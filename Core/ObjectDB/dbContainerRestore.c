/***************************************************************************
*     Copyright (c) 2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "dbContainerRestore.h"
#include "WorkerThread.h"
#include "XboxThreads.h"
#include "structInternals.h"
#include "objTransactions.h"
#include "logging.h"
#include "TimedCallback.h"
#include "objContainerIO.h"
#include "TransactionSystem.h"
#include "GlobalTypes.h"
#include "cmdparse.h"
#include "AccountStub.h"
#include "dbOfflining.h"
#include "GlobalTypes_h_ast.h"

#include "AutoGen/objContainerIO_h_ast.h"
#include "AutoGen/ObjectDB_autogen_RemoteFuncs.h"
#include "autogen/ObjectDB_autogen_SlowFuncs.h"

#define CONTAINER_RESTORE_RETRY_TIMEOUT 60*60*10 // ten minutes

typedef struct CheckRestoreBaton
{
	WorkerThread *wt;
	volatile bool doReturn;
	CmdSlowReturnForServerMonitorInfo sri;

	//parameters
	char *filename;
	U32 type;
	U32 id;

	//return value
	ContainerRestoreState *restorestate;
} CheckRestoreBaton;

enum
{
	ACMDTHREAD_CMDDELAYTEST = WT_CMD_USER_START,
	ACMDTHREAD_MSGDELAYTEST
};

typedef struct ThreadDelay
{
	WorkerThread *wt;
	bool doReturn;
	CmdSlowReturnForServerMonitorInfo sri;

	//parameters
	U32 delay;
} ThreadDelay;


static void wt_ThreadDelayTestDispatch(void *user_data, ThreadDelay **delay, WTCmdPacket *packet)
{
	F32 starttime = timerGetSecondsAsFloat();
	printf("Starting Thread Delay Test on Thread.\n");
	Sleep(delay[0]->delay); //1.5s
	wtQueueMsg(delay[0]->wt, ACMDTHREAD_MSGDELAYTEST, delay, sizeof(ThreadDelay));
	printf("Finished Thread Delay Test Delay. %4.4fs\n", (timerGetSecondsAsFloat() - starttime));
	
}

static void mt_ThreadDelayTestProcess(void *user_data, ThreadDelay **delay, WTCmdPacket *packet)
{
	delay[0]->doReturn = true;
}

static void ThreadDelayTest_Check(TimedCallback *callback, F32 timeSinceLastCallback, ThreadDelay *delay)
{
	wtMonitor(delay->wt);
	if (delay->doReturn)
	{
		DoSlowCmdReturn(TRANSACTION_OUTCOME_SUCCESS, "", &delay->sri);
		wtDestroy(delay->wt);
		free(delay);
	}
	else
	{
		TimedCallback_Run(ThreadDelayTest_Check, delay, 0.005f); //5ms
	}
}

//This is an example of how to do an autocommand on a secondary thread
AUTO_COMMAND ACMD_CATEGORY(DEBUG) ACMD_NAME(ThreadTest) ACMD_ACCESSLEVEL(9);
void ThreadDelayTest(CmdContext *context, U32 msecdelay)
{
	ThreadDelay *delay = (ThreadDelay*)calloc(1, sizeof(ThreadDelay));
	F32 starttime = timerGetSecondsAsFloat();
	printf("Starting Thread Delay Test call.\n");

	delay->doReturn = false;
	delay->delay = msecdelay;
	context->slowReturnInfo.bDoingSlowReturn = true;
	memcpy(&delay->sri, &context->slowReturnInfo, sizeof(CmdSlowReturnForServerMonitorInfo));

	delay->wt = wtCreate(2, 2, NULL, "ThreadDelayTest");
	wtRegisterCmdDispatch(delay->wt, ACMDTHREAD_CMDDELAYTEST, wt_ThreadDelayTestDispatch);
	wtRegisterMsgDispatch(delay->wt, ACMDTHREAD_MSGDELAYTEST, mt_ThreadDelayTestProcess);

	wtSetProcessor(delay->wt, THREADINDEX_MISC);
	wtSetThreaded(delay->wt, true, 0, false);
	wtStart(delay->wt);

	wtQueueCmd(delay->wt, ACMDTHREAD_CMDDELAYTEST, &delay, sizeof(ThreadDelay));

	TimedCallback_Run(ThreadDelayTest_Check, delay, 0.005f); //5ms

	printf("Finished Thread Delay Test call. %4.4fs\n", (timerGetSecondsAsFloat() - starttime));
}

static void wt_CheckRestoreDispatch(void *user_data, CheckRestoreBaton **rsb, WTCmdPacket *packet)
{
	PERFINFO_AUTO_START("objCheckRestoreState", 1);
	rsb[0]->restorestate = objCheckRestoreState(rsb[0]->filename, rsb[0]->type, rsb[0]->id);
	wtQueueMsg(rsb[0]->wt, ACMDTHREAD_MSGDELAYTEST, rsb, sizeof(CheckRestoreBaton*));
	PERFINFO_AUTO_STOP();
}

static void mt_CheckRestoreProcess(void *user_data, CheckRestoreBaton **rsb, WTCmdPacket *packet)
{ rsb[0]->doReturn = true; }

static void CheckRestore_Check(TimedCallback *callback, F32 timeSinceLastCallback, CheckRestoreBaton *rsb)
{
	wtMonitor(rsb->wt);
	if (rsb->doReturn)
	{
		char *result = NULL;
		PERFINFO_AUTO_START("SerializeRestoreState",1);
		estrStackCreate(&result);
		if (rsb->restorestate)
		{
			estrPrintf(&result, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<methodResponse><params><param><value>");

			if (rsb->sri.eHowCalled == CMD_CONTEXT_HOWCALLED_XMLRPC)
			{
				ParserWriteXMLEx(&result, parse_ContainerRestoreState, rsb->restorestate, TPXML_FORMAT_XMLRPC|TPXML_NO_PRETTY);
			}
			else
			{
				ParserWriteTextEscaped(&result, parse_ContainerRestoreState, rsb->restorestate, 0, 0, 0);
			}
			StructDestroy(parse_ContainerRestoreState, rsb->restorestate);

			estrConcatf(&result, "</value></param></params></methodResponse>");
		}
		PERFINFO_AUTO_STOP();
		DoSlowCmdReturn(TRANSACTION_OUTCOME_SUCCESS, result, &rsb->sri);
		wtDestroy(rsb->wt);
		estrDestroy(&result);
		free(rsb->filename);
		free(rsb);
	}
	else
	{
		TimedCallback_Run(CheckRestore_Check, rsb, 0.005f); //5ms
	}
}				
				
//Run the slow objCheckRestoreState function on a secondary thread and block the client.
AUTO_COMMAND ACMD_CATEGORY(ObjectDB) ACMD_ACCESSLEVEL(9);
void CheckContainerRestoreState(CmdContext *context, const char *filename, U32 type, U32 id)
{
	CheckRestoreBaton *rsb = (CheckRestoreBaton*)calloc(1, sizeof(CheckRestoreBaton));

	rsb->doReturn = false;
	rsb->filename = strdup(filename);
	rsb->type = type;
	rsb->id = id;
	context->slowReturnInfo.bDoingSlowReturn = true;
	memcpy(&rsb->sri, &context->slowReturnInfo, sizeof(CmdSlowReturnForServerMonitorInfo));

	rsb->wt = wtCreate(2, 2, NULL, "CheckRestoreThread");
	wtRegisterCmdDispatch(rsb->wt, ACMDTHREAD_CMDDELAYTEST, wt_CheckRestoreDispatch);
	wtRegisterMsgDispatch(rsb->wt, ACMDTHREAD_MSGDELAYTEST, mt_CheckRestoreProcess);

	wtSetProcessor(rsb->wt, THREADINDEX_MISC);
	wtSetThreaded(rsb->wt, true, 0, false);
	wtStart(rsb->wt);

	wtQueueCmd(rsb->wt, ACMDTHREAD_CMDDELAYTEST, &rsb, sizeof(CheckRestoreBaton*));

	TimedCallback_Run(CheckRestore_Check, rsb, 0.005f); //5ms
}


AUTO_COMMAND ACMD_CATEGORY(ObjectDB) ACMD_ACCESSLEVEL(9);
DatabaseFileList * GetListOfDatabaseHogs()
{
	return objFindAllDatabaseHogs();
}

bool buildRestoreTransaction(GlobalType type, ContainerID id, ContainerRestoreState *rs, TransactionRequest *request, bool recursive, bool topLevel, bool alertOnExistingContainer)
{
	ContainerRef **ppRefs = NULL;
	char *diffstr = NULL;
	bool ok = true;

	PERFINFO_AUTO_START_FUNC();

	estrCreate(&diffstr);
	eaCreate(&ppRefs);

	if (objContainerGetRestoreStringFromState(type, id, rs, &diffstr, &ppRefs, recursive, alertOnExistingContainer))
	{
		if (diffstr && diffstr[0])
		{
			objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, objServerID(), NULL, 
				"VerifyAndSetContainer containerIDVar %s %u %s",
				GlobalTypeToName(type), id, diffstr);
		}

		//Iterate over dependent containers and restore them too.
		if (eaSize(&ppRefs) && (recursive || topLevel))
		{
			EARRAY_FOREACH_BEGIN(ppRefs, i);
			{
				ContainerRestoreState *subrs = objCheckRestoreState(rs->hog_file, ppRefs[i]->containerType, ppRefs[i]->containerID);
				if (subrs)
				{
					if (!buildRestoreTransaction(ppRefs[i]->containerType, ppRefs[i]->containerID, subrs, request, recursive, false, alertOnExistingContainer))
					{
						ErrorDeferredf("Failed building restore transaction for container: %s[%u]", 
							GlobalTypeToName(ppRefs[i]->containerType), ppRefs[i]->containerID);
						ok = false;
						break;
					}

					StructDestroy(parse_ContainerRestoreState, subrs);
				}
			}
			EARRAY_FOREACH_END;
		}
	}
	else
	{
		ok = false;
	}

	estrDestroy(&diffstr);
	eaDestroyStruct(&ppRefs, parse_ContainerRef);
	PERFINFO_AUTO_STOP();
	return ok;
}

static int sRestores = 0;

int GetTotalRestoredCharacters(void)
{
	return sRestores;
}

static U32 sLastRestoreTime = 0;

U32 GetLastRestoreTime(void)
{
	return sLastRestoreTime;
}

void SetLastRestoreTime(U32 restoreTime)
{
	sLastRestoreTime = restoreTime;
}

void RestoreContainer_CB(TransactionReturnVal *pReturn, ContainerRestoreRequest *request) 
{
	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		servLog(LOG_OBJTRANS, "ContainerRestoreSucceeded", "ContainerType \"%s\" ContainerID %u ReturnString \"%s\"", GlobalTypeToName(request->pConRef->containerType), request->pConRef->containerID, pReturn->pBaseReturnVals[0].returnString);
		request->eRestoreStatus = CR_SUCCESS;
		SetLastRestoreTime(timeSecondsSince2000());
		++sRestores;
	}
	else
	{
		servLog(LOG_OBJTRANS, "ContainerRestoreFailed", "ReturnString \"%s\"", pReturn->pBaseReturnVals[0].returnString);
		request->eRestoreStatus = CR_TRANSACTIONFAILED;
	}
	CleanUpContainerRestoreRequest(&request);
}

static enum
{
	CRTHREAD_CMDCREATETRANS = WT_CMD_USER_START,
	CRTHREAD_MSGCOMMITTRANS,
	CRTHREAD_MSGFAILEDTOOPEN,
	CRTHREAD_MSGNOTHINGTODO,
	CRTHREAD_MSGLOADINGHOG,
	CRTHREAD_MSGCONTAINERNOTFOUND,
};

WorkerThread *wtContainerRestore;
static StashTable gAllContainerRestoreRequests;
static StashTable gAllAccountRestoreRequests;

static void AddMarkContainerRestoredInAccountStubToRequest(TransactionRequest *request, ContainerID accountID, GlobalType containerType, ContainerID containerID, RestoreType restoreType);

static void wt_ContainerRestoreThreadRestoreContainer(void *user_data, ContainerRestoreRequest **request, WTCmdPacket *packet)
{
	ContainerRef **ppRefs = NULL;
	void *the_hog = NULL;

	PERFINFO_AUTO_START_FUNC();

	wtQueueMsg(wtContainerRestore, CRTHREAD_MSGLOADINGHOG, request, sizeof(ContainerRestoreRequest*));

	PERFINFO_AUTO_START("HoggLoad", 1);
	//We'll open the hog here once since each dependant container will hit it again.
	if (!(the_hog = objOpenRestoreStateHog(request[0]->pRestoreState)))
	{
		ErrorDeferredf("Could not open hog file %s for container restore", request[0]->pRestoreState->filename);
		wtQueueMsg(wtContainerRestore, CRTHREAD_MSGFAILEDTOOPEN, request, sizeof(ContainerRestoreRequest*));
		PERFINFO_AUTO_STOP(); // HoggLoad
		PERFINFO_AUTO_STOP();
		return;
	}

	PERFINFO_AUTO_STOP(); // HoggLoad

	request[0]->pRequest = objCreateTransactionRequest();

	if (buildRestoreTransaction(request[0]->eGlobalType, request[0]->sourceID, request[0]->pRestoreState, request[0]->pRequest, request[0]->bRecursive,  true, request[0]->bAlertOnExistingContainer))
	{
		if (eaSize(&request[0]->pRequest->ppBaseTransactions))
		{
			if(!request[0]->pConRef)
			{
				request[0]->pConRef = StructCreate(parse_ContainerRef);
			}

			request[0]->pConRef->containerType = request[0]->eGlobalType;
			request[0]->pConRef->containerID = request[0]->sourceID;

			if(request[0]->bUseAccountID && request[0]->accountID)
				AddMarkContainerRestoredInAccountStubToRequest(request[0]->pRequest, request[0]->accountID, request[0]->eGlobalType, request[0]->sourceID, request[0]->eRestoreType);

			wtQueueMsg(wtContainerRestore, CRTHREAD_MSGCOMMITTRANS, request, sizeof(ContainerRestoreRequest*));
		}
		else
		{
			ErrorDeferredf("Container restore contained no differences for container %s[%u] in file %s",
				GlobalTypeToName(request[0]->eGlobalType), request[0]->sourceID, request[0]->pRestoreState->hog_file);

			wtQueueMsg(wtContainerRestore, CRTHREAD_MSGNOTHINGTODO, request, sizeof(ContainerRestoreRequest*));
		}
	}
	else
	{
		wtQueueMsg(wtContainerRestore, CRTHREAD_MSGCONTAINERNOTFOUND, request, sizeof(ContainerRestoreRequest*));
	}

	objCloseRestoreStateHog(the_hog);
	PERFINFO_AUTO_STOP();
}

static void mt_ApplyContainerRestoreTransaction(void *user_data, ContainerRestoreRequest **request, WTCmdPacket *packet)
{
	request[0]->eRestoreStatus = CR_TRANSACTION_BUILT;
	objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC,
		objCreateManagedReturnVal(request[0]->callbackData.cbFunc ? request[0]->callbackData.cbFunc : RestoreContainer_CB, request[0]), "RestoreContainerVerify", request[0]->pRequest);	

	objDestroyTransactionRequest(request[0]->pRequest);
	request[0]->pRequest = NULL;
}

static void mt_CouldNotOpenHog(void *user_data, ContainerRestoreRequest **request, WTCmdPacket *packet)
{
	StructDestroySafe(parse_ContainerRestoreState, &request[0]->pRestoreState);
	request[0]->bDone = true;
	request[0]->eRestoreStatus = CR_NOTHINGTODO;
}

static void mt_NothingToDo(void *user_data, ContainerRestoreRequest **request, WTCmdPacket *packet)
{
	objDestroyTransactionRequest(request[0]->pRequest);
	request[0]->pRequest = NULL;
	request[0]->bDone = true;
	request[0]->eRestoreStatus = CR_NOTHINGTODO;
	StructDestroySafe(parse_ContainerRestoreState, &request[0]->pRestoreState);
}

static void mt_LoadingHog(void *user_data, ContainerRestoreRequest **request, WTCmdPacket *packet)
{
	request[0]->eRestoreStatus = CR_LOADINGHOG;
}

static void mt_ContainerNotFound(void *user_data, ContainerRestoreRequest **request, WTCmdPacket *packet)
{
	objDestroyTransactionRequest(request[0]->pRequest);
	request[0]->pRequest = NULL;
	request[0]->bDone = true;
	request[0]->eRestoreStatus = CR_CONTAINERNOTFOUND;
	StructDestroySafe(parse_ContainerRestoreState, &request[0]->pRestoreState);
}

void initContainerRestoreStashTable()
{
	gAllContainerRestoreRequests = stashTableCreateFixedSize(1000, sizeof(ContainerRef));
	gAllAccountRestoreRequests = stashTableCreateWithStringKeys(1000, StashDeepCopyKeys);
}

void initContainerRestoreThread()
{
	int iWorkerThreadCmdQueueSize;

#ifdef _M_X64
	iWorkerThreadCmdQueueSize = 1024 * 1024;
#else
	iWorkerThreadCmdQueueSize = 1024;
#endif

	//Setup container restore worker thread.
	wtContainerRestore = wtCreateEx(iWorkerThreadCmdQueueSize, 1024, NULL, "ContainerRestoreThread", true);
	wtRegisterCmdDispatch(wtContainerRestore, CRTHREAD_CMDCREATETRANS, wt_ContainerRestoreThreadRestoreContainer);
	wtRegisterMsgDispatch(wtContainerRestore, CRTHREAD_MSGCOMMITTRANS, mt_ApplyContainerRestoreTransaction);
	wtRegisterMsgDispatch(wtContainerRestore, CRTHREAD_MSGFAILEDTOOPEN, mt_CouldNotOpenHog);
	wtRegisterMsgDispatch(wtContainerRestore, CRTHREAD_MSGNOTHINGTODO, mt_NothingToDo);
	wtRegisterMsgDispatch(wtContainerRestore, CRTHREAD_MSGLOADINGHOG, mt_LoadingHog);
	wtRegisterMsgDispatch(wtContainerRestore, CRTHREAD_MSGCONTAINERNOTFOUND, mt_ContainerNotFound);

	wtSetProcessor(wtContainerRestore, THREADINDEX_MISC);
	wtSetThreaded(wtContainerRestore, true, 0, false);
	wtSetSkipIfFull(wtContainerRestore, true);
	wtStart(wtContainerRestore);

	initContainerRestoreStashTable();
}

// Does not take ownership of ref, which can be allocated on the stack.
const ContainerRestoreRequest *GetContainerRestoreRequest(const ContainerRef *ref)
{
	ContainerRestoreRequest *pRequest = NULL;
	if(stashFindPointer(gAllContainerRestoreRequests, ref, &pRequest))
	{
		pRequest->iRefCount += 1;
		return pRequest;
	}

	return NULL;
}

void ReleaseContainerRestoreRequest(const ContainerRestoreRequest **ppRequest)
{
	// Casting away const-ness to correct the ref count.
	// The const is to confirm that anyone accessing ContainerRestoreRequest this way does not change it.
	ContainerRestoreRequest *pRequest;
	if(!ppRequest || !*ppRequest)
		return;

	pRequest = (ContainerRestoreRequest*) *ppRequest;
	pRequest->iRefCount -= 1;
	ppRequest = NULL;
}

AUTO_COMMAND ACMD_CATEGORY(ObjectDB) ACMD_ACCESSLEVEL(9);
int dbContainerRestoreStatus(GlobalType containerType, ContainerID containerID)
{
	int eRestoreStatus = CR_UNKNOWNRESTORE;
	ContainerRef ref;
	const ContainerRestoreRequest *pRequest;
	ref.containerType = containerType;
	ref.containerID = containerID;
	pRequest = GetContainerRestoreRequest(&ref);
	if(pRequest)
	{
		eRestoreStatus = pRequest->eRestoreStatus;
		ReleaseContainerRestoreRequest(&pRequest);
	}

	return eRestoreStatus;
}

// Returns true if a request for that id already exists
// Takes ownership of ref
// Any ContainerRestoreRequest created by this should only be cleaned up by calling CleanUpContainerRestoreRequest
bool GetOrCreateContainerRestoreRequest(ContainerRef *ref, ContainerRestoreRequest **ppRequest)
{
	if(stashFindPointer(gAllContainerRestoreRequests, &ref, ppRequest))
	{
		StructDestroy(parse_ContainerRef, ref);
		(*ppRequest)->iRefCount += 1;
		return true;
	} 
	else
	{
		*ppRequest = (ContainerRestoreRequest*)calloc(1,sizeof(ContainerRestoreRequest));
		stashAddPointer(gAllContainerRestoreRequests, ref, *ppRequest, false);
		(*ppRequest)->iRefCount += 1;
		return false;
	}
}

// If there are any problems with ContainerRestoreRequests being destroyed too early, run this command to keep them cached forever.
static bool gDestroyCompletedContainerRestoreRequests = true;
AUTO_CMD_INT(gDestroyCompletedContainerRestoreRequests, DestroyCompletedContainerRestoreRequests);

void CleanUpContainerRestoreRequest(ContainerRestoreRequest **ppRequest)
{
	ContainerRestoreRequest *pRequest;
	if(!ppRequest || !*ppRequest)
		return;
	
	pRequest = *ppRequest;
	pRequest->bDone = true;
	StructDestroySafe(parse_ContainerRestoreState, &pRequest->pRestoreState);
	StructDestroySafe(parse_ContainerRef, &pRequest->pConRef);
	pRequest->iRefCount -= 1;
	if(gDestroyCompletedContainerRestoreRequests && !pRequest->bKeepCached && pRequest->bDone && pRequest->eRestoreStatus >= 0)
	{
		ContainerRef ref = {0};
		ref.containerType = pRequest->eGlobalType;
		ref.containerID = pRequest->sourceID;
		stashRemovePointer(gAllContainerRestoreRequests, &ref, NULL);
		free(pRequest);
	}
	ppRequest = NULL;
}

int RestoreContainer_internal(GlobalType type, ContainerID containerID, ContainerRestoreState *rs, bool bUseAccountID, ContainerID accountID, bool recursive, bool alertOnExistingContainer, bool forceKeepCached)
{
	int result = 0;
	int queue_success = 0;
	ContainerRestoreRequest *pRequest;
	ContainerRef *ref;

	if(IsThisCloneObjectDB())
	{
		return CR_RESTORINGONCLONE;
	}

	ref = StructCreate(parse_ContainerRef);
	ref->containerType = type;
	ref->containerID = containerID;

	if(GetOrCreateContainerRestoreRequest(ref, &pRequest))
	{
		if(!pRequest->bDone)
			return CR_ALREADYREQUESTED;
	}
	
	pRequest->sourceID = containerID;
	pRequest->eGlobalType = type;
	pRequest->bKeepCached = forceKeepCached;

	if(objDoesDeletedContainerExist(type, containerID))
	{
		pRequest->bDone = true;
		pRequest->eRestoreStatus = CR_CONTAINERINDELETEDCACHE;
		return CR_CONTAINERINDELETEDCACHE;
	}

	if(objDoesContainerExist(type, containerID) && !objIsContainerOwnedByMe(type, containerID))
	{
		pRequest->bDone = true;
		pRequest->eRestoreStatus = CR_CONTAINERNOTOWNEDBYDB;
		return CR_CONTAINERNOTOWNEDBYDB;
	}

	pRequest->iTimeRequested = timeSecondsSince2000();
	pRequest->bDone = false;
	pRequest->bUseAccountID = bUseAccountID;
	pRequest->accountID = accountID;
	pRequest->pRestoreState = StructClone(parse_ContainerRestoreState, rs);
	pRequest->bRecursive = recursive;
	pRequest->callbackData.cbFunc = NULL;
	pRequest->bAlertOnExistingContainer = alertOnExistingContainer;

	queue_success = wtQueueCmd(wtContainerRestore, CRTHREAD_CMDCREATETRANS, &pRequest, sizeof(ContainerRestoreRequest*));
	if(queue_success)
	{
		pRequest->eRestoreStatus = CR_QUEUED;
		result = CR_QUEUED;
	}
	else
	{
		pRequest->bDone = true;
		StructDestroySafe(parse_ContainerRestoreState, &pRequest->pRestoreState);
		pRequest->eRestoreStatus = CR_QUEUEISFULL;
		result = CR_QUEUEISFULL;
	}

	return result;
}

AUTO_COMMAND ACMD_CATEGORY(ObjectDB) ACMD_NAME(RestoreContainer) ACMD_ACCESSLEVEL(9);
int RestoreContainer(CmdContext *context, GlobalType type, U32 id, ContainerRestoreState *rs)
{
	return RestoreContainer_internal(type, id, rs, false, 0, true, false, true);
}

AUTO_COMMAND ACMD_CATEGORY(ObjectDB) ACMD_NAME(RestoreContainerWithAccountID) ACMD_ACCESSLEVEL(9);
int RestoreContainerWithAccountID(CmdContext *context, GlobalType type, U32 id, U32 accountid, ContainerRestoreState *rs)
{
	return RestoreContainer_internal(type, id, rs, true, accountid, true, false, true);
}

static void AutoUndeleteCharacter_CB(TransactionReturnVal *returnVal, ContainerRef *cbData)
{
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		servLog(LOG_CONTAINER, "AutoUndeleteSucceeded", "ContainerType \"%s\" ContainerID %u ReturnString \"%s\"", GlobalTypeToName(cbData->containerType), cbData->containerID, returnVal->pBaseReturnVals[0].returnString);
	}
	else
	{
		servLog(LOG_CONTAINER, "AutoUndeleteFailed", "ReturnString \"%s\"", returnVal->pBaseReturnVals[0].returnString);
	}
	StructDestroy(parse_ContainerRef, cbData);
}

static void AutoUndeleteCharacterRestoreRequest_CB(TransactionReturnVal *returnVal, ContainerRestoreRequest *cbData)
{
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		servLog(LOG_CONTAINER, "AutoUndeleteSucceeded", "ContainerType \"%s\" ContainerID %u ReturnString \"%s\"", GlobalTypeToName(cbData->eGlobalType), cbData->sourceID, returnVal->pBaseReturnVals[0].returnString);
	}
	else
	{
		servLog(LOG_CONTAINER, "AutoUndeleteFailed", "ReturnString \"%s\"", returnVal->pBaseReturnVals[0].returnString);
	}
	CleanUpContainerRestoreRequest(&cbData);
}

static void MarkCharacterRestored_CB(TransactionReturnVal *returnVal, ContainerRef *cbData)
{
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		servLog(LOG_CONTAINER, "MarkCharacterRestoredSucceeded", "ContainerType \"%s\" ContainerID %u ReturnString \"%s\"", GlobalTypeToName(cbData->containerType), cbData->containerID, returnVal->pBaseReturnVals[0].returnString);
	}
	else
	{
		servLog(LOG_CONTAINER, "MarkCharacterRestoredFailed", "ContainerType \"%s\" ContainerID %u ReturnString \"%s\"", GlobalTypeToName(cbData->containerType), cbData->containerID, returnVal->pBaseReturnVals[0].returnString);
	}
	StructDestroy(parse_ContainerRef, cbData);
}

AUTO_COMMAND ACMD_CATEGORY(ObjectDB) ACMD_ACCESSLEVEL(9);
int RestoreOfflineEntityPlayer(CmdContext *context, ContainerID containerID, ContainerID accountID, bool forceOnline)
{
	int result;
	ContainerRestoreState *rs;
	Container *con;

	con = objGetContainerEx(GLOBALTYPE_ACCOUNTSTUB, accountID, true, false, true);

	if(con)
	{
		AccountStub *stub = con->containerData;
		if(stub)
		{
			bool found = false;
			EARRAY_CONST_FOREACH_BEGIN(stub->eaOfflineCharacters, i, size);
			{
				if(stub->eaOfflineCharacters[i] && stub->eaOfflineCharacters[i]->iContainerID == containerID)
				{
					found = true;
					if(stub->eaOfflineCharacters[i]->restored && !forceOnline)
					{
						objUnlockContainer(&con);
						return CR_ALREADYRESTOREDFROMOFFLINE;
					}
				}
			}
			EARRAY_FOREACH_END;

			if(!found)
			{
				objUnlockContainer(&con);
				return CR_CONTAINERACCOUNTMISMATCH;
			}
		}
		else
		{
			objUnlockContainer(&con);
			return CR_CONTAINERACCOUNTMISMATCH;
		}
		objUnlockContainer(&con);
	}
	else
	{
		return CR_CONTAINERACCOUNTMISMATCH;
	}

	if(objDoesContainerExist(GLOBALTYPE_ENTITYPLAYER, containerID) && !forceOnline)
	{
		TransactionRequest *request = NULL;
		ContainerRef *pConRef = NULL;

		pConRef = StructCreate(parse_ContainerRef);
		pConRef->containerID = containerID;
		pConRef->containerType = GLOBALTYPE_ENTITYPLAYER;

		request = objCreateTransactionRequest();
		AddMarkEntityPlayerRestoredInAccountStubToRequest(request, accountID, containerID);
		objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC,
			objCreateManagedReturnVal(MarkCharacterRestored_CB, pConRef), "MarkCharacterRestored", request);	

		objDestroyTransactionRequest(request);

		ErrorOrAlert("OBJECTDB_AUTORESTOREWEIRDNESS", "Fixing %u@%u that is marked as offline, but is in snapshot.", containerID, accountID);		
		return CR_CHARACTERISNOTOFFLINE;
	}

	if(objDoesDeletedContainerExist(GLOBALTYPE_ENTITYPLAYER, containerID))
	{
		TransactionRequest *request = NULL;
		ContainerRef *pConRef = NULL;

		pConRef = StructCreate(parse_ContainerRef);
		pConRef->containerID = containerID;
		pConRef->containerType = GLOBALTYPE_ENTITYPLAYER;

		request = objCreateTransactionRequest();
		AddContainerUndeleteToRequest(request, GLOBALTYPE_ENTITYPLAYER, containerID, objServerType(), objServerID(), NULL);
		AddMarkEntityPlayerRestoredInAccountStubToRequest(request, accountID, containerID);
		objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC,
			objCreateManagedReturnVal(AutoUndeleteCharacter_CB, pConRef), "AutoUndeleteCharacter", request);	

		objDestroyTransactionRequest(request);
		return CR_QUEUED;		
	}
	else
	{
		rs = objCheckOfflineRestoreState(GLOBALTYPE_ENTITYPLAYER, containerID);
		if(rs)
		{
			result = RestoreContainer_internal(GLOBALTYPE_ENTITYPLAYER, containerID, rs, true, accountID, false, !forceOnline, true);
			StructDestroy(parse_ContainerRestoreState, rs);
		}
		else
		{
			return CR_CONTAINERNOTFOUND;
		}
	}

	return result;
}

static void AddMarkContainerRestoredInAccountStubToRequest(TransactionRequest *request, ContainerID accountID, GlobalType containerType, ContainerID containerID, RestoreType restoreType)
{
	switch(restoreType)
	{
		case RESTORETYPE_ENTITYPLAYER:
			AddMarkEntityPlayerRestoredInAccountStubToRequest(request, accountID, containerID);
			break;
		case RESTORETYPE_ACCOUNTWIDE:
			AddMarkAccountWideContainerRestoredInAccountStubToRequest(request, accountID, containerType);
			break;
		default:
			assertmsg(0, "Someone called this with a new restore type that is not yet supported.");
	}
}

static void BuildTransactionToFixMarks(ContainerID accountID, GlobalType containerType, ContainerID containerID, RestoreType restoreType)
{
	TransactionRequest *request = NULL;
	ContainerRef *pConRef = NULL;

	pConRef = StructCreate(parse_ContainerRef);
	pConRef->containerID = containerID;
	pConRef->containerType = containerType;
	request = objCreateTransactionRequest();
	AddMarkContainerRestoredInAccountStubToRequest(request, accountID, containerType, containerID, restoreType);
	objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC,
		objCreateManagedReturnVal(MarkCharacterRestored_CB, pConRef), "MarkCharacterRestored", request);	

	objDestroyTransactionRequest(request);
}

static void BuildTransactionToPerformUndelete(ContainerRestoreRequest *pRestoreRequest, 
												ContainerID accountID, 
												GlobalType containerType, 
												ContainerID containerID, 
												RestoreType restoreType,
												const RestoreCallbackData *callbackData)
{
	TransactionRequest *transactionRequest;
	ContainerRef *pConRef;
	pConRef = StructCreate(parse_ContainerRef);
	pConRef->containerID = containerID;
	pConRef->containerType = containerType;
	pRestoreRequest->pConRef = pConRef;
	pRestoreRequest->eGlobalType = containerType;
	pRestoreRequest->pRestoreState = NULL;
	pRestoreRequest->sourceID = containerID;
	pRestoreRequest->bUseAccountID = false;
	pRestoreRequest->accountID = 0;
	pRestoreRequest->iTimeRequested = timeSecondsSince2000();
	pRestoreRequest->bDone = false;
	pRestoreRequest->bRecursive = false;
	if(callbackData)
		pRestoreRequest->callbackData = *callbackData;

	transactionRequest = objCreateTransactionRequest();
	AddContainerUndeleteToRequest(transactionRequest, containerType, containerID, objServerType(), objServerID(), NULL);
	AddMarkContainerRestoredInAccountStubToRequest(transactionRequest, accountID, containerType, containerID, restoreType);

	objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC,
		objCreateManagedReturnVal(pRestoreRequest->callbackData.cbFunc ? pRestoreRequest->callbackData.cbFunc : AutoUndeleteCharacterRestoreRequest_CB, pRestoreRequest), "AutoUndeleteCharacter", transactionRequest);	

	objDestroyTransactionRequest(transactionRequest);
}

bool AutoRestoreContainerEx(	ContainerID accountID, 
								GlobalType containerType, 
								ContainerID containerID, 
								RestoreType restoreType, 
								const RestoreCallbackData *callbackData)
{
	PERFINFO_AUTO_START_FUNC();
	if(objDoesContainerExist(containerType, containerID))
	{
		BuildTransactionToFixMarks(accountID, containerType, containerID, restoreType);

		ErrorOrAlert("OBJECTDB_AUTORESTOREWEIRDNESS", "Fixing %s:%u@%u that is marked as offline, but is in snapshot.", GlobalTypeToName(containerType), containerID, accountID);
		PERFINFO_AUTO_STOP();
		return false;
	}
	else if(objDoesDeletedContainerExist(containerType, containerID))
	{
		TransactionRequest *request = NULL;
		ContainerRestoreRequest *pRestoreRequest = NULL;
		ContainerRef *pConRef = NULL;
		ContainerRef *pTempRef = NULL;
		pTempRef = StructCreate(parse_ContainerRef);
		pTempRef->containerType = containerType;
		pTempRef->containerID = containerID;

		if(GetOrCreateContainerRestoreRequest(pTempRef, &pRestoreRequest))
		{
			if(!pRestoreRequest->bDone)
			{
				PERFINFO_AUTO_STOP();
				return true;
			}
			else if(pRestoreRequest->iTimeRequested > timeSecondsSince2000() - CONTAINER_RESTORE_RETRY_TIMEOUT)
			{
				if(pRestoreRequest->eRestoreStatus < 0)
					ErrorOrCriticalAlert("OBJECTDB_AUTOUNDELETEFAILURE", "Failed to automatically undelete %u@%u, with error code %d. ", containerID, accountID, pRestoreRequest->eRestoreStatus);

				PERFINFO_AUTO_STOP();
				return false;
			}
		}

		BuildTransactionToPerformUndelete(pRestoreRequest, accountID, containerType, containerID, restoreType, callbackData);

		PERFINFO_AUTO_STOP();
		return true;
	}
	else
	{
		ContainerRestoreState *rs = objCheckOfflineRestoreState(containerType, containerID);
		if(rs)
		{
			bool queue_success = false;
			ContainerRestoreRequest *pRestoreRequest = NULL;
			ContainerRef *pConRef = NULL;
			ContainerRef *pTempRef = NULL;
			pTempRef = StructCreate(parse_ContainerRef);
			pTempRef->containerType = containerType;
			pTempRef->containerID = containerID;

			if(GetOrCreateContainerRestoreRequest(pTempRef, &pRestoreRequest))
			{
				if(!pRestoreRequest->bDone)
				{
					PERFINFO_AUTO_STOP();
					return true;
				}
				else if(pRestoreRequest->iTimeRequested > timeSecondsSince2000() - CONTAINER_RESTORE_RETRY_TIMEOUT)
				{
					if(pRestoreRequest->eRestoreStatus < 0)
						ErrorOrCriticalAlert("OBJECTDB_AUTORESTOREFAILURE", "Failed to automatically restore %u@%u, with error code %d. ", containerID, accountID, pRestoreRequest->eRestoreStatus);

					PERFINFO_AUTO_STOP();
					return false;
				}
			}

			pConRef = StructCreate(parse_ContainerRef);
			pConRef->containerID = containerID;
			pConRef->containerType = containerType;
			pRestoreRequest->pConRef = pConRef;
			pRestoreRequest->eGlobalType = containerType;
			pRestoreRequest->pRestoreState = rs;
			pRestoreRequest->sourceID = containerID;
			pRestoreRequest->bUseAccountID = true;
			pRestoreRequest->accountID = accountID;
			pRestoreRequest->iTimeRequested = timeSecondsSince2000();
			pRestoreRequest->bDone = false;
			pRestoreRequest->bRecursive = false;
			pRestoreRequest->pRestoreState = StructClone(parse_ContainerRestoreState, rs);
			if(callbackData)
				pRestoreRequest->callbackData = *callbackData;
			pRestoreRequest->bAlertOnExistingContainer = true;
			pRestoreRequest->eRestoreType = restoreType;
			queue_success = wtQueueCmd(wtContainerRestore, CRTHREAD_CMDCREATETRANS, &pRestoreRequest, sizeof(ContainerRestoreRequest*));
			if(queue_success)
			{
				pRestoreRequest->eRestoreStatus = CR_QUEUED;
				PERFINFO_AUTO_STOP();
				return true;
			}
			else
			{
				pRestoreRequest->bDone = true;
				StructDestroySafe(parse_ContainerRestoreState, &pRestoreRequest->pRestoreState);
				pRestoreRequest->eRestoreStatus = CR_QUEUEISFULL;
				PERFINFO_AUTO_STOP();
				return false;
			}
		}
		else
		{
			ErrorOrCriticalAlert("OBJECTDB_AUTORESTOREFAILURE", "Failed to automatically restore %u@%u. It is not in %s.", containerID, accountID, gContainerSource.offlinePath);
		}
	}

	PERFINFO_AUTO_STOP();
	return false;
}

//This returns a bool of whether it initiated any restores
bool InitiateAutoRestore(U32 accountID)
{
	bool initiatedRestore = false;
	Container *con = NULL;
	AccountStub *stub = NULL;

	PERFINFO_AUTO_START_FUNC();
	con = objGetContainerEx(GLOBALTYPE_ACCOUNTSTUB, accountID, true, false, true);

	if(con)
	{
		stub = con->containerData;
	}

	if(stub)
	{
		EARRAY_FOREACH_BEGIN(stub->eaOfflineCharacters, i);
		{
			if(stub->eaOfflineCharacters[i] && !stub->eaOfflineCharacters[i]->restored)
			{
				initiatedRestore |= AutoRestoreEntityPlayer(accountID, stub->eaOfflineCharacters[i]->iContainerID);
			}
		}
		EARRAY_FOREACH_END;

		EARRAY_FOREACH_BEGIN(stub->eaOfflineAccountWideContainers, i);
		{
			if(stub->eaOfflineAccountWideContainers[i] && !stub->eaOfflineAccountWideContainers[i]->restored)
			{
				if(stub->eaOfflineAccountWideContainers[i]->containerType == GLOBALTYPE_ENTITYSHAREDBANK && LazyRestoreSharedBank())
					continue;
				AutoRestoreContainerEx(accountID, stub->eaOfflineAccountWideContainers[i]->containerType, accountID, RESTORETYPE_ACCOUNTWIDE, NULL);
			}
		}
		EARRAY_FOREACH_END;
	}

	if(con)
		objUnlockContainer(&con);

	PERFINFO_AUTO_STOP();
	return initiatedRestore;
}

void RestoreSingleAccountWideContainer_CB(TransactionReturnVal *returnVal, ContainerRestoreRequest *request)
{
	if(returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		SlowRemoteCommandReturn_DBCheckAccountWideContainerExistsWithRestore(request->callbackData.slowCommandID, true);
	}
	else
	{
		SlowRemoteCommandReturn_DBCheckAccountWideContainerExistsWithRestore(request->callbackData.slowCommandID, false);
	}
	CleanUpContainerRestoreRequest(&request);
}

//Returns true if the container exists
AUTO_COMMAND_REMOTE_SLOW(bool);
void DBCheckAccountWideContainerExistsWithRestore(SlowRemoteCommandID iCmdID, ContainerID accountID, GlobalType containerType)
{
	Container *accountStubContainer = NULL;
	AccountStub *accountStub = NULL;
	if(objDoesContainerExist(containerType, accountID))
	{
		SlowRemoteCommandReturn_DBCheckAccountWideContainerExistsWithRestore(iCmdID, true);
		return;
	}
	
	accountStubContainer = objGetContainerEx(GLOBALTYPE_ACCOUNTSTUB, accountID, true, false, true);

	if(accountStubContainer && (accountStub = accountStubContainer->containerData) && eaSize(&accountStub->eaOfflineAccountWideContainers))
	{
		AccountWideContainerStub *containerStub = eaIndexedGetUsingInt(&accountStub->eaOfflineAccountWideContainers, containerType);
		if(containerStub && !containerStub->restored)
		{
			RestoreCallbackData callbackData = {0};
			callbackData.cbFunc = RestoreSingleAccountWideContainer_CB;
			callbackData.slowCommandID = iCmdID;
			AutoRestoreContainerEx(accountID, containerType, accountID, RESTORETYPE_ACCOUNTWIDE, &callbackData);
			objUnlockContainer(&accountStubContainer);
			return;
		}
	}

	if(accountStubContainer)
		objUnlockContainer(&accountStubContainer);
	
	SlowRemoteCommandReturn_DBCheckAccountWideContainerExistsWithRestore(iCmdID, false);
}

AUTO_COMMAND ACMD_CATEGORY(ObjectDB) ACMD_NAME(ConsoleRestoreContainer) ACMD_ACCESSLEVEL(9);
int ConsoleRestoreContainer(CmdContext *context, const char *typeName, U32 id, char *hog_file, char *filename)
{
	ContainerRestoreState *rs;
	int result = 0;
	GlobalType type;

	if(!typeName || !typeName[0] || !id)
		return CR_CONTAINERNOTFOUND;

	type = NameToGlobalType(typeName);

	if(!type)
		return CR_CONTAINERNOTFOUND;

	rs = StructCreate(parse_ContainerRestoreState);
	estrPrintf(&rs->hog_file, "%s", hog_file);
	estrPrintf(&rs->filename, "%s", filename);
	
	result = RestoreContainer(context, type, id, rs);
	StructDestroy(parse_ContainerRestoreState, rs);
	return result;
}

void GetOfflineDependentContainers(const char *file, GlobalType containerType, ContainerID containerID, ContainerRef ***pppRefs)
{
	ContainerRestoreState *rs = objCheckOfflineRestoreState(containerType, containerID);
	if(rs)
	{
		char *diffstr = NULL;
		estrCreate(&diffstr);
		objContainerGetRestoreStringFromState(containerType, containerID, rs, &diffstr, pppRefs, false, false);
		StructDestroy(parse_ContainerRestoreState, rs);
		estrDestroy(&diffstr);
	}
}

void InitAccountRestoreRequest(AccountRestoreRequest *accountRestoreRequest, const char *displayName)
{
	accountRestoreRequest->displayName = strdup(displayName);
}

void InitTemporaryAccountRestoreRequest(AccountRestoreRequest *accountRestoreRequest, const char *displayName)
{
	accountRestoreRequest->displayName = displayName;
}

AccountRestoreRequest *CreateAccountRestoreRequest(const char *displayName)
{
	AccountRestoreRequest *request;
	request = calloc(1, sizeof(AccountRestoreRequest));
	InitAccountRestoreRequest(request, displayName);
	return request;
}

AccountRestoreRequest *GetCachedAccountRestoreRequest(const char *displayName)
{
	AccountRestoreRequest *accountRestoreRequest;
	if(!stashFindPointer(gAllAccountRestoreRequests, displayName, &accountRestoreRequest))
	{
		accountRestoreRequest = CreateAccountRestoreRequest(displayName);
		stashAddPointer(gAllAccountRestoreRequests, displayName, accountRestoreRequest, false);
		accountRestoreRequest->cached = true;
	}

	return accountRestoreRequest;
}

void DestroyAccountRestoreRequest(AccountRestoreRequest *accountRestoreRequest)
{
	if(accountRestoreRequest->cached)
		return;

	SAFE_FREE(accountRestoreRequest->displayName);
	SAFE_FREE(accountRestoreRequest->characterName);
	ea32Destroy(&accountRestoreRequest->offlineCharacterIDs);
	free(accountRestoreRequest);
}

void RestoreAccountWideContainersForAccountStub(AccountStub *pStub, bool ignoreLazyLoad)
{
	EARRAY_FOREACH_BEGIN(pStub->eaOfflineAccountWideContainers, i);
	{
		AccountWideContainerStub *containerStub = pStub->eaOfflineAccountWideContainers[i];
		if(containerStub && !containerStub->restored)
		{
			if(!ignoreLazyLoad)
			{
				if(containerStub->containerType == GLOBALTYPE_ENTITYSHAREDBANK && LazyRestoreSharedBank())
					continue;
			}
			AutoRestoreContainerEx(pStub->iAccountID, containerStub->containerType, pStub->iAccountID, RESTORETYPE_ACCOUNTWIDE, NULL);
		}
	}
	EARRAY_FOREACH_END;
}

bool RestoreContainersForAccountStubEx(char **resultString, AccountStub *pStub, const char *pCharacterName, INT_EARRAY *peaOfflineCharacters, const RestoreCallbackData *callbackData, bool ignoreLazyLoad)
{
	bool foundSpecificCharacter = false;
	bool foundOffline = false;

	EARRAY_FOREACH_BEGIN(pStub->eaOfflineCharacters, i);
	{
		CharacterStub *character = pStub->eaOfflineCharacters[i];
		if(character && !character->restored)
		{
			if(!foundOffline)
			{
				foundOffline = true;
				estrAppend2(resultString, "OfflineCharacters:\n");
			}

			if(peaOfflineCharacters)
				ea32PushUnique(peaOfflineCharacters, character->iContainerID);

			if(character->savedName && character->savedName[0])
				estrConcatf(resultString, "%d - %s\n", character->iContainerID, character->savedName);
			else
				estrConcatf(resultString, "%d\n", character->iContainerID);

			if(!pCharacterName || stricmp(pCharacterName, character->savedName) == 0)
			{
				AutoRestoreEntityPlayerEx(pStub->iAccountID, character->iContainerID, callbackData);
				foundSpecificCharacter = true;
			}
			else
			{
				AutoRestoreEntityPlayerEx(pStub->iAccountID, character->iContainerID, NULL);
			}
		}
	}
	EARRAY_FOREACH_END;

	RestoreAccountWideContainersForAccountStub(pStub, ignoreLazyLoad);
	return foundSpecificCharacter;
}

bool FindOfflineCharactersAndAutoRestoreEx(char **resultString, INT_EARRAY *pOfflineCharacterIDs, const RestoreCallbackData *callbackData, U32 accountId, const char *characterName)
{
	bool foundSpecificCharacter = false;
	// Check account stub for characters
	Container *con = objGetContainerEx(GLOBALTYPE_ACCOUNTSTUB, accountId, true, false, true);
	if(con)
	{
		AccountStub *stub = con->containerData;
		if(stub)
		{
			foundSpecificCharacter = RestoreContainersForAccountStubEx(resultString, stub, characterName, pOfflineCharacterIDs, callbackData, false);
		}
		objUnlockContainer(&con);
	}
	return foundSpecificCharacter;
}

void RestoreContainersForAccountStub_ForEach(Container *con, void *unused)
{
	AccountStub *pStub = con->containerData;
	if(pStub)
		RestoreContainersForAccountStub(pStub);
}

AUTO_COMMAND ACMD_CATEGORY(ObjectDB) ACMD_ACCESSLEVEL(9);
void RestoreAllContainers(void)
{
	ForEachContainerOfType(GLOBALTYPE_ACCOUNTSTUB, RestoreContainersForAccountStub_ForEach, NULL, true);
}