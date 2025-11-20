#pragma once
GCC_SYSTEM

C_DECLARATIONS_BEGIN

typedef struct DefineContext DefineContext;

// non property-based flags
typedef enum GroupFlags
{
	GRP_HAS_MATERIAL_REPLACE		= (1<<0),
	GRP_HAS_MATERIAL_SWAPS			= (1<<1),
	GRP_HAS_TEXTURE_SWAPS			= (1<<2),
	GRP_HAS_TINT					= (1<<3),
	GRP_HAS_MATERIAL_PROPERTIES		= (1<<4),
	GRP_HAS_TINT_OFFSET				= (1<<5),
} GroupFlags;

typedef enum VolumeFlags
{
	VOL_TYPE_OCCLUDER		= (1<<0),
	VOL_TYPE_SOUND			= (1<<1),
	VOL_TYPE_SKY			= (1<<2),
	VOL_TYPE_NEIGHBORHOOD	= (1<<3),
	VOL_TYPE_LANDMARK		= (1<<4),
	VOL_TYPE_POWER			= (1<<5),
	VOL_TYPE_WARP			= (1<<6),
	VOL_TYPE_INTERACTION    = (1<<7),
	VOL_TYPE_GENESIS		= (1<<8),
	VOL_TYPE_BLOB_FILTER	= (1<<9),
	VOL_TYPE_EXCLUSION		= (1<<10),

	VOL_TYPE_TINTED			= (1<<31),
} VolumeFlags;

AUTO_ENUM;
typedef enum GroupDefVolumeShape
{
	GVS_Error=0,
	GVS_Box,
	GVS_Sphere,
} GroupDefVolumeShape;

AUTO_ENUM;
typedef enum WorldMapRepType
{
	WorldMapRepType_Unspecified=0,
	WorldMapRepType_SolarSystem,
} WorldMapRepType;

AUTO_ENUM;
typedef enum mapSnapSelectionType
{
	MSNAP_Rectangle=0,
	MSNAP_Ellipse,
	MSNAP_Free,
} mapSnapSelectionType;

AUTO_ENUM;
typedef enum LogicalGroupRandomType {
	LogicalGroupRandomType_None,
	LogicalGroupRandomType_OnceOnLoad,
	LogicalGroupRandomType_Continuous,
} LogicalGroupRandomType;
extern StaticDefineInt LogicalGroupRandomTypeEnum[];

AUTO_ENUM;
typedef enum LogicalGroupSpawnAmountType {
	LogicalGroupSpawnAmountType_Number,
	LogicalGroupSpawnAmountType_Percentage,
} LogicalGroupSpawnAmountType;
extern StaticDefineInt LogicalGroupSpawnAmountTypeEnum[];

typedef enum ServerResponseStatus
{
	StatusNoReply = 0,			// Command doesn't return a reply
	StatusSuccess,				// Command succeeded fully
	StatusError,				// Command completely failed
	StatusInProgress,			// Command contains progress information
} ServerResponseStatus;

AUTO_ENUM;
typedef enum GenesisEditType
{
	GENESIS_EDIT_STREAMING,							ENAMES(Game_Mode)
	GENESIS_EDIT_EDITING,							ENAMES(Editable)
} GenesisEditType;
extern StaticDefineInt GenesisEditTypeEnum[];

AUTO_ENUM;
typedef enum GenesisViewType
{
	GENESIS_VIEW_NODES,								ENAMES(Nodes)
	GENESIS_VIEW_WHITEBOX,							ENAMES(Whitebox)
	GENESIS_VIEW_NODETAIL,							ENAMES(No_Detail)
	GENESIS_VIEW_FULL,								ENAMES(Full)
} GenesisViewType;
extern StaticDefineInt GenesisViewTypeEnum[];

AUTO_ENUM;
typedef enum GenesisExteriorShape 
{
	GENESIS_EXT_RAND=0,
	GENESIS_EXT_LINEAR,
} GenesisExteriorShape;
extern StaticDefineInt GenesisExteriorShapeEnum[];

AUTO_ENUM;
typedef enum GenesisVertDir
{
	GENESIS_PATH_RAND=0,
	GENESIS_PATH_UPHILL,
	GENESIS_PATH_DOWNHILL,
	GENESIS_PATH_FLAT,
} GenesisVertDir;
extern StaticDefineInt GenesisVertDirEnum[];

AUTO_ENUM;
typedef enum GenesisPatrolType
{
	GENESIS_PATROL_None=0,
	GENESIS_PATROL_Path,
	GENESIS_PATROL_Path_OneWay,
	GENESIS_PATROL_Challenges,
	GENESIS_PATROL_Perimeter,
	GENESIS_PATROL_OtherRoom,
	GENESIS_PATROL_OtherRoom_OneWay,
} GenesisPatrolType;
extern StaticDefineInt GenesisPatrolTypeEnum[];

AUTO_ENUM;
typedef enum GenesisSpacePatrolType
{
	GENESIS_SPACE_PATROL_None=0,
	GENESIS_SPACE_PATROL_Path,
	GENESIS_SPACE_PATROL_Path_OneWay,
	GENESIS_SPACE_PATROL_Perimeter,
	GENESIS_SPACE_PATROL_Orbit,
} GenesisSpacePatrolType;
extern StaticDefineInt GenesisSpacePatrolTypeEnum[];

AUTO_ENUM;
typedef enum WLCameraCollisionType {
	WLCCT_FullCamCollision = 0,			ENAMES("Full_Collision")
	WLCCT_ObjectFades,					ENAMES("Object_Fades")
	WLCCT_ObjectVanishes,				ENAMES("Object_Vanishes")
	WLCCT_NoCamCollision,				ENAMES("No_Collision")
} WLCameraCollisionType;
extern StaticDefineInt WLCameraCollisionTypeEnum[];

AUTO_ENUM;
typedef enum WLGameCollisionType {
	WLGCT_NotPermeable = 0,				ENAMES("Not_Permeable")
	WLGCT_TargetableOnly,				ENAMES("Targetable_Only")
	WLGCT_FullyPermeable,				ENAMES("Fully_Permeable")
} WLGameCollisionType;
extern StaticDefineInt WLGameCollisionTypeEnum[];

typedef enum WorldLibLoadFlags {
	WL_NO_LOAD_MATERIALS = 1 << 0,
	WL_NO_LOAD_DYNFX = 1 << 1,
	WL_NO_LOAD_DYNANIMATIONS = 1 << 2,
	WL_NO_LOAD_MODELS = 1 << 3,
	WL_NO_LOAD_COSTUMES = 1 << 4,
	WL_NO_LOAD_PROFILES = 1 << 5,
	WL_LOAD_DYNFX_NAMES_FOR_VERIFICATION = 1 << 6,
	WL_NO_LOAD_ZONEMAPS = 1 << 7,
	WL_NO_LOAD_DYNMISC = 1 << 8, // Misc stuff not needed by GetVRML
} WorldLibLoadFlags;

// Used for both Materials and Models/Geos
typedef enum WLUsageFlagsBitIndex {
	WL_FOR_NOTSURE_BITINDEX, // Should be first
	// The rest specifically arranged in order of who they should be accounted to
	// if it is a shared resource
	WL_FOR_WORLD_BITINDEX,
	WL_FOR_ENTITY_BITINDEX,
	WL_FOR_FX_BITINDEX,
	WL_FOR_UI_BITINDEX,
	WL_FOR_TERRAIN_BITINDEX,
	WL_FOR_UTIL_BITINDEX,
	WL_FOR_FONTS_BITINDEX,
	WL_FOR_PREVIEW_INTERNAL_BITINDEX,
	WL_FOR_MAXCOUNT,
} WLUsageFlagsBitIndex;

typedef enum WLUsageFlags {
	WL_FOR_NOTSURE	= 1 << WL_FOR_NOTSURE_BITINDEX,
	WL_FOR_UI		= 1 << WL_FOR_UI_BITINDEX,
	WL_FOR_FX		= 1 << WL_FOR_FX_BITINDEX,
	WL_FOR_ENTITY	= 1 << WL_FOR_ENTITY_BITINDEX,
	WL_FOR_WORLD	= 1 << WL_FOR_WORLD_BITINDEX,
	WL_FOR_TERRAIN	= 1 << WL_FOR_TERRAIN_BITINDEX,
	WL_FOR_UTIL		= 1 << WL_FOR_UTIL_BITINDEX,
	WL_FOR_FONTS	= 1 << WL_FOR_FONTS_BITINDEX,

	// override flags 
	WL_FOR_PREVIEW_INTERNAL = 1 << WL_FOR_PREVIEW_INTERNAL_BITINDEX,
	WL_FOR_OVERRIDE = WL_FOR_PREVIEW_INTERNAL,

	WL_FOR_MAX,
} WLUsageFlags;
#define WLUsageFlags_NumBits 9
STATIC_ASSERT(WL_FOR_MAX == (1<<(WLUsageFlags_NumBits-1))+1);

__forceinline static WLUsageFlagsBitIndex wlGetUsageFlagsBitIndex(WLUsageFlags flags)
{
	int i;
	for (i = 0; i < WL_FOR_MAXCOUNT; ++i)
	{
		if (flags & (1 << i))
			return (WLUsageFlagsBitIndex)i;
	}
	return WL_FOR_NOTSURE_BITINDEX;
}


typedef enum WorldCellState
{
	WCS_CLOSED,
	WCS_LOADING_BG,
	WCS_LOADING_FG,
	WCS_LOADED,
	WCS_OPEN,
} WorldCellState;

AUTO_ENUM;
typedef enum WorldRegionType
{
	WRT_None = -1,		//< only -1, because I don't know if it is
						//< safe to make this 0. -- MJF
	WRT_Ground,
	WRT_Space,
	WRT_SectorSpace,
	WRT_SystemMap,
	WRT_CharacterCreator,
	WRT_Indoor,
	WRT_PvP,

	// These are problematic on a number of levels. Please see the comment in RegionRules.h

	WRT_COUNT,
} WorldRegionType;

extern StaticDefineInt WorldRegionTypeEnum[];

typedef enum ZoneMapLayerMode
{
	LAYER_MODE_NOT_LOADED = 0,	// Layer hasn't been loaded yet
	LAYER_MODE_STREAMING,		// Streaming from world cell & terrain bins
	LAYER_MODE_GROUPTREE,		// Grouptree sources are loaded, terrain streaming from bins
	LAYER_MODE_TERRAIN,			// Grouptree & terrain loaded from source
	LAYER_MODE_EDITABLE,		// Grouptree & terrain loaded, layer is locked and fully editable
} ZoneMapLayerMode;

AUTO_ENUM;
typedef enum ZoneRespawnType
{
	ZoneRespawnType_Default,
	ZoneRespawnType_NearTeam,
}ZoneRespawnType;

extern StaticDefineInt ZoneRespawnTypeEnum[];

typedef enum ResourcePackType
{
	RPACK_WORLD_OBJECTS,
	RPACK_WORLD_TEXTURES,
	RPACK_DYN_OBJECTS,
	RPACK_DYN_TEXTURES,

	RPACK_TYPE_COUNT,
} ResourcePackType;

AUTO_ENUM;
typedef enum LightType
{
	WL_LIGHT_NONE, // Don't try to make a light with this light type, used to mark a dead light
	WL_LIGHT_DIRECTIONAL,
	WL_LIGHT_POINT,
	WL_LIGHT_SPOT,
	WL_LIGHT_PROJECTOR,
} LightType;

AUTO_ENUM;
typedef enum GenesisTagOrName
{
	GenesisTagOrName_RandomByTag,
	GenesisTagOrName_SpecificByName,
} GenesisTagOrName;
extern StaticDefineInt GenesisTagOrNameEnum[];

AUTO_ENUM;
typedef enum GenesisTemplateOrCustom
{
	GenesisTemplateOrCustom_Custom,
	GenesisTemplateOrCustom_Template,
} GenesisTemplateOrCustom;
extern StaticDefineInt GenesisTemplateOrCustomEnum[];

AUTO_ENUM;
typedef enum GenesisTagNameDefault
{
	GenesisTagNameDefault_UseDefault,
	GenesisTagNameDefault_RandomByTag,
	GenesisTagNameDefault_SpecificByName,
} GenesisTagNameDefault;
extern StaticDefineInt GenesisTagNameDefaultEnum[];

AUTO_ENUM;
typedef enum TerrainExclusionVersion
{
	EXCLUSION_NONE = 0,
	EXCLUSION_SIMPLE = 1,
	EXCLUSION_VOLUMES = 2
} TerrainExclusionVersion;
extern StaticDefineInt TerrainExclusionVersionEnum[];

AUTO_ENUM;
typedef enum GenesisChallengeFacing
{
	GenesisChallengeFace_Random,
	GenesisChallengeFace_Fixed,
	GenesisChallengeFace_Entrance,
	GenesisChallengeFace_Exit,
	GenesisChallengeFace_Entrance_Exit,
	GenesisChallengeFace_Center,
	GenesisChallengeFace_Challenge_Away,
	GenesisChallengeFace_Challenge_Toward,
} GenesisChallengeFacing;
extern StaticDefineInt GenesisChallengeFacingEnum[];

AUTO_ENUM;
typedef enum GenesisChallengePlacement
{
	GenesisChallengePlace_Random,
	GenesisChallengePlace_Center,
	GenesisChallengePlace_ExactCenter,
	GenesisChallengePlace_On_Wall,
	GenesisChallengePlace_Entrance,
	GenesisChallengePlace_Exit,
	GenesisChallengePlace_Entrance_Exit,
	GenesisChallengePlace_Near_Challenge,
	GenesisChallengePlace_InSpecificDoor,			EIGNORE
	GenesisChallengePlace_Prefab_Location,
} GenesisChallengePlacement;
extern StaticDefineInt GenesisChallengePlacementEnum[];

AUTO_ENUM;
typedef enum GenesisChallengeType {
	GenesisChallenge_None,			ENAMES(None)
	GenesisChallenge_Clickie,		ENAMES(Clickie)
	GenesisChallenge_Encounter,		ENAMES(Encounter1 Kill Encounter)
	GenesisChallenge_Encounter2,	ENAMES(Encounter2)
	GenesisChallenge_Destructible,	ENAMES(Destructible)
	GenesisChallenge_Contact,		ENAMES(Contact)

	GenesisChallenge_Count,			EIGNORE  //< MUST BE LAST
} GenesisChallengeType;
extern StaticDefineInt GenesisChallengeTypeEnum[];

AUTO_ENUM;
typedef enum WorldFXVolumeFilter
{
	WorldFXVolumeFilter_AllEntities,
	WorldFXVolumeFilter_LocalPlayer,
	WorldFXVolumeFilter_Camera3D,
	WorldFXVolumeFilter_Camera2D,

	WorldFXVolumeFilter_Count,
} WorldFXVolumeFilter;
extern StaticDefineInt WorldFXVolumeFilterEnum[];

AUTO_ENUM;
typedef enum WorldWindEffectType
{
	WorldWindEffectType_Override,	ENAMES(Override)
	WorldWindEffectType_Add,		ENAMES(Add)
	WorldWindEffectType_Multiply,	ENAMES(Multiply)

	WorldWindEffectType_COUNT,		EIGNORE //Must be last
} WorldWindEffectType;
extern StaticDefineInt WorldWindEffectTypeEnum[];

// Possible handling of a child group under a curve.
AUTO_ENUM;
typedef enum CurveChildType {
	// Instance this group as a rigid object, without passing a curve down.
	CURVE_CHILD_RIGID,
	// Pick a child based on optimal line-filling algorithm, without passing a curve down.
	CURVE_CHILD_OPTIMIZE,
	// Choose a random child to instance at each control point, without passing a curve down.
	CURVE_CHILD_RANDOM,
	// Pass the curve down to each subobject separately.
	CURVE_CHILD_INHERIT,
} CurveChildType;
extern StaticDefineInt CurveChildTypeEnum[];

AUTO_ENUM;
typedef enum WorldPatrolRouteType
{
	PATROL_PINGPONG,
	PATROL_CIRCLE,
	PATROL_ONEWAY,
} WorldPatrolRouteType;
extern StaticDefineInt WorldPatrolRouteTypeEnum[];

AUTO_ENUM;
typedef enum WorldSpawnPointType
{
	SPAWNPOINT_RESPAWN,
	SPAWNPOINT_GOTO,
	SPAWNPOINT_STARTSPAWN,
} WorldSpawnPointType;
extern StaticDefineInt WorldSpawnPointTypeEnum[];

AUTO_ENUM;
typedef enum WorldPowerVolumeStrength
{
	WorldPowerVolumeStrength_Harmless,
	WorldPowerVolumeStrength_Default,
	WorldPowerVolumeStrength_Deadly,

	WorldPowerVolumeStrength_Count,
} WorldPowerVolumeStrength;
extern StaticDefineInt WorldPowerVolumeStrengthEnum[];

AUTO_ENUM;
typedef enum WorldSkillType
{
	WorldSkillType_None = 0,
	WorldSkillType_Arms = (1<<0), 
	WorldSkillType_Mysticism = (1<<1), 
	WorldSkillType_Science = (1<<2), 

	WorldSkillType_Count,  EIGNORE
} WorldSkillType;
extern StaticDefineInt WorldSkillTypeEnum[];

AUTO_ENUM;
typedef enum WorldCooldownTime
{
	WorldCooldownTime_None, 
	WorldCooldownTime_Short, 
	WorldCooldownTime_Medium,
	WorldCooldownTime_Long, 
	WorldCooldownTime_Custom 
} WorldCooldownTime;
extern StaticDefineInt WorldCooldownTimeEnum[];

AUTO_ENUM;
typedef enum WorldDynamicSpawnType
{
	WorldDynamicSpawnType_Default,
	WorldDynamicSpawnType_Static,
	WorldDynamicSpawnType_Dynamic,
} WorldDynamicSpawnType;
extern StaticDefineInt WorldDynamicSpawnTypeEnum[];

AUTO_ENUM;
typedef enum WorldRewardLevelType
{
	WorldRewardLevelType_Map, 
	WorldRewardLevelType_Custom,
	WorldRewardLevelType_MapVariable,
	WorldRewardLevelType_Player
} WorldRewardLevelType;
extern StaticDefineInt WorldRewardLevelTypeEnum[];

AUTO_ENUM;
typedef enum WorldGameActionType
{
	WorldGameActionType_None, EIGNORE
	WorldGameActionType_GrantMission,       // Directly adds a mission to a player
	WorldGameActionType_GrantSubMission,
	WorldGameActionType_MissionOffer,       // Shows a Mission Offer dialog
	WorldGameActionType_DropMission,
	WorldGameActionType_SendFloaterMsg,
	WorldGameActionType_SendNotification,
	WorldGameActionType_TakeItem,
	WorldGameActionType_GiveItem,
	WorldGameActionType_GiveDoorKeyItem,
	WorldGameActionType_ChangeNemesisState,
	WorldGameActionType_Warp,
	WorldGameActionType_Contact,
	WorldGameActionType_Expression,
	WorldGameActionType_NPCSendMail,
	WorldGameActionType_GADAttribValue,
	WorldGameActionType_ShardVariable,
	WorldGameActionType_ActivityLog,
	WorldGameActionType_GuildStatUpdate,
	WorldGameActionType_GuildThemeSet,
	WorldGameActionType_UpdateItemAssignment,

	WorldGameActionType_Count,  EIGNORE
} WorldGameActionType;
extern StaticDefineInt WorldGameActionTypeEnum[];

AUTO_ENUM;
typedef enum WorldCarryAnimationMode
{
	WorldCarryAnimationMode_None = 0,
	WorldCarryAnimationMode_BoxCarryMode,
	WorldCarryAnimationMode_OverheadMode,
} WorldCarryAnimationMode;
extern StaticDefineInt WorldCarryAnimationModeEnum[];

AUTO_ENUM;
typedef enum WorldGameActionHeadshotType
{
	WorldGameActionHeadshotType_Specified,			// A specific costume
	WorldGameActionHeadshotType_PetContactList,		// A costume generated from a PetContactList
	WorldGameActionHeadshotType_CritterGroup,		// A costume generated from a critter group
	WorldGameActionHeadshotType_Player,				// A costume generated from the player
} WorldGameActionHeadshotType;
extern StaticDefineInt WorldGameActionHeadshotTypeEnum[];

AUTO_ENUM;
typedef enum WorldHeadshotMapVarOverrideType
{
	WorldHeadshotMapVarOverrideType_Specified,
	WorldHeadshotMapVarOverrideType_MapVar,
} WorldHeadshotMapVarOverrideType;
extern StaticDefineInt WorldHeadshotMapVarOverrideTypeEnum[];

AUTO_ENUM;
typedef enum WorldMissionActionType
{
	WorldMissionActionType_Named,
	WorldMissionActionType_MapVariable,
	WorldMissionActionType_MissionVariable,		ENAMES(MissionVariable Variable)
} WorldMissionActionType;
extern StaticDefineInt WorldMissionActionTypeEnum[];

AUTO_ENUM;
typedef enum WorldVariableActionType
{
	WorldVariableActionType_Set,
	WorldVariableActionType_IntIncrement,
	WorldVariableActionType_FloatIncrement,
	WorldVariableActionType_Reset,
} WorldVariableActionType;
extern StaticDefineInt WorldVariableActionTypeEnum[];

AUTO_ENUM;
typedef enum WleAELightType
{
	WleAELightNone,				ENAMES(None)
	WleAELightController,		ENAMES(LightController)
	WleAELightPoint,			ENAMES(PointLight)
	WleAELightSpot,				ENAMES(SpotLight)
	WleAELightProjector,		ENAMES(ProjectorLight)
	WleAELightDirectional,		ENAMES(DirectionalLight)
	WleAELightTypeCount,		EIGNORE
} WleAELightType;
extern StaticDefineInt WleAELightTypeEnum[];

AUTO_ENUM;
typedef enum WorldRoomType
{
	WorldRoomType_None = 0,
	WorldRoomType_Room,
	WorldRoomType_Partition,
	WorldRoomType_Portal,
} WorldRoomType;
extern StaticDefineInt WorldRoomTypeEnum[];

AUTO_ENUM;
typedef enum LightAffectType
{
	WL_LIGHTAFFECT_ALL,
	WL_LIGHTAFFECT_STATIC,
	WL_LIGHTAFFECT_DYNAMIC,
} LightAffectType;
extern StaticDefineInt LightAffectTypeEnum[];

AUTO_ENUM;
typedef enum GenesisMultiExcludeRotType
{
	WLECER_INPLACE,		ENAMES("InPlaceRotation")
	WLECER_FULL,		ENAMES("FullRotation")
	WLECER_NONE,		ENAMES("NoRotation")
	WLECER_COUNT,		EIGNORE
} GenesisMultiExcludeRotType;
extern StaticDefineInt GenesisMultiExcludeRotTypeEnum[];

AUTO_ENUM;
typedef enum WorldDoorType
{
	WorldDoorType_None,					//Don't transfer the player anywhere	
	WorldDoorType_MapMove,				//Standard door type
	WorldDoorType_QueuedInstance,		//Queued pve instance, just pops up a UI to join a queue
	WorldDoorType_JoinTeammate,			//Allows players to transfer to teammates that have transferred through identically tagged doors
	WorldDoorType_Count,	EIGNORE
	WorldDoorType_Keyed,
} WorldDoorType;
extern StaticDefineInt WorldDoorTypeEnum[];

AUTO_ENUM;
typedef enum WorldEncounterRewardLevelType
{
	kWorldEncounterRewardLevelType_DefaultLevel = 0,
	kWorldEncounterRewardLevelType_PlayerLevel,
	kWorldEncounterRewardLevelType_SpecificLevel,
} WorldEncounterRewardLevelType;
extern StaticDefineInt WorldEncounterRewardLevelTypeEnum[];

AUTO_ENUM;
typedef enum WorldEncounterRewardType
{
	kWorldEncounterRewardType_DefaultRewards = 0,
	kWorldEncounterRewardType_OverrideStandardRewards,
	kWorldEncounterRewardType_AdditionalRewards,
} WorldEncounterRewardType;
extern StaticDefineInt WorldEncounterRewardTypeEnum[];

AUTO_ENUM;
typedef enum WorldEncounterSpawnCondType
{
	WorldEncounterSpawnCondType_Normal, 
	WorldEncounterSpawnCondType_RequiresPlayer, 
} WorldEncounterSpawnCondType;
extern StaticDefineInt WorldEncounterSpawnCondTypeEnum[];

AUTO_ENUM;
typedef enum WorldEncounterRadiusType
{
	WorldEncounterRadiusType_None,
	WorldEncounterRadiusType_Always,
	WorldEncounterRadiusType_Short, 
	WorldEncounterRadiusType_Medium, 
	WorldEncounterRadiusType_Long,
	WorldEncounterRadiusType_Custom
} WorldEncounterRadiusType;
extern StaticDefineInt WorldEncounterRadiusTypeEnum[];

AUTO_ENUM;
typedef enum WorldEncounterTimerType
{
	WorldEncounterTimerType_None,
	WorldEncounterTimerType_Never,
	WorldEncounterTimerType_Short, 
	WorldEncounterTimerType_Medium, 
	WorldEncounterTimerType_Long, 
	WorldEncounterTimerType_Custom, 
} WorldEncounterTimerType;
extern StaticDefineInt WorldEncounterTimerTypeEnum[];

AUTO_ENUM;
typedef enum WorldEncounterWaveTimerType
{
	WorldEncounterWaveTimerType_Short, 
	WorldEncounterWaveTimerType_Medium, 
	WorldEncounterWaveTimerType_Long, 
	WorldEncounterWaveTimerType_Immediate, 
	WorldEncounterWaveTimerType_Custom,		ENAMES(Custom None Never)
} WorldEncounterWaveTimerType;
extern StaticDefineInt WorldEncounterWaveTimerTypeEnum[];

AUTO_ENUM;
typedef enum WorldEncounterWaveDelayTimerType
{
	WorldEncounterWaveDelayTimerType_None, 
	WorldEncounterWaveDelayTimerType_Short, 
	WorldEncounterWaveDelayTimerType_Medium, 
	WorldEncounterWaveDelayTimerType_Long, 
	WorldEncounterWaveDelayTimerType_Custom,	ENAMES(Custom Never)
} WorldEncounterWaveDelayTimerType;
extern StaticDefineInt WorldEncounterWaveDelayTimerTypeEnum[];

AUTO_ENUM;
typedef enum WorldEncounterDynamicSpawnType
{
	WorldEncounterDynamicSpawnType_Default,
	WorldEncounterDynamicSpawnType_Static,
	WorldEncounterDynamicSpawnType_Dynamic,
} WorldEncounterDynamicSpawnType;
extern StaticDefineInt WorldEncounterDynamicSpawnTypeEnum[];

AUTO_ENUM;
typedef enum WorldEncounterSpawnTeamSize
{
	WorldEncounterSpawnTeamSize_NotForced = 0,
	WorldEncounterSpawnTeamSize_1 = 1,
	WorldEncounterSpawnTeamSize_2 = 2,
	WorldEncounterSpawnTeamSize_3 = 3,
	WorldEncounterSpawnTeamSize_4 = 4,
	WorldEncounterSpawnTeamSize_5 = 5,
} WorldEncounterSpawnTeamSize;
extern StaticDefineInt WorldEncounterSpawnTeamSizeEnum[];

AUTO_ENUM;
typedef enum WorldEncounterMastermindSpawnType
{
	WorldEncounterMastermindSpawnType_None,
	WorldEncounterMastermindSpawnType_StaticAllowRespawn,
	WorldEncounterMastermindSpawnType_DynamicOnly,
} WorldEncounterMastermindSpawnType;
extern StaticDefineInt WorldEncounterMastermindSpawnTypeEnum[];

AUTO_ENUM;
typedef enum WorldOptionalActionPriority
{
	WorldOptionalActionPriority_Low = 0,
	WorldOptionalActionPriority_Normal = 5,
	WorldOptionalActionPriority_High = 10,
	WorldOptionalActionPriority_Order_1 = 9,
	WorldOptionalActionPriority_Order_2 = 8,
	WorldOptionalActionPriority_Order_3 = 7,
	WorldOptionalActionPriority_Order_4 = 6,
	WorldOptionalActionPriority_Order_5 = 4,
	WorldOptionalActionPriority_Order_6 = 3,
	WorldOptionalActionPriority_Order_7 = 2,
	WorldOptionalActionPriority_Order_8 = 1,
} WorldOptionalActionPriority;
extern StaticDefineInt WorldOptionalActionPriorityEnum[];

AUTO_ENUM;
typedef enum WorldTerrainExclusionType
{
	WorldTerrainExclusionType_Anywhere,
	WorldTerrainExclusionType_Above_Terrain, 
	WorldTerrainExclusionType_Below_Terrain 
} WorldTerrainExclusionType;
extern StaticDefineInt WorldTerrainExclusionTypeEnum[];

//If you change this, you need to change WorldTerrainCollisionTypeUIEnum as well
AUTO_ENUM;
typedef enum WorldTerrainCollisionType
{
	WorldTerrainCollisionType_Collide_All,
	WorldTerrainCollisionType_Collide_All_Except_Paths,
	WorldTerrainCollisionType_Collide_None,
	WorldTerrainCollisionType_Collide_SimilarTypes,				EIGNORE //All enum values after this are assumed to want to collide with only volumes of the same type
	WorldTerrainCollisionType_Collide_Lights,
	WorldTerrainCollisionType_Collide_Encounters,
	WorldTerrainCollisionType_Collide_Detail_1,
	WorldTerrainCollisionType_Collide_Detail_2,
	WorldTerrainCollisionType_Collide_Detail_3,
	WorldTerrainCollisionType_Collide_Enum_Count,				EIGNORE 
} WorldTerrainCollisionType;
extern StaticDefineInt WorldTerrainCollisionTypeEnum[];

AUTO_ENUM;
typedef enum WorldPlatformType
{
	WorldPlatformType_None,
	WorldPlatformType_Volume_AllSides,
	WorldPlatformType_Volume_Floor,
	WorldPlatformType_InsideCollidable,
	WorldPlatformType_BlockingVolume_AllSides,
} WorldPlatformType;
extern StaticDefineInt WorldPlatformTypeEnum[];

AUTO_ENUM;
typedef enum ContentAuthorSource
{
	ContentAuthor_Cryptic,
	ContentAuthor_User,
} ContentAuthorSource;
extern StaticDefineInt ContentAuthorSourceEnum[];

AUTO_ENUM;
typedef enum UGCMapType
{
	UGC_MAP_TYPE_ANY,
	UGC_MAP_TYPE_PREFAB_INTERIOR,
	UGC_MAP_TYPE_INTERIOR,
	UGC_MAP_TYPE_PREFAB_SPACE,
	UGC_MAP_TYPE_SPACE,
	UGC_MAP_TYPE_PREFAB_GROUND,
	UGC_MAP_TYPE_GROUND,
} UGCMapType;
extern StaticDefineInt UGCMapTypeEnum[];

AUTO_ENUM;
typedef enum WorldUGCRoomObjectType
{
	UGC_ROOM_OBJECT_NONE,
	UGC_ROOM_OBJECT_DOOR,
	UGC_ROOM_OBJECT_DETAIL_SET,
	UGC_ROOM_OBJECT_DETAIL_ENTRY,
	UGC_ROOM_OBJECT_PREPOP_SET,
} WorldUGCRoomObjectType;
extern StaticDefineInt WorldUGCRoomObjectTypeEnum[];

AUTO_ENUM;
typedef enum ZoneMapLightOverrideType
{
	MAP_LIGHT_OVERRIDE_NONE,
	MAP_LIGHT_OVERRIDE_USE_PRIMARY, // Primary map lights override secondary map
	MAP_LIGHT_OVERRIDE_USE_SECONDARY, // Secondary map lights override primary map
} ZoneMapLightOverrideType;
extern StaticDefineInt ZoneMapLightOverrideTypeEnum[];

AUTO_ENUM;
typedef enum VehicleRules
{
	kVehicleRules_Inherit = 0,
	kVehicleRules_VehicleAllowed,
	kVehicleRules_VehicleRequired,
	kVehicleRules_VehicleNotAllowed,
}VehicleRules;
extern StaticDefineInt VehicleRulesEnum[];

typedef enum GfxShaderSliderMode {
	SHADER_QUALITY_SLIDER_NONE,
	SHADER_QUALITY_SLIDER_LABEL,
} GfxShaderSliderMode;

AUTO_ENUM;
typedef enum ZoneMapUGCUsage
{
	ZMAP_UGC_UNUSED,
	ZMAP_UGC_USED_AS_ASSET,
	ZMAP_UGC_USED_AS_EXTERNAL_MAP,
} ZoneMapUGCUsage;
extern StaticDefineInt ZoneMapUGCUsageEnum[];

C_DECLARATIONS_END
