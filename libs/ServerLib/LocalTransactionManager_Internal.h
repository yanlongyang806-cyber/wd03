/***************************************************************************



***************************************************************************/

#ifndef _LOCALTRANSACTIONMANAGER_INTERNAL_H_
#define _LOCALTRANSACTIONMANAGER_INTERNAL_H_

#include "earray.h"
#include "estring.h"
#include "GlobalTypeEnum.h"
#include "LocalTransactionManager.h"
#include "logging.h"
#include "loggingEnums.h"
#include "LinkToMultiplexer.h"
#include "NameTable.h"
#include "net/net.h"
#include "TransactionRequestManager.h"


#if _M_X64
#define HANDLE_CACHE_INDEX_BITS 20
#else
#define HANDLE_CACHE_INDEX_BITS 16
#endif

#define MAX_HANDLE_CACHES (1 << HANDLE_CACHE_INDEX_BITS)


#define HANDLE_CACHE_VERIFICATION_BITS 8



#if _M_X64
#define SLOW_TRANSACTION_INDEX_BITS 18
#else
#define SLOW_TRANSACTION_INDEX_BITS 14
#endif

#define MAX_SLOW_TRANSACTIONS (1 << SLOW_TRANSACTION_INDEX_BITS)


#define SLOW_TRANSACTION_VERIFICATION_BITS 8



typedef struct TransactionHandleCache
{
	int iID; //HANDLE_CACHE_INDEX_BITS bits of index, HANDLE_CACHE_VERIFICATION_BITS bits that cycle through for verification

	GlobalType eObjType;

	LTMObjectHandle objHandle;
	LTMObjectFieldsHandle objFieldsHandle;
	const char *pTransName; //for debugging only

	int iNextFree; //-1 terminates, -2 means this cache is currently used

} TransactionHandleCache;

typedef struct SlowTransactionInfo
{
	GlobalType eObjType;
	int iID; 
	int iHandleCacheID;

	//we store both of these handles, one of which is duplicated in the handle cache,
	//so that non-atomic transactions can remove them both when they complete, and
	//atomic transactions can remove the processed handle when they complete
	LTMProcessedTransactionHandle processedTransHandle;
	LTMObjectFieldsHandle objFieldsHandle;

	TransactionID iTransID;
	const char *pTransactionName;
	int iTransIndex;
	bool bRequiresConfirm;
	bool bSucceedAndConfirmIsOK;

	NameTable transVariableTable;

	int iNextFree; //-1 terminates, -2 means this cache is currently used

	//truncated, used only for debugging (so, for instance, if you run out of slow handles you can see
	//what transactions are clogging things up)
	char dbgTransString[128];
} SlowTransactionInfo;

typedef struct LocalBaseTransactionState
{
	enumTransactionOutcome eOutcome;
	char * returnString;
	TransDataBlock databaseUpdateData;
	char * transServerUpdateString;
} LocalBaseTransactionState;

typedef struct LocalTransaction
{
	const char *pTransactionName; //for debugging. must be static or cached, or otherwise unleakable

	//local transactions always have their high bit set, to ensure no naming overlap with server-side transactions
	TransactionID iID;

	enumTransactionType eType;

	bool bAtLeastOneFailure;

	//the next base transaction that should be attempted, for a sequential transaction
	int iCompletionCounter;

	BaseTransaction **ppBaseTransactions;
	LocalBaseTransactionState *pBaseTransactionStates;

	//if 0, no return value requested
	U32 iReturnValID;

	//local transactions are kept in a linked list linked by index. -1 = none
	struct LocalTransaction *pNext;

} LocalTransaction;

typedef struct LocalTransactionManager
{
	//callback setup stuff.
	void *pCBUserData;
	LTMCallBack_DoesObjectExistLocally *pDoesObjectExistCB;
	LTMCallBack_PreProcessTransactionString *pPreProcessTransactionStringCB;
	LTMCallBack_AreFieldsOKToBeLocked *pAreFieldsOKToBeLockedCB;
	LTMCallBack_CanTransactionBeDone *pCanTransactionBeDoneCB;
	LTMCallBack_BeginLock *pBeginLockCB;
	LTMCallBack_ApplyTransaction *pApplyTransactionCB;
	LTMCallBack_ApplyTransactionIfPossible *pApplyTransactionIfPossibleCB;
	LTMCallBack_UndoLock *pUndoLockCB;
	LTMCallBack_CommitAndReleaseLock *pCommitAndReleaseLockCB;
	LTMCallBack_ReleaseObjectFieldsHandle *pReleaseObjectFieldsHandleCB;
	LTMCallBack_ReleaseProcessedTransactionHandle *pReleaseProcessedTransactionHandleCB;
	LTMCallBack_ReleaseString *pReleaseStringCB;
	LTMCallBack_BeginSlowTransaction *pBeginSlowTransactionCB;
	LTMCallBack_ProcessDBUpdateData *pProcessDBUpdateData;
	LTMCallBack_ReleaseDataBlock *pReleaseDataBlockCB;
	LTMCallback_GracefulShutdown *pGracefulShutdownCB;

	bool bDestroying;

	bool bIsFullyLocal;

	bool bCanSurviveLinkDisconnect;

	SlowTransactionInfo **ppSlowTransactions;
	int iFirstFreeSlowTransactionIndex; //-1 if none


	//if pMultiplexLink is set, then use it. Otherwise, use pNetLink
	LinkToMultiplexer *pMultiplexLink;
	NetLink *pNetLink;

	GlobalType eServerType; //the type and ID of the server that this local transaction manager is part of
	U32 iServerID;

	LocalTransaction *pFirstBlocked;
	LocalTransaction *pLastBlocked;
	U32 iNumLocalBlocked;
	U32 iNextLocalTransactionID;

	TransactionID iCurrentlyActiveTransaction;
	NameTable transVariableTable;

	//result from trans server handshaking
	enumTransServerConnectResult eTransServerConnectResult;

	U32 iThreadID;

	double averageBaseTransactionSize;
	U32 totalBaseTransactions;
	double averageSentTransactionSize;
	U32 totalSentTransactions;


	char currentTransactionName[1024];
	bool bDoingLocalTransaction;
	bool bDoingRemoteTransaction;

} LocalTransactionManager;


U32 GetNextLocalTransactionID(LocalTransactionManager *pManager);

bool AttemptLocalTransaction(LocalTransactionManager *pManager, LocalTransaction *pTransaction, LTMObjectHandle *pPreCalcedObjectHandles);

void CopyAndBlockTransaction(LocalTransactionManager *pManager, LocalTransaction *pTransaction);

Packet *CreateLTMPacket(LocalTransactionManager *pManager, int iCmd, PacketTracker *pTracker);

#define CreateLTMPacketWithFunctionNameTracker(pPacket, pManager, iCmd) { static PacketTracker *__pTracker; ONCE(__pTracker = PacketTrackerFind("LTMPacket", 0, __FUNCTION__)); pPacket = CreateLTMPacket(pManager, iCmd, __pTracker); }

static __forceinline bool LTMIsFullyLocal(LocalTransactionManager *pManager) { return pManager->bIsFullyLocal;}

AUTO_ENUM;
typedef enum HandleNewTransOutcome
{
	OUTCOME_COMPLETED,
	OUTCOME_HANDLE_CACHE_FULL,
	OUTCOME_OBJ_DOESNT_EXIST,
	OUTCOME_TRANS_INVALID,
	OUTCOME_FIELDS_LOCKED,
	OUTCOME_TOO_MANY_SLOW,

	OUTCOME_COUNT
} HandleNewTransOutcome;

LATELINK;
void GetLocalTransactionManagerArraySizes(int *piMaxHandleCaches, int *piMaxSlowTransactions);

SlowTransactionInfo *GetEmptySlowTransactionInfo(LocalTransactionManager *pManager);
void SimpleMessageToServer(LocalTransactionManager *pManager, int eMessageType, TransactionID iTransID, int iTransIndex, U32 extraData, char *pReturnValString, TransDataBlock *pDBUpdateData, char *pTransServerUpdateString);
void ReleaseEverything(LocalTransactionManager *pManager, int eObjType, LTMObjectFieldsHandle objFieldsHandle,LTMProcessedTransactionHandle processedTransactionHandle, char **ppReturnString, char **ppTransactString);
void AddHandleNewTransactionTiming(const char *pTransName, U64 iStartingTicks, HandleNewTransOutcome eOutcome);
void AddHandleNewTransactionThreadTiming(const char *pTransName, U64 iForegroundTicks, U64 iBackgroundTicks, U64 iQueueTicks, HandleNewTransOutcome eOutcome);
TransactionHandleCache *AcquireNewHandleCache(GlobalType eObjType, const char *pTransactionName);
bool HandleCacheIsFull(void);
void ReportHandleCacheOverflow(const char *pTransactionName);
#define LTM_LOG(format, ...) log_printf(LOG_LTM, format, __VA_ARGS__); printf(format, __VA_ARGS__);
void ReleaseAllTransVariables(LocalTransactionManager *pManager);
TransactionHandleCache *FindExistingHandleCache(int iID);
void ReleaseHandleCache(TransactionHandleCache **pCache);


typedef struct LTM_ThreadData
{
	bool bDoingRemoteTransaction;
	TransactionID iCurrentlyActiveTransaction;
	NameTable transVariableTable;
} LTM_ThreadData;

LTM_ThreadData * GetLTMThreadData(void);

bool LTMLoggingEnabled(void);

#endif
