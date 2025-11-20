/***************************************************************************



***************************************************************************/

#include "LocalTransactionManager_internal.h"

#include "logging.h"
#include "ServerLib.h"
#include "timing.h"
#include "wininclude.h"
#include "serverlib_h_ast.h"
#include "StashTable.h"
#include "UtilitiesLib.h"
#include "objects/objTransactionCommands.h"
#include "objTransactions.h"
#include "StringCache.h"
#include "ControllerLink.h"
#include "alerts.h"
#include "file.h"
#include "TransactionSystem_h_ast.h"
#include "LocalTransactionManager_c_ast.h"
#include "LocalTransactionManager_Internal_h_ast.h"
#include "resourceInfo.h"
#include "GlobalStateMachine.h"
#include "autogen/GlobalComm_h_ast.h"

static bool gbLTMLogging = false;
U32 giLagOnTransact = 0;
static bool sbLTMLinkDebug = false;

AUTO_CMD_INT(gbLTMLogging, LTMLogging);
AUTO_CMD_INT(giLagOnTransact, LagOnTransact) ACMD_COMMANDLINE;

// Turn on debugging and verification code for the Local Transaction Manager link, if we're not using a multiplexer.
AUTO_CMD_INT(sbLTMLinkDebug, LTMLinkDebug) ACMD_COMMANDLINE;

// If true, force the transaction link to be compressed.
// If false, request transaction link be uncompressed.
// Otherwise, if it's -1, do some default server-dependent behavior.
int giCompressTransactionLink = -1;
AUTO_CMD_INT(giCompressTransactionLink, CompressTransactionLink);
AUTO_CMD_INT(giCompressTransactionLink, gbCompressTransactionLink) ACMD_HIDE;  // Legacy name

// Debugging: If set, take 
bool sbSignalReplay = false;
AUTO_CMD_INT(sbSignalReplay, SignalReplay) ACMD_COMMANDLINE ACMD_CATEGORY(Debug);

SlowTransactionInfo *GetEmptySlowTransactionInfo(LocalTransactionManager *pManager);
SlowTransactionInfo *GetAndReleaseSlowTransactionInfo(LocalTransactionManager *pManager, int iID);
void ReportHandleCacheOverflow(const char *pTransactionName);

int HandshakeResultCB(Packet *pak, int cmd, NetLink *link, void *pUserData);

void PromoteLocalTransaction(LocalTransactionManager *pManager, LocalTransaction *pTransaction);

//do lots of logging of what's going on with local transaction manager
bool gbLTMVerboseLogEverything = false;
AUTO_CMD_INT(gbLTMVerboseLogEverything, LTMVerboseLogEverything);


Packet *CreateLTMPacket(LocalTransactionManager *pManager, int iCmd, PacketTracker *pTracker)
{
	if (pManager->pNetLink)
	{
		return pktCreateWithTracker(pManager->pNetLink, iCmd, pTracker);
	}
	else
	{
		//during server shutdown this can possibly be NULL, might as well just do nothing rather than
		//returning a NULL packet which will crash a billion places
		Packet *pRetVal = CreateLinkToMultiplexerPacket(pManager->pMultiplexLink, MULTIPLEX_CONST_ID_TRANSACTION_SERVER, iCmd, pTracker);

		if (!pRetVal)
		{
			pRetVal = pktCreateTemp(NULL);
		}

		return pRetVal;
	}
}


//extraData is the handle cache ID for TRANSACTIONPOSSIBLE and the trans id causing the block for TRANSACTION_BLOCKED
void SimpleMessageToServer(LocalTransactionManager *pManager, int eMessageType, TransactionID iTransID, int iTransIndex, U32 extraData, 
	char *pReturnValString, TransDataBlock *pDBUpdateData, char *pTransServerUpdateString)
{

	Packet *pPacket;
	PERFINFO_AUTO_START("SimpleMessageToServer",1);

	CreateLTMPacketWithFunctionNameTracker(pPacket, pManager, eMessageType);

	PutTransactionIDIntoPacket(pPacket, iTransID);
	pktSendBitsPack(pPacket, 1, iTransIndex);

	if (gbLTMLogging)
	{
		LTM_LOG("Sending message %s to server for trans %u\n", 
			StaticDefineIntRevLookup(TransClientPacketTypeEnum, eMessageType), iTransID);
	}

	if (eMessageType == TRANSCLIENT_TRANSACTIONFAILED || eMessageType == TRANSCLIENT_TRANSACTIONSUCCEEDED || eMessageType == TRANSCLIENT_TRANSACTIONPOSSIBLE || eMessageType == TRANSCLIENT_TRANSACTIONPOSSIBLEANDCONFIRMED)
	{
		if (pReturnValString && pReturnValString[0])
		{
			pktSendBits(pPacket, 1, 1);

			pktSendString(pPacket, pReturnValString);
		}
		else
		{
			pktSendBits(pPacket, 1, 0);
		}
	}
	else
	{
		assert(pReturnValString == NULL);
	}

	if (eMessageType == TRANSCLIENT_TRANSACTIONSUCCEEDED || eMessageType == TRANSCLIENT_TRANSACTIONPOSSIBLE || eMessageType == TRANSCLIENT_TRANSACTIONPOSSIBLEANDCONFIRMED)
	{
		PutTransDataBlockIntoPacket(pPacket, pDBUpdateData);

		if (pTransServerUpdateString && pTransServerUpdateString[0])
		{
			pktSendBits(pPacket, 1, 1);
			pktSendString(pPacket, pTransServerUpdateString);
		}
		else
		{
			pktSendBits(pPacket, 1, 0);
		}
	}
	else
	{
		assert(TransDataBlockIsEmpty(pDBUpdateData) && 
			(pTransServerUpdateString == NULL || pTransServerUpdateString[0] == '\0'));
	}

	if (eMessageType == TRANSCLIENT_TRANSACTIONPOSSIBLE)
	{
		pktSendBits(pPacket, 32, extraData);
	}
	else
	{
		PutTransactionIDIntoPacket(pPacket, extraData);
	}

	pktSend(&pPacket);
	PERFINFO_AUTO_STOP();
}

void ReleaseAllTransVariables(LocalTransactionManager *pManager)
{
	if (pManager->transVariableTable)
	{
		DestroyNameTable(pManager->transVariableTable);
		pManager->transVariableTable = NULL;
	}
}

void ReleaseEverything(LocalTransactionManager *pManager, int eObjType, LTMObjectFieldsHandle objFieldsHandle,
	LTMProcessedTransactionHandle processedTransactionHandle, char **ppReturnString, char **ppTransactString)
{
	PERFINFO_AUTO_START("ReleaseEverything",1);
	if (ppTransactString)
	{
		estrDestroy(ppTransactString);
	}

	if (pManager->pReleaseStringCB)
	{
		if (ppReturnString)
		{
			pManager->pReleaseStringCB(eObjType, *ppReturnString, pManager->pCBUserData);
		}
	}

	if (ppReturnString)
	{
		*ppReturnString = NULL;
	}

	if (pManager->pReleaseObjectFieldsHandleCB)
	{
		pManager->pReleaseObjectFieldsHandleCB(eObjType, objFieldsHandle, pManager->pCBUserData);
	}

	if (pManager->pReleaseProcessedTransactionHandleCB)
	{
		pManager->pReleaseProcessedTransactionHandleCB(eObjType, processedTransactionHandle, pManager->pCBUserData);
	}

	ReleaseAllTransVariables(pManager);
	pManager->iCurrentlyActiveTransaction = 0;
	pManager->bDoingRemoteTransaction = false;
	PERFINFO_AUTO_STOP();
}

void LTM_ExitOrShutdownCallback(LocalTransactionManager *pManager)
{
	if ((void*)pManager->pGracefulShutdownCB > (void*)0x01)
	{
		pManager->pGracefulShutdownCB();
		pManager->pGracefulShutdownCB = (LTMCallback_GracefulShutdown*)0x01;
	}
	else if (pManager->pGracefulShutdownCB == NULL && !pManager->bDestroying)
	{
		svrExit(-1);
	}
}


void SetLocalTransactionManagerShutdownCB(LocalTransactionManager *pLTM, LTMCallback_GracefulShutdown *pCB)
{
	pLTM->pGracefulShutdownCB = pCB;
}

#define HANDLE_CACHE_OVERFLOW_REPORT_INTERVAL 600

static CRITICAL_SECTION handleCacheCS = {0};
static int iFirstFreeHandleCacheIndex;
TransactionHandleCache **ppHandleCaches;
static int iNumHandleCachesInUse;

int GetNumberOfInUseHandleCaches(void)
{
	// This read does not need to enter the critical section because it is for reporting purposes only
	// and we do not need perfect accuracy. Integer writes are atomic so we should never get garbage back.
	return iNumHandleCachesInUse;
}

void InitializeHandleCache(void)
{
	InitializeCriticalSection(&handleCacheCS);
	iFirstFreeHandleCacheIndex = -1;
	ppHandleCaches = NULL;
	iNumHandleCachesInUse = 0;
}

bool HandleCacheIsFull(void)
{
	return iFirstFreeHandleCacheIndex == -1 && eaSize(&ppHandleCaches) == MAX_HANDLE_CACHES;
}

void GetHandleCacheCacheCountString(char **ppCountString)
{
	PointerCounter *pCounter = PointerCounter_Create();
	PointerCounterResult **ppResults = NULL;
	int i;
	
	EnterCriticalSection(&handleCacheCS);
	for (i=0; i < eaSize(&ppHandleCaches); i++)
	{
		if (ppHandleCaches[i]->iNextFree == -2)
		{
			PointerCounter_AddSome(pCounter, ppHandleCaches[i]->pTransName, 1);
		}
	}

	PointerCounter_GetMostCommon(pCounter, 5, &ppResults);
	PointerCounter_Destroy(&pCounter);

	for (i=0; i < eaSize(&ppResults); i++)
	{
		estrConcatf(ppCountString, "%d occurrence%s of \"%s\". ",
			ppResults[i]->iCount, ppResults[i]->iCount > 1 ? "s" : "", 
			ppResults[i]->pPtr ? (char*)ppResults[i]->pPtr : "(void)");
	}

	eaDestroyEx(&ppResults, NULL);
	LeaveCriticalSection(&handleCacheCS);
}

void ReportHandleCacheOverflow(const char *pTransactionName)
{
	char *pCountString = NULL;
	static U32 iLastReportTime = 0;

	if (isDevelopmentMode())
	{
		GetHandleCacheCacheCountString(&pCountString);

		assertmsgf(0, "More than %d simultaneous local transactions on %s. (This will be an alert in prod mode). Most common transactions: %s",
			MAX_HANDLE_CACHES, GlobalTypeAndIDToString(GetAppGlobalType(), GetAppGlobalID()), pCountString);
	}
	else
	{
		if (iLastReportTime < timeSecondsSince2000() - 600)
		{
			iLastReportTime = timeSecondsSince2000();
			GetHandleCacheCacheCountString(&pCountString);

			TriggerAlertf("HANDLE_CACHE_OVERFLOW", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, 0, 0, 0, 0, NULL, 0,
				"More than %d simultaneous local transactions on %s. This is causing a <%s> to be blocked, and will cause VERY bad performance until remedied. This will presumably be happening a LOT but will not be reported for another %d seconds. Most common transactions: %s",
				MAX_HANDLE_CACHES, GlobalTypeAndIDToString(GetAppGlobalType(), GetAppGlobalID()), pTransactionName, HANDLE_CACHE_OVERFLOW_REPORT_INTERVAL, pCountString);
		}
	}
}

// This should be used when you need a new HandleCache for a transaction
TransactionHandleCache *AcquireNewHandleCache(GlobalType eObjType, const char *pTransactionName)
{
	int iCount = 0;

	TransactionHandleCache *pCache;

	int iCurVerificationValue;

	if (HandleCacheIsFull())
	{
		ReportHandleCacheOverflow(pTransactionName);
		return NULL;
	}

	EnterCriticalSection(&handleCacheCS);
	if (iFirstFreeHandleCacheIndex != -1)
	{
		pCache = ppHandleCaches[iFirstFreeHandleCacheIndex];
		iFirstFreeHandleCacheIndex = pCache->iNextFree;
	}
	else
	{
		pCache = calloc(sizeof(TransactionHandleCache), 1);
		eaPush(&ppHandleCaches, pCache);
		pCache->iID = eaSize(&ppHandleCaches) - 1;
	}

	iNumHandleCachesInUse++;

	iCurVerificationValue = pCache->iID >> HANDLE_CACHE_INDEX_BITS;
	iCurVerificationValue = (iCurVerificationValue + 1) & ((1 << HANDLE_CACHE_VERIFICATION_BITS) - 1);
	pCache->iID = (pCache->iID & ((1 << HANDLE_CACHE_INDEX_BITS) - 1)) | (iCurVerificationValue << HANDLE_CACHE_INDEX_BITS);

	pCache->eObjType = eObjType;
	pCache->pTransName = pTransactionName;

	pCache->iNextFree = -2;
	LeaveCriticalSection(&handleCacheCS);
	return pCache;
}

// FindExistingHandleCache and ReleaseHandleCache should be used in the threaded environment. 
// This makes sure that we can release on the background thread after we are finished.
// GetAndReleaseHandleCache can be used in a single threaded transaction environment.

TransactionHandleCache *FindExistingHandleCache(int iID)
{
	TransactionHandleCache *pCache;

	int iIndex = iID & ((1 << HANDLE_CACHE_INDEX_BITS) - 1);

	EnterCriticalSection(&handleCacheCS);

	assertmsg(iIndex < eaSize(&ppHandleCaches), "Cache ID corruption");
	assert(ppHandleCaches);


	pCache = ppHandleCaches[iIndex];

	assertmsg(pCache->iID == iID, "Cache ID corruption");

	LeaveCriticalSection(&handleCacheCS); // Separate get/release
	return pCache;
}

// Multithreaded version
void ReleaseHandleCache(TransactionHandleCache **ppCache)
{
	TransactionHandleCache *pCache;
	int iIndex;
	if(!ppCache || !*ppCache)
		return;

	pCache = *ppCache;

	iIndex = pCache->iID & ((1 << HANDLE_CACHE_INDEX_BITS) - 1);

	EnterCriticalSection(&handleCacheCS);

	assertmsg(iIndex < eaSize(&ppHandleCaches), "Cache ID corruption");
	assert(ppHandleCaches);

	assertmsg(pCache == ppHandleCaches[iIndex], "Cache corruption");

	pCache->pTransName = NULL;
	pCache->iNextFree = iFirstFreeHandleCacheIndex;
	iFirstFreeHandleCacheIndex = iIndex;
	iNumHandleCachesInUse--;
	LeaveCriticalSection(&handleCacheCS); // Separate get/release
	*ppCache = NULL;
}

// Single Threaded
TransactionHandleCache *GetAndReleaseHandleCache(int iID)
{
	TransactionHandleCache *pCache;

	int iIndex = iID & ((1 << HANDLE_CACHE_INDEX_BITS) - 1);

	EnterCriticalSection(&handleCacheCS);

	assertmsg(iIndex < eaSize(&ppHandleCaches), "Cache ID corruption");
	assert(ppHandleCaches);


	pCache = ppHandleCaches[iIndex];

	assertmsg(pCache->iID == iID, "Cache ID corruption");

	pCache->pTransName = NULL;
	pCache->iNextFree = iFirstFreeHandleCacheIndex;
	iFirstFreeHandleCacheIndex = iIndex;
	iNumHandleCachesInUse--;
	LeaveCriticalSection(&handleCacheCS);
	return pCache;
}

AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "Calls, Ticks, AverageTicks, ForegroundTicks, AverageForegroundTicks, BackgroundTicks, AverageBackgroundTicks, QueueTicks, AverageQueueTicks, Completed, HandleCacheFull, ObjDoesntExist,TransInvalid,FieldsLocked,TooManySlow");
typedef struct HandleTransactionTracker
{
	const char *pTransName; AST(POOL_STRING)
	U64 iCalls;
	U64 iTicks;

	int AverageTicks;

	U64 iForegroundTicks;
	int AverageForegroundTicks;

	U64 iBackgroundTicks;
	int AverageBackgroundTicks;

	U64 iQueueTicks;
	int AverageQueueTicks;

	U64 iCompleted[OUTCOME_COUNT]; AST(INDEX(OUTCOME_COMPLETED, Completed) INDEX(OUTCOME_HANDLE_CACHE_FULL, HandleCacheFull) INDEX(OUTCOME_OBJ_DOESNT_EXIST, ObjDoesntExist) INDEX(OUTCOME_TRANS_INVALID, TransInvalid) INDEX(OUTCOME_FIELDS_LOCKED, FieldsLocked) INDEX(OUTCOME_TOO_MANY_SLOW, TooManySlow))

} HandleTransactionTracker;

AUTO_FIXUPFUNC;
TextParserResult fixupHandleTransactionTracker(HandleTransactionTracker* pTracker, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_ABOUT_TO_BE_SERVERMONITORED:
		pTracker->AverageTicks = pTracker->iTicks / pTracker->iCalls;
		pTracker->AverageForegroundTicks = pTracker->iForegroundTicks / pTracker->iCalls;
		pTracker->AverageBackgroundTicks = pTracker->iBackgroundTicks / pTracker->iCalls;
		pTracker->AverageQueueTicks = pTracker->iQueueTicks / pTracker->iCalls;
		break;
	}

	return 1;
}

StashTable sHandleTransactionTrackersByName = NULL;
CRITICAL_SECTION csHandleTransactionTrackers;

AUTO_RUN;
void HandleTransactionTrackerInit(void)
{
	sHandleTransactionTrackersByName = stashTableCreateAddress(16);
	resRegisterDictionaryForStashTable("HandleNewTransaction_Calls", RESCATEGORY_SYSTEM, 0, sHandleTransactionTrackersByName, parse_HandleTransactionTracker);
	InitializeCriticalSection(&csHandleTransactionTrackers);
}

// This should only ever be called from the main thread
// If this is called from a background thread and the main thread tries to server monitor it, 
// we can either have a problem with stash table resizing, or with iCalls being 0. If iCalls is zero,
// the ServerMonitor callback on HandleTransactionTracker will divide by 0 and crash.
void AddHandleNewTransactionTiming(const char *pTransName, U64 iStartingTicks, HandleNewTransOutcome eOutcome)
{
	HandleTransactionTracker *pTracker;
	U64 iNowTicks;
	GET_CPU_TICKS_64(iNowTicks);
	
	EnterCriticalSection(&csHandleTransactionTrackers);
	if (!stashFindPointer(sHandleTransactionTrackersByName, pTransName, &pTracker))
	{
		pTracker = StructCreate(parse_HandleTransactionTracker);
		pTracker->pTransName = pTransName;
		stashAddPointer(sHandleTransactionTrackersByName, pTransName, pTracker, false);
	}
	LeaveCriticalSection(&csHandleTransactionTrackers);

	pTracker->iCalls++;
	pTracker->iTicks += iNowTicks - iStartingTicks;
	pTracker->iCompleted[eOutcome]++;
}

// This should only ever be called from the main thread
// If this is called from a background thread and the main thread tries to server monitor it, 
// we can either have a problem with stash table resizing, or with iCalls being 0. If iCalls is zero,
// the ServerMonitor callback on HandleTransactionTracker will divide by 0 and crash.
void AddHandleNewTransactionThreadTiming(const char *pTransName, U64 iForegroundTicks, U64 iBackgroundTicks, U64 iQueueTicks, HandleNewTransOutcome eOutcome)
{
	HandleTransactionTracker *pTracker;
	U64 iNowTicks;
	GET_CPU_TICKS_64(iNowTicks);
	
	EnterCriticalSection(&csHandleTransactionTrackers);
	if (!stashFindPointer(sHandleTransactionTrackersByName, pTransName, &pTracker))
	{
		pTracker = StructCreate(parse_HandleTransactionTracker);
		pTracker->pTransName = pTransName;
		stashAddPointer(sHandleTransactionTrackersByName, pTransName, pTracker, false);
	}
	LeaveCriticalSection(&csHandleTransactionTrackers);

	pTracker->iCalls++;
	pTracker->iTicks += iForegroundTicks + iBackgroundTicks + iQueueTicks;
	pTracker->iForegroundTicks += iForegroundTicks;
	pTracker->iBackgroundTicks += iBackgroundTicks;
	pTracker->iQueueTicks += iQueueTicks;
	pTracker->iCompleted[eOutcome]++;
}

void HandleNewTransaction(LocalTransactionManager *pManager, Packet *pPacket)
{
	U64 iStartingTicks;
	PERFINFO_AUTO_START("HandleNewTransaction",1);
	assert(pManager->iThreadID == GetCurrentThreadId());
	GET_CPU_TICKS_64(iStartingTicks);
	{	
	char *pTransactionString = NULL;
	char *pReturnString = NULL;
	char **ppReturnString;	

	LTMObjectHandle objHandle = 0;
	LTMObjectFieldsHandle objFieldsHandle = 0;
	LTMProcessedTransactionHandle processedTransactionHandle = 0;

	bool bWantsReturn = pktGetBits(pPacket, 1);
	bool bRequiresConfirm = pktGetBits(pPacket, 1);

	//only applicable if bRequiresConfirm is set (ie, this is an atomic transaction). This means that
	//there is only one base transaction and no possibility of unrolling, so if you succeed you can confirm
	//immediately.
	bool bSucceedAndConfirmIsOK = pktGetBits(pPacket, 1);

	TransactionID iTransID = pManager->iCurrentlyActiveTransaction = GetTransactionIDFromPacket(pPacket);
	const char *pTransactionName = allocAddString(pktGetStringTemp(pPacket));

	int iTransIndex = pktGetBitsPack(pPacket, 1);
	int eRecipientType = GetContainerTypeFromPacket(pPacket);

	int iRecipientID = GetContainerIDFromPacket(pPacket);

	bool bTransactionIsSlow = false;

	TransactionID iTransIDCausingBlock = 0;




	if (bRequiresConfirm)
	{
		if (HandleCacheIsFull())
		{
			ReportHandleCacheOverflow(pTransactionName);
			SimpleMessageToServer(pManager, TRANSCLIENT_TRANSACTIONBLOCKED, iTransID, iTransIndex, 0, NULL, NULL, NULL);
			
			AddHandleNewTransactionTiming(pTransactionName, iStartingTicks, OUTCOME_HANDLE_CACHE_FULL);
			
			PERFINFO_AUTO_STOP();
			return;
		}
	}


	pManager->bDoingRemoteTransaction = true;
	strcpy(pManager->currentTransactionName, pTransactionName);

	if (gbLTMLogging)
	{
		LTM_LOG("Got HandleNewTransaction, trans ID %u (%s)\n", iTransID, pTransactionName);
	}

	estrAppendFromPacket(&pTransactionString,pPacket);

	if (pktGetBits(pPacket, 1))
	{
		assert(pManager->transVariableTable == NULL);
		pManager->transVariableTable = CreateNameTable(pPacket);
	}


	ppReturnString = bWantsReturn ? &pReturnString : NULL;

	pManager->averageBaseTransactionSize = (pManager->averageBaseTransactionSize * pManager->totalBaseTransactions + pktGetSize(pPacket)) / (pManager->totalBaseTransactions + 1);
	pManager->totalBaseTransactions++;

	if (!pManager->pDoesObjectExistCB(eRecipientType, iRecipientID, &objHandle, ppReturnString, pManager->pCBUserData))
	{
		SimpleMessageToServer(pManager, TRANSCLIENT_TRANSACTIONFAILED, iTransID, iTransIndex, 0, pReturnString, NULL, NULL);

		ReleaseEverything(pManager, eRecipientType, objFieldsHandle, processedTransactionHandle, &pReturnString, &pTransactionString);
		AddHandleNewTransactionTiming(pTransactionName, iStartingTicks, OUTCOME_OBJ_DOESNT_EXIST);
		PERFINFO_AUTO_STOP();
		return;
	}

	if (pManager->pPreProcessTransactionStringCB)
	{
		enumTransactionValidity eValidity = pManager->pPreProcessTransactionStringCB(eRecipientType, pTransactionString, &processedTransactionHandle, &objFieldsHandle,
			ppReturnString, iTransID, pTransactionName, pManager->pCBUserData);

		switch (eValidity)
		{
		case TRANSACTION_INVALID:
			SimpleMessageToServer(pManager, TRANSCLIENT_TRANSACTIONFAILED, iTransID, iTransIndex, 0, pReturnString, NULL, NULL);

			ReleaseEverything(pManager, eRecipientType, objFieldsHandle, processedTransactionHandle, &pReturnString, &pTransactionString);
			AddHandleNewTransactionTiming(pTransactionName, iStartingTicks, OUTCOME_TRANS_INVALID);
			PERFINFO_AUTO_STOP();
			return;

		case TRANSACTION_VALID_SLOW:
			bTransactionIsSlow = true;
			break;
		}
	}

	

	if (!pManager->pAreFieldsOKToBeLockedCB(eRecipientType, objHandle, pTransactionString, objFieldsHandle, iTransID,  pTransactionName, pManager->pCBUserData, &iTransIDCausingBlock))
	{
		SimpleMessageToServer(pManager, TRANSCLIENT_TRANSACTIONBLOCKED, iTransID, iTransIndex, iTransIDCausingBlock, NULL, NULL, NULL);

		ReleaseEverything(pManager, eRecipientType, objFieldsHandle, processedTransactionHandle, &pReturnString, &pTransactionString);
		AddHandleNewTransactionTiming(pTransactionName, iStartingTicks, OUTCOME_FIELDS_LOCKED);
		PERFINFO_AUTO_STOP();
		return;
	}

	if (bTransactionIsSlow)
	{
		TransactionHandleCache *pHandleCache = NULL;
		SlowTransactionInfo *pSlowTransInfo = GetEmptySlowTransactionInfo(pManager);

		if (!pSlowTransInfo)
		{
			AssertOrAlert("TOO_MANY_SLOW_TRANS", "Too many slow transactions... blocking a %s transaction. This is going to clobber performance",
				pTransactionName);

			SimpleMessageToServer(pManager, TRANSCLIENT_TRANSACTIONBLOCKED, iTransID, iTransIndex, 0, NULL, NULL, NULL);

			ReleaseEverything(pManager, eRecipientType, objFieldsHandle, processedTransactionHandle, &pReturnString, &pTransactionString);
			AddHandleNewTransactionTiming(pTransactionName, iStartingTicks, OUTCOME_TOO_MANY_SLOW);
			PERFINFO_AUTO_STOP();
			return;
		}


		if (bRequiresConfirm)
		{
			pHandleCache = AcquireNewHandleCache(eRecipientType, pTransactionName);

			assertmsgf(pHandleCache, "Something went wrong. No Free handle caches. This case should already have been caught and TRANS_BLOCKED returned. Talk to Alex.");

			pHandleCache->objFieldsHandle = objFieldsHandle;
			pHandleCache->objHandle = objHandle;
		}

		pSlowTransInfo->bRequiresConfirm = bRequiresConfirm;
		pSlowTransInfo->bSucceedAndConfirmIsOK = bSucceedAndConfirmIsOK;
		pSlowTransInfo->iHandleCacheID = bRequiresConfirm ? pHandleCache->iID : -1;
		pSlowTransInfo->iTransID = iTransID;
		pSlowTransInfo->pTransactionName = pTransactionName;
		pSlowTransInfo->iTransIndex = iTransIndex;
		pSlowTransInfo->eObjType = eRecipientType;

		pSlowTransInfo->objFieldsHandle = objFieldsHandle;
		pSlowTransInfo->processedTransHandle = processedTransactionHandle;

		pSlowTransInfo->transVariableTable = pManager->transVariableTable;

		strcpy_trunc(pSlowTransInfo->dbgTransString, pTransactionString);

		pManager->transVariableTable = NULL;

		pManager->iCurrentlyActiveTransaction = 0;

		assert(pManager->pBeginSlowTransactionCB);

		pManager->pBeginSlowTransactionCB(eRecipientType, objHandle, bRequiresConfirm, pTransactionString,
			processedTransactionHandle, objFieldsHandle, iTransID,  pTransactionName, pSlowTransInfo->iID, pManager->pCBUserData);


		if (pManager->pReleaseStringCB)
		{
			pManager->pReleaseStringCB(eRecipientType, pReturnString, pManager->pCBUserData);
		}
		pReturnString = NULL;

	

		

		estrDestroy(&pTransactionString);

	}
	else
	{
		if (bRequiresConfirm)
		{
			if (pManager->pCanTransactionBeDoneCB(eRecipientType, objHandle, pTransactionString, processedTransactionHandle, objFieldsHandle,
				ppReturnString, iTransID,  pTransactionName, pManager->pCBUserData))
			{
				TransDataBlock dbUpdateData = {0};
				char *pTransServerUpdateString = NULL;
				TransactionHandleCache *pHandleCache;

				pManager->pBeginLockCB(eRecipientType, objHandle, objFieldsHandle, iTransID,  pTransactionName, pManager->pCBUserData);

				if (pManager->pApplyTransactionCB(eRecipientType, objHandle, pTransactionString, processedTransactionHandle, objFieldsHandle,
					ppReturnString, &dbUpdateData, &pTransServerUpdateString, iTransID,  pTransactionName, pManager->pCBUserData))
				{
					//for atomic transactions with only one step, we can do the succeed and confirm all at once
					if (bSucceedAndConfirmIsOK)
					{
						SimpleMessageToServer(pManager, TRANSCLIENT_TRANSACTIONPOSSIBLEANDCONFIRMED, iTransID, iTransIndex,
							0, pReturnString, &dbUpdateData, pTransServerUpdateString);

						pManager->pCommitAndReleaseLockCB(eRecipientType, objHandle, objFieldsHandle, iTransID, pManager->pCBUserData);

						ReleaseEverything(pManager, eRecipientType, objFieldsHandle, processedTransactionHandle, &pReturnString, &pTransactionString);
					
						if (pManager->pReleaseStringCB)
						{
							pManager->pReleaseStringCB(eRecipientType, pTransServerUpdateString, pManager->pCBUserData);
						}

						if (pManager->pReleaseDataBlockCB)
						{
							pManager->pReleaseDataBlockCB(eRecipientType, &dbUpdateData, pManager->pCBUserData);
						}
	
					}
					else
					{
						pHandleCache = AcquireNewHandleCache(eRecipientType, pTransactionName);
						assertmsgf(pHandleCache, "Something went wrong. No Free handle caches. This case should already have been caught and TRANS_BLOCKED returned. Talk to Alex.");

						pHandleCache->objFieldsHandle = objFieldsHandle;
						pHandleCache->objHandle = objHandle;

						SimpleMessageToServer(pManager, TRANSCLIENT_TRANSACTIONPOSSIBLE, iTransID, iTransIndex, pHandleCache->iID, pReturnString,
							&dbUpdateData, pTransServerUpdateString);


						//NOT a release everything, because the obj fields handle is kept to make releasing blocks fast
						if (pManager->pReleaseStringCB)
						{
							pManager->pReleaseStringCB(eRecipientType, pReturnString, pManager->pCBUserData);
							pManager->pReleaseStringCB(eRecipientType, pTransServerUpdateString, pManager->pCBUserData);
						}

						if (pManager->pReleaseDataBlockCB)
						{
							pManager->pReleaseDataBlockCB(eRecipientType, &dbUpdateData, pManager->pCBUserData);
						}

						pReturnString = NULL;

						if (pManager->pReleaseProcessedTransactionHandleCB)
						{
							pManager->pReleaseProcessedTransactionHandleCB(eRecipientType, processedTransactionHandle, pManager->pCBUserData);
						}

						ReleaseAllTransVariables(pManager);
						pManager->iCurrentlyActiveTransaction = 0;
						estrDestroy(&pTransactionString);
					}
				}
				else
				{
					assertmsg(TransDataBlockIsEmpty(&dbUpdateData) && 
						(!pTransServerUpdateString || !pTransServerUpdateString[0]), 
						"Update strings can not be generated when a transaction fails");

					pManager->pUndoLockCB(eRecipientType, objHandle, objFieldsHandle, iTransID, pManager->pCBUserData);

					SimpleMessageToServer(pManager, TRANSCLIENT_TRANSACTIONFAILED, iTransID, iTransIndex, 0, pReturnString, NULL, NULL);

					ReleaseEverything(pManager, eRecipientType, objFieldsHandle, processedTransactionHandle, &pReturnString, &pTransactionString);
				}

			}
			else
			{
				SimpleMessageToServer(pManager, TRANSCLIENT_TRANSACTIONFAILED, iTransID, iTransIndex, 0, pReturnString, NULL, NULL);

				ReleaseEverything(pManager, eRecipientType, objFieldsHandle, processedTransactionHandle, &pReturnString, &pTransactionString);
			}
		}
		else
		{
			bool bCanDoTransaction;
			TransDataBlock dbUpdateData = {0};
			char *pTransServerUpdateString = NULL;

			if (pManager->pApplyTransactionIfPossibleCB)
			{
				bCanDoTransaction = pManager->pApplyTransactionIfPossibleCB(eRecipientType, objHandle, pTransactionString,
					processedTransactionHandle, objFieldsHandle, ppReturnString, &dbUpdateData, &pTransServerUpdateString,
					iTransID, pTransactionName, pManager->pCBUserData);
			}
			else
			{
				bCanDoTransaction = pManager->pCanTransactionBeDoneCB(eRecipientType, objHandle, pTransactionString,
					processedTransactionHandle, objFieldsHandle, ppReturnString, iTransID, pTransactionName, pManager->pCBUserData);

				if (bCanDoTransaction)
				{
					bCanDoTransaction = pManager->pApplyTransactionCB(eRecipientType, objHandle, pTransactionString, processedTransactionHandle, objFieldsHandle,
						ppReturnString,  &dbUpdateData, &pTransServerUpdateString, iTransID, pTransactionName, pManager->pCBUserData);

					if (!bCanDoTransaction)
					{
						assertmsg(TransDataBlockIsEmpty(&dbUpdateData) && 
							(!pTransServerUpdateString || !pTransServerUpdateString[0]), 
							"Update strings can not be generated when a transaction fails");
					}

				}
			}

			if (bCanDoTransaction)
			{
				SimpleMessageToServer(pManager, TRANSCLIENT_TRANSACTIONSUCCEEDED, iTransID, iTransIndex, 0, pReturnString,
					&dbUpdateData, pTransServerUpdateString);
			}
			else
			{
				SimpleMessageToServer(pManager, TRANSCLIENT_TRANSACTIONFAILED, iTransID, iTransIndex, 0, pReturnString, NULL, NULL);
			}

			ReleaseEverything(pManager, eRecipientType, objFieldsHandle, processedTransactionHandle, &pReturnString, &pTransactionString);

			if (pManager->pReleaseStringCB)
			{
				pManager->pReleaseStringCB(eRecipientType, pTransServerUpdateString, pManager->pCBUserData);
			}

			if (pManager->pReleaseDataBlockCB)
			{
				pManager->pReleaseDataBlockCB(eRecipientType, &dbUpdateData, pManager->pCBUserData);
			}
		}
		}
		AddHandleNewTransactionTiming(pTransactionName, iStartingTicks, OUTCOME_COMPLETED);
	}


	PERFINFO_AUTO_STOP();
	pManager->bDoingRemoteTransaction = false;
}

void SlowTransactionCompleted(LocalTransactionManager *pManager, LTMSlowTransactionID slowTransactionID, enumSlowTransactionOutcome eOutcome,
  char *pReturnValString, TransDataBlock *pDBUpdateData, char *pTransServerUpdateString)
{
	SlowTransactionInfo *pSlowTransInfo = GetAndReleaseSlowTransactionInfo(pManager, slowTransactionID);

	TransactionHandleCache *pHandleCache = NULL;

	assert(pManager->iThreadID == GetCurrentThreadId());


	assert(pSlowTransInfo);

	
	if (pManager->pReleaseProcessedTransactionHandleCB)
	{
		pManager->pReleaseProcessedTransactionHandleCB(pSlowTransInfo->eObjType, pSlowTransInfo->processedTransHandle, pManager->pCBUserData);
	}


	switch (eOutcome)
	{
	case SLOWTRANSACTION_FAILED:

		SimpleMessageToServer(pManager, TRANSCLIENT_TRANSACTIONFAILED, pSlowTransInfo->iTransID, pSlowTransInfo->iTransIndex, 0, pReturnValString, NULL, NULL);
		if (pSlowTransInfo->bRequiresConfirm)
		{
			pHandleCache = FindExistingHandleCache(pSlowTransInfo->iHandleCacheID);

		}
		break;

	case SLOWTRANSACTION_BLOCKED:
		SimpleMessageToServer(pManager, TRANSCLIENT_TRANSACTIONBLOCKED, pSlowTransInfo->iTransID, pSlowTransInfo->iTransIndex, 0, NULL, NULL, NULL);
		pHandleCache = FindExistingHandleCache(pSlowTransInfo->iHandleCacheID);

		break;

	case SLOWTRANSACTION_SUCCEEDED:
		if (pSlowTransInfo->bRequiresConfirm)
		{
			if (pSlowTransInfo->bSucceedAndConfirmIsOK)
			{
				//first, send our success message to the server... then do exactly what we would do if we got a confirm 
				//message back immediately
				SimpleMessageToServer(pManager, TRANSCLIENT_TRANSACTIONPOSSIBLEANDCONFIRMED, pSlowTransInfo->iTransID, pSlowTransInfo->iTransIndex,
					0, pReturnValString, pDBUpdateData, pTransServerUpdateString);

				pHandleCache = FindExistingHandleCache(pSlowTransInfo->iHandleCacheID);
				pManager->pCommitAndReleaseLockCB(pSlowTransInfo->eObjType, pHandleCache->objHandle, pHandleCache->objFieldsHandle, pSlowTransInfo->iTransID, pManager->pCBUserData);

			}
			else
			{
				SimpleMessageToServer(pManager, TRANSCLIENT_TRANSACTIONPOSSIBLE, pSlowTransInfo->iTransID, pSlowTransInfo->iTransIndex,
					pSlowTransInfo->iHandleCacheID, pReturnValString, pDBUpdateData, pTransServerUpdateString);
			}
		}
		else
		{
			SimpleMessageToServer(pManager, TRANSCLIENT_TRANSACTIONSUCCEEDED, pSlowTransInfo->iTransID, pSlowTransInfo->iTransIndex,
					0, pReturnValString, pDBUpdateData, pTransServerUpdateString);


		}
		break;
	}



	if (pManager->pReleaseStringCB)
	{
		pManager->pReleaseStringCB(pSlowTransInfo->eObjType, pReturnValString, pManager->pCBUserData);
		pManager->pReleaseStringCB(pSlowTransInfo->eObjType, pTransServerUpdateString, pManager->pCBUserData);
	}

	if (pManager->pReleaseDataBlockCB)
	{
		pManager->pReleaseDataBlockCB(pSlowTransInfo->eObjType, pDBUpdateData, pManager->pCBUserData);
	}

	if (pHandleCache && pManager->pReleaseObjectFieldsHandleCB)
	{
		pManager->pReleaseObjectFieldsHandleCB(pSlowTransInfo->eObjType, pHandleCache->objFieldsHandle, pManager->pCBUserData);
	}

	ReleaseHandleCache(&pHandleCache);
}



void HandleCancelTransaction(LocalTransactionManager *pManager, Packet *pPacket)
{
	PERFINFO_AUTO_START("HandleCancelTransaction",1);
	assert(pManager->iThreadID == GetCurrentThreadId());
	{	
	TransactionID iTransID = GetTransactionIDFromPacket(pPacket);
	int iTransIndex = pktGetBitsPack(pPacket, 1);
	int eRecipientType = GetContainerTypeFromPacket(pPacket);
	int iRecipientID = GetContainerIDFromPacket(pPacket);

	int iHandleCacheID = pktGetBits(pPacket, 32);
	TransactionHandleCache *pHandleCache;

	pManager->bDoingRemoteTransaction = true;
	strcpy(pManager->currentTransactionName, "CANCELLING");

	if (gbLTMLogging)
	{
		LTM_LOG("Got HandleCancelTransaction, trans ID %u\n", iTransID);
	}

	pHandleCache = GetAndReleaseHandleCache(iHandleCacheID);


	SimpleMessageToServer(pManager, TRANSCLIENT_TRANSACTIONCANCELCONFIRMED, iTransID, iTransIndex, 0, NULL, NULL, NULL);

	pManager->pUndoLockCB(eRecipientType, pHandleCache->objHandle, pHandleCache->objFieldsHandle, iTransID, pManager->pCBUserData);

	if (pManager->pReleaseObjectFieldsHandleCB)
	{
		pManager->pReleaseObjectFieldsHandleCB(eRecipientType, pHandleCache->objFieldsHandle, pManager->pCBUserData);
	}
	}
	
	pManager->bDoingRemoteTransaction = false;

	PERFINFO_AUTO_STOP();
}



void HandleConfirmTransaction(LocalTransactionManager *pManager, Packet *pPacket)
{
	PERFINFO_AUTO_START("HandleConfirmTransaction",1);
	assert(pManager->iThreadID == GetCurrentThreadId());
	{	
	TransactionID iTransID = GetTransactionIDFromPacket(pPacket);
	int iTransIndex = pktGetBitsPack(pPacket, 1);
	int eRecipientType = GetContainerTypeFromPacket(pPacket);
	int iRecipientID = GetContainerIDFromPacket(pPacket);

	int iHandleCacheID = pktGetBits(pPacket, 32);

	TransactionHandleCache *pHandleCache;

	pManager->bDoingRemoteTransaction = true;
	strcpy(pManager->currentTransactionName, "CONFIRMING");

	if (gbLTMLogging)
	{
		LTM_LOG("Got HandleConfirmTransaction, trans ID %u\n", iTransID);
	}

	pHandleCache = GetAndReleaseHandleCache(iHandleCacheID);

	SimpleMessageToServer(pManager, TRANSCLIENT_TRANSACTIONSUCCEEDED, iTransID, iTransIndex, 0, NULL, NULL, NULL);

	pManager->pCommitAndReleaseLockCB(eRecipientType, pHandleCache->objHandle, pHandleCache->objFieldsHandle, iTransID, pManager->pCBUserData);

	if (pManager->pReleaseObjectFieldsHandleCB)
	{
		pManager->pReleaseObjectFieldsHandleCB(eRecipientType, pHandleCache->objFieldsHandle, pManager->pCBUserData);
	}
	}
	pManager->bDoingRemoteTransaction = false;
	PERFINFO_AUTO_STOP();
}


enumTransServerConnectResult SendTransactionServerHandshake(LocalTransactionManager *pLTM, NetLink *pLink, int eServerType, U32 iServerID)
{
	int ret;

	Packet *pPacket = pktCreate(pLink, TRANSCLIENT_REGISTERCLIENTINFO);

	pktSendBitsPack(pPacket, 1, eServerType);
	pktSendBitsPack(pPacket, 1, iServerID);
	pktSendBitsPack(pPacket, 1, gServerLibState.antiZombificationCookie);
	pktSendString(pPacket, GetUsefulVersionString());

	pktSend(&pPacket);

	pLTM->eTransServerConnectResult = TRANS_SERVER_CONNECT_RESULT_NONE;

	ret = linkWaitForPacket(pLink,0,1000.f);
	if (!ret)
	{
		return TRANS_SERVER_CONNECT_RESULT_HANDSHAKE_FAILED;
	}

	return pLTM->eTransServerConnectResult;
}

enumTransServerConnectResult SendTransactionServerMultiplexHandshake(LocalTransactionManager *pLTM, LinkToMultiplexer *pManager, int eServerType, U32 iServerID)
{
	int ret;

	Packet *pPacket = CreateLinkToMultiplexerPacket( pManager, MULTIPLEX_CONST_ID_TRANSACTION_SERVER, TRANSCLIENT_REGISTERCLIENTINFO, NULL);

	pktSendBitsPack(pPacket, 1, eServerType);
	pktSendBitsPack(pPacket, 1, iServerID);
	pktSendBitsPack(pPacket, 1, gServerLibState.antiZombificationCookie);
	pktSendString(pPacket, GetUsefulVersionString());

	pktSend(&pPacket);

	pLTM->eTransServerConnectResult = TRANS_SERVER_CONNECT_RESULT_SUCCESS;

	ret = LinkToMultiplexerMonitorBlock(pManager, TRANSSERVER_CONNECTION_RESULT, HandshakeResultCB, 1000.0f);

	if (!ret)
	{
		return TRANS_SERVER_CONNECT_RESULT_HANDSHAKE_FAILED;
	}

	return pLTM->eTransServerConnectResult;


}

void RegisterObjectWithTransactionServer(LocalTransactionManager *pManager, GlobalType eObjectType, int iObjectID)
{
	char commandString[256];
	char commentString[1024];

	Packet *pPacket;
	
	if (pManager->bIsFullyLocal)
		return; // do nothing for fully local managers
	pPacket = CreateLTMPacket(pManager, TRANSCLIENT_SENDTRANSSERVERCOMMANDANDCOMMENT, PacketTrackerFind("RegisterObjectWithTS", 0, GlobalTypeToName(eObjectType)));
	assert(pManager->iThreadID == GetCurrentThreadId());

	sprintf(commandString, "%s %s %d %s %d",
		TRANSACTIONSERVER_COMMAND_ONLINE,
		GlobalTypeToName(eObjectType),
		iObjectID,
		GlobalTypeToName(pManager->eServerType),
		pManager->iServerID);

	sprintf(commentString, "%s[%u] registering %s[%u] with trans server",
		GlobalTypeToName(GetAppGlobalType()), gServerLibState.containerID, GlobalTypeToName(eObjectType), iObjectID);

	pktSendString(pPacket, commandString);
	pktSendString(pPacket, commentString);

	pktSend(&pPacket);
}

void SendTransactionServerCommand(LocalTransactionManager *pManager, char *commandString, char *pCommentString)
{
	Packet *pPacket;
	
	CreateLTMPacketWithFunctionNameTracker(pPacket, pManager, TRANSCLIENT_SENDTRANSSERVERCOMMANDANDCOMMENT);
	assert(pManager->iThreadID == GetCurrentThreadId());

	pktSendString(pPacket, commandString);
	pktSendString(pPacket, pCommentString);

	pktSend(&pPacket);
}

void RegisterAsDefaultOwnerOfObjectTypeWithTransactionServer(LocalTransactionManager *pManager, GlobalType eObjectType)
{
	char commandString[256];
	char commentString[1024];
	Packet *pPacket;

	CreateLTMPacketWithFunctionNameTracker(pPacket, pManager, TRANSCLIENT_SENDTRANSSERVERCOMMANDANDCOMMENT);
	
	assert(pManager->iThreadID == GetCurrentThreadId());

	sprintf(commandString, "%s %s %d %s %s %d",
		TRANSACTIONSERVER_COMMAND_REGISTER,
		GlobalTypeToName(eObjectType),
		eObjectType,
		GlobalTypeSchemaType(eObjectType) == SCHEMATYPE_PERSISTED? "persisted" : "transacted",
		GlobalTypeToName(pManager->eServerType),
		pManager->iServerID);

	sprintf(commentString, "%s[%u] is registering as default owner of type %s",
		GlobalTypeToName(GetAppGlobalType()), GetAppGlobalID(), GlobalTypeToName(eObjectType));

	pktSendString(pPacket, commandString);
	pktSendString(pPacket, commentString);

	pktSend(&pPacket);
}

void RegisterMultipleObjectsWithTransactionServer(LocalTransactionManager *pManager, GlobalType eObjectType, int iNumObjects, int *piObjectIDs)
{
	char tempString[10000];
	char *pStartingString = tempString;
	size_t pWorkString_size = ARRAY_SIZE(tempString);

	char *pWorkString;
	char commentString[1024];

	int i;

	Packet *pPacket;
	
	CreateLTMPacketWithFunctionNameTracker(pPacket, pManager, TRANSCLIENT_SENDTRANSSERVERCOMMANDANDCOMMENT);
	assert(pManager->iThreadID == GetCurrentThreadId());

	if (iNumObjects * 64 > 10000)
	{
		pWorkString_size = iNumObjects * 64;
		pStartingString = malloc(pWorkString_size);
		assert(pStartingString);
	}

	pWorkString = pStartingString;


	for (i=0; i < iNumObjects; i++)
	{
		size_t len;
		snprintf_s(pWorkString, pWorkString_size, "%s %s %d %s %d %s",
			TRANSACTIONSERVER_COMMAND_ONLINE,
			GlobalTypeToName(eObjectType),
			piObjectIDs[i],
			GlobalTypeToName(pManager->eServerType),
			pManager->iServerID,
			i == iNumObjects - 1 ? "" : "$$");

		len = strlen(pWorkString);
		pWorkString += len;
		pWorkString_size -= len;
	}

	pktSendString(pPacket, pStartingString);

	sprintf(commentString, "%s[%u] is registering as owner of %d objects of type %s",
		GlobalTypeToName(GetAppGlobalType()), GetAppGlobalID(), iNumObjects, GlobalTypeToName(eObjectType));

	pktSendString(pPacket, commentString);
	pktSend(&pPacket);

	if (pStartingString != tempString)
	{
		free(pStartingString);
	}
}

void DestroyLocalTransaction(LocalTransaction *pTransaction)
{
	int i;
	int iNumBaseTransactions = eaSize(&pTransaction->ppBaseTransactions);

	for (i=0; i < iNumBaseTransactions; i++)
	{
		estrDestroy(&pTransaction->pBaseTransactionStates[i].returnString);
		TransDataBlockClear(&pTransaction->pBaseTransactionStates[i].databaseUpdateData);
		estrDestroy(&pTransaction->pBaseTransactionStates[i].transServerUpdateString);
		
	}

	assert(pTransaction->ppBaseTransactions);
	assert(pTransaction->ppBaseTransactions[0]);
	//all the base transaction structs and data were allocated with one big malloc
	free(pTransaction->ppBaseTransactions[0]);

	eaDestroy(&pTransaction->ppBaseTransactions);

}

void DestroyLocalTransactionManager(LocalTransactionManager *pManager)
{
	LocalTransaction *pTransaction, *pNextTransaction;

	pManager->bDestroying = true;

	if (pManager->pNetLink)
	{
		linkRemove(&pManager->pNetLink);
	}
	else
	{
		DestroyLinkToMultiplexer(pManager->pMultiplexLink);
	}

	pTransaction = pManager->pFirstBlocked;

	while (pTransaction)
	{
		pNextTransaction = pTransaction->pNext;

		DestroyLocalTransaction(pTransaction);

		pTransaction = pNextTransaction;
	}


	if (pManager->pReleaseObjectFieldsHandleCB)
	{
		int i;
		EnterCriticalSection(&handleCacheCS);
		for (i=0; i < eaSize(&ppHandleCaches); i++)
		{
			TransactionHandleCache *pCache = ppHandleCaches[i];
			if (pCache->iNextFree == -2)
			{
				pManager->pReleaseObjectFieldsHandleCB(pCache->eObjType, pCache->objFieldsHandle, pManager->pCBUserData);
			}
		}
		LeaveCriticalSection(&handleCacheCS);
	}
	EnterCriticalSection(&handleCacheCS);
	eaDestroyEx(&ppHandleCaches, NULL);
	LeaveCriticalSection(&handleCacheCS);
	eaDestroyEx(&pManager->ppSlowTransactions, NULL);

	free(pManager);
}

static bool sbAssertOnTransactionDisconnect = false;
AUTO_CMD_INT(sbAssertOnTransactionDisconnect, AssertOnTransactionDisconnect) ACMD_CMDLINE ACMD_ACCESSLEVEL(0) ACMD_HIDE;

static void HandleTransactionManagerDisconnect(LocalTransactionManager *pManager, const char *pErrorString)
{
	if (!pManager->bDestroying && !GSM_IsQuitting())
	{
		
		assertmsg(!sbAssertOnTransactionDisconnect, pErrorString);
		AttemptToConnectToController(true, NULL, false);
		ErrorOrAlert("LOST_TRANSSERVER_CNCT", "%s", pErrorString);
		log_printf(LOG_CRASH, "%s", pErrorString);
	}

	if (pManager)
	{
		LTM_ExitOrShutdownCallback(pManager);
	}
}

void LocalTransactionManagerDisconnectCallback(NetLink *link, void *pUserData)
{
	LocalTransactionManager *pManager = pUserData;
	char *pDisconnectReason = NULL;
	char *pErrorString = NULL;

	estrStackCreate(&pDisconnectReason);
	linkGetDisconnectReason(link, &pDisconnectReason);
	estrPrintf(&pErrorString, "Local Transaction Manager lost connection to transaction server. Reason: %s", pDisconnectReason);

	HandleTransactionManagerDisconnect(pManager, pErrorString);

	estrDestroy(&pErrorString);
	estrDestroy(&pDisconnectReason);
}

void sSingleMultiplexedServerDiedCB(int iIndexOfDeadServer, LinkToMultiplexer *pManager)
{
	if (iIndexOfDeadServer == MULTIPLEX_CONST_ID_TRANSACTION_SERVER)
	{
		HandleTransactionManagerDisconnect(pManager->pUserData, "Local Transaction Manager lost connection to transaction server (multiplexed)");
	}
}

void sEntireMultiplexedServerDiedCB(LinkToMultiplexer *pManager)
{
	HandleTransactionManagerDisconnect(pManager->pUserData, "Local Transaction Manager lost connection to launcher/multiplexer");
}

void sGetMultiplexedMessageCB(Packet *pak, int iMsg, int iIndexOfSender, LinkToMultiplexer *pMultiplexManager);

void ReportHandshakeError(enumTransServerConnectResult eResult)
{
	switch (eResult)
	{
	case TRANS_SERVER_CONNECT_RESULT_FAILURE_OBJECTDBALREADYCONNECTED:
		Errorf("The transaction server already has an object DB connected... can't connect another one");
		break;
	case TRANS_SERVER_CONNECT_RESULT_FAILURE_SERVERIDNOTUNIQUE:
		Errorf("The transaction server already has an a server with this type and ID connected... can't connect another one");
		break;
	case TRANS_SERVER_CONNECT_RESULT_FAILURE_ANTIZOMBIFICATIONCOOKIE_MISMATCH:
		Errorf("The transaction server has a different anti-zombification cookie. Help! Zombies!");
		break;
	case TRANS_SERVER_CONNECT_RESULT_FAILURE_TOOMANYMULTIPLEXERS:
		Errorf("The transaction server has too many multiplexers connected already");
		break;
	case TRANS_SERVER_CONNECT_RESULT_FAILURE_MULTIPLEXIDINUSE:
		Errorf("The transaction server already is communicating with someone with that multiplex index");
		break;
	case TRANS_SERVER_CONNECT_RESULT_HANDSHAKE_FAILED:
		Errorf("The transaction server never responded to the connection handshake");
		break;
	case TRANS_SERVER_CONNECT_RESULT_TOO_MANY_CONNECTIONS:
		Errorf("The transaction server has too many connections already");
		break;
	}
}

static float sTransServerConnectTimeout = 30.0f;

void SetLocalTransactionManagerConnectionDelayTime(float fTime)
{
	sTransServerConnectTimeout = fTime;
}


void DEFAULT_LATELINK_GetLocalTransactionManagerArraySizes(int *piMaxHandleCaches, int *piMaxSlowTransactions)
{
	*piMaxHandleCaches = 65536;
	*piMaxSlowTransactions = 32768;
}

LocalTransactionManager *CreateBackgroundThreadLocalTransactionManager(LocalTransactionManager *pMainManager)
{
	LocalTransactionManager *pBackgroundManager;
	pBackgroundManager = (LocalTransactionManager*)calloc(sizeof(LocalTransactionManager),1);
	pBackgroundManager->pDoesObjectExistCB = pMainManager->pDoesObjectExistCB;
	pBackgroundManager->pPreProcessTransactionStringCB = pMainManager->pPreProcessTransactionStringCB;
	pBackgroundManager->pAreFieldsOKToBeLockedCB = pMainManager->pAreFieldsOKToBeLockedCB;
	pBackgroundManager->pCanTransactionBeDoneCB = pMainManager->pCanTransactionBeDoneCB;
	pBackgroundManager->pBeginLockCB = pMainManager->pBeginLockCB;
	pBackgroundManager->pApplyTransactionCB = pMainManager->pApplyTransactionCB;
	pBackgroundManager->pApplyTransactionIfPossibleCB = pMainManager->pApplyTransactionIfPossibleCB;
	pBackgroundManager->pUndoLockCB = pMainManager->pUndoLockCB;
	pBackgroundManager->pCommitAndReleaseLockCB = pMainManager->pCommitAndReleaseLockCB;
	pBackgroundManager->pReleaseObjectFieldsHandleCB = pMainManager->pReleaseObjectFieldsHandleCB;
	pBackgroundManager->pReleaseProcessedTransactionHandleCB = pMainManager->pReleaseProcessedTransactionHandleCB;
	pBackgroundManager->pReleaseStringCB = pMainManager->pReleaseStringCB;
	pBackgroundManager->pBeginSlowTransactionCB = pMainManager->pBeginSlowTransactionCB;
	pBackgroundManager->pProcessDBUpdateData = pMainManager->pProcessDBUpdateData;
	pBackgroundManager->pReleaseDataBlockCB = pMainManager->pReleaseDataBlockCB;
	pBackgroundManager->pGracefulShutdownCB = pMainManager->pGracefulShutdownCB;

	pBackgroundManager->bIsFullyLocal = pMainManager->bIsFullyLocal;
	
	pBackgroundManager->eServerType = pMainManager->eServerType;
	pBackgroundManager->iServerID = pMainManager->iServerID;

	pBackgroundManager->iThreadID = GetCurrentThreadId();
	pBackgroundManager->pCBUserData = pMainManager->pCBUserData;

	pBackgroundManager->iFirstFreeSlowTransactionIndex = -1;
	pBackgroundManager->iNextLocalTransactionID = TRANSACTIONID_SPECIALBIT_LOCALTRANSACTION;
	pBackgroundManager->pFirstBlocked = pBackgroundManager->pLastBlocked = NULL;
	pBackgroundManager->pProcessDBUpdateData= NULL;

	pBackgroundManager->pMultiplexLink = pMainManager->pMultiplexLink;
	pBackgroundManager->pNetLink = pMainManager->pNetLink;

	return pBackgroundManager;
}

typedef struct LocalTransactionManagerThreadData
{
	LocalTransactionManager *pManager;
} LocalTransactionManagerThreadData;

LocalTransactionManagerThreadData *GetLocalTransactionManagerThreadData(void)
{
	LocalTransactionManagerThreadData *threadData;
	STATIC_THREAD_ALLOC(threadData);
	return threadData;
}

LocalTransactionManager **objGetThreadLocalTransactionManager(void)
{
	LocalTransactionManagerThreadData *threadData = GetLocalTransactionManagerThreadData();
	assert(threadData);
	if(!threadData->pManager)
	{
		threadData->pManager = CreateBackgroundThreadLocalTransactionManager(gObjectTransactionManager.localManager);
	}
	return &threadData->pManager;
}

LocalTransactionManager *CreateAndRegisterLocalTransactionManager(
	void *pCBUserData,
	LTMCallBack_DoesObjectExistLocally *pDoesObjectExistCB,
	LTMCallBack_PreProcessTransactionString *pPreProcessTransactionStringCB,
	LTMCallBack_AreFieldsOKToBeLocked *pAreFieldsOKToBeLockedCB,
	LTMCallBack_CanTransactionBeDone *pCanTransactionBeDoneCB,
	LTMCallBack_BeginLock *pBeginLockCB,
	LTMCallBack_ApplyTransaction *pApplyTransactionCB,
	LTMCallBack_ApplyTransactionIfPossible *pApplyTransactionIfPossibleCB,
	LTMCallBack_UndoLock *pUndoLockCB,
	LTMCallBack_CommitAndReleaseLock *pCommitAndReleaseLockCB,
	LTMCallBack_ReleaseObjectFieldsHandle *pReleaseObjectFieldsHandleCB,
	LTMCallBack_ReleaseProcessedTransactionHandle *pReleaseProcessedTransactionHandleCB,
	LTMCallBack_ReleaseString *pReleaseStringCB,
	LTMCallBack_BeginSlowTransaction *pBeginSlowTransactionCB,
	LTMCallBack_ReleaseDataBlock *pReleaseDataBlockCB,
	PacketCallback *pPacketCB,


	GlobalType eServerType, //what type of global object the current entire server is
	U32 iServerID, //its ID
	char *pServerHostName,
	int iServerPort,
	bool bIsMultiplexServer,

	bool bIsFullyLocal,
	char **ppErrorString
	)
{
	void LocalTransactionManagerLinkCallback(Packet *pak, int cmd, NetLink *link, void *pUserData);

	LocalTransactionManager *pManager;

	pManager = (LocalTransactionManager*)calloc(sizeof(LocalTransactionManager),1);

	assert(pManager);
	pManager->iThreadID = GetCurrentThreadId();

	pManager->bIsFullyLocal = bIsFullyLocal;

	if (!LTMIsFullyLocal(pManager))
	{
		if (bIsMultiplexServer)
		{
			int iRequiredServers[] = { MULTIPLEX_CONST_ID_TRANSACTION_SERVER, -1 };
			enumTransServerConnectResult eResult;

			char *pMultiplexError = NULL;

			pManager->pMultiplexLink = GetAndAttachLinkToMultiplexer("localhost", GetMultiplexerListenPort(), LINKTYPE_SHARD_CRITICAL_2MEG,
				sGetMultiplexedMessageCB,
				enumTransServerMessagesToLTMsEnum,
				sSingleMultiplexedServerDiedCB,
				sEntireMultiplexedServerDiedCB,
				iRequiredServers, MULTIPLEX_CONST_ID_TRANSACTION_SERVER, pManager, "Link to multiplexer, thence to transaction server", &pMultiplexError);

			

			if (!pManager->pMultiplexLink)
			{
				if (ppErrorString)
				{
					estrPrintf(ppErrorString, "GetAndAttachLinkToMultiplexer failed: %s", pMultiplexError);
				}
				estrDestroy(&pMultiplexError);
				free(pManager);
				return NULL;
			}

			eResult = SendTransactionServerMultiplexHandshake(pManager, pManager->pMultiplexLink,
				eServerType, iServerID);

			if (eResult != TRANS_SERVER_CONNECT_RESULT_SUCCESS)
			{
				if (ppErrorString)
				{
					estrPrintf(ppErrorString, "SendTransactionServerMultiplexHandshake did not suceed. Results was: %s",
						StaticDefineIntRevLookup(enumTransServerConnectResultEnum, eResult));
				}
				ReportHandshakeError(eResult);
				DestroyLinkToMultiplexer(pManager->pMultiplexLink);
				free(pManager);
				return NULL;
			}
		}
		else
		{
			enumTransServerConnectResult eResult;
			LinkFlags extraFlags = 0;

			// If requested, add extra debugging flags to the link.
			if (sbLTMLinkDebug)
				extraFlags |= LINK_PACKET_VERIFY | LINK_CRAZY_DEBUGGING;

			// Require the link to be compressed unless suppressed by a command line option.
			// This differs from Multiplexer links to the Transaction Server, which are uncompressed by default.
			if (giCompressTransactionLink == 0)
				extraFlags |= LINK_NO_COMPRESS;

			// Connect to Transaction Server.
			pManager->pNetLink = commConnect(commDefault(),
				LINKTYPE_SHARD_CRITICAL_20MEG,
				LINK_FORCE_FLUSH | extraFlags,
				pServerHostName,
				iServerPort,
				pPacketCB ? pPacketCB : LocalTransactionManagerLinkCallback,
				0,
				LocalTransactionManagerDisconnectCallback,
				0);

			linkSetDebugName(pManager->pNetLink, "Link to transaction server");

			// If requested, turn on keep-alive.
			// Note: Currently, the interaction between keep-alive and the transaction link is not completely understood, so
			// this is disabled right now.
			//if (sbLTMLinkDebug)
			//	linkSetKeepAlive(pManager->pNetLink);

			if (!linkConnectWait(&pManager->pNetLink,sTransServerConnectTimeout))
			{
				if (ppErrorString)
				{
					estrPrintf(ppErrorString, "commConnect failed");
				}
				free(pManager);
				return NULL;
			}

			linkInitReceiveStats(pManager->pNetLink, enumTransServerMessagesToLTMsEnum);

			linkSetTimeout(pManager->pNetLink, 0);

			linkSetUserData(pManager->pNetLink, pManager);

			eResult = SendTransactionServerHandshake(pManager, pManager->pNetLink,
				eServerType, iServerID);

			if (eResult != TRANS_SERVER_CONNECT_RESULT_SUCCESS)
			{
				if (ppErrorString)
				{
					estrPrintf(ppErrorString, "SendTransactionServerMultiplexHandshake did not suceed. Results was: %s",
						StaticDefineIntRevLookup(enumTransServerConnectResultEnum, eResult));
				}
				ReportHandshakeError(eResult);
				linkRemove(&pManager->pNetLink);
				free(pManager);
				return NULL;
			}


		}
	}

	pManager->eServerType = eServerType;
	pManager->iServerID = iServerID;

	pManager->pCBUserData = pCBUserData;
	pManager->pDoesObjectExistCB = pDoesObjectExistCB;
	pManager->pPreProcessTransactionStringCB = pPreProcessTransactionStringCB;
	pManager->pAreFieldsOKToBeLockedCB = pAreFieldsOKToBeLockedCB;
	pManager->pCanTransactionBeDoneCB = pCanTransactionBeDoneCB;
	pManager->pBeginLockCB = pBeginLockCB;
	pManager->pApplyTransactionCB = pApplyTransactionCB;
	pManager->pApplyTransactionIfPossibleCB = pApplyTransactionIfPossibleCB;
	pManager->pUndoLockCB = pUndoLockCB;
	pManager->pCommitAndReleaseLockCB = pCommitAndReleaseLockCB;
	pManager->pReleaseObjectFieldsHandleCB = pReleaseObjectFieldsHandleCB;
	pManager->pReleaseProcessedTransactionHandleCB = pReleaseProcessedTransactionHandleCB;
	pManager->pReleaseStringCB = pReleaseStringCB;
	pManager->pBeginSlowTransactionCB = pBeginSlowTransactionCB;
	pManager->pReleaseDataBlockCB = pReleaseDataBlockCB;

	pManager->iFirstFreeSlowTransactionIndex = -1;

	pManager->iNextLocalTransactionID = TRANSACTIONID_SPECIALBIT_LOCALTRANSACTION;
	pManager->pFirstBlocked = pManager->pLastBlocked = NULL;



	pManager->pProcessDBUpdateData= NULL;




	return pManager;
}

void RegisterDBUpdateDataCallback(LocalTransactionManager *pManager, LTMCallBack_ProcessDBUpdateData *pProcessDBUpdateData)
{
	pManager->pProcessDBUpdateData = pProcessDBUpdateData;
}

SlowTransactionInfo *GetEmptySlowTransactionInfo(LocalTransactionManager *pManager)
{
	SlowTransactionInfo *pInfo;
	int iCurVerificationValue;

	if (pManager->iFirstFreeSlowTransactionIndex != -1)
	{
		pInfo = pManager->ppSlowTransactions[pManager->iFirstFreeSlowTransactionIndex];
		pManager->iFirstFreeSlowTransactionIndex = pInfo->iNextFree;
	}
	else
	{
		if (eaSize(&pManager->ppSlowTransactions) == MAX_SLOW_TRANSACTIONS)
		{
			return NULL;
		}

		pInfo = calloc(sizeof(SlowTransactionInfo), 1);
		pInfo->iID = eaSize(&pManager->ppSlowTransactions);
		eaPush(&pManager->ppSlowTransactions, pInfo);
	}

	iCurVerificationValue = pInfo->iID >> SLOW_TRANSACTION_INDEX_BITS;
	iCurVerificationValue = (iCurVerificationValue + 1) & ((1 << SLOW_TRANSACTION_VERIFICATION_BITS) - 1);
	if (!iCurVerificationValue)
	{
		iCurVerificationValue++;
	}
	pInfo->iID = (pInfo->iID & ((1 << SLOW_TRANSACTION_INDEX_BITS) - 1)) | (iCurVerificationValue << SLOW_TRANSACTION_INDEX_BITS);

	return pInfo;
}


static __forceinline SlowTransactionInfo *GetSlowTransactionInfo(LocalTransactionManager *pManager, int iID)
{
	SlowTransactionInfo *pInfo;
	int iIndex = iID % MAX_SLOW_TRANSACTIONS;

	assert(pManager->ppSlowTransactions);
	assert(iIndex < eaSize(&pManager->ppSlowTransactions));
	pInfo = pManager->ppSlowTransactions[iIndex];

	assert(pInfo->iID == iID);

	return pInfo;
}

SlowTransactionInfo *GetAndReleaseSlowTransactionInfo(LocalTransactionManager *pManager, int iID)
{
	SlowTransactionInfo *pInfo = GetSlowTransactionInfo(pManager, iID);

	pInfo->iNextFree = pManager->iFirstFreeSlowTransactionIndex;
	pManager->iFirstFreeSlowTransactionIndex = iID % MAX_SLOW_TRANSACTIONS;


	return pInfo;
}

void HandleDBUpdateData(LocalTransactionManager *pManager, Packet *pak)
{
	TransDataBlock *pDataBlock;
	TransDataBlock **ppDataBlocks = NULL;
	PERFINFO_AUTO_START("HandleDBUpdateString",1);
	assert(pManager->iThreadID == GetCurrentThreadId());

	if (!pManager->pProcessDBUpdateData)
	{
		Errorf("Server with no Process DB Update String Callback got a DB Update message. Ack!");
		PERFINFO_AUTO_STOP();
		return;
	}

	pManager->averageBaseTransactionSize = (pManager->averageBaseTransactionSize * pManager->totalBaseTransactions + pktGetSize(pak)) / (pManager->totalBaseTransactions + 1);
	pManager->totalBaseTransactions++;

	while ((pDataBlock = GetTransDataBlockFromPacket_Temp(pak)))
	{
		eaPush(&ppDataBlocks, pDataBlock);
	}
	pManager->pProcessDBUpdateData(&ppDataBlocks, pManager->pCBUserData);

	eaDestroyEx(&ppDataBlocks, TransDataBlockDestroy);

	PERFINFO_AUTO_STOP();
}


int HandshakeResultCB(Packet *pak, int cmd, NetLink *link, void *pUserData)
{
	LocalTransactionManager *pManager = pUserData;
	assert(pManager->iThreadID == GetCurrentThreadId());
	assert(cmd == TRANSSERVER_CONNECTION_RESULT);

	pManager->eTransServerConnectResult = (enumTransServerConnectResult)pktGetBitsPack(pak, 1);
	return 1;
}


//note that this works differently on the transaction server than everywhere else... on the trans server,
//it's simple remote commands that have passed through. Everywhere else, it's counting them RECEIVED
AUTO_STRUCT  AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "Count, totalBytes, totalMsecs");
typedef struct SimpleRemoteCommandTracker
{
	const char *pName; AST(KEY, UNOWNED)
	SimplePacketRemoteCommandFunc *pFunc; NO_AST
	int iCount;
	U64 iTotalBytes;
	float fTotalMsecs;

	PacketTracker *pPacketTracker; NO_AST

	AST_COMMAND("Reset counts", "ResetSimpleRemoteCommandCounts $FIELD(Name) $NOCONFIRM $NORETURN")
} SimpleRemoteCommandTracker;

StashTable sSimplePacketRemoteCommandsByName = 0;

void ResetSimpleRemoteCommandCounts(char *pName)
{
	SimpleRemoteCommandTracker *pTracker;

	if (sSimplePacketRemoteCommandsByName && stashFindPointer(sSimplePacketRemoteCommandsByName, pName, &pTracker))
	{
		pTracker->iCount = 0;
		pTracker->iTotalBytes = 0;
		pTracker->fTotalMsecs = 0;
	}
}

void DirectlyUpdateSimpleRemoteCommandStats(char *pName, int iBytes, PacketTracker **ppOutTracker)
{
	SimpleRemoteCommandTracker *pTracker;

	if (!sSimplePacketRemoteCommandsByName)
	{
		sSimplePacketRemoteCommandsByName = stashTableCreateWithStringKeys(16, StashDefault);
		resRegisterDictionaryForStashTable("SimpleRemoteCmds", RESCATEGORY_SYSTEM, 0, sSimplePacketRemoteCommandsByName, parse_SimpleRemoteCommandTracker);
	}

	if (!stashFindPointer(sSimplePacketRemoteCommandsByName, pName, &pTracker))
	{
		pTracker = StructCreate(parse_SimpleRemoteCommandTracker);
		pTracker->pName = allocAddString(pName);
		stashAddPointer(sSimplePacketRemoteCommandsByName, pTracker->pName, pTracker, true);
	}


	if (!pTracker->pPacketTracker)
	{
		pTracker->pPacketTracker = PacketTrackerFind("SimpleRemoteCommand", 0, pTracker->pName);
	}

	pTracker->iCount++;
	pTracker->iTotalBytes += iBytes;

	*ppOutTracker = pTracker->pPacketTracker;



}

void HandleGotSendPacketSimple(LocalTransactionManager *pManager, Packet *pak)
{
	char *pTransName = pktGetStringTemp(pak);
	U32 iSimplePacketCmd = pktGetBits(pak, 32);
	switch (iSimplePacketCmd)
	{
	xcase TRANSPACKETCMD_REMOTECOMMAND:
		{
			SimpleRemoteCommandTracker *pTracker;

			if (sSimplePacketRemoteCommandsByName && stashFindPointer(sSimplePacketRemoteCommandsByName, pTransName, &pTracker))
			{
				U64 iStartingTicks;
				pTracker->iCount++;
				pTracker->iTotalBytes += pktGetSize(pak) - pktGetIndex(pak);
				iStartingTicks = timerCpuTicks64();
				pTracker->pFunc(pak);
				pTracker->fTotalMsecs += timerSeconds64(timerCpuTicks64() - iStartingTicks) * 1000.0f;
			}
		}
	}
}

void HandleSimplePacketError(LocalTransactionManager *pManager, Packet *pak)
{
	TransServerPacketFailureCB *pCB = (TransServerPacketFailureCB*)pktGetBits64(pak, 64);
	void *pUserData1 = (void*)pktGetBits64(pak, 64);
	void *pUserData2 = (void*)pktGetBits64(pak, 64);

	pCB(pUserData1, pUserData2);
}



void RegisterSimplePacketRemoteCommandFunc(char *pCommandName, SimplePacketRemoteCommandFunc *pFunc)
{
	SimpleRemoteCommandTracker *pTracker = StructCreate(parse_SimpleRemoteCommandTracker);
	if (!sSimplePacketRemoteCommandsByName)
	{
		sSimplePacketRemoteCommandsByName = stashTableCreateWithStringKeys(16, StashDefault);
		resRegisterDictionaryForStashTable("SimpleRemoteCmds", RESCATEGORY_SYSTEM, 0, sSimplePacketRemoteCommandsByName, parse_SimpleRemoteCommandTracker);
	}


	pTracker->pName = pCommandName;
	pTracker->pFunc = pFunc;

	stashAddPointer(sSimplePacketRemoteCommandsByName, pCommandName, pTracker, true);
}

void LocalTransactionManagerLinkCallback(Packet *pak, int cmd, NetLink *link, void *pUserData)
{
	LocalTransactionManager *pManager = pUserData;
	PERFINFO_AUTO_START("LocalTransactionManagerLinkCallback",1);
	assert(pManager->iThreadID == GetCurrentThreadId());

	switch(cmd)
	{
		xcase TRANSSERVER_CONNECTION_RESULT:
			HandshakeResultCB(pak,cmd,link,pUserData);
		xcase TRANSSERVER_REQUEST_SINGLE_TRANSACTION:
			HandleNewTransaction(pManager, pak);
		xcase TRANSSERVER_CANCEL_TRANSACTION:
			HandleCancelTransaction(pManager, pak);
		xcase TRANSSERVER_CONFIRM_TRANSACTION:
			HandleConfirmTransaction(pManager, pak);
		xcase TRANSSERVER_TRANSACTION_COMPLETE:
			HandleTransactionReturnVal(pak);
		xcase TRANSSERVER_TRANSACTION_DBUPDATE:
			HandleDBUpdateData(pManager, pak);
		xcase TRANSSERVER_SEND_PACKET_SIMPLE:
			HandleGotSendPacketSimple(pManager, pak);
		xcase TRANSSERVER_SIMPLE_PACKET_ERROR:
			HandleSimplePacketError(pManager, pak);
		xcase TRANSSERVER_HERE_IS_OWNER:
			HandleContainerOwner(pManager, pak);


		xdefault:
			printf("Unknown command %d\n",cmd);
	}
	PERFINFO_AUTO_STOP();

	// Request the next packet.
	if (sbSignalReplay)
	{
		static HANDLE sGotPacket = NULL;
		if (!sGotPacket)
		{
			sGotPacket = OpenEvent(EVENT_ALL_ACCESS, false, LTM_DEBUG_REPLAY_SIGNAL_NAME);
			assert(sGotPacket);
		}
		SetEvent(sGotPacket);
	}
}

void sGetMultiplexedMessageCB(Packet *pak, int iMsg, int iIndexOfSender, LinkToMultiplexer *pMultiplexManager)
{
	LocalTransactionManager *pManager = (LocalTransactionManager*)pMultiplexManager->pUserData;

	PERFINFO_AUTO_START_FUNC();

	assert(iIndexOfSender == MULTIPLEX_CONST_ID_TRANSACTION_SERVER);
	assert(pManager->iThreadID == GetCurrentThreadId());

	switch(iMsg)
	{
		xcase TRANSSERVER_REQUEST_SINGLE_TRANSACTION:
			HandleNewTransaction(pManager, pak);
		xcase TRANSSERVER_CANCEL_TRANSACTION:
			HandleCancelTransaction(pManager, pak);
		xcase TRANSSERVER_CONFIRM_TRANSACTION:
			HandleConfirmTransaction(pManager, pak);
		xcase TRANSSERVER_TRANSACTION_COMPLETE:
			HandleTransactionReturnVal(pak);
		xcase TRANSSERVER_TRANSACTION_DBUPDATE:
			HandleDBUpdateData(pManager, pak);
		xcase TRANSSERVER_SEND_PACKET_SIMPLE:
			HandleGotSendPacketSimple(pManager, pak);
		xcase TRANSSERVER_SIMPLE_PACKET_ERROR:
			HandleSimplePacketError(pManager, pak);
		xcase TRANSSERVER_HERE_IS_OWNER:
			HandleContainerOwner(pManager, pak);
		xdefault:
			printf("Unknown command %d\n",iMsg);			
	}
	
	PERFINFO_AUTO_STOP();
}

void ReleasePreviouslyBlockedLocalTransaction(LocalTransactionManager *pManager, LocalTransaction *pTransaction)
{
	free(pTransaction->ppBaseTransactions[0]);
	eaDestroy(&pTransaction->ppBaseTransactions);

	free(pTransaction);
}

void UpdateLocalTransactionManager(LocalTransactionManager *pManager)
{
	int i;
	int numTries = pManager->iNumLocalBlocked / 2 + 1;
	PERFINFO_AUTO_START_FUNC();

	assert(pManager->iThreadID == GetCurrentThreadId());
	if (!pManager->bIsFullyLocal)
	{
		PERFINFO_AUTO_START("Monitoring NetLink",1);
		if (pManager->pNetLink)
		{
			//commMonitor(commDefault());
			// VAS 08/08/11 - No longer need to do this, because there's a disconnect callback for the link
// 			if (linkDisconnected(pManager->pNetLink))
// 			{
// 				char *pDisconnectReason;
// 				estrStackCreate(&pDisconnectReason);
// 				linkGetDisconnectReason(pManager->pNetLink, &pDisconnectReason);
// 				AttemptToConnectToController(true, NULL, false);
// 				log_printf(LOG_CRASH, "Local Transaction Manager lost connection to transaction server. Reason: %s", pDisconnectReason);
// 				ErrorOrAlert("LOST_TRANSSERVER_CNCT", "Local Transaction Manager lost connection to transaction server. Reason: %s", pDisconnectReason);
// 				PERFINFO_AUTO_STOP();
// 				
// 				LTM_ExitOrShutdownCallback(pManager);
// 				estrDestroy(&pDisconnectReason);
// 			}
		}
		else
		{
			assert(pManager->pMultiplexLink);

			UpdateLinkToMultiplexer(pManager->pMultiplexLink);
		}
		PERFINFO_AUTO_STOP();
	}

	for (i = 0; pManager->pFirstBlocked && i < numTries; i++)
	{	
		PERFINFO_AUTO_START("try blocked transaction", 1);
		{
			LocalTransaction *pTransactionToTry = pManager->pFirstBlocked;
			pManager->pFirstBlocked = pTransactionToTry->pNext;
			pManager->iNumLocalBlocked--;
			if (pManager->pFirstBlocked == NULL)
			{
				pManager->pLastBlocked = NULL;
			}

			if (AttemptLocalTransaction(pManager, pTransactionToTry, NULL))
			{
				ReleasePreviouslyBlockedLocalTransaction(pManager, pTransactionToTry);
			}
			else
			{
				pTransactionToTry->pNext = NULL;
				pManager->iNumLocalBlocked++;

				if (pManager->pFirstBlocked)
				{
					pManager->pLastBlocked->pNext = pTransactionToTry;
					pManager->pLastBlocked = pTransactionToTry;
				}
				else
				{
					pManager->pFirstBlocked = pManager->pLastBlocked = pTransactionToTry;
				}
			}
		}			
		PERFINFO_AUTO_STOP();
	}
	
	PERFINFO_AUTO_STOP();// FUNC
}

bool GetObjHandleToUse(LocalTransactionManager *pManager, LTMObjectHandle *pPreCalcedObjectHandles, int iIndex, ContainerRef *pRecipient,
	LTMObjectHandle *pOutObjectHandle)
{

	if (pPreCalcedObjectHandles)
	{
		*pOutObjectHandle = pPreCalcedObjectHandles[iIndex];
		return true;
	}

	return pManager->pDoesObjectExistCB(pRecipient->containerType, pRecipient->containerID, pOutObjectHandle, NULL, pManager->pCBUserData);

}

void SendUpdateStringsToServer(LocalTransactionManager *pManager, LocalTransaction *pTransaction)
{

	Packet *pPacket = NULL;
	int i;
	int iNumBaseTransactions = eaSize(&pTransaction->ppBaseTransactions);
	char comment[2048];

	comment[0] = 0;

	if (gbLTMVerboseLogEverything)
	{
		sprintf(comment, "LocaTrans %u(%s) has completed on %s", pTransaction->iID, pTransaction->pTransactionName, GlobalTypeAndIDToString(GetAppGlobalType(), GetAppGlobalID()));
	}

	if (pManager->pProcessDBUpdateData)
	{
		// We're a local database, as an optimization apply them here.
		// This may not be safe with multiple databases
		TransDataBlock **ppDBUpdateDataList = NULL;

		for (i = 0; i < iNumBaseTransactions; i++)
		{
			if (!TransDataBlockIsEmpty(&pTransaction->pBaseTransactionStates[i].databaseUpdateData))
			{
				eaPush(&ppDBUpdateDataList, &pTransaction->pBaseTransactionStates[i].databaseUpdateData);
			}
		}

		if (eaSize(&ppDBUpdateDataList))
		{
			pManager->pProcessDBUpdateData(&ppDBUpdateDataList, pManager->pCBUserData);
			eaDestroy(&ppDBUpdateDataList);
		}

		if (LTMIsFullyLocal(pManager))
		{
			return;
		}
	}
	else
	{	
		assert(!LTMIsFullyLocal(pManager));

		for (i = 0; i < iNumBaseTransactions; i++)
		{
			if (!TransDataBlockIsEmpty(&pTransaction->pBaseTransactionStates[i].databaseUpdateData))
			{
				if (!pPacket)
				{
					pPacket = CreateLTMPacket(pManager, TRANSCLIENT_UPDATESFROMLOCALTRANS, PacketTrackerFind("UpdateFromLocalTrans", 0, pTransaction->pTransactionName));
					pktSendString(pPacket, pTransaction->pTransactionName);
					pktSendString(pPacket, comment);
				}

				PutTransDataBlockIntoPacket(pPacket, &pTransaction->pBaseTransactionStates[i].databaseUpdateData);
			}
		}
	}
	if (pPacket)
	{
		// Done with database updates
		PutTransDataBlockIntoPacket(pPacket, NULL);
	}

	for (i = 0; i < iNumBaseTransactions; i++)
	{
		char *pTransServerUpdateString = pTransaction->pBaseTransactionStates[i].transServerUpdateString;

		if (pTransServerUpdateString && pTransServerUpdateString[0])
		{
			char commentString[1024];
			char baseTransTruncString[64];
			if (!pPacket)
			{
				pPacket = CreateLTMPacket(pManager, TRANSCLIENT_UPDATESFROMLOCALTRANS, PacketTrackerFind("UpdateFromLocalTrans", 0, pTransaction->pTransactionName));
				pktSendString(pPacket, pTransaction->pTransactionName);
				pktSendString(pPacket, comment);
				PutTransDataBlockIntoPacket(pPacket, NULL);
			}

			pktSendBits(pPacket, 1, 1);
			pktSendString(pPacket, pTransServerUpdateString);
		
			strcpy_trunc(baseTransTruncString, pTransaction->ppBaseTransactions[i]->pData);
			sprintf(commentString, "Base trans %d (%s) of local trans %s producted update string",
				i, baseTransTruncString, pTransaction->pTransactionName);
	
			pktSendString(pPacket, commentString);

		}

	}

	if (pPacket)
	{
		// Done with transServer updates
		pktSendBits(pPacket,1,0);
		
		pktSend(&pPacket);
	}
}

void SendTransVariableToTransServer(struct LocalTransactionManager *pManager, TransactionID iTransactionID, const char *pVariableName, void *pData, int iDataSize)
{
	Packet *pPacket;
		
	CreateLTMPacketWithFunctionNameTracker(pPacket, pManager, TRANSCLIENT_SETTRANSVARIABLE);
	PutTransactionIDIntoPacket(pPacket, iTransactionID);
	pktSendString(pPacket, pVariableName);
	pktSendBits(pPacket, 32, iDataSize);
	pktSendBytes(pPacket, iDataSize, pData);
	pktSend(&pPacket);
}

void *GetTransactionVariable(struct LocalTransactionManager *pManager, TransactionID iTransactionID, const char *pVariableName, int *piSize)
{
	assert(pManager->iCurrentlyActiveTransaction == iTransactionID); // Make sure this is a Sequential Atomic transaction
	assert(pManager->transVariableTable);

	return NameTableLookup(pManager->transVariableTable, pVariableName, piSize);
}


void SetTransactionVariableBytes(struct LocalTransactionManager *pManager, TransactionID iTransactionID, const char *pVariableName, void *pData, int iDataSize)
{
	assert(pManager->iCurrentlyActiveTransaction == iTransactionID); // Make sure this is a Sequential Atomic transaction
	assert(pVariableName);
	assert(pData);
	assert(iDataSize);


	if (iTransactionID & TRANSACTIONID_SPECIALBIT_LOCALTRANSACTION)
	{
		if (!pManager->transVariableTable)
		{
			pManager->transVariableTable = CreateNameTable(NULL);
		}
		NameTableAddBytes(pManager->transVariableTable, pVariableName, pData, iDataSize);
	}
	else
	{
		SendTransVariableToTransServer(pManager, iTransactionID, pVariableName, pData, iDataSize);
	}
}


void SetTransactionVariablePacket(struct LocalTransactionManager *pManager, TransactionID iTransactionID, const char *pVariableName, Packet *pPacket)
{
	void *pData;
	int iSize;

	pData = pktGetEntirePayload(pPacket, &iSize);
	SetTransactionVariableBytes(pManager, iTransactionID, pVariableName,pData, iSize);
}


void *SlowTransaction_GetTransactionVariable(struct LocalTransactionManager *pManager, LTMSlowTransactionID slowTransactionID,
   char *pVariableName, int *piDataSize)
{
	SlowTransactionInfo *pSlowTransInfo = GetSlowTransactionInfo(pManager, slowTransactionID);

	assert(pSlowTransInfo->transVariableTable);

	return NameTableLookup(pSlowTransInfo->transVariableTable, pVariableName, piDataSize);
}



//slow transaction sets a transaction variable
void SlowTransaction_SetTransactionVariableBytes(struct LocalTransactionManager *pManager, LTMSlowTransactionID slowTransactionID,
	char *pVariableName, void *pData, int iDataSize)
{
	SlowTransactionInfo *pSlowTransInfo = GetSlowTransactionInfo(pManager, slowTransactionID);
	assert(pManager->iThreadID == GetCurrentThreadId());

	SendTransVariableToTransServer(pManager, pSlowTransInfo->iTransID, pVariableName, pData, iDataSize);
}




void ReturnLocalTransaction(LocalTransactionManager *pManager, LocalTransaction *pTransaction, enumTransactionOutcome eOutcome)
{
	TransactionReturnVal *pReturnVal;
	int i;
	int iNumBaseTransactions = eaSize(&pTransaction->ppBaseTransactions);
	assert(pManager->iThreadID == GetCurrentThreadId());

	if (giLagOnTransact)
	{
		printf("Sleeping %d seconds on local transaction...", giLagOnTransact);
		Sleep(giLagOnTransact * 1000);
		printf("Done\n");
	}

	if (pTransaction->iReturnValID == 0)
	{

		for (i=0; i < iNumBaseTransactions; i++)
		{
			estrDestroy(&pTransaction->pBaseTransactionStates[i].returnString);
		}
	}
	else
	{

		pReturnVal = GetReturnValFromID(pTransaction->iReturnValID);

		if (pReturnVal)
		{
			pReturnVal->eOutcome = eOutcome;

	
			pReturnVal->iNumBaseTransactions = eaSize(&pTransaction->ppBaseTransactions);
			pReturnVal->pBaseReturnVals = (BaseTransactionReturnVal*)calloc(pReturnVal->iNumBaseTransactions * sizeof(BaseTransactionReturnVal),1);

			assert(pReturnVal->pBaseReturnVals);

			for (i=0; i < pReturnVal->iNumBaseTransactions; i++)
			{				
				pReturnVal->pBaseReturnVals[i].eOutcome = (pTransaction->pBaseTransactionStates[i].eOutcome == TRANSACTION_OUTCOME_NONE) ? TRANSACTION_OUTCOME_UNDEFINED : pTransaction->pBaseTransactionStates[i].eOutcome;

				pReturnVal->pBaseReturnVals[i].returnString = pTransaction->pBaseTransactionStates[i].returnString;
			}


			if (pReturnVal->eFlags & TRANSACTIONRETURN_FLAG_MANAGED_RETURN_VAL)
			{
				ManagedReturnValLog_Internal((ActiveTransaction*)pReturnVal, "Local transaction returned %s.", StaticDefineIntRevLookup(enumTransactionOutcomeEnum, pReturnVal->eOutcome));
			}

		}
	}

	if (eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		// only send update strings, if it worked
		SendUpdateStringsToServer(pManager, pTransaction);
	}

	for (i=0; i < iNumBaseTransactions; i++)
	{
		TransDataBlockClear(&pTransaction->pBaseTransactionStates[i].databaseUpdateData);
		estrDestroy(&pTransaction->pBaseTransactionStates[i].transServerUpdateString);
	}
}

void ResetLocalTransaction(LocalTransactionManager *pManager, LocalTransaction *pTransaction)
{
	int i;

	int iNumBaseTransactions = eaSize(&pTransaction->ppBaseTransactions);

	for (i=0; i < iNumBaseTransactions; i++)
	{
		estrDestroy(&pTransaction->pBaseTransactionStates[i].returnString);
		TransDataBlockClear(&pTransaction->pBaseTransactionStates[i].databaseUpdateData);
		estrDestroy(&pTransaction->pBaseTransactionStates[i].transServerUpdateString);

		pTransaction->pBaseTransactionStates[i].eOutcome = TRANSACTION_OUTCOME_UNDEFINED;
	}
}


//returns true if it completed or promoted, false if it needs to be blocked
bool AttemptLocalTransaction(LocalTransactionManager *pManager, LocalTransaction *pTransaction, LTMObjectHandle *pPreCalcedObjectHandles)
{

	int i;

	char *pReturnString = NULL;
	char **ppReturnString = (pTransaction->iReturnValID != 0) ? &pReturnString : NULL;

	bool bNeedsToBlock = false;

	bool bNeedsToPromote = false;
	U32 iTransIDCausingBlock = 0;
	int iNumBaseTransactions = eaSize(&pTransaction->ppBaseTransactions);
	assert(pManager->iThreadID == GetCurrentThreadId());


	PERFINFO_AUTO_START_FUNC();
	pManager->bDoingLocalTransaction = true;
	if (pTransaction->pTransactionName)
		strcpy(pManager->currentTransactionName, pTransaction->pTransactionName);
	else
		strcpy(pManager->currentTransactionName, "NO NAME");
	
	assert(pTransaction->ppBaseTransactions);

	switch (pTransaction->eType)
	{
	case TRANS_TYPE_SIMULTANEOUS:
		for (i=0; i < iNumBaseTransactions; i++)
		{
			BaseTransaction *pBaseTransaction = pTransaction->ppBaseTransactions[i];
			int eRecipientType = pBaseTransaction->recipient.containerType;

			if (pBaseTransaction->recipient.containerType != GLOBALTYPE_NONE)
			{
				LTMObjectHandle objHandle;

				if (!GetObjHandleToUse(pManager, pPreCalcedObjectHandles, i, &pBaseTransaction->recipient, &objHandle))
				{
					bNeedsToPromote = true;
				}
				else
				{
					LTMObjectFieldsHandle objFieldsHandle = NULL;
					LTMProcessedTransactionHandle processedTransactionHandle = NULL;

					if (pManager->pPreProcessTransactionStringCB)
					{
						enumTransactionValidity eValidity = pManager->pPreProcessTransactionStringCB(eRecipientType,
							pBaseTransaction->pData, &processedTransactionHandle, &objFieldsHandle, ppReturnString,
							pTransaction->iID, pTransaction->pTransactionName, pManager->pCBUserData);

						if (eValidity == TRANSACTION_INVALID)
						{
							pTransaction->bAtLeastOneFailure = true;

							estrCopy2(&pTransaction->pBaseTransactionStates[i].returnString, pReturnString);
							pTransaction->pBaseTransactionStates[i].eOutcome = TRANSACTION_OUTCOME_FAILURE;

							ReleaseEverything(pManager, eRecipientType, objFieldsHandle, processedTransactionHandle, &pReturnString, NULL);
							continue;
						}
						else if (eValidity == TRANSACTION_VALID_SLOW)
						{
							bNeedsToPromote = true;
							continue;
						}
					}

					if (!pManager->pAreFieldsOKToBeLockedCB(eRecipientType,
						objHandle, pBaseTransaction->pData, objFieldsHandle, pTransaction->iID, pTransaction->pTransactionName, pManager->pCBUserData, &iTransIDCausingBlock))
					{
						bNeedsToBlock = true;

						ReleaseEverything(pManager, eRecipientType, objFieldsHandle, processedTransactionHandle, &pReturnString, NULL);
					}
					else
					{
						if (pManager->pApplyTransactionIfPossibleCB)
						{
							TransDataBlock dbUpdateData = {0};
							char *pTransServerUpdateString = NULL;

							if (!pManager->pApplyTransactionIfPossibleCB(eRecipientType, objHandle, pBaseTransaction->pData,
								processedTransactionHandle, objFieldsHandle, ppReturnString,
								&dbUpdateData, &pTransServerUpdateString, pTransaction->iID, pTransaction->pTransactionName, pManager->pCBUserData))
							{
								pTransaction->pBaseTransactionStates[i].eOutcome = TRANSACTION_OUTCOME_FAILURE;
								pTransaction->bAtLeastOneFailure = true;
								assert(TransDataBlockIsEmpty(&dbUpdateData));
								assert(!pTransServerUpdateString || !pTransServerUpdateString[0]);
							}
							else
							{
								pTransaction->pBaseTransactionStates[i].eOutcome = TRANSACTION_OUTCOME_SUCCESS;

								TransDataBlockCopy(&pTransaction->pBaseTransactionStates[i].databaseUpdateData, &dbUpdateData);
								estrCopy2(&pTransaction->pBaseTransactionStates[i].transServerUpdateString, pTransServerUpdateString);

								if (pManager->pReleaseStringCB)
								{
									pManager->pReleaseStringCB(eRecipientType, pTransServerUpdateString, pManager->pCBUserData);
								}
								if (pManager->pReleaseDataBlockCB)
								{
									pManager->pReleaseDataBlockCB(eRecipientType, &dbUpdateData, pManager->pCBUserData);
								}
							}


							estrCopy2(&pTransaction->pBaseTransactionStates[i].returnString, pReturnString);

							ReleaseEverything(pManager, eRecipientType, objFieldsHandle, processedTransactionHandle, &pReturnString, NULL);
						}
						else
						{
							if (!pManager->pCanTransactionBeDoneCB(eRecipientType,
								objHandle, pBaseTransaction->pData, processedTransactionHandle, objFieldsHandle, ppReturnString,
								pTransaction->iID, pTransaction->pTransactionName, pManager->pCBUserData))
							{
								pTransaction->bAtLeastOneFailure = true;
								pTransaction->pBaseTransactionStates[i].eOutcome = TRANSACTION_OUTCOME_FAILURE;

								estrCopy2(&pTransaction->pBaseTransactionStates[i].returnString, pReturnString);

								ReleaseEverything(pManager, eRecipientType, objFieldsHandle, processedTransactionHandle, &pReturnString, NULL);
							}
							else
							{
								TransDataBlock dbUpdateData = {0};
								char *pTransServerUpdateString = NULL;

								if (!pManager->pApplyTransactionCB(eRecipientType, objHandle, pBaseTransaction->pData, processedTransactionHandle,
									objFieldsHandle, ppReturnString, &dbUpdateData, &pTransServerUpdateString, pTransaction->iID, pTransaction->pTransactionName, pManager->pCBUserData))
								{
									assertmsg(TransDataBlockIsEmpty(&dbUpdateData) && 
										(!pTransServerUpdateString || !pTransServerUpdateString[0]), 
										"Update strings can not be generated when a transaction fails");

									pTransaction->bAtLeastOneFailure = true;
									pTransaction->pBaseTransactionStates[i].eOutcome = TRANSACTION_OUTCOME_FAILURE;

									estrCopy2(&pTransaction->pBaseTransactionStates[i].returnString, pReturnString);

									ReleaseEverything(pManager, eRecipientType, objFieldsHandle, processedTransactionHandle, &pReturnString, NULL);
								}
								else
								{

									pTransaction->pBaseTransactionStates[i].eOutcome = TRANSACTION_OUTCOME_SUCCESS;

									estrCopy2(&pTransaction->pBaseTransactionStates[i].returnString, pReturnString);
									TransDataBlockCopy(&pTransaction->pBaseTransactionStates[i].databaseUpdateData, &dbUpdateData);
									estrCopy2(&pTransaction->pBaseTransactionStates[i].transServerUpdateString, pTransServerUpdateString);

									if (pManager->pReleaseStringCB)
									{
										pManager->pReleaseStringCB(eRecipientType, pTransServerUpdateString, pManager->pCBUserData);
									}

									if (pManager->pReleaseDataBlockCB)
									{
										pManager->pReleaseDataBlockCB(eRecipientType, &dbUpdateData, pManager->pCBUserData);
									}

									ReleaseEverything(pManager, eRecipientType, objFieldsHandle, processedTransactionHandle, &pReturnString, NULL);
								}
							}
						}

						//mark this base transaction as done so it won't be repeated if this transaction blocks and repeats
						pBaseTransaction->recipient.containerType = GLOBALTYPE_NONE;
					}
				}
			}
		}

		if (bNeedsToPromote)
		{

			PromoteLocalTransaction(pManager, pTransaction);
			pManager->bDoingLocalTransaction = false;
			PERFINFO_AUTO_STOP();
			return true;
		}
		else if (bNeedsToBlock)
		{
			pManager->bDoingLocalTransaction = false;
			PERFINFO_AUTO_STOP();
			return false;
		}
		else
		{
			ReturnLocalTransaction(pManager, pTransaction, pTransaction->bAtLeastOneFailure ? TRANSACTION_OUTCOME_FAILURE : TRANSACTION_OUTCOME_SUCCESS);
			pManager->bDoingLocalTransaction = false;
			PERFINFO_AUTO_STOP();
			return true;
		}

	case TRANS_TYPE_SIMULTANEOUS_ATOMIC:
	case TRANS_TYPE_SEQUENTIAL_ATOMIC:
		{
			LTMObjectFieldsHandle objFieldsHandles[MAX_BASE_TRANSACTIONS_PER_TRANSACTION];
			LTMObjectHandle objHandles[MAX_BASE_TRANSACTIONS_PER_TRANSACTION];

			bool bFailed = false;
			bool bBlocked = false;
			int iNumCancellationsNeeded = 0;

			//only set the current transaction ID for Sequential atomic, because that's the only
			//type that allows transVariable access
			if (pTransaction->eType == TRANS_TYPE_SEQUENTIAL_ATOMIC)
			{
				pManager->iCurrentlyActiveTransaction = pTransaction->iID;
			}

			for (i=0; i < iNumBaseTransactions; i++)
			{
				BaseTransaction *pBaseTransaction = pTransaction->ppBaseTransactions[i];
				int eRecipientType = pBaseTransaction->recipient.containerType;
				LTMProcessedTransactionHandle processedTransactionHandle = NULL;

				if (!GetObjHandleToUse(pManager, pPreCalcedObjectHandles, i, &pBaseTransaction->recipient, &objHandles[i]))
				{
					bNeedsToPromote = true;
					iNumCancellationsNeeded = i;
					break;
				}
				objFieldsHandles[i] = NULL;


				if (pManager->pPreProcessTransactionStringCB)
				{
					enumTransactionValidity eValidity = pManager->pPreProcessTransactionStringCB(eRecipientType,
						pBaseTransaction->pData, &processedTransactionHandle, &objFieldsHandles[i],
						ppReturnString, pTransaction->iID, pTransaction->pTransactionName, pManager->pCBUserData);

					if (eValidity == TRANSACTION_INVALID)
					{
						bFailed = true;
						iNumCancellationsNeeded = i;
						pTransaction->pBaseTransactionStates[i].eOutcome = TRANSACTION_OUTCOME_FAILURE;

					}
					else if (eValidity == TRANSACTION_VALID_SLOW)
					{
						bNeedsToPromote = true;
						iNumCancellationsNeeded = i;
					}
				}


				if (!bFailed && !bNeedsToPromote)
				{
					if (!pManager->pAreFieldsOKToBeLockedCB(eRecipientType,
						objHandles[i], pBaseTransaction->pData, objFieldsHandles[i], pTransaction->iID, pTransaction->pTransactionName, pManager->pCBUserData, &iTransIDCausingBlock))
					{
						if (pTransaction->iID == iTransIDCausingBlock)
						{
							Errorf("Transaction %d is deadlocked with itself on step '%s'",pTransaction->iID,pBaseTransaction->pData);						
							pTransaction->pBaseTransactionStates[i].eOutcome = TRANSACTION_OUTCOME_FAILURE;
							bFailed = true;
							iNumCancellationsNeeded = i;
						}
						else
						{
							bBlocked = true;
							iNumCancellationsNeeded = i;
						}
					}
					else if (!pManager->pCanTransactionBeDoneCB(eRecipientType, objHandles[i], pBaseTransaction->pData, processedTransactionHandle,
						objFieldsHandles[i], ppReturnString, pTransaction->iID, pTransaction->pTransactionName, pManager->pCBUserData))
					{
						pTransaction->pBaseTransactionStates[i].eOutcome = TRANSACTION_OUTCOME_FAILURE;
						bFailed = true;
						iNumCancellationsNeeded = i;
					}
					else
					{
						TransDataBlock dbUpdateData = {0};
						char *pTransServerUpdateString = NULL;

						pTransaction->pBaseTransactionStates[i].eOutcome = TRANSACTION_OUTCOME_SUCCESS;

						pManager->pBeginLockCB(eRecipientType, objHandles[i], objFieldsHandles[i], pTransaction->iID, pTransaction->pTransactionName, pManager->pCBUserData);

						if (!pManager->pApplyTransactionCB(eRecipientType, objHandles[i], pBaseTransaction->pData, processedTransactionHandle, objFieldsHandles[i],
							ppReturnString, &dbUpdateData, &pTransServerUpdateString, pTransaction->iID, pTransaction->pTransactionName, pManager->pCBUserData))
						{
							assertmsg(TransDataBlockIsEmpty(&dbUpdateData) &&
								(!pTransServerUpdateString || !pTransServerUpdateString[0]), 
								"Update strings can not be generated when a transaction fails");

							pManager->pUndoLockCB(eRecipientType, objHandles[i], objFieldsHandles[i], pTransaction->iID, pManager->pCBUserData);
							pTransaction->pBaseTransactionStates[i].eOutcome = TRANSACTION_OUTCOME_FAILURE;
							bFailed = true;
							iNumCancellationsNeeded = i;
						}
						else
						{

							TransDataBlockCopy(&pTransaction->pBaseTransactionStates[i].databaseUpdateData, &dbUpdateData);
							estrCopy2(&pTransaction->pBaseTransactionStates[i].transServerUpdateString, pTransServerUpdateString);

							if (pManager->pReleaseStringCB)
							{
								pManager->pReleaseStringCB(eRecipientType, pTransServerUpdateString, pManager->pCBUserData);
							}

							if (pManager->pReleaseDataBlockCB)
							{
								pManager->pReleaseDataBlockCB(eRecipientType, &dbUpdateData, pManager->pCBUserData);
							}
						}
					}
				}

				estrCopy2(&pTransaction->pBaseTransactionStates[i].returnString, pReturnString);

				if (pManager->pReleaseProcessedTransactionHandleCB)
				{
					pManager->pReleaseProcessedTransactionHandleCB(eRecipientType, processedTransactionHandle, pManager->pCBUserData);
				}

				if (pManager->pReleaseStringCB)
				{
					pManager->pReleaseStringCB(eRecipientType, pReturnString, pManager->pCBUserData);
				}
				pReturnString = NULL;

				if (bBlocked || bFailed || bNeedsToPromote)
				{
					if (pManager->pReleaseObjectFieldsHandleCB)
					{
						pManager->pReleaseObjectFieldsHandleCB(eRecipientType, objFieldsHandles[i], pManager->pCBUserData);
					}

					break;
				}
			}//FOR

			ReleaseAllTransVariables(pManager);
			pManager->iCurrentlyActiveTransaction = 0;

			if (bBlocked || bFailed || bNeedsToPromote)
			{
				for (i=0; i < iNumCancellationsNeeded; i++)
				{
					pManager->pUndoLockCB(pTransaction->ppBaseTransactions[i]->recipient.containerType, objHandles[i], objFieldsHandles[i], pTransaction->iID, pManager->pCBUserData);

					if (pManager->pReleaseObjectFieldsHandleCB)
					{
						pManager->pReleaseObjectFieldsHandleCB(pTransaction->ppBaseTransactions[i]->recipient.containerType, objFieldsHandles[i], pManager->pCBUserData);
					}
				}

				if (bNeedsToPromote)
				{
					ResetLocalTransaction(pManager, pTransaction);
					PromoteLocalTransaction(pManager, pTransaction);
					pManager->bDoingLocalTransaction = false;
					PERFINFO_AUTO_STOP();
					return true;

				}
				else if (bBlocked)
				{
					ResetLocalTransaction(pManager, pTransaction);
					pManager->bDoingLocalTransaction = false;
					PERFINFO_AUTO_STOP();
					return false;
				}
				else
				{
					ReturnLocalTransaction(pManager, pTransaction, TRANSACTION_OUTCOME_FAILURE);
					pManager->bDoingLocalTransaction = false;
					PERFINFO_AUTO_STOP();
					return true;
				}
			}
			else
			{
				for (i=0; i < iNumBaseTransactions; i++)
				{
					pManager->pCommitAndReleaseLockCB(pTransaction->ppBaseTransactions[i]->recipient.containerType, objHandles[i], objFieldsHandles[i], pTransaction->iID, pManager->pCBUserData);
					if (pManager->pReleaseObjectFieldsHandleCB)
					{
						pManager->pReleaseObjectFieldsHandleCB(pTransaction->ppBaseTransactions[i]->recipient.containerType, objFieldsHandles[i], pManager->pCBUserData);
					}
				}
				ReturnLocalTransaction(pManager, pTransaction, TRANSACTION_OUTCOME_SUCCESS);
			}
			pManager->bDoingLocalTransaction = false;
			PERFINFO_AUTO_STOP();
			return true;
		}

	case TRANS_TYPE_SEQUENTIAL:
	case TRANS_TYPE_SEQUENTIAL_STOPONFAIL:
		PERFINFO_AUTO_START("Sequential", 1);
		for (i=pTransaction->iCompletionCounter; i < iNumBaseTransactions; i++)
		{
			BaseTransaction *pBaseTransaction = pTransaction->ppBaseTransactions[i];
			int eRecipientType = pBaseTransaction->recipient.containerType;

			LTMObjectHandle objHandle;

			LTMObjectFieldsHandle objFieldsHandle = NULL;
			LTMProcessedTransactionHandle processedTransactionHandle = NULL;

			bool bFailed = false;

			if (!GetObjHandleToUse(pManager, pPreCalcedObjectHandles, i, &pBaseTransaction->recipient, &objHandle))
			{
				PromoteLocalTransaction(pManager, pTransaction);
				pManager->bDoingLocalTransaction = false;
				PERFINFO_AUTO_STOP();
				PERFINFO_AUTO_STOP();
				return true;
			}


			if (pManager->pPreProcessTransactionStringCB)
			{
				enumTransactionValidity eValidity = pManager->pPreProcessTransactionStringCB(eRecipientType,
					pBaseTransaction->pData, &processedTransactionHandle, &objFieldsHandle, ppReturnString,
					pTransaction->iID, pTransaction->pTransactionName, pManager->pCBUserData);

				if (eValidity == TRANSACTION_INVALID)
				{
					pTransaction->pBaseTransactionStates[i].eOutcome = TRANSACTION_OUTCOME_FAILURE;

					pTransaction->bAtLeastOneFailure = true;
					bFailed = true;

					estrCopy2(&pTransaction->pBaseTransactionStates[i].returnString, pReturnString);

					ReleaseEverything(pManager, eRecipientType, objFieldsHandle, processedTransactionHandle, &pReturnString, NULL);
				}
				else if (eValidity == TRANSACTION_VALID_SLOW)
				{
					ReleaseEverything(pManager, eRecipientType, objFieldsHandle, processedTransactionHandle, &pReturnString, NULL);

					PromoteLocalTransaction(pManager, pTransaction);
					pManager->bDoingLocalTransaction = false;
					PERFINFO_AUTO_STOP();
					PERFINFO_AUTO_STOP();
					return true;
				}
			}

			if (!bFailed)
			{
				if (!pManager->pAreFieldsOKToBeLockedCB(eRecipientType,
					objHandle, pBaseTransaction->pData, objFieldsHandle, pTransaction->iID, pTransaction->pTransactionName, pManager->pCBUserData, &iTransIDCausingBlock))
				{
					bNeedsToBlock = true;

					ReleaseEverything(pManager, eRecipientType, objFieldsHandle, processedTransactionHandle, &pReturnString, NULL);
				}
				else
				{
					if (pManager->pApplyTransactionIfPossibleCB)
					{
						TransDataBlock dbUpdateData = {0};
						char *pTransServerUpdateString = NULL;

						if (!pManager->pApplyTransactionIfPossibleCB(eRecipientType, objHandle, pBaseTransaction->pData,
							processedTransactionHandle, objFieldsHandle, ppReturnString, &dbUpdateData,
							&pTransServerUpdateString, pTransaction->iID, pTransaction->pTransactionName, pManager->pCBUserData))
						{
							pTransaction->pBaseTransactionStates[i].eOutcome = TRANSACTION_OUTCOME_FAILURE;

							pTransaction->bAtLeastOneFailure = true;
							assert(TransDataBlockIsEmpty(&dbUpdateData));
							assert(pTransServerUpdateString == NULL || pTransServerUpdateString[0] == '\0');
						}
						else
						{
							pTransaction->pBaseTransactionStates[i].eOutcome = TRANSACTION_OUTCOME_SUCCESS;

							TransDataBlockCopy(&pTransaction->pBaseTransactionStates[i].databaseUpdateData, &dbUpdateData);
							estrCopy2(&pTransaction->pBaseTransactionStates[i].transServerUpdateString, pTransServerUpdateString);

							if (pManager->pReleaseStringCB)
							{
								pManager->pReleaseStringCB(eRecipientType, pTransServerUpdateString, pManager->pCBUserData);
							}
							if (pManager->pReleaseDataBlockCB)
							{
								pManager->pReleaseDataBlockCB(eRecipientType, &dbUpdateData, pManager->pCBUserData);
							}
						}

						estrCopy2(&pTransaction->pBaseTransactionStates[i].returnString, pReturnString);

						ReleaseEverything(pManager, eRecipientType, objFieldsHandle, processedTransactionHandle, &pReturnString, NULL);
					}
					else
					{
						if (!pManager->pCanTransactionBeDoneCB(eRecipientType,
							objHandle, pBaseTransaction->pData, processedTransactionHandle, objFieldsHandle,
							ppReturnString, pTransaction->iID, pTransaction->pTransactionName, pManager->pCBUserData))
						{
							pTransaction->pBaseTransactionStates[i].eOutcome = TRANSACTION_OUTCOME_FAILURE;

							pTransaction->bAtLeastOneFailure = true;

							estrCopy2(&pTransaction->pBaseTransactionStates[i].returnString, pReturnString);

							ReleaseEverything(pManager, eRecipientType, objFieldsHandle, processedTransactionHandle, &pReturnString, NULL);
						}
						else
						{
							TransDataBlock dbUpdateData = {0};
							char *pTransServerUpdateString = NULL;

							pTransaction->pBaseTransactionStates[i].eOutcome = TRANSACTION_OUTCOME_SUCCESS;

							if (!pManager->pApplyTransactionCB(eRecipientType, objHandle, pBaseTransaction->pData, processedTransactionHandle,
								objFieldsHandle, ppReturnString, &dbUpdateData, &pTransServerUpdateString,
								pTransaction->iID, pTransaction->pTransactionName, pManager->pCBUserData))
							{

								assertmsg(TransDataBlockIsEmpty(&dbUpdateData) &&
									(!pTransServerUpdateString || !pTransServerUpdateString[0]), 
									"Update strings can not be generated when a transaction fails");

								pTransaction->pBaseTransactionStates[i].eOutcome = TRANSACTION_OUTCOME_FAILURE;

								pTransaction->bAtLeastOneFailure = true;

								estrCopy2(&pTransaction->pBaseTransactionStates[i].returnString, pReturnString);

								ReleaseEverything(pManager, eRecipientType, objFieldsHandle, processedTransactionHandle, &pReturnString, NULL);
							}
							else
							{


								TransDataBlockCopy(&pTransaction->pBaseTransactionStates[i].databaseUpdateData, &dbUpdateData);
								estrCopy2(&pTransaction->pBaseTransactionStates[i].transServerUpdateString, pTransServerUpdateString);

								if (pManager->pReleaseStringCB)
								{
									pManager->pReleaseStringCB(eRecipientType, pTransServerUpdateString, pManager->pCBUserData);
								}
								if (pManager->pReleaseDataBlockCB)
								{
									pManager->pReleaseDataBlockCB(eRecipientType, &dbUpdateData, pManager->pCBUserData);
								}
								estrCopy2(&pTransaction->pBaseTransactionStates[i].returnString, pReturnString);

								ReleaseEverything(pManager, eRecipientType, objFieldsHandle, processedTransactionHandle, &pReturnString, NULL);
							}
						}
					}
				}
			}

			if (pTransaction->bAtLeastOneFailure && pTransaction->eType == TRANS_TYPE_SEQUENTIAL_STOPONFAIL)
			{
				ReturnLocalTransaction(pManager, pTransaction, TRANSACTION_OUTCOME_FAILURE);
				pManager->bDoingLocalTransaction = false;
				PERFINFO_AUTO_STOP();
				PERFINFO_AUTO_STOP();
				return true;
			}

			if (bNeedsToBlock)
			{
				pManager->bDoingLocalTransaction = false;
				PERFINFO_AUTO_STOP();
				PERFINFO_AUTO_STOP();
				return false;
			}

			pTransaction->iCompletionCounter++;

		}

		ReturnLocalTransaction(pManager, pTransaction, pTransaction->bAtLeastOneFailure ? TRANSACTION_OUTCOME_FAILURE : TRANSACTION_OUTCOME_SUCCESS);
		pManager->bDoingLocalTransaction = false;
		PERFINFO_AUTO_STOP();
		PERFINFO_AUTO_STOP();
		return true;
	}

	//should never get here
	assert(0);
	pManager->bDoingLocalTransaction = false;
	PERFINFO_AUTO_STOP();
	return true;
}

bool IsLocalTransactionCurrentlyHappening(LocalTransactionManager *pManager)
{
	return pManager->bDoingLocalTransaction;
}

const char *GetTransactionCurrentlyHappening(LocalTransactionManager *pManager)
{
	if (pManager->bDoingLocalTransaction || pManager->bDoingRemoteTransaction)
	{
		return pManager->currentTransactionName;
	}
	return NULL;
}


bool IsLocalManagerFullyLocal(LocalTransactionManager *pManager)
{
	if (pManager)
		return pManager->bIsFullyLocal;
	return false;
}


U32 GetNextLocalTransactionID(LocalTransactionManager *pManager)
{
	U32 iRetVal = pManager->iNextLocalTransactionID;
	pManager->iNextLocalTransactionID++;
	if (!(pManager->iNextLocalTransactionID & TRANSACTIONID_SPECIALBIT_LOCALTRANSACTION))
	{
		pManager->iNextLocalTransactionID = TRANSACTIONID_SPECIALBIT_LOCALTRANSACTION;
	}

	return iRetVal;
}


LocalTransaction *GetFreeLocalTransactionAndAddToBlockList(LocalTransactionManager *pManager)
{
	LocalTransaction *pTransaction = calloc(sizeof(LocalTransaction), 1);

	pManager->iNumLocalBlocked++;

	if (pManager->pFirstBlocked)
	{
		pManager->pLastBlocked->pNext = pTransaction;
		pManager->pLastBlocked = pTransaction;
	}
	else
	{
		pManager->pFirstBlocked = pManager->pLastBlocked = pTransaction;
	}

	return pTransaction;
}

void CopyAndBlockTransaction(LocalTransactionManager *pManager, LocalTransaction *pSourceTransaction)
{
	int iTotalDataSize = 0;
	int dataSizes[MAX_BASE_TRANSACTIONS_PER_TRANSACTION];
	int varSizes[MAX_BASE_TRANSACTIONS_PER_TRANSACTION];
	int i;
	char *pDataBuffer;
	int iNumBaseTransactions = eaSize(&pSourceTransaction->ppBaseTransactions);

	BaseTransaction *pBaseTransactionBlock;

	LocalTransaction *pTransaction = GetFreeLocalTransactionAndAddToBlockList(pManager);


	pTransaction->pTransactionName = pSourceTransaction->pTransactionName;
	pTransaction->iID = pSourceTransaction->iID;
	pTransaction->eType = pSourceTransaction->eType;
	pTransaction->bAtLeastOneFailure = pSourceTransaction->bAtLeastOneFailure;
	pTransaction->iCompletionCounter = pSourceTransaction->iCompletionCounter;
	pTransaction->iReturnValID = pSourceTransaction->iReturnValID;

	for (i=0; i < iNumBaseTransactions; i++)
	{
		dataSizes[i] = (int)strlen(pSourceTransaction->ppBaseTransactions[i]->pData) + 1;
		iTotalDataSize += dataSizes[i];
		if (pSourceTransaction->ppBaseTransactions[i]->pRequestedTransVariableNames)
		{
			varSizes[i] = (int)strlen(pSourceTransaction->ppBaseTransactions[i]->pRequestedTransVariableNames) + 1;
		}
		else
		{
			varSizes[i] = 0;
		}
		iTotalDataSize += varSizes[i];
	}

	pTransaction->ppBaseTransactions = NULL;




	pBaseTransactionBlock = (BaseTransaction*)calloc(iTotalDataSize + (sizeof(BaseTransaction) + sizeof(LocalBaseTransactionState)) * iNumBaseTransactions,1);

	pTransaction->pBaseTransactionStates = (LocalBaseTransactionState*)(pBaseTransactionBlock + iNumBaseTransactions);

	pDataBuffer = (char*)(pTransaction->pBaseTransactionStates + iNumBaseTransactions);

	for (i=0; i < iNumBaseTransactions; i++)
	{
		eaPush(&pTransaction->ppBaseTransactions, &pBaseTransactionBlock[i]);
		memcpy(pTransaction->ppBaseTransactions[i], pSourceTransaction->ppBaseTransactions[i], sizeof(BaseTransaction));
		memcpy(pDataBuffer, pSourceTransaction->ppBaseTransactions[i]->pData, dataSizes[i]);		

		pTransaction->ppBaseTransactions[i]->pData = pDataBuffer;
		pDataBuffer += dataSizes[i];

		if (varSizes[i])
		{
			memcpy(pDataBuffer, pSourceTransaction->ppBaseTransactions[i]->pRequestedTransVariableNames, varSizes[i]);
			pTransaction->ppBaseTransactions[i]->pRequestedTransVariableNames = pDataBuffer;
			pDataBuffer += varSizes[i];
		}

	}

	for (i=0; i < iNumBaseTransactions; i++)
	{
		pTransaction->pBaseTransactionStates[i].eOutcome = pSourceTransaction->pBaseTransactionStates[i].eOutcome;
		pTransaction->pBaseTransactionStates[i].returnString = pSourceTransaction->pBaseTransactionStates[i].returnString;
		memcpy(&pTransaction->pBaseTransactionStates[i].databaseUpdateData, &pSourceTransaction->pBaseTransactionStates[i].databaseUpdateData, sizeof(TransDataBlock));
		pTransaction->pBaseTransactionStates[i].transServerUpdateString = pSourceTransaction->pBaseTransactionStates[i].transServerUpdateString;
	}
}

extern FILE *trDebugLogFile;

void PromoteLocalTransaction(LocalTransactionManager *pManager, LocalTransaction *pTransaction)
{

	int iTotalDataSize = 0;
	int iSizes[MAX_BASE_TRANSACTIONS_PER_TRANSACTION];
	Packet *pPacket;
	int i;
	int iNumBaseTransactions = eaSize(&pTransaction->ppBaseTransactions);


	assertmsg(!LTMIsFullyLocal(pManager), "Fully local transaction manager wants to promote a transaction... something is very wrong");


	//	filelog_printf("transactions.log","PROMOTE%d %s[%d]: %s\n",pTransaction->iID,GlobalTypeToName(pTransaction->pBaseTransactions[0].recipient.iRecipientType),pTransaction->pBaseTransactions[0].recipient.containerID,pTransaction->pBaseTransactions[0].pData);

	for (i=0; i < iNumBaseTransactions; i++)
	{
		iSizes[i] = (int)strlen(pTransaction->ppBaseTransactions[i]->pData) + 1;
		iTotalDataSize += iSizes[i];
	}

	//at least one base transaction must be being promoted
	assert(iTotalDataSize);

	if (gbLTMLogging)
	{
		LTM_LOG("Promoting local transaction (%s)\n", pTransaction->pTransactionName);
	}

	pPacket = CreateLTMPacket(pManager, TRANSCLIENT_REQUEST_NEW_TRANSACTION, PacketTrackerFind("PromoteLocalTrans", 0, pTransaction->pTransactionName));

	pktSendBitsPack(pPacket, 1, pTransaction->eType);
	pktSendBitsPack(pPacket, 1, iNumBaseTransactions);
	pktSendBitsPack(pPacket, 7, iTotalDataSize);



	for (i = 0; i < iNumBaseTransactions; i++)
	{
		PutContainerIDIntoPacket(pPacket, pTransaction->ppBaseTransactions[i]->recipient.containerID);
		PutContainerTypeIntoPacket(pPacket, pTransaction->ppBaseTransactions[i]->recipient.containerType);
		pktSendString(pPacket, pTransaction->ppBaseTransactions[i]->pData);
		if (pTransaction->ppBaseTransactions[i]->pRequestedTransVariableNames)
		{
			pktSendBits(pPacket, 1, 1);
			pktSendString(pPacket, pTransaction->ppBaseTransactions[i]->pRequestedTransVariableNames);
		}
		else
		{
			pktSendBits(pPacket, 1, 0);
		}
	}

	pktSendString(pPacket, pTransaction->pTransactionName);


	if (pTransaction->iReturnValID)
	{
		pktSendBits(pPacket, 1, 1);
		pktSendBits(pPacket, 32, pTransaction->iReturnValID);
	}
	else
	{
		pktSendBits(pPacket, 1, 0);
	}

	//send a bit saying we are sending all the current state info
	pktSendBits(pPacket, 1, 1);

	for (i=0; i < iNumBaseTransactions; i++)
	{
		pktSendBits(pPacket, 2, pTransaction->pBaseTransactionStates[i].eOutcome);

		pktSendString(pPacket, pTransaction->pBaseTransactionStates[i].returnString);
		PutTransDataBlockIntoPacket(pPacket, &pTransaction->pBaseTransactionStates[i].databaseUpdateData);
		pktSendString(pPacket, pTransaction->pBaseTransactionStates[i].transServerUpdateString);

	}

	pktSend(&pPacket);

}


bool bLocalTransactionsEnabled = true;

AUTO_COMMAND ACMD_COMMANDLINE ACMD_CATEGORY(Debug);
void EnableLocalTransactions(bool enabled)
{
	bLocalTransactionsEnabled = enabled;
}

bool AreLocalTransactionsEnabled(void)
{
	return bLocalTransactionsEnabled;
}

bool GetTransactionMetrics(struct LocalTransactionManager *pManager,
						   U32 *processedBaseTransactions, F32 *averageBaseTransactionSize,
						   U32 *sentTransactions, F32 *averageSentTransactionSize)
{
	if (!pManager)	
		return false;	
	if (processedBaseTransactions)
		*processedBaseTransactions = pManager->totalBaseTransactions;	
	if (averageBaseTransactionSize)
		*averageBaseTransactionSize = (F32)pManager->averageBaseTransactionSize;
	if (sentTransactions)
		*sentTransactions = pManager->totalSentTransactions;	
	if (averageSentTransactionSize)
		*averageSentTransactionSize = (F32)pManager->averageSentTransactionSize;
	return true;
}

char *GetTransactionFailureString(TransactionReturnVal *pRetVal)
{
	int i;

	if (pRetVal->eFlags & TRANSACTIONRETURN_FLAG_TIMED_OUT)
	{
		return "Timed Out";
	}

	for (i=0; i < pRetVal->iNumBaseTransactions; i++)
	{
		if (pRetVal->pBaseReturnVals[i].eOutcome == TRANSACTION_OUTCOME_FAILURE)
		{
			return pRetVal->pBaseReturnVals[i].returnString && pRetVal->pBaseReturnVals[i].returnString[0] ? pRetVal->pBaseReturnVals[i].returnString : "Unspecified";
		}
	}

	return "Unknown";
}


AUTO_COMMAND;
void PingMultiplexer(void)
{
	LocalTransactionManager *pManager = objLocalManager();
	if (!pManager)
	{
		printf("No local manager\n");
		return;
	}

	if (!pManager->pMultiplexLink)
	{
		printf("Local manager not multiplexed\n");
		return;
	}

	LinkToMultiplexer_Ping(pManager->pMultiplexLink);
}

bool OnLocalTransactionManagerThread(LocalTransactionManager *pManager)
{
	return pManager->iThreadID == GetCurrentThreadId();
}

LTM_ThreadData * GetLTMThreadData(void)
{
	LTM_ThreadData *threadData;
	STATIC_THREAD_ALLOC(threadData);
	return threadData;
}

bool LTMLoggingEnabled(void)
{
	return gbLTMLogging;
}

#include "LocalTransactionManager_Internal_h_ast.c"
#include "LocalTransactionManager_c_ast.c"
