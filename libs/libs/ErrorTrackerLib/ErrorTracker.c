#include "errortracker.h"

#include "ErrorTrackerDB.h"
#include "ErrorEntry.h"

#include "etTrivia.h"
#include "ErrorTrackerLib.h"
#include "ErrorTrackerLibPrivate.h"
#include "Search.h"
#include "timing.h"
#include "utils.h"
#include "error.h"
#include "MemoryMonitor.h"
#include "file.h"
#include "FolderCache.h"
#include "earray.h"
#include "sysutil.h"
#include "estring.h"
#include "gimmeDLLWrapper.h"
#include "winutil.h"
#include "ServerLib.h"
#include "stashtable.h"
#include "process_util.h"
#include "textparser.h"
#include "HashFunctions.h"
#include "errornet.h"
#include "callstack.h"
#include "timing.h"
#include "autogen/errornet_h_ast.h"
#include "autogen/callstack_h_ast.h"
#include "autogen/trivia_h_ast.h"

#include "ETCommon/symstore.h"
#include "ETCommon/ETShared.h"
#include "ETCommon/ETIncomingData.h"
#include "ETCommon/ETDumps.h"
#include "ETCommon/ETWebCommon.h"

#include "Alerts.h"
#include "WebInterface.h"
#include "WebReport.h"
#include "AutoGen/ETIncomingData_h_ast.h"
#include "Backlog.h"
#include "email.h"
#include "blame.h"
#include "sock.h"
#include "hoglib.h"
#include "netipfilter.h"
#include "network/crypt.h"
#include <psapi.h>
#include "logging.h"
#include "StringCache.h"
#include "timing_profiler_interface.h"

#include "objSchema.h"
#include "objContainer.h"
#include "objContainerIO.h"
#include "objTransactions.h"
#include "fileutil2.h"
#include "fileutil.h"
#include "ResourceInfo.h"
#include "objBackupCache.h"

#include "UTF8.h"

#include "jira.h"
#include "AutoGen/jira_h_ast.h"

#include "AutoGen/ErrorTracker_h_ast.h"
#include "AutoGen/ErrorTrackerLib_autotransactions_autogen_wrappers.h"

#define CONTAINER_ENTRY_ID 1
#define CONTAINER_ENTITY_ID 2

// Few is going to be "two" for now
#define EVERY_FEW_MINUTES_IN_SECONDS (120)

#define HOUR_OF_DAY_WHEN_NIGHTLY_REPORTS_OCCUR (6)

#define NONFATAL_ERROR_STARTUP_AGE_CUTOFF (1 * 24 * 60 * 60) // 1 day, in seconds

static int siNonFatalErrorsSeen = 0;
static int siFatalErrorsSeen    = 0;
static unsigned int suLastErrorCountTick = 0;
static bool sbExitTriggered = false; // used for exiting out of persisted thread loops
bool ErrorTrackerExitTriggered(void) { return sbExitTriggered; }

int iXDayCount = 2;
AUTO_CMD_INT(iXDayCount, PreviousDaysToCount);

bool gbMergeTriviaFileToContainer = false;
AUTO_CMD_INT(gbMergeTriviaFileToContainer, MergeTriviaData) ACMD_CMDLINE;

// Maximum age of error reports, based on date/time of most recent occurance of error
U32 gbErrorAgeCutoffDays = 45;
AUTO_CMD_INT(gbErrorAgeCutoffDays, ErrorAgeCutoffDays) ACMD_CMDLINE;

void launchSymServLookup(void);
static NetComm *symsrvComm = NULL;
extern int gUseRemoteSymServ;
extern int gMaxInfoEntries;

extern int giNumSlaves;
extern bool gMasterETDisconnected;
extern IncomingClientState *currContext;

// -------------------------------------------------------------------------------------

// Source Controlled
//char gErrorTrackerDataDir[MAX_PATH] = "C:\\Core\\data\\server\\ErrorTracker\\Data\\";
//AUTO_CMD_STRING(gErrorTrackerDataDir, errorTrackerDataDir);

// Not Source Controlled
//char gErrorTrackerAltDataDir[MAX_PATH] = "C:\\Core\\ErrorTracker\\Data\\";
//AUTO_CMD_STRING(gErrorTrackerAltDataDir, errorTrackerAltDataDir);
char gErrorTrackerDBBackupDir[MAX_PATH] = "N:\\ErrorTracker\\DB\\";
AUTO_CMD_STRING(gErrorTrackerDBBackupDir, errorTrackerDBBackupDir);
//char gErrorTrackerPDBTempDir[MAX_PATH] = "C:\\Core\\ErrorTracker\\PDBtemp\\";
//AUTO_CMD_STRING(gErrorTrackerPDBTempDir, errorTrackerPDBTempDir);

static const char *getBacklogFilename()
{
	static char backlogFilename[MAX_PATH] = {0};
	if(!backlogFilename[0])
	{
		sprintf_s(SAFESTR(backlogFilename), "%sbacklog.dat", errorTrackerGetDatabaseDir());
	}
	return backlogFilename;
}

const char *getRawDataDir(void)
{
	return gErrorTrackerSettings.pRawDataDir;
}

int gPidSymSrv = 0;

#ifdef _FULLDEBUG
static char *sSymSrvCmd= "SymServLookupFD.exe";
static char *sSymSrvX64Cmd= "SymServLookupX64FD.exe";
#else
static char *sSymSrvCmd= "SymServLookup.exe";
static char *sSymSrvX64Cmd= "SymServLookupX64.exe";

#endif

CRITICAL_SECTION gSymSrvQueue;
CRITICAL_SECTION gMergeQueueCritical;
CRITICAL_SECTION gDumpReprocessCritical;
CRITICAL_SECTION gNewQueueCritical;
CRITICAL_SECTION gSVNBlameCritical;

AUTO_RUN;
void initErrorTrackerCriticalSections(void)
{
	InitializeCriticalSection(&gSymSrvQueue);
	InitializeCriticalSection(&gMergeQueueCritical);
	InitializeCriticalSection(&gDumpReprocessCritical);
	InitializeCriticalSection(&gNewQueueCritical);
	InitializeCriticalSection(&gSVNBlameCritical);
}

void ErrorTrackerMergeFixup()
{
	ContainerIterator iter = {0};
	Container *currCon = NULL;
	int count = 0;

	objInitContainerIteratorFromType(GLOBALTYPE_ERRORTRACKERENTRY, &iter);
	currCon = objGetNextContainerFromIterator(&iter);
	while (currCon)
	{
		ErrorEntry *pEntry = CONTAINER_ENTRY(currCon);
		if (pEntry->uMergeID == pEntry->uID)
		{
			AutoTrans_trErrorEntry_MergeFixup(NULL, GLOBALTYPE_ERRORTRACKER, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID);
			count++;
		}

		currCon = objGetNextContainerFromIterator(&iter);
	}
	objClearContainerIterator(&iter);
	printf("Fixup complete, %d entries restored.", count);
}

// -------------------------------------------------------------------------------------
extern ErrorTrackerContext *gpCurrentContext;
extern ParseTable parse_TriviaData[];
#define TYPE_parse_TriviaData TriviaData

extern StashTable dumpIDToDumpDataTable;
extern StashTable errorSourceFileLineTable;
// -------------------------------------------------------------------------------------

static void destroyStringArray(char *pString)
{
	free(pString);
}

void dumpStackTraceLines (char **estr, ErrorEntry *pEntry)
{
	int i;
	estrClear(estr);

	for (i=0; i<eaSize(&pEntry->ppStackTraceLines); i++)
	{
		estrConcatf(estr, "%d %s; Module: %s\n"
			"\tFile: %s ; Line: %d\n",
			 i+1,
			 pEntry->ppStackTraceLines[i]->pFunctionName, 
			 pEntry->ppStackTraceLines[i]->pModuleName, 
			 pEntry->ppStackTraceLines[i]->pFilename,
			 pEntry->ppStackTraceLines[i]->iLineNum);
	}
}

// -------------------------------------------------------------------------------------
// Retrieving entries

// Deprecated function for performMigration()
ErrorEntry * getErrorTrackerEntryByIndex(ErrorTrackerContext *pContext, int i)
{
	if (pContext == NULL || pContext->entryList.ppEntries == NULL || 
		i < 0 || i > eaSize(&pContext->entryList.ppEntries))
		return NULL;
	return (ErrorEntry *) pContext->entryList.ppEntries[i];
}

void errorTrackerSaveAndClose(void)
{
	objContainerSaveTick();
	objFlushContainers();
	objCloseContainerSource();
}

// -------------------------------------------------------------------------------

void errorTrackerEntryDeleteDumps(ErrorEntry *pEntry)
{
	if (pEntry->ppDumpData || pEntry->ppMemoryDumps)
	{
		int ret = 0;
		char cmdBuffer[512];
		char dumpPath[MAX_PATH];
		GetErrorEntryDir(gErrorTrackerSettings.pDumpDir, pEntry->uID, SAFESTR(dumpPath));
		backSlashes(dumpPath);
		if (dirExists(dumpPath))
		{
			sprintf(cmdBuffer, "rmdir /s /q %s", dumpPath);
			ret = system(cmdBuffer);
		}
	}
}

void errorTrackerEntryDeleteEx (ErrorEntry *pEntry, bool bDeleteDumps, const char *file, int line)
{
	if (!pEntry)
		return;
	if (bDeleteDumps)
	{
		errorTrackerEntryDeleteDumps(pEntry);
	}
	removeEntryFromSortedSearch(pEntry);
	removeEntryFromStashTables(CONTAINER_NOCONST(ErrorEntry, pEntry));
	ErrorEntry_RemoveHashStash(pEntry);
	{
		char path[MAX_PATH];
		sprintf(path, "%s\\delete.log", errorTrackerGetDatabaseDir());
		filelog_printf(path, "Deleted #%d from %s on line %d\n", pEntry->uID, file, line);
	}
	objRequestContainerDestroyLocal(NULL, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID);
}

void errorTrackerEntryCreateStub(ErrorEntry *pEntry, U32 uMergeID)
{
	errorTrackerEntryDeleteDumps(pEntry);
	removeEntryFromSortedSearch(pEntry);
	removeEntryFromStashTables(CONTAINER_NOCONST(ErrorEntry, pEntry));
	ErrorEntry_RemoveHashStash(pEntry);
	AutoTrans_trErrorTracker_CreateStub(NULL, GLOBALTYPE_ERRORTRACKER, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID, uMergeID);
}

AUTO_TRANSACTION ATR_LOCKS(pEntry, ".aiUniqueHash, .aiUniqueHashNew, .uMergeID");
enumTransactionOutcome trErrorTracker_CreateStub (ATR_ARGS, NOCONST(ErrorEntry) *pEntry, U32 uMergeID)
{
	pEntry->aiUniqueHash[0] = pEntry->aiUniqueHash[1] = pEntry->aiUniqueHash[2] = pEntry->aiUniqueHash[3] = 0;
	if (pEntry->aiUniqueHashNew)
	{
		pEntry->aiUniqueHashNew[0] = pEntry->aiUniqueHashNew[1] = pEntry->aiUniqueHashNew[2] = pEntry->aiUniqueHashNew[3] = 0;
	}
	pEntry->uMergeID = uMergeID;
	return TRANSACTION_OUTCOME_SUCCESS;
}

void getErrorEntryFilename(char *filename, size_t filename_size, U32 uID, U32 uIndex)
{
	char file[20];
	GetErrorEntryDir(getRawDataDir(), uID, SAFESTR2(filename));
	sprintf_s(SAFESTR(file), "\\%d.ee", uIndex);
	strcat_s(SAFESTR2(filename), file);
}

bool loadErrorEntry(U32 uID, U32 uIndex, NOCONST(ErrorEntry) *output)
{
	char filename[MAX_PATH];

	PERFINFO_AUTO_START_FUNC();

	getErrorEntryFilename(SAFESTR(filename), uID, uIndex);

	if(ParserReadTextFile(filename, parse_ErrorEntry, output, 0))
	{
		output->uID = uID;
		PERFINFO_AUTO_STOP_FUNC();
		return true;
	}
	else
	{
		sprintf_s(SAFESTR(filename), "%s\\%d\\%d.ee", getRawDataDir(), uID, uIndex);
		if(ParserReadTextFile(filename, parse_ErrorEntry, output, 0))
		{
			output->uID = uID;
			PERFINFO_AUTO_STOP_FUNC();
			return true;
		}
	}

	PERFINFO_AUTO_STOP_FUNC();
	return false;
}

void recordErrorEntry(U32 uID, U32 uIndex, ErrorEntry *pEntry)
{
	if (pEntry && (errorTrackerLibGetOptions() & ERRORTRACKER_OPTION_RECORD_ALL_ERRORDATA_TO_DISK)
		&& (pEntry->eType != ERRORDATATYPE_ERROR)) // Too spammy, not useful enough
	{
		char filename[MAX_PATH];
		getErrorEntryFilename(SAFESTR(filename), uID, uIndex);
		mkdirtree(filename);
		ParserWriteTextFile(filename, parse_ErrorEntry, pEntry, 0, 0);
		backlogReceivedNewError(uID, uIndex, pEntry);
	}
}

void errorTrackerOnNewError(ErrorTrackerContext *pContext, NOCONST(ErrorEntry) *pNewEntry, ErrorEntry *pMergedEntry)
{
	PERFINFO_AUTO_START_FUNC();
	errorTrackerLibCallNewEntryCallback(pContext, pNewEntry, pMergedEntry);
	sendEmailsOnNewError(CONST_ENTRY(pNewEntry), pMergedEntry);
	PERFINFO_AUTO_STOP();
}

bool entryShouldBeThrownOut(ErrorEntry *pEntry)
{
	if(pEntry->pLastBlamedPerson)
	{
		// This person doesn't have the latest data. No thanks!
		if(strstri(pEntry->pLastBlamedPerson, "do not have"))
			return true;
		// This person is using a new, locally created file. No thanks!
		if(strstri(pEntry->pLastBlamedPerson, "not in database"))
			return true;
	}
	return false;
}

static bool IsStackTraceEmpty(CONST_EARRAY_OF(StackTraceLine) ppStackTraceLines)
{
	int i;
	for (i=0; i<eaSize(&ppStackTraceLines); i++)
	{
		// if there's a line number anywhere, then the stack trace isn't empty
		if (ppStackTraceLines[i]->iLineNum > 0)
			return false;
	}
	return true;
}

AUTO_TRANSACTION ATR_LOCKS(pEntry, ".*");
enumTransactionOutcome trErrorEntry_MergeFixup(ATR_ARGS, NOCONST(ErrorEntry) *pEntry)
{
	char *pCRCNewString = NULL;

	ET_ConstructHashString(&pCRCNewString, pEntry, true);

	// -------------------------------------------------------------------------------
	// Unique ID generation

	strupr(pCRCNewString);
	cryptMD5(pCRCNewString, (int)strlen(pCRCNewString), pEntry->aiUniqueHashNew);

	pEntry->uMergeID = 0;
	pEntry->iTotalCount = pEntry->iTotalCount / 2;

	pEntry->uHashVersion = ET_LATEST_HASH_VERSION;
	estrDestroy(&pCRCNewString);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(pEntry, ".ppDumpData, .iTotalCount, .iFullDumpCount, .iMiniDumpCount");
enumTransactionOutcome trErrorEntry_AddDump(ATR_ARGS, NOCONST(ErrorEntry) *pEntry, NON_CONTAINER DumpData *pDumpData, int bIncrementCount)
{
	NOCONST(DumpData) *pData = StructCreateNoConst(parse_DumpData);
	StructCopyDeConst(parse_DumpData, pDumpData, pData, 0, 0, 0);
	pData->iDumpArrayIndex = eaSize(&pEntry->ppDumpData);
	eaPush(&pEntry->ppDumpData, pData);
	if (pDumpData->bWritten && (pDumpData->uFlags & DUMPDATAFLAGS_FULLDUMP))
		pEntry->iFullDumpCount++;
	else if (pDumpData->bWritten)
		pEntry->iMiniDumpCount++;
	if (bIncrementCount)
		pEntry->iTotalCount += 1;
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(pEntry, ".ppRecentErrors");
enumTransactionOutcome trErrorEntry_AddRecentError(ATR_ARGS, NOCONST(ErrorEntry) *pEntry, NON_CONTAINER ErrorEntry *pError, int iETIndex, int maxErrors)
{
	int i;
	int iSizeDiff = eaSize(&pEntry->ppRecentErrors) - maxErrors;
	NOCONST(ErrorEntry) *pData = StructCreateNoConst(parse_ErrorEntry);
	StructCopyDeConst(parse_ErrorEntry, pError, pData, 0, 0, 0);
	pData->iETIndex = iETIndex;
	if (iSizeDiff == 0) 
	{
		StructDestroyNoConst(parse_ErrorEntry, pEntry->ppRecentErrors[0]);
		eaRemove(&pEntry->ppRecentErrors, 0);
	}
	else if (iSizeDiff > 0)
	{
		for (i = 0; i <= iSizeDiff; i++)
		{
			StructDestroyNoConst(parse_ErrorEntry, pEntry->ppRecentErrors[i]);
		}
		eaRemoveRange(&pEntry->ppRecentErrors, 0, iSizeDiff + 1);
	}
	eaPush(&pEntry->ppRecentErrors, pData);
 	
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(pEntry, ".ppMemoryDumps, .iMemoryDumpCount");
enumTransactionOutcome trErrorEntry_AddMemoryDump(ATR_ARGS, NOCONST(ErrorEntry) *pEntry, int iDumpIndex)
{
	NOCONST(MemoryDumpData) *memDump = StructCreateNoConst(parse_MemoryDumpData);
	memDump->iDumpIndex = iDumpIndex;
	memDump->uTimeReceived = timeSecondsSince2000();
	
	eaPush(&pEntry->ppMemoryDumps, memDump);
	pEntry->iMemoryDumpCount++;
	return TRANSACTION_OUTCOME_SUCCESS;
}

void RespondToError (NetLink *link, IncomingClientState *pClientState, NOCONST(ErrorEntry) *pEntry, ErrorEntry *pMergedEntry, int iETIndex, NOCONST(TriviaData) **ppTriviaData)
{
	DumpData *pDumpData = NULL;
	Packet *pak = NULL;
	int iDumpFlags = 0;
	int iDumpIndex = -1;
	bool bIsNewCrash = (pEntry == NULL);

	if (!pEntry) // The "Merged" IS the new entry
	{
		// pEntry is already owned. Dupe it for the dump.
		pEntry = StructCreateNoConst(parse_ErrorEntry);
		StructCopyFieldsDeConst(parse_ErrorEntry, pMergedEntry, pEntry, 0, 0);
		if ((errorTrackerLibGetOptions() & ERRORTRACKER_OPTION_LOG_TRIVIA) == 0)
			 eaCopyStructsNoConst(&ppTriviaData, &pEntry->ppTriviaData, parse_TriviaData);
	}
	assert(pMergedEntry && pEntry);
	pEntry->uID = pMergedEntry->uID;

	// Notify the client what kind of dumps we want.
	// Note: ERRORF types aren't worthy of a dump.
	if(ERRORDATATYPE_IS_A_CRASH(pMergedEntry->eType))
	{
		iDumpFlags = calcRequestedDumpFlags(pMergedEntry, CONST_ENTRY(pEntry));
		// Setup the dump ID
		if(iDumpFlags == 0)
		{
			// No dump needed, uDumpID is irrelevant.
			pClientState->uDumpID = 0;
			if (!ipfIsLocalIp(linkGetIp(link)))
				iDumpFlags |= DUMPFLAGS_EXTERNAL;
			if (gErrorTrackerSettings.iMaxInfoEntries > 0 && pMergedEntry->iTotalCount > 1)
				AutoTrans_trErrorEntry_AddRecentError(NULL, GLOBALTYPE_ERRORTRACKER, GLOBALTYPE_ERRORTRACKERENTRY, 
					pMergedEntry->uID, (ErrorEntry*) pEntry, iETIndex, gErrorTrackerSettings.iMaxInfoEntries);
		}
		else
		{
			// We're going to want a dump. Make a new DumpData and give it a copy of pEntry 
			// (the incoming entry data), or just pEntry if gpCurrentContext->entryList didn't take 
			// ownership of it already.
			if (!ipfIsLocalIp(linkGetIp(link)))
				iDumpFlags |= DUMPFLAGS_EXTERNAL;

			if (eaSize(&pEntry->ppPlatformCounts) == 1 &&
				pEntry->ppPlatformCounts[0]->ePlatform == PLATFORM_XBOX360)
			{  	// for XBox crashes deferred dumps send over the error data again
				// and do NOT need a DumpData struct now, or a dump ID
				pClientState->uDumpID = 0; 
			}
			else
			{
				NOCONST(DumpData) *pData = StructCreateNoConst(parse_DumpData);
				pData->uFlags =  (iDumpFlags & DUMPFLAGS_FULLDUMP) ? DUMPDATAFLAGS_FULLDUMP : 0;
				pData->iETIndex = iETIndex;
				pData->pEntry = pEntry;

				AutoTrans_trErrorEntry_AddDump(NULL, GLOBALTYPE_ERRORTRACKER, GLOBALTYPE_ERRORTRACKERENTRY, 
					pMergedEntry->uID, (DumpData*) pData, false);
				assert(pMergedEntry->ppDumpData);
				iDumpIndex = eaSize(&pMergedEntry->ppDumpData) - 1;
				pClientState->uDumpID = getNewDumpID(pMergedEntry->ppDumpData[iDumpIndex]);

				pData->pEntry = NULL;
				StructDestroyNoConst(parse_DumpData, pData);
			}
		}

		iDumpFlags |= DUMPFLAGS_UNIQUEID|DUMPFLAGS_DUMPINDEX; // Sending the unique ID and actual dump index
		SendDumpFlagsWithContext(link, iDumpFlags, pClientState->uDumpID, pMergedEntry->uID, iDumpIndex, pClientState->context);
		if (giNumSlaves > 0)
			addDumpID(pClientState->context, pClientState->uDumpID, true, NULL);
	}
	else
	{
		if (gErrorTrackerSettings.iMaxInfoEntries > 0 && pMergedEntry->iTotalCount > 1)
			AutoTrans_trErrorEntry_AddRecentError(NULL, GLOBALTYPE_ERRORTRACKER, GLOBALTYPE_ERRORTRACKERENTRY, 
				pMergedEntry->uID, (ErrorEntry*) pEntry, iETIndex, 1);
		// Just send the unique ID ... errors are still interested
		SendDumpFlags(link, DUMPFLAGS_UNIQUEID, 0, pMergedEntry->uID, 0);
	}

	if ( !pMergedEntry->bMustFindProgrammer && ERRORDATATYPE_IS_A_CRASH(pMergedEntry->eType) && pMergedEntry->iTotalCount == 5 && 
		eaSize(&pMergedEntry->ppUserInfo) > 1 && !(pMergedEntry->iFullDumpCount + pMergedEntry->iMiniDumpCount) )
	{
		objRequestTransactionSimplef(NULL, GLOBALTYPE_ERRORTRACKERENTRY, pMergedEntry->uID, 
			"setFindProgrammer", "set bMustFindProgrammer = %d", true);
	}

	if (ERRORDATATYPE_IS_A_CRASH(pMergedEntry->eType) && IsStackTraceEmpty(CONST_ENTRY(pEntry)->ppStackTraceLines))
	{
		// Set "bug programmer" flag and send NULL stack message
		char *pErrString = NULL;
		estrCreate(&pErrString);
		estrPrintf(&pErrString, "There is no callstack information (NULL Callstack)");
		pak = pktCreate(link, FROM_ERRORTRACKER_ERRRESPONSE);
		pktSendU32(pak, ERRORRESPONSE_NULLCALLSTACK);
		pktSendString(pak, pErrString);
		pktSend(&pak);
		estrDestroy(&pErrString);

		// This flag defaults to true for new NULL callstack crashes
		if (bIsNewCrash && !pMergedEntry->bMustFindProgrammer)
			objRequestTransactionSimplef(NULL, GLOBALTYPE_ERRORTRACKERENTRY, pMergedEntry->uID, 
				"setFindProgrammer", "set bMustFindProgrammer = %d", true);
	}
	else if (pMergedEntry->bMustFindProgrammer)
	{
		char *pErrString = NULL;
		estrCreate(&pErrString);
		estrConcatf(&pErrString, "%s has requested that you leave this open and \nfind a programmer to debug the crash.",
			pMergedEntry->pProgrammerName ? pMergedEntry->pProgrammerName : "Software");
		pak = pktCreate(link, FROM_ERRORTRACKER_ERRRESPONSE);
		pktSendU32(pak, ERRORRESPONSE_PROGRAMMERREQUEST);
		pktSendString(pak, pErrString);
		pktSend(&pak);
		estrDestroy(&pErrString);
	}
	else
	{
		char *stackdump = NULL;
		if (pEntry && ERRORDATATYPE_IS_A_CRASH(pMergedEntry->eType) && !(iDumpFlags & DUMPFLAGS_EXTERNAL))
			dumpStackTraceLines (&stackdump, CONST_ENTRY(pEntry));
		else
			estrCopy2(&stackdump, "");
		pak = pktCreate(link, FROM_ERRORTRACKER_ERRRESPONSE);
		pktSendU32(pak, 0);
		pktSendString(pak, stackdump);
		pktSend(&pak);
		estrDestroy(&stackdump);
	}

	// The transaction creates a copy of it
	StructDestroyNoConst(parse_ErrorEntry, pEntry);
}

void ProcessEntry(NetLink *link, IncomingClientState *pClientState, NOCONST(ErrorEntry) *pEntry)
{
	ErrorEntry *pMergedEntry = NULL;
	bool bFoundOldMerge = false;
	PERFINFO_AUTO_START_FUNC();
	errorTrackerSendStatusUpdate(link, STATE_ERRORTRACKER_MERGE);
	
	if (pEntry == NULL)
	{   // createErrorEntryFromErrorData() didn't like it. This should already have been checked.
		sendFailureResponse(link);
		PERFINFO_AUTO_STOP();
		return;
	}

	// Errors in an out-of-date data file get tossed when DISCARD_NOT_LATEST is not disabled
	if( !(errorTrackerLibGetOptions() & ERRORTRACKER_OPTION_DISABLE_DISCARD_NOT_LATEST) && entryShouldBeThrownOut(CONST_ENTRY(pEntry)))
	{
		char *pErrString = NULL;
		Packet *pak;
		StructDestroyNoConst(parse_ErrorEntry, pEntry);

		SendDumpFlags(link, DUMPFLAGS_UNIQUEID, 0, 0, 0);
		estrCreate(&pErrString);
		estrPrintf(&pErrString, "File is not the most recent version.\nNo Error Tracker entry created.");
		pak = pktCreate(link, FROM_ERRORTRACKER_ERRRESPONSE);
		pktSendU32(pak, ERRORRESPONSE_OLDVERSION);
		pktSendString(pak, pErrString);
		pktSend(&pak);
		estrDestroy(&pErrString);
		PERFINFO_AUTO_STOP();
		return;
	}

	// GameBugs have been repurposed to be manual userdumps, and should always be unique!
	if(pEntry->eType != ERRORDATATYPE_GAMEBUG) 
	{
		pMergedEntry = findErrorTrackerEntryFromNewEntry(pEntry);
		if (pMergedEntry && (hasHash(pMergedEntry->aiUniqueHash) || !hasHash(pMergedEntry->aiUniqueHashNew)) && hasHash(pEntry->aiUniqueHashNew))
		{
			AutoTrans_trErrorEntry_AddNewHash(NULL, GLOBALTYPE_ERRORTRACKER, GLOBALTYPE_ERRORTRACKERENTRY, pMergedEntry->uID, 
											  pEntry->aiUniqueHashNew[0], pEntry->aiUniqueHashNew[1], pEntry->aiUniqueHashNew[2], pEntry->aiUniqueHashNew[3]);

			AutoTrans_trErrorEntry_UpdateMergedEntry(NULL, GLOBALTYPE_ERRORTRACKER, GLOBALTYPE_ERRORTRACKERENTRY, pMergedEntry->uID, pEntry->pLargestMemory);
		}
		if (!pMergedEntry && hasHash(pEntry->aiUniqueHashNew)) //If we didn't find a match, remove the old hash from our new entry, we no longer want it.
		{
			pEntry->aiUniqueHash[0] = pEntry->aiUniqueHash[1] = pEntry->aiUniqueHash[2] = pEntry->aiUniqueHash[3] = 0;
		}
	}

	if (ERRORDATATYPE_IS_A_CRASH(pEntry->eType) && errorTrackerIsGenericHash(CONST_ENTRY(pEntry), &pEntry->pGenericLabel))
	{	// Make NULL callstack dumps unique, with no hash (only for persisted builds (eg. non-CB))
		estrDestroy(&pEntry->pStashString);
		AutoTrans_trErrorEntry_RemoveHash(NULL, GLOBALTYPE_ERRORTRACKER, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID);
		pMergedEntry = NULL; // clear this so it creates a new one for the NULL callstack
	}

	if (pMergedEntry) // Container Object processing
	{
		PERFINFO_AUTO_START("ET_MergeError", 1);
		AddClientLink(link);
		ErrorEntry_AddMergeQueue(link ? linkID(link) : 0, pMergedEntry, pEntry);
		PERFINFO_AUTO_STOP();
	}
	else
	{
		PERFINFO_AUTO_START("ET_NewError", 1);
		AddClientLink(link);
		ErrorEntry_AddNewQueue(link ? linkID(link) : 0, pEntry);
		PERFINFO_AUTO_STOP();
	}
	PERFINFO_AUTO_STOP();
}

AUTO_TRANSACTION ATR_LOCKS(pEntry, ".ppStackTraceLines");
enumTransactionOutcome trErrorEntry_SetStackTrace(ATR_ARGS, NOCONST(ErrorEntry) *pEntry, NON_CONTAINER StackTraceLineList *pLineList)
{
	if (pEntry->ppStackTraceLines)
		eaDestroyStructNoConst(&pEntry->ppStackTraceLines, parse_StackTraceLine);
	eaCopyStructs(&pLineList->ppStackTraceLines, &((StackTraceLine**)(pEntry->ppStackTraceLines)), parse_StackTraceLine);
	return TRANSACTION_OUTCOME_SUCCESS;
}

static void errorTrackerReportUnknownServer(U32 uIP, ErrorEntry *pMergedEntry, NOCONST(ErrorEntry) *pEntry)
{
	ErrorEntry *pData = pEntry ? CONTAINER_RECONST(ErrorEntry, pEntry) : pMergedEntry;
	struct in_addr ina = {0};
	ina.S_un.S_addr = uIP;
	SERVLOG_PAIRS(LOG_ERRORTRACKER_UNKNOWNSERVER, "ErrorTracker_UnknownServer", 
		("etid", "%d", pMergedEntry->uID)
		("version", "%s", eaSize(&pData->ppVersions) ? pData->ppVersions[0] : "Unknown")
		("exe", "%s", eaSize(&pData->ppExecutableNames) ? pData->ppExecutableNames[0] : "Unknown")
		("ip", "%s", inet_ntoa(ina))
		("user", "%s", eaSize(&pData->ppUserInfo) ? pData->ppUserInfo[0]->pUserName : "-"));
	AddUnknownIP(uIP, pEntry ? CONTAINER_RECONST(ErrorEntry, pEntry) : pMergedEntry, pMergedEntry->uID);
}

void ProcessEntry_Finish (NetLink *link, ErrorEntry *pMergedEntry, NOCONST(ErrorEntry) *pEntry, int iETIndex, NOCONST(TriviaData) **ppTriviaData)
{
	IncomingClientState *pClientState = NULL;
	ErrorTrackerContext *pContext = errorTrackerLibGetCurrentContext();
	PERFINFO_AUTO_START_FUNC();
	if (link)
	{
		pClientState = linkGetUserData(link);
		if (!pClientState) // Treat link as bad if there is no Link data
		{
			if (!currContext)
				link = NULL;
			else
				pClientState = currContext;
		}
	}
	addEntryToSortedSearch(pMergedEntry, !!pEntry);
	pContext->entryList.bSomethingHasChanged = true;
	
	if (ipfGroupExists("CrypticServers"))
	{
		U32 uIP;
		CONST_EARRAY_OF(IPCount) eaIPs = pEntry ? (CONST_EARRAY_OF(IPCount)) pEntry->ppIPCounts : pMergedEntry->ppIPCounts;
		uIP = eaSize(&eaIPs) > 0 ? eaIPs[0]->uIP : link ? linkGetIp(link) : 0;
		if (uIP && !ipfIsLocalIp(uIP) && !ipfIsTrustedIp(uIP) && !ipfIsIpInGroup("CrypticServers", uIP))
		{
			if (!etExeIsClient(pEntry ? CONTAINER_RECONST(ErrorEntry, pEntry) : pMergedEntry))
				errorTrackerReportUnknownServer(uIP, pMergedEntry, pEntry);
		}
	}

	if(pMergedEntry->eType == ERRORDATATYPE_ERROR)
		siNonFatalErrorsSeen++;
	else siFatalErrorsSeen++;

	if (pEntry && pMergedEntry->eType == ERRORDATATYPE_ERROR && !pMergedEntry->ppStackTraceLines && pEntry->ppStackTraceLines)
	{
		StackTraceLineList stack = {0};
		stack.ppStackTraceLines = (StackTraceLine**) pEntry->ppStackTraceLines;
		AutoTrans_trErrorEntry_SetStackTrace(NULL, GLOBALTYPE_ERRORTRACKER, GLOBALTYPE_ERRORTRACKERENTRY, 
			pMergedEntry->uID, &stack);
		eaDestroyStructNoConst(&pEntry->ppStackTraceLines, parse_StackTraceLine);
	}

	// Calls various callbacks, sends emails
	errorTrackerOnNewError(errorTrackerLibGetCurrentContext(), pEntry, pMergedEntry);

	if (linkConnected(link) && pMergedEntry)
		RespondToError(link, pClientState, pEntry, pMergedEntry, iETIndex, ppTriviaData);
	else
	{
		/*if (!linkConnected(link))
			printf("  Link closed, no response sent!\n");
		if (!pMergedEntry)
			printf("  Error merging entry!\n");*/
		if (pEntry && pEntry != UNCONST_ENTRY(pMergedEntry))
			StructDestroyNoConst(parse_ErrorEntry, pEntry);
	}
	PERFINFO_AUTO_STOP();
}

static __forceinline void errorTrackerSendReceipt (NetLink *link) { 
	Packet *pkt = pktCreate(link, FROM_ERRORTRACKER_RECEIVED);
	pktSend(&pkt);
	linkFlush(link);
}

void ErrorTracker_IncomingErrorData(IncomingData *pIncomingData, NetLink *link, IncomingClientState *pClientState)
{
	NOCONST(ErrorEntry) *pNewEntry = NULL;
	PERFINFO_AUTO_START_FUNC();
	errorTrackerSendStatusUpdate(link, STATE_ERRORTRACKER_PARSEENTRY);
	pNewEntry = createErrorEntry_ErrorTracker(link, pClientState, pIncomingData->pErrorData, 0);

	if (!pNewEntry)
	{
		sendFailureResponse(link);
		return;
	}

	if (!pNewEntry->bDelayResponse)
	{
		ProcessEntry(link, pClientState, pNewEntry);
	}
	else
	{
		pIncomingData->pErrorData = NULL; // do not destroy this
		errorTrackerSendStatusUpdate(link, STATE_ERRORTRACKER_STACKWALK);
	}
	PERFINFO_AUTO_STOP();
}
void ErrorTracker_IncomingDump(IncomingData *pIncomingData, NetLink *link, IncomingClientState *pClientState)
{
	ErrorEntry *pEntry = NULL;
	DumpData *pDumpData = NULL;
	if(pClientState->pDeferredDumpData)
	{
		// Take ownership of the dump data
		pDumpData = (DumpData*) pClientState->pDeferredDumpData;
		pClientState->pDeferredDumpData = NULL;
		if (pClientState->uDeferredDumpEntryID)
			pEntry = findErrorTrackerByID(pClientState->uDeferredDumpEntryID);

		if (pDumpData && pEntry)
		{
			// Push the deferred dump data now; non-deferred dump datas should have already been pushed
			AutoTrans_trErrorEntry_AddDump(NULL, GLOBALTYPE_ERRORTRACKER, GLOBALTYPE_ERRORTRACKERENTRY, 
				pEntry->uID, pDumpData, false);
			StructDestroy(parse_DumpData, pDumpData);
			pDumpData = pEntry->ppDumpData[eaSize(&pEntry->ppDumpData)-1];
		}
		else
		{   // Ownership was already claimed, but never set since ErrorEntry doesn't exist
			StructDestroy(parse_DumpData, pDumpData);
			pDumpData = NULL;
		}
	}
	else
	{
		if(!stashIntFindPointer(dumpIDToDumpDataTable, pIncomingData->id, &pDumpData))
		{
			pDumpData = NULL;
		}
		else
		{
			if (!(pDumpData->uFlags & DUMPDATAFLAGS_FULLDUMP) || 
				pIncomingData->eType == INCOMINGDATATYPE_FULLDUMP_RECEIVED)
			{   // No use for the dumpTable stash entry now
				stashIntRemovePointer(dumpIDToDumpDataTable, pIncomingData->id, NULL);
			}
			if (pDumpData && pDumpData->pEntry)
				pEntry = findErrorTrackerByID(pDumpData->pEntry->uID);
		}
	}

	if(pDumpData != NULL)
	{
		char szFinalFilename[MAX_PATH];

		if(pEntry)
		{
			bool bFullDump = (pIncomingData->eType == INCOMINGDATATYPE_FULLDUMP_RECEIVED);
			int iDumpIndex = pDumpData->iDumpArrayIndex;
			bool bWritten = false; // true if requested is same as received

			if (!bFullDump && pDumpData->uFlags & DUMPDATAFLAGS_FULLDUMP)
			{
				objRequestTransactionSimplef(NULL, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID, 
					"setMiniDumpIndex", "set ppDumpData[%d].iMiniDumpIndex = %d", pDumpData->iDumpArrayIndex, 
					iDumpIndex);
			}
			else
			{
				objRequestTransactionSimplef(NULL, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID, 
					"setDumpIndex", "set ppDumpData[%d].iDumpIndex = %d", pDumpData->iDumpArrayIndex, 
					iDumpIndex);
				bWritten = true;
			}
			calcWriteDumpPath(SAFESTR(szFinalFilename), pEntry->uID, iDumpIndex, bFullDump);

			mkdirtree(szFinalFilename);
			DeleteFile_UTF8(szFinalFilename);
			if(MoveFile_UTF8(pIncomingData->pTempFilename, szFinalFilename))
			{
				if (gbETVerbose) printf("Received dump: %s\n", szFinalFilename);
				if (bFullDump && !(pDumpData->uFlags & DUMPDATAFLAGS_FULLDUMP))
				{
					objRequestTransactionSimplef(NULL, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID, 
						"setDumpFlags", "set ppDumpData[%d].uFlags = %d", pDumpData->iDumpArrayIndex, 
						pDumpData->uFlags | DUMPDATAFLAGS_FULLDUMP);
				}
				if (bWritten)
					objRequestTransactionSimplef(NULL, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID, 
					"setDumpWritten", "set ppDumpData[%d].bWritten = %d", pDumpData->iDumpArrayIndex, 
					true);
				else
					objRequestTransactionSimplef(NULL, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID, 
					"setMiniDumpWritten", "set ppDumpData[%d].bMiniDumpWritten = %d", pDumpData->iDumpArrayIndex, 
					true);

				if(pIncomingData->eType == INCOMINGDATATYPE_FULLDUMP_RECEIVED)
				{
					objRequestTransactionSimplef(NULL, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID, 
						"setFullDumpCount", "set iFullDumpCount = %d", pEntry->iFullDumpCount+1);
					if (pEntry->bFullDumpRequested)
						objRequestTransactionSimplef(NULL, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID, 
						"setFullDumpRequest", "set bFullDumpRequested = %d", false);
					objRequestTransactionSimplef(NULL, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID, 
						"setLastFullDump", "set uLastSavedDump = %d", timeSecondsSince2000());
				}
				else
				{
					objRequestTransactionSimplef(NULL, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID, 
						"setMiniDumpCount", "set iMiniDumpCount = %d", pEntry->iMiniDumpCount+1);
				}
				notifyDumpReceived(pEntry);
				if (ErrorEntry_isNullCallstack(pEntry))
					ReprocessDumpData(pEntry, iDumpIndex, link, true);
			}
			else
			{
				AssertOrAlert("MoveFile_UTF8Failed", "Could not move temporary file %s to final destination %s", pIncomingData->pTempFilename, szFinalFilename);
			}

			if (pClientState->pTempDescription)
			{
				ErrorEntry_EditDumpDescription(pEntry, pDumpData->iDumpArrayIndex, pClientState->pTempDescription);
			}
		}
	}
	// Always delete temp file even if MoveFile_UTF8 fails or never happened
	if (fileExists(pIncomingData->pTempFilename))
		DeleteFile_UTF8(pIncomingData->pTempFilename);
	errorTrackerSendReceipt(link);
}

void ErrorTracker_IncomingDumpCancel(IncomingData *pIncomingData, NetLink *link, IncomingClientState *pClientState)
{
	ErrorEntry *pEntry = NULL;
	DumpData *pDumpData = NULL;
	if (pClientState->pDeferredDumpData)
	{
		// Take ownership of the dump data
		pDumpData = (DumpData*) pClientState->pDeferredDumpData;
		pClientState->pDeferredDumpData = NULL;
		if (pClientState->uDeferredDumpEntryID)
			pEntry = findErrorTrackerByID(pClientState->uDeferredDumpEntryID);

		if (pDumpData && pEntry)
		{
			// Push the deferred dump data now; non-deferred dump datas should have already been pushed
			AutoTrans_trErrorEntry_AddDump(NULL, GLOBALTYPE_ERRORTRACKER, GLOBALTYPE_ERRORTRACKERENTRY, 
				pEntry->uID, pDumpData, false);
			StructDestroy(parse_DumpData, pDumpData);
			pDumpData = pEntry->ppDumpData[eaSize(&pEntry->ppDumpData)-1];
		}
		else
		{   // Ownership was already claimed, but never set since ErrorEntry doesn't exist
			StructDestroy(parse_DumpData, pDumpData);
			pDumpData = NULL;
		}
	}
	else
	{
		if(!stashIntFindPointer(dumpIDToDumpDataTable, pIncomingData->id, &pDumpData))
		{
			pDumpData = NULL;
		}
		else
		{
			// No use for the dumpTable stash entry now
			stashIntRemovePointer(dumpIDToDumpDataTable, pIncomingData->id, NULL);
			if (pDumpData)
				pEntry = findErrorTrackerByID(pDumpData->pEntry->uID);
		}
	}

	if (pDumpData != NULL && pEntry)
	{
		ANALYSIS_ASSUME(pDumpData != NULL);
		objRequestTransactionSimplef(NULL, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID, 
			"setDumpCancelled", "set ppDumpData[%d].bCancelled = %d", pDumpData->iDumpArrayIndex, true);
	}
	errorTrackerSendReceipt(link);
}
void ErrorTracker_IncomingMemDump(IncomingData *pIncomingData, NetLink *link, IncomingClientState *pClientState)
{
	ErrorEntry *pEntry = NULL;
	char szFinalFilename[MAX_PATH];

	pEntry = findErrorTrackerByID(pIncomingData->id);
	if (pEntry)
	{
		int iDumpIndex = pEntry->iMemoryDumpCount;

		calcMemoryDumpPath(SAFESTR(szFinalFilename), pEntry->uID, iDumpIndex);
		mkdirtree(szFinalFilename);
		DeleteFile_UTF8(szFinalFilename);

		if(MoveFile_UTF8(pIncomingData->pTempFilename, szFinalFilename))
		{
			AutoTrans_trErrorEntry_AddMemoryDump(NULL, GLOBALTYPE_ERRORTRACKER, GLOBALTYPE_ERRORTRACKERENTRY, 
				pEntry->uID, iDumpIndex);					
		}
		else
		{
			AssertOrAlert("MoveFile_UTF8Failed", "Could not move temporary file %s to final destination %s", pIncomingData->pTempFilename, szFinalFilename);
		}

		if (fileExists(pIncomingData->pTempFilename))
			DeleteFile_UTF8(pIncomingData->pTempFilename);
	}
	errorTrackerSendReceipt(link);
}
void ErrorTracker_IncomingDumpDescription(IncomingData *pIncomingData, NetLink *link, IncomingClientState *pClientState)
{
	ErrorEntry *pEntry = NULL;
	DumpData *pDumpData = NULL;
	if(!stashIntFindPointer(dumpIDToDumpDataTable, pIncomingData->id, &pDumpData))
		pDumpData = NULL;
	else
	{
		// No use for the dumpTable stash entry now
		stashIntRemovePointer(dumpIDToDumpDataTable, pIncomingData->id, NULL);
		if (pDumpData)
			pEntry = findErrorTrackerByID(pDumpData->pEntry->uID);
	}
	if (pDumpData && pEntry && pClientState->pTempDescription)
		ErrorEntry_EditDumpDescription(pEntry, pDumpData->iDumpArrayIndex, pClientState->pTempDescription);
	errorTrackerSendReceipt(link);
}
void ErrorTracker_IncomingDescriptionUpdate(IncomingData *pIncomingData, NetLink *link, IncomingClientState *pClientState)
{
	ErrorEntry *pEntry = NULL;
	DumpData *pDumpData = NULL;
	pEntry = findErrorTrackerByID(pIncomingData->id);
	if(pEntry)
	{
		U32 size = eaSize(&pEntry->ppDumpData);
		if((pIncomingData->index != (U32)-1) && (pIncomingData->index < size))
		{
			pDumpData = pEntry->ppDumpData[pIncomingData->index];
		}
	}
	if (pEntry && pClientState->pTempDescription) {
		if (pDumpData) {
			ErrorEntry_EditDumpDescription(pEntry, pDumpData->iDumpArrayIndex, pClientState->pTempDescription);
		}
		if (*pClientState->pTempDescription && strcmp(pClientState->pTempDescription, "--") != 0) {
			char buf[MAX_IP_STR];
			linkGetIpStr(link, buf, MAX_IP_STR);
			AutoTrans_trErrorEntry_AddTriviaDescription(NULL, GLOBALTYPE_ERRORTRACKER, GLOBALTYPE_ERRORTRACKERENTRY, 
				pEntry->uID, pClientState->pTempDescription, buf);
		}
	}
	errorTrackerSendReceipt(link);
}
void ErrorTracker_IncomingLinkDrop(IncomingData *pIncomingData, NetLink *link, IncomingClientState *pClientState)
{	
	SymSrvQueue_FindAndRemoveByLink(link);
}
void ErrorTracker_IncomingXperfFile(IncomingData *pIncomingData, NetLink *link, IncomingClientState *pClientState)
{
	ErrorEntry *pEntry = NULL;

	pEntry = findErrorTrackerByID(pIncomingData->id);
	if (pEntry && pIncomingData->pTempFilename && pIncomingData->pPermanentFilename)
	{
		char szFilename[MAX_PATH];
		char szExt[9];
		char szFullPath[MAX_PATH];
		int count = 0;
		char dumpDir[MAX_PATH];
		
		GetErrorEntryDir(gErrorTrackerSettings.pDumpDir, pEntry->uID, SAFESTR(dumpDir));

		fileSplitFilepath(pIncomingData->pPermanentFilename, szFilename, szExt);
		sprintf(szFullPath, "%s\\%s.gz", dumpDir, pIncomingData->pPermanentFilename);

		mkdirtree(szFullPath);
		while (fileExists(szFullPath))
		{
			sprintf(szFullPath, "%s\\%s_%d.%s.gz", dumpDir, szFilename, ++count, szExt);
		}		
		if(MoveFile_UTF8(pIncomingData->pTempFilename, szFullPath))
		{
			if (count)
				sprintf(szFullPath, "%s_%d.%s.gz", szFilename, count, szExt);
			else
				sprintf(szFullPath, "%s.gz", pIncomingData->pPermanentFilename);
			AutoTrans_trErrorEntry_AddXperfFile(NULL, GLOBALTYPE_ERRORTRACKER, GLOBALTYPE_ERRORTRACKERENTRY, 
				pEntry->uID, szFullPath);
		}
		else
			AssertOrAlert("MoveFile_UTF8Failed", "Could not move temporary file %s to final destination %s", pIncomingData->pTempFilename, szFullPath);

		if (fileExists(pIncomingData->pTempFilename))
			DeleteFile_UTF8(pIncomingData->pTempFilename);
	}
	errorTrackerSendReceipt(link);
}

AUTO_TRANSACTION ATR_LOCKS(pEntry, ".ppXperfDumps");
enumTransactionOutcome trErrorEntry_AddXperfFile(ATR_ARGS, NOCONST(ErrorEntry) *pEntry, const char *filename)
{
	NOCONST(XperfDumpData) *pData = StructCreateNoConst(parse_XperfDumpData);
	pData->filename = StructAllocString(filename);
	pData->uTimeReceived = timeSecondsSince2000();
	eaPush(&pEntry->ppXperfDumps, pData);
	return TRANSACTION_OUTCOME_SUCCESS;
}

#define NUM_INCOMING_DATATYPES 9
static IncomingDataType sIncomingDataTypes[NUM_INCOMING_DATATYPES] = {
	INCOMINGDATATYPE_ERRORDATA,
	INCOMINGDATATYPE_LINK_DROPPED,
	INCOMINGDATATYPE_MINIDUMP_RECEIVED,
	INCOMINGDATATYPE_FULLDUMP_RECEIVED,
	INCOMINGDATATYPE_MEMORYDUMP_RECEIVED,
	INCOMINGDATATYPE_DUMP_DESCRIPTION_RECEIVED,
	INCOMINGDATATYPE_DUMP_CANCELLED,
	INCOMINGDATATYPE_DUMP_DESCRIPTION_UPDATE,
	INCOMINGDATATYPE_XPERF_FILE,
};
static IncomingDataHandler sIncomingDataHandlers[NUM_INCOMING_DATATYPES] = {
	ErrorTracker_IncomingErrorData,
	ErrorTracker_IncomingLinkDrop,
	ErrorTracker_IncomingDump,
	ErrorTracker_IncomingDump,
	ErrorTracker_IncomingMemDump,
	ErrorTracker_IncomingDumpDescription,
	ErrorTracker_IncomingDumpCancel,
	ErrorTracker_IncomingDescriptionUpdate,
	ErrorTracker_IncomingXperfFile,
};

AUTO_RUN;
void ErrorTracker_InitializeIncomingDataFunctions(void)
{
	int i;
	for (i=0; i<NUM_INCOMING_DATATYPES; i++)
	{
		ETIncoming_SetDataTypeHandler(sIncomingDataTypes[i], sIncomingDataHandlers[i]);
	}
}

// -----------------------------------------------------------------------------------------
// Migration


void zipMigration(const char *srcname, const char *dstname)
{
	char buffer[1024];
	int len;
	FILE *src = NULL;
	FILE *dst = NULL;
	
	src = fopen(srcname, "rb");

	if(!src)
	{
		if (gbETVerbose) printf("ERROR: Failed to open '%s'\n", srcname);
		return;
	}

	dst = fopen(dstname, "wbz");

	if(!src)
	{
		fclose(src);
		if (gbETVerbose) printf("ERROR: Failed to open '%s'\n", dstname);
		return;
	}

	len = (int)fread(buffer, 1, 1024, src);
	while(len > 0)
	{
		fwrite(buffer, 1, len, dst);
		len = (int)fread(buffer, 1, 1024, src);
	}

	fclose(src);
	fclose(dst);
}

// deletes dumps for ETs with 1 occurrance and completely unuseful callstacks
static bool etMigrate_DeleteBadETDumps(ErrorEntry *pEntry)
{
	if (pEntry->iTotalCount > 1 || !ERRORDATATYPE_IS_A_CRASH(pEntry->eType))
		return false;
	if (eaSize(&pEntry->ppDumpData) == 0)
		return false;

	EARRAY_FOREACH_BEGIN(pEntry->ppStackTraceLines, i);
	{
		if (pEntry->ppStackTraceLines[i] && (strnicmp(pEntry->ppStackTraceLines[i]->pFunctionName, "0x", 2) != 0))
			return false;
	}
	EARRAY_FOREACH_END;
	errorTrackerEntryDeleteDumps(pEntry);
	// doesn't do anything else - dump data still exists
	return true;
}

AUTO_TRANSACTION ATR_LOCKS(pEntry, ".ppRecentErrors");
enumTransactionOutcome trDestroyRecentErrors(ATR_ARGS, NOCONST(ErrorEntry) *pEntry)
{
	eaDestroyStructNoConst(&pEntry->ppRecentErrors, parse_ErrorEntry);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(pEntry, ".eaProductOccurrences, .ppProductNames");
enumTransactionOutcome trMigrateProductNames(ATR_ARGS, NOCONST(ErrorEntry) *pEntry)
{
	EARRAY_CONST_FOREACH_BEGIN(pEntry->ppProductNames, i, s);
	{
		if (pEntry->ppProductNames[i] && *pEntry->ppProductNames[i])
			etAddProductCount(pEntry, pEntry->ppProductNames[i], 1);
	}
	EARRAY_FOREACH_END;
	eaDestroyEx(&pEntry->ppProductNames, NULL);
	return TRANSACTION_OUTCOME_SUCCESS;
}

#define DELAYED_MIGRATION_PER_FRAME_ENTRIES (1000)
static CONTAINERID_EARRAY sDelayedMigration = NULL;

static void runDelayMigration(void)
{
	ErrorEntry *pEntry;
	int count = 0, i;

	for (i=eaiSize(&sDelayedMigration)-1; i>=0 && count < DELAYED_MIGRATION_PER_FRAME_ENTRIES; i--)
	{
		bool bUpdated = false;
		pEntry = findErrorTrackerByID(sDelayedMigration[i]);
		if (pEntry)
		{
			if (eaSize(&pEntry->ppProductNames) > 0)
			{
				AutoTrans_trMigrateProductNames(NULL, objServerType(), GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID);
				bUpdated = true;
			}

			if (bUpdated)
				count++;
		}
	}
	if (count > 0)
		UpdateObjectTransactionManager();
	eaiRemoveRange(&sDelayedMigration, i+1, eaiSize(&sDelayedMigration)-i-1);
}

void performMigration(void)
{
	U32 uTotalContainersUpdated = 0;
	loadstart_printf("Migrating Error Tracker Entries... ");
	CONTAINER_FOREACH_BEGIN(GLOBALTYPE_ERRORTRACKERENTRY, etContainer);
	{
		ErrorEntry *pEntry = CONTAINER_ENTRY(etContainer);
		bool bUpdated = false;

		bUpdated = etMigrate_DeleteBadETDumps(pEntry);

		/*if (eaSize(&pEntry->ppRecentErrors) == 1)
		{
			AutoTrans_trDestroyRecentErrors(NULL, objServerType(), GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID);
			bUpdated = true;
		}
		if (eaSize(&pEntry->ppProductNames) > 0)
		{
			eaiPush(&sDelayedMigration, pEntry->uID);
		}*/

		if (pEntry->eType == ERRORDATATYPE_ASSERT)
		{
			AutoTrans_trErrorEntry_RecalcHash(NULL, GLOBALTYPE_ERRORTRACKER, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID);
			bUpdated = true;
		}

		if (bUpdated)
		{
			uTotalContainersUpdated++;
			UpdateObjectTransactionManager();
		}
		autoTimerThreadFrameEnd();
	}
	CONTAINER_FOREACH_END;
	loadend_printf("%u entries updated.", uTotalContainersUpdated);
}

void TallyTwoWeekCount()
{
	U32 timeCurrent = timeSecondsSince2000();
	ContainerIterator iter = {0};
	Container *currCon = NULL;

	objInitContainerIteratorFromType(GLOBALTYPE_ERRORTRACKERENTRY, &iter);
	currCon = objGetNextContainerFromIterator(&iter);
	while (currCon)
	{
		int i, iTwoWeekCount = 0;
		ErrorEntry *pEntry = CONTAINER_ENTRY(currCon);
		int iMinimumDaysAfterFirstTime = calcElapsedDays(pEntry->uFirstTime, timeCurrent);
		iMinimumDaysAfterFirstTime -= iXDayCount; // take off X days;
		
		for (i=eaSize(&pEntry->ppDayCounts)-1; i>=0; i--)
		{
			if (pEntry->ppDayCounts[i]->iDaysSinceFirstTime < iMinimumDaysAfterFirstTime)
				break;
			else
			{
				iTwoWeekCount += pEntry->ppDayCounts[i]->iCount;
			}
		}

		if (ERRORDATATYPE_IS_A_CRASH(pEntry->eType) && pEntry->ppExecutableNames)
		{
			int j;
			for (j=eaSize(&pEntry->ppExecutableNames)-1; j>=0; j--)
			{
				if (strstri_safe(pEntry->ppExecutableNames[j], "Server"))
				{
					iTwoWeekCount += 100;
					break;
				}
			}
		}
		if (iTwoWeekCount != pEntry->iTwoWeekCount)
		{
			objRequestTransactionSimplef(NULL, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID, 
				"setTwoWeekCount", "set iTwoWeekCount = %d", iTwoWeekCount);
		}
		currCon = objGetNextContainerFromIterator(&iter);
	}
	objClearContainerIterator(&iter);
}

// -----------------------------------------------------------------------------------------
// Scheduled Tasks

bool gbQueueSymSrvRestart = false;

void EveryFewMinutesActivities(void)
{
	char time[32];
	static PROCESS_MEMORY_COUNTERS memCount = {0};
	timeMakeLocalTimeStringFromSecondsSince2000(time, timeSecondsSince2000());
	if (gbETVerbose) printf("EveryFewMinutesActivities: %s\n", time);

	// SymServLookup running check
	{
		HANDLE hProcess = 0;
		DWORD exitCode = 0;

		if (gPidSymSrv)
			hProcess = OpenProcess(PROCESS_ALL_ACCESS, false, gPidSymSrv);		
		if (!hProcess || (GetExitCodeProcess(hProcess, &exitCode) && exitCode != STILL_ACTIVE))
		{
			// no SymSrvLookup running, try to start a new one
			launchSymServLookup();
		}
		if (hProcess)
			CloseHandle(hProcess);
	}

	if((errorTrackerLibGetOptions() & ERRORTRACKER_OPTION_DISABLE_EMAILS) == 0)
	{
		ProcessEmailQueue();
		PurgeErrorEmailTimesTable();
	}

	if((errorTrackerLibGetOptions() & ERRORTRACKER_OPTION_DISABLE_AUTO_BLAME) == 0)
	{
		updateRequestedBlameInfo();
	}

	if(suLastErrorCountTick)
	{
		unsigned int msElapsed = GetTickCount() - suLastErrorCountTick;
		printf("Reports in the last %2.2fsec: %d nonfatal, %d fatal\n", (float)msElapsed / 1000.0, siNonFatalErrorsSeen, siFatalErrorsSeen);
		siNonFatalErrorsSeen = 0;
		siFatalErrorsSeen    = 0;
	}
	if((errorTrackerLibGetOptions() & ERRORTRACKER_OPTION_DISABLE_AUTO_SAVE) == 0)
	{
		backlogSave(getBacklogFilename());
	}
	expireOldReports();
	UnknownIPSaveFile();

	suLastErrorCountTick = GetTickCount();
}

void HourlyActivities(void)
{
	/*char time[32];
	timeMakeLocalTimeStringFromSecondsSince2000(time, timeSecondsSince2000());
	printf("HourlyActivities: %s\n", time);

	if((errorTrackerLibGetOptions() & ERRORTRACKER_OPTION_DISABLE_AUTO_SAVE) == 0)
	{
	}*/
}

static ErrorEntry **sppJiraToUpdate = NULL; // These are actually copies that can be modified safely
static bool sbJiraUpdateDone = false;
static DWORD WINAPI ErrorUpdateJiras(LPVOID lpParam)
{
	EXCEPTION_HANDLER_BEGIN
	{
	static NetComm *sJiraUpdateComm = NULL;
	int i, size = eaSize(&sppJiraToUpdate);
	NOCONST(JiraIssue) jiraCopy = {0};

	if (!sJiraUpdateComm)
		sJiraUpdateComm = commCreate(0,0);
	sbJiraUpdateDone = false;
	for (i=size-1; i>=0; i--)
	{
		ErrorEntry *pEntry = findErrorTrackerByID(sppJiraToUpdate[i]->uID);
		if (pEntry)
		{
			JiraIssue* jira = pEntry->pJiraIssue;
			bool bEdited = false;
			estrCopy2(&jiraCopy.key, jira->key);
			estrCopy2(&jiraCopy.assignee, jira->assignee);
			jiraCopy.status = jira->status;
		
			jiraGetIssue((JiraIssue*) (&jiraCopy), sJiraUpdateComm);
			if (stricmp(jiraCopy.assignee, jira->assignee) != 0 || jiraCopy.status != jira->status)
				bEdited = true;

			if (bEdited)
			{
				estrCopy2(&(CONTAINER_NOCONST(JiraIssue, sppJiraToUpdate[i]->pJiraIssue))->assignee, jiraCopy.assignee);
				(CONTAINER_NOCONST(JiraIssue, sppJiraToUpdate[i]->pJiraIssue))->status = jiraCopy.status;
			}
			else
			{
				StructDestroy(parse_ErrorEntry, sppJiraToUpdate[i]);
				eaRemove(&sppJiraToUpdate, i);
			}
		}
	}
	StructDeInitNoConst(parse_JiraIssue, &jiraCopy);
	sbJiraUpdateDone = true;
	}
	EXCEPTION_HANDLER_END
	return 0;
}

void SaveJiraUpdates(void)
{
	int i;
	if (!sbJiraUpdateDone)
		return;
	for (i=eaSize(&sppJiraToUpdate)-1; i>=0; i--)
	{
		ErrorEntry *pEntry = findErrorTrackerByID(sppJiraToUpdate[i]->uID);
		JiraIssue *updateJira = sppJiraToUpdate[i]->pJiraIssue;
		if (pEntry)
		{
			JiraIssue* jira = pEntry->pJiraIssue;

			if (stricmp(updateJira->assignee, jira->assignee) != 0)
			{
				objRequestTransactionSimplef(NULL, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID, 
					"setJiraAssignee", "set pJiraIssue.assignee = \"%s\"", updateJira->assignee);
			}
			if (updateJira->status != jira->status)
			{
				objRequestTransactionSimplef(NULL, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID, 
					"setJiraStatus", "set pJiraIssue.status = %d", updateJira->status);
			}
		}
	}
	eaDestroyStruct(&sppJiraToUpdate, parse_ErrorEntry);
	sbJiraUpdateDone = false;
}

void NightlyPreJiraActivities(void)
{
	ContainerIterator iter = {0};
	Container *con;

	if (!jiraDefaultLogin())
		return;

	eaClearStruct(&sppJiraToUpdate, parse_ErrorEntry);
	objInitContainerIteratorFromType(GLOBALTYPE_ERRORTRACKERENTRY, &iter);
	con = objGetNextContainerFromIterator(&iter);
	while(con)
	{
		ErrorEntry *pEntry = CONTAINER_ENTRY(con);
		if (pEntry->pJiraIssue)
		{
			NOCONST(ErrorEntry) *pDataCopy = StructCreateNoConst(parse_ErrorEntry);
			pDataCopy->uID = pEntry->uID;
			pDataCopy->pJiraIssue = StructCloneDeConst(parse_JiraIssue, pEntry->pJiraIssue);
			eaPush(&sppJiraToUpdate, CONTAINER_RECONST(ErrorEntry, pDataCopy));
		}
		con = objGetNextContainerFromIterator(&iter);
	}
	objClearContainerIterator(&iter);
}

void NightlyCleanupActivities(void)
{
	char time[32];
	U32 timeCurrent = timeSecondsSince2000();
	timeMakeLocalTimeStringFromSecondsSince2000(time, timeCurrent);
	printf("NightlyCleanupActivities: %s\n", time);

	if((errorTrackerLibGetOptions() & ERRORTRACKER_OPTION_DISABLE_AUTO_CLEAR) == 0)
	{
		int count;
		loadstart_printf("Deleting old entries... ");
		count = RemoveOldEntries(timeCurrent, gbErrorAgeCutoffDays * SECONDS_PER_DAY, false);
		loadend_printf("Removed %d entries.\n", count);
	}
}

AUTO_COMMAND ACMD_CATEGORY(ET_Debug);
void NightlyActivities(void)
{
	char time[32];
	U32 timeCurrent = timeSecondsSince2000();
	timeMakeLocalTimeStringFromSecondsSince2000(time, timeCurrent);
	printf("NightlyActivities: %s\n", time);

	if((errorTrackerLibGetOptions() & ERRORTRACKER_OPTION_DISABLE_EMAILS) == 0)
	{
		ClearEmailFiles();
		SendNightlyEmails();
	}
	{
		ContainerIterator iter = {0};
		Container *con;

		objInitContainerIteratorFromType(GLOBALTYPE_ERRORTRACKERENTRY, &iter);
		con = objGetNextContainerFromIterator(&iter);
		while(con)
		{
			ErrorEntry *pEntry = CONTAINER_ENTRY(con);
			pEntry->iDailyCount = 0;
			pEntry->bIsNewEntry = false;
			con = objGetNextContainerFromIterator(&iter);
		}
		objClearContainerIterator(&iter);
	}
	stashTableClear(errorSourceFileLineTable); // clear the error source file+line count
}

AUTO_COMMAND ACMD_CATEGORY(ET_Debug);
void ForceUpdateJiras(void)
{
	DWORD dummy;
	NightlyPreJiraActivities();
	CloseHandle((HANDLE) _beginthreadex(0, 0, ErrorUpdateJiras, 0, 0, &dummy));
}

// -----------------------------------------------------------------------------------------
// Scheduled Checks
void EveryFewMinutesCheck(void)
{
	static int lastSecondChecked = -1;
	int iTime = timeSecondsSince2000();

	// Lazily init lastSecondChecked
	if(lastSecondChecked == -1)
	{
		lastSecondChecked = timeSecondsSince2000();
	}

	if(iTime > (lastSecondChecked + EVERY_FEW_MINUTES_IN_SECONDS))
	{
		EveryFewMinutesActivities();
		lastSecondChecked = iTime;
	}
}

int GetCurrentHour(void)
{
	struct tm timeStruct;
	timeMakeLocalTimeStructFromSecondsSince2000(timeSecondsSince2000(), &timeStruct);
	return timeStruct.tm_hour;
}

void HourlyCheck(void)
{
	static int lastHourChecked = -1;
	int iCurrentHour = GetCurrentHour();

	// Lazily init lastHourChecked
	if(lastHourChecked == -1)
	{
		lastHourChecked = GetCurrentHour();
	}

	if(iCurrentHour != lastHourChecked)
	{
		HourlyActivities();
		lastHourChecked = iCurrentHour;
	}
}

void NightlyCheck(void)
{
	static int lastDayOfWeekChecked = -1;
	static int lastCleanupDayOfWeekChecked = -1;
	static int lastJiraDayOfWeekChecked = -1;
	int currentDayOfWeek, currentHour;
	struct tm timeStruct;

	timeMakeLocalTimeStructFromSecondsSince2000(timeSecondsSince2000(), &timeStruct);
	currentDayOfWeek = timeStruct.tm_wday;
	currentHour      = timeStruct.tm_hour;

	if (lastCleanupDayOfWeekChecked != currentDayOfWeek &&
		currentHour == (HOUR_OF_DAY_WHEN_NIGHTLY_REPORTS_OCCUR - 2) % 24)
	{
		if((errorTrackerLibGetOptions() & ERRORTRACKER_OPTION_DISABLE_AUTO_SAVE) == 0)
		{
			NightlyCleanupActivities();
		}
		lastCleanupDayOfWeekChecked = currentDayOfWeek;
	}
	if (lastJiraDayOfWeekChecked != currentDayOfWeek &&
		currentHour == (HOUR_OF_DAY_WHEN_NIGHTLY_REPORTS_OCCUR - 1) % 24)
	{
		if((errorTrackerLibGetOptions() & ERRORTRACKER_OPTION_DISABLE_AUTO_SAVE) == 0)
		{
			DWORD dummy;
			NightlyPreJiraActivities();
			CloseHandle((HANDLE) _beginthreadex(0, 0, ErrorUpdateJiras, 0, 0, &dummy));
		}
		lastJiraDayOfWeekChecked = currentDayOfWeek;
	}
	if(currentDayOfWeek != lastDayOfWeekChecked &&
		currentHour == HOUR_OF_DAY_WHEN_NIGHTLY_REPORTS_OCCUR)
	{
		if((errorTrackerLibGetOptions() & ERRORTRACKER_OPTION_DISABLE_AUTO_SAVE) == 0)
			TallyTwoWeekCount();
		NightlyActivities();
		lastDayOfWeekChecked = currentDayOfWeek;
	}
}

// -----------------------------------------------------------------------------------------

NetComm *getSymbolServerComm(void)
{
	if (!symsrvComm)
	{
		symsrvComm = commCreate(0,0); // Must be non-threaded!
		commSetSendTimeout(symsrvComm, 10.0f);
	}
	return symsrvComm;
}

static char *getSymServFullCmdLine()
{
	static char symServCmd[MAX_PATH+1] = {0};

	if(!symServCmd[0])
	{
		/*if (strstri(getExecutableName(), "64.exe")) // TODO symserv doesn't work with X64 yet
			sprintf(symServCmd, "%s -UseRemoteSymServ %d", sSymSrvX64Cmd, gUseRemoteSymServ);
		else*/
			sprintf(symServCmd, "%s -UseRemoteSymServ %d -setLogFile %s%s -LogServer %s %s%s", 
				sSymSrvCmd, gUseRemoteSymServ, logGetDir(), "symsrv.log", gServerLibState.logServerHost, 
				gErrorTrackerSettings.pTempPDBDir ? "-pdbdir " : "", 
				gErrorTrackerSettings.pTempPDBDir ? gErrorTrackerSettings.pTempPDBDir : "");
	}

	return symServCmd;
}

AUTO_COMMAND;
void launchSymServLookup(void)
{
	char cwd[MAX_PATH];
	static U32 suStartRequested = 0;
	U32 uCurTime = timeSecondsSince2000();

	if (suStartRequested && suStartRequested + 30 > uCurTime)
		return;
	fileGetcwd(cwd, MAX_PATH);

	/*if (strstri(getExecutableName(), "64.exe"))
		killall(sSymSrvX64Cmd);
	else */
		killall(sSymSrvCmd);

	suStartRequested = uCurTime;
	if (symsrvComm)
		commDestroy(&symsrvComm);
	printf("CWD is %s\n", cwd);
	printf("About to try to launch %s\n", getSymServFullCmdLine());

	if (errorTrackerLibGetOptions() & ERRORTRACKER_OPTION_FORCE_RUN_SYMSERV_FROM_CORE_TOOLS_BIN)
	{
		gPidSymSrv = 0;
	}
	else
	{
		gPidSymSrv = system_detach(getSymServFullCmdLine(), 0, 0);
	
		//if launching like that fails, try launching directly from core tools bin
		if (gPidSymSrv == 0)
		{
			char symServerFullPath[CRYPTIC_MAX_PATH];
			sprintf(symServerFullPath, "%s\\%s", fileCoreToolsBinDir(), getSymServFullCmdLine());
			backSlashes(symServerFullPath);

			printf("About to try to launch %s\n", symServerFullPath);

			gPidSymSrv = system_detach(symServerFullPath, 0, 0);
		}
	}

	//if that also fails, try getting to c:\core\tools\bin even if we launched from src
	if (gPidSymSrv == 0)
	{
		char *pFullPath = NULL;

		if (fileCoreDataDir())
		{
			estrPrintf(&pFullPath, "%s", fileCoreDataDir());
		}
		else
		{
			estrPrintf(&pFullPath, "c:\\core\\tools\\bin");
		}

		backSlashes(pFullPath);
		estrReplaceOccurrences(&pFullPath, "\\data", "\\tools\\bin");
		estrConcatf(&pFullPath, "\\%s", getSymServFullCmdLine());

		printf("About to try to launch %s\n", pFullPath);
		
		gPidSymSrv = system_detach(pFullPath, 0, 0);

		estrDestroy(&pFullPath);
	}
	assertmsg(gPidSymSrv != 0, "SymServLookup helper application failed to launch");
}

static void deleteTemporaryFiles(void)
{
	char ** eaFiles = fileScanDirNoSubdirRecurse(gErrorTrackerSettings.pDumpTempDir);
	int iCount = 0;

	EARRAY_CONST_FOREACH_BEGIN(eaFiles, i, s);
	{
		if (strEndsWith(eaFiles[i], ".tmp"))
		{
			DeleteFile_UTF8(eaFiles[i]);
			iCount++;
		}
	}
	EARRAY_FOREACH_END;
	printf("\nDeleted %d temporary files.\n", iCount);
}

void initErrorTracker(void)
{
	// Other stuff
	ETWeb_InitWebRootDirs();
	errorTrackerWebInterfaceInit();
	InitializeTriviaFilter();
	launchSymServLookup();

	if((errorTrackerLibGetOptions() & ERRORTRACKER_OPTION_DISABLE_EMAILS) == 0)
	{
		ClearEmailFiles();
	}
	initWebReports();
	initSearchReports();
	backlogInit();
	loadstart_printf("Loading backlog... ");
	backlogLoad(getBacklogFilename());
	loadend_printf("Done.");

	loadstart_printf("Deleting Old Temp Files... ");
	deleteTemporaryFiles();
	loadend_printf("Done.");

	errorTrackerLoadConfig();
	ErrorTrackerStashTableInit();
	etLoadClientExeList();
	UnknownIPLoadFile();

	TriviaDataInit();
}

void shutdownErrorTracker(void)
{
	sbExitTriggered = true;
	terminateSymServLookup();

	printf("Saving backlog...\n");
	backlogSave(getBacklogFilename());

	if((errorTrackerLibGetOptions() & ERRORTRACKER_OPTION_DISABLE_EMAILS) == 0)
	{
		ClearEmailFiles();
	}
	if((errorTrackerLibGetOptions() & ERRORTRACKER_OPTION_DISABLE_AUTO_SAVE) == 0)
	{
		errorTrackerSaveAndClose();
	}

	StructDeInit(parse_ErrorTrackerEntryList, &gpCurrentContext->entryList);

	stashTableDestroy(dumpIDToDumpDataTable);
}

void terminateSymServLookup(void)
{
	if (gPidSymSrv)
	{
		HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, false, gPidSymSrv);
		if (hProcess)
		{
			TerminateProcess(hProcess, 0);
			CloseHandle(hProcess);
		}
		gPidSymSrv = 0;
	}
}

// -----------------------------------------------------------------------------------------

void ErrorTracker_DelayedTasks(void);
void updateErrorTracker(void)
{
	int queueSize = 0;
	updateWebInterface();
	BackupCacheOncePerFrame();

	commMonitor(errorTrackerCommDefault());
	if (symsrvComm)
		commMonitor(symsrvComm);

	queueSize = SymSrvQueue_OncePerFrame();
	if (gbQueueSymSrvRestart)
	{
		// SymSrvLookup Restart Queued
		if (queueSize == 0) // Don't restart if there are entries that need to be processed
		{
			HANDLE hProcess = 0;
			if (gPidSymSrv)
				hProcess = OpenProcess(PROCESS_ALL_ACCESS, false, gPidSymSrv);
			if (hProcess)
				TerminateProcess(hProcess, 0);
			launchSymServLookup();
			gbQueueSymSrvRestart = false;
			if (hProcess)
				CloseHandle(hProcess);
		}
	}

	if (gMasterETDisconnected)
		connectToMasterErrorTracker();

	ErrorTracker_DelayedTasks();
	EveryFewMinutesCheck();
	HourlyCheck();
	NightlyCheck();
}

bool isClosedJira(JiraIssue *jira)
{
	if (!jira) return true;
	else
	{
		const char *jiraStatusString = jiraGetStatusString(jira->status);
		if (stricmp(jiraStatusString, "Closed") == 0 || stricmp(jiraStatusString, "Resolved") == 0)
		{
			// Closed or resolved
			return true;
		}
	}
	return false;
}

#define MIN_FULLDUMPS_TO_KEEP (2)
#define MIN_MINIDUMPS_TO_KEEP (1)
#define MAX_DUMP_AGE_SECS (45 * SECONDS_PER_DAY)
typedef struct DumpFilePaths 
{
	char *fullPath;
	char *miniPath;
} DumpFilePaths;
void ErrorTrackerEntry_DeleteOldDumps(ErrorEntry *pEntry)
{
	U32 uOldestDumpToKeepTime = timeSecondsSince2000() - MAX_DUMP_AGE_SECS;
	U32 uOldestDumpToKeepFromSingleEntryTime = timeSecondsSince2000() - (MAX_DUMP_AGE_SECS / 2);
	int iFullCount = 0, iMiniCount = 0;
	DumpFilePaths **eaDumpPaths = NULL;
	bool bIgnoreMinDumpCount = (pEntry->iTotalCount == 1) && isClosedJira(pEntry->pJiraIssue);

	eaStackCreate(&eaDumpPaths, eaSize(&pEntry->ppDumpData));
	eaSetSize(&eaDumpPaths, eaSize(&pEntry->ppDumpData));
	// Figure out file paths and how many actual dump files there are
	EARRAY_FOREACH_BEGIN(pEntry->ppDumpData, i);
	{
		DumpData *pDump = pEntry->ppDumpData[i];
		if (pDump->uFlags & (DUMPDATAFLAGS_MOVED | DUMPDATAFLAGS_DELETED))
			continue;
		if (pDump->bWritten || pDump->bMiniDumpWritten)
		{
			if (pDump->pEntry)
			{
				char fullPath[MAX_PATH];
				char miniPath[MAX_PATH];
				fullPath[0] = '\0';
				miniPath[0] = '\0';

				if (pDump->bWritten)
				{
					if (pDump->uFlags & DUMPDATAFLAGS_FULLDUMP)
						errorTrackerLibGetDumpFilename(fullPath, ARRAY_SIZE_CHECKED(fullPath), pEntry, pDump, false);
					else
						errorTrackerLibGetDumpFilename(miniPath, ARRAY_SIZE_CHECKED(miniPath), pEntry, pDump, false);
				}
				if (pDump->bMiniDumpWritten)
					errorTrackerLibGetDumpFilename(miniPath, ARRAY_SIZE_CHECKED(miniPath), pEntry, pDump, true);

				if (fullPath[0] && fileExists(fullPath))
				{
					if (!eaDumpPaths[i])
						eaDumpPaths[i] = calloc(1, sizeof(DumpFilePaths));
					eaDumpPaths[i]->fullPath = strdup(fullPath);
					iFullCount++;
				}
				if (miniPath[0] && fileExists(miniPath))
				{
					if (!eaDumpPaths[i])
						eaDumpPaths[i] = calloc(1, sizeof(DumpFilePaths));
					eaDumpPaths[i]->miniPath = strdup(miniPath);
					iMiniCount++;
				}
			}
		}
	}
	EARRAY_FOREACH_END;

	// Now delete the actual dumps
	EARRAY_FOREACH_BEGIN(pEntry->ppDumpData, i);
	{
		DumpData *pDump = pEntry->ppDumpData[i];
		if (pDump->uFlags & (DUMPDATAFLAGS_MOVED | DUMPDATAFLAGS_DELETED))
			continue;
		if (!pDump->pEntry) 
		{
			AssertOrAlert("BAD_DUMPDATA", "Dump #%i of ETID %i has no attached error information!", i, pEntry->uID);
			continue;
		}
		if ((pEntry->iTotalCount >= 2 && pDump->pEntry->uFirstTime >= uOldestDumpToKeepTime) || pDump->pEntry->uFirstTime >= uOldestDumpToKeepFromSingleEntryTime)
		{
			// Update the Oldest Dump left to check on this entry
			if (pEntry->uOldestDump == 0 || pDump->pEntry->uFirstTime < pEntry->uOldestDump)
				pEntry->uOldestDump = pDump->pEntry->uFirstTime;
			continue;
		}

		if (eaDumpPaths[i])
		{
			const char *fullPath = eaDumpPaths[i]->fullPath;
			const char *miniPath = eaDumpPaths[i]->miniPath;

			if (bIgnoreMinDumpCount || ((!fullPath || iFullCount > MIN_FULLDUMPS_TO_KEEP) &&
				(!miniPath || iMiniCount > MIN_MINIDUMPS_TO_KEEP)))
			{
				if (fullPath)
				{
					remove(fullPath);
					iFullCount--;
				}
				if (miniPath)
				{
					remove(miniPath);
					iMiniCount--;
				}
				objRequestTransactionSimplef(NULL, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID, 
					"setDumpFlags", "set ppDumpData[%d].uFlags = %d", i, pDump->uFlags | DUMPDATAFLAGS_DELETED);
			}

#pragma warning(suppress:6001) // /analzye "Using uninitialized memory '*fullPath'"
			SAFE_FREE(fullPath);
#pragma warning(suppress:6001) // /analzye "Using uninitialized memory '*miniPath'"
			SAFE_FREE(miniPath);
			free(eaDumpPaths[i]);
		}
	}
	EARRAY_FOREACH_END;
	eaDestroy(&eaDumpPaths);
}

#define DELAYED_PROCESS_PER_FRAME (1000) // Number of containers to process per delayed action per frame
CONTAINERID_EARRAY geaiContainersToDelete = NULL;
CONTAINERID_EARRAY geaiContainerToCheckDumps = NULL;
// Run per-frame in main thread
void ErrorTracker_DelayedTasks(void)
{
	bool bDeleted = false;

	// Deleting old ET entries
	PERFINFO_AUTO_START("ET_DeleteOldContainers", 1);
	EARRAY_INT_CONST_FOREACH_BEGIN(geaiContainersToDelete, i, n);
	{
		ErrorEntry *pEntry = findErrorTrackerByID(geaiContainersToDelete[i]);
		if (pEntry)
		{
			errorTrackerEntryDelete(pEntry, true);
			bDeleted = true;
		}
		if (i >= DELAYED_PROCESS_PER_FRAME-1)
			break;
	}
	EARRAY_FOREACH_END;
	if (eaiSize(&geaiContainersToDelete) < DELAYED_PROCESS_PER_FRAME)
		eaiDestroy(&geaiContainersToDelete);
	else
		eaiRemoveRange(&geaiContainersToDelete, 0, DELAYED_PROCESS_PER_FRAME);
	if (bDeleted)
		initializeEntries(); // Reload the web list of entries
	PERFINFO_AUTO_STOP();

	// Deleting old dumps in kept ET entries
	PERFINFO_AUTO_START("ET_DeleteOldDumps", 1);
	EARRAY_INT_CONST_FOREACH_BEGIN(geaiContainerToCheckDumps, i, n);
	{
		ErrorEntry *pEntry = findErrorTrackerByID(geaiContainerToCheckDumps[i]);		
		ErrorTrackerEntry_DeleteOldDumps(pEntry);
		if (i >= DELAYED_PROCESS_PER_FRAME-1)
			break;
	}
	EARRAY_FOREACH_END;
	if (eaiSize(&geaiContainerToCheckDumps) < DELAYED_PROCESS_PER_FRAME)
		eaiDestroy(&geaiContainerToCheckDumps);
	else
		eaiRemoveRange(&geaiContainerToCheckDumps, 0, DELAYED_PROCESS_PER_FRAME);

	// Processing Delayed Migrations
	runDelayMigration();

	PERFINFO_AUTO_STOP();
}

int RemoveOldEntries(U32 timeCurrent, U32 maxAgeInSeconds, bool bOnlyNonfatals)
{
	U32 uOldestDumpToKeepTime = timeCurrent - MAX_DUMP_AGE_SECS;
	U32 uOldestDumpToKeepFromSingleEntryTime = timeCurrent - (MAX_DUMP_AGE_SECS / 2);
	U32 minLastSeenTime = timeCurrent - maxAgeInSeconds;
	ContainerIterator iter = {0};
	Container *currCon = NULL;
	char datetime[128] = "";
	ErrorTrackerContext *context = errorTrackerLibGetCurrentContext();
	bool bIsDeletingEntries = eaiSize(&geaiContainersToDelete) > 0;
	bool bIsDeletingDumps = eaiSize(&geaiContainerToCheckDumps) > 0;
	
	if (!bIsDeletingEntries || !bIsDeletingDumps)
	{
		objInitContainerIteratorFromType(context->entryList.eContainerType, &iter);
		timeMakeLocalDateStringFromSecondsSince2000(datetime, minLastSeenTime);
		printf ("\nMax Date: %s\n", datetime);
		while (currCon = objGetNextContainerFromIterator(&iter))
		{
			ErrorEntry *pEntry = CONTAINER_ENTRY(currCon);

			if(bOnlyNonfatals && (pEntry->eType != ERRORDATATYPE_ERROR))
				continue;

			if (pEntry->uNewestTime < minLastSeenTime && isClosedJira(pEntry->pJiraIssue) && !bIsDeletingEntries)
			{
				eaiPush(&geaiContainersToDelete, pEntry->uID);
			}
			else if (eaSize(&pEntry->ppDumpData) > 0 && !bIsDeletingDumps)
			{
				if ((!pEntry->uOldestDump || pEntry->uOldestDump < uOldestDumpToKeepTime) ||
					(pEntry->iTotalCount < 2 && pEntry->uOldestDump < uOldestDumpToKeepFromSingleEntryTime))
				{
					eaiPush(&geaiContainerToCheckDumps, pEntry->uID);
				}
			}
		}
		objClearContainerIterator(&iter);
	}
	return eaiSize(&geaiContainersToDelete);
}

AUTO_COMMAND ACMD_CATEGORY(ET_Debug);
int ForceCleanup (U32 uMaxAgeDays)
{
	return RemoveOldEntries(timeSecondsSince2000(), uMaxAgeDays * SECONDS_PER_DAY, false);
}

void errorTrackerCleanUpDumps(void)
{
	//sprintf_s(pFilename, iMaxLength, "%sdumps\\%d\\%d.%s", ETWeb_GetDataDir(), uID, iDumpIndex, (bFullDump) ? "dmp.gz" : "mdmp.gz");
	char dumpPath[MAX_PATH];
	char **dumpFolders;
	int i;
	char cmdBuffer[512]; 

	sprintf(dumpPath, "%s", gErrorTrackerSettings.pDumpDir);
	dumpFolders = fileScanDirFolders(dumpPath, FSF_FOLDERS);

	loadstart_printf("Deleting old dumps... ");
	for (i=eaSize(&dumpFolders)-1; i>=0; i--)
	{
		int entryID = 0;

		entryID = parseErrorEntryDir(dumpFolders[i]);

		if (entryID)
		{
			Container *con = objGetContainer(errorTrackerLibGetCurrentContext()->entryList.eContainerType, entryID);
			if (con)
			{
				// delete bad dumps
				char entryDumpPath[MAX_PATH];
				ErrorEntry *pEntry = CONTAINER_ENTRY(con);
				int j;
				int dumpCount = eaSize(&pEntry->ppDumpData);
				char ** dumpFiles;

				sprintf(entryDumpPath, "%s\\%d", dumpPath, pEntry->uID);
				dumpFiles = fileScanDirNoSubdirRecurse(entryDumpPath);
				for (j=eaSize(&dumpFiles)-1; j>=0; j--)
				{
					char *fileName = NULL;
					backSlashes(dumpFiles[j]);
					fileName = strrchr(dumpFiles[j], '\\');

					if (fileName && *(fileName+1) >= '0' && *(fileName+1) <= '9')
					{
						int dumpIndex = atoi(fileName+1);
						if (dumpIndex >= dumpCount)
						{
							// delete file
							int ret = remove(dumpFiles[j]);
						}
					}
					free(dumpFiles[j]);
				}
				eaDestroy(&dumpFiles);
			}
			else
			{
				// delete entire folder
				int ret;
				sprintf(cmdBuffer, "rmdir /s /q %s", dumpFolders[i]);
				ret = system(cmdBuffer);
			}
		}
		free(dumpFolders[i]);
	}
	loadend_printf("Done.");
	eaDestroy(&dumpFolders);
}

// Deletes all dumps older than X days old, even if the entry has been recently seen.
AUTO_COMMAND ACMD_CATEGORY(ET_Debug);
void deleteOldDumps(U32 uAgeThresholdDays)
{
	// Also cleans up dumps for non-existant entries
	char dumpPath[MAX_PATH];
	char **dumpFolders;
	char cmdBuffer[512];
	U32 uTime = timeSecondsSince2000();

	sprintf(dumpPath, "%s", gErrorTrackerSettings.pDumpDir);
	dumpFolders = fileScanDirFolders(dumpPath, FSF_FOLDERS);

	loadstart_printf("Deleting dumps older than %d days... ", uAgeThresholdDays);
	EARRAY_FOREACH_BEGIN(dumpFolders, iFolder);
	{
		char *folderPath = dumpFolders[iFolder];
		int entryID = 0;

		entryID = parseErrorEntryDir(folderPath);

		if (entryID)
		{
			Container *con = objGetContainer(errorTrackerLibGetCurrentContext()->entryList.eContainerType, entryID);
			if (con)
			{
				ErrorEntry *pEntry = CONTAINER_ENTRY(con);
				EARRAY_FOREACH_BEGIN(pEntry->ppDumpData, iDump);
				{
					DumpData *pDump = pEntry->ppDumpData[iDump];
					if ((uTime - pDump->pEntry->uNewestTime) / SECONDS_PER_DAY >= uAgeThresholdDays)
					{
						char filepath[MAX_PATH];
						if (pDump->bWritten)
						{
							int ret;
							sprintf(filepath, "%s\\%d.%s.gz", folderPath, pDump->iDumpIndex, 
								(pDump->uFlags & DUMPDATAFLAGS_FULLDUMP) ? "dmp" : "mdmp");
							ret = remove(filepath);
						}
						if (pDump->bMiniDumpWritten)
						{
							int ret;
							sprintf(filepath, "%s\\%d.mdmp.gz", folderPath, pDump->iDumpIndex);
							ret = remove(filepath);
						}
						objRequestTransactionSimplef(NULL, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID, 
							"setDumpFlags", "set ppDumpData[%d].uFlags = %d", iDump, pDump->uFlags | DUMPDATAFLAGS_DELETED);
					}
				}
				EARRAY_FOREACH_END;
			}
			else
			{
				// delete entire folder
				int ret;
				sprintf(cmdBuffer, "rmdir /s /q %s", folderPath);
				ret = system(cmdBuffer);
			}
		}
		free(folderPath);
	}
	EARRAY_FOREACH_END;
	eaDestroy(&dumpFolders);
	loadend_printf("Done.");
}

static char sDumpProfileOutput[MAX_PATH] = "";
static bool sbRunningDumpProfile = false;
static DWORD WINAPI errorTrackerProfileDumps(LPVOID lpParam)
{
	// Columns: Entry ID, Dump/Minidump, Size, Date Modified
	if (sbRunningDumpProfile)
		return 0;
	sbRunningDumpProfile = true;
	EXCEPTION_HANDLER_BEGIN
	{
		char dumpPath[MAX_PATH];
		char **dumpFolders;
		FILE *file = NULL;

		sprintf(dumpPath, "%s", gErrorTrackerSettings.pDumpDir);
		dumpFolders = fileScanDirFolders(dumpPath, FSF_FOLDERS);
		if (*sDumpProfileOutput)
			file = fopen(sDumpProfileOutput, "w");
		if (!file)
		{
			char default_logfile_path[MAX_PATH];
			sprintf(default_logfile_path, "%s\\profile.csv", gErrorTrackerSettings.pDumpDir);
			file = fopen(default_logfile_path, "w");
		}

		if (file)
		{
			fprintf(file, "Entry ID, File Type, Size (bytes), Date Modified\n");
			EARRAY_FOREACH_BEGIN(dumpFolders, i);
			{
				char *folderPath = dumpFolders[i];
				int entryID = 0;
				char ** dumpFiles;

				entryID = parseErrorEntryDir(folderPath);
				if (!entryID)
					continue;

				dumpFiles = fileScanDirNoSubdirRecurse(folderPath);
				EARRAY_FOREACH_BEGIN(dumpFiles, j);
				{
					char *dumpFilename = dumpFiles[j];
					char *fileName = NULL;
					U32 uAge = fileLastChangedSS2000(dumpFilename);
					intptr_t size = fileSizeEx(dumpFilename, false);
					char *extension = NULL;

					backSlashes(dumpFilename);
					fileName = strrchr(dumpFiles[j], '\\');
					extension = fileName + strlen(fileName);
					while (*(extension-1) != '.' && extension > fileName)
						extension--;
					if (stricmp(extension, "gz") == 0 && *(extension-1) == '.')
					{
						char *gzStart = --extension;
						*gzStart = '\0';
						while (*(extension-1) != '.' && extension > fileName)
							extension--;
						if (*(extension-1) == '.')
							extension = strdup(extension);
						else
							extension = NULL;
						*gzStart = '.';
					}
					else if (*(extension-1) == '.')
						extension = strdup(extension);
					else
						extension = NULL;
					
					fprintf(file, 
						"%d, %s, %"FORM_LL"u, %s\n",
						entryID,
						extension, 
						(U64) size,
						timeGetLocalDateStringFromSecondsSince2000(uAge));
					if (extension)
						free(extension);
					free(dumpFilename);
				}
				EARRAY_FOREACH_END;
				eaDestroy(&dumpFiles);
				free(folderPath);
			}
			EARRAY_FOREACH_END;
			fclose(file);
			eaDestroy(&dumpFolders);
		}
	}
	EXCEPTION_HANDLER_END
	sbRunningDumpProfile = false;
	return 0;
}
// Outputs the profile as a CSV file
AUTO_COMMAND ACMD_CATEGORY(ET_Debug);
void profileDumpDiskUsage(char *output_file)
{
	DWORD dummy;
	if (output_file)
		sprintf(sDumpProfileOutput, "%s", output_file);
	else
		sDumpProfileOutput[0] = 0;
	CloseHandle((HANDLE) _beginthreadex(0, 0, errorTrackerProfileDumps, 0, 0, &dummy));
}

//rearranges the rawdata folder
AUTO_COMMAND ACMD_CATEGORY(ErrorTracker);
void refactorErrorEntryDir(char *datadir)
{
	char **errorFolders;
	errorFolders = fileScanDirFoldersNoSubdirRecurse(datadir, FSF_FOLDERS);

	EARRAY_FOREACH_BEGIN(errorFolders, i);
	{
		char *errorName, *errorPath = errorFolders[i];
		int entryID;
		int val;
		char foldername[MAX_PATH];
		size_t len;
		char *last;

		PERFINFO_AUTO_START("main loop", 1);

		backSlashes(errorPath);
		errorName = strrchr(errorPath, '\\');
		if (errorName)
		{
			len = strlen(errorName);
			last = (errorName + len - 1); //len must be at least 1 here
			if (!isdigit(*last))
				continue;
			entryID = atoi(errorName + 1);
			if (entryID == 0)
				continue;
		}
		else
			continue;

		GetErrorEntryDir(datadir, entryID, SAFESTR(foldername));
		strcat(foldername, "\\");
		mkdirtree(foldername);
		val = rmdir(foldername);
		if (val == 0)
		{
			PERFINFO_AUTO_START("Move file", 1);
			MoveFile_UTF8(errorPath, foldername);
			PERFINFO_AUTO_STOP();
		}
		else
		{
			char filename[MAX_PATH];
			char **errorEntries = fileScanDir(errorPath);
			EARRAY_FOREACH_BEGIN(errorEntries, j);
			{
				char *srcPath = errorEntries[j];
				char *srcfile;

				backSlashes(srcPath);
				srcfile = strrchr(errorEntries[j], '\\');
				strcpy(filename, foldername);
				strcat(filename, srcfile + 1);
				MoveFile_UTF8(srcPath, filename);
			}
			EARRAY_FOREACH_END;
			val = rmdir(errorPath);
			fileScanDirFreeNames(errorEntries);
		}
		if(i % 1000 == 0)
			printf("Completed entry %d\n", entryID);
		PERFINFO_AUTO_STOP();
	}
	EARRAY_FOREACH_END;

	fileScanDirFreeNames(errorFolders);

	printf("Refactor complete!\n");
	exit(0);
}

int errorTrackerCleanupNonfatals(void)
{
	return RemoveOldEntries(timeSecondsSince2000(), NONFATAL_ERROR_STARTUP_AGE_CUTOFF, true);
}

AUTO_TRANSACTION ATR_LOCKS(pEntry, ".ppUserInfo");
enumTransactionOutcome trErrorEntry_TrimUserList(ATR_ARGS, NOCONST(ErrorEntry) *pEntry)
{
	int size = eaSize(&pEntry->ppUserInfo);
	if (size > MAX_USERS_AND_IPS)
	{
		eaRemoveRange(&pEntry->ppUserInfo, MAX_USERS_AND_IPS, size-MAX_USERS_AND_IPS);
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}
AUTO_TRANSACTION ATR_LOCKS(pEntry, ".ppIPCounts");
enumTransactionOutcome trErrorEntry_TrimIPList(ATR_ARGS, NOCONST(ErrorEntry) *pEntry)
{
	int size = eaSize(&pEntry->ppIPCounts);
	if (size > MAX_USERS_AND_IPS)
	{
		eaRemoveRange(&pEntry->ppIPCounts, MAX_USERS_AND_IPS, size-MAX_USERS_AND_IPS);
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

int errorTrackerTrimUsers(void)
{
	int iTrimmedCount = 0;
	ContainerIterator iter = {0};
	Container *currCon = NULL;
	ErrorTrackerContext *context = errorTrackerLibGetCurrentContext();

	objInitContainerIteratorFromType(context->entryList.eContainerType, &iter);

	while (currCon = objGetNextContainerFromIterator(&iter))
	{
		bool bTrimmed = false;
		ErrorEntry *pEntry = CONTAINER_ENTRY(currCon);

		if(!pEntry->bUnlimitedUsers && eaSize(&pEntry->ppUserInfo) > MAX_USERS_AND_IPS)
		{
			AutoTrans_trErrorEntry_TrimUserList(NULL, GLOBALTYPE_ERRORTRACKER, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID);
			bTrimmed = true;
		}

		if(!pEntry->bUnlimitedUsers && eaSize(&pEntry->ppIPCounts) > MAX_USERS_AND_IPS)
		{
			AutoTrans_trErrorEntry_TrimIPList(NULL, GLOBALTYPE_ERRORTRACKER, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID);
			bTrimmed = true;
		}
		if(bTrimmed)
			iTrimmedCount++;
	}
	objClearContainerIterator(&iter);
	return iTrimmedCount;
}

void LogAllTriviaOverviews(void)
{
	ContainerIterator iter = {0};
	Container *currCon = NULL;
	Container **conToDelete = NULL;
	char datetime[128] = "";
	int iNumberOfContainers = objCountTotalContainers();
	int iContainersProcessed = 0;
	ErrorTrackerContext *context = errorTrackerLibGetCurrentContext();

	if (iNumberOfContainers == 0)
		return;

	objInitContainerIteratorFromType(context->entryList.eContainerType, &iter);
	
	printf("Writing out Trivia Data.\n");
	while (currCon = objGetNextContainerFromIterator(&iter))
	{
		ErrorEntry *pEntry = CONTAINER_ENTRY(currCon);

		if(eaSize(&pEntry->triviaOverview.ppTriviaItems) > 0)
		{
			LogTriviaOverview(pEntry->uID, pEntry);
			objRequestTransactionSimplef(NULL, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID, 
				"DestroyTriviaOverview", "destroy triviaOverview");
		}

		if(eaSize(&pEntry->ppTriviaData) > 0)
		{
			LogTriviaData(pEntry->uID, pEntry->ppTriviaData);
			AutoTrans_trErrorEntry_RemoveTriviaData(NULL, GLOBALTYPE_ERRORTRACKER, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID);
		}

		if((++iContainersProcessed % 1000) == 0)
		{
			printf("\rProcessed %d/%d (%d%% complete)                           ", iContainersProcessed, iNumberOfContainers, (iContainersProcessed * 100) / iNumberOfContainers);
		}
	}
	objClearContainerIterator(&iter);
	printf("\rProcessed %d/%d (%d%% complete)                           ", iNumberOfContainers, iNumberOfContainers, 100);
	printf("Done.\n");
}

// -------------------------------------------------------------------------
#define ERRORTRACKER_CONFIG_FILE "errortracker_config.txt"
static char sETConfigFilePath[MAX_PATH] = "";
ErrorTrackerConfig gETConfig = {0};

void errorTrackerLoadConfig(void)
{
	if (!sETConfigFilePath[0])
	{
		sprintf(sETConfigFilePath, "%s%s", errorTrackerGetDatabaseDir(), ERRORTRACKER_CONFIG_FILE);
	}
	ParserReadTextFile(sETConfigFilePath, parse_ErrorTrackerConfig, &gETConfig, 0);
	resRegisterDictionaryForEArray("Generic Crash Hashes", RESCATEGORY_OTHER, 0, 
		&gETConfig.ppGenericHashes, parse_ErrorTrackerHashWrapper);
}

void errorTrackerSaveConfig(void)
{
	if (!sETConfigFilePath[0])
	{
		sprintf(sETConfigFilePath, "%s%s", errorTrackerGetDatabaseDir(), ERRORTRACKER_CONFIG_FILE);
	}
	ParserWriteTextFile(sETConfigFilePath, parse_ErrorTrackerConfig, &gETConfig, 0, 0);
}

// Returns the genericID string
bool errorTrackerIsGenericHash (ErrorEntry *pEntry, char **estr)
{
	int i;
	if (!ERRORDATATYPE_IS_A_CRASH(pEntry->eType))
		return false;
	for (i=eaSize(&gETConfig.ppGenericHashes)-1; i>=0; i--)
	{
		if (hasHash(pEntry->aiUniqueHashNew))
		{
			if (hashMatchesU32(pEntry->aiUniqueHashNew, gETConfig.ppGenericHashes[i]->aiHash))
			{
				if (estr && gETConfig.ppGenericHashes[i]->pGenericLabel)
					estrCopy2(estr, gETConfig.ppGenericHashes[i]->pGenericLabel);
				return true;
			}
		} 
		else
		{
			if (hashMatchesU32(pEntry->aiUniqueHash, gETConfig.ppGenericHashes[i]->aiHash))
			{
				if (estr && gETConfig.ppGenericHashes[i]->pGenericLabel)
					estrCopy2(estr, gETConfig.ppGenericHashes[i]->pGenericLabel);
				return true;
			}
		}
	}
	return false;
}

void errorTrackerAddGenericHash(ErrorEntry *pEntry, const char *genericLabel)
{
	ErrorTrackerHashWrapper *pNewHash;
	if (errorTrackerIsGenericHash(pEntry, NULL))
		return; // already a generic hash
	pNewHash = StructCreate(parse_ErrorTrackerHashWrapper);
	if (hasHash(pEntry->aiUniqueHashNew))
	{
		pNewHash->aiHash[0] = pEntry->aiUniqueHashNew[0];
		pNewHash->aiHash[1] = pEntry->aiUniqueHashNew[1];
		pNewHash->aiHash[2] = pEntry->aiUniqueHashNew[2];
		pNewHash->aiHash[3] = pEntry->aiUniqueHashNew[3];
	}
	else
	{
		pNewHash->aiHash[0] = pEntry->aiUniqueHash[0];
		pNewHash->aiHash[1] = pEntry->aiUniqueHash[1];
		pNewHash->aiHash[2] = pEntry->aiUniqueHash[2];
		pNewHash->aiHash[3] = pEntry->aiUniqueHash[3];
	}
	if (genericLabel)
		estrCopy2(&pNewHash->pGenericLabel, genericLabel);

	pNewHash->uID = ++gETConfig.uLastKey;
	// Add hash to config + save config
	eaPush(&gETConfig.ppGenericHashes, pNewHash);
	errorTrackerSaveConfig();

	// Remove old hash mapping, NULL out hash, add into NULL hash array
	ErrorEntry_RemoveHashStash(pEntry);
	AutoTrans_trErrorEntry_RemoveHash(NULL, GLOBALTYPE_ERRORTRACKER, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID);
	ErrorEntry_AddHashStash(pEntry);

	objRequestTransactionSimplef(NULL, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID, 
		"setGenericLabel", "set pGenericLabel = \"%s\"", genericLabel);
}

void errorTrackerUndoGenericHash(ErrorEntry *pEntry)
{
	int i;
	if (!ErrorEntry_IsGenericCrash(pEntry))
		return; // not a generic hash

	// Remove old hash mapping, NULL out hash, add into NULL hash array
	ErrorEntry_RemoveHashStash(pEntry);
	AutoTrans_trErrorEntry_RecalcHash(NULL, GLOBALTYPE_ERRORTRACKER, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID);
	if (!ErrorEntry_AddHashStash(pEntry))
	{   // Hash conflict - re-remove the hash and return an error
		AutoTrans_trErrorEntry_RemoveHash(NULL, GLOBALTYPE_ERRORTRACKER, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID);
		Errorf("Could not undo hash-generification for this entry; Recalculated hash conflicts with another");
		return;
	}
	objRequestTransactionSimplef(NULL, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID, 
		"clearGenericLabel", "destroy pGenericLabel");

	// Remove hash from config
	for (i=eaSize(&gETConfig.ppGenericHashes)-1; i>=0; i--)
	{
		if (hashMatchesU32(gETConfig.ppGenericHashes[i]->aiHash, pEntry->aiUniqueHash) || 
			hashMatchesU32(gETConfig.ppGenericHashes[i]->aiHash, pEntry->aiUniqueHashNew))
		{
			StructDestroy(parse_ErrorTrackerHashWrapper, gETConfig.ppGenericHashes[i]);
			eaRemove(&gETConfig.ppGenericHashes, i);
			break;
		}
	}
	errorTrackerSaveConfig();
}

// 1. Reprocess based on callstack
// 2. Automatically merge - entry merges plus dumps, minus callstack? (or overwrite older callstacks?)
// Both delete the entry if... there's only one occurrence? Or a generic hash?

//Can we have it automatically tagged somehow so I can search through all of ET to find these null stack (or similar generic crash) types of ET buckets?
//Ability to reprocess based on the dump and deletes the entry after that
//After it’s reprocessed, if doesn’t automatically combine into something else, could we add the ability to manually merge it with another ET bucket?


#include "AutoGen/ErrorTracker_h_ast.c"
