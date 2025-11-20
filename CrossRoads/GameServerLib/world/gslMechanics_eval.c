/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "CharacterClass.h"
#include "Character_Combat.h"
#include "CombatGlobal.h"
#include "Entity.h"
#include "EntityIterator.h"
#include "error.h"
#include "Expression.h"
#include "GameAccountDataCommon.h"
#include "GameStringFormat.h"
#include "gslEncounter.h"
#include "gslMechanics.h"
#include "gslMission.h"
#include "gslOldEncounter.h"
#include "gslPartition.h"
#include "gslSendToClient.h"
#include "gslSpawnPoint.h"
#include "MessageExpressions.h"
#include "mission_common.h"
#include "NotifyCommon.h"
#include "oldencounter_common.h"
#include "Player.h"
#include "StringCache.h"
#include "timedeventqueue.h"
#include "wlTime.h"
#include "WorldGrid.h"

#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"


// ----------------------------------------------------------------------------------
// Time
// ----------------------------------------------------------------------------------

// Gets the server time (not to be confused with the absolute time).  0 is midnight, 12 is noon
AUTO_EXPR_FUNC(util) ACMD_NAME(GetTime);
F32 exprFuncTimeGet(void)
{
	return wlTimeGet();
}


// ----------------------------------------------------------------------------------
// Sounds
// ----------------------------------------------------------------------------------

// Play music for all players near a point
AUTO_EXPR_FUNC(encounter_action) ACMD_NAME(PlayMusicAtPoint);
void exprFuncPlayMusicAtPoint(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcSoundName, ACMD_EXPR_LOC_MAT4_IN mat4)
{
	mechanics_playMusicAtLocation(iPartitionIdx, mat4[3], pcSoundName, exprContextGetBlameFile(pContext));
}


// Clear music for all players near a point
AUTO_EXPR_FUNC(encounter_action) ACMD_NAME(ClearMusicAtPoint);
void exprFuncClearMusicAtPoint(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_LOC_MAT4_IN mat4)
{
	mechanics_clearMusicAtLocation(iPartitionIdx, mat4[3]);
}


// Replace music for all players near a point
AUTO_EXPR_FUNC(encounter_action) ACMD_NAME(ReplaceMusicAtPoint);
void exprFuncReplaceMusicAtPoint(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcSoundName, ACMD_EXPR_LOC_MAT4_IN mat4)
{
	mechanics_replaceMusicAtLocation(iPartitionIdx, mat4[3], pcSoundName, exprContextGetBlameFile(pContext));
}


// End music for all players near a point
AUTO_EXPR_FUNC(encounter_action) ACMD_NAME(EndMusicAtPoint);
void exprFuncEndMusicAtPoint(ExprContext* pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_LOC_MAT4_IN mat4)
{
	mechanics_endMusicAtLocation(iPartitionIdx, mat4[3]);
}


// Play a sound effect for all players near a point
AUTO_EXPR_FUNC(encounter_action) ACMD_NAME(PlayOneShotSoundAtPoint);
void exprFuncPlayOneShotSoundAtPoint(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcSoundName, ACMD_EXPR_LOC_MAT4_IN mat4)
{
	mechanics_playOneShotSoundAtLocation(iPartitionIdx, mat4[3], NULL, pcSoundName, exprContextGetBlameFile(pContext));
}

// Play a sound effect for the specified players at the specific point
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(PlaySoundAtPointForEnts);
void exprFuncPlaySoundAtPointForEnts(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN ents, const char* pcSoundName, ACMD_EXPR_LOC_MAT4_IN mat4)
{
	mechanics_playOneShotSoundAtLocation(iPartitionIdx, mat4[3], *ents, pcSoundName, exprContextGetBlameFile(pContext));
}

// Play a sound effect for the specified players at the specific point
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(PlaySoundForEnts);
void exprFuncPlaySoundForEnts(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN ents, const char* pcSoundName)
{
	mechanics_playOneShotSoundAtLocation(iPartitionIdx, NULL, *ents, pcSoundName, exprContextGetBlameFile(pContext));
}


// ----------------------------------------------------------------------------------
// Logout Timers
// ----------------------------------------------------------------------------------

AUTO_EXPR_FUNC(encounter_action) ACMD_NAME(ForceMissionReturn);
void exprForceMissionReturn(ExprContext *pContext, ACMD_EXPR_ENTARRAY_IN peaEnts)
{
	int i;

	for(i=eaSize(peaEnts)-1; i>=0; --i) {
		Entity *pEnt = (*peaEnts)[i];
		if (!pEnt->pPlayer) {
			ErrorFilenamef(exprContextGetBlameFile(pContext), "Passed in a non-player entity to MissionReturn");
			continue;
		}

		mechanics_LogoutTimerStart(pEnt, LogoutTimerType_MissionReturn);
	}
}


// ----------------------------------------------------------------------------------
// Map Restart
// ----------------------------------------------------------------------------------

static int exprRestartMapInternal(MissionActionDelayed *pAction)
{
	Entity *pPlayer = entFromEntityRef(pAction->iPartitionIdx, pAction->entRef);
	if (pPlayer) {
		const char *pcMapName = pAction->pData1;
		const char *pcSpawnName = pAction->pData2;
		const char *pcCurrentMapName = zmapInfoGetPublicName(NULL);
		// Make sure this is the correct map
		if ((!pcCurrentMapName || !strcmp(pcMapName, pcCurrentMapName)) &&
			(zmapInfoGetMapType(NULL) == ZMTYPE_MISSION || zmapInfoGetMapType(NULL) == ZMTYPE_OWNED || zmapInfoGetMapType(NULL) == ZMTYPE_PVP || zmapInfoGetMapType(NULL) == ZMTYPE_QUEUED_PVE)) {
			// Force InitEncounters on next tick
			g_EncounterResetOnNextTick = true;
			if (pcSpawnName && pcSpawnName[0]) {
				spawnpoint_MovePlayerToNamedSpawn(pPlayer, pcSpawnName, NULL, true);
			} else {
				spawnpoint_MovePlayerToStartSpawn(pPlayer, true);
			}
		}
	}
	free(pAction);
	return (pPlayer != NULL);
}


AUTO_EXPR_FUNC(player) ACMD_NAME(RestartMap);
int exprRestartMap(ExprContext *pContext, const char *pcMapName, const char *pcSpawnName)
{
	Entity *pPlayer = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	Mission *pMission = exprContextGetVarPointerUnsafePooled(pContext, g_MissionVarName);
	MissionActionDelayed *pAction = NULL;

	if (!pPlayer && pMission && pMission->infoOwner) {
		pPlayer = pMission->infoOwner->parentEnt;
	}

	if (pPlayer) {
		pAction = calloc(1, sizeof(MissionActionDelayed));
		pAction->iPartitionIdx = entGetPartitionIdx(pPlayer);
		pAction->entRef = pPlayer->myRef;
		pAction->pData1 = (char*)pcMapName;
		pAction->pData2 = (char*)pcSpawnName;
		pAction->actionFunc = exprRestartMapInternal;

		timedeventqueue_Set("MissionActions", pAction, timeSecondsSince2000()+5);
	}
	return 1;
}


// ----------------------------------------------------------------------------------
// Power Activation at Points
// ----------------------------------------------------------------------------------

AUTO_EXPR_FUNC_STATIC_CHECK;
void encountereval_LoadVerify_ActivatePowerPointToPoint(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcPowerName, const char *pcStrength, ACMD_EXPR_LOC_MAT4_IN mSrcPoint, ACMD_EXPR_LOC_MAT4_IN mTargetPoint, ACMD_EXPR_ERRSTRING errEstr)
{
	if (!ActivatePowerClassFromStrength(pcStrength)) {
		estrPrintf(errEstr, "Unknown strength used in ActivatePowerPointToPoint(%s, %s). Valid are Small, Medium, and Large.", pcPowerName, pcStrength);
	}
}

// Activate a power from one  point to another
AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(ActivatePowerPointToPoint) ACMD_EXPR_STATIC_CHECK(encountereval_LoadVerify_ActivatePowerPointToPoint);
void exprFuncActivatePowerPointToPoint(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_DICT(PowerDef) const char *pcPowerName, const char *pcStrength, ACMD_EXPR_LOC_MAT4_IN mSrcPoint, ACMD_EXPR_LOC_MAT4_IN mTargetPoint, ACMD_EXPR_ERRSTRING errEstr)
{
	PowerDef *pPowerDef = powerdef_Find(pcPowerName);
	if (pPowerDef) {
		int iLevel = mechanics_GetMapLevel(iPartitionIdx);
		location_ApplyPowerDef(mSrcPoint[3], iPartitionIdx, pPowerDef, 0, mTargetPoint[3], NULL, NULL, ActivatePowerClassFromStrength(pcStrength), iLevel, 0);
	} else {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "ActivatePowerPointToPoint: couldn't find power %s", pcPowerName);
	}
}


// Completely activates (as in charge, activate, deactivate) a power from one point to another
AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(ActivatePowerCompletePointToPoint) ACMD_EXPR_STATIC_CHECK(encountereval_LoadVerify_ActivatePowerPointToPoint);
void exprFuncActivatePowerCompletePointToPoint(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_DICT(PowerDef) const char *pcPowerName, const char *pcStrength, ACMD_EXPR_LOC_MAT4_IN mSrcPoint, ACMD_EXPR_LOC_MAT4_IN mTargetPoint, ACMD_EXPR_ERRSTRING errEstr)
{
	PowerDef *pPowerDef = powerdef_Find(pcPowerName);
	if (pPowerDef) {
		int iLevel = mechanics_GetMapLevel(iPartitionIdx);
		combat_GlobalActivationAdd(pPowerDef, mSrcPoint[3], iPartitionIdx, 0, mTargetPoint[3], ActivatePowerClassFromStrength(pcStrength), iLevel, 0);
	} else {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "ActivatePowerCompletePointToPoint: couldn't find power %s", pcPowerName);
	}
}

AUTO_EXPR_FUNC_STATIC_CHECK;
void encountereval_LoadVerify_ActivatePowerPointToPointDelay(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcPowerName, const char *pcStrength, ACMD_EXPR_LOC_MAT4_IN mSrcPoint, ACMD_EXPR_LOC_MAT4_IN mTargetPoint, F32 fDelay, ACMD_EXPR_ERRSTRING errEstr)
{
	if (!ActivatePowerClassFromStrength(pcStrength)) {
		estrPrintf(errEstr, "Unknown strength used in ActivatePowerPointToPoint(%s, %s). Valid are Small, Medium, and Large.", pcPowerName, pcStrength);
	}
}

// Completely activates (as in charge, activate, deactivate) a power from one point to another after a given delay
AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(ActivatePowerCompletePointToPointDelay) ACMD_EXPR_STATIC_CHECK(encountereval_LoadVerify_ActivatePowerPointToPointDelay);
void exprFuncActivatePowerCompletePointToPointDelay(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_DICT(PowerDef) const char *pcPowerName, const char *pcStrength, ACMD_EXPR_LOC_MAT4_IN mSrcPoint, ACMD_EXPR_LOC_MAT4_IN mTargetPoint, F32 fDelay, ACMD_EXPR_ERRSTRING errEstr)
{
	PowerDef *pPowerDef = powerdef_Find(pcPowerName);
	if (pPowerDef) {
		int iLevel = mechanics_GetMapLevel(iPartitionIdx);
		combat_GlobalActivationAdd(pPowerDef, mSrcPoint[3], iPartitionIdx, 0, mTargetPoint[3], ActivatePowerClassFromStrength(pcStrength), iLevel, fDelay);
	} else {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "ActivatePowerCompletePointToPoint: couldn't find power %s", pcPowerName);
	}
}


AUTO_EXPR_FUNC_STATIC_CHECK;
void encountereval_LoadVerify_ActivatePowerAtEnts(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcPowerName, const char *pcStrength, ACMD_EXPR_ENTARRAY_IN peaEnts, ACMD_EXPR_ERRSTRING errEstr)
{
	if (!ActivatePowerClassFromStrength(pcStrength)) {
		estrPrintf(errEstr, "Unknown strength used in ActivatePowerAtEnts(%s, %s, EntArray). Valid are Small, Medium, and Large.", pcPowerName, pcStrength);
	}
}


// Hit all ents in the entarray with a power from the clickable
AUTO_EXPR_FUNC(clickable) ACMD_NAME(ActivatePowerAtEnts) ACMD_EXPR_STATIC_CHECK(encountereval_LoadVerify_ActivatePowerAtEnts);
void exprFuncActivatePowerAtEnts(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_DICT(PowerDef) const char *pcPowerName, const char *pcStrength, ACMD_EXPR_ENTARRAY_IN peaEnts, ACMD_EXPR_ERRSTRING errEstr)
{
	PowerDef *pPowerDef = powerdef_Find(pcPowerName);
	if (pPowerDef) {
		WorldInteractionEntry *pInteractionEntry = exprContextGetVarPointerUnsafe(pContext, "ClickableTracker");
		if (pInteractionEntry) {
			int i;
			for (i=eaSize(peaEnts)-1; i>=0; --i) {
				EntityRef entRef = entGetRef((*peaEnts)[i]);
				int iLevel = mechanics_GetMapLevel(iPartitionIdx);
				location_ApplyPowerDef(pInteractionEntry->base_entry.bounds.world_matrix[3], iPartitionIdx, pPowerDef, entRef, NULL, NULL, NULL, ActivatePowerClassFromStrength(pcStrength), iLevel, 0);
			}
		} else {
			ErrorFilenamef(exprContextGetBlameFile(pContext), "No clickable in context of ActivatePowerAtEnts.");
		}
	} else {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "ActivatePowerAtEnts: couldn't find power %s", pcPowerName);
	}
}


AUTO_EXPR_FUNC_STATIC_CHECK;
void encountereval_LoadVerify_ActivatePowerPointAtEnts(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcPowerName, const char *pcStrength, ACMD_EXPR_LOC_MAT4_IN mSrcPoint, ACMD_EXPR_ENTARRAY_IN peaEnts, ACMD_EXPR_ERRSTRING errEstr)
{
	if (!ActivatePowerClassFromStrength(pcStrength)) {
		estrPrintf(errEstr, "Unknown strength used in ActivatePowerPointAtEnts(%s, %s, EntArray). Valid are Small, Medium, and Large.", pcPowerName, pcStrength);
	}
}


// Fire a power from a named point at an entarray
AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(ActivatePowerPointAtEnts) ACMD_EXPR_STATIC_CHECK(encountereval_LoadVerify_ActivatePowerPointAtEnts);
void exprFuncActivatePowerPointAtEnts(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_DICT(PowerDef) const char *pcPowerName, const char *pcStrength, ACMD_EXPR_LOC_MAT4_IN mSrcPoint, ACMD_EXPR_ENTARRAY_IN peaEnts, ACMD_EXPR_ERRSTRING errEstr)
{
	PowerDef *pPowerDef = powerdef_Find(pcPowerName);
	if (pPowerDef) {
		int i;
		for (i=eaSize(peaEnts)-1; i>=0; --i) {
			EntityRef entRef = entGetRef((*peaEnts)[i]);
			int iLevel = mechanics_GetMapLevel(iPartitionIdx);
			location_ApplyPowerDef(mSrcPoint[3], iPartitionIdx, pPowerDef, entRef, NULL, NULL, NULL, ActivatePowerClassFromStrength(pcStrength), iLevel, 0);
		}
	} else {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "ActivatePowerPointAtEnts: couldn't find power %s", pcPowerName);
	}
}

// Completely activates (as in charge, activate, deactivate) a power from a named point to an entarray
AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(ActivatePowerCompletePointAtEnts) ACMD_EXPR_STATIC_CHECK(encountereval_LoadVerify_ActivatePowerPointAtEnts);
void exprFuncActivatePowerCompletePointAtEnts(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_DICT(PowerDef) const char *pcPowerName, const char *pcStrength, ACMD_EXPR_LOC_MAT4_IN mSrcPoint, ACMD_EXPR_ENTARRAY_IN peaEnts, ACMD_EXPR_ERRSTRING errEstr)
{
	PowerDef *pPowerDef = powerdef_Find(pcPowerName);
	if (pPowerDef) {
		int i;
		for (i=eaSize(peaEnts)-1; i>=0; --i) {
			EntityRef entRef = entGetRef((*peaEnts)[i]);
			int iLevel = mechanics_GetMapLevel(iPartitionIdx);
			combat_GlobalActivationAdd(pPowerDef, mSrcPoint[3], iPartitionIdx, entRef, NULL, ActivatePowerClassFromStrength(pcStrength), iLevel, 0);
		}
	} else {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "ActivatePowerCompletePointAtEnts: couldn't find power %s", pcPowerName);
	}
}


AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal encountereval_LoadVerify_ActivatePowerEntToPoint(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcPowerName, const char *pcStrength, ACMD_EXPR_ENTARRAY_IN peaEnts, ACMD_EXPR_LOC_MAT4_IN mTargetPoint, ACMD_EXPR_ERRSTRING errEstr)
{
	if (!ActivatePowerClassFromStrength(pcStrength)) {
		estrPrintf(errEstr, "Unknown strength used in ActivatePowerEntToPoint(%s, %s, EntArray). Valid are Small, Medium, and Large.", pcPowerName, pcStrength);
	}

	return ExprFuncReturnFinished;
}

// Activates a power from one entity to a named point.  The entarray can only have one entity in it
AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(ActivatePowerEntAtPoint) ACMD_EXPR_STATIC_CHECK(encountereval_LoadVerify_ActivatePowerEntToPoint);
ExprFuncReturnVal exprFuncActivatePowerEntAtPoint(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_DICT(PowerDef) const char *pcPowerName, const char *pcStrength, ACMD_EXPR_ENTARRAY_IN peaEnts, ACMD_EXPR_LOC_MAT4_IN mTargetPoint, ACMD_EXPR_ERRSTRING errString)
{
	PowerDef *pPowerDef;

	if (eaSize(peaEnts) > 1) {
		estrPrintf(errString, "Only allowed to specify one entity as source for ActivatePowerEntAtPoint");
		return ExprFuncReturnError;

	} else if (eaSize(peaEnts) == 1) {
		pPowerDef = powerdef_Find(pcPowerName);
		if (pPowerDef) {
			Vec3 vSourcePos;
			int iLevel = mechanics_GetMapLevel(iPartitionIdx);

			entGetPos((*peaEnts)[0], vSourcePos);

			location_ApplyPowerDef(vSourcePos, iPartitionIdx, pPowerDef, 0, mTargetPoint[3], NULL, NULL, ActivatePowerClassFromStrength(pcStrength), iLevel, 0);
		} else {
			ErrorFilenamef(exprContextGetBlameFile(pContext), "ActivatePowerEntAtPoint: couldn't find power %s", pcPowerName);
		}
	}
	return ExprFuncReturnFinished;
}


AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal encountereval_LoadVerify_ActivatePowerEnts(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcPowerName, const char *pcStrength, ACMD_EXPR_ENTARRAY_IN peaEnts, ACMD_EXPR_ERRSTRING errEstr)
{
	if (!ActivatePowerClassFromStrength(pcStrength)) {
		estrPrintf(errEstr, "Unknown strength used in ActivatePowerEnts(%s, %s, EntArray). Valid are Small, Medium, and Large.", pcPowerName, pcStrength);
	}

	return ExprFuncReturnFinished;
}

// Activates a power on all entities, targeting themselves
AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(ActivatePowerEnts) ACMD_EXPR_STATIC_CHECK(encountereval_LoadVerify_ActivatePowerEnts);
ExprFuncReturnVal exprFuncActivatePowerEnts(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_DICT(PowerDef) const char *pcPowerName, const char *pcStrength, ACMD_EXPR_ENTARRAY_IN peaEnts, ACMD_EXPR_ERRSTRING errString)
{
	PowerDef *pPowerDef = powerdef_Find(pcPowerName);
	if (pPowerDef) {
		int i;
		for (i=eaSize(peaEnts)-1; i>=0; --i) {
			Entity *e = (*peaEnts)[i];
			if(e && e->pChar) {
				GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
				character_ApplyUnownedPowerDefToSelf(iPartitionIdx, e->pChar, pPowerDef, pExtract);
			}
		}
	} else {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "ActivatePowerEnts: couldn't find power %s", pcPowerName);
	}
	return ExprFuncReturnFinished;
}


// ----------------------------------------------------------------------------------
// Sending Floaters
// ----------------------------------------------------------------------------------

void mechanics_SendFloaterMsgInternal(ExprContext *pContext, Entity **eaPlayerEnts, Entity *pTarget, const char* pcMsgKey, int intArg, int bFormat)
{
	int i;
	static char *estrTextLanguages[LANGUAGE_MAX] = {NULL};
	static char *estrFormatted = NULL;
	Message *pMessage = RefSystem_ReferentFromString(gMessageDict, pcMsgKey);

	if (!pMessage) {
		return;
	}

	for(i=0; i<LANGUAGE_MAX; i++) {
		estrClear(&estrTextLanguages[i]);
	}

	// Say the message to all nearby players in their language
	for(i=eaSize(&eaPlayerEnts)-1; i>=0; --i) {
		int iLangID;
		if (!eaPlayerEnts[i]->pPlayer) {
			continue;
		}

		iLangID = entGetLanguage(eaPlayerEnts[i]);

		if (!estrTextLanguages[iLangID] || !estrTextLanguages[iLangID][0]) {
			// Only translate once per language
			if (bFormat) {
				const char *pcTranslated = langTranslateMessage(iLangID, pMessage);
				estrCopy2(&estrTextLanguages[iLangID], pcTranslated);
			} else {
				exprLangTranslate(&estrTextLanguages[iLangID], pContext, iLangID, pcMsgKey, NULL);
			}
		}

		if (estrTextLanguages[iLangID] && estrTextLanguages[iLangID][0]) {
			estrClear(&estrFormatted);

			// Format text
			if (bFormat) {
				Entity *pMapOwner = partition_GetPlayerMapOwner(entGetPartitionIdx(eaPlayerEnts[i]));

				langFormatGameString(iLangID, &estrFormatted, estrTextLanguages[iLangID], 
									STRFMT_ENTITY_KEY("Entity", eaPlayerEnts[i]), 
									STRFMT_ENTITY_KEY("Target", pTarget),
									STRFMT_ENTITY_KEY("MapOwner", pMapOwner),
									STRFMT_INT("Integer",intArg),
									STRFMT_END);

				ClientCmd_NotifySend(eaPlayerEnts[i], kNotifyType_LegacyFloaterMsg, estrFormatted, NULL, NULL);
			} else {
				ClientCmd_NotifySend(eaPlayerEnts[i], kNotifyType_LegacyFloaterMsg, estrTextLanguages[iLangID], NULL, NULL);
			}
		}
	}	
}


void mechanics_FloaterGatherEnts(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, Entity ***eaEntsOut)
{
	Mission *pMission = exprContextGetVarPointerUnsafePooled(pContext, g_MissionVarName);
	OldEncounter *pOldEncounter = exprContextGetVarPointerUnsafePooled(pContext, g_EncounterVarName);
	GameEncounter *pEncounter = exprContextGetVarPointerUnsafePooled(pContext, g_Encounter2VarName);
	Entity *pEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);

	// If there's a player in the context, or a mission, or an encounter, send it to the right players
	// Otherwise, it goes to everyone on the partition!
	if (pEnt) {
		eaPush(eaEntsOut, pEnt);
	} else if (pMission && pMission->infoOwner && pMission->infoOwner->parentEnt) {
		eaPush(eaEntsOut, pMission->infoOwner->parentEnt);
	} else if (gConf.bAllowOldEncounterData && pOldEncounter) {
		oldencounter_GetRewardedPlayers(pOldEncounter, eaEntsOut);
	} else if (pEncounter) {
		encounter_GetRewardedPlayers(iPartitionIdx, pEncounter, eaEntsOut);
	} else {
		Entity *pCurrEnt;
		EntityIterator *pIter = entGetIteratorSingleType(iPartitionIdx, 0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
		while ((pCurrEnt = EntityIteratorGetNext(pIter))) {
			eaPush(eaEntsOut, pCurrEnt);
		}
		EntityIteratorRelease(pIter);
	}
}


// Sends a floater message of specified <floatText> and colors (<r> <g> <b>) to whichever players
// make sense for the current context
// If this is called from a mission with an owner, it will only send it to the current owner
// If this is called from an encounter, it'll send it to everyone who will get a reward for the current encounter
// Otherwise, it'll send the floater to every player on the map
AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(SendFloaterMsg);
int exprSendFloaterMsg(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_DICT(message) const char *pcMsgKey, int r, int g, int b)
{
	char *estrText = NULL;
	
	if (pcMsgKey && RefSystem_ReferentFromString(gMessageDict, pcMsgKey)) {
		Entity **eaPlayers = NULL;

		mechanics_FloaterGatherEnts(pContext, iPartitionIdx, &eaPlayers);
		mechanics_SendFloaterMsgInternal(pContext, eaPlayers, NULL, pcMsgKey, 0, false);

		eaDestroy(&eaPlayers);
	} else {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "Missing or unknown message key: %s", pcMsgKey ? pcMsgKey : "Missing message key");
	}

	estrDestroy(&estrText);
	return 1;
}


// Like SendFloaterMsg, but game formatted (meaning {Entity.Name} etc. are legal) and thus more expensive
AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(SendFloaterMsgGameFormatted);
int exprSendFloaterMsgGameFormatted(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_DICT(message) const char *pcMsgKey, int r, int g, int b)
{
	char *estrText = NULL;

	if (pcMsgKey && RefSystem_ReferentFromString(gMessageDict, pcMsgKey)) {
		Entity **eaPlayers = NULL;

		mechanics_FloaterGatherEnts(pContext, iPartitionIdx, &eaPlayers);
		mechanics_SendFloaterMsgInternal(pContext, eaPlayers, NULL, pcMsgKey, 0, true);
		eaDestroy(&eaPlayers);
	} else {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "Missing or unknown message key: %s", pcMsgKey ? pcMsgKey : "Missing message key");
	}

	estrDestroy(&estrText);
	return 1;
}


// Like SendFloaterMsg, but has an integer argument passed in (meaning {Integer} is legal)
AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(SendFloaterMsgToEntsWithInt);
int exprSendFloaterMsgWithInt(ExprContext *pContext, ACMD_EXPR_ENTARRAY_IN peaEnts, ACMD_EXPR_DICT(Message) const char *pcMsgKey, int integer, int r, int g, int b)
{
	char *estrText = NULL;

	if (pcMsgKey && RefSystem_ReferentFromString(gMessageDict, pcMsgKey)) {
		mechanics_SendFloaterMsgInternal(pContext, *peaEnts, NULL, pcMsgKey, integer, true);
	} else {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "Missing or unknown message key: %s", pcMsgKey ? pcMsgKey : "Missing message key");
	}

	estrDestroy(&estrText);
	return 1;
}


// Like SendFloaterMsg, but takes an entarray of ents who should hear this message
AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(SendFloaterMsgToEnts);
int exprSendFloaterMsgToEnts(ExprContext *pContext, ACMD_EXPR_ENTARRAY_IN peaEnts, ACMD_EXPR_DICT(message) const char *pcMsgKey, int r, int g, int b)
{
	char *estrText = NULL;
	
	if (pcMsgKey && RefSystem_ReferentFromString(gMessageDict, pcMsgKey)) {
		mechanics_SendFloaterMsgInternal(pContext, *peaEnts, NULL, pcMsgKey, 0, false);
	} else {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "Missing or unknown message key: %s", pcMsgKey ? pcMsgKey : "Missing message key");
	}

	estrDestroy(&estrText);
	return 1;
}


// Like SendFloaterMsg, but takes an entarray of ents who should hear this message
//   This message is formated, so it is more expensive, use wisely
AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(SendFloaterMsgToEntsGameFormatted);
int exprSendFloaterMsgToEntsGameFormatted(ExprContext *pContext, ACMD_EXPR_ENTARRAY_IN peaEnts, ACMD_EXPR_DICT(message) const char *pcMsgKey, int r, int g, int b)
{
	char *estrText = NULL;

	if (pcMsgKey && RefSystem_ReferentFromString(gMessageDict, pcMsgKey)) {
		mechanics_SendFloaterMsgInternal(pContext, *peaEnts, NULL, pcMsgKey, 0, true);
	} else {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "Missing or unknown message key: %s", pcMsgKey ? pcMsgKey : "Missing message key");
	}

	estrDestroy(&estrText);
	return 1;
}


// Like SendFloaterMsg, but takes an entarray (of a single ent) and says something about them
AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(SendFloaterMsgAbout);
ExprFuncReturnVal exprSendFloaterMsgAbout(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_DICT(message) const char *pcMsgKey, int r, int g, int b, ACMD_EXPR_ENTARRAY_IN eaEntAbout, ACMD_EXPR_ERRSTRING errString)
{
	Entity **eaPlayers = NULL;

	if(!eaSize(eaEntAbout) || eaSize(eaEntAbout)>1)
	{
		estrPrintf(errString, "SendFloaterMsgAbout requires one and only one ent (got %d)", eaSize(eaEntAbout));
		return ExprFuncReturnError;
	}

	mechanics_FloaterGatherEnts(pContext, iPartitionIdx, &eaPlayers);
	mechanics_SendFloaterMsgInternal(pContext, eaPlayers, (*eaEntAbout)[0], pcMsgKey, 0, true);
	eaDestroy(&eaPlayers);

	return ExprFuncReturnFinished;
}


// Like SendFloaterMsg, but takes an entarray (of a single ent) and says something about them to specified ents
AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(SendFloaterMsgToEntsAbout);
ExprFuncReturnVal exprSendFloaterMsgToEntsAbout(ExprContext *pContext, ACMD_EXPR_ENTARRAY_IN peaEnts, ACMD_EXPR_DICT(message) const char *pcMsgKey, int r, int g, int b, ACMD_EXPR_ENTARRAY_IN eaEntAbout, ACMD_EXPR_ERRSTRING errString)
{
	if(!eaSize(eaEntAbout) || eaSize(eaEntAbout)>1)
	{
		estrPrintf(errString, "SendFloaterMsgAbout requires one and only one ent (got %d)", eaSize(eaEntAbout));
		return ExprFuncReturnError;
	}

	mechanics_SendFloaterMsgInternal(pContext, *peaEnts, (*eaEntAbout)[0], pcMsgKey,0, true);

	return ExprFuncReturnFinished;
}


AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprFuncSCSendExternMessageFloater(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx,
													 const char *pcCategory, const char *pcName, int r, int g, int b,
													 ACMD_EXPR_ERRSTRING_STATIC errString)
{
	if (ExprFuncReturnFinished == exprContextExternVarSC(pContext, pcCategory, pcName, NULL, NULL, MULTI_STRING, "message", true, errString)) {
		return ExprFuncReturnFinished;
	} else {
		return ExprFuncReturnError;
	}
}


AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprFuncSCSendExternMessageFloaterToEnts(ExprContext *pContext, ACMD_EXPR_ENTARRAY_IN peaEnts,
														   const char *pcCategory, const char *pcName, int r, int g, int b,
														   ACMD_EXPR_ERRSTRING_STATIC errString)
{
	if (ExprFuncReturnFinished == exprContextExternVarSC(pContext, pcCategory, pcName, NULL, NULL, MULTI_STRING, "message", true, errString)) {
		return ExprFuncReturnFinished;
	} else {
		return ExprFuncReturnError;
	}
}


// Sends a floater message from the <category> category with tag <name> and colors (<r> <g> <b>)
// to whichever players make sense for the current context
// If this is called from a mission with an owner, it will only send it to the current owner
// If this is called from an encounter, it'll send it to everyone who will get a reward for the current encounter
// Otherwise, it'll send the floater to every player on the map
AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(SendExternMessageFloater) ACMD_EXPR_STATIC_CHECK(exprFuncSCSendExternMessageFloater);
ExprFuncReturnVal exprFuncSendExternMessageFloater(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx,
												   const char *pcCategory, const char *pcName, int r, int g, int b,
												   ACMD_EXPR_ERRSTRING_STATIC errString)
{
	MultiVal mvAnswer = {0};
	ExprFuncReturnVal eRetval;

	eRetval = exprContextGetExternVar(pContext, pcCategory, pcName, MULTI_STRING, &mvAnswer, errString);
	if (eRetval == ExprFuncReturnFinished && mvAnswer.str[0]) {
		exprSendFloaterMsg(pContext, iPartitionIdx, mvAnswer.str, r, g, b);
	}
	return eRetval;
}


// Like SendExternMessageFloater, but gameformatted and more expensive, use only when necessary
AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(SendExternMessageFloaterGameFormatted) ACMD_EXPR_STATIC_CHECK(exprFuncSCSendExternMessageFloater);
ExprFuncReturnVal exprFuncSendExternMessageFloaterGameFormatted(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx,
												   const char *pcCategory, const char *pcName, int r, int g, int b,
												   ACMD_EXPR_ERRSTRING_STATIC errString)
{
	MultiVal mvAnswer = {0};
	ExprFuncReturnVal eRetval;

	eRetval = exprContextGetExternVar(pContext, pcCategory, pcName, MULTI_STRING, &mvAnswer, errString);
	if (eRetval == ExprFuncReturnFinished && mvAnswer.str[0]) {
		exprSendFloaterMsgGameFormatted(pContext, iPartitionIdx, mvAnswer.str, r, g, b);
	}
	return eRetval;
}


AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(SendExternMessageFloaterToEnts) ACMD_EXPR_STATIC_CHECK(exprFuncSCSendExternMessageFloaterToEnts);
ExprFuncReturnVal exprFuncSendExternMessageFloaterToEnts(ExprContext *pContext, ACMD_EXPR_ENTARRAY_IN peaEnts,
														 const char *pcCategory, const char *pcName, int r, int g, int b,
														 ACMD_EXPR_ERRSTRING_STATIC errString)
{
	MultiVal mvAnswer = {0};
	ExprFuncReturnVal eRetval;

	eRetval = exprContextGetExternVar(pContext, pcCategory, pcName, MULTI_STRING, &mvAnswer, errString);
	if (eRetval == ExprFuncReturnFinished && mvAnswer.str[0]) {
		exprSendFloaterMsgToEnts(pContext, peaEnts, mvAnswer.str, r, g, b);
	}
	return eRetval;
}


AUTO_EXPR_FUNC(encounter_action, ai) ACMD_NAME(SendExternMessageFloaterToEntsGameFormatted) ACMD_EXPR_STATIC_CHECK(exprFuncSCSendExternMessageFloaterToEnts);
ExprFuncReturnVal exprFuncSendExternMessageFloaterToEntsGameFormatted(ExprContext *pContext, ACMD_EXPR_ENTARRAY_IN peaEnts,
														 const char *pcCategory, const char *pcName, int r, int g, int b,
														 ACMD_EXPR_ERRSTRING_STATIC errString)
{
	MultiVal mvAnswer = {0};
	ExprFuncReturnVal eRetval;

	eRetval = exprContextGetExternVar(pContext, pcCategory, pcName, MULTI_STRING, &mvAnswer, errString);
	if (eRetval == ExprFuncReturnFinished && mvAnswer.str[0]) {
		exprSendFloaterMsgToEntsGameFormatted(pContext, peaEnts, mvAnswer.str, r, g, b);
	}
	return eRetval;
}

