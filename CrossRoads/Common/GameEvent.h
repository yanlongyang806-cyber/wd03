/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef GAMEEVENT_H
#define GAMEEVENT_H
GCC_SYSTEM

#include "encounter_enums.h"
#include "entEnums.h"
#include "GlobalEnums.h"
#include "GlobalTypeEnum.h"
#include "MinigameCommon.h"
#include "mission_enums.h"
#include "itemEnums.h"
#include "WorldLibEnums.h"

typedef U32 ContainerID;
typedef U32 EntityRef; // This is an opaque entity identifier, which is implemented elsewhere
typedef struct StashTableImp* StashTable;
typedef struct EventTracker EventTracker;
typedef struct Entity Entity;
typedef struct Expression Expression;
typedef struct ExprContext ExprContext;
typedef struct GameEvent GameEvent;
typedef struct MissionInfo MissionInfo;
typedef struct MissionDef MissionDef;
typedef struct ContactDef ContactDef;
typedef struct Mission Mission;
typedef struct MultiVal MultiVal;
typedef struct ParseTable ParseTable;
typedef struct RewardTable RewardTable;
typedef struct ContactInfo ContactInfo;
typedef struct MissionDefParams MissionDefParams;
typedef struct OldEncounter OldEncounter;
typedef struct GameEncounter GameEncounter;
typedef struct Team Team;
typedef struct WorldScope WorldScope;
typedef struct WorldScopeNamePair WorldScopeNamePair;
extern StaticDefineInt CritterTagsEnum[];


AUTO_ENUM;
typedef enum EventType {
	EventType_Assists,
	EventType_ContactDialogStart,
	EventType_ContactDialogComplete,
	EventType_CutsceneEnd,
	EventType_CutsceneStart,
	EventType_Damage,
	EventType_Emote,
	EventType_EncounterState,
	EventType_FSMState,
	EventType_Healing,
	EventType_HealthState,
	EventType_InteractBegin,		ENAMES(InteractBegin ClickableBeginInteract)     // Clickable or Critter initially "clicked" by a player
	EventType_InteractFailure,		ENAMES(InteractFailure ClickableFailure)         // Player failed to meet Interact condition
	EventType_InteractInterrupted,	ENAMES(InteractInterrupted ClickableInterrupted) // Player was interrupted
	EventType_InteractSuccess,		ENAMES(InteractSuccess ClickableInteract CritterInteract)  // Interaction completed successfully
	EventType_InteractEndActive,	ENAMES(InteractEndActive ClickableComplete)     // Clickable "Active" state is completed
	EventType_ItemGained,
	EventType_ItemLost,
	EventType_ItemPurchased,
	EventType_ItemPurchaseEP,
	EventType_ItemUsed,
	EventType_Kills,
	EventType_LevelUp,
	EventType_LevelUpPet,
	EventType_MissionLockoutState,
	EventType_MissionState,
	EventType_NemesisState,
	EventType_PickedUpObject,
	EventType_PlayerSpawnIn,
	EventType_Poke,					ENAMES(Poke FSMPoke)
	EventType_PowerAttrModApplied,
	EventType_VolumeEntered,
	EventType_VolumeExited,
	EventType_BagGetsItem,
	EventType_DuelVictory,
	EventType_MinigameBet,
	EventType_MinigamePayout,
	EventType_MinigameJackpot,
	EventType_PvPQueueMatchResult,
	EventType_PvPEvent,
	EventType_ItemAssignmentStarted,
	EventType_ItemAssignmentCompleted,
	EventType_VideoStarted,
	EventType_VideoEnded,
	EventType_GemSlotted,
	EventType_PowerTreeStepAdded,
	EventType_NearDeath,
	EventType_ContestWin,
	EventType_ScoreboardMetricResult,
	EventType_UGCProjectCompleted,
	EventType_GroupProjectTaskCompleted,
	EventType_AllegianceSet,
	EventType_UGCAccountChanged,

	// Deprecated event types
	EventType_ClickableActive,        // obsolete, replaced by ClickableIsActive() expression function
	EventType_ZoneEventRunning,       // Never sent anymore, but kept around in case they are in logs
	EventType_ZoneEventState,

	EventType_Count, EIGNORE
} EventType;

AUTO_ENUM;
typedef enum HealthState{
	HealthState_75_to_100 = 1,
	HealthState_67_to_100,
	HealthState_50_to_100,
	HealthState_50_to_75,
	HealthState_33_to_67,
	HealthState_25_to_50,
	HealthState_0_to_100,
	HealthState_0_to_50,
	HealthState_0_to_33,
	HealthState_0_to_25,

	HealthState_Count, EIGNORE
} HealthState;

AUTO_ENUM;
typedef enum PvPEvent{
	PvPEvent_CTF_FlagDrop,
	PvPEvent_CTF_FlagReturned,
	PvPEvent_CTF_FlagCaptured,
	PvPEvent_CTF_FlagPickedUp,
	PvPEvent_CTF_FlagPass,
	PvPEvent_DOM_CapturePoint,
	PvPEvent_DOM_DefendPoint,
	PvPEvent_DOM_AttackPoint
}PvPEvent;

AUTO_ENUM;
typedef enum TriState{
	TriState_DontCare,
	TriState_Yes,
	TriState_No,
} TriState;
extern StaticDefineInt TriStateEnum[];

// This contains all the info that is included with each Entity in a GameEvent
// This is only used for outgoing Events.  Filtering events (listeners) use 
// separate fields directly in the GameEvent struct for legacy reasons.  They need to be kept in-sync.
// TODO - Make Listening events use this data structure (requires a huge data conversion)
AUTO_STRUCT AST_NO_UNRECOGNIZED;
typedef struct GameEventParticipant{
	// Critter/Encounter data
	const char *pchActorName;			AST( POOL_STRING )
	const char *pchCritterName;			AST( POOL_STRING )
	const char *pchCritterGroupName;	AST( POOL_STRING )
	S32 *piCritterTags;					AST( UNOWNED )
	const char *pchEncounterName;		AST( POOL_STRING )
	WorldScopeNamePair **eaStaticEncScopeNames;		NO_AST		// direct pointer to a static array of scope-name pairs (should never be freed)
	WorldScopeNamePair **eaEncGroupScopeNames;		NO_AST		// direct pointer to a static array of scope-name pairs (should never be freed)
	const char *pchObjectName;			AST( POOL_STRING )		// destructable object
	const char *pchRank;				AST( POOL_STRING )
	WorldRegionType eRegionType;		AST( DEFAULT(-1) )
	const char *pchNemesisType;			AST( POOL_STRING )		// type (costume) of the nemesis

	// Player data
	bool bIsPlayer;                 // Ent is a player (as opposed to a critter or encounter)

	// Both Players and Critters
	const char *pchFactionName;			AST( POOL_STRING )
	const char *pchAllegianceName;		AST( POOL_STRING )
	const char *pchClassName;			AST( POOL_STRING )
	bool bHasCredit;			// If FALSE, this entity should not qualify as a match (only included for logging)
	F32 fCreditPercentage;		// Amount of "credit" this entity should receive
	bool bHasTeamCredit;
	F32 fTeamCreditPercentage;

	// Extra data for logging (no matching is done on these fields)
	GlobalType eContainerType;
	U32 iContainerID;
	char *pchDebugName;
	char *pchAccountName;
	char *pchLogString;
	U32 iLevelReal;
	U32 iLevelCombat;

	// NO_AST pointers, used to figure out which event logs to send to
	Entity* pEnt;				NO_AST
	Team* pTeam;				NO_AST
	OldEncounter* pEncounter;	NO_AST
	GameEncounter* pEncounter2;	NO_AST

	// The Entity/Team involved, used for player- and team-scoped Events
	EntityRef entRef;			NO_AST  // The entity involved, used if tMatchSource/tMatchTarget are true
	ContainerID teamID;			NO_AST  // Used for matching if tMatchSourceTeam/TargetTeam are true

} GameEventParticipant;
extern ParseTable parse_GameEventParticipant[];
#define TYPE_parse_GameEventParticipant GameEventParticipant


// Mainly just used to serialize the source and target participants for debugging purposes.
AUTO_STRUCT AST_NO_UNRECOGNIZED;
typedef struct GameEventParticipants{
	GameEventParticipant **eaSources;
	GameEventParticipant **eaTargets;
} GameEventParticipants;
extern ParseTable parse_GameEventParticipants[];
#define TYPE_parse_GameEventParticipants GameEventParticipants


// This defines an Event in the mission system
//
// Because this is shared across projects, particularly with LogParser, it is crucial
// that all of its enums and substructs exist in all projects it is compiled into, so we add AST_NO_UNRECOGNIZED
//
// IMPORTANT:
// If you change something about this struct, please make sure that the following functions are updated:
// gameevent_WriteEventEscapedFaster
// gameevent_FastCopyListener
AUTO_STRUCT AST_NO_UNRECOGNIZED AST_IGNORE("ZoneEventName") AST_IGNORE("zoneEventState");
typedef struct GameEvent{
	// The overall type of the event
	EventType type; AST( STRUCTPARAM REQUIRED)

	// Name (used as a key for some event callbacks)
	const char *pchEventName;				AST(POOL_STRING)

	// User data (used by owner to track)
	void *pUserData;						NO_AST

    // the partition this event lives in.
	int iPartitionIdx;
	
	// Entity info - Source
	// Fields for Listening Events (filters) only.  Sent Events use the GameEventParticipant structs.

	// Scope (scope context for all applicable name fields)
	WorldScope *pScope;						NO_AST

	const char *pchSourceActorName;         AST(POOL_STRING)
	const char *pchSourceCritterName;       AST(POOL_STRING)
	const char *pchSourceCritterGroupName;  AST(POOL_STRING)
	S32 *piSourceCritterTags;				AST(NAME(SourceCritterTags), SUBTABLE(CritterTagsEnum))
	const char *pchSourceEncounterName;     AST(POOL_STRING)
	const char *pchSourceStaticEncName;     AST(POOL_STRING)
	const char *pchSourceStaticEncExclude;  AST(POOL_STRING)    // For "MatchSource No" encounters
	const char *pchSourceEncGroupName;      AST(POOL_STRING)
	const char *pchSourceObjectName;        AST(POOL_STRING)    // destructable object
	const char *pchSourceFactionName;       AST(POOL_STRING)
	const char *pchSourceAllegianceName;    AST(POOL_STRING)
	const char *pchSourceRank;               AST(POOL_STRING)
	const char *pchSourceClassName;			AST(POOL_STRING)
	const char *pchSourcePowerMode;			AST(POOL_STRING)
	WorldRegionType eSourceRegionType;		AST( DEFAULT(-1) )
	TriState tMatchSource;                  AST(NAME("MatchSource"))      // Listeners only; "Yes" if this player/encounter must be a Source for this Event
	TriState tMatchSourceTeam;              AST(NAME("MatchSourceTeam"))  // Listeners only; match on the same team
	TriState tSourceIsPlayer;               AST(NAME("SourceIsPlayer"))   // Source ent is a player (as opposed to a critter)
	EntityRef sourceEntRef;                 NO_AST				// Post-processed, added to listener if tMatchSource/tMatchSourceTeam is true

	// For outgoing Events
	GameEventParticipant **eaSources;		AST(UNOWNED)
	
	// Entity info - Target
	// Fields for Listening Events (filters) only.  Sent Events use the GameEventParticipant structs.
	const char *pchTargetActorName;         AST(POOL_STRING)
	const char *pchTargetCritterName;       AST(POOL_STRING)
	const char *pchTargetCritterGroupName;  AST(POOL_STRING)
	S32 *piTargetCritterTags;				AST(NAME(TargetCritterTags), SUBTABLE(CritterTagsEnum))
	const char *pchTargetEncounterName;     AST(POOL_STRING)
	const char *pchTargetStaticEncName;     AST(POOL_STRING)
	const char *pchTargetStaticEncExclude;  AST(POOL_STRING)    // For "MatchTarget No" encounters
	const char *pchTargetEncGroupName;      AST(POOL_STRING)
	const char *pchTargetObjectName;        AST(POOL_STRING)	// destructable object
	const char *pchTargetFactionName;       AST(POOL_STRING)
	const char *pchTargetAllegianceName;    AST(POOL_STRING)
	const char *pcTargetRank;               AST(POOL_STRING)
	const char *pchTargetClassName;			AST(POOL_STRING)
	const char *pchTargetPowerMode;			AST(POOL_STRING)
	WorldRegionType eTargetRegionType;		AST( DEFAULT(-1) )
	TriState tMatchTarget;                  AST(NAME("MatchTarget"))      // Listeners only; "Yes" if this player/encounter must be a Target for this Event
	TriState tMatchTargetTeam;              AST(NAME("MatchTargetTeam"))  // Listeners only; match on the same team
	TriState tTargetIsPlayer;               AST(NAME("TargetIsPlayer"))   // Target ent is a player (as opposed to a critter)
	EntityRef targetEntRef;                 NO_AST                        // Post-processed, added to listener if tMatchTarget/tMatchTargetTeam is true
	const char *pchNemesisType;				AST(POOL_STRING)				// The nemesis type (of costume)

	// For outgoing Events
	GameEventParticipant **eaTargets;		AST(UNOWNED)

	// Scoped fields
	const char *pchClickableName;								// Listening field
	WorldScopeNamePair **eaClickableScopeNames;			NO_AST	// Sent field; direct pointer to the clickable's static array of scope-name pairs (should never be freed)
	const char *pchClickableGroupName;							// Listening field
	WorldScopeNamePair **eaClickableGroupScopeNames;	NO_AST	// Sent field; direct pointer to the clickable group's static array of scope-name pairs (should never be freed)
	const char *pchVolumeName;									// Listening field
	WorldScopeNamePair **eaVolumeScopeNames;			NO_AST	// Sent field; direct pointer to the volume's static array of scope-name pairs (should never be freed)

	// Other names of things
	const char *pchContactName;				AST(POOL_STRING)
	const char *pchStoreName;				AST(POOL_STRING)
	const char *pchMissionRefString;		AST(POOL_STRING)
	const char *pchMissionCategoryName;		AST(POOL_STRING)
	const char *pchItemName;				AST(POOL_STRING)
	const char *pchGemName;					AST(POOL_STRING)
	const char *pchCutsceneName;
	const char *pchVideoName;
	const char *pchFSMName;					AST(POOL_STRING)		// For now this is just the name of the root FSM
	const char *pchFsmStateName;			AST(POOL_STRING)
	const char *pchItemAssignmentName;		AST(POOL_STRING)
	const char *pchItemAssignmentOutcome;	AST(POOL_STRING)
	const char *pchPowerName;
	const char *pchPowerEventName;
	const char *pchDamageType;	// In the case of healing, this is AttribType
	const char *pchDialogName;
	const char *pchNemesisName;
	const char *pchEmoteName;				AST(POOL_STRING)
	const char *pchMessage;      // A generic message string that designers can use
	const char *pchGroupProjectName;		AST(POOL_STRING)
	const char *pchAllegianceName;			AST(POOL_STRING)
	ItemCategory* eaItemCategories;
	InvBagIDs bagType;
	Vec3 pos;
	U32 iNemesisID;				// ContainerID of a Nemesis
	F32 fItemAssignmentSpeedBonus;	

	// Flags only available on UGCProjectCompleted events
	TriState tUGCFeaturedCurrently;		AST(NAME(UGCFeaturedCurrently))
	TriState tUGCFeaturedPreviously;	AST(NAME(UGCFeaturedPreviously))
	// Does this ugc project qualify for a reward?
	TriState tUGCProjectQualifiesForReward;			AST(NAME(UGCProjectQualifiesForReward))

	// TRUE if the Mission in pchMissionRefString is a root-level Mission
	bool bIsRootMission;

	// TRUE if the Mission in pchMissionRefString is the tracked mission
	bool bIsTrackedMission;

	// States and counts
	EncounterState encState;					AST( DEFAULT(-1) )
	MissionState missionState;					AST( DEFAULT(-1) )
	MissionType missionType;					AST( DEFAULT(-1) )
	MissionLockoutState missionLockoutState;	AST( DEFAULT(-1) )
	NemesisState nemesisState;					AST( DEFAULT(-1) )
	HealthState healthState;					AST( DEFAULT(-1) )
	PVPDuelVictoryType victoryType;				AST( DEFAULT(-1) )
	MinigameType eMinigameType;					AST( DEFAULT(-1) )
	PvPQueueMatchResult ePvPQueueMatchResult;	AST( DEFAULT(-1) )
	PvPEvent ePvPEvent;						AST( DEFAULT(-1) )

	// Map name
	// Internally, this is handled somewhat specially.  Any Event with a map name other than the current map just can't be listened for (eventtracker_StartTracking does nothing)
	const char *pchMapName;						AST( POOL_STRING )
	char *pchDoorKey;
 
	// "Yes" if the map's owner needs to be the source ent (for Nemesis missions)
	TriState tMatchMapOwner;                    AST(NAME(MatchMapOwner))
	EntityRef mapOwnerEntRef;                   NO_AST

	int count;  // Count of this Event (stored here for logging)

	// Extra info for Logging
	bool bUnique; // TRUE if this is the first occurance of this Event in the logfile

	// Misc

	TriState tPartOfUGCProject;                  AST(NAME("PartOfUGCProject"))

	// If this is true, don't do the normal team matching based on Team IDs.
	// Instead, look for the "bHasTeamCredit" flag on GameEventParticipant structs.
	// This causes ALL teammates to be included in the event for things like logging,
	// and allows the code that sent the Event to have greater control over who deserves
	// team credit.
	// Currently this is only used by Kills.
	// TODO - Investigate whether all Event types should use this form of matching
	bool bUseComplexTeamMatchingSource;				NO_AST
	bool bUseComplexTeamMatchingTarget;				NO_AST
	
	// the event will not be fully triggered until the whole chain of game events has been reached.

	// The next event we will listen for before (possibly) this event has been completed in full,
	// this is a pointer to another gameEvent def
	GameEvent *pChainEventDef;

	// 
	const GameEvent *pChainEvent;					AST(UNOWNED)
	const GameEvent *pRootEvent;					NO_AST


	// the time until the next game event 
	F32 fChainTime;									AST( DEFAULT(1.f) NAME("chainTime"))

	U32 bIsChainedEvent : 1;

	S32 iRewardTier;

	const char *pchScoreboardMetricName;			AST(POOL_STRING)
	
	S32 iScoreboardRank;

	const char *pchTrackedMission;					NO_AST

} GameEvent;
extern ParseTable parse_GameEvent[];
#define TYPE_parse_GameEvent GameEvent


// Holds metadata about what context a type of event requires
typedef struct GameEventTypeInfo{

	// General description of the Event type
	const char *pchDescription;

	// If TRUE, this Event won't show up in combos
	bool bDeprecated;

	// Map
	bool hasMap;

	// Source/Target info
	bool hasSourcePlayer;
	bool hasSourcePlayerTeam;
	bool hasTargetPlayer;
	bool hasTargetPlayerTeam;
	bool hasSourceCritter;
	bool hasTargetCritter;
	bool hasSourceEncounter;
	bool hasTargetEncounter;

	// Other names of things
	bool hasContactName;
	bool hasStoreName;
	bool hasMissionRefString;
	bool hasItemName;
	bool hasGemName;
	bool hasBagName;
	bool hasClickableName;
	bool hasCutsceneName;
	bool hasVideoName;
	bool hasFSMName;
	bool hasFsmStateName;
	bool hasVolumeName;
	bool hasDialogName;
	bool hasPower;
	bool hasDamageType;
	bool hasAttribType;
	bool hasMessage;
	bool hasNemesisName;
	bool hasEmoteName;
	bool hasItemAssignmentName;

	// States and counts
	bool hasEncState;
	bool hasMissionState;
	bool hasMissionLockoutState;
	bool hasNemesisState;
	bool hasHealthState;
	bool hasVictoryType;
	bool hasMinigameType;
	bool hasPvPQueueMatchResult;
	bool hasPvPEventType;
	bool hasScoreboardMetricName;
	bool hasScoreboardRank;

	bool hasItemCategories;
	bool hasUGCProject;
	bool hasUGCProjectData;
	bool hasItemAssignmentOutcome;
	bool hasItemAssignmentSpeedBonus;

} GameEventTypeInfo;

// Gets the type info for the specified event type
const GameEventTypeInfo* gameevent_GetTypeInfo(EventType type);

// Prints an event to the buffer in a user-friendly format, BUT NOT OTHERWISE USABLE
void gameevent_PrettyPrint(GameEvent *ev, char **estrBuffer);

// Prints an event to the buffer as a human-readable single line, BUT NOT OTHERWISE USABLE
void gameevent_WriteEventSingleLine(GameEvent *ev, char **estrBuffer);

// Writes an event as a string using textparser (doesn't convert back to the old event format)
void gameevent_WriteEvent(GameEvent *ev, char **estrBuffer);

// Writes an event as a string using textparser, then escapes it with estrEscape
void gameevent_WriteEventEscaped(GameEvent *ev, char **estrBuffer);

// Writes an event as a string using special code rather than general-purpose textparser.
void gameevent_WriteEventEscapedFaster(GameEvent *ev, char **estrBuffer);


// Reads an escaped GameEvent string into the given Event.  Return true on success
bool gameevent_ReadEventEscaped(GameEvent *ev, const char *eventName);

// Converts an old-style Event string like "critterDeaths.Electro" to a new Event struct.
GameEvent *gameevent_EventFromString(const char *eventName);

// Copies a listener event (devasserts if source or target ents are present)
GameEvent* gameevent_CopyListener(const GameEvent *pSrc);

// If needed, this allocates a new copy of the GameEvent that is set up correctly for player-scoping
GameEvent* gameevent_SetupPlayerScopedEvent(SA_PARAM_OP_VALID GameEvent *defEvent, SA_PARAM_OP_VALID Entity *playerEnt);

// Runs some validation on a GameEvent
bool gameevent_Validate(GameEvent *ev, char **estrError, const char *pchErrorDetailString, bool bAllowPlayerScoped);

bool gameevent_AreAssistsEnabled();


//some of the event types are put into one of these groups (the rest defaulting to NONE obviously), 
//because this is the granularity at which
//they can be turned on or off via AUTO_SETTING
// This enum needs to stay in sync with a static struct named "spbDisableLoggingCategories" in gslEventTracker.c
typedef enum gslEventLogGroup
{
	LOGGROUP_NONE,
	LOGGROUP_COMBATDAMAGEEVENT,
	LOGGROUP_COMBATHEALINGEVENT,
	LOGGROUP_COMBATKILLEVENT,
	LOGGROUP_CONTENTEVENT,
	LOGGROUP_GAMEPLAYEVENT,
	LOGGROUP_ITEMEVENT,
	LOGGROUP_POWERSDATA,
	LOGGROUP_PVP,
	LOGGROUP_REWARDDATA,

	LOGGROUP_COUNT,
} gslEventLogGroup;

#endif
