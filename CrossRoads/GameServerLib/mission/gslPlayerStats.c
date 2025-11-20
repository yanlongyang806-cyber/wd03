/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gslPlayerStats.h"

#include "Entity.h"
#include "gslEventTracker.h"
#include "EString.h"
#include "GameEvent.h"
#include "GlobalTypes.h"
#include "gslSendToClient.h"
#include "MemoryPool.h"
#include "Player.h"
#include "playerstats_common.h"
#include "ResourceManager.h"
#include "team.h"
#include "NotifyEnum.h"
#include "GameStringFormat.h"
#include "gslPlayerStats_c_ast.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


// ----------------------------------------------------------------------------------
// Data types
// ----------------------------------------------------------------------------------

// This is autostructed only so structparser will manage the memory for me
// NO_AST substructs on Entities tend to not work out well
AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct PlayerStatListener
{
	int dummy; // so structparser doesn't complain

	// Callback that will occur when the stat changes
	PlayerStatUpdateFunc updateFunc;	NO_AST
	void *pData;						NO_AST

} PlayerStatListener;


static bool g_bTracking = false;

// ----------------------------------------------------------------------------
//  Forward declarations
// ----------------------------------------------------------------------------

static void playerstats_EventCallback(void *pchPooledStatName, GameEvent *ev, GameEvent *specific, int amount);

MP_DEFINE(PlayerStatListener);


// ----------------------------------------------------------------------------
//  Auto-runs/Start-up code
// ----------------------------------------------------------------------------

AUTO_RUN;
void playerstats_Init(void)
{
	MP_CREATE(PlayerStatListener, 512);
}


// ----------------------------------------------------------------------------
//  Dictionary logic
// ----------------------------------------------------------------------------

void playerstats_DictionaryChangeCB(enumResourceEventType eType, const char *pDictName, const char *pRefData, Referent pReferent, void *pUserData)
{
	PlayerStatDef *pDef = (PlayerStatDef*)pReferent;
	int i;

	// Do nothing if tracking is currently disabled
	if (!g_bTracking) {
		return;
	}

	// Tracks the Event for this stat
	if (eType == RESEVENT_RESOURCE_ADDED ||
		eType == RESEVENT_RESOURCE_MODIFIED) {
		if (pDef) {
			for(i = 0; i < eaSize(&pDef->eaEvents); i++) {
				eventtracker_StartTracking(pDef->eaEvents[i], NULL, pDef, playerstats_EventCallback, NULL);
			}
		}
	}

	if (eType == RESEVENT_RESOURCE_REMOVED ||
		eType == RESEVENT_RESOURCE_PRE_MODIFIED) {
		if (pDef) {
			for(i = 0; i < eaSize(&pDef->eaEvents); i++) {
				eventtracker_StopTracking(pDef->eaEvents[i]->iPartitionIdx, pDef->eaEvents[i], pDef);
			}
		}
	}
}


// ----------------------------------------------------------------------------
//  Functions to start/stop tracking
// ----------------------------------------------------------------------------

static void playerstats_StopTrackingAll(void)
{
	DictionaryEArrayStruct *pStruct = resDictGetEArrayStruct(g_PlayerStatDictionary);
	int i,j;
	for (i = 0; i < eaSize(&pStruct->ppReferents); i++){
		PlayerStatDef *pDef = pStruct->ppReferents[i];
		if (pDef) {
			for(j = 0; j < eaSize(&pDef->eaEvents); j++) {
				eventtracker_StopTracking(pDef->eaEvents[j]->iPartitionIdx, pDef->eaEvents[j], pDef);
			}
		}
	}
	g_bTracking = false;
}


static void playerstats_StartTrackingAll(void)
{
	DictionaryEArrayStruct *pStruct = resDictGetEArrayStruct(g_PlayerStatDictionary);
	int i,j;
	for (i = 0; i < eaSize(&pStruct->ppReferents); i++){
		PlayerStatDef *pDef = pStruct->ppReferents[i];
		if (pDef) {
			for(j = 0; j < eaSize(&pDef->eaEvents); j++) {
				assertmsgf(pDef->eaEvents[j]->iPartitionIdx == PARTITION_ANY, "All player stats events must have partition set to ANY (%d) but this one is set to %d, which is bad", PARTITION_ANY, pDef->eaEvents[j]->iPartitionIdx);
				eventtracker_StartTracking(pDef->eaEvents[j], NULL, pDef, playerstats_EventCallback, NULL);
			}
		}
	}
	g_bTracking = true;
}


// Callbacks to load/unload a map
void playerstats_MapLoad(bool bFullInit)
{
	if (bFullInit) {
		playerstats_StartTrackingAll();
	}
}


void playerstats_MapUnload(void)
{
	playerstats_StopTrackingAll();
}


// ----------------------------------------------------------------------------
//  Utilities
// ----------------------------------------------------------------------------

PlayerStat* playerstats_GetOrCreateStat(PlayerStatsInfo *pStatsInfo, const char *pchPooledStatName)
{
	if (pStatsInfo) {
		PlayerStat *pStat = eaIndexedGetUsingString(&pStatsInfo->eaPlayerStats, pchPooledStatName);

		// See if there is an entry for this stat in the "placeholder" array
		if (!pStat) {
			int index = eaIndexedFindUsingString(&pStatsInfo->eaPlayerStatPlaceholders, pchPooledStatName);
			if (index >= 0) {
				pStat = eaRemove(&pStatsInfo->eaPlayerStatPlaceholders, index);
				if (pStat) {
					eaIndexedAdd(&pStatsInfo->eaPlayerStats, pStat);
				}
			}
		}

		// If not found, create the stat
		if (!pStat) {
			pStat = StructCreate(parse_PlayerStat);
			pStat->pchStatName = pchPooledStatName;
			eaIndexedAdd(&pStatsInfo->eaPlayerStats, pStat);
		}

		return pStat;
	}
	return NULL;
}


void playerstats_SetStat(Entity *pEnt, PlayerStatsInfo *pStatsInfo, PlayerStat *pStat, U32 uNewValue)
{
	int i;
	if (pStat) {
		U32 uOldValue = pStat->uValue;
		pStat->uValue = uNewValue;

		// If the new value is 0, un-persist it
		if (uNewValue == 0) {
			if (eaFindAndRemove(&pStatsInfo->eaPlayerStats, pStat) >= 0)
				eaIndexedAdd(&pStatsInfo->eaPlayerStatPlaceholders, pStat);
		}

		// Notify listeners
		if (uNewValue != uOldValue) {
			PlayerStatDef *pStatDef;
			for (i = eaSize(&pStat->eaListeners)-1; i >= 0; --i){
				if (pStat->eaListeners[i]->updateFunc){
					pStat->eaListeners[i]->updateFunc(pStat->pchStatName, uOldValue, uNewValue, pStat->eaListeners[i]->pData);
				}
			}

			pStatDef = RefSystem_ReferentFromString(g_PlayerStatDictionary,pStat->pchStatName);
			if (pStatDef && pStatDef->bNotifyPlayerOnChange)
			{
				static char *estr = NULL;
				S32 iValueChange;
				estrClear(&estr);
				
				iValueChange = (S32)uNewValue - (S32)uOldValue;
				entFormatGameMessageKey(pEnt, &estr, "PlayerStat_Changed", 
										STRFMT_INT("statDifference", iValueChange),
										STRFMT_END);

				ClientCmd_NotifySend(pEnt,kNotifyType_PlayerStatChange,estr,pStatDef->pchName,NULL);
			}
		}

		// Set PlayerStatsInfo dirty bit
		entity_SetDirtyBit(pEnt, parse_PlayerStatsInfo, pStatsInfo, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, true);
	}
}


// ----------------------------------------------------------------------------
//  Update logic
// ----------------------------------------------------------------------------

static void playerstats_UpdateStat(Entity *pPlayerEnt, PlayerStatsInfo *pStatsInfo, const char *pchPooledStatName, int amount, PlayerStatUpdateType eType)
{
	if (pStatsInfo){
		PlayerStat *pStat = eaIndexedGetUsingString(&pStatsInfo->eaPlayerStats, pchPooledStatName);
		U32 uOldValue = pStat?pStat->uValue:0;
		S64 uNewValue = uOldValue;
		
		switch(eType){
			xcase PlayerStatUpdateType_Sum:	
				if (amount > 0) {
					uNewValue += amount;
				}

			xcase PlayerStatUpdateType_Max:
				if (amount > 0) {
					uNewValue = MAX(uOldValue, (U32)amount);
				}

			xdefault:
			{
				const char *pchUpdateType = StaticDefineIntRevLookup(PlayerStatUpdateTypeEnum, eType);
				if (pchUpdateType){
					Errorf("Error: PlayerStatUpdateType %s was not implemented", pchUpdateType);
				} else {
					Errorf("Error: Invalid PlayerStatUpdateType %d", eType);
				}
				return;
			}
		}

		// If the value is U32_MAX, it was probably caused by the bug fixed
		// in svn 79761 (or another similar bug) and it should be fixed by some
		// data fixup code when the player logs in.  To avoid confusion, I am
		// going to clamp valid values to S32_MAX for now.
		// 
		// However, if the old value was U32_MAX, I don't want to change it until
		// the data fixup code fixes it.
		
		if (uOldValue == U32_MAX) {
			return;
		}
		
		uNewValue = CLAMP(uNewValue, 0, S32_MAX);

		if ((U32)uNewValue == uOldValue) {
			return;
		}

		if (!pStat){
			pStat = playerstats_GetOrCreateStat(pStatsInfo, pchPooledStatName);
		}
		playerstats_SetStat(pPlayerEnt, pStatsInfo, pStat, (U32)uNewValue);
	}
}


static void playerstats_EventCallback(PlayerStatDef *pStatDef, GameEvent *pSubscribeEvent, GameEvent *pSpecificEvent, int amount)
{
	static Entity** eaEntsWithCredit = NULL;
	int i;

	// Look at players who are sources for the event
	// Assume they are in the right partition as the event
	if (pSubscribeEvent->tMatchSource == TriState_Yes || pSubscribeEvent->tMatchSourceTeam == TriState_Yes) {
		for (i = eaSize(&pSpecificEvent->eaSources)-1; i >= 0; --i) {
			Entity *pEnt = pSpecificEvent->eaSources[i]->pEnt;
			if (pEnt && pSpecificEvent->eaSources[i]->bIsPlayer) {

				// Add this entity if they have credit
				if (pSpecificEvent->eaSources[i]->bHasCredit || (pSpecificEvent->eaSources[i]->bHasTeamCredit && pSubscribeEvent->tMatchSourceTeam == TriState_Yes)) {
					eaPushUnique(&eaEntsWithCredit, pEnt);
				}

				// If we're using simple team matching, add all teammates
				if (!pSpecificEvent->bUseComplexTeamMatchingSource && pSubscribeEvent->tMatchSourceTeam == TriState_Yes) {
					if (team_IsMember(pEnt)) {
						Team *pTeam = GET_REF(pEnt->pTeam->hTeam);
						if (pTeam) {
							team_GetOnMapEntsUnique(entGetPartitionIdx(pEnt), &eaEntsWithCredit, pTeam, false);
						}
					}
				}
			}
		}
	}

	// Look at players who are targets for the event
	// Assume they are in the right partition as the event
	if (pSubscribeEvent->tMatchTarget == TriState_Yes || pSubscribeEvent->tMatchTargetTeam == TriState_Yes){
		for (i = eaSize(&pSpecificEvent->eaTargets)-1; i >= 0; --i){
			Entity *pEnt = pSpecificEvent->eaTargets[i]->pEnt;
			if (pEnt && pSpecificEvent->eaTargets[i]->bIsPlayer){

				// Add this entity if they have credit
				if (pSpecificEvent->eaTargets[i]->bHasCredit || (pSpecificEvent->eaTargets[i]->bHasTeamCredit && pSubscribeEvent->tMatchTargetTeam == TriState_Yes)){
					eaPushUnique(&eaEntsWithCredit, pEnt);
				}

				// If we're using simple team matching, add all teammates
				if (!pSpecificEvent->bUseComplexTeamMatchingTarget && pSubscribeEvent->tMatchTargetTeam == TriState_Yes){
					
					if (team_IsMember(pEnt)){
						Team *pTeam = GET_REF(pEnt->pTeam->hTeam);
						if (pTeam){
							team_GetOnMapEntsUnique(entGetPartitionIdx(pEnt), &eaEntsWithCredit, pTeam, false);
						}
					}
				}
			}
		}
	}

	// Update stats for all players
	for (i = eaSize(&eaEntsWithCredit)-1; i >= 0; --i){
		if (eaEntsWithCredit[i] && eaEntsWithCredit[i]->pPlayer && eaEntsWithCredit[i]->pPlayer->pStatsInfo){
			playerstats_UpdateStat(eaEntsWithCredit[i], eaEntsWithCredit[i]->pPlayer->pStatsInfo, pStatDef->pchName, amount, pStatDef->eUpdateType);
		}
	}

	eaClear(&eaEntsWithCredit);
}


// ----------------------------------------------------------------------------
//  Listener tracking logic
// ----------------------------------------------------------------------------

void playerstats_BeginTracking(PlayerStatsInfo *pStatsInfo, const char *pchPooledStatName, PlayerStatUpdateFunc updateFunc, void *pUserData)
{
   if (pStatsInfo){
	   PlayerStat *pPlayerStat = eaIndexedGetUsingString(&pStatsInfo->eaPlayerStats, pchPooledStatName);
	   PlayerStatListener *pListener = NULL;

	   if (!pPlayerStat){
		   pPlayerStat = eaIndexedGetUsingString(&pStatsInfo->eaPlayerStatPlaceholders, pchPooledStatName);
	   }
	   if (!pPlayerStat){
		   pPlayerStat = StructCreate(parse_PlayerStat);
		   pPlayerStat->pchStatName = pchPooledStatName;
		   eaIndexedAdd(&pStatsInfo->eaPlayerStatPlaceholders, pPlayerStat);
	   }
	   if (pPlayerStat){
		   pListener = MP_ALLOC(PlayerStatListener);
		   pListener->updateFunc = updateFunc;
		   pListener->pData = pUserData;
		   eaPush(&pPlayerStat->eaListeners, pListener);
	   }
   }
}


void playerstats_StopTrackingAllForListener(PlayerStatsInfo *pStatsInfo, void *pUserData)
{
	int i, j;
	if (pStatsInfo){
		for (i = eaSize(&pStatsInfo->eaPlayerStats)-1; i >=0; --i){
			PlayerStat *pStat = pStatsInfo->eaPlayerStats[i];
			for (j = eaSize(&pStat->eaListeners)-1; j >= 0; --j){
				if (pStat->eaListeners[j]->pData == pUserData){
					MP_FREE(PlayerStatListener, pStat->eaListeners[j]);
					eaRemoveFast(&pStat->eaListeners, j);
				}
			}
		}
		for (i = eaSize(&pStatsInfo->eaPlayerStatPlaceholders)-1; i >=0; --i){
			PlayerStat *pStat = pStatsInfo->eaPlayerStatPlaceholders[i];
			for (j = eaSize(&pStat->eaListeners)-1; j >= 0; --j){
				if (pStat->eaListeners[j]->pData == pUserData){
					MP_FREE(PlayerStatListener, pStat->eaListeners[j]);
					eaRemoveFast(&pStat->eaListeners, j);
				}
			}
		}
	}
}


#include "gslPlayerStats_c_ast.c"