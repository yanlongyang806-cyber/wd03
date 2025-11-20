
/***************************************************************************
*     Copyright (c) 2006-2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "referencesystem.h"
#include "stdtypes.h"
#include "Message.h" // For DisplayMessage
#include "ActivityCalendar.h" 
#include "WorldVariable.h"
#include "ShardEventTimingCommon.h"

#ifndef ACTIVITY_COMMON_H
#define ACTIVITY_COMMON_H

#define EVENT_VAR_CONTAINER_ID  1

typedef struct AllegianceDef AllegianceDef;
typedef struct DisplayMessage DisplayMessage;
typedef struct DoorTransitionSequenceDef DoorTransitionSequenceDef;
typedef struct Entity Entity;
typedef struct QueueDef QueueDef;


typedef enum kEventRunMode
{
	kEventRunMode_ForceOff	= -1,
	kEventRunMode_Auto = 0,
	kEventRunMode_ForceOn = 1,
}kEventRunMode;


AUTO_STRUCT;
typedef struct ActivityDef
{
	const char *pchActivityName;				AST(KEY STRUCTPARAM POOL_STRING)
		// The internal name of the activity
	
	const char** ppchDependentMissionDefs;		AST(NO_TEXT_SAVE POOL_STRING)
		// This is filled in at load-time
	WorldVariableDef **eaVaraiableDefs;			AST(NAME(Variable))

	U32 uDefaultDelay;							AST(NAME(StartDelay))
	U32 uDuration;								AST(NAME(Duration))
}ActivityDef;

AUTO_STRUCT;
typedef struct ActivityDefs
{
	ActivityDef **ppDefs;					AST(NAME(Activity))
}ActivityDefs;

AUTO_STRUCT;
typedef struct ActivityDefRef
{
	const char* pchActivityName;				AST(POOL_STRING STRUCTPARAM REQUIRED)

	U32 uDelayStart;
	U32 uEarlyEnd;
}ActivityDefRef;

AUTO_STRUCT;
typedef struct EventContactDef
{
	const char *pchContactDef;							AST(STRUCTPARAM POOL_STRING)
	const char *pchDialogName;							AST(NAME(DialogName) POOL_STRING)
	const char **ppchAllegiances;						AST(NAME(Allegiance) POOL_STRING)
} EventContactDef;

AUTO_STRUCT;
typedef struct EventWarpDef
{
	const char *pchAllegianceName;						AST(STRUCTPARAM KEY POOL_STRING)
	const char *pchSpawnMap;							AST(NAME(SpawnMap) POOL_STRING)
	const char *pchSpawnPoint;							AST(NAME(SpawnPoint) POOL_STRING)
	S32 iRequiredLevel;									AST(NAME(RequiredLevel))
	REF_TO(DoorTransitionSequenceDef) hTransOverride;	AST(NAME(TransitionOverride) REFDICT(DoorTransitionSequenceDef))
} EventWarpDef;

AUTO_STRUCT AST_CONTAINER;
typedef struct EventEntry
{
	CONST_STRING_POOLED pchEventName;		AST(PERSIST SUBSCRIBE KEY)

		const U32 uLastTimeStarted;				AST(PERSIST SUBSCRIBE)
		const S32 iTimingIndex;					AST(PERSIST SUBSCRIBE)
		const S32 iEventRunMode;				AST(PERSIST SUBSCRIBE)	// ForceOff, ForceOn, Auto
}EventEntry;

AUTO_STRUCT AST_CONTAINER;
typedef struct EventContainer
{
	const ContainerID iContainerID;			AST(PERSIST SUBSCRIBE KEY) //Should almost always be 1
	CONST_EARRAY_OF(EventEntry)	ppEntries;	AST(PERSIST SUBSCRIBE)
}EventContainer;

AUTO_STRUCT;
typedef struct EventBulletin
{
	DisplayMessage msgTitle;			AST(NAME(Title) STRUCT(parse_DisplayMessage))
	DisplayMessage msgMessageBody;		AST(NAME(MessageBody) STRUCT(parse_DisplayMessage))

	U32 uPrecedeEventTime;				AST(NAME(PrecedeEventTime))
} EventBulletin;

AUTO_STRUCT;
typedef struct EventDef
{
	char *pchEventName;					AST(KEY STRUCTPARAM POOL_STRING)

	const char* pchFilename;			AST(CURRENTFILE)

	ActivityDefRef **ppActivities;		AST(NAME(Activity))

	ShardEventTimingDef ShardTimingDef; AST(EMBEDDED_FLAT)

	DisplayMessage msgDisplayName;		AST(STRUCT(parse_DisplayMessage))
	DisplayMessage msgDisplayShortDesc;	AST(STRUCT(parse_DisplayMessage))
	DisplayMessage msgDisplayLongDesc;	AST(STRUCT(parse_DisplayMessage))

	const char *pchIcon;				AST(POOL_STRING)
	const char *pchBackground;			AST(POOL_STRING)

	U32 *uDisplayTags;					AST(NAME(DisplayTag) SUBTABLE(ActivityDisplayTagsEnum))

	const char **ppchMapIncluded;		AST(NAME(IncludeMap))
	const char **ppchMapExcluded;		AST(NAME(ExcludeMap))

	const char *pchSpawnMap;			AST(NAME(SpawnMap) POOL_STRING)
		// Universal map to spawn on
	const char *pchSpawnPoint;			AST(NAME(SpawnPoint) POOL_STRING)
		// Universal spawn point
	S32 iWarpRequiredLevel;				AST(NAME(WarpRequiredLevel))
		// Required level to warp
	REF_TO(DoorTransitionSequenceDef) hTransOverride; AST(NAME(TransitionOverride) REFDICT(DoorTransitionSequenceDef))
		// Universal transition sequence override for a map transfer

	EventWarpDef** eaWarpSpawns;		AST(NAME(Warp))
		// Allegiance-specific spawn information

	EventContactDef** eaContacts;		AST(NAME(Contact))
		// Allegiance-specific contact information

	EventBulletin* pBulletin;			AST(NAME(Bulletin))
		// A bulletin to display along with this event

	// The queue which this event is linked with (optional)
	REF_TO(QueueDef) hLinkedQueue;		AST(NAME(Queue))

	const char *pchParentEvent;

	S32 iEventRunMode;			// ForcedOff/Disabled, Auto, ForcedOn/Overridden

	U32 bHideEventFromClient : 1;		AST(NAME(HideEventFromClient, uHideEventFromClient))
	U32 bHideFromExcludedMaps : 1;		AST(NAME(HideFromExcludedMaps, uHideFromExcludedMaps))
}EventDef;

AUTO_STRUCT;
typedef struct EventDefRef
{
	REF_TO(EventDef) hEvent;				AST( REFDICT ( Event ) NAME ( Event ) STRUCTPARAM KEY )
} EventDefRef;

AUTO_STRUCT;
typedef struct EventDefs
{
	EventDefRef **ppDefs;					AST(NAME(Event))
}EventDefs;

AUTO_STRUCT;
typedef struct EventConfig
{
	// Indicates how many minutes before the event the players should be notified
	U32 uPreEventReminderTimeInMinutes;	AST(NAME(PreEventReminderTimeInMinutes))

	// Maximum number of event reminders
	S32 iMaxEventReminders;				AST(DEFAULT(10) NAME(MaxEventReminders))


	// Fields relating to active calendar settings
	U32 *piServerTagsInclude;			AST(NAME(ServerIncludeTag) SUBTABLE(ActivityDisplayTagsEnum))
	U32 *piServerTagsExclude;			AST(NAME(ServerExcludeTag) SUBTABLE(ActivityDisplayTagsEnum))
	U32 uServerCalendarTime;			AST(NAME(ServerCalendarTime))
} EventConfig;
extern EventConfig g_EventConfig;

// Dictionary holding the events
extern DictionaryHandle g_hEventDictionary;

extern ActivityDefs g_ActivityDefs;

void EventDefValidate(EventDef *pEventDef);
ActivityDef *ActivityDef_Find(const char *pchName);
EventDef *EventDef_Find(const char *pchName);
EventWarpDef *EventDef_GetWarpForAllegiance(EventDef *pDef, AllegianceDef *pAllegianceDef);
bool Event_CanPlayerUseWarp(EventWarpDef *pWarpDef, Entity* pEnt, bool bCheckEntStatus);
EventContactDef *EventDef_GetContactForAllegiance(EventDef *pDef, AllegianceDef *pAllegianceDef);

bool EventDef_MapCheck(EventDef *pDef, const char *pchMapName);

#endif
