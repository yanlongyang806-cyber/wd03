/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

typedef struct Mission Mission;
typedef struct MissionDef MissionDef;
typedef struct MissionInfo MissionInfo;
typedef struct GameEvent GameEvent;
typedef struct EventTracker EventTracker;

// Gets the EventTracker from the owner of the mission
EventTracker* mission_GetEventTracker(Mission *pMission);

// Gets the current value for the event count from the mission
// 0 will be returned if event is not tracked
int missionevent_EventCount(Mission *pMission, const char *pcEventName);

// Adds all the events the mission cares about to the global tracker
void missionevent_StartTrackingEvents(int iPartitionIdx, MissionDef *pDef, Mission *pMission);
void missionevent_StartTrackingEventsRecursive(int iPartitionIdx, MissionDef *pDef, Mission *pMission);

// Removes all the events the mission cares about from the global tracker
void missionevent_StopTrackingEvents(int iPartitionIdx, Mission *pMission);
void missionevent_StopTrackingEventsRecursive(int iPartitionIdx, Mission *pMission);

// Ensures all missions continue to track the correct events after a map change
void missionevent_StopTrackingAll(void);
void missionevent_StartTrackingAll(void);

// Ensures all missions continue to track the correct events after a def changes
void missionevent_StopTrackingForName(const char *pcMissionName);
void missionevent_StartTrackingForName(const char *pcMissionName);
