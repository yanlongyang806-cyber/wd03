#include "ETCommon/ETDumps.h"
#include "ETCommon/ETCommonStructs.h"
#include "ETCommon/ETShared.h"

#include "estring.h"
#include "earray.h"
#include "fileutil2.h"
#include "winutil.h"
#include "StashTable.h"
#include "timing.h"
#include "utils.h"

extern ErrorTrackerSettings gErrorTrackerSettings;
StashTable dumpIDToDumpDataTable;

U32 getNewDumpID(DumpData *pData)
{
	static U32 nextDumpID = 0;
	nextDumpID++;
	stashIntAddPointer(dumpIDToDumpDataTable, nextDumpID, pData, true);
	return nextDumpID;
}
DumpData * findDumpData(int id)
{
	DumpData *p = NULL;
	if(stashIntFindPointer(dumpIDToDumpDataTable, id, &p))
		return p;
	return NULL;
}

int countReceivedDumps(U32 id, U32 dumpflags)
{
	int count = 0;
	ErrorEntry *pEntry = findErrorTrackerByID(id);
	if(pEntry)
	{
		if(dumpflags & DUMPFLAGS_MINIDUMP)
			count += pEntry->iMiniDumpCount;
		if(dumpflags & DUMPFLAGS_FULLDUMP)
			count += pEntry->iFullDumpCount;
	}

	return count;
}

AUTO_COMMAND ACMD_CATEGORY(ET_Debug);
int getIncomingDumpCount(U32 id)
{
	ErrorEntry *pEntry = findErrorTrackerByID(id);
	if(pEntry)
		return pEntry->iCurrentDumpReceiveCount;
	return 0;
}

void addIncomingDumpCount(U32 id)
{
	ErrorEntry *pEntry = findErrorTrackerByID(id);
	if(pEntry)
		pEntry->iCurrentDumpReceiveCount++;
}

void removeIncomingDumpCount(U32 id)
{
	ErrorEntry *pEntry = findErrorTrackerByID(id);
	if(pEntry)
	{
		pEntry->iCurrentDumpReceiveCount--;
		if(pEntry->iCurrentDumpReceiveCount < 0)
			pEntry->iCurrentDumpReceiveCount = 0;
	}
}

// Removes all directories referring to an ID that doesn't exist anymore
// uMaxAge is an optional parameter - 0 = only prune if it doesn't exist
//   non-zero means to prune data dirs for entries older than [uMaxAge] seconds
//   Older = when the error was last seen
static int scanETIDDataDir(const char *dumpDir, U32 uMaxAge)
{
	char *tempDir = NULL;
	char *tempFile = NULL;
	char **folders = NULL;
	int numKept = 0;
	int numDeleted = 0;
	int count;
	U32 uCurrentTime = timeSecondsSince2000();

	estrCopy2(&tempDir, dumpDir);
	estrReplaceOccurrences(&tempDir, "\\", "/");
	printf("Scanning ETID Data Dir for worthless data [%s]\n", tempDir);
	folders = fileScanDirFolders(tempDir, FSF_FOLDERS);
	count = eaSize(&folders);

	EARRAY_FOREACH_BEGIN(folders, i);
	{
		int id = parseErrorEntryDir(folders[i]);
		if(id)
		{
			ErrorEntry *pEntry = findErrorTrackerByID(id);
			bool bPurge = false;

			if (pEntry)
			{
				if (uMaxAge && (uCurrentTime - pEntry->uNewestTime > uMaxAge))
					bPurge = true;
				else
					numKept++;
			}
			else
				bPurge = true;
			if (bPurge)
			{
				int ret = 0;
				char cmdBuffer[1024];
				estrCopy2(&tempFile, folders[i]);
				estrReplaceOccurrences(&tempFile, "/", "\\");
				sprintf(cmdBuffer, "rmdir /s /q \"%s\"", tempFile);
				ret = system(cmdBuffer);

				numDeleted++;
			}
		}

		if(((i+1) % 50) == 0)
		{
			printf("Pruning Data [%s] [%d/%d] [Kept:%d] [Deleted:%d]\n", 
				tempDir, i+1, count, numKept, numDeleted);
		}
	}
	EARRAY_FOREACH_END;

	printf("Pruning Data [%s] Dir Complete [%d/%d dirs deleted]\n", tempDir, numDeleted, count);
	fileScanDirFreeNames(folders);
	estrDestroy(&tempDir);
	estrDestroy(&tempFile);

	return numDeleted;
}

void rawDataPrune(int iMaxAge)
{
	int delCount = 0;
	delCount += scanETIDDataDir(gErrorTrackerSettings.pRawDataDir, iMaxAge);
	printf("Pruning Raw Data: Work Complete (Zug zug!) [%d dirs deleted]\n", delCount);
}
void dumpPrune(int iMaxAge)
{
	int delCount = 0;
	delCount += scanETIDDataDir(gErrorTrackerSettings.pDumpDir, iMaxAge);
	EARRAY_FOREACH_BEGIN(gErrorTrackerSettings.ppAlternateDumpDir, i);
	{
		delCount += scanETIDDataDir(gErrorTrackerSettings.ppAlternateDumpDir[i], iMaxAge);
	}
	EARRAY_FOREACH_END;
	printf("Pruning Dumps: Work Complete (Zug zug!) [%d dirs deleted]\n", delCount);
}

static DWORD WINAPI rawDataPruneThread(int *piMaxAge)
{
	EXCEPTION_HANDLER_BEGIN
	rawDataPrune(piMaxAge ? *piMaxAge : 0);
	if (piMaxAge) free(piMaxAge);
	EXCEPTION_HANDLER_END
	return 0;
}

static DWORD WINAPI dumpPruneThread(int *piMaxAge)
{
	EXCEPTION_HANDLER_BEGIN
	dumpPrune(piMaxAge ? *piMaxAge : 0);
	if (piMaxAge) free(piMaxAge);
	EXCEPTION_HANDLER_END
	return 0;
}

static DWORD WINAPI allDataPruneThread(int *piMaxAge)
{
	int iMaxAge = piMaxAge ? *piMaxAge : 0;
	EXCEPTION_HANDLER_BEGIN
	dumpPrune(iMaxAge);
	rawDataPrune(iMaxAge);
	if (piMaxAge) free(piMaxAge);
	EXCEPTION_HANDLER_END
	return 0;
}

AUTO_COMMAND ACMD_CATEGORY(ErrorTracker);
void pruneInactiveRawDataDirs(void)
{
	DWORD ignored = 0;
	CloseHandle((HANDLE) _beginthreadex(NULL, 0, rawDataPruneThread, NULL, 0, &ignored));
}

AUTO_COMMAND ACMD_CATEGORY(ErrorTracker);
void pruneInactiveDumpDirs(void)
{
	DWORD ignored = 0;
	CloseHandle((HANDLE) _beginthreadex(NULL, 0, dumpPruneThread, NULL, 0, &ignored));
}

AUTO_COMMAND ACMD_CATEGORY(ErrorTracker);
void pruneAllInactiveDataDirs(void)
{
	DWORD ignored = 0;
	CloseHandle((HANDLE) _beginthreadex(NULL, 0, allDataPruneThread, NULL, 0, &ignored));
}

// Also prunes inactive ones
AUTO_COMMAND ACMD_CATEGORY(ErrorTracker);
void pruneAllInactiveDataDirsOlderThan(int iDays)
{
	DWORD ignored = 0;
	int *piDays = malloc(sizeof(int));
	*piDays = iDays * 24*60*60; // convert into seconds
	CloseHandle((HANDLE) _beginthreadex(NULL, 0, allDataPruneThread, piDays, 0, &ignored));
}
