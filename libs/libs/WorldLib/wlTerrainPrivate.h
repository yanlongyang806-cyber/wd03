#pragma once
GCC_SYSTEM

#include "wlTerrain.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Terrain_System););

typedef struct ZoneMapLayer			ZoneMapLayer;
typedef union Color					Color;
typedef struct PSDKCookedMesh		PSDKCookedMesh;
typedef struct WorldCollObject		WorldCollObject;
typedef struct WorldRegion			WorldRegion;
typedef struct GMesh				GMesh;
typedef struct HeightMap			HeightMap;
typedef struct HogFile				HogFile;
typedef struct SimpleBuffer			SimpleBuffer;
typedef struct PerThreadAtlasParams	PerThreadAtlasParams;
typedef struct BlockRange			BlockRange;
typedef struct TerrainBinnedObject	TerrainBinnedObject;
typedef struct HeightMapExcludeObject HeightMapExcludeObject;

#define HEIGHTMAP_MAGIC_NUMBER 0x4F0EE123

#define TERRAIN_TILE_MODEL_NAME "Terrain Tile"
#define TERRAIN_TILE_LIGHTING_MODEL_NAME "Terrain Tile Lighting"
#define TERRAIN_ATLAS_LIGHTING_MODEL_NAME "Terrain Atlas Lighting"
#define TERRAIN_ATLAS_MODEL_NAME "Terrain Atlas"
#define TERRAIN_WORLD_CLUSTER_MODEL_NAME "WorldCluster"
#define TERRAIN_TILE_OCC_MODEL_NAME "Terrain Tile Occlusion"
#define TERRAIN_ATLAS_OCC_MODEL_NAME "Terrain Atlas Occlusion"
#define TERRAIN_TILE_COLL_MODEL_NAME "Terrain Tile Collision"

#define TERRAIN_ATLAS_LOAD_DISTANCE 40.f

#define MAKE_TERRAIN_ATLAS_VERSION(terrain_bin_ver, atlas_ver, geo2_ol_bin_ver)	((terrain_bin_ver) + (atlas_ver)*100 + (geo2_ol_bin_ver)*10000)

#define TERRAIN_BIN_VERSION 14
#define TERRAIN_ATLAS_VERSION_ACTUAL 6
#define TERRAIN_ATLAS_VERSION_LATEST MAKE_TERRAIN_ATLAS_VERSION(TERRAIN_BIN_VERSION, TERRAIN_ATLAS_VERSION_ACTUAL, GEO2_OL_BIN_VERSION)

#define TERRAIN_ATLAS_VERSION TERRAIN_ATLAS_VERSION_LATEST

// ignore any weights below TERRAIN_MIN_WEIGHT (range 0-255)
#define TERRAIN_MIN_WEIGHT 5


#define SUBDIV_SIZE 8
typedef struct SubdivArrays
{
	Vec3 bounds_min, bounds_max;
	F32 cell_div;
	int *tri_arrays[SUBDIV_SIZE][SUBDIV_SIZE];
} SubdivArrays;

void subdivCreate(SubdivArrays *subdiv, GMesh *mesh);
int *subdivGetBucket(SubdivArrays *subdiv, const Vec3 pos);
void subdivClear(SubdivArrays *subdiv);


typedef struct TerrainMesh
{
	GMesh *mesh;
	const char **material_names;
	SubdivArrays subdiv;
} TerrainMesh;


typedef struct HeightMap
{
	U32						magic;									// For debugging only
    
	ZoneMapLayer			*zone_map_layer;
	IVec2					map_local_pos;

	struct 
	{
		Vec3				offset;
		Vec3				local_min, local_max;
		Vec3				world_mid;
		F32					radius;
	} bounds;

	U32						size;										// Width and height (= GRID_BLOCK_SIZE >> loaded_level_of_detail + 1)
    TerrainBuffer*			buffer_list[TERRAIN_BUFFER_NUM_TYPES];		// Terrain data (one buffer per type)
    TerrainBuffer*			color_buffer_list[HEIGHTMAP_MAX_LOD+1];		// Terrain color data (one buffer per LOD)
	TerrainBuffer*			normal_buffer_list[HEIGHTMAP_MAX_LOD+1];	// Terrain normal data (one buffer per LOD)
    
	TerrainBuffer*			backup_buffer[TERRAIN_BUFFER_NUM_TYPES];	// Saved data for undo 
	U8*						touch_buffer;								// Keeps track of the points that were drawn on
	U8						material_ids[NUM_MATERIALS];				// Base materials to use
	U8						material_count;								// How many materials are being used on this heightmap

	TerrainBinnedObject**   object_instances;                       	// Instances loaded from bin instead of source

    HeightMapExcludeObject** exclude_objects;							// Used for objects excluding other objects
    HeightMapExcludeObject** exclude_neighbors;							// Used for objects excluding other objects (neighbor maps)

	U32						mod_time;

	U32 					unsaved:1;
	U32						hide_detail_in_editor:1;

	U32						height_time, height_update_time;
    U32						texture_time, texture_update_time;
    U32						visualization_time, visualization_update_time;

    bool					buffer_touched[TERRAIN_BUFFER_NUM_TYPES]; // For editing only

	int						edge_color[4]; // 0 = none, 1 = white, 2 = red, 3 = green, 4 = blue
	bool					edge_locked[4];

	// For LOD's, 0 is full detail, 1 is half detail, 2 is quarter detail
	U32						loaded_level_of_detail;			// HeightMap LOD (Normal/color maps are 2 lods higher)
	U32						level_of_detail;				// Currently rendering (is equal to loaded_level_of_detail unless in the terrain editor)

	HeightMapAtlasData		*data;

	U32						editor_cooked_mesh_lod;
	U32						editor_cooked_mesh_mod_time;
	WorldCollObject 		*wcoEditor;
	PSDKCookedMesh			*psdkCookedMeshEditor;

	TerrainMesh				*terrain_mesh; // only used during binning

} HeightMap;

typedef struct TerrainFileDef
{
	char *ext;
	int lod_size;
	int lod;
	int patch_stride;
	int lod_stride;
} TerrainFileDef;

extern const TerrainFileDef heightmap_def;
extern const TerrainFileDef colormap_def;
extern const TerrainFileDef tiffcolormap_def;
extern const TerrainFileDef materialmap_def;
extern const TerrainFileDef soildepthmap_def;
extern const TerrainFileDef holemap_def;
extern const TerrainFileDef objectmap_def;
extern const TerrainFileDef roadmap_def;
//extern const TerrainFileDef terrainmap_def;
//extern const TerrainFileDef terrainmap_detail_def;

typedef struct TerrainFile TerrainFile;

void updateTerrainFileSource(const TerrainFileDef *def, TerrainBlockRange *block, TerrainFile *file);
bool openTerrainFileWriteSource(TerrainFile *file, bool validate);
bool openTerrainFileReadSource(TerrainFile *file, bool validate);
bool commitTerrainFileSource(TerrainFile *file);

SimpleBufHandle readTerrainFileHogg(ZoneMapLayer *layer, int blocknum, char *relpath);

void closeTerrainFile(TerrainFile *file);

typedef struct HeightMapBinAtlas
{
	U32							lod;
	IVec2						local_pos;
	struct HeightMapBinAtlas	*children[4];
	struct HeightMapBinAtlas	*parent;

	bool						needs_collision;

	U8							*color_array;
	bool						is_dxt_color;

	// not in the intermediate bins:
	const char					**detail_material_names; // pooled strings
	GMesh						*mesh;
	GMesh						*occlusion_mesh;
	int							tile_count;
	F32							corner_heights[4], corner_avg_heights[4];
	Vec3						corner_normals[4], corner_avg_normals[4];
	U8							*corner_colors[4]; // in local detail material color space
	U8							*corner_avg_colors[4]; // in common detail material color space
	const char					**corner_detail_material_names[4];

	// processing state
	bool						merging, merged;		// has created and reduced a mesh from children (for LOD 0 this means loading from disk)
	bool						writing, written;		// has been written to a SimpleBuffer

	bool						corner_averaged[4];		// has had corner averages computed
	bool						left_seamed;			// has been seamed with left neighbor
	bool						bottom_seamed;			// has been seamed with bottom neighbor
	bool						right_seamed;			// has been seamed with right neighbor
	bool						top_seamed;				// has been seamed with top neighbor

	// For error handling
	const char					*layer_filename;

} HeightMapBinAtlas;


void freeTerrainMesh(TerrainMesh *terrain_mesh);

// Atlas functions
void layerSaveBlockAtlas(ZoneMapLayer *layer, int blocknum, HeightMapBinAtlas *atlas, HogFile *interm_file, SimpleBuffer ***output_array);
HeightMapBinAtlas* atlasFindByLocationEx(HeightMapBinAtlas ***atlas_list, IVec2 world_pos, U32 lod, IVec2 out_height_pos, IVec2 out_color_pos, ZoneMapLayer **layers, const IVec2 min_local_pos, const IVec2 max_local_pos, PerThreadAtlasParams *thread_params, HeightMapAtlasRegionData *atlases);
#define atlasFindByLocation(atlas_list, world_pos, lod, out_height_pos, out_color_pos) atlasFindByLocationEx(atlas_list, world_pos, lod, out_height_pos, out_color_pos, NULL, NULL, NULL, NULL, NULL)
void atlasBinFree(HeightMapBinAtlas *atlas);

// Heightmap functions
void heightMapDestroy(HeightMap *height_map);

void occlusion_quad_recurse(F32 *height_buf, S32 buf_size, F32 scale, F32 cutoff, F32 min_height, IVec2 min_pos, IVec2 max_pos, GMesh *mesh, U8 *subdiv_array);
HeightMap *heightMapCreate(ZoneMapLayer *layer, int map_local_pos_x, int map_local_pos_z, HeightMapAtlasData *data);

TerrainMeshRenderMaterial **terrainConvertHeightMapMeshToRenderable(GMesh *source_mesh, const IVec2 local_pos, GMesh *parent_mesh, 
																	const IVec2 parent_local_pos, GMesh *output_mesh, 
																	int atlas_size, F32 *min_height, F32 *max_height, 
																	bool use_material_fade, bool disable_tangent_space, PerThreadAtlasParams *thread_params);

void atlasFree(SA_PRE_OP_VALID SA_POST_P_FREE HeightMapAtlas *atlas);
void heightMapAtlasDataFree(HeightMapAtlasData *atlas_data);
void terrainLoadAtlasesForCameraPos(int iPartitionIdx, HeightMapAtlasRegionData *atlases, const F32 *camera_positions, int camera_position_count, const BlockRange **terrain_editor_blocks, bool load_all, U32 timestamp);
void terrainUnloadAtlases(HeightMapAtlasRegionData *atlases);
void terrainCloseAtlasesForPartition(int iPartitionIdx, HeightMapAtlasRegionData *atlases);

F32 terrainGetDesiredMeshError(int lod);
int terrainGetDesiredMeshTriCount(int lod, int tile_count);

bool atlasLoadData(HeightMapAtlasData *data, const IVec2 local_pos, int lod, WorldRegion *region, HogFile *atlas_hogg_file, HogFile *model_hogg_file, HogFile *light_model_hogg_file, HogFile *coll_hogg_file);
