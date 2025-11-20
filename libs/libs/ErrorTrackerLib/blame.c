#include "blame.h"
#include "AutoGen/blame_c_ast.h"

#include "file.h"
#include "fileutil.h"
#include "timing.h"
#include "timing_profiler_interface.h"
#include "winutil.h"
#include "earray.h"
#include "estring.h"
#include "ErrorTracker.h"
#include "ErrorTrackerLib.h"
#include "ErrorTrackerLibPrivate.h"
#include "email.h"
#include "ThreadManager.h"
#include "objContainer.h"
#include "objTransactions.h"
#include "MultiWorkerThread.h"
#include "ThreadSafeMemoryPool.h"

#include "ETCommon/ETCommonStructs.h"
#include "ETCommon/ETShared.h"
#include "AutoGen/ETCommonStructs_h_ast.h"
#include "AutoGen/ErrorTrackerLib_autotransactions_autogen_wrappers.h"

#include "UTF8.h"

static __forceinline UINT GetTempFileName_Static(const char *pPathName, const char *pPrefixStirng,
	UINT uUnique, char *pOutBuf, int iOutBufSize)
{
	char *pTemp = NULL;
	UINT iRetVal;
	estrStackCreate(&pTemp);
	iRetVal = GetTempFileName_UTF8(pPathName, pPrefixStirng, uUnique, &pTemp);
	if (pTemp)
	{
		strcpy_s(pOutBuf, iOutBufSize, pTemp);
	}
	estrDestroy(&pTemp);
	return iRetVal;
}

// Nice to disable during debugging
//#define DISABLE_BLAME_CACHING

// Max number of worker threads allowed to be running that are asking SVN for blame info
#define MAX_ACTIVE_BLAMES (10)

//#define INTERNAL_SVN

AUTO_STRUCT;
typedef struct BlameCache
{
	U32 uID; AST(KEY)
	U32 uEntryID;

	int iCurrent;
	int iTotal;
	bool bComplete;
	bool bPersistent; // Don't remove, as the web interface or someone else cares about it (Deprecated)

	// Updated Data
	StackTraceLine **ppStackTrace; // This really shouldn't be consted data, but AUTO_STRUCT doesn't like NOCONST
	int iBlameVersion;
} BlameCache;

TSMP_DEFINE(BlameCache);

AUTO_STRUCT;
typedef struct BlameStackOptimized
{
	char *pFilename; AST(KEY ESTRING)
	StackTraceLine **ppLines; AST(UNOWNED)
	int iLastIndex;
} BlameStackOptimized;

bool errorTracker_FindSVNBranch (char **estr, ErrorEntry *pEntry);
bool errorTracker_CreateBlamePath(char **blamePath, const char *filename, const char *branch);
void errorTracker_BlameStackLine (const char *branch, BlameStackOptimized *blame, int iRevision);

AUTO_FIXUPFUNC;
TextParserResult fixupBlameStackOptimized(BlameStackOptimized *p, enumTextParserFixupType eFixupType, void *pExtraData)
{
	if (eFixupType == FIXUPTYPE_DESTRUCTOR)
	{
		eaDestroy(&p->ppLines);
	}
	return PARSERESULT_SUCCESS;
}


static int StackTraceLineCmp(const StackTraceLine **pptr1, const StackTraceLine **pptr2)
{
	if ((*pptr1)->iLineNum < (*pptr2)->iLineNum)
		return -1;
	if ((*pptr1)->iLineNum ==(*pptr2)->iLineNum)
		return 0;
	return 1;
}

static char sBlameRoot[MAX_PATH] = "c:\\src";
AUTO_CMD_STRING(sBlameRoot,blameRoot) ACMD_CMDLINE;

static int blameThreadQueueSize = 16384;
AUTO_CMD_INT(blameThreadQueueSize, BlameThreadQueueSize);

void updateBlameInfo(SA_PARAM_NN_VALID BlameCache *blameCache, int *pOutputCurrentIndex);

// SVN Queries
U32 GetHeadRevision(void);

static int siBlameCacheActiveCount = 0;

void blameCacheWait(void)
{
	printf("Waiting for blames to complete...\n");
	while(siBlameCacheActiveCount != 0)
	{
		Sleep(100);
		BlameCache_OncePerFrame();
	}
}

#define CHECKEOF if (pReadHead - pFileBuf >= iFileSize)		\
{												\
	free(pFileBuf);								\
	return "[unknown]";							\
}

// This is called every once in a while, and quits early (between iterations) if 
// the function has taken longer than MAX_INCREMENTAL_BLAME_UPDATE_MS to complete.
// This ensures that we don't lose out on various crashes because we are performing
// tons of SVN blames.
AUTO_COMMAND;
void updateRequestedBlameInfo(void)
{
#ifndef DISABLE_BLAME_CACHING
	ContainerIterator iter = {0};
	Container *currCon = NULL;

	UINT startTime = GetTickCount();

	int sawCount = 0;
	int startedCount = 0;

	objInitContainerIteratorFromType(errorTrackerLibGetCurrentType(), &iter);
	currCon = objGetNextContainerFromIterator(&iter);
	while (currCon)
	{
		ErrorEntry *pEntry = CONTAINER_ENTRY(currCon);
		if(pEntry->iRequestedBlameVersion != 0)
		{
			sawCount++;
			if(siBlameCacheActiveCount < blameThreadQueueSize)
			{
				startedCount++;
				startBlameCache(pEntry);
			}
		}
		currCon = objGetNextContainerFromIterator(&iter);
	}
	objClearContainerIterator(&iter);

	printf("Blame Caching: [Queued: %d] [Active: %d] [Just Started: %d] [UpdateMS: %d]\n", 
		sawCount - siBlameCacheActiveCount, 
		siBlameCacheActiveCount, 
		startedCount,
		GetTickCount() - startTime);
#endif
}

// ----------------------------------------------------------------------------------------------------
// SVN querying code, taken entirely from Alex's BugCounter system

const char *GetUserNameFromBlame(char *pFileName, int iLineNum, int iInRevisionNum, int *pOutRevisionNum)
{
	char systemString[1024];
	char *pFileBuf;
	int iFileSize;
	int iLineCount;
	char *pReadHead;
	static char nameString[256];
	char *pWriteHead = nameString;

	char tempFileName[MAX_PATH];

	*pOutRevisionNum = 0;
	nameString[0] = 0;

	sprintf(tempFileName, "%s/foo.txt", gErrorTrackerSettings.pDumpTempDir);
	mkdirtree_const(tempFileName);

	GetTempFileName_Static(gErrorTrackerSettings.pDumpTempDir, "BUG", 0, SAFESTR(tempFileName));

	backSlashes(tempFileName);

	if (iInRevisionNum > 0)
		sprintf(systemString, "svn blame %s -r %d > %s", pFileName, iInRevisionNum, tempFileName);
	else
		sprintf(systemString, "svn blame %s > %s", pFileName, tempFileName);

	system(systemString);

	pFileBuf = fileAlloc(tempFileName, &iFileSize);

	sprintf(systemString, "del %s", tempFileName);
	
	system(systemString);

	if (!pFileBuf)
	{
		return "[unknown]";
	}

	pReadHead = pFileBuf;
	iLineCount = 1;

	while (iLineCount < iLineNum)
	{
		while (*pReadHead != '\n')
		{
			pReadHead++;
			CHECKEOF
		}

		pReadHead++;
		iLineCount++;
	}

	//skip over whitespace before rev num
	while (!isdigit(*pReadHead))
	{
		CHECKEOF
			pReadHead++;
	}

	//skip over rev num

	while (isdigit(*pReadHead))
	{
		*pOutRevisionNum *= 10;
		*pOutRevisionNum += (*pReadHead - '0');

		CHECKEOF
			pReadHead++;
	}

	//skip over whitespace after rev num
	while (!isalpha(*pReadHead))
	{
		CHECKEOF
			pReadHead++;
	}

	//copy name
	while (isalpha(*pReadHead))
	{
		CHECKEOF
			*pWriteHead = *pReadHead;
		pReadHead++;
		pWriteHead++;

		if (pWriteHead - nameString > sizeof(nameString) - 2)
		{
			break;
		}
	}

	*pWriteHead = 0;

	free(pFileBuf);

	return nameString;
}

U32 GetTimeOfRevisionNum(int iRevisionNum)
{
	char systemString[1024];
	char tempFileName[MAX_PATH];
	char *pFileBuf;
	int iFileSize;
	char *pTemp;
	U32 uTimeOfRev;

	GetTempFileName_Static(gErrorTrackerSettings.pDumpTempDir, "BUG", 0, SAFESTR(tempFileName));

	backSlashes(tempFileName);

	sprintf(systemString, "svn info -r%d %s > %s", iRevisionNum, gErrorTrackerSettings.pSVNRoot, tempFileName);

	system(systemString);

	pFileBuf = fileAlloc(tempFileName, &iFileSize);

	if (!pFileBuf)
	{
		return -1;
	}

	sprintf(systemString, "del %s", tempFileName);
	system(systemString);

	pTemp = strstr(pFileBuf, "Last Changed Date: ");
	if (!pTemp)
	{
		free(pFileBuf);
		return -1;
	}

	pTemp += strlen("Last Changed Date: ");

	uTimeOfRev = timeGetSecondsSince2000FromDateString(pTemp);

	free(pFileBuf);

	if (!uTimeOfRev)
	{
		return -1;
	}

	return uTimeOfRev;
}

U32 GetHeadRevision(void)
{
	char systemString[1024];
	char tempFileName[MAX_PATH];
	char *pFileBuf;
	int iFileSize;
	char *pTemp;
	U32 iHeadRev;
		
	GetTempFileName_Static(gErrorTrackerSettings.pDumpTempDir, "BUG", 0, SAFESTR(tempFileName));

	backSlashes(tempFileName);

	sprintf(systemString, "svn info -r HEAD %s > %s", gErrorTrackerSettings.pSVNRoot, tempFileName);

	system(systemString);

	pFileBuf = fileAlloc(tempFileName, &iFileSize);
	
	if (!pFileBuf)
	{
		return -1;
	}

	sprintf(systemString, "del %s", tempFileName);
	system(systemString);

	pTemp = strstr(pFileBuf, "Revision: ");
	if (!pTemp)
	{
		free(pFileBuf);
		return -1;
	}

	pTemp += strlen("Revision: ");
	iHeadRev = atoi(pTemp);

	free (pFileBuf);

	return iHeadRev;
}

// This performs the actual SVN update
void updateBlameInfo(BlameCache *blameCache, int *pOutputCurrentIndex)
{
	int i;
	ErrorEntry *pEntry = findErrorTrackerByID(blameCache->uEntryID);
	int iRequestedBlameVersion;
	EARRAY_OF(BlameStackOptimized) eaBlames = NULL;
	if (!pEntry)
		return;

	PERFINFO_AUTO_START_FUNC();

	iRequestedBlameVersion = pEntry->iRequestedBlameVersion;
	eaClear(&blameCache->ppStackTrace);

	//print("[%x]...", pEntry->iUniqueID);

	// TODO fix the iRequestedBlameVersion to properly parse extended prod build version strings

	if (!ERRORDATATYPE_IS_A_ERROR(pEntry->eType))
	{
		//int indexFlags = 0;
		//bool bSendBlameEmails = pEntry->uID && (RunningFromToolsBin(pEntry) || pEntry->bProductionMode);
		char *svnBlameBranch = NULL;
		int iBlames = eaSize(&pEntry->ppStackTraceLines);

		eaCopyStructs(&pEntry->ppStackTraceLines, &blameCache->ppStackTrace, parse_StackTraceLine);

		if (!errorTracker_FindSVNBranch(&svnBlameBranch, pEntry))
		{
			PERFINFO_AUTO_STOP_FUNC();
			return; // TODO error handling?
		}

#ifdef INTERNAL_SVN
		
		if(iRequestedBlameVersion == 0)
			iRequestedBlameVersion = SVNGetRevisionForPath(svnBlameBranch);

		eaIndexedEnable(&eaBlames, parse_BlameStackOptimized);
		for (i=0; i<iBlames; i++)
		{
			StackTraceLine *pLine = blameCache->ppStackTrace[i];
			if (pLine->pFilename)
			{
				BlameStackOptimized *pBlame = eaIndexedGetUsingString(&eaBlames, pLine->pFilename);
				if (!pBlame)
				{
					pBlame = StructCreate(parse_BlameStackOptimized);
					estrCopy2(&pBlame->pFilename , pLine->pFilename);
					eaIndexedAdd(&eaBlames, pBlame);
				}
				eaPush(&pBlame->ppLines, pLine);
			}
		}
		iBlames = eaSize(&eaBlames);
		for (i=0; i<iBlames; i++)
			eaQSort(eaBlames[i]->ppLines, StackTraceLineCmp);

		for (i=0; i < iBlames; i++)
		{		
			if(pOutputCurrentIndex)
			{
				*pOutputCurrentIndex = i;
			}
			errorTracker_BlameStackLine(svnBlameBranch, eaBlames[i], iRequestedBlameVersion);

			/*if (bSendBlameEmails && i < MAX_SVN_BLAME_DEPTH && (!pEntry->ppStackTraceLines[i]->pBlamedPerson || 
			stricmp(pEntry->ppStackTraceLines[i]->pBlamedPerson, pUserName) ) )
			{
			bool addUser = true;
			int j;
			for (j=0; j<i; j++)
			{
			// Already blaming SVN-blamed user for this file/line?
			if (indexFlags & (1 << j) && !stricmp(pEntry->ppStackTraceLines[j]->pBlamedPerson, pUserName))
			{
			addUser = false;
			break;
			}
			}
			if (addUser)
			indexFlags |= 1 << i;
			}*/
		}
		//sendEmailsOnNewBlame(pEntry, indexFlags); // incomplete
		eaDestroyStruct(&eaBlames, parse_BlameStackOptimized);
		estrDestroy(&svnBlameBranch);
#else
		if(iRequestedBlameVersion == 0)
			iRequestedBlameVersion = GetHeadRevision();

		for (i=0; i < eaSize(&pEntry->ppStackTraceLines); i++)
		{
			int iBlameRevisionForThisFile = 0;
			const char *pUserName = NULL;
			char *pBlamePath = NULL;
			StackTraceLine *stackline = pEntry->ppStackTraceLines[i];

			estrStackCreate(&pBlamePath);

			if(pOutputCurrentIndex)
			{
				*pOutputCurrentIndex = i;
			}
			
			if (stackline->pFilename && strstri(stackline->pFilename, "autogen") != NULL)
				continue;
			if (stackline->pModuleName && strstri(stackline->pModuleName, ".dll") != NULL)
				continue;
			if(!errorTracker_CreateBlamePath(&pBlamePath, stackline->pFilename, svnBlameBranch))
				continue;

			pUserName = GetUserNameFromBlame(
				pBlamePath,
				pEntry->ppStackTraceLines[i]->iLineNum,
				iRequestedBlameVersion,
				&iBlameRevisionForThisFile);

			if (!pUserName || strlen(pUserName) == 0)
				continue;

			/*if (bSendBlameEmails && i < MAX_SVN_BLAME_DEPTH && (!pEntry->ppStackTraceLines[i]->pBlamedPerson || 
					stricmp(pEntry->ppStackTraceLines[i]->pBlamedPerson, pUserName) ) )
			{
				bool addUser = true;
				int j;
				for (j=0; j<i; j++)
				{
					// Already blaming SVN-blamed user for this file/line?
					if (indexFlags & (1 << j) && !stricmp(pEntry->ppStackTraceLines[j]->pBlamedPerson, pUserName))
					{
						addUser = false;
						break;
					}
				}
				if (addUser)
					indexFlags |= 1 << i;
			}*/
			estrCopy2(&((NOCONST(StackTraceLine)*) blameCache->ppStackTrace[i])->pBlamedPerson, pUserName);
			((NOCONST(StackTraceLine)*) blameCache->ppStackTrace[i])->iBlamedRevision = iBlameRevisionForThisFile;
			((NOCONST(StackTraceLine)*) blameCache->ppStackTrace[i])->uBlamedRevisionTime = GetTimeOfRevisionNum(iBlameRevisionForThisFile);
		}

#endif
	}

	blameCache->iBlameVersion = iRequestedBlameVersion;
	PERFINFO_AUTO_STOP_FUNC();
}

// -----------------------------------------------------------------------------------------
// Blame Cache

// Didn't bother with locks here, as the main thread only ever reads from a BlameCache entry,
// and only manipulates / deletes it after bComplete is set (which is after the first thread 
// is done).

AUTO_TRANSACTION ATR_LOCKS(pEntry, ".ppStackTraceLines, .iCurrentBlameVersion, .iRequestedBlameVersion, .uCurrentBlameTime");
enumTransactionOutcome trErrorEntry_UpdateBlameData(ATR_ARGS, NOCONST(ErrorEntry) *pEntry, 
													int iRevision, NON_CONTAINER StackTraceLineList *pStackTrace)
{
	int i,size;
	size = eaSize(&pStackTrace->ppStackTraceLines);
	for (i=0; i<eaSize(&pEntry->ppStackTraceLines) && i < size; i++)
	{
		estrCopy2(&pEntry->ppStackTraceLines[i]->pBlamedPerson, pStackTrace->ppStackTraceLines[i]->pBlamedPerson);
		pEntry->ppStackTraceLines[i]->iBlamedRevision = pStackTrace->ppStackTraceLines[i]->iBlamedRevision;
		pEntry->ppStackTraceLines[i]->uBlamedRevisionTime = pStackTrace->ppStackTraceLines[i]->uBlamedRevisionTime;
	}

	pEntry->iCurrentBlameVersion = iRevision;
	pEntry->iRequestedBlameVersion = 0;
	pEntry->uCurrentBlameTime = timeSecondsSince2000();
	return TRANSACTION_OUTCOME_SUCCESS;
}

static BlameCache **sppCacheList = NULL;
static MultiWorkerThreadManager *sBlameMultiThread = NULL;

void BlameCache_OncePerFrame(void)
{
	int i,size;
	size = eaSize(&sppCacheList);
	for (i=size-1; i>=0; i--)
	{
		BlameCache *cache = sppCacheList[i];
		if (cache->bComplete)
		{
			ErrorEntry *pEntry = findErrorTrackerByID(cache->uEntryID);
			if (pEntry)
			{
				StackTraceLineList stack = {0};
				stack.ppStackTraceLines = cache->ppStackTrace;
				AutoTrans_trErrorEntry_UpdateBlameData(NULL, GLOBALTYPE_ERRORTRACKER, errorTrackerLibGetCurrentType(), 
					pEntry->uID, cache->iBlameVersion, &stack);
				pEntry->bBlameDataLocked = false;
				//printf("Blame Cache: Unlocked ID# %d ...\n", pEntry->uID);
				eaDestroyStruct(&cache->ppStackTrace, parse_StackTraceLine);
			}
			StructDestroy(parse_BlameCache, sppCacheList[i]);
			eaRemove(&sppCacheList, i);
			siBlameCacheActiveCount--;
		}
	}
}

static bool sbDisableBlameCache = false;
AUTO_CMD_INT(sbDisableBlameCache, DisableBlame);
void BlameCacheDisable(void) // Stops the Blame Cache thread; Used on shutdown // TODO incomplete
{
	sbDisableBlameCache = true;
}

DWORD WINAPI BlameCacheThread(LPVOID lpParam)
{
	BlameCache *p = (BlameCache *)lpParam;
	EXCEPTION_HANDLER_BEGIN
	autoTimerThreadFrameBegin(__FUNCTION__);

	updateBlameInfo(p, &p->iCurrent);
	p->bComplete = true;

	// Unlocking and other cleanup is done when data is transacted in the main thread
	autoTimerThreadFrameEnd();
	EXCEPTION_HANDLER_END
	return 0;
}

void mwt_BlameThreadAction(BlameCache *blame)
{
	updateBlameInfo(blame, &blame->iCurrent);
	blame->bComplete = true;
}

U32 startBlameCache(ErrorEntry *pEntry)
{
	static int nextID = 0;
	BlameCache *pCache;

	if (pEntry->bBlameDataLocked || sbDisableBlameCache)
	{
		return 0;
	}
	pEntry->bBlameDataLocked = true;
		
	pCache = StructCreate(parse_BlameCache);

	nextID++;
	pCache->uID = nextID;
	pCache->uEntryID = pEntry->uID;
	pCache->iCurrent = 0;
	pCache->iTotal = eaSize(&pEntry->ppStackTraceLines);
	pCache->bComplete = false;
	siBlameCacheActiveCount++;

	eaIndexedAdd(&sppCacheList, pCache);
	
	mwtQueueInput(sBlameMultiThread, pCache, true);
	return nextID;
}

bool getBlameCacheProgress(U32 id, U32 *current, U32 *total, bool *complete)
{
	BlameCache *cache = eaIndexedGetUsingInt(&sppCacheList, id);
	if (cache)
	{
		if(current)  *current  = cache->iCurrent;
		if(total)    *total    = cache->iTotal;
		if(complete) *complete = cache->bComplete;
		return true;
	}
	return false;
}

ErrorEntry *getBlameCache(U32 id)
{
	BlameCache *cache = eaIndexedGetUsingInt(&sppCacheList, id);
	if (cache)
	{
		ErrorEntry *pEntry;
		if(!cache->bComplete || cache->ppStackTrace)
			return NULL;
		pEntry = findErrorTrackerByID(cache->uEntryID);
		return pEntry;
	}
	return NULL;
}

AUTO_RUN;
void BlameCache_Init(void)
{
	eaIndexedEnable(&sppCacheList, parse_BlameCache);
	sBlameMultiThread = mwtCreate(blameThreadQueueSize, 0, 8, NULL, NULL, mwt_BlameThreadAction, NULL, "BlameCacheMultiThread");
	TSMP_CREATE(BlameCache, blameThreadQueueSize);
	ParserSetTPIUsesThreadSafeMemPool(parse_BlameCache, &TSMP_NAME(BlameCache));
}

#include "svn_client.h"
#include "svn_pools.h"
#include "svn_config.h"
#include "svn_error.h"
#include "svn_auth.h"
#include "svn_path.h"

#include <apr.h>
#include <apr_pools.h>

#define INT_ERR(expr)                             \
	{                                             \
		svn_error_t *__temperr = (expr);          \
		if (__temperr)                            \
		{                                         \
			svn_error_clear(__temperr);           \
			return;                               \
		}                                         \
	}

typedef enum SVNRootTypeEnum 
{
	SVNROOT_TYPE_SVN = 0,
	SVNROOT_TYPE_WEBDAV,
	SVNROOT_TYPE_LOCAL,
	SVNROOT_TYPE_OTHER
} SVNRootTypeEnum ;

#define SVNROOT_PATH_SVN "svn://code"
#define SVNROOT_PATH_WEBDAV "http://code/svn"

static char svnUsername[64] = "errortracker";
static char svnPassword[64] = "";
static char svnRoot[64] = SVNROOT_PATH_SVN;
static SVNRootTypeEnum seSVNType = SVNROOT_TYPE_SVN;


// Calling these after initSVN has been run probably won't work
void setSVNUsername(const char *username)
{
	strcpy(svnUsername, username);
}
void setSVNPassword(const char *password)
{
	strcpy(svnPassword, password);
}
void setSVNRoot (const char *rootPath)
{
	// Don't copy empty strings
	if (rootPath && *rootPath)
	{
		U32 uLastChar;
		strcpy(svnRoot, rootPath);
		uLastChar = (U32) strlen(svnRoot)-1;
		if (svnRoot[uLastChar] == '/') // Remove trailing separator, if included
			svnRoot[uLastChar] = '\0';

		if (stricmp(svnRoot, SVNROOT_PATH_SVN) == 0)
			seSVNType = SVNROOT_TYPE_SVN;
		else if (stricmp(svnRoot, SVNROOT_PATH_WEBDAV) == 0)
			seSVNType = SVNROOT_TYPE_WEBDAV;
		else
			seSVNType = SVNROOT_TYPE_OTHER;
	}
}

void simpleSVNCopy(char **estr, const char *branch)
{
	if (strStartsWith(branch, "svn://"))
	{
		estrPrintf(estr, "%s", branch);
		if (seSVNType != SVNROOT_TYPE_SVN)
			estrReplaceOccurrences(estr, SVNROOT_PATH_SVN, svnRoot);
	}
	else if (strStartsWith(branch, "http://"))
	{
		estrPrintf(estr, "%s", branch);
		if (seSVNType != SVNROOT_TYPE_WEBDAV)
			estrReplaceOccurrences(estr, SVNROOT_PATH_WEBDAV, svnRoot);
	}
	else
	{
		bool bStartsWithSlash = branch[0] == '/';
		estrPrintf(estr, "%s%s%s", svnRoot, bStartsWithSlash ? "" : "/", branch);
	}
}

static bool parseSVNBaseline(char **estr, ErrorEntry *pEntry, const char *branch)
{
	bool bHadPrefix = true;
	const char *prefix = branch + strlen(branch)-1;
	int i, size;
	while (*prefix != '/' && *prefix != '\\')
	{
		if (prefix == branch)
			return false;
		prefix--;
	}
	prefix++;
	if (*prefix == '\0')
	{
		const char *product = getSVNBranchProject(branch);
		prefix = ETGetShortProductName(product);
		if (!prefix)
			return false;
		bHadPrefix = false;
	}

	if (strlen(prefix) > 3)
	{
		simpleSVNCopy(estr, branch);
		estrTruncateAtLastOccurrence(estr, '.');
		estrAppend2(estr, ".0");
		return true;
	}

	size = eaSize(&pEntry->ppVersions);
	if (size == 0)
		return false;

	// Find the latest matching version
	for (i=size-1; i>=0; i--)
	{
		char *pVersion = (char*) pEntry->ppVersions[i];
		if (strStartsWith(pVersion, prefix))
		{
			char *versionEnd = strstri(pVersion, " ");
			if (versionEnd == NULL)
				continue;
			while (*versionEnd != '.')
			{
				if (versionEnd == pVersion)
					break;
				versionEnd--;
			}
			if (versionEnd == pVersion)
				continue;
			*versionEnd = '\0';
			simpleSVNCopy(estr, branch);
			// Concat the version bit and return the version string to normal
			if (bHadPrefix)
				estrConcatf(estr, "%s.0", pVersion+strlen(prefix));
			else
				estrConcatf(estr, "%s.0", pVersion);
			*versionEnd = '.';
			return true;
		}
	}
	return false;
}

// Find the SVN branch for the newest instance
bool errorTracker_FindSVNBranch (char **estr, ErrorEntry *pEntry)
{
	int i, size;
	BranchTimeLog *pNewest;
	const char *svnProduct;
	size = eaSize(&pEntry->branchTimeLogs);
	if (size == 0)
		return false;
	pNewest = pEntry->branchTimeLogs[0];
	for (i=1; i<size; i++)
	{   // Preferentially take things that HAVE a branch
		if (pEntry->branchTimeLogs[i]->uNewestTime > pNewest->uNewestTime && pEntry->branchTimeLogs[i]->branch)
			pNewest = pEntry->branchTimeLogs[i];
	}
	if (pNewest->branch == NULL)
		return false;
	
	svnProduct = getSVNBranchProject(pNewest->branch);
	if (!svnProduct)
		return false;
	
	if (stricmp(svnProduct, "dev") == 0)
	{
		simpleSVNCopy(estr, pNewest->branch);
		return true;
	}
	else if (strstri(pNewest->branch, "Baselines"))
	{
		return parseSVNBaseline(estr, pEntry, pNewest->branch);
	}
	else // Non-Baseline, non-dev branches
	{
		const char *branchEnd = pNewest->branch + strlen(pNewest->branch)-2;
		const char *branchStart = pNewest->branch;
		int depth = 0;
		if (strStartsWith(pNewest->branch, "svn://"))
		{
			branchStart += strlen(SVNROOT_PATH_SVN);
		}
		else if (strStartsWith(pNewest->branch, "http://"))
		{
			branchStart += strlen(SVNROOT_PATH_WEBDAV);
		}
		while (branchEnd > branchStart)
		{
			if (*branchEnd == '/' || *branchEnd == '\\')
				depth++;
			branchEnd--;
		}
		if (branchEnd <= branchStart)
			return false;
		if (depth > 0)
		{
			simpleSVNCopy(estr, pNewest->branch);
			return true;
		}
		else
		{
			char *newBranch = NULL;
			bool returnVal;
			estrCopy2(&newBranch, pNewest->branch);
			if (*branchStart == '/' || *branchStart == '\\')
				branchStart++;
			if (strStartsWith(branchStart, "StarTrek"))
				estrAppend2(&newBranch, "Baselines/ST");
			else if (strStartsWith(branchStart, "FightClub"))
				estrAppend2(&newBranch, "Baselines/FC");
			else if (strStartsWith(branchStart, "Night"))
				estrAppend2(&newBranch, "Baselines/NW");
			returnVal = parseSVNBaseline(estr, pEntry, newBranch);
			estrDestroy(&newBranch);
			return returnVal;
		}
	}
}

/////////////////////////////
// SVN Calls

static apr_pool_t *sPool = NULL;
static svn_client_ctx_t sContext;
extern CRITICAL_SECTION gSVNBlameCritical;
void initSVN(void)
{
	static bool sbInitialized = false;
	apr_array_header_t *providers;
	svn_auth_provider_object_t *provider;
	svn_auth_baton_t *ab;

	

	if (sbInitialized) 
	{
		return;
	}
	sbInitialized = true;

	PERFINFO_AUTO_START_FUNC();

	if (sPool == NULL)
	{
		apr_initialize();
		sPool = svn_pool_create(NULL);
	}

	// Setting up some stupid providers so things work
	providers = apr_array_make(sPool, 1, sizeof(svn_auth_provider_object_t *));
	svn_auth_get_simple_provider(&provider, sPool);
	*(svn_auth_provider_object_t **)apr_array_push(providers) = provider;
	//svn_auth_get_username_provider(&provider, sPool);
	//*(svn_auth_provider_object_t **)apr_array_push(providers) = provider;


	svn_auth_open(&ab, providers, sPool);
	// initialize ctx structure
	memset(&sContext, 0, sizeof(sContext));
	// get the config based on the configDir passed in
	svn_config_get_config(&sContext.config, NULL, sPool);
	// tell the auth functions where the config is
	svn_auth_set_parameter(ab, SVN_AUTH_PARAM_CONFIG_DIR, NULL);

	sContext.auth_baton = ab;
	sContext.log_msg_baton = NULL;
	sContext.notify_baton = NULL;
	sContext.cancel_baton = NULL;

	svn_auth_set_parameter(ab, SVN_AUTH_PARAM_DEFAULT_USERNAME, svnUsername);
	svn_auth_set_parameter(ab, SVN_AUTH_PARAM_DEFAULT_PASSWORD, svnPassword);
	PERFINFO_AUTO_STOP_FUNC();
}

svn_error_t* SVNBlame_Receiver(void *baton, apr_int64_t line_no, svn_revnum_t revision, const char *author, 
							   const char *date, svn_revnum_t merged_revision, const char *merged_author, 
							   const char *merged_date, const char *merged_path, const char *line, apr_pool_t *pool)
{
	BlameStackOptimized *blame = (BlameStackOptimized*) baton;
	NOCONST(StackTraceLine) *stackline = NULL;
	int iNumLines = eaSize(&blame->ppLines);

	PERFINFO_AUTO_START_FUNC();

	if (blame->ppLines && blame->iLastIndex < iNumLines)
		stackline = CONTAINER_NOCONST(StackTraceLine, blame->ppLines[blame->iLastIndex]);

	// line_no for SVN starts from 0, from 1 for StackTraceLines
	while (stackline && line_no == stackline->iLineNum - 1)
	{
		U32 uTimeOfRev = timeGetSecondsSince2000FromDateString(date);
		estrCopy2(&stackline->pBlamedPerson, author);
		stackline->iBlamedRevision = (int) revision;
		if (uTimeOfRev)
			stackline->uBlamedRevisionTime = uTimeOfRev;
		else
			stackline->uBlamedRevisionTime = -1;
		blame->iLastIndex++;
		if (blame->iLastIndex < iNumLines)
			stackline = CONTAINER_NOCONST(StackTraceLine, blame->ppLines[blame->iLastIndex]);
		else
			stackline = NULL;
	}
	PERFINFO_AUTO_STOP_FUNC();
	return SVN_NO_ERROR;
}

static void errorTracker_RunBlame (BlameStackOptimized *blame, const char *blamePath, int iRevision)
{
	PERFINFO_AUTO_START_FUNC();
	EnterCriticalSection(&gSVNBlameCritical);
	initSVN();
	{
		svn_opt_revision_t start = {0}, end = {0}, peg_revision;
		svn_diff_file_options_t diffOpts = {0};
		svn_error_t *err;
		apr_pool_t *childPool = svn_pool_create(sPool);
		const char *canonPath = svn_path_internal_style(blamePath, childPool);

		peg_revision.kind = svn_opt_revision_unspecified;
		start.kind = svn_opt_revision_number;
		start.value.number = 1;
		if (SVN_IS_VALID_REVNUM(iRevision))
		{
			end.kind = svn_opt_revision_number;
			end.value.number = iRevision;
		}
		else
		{
			end.kind = svn_opt_revision_head;
		}
		diffOpts.ignore_eol_style = true;
		diffOpts.ignore_space = svn_diff_file_ignore_space_none;
		diffOpts.show_c_function = false;

		PERFINFO_AUTO_START("svn client blaming", 1);
		err = svn_client_blame4(canonPath, &peg_revision, &start, &end, &diffOpts, false, true,
			SVNBlame_Receiver, blame, &sContext, childPool);
		PERFINFO_AUTO_STOP();
		if (err)
		{
			printf("An error has occurred: %d - %s.\n", err->apr_err, err->message);
			svn_error_clear(err); 
		}
		svn_pool_destroy(childPool);
	}
	//svn_pool_clear(sPool);
	LeaveCriticalSection(&gSVNBlameCritical);
	PERFINFO_AUTO_STOP_FUNC();
}

bool errorTracker_CreateBlamePath(char **blamePath, const char *filename, const char *branch)
{
	char caseInsensitiveFilename[MAX_PATH] = "";
	char caseSensitiveFilename[MAX_PATH] = "";
	char *pathStart;

	// Skip anything with "AutoGen" - assume it's an autogenned file and don't blame it
	if (!filename || strlen(filename) < 2 || strstri(filename, "AutoGen") != NULL)
		return false;

	pathStart = strstri(filename, "src");

	// expects to find "<drive letter>:<forward/backslash>src..." for SVN files
	if (filename[1] != ':' || !pathStart)
		return false;

	PERFINFO_AUTO_START_FUNC();

	while (*pathStart != '\\' && *pathStart != '/')
		pathStart++;
	while (*pathStart == '\\' || *pathStart == '/')
		pathStart++;
	sprintf(caseInsensitiveFilename, "%s/%s", sBlameRoot, pathStart);
	makeLongPathName(caseInsensitiveFilename, caseSensitiveFilename);
	pathStart = caseSensitiveFilename + strlen(sBlameRoot);
	while (*pathStart == '\\' || *pathStart == '/')
		pathStart++;

	if (branch)
		estrPrintf(blamePath, "%s/%s", branch, pathStart);
	else // branch defaults to dev
		estrPrintf(blamePath, "%s/dev/%s", gErrorTrackerSettings.pSVNRoot, pathStart);

	PERFINFO_AUTO_STOP_FUNC();
	return true;
}

void errorTracker_BlameStackLine (const char *branch, BlameStackOptimized *blame, int iRevision)
{
	char *blamePath = NULL;

	estrStackCreate(&blamePath);
	
	if(!errorTracker_CreateBlamePath(&blamePath, blame->pFilename, branch))
		return;

	errorTracker_RunBlame(blame, blamePath, iRevision);
	PERFINFO_AUTO_STOP_FUNC();
}

static svn_error_t *SVNGetHeadRevision(int *pRevOut, const char *path, const svn_info_t *info, apr_pool_t *pool)
{
	printf ("Last Revision for %s: %d\n", path, info->last_changed_rev);
	if (pRevOut)
		*pRevOut = info->last_changed_rev;
	return SVN_NO_ERROR;
}

AUTO_COMMAND ACMD_CATEGORY(ET_Debug);
int SVNGetRevisionForPath (const char *svnPath)
{
	int rev = 0;
	if (!svnPath || !*svnPath)
		return 0;
	EnterCriticalSection(&gSVNBlameCritical);
	initSVN();
	{
		svn_opt_revision_t peg_revision, start = {0};
		svn_error_t *err;
		apr_pool_t *childPool = svn_pool_create(sPool);
		const char *canonPath = svn_path_internal_style(svnPath, childPool);

		start.kind = svn_opt_revision_head;
		peg_revision.kind = svn_opt_revision_head;
		err = svn_client_info2(canonPath, &peg_revision, &start, SVNGetHeadRevision, &rev, 
			svn_depth_empty, NULL, &sContext, childPool);
		if (err)
		{
			printf("An error has occurred: %d - %s.\n", err->apr_err, err->message);
			svn_error_clear(err); 
		}
		svn_pool_destroy(childPool);
	}
	//svn_pool_clear(sPool);
	LeaveCriticalSection(&gSVNBlameCritical);
	return rev;
}

AUTO_COMMAND ACMD_CATEGORY(ET_Debug);
void RunSVNBranch(int id)
{
	ErrorEntry *pEntry = findErrorTrackerByID(id);
	if (pEntry)
	{
		char *branch = NULL;
		if (errorTracker_FindSVNBranch(&branch, pEntry))
		{
			printf("Found Branch for #%d: %s\n", id, branch);
			estrDestroy(&branch);
		}
	}
}

// does nothing right now
AUTO_COMMAND ACMD_CATEGORY(ET_Debug);
void RunSVNBlame(const char *file, int line, int revnum)
{
	BlameStackOptimized blame = {0};
	NOCONST(StackTraceLine)* stackline = StructCreateNoConst(parse_StackTraceLine);
	eaPush(&blame.ppLines, (StackTraceLine*) stackline);
	stackline->iLineNum = line;
	estrCopy2(&stackline->pFilename, file);
	estrCopy2(&blame.pFilename, file);

	errorTracker_RunBlame(&blame, file, revnum);
	printf("[%d] [Rev %d] %s at %s\n", stackline->iLineNum, stackline->iBlamedRevision, stackline->pBlamedPerson, 
		timeGetLogDateStringFromSecondsSince2000(stackline->uBlamedRevisionTime));

	StructDeInit(parse_BlameStackOptimized, &blame);
	StructDestroyNoConst(parse_StackTraceLine, stackline);
}

AUTO_COMMAND ACMD_CATEGORY(ET_Debug);
void forceBlame(U32 uID)
{
	ErrorEntry *pEntry = findErrorTrackerByID(uID);
	if (pEntry)
		startBlameCache(pEntry);
}

#include "AutoGen/blame_c_ast.c"