#include "earray.h"
#include "Entity.h"
#include "EntityIterator.h"
#include "entCritter.h"
#include "Expression.h"
#include "gslPartition.h"
#include "gslPlayerMatchStats.h"
#include "Player.h"
#include "playerMatchStats_common.h"
#include "playerstats_common.h"
#include "WorldGrid.h"
#include "../StaticWorld/ZoneMap.h"
#include "mission_common.h"

#include "mission_common_h_ast.h"
#include "playerMatchStats_common_h_ast.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

typedef struct PlayerMatchStatsManager
{
	PerMatchPlayerStatList		*pPlayerMatchStats;
	PlayerMatchInfo				**eaPlayersLeftStats;
} PlayerMatchStatsManager;

static S32 g_debugPlayerMatchStats = 0;
static PlayerMatchStatsManager **g_eaPlayerMatchStatsManager = NULL;

AUTO_CMD_INT(g_debugPlayerMatchStats, PlayerMatchStatsDebug);

// ------------------------------------------------------------------------------------------
static PlayerMatchStatsManager *playermatchstats_GetManager(int iPartitionIdx, bool bNullOkay)
{
	PlayerMatchStatsManager *pManager = eaGet(&g_eaPlayerMatchStatsManager, iPartitionIdx);
	if (!bNullOkay) 
	{
		assertmsgf(pManager, "Partition %d does not exist", iPartitionIdx);
	}
	return pManager;
}

// ------------------------------------------------------------------------------------------
int playermatchstats_IsEnabled(int iPartitionIdx)
{
	return (playermatchstats_GetManager(iPartitionIdx, true) != NULL);
}

// ------------------------------------------------------------------------------------------
int playermatchstats_ShouldTrackMatchStats(void)
{
	return zmapInfoGetRecordPlayerMatchStats(NULL);
}

// ------------------------------------------------------------------------------------------
void playermatchstats_PartitionLoad(int iPartitionIdx, int bFullInit)
{
	PlayerMatchStatsManager *pManager;

	PERFINFO_AUTO_START_FUNC();

	if (!playermatchstats_ShouldTrackMatchStats())
	{
		if (g_debugPlayerMatchStats)
			printf("\nPlayerMatchStats: NOT Tracking Match Stats for this map.\n");

		PERFINFO_AUTO_STOP();
		return;
	}
	
	if (g_debugPlayerMatchStats)
		printf("PlayerMatchStats: Tracking Match Stats for this map.\n");

	// Create stats is not present, otherwise reset stats
	pManager = eaGet(&g_eaPlayerMatchStatsManager, iPartitionIdx);

	if (!pManager)
	{
		pManager = calloc(1, sizeof(PlayerMatchStatsManager));
		pManager->pPlayerMatchStats = StructCreate(parse_PerMatchPlayerStatList);
		eaSet(&g_eaPlayerMatchStatsManager, pManager, iPartitionIdx);
	}	
	else if (bFullInit)
	{
		eaDestroyStruct(&pManager->eaPlayersLeftStats, parse_PlayerMatchInfo);
		StructDestroySafe(parse_PerMatchPlayerStatList, &pManager->pPlayerMatchStats);

		pManager->pPlayerMatchStats = StructCreate(parse_PerMatchPlayerStatList);
	}

	PERFINFO_AUTO_STOP();
}

void playermatchstats_PartitionLoadWrapper(int iPartitionIdx, int *pbFullInit)
{
	playermatchstats_PartitionLoad(iPartitionIdx, *pbFullInit);
}

// ------------------------------------------------------------------------------------------
void playermatchstats_PartitionUnload(int iPartitionIdx)
{
	PlayerMatchStatsManager *pManager = eaGet(&g_eaPlayerMatchStatsManager, iPartitionIdx);
	if (pManager) 
	{
		eaDestroyStruct(&pManager->eaPlayersLeftStats, parse_PlayerMatchInfo);
		StructDestroySafe(parse_PerMatchPlayerStatList, &pManager->pPlayerMatchStats);
		free(pManager);
		eaSet(&g_eaPlayerMatchStatsManager, NULL, iPartitionIdx);
	}
}

// ------------------------------------------------------------------------------------------
void playermatchstats_MapLoad(int bFullInit)
{
	// Reset stats on any active partitions
	partition_ExecuteOnEachPartitionWithData(playermatchstats_PartitionLoadWrapper, &bFullInit);
}

// ------------------------------------------------------------------------------------------
void playermatchstats_MapUnload(void)
{
	int i;
	// Unload all partitions
	for(i=eaSize(&g_eaPlayerMatchStatsManager)-1; i>=0; --i) 
	{
		playermatchstats_PartitionUnload(i);
	}
}

// ------------------------------------------------------------------------------------------
// optionally removes from the list, but will not destroy struct
static void removeLeftPlayerMatchStatsForEnt(PlayerMatchStatsManager *pManager, Entity *pEnt)
{
	S32 i = eaSize(&pManager->eaPlayersLeftStats) - 1;

	for (; i >= 0; --i)
	{
		PlayerMatchInfo *pmi = pManager->eaPlayersLeftStats[i];
		if (pmi->iContainerID == entGetContainerID(pEnt))
		{
			StructDestroy(parse_PlayerMatchInfo, pmi);
			eaRemoveFast(&pManager->eaPlayersLeftStats, i);
		}
	}
	
}

// ------------------------------------------------------------------------------------------
static int addPlayerMatchStats(PerMatchPlayerStatList *pPerMatchStats, Entity *pPlayerEnt)
{
	PlayerMatchInfo *pmi;
	if (!pPerMatchStats || !pPlayerEnt || !pPlayerEnt->pPlayer)
		return false;

	
	pmi = playermatchstats_FindPlayerByContainerID(pPerMatchStats, entGetContainerID(pPlayerEnt));
	if (pmi)
	{	// this player entity has already been processed, update the entity ref
		PlayerMatchStatsManager *pManager = playermatchstats_GetManager(entGetPartitionIdx(pPlayerEnt), false);
		pmi->iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
		pmi->erEntity = entGetRef(pPlayerEnt);

		removeLeftPlayerMatchStatsForEnt(pManager, pPlayerEnt);
		
		return true;
	}
	
	pmi = calloc(1, sizeof(PlayerMatchInfo));
	if (!pmi)
		return false;
	eaIndexedEnable(&pmi->eaPlayerStats, parse_PlayerMatchStat);
	pmi->iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
	pmi->erEntity = entGetRef(pPlayerEnt);
	pmi->iContainerID = entGetContainerID(pPlayerEnt);
	pmi->pchPlayerName = StructAllocString(entGetLocalName(pPlayerEnt));
			

	eaPush(&pPerMatchStats->eaPlayerMatchStats, pmi);

	if (pPlayerEnt->pPlayer->pStatsInfo)
	{
		PlayerStatsInfo *pStatsInfo = pPlayerEnt->pPlayer->pStatsInfo;
		// save all the current player stats
		FOR_EACH_IN_EARRAY(pStatsInfo->eaPlayerStats, PlayerStat, pPlayerStat)
		{
			PlayerStatDef *pDef = RefSystem_ReferentFromString(g_PlayerStatDictionary,pPlayerStat->pchStatName);
			PlayerMatchStat *pms;

			if (!pDef)
			{
				if (g_debugPlayerMatchStats)
					printf("PlayerMatchStats: Could not find playerStatDef that existed previously: %s\n", pPlayerStat->pchStatName);
				continue;
			}
			if (!pDef->bPlayerPerMatchStat)
				continue;

			pms = malloc(sizeof(PlayerMatchStat));
			if (!pms) 
				return false;
			
			pms->pchStatName = pPlayerStat->pchStatName;
			pms->uValue = pPlayerStat->uValue;
			eaIndexedAdd(&pmi->eaPlayerStats, pms);
		}
		FOR_EACH_END
	}

	return true;
}


// ------------------------------------------------------------------------------------------
int playermatchstats_RegisterPlayer(Entity *pPlayerEnt)
{
	PlayerMatchStatsManager *pManager;

	PERFINFO_AUTO_START_FUNC();

	pManager = playermatchstats_GetManager(entGetPartitionIdx(pPlayerEnt), true);
	if (pManager) 
	{
		return addPlayerMatchStats(pManager->pPlayerMatchStats, pPlayerEnt);
	}
	PERFINFO_AUTO_STOP();

	return 0;
}


// ------------------------------------------------------------------------------------------
static PlayerMatchInfo* getPlayerMatchInfo(const PlayerMatchInfo *pmi, const PlayerStatsInfo *pStatsInfo)
{
	PlayerMatchInfo *pNewPMI = StructCreate(parse_PlayerMatchInfo);
	eaIndexedEnable(&pNewPMI->eaPlayerStats, parse_PlayerMatchStat);

	pNewPMI->iPartitionIdx = pmi->iPartitionIdx;
	pNewPMI->erEntity = pmi->erEntity;
	pNewPMI->iContainerID = pmi->iContainerID;
	pNewPMI->pchPlayerName = StructAllocString(pmi->pchPlayerName);
	
	// get the new stats and make them relative to just the match
	FOR_EACH_IN_EARRAY(pStatsInfo->eaPlayerStats, const PlayerStat, pPlayerStat)
	{
		U32 oldStatValue = 0;
		U32 matchValue = 0;

		PlayerStatDef *pDef = RefSystem_ReferentFromString(g_PlayerStatDictionary,pPlayerStat->pchStatName);

		if (!pDef)
		{
			if (g_debugPlayerMatchStats)
				printf("PlayerMatchStats: Could not find playerStatDef that existed previously: %s\n", pPlayerStat->pchStatName);
			continue;
		}

		if (!pDef->bPlayerPerMatchStat)
			continue;
						
		oldStatValue = playermatchstats_GetValue(pmi, pPlayerStat->pchStatName);
		matchValue = pPlayerStat->uValue - oldStatValue;
		if(matchValue) // if the match value is 0, don't bother sending - this may change if we need these 
		{
			PlayerMatchStat *pnewStat = malloc(sizeof(PlayerMatchStat));
			if (!pnewStat)
			{
				StructDestroy(parse_PlayerMatchInfo, pNewPMI);
				return NULL;
			}
			pnewStat->pchStatName = pPlayerStat->pchStatName;
			pnewStat->uValue = matchValue;
			eaIndexedAdd(&pNewPMI->eaPlayerStats, pnewStat);
		}
	}
	FOR_EACH_END

	FOR_EACH_IN_EARRAY(pmi->eaPlayerMissionsCompleted, MissionDefRef, pMissionDef)
	{
		MissionDefRef *pRef = StructClone(parse_MissionDefRef, pMissionDef);
		eaPush(&pNewPMI->eaPlayerMissionsCompleted, pRef);
	}
	FOR_EACH_END




	return pNewPMI;
}

// ------------------------------------------------------------------------------------------
static __forceinline PlayerStatsInfo* _getPlayerStats(Entity *pEnt)
{
	PlayerStatsInfo *pStats = SAFE_MEMBER2(pEnt, pPlayer, pStatsInfo);
	return pStats;
}

// ------------------------------------------------------------------------------------------
static void playerLeftRecordStats(PlayerMatchStatsManager *pmanager, Entity *pPlayerEnt)
{
	PlayerMatchInfo *pmi;
	PlayerMatchInfo *pNewPMI;
	PlayerStatsInfo *pStatsInfo;

	if (!pmanager || !pmanager->pPlayerMatchStats || !pPlayerEnt || !pPlayerEnt->pPlayer)
		return;

	removeLeftPlayerMatchStatsForEnt(pmanager, pPlayerEnt);

	pmi = playermatchstats_FindPlayerByContainerID(pmanager->pPlayerMatchStats, entGetContainerID(pPlayerEnt));
	if (!pmi)
	{	// actually an error, why was this player not added
		return;
	}

	pStatsInfo = _getPlayerStats(pPlayerEnt);
	if (!pStatsInfo)
		return;

	pNewPMI = getPlayerMatchInfo(pmi, pStatsInfo);
	if (pNewPMI)
	{
		CritterFaction *pFaction = NULL;
		pFaction = entGetFaction(pPlayerEnt);
		pNewPMI->pchFactionName = SAFE_MEMBER(pFaction, pchName);
		
		eaPush(&pmanager->eaPlayersLeftStats, pNewPMI);
	}
}

// ------------------------------------------------------------------------------------------
// the player is leaving the match, record his stats 
void playermatchstats_PlayerLeaving(Entity *pPlayerEnt)
{
	PlayerMatchStatsManager *pManager = playermatchstats_GetManager(entGetPartitionIdx(pPlayerEnt), true);
	if (pManager)
	{
		playerLeftRecordStats(pManager, pPlayerEnt);
	}
}

// ------------------------------------------------------------------------------------------
static PerMatchPlayerStatList* playermatchstats_CreateMatchStatsSnapshot(PlayerMatchStatsManager *pManager)
{
	PerMatchPlayerStatList *pNewPerMatchStats = StructCreate(parse_PerMatchPlayerStatList);
	const PerMatchPlayerStatList *preMatchServerStats = pManager->pPlayerMatchStats;
	
	if (!preMatchServerStats)
	{
		if (g_debugPlayerMatchStats)
			printf("PlayerMatchStats: Failed to create match snapshot, no prematch stats were found.\n");
		return NULL;
	}

	// get the snapshot for all the players that are still in the game
	FOR_EACH_IN_EARRAY(preMatchServerStats->eaPlayerMatchStats, const PlayerMatchInfo, pmi)
	{
		Entity *pEnt = entFromEntityRef(pmi->iPartitionIdx, pmi->erEntity);
		PlayerStatsInfo *pStatsInfo;
		PlayerMatchInfo *pNewPMI;

		if (!pEnt)
			continue;
		pStatsInfo = _getPlayerStats(pEnt);
		if (!pStatsInfo)
			continue;

		pNewPMI = getPlayerMatchInfo(pmi, pStatsInfo);

		{
			CritterFaction *pFaction = NULL;
			pFaction = entGetFaction(pEnt);
			pNewPMI->pchFactionName = SAFE_MEMBER(pFaction, pchName);
		}
					
	
		eaPush(&pNewPerMatchStats->eaPlayerMatchStats, pNewPMI);
	}
	FOR_EACH_END
	
	// now add all the snapshots of the players that have left
	FOR_EACH_IN_EARRAY(pManager->eaPlayersLeftStats, const PlayerMatchInfo, pmi)
	{
		PlayerMatchInfo *pNewPMI;

		pNewPMI = StructClone(parse_PlayerMatchInfo, pmi);
		if (pNewPMI)
			eaPush(&pNewPerMatchStats->eaPlayerMatchStats, pNewPMI);
	}	
	FOR_EACH_END
	
	return pNewPerMatchStats;
}

// ------------------------------------------------------------------------------------------
void playermatchstats_ReportCompletedMission(Entity *pPlayerEnt, Mission *pMission)
{
	PlayerMatchStatsManager *pManager = playermatchstats_GetManager(entGetPartitionIdx(pPlayerEnt), true);
	MissionDef *pMissionDef;
	PlayerMatchInfo* pMatchStats;
	MissionDefRef *pMissionDefRef;
	
	if (!pManager)
		return; 

	pMissionDef = mission_GetDef(pMission);
	if (!pMissionDef)
		return; // should not track this mission

	pMatchStats = playermatchstats_FindPlayerByEntRef(pManager->pPlayerMatchStats, entGetRef(pPlayerEnt));
	if (!pMatchStats)
		return;
	
	pMissionDefRef = StructCreate(parse_MissionDefRef);
	if (pMissionDefRef)
	{
		REF_HANDLE_SET_FROM_STRING(g_MissionDictionary, pMissionDef->name, pMissionDefRef->hMission);
		if(IS_HANDLE_ACTIVE(pMissionDefRef->hMission))
		{
			eaPush(&pMatchStats->eaPlayerMissionsCompleted, pMissionDefRef);
		}
		else
		{
			StructDestroy(parse_MissionDefRef, pMissionDefRef);
		}
	}

}

// ------------------------------------------------------------------------------------------
// Sends match stats to all currently connected players
void playermatchstats_SendMatchStatsToPlayers(int iPartitionIndex)
{
	PlayerMatchStatsManager *pManager = playermatchstats_GetManager(iPartitionIndex, true);
	if (pManager)
	{
		PerMatchPlayerStatList *pPlayerMatchStatList = playermatchstats_CreateMatchStatsSnapshot(pManager);
		Entity *pPlayerEnt;
		EntityIterator* iter;

		if (!pPlayerMatchStatList)
		{
			return;
		}

		if (g_debugPlayerMatchStats)
			printf("PlayerMatchStats: Sending stats to players.\n");

		// go through all the players on the partition and send them the per match stats
		iter = entGetIteratorSingleType(iPartitionIndex, 0, 0, GLOBALTYPE_ENTITYPLAYER);
		while (pPlayerEnt = EntityIteratorGetNext(iter))
		{
			ClientCmd_playermatchstats_RecieveMatchStats(pPlayerEnt, pPlayerMatchStatList);
			if (g_debugPlayerMatchStats)
			{
				const char *pchName = entGetLocalName(pPlayerEnt);
				printf("PlayerMatchStats: Sending stats to player: %s\n", pchName ? pchName : "noname");
			}
		}
		EntityIteratorRelease(iter);

		StructDestroy(parse_PerMatchPlayerStatList, pPlayerMatchStatList);
	}
	
}

AUTO_EXPR_FUNC(encounter_action) ACMD_NAME(SendMatchStatsToPlayers);
ExprFuncReturnVal exprFuncSendMatchStatsToPlayers(ACMD_EXPR_PARTITION iPartitionIdx)
{
	playermatchstats_SendMatchStatsToPlayers(iPartitionIdx);
	return ExprFuncReturnFinished;
}
