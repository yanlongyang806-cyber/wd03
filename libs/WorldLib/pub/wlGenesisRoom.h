#pragma once
GCC_SYSTEM

#if !defined(GENESIS_ALLOW_OLD_HEADERS)
#error Including this file was probably a mistake.  UGC should include the appropriate gslUGC*.h files.
#endif

#include "wlGenesis.h"

typedef struct GenesisDetailKit GenesisDetailKit;
typedef struct GenesisInteriorReplace GenesisInteriorReplace;
typedef struct GenesisObject GenesisObject;
typedef struct GenesisPatrolObject GenesisPatrolObject;
typedef struct ParseTable ParseTable;
typedef struct WorldActionVolumeProperties WorldActionVolumeProperties;

//////////////////////////////////////////////////////////////////////////
// RoomDef Library
//////////////////////////////////////////////////////////////////////////

#define GENESIS_ROOM_DEF_DICTIONARY "GenesisRoomDef"

// A connection point on a Room for a Path
AUTO_STRUCT;
typedef struct GenesisRoomDoor
{
	const char *name;						AST(NAME("Name") POOL_STRING) // Must be unique per room
	S32 x;									AST(NAME("GridX"))			// In horizontal grid units
	S32 y;									AST(NAME("GridY"))			// In vertical grid units (zero default)
	S32 z;									AST(NAME("GridZ"))			// In horizontal grid units
	S32 rotation;							AST(NAME("Rotation"))		// 0-4, in 90 degree increments
	bool always_open;						AST(NAME("AlwaysOpen"))		// If true, don't put a blocker geo in
} GenesisRoomDoor;
extern ParseTable parse_GenesisRoomDoor[];
#define TYPE_parse_GenesisRoomDoor GenesisRoomDoor

AUTO_ENUM;
typedef enum GenesisRoomRectAreaType
{
	GENESIS_ROOM_RECT_SUBTRACT,											// Walled-off or unpaved
	GENESIS_ROOM_RECT_RAISED_FLOOR,
	GENESIS_ROOM_RECT_LOWERED_FLOOR,
	GENESIS_ROOM_RECT_RAISED_CIELING,									// Interiors only
	GENESIS_ROOM_RECT_LOWERED_CIELING,									// Interiors only
} GenesisRoomRectAreaType;

// A portion of a room or clearing that has special properties
AUTO_STRUCT;
typedef struct GenesisRoomRectArea
{
	GenesisRoomRectAreaType type;			AST(NAME("Type"))
	S32 x;									AST(NAME("GridX"))			// In grid units
	S32 z;									AST(NAME("GridZ"))			// In grid units
	S32 width;								AST(NAME("Width"))			// In grid units
	S32 depth;								AST(NAME("Depth"))			// In grid units
} GenesisRoomRectArea;

// The definition of an Interior room or an Exterior clearing
AUTO_STRUCT AST_IGNORE_STRUCT("PriorityObject");
typedef struct GenesisRoomDef
{
	const char *filename;					AST(NAME("Filename") CURRENTFILE USERFLAG(TOK_USEROPTIONBIT_1))
	char *name;								AST(NAME("Name") KEY)
	char *tags;								AST(NAME("Tags"))
	S32 width;								AST(NAME("Width"))			// In grid units
	S32 depth;								AST(NAME("Depth"))			// In grid units
	
	GenesisRoomDoor **doors;				AST(NAME("Door"))
	GenesisRoomRectArea **areas;			AST(NAME("Area"))

	char *library_piece;					AST(NAME("CustomObject"))	// One-off. If this is set, no other objects are generated
	U8 use_proj_lights : 1;					AST(NAME("UseProjLights"))	// Forces projector light placement even if this is a one-off
	U8 no_details_or_paths : 1;				AST(NAME("NoDetailsOrPaths"))	// Don't place details or ensure walkable paths
	U8 volumized : 1;						AST(NAME("Volumized"))			// If true, then a given placement is only valid if it lands on a platform.
} GenesisRoomDef;
extern ParseTable parse_GenesisRoomDef[];
#define TYPE_parse_GenesisRoomDef GenesisRoomDef

//////////////////////////////////////////////////////////////////////////
// PathDef Library
//////////////////////////////////////////////////////////////////////////

#define GENESIS_PATH_DEF_DICTIONARY "GenesisPathDef"

// The definition of an Interior hallway or an Exterior path
AUTO_STRUCT;
typedef struct GenesisPathDef
{
	const char *filename;					AST(NAME("Filename") CURRENTFILE USERFLAG(TOK_USEROPTIONBIT_1))
	char *name;								AST(NAME("Name") KEY)
	char *tags;								AST(NAME("Tags"))
	S32 width;								AST(NAME("Width"))			// In grid units
	S32 min_length;							AST(NAME("MinLength"))		// In grid units
	S32 max_length;							AST(NAME("MaxLength"))		// In grid units
	F32 windiness;							AST(NAME("Windiness"))		// 0.0 - 1.0
	U8 no_details_or_paths : 1;				AST(NAME("NoDetailsOrPaths"))	// Don't place details or ensure walkable paths
} GenesisPathDef;
extern ParseTable parse_GenesisPathDef[];
#define TYPE_parse_GenesisPathDef GenesisPathDef

//////////////////////////////////////////////////////////////////////////
// Layout embedded in a Zone Map
//////////////////////////////////////////////////////////////////////////

// Defines a set of objects to be placed only for the specific mission. The objects are ordered,
// and objects at the same index in all the missions for a RoomDef have to share a single space
AUTO_STRUCT AST_IGNORE("DetailKit");
typedef struct GenesisRoomMission
{
	int mission_uid;						AST(NAME("MissionUID"))
	char *mission_name;						AST(NAME("MissionName"))
	GenesisObject **objects;				AST(NAME("Object"))			// Ordered list
	GenesisPatrolObject **patrols;			AST(NAME("Patrol"))
	GenesisInteriorReplace **replaces;		AST(NAME("ReplaceObject"))	// Replace pieces of the kit with a given override
	bool has_portal;						AST(NAME("HasPortal"))
} GenesisRoomMission;
extern ParseTable parse_GenesisRoomMission[];
#define TYPE_parse_GenesisRoomMission GenesisRoomMission

AUTO_STRUCT AST_IGNORE("Seed") AST_IGNORE("LightDensity") AST_IGNORE_STRUCT("LightDetail");
typedef struct GenesisZoneMapRoom
{
	GenesisRoomDef room;						AST(EMBEDDED_FLAT)
	U32 detail_seed;							AST(NAME("DetailSeed"))		// Rooms get unique seeds, for stability

	char *pcVisibleName;						AST(NAME("VisibleName"))
	U32 iPosX;									AST(NAME("PosX"))
	S32 iPosY;									AST(NAME("PosY"))
	U32 iPosZ;									AST(NAME("PosZ"))
	U32 iRot;									AST(NAME("Rotation"))		// From 0-3, counterclockwise rotation

	GenesisObject **static_objects;				AST(NAME("StaticObject"))	// One set of objects, shared by all missions
	GenesisDetailKitAndDensity detail_kit_1;	AST(EMBEDDED_FLAT)			
	GenesisDetailKitAndDensity detail_kit_2;	AST(NAME("Detail2"))			
	U8 side_trail : 1;							AST(NAME("SideTrail"))
	U8 hallway_room : 1;						AST(NAME("HallwayRoom"))	// Is this a 1x1 room that should act like a hall
	U8 off_map : 1;								AST(NAME("OffMap"))
	U8 is_prefab : 1;							AST(NAME("Prefab"))			// This is a pre-fab (non-user-editable) room

	GenesisRoomMission **missions;				AST(NAME("Mission"))		// Per-mission object placement data
	int path_count;								NO_AST
	bool is_start_end;							NO_AST
} GenesisZoneMapRoom;
extern ParseTable parse_GenesisZoneMapRoom[];
#define TYPE_parse_GenesisZoneMapRoom GenesisZoneMapRoom

AUTO_STRUCT AST_IGNORE("Seed") AST_IGNORE_STRUCT("LightDetail");
typedef struct GenesisZoneMapPath
{
	GenesisPathDef path;						AST(EMBEDDED_FLAT)
	U32 detail_seed;							AST(NAME("DetailSeed"))		// Paths get unique seeds, for stability

	char *pcVisibleName;						AST(NAME("VisibleName"))

	char **start_rooms;							AST(NAME("StartRoom"))
	char **end_rooms;							AST(NAME("EndRoom"))

	S32 *control_points;						AST(NAME("ControlPoints"))
	int *start_doors;							AST(NAME("StartDoor"))
	int *end_doors;								AST(NAME("EndDoor"))

	GenesisDetailKitAndDensity detail_kit_1;	AST(EMBEDDED_FLAT)			
	GenesisDetailKitAndDensity detail_kit_2;	AST(NAME("Detail2"))			
	U8 side_trail : 1;							AST(NAME("SideTrail"))

	GenesisRoomMission **missions;				AST(NAME("Mission"))		// Per-mission object placement data
} GenesisZoneMapPath;
extern ParseTable parse_GenesisZoneMapPath[];
#define TYPE_parse_GenesisZoneMapPath GenesisZoneMapPath

//////////////////////////////////////////////////////////////////////////
// "Just Written" Layout
//////////////////////////////////////////////////////////////////////////

AUTO_STRUCT AST_IGNORE(LightDensityOverride) AST_IGNORE(LightDensity) AST_IGNORE_STRUCT("LightDetail") AST_FIXUPFUNC(fixupGenesisLayoutRoom);
typedef struct GenesisLayoutRoom
{
	char *name;									AST(STRUCTPARAM)
	U32 detail_seed;							AST(NAME("DetailSeed"))		// Rooms get unique seeds, for stability

	GenesisTagOrName room_specifier;			AST(NAME("RoomSpecifier"))
	char **room_tag_list;						AST(NAME("RoomTags2"))
	char *old_room_tags;						AST(NAME("RoomTags"))
	REF_TO(GenesisRoomDef) room;				AST(NAME("RoomDef"))
	U8 off_map : 1;								AST(NAME("OffMap"))

	GenesisRoomDetailKitLayout detail_kit_1;	AST(EMBEDDED_FLAT)
	GenesisRoomDetailKitLayout detail_kit_2;	AST(NAME("Detail2"))
} GenesisLayoutRoom;
extern ParseTable parse_GenesisLayoutRoom[];
#define TYPE_parse_GenesisLayoutRoom GenesisLayoutRoom

AUTO_STRUCT AST_IGNORE_STRUCT("LightDetail") AST_FIXUPFUNC(fixupGenesisLayoutPath);
typedef struct GenesisLayoutPath
{
	char *name;									AST(STRUCTPARAM)
	U32 detail_seed;							AST(NAME("DetailSeed"))		// Rooms get unique seeds, for stability

	GenesisTagOrName path_specifier;			AST(NAME("PathSpecifier"))
	char **path_tag_list;						AST(NAME("PathTags2"))
	char *old_path_tags;						AST(NAME("PathTags"))
	REF_TO(GenesisPathDef) path;				AST(NAME("PathDef"))

	GenesisRoomDetailKitLayout detail_kit_1;	AST(EMBEDDED_FLAT)
	GenesisRoomDetailKitLayout detail_kit_2;	AST(NAME("Detail2"))
	S32 min_length;								AST(NAME("MinLength"))		// In grid units
	S32 max_length;								AST(NAME("MaxLength"))		// In grid units

	char **start_rooms;							AST(NAME("StartRoom"))
	char **end_rooms;							AST(NAME("EndRoom"))
} GenesisLayoutPath;
extern ParseTable parse_GenesisLayoutPath[];
#define TYPE_parse_GenesisLayoutPath GenesisLayoutPath

//////////////////////////////////////////////////////////////////////////
// Common Functions
//////////////////////////////////////////////////////////////////////////

void genesisLoadRoomDefLibrary();
GenesisLayoutRoom *genesisFindRoom( GenesisMapDescription* mapDesc, const char* room_name, const char* layout_name );
GenesisRoomMission *genesisFindRoomMission( GenesisRoomMission **room_missions, char* mission_name );
