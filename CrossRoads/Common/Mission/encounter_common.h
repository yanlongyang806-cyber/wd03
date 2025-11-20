/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once
GCC_SYSTEM

#include "Encounter_Enums.h"
#include "entEnums.h"
#include "Message.h"
#include "MultiVal.h"
#include "ReferenceSystem.h"
#include "WorldLibEnums.h"

typedef struct AIJobDesc AIJobDesc;
typedef struct AICombatRolesDef AICombatRolesDef;
typedef struct AIConfig AIConfig;
typedef struct CritterDef CritterDef;
typedef struct CritterFaction CritterFaction;
typedef struct CritterGroup CritterGroup;
typedef struct Entity Entity;
typedef struct Expression Expression;
typedef struct FSM FSM;
typedef struct GameEvent GameEvent;
typedef struct PetContactList PetContactList;
typedef struct RewardTable RewardTable;
typedef struct WorldActorProperties WorldActorProperties;
typedef struct WorldEncounterProperties WorldEncounterProperties;
typedef struct WorldInteractionProperties WorldInteractionProperties;
typedef struct WorldVariableDef WorldVariableDef;
typedef struct WorldVariable WorldVariable;
typedef struct ZoneMapInfo ZoneMapInfo;
typedef struct EncounterTemplate EncounterTemplate;
typedef struct WorldInteractionPropertyEntry WorldInteractionPropertyEntry;
typedef struct ExprFuncTable  ExprFuncTable;

// Defines for the magic designer values for critter strength
#define HENCHMAN_WEAK        0.2
#define HENCHMAN_NORMAL      0.25
#define HENCHMAN_TOUGH       0.33
#define VILLAIN_WEAK         0.5
#define VILLAIN_NORMAL       0.66
#define VILLAIN_TOUGH        0.75
#define MASTERVILLAIN_WEAK   0.75
#define MASTERVILLAIN_NORMAL 1.08
#define MASTERVILLAIN_TOUGH  1.38
#define SUPERVILLAIN_WEAK    1.0
#define SUPERVILLAIN_NORMAL  1.5
#define SUPERVILLAIN_TOUGH   2.0
#define LEGENDARY_VILLAIN    5.0
#define COSMIC_VILLAIN      10.0

// define always range
#define WORLD_ENCOUNTER_RADIUS_TYPE_ALWAYS_DISTANCE 1000000.0f

AUTO_ENUM;
typedef enum EncounterSharedCritterGroupSource
{
	EncounterSharedCritterGroupSource_Specified,
	EncounterSharedCritterGroupSource_MapVariable, 
	EncounterSharedCritterGroupSource_FromParent, 
} EncounterSharedCritterGroupSource;

extern StaticDefineInt EncounterSharedCritterGroupSourceEnum[];

AUTO_ENUM;
typedef enum EncounterCritterOverrideType
{
	EncounterCritterOverrideType_FromCritter, 
	EncounterCritterOverrideType_Specified, 
} EncounterCritterOverrideType;
extern StaticDefineInt EncounterCritterOverrideTypeEnum[];

AUTO_ENUM;
typedef enum EncounterSpawnAnimType
{
	EncounterSpawnAnimType_FromCritter, 
	EncounterSpawnAnimType_Specified, 
	EncounterSpawnAnimType_FromCritterAlternate,
} EncounterSpawnAnimType;
extern StaticDefineInt EncounterSpawnAnimTypeEnum[];


AUTO_ENUM;
typedef enum EncounterTemplateOverrideType
{
	EncounterTemplateOverrideType_FromTemplate,
	EncounterTemplateOverrideType_Specified, 
} EncounterTemplateOverrideType;

extern StaticDefineInt EncounterTemplateOverrideTypeEnum[];

AUTO_ENUM;
typedef enum EncounterLevelType
{
	EncounterLevelType_MapLevel,
	EncounterLevelType_Specified, 
	EncounterLevelType_PlayerLevel,
	EncounterLevelType_MapVariable,
} EncounterLevelType;

extern StaticDefineInt EncounterLevelTypeEnum[];

AUTO_ENUM;
typedef enum EncounterLevelClampType
{
	EncounterLevelClampType_Specified, 
	EncounterLevelClampType_MapLevel,
	EncounterLevelClampType_MapVariable,
} EncounterLevelClampType;

extern StaticDefineInt EncounterLevelClampTypeEnum[];

AUTO_ENUM;
typedef enum EncounterDifficultyType
{
	EncounterDifficultyType_MapDifficulty,
	EncounterDifficultyType_Specified, 
	EncounterDifficultyType_MapVariable,
} EncounterDifficultyType;

extern StaticDefineInt EncounterDifficultyTypeEnum[];

AUTO_ENUM;
typedef enum ActorCritterType
{
	ActorCritterType_FromTemplate, 
	ActorCritterType_CritterGroup, 
	ActorCritterType_CritterDef, 
	ActorCritterType_MapVariableDef, 
	ActorCritterType_MapVariableGroup, 
	ActorCritterType_PetContactList,
	ActorCritterType_Nemesis,
	ActorCritterType_NemesisMinion,
	ActorCritterType_NemesisNormal,
	ActorCritterType_NemesisMinionNormal,
	ActorCritterType_NemesisForLeader,
	ActorCritterType_NemesisMinionForLeader,
	ActorCritterType_NemesisTeam,
	ActorCritterType_NemesisMinionTeam,
} ActorCritterType;

extern StaticDefineInt ActorCritterTypeEnum[];

AUTO_ENUM;
typedef enum TeamSizeFlags
{
	TeamSizeFlags_1   = (1 << 0),
	TeamSizeFlags_2   = (1 << 1),
	TeamSizeFlags_3	  = (1 << 2),
	TeamSizeFlags_4   = (1 << 3),
	TeamSizeFlags_5   = (1 << 4),
} TeamSizeFlags;

extern StaticDefineInt TeamSizeFlagsEnum[];


// All information needed for the AI job system
AUTO_STRUCT;
typedef struct AIJobDesc
{
	// File this came from for blaming
	const char* filename;		NO_AST

		char* jobName;				AST(REQUIRED)
		char* fsmName;				AST(REQUIRED)
		Expression* jobRequires;	AST(NAME("JobRequiresBlock"), REDUNDANT_STRUCT("JobRequires", parse_Expression_StructParam), LATEBIND)
		Expression* jobRating;		AST(NAME("JobRatingBlock"), REDUNDANT_STRUCT("JobRating", parse_Expression_StructParam), LATEBIND)

		F32 priority;

	AIJobDesc** subJobDescs;
}AIJobDesc;
extern ParseTable parse_AIJobDesc[];
#define TYPE_parse_AIJobDesc AIJobDesc

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct EncounterLevelProperties
{
	// How the level of the encounter is determined
	EncounterLevelType eLevelType;						AST( NAME("LevelType") )
	int iSpecifiedMin;									AST( NAME("SpecifiedLevelMin") )
	int iSpecifiedMax;									AST( NAME("SpecifiedLevelMax") )
	int iLevelOffsetMin;								AST( NAME("LevelOffsetMin") )
	int iLevelOffsetMax;								AST( NAME("LevelOffsetMax") )
	char *pcMapVariable;								AST( NAME("MapVariable") )

	// This is the level range to clamp at
	// Zero on specified min/max means don't clamp this value
	EncounterLevelClampType eClampType;					AST( NAME("ClampType") )
	int iClampSpecifiedMin;								AST( NAME("MinLevel") )
	int iClampSpecifiedMax;								AST( NAME("MaxLevel") )
	int iClampOffsetMin;								AST( NAME("ClampOffsetMin") )
	int iClampOffsetMax;								AST( NAME("ClampOffsetMax") )
	char *pcClampMapVariable;							AST( NAME("ClampMapVariable") )
	bool bOverrideParentValues;							AST( NAME("OverrideParentValues") )
} EncounterLevelProperties;

extern ParseTable parse_EncounterLevelProperties[];
#define TYPE_parse_EncounterLevelProperties EncounterLevelProperties

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct EncounterDifficultyProperties
{
	// How the level of the encounter is determined
	EncounterDifficultyType eDifficultyType;			AST( NAME("DifficultyType") )
	EncounterDifficulty eSpecifiedDifficulty;			AST( NAME("SpecifiedDifficulty") )
	char *pcMapVariable;								AST( NAME("MapVariable") )
} EncounterDifficultyProperties;

extern ParseTable parse_EncounterDifficultyProperties[];
#define TYPE_parse_EncounterDifficultyProperties EncounterDifficultyProperties

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct EncounterRewardProperties
{
	WorldEncounterRewardType eRewardType;				AST( NAME("RewardType") )
	REF_TO(RewardTable) hRewardTable;					AST( NAME("RewardTable") )
	WorldEncounterRewardLevelType eRewardLevelType;		AST( NAME("RewardLevelType") )
	S32 iRewardLevel;									AST( NAME("RewardLevel") )

	bool bOverrideParentValues;							AST( NAME("OverrideParentValues") )
} EncounterRewardProperties;

extern ParseTable parse_EncounterRewardProperties[];
#define TYPE_parse_EncounterRewardProperties EncounterRewardProperties

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct EncounterSpawnProperties
{
	// The spawn animation
	EncounterSpawnAnimType eSpawnAnimType;				AST( NAME("SpawnAnimType") )
	char *pcSpawnAnim;									AST( NAME("SpawnAnim") )
	F32 fSpawnLockdownTime;								AST( NAME("SpawnLockdownTime") )

	// Dynamic Spawning
	WorldEncounterDynamicSpawnType eDynamicSpawnType;	AST( NAME("DynamicSpawnType") )

	// Special flag for Champions Nemesis
	bool bIsAmbush;										AST( NAME("IsAmbush") )
	bool bOverrideParentValues;							AST( NAME("OverrideParentValues") )
} EncounterSpawnProperties;

extern ParseTable parse_EncounterSpawnProperties[];
#define TYPE_parse_EncounterSpawnProperties EncounterSpawnProperties

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct EncounterWaveProperties
{
	// The wave continues as long as this condition is true
	Expression *pWaveCond;								AST( NAME("WaveCondition") LATEBIND )

	// Wave interval time
	// Interval is only provided if type is "Specified"
	WorldEncounterWaveTimerType eWaveIntervalType;		AST( NAME("WaveIntervalType") )
	F32 fWaveInterval;									AST( NAME("WaveInterval") )

	// Wave delay time
	// Min and max only provided if type is "Specified"
	WorldEncounterWaveDelayTimerType eWaveDelayType;	AST( NAME("WaveDelayType") )
	F32 fWaveDelayMin;									AST( NAME("WaveDelayMin") )
	F32 fWaveDelayMax;									AST( NAME("WaveDelayMax") )
	bool bOverrideParentValues;							AST( NAME("OverrideParentValues") )
} EncounterWaveProperties;

extern ParseTable parse_EncounterWaveProperties[];
#define TYPE_parse_EncounterWaveProperties EncounterWaveProperties

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct EncounterActorSharedProperties
{
	// Default critter group for actors
	EncounterSharedCritterGroupSource eCritterGroupType;		AST( NAME("CritterGroupType") )
	REF_TO(CritterGroup) hCritterGroup;					AST( NAME("CritterGroup") )
	char *pcCritterGroupMapVar;							AST( NAME("CritterGroupMapVar") )

	// Default faction for actors
	EncounterCritterOverrideType eFactionType;			AST( NAME("CritterFactionType") )
	REF_TO(CritterFaction) hFaction;					AST( NAME("CritterFaction") )

	// Actors in this encounter will use a non-default entity send distance
	F32 fOverrideSendDistance;							AST( NAME("OverrideSendDistance") )

	// Default gang for actors (exposed based eFactionType)
	int iGangID;										AST( NAME("Gang") )
	bool bOverrideParentValues;							AST( NAME("OverrideParentValues") )
} EncounterActorSharedProperties;

extern ParseTable parse_EncounterActorSharedProperties[];
#define TYPE_parse_EncounterActorSharedProperties EncounterActorSharedProperties

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct EncounterAIProperties
{
	// optional combat role
	char *pchCombatRolesDef;							AST( NAME("CombatRoles") RESOURCEDICT(AICombatRolesDef) POOL_STRING  )
		
	// Default FSM for actors
	EncounterCritterOverrideType eFSMType;				AST( NAME("FSMType") )
	REF_TO(FSM) hFSM;									AST( NAME("FSM") )

	// Default FSM variables for actors
	WorldVariableDef **eaVariableDefs;					AST( NAME("VariableDef") )
	bool bOverrideParentValues;							AST( NAME("OverrideParentValues") )
} EncounterAIProperties;

extern ParseTable parse_EncounterAIProperties[];
#define TYPE_parse_EncounterAIProperties EncounterAIProperties

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct EncounterPointProperties
{
	const char *pcName;									AST( NAME("Name") )
} EncounterPointProperties;

extern ParseTable parse_EncounterPointProperties[];
#define TYPE_parse_EncounterPointProperties EncounterPointProperties

// Actor will spawn at given difficulty 
AUTO_STRUCT;
typedef struct EncounterActorSpawnProperties
{
	TeamSizeFlags eSpawnAtTeamSize;						AST( STRUCTPARAM NAME("SpawnAtTeamSize") FLAGS )
	EncounterDifficulty eSpawnAtDifficulty;				AST( KEY )
} EncounterActorSpawnProperties;
extern ParseTable parse_EncounterActorSpawnProperties[];
#define TYPE_parse_EncounterActorSpawnProperties EncounterActorSpawnProperties

AUTO_STRUCT;
typedef struct EncounterActorNameProperties
{
	// Display Name for Actor
	EncounterCritterOverrideType eCritterGroupDisplayNameType;		AST( NAME("CritterGroupDisplayNameType") )
	DisplayMessage critterGroupDisplayNameMsg;						AST( NAME("CritterGroupDisplayName")  STRUCT(parse_DisplayMessage) )
	EncounterCritterOverrideType eDisplayNameType;		AST( NAME("DisplayNameType") )
	DisplayMessage displayNameMsg;						AST( NAME("DisplayName")  STRUCT(parse_DisplayMessage) )
	EncounterCritterOverrideType eDisplaySubNameType;	AST( NAME("DisplaySubNameType") )
	DisplayMessage displaySubNameMsg;					AST( NAME("DisplaySubName") STRUCT(parse_DisplayMessage) )

	// Field for adding comments only viewable when editing this actor.  These comments have no functionality
	// and are used soley for helping the designer describe the actor to other designers
	const char* pchComments;							AST( NAME("Comments", "Comments:") SERVER_ONLY)

} EncounterActorNameProperties;

extern ParseTable parse_EncounterActorNameProperties[];
#define TYPE_parse_EncounterActorNameProperties EncounterActorNameProperties

AUTO_STRUCT;
typedef struct EncounterActorCritterProperties
{
	// The critter type
	ActorCritterType eCritterType;						AST( NAME("CritterType") )
	REF_TO(CritterDef) hCritterDef;						AST( NAME("CritterDef") )
	REF_TO(CritterGroup) hCritterGroup;					AST( NAME("CritterGroup") )
	const char *pcRank;									AST( NAME("CritterRank") POOL_STRING )
	const char *pcSubRank;								AST( NAME("CritterSubRank") POOL_STRING )
	// Level offset relative to encounter level
	int iLevelOffset;									AST( NAME("LevelOffset") )
	char *pcCritterMapVariable;							AST( NAME("CritterMapVariable") )
	// Use any member of the team to spawn the nemesis if the leader doesn't have one
	bool bNemesisLeaderTeam;							AST( NAME("NemesisLeaderTeam") )
	// Team index for nemesis to use
	S32 iNemesisTeamIndex;								AST( NAME("NemesisTeamIndex") )
} EncounterActorCritterProperties;

extern ParseTable parse_EncounterActorCritterProperties[];
#define TYPE_parse_EncounterActorCritterProperties EncounterActorCritterProperties

AUTO_STRUCT;
typedef struct EncounterActorFactionProperties
{
	// The critter faction
	EncounterTemplateOverrideType eFactionType;			AST( NAME("CritterFactionType") )
	REF_TO(CritterFaction) hFaction;					AST( NAME("CritterFaction") )
	int iGangID;										AST( NAME("CritterGang") )
} EncounterActorFactionProperties;

extern ParseTable parse_EncounterActorFactionProperties[];
#define TYPE_parse_EncounterActorFactionProperties EncounterActorFactionProperties

AUTO_STRUCT;
typedef struct EncounterActorSpawnInfoProperties
{
	// Spawn Animation
	EncounterTemplateOverrideType eSpawnAnimType;		AST( NAME("SpawnAnimType") )
	char *pcSpawnAnim;									AST( NAME("SpawnAnim") )
	F32 fSpawnLockdownTime;								AST( NAME("SpawnLockdownTime") )
} EncounterActorSpawnInfoProperties;

extern ParseTable parse_EncounterActorSpawnInfoProperties[];
#define TYPE_parse_EncounterActorSpawnInfoProperties EncounterActorSpawnInfoProperties

AUTO_STRUCT;
typedef struct EncounterActorMiscProperties
{
	// Whether the actor is a combatant or not
	bool bIsNonCombatant;								AST( NAME("IsNonCombatant") )
	REF_TO(PetContactList) hPetContactList;				AST( NAME("PetContactList") )
	const char *pcCombatRole;	 						AST( NAME("CombatRole") POOL_STRING )
} EncounterActorMiscProperties;

extern ParseTable parse_EncounterActorMiscProperties[];
#define TYPE_parse_EncounterActorMiscProperties EncounterActorMiscProperties

AUTO_STRUCT;
typedef struct EncounterActorFSMProperties
{
	// FSM 
	EncounterTemplateOverrideType eFSMType;				AST( NAME("FSMType") )
	REF_TO(FSM) hFSM;									AST( NAME("FSM") )
} EncounterActorFSMProperties;

extern ParseTable parse_EncounterActorFSMProperties[];
#define TYPE_parse_EncounterActorFSMProperties EncounterActorFSMProperties

AUTO_STRUCT;
typedef struct EncounterActorInteractionProperties
{
	// Interaction properties (if any)
	EncounterCritterOverrideType eInteractionType;		AST( NAME("InteractionType") )
} EncounterActorInteractionProperties;

extern ParseTable parse_EncounterActorInteractionProperties[];
#define TYPE_parse_EncounterActorInteractionProperties EncounterActorInteractionProperties

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_STRIP_UNDERSCORES;
typedef struct EncounterActorProperties
{
	const char *pcName;									AST( NAME("Name") POOL_STRING )

	bool bOverrideCritterGroupDisplayName;
	bool bOverrideDisplayName;
	bool bOverrideDisplaySubName;
	EncounterActorNameProperties nameProps;	AST(EMBEDDED_FLAT)

	bool bOverrideSpawnConditions;
	// What team sizes the actor spawns at
	EncounterActorSpawnProperties **eaSpawnProperties;	AST( NAME(SpawnAtTeamSize) )
	// What team sizes the actor has a boss bar for
	EncounterActorSpawnProperties **eaBossSpawnProperties;	AST( NAME(BossAtTeamSize) )

	bool bOverrideCritterType;
	EncounterActorCritterProperties critterProps;	AST(EMBEDDED_FLAT)

	bool bOverrideFaction;
	EncounterActorFactionProperties factionProps;	AST(EMBEDDED_FLAT)

	bool bOverrideCritterSpawnInfo;
	EncounterActorSpawnInfoProperties spawnInfoProps;	AST(EMBEDDED_FLAT)

	bool bOverrideMisc;
	EncounterActorMiscProperties miscProps;	AST(EMBEDDED_FLAT)

	bool bOverrideFSMInfo;
	EncounterActorFSMProperties fsmProps;	AST(EMBEDDED_FLAT)
	// FSM variables
	WorldVariableDef **eaVariableDefs;					AST( NAME("VariableDef") )

	bool bOverrideInteractionInfo;						AST(NO_TEXT_SAVE)
	//DEPRECATED
	EncounterCritterOverrideType eInteractionType;		AST( NAME("InteractionType") )
	WorldInteractionProperties *pInteractionProperties;	AST( NAME("InteractionProperties") )

} EncounterActorProperties;

extern ParseTable parse_EncounterActorProperties[];
#define TYPE_parse_EncounterActorProperties EncounterActorProperties

AUTO_STRUCT;
typedef struct EncounterTemplate
{
	// These lines up to "pcFilename" must match the WorldEncounterTemplateHeader struct in "wlGroupPropertyStructs.h"
	const char *pcName;										AST( STRUCTPARAM KEY POOL_STRING )
	const char *pcScope;									AST( POOL_STRING SERVER_ONLY )
	const char *pcFilename;									AST( CURRENTFILE )

	REF_TO(EncounterTemplate) hParent;						AST( NAME("ParentEncounter"))

	// Properties for the encounter
	EncounterLevelProperties *pLevelProperties;				AST( NAME("LevelProperties") )
	EncounterSpawnProperties *pSpawnProperties;				AST( NAME("SpawnProperties") )
	EncounterDifficultyProperties *pDifficultyProperties;	AST( NAME("DifficultyProperties") )
	EncounterWaveProperties *pWaveProperties;				AST( NAME("WaveProperties") )
	EncounterActorSharedProperties *pActorSharedProperties; AST( NAME("ActorSharedProperties") )
	EncounterAIProperties *pSharedAIProperties;				AST( NAME("SharedAIProperties") )
	EncounterRewardProperties *pRewardProperties;			AST( NAME("RewardProperties") )
	AIJobDesc **eaJobs;										AST( NAME("JobProperties") )
	/*
		*** READ THIS IF YOU'RE ADDING MORE PROPERTY GROUP STRUCTS ***
		
		Please make sure your new properties support encounter template inheritance.
	 
		Each of the sub-structs above has a "bOverrideParentValues" member which is referenced in several
		 places and is what controls whether or not a child overrides its parent's values. 
		
		In general, make sure you've touched each of these places:

		* Your accessor functions need to check bOverrideParentValues in the same way all the existing ones do.
			(See encounterTemplate_GetLevelProperties)
		* Your ETRefresh<Whatever> function needs a few lines at the top that manage the "Override" checkbox. You'll need to create your own 
		   ETOverrideEncounter<Whatever>PropsToggled function (and associated checkbox control in your 
		   ET<Whatever>Group struct) and hook it up here.
			(See ETRefreshLevel and ETOverrideEncounterLevelPropsToggled)
		* ETSetWidgetStatesFromInheritance is what disables controls that aren't overridden.
		* ETCopyInheritableFields is what copies inherited data when removing the parent ref.
	*/

	// Actors for the encounter
	EncounterActorProperties **eaActors;					AST( NAME("Actor") )

	// Points for the encounter
	EncounterPointProperties **eaPoints;					AST( NAME("Point") )

	// Game Event data is stored here when expressions generate
	GameEvent **eaTrackedEvents;							AST(NO_TEXT_SAVE SERVER_ONLY LATEBIND)
	GameEvent **eaTrackedEventsSinceSpawn;					AST(NO_TEXT_SAVE SERVER_ONLY LATEBIND)
	GameEvent **eaTrackedEventsSinceComplete;				AST(NO_TEXT_SAVE SERVER_ONLY LATEBIND)

	// Set to true if this encounter was created using the "clone" button in the world editor.
	// This field is used to filter out "One-Off" encounters from the encounter template drop down menus
	bool bOneOff;

} EncounterTemplate;

extern ParseTable parse_EncounterTemplate[];
#define TYPE_parse_EncounterTemplate EncounterTemplate

// Debugging beacon used to show the state of an encounter in the world
AUTO_STRUCT;
typedef struct EncounterDebugBeacon
{
	Vec3 vEncPos;										AST( NAME("encPos") )
	EncounterState eCurrState;
} EncounterDebugBeacon;

extern ParseTable parse_EncounterDebugBeacon[];
#define TYPE_parse_EncounterDebugBeacon EncounterDebugBeacon

// Debugging information for everything encounter related
AUTO_STRUCT;
typedef struct EncounterDebug
{
	EncounterDebugBeacon **eaEncBeacons;
	U32 uLastBeaconUpdate; NO_AST
} EncounterDebug;

extern ParseTable parse_EncounterDebug[];
#define TYPE_parse_EncounterDebug EncounterDebug

extern DictionaryHandle g_hEncounterTemplateDict;

typedef void (*EncounterTemplateChangeFunc)(EncounterTemplate *pTemplate);

// Struct for storing data defined encounter difficulties
AUTO_STRUCT;
typedef struct ExtraEncounterDifficulties
{
	char **ppchDifficulties;					AST(NAME(Difficulty))
} ExtraEncounterDifficulties;
extern ParseTable parse_ExtraEncounterDifficulties[];
#define TYPE_parse_ExtraEncounterDifficulties ExtraEncounterDifficulties



// Loads the dictionary
void encounterTemplate_Load(void);

// Register callback when a dictionary element is loaded or reloaded
void encounterTemplate_SetPostProcessCallback(EncounterTemplateChangeFunc changeFunc);

// Get the array of actors.
void encounterTemplate_FillActorEarray(EncounterTemplate* pTemplate, EncounterActorProperties*** eaActors);

// Get the array of AI Jobs
void encounterTemplate_FillAIJobEArray(EncounterTemplate *pTemplate, AIJobDesc*** eaJobsToFill);
void encounterTemplate_FillActorInteractionEarray(EncounterTemplate* pTemplate, EncounterActorProperties* pActor, WorldInteractionPropertyEntry*** eaInteractionsToFill);

// Get level properties.
EncounterLevelProperties* encounterTemplate_GetLevelProperties(EncounterTemplate* pTemplate);

// Get difficulty properties.
EncounterDifficultyProperties* encounterTemplate_GetDifficultyProperties(EncounterTemplate* pTemplate);

// Get reward properties.
EncounterRewardProperties* encounterTemplate_GetRewardProperties(EncounterTemplate *pTemplate);

// Get AI properties.
EncounterAIProperties* encounterTemplate_GetAIProperties(EncounterTemplate* pTemplate);

// Get Spawn properties.
EncounterSpawnProperties* encounterTemplate_GetSpawnProperties(EncounterTemplate* pTemplate);

// Get Wave properties
EncounterWaveProperties* encounterTemplate_GetWaveProperties(EncounterTemplate* pTemplate);

// Get Actor Shared properties
EncounterActorSharedProperties* encounterTemplate_GetActorSharedProperties(EncounterTemplate* pTemplate);

// Get an actor by name
EncounterActorProperties *encounterTemplate_GetActorByName(EncounterTemplate *pTemplate, const char *pcName);

// Get an actor by index
EncounterActorProperties *encounterTemplate_GetActorByIndex(EncounterTemplate *pTemplate, int iActorIndex);

// Gets an actor either by name or index depending on the world encounter and actor
EncounterActorProperties* encounterTemplate_GetActorFromWorldActor(EncounterTemplate *pTemplate, WorldEncounterProperties *pWorldEncounter, WorldActorProperties *pWorldActor);

// Get the encounter level
void encounterTemplate_GetLevelRange(EncounterTemplate *pTemplate, Entity *pEnt, int iPartitionIdx, int *piMin, int *piMax, Vec3 v3EncounterPos);

// Get the encounter difficulty
EncounterDifficulty encounterTemplate_GetDifficulty(EncounterTemplate *pTemplate, int iPartitionIdx);

//Get tracked events
void encounterTemplate_FillTrackedEventsEarrays(EncounterTemplate* pTemplate, GameEvent*** peaTrackedEvents, GameEvent*** peaTrackedEventsSinceSpawn, GameEvent*** peaTrackedEventsSinceComplete);

// Gets the actor CritterGroup and ranks following override rules
EncounterSharedCritterGroupSource encounterTemplate_GetCritterGroupSource(EncounterTemplate *pTemplate);
const char* encounterTemplate_GetCritterGroupSourceMapVarName(EncounterTemplate *pTemplate);
CritterGroup *encounterTemplate_GetCritterGroup(EncounterTemplate *pTemplate, int iPartitionIdx);
const char *encounterTemplate_GetCritterGroupMapVarName(EncounterTemplate *pTemplate);
CritterGroup *encounterTemplate_GetActorCritterGroup(EncounterTemplate *pTemplate, EncounterActorProperties *pActor, int iPartitionIdx, ActorCritterType eType);
const char *encounterTemplate_GetActorRank(EncounterTemplate *pTemplate, EncounterActorProperties *pActor);
const char *encounterTemplate_GetActorSubRank(EncounterTemplate *pTemplate, EncounterActorProperties *pActor);
ActorCritterType encounterTemplate_GetActorCritterType(EncounterTemplate *pTemplate, EncounterActorProperties *pActor);
const char* encounterTemplate_GetActorCritterTypeMapVarName(EncounterTemplate *pTemplate,EncounterActorProperties* pActor);
WorldInteractionProperties* encounterTemplate_GetActorInteractionProps(EncounterTemplate *pTemplate, EncounterActorProperties *pActor);
bool encounterTemplate_GetActorIsCombatant(EncounterTemplate *pTemplate, EncounterActorProperties *pActor);
// Gets the actor CritterDef following override rules
CritterDef *encounterTemplate_GetActorCritterDef(EncounterTemplate *pTemplate, EncounterActorProperties *pActor, int iPartitionIdx);
bool encounterTemplate_IsActorCritterDefKnown(EncounterTemplate *pTemplate, EncounterActorProperties *pActor);

// Gets the actor CritterFaction following override rules
EncounterCritterOverrideType encounterTemplate_GetFactionSource(EncounterTemplate* pTemplate);
CritterFaction* encounterTemplate_GetFaction(EncounterTemplate* pTemplate);
CritterFaction *encounterTemplate_GetActorFaction(EncounterTemplate *pTemplate, EncounterActorProperties *pActor, int iPartitionIdx);
int encounterTemplate_GetGangID(EncounterTemplate* pTemplate);
int encounterTemplate_GetActorGangID(EncounterTemplate *pTemplate, EncounterActorProperties *pActor, int iPartitionIdx);
bool encounterTemplate_IsActorFactionKnown(EncounterTemplate *pTemplate, EncounterActorProperties *pActor);
bool encounterTemplate_GetActorFactionName(EncounterTemplate *pTemplate, EncounterActorProperties *pActor, int iPartitionIdx, char **estrName);
EncounterTemplateOverrideType encounterTemplate_GetActorFactionSource(EncounterTemplate *pTemplate, EncounterActorProperties *pActor);


// Gets the encounter/actor combat role information
AICombatRolesDef* encounterTemplate_GetCombatRolesDef(EncounterTemplate *pTemplate);
const char* encounterTemplate_GetActorCombatRole(EncounterTemplate *pTemplate, EncounterActorProperties *pActor);

// Gets the  FSM following override rules
FSM *encounterTemplate_GetEncounterFSM(EncounterTemplate *pTemplate);
FSM *encounterTemplate_GetActorFSM(EncounterTemplate *pTemplate, EncounterActorProperties *pActor, int iPartitionIdx);
bool encounterTemplate_IsActorFSMKnown(EncounterTemplate *pTemplate, EncounterActorProperties *pActor);
bool encounterTemplate_GetActorFSMName(EncounterTemplate *pTemplate, EncounterActorProperties *pActor, int iPartitionIdx, char **estrName);

// Adds a world variable to the specified list if it's not already on either list
void encounterTemplate_AddVarIfNotPresent(WorldVariableDef ***peaVarDefs, WorldVariable ***peaVars, WorldVariable *pVar);
void encounterTemplate_AddVarDefIfNotPresent(WorldVariableDef ***peaVarDefs, WorldVariable ***peaVars, WorldVariableDef *pVarDef);

// Gets the FSM vars following override rules
void encounterTemplate_GetEncounterFSMVarDefs(EncounterTemplate *pTemplate, WorldVariableDef ***peaVarDefs, WorldVariable ***peaVars);
void encounterTemplate_GetEncounterGroupFSMVars(EncounterTemplate *pTemplate, int iPartitionIdx, WorldVariableDef ***peaVarDefs, WorldVariable ***peaVars);
void encounterTemplate_GetActorFSMVarDefs(EncounterTemplate *pTemplate, EncounterActorProperties *pActor, WorldVariableDef ***peaVarDefs, WorldVariable ***peaVars);
void encounterTemplate_GetActorCritterFSMVars(EncounterTemplate *pTemplate, EncounterActorProperties *pActor, int iPartitionIdx, WorldVariableDef ***peaVarDefs, WorldVariable ***peaVars);
void encounter_GetWorldActorFSMVarDefs(WorldActorProperties *pActor, WorldVariableDef ***peaVarDefs, WorldVariable ***peaVars);
EncounterTemplateOverrideType encounterTemplate_GetActorFSMSource(EncounterTemplate *pTemplate, EncounterActorProperties *pActor);

// Gets level of an actor, modified by the per-actor modifier if applicable
int encounterTemplate_GetActorLevel(EncounterTemplate *pTemplate, EncounterActorProperties *pActor, int iEncounterLevel);
int encounterTemplate_GetActorLevelOffset(EncounterTemplate *pTemplate, EncounterActorProperties *pActor);
// Gets the actor spawn animation following override rules
const char *encounterTemplate_GetActorSpawnAnim(EncounterTemplate *pTemplate, EncounterActorProperties *pActor, int iPartitionIdx, F32* pfAnimTime);
bool encounterTemplate_IsActorSpawnAnimKnown(EncounterTemplate *pTemplate, EncounterActorProperties *pActor);
EncounterTemplateOverrideType encounterTemplate_GetActorSpawnAnimSource(EncounterTemplate *pTemplate, EncounterActorProperties *pActor);

// Gets the actor display message
Message *encounterTemplate_GetActorCritterGroupDisplayMessageInternal(EncounterTemplate *pTemplate, EncounterActorProperties *pActor, int iPartitionIdx, bool bOnlyUseOverride);
#define encounterTemplate_GetActorCritterGroupDisplayMessage(pTemplate, pActor, iPartitionIdx) encounterTemplate_GetActorCritterGroupDisplayMessageInternal(pTemplate, pActor, iPartitionIdx, false)
#define encounterTemplate_GetActorOverrideCritterGroupDisplayMessage(pTemplate, pActor, iPartitionIdx) encounterTemplate_GetActorCritterGroupDisplayMessageInternal(pTemplate, pActor, iPartitionIdx, true)
EncounterCritterOverrideType encounterTemplate_GetActorCritterGroupDisplayMessageSource(EncounterTemplate *pTemplate, EncounterActorProperties *pActor);

Message *encounterTemplate_GetActorDisplayMessageInternal(EncounterTemplate *pTemplate, EncounterActorProperties *pActor, int iPartitionIdx, bool bOnlyUseOverride);
#define encounterTemplate_GetActorDisplayMessage(pTemplate, pActor, iPartitionIdx) encounterTemplate_GetActorDisplayMessageInternal(pTemplate, pActor, iPartitionIdx, false)
#define encounterTemplate_GetActorOverrideDisplayMessage(pTemplate, pActor, iPartitionIdx) encounterTemplate_GetActorDisplayMessageInternal(pTemplate, pActor, iPartitionIdx, true)
EncounterCritterOverrideType encounterTemplate_GetActorDisplayMessageSource(EncounterTemplate *pTemplate, EncounterActorProperties *pActor);

Message *encounterTemplate_GetActorDisplaySubNameMessageInternal(EncounterTemplate *pTemplate, EncounterActorProperties *pActor, int iPartitionIdx, bool bOnlyUseOverride);
#define encounterTemplate_GetActorDisplaySubNameMessage(pTemplate, pActor, iPartitionIdx) encounterTemplate_GetActorDisplaySubNameMessageInternal(pTemplate, pActor, iPartitionIdx, false)
#define encounterTemplate_GetActorOverrideDisplaySubNameMessage(pTemplate, pActor, iPartitionIdx) encounterTemplate_GetActorDisplaySubNameMessageInternal(pTemplate, pActor, iPartitionIdx, true)
EncounterCritterOverrideType encounterTemplate_GetActorDisplaySubNameMessageSource(EncounterTemplate *pTemplate, EncounterActorProperties *pActor);

// Gets the actor is enabled for the given team size
bool encounterTemplate_GetActorEnabled(EncounterTemplate *pTemplate, EncounterActorProperties *pActor, int iTeamSize, EncounterDifficulty eDifficulty);

// Returns the number of enabled actors at a specified team size
int encounterTemplate_GetNumActorsAtTeamSize(EncounterTemplate *pTemplate, int iTeamSize, EncounterDifficulty eDifficulty);

// Returns the maximum number of enabled actors at any team size
int encounterTemplate_GetMaxNumActors(EncounterTemplate *pTemplate);

// Gets the actor boss bar for the given team size
bool encounterTemplate_GetActorBossBar(EncounterTemplate *pTemplate, EncounterActorProperties *pActor, int iTeamSize, EncounterDifficulty eDifficulty);

EncounterActorSpawnProperties** encounterTemplate_GetActorBossProps(EncounterTemplate *pTemplate, EncounterActorProperties *pActor);
EncounterActorSpawnProperties** encounterTemplate_GetActorSpawnProps(EncounterTemplate *pTemplate, EncounterActorProperties *pActor);

// Gets the difficulty ratings for actors and encounters
F32 encounterTemplate_GetActorValue(EncounterTemplate *pTemplate, EncounterActorProperties *pActor, int iPartitionIdx, int iTeamSize, EncounterDifficulty eDifficulty);
F32 encounterTemplate_GetEncounterValue(EncounterTemplate *pTemplate, int iPartitionIdx, int iTeamSize, EncounterDifficulty eDifficulty);

// Message Cleanup
void encounterTemplate_FixupMessages(EncounterTemplate *pTemplate);
void encounterTemplate_Clean(EncounterTemplate *pTemplate);
void worldEncounter_FixupMessages(WorldEncounterProperties *pWorldEncounter, const char *pcFilename, const char *pcGroupName, const char *pcScope);

// Optimization of the template
void encounterTemplate_Optimize(EncounterTemplate *pTemplate);
void encounterTemplate_Deoptimize(EncounterTemplate *pTemplate);

// Gets values for parameterized values
F32 encounter_GetRadiusValue(WorldEncounterRadiusType eRadius, F32 fCustom, Vec3 vPos);
F32 encounter_GetTimerValue(WorldEncounterTimerType eTimer, F32 fCustom, const Vec3 vPos);
F32 encounter_GetWaveTimerValue(WorldEncounterWaveTimerType eTimer, F32 fCustom, const Vec3 vPos);
void encounter_GetWaveDelayTimerValue(WorldEncounterWaveDelayTimerType eTimer, F32 fCustomMin, F32 fCustomMax, const Vec3 vPos, F32 *fpMin, F32 *fpMax);

F32 encounter_GetActiveRadius(Vec3 vPos);

WorldEncounterRadiusType encounter_GetSpawnRadiusTypeFromProperties(ZoneMapInfo* pZmapInfo, WorldEncounterProperties *pProps);
F32 encounter_GetSpawnRadiusValueFromProperties(WorldEncounterProperties *pProps, Vec3 vPos);
WorldEncounterTimerType encounter_GetRespawnTimerTypeFromProperties(ZoneMapInfo* pZmapInfo, WorldEncounterProperties *pProps);
F32 encounter_GetRespawnTimerValueFromProperties(WorldEncounterProperties *pProps, Vec3 vPos);

// Gets the Gang ID in the majority among all the actors on the template
U32 encounterTemplate_GetMajorityGangID(EncounterTemplate *pTemplate, int iPartitionIdx);
// Gets the Faction in the majority among all the actors on the template
CritterFaction* encounterTemplate_GetMajorityFaction(EncounterTemplate *pTemplate, int iPartitionIdx);

// Returns the number of teammates including the player who are within a predefined maximum team distance (defined in the regionrules).
// Sets peaTeam to this array of teammates.
void encounter_getTeammatesInRange(Entity* pEntPlayer, Entity*** peaTeam);
// Finds all teammates within range of the player, and computes the average level of all the players given they are within 
// a predefined amount of levels of the highest level player found, number in range is returned in optional pCount
int encounter_getTeamLevelInRange(Entity* pEntPlayer, int *pCount, bool bUseCachedLevel);
// Determine if a template has another template as a parent at any level of inheritance.
bool encounterTemplate_TemplateExistsInInheritanceChain(EncounterTemplate* pTemplate, EncounterTemplate* pTemplateToFind);

int encounter_GetEncounterDifficultiesCount();

//Disabling Damage
bool encounter_IsDamageDisabled(int iPartitionIdx);
void encounter_SetDamageDisabled(int iPartitionIdx);
void encounter_RemoveDamageDisabled(int iPartitionIdx);

ExprFuncTable* encounter_CreateInteractExprFuncTable();

EncounterActorNameProperties* encounterTemplate_GetActorNameProperties(EncounterTemplate *pTemplate, EncounterActorProperties *pActor);
EncounterActorCritterProperties* encounterTemplate_GetActorCritterProperties(EncounterTemplate *pTemplate, EncounterActorProperties *pActor);
EncounterActorFactionProperties* encounterTemplate_GetActorFactionProperties(EncounterTemplate *pTemplate, EncounterActorProperties *pActor);
EncounterActorSpawnInfoProperties* encounterTemplate_GetActorSpawnInfoProperties(EncounterTemplate *pTemplate, EncounterActorProperties *pActor);
EncounterActorMiscProperties* encounterTemplate_GetActorMiscProperties(EncounterTemplate *pTemplate, EncounterActorProperties *pActor);
EncounterActorFSMProperties* encounterTemplate_GetActorFSMProperties(EncounterTemplate *pTemplate, EncounterActorProperties *pActor);

Message *encounter_GetActorCritterGroupDisplayMessageInternal(int iPartitionIdx, WorldEncounterProperties* pProps, WorldActorProperties *pWorldActor, bool bOnlyUseOverride);
#define encounter_GetActorCritterGroupDisplayMessage(iPartitionIdx, pProps, pWorldActor) encounter_GetActorCritterGroupDisplayMessageInternal(iPartitionIdx, pProps, pWorldActor, false)
#define encounter_GetActorOverrideCritterGroupDisplayMessage(iPartitionIdx, pProps, pWorldActor) encounter_GetActorCritterGroupDisplayMessageInternal(iPartitionIdx, pProps, pWorldActor, true)

Message *encounter_GetActorDisplayMessageInternal(int iPartitionIdx, WorldEncounterProperties* pProps, WorldActorProperties *pWorldActor, bool bOnlyUseOverride);
#define encounter_GetActorDisplayMessage(iPartitionIdx, pProps, pWorldActor) encounter_GetActorDisplayMessageInternal(iPartitionIdx, pProps, pWorldActor, false)
#define encounter_GetActorOverrideDisplayMessage(iPartitionIdx, pProps, pWorldActor) encounter_GetActorDisplayMessageInternal(iPartitionIdx, pProps, pWorldActor, true)

Message *encounter_GetActorDisplaySubNameMessageInternal(int iPartitionIdx, WorldEncounterProperties* pProps, WorldActorProperties *pWorldActor, bool bOnlyUseOverride);
#define encounter_GetActorDisplaySubNameMessage(iPartitionIdx, pProps, pWorldActor) encounter_GetActorDisplaySubNameMessageInternal(iPartitionIdx, pProps, pWorldActor, false)
#define encounter_GetActorOverrideDisplaySubNameMessage(iPartitionIdx, pProps, pWorldActor) encounter_GetActorDisplaySubNameMessageInternal(iPartitionIdx, pProps, pWorldActor, true)

void encounter_GetAudioAssets(const char **ppcType, const char ***peaStrings, U32 *puiNumData, U32 *puiNumDataWithAudio);