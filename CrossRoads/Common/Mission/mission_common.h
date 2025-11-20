/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once
GCC_SYSTEM

#include "Message.h"
#include "referencesystem.h"
#include "multival.h"
#include "Mission_Enums.h"
#include "ItemEnums.h"
#include "GlobalTypeEnum.h"
#include "wlGenesisMissionsGameStructs.h"
#include "WorldLibEnums.h"

typedef U32 ContainerID;
typedef U32 EntityRef; // This is an opaque entity identifier, which is implemented elsewhere

extern StaticDefineInt SkillTypeEnum[];

typedef struct AllegianceRef AllegianceRef;
typedef struct ContactDef ContactDef;
typedef struct ContactInfo ContactInfo;
typedef struct ContactDialog ContactDialog;
typedef struct ContactDialogInfo ContactDialogInfo;
typedef struct InteractionInfo InteractionInfo;
typedef struct ContactCostumeFallback ContactCostumeFallback;
typedef struct ContactMissionOffer ContactMissionOffer;
typedef struct ContactImageMenuItem ContactImageMenuItem;
typedef struct Critter Critter;
typedef struct CritterInteractInfo CritterInteractInfo;
typedef struct DoorTransitionSequenceDef DoorTransitionSequenceDef;
typedef struct Entity Entity;
typedef struct EventTracker EventTracker;
typedef struct ExprContext ExprContext;
typedef struct ExprFuncTable ExprFuncTable;
typedef struct Expression Expression;
typedef struct GameEvent GameEvent;
typedef struct GameTimer GameTimer;
typedef struct InventoryBag InventoryBag;
typedef struct Item Item;
typedef struct ItemDef ItemDef;
typedef struct Message Message;
typedef struct Mission Mission;
typedef struct MissionCategory MissionCategory;
typedef struct MissionDef MissionDef;
typedef struct MissionDefParams MissionDefParams;
typedef struct MissionInfo MissionInfo;
typedef struct MissionLevelDef MissionLevelDef;
typedef struct MissionLevelClamp MissionLevelClamp;
typedef struct MissionSet MissionSet;
typedef struct MissionTemplate MissionTemplate;
typedef struct MissionTemplateType MissionTemplateType;
typedef struct MissionVarTable MissionVarTable;
typedef struct MultiVal MultiVal;
typedef struct OldEncounter OldEncounter;
typedef struct OpenMissionScoreEvent OpenMissionScoreEvent;
typedef struct ParseTable ParseTable;
typedef struct QueuedContactDialog QueuedContactDialog;
typedef struct RemoteContact RemoteContact;
typedef struct Reward Reward;
typedef struct RewardTable RewardTable;
typedef struct SpecialDialogBlock SpecialDialogBlock;
typedef struct StoreItemInfo StoreItemInfo;
typedef struct StoryArcDef StoryArcDef;
typedef struct Team Team;
typedef struct TemplateVariableGroup TemplateVariableGroup;
typedef struct WorldGameActionProperties WorldGameActionProperties;
typedef struct WorldInteractionPropertyEntry WorldInteractionPropertyEntry;
typedef struct WorldVariable WorldVariable;
typedef struct WorldVariableContainer WorldVariableContainer;
typedef struct WorldVariableDef WorldVariableDef;
typedef struct PlayerCostume PlayerCostume;
typedef struct GameProgressionNodeDef GameProgressionNodeDef;
typedef struct SpecialActionBlock SpecialActionBlock;
typedef struct EventDef EventDef;
typedef struct UGCMissionData UGCMissionData;

// Setup the mission using the template stored on it
typedef void (*MissionTemplateCBFunc) (MissionDef* def, MissionDef* parentDef);

typedef int (*MissionComparatorFunc)(const Mission**, const Mission**);
typedef bool (*MissionAddToListCB) (const Mission* mission);
typedef bool (*MissionExpandChildrenCB) (const Mission* mission);

// Maximum length of a Mission Refstring
#define MAX_MISSIONREF_LEN 256

// Struct for any extra dynlists
AUTO_STRUCT;
typedef struct TemplateVariable
{
	const char* varName;						AST( POOL_STRING )
	const char* varDependency;					AST( POOL_STRING )

	MultiVal varValue;

	// For Mad-libs Missions
	Expression *varValueExpression;				AST( LATEBIND )

	TemplateVariableType varType;
	U32 id;
} TemplateVariable;


AUTO_STRUCT;
typedef struct TemplateVariableGroup
{
	const char* groupName;						AST( POOL_STRING )

	TemplateVariable** variables;				AST( NAME("Variable") )
	TemplateVariableGroup** subGroups;

	char* instructions;

	// If true, all sub groups of this group are identical and the UI should support
	// adding and deleting subgroups
	bool list;		

	// Any new elements of the list will look like this
	TemplateVariableGroup* listPrototype;	

	// UI Variables
	// Whether this group is open by default in the editor
	bool startsOpen;							AST( DEF(1) )	
	// Whether this group is hidden in the editor
	bool hidden;		

	// Editor variables; used to keep track of relationships in the editor
	MissionDef* parentDef;						NO_AST
	TemplateVariableGroup* parentGroup;			NO_AST
} TemplateVariableGroup;


AUTO_STRUCT;
typedef struct TemplateVariableOption
{
	TemplateVariable value;
	F32 weight;									AST( DEF(1) )
	int minLevel;
	int maxLevel;
} TemplateVariableOption;


AUTO_STRUCT;
typedef struct TemplateVariableSubList
{
	// Value of the dependency
	TemplateVariable* parentValue;

	// If the dependency is an Int type, we use this range
	// instead of parentValue
	int iParentValueMin;						AST( DEF(1))
	int iParentValueMax;						AST( DEF(1))

	// Weighted list of possible values for the child
	TemplateVariableOption** childValues;

	// An Item variable can reference a Reward Table instead
	// of using a list of child values.
	REF_TO(RewardTable) hRewardTable;
} TemplateVariableSubList;


AUTO_STRUCT;
typedef struct MissionVarTable
{
	// The name of this table
	const char *pchName;						AST( STRUCTPARAM KEY POOL_STRING )
	const char* filename;						AST( CURRENTFILE)

	// The type of dependency variable this table accepts.
	// Can be "None" if this table is not associated with
	// a dependency.
	TemplateVariableType dependencyType;

	// The type of variable this table returns
	TemplateVariableType varType;

	// Sub-lists of variables of type varType.  Each list is
	// associated with a value of type dependencyType
	TemplateVariableSubList **subLists;

	// Used during editing
	DisplayMessageList varMessageList;			AST( NO_TEXT_SAVE STRUCT(parse_DisplayMessageList) ) 

	U32 nextVarId;
} MissionVarTable;

AUTO_STRUCT;
typedef struct MissionTemplateType
{
	const char* templateName;					AST( NAME("Name") STRUCTPARAM KEY POOL_STRING )

	TemplateVariableGroup rootVarGroup;

	MissionTemplateCBFunc CBFunc; NO_AST
} MissionTemplateType;

AUTO_STRUCT;
typedef struct MissionTemplate
{
	// Template type
	const char* templateTypeName;				AST( POOL_STRING )

	// Params
	TemplateVariableGroup* rootVarGroup;

	U32 nextVarId;
} MissionTemplate;

// Expression Condition format for the editor that gets post processed into an useable state
typedef struct MissionEditCond MissionEditCond;
AUTO_STRUCT;
typedef struct MissionEditCond
{
	// Type of condition
	MissionCondType type;					AST( STRUCTPARAM ) 

	// SubMission name if is objective, Expression otherwise, if NULL, this is just a group
	char* valStr;							AST( STRUCTPARAM ) 

	// Sub conditions grouped under this condition
	MissionEditCond** subConds;				AST( NAME("When") ) 

	// Dual purpose: for parsing and displaying expressions and for displaying counts of subconditions
	MDEShowCount showCount;					AST( NAME("ShowCount") )

	// Count of subconditions if this is a count condition type
	S32 iCount;								AST( NAME("Count") )

	// Expression for this condition if of type Expression. Postprocessed.
	Expression* expression;					AST( NAME("Expression") NO_TEXT_SAVE LATEBIND SERVER_ONLY )
} MissionEditCond;
extern ParseTable parse_MissionEditCond[];
#define TYPE_parse_MissionEditCond MissionEditCond


// Structure of a MissionWaypoint
AUTO_STRUCT;
typedef struct MissionWaypoint
{
	// Type of waypoint, i.e. the type of object this waypoint is attached to
	MissionWaypointType type;

	// Name of the object this waypoint is attached to
	char* name;

	// Map
	const char* mapName;					AST(POOL_STRING)

	// Works on all maps
	bool bAnyMap;
} MissionWaypoint;
extern ParseTable parse_MissionWaypoint[];
#define TYPE_parse_MissionWaypoint MissionWaypoint

AUTO_STRUCT;
typedef struct MinimapWaypoint
{
	MinimapWaypointType type;
	Vec3 pos;

	const char* pchMissionRefString; AST(POOL_STRING)
	const char *pchContactName;		AST(POOL_STRING)
	const char *pchStaticEncName;	AST(POOL_STRING)
	char* pchIconTexName;
	char* pchDescription;
	REF_TO(Message) hDisplayNameMsg;

	// Used to know if there's a waypoint at a destination, e.g. when looking at the overworld map.
	const char* pchDestinationMap;	AST(POOL_STRING)

	// If these are non-zero, display as an ellipse
	F32 fXAxisRadius;
	F32 fYAxisRadius;
	F32 fRotation;   // Rotation around the vertical axis

	bool bHideUnlessRevealed;

	// Used on the client to determine which waypoints to display
	F32 distSquared;						NO_AST 
	const char* MapCreatedOn;				AST(POOL_STRING)

	//Should be set to true when the waypoint is on a door that leads to another map or region.
	bool bIsDoorWaypoint;
} MinimapWaypoint;


// MissionDrop is a reward that only drops for players who have the mission
AUTO_STRUCT AST_IGNORE(Spawningplayeronly);
typedef struct MissionDrop
{
	// What type of thing this should drop from
	MissionDropTargetType type;
	
	// When this reward table should drop 
	MissionDropWhenType whenType;

	// CritterDef Name, EncounterGroup Name, etc. depending on MissionDropTargetType
	const char* value;						AST( NAME("Value", "CritterName") POOL_STRING )  

	// Reward table to drop from
	const char* RewardTableName;			AST(POOL_STRING)

	// Map
	const char* pchMapName;					AST(POOL_STRING)
} MissionDrop;
extern ParseTable parse_MissionDrop[];
#define TYPE_parse_MissionDrop MissionDrop

AUTO_STRUCT;
typedef struct MissionMap
{
	const char* pchMapName;					AST( STRUCTPARAM POOL_STRING)
	REF_TO(Message)	hMapDisplayName;		AST( NAME("MapDisplayName") NO_TEXT_SAVE CLIENT_ONLY)
	WorldVariable** eaWorldVars;			AST( NAME("Var") )

	// Hides the "goto <mapname>" string in the mission tracker UI.
	bool bHideGotoString;
} MissionMap;
extern ParseTable parse_MissionMap[];
#define TYPE_parse_MissionMap MissionMap

extern DefineContext* g_pDefineMissionWarpCost;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pDefineMissionWarpCost);
typedef enum MissionWarpCostType
{
	kMissionWarpCostType_None = 0, ENAMES(None)
	// Values are data-defined...
} MissionWarpCostType;

AUTO_STRUCT;
typedef struct MissionWarpCostDef
{
	char* pchName; AST(STRUCTPARAM KEY)
		// The internal name of this def
	const char* pchNumeric; AST(POOL_STRING)
		// The numeric ItemDef to use
	S32 iNumericCost;
		// The numeric cost
	MissionWarpCostType eCostType; NO_AST
		// The cost type
} MissionWarpCostDef;

AUTO_STRUCT;
typedef struct MissionWarpCostDefs
{
	EARRAY_OF(MissionWarpCostDef) eaDefs; AST(NAME(MissionWarpCostDef))
} MissionWarpCostDefs;

AUTO_STRUCT;
typedef struct MissionMapWarpData
{
	const char* pchMapName;	AST(POOL_STRING)
		// The map name to warp to
	const char* pchSpawn; AST(POOL_STRING)
		// The spawn point to warp to
	MissionWarpCostType eCostType;
		// Defines the cost to warp
	S32 iRequiredLevel;
		// The level required in order to use this warp
	REF_TO(DoorTransitionSequenceDef) hTransSequence; AST(NAME(TransitionSequence))
		// The transition sequence to play
} MissionMapWarpData;

AUTO_STRUCT;
typedef struct MissionNumericScale
{
	const char* pchNumeric; AST(STRUCTPARAM POOL_STRING)
		// The name of the numeric to scale
	F32 fScale;				AST(DEFAULT(1))
		// The scale value to apply to this numeric
} MissionNumericScale;

AUTO_STRUCT;
typedef struct MissionDefParams
{
	F32 NumericRewardScale;
	F32 NumericRewardScaleIneligible;
	F32 NumericRewardScaleAlreadyCompleted;
	S32 iScaleRewardOverTime;					AST( NAME(ScaleRewardOverTime) ) //in minutes

	MissionNumericScale** eaNumericScales;		AST( NAME(PerNumericScale) )

	const char* pchActivityName;				AST( POOL_STRING )

	const char* OnstartRewardTableName;			AST( POOL_STRING )
	const char* OnfailureRewardTableName;		AST( POOL_STRING )
	const char* OnsuccessRewardTableName;		AST( POOL_STRING )
	const char* OnreturnRewardTableName;		AST( POOL_STRING )
	const char* OnReplayReturnRewardTableName;	AST( POOL_STRING )
	const char* ActivitySuccessRewardTableName; AST( POOL_STRING )
	const char* ActivityReturnRewardTableName;	AST( POOL_STRING )

	// Open Mission Success reward tables
	const char* pchGoldRewardTable;				AST( POOL_STRING )
	const char* pchSilverRewardTable;			AST( POOL_STRING )
	const char* pchBronzeRewardTable;			AST( POOL_STRING )
	const char* pchDefaultRewardTable;			AST( POOL_STRING )

	// Open Mission Failure reward tables
	const char* pchFailureGoldRewardTable;		AST( POOL_STRING )
	const char* pchFailureSilverRewardTable;	AST( POOL_STRING )
	const char* pchFailureBronzeRewardTable;	AST( POOL_STRING )
	const char* pchFailureDefaultRewardTable;	AST( POOL_STRING )

	// Rewards that drop from certain critters when the player has this mission
	MissionDrop** missionDrops;
} MissionDefParams;
extern ParseTable parse_MissionDefParams[];
#define TYPE_parse_MissionDefParams MissionDefParams

AUTO_STRUCT;
typedef struct InteractableOverride
{
	const char *pcInteractableName;				AST( POOL_STRING ) // Name of the interactable to override
	const char *pcTypeTagName;					AST( POOL_STRING ) // Interaction type tag to match; must be used if interactable name is not provided
	const char *pcMapName;						AST( POOL_STRING ) // Public name of the interactable's map
	// These properties are the ones to add to the interactable
	WorldInteractionPropertyEntry *pPropertyEntry;
	bool bTreatAsMissionReward;
} InteractableOverride;
extern ParseTable parse_InteractableOverride[];
#define TYPE_parse_InteractableOverride InteractableOverride

AUTO_STRUCT;
typedef struct SpecialDialogOverride
{
	const char *pcContactName;				AST( POOL_STRING ) // Name of the contact to override

	// This special dialog is the one to be added to the matching contact
	SpecialDialogBlock *pSpecialDialog;
} SpecialDialogOverride;
extern ParseTable parse_SpecialDialogOverride[];
#define TYPE_parse_SpecialDialogOverride SpecialDialogOverride

AUTO_STRUCT;
typedef struct ActionBlockOverride
{
	const char *pcContactName;				AST( POOL_STRING ) // Name of the contact to override

	// This special action block is the one to be added to the matching contact
	SpecialActionBlock *pSpecialActionBlock;
} ActionBlockOverride;
extern ParseTable parse_ActionBlockOverride[];
#define TYPE_parse_ActionBlockOverride ActionBlockOverride

AUTO_STRUCT;
typedef struct MissionOfferOverride
{
	const char *pcContactName;				AST( POOL_STRING ) // Name of the contact to override

	// This mission offer is the one to be added to the matching contact
	ContactMissionOffer *pMissionOffer;
} MissionOfferOverride;
extern ParseTable parse_MissionOfferOverride[];
#define TYPE_parse_MissionOfferOverride MissionOfferOverride

AUTO_STRUCT;
typedef struct ImageMenuItemOverride
{
	const char *pcContactName;				AST( POOL_STRING ) // Name of the contact to override

	// This image menu item is the one to be added to the matching contact
	ContactImageMenuItem *pImageMenuItem;
} ImageMenuItemOverride;
extern ParseTable parse_ImageMenuItemOverride[];
#define TYPE_parse_ImageMenuItemOverride ImageMenuItemOverride

AUTO_ENUM;
typedef enum MissionDefRequestType
{
	MissionDefRequestType_Mission,
	MissionDefRequestType_MissionSet,
} MissionDefRequestType;

AUTO_STRUCT;
typedef struct MissionDefRequest
{
	MissionDefRequestType eType;
	REF_TO(MissionDef) hRequestedDef;           AST( NAME("RequestedDef") )
	REF_TO(MissionSet) hRequestedMissionSet;    AST( NAME("RequestedMissionSet") )
} MissionDefRequest;

AUTO_ENUM;
typedef enum MissionRequestGrantType
{
	MissionRequestGrantType_None,
	MissionRequestGrantType_Contact,
	MissionRequestGrantType_Direct,
	MissionRequestGrantType_Drop,
//	MissionRequestGrantType_Email,
} MissionRequestGrantType;

AUTO_ENUM;
typedef enum MissionRequestState
{
	MissionRequestState_None,
	MissionRequestState_Open,
	MissionRequestState_Succeeded,
} MissionRequestState;

AUTO_STRUCT AST_CONTAINER;
typedef struct MissionRequest
{
	// Unique ID
	const U32 uID;										AST(STRUCTPARAM PERSIST KEY)

	// The mission that made the request
	CONST_STRING_POOLED pchRequesterRef;				AST(PERSIST POOL_STRING)

	// The mission being requested
	CONST_REF_TO(MissionDef) hRequestedMission;			AST(PERSIST NAME("RequestedMission"))

	// The MissionSet being requested.  This will be used to populate hRequestedMission.
	CONST_REF_TO(MissionSet) hRequestedMissionSet;		AST(PERSIST NAME("RequestedMissionSet"))

	// The state of the request
	const MissionRequestState eState;					AST(PERSIST)

	// This request doesn't attempt to grant the mission until after this time
	U32 uInactiveTime;									AST(PERSIST NO_TRANSACT)

} MissionRequest;

extern DefineContext *g_pDefineTutorialScreenRegions;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pDefineTutorialScreenRegions);
typedef enum TutorialScreenRegion
{
	kTutorialScreenRegion_None = -1, ENAMES(None)
	kTutorialScreenRegion_FIRST_DATA_DEFINED, EIGNORE
}TutorialScreenRegion;

AUTO_STRUCT;
typedef struct TutorialScreenRegionInfo
{
	const char *pchName; AST(POOL_STRING KEY STRUCTPARAM)
	const char *pchUIGen; AST(POOL_STRING)
	bool bHorizontalAlignment : 1;
	bool bVerticalAlignment : 1;
} TutorialScreenRegionInfo;

AUTO_STRUCT;
typedef struct TutorialScreenRegions
{
	TutorialScreenRegionInfo **eaRegions; AST(NAME(TutorialScreenRegion))
} TutorialScreenRegions;

extern TutorialScreenRegions g_TutorialScreenRegions;

extern DefineContext *g_pDefineMissionUITypes;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pDefineMissionUITypes);
typedef enum MissionUIType
{
	kMissionUIType_None = 0, ENAMES(None)
	// Data-defined ...
} MissionUIType;

AUTO_STRUCT;
typedef struct MissionUITypeData
{
	// The name of the mission UI type
	const char *pchName;			AST(KEY STRUCTPARAM POOL_STRING)

	// The display name of this tag
	DisplayMessage msgDisplayName;	AST(NAME(DisplayName) STRUCT(parse_DisplayMessage))

	// An icon to display in the mission UI
	const char *pchIcon;			AST(NAME(Icon) POOL_STRING)

	// The MissionUIType associated with this data. Set at load-time.
	MissionUIType eUIType;			NO_AST
} MissionUITypeData;

AUTO_STRUCT;
typedef struct MissionUITypes
{
	MissionUITypeData** eaTypes; AST(NAME(MissionUIType))
} MissionUITypes;

extern DefineContext *g_pDefineMissionTags;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pDefineMissionTags);
typedef enum MissionTag
{
	kMissionTag_None = 0, ENAMES(None)
	// Data-defined ...
} MissionTag;

AUTO_STRUCT;
typedef struct MissionTagData
{
	// The name of the mission tag
	const char *pchName;			AST(KEY STRUCTPARAM POOL_STRING)
} MissionTagData;

// Array of the MissionTags, loaded and indexed directly
AUTO_STRUCT;
typedef struct MissionTags
{
	MissionTagData **ppTags; AST(NAME(MissionTag))
} MissionTags;

// Static Definition of a mission;
AUTO_STRUCT AST_IGNORE("IconString:") AST_IGNORE("NextActionUniqueID");
typedef struct MissionDef
{
	// Mission name that uniquely identifies the mission. Required.
	const char* name;						AST( STRUCTPARAM KEY POOL_STRING )

	// Scope of the MissionDef.  Root MissionDef only.
	const char* scope;						AST( SERVER_ONLY POOL_STRING )

	// Filename that this mission came from.
	const char* filename;					AST( CURRENTFILE )

	// Version of this MissionDef.  If a Mission was created with an old version, it will be reset.
	U32 version;

	//Parent mission def of this mission, NULL at the root missionDef.
	REF_TO(MissionDef) parentDef;

	// If this mission was created by genesis, this is the zonemap that created it.
	char* genesisZonemap;

	// The functional type of the mission. Examples: normal, perk, etc.
	MissionType missionType;  

	// The maps for the mission determines how the mission displays in the UI.
	// For now this only matters on the root mission.  Eventually may want the ability to tag sub-missions?
	// Where the mission objective is
	MissionMap **eaObjectiveMaps;					AST( NAME("ObjectiveMap") )
	// Where the mission needs to be returned to
	const char *pchReturnMap;						AST( POOL_STRING )
	// The map and spawn to warp to in order to quickly play this mission
	MissionMapWarpData* pWarpToMissionDoor;
	// The mission journal category (if not the map)
	REF_TO(MissionCategory) hCategory;				AST(NAME("Category"))
	// WorldVars which represent a map and map variable.
	//WorldVariable** eaWorldVars;					AST(NAME("WorldVar"))
	
	// Open Missions: The volume that the Open Mission takes place in
	char **eaOpenMissionVolumes;					AST(NAME("OpenMissionVolume"))

	// Open Teams: when granted the open mission, give an open team join request to this team
	char *TeamUpName;								AST(NAME("TeamUpName") POOL_STRING)

	DisplayMessage TeamUpDisplayName;				AST(STRUCT(parse_DisplayMessage))

	// Mission template.  May be no template at all
	MissionTemplate *missionTemplate;		AST( SERVER_ONLY )

	// Section for whoever is working on the mission to leave comments for other people who open this mission
	char* comments;							AST( NAME("Comments", "Comments:") SERVER_ONLY)

	// Sound Event Path (optional) - to be played when the mission is offered from a contact
	char *pchSoundOnContactOffer;			AST( NAME("SoundOnContactOffer") )

	// Sound Event Path (optional) - to be played when mission starts / in progress
	char* pchSoundOnStart;					AST( NAME("SoundOnStart") )

	// Sound Event Path (optional) - to be played instead of algorithmic combat music
	char* pchSoundCombat;					AST( NAME("SoundCombat") )

	// Sound Event Path (optional) - to be played instead of algorithmic ambient music
	char* pchSoundAmbient;					AST( NAME("SoundAmbient") )

	// Sound Event Path (optional) - to be played when mission is complete
	char* pchSoundOnComplete;				AST( NAME("SoundOnComplete") )

	// Display strings for the mission
	// Name of mission. Root mission only
	DisplayMessage displayNameMsg;			AST(STRUCT(parse_DisplayMessage))  
	// Objective, i.e. "Kill 10 Rabbits".  No UIstring means it will be hidden.
	DisplayMessage uiStringMsg;				AST(STRUCT(parse_DisplayMessage))    
	// Long description of mission with flavor text
	DisplayMessage detailStringMsg;			AST(STRUCT(parse_DisplayMessage)) 
	// Short description of mission
	DisplayMessage summaryMsg;				AST(STRUCT(parse_DisplayMessage)) 
	// Mission Journal text to display when this mission fails
	DisplayMessage failureMsg;				AST(STRUCT(parse_DisplayMessage)) 
	// UIString to display with this mission fails
	DisplayMessage failReturnMsg;			AST(STRUCT(parse_DisplayMessage)) 
	// Display message for text splats
	DisplayMessage splatDisplayMsg;			AST(STRUCT(parse_DisplayMessage))
	// name of FX for splat to display
	char *pchSplatFX;

	// Perks only for now - Icon to display for this Mission
	const char *pchIconName;				AST( POOL_STRING )

	MissionLevelDef levelDef;				AST( EMBEDDED_FLAT )

	// Recommended team size for the mission (displays in UI)
	int iSuggestedTeamSize;

	// Scales for any team size
	bool bScalesForTeamSize;

	// Time in seconds that a player has to finish the mission once it is assigned. Optional.
	U32 uTimeout;							AST( NAME("TimeToComplete", "TimeToComplete:") )

	// Name of the map needed to autogrant this mission to a player
	const char* autoGrantOnMap;				AST( NAME("GrantOnMap", "GrantOnMap:") SERVER_ONLY POOL_STRING )

	// Requirements for this mission to become available to the player
	Expression* missionReqs;				AST( NAME("RequiresBlock") REDUNDANT_STRUCT("Requires:", parse_Expression_StructParam) LATEBIND SERVER_ONLY)

	Expression *pMapRequirements;			AST( NAME("MapRequiresBlock") LATEBIND SERVER_ONLY )

	Expression *pMapSuccess;				AST( NAME("MapSuccessBlock") LATEBIND SERVER_ONLY )
	Expression *pMapFailure;				AST( NAME("MapFailureBlock") LATEBIND SERVER_ONLY )

	const char** ppchRequiresAllActivities; AST( NAME("RequiresActivityAnd") POOL_STRING )
	// The mission requires that *ALL* of the activities in this array are active in order to be available to the player
	
	const char** ppchRequiresAnyActivities; AST( NAME("RequiresActivityOr") POOL_STRING )
	// The mission requires that *ANY* of the activities in this array are active in order to be available to the player

	//The name of an event that the mission is connected to; used for open missions that have events that control them
	const char* pchRelatedEvent;			AST( NAME("RelatedEvent") POOL_STRING )

	// Whether or not the mission can be taken after having been completed
	bool repeatable;						AST( NAME("CanRepeat", "CanRepeat:"))

	// Cooldown time for how often the player can complete the Mission.  This checks when you last COMPLETED the mission.
	F32 fRepeatCooldownHours;				AST( NAME("RepeatCooldownHours"))

	// Cooldown time for how often the player can complete the Mission. This checks from when you last STARTED the mission.
	F32 fRepeatCooldownHoursFromStart;		AST( NAME("RepeatCooldownHoursFromStart"))

	// Number of times that the mission can be run before it enter cooldown. Only values > 1 will
	// produce behavior different from that of pre-fRepeatCooldownTimes behavior. 
	// This is used for both repeat cooldown and cooldown from start
	U32 iRepeatCooldownCount;				AST( NAME("RepeatCooldownCount"))

	// Used with iRepeatCooldownCount
	// If true then mission cooldown start / complete times are set to the nearest block based on cooldown time
	// Example 24 in fRepeatCooldownHours would result in mission cooldwon times being started at 12:00am and ending 24 hours later
	// Example 8 in fRepeatCooldownHours would result in mission cooldwon times being started at 12:00am 08:00am and 4:00pm and ending 8 hours later
	// When it is past the last block then its in a new cooldown block (count starts over again).
	bool bRepeatCooldownBlockTime;				AST( NAME("RepeatCooldownBlockTime"))

	// If set, then do not set a cooldown if the player fails or drops a mission
	bool bCooldownOnlyOnSuccess;				AST( NAME("CooldownOnlyOnSuccess") )

	// Whether or not the mission needs to be returned to a contact
	bool needsReturn;						AST( NAME("NeedsReturn", "NeedsReturn:") DEF(1) )

	// Whether the map should only let a certain set of players have this Mission
	MissionLockoutType lockoutType;			AST( SERVER_ONLY )

	// TODO: This should be flatted into this data structure when we
	// next do a mission file rewrite
	MissionDefParams* params;				AST( NAME("Parameters", "Parameters:") LATEBIND SERVER_ONLY )

	// Success and Failure conditions
	MissionEditCond* meSuccessCond;			AST( NAME("SuccessWhen") SERVER_ONLY )
	MissionEditCond* meFailureCond;			AST( NAME("FailureWhen") SERVER_ONLY )
	MissionEditCond* meResetCond;			AST( NAME("ResetWhen") SERVER_ONLY )

	// Actions that the Mission must perform
	WorldGameActionProperties** ppOnStartActions;			AST( NAME("OnStartAction") SERVER_ONLY )
	WorldGameActionProperties** ppSuccessActions;			AST( NAME("OnSuccessAction") SERVER_ONLY )
	WorldGameActionProperties** ppFailureActions;			AST( NAME("OnFailureAction") SERVER_ONLY )
	WorldGameActionProperties** ppOnReturnActions;			AST( NAME("OnReturnAction") SERVER_ONLY )

	// Interactable additonal properties that are active while the server knows about this mission
	InteractableOverride** ppInteractableOverrides;	AST( SERVER_ONLY )

	// Additional special dialog blocks that are active on matching contacts while the server knows about this mission
	SpecialDialogOverride** ppSpecialDialogOverrides;	AST( SERVER_ONLY )

	ActionBlockOverride **ppSpecialActionBlockOverrides; AST( SERVER_ONLY )

	// Additional mission offers that are active on matching contacts while the server knows about this mission
	MissionOfferOverride** ppMissionOfferOverrides;		AST( SERVER_ONLY )

	// Additional Image Menu items that are active on matching contacts while the server knows about this mission
	ImageMenuItemOverride **ppImageMenuItemOverrides;	 AST( SERVER_ONLY )

	// List of submissions that can be given by the mission
	MissionDef** subMissions;				AST( NAME("SubMission","SubMission:") NO_INDEX )

	// Waypoint to be displayed for this mission.  Optional.
	MissionWaypoint** eaWaypoints;				AST( SERVER_ONLY NAME(Waypoint) )

	// Information for a "Return to Contact" bullet to display when the mission is complete
	MissionReturnType eReturnType;
	DisplayMessage msgReturnStringMsg;		AST(STRUCT(parse_DisplayMessage))

	// Perks: Expression used to determine when the Perk is "discovered"
	Expression *pDiscoverCond;				AST( SERVER_ONLY LATEBIND )

	// Events tracked by this Mission
	GameEvent** eaTrackedEvents;			AST( SERVER_ONLY NAME("TrackedEvents") )

	// Open Missions: Events used to calculate Scores
	OpenMissionScoreEvent** eaOpenMissionScoreEvents;   AST( SERVER_ONLY )

	// The progression nodes for this mission (Derived at load-time)
	const char** ppchProgressionNodes;		AST(POOL_STRING NO_TEXT_SAVE)

	// The required minimum level in order to play this mission
	S32 iMinLevel; AST( NAME("MinLevel") )

	// If specified, the player must be one of the following allegiances in order to play this mission
	AllegianceRef** eaRequiredAllegiances;	AST( NAME("RequiredAllegiance") )

	// If true, then the player must have all of the allegiances in eaRequiredAllegiances. Useful for Allegiance and Sub-Allegiance matching
	bool bRequireAllAllegiances;			AST( NAME("RequireAllAllegiances") )

	// Open Missions: Related personal mission. Used for mission tracking.
	const char *pcRelatedMission;			AST( NAME("RelatedMission", "RelatedMission:") POOL_STRING RESOURCEDICT(Misison))

	// Requests for other Missions to take place
	MissionDefRequest** eaRequests;			AST( SERVER_ONLY )

	// Number of "points" a Perk is worth
	U32 iPerkPoints;

	// Sort priority to use when displaying this in a list on the UI
	S32 iSortPriority; AST(NAME("SortPriority"))

	// When completed, this mission is still displayed as being in progress.  Useful for hand-off missions.
	bool bIsHandoff;  

	// Once completed, this mission is always complete
	bool doNotUncomplete;

	// Make the mission so it can't be dropped
	bool doNotAllowDrop;

	// Whether this mission can be shared
	MissionShareableType eShareable;				AST(NAME("Shareable"))

	// This mission can be requested by a "Mission Request"
	MissionRequestGrantType eRequestGrantType;		AST(NAME("RequestGrantType"))

	// Mission variables to set on this mission
	WorldVariableDef **eaVariableDefs;				AST(SERVER_ONLY)

	// Flag to disable completion tracking of this mission (no completed mission struct will be created upon completion
	bool bDisableCompletionTracking;

	// Specifies what the source for this content is (Cryptic/UGC/etc)
	ContentAuthorSource eAuthorSource;				AST(NAME("AuthorSource"))

	// Specifies gameplay style, can affect rewards
	MissionPlayType ePlayType;						AST(NAME("PlayType"))

	// If this came from a UGCProject, this is the project's container ID 
	ContainerID ugcProjectID;						AST(NAME("UGCProjectID"))

	// ---- Data below here is NO_TEXT_SAVE and is postprocessed on load ----

	// Postprocessed on load, ref string for the mission
	const char* pchRefString;				AST( NO_TEXT_SAVE POOL_STRING )

	// Display messages for this def
	// TODO: do we always need them?  Right now they're binned with the mission.  Can we avoid generating them when we're not editing?
	// Used during editing
	DisplayMessageList varMessageList;		AST( NO_TEXT_SAVE STRUCT(parse_DisplayMessageList) ) 

	// Expressions for the (x/y) display in the mission UI, e.g. "Defeat 10 thugs (8/10)"
	Expression* successCount;				AST( NO_TEXT_SAVE LATEBIND SERVER_ONLY )
	Expression* successTarget;				AST( NO_TEXT_SAVE LATEBIND SERVER_ONLY )
	MDEShowCount showCount;				AST( NAME("ShowCount") NO_TEXT_SAVE )

	// Post processed; Whether or not the mission is required
	U32 isRequired : 1;						AST( NO_TEXT_SAVE SERVER_ONLY )

	// Tracked Events that don't bother saving a count, but still need to trigger the Mission to refresh
	GameEvent** eaTrackedEventsNoSave;		AST( NO_TEXT_SAVE SERVER_ONLY )

	// PlayerStats tracked by this mission
	const char** eaTrackedStats;			AST( NO_TEXT_SAVE SERVER_ONLY POOL_STRING )

	// Current wrapped mission. Used to pass mission names as variables into other missions
	const char *pchTrackedMission;				AST( NO_TEXT_SAVE SERVER_ONLY POOL_STRING )

	// Once completed, refresh the owner entity's autobuy powers.
	bool bRefreshCharacterPowersOnComplete;

	bool bOmitFromMissionTracker;

	// The mission tag assigned to this mission
	MissionTag* peMissionTags;				AST(NAME(MissionTag), SUBTABLE(MissionTagEnum))

	// The mission type for UI display purposes
	MissionUIType eUIType;					AST(NAME(MissionUIType) SUBTABLE(MissionUITypeEnum))

	// Used for perks to determine whether they are a tutorial perk or not
	bool bIsTutorialPerk;

	// Used for open missions with scoreboard based awards. If enabled, players without a score are not rewarded at all.
	bool bOnlyRewardIfScored;

	// Allows an open mission to grant rewards that may be unreliable in some situations.  
	// Leave this as 0 unless someone who understands it says it's okay.
	bool bSuppressUnreliableOpenRewardErrors;

	// Which regions on the screen to point to for tutorial perks
	TutorialScreenRegion eTutorialScreenRegion;	AST(NAME(TutorialScreenRegion) SUBTABLE(TutorialScreenRegionEnum))
	
} MissionDef;
extern ParseTable parse_MissionDef[];
#define TYPE_parse_MissionDef MissionDef


// Shared mission template list
extern DictionaryHandle g_MissionTemplateTypeDict;
extern DictionaryHandle g_MissionVarTableDict;
extern MissionTemplateType **g_MissionTemplateTypeList;

// An event that a mission cares about that happens in a mission, in the world, etc.
AUTO_STRUCT AST_CONTAINER;
typedef struct MissionEventContainer
{
	// The name of the Event
	const char* pchEventName;			AST( STRUCTPARAM PERSIST KEY NAME("EventName") POOL_STRING NO_TRANSACT )

	// Current value associated with the event
	int iEventCount;					AST( STRUCTPARAM PERSIST NAME("EventCount") NO_TRANSACT)
} MissionEventContainer;

AUTO_STRUCT AST_CONTAINER;
typedef struct CompletedMissionStats
{
	// The Nemesis used to complete this mission
	const ContainerID iNemesisID;			AST( PERSIST NAME("NemesisID") FORMATSTRING(DEPENDENT_CONTAINER_TYPE = "EntitySavedPet"))

	// -- Statistics about the mission --

	// Timestamp for when this mission was completed
	const U32 firstCompletedTime;			AST( PERSIST )
	const U32 lastCompletedTime;			AST( PERSIST )

	// Timestamp for when this mission was started
	const U32 firstStartTime;				AST( PERSIST )
	const U32 lastStartTime;				AST( PERSIST )

	// Best completed time
	const U32 bestTime;						AST( PERSIST )

	// Number of times this mission has been repeated
	const U16 timesRepeated;				AST( PERSIST )
	
} CompletedMissionStats;

AUTO_STRUCT AST_CONTAINER;
typedef struct CompletedMission
{
	// The definition of the mission that was completed
	CONST_REF_TO(MissionDef) def;			AST( PERSIST KEY REFDICT(Mission) SUBSCRIBE )

	// Whether this should be hidden in the journal
	// TODO - Move this to something on the def instead
	const bool bHidden;						AST( PERSIST )

	// Stats for non-repeatable Missions
	const U32 completedTime;				AST( PERSIST )
	const U32 startTime;					AST( PERSIST )
	
	// Number of times repeated against current cooldown timer, not needed in stats just in the direct complete mission
	const U32 iRepeatCooldownCount;			AST( PERSIST )

	// Stats for repeatable Missions or Nemesis missions (which are usually repeatable)
	CONST_EARRAY_OF(CompletedMissionStats) eaStats;		AST( PERSIST NAME("Stats"))

	// non-persisted data
	DirtyBit dirtyBit;						AST( NO_NETSEND )

} CompletedMission;


// Contains all debugging information for a mission that needs to be sent to the client
AUTO_STRUCT;
typedef struct MissionDebug
{
	// This is a copy of the event log so that it gets sent to the client
	MissionEventContainer** debugEventLog;
} MissionDebug;

AUTO_STRUCT;
typedef struct OpenMissionScoreEntry{
	ContainerID playerID;		AST(KEY)
	char *pchPlayerName;		AST(ESTRING)// Stored in case the player has left
	char *pchAccountName;		AST(ESTRING)
	PlayerCostume *pCostume;	AST(CLIENT_ONLY)
	F32 fPoints;
	S32 iRank;

	//Used in the UI
	S32 iRewardTier;
	bool bIsHeader;
} OpenMissionScoreEntry;

AUTO_STRUCT;
typedef struct OpenMission{
	const char* pchName;	AST( KEY POOL_STRING)
	Mission* pMission;
	
	// Time remaning before mission resets, in seconds
	// (Sent to client as an Int to make sends less frequent)
	F32 fResetTimeRemaining;	AST( SERVER_ONLY )
	U32 uiResetTimeRemaining;

	// Scoreboard
	// this is indexed by ContainerID (container type is always ENTITYPLAYER)
	OpenMissionScoreEntry** eaScores;

	// An array of participants which should receive credit for completing the open mission
	ContainerID* eaiParticipants;

	// A timestamp for when the mission transitioned to the "succeeded" state
	U32 uiSucceededTime;

	// TODO_PARTITION: flesh this out
	// partition this open mission lives on
	int iPartitionIdx;
} OpenMission;

__forceinline int openmission_GetPartitionIdx(OpenMission *o) { return SAFE_MEMBER(o,iPartitionIdx); }

AUTO_STRUCT;
typedef struct OpenMissionScoreEvent{
	GameEvent *pEvent;
	F32 fScale;
} OpenMissionScoreEvent;

// A running mission that can be attached to a player, encounter, zone, etc.
AUTO_STRUCT AST_CONTAINER AST_SINGLETHREADED_MEMPOOL;
typedef struct Mission
{
	// Name of sub-mission
	CONST_STRING_POOLED missionNameOrig;					AST( PERSIST SUBSCRIBE KEY NAME("MissionName") POOL_STRING ) 

	// Version of the MissionDef used to create this Mission (root missions only)
	const U32 version;										AST(PERSIST)

	// List of sub-missions of this task.
	CONST_EARRAY_OF(Mission) children;						AST(PERSIST SUBSCRIBE NO_INDEXED_PREALLOC)

	// List of full mission children of this task.
	CONST_STRING_EARRAY childFullMissions;					AST(PERSIST)

	const int displayOrder;									AST(PERSIST SUBSCRIBE)

	// TemplateVariableGroup used to override variables, for MadLibs missions
	// Stored as a String to avoid AST_CONTAINER weirdness
	CONST_STRING_MODIFIABLE pchTemplateVarGroupOverride;	AST(PERSIST)

	// WorldVariables
	CONST_EARRAY_OF(WorldVariableContainer) eaMissionVariables;	AST(PERSIST FORCE_CONTAINER NO_INDEXED_PREALLOC)

	// Current state of the mission.
	const MissionState state;								AST(PERSIST SUBSCRIBE)

	// Time when the mission began.
	const U32 startTime;									AST( PERSIST SUBSCRIBE)

	// The start time to use for the sake of the timer.  (Default to "startTime")
	const U32 timerStartTime;								AST( PERSIST SUBSCRIBE)

	// Keeps track of Events that happened during this Mission
	MissionEventContainer** eaEventCounts;					AST( PERSIST NO_TRANSACT SERVER_ONLY NAME("EventCounts") NO_INDEXED_PREALLOC)

	// Root missions only: What type of credit the player will receive for this mission
	const MissionCreditType eCreditType;					AST( PERSIST SUBSCRIBE NAME("CreditType"))

	// Never uncomplete this mission (so the MMM Complete, CSR commands, etc. work)
	const bool permaComplete;								AST( PERSIST )

	// Whether or not this mission is being tracked.
	bool tracking;											AST( PERSIST NO_TRANSACT )

	// Perks: Whether this Perk has been "discovered" and should appear in the UI
	bool bDiscovered;										AST( PERSIST NO_TRANSACT )

	// Whether this is a child full mission
	const bool bChildFullMission;							AST( PERSIST SUBSCRIBE )

	// Whether this is a hidden child.  This is used on child full missions
	const bool bHiddenFullChild;							AST( PERSIST SUBSCRIBE )

	// The level of the mission.  If < 1, the missionDef's specified level is used
	const U8 iLevel;										AST( PERSIST )

	// Is this mission hidden on the HUD ui? If true then it will only show if there are
	// no other missions on hud and it is highest time stamp.
	bool bHidden;											AST( PERSIST NO_TRANSACT SELF_ONLY)

	// Optional tracked mission. Tracked missions are missions that this mission knows the state of
	CONST_STRING_MODIFIABLE pchTrackedMission;				AST( PERSIST SUBSCRIBE NAME("TrackedMission") )

	// UGC-specific data for a Mission, if it is from a UGC project
	CONST_OPTIONAL_STRUCT(UGCMissionData) pUGCMissionData;	AST(PERSIST SUBSCRIBE) // not all fields of this struct are open to client for display

	// ---- The following fields are NOT persisted, but are sent on network ----

	// The original def, before MadLibs generation, if applicable.
	REF_TO(MissionDef) rootDefOrig;

	// Don't reference these directly unless you have a reason to.  Instead, use mission_GetDef()
	// The definition of the mission. Derived from origDef on load.
	REF_TO(MissionDef) rootDefOverride;
	// Name of sub-mission
	char* missionNameOverride;
	
	// Count and Target of objectives completed, for UI
	int count;
	int target;

	// Expiration time for timed missions, in ServerSecondsSince2000
	U32 expirationTime;

	// Debugging information about the current mission, only filled out if debugging is enabled
	// I don't anyone actually uses this
	MissionDebug* debugInfo;

	// Flag for whether this mission needs to be expanded in the UI
	U32 openChildren : 1;

	// The depth of this mission in the tree, used for the UI
	U32 depth;

	DirtyBit dirtyBit;							AST( NO_NETSEND )


	// ---- The following NO_WRITE and NO_AST fields are post-processed in memory  ----
	// ---- and are only available on the client or server since they are not sent ----

	int lastCount;								NO_AST
	F32 flashTime;								NO_AST

	// Points to the missions parent, will only exist if this is a submission of another
	Mission* parent;							NO_AST

	// This is a pointer to the info this mission is a part of. Only exists when a missions is attached to a player.
	MissionInfo* infoOwner;						NO_AST

	// This is a pointer to the Open Mission this mission is a part of. Only exists when a mission is part of an Open Mission.
	OpenMission* pOpenMission;					NO_AST

	// Needs to be reevaluated because some event changed
	U32	needsEval : 1;							NO_AST

	// Set when UGC namespace missions are being checked for validity
	U32	bCheckingValidity : 1;					NO_AST

	// List of events this mission is tracking (player-scoped copies of Events in the MissionDef)
	GameEvent** eaTrackedEvents;				NO_AST

	// List of open mission scoreboard events this mission is tracking (player-scoped copies of Events in the MissionDef)
	OpenMissionScoreEvent** eaTrackedScoreboardEvents;		NO_AST

} Mission;


AUTO_STRUCT;
typedef struct MissionOfferParams{
	// The player who shared this mission
	GlobalType eSharerType;
	ContainerID uSharerID;

	// The start time to use for any timers on the mission
	U32 uTimerStartTime;

	// The type of credit that the player will receive for this mission
	MissionCreditType eCreditType;

	// The mission that is parent for this one
	// This can be used when a full mission grants another full mission
	// It only works if the parent is a persisted mission.  It does not work on non-persisted ones.
	const char *pchParentMission;
	const char *pchParentSubMission;

} MissionOfferParams;

AUTO_STRUCT AST_CONTAINER;
typedef struct MissionCooldown{
	// Mission on cooldown
	CONST_STRING_POOLED pchMissionName;		AST(POOL_STRING KEY PERSIST SUBSCRIBE)
	
	// Time when the mission was started
	const U32 startTime;					AST( PERSIST SUBSCRIBE)

	// Time when the mission was completed or dropped
	const U32 completedTime;				AST( PERSIST SUBSCRIBE)

	// number of times this mission has been started during current cooldown period
	const U32 iRepeatCooldownCount;			AST( PERSIST SUBSCRIBE)
	
} MissionCooldown;

AUTO_STRUCT;
typedef struct MissionCooldownInfo
{
	// is this mission in cooldwn and therefore can not be taken?
	bool bIsInCooldown;
	
	// This mission is in a cooldown block time, this could be true and bIsInCooldown false
	// if the mission has a cooldowncount 
	bool bIsInCooldownBlock;

	// Number of seconds left in cooldown block
	U32 uCooldownSecondsLeft;	
	
	// how many times has the cooldown repeat count been used
	U32 uRepeatCount;

}MissionCooldownInfo;

AUTO_STRUCT;
typedef struct QueuedMissionOffer
{
	// The Mission being offered
	REF_TO(MissionDef) hMissionDef;
	
	// Player who shared the Mission, if any
	GlobalType eSharerType;
	ContainerID uSharerID;

	// CreditType of the mission
	MissionCreditType eCreditType;

	// Entity to show in the Headshot part of the UI.  Default to the player who shared the Mission.
	EntityRef entHeadshotOverride;

	// Whether to notify the sharing player when the Mission is accepted/declined
	bool bSilent;

	// Time this offer was queued, for timeout
	U32 timestamp;

	// For timed missions, this is used to share the state of the timer
	U32 uTimerStartTime;
} QueuedMissionOffer;

// Structure to hold all interaction-related data
AUTO_STRUCT;
typedef struct InteractInfo
{
	DirtyBit dirtyBit;							AST(NO_NETSEND)
	// Ref to a contact that this player is currently interacting with.
	// If the player is in some sort of nested dialog, this is always the root contact
	REF_TO(ContactDef) hRootInteractContact;	AST( REFDICT(Contact) ) 

	// Ref to an item that is currently granting a mission to the player
	REF_TO(ItemDef) interactItem;				AST( REFDICT(ItemDef) )

	// Shared Mission the player is currently interacting with
	QueuedMissionOffer *pSharedMission;

	// The state of all contacts considered nearby the player
	ContactInfo** nearbyContacts;				AST( LATEBIND )

	// A list of nearby contacts that have been verified to be in interact range
	// The pointers in this list are unowned. The real pointers reside in nearbyContacts.
	// Invalid if the current frame is not equal to uLastVerifyInteractFrame.
	ContactInfo** eaInteractableContacts;		AST(CLIENT_ONLY)

	// Last frame that nearby contacts were verified to be in interact range
	U32 uLastVerifyInteractFrame;				AST(CLIENT_ONLY)

	// Last ContactFlags used to verify nearby interactable contacts
	U32 uLastVerifyContactFlags;				AST(CLIENT_ONLY)

	// A list of Critters that are NOT contacts that are considered nearby the player
	// The list will frequently be empty
	CritterInteractInfo** nearbyInteractCritterEnts;	

	// A Contact Dialog that the player is currently in
	ContactDialog* pContactDialog;				AST( LATEBIND )

	// Recently completed contact dialogs
	ContactDialogInfo **recentlyCompletedDialogs;	AST( LATEBIND SERVER_ONLY )

	// Recently viewed "force on team" contact dialogs
	ContactDialogInfo **recentlyViewedForceOnTeamDialogs;	AST( LATEBIND SERVER_ONLY )

	// List of recently completed interacts
	InteractionInfo **recentlyCompletedInteracts;	AST( LATEBIND SERVER_ONLY )

	// Queued contact dialog to appear when the current dialog is closed
	QueuedContactDialog *pQueuedContactDialog;		AST( LATEBIND SERVER_ONLY )
	
	// Queued contact to show once player exits combat
	QueuedContactDialog *pQueuedNotInCombatDialog;	AST( LATEBIND SERVER_ONLY )
	
	ContactCostumeFallback *pCostumeFallback;		AST( LATEBIND SERVER_ONLY )

	// Whether the player is interacting with a crafting table
	bool bCrafting;

	// Interaction table data for the UI; only valid if bCrafting is true
	// Note: client doesn't have access to interactible entries, so this must be set by the server
	SkillType eCraftingTable;					// skill type of the table
	int iCraftingMaxLevel;						// max level of the table

	//List of remote contacts which the player can interact with
	RemoteContact **eaRemoteContacts;	
	
	// The next time the contact system should automatically update remote contacts for this player
	U32 uNextRemoteContactUpdateTime;					AST(SERVER_ONLY)
	bool bUpdateRemoteContactsNextTick;					AST(SERVER_ONLY)

	// The next time the contact system should automatically update its contact dialog options for this player
	U32 uNextContactDialogOptionsUpdateTime;			AST(SERVER_ONLY)
	bool bUpdateContactDialogOptionsNextTick;			AST(SERVER_ONLY)

	// Whether the player only has partial permissions because the player is remotely interacting with a contact
	bool bPartialPermissions;							AST(SERVER_ONLY)

	// Whether the player is accessing the contact remotely
	bool bRemotelyAccessing;							AST(SERVER_ONLY)
	
	// True if the player is waiting on a contact dialog to close before starting a map transfer
	bool bAwaitingMapTransfer;							AST(SERVER_ONLY)

	// The player is in the process of purchasing something with a requires expression
	// While this is true, no other purchase with a requires expression is allowed
	bool bPurchaseInProgress;							AST(SERVER_ONLY)

	// The list of recently sold items available to buy back
	StoreItemInfo **eaBuyBackList;

	// The names of the special dialogs visited by the player
	char **ppchVisitedSpecialDialogs;					AST(SERVER_ONLY)
} InteractInfo;
extern ParseTable parse_InteractInfo[];
#define TYPE_parse_InteractInfo InteractInfo

// Structure for storing a teammate's mission info, as requested by my team mission UI
AUTO_STRUCT;
typedef struct TeammateMission
{
	// The teammate's entity ID
	ContainerID iEntID;
	
	// The mission instance, or NULL if they don't have the mission
	Mission *pMission;
	
	// This field is only relevant if pMission is NULL
	// Specifies what kind of credit they would get for the mission if it were shared to them now
	MissionCreditType eCreditType;
} TeammateMission;

// Structure for storing info about each teammate's instance of a given mission
AUTO_STRUCT;
typedef struct TeamMissionInfo
{
	// The def of the mission in question
	REF_TO(MissionDef) hDef;
	
	// An array of each teammate's instance of or status with the mission
	EARRAY_OF(TeammateMission) eaTeammates;
} TeamMissionInfo;

AUTO_STRUCT;
typedef struct OpenMissionLeaderboardInfo
{
	char *pOpenMissionDisplayName;		AST(ESTRING)
	OpenMissionScoreEntry **eaLeaders;	AST(NO_INDEX)
	S32 iTotalParticipants;				
	S32 iPlayerIndex;
	S32 iRewardTier;
	const char *pchRelatedEvent;		AST(POOL_STRING)
	REF_TO(EventDef) hRelatedEvent;
	bool bIsRelatedEventActive;
} OpenMissionLeaderboardInfo;

// Structure attached to the player containing everything mission related
// eaRequests - Safe to remove this after launch.  This data should only have existed on development characters anyway.
AUTO_STRUCT AST_CONTAINER AST_IGNORE_STRUCT("eaRequests");
typedef struct MissionInfo
{
	// Contains all missions currently assigned to a player
	CONST_EARRAY_OF(Mission) missions;						AST( PERSIST SUBSCRIBE )

	// Contains a list of all missions the player has completed
	CONST_EARRAY_OF(CompletedMission) completedMissions;	AST( PERSIST SUBSCRIBE SELF_ONLY)

	// Contains a list of missions that the player has recently completed for non-primary credit
	CONST_EARRAY_OF(CompletedMission) eaRecentSecondaryMissions;	AST( PERSIST SERVER_ONLY NAME("RecentSecondaryMissions"))

	// Tracks any missions currently being requested by "controller" missions
	CONST_EARRAY_OF(MissionRequest) eaMissionRequests;		AST( PERSIST SERVER_ONLY NAME("MissionRequests"))
	const U32 uNextRequestID;								AST( PERSIST SERVER_ONLY )

	// Contains a list of repeatable missions which have not been completed for primary credit, but are cooling down due to
	// either secondary completion or being dropped.  Missions only appear here if they are not represented on the completed mission list.
	CONST_EARRAY_OF(MissionCooldown) eaMissionCooldowns;	AST(PERSIST SUBSCRIBE)
	
	// The primary mission when soloing
	const char* pchPrimarySoloMission;						AST( PERSIST SUBSCRIBE POOL_STRING NO_TRANSACT ADDNAMES(ePrimarySoloMission))

	// ---- The following fields are NOT persisted, but are sent on network ----

	// Missions that the player needs to track, but do not need to be persisted, e.g. Perks
	Mission **eaNonPersistedMissions;			AST( SERVER_ONLY )

	// Discovered perks that haven't been persisted
	Mission **eaDiscoveredMissions;				AST( SELF_ONLY )

	// "Fake" version of the team's Primary Mission for UI display purposes
	Mission *pTeamPrimaryMission;				AST( SELF_AND_TEAM_ONLY )
	
	// Info on all missions that any teammates have
	// Only filled in on request with the "mission_GetTeamMissions" server command
	EARRAY_OF(TeamMissionInfo) eaTeamMissions;	AST( SELF_ONLY )
	
	// Which subobjective of the Team's primary mission the team is working on
	const char *pchTeamCurrentObjective;		AST( POOL_STRING )

	// The last mission this player has made progress on
	const char *pchLastActiveMission;			AST( POOL_STRING )
	U32 uLastActiveMissionTimestamp;
	
	// Keep track of the last mission rating request ProjectID and whether the player was beta reviewing it
	U32 uLastMissionRatingRequestID;			AST( SELF_ONLY )
	bool bLastMissionPlayingAsBetaReviewer;		AST( SELF_ONLY )

	MinimapWaypoint **waypointList;

	// Sent down with client commands
	GameTimer** clientGameTimers;				AST( CLIENT_ONLY )

	DirtyBit dirtyBit;							AST( NO_NETSEND )

	// Queue of missions that should be offered to the player
	QueuedMissionOffer** eaQueuedMissionOffers;   AST( SERVER_ONLY )

	// Current Open Mission to display
	const char *pchCurrentOpenMission;			AST( POOL_STRING )
	U32 openMissionVolumeTimestamp;				AST( SERVER_ONLY )
	OpenMissionLeaderboardInfo *pLeaderboardInfo; AST(SELF_ONLY)

	const char *pchTrackedMission;				AST( SERVER_ONLY ) // Deprecated

	// Contact that this player currently has a waypoint to
	REF_TO(ContactDef) hTrackedContact;			AST( SERVER_ONLY )

	// Number of Perk Points the player has (cached for performance)
	U32 iTotalPerkPoints;

	// The list of entity refs that this player cares about (escorting).
	EntityRef *eaiEscorting;

	bool bHasTeamCorral;						AST( SELF_ONLY )

	U32 uLastTeamCorralUpdateTime;				AST( SELF_ONLY )

	// ---- The following NO_WRITE and NO_AST fields are post-processed in memory  ----
	// ---- and are only available on the client or server since they are not sent ----

	// Contains a back pointer to the entity who this info belongs to
	Entity* parentEnt;							NO_AST

	// Tracks Events scoped to the player (or team)
	EventTracker *eventTracker;					NO_AST

	// True if something in the missions array needs to be reevaluated
	U32 needsEval : 1;							NO_AST

	// When turned on, sends debugging info to the client
	U32 showDebugInfo : 1;						NO_AST

	// True if this player's waypoints need to be updated
	U32 bWaypointsNeedEval : 1;					NO_AST

	// True if this player's Perk Points need to be updated
	U32 bPerkPointsNeedEval : 1;				NO_AST

	// True if this player's Mission Requests need to be updated
	U32 bRequestsNeedEval : 1;					NO_AST

	// True if any Mission this player has is from a namespace
	U32 bHasNamespaceMission : 1;				NO_AST

	// True if this player's mission list needs to be checked for validity (e.g. if mission versions are out-of-date)
	U32 bMissionsNeedVerification : 1;			NO_AST
	U16 iNumVerifyAttempts;						NO_AST

} MissionInfo;

// A Category for the Mission Journal
AUTO_STRUCT;
typedef struct MissionCategory{
	const char* name;					AST( STRUCTPARAM KEY POOL_STRING )
	const char* scope;					AST( SERVER_ONLY POOL_STRING )
	const char* filename;				AST( CURRENTFILE )

	DisplayMessage displayNameMsg;      AST( STRUCT(parse_DisplayMessage) )
} MissionCategory;

// Message references for hardcoded Mission System messages
typedef struct MissionSystemMessages
{
	REF_TO(Message) hFlashbackDisplayName;
} MissionSystemMessages;

extern MissionSystemMessages g_MissionSystemMsgs;


//Structs for sending monster manual data from server to client.
AUTO_STRUCT;
typedef struct StoredCritterLoreEntry
{
	const char* pchName; AST(POOL_STRING)

	char* estrDisplayName; AST(ESTRING)
	//critter's costume
	REF_TO(PlayerCostume) hCostume;

	//attribs that the client needs to know for this critter at the player's level
	S32* eaiAttribs;

	//values for above attribs
	F32* eafValues;

	StashTable pAttrStash;	NO_AST

}StoredCritterLoreEntry;
extern ParseTable parse_StoredCritterLoreEntry[];
#define TYPE_parse_StoredCritterLoreEntry StoredCritterLoreEntry

AUTO_STRUCT;
typedef struct CritterLoreList
{
	StoredCritterLoreEntry** eaCritterData;
}CritterLoreList;
extern ParseTable parse_CritterLoreList[];
#define TYPE_parse_CritterLoreList CritterLoreList

AUTO_STRUCT;
typedef struct RequestedCritterAttribs
{
	S32* eaiAttribs;
}RequestedCritterAttribs;
extern ParseTable parse_RequestedCritterAttribs[];
#define TYPE_parse_RequestedCritterAttribs RequestedCritterAttribs

AUTO_STRUCT;
typedef struct MissionDefRef
{
	REF_TO(MissionDef) hMission;				AST( REFDICT ( Mission ) NAME ( Mission ) STRUCTPARAM )
} MissionDefRef;

AUTO_STRUCT;
typedef struct MissionRefList
{
	MissionDefRef** eaRefs; AST(NAME(MissionDef))
} MissionRefList;

AUTO_STRUCT;
typedef struct CachedMissionRequest
{
	const char** ppchMissionDefs; AST(POOL_STRING)
} CachedMissionRequest;

AUTO_STRUCT;
typedef struct CachedMissionData
{
	const char* pchMissionDef; AST(POOL_STRING KEY)
		// The internal mission name
	REF_TO(Message) hDisplayName;
		// The display name of the mission
	const char* pchContact; AST(POOL_STRING)
		// The contact accessible through as a RemoteContact that can grant this mission
	const char* pchProgressionNode; AST(POOL_STRING)
		// The progression node for this mission
	REF_TO(Message) hContactDisplayName;
		// The display name of the contact
	char* pchContactKey;
		// The contact dialog key to grant the mission
	bool bRemoteContact;
		// The contact information provided is not a remote contact.
	MissionCreditType eCreditType;
		// The credit for completing the mission at this time
	MissionState eState;
		// The state of the mission when this data was requested
	S32 iMinLevel;
		// The minimum level required for this mission
	U32 uSecondaryLockoutTime;
		// If non-zero, this is the amount of time that this mission is locked out for secondary credit
	U32 uTime; AST(CLIENT_ONLY)
		// The age of this information
	U32 bAvailable : 1;
		// Whether or not the server thinks this mission is available to the player
	U32 bVisible : 1;
		// Whether or not the mission is visible to the player
	U32 bUpdate : 1; AST(CLIENT_ONLY)
		// If an update request has already been made for this data
	U32 bCurrent : 1; AST(CLIENT_ONLY)
		// Set if this mission is in the player's current progression node
} CachedMissionData;

AUTO_STRUCT;
typedef struct CachedMissionReward
{
	const char* pchMissionDef; AST(POOL_STRING KEY)
		// The internal mission name
	InventoryBag** eaRewardBags; AST(NO_INDEX)
		// The rewards data
	U32 uTime; AST(CLIENT_ONLY)
		// The age of this information
	U32 bUpdate : 1; AST(CLIENT_ONLY)
		// If an update request has already been made for this data
} CachedMissionReward;

AUTO_STRUCT;
typedef struct CachedMissionList
{
	CachedMissionData** eaData;
	CachedMissionReward** eaRewardData;
} CachedMissionList;

// Reference system dictionary handle for mission
extern DictionaryHandle g_MissionDictionary;
extern DictionaryHandle g_MissionCategoryDict;

// Loads all missiondefs
int missiondef_LoadMissionDefs(void);
void missiondef_FixBackPointers(MissionDef* pDef, MissionDef* parentDef);
bool missiondef_Validate(MissionDef* def, MissionDef *rootDef, bool isChild);

// Loads all Mission Categories
int missiondef_LoadCategories(void);

// Validate a StoryArcDef
bool mission_ValidateStoryArc(StoryArcDef* pDef);
// Get the Story Arc expression context
ExprContext* mission_GetProgressionContext(Entity* pEnt);

// Check to see if the player meets the requirements for a progression mission
bool mission_CheckProgressionRequirements(Entity* pPlayerEnt, int iPlayerLevel, MissionDef* pMissionDef);

// Create an expression context function table with functions that missions can use
ExprFuncTable* missiondef_CreateExprFuncTable(void);
ExprFuncTable* missiondef_CreateRequiresExprFuncTable(void);
ExprFuncTable* missiondef_CreateOpenMissionExprFuncTable(void);
ExprFuncTable* missiondef_CreateOpenMissionRequiresExprFuncTable(void);
ExprFuncTable* missiontemplate_CreateTemplateVarExprFuncTable(void); // for madlibs template var expressions

// Returns the missioninfo attach to the player entity
MissionInfo* mission_GetInfoFromPlayer(SA_PARAM_OP_VALID const Entity* ent);

// Returns a mission matching the specified mission def or name. Does not recurse to child missions.
Mission* mission_GetMissionFromDef(MissionInfo* info, const MissionDef* defToMatch);
Mission* mission_GetMissionByName(MissionInfo* info, const char* missionName);
Mission* mission_GetMissionOrSubMission(MissionInfo* info, const MissionDef* defToMatch);
Mission* mission_GetMissionOrSubMissionByName(MissionInfo* info, const char* missionName);
Mission* mission_GetMissionByOrigName(MissionInfo* info, const char* missionName);

// Finds a child mission with the given name
MissionDef *missiondef_FindMissionByName(MissionDef *rootDef, const char *name);

Mission* mission_FindChildByName(Mission *pParent, const char *pcMissionName);

// Searches the mission tree for a specific mission. Does not have to be a root def. Recurses to child missions.
Mission* mission_FindMissionFromRefString(MissionInfo* info, const char* pchRefKeyString);

// Searches the mission list for a mission that has the specified UGC Project ID
Mission* mission_FindMissionFromUGCProjectID(MissionInfo* info, U32 uProjectID);

// Returns a completed mission that matches the given def
CompletedMission* mission_GetCompletedMissionByDef(const MissionInfo* info, const MissionDef* defToMatch);

// Returns a completed mission that matches the given name
CompletedMission* mission_GetCompletedMissionByName(const MissionInfo* info, const char *name);

// Returns if a player has completed a mission
bool mission_FuncHasCompletedMission(ExprContext *pContext, const char *pcMissionName);

// Returns the number of times a player has completed the specified Mission.
U32 mission_GetNumTimesCompletedByDef(MissionInfo* info, MissionDef* defToMatch);
U32 mission_GetNumTimesCompletedByName(MissionInfo* info, const char *pchMissionName);
U32 completedmission_GetNumTimesCompleted(const CompletedMission *pCompletedMission);

// Gets the timestamp for the last time a mission has been completed
U32 mission_GetLastCompletedTimeByDef(const MissionInfo* pInfo, const MissionDef* pDef);
U32 mission_GetLastCompletedTimeByName(const MissionInfo* pInfo, const char* pchMissionName);
U32 completedmission_GetLastCompletedTime(const CompletedMission *pCompletedMission);
U32 completedmission_GetLastStartedTime(const CompletedMission *pCompletedMission);
U32 completedmission_GetLastCooldownRepeatCount(const CompletedMission *pCompletedMission);

// get the start time in seconds of the passed in time
U32 mission_GetCooldownBlockStart(U32 timeToCheck, U32 coolDownHours, bool bUseBlockTime);

// The common information used in transactions
const MissionCooldownInfo *mission_GetCooldownInfoInternal(MissionDef *pMissionDef, MissionCooldown *pCooldown, CompletedMission *pCompletedMission);

// Get information on this missions cooldown status
const MissionCooldownInfo *mission_GetCooldownInfo(Entity *pEntity, const char *missionName);

// Recursively finds the first In Progress objective for this mission
Mission* mission_GetFirstInProgressObjective(const Mission *pMission);

// Gets the def used for this mission
MissionDef *mission_GetDef(const Mission* mission);
// This gets the original def (before MadLibs generation, if applicable).
// You should normally use mission_GetDef
MissionDef *mission_GetOrigDef(const Mission* mission);


bool mission_HasUIString(const Mission *mission);
bool missiondef_HasUIString(MissionDef *def);

bool mission_HasDisplayName(const Mission *mission);
bool missiondef_HasDisplayName(MissionDef *def);

bool mission_HasReturnString(const Mission *mission);
bool missiondef_HasReturnString(MissionDef *def);

bool mission_HasFailedReturnString(const Mission *mission);
bool missiondef_HasFailedReturnString(MissionDef *def);


bool missiondef_HasSummaryString(MissionDef *def);

// Returns the MissionDef from the RefKeyString (root or child mission)
MissionDef* missiondef_DefFromRefString(const char* missionName);

// Returns the child mission of the given missionDef that matches the name
MissionDef* missiondef_ChildDefFromName(const MissionDef* missionDef, const char* name);
MissionDef* missiondef_ChildDefFromNamePooled(const MissionDef* missionDef, const char* pchPooledName);

// Creates the ref string for a mission def based on the parent def
void missiondef_CreateRefString(MissionDef* def, MissionDef* parentDef);
void missiondef_CreateRefStringsRecursive(MissionDef* def, MissionDef* parentDef);

// Create a full mission def from a def with a template
void missiontemplate_GenerateDefFromTemplate(MissionDef* def, bool recursive);

// Delete submissions that aren't granted
bool missiondef_RemoveUnusedSubmissions(MissionDef* def);

// Clear fields on a Submission that are not legally set on one
void missiondef_CleanSubmission(MissionDef *def);

// Gets all submissions of the current mission in a flat list
int mission_GetSubmissions(Mission* mission, Mission*** missionList, int insertAt, MissionComparatorFunc comparator, MissionAddToListCB addCB, MissionExpandChildrenCB expandCB, bool bRecurse);

// Checks to see if pMission pertains to the map(pchMapName,pchMapVars)
bool mission_HasMissionOrSubMissionOnMap(Mission* pMission, const char* pchMapName, const char* pchMapVars, bool bCheckInProgress);

// If grantedMissions is not null, return a list of all missions that the root def can grant.
// Call the callback on each of these missions (and any GrantMission actions that don't resolve to a child mission def)
typedef void (*FindGrantMissionCallback)(const char* missionName, MissionDef* missionDef, MissionDef* rootDef, WorldGameActionProperties* action, void* data);
void missiondef_FindAllGrantedMissions(MissionDef* def, MissionDef* rootDef, MissionDef*** grantedMissions, FindGrantMissionCallback callback, void* user_data);

// Returns true if the mission has reached a completion state
bool mission_IsComplete(Mission* mission);

// Gets the number of seconds remaining before the Mission expires
U32 mission_GetTimeRemainingSeconds(const Mission *mission);

// Gets how often this mission can be completed for secondary credit (seconds)
U32 missiondef_GetSecondaryCreditLockoutTime(MissionDef *pDef);

// Gets the type of the given Mission
MissionType mission_GetType(const Mission *mission);

// Gets the type of the given MissionDef
MissionType missiondef_GetType(const MissionDef *pDef);

// Returns the root mission def for the given mission def
MissionDef * missiondef_GetRootDef(MissionDef *pDef);

// Determines if pchMapName is represented on pDef's ObjectiveMaps array
bool missiondef_HasObjectiveOnMap(MissionDef* pDef, const char* pchMapName, const char* pchMapVars);
// Gets the MissionMap for the MissionDef given the name of the map
MissionMap* missiondef_GetMissionMap(MissionDef* pDef, const char* pchMapName);

MissionTemplate* missiontemplate_CreateNewTemplate(MissionTemplateType* tempType, int level, const char* mapName);

// Helper functions to look up a variable in a mission template by name
TemplateVariable* missiontemplate_LookupTemplateVarInVarGroup(TemplateVariableGroup* varGroup, const char* varName, bool recursive);
TemplateVariable* missiontemplate_LookupTemplateVar(MissionTemplate* missionTemplate, const char* varName);
void missiontemplate_FindChildGroupsByName(TemplateVariableGroup *varGroup, const char *groupName, TemplateVariableGroup*** subGroups);
bool missiontemplate_VarGroupIsMadLibs(const TemplateVariableGroup *group);

// Formats mission related messages
void missionsystem_FormatMessagePtr(Language eLang, const char* sourceName, const Entity *pEnt, const MissionDef *def, U32 iNemesisID, char** ppchResult, Message *pMessage);

// Returns whether or not the cooldown on the mission should be updated when dropping the mission
bool missiondef_DropMissionShouldUpdateCooldown(MissionDef* pDef);

// Indicates if the mission info has any missions with the given state and tag
bool mission_HasMissionInStateByTag(MissionInfo* pMissionInfo, MissionTag eTag, MissionState eState);

// Indicates if the mission has the given tag
bool missiondef_HasTag(SA_PARAM_OP_VALID MissionDef *pMissionDef, MissionTag eTag);

// Get MissionUITypeData associated with the MissionUIType
MissionUITypeData* mission_GetMissionUITypeData(MissionUIType eType);

// Returns true if the current mission state is succeeded
bool mission_StateSucceeded(MissionInfo *pInfo, const char *pcMissionName);

MissionWarpCostDef* missiondef_GetWarpCostDef(MissionDef* pMissionDef);
bool mission_CanPlayerUseMissionWarp(Entity* pEnt, Mission* pMission);

// Returns the display name of an objective map specified by the mission def
const char *mission_GetMapDisplayNameFromMissionDef(MissionDef *pMissionDef);

//Calculates the reward tier for open missions
S32 openmission_GetRewardTierForScoreEntry(OpenMissionScoreEntry ***peaSortedScores, S32 index, S32 iNumPlayers);

#ifdef GAMECLIENT
//Client only function to update the notification time of missions with an objective on the current map
void mission_gclUpdateCurrentMapMissions(Entity* pEnt);
#endif

void TutorialScreenRegionsLoad(void);

#define ALL_MISSIONS_INDEX "AllMissionsIndex"

// Pooled versions of Expression var names, for performance
extern const char *g_MissionVarName;
extern const char *g_MissionDefVarName;
extern const char *g_PlayerVarName;
extern const char *g_EncounterVarName;
extern const char *g_Encounter2VarName;
extern const char *g_EncounterTemplateVarName;

void mission_GetAudioAssets(const char **ppcType, const char ***peaStrings, U32 *puiNumData, U32 *puiNumDataWithAudio);

