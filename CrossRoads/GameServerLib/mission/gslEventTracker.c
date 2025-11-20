/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "AutoTransDefs.h"
#include "earray.h"
#include "Entity.h"
#include "entitylib.h"
#include "error.h"
#include "EString.h"
#include "eval.h"
#include "gameevent.h"
#include "GameServerLib.h"
#include "gslEncounter.h"
#include "gslEventSend.h"
#include "gslEventTracker.h"
#include "gslLogSettings.h"
#include "gslMapVariable.h"
#include "gslOldEncounter.h"
#include "gslPartition.h"
#include "gslVolume.h"
#include "logging.h"
#include "MapDescription.h"
#include "memorypool.h"
#include "mission_common.h"
#include "oldencounter_common.h"
#include "Player.h"
#include "PowerModes.h"
#include "stashtable.h"
#include "stringcache.h"
#include "team.h"
#include "textparser.h"
#include "timing.h"
#include "worldgrid.h"
#include "GameEvent.h"
#include "EntityIterator.h"

#include "gameevent_h_ast.h"

#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

// ----------------------------------------------------------------------------------
// Data types
// ----------------------------------------------------------------------------------

// A single Listener for an Event
typedef struct EventListener{
	void* structPtr;
	EventTrackerUpdateCountFunc updateFunc;
	EventTrackerSetCountFunc setFunc;
} EventListener;

// Mapping of EventListeners to a GameEvent
typedef struct EventTrackerObject
{
	GameEvent *ev;
	EventListener** listeners;
} EventTrackerObject;

typedef EventTrackerObject** TrackedObjectsArray;

// An EventTracker is what matches up sent events to listening events
typedef struct EventTracker{
	int iPartitionIdx;
	TrackedObjectsArray* eventTypeArray; // Events are saved in buckets according to the event type
	U32 uNumInvalid;
} EventTracker;

// Queued Callback to run when Event sending finishes
typedef struct EventTrackerQueuedCallback
{
	EventTrackerFinishedCB callback;
	void *pUserData;
} EventTrackerQueuedCallback;

typedef struct ChainedEventTracker
{
	EventTracker		*pTracker;
	EventTrackerObject	*pTrackedObject;
	S64					chainStartTime;
} ChainedEventTracker;


typedef struct EventTrackerPartitionState
{
	int iPartitionIdx;

	// All Events Trackers
	EventTracker **eaAllTrackers;
	bool bCompactTrackers;

	// Global Events Tracking
	EventTracker *pGlobalEventTracker;
	GameEvent **eaGlobalTrackedEvents;
	ChainedEventTracker **eaChainedEventTrackers;
	StashTable pGlobalEventLog;
	bool bGlobalEventLogTracking;
} EventTrackerPartitionState;



typedef struct EventLoggingCategoryDef
{
	EventType eEventType;
	enumLogCategory eLogCategory;
	gslEventLogGroup eGroup;
} EventLoggingCategoryDef;



static EventLoggingCategoryDef sLogCategoryDefs[] = 
{
	{
		EventType_ContactDialogStart,
		LOG_MISSION_EVENTS,
		LOGGROUP_CONTENTEVENT,
	},
	{
		EventType_ContactDialogComplete,      
		LOG_MISSION_EVENTS,
		LOGGROUP_CONTENTEVENT,
	},
	{
		EventType_CutsceneEnd,      
		LOG_CUTSCENE_EVENTS,
		LOGGROUP_CONTENTEVENT,
	},
	{
		EventType_CutsceneStart,      
		LOG_CUTSCENE_EVENTS,
		LOGGROUP_CONTENTEVENT,
	},
	{
		EventType_Damage,    
		LOG_DAMAGE_EVENTS,
		LOGGROUP_COMBATDAMAGEEVENT,
	},
	{
		EventType_EncounterState,    
		LOG_ENCOUNTER_EVENTS,
		LOGGROUP_CONTENTEVENT,
	},
	{
		EventType_Poke,    
		LOG_EVENTS,
		LOGGROUP_GAMEPLAYEVENT,
	},
	{
		EventType_FSMState,    
		LOG_FSM_EVENTS,
		LOGGROUP_CONTENTEVENT,
	},
	{
		EventType_Healing,
		LOG_HEALING_EVENTS,
		LOGGROUP_COMBATHEALINGEVENT,
	},
	{
		EventType_HealthState,    
		LOG_DAMAGE_EVENTS,
		LOGGROUP_COMBATDAMAGEEVENT,
	},
	{
		EventType_InteractBegin,      
		LOG_CLICKABLE_EVENTS,
		LOGGROUP_CONTENTEVENT,
	},
	{
		EventType_InteractFailure,      
		LOG_CLICKABLE_EVENTS,
		LOGGROUP_CONTENTEVENT,
	},
	{
		EventType_InteractInterrupted,      
		LOG_CLICKABLE_EVENTS,
		LOGGROUP_CONTENTEVENT,
	},
	{
		EventType_InteractSuccess,      
		LOG_CLICKABLE_EVENTS,
		LOGGROUP_CONTENTEVENT,
	},
	{
		EventType_InteractEndActive,      
		LOG_CLICKABLE_EVENTS,
		LOGGROUP_CONTENTEVENT,
	},
	{
		EventType_ItemGained,      
		LOG_ITEMGAINED_EVENTS,
		LOGGROUP_ITEMEVENT,
	},
	{
		EventType_ItemLost,      
		LOG_ITEMGAINED_EVENTS,
		LOGGROUP_ITEMEVENT,
	},
	{
		EventType_ItemPurchased,      
		LOG_ITEMGAINED_EVENTS,
		LOGGROUP_ITEMEVENT,
	},
	{
		EventType_ItemPurchaseEP,
		LOG_ITEMGAINED_EVENTS,
		LOGGROUP_ITEMEVENT,
	},
	{
		EventType_ItemUsed,
		LOG_ITEMGAINED_EVENTS,
		LOGGROUP_ITEMEVENT,
	},
	{
		EventType_GemSlotted,
		LOG_ITEMGAINED_EVENTS,
		LOGGROUP_ITEMEVENT,
	},
	{
		EventType_Kills,      
		LOG_KILL_EVENTS,
		LOGGROUP_COMBATKILLEVENT,
	},
	{
		EventType_Assists,
		LOG_KILL_EVENTS,
		LOGGROUP_COMBATKILLEVENT,
	},
	{
		EventType_NearDeath,
		LOG_KILL_EVENTS,
		LOGGROUP_COMBATKILLEVENT,
	},
	{
		EventType_LevelUp,      
		LOG_OTHER_PLAYER_EVENTS,
		LOGGROUP_GAMEPLAYEVENT,
	},
	{
		EventType_LevelUpPet,
		LOG_OTHER_PLAYER_EVENTS,
		LOGGROUP_GAMEPLAYEVENT,
	},
	{
		EventType_MissionLockoutState,      
		LOG_MISSION_EVENTS,
		LOGGROUP_CONTENTEVENT,
	},
	{
		EventType_MissionState,      
		LOG_MISSION_EVENTS,
		LOGGROUP_CONTENTEVENT,
	},
	{
		EventType_PickedUpObject,      
		LOG_EVENTS,
		LOGGROUP_GAMEPLAYEVENT,
	},
	{
		EventType_PowerAttrModApplied,      
		LOG_OTHER_PLAYER_EVENTS,
		LOGGROUP_GAMEPLAYEVENT,
	},
	{
		EventType_PlayerSpawnIn,   
		LOG_OTHER_PLAYER_EVENTS,
		LOGGROUP_GAMEPLAYEVENT,
	},
	{
		EventType_VolumeEntered,      
		LOG_VOLUME_EVENTS,
		LOGGROUP_CONTENTEVENT,
	},
	{
		EventType_VolumeExited,      
		LOG_VOLUME_EVENTS,
		LOGGROUP_CONTENTEVENT,
	},
	{
		EventType_ZoneEventRunning,      
		LOG_ZONE_EVENTS,
		LOGGROUP_CONTENTEVENT,
	},
	{
		EventType_ZoneEventState,      
		LOG_ZONE_EVENTS,
		LOGGROUP_CONTENTEVENT,
	},
	{
		EventType_ClickableActive,      
		LOG_CLICKABLE_EVENTS,
		LOGGROUP_CONTENTEVENT,
	},
	{
		EventType_Emote,
		LOG_COMMANDS,
		LOGGROUP_GAMEPLAYEVENT,
	},
	{
		EventType_NemesisState,
		LOG_OTHER_PLAYER_EVENTS,
		LOGGROUP_GAMEPLAYEVENT,
	},
	{
		EventType_BagGetsItem,      
		LOG_ITEMGAINED_EVENTS,
		LOGGROUP_ITEMEVENT,
	},
	{
		EventType_DuelVictory,
		LOG_DUEL_EVENTS,
		LOGGROUP_GAMEPLAYEVENT,
	},
	{
		EventType_MinigameBet,
		LOG_MINIGAME,
		LOGGROUP_REWARDDATA,
	},
	{
		EventType_MinigamePayout,
		LOG_MINIGAME,
		LOGGROUP_REWARDDATA,
	},
	{
		EventType_MinigameJackpot,
		LOG_MINIGAME,
		LOGGROUP_REWARDDATA,
	},
	{
		EventType_PvPQueueMatchResult,
		LOG_QUEUE,
		LOGGROUP_GAMEPLAYEVENT,
	},
	{
		EventType_PvPEvent,
		LOG_QUEUE,
		LOGGROUP_PVP,
	},
	{
		EventType_ItemAssignmentStarted,
		LOG_OTHER_PLAYER_EVENTS,
		LOGGROUP_GAMEPLAYEVENT,
	},
	{
		EventType_ItemAssignmentCompleted,
		LOG_OTHER_PLAYER_EVENTS,
		LOGGROUP_GAMEPLAYEVENT,
	},
	{
		EventType_VideoStarted,
		LOG_CUTSCENE_EVENTS,
		LOGGROUP_CONTENTEVENT,
	},
	{
		EventType_VideoEnded,
		LOG_CUTSCENE_EVENTS,
		LOGGROUP_CONTENTEVENT,
	},
	{
		EventType_PowerTreeStepAdded,
		LOG_POWERTREES,
		LOGGROUP_POWERSDATA,
	},
	{
		EventType_ContestWin,
		LOG_OTHER_PLAYER_EVENTS,
		LOGGROUP_GAMEPLAYEVENT,
	},
	{
		EventType_ScoreboardMetricResult,
		LOG_QUEUE,
		LOGGROUP_GAMEPLAYEVENT,
	},
	{
		EventType_UGCProjectCompleted,
		LOG_MISSION_EVENTS,
		LOGGROUP_CONTENTEVENT,
	},
	{
		EventType_GroupProjectTaskCompleted,
		LOG_MISSION_EVENTS,
		LOGGROUP_GAMEPLAYEVENT,
	},
	{
		EventType_AllegianceSet,
		LOG_MISSION_EVENTS,
		LOGGROUP_GAMEPLAYEVENT,
	},
	{
		EventType_UGCAccountChanged,
		LOG_OTHER_PLAYER_EVENTS,
		LOGGROUP_GAMEPLAYEVENT,
	},
};
STATIC_ASSERT_MESSAGE(ARRAY_SIZE(sLogCategoryDefs) == EventType_Count,"wrong size for defs, probably added enum without adding here");


// The entries in this array need to stay in sync with the LOGGROUP_ enum values!
static bool *spbDisableLoggingCategories[LOGGROUP_COUNT] = 
{
	NULL,
	&gbEnableCombatDamageEventLogging,
	&gbEnableCombatHealingEventLogging,
	&gbEnableCombatKillEventLogging,
	&gbEnableContentEventLogging,
	&gbEnableGamePlayEventLogging,
	&gbEnableItemEventLogging,
	&gbEnablePowersDataLogging,
	&gbEnablePvPLogging,
	&gbEnableRewardDataLogging,
};

bool LoggingIsEnabledForEventType(EventType eType)
{
	if (sLogCategoryDefs[eType].eGroup)
	{
		return *spbDisableLoggingCategories[sLogCategoryDefs[eType].eGroup];
	}

	return false;
}


// ----------------------------------------------------------------------------------
// Static Data
// ----------------------------------------------------------------------------------

static EventTrackerPartitionState **s_eaTrackerPartitionStates;
static EventTrackerPartitionState *s_pPartitionAnyState;

static bool s_bEventSendInProgress = false;

// EArray of callbacks to run when the current Event finishes sending
EventTrackerQueuedCallback **s_eaQueuedEventTrackerCallbacks;

// Unshared copy of globally tracked events
static GameEvent **s_eaUnsharedGlobalTrackedEvents = NULL;

extern U32 g_EventLogDebug;
extern U32 g_EventLogTest;

// A filter for the eventlogdebug print
GameEvent *s_EventLogDebugFilter = NULL;

static enumLogCategory eLogCategoriesForEventTypes[EventType_Count];

MP_DEFINE(EventTrackerObject);
MP_DEFINE(EventListener);
MP_DEFINE(ChainedEventTracker);

static bool eventtracker_MatchEvents(GameEvent *pListeningEvent, GameEvent *pSentEvent, bool bSimpleSourceTarget);
static EventTracker* eventtracker_CreateFromState(EventTrackerPartitionState *pState);

static void eventtracker_FindAndRemoveChainedEventByTracker(EventTracker *tracker);
static void eventtracker_FindAndRemoveChainedEventByTrackerObject(EventTrackerObject *trackerObject);
static void eventtracker_RemoveAndDestroyChainedEvent(EventTrackerPartitionState *pState, ChainedEventTracker *pChainedTracker, GameEvent *ev, S32 chainedEventIdx);


// ----------------------------------------------------------------------------------
// Debug Utility Functions
// ----------------------------------------------------------------------------------

void eventtracker_DebugPrint(GameEvent *ev, int iNumListeners)
{
	if (ev && (!s_EventLogDebugFilter || eventtracker_MatchEvents(s_EventLogDebugFilter, ev, false))) {
		char *estrBuffer = NULL;
		estrStackCreate(&estrBuffer);
		gameevent_WriteEvent(ev, &estrBuffer);
		printf("%s", estrBuffer);
		estrDestroy(&estrBuffer);
		if (iNumListeners > 0) {
			printf("(%d listeners)\n", iNumListeners);
		}
	}
}


// ----------------------------------------------------------------------------------
// Partition State Utility Functions
// ----------------------------------------------------------------------------------

static EventTrackerPartitionState *eventtracker_GetPartitionState(int iPartitionIdx)
{
	if (iPartitionIdx != PARTITION_ANY) {
		EventTrackerPartitionState *pState = eaGet(&s_eaTrackerPartitionStates, iPartitionIdx);
		assertmsgf(pState, "Partition %d does not exist", iPartitionIdx);
		return pState;
	} else {
		return s_pPartitionAnyState;
	}
}


static EventTrackerPartitionState *eventtracker_CreatePartitionState(int iPartitionIdx)
{
	EventTrackerPartitionState *pState;
	int i;

	pState = calloc(1,sizeof(EventTrackerPartitionState));
	pState->iPartitionIdx = iPartitionIdx;
	pState->pGlobalEventTracker = eventtracker_CreateFromState(pState);
	pState->pGlobalEventTracker->eventTypeArray = calloc(EventType_Count, sizeof(TrackedObjectsArray));

	// Copy global unshared events into the partition
	for(i=0; i<eaSize(&s_eaUnsharedGlobalTrackedEvents); ++i) {
		GameEvent *pCopy = StructClone(parse_GameEvent, s_eaUnsharedGlobalTrackedEvents[i]);
		pCopy->iPartitionIdx = iPartitionIdx;
		eaPush(&pState->eaGlobalTrackedEvents, pCopy);
	}

	return pState;
}


static void eventtracker_DestroyPartitionState(EventTrackerPartitionState *pState)
{
	// Assumes that global tracking was already stopped
	assert(!pState->bGlobalEventLogTracking);

	// Clear our log
	stashTableDestroySafe(&pState->pGlobalEventLog);

	// Clear out trackers
	eaDestroy(&pState->eaAllTrackers); // Creator of Tracker should destroy it, so not destroyed here
	pState->pGlobalEventTracker = NULL; // Was destroyed on previous line

	// Clear out global events
	eaDestroyStruct(&pState->eaGlobalTrackedEvents, parse_GameEvent);

	free(pState);
}


// ----------------------------------------------------------------------------------
// Object Utility Functions
// ----------------------------------------------------------------------------------

static EventListener* eventtracker_ObjectFindListener(EventTrackerObject *pObject, void *pStructPtr)
{
	if (pObject) {
		int i, n = eaSize(&pObject->listeners);
		for (i = 0; i < n; i++) {
			EventListener *pListener = pObject->listeners[i];
			if (pListener && pListener->structPtr == pStructPtr) {
				return pListener;
			}
		}
	}
	return NULL;
}


static void eventtracker_ObjectRemoveListener(EventTrackerObject *pObject, void *pStructPtr)
{
	if (pObject) {
		bool bEmpty = true;
		int i, n = eaSize(&pObject->listeners);

		// Remove all matching Listeners from the EventTrackerObject
		for (i = 0; i < n; i++){
			if (pObject->listeners[i]) {
				if (pObject->listeners[i]->structPtr == pStructPtr) {
					MP_FREE(EventListener, pObject->listeners[i]);
					pObject->listeners[i] = NULL;
				} else {
					bEmpty = false;
				}
			}
		}

		// If this EventTrackerObject is now empty, NULL out the Event
		// so that the EventTrackerObject can get cleaned up later
		if (bEmpty){
			pObject->ev = NULL;
		}
	}
}


static void eventtracker_ObjectDestroy(EventTrackerObject *pObject)
{
	int iListener;
	int inumListeners = eaSize(&pObject->listeners);
	for (iListener = inumListeners-1; iListener >= 0; iListener--) {
		EventListener *pListener = pObject->listeners[iListener];
		if (pListener) {
			MP_FREE(EventListener, pListener);
		}
	}
	eaDestroy(&pObject->listeners);
	MP_FREE(EventTrackerObject, pObject);
}


// ----------------------------------------------------------------------------------
// Logging Functions
// ----------------------------------------------------------------------------------

static __forceinline enumLogCategory eventtracker_LogTypeFromEventType(EventType eType)
{
	return eLogCategoriesForEventTypes[eType];
}

static void eventtracker_LogEvent(GameEvent *ev)
{
	// Log the Event
	// The Event must be logged once for each entity involved to get all the EntLog information.
	if (ev) {
		int i, n;
		static char* estrEvent = NULL;

		PERFINFO_AUTO_START_FUNC();

		if (!LoggingIsEnabledForEventType(ev->type))
		{
			PERFINFO_AUTO_STOP();
			return;
		}

		gameevent_WriteEventEscapedFaster(ev, &estrEvent);

		if (g_EventLogTest) {
			static char *estrA;
			static char *estrTest;

			GameEvent *pevA;
			GameEvent *pevB;

			pevA = StructCreate(parse_GameEvent);
			gameevent_WriteEventEscaped(ev, &estrA);
			estrClear(&estrTest);
			estrAppendUnescaped(&estrTest, estrA);
			ParserReadText(estrTest, parse_GameEvent, pevA, 0);

			pevB = StructCreate(parse_GameEvent);
			// Did gameevent_WriteEventEscapedFaster(...) above already
			estrClear(&estrTest);
			estrAppendUnescaped(&estrTest, estrEvent);
			ParserReadText(estrTest, parse_GameEvent, pevB, 0);

			if(StructCompare(parse_GameEvent, pevA, pevB, 0, 0, 0)!=0)
			{
				Errorf("Event logs are different, and they shouldn't be. Report to poz!\nold:[%s]\nnew:[%s]\n", estrA, estrEvent);
			}

			StructDestroy(parse_GameEvent, pevA);
			StructDestroy(parse_GameEvent, pevB);
		}

		// This flag should be TRUE the first time the Event is logged
		ev->bUnique = true;

		// Log once for each Source and each Target
		n = eaSize(&ev->eaSources);
		for (i = 0; i < n; i++){
			if (ev->eaSources[i]->pEnt){
				entLog(eventtracker_LogTypeFromEventType(ev->type), ev->eaSources[i]->pEnt, ev->bUnique ? "EventSource" : "EventSourceDup", FORMAT_OK(ev->bUnique ? "%s" : " %s"), ev->bUnique ? estrEvent : StaticDefineIntRevLookup(EventTypeEnum, ev->type));
				ev->bUnique = false;
			}
		}

		n = eaSize(&ev->eaTargets);
		for (i = 0; i < n; i++){
			if (ev->eaTargets[i]->pEnt){
				entLog(eventtracker_LogTypeFromEventType(ev->type), ev->eaTargets[i]->pEnt, ev->bUnique ? "EventTarget" : "EventTargetDup", FORMAT_OK(ev->bUnique ? "%s" : " %s"), ev->bUnique ? estrEvent : StaticDefineIntRevLookup(EventTypeEnum, ev->type));
				ev->bUnique = false;
			}
		}

		// If there were no Sources or Targets, log as an ObjectLog
		if (ev->bUnique){
			objLog(eventtracker_LogTypeFromEventType(ev->type), GLOBALTYPE_NONE, 0, 0, NULL, NULL, NULL, "Event", NULL, "%s", estrEvent);
			ev->bUnique = false;
		}

		estrClear(&estrEvent);

		PERFINFO_AUTO_STOP();
	}
}


// ----------------------------------------------------------------------------------
// Event Utility Functions
// ----------------------------------------------------------------------------------

// Currently Events are only considered to be the same if the pointer is the same.
// TODO - do some more careful match (structcompare?) so that there aren't duplicate
// events.  Should improve performance slightly.
static EventTrackerObject* eventtracker_FindEvent(EventTracker *pTracker, GameEvent *ev)
{
	if (ev && pTracker->eventTypeArray) {
		TrackedObjectsArray trackedObjects = pTracker->eventTypeArray[ev->type];
		int i, n = eaSize(&trackedObjects);
		for(i = 0; i < n; i++) {
			if (trackedObjects[i]->ev == ev){
				return trackedObjects[i];
			}
		}
	}
	return NULL;
}


// This is a helper function that adds a GameEvent to a Tracked Events earray, with some error checking
bool eventtracker_AddNamedEventToList(GameEvent ***peaTrackedEvents, GameEvent *pEvent, const char *pcErrorFilename)
{
	int i;
	if (pEvent) {
		if (!pEvent->pchEventName){
			// Should never happen unless there's a programmer error
			ErrorFilenamef(pcErrorFilename, "Error: EventCount Event with no name?");
			return false;
		}

		// Make sure there are no duplicate names
		for (i = 0; i < eaSize(peaTrackedEvents); i++){
			GameEvent *pOther = (*peaTrackedEvents)[i];
			if (pOther && pOther->pchEventName == pEvent->pchEventName){
				// Duplicate Event name
				if (StructCompare(parse_GameEvent, pEvent, pOther, 0, 0, 0) != 0){
					// Throw an error if the duplicates don't match (should never happen)
					ErrorFilenamef(pcErrorFilename, "Error: Multiple different Events found sharing name '%s'", pEvent->pchEventName);
				}
				return false;
			}
		}
		
		// Not a duplicate, add to array
		eaPush(peaTrackedEvents, pEvent);
		return true;
	}
	return false;
}


static __forceinline void eventtracker_GetTrackersForEventParticipant(S32 iPartitionIdx, GameEventParticipant *pParticipant, EventTracker ***peaTrackerList)
{
	if (pParticipant) {
		OldStaticEncounter *pStaticEnc = (pParticipant->pEncounter?GET_REF(pParticipant->pEncounter->staticEnc):0);
		GameEncounter *pEncounter = pParticipant->pEncounter2;
		const Entity *pEnt = pParticipant->pEnt;

		if (!pEnt && pParticipant->entRef) {
			pEnt = entFromEntityRef(iPartitionIdx, pParticipant->entRef);
		}

		if (!pEncounter && eaSize(&pParticipant->eaStaticEncScopeNames) > 0) {
			pEncounter = encounter_GetByName(pParticipant->eaStaticEncScopeNames[0]->name, pParticipant->eaStaticEncScopeNames[0]->scope);
		}
		if (gConf.bAllowOldEncounterData && !pStaticEnc && eaSize(&pParticipant->eaStaticEncScopeNames) > 0) {
			pStaticEnc = oldencounter_StaticEncounterFromName(pParticipant->eaStaticEncScopeNames[0]->name);
		}

		// Player's event tracker
		if (pEnt) {
			MissionInfo *pInfo = mission_GetInfoFromPlayer(pEnt);
			if (pInfo && pInfo->eventTracker) {
				eaPushUnique(peaTrackerList, pInfo->eventTracker);
			}
		}

		// Team members' trackers
		if (pParticipant->pTeam) {
			const Team *pTeam = pParticipant->pTeam;
			int i, n = eaSize(&pTeam->eaMembers);
			for (i = 0; i < n; i++)
			{
				TeamMember *pMember = pTeam->eaMembers[i];
				Entity *pCurrEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pMember->iEntID);
				if (pCurrEnt) {
					MissionInfo *pInfo = mission_GetInfoFromPlayer(pCurrEnt);
					if (pInfo && pInfo->eventTracker) {
						eaPushUnique(peaTrackerList, pInfo->eventTracker);
					}
				}
			}
		}

		// Encounter's event tracker
		if (pEncounter) {
			GameEncounterPartitionState *pState = encounter_GetPartitionStateIfExists(iPartitionIdx, pEncounter);
			if (pState && pState->eventData.pEventTracker) {
				eaPushUnique(peaTrackerList, pState->eventData.pEventTracker);
			}
		}
		if (pStaticEnc && gConf.bAllowOldEncounterData) {
			OldEncounter *pOldEncounter = oldencounter_FromStaticEncounterIfExists(iPartitionIdx, pStaticEnc);
			if (pOldEncounter) {
				eaPushUnique(peaTrackerList, pOldEncounter->eventTracker);
			}
		}
	}
}


// Finds all the possible custom EventTrackers that the given Event could use.
// Doesn't include pGlobalEventTracker, which anything can use.
// An Event is always sent to *all* possible trackers.
static __forceinline void eventtracker_GetTrackersForEvent(GameEvent *ev, EventTracker*** peaTrackerList)
{
	int i;

	PERFINFO_AUTO_START_FUNC();

	if (ev) {
		OldStaticEncounter *pSourceStaticEnc = NULL;
		OldStaticEncounter *pTargetStaticEnc = NULL;
		GameEncounter *pSourceGameEnc = NULL;
		GameEncounter *pTargetGameEnc = NULL;
		Entity *pSourceEnt = NULL;
		Entity *pTargetEnt = NULL;

		// Get source and target ent
		if (ev->sourceEntRef) {
			pSourceEnt = entFromEntityRef(ev->iPartitionIdx, ev->sourceEntRef);
		}
		if (ev->targetEntRef) {
			pTargetEnt = entFromEntityRef(ev->iPartitionIdx, ev->targetEntRef);
		}

		// Get source and target encounter
		if (ev->pchSourceStaticEncName) {
			pSourceGameEnc = encounter_GetByName(ev->pchSourceStaticEncName, NULL);
			if (gConf.bAllowOldEncounterData && !pSourceGameEnc) {
				pSourceStaticEnc = oldencounter_StaticEncounterFromName(ev->pchSourceStaticEncName);
			}
		}
		if (ev->pchTargetStaticEncName) {
			pTargetGameEnc = encounter_GetByName(ev->pchTargetStaticEncName, NULL);
			if (gConf.bAllowOldEncounterData && !pTargetGameEnc) {
				pTargetStaticEnc = oldencounter_StaticEncounterFromName(ev->pchTargetStaticEncName);
			}
		}

		// Source player's event tracker
		if (pSourceEnt && (ev->tMatchSource == TriState_Yes || ev->tMatchSourceTeam == TriState_Yes)) {
			MissionInfo *pInfo = mission_GetInfoFromPlayer(pSourceEnt);
			if (pInfo && pInfo->eventTracker) {
				eaPushUnique(peaTrackerList, pInfo->eventTracker);
			}
		}
		
		// Target player's event tracker
		if (pTargetEnt && (ev->tMatchTarget == TriState_Yes || ev->tMatchTargetTeam == TriState_Yes)) {
			MissionInfo *pInfo = mission_GetInfoFromPlayer(pTargetEnt);
			if (pInfo && pInfo->eventTracker) {
				eaPushUnique(peaTrackerList, pInfo->eventTracker);
			}
		}

		// Source encounter's event tracker
		if (pSourceGameEnc) {
			GameEncounterPartitionState *pState = encounter_GetPartitionStateIfExists(ev->iPartitionIdx, pSourceGameEnc);
			if (pState && pState->eventData.pEventTracker) {
				eaPushUnique(peaTrackerList, pState->eventData.pEventTracker);
			}
		}

		// Target encounter's event tracker
		if (pTargetGameEnc) {
			GameEncounterPartitionState *pState = encounter_GetPartitionStateIfExists(ev->iPartitionIdx, pTargetGameEnc);
			if (pState && pState->eventData.pEventTracker) {
				eaPushUnique(peaTrackerList, pState->eventData.pEventTracker);
			}
		}

		if (gConf.bAllowOldEncounterData) {
			// Source encounter's event tracker
			if (pSourceStaticEnc) {
				OldEncounter *pOldEncounter = oldencounter_FromStaticEncounterIfExists(ev->iPartitionIdx, pSourceStaticEnc);
				if (pOldEncounter) {
					eaPushUnique(peaTrackerList, pOldEncounter->eventTracker);
				}
			}
			// Target encounter's event tracker
			if (pTargetStaticEnc) {
				OldEncounter *pOldEncounter = oldencounter_FromStaticEncounterIfExists(ev->iPartitionIdx, pTargetStaticEnc);
				if (pOldEncounter) {
					eaPushUnique(peaTrackerList, pOldEncounter->eventTracker);
				}
			}
		}

		// Get all Trackers from the Source and Target participants
		for (i = 0; i < eaSize(&ev->eaSources); i++){
			eventtracker_GetTrackersForEventParticipant(ev->iPartitionIdx, ev->eaSources[i], peaTrackerList);
		}
		for (i = 0; i < eaSize(&ev->eaTargets); i++){
			eventtracker_GetTrackersForEventParticipant(ev->iPartitionIdx, ev->eaTargets[i], peaTrackerList);
		}
	}

	PERFINFO_AUTO_STOP();
}


static EventTracker* eventtracker_FindBestTracker(GameEvent *ev)
{
	EventTracker *bestTracker = NULL;

	if (ev) {
		// Source player's event tracker
		if (ev->tMatchSource == TriState_Yes || ev->tMatchSourceTeam == TriState_Yes) {
			Entity *pEnt = entFromEntityRef(ev->iPartitionIdx, ev->sourceEntRef);
			MissionInfo *pInfo = SAFE_MEMBER2(pEnt, pPlayer, missionInfo);
			if (pInfo && pInfo->eventTracker){
				return pInfo->eventTracker;
			}
		}
		
		// Target player's event tracker
		if (ev->tMatchTarget == TriState_Yes || ev->tMatchTargetTeam == TriState_Yes) {
			Entity *pEnt = entFromEntityRef(ev->iPartitionIdx, ev->targetEntRef);
			MissionInfo *pInfo = SAFE_MEMBER2(pEnt, pPlayer, missionInfo);
			if (pInfo && pInfo->eventTracker){
				return pInfo->eventTracker;
			}
		}

		// Source encounter's event tracker
		if (ev->pchSourceStaticEncName) {
			GameEncounter *pEncounter = encounter_GetByName(ev->pchSourceStaticEncName, NULL);
			if (pEncounter) {
				GameEncounterPartitionState *pState = encounter_GetPartitionStateIfExists(ev->iPartitionIdx, pEncounter);
				if (pState && pState->eventData.pEventTracker) {
					return pState->eventData.pEventTracker;
				}
			} else if (gConf.bAllowOldEncounterData) {
				OldEncounter *pOldEncounter = oldencounter_FromStaticEncounterNameIfExists(ev->iPartitionIdx, ev->pchSourceStaticEncName);
				if (pOldEncounter && pOldEncounter->eventTracker) {
					return pOldEncounter->eventTracker;
				}
			}
		}

		// Target encounter's event tracker
		if (ev->pchTargetStaticEncName){
			GameEncounter *pEncounter = encounter_GetByName(ev->pchTargetStaticEncName, NULL);
			if (pEncounter) {
				GameEncounterPartitionState *pState = encounter_GetPartitionStateIfExists(ev->iPartitionIdx, pEncounter);
				if (pState && pState->eventData.pEventTracker) {
					return pState->eventData.pEventTracker;
				}
			} else if (gConf.bAllowOldEncounterData) {
				OldEncounter *pOldEncounter = oldencounter_FromStaticEncounterNameIfExists(ev->iPartitionIdx, ev->pchTargetStaticEncName);
				if (pOldEncounter && pOldEncounter->eventTracker) {
					return pOldEncounter->eventTracker;
				}
			}
		}
	}

	// Default to global event tracker
	if (!bestTracker) {
		EventTrackerPartitionState *pState = eventtracker_GetPartitionState(ev->iPartitionIdx);
		bestTracker = pState->pGlobalEventTracker;
	}

	return bestTracker;
}


// ----------------------------------------------------------------------------------
// Tracker Life Cycle Functions
// ----------------------------------------------------------------------------------

static EventTracker* eventtracker_CreateFromState(EventTrackerPartitionState *pState)
{
	EventTracker *pTracker = calloc(1, sizeof(EventTracker));
	pTracker->iPartitionIdx = pState->iPartitionIdx;
	eaPush(&pState->eaAllTrackers, pTracker);
    return pTracker;
}


EventTracker* eventtracker_Create(int iPartitionIdx)
{
	EventTrackerPartitionState *pState = eventtracker_GetPartitionState(iPartitionIdx);
	EventTracker *pTracker = calloc(1, sizeof(EventTracker));
	pTracker->iPartitionIdx = iPartitionIdx;
	eaPush(&pState->eaAllTrackers, pTracker);
    return pTracker;
}


// TODO(MK): It may not be necessary anymore to move event tracker objects to the global event tracker,
// but I'm doing it anyway under specific circumstances just to be safe
void eventtracker_Destroy(EventTracker *pTracker, bool bMoveObjectsToGlobalEventTracker)
{
	EventTrackerPartitionState *pState;
	if (!pTracker) {
		return;
	}

	pState = eaGet(&s_eaTrackerPartitionStates, pTracker->iPartitionIdx);
	if (!pState) {
		return; // It is possible to destroy a tracker after the partition is gone
	}

	if (pTracker->eventTypeArray) {
		int i, n = EventType_Count;
		for (i = 0; i < n; i++) {
			if (bMoveObjectsToGlobalEventTracker) {
				// If there are still things in this eventtracker, they should get moved to the global eventtracker
				if (pTracker != pState->pGlobalEventTracker) {
					int j, m = eaSize(&pTracker->eventTypeArray[i]);
					for (j = m-1; j >= 0; --j) {
						EventTrackerObject *pObject = eaRemove(&pTracker->eventTypeArray[i], j);

						eaPush(&pState->pGlobalEventTracker->eventTypeArray[i], pObject);
					}
				}
			}
			eaDestroyEx(&pTracker->eventTypeArray[i], eventtracker_ObjectDestroy);
		}
		free(pTracker->eventTypeArray);
	}

	eaFindAndRemove(&pState->eaAllTrackers, pTracker);

	eventtracker_FindAndRemoveChainedEventByTracker(pTracker);

	free(pTracker);
}


static void eventtracker_Compact(EventTracker *pTracker)
{
	if (!pTracker) {
		return;
	}

	if (pTracker->eventTypeArray) {
		int i, n = EventType_Count;
		for (i = 0; i < n; i++) {
			int j, m = eaSize(&pTracker->eventTypeArray[i]);
			for (j = m-1; j >= 0; --j) {
				if (!pTracker->eventTypeArray[i][j]->ev){
					EventTrackerObject *pObject = eaRemoveFast(&pTracker->eventTypeArray[i], j);
					eventtracker_ObjectDestroy(pObject);
				}
			}
		}
	}
	pTracker->uNumInvalid = 0;
}


static GameEvent* eventtracker_StartTrackingInternal(	GameEvent *ev, WorldScope *pScope, void *pStructPtr, 
														EventTrackerUpdateCountFunc updateFunc, EventTrackerSetCountFunc setFunc, 
														bool bCheckDuplicates, EventTracker *trackerOverride, 
														EventTrackerObject **pOutTracker )
{
	EventTracker *pTracker = NULL;

	if (pOutTracker) 
		*pOutTracker = NULL;

	if (!ev) {
		return NULL;
	}

	PERFINFO_AUTO_START_FUNC();

	if (ev->pchMapName){ 
		// Ignore events that aren't for this map
		const char *pcMapPublicName = zmapInfoGetPublicName(NULL);
		if (ev->pchMapName != pcMapPublicName){
			PERFINFO_AUTO_STOP();
			return NULL;
		}
	}

	if (ev->pchDoorKey) {
		// Ignore events that don't match this Door Key
		MapVariable *pMapVariable = mapvariable_GetByName(ev->iPartitionIdx, ITEM_DOOR_KEY_MAP_VAR);
		if (pMapVariable && pMapVariable->pVariable && pMapVariable->pVariable->pcStringVal 
					&& stricmp(pMapVariable->pVariable->pcStringVal, ev->pchDoorKey) != 0){
			PERFINFO_AUTO_STOP();
			return NULL;
		}
	}

	if(ev->tMatchMapOwner == TriState_Yes) {
		// Ignore events that don't match this map owner
		Entity *pMapOwnerEnt = entFromEntityRef(ev->iPartitionIdx, ev->mapOwnerEntRef);
		if (!zmapInfoGetMapType(NULL) == ZMTYPE_OWNED) {
			PERFINFO_AUTO_STOP();
			return NULL;	// Not an owned map, so there will never be an owner
		}
		if (!pMapOwnerEnt || pMapOwnerEnt->myContainerID != partition_OwnerIDFromIdx(ev->iPartitionIdx)){
			PERFINFO_AUTO_STOP();
			return NULL;
		}
	} else if (ev->tMatchMapOwner == TriState_No){
		if (zmapInfoGetMapType(NULL)==ZMTYPE_OWNED) {
			Entity *pMapOwnerEnt = entFromEntityRef(ev->iPartitionIdx, ev->mapOwnerEntRef);
			if (pMapOwnerEnt && pMapOwnerEnt->myContainerID == partition_OwnerIDFromIdx(ev->iPartitionIdx)){
				PERFINFO_AUTO_STOP();
				return NULL;
			}
		}
	}

	if (ev->pScope != pScope) {
		ev->pScope = pScope;
	}

	if (!trackerOverride) {
		pTracker = eventtracker_FindBestTracker(ev);
	} else {
		pTracker = trackerOverride;
	}

	if (pTracker) {
		EventTrackerObject *pTrackedObject = NULL;
		
		if (bCheckDuplicates){
			pTrackedObject = eventtracker_FindEvent(pTracker, ev);
		}

		if (!pTrackedObject) {
			// Create tracker eventTypeArray if required
			if (!pTracker->eventTypeArray) {
				pTracker->eventTypeArray = calloc(EventType_Count, sizeof(TrackedObjectsArray));
			}
			// Pre-populate the event array for the current type if not already there
			if (!pTracker->eventTypeArray[ev->type]) {
				eaSetCapacity(&(pTracker->eventTypeArray[ev->type]), 32);
			}
			pTrackedObject = MP_ALLOC(EventTrackerObject);
			pTrackedObject->ev = ev;
			pTrackedObject->listeners = NULL;
			eaPush(&pTracker->eventTypeArray[ev->type], pTrackedObject);
		}

		if (pOutTracker) 
			*pOutTracker = pTrackedObject;

		if (!eventtracker_ObjectFindListener(pTrackedObject, pStructPtr)) {
			EventListener *pListener = MP_ALLOC(EventListener);
			pListener->structPtr = pStructPtr;
			pListener->updateFunc = updateFunc;
			pListener->setFunc = setFunc;
			eaPush(&pTrackedObject->listeners, pListener);
		}
		PERFINFO_AUTO_STOP();
		return pTrackedObject->ev;
	}

	PERFINFO_AUTO_STOP();
	return NULL;
}

GameEvent* eventtracker_StartTrackingEx(GameEvent *ev, WorldScope *scope, void* structPtr, 
										EventTrackerUpdateCountFunc updateFunc, EventTrackerSetCountFunc setFunc, 
										bool bCheckDuplicates)
{
	return eventtracker_StartTrackingInternal(ev, scope, structPtr, updateFunc, setFunc, bCheckDuplicates, NULL, NULL);
}


static void RemoveThisObjectAsAListener(EventTrackerPartitionState *pState, 
										EventTracker *tracker, EventTrackerObject *trackedObject, 
										void* structPtr, int eventArrayIndex)
{
	GameEvent *ev = trackedObject->ev;
	devassert(ev);
	// Remove this object as a listener
	eventtracker_ObjectRemoveListener(trackedObject, structPtr);
	if (!trackedObject->ev){
		if (s_bEventSendInProgress){
			tracker->uNumInvalid++;
			pState->bCompactTrackers = true;
		} else {
			if (eventArrayIndex != -1)
			{
				devassert(tracker->eventTypeArray[ev->type][eventArrayIndex] == trackedObject);
				eaRemoveFast(&tracker->eventTypeArray[ev->type], eventArrayIndex);
			}
			else
			{
				eaFindAndRemove(&tracker->eventTypeArray[ev->type], trackedObject);
			}
			eventtracker_ObjectDestroy(trackedObject);
		}
	}
}

static void eventtracker_stopTrackingForChainedEvents(EventTrackerPartitionState *pState, GameEvent *ev, void *pStructPtr)
{
	S32 i;
	// Go through the chained events and see if this event matches any of the chained event's root event
	// and if it does, remove and destroy the chained event
	for (i = eaSize(&pState->eaChainedEventTrackers) -1; i >= 0; --i){
		ChainedEventTracker *pChained = pState->eaChainedEventTrackers[i];
		if (pChained->pTrackedObject->ev->pRootEvent == ev) {
			eventtracker_RemoveAndDestroyChainedEvent(pState, pChained, NULL, i);
		}
	}
}

void eventtracker_StopTracking(int iPartitionIdx, GameEvent *ev, void *pStructPtr)
{
	static EventTracker **eaTrackerList = NULL;
	int i, j, n;
	EventTrackerPartitionState *pState;

	// Handle case where partition is done and entity is being cleaned up
	if (iPartitionIdx == PARTITION_ENT_BEING_DESTROYED) {
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	assert(ev && (ev->iPartitionIdx == iPartitionIdx)); // Safety check

	// Find all the custom trackers the event could be in
	eventtracker_GetTrackersForEvent(ev, &eaTrackerList);

	// Add the global tracker
	pState = eventtracker_GetPartitionState(iPartitionIdx);
	eaPush(&eaTrackerList, pState->pGlobalEventTracker);

	// For each tracker, try to stop tracking the event
	n = eaSize(&eaTrackerList);
	for (i = 0; i < n; ++i) {
		EventTracker *pTracker = eaTrackerList[i];
		if (pTracker && ev) {
			if (pTracker->eventTypeArray) {
				for (j = eaSize(&pTracker->eventTypeArray[ev->type])-1; j >= 0; --j) {
					EventTrackerObject *pTrackedObject = pTracker->eventTypeArray[ev->type][j];
					if (pTrackedObject && pTrackedObject->ev == ev) {
						// Remove this object as a listener
						RemoveThisObjectAsAListener(pState, pTracker, pTrackedObject, pStructPtr, j);
					}
				}
			}
		}
	}

	if (iPartitionIdx == PARTITION_ANY)
	{
		eventtracker_stopTrackingForChainedEvents(pState, ev, pStructPtr);
		FOR_EACH_IN_EARRAY(s_eaTrackerPartitionStates, EventTrackerPartitionState, pOtherState)
			if(pOtherState)
				eventtracker_stopTrackingForChainedEvents(pOtherState, ev, pStructPtr);
		FOR_EACH_END
	}
	else
	{
		eventtracker_stopTrackingForChainedEvents(pState, ev, pStructPtr);
	}
	
	
	eaClearFast(&eaTrackerList);

	PERFINFO_AUTO_STOP_FUNC();
}

static void eventtracker_StopTrackingAllForListenerChainedEvents(EventTrackerPartitionState *pState, void *pStructPtr)
{
	S32 i;
	// Go through the chained events and see if this event matches any of the chained event's root event
	// and if it does, remove and destroy the chained event
	for (i = eaSize(&pState->eaChainedEventTrackers) -1; i >= 0; --i) {
		ChainedEventTracker *pChained = pState->eaChainedEventTrackers[i];
		EventListener *listener = eaGet(&pChained->pTrackedObject->listeners, 0);

		if (listener->structPtr == pStructPtr) {
			eventtracker_RemoveAndDestroyChainedEvent(pState, pChained, NULL, i);
		}
	}
}

void eventtracker_StopTrackingAllForListener(int iPartitionIdx, void *pStructPtr)
{
	EventTrackerPartitionState *pState = eventtracker_GetPartitionState(iPartitionIdx);
	int iTracker, iEventType, iEvent;

	// For each tracker, try to stop tracking the event
	for(iTracker=eaSize(&pState->eaAllTrackers)-1; iTracker>=0; --iTracker) {

		EventTracker *pTracker = pState->eaAllTrackers[iTracker];
	
		if (pTracker && pTracker->eventTypeArray) {
			for (iEventType = EventType_Count-1; iEventType >= 0; --iEventType) {
				for (iEvent = eaSize(&pTracker->eventTypeArray[iEventType])-1; iEvent >= 0; --iEvent){
					EventTrackerObject *pTrackedObject = pTracker->eventTypeArray[iEventType][iEvent];

					if (pTrackedObject){
						eventtracker_ObjectRemoveListener(pTrackedObject, pStructPtr);
						if (!pTrackedObject->ev){
							if (s_bEventSendInProgress){
								pTracker->uNumInvalid++;
								pState->bCompactTrackers = true;
							} else {
								eaRemoveFast(&pTracker->eventTypeArray[iEventType], iEvent);
								eventtracker_ObjectDestroy(pTrackedObject);
							}
						}
					}
				}
			}
		}
	}

	if (iPartitionIdx == PARTITION_ANY)
	{
		eventtracker_StopTrackingAllForListenerChainedEvents(pState, pStructPtr);
		FOR_EACH_IN_EARRAY(s_eaTrackerPartitionStates, EventTrackerPartitionState, pOtherState)
			if(pOtherState)
				eventtracker_StopTrackingAllForListenerChainedEvents(pOtherState, pStructPtr);
		FOR_EACH_END
	}
	else
	{
		eventtracker_StopTrackingAllForListenerChainedEvents(pState, pStructPtr);
	}

	
}


// ----------------------------------------------------------------------------------
// Event Matching Functions
// ----------------------------------------------------------------------------------

#define eventtracker_FieldMatches(a)				eventtracker_FieldMatches2(pListeningEvent->a, pSentEvent->a)
#define eventtracker_MatchesPooled(a)				((!pListeningEvent->a)||(pListeningEvent->a == pSentEvent->a))
// -1 implies wildcard
#define eventtracker_FieldMatchesInt(a)				(pListeningEvent->a==-1 || pListeningEvent->a==pSentEvent->a)

#define eventtracker_FieldMatches2(a, b)			((!a)||((b) && !stricmp((a),(b))))
#define eventtracker_FieldMatchesPooled2(a, b)		((!a)||(a == b))
// -1 implies wildcard
#define eventtracker_FieldMatchesInt2(a, b)			(a==-1 || a==b)
#define eventtracker_FieldMatchesTriStateBool(a, b) ((a == TriState_DontCare)||((a == TriState_Yes) == b))


static bool eventtracker_FieldMatchesScoped(WorldScope *pListeningScope, const char *pcListeningField, WorldScopeNamePair **eaScopeNames)
{
	int i;

	if (eaSize(&eaScopeNames) == 0) {
		return eventtracker_FieldMatches2(pcListeningField, NULL);
	}

	for (i = 0; i < eaSize(&eaScopeNames); i++) {
		WorldScopeNamePair *pPair = eaScopeNames[i];
		if (pPair->scope == pListeningScope || !pListeningScope) {
			if(eventtracker_FieldMatches2(pcListeningField, pPair->name))
				return true;
		}
	}

	return false;
}


static bool eventtracker_MatchVolume(GameEvent *pListeningEvent, GameEvent *pSentEvent)
{
	// Early out if listening event doesn't care
	if (!pListeningEvent->pchVolumeName) {
		return true;
	}

	if (pSentEvent->type == EventType_VolumeEntered ||
		pSentEvent->type == EventType_VolumeExited ||
		pSentEvent->type == EventType_InteractBegin ||
		pSentEvent->type == EventType_InteractEndActive ||
		pSentEvent->type == EventType_InteractFailure ||
		pSentEvent->type == EventType_InteractInterrupted ||
		pSentEvent->type == EventType_InteractSuccess){

		// For Volume and Interact events, do a scoped match on the volume name field
		return eventtracker_FieldMatchesScoped(pListeningEvent->pScope, pListeningEvent->pchVolumeName, pSentEvent->eaVolumeScopeNames);
		
	} else if (pSentEvent->type == EventType_Kills ||
			   pSentEvent->type == EventType_Assists) {
		
		// For Kill Events, just check the target ent
		GameEventParticipant *pEntInfo = eaGet(&pSentEvent->eaTargets, 0);
		if (pEntInfo && pEntInfo->pEnt) {
			return volume_IsEntityInVolumeByName(pEntInfo->pEnt, pListeningEvent->pchVolumeName, pListeningEvent->pScope);
		}
		return false;

	} else {
		// Default: At least one Source or Target entity must be in the volume.
		int i;
		for (i = eaSize(&pSentEvent->eaSources)-1; i>=0; --i) {
			GameEventParticipant *pEntInfo = pSentEvent->eaSources[i];
			if (pEntInfo->pEnt && pEntInfo->bHasCredit && volume_IsEntityInVolumeByName(pEntInfo->pEnt, pListeningEvent->pchVolumeName, pListeningEvent->pScope)){
				return true;
			}
		}

		for (i = eaSize(&pSentEvent->eaTargets)-1; i>=0; --i) {
			GameEventParticipant *pEntInfo = pSentEvent->eaTargets[i];
			if (pEntInfo->pEnt && pEntInfo->bHasCredit && volume_IsEntityInVolumeByName(pEntInfo->pEnt, pListeningEvent->pchVolumeName, pListeningEvent->pScope)){
				return true;
			}
		}
			
		return false;
	}
}

// returns true if the pSentEventTags has all the given piListeningEventTags
static __forceinline bool eventtracker_HasTags(S32 *piListeningEventTags, S32 *piTriggeringEventTags)
{
	S32 iListeningSize = eaiSize(&piListeningEventTags);
	
	if (iListeningSize)
	{
		S32 iSentSize = eaiSize(&piTriggeringEventTags);
		if (iListeningSize <= iSentSize)
		{
			S32 i;
			for (i = iListeningSize-1; i >= 0; --i)
			{
				if (eaiFind(&piTriggeringEventTags, piListeningEventTags[i]) < 0)
					return false;
			}

			return true;
		}
		
		return false;
	}

	return true;
}


// Returns TRUE if the Listening event matches any Source entity in the Sent event
static bool eventtracker_MatchSource(GameEvent *pListeningEvent, GameEvent *pSentEvent)
{
	int i, n;
	bool bCaresAboutSource = false;

	// See if we care about the source entity at all
	if (pListeningEvent->pchSourceActorName
	 || pListeningEvent->pchSourceCritterName
	 || pListeningEvent->pchSourceCritterGroupName
	 || pListeningEvent->pchSourceEncounterName
	 || pListeningEvent->pchSourceStaticEncName
	 || pListeningEvent->pchSourceStaticEncExclude
	 || pListeningEvent->pchSourceEncGroupName
	 || pListeningEvent->pchSourceObjectName
	 || pListeningEvent->pchSourceFactionName
	 || pListeningEvent->pchSourceAllegianceName
	 || pListeningEvent->pchSourceRank
	 || pListeningEvent->pchSourceClassName
	 || pListeningEvent->pchSourcePowerMode
	 || pListeningEvent->eSourceRegionType != -1
	 || pListeningEvent->tMatchSource != TriState_DontCare
	 || pListeningEvent->tMatchSourceTeam != TriState_DontCare
	 || pListeningEvent->tSourceIsPlayer != TriState_DontCare
	 || eaiSize(&pListeningEvent->piSourceCritterTags))
	{
		bCaresAboutSource = true;
	}

	// Early out if don't care about the source
	if (!bCaresAboutSource) {
		return true;
	}

	// Return TRUE if at least one EntInfo is a match
	n = eaSize(&pSentEvent->eaSources);
	for (i = 0; i < n; i++){
		GameEventParticipant *pEntInfo = pSentEvent->eaSources[i];

		if (!pEntInfo->bHasCredit && !(pEntInfo->bHasTeamCredit && pSentEvent->bUseComplexTeamMatchingSource && pListeningEvent->tMatchSourceTeam == TriState_Yes)) continue;
		if (!eventtracker_FieldMatchesPooled2(pListeningEvent->pchSourceActorName, pEntInfo->pchActorName)) continue;
		if (!eventtracker_FieldMatchesPooled2(pListeningEvent->pchSourceCritterName, pEntInfo->pchCritterName)) continue;
		if (!eventtracker_FieldMatchesPooled2(pListeningEvent->pchSourceCritterGroupName, pEntInfo->pchCritterGroupName)) continue;
		if (!eventtracker_HasTags(pListeningEvent->piSourceCritterTags, pEntInfo->piCritterTags)) continue;
		if (!eventtracker_FieldMatchesPooled2(pListeningEvent->pchSourceEncounterName, pEntInfo->pchEncounterName)) continue;
		if (!eventtracker_FieldMatchesScoped(pListeningEvent->pScope, pListeningEvent->pchSourceStaticEncName, pEntInfo->eaStaticEncScopeNames)) continue;
		if (pListeningEvent->pchSourceStaticEncExclude && eventtracker_FieldMatchesScoped(pListeningEvent->pScope, pListeningEvent->pchSourceStaticEncExclude, pEntInfo->eaStaticEncScopeNames)) continue;
		if (!eventtracker_FieldMatchesScoped(pListeningEvent->pScope, pListeningEvent->pchSourceEncGroupName, pEntInfo->eaEncGroupScopeNames)) continue;
		if (!eventtracker_FieldMatchesPooled2(pListeningEvent->pchSourceObjectName, pEntInfo->pchObjectName)) continue;
		if (!eventtracker_FieldMatchesPooled2(pListeningEvent->pchSourceFactionName, pEntInfo->pchFactionName)) continue;
		if (!eventtracker_FieldMatchesPooled2(pListeningEvent->pchSourceAllegianceName, pEntInfo->pchAllegianceName)) continue;
		if (!eventtracker_FieldMatchesPooled2(pListeningEvent->pchSourceRank, pEntInfo->pchRank)) continue;
		if (!eventtracker_FieldMatchesPooled2(pListeningEvent->pchSourceClassName, pEntInfo->pchClassName)) continue;
		if (!eventtracker_FieldMatchesInt2(pListeningEvent->eSourceRegionType, pEntInfo->eRegionType)) continue;
		if (!eventtracker_FieldMatchesTriStateBool(pListeningEvent->tSourceIsPlayer, pEntInfo->bIsPlayer)) continue;
		
		// Match Source credit
		if (pListeningEvent->tMatchSource == TriState_Yes && pListeningEvent->tMatchSourceTeam != TriState_Yes && pListeningEvent->sourceEntRef && pEntInfo->entRef != pListeningEvent->sourceEntRef) continue;
		if (pListeningEvent->tMatchSource == TriState_No && pListeningEvent->sourceEntRef && pEntInfo->entRef == pListeningEvent->sourceEntRef) continue;

		// Team credit
		if (pListeningEvent->tMatchSourceTeam == TriState_Yes && pListeningEvent->sourceEntRef && pEntInfo->entRef != pListeningEvent->sourceEntRef){
			if (pSentEvent->bUseComplexTeamMatchingSource){
				// "Complex" team matching - All teammates were added as Sources, so no need to check teammates here
				continue;
			} else {
				// "Normal" team matching - Check the TeamID
				Entity *pListenerEnt = entFromEntityRef(pListeningEvent->iPartitionIdx, pListeningEvent->sourceEntRef);
				if (!team_IsOnTeamWithID(pListenerEnt, pEntInfo->teamID)) continue;
			}
		}
		if (pListeningEvent->tMatchSourceTeam == TriState_No && pListeningEvent->sourceEntRef){
			Entity *pListenerEnt = entFromEntityRef(pListeningEvent->iPartitionIdx, pListeningEvent->sourceEntRef);
			if (team_IsOnTeamWithID(pListenerEnt, pEntInfo->teamID)) continue;	
		}
		if (pListeningEvent->pchSourcePowerMode && pEntInfo->entRef){
			Entity *pListeningEnt = entFromEntityRef(pListeningEvent->iPartitionIdx, pEntInfo->entRef);
			if (pListeningEnt && pListeningEnt->pChar){
				S32 iPowerMode = StaticDefineIntGetInt(PowerModeEnum, pListeningEvent->pchSourcePowerMode);
				if (iPowerMode < 0 || !character_HasMode(pListeningEnt->pChar, iPowerMode)) {
					return false;
				}
			} else {
				return false;
			}
		}

		// All criteria were met
		return true;
	}

	// Did not find a matching entity
	return false;
}


static __forceinline bool eventtracker_MatchParticipant(GameEvent *pListeningEvent, GameEvent *pSentEvent, GameEventParticipant *pEntInfo)
{
	if (!pEntInfo->bHasCredit && !(pEntInfo->bHasTeamCredit && pSentEvent->bUseComplexTeamMatchingTarget && pListeningEvent->tMatchTargetTeam == TriState_Yes)) return false;
	if (!eventtracker_FieldMatchesPooled2(pListeningEvent->pchTargetActorName, pEntInfo->pchActorName)) return false;
	if (!eventtracker_FieldMatchesPooled2(pListeningEvent->pchTargetCritterName, pEntInfo->pchCritterName)) return false;
	if (!eventtracker_FieldMatchesPooled2(pListeningEvent->pchTargetCritterGroupName, pEntInfo->pchCritterGroupName)) return false;
	if (!eventtracker_HasTags(pListeningEvent->piTargetCritterTags, pEntInfo->piCritterTags)) return false;
	if (!eventtracker_FieldMatchesPooled2(pListeningEvent->pchTargetEncounterName, pEntInfo->pchEncounterName)) return false;
	if (!eventtracker_FieldMatchesScoped(pListeningEvent->pScope, pListeningEvent->pchTargetStaticEncName, pEntInfo->eaStaticEncScopeNames)) return false;
	if (pListeningEvent->pchTargetStaticEncExclude && eventtracker_FieldMatchesScoped(pListeningEvent->pScope, pListeningEvent->pchTargetStaticEncExclude, pEntInfo->eaStaticEncScopeNames)) return false;
	if (!eventtracker_FieldMatchesScoped(pListeningEvent->pScope, pListeningEvent->pchTargetEncGroupName, pEntInfo->eaEncGroupScopeNames)) return false;
	if (!eventtracker_FieldMatchesPooled2(pListeningEvent->pchTargetObjectName, pEntInfo->pchObjectName)) return false;
	if (!eventtracker_FieldMatchesPooled2(pListeningEvent->pchTargetFactionName, pEntInfo->pchFactionName)) return false;
	if (!eventtracker_FieldMatchesPooled2(pListeningEvent->pchTargetAllegianceName, pEntInfo->pchAllegianceName)) return false;
	if (!eventtracker_FieldMatchesPooled2(pListeningEvent->pcTargetRank, pEntInfo->pchRank)) return false;
	if (!eventtracker_FieldMatchesInt2(pListeningEvent->eTargetRegionType, pEntInfo->eRegionType)) return false;
	if (!eventtracker_FieldMatchesPooled2(pListeningEvent->pchTargetClassName, pEntInfo->pchClassName)) return false;
	if (!eventtracker_FieldMatchesTriStateBool(pListeningEvent->tTargetIsPlayer, pEntInfo->bIsPlayer)) return false;
	if (!eventtracker_FieldMatchesPooled2(pListeningEvent->pchNemesisType, pEntInfo->pchNemesisType)) return false;

	// Match Target credit
	if (pListeningEvent->tMatchTarget == TriState_Yes && pListeningEvent->tMatchTargetTeam != TriState_Yes && pListeningEvent->targetEntRef && pEntInfo->entRef != pListeningEvent->targetEntRef) return false;
	if (pListeningEvent->tMatchTarget == TriState_No && pListeningEvent->targetEntRef && pEntInfo->entRef == pListeningEvent->targetEntRef) return false;

	// Team credit
	if (pListeningEvent->tMatchTargetTeam == TriState_Yes && pListeningEvent->targetEntRef && pEntInfo->entRef != pListeningEvent->targetEntRef){
		if (pSentEvent->bUseComplexTeamMatchingTarget){
			// "Complex" team matching - All teammates were added as Targets, so no need to check teammates here
			return false;
		} else {
			// "Normal" team matching - Check the TeamID
			Entity *pListeningEnt = entFromEntityRef(pListeningEvent->iPartitionIdx, pListeningEvent->targetEntRef);
			if (!team_IsOnTeamWithID(pListeningEnt, pEntInfo->teamID)) return false;
		}
	}
	if (pListeningEvent->tMatchTargetTeam == TriState_No && pListeningEvent->targetEntRef){
		Entity *pListeningEnt = entFromEntityRef(pListeningEvent->iPartitionIdx, pListeningEvent->targetEntRef);
		if (team_IsOnTeamWithID(pListeningEnt, pEntInfo->teamID)) return false;
	}
	if (pListeningEvent->pchTargetPowerMode && pEntInfo->entRef){
		Entity *pListeningEnt = entFromEntityRef(pListeningEvent->iPartitionIdx, pEntInfo->entRef);
		if (pListeningEnt && pListeningEnt->pChar){
			S32 iPowerMode = StaticDefineIntGetInt(PowerModeEnum, pListeningEvent->pchTargetPowerMode);
			if (iPowerMode < 0 || !character_HasMode(pListeningEnt->pChar, iPowerMode))
				return false;
		} else {
			return false;
		}
	}

	return true;
}


bool eventtracker_EntityMatchesEvent(GameEvent *pListeningEvent, GameEvent *pSentEvent, Entity *pEnt)
{
	GameEventParticipant *pEntInfo = eventsend_GetOrCreateEntityInfo(pEnt);

	return eventtracker_MatchParticipant(pListeningEvent, pSentEvent, pEntInfo);
}


bool eventtracker_CaresAboutTarget(GameEvent *pListeningEvent)
{
	// See if we care about the target entity at all
	if (pListeningEvent->pchTargetActorName
		|| pListeningEvent->pchTargetCritterName
		|| pListeningEvent->pchTargetCritterGroupName
		|| pListeningEvent->pchTargetEncounterName
		|| pListeningEvent->pchTargetStaticEncName
		|| pListeningEvent->pchTargetStaticEncExclude
		|| pListeningEvent->pchTargetEncGroupName
		|| pListeningEvent->pchTargetObjectName
		|| pListeningEvent->pchTargetFactionName
		|| pListeningEvent->pchTargetAllegianceName
		|| pListeningEvent->pcTargetRank
		|| pListeningEvent->pchTargetClassName
		|| pListeningEvent->pchTargetPowerMode
		|| pListeningEvent->eTargetRegionType != -1
		|| pListeningEvent->tMatchTarget != TriState_DontCare
		|| pListeningEvent->tMatchTargetTeam != TriState_DontCare
		|| pListeningEvent->tTargetIsPlayer != TriState_DontCare
		|| eaiSize(&pListeningEvent->piTargetCritterTags))
	{
		return true;
	}

	return false;
}


// Returns TRUE if the Listening event matches any Target entity in the Sent event
static bool eventtracker_MatchTarget(GameEvent *pListeningEvent, GameEvent *pSentEvent)
{
	int i,n;
	bool bCaresAboutTarget = eventtracker_CaresAboutTarget(pListeningEvent);

	if (!bCaresAboutTarget) {
		return true;
	}

	// Return TRUE if at least one EntInfo is a match
	n = eaSize(&pSentEvent->eaTargets);
	for (i = 0; i < n; i++) {
		GameEventParticipant *pEntInfo = pSentEvent->eaTargets[i];

		if (!eventtracker_MatchParticipant(pListeningEvent, pSentEvent, pEntInfo)) {
			continue;
		}

		// All criteria were met
		return true;
	}
		
	// Did not find a matching entity
	return false;
}

static bool eventtracker_MatchEvents(GameEvent *pListeningEvent, GameEvent *pSentEvent, bool bSimpleSourceTarget)
{
	// Pooled strings can use EventFieldMatchesPooled for better performance
	// TODO - make more of these pooled, or find some other way of making cheaper comparisons

	if (!pListeningEvent || !pSentEvent) return false;
	if ((pListeningEvent->iPartitionIdx != PARTITION_ANY) && (pListeningEvent->iPartitionIdx != pSentEvent->iPartitionIdx)) return false;
	if (pListeningEvent->type != pSentEvent->type) return false;
	
	// Match all simple fields
	if (!eventtracker_MatchesPooled(pchFSMName)) return false;
	if (!eventtracker_MatchesPooled(pchFsmStateName)) return false;
	if (!eventtracker_MatchesPooled(pchContactName)) return false;
	if (!eventtracker_MatchesPooled(pchStoreName)) return false;
	if (!eventtracker_MatchesPooled(pchMissionRefString)) return false;
	if (!eventtracker_MatchesPooled(pchMissionCategoryName)) return false;
	if (!eventtracker_MatchesPooled(pchItemName)) return false;
	if (!eventtracker_FieldMatches(pchCutsceneName)) return false;
	if (!eventtracker_FieldMatches(pchVideoName)) return false;
	if (!eventtracker_FieldMatches(pchPowerName)) return false;
	if (!eventtracker_FieldMatches(pchPowerEventName)) return false;
	if (!eventtracker_FieldMatches(pchDamageType)) return false;
	if (!eventtracker_FieldMatches(pchDialogName)) return false;
	if (!eventtracker_MatchesPooled(pchEmoteName)) return false;
	if (!eventtracker_MatchesPooled(pchItemAssignmentName)) return false;
	if (!eventtracker_MatchesPooled(pchItemAssignmentOutcome)) return false;
	if (!eventtracker_FieldMatches(pchMessage)) return false;

	if (!eventtracker_FieldMatchesInt(encState)) return false;
	if (!eventtracker_FieldMatchesInt(missionState)) return false;
	if (!eventtracker_FieldMatchesInt(missionType)) return false;
	if (!eventtracker_FieldMatchesInt(missionLockoutState)) return false;
	if (!eventtracker_FieldMatchesInt(nemesisState)) return false;
	if (!eventtracker_FieldMatchesInt(healthState)) return false;
	if (!eventtracker_FieldMatchesInt(eMinigameType)) return false;
	if (!eventtracker_FieldMatchesInt(ePvPQueueMatchResult)) return false;
	if (!eventtracker_FieldMatchesInt(ePvPEvent)) return false;
	//if (!eventtracker_FieldMatchesInt(iRewardTier)) return false;
	if (!eventtracker_FieldMatchesInt(iScoreboardRank)) return false;
	if (!eventtracker_FieldMatches(pchScoreboardMetricName)) return false;
	if (!eventtracker_MatchesPooled(pchGroupProjectName)) return false;
	if (!eventtracker_MatchesPooled(pchAllegianceName)) return false;
	if (pListeningEvent->bIsRootMission && !pSentEvent->bIsRootMission) return false;
	if (pListeningEvent->pchTrackedMission && stricmp(pListeningEvent->pchTrackedMission, pSentEvent->pchMissionRefString) != 0) return false;

	if (ea32Size(&pListeningEvent->eaItemCategories) 
		&& pListeningEvent->eaItemCategories[0] != kItemCategory_None
		&& ea32Find((U32**)&pSentEvent->eaItemCategories, pListeningEvent->eaItemCategories[0])<0) 
	{
		return false;
	}

	if (pListeningEvent->tPartOfUGCProject != TriState_DontCare)
	{
		if (pListeningEvent->tPartOfUGCProject != pSentEvent->tPartOfUGCProject)
			return false;
	}
	if (pListeningEvent->tUGCFeaturedCurrently != TriState_DontCare)
	{
		if (pListeningEvent->tUGCFeaturedCurrently != pSentEvent->tUGCFeaturedCurrently)
			return false;
	}
	if (pListeningEvent->tUGCFeaturedPreviously != TriState_DontCare)
	{
		if (pListeningEvent->tUGCFeaturedPreviously != pSentEvent->tUGCFeaturedPreviously)
			return false;
	}
	if (pListeningEvent->tUGCProjectQualifiesForReward != TriState_DontCare)
	{
		if (pListeningEvent->tUGCProjectQualifiesForReward != pSentEvent->tUGCProjectQualifiesForReward)
			return false;
	}

	if (pListeningEvent->fItemAssignmentSpeedBonus && 
		pSentEvent->fItemAssignmentSpeedBonus < pListeningEvent->fItemAssignmentSpeedBonus)
		return false;

	// Match on scoped fields
	if (!eventtracker_FieldMatchesScoped(pListeningEvent->pScope, pListeningEvent->pchClickableName, pSentEvent->eaClickableScopeNames)) return false;
	if (!eventtracker_FieldMatchesScoped(pListeningEvent->pScope, pListeningEvent->pchClickableGroupName, pSentEvent->eaClickableGroupScopeNames)) return false;

	if (!bSimpleSourceTarget)
	{
		// Match the Source and Target
		if (!eventtracker_MatchSource(pListeningEvent, pSentEvent)) return false; 
		if (!eventtracker_MatchTarget(pListeningEvent, pSentEvent)) return false;
	}
	else
	{
		if (pListeningEvent->sourceEntRef != pSentEvent->sourceEntRef) return false;
		if (pListeningEvent->targetEntRef != pSentEvent->targetEntRef) return false;
	}
	
	
	// Match Volumes
	if (!eventtracker_MatchVolume(pListeningEvent, pSentEvent)) return false;
		
	return true;
}


// ----------------------------------------------------------------------------------
// Event Sending Functions
// ----------------------------------------------------------------------------------

void eventtracker_QueueFinishedSendingCallback(EventTrackerFinishedCB callback, void *pUserData, bool bRunOnce)
{
	if (s_bEventSendInProgress) {
		EventTrackerQueuedCallback *pQueuedCallback = NULL;
		if (bRunOnce) {
			int i, n = eaSize(&s_eaQueuedEventTrackerCallbacks);
			for (i = 0; i < n; i++) {
				pQueuedCallback = s_eaQueuedEventTrackerCallbacks[i];
				if (pQueuedCallback->callback == callback && pQueuedCallback->pUserData == pUserData) {
					return;
				}
			}
		}

		pQueuedCallback = calloc(1, sizeof(EventTrackerQueuedCallback));
		pQueuedCallback->callback = callback;
		pQueuedCallback->pUserData = pUserData;
		eaPush(&s_eaQueuedEventTrackerCallbacks, pQueuedCallback);
	} else {
		callback(pUserData);
	}
}

static S32 eventTracker_hasChainedEvent(SA_PARAM_NN_VALID GameEvent *ev)
{
	return ev->pChainEventDef || ev->pChainEvent;
}


static __forceinline S32 eventTracker_DoesChainExist(ChainedEventTracker *pChainedTracker, EventTracker *pTracker, GameEvent *pNewChainedEvent)
{
	if (pChainedTracker->pTracker == pTracker)
	{
		GameEvent *pChainedEv = pChainedTracker->pTrackedObject->ev;
		return (pChainedEv->pRootEvent == pNewChainedEvent->pRootEvent && 
				pChainedEv->sourceEntRef == pNewChainedEvent->sourceEntRef &&
				pChainedEv->targetEntRef == pNewChainedEvent->targetEntRef &&
				pChainedEv->mapOwnerEntRef == pNewChainedEvent->mapOwnerEntRef);
	}

	return false;
}

static GameEvent* eventTracker_CreateAndTrackChainedEvent(SA_PARAM_NN_VALID EventTracker *pTracker, SA_PARAM_NN_VALID EventListener *pListener, 
															SA_PARAM_NN_VALID GameEvent *pSrcEvent, SA_PARAM_NN_VALID GameEvent *pTriggeringEvent)
{
	GameEvent *pChainedEvDef;
	GameEvent *pRetEvent;
	ChainedEventTracker *pChainTracker = NULL;
	EventTrackerObject *pChainedTrackerObject = NULL;
	EventTrackerPartitionState *pState;
	GameEvent *pChainedEvent;

	PERFINFO_AUTO_START_FUNC();

	devassert(pSrcEvent);

	if (g_EventLogDebug){
		printf("\n<><><> EventTrigger Created New Chained Event:\n");
		eventtracker_DebugPrint(pSrcEvent, -1);
	}

	pChainedEvDef = pSrcEvent->pChainEventDef ? pSrcEvent->pChainEventDef : pSrcEvent->pChainEvent;
	if(!pChainedEvDef) {
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	pChainedEvent = gameevent_CopyListener(pChainedEvDef);
	if (!pChainedEvent) {
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	pChainedEvent->iPartitionIdx = pTriggeringEvent->iPartitionIdx;
	pChainedEvent->fChainTime = pSrcEvent->fChainTime;
	if (!pSrcEvent->pRootEvent)
		pChainedEvent->pRootEvent = pSrcEvent;
	else
		pChainedEvent->pRootEvent = pSrcEvent->pRootEvent;


	// set the source/target specific refs
	if (eaSize(&pTriggeringEvent->eaSources))
	{
		pChainedEvent->sourceEntRef = pTriggeringEvent->eaSources[0]->entRef;
	}
	if (eaSize(&pTriggeringEvent->eaTargets))
	{
		pChainedEvent->targetEntRef = pTriggeringEvent->eaTargets[0]->entRef;
	}
	if (pTriggeringEvent->mapOwnerEntRef && pChainedEvDef->tMatchMapOwner)
	{
		pChainedEvent->mapOwnerEntRef = pTriggeringEvent->mapOwnerEntRef;
	}
	
	pState = eventtracker_GetPartitionState(pTriggeringEvent->iPartitionIdx);

	// check if we are already tracking this event
	FOR_EACH_IN_EARRAY(pState->eaChainedEventTrackers, ChainedEventTracker, pTest)
	{
		if (pSrcEvent != pTest->pTrackedObject->ev && eventTracker_DoesChainExist(pTest, pTracker, pChainedEvent))
		{
			pTest->chainStartTime = ABS_TIME;
			StructDestroy(parse_GameEvent, pChainedEvent);
			PERFINFO_AUTO_STOP();
			return NULL;
		}
	}
	FOR_EACH_END


	if (g_EventLogDebug){
		printf("\n<><><> EventTrigger Created New Chained Event:\n");
		eventtracker_DebugPrint(pSrcEvent, -1);
	}


	pRetEvent = eventtracker_StartTrackingInternal(	pChainedEvent, pChainedEvent->pScope, 
													pListener->structPtr, pListener->updateFunc, pListener->setFunc, 
													false, pTracker, &pChainedTrackerObject);
	if (!pRetEvent)
	{
		StructDestroy(parse_GameEvent, pChainedEvent);
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	pRetEvent->bIsChainedEvent = true;

	pChainTracker = MP_ALLOC(ChainedEventTracker);
	pChainTracker->chainStartTime = ABS_TIME;
	pChainTracker->pTracker = pTracker;
	pChainTracker->pTrackedObject = pChainedTrackerObject;

	// add to the list of chained events 
	eaPush(&pState->eaChainedEventTrackers, pChainTracker);

	if (g_EventLogDebug){
		printf("\n<><><> Listening for Chained New Event:\n");
		eventtracker_DebugPrint(pChainedEvDef, -1);
		printf("<><><>\n\n");
	}

	PERFINFO_AUTO_STOP();
	return pChainedEvent;
}

static void eventtracker_RemoveAndDestroyChainedEvent(EventTrackerPartitionState *pState, ChainedEventTracker *pChainedTracker, GameEvent *ev, S32 chainedEventIdx)
{
	devassert(pChainedTracker->pTrackedObject && pChainedTracker->pTracker);

	if (!ev)
		ev = pChainedTracker->pTrackedObject->ev;

	devassert(eaSize(&pChainedTracker->pTrackedObject->listeners) == 1);
	RemoveThisObjectAsAListener(pState, pChainedTracker->pTracker, pChainedTracker->pTrackedObject, 
								pChainedTracker->pTrackedObject->listeners[0]->structPtr, -1);
		
	if (ev)
		StructDestroy(parse_GameEvent, ev);

	MP_FREE(ChainedEventTracker, pChainedTracker);
	eaRemoveFast(&pState->eaChainedEventTrackers, chainedEventIdx);
}

//remove all chained events that this tracker is apart of
static void eventtracker_FindAndRemoveChainedEventByTracker(EventTracker *pTracker)
{
	EventTrackerPartitionState *pState;
	S32 i;
	
	pState = eventtracker_GetPartitionState(pTracker->iPartitionIdx);

	for (i = eaSize(&pState->eaChainedEventTrackers) - 1; i >= 0; --i)
	{
		ChainedEventTracker *pChainedTracker = pState->eaChainedEventTrackers[i];
		if (pChainedTracker->pTracker == pTracker)
		{
			eventtracker_RemoveAndDestroyChainedEvent(pState, pChainedTracker, SAFE_MEMBER2(pChainedTracker, pTrackedObject, ev), i);
		}
	}
	
}


//remove all chained events that this trackerObject is apart of
static void eventtracker_FindAndRemoveChainedEventByTrackerObject(EventTrackerObject *pTrackerObject)
{
	EventTrackerPartitionState *pState;
	S32 i;

	devassert(pTrackerObject->ev);
	pState = eventtracker_GetPartitionState(pTrackerObject->ev->iPartitionIdx);
	devassertmsg(pState, "Bad GameEvent PartitionIndex");
	
	for (i = eaSize(&pState->eaChainedEventTrackers) - 1; i >= 0; --i)
	{
		ChainedEventTracker *pChainedTracker = pState->eaChainedEventTrackers[i];
		if (pChainedTracker->pTrackedObject == pTrackerObject)
		{
			eventtracker_RemoveAndDestroyChainedEvent(pState, pChainedTracker, pTrackerObject->ev, i);
		}
	}

}

static int eventtracker_SendEventToTracker(EventTrackerPartitionState *pState, EventTracker *pTracker, GameEvent *ev, int iValue, EventUpdateType eType, EventTrackerObject ***peaExpiredChainedTrackers)
{
	// Index into the array; this narrows it down to events of the same general type
	int i, n;
	int iNumListeners = 0;

	if (!pTracker->eventTypeArray) {
		return 0;
	}

	PERFINFO_AUTO_START_FUNC();

	n = eaSize(&pTracker->eventTypeArray[ev->type]);
	for (i = n-1; i >= 0; --i) {
		// Compare with each event of this type
		EventTrackerObject *pTrackerObject = pTracker->eventTypeArray[ev->type][i];
		bool bMatch;


		if (!pTrackerObject) {
			continue;
		}

		PERFINFO_AUTO_START("eventtracker_MatchEvents", 1);
		bMatch = eventtracker_MatchEvents(pTrackerObject->ev, ev, false);
		PERFINFO_AUTO_STOP();

		if (bMatch) {
			// If there's a match, update all listeners
			int j, m;

			PERFINFO_AUTO_START("Matched",1);

			m = eaSize(&pTrackerObject->listeners);
			for (j = m-1; j >= 0; --j) {
				EventListener *pListener = pTrackerObject->listeners[j];
				if (pListener) {
					// 
					if (eventTracker_hasChainedEvent(pTrackerObject->ev)) {
						// this event has a chain that must also succeed before we send to the listener.
						// Create the new game event that we will track
						eventTracker_CreateAndTrackChainedEvent(pTracker, pListener, pTrackerObject->ev, ev);
					} else {
						if (eType == EventLog_Add && pListener->updateFunc) {
							pListener->updateFunc(pListener->structPtr, pTrackerObject->ev, ev, iValue);
						} else if (eType == EventLog_Set && pListener->setFunc) {
							pListener->setFunc(pListener->structPtr, pTrackerObject->ev, ev, iValue);
						}
					}
					iNumListeners++;
				} else {
					// Clear freed listeners out of the tracker
					eaRemoveFast(&pTrackerObject->listeners, j);
				}
			}

			if (pTrackerObject->ev->bIsChainedEvent) {
				if (g_EventLogDebug){
					printf("\n<><><> Chain Event Ended:\n");
					eventtracker_DebugPrint(pTrackerObject->ev, iNumListeners);
					printf("<><><>\n\n");
				}
				// queue this chained event to expire
				eaPush(peaExpiredChainedTrackers, pTrackerObject);
			}

			PERFINFO_AUTO_STOP(); // Matched
		}
	}

	PERFINFO_AUTO_STOP(); // Func

	return iNumListeners;
}


// This is the public function that sends an Event to all possible trackers
void eventtracker_SendEvent( int iPartitionIdx, GameEvent *ev, int iCount, EventUpdateType eType, bool bLog )
{
	static EventTracker **eaTrackerList = NULL;
	static EventTrackerObject **s_eaExpiredChainedTrackers = NULL;

	EventTrackerPartitionState *pState;
	int iNumListeners = 0;
	int i, n;
	bool bPrevSendInProgress = s_bEventSendInProgress;

	if ((iPartitionIdx == PARTITION_IN_TRANSACTION) || (iPartitionIdx == PARTITION_ENT_BEING_DESTROYED) || (iPartitionIdx == PARTITION_ORPHAN_PET)) {
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	s_bEventSendInProgress = true;

	PERFINFO_AUTO_START("GetTrackers", 1);

	// Get player, team, encounter, etc. trackers
	eventtracker_GetTrackersForEvent(ev, &eaTrackerList);

	// add global tracker
	pState = eventtracker_GetPartitionState(iPartitionIdx);
	eaPush(&eaTrackerList, pState->pGlobalEventTracker);

	// add the "any" global tracker
	eaPush(&eaTrackerList, s_pPartitionAnyState->pGlobalEventTracker);

	eaClearFast(&s_eaExpiredChainedTrackers);

	PERFINFO_AUTO_STOP(); // GetTrackers
	
	PERFINFO_AUTO_START("Sending", 1);

	// Send event to all possible trackers
	n = eaSize(&eaTrackerList);
	for (i = 0; i < n; ++i) {
		iNumListeners += eventtracker_SendEventToTracker(pState, eaTrackerList[i], ev, iCount, eType, &s_eaExpiredChainedTrackers);
	}

	PERFINFO_AUTO_STOP(); //Sending

	PERFINFO_AUTO_START("Cleanup", 1);

	// remove all the expired chained trackers
	FOR_EACH_IN_EARRAY(s_eaExpiredChainedTrackers, EventTrackerObject, pTrackerObject)
		eventtracker_FindAndRemoveChainedEventByTrackerObject(pTrackerObject);
	FOR_EACH_END
	

	ev->count = iCount;
	if (g_EventLogDebug){
		EntityIterator* entityIterator = entGetIteratorSingleTypeAllPartitions(0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
		Entity *pPlayerEntity = NULL;
		while(pPlayerEntity = EntityIteratorGetNext(entityIterator))
		{
			GameEventParticipants *pGameEventParticipants = StructCreate(parse_GameEventParticipants);
			pGameEventParticipants->eaSources = ev->eaSources;
			pGameEventParticipants->eaTargets = ev->eaTargets;
			ClientCmd_DebugSendEvent(pPlayerEntity, ev, pGameEventParticipants);
		}
		EntityIteratorRelease(entityIterator);

		eventtracker_DebugPrint(ev, iNumListeners);
	}
	if (bLog) {
		eventtracker_LogEvent(ev);
	}

	eaClearFast(&eaTrackerList);

	PERFINFO_AUTO_STOP(); // Cleanup

	s_bEventSendInProgress = bPrevSendInProgress;

	// Run all queued callbacks
	if (!s_bEventSendInProgress) {
		PERFINFO_AUTO_START("Queued Callbacks", 1);

		n = eaSize(&s_eaQueuedEventTrackerCallbacks);
		for(i=n-1; i>=0; --i) {
			EventTrackerQueuedCallback *pCallback = eaRemove(&s_eaQueuedEventTrackerCallbacks, i);
			pCallback->callback(pCallback->pUserData);
			free(pCallback);
		}

		PERFINFO_AUTO_STOP(); // Queued Callbacks

		// If something tried to remove an event listener while sending was in progress,
		// compact the eventtrackers here
		if (pState->bCompactTrackers) {
			PERFINFO_AUTO_START("Compact Trackers", 1);

			for (i = 0; i < eaSize(&pState->eaAllTrackers); i++){
				if (pState->eaAllTrackers[i]->uNumInvalid > 0){
					eventtracker_Compact(pState->eaAllTrackers[i]);
				}
			}
			pState->bCompactTrackers = false;

			PERFINFO_AUTO_STOP(); // Compact Trackers
		}
	}

	PERFINFO_AUTO_STOP(); // Func
}


// ----------------------------------------------------------------------------------
// Global Event Tracking Functions
// ----------------------------------------------------------------------------------

int eventtracker_GlobalEventCount(int iPartitionIdx, const char *pcEventName)
{
	EventTrackerPartitionState *pState = eventtracker_GetPartitionState(iPartitionIdx);
	int iEventCount;

	stashFindInt(pState->pGlobalEventLog, pcEventName, &iEventCount);
	return iEventCount;
}


static void eventtracker_GlobalEventCountAdd(void* UNUSED, GameEvent *pSubscribeEvent, GameEvent *pSpecificEvent, int iIncrement)
{
	int iPartitionIdx = pSpecificEvent->iPartitionIdx;
	EventTrackerPartitionState *pState = eventtracker_GetPartitionState(iPartitionIdx);
	int iEventCount = eventtracker_GlobalEventCount(iPartitionIdx, pSubscribeEvent->pchEventName) + iIncrement;
	stashAddInt(pState->pGlobalEventLog, pSubscribeEvent->pchEventName, iEventCount, true);
}


static void eventtracker_GlobalEventCountSet(void* UNUSED, GameEvent *pSubscribeEvent, GameEvent *pSpecificEvent, int iEventCount)
{
	EventTrackerPartitionState *pState = eventtracker_GetPartitionState(pSpecificEvent->iPartitionIdx);
	stashAddInt(pState->pGlobalEventLog, pSubscribeEvent->pchEventName, iEventCount, true);
}


void eventtracker_AddGlobalTrackedEvent(GameEvent *pEvent, const char *pcErrorFilename)
{
	if (eventtracker_AddNamedEventToList(&s_eaUnsharedGlobalTrackedEvents, pEvent, pcErrorFilename)){
		// If tracking has already started, begin tracking immediately on all active partitions
		int i;
		for(i=eaSize(&s_eaTrackerPartitionStates)-1; i>=0; --i) {
			EventTrackerPartitionState *pState = s_eaTrackerPartitionStates[i];
			if (pState) {
				// We should push the event onto the global tracked list now, even if we haven't started officially logging
				//   yet. Otherwise we run the risk of not starting tracking on these events when StartGlobalEventLog is called.
				GameEvent *pCopy = StructClone(parse_GameEvent, pEvent);
				pCopy->iPartitionIdx = i;
				eaPush(&pState->eaGlobalTrackedEvents, pCopy);
				if (pState->pGlobalEventLog) {
					eventtracker_StartTracking(pCopy, NULL, NULL, eventtracker_GlobalEventCountAdd, eventtracker_GlobalEventCountSet);
				}
			}
		}
	}
}


static void eventtracker_StartGlobalEventLog(EventTrackerPartitionState *pState)
{
	int i;

	if (!pState->pGlobalEventLog) {
		pState->pGlobalEventLog = stashTableCreate(8, StashDefault,StashKeyTypeStrings,sizeof(void*));
	}

	// Create the global event log
	for(i=0; i<eaSize(&pState->eaGlobalTrackedEvents); i++) {
		eventtracker_StartTracking(pState->eaGlobalTrackedEvents[i], NULL, NULL, eventtracker_GlobalEventCountAdd, eventtracker_GlobalEventCountSet);
	}
	pState->bGlobalEventLogTracking = true;
}


static void eventtracker_StopGlobalEventLog(EventTrackerPartitionState *pState)
{
	int i;

	// Remove all events from the global event log
	for(i=0; i<eaSize(&pState->eaGlobalTrackedEvents); i++) {
		eventtracker_StopTracking(pState->iPartitionIdx, pState->eaGlobalTrackedEvents[i], NULL);
	}

	stashTableClear(pState->pGlobalEventLog);
	pState->bGlobalEventLogTracking = false;
}


// ----------------------------------------------------------------------------------
// Map Lifecycle Functions
// ----------------------------------------------------------------------------------

void eventtracker_PartitionLoad(int iPartitionIdx)
{
	EventTrackerPartitionState *pState = eaGet(&s_eaTrackerPartitionStates, iPartitionIdx);

	PERFINFO_AUTO_START_FUNC();

	if (!pState) {
		// Initialize state
		pState = eventtracker_CreatePartitionState(iPartitionIdx);
		eaSet(&s_eaTrackerPartitionStates, pState, iPartitionIdx);
	} else {
		// Reset the global event log
		eventtracker_StopGlobalEventLog(pState);
	}

	PERFINFO_AUTO_STOP();
}


void eventtracker_PartitionLoadLate(int iPartitionIdx)
{
	EventTrackerPartitionState *pState = eaGet(&s_eaTrackerPartitionStates, iPartitionIdx);

	PERFINFO_AUTO_START_FUNC();
	// Start tracking global events
	eventtracker_StartGlobalEventLog(pState);

	PERFINFO_AUTO_STOP();
}


void eventtracker_PartitionUnload(int iPartitionIdx)
{
	EventTrackerPartitionState *pState = eaGet(&s_eaTrackerPartitionStates, iPartitionIdx);
	if (pState) {
		// Stop global events
		eventtracker_StopGlobalEventLog(pState);

		// Clean up other partition data
		eventtracker_DestroyPartitionState(pState);
		eaSet(&s_eaTrackerPartitionStates, NULL, iPartitionIdx);
	}
}


void eventtracker_MapLoad(bool bFullInit)
{
	if (bFullInit) {
		// Cause each partition to get a load callback
		partition_ExecuteOnEachPartition(eventtracker_PartitionLoad);

		// Reset the "any" partition
		eventtracker_StopGlobalEventLog(s_pPartitionAnyState);
	}
}


void eventtracker_MapLoadLate(bool bFullInit)
{
	if (bFullInit) {
		// Cause each partition to get a load callback
		partition_ExecuteOnEachPartition(eventtracker_PartitionLoadLate);

		eventtracker_StartGlobalEventLog(s_pPartitionAnyState);
	}
}


void eventtracker_MapUnload(void)
{
	// Unload all partitions
	int i;
	for(i=eaSize(&s_eaTrackerPartitionStates)-1; i>=0; --i) {
		if (s_eaTrackerPartitionStates[i]) {
			eventtracker_PartitionUnload(i);
		}
	}

	// Stop the "any" partition
	eventtracker_StopGlobalEventLog(s_pPartitionAnyState);
}


// ----------------------------------------------------------------------------------
// Initialization Functions
// ----------------------------------------------------------------------------------

AUTO_RUN;
void EventTrackerInit(void)
{
	MP_CREATE(EventTrackerObject, 500);
	MP_CREATE(EventListener, 1000);
	MP_CREATE(ChainedEventTracker, 20);

	s_pPartitionAnyState = eventtracker_CreatePartitionState(-1);
}


AUTO_RUN_LATE;
void eventtracker_InitLogCategories(void)
{
	int i;

	for (i=0; i < EventType_Count; i++) {
		eLogCategoriesForEventTypes[i] = LOG_LAST;
	}

	for (i=0; i < ARRAY_SIZE(sLogCategoryDefs); i++) {
		assert(sLogCategoryDefs[i].eEventType >= 0 && sLogCategoryDefs[i].eEventType < EventType_Count);
		assert(sLogCategoryDefs[i].eLogCategory >= 0 && sLogCategoryDefs[i].eLogCategory < LOG_LAST);
		assertmsgf(eLogCategoriesForEventTypes[sLogCategoryDefs[i].eEventType] == LOG_LAST, "Duplicate log category assigned for event category %s",
			StaticDefineIntRevLookup(EventTypeEnum, sLogCategoryDefs[i].eEventType));
		eLogCategoriesForEventTypes[sLogCategoryDefs[i].eEventType] = sLogCategoryDefs[i].eLogCategory;
	}

	for (i=0; i < EventType_Count; i++) {
		assertmsgf(eLogCategoriesForEventTypes[sLogCategoryDefs[i].eEventType] != LOG_LAST, "No log category assigned for event type %s. Please assign one in sLogCategoryDefs in EventTracker.c",
			StaticDefineIntRevLookup(EventTypeEnum, i));
	}
}

static void eventtracker_OncePerFrame(EventTrackerPartitionState *pState)
{
	S32 i;

	// go through all the chained event trackers and check to see if the chain time has expired	
	for (i = eaSize(&pState->eaChainedEventTrackers) -1; i >= 0; --i)
	{
		ChainedEventTracker *pChainTracker = pState->eaChainedEventTrackers[i];
		EventTrackerObject *pTrackedObject = pChainTracker->pTrackedObject;
		EventTracker *pTracker = pChainTracker->pTracker;
		GameEvent *ev = pTrackedObject->ev;
		
		devassert(ev && pTracker && pTrackedObject);

		if (ABS_TIME_SINCE(pChainTracker->chainStartTime) >= SEC_TO_ABS_TIME(ev->fChainTime))
		{
			if (g_EventLogDebug) {
				printf("\n<><><>Chain event Expiring:\n");
				eventtracker_DebugPrint(ev, -1);
				printf("<><><>\n\n");
			}
			
			eventtracker_RemoveAndDestroyChainedEvent(pState, pChainTracker, ev, i);
		}
	}

}

void eventTracker_OncePerFrameUpdateAllPartitions()
{
	int i;

	for(i=eaSize(&s_eaTrackerPartitionStates)-1; i>=0; --i) {
		EventTrackerPartitionState *pState = s_eaTrackerPartitionStates[i];
		if (pState) {
			eventtracker_OncePerFrame(pState);
		}
	}
}

