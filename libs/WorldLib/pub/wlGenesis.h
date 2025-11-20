#pragma once
GCC_SYSTEM

#if !defined(GENESIS_ALLOW_OLD_HEADERS)
#error Including this file was probably a mistake.  UGC should include the appropriate gslUGC*.h files.
#endif

#include "WorldLibEnums.h"
#include "Message.h"
#include "StashTable.h"
#include "referencesystem.h"
#include "worldlibstructs.h"
#include "WorldLib.h"
#include "TextParserEnums.h"
#include "wlGroupPropertyStructs.h"
#include "wlGenesisMissionsGameStructs.h"

typedef struct DoorTransitionSequenceDef DoorTransitionSequenceDef;
typedef struct Expression Expression;
typedef struct UGCGenesisBackdropSun UGCGenesisBackdropSun;
typedef struct GenesisDetailKit GenesisDetailKit;
typedef struct GenesisExteriorLayout GenesisExteriorLayout;
typedef struct GenesisExteriorLayoutInfo GenesisExteriorLayoutInfo;
typedef struct GenesisInstancedObjectParams GenesisInstancedObjectParams;
typedef struct GenesisInteriorLayout GenesisInteriorLayout;
typedef struct GenesisDetailKitLayout GenesisDetailKitLayout;
typedef struct GenesisInstancedChildParams GenesisInstancedChildParams;
typedef struct GenesisInteriorLayoutInfo GenesisInteriorLayoutInfo;
typedef struct GenesisZoneExterior GenesisZoneExterior;
typedef struct GenesisZoneInterior GenesisZoneInterior;
typedef struct GenesisZoneMission GenesisZoneMission;
typedef struct GenesisZoneNodeLayout GenesisZoneNodeLayout;
typedef struct UGCGenesisZoneShared UGCGenesisZoneShared;
typedef struct GenesisMissionChallenge GenesisMissionChallenge;
typedef struct GenesisMissionContactRequirements GenesisMissionContactRequirements;
typedef struct GenesisMissionDescription GenesisMissionDescription;
typedef struct GenesisMissionRequirements GenesisMissionRequirements;
typedef struct GenesisMissionZoneChallenge GenesisMissionZoneChallenge;
typedef struct GlobalGAELayerDef GlobalGAELayerDef;
typedef struct GroupDef GroupDef;
typedef struct GroupDefLib GroupDefLib;
typedef struct GroupVolumeProperties GroupVolumeProperties;
typedef struct HeightMapExcludeGrid HeightMapExcludeGrid;
typedef struct LogicalGroup LogicalGroup;
typedef struct MersenneTable MersenneTable;
typedef struct PlayerCostume PlayerCostume;
typedef struct GenesisProceduralEncounterProperties GenesisProceduralEncounterProperties;
typedef struct PropertyLoad PropertyLoad;
typedef struct ResourceSearchResult ResourceSearchResult;
typedef struct RewardTable RewardTable;
typedef struct SkyInfoGroup SkyInfoGroup;
typedef struct GenesisSolSysLayout GenesisSolSysLayout;
typedef struct GenesisZoneMapRoom GenesisZoneMapRoom;
typedef struct SolarSystemSunsInfo SolarSystemSunsInfo;
typedef struct GenesisSolSysZoneMap GenesisSolSysZoneMap;
typedef struct SpaceBackdropLight SpaceBackdropLight;
typedef struct TextParserState TextParserState;
typedef struct TrackerHandle TrackerHandle;
typedef struct WorldActionVolumeProperties WorldActionVolumeProperties;
typedef struct WorldCurve WorldCurve;
typedef struct WorldEventVolumeProperties WorldEventVolumeProperties;
typedef struct WorldInteractionProperties WorldInteractionProperties;
typedef struct WorldInteractionPropertyEntry WorldInteractionPropertyEntry;
typedef struct WorldOptionalActionVolumeProperties WorldOptionalActionVolumeProperties;
typedef struct WorldPatrolProperties WorldPatrolProperties;
typedef struct WorldSpawnProperties WorldSpawnProperties;
typedef struct WorldVariableDef WorldVariableDef;
typedef struct ZoneMap ZoneMap;
typedef struct ZoneMapLayer ZoneMapLayer;

#define GENESIS_EXTERIOR_KIT_SIZE 20

#define GENESIS_EXTERIOR_DEFAULT_VISTA_THICKNESS 8
#define GENESIS_EXTERIOR_DEFAULT_VISTA_HOLE_SIZE 8
#define GENESIS_EXTERIOR_VISTAS_FOLDER "GenesisExteriorVistas"
#define GENESIS_SIDE_TRAIL_NAME "Auto_Side_Trail"

#define GENESIS_EXT_LAYOUT_TEMP_FILE_DICTIONARY "GenesisMapDescExteriorLayoutTemplate"
#define GENESIS_INT_LAYOUT_TEMP_FILE_DICTIONARY "GenesisMapDescInteriorLayoutTemplate"
#define GENESIS_BACKDROP_FILE_DICTIONARY "GenesisBackdrop"

AUTO_ENUM;
typedef enum GenesisRuntimeErrorType
{
	GENESIS_WARNING,						// Something the user should probably know about but not a serious problem
	GENESIS_ERROR,							// A potentially serious problem with the generator algorithm
	GENESIS_FATAL_ERROR,					// We've hit an error that halts the generation process
} GenesisRuntimeErrorType;

AUTO_ENUM;
typedef enum GenesisRuntimeErrorScope
{
	GENESIS_SCOPE_DEFAULT,					// No knowledge, shouldn't be used
	GENESIS_SCOPE_MAP,						// This error has to do with global map properties
	GENESIS_SCOPE_ROOM,						// This error has to do with a specific room in the layout
	GENESIS_SCOPE_PATH,						// This error has to do with a specific path in the layout
	GENESIS_SCOPE_CHALLENGE,				// This error has to do with a specific challenge in the layout
	GENESIS_SCOPE_OBJECTIVE,				// This error has to do with a specific objective in the layout
	GENESIS_SCOPE_PROMPT,					// This error has to do with a specific prompt in the layout
	GENESIS_SCOPE_MISSION,					// This error has to do with a mission
	GENESIS_SCOPE_PORTAL,
	GENESIS_SCOPE_ROOM_DOOR,
	GENESIS_SCOPE_LAYOUT,
	GENESIS_SCOPE_EPISODE_PART,
	GENESIS_SCOPE_SOLSYS_DETAIL_OBJECT,
	GENESIS_SCOPE_MAP_TRANSITION,
	GENESIS_SCOPE_UGC_ITEM,
	
	GENESIS_SCOPE_INTERNAL_DICT,			// This error has to do with an internal dictionary
	GENESIS_SCOPE_INTERNAL_CODE,
} GenesisRuntimeErrorScope;

AUTO_STRUCT;
typedef struct GenesisRuntimeErrorContext
{
	GenesisRuntimeErrorScope scope;			AST(NAME("Scope"))
	bool auto_generated;					AST(NAME("AutoGenerated"))

	char* map_name;							AST(NAME("MapName"))
	char* location_name;					AST(NAME("LocationName", "RoomName", "PathName"))
	char* challenge_name;					AST(NAME("ChallengeName"))
	char* objective_name;					AST(NAME("ObjectiveName"))
	char* prompt_name;						AST(NAME("PromptName"))
	char* prompt_block_name;				AST(NAME("PromptBlockName")) 
	int prompt_action_index;				AST(NAME("PromptActionIndex") DEFAULT(-1))
	char* mission_name;						AST(NAME("MissionName"))
	char* portal_name;						AST(NAME("PortalName"))
	char* layout_name;						AST(NAME("LayoutName"))
	char* solsys_detail_object_name;		AST(NAME("SolSysDetailObjectName"))
	char* ep_part_name;						AST(NAME("EpPartName", "EpisodePartName"))
	char* ugc_item_name;					AST(NAME("UGCItemName"))
	
	const char* dict_name;					AST(NAME("DictionaryName") POOL_STRING)
	const char* resource_name;				AST(NAME("ResourceName") POOL_STRING)
} GenesisRuntimeErrorContext;
extern ParseTable parse_GenesisRuntimeErrorContext[];
#define TYPE_parse_GenesisRuntimeErrorContext GenesisRuntimeErrorContext

AUTO_STRUCT;
typedef struct GenesisRuntimeError
{
	GenesisRuntimeErrorType type;			AST(NAME("Type"))
	GenesisRuntimeErrorContext* context;	AST(NAME("Entry"))
	char *field_name;						AST(NAME("FieldName"))
	char *object_name;						AST(NAME("ObjectName")) // Name of the scope object, if there is one (i.e. room or challenge name)
	char *message;							AST(NAME("Message"))
	const char *message_key;				AST(NAME("MessageKey") POOL_STRING)

	// Use sparingly!  This text is not translated!
	char* extraText;						AST(NAME("ExtraText"))
} GenesisRuntimeError;
extern ParseTable parse_GenesisRuntimeError[];
#define TYPE_parse_GenesisRuntimeError GenesisRuntimeError

AUTO_STRUCT;
typedef struct GenesisRuntimeStage
{
	char *name;								AST(STRUCTPARAM)
	GenesisRuntimeError **errors;			AST(NAME("Error"))
} GenesisRuntimeStage;
extern ParseTable parse_GenesisRuntimeStage[];
#define TYPE_parse_GenesisRuntimeStage GenesisRuntimeStage

AUTO_STRUCT;
typedef struct GenesisRuntimeStatus
{
	GenesisRuntimeStage **stages;			AST(NAME("Stage")) // Each stage of the process creates a new block here
} GenesisRuntimeStatus;
extern ParseTable parse_GenesisRuntimeStatus[];
#define TYPE_parse_GenesisRuntimeStatus GenesisRuntimeStatus

AUTO_ENUM;
typedef enum GenesisSkyType
{
	GST_Name,
	GST_Tag,
} GenesisSkyType;

AUTO_STRUCT;
typedef struct GenesisSky
{
	GenesisSkyType type;						AST(STRUCTPARAM)
	char *str;									AST(STRUCTPARAM)
} GenesisSky;

AUTO_STRUCT;
typedef struct GenesisSkyGroup
{
	GenesisSky **skies;							AST( NAME("Sky"))
	U32 override_times : 1;						AST( NAME("OverrideTimes"))
} GenesisSkyGroup;
extern ParseTable parse_GenesisSkyGroup[];
#define TYPE_parse_GenesisSkyGroup GenesisSkyGroup

AUTO_STRUCT;
typedef struct GenesisInteriorLightingProps
{
	GroupDefRef child_light;					AST(EMBEDDED_FLAT)	
	F32 inner_angle;							AST(NAME("InnerAngle"))
	F32 outer_angle;							AST(NAME("OuterAngle"))
	U8 no_lights : 1;							AST(NAME("NoLights"))
} GenesisInteriorLightingProps;

AUTO_STRUCT;
typedef struct GenesisSoundInfo
{
	char **amb_sounds;							AST(NAME("AmbSound"))
	char **amb_hallway_sounds;					AST(NAME("AmbHallwaySound")) //Interiors Only

	GlobalGAELayerDef **not_used;				AST(NAME("GlobalGAELayers")) //Not used anymore, but can't ast ignore
} GenesisSoundInfo;

AUTO_STRUCT AST_IGNORE_STRUCT(UGCProperties);
typedef struct GenesisBackdrop
{
	const char	*filename;						AST( NAME(FN) CURRENTFILE  USERFLAG(TOK_USEROPTIONBIT_1)) // Must be first for ParserReloadFile
	char		*name;							AST( NAME(Name) KEY  USERFLAG(TOK_USEROPTIONBIT_1))
	char		*tags;							AST( NAME("Tags"))

	SkyInfoGroup *sky_group;					AST(NAME("SkyGroup"))
	GenesisSkyGroup *rand_sky_group;			AST(NAME("RandSkyGroup")) //Only used in transmogrify

	char **amb_sounds;							AST(NAME("AmbSound"))
	char **amb_hallway_sounds;					AST(NAME("AmbHallwaySound")) //Interiors Only

	const char *override_cubemap;				AST( NAME(OverrideCubeMap) POOL_STRING )

	WorldPowerVolumeProperties *power_volume;	AST(NAME("PowerVolume"))
	WorldFXVolumeProperties *fx_volume;			AST(NAME("FXVolume"))

	//Interior Only
	GenesisInteriorLightingProps int_light;		AST(NAME("InteriorLight"))

	//SolarSystem Only
	SolarSystemSunsInfo *sun_info;				AST(NAME("SunInfo"), ADDNAMES("Root"))
	SpaceBackdropLight *far_light;				AST(NAME("FarSystemLight"))
	SpaceBackdropLight *near_light;				AST(NAME("NearSystemLight"))

	GlobalGAELayerDef **not_used;				AST(NAME("GlobalGAELayers")) //Not used anymore, but can't ast ignore
} GenesisBackdrop;
extern ParseTable parse_GenesisBackdrop[];
#define TYPE_parse_GenesisBackdrop GenesisBackdrop

AUTO_ENUM;
typedef enum GenesisEncounterJitterType
{
	GEJT_Default = 0,
	GEJT_None,
	GEJT_Custom
} GenesisEncounterJitterType;
extern StaticDefineInt GenesisEncounterJitterTypeEnum[];

AUTO_STRUCT;
typedef struct GenesisEncounterJitter
{
	GenesisEncounterJitterType jitter_type;			AST(NAME("JitterType"))
	F32 enc_pos_jitter;								AST(NAME("EncPosJitter"))
	F32 enc_rot_jitter;								AST(NAME("EncRotJitter"))
} GenesisEncounterJitter;
extern ParseTable parse_GenesisEncounterJitter[];
#define TYPE_parse_GenesisEncounterJitter GenesisEncounterJitter

AUTO_STRUCT;
typedef struct GenesisPlacementActorParams {
	WorldVariableDef **world_vars;
	WorldActorCostumeProperties *costume;
	const char *pcFSMName;
	const char *pcActorName;
} GenesisPlacementActorParams;
extern ParseTable parse_GenesisPlacementActorParams[];
#define TYPE_parse_GenesisPlacementActorParams GenesisPlacementActorParams

AUTO_STRUCT;
typedef struct GenesisPlacementChildParams
{
	Vec3 vOffset;
	Vec3 vPyr;

	GenesisPlacementActorParams actor_params;

	U32 is_actor : 1;

	// See GenesisPlacementParams for information on these booleans:
	bool bAbsolutePos;						AST(NAME("AbsolutePosition"))
	bool bSnapRayCast;						AST(NAME("SnapRayCast"))
	bool bSnapToGeo;						AST(NAME("SnapToGeo"))
	bool bSnapNormal;						AST(NAME("SnapNormal"))
	bool bLegacyHeightCheck;				AST(NAME("LegacyHeightCheck"))
} GenesisPlacementChildParams;
extern ParseTable parse_GenesisPlacementChildParams[];
#define TYPE_parse_GenesisPlacementChildParams GenesisPlacementChildParams

AUTO_STRUCT;
typedef struct GenesisPlacementParams
{
	GenesisChallengeFacing facing;			AST(NAME("Facing"))
	GenesisChallengePlacement location;		AST(NAME("Placement"))

	char *ref_prefab_location;				AST(NAME("RefPrefabLocation"))
	char *ref_challenge_name;				AST(NAME("RefChallengeName"))
	char *ref_door_dest_name;				AST(NAME("RefDoorDestName"))

	GenesisEncounterJitter enc_jitter;		AST(NAME("EncounterJitter"))
	F32 constant_rotation;					AST(NAME("ConstantRotation"))	// Added to initial orientation
	F32 rotation_increment;					AST(NAME("RotationIncrement", "RandomRotation"))	// In degrees, the possible rotations of this object

	U8 on_wall : 1;							AST(NAME("WallObject"))			// This object only appears along a wall (DEPRECATED; Use Placement instead)
	U8 pivot_on_wall : 1;					AST(NAME("PivotOnWall"))		// Ignores the bounds of the object when placing an object on a wall
	U8 common_room_rot : 1;					AST(NAME("CommonRoomRot"))		// All objects with this set, get the same rotation for the whole room.
	U8 use_room_dir : 1;					AST(NAME("UseRoomDir"))			// All objects with this set, get the same rotaion as the grid pattern.
	U8 mirror : 1;							AST(NAME("Mirror"))				// Only works for detail kits. Place each instance of this object on both sides of the room. Randomly north/south or east/west.
	U8 fill_grid : 1;						AST(NAME("FillGrid"))			// Fills a grid full of this object
	U8 grid_uses_density : 1;				AST(NAME("GridUsesDenstiy"))	// grid will get random gaps
	U8 pre_challenge : 1;					AST(NAME("PreChallenge"))		// Gets placed before challenge objects
	U8 is_start_spawn : 1;					AST(NAME("StartSpawn"))			// Gets placed before challenge objects
	U8 grid_ignores_padding : 1;			AST(NAME("GridIgnoresPadding"))	// Grid placement will ignore padding when centering and offseting

	int max_rows;							AST(NAME("MaxRows"))			// If FillGrid is set then the max number of rows to use
	int max_cols;							AST(NAME("MaxCols"))			// If FillGrid is set then the max number of cols to use
	F32 row_spacing;						AST(NAME("RowSpacing"))			// Grid scale.  When facing north, this is the z scale
	F32 col_spacing;						AST(NAME("ColSpacing"))			// Grid scale.  When facing north, this is the x scale
	F32 row_offset;							AST(NAME("RowOffset"))			// Grid row offset.  When facing north, this is the z offset
	F32 col_offset;							AST(NAME("ColOffset"))			// Grid coll offset.  When facing north, this is the x offset
	int max_per_room;						AST(NAME("MaxPerRoom"))			// Maximum that we want to be placed in a given room
	F32 exclusion_dist;						AST(NAME("ExclusionDist"))		// Distance to keep between objects of the same affinity group or def
	const char *affinity_group;				AST(NAME("AffinityGroup") POOL_STRING)

	F32 override_position[3];				AST(NAME("OverridePos"))		// If we're in Specified mode, this is used to the exclusion of all else
	F32 override_rot;						AST(NAME("OverrideRot"))		// If we're in Specified mode, this is used to the exclusion of all else
	bool bAbsolutePos;						AST(NAME("AbsolutePosition"))	// Use the InternalSpawnPoint Y for height. Overrides SnapRayCast
	bool bSnapRayCast;						AST(NAME("SnapRayCast"))		// Do a ray cast to determine Y placement. Ignored if bAbsolutePos
	bool bSnapToGeo;						AST(NAME("SnapToGeo"))
	bool bSnapNormal;						AST(NAME("SnapNormal"))
	bool bLegacyHeightCheck;				AST(NAME("LegacyHeightCheck"))	// If SnapRayCast, use the old height check values to preserve legacy project functionality
	GenesisPlacementChildParams **children; AST(NAME("ChildPlacement"))		// If this object has children (e.g. patrol, encounter, use these)
} GenesisPlacementParams;

AUTO_STRUCT;//sfenton TODO: PropLoad: need to do fixup on this too
typedef struct GenesisProceduralObjectParams
{
	char *model_name;										AST( NAME(ModelName) )
	WorldRoomProperties *room_properties;					AST( NAME(RoomProperties) )
	WorldPowerVolumeProperties *power_volume;				AST( NAME(PowerVolume) )
	WorldFXVolumeProperties *fx_volume;						AST( NAME(FXVolume) )
	WorldSkyVolumeProperties *sky_volume_properties;		AST( NAME(SkyVolume) )
	WorldSoundVolumeProperties *sound_volume_properties;	AST( NAME(SoundVolume) )
	WorldSoundSphereProperties *sound_sphere_properties;	AST( NAME(SoundSphere) )
	WorldActionVolumeProperties *action_volume_properties;	AST( NAME(ActionVolume) )
	WorldEventVolumeProperties *event_volume_properties;	AST( NAME(EventVolume) )
	WorldOptionalActionVolumeProperties *optionalaction_volume_properties; AST( NAME(OptionalActionVolume) )
	WorldCurve *curve;										AST( NAME(Curve) )
	WorldPatrolProperties *patrol_properties;				AST( NAME(PatrolProperties) )
	WorldInteractionProperties *interaction_properties;		AST( NAME(InteractionProperties) )
	WorldSpawnProperties *spawn_properties;					AST( NAME(SpawnProperties) )
	WorldPhysicalProperties physical_properties;			AST( NAME(PhysicalProperties) )
	WorldGenesisProperties *genesis_properties;				AST( NAME(Genesis) )
	WorldTerrainProperties terrain_properties;				AST( NAME(TerrainProperties) )
	GroupVolumeProperties *volume_properties;				AST( NAME(VolumeProperties) )
	GroupHullProperties *hull_properties;					AST( NAME(HullProperties) )
	WorldLightProperties *light_properties;					AST( NAME(LightProperties) )
} GenesisProceduralObjectParams;
extern ParseTable parse_GenesisProceduralObjectParams[];
#define TYPE_parse_GenesisProceduralObjectParams GenesisProceduralObjectParams

AUTO_STRUCT;
typedef struct GenesisInstancedChildParams
{
	const char *pcInternalName;
	DisplayMessage displayNameMsg;							AST(STRUCT(parse_DisplayMessage))
	Vec3 vPyr;
	Vec3 vOffset;

	WorldActorCostumeProperties *pCostumeProperties;
} GenesisInstancedChildParams;
extern ParseTable parse_GenesisInstancedChildParams[];
#define TYPE_parse_GenesisInstancedChildParams GenesisInstancedChildParams

AUTO_STRUCT;
typedef struct GenesisContactParams
{
	char *pcContactName;									AST(NAME("ContactName"))
	REF_TO(PlayerCostume) hCostume;							AST(NAME("Costume"))
} GenesisContactParams;
extern ParseTable parse_GenesisContactParams[];
#define TYPE_parse_GenesisContactParams GenesisContactParams

AUTO_STRUCT;
typedef struct GenesisInteractObjectParams
{
	bool bDisallowVolume;									AST(NAME(DisallowVolume))
	bool bIsUGCDoor;										AST(NAME(IsUGCDoor))

	bool clickieVisibleWhenCondPerEnt;						AST( NAME(ClickieVisibleWhenPerEnt) )
	Expression *clickieVisibleWhenCond;						AST( NAME(ClickieVisibleWhen) LATEBIND )

	DisplayMessage displayNameMsg;							AST( NAME(DisplayName) STRUCT(parse_DisplayMessage) )
	Expression *succeedWhenCond;							AST( NAME(SucceedWhen) LATEBIND )	
	Expression *interactWhenCond;							AST( NAME(InteractWhen) LATEBIND )
	WorldInteractionPropertyEntry **eaInteractionEntries;	AST( NAME(InteractionEntry) )
} GenesisInteractObjectParams;
extern ParseTable parse_GenesisInteractObjectParams[];
#define TYPE_parse_GenesisInteractObjectParams GenesisInteractObjectParams

AUTO_STRUCT;
typedef struct GenesisInstancedObjectParams
{
	F32 model_scale[3];										AST( NAME(ModelScale) )
	Expression *encounterSpawnCond;							AST( NAME(EncounterSpawnWhen) LATEBIND )
	Expression *encounterDespawnCond;						AST( NAME(EncounterDespawnWhen) LATEBIND )
	bool has_patrol;										AST( NAME(HasPatrol) )
	const char* pcFSMName;									AST( NAME(FSMName) )
	GenesisInstancedChildParams **eaChildParams;			AST( NAME(ChildParam) NAME(ActorData) )
	bool bChildParamsAreGroupDefs;							AST( NAME(ChildParamsAreGroupDefs))
	char *pcMissionName;									AST( NAME(MissionName))
	GenesisMissionContactRequirements *pContact;			AST(NAME("Contact"))
} GenesisInstancedObjectParams;
extern ParseTable parse_GenesisInstancedObjectParams[];
#define TYPE_parse_GenesisInstancedObjectParams GenesisInstancedObjectParams

AUTO_STRUCT;
typedef struct GenesisObjectVolume
{
	// Determines if we use the size for a cube or a sphere
	bool is_square;											AST(NAME("IsSquare"))
	// if this is true, size is a relative scale of the object's radius or bounds
	bool is_relative;										AST(NAME("IsRelative"))
	// Take the object bounds and scale them by the following
	F32 size;												AST(NAME("Size"))
	// Or, ignore all the previous and just use these volume properties
	GroupVolumeProperties *pVolumeProperties;				AST(NAME("VolumeProperties"))
} GenesisObjectVolume;
extern ParseTable parse_GenesisObjectVolume[];
#define TYPE_parse_GenesisObjectVolume GenesisObjectVolume

AUTO_STRUCT;
typedef struct GenesisRoomDetail 
{
	int iSelected;
	const char *astrParameter;									AST(POOL_STRING)
} GenesisRoomDetail;
extern ParseTable parse_GenesisRoomDetail[];
#define TYPE_parse_GenesisRoomDetail GenesisRoomDetail

AUTO_STRUCT;
typedef struct GenesisRoomDoorSwitch
{
	int iIndex;
	int iSelected;
	const char *astrScopePath;									AST(POOL_STRING)
} GenesisRoomDoorSwitch;
extern ParseTable parse_GenesisRoomDoorSwitch[];
#define TYPE_parse_GenesisRoomDoorSwitch GenesisRoomDoorSwitch

// An important object that must be placed inside our room
AUTO_STRUCT;
typedef struct GenesisObject
{
	// exactly one of these should be filled out
	GroupDefRef obj;										AST(EMBEDDED_FLAT)
	REF_TO(DoorTransitionSequenceDef) start_spawn_using_transition; AST(NAME("StartSpawnUsingTransition"))

	GenesisChallengeType challenge_type;					AST(NAME("ChallengeType"))
	char *challenge_name;									AST(NAME("ExternalName"))
	int challenge_id;										AST(NAME("ChallengeID"))
	int challenge_count;									AST(NAME("ChallengeCount"))
	GenesisPlacementParams params;							AST(EMBEDDED_FLAT)
	bool has_patrol;
	WorldPatrolProperties *patrol_specified;				AST(NAME("SpecifiedPatrol"))
	bool challenge_is_unique;								AST(NAME("ChallengeIsUnique"))
	bool force_named_object;								AST(NAME("ForceNamedObject"))
	char *spawn_point_name;									AST(NAME("ChallengeSpawnName"))
	bool is_trap;											AST(NAME("IsTrap"))
	int platform_group;										AST(NAME("PlatformGroup"))
	int platform_parent_group;								AST(NAME("PlatformParentGroup"))
	int platform_parent_level;								AST(NAME("PlatformParentLevel"))
	GenesisRoomDetail **eaRoomDetails;						AST(NAME("RoomDetail"))
	GenesisRoomDoorSwitch **eaRoomDoors;					AST(NAME("RoomDoor"))

	GenesisObjectVolume *volume;							AST(NAME("Volume"))

	GenesisRuntimeErrorContext* source_context;			AST(NAME("SourceContext"))
} GenesisObject;
extern ParseTable parse_GenesisObject[];
#define TYPE_parse_GenesisObject GenesisObject

// A patrol object that will get placed
AUTO_STRUCT;
typedef struct GenesisPatrolObject
{
	GenesisObject* owner_challenge;							AST(NAME("OwnerChallenge"))
	GenesisPatrolType type;									AST(NAME("Type"))

	// Path-specific data
	bool path_start_is_challenge_pos;						AST(NAME("PathStartIsChallengePos"))
	GenesisPlacementParams path_start;						AST(NAME("PathStart"))
	GenesisPlacementParams path_end;						AST(NAME("PathEnd"))
} GenesisPatrolObject;
extern ParseTable parse_GenesisPatrolObject[];
#define TYPE_parse_GenesisPatrolObject GenesisPatrolObject

////////////////////////////////////////////////////////////
// Placement data structures
//
// These all are created during genesisGenerate() and get consumed by
// genesisPlaceObjects()
////////////////////////////////////////////////////////////
typedef struct GenesisToPlaceObject
{
	const char *object_name;
	Mat4 mat;
	bool mat_relative;
	int uid;								// If zero, generate a new GroupDef based on the GenesisProceduralObjectParams below
	U32 seed;
	F32 scale;
	bool challenge_is_unique;				// If TRUE, use challenge_name as logical name, else logical group
	char *challenge_name;					// Used as the logical group or logical name
	int challenge_count;
	U32 challenge_index;					// Unique index in challenge group
	bool force_named_object;				// This object *always* gets a logical name, even if we have to place a group above it
	struct GenesisToPlaceObject *parent;
	GenesisProceduralObjectParams *params;		// Only used if uid==0
	GenesisInteractObjectParams *interact;		// In Genesis, object must also be instanced. This restriction doesn't exist in UGC.
	GenesisInstancedObjectParams *instanced;	// Only used if uid!=0 (Never needs to be freed; points directly to MissionReqs)
	const char *spawn_name;					// Logical name at map scope for a subobject called "SpawnPoint"
	const char *trap_name;					// Logical name at map scope for a subobject called "Trap_Core"

	GenesisRoomDetail **eaRoomDetails;
	GenesisRoomDoorSwitch **eaRoomDoors;

	// Used internally
	TrackerHandle *handle;					// If we're using wleOpCreate()
	GroupDef *group_def;					// If we're manually creating GroupDefs
	U32 uid_in_parent;						// Filled in while creating objects
	GenesisRuntimeErrorContext* source_context;
} GenesisToPlaceObject;

typedef struct GenesisToPlacePatrol
{
	char* patrol_name;
	WorldPatrolProperties patrol_properties;

	GenesisRuntimeErrorContext* source_context;
} GenesisToPlacePatrol;

typedef struct GenesisToPlacePlatformGroup
{
	int group_id;
	int platform_level;
	HeightMapExcludeGrid *platform_grid;
} GenesisToPlacePlatformGroup;

typedef struct GenesisToPlaceState
{
	GenesisToPlaceObject** objects;
	
	// Patrols need to be placed later so that different room's patrols
	// can be linked together
	GenesisToPlacePatrol** patrols;

	GenesisToPlacePlatformGroup** platform_groups;
} GenesisToPlaceState;

AUTO_ENUM;
typedef enum GenesisMapType
{
	GenesisMapType_SolarSystem,
	GenesisMapType_Exterior,
	GenesisMapType_Interior,
	GenesisMapType_MiniSolarSystem,
	GenesisMapType_UGC_Space,
	GenesisMapType_UGC_Prefab,
	GenesisMapType_None,//Can not be 0 because maps exist where default = solar system
} GenesisMapType;
extern StaticDefineInt GenesisMapTypeEnum[];

AUTO_STRUCT;
typedef struct GenesisMapDescBackdrop
{
	GenesisTagOrName backdrop_specifier;			AST(NAME("BackdropSpecifier"))
	char **backdrop_tag_list;						AST(NAME("BackdropTags2")) //Either specify a random Backdrop with said tags,
	char *old_backdrop_tags;						AST(NAME("BackdropTags"))
	REF_TO(GenesisBackdrop) backdrop;				AST(NAME("Backdrop")) //or a specific Backdrop
} GenesisMapDescBackdrop;
extern ParseTable parse_GenesisMapDescBackdrop[];
#define TYPE_parse_GenesisMapDescBackdrop GenesisMapDescBackdrop

AUTO_STRUCT;
typedef struct GenesisMapDescExteriorLayoutTemplate
{
	char *name;										AST(NAME("Name") STRUCTPARAM KEY)
	const char *filename;							AST(CURRENTFILE  USERFLAG(TOK_USEROPTIONBIT_1))
	GenesisMapDescBackdrop *backdrop_info;			AST(NAME("BackdropInfo"))
	GenesisExteriorLayoutInfo *layout_info;			AST(NAME("LayoutInfo"))
	GenesisDetailKitLayout *detail_kit_1;			AST(NAME("Detail1"))
	GenesisDetailKitLayout *detail_kit_2;			AST(NAME("Detail2"))
} GenesisMapDescExteriorLayoutTemplate;
extern ParseTable parse_GenesisMapDescExteriorLayoutTemplate[];
#define TYPE_parse_GenesisMapDescExteriorLayoutTemplate GenesisMapDescExteriorLayoutTemplate

AUTO_STRUCT;
typedef struct GenesisMapDescInteriorLayoutTemplate
{
	char *name;										AST(NAME("Name") STRUCTPARAM KEY)
	const char *filename;							AST(CURRENTFILE  USERFLAG(TOK_USEROPTIONBIT_1))
	GenesisMapDescBackdrop *backdrop_info;			AST(NAME("BackdropInfo"))
	GenesisInteriorLayoutInfo *layout_info;			AST(NAME("LayoutInfo"))
	GenesisDetailKitLayout *detail_kit_1;			AST(NAME("Detail1"))
	GenesisDetailKitLayout *detail_kit_2;			AST(NAME("Detail2"))
} GenesisMapDescInteriorLayoutTemplate;
extern ParseTable parse_GenesisMapDescInteriorLayoutTemplate[];
#define TYPE_parse_GenesisMapDescInteriorLayoutTemplate GenesisMapDescInteriorLayoutTemplate

AUTO_STRUCT;
typedef struct GenesisLayoutCommonData
{
	U32 layout_seed;												AST(NAME("LayoutSeed"))
	GenesisMapDescBackdrop backdrop_info;							AST(NAME("BackdropInfo"))
	bool no_sharing_detail;											AST(NAME("NoSharingDetail")) // used to force detail objects to not be shared 
	GenesisEncounterJitter jitter;									AST(NAME("EncounterJitter"))	
} GenesisLayoutCommonData;
extern ParseTable parse_GenesisLayoutCommonData[];
#define TYPE_parse_GenesisLayoutCommonData GenesisLayoutCommonData

AUTO_STRUCT;
typedef struct GenesisLayerBounds
{
	Vec3 layer_min;
	Vec3 layer_max;
} GenesisLayerBounds;

AUTO_STRUCT;
typedef struct GenesisLayerBoundsList
{
	GenesisLayerBounds **sol_sys_bounds;
	GenesisLayerBounds **interior_bounds;
	GenesisLayerBounds *exterior_bounds;
} GenesisLayerBoundsList;
extern ParseTable parse_GenesisLayerBoundsList[];
#define TYPE_parse_GenesisLayerBoundsList GenesisLayerBoundsList

//Obsolete Map Description Data
AUTO_STRUCT;
typedef struct GenesisMapDescriptionVersion0
{
	GenesisMapType map_type;										AST(NAME("Type"))
	GenesisTemplateOrCustom layout_info_specifier;					AST(NAME("LayoutInfoSpecifier"))
	REF_TO(GenesisMapDescExteriorLayoutTemplate) ext_template;		AST(NAME("ExteriorLayoutInfoTemplate"))
	REF_TO(GenesisMapDescInteriorLayoutTemplate) int_template;		AST(NAME("InteriorLayoutInfoTemplate"))
	GenesisMapDescBackdrop backdrop_info;							AST(EMBEDDED_FLAT)
	bool no_sharing_detail;											AST(NAME("NoSharingDetail")) // used to force detail objects to not be shared 
	GenesisEncounterJitter jitter;									AST(NAME("EncounterJitter"))	
} GenesisMapDescriptionVersion0;

AUTO_STRUCT AST_FIXUPFUNC(fixupGenesisMapDescription);
typedef struct GenesisMapDescription
{
	char *name;														AST(NAME("Name") STRUCTPARAM KEY)
	const char *filename;											AST(CURRENTFILE  USERFLAG(TOK_USEROPTIONBIT_1))
	char *scope;													AST(POOL_STRING)
	char* comments;													AST(NAME("Comments"))
	U8 version;														AST(NAME("Version"))
	bool is_tracking_enabled;										AST(NAME("TrackingEnabled"))

	//Layouts
	GenesisSolSysLayout **solar_system_layouts;						AST(NAME("SolarSystemLayout"))
	GenesisInteriorLayout **interior_layouts;						AST(NAME("InteriorLayout"))
	GenesisExteriorLayout *exterior_layout;							AST(NAME("ExteriorLayout"))

	//Mission Data
	GenesisMissionDescription **missions;							AST(NAME("MissionDescription"))
	GenesisMissionChallenge **shared_challenges;					AST(NAME("SharedChallenge"))

	//Obsolete Data
	GenesisMapDescriptionVersion0 version_0;						AST(EMBEDDED_FLAT)
} GenesisMapDescription;
extern ParseTable parse_GenesisMapDescription[];
#define TYPE_parse_GenesisMapDescription GenesisMapDescription
#define GENESIS_MAP_DESC_VERSION 2

AUTO_STRUCT AST_IGNORE("Type") AST_IGNORE("NoSharingDetail");
typedef struct GenesisZoneMapData
{
	U32 seed;														AST(NAME("Seed"))
	U32 detail_seed;												AST(NAME("DetailSeed"))
	bool is_map_tracking_enabled;									AST(NAME("MapTrackingEnabled"))

	//Layout Data
	GenesisSolSysZoneMap **solar_systems;							AST(NAME("SolarSystem"))
	GenesisZoneInterior **genesis_interiors;						AST(NAME("GenesisInterior"))
	GenesisZoneExterior *genesis_exterior;							AST(NAME("GenesisExterior"))
	GenesisZoneNodeLayout *genesis_exterior_nodes;					AST(NAME("GenesisExteriorNodes"))

	//Mission Data
	GenesisZoneMission **genesis_mission;							AST(NAME("GenesisMission"))
	GenesisMissionZoneChallenge **genesis_shared_challenges;		AST(NAME("GenesisSharedChallenge"))
	GenesisProceduralEncounterProperties **encounter_overrides;		AST(NAME("EncounterOverride"))

	// Kept for reference and possible revision
	GenesisMapDescription *map_desc;								AST(NAME("MapDescription"))

	bool skip_terrain_update;										NO_AST
} GenesisZoneMapData;
extern ParseTable parse_GenesisZoneMapData[];
#define TYPE_parse_GenesisZoneMapData GenesisZoneMapData

// This is used for import/export
AUTO_STRUCT  AST_STARTTOK("") AST_ENDTOK("");
typedef struct GenesisMapDescriptionFile
{
	GenesisMapDescription *map_desc;				AST(NAME("MapDescription"))
} GenesisMapDescriptionFile;
extern ParseTable parse_GenesisMapDescriptionFile[];
#define TYPE_parse_GenesisMapDescriptionFile GenesisMapDescriptionFile

typedef enum GenesisDetailKitType
{
	GDKT_Detail_1=0,
	GDKT_Detail_2,
	GDKT_Light_Details,
} GenesisDetailKitType;

AUTO_STRUCT;
typedef struct GenesisDetailKitAndDensity
{
	REF_TO(GenesisDetailKit) details;				AST(NAME("Details"), ADDNAMES("DetailKit"))		// Detail set to place, unless overridden by mission
	F32 detail_density;								AST(NAME("DetailDensity"))	// From 0-100%, multiplies the Detail Kit's object count
	GenesisDetailKit *light_details;				NO_AST //Only used for lights and replaces the details ref pointer
} GenesisDetailKitAndDensity;
extern ParseTable parse_GenesisDetailKitAndDensity[];
#define TYPE_parse_GenesisDetailKitAndDensity GenesisDetailKitAndDensity

AUTO_STRUCT AST_FIXUPFUNC(fixupGenesisDetailKitLayout);
typedef struct GenesisDetailKitLayout
{
	GenesisTagOrName detail_kit_specifier;			AST(NAME("DetailSpecifier"))
	char **detail_tag_list;							AST(NAME("DetailTags2"))
	char *old_detail_tags;							AST(NAME("DetailTags"))
	REF_TO(GenesisDetailKit) detail_kit;			AST(NAME("DetailKit"))
	F32 detail_density;								AST(NAME("DetailDensity") DEFAULT(100)) // From 0-100%, multiplies the Detail Kit's object count
	bool vary_per_room;								AST(NAME("VaryPerRoom"))
	GenesisDetailKit *random_detail_kit;			NO_AST
} GenesisDetailKitLayout;
extern ParseTable parse_GenesisDetailKitLayout[];
#define TYPE_parse_GenesisDetailKitLayout GenesisDetailKitLayout

AUTO_STRUCT AST_FIXUPFUNC(fixupGenesisRoomDetailKitLayout);
typedef struct GenesisRoomDetailKitLayout
{
	GenesisTagNameDefault detail_specifier;	AST(NAME("DetailSpecifier"))
	char **detail_tag_list;					AST(NAME("DetailTags2"))
	char *old_detail_tags;					AST(NAME("DetailTags"))
	REF_TO(GenesisDetailKit) detail_kit;	AST(NAME("DetailKit"))
	bool detail_density_override;			AST(NAME("DetailDensityOverride")) // If this is set, then detail_density overrides layout
	F32 detail_density;						AST(NAME("DetailDensity")) // From 0-100%, multiplies the Detail Kit's object count
} GenesisRoomDetailKitLayout;
extern ParseTable parse_GenesisRoomDetailKitLayout[];
#define TYPE_parse_GenesisRoomDetailKitLayout GenesisRoomDetailKitLayout

AUTO_STRUCT;
typedef struct GenesisConfigCheckedAttrib
{
	const char* name;						AST(NAME(Name) POOL_STRING)
	const char* displayName;				AST(NAME(DisplayName))
	char* playerExprText;					AST(NAME(PlayerExprText))
	char* teamExprText;						AST(NAME(TeamExprText))
} GenesisConfigCheckedAttrib;
extern ParseTable parse_GenesisConfigCheckedAttrib[];
#define TYPE_parse_GenesisConfigCheckedAttrib GenesisConfigCheckedAttrib

AUTO_STRUCT AST_IGNORE(WarpActionCategory);
typedef struct GenesisConfig
{
	char* fallback_prompt_text;				AST(NAME("FallbackPromptText"))
	
	WorldVariableDef** variable_defs;		AST(NAME("VariableDef"))
	int vista_thickness;					AST(NAME("VistaThickness"))
	int vista_hole_size;					AST(NAME("VistaHoleSize"))

	WorldSkyVolumeProperties *boundary;		AST(NAME("BoundsSkyFade"))
	GenesisConfigCheckedAttrib** checkedAttribs; AST(NAME("CheckedAttrib"))
} GenesisConfig;
extern ParseTable parse_GenesisConfig[];
#define TYPE_parse_GenesisConfig GenesisConfig

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(End) AST_STRIP_UNDERSCORES;
typedef struct GenesisZoneMapInfoLayer
{
	SkyInfoGroup *			region_sky_group;			AST( NAME(SkyGroup) )
	const char *			region_override_cubemap;	AST( NAME(OverrideCubeMap) POOL_STRING )	

	// Prefab only
	WorldRegionType			region_type;				AST( NAME(RegionType) )
	const char *			external_map_name;			AST( NAME(ExternalMap) POOL_STRING ) // If region_type cannot be determined (WRT_NONE)
} GenesisZoneMapInfoLayer;
extern ParseTable parse_GenesisZoneMapInfoLayer[];
#define TYPE_parse_GenesisZoneMapInfoLayer GenesisZoneMapInfoLayer

AUTO_STRUCT AST_IGNORE(Type) AST_IGNORE(ExternalRegion) AST_IGNORE(OverrideCubeMap) AST_IGNORE(SolarSystemShoeboxes)  AST_STARTTOK("") AST_ENDTOK(End) AST_STRIP_UNDERSCORES;
typedef struct GenesisZoneMapInfo
{
	const char *			filename;					AST (STRUCTPARAM POOL_STRING FILENAME)

	// Added during fixup
	const char*				vista_map;					AST( NAME(VistaMap) POOL_STRING )
	bool					is_vista;					AST( NAME(IsVistaTerrain) )
	const char*				external_map;				AST( NAME(ExternalMap) POOL_STRING )
	int						mission_level;				AST( NAME(MissionLevel) )
	MissionLevelType		level_type;					AST( NAME(LevelType) )
	GenesisProceduralEncounterProperties **encounter_overrides; AST( NAME(EncounterOverride) )
	GenesisZoneMapInfoLayer **solsys_layers;			AST( NAME(SolSysLayerInfo) )
	GenesisZoneMapInfoLayer **interior_layers;			AST( NAME(InteriorLayerInfo) )
	GenesisZoneMapInfoLayer **exterior_layers;			AST( NAME(ExteriorLayerInfo) )

	SkyInfoGroup *			do_not_use_this;			AST( NAME(SkyGroup) )//obsolete
} GenesisZoneMapInfo;
extern ParseTable parse_GenesisZoneMapInfo[];
#define TYPE_parse_GenesisZoneMapInfo GenesisZoneMapInfo

typedef struct GenesisBoundingVolume {
	Vec3 center;
	Vec3 extents[2];
	float rot;
} GenesisBoundingVolume;

typedef void (*GenesisInstancePropertiesApplyFn)(const char *zmap_name, GroupDef *def, GenesisInstancedObjectParams *params, GenesisInteractObjectParams *interact, char *challenge_name, GenesisRuntimeErrorContext* debugContext);

#ifndef NO_EDITORS

#define GENESIS_MAPDESC_DICTIONARY "MapDescription"
extern DictionaryHandle g_MapDescDictionary;
extern DictionaryHandle g_EpisodeDictionary;
extern DictionaryHandle g_TagTypeDictionary;

void genesisSetInstancePropertiesFunction(GenesisInstancePropertiesApplyFn function);

void genesisLoadAllLibraries();
void genesisTemplateLoadTagTypeLibrary();
void genesisFixupZoneMapInfo(ZoneMapInfo *info);
void genesisInternVariableDefs(ZoneMapInfo* zminfo, bool removeOtherVars);
void genesisVariableDefNames(const char*** outVariableNames);

// Checks to see if we've encountered any fatal errors
bool genesisStatusFailed(GenesisRuntimeStatus *status);
bool genesisStatusHasErrors(GenesisRuntimeStatus *status, GenesisRuntimeErrorType errorLevel);
bool genesisStageFailed();
bool genesisStageHasErrors(GenesisRuntimeErrorType errorLevel);
void genesisSetStage(GenesisRuntimeStage *stage);
GenesisRuntimeStage *genesisGetCurrentStage();
void genesisSetStageAndAdd(GenesisRuntimeStatus *status, char* stageName, ...);
const GenesisRuntimeError* genesisStatusMostImportantError( const GenesisRuntimeStatus* status );
void genesisSetMap(const char * mapName);

void genesisProceduralObjectEnsureType(GenesisProceduralObjectParams *params);
void genesisProceduralObjectAddVolumeType(GenesisProceduralObjectParams *params, const char *type);
void genesisObjectGetAbsolutePos(GenesisToPlaceObject* obj, Vec3 out);
void genesisProceduralObjectSetEventVolume(GenesisProceduralObjectParams *params);
void genesisProceduralObjectSetActionVolume(GenesisProceduralObjectParams *params);
void genesisProceduralObjectSetOptionalActionVolume(GenesisProceduralObjectParams *params);
void genesisApplyObjectParams(GroupDef *def, GenesisProceduralObjectParams *params);
void genesisPlaceObjects(ZoneMapInfo *zmap_info, GenesisToPlaceState *to_place, GroupDef *root_def); // Destroys object_list EArray
void genesisDestroyToPlaceObject(GenesisToPlaceObject *object);
void genesisDestroyToPlacePatrol(GenesisToPlacePatrol *patrol);
void genesisDestroyToPlacePlatformGroup(GenesisToPlacePlatformGroup *group);

void genesisPopulateWaypointVolumes(GenesisToPlaceState *to_place, GenesisMissionRequirements **mission_reqs);
void genesisPopulateWaypointVolumesAsError(GenesisToPlaceState *to_place, GenesisMissionRequirements **mission_reqs, GenesisRuntimeErrorType errorType);
GenesisToPlaceObject *genesisMakeToPlaceObject(const char *name, GenesisToPlaceObject *parent_object, const F32 *pos, GenesisToPlaceState *to_place);

void genesisLoadDetailKitLibrary();
void genesisLoadBackdropLib();

void genesisGenerateGeometry(int iPartitionIdx, ZoneMap *zmap, GenesisMissionRequirements** mission_reqs, bool preview_mode, bool write_layers);
void genesisGenerateTerrain(int iPartitionIdx, ZoneMap *zmap, bool saveSource );

void genesisRebuildLayers(int iPartitionIdx, ZoneMap *zmap, bool external_map);

int genesisResourceLoad(void);
void genesisSetWLGenerateFunc(GenesisGenerateFunc func, GenesisGenerateMissionsFunc missionFunc, GenesisGenerateEpisodeMissionFunc episodeMissionFunc, GenesisGetSpawnPositionsFunc getSpawnPosFunc);
void genesisReloadLayers(ZoneMap *zmap);

GenesisZoneNodeLayout *genesisGetLastNodeLayout();

// Transmogrify interface
void genesisTransmogrifySolarSystem(U32 seed, U32 detail_seed, GenesisMapDescription *map_desc, GenesisSolSysLayout *vague, GenesisSolSysZoneMap *concrete);
void genesisTransmogrifyInterior(U32 seed, U32 detail_seed, GenesisMapDescription *map_desc, GenesisInteriorLayout *vague_in, GenesisZoneInterior *concrete);
void genesisTransmogrifyExterior(U32 seed, U32 detail_seed, GenesisMapDescription *map_desc, GenesisExteriorLayout *vague_in, GenesisZoneExterior *concrete);

bool genesisTerrainCanSkipUpdate(GenesisMapDescription *old_data, GenesisMapDescription *new_data);
int genesisFindMission( GenesisMapDescription* mapDesc, const char* missionName );

GenesisProceduralObjectParams* genesisCreateStartSpawn( const char* transition );

GenesisProceduralObjectParams* genesisCreateMultiMissionWrapperParams(void);

void genesisSetErrorOnEncounter1( bool state );

// Genesis only interface to worldCellSetEditable.  DO NOT CALL THIS
// UNLESS YOU KNOW WHAT YOU ARE DOING.
void genesisWorldCellSetEditable(void); 

bool genesisIsStarClusterMap(ZoneMapInfo* zminfo);

#endif

GenesisConfig* genesisConfig(void);
GenesisConfigCheckedAttrib* genesisCheckedAttrib( const char* attribName );

ResourceSearchResult* genesisDictionaryItemsFromTag( char* dictionary, char* tags, bool validation_only  );
ResourceSearchResult* genesisDictionaryItemsFromTagList( char* dictionary, const char** tags, const char** append_tags, bool validation_only  );
void* genesisGetRandomItemFromTag(MersenneTable *table, char *dictionary, char *tags, char* append_tags);

GenesisZoneMapRoom *genesisGetRoomByName(GenesisZoneMapRoom **rooms, const char *name);

// Misc interface
void genesisDestroyStateData(void);
void genesisMakeLayers(ZoneMap *zmap);
ZoneMapLayer *genesisMakeSingleLayer(ZoneMap *zmap, const char *layer_name);
void genesisResetLayout();
bool genesisUnfreezeDisabled(void);
bool genesisDataHasTerrain(GenesisZoneMapData *data);
U32 genesisGetBinVersion(ZoneMap *zmap);

/// Error interface
void genesisRaiseError(GenesisRuntimeErrorType type, GenesisRuntimeErrorContext* context, char* message, ...);
void genesisRaiseErrorInternal(GenesisRuntimeErrorType type, const char* dict_name, const char *object_name, char *message, ...);
void genesisRaiseErrorInternalCode(GenesisRuntimeErrorType type, char *message, ...);
void genesisRaiseUGCError(GenesisRuntimeErrorType type, GenesisRuntimeErrorContext* context, const char *message_key, char* message, ...);
void genesisRaiseUGCErrorInField(GenesisRuntimeErrorType type, GenesisRuntimeErrorContext* context, const char* fieldName, const char *message_key, char* message, ...);
void genesisRaiseUGCErrorInFieldExtraText(GenesisRuntimeErrorType type, GenesisRuntimeErrorContext* context, const char* fieldName, const char *message_key, const char* extra_text, char* message, ...);

// Internal only. Use the macros below.
GenesisRuntimeErrorContext* genesisMakeErrorContextDefault_Internal(bool alloc MEM_DBG_PARMS);
GenesisRuntimeErrorContext* genesisMakeErrorContextMap_Internal(bool alloc, const char *map_name MEM_DBG_PARMS);
GenesisRuntimeErrorContext* genesisMakeErrorContextRoom_Internal(bool alloc, const char *room_name, const char *layout_name MEM_DBG_PARMS);
GenesisRuntimeErrorContext* genesisMakeErrorContextRoomDoor_Internal(bool alloc, const char *challenge_name MEM_DBG_PARMS);
GenesisRuntimeErrorContext* genesisMakeErrorContextPath_Internal(bool alloc, const char *path_name, const char *layout_name MEM_DBG_PARMS);
GenesisRuntimeErrorContext* genesisMakeErrorContextChallenge_Internal(bool alloc, const char *challenge_name, const char *mission_name, const char *layout_name MEM_DBG_PARMS);
GenesisRuntimeErrorContext* genesisMakeErrorContextObjective_Internal(bool alloc, const char *objective_name, const char *mission_name MEM_DBG_PARMS);
GenesisRuntimeErrorContext* genesisMakeErrorContextPrompt_Internal(bool alloc, const char *prompt_name, const char *block_name, const char *mission_name, const char *layout_name MEM_DBG_PARMS);
GenesisRuntimeErrorContext* genesisMakeErrorContextLockedDoor_Internal(bool alloc, const char *target_name, const char *mission_name, const char *layout_name MEM_DBG_PARMS);
GenesisRuntimeErrorContext* genesisMakeErrorContextMission_Internal(bool alloc, const char *mission_name MEM_DBG_PARMS);
GenesisRuntimeErrorContext* genesisMakeErrorContextPortal_Internal(bool alloc, const char *portal_name, const char *mission_name, const char *layout_name MEM_DBG_PARMS);
GenesisRuntimeErrorContext* genesisMakeErrorContextLayout_Internal(bool alloc, const char *layout_name MEM_DBG_PARMS);
GenesisRuntimeErrorContext* genesisMakeErrorContextEpisodePart_Internal(bool alloc, const char *episode_part MEM_DBG_PARMS);
GenesisRuntimeErrorContext* genesisMakeErrorContextSolSysDetailObject_Internal(bool alloc, const char *detail_object_name MEM_DBG_PARMS);
GenesisRuntimeErrorContext* genesisMakeErrorContextDictionary_Internal(bool alloc, const char *dict_name, const char *res_name MEM_DBG_PARMS);
GenesisRuntimeErrorContext* genesisMakeErrorContextMapTransition_Internal(bool alloc, const char *objective_name, const char *mission_name MEM_DBG_PARMS);
GenesisRuntimeErrorContext* genesisMakeErrorContextUGCItem_Internal(bool alloc, const char *item_name MEM_DBG_PARMS);

// These allocate a new GenesisRuntimeErrorContext, which must be freed.
#define genesisMakeErrorContextDefault()								genesisMakeErrorContextDefault_Internal(true MEM_DBG_PARMS_INIT)
#define genesisMakeErrorContextMap(map_name)							genesisMakeErrorContextMap_Internal(true, map_name MEM_DBG_PARMS_INIT)
#define genesisMakeErrorContextRoom(room, layout)						genesisMakeErrorContextRoom_Internal(true, room, layout MEM_DBG_PARMS_INIT)
#define genesisMakeErrorContextRoomDoor(challenge)						genesisMakeErrorContextRoomDoor_Internal(true, challenge MEM_DBG_PARMS_INIT)
#define genesisMakeErrorContextPath(path, layout)						genesisMakeErrorContextPath_Internal(true, path, layout MEM_DBG_PARMS_INIT)
#define genesisMakeErrorContextChallenge(challenge, mission, layout)	genesisMakeErrorContextChallenge_Internal(true, challenge, mission, layout MEM_DBG_PARMS_INIT)
#define genesisMakeErrorContextObjective(objective, mission)			genesisMakeErrorContextObjective_Internal(true, objective, mission MEM_DBG_PARMS_INIT)
#define genesisMakeErrorContextPrompt(prompt, block, mission, layout)	genesisMakeErrorContextPrompt_Internal(true, prompt, block, mission, layout MEM_DBG_PARMS_INIT)
#define genesisMakeErrorContextMission(mission)							genesisMakeErrorContextMission_Internal(true, mission MEM_DBG_PARMS_INIT)
#define genesisMakeErrorContextPortal(portal, mission, layout)			genesisMakeErrorContextPortal_Internal(true, portal, mission, layout MEM_DBG_PARMS_INIT)
#define genesisMakeErrorContextLayout(layout)							genesisMakeErrorContextLayout_Internal(true, layout MEM_DBG_PARMS_INIT)
#define genesisMakeErrorContextLockedDoor(target, mission, layout)		genesisMakeErrorContextLockedDoor_Internal(true, target, mission, layout MEM_DBG_PARMS_INIT)
#define genesisMakeErrorContextEpisodePart(part)						genesisMakeErrorContextEpisodePart_Internal(true, part MEM_DBG_PARMS_INIT)
#define genesisMakeErrorContextSolSysDetailObject(object)				genesisMakeErrorContextSolSysDetailObject_Internal(true, object MEM_DBG_PARMS_INIT)
#define genesisMakeErrorContextDictionary(dictionary, resource)			genesisMakeErrorContextDictionary_Internal(true, dictionary, resource MEM_DBG_PARMS_INIT)
#define genesisMakeErrorContextMapTransition(objective, mission)		genesisMakeErrorContextMapTransition_Internal(true, objective, mission MEM_DBG_PARMS_INIT)
#define genesisMakeErrorContextUGCItem(item)							genesisMakeErrorContextUGCItem_Internal(true, item MEM_DBG_PARMS_INIT)

// These return a global variable, to be passed immediately to genesisRaiseError()
#define genesisMakeTempErrorContextDefault()								genesisMakeErrorContextDefault_Internal(false MEM_DBG_PARMS_INIT)
#define genesisMakeTempErrorContextMap(map_name)								genesisMakeErrorContextMap_Internal(false, map_name MEM_DBG_PARMS_INIT)
#define genesisMakeTempErrorContextRoom(room, layout)						genesisMakeErrorContextRoom_Internal(false, room, layout MEM_DBG_PARMS_INIT)
#define genesisMakeTempErrorContextRoomDoor(challenge)						genesisMakeErrorContextRoomDoor_Internal(false, challenge MEM_DBG_PARMS_INIT)
#define genesisMakeTempErrorContextPath(path, layout)						genesisMakeErrorContextPath_Internal(false, path, layout MEM_DBG_PARMS_INIT)
#define genesisMakeTempErrorContextChallenge(challenge, mission, layout)	genesisMakeErrorContextChallenge_Internal(false, challenge, mission, layout MEM_DBG_PARMS_INIT)
#define genesisMakeTempErrorContextObjective(objective, mission)			genesisMakeErrorContextObjective_Internal(false, objective, mission MEM_DBG_PARMS_INIT)
#define genesisMakeTempErrorContextPrompt(prompt, block, mission, layout)	genesisMakeErrorContextPrompt_Internal(false, prompt, block, mission, layout MEM_DBG_PARMS_INIT)
#define genesisMakeTempErrorContextMission(mission)							genesisMakeErrorContextMission_Internal(false, mission MEM_DBG_PARMS_INIT)
#define genesisMakeTempErrorContextPortal(portal, mission, layout)			genesisMakeErrorContextPortal_Internal(false, portal, mission, layout MEM_DBG_PARMS_INIT)
#define genesisMakeTempErrorContextLockedDoor(target, mission, layout)		genesisMakeErrorContextLockedDoor_Internal(false, target, mission, layout MEM_DBG_PARMS_INIT)
#define genesisMakeTempErrorContextLayout(layout)							genesisMakeErrorContextLayout_Internal(false, layout MEM_DBG_PARMS_INIT)
#define genesisMakeTempErrorContextEpisodePart(part)						genesisMakeErrorContextEpisodePart_Internal(false, part MEM_DBG_PARMS_INIT)
#define genesisMakeTempErrorContextSolSysDetailObject(object)				genesisMakeErrorContextSolSysDetailObject_Internal(false, object MEM_DBG_PARMS_INIT)
#define genesisMakeTempErrorContextDictionary(dictionary, resource)			genesisMakeErrorContextDictionary_Internal(false, dictionary, resource MEM_DBG_PARMS_INIT)
#define genesisMakeTempErrorContextMapTransition(objective, mission)		genesisMakeErrorContextMapTransition_Internal(false, objective, mission MEM_DBG_PARMS_INIT)
#define genesisMakeTempErrorContextUGCItem(item)							genesisMakeErrorContextUGCItem_Internal(false, item MEM_DBG_PARMS_INIT)

GenesisRuntimeErrorContext* genesisMakeErrorAutoGen(GenesisRuntimeErrorContext *context);
void genesisErrorPrintContextStr( const GenesisRuntimeErrorContext* context, char* text, size_t text_size );
void genesisErrorPrint( const GenesisRuntimeError* error, char* text, size_t text_size );

// Volume creation utility
GenesisToPlaceObject *genesisCreateChallengeVolume(GenesisToPlaceObject *parent_object, GenesisProceduralObjectParams* volume_params,
								  GroupDef *orig_def, GenesisToPlaceObject *orig_object, GenesisRuntimeErrorContext *parent_context, GenesisObjectVolume *volume);

// Generation functions
GroupDef *genesisInstancePath(GroupDefLib *def_lib, GroupDef *def, char *path);
void genesisApplyActorData(GenesisInstancedChildParams ***ea_actor_data, GroupDef *challenge_def, const Mat4 parent_mat);
void genesisSetInternalObjectLogicalName(GenesisToPlaceObject *object, const char *external_name, const char *internal_name, const char *internal_path, GroupDef *root_def, LogicalGroup *group);

bool genesisGetBoundingVolumeFromPoints(GenesisBoundingVolume* out_boundingVolume, F32 *points);

// Functions that enforce naming conventions
const char *genesisMissionRoomVolumeName(const char *layout_name, const char *room_name, const char *mission_name);
const char *genesisMissionChallengeVolumeName(const char *challenge_name, const char *mission_name);
const char *genesisMissionVolumeName(const char *layout_name, const char *mission_name);

// Fixup functions
TextParserResult fixupGenesisDetailKitLayout(GenesisDetailKitLayout *pDetailKit, enumTextParserFixupType eType, void *pExtraData);
TextParserResult fixupGenesisRoomDetailKitLayout(GenesisRoomDetailKitLayout *pDetailKit, enumTextParserFixupType eType, void *pExtraData);

// Use these functions to catch errors during generate
void genesisGenerateResetError();
bool genesisGeneratedWithErrors();

// Other
void genesisGetBoundsForLayer(GenesisZoneMapData *gen_data, GenesisMapType type, int layer_idx, Vec3 bounds_min, Vec3 bounds_max);

bool genesisLayoutNameExists(GenesisMapDescription *pMapDesc, const char *pcName, void *pExcludeLayout);
char* genesisMakeNewLayoutName(GenesisMapDescription *pMapDesc, GenesisMapType layer_type);

#define UGC_PREFIX_DOOR "Door_"
