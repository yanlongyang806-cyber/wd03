#pragma once
GCC_SYSTEM

#if !defined(GENESIS_ALLOW_OLD_HEADERS)
#error Including this file was probably a mistake.  UGC should include the appropriate gslUGC*.h files.
#endif

#include "worldgrid.h"
#include "ReferenceSystem.h"
#include "wlGenesis.h"
#include "wlGenesisMissionsGameStructs.h"
#include "WorldLibEnums.h"

typedef struct AIAnimList AIAnimList;
typedef struct DoorTransitionSequenceDef DoorTransitionSequenceDef;
typedef struct GenesisCheckedAttrib GenesisCheckedAttrib;
typedef struct GenesisFSM GenesisFSM;
typedef struct GenesisFSMChain GenesisFSMChain;
typedef struct GenesisFSMChainLink GenesisFSMChainLink;
typedef struct GenesisInstancedObjectParams GenesisInstancedObjectParams;
typedef struct GenesisMapDescription GenesisMapDescription;
typedef struct GenesisMissionAction GenesisMissionAction;
typedef struct GenesisMissionChallengeRequirements GenesisMissionChallengeRequirements;
typedef struct GenesisMissionExtraVolume GenesisMissionExtraVolume;
typedef struct GenesisMissionRoomRequirements GenesisMissionRoomRequirements;
typedef struct GenesisMissionVolumeRequirements GenesisMissionVolumeRequirements;
typedef struct GenesisObject GenesisObject;
typedef struct GenesisProceduralObjectParams GenesisProceduralObjectParams;
typedef struct GenesisWhenExternalChallenge GenesisWhenExternalChallenge;
typedef struct GenesisWhenExternalPrompt GenesisWhenExternalPrompt;
typedef struct GenesisWhenExternalRoom GenesisWhenExternalRoom;
typedef struct InteractionDef InteractionDef;
typedef struct MissionCategory MissionCategory;
typedef struct MissionWaypoint MissionWaypoint;
typedef struct ParseTable ParseTable;
typedef struct PetContactList PetContactList;
typedef struct PlayerCostume PlayerCostume;
typedef struct WorldGameActionBlock WorldGameActionBlock;

// --------------------------------------------------------------------------
// Map Description Mission Data
// --------------------------------------------------------------------------

AUTO_ENUM;
typedef enum GenesisMissionExitFrom
{
	GenesisMissionExitFrom_Entrance,
	GenesisMissionExitFrom_DoorInRoom,		ENAMES(ExtraDoorInRoom DoorInRoom)
	GenesisMissionExitFrom_Anywhere,
	GenesisMissionExitFrom_Challenge,
} GenesisMissionExitFrom;
extern StaticDefineInt GenesisMissionExitFromEnum[];

AUTO_ENUM;
typedef enum GenesisMissionGenerationType
{
	GenesisMissionGenerationType_PlayerMission,
	GenesisMissionGenerationType_OpenMission,
	GenesisMissionGenerationType_OpenMission_NoPlayerMission,
} GenesisMissionGenerationType;
extern StaticDefineInt GenesisMissionGenerationTypeEnum[];

AUTO_ENUM;
typedef enum GenesisMissionGrantType 
{
	GenesisMissionGrantType_MapEntry,
	GenesisMissionGrantType_RandomNPC,
	GenesisMissionGrantType_Contact,
	GenesisMissionGrantType_Manual,
} GenesisMissionGrantType;
extern StaticDefineInt GenesisMissionGrantTypeEnum[];

AUTO_ENUM;
typedef enum GenesisMissionTurnInType 
{
	GenesisMissionTurnInType_Automatic,
	GenesisMissionTurnInType_GrantingContact,
	GenesisMissionTurnInType_DifferentContact,
} GenesisMissionTurnInType;
extern StaticDefineInt GenesisMissionTurnInTypeEnum[];

AUTO_ENUM;
typedef enum GenesisMissionFailType
{
	GenesisMissionFailType_Never,
	GenesisMissionFailType_Timeout,
} GenesisMissionFailType;
extern StaticDefineInt GenesisMissionFailTypeEnum[];

AUTO_ENUM;
typedef enum GenesisWhenType
{
	// When conditions that shouldn't be used in missions.
	GenesisWhen_MapStart,				ENAMES(MapStart MapEntry)  // From the start
	GenesisWhen_Manual,					// Not allowed by default, other places may override
	GenesisWhen_MissionComplete,		// Not allowed until the primary mission succeeds.
	GenesisWhen_MissionNotInProgress,	// Triggers at MapEntry unless the mission has been started
	GenesisWhen_ObjectiveComplete,		// Names are objectives, complete one to trigger
	GenesisWhen_ObjectiveCompleteAll,	// Names are objectives, complete all to trigger
	GenesisWhen_ObjectiveInProgress,	// Names are objectives, have any active to trigger
	GenesisWhen_ChallengeAdvance,		// Names are challenges, complete one to trigger

	// When conditions that are always safe
	GenesisWhen_PromptStart,			// Names are prompts, first one to start triggers
	GenesisWhen_PromptComplete,			// Names are prompts, first one to complete triggers
	GenesisWhen_PromptCompleteAll,		EIGNORE // Names are prompts, complete all to trigger
	GenesisWhen_ContactComplete,		ENAMES(ContactComplete TalkToContact) // Names are ContactDefs, first one to complete triggers
	GenesisWhen_ChallengeComplete,		ENAMES(ChallengeComplete CompleteChallenge) // Names are challenges, complete all to trigger
	GenesisWhen_RoomEntry,				ENAMES(RoomEntry ReachLocation)  // Names are rooms, first one entered triggers
	GenesisWhen_RoomEntryAll,			ENAMES(RoomEntryAll) // Names are rooms, enter *all* to trigger
	GenesisWhen_CritterKill,			ENAMES(CritterKill KillCritter KillCritterGroup) // Names are CritterDefs and CritterGroups, kill a number of them to trigger
	GenesisWhen_ItemCount,				ENAMES(ItemCount CollectItems) // Names are ItemDefs, have a certain number of them in your inventory to succeed

	// When conditions added only for UGC
	GenesisWhen_ExternalOpenMissionComplete, EIGNORE
	GenesisWhen_ExternalChallengeComplete, EIGNORE
	GenesisWhen_ExternalPromptComplete, EIGNORE
	GenesisWhen_ExternalRoomEntry, EIGNORE
	GenesisWhen_ExternalMapStart, EIGNORE
	GenesisWhen_ExternalRewardBoxLooted, EIGNORE
	GenesisWhen_RewardBoxLooted, EIGNORE
	GenesisWhen_ReachChallenge, EIGNORE

	// When conditions that exist only for missions
	GenesisWhen_AllOf,					// All child objectives get granted simultaneously, complete
										// all of them to trigger
	GenesisWhen_InOrder,				// Child objectives get granted in order, complete all of
										// them to trigger
	GenesisWhen_Branch,					// All child objectives get granted simultaneously, complete
										// one to trigger, and when one succeeds all the others
										// immediately fail
} GenesisWhenType;
extern StaticDefineInt GenesisWhenTypeEnum[];

AUTO_STRUCT;
typedef struct GenesisWhenRoom
{
	char *roomName;									AST(STRUCTPARAM NAME("RoomName"))
	char *layoutName;								AST(STRUCTPARAM NAME("LayoutName"))
} GenesisWhenRoom;
extern ParseTable parse_GenesisWhenRoom[];
#define TYPE_parse_GenesisWhenRoom GenesisWhenRoom

AUTO_STRUCT;
typedef struct GenesisWhenPromptBlock
{
	char* promptName;								AST(NAME("PromptName"))
	char* blockName;								AST(NAME("BlockName"))
} GenesisWhenPromptBlock;
extern ParseTable parse_GenesisWhenPromptBlock[];
#define TYPE_parse_GenesisWhenPromptBlock GenesisWhenPromptBlock

AUTO_STRUCT;
typedef struct GenesisWhen
{
	GenesisWhenType type;							AST(NAME("whenType", "ShowWhen", "SpawnWhen"))

	// NOTE: Stopgap solution.  The best solution would be to have a way to nest Whens. 
	bool bNot;										AST(NAME("Not"))
	GenesisCheckedAttrib* checkedAttrib;			AST(NAME("CheckedAttrib"))

	char **eaChallengeNames;						AST(NAME("WhenChallengeName", "ShowWhenChallengeName", "SpawnWhenChallengeName"))
	int iChallengeNumToComplete;					AST(NAME("WhenChallengeNumToComplete", "ShowWhenChallengeNumToComplete"))

	GenesisWhenRoom **eaRooms;						AST(NAME("WhenRoomName", "ShowWhenRoomName", "SpawnWhenRoomName"))								
	char **eaObjectiveNames;						AST(NAME("WhenObjectiveName", "ShowWhenObjectiveName", "SpawnWhenObjectiveName"))
	char **eaPromptNames;							AST(NAME("WhenPromptName", "ShowWhenPromptName", "SpawnWhenPromptName"))
	GenesisWhenPromptBlock** eaPromptBlocks;		AST(NAME("WhenPromptBlock"))
	char *pcPromptChallengeName;					AST(NAME("WhenPromptChallengeName"))
	char **eaContactNames;							AST(NAME("WhenContactName"))
	
	char **eaCritterDefNames;						AST(NAME("WhenCritterDefName"))
	char **eaCritterGroupNames;						AST(NAME("WhenCritterGroupName"))
	int iCritterNumToComplete;						AST(NAME("WhenCritterNumToComplete"))
	const char **eaExternalMissionNames;			AST(NAME("WhenExternalMission") POOL_STRING)
	const char **eaExternalMapNames;				AST(NAME("WhenExternalMap"))
	bool bAnyCrypticMap;							AST(NAME("WhenAnyCrypticMap"))
	GenesisWhenExternalChallenge** eaExternalChallenges; AST(NAME("WhenExternalChallenge"))
	GenesisWhenExternalRoom **eaExternalRooms; AST(NAME("WhenExternalRoom"))
	GenesisWhenExternalPrompt **eaExternalPrompts;	AST(NAME("WhenExternalPrompt"))

	char **eaItemDefNames;							AST(NAME("WhenItemDefName"))
	int iItemCount;									AST(NAME("WhenItemCount"))
} GenesisWhen;
extern ParseTable parse_GenesisWhen[];
#define TYPE_parse_GenesisWhen GenesisWhen

AUTO_STRUCT;
typedef struct GenesisCheckedAttrib
{
	const char* name;								AST(NAME("Name") POOL_STRING)
	bool bNot;										AST(NAME("Not"))
	const char *astrItemName;						AST(NAME("ItemName") POOL_STRING) // For attrib "PlayerHasItem"
} GenesisCheckedAttrib;
extern ParseTable parse_GenesisCheckedAttrib[];
#define TYPE_parse_GenesisCheckedAttrib GenesisCheckedAttrib

AUTO_ENUM;
typedef enum GenesisMissionCostumeType
{
	GenesisMissionCostumeType_Specified,
	GenesisMissionCostumeType_PetCostume,
	GenesisMissionCostumeType_CritterGroup,
	GenesisMissionCostumeType_Player,
} GenesisMissionCostumeType;
extern StaticDefineInt GenesisMissionCostumeTypeEnum[];

AUTO_STRUCT;
typedef struct GenesisMissionCostume
{
	GenesisMissionCostumeType eCostumeType;			AST(NAME("CostumeType", "UsePetCostume"))

	// CostumeType Specified
	REF_TO(PlayerCostume) hCostume;					AST(NAME("Costume"))

	// CostumeType PetCostume
	REF_TO(PetContactList) hPetCostume;				AST(NAME("PetCostume"))

	// CostumeType CritterGroup
	ContactMapVarOverrideType eCostumeCritterGroupType;	AST(NAME("CostumeCritterGroupType"))
	// exactly one of these will be specified
	REF_TO(CritterGroup) hCostumeCritterGroup;		AST(NAME("CostumeCritterGroup"))
	char* pchCostumeMapVar;							AST(NAME("CostumeMapVar"))
	char* pchCostumeIdentifier;						AST(NAME("CostumeIdentifier"))
} GenesisMissionCostume;
extern ParseTable parse_GenesisMissionCostume[];
#define TYPE_parse_GenesisMissionCostume GenesisMissionCostume

AUTO_STRUCT;
typedef struct GenesisMissionGrant_Contact {
	char* pcOfferText;								AST(NAME("OfferText"))
	char* pcInProgressText;							AST(NAME("InProgressText"))
} GenesisMissionGrant_Contact;
extern ParseTable parse_GenesisMissionGrant_Contact[];
#define TYPE_parse_GenesisMissionGrant_Contact GenesisMissionGrant_Contact

AUTO_STRUCT;
typedef struct GenesisMissionTurnIn_Contact {
	char* pcCompletedText;							AST(NAME("CompletedText"))
	char* pcMissionReturnText;						AST(NAME("MissionReturnText"))
} GenesisMissionTurnIn_Contact;
extern ParseTable parse_GenesisMissionTurnIn_Contact[];
#define TYPE_parse_GenesisMissionTurnIn_Contact GenesisMissionTurnIn_Contact

AUTO_STRUCT;
typedef struct GenesisMissionGrantDescription
{
	GenesisMissionGrantType eGrantType;				AST(NAME("GrantType"))
	GenesisMissionGrant_Contact *pGrantContact;		AST(NAME("GrantContact"))

	GenesisMissionTurnInType eTurnInType;			AST(NAME("TurnInType"))
	GenesisMissionTurnIn_Contact *pTurnInContact;	AST(NAME("TurnInContact"))

	GenesisMissionFailType eFailType;				AST(NAME("FailType"))
	int iFailTimeoutSeconds;						AST(NAME("FailTimeoutSeconds"))

	bool bRepeatable;								AST(NAME("CanRepeat"))
	F32 fRepeatCooldownHours;						AST(NAME("RepeatCooldownHours"))
	F32 fRepeatCooldownHoursFromStart;				AST(NAME("RepeatCooldownHoursFromStart"))
	
	// Number of times that the mission can be run before it enter cooldown. Only values > 1 will
	// produce behavior different from that of pre-fRepeatCooldownTimes behavior. 
	// This is used for both repeat cooldown and cooldown from start
	U32 iRepeatCooldownCount;						AST( NAME("RepeatCooldownCount"))

	// Used with iRepeatCooldownCount
	// If true then mission cooldown start / complete times are set to the nearest block based on cooldown time
	// Example 24 in fRepeatCooldownHours would result in mission cooldwon times being started at 12:00am and ending 24 hours later
	// When it is past the last block then its in a new cooldown block (count starts over again).
	bool bRepeatCooldownBlockTime;				AST( NAME("RepeatCooldownBlockTime"))

	const char **eaRequiresMissions;				AST(NAME("RequiresMission"))
} GenesisMissionGrantDescription;
extern ParseTable parse_GenesisMissionGrantDescription[];
#define TYPE_parse_GenesisMissionGrantDescription GenesisMissionGrantDescription

AUTO_STRUCT;
typedef struct GenesisMissionOpenMissionDescription
{
	char *pcPlayerSpecificDisplayName;				AST(NAME("DisplayName"))
	char *pcPlayerSpecificShortText;				AST(NAME("UIString", "ShortText"))
} GenesisMissionOpenMissionDescription;
extern ParseTable parse_GenesisMissionOpenMissionDescription[];
#define TYPE_parse_GenesisMissionOpenMissionDescription GenesisMissionOpenMissionDescription

AUTO_STRUCT AST_IGNORE("ReturnPromptCostume") AST_IGNORE("ExitPromptCostume") AST_IGNORE("ContinuePromptCostume") AST_IGNORE("ExitWhen");
typedef struct GenesisMissionStartDescription
{
	char *pcStartLayout;							AST(NAME("StartLayout"))
	char *pcStartRoom;								AST(NAME("StartRoom"))
	bool bHasEntryDoor;								AST(NAME("HasEntryDoor"))
	F32 *eaStartPosOverride;						AST(NAME("StartPosOverride")) // Only used in fully specified layout mode
	REF_TO(DoorTransitionSequenceDef) hStartTransitionOverride; AST(NAME("StartTransitionOverride"))

	GenesisMissionExitFrom eExitFrom;				AST(NAME("ExitFrom"))
	char *pcExitLayout;								AST(NAME("ExitLayout"))
	char *pcExitRoom;								AST(NAME("ExitRoom")) // This only applies if exit from is a room, or when exterior has a shape
	char *pcExitChallenge;							AST(NAME("ExitChallenge")) // This only applies if exit from is a challenge
	REF_TO(DoorTransitionSequenceDef) hExitTransitionOverride; AST(NAME("ExitTransitionOverride"))
	GenesisMissionCostume exitPromptCostume;		AST(NAME("ExitPromptCostume2"))

	bool bContinue;									AST(NAME("Continue"))
	GenesisMissionExitFrom eContinueFrom;			AST(NAME("ContinueFrom"))
	char *pcContinueLayout;							AST(NAME("ContinueLayout"))
	char *pcContinueRoom;							AST(NAME("ContinueRoom")) // This only applies if continue from is a room, or when exterior has a shape
	char *pcContinueChallenge;						AST(NAME("ContinueChallenge")) // This only applies if continue from is a challenge
	char *pcContinueMap;							AST(NAME("ContinueMap"))
	WorldVariable **eaContinueVariables;			AST(NAME("ContinueVariable"))
	REF_TO(DoorTransitionSequenceDef) hContinueTransitionOverride; AST(NAME("ContinueTransitionOverride"))
	
	GenesisMissionCostume continuePromptCostume;	AST(NAME("ContinuePromptCostume2"))
	char *pcContinuePromptButtonText;				AST(NAME("ContinuePromptButtonText"))
	char *pcContinuePromptCategoryName;				AST(NAME("ContinuePromptCategoryName"))
	WorldOptionalActionPriority eContinuePromptPriority; AST(NAME("ContinuePromptPriority"))
	char *pcContinuePromptTitleText;				AST(NAME("ContinuePromptTitleText"))
	char **eaContinuePromptBodyText;				AST(NAME("ContinuePromptBodyText"))


	// These apply if the mission is to have an entrance from some
	// other map, but only when the mission is granted.
	char *pcEntryFromMapName;						AST(NAME("EntryFromMapName"))
	char *pcEntryFromInteractableName;				AST(NAME("EntryFromInteractableName"))
} GenesisMissionStartDescription;
extern ParseTable parse_GenesisMissionStartDescription[];
#define TYPE_parse_GenesisMissionStartDescription GenesisMissionStartDescription


AUTO_STRUCT;
typedef struct GenesisMissionChallengeEncounter
{
	GenesisPatrolType ePatrolType;					AST(NAME("PatrolType"))
	char *pcPatOtherRoomName;						AST(NAME("PatOtherRoom"))
	GenesisChallengePlacement ePatPlacement;		AST(NAME("PatPlacement"))
	char *pcPatRefChallengeName;					AST(NAME("PatRefChallengeName"))
	
	GenesisSpacePatrolType eSpacePatrolType;		AST(NAME("SpacePatrolType")) //Space works differently
	char *pcSpacePatRefChallengeName;				AST(NAME("SpacePatRefChallengeName"))
} GenesisMissionChallengeEncounter;
extern ParseTable parse_GenesisMissionChallengeEncounter[];
#define TYPE_parse_GenesisMissionChallengeEncounter GenesisMissionChallengeEncounter

AUTO_STRUCT;
typedef struct GenesisMissionChallengeClickie
{
	// The interaction def used for the clickie
	REF_TO(InteractionDef) hInteractionDef;			AST(NAME("InteractionDef"))

	// The text shown above the clickie
	char *strVisibleName;							AST(NAME("VisibleName"))

	// The text shown to interacted with the clickie.
	char *pcInteractText;							AST(NAME("InteractText"))

	// The text shown if the clickie has been successfully interacted with
	char *pcSuccessText;							AST(NAME("SuccessText"))

	// The text shown if the clickie has failed to interact successfully
	char *pcFailureText;							AST(NAME("FailureText"))

	// The animation the player sees while interacting with the clickie
	REF_TO(AIAnimList) hInteractAnim;				AST(NAME("InteractAnim"))

	// The name of the reward table that is dropped when this clickie succeeds
	REF_TO(RewardTable) hRewardTable;				AST(NAME("RewardTable"))

	// Take the checked attrib item from the player's inventory
	bool bConsumeSuccessItem;						AST(NAME("ConsumeSuccessItem"))

	// Indicates we need special door logic
	bool bIsUGCDoor;								AST(NAME("IsUGCDoor"))
} GenesisMissionChallengeClickie;
extern ParseTable parse_GenesisMissionChallengeClickie[];
#define TYPE_parse_GenesisMissionChallengeClickie GenesisMissionChallengeClickie

AUTO_STRUCT;
typedef struct GenesisMissionTrap
{
	bool bOnVolumeEntered;									AST(NAME("OnVolumeEntered"))
	char *pcPowerName;										AST(NAME("PowerName"))
	char *pcEmitterChallenge;								AST(NAME("EmitterChallenge"))
	char *pcTargetChallenge;								AST(NAME("TargetChallenge"))
} GenesisMissionTrap;
extern ParseTable parse_GenesisMissionTrap[];
#define TYPE_parse_GenesisMissionTrap GenesisMissionTrap

AUTO_STRUCT AST_IGNORE("Level") AST_FIXUPFUNC(fixupGenesisMissionChallenge);
typedef struct GenesisMissionChallenge 
{
	// The name of the challenge
	char *pcName;									AST(NAME("Name"))

	// Layout the challenge is in
	char *pcLayoutName;								AST(NAME("LayoutName"))

	// The location of the challenge
	// If multiple rooms specified, it randomly chooses among rooms
	char **eaRoomNames;								AST(NAME("RoomName"))

	// Which challenge to place.
	// The type acts as a filter if present.
	GenesisChallengeType eType;						AST(NAME("Type"))
	GenesisTagOrName eSpecifier;					AST(NAME("ChallengeSpecifier"))
	bool bHeterogenousObjects;						AST(NAME("HeterogenousObjects"))
	char **eaChallengeTags;							AST(NAME("ChallengeTags2"))
	char *pchOldChallengeTags;						AST(NAME("ChallengeTags","Tags"))
	char *pcChallengeName;							AST(NAME("SpecificChallenge"))

	// How many copies of the challenge to place
	// If zero, then it means one.
	int iCount;										AST(NAME("Count"))

	// How many of the copies to spawn
	// If zero, then all of them.  It's an error to have this bigger than iCount.
	int iNumToSpawn;								AST(NAME("NumToSpawn"))

	// Rotational reference point for this challenge
	GenesisChallengeFacing eFacing;					AST(NAME("Facing"))

	// Rotation snapping.  If set, then only place down rotation at this level 
	float fRotationIncrement;						AST(NAME("RotationIncrement"))

	// Distance to keep other challenges that have exclude distances and are of similar challenge type
	float fExcludeDist;								AST(NAME("ExcludeDist"))

	// Positional attributes for this challenge
	GenesisChallengePlacement ePlacement;			AST(NAME("Placement"))

	// Location name that facing/placement refer to
	char *pcRefPrefabLocation;						AST(NAME("RefPrefabLocation"))

	// Challenge that facing/placement refer to
	char *pcRefChallengeName;						AST(NAME("RefChallengeName"))

	// Logical name to give the internal spawnpoint in this challenge
	char *pcStartSpawnName;							AST(NAME("ChallengeSpawnName"))

	// Note: I need a name here to correct AutoStruct's stupid casing.
	// But it can't be "SpawnWhen" or else it will override the
	// EMBEDDED_FLAT
	GenesisWhen spawnWhen;							AST(EMBEDDED_FLAT NAME("SpawnWhen2"))

	// When true, this clickie is visible.  Note that if a clickie is
	// not visible, then it will not be able to be interacted with.
	GenesisWhen clickieVisibleWhen;					AST(NAME("ClickieVisibleWhen"))

	// If set, successfully completing this challenge requires a having a checked attribute
	GenesisCheckedAttrib* succeedCheckedAttrib;		AST(NAME("SucceedCheckedAttrib"))

	// Properties that are only valid for encounters/clickies.  Cannot
	// contain DisplayMessages.
	GenesisMissionChallengeClickie* pClickie;		AST(NAME("Clickie"))
	GenesisMissionChallengeEncounter* pEncounter;	AST(NAME("Encounter"))
	GenesisContactParams *pContact;					AST(NAME("Contact"))

	// Always make sure this challenge gets a name, so it can be referenced externally
	bool bForceNamedObject;							AST(NAME("ForceNamedObject"))

    // old format support
	GenesisMissionChallengeEncounter oldEncounter;	AST(EMBEDDED_FLAT)

	F32 *eaPositions;								AST(NAME("ExactPositions"))
	bool bAbsolutePos;								AST(NAME("AbsolutePosition"))
	bool bSnapRayCast;								AST(NAME("SnapRayCast"))
	bool bSnapToGeo;								AST(NAME("SnapToGeo"))
	bool bSnapNormal;								AST(NAME("SnapNormal"))
	bool bLegacyHeightCheck;						AST(NAME("LegacyHeightCheck"))
	
	WorldPatrolProperties *pPatrol;					AST(NAME("Patrol"))

	// For challenge types with child objects to be specifically overridden
	GenesisPlacementChildParams **eaChildren;		AST(NAME("Child"))
	
	// If true, then eaChildren specifically applies to the direct
	// GroupDef children.  Otherwise it finds an applies to the first
	// found Encounter that is in this GroupDef.
	bool bChildrenAreGroupDefs;						AST(NAME("ChildrenAreGroupDefs"))

	// Currently only used for planets/space objects
	GenesisObjectVolume *pVolume;					AST(NAME("Volume"))

	// Object platforms only apply to objects in the same platform group
	int iPlatformGroup;								AST(NAME("PlatformGroup")) // This object's group
	int iPlatformParentGroup;						AST(NAME("PlatformParentGroup")) // The parent group for this object
	int iPlatformParentLevel;						AST(NAME("PlatformParentLevel")) // The platform level in the parent object

	// Detail selectors
	GenesisRoomDetail **eaRoomDetails;				AST(NAME("RoomDetail"))
	GenesisRoomDoorSwitch **eaRoomDoors;			AST(NAME("RoomDoor"))

	// Traps
	GenesisMissionTrap **eaTraps;					AST(NAME("Trap")) // Traps triggered by this object
	char *pcTrapObjective;							AST(NAME("TrapObjective")) // Objective that enables this trap

	// TODO: Possibly add Contact properties here that would
	//       be provided to the challenge.
} GenesisMissionChallenge;
extern ParseTable parse_GenesisMissionChallenge[];
#define TYPE_parse_GenesisMissionChallenge GenesisMissionChallenge

/// Transmogrified version of a challenge
AUTO_STRUCT;
typedef struct GenesisMissionZoneChallenge
{
	char* pcName;									AST(NAME("Name"))
	GenesisChallengeType eType;						AST(NAME("Type"))
	int iNumToComplete;								AST(NAME("NumToComplete"))
	char *pcLayoutName;								AST(NAME("LayoutName"))

	// Note: I need a name here to correct AutoStruct's stupid casing.
	// But it can't be "SpawnWhen" or else it will override the
	// EMBEDDED_FLAT
	GenesisWhen spawnWhen;							AST(EMBEDDED_FLAT NAME("SpawnWhen2"))

	// When true, this clickie is visible.  Note that if a clickie is
	// not visible, then it will not be able to be interacted with.
	GenesisWhen clickieVisibleWhen;					AST(NAME("ClickieVisibleWhen"))

	// If set, successfully completing this challenge requires having a checked attribute
	GenesisCheckedAttrib* succeedCheckedAttrib;		AST(NAME("SucceedCheckedAttrib"))

    // Properties that are only valid for encounters/clickies
	// contain DisplayMessages.
	GenesisMissionChallengeClickie* pClickie;		AST(NAME("Clickie"))
	GenesisContactParams *pContact;					AST(NAME("Contact"))
	bool bForceNamedObject;							AST(NAME("ForceNamedObject"))
	GenesisPlacementChildParams **eaChildren;		AST(NAME("Child"))
	bool bChildrenAreGroupDefs;						AST(NAME("ChildrenAreGroupDefs"))
	bool bIsVolume;									AST(NAME("IsVolume"))

	GenesisMissionTrap **eaTraps;					AST(NAME("Trap"))
	char *pcTrapObjective;							AST(NAME("TrapObjective"))

	GenesisRuntimeErrorContext* pSourceContext;		AST(NAME("SourceContext"))
} GenesisMissionZoneChallenge;
extern ParseTable parse_GenesisMissionZoneChallenge[];
#define TYPE_parse_GenesisMissionZoneChallenge GenesisMissionZoneChallenge

/// External version of a challenge, stored directly in a GenesisWhen.
///
/// This is needed if the chalenge is on another map.
AUTO_STRUCT;
typedef struct GenesisWhenExternalChallenge
{
	char* pcMapName;								AST(NAME("MapName"))
	char* pcName;									AST(NAME("Name"))
	GenesisChallengeType eType;						AST(NAME("Type"))

	GenesisMissionChallengeClickie* pClickie;		AST(NAME("Clickie"))
} GenesisWhenExternalChallenge;
extern ParseTable parse_GenesisWhenExternalChallenge[];
#define TYPE_parse_GenesisWhenExternalChallenge GenesisWhenExternalChallenge


/// External version of a "room", stored directly in a GenesisWhen.
///
/// This is needed if the room is on another map.
AUTO_STRUCT;
typedef struct GenesisWhenExternalRoom
{
	char* pcMapName;								AST(NAME("MapName"))
	char* pcName;									AST(NAME("Name"))
} GenesisWhenExternalRoom;
extern ParseTable parse_GenesisWhenExternalRoom[];
#define TYPE_parse_GenesisWhenExternalRoom GenesisWhenExternalRoom

/// External version of a "prompt", stored directly in a GenesisWhen.
AUTO_STRUCT;
typedef struct GenesisWhenExternalPrompt
{
	char* pcContactName;
	char* pcPromptName;

	// If you want a waypoint, you need to fill these out to place a waypoint marker
	char* pcEncounterName;
	char* pcEncounterMapName;
} GenesisWhenExternalPrompt;
extern ParseTable parse_GenesisWhenExternalPrompt[];
#define TYPE_parse_GenesisWhenExternalPrompt GenesisWhenExternalPrompt

typedef struct GenesisMissionPrompt GenesisMissionPrompt;

AUTO_STRUCT;
typedef struct GenesisMissionPromptAction
{
	// Text for the prompt window button
	char *pcText;									AST(NAME("Text"))

	// Style for the button being generated
	const char* astrStyleName;						AST(NAME("Style") POOL_STRING)

	// MJF TODO: Remove legacy data when not used any more.  Superceded by multi-blocks in a prompt.
	// Optional next prompt to show
	char *pcNextPromptName;							AST(NAME("NextPromptName"))

    // Grant the mission
	bool bGrantMission;								AST(NAME("GrantMission"))

    // Do not fire off "PromptSuccess" events
	bool bDismissAction;							AST(NAME("DismissAction"))

	// Actions to perform when action it selected
	WorldGameActionBlock actionBlock;				AST(NAME("Actions"))

	// Optional next block to show, if set then this won't fire off prompt success events.
	char *pcNextBlockName;							AST(NAME("NextBlockName"))

	// When true, this action is available.
	GenesisWhen when;								AST(NAME("When"))

	// If true then if you do not pass the skill check, a "disabled" version of the prompt should appear
	GenesisCheckedAttrib* enabledCheckedAttrib;		AST(NAME("EnabledCheckedAttrib"))
} GenesisMissionPromptAction;
extern ParseTable parse_GenesisMissionPromptAction[];
#define TYPE_parse_GenesisMissionPromptAction GenesisMissionPromptAction

AUTO_STRUCT;
typedef struct GenesisMissionPromptBlock
{
	// name for this block, only matters in named blocks
	char* name;										AST(NAME("BlockName"))

	// Costume for head shot
	GenesisMissionCostume costume;					AST(NAME("Costume2") EMBEDDED_FLAT)

	// Costume style
	char* pchHeadshotStyle;							AST(NAME("HeadshotStyle"))
 
	// Cutscene for the prompt (used by NNO)
	REF_TO(CutsceneDef) hCutsceneDef;				AST(NAME("CutsceneDef"))

	// Animation for contact in cutscene (used by NNO)
	REF_TO(AIAnimList) hAnimList;					AST(NAME("AnimList"))

	// Text for the prompt window
	char *pcTitleText;								AST(NAME("TitleText"))
	char **eaBodyText;								AST(NAME("BodyText"))

	// Phrase to say
	char *pcPhrase;									AST(NAME("Phrase"))
	
	// Flags which specify specialized behavior for this special dialog block
	SpecialDialogFlags eDialogFlags;				AST(NAME("DialogFlags") FLAGS)

	// Options to show on the prompt window
	GenesisMissionPromptAction **eaActions;		AST(NAME("Action"))
} GenesisMissionPromptBlock;
extern ParseTable parse_GenesisMissionPromptBlock[];
#define TYPE_parse_GenesisMissionPromptBlock GenesisMissionPromptBlock

AUTO_STRUCT;
typedef struct GenesisMissionPrompt
{
	// Name of the Prompt
	char *pcName;									AST(NAME("Name"))

	// Layout the prompt is in
	char *pcLayoutName;								AST(NAME("LayoutName"))

	// If set, this prompt should apply to some map *other* the the
	// one being generated.  Then pcChallengeName is the logical name
	// of the contact.
	char **eaExternalMapNames;						AST(NAME("ExternalMapName"))

	// Challenge of the critter who will use this Contact (NULL means use an optional action)
	char *pcChallengeName;

	// If set, this prompt should apply to some contact *other* than
	// the one being generated.  pcExternalMapName and pcChallengeName
	// will be ignored then.
	const char* pcExternalContactName;				AST(POOL_STRING NAME("ExternalContactName"))

	// Defines when the prompt gets shown
	GenesisWhen showWhen;							AST(EMBEDDED_FLAT NAME("ShowWhen"))

    // Optional action fields
	bool bOptional;									AST(NAME("OptionalPrompt"))
	char *pcOptionalButtonText;						AST(NAME("OptionalButtonText"))
	char *pcOptionalCategoryName;					AST(NAME("OptionalCategoryName"))
	WorldOptionalActionPriority eOptionalPriority;	AST(NAME("OptionalPriority"))
	bool bOptionalAutoExecute;						AST(NAME("OptionalAutoExecute"))
	bool bOptionalHideOnComplete;					AST(NAME("OptionalHideOnComplete"))

	// MJF TODO: Remove legacy data when not used any more.  Superceded by multi-blocks in a prompt.
	char *pcOptionalHideOnCompletePrompt;			AST(NAME("OptionalHideOnCompletePrompt"))

	// blocks
	GenesisMissionPromptBlock sPrimaryBlock;		AST(EMBEDDED_FLAT)
	GenesisMissionPromptBlock** namedBlocks;		AST(NAME("Block"))
} GenesisMissionPrompt;
extern ParseTable parse_GenesisMissionPrompt[];
#define TYPE_parse_GenesisMissionPrompt GenesisMissionPrompt

AUTO_STRUCT AST_IGNORE("ActorIndex");
typedef struct GenesisFSM
{
	const char *pcName;						AST(NAME("Name") POOL_STRING STRUCTPARAM KEY)
	const char *pcFileName;					AST(NAME("Filename") CURRENTFILE)
	char *pcChallengeLogicalName;			AST(NAME("ChallengeLogicalName"))

	GenesisWhen activeWhen;

	WorldVariableDef **eaVarDefs;			AST(NAME("VarDef"))

	char *pcFSMName;						AST(NAME("FSMName"))
} GenesisFSM;
extern ParseTable parse_GenesisFSM[];
#define TYPE_parse_GenesisFSM GenesisFSM

AUTO_ENUM;
typedef enum GenesisMissionPortalUseType
{
	GenesisMissionPortal_Volume,
	GenesisMissionPortal_Door,
} GenesisMissionPortalUseType;
extern StaticDefineInt GenesisMissionPortalUseTypeEnum[];

AUTO_ENUM;
typedef enum GenesisMissionPortalType
{
	GenesisMissionPortal_Normal,
	GenesisMissionPortal_OneWayOutOfMap,
	GenesisMissionPortal_BetweenLayouts,
} GenesisMissionPortalType;
extern StaticDefineInt GenesisMissionPortalTypeEnum[];

AUTO_STRUCT;
typedef struct GenesisMissionPortal
{
	char *pcName;									AST(NAME("Name"))
	GenesisMissionPortalType eType;					AST(NAME("Type"))
	GenesisMissionPortalUseType eUseType;			AST(NAME("UseType"))

	// Layout the portal is in -- there will be an end layout name
	// only if the portal is the BetweenLayouts type.
	char *pcStartLayout;							AST(NAME("StartLayout", "LayoutName"))
	char *pcEndLayout;								AST(NAME("EndLayout"))

	// Rooms connected by the portal
	char *pcStartRoom;								AST(NAME("StartRoom"))
	char *pcEndRoom;								AST(NAME("EndRoom"))
	
	// Optional Door name to connect the portal
	char *pcStartDoor;								AST(NAME("StartDoor"))
	char *pcEndDoor;								AST(NAME("EndDoor"))

	// When this portal is active
	GenesisWhen when;								AST(NAME("When"))

	// Spawn points (by logical name) to use, or NULL to create one
	char *pcStartSpawn;								AST(NAME("StartSpawn"))
	char *pcEndSpawn;								AST(NAME("EndSpawn"))

	// Challenge names of clickable object to use on each side, or NULL to create
	// a room volume.
	char *pcStartClickable;							AST(NAME("StartClickable"))
	char *pcEndClickable;							AST(NAME("EndClickable"))

	bool bStartUseVolume;							AST(NAME("StartUseVolume"))
	bool bEndUseVolume;								AST(NAME("EndUseVolume"))

	// The following data all is only relevant if the EndRoom is in
	// another map.
	char *pcEndZmap;								AST(NAME("EndZmap"))
	WorldVariableDef **eaEndVariables;				AST(NAME("EndVariable"))				
   
	// What text to display on the button that will warp you
	char *pcWarpToStartText;						AST(NAME("WarpToStartText"))
	char *pcWarpToEndText;							AST(NAME("WarpToEndText"))

	// TODO: As soon as we can do DoorInRoom on interiors, this should be added
	// GenesisMissionPortalFrom eFrom;				AST(NAME("From"))
} GenesisMissionPortal;
extern ParseTable parse_GenesisMissionPortal[];
#define TYPE_parse_GenesisMissionPortal GenesisMissionPortal

AUTO_STRUCT;
typedef struct GenesisMissionObjectiveOld_CompleteChallenge 
{
	// The names of the challenges to complete
	char **pcChallengeNames;						AST(NAME("ChallengeName"))

	// The number of placed challenges to complete
	// It is an error for this to be greater than the count on the challenge
	// If zero, this is treated as all of the challenge.
	int iCount;										AST(NAME("Count"))
} GenesisMissionObjectiveOld_CompleteChallenge;
extern ParseTable parse_GenesisMissionObjectiveOld_CompleteChallenge[];
#define TYPE_parse_GenesisMissionObjectiveOld_CompleteChallenge GenesisMissionObjectiveOld_CompleteChallenge

AUTO_STRUCT;
typedef struct GenesisMissionObjectiveOld_ReachLocation 
{
	// The room to be reached
	// This implies a volume in that room
	char *pcRoomName;								AST(NAME("RoomName"))
} GenesisMissionObjectiveOld_ReachLocation;
extern ParseTable parse_GenesisMissionObjectiveOld_ReachLocation[];
#define TYPE_parse_GenesisMissionObjectiveOld_ReachLocation GenesisMissionObjectiveOld_ReachLocation

AUTO_STRUCT;
typedef struct GenesisMissionObjectiveOld_KillCritter
{
	// What critter to kill
	REF_TO(CritterDef) hCritter;					AST(NAME("Critter"))

    // The number of critters to kill
	int iCount;										AST(NAME("Count"))
} GenesisMissionObjectiveOld_KillCritter;
extern ParseTable parse_GenesisMissionObjectiveOld_KillCritter[];
#define TYPE_parse_GenesisMissionObjectiveOld_KillCritter GenesisMissionObjectiveOld_KillCritter

AUTO_STRUCT;
typedef struct GenesisMissionObjectiveOld_KillCritterGroup
{
	// What critter group to kill
	REF_TO(CritterGroup) hCritterGroup;				AST(NAME("CritterGroup"))

	// The number of critters to kill
	int iCount;										AST(NAME("Count"))
} GenesisMissionObjectiveOld_KillCritterGroup;
extern ParseTable parse_GenesisMissionObjectiveOld_KillCritterGroup[];
#define TYPE_parse_GenesisMissionObjectiveOld_KillCritterGroup GenesisMissionObjectiveOld_KillCritterGroup

AUTO_STRUCT;
typedef struct GenesisMissionObjectiveOld_TalkToContact 
{
	// The contact to talk to
	char *pcContactName;							AST(NAME("ContactName"))
} GenesisMissionObjectiveOld_TalkToContact;
extern ParseTable parse_GenesisMissionObjectiveOld_TalkToContact[];
#define TYPE_parse_GenesisMissionObjectiveOld_TalkToContact GenesisMissionObjectiveOld_TalkToContact

AUTO_STRUCT;
typedef struct GenesisMissionObjectiveOld_CollectItems
{
	// The item to take
	REF_TO(ItemDef) hItemDef;						AST(NAME("ItemDef"))
	int iCount;										AST(NAME("Count"))
} GenesisMissionObjectiveOld_CollectItems;
extern ParseTable parse_GenesisMissionObjectiveOld_CollectItems[];
#define TYPE_parse_GenesisMissionObjectiveOld_CollectItems GenesisMissionObjectiveOld_CollectItems

AUTO_STRUCT;
typedef struct GenesisMissionObjectiveOld_PromptComplete
{
	char* pcPromptName;								AST(NAME("PromptName"))
} GenesisMissionObjectiveOld_PromptComplete;
extern ParseTable parse_GenesisMissionObjectiveOld_PromptComplete[];
#define TYPE_parse_GenesisMissionObjectiveOld_PromptComplete GenesisMissionObjectiveOld_PromptComplete

AUTO_STRUCT;
typedef struct GenesisMissionObjectiveOld
{
	GenesisWhenType eType;				AST(NAME("Type") DEFAULT(GenesisWhen_ChallengeComplete))

	// Only one of these should be specified, depending on what type is
	GenesisMissionObjectiveOld_CompleteChallenge *pCompleteChallenge;	AST(NAME("CompleteChallenge"))
	GenesisMissionObjectiveOld_ReachLocation *pReachLocation;			AST(NAME("ReachLocation"))
	GenesisMissionObjectiveOld_KillCritter *pKillCritter;				AST(NAME("KillCritter"))
	GenesisMissionObjectiveOld_KillCritterGroup *pKillCritterGroup;		AST(NAME("KillCritterGroup"))
	GenesisMissionObjectiveOld_TalkToContact *pTalkToContact;			AST(NAME("TalkToContact"))
	GenesisMissionObjectiveOld_CollectItems *pCollectItems;				AST(NAME("CollectItems"))
	GenesisMissionObjectiveOld_PromptComplete *pPromptComplete;			AST(NAME("PromptComplete"))
} GenesisMissionObjectiveOld;
extern ParseTable parse_GenesisMissionObjectiveOld[];
#define TYPE_parse_GenesisMissionObjectiveOld GenesisMissionObjectiveOld

typedef struct GenesisMissionObjective GenesisMissionObjective;

AUTO_STRUCT AST_IGNORE("DescriptionText") AST_FIXUPFUNC(fixupGenesisMissionObjective);
typedef struct GenesisMissionObjective {
	char *pcName;									AST(NAME("Name"))

	// This is the English text for the objective
	char *pcShortText;								AST(NAME("UIString", "ShortText"))

	// This is the English text for a floater that happens when the objective is complete
	char *pcSuccessFloaterText;						AST(NAME("SuccessFloaterText"))

    // A timeout, when this time elapses the objective auto-succeeds
	U32 uTimeout;									AST(NAME("TimeToComplete"))

    // These are for optional objectives
	bool bOptional;									AST(NAME("Optional"))

	// If true, then show waypoints for this objective
	bool bShowWaypoints;							AST(NAME("ShowWaypoints"))

	GenesisWhen succeedWhen;						AST(NAME("SucceedWhen"))

	// On success reward. Added for UGC to be able to set this on the 'Get_Reward' submission
	REF_TO(RewardTable) hReward;					AST(NAME("Reward"))

	// This is used by the AllOf and InOrder types
	GenesisMissionObjective **eaChildren;		    AST(NAME("Objective"))

	// Extra on-start actions
	WorldGameActionProperties** eaOnStartActions;	AST(NAME("OnStartAction"))

	// Extra waypoints appear in addition to the auto-generated ones created by bShowWaypoints = true
	MissionWaypoint** eaExtraWaypoints;				AST(NAME("ExtraWaypoint") LATEBIND)

	GenesisMissionObjectiveOld succeedWhenOld;		AST(EMBEDDED_FLAT)
} GenesisMissionObjective;
extern ParseTable parse_GenesisMissionObjective[];
#define TYPE_parse_GenesisMissionObjective GenesisMissionObjective

/// Input data structure for the Generate step.
AUTO_STRUCT;
typedef struct GenesisMissionZoneDescription
{
	// System name of the mission
	char *pcName;									AST(NAME("Name"))

	// English display name of the mission
	char *pcDisplayName;							AST(NAME("DisplayName"))

	// This is the English text for the objective
	char *pcShortText;								AST(NAME("UIString", "ShortText"))
	char *pcDescriptionText;						AST(NAME("DescriptionText"))
	char *pcSummaryText;							AST(NAME("Summary"))
	char *strReturnText;							AST(NAME("ReturnText"))

	// The category
	REF_TO(MissionCategory) hCategory;				AST(NAME("Category"))
	MissionPlayType ePlayType;						AST(NAME("PlayType"))
	ContentAuthorSource eAuthorSource;				AST(NAME("AuthorSource"))
	ContainerID ugcProjectID;						AST(NAME("UGCProjectID"))

	MissionLevelDef levelDef;						AST( EMBEDDED_FLAT )

	MissionShareableType eShareable;				AST(NAME("Shareable"))

    // The rewards
	REF_TO(RewardTable) hReward;					AST(NAME("Reward"))

	// Rewards
	F32 rewardScale;								AST(NAME("RewardScale"))

	// The description of how the mission is granted
	GenesisMissionGrantDescription grantDescription; AST(NAME("Grant"))

	// The type of mission to generate
	GenesisMissionGenerationType generationType;	AST(NAME("GenerationType", "IsOpenMission"))

    // The description of how this mission works as an Open Mission
	GenesisMissionOpenMissionDescription *pOpenMissionDescription; AST(NAME("OpenMission"))

	// The description of where the player starts on the map
	GenesisMissionStartDescription startDescription; AST(NAME("Start"))

	// A list of prompts
	GenesisMissionPrompt **eaPrompts;				AST(NAME("Prompt"))

	// A list of FSMs
	GenesisFSM **eaFSMs;

	// A list of portals
	GenesisMissionPortal **eaPortals;				AST(NAME("Portal"))

	// Mission Drops
	REF_TO(RewardTable) dropRewardTable;			AST(NAME("DropRewardTable"))
	char** dropChallengeNames;						AST(NAME("DropChallengeName"))

	// A list of objectives to be completed in order
	GenesisMissionObjective **eaObjectives;			AST(NAME("Objective"))
} GenesisMissionZoneDescription;
extern ParseTable parse_GenesisMissionZoneDescription[];
#define TYPE_parse_GenesisMissionZoneDescription GenesisMissionZoneDescription

/// Input data structure for the Transmogrify step. 
AUTO_STRUCT;
typedef struct GenesisMissionDescription
{
	GenesisMissionZoneDescription zoneDesc;			AST(EMBEDDED_FLAT)
	
	// A list of challenges to be placed on the map
	GenesisMissionChallenge **eaChallenges;			AST(NAME("Challenge"))
} GenesisMissionDescription;
extern ParseTable parse_GenesisMissionDescription[];
#define TYPE_parse_GenesisMissionDescription GenesisMissionDescription

/// Transmogrified version of a MissionDescription
AUTO_STRUCT AST_IGNORE_STRUCT(RoomRequirement);
typedef struct GenesisZoneMission
{
	GenesisMissionZoneDescription desc;				AST(EMBEDDED_FLAT)
	
	bool bTrackingEnabled;							AST(NAME("TrackingEnabled"))
	GenesisMissionZoneChallenge** eaChallenges;		AST(NAME("Challenge"))
} GenesisZoneMission;
extern ParseTable parse_GenesisZoneMission[];
#define TYPE_parse_GenesisZoneMission GenesisZoneMission

/// A list of actions that can happen.
AUTO_STRUCT;
typedef struct GenesisMissionAction
{
	char* promptName;								AST(NAME("PromptName"))
} GenesisMissionAction;
extern ParseTable parse_GenesisMissionAction[];
#define TYPE_parse_GenesisMissionAction GenesisMissionAction

/// A concrete list of extra requirements a mission can impose.
AUTO_STRUCT;
typedef struct GenesisMissionRequirements
{
	// This exists so that the messages in this requirements structure
	// can be saved out.
	char* messageFilename;							AST(CURRENTFILE)
	
	char* missionName;								AST(NAME("Name"))
	GenesisMissionRoomRequirements** roomRequirements; AST(NAME("RoomRequirement"))
	GenesisMissionChallengeRequirements** challengeRequirements; AST(NAME("ChallengeRequirement"))
	GenesisMissionExtraVolume** extraVolumes;		AST(NAME("ExtraVolume"))

	GenesisProceduralObjectParams* params;			AST(NAME("Params"))
} GenesisMissionRequirements;
extern ParseTable parse_GenesisMissionRequirements[];
#define TYPE_parse_GenesisMissionRequirements GenesisMissionRequirements

/// A door required for a mission
AUTO_STRUCT;
typedef struct GenesisMissionDoorRequirements
{
	char* doorName;									AST(NAME("DoorName"))
} GenesisMissionDoorRequirements;
extern ParseTable parse_GenesisMissionDoorRequirements[];
#define TYPE_parse_GenesisMissionDoorRequirements GenesisMissionDoorRequirements

/// A concrete list of extra requirements a mission can impose on
/// rooms.
AUTO_STRUCT;
typedef struct GenesisMissionRoomRequirements
{
	char* layoutName;								AST(NAME("LayoutName"))
	char* roomName;									AST(NAME("Name"))
	GenesisMissionDoorRequirements** doors;			AST(NAME("Door"))
	GenesisProceduralObjectParams* params;			AST(NAME("Params"))
} GenesisMissionRoomRequirements;
extern ParseTable parse_GenesisMissionRoomRequirements[];
#define TYPE_parse_GenesisMissionRoomRequirements GenesisMissionRoomRequirements

AUTO_STRUCT;
typedef struct GenesisMissionPromptExprPair
{
	char* name;										AST(NAME("Name"))
	char* exprText;									AST(NAME("ExprText"))
} GenesisMissionPromptExprPair;
extern ParseTable parse_GenesisMissionPromptExprPair[];
#define TYPE_parse_GenesisMissionPromptExprPair GenesisMissionPromptExprPair

AUTO_STRUCT;
typedef struct GenesisMissionContactRequirements
{
	DisplayMessage contactName;						AST(NAME("ContactName") STRUCT(parse_DisplayMessage) )
	GenesisMissionPromptExprPair **eaPrompts;		AST(NAME("Prompts"))
	REF_TO(PlayerCostume) hCostume;					AST(NAME("Costume"))
	char *pcContactFileName;						AST(NAME("ContactFileName"))
} GenesisMissionContactRequirements;
extern ParseTable parse_GenesisMissionContactRequirements[];
#define TYPE_parse_GenesisMissionContactRequirements GenesisMissionContactRequirements

/// A wrapper for a struct which contains fields to override on a
/// specific challenge.
AUTO_STRUCT;
typedef struct GenesisMissionChallengeRequirements
{
	char* challengeName;							AST(NAME("Name"))
	GenesisInstancedObjectParams* params;			AST(NAME("Params"))
	GenesisInteractObjectParams* interactParams;	AST(NAME("InteractParams"))
	GenesisProceduralObjectParams *volumeParams;	AST(NAME("VolumeParams"))
} GenesisMissionChallengeRequirements;
extern ParseTable parse_GenesisMissionChallengeRequirements[];
#define TYPE_parse_GenesisMissionChallengeRequirements GenesisMissionChallengeRequirements

/// A definition for an extra volume to be placed in the world that
/// the mission requires
AUTO_STRUCT;
typedef struct GenesisMissionExtraVolume {
	char* volumeName;								AST(NAME("Name"))

	// this names challenges or rooms
	char** objects;									AST(NAME("Object"))
} GenesisMissionExtraVolume;
extern ParseTable parse_GenesisMissionExtraVolume[];
#define TYPE_parse_GenesisMissionExtraVolume GenesisMissionExtraVolume

/// Description of one part of an episode.
AUTO_STRUCT AST_IGNORE("ContinuePromptCostume");
typedef struct GenesisEpisodePart
{
	char* name;										AST(NAME("Name"))
	REF_TO(GenesisMapDescription) map_desc;			AST(NAME("MapDesc"))
	char* mission_name;								AST(NAME("MissionName"))

	GenesisMissionExitFrom eContinueFrom;			AST(NAME("ContinueFrom"))
	char *pcContinueRoom;							AST(NAME("ContinueRoom"))
	char *pcContinueChallenge;						AST(NAME("ContinueChallenge"))
	char *pcContinueMissionText;					AST(NAME("ContinueMissionText"))
	GenesisMissionCostume continuePromptCostume;	AST(NAME("ContinuePromptCostume2"))
	char *pcContinuePromptButtonText;				AST(NAME("ContinuePromptButtonText"))
	char *pcContinuePromptCategoryName;				AST(NAME("ContinuePromptCategoryName"))
	WorldOptionalActionPriority eContinuePromptPriority; AST(NAME("ContinuePromptPriority"))
	char *pcContinuePromptTitleText;				AST(NAME("ContinuePromptTitleText"))
	char **eaContinuePromptBodyText;				AST(NAME("ContinuePromptBodyText"))

    // transition overrides
	REF_TO(DoorTransitionSequenceDef) hStartTransitionOverride; AST(NAME("StartTransitionOverride"))
	REF_TO(DoorTransitionSequenceDef) hContinueTransitionOverride; AST(NAME("ContinueTransitionOverride"))
} GenesisEpisodePart;
extern ParseTable parse_GenesisEpisodePart[];
#define TYPE_parse_GenesisEpisodePart GenesisEpisodePart

/// Description of how an episode will be granted.
AUTO_STRUCT;
typedef struct GenesisEpisodeGrantDescription
{
	bool bNeedsReturn;			AST(NAME("NeedsReturn"))
	char* pcEpisodeReturnText;	AST(NAME("ReturnText"))
} GenesisEpisodeGrantDescription;
extern ParseTable parse_GenesisEpisodeGrantDescription[];
#define TYPE_parse_GenesisEpisodeGrantDescription GenesisEpisodeGrantDescription

/// A description of an Episode for Genesis.
AUTO_STRUCT;
typedef struct GenesisEpisode {
	char *name;									AST(NAME("Name") STRUCTPARAM KEY)
	const char *filename;						AST(CURRENTFILE)
	const char *scope;							AST(POOL_STRING)
	char* comments;								AST(NAME("Comments"))

	MissionLevelDef levelDef;					AST(EMBEDDED_FLAT)
	F32 fNumericRewardScale;					AST(NAME("RewardScale"))
	Expression *missionReqs;					AST(NAME("RequiresBlock") LATEBIND )
	
	char *pcDisplayName;						AST(NAME("DisplayName"))
	char *pcShortText;							AST(NAME("UIString"))
	char *pcSummaryText;						AST(NAME("Summary"))
	char *pcDescriptionText;					AST(NAME("DescriptionText"))
	REF_TO(RewardTable) hReward;				AST(NAME("Reward"))
	REF_TO(MissionCategory) hCategory;			AST(NAME("Category"))
	GenesisEpisodeGrantDescription grantDescription; AST(NAME("Grant"))

	GenesisEpisodePart **parts;					AST(NAME("Part"))
} GenesisEpisode;
extern ParseTable parse_GenesisEpisode[];
#define TYPE_parse_GenesisEpisode GenesisEpisode

// TomY ENCOUNTER_HACK hacky hack continued
AUTO_STRUCT AST_IGNORE("CostumeName") AST_IGNORE("MissionName");
typedef struct GenesisProceduralPromptInfo
{
	char *dialogName;								AST( NAME(DialogName) )
} GenesisProceduralPromptInfo;
extern ParseTable parse_GenesisProceduralPromptInfo[];
#define TYPE_parse_GenesisProceduralPromptInfo GenesisProceduralPromptInfo

// TomY ENCOUNTER_HACK hacky hack continued
AUTO_STRUCT AST_FIXUPFUNC( fixupGenesisProceduralEncounterProperties );
typedef struct GenesisProceduralEncounterProperties
{
	const char *encounter_name;						AST( NAME(EncounterName) POOL_STRING )
	char *genesis_mission_name;						AST( NAME(MissionName) )
	int genesis_mission_num;						AST( NAME(MissionNum) )
	bool genesis_open_mission;						AST( NAME(OpenMission) )
	bool has_patrol;								AST( NAME(HasPatrol) )

	// Needs to work without Genesis data, keep the challenges around.
	GenesisWhen spawn_when;							AST( EMBEDDED_FLAT )
	GenesisMissionZoneChallenge **when_challenges;	AST( NAME(SpawnWhenChallenge) )
	GenesisProceduralPromptInfo **when_prompts;		AST( NAME(SpawnWhenPrompt) )
} GenesisProceduralEncounterProperties;
extern ParseTable parse_GenesisProceduralEncounterProperties[];
#define TYPE_parse_GenesisProceduralEncounterProperties GenesisProceduralEncounterProperties

AUTO_STRUCT;
typedef struct GenesisMissionVolumePoints
{
	char *volume_name;								AST( NAME(VolumeName) )
	F32 *positions;									AST( NAME(VolumePositions) )//Array of Vec3's
} GenesisMissionVolumePoints;
extern ParseTable parse_GenesisMissionVolumePoints[];
#define TYPE_parse_GenesisMissionVolumePoints GenesisMissionVolumePoints

SA_RET_OP_VALID GenesisProceduralObjectParams* genesisFindMissionRoomProceduralParams(GenesisMissionRequirements* req, const char* layout_name, const char* room_name);
SA_RET_OP_VALID GenesisInstancedObjectParams* genesisFindMissionChallengeInstanceParams(GenesisMissionRequirements* req, const char* challenge_name);
SA_RET_OP_VALID GenesisInteractObjectParams* genesisFindMissionChallengeInteractParams(GenesisMissionRequirements* req, const char* challenge_name);
SA_RET_OP_VALID GenesisProceduralObjectParams* genesisFindMissionChallengeVolumeParams(GenesisMissionRequirements* missionReq, const char* challenge_name);
void genesisCalcMissionVolumePointsInto(GenesisMissionVolumePoints ***peaVolumePointsList, GenesisMissionRequirements* pReq, GenesisToPlaceState *pToPlace);
#define genesisMissionPortalSpawnTargetName(buffer,portal,isTargetStart,mission_name,layout_name) genesisMissionPortalSpawnTargetName_s(SAFESTR(buffer),portal,isTargetStart,mission_name,layout_name)
SA_RET_NN_STR const char* genesisMissionPortalSpawnTargetName_s(SA_PRE_GOOD SA_POST_NN_STR char* buffer, int buffer_size, GenesisMissionPortal* portal, bool isTargetStart, const char* mission_name, const char* layout_name);

bool genesisMissionReturnIsAutogenerated(GenesisMissionZoneDescription* pMissionDesc, bool has_solar);
bool genesisMissionContinueIsAutogenerated(GenesisMissionZoneDescription* pMissionDesc, bool has_solar, bool has_exterior);
GenesisMissionChallenge* genesisFindChallenge(GenesisMapDescription* map_desc, GenesisMissionDescription* mission_desc, const char* challenge_name, bool* outIsShared);
