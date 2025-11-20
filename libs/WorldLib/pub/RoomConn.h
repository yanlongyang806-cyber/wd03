/******
* The room connectivity system allows groups of objects to be flagged as a room with certain volumes inside
* of the group acting as outgoing doors (or "portals) to other rooms.  The system will use these rooms and 
* calculate an encompassing hull volume that can be used for containment queries.  The system will also
* automatically calculate the connections between rooms by comparing portals with each other.  If two portals
* from different rooms intersect, then a connection will be created between the rooms.
******/
#pragma once
GCC_SYSTEM

#include "wlGroupPropertyStructs.h"

#include "MapSnap.h"

typedef struct AIMMRoom AIMMRoom;
typedef struct AIMMRoomConn AIMMRoomConn;
typedef struct GMesh GMesh;
typedef struct GConvexHull GConvexHull;
typedef struct RoomPoint RoomPoint;
typedef struct RoomPointLink RoomPointLink;
typedef struct GroupTracker GroupTracker;
typedef struct GroupDef GroupDef;
typedef struct GroupInfo GroupInfo;
typedef struct RoomPortal RoomPortal;
typedef struct RoomConnGraph RoomConnGraph;
typedef struct RoomConnGraphServerParsed RoomConnGraphServerParsed;
typedef struct RoomConnGraphClientParsed RoomConnGraphClientParsed;
typedef struct Model Model;
typedef struct Room Room;
typedef struct WorldVolume WorldVolume;
typedef struct ZoneMapLayer ZoneMapLayer;
typedef struct WorldOcclusionEntry WorldOcclusionEntry;
typedef struct WorldVolumeEntry WorldVolumeEntry;
typedef struct AtlasTex AtlasTex;
typedef struct GfxLight GfxLight;
typedef struct ZoneMap ZoneMap;

typedef struct RoomInstanceData RoomInstanceData;

typedef struct SoundSpace SoundSpace;
typedef struct SoundSpaceConnector SoundSpaceConnector;

//pointer needed for the graphics code
typedef struct ShadowSearchNode ShadowSearchNode;

typedef struct RoomPartitionModel
{
	GroupTracker *tracker;						// used as an index for when objects are deleted from a partition
	GroupDef *def;								// dereferenced when scaling
	Model *model;								// model whose verts are to be included in the partition
	Mat4 world_mat;								// mat converting model local verts to world coordinates
} RoomPartitionModel;

typedef struct RoomPartition
{
	GroupTracker *tracker;						// stores the tracker that created this partition in edit mode
	char *zmap_scope_name;						// stores the partition's unique name in its layer scope
	U8 box_partition : 1;						// indicates whether this partition is defined by box bounds or actual models

	Room *parent_room;							// parent room to which partition belongs

	GMesh *mesh;								// mesh specifying all points and faces on partition
	GMesh *reduced_mesh;						// reduced mesh for occlusion mesh merging

	GConvexHull *hull;							// hull representation using planes to calculate point containment
	int *tri_to_plane;							// mapping of mesh tris to hull planes; array index corresponds to mesh tri index,
												// while value at index corresponds to convex hull plane index (-1 being no plane assigned)

	Vec3 bounds_min;							// axis-aligned world min bounds
	Vec3 bounds_max;							// axis-aligned world max bounds
	Vec3 bounds_mid;							// approximate world midpoint of partition (calculated from bounds)

	// MODEL DATA
	RoomPartitionModel **models;				// all models comprising the partition

	// BOX DATA
	Vec3 local_min;								// a box partition's local minimum
	Vec3 local_max;								// a box partition's local maximum
	Mat4 world_mat;								// a box partition's transformation matrix to world coordinates

	// MAP DATA
	AtlasTex **tex_list;						// list of image names of the partition for the mini map
	AtlasTex *overview_tex;						// lower res overview image of the partition

	MapSnapRoomPartitionData mapSnapData;

	// INSTANCE DATA
	RoomInstanceData *partition_data;
} RoomPartition;

typedef struct Room
{
	ZoneMapLayer *layer;						// name of the layer in which this room exists
	char *def_name;								// name of the top-level groupdef that created this room

	RoomConnGraph *parent_graph;				// graph to which the room belongs
	RoomPartition **partitions;					// all partitions comprising the room
	RoomPortal **portals;						// all outgoing portals belonging to the room

	Vec3 bounds_min;							// axis-aligned world min bounds
	Vec3 bounds_max;							// axis-aligned world max bounds
	Vec3 bounds_mid;							// approximate world midpoint of room (calculated from bounds)

	WorldVolumeEntry *volume_entry;				// the volume entry generated for this room; partitions are separate volume elements
	WorldVolume **portal_volumes;				// world volume for all of this room's portals; each portal is a separate element
	WorldOcclusionEntry **occlusion_entries;	// occlusion world cell entry, only exists if not in streaming mode
	GMesh *union_mesh;							// mesh formed by union of all of room's partitions

	GroupVolumePropertiesServer server_volume;
	GroupVolumePropertiesClient client_volume;

	// FLAGS
	U8 dirty : 1;								// indicates room hulls need to be recalculated
	U8 box_room : 1;							// indicates whether this room is based on a single box partition or multiple hull partitions
	U8 is_occluder : 1;							// indicates whether this room should create occlusion geometry from its hull
	U8 double_sided_occluder : 1;				// indicates whether this room's occlusion geometry should be double sided
	U8 use_model_verts : 1;						// indicates whether this room is using model bounds or the model verts
	U8 limit_contained_lights_to_room : 1;      // indicates whether this room's lights only apply to objects in the room

	U32 volume_type_bits;

	// EXTERNAL DATA
	SoundSpace *sound_space;
	GfxLight**	lights_in_room; //LDM: this will be null unless GraphicsLib fills it so don't rely on it being correct
	AIMMRoom *ai_room;
	void *astar_info;
} Room;

typedef struct RoomPortal
{
	ZoneMapLayer *layer;						// the layer in which this portal exists
	char *def_name;								// name of the top-level groupdef that created this portal

	int node_id;								// used during bin saving/loading

	RoomConnGraph *parent_graph;				// conn graph to which the portal belongs
	Room *parent_room;							// room to which the portal belongs; NULL for outdoor portals
	RoomPortal *neighbor;						// adjacent portal connecting parent room to another room; NULL if not connected

	Vec3 bounds_min;							// local min bounds
	Vec3 bounds_max;							// local max bounds
	Mat4 world_mat;								// mat converting local coords to world coords

	Vec3 world_mid;								// midpoint of the portal

	// EXTERNAL DATA
	WorldSoundConnProperties *sound_conn_props;
	SoundSpaceConnector *sound_conn;
	ShadowSearchNode *gfx_search_node;
	AIMMRoomConn *ai_roomPortal;
} RoomPortal;

typedef struct RoomConnGraph
{
	Room **rooms;								// all rooms in the graph; graph can have multiple, disconnected sections
	RoomPortal **outdoor_portals;				// portals not in any room; assumed to connect outdoors

	U8 dirty : 1;								// indicates connections and room hulls need to be recalculated
} RoomConnGraph;

/********************
* WORLD INTEGRATION
********************/
void roomPartitionAddModel(SA_PARAM_NN_VALID RoomPartition *partition, SA_PARAM_OP_VALID GroupTracker *tracker, SA_PARAM_NN_VALID GroupDef *def, SA_PARAM_NN_VALID Model *model, Mat4 world_mat);
SA_RET_OP_VALID RoomPartition *roomAddPartition(SA_PARAM_NN_VALID Room *room, SA_PARAM_OP_VALID GroupTracker *tracker);
SA_RET_OP_VALID RoomPartition *roomAddBoxPartition(SA_PARAM_NN_VALID Room *room, Vec3 local_min, Vec3 local_max, Mat4 world_mat, SA_PARAM_OP_VALID GroupTracker *tracker);
void roomRemovePartition(SA_PARAM_NN_VALID Room *room, SA_PARAM_NN_VALID RoomPartition *partition);
void roomRemoveTracker(SA_PARAM_NN_VALID Room *room, SA_PARAM_NN_VALID GroupTracker *tracker);
SA_RET_NN_VALID RoomPortal *roomAddPortal(SA_PARAM_NN_VALID Room *room, const Vec3 bounds_min, const Vec3 bounds_max, Mat4 world_mat);
void roomRemovePortal(SA_PARAM_NN_VALID Room *room, SA_PARAM_NN_VALID RoomPortal *portal);

void roomDirty(SA_PARAM_NN_VALID Room *room);
void roomPortalDirty(SA_PARAM_NN_VALID RoomPortal *portal);
void roomConnGraphDestroy(SA_PARAM_NN_VALID RoomConnGraph *conn_graph);
SA_RET_NN_VALID Room *roomConnGraphAddRoom(SA_PARAM_NN_VALID GroupInfo *info, SA_PARAM_OP_VALID GroupTracker *tracker, SA_PARAM_NN_VALID GroupDef *def);
SA_RET_NN_VALID Room *roomConnGraphAddBoxRoom(SA_PARAM_NN_VALID GroupInfo *info, SA_PARAM_OP_VALID GroupTracker *tracker, SA_PARAM_NN_VALID GroupDef *def);
void roomConnGraphRemoveRoom(SA_PARAM_NN_VALID RoomConnGraph *conn_graph, SA_PARAM_NN_VALID Room *room);
// The commented functions should not be used until we've had more discussion about it
// For now, outdoors should be represented by a room volume encompassing everything
//SA_PRE_NN_FREE SA_POST_NN_VALID RoomPortal *roomConnGraphAddOutdoorPortal(SA_PARAM_NN_VALID RoomConnGraph *conn_graph, Vec3 bounds_min, Vec3 bounds_max, Mat4 world_mat);
//void roomConnGraphRemoveOutdoorPortal(SA_PARAM_NN_VALID RoomConnGraph *conn_graph, SA_PARAM_NN_VALID RoomPortal *portal);
void roomConnGraphUpdate(SA_PARAM_NN_VALID RoomConnGraph *conn_graph);
void roomConnGraphUpdateAllRegions(SA_PARAM_NN_VALID ZoneMap *zmap);
void roomConnGraphUnloadLayer(SA_PARAM_NN_VALID ZoneMapLayer *layer);

void roomConnGraphCreatePartition(ZoneMap *zmap, int iPartitionIdx);
void roomConnGraphDestroyPartition(ZoneMap *zmap, int iPartitionIdx);


/********************
* PARSED CONVERSION
********************/
SA_RET_NN_VALID RoomConnGraphServerParsed *roomConnGraphGetServerParsed(SA_PARAM_NN_VALID RoomConnGraph *graph, const char **layer_names);
SA_RET_NN_VALID RoomConnGraphClientParsed *roomConnGraphGetClientParsed(SA_PARAM_NN_VALID RoomConnGraph *graph, const char **layer_names);
SA_RET_NN_VALID RoomConnGraph *roomConnGraphFromServerParsed(SA_PARAM_NN_VALID RoomConnGraphServerParsed *graph_parsed, const char **layer_names);
SA_RET_NN_VALID RoomConnGraph *roomConnGraphFromClientParsed(SA_PARAM_NN_VALID RoomConnGraphClientParsed *graph_parsed, const char **layer_names);

/********************
* HELPER FUNCTIONS
********************/
void roomGetNeighbors(SA_PARAM_NN_VALID Room *room, SA_PARAM_NN_VALID Room ***neighbors);

