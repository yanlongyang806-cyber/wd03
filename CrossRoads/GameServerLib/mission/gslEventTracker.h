/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef GSLEVENTTRACKER_H
#define GSLEVENTTRACKER_H

typedef struct Entity Entity;
typedef struct EventTracker EventTracker;
typedef struct GameEvent GameEvent;
typedef struct WorldScope WorldScope;
typedef enum EventType EventType;

typedef enum EventUpdateType{
	EventLog_Add,
	EventLog_Set,
} EventUpdateType;

typedef void(*EventTrackerUpdateCountFunc)(void* structPtr, GameEvent *ev, GameEvent *specific, int value);
typedef void(*EventTrackerSetCountFunc)(void* structPtr, GameEvent *ev, GameEvent *specific, int value);
typedef void(*EventTrackerFinishedCB)(void* structPtr);

// Whether EventLog debugging is on or off
extern U32 g_EventLogDebug;

// Creates a new event tracker
EventTracker* eventtracker_Create(int iPartitionIdx);

// Frees the event tracker
void eventtracker_Destroy(SA_PARAM_OP_VALID EventTracker* tracker, bool bMoveObjectsToGlobalEventTracker);

// Sends an Event to all listeners
void eventtracker_SendEvent(int iPartitionIdx, GameEvent *ev, int count, EventUpdateType type, bool log);

// Starts tracking a GameEvent in the Event system.  The tracker to use will be determined automatically.
#define eventtracker_StartTracking(ev, scope, structPtr, updateFunc, setFunc) eventtracker_StartTrackingEx(ev, scope, structPtr, updateFunc, setFunc, true)
GameEvent* eventtracker_StartTrackingEx(GameEvent *ev, WorldScope *scope, void* structPtr, EventTrackerUpdateCountFunc updateFunc, EventTrackerSetCountFunc setFunc, bool bCheckDuplicates);

// Removes a struct from the eventtracker system
void eventtracker_StopTracking(int iPartitionIdx, GameEvent *ev, void* structPtr);
void eventtracker_StopTrackingAllForListener(int iPartitionIdx, void* structPtr);

// Get an event count from the global tracker
int eventtracker_GlobalEventCount(int iPartitionIdx, const char *pchEventName);

// Adds a tracked Event to the global EventTracker
void eventtracker_AddGlobalTrackedEvent(GameEvent *pEvent, const char *pchErrorFilename);

// Map Lifecycle
void eventtracker_PartitionLoad(int iPartitionIdx);
void eventtracker_PartitionLoadLate(int iPartitionIdx);
void eventtracker_PartitionUnload(int iPartitionIdx);

void eventtracker_MapLoad(bool bFullInit);
void eventtracker_MapLoadLate(bool bFullInit);
void eventtracker_MapUnload(void);

// This is a helper function that adds a GameEvent to a Tracked Events earray, with some error checking
bool eventtracker_AddNamedEventToList(GameEvent*** peaTrackedEvents, GameEvent *pEvent, const char *pchErrorFilename);

// This is a helper function for tricksy things
bool eventtracker_EntityMatchesEvent(GameEvent *listeningEvent, GameEvent *pSentEvent, Entity *e);
bool eventtracker_CaresAboutTarget(GameEvent *listeningEvent);

// This runs the supplied callback as soon as no Events are in the middle of sending.  If no Events are
// being sent, it will run immediately.
void eventtracker_QueueFinishedSendingCallback(EventTrackerFinishedCB callback, void* pUserData, bool bRunOnce);

void eventTracker_OncePerFrameUpdateAllPartitions();

// Debug function
void eventtracker_DebugPrint(GameEvent *ev, int iNumListeners);

#endif