/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

typedef struct OldEncounter OldEncounter;
typedef struct EncounterDef EncounterDef;
typedef struct GameEvent GameEvent;

// Increments the existing value for the event count if the encounter cares
void oldencounter_EventCountAdd(OldEncounter* encounter, GameEvent *ev, GameEvent *specific, int increment);
void oldencounter_EventCountAddSinceSpawn(OldEncounter* encounter, GameEvent *ev, GameEvent *specific, int increment);
void oldencounter_EventCountAddSinceComplete(OldEncounter* encounter, GameEvent *ev, GameEvent *specific, int increment);

// Sets the value for the event count if the encounter cares
void oldencounter_EventCountSet(OldEncounter* encounter, GameEvent *ev, GameEvent *specific, int value);
void oldencounter_EventCountSetSinceSpawn(OldEncounter* encounter, GameEvent *ev, GameEvent *specific, int value);
void oldencounter_EventCountSetSinceComplete(OldEncounter* encounter, GameEvent *ev, GameEvent *specific, int value);

// Gets the current value for the event count from the encounter
// 0 will be returned if event is not tracked
int oldencounter_EventCount(OldEncounter* encounter, const char *pchEventName);
int oldencounter_EventCountSinceSpawn(OldEncounter* encounter, const char *pchEventName);
int oldencounter_EventCountSinceComplete(OldEncounter* encounter, const char *pchEventName);

// Adds all the events the encounter cares about to the global tracker
void oldencounter_BeginTrackingEvents(OldEncounter* encounter);

// Removes all the events the encounter cares about from the global tracker
void oldencounter_StopTrackingEvents(OldEncounter* encounter);

// Stop/start encounter system tracking for a specific def
void oldencounter_StopTrackingForName(const char *encounterName);
void oldencounter_StartTrackingForName(const char *encounterName);
