#pragma once
GCC_SYSTEM

#if !defined(GENESIS_ALLOW_OLD_HEADERS)
#error Including this file was probably a mistake.  UGC should include the appropriate gslUGC*.h files.
#endif

#include "WorldLibEnums.h"
#include "ReferenceSystem.h"
#include "wlGenesis.h"

typedef struct GenesisDetailKit GenesisDetailKit;
typedef struct GenesisEcosystem GenesisEcosystem;
typedef struct GenesisGeotype GenesisGeotype;
typedef struct GenesisLayoutPath GenesisLayoutPath;
typedef struct GenesisLayoutRoom GenesisLayoutRoom;
typedef struct GenesisGeotypeNodeData GenesisGeotypeNodeData;
typedef struct GenesisNode GenesisNode;
typedef struct GenesisNodeBorder GenesisNodeBorder;
typedef struct GenesisNodeConstraint GenesisNodeConstraint;
typedef struct GenesisNodeConnection GenesisNodeConnection;
typedef struct GenesisNodeConnectionGroup GenesisNodeConnectionGroup;
typedef struct GenesisZoneNodeLayout GenesisZoneNodeLayout;
typedef struct GenesisRoomConstraint GenesisRoomConstraint;
typedef struct GenesisZoneMapPath GenesisZoneMapPath;
typedef struct GenesisZoneMapRoom GenesisZoneMapRoom;
typedef struct GenesisMissionRequirements GenesisMissionRequirements;
typedef struct ParseTable ParseTable;
typedef struct TerrainEditorSourceLayer TerrainEditorSourceLayer;
typedef struct TerrainTaskQueue TerrainTaskQueue;
typedef struct ZoneMapEncounterRoomInfo ZoneMapEncounterRoomInfo;
typedef struct ZoneMapLayer ZoneMapLayer;

#define GENESIS_GEOTYPE_DICTIONARY "GenesisGeotype"

#define DEFAULT_EXTERIOR_PLAYFIELD_SIZE 2048
#define DEFAULT_EXTERIOR_PLAYFIELD_BUFFER 500
#define EXTERIOR_MIN_PLAYFIELD_BUFFER 150

AUTO_STRUCT;
typedef struct GenesisGeotypeRoomData
{
	F32 water_offset;								AST(NAME("OffsetFromWater"))
	F32 max_road_angle;								AST(NAME("MaxRoadAngle"))
	F32 max_mountain_angle;							AST(NAME("MaxNatureAngle"))
	U8 no_mountain : 1;								AST(NAME("NoMountains"))
	U8 no_chasm : 1;								AST(NAME("NoChasms"))
	U8 no_plains : 1;								AST(NAME("NoPlains"))
} GenesisGeotypeRoomData;

AUTO_STRUCT;
typedef struct GenesisGeotypeErodeBrush
{
	char *brush_name;								AST(NAME("Name"))
	int count;										AST(NAME("Count"))
} GenesisGeotypeErodeBrush;
extern ParseTable parse_GenesisGeotypeErodeBrush[];
#define TYPE_parse_GenesisGeotypeErodeBrush GenesisGeotypeErodeBrush

AUTO_STRUCT;
typedef struct GenesisGeotypeNodeData
{
	U8 interp_pow;									AST(NAME("InterpPower"))
	F32 noise_level;								AST(NAME("NoiseLevel"))
	U8 no_water : 1;								AST(NAME("NoWater"))
	F32 max_water_depth;							AST(NAME("MaxWaterDepth"))
	F32 ignore_paths;								AST(NAME("IgnorePaths"))
	GenesisGeotypeErodeBrush **brushes;				AST(NAME("ErodeBrush"))
} GenesisGeotypeNodeData;
extern ParseTable parse_GenesisGeotypeNodeData[];
#define TYPE_parse_GenesisGeotypeNodeData GenesisGeotypeNodeData

AUTO_STRUCT;
typedef struct GenesisGeotype
{
	const char	*filename;							AST(NAME("Filename") CURRENTFILE USERFLAG(TOK_USEROPTIONBIT_1))
	char *name;										AST(NAME("Name") KEY USERFLAG(TOK_USEROPTIONBIT_1))
	GenesisGeotypeRoomData room_data;				AST(NAME("RoomData"))
	GenesisGeotypeNodeData node_data;				AST(NAME("NodeData"))
} GenesisGeotype;
extern ParseTable parse_GenesisGeotype[];
#define TYPE_parse_GenesisGeotype GenesisGeotype

//////////////////////////////////////////////////////////////////////////
// Exterior Layout in a Zone Map
//////////////////////////////////////////////////////////////////////////

// High-level "Room Layout" model
AUTO_STRUCT AST_IGNORE("DetailKit");
typedef struct GenesisZoneExterior
{
	char *layout_name;								AST(NAME("LayoutName"))
	U32 layout_seed;								AST(NAME("LayoutSeed"))
	U32 tmog_version;								AST(NAME("TransmogrifyVersion"))
	GenesisBackdrop *backdrop;						AST(NAME("Backdrop"))
	REF_TO(GenesisGeotype) geotype;					AST(NAME("Geotype"))
	REF_TO(GenesisEcosystem) ecosystem;				AST(NAME("Ecosystem"))
	F32 color_shift;								AST(NAME("ColorShift"))
	Vec2 play_min;									AST(NAME("PlayAreaMin"))
	Vec2 play_max;									AST(NAME("PlayAreaMax"))
	F32 play_buffer;								AST(NAME("PlayAreaBuffer"))
	bool is_vista;									AST(NAME("IsVistaTerrain"))
	GenesisVertDir vert_dir;						AST(NAME("VertDir"))
	GenesisExteriorShape shape;						AST(NAME("Shape"))
	F32 max_road_angle;								AST(NAME("MaxRoadAngle"))
	const char *vista_map;							AST(NAME("VistaMap") POOL_STRING )

	char *start_room;								AST(NAME("StartRoom"))
	char *end_room;									AST(NAME("EndRoom"))

	GenesisZoneMapRoom **rooms;						AST(NAME("Room"))
	GenesisZoneMapPath **paths;						AST(NAME("Path"))
	GenesisRoomConstraint **constraints;			AST(NAME("Constraint"))

	bool no_sharing_detail;							AST(NAME("NoSharingDetail"))
} GenesisZoneExterior;
extern ParseTable parse_GenesisZoneExterior[];
#define TYPE_parse_GenesisZoneExterior GenesisZoneExterior

// Placed 3D nodes, which can be edited
AUTO_STRUCT;
typedef struct GenesisZoneNodeLayout
{
	char *layout_name;								AST(NAME("LayoutName"))
	U32 tmog_version;								AST(NAME("TransmogrifyVersion"))
	U32 seed;										AST(NAME("Seed"))
	bool is_vista;									AST(NAME("IsVistaTerrain"))
	GenesisBackdrop *backdrop;						AST(NAME("Backdrop"))
	REF_TO(GenesisGeotype) geotype;					AST(NAME("Geotype"))
	REF_TO(GenesisEcosystem) ecosystem;				AST(NAME("Ecosystem"))

	F32 color_shift;								AST(NAME("ColorShift"))
	Vec2 play_min;									AST(NAME("PlayAreaMin"))
	Vec2 play_max;									AST(NAME("PlayAreaMax"))
	Vec2 play_heights;								AST(NAME("PlayHeights"))
	F32 play_buffer;								AST(NAME("PlayAreaBuffer"))
	const char *vista_map;							AST(NAME("VistaMap") POOL_STRING )

	ZoneMapEncounterRoomInfo **room_partitions;						NO_AST

	GenesisNode **nodes;							AST(NAME("GenesisNode"))
	GenesisNodeConnectionGroup **connection_groups;	AST(NAME("GenesisNodeConnectionGroup"))
	GenesisNodeBorder **node_borders;				AST(NAME("NodeBorder"))

	bool no_sharing_detail;							AST(NAME("NoSharingDetail"))
} GenesisZoneNodeLayout;
extern ParseTable parse_GenesisZoneNodeLayout[];
#define TYPE_parse_GenesisZoneNodeLayout GenesisZoneNodeLayout

//////////////////////////////////////////////////////////////////////////
// "Just Written" Exterior Layout
//////////////////////////////////////////////////////////////////////////

AUTO_STRUCT;
typedef struct GenesisExteriorLayoutInfo
{
	GenesisTagOrName geotype_specifier;				AST(NAME("GeotypeSpecifier"))
	char **geotype_tag_list;						AST(NAME("GeotypeTags2"))
	char *old_geotype_tags;							AST(NAME("GeotypeTags"))
	REF_TO(GenesisGeotype) geotype;					AST(NAME("Geotype"))

	GenesisTagOrName ecosystem_specifier;			AST(NAME("EcosystemSpecifier"))
	char **ecosystem_tag_list;						AST(NAME("EcosystemTags2"))
	char *old_ecosystem_tags;						AST(NAME("EcosystemTags"))
	REF_TO(GenesisEcosystem) ecosystem;				AST(NAME("Ecosystem"))

	F32 color_shift;								AST(NAME("ColorShift"))
} GenesisExteriorLayoutInfo;
extern ParseTable parse_GenesisExteriorLayoutInfo[];
#define TYPE_parse_GenesisExteriorLayoutInfo GenesisExteriorLayoutInfo

AUTO_STRUCT AST_IGNORE_STRUCT("LightDetail") AST_FIXUPFUNC(fixupGenesisExteriorLayout);
typedef struct GenesisExteriorLayout
{
	char *name;										AST(NAME("Name"))
	GenesisExteriorLayoutInfo info;					AST(EMBEDDED_FLAT) //For backwards compatibility it is embedded flat

	GenesisTemplateOrCustom layout_info_specifier;	AST(NAME("LayoutInfoSpecifier"))
	REF_TO(GenesisMapDescExteriorLayoutTemplate) ext_template; AST(NAME("ExteriorLayoutInfoTemplate"))

	Vec2 play_min;									AST(NAME("PlayAreaMin"))
	Vec2 play_max;									AST(NAME("PlayAreaMax"))
	F32 play_buffer;								AST(NAME("PlayAreaBuffer"))

	GenesisVertDir vert_dir;						AST(NAME("VertDir"))
	GenesisExteriorShape shape;						AST(NAME("Shape"))
	F32 max_road_angle;								AST(NAME("MaxRoadAngle"))
	S32 min_side_trail_length;						AST(NAME("MinSideTrailLength"))		// In grid units
	S32 max_side_trail_length;						AST(NAME("MaxSideTrailLength"))		// In grid units

	bool is_vista;									AST(NAME("IsVistaTerrain"))

	GenesisLayoutRoom **rooms;						AST(NAME("Room"))
	GenesisLayoutPath **paths;						AST(NAME("Path"))

	// Detail set to use by default
	GenesisDetailKitLayout detail_kit_1;			AST(EMBEDDED_FLAT) //For backwards compatibility it is embedded flat
	GenesisDetailKitLayout detail_kit_2;			AST(NAME("Detail2"))

	GenesisLayoutCommonData common_data;			AST(NAME("CommonData"))
} GenesisExteriorLayout;
extern ParseTable parse_GenesisExteriorLayout[];
#define TYPE_parse_GenesisExteriorLayout GenesisExteriorLayout

#ifndef NO_EDITORS

void genesisLoadGeotypeLibrary();
GenesisZoneNodeLayout* genesisExteriorMoveRoomsToNodes(GenesisZoneExterior *layout, U32 seed, U32 detail_seed, bool run_silent, bool make_vista, bool add_nature);
// node_layout_in is either destroyed or returned in the following function:
GenesisZoneNodeLayout* genesisExpandAndPlaceNodes(GenesisZoneNodeLayout **in_node_layout, GenesisNodeConstraint **input_constraints, 
											  GenesisGeotype *geo_data, Vec2 full_play_size, Vec2 full_play_offset, F32 play_buffer,
											  bool run_silent, bool make_vista, bool add_nature);

void genesisExteriorPaintTerrain(int iPartitionIdx, ZoneMap *zmap, TerrainEditorSourceLayer *source_layer, TerrainTaskQueue *queue, int flags, bool in_editor);
void genesisExteriorPopulateLayer(int iPartitionIdx, ZoneMap *zmap, GenesisMissionRequirements **mission_reqs, ZoneMapLayer *layer);
void genesisExteriorUpdate(ZoneMap *zmap);
void genesisExteriorGetBlockExtents(GenesisZoneMapData *data, IVec2 min_block, IVec2 max_block);
void genesisExteriorCreateMissionVolumes(GenesisToPlaceState *to_place, Vec3 min, Vec3 max, const char *layout_name, GenesisMissionRequirements **mission_reqs, GenesisBackdrop *backdrop);

#endif
