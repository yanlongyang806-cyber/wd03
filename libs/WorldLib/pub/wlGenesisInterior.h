#pragma once
GCC_SYSTEM

#if !defined(GENESIS_ALLOW_OLD_HEADERS)
#error Including this file was probably a mistake.  UGC should include the appropriate gslUGC*.h files.
#endif

#include "referencesystem.h"
#include "WorldLibEnums.h"
#include "wlGenesis.h"

#define GENESIS_INTERIORS_DICTIONARY "GenesisInteriorKit"

typedef struct ZoneMap ZoneMap;
typedef struct ZoneMapLayer ZoneMapLayer;
typedef struct GenesisInteriorTag GenesisInteriorTag;
typedef struct TerrainEditorSource TerrainEditorSource;
typedef struct GenesisToPlaceObject GenesisToPlaceObject;
typedef struct GenesisZoneMapRoom GenesisZoneMapRoom;
typedef struct GenesisZoneMapPath GenesisZoneMapPath;
typedef struct GenesisLayoutRoom GenesisLayoutRoom;
typedef struct GenesisLayoutPath GenesisLayoutPath;
typedef struct GenesisRoomConstraint GenesisRoomConstraint;
typedef struct ParseTable ParseTable;
typedef struct GenesisDetailKit GenesisDetailKit;
typedef struct GenesisInteriorPatternInternal GenesisInteriorPatternInternal;
typedef struct ExclusionVolumeGroup ExclusionVolumeGroup;


//////////////////////////////////////////////////////////////////////////
// Interior Kits
//////////////////////////////////////////////////////////////////////////

AUTO_STRUCT;
typedef struct GenesisInteriorElementObject
{
	char *							geometry;		AST(NAME("Geo"))
	Vec3							offset;			AST(NAME("Offset"))
	F32								rotation;		AST(NAME("Rotation"))
	int								geometry_uid;	NO_AST
} GenesisInteriorElementObject;
extern ParseTable parse_GenesisInteriorElementObject[];
#define TYPE_parse_GenesisInteriorElementObject GenesisInteriorElementObject

AUTO_STRUCT;
typedef struct GenesisInteriorPatternCoord
{
	IVec2							coord;			AST(STRUCTPARAM)
} GenesisInteriorPatternCoord;
extern ParseTable parse_GenesisInteriorPatternCoord[];
#define TYPE_parse_GenesisInteriorPatternCoord GenesisInteriorPatternCoord

AUTO_STRUCT;
typedef struct GenesisInteriorPattern
{
	const char *					pattern_name;	AST(STRUCTPARAM POOL_STRING)
	GenesisInteriorPatternCoord **	coords;			AST(NAME("Pos"))
	bool							any_coord;		AST(NAME("AnyPos"))
	GenesisInteriorPatternInternal *internal;		NO_AST
} GenesisInteriorPattern;

AUTO_STRUCT;
typedef struct GenesisInteriorElement
{
	char *							name;			AST(NAME("Name"))
	char *							tag_name;		AST(NAME("Tag"))
	GenesisInteriorElementObject	primary_object;	AST(EMBEDDED_FLAT)
	GenesisInteriorElementObject **	additional_objects;	AST(NAME("Geometry"))
	GenesisInteriorPattern **		patterns;		AST(NAME("Pattern"))

	GenesisInteriorTag *			tag;			NO_AST
} GenesisInteriorElement;
extern ParseTable parse_GenesisInteriorElement[];
#define TYPE_parse_GenesisInteriorElement GenesisInteriorElement

AUTO_ENUM;
typedef enum GenesisInteriorConnectionSide
{
	CONNECTION_SIDE_LEFT  = 1<<0,
	CONNECTION_SIDE_BACK  = 1<<1,
	CONNECTION_SIDE_RIGHT = 1<<2,
	CONNECTION_SIDE_FRONT = 1<<3,
} GenesisInteriorConnectionSide;

AUTO_ENUM;
typedef enum GenesisInteriorRotationFlags
{
	CONNECTION_ROTATION_NOROTATE  = 1<<0,
	CONNECTION_ROTATION_ROTATE90  = 1<<1,
	CONNECTION_ROTATION_ROTATE180 = 1<<2,
	CONNECTION_ROTATION_ROTATE270 = 1<<3, 
} GenesisInteriorRotationFlags;

AUTO_STRUCT;
typedef struct GenesisInteriorTagRelation
{
	char *							tag_name;		AST(STRUCTPARAM)
	GenesisInteriorConnectionSide	allow_flags;	AST(NAME("Allow") FLAGS)

	GenesisInteriorTag *			tag;			NO_AST
} GenesisInteriorTagRelation;
extern ParseTable parse_GenesisInteriorTagRelation[];
#define TYPE_parse_GenesisInteriorTagRelation GenesisInteriorTagRelation

AUTO_STRUCT;
typedef struct GenesisInteriorTagConnection
{
	GenesisInteriorConnectionSide	sides;			AST(STRUCTPARAM FLAGS)
	GenesisInteriorTagRelation **	relations;		AST(NAME("AllowTag"))
} GenesisInteriorTagConnection;
extern ParseTable parse_GenesisInteriorTagConnection[];
#define TYPE_parse_GenesisInteriorTagConnection GenesisInteriorTagConnection

AUTO_ENUM;
typedef enum GenesisInteriorStep
{
	INTERIOR_STEP_DOWN = -1,
	INTERIOR_STEP_NONE = 0,
	INTERIOR_STEP_UP = 1,
	INTERIOR_STEP_ANY
} GenesisInteriorStep;
 
AUTO_STRUCT;
typedef struct GenesisInteriorTag
{
	char *							name;			AST(NAME("Name"))
	char *							parent_name;	AST(NAME("Inherits"))
	GenesisInteriorTagConnection **	connections;	AST(NAME("Connection"))
	char **							detail_names;	AST(NAME("DetailObjects"))
	char *							light_name;		AST(NAME("Light"))
	GenesisInteriorStep				left_step;		AST(NAME("LeftStep"))
	GenesisInteriorStep				right_step;		AST(NAME("RightStep"))
	GenesisInteriorStep				front_step;		AST(NAME("FrontStep"))
	GenesisInteriorStep				back_step;		AST(NAME("BackStep"))
	bool					fallback_to_parent;		AST(NAME("FallbackToParent"))

	GenesisInteriorTag **			details;		NO_AST
	GenesisInteriorTag *			parent;			NO_AST
	GenesisInteriorTag *			light;			NO_AST
} GenesisInteriorTag;
extern ParseTable parse_GenesisInteriorTag[];
#define TYPE_parse_GenesisInteriorTag GenesisInteriorTag

AUTO_STRUCT AST_IGNORE_STRUCT(UGCProperties);
typedef struct GenesisInteriorKit
{
	const char *					filename;		AST(NAME("Filename") CURRENTFILE)
	char *							name;			AST(NAME("Name") KEY)
	char *							tags;			AST(NAME("Tags"))

	F32								spacing;		AST(NAME("KitElementSize"))			// Infrastructure Kit only
	F32								floor_height;	AST(NAME("KitFloorHeight"))			// Infrastructure Kit only
	F32								room_padding;	AST(NAME("RoomPadding"))			// Infrastructure Kit only
	F32								light_top;		AST(NAME("LightTop"))			
	F32								floor_bottom;	AST(NAME("FloorBottom"))			// Used for lights

	GenesisSoundInfo *				sound_info;		AST(NAME("SoundInfo"))

	char *							key_light;		AST(NAME("KeyLight"))				// Light Kit only
	GenesisDetailKit *				light_details;	AST(NAME("LightDetailKit"))			// Light Kit only

	//Flags to turn off things that make lighting work better.
	U8								compact_junct;	AST(NAME("CompactJunct"))

	U8								straight_door_only; AST(NAME("HallwayStraightDoorOnly"))
	U8								no_occlusion;	AST(NAME("NoOcclusionVolumes"))

	GenesisInteriorElement **		elements;		AST(NAME("Element"))
	GenesisInteriorTag **			interior_tags;	AST(NAME("InteriorTag"))

	GenesisDetailKitAndDensity		light_dummy;	NO_AST
} GenesisInteriorKit;
extern ParseTable parse_GenesisInteriorKit[];
#define TYPE_parse_GenesisInteriorKit GenesisInteriorKit

AUTO_ENUM;
typedef enum GenesisInteriorLayer
{
	GENESIS_INTERIOR_ANY_NO_ERROR = 0, // For legacy purposes
	GENESIS_INTERIOR_ANY,
	GENESIS_INTERIOR_FLOOR,
	GENESIS_INTERIOR_WALL,
	GENESIS_INTERIOR_CEILING,
	GENESIS_INTERIOR_HALLWAY,
	GENESIS_INTERIOR_CAP
} GenesisInteriorLayer;

AUTO_STRUCT;
typedef struct GenesisInteriorReplace
{
	char *							old_tag;		AST(NAME("OldTag"))
	char *							new_tag;		AST(NAME("NewTag"))
	char *							new_detail_tag;	AST(NAME("NewDetailTag"))
	char *							new_light_tag;	AST(NAME("NewLightTag"))
	char *							object;			AST(NAME("SpecificObject"))
	bool							is_door;		AST(NAME("IsDoor"))
	GenesisInteriorLayer			replace_layer;	AST(NAME("Layer"))
	S32								override_x;		AST(NAME("OverrideX")) // Only used in specified mode
	S32								override_z;		AST(NAME("OverrideZ")) // Only used in specified mode

	AST_STOP
	// Internal
	int								final_x;
	int								final_z;
} GenesisInteriorReplace;
extern ParseTable parse_GenesisInteriorReplace[];
#define TYPE_parse_GenesisInteriorReplace GenesisInteriorReplace

//////////////////////////////////////////////////////////////////////////
// Interior Layout in a Zone Map
//////////////////////////////////////////////////////////////////////////

AUTO_STRUCT AST_IGNORE("DetailKit");
typedef struct GenesisZoneInterior
{
	char *layout_name;								AST(NAME("LayoutName"))
	U32 layout_seed;								AST(NAME("LayoutSeed"))
	U32 tmog_version;								AST(NAME("TransmogrifyVersion"))

	GenesisBackdrop *backdrop;						AST(NAME("Backdrop"))
	REF_TO(GenesisInteriorKit) room_kit;			AST(NAME("InteriorKit"))
	REF_TO(GenesisInteriorKit) light_kit;			AST(NAME("LightKit"))

	GenesisZoneMapRoom **rooms;						AST(NAME("Room"))
	GenesisZoneMapPath **paths;						AST(NAME("Path"))

	bool override_positions;						AST(NAME("OverridePositions"))

	GenesisVertDir vert_dir;						AST(NAME("VertDir"))

	bool no_sharing_detail;							AST(NAME("NoSharingDetail"))

} GenesisZoneInterior;
extern ParseTable parse_GenesisZoneInterior[];
#define TYPE_parse_GenesisZoneInterior GenesisZoneInterior

//////////////////////////////////////////////////////////////////////////
// "Just Written" Interior Layout
//////////////////////////////////////////////////////////////////////////

AUTO_STRUCT;
typedef struct GenesisInteriorLayoutInfo
{
	GenesisTagOrName room_kit_specifier;			AST(NAME("InteriorKitSpecifier"))
	char **room_kit_tag_list;						AST(NAME("InteriorKitTags2"))
	char *old_room_kit_tags;						AST(NAME("InteriorKitTags"))
	REF_TO(GenesisInteriorKit) room_kit;			AST(NAME("InteriorKit"))

	GenesisTagOrName light_kit_specifier;			AST(NAME("LightKitSpecifier"))
	char **light_kit_tag_list;						AST(NAME("LightKitTags2"))
	char *old_light_kit_tags;						AST(NAME("LightKitTags"))
	REF_TO(GenesisInteriorKit) light_kit;			AST(NAME("LightKit"))	
} GenesisInteriorLayoutInfo;
extern ParseTable parse_GenesisInteriorLayoutInfo[];
#define TYPE_parse_GenesisInteriorLayoutInfo GenesisInteriorLayoutInfo

AUTO_STRUCT AST_IGNORE("LightDensity") AST_IGNORE_STRUCT("LightDetail") AST_FIXUPFUNC(fixupGenesisInteriorLayout);
typedef struct GenesisInteriorLayout
{
	char *name;										AST(NAME("Name"))
	GenesisInteriorLayoutInfo info;					AST(EMBEDDED_FLAT) //For backwards compatibility it is embedded flat

	GenesisTemplateOrCustom layout_info_specifier;	AST(NAME("LayoutInfoSpecifier"))
	REF_TO(GenesisMapDescInteriorLayoutTemplate) int_template; AST(NAME("InteriorLayoutInfoTemplate"))

	GenesisLayoutRoom **rooms;						AST(NAME("Room"))
	GenesisLayoutPath **paths;						AST(NAME("Path"))

	// Detail kit to use by default
	GenesisDetailKitLayout detail_kit_1;			AST(EMBEDDED_FLAT) //For backwards compatibility it is embedded flat
	GenesisDetailKitLayout detail_kit_2;			AST(NAME("Detail2"))
	GenesisVertDir vert_dir;						AST(NAME("VertDir"))

	GenesisLayoutCommonData common_data;			AST(NAME("CommonData"))
} GenesisInteriorLayout;

extern ParseTable parse_GenesisInteriorLayout[];
#define TYPE_parse_GenesisInteriorLayout GenesisInteriorLayout

#ifndef NO_EDITORS

void genesisLoadInteriorKitLibrary();
GenesisInteriorKit *genesisInteriorKitCreate();
bool genesisInteriorKitAppend(GenesisInteriorKit *kit, const char *name);
void genesisInteriorKitDestroy(GenesisInteriorKit *kit);
GenesisZoneInterior *genesisGetInteriorByIndex(GenesisZoneMapData *gen_data, int idx);
void genesisInteriorPopulateLayer(int iPartitionIdx, ZoneMapInfo *zmap_info, GenesisViewType view_type, GenesisMissionRequirements **mission_reqs, ZoneMapLayer *layer, GenesisZoneInterior *interior, int layer_idx);

// Utility functions
void genesisGenerateHallwayFromControlPoints(int *in_points, int **hallway_points);
int genesisInteriorCalculateHallwayMaxDelta(int *points);

#endif
