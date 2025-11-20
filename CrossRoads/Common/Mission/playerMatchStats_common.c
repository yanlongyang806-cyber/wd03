#include "playerMatchStats_common.h"
#include "mission_common.h"

#include "playerstats_common.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// ------------------------------------------------------------------------------------------
U32 playermatchstats_GetValue(const PlayerMatchInfo *pMatchInfo, const char *pchStatsName)
{
	if (pMatchInfo)
	{
		PlayerStat *pStat = eaIndexedGetUsingString(&pMatchInfo->eaPlayerStats, pchStatsName);
		if (pStat)
			return pStat->uValue;
	}
	return 0;
}

// ------------------------------------------------------------------------------------------
PlayerMatchInfo* playermatchstats_FindPlayerByEntRef(PerMatchPlayerStatList *pPerMatchStats, EntityRef erEnt)
{
	FOR_EACH_IN_EARRAY(pPerMatchStats->eaPlayerMatchStats, PlayerMatchInfo, pms)
		if (pms->erEntity == erEnt)
			return pms;
	FOR_EACH_END

	return NULL;

}

// ------------------------------------------------------------------------------------------
PlayerMatchInfo* playermatchstats_FindPlayerByContainerID(PerMatchPlayerStatList *pPerMatchStats, ContainerID iID)
{
	FOR_EACH_IN_EARRAY(pPerMatchStats->eaPlayerMatchStats, PlayerMatchInfo, pms)
		if (pms->iContainerID == iID)
			return pms;
	FOR_EACH_END

	return NULL;
}

#include "AutoGen/playerMatchStats_common_h_ast.c"
