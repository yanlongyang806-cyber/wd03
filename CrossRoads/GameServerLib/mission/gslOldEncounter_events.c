/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "array.h"
#include "earray.h"
#include "Entity.h"
#include "estring.h"
#include "GameEvent.h"
#include "gslEventTracker.h"
#include "gslOldEncounter.h"
#include "gslOldEncounter_events.h"
#include "mission_common.h"
#include "oldencounter_common.h"
#include "StashTable.h"
#include "stringcache.h"
#include "textparser.h"

#include "Autogen/GameEvent_h_ast.h"
#include "mission_common_h_ast.h"


// ----------------------------------------------------------------------------------
// Static Data
// ----------------------------------------------------------------------------------

extern OldEncounterPartitionState **s_eaOldEncounterPartitionStates;


// ----------------------------------------------------------------------------------
// Event Tracking Functions
// ----------------------------------------------------------------------------------

void oldencounter_EventCountAdd(OldEncounter* encounter, GameEvent *ev, GameEvent *specific, int increment)
{
	int eventCount = 0;
	if (ev) {
		eventCount = oldencounter_EventCount(encounter, ev->pchEventName);
		stashAddInt(encounter->eventLog, ev->pchEventName, eventCount+increment, true);
	}
}


void oldencounter_EventCountAddSinceSpawn(OldEncounter* encounter, GameEvent *ev, GameEvent *specific, int increment)
{
	int eventCount = 0;
	if (ev) {
		eventCount = oldencounter_EventCountSinceSpawn(encounter, ev->pchEventName);
		stashAddInt(encounter->eventLogSinceSpawn, ev->pchEventName, eventCount+increment, true);
	}
}


void oldencounter_EventCountAddSinceComplete(OldEncounter* encounter, GameEvent *ev, GameEvent *specific, int increment)
{
	int eventCount = 0;
	if (ev) {
		eventCount = oldencounter_EventCountSinceComplete(encounter, ev->pchEventName);
		stashAddInt(encounter->eventLogSinceComplete, ev->pchEventName, eventCount+increment, true);
	}
}


void oldencounter_EventCountSet(OldEncounter* encounter, GameEvent *ev, GameEvent *specific, int value)
{
	if (ev) {
		stashAddInt(encounter->eventLog, ev->pchEventName, value, true);
	}
}


void oldencounter_EventCountSetSinceSpawn(OldEncounter* encounter, GameEvent *ev, GameEvent *specific, int value)
{
	if (ev) {
		stashAddInt(encounter->eventLogSinceSpawn, ev->pchEventName, value, true);
	}
}


void oldencounter_EventCountSetSinceComplete(OldEncounter* encounter, GameEvent *ev, GameEvent *specific, int value)
{
	if (ev) {
		stashAddInt(encounter->eventLogSinceComplete, ev->pchEventName, value, true);
	}
}


int oldencounter_EventCount(OldEncounter* encounter, const char *pchEventName)
{
	int eventCount;
	stashFindInt(encounter->eventLog, pchEventName, &eventCount);
	return eventCount;
}


int oldencounter_EventCountSinceSpawn(OldEncounter* encounter, const char *pchEventName)
{
	int eventCount;
	stashFindInt(encounter->eventLogSinceSpawn, pchEventName, &eventCount);
	return eventCount;
}


int oldencounter_EventCountSinceComplete(OldEncounter* encounter, const char *pchEventName)
{
	int eventCount;
	stashFindInt(encounter->eventLogSinceComplete, pchEventName, &eventCount);
	return eventCount;
}


// Set up a "MatchSource" or "MatchTarget" event for this encounter, if necessary
static GameEvent* oldencounter_SetUpEncounterScopedEvent(int iPartitionIdx, GameEvent*** peaScopedEvents, GameEvent *ev, const char *pchPooledEncName)
{
	GameEvent *pEventCopy = StructClone(parse_GameEvent, ev);
	
	// Set up event copy
	pEventCopy->iPartitionIdx = iPartitionIdx;
	eaPush(peaScopedEvents, pEventCopy);

	if (peaScopedEvents && ev && pchPooledEncName &&
		pEventCopy->tMatchSource != TriState_DontCare || pEventCopy->tMatchTarget != TriState_DontCare) 
	{
		if (pEventCopy->tMatchSource == TriState_Yes){
			pEventCopy->pchSourceStaticEncName = pchPooledEncName;
		} else if (pEventCopy->tMatchSource == TriState_No){
			pEventCopy->pchSourceStaticEncExclude = pchPooledEncName;
		}

		if (pEventCopy->tMatchTarget == TriState_Yes){
			pEventCopy->pchTargetStaticEncName = pchPooledEncName;
		} else if (pEventCopy->tMatchTarget == TriState_No){
			pEventCopy->pchTargetStaticEncExclude = pchPooledEncName;
		}
	}

	return pEventCopy;
}


void oldencounter_BeginTrackingEvents(OldEncounter* encounter)
{
	EncounterDef* def = oldencounter_GetDef(encounter);
	OldStaticEncounter *staticEnc = encounter?GET_REF(encounter->staticEnc):NULL;
	if (encounter && def && staticEnc) {
		// Copy events from EncounterDef onto OldEncounter instance
		int i, n = eaSize(&def->eaUnsharedTrackedEvents);
		for (i = 0; i < n; i++) {
			GameEvent *ev = oldencounter_SetUpEncounterScopedEvent(encounter->iPartitionIdx, &encounter->eaTrackedEvents, def->eaUnsharedTrackedEvents[i], staticEnc->name);
			if (ev) {
				eventtracker_StartTracking(ev, NULL, encounter, oldencounter_EventCountAdd, oldencounter_EventCountSet);
			}
		}

		n = eaSize(&def->eaUnsharedTrackedEventsSinceSpawn);
		for (i = 0; i < n; i++) {
			GameEvent *ev = oldencounter_SetUpEncounterScopedEvent(encounter->iPartitionIdx, &encounter->eaTrackedEventsSinceSpawn, def->eaUnsharedTrackedEventsSinceSpawn[i], staticEnc->name);
			if (ev) {
				eventtracker_StartTracking(ev, NULL, encounter, oldencounter_EventCountAddSinceSpawn, oldencounter_EventCountSetSinceSpawn);
			}
		}

		n = eaSize(&def->eaUnsharedTrackedEventsSinceComplete);
		for (i = 0; i < n; i++) {
			GameEvent *ev = oldencounter_SetUpEncounterScopedEvent(encounter->iPartitionIdx, &encounter->eaTrackedEventsSinceComplete, def->eaUnsharedTrackedEventsSinceComplete[i], staticEnc->name);
			if (ev) {
				eventtracker_StartTracking(ev, NULL, encounter, oldencounter_EventCountAddSinceComplete, oldencounter_EventCountSetSinceComplete);
			}
		}
	}
}


void oldencounter_StopTrackingEvents(OldEncounter* encounter)
{
	EncounterDef* def = oldencounter_GetDef(encounter);
	int i;

	if (encounter) {
		for (i = 0; i < eaSize(&encounter->eaTrackedEvents); i++) {
			eventtracker_StopTracking(encounter->iPartitionIdx, encounter->eaTrackedEvents[i], encounter);
		}
		eaDestroyStruct(&encounter->eaTrackedEvents, parse_GameEvent);

		for (i = 0; i < eaSize(&encounter->eaTrackedEventsSinceSpawn); i++) {
			eventtracker_StopTracking(encounter->iPartitionIdx, encounter->eaTrackedEventsSinceSpawn[i], encounter);
		}
		eaDestroyStruct(&encounter->eaTrackedEventsSinceSpawn, parse_GameEvent);

		for (i = 0; i < eaSize(&encounter->eaTrackedEventsSinceComplete); i++) {
			eventtracker_StopTracking(encounter->iPartitionIdx, encounter->eaTrackedEventsSinceComplete[i], encounter);
		}
		eaDestroyStruct(&encounter->eaTrackedEventsSinceComplete, parse_GameEvent);
	}
}


void oldencounter_StopTrackingForName(const char *encounterName)
{
	int iPartitionIdx;
	for(iPartitionIdx=eaSize(&s_eaOldEncounterPartitionStates)-1; iPartitionIdx>=0; --iPartitionIdx) {
		OldEncounterPartitionState *pState = s_eaOldEncounterPartitionStates[iPartitionIdx];
		if (pState) {
			int i, n = eaSize(&pState->eaEncounters);
			for (i = 0; i < n; i++) {
				EncounterDef *eDef;
				OldEncounter* encounter = pState->eaEncounters[i];
				eDef = oldencounter_GetDef(encounter);
				if (eDef && eDef->name && stricmp(eDef->name, encounterName)) {
					oldencounter_StopTrackingEvents(encounter);
				}
			}
		}
	}
}


void oldencounter_StartTrackingForName(const char *encounterName)
{
	int iPartitionIdx;
	for(iPartitionIdx=eaSize(&s_eaOldEncounterPartitionStates)-1; iPartitionIdx>=0; --iPartitionIdx) {
		OldEncounterPartitionState *pState = s_eaOldEncounterPartitionStates[iPartitionIdx];
		if (pState) {
			int i, n = eaSize(&pState->eaEncounters);
			for (i = 0; i < n; i++) {
				EncounterDef *eDef;
				OldEncounter* encounter = pState->eaEncounters[i];
				eDef = oldencounter_GetDef(encounter);
				if (eDef && eDef->name && stricmp(eDef->name, encounterName)) {
					oldencounter_BeginTrackingEvents(encounter);
				}
			}
		}
	}
}