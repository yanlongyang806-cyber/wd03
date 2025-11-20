/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Entity.h"
#include "gslPlayerStats.h"
#include "gslSendToClient.h"
#include "player.h"
#include "playerstats_common.h"


// ----------------------------------------------------------------------------
//  Debug Commands
// ----------------------------------------------------------------------------

// Resets all PlayerStats for the current player
AUTO_COMMAND ACMD_NAME(playerstats_Reset);
void playerstats_Reset(Entity *pPlayerEnt)
{
	if (pPlayerEnt && pPlayerEnt->pPlayer && pPlayerEnt->pPlayer->pStatsInfo) {
		int i;
		for (i = eaSize(&pPlayerEnt->pPlayer->pStatsInfo->eaPlayerStats)-1; i >= 0; --i) {
			playerstats_SetStat(pPlayerEnt, pPlayerEnt->pPlayer->pStatsInfo, pPlayerEnt->pPlayer->pStatsInfo->eaPlayerStats[i], 0);
		}
	}
}


// Sets the value of a specific stat
AUTO_COMMAND ACMD_NAME(playerstats_SetByName);
const char* playerstats_SetByName(Entity *pPlayerEnt, ACMD_NAMELIST("PlayerStatDef", RESOURCEDICTIONARY) const char *pchStatName, U32 uValue)
{
	PlayerStatDef *pStatDef = RefSystem_ReferentFromString(g_PlayerStatDictionary, pchStatName);
	if (!pStatDef){
		return "Invalid stat name";
	}

	if (pPlayerEnt && pPlayerEnt->pPlayer && pPlayerEnt->pPlayer->pStatsInfo){
		PlayerStat *pStat = playerstats_GetOrCreateStat(pPlayerEnt->pPlayer->pStatsInfo, pStatDef->pchName);
		playerstats_SetStat(pPlayerEnt, pPlayerEnt->pPlayer->pStatsInfo, pStat, uValue);
	}
	return "";
}


// Display all PlayerStats values for the current player
AUTO_COMMAND ACMD_NAME(playerstats_Liststats);
void playerstats_Liststats(Entity *pPlayerEnt)
{
	if (pPlayerEnt && pPlayerEnt->pPlayer && pPlayerEnt->pPlayer->pStatsInfo){
		int i;
		char *estrBuffer = NULL;
		estrStackCreate(&estrBuffer);
		for (i = eaSize(&pPlayerEnt->pPlayer->pStatsInfo->eaPlayerStats)-1; i >= 0; --i){
			estrConcatf(&estrBuffer, "%s: %u\n", pPlayerEnt->pPlayer->pStatsInfo->eaPlayerStats[i]->pchStatName, pPlayerEnt->pPlayer->pStatsInfo->eaPlayerStats[i]->uValue);
		}
		gslSendPrintf(pPlayerEnt, "%s", estrBuffer);
		estrDestroy(&estrBuffer);
	}
}

