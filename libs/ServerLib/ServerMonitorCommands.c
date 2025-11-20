#include "autogen/ServerLib_autogen_RemoteFuncs.h"
#include "autogen/transactionOutcomes_h_ast.h"
#include "CachedGlobalObjectList.h"

//this file contains commands that are called by the server monitor... that is, commands that are
//embedded with AST_COMMAND into entities and containers and other things that can be browsed
//by the server monitor

void ServerMonTransactionOnEntity_Return(TransactionReturnVal *pReturnVal, CmdSlowReturnForServerMonitorInfo *pSlowReturnInfo)
{
	char *pFullRetString = NULL;
	int i;
	estrStackCreate(&pFullRetString);

	estrConcatf(&pFullRetString, "Transaction outcome: %s\n", StaticDefineIntRevLookup(enumTransactionOutcomeEnum, pReturnVal->eOutcome));
	for (i=0; i < pReturnVal->iNumBaseTransactions; i++)
	{
		estrConcatf(&pFullRetString, "Step %d result %s: %s\n", i, 
			StaticDefineIntRevLookup(enumTransactionOutcomeEnum, pReturnVal->pBaseReturnVals[i].eOutcome), pReturnVal->pBaseReturnVals[i].returnString);
	}

	DoSlowCmdReturn(pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS, pFullRetString, pSlowReturnInfo);

	free(pSlowReturnInfo);
	estrDestroy(&pFullRetString);

}

AUTO_COMMAND;
void ServerMonTransactionOnEntity(CmdContext *pContext, char *pEntityTypeName, ContainerID iID, ACMD_SENTENCE pTransactionString)
{
	GlobalType eType = NameToGlobalType(pEntityTypeName);
	TransactionReturnVal *pTransReturnStruct;
	CmdSlowReturnForServerMonitorInfo *pSlowReturnInfo;

	if (eType == GLOBALTYPE_NONE)
	{
		return;
	}

	if (!objLocalManager())
	{
		return;
	}

	pContext->slowReturnInfo.bDoingSlowReturn = true;
	pSlowReturnInfo = malloc(sizeof(CmdSlowReturnForServerMonitorInfo));
	memcpy(pSlowReturnInfo, &pContext->slowReturnInfo, sizeof(CmdSlowReturnForServerMonitorInfo));

	pTransReturnStruct = objCreateManagedReturnVal(ServerMonTransactionOnEntity_Return, pSlowReturnInfo);


	RequestSimpleTransaction(objLocalManager(), eType, iID, "ServerMonTransaction", pTransactionString,
		TRANS_TYPE_SIMULTANEOUS, pTransReturnStruct);
}


AUTO_COMMAND;
void ServerMonTransactionOnContainerList(CmdContext *pContext, int iListID, ACMD_SENTENCE pTransactionString)
{
	CachedGlobalObjectList *pList = GetCachedGlobalObjectListFromID(iListID);
	TransactionReturnVal *pTransReturnStruct;
	CmdSlowReturnForServerMonitorInfo *pSlowReturnInfo;
	BaseTransaction **ppBaseTransactions = NULL;
	int i;

	if (!pList)
	{
		return;
	}

	if (pList->eContainerType == GLOBALTYPE_NONE)
	{
		return;
	}
	
	if (!objLocalManager())
	{
		return;
	}

	if (!eaSize(&pList->ppLinks))
	{
		return;
	}

	pContext->slowReturnInfo.bDoingSlowReturn = true;
	pSlowReturnInfo = malloc(sizeof(CmdSlowReturnForServerMonitorInfo));
	memcpy(pSlowReturnInfo, &pContext->slowReturnInfo, sizeof(CmdSlowReturnForServerMonitorInfo));

	pTransReturnStruct = objCreateManagedReturnVal(ServerMonTransactionOnEntity_Return, pSlowReturnInfo);

	for (i=0; i < eaSize(&pList->ppLinks); i++)
	{
		BaseTransaction *pBaseTransaction = calloc(sizeof(BaseTransaction), 1);
		pBaseTransaction->pData = pTransactionString;
		pBaseTransaction->recipient.containerType = pList->eContainerType;
		pBaseTransaction->recipient.containerID = atoi(pList->ppLinks[i]->pObjName);

		eaPush(&ppBaseTransactions, pBaseTransaction);
	}

	RequestNewTransaction(objLocalManager(), "ServerMonTransaction", ppBaseTransactions,
		TRANS_TYPE_SIMULTANEOUS, pTransReturnStruct, 0);
}



AUTO_COMMAND;
void BroadcastMessageToPlayer(ContainerID iContainerID, ACMD_SENTENCE pMessage)
{
	RemoteCommand_RemoteObjBroadcastMessage(GLOBALTYPE_ENTITYPLAYER, iContainerID, "Player-specific Message", pMessage);
}

