/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Expression.h"
#include "Entity.h"
#include "gslQueue.h"
#include "Team.h"
#include "Player.h"
#include "GamePermissionsCommon.h"			// For g_PlayerVarName

///////////////////////////////////////////////////////////////////////////////////////////
// Expression functions
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_EXPR_FUNC(Player, Mission) ACMD_NAME(queue_HaveAllFriendlyGroupsConceded);
ExprFuncReturnVal gslQueueExprHaveAllFriendlyGroupsConceded(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT piResult,
															const char* pchFaction)
{
	gslQueue_CheckGroupsConceded(iPartitionIdx, pchFaction, true, piResult);
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(Player, Mission) ACMD_NAME(queue_HaveAllOpposingGroupsConceded);
ExprFuncReturnVal gslQueueExprHaveAllOpposingGroupsConceded(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT piResult,
															const char* pchFaction)
{
	gslQueue_CheckGroupsConceded(iPartitionIdx, pchFaction, false, piResult);
	return ExprFuncReturnFinished;
}


AUTO_EXPR_FUNC(Player, Mission) ACMD_NAME(queue_PlayerHasQueueInfo);
bool gslQueueExprPlayerHasQueueInfo(ExprContext* context)
{
	Entity* pEntity = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);
	
	if (pEntity!=NULL && pEntity->pPlayer!=NULL && pEntity->pPlayer->pPlayerQueueInfo!=NULL)
	{
		int iMyQIdx;
		PlayerQueueInfo *pPlayerQueueInfo = pEntity->pPlayer->pPlayerQueueInfo;

		if (pPlayerQueueInfo->pQueueInstantiationInfo!=NULL)
		{
			return(true);
		}
		for (iMyQIdx = eaSize(&pPlayerQueueInfo->eaQueues) - 1; iMyQIdx >=0; iMyQIdx--)
		{
			PlayerQueue* pQueue = pPlayerQueueInfo->eaQueues[iMyQIdx];
			if (eaSize(&pQueue->eaInstances) > 0)
			{
				return(true);
			}
		}
	}

	return(false);
}


AUTO_EXPR_FUNC(encounter_action) ACMD_NAME(StopTrackingMapOnQueueServer);
ExprFuncReturnVal gslQueueExprStopTrackingMapOnQueueServer(ACMD_EXPR_PARTITION iPartitionIdx)
{
	gslQueue_StopTrackingMapOnQueueServer(iPartitionIdx);
	return ExprFuncReturnFinished;
}

// Attempts to have the entity join the queue. 
// if EnableStrictTeamRules is enabled on the queueConfig, if the entity is on the team it will attempt to join the team
AUTO_EXPR_FUNC(encounter_action) ACMD_NAME(queue_Join);
ExprFuncReturnVal gslQueueExprJoinQueue(ACMD_EXPR_PARTITION iPartitionIdx, 
										SA_PARAM_NN_VALID Entity *pEntity, 
										SA_PARAM_NN_STR const char *pchQueueName)
{
	if (!pEntity  || !pchQueueName || !(*pchQueueName))
		return ExprFuncReturnError;

	// refresh the entity's
	gslQueueRefresh(pEntity);

	if (pEntity->pPlayer->pPlayerQueueInfo)
	{
		pEntity->pPlayer->pPlayerQueueInfo->pszAttemptQueueName = allocAddString(pchQueueName);
	}

	return ExprFuncReturnFinished;
}
