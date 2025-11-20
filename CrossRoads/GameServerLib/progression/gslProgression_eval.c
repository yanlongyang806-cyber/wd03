/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "progression_common.h"
#include "mission_common.h"
#include "Entity.h"
#include "Expression.h"
#include "Team.h"
#include "AutoGen/progression_common_h_ast.h"

// Returns the current progress of the player for the given story. If the player is in a team, it returns the progress of the team.
AUTO_EXPR_FUNC(player) ACMD_NAME(ProgressionGetCurrentProgressByStory);
const char * progression_FuncProgressionGetCurrentProgressByStory(ExprContext *pContext, ACMD_EXPR_DICT("GameProgressionNodeDef") const char *pchStoryArcNode)
{
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	GameProgressionNodeDef* pRootNodeDef = progression_NodeDefFromName(pchStoryArcNode);
	ProgressionInfo* pInfo = progression_GetInfoFromPlayer(pPlayerEnt);

	if (pInfo && pRootNodeDef)
	{
		GameProgressionNodeDef* pNodeDef = NULL;
		Team* pTeam = team_GetTeam(pPlayerEnt);
		
		if (pTeam && g_GameProgressionConfig.bEnableTeamProgressionTracking)
		{
			TeamProgressionData* pTeamData = progression_GetCurrentTeamProgress(pTeam);
			if (pTeamData)
			{
				pNodeDef = GET_REF(pTeamData->hNode);
			}
		}
		else
		{
			pNodeDef = progression_GetCurrentProgress(pPlayerEnt, pRootNodeDef);
		}
		return pNodeDef ? pNodeDef->pchName : "";
	}

	return "";
}

AUTO_EXPR_FUNC(player) ACMD_NAME(ProgressionIsCurrentProgressEqualOrGreaterThanNode);
bool progression_FuncProgressionIsCurrentProgressEqualOrGreaterThanNode(ExprContext *pContext, ACMD_EXPR_DICT("GameProgressionNodeDef") const char *pchStoryArcNode, ACMD_EXPR_DICT("GameProgressionNodeDef") const char *pchNodeToCompare)
{
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	GameProgressionNodeDef* pRootNodeDef = progression_NodeDefFromName(pchStoryArcNode);
	GameProgressionNodeDef* pNodeDef = progression_NodeDefFromName(pchNodeToCompare);

	if (pPlayerEnt && pNodeDef && pRootNodeDef)
	{
		// Current progression node
		GameProgressionNodeDef* pCurrentNodeDef = progression_GetCurrentProgress(pPlayerEnt, pRootNodeDef);

		while (pCurrentNodeDef)
		{
			if (pCurrentNodeDef == pNodeDef)
			{
				return true;
			}
			pCurrentNodeDef = GET_REF(pCurrentNodeDef->hPrevSibling);
		}
	}

	return false;
}

// Returns the current progress of the player for the given story.
AUTO_EXPR_FUNC(player) ACMD_NAME(ProgressionGetPersonalProgressByStory);
const char * progression_FuncProgressionGetPersonalProgressByStory(ExprContext *pContext, ACMD_EXPR_DICT("GameProgressionNodeDef") const char *pchStoryArcNode)
{
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	GameProgressionNodeDef* pRootNodeDef = progression_NodeDefFromName(pchStoryArcNode);

	if (pPlayerEnt && pRootNodeDef)
	{
		GameProgressionNodeDef* pNodeDef = progression_GetCurrentProgress(pPlayerEnt, pRootNodeDef);

		return pNodeDef ? pNodeDef->pchName : "";
	}

	return "";
}