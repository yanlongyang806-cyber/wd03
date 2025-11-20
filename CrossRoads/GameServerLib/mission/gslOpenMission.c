/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Entity.h"
#include "EntityIterator.h"
#include "EntityLib.h"
#include "GameEvent.h"
#include "gslEventSend.h"
#include "gslGameAction.h"
#include "gslMapState.h"
#include "gslMission.h"
#include "gslMissionEvents.h"
#include "gslOpenMission.h"
#include "gslPartition.h"
#include "gslVolume.h"
#include "gslWaypoint.h"
#include "mapstate_common.h"
#include "mission_common.h"
#include "WorldGrid.h"
#include "gslTeamUp.h"
#include "CostumeCommonEntity.h"
#include "NotifyEnum.h"
#include "gslActivity.h"

#include "mission_common_h_ast.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

#define NUM_LEADERBOARD_ENTRIES 5


// ----------------------------------------------------------------------------
//  Static Data
// ----------------------------------------------------------------------------

// Open Mission defines
#define OPEN_MISSION_EXIT_DELAY		5
#define OPEN_MISSION_SYSTEM_TICK	30

static OpenMissionPartitionState **s_eaOpenMissionPartitionStates;
static U32 s_OpenMissionTick = 0;


// ----------------------------------------------------------------------------
//  Open Mission Structure Management
// ----------------------------------------------------------------------------

static OpenMission* openmission_CreateOpenMission(int iPartitionIdx, char const *pcMissionName)
{
	OpenMission *pOpenMission = StructCreate(parse_OpenMission);

	pOpenMission->iPartitionIdx = iPartitionIdx;
	pOpenMission->pchName = pcMissionName;

	return pOpenMission;
}


static void openmission_DestroyOpenMission(OpenMission *pOpenMission)
{
	if (pOpenMission->pMission) {
		mission_PreMissionDestroyDeinitRecursive(pOpenMission->iPartitionIdx, pOpenMission->pMission);
	}

	StructDestroy(parse_OpenMission, pOpenMission);
}


// ----------------------------------------------------------------------------
//  Partition State Management
// ----------------------------------------------------------------------------

static OpenMissionPartitionState *openmission_CreatePartitionState(int iPartitionIdx)
{
	OpenMissionPartitionState *pState = calloc(1,sizeof(OpenMissionPartitionState));

	pState->iPartitionIdx = iPartitionIdx;

	return pState;
}


static void openmission_DestroyPartitionState(OpenMissionPartitionState *pState)
{
	mapState_UpdateOpenMissions(pState->iPartitionIdx, NULL);
	eaDestroyEx(&pState->eaOpenMissions, openmission_DestroyOpenMission);
	stashTableDestroySafe(&pState->pOpenMissionTable);
	free(pState);

}

static OpenMissionPartitionState *openmission_GetPartitionState(int iPartitionIdx)
{
	return eaGet(&s_eaOpenMissionPartitionStates, iPartitionIdx);
}


// ----------------------------------------------------------------------------
//  Scoreboard Functions
// ----------------------------------------------------------------------------


void openmission_GetScoreboardEnts(int iPartitionIdx, Mission *pMission, Entity ***peaPlayerEnts)
{
	OpenMission *pOpenMission = openmission_GetFromName(iPartitionIdx, pMission->missionNameOrig);
	if (pOpenMission) {
		int i, n = eaSize(&pOpenMission->eaScores);
		for (i = 0; i < n; i++){
			Entity *pEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pOpenMission->eaScores[i]->playerID);
			if (pEnt) {
				eaPush(peaPlayerEnts, pEnt);
			}
		}
	}
}


static void openmission_AddScoreboardPoints(OpenMission *pOpenMission, Entity *pPlayerEnt, F32 fValue)
{
	OpenMissionScoreEntry *pScoreEntry;
	ContainerID iContainerID;

	if (!pPlayerEnt || !fValue || (entGetType(pPlayerEnt) != GLOBALTYPE_ENTITYPLAYER)) {
		return;
	}

	iContainerID = entGetContainerID(pPlayerEnt);
	pScoreEntry = eaIndexedGetUsingInt(&pOpenMission->eaScores, iContainerID);
	if (!pScoreEntry){
		pScoreEntry = StructCreate(parse_OpenMissionScoreEntry);

		pScoreEntry->playerID = iContainerID;
		estrAppend2(&pScoreEntry->pchPlayerName, entGetLocalName(pPlayerEnt));
		estrAppend2(&pScoreEntry->pchAccountName, entGetAccountOrLocalName(pPlayerEnt));

		eaIndexedAdd(&pOpenMission->eaScores, pScoreEntry);
	}
	pScoreEntry->fPoints += fValue;
}


void openmission_OpenMissionScoreEventCB(Mission *pMission, GameEvent *pSubscribeEvent, GameEvent *pSpecificEvent, int iValue)
{
	OpenMissionScoreEvent *pScoreEvent = NULL;
	MissionDef *pDef;
	F32 fPoints;
	int iPartitionIdx;
	int i;

	if (!pMission || !pMission->pOpenMission || (pMission->state != MissionState_InProgress)) {
		return;
	}

	pDef = mission_GetDef(pMission);
	if (!pDef) {
		return;
	}

	// Find OpenMissionScoreEvent struct for this Event
	// (this earray should be small)
	for (i=eaSize(&pMission->eaTrackedScoreboardEvents)-1; i>=0; --i) {
		if (pMission->eaTrackedScoreboardEvents[i]->pEvent == pSubscribeEvent){
			pScoreEvent = pMission->eaTrackedScoreboardEvents[i];
			break;
		}
	}

	assert(pScoreEvent); // Or else we got an event we never subscribed to

	// Calculate points to be awarded
	fPoints = iValue * pScoreEvent->fScale;

	iPartitionIdx = pMission->pOpenMission->iPartitionIdx;

	// Award points to all players involved based on tMatchSource or tMatchTarget fields
	if (pSubscribeEvent->tMatchSource == TriState_Yes || pSubscribeEvent->tMatchSourceTeam == TriState_Yes) {
		for(i=eaSize(&pSpecificEvent->eaSources)-1; i>=0; --i) {
			if (pSpecificEvent->eaSources[i]->bIsPlayer) {
				Entity *pEnt = entFromEntityRef(iPartitionIdx, pSpecificEvent->eaSources[i]->entRef);

				// Award points if player meets requirements
				if ((!pDef->missionReqs || mission_EvalRequirements(iPartitionIdx, pDef, pEnt)) &&
					(!pDef->pMapRequirements || mission_EvalMapRequirements(iPartitionIdx, pDef))) {
					// Kills are the only things that use variable percentages right now
					if (pSubscribeEvent->type == EventType_Kills) {
						openmission_AddScoreboardPoints(pMission->pOpenMission, pEnt, fPoints*pSpecificEvent->eaSources[i]->fCreditPercentage);
					} else {
						openmission_AddScoreboardPoints(pMission->pOpenMission, pEnt, fPoints/eaSize(&pSpecificEvent->eaSources));
					}

					// TODO - if tMatchSourceTeam is true, split points with team
				}
			}
		}
	}

	// Award points to all players involved based on tMatchSource or tMatchTarget fields
	if (pSubscribeEvent->tMatchTarget == TriState_Yes || pSubscribeEvent->tMatchTargetTeam == TriState_Yes) {
		for(i=eaSize(&pSpecificEvent->eaTargets)-1; i>=0; --i) {
			if (pSpecificEvent->eaTargets[i]->bIsPlayer) {
				Entity *pEnt = entFromEntityRef(iPartitionIdx, pSpecificEvent->eaTargets[i]->entRef);

				// Award points if player meets requirements
				if ((!pDef->missionReqs || mission_EvalRequirements(iPartitionIdx, pDef, pEnt)) &&
					(!pDef->pMapRequirements || mission_EvalMapRequirements(iPartitionIdx, pDef))) {
					// Kills are the only things that use variable percentages right now
					if (pSubscribeEvent->type == EventType_Kills) {
						openmission_AddScoreboardPoints(pMission->pOpenMission, pEnt, fPoints*pSpecificEvent->eaTargets[i]->fCreditPercentage);
					} else {
						openmission_AddScoreboardPoints(pMission->pOpenMission, pEnt, fPoints/eaSize(&pSpecificEvent->eaTargets));
					}

					// TODO - if tMatchTargetTeam is true, split points with team
				}
			}
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_SERVERCMD;
void OpenMissionRequestScoreEntryCostume(Entity *pPlayer, ContainerID scorePlayerID)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, scorePlayerID);
	if(pEnt)
	{
		ClientCmd_OpenMissionSetScoreEntryCostume(pPlayer, scorePlayerID, costumeEntity_GetEffectiveCostume(pEnt));
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_SERVERCMD;
void OpenMissionRequestIsEventActive(Entity *pPlayer, char *pchEventName)
{
	EventDef *pDef = EventDef_Find(pchEventName);
	ClientCmd_OpenMissionLeaderboardSetEventIsActive(pPlayer, gslEvent_IsActive(pDef));
}


// ----------------------------------------------------------------------------
//  Event Tracking Functions
// ----------------------------------------------------------------------------

void openmission_StopTrackingAllEvents(void)
{
	int i, j;

	for(i=eaSize(&s_eaOpenMissionPartitionStates)-1; i>=0; --i) {
		OpenMissionPartitionState *pState = s_eaOpenMissionPartitionStates[i];
		if (pState) {
			for(j=eaSize(&pState->eaOpenMissions)-1; j>=0; --j) {
				missionevent_StopTrackingEventsRecursive(i, pState->eaOpenMissions[j]->pMission);
			}
		}
	}
}


void openmission_StartTrackingAll(void)
{
	int i, j;

	for(i=eaSize(&s_eaOpenMissionPartitionStates)-1; i>=0; --i) {
		OpenMissionPartitionState *pState = s_eaOpenMissionPartitionStates[i];
		if (pState) {
			for(j=eaSize(&pState->eaOpenMissions)-1; j>=0; --j) {
				MissionDef *pDef = mission_GetDef(pState->eaOpenMissions[j]->pMission);
				missionevent_StartTrackingEventsRecursive(i, pDef, pState->eaOpenMissions[j]->pMission);
			}
		}
	}
}


void openmission_StopTrackingAllForName(const char *pcMissionName)
{
	int i;

	for(i=eaSize(&s_eaOpenMissionPartitionStates)-1; i>=0; --i) {
		OpenMissionPartitionState *pState = s_eaOpenMissionPartitionStates[i];
		if (pState) {
			OpenMission *pOpenMission = openmission_GetFromName(i, pcMissionName);
			if (pOpenMission) {
				missionevent_StopTrackingEventsRecursive(i, pOpenMission->pMission);
			}
		}
	}
}


void openmission_StartTrackingForName(const char *pcMissionName)
{
	int i;

	for(i=eaSize(&s_eaOpenMissionPartitionStates)-1; i>=0; --i) {
		OpenMissionPartitionState *pState = s_eaOpenMissionPartitionStates[i];
		if (pState) {
			OpenMission *pOpenMission = openmission_GetFromName(i, pcMissionName);
			if (pOpenMission) {
				MissionDef *pDef = mission_GetDef(pOpenMission->pMission);
				missionevent_StartTrackingEventsRecursive(i, pDef, pOpenMission->pMission);
			}
		}
	}
}


// ----------------------------------------------------------------------------------
// Open Mission Access Logic
// ----------------------------------------------------------------------------------

static void openmission_DecodeOpenMissionRefString(const char *pcRefString, char **estrScratch, char **ppcChildName)
{
	estrCopy2(estrScratch, pcRefString);
	*ppcChildName = strstr(*estrScratch, "::");
	if (*ppcChildName) {
		**ppcChildName = 0;
		*ppcChildName += 2;
	}
}

OpenMission *openmission_GetFromName(int iPartitionIdx, const char *pcMissionName)
{
	OpenMissionPartitionState *pState = openmission_GetPartitionState(iPartitionIdx);

	if (pState && pcMissionName) {
		OpenMission *pOpenMission = NULL;
		char *estrParentName = NULL;
		char *pchChildName = NULL;

		estrStackCreate(&estrParentName);
		openmission_DecodeOpenMissionRefString(pcMissionName, &estrParentName, &pchChildName);
		pOpenMission = eaIndexedGetUsingString(&pState->eaOpenMissions, estrParentName);
		estrDestroy(&estrParentName);
		return pOpenMission;
	}
	return NULL;
}

// Indicates if the map has any missions with the given state and tag
bool openmission_HasMissionInStateByTag(int iPartitionIdx, S32 eTag, MissionState eState)
{
	OpenMissionPartitionState *pState;
	int i;

	pState = openmission_GetPartitionState(iPartitionIdx);
	if (pState) {
		for(i=eaSize(&pState->eaOpenMissions)-1; i>=0; --i) {
			OpenMission *pOpenMission = pState->eaOpenMissions[i];
			if (pOpenMission->pMission && 
				pOpenMission->pMission->state == eState &&
				missiondef_HasTag(mission_GetDef(pOpenMission->pMission), (MissionTag)eTag)) {
				return true;
			}
		}
	}
	return false;
}


Mission *openmission_FindMissionFromRefString(int iPartitionIdx, const char *pcRefString)
{
	OpenMissionPartitionState *pState = openmission_GetPartitionState(iPartitionIdx);

	if (pcRefString && pState && pState->pOpenMissionTable) {
		OpenMission *pOpenMission = NULL;
		Mission *pMission = NULL;
		char *estrParentName;
		char *pcChildName = NULL;

		estrStackCreate(&estrParentName);

		openmission_DecodeOpenMissionRefString(pcRefString, &estrParentName, &pcChildName);
		
		if (stashFindPointer(pState->pOpenMissionTable, estrParentName, &pOpenMission)) {
			pMission = pOpenMission->pMission;

			if (pMission && pcChildName) {
				pMission = mission_FindChildByName(pMission, pcChildName);
			}
		}

		estrDestroy(&estrParentName);
		return pMission;
	}
	return NULL;
}


void openmission_addOpenMissionParticipant(int iPartitionIdx, const char *pcMissionName, Entity *pEnt)
{
	OpenMission *pOpenMission = openmission_GetFromName(iPartitionIdx, pcMissionName);
	if (pOpenMission) {
		eaiPushUnique(&pOpenMission->eaiParticipants, entGetContainerID(pEnt));
	}
}


void openmission_ClearOpenMissionParticipants(int iPartitionIdx, const char *pcMissionName)
{
	OpenMission *pOpenMission = openmission_GetFromName(iPartitionIdx, pcMissionName);
	if (pOpenMission) {
		eaiClear(&pOpenMission->eaiParticipants);
	}
}


// Sets the entities which will receive credit from completing the open mission
// Currently finds all players which are participating in the open mission at the time of completion
void openmission_SetParticipants(OpenMission *pOpenMission)
{
	Entity *pCurrEnt;
	EntityIterator *pIter = entGetIteratorSingleType(pOpenMission->iPartitionIdx, 0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);

	openmission_ClearOpenMissionParticipants(pOpenMission->iPartitionIdx, pOpenMission->pchName);

	while ((pCurrEnt = EntityIteratorGetNext(pIter))) {
		MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(pCurrEnt);
		if (pMissionInfo && pMissionInfo->pchCurrentOpenMission && stricmp(pMissionInfo->pchCurrentOpenMission, pOpenMission->pchName) == 0) {
			openmission_addOpenMissionParticipant(pOpenMission->iPartitionIdx, pOpenMission->pchName, pCurrEnt);
		}
	}
	EntityIteratorRelease(pIter);
}


bool openmission_DidEntParticipateInOpenMission(int iPartitionIdx, const char *pcMissionName, Entity *pEnt)
{
	if (!pEnt || !pcMissionName) {
		return false;
	}

	if (strstr(pcMissionName, "::")) {
		// Submission
		Mission *pMission = openmission_FindMissionFromRefString(iPartitionIdx, pcMissionName);
		if (pMission && pMission->state == MissionState_Succeeded) {
			return true;
		}
	} else {
		OpenMission *pOpenMission = openmission_GetFromName(iPartitionIdx, pcMissionName);

		if (pOpenMission && (eaiFind(&pOpenMission->eaiParticipants, entGetContainerID(pEnt)) != -1)) {
			// Root mission
			return true;
		}
	}

	return false;
}


// ----------------------------------------------------------------------------------
// Processing Open Missions for Players
// ----------------------------------------------------------------------------------

static void openmission_ClearAllOpenMissionWaypoints(MissionInfo *pInfo)
{
	int i;

	if (!pInfo) {
		return;
	}

	for (i = eaSize(&pInfo->waypointList)-1; i>=0; --i){
		if (pInfo->waypointList[i]->type == MinimapWaypointType_OpenMission){
			MinimapWaypoint *pWaypoint = eaRemove(&pInfo->waypointList, i);
			waypoint_DestroyMinimapWaypoint(pWaypoint);
		}
	}
	mission_FlagInfoAsDirty(pInfo);
}

static int openmission_ScoreEntryCompareByScore(const OpenMissionScoreEntry **ppEntry1, const OpenMissionScoreEntry **ppEntry2)
{
	F32 fResult = (*ppEntry2)->fPoints - (*ppEntry1)->fPoints;

	if(fResult > 0)
	{
		return 1;
	}
	else if (fResult < 0)
	{
		return -1;
	}
	else
	{
		return 0;
	}
}

// Determines which Open Mission the player is participating in, if any
void openmission_ProcessOpenMissionsForPlayer(int iPartitionIdx, Entity *pPlayerEnt, MissionInfo *pInfo)
{
	OpenMissionPartitionState *pState = openmission_GetPartitionState(iPartitionIdx);
	OpenMission *pCurrentOpenMission = NULL;
	Mission *pCurrentMission = NULL;
	bool bInVolume = false;
	const char *pcOrigOpenMission;
	
	// Get current open mission
	pcOrigOpenMission = pInfo->pchCurrentOpenMission;
	if (pInfo->pchCurrentOpenMission) {
		pCurrentOpenMission = openmission_GetFromName(iPartitionIdx, pInfo->pchCurrentOpenMission);
		if (pCurrentOpenMission) {
			pCurrentMission = pCurrentOpenMission->pMission;
		}
	}

	// If the player is in an Open Mission already, check that it still exists
	if (pInfo->pchCurrentOpenMission && !pCurrentMission){			
		pInfo->pchCurrentOpenMission = NULL;
		mission_FlagInfoAsDirty(pInfo);
	}

	// Check whether the player is still in the volume for their current Mission
	if (pCurrentMission) {
		MissionDef *pDef = mission_GetDef(pCurrentMission);
		if (pDef) {
			if (eaSize(&pDef->eaOpenMissionVolumes)) {
				if (volume_IsEntityInAnyVolumeByName(pPlayerEnt, pDef->eaOpenMissionVolumes, NULL)) {
					pInfo->openMissionVolumeTimestamp = timeSecondsSince2000();
					bInVolume = true;
				} else {
					// If they haven't been in the volume for a long time, clear the Mission
					if (timeSecondsSince2000() - pInfo->openMissionVolumeTimestamp > OPEN_MISSION_EXIT_DELAY) {
						pCurrentMission = NULL;
						pInfo->pchCurrentOpenMission = NULL;
						mission_FlagInfoAsDirty(pInfo);
					}
				}
			}
			if ((pDef->missionReqs && !mission_EvalRequirements(iPartitionIdx, pDef, pPlayerEnt)) ||
				(pDef->pMapRequirements && !mission_EvalMapRequirements(iPartitionIdx, pDef))) {
				// If player not longer meets requires expression remove it
				pCurrentMission = NULL;
				pInfo->pchCurrentOpenMission = NULL;
				mission_FlagInfoAsDirty(pInfo);
			}
		}
	}

	// If the player isn't already in an Open Mission, check whether there's 
	// another Open Mission that can be displayed
	// Only give an open mission if the player meets the requires expression
	if (!pInfo->pchCurrentOpenMission || !bInVolume) {
		OpenMission *pMapMission = NULL;
		int i, n = pState ? eaSize(&pState->eaOpenMissions) : 0;
		for (i = 0; i < n; i++) {
			OpenMission *pOpenMission = pState->eaOpenMissions[i];
			Mission *pMission = pOpenMission->pMission;
			MissionDef *pDef = mission_GetDef(pMission);
			if (pMission && pDef &&
			   ((!pDef->missionReqs || mission_EvalRequirements(iPartitionIdx, pDef, pPlayerEnt)) &&
				(!pDef->pMapRequirements || mission_EvalMapRequirements(iPartitionIdx, pDef)))) {
				if (volume_IsEntityInAnyVolumeByName(pPlayerEnt, pDef->eaOpenMissionVolumes, NULL) ) {
					pInfo->pchCurrentOpenMission = pOpenMission->pchName;
					pCurrentMission = pMission;
					pInfo->openMissionVolumeTimestamp = timeSecondsSince2000();
					mission_FlagInfoAsDirty(pInfo);
					break;
				} else if (!pMapMission && !eaSize(&pDef->eaOpenMissionVolumes)) {
					pMapMission = pOpenMission;
				}
			}
		}
			
		// If no volume-scoped Mission matched, default to a map-wide Mission if available
		if (pMapMission && !pInfo->pchCurrentOpenMission) {
			pInfo->pchCurrentOpenMission = pMapMission->pchName;
			pCurrentMission = pMapMission->pMission;
			pInfo->openMissionVolumeTimestamp = timeSecondsSince2000();
			mission_FlagInfoAsDirty(pInfo);
		}
	}

	// If the Open Mission has changed, update waypoints
	if (pcOrigOpenMission != pInfo->pchCurrentOpenMission){
		openmission_ClearAllOpenMissionWaypoints(pInfo);
		if (pCurrentMission) {
			waypoint_GetMissionWaypoints(pInfo, pCurrentMission, &pInfo->waypointList, true);
		}
		mission_FlagInfoAsDirty(pInfo);
	}

	//Fill the player's leaderboard info with information about the top players
	if(pCurrentOpenMission && pCurrentOpenMission->eaScores)
	{
		OpenMissionScoreEntry **eaSortedEntries = NULL;
		S32 i, iPlayerIndex = 0;

		eaCreate(&eaSortedEntries);
		
		eaPushEArray(&eaSortedEntries, &pCurrentOpenMission->eaScores);

		eaQSort(eaSortedEntries, openmission_ScoreEntryCompareByScore);

		for(i = 0; i < eaSize(&eaSortedEntries); ++i)
		{
			OpenMissionScoreEntry *pEntry = eaSortedEntries[i];
			if(pEntry)
			{
				if(i > 0 && eaSortedEntries[i - 1] && pEntry->fPoints == eaSortedEntries[i - 1]->fPoints)
				{
					pEntry->iRank = eaSortedEntries[i - 1]->iRank;
				}
				else
				{
					pEntry->iRank = i + 1;
				}

				if(pEntry->playerID == pPlayerEnt->myContainerID)
				{
					iPlayerIndex = i;
				}
			}
		}
		
		if(!pInfo->pLeaderboardInfo)
			pInfo->pLeaderboardInfo = StructCreate(parse_OpenMissionLeaderboardInfo);

		if (!pInfo->pLeaderboardInfo->eaLeaders)
		{
			eaCreate(&pInfo->pLeaderboardInfo->eaLeaders);
		}
		eaSetSizeStruct(&pInfo->pLeaderboardInfo->eaLeaders, parse_OpenMissionScoreEntry, NUM_LEADERBOARD_ENTRIES);

		//Update the leader's score entries if needed
		for(i = 0; i < NUM_LEADERBOARD_ENTRIES; ++i)
		{
			OpenMissionScoreEntry *pLeaderEntry = (eaSize(&pInfo->pLeaderboardInfo->eaLeaders) > i ? pInfo->pLeaderboardInfo->eaLeaders[i] : NULL);
			OpenMissionScoreEntry *pScoreboardEntry = (eaSize(&eaSortedEntries) > i ? eaSortedEntries[i] : NULL);

			//If the player has too low of a rank, we force the last entry to be the player
			if(iPlayerIndex >= NUM_LEADERBOARD_ENTRIES && i == NUM_LEADERBOARD_ENTRIES - 1 && eaSortedEntries[iPlayerIndex])
			{
				pScoreboardEntry = eaSortedEntries[iPlayerIndex];
			}

			if(pLeaderEntry)
			{
				if(pScoreboardEntry)
				{
					if(pLeaderEntry->playerID != pScoreboardEntry->playerID || pLeaderEntry->fPoints != pScoreboardEntry->fPoints || pLeaderEntry->iRank != pScoreboardEntry->iRank)
					{
						pLeaderEntry->iRank = pScoreboardEntry->iRank;
						pLeaderEntry->fPoints = pScoreboardEntry->fPoints;
						pLeaderEntry->playerID = pScoreboardEntry->playerID;
						estrCopy(&pLeaderEntry->pchPlayerName, &pScoreboardEntry->pchPlayerName);
						estrCopy(&pLeaderEntry->pchAccountName, &pScoreboardEntry->pchAccountName);
						mission_FlagInfoAsDirty(pInfo);
					}
				}
				else
				{
					pLeaderEntry->iRank = i + 1;

					if(pLeaderEntry->playerID != 0)
					{
						pLeaderEntry->fPoints = 0.0f;
						pLeaderEntry->playerID = 0;
						estrClear(&pLeaderEntry->pchPlayerName);
						estrClear(&pLeaderEntry->pchAccountName);
						mission_FlagInfoAsDirty(pInfo);
					}
				}
			}

			if(pLeaderEntry && pLeaderEntry->playerID == pPlayerEnt->myContainerID)
			{
				pInfo->pLeaderboardInfo->iPlayerIndex = i;
			}
		}

		{//We store the name of the mission and the number of participants so that they can be referenced on the client after the mission has ended
			MissionDef *pOpenDef = mission_GetDef(pCurrentOpenMission->pMission);
			char *estrTemp = NULL;
			estrStackCreate(&estrTemp);
			if(pOpenDef)
			{
				missionsystem_FormatMessagePtr(locGetLanguage(getCurrentLocale()),"MissionUI", pPlayerEnt, pOpenDef, 0, &estrTemp, GET_REF(pOpenDef->displayNameMsg.hMessage));
			}

			if(stricmp(pInfo->pLeaderboardInfo->pOpenMissionDisplayName, estrTemp) != 0)
			{
				estrClear(&pInfo->pLeaderboardInfo->pOpenMissionDisplayName);
				estrCopy(&pInfo->pLeaderboardInfo->pOpenMissionDisplayName, &estrTemp);
				if (!pInfo->pLeaderboardInfo->pOpenMissionDisplayName || !pInfo->pLeaderboardInfo->pOpenMissionDisplayName[0])
				{
					estrClear(&pInfo->pLeaderboardInfo->pOpenMissionDisplayName);
					estrAppend2(&pInfo->pLeaderboardInfo->pOpenMissionDisplayName, "?????");
					estrCopy(&pInfo->pLeaderboardInfo->pOpenMissionDisplayName, &estrTemp);
				}
				mission_FlagInfoAsDirty(pInfo);
			}
			estrDestroy(&estrTemp);
		}

		{//Update the related event name
				MissionDef *pDef = mission_GetDef(pCurrentMission);
				if (pDef)
				{
					if(pInfo->pLeaderboardInfo->pchRelatedEvent != pDef->pchRelatedEvent)
					{
						pInfo->pLeaderboardInfo->pchRelatedEvent = pDef->pchRelatedEvent;
					}
				}
		}

		if(pInfo->pLeaderboardInfo->iTotalParticipants != eaSize(&eaSortedEntries))
		{
			pInfo->pLeaderboardInfo->iTotalParticipants = eaSize(&eaSortedEntries);
			mission_FlagInfoAsDirty(pInfo);
		}

		eaDestroy(&eaSortedEntries);
	}

	// Handle open team tech
	if(pCurrentMission)
	{
		MissionDef *pDef = mission_GetDef(pCurrentMission);
		gslTeamUp_HandleTeamUpRequest(pPlayerEnt,pDef?pDef->TeamUpName:NULL);
	}
	else
	{
		gslTeamUp_HandleTeamUpRequest(pPlayerEnt,NULL);
	}
}


void openmission_OncePerFrame(F32 fTimeStep)
{
	int iPartitionIdx;
	Entity *pCurrEnt;
	U32 uWhichPlayer = 0;
	EntityIterator* pIter;
	U32 uCurrOpenMissionTickModded = s_OpenMissionTick % OPEN_MISSION_SYSTEM_TICK;

	// Process all players
	// Don't process ENTITYFLAG_IGNORE ents here, just to save time; they're probably still loading the map
	pIter = entGetIteratorSingleTypeAllPartitions(0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
	while ((pCurrEnt = EntityIteratorGetNext(pIter))) {
		if ((uWhichPlayer % OPEN_MISSION_SYSTEM_TICK) == uCurrOpenMissionTickModded) {
			MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(pCurrEnt);
			iPartitionIdx = entGetPartitionIdx(pCurrEnt);

			PERFINFO_AUTO_START("OpenMissionPlayerTick", 1);
			openmission_ProcessOpenMissionsForPlayer(iPartitionIdx, pCurrEnt, pMissionInfo);

			PERFINFO_AUTO_STOP();
		}
		
		uWhichPlayer++;
	}
	EntityIteratorRelease(pIter);
	
	PERFINFO_AUTO_START("OpenMissionTick", 1);

	// Process all Open Missions on all partitions
	for(iPartitionIdx=eaSize(&s_eaOpenMissionPartitionStates)-1; iPartitionIdx>=0; --iPartitionIdx) {
		OpenMissionPartitionState *pState = s_eaOpenMissionPartitionStates[iPartitionIdx];
		int i;

		if (!pState || mapState_IsMapPausedForPartition(iPartitionIdx)) {
			continue;
		}
		
		for (i = 0; i < eaSize(&pState->eaOpenMissions); i++){
			OpenMission *pOpenMission = pState->eaOpenMissions[i];
			Mission *pMission = pOpenMission->pMission;

			// Process Mission
			if (pMission && mission_NeedsEvaluation(pMission)) {
				mission_UpdateState(iPartitionIdx, NULL, pMission);
			}

			// Update Reset timer
			if (pOpenMission->fResetTimeRemaining > 0.0f) {
				pOpenMission->fResetTimeRemaining -= fTimeStep;
				if (pOpenMission->fResetTimeRemaining < 0.0f) {
					pOpenMission->fResetTimeRemaining = 0.0f;
				}
				pOpenMission->uiResetTimeRemaining = (U32)pOpenMission->fResetTimeRemaining;
			}
		}
	}

	s_OpenMissionTick++;

	PERFINFO_AUTO_STOP();
}


// ----------------------------------------------------------------------------------
// Open Mission Lifecycle
// ----------------------------------------------------------------------------------

void openmission_BeginOpenMission( int iPartitionIdx, MissionDef *pDef, bool bSendNotification )
{
	OpenMissionPartitionState *pState;
	OpenMission *pOpenMission;

	if (!pDef || (pDef->missionType != MissionType_OpenMission)) {
		Errorf("OpenMissionStart: Mission %s is not an Open Mission!", pDef->name);
		return;
	}

	pOpenMission = openmission_GetFromName(iPartitionIdx, pDef->name);
	if (!pOpenMission) {
		// Create the open mission
		pState = openmission_GetPartitionState(iPartitionIdx);
		pOpenMission = openmission_CreateOpenMission(iPartitionIdx, pDef->name);
		if (!pState->eaOpenMissions) {
			eaIndexedEnable(&pState->eaOpenMissions, parse_OpenMission);
		}
		eaIndexedAdd(&pState->eaOpenMissions, pOpenMission);
		if (!pState->pOpenMissionTable) {
			pState->pOpenMissionTable = stashTableCreateWithStringKeys(8, StashDefault);
		}
		stashAddPointer(pState->pOpenMissionTable, pOpenMission->pchName, pOpenMission, false);

		// Initialize the data
		pOpenMission->pMission = (Mission*)mission_CreateMission(iPartitionIdx, pDef, missiondef_CalculateLevel(iPartitionIdx, 0, pDef), 0);
		NOCONSTMISSION(pOpenMission->pMission)->state = MissionState_InProgress;
		NOCONSTMISSION(pOpenMission->pMission)->startTime = timeSecondsSince2000();
		mission_PostMissionCreateInit(iPartitionIdx, pOpenMission->pMission, NULL, pOpenMission, NULL, true);

		eventsend_RecordMissionState(iPartitionIdx, NULL, pDef->name, MissionType_OpenMission, MissionState_Started, REF_STRING_FROM_HANDLE(pDef->hCategory), true, 0);
		eventsend_RecordMissionState(iPartitionIdx, NULL, pDef->name, MissionType_OpenMission, MissionState_InProgress, REF_STRING_FROM_HANDLE(pDef->hCategory), true, 0);

		// Run Actions to add new sub-missions and send floaters
		gameaction_np_RunActionsSubMissions(iPartitionIdx, pOpenMission->pMission, &pDef->ppOnStartActions);

		// Must notify Map State any time the open mission array changes
		mapState_UpdateOpenMissions(iPartitionIdx, pState->eaOpenMissions);

		if(pDef->TeamUpName)
			gslTeamUp_CreateNewInstance(iPartitionIdx,pDef->TeamUpName,&pDef->TeamUpDisplayName, kTeamUpFlags_None);

		if(bSendNotification)
		{
			EntityIterator *iter = entGetIteratorSingleType(iPartitionIdx, 0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
			Entity *pEnt = NULL;

			while (pEnt = EntityIteratorGetNext(iter))
			{
				const char *pchDispName = TranslateDisplayMessage(pDef->displayNameMsg);
				ClientCmd_NotifySend(pEnt, kNotifyType_OpenMissionStarted, pchDispName, pOpenMission->pchName, NULL);
			}

			EntityIteratorRelease(iter);
		}
	}
}


void openmission_BeginAutoStartOpenMissions(int iPartitionIdx)
{
	const char *pcMapName = zmapInfoGetPublicName(NULL);

	// Start all auto-starting open missions 
	FOR_EACH_IN_REFDICT(g_MissionDictionary, MissionDef, pDef) {
		if ((pDef->missionType == MissionType_OpenMission) && 
			(pDef->autoGrantOnMap) && 
			(stricmp(pcMapName, pDef->autoGrantOnMap) == 0) && 
			(missiondef_CheckRequiredActivitiesActive(pDef)))
		{
			openmission_BeginOpenMission(iPartitionIdx, pDef, false);
		}
	}
	FOR_EACH_END;
}


static void openmission_EndOpenMissionInternal(OpenMissionPartitionState *pState, OpenMission *pOpenMission)
{
	eaFindAndRemove(&pState->eaOpenMissions, pOpenMission);
	if (pState->pOpenMissionTable) {
		stashRemovePointer(pState->pOpenMissionTable, pOpenMission->pchName, NULL);
	}
	openmission_DestroyOpenMission(pOpenMission);

	// Must notify Map State any time the open mission array changes
	mapState_UpdateOpenMissions(pState->iPartitionIdx, pState->eaOpenMissions);
}


void openmission_EndOpenMission(int iPartitionIdx, MissionDef *pDef)
{
	OpenMission *pOpenMission = openmission_GetFromName(iPartitionIdx, pDef->name);
	if (pOpenMission) {
		OpenMissionPartitionState *pState = openmission_GetPartitionState(iPartitionIdx);
		if (pState) {
			openmission_EndOpenMissionInternal(pState, pOpenMission);
		}
	}
}


static void openmission_EndAllOpenMissions(int iPartitionIdx)
{
	OpenMissionPartitionState *pState = openmission_GetPartitionState(iPartitionIdx);
	if (pState) {
		eaDestroyEx(&pState->eaOpenMissions, openmission_DestroyOpenMission);
		stashTableDestroySafe(&pState->pOpenMissionTable);

		// Must notify Map State any time the open mission array changes
		mapState_UpdateOpenMissions(iPartitionIdx, pState->eaOpenMissions);
	}
}


void openmission_ResetOpenMissions(int iPartitionIdx)
{
	openmission_EndAllOpenMissions(iPartitionIdx);
	openmission_BeginAutoStartOpenMissions(iPartitionIdx);
}


// ----------------------------------------------------------------------------------
// Partition Lifecycle
// ----------------------------------------------------------------------------------

void openmission_PartitionLoad(int iPartitionIdx)
{
	// Create state if it doesn't exist
	OpenMissionPartitionState *pState = eaGet(&s_eaOpenMissionPartitionStates, iPartitionIdx);
	if (!pState) {
		pState = openmission_CreatePartitionState(iPartitionIdx);
		eaSet(&s_eaOpenMissionPartitionStates, pState, iPartitionIdx);
	}

	// Start any auto-start missions for the partition
	openmission_ResetOpenMissions(iPartitionIdx);
}


void openmission_PartitionUnload(int iPartitionIdx)
{
	// Destroy partition state
	OpenMissionPartitionState *pState = eaGet(&s_eaOpenMissionPartitionStates, iPartitionIdx);
	if (pState) {
		openmission_DestroyPartitionState(pState);
		eaSet(&s_eaOpenMissionPartitionStates, NULL, iPartitionIdx);
	}
	
}


void openmission_MapLoad(bool bFullInit)
{
	if (bFullInit) {
		partition_ExecuteOnEachPartition(openmission_PartitionLoad);
	}
}


void openmission_MapUnload(void)
{
	int i;
	for(i=eaSize(&s_eaOpenMissionPartitionStates)-1; i>=0; --i) {
		if (s_eaOpenMissionPartitionStates[i]) {
			openmission_PartitionUnload(i);
		}
	}
}
