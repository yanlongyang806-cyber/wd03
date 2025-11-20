/***************************************************************************



***************************************************************************/

#include "TransactionRequestManager.h"
#include "LocalTransactionManager_Internal.h"
#include "ServerLib.h"
#include "ResourceInfo.h"
#include "objTransactions.h"
#include "windefinclude.h"
#include "alerts.h"
#include "stringCache.h"
#include "AutoTransSupport.h"

typedef struct TransactionRequestManager
{
	U32 iNextID;

	StashTable ReturnValueNodesByID;

	CRITICAL_SECTION AccessCriticalSection;
} TransactionRequestManager;

// Yes, I know, it's a little crappy to demote this to a singleton, but it's the path of least resistance for
// dealing with managed return vals in the ObjectDB when threading is on
TransactionRequestManager gTransactionRequestManager = {0};

TransactionReturnVal *GetReturnValFromID(U32 iID)
{
	TransactionReturnVal *pRetVal;

	EnterCriticalSection(&gTransactionRequestManager.AccessCriticalSection);
	if (stashIntRemovePointer(gTransactionRequestManager.ReturnValueNodesByID, iID, &pRetVal))
	{
		LeaveCriticalSection(&gTransactionRequestManager.AccessCriticalSection);
		assertmsgf(pRetVal->iID == iID, "A transaction return val has been corrupted. Trans name may be %s", pRetVal->pTransactionName);
		return pRetVal;
	}
	LeaveCriticalSection(&gTransactionRequestManager.AccessCriticalSection);

	return NULL;
}

static U32 GetIDForReturnVal(TransactionReturnVal *pReturnVal, const char *pTransactionName)
{
	bool bGotAConflict = false;
	U32 iCounter = 0;
	U32 iRetVal;

	EnterCriticalSection(&gTransactionRequestManager.AccessCriticalSection);
	while (stashIntFindPointer(gTransactionRequestManager.ReturnValueNodesByID, gTransactionRequestManager.iNextID, NULL))
	{
		bGotAConflict = true;
		iCounter++;
		assertmsgf(iCounter, "More than 4 billion transaction requests currently exist? That seems implausible...");
		gTransactionRequestManager.iNextID++;
		if (gTransactionRequestManager.iNextID == 0)
		{
			gTransactionRequestManager.iNextID++;
		}
	}

	iRetVal = gTransactionRequestManager.iNextID++;
	if (gTransactionRequestManager.iNextID == 0)
	{
		gTransactionRequestManager.iNextID++;
	}

	stashIntAddPointer(gTransactionRequestManager.ReturnValueNodesByID, iRetVal, pReturnVal, false);
	LeaveCriticalSection(&gTransactionRequestManager.AccessCriticalSection);

	if (bGotAConflict)
	{
		TriggerAlert(allocAddString("LEAKEDTRANSRETURNVAL"), "A transaction return val has presumably been leaked", 
			ALERTLEVEL_WARNING, ALERTCATEGORY_NETOPS, 0,
			GetAppGlobalType(), GetAppGlobalID(), GetAppGlobalType(), GetAppGlobalID(),
			getHostName(), 0);
	}

	pReturnVal->iID = iRetVal;
	pReturnVal->pTransactionName = allocAddString(pTransactionName);

	return iRetVal;
}




FILE *trDebugLogFile;

void HandleTransactionReturnVal(Packet *pPacket)
{
	PERFINFO_AUTO_START("HandleTransactionReturnVal",1);
	{	
	int iID = pktGetBits(pPacket, 32);
	int i;

	TransactionReturnVal *pReturnVal= GetReturnValFromID(iID);

	if (!pReturnVal)
	{
		//if there's no return val node, that must mean that whoever originally requested a transaction result
		//has now cancelled that request
		PERFINFO_AUTO_STOP();
		return;
	}

	pReturnVal->eOutcome = pktGetBits(pPacket, 2);


	pReturnVal->iNumBaseTransactions = pktGetBitsPack(pPacket, 1);

	assert(pReturnVal->iNumBaseTransactions);

	pReturnVal->pBaseReturnVals = (BaseTransactionReturnVal*)calloc(pReturnVal->iNumBaseTransactions * sizeof(BaseTransactionReturnVal),1);

	for (i=0; i < pReturnVal->iNumBaseTransactions; i++)
	{
		pReturnVal->pBaseReturnVals[i].eOutcome = pktGetBits(pPacket, 2);

		estrAppendFromPacket(&pReturnVal->pBaseReturnVals[i].returnString,pPacket);
	}

	//filelog_printf("transactions.log","TR%d: %s\n",iID,pReturnValNode->pReturnVal->pBaseReturnVals[0].returnString);

	if (pReturnVal->eFlags & TRANSACTIONRETURN_FLAG_MANAGED_RETURN_VAL)
	{
		ManagedReturnValLog_Internal((ActiveTransaction*)pReturnVal, "Transaction Server returned %s.", StaticDefineIntRevLookup(enumTransactionOutcomeEnum, pReturnVal->eOutcome));
	}

	}

	PERFINFO_AUTO_STOP();
}

void InitTransactionRequestManager(void)
{
	gTransactionRequestManager.iNextID = 1;
	gTransactionRequestManager.ReturnValueNodesByID = stashTableCreateInt(2048);
	InitializeCriticalSection(&gTransactionRequestManager.AccessCriticalSection);
}

void RequestNewTransaction_Deprecated(struct LocalTransactionManager *pManager, char *pTransactionName,  int iNumBaseTransactions, BaseTransaction *pBaseTransactions, 
	enumTransactionType eTransType, TransactionReturnVal *pWhereToPutReturnVal)
{
	BaseTransaction **ppTransactionArray = NULL;

	int i;

	if (!pManager)
	{
		return;
	}

	for (i=0; i < iNumBaseTransactions; i++)
	{
		eaPush(&ppTransactionArray, &pBaseTransactions[i]);
	}

	RequestNewTransaction(pManager, pTransactionName, ppTransactionArray, eTransType, pWhereToPutReturnVal, 0);

	eaDestroy(&ppTransactionArray);
}

bool CanTransactionsBeRequested(LocalTransactionManager *pTransManager)
{
	return pTransManager->iThreadID == GetCurrentThreadId();
}

void ValidateReturnVal(const char *pTransactionName, TransactionReturnVal *pWhereToPutReturnVal)
{
	if (pWhereToPutReturnVal->eFlags & TRANSACTIONRETURN_FLAG_MANAGED_RETURN_VAL)
	{
		ValidateManagedReturnValFirstTimeInTransSystem(pTransactionName, pWhereToPutReturnVal);
	}
}

static void DoLocalStructFixup(BaseTransaction *pBaseTrans)
{
	char *pCopyString = NULL;
	estrStackCreate(&pCopyString);
	estrCopy2(&pCopyString, pBaseTrans->pData);

	if (AutoTrans_FixupLocalStructStringIntoParserWriteText(&pCopyString))
	{
		free(pBaseTrans->pData);
		pBaseTrans->pData = strdup(pCopyString);
	}

	estrDestroy(&pCopyString);
}

void RequestNewTransaction(LocalTransactionManager *pTransManager, const char *pTransactionName, BaseTransaction **ppBaseTransactions,
	enumTransactionType eTransType, TransactionReturnVal *pWhereToPutReturnVal, TransactionRequestFlags eFlags)
{
	//static recursion = 0;
	int iTotalDataSize = 0;

	int i;

	int iSizes[MAX_BASE_TRANSACTIONS_PER_TRANSACTION];

	LTMObjectHandle objHandles[MAX_BASE_TRANSACTIONS_PER_TRANSACTION];

	


	bool bIsLocalTransaction = true;
	bool bNeedsSpecialContainerTypeFixupIfLocal = false;

	Packet *pPacket;

	int iNumBaseTransactions = eaSize(&ppBaseTransactions);

	if (!pTransManager)
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	assert(pTransactionName);

	assert(pTransManager->iThreadID == GetCurrentThreadId());
	//assertmsg(!recursion, "RequestNewTransaction cannot be called recursively! This will break horribly!");
	//recursion++;

	if (pWhereToPutReturnVal)
	{
		ValidateReturnVal(pTransactionName, pWhereToPutReturnVal);
	}

	if (iNumBaseTransactions > MAX_BASE_TRANSACTIONS_PER_TRANSACTION)
	{

		ErrorOrAlert("TOO_MANY_BASE_TRANS", "Transaction %s had too many base transactions (%d) (max %d)",
			pTransactionName, iNumBaseTransactions, MAX_BASE_TRANSACTIONS_PER_TRANSACTION);
		if (pWhereToPutReturnVal)
		{
			pWhereToPutReturnVal->eOutcome = TRANSACTION_OUTCOME_FAILURE;
		}
		PERFINFO_AUTO_STOP();
		return;
	}

	//do simple validation of transactees... might be made more complex later
	for (i=0; i < iNumBaseTransactions; i++)
	{
		assertmsg(ppBaseTransactions[i]->recipient.containerType > GLOBALTYPE_NONE && ppBaseTransactions[i]->recipient.containerType < GLOBALTYPE_MAXTYPES, "Transaction requested with invalid global type");
	}

	// Allow wholesale disabling of local transactions by calling EnableLocalTransactions(0)
	if (AreLocalTransactionsEnabled() && pTransManager->pDoesObjectExistCB)
	{
		//check if we can do a local transaction
		for (i=0; i < iNumBaseTransactions; i++)
		{
			//for special container IDs, handle each type separately
			if (ppBaseTransactions[i]->recipient.containerID >= LOWEST_SPECIAL_CONTAINERID)
			{
				if (ppBaseTransactions[i]->recipient.containerID == SPECIAL_CONTAINERID_FIND_BEST_FOR_TRANSACTION
					&& ppBaseTransactions[i]->recipient.containerType == GetAppGlobalType())
				{
					bNeedsSpecialContainerTypeFixupIfLocal = true;
				}
				else
				{
					assert(!LTMIsFullyLocal(pTransManager));
					bIsLocalTransaction = false;
					break;
				}
			}
			else
			{
				//for non-special container IDs, only local if the local manager owns each object
				if (!pTransManager->pDoesObjectExistCB(ppBaseTransactions[i]->recipient.containerType,
					ppBaseTransactions[i]->recipient.containerID, &objHandles[i], NULL, pTransManager->pCBUserData))
				{
					assert(!LTMIsFullyLocal(pTransManager));
					bIsLocalTransaction = false;
					break;
				}
			}
		}
	}
	else
	{
		// If local transactions were disabled, automatically promote
		assert(!LTMIsFullyLocal(pTransManager));
		bIsLocalTransaction = false;
	}

	if (bIsLocalTransaction)
	{
		LocalTransaction localTransaction;
		LocalBaseTransactionState baseTransactionStates[MAX_BASE_TRANSACTIONS_PER_TRANSACTION] = {0};

		if (bNeedsSpecialContainerTypeFixupIfLocal)
		{
			for (i=0; i < iNumBaseTransactions; i++)
			{
				//for special container IDs, handle each type separately
				if (ppBaseTransactions[i]->recipient.containerID >= LOWEST_SPECIAL_CONTAINERID)
				{
					if (ppBaseTransactions[i]->recipient.containerID == SPECIAL_CONTAINERID_FIND_BEST_FOR_TRANSACTION
						&& ppBaseTransactions[i]->recipient.containerType == GetAppGlobalType())
					{
						ppBaseTransactions[i]->recipient.containerID = gServerLibState.containerID;

						if (!pTransManager->pDoesObjectExistCB(ppBaseTransactions[i]->recipient.containerType,
							ppBaseTransactions[i]->recipient.containerID, &objHandles[i], NULL, pTransManager->pCBUserData))
						{
							assertmsg(0, "Transaction Request Manager doesn't think it exists");
						}
					}
				}
			}
		}

		localTransaction.pTransactionName = pTransactionName;
		localTransaction.bAtLeastOneFailure = false;
		localTransaction.eType = eTransType;
		localTransaction.iCompletionCounter = 0;
		localTransaction.iID = GetNextLocalTransactionID(pTransManager);
		localTransaction.ppBaseTransactions = ppBaseTransactions;
		localTransaction.pBaseTransactionStates = baseTransactionStates;		

		if (pWhereToPutReturnVal)
		{
			localTransaction.iReturnValID = GetIDForReturnVal(pWhereToPutReturnVal, pTransactionName);
			pWhereToPutReturnVal->eOutcome = TRANSACTION_OUTCOME_NONE;
		}
		else
		{
			localTransaction.iReturnValID = 0;
		}

		if (!AttemptLocalTransaction(pTransManager, &localTransaction, objHandles))
		{
			if (eFlags & TRANSREQUESTFLAG_DO_LOCAL_STRUCT_FIXUP_FOR_AUTO_TRANS)
			{
				BaseTransaction *pMiddleTrans;

				assertmsgf(iNumBaseTransactions % 2 == 1, "Expect AUTO_TRANS to have an odd number of steps, so I can isolate the middle one for local struct fixup");
				pMiddleTrans = ppBaseTransactions[iNumBaseTransactions / 2];

				DoLocalStructFixup(pMiddleTrans);
			}

			if (eFlags & TRANSREQUESTFLAG_DO_LOCAL_STRUCT_FIXUP_FOR_AUTO_TRANS_WITH_APPENDING)
			{
				for (i = 0; i < iNumBaseTransactions; i++)
				{
					if (strStartsWith(ppBaseTransactions[i]->pData, "runautotrans"))
					{
						DoLocalStructFixup(ppBaseTransactions[i]);
					}
				}
			}

			//filelog_printf("transactions.log","BLOCKED%d %s[%d]: %s\n",localTransaction.iID,GlobalTypeToName(localTransaction.ppBaseTransactions[0]->recipient.containerType),localTransaction.ppBaseTransactions[0]->recipient.containerID,localTransaction.ppBaseTransactions[0]->pData);
			CopyAndBlockTransaction(pTransManager, &localTransaction);
		}
	}
	else
	{
		assertmsg(!LTMIsFullyLocal(pTransManager), "Fully Local LTMs must have a database update callback registered via RegisterDBUpdateDataCallback");

		if (eFlags & TRANSREQUESTFLAG_DO_LOCAL_STRUCT_FIXUP_FOR_AUTO_TRANS)
		{

			BaseTransaction *pMiddleTrans;

			assertmsgf(iNumBaseTransactions % 2 == 1, "Expect AUTO_TRANS to have an odd number of steps, so I can isolate the middle one for local struct fixup");
			pMiddleTrans = ppBaseTransactions[iNumBaseTransactions / 2];
			DoLocalStructFixup(pMiddleTrans);
		}


		if (eFlags & TRANSREQUESTFLAG_DO_LOCAL_STRUCT_FIXUP_FOR_AUTO_TRANS_WITH_APPENDING)
		{
			for (i = 0; i < iNumBaseTransactions; i++)
			{
				if (strStartsWith(ppBaseTransactions[i]->pData, "runautotrans"))
				{
					DoLocalStructFixup(ppBaseTransactions[i]);
				}
			}
		}

		for (i=0; i < iNumBaseTransactions; i++)
		{
			iSizes[i] = (int)strlen(ppBaseTransactions[i]->pData) + 1;
			iTotalDataSize += iSizes[i];
		}
		
		pPacket = CreateLTMPacket(pTransManager, TRANSCLIENT_REQUEST_NEW_TRANSACTION, PacketTrackerFind("RequestNewTransaction", 0, pTransactionName));

		pktSendBitsPack(pPacket, 1, eTransType);
		pktSendBitsPack(pPacket, 1, iNumBaseTransactions);
		pktSendBitsPack(pPacket, 7, iTotalDataSize);

		for (i = 0; i < iNumBaseTransactions; i++)
		{
			PutContainerIDIntoPacket(pPacket, ppBaseTransactions[i]->recipient.containerID);
			PutContainerTypeIntoPacket(pPacket, ppBaseTransactions[i]->recipient.containerType);
			pktSendString(pPacket, ppBaseTransactions[i]->pData);
			if (ppBaseTransactions[i]->pRequestedTransVariableNames)
			{
				pktSendBits(pPacket, 1, 1);
				pktSendString(pPacket, ppBaseTransactions[i]->pRequestedTransVariableNames);
			}
			else
			{
				pktSendBits(pPacket, 1, 0);
			}
		}

		pktSendString(pPacket, pTransactionName);

		if (pWhereToPutReturnVal)
		{
			pktSendBits(pPacket, 1, 1);
			pktSendBits(pPacket, 32, GetIDForReturnVal(pWhereToPutReturnVal, pTransactionName));
			pWhereToPutReturnVal->eOutcome = TRANSACTION_OUTCOME_NONE;
		}
		else
		{
			pktSendBits(pPacket, 1, 0);
		}

		//no pre-failed transaction
		pktSendBits(pPacket, 1, 0);

		//no pre-existing return val string
		pktSendBits(pPacket, 1, 0);

		//no pre-existing database update string
		pktSendBits(pPacket, 1, 0);

		//no pre-existing transaction server update string
		pktSendBits(pPacket, 1, 0);

		pTransManager->averageSentTransactionSize = (pTransManager->averageSentTransactionSize * pTransManager->totalSentTransactions + pktGetSize(pPacket)) / (pTransManager->totalSentTransactions + 1);
		pTransManager->totalSentTransactions++;

		pktSend(&pPacket);
	}

	PERFINFO_AUTO_STOP();
}

void RequestSimpleTransaction(LocalTransactionManager *pTransManager, U32 eRecipientType, U32 iRecipientID, const char *pTransactionName, const char *pData, 
							  enumTransactionType eTransType, TransactionReturnVal *pWhereToPutReturnVal)
{

	BaseTransaction transaction;
	BaseTransaction **ppTransactionList = NULL;

	PERFINFO_AUTO_START_FUNC();

	transaction.recipient.containerID = iRecipientID;
	transaction.recipient.containerType = eRecipientType;
	transaction.pRequestedTransVariableNames = NULL;

	transaction.pData = (char *)pData;

	eaPush(&ppTransactionList, &transaction);

	RequestNewTransaction(pTransManager, pTransactionName, ppTransactionList, eTransType, pWhereToPutReturnVal, 0);

	eaDestroy(&ppTransactionList);
	PERFINFO_AUTO_STOP();
}


void ReleaseReturnValData(LocalTransactionManager *pTransManager, TransactionReturnVal *pReturnVal)
{
	int i;

	if (pReturnVal->iNumBaseTransactions)
	{
		for (i=0; i < pReturnVal->iNumBaseTransactions; i++)
		{
			estrDestroy(&pReturnVal->pBaseReturnVals[i].returnString);
		}

		free(pReturnVal->pBaseReturnVals);
	}
	memset(pReturnVal,0,sizeof(TransactionReturnVal));
}

void CancelAndRelaseReturnValData(LocalTransactionManager *pTransManager, TransactionReturnVal *pReturnVal)
{
	EnterCriticalSection(&gTransactionRequestManager.AccessCriticalSection);
	stashIntRemovePointer(gTransactionRequestManager.ReturnValueNodesByID, pReturnVal->iID, NULL);
	LeaveCriticalSection(&gTransactionRequestManager.AccessCriticalSection);

	if (pReturnVal->eFlags & TRANSACTIONRETURN_FLAG_MANAGED_RETURN_VAL)
	{
		ManagedReturnValLog_Internal((ActiveTransaction*)pReturnVal, "Transaction cancelled");
	}

	ReleaseReturnValData(pTransManager, pReturnVal);
}



Packet *GetPacketToSendThroughTransactionServer(LocalTransactionManager *pManager, PacketTracker *pTracker, GlobalType eDestType, ContainerID iDestID, 
	enumTransPacketCommand eCmd, char *pTransName, TransServerPacketFailureCB *pFailureCB, void *pFailureUserData1, void *pFailureUserData2)
{
	Packet *pRetVal = CreateLTMPacket(pManager, TRANSCLIENT_SEND_PACKET_SIMPLE, pTracker ? pTracker : PacketTrackerFind("RemoteCommand_Uncached", 0, "pTransName"));



	pktSendString(pRetVal, pTransName);

	PutContainerTypeIntoPacket(pRetVal, eDestType);
	PutContainerIDIntoPacket(pRetVal, iDestID);

	//null-terminate the list of containers
	PutContainerTypeIntoPacket(pRetVal, 0);
	if (pFailureCB)
	{
		//because trans server might have different bit size, just do everything as U64
		pktSendBits64(pRetVal, 64, (U64)pFailureCB);
		pktSendBits64(pRetVal, 64, (U64)pFailureUserData1);
		pktSendBits64(pRetVal, 64, (U64)pFailureUserData2);
	}
	else
	{
		pktSendBits64(pRetVal, 64, 0);
	}

	pktSendBits(pRetVal, 32, eCmd);


	return pRetVal;
}

Packet *GetPacketToSendThroughTransactionServerToOtherShard(LocalTransactionManager *pManager, PacketTracker *pTracker,
	const char *pDestShardName, GlobalType eDestType, ContainerID iDestID, enumTransPacketCommand eCmd, char *pTransName, TransServerPacketFailureCB *pFailureCB, void *pFailureUserData1, void *pFailureUserData2)
{
	Packet *pRetVal = CreateLTMPacket(pManager, TRANSCLIENT_SEND_PACKET_SIMPLE_OTHER_SHARD, pTracker ? pTracker : PacketTrackerFind("RemoteCommand_Uncached", 0, "pTransName"));


	pktSendString(pRetVal, pTransName);

	pktSendString(pRetVal, pDestShardName);

	PutContainerTypeIntoPacket(pRetVal, eDestType);
	PutContainerIDIntoPacket(pRetVal, iDestID);

	if (pFailureCB)
	{
		//because trans server might have different bit size, just do everything as U64
		pktSendBits64(pRetVal, 64, (U64)pFailureCB);
		pktSendBits64(pRetVal, 64, (U64)pFailureUserData1);
		pktSendBits64(pRetVal, 64, (U64)pFailureUserData2);
	}
	else
	{
		pktSendBits64(pRetVal, 64, 0);
	}

	pktSendBits(pRetVal, 32, eCmd);

	return pRetVal;
}

Packet *GetPacketToSendThroughTransactionServer_MultipleRecipients(LocalTransactionManager *pManager, PacketTracker *pTracker, ContainerRef ***pppRecipients, 
		enumTransPacketCommand eCmd, char *pTransName)
{
	if (eaSize(pppRecipients))
	{
		Packet *pRetVal = CreateLTMPacket(pManager, TRANSCLIENT_SEND_PACKET_SIMPLE, pTracker ? pTracker : PacketTrackerFind("RemoteCommand_Uncached", 0, "pTransName"));
		int i;

		pktSendString(pRetVal, pTransName);

		for (i=0; i < eaSize(pppRecipients); i++)
		{
			PutContainerTypeIntoPacket(pRetVal, (*pppRecipients)[i]->containerType);
			PutContainerIDIntoPacket(pRetVal, (*pppRecipients)[i]->containerID);
		}
	
		PutContainerTypeIntoPacket(pRetVal, 0);
		pktSendBits64(pRetVal, 64, 0);


		pktSendBits(pRetVal, 32, eCmd);



		return pRetVal;
	}

	return NULL;
}



void SendPacketThroughTransactionServer(LocalTransactionManager *pManager, Packet **ppPak)
{
	pktSend(ppPak);
}


typedef struct CachedContainerLocationRequest
{
	int iRequestID;
	TransServerContainerLocRequestCB *pCB;
	void *pUserData;
} CachedContainerLocationRequest;


static CachedContainerLocationRequest **sppContainerLocRequests = NULL;

void RequestTransactionServerContainerLocation(LocalTransactionManager *pManager, TransServerContainerLocRequestCB *pCB, void *pUserData, 
	GlobalType eContainerType, ContainerID iContainerID)
{
	CachedContainerLocationRequest *pRequest = malloc(sizeof(CachedContainerLocationRequest));
	static int iNextRequestID = 1;
	Packet *pPak;

	CreateLTMPacketWithFunctionNameTracker(pPak, pManager, TRANSCLIENT_DBG_REQUESTCONTAINEROWNER);
	pRequest->pCB = pCB;
	pRequest->pUserData = pUserData;
	pRequest->iRequestID = iNextRequestID++;
	eaPush(&sppContainerLocRequests, pRequest);

	pktSendBits(pPak, 32, pRequest->iRequestID);
	PutContainerTypeIntoPacket(pPak, eContainerType);
	PutContainerIDIntoPacket(pPak, iContainerID);
	pktSend(&pPak);
}

void ReportLaggedTransactions(LocalTransactionManager *pLTM, const char *pTransName, int iRecentCount, int iRecentTime)
{
	Packet *pPak;

	CreateLTMPacketWithFunctionNameTracker(pPak, pLTM, TRANSCLIENT_DBG_LAGGEDTRANSACTION);
	pktSendString(pPak, pTransName);
	pktSendBits(pPak, 32, iRecentCount);
	pktSendBits(pPak, 32, iRecentTime);
	pktSend(&pPak);
}

void ReportTransactionWithSlowCallback(LocalTransactionManager *pLTM, const char *pTransName)
{
	if(!pLTM->bIsFullyLocal)
	{
		Packet *pPak;
		CreateLTMPacketWithFunctionNameTracker(pPak, pLTM, TRANSCLIENT_REGISTER_SLOW_TRANSCALLBACK_WITH_TRACKER);
		pktSendString(pPak, pTransName);

		pktSend(&pPak);
	}
}




void HandleContainerOwner(LocalTransactionManager *pManager, Packet *pPak)
{
	int iRequestID = pktGetBits(pPak, 32);
	GlobalType eContainerType = GetContainerTypeFromPacket(pPak);
	ContainerID iContainerID = GetContainerIDFromPacket(pPak);
	GlobalType eOwnerType = GetContainerTypeFromPacket(pPak);
	ContainerID iOwnerID = GetContainerIDFromPacket(pPak);
	bool bDefault = pktGetBits(pPak, 32);
	int i;

	for (i =0; i < eaSize(&sppContainerLocRequests); i++)
	{
		if (sppContainerLocRequests[i]->iRequestID == iRequestID)
		{
			sppContainerLocRequests[i]->pCB(eContainerType, iContainerID, eOwnerType, iOwnerID, bDefault, sppContainerLocRequests[i]->pUserData);
			free(sppContainerLocRequests[i]);
			eaRemoveFast(&sppContainerLocRequests, i);
			return;
		}
	}
}





#include "autogen/transactionsystem_h_ast.c"
