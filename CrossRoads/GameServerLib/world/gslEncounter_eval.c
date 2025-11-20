/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Character.h"
#include "encounter_common.h"
#include "entCritter.h"
#include "Entity.h"
#include "error.h"
#include "EString.h"
#include "Expression.h"
#include "file.h"
#include "GameEvent.h"
#include "gslEncounter.h"
#include "gslEntity.h"
#include "gslEventTracker.h"
#include "gslInteractable.h"
#include "gslMission.h"
#include "gslOldEncounter.h"
#include "gslOldEncounter_events.h"
#include "mission_common.h"
#include "oldencounter_common.h"
#include "StringCache.h"
#include "wlEncounter.h"
#include "mapstate_common.h"
#include "EntityLib.h"
#include "nemesis.h"

// ----------------------------------------------------------------------------------
// Encounter Entity Expressions
// ----------------------------------------------------------------------------------

// Reset an encounter (kill all entities and allow to respawn)
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(EncounterReset);
void encounter_FuncEncounterReset(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcEncounterName)
{
	WorldScope *pScope = exprContextGetScope(pContext);
	GameEncounter *pEncounter = encounter_GetByName(pcEncounterName, pScope);

	if (pEncounter) {
		GameEncounterPartitionState *pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);
		encounter_Reset(pEncounter, pState);

	} else if (gConf.bAllowOldEncounterData) {
		OldEncounter *pOldEncounter = oldencounter_FromStaticEncounterName(iPartitionIdx, pcEncounterName);

		if (pOldEncounter) {
			oldencounter_Reset(pOldEncounter);
		} else {
			ErrorFilenamef(exprContextGetBlameFile(pContext), "EncounterReset: No encounter named %s", pcEncounterName);
		}
	} else {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "EncounterReset: No encounter named %s", pcEncounterName);
	}
}


// Reset all encounters in an encounter group (kill all entities and allow to respawn)
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(EncounterGroupReset);
void encounter_FuncEncounterGroupReset(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcGroupName)
{
	WorldScope *pScope = exprContextGetScope(pContext);
	GameEncounterGroup *pGroup = encounter_GetGroupByName(pcGroupName, pScope);

	if (pGroup) {
		int i;
		for(i=eaSize(&pGroup->eaGameEncounters)-1; i>=0; --i) {
			GameEncounter *pEncounter = pGroup->eaGameEncounters[i];
			GameEncounterPartitionState *pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);
			encounter_Reset(pEncounter, pState);
		}

	} else {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "EncounterGroupReset: No encounter group named %s (only works with Encounter 2)", pcGroupName);
	}
}

// Reset all encounters (kill all entities and allow to respawn)
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(EncounterResetAll);
void encounter_FuncEncounterResetAll(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx)
{
	encounter_ResetAll(iPartitionIdx);
}

// Force an encounter to spawn (unless it has already spawned)
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(EncounterSpawn);
void encounter_FuncEncounterSpawn(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcEncounterName)
{
	WorldScope *pScope = exprContextGetScope(pContext);
	GameEncounter *pEncounter = encounter_GetByName(pcEncounterName, pScope);

	if (pEncounter) {
		GameEncounterPartitionState *pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);
		if ((pState->eState == EncounterState_Asleep) || (pState->eState == EncounterState_Waiting)) {
			encounter_SpawnEncounter(pEncounter, pState);
		}

	} else if (gConf.bAllowOldEncounterData) {
		OldEncounter *pOldEncounter = oldencounter_FromStaticEncounterName(iPartitionIdx, pcEncounterName);

		if (pOldEncounter) {
			if (oldencounter_IsWaitingToSpawn(pOldEncounter)) {
				oldencounter_Spawn(oldencounter_GetDef(pOldEncounter), pOldEncounter);
			}
		} else {
			ErrorFilenamef(exprContextGetBlameFile(pContext), "EncounterReset: No encounter named %s", pcEncounterName);
		}
	} else {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "EncounterReset: No encounter named %s", pcEncounterName);
	}
}


// Force an encounter to spawn (unless it has already spawned), with the passed in entity as the spawning player
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(EncounterSpawnForPlayer);
void encounter_FuncEncounterSpawnForPlayer(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcEncounterName, ACMD_EXPR_ENTARRAY_IN eaEntsIn)
{
	WorldScope *pScope = exprContextGetScope(pContext);
	GameEncounter *pEncounter = encounter_GetByName(pcEncounterName, pScope);
	Entity *pSpawningEnt = NULL;

	if (eaSize(eaEntsIn)) {
		pSpawningEnt = (*eaEntsIn)[0];
	}

	if (pEncounter) {
		GameEncounterPartitionState *pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);
		if ((pState->eState == EncounterState_Asleep) || (pState->eState == EncounterState_Waiting)) {
			if (pSpawningEnt) {
				pState->playerData.uSpawningPlayer = entGetRef(pSpawningEnt);
			}
			encounter_SpawnEncounter(pEncounter, pState);
		}

	} else if (gConf.bAllowOldEncounterData) {
		OldEncounter *pOldEncounter = oldencounter_FromStaticEncounterName(iPartitionIdx, pcEncounterName);

		if (pOldEncounter) {
			if (oldencounter_IsWaitingToSpawn(pOldEncounter)) {
				if (pSpawningEnt)
				{
					pOldEncounter->spawningPlayer = entGetRef(pSpawningEnt);
				}
				oldencounter_Spawn(oldencounter_GetDef(pOldEncounter), pOldEncounter);
			}
		} else {
			ErrorFilenamef(exprContextGetBlameFile(pContext), "EncounterReset: No encounter named %s", pcEncounterName);
		}
	} else {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "EncounterReset: No encounter named %s", pcEncounterName);
	}
}


// Spawn all encounters in an encounter group (unless they have already spawned)
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(EncounterGroupSpawn);
void encounter_FuncEncounterGroupSpawn(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcGroupName)
{
	WorldScope *pScope = exprContextGetScope(pContext);
	GameEncounterGroup *pGroup = encounter_GetGroupByName(pcGroupName, pScope);

	if (pGroup) {
		int i;
		for(i=eaSize(&pGroup->eaGameEncounters)-1; i>=0; --i) {
			GameEncounter *pEncounter = pGroup->eaGameEncounters[i];
			GameEncounterPartitionState *pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);
			if ((pState->eState == EncounterState_Asleep) || (pState->eState == EncounterState_Waiting)) {
				encounter_SpawnEncounter(pEncounter, pState);
			}
		}

	} else {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "EncounterGroupSpawn: No encounter group named %s (only works with Encounter 2)", pcGroupName);
	}
}


// Destroy all entities in an encounter (may not actually shut it down)
AUTO_EXPR_FUNC(encounter) ACMD_NAME(ShutDownEncounter);
int encounter_FuncShutdownEncounter(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx)
{
	GameEncounter *pEncounter = exprContextGetVarPointerUnsafePooled(pContext, g_Encounter2VarName);

	if (pEncounter) {
		// This currently just destroys all entities, but really it should force them to switch their AI to run away
		GameEncounterPartitionState *pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);
		eaDestroyEx(&pState->eaEntities, gslQueueEntityDestroy);

	} else if (gConf.bAllowOldEncounterData) {
		OldEncounter *pOldEncounter = exprContextGetVarPointerUnsafePooled(pContext, g_EncounterVarName);
		if (pOldEncounter) {
			// This currently just destroys all entities, but really it should force them to switch their AI to run away
			eaDestroyEx(&pOldEncounter->ents, gslQueueEntityDestroy);
		} else {
			ErrorFilenamef(exprContextGetBlameFile(pContext), "ShutDownEncounter: No encounter in context");
		}

	} else {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "ShutDownEncounter: No encounter in context");
	}
	return 1;
}


// Returns the number of times this encounter has spawned critters
AUTO_EXPR_FUNC(encounter) ACMD_NAME(GetNumTimesSpawned);
int encounter_FuncGetNumTimesSpawned(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx)
{
	GameEncounter *pEncounter = exprContextGetVarPointerPooled(pContext, g_Encounter2VarName, parse_GameEncounter);
	
	if (pEncounter) {
		GameEncounterPartitionState *pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);
		return pState->iNumTimesSpawned;

	} else {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GetNumTimesSpawned: No encounter in context");
	}
	return 0;
}


// Returns the percentage of entities in the current encounter that are alive
AUTO_EXPR_FUNC(encounter) ACMD_NAME(PercentLivingInThisEncounter);
int encounter_FuncPercentLivingInThisEncounter(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx)
{
	GameEncounter *pEncounter = exprContextGetVarPointerPooled(pContext, g_Encounter2VarName, parse_GameEncounter);

	if (pEncounter) {
		GameEncounterPartitionState *pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);
		int iNumLiving = encounter_GetNumLivingEnts(pEncounter, pState);
		int iNumTotal = pState->iNumEntsSpawned;

		if (iNumTotal <= 0) {
			iNumTotal = 1;
		}
		return iNumLiving * 100 / iNumTotal;

	} else if (gConf.bAllowOldEncounterData) {
		OldEncounter *pOldEncounter = exprContextGetVarPointerPooled(pContext, g_EncounterVarName, parse_OldEncounter);
		if (pOldEncounter) {
			int iNumLiving = oldencounter_NumLivingEnts(pOldEncounter);
			int iNumTotal = oldencounter_NumEntsToSpawn(pOldEncounter);

			if (iNumTotal <= 0) {
				iNumTotal = 1;
			}
			return iNumLiving * 100 / iNumTotal;
		}
	}
	return 0;
}


// Returns the percentage of entities in the passed in static encounter that are alive
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(PercentLivingInEncounter);
int encounter_FuncPercentLivingInEncounter(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcEncounterName)
{
	WorldScope *pScope = exprContextGetScope(pContext);
	GameEncounter *pEncounter = encounter_GetByName(pcEncounterName, pScope);
	if (pEncounter) {
		GameEncounterPartitionState *pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);
		int iNumLiving = encounter_GetNumLivingEnts(pEncounter, pState);
		int iNumTotal = pState->iNumEntsSpawned;

		if (iNumTotal < 1) {
			iNumTotal = 1;
		}
		return iNumLiving * 100 / iNumTotal;

	} else if (gConf.bAllowOldEncounterData) {
		OldEncounter *pOldEncounter = oldencounter_FromStaticEncounterName(iPartitionIdx, (char*)pcEncounterName);
		if (pOldEncounter) {
			int iNumLiving = oldencounter_NumLivingEnts(pOldEncounter);
			int iNumTotal = oldencounter_NumEntsToSpawn(pOldEncounter);

			if (iNumTotal <= 0) {
				iNumTotal = 1;
			}

			return iNumLiving * 100 / iNumTotal;
		}
	}
	return 0;
}


// Returns the number of living ents in the encounter group
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(GetNumLivingInEncounterGroup);
int encounter_FuncGetNumLivingInEncounterGroup(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcGroupName)
{
	WorldScope *pScope = exprContextGetScope(pContext);
	GameEncounterGroup *pGroup = encounter_GetGroupByName(pcGroupName, pScope);

	if (pGroup) {
		int i;
		int iNumLiving = 0;
		for(i=eaSize(&pGroup->eaGameEncounters)-1; i>=0; --i) {
			GameEncounter *pEncounter = pGroup->eaGameEncounters[i];
			GameEncounterPartitionState *pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);
			iNumLiving += encounter_GetNumLivingEnts(pEncounter, pState);
		}
		return iNumLiving;

	} else {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GetNumLivingInEncounterGroup: No encounter group named %s (only works with Encounter 2)", pcGroupName);
	}

	return 0;
}


static Entity *encounter_GetSpawningPlayerFromContext(Entity *pEnt, ExprContext *pContext, int iPartitionIdx)
{
	Entity *pSpawner = NULL;

	if (pEnt && pEnt->pCritter && pEnt->pCritter->spawningPlayer) {
		pSpawner = entFromEntityRef(iPartitionIdx, pEnt->pCritter->spawningPlayer);
	} 
	if (!pSpawner) {
		GameEncounter *pEncounter = exprContextGetVarPointerUnsafePooled(pContext, g_Encounter2VarName);
		if (pEncounter) {
			GameEncounterPartitionState *pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);
			if (pState->playerData.uSpawningPlayer) {
				pSpawner = entFromEntityRef(iPartitionIdx, pState->playerData.uSpawningPlayer);
			}
		}
	}
	if (!pSpawner && gConf.bAllowOldEncounterData) {
		OldEncounter *pOldEncounter = exprContextGetVarPointerUnsafePooled(pContext, g_EncounterVarName);
		if (pOldEncounter && pOldEncounter->spawningPlayer) {
			pSpawner = entFromEntityRef(iPartitionIdx, pOldEncounter->spawningPlayer);
		}
	}

	return pSpawner;
}


// Get the player who spawned a critter/encounter, if that player is targetable and alive.
// Note that this player may already have left the area or even the map.
AUTO_EXPR_FUNC(entity, ai) ACMD_NAME(GetSpawningPlayer);
void encounter_FuncGetSpawningPlayer(ACMD_EXPR_SELF Entity *pEnt, ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_OUT peaEntsOut)
{
	Entity *pPlayer = encounter_GetSpawningPlayerFromContext(pEnt, pContext, iPartitionIdx);

	if (!pPlayer && isDevelopmentMode()) {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GetSpawningPlayer: Couldn't find spawning player");
	}

	if (pPlayer) {
		ANALYSIS_ASSUME(pPlayer != NULL);
		if (entIsAlive(pPlayer) && !entCheckFlag(pPlayer, ENTITYFLAG_IGNORE)) {
			eaPush(peaEntsOut, pPlayer);
		}
	}

	devassert(eaSize(peaEntsOut) <= 1);
}


// Get the player who spawned an encounter, even if that player is dead or untargetable for some reason
AUTO_EXPR_FUNC(entity, ai) ACMD_NAME(GetSpawningPlayerAll);
void encounter_FuncGetSpawningPlayerAll(ACMD_EXPR_SELF Entity *pEnt, ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_OUT peaEntsOut)
{
	Entity *pPlayer = encounter_GetSpawningPlayerFromContext(pEnt, pContext, iPartitionIdx);

	if (!pPlayer && isDevelopmentMode()) {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GetSpawningPlayer: Couldn't find spawning player");
	}

	if (pPlayer) {
		eaPush(peaEntsOut, pPlayer);
	}

	devassert(eaSize(peaEntsOut) <= 1);
}


// Get the player who spawned an encounter, even if that player is dead or untargetable for some reason
//  This function also doesn't error (Useful for critter expressions)
AUTO_EXPR_FUNC(entity, ai) ACMD_NAME(GetSpawningPlayerSilent);
void encounter_FuncGetSpawningPlayerSilent(ACMD_EXPR_SELF Entity *pEnt, ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_OUT peaEntsOut)
{
	Entity *pPlayer = encounter_GetSpawningPlayerFromContext(pEnt, pContext, iPartitionIdx);

	if (pPlayer) {
		eaPush(peaEntsOut, pPlayer);
	}

	devassert(eaSize(peaEntsOut) <= 1);
}


SA_RET_NN_VALID static Entity***encounter_GetNearbyPlayersForEnc(ExprContext* pContext, ACMD_EXPR_PARTITION iPartitionIdx)
{
	static Entity** emptyList = NULL;
				
	Entity *pEnt = exprContextGetSelfPtr(pContext);
	GameEncounter *pEncounter = exprContextGetVarPointerUnsafePooled(pContext, g_Encounter2VarName);
	if (!pEncounter && pEnt && pEnt->pCritter && pEnt->pCritter->encounterData.pGameEncounter) {
		pEncounter = pEnt->pCritter->encounterData.pGameEncounter;
	}

	if (pEncounter) {
		return encounter_GetNearbyPlayers(iPartitionIdx, pEncounter, -1);
		
	} else if (gConf.bAllowOldEncounterData) {
		OldEncounter *pOldEncounter = exprContextGetVarPointerUnsafePooled(pContext, g_EncounterVarName);
		if (!pOldEncounter && pEnt && pEnt->pCritter && pEnt->pCritter->encounterData.parentEncounter) {
			pOldEncounter = pEnt->pCritter->encounterData.parentEncounter;
		}
		if (pOldEncounter) {
			EncounterDef *pDef = oldencounter_GetDef(pOldEncounter);
			if (pDef){
				return oldencounter_GetNearbyPlayers(pOldEncounter, pDef->spawnRadius);
			} else {
				// should never be able to get here, but it didn't error before.
				return &emptyList;
			}
		}
	}

	ErrorFilenamef(exprContextGetBlameFile(pContext), "GetNearbyPlayersForEnc: No encounter in context");
	return &emptyList;
}


// Get players within the spawn radius of the encounter
AUTO_EXPR_FUNC(encounter, ai) ACMD_NAME(GetNearbyPlayersForEnc);
void encounter_FuncGetNearbyPlayersForEnc(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_OUT peaEntsOut)
{
	Entity*** peaNearbyPlayers = encounter_GetNearbyPlayersForEnc(pContext, iPartitionIdx);
	eaPushEArray(peaEntsOut, peaNearbyPlayers);
}


// Checks if there are any nearby players for the encounter.
AUTO_EXPR_FUNC(encounter, ai) ACMD_NAME(HasNearbyPlayersForEnc);
bool encounter_FuncHasNearbyPlayersForEnc(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx)
{
	Entity*** peaNearbyPlayers = encounter_GetNearbyPlayersForEnc(pContext, iPartitionIdx);
	return eaSize(peaNearbyPlayers) != 0;
}


// Get an entarray of all ents who currently have credit for an Encounter completion
AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(GetEntsWithEncounterCredit);
ExprFuncReturnVal exprFuncGetEntsWithEncounterCredit(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_OUT peaEntsOut, ACMD_EXPR_ERRSTRING errString)
{
	GameEncounter *pEncounter = exprContextGetVarPointerUnsafePooled(pContext, g_Encounter2VarName);
	int i;
	Entity *pEnt = exprContextGetSelfPtr(pContext);

	if (!pEncounter && pEnt && pEnt->pCritter && pEnt->pCritter->encounterData.pGameEncounter) {
		pEncounter = pEnt->pCritter->encounterData.pGameEncounter;
	}

	eaClear(peaEntsOut);

	if (pEncounter) {
		GameEncounterPartitionState *pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);
		for(i=eaiSize(&pState->playerData.eauEntsWithCredit)-1; i>=0; --i) {
			Entity *pCreditEnt = entFromEntityRef(iPartitionIdx, pState->playerData.eauEntsWithCredit[i]);
			if (pCreditEnt) {
				eaPush(peaEntsOut, pCreditEnt);
			}
		}

	} else if (gConf.bAllowOldEncounterData) {
		OldEncounter *pOldEncounter = exprContextGetVarPointerUnsafePooled(pContext, g_EncounterVarName);
		if (!pOldEncounter && pEnt && pEnt->pCritter && pEnt->pCritter->encounterData.parentEncounter) {
			pOldEncounter = pEnt->pCritter->encounterData.parentEncounter;
		}

		if (pOldEncounter) {
			for(i=eaiSize(&pOldEncounter->entsWithCredit)-1; i>=0; --i) {
				Entity *pCreditEnt = entFromEntityRef(iPartitionIdx, pOldEncounter->entsWithCredit[i]);
				if (pCreditEnt) {
					eaPush(peaEntsOut, pCreditEnt);
				}
			}
		} else {
			estrPrintf(errString, "No encounter in context");
			return ExprFuncReturnError;
		}

	} else {
		estrPrintf(errString, "No encounter in context");
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}


AUTO_EXPR_FUNC(mission, encounter) ACMD_NAME(RefillHealthPow);
int encounter_FuncRefillHealthPow(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, F32 fHealth)
{
	MissionInfo *pMissionInfo;
	F32 fHealthPerc = fHealth / 100.0;
	Mission *pMission = exprContextGetVarPointerUnsafePooled(pContext, g_MissionVarName);
	OldEncounter *pOldEncounter = exprContextGetVarPointerUnsafePooled(pContext, g_EncounterVarName);
	GameEncounter *pEncounter = exprContextGetVarPointerUnsafePooled(pContext, g_Encounter2VarName);
	Entity **eaPlayers = NULL;
	int i;

	if (pMission && (pMissionInfo = pMission->infoOwner)) {
		Character *pChar = pMissionInfo->parentEnt->pChar;
		if (pChar) {
			pChar->pattrBasic->fHitPoints = pChar->pattrBasic->fHitPointsMax * fHealthPerc;
			pChar->pattrBasic->fPower = pChar->pattrBasic->fPowerMax * fHealthPerc;
		}

	} else if (pEncounter) {
		encounter_GetRewardedPlayers(iPartitionIdx, pEncounter, &eaPlayers);
		for(i=eaSize(&eaPlayers)-1; i>=0; --i) {
			Character *pChar = eaPlayers[i]->pChar;
			if (pChar) {
				pChar->pattrBasic->fHitPoints = pChar->pattrBasic->fHitPointsMax * fHealthPerc;
				pChar->pattrBasic->fPower = pChar->pattrBasic->fPowerMax * fHealthPerc;
			}
		}
		eaDestroy(&eaPlayers);

	} else if (gConf.bAllowOldEncounterData && pOldEncounter) {
		oldencounter_GetRewardedPlayers(pOldEncounter, &eaPlayers);
		for(i=eaSize(&eaPlayers)-1; i>=0; --i) {
			Character *pChar = eaPlayers[i]->pChar;
			if (pChar) {
				pChar->pattrBasic->fHitPoints = pChar->pattrBasic->fHitPointsMax * fHealthPerc;
				pChar->pattrBasic->fPower = pChar->pattrBasic->fPowerMax * fHealthPerc;
			}
		}
		eaDestroy(&eaPlayers);

	}
	return 1;
}


// ----------------------------------------------------------------------------------
// Encounter Status Expressions
// ----------------------------------------------------------------------------------

// Alias expression for following function
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(StaticEncIsComplete);
int encounter_FuncStaticEncIsComplete(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcEncounterName);

// Returns whether the specified encounter has completed
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(EncounterIsComplete);
int encounter_FuncStaticEncIsComplete(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcEncounterName)
{
	WorldScope *pScope = exprContextGetScope(pContext);
	GameEncounter *pEncounter = encounter_GetByName(pcEncounterName, pScope);
	if (pEncounter) {
		GameEncounterPartitionState *pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);
		return  (pState->eState == EncounterState_Success) ||
				(pState->eState == EncounterState_Failure) ||
				(pState->eState == EncounterState_Off);

	} else if (gConf.bAllowOldEncounterData) {
		OldEncounter *pOldEncounter = oldencounter_FromStaticEncounterName(iPartitionIdx, pcEncounterName);
		if (!pOldEncounter) {
			ErrorFilenamef(exprContextGetBlameFile(pContext), "EncounterIsComplete: No encounter named %s", pcEncounterName);
		}
		return !pOldEncounter || oldencounter_IsComplete(pOldEncounter);

	} else {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "EncounterIsComplete: No encounter named %s", pcEncounterName);
		return false;
	}
}


// Alias expression for following function
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(StaticEncSuccess);
int encounter_FuncStaticEncSuccess(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcEncounterName);

// Returns whether the specified encounter has succeeded
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(EncounterSuccess);
int encounter_FuncStaticEncSuccess(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcEncounterName)
{
	WorldScope *pScope = exprContextGetScope(pContext);
	GameEncounter *pEncounter = encounter_GetByName(pcEncounterName, pScope);
	if (pEncounter) {
		GameEncounterPartitionState *pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);
		return  (pState->eState == EncounterState_Success);

	} else if (gConf.bAllowOldEncounterData) {
		OldEncounter *pOldEncounter = oldencounter_FromStaticEncounterName(iPartitionIdx, pcEncounterName);
		if (!pOldEncounter) {
			ErrorFilenamef(exprContextGetBlameFile(pContext), "EncounterSuccess: No encounter named %s", pcEncounterName);
		}
		return !pOldEncounter || (pOldEncounter->state == EncounterState_Success);

	} else {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "EncounterSuccess: No encounter named %s", pcEncounterName);
		return false;
	}
}


// Alias expression for following function
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(StaticEncFailure);
int encounter_FuncStaticEncFailure(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcEncounterName);

// Returns whether the specified static encounter has failed
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(EncounterFailure);
int encounter_FuncStaticEncFailure(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcEncounterName)
{
	WorldScope *pScope = exprContextGetScope(pContext);
	GameEncounter *pEncounter = encounter_GetByName(pcEncounterName, pScope);
	if (pEncounter) {
		GameEncounterPartitionState *pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);
		return  (pState->eState == EncounterState_Failure);

	} else if (gConf.bAllowOldEncounterData) {
		OldEncounter *pOldEncounter = oldencounter_FromStaticEncounterName(iPartitionIdx, pcEncounterName);
		if (!pOldEncounter) {
			ErrorFilenamef(exprContextGetBlameFile(pContext), "EncounterFailure: No static encounter named %s", pcEncounterName);
		}
		return !pOldEncounter || (pOldEncounter->state == EncounterState_Failure);

	} else {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "EncounterFailure: No static encounter named %s", pcEncounterName);
		return false;
	}
}


// ----------------------------------------------------------------------------------
// Event Count Checks (Encounter & Encounter Action)
// ----------------------------------------------------------------------------------

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal encounter_FuncEventCount_StaticCheck(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT outVal, const char *pcEventName, ACMD_EXPR_ERRSTRING errString)
{
	MissionDef *pMissionDef;
	GameInteractable *pInteractable;
	EncounterTemplate *pTemplate;
	GameEncounter *pEncounter;
	EncounterDef *pEncDef;

	GameEvent *pEvent = gameevent_EventFromString(pcEventName);
	if(!pEvent)
	{
		*outVal = 0;
		estrPrintf(errString, "Invalid event: %s", pcEventName);
		return ExprFuncReturnError;
	}

	pEvent->pchEventName = allocAddString(pcEventName);

	if ((pMissionDef = exprContextGetVarPointerUnsafePooled(pContext, g_MissionDefVarName))) {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "Error: EventCount() is not supported on Missions.  Use MissionEventCount().");

	} else if (gConf.bAllowOldEncounterData && (pEncDef = exprContextGetVarPointerUnsafe(pContext, "EncounterDef"))) {
		eventtracker_AddNamedEventToList(&pEncDef->eaUnsharedTrackedEvents, pEvent, exprContextGetBlameFile(pContext));

	} else if ((pEncounter = exprContextGetVarPointerUnsafe(pContext, g_Encounter2VarName))) {
		eventtracker_AddNamedEventToList(&pEncounter->eaUnsharedTrackedEvents, pEvent, exprContextGetBlameFile(pContext));

	} else if ((pTemplate = exprContextGetVarPointerUnsafe(pContext, g_EncounterTemplateVarName))) {
		eventtracker_AddNamedEventToList(&pTemplate->eaTrackedEvents, pEvent, exprContextGetBlameFile(pContext));

	} else if ((pInteractable = exprContextGetVarPointerUnsafePooled(pContext, g_InteractableExprVarName))) {
		eventtracker_AddNamedEventToList(&pInteractable->eaUnsharedTrackedEvents, pEvent, exprContextGetBlameFile(pContext));

	} else { 
		// Add to global tracker data
		eventtracker_AddGlobalTrackedEvent(pEvent, exprContextGetBlameFile(pContext));
	}

	*outVal = 0;
	return ExprFuncReturnFinished;
}


// Get the event count for the given event.
AUTO_EXPR_FUNC(event_count) ACMD_NAME(EventCount) ACMD_EXPR_STATIC_CHECK(encounter_FuncEventCount_StaticCheck);
ExprFuncReturnVal encounter_FuncEventCount(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT outVal, ACMD_EXPR_SC_TYPE(Event) const char *pcEventName, ACMD_EXPR_ERRSTRING errString)
{
	Mission *pMission;
	GameEncounter *pEncounter;
	GameInteractable *pInteractable;
	OldEncounter *pOldEncounter;

	int iEventCount = 0;

	if ((pMission = exprContextGetVarPointerUnsafePooled(pContext, g_MissionVarName))){
		ErrorFilenamef(exprContextGetBlameFile(pContext), "Error: EventCount() is not supported on Missions.  Use MissionEventCount().");

	} else if (gConf.bAllowOldEncounterData && (pOldEncounter = exprContextGetVarPointerUnsafePooled(pContext, g_EncounterVarName))) {
		iEventCount = oldencounter_EventCount(pOldEncounter, pcEventName);

	} else if ((pEncounter = exprContextGetVarPointerUnsafePooled(pContext, g_Encounter2VarName))) {
		iEventCount = encounter_EventCount(iPartitionIdx, pEncounter, pcEventName);

	} else if ((pInteractable = exprContextGetVarPointerUnsafePooled(pContext, g_InteractableExprVarName))) {
		iEventCount = interactable_EventCount(iPartitionIdx, pInteractable, pcEventName);

	} else {
		iEventCount = eventtracker_GlobalEventCount(iPartitionIdx, pcEventName);
	}

	*outVal = iEventCount;
	return ExprFuncReturnFinished;
}


AUTO_EXPR_FUNC_STATIC_CHECK;
int encounter_FuncEventCountSinceSpawn_StaticCheck(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcEventName)
{
	EncounterTemplate *pTemplate;
	EncounterDef *pEncDef;
	GameEncounter *pEncounter;

	GameEvent *pEvent = gameevent_EventFromString(pcEventName);
	pEvent->pchEventName = allocAddString(pcEventName);

	if (pTemplate = exprContextGetVarPointerUnsafe(pContext, "EncounterTemplate")){
		eventtracker_AddNamedEventToList(&pTemplate->eaTrackedEventsSinceSpawn, pEvent, exprContextGetBlameFile(pContext));

	} else if (gConf.bAllowOldEncounterData && (pEncDef = exprContextGetVarPointerUnsafe(pContext, "EncounterDef"))){
		eventtracker_AddNamedEventToList(&pEncDef->eaUnsharedTrackedEventsSinceSpawn, pEvent, exprContextGetBlameFile(pContext));

	} else if ((pEncounter = exprContextGetVarPointerUnsafe(pContext, g_Encounter2VarName))) {
		eventtracker_AddNamedEventToList(&pEncounter->eaUnsharedTrackedEventsSinceSpawn, pEvent, exprContextGetBlameFile(pContext));

	}

	return 0;
}


AUTO_EXPR_FUNC(encounter) ACMD_NAME(EventCountSinceSpawn) ACMD_EXPR_STATIC_CHECK(encounter_FuncEventCountSinceSpawn_StaticCheck);
int encounter_FuncEventCountSinceSpawn(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_SC_TYPE(Event) const char *pcEventName)
{
	GameEncounter *pEncounter;
	OldEncounter *pOldEncounter;

	int iEventCount = 0;

	if ((pEncounter = exprContextGetVarPointerUnsafePooled(pContext, g_Encounter2VarName))) {
		iEventCount = encounter_EventCountSinceSpawn(iPartitionIdx, pEncounter, pcEventName);

	} else if (gConf.bAllowOldEncounterData && (pOldEncounter = exprContextGetVarPointerUnsafePooled(pContext, g_EncounterVarName))) {
		iEventCount = oldencounter_EventCountSinceSpawn(pOldEncounter, pcEventName);

	} else {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "EventCountSinceSpawn: No encounter in context");
	}

	return iEventCount;
}


AUTO_EXPR_FUNC_STATIC_CHECK;
int encounter_FuncEventCountSinceComplete_StaticCheck(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcEventName)
{
	EncounterTemplate *pTemplate;
	EncounterDef *pEncDef;
	GameEncounter *pEncounter;

	GameEvent *pEvent = gameevent_EventFromString(pcEventName);
	pEvent->pchEventName = allocAddString(pcEventName);

	if (pTemplate = exprContextGetVarPointerUnsafe(pContext, "EncounterTemplate")){
		eventtracker_AddNamedEventToList(&pTemplate->eaTrackedEventsSinceComplete, pEvent, exprContextGetBlameFile(pContext));

	} else if (gConf.bAllowOldEncounterData && (pEncDef = exprContextGetVarPointerUnsafe(pContext, "EncounterDef"))) {
		eventtracker_AddNamedEventToList(&pEncDef->eaUnsharedTrackedEventsSinceComplete, pEvent, exprContextGetBlameFile(pContext));

	} else if ((pEncounter = exprContextGetVarPointerUnsafe(pContext, g_Encounter2VarName))) {
		eventtracker_AddNamedEventToList(&pEncounter->eaUnsharedTrackedEventsSinceComplete, pEvent, exprContextGetBlameFile(pContext));

	}

	return 0;
}


AUTO_EXPR_FUNC(encounter) ACMD_NAME(EventCountSinceComplete) ACMD_EXPR_STATIC_CHECK(encounter_FuncEventCountSinceComplete_StaticCheck);
int encounter_FuncEventCountSinceComplete(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_SC_TYPE(Event) const char *pcEventName)
{
	GameEncounter *pEncounter;
	OldEncounter *pOldEncounter;

	int iEventCount = 0;

	if ((pEncounter = exprContextGetVarPointerUnsafePooled(pContext, g_Encounter2VarName))) {
		iEventCount = encounter_EventCountSinceComplete(iPartitionIdx, pEncounter, pcEventName);
	
	} else if (gConf.bAllowOldEncounterData && (pOldEncounter = exprContextGetVarPointerUnsafePooled(pContext, g_EncounterVarName))) {
		iEventCount = oldencounter_EventCountSinceComplete(pOldEncounter, pcEventName);

	} else {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "EventCountSinceSpawn: No encounter in context");
	}

	return iEventCount;
}

// Returns true if the team leader (or someone on the team if bUseAnyMemeber is true) could spawn a nemesis (or minion), will check players in range of the spawn
// Will return true if the leader has been checked (or whole team) and can't spawn a nemesis. The spawn code in encounter 2 will use a default nemesis in this case
// This function will set the mapstate nemesis team leader if it hasn't been set and the leader is present)
AUTO_EXPR_FUNC(encounter, ai) ACMD_NAME(NemesisTeamLeaderReady);
bool Encounter_NemesisTeamLeaderReady(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, bool bUseAnyMemeber)
{
	MapState *pState = mapState_FromPartitionIdx(iPartitionIdx);
	Entity*** eaNearbyPlayers;

	// this is the case in which we already picked out a leader and are using his nemesis
	if(pState)
	{
		if(pState->nemesisInfo.bLeaderSet)
		{
			// leader was already recorded
			return true;
		}

		if(pState->nemesisInfo.bLeaderNoNemesis)
		{
			// the leader doesn't have a nemesis and the entire team has been checked
			return true;
		}

		// try to find a player to start check for leader nemesis
		eaNearbyPlayers = encounter_GetNearbyPlayersForEnc(pContext, iPartitionIdx);

		if(eaSize(eaNearbyPlayers) > 0)
		{
			// get the entity that qualifies as leader, this will set mapstate if it succeeds (or if whole team fails)
			Entity *pLeader = Nemesis_TeamGetTeamLeader(*eaNearbyPlayers[0], bUseAnyMemeber);	

			if(pLeader || pState->nemesisInfo.bLeaderSet || pState->nemesisInfo.bLeaderNoNemesis)
			{
				// We found a qualifying member
				return true;
			}
		}
	}
	return false;
}

// Returns true if the team index could spawn a nemesis (or minion), will check players in range of the spawn
// Will return true if the team index and can't spawn a nemesis. The spawn code in encounter 2 will use a default nemesis in this case
// This function will set the mapstate nemesis team index if it hasn't been set and the leader is present.
AUTO_EXPR_FUNC(encounter, ai) ACMD_NAME(NemesisTeamIndexReady);
bool Encounter_NemesisTeamIndexReady(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, S32 iTeamIndex)
{
	MapState *pState = mapState_FromPartitionIdx(iPartitionIdx);
	Entity*** eaNearbyPlayers;

	// this is the case in which we already picked out a leader and are using his nemesis
	if(pState)
	{
		if
		(
			iTeamIndex >= 0 && iTeamIndex < eaSize(&pState->nemesisInfo.eaNemesisTeam) && 
			(pState->nemesisInfo.eaNemesisTeam[iTeamIndex]->iId > 0 || pState->nemesisInfo.eaNemesisTeam[iTeamIndex]->bNoNemesis)
		)
		{
			// index already recorded
			return true;
		}

		// try to find a player to start check for leader nemesis
		eaNearbyPlayers = encounter_GetNearbyPlayersForEnc(pContext, iPartitionIdx);

		if(eaSize(eaNearbyPlayers) > 0)
		{
			// get the entity that qualifies at team index, this will set mapstate if it succeeds or the fails due to no nemesis
			Entity *pEntity = Nemesis_TeamGetTeamIndex(*eaNearbyPlayers[0], iTeamIndex);	

			if
			(
				pEntity ||
				(
					iTeamIndex >= 0 && iTeamIndex < eaSize(&pState->nemesisInfo.eaNemesisTeam) && 
					(pState->nemesisInfo.eaNemesisTeam[iTeamIndex]->iId > 0 || pState->nemesisInfo.eaNemesisTeam[iTeamIndex]->bNoNemesis)
				)
			)
			{
				// We found a qualifying member
				return true;
			}
		}
	}
	return false;
}

// Returns timer for when encounter spawned.  Returns -1 on error (used in encounter1 or not spawned ever)
AUTO_EXPR_FUNC(encounter) ACMD_NAME(EncounterTimeSinceSpawned);
ExprFuncReturnVal exprFuncEncounterTimeSpawned(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_FLOAT_OUT timeOut, ACMD_EXPR_ERRSTRING errString)
{
	GameEncounter *pEncounter = exprContextGetVarPointerUnsafePooled(pContext, g_Encounter2VarName);
	GameEncounterPartitionState *pState = NULL;

	*timeOut = -1;
	if(!pEncounter)
	{
		estrPrintf(errString, "No encounter specified (ensure this expression is not used in Encounter1)");
		return ExprFuncReturnError;
	}

	pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);

	if(pState->timerData.uTimeLastSpawned==0)
		return ExprFuncReturnFinished;

	*timeOut = timeSecondsSince2000() - pState->timerData.uTimeLastSpawned;

	return ExprFuncReturnFinished;
}