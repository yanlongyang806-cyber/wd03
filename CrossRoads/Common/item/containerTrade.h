/***************************************************************************
*     Copyright (c) 2005-2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "GlobalTypeEnum.h"
#include "objTransactions.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

typedef struct ContainerTradeData ContainerTradeData;

typedef void (*ContainerTradeCallbackFunc)(ContainerTradeData *pData, void *pUserData);

AUTO_STRUCT;
typedef struct ContainerTrade
{
	ContainerTradeData *pParentData;	AST(UNOWNED)

	GlobalType eContainerType;
	ContainerID eOldID;
	ContainerID eNewID;
	bool bCreateFailed;
}ContainerTrade;

AUTO_STRUCT;
typedef struct ContainerTradeData
{
	ContainerTrade **ppContainers;
	ContainerTradeCallbackFunc func;	NO_AST
	void *pUserData;					NO_AST
	bool bCreateFailed;
}ContainerTradeData;

void ContainerTrade_Finish(ContainerTradeData *pData);
void ContainerTrade_Begin(ContainerTradeData *pData);