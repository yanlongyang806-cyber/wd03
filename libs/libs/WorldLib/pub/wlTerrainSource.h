#pragma once
GCC_SYSTEM

#ifndef NO_EDITORS

#include "ReferenceSystem.h"
#include "wlTerrain.h"
#include "WorldLibEnums.h"

typedef struct TerrainEditorSource TerrainEditorSource;
typedef struct ZoneMapLayer ZoneMapLayer; 
typedef struct GenesisZoneNodeLayout GenesisZoneNodeLayout;
typedef struct TerrainImageBuffer TerrainImageBuffer;
typedef struct VaccuformObject VaccuformObject;
typedef struct TerrainCompiledMultiBrush TerrainCompiledMultiBrush;
typedef struct GenesisToPlaceObject GenesisToPlaceObject;
typedef struct GenesisZoneExterior GenesisZoneExterior;

typedef struct TerrainChangeList
{
	int ref_count;
	TerrainMaterialChangeList *mat_change_list;
    TerrainMaterialChangeList added_mat_list;
	HeightMapBackupList *map_backup_list;	
	int draw_lod;
    bool keep_high_res;
} TerrainChangeList;

typedef struct TerrainEditorSourceLayer
{
	TerrainEditorSource	*		source;
    ZoneMapLayer *				layer;
    ZoneMapLayer *				dummy_layer;
	ZoneMapLayerMode			effective_mode;

	int							loaded_lod;
    TerrainBlockRange**			blocks;

	//River**						rivers;

    bool						playable;
	TerrainExclusionVersion		exclusion_version;
	F32							color_shift;

	bool						loading;
    bool						loaded;
	bool						saved;
    
    HeightMapTracker **			heightmap_trackers;
    int *						material_lookup;
    int *						object_lookup;
	bool						disable_normals; // turns off normal and tangent space generation

    TerrainMaterialChangeList *	added_mat_list; // List of heightmaps who have newly added materials
} TerrainEditorSourceLayer;

typedef struct TerrainEditorSource
{
    TerrainEditorSourceLayer **	layers;

    StashTable					heightmaps;

	char **						material_table;
	TerrainObjectEntry **		object_table;

	int							editing_lod;
	int							visible_lod;

	int							unlock_on_save;

    bool						objects_hidden;

	bool						lock_cursor_position;

	//Brush Images
	TerrainImageBuffer **		brush_images;

	//Genesis Items
	GroupDef **					loaded_objects;

    TerrainEditorSourceLayer**	finish_save_layers;
} TerrainEditorSource;

typedef struct VaccuformObjectInternal
{
	F32 falloff;
	int vert_count;
	int tri_count;
	F32 *verts;
	U32 *inds;
	Vec3 min;
	Vec3 max;
	int buffer_width;
	int buffer_height;
	F32 *buffers;
	TerrainCompiledMultiBrush *multibrush;
} VaccuformObjectInternal;

typedef void (*progress_callback)(U32 id, S32 steps);

// Source functions
TerrainEditorSource *terrainSourceInitialize();
void terrainSourceDestroy(TerrainEditorSource *source);

void terrainSourceGetTouchingHeightMaps(TerrainEditorSource *source, F32 x, F32 z, int max_out_maps, int* num_out_maps, HeightMap* out_maps[], int out_map_x[], int out_map_y[], bool color_lod, HeightMapCache *cache, bool editable_only);
HeightMap* terrainSourceGetHeightMap(TerrainEditorSource *source, IVec2 local_pos);
TerrainEditorSourceLayer *terrainSourceGetHeightMapLayer(TerrainEditorSource *source, HeightMap *height_map);
TerrainBlockRange *terrainSourceGetBlockAt(TerrainEditorSource *source, F32 x, F32 z);
void terrainSourceGetNeighborLayers(TerrainEditorSource *source, HeightMap *height_map,
                                    ZoneMapLayer *out_layers[4], int block_idxs[4], bool include_self);
void terrainSetLockedEdges(TerrainEditorSource *source, bool lock_edges);
void terrainSourceRefreshHeightmaps(TerrainEditorSource *source, bool textures_only);
void terrainSourceModifyHeightmaps(TerrainEditorSource *source);

int terrainSourceGetObjectIndex( SA_PARAM_NN_VALID TerrainEditorSource *source, int uid, int seed, bool create);
int terrainSourceGetMaterialIndex( SA_PARAM_NN_VALID TerrainEditorSource *source, const char *material, bool create);

bool terrainSourceGetInterpolatedHeight(TerrainEditorSource *source, F32 x, F32 z, F32 *height, HeightMapCache *cache );
bool terrainSourceGetInterpolatedNormal(TerrainEditorSource *source, F32 x, F32 z, U32 lod, U8Vec3 normal, HeightMapCache *cache);
bool terrainSourceGetInterpolatedSoilDepth(TerrainEditorSource *source, F32 x, F32 z, F32 *depth, HeightMapCache *cache);

bool terrainSourceGetHeight(TerrainEditorSource *source, F32 x, F32 z, F32 *height, HeightMapCache *cache );
bool terrainSourceGetNormal(TerrainEditorSource *source, F32 x, F32 z, U8 normal[3], HeightMapCache *cache );
bool terrainSourceGetSelection(TerrainEditorSource *source, F32 x, F32 z, F32 *sel, HeightMapCache *cache );
bool terrainSourceGetAttribute(TerrainEditorSource *source, F32 x, F32 z, U8 *attr, HeightMapCache *cache );
bool terrainSourceGetShadow(TerrainEditorSource *source, F32 x, F32 z, F32 *sel, HeightMapCache *cache );
bool terrainSourceGetSoilDepth(TerrainEditorSource *source, F32 x, F32 z, F32 *depth, HeightMapCache *cache );
bool terrainSourceGetColor(TerrainEditorSource *source, F32 x, F32 z, Color *color, HeightMapCache *cache );
TerrainMaterialWeight *terrainSourceGetMaterialWeights(TerrainEditorSource *source, F32 x, F32 z, const char *material_names[NUM_MATERIALS], int *material_count);
F32 terrainSourceGetMaterialWeight(TerrainEditorSource *source, F32 x, F32 z, U8 global_mat);
void terrainSourceGetObjectDensities(TerrainEditorSource *source, F32 x, F32 z,
                                     TerrainObjectEntry ***entries, U32 **densities);
F32 terrainSourceGetObjectDensity(TerrainEditorSource *source, F32 x, F32 z, U8 obj);

void terrainSourceDrawHeight(TerrainEditorSource *source, F32 x, F32 z, int draw_lod, F32 delta_height, HeightMapCache *cache);
void terrainSourceDrawSoilDepth(TerrainEditorSource *source, F32 x, F32 z, int draw_lod, F32 delta_soil_depth, HeightMapCache *cache);
void terrainSourceDrawColor(TerrainEditorSource *source, F32 x, F32 z, U32 draw_lod, Color color, F32 strength, HeightMapCache *cache);
void terrainSourceDrawAlpha(TerrainEditorSource *source, F32 x, F32 z, U32 lod, U8 alpha);
void terrainSourceDrawOcclusion(TerrainEditorSource *source, F32 x, F32 z, bool reverse, S32 *last_draw_pos);
void terrainSourceDrawMaterial(TerrainEditorSource *source, F32 x, F32 z, int draw_lod, U8 global_mat, F32 strength, HeightMapCache *cache);
void terrainSourceReplaceMaterial(TerrainEditorSource *source, F32 x, F32 z, int draw_lod, U8 global_mat_old, U8 global_mat_new, F32 strength, HeightMapCache *cache);
void terrainSourceDrawObjects(TerrainEditorSource *source, F32 x, F32 z, int draw_lod, U8 global_object, TerrainObjectDensity density, F32 strength, HeightMapCache *cache);
void terrainSourceDrawAllExistingObjects(TerrainEditorSource *source, F32 x, F32 z, int draw_lod, TerrainObjectDensity density, HeightMapCache *cache);
void terrainSourceDrawSelection(TerrainEditorSource *source, F32 x, F32 z, int draw_lod, F32 delta_sel, HeightMapCache *cache);
void terrainSourceDrawTerrainType(TerrainEditorSource *source, F32 x, F32 z, U32 draw_lod, U8 type, F32 strength, HeightMapCache *cache);

void terrainSourceDoVaccuform(TerrainEditorSource *source, VaccuformObjectInternal *object);
void terrainSourceFinishVaccuform(TerrainEditorSource *source, VaccuformObjectInternal **objects);

bool terrainSourceStitchNeighbors(TerrainEditorSource *source);

bool terrainSourceDoRayCast(TerrainEditorSource *source, Vec3 start, Vec3 end, Vec3 out_pos, HeightMap **out_heightmap);

void terrainSourceUpdateTrackers(TerrainEditorSource *source, bool force, bool update_collision);
void terrainSourceBrushMouseUp(TerrainEditorSource *source, TerrainChangeList *change_list);

void terrainSourceUndoBackupBuffer(TerrainEditorSource *source, HeightMapBackupList *map_backup_list);

// Layer functions
void terrainSourceLayerSetUnsaved(TerrainEditorSourceLayer *layer);
TerrainEditorSourceLayer *terrainSourceAddLayer(TerrainEditorSource *source, ZoneMapLayer *layer);
int terrainSourceLoadLayerData(TerrainEditorSourceLayer *layer, bool clear_map_cache, bool open_trackers, progress_callback callback, U32 progress_id);
void terrainSourceFinishLoadLayerData(int iPartitionIdx, TerrainEditorSourceLayer *layer);
void terrainSourceUnloadLayerData(TerrainEditorSourceLayer *layer);
void terrainSourceBeginSaveLayer(TerrainEditorSourceLayer *layer);
bool terrainSourceSaveLayer(TerrainEditorSourceLayer *layer, bool force);
void terrainSourceFinishSaveLayer(TerrainEditorSourceLayer *layer);

bool terrainSourceCreateBlock(int iPartitionIdx, TerrainEditorSourceLayer *layer, IVec2 min_block, IVec2 max_block, bool do_checkout, bool create_trackers);
void terrainSourceSetLOD(TerrainEditorSourceLayer *layer, U32 lod);
void terrainSourceLayerResample(TerrainEditorSourceLayer *layer, U32 lod);
bool terrainSourceModifyLayers(int iPartitionIdx, TerrainEditorSourceLayer **layers, TerrainEditorSourceLayer *dest_layer,
                               IVec2 min, IVec2 max);

bool terrainSourceOptimizeLayer(TerrainEditorSourceLayer *layer, TerrainBlockRange **new_blocks, bool do_checkout);

void terrainSourceUpdateNormals(TerrainEditorSourceLayer *layer, bool force);
void terrainSourceUpdateColors(TerrainEditorSourceLayer *layer, bool force);
void terrainSourceUpdateAllObjects(TerrainEditorSourceLayer *layer, bool force, bool create_entries);
void terrainSourceUpdateObjectsByDef(TerrainEditorSourceLayer *layer, GroupDef *def);
void terrainSourceClearTouchedBits(TerrainEditorSourceLayer *layer);

void terrainUndoMaterialChanges(ZoneMapLayer *layer, TerrainMaterialChangeList *list, bool is_undo);

void terrainSourceSetVisibleLOD(TerrainEditorSource *source, int lod);

void terrainSourceChangeListDestroy(TerrainChangeList *change_list);

int terrainChangeListGetMemorySize(TerrainChangeList *change_list);

#endif
