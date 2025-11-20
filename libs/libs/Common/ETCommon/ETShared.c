#include "ETCommon/ETShared.h"
#include "ETCommon/ETCommonStructs.h"
#include "AutoGen/ETCommonStructs_h_ast.h"
#include "ETCommon/symstore.h"
#include "ETCommon/ETDumps.h"
#include "ETCommon/ETWebCommon.h"
#include "../../libs/ErrorTrackerLib/ErrorEntry.h"

#include "Alerts.h"
#include "callstack.h"
#include "crypt.h"
#include "CrypticPorts.h"
#include "earray.h"
#include "errornet.h"
#include "estring.h"
#include "fastAtoi.h"
#include "file.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "GlobalComm.h"
#include "net.h"
#include "objContainer.h"
#include "StashTable.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "timing.h"
#include "trivia.h"
#include <winsock2.h>
#include "utf8.h"

#include "AutoGen/ETShared_h_ast.h"
#include "Autogen/callstack_h_ast.h"
#include "AutoGen/errornet_h_ast.h"

extern ParseTable parse_TriviaData[];
#define TYPE_parse_TriviaData TriviaData
StashTable errorSourceFileLineTable; // Contains counts of ERRORDATATYPE_ERROR based on "<source file>:<line>"
extern StashTable dumpIDToDumpDataTable;
extern ErrorTrackerSettings gErrorTrackerSettings;
extern int gMaxIncomingDumps;
extern int gMaxIncomingDumpsPerEntry;
extern int giCurrentDumpReceives;
extern bool gbETVerbose;
extern bool gIgnoreCallocTimed;

static StashTable uniqueHashTable;
static StashTable uniqueHashNewTable;

// Fully Qualified Domain Name
char gFQDN[MAX_PATH] = "";
AUTO_CMD_STRING(gFQDN, FQDN);

bool gbDisableClientMemoryDumps = false;
AUTO_CMD_INT(gbDisableClientMemoryDumps, DisableClientMemoryDumps);

void ErrorTrackerStashTableInit()
{
	int count; 
	ContainerIterator iter = {0};
	Container *currCon = NULL;
	ErrorEntry **ppEntriesToMerge = NULL;
	static char hashString[128];
	PERFINFO_AUTO_START_FUNC();

	count = objCountTotalContainersWithType(GLOBALTYPE_ERRORTRACKERENTRY);
	uniqueHashTable = stashTableCreateWithStringKeys(count, StashDeepCopyKeys_NeverRelease);
	uniqueHashNewTable = stashTableCreateWithStringKeys(count, StashDeepCopyKeys_NeverRelease);

	objInitContainerIteratorFromType(GLOBALTYPE_ERRORTRACKERENTRY, &iter);
	currCon = objGetNextContainerFromIterator(&iter);
	while (currCon)
	{
		ErrorEntry *pEntry = CONTAINER_ENTRY(currCon);
		if(hasHash(pEntry->aiUniqueHash))
		{
			sprintf(hashString, "%u_%u_%u_%u", pEntry->aiUniqueHash[0], pEntry->aiUniqueHash[1], pEntry->aiUniqueHash[2], pEntry->aiUniqueHash[3]);
			if(!stashAddPointer(uniqueHashTable, hashString, pEntry, false))
			{
				eaPush(&ppEntriesToMerge, pEntry);
			}
		}
		if(hasHash(pEntry->aiUniqueHashNew))
		{
			sprintf(hashString, "%u_%u_%u_%u", pEntry->aiUniqueHashNew[0], pEntry->aiUniqueHashNew[1], pEntry->aiUniqueHashNew[2], pEntry->aiUniqueHashNew[3]);
			if(!stashAddPointer(uniqueHashNewTable, hashString, pEntry, false))
			{
				eaPush(&ppEntriesToMerge, pEntry);
			}
		}

		currCon = objGetNextContainerFromIterator(&iter);
	}
	objClearContainerIterator(&iter);

	if (eaSize(&ppEntriesToMerge) > 0)
	{
		int i;
		for (i = 0; i < eaSize(&ppEntriesToMerge); i++)
		{
			ErrorEntry *pEntry = ppEntriesToMerge[i];
			ErrorEntry *pMergedEntry = NULL;
			if (hasHash(pEntry->aiUniqueHash))
			{
				sprintf(hashString, "%u_%u_%u_%u", pEntry->aiUniqueHash[0], pEntry->aiUniqueHash[1], pEntry->aiUniqueHash[2], pEntry->aiUniqueHash[3]);
				if(stashFindPointer(uniqueHashTable, hashString, &pMergedEntry) && pMergedEntry)
				{
					if (pMergedEntry->uID != pEntry->uID)
					{
						if (pEntry->pJiraIssue && !pMergedEntry->pJiraIssue)
						{
							ErrorEntry_MergeAndDeleteEntry(pMergedEntry, pEntry, true);
							stashAddPointer(uniqueHashTable, hashString, pEntry, true);
						}
						else
						{
							ErrorEntry_MergeAndDeleteEntry(pEntry, pMergedEntry, true);
							stashAddPointer(uniqueHashTable, hashString, pMergedEntry, true);
						}
						ppEntriesToMerge[i] = NULL;
						continue;
					}
				}
			}
			if (hasHash(pEntry->aiUniqueHashNew))
			{
				sprintf(hashString, "%u_%u_%u_%u", pEntry->aiUniqueHashNew[0], pEntry->aiUniqueHashNew[1], pEntry->aiUniqueHashNew[2], pEntry->aiUniqueHashNew[3]);
				if(stashFindPointer(uniqueHashNewTable, hashString, &pMergedEntry) && pMergedEntry)
				{
					if (pMergedEntry->uID != pEntry->uID)
					{
						if (pEntry->pJiraIssue && !pMergedEntry->pJiraIssue)
						{
							ErrorEntry_MergeAndDeleteEntry(pMergedEntry, pEntry, true);
							stashAddPointer(uniqueHashNewTable, hashString, pEntry, true);
						}
						else
						{
							ErrorEntry_MergeAndDeleteEntry(pEntry, pMergedEntry, true);
							stashAddPointer(uniqueHashNewTable, hashString, pMergedEntry, true);
						}
						ppEntriesToMerge[i] = NULL;
						continue;
					}
				}
			}
			ErrorOrAlert("FailedErrorMerge", "Found duplicate error #%i but was unable to merge it.", pEntry->uID);
		}
		eaDestroy(&ppEntriesToMerge);
	}

	PERFINFO_AUTO_STOP();
}

void addEntryToStashTables(ErrorEntry *pEntry)
{
	static char hashString[128];
	if(hasHash(pEntry->aiUniqueHash))
	{
		sprintf(hashString, "%u_%u_%u_%u", pEntry->aiUniqueHash[0], pEntry->aiUniqueHash[1], pEntry->aiUniqueHash[2], pEntry->aiUniqueHash[3]);
		stashAddPointer(uniqueHashTable, hashString, pEntry, false);
	}
	if(hasHash(pEntry->aiUniqueHashNew))
	{
		sprintf(hashString, "%u_%u_%u_%u", pEntry->aiUniqueHashNew[0], pEntry->aiUniqueHashNew[1], pEntry->aiUniqueHashNew[2], pEntry->aiUniqueHashNew[3]);
		stashAddPointer(uniqueHashNewTable, hashString, pEntry, false);
	}
}

AUTO_TRANS_HELPER;
void removeEntryFromStashTables(ATH_ARG NOCONST(ErrorEntry) *pEntry)
{
	static char hashString[128];
	if(hasHash(pEntry->aiUniqueHash))
	{
		sprintf(hashString, "%u_%u_%u_%u", pEntry->aiUniqueHash[0], pEntry->aiUniqueHash[1], pEntry->aiUniqueHash[2], pEntry->aiUniqueHash[3]);
		stashRemovePointer(uniqueHashTable, hashString, NULL);
	}
	if(hasHash(pEntry->aiUniqueHashNew))
	{
		sprintf(hashString, "%u_%u_%u_%u", pEntry->aiUniqueHashNew[0], pEntry->aiUniqueHashNew[1], pEntry->aiUniqueHashNew[2], pEntry->aiUniqueHashNew[3]);
		stashRemovePointer(uniqueHashNewTable, hashString, NULL);
	}
}

NetComm *errorTrackerCommDefault()
{
	static NetComm	*comm;

	if (!comm)
		comm = commCreate(0,1);
	return comm;
}

void errorTrackerLibSendWrappedString(NetLink *link, const char *pString)
{
	httpSendWrappedString(link, pString ? pString : "", NULL, NULL);
}
char *errorTrackerLibStringFromUniqueHash(ErrorEntry *pEntry)
{
	static char retString[128];
	if (hasHash(pEntry->aiUniqueHashNew)) 
		sprintf(retString, "%u_%u_%u_%u", pEntry->aiUniqueHashNew[0], pEntry->aiUniqueHashNew[1], pEntry->aiUniqueHashNew[2], pEntry->aiUniqueHashNew[3]);
	else
		sprintf(retString, "%u_%u_%u_%u", pEntry->aiUniqueHash[0], pEntry->aiUniqueHash[1], pEntry->aiUniqueHash[2], pEntry->aiUniqueHash[3]);
	return retString;
}

char *errorTrackerLibShortStringFromUniqueHash(ErrorEntry *pEntry)
{
	static char retString[16];
	if (hasHash(pEntry->aiUniqueHashNew)) 
		sprintf(retString, "%u", pEntry->aiUniqueHashNew[0] ^ pEntry->aiUniqueHashNew[1] ^ pEntry->aiUniqueHashNew[2] ^ pEntry->aiUniqueHashNew[3]);
	else
		sprintf(retString, "%u", pEntry->aiUniqueHash[0] ^ pEntry->aiUniqueHash[1] ^ pEntry->aiUniqueHash[2] ^ pEntry->aiUniqueHash[3]);
	return retString;
}

// Source Controlled
const char *errorTrackerGetSourceDataDir(void)
{
	static char sDataDir[MAX_PATH] = "";
	if (!sDataDir[0])
	{
		sprintf(sDataDir, "%s/server/ErrorTracker/", fileLocalDataDir());
		forwardSlashes(sDataDir);
	}
	return sDataDir;
}

// Not Source Controlled
const char *errorTrackerGetDatabaseDir(void)
{
	static char path[MAX_PATH] = "";
	if (!path[0])
	{
		fileSpecialDir("errordb", SAFESTR(path));
		if (path[0] == '.')
		{
			char fullPath[MAX_PATH] = {0};
			fileGetcwd(SAFESTR(fullPath));
			strcat(fullPath, path);
			makeLongPathName(fullPath, path);
		}

		strcat(path, "/");
	}
	return path;
}

const char *errorTrackerGetPDBDir(void)
{
	static char path[MAX_PATH] = "";
	if (!path[0])
	{
		strcpy(path, fileTempDir());
		if(!dirExists(path))
		{
			if(mkdir(path))
			{
			}
		}
		strcat(path, "/PDB/");
	}
	return path;
}

const char * getMachineAddress(void)
{
	static char *pMachineAddress = NULL;
	if(pMachineAddress == NULL)
	{
		if(gFQDN[0] != 0)
		{
			estrCopy2(&pMachineAddress, gFQDN);
		}
		else
		{
			estrForceSize(&pMachineAddress, 128);
			gethostname(pMachineAddress, 127);
		}
	}
	return pMachineAddress;
}

void ETShared_SetFQDN(const char *fqdn)
{
	strcpy(gFQDN, fqdn);
}

// Strips the paths from the name
const char * errorExecutableGetName(const char *pExeFullPath)
{
	const char *pNoPathExecutable = pExeFullPath;
	int pathLen = (int) strlen(pExeFullPath);
	pNoPathExecutable += pathLen;

	while (pathLen && (*(pNoPathExecutable-1) !=  '\\') && (*(pNoPathExecutable-1) != '/'))
	{
		pathLen--;
		pNoPathExecutable--;
	}
	return pNoPathExecutable;
}

#define CLIENT_EXE_PATH "server/ErrorTracker/clientExeList.txt"
static ETClientExeList sClientExes = {0};
static void etClientExeReloadCallback(const char *relpath, int when)
{
	loadstart_printf("Reloading Client Exe List...");
	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);
	StructReset(parse_ETClientExeList, &sClientExes);
	if (fileExists(relpath))
		ParserReadTextFile(CLIENT_EXE_PATH, parse_ETClientExeList, &sClientExes, 0);
	loadend_printf("done");
}

void etLoadClientExeList(void)
{
	StructReset(parse_ETClientExeList, &sClientExes);
	if (fileExists(CLIENT_EXE_PATH))
		ParserReadTextFile(CLIENT_EXE_PATH, parse_ETClientExeList, &sClientExes, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, CLIENT_EXE_PATH, etClientExeReloadCallback);
}

bool etExeIsClient(ErrorEntry *pEntry)
{
	const char *exe;
	if (eaSize(&sClientExes.eaExeNames) == 0)
		return true;
	exe = eaSize(&pEntry->ppExecutableNames) ? pEntry->ppExecutableNames[0] : NULL;
	if (!exe)
		return false;
	if (eaSize(&pEntry->ppVersions))
	{
		if (stricmp(pEntry->ppVersions[0], StaticDefineIntRevLookup(GlobalTypeEnum, GLOBALTYPE_CRYPTICLAUNCHER)) == 0 &&
			strEndsWith(exe, "(Production)"))
		{
			// Check to see if it's a production-mode CrypticLauncher; these exe names are unreliable
			return true;
		}
	}
	if (strstri(exe, "CrypticError.exe"))
		return true;
	EARRAY_CONST_FOREACH_BEGIN(sClientExes.eaExeNames, i, s);
	{
		if (sClientExes.eaExeNames[i] && strStartsWith(exe, sClientExes.eaExeNames[i]))
			return true;
	}
	EARRAY_FOREACH_END;
	return false;
}

// -----------------------------------------------------------------------
// Context stuff

ErrorTrackerContext sDefaultContext = {0};
ErrorTrackerContext *gpCurrentContext = &sDefaultContext;


ErrorTrackerContext * errorTrackerLibCreateContext(void)
{
	ErrorTrackerContext *pContext = StructCreate(parse_ErrorTrackerContext);
	pContext->bCreatedContext = true;
	pContext->entryList.eContainerType = GLOBALTYPE_ERRORTRACKERENTRY;

	return pContext;
}
ErrorTrackerContext * errorTrackerLibGetCurrentContext(void)
{
	return gpCurrentContext;
}
AUTO_COMMAND ACMD_CATEGORY(ET_Debug);
void errorTrackerLibDestroyCurrentContext(void)
{
	//errorTrackerLibDestroyContext(gpCurrentContext);
	// TODO does nothing now
}
void errorTrackerLibSetCurrentContext(ErrorTrackerContext *pContext)
{
	if(pContext)
	{
		gpCurrentContext = pContext;
	}
	else
	{
		gpCurrentContext = &sDefaultContext;
	}
}
GlobalType errorTrackerLibGetCurrentType(void)
{
	if (gpCurrentContext)
	{
		return gpCurrentContext->entryList.eContainerType;
	}
	else
	{
		return GLOBALTYPE_ERRORTRACKERENTRY;
	}
}

// -----------------------------------------------------------------------

void errorTrackerLibSetType(ErrorTrackerContext *pContext, GlobalType eType)
{
	pContext->entryList.eContainerType = eType;
}

ErrorEntry * findErrorTrackerByIDEx(U32 uID, char *file, int line)
{
	Container * con;
	PERFINFO_AUTO_START_FUNC();
	con = objGetContainer(errorTrackerLibGetCurrentType(), uID);
	if (con)
	{
		ErrorEntry *pEntry = CONTAINER_ENTRY(con);
		if (pEntry->uID != uID)
		{
			char buffer[256];
			sprintf(buffer, "Error Tracker ID #%d freed but is still in the DB!", uID);
			TriggerAlertEx(allocAddString("ERRORTRACKER_BADSTATE"), buffer, ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS,
				0, GetAppGlobalType(), GetAppGlobalID(), GetAppGlobalType(), GetAppGlobalID(), 
				getHostName(), 0, file, line);
			PERFINFO_AUTO_STOP();
			return NULL;
		}
		PERFINFO_AUTO_STOP();
		return pEntry;
	}
	PERFINFO_AUTO_STOP();
	return NULL;
}

//Prints the old hash string for a given ETID
AUTO_COMMAND ACMD_CATEGORY(ET_Debug);
char *findOldHashStringForEntry(U32 uId)
{
	ErrorEntry *pEntry = findErrorTrackerByID(uId);
	char *pCRCString = NULL;

	NOCONST(ErrorEntry) *pNoConstEntry = StructCloneDeConst(parse_ErrorEntry, pEntry);

	ET_ConstructHashString(&pCRCString, pNoConstEntry, false);

	StructDestroyNoConst(parse_ErrorEntry, pNoConstEntry);

	return pCRCString;
}

//Prints the new hash string for a given ETID
AUTO_COMMAND ACMD_CATEGORY(ET_Debug);
char *findNewHashStringForEntry(U32 uId)
{
	ErrorEntry *pEntry = findErrorTrackerByID(uId);
	char *pCRCNewString = NULL;

	NOCONST(ErrorEntry) *pNoConstEntry = StructCloneDeConst(parse_ErrorEntry, pEntry);

	ET_ConstructHashString(&pCRCNewString, pNoConstEntry, true);

	StructDestroyNoConst(parse_ErrorEntry, pNoConstEntry);

	return pCRCNewString;
}

//Finds the ID of the first ErrorEntry with a matching hash.
AUTO_COMMAND ACMD_CATEGORY(ET_Debug);
U32 findIDFromUniqueHash(U32 h0, U32 h1, U32 h2, U32 h3)
{
	ErrorEntry *returnVal = NULL;
	static char hashString[128];

	PERFINFO_AUTO_START_FUNC();

	sprintf(hashString, "%u_%u_%u_%u", h0, h1, h2, h3);

	//We don't really care about finding old merges here, so search the more relevant stashtable first.
	if(stashFindPointer(uniqueHashNewTable, hashString, &returnVal) && returnVal)
	{
		PERFINFO_AUTO_STOP();
		return returnVal->uID;
	}

	if(stashFindPointer(uniqueHashTable, hashString, &returnVal) && returnVal)
	{
		PERFINFO_AUTO_STOP();
		return returnVal->uID;
	}

	PERFINFO_AUTO_STOP();
	return 0;
}

ErrorEntry * findErrorTrackerEntryFromNewEntry(NOCONST(ErrorEntry) *pNewEntry)
{
	ErrorEntry *returnVal = NULL;
	static char hashString[128];

	PERFINFO_AUTO_START_FUNC();

	if (hasHash(pNewEntry->aiUniqueHash))
	{
		sprintf(hashString, "%u_%u_%u_%u", pNewEntry->aiUniqueHash[0], pNewEntry->aiUniqueHash[1], pNewEntry->aiUniqueHash[2], pNewEntry->aiUniqueHash[3]);
		if(stashFindPointer(uniqueHashTable, hashString, &returnVal) && returnVal)
		{
			PERFINFO_AUTO_STOP();
			return returnVal;
		}
	}
	if (hasHash(pNewEntry->aiUniqueHashNew))
	{
		sprintf(hashString, "%u_%u_%u_%u", pNewEntry->aiUniqueHashNew[0], pNewEntry->aiUniqueHashNew[1], pNewEntry->aiUniqueHashNew[2], pNewEntry->aiUniqueHashNew[3]);
		if(stashFindPointer(uniqueHashNewTable, hashString, &returnVal) && returnVal)
		{
			PERFINFO_AUTO_STOP();
			return returnVal;
		}
	}

	PERFINFO_AUTO_STOP();
	return NULL;
}

bool hasHash(const U32 *hash)
{
	if (hash && (hash[0] != 0 || hash[1] != 0 || hash[2] != 0 || hash[3] != 0))
		return true;
	else
		return false;
}

bool hashMatchesU32(const U32 *h1, const U32 *h2)
{
	return (
		(h1[0] == h2[0])
		&&  (h1[1] == h2[1])
		&&  (h1[2] == h2[2])
		&&  (h1[3] == h2[3]));
}
bool hashMatches(ErrorEntry *p1, ErrorEntry *p2)
{
	if (hasHash(p1->aiUniqueHash) && hashMatchesU32(p1->aiUniqueHash, p2->aiUniqueHash))
	{
		return true;
	}
	else if (hasHash(p1->aiUniqueHashNew) && hasHash(p2->aiUniqueHashNew) && hashMatchesU32(p1->aiUniqueHashNew, p2->aiUniqueHashNew))
	{
		return true;
	}
	else
	{
		return false;
	}
}

const char * ErrorDataTypeToString(ErrorDataType eType)
{
	switch(eType)
	{
		xcase ERRORDATATYPE_ASSERT:         return "Assert";
		xcase ERRORDATATYPE_ERROR:          return "Error";
		xcase ERRORDATATYPE_FATALERROR:     return "Fatal Error";
		xcase ERRORDATATYPE_CRASH:          return "Crash";
		xcase ERRORDATATYPE_COMPILE:        return "Compile";
		xcase ERRORDATATYPE_GAMEBUG:		return "Manual Dump";
		xcase ERRORDATATYPE_XPERF:			return "Xperf";
	};
	return "Unknown";
}

const char *getPlatformName(Platform ePlatform)
{
	switch(ePlatform)
	{
		xcase PLATFORM_WIN32:   return "Win32";
		xcase PLATFORM_XBOX360: return "Xbox 360";
		xcase PLATFORM_PS3:		return "PS3";
		xcase PLATFORM_WIN64:	return "Win64";
		xcase PLATFORM_WINE:	return "Wine";
	};

	return "Unknown";
}

Platform getPlatformFromName(const char *pPlatformName)
{
	if(!pPlatformName) 
		return PLATFORM_UNKNOWN;

	if(!strcmp(pPlatformName, "Win32"))    return PLATFORM_WIN32;
	if(!strcmp(pPlatformName, "Xbox 360")) return PLATFORM_XBOX360;
	if(!strcmp(pPlatformName, "PS3"))	   return PLATFORM_PS3;
	if(!strcmp(pPlatformName, "Win64"))	   return PLATFORM_WIN64;
	if(!strcmp(pPlatformName, "Wine"))	   return PLATFORM_WINE;

	return PLATFORM_UNKNOWN;
}

// Returns:
// - "???" if pModuleName is NULL
// - pModuleName if pModuleName isn't found in the executables array
// - "..." if pModuleName IS found in the executables array
const char * getPrintableModuleName(ErrorEntry *p, const char *pModuleName)
{
	if(pModuleName)
	{
		int j;
		for(j=0; j<eaSize(&p->ppExecutableNames); j++)
		{
			if(strstri_safe(p->ppExecutableNames[j], pModuleName))
				return "...";
		}
		return pModuleName;
	}
	return "???";
}

int calcElapsedDays(U32 uStartTime, U32 uEndTime)
{
	int iElapsedDays = (uEndTime - uStartTime) / (24 * 60 * 60);
	if(iElapsedDays < 0)
		iElapsedDays = 0;
	return iElapsedDays;
}

bool findUniqueString(CONST_STRING_EARRAY ppStringList, const char *pStr)
{
	int i, size;

	size = eaSize(&ppStringList);
	for(i=0; i<size; i++)
	{
		if(!stricmp(ppStringList[i], pStr))
		{
			// Already have it.
			return true;
		}
	}
	return false;
}

const char * findNewestVersion( CONST_STRING_EARRAY * const pppVersions) //char ***pppVersions)
{
	int i, size = eaSize(pppVersions);
	const char * pNewestVersion = NULL;
	for(i=0;i<size; i++)
	{
		const char * pCurr = (*pppVersions)[i];

		if(pNewestVersion == NULL && pCurr)
		{
			pNewestVersion = pCurr;
		}
		else if (pCurr)
		{
			if(CompareUsefulVersionStrings(pNewestVersion, pCurr) < 0)
			{
				pNewestVersion = pCurr;
			}
		}
	}
	return pNewestVersion;
}

bool hasValidBlameInfo(ErrorEntry *pEntry)
{
	// We have valid blame info if we've ever gotten blame info at
	// least once, and nothing has flagged this entry for a new query.
	if(pEntry->bBlameDataLocked)
	{
		// Someone has it locked. Pretend it is invalid.
		return false;
	}

	return ((pEntry->iCurrentBlameVersion   != 0) 
		&&  (pEntry->iRequestedBlameVersion == 0));
}

int findDayCount(CONST_EARRAY_OF(DayCount) ppDays, int iDaysSinceFirstTime)
{
	int i;
	for(i=0; i<eaSize(&ppDays); i++)
	{
		if(ppDays[i]->iDaysSinceFirstTime == iDaysSinceFirstTime)
		{
			return ppDays[i]->iCount;
		}
	}

	return 0;
}

// Sends a keep-alive status update message to the client
void errorTrackerSendStatusUpdate(NetLink *link, int update_code)
{
	if (linkConnected(link))
	{
		Packet *pak = pktCreate(link, FROM_ERRORTRACKER_STATUS_UPDATE);
		pktSendU32(pak, update_code);
		pktSend(&pak);
	}
}

void SendDumpFlags(NetLink *link, U32 uFlags, U32 uDumpID, U32 uETID, U32 uDumpIndex)
{
	SendDumpFlagsWithContext(link, uFlags, uDumpID, uETID, uDumpIndex, 0);
}

void SendDumpFlagsWithContext(NetLink *link, U32 uFlags, U32 uDumpID, U32 uETID, U32 uDumpIndex, U32 context)
{
	Packet *pak = pktCreate(link, FROM_ERRORTRACKER_DUMPFLAGS);

	if(errorTrackerLibGetOptions() & ERRORTRACKER_OPTION_REQUEST_AUTOCLOSE_ON_ERROR)
		uFlags |= DUMPFLAGS_AUTOCLOSE;

	pktSendU32(pak, uFlags);
	pktSendU32(pak, uDumpID);
	if(uFlags & DUMPFLAGS_UNIQUEID)
		pktSendU32(pak, uETID);
	if(uFlags & DUMPFLAGS_DUMPINDEX)
		pktSendU32(pak, uDumpIndex);
	if (context > 0)
		pktSendU32(pak, context);
	pktSend(&pak);
}

void sendFailureResponse(NetLink *link)
{
	if (linkConnected(link))
	{
		Packet *pak;
		SendDumpFlags(link, DUMPFLAGS_UNIQUEID, 0, 0, 0);
		pak = pktCreate(link, FROM_ERRORTRACKER_ERRRESPONSE);
		pktSendU32(pak, ERRORRESPONSE_ERRORCREATING);
		pktSendString(pak, "Failed to create Error Tracker entry: Either bad data was received, or max-per-day for this error was hit.");
		pktSend(&pak);
	}
}

int ETShared_ParseSVNRevision (const char *pVersionString)
{
	int iRevision;
	const char *pSVNPart = NULL;
	if (!pVersionString || !*pVersionString) // NULL or empty string, gdiaf
		return 0;
	iRevision = atoi(pVersionString);
	if (iRevision) // String starts with a non-zero numerical value
		return iRevision;

	if (pSVNPart = strstri(pVersionString, "SVN ")) // space is required
		return atoi(pSVNPart+4);
	return 0;
}

// -----------------------------------------------------------------------

static bool stackContainsCallocTimed(ErrorEntry *pEntry)
{
	int i, size = eaSize(&pEntry->ppStackTraceLines);
	for (i=0; i<size; i++)
	{
		if (strstri(pEntry->ppStackTraceLines[i]->pFunctionName, "calloc_timed"))
			return true;
	}
	return false;
}

static bool errorIsNewExecutable(ErrorEntry *pMergedEntry, ErrorEntry *pEntry)
{
	int i;
	char filename[MAX_PATH];

	if (eaSize(&pEntry->ppExecutableNames) == 0) // No executable names
		return false;
	if (!pEntry->ppExecutableNames[0] || !fileGetFilename(pEntry->ppExecutableNames[0], filename)) // Error copying
		return false;
	if (!filename[0]) // Empty String
		return false;

	for (i=eaSize(&pMergedEntry->ppExecutableNames)-1; i>=0; i--)
	{
		if (strstri_safe(pMergedEntry->ppExecutableNames[i], filename))
			return false;
	}
	return true;
}

void calcReadDumpPath(char *filename, int filename_size, U32 uID, int iDumpIndex, bool bFullDump)
{
	char file[20];
	GetErrorEntryDirDashes(strrchr(gErrorTrackerSettings.pDumpDir, '/'), uID, SAFESTR2(filename));
	sprintf_s(SAFESTR(file), "-%d.%s", iDumpIndex, (bFullDump) ? "dmp" : "mdmp");
	strcat_s(SAFESTR2(filename), file);
}

void calcWriteDumpPath(char *filename, int filename_size, U32 uID, int iDumpIndex, bool bFullDump)
{
	char file[20];
	GetErrorEntryDir(gErrorTrackerSettings.pDumpDir, uID, SAFESTR2(filename));
	sprintf_s(SAFESTR(file), "\\%d.%s", iDumpIndex, (bFullDump) ? "dmp.gz" : "mdmp.gz");
	strcat_s(SAFESTR2(filename), file);
}

void calcMemoryDumpPath(char *filename, int filename_size, U32 uID, int iDumpIndex)
{
	char file[20];
	GetErrorEntryDir(gErrorTrackerSettings.pDumpDir, uID, SAFESTR2(filename));
	sprintf_s(SAFESTR(file), "\\memdmp_%d.txt", iDumpIndex);
	strcat_s(SAFESTR2(filename), file);
}

void calcTriviaDataPath(char *filename, int filename_size, U32 uID)
{
	GetErrorEntryDir(gErrorTrackerSettings.pDumpDir, uID, SAFESTR2(filename));
	strcat_s(SAFESTR2(filename), "\\trivia.txt");
}

/*Parses a path to find the errorTracker ID associated with it. Returns 0 if the path given is invalid.
  Will accept either ErrorTracker format (BASEDIR/##### or BASEDIR/##m/###k/###)
*/ 
int parseErrorEntryDir(char *dirPath)
{
	char path[MAX_PATH];
	char *folder;
	char *next_tok = NULL;
	int id = 0;
	size_t len;
	char *last;

	len = strlen(dirPath);
	last = dirPath + len - 1;
	if (len == 0 || *last < '0' || *last > '9')
		return 0;

	strcpy(path, dirPath);

	backSlashes(path);
	folder = strtok_s(path, "\\", &next_tok);
	while (folder)
	{
		if (*folder >= '0' && *folder <= '9') 
		{
			len = strlen(folder);
			last = (folder + len - 1);
			if (*last == 'm' || *last == 'k') 
			{
				*last = '\0';
				last = last - 1;
			}
			if (*last >= '0' && *last <= '9')
				id = id * 1000 + atoi(folder);
		}
		
		folder = strtok_s(NULL, "\\", &next_tok);
	}
	return id;
}

//Creates a path with root directory srcdir for the given ErrorTracker ID, and puts it in dirname.
void GetErrorEntryDir(const char *srcdir, U32 uID, char *dirname, size_t dirname_size)
{
	U32 i, k, m;
	m = uID / 1000000;
	k = (uID - 1000000 * m) / 1000;
	i = uID % 1000;
	sprintf_s(SAFESTR2(dirname), "%s\\%dm\\%dk\\%d", srcdir, m, k, i);
}

void GetErrorEntryDirDashes(const char *srcdir, U32 uID, char *dirname, size_t dirname_size)
{
	U32 i, k, m;
	m = uID / 1000000;
	k = (uID - 1000000 * m) / 1000;
	i = uID % 1000;
	sprintf_s(SAFESTR2(dirname), "%s/%dm-%dk-%d", srcdir, m, k, i);
}

static bool dumpExistsForVersion(ErrorEntry *pMergedEntry, const char *pVersion)
{
	int i;

	if(!pMergedEntry)
		return false;

	for(i=0; i<eaSize(&pMergedEntry->ppDumpData); i++)
	{
		ErrorEntry *pDumpEntry = pMergedEntry->ppDumpData[i]->pEntry;

		// Make sure dumps were actually written
		if((pMergedEntry->ppDumpData[i]->bWritten || pMergedEntry->ppDumpData[i]->bMiniDumpWritten) && 
			pDumpEntry && eaSize(&pDumpEntry->ppVersions) > 0)
		{
			// ppVersions will always have either zero or one entry in it, since its in a DumpData
			if(!stricmp(pDumpEntry->ppVersions[0], pVersion))
				return true;
		}
	}

	return false;
}

int calcRequestedDumpFlags(ErrorEntry *pMergedEntry, ErrorEntry *pEntry)
{
	int iDumpFlags = 0;
	int incomingDumpCount;
	bool bFrom360 = false;
	bool bHaveADumpForThisVersionAlready = false;
	bool bHasCallocTimed = false;
	U32 uTime;

	if(pMergedEntry->eType == ERRORDATATYPE_GAMEBUG)
	{
		if (gbETVerbose)
			printf("Sending: Manual Dump - Full Dump!\n");
		return DUMPFLAGS_FULLDUMP;
	}

	if(eaSize(&pEntry->ppVersions) > 0)
	{
		bHaveADumpForThisVersionAlready = dumpExistsForVersion(pMergedEntry, pEntry->ppVersions[0]);
	}

	if (errorTrackerLibGetOptions() & ERRORTRACKER_OPTION_FORCE_NO_DUMP)
	{
		return 0;
	}

	if(eaSize(&pEntry->ppPlatformCounts) > 0)
	{
		if(pEntry->ppPlatformCounts[0]->ePlatform == PLATFORM_XBOX360)
			bFrom360 = true;
	}
	// Does not ask for full dumps for calloc_timed crashes/asserts
	if (gIgnoreCallocTimed)
	{
		bHasCallocTimed = stackContainsCallocTimed(pMergedEntry);
	}
	else
	{
		bHasCallocTimed = false;
	}

	if (gbETVerbose)
		printf("Full Total: %d\nMini Total: %d\n", pMergedEntry->iFullDumpCount, pMergedEntry->iMiniDumpCount);

	uTime = timeSecondsSince2000();
	incomingDumpCount = getIncomingDumpCount(pMergedEntry->uID);
	if(gMaxIncomingDumpsPerEntry && (incomingDumpCount >= gMaxIncomingDumpsPerEntry))
	{
		if (gbETVerbose) printf("Sending: No dump please! (Already receiving %d dumps for this ID)\n[(cmdline: -MaxIncomingDumpsPerEntry N) or setMaxIncomingDumpsPerEntry N on debug console]\n", incomingDumpCount);
	}
	else if(gMaxIncomingDumps && (giCurrentDumpReceives >= gMaxIncomingDumps))
	{
		if (gbETVerbose) printf("Sending: No dump please! (Already receiving %d total dumps)\n[(cmdline: -MaxIncomingDumps N) or setMaxIncomingDumps N on debug console]\n", giCurrentDumpReceives);
	}
	else if(pEntry->bBlockDumpRequests)
	{
		if (gbETVerbose) printf("Sending: No dump please! (Dump requests explicitly blocked for this crash)\n");
	}
	else if(!bFrom360 && !RunningFromToolsBin(pEntry) && !pEntry->bProductionMode)
	{
		// If the error isn't running from TOOLS/BIN, we aren't interested in a dump.
		if (gbETVerbose) printf("Sending: No dump please! (not in TOOLS/BIN, nor production mode)\n");
	}
	else if (gbDisableClientMemoryDumps && pMergedEntry->iTotalCount <= 3 && pMergedEntry->pErrorString && strstri(pMergedEntry->pErrorString, "failed to allocate") && 
		(eaSize(&pEntry->ppExecutableNames) == 0 || stricmp(pEntry->ppExecutableNames[0], "GameClient.exe (Production)") == 0))
	{
		// TODO(Theo) temporary hack to stop collecting GameClient OOM crashes
	}
	else if (!bHasCallocTimed && (pMergedEntry->bFullDumpRequested || uTime - ERRORTRACKER_DUMP_AGE_CUTOFF > pMergedEntry->uLastSavedDump))
	{
		iDumpFlags |= DUMPFLAGS_FULLDUMP;
		if (gbETVerbose)
			printf("Sending: Full dump please!\n");
	}
	else if (!bHasCallocTimed && errorIsNewExecutable(pMergedEntry, pEntry))
	{
		iDumpFlags |= DUMPFLAGS_FULLDUMP;
		if (gbETVerbose) printf("Sending: New Executable, Full dump please!\n");
	}
	else if(!bHaveADumpForThisVersionAlready)
	{
		iDumpFlags |= DUMPFLAGS_MINIDUMP;
		if (gbETVerbose) printf("Sending: Mini dump please! (haven't seen this version yet)\n");
	}
	else if(pMergedEntry->iMiniDumpCount < MINIDUMP_MAX_THRESHOLD)
	{
		iDumpFlags |= DUMPFLAGS_MINIDUMP;
		if (gbETVerbose) printf("Sending: Mini dump please! (haven't hit the limit yet)\n");
	}
	else if (gbETVerbose)
	{
		printf("Sending: No dump please! (have enough, already saw this version)\n");
	}

	if(iDumpFlags & DUMPFLAGS_FULLDUMP)
	{
		if(pMergedEntry->iMiniDumpCount < MINIDUMP_MAX_THRESHOLD)
		{
			iDumpFlags |= DUMPFLAGS_MINIDUMP;
			if (gbETVerbose) printf("Sending: (also send minidump, haven't hit the limit yet)\n");
		}
	}
	return iDumpFlags;
}

void ET_ConstructHashString (SA_PARAM_NN_VALID char **estrHash, SA_PARAM_NN_VALID NOCONST(ErrorEntry) *pEntry, bool newHash)
{
	// Note: Changing this function in any way other than appending brand new struct
	//       data might invalidate previously existing unique keys. This might lead
	//       to two unique errors that are actually the same one. You have been warned!
	// Note: devassertmsgunique() uses ERRORDATATYPE_FATALERROR
	bool bIgnoreErrorString = pEntry->eType == ERRORDATATYPE_ASSERT;	// Note: devassertmsgunique() uses ERRORDATATYPE_FATALERROR
	// so that the error string will not be ignored.
	char *numbytes;
	bool bIgnoreCallStack = false;
	static const char failAllocationString[] = "failed to allocate "; //This has to match assertOnAllocError (MemTrack.c)
	char *recentNonCrypticModule = NULL; //We generally only want the entry point to non-cryptic calls, so we need to keep it around.

	PERFINFO_AUTO_START_FUNC();

	assert(pEntry);
	// -------------------------------------------------------------------------------
	// Identification
	// * iUniqueID : We're probably generating this right now.
	estrPrintf(estrHash, "%d", pEntry->eType);

	// -------------------------------------------------------------------------------
	// Crash Info
	if (pEntry->pErrorString)
	{
		numbytes = strstr(pEntry->pErrorString, failAllocationString);
		if (newHash && numbytes && pEntry->pLargestMemory)
		{
			int num = strtoul(numbytes + sizeof(failAllocationString) - 1, NULL, 10);
			if (num != 0 && num < 16*1024*1024)
			{
				estrAppend2(estrHash, pEntry->pLargestMemory);
				bIgnoreCallStack = true;
			}
		}
	}
	

	if (ERRORDATATYPE_IS_A_CRASH(pEntry->eType) && pEntry->eType != ERRORDATATYPE_FATALERROR && pEntry->ppStackTraceLines)
	{
		int i;
		int size = eaSize(&pEntry->ppStackTraceLines);
		int countSuccess = 0; // number of lines without "0x__" functions that are failed lookups
		int crypticSuccess = 0;
		for (i=0; i<size; i++)
		{
			if (pEntry->ppStackTraceLines[i] && (strnicmp(pEntry->ppStackTraceLines[i]->pFunctionName, "0x", 2) != 0) &&
				strnicmp(pEntry->ppStackTraceLines[i]->pFunctionName, "-nosymbols-", 11) != 0)
			{
				countSuccess++; // We have at least one frame that went to module!offset at least, so this stack is valid enough to use.
				if (isCrypticModule(pEntry->ppStackTraceLines[i]->pModuleName) && !strchr(pEntry->ppStackTraceLines[i]->pFunctionName, '!'))
				{
					crypticSuccess++; // We have a full symbol resolution in a Cryptic module, so we have enough info to throw out non-cryptic modules.
					break;
				}
			}
		}
		if(countSuccess == 0)
		{
			// Every line in this callstack looks like 0xNNNNNNNN, so we'll need to use the error/expression string
			// to avoid getting dropped into the NULL callstack bucket (hash == 0).
			bIgnoreErrorString = false;
		}

		for (i=0; i < size; i++)
		{
			if (newHash && bIgnoreCallStack)
				break;
			// Skip recursive call entries in the stack for unique ID calculation ... 
			// helps gather up recursive crashes under one ID.
			if (countSuccess && pEntry->ppStackTraceLines[i] && (strnicmp(pEntry->ppStackTraceLines[i]->pFunctionName, "0x", 2) == 0 ||
			   (strnicmp(pEntry->ppStackTraceLines[i]->pFunctionName, "-nosymbols-", 11) == 0)))
				continue;
			// Entries with no function name will now show up as module!offset, so this makes sure our bucketing is still consistent.
			if (countSuccess && pEntry->ppStackTraceLines[i] && !newHash && strchr(pEntry->ppStackTraceLines[i]->pFunctionName, '!'))
				continue;
			// Avoids walking below where the thread actually began
			if (pEntry->ppStackTraceLines[i] && strstri(pEntry->ppStackTraceLines[i]->pFunctionName, "threadstartex") )
				break;
			if(!pEntry->ppStackTraceLines[i]) continue; // This line and the "!pEntry->ppStackTraceLines[i-1]" below are just to dodge static analysis complaints
			if(!i || !pEntry->ppStackTraceLines[i-1] || strcmp(pEntry->ppStackTraceLines[i-1]->pFunctionName, pEntry->ppStackTraceLines[i]->pFunctionName))
			{
				if (newHash && strchr(pEntry->ppStackTraceLines[i]->pFunctionName, '!')) //This stack line is at module!offset
				{
					int j;
					char *moduleOffset = pEntry->ppStackTraceLines[i]->pFunctionName;
					for(j = 0; moduleOffset[j]; j++)
					{
						moduleOffset[j] = tolower(moduleOffset[j]);
					}
					if (!crypticSuccess) // If we don't have any resolved cryptic symbols, we should use the entire stack regardless of anything else.
					{
						estrAppend2(estrHash, moduleOffset);
					}
					else if (!isCrypticModule(pEntry->ppStackTraceLines[i]->pModuleName)) // If we have cryptic symbols, throw out any non-cryptic modules that only get to module!offset.
					{
						recentNonCrypticModule = NULL; // If we were saving a valid frame from above, it's now irrelevant.
					}
					else // We're in a Cryptic module. We always want those, even though it only got to module!offset.
					{
						if (recentNonCrypticModule) // If we have a non-cryptic entry point above this, add it in.
						{
							estrAppend2(estrHash, recentNonCrypticModule);
							recentNonCrypticModule = NULL;
						}
						estrAppend2(estrHash, moduleOffset);
					}
				}
				else if (newHash) // This stack line resolved fully.
				{
					if (!crypticSuccess) // If we don't have any resolved cryptic symbols, we should use the entire stack regardless of anything else.
					{
						estrAppend2(estrHash, pEntry->ppStackTraceLines[i]->pFunctionName);
					}
					else if (!isCrypticModule(pEntry->ppStackTraceLines[i]->pModuleName)) // We fully resolved a non-cryptic module. We should save it in case it's an entry point.
					{
						recentNonCrypticModule = pEntry->ppStackTraceLines[i]->pFunctionName;
					}
					else // We have a fully resolved Cryptic module. Definitely add it in.
					{
						if (recentNonCrypticModule) // Add in an entry point above it if we have one.
						{
							estrAppend2(estrHash, recentNonCrypticModule);
							recentNonCrypticModule = NULL;
						}
						estrAppend2(estrHash, pEntry->ppStackTraceLines[i]->pFunctionName);
					}
				}
				else // We're using the old hash system - just add in all stack lines.
				{
					estrAppend2(estrHash, pEntry->ppStackTraceLines[i]->pFunctionName);
				}
				backSlashes(pEntry->ppStackTraceLines[i]->pFilename);

				// Hack to make identical crashes built from different first-directory-folders match
				if (pEntry->ppStackTraceLines[i]->pFilename && *pEntry->ppStackTraceLines[i]->pFilename)
				{
					if (filePathBeginsWith(pEntry->ppStackTraceLines[i]->pFilename+1, ":\\src") == 0)
					{
						// strip off first folder for comparisons to make src == src_fix == src_prodbuild, etc
						// advance past first backslash
						char * pTruncatedPath = pEntry->ppStackTraceLines[i]->pFilename + 6; // strlen("c:\\src")
						// find the delimiter for the next folder
						pTruncatedPath = strchr(pTruncatedPath, '\\'); 
						if (pTruncatedPath)
						{
							estrAppend2(estrHash, "c:\\src");
							estrAppend2(estrHash, pTruncatedPath);
						}
					}
					else
						estrAppend2(estrHash, pEntry->ppStackTraceLines[i]->pFilename);
					// * iLineNum might change if the file is edited for reasons other than this error,
					//   and we want the new error to still match the previous error. Not unique-id worthy.
				}
			}
			//Don't add functions below main, if any.
			if (newHash && pEntry->ppStackTraceLines[i] && (strstri(pEntry->ppStackTraceLines[i]->pFunctionName, "main") || (strstri(pEntry->ppStackTraceLines[i]->pFunctionName, "WinMain"))))
				break;
		}
	}
	else if (!bIgnoreCallStack) 
	{
		bIgnoreErrorString = false;
	}

	if (!bIgnoreErrorString)
	{
		if(pEntry->pErrorString)
		{
			estrConcatf(estrHash, "%s", pEntry->pErrorString);
		}
		if(pEntry->pCategory)
		{
			estrConcatf(estrHash, "%s", pEntry->pCategory);
		}
	}
	
	if(pEntry->pExpression) // Always use assert expressions
	{
		estrConcatf(estrHash, "%s", pEntry->pExpression);
	}

	if(pEntry->pSourceFile)
	{
		char * pPath = pEntry->pSourceFile;
		// Same hack from the stack trace
		backSlashes(pEntry->pSourceFile);
		if (filePathBeginsWith(pEntry->pSourceFile, "c:\\src") == 0)
		{
			// strip off first folder for comparisons to make src == src_fix == src_prodbuild, etc
			// advance past first backslash
			pPath = pEntry->pSourceFile + 6; // strlen("c:\\src")
			// find the delimiter for the next folder
			pPath = strchr(pPath, '\\'); 
			if (pPath)
			{
				estrAppend2(estrHash, "c:\\src");
			}
			else
			{
				pPath = pEntry->pSourceFile;
			}
		}
		if (newHash)
			estrConcatf(estrHash, "%s", pPath);
		else
			estrConcatf(estrHash, "%s (%d)", pPath, pEntry->iSourceFileLine);
	}

	// Extra stuff used for bucketing XPerf dumps
	if (pEntry->eType == ERRORDATATYPE_XPERF)
	{
		EARRAY_FOREACH_BEGIN(pEntry->ppExecutableNames, i);
		{
			estrConcatf(estrHash, "%s", pEntry->ppExecutableNames[i]);
		}
		EARRAY_FOREACH_END;
	}

	// * ppUserNames doesn't matter
	// * ppExecutableNames doesn't matter
	// * ppProductNames doesn't matter
	// * bProductionMode doesn't matter

	// -------------------------------------------------------------------------------
	// Counts
	// * iTotalCount doesn't matter
	// * ppDayCounts doesn't matter
	// * ppPlatformCounts doesn't matter

	// -------------------------------------------------------------------------------
	// Timing
	// * uFirstTime doesn't matter
	// * uNewestTime doesn't matter

	// -------------------------------------------------------------------------------
	// Versioning
	// * iOldestVersion doesn't matter
	// * iNewestVersion doesn't matter
	// * ppVersions doesn't matter

	// -------------------------------------------------------------------------------
	// Data file info
	if(pEntry->pDataFile)
	{
		estrConcatf(estrHash, "%s %d", 
			pEntry->pDataFile, 
			pEntry->uDataFileTime);
	}
	// * pLastBlamedPerson might change if someone else tries (and fails) to fix it...
	//   it'd still be the same error.

	// -------------------------------------------------------------------------------
	// Client counts
	// * iMaxClients doesn't matter
	// * iTotalClients doesn't matter

	// -------------------------------------------------------------------------------
	// Trivia strings
	// * pTriviaString doesn't matter

	// -------------------------------------------------------------------------------
	// Dump Information
	// * ppDumpData doesn't matter
	// * iMiniDumpCount doesn't matter
	// * iFullDumpCount doesn't matter
	// * bFullDumpRequested doesn't matter

	// -------------------------------------------------------------------------------
	// SVN Blame Information
	// * iCurrentBlameVersion doesn't matter
	// * uCurrentBlameTime doesn't matter
	// * iRequestedBlameVersion doesn't matter

	PERFINFO_AUTO_STOP();
}

// Stored ET version, so it may be "<exe name> (Production)"
// Returns an EString
char *ET_GetExecutableName (char *exeNameOrPath)
{
	char *exeName, *temp;
	if (!exeNameOrPath)
		return NULL;
	temp = strstri(exeNameOrPath, " (Production)");
	if (temp)
	{
		*temp = '\0';
		exeName = estrDup(exeNameOrPath);
		*temp = ' ';
	}
	else
		exeName = estrDup(errorExecutableGetName(exeNameOrPath));
	return exeName;
}

void ET_ConstructHashString_v1 (SA_PARAM_NN_VALID char **estrHash, SA_PARAM_NN_VALID NOCONST(ErrorEntry) *pEntry)
{
	ET_ConstructHashString(estrHash, pEntry, false); 
	if (eaSize(&pEntry->ppExecutableNames))
	{
		char *exeName = ET_GetExecutableName(pEntry->ppExecutableNames[0]);
		if (exeName)
		{
			estrConcatf(estrHash, "%s", exeName);
			estrDestroy(&exeName);
		}
	}
	if (pEntry->bProductionMode)
	{
		estrConcatf(estrHash, "Prod=1");
	}
}

AUTO_TRANS_HELPER;
void recalcUniqueID(ATH_ARG NOCONST(ErrorEntry) *pEntry, U32 uVersion)
{
	// Note: The order of operations in this file matches the struct order...
	//       keeps these large-struct-manipulating function calls in check.

	// Explanation: The "Unique ID" is just a CRC generated using all of the 
	//              pieces of the error that (if matching) indicate that the
	//              error is the exact same error as another. All other entries
	//              "don't matter".

	// Note: Changing this function in any way other than appending brand new struct
	//       data might invalidate previously existing unique keys. This might lead
	//       to two unique errors that are actually the same one. You have been warned!

	char *pCRCString = NULL;
	char *pCRCNewString = NULL;
	PERFINFO_AUTO_START_FUNC();
	switch (uVersion)
	{
	case 1:
		ET_ConstructHashString_v1(&pCRCString, pEntry);
	xcase 0:
	default:
		ET_ConstructHashString(&pCRCString, pEntry, false);  
	}
	ET_ConstructHashString(&pCRCNewString, pEntry, true);

	// -------------------------------------------------------------------------------
	// Unique ID generation

	strupr(pCRCString);
	cryptMD5(pCRCString, (int)strlen(pCRCString), pEntry->aiUniqueHash);

	strupr(pCRCNewString);
	cryptMD5(pCRCNewString, (int)strlen(pCRCNewString), pEntry->aiUniqueHashNew);


	pEntry->uHashVersion = uVersion;
	estrDestroy(&pCRCString);
	estrDestroy(&pCRCNewString);
	PERFINFO_AUTO_STOP();
}

// Returns whether or not data info was found
bool parseErrorType(const char *pStr, NOCONST(ErrorEntry) *pEntry)
{
	char *pDataAuthorIntro = "Last Author/Status:";
	char *pDataFileIntro = "File: ";
	char *pHeaderLoc = strstr(pStr, pDataAuthorIntro);
	char *pNewLineLoc = NULL;
	int len;

	bool bHasDataFileInfo = false;

	char *pLine = NULL;
	char *pLineContext = NULL;
	char *pLineBuffer = NULL;

	char *pOutputErrorString = NULL;

	// Make a maximum amount of room for the non-parsed data
	estrAllocaCreate(&pOutputErrorString, (int)strlen(pStr)+1);
	estrCopy2(&pOutputErrorString, "");

	// Duplicate this string on the stack so that we can abuse it with strtok_s()
	estrAllocaCreate(&pLineBuffer, (int)strlen(pStr)+1);
	estrCopy2(&pLineBuffer, pStr);

	pLine = strtok_s(pLineBuffer, "\n", &pLineContext);
	while(pLine != NULL)
	{
		if(!strncmp(pLine, pDataAuthorIntro, (int)strlen(pDataAuthorIntro)))
		{
			pLine += strlen(pDataAuthorIntro);
			len = (int)strlen(pLine);
			if(len > 0 && !pEntry->pLastBlamedPerson)
			{
				pEntry->pLastBlamedPerson = malloc(len+1);
				strncpy_s(pEntry->pLastBlamedPerson, len+1, pLine, len);
				pEntry->pLastBlamedPerson[len] = 0;
			}
		}
		else if(!strncmp(pLine, pDataFileIntro, (int)strlen(pDataFileIntro)))
		{
			pLine += strlen(pDataFileIntro);
			len = (int)strlen(pLine);
			if(len > 0)
			{
				pEntry->pDataFile = malloc(len+1);
				strncpy_s(pEntry->pDataFile, len+1, pLine, len);
				pEntry->pDataFile[len] = 0;
			}
		}
		else
		{
			// Leftover data, just throw it in the output bin
			estrConcatf(&pOutputErrorString, "%s", pLine);
		}

		pLine = strtok_s(NULL, "\n", &pLineContext);
	}

	bHasDataFileInfo = ((pEntry->pLastBlamedPerson != NULL) && (pEntry->pDataFile != NULL));
	if(bHasDataFileInfo)
	{
		// Use the "non-parsed data" as the error string
		pEntry->pErrorString = strdup(pOutputErrorString);
	}
	else
	{
		pEntry->pErrorString = strdup(pStr);
	}

	estrDestroy(&pOutputErrorString);
	estrDestroy(&pLineBuffer);

	return bHasDataFileInfo;
}

// -----------------------------------------------------------------------

AUTO_TRANS_HELPER;
void addCountForDay(ATH_ARG NOCONST(ErrorEntry) *pEntry, U32 uTime, int iCount)
{
	int i;
	int iDaysSinceFirstTime = calcElapsedDays(pEntry->uFirstTime, uTime);
	NOCONST(DayCount) *pDayCount;

	for(i=0; i<eaSize(&pEntry->ppDayCounts); i++)
	{
		if(iDaysSinceFirstTime == pEntry->ppDayCounts[i]->iDaysSinceFirstTime)
		{
			pEntry->ppDayCounts[i]->iCount += iCount;
			return;
		}
	}
	// If we get here, we need a new DayCount entry
	pDayCount = StructCreateNoConst(parse_DayCount);
	pDayCount->iDaysSinceFirstTime = iDaysSinceFirstTime;
	pDayCount->iCount = iCount;
	eaPush(&pEntry->ppDayCounts, pDayCount);
}

AUTO_TRANS_HELPER;
void addCountForPlatform(ATH_ARG NOCONST(ErrorEntry) *pEntry, Platform ePlatform, int iCount)
{
	int i;
	NOCONST(PlatformCount) *pPlatformCount;

	for(i=0; i<eaSize(&pEntry->ppPlatformCounts); i++)
	{
		if(pEntry->ppPlatformCounts[i]->ePlatform == ePlatform)
		{
			// Add the count to the pre-existing entry.
			pEntry->ppPlatformCounts[i]->iCount += iCount;
			return;
		}
	}
	// Add a new one
	pPlatformCount = StructCreateNoConst(parse_PlatformCount);
	pPlatformCount->ePlatform = ePlatform;
	pPlatformCount->iCount    = iCount;
	eaPush(&pEntry->ppPlatformCounts, pPlatformCount);
}

AUTO_TRANS_HELPER;
void addCountForUser(ATH_ARG NOCONST(ErrorEntry) *pEntry, const char *pUserName, int iCount)
{
	int i;
	NOCONST(UserInfo) *pUserInfo;

	if(!pUserName)
		return;
	for(i=0; i<eaSize(&pEntry->ppUserInfo); i++)
	{
		if(!stricmp(pEntry->ppUserInfo[i]->pUserName, pUserName))
		{
			// Add the count to the pre-existing entry.
			pEntry->ppUserInfo[i]->iCount += iCount;
			return;
		}
	}
	// Add a new one
	pUserInfo = StructCreateNoConst(parse_UserInfo);
	pUserInfo->pUserName = strdup(pUserName);
	pUserInfo->iCount    = iCount;
	eaPush(&pEntry->ppUserInfo, pUserInfo);
}

AUTO_TRANS_HELPER;
void addCountForIP(ATH_ARG NOCONST(ErrorEntry) *pEntry, U32 uIP, int iCount)
{
	int i;
	NOCONST(IPCount) *pIPCount;

	if(!uIP)
		return;

	for(i=0; i<eaSize(&pEntry->ppIPCounts); i++)
	{
		if(pEntry->ppIPCounts[i]->uIP == uIP)
		{
			// Add the count to the pre-existing entry.
			pEntry->ppIPCounts[i]->iCount += iCount;
			return;
		}
	}
	// Add a new one
	pIPCount = StructCreateNoConst(parse_IPCount);
	pIPCount->uIP = uIP;
	pIPCount->iCount = iCount;
	eaPush(&pEntry->ppIPCounts, pIPCount);
}

//Adds a count for an executable to an ErrorEntry. Either adds to the count for an existing executable,
//or adds a new count for a new one.
AUTO_TRANS_HELPER;
void addCountForExecutable(ATH_ARG NOCONST(ErrorEntry) *pEntry, const char *pExecutable, int iCount)
{
	int i;
	if(!pExecutable)
		return;

	addUniqueString (&pEntry->ppExecutableNames, pExecutable);

	for(i=0; i<eaSize(&pEntry->ppExecutableNames); i++)
	{
		if(!stricmp(pEntry->ppExecutableNames[i], pExecutable))
		{
			if (i < eaiSize(&pEntry->ppExecutableCounts)) 
				pEntry->ppExecutableCounts[i] += iCount;
			else if (i == eaiSize(&pEntry->ppExecutableCounts)) 
				eaiPush(&pEntry->ppExecutableCounts, iCount);
			// Older versions without exectuable counts will never get them
		}
	}
}

void CopyStackTraceLine(NOCONST(StackTraceLine) **dst, StackTraceLine *src)
{
	if (!(*dst))
		*dst = StructCreateNoConst(parse_StackTraceLine);

	if (src->pFunctionName)
		estrCopy2(&(*dst)->pFunctionName,  src->pFunctionName);
	if (src->pModuleName)
		estrCopy2(&(*dst)->pModuleName, src->pModuleName);
	if (src->pFilename)
		estrCopy2(&(*dst)->pFilename, src->pFilename);
	(*dst)->iLineNum = src->iLineNum;

	if (src->pBlamedPerson)
		estrCopy2(&(*dst)->pBlamedPerson, src->pBlamedPerson);
	(*dst)->uBlamedRevisionTime = src->uBlamedRevisionTime;
	(*dst)->iBlamedRevision = src->iBlamedRevision;
}

static void destroyStackLine(StackTraceLine *pLine)
{
	StructDestroy(parse_StackTraceLine, pLine);
}
// For PC-style stack data (built into the error string)
void parseStackTraceLines(NOCONST(ErrorEntry) *pEntry, char *pStr)
{
	NOCONST(StackTraceLine) **pLines = NULL;
	NOCONST(StackTraceLine) *pStackTraceLine = NULL;
	char *pLine = pStr;
	char *pCurr = NULL;
	char *pEnd  = NULL;

	PERFINFO_AUTO_START_FUNC();

	while(pLine && *pLine)
	{
		// Expression parsing ... hunting for a line like this:
		// Expression: any_text_here
		pCurr = pLine;
		if(strStartsWith(pLine, "Expression: "))
		{
			pCurr += (int)strlen("Expression: ");

			if(pEntry->pExpression) // just in case "Expression: " is on multiple lines
			{
				free(pEntry->pExpression);
			}
			// Find the end of the line
			pEnd = strchr(pCurr, '\n');

			if(pEnd)
			{
				int iLineLen = pEnd - pCurr;
				if(iLineLen > 0)
				{
					pEntry->pExpression = malloc(iLineLen+1);
					strncpy_s(pEntry->pExpression, iLineLen+1, pCurr, iLineLen);
					pEntry->pExpression[iLineLen] = 0;
				}
			}
			else
			{
				// End of the string, just grab the rest
				pEntry->pExpression = strdup(pCurr);
			}
		}

		// Error parsing ... hunting for a line like this:
		// Error Message: any_text_here
		pCurr = pLine;
		if(strStartsWith(pLine, "Error Message: "))
		{
			pCurr += (int)strlen("Error Message: ");

			if(pEntry->pErrorString) // just in case "Error Message: " is on multiple lines
			{
				free(pEntry->pErrorString);
			}

			// Find the end of the line
			pEnd = strchr(pCurr, '\n');

			if(pEnd)
			{
				int iLineLen = pEnd - pCurr;
				if(iLineLen > 0)
				{
					pEntry->pErrorString = malloc(iLineLen+1);
					strncpy_s(pEntry->pErrorString, iLineLen+1, pCurr, iLineLen);
					pEntry->pErrorString[iLineLen] = 0;
				}
			}
			else
			{
				// End of the string, just grab the rest
				pEntry->pErrorString = strdup(pCurr);
			}
		}

		// Stack trace parsing ... hunting for lines beginning with 
		// a number (leading spaces ignored)
		{
			char *pFuncName = NULL;

			// Start from the beginning of the line
			pCurr = pLine;

			while(*pCurr == ' ') pCurr++; // Skip past any leading spaces

			if(isdigit(*pCurr))
			{
				// This line starts with a number. Assume it is a stack line and read it in
				// Stack lines have the following form:
				//
				//  0 WinMain (400000,0,1423a3,1)
				//                Line: c:\src\core\mastercontrolprogram\mastercontrolprogram.c(1406)
				int iStackDepth = 0;
				while(isdigit(*pCurr))
				{
					iStackDepth = (iStackDepth*10) + *pCurr - '0';
					pCurr++;
				}

				while(*pCurr == ' ') pCurr++; // Skip past any spaces after the number

				// Find the end of this token (the function name)
				pEnd = strchr(pCurr, ' ');
				if(pEnd)
				{
					int iFunctionNameLen = pEnd - pCurr;
					if(iFunctionNameLen > 0)
					{
						estrForceSize(&pFuncName, iFunctionNameLen+1);
						strncpy_s(pFuncName, iFunctionNameLen+1, pCurr, iFunctionNameLen);
						pFuncName[iFunctionNameLen] = 0;

						// Now find the source file/line
						pCurr = strstr(pEnd, "Line:");
						if(pCurr)
						{
							pCurr += 5; // Skip past "Line:"
							while(*pCurr == ' ') pCurr++; // Skip past any spaces after "Line:"

							pEnd = strchr(pCurr, '(');
							if(pEnd)
							{
								int iFileNameLen = pEnd - pCurr;
								if(iFileNameLen > 0)
								{
									char *pSourceFilename = NULL;
									int iLineNum = 0;

									estrForceSize(&pSourceFilename, iFileNameLen+1);
									strncpy_s(pSourceFilename, iFileNameLen+1, pCurr, iFileNameLen);
									pSourceFilename[iFileNameLen] = 0;

									// Now grab the source line
									pCurr = pEnd+1;
									while(isdigit(*pCurr))
									{
										iLineNum = (iLineNum*10) + *pCurr - '0';
										pCurr++;
									}

									// if the function name is one of the following, it is probably
									// just post-crash calls (the assert function, stack dumping, etc)
									// Just throw the stack that we have so far out and continue.

									if((strStartsWith(pFuncName, "superassert")) || strStartsWith(pFuncName, "FatalErrorf")
										|| (strstri(pFuncName, "printf")))
									{
										// Throw out our current stack data.
										eaDestroyEx(&pLines, destroyStackLine);
									}
									else
									{
										// Check to see if we have a module name
										char *pModuleName = NULL;

										pCurr = strstr(pEnd, "Module:");
										if(pCurr)
										{
											pCurr += 7; // Skip past "Module:"
											while(*pCurr == ' ') pCurr++; // Skip past any spaces after "Module:"

											pEnd = strchr(pCurr, '\n');
											if(pEnd)
											{
												int iModuleNameLen = pEnd - pCurr;
												if(iModuleNameLen > 0)
												{
													estrForceSize(&pModuleName, iModuleNameLen+1);
													strncpy_s(pModuleName, iModuleNameLen+1, pCurr, iModuleNameLen);
													pModuleName[iModuleNameLen] = 0;
												}
											}
										}

										// We now have a complete stack entry ... add it in
										pStackTraceLine = StructCreateNoConst(parse_StackTraceLine);
										pStackTraceLine->pFunctionName = pFuncName;
										pStackTraceLine->pFilename     = pSourceFilename;
										pStackTraceLine->pModuleName   = pModuleName;
										pStackTraceLine->iLineNum      = iLineNum;

										eaPush(&pLines, pStackTraceLine);
									}
								}
							}
							else
								estrDestroy(&pFuncName);
						}
						else
							estrDestroy(&pFuncName);
					}
				}
			}
		}

		// Find the beginning of the next line
		pLine = strchr(pLine, '\n');
		if (pLine) // Advance past the newline marker
			pLine++;
		else // No more lines!
			break;
	};

	pEntry->ppStackTraceLines = pLines;

	PERFINFO_AUTO_STOP();
}

// -----------------------------------------------------------------------
// Creating a New ErrorEntry

AUTO_TRANS_HELPER;
void etAddProductCount(ATH_ARG NOCONST(ErrorEntry) *pEntry, const char *productName, U32 uCount)
{
	NOCONST(ErrorCountData) *data;	
	if (!productName || !*productName)
		return;
	if (!pEntry->eaProductOccurrences)
		eaIndexedEnableNoConst(&pEntry->eaProductOccurrences, parse_ErrorCountData);
	data = eaIndexedGetUsingString(&pEntry->eaProductOccurrences, productName);
	if (!data)
	{
		data = StructCreateNoConst(parse_ErrorCountData);
		data->key = StructAllocString(productName);
		eaIndexedAdd(&pEntry->eaProductOccurrences, data);
	}
	data->uCount += uCount;
}

// Generates a brand new ErrorEntry, which will either be merged into a 
// pre-existing ErrorEntry (using mergeErrorTrackerEntries()), or will
// be added to our full list.
NOCONST(ErrorEntry) * createErrorEntryFromErrorData(ErrorData *pErrorData, int iMergedId, bool *pbExternalSymsrv)
{
	// Note: The order of operations in this file matches the struct order...
	//       keeps these large-struct-manipulating function calls in check.

	Platform ePlatform = getPlatformFromName(pErrorData->pPlatformName);
	NOCONST(ErrorEntry) *pEntry = StructCreateNoConst(parse_ErrorEntry);
	bool receivedOldFormat = false;
	bool addToQueue = false;

	PERFINFO_AUTO_START_FUNC();

	// -------------------------------------------------------------------------------
	// Identification
	// * iUniqueID is deprecated
	// * uID is set when added to the list
	// * aiUniqueHash calculated at very end
	pEntry->eType = pErrorData->eType;

	// -------------------------------------------------------------------------------
	// Crash Info

	if(pErrorData->pStackData)
	{
		if (strstr(pErrorData->pStackData, "Line:") == NULL)
		{
			if (!iMergedId && pbExternalSymsrv && *pbExternalSymsrv)
				addToQueue = true;
			else if (!iMergedId)
			{
				// Never call the symstore normally from this
				parseStackTraceLines(pEntry, pErrorData->pStackData);
			}
		}
		else
		{
			// Using old method for performance reasons
			parseStackTraceLines(pEntry, pErrorData->pStackData);
		}
	}

	// Copying error and/or expression strings
	if(ERRORDATATYPE_IS_A_CRASH(pErrorData->eType))
	{
		if(!receivedOldFormat && pErrorData->pErrorString)
			pEntry->pErrorString = strdup(pErrorData->pErrorString);

		if(!receivedOldFormat && pErrorData->eType == ERRORDATATYPE_ASSERT && pErrorData->pExpression)
			pEntry->pExpression  = strdup(pErrorData->pExpression);
	}
	if (pErrorData->eType == ERRORDATATYPE_GAMEBUG)
	{
		if(pErrorData->pErrorString)
			pEntry->pErrorString = strdup(pErrorData->pErrorString);

		if(pErrorData->pExpression)
			pEntry->pCategory = strdup(pErrorData->pExpression);

		if(pErrorData->pErrorSummary)
			pEntry->pErrorSummary = strdup(pErrorData->pErrorSummary);
	}
	if(ERRORDATATYPE_IS_A_ERROR(pErrorData->eType))
	{
		if (pErrorData->pAuthor)
		{
			pEntry->pLastBlamedPerson = StructAllocString(pErrorData->pAuthor);
		}
		if(pErrorData->pErrorString)
		{
			parseErrorType(pErrorData->pErrorString, pEntry);
			if (!pEntry->pDataFile && pErrorData->pDataFile)
				pEntry->pDataFile = strdup(pErrorData->pDataFile);
			pEntry->uDataFileTime = pErrorData->iDataFileModificationTime;
		}
	}
	if (pErrorData->eType == ERRORDATATYPE_COMPILE)
	{
		if(pErrorData->pErrorString)
			pEntry->pErrorString = strdup(pErrorData->pErrorString);
	}


	// * pError is (potentially) populated by one of the parse*() calls
	// * pExpression is (potentially) populated by one of the parse*() calls
	if(pErrorData->pSourceFile != NULL)
	{
		pEntry->pSourceFile     = strdup(pErrorData->pSourceFile);
		pEntry->iSourceFileLine = pErrorData->iSourceFileLine;
	}
	addCountForUser(pEntry, pErrorData->pUserWhoGotIt, 1);
	addCountForIP(pEntry, pErrorData->uIP, 1);
	etAddProductCount(pEntry, pErrorData->pProductName, 1);
	pEntry->bProductionMode = (pErrorData->iProductionMode != 0);
	if (pErrorData->pExecutableName)
	{
		if (pEntry->bProductionMode)
		{
			// Strip path from executable name for production builds
			const char *pNoPathExecutable;
			backSlashes(pErrorData->pExecutableName);
			pNoPathExecutable = errorExecutableGetName(pErrorData->pExecutableName);

			if (*pNoPathExecutable)
			{
				// not an empty string, append "(Production)" to string
				char *pProductionName = NULL;
				estrCopy2(&pProductionName, pNoPathExecutable);
				estrConcatf(&pProductionName, " (Production)");
				addUniqueString(&pEntry->ppExecutableNames, pProductionName);
				eaiPush(&pEntry->ppExecutableCounts, 1);
				estrDestroy(&pProductionName);
			}
		}
		else
		{
			addUniqueString(&pEntry->ppExecutableNames, pErrorData->pExecutableName);
			eaiPush(&pEntry->ppExecutableCounts, 1);
		}
	}
		
	if (pErrorData->pAppGlobalType)
		addUniqueString(&pEntry->ppAppGlobalTypeNames, pErrorData->pAppGlobalType);

	if (pErrorData->lastMemItem)
		pEntry->pLargestMemory = strdup(pErrorData->lastMemItem);

	// -------------------------------------------------------------------------------
	// Counts
	pEntry->iTotalCount = 1;
	addCountForDay(pEntry, pEntry->uFirstTime, 1); // Add one count to "right now"
	addCountForPlatform(pEntry, ePlatform, 1);
	pEntry->iDailyCount = pEntry->iTotalCount;
	pEntry->bIsNewEntry = true;

	// -------------------------------------------------------------------------------
	// Timing
	pEntry->uFirstTime  = timeSecondsSince2000();
	pEntry->uNewestTime = pEntry->uFirstTime;

	if (pErrorData->pSVNBranch)
	{
		char *baselineEnd = strstri(pErrorData->pSVNBranch, "baselines/");
		if (baselineEnd)
		{
			char temp;
			char *truncatedSVNBranch;
			baselineEnd += 10; // length of "baselines/"
			temp = *baselineEnd;
			*baselineEnd = '\0';
			truncatedSVNBranch = strdup(pErrorData->pSVNBranch);
			*baselineEnd = temp;
			free(pErrorData->pSVNBranch);
			pErrorData->pSVNBranch = truncatedSVNBranch;
		}
	}

	{
		NOCONST(BranchTimeLog) * pBranchLog = StructCreateNoConst(parse_BranchTimeLog);
		pBranchLog->branch = pErrorData->pSVNBranch ? strdup(pErrorData->pSVNBranch) : NULL;
		pBranchLog->uFirstTime = pEntry->uFirstTime;
		pBranchLog->uNewestTime = pEntry->uNewestTime;
		eaPush(&pEntry->branchTimeLogs, pBranchLog);
	}

	// -------------------------------------------------------------------------------
	// Versioning
	addUniqueString(&pEntry->ppVersions, pErrorData->pVersionString);
	if (pErrorData->pShardInfoString)
	{
		char *shardInfo = NULL;
		char *startOffset = NULL;

		estrCopy2(&shardInfo, pErrorData->pShardInfoString);
		startOffset = strstri(shardInfo, ", start time (");
		if (startOffset)
		{
			char *endOffset = strchr(startOffset, ')');
			int startIndex, len;
			if (!endOffset)
				endOffset = estrLength(&shardInfo) + shardInfo; // set to EOS
			else
			{
				*endOffset = '\0'; // This will be removed by the estrRemove at the end
				endOffset++; // advance past the ')'
			}
			pEntry->pShardStartString = strdup(startOffset + strlen(", start time ("));
			len = endOffset - startOffset;
			startIndex = startOffset - shardInfo;
			estrRemove(&shardInfo, startIndex, len);
		}
		addUniqueString(&pEntry->ppShardInfoStrings, shardInfo);
		estrDestroy(&shardInfo);
	}
	if (pErrorData->pExecutableName && strstri(pErrorData->pExecutableName, "gimme.exe") != NULL)
	{
		addUniqueString(&pEntry->ppBranches, "svn://code/dev/");
	}
	else if (pErrorData->pSVNBranch)
	{
		addUniqueString(&pEntry->ppBranches, pErrorData->pSVNBranch);
	}
	addUniqueString(&pEntry->ppProductionBuildNames, pErrorData->pProductionBuildName);

	// -------------------------------------------------------------------------------
	// Client counts
	pEntry->iMaxClients = pErrorData->iClientCount;
	pEntry->iTotalClients = pErrorData->iClientCount;

	// -------------------------------------------------------------------------------
	// Trivia strings (one per crash ... might get hefty)
	if (pErrorData->pTriviaList && pErrorData->pTriviaList->triviaDatas)
	{
		NOCONST(TriviaData) *data;
		struct in_addr ina = {0};
		int iDetailIdx = eaIndexedFindUsingString(&pErrorData->pTriviaList->triviaDatas, "details");

		if (iDetailIdx > -1)
		{
			if (!nullStr(pErrorData->pTriviaList->triviaDatas[iDetailIdx]->pVal))
				addUniqueString(&pEntry->eaErrorDetails, pErrorData->pTriviaList->triviaDatas[iDetailIdx]->pVal);
			StructDestroy(parse_TriviaData, pErrorData->pTriviaList->triviaDatas[iDetailIdx]);
			eaRemove((TriviaData***) &pErrorData->pTriviaList->triviaDatas, iDetailIdx);
		}

		eaCopyStructs(&pErrorData->pTriviaList->triviaDatas, &((TriviaData**)pEntry->ppTriviaData), parse_TriviaData);

		if (pErrorData->pUserWhoGotIt)
		{
			data = StructCreateNoConst(parse_TriviaData);
			estrCopy2(&data->pKey, "User");
			estrCopy2(&data->pVal, pErrorData->pUserWhoGotIt);
			eaPush(&pEntry->ppTriviaData, data);
		}

		ina.S_un.S_addr = pErrorData->uIP;
		data = StructCreateNoConst(parse_TriviaData);
		estrCopy2(&data->pKey, "IP");
		estrCopy2(&data->pVal, inet_ntoa(ina));
		eaPush(&pEntry->ppTriviaData, data);
	}
	else if(pErrorData->pTrivia)
	{
		char * pCurString = pErrorData->pTrivia;
		char * pEol = strchr(pCurString, '\n');
		static char *pTempEstring = NULL;

		while (pEol)
		{
			char * pSpace = 0;
			*pEol = '\0'; 
			pSpace = strchr(pCurString, ' ');
			if (pSpace)
			{
				NOCONST(TriviaData) *td = StructCreateNoConst(parse_TriviaData);
				*pSpace = '\0';

				estrCopy2(&td->pKey, pCurString);

				estrClear(&td->pVal);
				estrAppendUnescaped(&td->pVal, pSpace+1);

				eaPush(&pEntry->ppTriviaData, td);
				*pSpace = ' ';
			}
			*pEol = '\n';
			pCurString = pEol + 1;
			pEol = strchr(pCurString, '\n');
		}
	}

	// -------------------------------------------------------------------------------
	// Dump Information
	pEntry->ppDumpData = NULL;
	pEntry->iMiniDumpCount = 0;
	pEntry->iFullDumpCount = 0;
	pEntry->bFullDumpRequested = true; // Temporary? By default, we want one full dump

	if(pErrorData->eType == ERRORDATATYPE_GAMEBUG || ePlatform == PLATFORM_XBOX360)
		pEntry->bFullDumpRequested = false; // Its too slow!

	// -------------------------------------------------------------------------------
	// SVN Blame Information
	pEntry->iCurrentBlameVersion   = 0;
	pEntry->uCurrentBlameTime      = 0;
	pEntry->iRequestedBlameVersion = ETShared_ParseSVNRevision(pErrorData->pVersionString);

	// -------------------------------------------------------------------------------
	// Character Info, Other In-Game debugging info
	if (pErrorData->pUserDataStr)
		estrCopy2(&pEntry->pUserDataStr, pErrorData->pUserDataStr);

	// -------------------------------------------------------------------------------
	// Unique ID generation and final checks

	if (pbExternalSymsrv)
		*pbExternalSymsrv = addToQueue;
	if (!addToQueue || !pbExternalSymsrv)
		recalcUniqueID(pEntry, ET_LATEST_HASH_VERSION); // always create with version 0

	PERFINFO_AUTO_STOP();
	return pEntry;
}

// -----------------------------------------------------------------------
// Merging ErrorEntry's


AUTO_TRANS_HELPER;
void mergeErrorEntry_Part1(ATH_ARG NOCONST(ErrorEntry) *pDst, NON_CONTAINER ErrorEntry *pNew, U32 uTime)
{
	// Note: The order of operations in this file matches the struct order...
	//       keeps these large-struct-manipulating function calls in check.

	// -------------------------------------------------------------------------------
	// Crash Info
	if(pNew->pLargestMemory && !pDst->pLargestMemory)
	{
		pDst->pLargestMemory = strdup(pNew->pLargestMemory);
	}

	// * Update the line numbers in the stack trace
	if (pDst->bReplaceCallstack && ERRORDATATYPE_IS_A_ERROR(pDst->eType) && pNew->ppStackTraceLines)
	{
		pDst->bReplaceCallstack = false;
		eaDestroyStructNoConst(&pDst->ppStackTraceLines, parse_StackTraceLine);
		eaCopyStructs(&pNew->ppStackTraceLines, &((StackTraceLine**)(pDst->ppStackTraceLines)), parse_StackTraceLine);
	}
	else if(eaSize(&pDst->ppStackTraceLines) == eaSize(&pNew->ppStackTraceLines)) // sanity check, should always succeed
	{
		EARRAY_CONST_FOREACH_BEGIN(pDst->ppStackTraceLines, i, s);
		{
			pDst->ppStackTraceLines[i]->iLineNum = pNew->ppStackTraceLines[i]->iLineNum;
		}
		EARRAY_FOREACH_END;
	}

	// * pError should match
	// * pSourceFile should match
	// * iSourceFileLine should match
	if(pDst->bUnlimitedUsers || eaSize(&pDst->ppUserInfo) < MAX_USERS_AND_IPS)
	{
		EARRAY_CONST_FOREACH_BEGIN(pNew->ppUserInfo, i, s);
		{
			addCountForUser(pDst, pNew->ppUserInfo[i]->pUserName, pNew->ppUserInfo[i]->iCount);
		}
		EARRAY_FOREACH_END;
	}
	if(pDst->bUnlimitedUsers || eaSize(&pDst->ppIPCounts) < MAX_USERS_AND_IPS)
	{
		EARRAY_CONST_FOREACH_BEGIN(pNew->ppIPCounts, i, s);
		{
			addCountForIP(pDst, pNew->ppIPCounts[i]->uIP, pNew->ppIPCounts[i]->iCount);
		}
		EARRAY_FOREACH_END;
	}
	EARRAY_CONST_FOREACH_BEGIN(pNew->ppExecutableNames, i, s);
	{
		if (pNew->ppExecutableCounts)
			addCountForExecutable(pDst, pNew->ppExecutableNames[i], pNew->ppExecutableCounts[i]);
		else
			addCountForExecutable(pDst, pNew->ppExecutableNames[i], 1);
	}
	EARRAY_FOREACH_END;
	EARRAY_CONST_FOREACH_BEGIN(pNew->ppAppGlobalTypeNames, i, s);
	{
		addUniqueString(&pDst->ppAppGlobalTypeNames, pNew->ppAppGlobalTypeNames[i]);
	}
	EARRAY_FOREACH_END;
	EARRAY_CONST_FOREACH_BEGIN(pNew->eaProductOccurrences, i, s);
	{
		etAddProductCount(pDst, pNew->eaProductOccurrences[i]->key, pNew->eaProductOccurrences[i]->uCount);
	}
	EARRAY_FOREACH_END;
	if(pNew->bProductionMode) pDst->bProductionMode = true;

	// -------------------------------------------------------------------------------
	// Counts
	pDst->iTotalCount += pNew->iTotalCount;
	EARRAY_CONST_FOREACH_BEGIN(pNew->ppDayCounts, i, s);
	{
		U32 uTimeOfCounts = pNew->uFirstTime + (pNew->ppDayCounts[i]->iDaysSinceFirstTime * 60 * 60 * 24);
		addCountForDay(pDst, uTimeOfCounts, pNew->ppDayCounts[i]->iCount);
	}
	EARRAY_FOREACH_END;
	EARRAY_CONST_FOREACH_BEGIN(pNew->ppPlatformCounts, i, s);
	{
		addCountForPlatform(pDst, pNew->ppPlatformCounts[i]->ePlatform, pNew->ppPlatformCounts[i]->iCount);
	}
	EARRAY_FOREACH_END;

	// -------------------------------------------------------------------------------
	// Timing
	// * uFirstTime should never change ... obviously pDst came first
	pDst->uNewestTime = max(pDst->uNewestTime, pNew->uNewestTime);

	EARRAY_FOREACH_REVERSE_BEGIN(pNew->branchTimeLogs, i);
	{
		int j;
		for (j=eaSize(&pDst->branchTimeLogs)-1; j>=0; j--)
		{
			if (pDst->branchTimeLogs[j]->branch && pNew->branchTimeLogs[i]->branch)
			{ // make sure both are non-NULL
				if (stricmp(pDst->branchTimeLogs[j]->branch, pNew->branchTimeLogs[i]->branch) == 0)
				{
					pDst->branchTimeLogs[j]->uNewestTime = max (pDst->branchTimeLogs[j]->uNewestTime, 
						pNew->branchTimeLogs[i]->uNewestTime);
					break;
				}
			}
			else if (pDst->branchTimeLogs[j]->branch == pNew->branchTimeLogs[i]->branch)
			{ // else check if both "branches" are NULL
				pDst->branchTimeLogs[j]->uNewestTime = max (pDst->branchTimeLogs[j]->uNewestTime, 
					pNew->branchTimeLogs[i]->uNewestTime);
				break;
			}
		}
		if (j<0)
		{
			BranchTimeLog *newLog = StructCreate(parse_BranchTimeLog);
			StructCopyAll(parse_BranchTimeLog, pNew->branchTimeLogs[i], newLog);
			eaPush(&pDst->branchTimeLogs, CONTAINER_NOCONST(BranchTimeLog, newLog));
		}
	}
	EARRAY_FOREACH_END;

	// -------------------------------------------------------------------------------
	// Versioning
	EARRAY_CONST_FOREACH_BEGIN(pNew->ppVersions, i, s);
	{
		addUniqueString(&pDst->ppVersions, pNew->ppVersions[i]);
	}
	EARRAY_FOREACH_END;
	EARRAY_CONST_FOREACH_BEGIN(pNew->ppBranches, i, s);
	{
		addUniqueString(&pDst->ppBranches, pNew->ppBranches[i]);
	}
	EARRAY_FOREACH_END;
	EARRAY_CONST_FOREACH_BEGIN(pNew->ppProductionBuildNames, i, s);
	{
		addUniqueString(&pDst->ppProductionBuildNames, pNew->ppProductionBuildNames[i]);
	}
	EARRAY_FOREACH_END;
	EARRAY_CONST_FOREACH_BEGIN(pNew->ppShardInfoStrings, i, s);
	{
		if (pNew->ppShardInfoStrings[i])
			addUniqueString(&pDst->ppShardInfoStrings, pNew->ppShardInfoStrings[i]);
	}
	EARRAY_FOREACH_END;

	// -------------------------------------------------------------------------------
	// Data file info
	// * pDataFile should match
	// * uDataFileTime should match
	if(pNew->pLastBlamedPerson && pNew->pLastBlamedPerson[0] && !strchr(pNew->pLastBlamedPerson, ' '))
	{
		if(pDst->pLastBlamedPerson)
		{
			free(pDst->pLastBlamedPerson);
		}
		pDst->pLastBlamedPerson = strdup(pNew->pLastBlamedPerson);
	}

	// -------------------------------------------------------------------------------
	// Client counts
	pDst->iMaxClients    = max(pDst->iMaxClients, pNew->iMaxClients);
	pDst->iTotalClients += pNew->iTotalClients;
}

void MoveDumpFiles(ErrorEntry *pOldEntry, DumpData *oldDump, ErrorEntry *pNewEntry, NOCONST(DumpData) *newDump)
{
	char dumpPath[MAX_PATH], newDumpPath[MAX_PATH];
	if (oldDump->bWritten && (oldDump->uFlags & DUMPDATAFLAGS_FULLDUMP))
	{
		calcWriteDumpPath(dumpPath, ARRAY_SIZE_CHECKED(dumpPath), pOldEntry->uID, oldDump->iDumpIndex, true);
		calcWriteDumpPath(newDumpPath, ARRAY_SIZE_CHECKED(newDumpPath), pNewEntry->uID, 
			newDump->iDumpIndex, true);
		mkdirtree(newDumpPath);
		DeleteFile_UTF8(newDumpPath);
		if (fileExists(dumpPath))
			MoveFile_UTF8(dumpPath, newDumpPath);
	}
	else if (oldDump->bWritten)
	{
		calcWriteDumpPath(dumpPath, ARRAY_SIZE_CHECKED(dumpPath), pOldEntry->uID, oldDump->iDumpIndex, false);
		calcWriteDumpPath(newDumpPath, ARRAY_SIZE_CHECKED(newDumpPath), pNewEntry->uID, 
			newDump->iMiniDumpIndex, false);
		mkdirtree(newDumpPath);
		DeleteFile_UTF8(newDumpPath);
		if (fileExists(dumpPath))
			MoveFile_UTF8(dumpPath, newDumpPath);
	}
	if (oldDump->bMiniDumpWritten)
	{
		calcWriteDumpPath(dumpPath, ARRAY_SIZE_CHECKED(dumpPath), pOldEntry->uID, oldDump->iMiniDumpIndex, false);
		calcWriteDumpPath(newDumpPath, ARRAY_SIZE_CHECKED(newDumpPath), pNewEntry->uID, 
			newDump->iMiniDumpIndex, false);
		mkdirtree(newDumpPath);
		DeleteFile_UTF8(newDumpPath);
		if (fileExists(dumpPath))
			MoveFile_UTF8(dumpPath, newDumpPath);
	}
}

AUTO_TRANS_HELPER;
void mergeErrorEntry_Part2(ATH_ARG NOCONST(ErrorEntry) *pDst, NON_CONTAINER ErrorEntry *pNew, U32 uTime)
{
	const char *pNewestDstVersion = NULL;
	const char *pNewestNewVersion = NULL;
	bool bUpdateBlameInfo = false;
	int i;

	// -------------------------------------------------------------------------------
	// Trivia strings - store in overview
	if (((errorTrackerLibGetOptions() & ERRORTRACKER_OPTION_LOG_TRIVIA) == 0) && eaSize(&pNew->ppTriviaData) > 0)
	{
		bool bKeepAllTrivia = 
			(!ERRORDATATYPE_IS_A_ERROR(pDst->eType)                            // Keep all fatal error / crash trivia
			||  pDst->uTriviaCount < 5                                            // Keep the first 5 nonfatal errors' trivia
			||  uTime - ERRORTRACKER_TRIVIA_AGE_CUTOFF > pDst->uLastSavedTrivia); // Keep trivia from resurfacing crashes

		triviaMergeOverview(&pDst->triviaOverview, (TriviaData**) pNew->ppTriviaData, bKeepAllTrivia);
		pDst->uLastSavedTrivia = uTime;
		pDst->uTriviaCount++;
	}

	for (i=0; i<eaSize(&pNew->eaErrorDetails); i++)
	{
		if (pNew->eaErrorDetails[i] && *pNew->eaErrorDetails[i])
		{
			int idx = eaFindString(&pDst->eaErrorDetails, pNew->eaErrorDetails[i]);
			if (idx != -1)
			{
				free(pDst->eaErrorDetails[idx]);
				eaRemove(&pDst->eaErrorDetails, idx);
			}
			else if (eaSize(&pDst->eaErrorDetails) > MAX_ERRORDETAILS)
			{
				free(pDst->eaErrorDetails[0]);
				eaRemove(&pDst->eaErrorDetails, 0);
			}
			eaPush(&pDst->eaErrorDetails, strdup(pNew->eaErrorDetails[i]));
		}
	}

	// -------------------------------------------------------------------------------
	// Dump Information
	// * pNew's ppDumpData will be NULL
	// * pNew's iMiniDumpCount will be zero
	// * pNew's iFullDumpCount will be zero
	// * pNew's bFullDumpRequested will be false

	// TODO move over dumps and memory dumps for manual merging
	for (i=0; i<eaSize(&pNew->ppDumpData); i++)
	{
		NOCONST(DumpData) *pDumpData = StructCloneDeConst(parse_DumpData, pNew->ppDumpData[i]);
		pDumpData->iDumpIndex = pDumpData->iMiniDumpIndex = eaSize(&pDst->ppDumpData);
		MoveDumpFiles(pNew, pNew->ppDumpData[i], CONTAINER_RECONST(ErrorEntry, pDst), pDumpData);
		eaPush(&pDst->ppDumpData, pDumpData);

		if (pDumpData->bWritten && (pDumpData->uFlags & DUMPDATAFLAGS_FULLDUMP))
			pDst->iFullDumpCount++;
		else if (pDumpData->bWritten)
			pDst->iMiniDumpCount++;
	}
	for (i=0; i<eaSize(&pNew->ppMemoryDumps); i++)
	{
		NOCONST(MemoryDumpData) *pDumpData = StructCloneDeConst(parse_MemoryDumpData, pNew->ppMemoryDumps[i]);
		char dumpPath[MAX_PATH], newDumpPath[MAX_PATH];
		pDumpData->iDumpIndex = eaSize(&pDst->ppMemoryDumps);
		calcMemoryDumpPath(dumpPath, ARRAY_SIZE_CHECKED(dumpPath), pNew->uID, i);
		calcMemoryDumpPath(newDumpPath, ARRAY_SIZE_CHECKED(newDumpPath), pDst->uID, pDumpData->iDumpIndex);

		mkdirtree(newDumpPath);
		DeleteFile_UTF8(newDumpPath);
		if (fileExists(dumpPath) && MoveFile_UTF8(dumpPath, newDumpPath))
		{
			eaPush(&pDst->ppMemoryDumps, pDumpData);
			pDst->iMemoryDumpCount++;
		}
		
	}

	// -------------------------------------------------------------------------------
	// SVN Blame Information
	// We want to update the blame info if its been MAX_BLAME_AGE seconds since the 
	// last query, or if the current blame information is from an older version.

	// Find the latest versions
	pNewestDstVersion = findNewestVersion(&pDst->ppVersions);
	pNewestNewVersion = findNewestVersion(&pNew->ppVersions);

	bUpdateBlameInfo = false;

	// If there is no new version coming in, don't bother to query anything.
	if(pNewestNewVersion != NULL)
	{
		if(pDst->uNewestTime > (pDst->uCurrentBlameTime + MAX_BLAME_AGE))
			bUpdateBlameInfo = true;
		else
		{
			if(pNewestDstVersion != NULL)
			{
				if(CompareUsefulVersionStrings(pNewestDstVersion, pNewestNewVersion) < 0)
				{
					// pNewestDstVersion is older ... flag for query
					bUpdateBlameInfo = true;
				}
			}
		}
		if(bUpdateBlameInfo)
			pDst->iRequestedBlameVersion = atoi(pNewestNewVersion);
	}
}

const char *getVersionPatchProject(const char *version)
{
	char twoLetterCode[3];
	if (!version[0] || !version[1])
		return NULL;	
	twoLetterCode[0] = toupper(version[0]);
	twoLetterCode[1] = toupper(version[1]);
	twoLetterCode[2] = 0;

	if (strcmp(twoLetterCode, "FC") == 0) return "Fightclub";
	if (strcmp(twoLetterCode, "ST") == 0) return "Startrek";
	if (strcmp(twoLetterCode, "NW") == 0) return "Night";
	if (strcmp(twoLetterCode, "AS") == 0 || // Account Server
		strcmp(twoLetterCode, "GC") == 0 || // Global Chat
		strcmp(twoLetterCode, "TT") == 0 || // Ticket Tracker
		strcmp(twoLetterCode, "ET") == 0 || // Error Tracker
		strcmp(twoLetterCode, "PS") == 0) // Patch Server
		return "Infrastructure";
	if (strcmp(twoLetterCode, "BA") == 0) return "Bronze";
	return NULL;
}

// Project names should never be longer than this
#define SVN_PROJECT_MAX_LEN 32
// Only works for full SVN paths
const char *getSVNBranchProject(const char *branch)
{
	static char sProjectBuffer[SVN_PROJECT_MAX_LEN + 1];
	const char *svnProjectStart = strstri(branch, "code/svn/");
	char *svnProjectEnd;
	if (!svnProjectStart)
		return NULL;
	svnProjectStart += 9; // strlen("code/svn/")
	while (*svnProjectStart == '/' || *svnProjectStart == '\\')
		svnProjectStart++;
	if (*svnProjectStart == 0)
		return NULL;
	svnProjectEnd = strchr(svnProjectStart, '/');
	if (svnProjectEnd)
	{
		if (svnProjectEnd - svnProjectStart > SVN_PROJECT_MAX_LEN)
			return NULL;
		strncpy(sProjectBuffer, svnProjectStart, svnProjectEnd - svnProjectStart);
	}
	else if (strlen(svnProjectStart) > SVN_PROJECT_MAX_LEN)
		return NULL;
	else
		strcpy(sProjectBuffer, svnProjectStart);
	return sProjectBuffer;
}

char *getSimplifiedVersionString(char **estr, const char *version)
{
	char *truncate = NULL;
	char *patchName = NULL;
	if (!version)
		return NULL;
	if (getVersionPatchProject(version))
	{
		estrStackCreate(&patchName);
		estrCopy2(&patchName, version);
		truncate = strchr(patchName, ' ');
		if (truncate)
		{
			*truncate = 0;
			estrCopy2(estr, patchName);
		}
		else
			estrCopy2(estr, version);
		estrDestroy(&patchName);
	}
	else
		estrCopy2(estr, version);
	return *estr;
}

#include "AutoGen/ETShared_h_ast.c"