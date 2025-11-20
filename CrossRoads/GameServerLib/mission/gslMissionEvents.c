/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "array.h"
#include "earray.h"
#include "Entity.h"
#include "entityIterator.h"
#include "estring.h"
#include "GameEvent.h"
#include "gslEventTracker.h"
#include "gslMission.h"
#include "gslMissionEvents.h"
#include "gslMission_transact.h"
#include "gslOldEncounter.h"
#include "gslOpenMission.h"
#include "gslPartition.h"
#include "gslPlayerstats.h"
#include "gslUserExperience.h"
#include "mapstate_common.h"
#include "mission_common.h"
#include "objtransactions.h"
#include "Player.h"
#include "playerstats_common.h"
#include "stringcache.h"
#include "timedeventqueue.h"
#include "WorldGrid.h"

#include "GameEvent_h_ast.h"
#include "AutoGen/mission_common_h_ast.h"


// ----------------------------------------------------------------------------
//  Mission Event Functions
// ----------------------------------------------------------------------------


// Adds an EventCount to the Event Log
static void missionevent_EventCountAdd(Mission *pMission, GameEvent *pSubscribeEvent, GameEvent *pSpecificEvent, int iIncrement)
{
	if (!pSubscribeEvent || !pMission) {
		return;
	}

	if (mission_GetType(pMission) == MissionType_Perk && !mission_IsPersisted(pMission)) {
		mission_tr_PersistMissionAndUpdateEventCount(pMission->infoOwner->parentEnt, pMission, pSubscribeEvent->pchEventName, iIncrement, false);

	} else {
		MissionEventContainer ***peaEventLog = (MissionEventContainer***)&pMission->eaEventCounts;
		MissionEventContainer *pEntry = eaIndexedGetUsingString(peaEventLog, pSubscribeEvent->pchEventName);
		Mission *pRootMission = pMission;

		if (!pEntry) {
			// Because field is NO_INDEXED_PREALLOC we need to do this manually
			eaIndexedEnable(peaEventLog, parse_MissionEventContainer);

			pEntry = StructCreate(parse_MissionEventContainer);
			pEntry->pchEventName = pSubscribeEvent->pchEventName;
			eaIndexedAdd(peaEventLog, pEntry);
		}
		if (pEntry) {
			pEntry->iEventCount += iIncrement;
		}
		mission_FlagAsDirty(pMission);
		mission_FlagAsNeedingEval(pMission);

		if (pMission->infoOwner) {
			while (pRootMission && pRootMission->parent)
			{
				pRootMission = pRootMission->parent;
			}

			// Log for user experience system
			UserExp_LogMissionProgress(pMission->infoOwner->parentEnt, pRootMission->missionNameOrig, 
						(pRootMission == pMission ? NULL : pMission->missionNameOrig), 
						pEntry->pchEventName, pEntry->iEventCount);
		}
	}
}


// Sets an EventCount in the Event Log
static void missionevent_EventCountSet(Mission *pMission, GameEvent *pSubscribeEvent, GameEvent *pSpecificEvent, int iValue)
{
	if (!pSubscribeEvent || !pMission) {
		return;
	}

	if (mission_GetType(pMission) == MissionType_Perk && !mission_IsPersisted(pMission)) {
		mission_tr_PersistMissionAndUpdateEventCount(pMission->infoOwner->parentEnt, pMission, pSubscribeEvent->pchEventName, iValue, true);

	} else {
		MissionEventContainer ***peaEventLog = (MissionEventContainer***)&pMission->eaEventCounts;
		MissionEventContainer *pEntry = eaIndexedGetUsingString(peaEventLog, pSubscribeEvent->pchEventName);
		Mission *pRootMission = pMission;

		if (!pEntry) {
			// Because field is NO_INDEXED_PREALLOC we need to do this manually
			eaIndexedEnable(peaEventLog, parse_MissionEventContainer);

			pEntry = StructCreate(parse_MissionEventContainer);
			pEntry->pchEventName = pSubscribeEvent->pchEventName;
			eaIndexedAdd(peaEventLog, pEntry);
		}
		if (pEntry) {
			pEntry->iEventCount = iValue;
		}
		mission_FlagAsDirty(pMission);
		mission_FlagAsNeedingEval(pMission);

		if (pMission->infoOwner) {
			while (pRootMission && pRootMission->parent)
			{
				pRootMission = pRootMission->parent;
			}

			// Log for user experience system
			UserExp_LogMissionProgress(pMission->infoOwner->parentEnt, pRootMission->missionNameOrig, 
						(pRootMission == pMission ? NULL : pMission->missionNameOrig), 
						pEntry->pchEventName, pEntry->iEventCount);
		}
	}
}


// This callback is used for trackedEventsNoSave; it just flags the Mission as dirty without recording the Event
static void missionevent_TriggerUpdateFromEvent(Mission *pMission, GameEvent *pSubscribeEvent, GameEvent *pSpecificEvent, int iIncrement)
{
	mission_FlagAsNeedingEval(pMission);
}


// Callback from Tracked Stats
static void missionevent_TrackedStatUpdateCB(const char *pcPooledPlayerStatName, U32 uOldValue, U32 uNewValue, Mission *pMission)
{
	mission_FlagAsNeedingEval(pMission);
}


int missionevent_EventCount(Mission *pMission, const char *pcEventName)
{
	MissionEventContainer *pWhichEvent = eaIndexedGetUsingString(&pMission->eaEventCounts, pcEventName);
	return pWhichEvent ? pWhichEvent->iEventCount : 0;
}


void missionevent_StartTrackingEvents(int iPartitionIdx, MissionDef *pDef, Mission *pMission)
{
	Entity *pPlayerEnt;
	const char *pcMapName;
	int i, n, iNumTrackedEvents;

	if (!pMission || !pDef) {
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	// Skip any Missions that are completed and will never uncomplete
	if (pMission->state == MissionState_Succeeded && pDef->doNotUncomplete){
		PERFINFO_AUTO_STOP();
		return;
	}

	// Failed missions can never un-fail, so they should not track Events
	if (pMission->state == MissionState_Failed){
		PERFINFO_AUTO_STOP();
		return;
	}

	// Complain and don't take action if are trying to begin tracking events a second time
	// Code is supposed to ensure that previous events are stopped before beginning again,
	// otherwise we get weird side-effects.
	if (eaSize(&pMission->eaTrackedEvents) > 0) {
		Errorf("Trying to track events for a second time on mission '%s'", pDef->name);
		PERFINFO_AUTO_STOP();
		return;
	}

	// Clean up any previous events tracked, which matters on character reset
	eaDestroyStruct(&pMission->eaTrackedEvents, parse_GameEvent); 
	eaDestroyStruct(&pMission->eaTrackedScoreboardEvents, parse_OpenMissionScoreEvent); 

	// Set default capacity for the TrackedEventOverrides array
	iNumTrackedEvents = eaSize(&pDef->eaTrackedEvents) + eaSize(&pDef->eaTrackedEventsNoSave);
	if (iNumTrackedEvents > 1) {
		eaSetCapacity(&pMission->eaTrackedEvents, iNumTrackedEvents);
	}

	pcMapName = zmapInfoGetPublicName(NULL);
	pPlayerEnt = SAFE_MEMBER2(pMission, infoOwner, parentEnt);

	// Start tracking "normal" events
	n = eaSize(&pDef->eaTrackedEvents);
	for (i = 0; i < n; i++) {
		GameEvent *pDefEvent = pDef->eaTrackedEvents[i];
		if (!pDefEvent->pchEventName) {
			ErrorFilenamef(pDef->filename, "Mission %s has a tracked event that has no name", pDef->name);
		} else if (!pDefEvent->pchMapName || pDefEvent->pchMapName == pcMapName) {
			GameEvent *pEvent = gameevent_SetupPlayerScopedEvent(pDefEvent, pPlayerEnt);
			if (!pEvent) {
				pEvent = StructClone(parse_GameEvent, pDefEvent);
			}
			if (pEvent) {
				pEvent->iPartitionIdx = iPartitionIdx;
				if (pEvent->bIsTrackedMission && pMission->pchTrackedMission) {
					pEvent->pchTrackedMission = allocAddString(pMission->pchTrackedMission);
				}
				eventtracker_StartTrackingEx(pEvent, NULL, pMission, missionevent_EventCountAdd, missionevent_EventCountSet, false);
				eaPush(&pMission->eaTrackedEvents, pEvent);
			}
		}
	}

	// Start tracking "no save" events
	n = eaSize(&pDef->eaTrackedEventsNoSave);
	for (i = 0; i < n; i++) {
		GameEvent *pDefEvent = pDef->eaTrackedEventsNoSave[i];
		if (!pDefEvent->pchEventName) {
			ErrorFilenamef(pDef->filename, "Mission %s has a no-save event that has no name", pDef->name);
		} else if (!pDefEvent->pchMapName || pDefEvent->pchMapName == pcMapName) {
			GameEvent *pEvent = gameevent_SetupPlayerScopedEvent(pDefEvent, pPlayerEnt);
			if (!pEvent) {
				pEvent = StructClone(parse_GameEvent, pDefEvent);
			}
			if (pEvent) {
				pEvent->iPartitionIdx = iPartitionIdx;
				if (pEvent->bIsTrackedMission && pMission->pchTrackedMission) {
					pEvent->pchTrackedMission = allocAddString(pMission->pchTrackedMission);
				}
				eventtracker_StartTrackingEx(pEvent, NULL, pMission, missionevent_TriggerUpdateFromEvent, missionevent_TriggerUpdateFromEvent, false);
				eaPush(&pMission->eaTrackedEvents, pEvent);
			}
		}
	}
			

	// Start tracking "stats"
	if (pPlayerEnt) {
		PlayerStatsInfo *pStatsInfo = SAFE_MEMBER2(pPlayerEnt, pPlayer, pStatsInfo);

		if (pStatsInfo) {
			n = eaSize(&pDef->eaTrackedStats);
			for (i = 0; i < n; i++){
				playerstats_BeginTracking(pStatsInfo, pDef->eaTrackedStats[i], missionevent_TrackedStatUpdateCB, pMission);
			}
		}
	}

			
	// Timeout
	pMission->expirationTime = mission_GetEndTime(pDef, pMission);
	if (pMission->expirationTime) {
		timedeventqueue_Set("Mission", pMission, pMission->expirationTime);
	}

	// Start tracking Open Mission scoreboard Events
	if (missiondef_GetType(pDef) == MissionType_OpenMission) {
		for (i = 0; i < eaSize(&pDef->eaOpenMissionScoreEvents); i++) {
			OpenMissionScoreEvent *pScoreEvent = pDef->eaOpenMissionScoreEvents[i];
			if (pScoreEvent && pScoreEvent->pEvent && pScoreEvent->fScale) {
				OpenMissionScoreEvent *pCopy = StructClone(parse_OpenMissionScoreEvent, pScoreEvent);
				pCopy->pEvent->iPartitionIdx = iPartitionIdx;
				eventtracker_StartTracking(pCopy->pEvent, NULL, pMission, openmission_OpenMissionScoreEventCB, openmission_OpenMissionScoreEventCB);
				eaPush(&pMission->eaTrackedScoreboardEvents, pCopy);
			}
		}
	}

	PERFINFO_AUTO_STOP();
}


void missionevent_StartTrackingEventsRecursive(int iPartitionIdx, MissionDef *pDef, Mission *pMission)
{
	int i, n = eaSize(&pMission->children);
	
	missionevent_StartTrackingEvents(iPartitionIdx, pDef, pMission);
	
	for (i = 0; i < n; i++) {
		MissionDef *pChildDef = mission_GetDef(pMission->children[i]);
		missionevent_StartTrackingEventsRecursive(iPartitionIdx, pChildDef, pMission->children[i]);
	}
}


void missionevent_StopTrackingEvents(int iPartitionIdx, Mission *pMission)
{
	MissionDef *pDef;
	PlayerStatsInfo *pStatsInfo;
	int i;

	if (!pMission) {
		return;
	}

	// Stop tracking mission instance events
	for (i = eaSize(&pMission->eaTrackedEvents)-1; i >= 0; --i) {
		eventtracker_StopTracking(iPartitionIdx, pMission->eaTrackedEvents[i], pMission);
	}
	eaDestroyStruct(&pMission->eaTrackedEvents, parse_GameEvent);

	// Stop tracking Open Mission scoreboard instance events
	for (i = eaSize(&pMission->eaTrackedScoreboardEvents)-1; i >= 0; --i) {
		eventtracker_StopTracking(iPartitionIdx, pMission->eaTrackedScoreboardEvents[i]->pEvent, pMission);
	}
	eaDestroyStruct(&pMission->eaTrackedScoreboardEvents, parse_OpenMissionScoreEvent);

	// Stop tracking player stats
	pStatsInfo = SAFE_MEMBER4(pMission, infoOwner, parentEnt, pPlayer, pStatsInfo);
	if (pStatsInfo) {
		playerstats_StopTrackingAllForListener(pStatsInfo, pMission);
	}

	// Clear timeout
	pDef = mission_GetDef(pMission);
	if (pDef) {
		if (pDef->uTimeout){
			timedeventqueue_Remove("Mission", pMission);
			pMission->expirationTime = 0;
			mission_FlagAsDirty(pMission);
		}
	}
}


void missionevent_StopTrackingEventsRecursive(int iPartitionIdx, Mission *pMission)
{
	int i, n = eaSize(&pMission->children);
	
	missionevent_StopTrackingEvents(iPartitionIdx, pMission);
	
	for (i = 0; i < n; i++) {
		missionevent_StopTrackingEventsRecursive(iPartitionIdx, pMission->children[i]);
	}
}


static void missionevent_StopTrackingAllRecurse(int iPartitionIdx, CONST_EARRAY_OF(Mission) eaMissions)
{
	int i, n = eaSize(&eaMissions);
	for (i = 0; i < n; i++) {
		missionevent_StopTrackingEvents(iPartitionIdx, eaMissions[i]);
		missionevent_StopTrackingAllRecurse(iPartitionIdx, eaMissions[i]->children);
	}
}


void missionevent_StopTrackingAll(void)
{
	Entity *pCurrEnt;
	EntityIterator *pIter = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);

	// Stop tracking on all players
	while ((pCurrEnt = EntityIteratorGetNext(pIter))) {
		MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(pCurrEnt);
		if (pMissionInfo) {
			int iPartitionIdx = entGetPartitionIdx(pCurrEnt);
			missionevent_StopTrackingAllRecurse(iPartitionIdx, pMissionInfo->missions);
			missionevent_StopTrackingAllRecurse(iPartitionIdx, pMissionInfo->eaNonPersistedMissions);
			missionevent_StopTrackingAllRecurse(iPartitionIdx, pMissionInfo->eaDiscoveredMissions);
		}
	}
	EntityIteratorRelease(pIter);

	// Stop tracking for open missions
	openmission_StopTrackingAllEvents();
}


static void missionevent_StartTrackingAllRecurse(int iPartitionIdx, CONST_EARRAY_OF(Mission) eaMissions)
{
	int i, n = eaSize(&eaMissions);
	for (i = 0; i < n; i++) {
		MissionDef *pDef = mission_GetDef(eaMissions[i]);
		missionevent_StartTrackingEvents(iPartitionIdx, pDef, eaMissions[i]);
		mission_FlagAsNeedingEval(eaMissions[i]);
		missionevent_StartTrackingAllRecurse(iPartitionIdx, eaMissions[i]->children);
	}
}


void missionevent_StartTrackingAll(void)
{
	Entity *pCurrEnt;
	EntityIterator *pIter = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);

	// Stop tracking player events
	while ((pCurrEnt = EntityIteratorGetNext(pIter))) {
		MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(pCurrEnt);
		if (pMissionInfo) {
			int iPartitionIdx = entGetPartitionIdx(pCurrEnt);
			missionevent_StartTrackingAllRecurse(iPartitionIdx, pMissionInfo->missions);
			missionevent_StartTrackingAllRecurse(iPartitionIdx, pMissionInfo->eaNonPersistedMissions);
			missionevent_StartTrackingAllRecurse(iPartitionIdx, pMissionInfo->eaDiscoveredMissions);
		}
	}
	EntityIteratorRelease(pIter);

	// Stop tracking open mission events
	openmission_StopTrackingAllEvents();
}


void missionevent_StopTrackingForName(const char *pcMissionName)
{
	Entity *pCurrEnt;
	EntityIterator *pIter = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);
	int i, n;

	while ((pCurrEnt = EntityIteratorGetNext(pIter))) {
		MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(pCurrEnt);
		if (pMissionInfo) {
			int iPartitionIdx = entGetPartitionIdx(pCurrEnt);

			// Check normal missions
			n = eaSize(&pMissionInfo->missions);
			for(i = 0; i < n; i++) {
				MissionDef *pDef = mission_GetDef(pMissionInfo->missions[i]);
				if (pcMissionName && pDef && pDef->name && !stricmp(pcMissionName, pDef->name)) {
					missionevent_StopTrackingEventsRecursive(iPartitionIdx, pMissionInfo->missions[i]);
				}
			}

			// Check non-persisted missions
			n = eaSize(&pMissionInfo->eaNonPersistedMissions);
			for(i = 0; i < n; i++) {
				MissionDef *pDef = mission_GetDef(pMissionInfo->eaNonPersistedMissions[i]);
				if (pcMissionName && pDef && pDef->name && !stricmp(pcMissionName, pDef->name)) {
					missionevent_StopTrackingEventsRecursive(iPartitionIdx, pMissionInfo->eaNonPersistedMissions[i]);
				}
			}

			// Check discovered missions
			n = eaSize(&pMissionInfo->eaDiscoveredMissions);
			for(i = 0; i < n; i++) {
				MissionDef *pDef = mission_GetDef(pMissionInfo->eaDiscoveredMissions[i]);
				if (pcMissionName && pDef && pDef->name && !stricmp(pcMissionName, pDef->name))
					missionevent_StopTrackingEventsRecursive(iPartitionIdx, pMissionInfo->eaDiscoveredMissions[i]);
			}
		}
	}
	EntityIteratorRelease(pIter);

	// Stop open missions
	openmission_StopTrackingAllForName(pcMissionName);
}


void missionevent_StartTrackingForName(const char *pcMissionName)
{
	Entity *pCurrEnt;
	EntityIterator *pIter = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);
	int i, n;

	while ((pCurrEnt = EntityIteratorGetNext(pIter))) {
		MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(pCurrEnt);
		if (pMissionInfo) {
			int iPartitionIdx = entGetPartitionIdx(pCurrEnt);

			// Check normal missions
			n = eaSize(&pMissionInfo->missions);
			for(i = 0; i < n; i++) {
				MissionDef *pDef = mission_GetDef(pMissionInfo->missions[i]);
				if (pcMissionName && pDef && pDef->name && !stricmp(pcMissionName, pDef->name)) {
					missionevent_StartTrackingEventsRecursive(iPartitionIdx, pDef, pMissionInfo->missions[i]);
				}
			}

			// Check non-persisted missions
			n = eaSize(&pMissionInfo->eaNonPersistedMissions);
			for(i = 0; i < n; i++) {
				MissionDef *pDef = mission_GetDef(pMissionInfo->eaNonPersistedMissions[i]);
				if (pcMissionName && pDef && pDef->name && !stricmp(pcMissionName, pDef->name)) {
					missionevent_StartTrackingEventsRecursive(iPartitionIdx, pDef, pMissionInfo->eaNonPersistedMissions[i]);
				}
			}

			// Check discovered missions
			n = eaSize(&pMissionInfo->eaDiscoveredMissions);
			for(i = 0; i < n; i++) {
				MissionDef *pDef = mission_GetDef(pMissionInfo->eaDiscoveredMissions[i]);
				if (pcMissionName && pDef && pDef->name && !stricmp(pcMissionName, pDef->name)) {
					missionevent_StartTrackingEventsRecursive(iPartitionIdx, pDef, pMissionInfo->eaDiscoveredMissions[i]);
				}
			}
		}
	}
	EntityIteratorRelease(pIter);

	// Check open missions
	openmission_StartTrackingForName(pcMissionName);
}


