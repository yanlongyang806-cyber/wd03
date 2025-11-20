#include "ErrorEntry.h"
#include "AutoGen/ErrorEntry_h_ast.h"
#include "ErrorTracker.h"
#include "errornet.h"
#include "autogen/errornet_h_ast.h"
#include "ErrorTrackerLib.h"
#include "email.h"
#include "ETCommon/ETShared.h"
#include "ETCommon/ETIncomingData.h"
#include "etTrivia.h"

#include "jira.h"
#include "AutoGen/jira_h_ast.h"
#include "callstack.h"
#include "net.h"
#include "objContainer.h"
#include "objTransactions.h"
#include "StashTable.h"
#include "ETCommon/symstore.h"
#include "timing.h"
#include "GlobalComm.h"
#include "crypt.h"
//#include "file.h"
#include "fileutil.h"
#include "fileutil2.h"
#include "FolderCache.h"
#include "trivia.h"
#include "qsortG.h"
#include "AutoGen/trivia_h_ast.h"
#include <winsock2.h>
#include "logging.h"
#include "RateLimit.h"
#include "UTF8.h"

#include "AutoGen/ErrorTrackerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/RateLimit_h_ast.h"

extern StashTable errorSourceFileLineTable;
extern ParseTable parse_CallStack[];
#define TYPE_parse_CallStack CallStack
extern ParseTable parse_JiraIssue[];
#define TYPE_parse_JiraIssue JiraIssue
extern int gPidSymSrv;
extern CRITICAL_SECTION gSymSrvQueue;
extern CRITICAL_SECTION gMergeQueueCritical;
extern CRITICAL_SECTION gDumpReprocessCritical;
extern CRITICAL_SECTION gNewQueueCritical;
extern bool gbETVerbose;

#define SYMSRV_CONNECT_TIMEOUT (10)

// deprecated and not used for transactions
static U32 getNextID(void)
{
	ErrorTrackerContext *pContext = errorTrackerLibGetCurrentContext();
	if(!pContext->entryList.uNextID) 
		pContext->entryList.uNextID++; // We never want ID #0

	do 
	{
		pContext->entryList.uNextID++;
	} while (findErrorTrackerByID(pContext->entryList.uNextID));

	return (U32) pContext->entryList.uNextID;
}

// -----------------------------------------------------------------------
// Queue Stuff

static void errorTracker_SendSymbolLookupRequest(NetLink *link, const char *hashString, const char *stackdata, const char *executable, const char *build, U32 uIP)
{
	char *dataString = NULL;
	struct in_addr ina = {0};
	Packet *pPak;

	ina.S_un.S_addr = uIP;
	estrStackCreate(&dataString);
	estrPrintf(&dataString, "%s -- %s -- %s", executable, build,  inet_ntoa(ina));

	pPak = pktCreate(link, FROM_ERRORTRACKER_SYMLOOKUP);
	pktSendString(pPak, hashString);
	pktSendString(pPak, stackdata);	
	pktSendString(pPak, dataString);
	pktSend(&pPak);
	estrDestroy(&dataString);
}

static QueueNewEntryStruct **sppQueuedEntries = NULL;
static char *gSymbolLookupFilePath = "C:\\src\\Utilities\\bin\\ET";
static int siSymSrvTimeout = 45; // In seconds; this should be long so Error Tracker gets the stack even if 
								 // SymServLookup takes a while (and the client may time out)
void ProcessQueuedEntry(QueueNewEntryStruct *pQueue);

void AddToQueue(QueueNewEntryStruct *pQueue)
{
	EnterCriticalSection(&gSymSrvQueue);
	eaPush(&sppQueuedEntries, pQueue);
	LeaveCriticalSection(&gSymSrvQueue);
}

void SymSrvQueue_FindAndRemoveByLink (NetLink *link)
{
	int i, size = eaSize(&sppQueuedEntries);
	for (i=size-1; i>=0; i--)
	{
		if (link == sppQueuedEntries[i]->link)
			sppQueuedEntries[i]->link = NULL;
	}
}

void DestroyQueueStruct(QueueNewEntryStruct *pQueue)
{
	StructDestroy(parse_ErrorData, pQueue->pErrorData);
	estrDestroy(&pQueue->pHashString);
	estrDestroy(&pQueue->pCallstackText);

	free(pQueue);
	pQueue = NULL;
}

int SymSrvQueue_OncePerFrame(void)
{
	int i, queueSize;
	int iCurTime = timeSecondsSince2000();

	EnterCriticalSection(&gSymSrvQueue);
	queueSize = eaSize(&sppQueuedEntries);
	for (i=0; i<queueSize;)
	{
		QueueNewEntryStruct *pQueue = sppQueuedEntries[i];

		switch (pQueue->eStatus)
		{
		case SYMSTATUS_Connecting:
			if (linkConnected(pQueue->symsrvLink))
			{
				errorTracker_SendSymbolLookupRequest(pQueue->symsrvLink, pQueue->pHashString, pQueue->pCallstackText, 
					pQueue->pErrorData->pExecutableName, pQueue->pErrorData->pVersionString, pQueue->pErrorData->uIP);
				pQueue->eStatus = SYMSTATUS_AwaitingResponse;
				pQueue->starttime = iCurTime;
				i++;
			}
			else if (pQueue->starttime + SYMSRV_CONNECT_TIMEOUT < iCurTime)
			{
				recalcUniqueID(pQueue->pEntry, 0);
				eaRemove(&sppQueuedEntries, i);
				ProcessQueuedEntry(pQueue); // Process with no stack trace lines
				queueSize--;
			}
			else
				i++;
		xcase SYMSTATUS_AwaitingResponse:
			if (!linkConnected(pQueue->link))
				pQueue->link = NULL; // remove the link so it doesn't try to use it to send responses to client
			if (iCurTime - pQueue->starttime > siSymSrvTimeout) 
			{
				// timeout waiting for symsrvlookup response
				ErrorOrAlert("SYMSERV_TIMEOUT", "SymServ timeout after %d seconds", siSymSrvTimeout);
				eaRemove(&sppQueuedEntries, i);
				ProcessQueuedEntry(pQueue); // Process with no stack trace lines
				queueSize--;
			}
			else
				i++;
		}
	}
	LeaveCriticalSection(&gSymSrvQueue);
	return queueSize;
}

void ProcessQueuedEntry(QueueNewEntryStruct *pQueue)
{
	ErrorEntry *pEntry = 0;
	char ip[IPV4_ADDR_STR_SIZE];
	recalcUniqueID(pQueue->pEntry, 0);
	servLog(LOG_SYMSERVLOOKUP, "EntryProcessing", "LookupHash %s BucketingHash %s LinkID %d IP %s", pQueue->pHashString, errorTrackerLibStringFromUniqueHash(CONTAINER_RECONST(ErrorEntry, pQueue->pEntry)),
		linkID(pQueue->link), linkGetIpStr(pQueue->link, SAFESTR(ip)));

	linkRemove(&pQueue->symsrvLink);
	pQueue->pEntry->bDelayResponse = false;
	ProcessEntry(pQueue->link, pQueue->pClientState, pQueue->pEntry);
	DestroyQueueStruct(pQueue);
}

void ProcessStackTrace(const char *pHashString, NOCONST(StackTraceLine) **ppStackTraceLines)
{
	int i;
	int queueSize;

	EnterCriticalSection(&gSymSrvQueue);

	// Loop until we find a matching queue entry.
	queueSize = eaSize(&sppQueuedEntries);
	for (i=0; i<queueSize;i++)
	{
		QueueNewEntryStruct * pQueue = sppQueuedEntries[i];
		if (!strcmp(pHashString, pQueue->pHashString))
		{
			eaRemove(&sppQueuedEntries, i);
			devassert(!pQueue->pEntry->ppStackTraceLines);
			pQueue->pEntry->ppStackTraceLines = ppStackTraceLines;
			ProcessQueuedEntry(pQueue);
			break;		// Exit: There might be more than one, and it will be serviced separately, in order.
		}
	}

	LeaveCriticalSection(&gSymSrvQueue);
}

extern bool gbQueueSymSrvRestart;
void ReceiveSymSrvMessage(Packet *pak,int cmd,NetLink* link,void *user_data)
{
	switch (cmd)
	{
	xcase TO_ERRORTRACKER_SYMSRV_DONE:
		{
			StackTraceLineList stackTraceLines = {0};
			char pHashString[MAX_PATH];
			char *pParseString;

			pktGetString(pak, pHashString, MAX_PATH);
			pParseString = pktGetStringTemp(pak);

			ParserReadText(pParseString, parse_StackTraceLineList, &stackTraceLines, 0);
			ProcessStackTrace(pHashString, (NOCONST(StackTraceLine)**) stackTraceLines.ppStackTraceLines);
		}
	xcase TO_ERRORTRACKER_SYMSRV_STATUS_UPDATE:
		{
			char *pHashString = pktGetStringTemp(pak);
			int statuscount = pktGetU32(pak);
			int i;
			int curTime = timeSecondsSince2000();

			EnterCriticalSection(&gSymSrvQueue);
			for (i=0; i<eaSize(&sppQueuedEntries); i++)
			{
				if (!strcmp(pHashString, sppQueuedEntries[i]->pHashString))
				{
					sppQueuedEntries[i]->statuscount = statuscount;
					sppQueuedEntries[i]->starttime = curTime; // reset timeout

					errorTrackerSendStatusUpdate(sppQueuedEntries[i]->link, STATE_ERRORTRACKER_STACKWALK);
				}
			}
			LeaveCriticalSection(&gSymSrvQueue);
		}
	xcase TO_ERRORTRACKER_SYMSRV_MEM_OVERLIMIT:
		{
			F32 fMemUsage = pktGetF32(pak);
			U32 uMemLimit = pktGetU32(pak);
			if (gbETVerbose)
				printf("SymServLookup exceeded memory usage limits: %6.2fMB out of %dMB\n", fMemUsage, uMemLimit);
			gbQueueSymSrvRestart = true;
			SERVLOG_PAIRS(LOG_MEMREPORT, "SymServLookupMem", 
				("usage", "%6.2fMB", fMemUsage));
		}
	xcase TO_ERRORTRACKER_SYMSRV_MODULE_FAILURE:
		{
			const char *pModule = pktGetStringTemp(pak);
			const char *pGuid = pktGetStringTemp(pak);
			const char *pHashString = pktGetStringTemp(pak);

			if (pModule != NULL && pGuid != NULL)
				AddSymbolLookupFailure(pModule, pGuid);
		}
	}
}

static RateLimit *gpETRateLimit = NULL;

static bool errorRateLimit(char *errMsg, ETRateLimitActivity eActivity)
{
	bool bAllowed = true;

	PERFINFO_AUTO_START_FUNC();

	if (!gpETRateLimit)
	{
		RateLimitConfig rateLimitConfig = {0};

		StructInit(parse_RateLimitConfig, &rateLimitConfig);
		rateLimitConfig.bEnabled = false; // Disabled until a file says otherwise
		RLAutoLoadConfigFromFile(&gpETRateLimit, "server/ErrorTracker/errorRateLimit.txt",
			&rateLimitConfig, "ET_RATE_LIMIT", ETRateLimitActivityEnum);
		StructDeInit(parse_RateLimitConfig, &rateLimitConfig);
	}

	if (!devassert(gpETRateLimit))
	{
		PERFINFO_AUTO_STOP();
		return true;
	}

	if (!errMsg || !*errMsg)
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	bAllowed = RLCheck(gpETRateLimit, errMsg, eActivity);
	
	PERFINFO_AUTO_STOP();
	return bAllowed;
}

// -----------------------------------------------------------------------
// Creating a New ErrorEntry

// Generates a brand new ErrorEntry, which will either be merged into a 
// pre-existing ErrorEntry (using mergeErrorTrackerEntries()), or will
// be added to our full list.
NOCONST(ErrorEntry) * createErrorEntry_ErrorTracker(NetLink *link, IncomingClientState *pClientState, ErrorData *pErrorData, int iMergedId)
{
	// Note: The order of operations in this file matches the struct order...
	//       keeps these large-struct-manipulating function calls in check.

	bool addToQueue = (link && pClientState);
	NOCONST(ErrorEntry) *pEntry = NULL;
	PERFINFO_AUTO_START_FUNC();

	if ((errorTrackerLibGetOptions() & ERRORTRACKER_OPTION_DISABLE_ERROR_LIMITING) == 0 && 
		pErrorData->eType == ERRORDATATYPE_ERROR && pErrorData->pSourceFile)
	{
		char pErrorSourceKey[256];
		int iCount = 0;
		sprintf(pErrorSourceKey, "%s:%d", pErrorData->pSourceFile, pErrorData->iSourceFileLine);

		if (stashFindInt(errorSourceFileLineTable, pErrorSourceKey, &iCount))
		{
			iCount++;
			stashAddInt(errorSourceFileLineTable, pErrorSourceKey, iCount, true);
		}
		else
			stashAddInt(errorSourceFileLineTable, pErrorSourceKey, 1, true);

		if (!errorRateLimit(pErrorSourceKey, ETRLA_ErrorReceive))
		{
			StructDestroyNoConst(parse_ErrorEntry, pEntry);
			PERFINFO_AUTO_STOP();
			return NULL;
		}
	}
	pEntry = createErrorEntryFromErrorData(pErrorData, iMergedId, &addToQueue);
	FilterIncomingTriviaData((TriviaData***) &pEntry->ppTriviaData);

	if (addToQueue)
	{
		// Try to send the callstack to external SymServLookup executable to process stack trace information
		U32 aiUniqueHash[ET_HASH_ARRAY_SIZE];
		char *fnCS = 0, *fnST = 0;
		CallStack *pCallstack;
		QueueNewEntryStruct *pQS = 0;
		char *cmd = NULL;
		
		// No SymServLookup running
		if (!gPidSymSrv)
		{
			recalcUniqueID(pEntry, 0);
			PERFINFO_AUTO_STOP();
			return pEntry;
		}

		pCallstack = callstackCreateFromTextReport(pErrorData->pStackData);
		pQS = (QueueNewEntryStruct*) calloc(1, sizeof(QueueNewEntryStruct));

		ParserWriteText(&pQS->pCallstackText, parse_CallStack, pCallstack, 0, 0, 0);
		StructDestroy(parse_CallStack, pCallstack);
		pCallstack = NULL;
		cryptMD5(pQS->pCallstackText, (int)strlen(pQS->pCallstackText), aiUniqueHash); //Used for locating this error in the queue later, not for merging.

		estrConcatf(&pQS->pHashString, "%d_%d_%d_%d", 
			aiUniqueHash[0], aiUniqueHash[1], aiUniqueHash[2], aiUniqueHash[3]);
		pQS->fNameCallstack = pQS->fNameStackTrace = 0;
		pQS->pEntry = pEntry;
		pQS->pErrorData = pErrorData;
		pQS->link = link;
		pQS->pClientState = pClientState;
		pQS->eStatus = SYMSTATUS_Connecting;
		pQS->symsrvLink = commConnect(getSymbolServerComm(), LINKTYPE_UNSPEC, LINK_NO_COMPRESS|LINK_FORCE_FLUSH,
			"localhost", DEFAULT_SYMSRV_PORT, ReceiveSymSrvMessage,0,0,0);
		pQS->starttime = timeSecondsSince2000();

		AddToQueue(pQS);
		pEntry->bDelayResponse = true;
		PERFINFO_AUTO_STOP();
		return pEntry;
	}
	else
	{
		// Unique Hash ID was generated - stringify it
		estrCopy2(&pEntry->pStashString, errorTrackerLibStringFromUniqueHash(CONST_ENTRY(pEntry)));
		PERFINFO_AUTO_STOP();
		return pEntry;
	}
}

void SymbolLookupTestMsg(Packet *pak,int cmd,NetLink* link,void *user_data)
{
	switch (cmd)
	{
	xcase TO_ERRORTRACKER_SYMSRV_DONE:
		{
			StackTraceLineList stackTraceLines = {0};
			char pHashString[MAX_PATH];
			char *pParseString;

			pktGetString(pak, pHashString, MAX_PATH);
			pParseString = pktGetStringTemp(pak);

			ParserReadText(pParseString, parse_StackTraceLineList, &stackTraceLines, 0);
			printf("Symbol Lookup Results:\n");
			EARRAY_CONST_FOREACH_BEGIN(stackTraceLines.ppStackTraceLines, i, s);
			{
				StackTraceLine *line = stackTraceLines.ppStackTraceLines[i];
				printf("%s - %s - %s (%d)\n", line->pModuleName, line->pFunctionName, line->pFilename, line->iLineNum);
			}
			EARRAY_FOREACH_END;
			StructDeInit(parse_StackTraceLineList, &stackTraceLines);

			linkRemove(&link);
		}
	xcase TO_ERRORTRACKER_SYMSRV_STATUS_UPDATE:
		// does nothing here
	xcase TO_ERRORTRACKER_SYMSRV_MEM_OVERLIMIT:
		// does nothing here
	xcase TO_ERRORTRACKER_SYMSRV_MODULE_FAILURE:
		{
			const char *pModule = pktGetStringTemp(pak);
			const char *pGuid = pktGetStringTemp(pak);
			const char *pHashString = pktGetStringTemp(pak);
			printf("Failed to load symbols for module %s. Module GUID: \"%s\" Lookup Hash: %s\n", pModule, pGuid, pHashString);
		}
	}
}

static void testSymbolLookup_helper(CallStack *pCallstack)
{
	char *pCallstackText = 0;
	char *pHashString = NULL;
	U32 aiUniqueHash[ET_HASH_ARRAY_SIZE];

	ParserWriteText(&pCallstackText, parse_CallStack, pCallstack, 0, 0, 0);
	cryptMD5(pCallstackText, (int)strlen(pCallstackText), aiUniqueHash);

	estrConcatf(&pHashString, "%d_%d_%d_%d", 
		aiUniqueHash[0], aiUniqueHash[1], aiUniqueHash[2], aiUniqueHash[3]);

	if (gPidSymSrv)
	{
		NetLink *symsrvLink = commConnect(getSymbolServerComm(), LINKTYPE_UNSPEC, LINK_NO_COMPRESS|LINK_FORCE_FLUSH,
			"localhost", DEFAULT_SYMSRV_PORT, SymbolLookupTestMsg,0,0,0);
		if (linkConnectWait(&symsrvLink,2.f)) // connect successful
		{
			errorTracker_SendSymbolLookupRequest(symsrvLink, pHashString, pCallstackText, "Testing", "Testing", 0);
		}
	}
	estrDestroy(&pCallstackText);
	estrDestroy(&pHashString);
}

AUTO_COMMAND ACMD_CATEGORY(ET_Debug);
void testSymbolLookup (char *pCallstackFile)
{
	FILE *pFile = fopen(pCallstackFile, "rt");
	U32 uFileSize;
	char *fileData = NULL;
	CallStack *pCallstack = NULL;

	if (!pFile)
		return;
	uFileSize = (U32) fileGetSize(pFile);
	fileData = malloc(uFileSize);
	fread(fileData, sizeof(char), uFileSize, pFile);
	fclose(pFile);
	
	pCallstack = callstackCreateFromTextReport(fileData);
	testSymbolLookup_helper(pCallstack);

	free(fileData);
	StructDestroy(parse_CallStack, pCallstack);
}


AUTO_COMMAND ACMD_CATEGORY(ET_Debug);
void testSymbolLookupTP (char *pCallstackFile)
{
	CallStack callstack = {0};	
	if (!fileExists(pCallstackFile))
		return;	
	ParserReadTextFile(pCallstackFile, parse_CallStack, &callstack, 0);
	testSymbolLookup_helper(&callstack);
	StructDeInit(parse_CallStack, &callstack);
}

void trNewErrorEntry_CB(TransactionReturnVal *returnVal, ErrorTransactionNewQueue *pQueue)
{
	EnterCriticalSection(&gNewQueueCritical);
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		ErrorEntry *pEntry;
		pQueue->uNewID = atoi(returnVal->pBaseReturnVals->returnString);

		pEntry = findErrorTrackerByID(pQueue->uNewID);
		if (pEntry)
		{
			pEntry->iDailyCount = pEntry->iTotalCount;
			pEntry->bIsNewEntry = true;
			ErrorEntry_AddHashStash(pEntry);
			addEntryToStashTables(pEntry);
			if (gbETVerbose)
				printf("Recording new crash # %d\n", pEntry->uID);
		}
		else if (gbETVerbose)
			printf("\n%s - Could not find new entry %d.\n", StaticDefineIntRevLookup(GlobalTypeEnum, GLOBALTYPE_ERRORTRACKERENTRY), 
				pQueue->uNewID);
	}
	else
	{
		pQueue->uNewID = 0;
	}
	pQueue->eStep = ERRORTRANS_STEP_PROCESSTRIVIA;
	LeaveCriticalSection(&gNewQueueCritical);
}

// Saves the new ErrorEntry to the DB
void addNewErrorEntry(ErrorTransactionNewQueue *pQueue)
{
	Container * con = NULL;
	PERFINFO_AUTO_START_FUNC();

	if (pQueue)
	{
		pQueue->eStep = ERRORTRANS_STEP_CREATENEW;
		pQueue->ppTriviaData = pQueue->pNewEntry->ppTriviaData;
		pQueue->pNewEntry->ppTriviaData = NULL;
	}
	objRequestContainerCreateLocal(objCreateManagedReturnVal(trNewErrorEntry_CB, pQueue), GLOBALTYPE_ERRORTRACKERENTRY, pQueue->pNewEntry);
	StructDestroyNoConstSafe(parse_ErrorEntry, &pQueue->pNewEntry);
	PERFINFO_AUTO_STOP();
}

AUTO_TRANSACTION ATR_LOCKS(pEntry, ".triviaOverview, .uLastSavedTrivia");
enumTransactionOutcome trErrorEntry_UpdateTriviaOverview(ATR_ARGS, NOCONST(ErrorEntry) *pEntry, NON_CONTAINER TriviaList *pTriviaList)
{
	FilterForTriviaOverview(CONTAINER_RECONST(TriviaOverview, &pEntry->triviaOverview), pTriviaList->triviaDatas);
	pEntry->uLastSavedTrivia = timeSecondsSince2000();
	return TRANSACTION_OUTCOME_SUCCESS;
}

//Add comment trivia to the the commentList.
AUTO_TRANSACTION ATR_LOCKS(pEntry, ".ppCommentList");
enumTransactionOutcome trErrorEntry_AddTriviaDescription(ATR_ARGS, NOCONST(ErrorEntry) *pEntry, const char *description, const char *ip)
{
	NOCONST(CommentEntry) *entry;
	entry = StructCreateNoConst(parse_CommentEntry);
	entry->pDesc = strdup(description);
	entry->pIP = strdup(ip);
	//Add new info to entry here when we add it - will require finding it and passing it to this function.
	eaPush(&pEntry->ppCommentList, entry);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(pEntry, ".aiUniqueHash, .aiUniqueHashNew");
enumTransactionOutcome trErrorEntry_AddNewHash(ATR_ARGS, NOCONST(ErrorEntry) *pEntry, const U32 hash0, const U32 hash1, const U32 hash2, const U32 hash3)
{
	pEntry->aiUniqueHash[0] = pEntry->aiUniqueHash[1] = pEntry->aiUniqueHash[2] = pEntry->aiUniqueHash[3] = 0;
	pEntry->aiUniqueHashNew[0] = hash0;
	pEntry->aiUniqueHashNew[1] = hash1;
	pEntry->aiUniqueHashNew[2] = hash2;
	pEntry->aiUniqueHashNew[3] = hash3;
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(pEntry, ".ppExecutableCounts, .ppExecutableNames[AO], .pLargestMemory, .iTotalCount");
enumTransactionOutcome trErrorEntry_UpdateMergedEntry(ATR_ARGS, NOCONST(ErrorEntry) *pEntry, const char *memory)
{
	if (memory)
		pEntry->pLargestMemory = strdup(memory);
	if (eaiSize(&pEntry->ppExecutableCounts) == 0)
		eaiPush(&pEntry->ppExecutableCounts, pEntry->iTotalCount);
	else
		pEntry->ppExecutableCounts[0] = pEntry->iTotalCount;

	//Under normal circumstances, this function should be called with one entry in ExecutableNames and no entries in ExecutableCounts.
	//The code below should never execute, but it is a safeguard to make sure these arrays must be the same size, as other code relies on that fact.
	while (eaiSize(&pEntry->ppExecutableCounts) < eaSize(&pEntry->ppExecutableNames))
	{
		eaiPush(&pEntry->ppExecutableCounts, 0);
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

// This is called after the container is created and before the client response is sent
void ErrorEntryNew_PostProcessing (ErrorEntry *pEntry, ErrorTransactionNewQueue *pQueue)
{
	recordErrorEntry(pEntry->uID, ETINDEX_START, pEntry);

	if (errorTrackerLibGetOptions() & ERRORTRACKER_OPTION_LOG_TRIVIA)
	{
		LogTriviaData(pEntry->uID, (CONST_EARRAY_OF(TriviaData)) pQueue->ppTriviaData);
	}
	else if (errorTrackerLibGetOptions() & ERRORTRACKER_OPTION_WRITE_TRIVIA_DISK)
	{
		NOCONST(TriviaListMultiple) trivia = {0};
		NOCONST(TriviaList) triviaList = {0};
		char triviaFilename[MAX_PATH];

		calcTriviaDataPath(SAFESTR(triviaFilename), pEntry->uID);
		objRequestTransactionSimplef(NULL, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID, 
			"changeTriviaFile", "set pTriviaFilename = \"%s\"", triviaFilename);
		mkdirtree(triviaFilename);
		triviaList.triviaDatas = (NOCONST(TriviaData)**) pQueue->ppTriviaData;
		eaPush(&trivia.triviaLists, &triviaList);
		ParserWriteTextFile(pEntry->pTriviaFilename, parse_TriviaListMultiple, &trivia, 0, 0);
		triviaList.triviaDatas = NULL;
		eaDestroy(&trivia.triviaLists);
	}
	if ((errorTrackerLibGetOptions() & ERRORTRACKER_OPTION_LOG_TRIVIA) == 0 && eaSize(&pQueue->ppTriviaData) > 0)
	{
		NOCONST(TriviaList) trivia = {0};
		trivia.triviaDatas = pQueue->ppTriviaData;
		AutoTrans_trErrorEntry_UpdateTriviaOverview(NULL, GLOBALTYPE_ERRORTRACKER, GLOBALTYPE_ERRORTRACKERENTRY, 
			pEntry->uID, (TriviaList*) (&trivia));
		trivia.triviaDatas = NULL;

		// Keep the pEntry->ppTriviaData around as redundancy for dump cloning
		// Dump request code will delete it if it's unneeded
	}
	pQueue->eStep = ERRORTRANS_STEP_DONE;
}

// -----------------------------------------------------------------------
// Merging into an existing ErrorEntry

AUTO_TRANSACTION ATR_LOCKS(pDst, ".Plargestmemory, .Breplacecallstack, .Ppstacktracelines, .Bunlimitedusers, .Ppshardinfostrings, .Ppuserinfo, \
	.Bproductionmode, .Itotalcount, .Unewesttime, .Branchtimelogs, .Plastblamedperson, .Imaxclients, .Itotalclients, .Etype, .Ppappglobaltypenames, \
	.Ppversions, .Ppbranches, .Ppproductionbuildnames, .Ppipcounts, .Ppexecutablenames, .Ppexecutablecounts, .Eaproductoccurrences, .Ppdaycounts, \
	.Ufirsttime, .Ppplatformcounts");
enumTransactionOutcome trErrorEntry_Merge(ATR_ARGS, NOCONST(ErrorEntry) *pDst, NON_CONTAINER ErrorEntry *pNew, U32 uTime)
{
	mergeErrorEntry_Part1(pDst, pNew, uTime);
	return TRANSACTION_OUTCOME_SUCCESS;
}
AUTO_TRANSACTION ATR_LOCKS(pDst, ".*");
enumTransactionOutcome trErrorEntry_Merge2(ATR_ARGS, NOCONST(ErrorEntry) *pDst, NON_CONTAINER ErrorEntry *pNew, U32 uTime)
{
	mergeErrorEntry_Part2(pDst, pNew, uTime);
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Initialize the non-Containered data for the merger process
void initMergeErrorEntries(ErrorEntry *pDst, NOCONST(ErrorEntry) *pNew, int *piETIndex)
{
	*piETIndex = pDst->iTotalCount+1;
	recordErrorEntry(pDst->uID, *piETIndex, CONST_ENTRY(pNew));

	if (gbETVerbose) printf("Merging data with pre-existing crash # %d\n", pDst->uID);
	if(errorTrackerLibGetOptions() & ERRORTRACKER_OPTION_LOG_TRIVIA)
	{
		LogTriviaData(pDst->uID, CONST_ENTRY(pNew)->ppTriviaData);
		eaDestroyStructNoConst(&pNew->ppTriviaData, parse_TriviaData);
	}

	// -------------------------------------------------------------------------------
	// Identification
	// * iUniqueID is deprecated
	pNew->uID = pDst->uID;       // Set the ID of the new entry to the pre-existing ID
	// * aiUniqueHash should match
	// * eType should match
}

static ErrorTransactionMergeQueue  **sMergeQueue = NULL;
static ErrorTransactionNewQueue **sNewQueue = NULL;

AUTO_RUN;
void InitializeErrorEntryQueues(void)
{
	eaIndexedEnable(&sMergeQueue, parse_ErrorTransactionMergeQueue);
}

static DWORD sMainThreadID = 0; // used for ERRORTRACKER_OPTION_FORCE_SYNTRANS by CB
void ErrorEntry_SetMainThread(DWORD id)
{
	sMainThreadID = id;
}

void ErrorEntry_ProcessMerge (U32 uLinkID, ErrorEntry *pEntry, NOCONST(ErrorEntry) *pNewEntry);
void ErrorEntry_AddMergeQueue (U32 uLinkID, ErrorEntry *pMerge, NOCONST(ErrorEntry) *pNew)
{
	U32 uStartTime = timeSecondsSince2000(), uEndTime = 0;
	if (errorTrackerLibGetOptions() & ERRORTRACKER_OPTION_FORCE_SYNTRANS)
	{
		EnterCriticalSection(&gMergeQueueCritical);
		{
			ErrorTransactionMergeQueue *pQueue = eaIndexedGetUsingInt(&sMergeQueue, pMerge->uID);
			ErrorTransactionResponseData *pData = StructCreate(parse_ErrorTransactionResponseData);
			pData->pEntry = CONST_ENTRY(pNew);
			pData->linkID = uLinkID;

			if (!pQueue)
			{
				pQueue = StructCreate(parse_ErrorTransactionMergeQueue);
				pQueue->uMergeID = pMerge->uID;
				pQueue->eStep = ERRORTRANS_STEP_MERGE;
				eaIndexedAdd(&sMergeQueue, pQueue);
			}
			eaPush(&pQueue->ppNewEntries, pData);
		}
		LeaveCriticalSection(&gMergeQueueCritical);
	}
	else // Non-CB, this will always be run in the main thread
		ErrorEntry_ProcessMerge(uLinkID, pMerge, pNew);

	if (errorTrackerLibGetOptions() & ERRORTRACKER_OPTION_FORCE_SYNTRANS && sMainThreadID && sMainThreadID == GetCurrentThreadId())
	{
		while (eaSize(&sNewQueue) || eaSize(&sMergeQueue))
		{
			errorTrackerLibOncePerFrame_MainThread();
			Sleep(1);
		}
	}
	uEndTime = timeSecondsSince2000();
	if (uEndTime - uStartTime > 30)
	{
		char path[MAX_PATH];
		sprintf(path, "%s/timing.log", errorTrackerGetDatabaseDir());
		filelog_printf(path, "Merging to ID #%d took [%d] seconds.\n", pMerge->uID, uEndTime - uStartTime);
	}
}

void ErrorEntry_AddNewQueue (U32 uLinkID, NOCONST(ErrorEntry) *pNew)
{
	EnterCriticalSection(&gNewQueueCritical);
	{
		ErrorTransactionNewQueue *pQueue = malloc(sizeof(ErrorTransactionNewQueue));
		pQueue->pNewEntry = pNew;
		pQueue->linkID = uLinkID;
		pQueue->eStep = ERRORTRANS_STEP_START;
		pQueue->uNewID = 0;
		pQueue->ppTriviaData = NULL;
		eaPush(&sNewQueue, pQueue);
	}
	LeaveCriticalSection(&gNewQueueCritical);

	if (errorTrackerLibGetOptions() & ERRORTRACKER_OPTION_FORCE_SYNTRANS && sMainThreadID && sMainThreadID == GetCurrentThreadId())
	{
		while (eaSize(&sNewQueue) || eaSize(&sMergeQueue))
		{
			errorTrackerLibOncePerFrame_MainThread();
			Sleep(1);
		}
	}
}

void ErrorEntry_ProcessMerge (U32 uLinkID, ErrorEntry *pEntry, NOCONST(ErrorEntry) *pNewEntry)
{
	int iETIndex = 0;
	U32 uMergeTime = timeSecondsSince2000();
	NetLink *link = FindClientLink(uLinkID);
	initMergeErrorEntries (pEntry, pNewEntry, &iETIndex);
	
	// Clone itself as a recent entry
	if (gErrorTrackerSettings.iMaxInfoEntries > 0 && pEntry->iTotalCount == 1)
		AutoTrans_trErrorEntry_AddRecentError(NULL, GLOBALTYPE_ERRORTRACKER, GLOBALTYPE_ERRORTRACKERENTRY, 
			pEntry->uID, pEntry, iETIndex, gErrorTrackerSettings.iMaxInfoEntries);
 
	AutoTrans_trErrorEntry_Merge (NULL, GLOBALTYPE_ERRORTRACKER, 
		GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID, CONST_ENTRY(pNewEntry), uMergeTime);

	AutoTrans_trErrorEntry_Merge2 (NULL, GLOBALTYPE_ERRORTRACKER, 
		GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID, CONST_ENTRY(pNewEntry), uMergeTime);
	pEntry->iDailyCount += pNewEntry->iTotalCount;

	ProcessEntry_Finish(link, pEntry, pNewEntry, iETIndex, NULL);
}

void ReprocessDumpQueue(void);
void ErrorEntry_OncePerFrame(void)
{
	int i, size, timer;
	PERFINFO_AUTO_START_FUNC();
	EnterCriticalSection(&gNewQueueCritical);
	size = eaSize(&sNewQueue);
	if (size)
	{
		timer = timerAlloc();
		timerStart(timer);
		for(i = 0; i < eaSize(&sNewQueue); i++)
		{
			switch (sNewQueue[i]->eStep)
			{
			case ERRORTRANS_STEP_START:
				PERFINFO_AUTO_START("ErrorTrans_Start", 1);
				{
					ErrorEntry *pFoundEntry = findErrorTrackerEntryFromNewEntry(sNewQueue[i]->pNewEntry);
					if (!pFoundEntry)
						addNewErrorEntry(sNewQueue[i]);
					else
					{
						ErrorEntry_AddMergeQueue(sNewQueue[i]->linkID, pFoundEntry, sNewQueue[i]->pNewEntry);
						free(sNewQueue[i]);
						PERFINFO_AUTO_START("EARemove", 1);
						eaRemove(&sNewQueue, i);
						PERFINFO_AUTO_STOP();
						i--;
					}
				}
				PERFINFO_AUTO_STOP();
			xcase ERRORTRANS_STEP_PROCESSTRIVIA:
				{	
					ErrorEntry *pEntry = findErrorTrackerByID(sNewQueue[i]->uNewID);
					if (pEntry)
					{
						PERFINFO_AUTO_START("ErrorTrans_ProcessTrivia", 1);
						ErrorEntryNew_PostProcessing(pEntry, sNewQueue[i]);
						PERFINFO_AUTO_STOP();
					}
					else
					{
						eaDestroyStructNoConst(&sNewQueue[i]->ppTriviaData, parse_TriviaData);
						free(sNewQueue[i]);
						PERFINFO_AUTO_START("EARemove", 1);
						eaRemove(&sNewQueue, i);
						PERFINFO_AUTO_STOP();
						i--;
						break;
					}
				}
				// fall through is intended
			case ERRORTRANS_STEP_DONE:
				PERFINFO_AUTO_START("ErrorTrans_Done", 1);
				{
					NetLink *link = FindClientLink(sNewQueue[i]->linkID);
					if (sNewQueue[i]->uNewID)
					{
						ErrorEntry *pEntry = findErrorTrackerByID(sNewQueue[i]->uNewID);
						if (pEntry)
						{
							ProcessEntry_Finish(link, pEntry, NULL, ETINDEX_START, sNewQueue[i]->ppTriviaData);
						}
						else
						{
							if (gbETVerbose) 
								printf("\n%s - Could not find new entry %d.\n", StaticDefineIntRevLookup(GlobalTypeEnum, GLOBALTYPE_ERRORTRACKERENTRY), 
									sNewQueue[i]->uNewID);
						}
					}
					else
					{   // Transaction failed for some reason
						sendFailureResponse(link);
					}
					// This is always either copied or never used
					PERFINFO_AUTO_START("ErrorTrans_Cleanup", 1);
					eaDestroyStructNoConst(&sNewQueue[i]->ppTriviaData, parse_TriviaData);
					free(sNewQueue[i]);
					eaRemove(&sNewQueue, i);
					i--;
					PERFINFO_AUTO_STOP();
				}
				PERFINFO_AUTO_STOP();
			}
			if (timerElapsed(timer) > 0.5)
				break;
		}
		timerFree(timer);
	}
	LeaveCriticalSection(&gNewQueueCritical);

	// Merge queue is needed for processing queued "new" entries that match a just-entered entry
	EnterCriticalSection(&gMergeQueueCritical);
	size = eaSize(&sMergeQueue);
	for (i=size-1; i>=0; i--)
	{
		ErrorTransactionMergeQueue *merger = sMergeQueue[i];
		if (merger && eaSize(&merger->ppNewEntries))
		{
			ErrorEntry *pEntry = findErrorTrackerByID(merger->uMergeID);
			ErrorTransactionResponseData *pCurData = merger->ppNewEntries[0];

			if (!pEntry)
			{
				StructDestroy(parse_ErrorTransactionResponseData, pCurData);
				eaRemove(&merger->ppNewEntries, 0);
				printf("\n%s - Could not find merger entry %d.\n", StaticDefineIntRevLookup(GlobalTypeEnum, GLOBALTYPE_ERRORTRACKERENTRY), 
					merger->uMergeID);
				continue;
			}
			switch (merger->eStep)
			{
			case ERRORTRANS_STEP_MERGE:
				{
					NetLink *link = FindClientLink(pCurData->linkID);
					U32 uMergeTime = timeSecondsSince2000();
					int iETIndex = 0;
					initMergeErrorEntries (pEntry, UNCONST_ENTRY(pCurData->pEntry), &iETIndex);

					AutoTrans_trErrorEntry_Merge (NULL, GLOBALTYPE_ERRORTRACKER, 
						GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID, pCurData->pEntry, uMergeTime);

					AutoTrans_trErrorEntry_Merge2 (NULL, GLOBALTYPE_ERRORTRACKER, 
						GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID, pCurData->pEntry, uMergeTime);
					pEntry->iDailyCount += pCurData->pEntry->iTotalCount;

					ProcessEntry_Finish(link, pEntry, UNCONST_ENTRY(pCurData->pEntry), iETIndex, NULL);

					// Remove the new entry that is done merging. Remove the merge queue entry 
					// if there are no more new entries pending merger.
					StructDestroy(parse_ErrorTransactionResponseData, pCurData);
					eaRemove(&merger->ppNewEntries, 0);
					merger->eStep = ERRORTRANS_STEP_MERGE; // Just to make sure it stays on this
				}
			}
			if (eaSize(&merger->ppNewEntries) == 0)
			{
				eaRemove(&sMergeQueue, eaIndexedFindUsingInt(&sMergeQueue, merger->uMergeID));
				StructDestroy(parse_ErrorTransactionMergeQueue, merger);
			}
		}
	}
	LeaveCriticalSection(&gMergeQueueCritical);
	ReprocessDumpQueue();
	PERFINFO_AUTO_STOP();
}

AUTO_TRANSACTION ATR_LOCKS(pEntry, ".ppDumpNotifyEmail");
enumTransactionOutcome trErrorEntry_AddDumpNotify(ATR_ARGS, NOCONST(ErrorEntry) *pEntry, SA_PARAM_NN_STR const char *email)
{
	char *copyEmail = NULL;
	estrCopy2(&copyEmail, email);
	eaPush(&pEntry->ppDumpNotifyEmail, copyEmail);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(pEntry, ".ppDumpNotifyEmail[AO]");
enumTransactionOutcome trErrorEntry_RemoveDumpNotify(ATR_ARGS, NOCONST(ErrorEntry) *pEntry, const int index)
{
	eaRemove(&pEntry->ppDumpNotifyEmail, index);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(pEntry, ".pJiraIssue");
enumTransactionOutcome trErrorEntry_SetJiraIssue(ATR_ARGS, NOCONST(ErrorEntry) *pEntry, 
	const char *key,
	const char *assignee,
	const int status,
	const int resolution)
{
	if (!pEntry->pJiraIssue)
		pEntry->pJiraIssue = StructCreateNoConst(parse_JiraIssue);
	estrCopy2(&pEntry->pJiraIssue->key, key);
	estrCopy2(&pEntry->pJiraIssue->assignee, assignee);
	pEntry->pJiraIssue->status = status;
	pEntry->pJiraIssue->resolution = resolution;
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(pEntry, ".ppDumpNotifyEmail");
enumTransactionOutcome trErrorEntry_RemoveNotifyMails(ATR_ARGS, NOCONST(ErrorEntry) *pEntry)
{
	eaDestroyEString(&pEntry->ppDumpNotifyEmail);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(pEntry, ".ppTriviaData");
enumTransactionOutcome trErrorEntry_RemoveTriviaData(ATR_ARGS, NOCONST(ErrorEntry) *pEntry)
{
	eaDestroyStructNoConst(&pEntry->ppTriviaData, parse_TriviaData);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_COMMAND ACMD_CATEGORY(ET_Debug);
void ErrorEntry_RunSimpleTransaction (U32 uID, char *transName, ACMD_SENTENCE transString)
{
	objRequestTransactionSimplef(NULL, GLOBALTYPE_ERRORTRACKERENTRY, uID, 
			transName, "%s", transString);
}

// Continuous Builder Transactions - removed?
/*enumTransactionOutcome trErrorEntry_CBAddUserData(ATR_ARGS, NOCONST(ErrorEntry) *pEntry, NON_CONTAINER ErrorTrackerEntryUserData *pData)
{
	if (pEntry->pUserData)
		StructDestroy(parse_ErrorTrackerEntryUserData, pEntry->pUserData);
	pEntry->pUserData = StructCreate(parse_ErrorTrackerEntryUserData);
	if (pData->pStepsWhereItHappened)
		estrCopy2(&pEntry->pUserData->pStepsWhereItHappened, pData->pStepsWhereItHappened);
	pEntry->pUserData->iEmailNumberWhenItFirstHappened = pData->iEmailNumberWhenItFirstHappened;
	pEntry->pUserData->iTimeWhenItFirstHappened = pData->iTimeWhenItFirstHappened;
	pEntry->pUserData->pDumpFileName = StructAllocString(pData->pDumpFileName);
	pEntry->pUserData->pMemoryDumpFileName = StructAllocString(pData->pMemoryDumpFileName);
	return TRANSACTION_OUTCOME_SUCCESS;
}

enumTransactionOutcome trErrorEntry_CBUserDataAppendStep(ATR_ARGS, NOCONST(ErrorEntry) *pEntry, const char *step)
{
	estrAppend2(&pEntry->pUserData->pStepsWhereItHappened, step);
	return TRANSACTION_OUTCOME_SUCCESS;
}*/

///////////////////////////////////////////
// Loading from Dump Files stuff
///////////////////////////////////////////

static char *CDBProcessBatchFile = "dump_crashstack.bat";

// Appends "-%d.txt" to these; They should also be in the same directory so the failsafe cleanup works properly
#define CDB_OUTPUT_FILE "cdbout" 
#define CDB_TRASH_FILE "trash" // for redirecting the output so it doesn't clutter stuff up


// TODO return feedback to user on website
typedef enum DumpReprocessStep
{
	DUMPREPROCESS_START = 0,
	DUMPREPROCESS_CALLSTACK, // intermediate step
	DUMPREPROCESS_CALLSTACK_DONE,
	DUMPREPROCESS_NEWENTRY, // intermediate step
	DUMPREPROCESS_NEWENTRY_DONE,
} DumpReprocessStep;

typedef struct DumpReprocessQueue
{
	U32 uID;
	U32 uLinkID;
	DumpReprocessStep eStep;
	U32 uOldEntryID;
	U32 uOldDumpIndex;
	bool newlyRecieved;

	int iNewEntryID;
	NOCONST(StackTraceLine) **ppLines;
	NOCONST(DumpData) *pDumpData;

	bool bProcessing;
} DumpReprocessQueue;

static DumpReprocessQueue **sppDumpReprocessing = NULL;

static void clearCDBFiles(void)
{
	static char cdbFileDir[MAX_PATH] = "";
	char **ppFiles;
	int i;

	if (!cdbFileDir[0])
	{
		char *fileDelim = cdbFileDir, *lastDelim = NULL;
		sprintf(cdbFileDir, "%s", errorTrackerGetSourceDataDir());
		backSlashes(cdbFileDir);
		while (fileDelim = strchr(fileDelim, '\\'))
		{
			lastDelim = fileDelim;
			fileDelim++;
		}
		if (!lastDelim)
			assertmsg(0, "CDB Directory is not properly set.");
		*lastDelim = '\0';
	}

	ppFiles = fileScanDirNoSubdirRecurse(cdbFileDir);
	for (i=0; i<eaSize(&ppFiles); i++)
	{
		if (strStartsWith(ppFiles[i], CDB_OUTPUT_FILE) || strStartsWith(ppFiles[i], CDB_TRASH_FILE))
			DeleteFile_UTF8(ppFiles[i]);
	}
}

static bool lineStartsWithHex(char *string)
{
	char *wordEnd = strchr(string, ' ');
	char *curChar;
	if (!wordEnd)
		return false;
	for (curChar = string; curChar < wordEnd; curChar++)
	{
		if (isalpha(*curChar))
		{
			char charLower = tolower(*curChar);
			if (charLower < 'a' || 'f' < charLower)
				return false;
		}
		else if (!isdigit(*curChar))
			return false;
	}
	return true;
}

static void reparseStackFromDump(NOCONST(StackTraceLine) ***eaStacklines, const char *filepath, U32 uID)
{
	char *command = NULL;
	bool bFullDump = false;
	char fullCDBPath[MAX_PATH];
	char fullTrashPath[MAX_PATH];

	sprintf(fullCDBPath, "%s%s-%d.txt", errorTrackerGetSourceDataDir(), CDB_OUTPUT_FILE, uID);
	sprintf(fullTrashPath, "%s%s-%d.txt", errorTrackerGetSourceDataDir(), CDB_TRASH_FILE, uID);
	eaDestroyStructNoConst(eaStacklines, parse_StackTraceLine);
	if (!fileExists(filepath))
	{
		if (gbETVerbose) printf("Could not find dump file.\n");
		return;
	}
	estrPrintf(&command, "%s%s %s %s > %s", errorTrackerGetSourceDataDir(), CDBProcessBatchFile, filepath, fullCDBPath, fullTrashPath);
	backSlashes(command);
	system(command);
	estrDestroy(&command);
	fileForceRemove(fullTrashPath);
	if (fileExists(fullCDBPath))
	{
		FILE * file = fopen(fullCDBPath, "r");
		if (file)
		{
			char linebuffer[1024];
			char *exePath = NULL;

			while (fgets(linebuffer, ARRAY_SIZE_CHECKED(linebuffer), file) )
			{
				char *substr;
				if (substr = strstri(linebuffer, "mini dump"))
				{
					if (strstri(linebuffer, "full memory"))
						bFullDump = true;
					else
						bFullDump = false;
				}
				else if (substr = strstri(linebuffer, "name:"))
				{
					substr += strlen("name:");
					estrCopy2(&exePath, substr);
					estrTrimLeadingAndTrailingWhitespace(&exePath);
					break;
				}
			}

			while (fgets(linebuffer, ARRAY_SIZE_CHECKED(linebuffer), file) )
			{
				char *x64check = strchr(linebuffer, '`');
				if (lineStartsWithHex(linebuffer) || (x64check && lineStartsWithHex(x64check+1))) // check for hex at start of line
				{
					// <stuff> <module>!<function>+<address> [<file name> @ <line #>]
					// file name + line # not always there
					char *split = strchr(linebuffer, '!');
					char *module, *funcName, *src = NULL;
					int iLineNum = 0;
					bool bNofunc = false;

					if (!exePath) // exePath should be before all modules! Complete fail here
						break;
					if (!split)
					{ // no function name
						split = strchr(linebuffer, '+');
						bNofunc = true;
						if (!split) continue; // failed to parse line
					}
					module = split;
					while (module > linebuffer && *(module-1) != ' ')
						module--;
					*split = '\0';
					funcName = split+1;
					if (!bNofunc) // otherwise funcname = rest of string = the hex address of func
					{
						split = strchr(funcName, '+');
						if (!split)
							continue;
						*split = '\0';
						split = strchr(split+1, '[');
						if (split)
						{
							src = split+1;
							split = strchr(src, '@');
							if (split)
							{
								char *lineString = split+1;
								*split = '\0';
								split = strchr(lineString, ']');
								if (split) *split = '\0';
								iLineNum = atoi (lineString);
							}
						}
					}

					// toss out everything we have so far; only keep stuff after the assert function
					// KiUserExceptionDispatcher is, as far as I can tell, a function used by the Windows Debugger
					if (strstri(funcName, "superassert") || strstri (funcName, "assertExcept") ||
						strstri(funcName, "KiUserExceptionDispatcher"))
					{
						eaDestroyStructNoConst(eaStacklines, parse_StackTraceLine);
					}
					else
					{
						NOCONST(StackTraceLine) *stackline = StructCreateNoConst(parse_StackTraceLine);
						if (strstri(exePath, module) == NULL)
							estrPrintf(&stackline->pModuleName, "%s.dll", module);
						else
							estrPrintf(&stackline->pModuleName, "%s.exe", module);
						if(bNofunc)
						{
							char *moduleOffset = NULL;
							if(strstri(funcName, "0x"))
								funcName = funcName+2;
							estrPrintf(&moduleOffset, "%s!%09s", module, funcName);
							estrCopy2(&stackline->pFunctionName, moduleOffset);
							estrDestroy(&moduleOffset);
						}
						else
							estrCopy2(&stackline->pFunctionName, funcName);
						estrTrimLeadingAndTrailingWhitespace(&stackline->pFunctionName);
						if (src)
						{
							estrCopy2(&stackline->pFilename, src);
							estrTrimLeadingAndTrailingWhitespace(&stackline->pFilename);
						}
						else
							estrCopy2(&stackline->pFilename, "???");
						stackline->iLineNum = iLineNum;
						eaPush(eaStacklines, stackline);
					}
				}
			}
			estrDestroy(&exePath);
			fclose(file);
		}
		else
			printf("Could not open CDB output file.\n");
		fileForceRemove(fullCDBPath);
	}
}

// uNewIndex should be the old index + 1 (0 is reserved for no index)
AUTO_TRANSACTION ATR_LOCKS(pEntry, ".ppDumpData");
enumTransactionOutcome trErrorEntry_RemoveDumpInfo(ATR_ARGS, NOCONST(ErrorEntry) *pEntry, int iIndex, U32 uNewID, U32 uNewIndex)
{
	StructDestroyNoConst(parse_ErrorEntry, pEntry->ppDumpData[iIndex]->pEntry);
	pEntry->ppDumpData[iIndex]->pEntry = NULL;
	pEntry->ppDumpData[iIndex]->uFlags |= DUMPDATAFLAGS_MOVED;
	pEntry->ppDumpData[iIndex]->uMovedID = uNewID;
	pEntry->ppDumpData[iIndex]->uMovedIndex = uNewIndex;
	return TRANSACTION_OUTCOME_SUCCESS;
}

void trNewEntryFromDump_CB(TransactionReturnVal *returnVal, DumpReprocessQueue *pQueue)
{
	EnterCriticalSection(&gDumpReprocessCritical);
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		ErrorEntry *pEntry;
		pQueue->iNewEntryID = atoi(returnVal->pBaseReturnVals->returnString);

		pEntry = findErrorTrackerByID(pQueue->iNewEntryID);
		if (pEntry)
		{
			pEntry->iDailyCount = pEntry->iTotalCount;
			pEntry->bIsNewEntry = true;	
			addEntryToStashTables(pEntry);
		}
		else
			printf("\n%s - Could not find new entry %d.\n", StaticDefineIntRevLookup(GlobalTypeEnum, GLOBALTYPE_ERRORTRACKERENTRY), 
				pQueue->iNewEntryID);
	}
	else
	{
		pQueue->iNewEntryID = -1;
	}
	pQueue->eStep = DUMPREPROCESS_NEWENTRY_DONE;
	LeaveCriticalSection(&gDumpReprocessCritical);
}

// Threaded function for unzipping dump file and running cdb on it to dump the callstack + other info
DWORD WINAPI DumpReprocess_FindStack(DumpReprocessQueue *pQueue)
{
	EXCEPTION_HANDLER_BEGIN
	{
		NOCONST(StackTraceLine) **ppLines = NULL;
		NOCONST(DumpData) *dump;
		char dumpPath[MAX_PATH];
		char *syscommand = NULL;
		char *dumpPathBase = NULL;
		int iDumpIndex = 0;

		EnterCriticalSection(&gDumpReprocessCritical);
		// Simple lock to avoid maintaining critical section for entire process
		pQueue->bProcessing = true;
		LeaveCriticalSection(&gDumpReprocessCritical);

		dump = pQueue->pDumpData;

		if ((dump->uFlags & DUMPDATAFLAGS_FULLDUMP) != 0 && !dump->bWritten)
			iDumpIndex = dump->iMiniDumpIndex;
		else
			iDumpIndex = dump->iDumpIndex;
		calcWriteDumpPath(dumpPath, ARRAY_SIZE_CHECKED(dumpPath), pQueue->uOldEntryID, iDumpIndex, 
			(dump->uFlags & DUMPDATAFLAGS_FULLDUMP) != 0 && dump->bWritten);
		estrCopy2(&dumpPathBase, dumpPath);
		estrReplaceOccurrences(&dumpPathBase, ".gz", ""); // remove the gzip file ext
		estrPrintf(&syscommand, "gzip -c -d %s > %s", dumpPath, dumpPathBase);
		system(syscommand);
		reparseStackFromDump(&ppLines, dumpPathBase, pQueue->uID);
		fileForceRemove(dumpPathBase);

		estrDestroy(&syscommand);
		estrDestroy(&dumpPathBase);

		// Crit section here
		pQueue->ppLines = ppLines;
		pQueue->eStep = DUMPREPROCESS_CALLSTACK_DONE;
		pQueue->bProcessing = false;
	}
	EXCEPTION_HANDLER_END
	return 0;
}

#include "HttpClient.h"
extern void httpSendWrappedString(NetLink *link, const char *pBody, const char *pTitle, CookieList *pList);
static void sendDumpReprocessResponse(U32 uLinkID, const char *msg)
{
	NetLink *link = FindClientLink(uLinkID);
	if (linkConnected(link))
	{
		httpSendWrappedString(link, msg, NULL, NULL);
	}
}

static void destroyDumpQueueStruct(DumpReprocessQueue *queue)
{
	if (queue->pDumpData)
		StructDestroyNoConst(parse_DumpData, queue->pDumpData);
	free(queue);
}

// Returns true if done (whether because of failure or done merging)
bool DumpReprocess_ProcessStack(DumpReprocessQueue *pQueue)
{
	NOCONST(ErrorEntry) *pDumpEntry;
	NOCONST(DumpData) *pDumpCopy;
	ErrorEntry *pEntry = findErrorTrackerByID(pQueue->uOldEntryID);
	DumpData *dump;
	
	pDumpCopy = pQueue->pDumpData;
	if (!pDumpCopy) return false;
	pDumpEntry = pDumpCopy->pEntry;
	if (!pDumpEntry || !pEntry) // no point doing anything here?
	{
		sendDumpReprocessResponse(pQueue->uLinkID, "ERROR: Could not copy Dump data.");
		return true;
	}
	dump = pEntry->ppDumpData[pQueue->uOldDumpIndex];

	// Destroy existing stack trace
	eaDestroyStructNoConst(&pDumpEntry->ppStackTraceLines, parse_StackTraceLine);
	pDumpEntry->ppStackTraceLines = pQueue->ppLines;
	pQueue->ppLines = NULL;
	if(ErrorEntry_isNullCallstack(CONTAINER_RECONST(ErrorEntry, pDumpEntry)))
	{
		sendDumpReprocessResponse(pQueue->uLinkID, 
			"Unable to create a useful callstack from this dump.<br>No action taken.");
		return true; // don't want to destroy any information we might already have
	}
	recalcUniqueID(pDumpEntry, ET_LATEST_HASH_VERSION);
	if (hashMatches(pEntry, CONST_ENTRY(pDumpEntry)))
	{
		sendDumpReprocessResponse(pQueue->uLinkID, 
			"Hash for new callstack matches current Error Entry.<br>No action taken.");
		return true; // dump still matches current entry; nothing happens
	}

	{
		ErrorEntry *pMergedEntry = findErrorTrackerEntryFromNewEntry(pDumpEntry);
		if (pMergedEntry != NULL)
		{
			char *redirect = NULL;
			U32 uNewIndex;
			if(pQueue->newlyRecieved)
			{
				NOCONST(ErrorEntry) *pDumpEntryCopy = StructCloneNoConst(parse_ErrorEntry, pDumpEntry);
				ErrorEntry_ProcessMerge(pQueue->uLinkID, pMergedEntry, pDumpEntryCopy); //ProcessMerge StructDestroys pDumpEntryCopy.
			}
			pDumpCopy->iDumpIndex = pDumpCopy->iMiniDumpIndex = eaSize(&pMergedEntry->ppDumpData);
			if (!pDumpCopy->uPreviousID) 
				pDumpCopy->uPreviousID = pEntry->uID;
			MoveDumpFiles(pEntry, dump, pMergedEntry, pDumpCopy);
			AutoTrans_trErrorEntry_AddDump(NULL, GLOBALTYPE_ERRORTRACKER, GLOBALTYPE_ERRORTRACKERENTRY, 
				pMergedEntry->uID, (DumpData*) pDumpCopy, true);

			uNewIndex = eaSize(&pMergedEntry->ppDumpData)-1;
			estrPrintf(&redirect, "Dump moved to <a href=\"/dumpinfo?id=%d&index=%d\">Error Entry #%d</a>.",
				pMergedEntry->uID, uNewIndex, pMergedEntry->uID);	
			sendDumpReprocessResponse(pQueue->uLinkID, redirect);
			estrDestroy(&redirect);

			// Change the attached dump entry to "MOVED" and remove the heavy data
			AutoTrans_trErrorEntry_RemoveDumpInfo(NULL, GLOBALTYPE_ERRORTRACKER, GLOBALTYPE_ERRORTRACKERENTRY, 
				pEntry->uID, pQueue->uOldDumpIndex, pMergedEntry->uID, uNewIndex+1);
		}
		else
		{
			if (ErrorEntry_IsGenericCrash(pEntry))
			{
				AutoTrans_trErrorEntry_RemoveHash(NULL, GLOBALTYPE_ERRORTRACKER, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID);
				if (pEntry->pGenericLabel)
					estrCopy2(&pDumpEntry->pGenericLabel, pEntry->pGenericLabel);
			}
			pDumpCopy->iDumpIndex = pDumpCopy->iMiniDumpIndex = 0;
			if (!pDumpCopy->uPreviousID) 
				pDumpCopy->uPreviousID = pEntry->uID;
			pDumpEntry->uID = 0;
			pDumpEntry->bFullDumpRequested = false;
			objRequestContainerCreateLocal(objCreateManagedReturnVal(trNewEntryFromDump_CB, pQueue),
				GLOBALTYPE_ERRORTRACKERENTRY, pDumpEntry);
			return false; // not done
		}
	}
	return true; // done
}

void ReprocessDumpData (ErrorEntry *pEntry, int iDumpDataIndex, NetLink *link, bool newEntry)
{
	static int sQueueID = 1;
	DumpReprocessQueue *pQueue = malloc(sizeof(DumpReprocessQueue));

	if (sQueueID == 1) clearCDBFiles();
	pQueue->uOldEntryID = pEntry->uID;
	pQueue->uOldDumpIndex = iDumpDataIndex;
	pQueue->pDumpData = StructCloneNoConst(parse_DumpData, CONTAINER_NOCONST(DumpData, pEntry->ppDumpData[iDumpDataIndex]));
	pQueue->ppLines = NULL;
	pQueue->iNewEntryID = 0;
	pQueue->eStep = DUMPREPROCESS_START;
	pQueue->uID = sQueueID++;
	pQueue->uLinkID = linkID(link);
	pQueue->newlyRecieved = newEntry;
	pQueue->bProcessing = false;
	AddClientLink(link);

	EnterCriticalSection(&gDumpReprocessCritical);
	eaPush(&sppDumpReprocessing, pQueue);
	LeaveCriticalSection(&gDumpReprocessCritical);
}

void ReprocessDumpQueue (void)
{
	int i;
	EnterCriticalSection(&gDumpReprocessCritical);
	for (i=eaSize(&sppDumpReprocessing)-1; i>=0; i--)
	{
		DumpReprocessQueue *pQueue = sppDumpReprocessing[i];
		if (pQueue->bProcessing)
			continue;
		switch (pQueue->eStep)
		{
		case DUMPREPROCESS_START:
			{
				DWORD id;
				CloseHandle ((HANDLE) _beginthreadex(NULL, 0, DumpReprocess_FindStack, pQueue, 0, &id));
				pQueue->eStep = DUMPREPROCESS_CALLSTACK;
			}
		xcase DUMPREPROCESS_CALLSTACK_DONE:
			{
				if (DumpReprocess_ProcessStack(pQueue))
				{
					destroyDumpQueueStruct(pQueue);
					eaRemove(&sppDumpReprocessing, i);
				}
				else pQueue->eStep = DUMPREPROCESS_NEWENTRY;
			}
		xcase DUMPREPROCESS_NEWENTRY_DONE:
			{
				if (pQueue->iNewEntryID > 0) // Wait for iNewEntryID to be changed from 0 value
				{
					char *redirect = NULL;
					ErrorEntry *pOldEntry = findErrorTrackerByID(pQueue->uOldEntryID);
					ErrorEntry *pNewEntry = findErrorTrackerByID(pQueue->iNewEntryID);

					devassert(pOldEntry && pNewEntry);
					ANALYSIS_ASSUME(pOldEntry != NULL);
					ANALYSIS_ASSUME(pNewEntry != NULL);
					MoveDumpFiles(pOldEntry, pOldEntry->ppDumpData[pQueue->uOldDumpIndex], pNewEntry, pQueue->pDumpData);
					AutoTrans_trErrorEntry_AddDump(NULL, GLOBALTYPE_ERRORTRACKER, GLOBALTYPE_ERRORTRACKERENTRY, 
						pNewEntry->uID, CONTAINER_RECONST(DumpData, pQueue->pDumpData), false);

					// Change the attached dump entry to "MOVED" and remove the heavy data
					AutoTrans_trErrorEntry_RemoveDumpInfo(NULL, GLOBALTYPE_ERRORTRACKER, GLOBALTYPE_ERRORTRACKERENTRY, 
						pOldEntry->uID, pQueue->uOldDumpIndex, pNewEntry->uID, 1);

					estrPrintf(&redirect, "Dump moved to new entry <a href=\"/detail?id=%d\">Error Entry #%d</a>.",
						pNewEntry->uID, pNewEntry->uID);
					sendDumpReprocessResponse(pQueue->uLinkID, redirect);
					estrDestroy(&redirect);
					destroyDumpQueueStruct(pQueue);
					eaRemove(&sppDumpReprocessing, i);
				}
				else if (pQueue->iNewEntryID == -1)
				{
					sendDumpReprocessResponse(pQueue->uLinkID, "ERROR: failed creating new entry from dump.");
					destroyDumpQueueStruct(pQueue);
					eaRemove(&sppDumpReprocessing, i);
					AssertOrAlert("EntryCreateFail", "Something bad happened... Could not create new entry\n");
				}
			}
		}
	}
	LeaveCriticalSection(&gDumpReprocessCritical);
}

void ErrorEntry_EditDumpDescription(ErrorEntry *pEntry, int iDumpIndex, const char *description)
{
	NOCONST(TriviaList) trivia = {0};
	NOCONST(TriviaData) *triviaDescription = StructCreateNoConst(parse_TriviaData);
	estrCopy2(&triviaDescription->pKey, "UserDescription");
	estrCopy2(&triviaDescription->pVal, description);
	eaPush(&trivia.triviaDatas, triviaDescription);
	AutoTrans_trErrorEntry_UpdateTriviaOverview(NULL, GLOBALTYPE_ERRORTRACKER, GLOBALTYPE_ERRORTRACKERENTRY, 
		pEntry->uID, (TriviaList*) &trivia);
	StructDeInitNoConst(parse_TriviaList, &trivia);
	// TODO is this logging everything?
	objRequestTransactionSimplef(NULL, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID, 
		"setDumpDescription", "set ppDumpData[%d].pDumpDescription = \"%s\"", 
		iDumpIndex, description);
}

AUTO_TRANSACTION ATR_LOCKS(pEntry, ".aiUniqueHash, .aiUniqueHashNew");
enumTransactionOutcome trErrorEntry_RemoveHash(ATR_ARGS, NOCONST(ErrorEntry) *pEntry)
{
	removeEntryFromStashTables(pEntry);
	pEntry->aiUniqueHash[0] = pEntry->aiUniqueHash[1] = pEntry->aiUniqueHash[2] = pEntry->aiUniqueHash[3] = 0;
	pEntry->aiUniqueHashNew[0] = pEntry->aiUniqueHashNew[1] = pEntry->aiUniqueHashNew[2] = pEntry->aiUniqueHashNew[3] = 0;
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(pEntry, ".*");
enumTransactionOutcome trErrorEntry_RecalcHash(ATR_ARGS, NOCONST(ErrorEntry) *pEntry)
{
	char *pCRCNewString = NULL;

	ET_ConstructHashString(&pCRCNewString, pEntry, true);

	// -------------------------------------------------------------------------------
	// Unique ID generation

	removeEntryFromStashTables(pEntry);

	strupr(pCRCNewString);
	cryptMD5(pCRCNewString, (int)strlen(pCRCNewString), pEntry->aiUniqueHashNew);

	addEntryToStashTables(CONST_ENTRY(pEntry));

	pEntry->uHashVersion = ET_LATEST_HASH_VERSION;
	estrDestroy(&pCRCNewString);
	return TRANSACTION_OUTCOME_SUCCESS;
}

static StashTable stErrorHash = NULL;
static ErrorEntry **ppZeroHashList = NULL;

ErrorEntry ** ErrorEntry_GetGenericCrashes(void)
{
	return ppZeroHashList;
}

bool ErrorEntry_isNullCallstack(ErrorEntry *pEntry)
{
	int i;
	for (i = 0; i < eaSize(&pEntry->ppStackTraceLines); i++)
	{
		const char *func = pEntry->ppStackTraceLines[i]->pFunctionName;
		const char *module = pEntry->ppStackTraceLines[i]->pModuleName;
		if(!(strstri_safe(func, "0x") || strstri_safe(func, "!")) && isCrypticModule(module))
			return false;
	}
	return true;
}

int ErrorEntry_firstValidStackFrame(ErrorEntry *pEntry)
{
	int i;
	for (i = 0; i < eaSize(&pEntry->ppStackTraceLines); i++)
	{
		const char *func = pEntry->ppStackTraceLines[i]->pFunctionName;
		const char *module = pEntry->ppStackTraceLines[i]->pModuleName;
		if(!(strstri_safe(func, "0x") || strstri_safe(func, "!")) && isCrypticModule(module))
			return i;
	}
	return 0;
}

bool ErrorEntry_AddHashStash(ErrorEntry *pEntry)
{
	if (!pEntry->pStashString)
		estrCopy2(&pEntry->pStashString, errorTrackerLibStringFromUniqueHash(pEntry));
	if (!stErrorHash)
		stErrorHash = stashTableCreateWithStringKeys(10000, StashDefault);
	if (stricmp(pEntry->pStashString, ET_ZERO_HASH_STRING) == 0)
		eaPushUnique(&ppZeroHashList, pEntry);
	else
		return stashAddPointer(stErrorHash, pEntry->pStashString, pEntry, false);
	return true;
}
void ErrorEntry_RemoveHashStash(ErrorEntry *pEntry)
{
	if (stErrorHash && pEntry->pStashString)
	{
		if (stricmp(pEntry->pStashString, ET_ZERO_HASH_STRING) == 0)
			eaFindAndRemove(&ppZeroHashList, pEntry);
		else
			stashRemovePointer(stErrorHash, pEntry->pStashString, NULL);
		estrDestroy(&pEntry->pStashString);
	}
}

bool ErrorEntry_IsGenericCrash(ErrorEntry *pEntry)
{
	if (!pEntry->pStashString)
		estrCopy2(&pEntry->pStashString, errorTrackerLibStringFromUniqueHash(pEntry));
	if (stricmp(pEntry->pStashString, ET_ZERO_HASH_STRING) == 0)
		return true;
	return false;
}

void ErrorEntry_MergeAndDeleteEntry (ErrorEntry *mergee, ErrorEntry *target, bool leaveStub)
{
	U32 uMergeTime = timeSecondsSince2000();
	AutoTrans_trErrorEntry_Merge (NULL, GLOBALTYPE_ERRORTRACKER, 
		GLOBALTYPE_ERRORTRACKERENTRY, target->uID, mergee, uMergeTime);
	AutoTrans_trErrorEntry_Merge2 (NULL, GLOBALTYPE_ERRORTRACKER, 
		GLOBALTYPE_ERRORTRACKERENTRY, target->uID, mergee, uMergeTime);
	target->iDailyCount += mergee->iTotalCount;

	if (leaveStub)
	{
		errorTrackerEntryCreateStub(mergee, target->uID);
	}
	else
	{
		errorTrackerEntryDelete(mergee, true);
	}
	
}

// Only for generic crashes
bool ErrorEntry_ForceHashRecalculate (ErrorEntry *pEntry)
{
	AutoTrans_trErrorEntry_RecalcHash(NULL, GLOBALTYPE_ERRORTRACKER, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID);
	if (errorTrackerIsGenericHash(pEntry, NULL))
	{
		AutoTrans_trErrorEntry_RemoveHash(NULL, GLOBALTYPE_ERRORTRACKER, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID);
		return false;
	}
	objRequestTransactionSimplef(NULL, GLOBALTYPE_ERRORTRACKERENTRY, pEntry->uID, 
		"clearGenericLabel", "destroy pGenericLabel");
	return true;
}


AUTO_TRANSACTION ATR_LOCKS(pEntry, ".ppExecutableNames, .ppExecutableCounts");
enumTransactionOutcome trErrorEntry_RemoveNullExecutables(ATR_ARGS, NOCONST(ErrorEntry) *pEntry)
{
	int idx;
	do 
	{
		idx = eaFind(&pEntry->ppExecutableNames, NULL);
		if (idx != -1)
		{
			eaRemove(&pEntry->ppExecutableNames, idx);
			eaiRemove(&pEntry->ppExecutableCounts, idx);
		}
	} while (idx != -1);
	return TRANSACTION_OUTCOME_SUCCESS;
}


#include "AutoGen/ErrorEntry_h_ast.c"
