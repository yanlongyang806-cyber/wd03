#pragma once
GCC_SYSTEM

#include "wlTerrainEnums.h"
#include "file.h"
#include "WorldLibEnums.h"
#include "../StaticWorld/group.h" // for GroupDefRef
#include "StashTable.h"

#define HEIGHTMAP_SIZE(x)		(((U32)GRID_BLOCK_SIZE >> (2+x)) + 1)
#define COLORMAP_SIZE(x)		(((U32)GRID_BLOCK_SIZE >> (x)) + 1)

#define NUM_MATERIALS 8
#define MIN_DRAW_LOD 2

#define HEIGHTMAP_NO_LOD				9		// LOD for a layer with no heightmaps
#define HEIGHTMAP_MAX_LOD				7		// LOD 7 = 2x2, where 0 = 256x256 (1')
#define HEIGHTMAP_DEFAULT_LOD			4		// LOD 4 = 16x16 (16'), for newly created terrain
#define HEIGHTMAP_MIN_HEIGHT			-5000.f	// Terrain heights cannot be this height or lower
#define HEIGHTMAP_MAX_HEIGHT			50000.f

#define ATLAS_MAX_LOD					5		// LOD 5 = 2x2, where 0 = 64x64 (4')

#define HEIGHTMAP_ATLAS_MIN_LOD			1		// If any of these change, be sure to increment TERRAIN_BIN_VERSION
#define HEIGHTMAP_ATLAS_HEIGHT_SIZE		65		// 65 = (32 heightmaps x 2 grid points + 1 pad from neighbors)
#define HEIGHTMAP_ATLAS_COLOR_PAD		1
#define HEIGHTMAP_ATLAS_COLOR_SIZE		(256+HEIGHTMAP_ATLAS_COLOR_PAD)		// 256 = (32 heightmaps x 8 grid points)
#define ATLAS_COLOR_TEXTURE_SIZE		256


#define COLOR_LOD_DIFF 2						//Color is always 2 lod's higher than the rest.
#define GET_COLOR_LOD(x) MAX(0, ((int)(x))-COLOR_LOD_DIFF)
#define UNDEFINED_MAT 0xFF
#define UNDEFINED_OBJ 0xFF
#define MAX_SOIL_DEPTH 30 //Cannot be 0

#define SRC_HEADER_SIZE (3*sizeof(U32))	//Source File Header Size
#define SRC_MATERIAL_HEADER_SIZE (NUM_MATERIALS*sizeof(U8) + sizeof(U8))	//Material Source File Header Size
#define SRC_HEIGHT_SIZE (sizeof(U32))				//Height Datum Size (in Source Files)
#define SRC_ALPHA_SIZE (sizeof(U8))					//Alpha Datum Size (in Source Files)
#define SRC_COLOR_SIZE (sizeof(U8)*3)				//Tint Datum Size (in Source Files)
#define SRC_MATERIAL_SIZE (NUM_MATERIALS*sizeof(U8))	//Material Datum Size (in Source Files)
#define SRC_SOIL_DEPTH_SIZE (sizeof(F32))			//Soil Depth Datum Size (in Source Files)
#define BIN_STRING_SIZE 64

#define TERRAIN_SOURCE_COMPRESSED (1<<8)

typedef struct HeightMap HeightMap;
typedef struct ZoneMapLayer ZoneMapLayer;
typedef union Color Color;
typedef struct WorldFile WorldFile;
typedef struct WorldCollisionEntry WorldCollisionEntry;
typedef struct PSDKCookedMesh PSDKCookedMesh;
typedef struct WorldCollObject WorldCollObject;
typedef struct TerrainFileList TerrainFileList;
typedef struct TerrainFile TerrainFile;
typedef struct WorldRegion WorldRegion;
typedef struct MaterialNamedConstant MaterialNamedConstant;
typedef struct TerrainBlockRange TerrainBlockRange;
typedef struct Model Model;
typedef struct PhysicalProperties PhysicalProperties;
typedef struct HeightMapAtlas HeightMapAtlas;
typedef struct TerrainEditorState TerrainEditorState;
typedef struct ErodeBrushData ErodeBrushData;
typedef struct TerrainBuffer TerrainBuffer;
typedef struct TerrainObjectEntry	TerrainObjectEntry;
typedef struct GMesh GMesh;
typedef struct Material Material;
typedef struct RiverList RiverList;
typedef struct TerrainBinnedObjectGroup TerrainBinnedObjectGroup;
typedef struct TerrainObjectWrapper TerrainObjectWrapper;
typedef struct POIGenesisMap POIGenesisMap;
typedef struct GfxDynObjLightCache GfxDynObjLightCache;

typedef struct HogFile HogFile;
typedef struct SimpleBuffer SimpleBuffer;
typedef struct SimpleBuffer *SimpleBufHandle;
typedef struct BinFileList BinFileList;
typedef U8 TerrainCompressedNormal[3];

typedef struct HeightMapAtlas HeightMapAtlas;
typedef struct HeightMapAtlasGfxData HeightMapAtlasGfxData;
typedef void (*HeightMapAtlasGfxDataFree)(HeightMapAtlasGfxData *gfx_data);
typedef struct BasicTexture BasicTexture;

typedef struct WorldClusterState WorldClusterState;

extern ParseTable parse_BinFileList[];
#define TYPE_parse_BinFileList BinFileList

#define TERRAIN_TEXTURES_PER_POINT 4

typedef U8  U8Vec3[3];
#define TerrainObjectDensity U16
#define SignedTerrainObjectDensity S16
#define MAX_OBJECT_DENSITY 0xFFFF // Max of U16
#define LOG_MAX_OBJECT_DENSITY 11.1f // log(LOG_MAX_OBJECT_DENSITY)

AUTO_ENUM;
typedef enum TerrainEditorViewModes {
	TE_Regular = 0,
	TE_Extreme_Angles,
	TE_Grid,
	TE_Filters,
	TE_Object_Density,
	TE_Material_Weight,
	TE_Soil_Depth,
    TE_Selection,
    TE_Shadow,
	TE_DesignAttr,
} TerrainEditorViewModes;

typedef struct TerrainObjectBuffer
{
	U8 object_type;		
	TerrainObjectDensity *density;
}TerrainObjectBuffer;

typedef struct TerrainMaterialWeight
{
	F32 weights[NUM_MATERIALS];
} TerrainMaterialWeight;

typedef struct TerrainEditedMaterialWeights
{
	U8 *edited;
	int size[2];
	int offset[2];
	int min_map_pos[2];
	int lod_diff;
} TerrainEditedMaterialWeights;

typedef struct TerrainBuffer
{
	WorldFile *file; // File this buffer belongs to
	int size;
	int lod;
	TerrainBufferType type;
	union {
		F32 *data_f32;
		U8 *data_byte;
		Color *data_color;
		U8Vec3 *data_u8vec3;
		TerrainCompressedNormal *data_normal;
		TerrainMaterialWeight *data_material;
		TerrainObjectBuffer **data_objects;
	};
} TerrainBuffer;

typedef struct HeightMapTracker
{
	HeightMapAtlas			*atlas;
	HeightMap				*height_map;
	IVec2					world_pos;

	//HeightMap				*neighbors[3][3];

	U32						height_map_mod_time;		// synchronizes last time this tracker was updated to its groupdef

	U32						last_size;

} HeightMapTracker;


typedef struct TerrainMeshRenderMaterial
{
	U8 detail_material_ids[3]; // 255 for unused.  Sorted ascending.
	U8 color_idxs[3]; // 0=red, 1=green, 2=blue, 3=alpha
} TerrainMeshRenderMaterial;

typedef struct TerrainTextureData
{
	U8 *data;
	U32 width;
	bool is_dxt;
	bool has_alpha;
} TerrainTextureData;

typedef struct HeightMapAtlasData
{
	WorldRegion *region;
	Vec3 offset;

	// LOD 0 only:
	Model						*collision_model;
	WorldCollisionEntry			*collision_entry;

	// client only
	struct 
	{
		Model						*draw_model;
		Model						*draw_model_cluster;
		Model						*occlusion_model;
		Model						*static_vertex_light_model;
		int							video_memory_usage;

		TerrainTextureData			color_texture;

		GfxDynObjLightCache			*light_cache;

		HeightMapAtlasGfxData		*gfx_data;
		HeightMapAtlasGfxDataFree	gfx_free_func;

		TerrainMeshRenderMaterial	**model_materials;
		const char					**model_detail_material_names;

	} client_data;

} HeightMapAtlasData;

typedef struct TextureSwap TextureSwap;

AUTO_STRUCT;
typedef struct HeightMapAtlas
{
	// local_pos and lod MUST be first, they are used as a hash
	IVec2						local_pos;
	int							lod;

	int							layer_bitfield;
	F32							min_height;
	F32							max_height;

	AST_STOP

	WorldCellState				load_state;
	U32							unload_request_time;
	bool						atlas_active;
	bool						hidden;

	WorldRegion					*region;

	HeightMapAtlas				*children[4];
	HeightMapAtlas				*parent;
	HeightMapAtlas				*neighbors[8];

	HeightMapAtlasData			*data; // loaded from bin file

	F32							corner_lods[4];
	F32							last_corner_lods[4];

	HeightMap					*height_map; // in editor only

	bool						needs_static_light_update; // editor only




	// needs to be moved into HeightMapAtlasData *data so that this information will be streamed from the disk instead of taking up a ton of space memory.

	// expected to be used for rendering objects to the world instead of using the old cell system.
	// this way the simplygon models can be grouped in grids using the atlas system.
	//ModelClusterObject			**world_objects;
	//Model						**world_objects;
	//void						**world_object_matrices;	// earray of pointers to Mat4s
	//Mat4Container				**world_object_matrices;
	//WorldDrawableEntry		**world_drawables;

} HeightMapAtlas;

AUTO_STRUCT;
typedef struct HeightMapAtlasRegionData
{
	int bin_version_number;
	HeightMapAtlas **all_atlases;

	HeightMapAtlas **root_atlases;				NO_AST
	StashTable atlas_hash;						NO_AST
	char **layer_names;							NO_AST	// copied from BinFileList after loading

} HeightMapAtlasRegionData;

extern ParseTable parse_HeightMapAtlas[];
#define TYPE_parse_HeightMapAtlas HeightMapAtlas
extern ParseTable parse_HeightMapAtlasRegionData[];
#define TYPE_parse_HeightMapAtlasRegionData HeightMapAtlasRegionData

AUTO_STRUCT;
typedef struct TerrainTimestamp
{
	char *filename;
	U32 time;
} TerrainTimestamp;

AUTO_STRUCT;
typedef struct TerrainTimestamps
{
	F32 version;
	TerrainTimestamp **deplist;
	U32 bin_time; // Time of bin creation; used for synchronization with intermediate files
} TerrainTimestamps;

AUTO_STRUCT;
typedef struct TerrainExportInfoBlock
{
    IVec2 min;
    IVec2 max;
} TerrainExportInfoBlock;

AUTO_STRUCT;
typedef struct TerrainExportInfo
{
    int lod;
    F32 height_min;
    F32 height_max;
    TerrainExportInfoBlock **blocks;
} TerrainExportInfo;

AUTO_STRUCT;
typedef struct TerrainObjectEntry
{
	GroupDefRef	objInfo;		AST ( NAME(Object)	)
	int	seed;					AST ( NAME(Seed)	)
} TerrainObjectEntry;

extern ParseTable parse_TerrainObjectEntry[];
#define TYPE_parse_TerrainObjectEntry TerrainObjectEntry

typedef struct HeightMapBackup
{
	HeightMap			*height_map;
	TerrainBuffer		**backup_buffers;	
} HeightMapBackup;

typedef struct HeightMapBackupList
{
	HeightMapBackup **list;
}HeightMapBackupList;

typedef struct TerrainMaterialChange
{
	HeightMap *height_map;
	U8 old_material_ids[NUM_MATERIALS];			
	U8 old_material_count;	
	U8 new_material_ids[NUM_MATERIALS];			
	U8 new_material_count;	
}TerrainMaterialChange;

typedef struct TerrainMaterialChangeList
{
	TerrainMaterialChange **list;
}TerrainMaterialChangeList;

typedef struct HeightMapCache
{
    HeightMap *maps[2][2];
} HeightMapCache;

//////////////////////////////////////////////////////////////////////////

HeightMap* terrainFindHeightMap(StashTable heightmaps, IVec2 local_pos, HeightMapCache *cache);
HeightMap* terrainGetHeightMapForLocalGridPos(SA_PARAM_NN_VALID ZoneMapLayer *layer, SA_PRE_NN_ELEMS(2) SA_POST_OP_VALID IVec2 local_pos);

void heightMapMakeBackup_dbg(HeightMap *map, int x, int y, int draw_lod, TerrainBufferType type, int object_type MEM_DBG_PARMS);
#define heightMapMakeBackup(map, x, y, draw_lod, type, object_type) heightMapMakeBackup_dbg(map, x, y, draw_lod, type, object_type MEM_DBG_PARMS_INIT)

Material *terrainGetMaterial(HeightMap *height_map, F32 x, F32 z);
PhysicalProperties *terrainGetPhysicalProperties(HeightMap *height_map, F32 x, F32 z);
bool terrainGetInterpolatedValues(F32 x, F32 z, HeightMap *map, F32 mapx, F32 mapy, int *startX, int *startY, F32 *interpX, F32 *interpY);
bool terrainCheckAngleAndHeight(SA_PARAM_NN_VALID ZoneMapLayer *layer, F32 x, F32 z, U8 min_angle, U8 max_angle, F32 min_height, F32 max_height, HeightMap **cache);
bool terrainPointIsOnLayer(SA_PARAM_NN_VALID ZoneMapLayer *layer, F32 x, F32 z);

void heightMapRearrangeMaterialWeights(HeightMap *height_map, TerrainBuffer *material_buffer, U8 old_mats[NUM_MATERIALS], U8 old_mat_count, U8 new_mats[NUM_MATERIALS], U8 new_mat_count);

void heightMapGetNeighbors(HeightMap *height_maps[3][3], HeightMap **map_cache);

void terrainUpdateNormals(ZoneMapLayer *layer, bool force);

void heightMapDoUpsamples(HeightMap *height_map, int draw_lod, bool keep_high_res);
void heightMapFindUnusedObjects(HeightMap *height_map);
void heightMapFindUnusedMaterials(HeightMap *height_map, TerrainMaterialChangeList **mat_undo_list);
void heightMapApplyBackupBuffer(HeightMap *height_map, TerrainBuffer *buffer);
void heightMapSaveBackupBuffers(HeightMap *height_map, HeightMapBackupList **map_undo_list);
void heightMapFreeBackupBuffers(HeightMapBackupList *map_backup_list);

void terrainBinDownsampleAndSave(ZoneMapLayer *layer, TerrainBlockRange *range);
//void terrainSaveGenesisBins(ZoneMapLayer **layers, POIGenesisMap *genesis_map);
void terrainSaveBins(ZoneMapLayer *layer, int current_layer, int total_layers);
SA_ORET_OP_VALID BinFileList *terrainSaveRegionAtlases(WorldRegion *region);
void terrainLoadRegionAtlases(WorldRegion *region, BinFileList *file_list);
void freeHeightMapAtlasRegionData(HeightMapAtlasRegionData *atlases);

void terrainLock();
void terrainUnlock();

void *terrainAlloc_dbg(int count, int size MEM_DBG_PARMS);
#define terrainAlloc(count,size) terrainAlloc_dbg(count,size MEM_DBG_PARMS_INIT)

U32 terrainGetProcessMemory();

int heightmapAtlasGetLODSize(int lod);

//////////////////////////////////////////////////////////////////////////

void ClearTerrainBuffer(SA_PARAM_NN_VALID ZoneMapLayer *layer, SA_PARAM_NN_VALID TerrainBuffer *buffer);
TerrainBuffer *CreateTerrainBuffer_dbg(TerrainBufferType type, int width, int lod MEM_DBG_PARMS);
#define CreateTerrainBuffer(type, size, lod) CreateTerrainBuffer_dbg(type, size, lod MEM_DBG_PARMS_INIT)
void DestroyTerrainBuffer(TerrainBuffer *buffer);

TerrainBuffer *heightMapCreateBuffer_dbg(SA_PARAM_NN_VALID HeightMap *map, TerrainBufferType type, int lod MEM_DBG_PARMS);
#define heightMapCreateBuffer(map, type, lod) heightMapCreateBuffer_dbg(map, type, lod MEM_DBG_PARMS_INIT)
TerrainBuffer *heightMapCreateBufferSize_dbg(SA_PARAM_NN_VALID HeightMap *map, TerrainBufferType type, int lod, int size MEM_DBG_PARMS);
#define heightMapCreateBufferSize(map, type, lod, size) heightMapCreateBufferSize_dbg(map, type, lod, size MEM_DBG_PARMS_INIT)
TerrainBuffer *heightMapGetOldestBuffer(HeightMap *map, TerrainBufferType type, int lod);
TerrainBuffer *heightMapGetBuffer(SA_PARAM_NN_VALID HeightMap *map, TerrainBufferType type, int lod);
TerrainBuffer *heightMapDetachBuffer(HeightMap *map, TerrainBufferType type, int lod);
void heightMapDeleteBuffer(SA_PARAM_NN_VALID HeightMap *map, TerrainBufferType type, int lod);
U32 GetTerrainBufferStride(TerrainBufferType type);
U32 GetTerrainBufferSize(TerrainBuffer *buffer);
const char *TerrainBufferGetTypeName(TerrainBufferType type);

ZoneMapLayer *heightMapGetLayer(HeightMap *height_map);
void heightMapSetLayer(HeightMap *height_map, ZoneMapLayer *layer);
bool heightMapGetMapLocalPos(HeightMap *height_map, IVec2 map_local_pos);
void heightMapGetBounds(HeightMap *height_map, Vec3 min, Vec3 max);

U32		heightMapGetSize(HeightMap *height_map); // Size of this square heightmap
F32		heightMapGetInterpolatedHeight(HeightMap *interpMap, U32 x, U32 y, F32 interpX, F32 interpY); // interpX and interpZ are 0-1.0 above x and z
F32		heightMapGetInterpolatedShadow(HeightMap *interpMap, U32 startX, U32 startY, F32 interpX, F32 interpY);
void	heightMapGetInterpolatedNormal(HeightMap *interpMap, U32 startX, U32 startY, F32 interpX, F32 interpY, U32 lod, U8Vec3 normal);
void	heightMapGetInterpolatedSoilDepth(HeightMap *interpMap, U32 startX, U32 startY, F32 interpX, F32 interpY, F32 *depth);
F32		heightMapGetHeight(HeightMap *height_map, U32 x, U32 y); // Height of a given point
void	heightMapSetHeight(HeightMap *height_map, U32 x, U32 y, F32 newheight);
U8		heightMapGetAttribute(HeightMap *height_map, U32 x, U32 z);
void	heightMapSetAttribute(HeightMap *height_map, U32 x, U32 z, U8 newvalue);
F32		heightMapGetSelection(HeightMap *height_map, U32 x, U32 z);
void	heightMapSetSelection(HeightMap *height_map, U32 x, U32 z, F32 newvalue);
F32		heightMapGetShadow(HeightMap *height_map, U32 x, U32 z);
void	heightMapSetSoilDepth(HeightMap *height_map, U32 x, U32 z, F32 newvalue);
F32		heightMapGetSoilDepth(HeightMap *height_map, U32 x, U32 z);
Color	heightMapGetVertexColor(HeightMap *height_map, U32 x, U32 y, U32 lod);
void	heightMapSetVertexColor(HeightMap *height_map, U32 x, U32 y, U32 lod, Color newcolor);
void	heightMapGetVertexColorVec3(HeightMap *height_map, U32 x, U32 y, U32 lod, U8Vec3 *ret);
void	heightMapSetVertexColorVec3(HeightMap *height_map, U32 x, U32 y, U32 lod, U8Vec3 newcolor);
TerrainObjectDensity heightMapGetObjectDensity(HeightMap *height_map, U32 x, U32 z, U8 object_type);
void heightMapSetObjectDensity(HeightMap *height_map, U32 x, U32 z, TerrainObjectDensity newvalue, U8 object_type);
void heightMapSetAllObjectsAndBackup(HeightMap *height_map, U32 x, U32 z, int draw_lod, TerrainObjectDensity newvalue);
F32 heightMapGetMaterialWeight(HeightMap *height_map, U32 x, U32 z, U8 local_mat);
U8		heightMapGetAlpha(HeightMap *height_map, U32 x, U32 y, U32 lod);
void	heightMapSetAlpha(HeightMap *height_map, U32 x, U32 y, U32 lod, U8 alpha);

bool heightMapEdgeIsLocked(HeightMap *map, int x, int y);

void heightMapGetMaterialIDs(HeightMap *height_map, U8 material_ids[NUM_MATERIALS], int *num_materials);
TerrainMaterialWeight *heightMapGetMaterialWeights(HeightMap *height_map, U32 x, U32 z, const char *material_names[NUM_MATERIALS]);
int		heightMapAddMaterial(HeightMap *height_map, U8 id);
void	heightMapSetMaterialWeights(HeightMap *height_map, U32 x, U32 z, TerrainMaterialWeight mat_weights);
void	heightMapSetMaterialIDs(HeightMap *height_map, U8 material_ids[NUM_MATERIALS], int num_materials);

void	heightMapGetNormal(HeightMap *height_map, U32 x, U32 y, U32 lod, U8 normal[3]);

void	heightMapResample(HeightMap *height_map, U32 lod);
U32		heightMapGetLoadedLOD(HeightMap *height_map);

void	heightMapSetVisibleEdges(HeightMap *height_map, int color1, int color2, int color3, int color4);
void	heightMapSetLockedEdges(HeightMap *height_map, bool locked1, bool locked2, bool locked3, bool locked4);

bool	heightMapIsLODLocked(HeightMap *height_map);
//bool	heightMapIsLoadingDisabled(HeightMap *height_map);
U32		heightMapGetLevelOfDetail(HeightMap *height_map); // 0 is full detail, 1 is half detail, 2 is quarter detail...
void	heightMapSetLevelOfDetail(HeightMap *height_map, U32 level_of_detail);
void	heightMapUpdate(HeightMap *height_map);
void	heightMapUpdateGeometry(HeightMap *height_map);

bool	heightMapWasTouched(HeightMap *height_map, TerrainBufferType type);
void	heightMapClearTouchedBits(HeightMap *height_map);

U32		heightMapGetSizeForLOD(U32 lod);
U32		heightMapGetSizeLOD(HeightMap *height_map); // Size of this square heightmap, taking into account level of detail
F32		heightMapGetHeightLOD(HeightMap *height_map, U32 x, U32 y); // Height of a given point. input coordinates should be scaled based on LOD

const char **heightMapGetDetailTextures(HeightMap *height_map);

void heightMapModify(HeightMap *height_map);
void heightMapUpdateTextures(HeightMap *height_map);
void heightMapUpdateVisualization(HeightMap *height_map);
void heightMapMarkUnsaved(HeightMapTracker *tracker, void *unused);
bool heightMapIsUnsaved(HeightMap *height_map);

void heightMapUpdateNormals(HeightMap *height_maps[3][3]);
void heightMapUpdateColors(HeightMap *height_maps[3][3], int lod);

void heightMapDestroy(HeightMap *height_map);

void heightMapCreateCollision(SA_PARAM_OP_VALID HeightMapAtlasData *atlas_data, SA_PARAM_OP_VALID ZoneMapLayer *layer);
void heightMapDestroyCollision(SA_PARAM_OP_VALID HeightMapAtlasData *atlas_data);

HeightMapTracker *heightMapTrackerOpen(SA_PARAM_NN_VALID HeightMap *height_map);
void heightMapTrackerClose(SA_PARAM_OP_VALID HeightMapTracker *tracker);
void heightMapTrackerUpdate(SA_PARAM_OP_VALID HeightMapTracker *tracker, bool force, bool update_collision, bool use_bin_bounds);
void heightMapTrackerOpenEntries(int iPartitionIdx, SA_PARAM_OP_VALID HeightMapTracker *tracker);
void heightMapTrackerCloseEntries(SA_PARAM_OP_VALID HeightMapTracker *tracker);

int heightMapLoadTerrainObjects(ZoneMapLayer *layer, TerrainBlockRange *range, IVec2 rel_pos, bool new_block);

void layerTerrainGetNeighbors(HeightMap *height_map, ZoneMapLayer *out_layers[4], int block_idxs[4]);

//void terrainFreeObjectGroups(GroupDef *objects_root_def, GroupFile *objects_file);
void terrainCreateObjectGroups(HeightMap **maps, TerrainBlockRange *range, ZoneMapLayer *layer, TerrainObjectWrapper **wrappers, TerrainObjectEntry **object_table, 
							   TerrainExclusionVersion exclusion_version, F32 color_shift);
void terrainCreateObjectGroupsWithLookup(HeightMap **maps, TerrainBlockRange *range, ZoneMapLayer *layer, TerrainObjectWrapper **wrappers,
                               int *object_lookup, TerrainObjectEntry **object_table, TerrainExclusionVersion exclusion_version, F32 color_shift, bool playable, bool bForce);
void terrainUpdateObjectGroups( SA_PARAM_NN_VALID ZoneMapLayer *layer, bool from_bins );

TerrainObjectEntry *terrainGetLayerObject(ZoneMapLayer *layer, int index);
void terrainFreeTerrainBinnedObjectGroup(TerrainBinnedObjectGroup *object_group);

char *terrainGetLayerMaterial(ZoneMapLayer *layer, int index);

void terrainExportTiff(TerrainBlockRange **blocks, StashTable heightmaps, int lod, FILE *file_height, FILE *file_color, F32 *min, F32 *max);
bool terrainImportColorTiff(StashTable heightmaps, TerrainExportInfo *info, FILE *file_color, int lod);
bool terrainImportHeightTiff(StashTable heightmaps, TerrainExportInfo *info, FILE *file_height, int lod);

HeightMap *heightMapCreateDefault(ZoneMapLayer *layer, int lod, S32 map_x, S32 map_z);

void layerHeightmapCalcOcclusion(HeightMap *height_map);

const char *terrainBlockGetName(TerrainBlockRange *range);
int terrainBlockGetIndex(TerrainBlockRange *range);
void terrainBlockGetExtents(TerrainBlockRange *range, IVec2 min, IVec2 max);
bool terrainBlockSaveSource(TerrainBlockRange *block, int blocknum, int loaded_lod, StashTable heightmaps);
bool terrainBlockValidateSource(ZoneMapLayer *layer, TerrainBlockRange *block);
bool terrainBlockCommitSource(ZoneMapLayer *layer, TerrainBlockRange *block);
void terrainLoadRivers(ZoneMapLayer *layer, RiverList *list);
void terrainBlockLoadSource(ZoneMapLayer *layer, TerrainBlockRange *range, bool neighbors);
int terrainBlockCheckTimestamps(ZoneMapLayer *layer, TerrainBlockRange *block);

void terrainSubdivObjectDensity(TerrainObjectDensity *buffer_data, int stride, int size_x, int size_y );

bool heightMapValidateMaterials(HeightMap *height_map, int *lookup, char **material_table); // Debug only

Model *heightMapGenerateModel(HeightMap *height_maps[3][3], char **material_table, int *material_lookup, bool dynamic_normals);
// Terrain object grouptree wrappers

void layerInitObjectWrappers(ZoneMapLayer *layer, int table_size);
void layerFreeObjectWrapperEntries(TerrainObjectWrapper *wrapper);
void layerUpdateObjectWrapperEntries(TerrainObjectWrapper *wrapper);
void layerFreeObjectWrapper(TerrainObjectWrapper *wrapper);
TerrainObjectWrapper *layerCreateObjectWrapper(ZoneMapLayer *layer, int index);

// Reload all terrain atlases and models, keeping extra model PackData
// around so that vertex lighting calculations can be performed.
void atlasReloadEverythingForLightBin(HeightMapAtlas *atlas, bool keepPackData);
