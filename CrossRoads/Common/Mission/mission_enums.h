#pragma once
GCC_SYSTEM

//moved enums here from mission_Common.h so that they can be included without getting into a neverending
//structparser cycle of doom

AUTO_ENUM;
typedef enum MissionType
{
	MissionType_Normal,
	MissionType_Perk,
	MissionType_OpenMission,
	MissionType_Nemesis,
	MissionType_NemesisArc,
	MissionType_NemesisSubArc,
	MissionType_Episode,
	MissionType_TourOfDuty,
	MissionType_AutoAvailable, // Missions available via mission computer, or random npcs
} MissionType;

AUTO_ENUM;
typedef enum TemplateVariableType
{
	TemplateVariableType_None,
	TemplateVariableType_String,
	TemplateVariableType_Message,
	TemplateVariableType_Int,
	TemplateVariableType_LongString,
	TemplateVariableType_CritterDef,
	TemplateVariableType_CritterGroup,
	TemplateVariableType_Item,
	TemplateVariableType_StaticEncounter,
	TemplateVariableType_Volume,
	TemplateVariableType_Map,
	TemplateVariableType_Neighborhood,
	TemplateVariableType_Mission,
} TemplateVariableType;


// Describes the current state of the mission
AUTO_ENUM;
typedef enum MissionState
{
	// Mission has been assigned but is not complete
	MissionState_InProgress,

	// Mission has completed successfully
	MissionState_Succeeded,

	// Mission has failed
	MissionState_Failed,

	// --- The following are not real states, they are just used for Events and logging purposes ---
	// Mission has been returned to a contact (or just completed if it doesn't require a contact)
	MissionState_TurnedIn,

	// A player dropped the Mission
	MissionState_Dropped,

	// The mission was just started
	MissionState_Started,

	MissionState_Count, EIGNORE
} MissionState;


AUTO_ENUM;
typedef enum MissionCreditType{
	
	// Normal mission: the player will receive full rewards
	MissionCreditType_Primary,

	// The player wasn't eligible for this mission and is doing it for secondary rewards
	MissionCreditType_Ineligible,

	// The player has already completed this mission and is repeating it for secondary rewards
	MissionCreditType_AlreadyCompleted,

	// Flashback Mission - Has special behavior for the Champions "Flashback" feature
	MissionCreditType_Flashback,

} MissionCreditType;
extern StaticDefineInt MissionCreditTypeEnum[];

AUTO_ENUM;
typedef enum MissionLockoutState
{
	// The Mission Lockout has started, and is still open to new players
	MissionLockoutState_Open,

	// The Mission Lockout List is locked and no new players can join
	MissionLockoutState_Locked,

	// The Mission Lockout has ended
	MissionLockoutState_Finished,

	// The list of players with credit has been updated
	MissionLockoutState_ListUpdate,

} MissionLockoutState;

// A "LockoutType" means that only a certain set of people may have the Mission 
// at any given time.  The behavior is controlled through FSMs.
AUTO_ENUM;
typedef enum MissionLockoutType
{
	// Normal Mission
	MissionLockoutType_None,

	// Only one Team can work on this Mission at a time.  When the first player starts, all teammates
	// are added to the LockoutList and the list is immediately locked.
	MissionLockoutType_Team,

	// Any player may join the Mission until the mission is locked by calling MissionLockoutBegin()
	MissionLockoutType_Open,
} MissionLockoutType;


// This tells us exactly what type of condition it is
// Switch to use this instead of bitfields so that
// the output text file is easier to edit by hand
AUTO_ENUM;
typedef enum MissionCondType
{
	MissionCondType_And, ENAMES(AllOf:)
	MissionCondType_Or, ENAMES(OneOf:)
	MissionCondType_Count, ENAMES(CountOf:)
	MissionCondType_Objective, ENAMES(Objective:)
	MissionCondType_Expression, ENAMES(Expression:)
} MissionCondType;

// If you add a new param type, make sure to update these functions: MDEParamCreateWidgetExtern
AUTO_ENUM;
typedef enum MDEParamType
{
	MDEParamType_None,  EIGNORE
	MDEParamType_Timeout,
	MDEParamType_GrantOnMap,
	MDEParamType_Waypoint,
	MDEParamType_MissionDrop,
} MDEParamType;


// Types of waypoints, i.e. types of objects that Waypoints can be attached to.
AUTO_ENUM;
typedef enum MissionWaypointType
{
	MissionWaypointType_None,
	MissionWaypointType_Clicky,
	MissionWaypointType_Volume,
	MissionWaypointType_AreaVolume,
	MissionWaypointType_NamedPoint,
	MissionWaypointType_Encounter,

	MissionWaypointType_Count, EIGNORE
} MissionWaypointType;


AUTO_ENUM;
typedef enum MinimapWaypointType
{
	MinimapWaypointType_None = -1,              // Default to no type
	MinimapWaypointType_Mission,               // Waypoints set by a MissionDef
	MinimapWaypointType_OpenMission,           // Waypoints set by an Open Mission
	MinimapWaypointType_MissionReturnContact,  // Automatic "Return to Contact" waypoints
	MinimapWaypointType_MissionRestartContact, // Automatic "Restart from Contact" waypoints
	MinimapWaypointType_TrackedContact,        // A waypoint to a specific contact that the player has "tracked" (one at a time)
	MinimapWaypointType_Landmark,
	MinimapWaypointType_SavedWaypoint,
	MinimapWaypointType_TeamCorral,				// A waypoint to a team corral
} MinimapWaypointType;

extern StaticDefineInt MinimapWaypointTypeEnum[];

AUTO_ENUM;
typedef enum MissionReturnType
{
	MissionReturnType_None,
	MissionReturnType_Message,
} MissionReturnType;


// Types of mission drops
AUTO_ENUM;
typedef enum MissionDropTargetType
{
	MissionDropTargetType_Critter,
	MissionDropTargetType_Group,
	MissionDropTargetType_Actor,
	MissionDropTargetType_EncounterGroup,
	MissionDropTargetType_Nemesis,          // Nemesis missions only; drops for this player's nemesis
	MissionDropTargetType_NemesisMinion,

	MissionDropTargetType_Count, EIGNORE
} MissionDropTargetType;

// Describes when the MissionDrop should occur
AUTO_ENUM;
typedef enum MissionDropWhenType
{
	MissionDropWhenType_DuringMission,
	MissionDropWhenType_PreMission,
} MissionDropWhenType;

AUTO_ENUM;
typedef enum PerkNotificationType
{
	PerkNotificationType_Discovered,
	PerkNotificationType_Completed,
} PerkNotificationType;

AUTO_ENUM;
typedef enum MDEShowCount
{
	MDEShowCount_Normal     = 0,
	MDEShowCount_Show_Count = 1,
	MDEShowCount_Only_Count = 2,
	MDEShowCount_Count_Down = 3,
	MDEShowCount_Percent	= 4,
} MDEShowCount;
