/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aiMovement.h"
#include "aiStruct.h"
#include "Character.h"
#include "CommandQueue.h"
#include "earray.h"
#include "EntityGrid.h"
#include "EntityLib.h"
#include "error.h"
#include "EString.h"
#include "expression.h"
#include "GameAccountDataCommon.h"
#include "GameEvent.h"
#include "gslChatConfig.h"
#include "gslEventSend.h"
#include "gslEventTracker.h"
#include "gslInteractable.h"
#include "gslMapState.h"
#include "gslMission.h"
#include "gslMissionEvents.h"
#include "gslMissionLockout.h"
#include "gslMission_transact.h"
#include "gslOldEncounter.h"
#include "gslOldEncounter_events.h"
#include "gslOpenMission.h"
#include "Guild.h"
#include "itemcommon.h"
#include "itemTransaction.h"
#include "mapstate_common.h"
#include "mission_common.h"
#include "oldencounter_common.h"
#include "Player.h"
#include "playerstats_common.h"
#include "ResourceInfo.h"
#include "Reward.h"
#include "rewardCommon.h"
#include "StateMachine.h"
#include "stringcache.h"
#include "team.h"
#include "textparser.h"
#include "timing.h"
#include "WorldLib.h"
#include "WorldVariable.h"

#include "GameEvent_h_ast.h"
#include "AutoGen/gslOldEncounter_h_ast.h"
#include "AutoGen/chatCommon_h_ast.h"
#include "AutoGen/mission_common_h_ast.h"
#include "AutoGen/SoundLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/itemEnums_h_ast.h"


// ----------------------------------------------------------------------------------
// General Mission (Mission)
// ----------------------------------------------------------------------------------

// Returns TRUE if the timer for this mission has expired
AUTO_EXPR_FUNC(mission,OpenMission) ACMD_NAME(TimeExpired);
int mission_FuncTimeExpired(ExprContext *pContext)
{
	Mission *pMission = exprContextGetVarPointerUnsafePooled(pContext, g_MissionVarName);
	int iTimeLeft = 0;
	if (pMission) {
		MissionDef *pDef = mission_GetDef(pMission);
		iTimeLeft = mission_TimeRemaining(pDef, pMission);
	} else {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "TimeExpired: No Mission in context");
	}
	return !iTimeLeft;
}


// ----------------------------------------------------------------------------------
// Mission Events (Mission)
// ----------------------------------------------------------------------------------

AUTO_EXPR_FUNC_STATIC_CHECK;
int mission_FuncMissionEventCount_StaticCheck(ExprContext *pContext, const char *pcMissionEventName)
{
	MissionDef *pMissionDef;
	if ((pMissionDef = exprContextGetVarPointerUnsafePooled(pContext, g_MissionDefVarName))) {
		int i, n = eaSize(&pMissionDef->eaTrackedEvents);
		for (i = 0; i < n; i++){
			if (pMissionDef->eaTrackedEvents[i]->pchEventName && pcMissionEventName && !stricmp(pMissionDef->eaTrackedEvents[i]->pchEventName, pcMissionEventName)){
				break;
			}
		}
		if (i == n){
			// Matching Event not found
			ErrorFilenamef(exprContextGetBlameFile(pContext), "Error: Could not find Mission Event matching '%s'", pcMissionEventName);
		}
	} else {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "Error: MissionEventCount() must be used on a Mission!");
	}
	return 0;
}


// Get the event count for the given event.
AUTO_EXPR_FUNC(mission,OpenMission) ACMD_NAME(MissionEventCount) ACMD_EXPR_STATIC_CHECK(mission_FuncMissionEventCount_StaticCheck);
int mission_FuncMissionEventCount(ExprContext *pContext, const char *pcMissionEventName)
{
	Mission *pMission = exprContextGetVarPointerUnsafePooled(pContext, g_MissionVarName);

	if (pMission) {
		return missionevent_EventCount(pMission, pcMissionEventName);
	}
	return 0;
}


AUTO_EXPR_FUNC_STATIC_CHECK;
int mission_FuncMissionDeprecatedEventCount_StaticCheck(ExprContext *pContext, const char *pcMissionEventName)
{
	MissionDef *pMissionDef = exprContextGetVarPointerUnsafePooled(pContext, g_MissionDefVarName);
	if (pMissionDef) {
		// Skip validation
	} else {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "Error: MissionDeprecatedEventCount() must be used on a Mission!");
	}
	return 0;
}


// Get the event count for the given event.  This version doesn't verify that the event actually exists,
// which is useful in a few situations where the data has changed.
AUTO_EXPR_FUNC(mission) ACMD_NAME(MissionDeprecatedEventCount) ACMD_EXPR_STATIC_CHECK(mission_FuncMissionDeprecatedEventCount_StaticCheck);
int mission_FuncMissionDeprecatedEventCount(ExprContext *pContext, const char *pcMissionEventName)
{
	Mission *pMission = exprContextGetVarPointerUnsafePooled(pContext, g_MissionVarName);
	if (pMission) {
		return missionevent_EventCount(pMission, pcMissionEventName);
	}
	return 0;
}


// ----------------------------------------------------------------------------------
// Mission State and Status (Player)
// ----------------------------------------------------------------------------------

AUTO_EXPR_FUNC(player) ACMD_NAME(GrantMission);
int mission_FuncGrantMission(ExprContext *pContext, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionName)
{
	Entity *pEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->missionInfo) {
		MissionDef *pDef = RefSystem_ReferentFromString(g_MissionDictionary, pcMissionName);
		MissionOfferParams params = {0};

		if (missiondef_CanBeOfferedAtAll(pEnt, pDef, NULL, NULL, &params.eCreditType)) {
			missioninfo_AddMission(entGetPartitionIdx(pEnt), pEnt->pPlayer->missionInfo, pDef, &params, NULL, NULL);
		}
	}
	return 1;
}


AUTO_EXPR_FUNC(player) ACMD_NAME(GrantMissionPrimaryOnly);
int mission_FuncGrantMissionPrimaryOnly(ExprContext *pContext, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionName)
{
	Entity *pEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->missionInfo) {
		MissionDef *pDef = RefSystem_ReferentFromString(g_MissionDictionary, pcMissionName);

		if (missiondef_CanBeOfferedAsPrimary(pEnt, pDef, NULL, NULL)) {
			missioninfo_AddMission(entGetPartitionIdx(pEnt), pEnt->pPlayer->missionInfo, pDef, NULL, NULL, NULL);
		}
	}
	return 1;
}


AUTO_EXPR_FUNC_STATIC_CHECK;
int mission_FuncMissionState_LoadVerify(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcMissionName)
{
	// Track any Mission State Events
	MissionDef *pMissionDef = exprContextGetVarPointerUnsafePooled(pContext, g_MissionDefVarName);
	MissionDef *pTargetMissionDef = pcMissionName ? missiondef_DefFromRefString(pcMissionName) : NULL;
	if (pMissionDef && pTargetMissionDef) {
		GameEvent *pEvent = StructCreate(parse_GameEvent);
		char *estrBuffer = NULL;

		estrPrintf(&estrBuffer, "MissionStateChange_%s", pcMissionName);
		pEvent->pchEventName = allocAddString(estrBuffer);
		pEvent->type = EventType_MissionState;
		pEvent->pchMissionRefString = allocAddString(pcMissionName);
		pEvent->pchMissionCategoryName = REF_STRING_FROM_HANDLE(pTargetMissionDef->hCategory);
		if (missiondef_GetType(pMissionDef) != MissionType_OpenMission) {
			pEvent->tMatchSource = TriState_Yes;
		}

		eventtracker_AddNamedEventToList(&pMissionDef->eaTrackedEventsNoSave, pEvent, pMissionDef->filename);

		estrDestroy(&estrBuffer);
	}
	return 0;
}


AUTO_EXPR_FUNC(player, mission) ACMD_NAME(MissionStateInProgress) ACMD_EXPR_STATIC_CHECK(mission_FuncMissionState_LoadVerify);
int mission_FuncMissionStateInProgress(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionName)
{
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	if (pPlayerEnt) {
		MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
		if (pInfo){
			Mission *pMission = mission_FindMissionFromRefString(pInfo, pcMissionName);
			if (pMission && (pMission->state == MissionState_InProgress)){
				return true;
			}
		}
	} else {
		Mission *pMission = openmission_FindMissionFromRefString(iPartitionIdx, pcMissionName);
		if (pMission && (pMission->state == MissionState_InProgress)){
			return true;
		}
	}
	return false;
}


AUTO_EXPR_FUNC(player, mission) ACMD_NAME(MissionStateSucceeded) ACMD_EXPR_STATIC_CHECK(mission_FuncMissionState_LoadVerify);
int mission_FuncMissionStateSucceeded(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionName)
{
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	if (pPlayerEnt) {
		MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
		if (pInfo){
			Mission *pMission = mission_FindMissionFromRefString(pInfo, pcMissionName);
			if (pMission && (pMission->state == MissionState_Succeeded)){
				return true;
			}
		}
	} else {
		Mission *pMission = openmission_FindMissionFromRefString(iPartitionIdx, pcMissionName);
		if (pMission && (pMission->state == MissionState_Succeeded)){
			return true;
		}
	}
	return false;
}


AUTO_EXPR_FUNC(player, mission) ACMD_NAME(MissionStateFailed) ACMD_EXPR_STATIC_CHECK(mission_FuncMissionState_LoadVerify);
int mission_FuncMissionStateFailed(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionName)
{
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	if (pPlayerEnt) {
		MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
		if (pInfo){
			Mission *pMission = mission_FindMissionFromRefString(pInfo, pcMissionName);
			if (pMission && (pMission->state == MissionState_Failed)){
				return true;
			}
		}
	} else {
		Mission *pMission = openmission_FindMissionFromRefString(iPartitionIdx, pcMissionName);
		if (pMission && (pMission->state == MissionState_Failed)){
			return true;
		}
	}
	return false;
}


AUTO_EXPR_FUNC_STATIC_CHECK;
int mission_FuncHasCompletedMissionByTime_LoadVerify(ExprContext *pContext, const char *pcMissionName, U32 uTime)
{
	// Track any Mission State Events
	MissionDef *pMissionDef = exprContextGetVarPointerUnsafePooled(pContext, g_MissionDefVarName);
	MissionDef *pTargetMissionDef = pcMissionName ? missiondef_DefFromRefString(pcMissionName) : NULL;

	if (pMissionDef && pTargetMissionDef) {
		GameEvent *pEvent = StructCreate(parse_GameEvent);
		char *estrBuffer = NULL;

		estrPrintf(&estrBuffer, "MissionComplete_%s", pcMissionName);
		pEvent->pchEventName = allocAddString(estrBuffer);
		pEvent->type = EventType_MissionState;
		pEvent->pchMissionRefString = allocAddString(pcMissionName);
		pEvent->pchMissionCategoryName = REF_STRING_FROM_HANDLE(pTargetMissionDef->hCategory);
		pEvent->missionState = MissionState_TurnedIn;
		pEvent->tMatchSource = TriState_Yes;

		eventtracker_AddNamedEventToList(&pMissionDef->eaTrackedEventsNoSave, pEvent, pMissionDef->filename);

		estrDestroy(&estrBuffer);
	}
	return 0;
}


//
// Return true if the player has completed the mission before the given time
//
AUTO_EXPR_FUNC(player, mission) ACMD_NAME(HasCompletedMissionByTime) ACMD_EXPR_STATIC_CHECK(mission_FuncHasCompletedMissionByTime_LoadVerify);
int mission_FuncHasCompletedMissionByTime(ExprContext *pContext, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionName, U32 uTime)
{
	MissionDef *pTargetDef = missiondef_DefFromRefString(pcMissionName);
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	int bHasCompleted = 0;

	if (pPlayerEnt && pTargetDef) {
		MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
		if (pInfo) {
			CompletedMission *pCompletedMission = mission_GetCompletedMissionByDef(pInfo, pTargetDef);
			if ( pCompletedMission && (pCompletedMission->completedTime <= uTime) ) {
				bHasCompleted = 1;
			}
		}
	} else if (!pTargetDef) {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "HasCompletedMission %s : No such mission", pcMissionName);
	}
	return bHasCompleted;
}


AUTO_EXPR_FUNC_STATIC_CHECK;
int mission_FuncHasNoRecordOfMission_LoadVerify(ExprContext *pContext, const char *pcMissionName)
{
	// Track any Mission State Events
	MissionDef *pMissionDef = exprContextGetVarPointerUnsafePooled(pContext, g_MissionDefVarName);
	MissionDef *pTargetMissionDef = pcMissionName ? missiondef_DefFromRefString(pcMissionName) : NULL;

	if (pMissionDef && pTargetMissionDef) {
		GameEvent *pEvent = StructCreate(parse_GameEvent);
		char *estrBuffer = NULL;

		estrPrintf(&estrBuffer, "MissionComplete_%s", pcMissionName);
		pEvent->pchEventName = allocAddString(estrBuffer);
		pEvent->type = EventType_MissionState;
		pEvent->pchMissionRefString = allocAddString(pcMissionName);
		pEvent->pchMissionCategoryName = REF_STRING_FROM_HANDLE(pTargetMissionDef->hCategory);
		pEvent->missionState = MissionState_TurnedIn;
		pEvent->tMatchSource = TriState_Yes;

		eventtracker_AddNamedEventToList(&pMissionDef->eaTrackedEventsNoSave, pEvent, pMissionDef->filename);

		estrDestroy(&estrBuffer);
	}
	return 0;
}


AUTO_EXPR_FUNC(player, mission) ACMD_NAME(HasNoRecordOfMission) ACMD_EXPR_STATIC_CHECK(mission_FuncHasNoRecordOfMission_LoadVerify);
int mission_FuncHasNoRecordOfMission(ExprContext *pContext, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionName)
{
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	MissionDef *pTargetDef = missiondef_DefFromRefString(pcMissionName);

	if (!pPlayerEnt || !pTargetDef) {
		return false;
	}

	if (eaIndexedGetUsingString(&pPlayerEnt->pPlayer->missionInfo->missions, pcMissionName)) {
		return false;
	}

	// Make sure the player hasn't completed this mission before
	if(eaIndexedGetUsingString(&pPlayerEnt->pPlayer->missionInfo->completedMissions, pcMissionName)) {
		return false;
	}

	return true;
}


AUTO_EXPR_FUNC_STATIC_CHECK;
int mission_FuncCanAcceptMission_LoadVerify(ExprContext *pContext, const char *pcMissionName)
{
	// Track any Mission State Events
	MissionDef *pMissionDef = exprContextGetVarPointerUnsafePooled(pContext, g_MissionDefVarName);
	MissionDef *pTargetMissionDef = pcMissionName ? missiondef_DefFromRefString(pcMissionName) : NULL;

	if (pMissionDef && pTargetMissionDef) {
		GameEvent *pEvent = StructCreate(parse_GameEvent);
		char *estrBuffer = NULL;

		estrPrintf(&estrBuffer, "MissionComplete_%s", pcMissionName);
		pEvent->pchEventName = allocAddString(estrBuffer);
		pEvent->type = EventType_MissionState;
		pEvent->pchMissionRefString = allocAddString(pcMissionName);
		pEvent->pchMissionCategoryName = REF_STRING_FROM_HANDLE(pTargetMissionDef->hCategory);
		pEvent->missionState = MissionState_TurnedIn;
		pEvent->tMatchSource = TriState_Yes;

		eventtracker_AddNamedEventToList(&pMissionDef->eaTrackedEventsNoSave, pEvent, pMissionDef->filename);

		estrDestroy(&estrBuffer);
	}
	return 0;
}


AUTO_EXPR_FUNC(player, mission) ACMD_NAME(CanAcceptMission) ACMD_EXPR_STATIC_CHECK(mission_FuncCanAcceptMission_LoadVerify);
int mission_FuncCanAcceptMission(ExprContext *pContext, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionName)
{
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	MissionDef *pTargetDef = missiondef_DefFromRefString(pcMissionName);

	return missiondef_CanBeOfferedAsPrimary(pPlayerEnt, pTargetDef, NULL, NULL);
}


AUTO_EXPR_FUNC_STATIC_CHECK;
int mission_FuncHasCompletedAllMissionGrants_StaticCheck(ExprContext *pContext)
{
	// Track all mission turn-in events
	MissionDef *pMissionDef = exprContextGetVarPointerUnsafePooled(pContext, g_MissionDefVarName);
	if (pMissionDef) {
		GameEvent *pEvent;
		MissionDef *pRootDef = pMissionDef;

		if (GET_REF(pRootDef->parentDef)) {
			pRootDef = GET_REF(pRootDef->parentDef);
		}

		if (pRootDef->missionType != MissionType_Episode) {
			ErrorFilenamef(exprContextGetBlameFile(pContext), "HasCompletedAllMissionGrants cannot be used in non-episode-type missions");
			return 0;
		}

		pEvent = StructCreate(parse_GameEvent);
		pEvent->pchEventName = allocAddString("AnyMissionCompleted");
		pEvent->type = EventType_MissionState;
		pEvent->missionState = MissionState_TurnedIn;
		pEvent->tMatchSource = TriState_Yes;
		eventtracker_AddNamedEventToList(&pMissionDef->eaTrackedEventsNoSave, pEvent, pMissionDef->filename);
	}
	return 0;
}


AUTO_EXPR_FUNC(mission) ACMD_NAME(HasCompletedAllMissionGrants) ACMD_EXPR_STATIC_CHECK(mission_FuncHasCompletedAllMissionGrants_StaticCheck);
int mission_FuncHasCompletedAllMissionGrants(ExprContext *pContext)
{
	Mission *pMission = exprContextGetVarPointerUnsafePooled(pContext, g_MissionVarName);
	MissionInfo *pInfo = pMission ? pMission->infoOwner : NULL;
	int i;

	if (!pInfo) {
		return false;
	}

	for (i = eaSize(&pMission->childFullMissions) - 1; i >= 0; i--) {
		if (!mission_GetCompletedMissionByDef(pInfo, RefSystem_ReferentFromString(g_MissionDictionary, pMission->childFullMissions[i]))) {
			return false;
		}
	}

	return true;
}


// ----------------------------------------------------------------------------------
// Mission State and Status (Team)
// ----------------------------------------------------------------------------------

AUTO_EXPR_FUNC_STATIC_CHECK;
int mission_FuncTeamMissionState_LoadVerify(ExprContext *pContext, const char *pcMissionName)
{
	// Track any Mission State Events
	MissionDef *pMissionDef = exprContextGetVarPointerUnsafePooled(pContext, g_MissionDefVarName);
	MissionDef *pTargetMissionDef = pcMissionName ? missiondef_DefFromRefString(pcMissionName) : NULL;

	if (pMissionDef && pTargetMissionDef) {
		if (missiondef_GetType(pMissionDef) == MissionType_OpenMission) {
			ErrorFilenamef(exprContextGetBlameFile(pContext), "Error: Can't use TeamMissionState___ expressions for an Open Mission (%s)", pMissionDef->name);
		} else {
			GameEvent *pEvent = StructCreate(parse_GameEvent);
			char *estrBuffer = NULL;

			estrPrintf(&estrBuffer, "TeamMissionStateChange_%s", pcMissionName);
			pEvent->pchEventName = allocAddString(estrBuffer);
			pEvent->type = EventType_MissionState;
			pEvent->pchMissionRefString = allocAddString(pcMissionName);
			pEvent->pchMissionCategoryName = REF_STRING_FROM_HANDLE(pTargetMissionDef->hCategory);
			pEvent->tMatchSourceTeam = TriState_Yes;
			eventtracker_AddNamedEventToList(&pMissionDef->eaTrackedEventsNoSave, pEvent, pMissionDef->filename);

			estrDestroy(&estrBuffer);
		}
	}
	return 0;
}


// True if anyone on the team has this Mission in progress
AUTO_EXPR_FUNC(player, mission) ACMD_NAME(TeamMissionStateInProgress) ACMD_EXPR_STATIC_CHECK(mission_FuncTeamMissionState_LoadVerify);
int mission_FuncTeamMissionStateInProgress(ExprContext *pContext, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionName)
{
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	if (pPlayerEnt) {
		MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
		if (pInfo){
			Mission *pMission = mission_FindMissionFromRefString(pInfo, pcMissionName);
			if (pMission && (pMission->state == MissionState_InProgress)){
				return true;
			}
		}
		
		if ( team_IsMember(pPlayerEnt) ) 
		{
			Team *pTeam = team_GetTeam(pPlayerEnt);
			if (pTeam) 
			{
				int iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
				int i;
				for (i=eaSize(&pTeam->eaMembers)-1; i>=0; --i)
				{
					Entity *pTeammate = GET_REF(pTeam->eaMembers[i]->hEnt);
					if (pTeammate)
					{
						pInfo = mission_GetInfoFromPlayer(pTeammate);
						if (pInfo) 
						{
							Mission *pMission = mission_FindMissionFromRefString(pInfo, pcMissionName);
							if (pMission && (pMission->state == MissionState_InProgress)) 
							{
								return true;
							}
						}
					}
				}
			}
		}
	}
	return false;
}


// True if anyone on the team has this Mission Succeeded
AUTO_EXPR_FUNC(player, mission) ACMD_NAME(TeamMissionStateSucceeded) ACMD_EXPR_STATIC_CHECK(mission_FuncTeamMissionState_LoadVerify);
int mission_FuncTeamMissionStateSucceeded(ExprContext *pContext, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionName)
{
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	if (pPlayerEnt) {
		MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
		Team *pTeam;
		if (pInfo) {
			Mission *pMission = mission_FindMissionFromRefString(pInfo, pcMissionName);
			if (pMission && (pMission->state == MissionState_Succeeded)){
				return true;
			}
		}
		
		pTeam = team_GetTeam(pPlayerEnt);
		if (pTeam && pPlayerEnt->pTeam->eState == TeamState_Member)
		{
			int iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
			int i;
			for (i=eaSize(&pTeam->eaMembers)-1; i>=0; --i) 
			{
				Entity *pTeammate = GET_REF(pTeam->eaMembers[i]->hEnt);
				if (pTeammate)
				{
					pInfo = mission_GetInfoFromPlayer(pTeammate);
					if (pInfo) 
					{
						Mission *pMission = mission_FindMissionFromRefString(pInfo, pcMissionName);
						if (pMission && (pMission->state == MissionState_Succeeded))
						{
							return true;
						}
					}
				}
			}
		}
	}
	return false;
}


// True if anyone on the team has this Mission Failed
AUTO_EXPR_FUNC(player, mission)  ACMD_NAME(TeamMissionStateFailed) ACMD_EXPR_STATIC_CHECK(mission_FuncTeamMissionState_LoadVerify);
int mission_FuncTeamMissionStateFailed(ExprContext *pContext, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionName)
{
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	if (pPlayerEnt) {
		Team *pTeam;
		MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
		if (pInfo) {
			Mission *pMission = mission_FindMissionFromRefString(pInfo, pcMissionName);
			if (pMission && (pMission->state == MissionState_Failed)) {
				return true;
			}
		}
		
		pTeam = team_GetTeam(pPlayerEnt);
		if (pTeam && pPlayerEnt->pTeam->eState == TeamState_Member)
		{
			int iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
			int i;
			for (i=eaSize(&pTeam->eaMembers)-1; i>=0; --i) 
			{
				Entity *pTeammate = GET_REF(pTeam->eaMembers[i]->hEnt);
				if (pTeammate)
				{
					pInfo = mission_GetInfoFromPlayer(pTeammate);
					if (pInfo) 
					{
						Mission *pMission = mission_FindMissionFromRefString(pInfo, pcMissionName);
						if (pMission && (pMission->state == MissionState_Failed))
						{
							return true;
						}
					}
				}
			}
		}
	}
	return false;
}


AUTO_EXPR_FUNC_STATIC_CHECK;
int mission_FuncTeamHasCompletedMission_LoadVerify(ExprContext *pContext, const char *pcMissionName)
{
	// Track any Mission State Events
	MissionDef *pMissionDef = exprContextGetVarPointerUnsafePooled(pContext, g_MissionDefVarName);
	MissionDef *pTargetMissionDef = pcMissionName ? missiondef_DefFromRefString(pcMissionName) : NULL;

	if (pMissionDef && pTargetMissionDef) {
		if (missiondef_GetType(pMissionDef) == MissionType_OpenMission){
			ErrorFilenamef(exprContextGetBlameFile(pContext), "Error: Can't use TeamHasCompletedMission an Open Mission (%s)", pMissionDef->name);
		}else{
			GameEvent *pEvent = StructCreate(parse_GameEvent);
			char *estrBuffer = NULL;

			estrPrintf(&estrBuffer, "TeamMissionComplete_%s", pcMissionName);
			pEvent->pchEventName = allocAddString(estrBuffer);
			pEvent->type = EventType_MissionState;
			pEvent->pchMissionRefString = allocAddString(pcMissionName);
			pEvent->pchMissionCategoryName = REF_STRING_FROM_HANDLE(pTargetMissionDef->hCategory);
			pEvent->missionState = MissionState_TurnedIn;
			pEvent->tMatchSourceTeam = TriState_Yes;

			eventtracker_AddNamedEventToList(&pMissionDef->eaTrackedEventsNoSave, pEvent, pMissionDef->filename);

			estrDestroy(&estrBuffer);
		}
	}
	return 0;
}


// True if anyone on the team has completed this Mission
AUTO_EXPR_FUNC(player, mission)  ACMD_NAME(TeamHasCompletedMission) ACMD_EXPR_STATIC_CHECK(mission_FuncTeamHasCompletedMission_LoadVerify);
int mission_FuncTeamHasCompletedMission(ExprContext *pContext, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionName)
{
	MissionDef *pTargetDef = missiondef_DefFromRefString(pcMissionName);
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	int bHasCompleted = 0;

	if (pPlayerEnt && pTargetDef) {
		Team *pTeam;
		MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
		if (pInfo && mission_GetCompletedMissionByDef(pInfo, pTargetDef)){
			bHasCompleted = 1;
		}
		
		pTeam = team_GetTeam(pPlayerEnt);
		if (!bHasCompleted && pTeam && pPlayerEnt->pTeam->eState == TeamState_Member) 
		{
			int iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
			int i;
			for (i=eaSize(&pTeam->eaMembers)-1; i>=0; --i)
			{
				Entity *pTeammate = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pTeam->eaMembers[i]->iEntID);
				if (!pTeammate) {
					pTeammate = GET_REF(pTeam->eaMembers[i]->hEnt);
				}
				if (pTeammate)
				{
					pInfo = mission_GetInfoFromPlayer(pTeammate);
					if (pInfo && mission_GetCompletedMissionByDef(pInfo, pTargetDef))
					{
						bHasCompleted = 1;
						break;
					}
				}
			}
		}
	} else if (!pTargetDef) {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "TeamHasCompletedMission %s : No such mission", pcMissionName);
	}
	return bHasCompleted;
}


// ----------------------------------------------------------------------------------
// Finding number of Completed Missions (Player)
// ----------------------------------------------------------------------------------

AUTO_EXPR_FUNC_STATIC_CHECK;
int mission_FuncGetNumCompletedMissions_StaticCheck(ExprContext *pContext, bool bIgnoreRepeats, bool bIgnoreInvisible)
{
	// Track any Mission Turn-in Events
	MissionDef *pMissionDef = exprContextGetVarPointerUnsafePooled(pContext, g_MissionDefVarName);
	if (pMissionDef) {
		GameEvent *pEvent = StructCreate(parse_GameEvent);

		pEvent->pchEventName = allocAddString("NormalMissionCompleted");
		pEvent->type = EventType_MissionState;
		pEvent->missionState = MissionState_TurnedIn;
		pEvent->tMatchSource = TriState_Yes;
		pEvent->missionType = MissionType_Normal;

		eventtracker_AddNamedEventToList(&pMissionDef->eaTrackedEventsNoSave, pEvent, pMissionDef->filename);
	}
	return 0;
}


// Gets the number of regular Missions a player has completed.  Does not count Perks, etc.
AUTO_EXPR_FUNC(player, mission)  ACMD_NAME(GetNumCompletedMissions) ACMD_EXPR_STATIC_CHECK(mission_FuncGetNumCompletedMissions_StaticCheck);
int mission_FuncGetNumCompletedMissions(ExprContext *pContext, bool bIgnoreRepeats, bool bIgnoreInvisible)
{
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
	int i, iNumCompleted = 0;

	if (pInfo) {
		for (i = 0; i < eaSize(&pInfo->completedMissions); i++) {
			MissionDef *pDef = GET_REF(pInfo->completedMissions[i]->def);
			if (pDef && 
				(missiondef_GetType(pDef) == MissionType_Normal 
				|| missiondef_GetType(pDef) == MissionType_Nemesis 
				|| missiondef_GetType(pDef) == MissionType_Episode 
				|| missiondef_GetType(pDef) == MissionType_TourOfDuty
				|| missiondef_GetType(pDef) == MissionType_AutoAvailable))
			{
				if (!bIgnoreInvisible || missiondef_HasDisplayName(pDef)) {
					if (bIgnoreRepeats) {
						iNumCompleted++;
					} else {
						iNumCompleted += completedmission_GetNumTimesCompleted(pInfo->completedMissions[i]);
					}
				}
			}
		}
	}

	return iNumCompleted;
}


AUTO_EXPR_FUNC_STATIC_CHECK;
int mission_FuncGetNumCompletedPerks_StaticCheck(ExprContext *pContext)
{
	// Track any Mission Turn-in Events
	MissionDef *pMissionDef = exprContextGetVarPointerUnsafePooled(pContext, g_MissionDefVarName);
	if (pMissionDef) {
		GameEvent *pEvent = StructCreate(parse_GameEvent);

		pEvent->pchEventName = allocAddString("PerkMissionCompleted");
		pEvent->type = EventType_MissionState;
		pEvent->missionState = MissionState_TurnedIn;
		pEvent->tMatchSource = TriState_Yes;
		pEvent->missionType = MissionType_Perk;

		eventtracker_AddNamedEventToList(&pMissionDef->eaTrackedEventsNoSave, pEvent, pMissionDef->filename);
	}
	return 0;
}


// Gets the number of Perks the player has completed.
AUTO_EXPR_FUNC(player, mission)  ACMD_NAME(GetNumCompletedPerks) ACMD_EXPR_STATIC_CHECK(mission_FuncGetNumCompletedPerks_StaticCheck);
int mission_FuncGetNumCompletedPerks(ExprContext *pContext)
{
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
	int i, iNumCompleted = 0;

	if (pInfo) {
		for (i = 0; i < eaSize(&pInfo->completedMissions); i++) {
			MissionDef *pDef = GET_REF(pInfo->completedMissions[i]->def);
			if (pDef && missiondef_GetType(pDef) == MissionType_Perk) {
				if (missiondef_HasDisplayName(pDef)) {
					iNumCompleted++;
				}
			}
		}
	}

	return iNumCompleted;
}


AUTO_EXPR_FUNC_STATIC_CHECK;
int mission_FuncGetNumCompletedMissionsByCategory_StaticCheck(ExprContext *pContext, ACMD_EXPR_RES_DICT(MissionCategory) const char *pcCategoryName, bool bIgnoreRepeats, bool bIgnoreInvisible)
{
	// Track any Mission Turn-in Events
	MissionDef *pMissionDef = exprContextGetVarPointerUnsafePooled(pContext, g_MissionDefVarName);
	if (pMissionDef) {
		GameEvent *pEvent = StructCreate(parse_GameEvent);

		pEvent->pchEventName = allocAddString("NormalMissionCompleted");
		pEvent->type = EventType_MissionState;
		pEvent->missionState = MissionState_TurnedIn;
		pEvent->tMatchSource = TriState_Yes;
		pEvent->missionType = MissionType_Normal;

		eventtracker_AddNamedEventToList(&pMissionDef->eaTrackedEventsNoSave, pEvent, pMissionDef->filename);
	}
	return 0;
}


// Gets the number of regular Missions a player has completed from the given category.  Does not count Perks, etc.
AUTO_EXPR_FUNC(player, mission)  ACMD_NAME(GetNumCompletedMissionsByCategory) ACMD_EXPR_STATIC_CHECK(mission_FuncGetNumCompletedMissionsByCategory_StaticCheck);
int mission_FuncGetNumCompletedMissionsByCategory(ExprContext *pContext, ACMD_EXPR_RES_DICT(MissionCategory) const char *pcCategoryName, bool bIgnoreRepeats, bool bIgnoreInvisible)
{
	MissionCategory *pCategory = RefSystem_ReferentFromString(g_MissionCategoryDict, pcCategoryName);
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
	int i, iNumCompleted = 0;

	if (pInfo && pCategory) {
		for (i = 0; i < eaSize(&pInfo->completedMissions); i++) {
			MissionDef *pDef = GET_REF(pInfo->completedMissions[i]->def);
			if(pDef && (missiondef_GetType(pDef) == MissionType_Normal 
				|| missiondef_GetType(pDef) == MissionType_Nemesis 
				|| missiondef_GetType(pDef) == MissionType_Episode 
				|| missiondef_GetType(pDef) == MissionType_TourOfDuty
				|| missiondef_GetType(pDef) == MissionType_AutoAvailable) 
				&& (pCategory == GET_REF(pDef->hCategory)))
			{
				if (!bIgnoreInvisible || missiondef_HasDisplayName(pDef)) {
					if (bIgnoreRepeats) {
						iNumCompleted++;
					} else {
						iNumCompleted += completedmission_GetNumTimesCompleted(pInfo->completedMissions[i]);
					}
				}
			}
		}
	}

	return iNumCompleted;
}


AUTO_EXPR_FUNC_STATIC_CHECK;
int mission_FuncGetNumCompletedPerksByCategory_StaticCheck(ExprContext *pContext, ACMD_EXPR_RES_DICT(MissionCategory) const char *pcCategoryName)
{
	// Track any Mission Turn-in Events
	MissionDef *pMissionDef = exprContextGetVarPointerUnsafePooled(pContext, g_MissionDefVarName);
	if (pMissionDef) {
		GameEvent *pEvent = StructCreate(parse_GameEvent);

		pEvent->pchEventName = allocAddString("PerkMissionCompleted");
		pEvent->type = EventType_MissionState;
		pEvent->missionState = MissionState_TurnedIn;
		pEvent->tMatchSource = TriState_Yes;
		pEvent->missionType = MissionType_Perk;

		eventtracker_AddNamedEventToList(&pMissionDef->eaTrackedEventsNoSave, pEvent, pMissionDef->filename);
	}
	return 0;
}


// Gets the number of Perks a player has completed from the given category.
AUTO_EXPR_FUNC(player, mission) ACMD_NAME(GetNumCompletedPerksByCategory) ACMD_EXPR_STATIC_CHECK(mission_FuncGetNumCompletedPerksByCategory_StaticCheck);
int mission_FuncGetNumCompletedPerksByCategory(ExprContext *pContext, ACMD_EXPR_RES_DICT(MissionCategory) const char *pcCategoryName)
{
	MissionCategory *pCategory = RefSystem_ReferentFromString(g_MissionCategoryDict, pcCategoryName);
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
	int i, iNumCompleted = 0;

	if (pInfo && pCategory) {
		for (i = 0; i < eaSize(&pInfo->completedMissions); i++) {
			MissionDef *pDef = GET_REF(pInfo->completedMissions[i]->def);
			if (pDef && missiondef_GetType(pDef) == MissionType_Perk && (pCategory == GET_REF(pDef->hCategory))) {
				if (missiondef_HasDisplayName(pDef)) {
					iNumCompleted++;
				}
			}
		}
	}

	return iNumCompleted;
}


AUTO_EXPR_FUNC_STATIC_CHECK;
void mission_FuncEntCropMissionState_LoadVerify(ExprContext *pContext, ACMD_EXPR_ENTARRAY_IN_OUT peaEntsInOut, const char *pcMissionRefString)
{
	if (g_MissionDictionary && !missiondef_DefFromRefString(pcMissionRefString)){
		//ErrorFilenamef(exprContextGetBlameFile(pContext), "EntCropMissionState___ %s : No such mission", pcMissionRefString);
	}
}


// Removes every entity from the passed in ent array that doesn't have the mission in progress
AUTO_EXPR_FUNC(ai, encounter_action)  ACMD_NAME(EntCropMissionStateInProgress) ACMD_EXPR_STATIC_CHECK(mission_FuncEntCropMissionState_LoadVerify);
void mission_FuncEntCropMissionStateInProgress(ExprContext *pContext, ACMD_EXPR_ENTARRAY_IN_OUT peaEntsInOut, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionRefString)
{
	int i, num = eaSize(peaEntsInOut);

	if (!pcMissionRefString || !pcMissionRefString[0]) {
		return;
	}

	for(i = num - 1; i >= 0; i--) {
		MissionInfo *pInfo = mission_GetInfoFromPlayer((*peaEntsInOut)[i]);
		Mission *pMission = pInfo?mission_FindMissionFromRefString(pInfo, pcMissionRefString):NULL;

		if (!pMission || pMission->state != MissionState_InProgress) {
			eaRemoveFast(peaEntsInOut, i);
		}
	}
}


// Removes every entity from the passed in ent array that doesn't have the mission in the Succeeded state
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(EntCropMissionStateSucceeded) ACMD_EXPR_STATIC_CHECK(mission_FuncEntCropMissionState_LoadVerify);
void mission_FuncEntCropMissionStateSucceeded(ExprContext *pContext, ACMD_EXPR_ENTARRAY_IN_OUT peaEntsInOut, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionRefString)
{
	int i, num = eaSize(peaEntsInOut);

	if (!pcMissionRefString || !pcMissionRefString[0]) {
		return;
	}

	for(i = num - 1; i >= 0; i--) {
		MissionInfo *pInfo = mission_GetInfoFromPlayer((*peaEntsInOut)[i]);
		Mission *pMission = pInfo ? mission_FindMissionFromRefString(pInfo, pcMissionRefString) : NULL;

		if (!pMission || pMission->state != MissionState_Succeeded) {
			eaRemoveFast(peaEntsInOut, i);
		}
	}
}


// Removes every entity from the passed in ent array that doesn't have the mission in the Failed state
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(EntCropMissionStateFailed) ACMD_EXPR_STATIC_CHECK(mission_FuncEntCropMissionState_LoadVerify);
void mission_FuncEntCropMissionStateFailed(ExprContext *pContext, ACMD_EXPR_ENTARRAY_IN_OUT peaEntsInOut, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionRefString)
{
	int i, num = eaSize(peaEntsInOut);

	if (!pcMissionRefString || !pcMissionRefString[0]) {
		return;
	}

	for(i = num - 1; i >= 0; i--) {
		MissionInfo *pInfo = mission_GetInfoFromPlayer((*peaEntsInOut)[i]);
		Mission *pMission = pInfo ? mission_FindMissionFromRefString(pInfo, pcMissionRefString) : NULL;

		if (!pMission || pMission->state != MissionState_Failed) {
			eaRemoveFast(peaEntsInOut, i);
		}
	}
}


AUTO_EXPR_FUNC_STATIC_CHECK;
void mission_FuncEntCropHasCompletedMission_LoadVerify(ExprContext *pContext, ACMD_EXPR_ENTARRAY_IN_OUT peaEntsInOut, const char *pcMissionName)
{
	if (g_MissionDictionary && !RefSystem_ReferentFromString(g_MissionDictionary, pcMissionName)){
//		ErrorFilenamef(exprContextGetBlameFile(pContext), "EntCropHasCompletedMission %s : No such mission", pcMissionName);
	}
}


// Removes every entity from the passed in ent array that hasn't completed the given Mission
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(EntCropHasCompletedMission) ACMD_EXPR_STATIC_CHECK(mission_FuncEntCropHasCompletedMission_LoadVerify);
void mission_FuncEntCropHasCompletedMission(ExprContext *pContext, ACMD_EXPR_ENTARRAY_IN_OUT peaEntsInOut, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionName)
{
	int i, num = eaSize(peaEntsInOut);

	if (!pcMissionName || !pcMissionName[0]) {
		return;
	}

	for(i = num - 1; i >= 0; i--) {
		MissionInfo *pInfo = mission_GetInfoFromPlayer((*peaEntsInOut)[i]);
		if (!pInfo || !mission_GetNumTimesCompletedByName(pInfo, pcMissionName)) {
			eaRemoveFast(peaEntsInOut, i);
		}
	}
}

AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(EntCropCanAcceptMission) ACMD_EXPR_STATIC_CHECK(mission_FuncEntCropMissionState_LoadVerify);
void mission_FuncEntCropCanAcceptMission(ExprContext *pContext, ACMD_EXPR_ENTARRAY_IN_OUT peaEntsInOut, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionName)
{
	MissionDef *pMissionDef = missiondef_DefFromRefString(pcMissionName);
	int i;

	for(i = eaSize(peaEntsInOut)-1; i >= 0; i--) {
		if (!missiondef_CanBeOfferedAsPrimary((*peaEntsInOut)[i], pMissionDef, NULL, NULL)) {
			eaRemoveFast(peaEntsInOut, i);
		}
	}
}


// -------------------------------------------------------------
// Granting a random Mission
// -------------------------------------------------------------

AUTO_EXPR_FUNC(player) ACMD_NAME(GrantRandomAutoMission);
ExprFuncReturnVal mission_FuncGrantRandomAutoMission(ExprContext *pContext, const char *pcCategory, ACMD_EXPR_ERRSTRING errString)
{
	Entity *pPlayer = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	if (pPlayer && pcCategory) {
		mission_OfferRandomAvailableMission(pPlayer, pcCategory);
	}
	return ExprFuncReturnFinished;
}


// ----------------------------------------------------------------------------
//  Mission Variables
// ----------------------------------------------------------------------------

static Mission* GetMissionFromPlayer(Entity* pPlayerEnt, const char *pcMissionName, ACMD_EXPR_ERRSTRING errString)
{
	if (pPlayerEnt) {
		MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
		if (pInfo) {
			Mission *pMission = mission_FindMissionFromRefString(pInfo, pcMissionName);

			if (pMission) {
				return pMission;
			}
		}
		if (errString)
		{
			estrPrintf(errString, "Player does not have mission %s", pcMissionName);
		}
		return NULL;
	}

	if (errString)
	{
		estrPrintf(errString, "Player not found");
	}
	return NULL;
}

static Mission *
GetPlayerMission(ExprContext *pContext, const char *pcMissionName, ACMD_EXPR_ERRSTRING errString)
{
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	return GetMissionFromPlayer(pPlayerEnt, pcMissionName, errString);
}

// Gets the value of a mission variable as a string
static ExprFuncReturnVal GetMissionVariableStringInternal(ExprContext *pContext, Mission *pMission, ACMD_EXPR_STRING_OUT ppcRet, const char *pcVarName, ACMD_EXPR_ERRSTRING errString)
{
	WorldVariable *pVariable = pMission ? eaIndexedGetUsingString(&pMission->eaMissionVariables, pcVarName) : NULL;

	if (pVariable) {
		static char *estr = NULL;
		worldVariableToEString(pVariable, &estr);
		*ppcRet = exprContextAllocString(pContext, estr);
		return ExprFuncReturnFinished;
	} else {
		estrPrintf(errString, "No mission variable named '%s' was found", pcVarName);
		return ExprFuncReturnError;
	}
}

// Gets the value of a mission variable as a string
AUTO_EXPR_FUNC(Mission) ACMD_NAME(GetMissionVariableString);
ExprFuncReturnVal mission_FuncGetMissionVariableString(ExprContext *pContext, ACMD_EXPR_STRING_OUT ppcRet, const char *pcVarName, ACMD_EXPR_ERRSTRING errString)
{
	Mission *pMission = exprContextGetVarPointerUnsafePooled(pContext, g_MissionVarName);

	return GetMissionVariableStringInternal(pContext, pMission, ppcRet, pcVarName, errString);
}

AUTO_EXPR_FUNC(player, mission) ACMD_NAME(GetPlayerMissionVariableString);
ExprFuncReturnVal mission_FuncGetPlayerMissionVariableString(ExprContext *pContext, ACMD_EXPR_STRING_OUT ppcRet, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionName, const char *pcVarName, ACMD_EXPR_ERRSTRING errString)
{
	Mission *pMission = GetPlayerMission(pContext, pcMissionName, errString);

	if (!pMission) {
		// Note - GetPlayerMission sets the error string on error
		return ExprFuncReturnError;
	}

	return GetMissionVariableStringInternal(pContext, pMission, ppcRet, pcVarName, errString);
}

AUTO_EXPR_FUNC(ai, encounter_action, Mission) ACMD_NAME(GetEntMissionVariableString);
ExprFuncReturnVal mission_FuncGetEntMissionVariableString(ExprContext *pContext, ACMD_EXPR_STRING_OUT ppcRet, ACMD_EXPR_ENTARRAY_IN entsIn, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionName, const char *pcVarName, ACMD_EXPR_ERRSTRING errString)
{
	Mission *pMission = GetMissionFromPlayer(eaGet(entsIn, 0), pcMissionName, errString);

	if (!pMission) {
		// Note - GetMissionFromPlayer sets the error string on error
		return ExprFuncReturnError;
	}

	return GetMissionVariableStringInternal(pContext, pMission, ppcRet, pcVarName, errString);
}

static ExprFuncReturnVal GetMissionVariableStringWithDefaultInternal(ExprContext *pContext, Mission *pMission, ACMD_EXPR_STRING_OUT ppcRet, const char *pcVarName, const char *pcDefault, ACMD_EXPR_ERRSTRING errString)
{
	WorldVariable *pVariable = pMission ? eaIndexedGetUsingString(&pMission->eaMissionVariables, pcVarName) : NULL;
	static char *estr = NULL;

	if (pVariable) {
		worldVariableToEString(pVariable, &estr);
	} else {
		estrPrintf(&estr, "%s", pcDefault);
	}
	*ppcRet = exprContextAllocString(pContext, estr);
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(Mission) ACMD_NAME(GetMissionVariableStringWithDefault);
ExprFuncReturnVal mission_FuncGetMissionVariableStringWithDefault(ExprContext *pContext, ACMD_EXPR_STRING_OUT ppcRet, const char *pcVarName, const char *pcDefault, ACMD_EXPR_ERRSTRING errString)
{
	Mission *pMission = exprContextGetVarPointerUnsafePooled(pContext, g_MissionVarName);

	return GetMissionVariableStringWithDefaultInternal(pContext, pMission, ppcRet, pcVarName, pcDefault, errString);
}

AUTO_EXPR_FUNC(Player, Mission) ACMD_NAME(GetPlayerMissionVariableStringWithDefault);
ExprFuncReturnVal mission_FuncGetPlayerMissionVariableStringWithDefault(ExprContext *pContext, ACMD_EXPR_STRING_OUT ppcRet, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionName, const char *pcVarName, const char *pcDefault, ACMD_EXPR_ERRSTRING errString)
{
	Mission *pMission = GetPlayerMission(pContext, pcMissionName, NULL);

	return GetMissionVariableStringWithDefaultInternal(pContext, pMission, ppcRet, pcVarName, pcDefault, errString);
}

AUTO_EXPR_FUNC(Player, Mission) ACMD_NAME(SetPlayerMissionVariableString);
ExprFuncReturnVal mission_FuncSetPlayerMissionVariableString(ExprContext *pContext, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionName, const char *pcVarName, const char *pchString, ACMD_EXPR_ERRSTRING errString)
{
	Mission *pMission = GetPlayerMission(pContext, pcMissionName, NULL);

	if (pMission)
	{
		MissionDef *pMissionDef = mission_GetDef(pMission);
		WorldVariable *pVariable = pMission ? eaIndexedGetUsingString(&pMission->eaMissionVariables, pcVarName) : NULL;
		WorldVariableDef *pVariableDef = pMissionDef ? eaIndexedGetUsingString(&pMissionDef->eaVariableDefs, pcVarName) : NULL;

		if (pVariable && pVariableDef) {
			if (pVariableDef->eType == WVAR_STRING) {
				StructCopyString(&pVariable->pcStringVal, pchString);
				mission_FlagAsNeedingEval(pMission);
				return ExprFuncReturnFinished;
			} else {
				estrPrintf(errString, "Mission variable named '%s' is not a string value", pcVarName);
				return ExprFuncReturnError;
			}
		} else {
			estrPrintf(errString, "No mission variable named '%s' was found", pcVarName);
			return ExprFuncReturnError;
		}
	}
	return ExprFuncReturnFinished;
}

// Set a message type mission variable by message key
AUTO_EXPR_FUNC(Player, Mission) ACMD_NAME(SetPlayerMissionVariableMessage);
ExprFuncReturnVal mission_FuncSetPlayerMissionVariableMessage(ExprContext *pContext, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionName, const char *pcVarName, const char *pchMessageKey, ACMD_EXPR_ERRSTRING errString)
{
	Mission *pMission = GetPlayerMission(pContext, pcMissionName, NULL);

	if (pMission)
	{
		MissionDef *pMissionDef = mission_GetDef(pMission);
		WorldVariable *pVariable = pMission ? eaIndexedGetUsingString(&pMission->eaMissionVariables, pcVarName) : NULL;
		WorldVariableDef *pVariableDef = pMissionDef ? eaIndexedGetUsingString(&pMissionDef->eaVariableDefs, pcVarName) : NULL;

		if (pVariable && pVariableDef) {
			if (pVariableDef->eType == WVAR_MESSAGE) {
				SET_HANDLE_FROM_STRING("Message", pchMessageKey, pVariable->messageVal.hMessage);
				mission_FlagAsNeedingEval(pMission);
				return ExprFuncReturnFinished;
			} else {
				estrPrintf(errString, "Mission variable named '%s' is not of type message", pcVarName);
				return ExprFuncReturnError;
			}
		} else {
			estrPrintf(errString, "No mission variable named '%s' was found", pcVarName);
			return ExprFuncReturnError;
		}
	}
	return ExprFuncReturnFinished;
}

// Gets the value of a mission variable as an integer
static ExprFuncReturnVal GetMissionVariableIntInternal(Mission *pMission, ACMD_EXPR_INT_OUT piRet, const char *pcVarName, ACMD_EXPR_ERRSTRING errString)
{
	MissionDef *pMissionDef = mission_GetDef(pMission);
	WorldVariable *pVariable = pMission ? eaIndexedGetUsingString(&pMission->eaMissionVariables, pcVarName) : NULL;
	WorldVariableDef *pVariableDef = pMissionDef ? eaIndexedGetUsingString(&pMissionDef->eaVariableDefs, pcVarName) : NULL;

	if (pVariable && pVariableDef) {
		if (pVariableDef->eType == WVAR_INT) {
			*piRet = pVariable->iIntVal;
			return ExprFuncReturnFinished;
		} else {
			estrPrintf(errString, "Mission variable named '%s' is not an integer value", pcVarName);
			return ExprFuncReturnError;
		}
	} else {
		estrPrintf(errString, "No mission variable named '%s' was found", pcVarName);
		return ExprFuncReturnError;
	}
}

// Gets the value of a mission variable as an integer
AUTO_EXPR_FUNC(Mission) ACMD_NAME(GetMissionVariableInt);
ExprFuncReturnVal mission_FuncGetMissionVariableInt(ExprContext *pContext, ACMD_EXPR_INT_OUT piRet, const char *pcVarName, ACMD_EXPR_ERRSTRING errString)
{
	Mission *pMission = exprContextGetVarPointerUnsafePooled(pContext, g_MissionVarName);

	return GetMissionVariableIntInternal(pMission, piRet, pcVarName, errString);
}

// Gets the value of a mission variable as an integer
AUTO_EXPR_FUNC(Player, Mission) ACMD_NAME(GetPlayerMissionVariableInt);
ExprFuncReturnVal mission_FuncGetPlayerMissionVariableInt(ExprContext *pContext, ACMD_EXPR_INT_OUT piRet, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionName, const char *pcVarName, ACMD_EXPR_ERRSTRING errString)
{
	Mission *pMission = GetPlayerMission(pContext, pcMissionName, errString);

	if (!pMission) {
		// Note - GetPlayerMission sets the error string on error
		return ExprFuncReturnError;
	}
	
	return GetMissionVariableIntInternal(pMission, piRet, pcVarName, errString);
}

AUTO_EXPR_FUNC(ai, encounter_action, Mission) ACMD_NAME(GetEntMissionVariableInt);
ExprFuncReturnVal mission_FuncGetEntMissionVariableInt(ExprContext *pContext, ACMD_EXPR_INT_OUT piRet, ACMD_EXPR_ENTARRAY_IN entsIn, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionName, const char *pcVarName, ACMD_EXPR_ERRSTRING errString)
{
	Mission *pMission = GetMissionFromPlayer(eaGet(entsIn, 0), pcMissionName, errString);

	if (!pMission) {
		// Note - GetMissionFromPlayer sets the error string on error
		return ExprFuncReturnError;
	}

	return GetMissionVariableIntInternal(pMission, piRet, pcVarName, errString);
}

static ExprFuncReturnVal GetMissionVariableIntWithDefaultInternal(Mission *pMission, ACMD_EXPR_INT_OUT piRet, const char *pcVarName, int iDefault, ACMD_EXPR_ERRSTRING errString)
{
	MissionDef *pMissionDef = mission_GetDef(pMission);
	WorldVariable *pVariable = pMission ? eaIndexedGetUsingString(&pMission->eaMissionVariables, pcVarName) : NULL;
	WorldVariableDef *pVariableDef = pMissionDef ? eaIndexedGetUsingString(&pMissionDef->eaVariableDefs, pcVarName) : NULL;

	if (pVariable && pVariableDef) {
		if (pVariableDef->eType == WVAR_INT) {
			*piRet = pVariable->iIntVal;
			return ExprFuncReturnFinished;
		} else {
			estrPrintf(errString, "Mission variable named '%s' is not an integer value", pcVarName);
			return ExprFuncReturnError;
		}
	} else {
		*piRet = iDefault;
	}

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(Mission) ACMD_NAME(GetMissionVariableIntWithDefault);
ExprFuncReturnVal mission_FuncGetMissionVariableIntWithDefault(ExprContext *pContext, ACMD_EXPR_INT_OUT piRet, const char *pcVarName, int iDefault, ACMD_EXPR_ERRSTRING errString)
{
	Mission *pMission = exprContextGetVarPointerUnsafePooled(pContext, g_MissionVarName);

	return GetMissionVariableIntWithDefaultInternal(pMission, piRet, pcVarName, iDefault, errString);
}

AUTO_EXPR_FUNC(Player, Mission) ACMD_NAME(GetPlayerMissionVariableIntWithDefault);
ExprFuncReturnVal mission_FuncGetPlayerMissionVariableIntWithDefault(ExprContext *pContext, ACMD_EXPR_INT_OUT piRet, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionName, const char *pcVarName, int iDefault, ACMD_EXPR_ERRSTRING errString)
{
	Mission *pMission = GetPlayerMission(pContext, pcMissionName, NULL);

	return GetMissionVariableIntWithDefaultInternal(pMission, piRet, pcVarName, iDefault, errString);
}

AUTO_EXPR_FUNC(Player, Mission) ACMD_NAME(SetPlayerMissionVariableInt);
ExprFuncReturnVal mission_FuncSetPlayerMissionVariableInt(ExprContext *pContext, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionName, const char *pcVarName, int iValue, ACMD_EXPR_ERRSTRING errString)
{
	Mission *pMission = GetPlayerMission(pContext, pcMissionName, NULL);

	if (pMission)
	{
		MissionDef *pMissionDef = mission_GetDef(pMission);
		WorldVariable *pVariable = pMission ? eaIndexedGetUsingString(&pMission->eaMissionVariables, pcVarName) : NULL;
		WorldVariableDef *pVariableDef = pMissionDef ? eaIndexedGetUsingString(&pMissionDef->eaVariableDefs, pcVarName) : NULL;

		if (pVariable && pVariableDef) {
			if (pVariableDef->eType == WVAR_INT) {
				pVariable->iIntVal = iValue;
				mission_FlagAsNeedingEval(pMission);
				return ExprFuncReturnFinished;
			} else {
				estrPrintf(errString, "Mission variable named '%s' is not an int value", pcVarName);
				return ExprFuncReturnError;
			}
		} else {
			estrPrintf(errString, "No mission variable named '%s' was found", pcVarName);
			return ExprFuncReturnError;
		}
	}
	return ExprFuncReturnFinished;
}

static ExprFuncReturnVal GetMissionVariableFloatInternal(Mission *pMission, ACMD_EXPR_FLOAT_OUT pfRet, const char *pcVarName, ACMD_EXPR_ERRSTRING errString)
{
	MissionDef *pMissionDef = mission_GetDef(pMission);
	WorldVariable *pVariable = pMission ? eaIndexedGetUsingString(&pMission->eaMissionVariables, pcVarName) : NULL;
	WorldVariableDef *pVariableDef = pMissionDef ? eaIndexedGetUsingString(&pMissionDef->eaVariableDefs, pcVarName) : NULL;

	if (pVariable && pVariableDef) {
		if (pVariableDef->eType == WVAR_FLOAT) {
			*pfRet = pVariable->fFloatVal;
			return ExprFuncReturnFinished;
		} else {
			estrPrintf(errString, "Mission variable named '%s' is not a float value", pcVarName);
			return ExprFuncReturnError;
		}
	} else {
		estrPrintf(errString, "No mission variable named '%s' was found", pcVarName);
		return ExprFuncReturnError;
	}
}

// Gets the value of a mission variable as a float
AUTO_EXPR_FUNC(Mission) ACMD_NAME(GetMissionVariableFloat);
ExprFuncReturnVal mission_FuncGetMissionVariableFloat(ExprContext *pContext, ACMD_EXPR_FLOAT_OUT pfRet, const char *pcVarName, ACMD_EXPR_ERRSTRING errString)
{
	Mission *pMission = exprContextGetVarPointerUnsafePooled(pContext, g_MissionVarName);

	return GetMissionVariableFloatInternal(pMission, pfRet, pcVarName, errString);
}

// Gets the value of a mission variable as a float
AUTO_EXPR_FUNC(Player, Mission) ACMD_NAME(GetPlayerMissionVariableFloat);
ExprFuncReturnVal mission_FuncGetPlayerMissionVariableFloat(ExprContext *pContext, ACMD_EXPR_FLOAT_OUT pfRet, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionName, const char *pcVarName, ACMD_EXPR_ERRSTRING errString)
{
	Mission *pMission = GetPlayerMission(pContext, pcMissionName, errString);

	if (!pMission)
	{
		// Note - GetPlayerMission sets the error string on error
		return ExprFuncReturnError;
	}

	return GetMissionVariableFloatInternal(pMission, pfRet, pcVarName, errString);
}

AUTO_EXPR_FUNC(ai, encounter_action, Mission) ACMD_NAME(GetEntMissionVariableFloat);
ExprFuncReturnVal mission_FuncGetEntMissionVariableFloat(ExprContext *pContext, ACMD_EXPR_FLOAT_OUT pfRet, ACMD_EXPR_ENTARRAY_IN entsIn, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionName, const char *pcVarName, ACMD_EXPR_ERRSTRING errString)
{
	Mission *pMission = GetMissionFromPlayer(eaGet(entsIn, 0), pcMissionName, errString);

	if (!pMission) {
		// Note - GetMissionFromPlayer sets the error string on error
		return ExprFuncReturnError;
	}

	return GetMissionVariableFloatInternal(pMission, pfRet, pcVarName, errString);
}

static ExprFuncReturnVal GetMissionVariableFloatWithDefaultInternal(Mission *pMission, ACMD_EXPR_FLOAT_OUT pfRet, const char *pcVarName, int fDefault, ACMD_EXPR_ERRSTRING errString)
{
	MissionDef *pMissionDef = mission_GetDef(pMission);
	WorldVariable *pVariable = pMission ? eaIndexedGetUsingString(&pMission->eaMissionVariables, pcVarName) : NULL;
	WorldVariableDef *pVariableDef = pMissionDef ? eaIndexedGetUsingString(&pMissionDef->eaVariableDefs, pcVarName) : NULL;

	if (pVariable && pVariableDef) {
		if (pVariableDef->eType == WVAR_FLOAT) {
			*pfRet = pVariable->fFloatVal;
			return ExprFuncReturnFinished;
		} else {
			estrPrintf(errString, "Mission variable named '%s' is not a float value", pcVarName);
			return ExprFuncReturnError;
		}
	} else {
		*pfRet = fDefault;
	}

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(Mission) ACMD_NAME(GetMissionVariableFloatWithDefault);
ExprFuncReturnVal mission_FuncGetMissionVariableFloatWithDefault(ExprContext *pContext, ACMD_EXPR_FLOAT_OUT pfRet, const char *pcVarName, int fDefault, ACMD_EXPR_ERRSTRING errString)
{
	Mission *pMission = exprContextGetVarPointerUnsafePooled(pContext, g_MissionVarName);

	return GetMissionVariableFloatWithDefaultInternal(pMission, pfRet, pcVarName, fDefault, errString);
}

AUTO_EXPR_FUNC(Player, Mission) ACMD_NAME(GetPlayerMissionVariableFloatWithDefault);
ExprFuncReturnVal mission_FuncGetPlayerMissionVariableFloatWithDefault(ExprContext *pContext, ACMD_EXPR_FLOAT_OUT pfRet, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionName, const char *pcVarName, int fDefault, ACMD_EXPR_ERRSTRING errString)
{
	Mission *pMission = GetPlayerMission(pContext, pcMissionName, NULL);

	return GetMissionVariableFloatWithDefaultInternal(pMission, pfRet, pcVarName, fDefault, errString);
}

AUTO_EXPR_FUNC(Player, Mission) ACMD_NAME(SetPlayerMissionVariableFloat);
ExprFuncReturnVal mission_FuncSetPlayerMissionVariableFloat(ExprContext *pContext, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionName, const char *pcVarName, F32 fValue, ACMD_EXPR_ERRSTRING errString)
{
	Mission *pMission = GetPlayerMission(pContext, pcMissionName, NULL);

	if (pMission)
	{
		MissionDef *pMissionDef = mission_GetDef(pMission);
		WorldVariable *pVariable = pMission ? eaIndexedGetUsingString(&pMission->eaMissionVariables, pcVarName) : NULL;
		WorldVariableDef *pVariableDef = pMissionDef ? eaIndexedGetUsingString(&pMissionDef->eaVariableDefs, pcVarName) : NULL;

		if (pVariable && pVariableDef) {
			if (pVariableDef->eType == WVAR_FLOAT) {
				pVariable->fFloatVal = fValue;
				mission_FlagAsNeedingEval(pMission);
				return ExprFuncReturnFinished;
			} else {
				estrPrintf(errString, "Mission variable named '%s' is not a float value", pcVarName);
				return ExprFuncReturnError;
			}
		} else {
			estrPrintf(errString, "No mission variable named '%s' was found", pcVarName);
			return ExprFuncReturnError;
		}
	}
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC_STATIC_CHECK;
int mission_FuncMissionStateTag_LoadVerify(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcMissionTagName)
{
	// Track any Mission State Events
	MissionDef *pMissionDef = exprContextGetVarPointerUnsafePooled(pContext, g_MissionDefVarName);
	if (pMissionDef) {
		GameEvent *pEvent = StructCreate(parse_GameEvent);

		pEvent->type = EventType_MissionState;
		if (missiondef_GetType(pMissionDef) != MissionType_OpenMission) {
			pEvent->pchEventName = allocAddString("NonOpenMissionStateChange");
			pEvent->tMatchSource = TriState_Yes;
		} else {
			pEvent->pchEventName = allocAddString("OpenMissionStateChange");
		}
		eventtracker_AddNamedEventToList(&pMissionDef->eaTrackedEventsNoSave, pEvent, pMissionDef->filename);
	}
	return 0;
}


AUTO_EXPR_FUNC(player, mission) ACMD_NAME(MissionStateInProgressByTag) ACMD_EXPR_STATIC_CHECK(mission_FuncMissionStateTag_LoadVerify);
int mission_FuncMissionStateInProgressByTag(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcMissionTagName)
{
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	MissionTag eTag = StaticDefineIntGetInt(MissionTagEnum, pcMissionTagName);
	if (pPlayerEnt) {
		// Get the mission info from the player
		MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(pPlayerEnt);
		return mission_HasMissionInStateByTag(pMissionInfo, eTag, MissionState_InProgress);
	} else {
		// Get mission info for open mission
		return openmission_HasMissionInStateByTag(iPartitionIdx, eTag, MissionState_InProgress);
	}
	return false;
}


AUTO_EXPR_FUNC(player, mission) ACMD_NAME(MissionStateSucceededByTag) ACMD_EXPR_STATIC_CHECK(mission_FuncMissionStateTag_LoadVerify);
int mission_FuncMissionStateSucceededByTag(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char* pcMissionTagName)
{
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	MissionTag eTag = StaticDefineIntGetInt(MissionTagEnum, pcMissionTagName);
	if (pPlayerEnt) {
		// Get the mission info from the player
		MissionInfo* pMissionInfo = mission_GetInfoFromPlayer(pPlayerEnt);
		return mission_HasMissionInStateByTag(pMissionInfo, eTag, MissionState_Succeeded);
	} else {
		// Get mission info for open mission
		return openmission_HasMissionInStateByTag(iPartitionIdx, eTag, MissionState_Succeeded);
	}
	return false;
}

AUTO_EXPR_FUNC(Mission) ACMD_NAME(GetTrackedMission);
ExprFuncReturnVal mission_FuncGetTrackedMission(ExprContext *pContext, ACMD_EXPR_STRING_OUT ppcRet, ACMD_EXPR_ERRSTRING errString)
{
	Mission *pMission = exprContextGetVarPointerUnsafePooled(pContext, g_MissionVarName);

	if (pMission && pMission->pchTrackedMission)
	{
		*ppcRet = exprContextAllocString(pContext, pMission->pchTrackedMission);
		return ExprFuncReturnFinished;
	}

	estrPrintf(errString, "This mission does not have a tracked mission. Did you enable Uses Tracked Mission?");
	return ExprFuncReturnError;
}
