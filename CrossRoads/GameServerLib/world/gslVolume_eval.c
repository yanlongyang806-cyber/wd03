/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "CharacterClass.h"
#include "Character_Combat.h"
#include "Entity.h"
#include "EString.h"
#include "Expression.h"
#include "gslMechanics.h"
#include "gslSpawnPoint.h"
#include "gslVolume.h"
#include "mission_common.h"
#include "player.h"
#include "powers.h"
#include "wlVolumes.h"
#include "WorldGrid.h"



// ----------------------------------------------------------------------------------
// Expression functions
// ----------------------------------------------------------------------------------

// Get an entarray of all ents currently inside an encounter layer volume
AUTO_EXPR_FUNC(encounter_action, ai, OpenMission) ACMD_NAME(GetEntsInVolume);
ExprFuncReturnVal exprFuncGetEntsInVolume(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_OUT peaEntsOut, const char *pcVolName, ACMD_EXPR_ERRSTRING errString)
{
	WorldScope *pScope = exprContextGetScope(pContext);
	
	if (pcVolName) {
		if (!volume_VolumeExists(pcVolName, pScope)) {
			estrPrintf(errString, "Unable to find volume: %s", pcVolName);
			return ExprFuncReturnError;
		}
		if (strnicmp(pcVolName, GROUP_UNNAMED_PREFIX, strlen(GROUP_UNNAMED_PREFIX)) == 0) {
			estrPrintf(errString, "Cannot reference temporary volume name: %s", pcVolName);
			return ExprFuncReturnError;
		}

		eaSetSize(peaEntsOut, 0);
		volume_GetEntitiesInVolume(iPartitionIdx, pcVolName, pScope, peaEntsOut, true);
	}

	return ExprFuncReturnFinished;
}


AUTO_EXPR_FUNC(encounter_action, ai, OpenMission) ACMD_NAME(GetEntsInVolumeAll);
ExprFuncReturnVal exprFuncGetEntsInVolumeAll(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_OUT peaEntsOut, const char *pcVolName, ACMD_EXPR_ERRSTRING errString)
{
	WorldScope *pScope = exprContextGetScope(pContext);
	
	if (pcVolName) {
		if(!volume_VolumeExists(pcVolName, pScope)) {
			estrPrintf(errString, "Unable to find volume: %s", pcVolName);
			return ExprFuncReturnError;
		}
		if (strnicmp(pcVolName, GROUP_UNNAMED_PREFIX, strlen(GROUP_UNNAMED_PREFIX)) == 0) {
			estrPrintf(errString, "Cannot reference temporary volume name: %s", pcVolName);
			return ExprFuncReturnError;
		}

		eaSetSize(peaEntsOut, 0);
		volume_GetEntitiesInVolume(iPartitionIdx, pcVolName, pScope, peaEntsOut, false);
	}

	return ExprFuncReturnFinished;
}


AUTO_EXPR_FUNC(player) ACMD_NAME(CurrentPlayerInVolume);
int exprCurrentPlayerInVolume(ExprContext *pContext, const char *pcVolumeName)
{
	Entity *pPlayer = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	const WorldScope *pScope = exprContextGetScope(pContext);
	return volume_IsEntityInVolumeByName(pPlayer, pcVolumeName, pScope);
}


ExprFuncReturnVal exprFuncEntCropVolumeInternal(ACMD_EXPR_ENTARRAY_IN_OUT peaEntsOut, const char *pcVolName, const WorldScope* pScope, int requireExists, ACMD_EXPR_ERRSTRING errString)
{
	if (pcVolName) {
		int i;
		GameNamedVolume *volume = volume_GetByName(pcVolName, pScope);

		if (!volume) {
			if(requireExists)
			{
				estrPrintf(errString, "Unable to find volume: %s", pcVolName);
				return ExprFuncReturnError;
			}
			eaClear(peaEntsOut);
			return ExprFuncReturnFinished;
		}
		if (strnicmp(pcVolName, GROUP_UNNAMED_PREFIX, strlen(GROUP_UNNAMED_PREFIX)) == 0) {
			estrPrintf(errString, "Cannot reference temporary volume name: %s", pcVolName);
			return ExprFuncReturnError;
		}

		for(i=eaSize(peaEntsOut)-1; i>=0; i--) {
			if(!volume_IsEntityInVolume((*peaEntsOut)[i], volume)) {
				eaRemoveFast(peaEntsOut, i);
			}
		}
	}

	return ExprFuncReturnFinished;
}


AUTO_EXPR_FUNC(ai, OpenMission) ACMD_NAME(EntCropVolumeIfExists);
ExprFuncReturnVal exprFuncEntCropVolumeIfExists(ExprContext* pContext, ACMD_EXPR_ENTARRAY_IN_OUT peaEntsOut, const char *pcVolName, ACMD_EXPR_ERRSTRING errString)
{
	const WorldScope *pScope = exprContextGetScope(pContext);
	return exprFuncEntCropVolumeInternal(peaEntsOut, pcVolName, pScope, false, errString);
}


AUTO_EXPR_FUNC(encounter_action, ai, OpenMission) ACMD_NAME(EntCropVolume);
ExprFuncReturnVal exprFuncEntCropVolume(ExprContext* pContext, ACMD_EXPR_ENTARRAY_IN_OUT peaEntsOut, const char *pcVolName, ACMD_EXPR_ERRSTRING errString)
{
	const WorldScope *pScope = exprContextGetScope(pContext);
	return exprFuncEntCropVolumeInternal(peaEntsOut, pcVolName, pScope, true, errString);
}


/// MJF - test code for scoping; these functions should be replaced by
/// the movePlayerToSpawn command.
AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprFuncWarpToVolume_Check(ExprContext *pContext, const char *pcSpawnPoint, ACMD_EXPR_ERRSTRING errEstr)
{
	WorldScope *pScope = exprContextGetScope(pContext);
	
	if (!spawnpoint_GetByName(pcSpawnPoint, pScope)) {
		estrPrintf( errEstr, "No such volume %s.", pcSpawnPoint );
		return ExprFuncReturnError;
	}
	if (strnicmp(pcSpawnPoint, GROUP_UNNAMED_PREFIX, strlen(GROUP_UNNAMED_PREFIX)) == 0) {
		estrPrintf(errEstr, "Cannot reference temporary spawn point name: %s", pcSpawnPoint);
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}


AUTO_EXPR_FUNC(player) ACMD_NAME(WarpToVolume) ACMD_EXPR_STATIC_CHECK(exprFuncWarpToVolume_Check);
ExprFuncReturnVal exprFuncWarpToVolume(ExprContext *pContext, const char *pcSpawnPoint, ACMD_EXPR_ERRSTRING errEstr)
{
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	WorldScope *pScope = exprContextGetScope(pContext);
	
	if (pPlayerEnt) {
		spawnpoint_MovePlayerToNamedSpawn(pPlayerEnt, pcSpawnPoint, pScope, false);
	}
	return ExprFuncReturnFinished;
}


// Moves all players in the ent array to given spawn point - 
//  only players because critters can't use spawn logic and players need to
AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(WarpPlayerEntArrayToSpawnPoint);
ExprFuncReturnVal exprFuncWarpEntArrayToPoint(ACMD_EXPR_ENTARRAY_IN eaEnts, const char *pcSpawnPoint, ACMD_EXPR_ERRSTRING errString)
{
	int i;

	if (!spawnpoint_SpawnPointExists(pcSpawnPoint, NULL)) {
		estrPrintf( errString, "No such spawn point %s.", pcSpawnPoint );
		return ExprFuncReturnError;
	}

	if (strnicmp(pcSpawnPoint, GROUP_UNNAMED_PREFIX, strlen(GROUP_UNNAMED_PREFIX)) == 0) {
		estrPrintf(errString, "Cannot reference temporary spawn point name: %s", pcSpawnPoint);
		return ExprFuncReturnError;
	}

	for(i=eaSize(eaEnts)-1; i>=0; i--) {
		Entity *pEnt = (*eaEnts)[i];

		if (!entIsPlayer(pEnt)) {
			continue;
		}

		spawnpoint_MovePlayerToNamedSpawn(pEnt, pcSpawnPoint, NULL, 0);
	}

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(WarpPlayerEntArrayToNearestSpawnPoint);
ExprFuncReturnVal exprFuncWarpEntArrayToNearestPoint(ACMD_EXPR_ENTARRAY_IN eaEnts)
{
	int i;
	for(i=eaSize(eaEnts)-1; i>=0; i--) {
		Entity *pEnt = (*eaEnts)[i];

		if (!entIsPlayer(pEnt)) {
			continue;
		}
		spawnpoint_MovePlayerToNearestSpawn(pEnt, true, true);
	}

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(WarpPlayerEntArrayToStartSpawnPoint);
ExprFuncReturnVal exprFuncWarpEntArrayToStartPoint(ACMD_EXPR_ENTARRAY_IN eaEnts)
{
	int i;
	for(i=eaSize(eaEnts)-1; i>=0; i--) {
		Entity *pEnt = (*eaEnts)[i];

		if (!entIsPlayer(pEnt)) {
			continue;
		}
		spawnpoint_MovePlayerToStartSpawn(pEnt,true);
	}

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC_STATIC_CHECK;
void encountereval_LoadVerify_ActivatePowerInVolume(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcPowerName, const char *pcStrength, const char *pcVolName, ACMD_EXPR_ERRSTRING errEstr)
{
	if (!ActivatePowerClassFromStrength(pcStrength)) {
		estrPrintf(errEstr, "Unknown strength used in ActivatePowerInVolume(%s, %s, %s). Valid are Small, Medium, and Large.", pcPowerName, pcStrength, pcVolName);
	}
}


// Hit every entity in a volume with a power
AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(ActivatePowerInVolume) ACMD_EXPR_STATIC_CHECK(encountereval_LoadVerify_ActivatePowerInVolume);
void exprFuncActivatePowerInVolume(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_DICT(PowerDef) const char *pcPowerName, const char *pcStrength, const char *pcVolName, ACMD_EXPR_ERRSTRING errEstr)
{
	const WorldScope *pScope = exprContextGetScope(pContext);
	PowerDef *pPowerDef = powerdef_Find(pcPowerName);

	if (pPowerDef) {
		WorldVolume **eaVolList = NULL;
		WorldVolume *pVol;
		int iLevel = mechanics_GetMapLevel(iPartitionIdx);
		Vec3 vSourcePos;

		pVol = volume_GetWorldVolume(iPartitionIdx, pcVolName, pScope);
		if (pVol) {
			eaPush(&eaVolList, pVol);
		}

		// If this is a single volume, the power comes from its center.
		// Otherwise, the power comes from "nowhere"
		if (eaSize(&eaVolList) == 1) {
			wlVolumeGetVolumeWorldMid(pVol, vSourcePos);
		} else {
			copyVec3(zerovec3, vSourcePos);
		}

		location_ApplyPowerDef(vSourcePos, iPartitionIdx, pPowerDef, 0, NULL, &eaVolList, NULL, ActivatePowerClassFromStrength(pcStrength), iLevel, 0);

		if (!eaSize(&eaVolList)) {
			ErrorFilenamef(exprContextGetBlameFile(pContext), "ActivatePowerInVolume: no volumes named %s.", pcVolName);
		}
		eaDestroy(&eaVolList);
	} else {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "ActivatePowerInVolume: couldn't find power %s", pcPowerName);
	}
}


// Clear the auto-exec optional actions for a particular volume, making eligible to be automatically executed again
AUTO_EXPR_FUNC(ai, player) ACMD_NAME(ClearAutoExecForVolume);
void exprFuncClearAutoExecForVolume(ExprContext *pContext, SA_PARAM_OP_VALID Entity* pEnt, const char* pchVolumeName) {
	int j;

	if(pEnt && pEnt->pPlayer && pEnt->pPlayer->InteractStatus.recentAutoExecuteInteractOptions.eaOptions) {
		InteractOption* pRecentOption;
		for(j=eaSize(&pEnt->pPlayer->InteractStatus.recentAutoExecuteInteractOptions.eaOptions)-1; j>=0; --j) {
			pRecentOption = pEnt->pPlayer->InteractStatus.recentAutoExecuteInteractOptions.eaOptions[j];
			if (stricmp(pRecentOption->pcVolumeName, pchVolumeName) == 0) {
				eaRemove(&pEnt->pPlayer->InteractStatus.recentAutoExecuteInteractOptions.eaOptions, j);
			}
		}
	}
}


AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(7) ACMD_NAME(ResetFirstEnterVolumeData);
void cmdResetFirstEnterVolumeData(Entity* pPlayerEnt){
	
	eaDestroy(&pPlayerEnt->pPlayer->InteractStatus.eaFirstEnterEventVolumes);
}
