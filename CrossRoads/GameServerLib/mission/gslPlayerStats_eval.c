/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/


#include "Entity.h"
#include "Expression.h"
#include "mission_common.h"
#include "Player.h"
#include "playerstats_common.h"
#include "stringcache.h"


// ----------------------------------------------------------------------------
//  Player Stats Expression Functions
// ----------------------------------------------------------------------------

AUTO_EXPR_FUNC_STATIC_CHECK;
int mission_FuncPlayerStatCount_StaticCheck(ExprContext *pContext, ACMD_EXPR_RES_DICT(PlayerStatDef) const char *pcStatName)
{
	MissionDef *pMissionDef = exprContextGetVarPointerUnsafePooled(pContext, g_MissionDefVarName);
	if (pMissionDef) {
		eaPushUnique(&pMissionDef->eaTrackedStats, allocAddString(pcStatName));
	}
	return 0;
}


// Returns the value of a named PlayerStat for the current player.
AUTO_EXPR_FUNC(player, mission) ACMD_NAME(PlayerStatCount) ACMD_EXPR_STATIC_CHECK(mission_FuncPlayerStatCount_StaticCheck);
int mission_FuncPlayerStatCount(ExprContext *pContext, ACMD_EXPR_RES_DICT(PlayerStatDef) const char *pcStatName)
{
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	if (pPlayerEnt && pPlayerEnt->pPlayer && pPlayerEnt->pPlayer->pStatsInfo && pcStatName) {
		return playerstat_GetValue(pPlayerEnt->pPlayer->pStatsInfo, pcStatName);
	}
	return 0;
}


// Returns the value of a named PlayerStat for the current player.
// Leaderboard version that does not need the mission context expression
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(Leaderboard_PlayerStatCount);
int mission_FuncLeaderboardPlayerStatCount(ExprContext *pContext, SA_PARAM_NN_VALID Entity *pPlayerEnt, ACMD_EXPR_RES_DICT(PlayerStatDef) const char *pcStatName)
{
	if (pPlayerEnt && pPlayerEnt->pPlayer && pPlayerEnt->pPlayer->pStatsInfo && pcStatName) {
		return playerstat_GetValue(pPlayerEnt->pPlayer->pStatsInfo, pcStatName);
	}
	return 0;
}


