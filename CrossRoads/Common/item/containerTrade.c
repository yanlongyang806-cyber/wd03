/***************************************************************************
*     Copyright (c) 2005-2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "containerTrade.h"
#include "objTransactions.h"
#include "AutoTransDefs.h"

#ifdef GAMESERVER
#include "LoggedTransactions.h"
#endif

#include "AutoGen/containerTrade_h_ast.h"

static void CreatePet_CB(TransactionReturnVal *returnVal, ContainerTradeData *cbData)
{
	int i;

	for(i=eaSize(&cbData->ppContainers)-1;i>=0;i--)
	{
		if(cbData->ppContainers[i]->eNewID == 0)
			break;
	}

	if(i==-1)
	{
		cbData->func(cbData,cbData->pUserData);
	}
}

static void DestroyPet_CB(TransactionReturnVal *returnVal, ContainerTradeData *pData)
{
	if(returnVal->eOutcome == TRANSACTION_OUTCOME_FAILURE)
	{
		//Make a log entry
	}
}

//Remove all old containers, free containerTradeData
void ContainerTrade_Finish(ContainerTradeData *pData)
{
	int i;

	for(i=eaSize(&pData->ppContainers)-1;i>=0;i--)
	{
		objRequestContainerDestroy(objCreateManagedReturnVal(DestroyPet_CB,NULL),pData->ppContainers[i]->eContainerType,pData->ppContainers[i]->eOldID,GLOBALTYPE_OBJECTDB,0);
	}

	StructDestroy(parse_ContainerTradeData,pData);
}

static void CreateContainer_CB(TransactionReturnVal *returnVal, ContainerTrade *pData)
{
	int i;
	ContainerTradeData *pParentData = pData->pParentData;

	if(returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		ContainerID newID = atoi(returnVal->pBaseReturnVals[0].returnString);

		pData->eNewID = newID;
	}
	else
	{
		//Transaction failed, mark it as so
		pParentData->bCreateFailed = true;
		pData->bCreateFailed = true;
	}

	for(i=eaSize(&pParentData->ppContainers)-1;i>=0;i--)
	{
		if(pParentData->ppContainers[i]->eNewID == 0
			&& pParentData->ppContainers[i]->bCreateFailed == false)
			break;
	}

	if(i==-1)
	{
		if(pParentData->bCreateFailed)
		{
			//Clean up all created containers.
			for(i=0;i<eaSize(&pParentData->ppContainers); i++)
			{
				if(!pParentData->ppContainers[i]->bCreateFailed)
					objRequestContainerDestroy(objCreateManagedReturnVal(DestroyPet_CB,NULL),pParentData->ppContainers[i]->eContainerType,pParentData->ppContainers[i]->eNewID,GLOBALTYPE_OBJECTDB,0);
			}
		}
		else
		{
			pParentData->func(pParentData,pParentData->pUserData);
		}
	}
}

void ContainerTrade_Begin(ContainerTradeData *pData)
{
	int i;

	for(i=0;i<eaSize(&pData->ppContainers);i++)
	{
		pData->ppContainers[i]->pParentData = pData;
		objRequestContainerCreate(objCreateManagedReturnVal(CreateContainer_CB,pData->ppContainers[i]),pData->ppContainers[i]->eContainerType,NULL,GLOBALTYPE_OBJECTDB,0);
	}
}

#include "AutoGen/containerTrade_h_ast.c"