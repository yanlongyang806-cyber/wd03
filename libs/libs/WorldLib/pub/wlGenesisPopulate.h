#pragma once
GCC_SYSTEM

#if !defined(GENESIS_ALLOW_OLD_HEADERS)
#error Including this file was probably a mistake.  UGC should include the appropriate gslUGC*.h files.
#endif

#include "wlGenesis.h"

#define GENESIS_DETAIL_DICTIONARY "GenesisDetailKit"

typedef struct GenesisDetailKit GenesisDetailKit;
typedef struct GenesisObject GenesisObject;
typedef struct GenesisRoomMission GenesisRoomMission;
typedef struct ExclusionVolumeGroup ExclusionVolumeGroup;
typedef struct ExclusionObject ExclusionObject;
typedef struct GenesisToPlaceObject GenesisToPlaceObject;

AUTO_STRUCT;
typedef struct GenesisDetail
{
	GroupDefRef obj;								AST(EMBEDDED_FLAT)
	F32 priority_scale;								AST(NAME("Priority") DEFAULT(1))
	F32 density;									AST(NAME("Density"))
	GenesisPlacementParams params;					AST(EMBEDDED_FLAT)

	GenesisToPlaceObject *parent_group;				NO_AST
	GenesisDetailKitAndDensity *parent_kit;			NO_AST
	GroupDef *def;									NO_AST
	F32 priority;									NO_AST
	int placed_in_room;								NO_AST
} GenesisDetail;

// If more than one tag is specified, weights have to be either non-zero or all zero, otherwise we throw up an error.
AUTO_STRUCT;
typedef struct GenesisDetailKit
{
	char *name;										AST(NAME("Name") STRUCTPARAM KEY)
	const char *filename;							AST(CURRENTFILE)
	char *tags;										AST(NAME("Tags"))
	
	GenesisDetail **details;						AST(NAME("Detail"))
	U32 default_density;							AST(NAME("DefaultDensity"))	// Number of objects per 10000 sq. feet

	GenesisDetail **path_details;					AST(NAME("PathDetail"))
	U32 path_default_density;						AST(NAME("PathDefaultDensity"))
} GenesisDetailKit;
extern ParseTable parse_GenesisDetailKit[];
#define TYPE_parse_GenesisDetailKit GenesisDetailKit

typedef struct GenesisPopulateDoor
{
	Vec3 point;
	bool entrance;
	bool exit;
	const char *dest_name;

	// Internal
	S32 gridx, gridz;
} GenesisPopulateDoor;

typedef struct GenesisPopulateArea
{
	char* prefab_library_piece;
	// The "anchor point" for the room, if it is a one-off --
	// different from min / max because those include some padding.
	Mat4 room_origin;
	
	Vec3 min;
	Vec3 max;
	F32 radius; // If zero, then use a square area. Otherwise use 2D distance from center
	F32 padding_used; //What padding was used to set the min and max
	GenesisPopulateDoor **doors;
	ExclusionObject **excluders;
	U8 no_details_or_paths : 1;				// Don't place details or ensure walkable paths
	U8 volumized : 1;						// If true, then a given placement is only valid if it lands on a platform.
} GenesisPopulateArea;

#ifndef NO_EDITORS

bool genesisDetailKitValidate(GenesisDetailKit *detail_kit, const char *file_name_in);

bool genesisPopulateArea(int iPartitionIdx, int debug_index, GenesisRuntimeErrorContext* room_context,
						 GenesisToPlaceState *to_place, GenesisPopulateArea *area, U32 seed,
						 GenesisToPlaceObject *shared_parent, GenesisToPlaceObject **mission_parents,
						 GenesisMissionRequirements **mission_reqs, GenesisRoomMission **room_missions,
						 GenesisObject **door_caps,
						 GenesisDetailKitAndDensity *detail_kits[3], bool place_volume_blobs, 
						 bool side_trail, bool in_path, bool disable_patrol, bool no_sharing, float low_res_spacing,
						 bool position_override);

GenesisInstancedChildParams *genesisChildParamsToInstancedChildParams(GenesisInstancedObjectParams *params, GenesisPlacementChildParams *child, int idx, const char* challengeName);

#endif
