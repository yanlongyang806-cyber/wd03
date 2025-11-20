#include "ErrorTracker.h"
#include "ErrorTrackerDB.h"
#include "ErrorEntry.h"
#include "ErrorTrackerLib.h"
#include "ErrorTrackerLibPrivate.h"
#include "ErrorTrackerHandlerInfo.h"
#include "autogen/ErrorTrackerHandlerInfo_h_ast.h"
#include "wininclude.h"
#include "error.h"
#include "earray.h"
#include "estring.h"
#include "textparser.h"
#include "net/net.h"
#include "ErrorTracker.h"
#include "WebInterface.h"
#include "email.h"
#include "blame.h"
#include "search.h"
#include "timing.h"
#include "file.h"
#include "jira.h"
#include "etTrivia.h"

#include "objSchema.h"
#include "objContainer.h"
#include "objContainerIO.h"
#include "objTransactions.h"
#include "logging.h"
#include "ETCommon/ETShared.h"
#include "ETCommon/ETIncomingData.h"
#include "ETCommon/ETWebCommon.h"
#include "AutoGen/ErrorTrackerLib_autotransactions_autogen_wrappers.h"

extern ErrorTrackerSettings gErrorTrackerSettings;

bool gbMigrateData = false;
AUTO_CMD_INT(gbMigrateData, MigrateData);
bool gbCleanupDumps = false;
AUTO_CMD_INT(gbCleanupDumps, CleanupDumps);
bool gbCleanupNonfatals = false;
AUTO_CMD_INT(gbCleanupNonfatals, CleanupNonfatals);
bool gbTrimUsers = false;
AUTO_CMD_INT(gbTrimUsers, TrimUsers);

bool gbLogTriviaData = false;
AUTO_CMD_INT(gbLogTriviaData, LogTriviaData);

// -------------------------------------------------------------------------

extern ErrorTrackerContext sDefaultContext;
extern ErrorTrackerContext *gpCurrentContext;

void errorTrackerLibDestroyContext(ErrorTrackerContext *pContext)
{
	ContainerIterator iter = {0};
	Container *currCon;
	U32 *eaiIDs = NULL;
	int i;
	if(!pContext)
		return;

	destroySortedSearch();

	objInitContainerIteratorFromType(pContext->entryList.eContainerType, &iter);
	currCon = objGetNextContainerFromIterator(&iter);
	while (currCon)
	{
		ErrorEntry *pEntry = CONTAINER_ENTRY(currCon);
		eaiPush(&eaiIDs, pEntry->uID);
		currCon = objGetNextContainerFromIterator(&iter);
	}
	objClearContainerIterator(&iter);
	for (i=eaiSize(&eaiIDs)-1; i>=0; i--)
	{
		objRequestContainerDestroyLocal(NULL, pContext->entryList.eContainerType, eaiIDs[i]);
	}
	eaiDestroy(&eaiIDs);

	StructDeInit(parse_ErrorTrackerEntryList, &pContext->entryList);

	do {
		UpdateObjectTransactionManager();
		objContainerSaveTick();
	} while (IsLocalTransactionCurrentlyHappening(objLocalManager()));

	if(pContext->bCreatedContext)
	{
		free(pContext);
		return;
	}
}

// -------------------------------------------------------------------------

// Post-Load updates for config
static void errorTrackerLibConfigUpdate(void)
{
	// Reloaded SVN settings are not applied
	if(gErrorTrackerSettings.iWebInterfacePort == 0)
		gErrorTrackerSettings.iWebInterfacePort = 80;
	if(gErrorTrackerSettings.pDumpDir == NULL)
		estrPrintf(&gErrorTrackerSettings.pDumpDir, "%sdumps", ETWeb_GetDataDir());
	if(gErrorTrackerSettings.pRawDataDir == NULL)
		estrPrintf(&gErrorTrackerSettings.pRawDataDir, "%srawdata", errorTrackerGetDatabaseDir());

	jiraSetDefaultAddress(gErrorTrackerSettings.pJiraHost, gErrorTrackerSettings.iJiraPort);

	printf("DumpDir (Read/Write): %s\n", gErrorTrackerSettings.pDumpDir);
	EARRAY_FOREACH_REVERSE_BEGIN(gErrorTrackerSettings.ppAlternateDumpDir, i);
	{
		if(dirExists(gErrorTrackerSettings.ppAlternateDumpDir[i]))
		{
			printf(" * AlternateDumpDir (Read-Only): %s\n", gErrorTrackerSettings.ppAlternateDumpDir[i]);
		}
		else
		{
			printf(" * Removing AlternateDumpDir , dir doesn't exist: %s\n", gErrorTrackerSettings.ppAlternateDumpDir[i]);
			estrDestroy(&gErrorTrackerSettings.ppAlternateDumpDir[i]);
			eaRemove(&gErrorTrackerSettings.ppAlternateDumpDir, i);
		}
	}
	EARRAY_FOREACH_END;
	if (!gErrorTrackerSettings.pDumpTempDir || !*gErrorTrackerSettings.pDumpTempDir)
		estrCopy2(&gErrorTrackerSettings.pDumpTempDir, gErrorTrackerSettings.pDumpDir);
	ETReloadSVNProductNameMappings();
}

void errorTrackerLibReloadConfig(FolderCache * pFolderCache, FolderNode * pFolderNode, 
	int iVirtualLocation, const char * szRelPath, int iWhen, void * pUserData)
{
	printf("Reloading Error Tracker config...\n");
	StructReset(parse_ErrorTrackerSettings, &gErrorTrackerSettings);
	ParserReadTextFile(szRelPath, parse_ErrorTrackerSettings, &gErrorTrackerSettings, 0);

	errorTrackerLibConfigUpdate();
}

extern bool gbCreateSnapshotMode;
bool errorTrackerLibInit(U32 uOptions, 
						 U32 uWebOptions, 
						 ErrorTrackerSettings *pSettings)
{
	int count;
	errorTrackerLibSetOptions(uOptions);
	
	ErrorTrackerDBInit();
	if (gbCreateSnapshotMode)
		return true;

	if (gpCurrentContext && !gpCurrentContext->entryList.eContainerType)
		gpCurrentContext->entryList.eContainerType = GLOBALTYPE_ERRORTRACKERENTRY;

	ETWeb_EnableDynamicHTMLTitles(true);
	StructCopyAll(parse_ErrorTrackerSettings, pSettings, &gErrorTrackerSettings);
	
	errorTrackerLibConfigUpdate();

	if (gErrorTrackerSettings.pSVNUsername)
		setSVNUsername(gErrorTrackerSettings.pSVNUsername);
	if (gErrorTrackerSettings.pSVNPassword)
		setSVNPassword(gErrorTrackerSettings.pSVNPassword);
	if (gErrorTrackerSettings.pSVNRoot && *gErrorTrackerSettings.pSVNRoot)
		setSVNRoot(gErrorTrackerSettings.pSVNRoot);

	loadstart_printf("Reloading Data...             ");
	initErrorTracker();
	loadend_printf("Done.");

	if (gbMigrateData)
	{
		loadstart_printf("Migrating old data...         ");
		performMigration();
		loadend_printf("Done.");
	}
	if (gbCleanupDumps && (errorTrackerLibGetOptions() & ERRORTRACKER_OPTION_DISABLE_AUTO_SAVE) == 0)
	{
		errorTrackerCleanUpDumps();
	}

	if(gbLogTriviaData)
	{
		LogAllTriviaOverviews();
	}

	if(gbCleanupNonfatals)
	{
		loadstart_printf("Cleaning up useless non-fatal errors... ");
		count = errorTrackerCleanupNonfatals();
		loadend_printf("Done. (%d removed)", count);
	}

	if(gbTrimUsers)
	{
		loadstart_printf("Trimming extra users and IPs... ");
		count = errorTrackerTrimUsers();
		loadend_printf("Done. (%d IDs trimmed)", count);
	}

	ETCommonInit();
	wiSetFlags(uWebOptions);
	initializeEntries(); // this all occurs after database is reloaded

	printf("\nErrorTracker Web Address: http://%s/\n", getMachineAddress());

	return true;
}

void errorTrackerLibShutdown(void)
{
	errorTrackerLibFlush();

	shutdownWebInterface();
	shutdownIncomingData();
	shutdownErrorTracker();

	objContainerSaveTick();
	objForceRotateIncrementalHog();
	objCloseContainerSource();
	logWaitForQueueToEmpty();
}

void errorTrackerLibOncePerFrame(void)
{
	PERFINFO_AUTO_START_FUNC();
	errorTrackerLibOncePerFrame_MainThread();
	errorTrackerLibOncePerFrame_SubThread();
	PERFINFO_AUTO_STOP();
}

// These two functions are for the CB
void errorTrackerLibOncePerFrame_MainThread(void)
{
	PERFINFO_AUTO_START_FUNC();
	UpdateObjectTransactionManager();
	objContainerSaveTick();

	// These need to occur in the same thread as the Transaction Manager
	ErrorEntry_OncePerFrame();
	BlameCache_OncePerFrame();
	SaveJiraUpdates();
	PERFINFO_AUTO_STOP();
}
void errorTrackerLibOncePerFrame_SubThread(void)
{
	commMonitor(errorTrackerCommDefault());
	updateErrorTracker();
}

void errorTrackerLibUpdateBlameCache(void)
{
	updateRequestedBlameInfo();
}

void errorTrackerLibFlush(void)
{
	blameCacheWait();
	//dumpSendWait();

	// Wait for web downloads to complete?
}

extern bool gbStopWaitingForDumpConnection;

void errorTrackerLibWaitForDumpConnection(F32 timeout)
{
	int timeoutTimer = timerAlloc();
	gbStopWaitingForDumpConnection = false;

	while(timerElapsed(timeoutTimer) < timeout)
	{
		Sleep(10);

		errorTrackerLibOncePerFrame();

		if(gbStopWaitingForDumpConnection)
			break;
	}

	timerFree(timeoutTimer);
}

// -------------------------------------------------------------------------

ErrorTrackerEntryList * errorTrackerLibGetEntries(ErrorTrackerContext *pContext)
{
	return &pContext->entryList;
}

ErrorEntry * errorTrackerLibSearchFirst(ErrorTrackerContext *pContext, SearchData *pSearchData)
{
	return searchFirst(pContext, pSearchData);
}

ErrorEntry * errorTrackerLibSearchNext (ErrorTrackerContext *pContext, SearchData *pSearchData)
{
	return searchNext(pContext, pSearchData);
}

// -------------------------------------------------------------------------
// Forwards and externs

extern char gFQDN[MAX_PATH];
extern char gDefaultPage[MAX_PATH];
// -------------------------------------------------------------------------

void errorTrackerLibSetWebRoot(const char *pRootPath)
{
	ETWeb_SetSourceDir(pRootPath);
}

void errorTrackerLibSetDefaultPage(const char *pPage)
{
	strcpy_s(gDefaultPage, MAX_PATH, pPage);
}

void errorTrackerLibStartSummaryTable(char **estr)
{
	appendSummaryHeader(estr, STF_DEFAULT);
}

void errorTrackerLibDumpEntryToSummaryTable(char **estr, ErrorEntry *pEntry)
{
	wiAppendSummaryTableEntry(pEntry, estr, pEntry->ppExecutableNames, false, 0, 100, pEntry->iTotalCount, STF_DEFAULT);
}

void errorTrackerLibEndSummaryTable(char **estr)
{
	estrConcatf(estr, "</table>\n");
}

void errorTrackerLibGetDumpFilename(char *dumpFilename, int dumpFilename_size, ErrorEntry *pEntry, DumpData *pDumpData, bool bGetMinidump)
{
	int i = -1; // -1 is for pDumpDir
	int altDirCount = eaSize(&gErrorTrackerSettings.ppAlternateDumpDir);
	char *ext;
	bool bIsMinidump = false;
	int idx;

	if (bGetMinidump || (pDumpData->uFlags & DUMPDATAFLAGS_FULLDUMP) == 0)
	{
		ext = "mdmp";
		bIsMinidump= true;
	}
	else
		ext = "dmp";

	if (bIsMinidump && (pDumpData->uFlags & DUMPDATAFLAGS_FULLDUMP))
		idx = pDumpData->iMiniDumpIndex;
	else
		idx = pDumpData->iDumpIndex;
	
	calcWriteDumpPath(dumpFilename, dumpFilename_size, pEntry->uID, idx, stricmp(ext, "mdmp"));
	if(fileExists(dumpFilename))
	{
		return;
	}

	while(i < altDirCount)
	{

		sprintf_s(SAFESTR2(dumpFilename), "%s\\%d\\%d.%s.gz", 
			(i == -1) ? gErrorTrackerSettings.pDumpDir : gErrorTrackerSettings.ppAlternateDumpDir[i], 
			pEntry->uID, idx, ext);

		if(fileExists(dumpFilename))
		{
			return;
		}
		i++;
	}
}

void errorTrackerLibStallUntilTransactionsComplete(void)
{
	do {
		errorTrackerLibOncePerFrame_MainThread();
	} while (IsLocalTransactionCurrentlyHappening(objLocalManager()));
}

void errorTrackerLibProcessNewErrorData(ErrorTrackerContext *pContext, ErrorData *pErrorData)
{
	NOCONST(ErrorEntry) *pEntry;
	int iETIndex = 0;

	pEntry = createErrorEntry_ErrorTracker(NULL, NULL, pErrorData, 0);
	if(pEntry == NULL)
	{
		// createErrorTrackerEntryFromErrorData() didn't like it.
		return;
	}
	pEntry->pUserData = StructCreate(parse_ErrorTrackerEntryUserData); // initialize this so it's ALWAYS there
	ProcessEntry(NULL, NULL, pEntry);
	return;
}
void errorTrackerLibSetCBErrorStatus (GlobalType eErrorType, ErrorEntry *pEntry, CBErrorStatus eStatus)
{
	if (pEntry->pUserData)
	{
		objRequestTransactionSimplef(NULL, eErrorType, pEntry->uID, "setCBStatus", "set pUserData.eStatus = %d", eStatus);
	}
}

// -------------------------------------------------------------------------

static NewEntryCallback spNewEntryCallback = NULL;

void errorTrackerLibSetNewEntryCallback(NewEntryCallback pCallback)
{
	spNewEntryCallback = pCallback;
}

void errorTrackerLibCallNewEntryCallback(ErrorTrackerContext *pContext, NOCONST(ErrorEntry) *pNewEntry, ErrorEntry *pMergedEntry)
{
	if(spNewEntryCallback)
	{
		spNewEntryCallback(pContext, CONST_ENTRY(pNewEntry), pMergedEntry);
	}
}

ErrorEntry *errorTrackerLibLookupHashString(ErrorTrackerContext *pContext, const char *pHashString)
{
	U32 tempHash[4];
	ContainerIterator iter = {0};
	Container *currCon = NULL;

	if(4 != sscanf(pHashString, "%u_%u_%u_%u", &tempHash[0], &tempHash[1], &tempHash[2], &tempHash[3]))
	{
		return NULL;
	}

	objInitContainerIteratorFromType(GLOBALTYPE_ERRORTRACKERENTRY, &iter);
	currCon = objGetNextContainerFromIterator(&iter);
	while (currCon)
	{
		ErrorEntry *pEntry = CONTAINER_ENTRY(currCon);
		if(hashMatchesU32(pEntry->aiUniqueHash, tempHash) || hashMatchesU32(pEntry->aiUniqueHashNew, tempHash))
		{
			objClearContainerIterator(&iter);
			return pEntry;
		}
		currCon = objGetNextContainerFromIterator(&iter);
	}
	objClearContainerIterator(&iter);

	return NULL;
}

bool errorTrackerLibGenerateErrorTrackerHandlerFile(ErrorEntry *pEntry, DumpData *pDumpData, const char *pOutputFile)
{
	char *textBlock = NULL;
	ErrorTrackerHandlerInfo info = {0};
	U32 magic, version, len;
	char dumpFilename[MAX_PATH];
	bool bFileExists;
	FILE *pIn, *pOut;
	int iBytesRead;
	char readBuffer[1024];

	magic   = MAGIC_HANDLER_HEADER;
	version = 1;

	// TODO(Theo) Fix this?
	errorTrackerLibGetDumpFilename(dumpFilename, MAX_PATH, pEntry, pDumpData, false);

	bFileExists = fileExists(dumpFilename);
	if(!bFileExists)
	{
		return false;
	}

	info.uID = pEntry->uID;
	info.bFullDump = (pDumpData->uFlags & DUMPFLAGS_FULLDUMP);
	estrClear(&textBlock);
	ParserWriteText(&textBlock, parse_ErrorTrackerHandlerInfo, &info, 0, 0, 0);
	len = (int)strlen(textBlock);

	pIn = fopen(dumpFilename, "rb");
	if(!pIn)
	{
		estrDestroy(&textBlock);
		return false;
	}

	pOut = fopen(pOutputFile, "wb");
	if(!pOut)
	{
		fclose(pIn);
		estrDestroy(&textBlock);
		return false;
	}

	fwrite(&magic, sizeof(U32), 1, pOut);
	fwrite(&version, sizeof(U32), 1, pOut);
	fwrite(&len, sizeof(U32), 1, pOut);
	fwrite(textBlock, 1, len, pOut);

	while(iBytesRead = (int)fread(readBuffer, 1, 1024, pIn))
	{
		fwrite(readBuffer, 1, iBytesRead, pOut);
	}

	fclose(pIn);
	fclose(pOut);
	estrDestroy(&textBlock);
	return true;
}
