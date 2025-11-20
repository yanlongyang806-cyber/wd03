#pragma once
GCC_SYSTEM

#include "wlGroupPropertyStructs.h"

#include "MapSnap.h"

typedef struct GMeshParsed GMeshParsed;
typedef struct RoomInstanceData RoomInstanceData;
typedef struct WorldSoundVolumeProperties WorldSoundVolumeProperties;
typedef struct WorldSoundConnProperties WorldSoundConnProperties;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

AUTO_STRUCT;
typedef struct RoomPartitionParsed
{
	char *zmap_scope_name;
	GMeshParsed *gmesh;

	Vec3 bounds_min;
	Vec3 bounds_max;
	Vec3 bounds_mid;

	// calculated by the mapsnap bin process  (TODO - I only need this on the client)
	MapSnapRoomPartitionData mapSnapData;

	// instance data
	RoomInstanceData *partition_data;
} RoomPartitionParsed;
extern ParseTable parse_RoomPartitionParsed[];
#define TYPE_parse_RoomPartitionParsed RoomPartitionParsed

AUTO_STRUCT;
typedef struct RoomPortalParsed
{
	int layer_idx;
	char *def_name;

	U32 portal_id;
	U32 neighbor_id;

	Vec3 bounds_min;
	Vec3 bounds_max;
	Mat4 world_mat;

	// external data
	WorldSoundConnProperties *sound_conn_props;
} RoomPortalParsed;
extern ParseTable parse_RoomPortalParsed[];
#define TYPE_parse_RoomPortalParsed RoomPortalParsed

AUTO_STRUCT;
typedef struct RoomServerParsed
{
	int layer_idx;
	char *def_name;

	RoomPartitionParsed **partitions;
	RoomPortalParsed **portals;

	Vec3 bounds_min;
	Vec3 bounds_max;
	Vec3 bounds_mid;

	GroupVolumePropertiesServer server_volume;

	const char					**volume_type_strings;			AST( POOL_STRING )
} RoomServerParsed;
extern ParseTable parse_RoomServerParsed[];
#define TYPE_parse_RoomServerParsed RoomServerParsed

AUTO_STRUCT;
typedef struct RoomClientParsed
{
	int layer_idx;
	char *def_name;

	RoomPartitionParsed **partitions;
	RoomPortalParsed **portals;

	Vec3 bounds_min;
	Vec3 bounds_max;
	Vec3 bounds_mid;

	U8 limit_contained_lights_to_room : 1;

	GroupVolumePropertiesClient client_volume;

	const char					**volume_type_strings;			AST( POOL_STRING )
} RoomClientParsed;
extern ParseTable parse_RoomClientParsed[];
#define TYPE_parse_RoomClientParsed RoomClientParsed

AUTO_STRUCT;
typedef struct RoomConnGraphServerParsed
{
	RoomServerParsed **rooms;
	RoomPortalParsed **outdoor_portals;
} RoomConnGraphServerParsed;
extern ParseTable parse_RoomConnGraphServerParsed[];
#define TYPE_parse_RoomConnGraphServerParsed RoomConnGraphServerParsed

AUTO_STRUCT;
typedef struct RoomConnGraphClientParsed
{
	RoomClientParsed **rooms;
	RoomPortalParsed **outdoor_portals;
} RoomConnGraphClientParsed;
extern ParseTable parse_RoomConnGraphClientParsed[];
#define TYPE_parse_RoomConnGraphClientParsed RoomConnGraphClientParsed
