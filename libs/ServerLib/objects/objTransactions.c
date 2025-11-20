/***************************************************************************



***************************************************************************/


#include "objTransactions.h"
#include "objTransactionCommands.h"
#include "MemoryPool.h"
#include "AutoTransDefs.h"
#include "ServerLib.h"
#include "StringUtil.h"
#include "logging.h"
#include "ResourceSystem_Internal.h"
#include "stringCache.h"
#include "ThreadSafeMemoryPool.h"
#include "TimedCallback.h"
#include "TransactionRequestManager.h"
#include "AccountStub.h"
#include "autogen/controller_autogen_remotefuncs.h"
#include "alerts.h"
// for isDevelopmentMode
#include "file.h"

void CheckForTooManyActiveTransactions(void);

void objUpdateDeferredAutoTransactions(void);

extern Expression *getActiveExpression(void);
extern char* exprGetCompleteString(Expression* expr);


//if this debug variable is set to true, auto transactions will always run on the server from which they are requested,
//if possible. This is useful for putting breakpoints and debugging. Otherwise, they'll run on the "best" server
bool gbForceAutoTransactionsToRunLocally = false;

AUTO_CMD_INT(gbForceAutoTransactionsToRunLocally, ForceAutoTransactionsToRunLocally);

// Functions for keeping track of Active Transactions

//these flags are both set when the active trans is created. The first one is cleared during the
//active trans loop. The second one is cleared as the trans system is handed the return val
#define ACTIVETRANS_FLAG_NEWLY_ALLOCATED (1 << 0)
#define ACTIVETRANS_FLAG_FIRST_TIME_IN_TRANS_SYSTEM (1 << 1)
#define ACTIVETRANS_FLAG_LAG_ALERTED (1 << 2)
#define ACTIVETRANS_FLAG_MAY_TAKE_LONG_TIME (1 << 3)


//if an active trans takes more than 30 seconds, that's likely enough to be a sign that something is wrong
//that we generate an error/alert
#define ACTIVETRANS_ALERT_TIME 30


typedef struct ActiveTransaction
{
	//MUST BE ARG 0
	TransactionReturnVal builtInReturnVal;
	//MUST BE ARG 0


	TransactionReturnCallback callbackFunc;
	void *userData;
	U32 eFlags;
	U32 iStartTime;
	U32 iTimeOutTime;
} ActiveTransaction;

static ActiveTransaction **gNewActiveTransactions = NULL;
static ActiveTransaction **gActiveTransactions = NULL;
static ActiveTransaction **gTimedOutTransactions = NULL;

MP_DEFINE(ActiveTransaction);


ActiveTransaction *CreateActiveTransaction(void)
{
	MP_CREATE(ActiveTransaction,1024);
	return MP_ALLOC(ActiveTransaction);
}


void DestroyActiveTransaction(ActiveTransaction *at)
{
	objReleaseTransactionReturn(&(at->builtInReturnVal));
	MP_FREE(ActiveTransaction,at);
}

__forceinline TransactionReturnVal *objCreateManagedReturnVal_internal(TransactionReturnCallback func, void *userData, U32 eStartingFlags, U32 iTimeOutDuration)
{
	ActiveTransaction *newTrans = CreateActiveTransaction();
	
	assert(func);

	// This will get cleaned up if it isn't used
	newTrans->builtInReturnVal.eOutcome = TRANSACTION_OUTCOME_UNINITIALIZED; 
	newTrans->builtInReturnVal.pTransactionName = NULL;

	newTrans->userData = userData;
	newTrans->callbackFunc = func;

	newTrans->builtInReturnVal.eFlags = TRANSACTIONRETURN_FLAG_MANAGED_RETURN_VAL;
	newTrans->eFlags = eStartingFlags | ACTIVETRANS_FLAG_NEWLY_ALLOCATED | ACTIVETRANS_FLAG_FIRST_TIME_IN_TRANS_SYSTEM;


	newTrans->iStartTime = timeSecondsSince2000();
	if (iTimeOutDuration)
	{
		newTrans->iTimeOutTime = newTrans->iStartTime + iTimeOutDuration;
	}
	else
	{
		newTrans->iTimeOutTime = 0;
	}

	eaPush(&gNewActiveTransactions,newTrans);

	ManagedReturnValLog(newTrans, "Newly allocated. Callback func: %p", func);

	return &newTrans->builtInReturnVal;
}

TransactionReturnVal *objCreateManagedReturnVal(TransactionReturnCallback func, void *userData)
{
	return objCreateManagedReturnVal_internal(func, userData, 0, 0);
}

TransactionReturnVal *objCreateManagedReturnVal_TransactionMayTakeALongTime(TransactionReturnCallback func, void *userData)
{
	return objCreateManagedReturnVal_internal(func, userData, ACTIVETRANS_FLAG_MAY_TAKE_LONG_TIME, 0);
}

TransactionReturnVal *objCreateManagedReturnVal_WithTimeOut(TransactionReturnCallback func, void *userData, U32 iTimeOutTime)
{
	return objCreateManagedReturnVal_internal(func, userData, ACTIVETRANS_FLAG_MAY_TAKE_LONG_TIME, iTimeOutTime);
}


void ValidateManagedReturnValFirstTimeInTransSystem(const char *pTransactionName, TransactionReturnVal *pReturnVal)
{
	ActiveTransaction *pActiveTrans = (ActiveTransaction*)pReturnVal;

	assertmsgf(pActiveTrans->eFlags & ACTIVETRANS_FLAG_FIRST_TIME_IN_TRANS_SYSTEM, "Someone is calling trans %s with an old managed return value. Michael Powell, is that you?", pTransactionName);

	pActiveTrans->eFlags &= ~ACTIVETRANS_FLAG_FIRST_TIME_IN_TRANS_SYSTEM;
}



static char *sOutcomeStrings[4] =
{
	"NONE",
	"FAILURE",
	"SUCCESS",
	"UNDEFINED",
};


void objTransactionPrintResultCB(TransactionReturnVal *returnVal, void *userData)
{
	int j;
	printf("Transaction returned\n");

	for (j=0; j < returnVal->iNumBaseTransactions; j++)
	{
		printf("%d: %s  \"%s\"\n", j, sOutcomeStrings[returnVal->pBaseReturnVals[j].eOutcome],
			returnVal->pBaseReturnVals[j].returnString ? returnVal->pBaseReturnVals[j].returnString : "NULL");
	}
}

char *objAutoTransactionGetResult(TransactionReturnVal *returnVal)
{
	if (!returnVal || returnVal->iNumBaseTransactions < 1)
	{
		return NULL;
	}
	// The middle transaction is always the auto transaction
	return returnVal->pBaseReturnVals[returnVal->iNumBaseTransactions / 2].returnString;
}

char *objAutoTransactionGetResult_TransWasAppended(int iNumManualSteps, TransactionReturnVal *returnVal)
{
	if (!returnVal || returnVal->iNumBaseTransactions < 1)
	{
		return NULL;
	}
	// The middle transaction is always the auto transaction

	return returnVal->pBaseReturnVals[iNumManualSteps + (returnVal->iNumBaseTransactions-iNumManualSteps) / 2].returnString;
}

// Utility functions to manage the actual TransactionCommand objects

ObjectTransactionManager gObjectTransactionManager = {0};

void AddFieldOperationToTransaction_EStrings(TransactionCommand *holder, ObjectPathOpType op, char*pathEString, char *valEString, bool quotedValue)
{
	ObjectPathOperation *fieldEntry = CreateObjectPathOperation();

	fieldEntry->op = op;

	fieldEntry->pathEString = pathEString;
	fieldEntry->valueEString = valEString;
	

	fieldEntry->quotedValue = quotedValue;

	eaPush(&holder->fieldEntries,fieldEntry);
}

void AddFieldOperationToTransaction_NotEStrings(TransactionCommand *holder, 
	ObjectPathOpType op, 
	const char*path, int iPathLen, 
	const char *val, int iValLen, bool quotedValue)
{
	ObjectPathOperation *fieldEntry = CreateObjectPathOperation();

	fieldEntry->op = op;
	
	if (iPathLen)
	{
		estrConcat(&fieldEntry->pathEString, path, iPathLen);
	}

	if (iValLen)
	{
		estrConcat(&fieldEntry->valueEString, val, iValLen);
	}
	
	fieldEntry->quotedValue = quotedValue;

	eaPush(&holder->fieldEntries,fieldEntry);
}

TSMP_DEFINE(TransactionCommand);

TransactionCommand* CreateTransactionCommand(void)
{
	ATOMIC_INIT_BEGIN;
	TSMP_SMART_CREATE(TransactionCommand, 1024, TSMP_X64_RECOMMENDED_CHUNK_SIZE);
	ATOMIC_INIT_END;

	return TSMP_CALLOC(TransactionCommand);
}

void DestroyTransactionCommand(TransactionCommand *holder)
{
	if (holder->stringData)
	{
		SAFE_FREE(holder->stringData);
	}

	if (holder->fieldEntries)
	{
		eaDestroyEx(&holder->fieldEntries,DestroyObjectPathOperation);
	}

	if (holder->diffString)
	{
		estrDestroy(&holder->diffString);
	}

	estrDestroy(&holder->pTransVarName1);
	estrDestroy(&holder->pTransVarName2);
	estrDestroy(&holder->pTransVarName3);
	estrDestroy(&holder->pTransVarName4);
	estrDestroy(&holder->pTransVarName5);
	estrDestroy(&holder->pTransVarName6);
	estrDestroy(&holder->pTransVarName7);
	estrDestroy(&holder->pTransVarName8);

	if (holder->pLocalContainerInfoCache)
	{
		LocalContainerInfoCache_DecreaseRefCount(holder->pLocalContainerInfoCache);
	}

	TSMP_FREE(TransactionCommand, holder);
}

bool IsContainerOwnedByMe(Container *object)
{
	if (object && object->meta.containerState == CONTAINERSTATE_OWNED)
	{
		return true;
	}
	return false;
}



static bool DoesContainerExist_CB(GlobalType eObjType, U32 iObjID, LTMObjectHandle *pFoundObjectHandle, char **ppReturnValString, void *pUserData)
{
	if (eObjType == objServerType() && iObjID == objServerID())
	{
		*pFoundObjectHandle = NULL;
		return true; //operation on this server, which we know exists
	}
	else
	{
		Container *pObject = objGetContainerEx(eObjType,iObjID, false, false, false);

		if (pObject && IsContainerOwnedByMe(pObject))
		{
			*pFoundObjectHandle = pObject;
			return true;
		}
		else
		{
            // IMPORTANT NOTE: If the output of this printf changes, there is code elsewhere that must be updated.
            // Search all code for OBJ_TRANS_CONTAINER_DOES_NOT_EXIST.
			estrPrintf(ppReturnValString, OBJ_TRANS_CONTAINER_DOES_NOT_EXIST ", GlobalType=%s ID=%u",
					   GlobalTypeToName( eObjType ), iObjID );
			return false;
		}
	}
}

static bool DoesContainerExistLocal_CB(GlobalType eObjType, U32 iObjID, LTMObjectHandle *pFoundObjectHandle, char **ppReturnValString, void *pUserData)
{
	if (eObjType == objServerType()) // only check type here
	{
		*pFoundObjectHandle = NULL;
		return true; //operation on this server, which we know exists
	}
	else
	{
		Container *pObject = objGetContainer(eObjType,iObjID);
		if (pObject) // ignore ownership
		{
			*pFoundObjectHandle = pObject;
			return true;
		}
		else
		{
			estrCopy2(ppReturnValString, OBJ_TRANS_CONTAINER_DOES_NOT_EXIST);
			return false;
		}
	}
}

static enumTransactionValidity PreProcessContainerString_CB(GlobalType eObjType,const char *pTransactionString,
	LTMProcessedTransactionHandle *pProcessedTransactionHandle,
	LTMObjectFieldsHandle *pObjectFieldsHandle, char **ppReturnValString,
	TransactionID iTransactionID, const char *pTransactionName, void *pUserData)
{
	U64 timeStart, timeEnd;
	int result;
	TransactionCommand *command = CreateTransactionCommand();
	
	PERFINFO_AUTO_START("TransactionPreProcess",1);
	
	GET_CPU_TICKS_64(timeStart);

	command->transactionID = iTransactionID;
	command->pTransactionName = pTransactionName;
	
	if (objLocalManager() && IsLocalManagerFullyLocal(objLocalManager()))
	{
		command->bShardExternalCommand = true;
	}

	if (eObjType == objServerType())
	{		
		result = ParseServerTransactionCommand(pTransactionString,command,ppReturnValString);
	}
	else
	{
		command->objectType = eObjType;
		result = ParseObjectTransactionCommand(pTransactionString,command,ppReturnValString);
	}

	if (!result)
	{
		DestroyTransactionCommand(command);
		PERFINFO_AUTO_STOP();
		return TRANSACTION_INVALID;
	}

	estrClear(ppReturnValString);

	*pObjectFieldsHandle = command;

	GET_CPU_TICKS_64(timeEnd);
	command->transactionTime += timeEnd - timeStart;

	if (command->bSlowTransaction)
	{
		PERFINFO_AUTO_STOP();
		return TRANSACTION_VALID_SLOW;
	}
	else
	{
		PERFINFO_AUTO_STOP();
		return TRANSACTION_VALID_NORMAL;
	}
}


static bool IsContainerOKToBeLocked_CB(GlobalType eObjType, LTMObjectHandle objHandle, const char *pTransactionString,
	LTMObjectFieldsHandle objFieldsHandle, U32 iTransactionID, const char *pTransactionName, void *pUserData, U32 *piTransIDCausingBlock)
{
	TransactionCommand *pCommand = (TransactionCommand *)objFieldsHandle;
	Container *baseContainer;
	U64 timeStart, timeEnd;

	PERFINFO_AUTO_START("TransactionCheckLock",1);

	GET_CPU_TICKS_64(timeStart);

	baseContainer = (Container *)objHandle;
	if (baseContainer && (!pCommand->objectID || !pCommand->objectType))
	{
		pCommand->objectType = objGetContainerType(baseContainer);
		pCommand->objectID = objGetContainerID(baseContainer);
	}
	
	GET_CPU_TICKS_64(timeEnd);
	pCommand->transactionTime += timeEnd - timeStart;

	PERFINFO_AUTO_STOP();

	return CanTransactionCommandBeLocked(pCommand,piTransIDCausingBlock);

}

extern FILE *trDebugLogFile;

bool ContainerCanTransactionBeDone_CB(GlobalType eObjType, LTMObjectHandle objHandle,
	const char *pTransactionString, LTMProcessedTransactionHandle processedTransactionHandle,
	LTMObjectFieldsHandle objFieldsHandle, char **ppReturnValString, TransactionID iTransID, const char *pTransactionName, void *pUserData)
{
	TransactionCommand *pCommand = (TransactionCommand *)objFieldsHandle;
	bool val;
	U64 timeStart, timeEnd;

	//filelog_printf("transactions.log","TR%d %s[%d]: %s\n",iTransID,GlobalTypeToName(eObjType),pCommand->objectID,pTransactionString);

	PERFINFO_AUTO_START("TransactionCheckPossible",1);

	GET_CPU_TICKS_64(timeStart);

	val = CanTransactionCommandBeDone(pCommand,ppReturnValString);
	
	GET_CPU_TICKS_64(timeEnd);
	pCommand->transactionTime += timeEnd - timeStart;

	PERFINFO_AUTO_STOP();

	return val;
}

void ContainerBeginLock_CB(GlobalType eObjType, LTMObjectHandle objHandle, LTMObjectFieldsHandle objFieldsHandle,
	U32 iTransactionID, const char *pTransactionName, void *pUserData)
{
	TransactionCommand *pCommand = (TransactionCommand *)objFieldsHandle;
	U64 timeStart, timeEnd;

	PERFINFO_AUTO_START("TransactionLock",1);

	GET_CPU_TICKS_64(timeStart);

	LockTransactionCommand(pCommand);

	GET_CPU_TICKS_64(timeEnd);
	pCommand->transactionTime += timeEnd - timeStart;

	PERFINFO_AUTO_STOP();

	return;
}

int gLogAllTransPerf = 0; // Default to on

AUTO_CMD_INT(gLogAllTransPerf, LogAllTransPerf) ACMD_CATEGORY(Profile) ACMD_ACCESSLEVEL(9) ACMD_CMDLINE;

bool ContainerApplyTransaction_CB(GlobalType eObjType, LTMObjectHandle objHandle, const char *pTransactionString,
	LTMProcessedTransactionHandle processedTransactionHandle, LTMObjectFieldsHandle objFieldsHandle,
	char **ppReturnValString, TransDataBlock *pDatabaseUpdateData, char **ppTransServerUpdateString,
	TransactionID iTransID, const char *pTransactionName, void *pUserData)
{
	U64 timeStart, timeEnd;
	TransactionCommand *pCommand = (TransactionCommand *)objFieldsHandle;
	bool val;
	
	PERFINFO_AUTO_START("TransactionApply",1);

	GET_CPU_TICKS_64(timeStart);
	pCommand->iBytesIn = pCommand->iBytesOut = 0;
	
	val = DoTransactionCommand(pCommand,ppReturnValString,pDatabaseUpdateData,ppTransServerUpdateString);

	GET_CPU_TICKS_64(timeEnd);
	pCommand->transactionTime += timeEnd - timeStart;
	
	PERFINFO_AUTO_STOP();

	return val;
}

void ContainerUndoLock_CB(GlobalType eObjType, LTMObjectHandle objHandle, LTMObjectFieldsHandle objFieldsHandle, U32 iTransactionID,
	void *pUserData)
{
	TransactionCommand *pCommand = (TransactionCommand *)objFieldsHandle;
	U64 timeStart, timeEnd;

	PERFINFO_AUTO_START("TransactionRevert",1);

	GET_CPU_TICKS_64(timeStart);

	RevertTransactionCommand(pCommand);

	GET_CPU_TICKS_64(timeEnd);
	pCommand->transactionTime += timeEnd - timeStart;

	PERFINFO_AUTO_STOP();

	return;
}

void ContainerCommitAndReleaseLock_CB(GlobalType eObjType, LTMObjectHandle objHandle, LTMObjectFieldsHandle objFieldsHandle, U32 iTransactionID,
	void *pUserData)
{
	TransactionCommand *pCommand = (TransactionCommand *)objFieldsHandle;
	U64 timeStart, timeEnd;
	
	PERFINFO_AUTO_START("TransactionCommit",1);

	GET_CPU_TICKS_64(timeStart);

	CommitTransactionCommand(pCommand);

	GET_CPU_TICKS_64(timeEnd);
	pCommand->transactionTime += timeEnd - timeStart;

	PERFINFO_AUTO_STOP();

	return;
}

void ContainerReleaseString_CB(GlobalType eObjType, char *pReturnValString, void *pUserData)
{
	PERFINFO_AUTO_START("FreeString",1);
	if (pReturnValString)
	{
		estrDestroy(&pReturnValString);
	}
	PERFINFO_AUTO_STOP();
}

void ContainerReleaseDataBlock_CB(GlobalType eObjType, TransDataBlock *pData, void *pUserData)
{
	PERFINFO_AUTO_START("FreeDataBlock",1);
	if (pData)
	{
		TransDataBlockClear(pData);
	}
	PERFINFO_AUTO_STOP();
}


void ContainerReleaseFieldsHolder_CB(GlobalType eObjType, LTMObjectFieldsHandle objFieldsHandle,
	void *pUserData)
{
	TransactionCommand *pCommand = (TransactionCommand *)objFieldsHandle;
	PERFINFO_AUTO_START("FreeHolder",1);
	if (pCommand)
	{
		if (gLogAllTransPerf)
		{
			PERFINFO_AUTO_START("Performance Logging",1);

			objLog(LOG_TRANSPERF, eObjType, pCommand->objectID, 0, NULL, NULL, NULL, "TransApply", NULL, 
				"Name %s ID %d Command %s Success %d Ticks %"FORM_LL"d BytesIn %d BytesOut %d",
				pCommand->pTransactionName, pCommand->transactionID, pCommand->parseCommand ? pCommand->parseCommand->name: "None", pCommand->commandState == TRANSTATE_COMMITTED, pCommand->transactionTime, pCommand->iBytesIn, pCommand->iBytesOut);

			PERFINFO_AUTO_STOP();
		}
		if (pCommand->slowTransactionID)
		{
			TransactionCommand *dummy;
			stashIntRemovePointer(gObjectTransactionManager.slowTransactions,pCommand->slowTransactionID,&dummy);
		}
		DestroyTransactionCommand(pCommand);
	}
	PERFINFO_AUTO_STOP();
}

void ContainerBeginSlowTransaction_CB(GlobalType eObjType, LTMObjectHandle objHandle,
	bool bTransactionRequiresLockAndConfirm, char *pTransactionString,
	LTMProcessedTransactionHandle processedTransactionHandle, LTMObjectFieldsHandle objFieldsHandle,
	U32 iTransactionID, const char *pTransactionName, LTMSlowTransactionID slowTransactionID, void *pUserData)
{
	U64 timeStart, timeEnd;
	TransactionCommand *pCommand;

	PERFINFO_AUTO_START("TransactionBeginSlow",1);

	GET_CPU_TICKS_64(timeStart);

	pCommand = (TransactionCommand *)objFieldsHandle;
	pCommand->slowTransactionID = slowTransactionID;
	pCommand->slowReturnString = NULL;
	TransDataBlockInit(&pCommand->slowDatabaseReturnData);
	pCommand->slowTransReturnString = NULL;

	CanTransactionCommandBeDone(pCommand,&pCommand->slowReturnString);

	if (pCommand->commandState == TRANSTATE_DISALLOWED)
	{
		pCommand->commandState = TRANSTATE_UNKNOWN;
		SlowTransactionCompleted(objLocalManager(),pCommand->slowTransactionID,
			SLOWTRANSACTION_FAILED,pCommand->slowReturnString,&pCommand->slowDatabaseReturnData,pCommand->slowTransReturnString);

		GET_CPU_TICKS_64(timeEnd);
		pCommand->transactionTime += timeEnd - timeStart;

		PERFINFO_AUTO_STOP();
		return;
	}

	AddTrackedSlowCommand(pCommand);

	LockTransactionCommand(pCommand);

	DoTransactionCommand(pCommand,&pCommand->slowReturnString,&pCommand->slowDatabaseReturnData,&pCommand->slowTransReturnString);

	if (pCommand->commandState == TRANSTATE_FAILED)
	{
		pCommand->commandState = TRANSTATE_UNKNOWN;
		SlowTransactionCompleted(objLocalManager(),pCommand->slowTransactionID,
			SLOWTRANSACTION_FAILED,pCommand->slowReturnString,&pCommand->slowDatabaseReturnData,pCommand->slowTransReturnString);

		GET_CPU_TICKS_64(timeEnd);
		pCommand->transactionTime += timeEnd - timeStart;

		PERFINFO_AUTO_STOP();
		return;
	}

	if (pCommand->commandState == TRANSTATE_EXECUTED)
	{
		pCommand->commandState = TRANSTATE_UNKNOWN;
		SlowTransactionCompleted(objLocalManager(),pCommand->slowTransactionID,
			SLOWTRANSACTION_SUCCEEDED,pCommand->slowReturnString,&pCommand->slowDatabaseReturnData,pCommand->slowTransReturnString);		
		
		GET_CPU_TICKS_64(timeEnd);
		pCommand->transactionTime += timeEnd - timeStart;

		PERFINFO_AUTO_STOP();
		return;
	}

	GET_CPU_TICKS_64(timeEnd);
	pCommand->transactionTime += timeEnd - timeStart;

	PERFINFO_AUTO_STOP();
	// Otherwise, it's now in a waiting state, and will be completed later
}

bool InitObjectTransactionManagerEx(GlobalType serverType, U32 serverID, char * hostName, int hostPort, bool useMultiplex, char **ppErrorString, PacketCallback *pPacketCB)
{
	gObjectTransactionManager.localManager =  CreateAndRegisterLocalTransactionManager(
		NULL,
		&DoesContainerExist_CB,
		&PreProcessContainerString_CB,
		&IsContainerOKToBeLocked_CB,
		&ContainerCanTransactionBeDone_CB,
		&ContainerBeginLock_CB,
		&ContainerApplyTransaction_CB,
		NULL,
		&ContainerUndoLock_CB,
		&ContainerCommitAndReleaseLock_CB,
		&ContainerReleaseFieldsHolder_CB,
		NULL,
		&ContainerReleaseString_CB,
		&ContainerBeginSlowTransaction_CB,
		&ContainerReleaseDataBlock_CB,
		pPacketCB,
		serverType, serverID,hostName,hostPort,useMultiplex, false, ppErrorString);

	if (!gObjectTransactionManager.localManager)
	{	
		return false;
	}

	gObjectTransactionManager.serverType = serverType;
	gObjectTransactionManager.serverID = serverID;

	InitTransactionRequestManager();
	InitializeHandleCache();

	RegisterObjectWithTransactionServer(objLocalManager(),serverType,serverID);
	
	RemoteCommand_RequestAutoSettings(GLOBALTYPE_CONTROLLER, 0, serverType, serverID);

	ServerLib_ConnectedToTransServer();

	return true;
}

bool InitObjectLocalTransactionManager(GlobalType serverType, char **ppErrorString)
{
	gObjectTransactionManager.localManager =  CreateAndRegisterLocalTransactionManager(
		NULL,
		&DoesContainerExistLocal_CB,
		&PreProcessContainerString_CB,
		&IsContainerOKToBeLocked_CB,
		&ContainerCanTransactionBeDone_CB,
		&ContainerBeginLock_CB,
		&ContainerApplyTransaction_CB,
		NULL,
		&ContainerUndoLock_CB,
		&ContainerCommitAndReleaseLock_CB,
		&ContainerReleaseFieldsHolder_CB,
		NULL,
		&ContainerReleaseString_CB,
		&ContainerBeginSlowTransaction_CB,
		&ContainerReleaseDataBlock_CB,
		NULL,
		serverType, 0,NULL,0,false, true, ppErrorString);

	if (!gObjectTransactionManager.localManager)
	{	
		return false;
	}

	gObjectTransactionManager.serverType = serverType;

	InitTransactionRequestManager();
	InitializeHandleCache();

	RegisterObjectWithTransactionServer(objLocalManager(),serverType,0);
	
	ServerLib_ConnectedToTransServer();

	return true;
}

//after a lagged transaction happens, group them together for the next n seconds, 
//then report them all at once
#define LOCAL_LAG_GROUP_TIME 600

static StashTable sLaggedActiveTransByName = NULL;
typedef struct LaggedTransactionTracker
{
	const char *pTransName;
	int iCount;
} 
LaggedTransactionTracker;

void ReportGroupedLaggedTransactions(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	LaggedTransactionTracker *pTracker = (LaggedTransactionTracker*)userData;

	if (pTracker->iCount == 0)
	{
		stashRemovePointer(sLaggedActiveTransByName, pTracker->pTransName, NULL);
		free(pTracker);
		return;
	}

	if (objLocalManager())
	{
		ReportLaggedTransactions(objLocalManager(), pTracker->pTransName, pTracker->iCount, LOCAL_LAG_GROUP_TIME);
	}

	pTracker->iCount = 0;
	TimedCallback_Run(ReportGroupedLaggedTransactions, pTracker, LOCAL_LAG_GROUP_TIME);
}


void AlertLaggedActiveTrans(ActiveTransaction *pActiveTrans, U32 iCurTime)
{
	const char *pTransName;
	LaggedTransactionTracker *pTracker;

	if (pActiveTrans->builtInReturnVal.pTransactionName)
	{
		pTransName = pActiveTrans->builtInReturnVal.pTransactionName;
	}
	else
	{
		pTransName = allocAddString(STACK_SPRINTF("UNKNOWN CB:%p", pActiveTrans->callbackFunc));
	}

	if (!sLaggedActiveTransByName)
	{
		sLaggedActiveTransByName = stashTableCreateAddress(4);
	}

	if (stashFindPointer(sLaggedActiveTransByName, pTransName, &pTracker))
	{
		pTracker->iCount++;
		return;
	}

	if (objLocalManager())
	{
		ReportLaggedTransactions(objLocalManager(), pTransName, 1, 0);
	}

	pTracker = calloc(sizeof(LaggedTransactionTracker), 1);
	pTracker->pTransName = pTransName;
	stashAddPointer(sLaggedActiveTransByName, pTransName, pTracker, false);
	TimedCallback_Run(ReportGroupedLaggedTransactions, pTracker, LOCAL_LAG_GROUP_TIME);
}

void UpdateObjectTransactionManager(void)
{
	U32 iCurTime;
	int i;

	if (!objLocalManager())
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	iCurTime = timeSecondsSince2000();


	UpdateLocalTransactionManager(objLocalManager());

	if( gObjectTransactionManager.slowTransactions &&
		stashGetCount(gObjectTransactionManager.slowTransactions))
	{
		StashElement elem;
		StashTableIterator iter;
		
		PERFINFO_AUTO_START("SlowTransactions",1);
		stashGetIterator(gObjectTransactionManager.slowTransactions, &iter);
		while (stashGetNextElement(&iter, &elem))
		{
			TransactionCommand *pCommand = stashElementGetPointer(elem);
			if (pCommand->commandState == TRANSTATE_WAITING)
			{
				TryWaitingTransactionCommand(pCommand,&pCommand->slowReturnString,&pCommand->slowDatabaseReturnData,&pCommand->slowTransReturnString);
			}
			if (pCommand->commandState == TRANSTATE_FAILED)
			{
				pCommand->commandState = TRANSTATE_UNKNOWN;
				SlowTransactionCompleted(objLocalManager(),pCommand->slowTransactionID,
					SLOWTRANSACTION_FAILED,pCommand->slowReturnString,&pCommand->slowDatabaseReturnData,pCommand->slowTransReturnString);
			}
			else if (pCommand->commandState == TRANSTATE_EXECUTED)
			{
				pCommand->commandState = TRANSTATE_UNKNOWN;
				SlowTransactionCompleted(objLocalManager(),pCommand->slowTransactionID,
					SLOWTRANSACTION_SUCCEEDED,pCommand->slowReturnString,&pCommand->slowDatabaseReturnData,pCommand->slowTransReturnString);				
			}
		}
		PERFINFO_AUTO_STOP();
	}

//do these things in a slightly funny order so that ir objUpdateDeferredAutoTransactions() spawns more deferred transactions,
//we won't try to process the managed return value this frame, since that would hit the "trans never started" error
	eaPushEArray(&gActiveTransactions, &gNewActiveTransactions);
	eaDestroy(&gNewActiveTransactions);

	objUpdateDeferredAutoTransactions();

	if(eaSize(&gActiveTransactions))
	{

		
		PERFINFO_AUTO_START("ActiveTransactions",1);

		for (i = 0; i < eaSize(&gActiveTransactions); i++)
		{
			ActiveTransaction *at = gActiveTransactions[i];
			
			if (at->eFlags &  ACTIVETRANS_FLAG_NEWLY_ALLOCATED)
			{
				at->eFlags &= ~ACTIVETRANS_FLAG_NEWLY_ALLOCATED;
				ManagedReturnValLog(at, "First time through update. Trans name: %s", at->builtInReturnVal.pTransactionName ? at->builtInReturnVal.pTransactionName : "NULL");
			}

			if (at->builtInReturnVal.eOutcome != TRANSACTION_OUTCOME_NONE)
			{
				if (at->builtInReturnVal.eOutcome != TRANSACTION_OUTCOME_UNINITIALIZED)
				{
					ManagedReturnValLog(at, "Trans completed. Outcome: %s", StaticDefineIntRevLookup(enumTransactionOutcomeEnum, at->builtInReturnVal.eOutcome));


					assertmsgf(at->builtInReturnVal.eOutcome == TRANSACTION_OUTCOME_SUCCESS || TRANSACTION_OUTCOME_FAILURE, "An activeTransaction (trans name %s?) seems to have gotten memory stomped", at->builtInReturnVal.pTransactionName);

					PERFINFO_AUTO_START("Calling Callback Func", 1);

					at->callbackFunc(&at->builtInReturnVal,at->userData);

					PERFINFO_AUTO_STOP(); // Calling Callback Func
				}
				else
				{
					ManagedReturnValLog(at, "Trans never started");
				}

				eaRemoveFast(&gActiveTransactions,i);
				DestroyActiveTransaction(at);
				i--;			
			}
			else
			{
				if (iCurTime - at->iStartTime > ACTIVETRANS_ALERT_TIME && !(at->eFlags & (ACTIVETRANS_FLAG_LAG_ALERTED | ACTIVETRANS_FLAG_MAY_TAKE_LONG_TIME)))
				{
					at->eFlags |= ACTIVETRANS_FLAG_LAG_ALERTED;

					AlertLaggedActiveTrans(at, iCurTime);
				}

				if (at->iTimeOutTime && at->iTimeOutTime < iCurTime)
				{
					at->builtInReturnVal.eOutcome = TRANSACTION_OUTCOME_FAILURE;
					at->callbackFunc(&at->builtInReturnVal,at->userData);
					at->builtInReturnVal.eOutcome = TRANSACTION_OUTCOME_NONE;

					eaRemoveFast(&gActiveTransactions,i);
					i--;	

					eaPush(&gTimedOutTransactions, at);

					ManagedReturnValLog(at, "Transaction timed out.");

				}
			}
		}
		PERFINFO_AUTO_STOP();

		CheckForTooManyActiveTransactions();

	}
	
	PERFINFO_AUTO_START("TimedOutTransactions",1);
	for (i = eaSize(&gTimedOutTransactions) - 1; i >= 0; i--)
	{
		ActiveTransaction *at = gTimedOutTransactions[i];

		if (at->builtInReturnVal.eOutcome != TRANSACTION_OUTCOME_NONE)
		{
			ManagedReturnValLog(at, "Already-timed-out Trans completed. Outcome: %s", StaticDefineIntRevLookup(enumTransactionOutcomeEnum, at->builtInReturnVal.eOutcome));

			eaRemoveFast(&gTimedOutTransactions,i);
			DestroyActiveTransaction(at);
		}	
	}
	PERFINFO_AUTO_STOP();


	PERFINFO_AUTO_STOP();
}

void AddTrackedSlowCommand(TransactionCommand *command)
{
	if (!gObjectTransactionManager.slowTransactions)
	{
		gObjectTransactionManager.slowTransactions = stashTableCreateInt(100);
	}
	if (!command->slowTransactionID)
	{
		return;
	}
	stashIntAddPointer(gObjectTransactionManager.slowTransactions,command->slowTransactionID,command,false);
}

TransactionCommand *GetTrackedSlowCommand(LTMSlowTransactionID id)
{
	TransactionCommand *returnCommand;
	if (!gObjectTransactionManager.slowTransactions || !id)
	{
		return NULL;
	}
	if (stashIntFindPointer(gObjectTransactionManager.slowTransactions,id,&returnCommand))
	{
		return returnCommand;
	}
	return NULL;
}

int ReturnSlowCommand(SlowRemoteCommandID id, bool success, const char *returnString)
{	
	TransactionCommandState state;
	TransactionCommand *pCommand;
	if (success) 
	{	
		state = TRANSTATE_EXECUTED;
	}
	else 
	{
		state = TRANSTATE_FAILED;
	}

	pCommand = GetTrackedSlowCommand(id);
	if (pCommand && pCommand->commandState == TRANSTATE_WAITING)
	{
		if (returnString && returnString[0])
		{
			estrCopy2(&pCommand->slowReturnString,returnString);
		}
		pCommand->commandState = state;

		return 1;
	}
	return 0;
}

void objShutdownTransactions(void)
{
	DestroyLocalTransactionManager(objLocalManager());
	gObjectTransactionManager.localManager = NULL;
}

void objReleaseTransactionReturn(TransactionReturnVal* returnStruct)
{
	ReleaseReturnValData(objLocalManager(),returnStruct);
}

TransactionRequest *objCreateTransactionRequest(void)
{
	return StructCreate(parse_TransactionRequest);
}

void objDestroyTransactionRequest(TransactionRequest *pRequest)
{
	StructDestroy(parse_TransactionRequest, pRequest);
}

void objAddToTransactionRequest(TransactionRequest *request, GlobalType destinationType, U32 destinationID,
								 const char *transactionVariableList, const char *transactionString)
{
	BaseTransaction *baseTransaction = NULL;

	PERFINFO_AUTO_START_FUNC();
	baseTransaction = StructCreate(parse_BaseTransaction);
	baseTransaction->recipient.containerType = destinationType;
	baseTransaction->recipient.containerID = destinationID;
	baseTransaction->pRequestedTransVariableNames = StructAllocString(transactionVariableList);
	baseTransaction->pData = StructAllocString(transactionString);
	eaPush(&request->ppBaseTransactions, baseTransaction);
	PERFINFO_AUTO_STOP();
}

void objAddToTransactionRequestf(TransactionRequest *request, GlobalType destinationType, U32 destinationID,
								  const char *transactionVariableList, const char *transactionString, ...)
{
	va_list va;
	char *commandstr = NULL;	

	PERFINFO_AUTO_START_FUNC();
	estrStackCreate(&commandstr);

	va_start( va, transactionString );
	estrConcatfv(&commandstr,transactionString,va);
	va_end( va );

	objAddToTransactionRequest(request, destinationType, destinationID, transactionVariableList, commandstr);

	estrDestroy(&commandstr);
	PERFINFO_AUTO_STOP();
}


void objAddToAutoTransactionRequestf(TransactionRequest *request, GlobalType destinationType, U32 destinationID,
								  char *transactionVariableList, const char *transactionString, ...)
{
	va_list va;
	BaseTransaction *baseTransaction = StructCreate(parse_BaseTransaction);
	char *commandstr = NULL;	
	char *varstr = NULL;

	PERFINFO_AUTO_START_FUNC();
	estrCreate(&commandstr);
	estrCreate(&varstr);

	if (transactionVariableList) estrPrintf(&varstr, "%s", transactionVariableList);

	va_start( va, transactionString );
	estrConcatfv(&commandstr,transactionString,va);
	va_end( va );

	baseTransaction->recipient.containerType = destinationType;
	baseTransaction->recipient.containerID = destinationID;
	baseTransaction->pRequestedTransVariableNames = varstr;
	baseTransaction->pData = commandstr;

	eaPush(&request->ppBaseTransactions, baseTransaction);
	PERFINFO_AUTO_STOP();
}


int objRequestTransaction(TransactionReturnVal* returnStruct, char *pTransactionName, TransactionRequest *request)
{
	return objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC,returnStruct, pTransactionName, request);
}

int objRequestTransaction_Flagged(enumTransactionType type, TransactionReturnVal* returnStruct, char *pTransactionName, TransactionRequest *request)
{
	if (!request)
	{
		return 0;
	}
	devassertmsgf(!GetTransactionCurrentlyHappening(objLocalManager()), "Cannot request transaction (%s) while inside another transaction (%s)! Will corrupt data.",pTransactionName, GetTransactionCurrentlyHappening(objLocalManager()));
	RequestNewTransaction(objLocalManager(), pTransactionName, request->ppBaseTransactions, type, returnStruct, 0);
	return 1;
}

//simple function... converts "foo arg arg arg arg" into "foo", and "remotecommand foo arg arg arg" into "foo"
char *GuessTransactionName(const char *pStr)
{
	static char *pRetVal = NULL;

	PERFINFO_AUTO_START_FUNC();
	estrCopy2(&pRetVal, pStr);
	if (strStartsWith(pRetVal, "remotecommand "))
	{
		estrRemove(&pRetVal, 0, 14);
	}

	estrTruncateAtFirstOccurrence(&pRetVal, ' ');
	PERFINFO_AUTO_STOP();
	return pRetVal;
}

void objRequestTransactionSimple(TransactionReturnVal* returnStruct, GlobalType destinationType, U32 destinationID, const char *pTransactionName, const char *transactionString)
{
	PERFINFO_AUTO_START_FUNC();
	if (!pTransactionName)
	{
		pTransactionName = GuessTransactionName(transactionString);
	}

	if (returnStruct)
	{
		RequestSimpleTransaction(objLocalManager(), destinationType, destinationID, pTransactionName, transactionString, 
			TRANS_TYPE_SIMULTANEOUS, returnStruct);
	}
	else
	{
		RequestSimpleTransaction(objLocalManager(), destinationType, destinationID, pTransactionName, transactionString, 
			TRANS_TYPE_SIMULTANEOUS, NULL);
	}
	PERFINFO_AUTO_STOP();
}

#undef objRequestTransactionSimplef
void objRequestTransactionSimplef(TransactionReturnVal* returnStruct, GlobalType destinationType, U32 destinationID, const char *pTransctionName, FORMAT_STR const char *transactionString, ...)
{
	va_list va;
	char *commandstr = NULL;	

	PERFINFO_AUTO_START_FUNC();
	estrStackCreate(&commandstr);

	va_start( va, transactionString );
	estrConcatfv(&commandstr,transactionString,va);
	va_end( va );

	objRequestTransactionSimple(returnStruct,destinationType,destinationID,pTransctionName, commandstr);

	estrDestroy(&commandstr);
	PERFINFO_AUTO_STOP();
}

extern FILE *trDebugLogFile;


int objRequestAutoTransactionf(TransactionRequest *pTransactionRequest_In, TransactionReturnVal* returnStruct, GlobalType eServerTypeToRunOn, ContainerID iServerIDToRunOn, const char *query, ...)
{
	va_list va;
	static char *commandstr = NULL;
	int returnVal;

	PERFINFO_AUTO_START_FUNC();
	estrClear(&commandstr);

	va_start( va, query );
	estrConcatfv(&commandstr,query,va);
	va_end( va );

	returnVal = objRequestAutoTransaction(pTransactionRequest_In, returnStruct, eServerTypeToRunOn, iServerIDToRunOn, commandstr);
	PERFINFO_AUTO_STOP();
	return returnVal;
}

static void DestroyAutoTransArgs(AutoTransContainer **containers)
{
	int i;
	PERFINFO_AUTO_START_FUNC();

	for (i = 0; i < eaSize(&containers); i++)
	{
		int j;
		for (j = 0; j < eaSize(&containers[i]->lockedFields); j++)
		{
			estrDestroy(&containers[i]->lockedFields[j]);
		}
		eaDestroy(&containers[i]->lockedFields);
		ea32Destroy(&containers[i]->lockTypes);

		ea32Destroy(&containers[i]->pContainerIDs);

		free(containers[i]);
	}
	eaDestroy(&containers);

	PERFINFO_AUTO_STOP();
}

bool FixAutoPath(char **result, char *path, ParseTable *baseTable)
{
	int i = 0;
	char *tempPath = NULL;

	PERFINFO_AUTO_START_FUNC();

	estrStackCreate(&tempPath);
	estrClear(result);
	
	while (path[i] != '\0')
	{
		if (path[i] == '-' && path[i+1] == '>')
		{
			estrConcatChar(&tempPath,'.');
			i++;i++;
		}
		else
		{
			if (i == 0 && path[0] != '.')
			{
				estrConcatChar(&tempPath,'.');
			}
			estrConcatChar(&tempPath,path[i]);
			i++;
		}
	}

	if (objPathNormalize(tempPath,baseTable,result))
	{
		estrDestroy(&tempPath);
		PERFINFO_AUTO_STOP();
		return true;
	}
	else
	{
		estrDestroy(&tempPath);
		PERFINFO_AUTO_STOP();
		return false;
	}
}




typedef struct DeferredAutoTrans
{
	TransactionReturnVal *pReturnStruct;
	GlobalType eServerTypeToRunOn;
	ContainerID iServerIDToRunOn;
	char *pQuery; //strduped
} DeferredAutoTrans;

DeferredAutoTrans **gppDeferredTransactions = NULL;


int objRequestAutoTransactionDeferred(TransactionReturnVal* returnStruct, GlobalType eServerTypeToRunOn, ContainerID iServerIDToRunOn, const char *query)
{
	DeferredAutoTrans *pDeferred = malloc(sizeof(DeferredAutoTrans));
	pDeferred->pReturnStruct = returnStruct;
	pDeferred->eServerTypeToRunOn = eServerTypeToRunOn;
	pDeferred->pQuery = strdup(query);
	pDeferred->iServerIDToRunOn = iServerIDToRunOn;

	eaPush(&gppDeferredTransactions, pDeferred);

	return 1;
}

void objUpdateDeferredAutoTransactions(void)
{
	DeferredAutoTrans **sppListCopy = NULL;
	int i;

	if (!eaSize(&gppDeferredTransactions))
	{
		return;
	}

	//need to copy the list and use a copy to avoid corruption
	eaCopy(&sppListCopy, &gppDeferredTransactions);
	eaDestroy(&gppDeferredTransactions);


	for (i=0; i < eaSize(&sppListCopy); i++)
	{
		objRequestAutoTransaction(NULL, sppListCopy[i]->pReturnStruct, sppListCopy[i]->eServerTypeToRunOn, sppListCopy[i]->iServerIDToRunOn, sppListCopy[i]->pQuery);
		free(sppListCopy[i]->pQuery);
		free(sppListCopy[i]);
	}

	eaDestroy(&sppListCopy);
}


void FindStaticDefineForEarrayUse(ATRContainerArgDef *pContainerArg, ATRFixedUpEarrayUse *pEarrayUse)
{
	char *pTempPath = NULL;

	ParseTable *pFoundTable;
	int iFoundColumn;
	void *pFoundStruct;
	int iFoundIndex;
	ParseTable *pActualTable;
	int iKeyColumn;

	estrStackCreate(&pTempPath);
	estrPrintf(&pTempPath, ".%s", pEarrayUse->pContainerDerefString);

	if (!objPathResolveField(pTempPath, pContainerArg->pParseTable, NULL, 
					   &pFoundTable, &iFoundColumn, &pFoundStruct, &iFoundIndex, 
						0))
	{
		devassertmsgf(0, "Unable to process path %s in table %s while doing static define lookup for an auto trans, very troublesome",
			pTempPath, ParserGetTableName(pContainerArg->pParseTable));
		goto NoStaticDefine;
	}
	
	//this resolves to the earray in the parent
	if (!(TOK_HAS_SUBTABLE(pFoundTable[iFoundColumn].type) && 
		TokenStoreStorageTypeIsEArray(TokenStoreGetStorageType(pFoundTable[iFoundColumn].type))))
	{
		devassertmsgf(0, "While processing path %s in table %s to do static define lookup for an auto trans, resolved our path but didn't get an earray of containers... very troublesome",
			pTempPath, ParserGetTableName(pContainerArg->pParseTable));
		goto NoStaticDefine;
	}

	pActualTable = pFoundTable[iFoundColumn].subtable;
	iKeyColumn = ParserGetTableKeyColumn(pActualTable);
	if (iKeyColumn == -1)
	{
		goto NoStaticDefine;
	}

	if (TypeIsInt(TOK_GET_TYPE(pActualTable[iKeyColumn].type)) && pActualTable[iKeyColumn].subtable)
	{
		pEarrayUse->pStaticDefineForKey = pActualTable[iKeyColumn].subtable;
		estrDestroy(&pTempPath);
		return;
	}


NoStaticDefine:
	pEarrayUse->pStaticDefineForKey = ATRFIXEDUPEARRAYUSE_NO_STATICDEFINE;
	estrDestroy(&pTempPath);

}

const char *ConvertIntEarrayLockToEnum(ATRContainerArgDef *pContainerArg, ATRFixedUpEarrayUse *pEarrayUse, int iVal)  
{
	if (pEarrayUse->pStaticDefineForKey == ATRFIXEDUPEARRAYUSE_NO_STATICDEFINE)
	{
		return NULL;
	}
	else if (pEarrayUse->pStaticDefineForKey == NULL)
	{
		FindStaticDefineForEarrayUse(pContainerArg, pEarrayUse);
		if (pEarrayUse->pStaticDefineForKey == ATRFIXEDUPEARRAYUSE_NO_STATICDEFINE)
		{
			return NULL;
		}
	}

	return StaticDefineInt_FastIntToString(pEarrayUse->pStaticDefineForKey, iVal);
}

const char *ConvertStringEarrayLockToEnum(ATRContainerArgDef *pContainerArg, ATRFixedUpEarrayUse *pEarrayUse, char *pStrVal)  
{
	int iInt;
	int iUInt;
	char *pTempCopy = NULL;

	if (pEarrayUse->pStaticDefineForKey == ATRFIXEDUPEARRAYUSE_NO_STATICDEFINE)
	{
		return NULL;
	}
	else if (pEarrayUse->pStaticDefineForKey == NULL)
	{
		FindStaticDefineForEarrayUse(pContainerArg, pEarrayUse);
		if (pEarrayUse->pStaticDefineForKey == ATRFIXEDUPEARRAYUSE_NO_STATICDEFINE)
		{
			return NULL;
		}
	}

	//the string may have already been quoted... need to check for that
	if (pStrVal[0] == '"')
	{
		estrStackCreate(&pTempCopy);
		estrCopy2(&pTempCopy, pStrVal + 1);
		estrSetSize(&pTempCopy, estrLength(&pTempCopy) - 1);

		if (StringToInt_Paranoid(pTempCopy, &iInt))
		{
			estrDestroy(&pTempCopy);
			return StaticDefineInt_FastIntToString(pEarrayUse->pStaticDefineForKey, iInt);
		}

		if (StringToUint_Paranoid(pTempCopy, &iUInt))
		{
			estrDestroy(&pTempCopy);
			return StaticDefineInt_FastIntToString(pEarrayUse->pStaticDefineForKey, iUInt);
		}

		estrDestroy(&pTempCopy);
		return NULL;
	}
	else
	{
		if (StringToInt_Paranoid(pStrVal, &iInt))
		{
			return StaticDefineInt_FastIntToString(pEarrayUse->pStaticDefineForKey, iInt);
		}

		if (StringToUint_Paranoid(pStrVal, &iUInt))
		{
			return StaticDefineInt_FastIntToString(pEarrayUse->pStaticDefineForKey, iUInt);
		}

		return NULL;
	}

}

// not thread/recursion safe
//
//if pTransactionRequest_In is non-NULL, appends transaction lines to it and does NOT call the transaction. If it is NULL,
//creates one internally and calls the transaction.
int objRequestAutoTransaction(TransactionRequest *pTransactionRequest_In, TransactionReturnVal* returnStruct, GlobalType eServerTypeToRunOn, ContainerID iServerIDToRunOn, const char *query)
{
	U64 timeStart, timeEnd;
	static char *queryCopy;
	char *args[1000];
	AutoTransContainer **outArgs = NULL;
	ATR_FuncDef *trans = NULL;
	int argCount = 0;
	int i;
	int transactArraySize = 0;
	int curArg = 0;
	char *pRunCommandVarString = NULL;
	char *pRunCommandString = NULL;
	BaseTransaction *pMasterBaseTrans;
	int iContainerArgNum = 0;
	char serverTypeToRunOn[256];

	TransactionRequest *pTransactionRequestToUse;

	if (!query || !query[0])
	{
		return 0;
	}

	sprintf(serverTypeToRunOn, "%s[%u]", GlobalTypeToName(eServerTypeToRunOn), iServerIDToRunOn);

	if(IsThisObjectDB())
	{
		Errorf("Auto transactions may not be initiated from the ObjectDB!");
		devassertmsg(0, "Auto transactions may not be initiated from the ObjectDB!");
		return 0;
	}
	if(GetTransactionCurrentlyHappening(objLocalManager()))
	{
		Errorf("Cannot request auto transaction (%s) while inside another transaction (%s)! Possible corrupt data!",query ? query : "<No Query>", GetTransactionCurrentlyHappening(objLocalManager()));
		devassertmsg(0, "Cannot request auto transaction while inside another transaction! Will corrupt data");
	}
       
	if(getActiveExpression())
	{
		ErrorDetailsf("%s", query ? query : "<No Query>");
		Errorf("Cannot request auto transaction (%s) while inside an expression (%s)!  Transaction will not happen in dev mode.", 
			returnStruct && returnStruct->pTransactionName ? returnStruct->pTransactionName : "<See Query in Details>", 
			exprGetCompleteString(getActiveExpression()));
		if (isDevelopmentMode())
		{
			// Don't let this even pretend to work in development mode, 
			// but let it pass in production to avoid breaking anything that works right now
			return 0; 
		}
	}

	GET_CPU_TICKS_64(timeStart);

	if (pTransactionRequest_In)
	{
		pTransactionRequestToUse = pTransactionRequest_In;
	}
	else
	{
		pTransactionRequestToUse = StructCreate(parse_TransactionRequest);
	}



	ATR_DoLateInitialization();


	estrCopy2(&queryCopy,query);

	argCount = TokenizeLineRespectingStrings(args, queryCopy) - 1;
	//argCount = tokenize_line_quoted(queryCopy,args,NULL) - 1;


	trans = FindATRFuncDef(args[0]);

	if (!trans)
	{		
		return 0;
	}

	PERFINFO_AUTO_START("1", 1);

	for (i = 0; i < argCount; i++)
	{
		ATRArgDef *argDef = &trans->pArgs[i];
		
		if ((argDef->eArgType == ATR_ARG_CONTAINER || argDef->eArgType == ATR_ARG_CONTAINER_EARRAY)
			&& strcmp(args[i+1], "NULL") != 0)
		{
			int j;
			char *parseString = args[i+1];
			char *containedQuery = parseString;
			const char *argContainerName = NULL;
			int argContainerType = 0;

			ATRContainerArgDef *pContainerArgDef = FindContainerArgDefByIndex(trans, i);


			AutoTransContainer *newArg = calloc(sizeof(AutoTransContainer),1);


			assert(pContainerArgDef);

			newArg->iTransArgNum = i;
			newArg->bIsArray = (argDef->eArgType == ATR_ARG_CONTAINER_EARRAY);

						
			containedQuery = strchr(containedQuery,'[');

			if (!containedQuery)
			{
				free(newArg);
				DestroyAutoTransArgs(outArgs);
				PERFINFO_AUTO_STOP();
				return 0;				
			}

			*(containedQuery++) = '\0';
			newArg->containerType = NameToGlobalType(parseString);

			if (argContainerName = ParserGetTableName(argDef->pParseTable))
			{
				argContainerType = NameToGlobalType(argContainerName);
				while (GlobalTypeParent(argContainerType))
				{
					argContainerType = GlobalTypeParent(argContainerType);
				}
			}

			parseString = containedQuery;
			containedQuery = strchr(containedQuery,']');

			if (!containedQuery || !newArg->containerType || 
				(newArg->containerType != argContainerType && GlobalTypeParent(newArg->containerType) != argContainerType))
			{
				// Make sure it's of a compatible container type as the one specified in the args
				free(newArg);
				DestroyAutoTransArgs(outArgs);
				PERFINFO_AUTO_STOP();
				return 0;				
			}

			*(containedQuery++) = '\0';

			if (newArg->bIsArray)
			{
				ea32PushUIntsFromString(&newArg->pContainerIDs, parseString);
				
				for (j=0; j < ea32Size(&newArg->pContainerIDs); j++)
				{
					assertmsgf(newArg->pContainerIDs[j] != 0, "Invalid containerID 0 found while parsing autotransaction %s", 
						args[0]);
				}
			}
			else
			{
				newArg->containerID = atoi(parseString);
				assertmsgf(newArg->containerID != 0, "Invalid containerID 0 found while parsing autotransaction %s", 
					args[0]);

			}


			for (j=0; j < eaSize(&pContainerArgDef->ppLocks); j++)
			{
				ATRFixedUpLock *pLock = pContainerArgDef->ppLocks[j];

				if (strcmp(pLock->pDerefString, ".*") == 0)
				{
					newArg->bLockAllFields = true;
					break;
				}

				eaPush(&newArg->lockedFields,NULL);
				eaiPush(&newArg->lockTypes, pLock->eLockType);
				if (!FixAutoPath(&newArg->lockedFields[eaSize(&newArg->lockedFields)-1],pLock->pDerefString,argDef->pParseTable))
				{
					// Make sure it's of a compatible container type as the one specified in the args
					free(newArg);
					DestroyAutoTransArgs(outArgs);
					PERFINFO_AUTO_STOP();

					//making this a devassert because any failure here means the auto_trans system is broken somehow, or at least
					//the current transaction is broken and will never succeed
					devassertmsgf(0, "AUTO_TRANSACTION failure... unrecognized argument \"%s\" for argument of type %s during auto transaction \"%s\"",
						pLock->pDerefString, ParserGetTableName(argDef->pParseTable), trans->pFuncName);

					return 0;
				}
			}

			if (!newArg->bLockAllFields)
			{
				for (j=0; j < eaSize(&pContainerArgDef->ppEarrayUses); j++)
				{
					ATRFixedUpEarrayUse *pEarrayUse = pContainerArgDef->ppEarrayUses[j];
					const char *pIntConvertedToEnum;

					eaPush(&newArg->lockedFields,NULL);
					eaiPush(&newArg->lockTypes, pEarrayUse->eLockType);
					if (!FixAutoPath(&newArg->lockedFields[eaSize(&newArg->lockedFields)-1],pEarrayUse->pContainerDerefString,argDef->pParseTable))
					{
						// Make sure it's of a compatible container type as the one specified in the args
						free(newArg);
						DestroyAutoTransArgs(outArgs);
						PERFINFO_AUTO_STOP();
						return 0;
					}

					switch (pEarrayUse->eIndexType)
					{
					xcase ATR_INDEX_LITERAL_INT:
						if ((pIntConvertedToEnum = ConvertIntEarrayLockToEnum(pContainerArgDef, pEarrayUse, pEarrayUse->iVal)))
						{
							estrConcatf(&newArg->lockedFields[eaSize(&newArg->lockedFields)-1],"[\"%s\"]",pIntConvertedToEnum);
						}
						else
						{
							estrConcatf(&newArg->lockedFields[eaSize(&newArg->lockedFields)-1],"[%d]",pEarrayUse->iVal);
						}
					xcase ATR_INDEX_LITERAL_STRING:
						estrConcatf(&newArg->lockedFields[eaSize(&newArg->lockedFields)-1],"[\"%s\"]",pEarrayUse->pSVal);
					xcase ATR_INDEX_SIMPLE_ARG:
						{
							char *pStr = args[pEarrayUse->iVal + 1];

							if ((pIntConvertedToEnum = ConvertStringEarrayLockToEnum(pContainerArgDef, pEarrayUse, pStr)))
							{
								estrConcatf(&newArg->lockedFields[eaSize(&newArg->lockedFields)-1],"[\"%s\"]",pIntConvertedToEnum);
							}
							else
							{
								//string args have already been quoted by the tokenizing or something (somewhere earlier in the process)
								estrConcatf(&newArg->lockedFields[eaSize(&newArg->lockedFields)-1],"[%s]",args[pEarrayUse->iVal + 1]);
							}
						}
				
					}
				}
			}

			// Remove duplicates and nested fields

			PERFINFO_AUTO_START("estrs", 1);
			if (newArg->bLockAllFields)
			{
				for (j = 0; j < eaSize(&newArg->lockedFields); j++)
				{
					estrDestroy(&newArg->lockedFields[j]);
				}
				eaClear(&newArg->lockedFields);				
			}
			else
			{			
				for (j = 0; j < eaSize(&newArg->lockedFields); j++)
				{
					int k;
					for (k = j + 1; k < eaSize(&newArg->lockedFields); k++)
					{
						int jLength = estrLength(&newArg->lockedFields[j]);
						int kLength = estrLength(&newArg->lockedFields[k]);


						if (jLength < kLength)
						{
							if (strnicmp(newArg->lockedFields[j],newArg->lockedFields[k],
								jLength) == 0
								&& (!isalnumorunderscore(newArg->lockedFields[k][jLength]) || !isalnumorunderscore(newArg->lockedFields[k][jLength-1]))
								&& newArg->lockTypes[j] == newArg->lockTypes[k])
							{
								estrDestroy(&newArg->lockedFields[k]);
								eaRemove(&newArg->lockedFields,k);
								eaiRemove(&newArg->lockTypes,k);
								k--;
							}
						}
						else
						{
							if (strnicmp(newArg->lockedFields[j],newArg->lockedFields[k],
								kLength) == 0
								&& (!isalnumorunderscore(newArg->lockedFields[j][kLength]) || !isalnumorunderscore(newArg->lockedFields[j][kLength-1]))
								&& newArg->lockTypes[j] == newArg->lockTypes[k])
							{
								estrDestroy(&newArg->lockedFields[j]);
								eaRemove(&newArg->lockedFields,j);
								eaiRemove(&newArg->lockTypes,j);
								j--;
								k = eaSize(&newArg->lockedFields);
							}
						}
					}
				}
			}
			PERFINFO_AUTO_STOP();

			eaPush(&outArgs,newArg);
		}		
	}
	
	PERFINFO_AUTO_STOP_START("2", 1);

	estrPrintf(&pRunCommandString, "runautotrans %s ",args[0]);

	iContainerArgNum = 0;

	//we've now decided that all of our input arguments are OK, so we start building the actual transaction
	for (i = 0; i < argCount; i++)
	{
		ATRArgDef *pArgDef = &trans->pArgs[i];
		
		if ((pArgDef->eArgType == ATR_ARG_CONTAINER || pArgDef->eArgType == ATR_ARG_CONTAINER_EARRAY)
			&& strcmp(args[i+1], "NULL") != 0)
		{
			AutoTransContainer *pCurContainerArg = outArgs[iContainerArgNum++];

			int iNumContainers = pCurContainerArg->bIsArray ? ea32Size(&pCurContainerArg->pContainerIDs) : 1;
			int iContainerNum;

			assert(pCurContainerArg->iTransArgNum == i);


			if (pCurContainerArg->bIsArray)
			{
				estrConcatf(&pRunCommandString, " TRVAR_ARRAY[");
			}

			for (iContainerNum = 0; iContainerNum < iNumContainers; iContainerNum++)
			{
				int j;
	
				char *pBaseTransDataEString;

				BaseTransaction *pBaseTrans = StructCreate(parse_BaseTransaction);

				estrStackCreate(&pBaseTransDataEString);


				pBaseTrans->recipient.containerType = pCurContainerArg->containerType;
				pBaseTrans->recipient.containerID = pCurContainerArg->bIsArray ? pCurContainerArg->pContainerIDs[iContainerNum] : pCurContainerArg->containerID;				

				if (pCurContainerArg->bIsArray)
				{
					if (pCurContainerArg->bLockAllFields)
					{
						estrPrintf(&pBaseTransDataEString, "AutoTransAcquireContainer %s_%d %s ", pArgDef->pArgName, iContainerNum, serverTypeToRunOn);
					}
					else
					{					
						estrPrintf(&pBaseTransDataEString, "AutoTransAcquireFields %s_%d %s ", pArgDef->pArgName, iContainerNum, serverTypeToRunOn);
					}
					estrConcatf(&pRunCommandVarString, "%s_%d " , pArgDef->pArgName, iContainerNum);
					estrConcatf(&pRunCommandString, "%s%s_%d", iContainerNum == 0 ? "" : ",", pArgDef->pArgName, iContainerNum);
				}
				else
				{
					if (pCurContainerArg->bLockAllFields)
					{
						estrPrintf(&pBaseTransDataEString, "AutoTransAcquireContainer %s %s ", pArgDef->pArgName, serverTypeToRunOn);
					}
					else
					{					
						estrPrintf(&pBaseTransDataEString, "AutoTransAcquireFields %s %s ", pArgDef->pArgName, serverTypeToRunOn);
					}
					estrConcatf(&pRunCommandVarString, "%s ", pArgDef->pArgName);
					estrConcatf(&pRunCommandString, "%s ", pArgDef->pArgName);
				}

				if (!pCurContainerArg->bLockAllFields)
				{	
					if (!eaSize(&pCurContainerArg->lockedFields))
					{
						estrConcatf(&pBaseTransDataEString, "\"\"");
					}
					else
					{
						for (j = 0; j < eaSize(&pCurContainerArg->lockedFields); j++)
						{
							estrConcatf(&pBaseTransDataEString,"%d %s\n",pCurContainerArg->lockTypes
								[j],pCurContainerArg->lockedFields[j]);
						}
					}
				}

				pBaseTrans->pData = strDupFromEString(&pBaseTransDataEString);
				eaPush(&pTransactionRequestToUse->ppBaseTransactions, pBaseTrans);
				estrDestroy(&pBaseTransDataEString);

			}

			if (pCurContainerArg->bIsArray)
			{
				estrConcatf(&pRunCommandString, "] ");
			}
		}
		else
		{
			estrConcatf(&pRunCommandString, "%s ", args[i+1]);
		}
	}

	PERFINFO_AUTO_STOP_START("3", 1);

	//now create the "master" base transaction
	pMasterBaseTrans = StructCreate(parse_BaseTransaction);


	pMasterBaseTrans->recipient.containerType = eServerTypeToRunOn;

	if (gbForceAutoTransactionsToRunLocally && eServerTypeToRunOn == GetAppGlobalType())
	{
		pMasterBaseTrans->recipient.containerID = gServerLibState.containerID;
	}
	else
	{
		pMasterBaseTrans->recipient.containerID = SPECIAL_CONTAINERID_FIND_BEST_FOR_TRANSACTION;
	}
	
	
	pMasterBaseTrans->pRequestedTransVariableNames = strDupFromEString(&pRunCommandVarString);
	pMasterBaseTrans->pData = strDupFromEString(&pRunCommandString);
	estrDestroy(&pRunCommandVarString);
	estrDestroy(&pRunCommandString);

	eaPush(&pTransactionRequestToUse->ppBaseTransactions, pMasterBaseTrans);
	for (i = 0; i < eaSize(&outArgs); i++)
	{
		AutoTransContainer *pCurContainerArg = outArgs[i];
		ATRArgDef *pArgDef = &trans->pArgs[pCurContainerArg->iTransArgNum];

		int iNumContainers = pCurContainerArg->bIsArray ? ea32Size(&pCurContainerArg->pContainerIDs) : 1;
		int iContainerNum;

		for (iContainerNum = 0; iContainerNum < iNumContainers; iContainerNum++)
		{
			BaseTransaction *pBaseTrans = StructCreate(parse_BaseTransaction);

			char *pBaseTransDataEString = NULL;
			char *pBaseTransDataReqTransVarNames = NULL;
			estrStackCreate(&pBaseTransDataEString);
			estrStackCreate(&pBaseTransDataReqTransVarNames);

			pBaseTrans->recipient.containerType = pCurContainerArg->containerType;
			pBaseTrans->recipient.containerID = pCurContainerArg->bIsArray ? pCurContainerArg->pContainerIDs[iContainerNum] : pCurContainerArg->containerID;			

			if (pCurContainerArg->bIsArray)
			{
				estrPrintf(&pBaseTransDataEString, "autotransupdate %s TRVAR_%s_%d", serverTypeToRunOn, pArgDef->pArgName, iContainerNum);
				estrPrintf(&pBaseTransDataReqTransVarNames, "%s_%d", pArgDef->pArgName, iContainerNum);
			}
			else
			{
				estrPrintf(&pBaseTransDataEString, "autotransupdate %s TRVAR_%s", serverTypeToRunOn, pArgDef->pArgName);
				estrPrintf(&pBaseTransDataReqTransVarNames, "%s ", pArgDef->pArgName);
			}

			pBaseTrans->pData = strDupFromEString(&pBaseTransDataEString);
			pBaseTrans->pRequestedTransVariableNames = strDupFromEString(&pBaseTransDataReqTransVarNames);

			estrDestroy(&pBaseTransDataEString);
			estrDestroy(&pBaseTransDataReqTransVarNames);

			eaPush(&pTransactionRequestToUse->ppBaseTransactions, pBaseTrans);
		}

	}

	GET_CPU_TICKS_64(timeEnd);

	if (gLogAllTransPerf)
	{	
		objLog(LOG_TRANSPERF, 0, 0, 0, NULL, NULL, NULL, "TransRequest", NULL, 
			"Name %s Ticks %"FORM_LL"d",
			trans->pFuncName, timeEnd - timeStart);
	}

	PERFINFO_AUTO_STOP();

	if (pTransactionRequest_In)
	{
		//if (returnStruct)
		//{
		//	RequestNewTransaction(objLocalManager(), pTransactionRequestToUse->ppBaseTransactions,
		//		TRANS_TYPE_SEQUENTIAL_ATOMIC,returnStruct);
		//}
		//else
		//{
		//	RequestNewTransaction(objLocalManager(), pTransactionRequestToUse->ppBaseTransactions,
		//		TRANS_TYPE_SEQUENTIAL_ATOMIC,NULL);
		//}
	}
	else
	{
		
		RequestNewTransaction(objLocalManager(), trans->pFuncName, pTransactionRequestToUse->ppBaseTransactions,
			TRANS_TYPE_SEQUENTIAL_ATOMIC,returnStruct, TRANSREQUESTFLAG_DO_LOCAL_STRUCT_FIXUP_FOR_AUTO_TRANS);



		StructDestroy(parse_TransactionRequest, pTransactionRequestToUse);
	}

	DestroyAutoTransArgs(outArgs);

	return 1;

}

void objRequestContainerMove(TransactionReturnVal *returnVal, GlobalType containerType, ContainerID containerID, GlobalType sourceType, ContainerID sourceID, GlobalType destType, ContainerID destID)
{
	TransactionRequest *request = objCreateTransactionRequest();

	assert(GlobalTypeSchemaType(containerType) == SCHEMATYPE_PERSISTED || GlobalTypeSchemaType(containerType) == SCHEMATYPE_TRANSACTED);

	if (destType == GLOBALTYPE_OBJECTDB)
	{
		objAddToTransactionRequestf(request, sourceType, sourceID, NULL, 
			"ReturnContainerTo %s %d %s %d",
			GlobalTypeToName(containerType),containerID,GlobalTypeToName(destType),destID);

		objAddToTransactionRequestf(request, destType, destID, NULL, 
			"RecoverContainerFrom %s %d %s %d",
			GlobalTypeToName(containerType),containerID,GlobalTypeToName(sourceType),sourceID);
	}
	else if (sourceType == GLOBALTYPE_OBJECTDB)
	{
		objAddToTransactionRequestf(request, sourceType, sourceID, NULL, 
			"SendContainerTo containerVar %s %d %s %d",
			GlobalTypeToName(containerType),containerID,GlobalTypeToName(destType),destID);

		objAddToTransactionRequestf(request, destType, destID, "containerVar containerVarBinary", 
			"ReceiveContainerFrom %s %d %s %d TRVAR_containerVar",
			GlobalTypeToName(containerType),containerID,GlobalTypeToName(sourceType),sourceID);
	}
	else
	{	
		objAddToTransactionRequestf(request, sourceType, sourceID, NULL, 
			"MoveContainerTo containerVar %s %d %s %d",
			GlobalTypeToName(containerType),containerID,GlobalTypeToName(destType),destID);

		objAddToTransactionRequestf(request, destType, destID, "containerVar containerVarBinary", 
			"ReceiveContainerFrom %s %d %s %d TRVAR_containerVar",
			GlobalTypeToName(containerType),containerID,GlobalTypeToName(sourceType),sourceID);
	}

	objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC, 
		returnVal, "ContainerMove", request);

	objDestroyTransactionRequest(request);

}

void objRequestContainerCreate(TransactionReturnVal *returnVal, GlobalType containerType, void *pObject, GlobalType destType, ContainerID destID)
{
	ContainerSchema *pSchema = objFindContainerSchema(containerType);
	ParseTable *pTable = pSchema->classParse;
	char *diffString = NULL;
	TransactionRequest *request = objCreateTransactionRequest();

	assert(pTable);
	assert(GlobalTypeSchemaType(containerType) == SCHEMATYPE_PERSISTED);
	// TODO: do _TRANSACTED version of this

	estrStackCreate(&diffString);

	//legal to pass in a NULL object if you want to create an empty object
	if (pObject)
	{
		StructTextDiffWithNull_Verify(&diffString,pTable,pObject,NULL,0,TOK_PERSIST,0,0);
	}
	else
	{
		estrCopy2(&diffString, "");
	}

	objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, 0, NULL, 
		"CreateContainerUsingData containerIDVar %s \"%s\"",
		GlobalTypeToName(containerType),diffString);

	if (destType && destType != GLOBALTYPE_OBJECTDB){
		objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, 0, "containerIDVar", 
			"MoveContainerTo containerVar %s TRVAR_containerIDVar %s %d",
			GlobalTypeToName(containerType),GlobalTypeToName(destType),destID);

		objAddToTransactionRequestf(request, destType, destID, "containerVar containerIDVar ContainerVarBinary", 
			"ReceiveContainerFrom %s TRVAR_containerIDVar ObjectDB 0 TRVAR_containerVar",
			GlobalTypeToName(containerType));
	}
	
	objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC, 
		returnVal, "ContainerCreate", request);

	objDestroyTransactionRequest(request);
	estrDestroy(&diffString);

}

void objRequestContainerVerifyAndMove(TransactionReturnVal *returnVal, GlobalType containerType, ContainerID containerID, GlobalType destType, ContainerID destID)
{
	TransactionRequest *request = objCreateTransactionRequest();

	objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, 0, NULL, 
		"VerifyContainer containerIDVar %s %u",
		GlobalTypeToName(containerType), containerID);

	if (destType && destType != GLOBALTYPE_OBJECTDB){
		objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, 0, "containerIDVar", 
			"MoveContainerTo containerVar %s TRVAR_containerIDVar %s %d",
			GlobalTypeToName(containerType),GlobalTypeToName(destType),destID);

		objAddToTransactionRequestf(request, destType, destID, "containerVar containerIDVar ContainerVarBinary", 
			"ReceiveContainerFrom %s TRVAR_containerIDVar ObjectDB 0 TRVAR_containerVar",
			GlobalTypeToName(containerType));
	}
	
	objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC, 
		returnVal, "ContainerVerify", request);

	objDestroyTransactionRequest(request);
}

void objRequestContainerVerifyAndSet(TransactionReturnVal *returnVal, GlobalType containerType, ContainerID containerID, void *pObject, GlobalType destType, ContainerID destID)
{
	ContainerSchema *pSchema = objFindContainerSchema(containerType);
	ParseTable *pTable = pSchema->classParse;
	char *diffString = NULL;
	TransactionRequest *request = objCreateTransactionRequest();

	assert(pTable);
	assert(GlobalTypeSchemaType(containerType) == SCHEMATYPE_PERSISTED);
	// TODO: do _TRANSACTED version of this

	estrStackCreate(&diffString);

	StructTextDiffWithNull_Verify(&diffString,pTable,pObject,NULL,0,TOK_PERSIST,0,0);

	objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, 0, NULL, 
		"VerifyAndSetContainer containerIDVar %s %u \"%s\"",
		GlobalTypeToName(containerType), containerID, diffString);

	if (destType && destType != GLOBALTYPE_OBJECTDB){
		objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, 0, "containerIDVar", 
			"MoveContainerTo containerVar %s TRVAR_containerIDVar %s %d",
			GlobalTypeToName(containerType),GlobalTypeToName(destType),destID);

		objAddToTransactionRequestf(request, destType, destID, "containerVar containerIDVar ContainerVarBinary", 
			"ReceiveContainerFrom %s TRVAR_containerIDVar ObjectDB 0 TRVAR_containerVar",
			GlobalTypeToName(containerType));
	}
	
	objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC, 
		returnVal, "ContainerVerifyAndSet", request);

	objDestroyTransactionRequest(request);
	estrDestroy(&diffString);

}

void objRequestContainerVerifyOrCreateAndInit(TransactionReturnVal *returnVal, GlobalType containerType, ContainerID containerID, void *pObject, GlobalType destType, ContainerID destID)
{
	TransactionRequest *request = objCreateTransactionRequest();
	objAddToTransactionContainerVerifyOrCreateAndInit(request, containerType, containerID, pObject, destType, destID);
	objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC, 
		returnVal, "ContainerVerifyOrCreateAndInit", request);
	objDestroyTransactionRequest(request);

}

void objAddToTransactionContainerVerifyOrCreateAndInit(TransactionRequest *request, GlobalType containerType, ContainerID containerID, void *pObject, GlobalType destType, ContainerID destID)
{
	ContainerSchema *pSchema = objFindContainerSchema(containerType);
	ParseTable *pTable = pSchema->classParse;
	char *diffString = NULL;

	assert(pTable);
	assert(GlobalTypeSchemaType(containerType) == SCHEMATYPE_PERSISTED);
	// TODO: do _TRANSACTED version of this

	estrStackCreate(&diffString);

	StructTextDiffWithNull_Verify(&diffString,pTable,pObject,NULL,0,TOK_PERSIST,0,0);

	objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, 0, NULL, 
		"VerifyOrCreateAndInitContainer containerIDVar %s %u \"%s\"",
		GlobalTypeToName(containerType), containerID, diffString);

	if (destType && destType != GLOBALTYPE_OBJECTDB){
		objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, 0, "containerIDVar", 
			"MoveContainerTo containerVar %s TRVAR_containerIDVar %s %d",
			GlobalTypeToName(containerType),GlobalTypeToName(destType),destID);

		objAddToTransactionRequestf(request, destType, destID, "containerVar containerIDVar ContainerVarBinary", 
			"ReceiveContainerFrom %s TRVAR_containerIDVar ObjectDB 0 TRVAR_containerVar",
			GlobalTypeToName(containerType));
	}
	
	estrDestroy(&diffString);

}

void objRequestContainerCreateLocal(TransactionReturnVal *returnVal, GlobalType containerType, void *pObject)
{
	ContainerSchema *pSchema = objFindContainerSchema(containerType);
	ParseTable *pTable = pSchema->classParse;
	char *diffString = NULL;
	TransactionRequest *request = NULL;
	int objectID = 0;

	PERFINFO_AUTO_START_FUNC();
	request = objCreateTransactionRequest();

	assert(pTable);
	assert(GlobalTypeSchemaType(containerType) == SCHEMATYPE_PERSISTED);
	assert(IsLocalManagerFullyLocal(objLocalManager()));

	estrStackCreate(&diffString);

	StructTextDiffWithNull_Verify(&diffString,pTable,pObject,NULL,0,TOK_PERSIST,0,0);
	objGetKeyInt(pTable, pObject, &objectID);

	// If id is 0, use the auto selecting transaction
	if (objectID)
	{	
		objAddToTransactionRequestf(request, objServerType(), 0, NULL,
			"CreateSpecificContainerUsingData containerIDVar %s %d \"%s\"",
			GlobalTypeToName(containerType), objectID, diffString);
	}
	else
	{		
		objAddToTransactionRequestf(request, objServerType(), 0, NULL,
			"CreateContainerUsingData containerIDVar %s \"%s\"",
			GlobalTypeToName(containerType), diffString);
	}
	
	objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC, 
		returnVal, "ContainerCreateLocal", request);

	objDestroyTransactionRequest(request);
	estrDestroy(&diffString);
	PERFINFO_AUTO_STOP();
}

static void
AddDestroyContainerAndDependentsToRequest(TransactionRequest *request, GlobalType containerType, ContainerID containerID, GlobalType sourceType, ContainerID sourceID, bool cleanup)
{
	assert(GlobalTypeSchemaType(containerType) == SCHEMATYPE_PERSISTED);
	// TODO: do _TRANSACTED version of this

	if (sourceType != GLOBALTYPE_OBJECTDB)
	{
		// We need to move it to ObjectDB first
		objAddToTransactionRequestf(request, sourceType, sourceID, NULL, 
			"ReturnContainerTo %s %d %s %d",
			GlobalTypeToName(containerType),containerID,GlobalTypeToName(GLOBALTYPE_OBJECTDB), 0);
	}

	if(cleanup)
	{
		objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, 0, NULL, 
			"DestroyContainerAndDependents %s %d",
			GlobalTypeToName(containerType), containerID);
	}
	else
	{
		objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, 0, NULL, 
			"DestroyContainerAndDependentsWithoutCleanup %s %d",
			GlobalTypeToName(containerType), containerID);
	}
	return;
}

void objRequestDestroyContainerAndDependents(TransactionReturnVal *returnVal, GlobalType containerType, ContainerID containerID, GlobalType sourceType, ContainerID sourceID)
{
	TransactionRequest *request = objCreateTransactionRequest();

	AddDestroyContainerAndDependentsToRequest(request, containerType, containerID, sourceType, sourceID, true);

	objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC,  returnVal, "DestroyContainerAndDependents", request);

	objDestroyTransactionRequest(request);
}

static void
AddContainerDeleteToRequest(TransactionRequest *request, GlobalType containerType, ContainerID containerID, GlobalType sourceType, ContainerID sourceID)
{
	assert(GlobalTypeSchemaType(containerType) == SCHEMATYPE_PERSISTED);
	// TODO: do _TRANSACTED version of this

	if (sourceType != GLOBALTYPE_OBJECTDB)
	{
		// We need to move it to ObjectDB first
		objAddToTransactionRequestf(request, sourceType, sourceID, NULL, 
			"ReturnContainerTo %s %d %s %d",
			GlobalTypeToName(containerType),containerID,GlobalTypeToName(GLOBALTYPE_OBJECTDB), 0);
	}

	objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, 0, NULL, 
		"DeleteContainer %s %d",
		GlobalTypeToName(containerType), containerID);

	return;
}

void objRequestContainerDelete(TransactionReturnVal *returnVal, GlobalType containerType, ContainerID containerID, GlobalType sourceType, ContainerID sourceID)
{
	TransactionRequest *request = objCreateTransactionRequest();

	AddContainerDeleteToRequest(request, containerType, containerID, sourceType, sourceID);

	objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC,  returnVal, "DeleteContainer", request);

	objDestroyTransactionRequest(request);
}

void
AddContainerUndeleteToRequest(TransactionRequest *request, GlobalType containerType, ContainerID containerID, GlobalType sourceType, ContainerID sourceID, char *namestr)
{
	assert(GlobalTypeSchemaType(containerType) == SCHEMATYPE_PERSISTED);
	// TODO: do _TRANSACTED version of this

	if (sourceType != GLOBALTYPE_OBJECTDB)
	{
		objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, 0, NULL, 
			"RecoverContainerFromWithBackup %s %d %s %d",
			GlobalTypeToName(containerType),containerID,GlobalTypeToName(sourceType),sourceID);
	}

	if(namestr && *namestr)
	{
		objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, 0, NULL, 
			"UndeleteContainerWithRename %s %d \"%s\"",
			GlobalTypeToName(containerType), containerID, namestr);
	}
	else
	{
		objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, 0, NULL, 
			"UndeleteContainer %s %d",
			GlobalTypeToName(containerType), containerID);
	}

	return;
}

void objRequestContainerUndelete(TransactionReturnVal *returnVal, GlobalType containerType, ContainerID containerID, GlobalType sourceType, ContainerID sourceID, char *namestr)
{
	TransactionRequest *request = objCreateTransactionRequest();

	AddContainerUndeleteToRequest(request, containerType, containerID, sourceType, sourceID, namestr);

	objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC,  returnVal, "UndeleteContainer", request);

	objDestroyTransactionRequest(request);
}

static void
AddContainerDestroyToRequest(TransactionRequest *request, GlobalType containerType, ContainerID containerID, GlobalType sourceType, ContainerID sourceID)
{
	assert(GlobalTypeSchemaType(containerType) == SCHEMATYPE_PERSISTED);
	// TODO: do _TRANSACTED version of this

	if (sourceType != GLOBALTYPE_OBJECTDB)
	{
		// We need to move it to ObjectDB first
		objAddToTransactionRequestf(request, sourceType, sourceID, NULL, 
			"ReturnContainerTo %s %d %s %d",
			GlobalTypeToName(containerType),containerID,GlobalTypeToName(GLOBALTYPE_OBJECTDB), 0);

		objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, 0, NULL, 
			"RecoverContainerFromWithBackup %s %d %s %d",
			GlobalTypeToName(containerType),containerID,GlobalTypeToName(sourceType),sourceID);
	}

	objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, 0, NULL, 
		"DestroyContainer %s %d",
		GlobalTypeToName(containerType), containerID);

	return;
}

static void
AddDeletedContainerDestroyToRequest(TransactionRequest *request, GlobalType containerType, ContainerID containerID, GlobalType sourceType, ContainerID sourceID)
{
	assert(GlobalTypeSchemaType(containerType) == SCHEMATYPE_PERSISTED);
	// TODO: do _TRANSACTED version of this

	if (sourceType != GLOBALTYPE_OBJECTDB)
	{
		// We need to move it to ObjectDB first
		objAddToTransactionRequestf(request, sourceType, sourceID, NULL, 
			"ReturnContainerTo %s %d %s %d",
			GlobalTypeToName(containerType),containerID,GlobalTypeToName(GLOBALTYPE_OBJECTDB), 0);

		objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, 0, NULL, 
			"RecoverContainerFromWithBackup %s %d %s %d",
			GlobalTypeToName(containerType),containerID,GlobalTypeToName(sourceType),sourceID);
	}

	objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, 0, NULL, 
		"DestroyDeletedContainer %s %d",
		GlobalTypeToName(containerType), containerID);

	return;
}

void objRequestContainerDestroy(TransactionReturnVal *returnVal, GlobalType containerType, ContainerID containerID, GlobalType sourceType, ContainerID sourceID)
{
	TransactionRequest *request = objCreateTransactionRequest();

	AddContainerDestroyToRequest(request, containerType, containerID, sourceType, sourceID);

	objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC,  returnVal, "ContainerDestroy", request);

	objDestroyTransactionRequest(request);
}

void objRequestDeletedContainerDestroy(TransactionReturnVal *returnVal, GlobalType containerType, ContainerID containerID, GlobalType sourceType, ContainerID sourceID)
{
	TransactionRequest *request = objCreateTransactionRequest();

	AddDeletedContainerDestroyToRequest(request, containerType, containerID, sourceType, sourceID);

	objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC,  returnVal, "DeletedContainerDestroy", request);

	objDestroyTransactionRequest(request);
}

static void
AddWriteContainerToOfflineHoggToRequest(TransactionRequest *request, GlobalType containerType, ContainerID containerID, GlobalType sourceType, ContainerID sourceID)
{
	assert(GlobalTypeSchemaType(containerType) == SCHEMATYPE_PERSISTED);
	// TODO: do _TRANSACTED version of this

	if (sourceType != GLOBALTYPE_OBJECTDB)
	{
		// We need to move it to ObjectDB first
		objAddToTransactionRequestf(request, sourceType, sourceID, NULL, 
			"ReturnContainerTo %s %d %s %d",
			GlobalTypeToName(containerType),containerID,GlobalTypeToName(GLOBALTYPE_OBJECTDB), 0);
	}

	objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, 0, NULL, 
		"WriteContainerToOfflineHogg %s %d",
		GlobalTypeToName(containerType), containerID);

	return;
}

static void
AddRemoveContainerFromOfflineHoggToRequest(TransactionRequest *request, GlobalType containerType, ContainerID containerID, GlobalType sourceType, ContainerID sourceID)
{
	assert(GlobalTypeSchemaType(containerType) == SCHEMATYPE_PERSISTED);
	// TODO: do _TRANSACTED version of this

	if (sourceType != GLOBALTYPE_OBJECTDB)
	{
		// We need to move it to ObjectDB first
		objAddToTransactionRequestf(request, sourceType, sourceID, NULL, 
			"ReturnContainerTo %s %d %s %d",
			GlobalTypeToName(containerType),containerID,GlobalTypeToName(GLOBALTYPE_OBJECTDB), 0);
	}

	objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, 0, NULL, 
		"RemoveContainerFromOfflineHogg %s %d",
		GlobalTypeToName(containerType), containerID);

	return;
}

void objFillRequestToOfflineEntityPlayerAndDependents(TransactionRequest *request, ContainerID accountID, GlobalType containerType, ContainerID containerID, const ObjectIndexHeader *header, ContainerRef **ppRefs, GlobalType sourceType, ContainerID sourceID)
{
	EARRAY_FOREACH_BEGIN(ppRefs, i);
	{
		AddWriteContainerToOfflineHoggToRequest(request, ppRefs[i]->containerType, ppRefs[i]->containerID, sourceType, sourceID);
	}
	EARRAY_FOREACH_END;

	AddEnsureAccountStubExistsToRequest(request, accountID);

	AddOfflineEntityPlayerToAccountStubRequest(request, accountID, header);

	AddDestroyContainerAndDependentsToRequest(request, containerType, containerID, sourceType, sourceID, false);
}

void objSendRequestOfOfflineEntityPlayerAndDependents(TransactionReturnVal *returnVal, TransactionRequest *request)
{
	objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC,  returnVal, "RequestOfflineEntityPlayerAndDependents", request);
}

void objRequestOfflineEntityPlayerAndDependents(TransactionReturnVal *returnVal, ContainerID accountID, GlobalType containerType, ContainerID containerID, const ObjectIndexHeader *header, ContainerRef **ppRefs, GlobalType sourceType, ContainerID sourceID)
{
	TransactionRequest *request = objCreateTransactionRequest();

	objFillRequestToOfflineEntityPlayerAndDependents(request, accountID, containerType, containerID, header, ppRefs, sourceType, sourceID);

	objSendRequestOfOfflineEntityPlayerAndDependents(returnVal, request);

	objDestroyTransactionRequest(request);
}

void objRequestOfflineAccountWideContainer(TransactionReturnVal *returnVal, ContainerID accountID, GlobalType containerType, GlobalType sourceType, ContainerID sourceID)
{
	TransactionRequest *request = objCreateTransactionRequest();
	AddWriteContainerToOfflineHoggToRequest(request, containerType, accountID, sourceType, sourceID);
	AddContainerDestroyToRequest(request, containerType, accountID, sourceType, sourceID);
	AddEnsureAccountStubExistsToRequest(request, accountID);
	AddOfflineAccountWideContainerToAccountStubRequest(request, accountID, containerType);
	objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC,  returnVal, "RequestOfflineAccountWideContainer", request);
	objDestroyTransactionRequest(request);
}

void objRequestRemoveOfflineEntityPlayerAndDependents(TransactionReturnVal *returnVal, ContainerID accountID, GlobalType containerType, ContainerID containerID, ContainerRef **ppRefs, GlobalType sourceType, ContainerID sourceID)
{
	TransactionRequest *request = objCreateTransactionRequest();

	EARRAY_FOREACH_BEGIN(ppRefs, i);
	{
		AddRemoveContainerFromOfflineHoggToRequest(request, ppRefs[i]->containerType, ppRefs[i]->containerID, sourceType, sourceID);
	}
	EARRAY_FOREACH_END;

	AddEnsureAccountStubExistsToRequest(request, accountID);
	AddRemoveOfflineEntityPlayerFromAccountStubToRequest(request, accountID, containerID);

	objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC,  returnVal, "RequestRemoveOfflineEntityPlayerAndDependents", request);

	objDestroyTransactionRequest(request);
}

void objRequestRemoveAccountWideContainerFromOffline(TransactionReturnVal *returnVal, ContainerID accountID, GlobalType containerType, GlobalType sourceType, ContainerID sourceID)
{
	TransactionRequest *request = objCreateTransactionRequest();

	AddRemoveContainerFromOfflineHoggToRequest(request, containerType, accountID, sourceType, sourceID);
	AddEnsureAccountStubExistsToRequest(request, accountID);
	AddRemoveOfflineAccountWideContainerFromAccountStubToRequest(request, accountID, containerType);

	objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC,  returnVal, "RequestRemoveAccountWideContainerFromOffline", request);

	objDestroyTransactionRequest(request);
}

void objRequestContainersDestroy(TransactionReturnVal *returnVal, ContainerRef **containerRefs, GlobalType sourceType, ContainerID sourceID)
{
	TransactionRequest *request = objCreateTransactionRequest();
	int i;
	int n;

	n = eaSize(&containerRefs);

	for ( i = 0; i < n; i++ )
	{
		AddContainerDestroyToRequest(request, containerRefs[i]->containerType, containerRefs[i]->containerID, sourceType, sourceID);
	}

	objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC,  returnVal, "ContainersDestroy", request);

	objDestroyTransactionRequest(request);
}

void objRequestDependentContainersDestroy(TransactionReturnVal *returnVal, ContainerRef *ref, ContainerRef **containerRefs, GlobalType sourceType, ContainerID sourceID)
{
	TransactionRequest *request = objCreateTransactionRequest();
	int i;
	int n;

	n = eaSize(&containerRefs);

	AddDeletedContainerDestroyToRequest(request, ref->containerType, ref->containerID, sourceType, sourceID);

	for ( i = 0; i < n; i++ )
	{
		AddContainerDestroyToRequest(request, containerRefs[i]->containerType, containerRefs[i]->containerID, sourceType, sourceID);
	}

	objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC,  returnVal, "ContainersDestroy", request);

	objDestroyTransactionRequest(request);
}

void objRequestContainerDestroyLocal(TransactionReturnVal *returnVal, GlobalType containerType, ContainerID containerID)
{
	TransactionRequest *request = objCreateTransactionRequest();

	PERFINFO_AUTO_START_FUNC();
	assert(GlobalTypeSchemaType(containerType) == SCHEMATYPE_PERSISTED);
	assert(IsLocalManagerFullyLocal(objLocalManager()));

	objAddToTransactionRequestf(request, objServerType(), objServerID(), NULL, 
		"DestroyContainer %s %d", GlobalTypeToName(containerType), containerID);

	objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC, 
		returnVal, "ContainerDestroyLocal", request);

	objDestroyTransactionRequest(request);
	PERFINFO_AUTO_STOP();
}

void LocalTransactionsTakeOwnership(void)
{
	int i;
	for (i = 0; i < GLOBALTYPE_MAXTYPES; i++)
	{
		char* classname = 0;
		ContainerStore *store = &gContainerRepository.containerStores[i];
		GlobalType type;
		int j,size;
		if (!store->containerSchema)
		{
			continue;
		}
		type = store->containerSchema->containerType;

		objLockContainerStore_ReadOnly(store);
		size = eaSize(&store->containers);
		for (j = 0; j < size; j++)
		{
			Container *object = store->containers[j];
			objChangeContainerState(object, CONTAINERSTATE_OWNED, GetAppGlobalType(), gServerLibState.containerID);
		}
		objUnlockContainerStore_ReadOnly(store);
	}
}



//special debugging code for logging what goes on with managed return vals. Saves the most recent 40 messages for each return val node
bool gLogManagedReturnVals = false;
AUTO_CMD_INT(gLogManagedReturnVals, LogManagedReturnVals) ACMD_COMMANDLINE;

typedef struct ManagedReturnValLog
{
	ActiveTransaction *pActiveTrans;
	char **ppLogLines; //newest message comes last
} ManagedReturnValLog;

static StashTable sLogsByPointer = NULL;
ManagedReturnValLog **gppManagedReturnValLogs = NULL;

ManagedReturnValLog *FindManagedReturnValLog(ActiveTransaction *pTrans)
{
	ManagedReturnValLog *pRetVal;
	if (!sLogsByPointer)
	{
		sLogsByPointer = stashTableCreateAddress(32);
	}

	if (stashFindPointer(sLogsByPointer, pTrans, &pRetVal))
	{
		return pRetVal;
	}

	pRetVal = calloc(sizeof(ManagedReturnValLog), 1);
	pRetVal->pActiveTrans = pTrans;
	eaPush(&gppManagedReturnValLogs, pRetVal);

	stashAddPointer(sLogsByPointer, pTrans, pRetVal, false);

	return pRetVal;
}

void ManagedReturnValLog_Internal(ActiveTransaction *pTrans, FORMAT_STR const char *pFmt, ...)
{
	char *pFullString = NULL;
	ManagedReturnValLog *pLog = FindManagedReturnValLog(pTrans);


	va_list ap;

	va_start(ap, pFmt);
	if (pFmt)
	{
		estrConcatfv(&pFullString, pFmt, ap);
	}
	va_end(ap);

	eaPush(&pLog->ppLogLines, pFullString);

	if (eaSize(&pLog->ppLogLines) == 40)
	{
		estrDestroy(&pLog->ppLogLines[0]);
		eaRemove(&pLog->ppLogLines, 0);
	}
}

static int siNumTransToAlert_ToUse = 0;

void NumTransToAlertCB(CMDARGS)
{
	siNumTransToAlert_ToUse = 0;
}

//If there are more than this managed transactions at once, something is probably wrong, generate
//an alert (GS)
static int siNumTransToAlert_GS = 2000;
AUTO_CMD_INT(siNumTransToAlert_GS, NumTransToAlert_GS) ACMD_AUTO_SETTING(Misc, GAMESERVER) ACMD_CALLBACK(NumTransToAlertCB);

//If there are more than this managed transactions at once, something is probably wrong, generate
//an alert (Object DB)
static int siNumTransToAlert_ObjectDB = 2000;
AUTO_CMD_INT(siNumTransToAlert_ObjectDB, NumTransToAlert_ObjectDB) ACMD_AUTO_SETTING(Misc, OBJECTDB) ACMD_CALLBACK(NumTransToAlertCB);

//If there are more than this managed transactions at once, something is probably wrong, generate
//an alert (Other servers)
static int siNumTransToAlert_Other = 2000;
AUTO_CMD_INT(siNumTransToAlert_Other, NumTransToAlert_Other) ACMD_AUTO_SETTING(Misc, ALL) ACMD_CALLBACK(NumTransToAlertCB);

//When a server starts up, wait this many seconds before generating Too_Many_Trans alerts (lots of ContainerMoves usually
//happen at startup, for instance)
static U32 siSecondsBeforeNumTransAlerts = 120;
AUTO_CMD_INT(siSecondsBeforeNumTransAlerts, SecondsBeforeNumTransAlerts) ACMD_AUTO_SETTING(Misc, ALL);


void CheckForTooManyActiveTransactions(void)
{
	static int siFirstTime = 0;
	if (!siFirstTime)
	{
		siFirstTime = timeSecondsSince2000();
		return;
	}

	if (timeSecondsSince2000() < siFirstTime + siSecondsBeforeNumTransAlerts)
	{
		return;
	}

	if (!siNumTransToAlert_ToUse)
	{
		if (GetAppGlobalType() == GLOBALTYPE_GAMESERVER)
		{
			siNumTransToAlert_ToUse = siNumTransToAlert_GS;
		}
		else if (GetAppGlobalType() == GLOBALTYPE_OBJECTDB)
		{
			siNumTransToAlert_ToUse = siNumTransToAlert_ObjectDB;
		}
		else 
		{
			siNumTransToAlert_ToUse = siNumTransToAlert_Other;
		}
	}

	if (eaSize(&gActiveTransactions) > siNumTransToAlert_ToUse)
	{
		PointerCounter *pCounter = PointerCounter_Create();
		char *pAlertString = NULL;
		PointerCounterResult **ppResults = NULL;

		FOR_EACH_IN_EARRAY(gActiveTransactions, ActiveTransaction, pTrans)
		{
			PointerCounter_AddSome(pCounter, pTrans->builtInReturnVal.pTransactionName, 1);
		}
		FOR_EACH_END;

		PointerCounter_GetMostCommon(pCounter, 5, &ppResults);

		estrPrintf(&pAlertString, "More than %d simultaneous active transactions, something is likely wrong. Top offenders:\n",
			siNumTransToAlert_ToUse);

		FOR_EACH_IN_EARRAY(ppResults, PointerCounterResult, pResult)
		{
			estrConcatf(&pAlertString, "%d: %s\n", pResult->iCount, (char*)(pResult->pPtr));
		}
		FOR_EACH_END;

		WARNING_NETOPS_ALERT("TOO_MANY_TRANS_ON_SERVER", "%s", pAlertString);

		eaDestroyEx(&ppResults, NULL);
		PointerCounter_Destroy(&pCounter);

		siNumTransToAlert_ToUse *= 2;
	}
}

LocalTransactionManager *objLocalManager(void)
{
	if(!gObjectTransactionManager.localManager)
		return NULL;
	if(OnLocalTransactionManagerThread(gObjectTransactionManager.localManager))
		return gObjectTransactionManager.localManager;

	return *objGetThreadLocalTransactionManager();
}



#include "AutoGen/objTransactions_h_ast.c"
