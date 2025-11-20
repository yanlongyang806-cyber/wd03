
#include "contact_common.h"
#include "Entity.h"
#include "EntityLib.h"
#include "Expression.h"
#include "mapstate_common.h"
#include "mission_common.h"
#include "PvPGameCommon.h"
#include "WorldVariable.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static bool s_bHasAnyPausedPartition = false;


//-----------------------------------------------------------------------
//   Map value management
//-----------------------------------------------------------------------

MapStateValue* mapState_FindMapValueInArray(MapStateValue ***peaValues, const char *pcValueName)
{
	return eaIndexedGetUsingString(peaValues, pcValueName);
}


MultiVal* mapState_GetValue(MapState *pState, const char *pcValueName)
{
	if (pState && pState->pMapValues) {
		MapStateValue* pValue = mapState_FindMapValueInArray(&pState->pMapValues->eaValues, pcValueName);

		if (pValue) {
			return &pValue->mvValue;
		}
	}
	return NULL;
}


//-----------------------------------------------------------------------
//   Player value management
//-----------------------------------------------------------------------

MapStateValue* mapState_FindPlayerValue(MapState *pState, Entity *pEnt, const char *pcValueName)
{
	if (pState && pState->pPlayerValueData && pcValueName) {

		PlayerMapValues* pPlayerValues = eaIndexedGetUsingInt(&pState->pPlayerValueData->eaPlayerValues, entGetContainerID(pEnt));

		if (pPlayerValues) {
			return mapState_FindMapValueInArray(&pPlayerValues->eaValues, pcValueName);
		}
	}
	return NULL;
}


MultiVal* mapState_GetPlayerValue(MapState *pState, Entity *pPlayerEnt, const char *pcValueName)
{
	MapStateValue *pValue = mapState_FindPlayerValue(pState, pPlayerEnt, pcValueName);

	if (pValue) {
		return &pValue->mvValue;
	}

	return NULL;
}


PlayerMapValues* mapState_FindPlayerValues(int iPartitionIdx, ContainerID iContID)
{
	MapState *pState = mapState_FromPartitionIdx(iPartitionIdx);
	if (pState && pState->pPlayerValueData) {
		return eaIndexedGetUsingInt(&pState->pPlayerValueData->eaPlayerValues, iContID);
	}
	return NULL;
}


//-----------------------------------------------------------------------
//   Team value management
//-----------------------------------------------------------------------

TeamMapValues* mapState_FindTeamValues(MapState *pState, U32 uiTeamID)
{
	if (pState && pState->pTeamValueData) {
		return eaIndexedGetUsingInt(&pState->pTeamValueData->eaTeamValues, uiTeamID);
	}
	return NULL;
}


//-----------------------------------------------------------------------
//   Public map variable management
//-----------------------------------------------------------------------

WorldVariable* mapState_GetPublicVarByName(MapState *pState, const char *pcVarName) 
{
	if (pState && pState->pPublicVarData && pState->pPublicVarData->eaPublicVars) {
		int i;
		for(i=0; i<eaSize(&pState->pPublicVarData->eaPublicVars); i++) {
			if (!stricmp(pState->pPublicVarData->eaPublicVars[i]->pcName, pcVarName)) {
				return pState->pPublicVarData->eaPublicVars[i];
			}
		}
	}
	return NULL;
}


void mapState_GetAllPublicVars(MapState *pState, WorldVariable ***peaWorldVars) 
{
	if (peaWorldVars) {
		if (pState && pState->pPublicVarData) {
			eaCopy(peaWorldVars, &pState->pPublicVarData->eaPublicVars);
		} else {
			eaClearFast(peaWorldVars);
		}
	}
}


//-----------------------------------------------------------------------
//   Public shard variable management
//-----------------------------------------------------------------------

WorldVariable* mapState_GetShardPublicVar(MapState *pState, const char *pcVarName) 
{
	if (pState && pState->pPublicVarData && pcVarName && *pcVarName) {
		int i;
		for(i=eaSize(&pState->pPublicVarData->eaShardPublicVars)-1; i>=0; i--) {
			if (!stricmp(pState->pPublicVarData->eaShardPublicVars[i]->pcName, pcVarName)) {
				return pState->pPublicVarData->eaShardPublicVars[i];
			}
		}
	}
	return NULL;
}


//-----------------------------------------------------------------------
//   Sync'd dialog management
//-----------------------------------------------------------------------

SyncDialog* mapState_GetSyncDialogForTeam(MapState *pState, ContainerID uiTeamID)
{
	if (pState) {
		return eaIndexedGetUsingInt(&pState->eaSyncDialogs, uiTeamID);
	}
	return NULL;
}


//-----------------------------------------------------------------------
//   Pet targeting management
//-----------------------------------------------------------------------

const PetTargetingInfo** mapState_GetPetTargetingInfo(Entity *pOwner)
{
	if (pOwner && entIsPlayer(pOwner)) {
		int iPartitionIdx = entGetPartitionIdx(pOwner);
		MapState *pState = mapState_FromPartitionIdx(iPartitionIdx);

		if (team_IsMember(pOwner)) {
			//if the player is on a team, use the per-team list
			TeamMapValues *pTeamMapValues = mapState_FindTeamValues(pState, pOwner->pTeam->iTeamID);
			if (pTeamMapValues) {
				return pTeamMapValues->eaPetTargetingInfo;
			}
		} else {	
			//if the player is not on team, use the per-player list
			PlayerMapValues *pPlayerMapValues = mapState_FindPlayerValues(iPartitionIdx, entGetContainerID(pOwner));
			if (pPlayerMapValues) {
				return pPlayerMapValues->eaPetTargetingInfo;
			}
		}
	}

	return NULL;
}


//-----------------------------------------------------------------------
//   Scoreboard management
//-----------------------------------------------------------------------

const char* mapState_GetScoreboard(MapState *pState)
{
	return pState ? pState->matchState.pcScoreboardName : NULL;
}


ScoreboardState mapState_GetScoreboardState(MapState *pState)
{
	return pState ? pState->matchState.eState : kScoreboardState_Init;
}


U32 mapState_GetScoreboardTimer(MapState *pState)
{
	return pState ? pState->matchState.uCounterTime : 0;
}


bool mapState_IsScoreboardInCountdown(MapState *pState)
{
	return pState && pState->matchState.bCountdown;
}


bool mapState_IsScoreboardInOvertime(MapState *pState)
{
	return pState && pState->matchState.bOvertime;
}


PVPGroupGameParams ***mapState_GetScoreboardGroupDefs(MapState *pState)
{
	return pState ? &pState->matchState.ppGroupGameParams : 0;
}

DOMControlPoint ***mapState_GetGameSpecificStructs(MapState *pState)
{
	return pState ? &pState->matchState.ppGameSpecific : 0;
}

U32 mapState_GetScoreboardTotalMatchTimeInSeconds(MapState *pState)
{
	return pState ? pState->matchState.uTotalMatchTime / 60.f : 0;
}


//-----------------------------------------------------------------------
//   Open Mission management
//-----------------------------------------------------------------------

void mapState_DecodeOpenMissionRefString(const char *pcRefString, char **estrScratch, char **ppcChildName)
{
	estrCopy2(estrScratch, pcRefString);
	*ppcChildName = strstr(*estrScratch, "::");
	if (*ppcChildName) {
		**ppcChildName = 0;
		*ppcChildName += 2;
	}
}


// Finds an Open Mission matching the given RefString
OpenMission* mapState_OpenMissionFromName(MapState *pState, const char *pcMissionName)
{
	assertmsgf(pState, "Requires a valid map state");
	
	if (pcMissionName) {
		OpenMission *pOpenMission = NULL;
		Mission *pMission = NULL;
		char *pcParentName = NULL;
		char *pcChildName = NULL;

		estrStackCreate(&pcParentName);
		mapState_DecodeOpenMissionRefString(pcMissionName, &pcParentName, &pcChildName);
		pOpenMission = eaIndexedGetUsingString(&pState->eaOpenMissions, pcParentName);
		estrDestroy(&pcParentName);
		return pOpenMission;
	}
	return NULL;
}


//-----------------------------------------------------------------------
//   Power speed recharge management
//-----------------------------------------------------------------------

F32 mapState_SpeedRecharge(MapState *pState)
{
	return pState ? pState->fSpeedRecharge : 1.f;
}


//-----------------------------------------------------------------------
//   Difficulty management
//-----------------------------------------------------------------------

PlayerDifficultyIdx mapState_GetDifficulty(MapState *pState)
{
	return pState ? pState->iDifficulty : 0;
}


bool mapState_IsMapPaused(MapState *pState)
{
	return pState ? pState->bPaused : false;
}


bool mapState_IsMapPausedForPartition(int iPartitionIdx)
{
	if (iPartitionIdx == PARTITION_ENT_BEING_DESTROYED) {
		return true;
	} else if (!s_bHasAnyPausedPartition) {
		// Short out early if no partitions are paused to avoid lookup of map state
		return false;
	} else {
		MapState *pState = mapState_FromPartitionIdx(iPartitionIdx);
		return pState ? pState->bPaused : false;
	}
}


void mapState_SetHasAnyPausedPartition(bool bHasPaused)
{
	s_bHasAnyPausedPartition = bHasPaused;
}


bool mapState_ArePVPQueuesDisabled(MapState *pState)
{
	return pState ? pState->bPVPQueuesDisabled : true;
}


#include "AutoGen/mapstate_common_h_ast.c"
