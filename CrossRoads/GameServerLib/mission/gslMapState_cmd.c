#include "Entity.h"
#include "gslMapState.h"
#include "gslQueue.h"
#include "mapstate_common.h"

#include "mapstate_common_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


// ----------------------------------------------------------------------------------
// Map State Commands
// ----------------------------------------------------------------------------------

// PowerSpeedRecharge <speed>: Adjusts the global modifier to recharge speed.  1 is default, larger is faster, negative is invalid.
AUTO_COMMAND ACMD_CATEGORY(Debug, Powers);
void PowersSpeedRecharge(Entity *pEnt, F32 fSpeed)
{
	if (pEnt) {
		mapState_SetPowersSpeedRecharge(entGetPartitionIdx(pEnt), fSpeed);
	}
}


// Show the scoreboard with the given name to all players in the entarray
AUTO_COMMAND ACMD_NAME(debugSetScoreboardState);
void dbgCmd_SetScoreboardState(Entity *pEnt, ACMD_EXPR_ENUM(ScoreboardState) const char *pcState)
{
	int eState = StaticDefineIntGetInt(ScoreboardStateEnum, pcState);

	if (eState >= 0) {
		int iPartitionIdx = entGetPartitionIdx(pEnt);

		mapState_SetScoreboardState(iPartitionIdx, eState);

		//TODO(BH): Untie these scoreboards from pvp leaderboards
		gslQueue_MapSetStateFromScoreboardState(iPartitionIdx, eState);
	}
}


// Show the scoreboard with the given name to all players in the entarray
AUTO_COMMAND ACMD_NAME(debugSetScoreboard);
void dbgCmd_SetScoreboard(Entity *pEnt, SA_PARAM_NN_STR const char *pcScoreboardName)
{
	mapState_SetScoreboard(entGetPartitionIdx(pEnt), pcScoreboardName);
}


// Set a map value to a specific value
AUTO_COMMAND ACMD_NAME(SetMapValue);
void dbgCmd_SetMapValue(Entity *pEnt, SA_PARAM_NN_STR const char *pcMapValue, int iValue)
{
	mapState_SetValue(entGetPartitionIdx(pEnt), pcMapValue, iValue, true);
}


