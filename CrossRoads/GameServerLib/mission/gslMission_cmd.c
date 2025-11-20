/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Character.h"
#include "characterClass.h"
#include "chatCommon.h"
#include "chatCommonStructs.h"
#include "cmdparse.h"
#include "cmdServerCombat.h"
#include "contact_common.h"
#include "continuousBuilderSupport.h"
#include "Entity.h"
#include "EntityIterator.h"
#include "EntityLib.h"
#include "error.h"
#include "estring.h"
#include "file.h"
#include "GameAccountDataCommon.h"
#include "GameStringFormat.h"
#include "gimmeutils.h"
#include "gslChat.h"
#include "gslContact.h"
#include "gslDoorTransition.h"
#include "gslEntity.h"
#include "gslEventSend.h"
#include "gslEventTracker.h"
#include "gslInteractable.h"
#include "gslInteraction.h"
#include "LoggedTransactions.h"
#include "gslMapState.h"
#include "gslMapTransfer.h"
#include "gslMission.h"
#include "gslMissionEvents.h"
#include "gslMission_transact.h"
#include "gslNotify.h"
#include "gslOpenMission.h"
#include "gslProgression.h"
#include "gslSendToClient.h"
#include "gslUserExperience.h"
#include "gslVolume.h"
#include "gslWaypoint.h"
#include "itemCommon.h"
#include "itemtransaction.h"
#include "mapstate_common.h"
#include "mission_common.h"
#include "namelist.h"
#include "Player.h"
#include "progression_common.h"
#include "RegionRules.h"
#include "ResourceInfo.h"
#include "Survey.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "Team.h"
#include "TimedCallback.h"
#include "Reward.h"
#include "WorldGrid.h"
#include "utilitiesLib.h"

#include "mission_common_h_ast.h"
#include "autogen/gameserverlib_autotransactions_autogen_wrappers.h"
#include "autogen/GameClientLib_AutoGen_ClientCmdWrappers.h"
#include "AutoGen/SoundLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"

// ----------------------------------------------------------------------------------
// Defines/Static Data
// ----------------------------------------------------------------------------------

//Crime Computer default contact
#define CRIME_COMPUTER_DEFAULT "CrimeComputer_Default"

NameList* g_MissionNameList = NULL;


// ----------------------------------------------------------------------------------
// Player Commands: Mission Sharing
// ----------------------------------------------------------------------------------

// Offer this mission to other nearby members of your team.
AUTO_COMMAND ACMD_NAME(mission_ShareMissionCmd) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Social, Mission) ACMD_HIDE;
void mission_CmdShareMission(Entity *pEnt, const char *pcMissionDefName)
{
	mission_ShareMission(pEnt, pcMissionDefName, false, true);
}


// Offer this mission to other nearby members of your team and make it primary
AUTO_COMMAND ACMD_NAME(mission_PrimaryMissionCmd) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Social, Mission) ACMD_NAME(PrimaryMission) ACMD_HIDE;
void mission_CmdPrimaryMission(Entity *pEnt, const char *pcMissionDefName, bool bMakePrimary)
{
	if (bMakePrimary) {
		mission_ShareMission(pEnt, pcMissionDefName, true, true);
		mission_SetPrimaryMission(pEnt, pcMissionDefName);
	} else {
		mission_SetPrimaryMission(pEnt, NULL);	// clear primary mission
	}
}


// ----------------------------------------------------------------------------------
// Player Commands: Mission Tracking
// ----------------------------------------------------------------------------------

static void mission_ToggleTracked(Mission *pMission)
{
	if (pMission) {
		pMission->tracking = !pMission->tracking;
		mission_FlagAsDirty(pMission);
	}
}


// Track/untrack mission in the HUD UI
AUTO_COMMAND ACMD_NAME(player_MissionToggleTracked) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Mission) ACMD_HIDE;
void mission_CmdMissionToggleTracked(Entity *pPlayerEnt, const char *pcMissionName)
{
	MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(pPlayerEnt);
	if (pMissionInfo) {
		Mission *pMission = mission_GetMissionByName(pMissionInfo, pcMissionName);
		if (pMission) {
			mission_ToggleTracked(pMission);
		}
	}
}


// ----------------------------------------------------------------------------------
// Player Commands: Mission Drop
// ----------------------------------------------------------------------------------

// Drop the named mission, if you have it
AUTO_COMMAND ACMD_NAME(player_DropMission) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Mission) ACMD_HIDE;
void mission_CmdDropMission(Entity *pPlayerEnt, const char *pcMissionName)
{
	MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(pPlayerEnt);
	if (pMissionInfo) {
		Mission *pMission = mission_GetMissionByName(pMissionInfo, pcMissionName);
		if (pMission) {
			MissionDef *pMissionDef = mission_GetDef(pMission);

			if (!pMissionDef)
			{
				// mblattel[29Sep11] This has happened at least once on live server for STO with unknown cause. Added AssertOrAlert
				//   per Jared Finder.
				AssertOrAlert("MISSION_DEF_MISSING", "Mission %s had no MissionDef when a request to DropMission happened", pcMissionName);
			}
			else
			{
				if (pMissionDef->doNotAllowDrop || pMission->bChildFullMission) {
					return;
				}
			}
			
			if (pPlayerEnt && pPlayerEnt->pTeam && team_IsTeamLeader(pPlayerEnt)) {
				Team *pTeam = GET_REF(pPlayerEnt->pTeam->hTeam);
				if (pTeam && stricmp(pcMissionName, pTeam->pchPrimaryMission) == 0) {
					mission_SetPrimaryMission(pPlayerEnt, NULL);
				}
			}
			
			missioninfo_DropMission(pPlayerEnt, pMissionInfo, pMission);

			if (pMissionDef) {
				survey_Mission(pPlayerEnt, pMissionDef);
			}
		}
	}
}


// Drop the named mission, if you have it
AUTO_COMMAND ACMD_NAME(player_ForceDropMission) ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Mission) ACMD_HIDE;
void mission_CmdForceDropMission(Entity *pPlayerEnt, const char *pcMissionName)
{
	MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(pPlayerEnt);
	if (pMissionInfo) {
		Mission *pMission = mission_GetMissionByName(pMissionInfo, pcMissionName);
		if (pMission) {
			MissionDef *pMissionDef = mission_GetDef(pMission);

			if (pPlayerEnt && pPlayerEnt->pTeam && team_IsTeamLeader(pPlayerEnt)) {
				Team *pTeam = GET_REF(pPlayerEnt->pTeam->hTeam);
				if (pTeam && stricmp(pcMissionName, pTeam->pchPrimaryMission) == 0) {
					mission_SetPrimaryMission(pPlayerEnt, NULL);
				}
			}

			missioninfo_DropMission(pPlayerEnt, pMissionInfo, pMission);

			if (pMissionDef) {
				survey_Mission(pPlayerEnt, pMissionDef);
			}
		}
	}
}


// ----------------------------------------------------------------------------------
// Player Commands: Crime Computer
// ----------------------------------------------------------------------------------

// Interacts with the player's "Crime Computer"
AUTO_COMMAND ACMD_NAME(player_UseCrimeComputer) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE ACMD_CATEGORY(Mission);
void mission_CmdUseCrimeComputer(Entity *pPlayerEnt)
{
	// TODO - This just uses a hardcoded ContactDef for now.
	// In the future, the ContactDef to use might be stored on the player, or come from
	// an inventory item, etc.
	ContactDef *pContactDef = RefSystem_ReferentFromString(g_ContactDictionary, CRIME_COMPUTER_DEFAULT);

	if (pContactDef && pPlayerEnt) {
		contact_InteractBegin(pPlayerEnt, NULL, pContactDef, NULL, NULL);
	}
}


// ----------------------------------------------------------------------------------
// Player Commands: Teams
// ----------------------------------------------------------------------------------

// Clear the list of your team mate's missions
AUTO_COMMAND ACMD_NAME(mission_ClearTeamMissions) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Mission) ACMD_HIDE;
void mission_CmdClearTeamMissions(Entity *pEnt)
{
	Team *pTeam = team_GetTeam(pEnt);
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEnt);
	
	if (!pEnt || !pInfo) {
		return;
	}
	eaClearStruct(&pInfo->eaTeamMissions, parse_TeamMissionInfo);
	entity_SetDirtyBit(pEnt, parse_MissionInfo, pEnt->pPlayer->missionInfo, true);
	entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, true);
}


// Fetch a list of your team mate's missions
AUTO_COMMAND ACMD_NAME(mission_GetTeamMissions) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Mission) ACMD_HIDE;
void mission_CmdGetTeamMissions(Entity *pEnt)
{
	Team *pTeam = team_GetTeam(pEnt);
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEnt);
	int iMember, iMission, iOtherMission, iOtherMember;
	
	if (!pEnt || !pInfo) {
		return;
	}
	eaClearStruct(&pInfo->eaTeamMissions, parse_TeamMissionInfo);
	entity_SetDirtyBit(pEnt, parse_MissionInfo, pEnt->pPlayer->missionInfo, true);
	entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, true);
	if (!pTeam) {
		return;
	}
	
	// Loop through each team member, to look at their missions
	for (iMember = 0; iMember < eaSize(&pTeam->eaMembers); iMember++) {
		MissionInfo *pMemberInfo;
		pMemberInfo = mission_GetInfoFromPlayer(GET_REF(pTeam->eaMembers[iMember]->hEnt));
		if (!pMemberInfo) {
			continue;
		}
		
		// Loop through each mission on the team member
		for (iMission = 0; iMission < eaSize(&pMemberInfo->missions); iMission++) {
			Mission *pMission = pMemberInfo->missions[iMission];
			MissionDef *pDef = mission_GetDef(pMission);
			bool bAlreadyInList = false;
			
			// Make sure this is a mission we're interested in, and have enough info about ...
			if (pMission->bHiddenFullChild ||
				pMission->bChildFullMission ||
				!pDef ||
				pDef->missionType == MissionType_Perk ||
				pDef->missionType == MissionType_OpenMission ||
				!missiondef_HasDisplayName(pDef)) {
				
				continue;
			}
			
			// Make sure the mission isn't already in the list ...
			for (iOtherMission = 0; iOtherMission < eaSize(&pInfo->eaTeamMissions); iOtherMission++) {
				TeamMissionInfo *pExistingMission = pInfo->eaTeamMissions[iOtherMission];
				if (GET_REF(pExistingMission->hDef) == pDef) {
					bAlreadyInList = true;
					break;
				}
			}
			
			// If it's not on the list, add it
			if (!bAlreadyInList) {
				TeamMissionInfo *pTeamMission = StructCreate(parse_TeamMissionInfo);
				SET_HANDLE_FROM_REFERENT(g_MissionDictionary, pDef, pTeamMission->hDef);
				// Fill out the array of each team member's version of the mission
				for (iOtherMember = 0; iOtherMember < eaSize(&pTeam->eaMembers); iOtherMember++) {
					TeammateMission *pTeammateMission = StructCreate(parse_TeammateMission);
					if (iMember == iOtherMember) {
						// This is the same team member we're already looking at, so this is simpler
						pTeammateMission->pMission = StructClone(parse_Mission, pMission);
						pTeammateMission->eCreditType = pMission->eCreditType;
						pTeammateMission->iEntID = pTeam->eaMembers[iMember]->iEntID;
					} else {
						Entity *pOtherEntity = GET_REF(pTeam->eaMembers[iOtherMember]->hEnt);
						MissionInfo *pOtherInfo = mission_GetInfoFromPlayer(pOtherEntity);
						Mission *pOtherMission = pOtherInfo ? mission_FindMissionFromDef(pOtherInfo, pDef) : NULL;
						
						pTeammateMission->iEntID = pTeam->eaMembers[iOtherMember]->iEntID;
						if (pOtherMission) {
							pTeammateMission->pMission = StructClone(parse_Mission, pOtherMission);
							pTeammateMission->eCreditType = pOtherMission->eCreditType;
						} else {
							missiondef_CanBeOfferedAtAll(pOtherEntity, pDef, NULL, NULL, &pTeammateMission->eCreditType);
						}
					}
					eaPush(&pTeamMission->eaTeammates, pTeammateMission);
				}
				eaPush(&pInfo->eaTeamMissions, pTeamMission);
			}
		}
	}
}


// ----------------------------------------------------------------------------------
// Remote Commands for transaction callbacks
// ----------------------------------------------------------------------------------

AUTO_COMMAND_REMOTE;
void mission_RemoteFlagPerkPointRefresh(CmdContext *pContext)
{
	if (pContext && pContext->clientType == GLOBALTYPE_ENTITYPLAYER) {
		Entity *pEnt = entFromContainerIDAnyPartition(pContext->clientType, pContext->clientID);
		if (pEnt && pEnt->pPlayer && pEnt->pPlayer->missionInfo) {
			pEnt->pPlayer->missionInfo->bPerkPointsNeedEval = 1;
		}
	}
}


AUTO_COMMAND_REMOTE;
void mission_RemoteFlagMissionRequestUpdate(CmdContext *pContext)
{
	if (pContext && pContext->clientType == GLOBALTYPE_ENTITYPLAYER) {
		Entity *pEnt = entFromContainerIDAnyPartition(pContext->clientType, pContext->clientID);
		if (pEnt && pEnt->pPlayer && pEnt->pPlayer->missionInfo) {
			pEnt->pPlayer->missionInfo->bRequestsNeedEval = 1;
		}
	}
}


AUTO_COMMAND_REMOTE;
void mission_RemoteMissionFlagAsNeedingEval(const char *pcMissionRefString, bool bUpdateTimestamp, CmdContext *pContext)
{
	if (pContext && pContext->clientType == GLOBALTYPE_ENTITYPLAYER) {
		Entity *pEnt = entFromContainerIDAnyPartition(pContext->clientType, pContext->clientID);
		MissionInfo *pInfo = pEnt?mission_GetInfoFromPlayer(pEnt):NULL;
		if (pEnt && pInfo) {
			Mission *pMission = mission_FindMissionFromRefString(pInfo, pcMissionRefString);
			if (pMission) {
				mission_FlagAsNeedingEval(pMission);
				if (bUpdateTimestamp) {
					MissionDef *pDef = mission_GetDef(pMission);
					mission_UpdateTimestamp(pDef, pMission);
				}
			}
		}
	}
}


// This is called the first time a Perk is persisted
AUTO_COMMAND_REMOTE;
void mission_RemoteUpdateEventCount(const char *pcRootMissionName, const char *pcMissionName, const char *pcEventName, int iAmount, bool bSet, CmdContext *pContext)
{
	if (pContext && pContext->clientType == GLOBALTYPE_ENTITYPLAYER) {
		Entity *pEnt = entFromContainerIDAnyPartition(pContext->clientType, pContext->clientID);
		MissionInfo *pInfo = pEnt ? mission_GetInfoFromPlayer(pEnt) : NULL;

		if (pInfo && pcRootMissionName){
			Mission *pMission = eaIndexedGetUsingString(&pInfo->missions, pcRootMissionName);
		
			if (pMission && pcMissionName && stricmp(pcMissionName, pcRootMissionName)!=0) {
				pMission = mission_FindChildByName(pMission, pcMissionName);
			}

			if (pMission){
				MissionEventContainer *pEntry = eaIndexedGetUsingString(&pMission->eaEventCounts, pcEventName);
				if (!pEntry) {
					// Because field is NO_INDEXED_PREALLOC we need to do this manually
					eaIndexedEnable(&pMission->eaEventCounts, parse_MissionEventContainer);

					pEntry = StructCreate(parse_MissionEventContainer);
					pEntry->pchEventName = allocAddString(pcEventName);
					pEntry->iEventCount = 0;
					eaIndexedAdd(&pMission->eaEventCounts, pEntry);
				}
				if (pEntry){
					if (bSet) {
						pEntry->iEventCount = iAmount;
					} else {
						pEntry->iEventCount += iAmount;
					}
				}
				mission_FlagAsDirty(pMission);
				mission_FlagAsNeedingEval(pMission);

				// Log for user experience system
				UserExp_LogMissionProgress(pEnt, pcRootMissionName, pcMissionName, pcEventName, pEntry->iEventCount);
			}
		}
	}
}


// This is called whenever a non-persisted discovered mission is persisted
AUTO_COMMAND_REMOTE;
void mission_RemoteDiscoverMission(const char *pcRootMissionName, CmdContext *pContext)
{
	if (pContext && pContext->clientType == GLOBALTYPE_ENTITYPLAYER) {
		Entity *pEnt = entFromContainerIDAnyPartition(pContext->clientType, pContext->clientID);
		MissionInfo *pInfo = pEnt ? mission_GetInfoFromPlayer(pEnt) : NULL;

		if (pInfo && pcRootMissionName) {
			Mission *pMission = eaIndexedGetUsingString(&pInfo->missions, pcRootMissionName);
			if (pMission) {
				mission_DiscoverMission(pMission);
			}
		}
	}
}


// This is called when a mission or sub-mission changes state
AUTO_COMMAND_REMOTE;
void mission_RemoteMissionStateChange(const char *pcMissionRefString, MissionState eNewState,
								bool bRootMission, CmdContext *pContext)
{
	if (pContext && pContext->clientType == GLOBALTYPE_ENTITYPLAYER) {
		Entity *pEnt = entFromContainerIDAnyPartition(pContext->clientType, pContext->clientID);
		MissionInfo *pInfo = pEnt ? mission_GetInfoFromPlayer(pEnt) : NULL;
		Mission *pMission = pInfo ? mission_FindMissionFromRefString(pInfo, pcMissionRefString) : NULL;
		MissionDef *pDef = pMission ? mission_GetDef(pMission) : NULL;
		MissionDef *pRootDef = pDef;
		int iPartitionIdx = entGetPartitionIdx(pEnt);
		
		while (pRootDef && GET_REF(pRootDef->parentDef)) {
			pRootDef = GET_REF(pRootDef->parentDef);
		}

		if (pEnt && pInfo) {
			// Send an Event
			iPartitionIdx = entGetPartitionIdx(pEnt);
			eventsend_RecordMissionState(iPartitionIdx, pEnt, pDef ? pDef->pchRefString : allocAddString(pcMissionRefString), missiondef_GetType(pDef), eNewState, pDef?REF_STRING_FROM_HANDLE(pDef->hCategory):NULL, bRootMission, pMission ? pMission->pUGCMissionData : NULL);
		}

		// Flag Mission for refresh
		if (pMission) {
			// Update Mission Waypoints
			waypoint_UpdateMissionWaypoints(pInfo, pMission);
			mission_FlagAsNeedingEval(pMission);
			mission_UpdateTimestamp(pDef, pMission);
		}

		// If this mission entered a state that can never change again, it should stop tracking Events
		if (eNewState == MissionState_Failed || (eNewState == MissionState_Succeeded && pDef && pDef->doNotUncomplete)) {
			missionevent_StopTrackingEvents(iPartitionIdx, pMission);
		}

		// Send notifications
		if (pDef && bRootMission && missiondef_HasDisplayName(pDef) && missiondef_GetType(pDef) != MissionType_Perk) {
			if (eNewState == MissionState_Succeeded && !pDef->bIsHandoff) {
				notify_SendMissionNotification(pEnt, NULL, pDef, MISSION_COMPLETE_MSG, kNotifyType_MissionSuccess);
			} else if(eNewState == MissionState_Failed) {
				notify_SendMissionNotification(pEnt, NULL, pDef, MISSION_FAILED_MSG, kNotifyType_MissionFailed);
			}
		}
		if (pDef && !bRootMission && eNewState == MissionState_Succeeded)
		{
			if (missiondef_HasUIString(pDef) && missiondef_HasDisplayName(pRootDef)) {
				notify_SendMissionNotification(pEnt, NULL, pDef, MISSION_SUBOBJECTIVE_COMPLETE_MSG, kNotifyType_MissionSubObjectiveComplete);
			}
			else {
				notify_SendMissionNotification(pEnt, NULL, pDef, MISSION_INVIS_SUBOBJECTIVE_COMPLETE_MSG, kNotifyType_MissionInvisibleSubObjectiveComplete);
			}
		}
		
	}
}


AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void mission_RemoteSendQueueMissionOffer(ContainerID iEntID, GlobalType iSharedType, ContainerID iSharedId,
										const char *pcMissionDefName, MissionCreditType eCreditType, U32 uTimerStartTime, bool bSilent)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iEntID);
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->missionInfo) {
		MissionDef *pMissionDef = RefSystem_ReferentFromString(g_MissionDictionary, pcMissionDefName);
		if (pMissionDef) {	
			QueuedMissionOffer *pOffer = StructCreate(parse_QueuedMissionOffer);

			pOffer->timestamp = timeSecondsSince2000();
			pOffer->eSharerType = iSharedType;
			pOffer->uSharerID = iSharedId;
			pOffer->bSilent = bSilent;
			pOffer->uTimerStartTime = uTimerStartTime;
			pOffer->eCreditType = eCreditType;
			SET_HANDLE_FROM_REFERENT(g_MissionDictionary, pMissionDef, pOffer->hMissionDef);

			eaPush(&pEnt->pPlayer->missionInfo->eaQueuedMissionOffers, pOffer);
		}
	}
}


// ----------------------------------------------------------------------------------
// Debugging Commands
// ----------------------------------------------------------------------------------


// Create a name list to use for the mission name auto commands
AUTO_STARTUP(Missions);
void mission_CreateNameList(void)
{
	g_MissionNameList = CreateNameList_RefDictionary("Mission");
}


// missioncomplete <missionName>: Completes mission of the given name
// To complete a sub-mission, use "missioncomplete missionname::submissionname"
AUTO_COMMAND ACMD_NAME(MissionComplete) ACMD_ACCESSLEVEL(4);
void mission_CmdMissionComplete(Entity *pPlayerEnt, ACMD_NAMELIST("AllMissionsIndex", RESOURCEDICTIONARY) char *pcMissionName)
{
	MissionInfo *pInfo;
	if (pPlayerEnt && (pInfo = mission_GetInfoFromPlayer(pPlayerEnt))) {
		Mission *pMission = mission_FindMissionFromRefString(pInfo, pcMissionName);
		if (pMission) {
			mission_CompleteMission(pPlayerEnt, pMission, true);
		}
	}
}


// OpenMissionCompleteCurrentPhase: Completes the current phase of the open mission on the player
//  Current phase is defined as the first InProgress mission.
AUTO_COMMAND ACMD_NAME(MissionCompleteCurrentPhase);
void mission_CmdMissionCompleteCurrentPhase(Entity *pPlayerEnt, ACMD_NAMELIST("AllMissionsIndex", RESOURCEDICTIONARY) char *pcMissionName)
{
	MissionInfo *pInfo;
	if (pPlayerEnt && (pInfo = mission_GetInfoFromPlayer(pPlayerEnt))) {
		Mission *pMission = mission_FindMissionFromRefString(pInfo, pcMissionName);
		if (pMission) {
			mission_CompleteCurrentPhase(pPlayerEnt, pMission);
		}
	}
}


// MissionTriggerCurrentEvent: Attempts to trigger the events listened to by the current mission
//  Sets the triggering player as the source ent.  Might not work correctly for events requiring
//  a target entity
AUTO_COMMAND ACMD_NAME(MissionTriggerCurrentEvent);
void mission_CmdMissionTriggerCurrentEvent(Entity *pPlayerEnt, ACMD_NAMELIST("AllMissionsIndex", RESOURCEDICTIONARY) char *pcMissionName)
{
	MissionInfo *pInfo;
	if (pPlayerEnt && (pInfo = mission_GetInfoFromPlayer(pPlayerEnt))) {
		Mission *pMission = mission_FindMissionFromRefString(pInfo, pcMissionName);
		if (pMission) {
			mission_TriggerCurrentEvent(pPlayerEnt, pMission);
		}
	}
}


// missionadd <missionName>: Adds a mission of the given name to the player
AUTO_COMMAND ACMD_NAME(MissionAdd) ACMD_ACCESSLEVEL(5);
void mission_CmdMissionAdd(Entity *pPlayerEnt, ACMD_NAMELIST(g_MissionNameList) char *pcMissionName)
{
	MissionInfo *pInfo;
	if (pPlayerEnt && (pInfo = mission_GetInfoFromPlayer(pPlayerEnt))) {
		missioninfo_AddMissionByName(entGetPartitionIdx(pPlayerEnt), pInfo, pcMissionName, NULL, NULL);
	}
}


// missionCooldownClear <missionName>: Clears the cooldown for this mission
AUTO_COMMAND ACMD_NAME(MissionCooldownClear) ACMD_ACCESSLEVEL(5);
void mission_CmdMissionCooldownClear(Entity *pPlayerEnt, ACMD_NAMELIST(g_MissionNameList) char *pcMissionName)
{
	if (pPlayerEnt) {
		missioninfo_ChangeCooldown(pPlayerEnt, pcMissionName, true, 1, true, 0);
	}
}


// missioninforeset: Resets a players mission info if they were just created
AUTO_COMMAND ACMD_NAME(MissionInfoReset) ACMD_ACCESSLEVEL(7);
void mission_CmdMissionInfoReset(Entity *pPlayerEnt)
{
	if (pPlayerEnt) {
		missioninfo_ResetMissionInfo(pPlayerEnt);

		// Also reset the "recently visited contacts" list
		if (pPlayerEnt->pPlayer && pPlayerEnt->pPlayer->eaRecentContacts) {
			eaDestroy(&pPlayerEnt->pPlayer->eaRecentContacts);
			pPlayerEnt->pPlayer->uRecentContactsIndex = 0;
		}
	}
}


AUTO_COMMAND ACMD_NAME(MissionCompleteAllPerks);
void mission_CmdMissionCompleteAllPerks(Entity *pPlayerEnt)
{
	// This is a little trickier than it sounds, since completing a Perk
	// can change the state of other Perks, which modifies the list.
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
	if (pInfo) {
		int iNumTries = 0;
		int i;

		while (iNumTries++ < 10000){
			// Complete a single Perk at a time, then start over from the beginning
			bool bPerkFound = false;
			for (i = eaSize(&pInfo->missions)-1; i >= 0; --i) {
				MissionDef *pDef = mission_GetDef(pInfo->missions[i]);
				if (pDef && pDef->missionType == MissionType_Perk && pInfo->missions[i]->state != MissionState_Succeeded) {
					mission_CompleteMission(pPlayerEnt, pInfo->missions[i], true);
					bPerkFound = true;
					break;
				}
			}
			if (!bPerkFound) {
				for (i = eaSize(&pInfo->eaNonPersistedMissions)-1; i >= 0; --i) {
					MissionDef *pDef = mission_GetDef(pInfo->eaNonPersistedMissions[i]);
					if (pDef && pDef->missionType == MissionType_Perk && pInfo->eaNonPersistedMissions[i]->state != MissionState_Succeeded) {
						mission_CompleteMission(pPlayerEnt, pInfo->eaNonPersistedMissions[i], true);
						bPerkFound = true;
						break;
					}
				}
			}
			if (!bPerkFound) {
				for (i = eaSize(&pInfo->eaDiscoveredMissions)-1; i >= 0; --i) {
					MissionDef *pDef = mission_GetDef(pInfo->eaDiscoveredMissions[i]);
					if (pDef && pDef->missionType == MissionType_Perk && pInfo->eaDiscoveredMissions[i]->state != MissionState_Succeeded) {
						mission_CompleteMission(pPlayerEnt, pInfo->eaDiscoveredMissions[i], true);
						bPerkFound = true;
						break;
					}
				}
			}

			if (!bPerkFound) {
				break;
			}
		}
	}
}


// missioncompleteall: Completes all non-perk missions on the player. For perk missions use MissionCompleteAllPerks.
AUTO_COMMAND ACMD_NAME(MissionCompleteAll) ACMD_ACCESSLEVEL(4);
void mission_CmdMissionCompleteAll(Entity *pPlayerEnt)
{
	MissionInfo* pInfo;
	if (pPlayerEnt && (pInfo = mission_GetInfoFromPlayer(pPlayerEnt))) {
		int i;

		for (i = eaSize(&pInfo->missions)-1; i >= 0; i--) {
			Mission* pMission = pInfo->missions[i];
			MissionDef *pDef = mission_GetDef(pMission);
			if (pDef && pDef->missionType != MissionType_Perk && pMission->state != MissionState_Succeeded) {
				mission_CompleteMission(pPlayerEnt, pMission, true);
			}
		}

		for (i = eaSize(&pInfo->eaNonPersistedMissions)-1; i >= 0; i--) {
			Mission* pMission = pInfo->eaNonPersistedMissions[i];
			MissionDef *pDef = mission_GetDef(pMission);
			if (pDef && pDef->missionType != MissionType_Perk && pMission->state != MissionState_Succeeded) {
				mission_CompleteMission(pPlayerEnt, pMission, true);
			}
		}

		for (i = eaSize(&pInfo->eaDiscoveredMissions)-1; i >= 0; i--) {
			Mission* pMission = pInfo->eaDiscoveredMissions[i];
			MissionDef *pDef = mission_GetDef(pMission);
			if (pDef && pDef->missionType != MissionType_Perk && pMission->state != MissionState_Succeeded) {
				mission_CompleteMission(pPlayerEnt, pMission, true);
			}
		}
	}
}


// repeatmissions: Completed missions can be re-granted
AUTO_COMMAND ACMD_NAME(RepeatMissions);
void mission_CmdRepeatMissions(Entity *pPlayerEnt)
{
	PlayerDebug *pDebug = entGetPlayerDebug(pPlayerEnt, true);
	if (pDebug) {
		pDebug->canRepeatMissions ^= !pDebug->canRepeatMissions;
		entity_SetDirtyBit(pPlayerEnt, parse_Player, pPlayerEnt->pPlayer, false);
	}
}


// Reset the player's ambush timer for testing
AUTO_COMMAND ACMD_NAME(AmbushTimerReset);
void mission_CmdAmbushTimerReset(Entity *pPlayerEnt)
{
	if (pPlayerEnt && pPlayerEnt->pPlayer) {
		U32 uTimeToAmbush = 0;
		Player *pPlayer = pPlayerEnt->pPlayer;

		if (pPlayer->nextAmbushTime > timeSecondsSince2000()) {
			uTimeToAmbush = pPlayer->nextAmbushTime - timeSecondsSince2000();
			pPlayer->nextAmbushTime = 0;
			printf("Ambush timer reset from %d seconds to 0 seconds\n", uTimeToAmbush);
		} else {
			printf("Ambush timer already at 0 seconds\n");
		}
	}
}


AUTO_COMMAND ACMD_NAME(DumpMissionInfo) ACMD_SERVERCMD ;
char *mission_CmdDumpMissionInfo(Entity *pPlayerEnt)
{
	FILE *file = fopen("c:\\MissionInfo.txt", "w");

	if (pPlayerEnt->pPlayer && pPlayerEnt->pPlayer->missionInfo) {
		ParserWriteTextFile("c:\\MissionInfo.txt", parse_MissionInfo, pPlayerEnt->pPlayer->missionInfo, 0, 0);
	}

	fclose(file);
	return "Player mission info written to 'c:\\MissionInfo.txt'";
}


AUTO_COMMAND ACMD_NAME(SetSecondaryMissionCooldownHours);
void mission_CmdSetSecondaryMissionCooldownHours(F32 fTimeInHours)
{
	gConf.fSecondaryMissionCooldownHours = fTimeInHours;
}


static bool mission_SetHidden(Mission *pMission, S32 bHidden)
{
	bool bMadeChange = false;
	// If the mission has a display name and is the correct type then add it to the list
	if (pMission
		&& mission_HasDisplayName(pMission)
		&& (mission_GetType(pMission) == MissionType_Normal
		|| mission_GetType(pMission) == MissionType_Nemesis
		|| mission_GetType(pMission) == MissionType_NemesisArc
		|| mission_GetType(pMission) == MissionType_NemesisSubArc
		|| mission_GetType(pMission) == MissionType_Episode
		|| mission_GetType(pMission) == MissionType_AutoAvailable)
		&& !pMission->bHiddenFullChild
		)
	{
		if (pMission->bHidden != bHidden) {
			bMadeChange = true;
			if (bHidden < 0) {
				pMission->bHidden = 1 - pMission->bHidden;
			} else {
				pMission->bHidden = bHidden;
			}
		}
	}

	if (bMadeChange) {
		entity_SetDirtyBit(NULL, parse_Mission, pMission, true);
	}

	return bMadeChange;
}


// Set this mission to hidden (for the hud), bHidden: 1==hide, 0==don't hide, -1==toggle
AUTO_COMMAND ACMD_NAME(Mission_SetMissionHidden) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void mission_CmdSetMissionHidden(Entity *pPlayerEnt, ACMD_NAMELIST(g_MissionNameList) char *pcMissionName, int bHidden)
{
	MissionInfo *pInfo;
	if (pcMissionName && pcMissionName[0] && pPlayerEnt && (pInfo = mission_GetInfoFromPlayer(pPlayerEnt))) {
		S32 i;
		for(i = 0; i < eaSize(&pInfo->missions); ++i) {
			MissionDef *pMisDef = mission_GetDef(pInfo->missions[i]);
			if (pMisDef && stricmp_safe(pcMissionName, pMisDef->name) == 0) {
				if (mission_SetHidden(pInfo->missions[i], bHidden)) {
					mission_FlagInfoAsDirty(pInfo);
				}
				return;
			}
		}
	}
}


// Set all missions to hidden (for the hud), bHidden: 1==hide, 0==don't hide, -1==toggle
AUTO_COMMAND ACMD_NAME(Mission_SetAllMissionHidden) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void mission_CmdSetAllMissionHidden(Entity *pPlayerEnt, int bHidden)
{
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
	if (pInfo) {
		S32 i;
		bool bDirty = false;
		for(i = 0; i < eaSize(&pInfo->missions); ++i) {
			bDirty |= mission_SetHidden(pInfo->missions[i], bHidden);
		}

		if (bDirty) {
			mission_FlagInfoAsDirty(pInfo);
		}
	}
}


// ---------------------------------------------------------------------
// Data Reporting commands
// ---------------------------------------------------------------------

// This was used as part of the fix-up above, but I'm leaving it in because it may be useful for designers
static void mission_FindAllContactsWhoGrantMission(MissionDef *pDef, ContactDef ***peaContacts, bool *pbSameText)
{
	DictionaryEArrayStruct *pStruct = resDictGetEArrayStruct("Contact");
	const char *pcOfferText = NULL;
	int i, n;

	if (pbSameText) {
		*pbSameText = true;
	}

	// Find all Contacts who grant this mission
	n = eaSize(&pStruct->ppReferents);
	for (i = 0; i < n; i++) {
		ContactDef *pContact = pStruct->ppReferents[i];
		ContactMissionOffer **eaOfferList = NULL;
		int j, k;

		contact_GetMissionOfferList(pContact, NULL, &eaOfferList);

		k = eaSize(&eaOfferList);
		for(j=0; j<k; j++) {
			ContactMissionOffer *pOffer = eaOfferList[j];
			if (GET_REF(pOffer->missionDef) && (0 == stricmp(pDef->name, GET_REF(pOffer->missionDef)->name)) && (pOffer->allowGrantOrReturn != ContactMissionAllow_ReturnOnly)) {
				int iDialog, iNumDialogs = eaSize(&pOffer->offerDialog);

				// If this contact hasn't already been altered, make an editor copy and add it to the list
				eaPushUnique(peaContacts, pContact);
				
				// Iterate through dialog text for this mission
				for (iDialog = 0; iDialog < iNumDialogs; iDialog++) {
					Message *pDisplayText = NULL;
					if ((pDisplayText = GET_REF(pOffer->offerDialog[iDialog]->displayTextMesg.hMessage)) && pDisplayText->pcDefaultString && pDisplayText->pcDefaultString[0])
					{
						if (pcOfferText && stricmp(pcOfferText, pDisplayText->pcDefaultString)) {
							if (*pbSameText) {
								*pbSameText = false;
							}
						} else if (!pcOfferText) {
							pcOfferText = pDisplayText->pcDefaultString;
						}
					}
				}
			}
		}

		eaDestroy(&eaOfferList);
	}
}

// Prints a list of all contacts that grant missions (and have a display name)
AUTO_COMMAND ACMD_NAME(ContactsThatGrantMission);
void mission_CmdContactsThatGrantMissions(const char *pchPath, bool bShowAll)
{
	DictionaryEArrayStruct *pStruct = resDictGetEArrayStruct("Contact");
	S32 i, n, numContacts;
	ContactDef **eaContacts = NULL;
	FILE* out;
	char *eaOut = NULL;
	const char* pchRowEnd = "\r\n";

	// init file
	makeDirectoriesForFile(pchPath);
	if (fileIsAbsolutePath(pchPath))
	{
		out = fopen(pchPath, "w");
	} else
	{
		out = fileOpen(pchPath, "w");
	}

	if (!out)
	{
		Errorf("Couldn't open file %s for writing", pchPath);
		return;
	}

	n = eaSize(&pStruct->ppReferents);
	for(i = 0; i < n; ++i)
	{
		ContactDef *pContact = pStruct->ppReferents[i];
		if((bShowAll || GET_REF(pContact->displayNameMsg.hMessage)) && eaSize(&pContact->offerList) > 0)
		{
			eaPush(&eaContacts, pContact);
		}
	}

	numContacts = eaSize(&eaContacts);

	// write these out
	for(i = 0; i < numContacts; ++i)
	{
		estrPrintf(&eaOut, "Contact Name: %s%s", eaContacts[i]->name, pchRowEnd);
		fwrite(eaOut, estrLength(&eaOut), sizeof(char), out);

	}

	estrDestroy(&eaOut);
	eaDestroy(&eaContacts);
	fclose(out);
}

// Prints a list of all Missions that are granted from multiple contacts
AUTO_COMMAND ACMD_NAME(MissionCheckMultipleContacts);
void mission_CmdMissionCheckMultipleContacts(void)
{
	DictionaryEArrayStruct *pStruct = resDictGetEArrayStruct(g_MissionDictionary);
	int i, n = eaSize(&pStruct->ppReferents);
	int j, m;
	for (i = 0; i < n; i++) {
		MissionDef *missionDef = pStruct->ppReferents[i];
		ContactDef **ppContacts = NULL;
		bool sameText;
		mission_FindAllContactsWhoGrantMission(pStruct->ppReferents[i], &ppContacts, &sameText);
		if (eaSize(&ppContacts) > 1) {
			char *estrError = NULL;
			estrStackCreate(&estrError);
			estrConcatf(&estrError, "%s granted by: ", missionDef->name);
			m = eaSize(&ppContacts);
			for (j = 0; j < m; j++) {
				estrConcatf(&estrError, "%s ", ppContacts[j]->name);
			}
			if (!sameText) {
				estrConcatf(&estrError, "and text is different!");
			}
			Alertf("%s", estrError);
			estrDestroy(&estrError);
		}
		eaDestroy(&ppContacts);
	}
}

// Recursive helper function for MissionChainReport. Typically we should avoid recursion due to the chance of
// stack overflows, but given that this is a debug command that will almost never be run (And should never, ever be
// run on live servers), this should be fine.
void mission_CmdMissionChainReport_Write(MissionDef *pMissionDef, FILE *out, MissionDef ***peaMissionDefLoopCheck)
{
	char *eaOut = NULL;
	const char* pchRowEnd = "\r\n";
	char *eaNotPrereqOut = NULL;
	MissionDef *pPrereqDef = NULL;
	int i, iSize;
	int *eaiIndices = NULL;

	iSize = eaSize(peaMissionDefLoopCheck);
	if (eaPushUnique(peaMissionDefLoopCheck, pMissionDef) != iSize)
	{
		estrPrintf(&eaOut, "LOOP FOUND: %s%s", pMissionDef->name, pchRowEnd);
		fwrite(eaOut, estrLength(&eaOut), sizeof(char), out);
		estrDestroy(&eaOut);
		return;
	}

	if (pMissionDef->missionReqs)
	{
		exprFindFunctions(pMissionDef->missionReqs, "HasCompletedMission",  &eaiIndices);
		for (i = 0; i < eaiSize(&eaiIndices); i++)
		{
			const MultiVal *pStringVal = exprFindFuncParam(pMissionDef->missionReqs, eaiIndices[i], 0);
			const char *pcMissionName = MultiValGetString(pStringVal, NULL);
			pPrereqDef = pcMissionName ? missiondef_DefFromRefString(pcMissionName) : NULL;

			estrPrintf(&eaNotPrereqOut, "not HasCompletedMission(\"%s\")", pcMissionName);
			if (pPrereqDef && !IS_HANDLE_ACTIVE(pPrereqDef->parentDef) && !exprMatchesString(pMissionDef->missionReqs, eaNotPrereqOut))
			{
				mission_CmdMissionChainReport_Write(pPrereqDef, out, peaMissionDefLoopCheck);
			}
		}

		eaiDestroy(&eaiIndices);
	}

	estrPrintf(&eaOut, "%s%s", pMissionDef->name, pchRowEnd);
	fwrite(eaOut, estrLength(&eaOut), sizeof(char), out);
	estrDestroy(&eaOut);

	eaPop(peaMissionDefLoopCheck);
}

// DO NOT RUN THIS ON LIVE SERVERS! Outputs a report of all mission chains to a text file.
// This command is dangerous due to the chance that it can produce a stack overflow.
// Run this on local servers only.
AUTO_COMMAND ACMD_NAME(MissionChainReport);
void mission_CmdMissionChainReport(const char *pchPath)
{
	DictionaryEArrayStruct *pStruct = resDictGetEArrayStruct("MissionDef");
	S32 i, j, n;
	MissionDef **eaMissionDefs = NULL;
	FILE* out;
	char *eaOut = NULL;
	char *eaNotPrereqOut = NULL;
	int *eaiIndices = NULL;

	makeDirectoriesForFile(pchPath);
	if (fileIsAbsolutePath(pchPath))
	{
		out = fopen(pchPath, "w");
	} else
	{
		out = fileOpen(pchPath, "w");
	}

	if (!out)
	{
		Errorf("Couldn't open file %s for writing", pchPath);
		return;
	}

	n = eaSize(&pStruct->ppReferents);
	for (i = 0; i < n; i++)
	{
		MissionDef *pMissionDef = pStruct->ppReferents[i];
		if (!pMissionDef->missionReqs || IS_HANDLE_ACTIVE(pMissionDef->parentDef))
		{
			eaPushUnique(&eaMissionDefs, pMissionDef);
			continue;
		}

		eaiClear(&eaiIndices);
		exprFindFunctions(pMissionDef->missionReqs, "HasCompletedMission",  &eaiIndices);

		if (eaiSize(&eaiIndices) == 0)
		{
			eaPushUnique(&eaMissionDefs, pMissionDef);
			continue;
		}

		for (j = 0; j < eaiSize(&eaiIndices); j++)
		{
			const MultiVal *pStringVal = exprFindFuncParam(pMissionDef->missionReqs, eaiIndices[j], 0);
			const char *pcMissionName = MultiValGetString(pStringVal, NULL);
			MissionDef *pPrereqDef = pcMissionName ? missiondef_DefFromRefString(pcMissionName) : NULL;
			estrPrintf(&eaNotPrereqOut, "not HasCompletedMission(\"%s\")", pcMissionName);
			if (pPrereqDef && !exprMatchesString(pMissionDef->missionReqs, eaNotPrereqOut))
			{
				eaPushUnique(&eaMissionDefs, pPrereqDef);
			}
		}
	}

	eaiDestroy(&eaiIndices);

	for (i = 0; i < n; i++)
	{
		MissionDef *pMissionDef = pStruct->ppReferents[i];
		if (eaFind(&eaMissionDefs, pMissionDef) == -1)
		{
			bool bFound = false;
			MissionDef **eaMissionDefLoopCheck = NULL;

			mission_CmdMissionChainReport_Write(pMissionDef, out, &eaMissionDefLoopCheck);

			estrPrintf(&eaOut, "\r\n");
			fwrite(eaOut, estrLength(&eaOut), sizeof(char), out);
		}
	}

	estrDestroy(&eaOut);
	eaDestroy(&eaMissionDefs);
	fclose(out);
}

// Sort missions by display name first and by logical name second.
// Note: Uses server's current locale to translate display messages
static int mission_CmpMissionDefByDisplayName(const void *a, const void *b)
{
	const MissionDef* defA = *(MissionDef**)a;
	const MissionDef* defB = *(MissionDef**)b;
	int ret = 0;
	
	if (defA && defB) {
		ret = stricmp(TranslateDisplayMessage(defA->displayNameMsg), TranslateDisplayMessage(defB->displayNameMsg));
		if (0 == ret) {
			ret = stricmp(defA->name, defB->name);
		}
	}
	return ret;
}


// Outputs a table with the display and internal names of all the missions to a file in either Wiki or HTML format.
// The table is formatted as follows:
// [Display Name | Internal Name | Target map(s) display name(s) ]
// [			 | Sub-mission display string | Sub-mission internal name | Sub-mission target map(s) display name(s)]
AUTO_COMMAND ACMD_NAME(Mission_WriteListToFile);
void mission_CmdWriteListToFile(const char* pchPath, bool bHTML)
{
	DictionaryEArrayStruct *pStruct = resDictGetEArrayStruct(g_MissionDictionary);
	MissionDef** eaSortedDefs = NULL;
	int i, s, j, n = eaSize(&pStruct->ppReferents);
	FILE* out;
	char* estrLine = NULL;
	char* estrDisplayName = NULL;
	const char* pchFileHeader = bHTML ? "<html><body><table border=1>\r\n" : "";
	const char* pchRow = bHTML ? "<tr>" : "";
	const char* pchRowEnd = bHTML ? "</tr>\r\n" : " |\n";
	const char* pchHeaderRow = bHTML ? "<tr bgcolor=lightgray>" : "";
	const char* pchHeaderRowEnd = bHTML ? "</tr>\r\n" : " ||\n";
	const char* pchCol = bHTML ? "<td>" : "| ";
	const char* pchColEnd = bHTML ? "</td>" : " ";
	const char* pchHeaderCol = bHTML ? "<td><b>" : "|| ";
	const char* pchHeaderColEnd = bHTML ? "</b></td>" : " ";
	const char* pchFileFooter = bHTML ? "</table></body></html>\r\n" : "";

	// init file
	makeDirectoriesForFile(pchPath);
	if (fileIsAbsolutePath(pchPath)) {
		out = fopen(pchPath, "w");
	} else {
		out = fileOpen(pchPath, "w");
	}
	if (!out) {
		Errorf("Couldn't open file %s for writing", pchPath);
		return;
	}

	// Sort Defs
	eaCopy(&eaSortedDefs, &((MissionDef**)pStruct->ppReferents));
	eaQSort(eaSortedDefs, mission_CmpMissionDefByDisplayName);

	// Write header
	estrCreate(&estrLine);
	estrCreate(&estrDisplayName);

	fwrite(pchFileHeader, strlen(pchFileHeader), sizeof(char), out);
	if (!bHTML) {
		fprintf(out, "h3. %s missions, branch %d, autogen %s\n", GetProductName(), Gimme_GetBranchNum(fileDataDir()), timeGetLocalDateNoTimeStringFromSecondsSince2000(timeSecondsSince2000()));
	}
	estrAppend2(&estrLine, pchHeaderRow);
	estrAppend2(&estrLine, pchHeaderCol);
	estrAppend2(&estrLine, "Mission Display Name");
	estrAppend2(&estrLine, pchHeaderColEnd);
	estrAppend2(&estrLine, pchHeaderCol);
	estrAppend2(&estrLine, "Mission Internal Name");
	estrAppend2(&estrLine, pchHeaderColEnd);
	estrAppend2(&estrLine, pchHeaderCol);
	estrAppend2(&estrLine, pchHeaderColEnd);
	estrAppend2(&estrLine, pchHeaderCol);
	estrAppend2(&estrLine, "Target Map(s) Display Name");
	estrAppend2(&estrLine, pchHeaderColEnd);
	estrAppend2(&estrLine, pchHeaderCol);
	estrAppend2(&estrLine, "Target Map(s) Internal Name");
	estrAppend2(&estrLine, pchHeaderColEnd);
	estrAppend2(&estrLine, pchHeaderRowEnd);

	estrAppend2(&estrLine, pchHeaderRow);
	estrAppend2(&estrLine, pchHeaderCol);
	estrAppend2(&estrLine, pchHeaderColEnd);
	estrAppend2(&estrLine, pchHeaderCol);
	estrAppend2(&estrLine, "Sub-Mission UI String");
	estrAppend2(&estrLine, pchHeaderColEnd);
	estrAppend2(&estrLine, pchHeaderCol);
	estrAppend2(&estrLine, "Sub-Mission Internal Name");
	estrAppend2(&estrLine, pchHeaderColEnd);
	estrAppend2(&estrLine, pchHeaderCol);
	estrAppend2(&estrLine, "Target Map(s) Display Name");
	estrAppend2(&estrLine, pchHeaderColEnd);
	estrAppend2(&estrLine, pchHeaderCol);
	estrAppend2(&estrLine, "Target Map(s) Internal Name");
	estrAppend2(&estrLine, pchHeaderColEnd);
	estrAppend2(&estrLine, pchHeaderRowEnd);
	fwrite(estrLine, estrLength(&estrLine), sizeof(char), out);
	
	// Write defs
	for(i = 0; i < n; i++)
	{
		MissionDef* pDef = eaSortedDefs[i];
		estrClear(&estrDisplayName);
		estrCopyWithHTMLEscaping(&estrDisplayName, TranslateDisplayMessage(pDef->displayNameMsg), false);
		if(!EMPTY_TO_NULL(estrDisplayName))
		{
			estrCopy2(&estrDisplayName,"(null)");
		} else if(!bHTML) {
			estrReplaceOccurrences(&estrDisplayName, "{", "\\{");
			estrReplaceOccurrences(&estrDisplayName, "}", "\\}");
		}

		// Ignore sub-missions
		if(GET_REF(pDef->parentDef)) {
			continue;
		}

		// Root mission
		estrClear(&estrLine);
		estrAppend2(&estrLine, pchRow);
		estrAppend2(&estrLine, pchCol);
		estrAppend(&estrLine, &estrDisplayName);
		estrAppend2(&estrLine, pchColEnd);
		estrAppend2(&estrLine, pchCol);
		estrAppend2(&estrLine, pDef->name);
		estrAppend2(&estrLine, pchColEnd);
		estrAppend2(&estrLine, pchCol);
		estrAppend2(&estrLine, pchColEnd);
		// Objective maps
		if(pDef->eaObjectiveMaps) {
			for(j = 0; j < eaSize(&pDef->eaObjectiveMaps); j++) 
			{
				ZoneMapInfo *zminfo = worldGetZoneMapByPublicName(pDef->eaObjectiveMaps[j]->pchMapName);
				estrAppend2(&estrLine, pchCol);
				if(zminfo) {
					DisplayMessage* pMsg = zmapInfoGetDisplayNameMessage(zminfo);
					if(pMsg) {
						estrAppend2(&estrLine, TranslateDisplayMessage(*pMsg));
					}
				}
				estrAppend2(&estrLine, pchColEnd);
				estrAppend2(&estrLine, pchCol);
				estrAppend2(&estrLine, pDef->eaObjectiveMaps[j]->pchMapName);
				estrAppend2(&estrLine, pchColEnd);
			}
		}
		estrAppend2(&estrLine, pchRowEnd);
		fwrite(estrLine, estrLength(&estrLine), sizeof(char), out);

		// Sub-missions
		for(s=0; s < eaSize(&pDef->subMissions); s++)
		{
			MissionDef* pSubDef = pDef->subMissions[s];
			estrClear(&estrLine);
			estrClear(&estrDisplayName);
			estrCopyWithHTMLEscaping(&estrDisplayName, TranslateDisplayMessage(pSubDef->uiStringMsg), false);
			if(!EMPTY_TO_NULL(estrDisplayName))
			{
				estrCopy2(&estrDisplayName,"(null)");
			} else if(!bHTML) {
				estrReplaceOccurrences(&estrDisplayName, "{", "\\{");
				estrReplaceOccurrences(&estrDisplayName, "}", "\\}");
			}

			estrAppend2(&estrLine, pchRow);
			estrAppend2(&estrLine, pchCol);
			estrAppend2(&estrLine, pchColEnd);
			estrAppend2(&estrLine, pchCol);
			estrAppend(&estrLine, &estrDisplayName);
			estrAppend2(&estrLine, pchColEnd);
			estrAppend2(&estrLine, pchCol);
			estrAppend2(&estrLine, pSubDef->name);
			estrAppend2(&estrLine, pchColEnd);
			if(pSubDef->eaObjectiveMaps) {
				for(j = 0; j < eaSize(&pSubDef->eaObjectiveMaps); j++) 
				{
					ZoneMapInfo *zminfo = worldGetZoneMapByPublicName(pSubDef->eaObjectiveMaps[j]->pchMapName);
					estrAppend2(&estrLine, pchCol);
					if(zminfo) {
						DisplayMessage* pMsg = zmapInfoGetDisplayNameMessage(zminfo);
						if(pMsg) {
							estrAppend2(&estrLine, TranslateDisplayMessage(*pMsg));
						}
					}
					estrAppend2(&estrLine, pchColEnd);
					estrAppend2(&estrLine, pchCol);
					estrAppend2(&estrLine, pSubDef->eaObjectiveMaps[j]->pchMapName);
					estrAppend2(&estrLine, pchColEnd);
				}
			}
			estrAppend2(&estrLine, pchRowEnd);
			fwrite(estrLine, estrLength(&estrLine), sizeof(char), out);
		}
	}

	estrDestroy(&estrLine);
	estrDestroy(&estrDisplayName);
	fwrite(pchFileFooter, strlen(pchFileFooter), sizeof(char), out);
	fclose(out);
}


// ---------------------------------------------------------------------
// Character class update after perk completion
// ---------------------------------------------------------------------

// This command exists for automated testing purposes
// It takes a mission, finds the first interaction property added by the mission that is of type "Door"
// and then acts like the player is directly interacting with that property.
AUTO_COMMAND ACMD_NAME("UseDoorFromMission");
void mission_CmdUseFirstDoorFromMission(Entity *pPlayerEnt, const char *pcMissionName)
{
	const char *pcMapName = zmapInfoGetPublicName(NULL);
	MissionDef *pMission;
	bool bFoundOne = false;
	int i;
	
	pMission = missiondef_DefFromRefString(pcMissionName);

	if (!pMission) {
		//if no mission found from mission name, it must be a substring... search through the player's missions to match it
		if (pcMissionName && !StringIsAllWhiteSpace(pcMissionName)) {
			MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
			U32 iNewestStartTime = 0;
			Mission *pNewestMission = NULL;
			if (!pInfo) {
				if (g_isContinuousBuilder) {
					assertmsgf(0, "Can't find mission name for UseFirstDoor... no MissionInfo");
				}
				return;
			}

			for (i=0; i < eaSize(&pInfo->missions); i++) {
				Mission *pCurMission = pInfo->missions[i];

				printf("found mission %s\n", mission_GetDef(pCurMission)->name);

				if (strstri(mission_GetDef(pCurMission)->name, pcMissionName)) {
					pcMissionName = mission_GetDef(pCurMission)->name;
					pMission = missiondef_DefFromRefString(pcMissionName);

					if (g_isContinuousBuilder) {
						assertmsgf(pMission, "Found mission named %s but then couldn't find it with DefFromRefString", pcMissionName);
					}

					break;
				}
			}

			if (g_isContinuousBuilder) {
				assertmsgf(pMission, "Can't find a mission containing substring %s", pcMissionName);
			}
		}
	}
			
	if (pMission) {
		for(i=0; i<eaSize(&pMission->ppInteractableOverrides); ++i) {
			InteractableOverride *pOverride = pMission->ppInteractableOverrides[i];
			if (pOverride->pcMapName && 
				pOverride->pPropertyEntry &&
				stricmp(pOverride->pcMapName, pcMapName) == 0 &&
				(stricmp(pOverride->pPropertyEntry->pcInteractionClass, "Door") == 0 || stricmp(pOverride->pPropertyEntry->pcInteractionClass, "Clickable") == 0)
				) {
				GameInteractable *pInteractable = interactable_GetByName(pOverride->pcInteractableName, NULL);
				if (pInteractable) {
					int j;

					//find the index of my override
					for (j=0; j < eaSize(&pInteractable->eaOverrides); j++) {
						if (stricmp(pcMissionName, pInteractable->eaOverrides[j]->pcName) == 0) {
							int iIndex = eaSize(&pInteractable->pWorldInteractable->entry->full_interaction_properties->eaEntries) + j;
							
							interaction_SetInteractTarget(pPlayerEnt, interactable_GetWorldInteractionNode(pInteractable), NULL, NULL, iIndex, 0, 0);
							interaction_ProcessInteraction(pPlayerEnt, pInteractable, NULL, NULL, pOverride->pPropertyEntry);

//							interaction_ContinueInteraction(pInteractable, NULL, NULL, iIndex, 0, 0, pPlayerEnt);
							
//							interaction_ProcessInteraction(pPlayerEnt, pInteractable, NULL, NULL, pOverride->pPropertyEntry);
//							interaction_StartInteracting(pPlayerEnt, GET_REF(pInteractable->pWorldInteractable->entry->hInteractionNode), NULL, iIndex, 0, 0);
							bFoundOne = true;
							break;
						}
					}

					if (g_isContinuousBuilder && !bFoundOne) {
						assertmsgf(0, "mission_UseFirstDoorFromMission failed... found door %s, couldn't find override", pcMissionName);
					}
				} else {
					//LIKELY THAT THIS DOES NOT WORK, since the previous case had to be changed a bunch
					GameNamedVolume *pVolume = volume_GetByName(pOverride->pcInteractableName, NULL);
					if (pVolume) {
						interaction_ProcessInteraction(pPlayerEnt, NULL, NULL, pVolume, pOverride->pPropertyEntry);
						bFoundOne = true;
					}
				}
				break;
			}
		}
	}

	if (g_isContinuousBuilder && !bFoundOne) {
		assertmsgf(0, "mission_UseFirstDoorFromMission failed... couldn't find a door for mission %s", pcMissionName);
	}

}


AUTO_COMMAND ACMD_NAME(RequestCritterDataForLoreJournal) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE; 
void mission_CmdRequestCritterDataForLoreJournal(Entity *pPlayerEnt, RequestedCritterAttribs *pAttribs)
{
	//stashtable prevents sending duplicate critter info
	static CritterLoreList critterData;
	static StashTable previousCritters = NULL;

	int i = 0;
	int num = 0;
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pPlayerEnt);
	BagIterator *pBagIter = invbag_LiteIteratorFromEnt(pPlayerEnt, InvBagIDs_Lore, pExtract);

	eaDestroyStruct(&critterData.eaCritterData, parse_StoredCritterLoreEntry);
	critterData.eaCritterData = NULL;

	if (!previousCritters) {
		previousCritters = stashTableCreateWithStringKeys(8, StashDefault);
	}

	stashTableClear(previousCritters);

	//iterate through the player's lore items, find all critters that we qualify for receiving data about, assemble the data and send it.
	for (; !bagiterator_Stopped(pBagIter); bagiterator_Next(pBagIter)) {
		ItemDef *pItemDef = bagiterator_GetDef(pBagIter);
		if (pItemDef && pItemDef->pJournalData && pItemDef->pJournalData->pchCritterName) {
			CritterDef* pCritter = RefSystem_ReferentFromString(g_hCritterDefDict, pItemDef->pJournalData->pchCritterName);

			if (pCritter && !stashGetKey(previousCritters, pCritter->pchName, NULL)) {
				CharacterClass* pClass = pCritter ? RefSystem_ReferentFromString(g_hCharacterClassDict, pCritter->pchClass) : NULL;

				eaPush(&critterData.eaCritterData, StructCreate(parse_StoredCritterLoreEntry));
				stashAddPointer(previousCritters, pCritter->pchName, NULL, false);
				REF_HANDLE_COPY(critterData.eaCritterData[num]->hCostume, pCritter->ppCostume[0]->hCostumeRef);
				critterData.eaCritterData[num]->pchName = pCritter->pchName;
				estrPrintf(&critterData.eaCritterData[num]->estrDisplayName, "%s", TranslateDisplayMessage(pCritter->displayNameMsg));

				if (pClass) {
					eaiCopy(&critterData.eaCritterData[num]->eaiAttribs, &pAttribs->eaiAttribs);
					for (i = 0; i < eaiSize(&critterData.eaCritterData[num]->eaiAttribs); i++) {
						int iLevel = entity_GetCombatLevel(pPlayerEnt);
						eafPush(&critterData.eaCritterData[num]->eafValues, class_GetAttribBasic(pClass, critterData.eaCritterData[num]->eaiAttribs[i], iLevel));
					}
				}
				num++;
			}
		}
	}
	bagiterator_Destroy(pBagIter);

	if (eaSize(&critterData.eaCritterData) > 0) {
		ClientCmd_LoreJournal_ReceiveCritterData(pPlayerEnt, &critterData);
	}
}

__forceinline static const char *RemoteContact_FindMissionKey(RemoteContact *pRemote, const char *pcMissionName)
{
	S32 i;
	if (pRemote)
	{
		for (i = eaSize(&pRemote->eaOptions)-1; i >= 0; i--)
		{
			if (pRemote->eaOptions[i]->pcMissionName == pcMissionName)
			{
				return pRemote->eaOptions[i]->pchKey;
			}
		}
	}
	return NULL;
}

static MissionOfferOverride *mission_FindBestMissionOfferOverride(Entity *pPlayerEnt, bool bGrant, bool bAlreadyCompleted, MissionOfferOverride **eaOverrides, bool *pbRemote)
{
	MissionOfferOverride *pBestOverride = NULL;
	bool bBestRemote = false;
	int i, j;
	for (i = 0; i < eaSize(&eaOverrides); i++)
	{
		MissionOfferOverride* pOverride = eaOverrides[i];
		ContactMissionOffer* pMissionOffer = pOverride->pMissionOffer;
		bool bRemote = false;

		// Validate the player is the correct allegiance
		if (eaSize(&pOverride->pMissionOffer->eaRequiredAllegiances))
		{
			for (j = eaSize(&pOverride->pMissionOffer->eaRequiredAllegiances) - 1; j >= 0; --j)
			{
				if (GET_REF(pPlayerEnt->hAllegiance) == GET_REF(pOverride->pMissionOffer->eaRequiredAllegiances[j]->hDef) ||
					GET_REF(pPlayerEnt->hSubAllegiance) == GET_REF(pOverride->pMissionOffer->eaRequiredAllegiances[j]->hDef))
				{
					break;
				}
			}
			if (j < 0)
			{
				continue;
			}
		}

		// Determine if it satisfies the criteria
		if (bGrant && (
				(!bAlreadyCompleted && (pMissionOffer->allowGrantOrReturn == ContactMissionAllow_GrantAndReturn || pMissionOffer->allowGrantOrReturn == ContactMissionAllow_GrantOnly)) ||
				( bAlreadyCompleted && (pMissionOffer->allowGrantOrReturn == ContactMissionAllow_ReplayGrant || pMissionOffer->allowGrantOrReturn == ContactMissionAllow_FlashbackGrant))
			))
		{
			bRemote = !!(pMissionOffer->eRemoteFlags & ContactMissionRemoteFlag_Grant);
		}
		else if (!bGrant && (pMissionOffer->allowGrantOrReturn == ContactMissionAllow_GrantAndReturn || pMissionOffer->allowGrantOrReturn == ContactMissionAllow_ReturnOnly))
		{
			bRemote = !!(pMissionOffer->eRemoteFlags & ContactMissionRemoteFlag_Return);
		}
		else
		{
			continue;
		}

		if (!pBestOverride || (!bBestRemote && bRemote))
		{
			pBestOverride = pOverride;
			bBestRemote = bRemote;
			if (bRemote)
				break;
		}
	}
	if (pbRemote)
		*pbRemote = pBestOverride && bBestRemote;
	return pBestOverride;
}

AUTO_COMMAND ACMD_NAME(RequestMissionDisplayData) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE; 
void mission_CmdRequestMissionDisplayData(Entity *pPlayerEnt, CachedMissionRequest *pRequest)
{
	if (pPlayerEnt && pRequest && eaSize(&pRequest->ppchMissionDefs))
	{
		CachedMissionList* pList = StructCreate(parse_CachedMissionList);
		int iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
		MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
		int i, j;
		for (i = 0; i < eaSize(&pRequest->ppchMissionDefs); i++)
		{
			GameProgressionMission* pProgMission = NULL;
			GameProgressionNodeDef* pNodeDef = NULL;
			MissionDef* pDef = missiondef_FindMissionByName(NULL, pRequest->ppchMissionDefs[i]);
			RemoteContact** eaRemoteContacts = SAFE_MEMBER3(pPlayerEnt, pPlayer, pInteractInfo, eaRemoteContacts);
			if (pDef)
			{
				CachedMissionData* pData = StructCreate(parse_CachedMissionData);
				Mission* pMission = mission_FindMissionFromDef(pInfo, pDef);
				CompletedMission* pCompletedMission = mission_GetCompletedMissionByDef(pInfo, pDef);
				MissionOfferOverride *pOverride = NULL;
				MissionOfferStatus eMissionOfferStatus = MissionOfferStatus_OK;
				pData->pchMissionDef = allocAddString(pDef->name);
				COPY_HANDLE(pData->hDisplayName, pDef->displayNameMsg.hMessage);
				pData->iMinLevel = pDef->iMinLevel;
				pData->eState = pMission ? pMission->state : -1;
				pData->uSecondaryLockoutTime = 0;

				if (eaSize(&pDef->ppchProgressionNodes))
				{
					pNodeDef = progression_GetNodeFromMissionDef(pPlayerEnt, pDef, NULL);
				}
				pData->pchProgressionNode = SAFE_MEMBER(pNodeDef, pchName);

				if (pNodeDef && pNodeDef->pMissionGroupInfo)
				{
					for (j = eaSize(&pNodeDef->pMissionGroupInfo->eaMissions)-1; j >= 0; j--)
					{
						if (pDef->name == pNodeDef->pMissionGroupInfo->eaMissions[j]->pchMissionName)
						{
							pProgMission =  pNodeDef->pMissionGroupInfo->eaMissions[j];
						}
					}
				}
				if (pProgMission && pProgMission->pExprVisible)
				{
					pData->bVisible = gslProgression_EvalExpression(iPartitionIdx, pProgMission->pExprVisible, pPlayerEnt);
				}
				else
				{
					pData->bVisible = true;
				}
				if (pData->bVisible)
				{
					pData->bAvailable = missiondef_CanBeOfferedAtAll(pPlayerEnt, pDef, NULL, &eMissionOfferStatus, &pData->eCreditType);
				}
				else
				{
					pData->bAvailable = false;
					pData->eCreditType = MissionCreditType_Ineligible;
				}

				if (pInfo && !pData->bAvailable && eMissionOfferStatus == MissionOfferStatus_SecondaryCooldown)
				{
					CompletedMission *pCompletedSecondary = eaIndexedGetUsingString(&pInfo->eaRecentSecondaryMissions, pDef->name);
					if (pCompletedSecondary) {
						U32 uCurrentTime = timeSecondsSince2000();
						U32 uLockoutEndTime = pCompletedSecondary->completedTime + missiondef_GetSecondaryCreditLockoutTime(pDef);
						if (uLockoutEndTime > uCurrentTime) {
							pData->uSecondaryLockoutTime = uLockoutEndTime - uCurrentTime;
						}
					}
				}

				if (pMission && (pMission->state == MissionState_InProgress || pMission->state == MissionState_Failed || pMission->state == MissionState_Succeeded))
					pOverride = mission_FindBestMissionOfferOverride(pPlayerEnt, false, false, pDef->ppMissionOfferOverrides, &pData->bRemoteContact);
				if (!pOverride)
					pOverride = mission_FindBestMissionOfferOverride(pPlayerEnt, true, pCompletedMission != NULL, pDef->ppMissionOfferOverrides, &pData->bRemoteContact);
				if (pOverride)
				{
					ContactDef* pContact = RefSystem_ReferentFromString(g_ContactDictionary, pOverride->pcContactName);
					RemoteContact* pRemote = eaIndexedGetUsingString(&eaRemoteContacts, pOverride->pcContactName);
					const char* pchRemoteKey = RemoteContact_FindMissionKey(pRemote, pDef->name);
					pData->pchContact = pOverride->pcContactName;
					pData->pchContactKey = StructAllocString(pchRemoteKey);
					if (pContact)
					{
						COPY_HANDLE(pData->hContactDisplayName, pContact->displayNameMsg.hMessage);
					}
					else
					{
						REMOVE_HANDLE(pData->hContactDisplayName);
					}
				}
				if (!eaSize(&pList->eaData))
					eaIndexedEnable(&pList->eaData, parse_CachedMissionData);
				eaIndexedAdd(&pList->eaData, pData);
			}
		}
		ClientCmd_ReceiveMissionDisplayData(pPlayerEnt, pList);
		StructDestroy(parse_CachedMissionList, pList);
	}
}

typedef struct MissionWarpToDoorData
{
	EntityRef erEnt;
	const char* pchMissionDefName; // pooled string
} MissionWarpToDoorData;

static void MissionWarpToDoorInternal(Entity* pEnt, MissionDef* pMissionDef)
{
	if (pMissionDef->pWarpToMissionDoor)
	{
		const char* pchMap = pMissionDef->pWarpToMissionDoor->pchMapName;
		const char* pchSpawn = pMissionDef->pWarpToMissionDoor->pchSpawn;
		DoorTransitionSequenceDef* pTransSequence = GET_REF(pMissionDef->pWarpToMissionDoor->hTransSequence);
		ZoneMapInfo* pNextZoneMapInfo = zmapInfoGetByPublicName(pchMap);
		RegionRules* pCurrRules = getRegionRulesFromEnt(pEnt);
		RegionRules* pNextRules = pNextZoneMapInfo ? getRegionRulesFromZoneMap(pNextZoneMapInfo) : NULL;

		gslEntityPlayTransitionSequenceThenMapMoveEx(pEnt, pchMap, ZMTYPE_UNSPECIFIED, pchSpawn, 0, 0, 0, 0, 0, NULL, pCurrRules, pNextRules, pTransSequence, 0);
	}
}

static void MissionWarpToDoor_CB(TransactionReturnVal* pReturn, MissionWarpToDoorData* pData)
{
	Entity *pEnt = entFromEntityRefAnyPartition(pData->erEnt);
	MissionDef* pMissionDef = missiondef_FindMissionByName(NULL, pData->pchMissionDefName);

	if (pEnt && pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		MissionWarpToDoorInternal(pEnt, pMissionDef);
	}

	SAFE_FREE(pData);
}

AUTO_COMMAND ACMD_NAME(mission_WarpToMissionDoor) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Mission) ACMD_PRIVATE;
void mission_CmdWarpToMissionDoor(Entity* pEnt, const char* pcMissionDefName)
{
	MissionInfo* pInfo = mission_GetInfoFromPlayer(pEnt);
	if (pInfo)
	{
		Mission* pMission = mission_GetMissionByName(pInfo, pcMissionDefName);
		MissionDef* pMissionDef = mission_GetDef(pMission);
		MissionWarpCostDef* pCostDef = missiondef_GetWarpCostDef(pMissionDef);
		MissionMapWarpData* pWarpData = SAFE_MEMBER(pMissionDef, pWarpToMissionDoor);
		const char* pchMapName = zmapInfoGetPublicName(NULL);
		ZoneMapType eMapType = zmapInfoGetMapType(NULL);
		
		if ((eMapType == ZMTYPE_SHARED || eMapType == ZMTYPE_STATIC) &&
			pWarpData && 
			pWarpData->pchMapName != pchMapName && 
			mission_CanPlayerUseMissionWarp(pEnt, pMission))
		{
			if (pCostDef && pCostDef->iNumericCost > 0)
			{
				S32 iNumericValue = inv_GetNumericItemValue(pEnt, pCostDef->pchNumeric);
				if (iNumericValue >= pCostDef->iNumericCost)
				{
					MissionWarpToDoorData* pCBData = calloc(1, sizeof(MissionWarpToDoorData));
					ItemChangeReason reason = {0};
					TransactionReturnVal* pReturn;

					pCBData->erEnt = entGetRef(pEnt);
					pCBData->pchMissionDefName = pMissionDef->pchRefString;
			
					pReturn = LoggedTransactions_CreateManagedReturnValEnt("WarpToMissionDoor", pEnt, MissionWarpToDoor_CB, pCBData);
					inv_FillItemChangeReason(&reason, pEnt, "Mission:WarpToDoor", pcMissionDefName);

					AutoTrans_inv_tr_ApplyNumeric(pReturn, GetAppGlobalType(), 
							entGetType(pEnt), entGetContainerID(pEnt), 
							pCostDef->pchNumeric, -pCostDef->iNumericCost, NumericOp_Add, &reason);
				}
			}
			else
			{
				MissionWarpToDoorInternal(pEnt, pMissionDef);
			}
		}
	}
}

AUTO_COMMAND ACMD_NAME(RequestMissionRewardData) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void mission_CmdRequestCurrentMissionRewards(Entity* pPlayerEnt, CachedMissionRequest *pRequest)
{
	if (pPlayerEnt && pRequest && eaSize(&pRequest->ppchMissionDefs))
	{
		CachedMissionList* pList = StructCreate(parse_CachedMissionList);
		int iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
		MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
		int i, j, k;
		RewardGatedDataInOut *pGatedData = NULL;

		for (i = 0; i < eaSize(&pRequest->ppchMissionDefs); i++)
		{
			MissionDef* pMissionDef = missiondef_FindMissionByName(NULL, pRequest->ppchMissionDefs[i]);
			RewardContextData rewardContextData = {0};
			rewardContextData.pEnt = pPlayerEnt;

			if (pMissionDef)
			{
				Mission* pMission = mission_GetMissionFromDef(pInfo, pMissionDef);
				RecruitType eRecruitType = GetRecruitTypes(pPlayerEnt);
				MissionCreditType eCredit;
				CachedMissionReward* pData;
				U32 seed;

				if (pMission)
				{
					// Use type from current mission
					eCredit = pMission->eCreditType;
				}
				else if (!missiondef_CanBeOfferedAtAll(pPlayerEnt, pMissionDef, NULL, NULL, &eCredit))
				{
					// Can't offer this mission, so don't generate rewards at all
					continue;
				}

				pData = StructCreate(parse_CachedMissionReward);
				pData->pchMissionDef = pMissionDef->name;

				// create gated reward data to allow us to see what the rewards will be. Note that if the dame gated type is used before the mission completes then the rewards given will not match
				pGatedData = mission_trh_CreateRewardGatedData(CONTAINER_NOCONST(Entity, pPlayerEnt));

				seed = mission_GetRewardSeed(pPlayerEnt, pMission, pMissionDef);
				reward_GenerateMissionActionRewards(iPartitionIdx, pPlayerEnt, pMissionDef, MissionState_TurnedIn, &pData->eaRewardBags, &seed,
					eCredit, 0, /*time_level=*/0, 0, 0, eRecruitType, /*bUGCProject=*/false, false, false, false, false, /*bGenerateChestRewards=*/true, &rewardContextData, 0, pGatedData);

				// destroy gated reward info
				if(pGatedData)
				{
					StructDestroy(parse_RewardGatedDataInOut, pGatedData);
				}

				// Remove silent items
				for (j = eaSize(&pData->eaRewardBags) - 1; j >= 0; j--)
				{
					NOCONST(InventoryBag) *pBag = CONTAINER_NOCONST(InventoryBag, pData->eaRewardBags[j]);
					if (pBag->pRewardBagInfo->PickupType == kRewardPickupType_Choose)
						continue;

					for (k = eaSize(&pBag->ppIndexedInventorySlots) - 1; k >= 0; k--)
					{
						ItemDef *pItemDef = pBag->ppIndexedInventorySlots[k]->pItem ? GET_REF(pBag->ppIndexedInventorySlots[k]->pItem->hItem) : NULL;
						if (!pItemDef || (pItemDef->flags & kItemDefFlag_Silent))
						{
							NOCONST(InventorySlot) *pSlot = eaRemove(&pBag->ppIndexedInventorySlots, k);
							StructDestroyNoConst(parse_InventorySlot, pSlot);
						}
					}

					if (eaSize(&pBag->ppIndexedInventorySlots) <= 0)
					{
						eaRemove(&pData->eaRewardBags, j);
						StructDestroyNoConst(parse_InventoryBag, pBag);
					}
				}

				eaPush(&pList->eaRewardData, pData);
			}
		}

		ClientCmd_ReceiveMissionDisplayData(pPlayerEnt, pList);
		StructDestroy(parse_CachedMissionList, pList);
	}
}

void mission_AllegianceBypassWarpRestrictionsComplete(TransactionReturnVal *returnVal, UserData data)
{
	ContainerID entContainerID = (ContainerID)data;
	Entity *pPlayerEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, entContainerID);
	if (pPlayerEnt && returnVal && returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		AllegianceDef *pAllegiance = SAFE_GET_REF(pPlayerEnt, hAllegiance);
		if (pAllegiance && pAllegiance->pWarpRestrict && pAllegiance->pWarpRestrict->pchRequiredMission)
		{
			MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
			if (pInfo)
			{
				Mission *pMission = mission_FindMissionFromRefString(pInfo, pAllegiance->pWarpRestrict->pchRequiredMission);
				if (pMission)
				{
					mission_CompleteMission(pPlayerEnt, pMission, true);
				}
			}
		}
	}
}

AUTO_COMMAND ACMD_NAME(AllegianceCompleteMissionWarpRestriction) ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Debug);
void mission_CmdAllegianceBypassWarpRestrictions(Entity *pPlayerEnt)
{
	AllegianceDef *pAllegiance = SAFE_GET_REF(pPlayerEnt, hSubAllegiance);
	if (!pAllegiance)
		pAllegiance = SAFE_GET_REF(pPlayerEnt, hAllegiance);
	if (pAllegiance && pAllegiance->pWarpRestrict && pAllegiance->pWarpRestrict->pchRequiredMission)
	{
		MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
		if (pInfo)
		{
			Mission *pMission = mission_FindMissionFromRefString(pInfo, pAllegiance->pWarpRestrict->pchRequiredMission);
			if (pMission)
			{
				mission_CompleteMission(pPlayerEnt, pMission, true);
			}
			else
			{
				missioninfo_AddMissionByName(entGetPartitionIdx(pPlayerEnt), pInfo, pAllegiance->pWarpRestrict->pchRequiredMission, mission_AllegianceBypassWarpRestrictionsComplete, (UserData)entGetContainerID(pPlayerEnt));
			}
		}
	}
}

AUTO_COMMAND_REMOTE;
void mission_IncrementDropCountStat(ContainerID iUGCProjectID)
{
	RemoteCommand_Intershard_aslUGCDataManager_IncrementDropCountStat(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0, iUGCProjectID);
}

AUTO_COMMAND_REMOTE;
void mission_RecordCompletion(ContainerID iUGCProjectID, const char *pcVersionNameSpace, U32 uDurationInMinutes, U32 bRecordCompletion)
{
	RemoteCommand_Intershard_aslUGCDataManager_RecordCompletion(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0, iUGCProjectID, pcVersionNameSpace, uDurationInMinutes, bRecordCompletion);
}

static void aslMapManager_RequestUGCDataForMission_CB(TransactionReturnVal *returnVal, void* rawEntContainerID)
{
	UGCPlayableNameSpaceData *pUGCPlayableNameSpaceData = NULL;

	if(RemoteCommandCheck_aslMapManager_RequestUGCDataForMission(returnVal, &pUGCPlayableNameSpaceData) && pUGCPlayableNameSpaceData)
	{
		ContainerID entContainerID = (ContainerID)(intptr_t)rawEntContainerID;

		Entity *pEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, entContainerID);

		if(pEntity)
		{
			AutoTrans_mission_tr_CreateUGCMissionData(NULL, GetAppGlobalType(),
				GLOBALTYPE_ENTITYPLAYER, entContainerID,
				pUGCPlayableNameSpaceData->strNameSpace,
				!!(pUGCPlayableNameSpaceData->uAccountID == entGetAccountID(pEntity)),
				pUGCPlayableNameSpaceData->bStatsQualifyForUGCRewards,
				pUGCPlayableNameSpaceData->iNumberOfPlays,
				pUGCPlayableNameSpaceData->fAverageDurationInMinutes,
				pUGCPlayableNameSpaceData->bProjectIsFeatured,
				pUGCPlayableNameSpaceData->bProjectWasFeatured);
		}

		StructDestroy(parse_UGCPlayableNameSpaceData, pUGCPlayableNameSpaceData);
	}
}

AUTO_COMMAND_REMOTE;
void mission_RequestUGCDataForMission(ContainerID entContainerID, const char *strNameSpace)
{
	RemoteCommand_aslMapManager_RequestUGCDataForMission(objCreateManagedReturnVal(aslMapManager_RequestUGCDataForMission_CB, (void*)(intptr_t)entContainerID),
		GLOBALTYPE_MAPMANAGER, 0,
		strNameSpace);
}
