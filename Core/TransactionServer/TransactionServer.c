#include "transactionserver.h"




#include "logging.h"
#include <stdio.h>
#include <conio.h>

#include "file.h"

#include <math.h>

#include "ServerLib.h"

#include "MultiplexedNetLinkList.h"
#include "objTransactions.h"
#include "structnet.h"
#include "../../libs/ServerLib/AutoGen/TransactionSystem_h_ast.h"
#include "autogen/transactionserver_h_ast.c"
#include "svrGlobalInfo.h"
#include "autogen/svrGlobalInfo_h_ast.h"
#include "autogen/TransactionOutcomes_h_ast.h"
#include "ControllerLink.h"
#include "utilitieslib.h"
#include "alerts.h"
#include "sock.h"
#include "StringCache.h"
#include "ServerLib_h_ast.h"
#include "TransactionServerUtilities.h"
#include "TransactionServerUtilities_h_ast.h"
#include "GlobalTypes.h"
#include "MemoryPool.h"
#include "resourceInfo.h"
#include "TransactionServer_c_ast.h"
#include "rand.h"
#include "..\multiplexer\multiplexer.h"
#include "sysutil.h"
#include "timing_profiler_interface.h"
#include "winutil.h"
#include "netreplay.h"
#include "TransactionServer_ShardCluster.h"


ContainerID FindRandomServerIDOfServerType(TransactionServer *pServer, GlobalType eType);
/*
#define gMaxTransactions 1048576

#define gMaxLogicalConnections 40000

#define gMaxMultiplexConnections 512

#define gMaxTransactions 512

#define gMaxLogicalConnections 20

#define gMaxMultiplexConnections 8
*/

int gMaxTransactions = 2048 * 8;
int gMaxLogicalConnections = 100;
int gMaxMultiplexConnections = 8;

int gbLogAllTransactions = 0;
AUTO_CMD_INT(gbLogAllTransactions, LogAllTransactions) ACMD_COMMANDLINE;

void OutputVerboseLogging(Transaction *pTrans, char *pDesc, U64 iCurTime, bool bComplete);

MP_DEFINE(TransVerboseLogEntry);

//this slows things down a TON be careful
bool gbVerboseLogEverything = false;
AUTO_CMD_INT(gbVerboseLogEverything, VerboseLogEverything);

//milliseconds
static int siVerboseTransMinLogCutoff = 20000;

//seconds
static int siVerboseIncompleteLogCutoff = 30;

//number of milliseconds. If verbose logged transactions complete faster than this, don't bother logging them, we dont' care
AUTO_CMD_INT(siVerboseTransMinLogCutoff, VerboseTransMinLogCutoff) ACMD_CATEGORY(DEBUG);

//number of seconds. If a verbose log trans has existed this long without completing, log it anyhow
AUTO_CMD_INT(siVerboseIncompleteLogCutoff, VerboseIncompleteLogCutoff) ACMD_CATEGORY(DEBUG);

//during killing of old transactions, we temporarily disable verbose logging to avoid massive stalls
bool gbNoVerboseLogging = false;

extern U32 giLagOnTransact;

//number of seconds. If a verbose log trans has existed this long without completing, log it anyhow
static char sObjectDBReplayFromFile[MAX_PATH] = "";
AUTO_CMD_STRING(sObjectDBReplayFromFile, ObjectDBReplayFromFile) ACMD_CMDLINE ACMD_CATEGORY(DEBUG);

//all transactions with this name will have verbose logging done. empty string to reset
AUTO_COMMAND ACMD_CATEGORY(debug);
void BeginVerboseLogging(const char *pName)
{
	TransactionTracker *pTracker = FindTransactionTracker(&gTransactionServer, allocAddString(pName), false);

	pTracker->bDoVerboseLogging = true;
}

AUTO_COMMAND ACMD_CATEGORY(debug);
void EndVerboseLogging(const char *pName)
{
	TransactionTracker *pTracker = FindTransactionTracker(&gTransactionServer, allocAddString(pName), false);

	pTracker->bDoVerboseLogging = false;
}

AUTO_COMMAND ACMD_CATEGORY(debug);
void ClearVerboseLogs(const char *pName)
{
	TransactionTracker *pTracker = FindTransactionTracker(&gTransactionServer, allocAddString(pName), false);
	estrDestroy(&pTracker->pVerboseLog_Complete);
	estrDestroy(&pTracker->pVerboseLog_Incomplete);
}


#define TRANS_VERBOSE_LOG(trans, eType, pStr, eCtrType, iCtrID, iOtherID) { if (trans->ppVerboseLogs || gbVerboseLogEverything) \
	DoVerboseTransLogging(trans, eType, pStr, eCtrType, iCtrID, iOtherID); }

#define TRANS_VERBOSE_LOG_W_INDEX(trans, eType, pStr, iConnectionIndex, iOtherID) { if (trans->ppVerboseLogs || gbVerboseLogEverything) \
{ LogicalConnection *pConnection = &pServer->pConnections[iConnectionIndex]; DoVerboseTransLogging(trans, eType, pStr, pConnection->eServerType, pConnection->iServerID, iOtherID); }}


void FindNextTransactionID(U32 *piCurID)
{
	*piCurID += gMaxTransactions;
	if ((*piCurID) & TRANSACTIONID_SPECIALBIT_LOCALTRANSACTION)
	{
		(*piCurID) &= ~TRANSACTIONID_SPECIALBIT_LOCALTRANSACTION;

		if ((*piCurID) == 0)
		{
			(*piCurID) += gMaxTransactions;
		}
	}
}

//doesn't check whether it's pointing to anything real or not, just whether it's pointing
static __forceinline bool ConnectionHandle_IsSet(LogicalConnectionHandle *pHandle)
{
	return (pHandle->iIndex != -1);
}

//returns true if the handle is set, and then returns the index, or -1 if the index is out of date
static __forceinline bool ConnectionHandle_CheckIfSetAndReturnIndex(TransactionServer *pServer, LogicalConnectionHandle *pHandle, int *pOutIndex)
{
	if (pHandle->iIndex == -1)
	{
		return false;
	}

	if (pServer->pConnections[pHandle->iIndex].iConnectionID != pHandle->iID)
	{
		*pOutIndex = -1;
		return true;
	}

	*pOutIndex = pHandle->iIndex;
	return true;

}

static __forceinline void ConnectionHandle_SetFromIndex(TransactionServer *pServer, LogicalConnectionHandle *pHandle, int iConnectionIndex)
{
	pHandle->iIndex = iConnectionIndex;
	pHandle->iID = pServer->pConnections[iConnectionIndex].iConnectionID;
}

static __forceinline void ConnectionHandle_Clear(LogicalConnectionHandle *pHandle)
{
	pHandle->iIndex = -1;
}

static __forceinline bool ConnectionHandle_MatchesIndex(TransactionServer *pServer, LogicalConnectionHandle *pHandle, int iConnectionIndex)
{
	if (pHandle->iIndex != iConnectionIndex)
	{
		return false;
	}

	if (pServer->pConnections[iConnectionIndex].iConnectionID != pHandle->iID)
	{
		return false;
	}

	return true;
}

char *ConnectionHandle_GetDescription(TransactionServer *pServer, LogicalConnectionHandle *pHandle)
{
	static char retString[2048];
	int iIndex;

	if (ConnectionHandle_CheckIfSetAndReturnIndex(pServer, pHandle, &iIndex))
	{
		LogicalConnection *pConnection = &pServer->pConnections[pHandle->iIndex];

		sprintf(retString, "(Active connection to %s)", GlobalTypeAndIDToString(pConnection->eServerType, pConnection->iServerID));
	}
	else
	{
		sprintf(retString, "(No-longer valid logical connection ID %d index %d)", pHandle->iID, pHandle->iIndex);
	}

	return retString;
}

static __forceinline void LogicalConnection_AddTransaction(LogicalConnection *pConnection, TransactionID iTransID)
{
	stashIntAddPointer(pConnection->transTable, iTransID, NULL, false);
}

static __forceinline void LogicalConneciton_RemoveTransaction(LogicalConnection *pConnection, TransactionID iTransID)
{
	stashIntRemovePointer(pConnection->transTable, iTransID, NULL);
}

void TransClearConnectionHandle(TransactionServer *pServer, Transaction *pTransaction, int iIndex)
{
	int i;
	bool bFound = false;

	int iMyConnectionIndex;

	if (!ConnectionHandle_CheckIfSetAndReturnIndex(pServer, &pTransaction->pBaseTransactions[iIndex].transConnectionHandle, &iMyConnectionIndex))
	{
		return;
	}

	if (iMyConnectionIndex == -1)
	{
		ConnectionHandle_Clear(&pTransaction->pBaseTransactions[iIndex].transConnectionHandle);
		return;
	}

	//a linear search is fine here because the number of base transactions is never very high
	//if we find another handle pointing to the same thing, then we don't remove ourself from
	//that connection's list
	for (i = 0; i < pTransaction->iNumBaseTransactions; i++)
	{
		if (i != iIndex)
		{
			if (ConnectionHandle_MatchesIndex(pServer, &pTransaction->pBaseTransactions[i].transConnectionHandle,
				iMyConnectionIndex))
			{
				bFound = true;
				break;
			}
		}
	}

	if (!bFound)
	{
		LogicalConneciton_RemoveTransaction(&pServer->pConnections[iMyConnectionIndex], pTransaction->iID);
	}

	ConnectionHandle_Clear(&pTransaction->pBaseTransactions[iIndex].transConnectionHandle);
}


#define TransactionAssert(bCondition, pTransaction, pString, ...) { int bCond = (bCondition); (((bCond) || (TransactionAssert_Internal(pServer, pTransaction, __FILE__, __LINE__, pString, __VA_ARGS__))),  __assume(bCondition), 0); if (!bCond) return; }
#define TransactionAssertReturnNULL(bCondition, pTransaction, pString, ...) { int bCond = (bCondition); (((bCond) || (TransactionAssert_Internal(pServer, pTransaction, __FILE__, __LINE__, pString, __VA_ARGS__))),  __assume(bCondition), 0); if (!bCond) return NULL; }
#define TransactionAssertReturnFALSE(bCondition, pTransaction, pString, ...) { int bCond = (bCondition); (((bCond) || (TransactionAssert_Internal(pServer, pTransaction, __FILE__, __LINE__, pString, __VA_ARGS__))),  __assume(bCondition), 0); if (!bCond) return FALSE; }


void DumpBaseTransactionInfoIntoEString(BaseTransactionInfo *pBase, char **ppEString)
{
	char *pShortData = NULL;

	estrStackCreate(&pShortData);

	estrConcatf(ppEString, "Recipient: %s. Outcome: %s\n",
		GlobalTypeAndIDToString(pBase->transaction.recipient.containerType, pBase->transaction.recipient.containerID),
		StaticDefineIntRevLookup(enumTransactionOutcomeEnum, pBase->eOutcome));
	
	estrCopy2(&pShortData, pBase->transaction.pData);
	if (estrLength(&pShortData) > 1000)
	{
		estrSetSize(&pShortData, 1000);
		estrConcatf(&pShortData, "((Too long, truncating))");
	}

	estrConcatf(ppEString, "Data: %s\n", pShortData);
	estrDestroy(&pShortData);

	estrConcatf(ppEString, "State: %s. Requested var names: %s\n",
		StaticDefineIntRevLookup(enumBaseTransactionStateEnum, pBase->eState),
		pBase->transaction.pRequestedTransVariableNames);
	estrConcatf(ppEString, "Connection ID %d index %d. LTM Callback ID %d\n",
		pBase->transConnectionHandle.iID, pBase->transConnectionHandle.iIndex, pBase->iLocalTransactionManagerCallbackID);
	estrConcatf(ppEString, "Return string: %s\n", pBase->returnString);
	estrConcatf(ppEString, "db update data, %d bytes of data, string: %s",
		pBase->databaseUpdateData.iDataSize, pBase->databaseUpdateData.pString1);
	if (pBase->databaseUpdateData.pString2)
	{
		estrConcatf(ppEString, " string2: %s", pBase->databaseUpdateData.pString2);
	}
	estrConcatf(ppEString, "\ntrans server update string: %s\n", pBase->transServerUpdateString);


}

void DumpTransactionIntoEString(TransactionServer *pServer, Transaction *pTransaction, char **ppEString)
{
	int i;

	estrConcatf(ppEString, "Name: %s ID: %u eType: %s iNumBaseTransactions: %d curSeqIndex: %d\n",
		pTransaction->pTransactionName,
		pTransaction->iID, StaticDefineIntRevLookup(enumTransactionTypeEnum, pTransaction->eType), pTransaction->iNumBaseTransactions,
		pTransaction->iCurSequentialIndex);
	
	for (i=0; i < pTransaction->iNumBaseTransactions; i++)
	{
		estrConcatf(ppEString, "-------  Base Trans %d ------\n", i);
		DumpBaseTransactionInfoIntoEString(&pTransaction->pBaseTransactions[i], ppEString);
	}

	estrConcatf(ppEString, "----------------------------------\n");
	estrConcatf(ppEString, "UnblockCount %d. BlockedStatus %s. WhoBlockedme %u. TimeBegan %"FORM_LL"d.\n",
		pTransaction->iUnblockCount, StaticDefineIntRevLookup(enumBlockedStatusEnum, pTransaction->eBlockedStatus),
		pTransaction->iWhoBlockedMe, pTransaction->iTimeBegan);

	estrConcatf(ppEString, "Respone handle: logical connection %s, ID %d\n",
		ConnectionHandle_GetDescription(pServer, &pTransaction->responseHandle.connectionHandle), 
		pTransaction->responseHandle.iReturnValID);

	estrConcatf(ppEString, "TransVariables:\n");
	DumpNameTableIntoEString(pTransaction->transVariableTable, ppEString);





}

bool TransactionAssert_Internal(TransactionServer *pServer, Transaction *pTransaction, const char *pFile, int iLine, FORMAT_STR const char *pString, ...)
{
	char *pErrorString = NULL;

	VA_START(args, pString);
	estrPrintf(&pErrorString, "Transaction Server asserting on %s line %d with message ", pFile, iLine);


	estrConcatfv(&pErrorString, pString, args);
	VA_END();

	estrConcatf(&pErrorString, "\nTransaction:\n");

	DumpTransactionIntoEString(pServer, pTransaction, &pErrorString);

	log_printf(LOG_TRANSSERVER, "%s", pErrorString);

	TriggerAlert("TRANS_ASSERT", pErrorString, ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, 0, 0, 0, 0, NULL, 0);

	estrDestroy(&pErrorString);

	pTransaction->eType = TRANS_TYPE_CORRUPT;
	return true;
}


//enough transactions for a big production-y shard (uses LOTS more memory)
AUTO_COMMAND ACMD_CMDLINE;
void SetBigMode(int bBigMode)
{
	if (bBigMode)
	{
		gMaxTransactions = 1048576;
		gMaxLogicalConnections = 40000;
		gMaxMultiplexConnections = 512;
	}
}

#if 0
void CheckCounts(TransactionServer *pServer)
{
/*	int iNumUnblocked = 0;
	int iNumBlockedByNothing = 0;
	int iNumBlockedByOthers = 0;
	int iTotalNumBlocked = 0;
	Transaction *pNext;

	int i;

	for (i=0; i < gMaxTransactions; i++)
	{
		Transaction *pTransaction = &pServer->pTransactions[i];
		if ( pTransaction->eType != TRANS_TYPE_NONE)
		{
			if (pTransaction->eBlockedStatus)
			{
				iTotalNumBlocked++;
			}
			else
			{
				pNext = pTransaction->pNextBlocked;

				iNumUnblocked++;

				while (pNext)
				{
					assert(pNext->eBlockedStatus == BLOCKED_BY_SOMETHING);
					iNumBlockedByOthers++;
					pNext = pNext->pNextBlocked;
				}
			}
		}
	}

	pNext = pServer->pFirstTransactionBlockedByNothing;

	while (pNext)
	{
		assert(pNext->eBlockedStatus == BLOCKED_BY_NOTHING);
		iNumBlockedByNothing++;
		pNext = pNext->pNextBlocked;
	}

//	printf("%d unblocked, %d blocked by others, %d blocked by nothing, %d total blocked, %d active\n",
//		iNumUnblocked, iNumBlockedByOthers, iNumBlockedByNothing, iTotalNumBlocked, pServer->iNumActiveTransactions);

	assert(iNumBlockedByOthers + iNumBlockedByNothing == iTotalNumBlocked);
	assert(iNumUnblocked + iTotalNumBlocked == pServer->iNumActiveTransactions);*/
}
#endif

#define CheckCounts(x)


AUTO_RUN_FIRST;
void SetUpMyType(void)
{
	SetAppGlobalType(GLOBALTYPE_TRANSACTIONSERVER);
}

int performanceStats;
FILE *perfStatFile;
int perfTimer;
int perfMainTicks;

//prints out performance stats
AUTO_CMD_INT(performanceStats,performanceStats) ACMD_CMDLINE;


//when allocating index arrays for multiplexed connections, allocate this many extra at once to avoid having to reallocate every time a new connection comes in
#define NUM_EXTRA_MULTIPLEX_INDICES_TO_ALLOCATE_AT_ONCE 32


void GenerateTransactionResponse(TransactionServer *pServer, Transaction *pTransaction, TransactionResponseHandle *pHandle, enumTransactionOutcome eOutcome);
void BeginCancellingSequentialAtomicTrans(TransactionServer *pServer, Transaction *pTransaction);

void ProcessTransactionServerCommandString(TransactionServer *pServer, char *pString, char *pComment);

void SendSimpleDBUpdateString(TransactionServer *pServer, Transaction *pTransaction, char *pDatabaseUpdateString, char *pComment);

bool TransactionIsAtomic(Transaction *pTransaction)
{
	return (pTransaction->eType == TRANS_TYPE_SEQUENTIAL_ATOMIC) || (pTransaction->eType == TRANS_TYPE_SIMULTANEOUS_ATOMIC);
}

void GenerateObjectNotFoundError(char **ppEString,
	Transaction *pTransaction, int iBaseTransIndex)
{
	estrConcatf(ppEString, "Trans Server couldn't find object of type %s (%d) with ID %u",
		GlobalTypeToName(pTransaction->pBaseTransactions[iBaseTransIndex].transaction.recipient.containerType),
		pTransaction->pBaseTransactions[iBaseTransIndex].transaction.recipient.containerType,
		pTransaction->pBaseTransactions[iBaseTransIndex].transaction.recipient.containerID);
}

//-1 on failure
int GetConnectionIndexFromRecipient(TransactionServer *pServer, const char *pTransactionName, ContainerRef *pRecipient)
{
	int iConnectionIndex;

	if(!(pRecipient->containerType > GLOBALTYPE_NONE && pRecipient->containerType < GLOBALTYPE_MAXTYPES))
	{
		return -1;
	}

	//if the recipient ID is 0, that means we want to find the unique object of that type, and change the
	//recipient ID to that ID, and return that connection
	if (pRecipient->containerID == 0)
	{
		StashTableIterator iter;
		int iIndex = -1;
		StashElement element;

		stashGetIterator(pServer->objectDirectories[pRecipient->containerType].directory, &iter);

		if (!stashGetNextElement(&iter, &element))
		{
			//nothing found, return -1
			return -1;
		}
	
		iIndex = stashElementGetInt(element);
		pRecipient->containerID = stashElementGetIntKey(element);

		if (stashGetNextElement(&iter, &element))
		{
			Errorf("Transaction %s is trying to use transaction recipient ID 0 when more than one objects of type %s exist",
				pTransactionName, GlobalTypeToName(pRecipient->containerType));
			return -1;
		}

		return iIndex;
	}

	if (stashIntFindInt(pServer->objectDirectories[pRecipient->containerType].directory, pRecipient->containerID, &iConnectionIndex))
	{
		if (pServer->pConnections[iConnectionIndex % gMaxLogicalConnections].iConnectionID != 0)
		{
			return iConnectionIndex;
		}
	}

	// the default is -1, so if there's no default, we return -1
	return (pServer->objectDirectories[pRecipient->containerType].iDefaultConnectionIndex);
}

int GetConnectionIndexFromRecipient_SupportingRandom(TransactionServer *pServer, const char *pTransactionName, ContainerRef *pRecipient)
{

	if (pRecipient->containerID == SPECIAL_CONTAINERID_RANDOM)
	{
		ContainerRef recipientCopy = *pRecipient;

		recipientCopy.containerID = FindRandomServerIDOfServerType(pServer, recipientCopy.containerType);

		if (!recipientCopy.containerID)
		{
			return -1;
		}

		return GetConnectionIndexFromRecipient(pServer, pTransactionName, &recipientCopy);
	}

	return GetConnectionIndexFromRecipient(pServer, pTransactionName, pRecipient);
}

//-1 on failure
int GetConnectionFromResponseHandle(TransactionServer *pServer, TransactionResponseHandle *pResponseHandle)
{
	int iRetVal = -1;

	ConnectionHandle_CheckIfSetAndReturnIndex(pServer, &pResponseHandle->connectionHandle, &iRetVal);

	return iRetVal;
}

Packet *NewTransactionPacket(TransactionServer *pServer, int iLogicalConnectionIndex, int ePacketType)
{
	LogicalConnection *pConnection;
	static PacketTracker *pTracker;

	ONCE(pTracker = PacketTrackerFind("NewTransactionPacket", 0, NULL));

	assert(iLogicalConnectionIndex >= 0 && iLogicalConnectionIndex < gMaxLogicalConnections && pServer->pConnections[iLogicalConnectionIndex].iConnectionID != 0);
	
	pConnection = &pServer->pConnections[iLogicalConnectionIndex];
	
	if (pConnection->iMultiplexConnectionIndex == -1)
	{
		return pktCreateWithTracker(pConnection->pNetLink, ePacketType, pTracker);
	}
	else
	{
		return CreateMultiplexedNetLinkListPacket(pConnection->pNetLink, pConnection->iMultiplexConnectionIndex, ePacketType, pTracker);
	}
}


Transaction *GetTransactionFromIDIfExists(TransactionServer *pServer, TransactionID iID)
{
	Transaction *pTransaction = &pServer->pTransactions[iID % gMaxTransactions];

	if (pTransaction->iID != iID || pTransaction->eType == TRANS_TYPE_NONE || pTransaction->eType == TRANS_TYPE_CORRUPT)
	{
		return NULL;
	}

	return pTransaction;
}

void AddTransactionToBlockedByNothingList(TransactionServer *pServer, Transaction *pTransaction)
{
	TransactionAssert(pTransaction->eBlockedStatus == IS_NOT_BLOCKED, pTransaction, "Trying to block a transaction that is already blocked");
	pTransaction->eBlockedStatus = BLOCKED_BY_NOTHING;

	pTransaction->pNextBlocked = NULL;

	TransactionAssert(pTransaction->eType != TRANS_TYPE_NONE, pTransaction, "Trying to block a non-existent transaction");


	if (pServer->pLastTransactionBlockedByNothing)
	{
		pServer->pLastTransactionBlockedByNothing->pNextBlocked = pTransaction;
		pServer->pLastTransactionBlockedByNothing = pTransaction;
	}
	else
	{
		pServer->pFirstTransactionBlockedByNothing = pServer->pLastTransactionBlockedByNothing = pTransaction;
	}

	CheckCounts(pServer);

	TRANS_VERBOSE_LOG(pTransaction, TVL_NOWBLOCKEDBYNOTHING, NULL, 0, 0, 0);

}

//this function needs to reverse the order of the list because the blocked-by-nothing list is FIFO and the 
//lists attached to each transaction are LIFO
void AddAllMyBlockedTransactionsToBlockedByNothingList(TransactionServer *pServer, Transaction *pTransaction)
{
	if (pTransaction->pNextBlocked)
	{
		Transaction *pFirstReversed = pTransaction->pNextBlocked;
		Transaction *pLastReversed = pFirstReversed;
		Transaction *pCur = pFirstReversed->pNextBlocked;
		pFirstReversed->pNextBlocked = NULL;

		TransactionAssert(pFirstReversed->eBlockedStatus == BLOCKED_BY_SOMETHING, pTransaction, "Blocked transaction has wrong status");
		pFirstReversed->eBlockedStatus = BLOCKED_BY_NOTHING;

		while (pCur)
		{
			Transaction *pNext = pCur->pNextBlocked;

			TransactionAssert(pCur->eBlockedStatus == BLOCKED_BY_SOMETHING, pTransaction, "Blocked transaction has wrong status");
			pCur->eBlockedStatus = BLOCKED_BY_NOTHING;

			pCur->pNextBlocked  = pFirstReversed;
			pFirstReversed = pCur;
			pCur = pNext;
		}

		if (pServer->pLastTransactionBlockedByNothing)
		{
			pServer->pLastTransactionBlockedByNothing->pNextBlocked = pFirstReversed;
			pServer->pLastTransactionBlockedByNothing = pLastReversed;
		}
		else
		{
			pServer->pFirstTransactionBlockedByNothing = pFirstReversed;
			pServer->pLastTransactionBlockedByNothing = pLastReversed;
		}
	}

	pTransaction->pNextBlocked = NULL;

	CheckCounts(pServer);
}



	


void CleanupTransaction(TransactionServer *pServer, Transaction *pTransaction)
{
	U64 iTransactionTime = timeMsecsSince2000() - pTransaction->iTimeBegan;

	TRANS_VERBOSE_LOG(pTransaction, TVL_CLEANUP, NULL, GLOBALTYPE_NONE, 0, 0);


	ReportCleanupToTracker(pTransaction->pTracker, iTransactionTime);
	IntAverager_AddDatapoint(pServer->pCompletionTimeAverager, iTransactionTime);

	AddAllMyBlockedTransactionsToBlockedByNothingList(pServer, pTransaction);


	if (pTransaction->pBaseTransactions)
	{
		int i;

		for (i=0; i < pTransaction->iNumBaseTransactions; i++)
		{
			TransClearConnectionHandle(pServer, pTransaction, i);

			estrDestroy(&pTransaction->pBaseTransactions[i].returnString);
			TransDataBlockClear(&pTransaction->pBaseTransactions[i].databaseUpdateData);
			estrDestroy(&pTransaction->pBaseTransactions[i].transServerUpdateString);
			estrDestroy(&pTransaction->pBaseTransactions[i].transaction.pRequestedTransVariableNames);
		}

		assert(pServer->iNumAllocations);
		pServer->iNumAllocations--;
		free(pTransaction->pBaseTransactions);
		pTransaction->pBaseTransactions = NULL;
	}

	
	pTransaction->eType = TRANS_TYPE_NONE;


	if (pTransaction->transVariableTable)
	{
		DestroyNameTable(pTransaction->transVariableTable);
	}

	TransactionAssert(pTransaction->eBlockedStatus == IS_NOT_BLOCKED, pTransaction, "A blocked transaction is having CleanupTransaction called on it... how is this possible?");

	
	pServer->iNumActiveTransactions--;
	pTransaction->pNextBlocked = NULL;

	if (!pServer->pNextEmptyTransaction)
	{
		pServer->pNextEmptyTransaction = pServer->pLastEmptyTransaction = pTransaction;
	}
	else
	{
		pServer->pLastEmptyTransaction->pNextBlocked = pTransaction;
		pServer->pLastEmptyTransaction = pTransaction;
	}

	CheckCounts(pServer);

}

Transaction *GetNextBlockedByNothingTransaction(TransactionServer *pServer)
{
	Transaction *pRetVal = NULL;

	if (pServer->pFirstTransactionBlockedByNothing)
	{
		pRetVal = pServer->pFirstTransactionBlockedByNothing;
		TransactionAssertReturnNULL(pRetVal->eBlockedStatus == BLOCKED_BY_NOTHING, pRetVal, "A transaction in the blocked-by-nothing list hast the wrong status");

		pServer->pFirstTransactionBlockedByNothing = pRetVal->pNextBlocked;
		if (!pServer->pFirstTransactionBlockedByNothing)
		{
			pServer->pLastTransactionBlockedByNothing = NULL;
		}

		pRetVal->pNextBlocked = NULL;
		pRetVal->eBlockedStatus = IS_NOT_BLOCKED;


		//before starting to try this transaction again, udpate its ID so that it's a "new" transaction in all ways
		FindNextTransactionID(&pRetVal->iID);
	}


	CheckCounts(pServer);

	

	return pRetVal;
}

void AddBlockedTransaction(TransactionServer *pServer, Transaction *pTransaction)
{
	//first, move anything blocked by this transaction into the blocked-by-nothing list	
	TransactionAssert(pTransaction->eBlockedStatus == IS_NOT_BLOCKED, pTransaction, "An already blocked transaction is having AddBlockedTransaction called on it... how is this possible?");
	AddAllMyBlockedTransactionsToBlockedByNothingList(pServer, pTransaction);

	CountAverager_ItHappened(pServer->pBlockedTransactionCounter);

	if (pTransaction->iWhoBlockedMe)
	{
		Transaction *pWhoBlockedIt = GetTransactionFromIDIfExists(pServer, pTransaction->iWhoBlockedMe);
		pTransaction->iWhoBlockedMe = 0;

		if (pWhoBlockedIt && (pWhoBlockedIt->eBlockedStatus == IS_NOT_BLOCKED))
		{
			pTransaction->eBlockedStatus = BLOCKED_BY_SOMETHING;

			//printf("adding trans %d to blocked list of %d\n", pTransaction->iID, pWhoBlockedIt->iID);

			pTransaction->pNextBlocked = pWhoBlockedIt->pNextBlocked;
			pWhoBlockedIt->pNextBlocked = pTransaction;

			CheckCounts(pServer);

			TRANS_VERBOSE_LOG(pTransaction, TVL_BLOCKEDBYSOMEONE, pWhoBlockedIt->pTransactionName, 0, 0, pWhoBlockedIt->iID);
			return;
		}
	}

	AddTransactionToBlockedByNothingList(pServer, pTransaction);

	CheckCounts(pServer);
}



		


Transaction *GetEmptyTransaction(TransactionServer *pServer)
{
	Transaction *pRetVal;
	U32 iNewID;

	if (!((pRetVal = pServer->pNextEmptyTransaction)))
	{
		return NULL;
	}

	if (pServer->pNextEmptyTransaction == pServer->pLastEmptyTransaction)
	{
		pServer->pNextEmptyTransaction = NULL;
		pServer->pLastEmptyTransaction = NULL;
	}
	else
	{
		pServer->pNextEmptyTransaction = pServer->pNextEmptyTransaction->pNextBlocked;
	}



	iNewID = pRetVal->iID + gMaxTransactions;

	FindNextTransactionID(&iNewID);

	memset(pRetVal, 0, sizeof(Transaction));
	
	pRetVal->iID = iNewID;

	pServer->iNumActiveTransactions++;

	pRetVal->iTimeBegan = timeMsecsSince2000();

	return pRetVal;
}

bool TransactionWantsReturnValue(Transaction *pTransaction)
{
	return pTransaction->responseHandle.iReturnValID != 0;
}

FILE *trDebugLogFile;

bool SendNewTransactionMessage(TransactionServer *pServer, Transaction *pTransaction, int iTransIndex, bool bRequiresConfirm, bool bSucceedAndConfirmIsOK)
{
	ContainerRef *pRecipient = &pTransaction->pBaseTransactions[iTransIndex].transaction.recipient;
	char *pTransactionData = pTransaction->pBaseTransactions[iTransIndex].transaction.pData;
	Packet *pPacket;
	int iConnectionIndex;

	PERFINFO_AUTO_START_FUNC();

	TransactionAssertReturnFALSE(!ConnectionHandle_IsSet(&pTransaction->pBaseTransactions[iTransIndex].transConnectionHandle), 
		pTransaction, "Trying to send a new transaction message, but there's already a connection");

	iConnectionIndex = GetConnectionIndexFromRecipient(pServer, pTransaction->pTransactionName, pRecipient);
	if (iConnectionIndex == -1)
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	//	filelog_printf("transactions.log","SENDTR%d %s[%d]: %s\n",pTransaction->iID,GlobalTypeToName(pTransaction->pBaseTransactions[0].transaction.recipient.containerType),pTransaction->pBaseTransactions[0].transaction.recipient.containerID,pTransaction->pBaseTransactions[0].transaction.pData);

	pPacket = NewTransactionPacket(pServer, iConnectionIndex, TRANSSERVER_REQUEST_SINGLE_TRANSACTION);

	pktSendBits(pPacket, 1, TransactionWantsReturnValue(pTransaction));
	pktSendBits(pPacket, 1, bRequiresConfirm);
	pktSendBits(pPacket, 1, bSucceedAndConfirmIsOK);


	PutTransactionIDIntoPacket(pPacket, pTransaction->iID);
	pktSendString(pPacket, pTransaction->pTransactionName);
	pktSendBitsPack(pPacket, 1, iTransIndex);
	PutContainerTypeIntoPacket(pPacket, pRecipient->containerType);
	PutContainerIDIntoPacket(pPacket, pRecipient->containerID);
	
	pktSendString(pPacket, pTransactionData);


	if (pTransaction->pBaseTransactions[iTransIndex].transaction.pRequestedTransVariableNames)
	{

		pktSendBits(pPacket, 1, 1);
		NameTablePutIntoPacket(pTransaction->transVariableTable, pPacket, pTransaction->pBaseTransactions[iTransIndex].transaction.pRequestedTransVariableNames);
	}
	else
	{
		pktSendBits(pPacket, 1, 0);
	}

	pktSend(&pPacket);

	ConnectionHandle_SetFromIndex(pServer, &pTransaction->pBaseTransactions[iTransIndex].transConnectionHandle, iConnectionIndex);
	LogicalConnection_AddTransaction(&pServer->pConnections[iConnectionIndex], pTransaction->iID);


	TRANS_VERBOSE_LOG_W_INDEX(pTransaction, TVL_BASETRANS_BEGAN, pTransactionData, iConnectionIndex, iTransIndex);

	PERFINFO_AUTO_STOP();

	return true;
}






void ChangeBaseTransState(TransactionServer *pServer, Transaction *pTransaction, int iIndex, enumBaseTransactionState eNewState)
{
	enumBaseTransactionState eOldState;

	TransactionAssert(iIndex >= 0 && iIndex < pTransaction->iNumBaseTransactions, pTransaction, "Invalid base trans index while changing state");

	TransactionAssert((int)eNewState >= 0 && (int)eNewState < BASETRANS_STATE_COUNT, pTransaction, "Trying to change base trans state to invalid state");

	eOldState = pTransaction->pBaseTransactions[iIndex].eState;

	TransactionAssert(pTransaction->iBaseTransactionsPerState[eOldState] > 0, pTransaction, "BaseTransactionPerState corruption");
	pTransaction->iBaseTransactionsPerState[eOldState]--;
	pTransaction->iBaseTransactionsPerState[eNewState]++;

	pTransaction->pBaseTransactions[iIndex].eState = eNewState;
}

void SendNextSequentialTransactionMessage(TransactionServer *pServer, Transaction *pTransaction)
{
	while (pTransaction->iCurSequentialIndex < pTransaction->iNumBaseTransactions)
	{
		TransactionAssert(pTransaction->pBaseTransactions[pTransaction->iCurSequentialIndex].eState == BASETRANS_STATE_STARTUP 
			|| pTransaction->pBaseTransactions[pTransaction->iCurSequentialIndex].eState == BASETRANS_STATE_BLOCKED,
			pTransaction, "Want to send new transaction message, but base trans %d is in the wrong state", pTransaction->iCurSequentialIndex);


		if (SendNewTransactionMessage(pServer, pTransaction, pTransaction->iCurSequentialIndex, 0, 0))
		{
			ChangeBaseTransState(pServer, pTransaction, pTransaction->iCurSequentialIndex, BASETRANS_STATE_INITQUERYSENT);
			return;
		}
		
		ChangeBaseTransState(pServer, pTransaction, pTransaction->iCurSequentialIndex, BASETRANS_STATE_FAILED);
		pTransaction->pBaseTransactions[pTransaction->iCurSequentialIndex].eOutcome = TRANSACTION_OUTCOME_FAILURE;

		GenerateObjectNotFoundError(&pTransaction->pBaseTransactions[pTransaction->iCurSequentialIndex].returnString,
			pTransaction, pTransaction->iCurSequentialIndex);

		pTransaction->iCurSequentialIndex++;
	} 

	GenerateTransactionResponse(pServer, pTransaction, &pTransaction->responseHandle, pTransaction->iBaseTransactionsPerState[BASETRANS_STATE_FAILED] ? TRANSACTION_OUTCOME_FAILURE : TRANSACTION_OUTCOME_SUCCESS );
	CleanupTransaction(pServer, pTransaction);
}

void SendNextSequentialStopOnFailTransactionMessage(TransactionServer *pServer, Transaction *pTransaction)
{
	if (pTransaction->iCurSequentialIndex < pTransaction->iNumBaseTransactions)
	{
		TransactionAssert(pTransaction->pBaseTransactions[pTransaction->iCurSequentialIndex].eState == BASETRANS_STATE_STARTUP 
			|| pTransaction->pBaseTransactions[pTransaction->iCurSequentialIndex].eState == BASETRANS_STATE_BLOCKED,
			pTransaction, "Want to send new transaction message, but base trans %d is in the wrong state", pTransaction->iCurSequentialIndex);


		if (SendNewTransactionMessage(pServer, pTransaction, pTransaction->iCurSequentialIndex, 0, 0))
		{
			ChangeBaseTransState(pServer, pTransaction, pTransaction->iCurSequentialIndex, BASETRANS_STATE_INITQUERYSENT);
			return;
		}

		ChangeBaseTransState(pServer, pTransaction, pTransaction->iCurSequentialIndex, BASETRANS_STATE_FAILED);
		pTransaction->pBaseTransactions[pTransaction->iCurSequentialIndex].eOutcome = TRANSACTION_OUTCOME_FAILURE;

		GenerateObjectNotFoundError(&pTransaction->pBaseTransactions[pTransaction->iCurSequentialIndex].returnString,
			pTransaction, pTransaction->iCurSequentialIndex);
	}
	 

	GenerateTransactionResponse(pServer, pTransaction, &pTransaction->responseHandle, pTransaction->iBaseTransactionsPerState[BASETRANS_STATE_FAILED] ? TRANSACTION_OUTCOME_FAILURE : TRANSACTION_OUTCOME_SUCCESS );
	CleanupTransaction(pServer, pTransaction);
}




void SendFirstMessagesForNewOrUnblockedTransaction(TransactionServer *pServer, Transaction *pTransaction)
{
	int i;
	bool bAtLeastOneSucceeded = false;

	switch (pTransaction->eType)
	{
	case TRANS_TYPE_SIMULTANEOUS:

		for (i=0; i < pTransaction->iNumBaseTransactions; i++)
		{
			if (pTransaction->pBaseTransactions[i].eState == BASETRANS_STATE_STARTUP || pTransaction->pBaseTransactions[i].eState == BASETRANS_STATE_BLOCKED)
			{
				if (SendNewTransactionMessage(pServer, pTransaction,i, 0, 0))
				{
					ChangeBaseTransState(pServer, pTransaction, i, BASETRANS_STATE_INITQUERYSENT);
					bAtLeastOneSucceeded = true;
				}
				else
				{

					ChangeBaseTransState(pServer, pTransaction, i, BASETRANS_STATE_FAILED);
					pTransaction->pBaseTransactions[i].eOutcome = TRANSACTION_OUTCOME_FAILURE;
					GenerateObjectNotFoundError(&pTransaction->pBaseTransactions[i].returnString,
						pTransaction, i);

				}
			}
			else
			{
				TransactionAssert(pTransaction->pBaseTransactions[i].eState == BASETRANS_STATE_FAILED ||
					pTransaction->pBaseTransactions[i].eState == BASETRANS_STATE_SUCCEEDED, 
					pTransaction, "Base trans %d in invalid state for New or Unblocked transaction", i);
			}
		}

		if (!bAtLeastOneSucceeded)
		{
			GenerateTransactionResponse(pServer, pTransaction, &pTransaction->responseHandle, TRANSACTION_OUTCOME_FAILURE);
			CleanupTransaction(pServer, pTransaction);
		}



		break;

	case TRANS_TYPE_SIMULTANEOUS_ATOMIC:
		for (i=0; i < pTransaction->iNumBaseTransactions; i++)
		{
			TransactionAssert(pTransaction->pBaseTransactions[i].eState == BASETRANS_STATE_STARTUP 
				|| pTransaction->pBaseTransactions[i].eState == BASETRANS_STATE_BLOCKED 
				|| pTransaction->pBaseTransactions[i].eState == BASETRANS_STATE_CANCELLED, pTransaction, "Base trans %d in invalid state for New or Unblocked transaction", i);

			if (SendNewTransactionMessage(pServer, pTransaction, i, true, pTransaction->iNumBaseTransactions == 1))
			{
				ChangeBaseTransState(pServer, pTransaction, i, BASETRANS_STATE_INITQUERYSENT);
				bAtLeastOneSucceeded = true;
			}
			else
			{
				ChangeBaseTransState(pServer, pTransaction, i, BASETRANS_STATE_FAILED);
				pTransaction->pBaseTransactions[i].eOutcome = TRANSACTION_OUTCOME_FAILURE;
				GenerateObjectNotFoundError(&pTransaction->pBaseTransactions[i].returnString,
					pTransaction, i);
			}
		}
		if (!bAtLeastOneSucceeded)
		{
			GenerateTransactionResponse(pServer, pTransaction, &pTransaction->responseHandle, TRANSACTION_OUTCOME_FAILURE);
			CleanupTransaction(pServer, pTransaction);
		}
		break;
	

	case TRANS_TYPE_SEQUENTIAL:
		TransactionAssert(pTransaction->iCurSequentialIndex < pTransaction->iNumBaseTransactions, pTransaction, "Invalid curSequentialIndex");
		SendNextSequentialTransactionMessage(pServer, pTransaction);
		break;

	case TRANS_TYPE_SEQUENTIAL_STOPONFAIL:
		TransactionAssert(pTransaction->iCurSequentialIndex < pTransaction->iNumBaseTransactions, pTransaction, "Invalid curSequentialIndex");
		SendNextSequentialStopOnFailTransactionMessage(pServer, pTransaction);
		break;


	case TRANS_TYPE_SEQUENTIAL_ATOMIC:
		pTransaction->iCurSequentialIndex = 0;
		TransactionAssert(pTransaction->pBaseTransactions[pTransaction->iCurSequentialIndex].eState == BASETRANS_STATE_STARTUP 
			|| pTransaction->pBaseTransactions[pTransaction->iCurSequentialIndex].eState == BASETRANS_STATE_BLOCKED
			|| pTransaction->pBaseTransactions[pTransaction->iCurSequentialIndex].eState == BASETRANS_STATE_CANCELLED,
			pTransaction, "Base Transaction %d in an invalid state for new or unblocked transaction", pTransaction->iCurSequentialIndex);

		if (SendNewTransactionMessage(pServer, pTransaction, pTransaction->iCurSequentialIndex, true, pTransaction->iNumBaseTransactions == 1))
		{
			ChangeBaseTransState(pServer, pTransaction, pTransaction->iCurSequentialIndex, BASETRANS_STATE_INITQUERYSENT);
		}
		else
		{
			GenerateObjectNotFoundError(&pTransaction->pBaseTransactions[pTransaction->iCurSequentialIndex].returnString,
				pTransaction, pTransaction->iCurSequentialIndex);
			pTransaction->pBaseTransactions[pTransaction->iCurSequentialIndex].eOutcome = TRANSACTION_OUTCOME_FAILURE;


			if (pTransaction->iCurSequentialIndex == 0)
			{
				GenerateTransactionResponse(pServer, pTransaction, &pTransaction->responseHandle, TRANSACTION_OUTCOME_FAILURE);
				CleanupTransaction(pServer, pTransaction);
			}
			else
			{
				ChangeBaseTransState(pServer, pTransaction, pTransaction->iCurSequentialIndex, BASETRANS_STATE_FAILED);

				BeginCancellingSequentialAtomicTrans(pServer, pTransaction);
			}
		}
		break;


	default: 
		TransactionAssert(0, pTransaction, "Unknown Transaction type");
	}
}

//only works for server types not entity/container types
ContainerID FindFirstExtantIDOfServerType(TransactionServer *pServer, GlobalType eType)
{
	ObjectTypeDirectory *pDirectory = &pServer->objectDirectories[eType];
	StashTableIterator iterator;
	StashElement element;
	int iConnectionIndex;


	stashGetIterator(pDirectory->directory, &iterator);


	if (stashGetNextElement(&iterator, &element))
	{
		LogicalConnection *pConnection;

		iConnectionIndex = stashElementGetInt(element);

		pConnection = &pServer->pConnections[iConnectionIndex % gMaxLogicalConnections];

		if (pConnection->iConnectionID != 0)
		{
			if (pConnection->eServerType == eType)
			{
				return pConnection->iServerID;
			}
		}
	}

	return 0;
}


ContainerID FindRandomServerIDOfServerType(TransactionServer *pServer, GlobalType eType)
{
	ObjectTypeDirectory *pDirectory = &pServer->objectDirectories[eType];
	StashTableIterator iterator;
	StashElement element;
	int iConnectionIndex;
	int iCount;
	ContainerID iRetVal;
	
	ContainerID *pOutIDs = NULL;
	ea32SetCapacity(&pOutIDs, 8);


	stashGetIterator(pDirectory->directory, &iterator);


	while (stashGetNextElement(&iterator, &element))
	{
		LogicalConnection *pConnection;

		iConnectionIndex = stashElementGetInt(element);
		pConnection = &pServer->pConnections[iConnectionIndex % gMaxLogicalConnections];

		if (pConnection->iConnectionID != 0)
		{
			if (pConnection->eServerType == eType)
			{
				ea32Push(&pOutIDs, pConnection->iServerID);
			}
		}
	}

	iCount = ea32Size(&pOutIDs);

	if (!iCount)
	{
		ea32Destroy(&pOutIDs);
		return 0;
	}

	if (iCount == 1)
	{
		iRetVal = pOutIDs[0];
	}
	else
	{
		iRetVal = pOutIDs[randomIntRange(0, iCount -1)];
	}

	ea32Destroy(&pOutIDs);
	return iRetVal;
}




//NOTE!!! READ THIS!!!!!     If you modify this function, note that new transaction requests are sent two different places:
//transactionRequestManager.c/RequestNewTransaction and LocalTransactionManager/PromoteLocalTransaction. They both must be modified
void HandleNewTransactionRequest(TransactionServer *pServer, Packet *pPacket, int eServerType, U32 iServerID, int iConnectionIndex)
{
	Transaction *pTransaction = GetEmptyTransaction(pServer);
	int iTotalMessageSize;
	char *pMessageBuf;
	int i;
	int iSizeToAlloc;
	char *pLocalError = NULL;

	assertmsgf(pTransaction, "Ran out of transactions on transaction server");

	PERFINFO_AUTO_START_FUNC();

	CountAverager_ItHappened(pServer->pAllTransactionCounter);

	pTransaction->eType = (enumTransactionType)pktGetBitsPack(pPacket, 1);
	
	pTransaction->iNumBaseTransactions = pktGetBitsPack(pPacket, 1);

	TransactionAssert(pTransaction->iNumBaseTransactions > 0, pTransaction, "A transaction must have at least one base transaction");

	iTotalMessageSize = pktGetBitsPack(pPacket, 7);


	IntAverager_AddDatapoint(pServer->pBytesPerTransactionAverager, iTotalMessageSize);

	TransactionAssert(iTotalMessageSize > 0, pTransaction, "zero-sized total message size");

	pServer->iNumAllocations++;

	iSizeToAlloc = sizeof(BaseTransactionInfo) * pTransaction->iNumBaseTransactions + iTotalMessageSize;
	pTransaction->pBaseTransactions = (BaseTransactionInfo*)calloc(iSizeToAlloc,1);
	
	pMessageBuf = (char*)(pTransaction->pBaseTransactions + pTransaction->iNumBaseTransactions);


	for (i=0; i < pTransaction->iNumBaseTransactions; i++)
	{
		char *pString;
		size_t len;
		ContainerID iID;
		GlobalType eType; 

		estrCreate(&pTransaction->pBaseTransactions[i].returnString);

		iID = pTransaction->pBaseTransactions[i].transaction.recipient.containerID = GetContainerIDFromPacket(pPacket);
		eType = pTransaction->pBaseTransactions[i].transaction.recipient.containerType = GetContainerTypeFromPacket(pPacket);
		
		//make this smarter, but for now make it work
		if (iID >= LOWEST_SPECIAL_CONTAINERID)
		{
			if (iID == SPECIAL_CONTAINERID_FIND_BEST_FOR_TRANSACTION)
			{
				//always use the server that requested the transaction, if possible, since we know it's running
				if (eType == eServerType)
				{
					pTransaction->pBaseTransactions[i].transaction.recipient.containerID = iServerID;
				}
				else
				{
					//FIXME this needs to get faster, although the previous clause makes the situation much less common where it matters
					pTransaction->pBaseTransactions[i].transaction.recipient.containerID = FindFirstExtantIDOfServerType(pServer, eType);
					if (!pTransaction->pBaseTransactions[i].transaction.recipient.containerID)
					{
						char tempTransData[64];
						strcpy_trunc(tempTransData, pTransaction->pBaseTransactions[i].transaction.pData ? pTransaction->pBaseTransactions[i].transaction.pData : "");
						estrConcatf(&pLocalError, "Tried to find best %s for transaction step beginning %s, but none seem to exist",
							GlobalTypeToName(eType), tempTransData);
					}

				}
			}
			else if (iID == SPECIAL_CONTAINERID_RANDOM)
			{
				if (!(pTransaction->pBaseTransactions[i].transaction.recipient.containerID = FindRandomServerIDOfServerType(pServer, eType)))
				{
					estrConcatf(&pLocalError, "Tried to find a random %s, failed to. This will make this transaction fail\n", GlobalTypeToName(eType));
				}
			}
			else
			{
				Errorf("Transaction server encountered unhandled special container ID %u... converting to 0", iID);
				pTransaction->pBaseTransactions[i].transaction.recipient.containerID = 0;
			}
		}



		pString = pktGetStringTemp(pPacket);
		len = strlen(pString);

	
		pTransaction->pBaseTransactions[i].transaction.pData = pMessageBuf;
		memcpy(pTransaction->pBaseTransactions[i].transaction.pData, pString, len + 1);

		pMessageBuf += len + 1;

		if (pktGetBits(pPacket, 1))
		{
			estrCopy2(&pTransaction->pBaseTransactions[i].transaction.pRequestedTransVariableNames, pktGetStringTemp(pPacket));
		}



		pTransaction->pBaseTransactions[i].eState = BASETRANS_STATE_STARTUP;
		pTransaction->pBaseTransactions[i].eOutcome = TRANSACTION_OUTCOME_UNDEFINED;
		pTransaction->iBaseTransactionsPerState[BASETRANS_STATE_STARTUP]++;

		TransClearConnectionHandle(pServer, pTransaction, i);
	}

	pTransaction->pTransactionName = allocAddString(pktGetStringTemp(pPacket));

	pTransaction->pTracker = FindTransactionTracker(pServer, pTransaction->pTransactionName, true);

	pTransaction->iCurSequentialIndex = 0;
	
	//transactions with only one base transaction are treated as TRANS_TYPE_SIMULTANEOUS, because all the extraneous logic is irrelevant
	// I disabled this, becuase my code depends on things locking when they're told to -BZ
	//if (pTransaction->iNumBaseTransactions == 1)
	//{
	//	pTransaction->eType = TRANS_TYPE_SIMULTANEOUS;
	//}

	if (pktGetBits(pPacket, 1))
	{
		pTransaction->responseHandle.iReturnValID = pktGetBits(pPacket, 32);

		ConnectionHandle_SetFromIndex(pServer, &pTransaction->responseHandle.connectionHandle, iConnectionIndex);

	}
	else
	{
		pTransaction->responseHandle.iReturnValID = 0;
	}

	//the transaction might be a local transaction being promoted to global. If so, it already has some result strings and some
	//base transactions might have already failed.
	if (pktGetBits(pPacket, 1))
	{
		for (i=0; i < pTransaction->iNumBaseTransactions; i++)
		{
			enumTransactionOutcome eNewOutcome = pktGetBits(pPacket, 2);
			
			pTransaction->pBaseTransactions[i].eOutcome = eNewOutcome;

			if (eNewOutcome == TRANSACTION_OUTCOME_FAILURE || eNewOutcome == TRANSACTION_OUTCOME_SUCCESS)
			{
				if (pTransaction->eType == TRANS_TYPE_SEQUENTIAL || pTransaction->eType == TRANS_TYPE_SEQUENTIAL_STOPONFAIL)
				{
					pTransaction->iCurSequentialIndex = i + 1;
				}

				ChangeBaseTransState(pServer, pTransaction, i, eNewOutcome == TRANSACTION_OUTCOME_FAILURE ? BASETRANS_STATE_FAILED : BASETRANS_STATE_SUCCEEDED);
			}
			estrAppendFromPacket(&pTransaction->pBaseTransactions[i].returnString,pPacket);

			SetTransDataBlockFromPacket(pPacket, &pTransaction->pBaseTransactions[i].databaseUpdateData);

			estrAppendFromPacket(&pTransaction->pBaseTransactions[i].transServerUpdateString,pPacket);
		}



	}

	if (gbLogAllTransactions)
	{
		objLog(LOG_TRANSSERVER, GLOBALTYPE_NONE, 0, 0, NULL, NULL, NULL, "newTrans", NULL, "name %s size %d",
			pTransaction->pTransactionName, iSizeToAlloc);
	
	}

	//check if we need to do verbose logging for this trans
	if (pTransaction->pTracker->bDoVerboseLogging)
	{
		eaSetCapacity(&pTransaction->ppVerboseLogs, 2);
		ea32Push(&pServer->pTransactionsToVerboseLog, pTransaction->iID);
	}
	

	TRANS_VERBOSE_LOG(pTransaction, TVL_CREATED, NULL, eServerType, iServerID, 0);


	//	filelog_printf("transactions.log","NEWTR%d %s[%d]: %s\n",pTransaction->iID,GlobalTypeToName(pTransaction->pBaseTransactions[0].transaction.recipient.containerType),pTransaction->pBaseTransactions[0].transaction.recipient.containerID,pTransaction->pBaseTransactions[0].transaction.pData);

	SendFirstMessagesForNewOrUnblockedTransaction(pServer, pTransaction);


	//Bruce's special idea for cancelling presumably stupid transactions if the trans server gets full
	if (pServer->iNumActiveTransactions == (gMaxTransactions - 5))
	{	
		S64 iStartingTicks = timerCpuTicks64();

		const char *pMostCommonName;

		int iKilledCount = 0;


		NamedTransactionCount **ppCounts = NULL;
	
		gbNoVerboseLogging = true;

		CountTransactionsByName(pServer, &ppCounts);
		
		assert(ppCounts[0]);
		pMostCommonName = ppCounts[0]->pTransName;

		eaDestroyStruct(&ppCounts, parse_NamedTransactionCount);

		for (i=0; i < gMaxTransactions; i++)
		{
			if (pServer->pTransactions[i].eType != TRANS_TYPE_NONE && pServer->pTransactions[i].eType != TRANS_TYPE_CORRUPT)
			{
				if (stricmp(pServer->pTransactions[i].pTransactionName, pMostCommonName) == 0)
				{
					int j;
					Transaction *pTransactionToKill = &pServer->pTransactions[i];

					iKilledCount++;

					for (j=0; j < pTransactionToKill->iNumBaseTransactions; j++)
					{
						AbortBaseTransaction(pServer, pTransactionToKill, j, "SpecialBruceKill");
						
						//check if aborting the base transaction caused the entire transaction to be finished
						if (pTransactionToKill->eType == TRANS_TYPE_NONE)
						{
							break;
						}
					}
				}
			}
		}
			
		ErrorOrAlert("TRANS_OVERLOAD_MANY_KILLED", "There were %d active transactions. This nearly maxed out the transaction server's limit of %d. The most common transaction was \"%s\". So all %d of those were killed. There are now %d total active transactions of all types. This whole operation took %f seconds",
			gMaxTransactions - 5, gMaxTransactions, pMostCommonName, iKilledCount, pServer->iNumActiveTransactions, timerSeconds64(timerCpuTicks64() - iStartingTicks));

		gbNoVerboseLogging = false;

	}

	if (pLocalError)
	{
		Errorf("Errors encountered for transaction %s: %s", pTransaction->pTransactionName, pLocalError);
		estrDestroy(&pLocalError);
	}

	PERFINFO_AUTO_STOP();
}
//!!!! READ THIS!!! READ THIS!!!! (read the comment at the top of the function)

	
bool TransactionRecipientHasMoved(TransactionServer *pServer, Transaction *pTransaction, int iTransIndex)
{
	ContainerRef *pRecipient = &pTransaction->pBaseTransactions[iTransIndex].transaction.recipient;

	int iConnectionIndex = GetConnectionIndexFromRecipient(pServer, pTransaction->pTransactionName, pRecipient);

	if (iConnectionIndex == -1)
	{
		return true;
	}

	if (!ConnectionHandle_MatchesIndex(pServer, &pTransaction->pBaseTransactions[iTransIndex].transConnectionHandle, iConnectionIndex))
	{
		return true;
	}

	return false;
}

typedef enum SendSimpleFlag
{
	SENDSIMPLEFLAG_ONLY_SEND_IF_CONNECTION_ALREADY_EXISTS = 1 << 0,
} SendSimpleFlag;

bool SendSimpleMessage(TransactionServer *pServer, Transaction *pTransaction, int iTransIndex, int eMessageType, SendSimpleFlag eFlags)
{
	Packet *pPacket;
	
	ContainerRef *pRecipient = &pTransaction->pBaseTransactions[iTransIndex].transaction.recipient;

	int iConnectionIndex;

	if (ConnectionHandle_CheckIfSetAndReturnIndex(pServer, &pTransaction->pBaseTransactions[iTransIndex].transConnectionHandle, &iConnectionIndex))
	{
		if (iConnectionIndex == -1)
		{
			if (eFlags & SENDSIMPLEFLAG_ONLY_SEND_IF_CONNECTION_ALREADY_EXISTS)
			{
				return false;
			}

		}
	}
	else
	{
		if (eFlags & SENDSIMPLEFLAG_ONLY_SEND_IF_CONNECTION_ALREADY_EXISTS)
		{
			return false;
		}

		iConnectionIndex = GetConnectionIndexFromRecipient(pServer, pTransaction->pTransactionName, pRecipient);
		if (iConnectionIndex == -1)
		{

			return false;
		}

		ConnectionHandle_SetFromIndex(pServer, &pTransaction->pBaseTransactions[iTransIndex].transConnectionHandle, iConnectionIndex);
		LogicalConnection_AddTransaction(&pServer->pConnections[iConnectionIndex], pTransaction->iID);

	}




/*
	if (iConnectionIndex == -1)
	{
		iConnectionIndex = GetConnectionIndexFromRecipient(pServer, pRecipient);
		if (iConnectionIndex == -1)
		{
			if (bAssertOnFailure)
			{
				TransactionAssert(0, pTransaction, "Unable to send required simple message of type %s",
					StaticDefineIntRevLookup(enumTransServerMessagesToLTMsEnum, eMessageType));
			}

			return false;
		}
		pTransaction->pBaseTransactions[iTransIndex].connection.iConnectionIndex = iConnectionIndex;
		pTransaction->pBaseTransactions[iTransIndex].connection.iConnectionID = pServer->pConnections[iConnectionIndex].iConnectionID;
	}
	else
	{
		if (pTransaction->pBaseTransactions[iTransIndex].connection.iConnectionID != pServer->pConnections[iConnectionIndex].iConnectionID)
		{
			if (bAssertOnFailure)
			{
				TransactionAssert(0, pTransaction, "Unable to send required simple message of type %s (recipient has moved?)",
					StaticDefineIntRevLookup(enumTransServerMessagesToLTMsEnum, eMessageType));
			}

			return false;
		}
	}
*/


	pPacket = NewTransactionPacket(pServer, iConnectionIndex, eMessageType);


	PutTransactionIDIntoPacket(pPacket, pTransaction->iID);
	pktSendBitsPack(pPacket, 1, iTransIndex);
	PutContainerTypeIntoPacket(pPacket, pRecipient->containerType);
	PutContainerIDIntoPacket(pPacket, pRecipient->containerID);

	if (eMessageType == TRANSSERVER_CONFIRM_TRANSACTION || eMessageType == TRANSSERVER_CANCEL_TRANSACTION)
	{
		pktSendBits(pPacket, 32, pTransaction->pBaseTransactions[iTransIndex].iLocalTransactionManagerCallbackID);
	}

	pktSend(&pPacket);



	return true;
}



enumTransactionOutcome GetBaseTransOutcome(Transaction *pTransaction, int iBaseTransIndex)
{
	return pTransaction->pBaseTransactions[iBaseTransIndex].eOutcome;
}

void VerboseLogUpdateStrings(TransactionServer *pServer, Transaction *pTransaction, char *pComment)
{
	char *pFullLogString = NULL;
	int i;

	estrPrintf(&pFullLogString, "Update strings beign committed for Trans %u(%s).\n",  pTransaction->iID, pTransaction->pTransactionName);

	for (i = 0; i < pTransaction->iNumBaseTransactions; i++)
	{
		if (!TransDataBlockIsEmpty(&pTransaction->pBaseTransactions[i].databaseUpdateData))
		{
			estrConcatf(&pFullLogString, "BT %d DB: ", i);
			if (pTransaction->pBaseTransactions[i].databaseUpdateData.pString1)
			{
				estrConcatf(&pFullLogString, "%s\n", pTransaction->pBaseTransactions[i].databaseUpdateData.pString1);
				if (pTransaction->pBaseTransactions[i].databaseUpdateData.pString2)
				{
					estrConcatf(&pFullLogString, "\t(string2) %s\n", pTransaction->pBaseTransactions[i].databaseUpdateData.pString2);
				}

			}
			else
			{
				estrConcatf(&pFullLogString, "(%d binary bytes)\n", pTransaction->pBaseTransactions[i].databaseUpdateData.iDataSize);
			}
		}
	}

	for (i = 0; i < pTransaction->iNumBaseTransactions; i++)
	{
		if (pTransaction->pBaseTransactions[i].transServerUpdateString &&
			pTransaction->pBaseTransactions[i].transServerUpdateString[0])
		{
			estrConcatf(&pFullLogString, "BT %d TS: %s\n", i, pTransaction->pBaseTransactions[i].transServerUpdateString);
		}
	}

	log_printf(LOG_VERBOSETRANS, "%s", pFullLogString);
	estrDestroy(&pFullLogString);
}

void ProcessAndSendUpdateStrings(TransactionServer *pServer, Transaction *pTransaction, char *pComment)
{
	int i;
	Packet *pPacket;
	TransDataBlock **ppDataBlocks = NULL;

	int iTotalDataSize = 0;

	if (gbVerboseLogEverything)
	{
		VerboseLogUpdateStrings(pServer, pTransaction, pComment);
	}

	for (i=0; i < pTransaction->iNumBaseTransactions; i++)
	{
		if (!TransDataBlockIsEmpty(&pTransaction->pBaseTransactions[i].databaseUpdateData))
		{
			if (pTransaction->pBaseTransactions[i].databaseUpdateData.iDataSize)
				iTotalDataSize += pTransaction->pBaseTransactions[i].databaseUpdateData.iDataSize;
			else
				iTotalDataSize += estrLength(&pTransaction->pBaseTransactions[i].databaseUpdateData.pString1) 
				+ estrLength(&pTransaction->pBaseTransactions[i].databaseUpdateData.pString2);
			eaPush(&ppDataBlocks, &pTransaction->pBaseTransactions[i].databaseUpdateData);
		}

		if (pTransaction->pBaseTransactions[i].transServerUpdateString &&
			pTransaction->pBaseTransactions[i].transServerUpdateString[0])
		{
			char pNewComment[2048];
			sprintf(pNewComment, "ProcessAndSendUpdateStrings called for transaction %s(%u) with comment %s", 
				pTransaction->pTransactionName, pTransaction->iID, pComment);
			ProcessTransactionServerCommandString(pServer, pTransaction->pBaseTransactions[i].transServerUpdateString, pNewComment);			
		}
	}

	if (eaSize(&ppDataBlocks))
	{
		TransactionAssert(pServer->iDatabaseConnectionIndex != -1, pTransaction, "To handle database updates, an ObjectDB must be running");
		pPacket = NewTransactionPacket(pServer, pServer->iDatabaseConnectionIndex, TRANSSERVER_TRANSACTION_DBUPDATE);
		for (i=0; i < eaSize(&ppDataBlocks); i++)
		{
			PutTransDataBlockIntoPacket(pPacket, ppDataBlocks[i]);
		}
		PutTransDataBlockIntoPacket(pPacket, NULL);
		pTransaction->pTracker->iObjectDBUpdateBytes += pktGetSize(pPacket);
		pktSend(&pPacket);
		eaDestroy(&ppDataBlocks);
	}

	if (gbLogAllTransactions)
	{
		objLog(LOG_TRANSSERVER, GLOBALTYPE_NONE, 0, 0, NULL, NULL, NULL, "transUpdateDB", NULL, "name %s size %d",
			pTransaction->pTransactionName, iTotalDataSize);
	}
}



void GenerateTransactionResponse(TransactionServer *pServer, Transaction *pTransaction, TransactionResponseHandle *pHandle, enumTransactionOutcome eOutcome)
{
	int i;
	Packet *pPacket;

	if (giLagOnTransact)
	{
		printf("Sleeping %d seconds on transaction (%s)...", giLagOnTransact, pTransaction->pTransactionName);
		Sleep(giLagOnTransact * 1000);
		printf("Done\n");
	}

	ReportOutcomeToTracker(pTransaction->pTracker, eOutcome);

	if (eOutcome == TRANSACTION_OUTCOME_FAILURE)
	{
		TRANS_VERBOSE_LOG(pTransaction, TVL_FAILED, NULL, 0, 0, 0);
	}
	else
	{
		TRANS_VERBOSE_LOG(pTransaction, TVL_SUCCEEDED, NULL, 0, 0, 0);
	}


	if (eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		pServer->iNumSucceededTransactions++;
		if ((pServer->iNumSucceededTransactions % 10000) == 0 && performanceStats)
		{
			filelog_printf("dbPerformance","%"FORM_LL"d transactions complete (%d processed, %d active) at %f after %d ticks\n",pServer->iNumSucceededTransactions,pServer->iNextTransactionID,pServer->iNumActiveTransactions,timerElapsed(perfTimer),perfMainTicks);
		}
	}
	else
	{
		pServer->iNumCompletedNonSucceededTransactions++;

		//log failed transactions one every 10 seconds per transaction name
		if (eOutcome == TRANSACTION_OUTCOME_FAILURE)
		{
			char *pLogString = NULL;
			U32 iCurTime = timeSecondsSince2000();

			if (iCurTime - 10 > pTransaction->pTracker->iLastFailedLogTime)
			{
				pTransaction->pTracker->iLastFailedLogTime = iCurTime;
				estrStackCreate(&pLogString);
				estrPrintf(&pLogString, "TRANSACTION FAILED. %d base transactions:", pTransaction->iNumBaseTransactions);

				for (i=0; i < pTransaction->iNumBaseTransactions; i++)
				{
					estrConcatf(&pLogString, " (Base Transaction %d to %s[%u]: String \"%s\". Status \"%s\". Return string \"%s\".) ",
						i, 
						GlobalTypeToName(pTransaction->pBaseTransactions[i].transaction.recipient.containerType),
						pTransaction->pBaseTransactions[i].transaction.recipient.containerID,
						pTransaction->pBaseTransactions[i].transaction.pData, StaticDefineIntRevLookup(enumTransactionOutcomeEnum, pTransaction->pBaseTransactions[i].eOutcome),
						pTransaction->pBaseTransactions[i].returnString);
				}

				estrCopy(&pTransaction->pTracker->pMostRecentLoggedFailString, &pLogString);

				log_printf(LOG_TRANSSERVER, "%s", pLogString);

				estrDestroy(&pLogString);
			}
		}
	}



	//check whether to send out update strings.. atomic transactions have already done this
	if (!TransactionIsAtomic(pTransaction))
	{
		ProcessAndSendUpdateStrings(pServer, pTransaction, "Non-atomic trans generating response");
	}

	if (pHandle->iReturnValID)
	{
		int iConnection = GetConnectionFromResponseHandle(pServer, pHandle);
		if (iConnection != -1)
		{
			//	filelog_printf("transactions.log","RESTR%d %s[%d]: %s\n",pTransaction->iID,GlobalTypeToName(pTransaction->pBaseTransactions[0].transaction.recipient.containerType),pTransaction->pBaseTransactions[0].transaction.recipient.containerID,pTransaction->pBaseTransactions[0].transaction.pData);

			pPacket = NewTransactionPacket(pServer, iConnection, TRANSSERVER_TRANSACTION_COMPLETE);

			pktSendBits(pPacket, 32, pHandle->iReturnValID);

			pktSendBits(pPacket, 2, eOutcome);

			pktSendBitsPack(pPacket, 1, pTransaction->iNumBaseTransactions);

			for (i=0; i < pTransaction->iNumBaseTransactions; i++)
			{
				enumTransactionOutcome eBaseTransOutcome = GetBaseTransOutcome(pTransaction, i);
				pktSendBits(pPacket, 2, eBaseTransOutcome);
				pktSendString(pPacket, pTransaction->pBaseTransactions[i].returnString);
			}
			
			
			if (gbLogAllTransactions)
			{
				objLog(LOG_TRANSSERVER, GLOBALTYPE_NONE, 0, 0, NULL, NULL, NULL, "transReturn", NULL, "name %s size %d",
					pTransaction->pTransactionName, pktGetSize(pPacket));
			}		
			
			pktSend(&pPacket);

	
		}
	}
	else
	{
		if (gbLogAllTransactions)
		{
			objLog(LOG_TRANSSERVER, GLOBALTYPE_NONE, 0, 0, NULL, NULL, NULL, "transReturn", NULL, "name %s size 0",
				pTransaction->pTransactionName);
		}	
	}
}


void ResetAtomicTransaction(TransactionServer *pServer, Transaction *pTransaction)
{
	int i;

	for (i=0; i < pTransaction->iNumBaseTransactions; i++)
	{
		ChangeBaseTransState(pServer, pTransaction, i, BASETRANS_STATE_STARTUP);
		estrDestroy(&pTransaction->pBaseTransactions[i].returnString);
		TransDataBlockClear(&pTransaction->pBaseTransactions[i].databaseUpdateData);
		estrDestroy(&pTransaction->pBaseTransactions[i].transServerUpdateString);
		pTransaction->pBaseTransactions[i].eOutcome = TRANSACTION_OUTCOME_UNDEFINED;

		TransClearConnectionHandle(pServer, pTransaction, i);
	}


	if (pTransaction->transVariableTable)
	{
		DestroyNameTable(pTransaction->transVariableTable);
		pTransaction->transVariableTable = NULL;
	}
}





void BeginCancellingSimulAtomicTransaction(TransactionServer *pServer, Transaction *pTransaction)
{
	int i;



	TransactionAssert(pTransaction->eType == TRANS_TYPE_SIMULTANEOUS_ATOMIC, pTransaction, "Type mismatch");


	for (i=0; i < pTransaction->iNumBaseTransactions; i++)
	{
		if (pTransaction->pBaseTransactions[i].eState == BASETRANS_STATE_POSSIBLE_WAITFORCONFIRM)
		{
			if (SendSimpleMessage(pServer, pTransaction, i, TRANSSERVER_CANCEL_TRANSACTION, SENDSIMPLEFLAG_ONLY_SEND_IF_CONNECTION_ALREADY_EXISTS))
			{
				ChangeBaseTransState(pServer, pTransaction, i, BASETRANS_STATE_CANCEL_SENT);
			}
			else
			{
				ChangeBaseTransState(pServer, pTransaction, i, BASETRANS_STATE_CANCELLED);
			}
		}
	}

	//only if no cancel messages could be sent. Normally a cancelled transaction blocks and gets retried later
	if (pTransaction->iBaseTransactionsPerState[BASETRANS_STATE_CANCELLED] == pTransaction->iNumBaseTransactions)
	{
		GenerateTransactionResponse(pServer, pTransaction, &pTransaction->responseHandle, TRANSACTION_OUTCOME_FAILURE);
		CleanupTransaction(pServer, pTransaction);
	}
}

void CheckForSimulAtomicTransactionCompleteCancelling(TransactionServer *pServer, Transaction *pTransaction)
{
	TransactionAssert(pTransaction->eType == TRANS_TYPE_SIMULTANEOUS_ATOMIC, pTransaction, "Type Mismatch");

	if ( pTransaction->iBaseTransactionsPerState[BASETRANS_STATE_FAILED]
		+ pTransaction->iBaseTransactionsPerState[BASETRANS_STATE_BLOCKED]
		+ pTransaction->iBaseTransactionsPerState[BASETRANS_STATE_CANCELLED] 
		== pTransaction->iNumBaseTransactions)
	{
		if (pTransaction->iBaseTransactionsPerState[BASETRANS_STATE_FAILED])
		{
			GenerateTransactionResponse(pServer, pTransaction, &pTransaction->responseHandle, TRANSACTION_OUTCOME_FAILURE);
			CleanupTransaction(pServer, pTransaction);
		}
		else
		{
			ResetAtomicTransaction(pServer, pTransaction);
			AddBlockedTransaction(pServer, pTransaction);
		}
	}
}

void BeginCancellingSequentialAtomicTrans(TransactionServer *pServer, Transaction *pTransaction)
{
	int i;
	
	TransactionAssert(pTransaction->iCurSequentialIndex > 0, pTransaction, "Trying to cancel non-started transaction");
	TransactionAssert(pTransaction->eType == TRANS_TYPE_SEQUENTIAL_ATOMIC, pTransaction, "Type Mismatch");

	TRANS_VERBOSE_LOG(pTransaction, TVL_BEGIN_CANCELLING, NULL, 0, 0, 0);

	

	for (i=0; i < pTransaction->iCurSequentialIndex; i++)
	{
		TransactionAssert(pTransaction->pBaseTransactions[i].eState == BASETRANS_STATE_POSSIBLE_WAITFORCONFIRM, pTransaction,
			"Trying to cancel step %d which should be possible, but isn't", i);

		if (SendSimpleMessage(pServer, pTransaction, i, TRANSSERVER_CANCEL_TRANSACTION, SENDSIMPLEFLAG_ONLY_SEND_IF_CONNECTION_ALREADY_EXISTS))
		{
			ChangeBaseTransState(pServer, pTransaction, i, BASETRANS_STATE_CANCEL_SENT);
		}
		else
		{
			ChangeBaseTransState(pServer, pTransaction, i, BASETRANS_STATE_CANCELLED);
		}
	}

	if (pTransaction->iBaseTransactionsPerState[BASETRANS_STATE_CANCELLED] == pTransaction->iCurSequentialIndex)
	{
		GenerateTransactionResponse(pServer, pTransaction, &pTransaction->responseHandle, TRANSACTION_OUTCOME_FAILURE);
		CleanupTransaction(pServer, pTransaction);
	}
}

void CheckForSimultaneousTransactionCompletionOrBlocking(TransactionServer *pServer, Transaction *pTransaction)
{
	TransactionAssert(pTransaction->eType == TRANS_TYPE_SIMULTANEOUS, pTransaction, "Type Mismatch");


	if (pTransaction->iBaseTransactionsPerState[BASETRANS_STATE_FAILED] 
		+ pTransaction->iBaseTransactionsPerState[BASETRANS_STATE_SUCCEEDED]
		== pTransaction->iNumBaseTransactions)
	{
		GenerateTransactionResponse(pServer, pTransaction, &pTransaction->responseHandle, pTransaction->iBaseTransactionsPerState[BASETRANS_STATE_FAILED] ? TRANSACTION_OUTCOME_FAILURE : TRANSACTION_OUTCOME_SUCCESS);
		CleanupTransaction(pServer, pTransaction);
	}
	else if (pTransaction->iBaseTransactionsPerState[BASETRANS_STATE_FAILED] 
		+ pTransaction->iBaseTransactionsPerState[BASETRANS_STATE_SUCCEEDED]
		+ pTransaction->iBaseTransactionsPerState[BASETRANS_STATE_BLOCKED]
		== pTransaction->iNumBaseTransactions)
	{

		AddBlockedTransaction(pServer, pTransaction);
	}
}

void GetReturnValStringFromPacketIfAny(TransactionServer *pServer, Transaction *pTransaction, int iBaseTransIndex, Packet *pPacket)
{
	if (pktGetBits(pPacket, 1))
	{
		estrAppendFromPacket(&pTransaction->pBaseTransactions[iBaseTransIndex].returnString,pPacket);
#if 0
		char *pString;
		int iLen;

		pString = pktGetStringAndLength(pPacket, &iLen);

		estrClear(&pTransaction->pBaseTransactions[iBaseTransIndex].returnString);
		estrConcatString(&pTransaction->pBaseTransactions[iBaseTransIndex].returnString, pString, iLen);
#endif
	}
}

void AddUpdateStringsFromPacketIfAny(TransactionServer *pServer, Transaction *pTransaction, int iBaseTransIndex, Packet *pPacket)
{
	SetTransDataBlockFromPacket(pPacket, &pTransaction->pBaseTransactions[iBaseTransIndex].databaseUpdateData);

	if (pktGetBits(pPacket, 1))
	{
		estrAppendFromPacket(&pTransaction->pBaseTransactions[iBaseTransIndex].transServerUpdateString,pPacket);
#if 0
		char *pString;
		int iLen;

		pString = pktGetStringAndLength(pPacket, &iLen);

		estrClear(&pTransaction->pBaseTransactions[iBaseTransIndex].transServerUpdateString);
		estrConcatString(&pTransaction->pBaseTransactions[iBaseTransIndex].transServerUpdateString, pString, iLen);
#endif
	}
}




void FailBaseTransactionFromInitQueryState(TransactionServer *pServer, Transaction *pTransaction, int iIndex)
{
	if (pTransaction->pBaseTransactions[iIndex].eState == BASETRANS_STATE_FAILED)
	{
		//already failed, this transaction must have been aborted at some point
		return;
	}
		

	TransactionAssert(pTransaction->pBaseTransactions[iIndex].eState == BASETRANS_STATE_INITQUERYSENT, 
		pTransaction, "FailBaseTransactionFromInitQueryState called on Base Transaction %d which is not in that state", iIndex );

		TransClearConnectionHandle(pServer, pTransaction, iIndex);
	pTransaction->pBaseTransactions[iIndex].eOutcome = TRANSACTION_OUTCOME_FAILURE;

	TRANS_VERBOSE_LOG(pTransaction, TVL_FAIL_FROM_INIT_QUERY_STATE, NULL, 0, 0, iIndex);


	switch (pTransaction->eType)
	{
	case TRANS_TYPE_SIMULTANEOUS:
		ChangeBaseTransState(pServer, pTransaction, iIndex, BASETRANS_STATE_FAILED);

		CheckForSimultaneousTransactionCompletionOrBlocking(pServer, pTransaction);
		break;

	case TRANS_TYPE_SIMULTANEOUS_ATOMIC:
		//if this is the first base transaction to fail or block, send out cancel messages to everyone who has succeeded
		if (pTransaction->iBaseTransactionsPerState[BASETRANS_STATE_FAILED] + pTransaction->iBaseTransactionsPerState[BASETRANS_STATE_BLOCKED] == 0)
		{
			BeginCancellingSimulAtomicTransaction(pServer, pTransaction);
		}

		ChangeBaseTransState(pServer, pTransaction, iIndex, BASETRANS_STATE_FAILED);

		CheckForSimulAtomicTransactionCompleteCancelling(pServer, pTransaction);

		break;

	case TRANS_TYPE_SEQUENTIAL:
		TransactionAssert(iIndex == pTransaction->iCurSequentialIndex, pTransaction, "Trying to cancel the wrong step (%d) in a sequential trans", iIndex);
		ChangeBaseTransState(pServer, pTransaction, iIndex, BASETRANS_STATE_FAILED);
		pTransaction->iCurSequentialIndex++;
		SendNextSequentialTransactionMessage(pServer, pTransaction);
		break;


	case TRANS_TYPE_SEQUENTIAL_STOPONFAIL:
		TransactionAssert(iIndex == pTransaction->iCurSequentialIndex, pTransaction, "Trying to cancel the wrong step (*%d) in a sequential trans", iIndex);
		GenerateTransactionResponse(pServer, pTransaction, &pTransaction->responseHandle, TRANSACTION_OUTCOME_FAILURE);
		CleanupTransaction(pServer, pTransaction);
		break;

	case TRANS_TYPE_SEQUENTIAL_ATOMIC:
		TransactionAssert(iIndex == pTransaction->iCurSequentialIndex, pTransaction, "Trying to cancel the wrong step (%d) in a sequential trans", iIndex);
		if (iIndex == 0)
		{
			GenerateTransactionResponse(pServer, pTransaction, &pTransaction->responseHandle, TRANSACTION_OUTCOME_FAILURE);
			CleanupTransaction(pServer, pTransaction);
		}
		else
		{
			ChangeBaseTransState(pServer, pTransaction, iIndex, BASETRANS_STATE_FAILED);

			BeginCancellingSequentialAtomicTrans(pServer, pTransaction);
		}
		break;
	}

}

void HandleTransactionFailed(TransactionServer *pServer, Packet *pPacket)
{
	Transaction *pTransaction;
	int iIndex;

	PERFINFO_AUTO_START_FUNC();

	pTransaction = GetTransactionFromIDIfExists(pServer, GetTransactionIDFromPacket(pPacket));
	iIndex = pktGetBitsPack(pPacket, 1);

	if (!pTransaction)
	{
		log_printf(LOG_TRANSSERVER, "SERIOUS WARNING: Unknown transaction failed");
		PERFINFO_AUTO_STOP();
		return;
	}

	TRANS_VERBOSE_LOG(pTransaction, TVL_STEP_FAILED, NULL, 0, 0, iIndex);




	GetReturnValStringFromPacketIfAny(pServer, pTransaction, iIndex, pPacket);

	FailBaseTransactionFromInitQueryState(pServer, pTransaction, iIndex);

	PERFINFO_AUTO_STOP();
}


void HandleAtomicTransactionSucceeded_Internal(TransactionServer *pServer, Transaction *pTransaction, int iIndex)
{
	TRANS_VERBOSE_LOG(pTransaction, TVL_STEP_SUCCEEDED, NULL, 0, 0, iIndex);

	
	TransClearConnectionHandle(pServer, pTransaction, iIndex);

	ChangeBaseTransState(pServer, pTransaction, iIndex, BASETRANS_STATE_CONFIRMED);

	TransactionAssert(pTransaction->iBaseTransactionsPerState[BASETRANS_STATE_CONFIRMED] 
		+ pTransaction->iBaseTransactionsPerState[BASETRANS_STATE_CONFIRM_SENT] == pTransaction->iNumBaseTransactions,
			pTransaction, "After receiving a confirmation for step %d, a transaction has a base trans in the wrong state", iIndex);

	if (pTransaction->iBaseTransactionsPerState[BASETRANS_STATE_CONFIRMED] == pTransaction->iNumBaseTransactions)
	{
		GenerateTransactionResponse(pServer, pTransaction, &pTransaction->responseHandle, TRANSACTION_OUTCOME_SUCCESS);
		CleanupTransaction(pServer, pTransaction);
	}
}

void HandleTransactionSucceeded(TransactionServer *pServer, Packet *pPacket)
{
	Transaction *pTransaction;
	int iIndex;

	PERFINFO_AUTO_START_FUNC();

	pTransaction = GetTransactionFromIDIfExists(pServer, GetTransactionIDFromPacket(pPacket));
	iIndex = pktGetBitsPack(pPacket, 1);

	if (!pTransaction)
	{
		log_printf(LOG_TRANSSERVER, "SERIOUS WARNING: Unknown transaction succeeded");
		PERFINFO_AUTO_STOP();
		return;
	}

	TransClearConnectionHandle(pServer, pTransaction, iIndex);

	GetReturnValStringFromPacketIfAny(pServer, pTransaction, iIndex, pPacket);
	AddUpdateStringsFromPacketIfAny(pServer, pTransaction, iIndex, pPacket);

	pTransaction->pBaseTransactions[iIndex].eOutcome = TRANSACTION_OUTCOME_SUCCESS;


	switch (pTransaction->eType)
	{
	case TRANS_TYPE_SIMULTANEOUS:
		TransactionAssert(pTransaction->pBaseTransactions[iIndex].eState == BASETRANS_STATE_INITQUERYSENT,
			pTransaction, "step %d in the wrong state when it succeeded", iIndex);
		ChangeBaseTransState(pServer, pTransaction, iIndex, BASETRANS_STATE_SUCCEEDED);

		CheckForSimultaneousTransactionCompletionOrBlocking(pServer, pTransaction);
		break;

	case TRANS_TYPE_SIMULTANEOUS_ATOMIC:
	case TRANS_TYPE_SEQUENTIAL_ATOMIC:
		HandleAtomicTransactionSucceeded_Internal(pServer, pTransaction, iIndex);
		break;

	case TRANS_TYPE_SEQUENTIAL:
		TransactionAssert(pTransaction->pBaseTransactions[iIndex].eState == BASETRANS_STATE_INITQUERYSENT,
			pTransaction, "step %d in the wrong state when it succeeded", iIndex);
		TransactionAssert(iIndex == pTransaction->iCurSequentialIndex, pTransaction, "Got succeeded for the wrong step (%d)", iIndex);
		ChangeBaseTransState(pServer, pTransaction, iIndex, BASETRANS_STATE_SUCCEEDED);
		pTransaction->iCurSequentialIndex++;
		SendNextSequentialTransactionMessage(pServer, pTransaction);
		break;

	
	case TRANS_TYPE_SEQUENTIAL_STOPONFAIL:
		TransactionAssert(pTransaction->pBaseTransactions[iIndex].eState == BASETRANS_STATE_INITQUERYSENT,
			pTransaction, "step %d in the wrong state when it succeeded", iIndex);
		TransactionAssert(iIndex == pTransaction->iCurSequentialIndex, pTransaction, "Got succeeded for the wrong step (%d)", iIndex);
		ChangeBaseTransState(pServer, pTransaction, iIndex, BASETRANS_STATE_SUCCEEDED);
		pTransaction->iCurSequentialIndex++;
		SendNextSequentialStopOnFailTransactionMessage(pServer, pTransaction);
		break;


	}

	PERFINFO_AUTO_STOP();
}


void HandleTransactionBlocked(TransactionServer *pServer, Packet *pPacket)
{
	Transaction *pTransaction;
	int iIndex;
	TransactionID iTransIDThatBlockedIt;


	Transaction *pOtherTransaction;

	PERFINFO_AUTO_START_FUNC();

	pTransaction = GetTransactionFromIDIfExists(pServer, GetTransactionIDFromPacket(pPacket));
	iIndex = pktGetBitsPack(pPacket, 1);
	iTransIDThatBlockedIt = GetTransactionIDFromPacket(pPacket);

	if (!pTransaction)
	{
		log_printf(LOG_TRANSSERVER, "SERIOUS WARNING: Unknown transaction blocked");
		PERFINFO_AUTO_STOP();
		return;
	}

	//presumably the transaction was killed for some other reason, such as gameserver disconnection...
	//abort immediately, do no blocking of any sort
	if (pTransaction->pBaseTransactions[iIndex].eState == BASETRANS_STATE_FAILED)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	pOtherTransaction = GetTransactionFromIDIfExists(pServer, iTransIDThatBlockedIt);

	if (pOtherTransaction)
	{
		if (pTransaction->pTracker == pOtherTransaction->pTracker)
		{
			pTransaction->pTracker->iTimesBlockedSelf++;
		}
		else
		{
			pOtherTransaction->pTracker->iTimesBlockedOther++;
		}
	}

	pTransaction->pTracker->iTimesWasBlocked++;
	

	// When two transactions are blocking each other (because they have each locked something that the other needs)
	//  the first to get here will fall through to the "else" clause below and begin unrolling itself, so that it
	//  can be restarted later.
	// The second transaction will get here and since it is blocked on a transaction that is also blocked, it will
	//  run the special case code here, which causes it to immediately retry the failed base transaction.  It will
	//  eventually get the lock that is released by the first transaction unrolling, and then be able to complete.
	// Finally the first transaction will eventually retry and will be able to lock the data it needs since the
	//  second transaction will have completed.
	if (pOtherTransaction && (pOtherTransaction->eBlockedStatus || pOtherTransaction->iBaseTransactionsPerState[BASETRANS_STATE_BLOCKED]))
	{
// 		printf("Transaction %d blocked by transaction %d, but transaction %d is already blocked, so transaction %d will retry this step\n",
// 			pTransaction->iID, iTransIDThatBlockedIt, iTransIDThatBlockedIt, pTransaction->iID);

		TransClearConnectionHandle(pServer, pTransaction, pTransaction->iCurSequentialIndex);

		if (TransactionIsAtomic(pTransaction))
		{
			SendNewTransactionMessage(pServer, pTransaction, pTransaction->iCurSequentialIndex, true, pTransaction->iNumBaseTransactions == 1);
		}
		else
		{
			SendNewTransactionMessage(pServer, pTransaction, pTransaction->iCurSequentialIndex, 0, 0);
		}
		PERFINFO_AUTO_STOP();
		return;
	}
	else
	{

		//printf("Transaction %d blocked by transaction %d\n", pTransaction->iID, iTransIDThatBlockedIt);

		TransClearConnectionHandle(pServer, pTransaction, iIndex);

		if (iTransIDThatBlockedIt == pTransaction->iID)
		{
			if (isDevelopmentMode())
			{
				Errorf("Transaction %d is deadlocked with itself on step '%s'",pTransaction->iID,pTransaction->pBaseTransactions[iIndex].transaction.pData);
			}
			FailBaseTransactionFromInitQueryState(pServer, pTransaction, iIndex);
			PERFINFO_AUTO_STOP();
			return;
		}
		else if (pTransaction->iWhoBlockedMe == 0)
		{
			pTransaction->iWhoBlockedMe = iTransIDThatBlockedIt;
		}

		TransactionAssert(pTransaction->pBaseTransactions[iIndex].eState == BASETRANS_STATE_INITQUERYSENT, pTransaction,
			"Step %d trying to block, but in wrong state", iIndex);

		switch (pTransaction->eType)
		{
		case TRANS_TYPE_SIMULTANEOUS:
			ChangeBaseTransState(pServer, pTransaction, iIndex, BASETRANS_STATE_BLOCKED);

			CheckForSimultaneousTransactionCompletionOrBlocking(pServer, pTransaction);
			break;

		case TRANS_TYPE_SIMULTANEOUS_ATOMIC:
			//if this is the first base transaction to fail or block, send out cancel messages to everyone who has succeeded
			if (pTransaction->iBaseTransactionsPerState[BASETRANS_STATE_FAILED] + pTransaction->iBaseTransactionsPerState[BASETRANS_STATE_BLOCKED] == 0)
			{
				BeginCancellingSimulAtomicTransaction(pServer, pTransaction);
			}

			ChangeBaseTransState(pServer, pTransaction, iIndex, BASETRANS_STATE_BLOCKED);

			CheckForSimulAtomicTransactionCompleteCancelling(pServer, pTransaction);
			break;

		case TRANS_TYPE_SEQUENTIAL:
		case TRANS_TYPE_SEQUENTIAL_STOPONFAIL:
			TransactionAssert(iIndex == pTransaction->iCurSequentialIndex, pTransaction, "Got blocked for wrong step(%d)", iIndex);
			ChangeBaseTransState(pServer, pTransaction, iIndex, BASETRANS_STATE_BLOCKED);

			AddBlockedTransaction(pServer, pTransaction);
			break;

		case TRANS_TYPE_SEQUENTIAL_ATOMIC:
			TransactionAssert(iIndex == pTransaction->iCurSequentialIndex, pTransaction, "Got blocked for wrong step(%d)", iIndex);
			if (iIndex == 0)
			{
				ChangeBaseTransState(pServer, pTransaction, iIndex, BASETRANS_STATE_BLOCKED);
				ResetAtomicTransaction(pServer, pTransaction);
				AddBlockedTransaction(pServer, pTransaction);
			}
			else
			{
				ChangeBaseTransState(pServer, pTransaction, iIndex, BASETRANS_STATE_BLOCKED);
				BeginCancellingSequentialAtomicTrans(pServer, pTransaction);
			}
			break;
		}
	}
	PERFINFO_AUTO_STOP();
}
void HandleTransactionCancelConfirmed_Internal(TransactionServer *pServer, Transaction *pTransaction, int iIndex)
{
	TRANS_VERBOSE_LOG(pTransaction, TVL_STEP_CANCEL_CONFIRMED, NULL, 0, 0, iIndex);


	TransactionAssert(pTransaction->pBaseTransactions[iIndex].eState == BASETRANS_STATE_CANCEL_SENT, pTransaction, 
		"Got cancel confirmed for invalid base trans %d", iIndex);

	TransClearConnectionHandle(pServer, pTransaction, iIndex);

	switch (pTransaction->eType)
	{
	case TRANS_TYPE_SIMULTANEOUS_ATOMIC:
		ChangeBaseTransState(pServer, pTransaction, iIndex, BASETRANS_STATE_CANCELLED);
		CheckForSimulAtomicTransactionCompleteCancelling(pServer, pTransaction);
		break;



	case TRANS_TYPE_SEQUENTIAL_ATOMIC:
		ChangeBaseTransState(pServer, pTransaction, iIndex, BASETRANS_STATE_CANCELLED);
		if (pTransaction->iBaseTransactionsPerState[BASETRANS_STATE_CANCELLED] == pTransaction->iCurSequentialIndex)
		{
			if (pTransaction->iBaseTransactionsPerState[BASETRANS_STATE_FAILED])
			{
				GenerateTransactionResponse(pServer, pTransaction, &pTransaction->responseHandle, TRANSACTION_OUTCOME_FAILURE);
				CleanupTransaction(pServer, pTransaction);
			}
			else
			{
				ResetAtomicTransaction(pServer, pTransaction);
				AddBlockedTransaction(pServer, pTransaction);
			}
		}
		break;


	default:
		TransactionAssert(0, pTransaction, "Got cancel confirmed for non-atomic transaction");
	}

	
}



void HandleTransactionCancelConfirmed(TransactionServer *pServer, Packet *pPacket)
{
	Transaction *pTransaction;
	int iIndex;

	PERFINFO_AUTO_START_FUNC();

	pTransaction = GetTransactionFromIDIfExists(pServer, GetTransactionIDFromPacket(pPacket));
	iIndex = pktGetBitsPack(pPacket, 1);

	if (!pTransaction)
	{
		log_printf(LOG_TRANSSERVER, "SERIOUS WARNING: Unknown transaction cancel confirmed");
		PERFINFO_AUTO_STOP();
		return;
	}

	if (pTransaction->eBlockedStatus)
	{
		//transaction must have been being cancelled just as it was being blocked, just ignore
		PERFINFO_AUTO_STOP();
		return;
	}

	TransClearConnectionHandle(pServer, pTransaction, iIndex);

	HandleTransactionCancelConfirmed_Internal(pServer, pTransaction, iIndex);
	PERFINFO_AUTO_STOP();
}


void HandleTransactionPossible(TransactionServer *pServer, Packet *pPacket)
{
	Transaction *pTransaction;
	int iIndex;

	PERFINFO_AUTO_START_FUNC();

	pTransaction = GetTransactionFromIDIfExists(pServer, GetTransactionIDFromPacket(pPacket));
	iIndex = pktGetBitsPack(pPacket, 1);

	if (!pTransaction)
	{
		log_printf(LOG_TRANSSERVER, "SERIOUS WARNING: Unknown transaction possible");
		PERFINFO_AUTO_STOP();
		return;
	}

	TRANS_VERBOSE_LOG(pTransaction, TVL_STEP_POSSIBLE, NULL, 0, 0, iIndex);

	//we may have cancelled this transaction after sending the "is this possible" request, in which case we
	//may get a "yes it's possible" even though we're waiting for a "yes, it's cancelled". We may also have aborted the transaction
	//because the connection died while still having messages waiting from it, in which case the
	//transaction state will be FAILED
	if (pTransaction->pBaseTransactions[iIndex].eState == BASETRANS_STATE_CANCEL_SENT || pTransaction->pBaseTransactions[iIndex].eState == BASETRANS_STATE_FAILED)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	pTransaction->pBaseTransactions[iIndex].eOutcome = TRANSACTION_OUTCOME_SUCCESS;

	GetReturnValStringFromPacketIfAny(pServer, pTransaction, iIndex, pPacket);

	AddUpdateStringsFromPacketIfAny(pServer, pTransaction, iIndex, pPacket);

	pTransaction->pBaseTransactions[iIndex].iLocalTransactionManagerCallbackID = pktGetBits(pPacket, 32);

	TransactionAssert(pTransaction->pBaseTransactions[iIndex].eState == BASETRANS_STATE_INITQUERYSENT, pTransaction,
		"Got possible for base trans %d in wrong state", iIndex);

	switch (pTransaction->eType)
	{
	case TRANS_TYPE_SIMULTANEOUS_ATOMIC:
		ChangeBaseTransState(pServer, pTransaction, iIndex, BASETRANS_STATE_POSSIBLE_WAITFORCONFIRM);

		//two things to check for... if every single base transaction succeeded, the whole thing succeeded,
		//so lets send out messages to that effect. Otherwise, we should check if anything has blocked or failed, and if so,
		//turn right around and cancel ourself
		if (pTransaction->iBaseTransactionsPerState[BASETRANS_STATE_POSSIBLE_WAITFORCONFIRM] == pTransaction->iNumBaseTransactions)
		{
			int i;

			//first check if any of the objects this transaction applies to have moved. If they have, cancel the transaction
			for (i=0; i < pTransaction->iNumBaseTransactions; i++)
			{
				if (TransactionRecipientHasMoved(pServer, pTransaction, i))
				{
					BeginCancellingSimulAtomicTransaction(pServer, pTransaction);
					PERFINFO_AUTO_STOP();
					return;
				}
			}



			for (i=0; i < pTransaction->iNumBaseTransactions; i++)
			{
				if (SendSimpleMessage(pServer, pTransaction, i, TRANSSERVER_CONFIRM_TRANSACTION, 0))
				{
					ChangeBaseTransState(pServer, pTransaction, i, BASETRANS_STATE_CONFIRM_SENT);
				}
				else
				{
					ChangeBaseTransState(pServer, pTransaction, i, BASETRANS_STATE_CONFIRMED);
				}
			}

			ProcessAndSendUpdateStrings(pServer, pTransaction, "All base transactions in simul atomic trans waiting for confirm");

			if (pTransaction->iBaseTransactionsPerState[BASETRANS_STATE_CONFIRMED] == pTransaction->iNumBaseTransactions)
			{
				GenerateTransactionResponse(pServer, pTransaction, &pTransaction->responseHandle, TRANSACTION_OUTCOME_SUCCESS);
				CleanupTransaction(pServer, pTransaction);
			}

		}
		else if (pTransaction->iBaseTransactionsPerState[BASETRANS_STATE_FAILED] || pTransaction->iBaseTransactionsPerState[BASETRANS_STATE_BLOCKED])
		{
			if (SendSimpleMessage(pServer, pTransaction, iIndex, TRANSSERVER_CANCEL_TRANSACTION, TRANSSERVER_CANCEL_TRANSACTION))
			{
				ChangeBaseTransState(pServer, pTransaction, iIndex, BASETRANS_STATE_CANCEL_SENT);
			}
			else
			{
				ChangeBaseTransState(pServer, pTransaction, iIndex, BASETRANS_STATE_CANCELLED);
	
				if (pTransaction->iBaseTransactionsPerState[BASETRANS_STATE_CANCELLED] == pTransaction->iNumBaseTransactions)
				{
					GenerateTransactionResponse(pServer, pTransaction, &pTransaction->responseHandle, TRANSACTION_OUTCOME_FAILURE);
					CleanupTransaction(pServer, pTransaction);
				}
			}

		}
		break;

	case TRANS_TYPE_SEQUENTIAL_ATOMIC:
		TransactionAssert(iIndex == pTransaction->iCurSequentialIndex, pTransaction, "Got possible for wrong step %d", iIndex);
		ChangeBaseTransState(pServer, pTransaction, iIndex, BASETRANS_STATE_POSSIBLE_WAITFORCONFIRM);

		pTransaction->iCurSequentialIndex++;
		if (pTransaction->iCurSequentialIndex == pTransaction->iNumBaseTransactions)
		{
			int i;


			//first check if any of the objects this transaction applies to have moved. If they have, cancel the transaction
			for (i=0; i < pTransaction->iNumBaseTransactions; i++)
			{
				if (TransactionRecipientHasMoved(pServer, pTransaction, i))
				{
					BeginCancellingSequentialAtomicTrans(pServer, pTransaction);
					PERFINFO_AUTO_STOP();
					return;
				}
			}


			for (i=0; i < pTransaction->iNumBaseTransactions; i++)
			{
				if (SendSimpleMessage(pServer, pTransaction, i, TRANSSERVER_CONFIRM_TRANSACTION, 0))
				{
					ChangeBaseTransState(pServer, pTransaction, i, BASETRANS_STATE_CONFIRM_SENT);
				}
				else
				{
					ChangeBaseTransState(pServer, pTransaction, i, BASETRANS_STATE_CONFIRMED);
				}
			}

			ProcessAndSendUpdateStrings(pServer, pTransaction, "All base trans in sequential atomic trans complete");


			if (pTransaction->iBaseTransactionsPerState[BASETRANS_STATE_CONFIRMED] == pTransaction->iNumBaseTransactions)
			{
				GenerateTransactionResponse(pServer, pTransaction, &pTransaction->responseHandle, TRANSACTION_OUTCOME_SUCCESS);
				CleanupTransaction(pServer, pTransaction);
			}
		}
		else
		{
			if (SendNewTransactionMessage(pServer, pTransaction, pTransaction->iCurSequentialIndex, true, false))
			{
				ChangeBaseTransState(pServer, pTransaction, pTransaction->iCurSequentialIndex, BASETRANS_STATE_INITQUERYSENT);
			}
			else
			{
				GenerateObjectNotFoundError(&pTransaction->pBaseTransactions[pTransaction->iCurSequentialIndex].returnString,
					pTransaction, pTransaction->iCurSequentialIndex);
				ChangeBaseTransState(pServer, pTransaction, pTransaction->iCurSequentialIndex, BASETRANS_STATE_FAILED);
				BeginCancellingSequentialAtomicTrans(pServer, pTransaction);
			}
		}
		break;
	default:
		TransactionAssert(0, pTransaction, "Got possible for non-atomic trans");
	}
	PERFINFO_AUTO_STOP();
}

void HandleTransactionPossibleAndConfirmed(TransactionServer *pServer, Packet *pPacket)
{
	Transaction *pTransaction;
	int iIndex;

	PERFINFO_AUTO_START_FUNC();

	pTransaction = GetTransactionFromIDIfExists(pServer, GetTransactionIDFromPacket(pPacket));
	iIndex = pktGetBitsPack(pPacket, 1);

	if (!pTransaction)
	{
		log_printf(LOG_TRANSSERVER, "SERIOUS WARNING: Unknown transaction possible and confirmed");
		PERFINFO_AUTO_STOP();
		return;
	}

	TRANS_VERBOSE_LOG(pTransaction, TVL_STEP_POSSIBLE_AND_CONFIRMED, NULL, 0, 0, iIndex);

	pTransaction->pBaseTransactions[iIndex].eOutcome = TRANSACTION_OUTCOME_SUCCESS;

	GetReturnValStringFromPacketIfAny(pServer, pTransaction, iIndex, pPacket);

	AddUpdateStringsFromPacketIfAny(pServer, pTransaction, iIndex, pPacket);

	TransactionAssert(pTransaction->pBaseTransactions[iIndex].eState == BASETRANS_STATE_INITQUERYSENT, pTransaction,
		"Got PossibleAndConfirmed for base trans %d in wrong state", iIndex);
	TransactionAssert(pTransaction->iNumBaseTransactions == 1, pTransaction, "Got PossibleAndConfirmed for trans with more than 1 step");

	TransactionAssert(pTransaction->eType == TRANS_TYPE_SEQUENTIAL_ATOMIC || pTransaction->eType == TRANS_TYPE_SIMULTANEOUS_ATOMIC,
		pTransaction, "Got PossibleAndConfirmed for non-atomic trans");

	ProcessAndSendUpdateStrings(pServer, pTransaction, "Received transaction PossibleAndConfirmed");
	GenerateTransactionResponse(pServer, pTransaction, &pTransaction->responseHandle, TRANSACTION_OUTCOME_SUCCESS);
	CleanupTransaction(pServer, pTransaction);
	PERFINFO_AUTO_STOP();
}



void HandleTransactionSetVariable(TransactionServer *pServer, Packet *pPacket)
{
	Transaction *pTransaction;
	char *pKey;
	int iSize;

	PERFINFO_AUTO_START_FUNC();

	pTransaction = GetTransactionFromIDIfExists(pServer, GetTransactionIDFromPacket(pPacket));

	if (!pTransaction)
	{
		log_printf(LOG_TRANSSERVER, "SERIOUS WARNING: Unknown transaction sent variable");
		PERFINFO_AUTO_STOP();
		return;
	}



	TransactionAssert(pTransaction->eType == TRANS_TYPE_SEQUENTIAL_ATOMIC, pTransaction, 
		"Got SetVariable for non-sequentialAtomic trans");


	if (!pTransaction->transVariableTable)
	{
		pTransaction->transVariableTable = CreateNameTable(NULL);
	}
	pKey = pktGetStringTemp(pPacket);
	iSize = pktGetBits(pPacket, 32);

	NameTableAddBytes(pTransaction->transVariableTable, pKey, pktGetBytesTemp(pPacket, iSize), iSize);

	if (gbLogAllTransactions)
	{
		objLog(LOG_TRANSSERVER, GLOBALTYPE_NONE, 0, 0, NULL, NULL, NULL, "transVarSet", NULL, "name %s size %d",
			pTransaction->pTransactionName, iSize);
	
	}
	PERFINFO_AUTO_STOP();
}


void UpdateTransactionServer(TransactionServer *pServer)
{
	Transaction *pTransaction;

	PERFINFO_AUTO_START_FUNC();
	
	CheckCounts(pServer);

	//each frame, attempt to process all the blocked-by-no one transactions 
	perfMainTicks++;

	while ((pTransaction = GetNextBlockedByNothingTransaction(pServer)))
	{
		TRANS_VERBOSE_LOG(pTransaction, TVL_UNBLOCKED, NULL, 0, 0, 0);

		SendFirstMessagesForNewOrUnblockedTransaction(pServer, pTransaction);
	}
	
	if (ea32Size(&pServer->pTransactionsToVerboseLog))
	{
		U64 iCurTime = timeMsecsSince2000();
		U64 iTimeCutoff = iCurTime - siVerboseIncompleteLogCutoff * 1000;

		while (ea32Size(&pServer->pTransactionsToVerboseLog))
		{
			pTransaction = GetTransactionFromIDIfExists(pServer, pServer->pTransactionsToVerboseLog[0]);

			if (pTransaction)
			{
				if (pTransaction->iTimeBegan > iTimeCutoff)
				{
					break;
				}

				ea32Remove(&pServer->pTransactionsToVerboseLog, 0);
				OutputVerboseLogging(pTransaction, "incomplete", iCurTime, false);
			}
			else
			{
				ea32Remove(&pServer->pTransactionsToVerboseLog, 0);
			}
		}
	}

	PERFINFO_AUTO_STOP();
}

void InitTransactionServer(TransactionServer *pServer)
{
	int i;

	memset(pServer, 0, sizeof(TransactionServer));

	pServer->pTransactions = calloc(gMaxTransactions * sizeof(Transaction), 1);
	pServer->pConnections = calloc(gMaxLogicalConnections * sizeof(LogicalConnection), 1);
	pServer->pMultiplexConnections = calloc(gMaxMultiplexConnections * sizeof(MultiplexConnection), 1);

	pServer->iNextConnectionID = 1;


	for (i = 0; i < gMaxLogicalConnections; i++)
	{
		pServer->pConnections[i].iConnectionIndex = i;
		pServer->pConnections[i].iConnectionID = 0;
		pServer->pConnections[i].pNext = (i == gMaxLogicalConnections - 1 ? NULL : &pServer->pConnections[i+1]);
		pServer->pConnections[i].pPrev = (i == 0 ? NULL : &pServer->pConnections[i-1]);
	}

	pServer->pFirstActive = NULL;
	pServer->pFirstFree = &pServer->pConnections[0];

	pServer->iNumActiveConnections = 0;

	for (i=0; i < GLOBALTYPE_MAXTYPES; i++)
	{
		pServer->objectDirectories[i].iDefaultConnectionIndex = -1;
		pServer->objectDirectories[i].directory = stashTableCreateInt( 100);
	}

	pServer->iDatabaseConnectionIndex = -1;

	pServer->pCompletionTimeAverager = IntAverager_Create(AVERAGE_DAY);
	pServer->pBytesPerTransactionAverager = IntAverager_Create(AVERAGE_DAY);
	pServer->pAllTransactionCounter = CountAverager_Create(AVERAGE_DAY);
	pServer->pBlockedTransactionCounter = CountAverager_Create(AVERAGE_DAY);

	pServer->pNextEmptyTransaction = &pServer->pTransactions[0];
	pServer->pLastEmptyTransaction = &pServer->pTransactions[gMaxTransactions-1];
	for (i=0; i < gMaxTransactions; i++)
	{
		pServer->pTransactions[i].iID = (i == 0  ? gMaxTransactions : i);
		pServer->pTransactions[i].pNextBlocked = (i == gMaxTransactions-1 ? NULL : &pServer->pTransactions[i+1]);
	}

	MP_CREATE(TransVerboseLogEntry, 256);

	pServer->sTrackersByTransName = stashTableCreateAddress(128);
	
	resRegisterDictionaryForStashTable("TransactionTrackers", RESCATEGORY_SYSTEM, 0, pServer->sTrackersByTransName, parse_TransactionTracker);
}

LogicalConnection *GetAndActivateNewLogicalConnection(TransactionServer *pServer)
{
	LogicalConnection *pConnection;
		
	if (!pServer->pFirstFree)
	{
		return NULL;
	}

	pConnection = pServer->pFirstFree;

	//unlink from free list
	pServer->pFirstFree = pConnection->pNext;
	if (pConnection->pNext)
	{
		pConnection->pNext->pPrev = NULL;
	}

	//add to active list
	if (pServer->pFirstActive)
	{
		pServer->pFirstActive->pPrev = pConnection;
	}
	pConnection->pNext = pServer->pFirstActive;
	pServer->pFirstActive = pConnection;
	pConnection->pPrev = NULL;

	pConnection->iConnectionID = pServer->iNextConnectionID;
	pServer->iNextConnectionID++;
	if (pServer->iNextConnectionID == 0)
	{
		pServer->iNextConnectionID++;
	}

	pServer->iNumActiveConnections++;

	pConnection->bAlreadyAbortedAll = false;

	if (!pConnection->transTable)
	{
		pConnection->transTable = stashTableCreateInt(32);
	}
	else
	{
		stashTableClear(pConnection->transTable);
	}

	return pConnection;
}

void ReturnActiveConnectionToFreeList(TransactionServer *pServer, LogicalConnection *pConnection)
{
	assert(pConnection->iConnectionID != 0);
	assert(pConnection == &pServer->pConnections[pConnection->iConnectionIndex]);

	//unlink connection from active list

	if (pConnection->pNext)
	{
		pConnection->pNext->pPrev = pConnection->pPrev;
	}
	if (pConnection->pPrev)
	{
		pConnection->pPrev->pNext = pConnection->pNext;
	}
	else
	{
		assert(pConnection == pServer->pFirstActive);
		pServer->pFirstActive = pConnection->pNext;
	}

	//put connection in free list
	pConnection->pNext = pServer->pFirstFree;
	pServer->pFirstFree = pConnection;

	pConnection->iConnectionID = 0;

	pServer->iNumActiveConnections--;
}

void GenerateConnectionResult(NetLink *pLink, enumTransServerConnectResult eResult)
{
	Packet *pak;
	
	pktCreateWithCachedTracker(pak, pLink, TRANSSERVER_CONNECTION_RESULT);
	pktSendBitsPack(pak, 1, eResult);
	pktSend(&pak);
}

void GenerateMultiplexedConnectionResult(NetLink *pLink, int iMultiplexIndex, enumTransServerConnectResult eResult)
{
	static PacketTracker *pTracker;
	Packet *pak;

	ONCE(pTracker = PacketTrackerFind("GenerateMultiplexedConnectionResult", 0, "TRANSSERVER_CONNECTION_RESULT"));
		
	pak = CreateMultiplexedNetLinkListPacket(pLink, iMultiplexIndex, TRANSSERVER_CONNECTION_RESULT, pTracker);
	pktSendBitsPack(pak, 1, eResult);
	pktSend(&pak);
}


void HandleNormalConnectionRegisterClientInfo(TransactionServer *pServer, Packet *pPacket, NetLink *pLink, TransactionServerClientLink *client)
{
	LogicalConnection *pConnection;


	GlobalType eServerType;
	int iServerID;
	int iCookie;
	char *pVersionString;

	PERFINFO_AUTO_START_FUNC();

	eServerType = pktGetBitsPack(pPacket, 1);
	iServerID = pktGetBitsPack(pPacket, 1);
	iCookie = pktGetBitsPack(pPacket, 1);
	pVersionString = pktGetStringTemp(pPacket);

	linkSetDebugName(pLink, STACK_SPRINTF("Trans server link to %s", GlobalTypeAndIDToString(eServerType, iServerID)));
	
	if (GlobalTypeIsCriticallyImportant(eServerType))
	{
		linkSetType(pLink, LINKTYPE_SHARD_CRITICAL_10MEG);
	}
	else if (GlobalServerTypeIsLowImportance(eServerType))
	{
		linkSetType(pLink, LINKTYPE_SHARD_NONCRITICAL_20MEG);
	}
	else
	{
		linkSetType(pLink, LINKTYPE_SHARD_NONCRITICAL_ALERTS_100MEG);		
	}

	// Debug: If requested, replay ObjectDB session from a file, instead of doing normal processing.
	if (*sObjectDBReplayFromFile && eServerType == GLOBALTYPE_OBJECTDB)
	{
		setWindowIconColoredLetter(compatibleGetConsoleWindow(), 'T', 0xff0000);
		setConsoleTitle("TransServer - Replaying transaction capture");
		TransactionServerSetCaptureReplayLink(pLink);
		return;
	}

	if (isProductionMode() && strcmp(pVersionString, GetUsefulVersionString()) != 0)
	{
		char *pAlertString = NULL;
		
		if (ControllerReportsAllowShardVersionMismatch())
		{
			if (!VersionMismatchAlreadyReported(pVersionString, eServerType))
			{
				VersionMismatchReported(pVersionString, eServerType);
				estrPrintf(&pAlertString, "Server %s(%u)(IP %s) trying to connect to trans server with version \"%s\", which doesn't match trans server version \"%s\", ALLOWING (will alert only once per version and type). This is expected behavior if this server type was frankenbuilt.", 
					GlobalTypeToName(eServerType), iServerID, makeIpStr(linkGetIp(pLink)),
					pVersionString, GetUsefulVersionString());
				TriggerAlert(ALERTKEY_VERSIONMISMATCH, pAlertString, ALERTLEVEL_WARNING, ALERTCATEGORY_NETOPS, 0, GLOBALTYPE_TRANSACTIONSERVER, gServerLibState.containerID,
					eServerType, iServerID, getHostName(), 0);
				estrDestroy(&pAlertString);
			}
		}
		else
		{
			estrPrintf(&pAlertString, "Server %s(%u)(IP %s) trying to connect to trans server with version \"%s\", which doesn't match trans server version \"%s\", REJECTING", 
				GlobalTypeToName(eServerType), iServerID, makeIpStr(linkGetIp(pLink)),
				pVersionString, GetUsefulVersionString());
			TriggerAlert(ALERTKEY_VERSIONMISMATCH_REJECT, pAlertString, ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, GLOBALTYPE_TRANSACTIONSERVER, gServerLibState.containerID,
				eServerType, iServerID, getHostName(), 0);
			estrDestroy(&pAlertString);
			
			GenerateConnectionResult(pLink, TRANS_SERVER_CONNECT_RESULT_FAILURE_VERSION_MISMATCH);
			PERFINFO_AUTO_STOP();
			return;
		}



	}




	if (iCookie != gServerLibState.antiZombificationCookie)
	{
		GenerateConnectionResult(pLink, TRANS_SERVER_CONNECT_RESULT_FAILURE_ANTIZOMBIFICATIONCOOKIE_MISMATCH);
		PERFINFO_AUTO_STOP();
		return;
	}

	if (IsTypeMasterObjectDB(eServerType))
	{
		if (pServer->iDatabaseConnectionIndex != -1)
		{
			GenerateConnectionResult(pLink, TRANS_SERVER_CONNECT_RESULT_FAILURE_OBJECTDBALREADYCONNECTED);
			PERFINFO_AUTO_STOP();
			return;
		}
	}

	pConnection = pServer->pFirstActive;

	while (pConnection)
	{
		if (pConnection->eServerType == eServerType && pConnection->iServerID == iServerID)
		{
			GenerateConnectionResult(pLink, TRANS_SERVER_CONNECT_RESULT_FAILURE_SERVERIDNOTUNIQUE);
			PERFINFO_AUTO_STOP();
			return;
		}

		pConnection = pConnection->pNext;
	}


	assert(client->iIndexOfLogicalConnection == -1);
	


	pConnection = GetAndActivateNewLogicalConnection(pServer);

	if (!pConnection)
	{
		AssertOrAlert("TOO_MANY_TS_CONNECTIONS", "Transaction server has too many logical connections and is refusing a connection from a %s. This may or may not be fatal. Is bigmode not on?",
			GlobalTypeToName(eServerType));
		GenerateConnectionResult(pLink, TRANS_SERVER_CONNECT_RESULT_TOO_MANY_CONNECTIONS);
		return;
	}




	client->iIndexOfLogicalConnection = pConnection->iConnectionIndex;

	pConnection->pNetLink = pLink;
	pConnection->iMultiplexConnectionIndex = -1;

	pConnection->eServerType = eServerType;
	pConnection->iServerID = iServerID;

	if (IsTypeMasterObjectDB(pConnection->eServerType))
	{
		printf("Object DB connected\n");
		pServer->iDatabaseConnectionIndex = pConnection->iConnectionIndex;
		linkSetTimeout(pLink, 0);
		linkSetMaxRecvSize(pLink, 8 * 1024 * 1024);
	}
	else if (pConnection->eServerType == GLOBALTYPE_CONTROLLER)
	{
		linkSetMaxRecvSize(pLink, 128 * 1024);
		linkSetTimeout(pLink, 0);
	}
	else
	{
		linkSetMaxRecvSize(pLink, 128 * 1024);
	}




	GenerateConnectionResult(pLink, TRANS_SERVER_CONNECT_RESULT_SUCCESS);

	PERFINFO_AUTO_STOP();
}



void HandleMultiplexConnectionRegisterClientInfo(TransactionServer *pServer, Packet *pPacket, NetLink *pLink, int iSenderMultiplexIndexFull, TransactionServerClientLink *client)
{
	LogicalConnection *pConnection;

	GlobalType eServerType;
	int iServerID;

	int iMultiplexConnectionIndex = client->iIndexOfMultiplexConnection;

	MultiplexConnection *pMultiplexConnection = &pServer->pMultiplexConnections[iMultiplexConnectionIndex];
	char *pVersionString;

	int iCookie;

	int iInternalIndex = MULTIPLEXER_GET_REAL_CONNECTION_INDEX(iSenderMultiplexIndexFull);

	PERFINFO_AUTO_START_FUNC();

	eServerType = pktGetBitsPack(pPacket, 1);
	iServerID = pktGetBitsPack(pPacket, 1);
	iCookie = pktGetBitsPack(pPacket, 1);

	if (iCookie != gServerLibState.antiZombificationCookie)
	{
		GenerateMultiplexedConnectionResult(pLink, iSenderMultiplexIndexFull, TRANS_SERVER_CONNECT_RESULT_FAILURE_ANTIZOMBIFICATIONCOOKIE_MISMATCH);
		PERFINFO_AUTO_STOP();
		return;
	}


	pVersionString = pktGetStringTemp(pPacket);


	if (isProductionMode() && strcmp(pVersionString, GetUsefulVersionString()) != 0)
	{
		char *pAlertString = NULL;
		if (ControllerReportsAllowShardVersionMismatch())
		{
			if (!VersionMismatchAlreadyReported(pVersionString, eServerType))
			{
				VersionMismatchReported(pVersionString, eServerType);
				estrPrintf(&pAlertString, "Server %s(%u)(IP %s) trying to connect to trans server with version \"%s\", which doesn't match trans server version \"%s\", ALLOWING (will alert only once per version and type). This is expected behavior if this server type was frankenbuilt.", 
					GlobalTypeToName(eServerType), iServerID, makeIpStr(linkGetIp(pLink)),
					pVersionString, GetUsefulVersionString());
				TriggerAlert(ALERTKEY_VERSIONMISMATCH, pAlertString, ALERTLEVEL_WARNING, ALERTCATEGORY_NETOPS, 0, GLOBALTYPE_TRANSACTIONSERVER, gServerLibState.containerID,
					eServerType, iServerID, getHostName(), 0);
				estrDestroy(&pAlertString);
			}
		}
		else
		{
			estrPrintf(&pAlertString, "Server %s(%u)(IP %s) trying to connect to trans server with version \"%s\", which doesn't match trans server version \"%s\", REJECTING", 
				GlobalTypeToName(eServerType), iServerID, makeIpStr(linkGetIp(pLink)),
				pVersionString, GetUsefulVersionString());
			TriggerAlert(ALERTKEY_VERSIONMISMATCH_REJECT, pAlertString, ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, GLOBALTYPE_TRANSACTIONSERVER, gServerLibState.containerID,
				eServerType, iServerID, getHostName(), 0);
			estrDestroy(&pAlertString);
			
			GenerateMultiplexedConnectionResult(pLink, iSenderMultiplexIndexFull, TRANS_SERVER_CONNECT_RESULT_FAILURE_VERSION_MISMATCH);
			PERFINFO_AUTO_STOP();
			return;
		}
	}

	if (iMultiplexConnectionIndex == -1)
	{
		//this is our first client registering over this multiplex client link... find an unused MultiplexConnection

		int i;

		for (i = 0; i < gMaxMultiplexConnections; i++)
		{
			if (pServer->pMultiplexConnections[i].pNetLink == NULL)
			{
				iMultiplexConnectionIndex = i;
				break;
			}
		}

		if (i == gMaxMultiplexConnections)
		{
			GenerateMultiplexedConnectionResult(pLink, iSenderMultiplexIndexFull, TRANS_SERVER_CONNECT_RESULT_FAILURE_TOOMANYMULTIPLEXERS);
			PERFINFO_AUTO_STOP();
			return;
		}
		
		iMultiplexConnectionIndex = i;
		pMultiplexConnection = &pServer->pMultiplexConnections[iMultiplexConnectionIndex];
		client->iIndexOfMultiplexConnection = iMultiplexConnectionIndex;

		pMultiplexConnection->piLogicalIndexesOfMultiplexConnections = NULL;
		pMultiplexConnection->iMaxConnectionIndex = -1;
		pMultiplexConnection->pNetLink = pLink;

		linkSetDebugName(pLink, STACK_SPRINTF("Multiplexed connection to %s", makeIpStr(linkGetIp(pLink))));
	}

	if (pMultiplexConnection->iMaxConnectionIndex >= iInternalIndex)
	{
		assert(pMultiplexConnection->piLogicalIndexesOfMultiplexConnections);

		if (pMultiplexConnection->piLogicalIndexesOfMultiplexConnections[iInternalIndex] != -1)
		{		
			GenerateMultiplexedConnectionResult(pLink, iSenderMultiplexIndexFull, TRANS_SERVER_CONNECT_RESULT_FAILURE_MULTIPLEXIDINUSE);
			PERFINFO_AUTO_STOP();
			return;
		}
	}




	if (IsTypeMasterObjectDB(eServerType))
	{
		if (pServer->iDatabaseConnectionIndex != -1)
		{
			GenerateMultiplexedConnectionResult(pLink, iSenderMultiplexIndexFull, TRANS_SERVER_CONNECT_RESULT_FAILURE_OBJECTDBALREADYCONNECTED);
			PERFINFO_AUTO_STOP();
			return;
		}
	}

	pConnection = pServer->pFirstActive;

	while (pConnection)
	{
		if (pConnection->eServerType == eServerType && pConnection->iServerID == iServerID)
		{
			GenerateMultiplexedConnectionResult(pLink, iSenderMultiplexIndexFull, TRANS_SERVER_CONNECT_RESULT_FAILURE_SERVERIDNOTUNIQUE);
			PERFINFO_AUTO_STOP();
			return;
		}

		pConnection = pConnection->pNext;
	}



	pConnection = GetAndActivateNewLogicalConnection(pServer);

	if (!pConnection)
	{
		AssertOrAlert("TOO_MANY_TS_CONNECTIONS", "Transaction server has too many logical connections and is refusing a connection from a %s. This may or may not be fatal. Is bigmode not on?",
			GlobalTypeToName(eServerType));
		GenerateConnectionResult(pLink, TRANS_SERVER_CONNECT_RESULT_TOO_MANY_CONNECTIONS);
		return;
	}

	pConnection->pNetLink = pLink;
	pConnection->iMultiplexConnectionIndex = iSenderMultiplexIndexFull;

	pConnection->eServerType = eServerType;
	pConnection->iServerID = iServerID;

	if (IsTypeMasterObjectDB(pConnection->eServerType))
	{
		pServer->iDatabaseConnectionIndex = pConnection->iConnectionIndex;
	}

	if (pMultiplexConnection->iMaxConnectionIndex >= iInternalIndex)
	{
		assert(pMultiplexConnection->piLogicalIndexesOfMultiplexConnections);
		pMultiplexConnection->piLogicalIndexesOfMultiplexConnections[iInternalIndex] = pConnection->iConnectionIndex;
	}
	else
	{
		int iNewMax = iInternalIndex + NUM_EXTRA_MULTIPLEX_INDICES_TO_ALLOCATE_AT_ONCE;
		int *pNewIndices = malloc(sizeof(int) * (iNewMax + 1));
		int i;

		for (i=0; i <= iNewMax; i++)
		{
			pNewIndices[i] = -1;
		}

		if (pMultiplexConnection->piLogicalIndexesOfMultiplexConnections)
		{
			memcpy(pNewIndices, pMultiplexConnection->piLogicalIndexesOfMultiplexConnections, (pMultiplexConnection->iMaxConnectionIndex + 1) * sizeof(int));

			free(pMultiplexConnection->piLogicalIndexesOfMultiplexConnections);
		}

		pMultiplexConnection->piLogicalIndexesOfMultiplexConnections = pNewIndices;

		pMultiplexConnection->piLogicalIndexesOfMultiplexConnections[iInternalIndex] = pConnection->iConnectionIndex;

		pMultiplexConnection->iMaxConnectionIndex = iNewMax;
	}
	
	GenerateMultiplexedConnectionResult(pLink, iSenderMultiplexIndexFull, TRANS_SERVER_CONNECT_RESULT_SUCCESS);


	PERFINFO_AUTO_STOP();
}





void AbortBaseTransaction(TransactionServer *pServer, Transaction *pTransaction, int iIndex, const char *pReason)
{

/*	printf("AbortBaseTransactioncalled on trans %d step %d (%s) currently in state %s\n",
		pTransaction->iID, iIndex, pTransaction->pBaseTransactions[iIndex].transaction.pData,
		StaticDefineIntRevLookup(enumBaseTransactionStateEnum, pTransaction->pBaseTransactions[iIndex].eState));
*/

	TRANS_VERBOSE_LOG(pTransaction, TVL_STEP_ABORTED, pReason, 0, 0, iIndex);


	switch(pTransaction->pBaseTransactions[iIndex].eState)
	{
	case BASETRANS_STATE_INITQUERYSENT:
		FailBaseTransactionFromInitQueryState(pServer, pTransaction, iIndex);
		break;

	case BASETRANS_STATE_POSSIBLE_WAITFORCONFIRM:
		//do nothing. Just check that when we send out this confirm or cancel message, if we can't send it, we just immediately
		//go to confirmed or cancelled state
		TransClearConnectionHandle(pServer, pTransaction, iIndex);
		break;
	
	
	case BASETRANS_STATE_CONFIRM_SENT:
		HandleAtomicTransactionSucceeded_Internal(pServer, pTransaction, iIndex);
		break;
		

	case BASETRANS_STATE_CANCEL_SENT:
		HandleTransactionCancelConfirmed_Internal(pServer, pTransaction, iIndex);
		break;
	}

}
void MultiplexConnectionDied(TransactionServer *pServer, int iMultiplexConnectionIndex, const char *pReason)
{	
	MultiplexConnection *pConnection;

	char *pInnerReason = NULL;

	if (iMultiplexConnectionIndex == -1)
	{
		return;
	}

	estrPrintf(&pInnerReason, "Multiplex connection %d died. Reason: %s", iMultiplexConnectionIndex, pReason);

	pConnection = &pServer->pMultiplexConnections[iMultiplexConnectionIndex];

	if (pConnection->piLogicalIndexesOfMultiplexConnections)
	{
		int i;

		for (i=0 ; i <= pConnection->iMaxConnectionIndex; i++)
		{
			LogicalConnectionDied(pServer, pConnection->piLogicalIndexesOfMultiplexConnections[i], false, pInnerReason);
		}

		free(pConnection->piLogicalIndexesOfMultiplexConnections);
	}

	pConnection->pNetLink = NULL;

	estrDestroy(&pInnerReason);
}

void ConnectionFromMultiplexConnectionDied(TransactionServer *pServer, int iMultiplexConnectionIndex, int iIndexOfServerFull, const char *pReason)
{
	char *pInnerReason = NULL;
	int iInternalIndex = MULTIPLEXER_GET_REAL_CONNECTION_INDEX(iIndexOfServerFull);

	//this might be a connection that we don't care about, ie, a connection from a game server to a
	//log server. If so, we ignore it
	if (iMultiplexConnectionIndex == -1 || iInternalIndex > pServer->pMultiplexConnections[iMultiplexConnectionIndex].iMaxConnectionIndex 
		|| pServer->pMultiplexConnections[iMultiplexConnectionIndex].piLogicalIndexesOfMultiplexConnections[iInternalIndex] == -1)
	{
		return;
	}

	estrPrintf(&pInnerReason, "ConnectionFromMultiplexConnectionDied. Reason: %s", pReason);

	assert(pServer->pMultiplexConnections[iMultiplexConnectionIndex].pNetLink);

	LogicalConnectionDied(pServer, pServer->pMultiplexConnections[iMultiplexConnectionIndex].piLogicalIndexesOfMultiplexConnections[iInternalIndex], false, pInnerReason);

	pServer->pMultiplexConnections[iMultiplexConnectionIndex].piLogicalIndexesOfMultiplexConnections[iInternalIndex] = -1;

	estrDestroy(&pInnerReason);
}


#define DISCONNECT_DONE() { float fSecs = timerSeconds64(timerCpuTicks64() - iStartTime); if (fSecs > 0.25f ) { ErrorOrAlert("SLOW_TRANS_DISCONNECT", "Trans server took %f seconds to disconnect from %s[%u] because of %s", fSecs, GlobalTypeToName(pConnection->eServerType), pConnection->iServerID, pDisconnectReason); }}



void LogicalConnectionDied(TransactionServer *pServer, int iLogicalConnectionIndex, bool bNeedToKillConnection, const char *pDisconnectReason)
{
	int i;

	int iDyingConnectionID;

	LogicalConnection *pConnection;
	U64 iStartTime;

	static char *pComment = NULL;


	if (iLogicalConnectionIndex == -1)
	{
		return;
	}

	iStartTime = timerCpuTicks64();


	pConnection = &pServer->pConnections[iLogicalConnectionIndex];
	iDyingConnectionID = pConnection->iConnectionID;

	estrPrintf(&pComment, "Connection %d to %s[%u] died. Reason: %s", iLogicalConnectionIndex, 
		GlobalTypeToName(pConnection->eServerType), pConnection->iServerID, pDisconnectReason);

	log_printf(LOG_TRANSSERVER, "%s", pComment);


	//need to abort transactions before we do anything to the connection. Otherwise the cancel might
	//get sent to the wrong place, or something else bad of that sort.
	if (!pConnection->bAlreadyAbortedAll)
	{
		StashTableIterator stashIterator;
		StashElement element;

		pConnection->bAlreadyAbortedAll = true;
		gbNoVerboseLogging = true;

		//need to iterate through all transactions in our transTable and possible abort them
		stashGetIterator(pConnection->transTable, &stashIterator);

		while (stashGetNextElement(&stashIterator, &element))
		{
			U32 iID = stashElementGetU32Key(element);

			Transaction *pTransaction = GetTransactionFromIDIfExists(pServer, iID);

			if (pTransaction)
			{
				int j;

				for (j=0; j < pTransaction->iNumBaseTransactions; j++)
				{
					if (pTransaction->pBaseTransactions[j].transConnectionHandle.iID == iDyingConnectionID)
					{
						AbortBaseTransaction(pServer, pTransaction, j, "ConnectionDied");
						
						//check if aborting the base transaction caused the entire transaction to be finished
						if (pTransaction->eType == TRANS_TYPE_NONE)
						{
							break;
						}
					}
				}
			}

		}
	
/*
		for (i=0; i < gMaxTransactions; i++)
		{
			if (pServer->pTransactions[i].eType != TRANS_TYPE_NONE)
			{
				int j;

				for (j=0; j < pServer->pTransactions[i].iNumBaseTransactions; j++)
				{
					if (pServer->pTransactions[i].pBaseTransactions[j].transConnectionHandle.iID == iDyingConnectionID)
					{
						AbortBaseTransaction(pServer, &pServer->pTransactions[i], j);
						
						//check if aborting the base transaction caused the entire transaction to be finished
						if (pServer->pTransactions[i].eType == TRANS_TYPE_NONE)
						{
							break;
						}
					}
				}
			}
		}*/

		gbNoVerboseLogging = false;


	}

	if (bNeedToKillConnection)
	{
		//if bNeedToKillConnection is set and it's not a muliplex connection, we just remove the connection. That will
		//retrigger this function with bNeedToKillConnection false
		if (pConnection->iMultiplexConnectionIndex == -1)
		{
			linkRemove(&pConnection->pNetLink);
			DISCONNECT_DONE();
			return;
		}
		else
		{
			TransactionServerClientLink *client = linkGetUserData(pConnection->pNetLink);

			if (client->iIndexOfMultiplexConnection != -1)
			{
				pServer->pMultiplexConnections[client->iIndexOfMultiplexConnection].piLogicalIndexesOfMultiplexConnections[MULTIPLEXER_GET_REAL_CONNECTION_INDEX(pConnection->iMultiplexConnectionIndex)] = -1;
			}
		}
	}

	assert(pConnection->iConnectionID != 0);

	
	if (IsTypeMasterObjectDB(pConnection->eServerType))
	{
		if (pServer->iDatabaseConnectionIndex == iLogicalConnectionIndex)
		{
			pServer->iDatabaseConnectionIndex = -1;
		}

		//if the shard is in the middle of shutting down, we don't want to randomly generate an assert here, so 
		//we wait for a few seconds before asserting.
		TimedCallback_Run(assertTimedCallback, (UserData)allocAddString(STACK_SPRINTF("Object DB Disconnected. Reason: %s", pDisconnectReason)), 5.0f);
		
	}


	
	ReturnActiveConnectionToFreeList(pServer, pConnection);


	for (i=0; i < GLOBALTYPE_MAXTYPES; i++)
	{
	

		StashTableIterator iterator;
		StashElement element;

		if (pServer->objectDirectories[i].iDefaultConnectionIndex == iLogicalConnectionIndex)
		{
			pServer->objectDirectories[i].iDefaultConnectionIndex = -1;
		}


		stashGetIterator(pServer->objectDirectories[i].directory, &iterator);

		do
		{
			if (!stashGetNextElement(&iterator, &element))
			{
				break;
			}

			if (stashElementGetInt(element) == iLogicalConnectionIndex)
			{
				int dummy;
				if (GlobalTypeSchemaType(i) == SCHEMATYPE_PERSISTED && pServer->iDatabaseConnectionIndex >= 0)
				{
					// If a persisted container goes offline, the owning Database needs to know
					// Fix this code if we support multiple databases
					char queryString[1024];
					LogicalConnection* dbConnection = &pServer->pConnections[pServer->iDatabaseConnectionIndex];
					sprintf(queryString,"dbUpdateContainerOwner %d %d %d %d",i,stashElementGetIntKey(element),
						dbConnection->eServerType,dbConnection->iServerID);
					SendSimpleDBUpdateString(pServer, NULL, queryString, pComment);
				}
				
				stashIntRemoveInt(pServer->objectDirectories[i].directory, stashElementGetIntKey(element), &dummy);
			}
		} while (1);
	}


	DISCONNECT_DONE();
	
}

int GetConnectionNumFromServerTypeAndID(TransactionServer *pServer, GlobalType eServerType, int iServerID)
{
	LogicalConnection *pConnection = pServer->pFirstActive;

	while (pConnection)
	{
		if (pConnection->eServerType == eServerType && pConnection->iServerID == iServerID)
		{
			return pConnection->iConnectionIndex;
		}

		pConnection = pConnection->pNext;
	}

	return -1;
}



void HandleControllerInformedUsOfServerDeath(TransactionServer *pServer, Packet *pPacket)
{
	GlobalType eServerType = GetContainerTypeFromPacket(pPacket);
	ContainerID iServerID = GetContainerIDFromPacket(pPacket);

	int iConnectionNum = GetConnectionNumFromServerTypeAndID(pServer, eServerType, iServerID);
	char *pReason = pktGetStringTemp(pPacket);
	
	if (iConnectionNum == -1)
	{
		return;
	}


	LogicalConnectionDied(pServer, iConnectionNum, true, STACK_SPRINTF("Informed by controller: %s", pReason));



}
void TransServerAuxControllerMessageHandler(Packet *pkt,int cmd,NetLink* link,void *user_data)
{
	switch (cmd)
	{
//special case... the controller informs the transaction server that a server has crashed
//or asserted, so the transaction server can disconnect all logical connections to it, even 
//though the tcp link might not die
		xcase FROM_CONTROLLER__SERVERSPECIFIC__SERVER_CRASHED:
			HandleControllerInformedUsOfServerDeath(&gTransactionServer, pkt);
	}
}


#define MAX_TRANSACTION_SERVER_COMMAND_STRING_TOKENS 7
#define MAX_TRANSACTION_SERVER_COMMAND_STRING_TOKEN_LENGTH 128

#define TSC_ASSERT(condition) { if (!(condition)) { TscAssert(tokens, token_count, #condition, ""); PERFINFO_AUTO_STOP(); return; }}
#define TSC_ASSERTMSG(condition, message, ...) { if (!(condition)) { TscAssert(tokens, token_count, #condition, message, __VA_ARGS__); PERFINFO_AUTO_STOP(); return; }}

void TscAssert(char **ppTokens, int iNumTokens, char *pConditionString, char *pMessage, ...)
{
	char *pFullInMessage = NULL;
	char *pFullTokenString = NULL;
	char *pFullErrorString = NULL;
	int i;

	va_list ap;
	va_start(ap, pMessage);
	estrConcatfv(&pFullInMessage, pMessage, ap);
	va_end(ap);

	for (i=0; i < iNumTokens; i++)
	{
		estrConcatf(&pFullTokenString, "%s%s", i == 0 ? "" : " ", ppTokens[i]);
	}

	estrPrintf(&pFullErrorString, "TRANS SERVER COMMAND ERROR! This is very serious. Talk to Ben or Alex or Kelvin. Error \"%s\", condition \"%s\", occurred while processing string \"%s\"",
		pFullInMessage, pConditionString, pFullTokenString);

	AssertOrAlert("TSC_ASSERT", "%s", pFullErrorString);

	objLog(LOG_TRANSSERVER, GLOBALTYPE_NONE, 0, 0, NULL, NULL, NULL, "TSC", NULL, "FAILED: %s", pFullErrorString);


	estrDestroy(&pFullInMessage);
	estrDestroy(&pFullTokenString);
	estrDestroy(&pFullErrorString);
}


void ProcessTransactionServerCommandString(TransactionServer *pServer, char *pString, char *pComment)
{
	char *pFirstDivider;
	char *pTemp;

	PERFINFO_AUTO_START_FUNC();

	verbose_printf("Command: %s\n",pString);

//	objLog(LOG_TRANSSERVER, GLOBALTYPE_NONE, 0, 0, NULL, NULL, NULL, "TSC", NULL, "%s: %s", pComment, pString);

	while (pString)
	{
		char *tokens[MAX_TRANSACTION_SERVER_COMMAND_STRING_TOKENS] = {0};
		char *pOldString = pString;
		int token_count;
		int i;

		pFirstDivider = strstr(pString, "$$");
		if (pFirstDivider)
		{
			pString = pFirstDivider + 2;
			*pFirstDivider = 0;
		}
		else
		{
			pString = NULL;
		}

		token_count = tokenize_line_safe(pOldString, tokens, ARRAY_SIZE(tokens), NULL);
		for (i=0; i<token_count; i++) 
			assert(tokens[i]);

		if (token_count > 4 && strcmp(tokens[0], TRANSACTIONSERVER_COMMAND_ONLINE) == 0)
		{
			GlobalType eObjectType = NameToGlobalType(tokens[1]);
			GlobalType eServerType = NameToGlobalType(tokens[3]);
			int iServerID;
			int iConnectionNum;
			int iObjectID;
			int iCurConnectionNum;

			TSC_ASSERT(eObjectType && eObjectType < GLOBALTYPE_MAXTYPES);
			TSC_ASSERT(eServerType && eServerType < GLOBALTYPE_MAXTYPES);

			errno = 0;
			iServerID = strtol(tokens[4], &pTemp, 10);

			TSC_ASSERT(errno == 0);
			if (iServerID == 0)
			{
				iServerID = FindFirstExtantIDOfServerType(pServer, eServerType);
				if (!iServerID)
				{
					Errorf("While processing TRANSACTIONSERVER_COMMAND_ONLINE, couldn't find a server of type %s for ID 0", 
						GlobalTypeToName(eServerType));
				}
			}

			TSC_ASSERT(iServerID);

			iConnectionNum = GetConnectionNumFromServerTypeAndID(pServer, eServerType, iServerID);

			TSC_ASSERT(iConnectionNum != -1);

			iObjectID = atoi(tokens[2]);

			TSC_ASSERT(iObjectID);

			if (stashIntFindInt(pServer->objectDirectories[eObjectType].directory, iObjectID, &iCurConnectionNum))
			{
				// allow duplicate onlines, as long as they're the same
				TSC_ASSERT(iCurConnectionNum == iConnectionNum);
				continue;
			}

			stashIntAddInt(pServer->objectDirectories[eObjectType].directory, iObjectID, iConnectionNum, true);			
		}
		else if (token_count > 5 && strcmp(tokens[0], TRANSACTIONSERVER_COMMAND_REGISTER) == 0)
		{
			char eObjectTypeName[GLOBALTYPE_MAXSCHEMALEN];
			GlobalType eObjectType = atoi(tokens[2]);
			GlobalType eServerType = NameToGlobalType(tokens[4]);
			int iServerID;
			int iConnectionNum;
			SchemaType eSchemaType;

			strcpy(eObjectTypeName,tokens[1]);

			if (stricmp(tokens[3],"persisted") == 0)
			{
				eSchemaType = SCHEMATYPE_PERSISTED;
			}
			else if(stricmp(tokens[3],"transacted") == 0)
			{
				eSchemaType = SCHEMATYPE_TRANSACTED;
			}
			else
			{
				TSC_ASSERTMSG(0, "REGISTER expects either persisted or transacted");
			}

			if (!NameToGlobalType(eObjectTypeName))
			{
				// Add it if it doesn't exist
				//AddGlobalTypeMapping(eObjectType,eObjectTypeName,eSchemaType,GLOBALTYPE_NONE);
				eObjectType = NameToGlobalType(eObjectTypeName);
			}

			TSC_ASSERT(eObjectType && eObjectType < GLOBALTYPE_MAXTYPES);
			TSC_ASSERT(eServerType && eServerType < GLOBALTYPE_MAXTYPES);

			errno = 0;
			iServerID = strtol(tokens[5], &pTemp, 10);
			TSC_ASSERT(errno == 0);
			if (iServerID == 0)
			{
				iServerID = FindFirstExtantIDOfServerType(pServer, eServerType);
				if (!iServerID)
				{
					Errorf("While processing TRANSACTIONSERVER_COMMAND_REGISTER, couldn't find a server of type %s for ID 0", 
						GlobalTypeToName(eServerType));
				}
			}

			iConnectionNum = GetConnectionNumFromServerTypeAndID(pServer, eServerType, iServerID);

			TSC_ASSERT(iConnectionNum != -1);

			TSC_ASSERT(pServer->objectDirectories[eObjectType].iDefaultConnectionIndex == -1 || pServer->objectDirectories[eObjectType].iDefaultConnectionIndex == iConnectionNum);
			pServer->objectDirectories[eObjectType].iDefaultConnectionIndex = iConnectionNum;
		}
		else if (token_count > 4 && strcmp(tokens[0], TRANSACTIONSERVER_COMMAND_OFFLINE) == 0)
		{
			GlobalType eObjectType = NameToGlobalType(tokens[1]);
			GlobalType eServerType = NameToGlobalType(tokens[3]);
			int iServerID;
			int iConnectionNum;
			int iObjectID;
			int iPrevConnectionNum;

			TSC_ASSERT(eObjectType);
			TSC_ASSERT(eServerType);

			errno = 0;
			iServerID = strtol(tokens[4], &pTemp, 10);
			TSC_ASSERT(errno == 0);
			if (iServerID == 0)
			{
				iServerID = FindFirstExtantIDOfServerType(pServer, eServerType);
				if (!iServerID)
				{
					Errorf("While processing TRANSACTIONSERVER_COMMAND_OFFLINE, couldn't find a server of type %s for ID 0", 
						GlobalTypeToName(eServerType));
				}
			}

			iConnectionNum = GetConnectionNumFromServerTypeAndID(pServer, eServerType, iServerID);

			TSC_ASSERT(iConnectionNum != -1);

			iObjectID = atoi(tokens[2]);

			TSC_ASSERT(iObjectID);

			if (!stashIntRemoveInt(pServer->objectDirectories[eObjectType].directory, iObjectID, &iPrevConnectionNum))
			{
				continue; //allow duplicate offlines
			}

			if (iPrevConnectionNum != iConnectionNum)
			{
				LogicalConnection *pPrevConnection = &pServer->pConnections[iPrevConnectionNum];

				TSC_ASSERTMSG(iPrevConnectionNum == iConnectionNum, "While offlining entity %u, the trans server thought it was on %s[%u], but the update string thought it was on %s[%u]. Talk to Alex or Ben or Vinay",
					iObjectID, GlobalTypeToName(pPrevConnection->eServerType), pPrevConnection->iServerID, GlobalTypeToName(eServerType), iServerID);
			}

		}
		else if (token_count > 6 && strcmp(tokens[0], TRANSACTIONSERVER_COMMAND_MOVE) == 0)
		{
			GlobalType eObjectType = NameToGlobalType(tokens[1]);
			GlobalType eServerType1 = NameToGlobalType(tokens[3]);
			GlobalType eServerType2 = NameToGlobalType(tokens[5]);
			int iServerID1, iServerID2;
			int iConnectionNum1, iConnectionNum2;
			int iObjectID;
			int iPrevConnectionNum;

			TSC_ASSERT(eObjectType);
			TSC_ASSERT(eServerType1);
			TSC_ASSERT(eServerType2);

			errno = 0;
			iServerID1 = strtol(tokens[4], &pTemp, 10);
			TSC_ASSERT(errno == 0);
			
			errno = 0;
			iServerID2 = strtol(tokens[6], &pTemp, 10);
			TSC_ASSERT(errno == 0);

			iConnectionNum1 = GetConnectionNumFromServerTypeAndID(pServer, eServerType1, iServerID1);
			iConnectionNum2 = GetConnectionNumFromServerTypeAndID(pServer, eServerType2, iServerID2);

			TSC_ASSERT(iConnectionNum1 != -1);
			TSC_ASSERT(iConnectionNum2 != -1);

			iObjectID = atoi(tokens[2]);

			TSC_ASSERT(iObjectID);

			if (!stashIntRemoveInt(pServer->objectDirectories[eObjectType].directory, iObjectID, &iPrevConnectionNum))
			{
				TSC_ASSERTMSG(0, "Couldn't find object to move");
			}

			if (iPrevConnectionNum != iConnectionNum1)
			{
				TSC_ASSERTMSG(0, "Object to move not where it should be");
			}

			stashIntAddInt(pServer->objectDirectories[eObjectType].directory, iObjectID, iConnectionNum2, true);

		}
		else
		{
			//unrecognized command
			TSC_ASSERTMSG(0, "Unrecognized trans server command");
		}
	}

	PERFINFO_AUTO_STOP();
}

void HandleTransactionServerCommand(TransactionServer *pServer, Packet *pPacket)
{
	char *pString;
	char *pComment;

	PERFINFO_AUTO_START_FUNC();

	pString = pktGetStringTemp(pPacket);
	pComment = pktGetStringTemp(pPacket);

	ProcessTransactionServerCommandString(pServer, pString, pComment);

	PERFINFO_AUTO_STOP();
}



void SendSimpleDBUpdateString(TransactionServer *pServer, Transaction *pTransaction, char *pDatabaseUpdateString, char *pComment)
{
	Packet *pPacket;
	static char *estrDBUpdate;
	TransDataBlock block = {0};

	if (pServer->iDatabaseConnectionIndex == -1)
	{
		return;
	}

	if (gbVerboseLogEverything)
	{
		log_printf(LOG_VERBOSETRANS, "%s. Therefore, DB update string: %s", pComment, pDatabaseUpdateString);
	}

	pPacket = NewTransactionPacket(pServer, pServer->iDatabaseConnectionIndex, TRANSSERVER_TRANSACTION_DBUPDATE);

	estrPrintf(&block.pString1, "%s", pDatabaseUpdateString);

	PutTransDataBlockIntoPacket(pPacket, &block);
	PutTransDataBlockIntoPacket(pPacket, NULL);

	if (pTransaction)
	{
		pTransaction->pTracker->iObjectDBUpdateBytes += pktGetSize(pPacket);
	}


	pktSend(&pPacket);

	estrDestroy(&block.pString1);
}

void HandleLocalTransactionUpdates(TransactionServer *pServer, Packet *pPacket)
{
	Packet *outPacket;

	TransDataBlock **ppDataBlocks = NULL;
	TransDataBlock *pDataBlock;
	int i;
	const char *transName;
	char *pComment;

	static char *pLogString = NULL;
	TransactionTracker *pTracker;

	PERFINFO_AUTO_START_FUNC();

	transName = allocAddString(pktGetStringTemp(pPacket));
	pComment = pktGetStringTemp(pPacket);

	pTracker = FindTransactionTracker(pServer, transName, false);
	pTracker->iNumRunAsLocal++;


	if(gbLogAllTransactions)
	{
		objLog(LOG_TRANSSERVER, GLOBALTYPE_NONE, 0, 0, NULL, NULL, NULL, "transUpdateDB", NULL, "name %s size %d",
			transName, pktGetSize(pPacket));
	}

	if (gbVerboseLogEverything)
	{
		estrPrintf(&pLogString, "Received local transaction update: %s\n", pComment);
	}


	while ((pDataBlock = GetTransDataBlockFromPacket(pPacket)))
	{
		eaPush(&ppDataBlocks, pDataBlock);
	}

	if (eaSize(&ppDataBlocks))
	{
		if (pServer->iDatabaseConnectionIndex == -1)
		{
			if (gbVerboseLogEverything)
			{
				estrConcatf(&pLogString, "No DB");
				log_printf(LOG_VERBOSETRANS, "%s", pLogString);
			}
			eaDestroyEx(&ppDataBlocks, TransDataBlockDestroy);
			PERFINFO_AUTO_STOP();
			return;
		}
		outPacket = NewTransactionPacket(pServer, pServer->iDatabaseConnectionIndex, TRANSSERVER_TRANSACTION_DBUPDATE);

		for (i=0; i < eaSize(&ppDataBlocks); i++)
		{
			PutTransDataBlockIntoPacket(outPacket, ppDataBlocks[i]);

			if (gbVerboseLogEverything)
			{
				if (ppDataBlocks[i]->pString1)
				{
					estrConcatf(&pLogString, "DB %d: %s\n", i, ppDataBlocks[i]->pString1);
					if (ppDataBlocks[i]->pString2)
					{
						estrConcatf(&pLogString, "\t(string 2): %s\n", ppDataBlocks[i]->pString2);
					}
				}
				else
				{
					estrConcatf(&pLogString, "DB %d: %d raw bytes\n", i, ppDataBlocks[i]->iDataSize);
				}
			}
		}
		PutTransDataBlockIntoPacket(outPacket, NULL);

		if (pTracker)
		{
			pTracker->iObjectDBUpdateBytes += pktGetSize(outPacket);
		}

		pktSend(&outPacket);
		eaDestroyEx(&ppDataBlocks, TransDataBlockDestroy);
	}

	while (pktGetBits(pPacket,1))
	{
		char *pTransServerUpdateString = pktGetStringTemp(pPacket);
		char *pCommentString = pktGetStringTemp(pPacket);
		ProcessTransactionServerCommandString(pServer, pTransServerUpdateString, pCommentString);

		if (gbVerboseLogEverything)
		{
			estrConcatf(&pLogString, "TS: %s\n", pTransServerUpdateString);
		}		
	}

	if (gbVerboseLogEverything)
	{
		log_printf(LOG_VERBOSETRANS, "%s", pLogString);
	}
	PERFINFO_AUTO_STOP();

}

void DumpTransaction(TransactionServer *pServer, Transaction *pTransaction)
{
	Transaction *pNext;
	int i;
	printf("Trans ID %u type %s\n",
		pTransaction->iID, StaticDefineIntRevLookup(enumTransactionTypeEnum, pTransaction->eType));
	printf("Blocked: %s\n", StaticDefineIntRevLookup(enumBlockedStatusEnum, pTransaction->eBlockedStatus));

	for (i = 0; i < pTransaction->iNumBaseTransactions; i++)
	{
		printf("  Base Trans %d, string %s, status %s\n",
			i, pTransaction->pBaseTransactions[i].transaction.pData,
			StaticDefineIntRevLookup(enumBaseTransactionStateEnum, pTransaction->pBaseTransactions[i].eState));
	}

	if (pTransaction->eBlockedStatus == IS_NOT_BLOCKED)
	{
		printf("I am blocking:\n");
		pNext = pTransaction->pNextBlocked;

		while (pNext)
		{
			printf("%u ", pNext->iID);
		}

		printf("\n");
	}

	printf("\n");

}
	

void DumpTransactionServerStatus(TransactionServer *pServer)
{
	int i;
	Transaction *pTrans;

	printf("\nTransaction server status:\nActive connections: %d  Active Transactions: %d  Succeded transactions: %"FORM_LL"d Other completed: %"FORM_LL"d\n",
		pServer->iNumActiveConnections, pServer->iNumActiveTransactions, pServer->iNumSucceededTransactions, pServer->iNumCompletedNonSucceededTransactions);

	printf("All active transactions:\n");

	for (i=0;i < gMaxTransactions; i++)
	{
		if (pServer->pTransactions[i].eType != TRANS_TYPE_NONE)
		{
			printf("In slot %d:\n", i);
			DumpTransaction(pServer, &pServer->pTransactions[i]);
		}
	}


	printf("\n\nTransactions blocked by nothing:\n");

	pTrans = pServer->pFirstTransactionBlockedByNothing;

	if (!pTrans)
	{
		printf("None\n\n");
	}

	while (pTrans)
	{
		printf("ID %u\n", pTrans->iID);
		
		if (!pTrans->pNextBlocked)
		{
			assert(pTrans == pServer->pLastTransactionBlockedByNothing);
		}

		pTrans = pTrans->pNextBlocked;
	}
}



void transactionServerSendGlobalInfo(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	if (GetControllerLink())
	{
		TransactionServer *pServer = (TransactionServer*)userData;
		TransactionServerGlobalInfo globalInfo = {0};
		Packet *pak;
		
		PERFINFO_AUTO_START_FUNC();

		globalInfo.iNumActive = pServer->iNumActiveTransactions;
		globalInfo.iNumSucceeded = pServer->iNumSucceededTransactions;
		globalInfo.iNumOtherCompleted = pServer->iNumCompletedNonSucceededTransactions;

		globalInfo.iTransPerSec = CountAverager_Query(pServer->pAllTransactionCounter, AVERAGE_MINUTE);
		globalInfo.iTransPerSecHrAvg = CountAverager_Query(pServer->pAllTransactionCounter, AVERAGE_HOUR);

	//	globalInfo.fBlocksPerSec = CountAverager_Query(pServer->pBlockedTransactionCounter, AVERAGE_MINUTE);
	//	globalInfo.fBlocksPerSecHrAvg = CountAverager_Query(pServer->pBlockedTransactionCounter, AVERAGE_HOUR);

		globalInfo.iBytesPerTrans = IntAverager_Query(pServer->pBytesPerTransactionAverager, AVERAGE_MINUTE);
	//	globalInfo.fBytesPerTransHrAvg = FloatAverager_Query(pServer->pBytesPerTransactionAverager, AVERAGE_HOUR);

		globalInfo.iMsecsLatency = IntAverager_Query(pServer->pCompletionTimeAverager, AVERAGE_MINUTE);
	//	globalInfo.fMsecsLatencyHrAvg = FloatAverager_Query(pServer->pCompletionTimeAverager, AVERAGE_HOUR);


		pktCreateWithCachedTracker(pak, GetControllerLink(), TO_CONTROLLER_HERE_IS_GLOBAL_SERVER_MONITORING_SUMMARY);

		PutContainerTypeIntoPacket(pak, GetAppGlobalType());
		PutContainerIDIntoPacket(pak, gServerLibState.containerID);

		ParserSend(parse_TransactionServerGlobalInfo, pak, NULL, &globalInfo, SENDDIFF_FLAG_FORCEPACKALL, 0, 0, NULL);

		pktSend(&pak);
		
		servLog(LOG_TRANSPERF, "TransServerGlobInfo", "NumActive %d NumSucceeded %I64d TransPerSec %d BytesPerTrans %d MsecsLatency %d",
			globalInfo.iNumActive, globalInfo.iNumSucceeded, globalInfo.iTransPerSec,
			globalInfo.iBytesPerTrans, globalInfo.iMsecsLatency);

		PERFINFO_AUTO_STOP();
	}
}

void HandleGotFailureFromOtherShard(int iReturnConnectionIndex, int iReturnConnectionID, 
	S64 iErrorCB, S64 iErrorUserData1, S64 iErrorUserData2)
{
	LogicalConnectionHandle returnHandle;
	int iActualIndex;
	
	returnHandle.iID = iReturnConnectionID;
	returnHandle.iIndex = iReturnConnectionIndex;

	if (ConnectionHandle_CheckIfSetAndReturnIndex(&gTransactionServer, &returnHandle, &iActualIndex))
	{
		Packet *pOutPack = NewTransactionPacket(&gTransactionServer, iActualIndex, TRANSSERVER_SIMPLE_PACKET_ERROR);
		pktSendBits64(pOutPack, 64, iErrorCB);
		pktSendBits64(pOutPack, 64, iErrorUserData1);
		pktSendBits64(pOutPack, 64, iErrorUserData2);
		pktSend(&pOutPack);
		return;
	}
}

void HandleSendPacketSimple(TransactionServer *pServer, Packet *pPak, int iReturnConnectionIndex)
{
	ContainerRef recip;
	Packet *pOutPack;

	int *pConnectionIndices = NULL;
	int i;
	char *pTransName;

	U64 iErrorCB = 0, iUserData1, iUserData2;

	PERFINFO_AUTO_START_FUNC();

	pTransName = pktGetStringTemp(pPak);

	if (gbLogAllTransactions)
	{	
		objLog(LOG_TRANSSERVER, GLOBALTYPE_NONE, 0, 0, NULL, NULL, NULL, "newTrans", NULL, "name %s size %d",
			pTransName, pktGetSize(pPak));
	}



	while (1)
	{
		int iConnectionIndex;

		recip.containerType = GetContainerTypeFromPacket(pPak);
		if (!recip.containerType)
		{
			break;
		}


		recip.containerID = GetContainerIDFromPacket(pPak);

		iConnectionIndex = GetConnectionIndexFromRecipient_SupportingRandom(pServer, pTransName, &recip);
		if (iConnectionIndex != -1)
		{
			ea32Push(&pConnectionIndices, iConnectionIndex);
		}
	}

	iErrorCB = pktGetBits64(pPak, 64);
	if (iErrorCB)
	{
		iUserData1 = pktGetBits64(pPak, 64);
		iUserData2 = pktGetBits64(pPak, 64);
		
		if (!ea32Size(&pConnectionIndices))
		{
			pOutPack = NewTransactionPacket(pServer, iReturnConnectionIndex, TRANSSERVER_SIMPLE_PACKET_ERROR);
			pktSendBits64(pOutPack, 64, iErrorCB);
			pktSendBits64(pOutPack, 64, iUserData1);
			pktSendBits64(pOutPack, 64, iUserData2);
			pktSend(&pOutPack);
			PERFINFO_AUTO_STOP();
			return;
		}
	}


	for (i=0; i < ea32Size(&pConnectionIndices); i++)
	{
		pOutPack = NewTransactionPacket(pServer, pConnectionIndices[i], TRANSSERVER_SEND_PACKET_SIMPLE);
	
		pktSendString(pOutPack, pTransName);


		pktCopyRemainingRawBytesToOtherPacket(pOutPack, pPak);
		pktSend(&pOutPack);
	}

	ea32Destroy(&pConnectionIndices);
	PERFINFO_AUTO_STOP();
}

void HandleSendPacketSimpleOtherShard(TransactionServer *pServer, Packet *pPak, int iReturnConnectionIndex)
{
	ContainerRef recip;

	char *pTransName;
	char *pShardName;

	U64 iErrorCB = 0, iUserData1 = 0, iUserData2 = 0;

	PERFINFO_AUTO_START_FUNC();

	pTransName = pktGetStringTemp(pPak);
	pShardName = pktGetStringTemp(pPak);

	if (gbLogAllTransactions)
	{	
		objLog(LOG_TRANSSERVER, GLOBALTYPE_NONE, 0, 0, NULL, NULL, NULL, "newTrans", NULL, "name %s size %d",
			pTransName, pktGetSize(pPak));
	}



	recip.containerType = GetContainerTypeFromPacket(pPak);
	recip.containerID = GetContainerIDFromPacket(pPak);

	iErrorCB = pktGetBits64(pPak, 64);
	if (iErrorCB)
	{
		iUserData1 = pktGetBits64(pPak, 64);
		iUserData2 = pktGetBits64(pPak, 64);	
	}

	//if we're sending to our own shard, shortcut
	if (stricmp(pShardName, GetShardNameFromShardInfoString()) == 0)
	{
		int iConnectionIndex = GetConnectionIndexFromRecipient_SupportingRandom(pServer, pTransName, &recip);
		if (iConnectionIndex == -1)
		{
			if (iErrorCB)
			{
				Packet *pOutPack = NewTransactionPacket(pServer, iReturnConnectionIndex, TRANSSERVER_SIMPLE_PACKET_ERROR);
				pktSendBits64(pOutPack, 64, iErrorCB);
				pktSendBits64(pOutPack, 64, iUserData1);
				pktSendBits64(pOutPack, 64, iUserData2);
				pktSend(&pOutPack);
			}
		}
		else
		{
			Packet *pOutPack = NewTransactionPacket(pServer, iConnectionIndex, TRANSSERVER_SEND_PACKET_SIMPLE);
	
			pktSendString(pOutPack, pTransName);
			pktCopyRemainingRawBytesToOtherPacket(pOutPack, pPak);
			pktSend(&pOutPack);
		}
	}
	else
	{
		if (!TransactionServer_ShardCluster_SendRemoteCommandPacket(pPak, pTransName, pShardName, recip.containerType, recip.containerID,
			iErrorCB, iUserData1, iUserData2, iReturnConnectionIndex, pServer->pConnections[iReturnConnectionIndex].iConnectionID))
		{
			if (iErrorCB)
			{
				Packet *pOutPack = NewTransactionPacket(pServer, iReturnConnectionIndex, TRANSSERVER_SIMPLE_PACKET_ERROR);
				pktSendBits64(pOutPack, 64, iErrorCB);
				pktSendBits64(pOutPack, 64, iUserData1);
				pktSendBits64(pOutPack, 64, iUserData2);
				pktSend(&pOutPack);
			}
		}
	}
	PERFINFO_AUTO_STOP();
}


void HandleRequestContainerOwner(TransactionServer *pServer, Packet *pPak, int iReturnConnectionIndex)
{
	int iRequestID = pktGetBits(pPak, 32);
	GlobalType eType = GetContainerTypeFromPacket(pPak);
	ContainerID iID = GetContainerIDFromPacket(pPak);
	int iConnectionIndex;
	GlobalType eOwnerType = 0;
	ContainerID iOwnerID = 0; 
	bool bIsDefault = false;
	Packet *pOutPack;

	if (eType >= 0 && eType < ARRAY_SIZE(pServer->objectDirectories) && pServer->objectDirectories[eType].directory)
	{

		if (!stashIntFindInt(pServer->objectDirectories[eType].directory, iID, &iConnectionIndex))
		{
			bIsDefault = true;
			iConnectionIndex = pServer->objectDirectories[eType].iDefaultConnectionIndex;
		}

		if (iConnectionIndex && pServer->pConnections[iConnectionIndex % gMaxLogicalConnections].iConnectionID != 0)
		{
			LogicalConnection *pConnection = &pServer->pConnections[iConnectionIndex % gMaxLogicalConnections];
			eOwnerType = pConnection->eServerType;
			iOwnerID = pConnection->iServerID;
		}
	}
	
	pOutPack = NewTransactionPacket(pServer, iReturnConnectionIndex, TRANSSERVER_HERE_IS_OWNER);
	if (pOutPack)
	{
		pktSendBits(pOutPack, 32, iRequestID);
		PutContainerTypeIntoPacket(pOutPack, eType);
		PutContainerIDIntoPacket(pOutPack, iID);
		PutContainerTypeIntoPacket(pOutPack, eOwnerType);
		PutContainerIDIntoPacket(pOutPack, iOwnerID);
		pktSendBits(pOutPack, 32, bIsDefault);
		pktSend(&pOutPack);
	}
}

AUTO_STRUCT;
typedef struct LaggedTransactionTracker
{
	const char *pTransName; AST(KEY POOL_STRING)
	U32 iStartTime; AST(FORMATSTRING(HTML_SECS_AGO=1))
	int iCount;
	int iPreviousCount;
} LaggedTransactionTracker;

StashTable sLaggedTransactions = NULL;

void HandleReportLaggedTransactions(TransactionServer *pServer, Packet *pPak)
{
	const char *pTransName = allocAddString(pktGetStringTemp(pPak));
	int iCount = pktGetBits(pPak, 32);
	int iTime = pktGetBits(pPak, 32);
	LaggedTransactionTracker *pTracker;

	if (!sLaggedTransactions)
	{
		sLaggedTransactions = stashTableCreateAddress(16);
		resRegisterDictionaryForStashTable("Lagged_Trans", RESCATEGORY_SYSTEM, 0, sLaggedTransactions, parse_LaggedTransactionTracker);

	}

	if (!stashFindPointer(sLaggedTransactions, pTransName, &pTracker))
	{
		pTracker = StructCreate(parse_LaggedTransactionTracker);
		pTracker->pTransName = pTransName;
		pTracker->iStartTime = timeSecondsSince2000();
		pTracker->iCount = 1;
		BeginVerboseLogging(pTransName);
		stashAddPointer(sLaggedTransactions, pTransName, pTracker, false);
	}
	else
	{
		pTracker->iCount++;
	}
}

void HandleRegisterSlowTransCallbackWithTracker(TransactionServer *pServer, Packet *pPak)
{
	const char *pTransName = allocAddString(pktGetStringTemp(pPak));
	TransactionTracker *pTracker = FindTransactionTracker(pServer, pTransName, false);
	if (pTracker)
	{
		pTracker->iSlowCallbacksOnServer++;
	}
}


void DumpLaggedTransactionSummary(void)
{
	U32 iCurTime = timeSecondsSince2000();
	StashTableIterator iterator;
	StashElement element;
	char *pFullReportString = NULL;

	//stick in the minus 2 because approximate is fine here
	U32 iStartCutoffTime = iCurTime - (giLaggedTransSummaryDumpInterval - 2);
	
	if (!sLaggedTransactions)
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	stashGetIterator(sLaggedTransactions, &iterator);

	while (stashGetNextElement(&iterator, &element))
	{
		LaggedTransactionTracker *pTracker = stashElementGetPointer(element);

		if (pTracker->iStartTime < iStartCutoffTime)
		{
			if (!pTracker->iCount)
			{
				stashRemovePointer(sLaggedTransactions, pTracker->pTransName, NULL);
				EndVerboseLogging(pTracker->pTransName);
				StructDestroy(parse_LaggedTransactionTracker, pTracker);
			}
			else
			{
				if (!pFullReportString)
				{
					char *pTimeStr = NULL;
					estrStackCreate(&pTimeStr);
					timeSecondsDurationToPrettyEString(giLaggedTransSummaryDumpInterval, &pTimeStr);
					estrPrintf(&pFullReportString, "During the past %s, some transactions were reported as completing slower than expected:", pTimeStr);
				}

				estrConcatf(&pFullReportString, " %d occurrence%s of %s.", pTracker->iCount, pTracker->iCount == 1 ? "" : "s", pTracker->pTransName);

				pTracker->iStartTime = iCurTime;
				pTracker->iPreviousCount = pTracker->iCount;
				pTracker->iCount = 0;
			}
		}
	}

	if (pFullReportString)
	{
		//no on ever looks at this alert, it's been obsoleted by TransactionTrackers for the most part... changing it to a log
		objLog(LOG_TRANSSERVER, 0, 0, 0, NULL, NULL, NULL, "laggedTransSummary", NULL, "%s", pFullReportString);
//		ErrorOrAlert("LAGGED_TRANS_SUMMARY", "%s", pFullReportString);
		estrDestroy(&pFullReportString);
	}
	
	PERFINFO_AUTO_STOP();
}

// Replay a transaction capture to a link.
// Instructions:
//
// To capture:
// 1) With the shard down, make an exact copy of the ObjectDB data.
// 2) Start the shard with the following options:
//		Transaction Server:		-NetCaptureBufferSize 1073741824 -NetAllowCaptureRequestOnPort 6978
//		ObjectDB:				-NetRequestPeerCaptureOnPort 6978
// 3) Let the shard run normally, or perform tests; it will capture transaction link data.
// 4) When finished capturing, run the following commands on the Transaction Server:
//		netStopCapture 1
//			(wait until successful capture stop is confirmed by printf in the console)
//		netDumpCapture c:\transactions.cap
//
// To replay:
// 5) Seed the ObjectDB with the exact ObjectDB that was saved in step #1 above.
// 6) Start the shard with the following options:
//		Transaction Server:		-ObjectDBReplayFromFile c:\transactions.cap
//		ObjectDB:				-SignalReplay
//
// Tips:
//   -You can do a capture and a replay at the same time (with different capture files), to verify that your replay is correct.
//   -You can start with an empty ObjectDB, as a simple way of keeping things synchronized for simple local tests.
void TransactionServerDebugCaptureReplay(NetLink *pLink)
{
	FILE *infile;
	char *buffer;
	const size_t buffer_size = 1024*1024;  // The buffer must be larger than the maximum flushed packet size
	size_t bytes_read;
	size_t buffer_bytes;
	int frameTimer;
	NetReplay *replay;
	const char *pos;
	HANDLE sGotPacket;

	// Accept but ignore incoming messages.
	TransactionServerIgnoreIncomingMessages(true);

	// Open the file.
	infile = fopen(sObjectDBReplayFromFile, "rb");
	assertmsgf(infile, "Unable to open replay file: %s", sObjectDBReplayFromFile);

	// Synchronize to beginning of real stream.
	setConsoleTitle("TransServer - Replaying transaction capture");
	buffer = malloc(buffer_size);
	bytes_read = fread(buffer, 1, buffer_size - 1, infile);
	assertmsgf(bytes_read > 0, "Unable to read from file: %s", sObjectDBReplayFromFile);
	pos = netReplayInit(&replay, buffer, bytes_read);
	buffer_bytes = bytes_read - (pos - buffer);
	memmove(buffer, pos, buffer_bytes);

	// Replay rest of file.
	printf("Initiating replay from \"%s\"...\n", sObjectDBReplayFromFile);
	frameTimer = timerAlloc();

	// Create event.
	sGotPacket = CreateEvent(NULL, true, false, LTM_DEBUG_REPLAY_SIGNAL_NAME);
	assertmsgf(sGotPacket, "Unable to create event: %d", GetLastError());

	for(;;)
	{
		Packet	*pak;
		size_t packet_size;

		// Tick.
		TransactionServerMonitorTick(timerElapsedAndStart(frameTimer));

		// Read into buffer.
		if (buffer_bytes != buffer_size)
			bytes_read = fread(buffer + buffer_bytes, 1, buffer_size - buffer_bytes, infile);
		buffer_bytes += bytes_read;
		if (!buffer_bytes)
			break;													// Exit: Done!

		// Set sGotPacket to non-signaled.
		ResetEvent(sGotPacket);

		// Replay until we get a complete packet block.
		pos = netReplayNext(replay, buffer, buffer_bytes);
		if (!pos)
		{
			printf("Unable to find next packet in capture, %u bytes left\n", (unsigned)buffer_bytes);
			break;
		}
		packet_size = pos - buffer;

		// Send the packet block
		pak = pktCreateRaw(pLink);
		pktSendBytesRaw(pak, buffer, (int)packet_size);
		pktSendRaw(&pak);

		// Advance buffer
		buffer_bytes -= packet_size;
		memmove(buffer, pos, buffer_bytes);

		// Wait for the ObjectDB to process the packet we just sent.
		PERFINFO_AUTO_START("WaitingForObjectDBTick", 1);
		WaitForSingleObject(sGotPacket, INFINITE);
		PERFINFO_AUTO_STOP();

		// Start next frame.
		autoTimerThreadFrameEnd();
		autoTimerThreadFrameBegin("replaying");
		ADD_MISC_COUNT(1, "replaying");
	}
	fclose(infile);
	netReplayDestroy(replay);
	free(buffer);

	// Wait forever.
	setConsoleTitle("TransServer - Replay done");
	timerStart(frameTimer);
	printf("Replay done, sleeping forever...\n");
	for(;;)
	{
		F32 frametime;
		autoTimerThreadFrameBegin("sleeping");
		ADD_MISC_COUNT(1, "sleeping");
		frametime = timerElapsedAndStart(frameTimer);
		TransactionServerMonitorTick(frametime);
		autoTimerThreadFrameEnd();
	}
	Sleep(INFINITE);
}

int OVERRIDE_LATELINK_comm_commandqueue_size(void)
{
	return (1<<20);
}


const char *GetShortTransDataString(const char *pInStr)
{
	char tempShort[100];
	char *pFirstSpace;

	if (strStartsWith(pInStr, "runautotrans "))
	{
		pInStr += 13;
	}
	else if (strStartsWith(pInStr, "slowRemoteCommand "))
	{
		pInStr += 18;
	}

	pFirstSpace = strchr(pInStr, ' ');
	if (!pFirstSpace)
	{
		strcpy_trunc(tempShort, pInStr);
		return allocAddString(tempShort);
	}

	strncpy_trunc(tempShort, pInStr, pFirstSpace - pInStr);
	return allocAddString(tempShort);
}


#define MAX_VERBOSE_LOGS_ONE_TRANSACTION 32

void VerboseEntryToString(TransVerboseLogEntry *pEntry, char **ppOutString)
{
	switch (pEntry->eType)
	{
	xcase TVL_CREATED:
		estrConcatf(ppOutString, "REQUESTED by %s", GlobalTypeAndIDToString(pEntry->eContainerType, pEntry->iContainerID));
	xcase TVL_NOWBLOCKEDBYNOTHING:
		estrConcatf(ppOutString, "Now blocked by NOTHING");
	xcase TVL_BLOCKEDBYSOMEONE:
		estrConcatf(ppOutString, "BLOCKED by %s(%u)", pEntry->pStr, pEntry->iOtherID);
	xcase TVL_BASETRANS_BEGAN:
		estrConcatf(ppOutString, "STEP %d(%s) BEGAN, sent to %s", pEntry->iOtherID, 
			pEntry->pStr, GlobalTypeAndIDToString(pEntry->eContainerType, pEntry->iContainerID));
	xcase TVL_FAILED:
		estrConcatf(ppOutString, "FAILED");
	xcase TVL_SUCCEEDED:
		estrConcatf(ppOutString, "SUCCEEDED");
	xcase TVL_BEGIN_CANCELLING:
		estrConcatf(ppOutString, "CANCELLING begun");
	xcase TVL_FAIL_FROM_INIT_QUERY_STATE:
		estrConcatf(ppOutString, "FAILing from query state of step %d", pEntry->iOtherID);
	xcase TVL_STEP_FAILED:
		estrConcatf(ppOutString, "Step %d FAILED", pEntry->iOtherID);
	xcase TVL_STEP_SUCCEEDED:
		estrConcatf(ppOutString, "Step %d SUCCEEDED", pEntry->iOtherID);
	xcase TVL_STEP_CANCEL_CONFIRMED:
		estrConcatf(ppOutString, "Step %d CANCEL CONFIRMED", pEntry->iOtherID);
	xcase TVL_STEP_POSSIBLE:
		estrConcatf(ppOutString, "Step %d POSSIBLE", pEntry->iOtherID);
	xcase TVL_STEP_POSSIBLE_AND_CONFIRMED:
		estrConcatf(ppOutString, "Step %d POSSIBLE AND CONFIRMED", pEntry->iOtherID);
	xcase TVL_STEP_ABORTED: 
		estrConcatf(ppOutString, "Step %d ABORTED (because: \"%s\")", pEntry->iOtherID, pEntry->pStr);
	xcase TVL_UNBLOCKED:
		estrConcatf(ppOutString, "UNBLOCKED");
	}
}

void OutputVerboseLogging(Transaction *pTrans, char *pDesc, U64 iCurTime, bool bComplete)
{
	char *pOutString = NULL;
	int i;

	//don't do verbose logs for the same transaction twice within 10 seconds unless it's going to
	//get put into the trans tracker
	if (pTrans->pTracker->iLastTimeVerboseLogged > iCurTime - 10000
		&& !(bComplete && !estrLength(&pTrans->pTracker->pVerboseLog_Complete))
		&& !(!bComplete && !estrLength(&pTrans->pTracker->pVerboseLog_Incomplete)))
	{
		return;
	}

	pTrans->pTracker->iLastTimeVerboseLogged = iCurTime;




	estrPrintf(&pOutString, "Trans %s(%u) is %s after %"FORM_LL"d msecs. History:\n",
		pTrans->pTransactionName, pTrans->iID, 
		pDesc,
		timeMsecsSince2000() - pTrans->iTimeBegan);


	for (i=0; i < eaSize(&pTrans->ppVerboseLogs); i++)
	{
		TransVerboseLogEntry *pEntry = pTrans->ppVerboseLogs[i];

		estrConcatf(&pOutString, "%"FORM_LL"d msecs: ", pEntry->iTime);

		VerboseEntryToString(pEntry, &pOutString);
	

		estrConcatf(&pOutString, "\n");
	}

	if (eaSize(&pTrans->ppVerboseLogs) >= MAX_VERBOSE_LOGS_ONE_TRANSACTION)
	{
		estrConcatf(&pOutString, "(Max %d steps, aborting verbose logging)", MAX_VERBOSE_LOGS_ONE_TRANSACTION);
	}

	if (bComplete && !estrLength(&pTrans->pTracker->pVerboseLog_Complete))
	{
		estrPrintf(&pTrans->pTracker->pVerboseLog_Complete, "<pre>\n%s\n</pre>", pOutString);
	}
	else if (!bComplete && !estrLength(&pTrans->pTracker->pVerboseLog_Incomplete))
	{
		estrPrintf(&pTrans->pTracker->pVerboseLog_Incomplete, "<pre>\n%s\n</pre>", pOutString);
	}


	log_printf(LOG_VERBOSETRANS, "%s", pOutString);
	estrDestroy(&pOutString);
}

void DoVerboseTransLogging(Transaction *pTrans, TransVerboseLogEntryType eType, const char *pStr, GlobalType eCtrType, ContainerID iCtrID, U32 iOtherID)
{

	if (gbVerboseLogEverything)
	{
		if (eType != TVL_CLEANUP)
		{
			TransVerboseLogEntry entry;
			char *pTempStr = NULL;
			estrStackCreate(&pTempStr);
			entry.eContainerType = eCtrType;
			entry.eType = eType;
			entry.iContainerID = iCtrID;
			entry.iOtherID = iOtherID;
			entry.pStr = pStr;
			estrPrintf(&pTempStr, "Verbose update for Trans %u(%s): ", pTrans->iID, pTrans->pTransactionName);
			VerboseEntryToString(&entry, &pTempStr);
			log_printf(LOG_VERBOSETRANS, "%s", pTempStr);
			estrDestroy(&pTempStr);
		}
		return;
	}

		


	if (eType == TVL_CLEANUP)
	{
		U64 iCurTime = timeMsecsSince2000();
		U64 iLifespan = iCurTime - pTrans->iTimeBegan;
		
		if (!gbNoVerboseLogging)
		{
			if (iLifespan > pTrans->pTracker->iCompleteVerboseLogLifespan)
			{
				estrDestroy(&pTrans->pTracker->pVerboseLog_Complete);
				OutputVerboseLogging(pTrans, "complete", iCurTime, true);
				pTrans->pTracker->iCompleteVerboseLogLifespan = iLifespan;
			}
			else if (iLifespan >= siVerboseTransMinLogCutoff)
			{
				OutputVerboseLogging(pTrans, "complete", iCurTime, true);
			}
		}

		eaDestroyStruct(&pTrans->ppVerboseLogs, parse_TransVerboseLogEntry);

	}
	else
	{
		if (!gbNoVerboseLogging)
		{
			if (eaSize(&pTrans->ppVerboseLogs) < MAX_VERBOSE_LOGS_ONE_TRANSACTION)
			{
				TransVerboseLogEntry *pEntry = StructCreate(parse_TransVerboseLogEntry);
				pEntry->eType = eType;
				if (eType == TVL_BASETRANS_BEGAN)
				{
					pEntry->pStr = GetShortTransDataString(pStr);
				}
				else
				{
					pEntry->pStr = pStr;
				}
				pEntry->eContainerType = eCtrType;
				pEntry->iContainerID = iCtrID;
				pEntry->iOtherID = iOtherID;
				pEntry->iTime = timeMsecsSince2000() - pTrans->iTimeBegan;

				eaPush(&pTrans->ppVerboseLogs, pEntry);
			}
		}
	}
}

void TransServer_HandleRemoteCommandFromOtherShard(NetLink *pLink, Packet *pPack)
{
	U32 iReceiptIndex = pktGetBits(pPack, 32);
	char *pTransName = pktGetStringTemp(pPack);
	ContainerRef recip;
	U32 iConnectionIndex;
	Packet *pOutPack;

	recip.containerType = GetContainerTypeFromPacket(pPack);
	recip.containerID = GetContainerIDFromPacket(pPack);
	
	iConnectionIndex = GetConnectionIndexFromRecipient_SupportingRandom(&gTransactionServer, pTransName, &recip);

	if (iConnectionIndex == -1)
	{
		if (recip.containerID == 0)
		{
			if (stashGetCount(gTransactionServer.objectDirectories[recip.containerType].directory) > 1)
			{
				TriggerAlertf("BAD_INTERSHARD_CMD", ALERTLEVEL_CRITICAL, ALERTCATEGORY_PROGRAMMER, 0, 0, 0, 0, 0, NULL, 0, "Someone sent intershard remote command %s to %s[0]. But there is more than one server of that type, this is illegal",
					pTransName, GlobalTypeToName(recip.containerType));
			}
		}

		TransServerShardCluster_SendReceipt(pLink, iReceiptIndex, false);
		return;
	}
	

	pOutPack = NewTransactionPacket(&gTransactionServer, iConnectionIndex, TRANSSERVER_SEND_PACKET_SIMPLE);
	
	pktSendString(pOutPack, pTransName);

	pktCopyRemainingRawBytesToOtherPacket(pOutPack, pPack);
	pktSend(&pOutPack);

	TransServerShardCluster_SendReceipt(pLink, iReceiptIndex, true);

}

//debug command that Jeff W requested, logs out onto a log line the entity IDs of all players currently online
AUTO_COMMAND;
void LogAllOnlinePlayerIDs(void)
{
	StashTableIterator iter;
	StashElement element;
	char *pOutString = NULL;

	stashGetIterator(gTransactionServer.objectDirectories[GLOBALTYPE_ENTITYPLAYER].directory, &iter);


	while (stashGetNextElement(&iter, &element))
	{
		estrConcatf(&pOutString, "%u ", stashElementGetIntKey(element));	
	}

	if (pOutString)
	{
		log_printf(LOG_MISC, "Entity IDs of players currently online, requested by LogAllOnlinePlayerIDs cmd: %s", pOutString);
	}
	else
	{
		log_printf(LOG_MISC, "LogAllOnlinePlayerIDs was called, but there are no players online");
	}

	estrDestroy(&pOutString);
}
	


#include "TransactionServer_c_ast.c"
