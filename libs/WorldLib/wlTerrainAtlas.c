#include "ScratchStack.h"
#include "hoglib.h"
#include "serialize.h"
#include "ThreadManager.h"
#include "qsortG.h"
#include "timing.h"
#include "StringCache.h"
#include "MemRef.h"
#include "file.h"
#include "EString.h"
#include "ContinuousBuilderSupport.h"
#include "Color.h"

#include "wlSaveDXT.h"
#include "wlTerrainPrivate.h"
#include "wlState.h"
#include "WorldCellStreamingPrivate.h"
#include "WorldCellStreaming.h"
#include "wlModelBinning.h"


#define DEBUG_COLOR_TEXTURES 0

#define ATLAS_MAX_DESIRED_LOD 3

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Terrain_System););
AUTO_RUN_ANON(memBudgetAddMapping("ThreadStack:terrainAtlasThreadFunc", BUDGET_Terrain_System););

#define TERRAIN_ATLAS_LOG "terrain_atlasing.log"

static CRITICAL_SECTION terrain_atlas_bins_cs;
static int heightmap_atlas_size[] = { 1, 2, 4, 8, 16, 32 };
bool terrain_atlas_thread_started;

static F32 terrain_lod_initial_tri_percent = 0.25f;
static F32 terrain_high_lod_tri_percent = 0.33f;

static bool terrain_atlas_critical_section_inited;
static CRITICAL_SECTION terrain_atlas_critical_section;

static const U32 color_datasize = 4 * sizeof(U8) * SQR(HEIGHTMAP_ATLAS_COLOR_SIZE);
static const U32 color_dxt_datasize = SQR(ATLAS_COLOR_TEXTURE_SIZE) / 2; // DXT1 compressed

static bool disable_threaded_binning, disable_terrain_background_loading, disable_terrain_material_merge;
static bool terrain_atlas_do_logging, terrain_seam_logging, write_debug_collision_data;
static bool terrain_atlas_high_color;
static FILE *terrain_atlas_log;
bool disable_terrain_collision;

extern bool g_TerrainUseOptimalVertPlacement;
extern F32 g_TerrainScaleByArea;

// turns off threaded terrain atlas binning
AUTO_CMD_INT(disable_threaded_binning, terrainDisableThreadedAtlasBinning) ACMD_CATEGORY(Debug) ACMD_CMDLINE;

// turns off threaded terrain loading
AUTO_CMD_INT(disable_terrain_background_loading, terrainDisableBackgroundStreaming) ACMD_CATEGORY(Debug) ACMD_CMDLINE;

// turns off terrain collision
AUTO_CMD_INT(disable_terrain_collision, terrainDisableCollision) ACMD_CATEGORY(Debug) ACMD_CMDLINE;

// turns off merging of terrain materials
AUTO_CMD_INT(disable_terrain_material_merge, terrainDisableMaterialMerging) ACMD_CATEGORY(Debug) ACMD_CMDLINE;

// writes terrain collision data (Novodex cooked format) during binning to the temp directory
AUTO_CMD_INT(write_debug_collision_data, terrainDebugCollision) ACMD_CATEGORY(Debug) ACMD_CMDLINE;

// logs terrain atlasing operations
AUTO_CMD_INT(terrain_atlas_do_logging, terrainLogAtlasing) ACMD_CATEGORY(Debug) ACMD_CMDLINE;

// logs terrain seaming operations
AUTO_CMD_INT(terrain_seam_logging, terrainLogSeaming) ACMD_CATEGORY(Debug) ACMD_CMDLINE;

// Sets the triangle percentage for the base lod.  Each subsequent lod is divied again by 4.  Defaults to 0.25 (25 percent).
AUTO_CMD_FLOAT(terrain_lod_initial_tri_percent, terrainLodInitialTriPercent) ACMD_CATEGORY(Debug, Performance) ACMD_CMDLINE;

// Sets the triangle percentage to use for the high LOD terrain when binning.  Defaults to 0.33 (33 percent).
AUTO_CMD_FLOAT(terrain_high_lod_tri_percent, terrainHighLodTriPercent) ACMD_CATEGORY(Debug, Performance) ACMD_CMDLINE;

// allows saving of the highest LOD of color textures to the terrain atlases
AUTO_CMD_INT(terrain_atlas_high_color, terrainAtlasHighColor) ACMD_CATEGORY(Debug) ACMD_CMDLINE;


static IVec3 save_debug_terrain_atlas_mesh = {-10000, -10000, -10000};

AUTO_COMMAND ACMD_CATEGORY(Debug) ACMD_CMDLINE;
void terrainSaveDebugAtlasMesh(char *x, char *y, int lod)
{
	int ix, iy;
	if (strStartsWith(x, "neg"))
		ix = -atoi(x+3);
	else
		ix = atoi(x);
	if (strStartsWith(y, "neg"))
		iy = -atoi(y+3);
	else
		iy = atoi(y);
	setVec3(save_debug_terrain_atlas_mesh, ix*(1<<lod), iy*(1<<lod), lod);
}

#define atlasFindForBinning(atlas_list, world_pos, lod, thread_params) atlasFindByLocationEx(atlas_list, world_pos, lod, NULL, NULL, g_AtlasThreadProcInfo.layers, g_AtlasThreadProcInfo.min_local_pos, g_AtlasThreadProcInfo.max_local_pos, thread_params, g_AtlasThreadProcInfo.atlases)

static void __stdcall backgroundLoadAtlas(GeoRenderInfo *dummy_info, void *parent_unused, int param_load_vtx_light);

AUTO_ENUM;
typedef enum TerrainAtlasTiming
{
	TAT_LOADING,
	TAT_SEAMING,
	TAT_MERGING,
	TAT_REDUCING,
	TAT_COMPRESSING,
	TAT_CONVERTING,
	TAT_WRITING,

	TAT_COUNT
} TerrainAtlasTiming;

#include "wlTerrainAtlas_c_ast.c"

typedef struct PerThreadAtlasParams PerThreadAtlasParams;
typedef struct PerThreadAtlasParams
{
	IVec2 min, max;
	void (*func)(PerThreadAtlasParams *params);
	U32 timer_id;
	F32 timings[TAT_COUNT];
	F32 total_time;
} PerThreadAtlasParams;

typedef struct CombinedAtlasBinStruct
{
	int lod, remaining, thread_count;
	ZoneMapLayer **layers;
	ZoneMapLayer **playable_layers;
	Vec2 terrain_playable_min;//Only used if there are no playable layers.
	Vec2 terrain_playable_max;
	IVec2 min_local_pos, max_local_pos;
	SimpleBuffer **atlas_output_list;
	HeightMapBinAtlas **atlas_list;
	HogFile *terrain_atlas_hoggs[ATLAS_MAX_LOD+1];
	HogFile *terrain_model_hoggs[ATLAS_MAX_LOD+1];
	HogFile *terrain_light_model_hoggs[ATLAS_MAX_LOD+1];
	HogFile *terrain_coll_hogg;
	HeightMapAtlasRegionData *atlases;
} CombinedAtlasBinStruct;

static CombinedAtlasBinStruct g_AtlasThreadProcInfo;


#define ENABLE_TRACE_VERTEX_LIGHT_HOGG 0
#if ENABLE_TRACE_VERTEX_LIGHT_HOGG
static void atlasTraceHoggf(const char * format, ...)
{
	va_list va;
	char buf[1024]={0};

	va_start(va, format);
	vsprintf(buf, format, va);
	va_end(va);

	if (IsDebuggerPresent())
	{
		OutputDebugString(buf);
	} 
}
#else
#define atlasTraceHoggf(fmt, ...)
#endif



typedef struct AtlasLoadData
{
	HogFile *hog_file;
	char filename[256];
} AtlasLoadData;

// Neighbors are ordered as follows:
// +Y ^
//    |  0 3 5
//    |  1 X 6
//    |  2 4 7 
//    +----------> +X

static IVec2 neighbor_offsets[8] = {
	{ -1,  1 },
	{ -1,  0 },
	{ -1, -1 },
	{  0,  1 },
	{  0, -1 },
	{  1,  1 },
	{  1,  0 },
	{  1, -1 }
};

// Corners are ordered as follows:
// +Y ^
//    | 2,3
//    | 0,1
//    +-----> +X
//
// = px+py*2
//

static const char **getCommonMaterials(const char **m1, const char **m2)
{
	const char **detail_material_names = NULL;
	int i, j;

	for (i = 0; i < eaSize(&m1); ++i)
	{
		for (j = 0; j < eaSize(&m2); ++j)
		{
			if (m1[i] == m2[j])
			{
				eaPushUnique(&detail_material_names, m1[i]);
				break;
			}
		}
	}

	return detail_material_names;
}

static void transformVarColor(U8 *cOut, const char **out_materials, const U8 *cIn, const char **in_materials)
{
	int i, sizeOut = eaSize(&out_materials);
	ZeroStructsForce(cOut, sizeOut);

	for (i = 0; i < sizeOut; ++i)
	{
		const char *material_name = out_materials[i];
		int input_idx = eaFind(&in_materials, material_name);
		if (input_idx >= 0)
			cOut[i] = cIn[input_idx];
	}
}

static Color transformVarColorToColor(const U8 *cIn, const TerrainMeshRenderMaterial *material)
{
	Color c;
	int i;

	c.integer_for_equality_only = 0;

	for (i = 0; i < 3; ++i)
	{
		if (material->color_idxs[i] < 4 && material->detail_material_ids[i] < 255)
			c.rgba[material->color_idxs[i]] = cIn[material->detail_material_ids[i]];
	}

	ColorNormalize(&c);
	return c;
}

static void filterVarColor(U8 *cOut, const U8 *cIn, const char **local_materials, const char **common_materials)
{
	int i, size = eaSize(&local_materials);
	for (i = 0; i < size; ++i)
	{
		if (eaFind(&common_materials, local_materials[i]) < 0)
			cOut[i] = 0;
		else
			cOut[i] = cIn[i];
	}
	VarColorNormalize(cOut, size);
}

/********************************
   Run-time Atlasing Functions
 ********************************/

int heightmapAtlasGetLODSize(int lod)
{
	return heightmap_atlas_size[lod];
}

void atlasFree(HeightMapAtlas *atlas)
{
	if (!atlas)
		return;

	assert(!atlas->data);
	assert(atlas->load_state == WCS_CLOSED);

	StructDestroy(parse_HeightMapAtlas, atlas);
}

//////////////////////////////////////////////////////////////////////////

// returns the LOD of the active parent or child of the given atlas that is closest to the given position
static int atlasGetActiveLOD(SA_PARAM_NN_VALID HeightMapAtlas *atlas, const IVec2 local_pos)
{
	HeightMapAtlas *iter;

	for (iter = atlas; iter; iter = iter->parent)
	{
		if (iter->atlas_active)
			return iter->lod;
	}

	iter = atlas;
	while (iter && !iter->atlas_active)
	{
		// recurse to children
		int child_idx = ((local_pos[0] > iter->local_pos[0]) ? 1 : 0) + ((local_pos[1] > iter->local_pos[1]) ? 2 : 0);
		if (iter->children[child_idx])
			iter = iter->children[child_idx];
		else
			iter = NULL;
	}

	return iter ? iter->lod : atlas->lod;
}

static void atlasMatchNeighborLOD(SA_PARAM_NN_VALID HeightMapAtlas *atlas, SA_PARAM_NN_VALID HeightMapAtlas *neighbor)
{
	HeightMapAtlas *iter;

	for (iter = atlas; iter; iter = iter->parent)
	{
		if (iter->atlas_active)
		{
			// TODO(CD) change corner_lod values
			return;
		}
	}

	iter = atlas;
	while (iter && !iter->atlas_active)
	{
		// recurse to children
		int child_idx = ((neighbor->local_pos[0] > iter->local_pos[0]) ? 1 : 0) + ((neighbor->local_pos[1] > iter->local_pos[1]) ? 2 : 0);
		if (iter->children[child_idx])
			iter = iter->children[child_idx];
		else
			iter = NULL;
	}

	// TODO(CD) change corner_lod values

	return;
}

static void atlasSetInactive(HeightMapAtlas *atlas)
{
	if (atlas->data && atlas->data->client_data.light_cache)
		wl_state.invalidate_light_cache_func((GfxLightCacheBase *)atlas->data->client_data.light_cache, LCIT_ALL);
	if (atlas->atlas_active)
		atlas->atlas_active = false;

	// recurse down
	if (atlas->children[0])
		atlasSetInactive(atlas->children[0]);
	if (atlas->children[1])
		atlasSetInactive(atlas->children[1]);
	if (atlas->children[2])
		atlasSetInactive(atlas->children[2]);
	if (atlas->children[3])
		atlasSetInactive(atlas->children[3]);
}

static void validateAtlasActiveRecurse(HeightMapAtlas *atlas, int any_ancestor_atlas_active)
{
	if (atlas->atlas_active)
		assert(!any_ancestor_atlas_active);

	any_ancestor_atlas_active = any_ancestor_atlas_active || atlas->atlas_active;

	// recurse down
	if (atlas->children[0])
		validateAtlasActiveRecurse(atlas->children[0], any_ancestor_atlas_active);
	if (atlas->children[1])
		validateAtlasActiveRecurse(atlas->children[1], any_ancestor_atlas_active);
	if (atlas->children[2])
		validateAtlasActiveRecurse(atlas->children[2], any_ancestor_atlas_active);
	if (atlas->children[3])
		validateAtlasActiveRecurse(atlas->children[3], any_ancestor_atlas_active);
}

static void validateAtlasActive(HeightMapAtlas *atlas)
{
	validateAtlasActiveRecurse(atlas, false);
}

static void validateAtlasParentsActive(HeightMapAtlas *atlas)
{
	HeightMapAtlas *check_parent;
	if (atlas->atlas_active)
	{
		check_parent = atlas->parent;
		while (check_parent)
		{
			// can't activate mid-tree

			assert(!check_parent->atlas_active);
			check_parent = check_parent->parent;
		}
	}
}

__forceinline static void atlasSetActiveAndDeactivateChildren(HeightMapAtlas *atlas)
{
	if (!atlas->atlas_active && atlas->data && atlas->data->client_data.light_cache)
		wl_state.invalidate_light_cache_func((GfxLightCacheBase *)atlas->data->client_data.light_cache, LCIT_ALL);
	atlas->atlas_active = true;

	// recurse down
	if (atlas->children[0])
		atlasSetInactive(atlas->children[0]);
	if (atlas->children[1])
		atlasSetInactive(atlas->children[1]);
	if (atlas->children[2])
		atlasSetInactive(atlas->children[2]);
	if (atlas->children[3])
		atlasSetInactive(atlas->children[3]);
}

static void atlasReduceLOD(HeightMapAtlas *atlas, int lod)
{
	if (atlas->lod >= lod)
	{
		atlasSetActiveAndDeactivateChildren(atlas);
		return;
	}

	// recurse up
	if (atlas->parent)
		atlasReduceLOD(atlas->parent, lod);
}

static ZoneMapLayer *atlasGetLayer(HeightMapAtlas *atlas)
{
	int i, j, k;
	if (!atlas->data || !atlas->data->region)
		return NULL;
	for (k = 0; k < eaSize(&atlas->data->region->atlases->layer_names); ++k)
	{
		if (atlas->layer_bitfield & (1 << k))
		{
			for (i = 0; i < eaSize(&world_grid.maps); ++i)
			{
				ZoneMap *zmap = world_grid.maps[i];
				if (!zmap)
					continue;
				for (j = 0; j < eaSize(&zmap->layers); ++j)
				{
					ZoneMapLayer *layer = zmap->layers[j];
					if (!layer)
						continue;
					if (stricmp(layer->filename, atlas->data->region->atlases->layer_names[k])==0)
						return layer;
				}
			}

			break;
		}
	}
	return NULL;
}

static void atlasMergeClusterBoundsWithTerrainBounds(HeightMapAtlas *atlas)
{
	const HeightMapAtlasData * data = atlas->data;
	const Model * cluster_model = data->client_data.draw_model_cluster;
	F32 min_height = cluster_model->min[1];
	F32 max_height = cluster_model->max[1];
	bool boundsContained = false;

	while (atlas && !boundsContained)
	{
		boundsContained = true;
		if (atlas->min_height > min_height)
		{
			boundsContained = false;
			atlas->min_height = min_height;
		}
		if (atlas->max_height < max_height)
		{
			boundsContained = false;
			atlas->max_height = max_height;
		}
		atlas = atlas->parent;
	}
}

static bool atlasDataCheckModelLoaded(HeightMapAtlasData *data, Model *parent_model, bool keep_pack_data)
{
	bool modelIsLoaded = false;
	if (!parent_model)
		modelIsLoaded = true;
	else
	{
		ModelLOD *model = modelLODLoadAndMaybeWait(parent_model, 0, false);
		if (model && (model->data && wl_state.gfx_check_model_loaded_callback(model) || model->loadstate == GEO_LOADED_NULL_DATA))
		{
			data->client_data.video_memory_usage += modelLODGetBytesUnpacked(model);

			modelLODFreeUnpacked(model);
			if(!keep_pack_data)
				modelLODFreePacked(model);

			modelIsLoaded = true;
		}
	}
	return modelIsLoaded;
}

static bool atlasDataCheckModelLoadReadyToCancel(HeightMapAtlasData *data, Model *parent_model)
{
	bool loadReadyForCancel = false;
	if (parent_model && parent_model->model_lods && parent_model->model_lods[0]->loadstate == GEO_LOADING)
	{
		ModelLOD *model = modelLODLoadAndMaybeWait(parent_model, 0, false);
		if (model && (model->data || model->loadstate == GEO_LOADED_NULL_DATA))
			loadReadyForCancel = true;
	}
	else
	{
		loadReadyForCancel = true;
	}

	return loadReadyForCancel;
}

static void atlasRequestDataLoad(HeightMapAtlas *atlas, bool is_client, bool load_all, bool keep_pack_data, bool load_vertex_light)
{
	atlas->unload_request_time = 0;

	if (atlas->load_state == WCS_CLOSED)
	{
		if (atlas->lod == 0 || is_client)
		{
			atlas->data = calloc(1, sizeof(HeightMapAtlasData));

			// mark as loading, gives ownership to loading thread
			atlas->load_state = WCS_LOADING_BG;

			if (load_all || disable_terrain_background_loading)
			{
				filePushDiskAccessAllowedInMainThread(true);
				backgroundLoadAtlas((GeoRenderInfo *)atlas, NULL, load_vertex_light);
				filePopDiskAccessAllowedInMainThread();
			}
			else
			{
				geoRequestBackgroundExec(backgroundLoadAtlas, NULL, (GeoRenderInfo *)atlas, NULL, load_vertex_light, FILE_MEDIUM_PRIORITY);
			}
		}
		else
		{
			atlas->load_state = WCS_LOADING_FG;
		}
	}

	if (atlas->load_state == WCS_LOADING_FG)
	{
		if (is_client && wl_state.gfx_check_model_loaded_callback && (atlas->data->client_data.draw_model || atlas->data->client_data.draw_model_cluster))
		{
			HeightMapAtlasData * data = atlas->data;
			bool terrain_loaded = atlasDataCheckModelLoaded(data, data->client_data.draw_model, keep_pack_data);
			bool cluster_loaded = atlasDataCheckModelLoaded(data, data->client_data.draw_model_cluster, true);
			if (terrain_loaded && cluster_loaded)
			{
				if (data->client_data.draw_model_cluster)
					atlasMergeClusterBoundsWithTerrainBounds(atlas);
				atlas->load_state = WCS_LOADED;
			}
		}
		else
		{
			atlas->load_state = WCS_LOADED;
		}
	}

	if (atlas->lod == 0 && atlas->data->collision_model && !atlas->data->collision_entry && !disable_terrain_collision && !atlas->hidden)
		heightMapCreateCollision(atlas->data, atlasGetLayer(atlas));

	if (atlas->hidden && atlas->data->collision_entry)
		heightMapDestroyCollision(atlas->data);
}

static bool atlasUnloadData(HeightMapAtlas *atlas, U32 timestamp, bool is_client, bool load_one_level, bool force)
{
	bool unload_succeeded = true;

	if (load_one_level)
	{
		atlasRequestDataLoad(atlas, is_client, false, false, is_client);
	}
	else
	{
		if (atlas->load_state == WCS_CLOSED)
		{
			atlasSetInactive(atlas);
			return true;
		}

		if (!force && atlas->unload_request_time < timestamp - 1)
		{
			// only unload if requested two frames in a row
			atlas->unload_request_time = timestamp;
			atlasSetInactive(atlas);
			return false;
		}

		atlas->unload_request_time = timestamp;
	}

	if (atlas->children[0])
		unload_succeeded = atlasUnloadData(atlas->children[0], timestamp, is_client, false, force) && unload_succeeded;
	if (atlas->children[1])
		unload_succeeded = atlasUnloadData(atlas->children[1], timestamp, is_client, false, force) && unload_succeeded;
	if (atlas->children[2])
		unload_succeeded = atlasUnloadData(atlas->children[2], timestamp, is_client, false, force) && unload_succeeded;
	if (atlas->children[3])
		unload_succeeded = atlasUnloadData(atlas->children[3], timestamp, is_client, false, force) && unload_succeeded;

	atlas->atlas_active = false;

	if (load_one_level)
		return false;

	if (atlas->load_state == WCS_LOADING_FG)
	{
		// if either model is NULL, not actively loading, or has completed background load, then forward state to loaded 
		if (atlasDataCheckModelLoadReadyToCancel(atlas->data, atlas->data->client_data.draw_model) && 
			atlasDataCheckModelLoadReadyToCancel(atlas->data, atlas->data->client_data.draw_model_cluster))
			atlas->load_state = WCS_LOADED;
	}

	// children still loaded, or data is owned by loading thread
	if (!unload_succeeded || atlas->load_state != WCS_LOADED)
		return false;

	if (atlas->data && atlas->data->collision_model && 
			(atlas->data->collision_model->collision_data.loadstate == GEO_LOADING || 
			 atlas->data->collision_model->model_lods && atlas->data->collision_model->model_lods[0]->loadstate == GEO_LOADING))
	{
		modelLODLoadAndMaybeWait(atlas->data->collision_model, 0, false);
		return false;
	}

	if (atlas->data && atlas->data->client_data.occlusion_model && 
			atlas->data->client_data.occlusion_model->model_lods && atlas->data->client_data.occlusion_model->model_lods[0]->loadstate == GEO_LOADING)
	{
		modelLODLoadAndMaybeWait(atlas->data->client_data.occlusion_model, 0, false);
		return false;
	}

	if (atlas->data && atlas->data->client_data.static_vertex_light_model && 
		atlas->data->client_data.static_vertex_light_model->model_lods && atlas->data->client_data.static_vertex_light_model->model_lods[0]->loadstate == GEO_LOADING)
	{
		modelLODLoadAndMaybeWait(atlas->data->client_data.static_vertex_light_model, 0, false);
		return false;
	}

	heightMapAtlasDataFree(atlas->data);
	atlas->data = NULL;
	atlas->load_state = WCS_CLOSED;

	return true;
}

void atlasReloadEverythingForLightBin(HeightMapAtlas *atlas, bool keepPackedData) {

	static int depth = 0;
	int i;

	if(!depth) {
		// Clean up all the atlas data. This will also free all the models
		// that lack the PackData. This will recurse for us, so we only
		// want to do it at the top level.
		atlasUnloadData(atlas, 0, true, false, true);
	}

	// Load this level's models with PackData.
	atlasRequestDataLoad(atlas, true, true, keepPackedData, false);

	depth++;

	// Recurse to all the children.
	for(i = 0; i < 4; i++) {
		if(atlas->children[i])
			atlasReloadEverythingForLightBin(atlas->children[i], keepPackedData);
	}

	depth--;
}

static bool terrainLoadAtlasesForCameraPosInternal(int iPartitionIdx, HeightMapAtlas *atlas, const F32 *camera_positions, int camera_position_count, const BlockRange **terrain_editor_blocks, bool load_all, bool is_client, U32 timestamp)
{
	bool needs_subdivide = load_all, needs_child_load = false;
	int corner, cam, b;
	Vec2 corner_positions[4];

	atlas->hidden = false;

	// calculate LOD values for each corner
	for (corner = 0; corner < 4; ++corner)
	{
		F32 min_distance = 8e16, xdist, ydist, dist;
		F32 cur_lod_mul;

		setVec2(corner_positions[corner], 
			atlas->local_pos[0] + ((corner & 1)?heightmap_atlas_size[atlas->lod]:0), 
			atlas->local_pos[1] + ((corner & 2)?heightmap_atlas_size[atlas->lod]:0));
		scaleVec2(corner_positions[corner], GRID_BLOCK_SIZE, corner_positions[corner]);

		for (cam = 0; cam < camera_position_count; ++cam)
		{
			xdist = camera_positions[cam*3+0] - corner_positions[corner][0];
			ydist = camera_positions[cam*3+2] - corner_positions[corner][1];
			xdist = ABS(xdist);
			ydist = ABS(ydist);
			dist = MAX(xdist, ydist);
			MIN1(min_distance, dist);
		}

		// take distance to editable terrain blocks into account and hide any atlases that overlap editable blocks
		for (b = 0; b < eaSize(&terrain_editor_blocks); ++b)
		{
			const BlockRange *block = terrain_editor_blocks[b];
			Vec2 block_min, block_max;
			F32 xdist2, ydist2;

			if (!(	atlas->local_pos[0] > block->max_block[0] || atlas->local_pos[1] > block->max_block[2] || 
					atlas->local_pos[0] + heightmap_atlas_size[atlas->lod] <= block->min_block[0] || atlas->local_pos[1] + heightmap_atlas_size[atlas->lod] <= block->min_block[2]))
			{
				min_distance = 0;
				atlas->hidden = true;
				break;
			}

			setVec2(block_min, block->min_block[0] * GRID_BLOCK_SIZE, block->min_block[2] * GRID_BLOCK_SIZE);
			setVec2(block_max, (block->max_block[0]+1) * GRID_BLOCK_SIZE, (block->max_block[2]+1) * GRID_BLOCK_SIZE);

			xdist = block_min[0] - corner_positions[corner][0];
			xdist = ABS(xdist);
			xdist2 = block_max[0] - corner_positions[corner][0];
			xdist2 = ABS(xdist2);
			MIN1(xdist, xdist2);

			ydist = block_min[1] - corner_positions[corner][1];
			ydist = ABS(ydist);
			ydist2 = block_max[1] - corner_positions[corner][1];
			ydist2 = ABS(ydist2);
			MIN1(ydist, ydist2);

			dist = MAX(xdist, ydist);
			MIN1(min_distance, dist);
		}

		atlas->corner_lods[corner] = 0;
		cur_lod_mul = MAXF(wl_state.terrain_lod_scale, 1);
		while (	min_distance > GRID_BLOCK_SIZE * cur_lod_mul && 
				atlas->corner_lods[corner] < ATLAS_MAX_DESIRED_LOD)
		{
			min_distance -= GRID_BLOCK_SIZE * cur_lod_mul;
			cur_lod_mul *= 2;
			atlas->corner_lods[corner] += 1;
		}
//		atlas->corner_lods[corner] += min_distance / (GRID_BLOCK_SIZE * cur_lod_mul);

		if (atlas->corner_lods[corner] < atlas->lod)
			needs_subdivide = true;
		if (min_distance >= 0 && min_distance <= TERRAIN_ATLAS_LOAD_DISTANCE)
			needs_child_load = true;

		atlas->corner_lods[corner] = CLAMP(atlas->corner_lods[corner], atlas->lod, atlas->lod+1);
	}

	// check if any camera position is within this atlas's bounds
	for (cam = 0; !needs_subdivide && cam < camera_position_count; ++cam)
	{
		if (!(camera_positions[cam*3+0] < corner_positions[0][0] || 
			  camera_positions[cam*3+0] > corner_positions[1][0] ||
			  camera_positions[cam*3+2] < corner_positions[0][1] ||
			  camera_positions[cam*3+2] > corner_positions[2][1]))
		{
			needs_subdivide = true;
		}
	}

	if (needs_subdivide)
	{
		bool children_loaded = true;

		if (atlas->children[0])
			children_loaded = terrainLoadAtlasesForCameraPosInternal(iPartitionIdx, atlas->children[0], camera_positions, camera_position_count, terrain_editor_blocks, load_all, is_client, timestamp) && children_loaded;
		if (atlas->children[1])
			children_loaded = terrainLoadAtlasesForCameraPosInternal(iPartitionIdx, atlas->children[1], camera_positions, camera_position_count, terrain_editor_blocks, load_all, is_client, timestamp) && children_loaded;
		if (atlas->children[2])
			children_loaded = terrainLoadAtlasesForCameraPosInternal(iPartitionIdx, atlas->children[2], camera_positions, camera_position_count, terrain_editor_blocks, load_all, is_client, timestamp) && children_loaded;
		if (atlas->children[3])
			children_loaded = terrainLoadAtlasesForCameraPosInternal(iPartitionIdx, atlas->children[3], camera_positions, camera_position_count, terrain_editor_blocks, load_all, is_client, timestamp) && children_loaded;

		if (!children_loaded || (!atlas->children[0] && !atlas->children[1] && !atlas->children[2] && !atlas->children[3]))
		{
			atlasSetActiveAndDeactivateChildren(atlas);

			// rectify LODs with neighbors to the left and below (which have already been touched by this function)

			if (atlas->neighbors[1])
			{
				int neighbor_lod = atlasGetActiveLOD(atlas->neighbors[1], atlas->local_pos);
				if (neighbor_lod > atlas->lod + 1)
					atlas->atlas_active = false;
				else if (neighbor_lod < atlas->lod - 1)
					atlasReduceLOD(atlas->neighbors[1], atlas->lod - 1);
			}

			if (atlas->neighbors[2])
			{
				int neighbor_lod = atlasGetActiveLOD(atlas->neighbors[2], atlas->local_pos);
				if (neighbor_lod > atlas->lod + 1)
					atlas->atlas_active = false;
				else if (neighbor_lod < atlas->lod - 1)
					atlasReduceLOD(atlas->neighbors[2], atlas->lod - 1);
			}

			if (atlas->neighbors[4])
			{
				int neighbor_lod = atlasGetActiveLOD(atlas->neighbors[4], atlas->local_pos);
				if (neighbor_lod > atlas->lod + 1)
					atlas->atlas_active = false;
				else if (neighbor_lod < atlas->lod - 1)
					atlasReduceLOD(atlas->neighbors[4], atlas->lod - 1);
			}

			if (atlas->atlas_active)
			{
				if (atlas->neighbors[1])
					atlasMatchNeighborLOD(atlas->neighbors[1], atlas);
				if (atlas->neighbors[2])
					atlasMatchNeighborLOD(atlas->neighbors[2], atlas);
				if (atlas->neighbors[4])
					atlasMatchNeighborLOD(atlas->neighbors[4], atlas);
			}
		}
		else
		{
			atlas->atlas_active = false;
		}
	}
	else
	{
		if (atlas->children[0])
			atlasUnloadData(atlas->children[0], timestamp, is_client, needs_child_load, false);
		if (atlas->children[1])
			atlasUnloadData(atlas->children[1], timestamp, is_client, needs_child_load, false);
		if (atlas->children[2])
			atlasUnloadData(atlas->children[2], timestamp, is_client, needs_child_load, false);
		if (atlas->children[3])
			atlasUnloadData(atlas->children[3], timestamp, is_client, needs_child_load, false);

		atlasSetActiveAndDeactivateChildren(atlas);
	}

	atlasRequestDataLoad(atlas, is_client, load_all, false, is_client);

	if (atlas->atlas_active && atlas->load_state != WCS_LOADED)
	{
		// this atlas wants to be active but is not loaded yet, tell parent it needs to be active instead
		atlas->atlas_active = false;
		return false;
	}

	if (atlas->atlas_active && atlas->lod == 0 && atlas->data->collision_entry && !worldCellEntryIsPartitionOpen(&atlas->data->collision_entry->base_entry, iPartitionIdx))
		worldCellEntryOpen(iPartitionIdx, &atlas->data->collision_entry->base_entry, atlas->data->region);

	return true;
}

void terrainLoadAtlasesForCameraPos(int iPartitionIdx, HeightMapAtlasRegionData *atlases, const F32 *camera_positions, int camera_position_count, const BlockRange **terrain_editor_blocks, bool load_all, U32 timestamp)
{
	bool is_client = wlIsClient();
	int i;
	
	if (!atlases || !camera_position_count)
		return;

	// CD: These need to be traversed in increasing x, then increasing y order.  They should be binned in that order.
	for (i = 0; i < eaSize(&atlases->root_atlases); ++i)
		terrainLoadAtlasesForCameraPosInternal(iPartitionIdx, atlases->root_atlases[i], camera_positions, camera_position_count, terrain_editor_blocks, load_all, is_client, timestamp);
}

static void terrainCloseAtlasesForPartitionInternal(int iPartitionIdx, HeightMapAtlas *atlas)
{
	// Recurse into children
	if (atlas->children[0])
		terrainCloseAtlasesForPartitionInternal(iPartitionIdx, atlas->children[0]);
	if (atlas->children[1])
		 terrainCloseAtlasesForPartitionInternal(iPartitionIdx, atlas->children[1]);
	if (atlas->children[2])
		terrainCloseAtlasesForPartitionInternal(iPartitionIdx, atlas->children[2]);
	if (atlas->children[3])
		terrainCloseAtlasesForPartitionInternal(iPartitionIdx, atlas->children[3]);

	// Close cell entry
	if (atlas->atlas_active && atlas->lod == 0 && atlas->data->collision_entry)
		worldCellEntryClose(iPartitionIdx, &atlas->data->collision_entry->base_entry);
}

void terrainCloseAtlasesForPartition(int iPartitionIdx, HeightMapAtlasRegionData *atlases)
{
	int i;

	if (!atlases)
		return;

	// CD: These need to be traversed in increasing x, then increasing y order.  They should be binned in that order.
	for (i = 0; i < eaSize(&atlases->root_atlases); ++i)
		terrainCloseAtlasesForPartitionInternal(iPartitionIdx, atlases->root_atlases[i]);
}

void terrainUnloadAtlases(HeightMapAtlasRegionData *atlases)
{
	bool all_data_unloaded, is_client;
	int i;

	if (!atlases)
		return;

	all_data_unloaded = false;
	is_client = wlIsClient();
	while (!all_data_unloaded)
	{
		all_data_unloaded = true;

		for (i = 0; i < eaSize(&atlases->root_atlases); ++i)
			all_data_unloaded = atlasUnloadData(atlases->root_atlases[i], 0, is_client, false, true) && all_data_unloaded;

		if (!all_data_unloaded)
			Sleep(5); // let background loader run
	}
}

//////////////////////////////////////////////////////////////////////////

static FILE *atlasLoadModelData(Model *model, AtlasLoadData *load_data)
{
	char filename[MAX_PATH];
	const char *archive_name = hogFileGetArchiveFileName(load_data->hog_file);
	atlasTraceHoggf("Loading from hogg 0x%p \"%s\" \"%s\"\n", load_data->hog_file, archive_name, load_data->filename);
	errorIsDuringDataLoadingInc(archive_name);
	sprintf(filename, "#%s#%s", archive_name, load_data->filename);
	return fopen(filename, "rb");
}

static void atlasLoadModels(HeightMapAtlasData *atlas_data, const IVec2 local_pos, SimpleBufHandle header_buf, SimpleBufHandle light_header_buf, HogFile *model_hogg_file, HogFile *light_model_hogg_file, HogFile *coll_hogg_file, bool is_tile)
{
	U8 i, detail_material_count;
	AtlasLoadData *load_data;

	assert(!atlas_data->client_data.draw_model);
	assert(!atlas_data->client_data.static_vertex_light_model);
	assert(!atlas_data->client_data.occlusion_model);
	assert(!atlas_data->collision_model);

	// materials
	SimpleBufReadU8(&detail_material_count, header_buf);
	assert(!atlas_data->client_data.model_detail_material_names);
	eaSetSize(&atlas_data->client_data.model_detail_material_names, detail_material_count);
	if (detail_material_count)
	{
		for (i = 0; i < detail_material_count; ++i)
		{
			char *material_name;
			SimpleBufReadString(&material_name, header_buf);
			atlas_data->client_data.model_detail_material_names[i] = allocAddString(material_name);
		}
	}

	SimpleBufReadU8(&detail_material_count, header_buf);
	assert(!atlas_data->client_data.model_materials);
	if (detail_material_count)
	{
		eaSetSize(&atlas_data->client_data.model_materials, detail_material_count);
		for (i = 0; i < detail_material_count; ++i)
		{
			TerrainMeshRenderMaterial *material = calloc(1, sizeof(TerrainMeshRenderMaterial));
			SimpleBufReadU8(&material->detail_material_ids[0], header_buf);
			SimpleBufReadU8(&material->detail_material_ids[1], header_buf);
			SimpleBufReadU8(&material->detail_material_ids[2], header_buf);
			SimpleBufReadU8(&material->color_idxs[0], header_buf);
			SimpleBufReadU8(&material->color_idxs[1], header_buf);
			SimpleBufReadU8(&material->color_idxs[2], header_buf);
			atlas_data->client_data.model_materials[i] = material;
		}
	}

	// models
	if (wlIsClient())
	{
		load_data = malloc(sizeof(AtlasLoadData));
		load_data->hog_file = model_hogg_file;
		sprintf(load_data->filename, "x%d_z%d.mset", local_pos[0], local_pos[1]);

		atlas_data->client_data.draw_model = tempModelLoad(is_tile?TERRAIN_TILE_MODEL_NAME:TERRAIN_ATLAS_MODEL_NAME, header_buf, atlasLoadModelData, load_data, WL_FOR_TERRAIN);

		if (atlas_data->client_data.draw_model)
		{
			// load data was used, allocate a new one for the occlusion model
			load_data = memcpy(malloc(sizeof(AtlasLoadData)), load_data, sizeof(AtlasLoadData));
		}

		atlas_data->client_data.occlusion_model = tempModelLoad(is_tile?TERRAIN_TILE_OCC_MODEL_NAME:TERRAIN_ATLAS_OCC_MODEL_NAME, header_buf, atlasLoadModelData, load_data, WL_FOR_TERRAIN);

		if (atlas_data->client_data.occlusion_model)
		{
			// load data was used, allocate a new one for the light model
			load_data = memcpy(malloc(sizeof(AtlasLoadData)), load_data, sizeof(AtlasLoadData));
		}

		// Load the baked-in vertex lighting information. But only do so if the files for it exist.
		if(light_model_hogg_file && light_header_buf) {
			
			// Switch load_data to the light model hogg file (separate from other terrain bins).
			load_data->hog_file = light_model_hogg_file;

			// Light models also have a different file name.
			sprintf(load_data->filename, "x%d_z%d_lights.mset", local_pos[0], local_pos[1]);
			
			atlasTraceHoggf("Queuing load from hogg 0x%p \"%s\" \"%s\"\n", load_data->hog_file, hogFileGetArchiveFileName(light_model_hogg_file), load_data->filename);
			atlas_data->client_data.static_vertex_light_model = tempModelLoad(is_tile?TERRAIN_TILE_LIGHTING_MODEL_NAME:TERRAIN_ATLAS_LIGHTING_MODEL_NAME, light_header_buf, atlasLoadModelData, load_data, WL_FOR_TERRAIN);

			if(!atlas_data->client_data.static_vertex_light_model) {
				// Something went horribly wrong loading the terrain
				// light model. Continue as though there isn't a light
				// model. Clean up extra load_data.
				free(load_data);

				// Oh, and complain about it.
				Errorf("Terrain static light model load failed.");
			}
		} else {

			// Vertex lights unused. Clean up extra load_data.
			free(load_data);
		}
	}
	else
	{
		U32 header_size;

		SimpleBufReadU32(&header_size, header_buf);
		SimpleBufSeek(header_buf, header_size - sizeof(U32), SEEK_CUR);

		SimpleBufReadU32(&header_size, header_buf);
		SimpleBufSeek(header_buf, header_size - sizeof(U32), SEEK_CUR);
	}

	if (is_tile)
	{
		load_data = malloc(sizeof(AtlasLoadData));
		load_data->hog_file = coll_hogg_file;
		sprintf(load_data->filename, "x%d_z%d.mset", local_pos[0], local_pos[1]);

		atlas_data->collision_model = tempModelLoad(TERRAIN_TILE_COLL_MODEL_NAME, header_buf, atlasLoadModelData, load_data, WL_FOR_TERRAIN);
		if (!atlas_data->collision_model)
		{
			// load data was not used, free it
			free(load_data);
		}
	}
	else
		tempModelSkipLoadHeader(header_buf);

	// models
	if (wlIsClient())
	{
		if(light_model_hogg_file && light_header_buf && !SimpleBufAtEnd(light_header_buf))
		{
			load_data = malloc(sizeof(AtlasLoadData));
			load_data->hog_file = light_model_hogg_file;
			sprintf(load_data->filename, "x%d_z%d_lights.mset", local_pos[0], local_pos[1]);

			atlasTraceHoggf("Queuing load from hogg 0x%p \"%s\" \"%s\"\n", load_data->hog_file, hogFileGetArchiveFileName(light_model_hogg_file), load_data->filename);
			atlas_data->client_data.draw_model_cluster = tempModelLoad(TERRAIN_WORLD_CLUSTER_MODEL_NAME, light_header_buf, atlasLoadModelData, load_data, WL_FOR_TERRAIN);

			if (!atlas_data->client_data.draw_model_cluster)
				free(load_data);
			load_data = NULL;
		}
	}
}

Model * tempModelLoadFromHog_dbg(HogFile * hog_file, const char *header_name, const char *modelset_name, const char *model_name, 
	WLUsageFlags use_flags MEM_DBG_PARMS)
{
	Model * model_result = NULL;
	AtlasLoadData * load_data = NULL;
	SimpleBufHandle model_header_buf = NULL;

	model_header_buf = SimpleBufOpenRead(header_name, hog_file);
	if (model_header_buf)
	{
		load_data = scallocStruct(AtlasLoadData);
		load_data->hog_file = hog_file;
		strcpy(load_data->filename, modelset_name);

		model_result = tempModelLoad(model_name, model_header_buf, atlasLoadModelData, load_data, WL_FOR_WORLD);
		if (!model_result)
			free(load_data);
	}

	return model_result;
}

bool atlasLoadData(HeightMapAtlasData *data, const IVec2 local_pos, int lod, WorldRegion *region, HogFile *atlas_hogg_file, HogFile *model_hogg_file, HogFile *light_model_hogg_file, HogFile *coll_hogg_file)
{
	U32 atlas_version, color_size;
	SimpleBufHandle buf = NULL;
	SimpleBufHandle light_header_buf = NULL;

	setVec3(data->offset, local_pos[0] * GRID_BLOCK_SIZE, 0, local_pos[1] * GRID_BLOCK_SIZE);
	data->region = region;

	assert(lod <= ATLAS_MAX_LOD);

	if (atlas_hogg_file)
	{
		char relpath[MAX_PATH];
		sprintf(relpath, "x%d_z%d.atl", local_pos[0], local_pos[1]);
		buf = SimpleBufOpenRead(relpath, atlas_hogg_file);
	}

	if (!buf)
	{
		verbose_printf("Failed to open atlases for %s\n", region->name ? region->name : "Default");
		return false;
	}

	// Terrain vertex lighting information is stored in a separate
	// .hogg file. If it exists, look in there for light model headers.
	if(light_model_hogg_file) {
		char relpath[MAX_PATH];
		sprintf(relpath, "x%d_z%d_light.atl", local_pos[0], local_pos[1]);
		light_header_buf = SimpleBufOpenRead(relpath, light_model_hogg_file);
	}

	SimpleBufReadU32(&atlas_version, buf);

	if (atlas_version != TERRAIN_ATLAS_VERSION)
	{
		verbose_printf("Failed to open atlas for %s, wrong version number.\n", region->name ? region->name : "Default");
		SimpleBufClose(buf);
		return false;
	}

	if (lod > 0)
		coll_hogg_file = NULL;

	SimpleBufReadU32(&color_size, buf);

	if (color_size)
	{
		if (color_size != color_dxt_datasize)
		{
			verbose_printf("Failed to open atlas for %s, wrong color texture size.\n", region->name ? region->name : "Default");
			SimpleBufClose(buf);
			return false;
		}

		if (wlIsClient())
		{
			data->client_data.color_texture.has_alpha = false;
			data->client_data.color_texture.is_dxt = true;
			data->client_data.color_texture.width = ATLAS_COLOR_TEXTURE_SIZE;
			data->client_data.color_texture.data = memrefAlloc(color_size);
			SimpleBufRead(data->client_data.color_texture.data, color_size, buf);
		}
		else
		{
			// skip color data
			SimpleBufSeek(buf, color_size, SEEK_CUR);
		}
	}

	atlasLoadModels(data, local_pos, buf, light_header_buf, model_hogg_file, light_model_hogg_file, coll_hogg_file, lod == 0);

	SimpleBufClose(buf);
	
	// Close light model header, if it exists.
	if(light_header_buf)
		SimpleBufClose(light_header_buf);

	return true;
}

// Load atlas data from disk
static void __stdcall backgroundLoadAtlas(GeoRenderInfo *dummy_info, void *parent_unused, int param_load_vtx_light)
{
	HeightMapAtlas *atlas = (HeightMapAtlas *)dummy_info;
	HogFile *atlas_hogg_file, *model_hogg_file, *light_model_hogg_file = NULL, *coll_hogg_file = NULL;

	PERFINFO_AUTO_START_FUNC();
	atlas_hogg_file       = worldRegionGetTerrainHogFile(atlas->region, THOG_ATLAS, atlas->lod);
	model_hogg_file       = worldRegionGetTerrainHogFile(atlas->region, THOG_MODEL, atlas->lod);
	if (param_load_vtx_light)
		light_model_hogg_file = worldRegionGetTerrainHogFile(atlas->region, THOG_LIGHTMODEL, atlas->lod);
	if (atlas->lod == 0)
		coll_hogg_file = worldRegionGetTerrainHogFile(atlas->region, THOG_COLL, 0);

	atlasLoadData(atlas->data, atlas->local_pos, atlas->lod, atlas->region, atlas_hogg_file, model_hogg_file, light_model_hogg_file, coll_hogg_file);
	atlas->load_state = WCS_LOADING_FG;
	PERFINFO_AUTO_STOP();
}

void freeHeightMapAtlasRegionData(HeightMapAtlasRegionData *atlases)
{
	if (!atlases)
		return;

	terrainUnloadAtlases(atlases);

	stashTableDestroy(atlases->atlas_hash);

	eaDestroy(&atlases->root_atlases);
	eaDestroyEx(&atlases->layer_names, StructFreeString);
	eaDestroyEx(&atlases->all_atlases, atlasFree);
	StructDestroy(parse_HeightMapAtlasRegionData, atlases);
}

void terrainLoadRegionAtlases(WorldRegion *region, BinFileList *file_list)
{
	char base_dir[MAX_PATH], filename[MAX_PATH];
	ZoneMap *zmap = region->zmap_parent;
	int i, j;

	freeHeightMapAtlasRegionData(region->atlases);

	region->atlases = StructAlloc(parse_HeightMapAtlasRegionData);
	worldGetClientBaseDir(zmapGetFilename(zmap), SAFESTR(base_dir));
	sprintf(filename, "%s/terrain_%s_atlases.bin", base_dir, region->name ? region->name : "Default");
	if (!ParserOpenReadBinaryFile(NULL, filename, parse_HeightMapAtlasRegionData, region->atlases, NULL, NULL, NULL, NULL, 0, 0, 0) || 
		region->atlases->bin_version_number != TERRAIN_ATLAS_VERSION)
	{
		freeHeightMapAtlasRegionData(region->atlases);
		region->atlases = NULL;
		return;
	}

	region->atlases->layer_names = file_list->layer_names;
	file_list->layer_names = NULL;

	region->atlases->atlas_hash = stashTableCreateFixedSize(64, sizeof(IVec2) + sizeof(U32));

	for (i = 0; i < eaSize(&region->atlases->all_atlases); ++i)
	{
		HeightMapAtlas *atlas = region->atlases->all_atlases[i];

		if (atlas->lod == ATLAS_MAX_LOD)
			eaPush(&region->atlases->root_atlases, atlas);

		atlas->region = region;
		atlas->corner_lods[0] = atlas->last_corner_lods[0] =
			atlas->corner_lods[1] = atlas->last_corner_lods[1] =
			atlas->corner_lods[2] = atlas->last_corner_lods[2] =
			atlas->corner_lods[3] = atlas->last_corner_lods[3] = atlas->lod;

		stashAddPointer(region->atlases->atlas_hash, atlas, atlas, false);
	}

	for (i = 0; i < eaSize(&region->atlases->all_atlases); ++i)
	{
		HeightMapAtlas *atlas = region->atlases->all_atlases[i];
		IVec3 pos_and_lod;
		int atlas_size = heightmap_atlas_size[atlas->lod];

		// fill in neighbor and parent pointers
		if (atlas->lod < ATLAS_MAX_LOD)
		{
			int parent_atlas_size = heightmap_atlas_size[atlas->lod + 1];
			setVec3(pos_and_lod,
				parent_atlas_size * round(floorf(atlas->local_pos[0] / ((F32)parent_atlas_size))),
				parent_atlas_size * round(floorf(atlas->local_pos[1] / ((F32)parent_atlas_size))),
				atlas->lod + 1);
			stashFindPointer(region->atlases->atlas_hash, pos_and_lod, &atlas->parent);

			if (atlas->parent)
			{
				int px, py;
				px = (pos_and_lod[0] == atlas->local_pos[0]) ? 0 : 1;
				py = (pos_and_lod[1] == atlas->local_pos[1]) ? 0 : 1;
				atlas->parent->children[px+py*2] = atlas;
			}
		}

		STATIC_INFUNC_ASSERT(ARRAY_SIZE(atlas->neighbors) == ARRAY_SIZE(neighbor_offsets));

		for (j = 0; j < ARRAY_SIZE(atlas->neighbors); ++j)
		{
			setVec3(pos_and_lod, 
				atlas->local_pos[0] + neighbor_offsets[j][0] * atlas_size, 
				atlas->local_pos[1] + neighbor_offsets[j][1] * atlas_size,
				atlas->lod);
			stashFindPointer(region->atlases->atlas_hash, pos_and_lod, &atlas->neighbors[j]);
		}
	}

	region->terrain_bounds.needs_update = true;
}


/********************************
   Bin-time Atlasing Functions
 ********************************/

// During BIN saving, create temporary block-level atlas files
void layerSaveBlockAtlas(ZoneMapLayer *layer, int blocknum, HeightMapBinAtlas *atlas, HogFile *interm_file, SimpleBuffer ***output_array)
{
	SimpleBufHandle buf;
	char relpath[MAX_PATH];

	sprintf(relpath, "x%d_z%d_%d.part", atlas->local_pos[0], atlas->local_pos[1], atlas->lod);
	buf = SimpleBufOpenWrite(relpath, true, interm_file, true, false);

	if (atlas->lod == 0)
	{
		int dxt_size;
		void *dxt_data = SimpleBufReserve(color_dxt_datasize, buf);
		dxt_size = nvdxtCompress(atlas->color_array, dxt_data, HEIGHTMAP_ATLAS_COLOR_SIZE, HEIGHTMAP_ATLAS_COLOR_SIZE, RTEX_DXT1, gConf.iDXTQuality, ATLAS_COLOR_TEXTURE_SIZE);
		assert(dxt_size == color_dxt_datasize);
	}
	else
	{
		SimpleBufWrite(atlas->color_array, color_datasize, buf);
	}

	gmeshWriteBinData(atlas->occlusion_mesh, buf);

	eaPush(output_array, buf);

	if (atlas->children[0])
		layerSaveBlockAtlas(layer, blocknum, atlas->children[0], interm_file, output_array);
	if (atlas->children[1])
		layerSaveBlockAtlas(layer, blocknum, atlas->children[1], interm_file, output_array);
	if (atlas->children[2])
		layerSaveBlockAtlas(layer, blocknum, atlas->children[2], interm_file, output_array);
	if (atlas->children[3])
		layerSaveBlockAtlas(layer, blocknum, atlas->children[3], interm_file, output_array);
}

// During client BIN saving, composite temporary block-level atlas files
static bool layerLoadBlockAtlas(ZoneMapLayer *layer, int blocknum, HeightMapBinAtlas *atlas)
{
	char relpath[MAX_PATH];
	SimpleBufHandle buf;
	sprintf(relpath, "x%d_z%d_%d.part", atlas->local_pos[0], atlas->local_pos[1], atlas->lod);
	if (buf = readTerrainFileHogg(layer, blocknum, relpath))
	{
		assert(!atlas->color_array);
		if (atlas->lod == 0)
		{
			atlas->is_dxt_color = true;
			atlas->color_array = malloc(color_dxt_datasize);
			SimpleBufRead(atlas->color_array, color_dxt_datasize, buf);
		}
		else
		{
			atlas->is_dxt_color = false;
			atlas->color_array = malloc(color_datasize);
			SimpleBufRead(atlas->color_array, color_datasize, buf);
		}
		if (!atlas->occlusion_mesh)
			atlas->occlusion_mesh = gmeshFromBinData(buf);
		SimpleBufClose(buf);

#if DEBUG_COLOR_TEXTURES
		{
			char debug_filename[MAX_PATH];
			sprintf(debug_filename, "%s/srcimg/%p_%d_%d_%d.tga", fileTempDir(), layer, atlas->local_pos[0], atlas->local_pos[1], atlas->lod);
			tgaSave(debug_filename, atlas->color_array, HEIGHTMAP_ATLAS_COLOR_SIZE, HEIGHTMAP_ATLAS_COLOR_SIZE, 4);
		}
#endif
		return true;
	}
	return false;
}

__forceinline static void atlasBinFreeData(HeightMapBinAtlas *atlas)
{
	if (atlas->children[0])
	{
		atlasBinFree(atlas->children[0]);
		atlas->children[0] = NULL;
	}
	if (atlas->children[1])
	{
		atlasBinFree(atlas->children[1]);
		atlas->children[1] = NULL;
	}
	if (atlas->children[2])
	{
		atlasBinFree(atlas->children[2]);
		atlas->children[2] = NULL;
	}
	if (atlas->children[3])
	{
		atlasBinFree(atlas->children[3]);
		atlas->children[3] = NULL;
	}

	eaDestroy(&atlas->detail_material_names);
	gmeshFreeData(atlas->mesh);
	SAFE_FREE(atlas->mesh);
	gmeshFreeData(atlas->occlusion_mesh);
	SAFE_FREE(atlas->occlusion_mesh);
	SAFE_FREE(atlas->color_array);
	SAFE_FREE(atlas->corner_colors[0]);
	SAFE_FREE(atlas->corner_colors[1]);
	SAFE_FREE(atlas->corner_colors[2]);
	SAFE_FREE(atlas->corner_colors[3]);
	SAFE_FREE(atlas->corner_avg_colors[0]);
	SAFE_FREE(atlas->corner_avg_colors[1]);
	SAFE_FREE(atlas->corner_avg_colors[2]);
	SAFE_FREE(atlas->corner_avg_colors[3]);
	eaDestroy(&atlas->corner_detail_material_names[0]);
	eaDestroy(&atlas->corner_detail_material_names[1]);
	eaDestroy(&atlas->corner_detail_material_names[2]);
	eaDestroy(&atlas->corner_detail_material_names[3]);
}

// Delete atlas contents
void atlasBinFree(HeightMapBinAtlas *atlas)
{
	atlasBinFreeData(atlas);
	SAFE_FREE(atlas);
}

static bool loadIntermediateHeightMapMesh(ZoneMapLayer *layer, int blocknum, HeightMapBinAtlas *atlas, PerThreadAtlasParams *thread_params)
{
	TerrainBlockRange *range = layer->terrain.blocks[blocknum];
	char relpath[MAX_PATH];
	SimpleBufHandle buf;

	if (thread_params)
		timerStart(thread_params->timer_id);

	assert(!atlas->mesh);
	assert(!atlas->detail_material_names);

	sprintf(relpath, "x%d_z%d.msh_part", atlas->local_pos[0] - range->range.min_block[0], atlas->local_pos[1] - range->range.min_block[2]);
	if (buf = readTerrainFileHogg(layer, blocknum, relpath))
	{
		U8 detail_material_name_count;
		U32 needs_collision;
		F32 min_height, max_height;
		int i, px, py;

		atlas->mesh = gmeshFromBinData(buf);
		atlas->layer_filename = layer->filename;

		if (atlas->mesh->varcolors)
		{
			atlas->corner_colors[0] = calloc(atlas->mesh->varcolor_size, sizeof(U8));
			atlas->corner_colors[1] = calloc(atlas->mesh->varcolor_size, sizeof(U8));
			atlas->corner_colors[2] = calloc(atlas->mesh->varcolor_size, sizeof(U8));
			atlas->corner_colors[3] = calloc(atlas->mesh->varcolor_size, sizeof(U8));
		}

		for (i = 0; i < atlas->mesh->vert_count; ++i)
		{
			if (atlas->mesh->positions[i][0] == 0)
				px = 0;
			else if (atlas->mesh->positions[i][0] == GRID_BLOCK_SIZE)
				px = 1;
			else
				continue;
			if (atlas->mesh->positions[i][2] == 0)
				py = 0;
			else if (atlas->mesh->positions[i][2] == GRID_BLOCK_SIZE)
				py = 1;
			else
				continue;
			atlas->corner_heights[px+py*2] = atlas->corner_avg_heights[px+py*2] = atlas->mesh->positions[i][1];
			if (atlas->mesh->normals)
			{
				copyVec3(atlas->mesh->normals[i], atlas->corner_normals[px+py*2]);
				copyVec3(atlas->mesh->normals[i], atlas->corner_avg_normals[px+py*2]);
			}
			if (atlas->mesh->varcolors)
			{
				CopyStructs(atlas->corner_colors[px+py*2], &atlas->mesh->varcolors[i*atlas->mesh->varcolor_size], atlas->mesh->varcolor_size);
			}
		}

		atlas->tile_count = 1;
		atlas->occlusion_mesh = gmeshFromBinData(buf);

		SimpleBufReadF32(&min_height, buf);
		SimpleBufReadF32(&max_height, buf);

		SimpleBufReadU32(&needs_collision, buf);
		atlas->needs_collision = !!needs_collision;

		SimpleBufReadU8(&detail_material_name_count, buf);
		eaSetSize(&atlas->detail_material_names, detail_material_name_count);
		for (i = 0; i < (int)detail_material_name_count; ++i)
		{
			char *material_name = NULL;
			SimpleBufReadString(&material_name, buf);
			atlas->detail_material_names[i] = allocAddString(material_name);
		}

		SimpleBufClose(buf);
		if (thread_params)
			thread_params->timings[TAT_LOADING] += timerElapsed(thread_params->timer_id);
		return true;
	}

	if (thread_params)
		thread_params->timings[TAT_LOADING] += timerElapsed(thread_params->timer_id);
	return false;
}

// During BIN creation, create an atlas if there isn't one already for a heightmap at a given LOD
HeightMapBinAtlas* atlasFindByLocationEx(HeightMapBinAtlas ***atlas_list, IVec2 world_pos, U32 lod, IVec2 out_height_pos, IVec2 out_color_pos, ZoneMapLayer **layers, const IVec2 min_local_pos, const IVec2 max_local_pos, PerThreadAtlasParams *thread_params, HeightMapAtlasRegionData *atlases)
{
	int i;
	int size = heightmap_atlas_size[ATLAS_MAX_LOD];
	IVec2 local_pos;
	HeightMapBinAtlas *ret = NULL;

	if (!atlas_list)
		return NULL;

	local_pos[0] = floor(((float)world_pos[0]) / size) * size;
	local_pos[1] = floor(((float)world_pos[1]) / size) * size;
	for (i = 0; i < eaSize(atlas_list); i++)
	{
		if (sameVec2((*atlas_list)[i]->local_pos, local_pos))
		{
			ret = (*atlas_list)[i];
			break;
		}
	}

	if (!ret)
	{
		if (min_local_pos && max_local_pos &&
			   (local_pos[0] < min_local_pos[0] || local_pos[1] < min_local_pos[1] ||
				local_pos[0] >= max_local_pos[0] || local_pos[1] >= max_local_pos[1])
			)
		{
			return NULL;
		}

		ret = calloc(1, sizeof(HeightMapBinAtlas));
		ret->lod = ATLAS_MAX_LOD;
		copyVec2(local_pos, ret->local_pos);
		eaPush(atlas_list, ret);
	}

	while (lod < ret->lod)
	{
		int px, py;
		px = (world_pos[0] - ret->local_pos[0]) >= (size/2) ? 1 : 0;
		py = (world_pos[1] - ret->local_pos[1]) >= (size/2) ? 1 : 0;

		if (!ret->children[px+py*2])
		{
			ret->children[px+py*2] = calloc(1, sizeof(HeightMapBinAtlas));
			ret->children[px+py*2]->lod = ret->lod-1;
			setVec2(ret->children[px+py*2]->local_pos,
				ret->local_pos[0] + px*(size/2),
				ret->local_pos[1] + py*(size/2));
			ret->children[px+py*2]->parent = ret;
		}
		ret = ret->children[px+py*2];
		size /= 2;
	}

	assert(world_pos[0] >= ret->local_pos[0] &&
		world_pos[0] < ret->local_pos[0]+heightmap_atlas_size[ret->lod] &&
		world_pos[1] >= ret->local_pos[1] &&
		world_pos[1] < ret->local_pos[1]+heightmap_atlas_size[ret->lod]);

	if (out_height_pos)
	{
		out_height_pos[0] = (world_pos[0] - ret->local_pos[0]) * (HEIGHTMAP_SIZE(ret->lod)-1);
		out_height_pos[1] = (world_pos[1] - ret->local_pos[1]) * (HEIGHTMAP_SIZE(ret->lod)-1);
	}

	if (out_color_pos)
	{
		out_color_pos[0] = (world_pos[0] - ret->local_pos[0]) * (COLORMAP_SIZE(ret->lod)-1);
		out_color_pos[1] = (world_pos[1] - ret->local_pos[1]) * (COLORMAP_SIZE(ret->lod)-1);
	}

	if (layers && ret->lod == 0)
	{
		if (!ret->merged && !ret->merging)
		{
			int block_increment = heightmapAtlasGetLODSize(ret->lod);
			bool found = false;
			int b, l;
			HeightMapAtlas *atlas_bin_data = NULL;

			ret->merging = true;
			if (terrain_atlas_log)
				fprintf(terrain_atlas_log, "%d_%d_%d : Started loading height map mesh.\n", ret->lod, ret->local_pos[0], ret->local_pos[1]);
			LeaveCriticalSection(&terrain_atlas_bins_cs);

			for (l = 0; !found && l < eaSize(&layers); l++)
			{
				ZoneMapLayer *layer = layers[l];
				for (b = 0; !found && b < eaSize(&layer->terrain.blocks); b++)
				{
					TerrainBlockRange *block = layer->terrain.blocks[b];
					if (block && 
						!(	block->range.max_block[0] < ret->local_pos[0] ||
							block->range.max_block[2] < ret->local_pos[1] ||
							ret->local_pos[0] + block_increment < block->range.min_block[0] ||
							ret->local_pos[1] + block_increment < block->range.min_block[2]
						 ) &&
							loadIntermediateHeightMapMesh(layer, b, ret, thread_params))
					{
						if (atlases)
						{
							atlas_bin_data = StructAlloc(parse_HeightMapAtlas);
							atlas_bin_data->lod = ret->lod;
							copyVec2(ret->local_pos, atlas_bin_data->local_pos);
							atlas_bin_data->layer_bitfield |= 1 << l;
						}

						found = true;
						break;
					}
				}
			}

			EnterCriticalSection(&terrain_atlas_bins_cs);
			if (atlas_bin_data)
			{
				eaPush(&atlases->all_atlases, atlas_bin_data);
				stashAddPointer(atlases->atlas_hash, atlas_bin_data, atlas_bin_data, false);
			}
			ret->merged = true;
			ret->merging = false;
			if (terrain_atlas_log)
				fprintf(terrain_atlas_log, "%d_%d_%d : Finished loading height map mesh.\n", ret->lod, ret->local_pos[0], ret->local_pos[1]);
		}

		while (ret->merging)
		{
			LeaveCriticalSection(&terrain_atlas_bins_cs);
			assert(g_AtlasThreadProcInfo.thread_count > 1);
			Sleep(10);
			EnterCriticalSection(&terrain_atlas_bins_cs);
		}
	}

	return ret;
}

#define TERRAIN_OCCLUSION_AREA_COEFF 0.015f  // Height differential to subdivide occlusion quadtree at (in ft. per sq.ft)
#define TERRAIN_OCCLUSION_DEPTH 2 // Number of recursion levels in quadtree
#define TERRAIN_OCCLUSION_LOD 1 // Highest atlas LOD to compute occlusion for

typedef struct OcclusionQuad
{
    F32 min_height;
    IVec2 min_pos;
    IVec2 max_pos;
    int subdiv;
	bool invisible;
    struct OcclusionQuad *ch[2][2];
} OcclusionQuad;

void occlusion_quad_free(OcclusionQuad *quad)
{
    if (quad->ch[0][0])
        occlusion_quad_free(quad->ch[0][0]);
    if (quad->ch[1][0])
        occlusion_quad_free(quad->ch[1][0]);
    if (quad->ch[0][1])
        occlusion_quad_free(quad->ch[0][1]);
    if (quad->ch[1][1])
        occlusion_quad_free(quad->ch[1][1]);
    SAFE_FREE(quad);
}

static bool occlusion_quad_helper1(OcclusionQuad *quad, F32 *height_buf, S32 buf_size, F32 cutoff, F32 scale, int depth, IVec2 subdiv_pos, U8 *subdiv_array)
{
    int x, z, cx, cz;
    F32 min_heights[] = { 10000.f, 10000.f, 10000.f, 10000.f };
 	F32 max_dist;
    bool child_recurse;
    IVec2 child_subdiv = { subdiv_pos[0], subdiv_pos[1] };

    if (subdiv_array)
    {
        if (depth < 2)
        {
            quad->subdiv = 1;
			quad->invisible = true;
	        for (x = 0; x < (4>>depth); x++)
    	        for (z = 0; z < (4>>depth); z++)
				{
					U8 val = subdiv_array[(subdiv_pos[0]<<(2-depth))+x+((subdiv_pos[1]<<(2-depth))+z)*4];
        	        if (val != 0)
                        quad->subdiv = 2;
					if (val != 2)
						quad->invisible = false;
				}
        }
        else
		{
            quad->subdiv = subdiv_array[subdiv_pos[0]+subdiv_pos[1]*4] + 1;
			if (quad->subdiv == 3)
				quad->invisible = true;
		}
    }
    
    if (depth > TERRAIN_OCCLUSION_DEPTH)
    {
        return false;
    }

    cx = (quad->min_pos[0]+quad->max_pos[0])/2;
    cz = (quad->min_pos[1]+quad->max_pos[1])/2;
    for (z = quad->min_pos[1]; z <= quad->max_pos[1]; z++)
	{
        for (x = quad->min_pos[0]; x <= quad->max_pos[0]; x++)
        {
            if (height_buf[x+z*buf_size] > cutoff)
            {
                if (x <= cx)
                {
                    if (z <= cz && height_buf[x+z*buf_size] < min_heights[0])
                        min_heights[0] = height_buf[x+z*buf_size];
                    if (z >= cz && height_buf[x+z*buf_size] < min_heights[2])
                        min_heights[2] = height_buf[x+z*buf_size];
                }
                if (x >= cx)
                {
                    if (z <= cz && height_buf[x+z*buf_size] < min_heights[1])
                        min_heights[1] = height_buf[x+z*buf_size];
                    if (z >= cz && height_buf[x+z*buf_size] < min_heights[3])
                        min_heights[3] = height_buf[x+z*buf_size];
                }
            }
        }
	}
    max_dist = MAX(MAX(MAX(min_heights[0], min_heights[0]), min_heights[2]), min_heights[3]) - quad->min_height;

    quad->ch[0][0] = calloc(1, sizeof(OcclusionQuad));
    quad->ch[0][0]->min_height = min_heights[0];
    setVec2(quad->ch[0][0]->min_pos, quad->min_pos[0], quad->min_pos[1]);
    setVec2(quad->ch[0][0]->max_pos, cx, cz);
    if (depth < 2)
    {
        child_subdiv[0] = subdiv_pos[0]*2;
        child_subdiv[1] = subdiv_pos[1]*2;
    }
    child_recurse = occlusion_quad_helper1(quad->ch[0][0], height_buf, buf_size, cutoff, scale,
                                           depth+1, child_subdiv, subdiv_array);

    quad->ch[1][0] = calloc(1, sizeof(OcclusionQuad));
    quad->ch[1][0]->min_height = min_heights[1];
    setVec2(quad->ch[1][0]->min_pos, cx, quad->min_pos[1]);
    setVec2(quad->ch[1][0]->max_pos, quad->max_pos[0], cz);
    if (depth < 2)
    {
        child_subdiv[0] = subdiv_pos[0]*2+1;
        child_subdiv[1] = subdiv_pos[1]*2;
    }
    child_recurse = occlusion_quad_helper1(quad->ch[1][0], height_buf, buf_size, cutoff, scale,
                                           depth+1, child_subdiv, subdiv_array) || child_recurse;

    quad->ch[0][1] = calloc(1, sizeof(OcclusionQuad));
    quad->ch[0][1]->min_height = min_heights[2];
    setVec2(quad->ch[0][1]->min_pos, quad->min_pos[0], cz);
    setVec2(quad->ch[0][1]->max_pos, cx, quad->max_pos[1]);
    if (depth < 2)
    {
        child_subdiv[0] = subdiv_pos[0]*2;
        child_subdiv[1] = subdiv_pos[1]*2+1;
    }
    child_recurse = occlusion_quad_helper1(quad->ch[0][1], height_buf, buf_size, cutoff, scale,
                                           depth+1, child_subdiv, subdiv_array) || child_recurse;

    quad->ch[1][1] = calloc(1, sizeof(OcclusionQuad));
    quad->ch[1][1]->min_height = min_heights[3];
    setVec2(quad->ch[1][1]->min_pos, cx, cz);
    setVec2(quad->ch[1][1]->max_pos, quad->max_pos[0], quad->max_pos[1]);
    if (depth < 2)
    {
        child_subdiv[0] = subdiv_pos[0]*2+1;
        child_subdiv[1] = subdiv_pos[1]*2+1;
    }
    child_recurse = occlusion_quad_helper1(quad->ch[1][1], height_buf, buf_size, cutoff, scale,
                                           depth+1, child_subdiv, subdiv_array) || child_recurse;

    if (child_recurse ||
        quad->subdiv == 2 ||
        max_dist > (quad->max_pos[0]-quad->min_pos[0])*(quad->max_pos[1]-quad->min_pos[1])*scale*scale*TERRAIN_OCCLUSION_AREA_COEFF)
    {
        return true;
    }

    occlusion_quad_free(quad->ch[0][0]);
    occlusion_quad_free(quad->ch[1][0]);
    occlusion_quad_free(quad->ch[0][1]);
    occlusion_quad_free(quad->ch[1][1]);
    quad->ch[0][0] = NULL;
    quad->ch[1][0] = NULL;
    quad->ch[0][1] = NULL;
    quad->ch[1][1] = NULL;

    return false;
}

static int occlusion_quad_helper2(OcclusionQuad *quad, F32 *height_buf, S32 buf_size, F32 scale, GMesh *mesh, int depth)
{
    int start = mesh->vert_count;
    int cx, cz;
    Color color;
	Vec3 position;

	switch (quad->subdiv)
	{
		xcase 0:
			color = ColorWhite;
		xcase 1:
			color = ColorGreen;
		xcase 2:
			color = ColorRed;
		xcase 3:
			return -1;
	}

	if (depth > 0 && quad->invisible)
		return -1;

	setVec3(position, quad->min_pos[0]*scale, quad->min_height-1, quad->min_pos[1]*scale);
	gmeshAddVertSimple(mesh, position, NULL, NULL, &color, NULL, NULL, false, false, false);

	setVec3(position, quad->max_pos[0]*scale, quad->min_height-1, quad->min_pos[1]*scale);
	gmeshAddVertSimple(mesh, position, NULL, NULL, &color, NULL, NULL, false, false, false);

	setVec3(position, quad->max_pos[0]*scale, quad->min_height-1, quad->max_pos[1]*scale);
	gmeshAddVertSimple(mesh, position, NULL, NULL, &color, NULL, NULL, false, false, false);

	setVec3(position, quad->min_pos[0]*scale, quad->min_height-1, quad->max_pos[1]*scale);
	gmeshAddVertSimple(mesh, position, NULL, NULL, &color, NULL, NULL, false, false, false);

  
	if (quad->ch[0][0] || quad->ch[1][0] || quad->ch[0][1] || quad->ch[1][1])
	{
		int child_starts[4];
		cx = (quad->min_pos[0]+quad->max_pos[0])/2;
		cz = (quad->min_pos[1]+quad->max_pos[1])/2;

		setVec3(position, cx*scale, quad->min_height-1, quad->min_pos[1]*scale);
		gmeshAddVertSimple(mesh, position, NULL, NULL, &color, NULL, NULL, false, false, false);
   
		setVec3(position, quad->max_pos[0]*scale, quad->min_height-1, cz*scale);
		gmeshAddVertSimple(mesh, position, NULL, NULL, &color, NULL, NULL, false, false, false);
   
		setVec3(position, cx*scale, quad->min_height-1, quad->max_pos[1]*scale);
		gmeshAddVertSimple(mesh, position, NULL, NULL, &color, NULL, NULL, false, false, false);
   
		setVec3(position, quad->min_pos[0]*scale, quad->min_height-1, cz*scale);
		gmeshAddVertSimple(mesh, position, NULL, NULL, &color, NULL, NULL, false, false, false);

       
		assert(quad->ch[0][0] && quad->ch[0][1] && quad->ch[1][0] && quad->ch[1][1]);
		child_starts[0] = occlusion_quad_helper2(quad->ch[0][0], height_buf, buf_size, scale, mesh, depth+1);
		child_starts[1] = occlusion_quad_helper2(quad->ch[1][0], height_buf, buf_size, scale, mesh, depth+1);
		child_starts[2] = occlusion_quad_helper2(quad->ch[0][1], height_buf, buf_size, scale, mesh, depth+1);
		child_starts[3] = occlusion_quad_helper2(quad->ch[1][1], height_buf, buf_size, scale, mesh, depth+1);

		if (child_starts[0] >= 0)
		{
			gmeshAddQuad(mesh, start+0, child_starts[0]+0, child_starts[0]+1, start+4, 0, false);
			gmeshAddQuad(mesh, start+7, child_starts[0]+3, child_starts[0]+0, start+0, 0, false);
		}
		if (child_starts[1] >= 0)
		{
			gmeshAddQuad(mesh, start+4, child_starts[1]+0, child_starts[1]+1, start+1, 0, false);
			gmeshAddQuad(mesh, start+1, child_starts[1]+1, child_starts[1]+2, start+5, 0, false);
		}

		if (child_starts[3] >= 0)
		{
			gmeshAddQuad(mesh, start+5, child_starts[3]+1, child_starts[3]+2, start+2, 0, false);
			gmeshAddQuad(mesh, start+2, child_starts[3]+2, child_starts[3]+3, start+6, 0, false);
		}
		if (child_starts[2] >= 0)
		{
			gmeshAddQuad(mesh, start+6, child_starts[2]+2, child_starts[2]+3, start+3, 0, false);
			gmeshAddQuad(mesh, start+3, child_starts[2]+3, child_starts[2]+0, start+7, 0, false);
		}

		if (child_starts[0] >= 0 && child_starts[1] >= 0)
		{
			gmeshAddQuad(mesh, child_starts[1]+0, child_starts[0]+1, child_starts[0]+2, child_starts[1]+3, 0, false);
		}
		if (child_starts[1] >= 0 && child_starts[3] >= 0)
		{
			gmeshAddQuad(mesh, child_starts[1]+3, child_starts[3]+0, child_starts[3]+1, child_starts[1]+2, 0, false);
		}
		if (child_starts[3] >= 0 && child_starts[2] >= 0)
		{
			gmeshAddQuad(mesh, child_starts[3]+0, child_starts[2]+1, child_starts[2]+2, child_starts[3]+3, 0, false);
		}
		if (child_starts[2] >= 0 && child_starts[0] >= 0)
		{
			gmeshAddQuad(mesh, child_starts[0]+3, child_starts[2]+0, child_starts[2]+1, child_starts[0]+2, 0, false);
		}
    }
    else
    {
        gmeshAddTri(mesh, start+0, start+2, start+1, 0, false);
		gmeshAddTri(mesh, start+0, start+3, start+2, 0, false);
    }

    if (depth == 0)
    {
		int x, z;
		int start2 = mesh->vert_count;
		bool corner_vis[4];
		F32 min_edges[] = { 10000.f, 10000.f, 10000.f, 10000.f };

		z = quad->min_pos[1];
		for (x = quad->min_pos[0]; x <= quad->max_pos[0]; x++)
		{
			if (height_buf[x+z*buf_size] < min_edges[0])
				min_edges[0] = height_buf[x+z*buf_size];
		}

		z = quad->max_pos[1];
		for (x = quad->min_pos[0]; x <= quad->max_pos[0]; x++)
		{
			if (height_buf[x+z*buf_size] < min_edges[2])
				min_edges[2] = height_buf[x+z*buf_size];
		}

		x = quad->min_pos[0];
		for (z = quad->min_pos[1]; z <= quad->max_pos[1]; z++)
		{
			if (height_buf[x+z*buf_size] < min_edges[3])
				min_edges[3] = height_buf[x+z*buf_size];
		}

		x = quad->max_pos[0];
		for (z = quad->min_pos[1]; z <= quad->max_pos[1]; z++)
		{
			if (height_buf[x+z*buf_size] < min_edges[1])
				min_edges[1] = height_buf[x+z*buf_size];
		}

		setVec3(position, quad->min_pos[0]*scale, min_edges[0]-1, quad->min_pos[1]*scale);
		gmeshAddVertSimple(mesh, position, NULL, NULL, &color, NULL, NULL, false, false, false);

		setVec3(position, quad->max_pos[0]*scale, min_edges[0]-1, quad->min_pos[1]*scale);
		gmeshAddVertSimple(mesh, position, NULL, NULL, &color, NULL, NULL, false, false, false);

		setVec3(position, quad->max_pos[0]*scale, min_edges[1]-1, quad->min_pos[1]*scale);
		gmeshAddVertSimple(mesh, position, NULL, NULL, &color, NULL, NULL, false, false, false);

		setVec3(position, quad->max_pos[0]*scale, min_edges[1]-1, quad->max_pos[1]*scale);
		gmeshAddVertSimple(mesh, position, NULL, NULL, &color, NULL, NULL, false, false, false);

		setVec3(position, quad->max_pos[0]*scale, min_edges[2]-1, quad->max_pos[1]*scale);
		gmeshAddVertSimple(mesh, position, NULL, NULL, &color, NULL, NULL, false, false, false);

		setVec3(position, quad->min_pos[0]*scale, min_edges[2]-1, quad->max_pos[1]*scale);
		gmeshAddVertSimple(mesh, position, NULL, NULL, &color, NULL, NULL, false, false, false);

		setVec3(position, quad->min_pos[0]*scale, min_edges[3]-1, quad->max_pos[1]*scale);
		gmeshAddVertSimple(mesh, position, NULL, NULL, &color, NULL, NULL, false, false, false);

		setVec3(position, quad->min_pos[0]*scale, min_edges[3]-1, quad->min_pos[1]*scale);
		gmeshAddVertSimple(mesh, position, NULL, NULL, &color, NULL, NULL, false, false, false);

		corner_vis[0] = (!quad->ch[0][0] || !quad->ch[0][0]->invisible);
		corner_vis[1] = (!quad->ch[1][0] || !quad->ch[1][0]->invisible);
		corner_vis[2] = (!quad->ch[1][1] || !quad->ch[1][1]->invisible);
		corner_vis[3] = (!quad->ch[0][1] || !quad->ch[0][1]->invisible);
		if (corner_vis[0] && corner_vis[1])
			gmeshAddQuad(mesh, start2+0, start+0, start+1, start2+1, 0, false);
		if (corner_vis[1] && corner_vis[2])
			gmeshAddQuad(mesh, start2+2, start+1, start+2, start2+3, 0, false);
		if (corner_vis[2] && corner_vis[3])
			gmeshAddQuad(mesh, start2+4, start+2, start+3, start2+5, 0, false);
		if (corner_vis[3] && corner_vis[0])
			gmeshAddQuad(mesh, start2+6, start+3, start+0, start2+7, 0, false);
    }

    return start;
}

void occlusion_quad_recurse(F32 *height_buf, S32 buf_size, F32 scale, F32 cutoff, F32 min_height, IVec2 min_pos, IVec2 max_pos, GMesh *mesh, U8 *subdiv_array)
{
	OcclusionQuad root = { 0 };
	IVec2 subdiv_pos = { 0, 0 };
	copyVec2(min_pos, root.min_pos);
	copyVec2(max_pos, root.max_pos);
	root.min_height = min_height;
	occlusion_quad_helper1(&root, height_buf, buf_size, cutoff, scale, 0, subdiv_pos, subdiv_array);
	occlusion_quad_helper2(&root, height_buf, buf_size, scale, mesh, 0);
	if (root.ch[0][0])
		occlusion_quad_free(root.ch[0][0]);
	if (root.ch[1][0])
		occlusion_quad_free(root.ch[1][0]);
	if (root.ch[0][1])
		occlusion_quad_free(root.ch[0][1]);
	if (root.ch[1][1])
		occlusion_quad_free(root.ch[1][1]);
}

void layerAtlasCalcOcclusion(HeightMapBinAtlas *atlas)
{
	// TODO(CD)
	/*
    if (atlas->lod <= TERRAIN_OCCLUSION_LOD)
    {
        int x, z;
        F32 scale = (F32)(4<<atlas->lod);
		F32 **verts = &atlas->occlusion_verts;
        U32 **inds = &atlas->occlusion_inds;
        F32 *height_buf = (F32*)atlas->height_array;
        F32 min_height = height_buf[0];
        IVec2 min_pos = { HEIGHTMAP_ATLAS_HEIGHT_SIZE, HEIGHTMAP_ATLAS_HEIGHT_SIZE };
        IVec2 max_pos = { 0, 0 };
        for (z = 0; z < HEIGHTMAP_ATLAS_HEIGHT_SIZE; z++)
		{
            for (x = 0; x < HEIGHTMAP_ATLAS_HEIGHT_SIZE; x++)
			{
                if (height_buf[x+z*HEIGHTMAP_ATLAS_HEIGHT_SIZE] > HEIGHTMAP_MIN_HEIGHT)
                {
                    if (x < min_pos[0])
                        min_pos[0] = x;
                    if (z < min_pos[1])
                        min_pos[1] = z;
                    if (x > max_pos[0])
                        max_pos[0] = x;
                    if (z > max_pos[1])
                        max_pos[1] = z;
                    if (height_buf[x+z*HEIGHTMAP_ATLAS_HEIGHT_SIZE] < min_height)
                        min_height = height_buf[x+z*HEIGHTMAP_ATLAS_HEIGHT_SIZE];
                }
			}
		}
        occlusion_quad_recurse(height_buf, HEIGHTMAP_ATLAS_HEIGHT_SIZE, scale, HEIGHTMAP_MIN_HEIGHT, min_height, min_pos, max_pos, verts, NULL, inds, NULL);
    }
	*/
}

static void fixupMaterialColorIdxs(TerrainMeshRenderMaterial *detail_material, int *preferred_color_idxs)
{
	int j, k;

	for (j = 0; j < 3; ++j)
	{
		detail_material->color_idxs[j] = 255;

		if (detail_material->detail_material_ids[j] < 255)
		{
			int color_counter = 0;
			bool taken;
			detail_material->color_idxs[j] = eaiGet(&preferred_color_idxs, detail_material->detail_material_ids[j]);
			detail_material->color_idxs[j] = CLAMP(detail_material->color_idxs[j], 0, 3);
			do
			{
				taken = false;
				for (k = 0; k < j; ++k)
				{
					if (detail_material->color_idxs[j] == detail_material->color_idxs[k])
						taken = true;
				}

				if (taken)
					detail_material->color_idxs[j] = color_counter++;

			} while (taken);

			assert(color_counter <= 4);
			assert(detail_material->color_idxs[j] < 4);
		}
	}
}

static int findHighestMaterialId(const U8 *varcolor0, const U8 *varcolor1, const U8 *varcolor2, int varcolor_size)
{
	int highest_idx = 0;
	int highest_weight = varcolor0[0] + varcolor1[0] + varcolor2[0];
	int i;

	for (i = 1; i < varcolor_size; ++i)
	{
		int this_weight = varcolor0[i] + varcolor1[i] + varcolor2[i];

		if (this_weight > highest_weight)
		{
			highest_weight = this_weight;
			highest_idx = i;
		}
	}

	return highest_idx;
}

static int getMeshMaterial(const U8 *varcolors[3], int varcolor_size, TerrainMeshRenderMaterial ***render_materials, const int *material_merges, bool use_material_fade)
{
	int highest_weights[3] = {0, 0, 0};
	TerrainMeshRenderMaterial sorted_material;
	TerrainMeshRenderMaterial *new_material;
	int material_count = eaSize(render_materials);
	int *preferred_color_idxs = NULL;
	int i, j, k;

	for (i = 0; i < 3; ++i)
	{
		sorted_material.detail_material_ids[i] = 255;
		sorted_material.color_idxs[i] = 255;
	}

	if (use_material_fade)
	{
		// find most significant weighted detail material idxs
		for (i = 0; i < varcolor_size; ++i)
		{
			int weight = varcolors[0][i] + varcolors[1][i] + varcolors[2][i];

			for (j = 0; j < 3; ++j)
			{
				if (weight > highest_weights[j])
				{
					for (k = 2; k > j; --k)
					{
						highest_weights[k] = highest_weights[k-1];
						sorted_material.detail_material_ids[k] = sorted_material.detail_material_ids[k-1];
						sorted_material.color_idxs[k] = sorted_material.color_idxs[k-1];
					}
					highest_weights[j] = weight;
					sorted_material.detail_material_ids[j] = i;
					sorted_material.color_idxs[j] = i % 4;
					break;
				}
			}
		}

		// sort idxs ascending for pooled materials
		for (i = 0; i < 3; ++i)
		{
			for (j = i+1; j < 3; ++j)
			{
				if (sorted_material.detail_material_ids[j] < sorted_material.detail_material_ids[i])
				{
					U8 temp = sorted_material.detail_material_ids[i];
					sorted_material.detail_material_ids[i] = sorted_material.detail_material_ids[j];
					sorted_material.detail_material_ids[j] = temp;

					temp = sorted_material.color_idxs[i];
					sorted_material.color_idxs[i] = sorted_material.color_idxs[j];
					sorted_material.color_idxs[j] = temp;
				}
			}
		}
	}
	else if (varcolors[0] && varcolors[1] && varcolors[2])
	{
		sorted_material.detail_material_ids[0] = findHighestMaterialId(varcolors[0], varcolors[1], varcolors[2], varcolor_size);
		sorted_material.color_idxs[0] = sorted_material.detail_material_ids[0] % 4;
	}

	// try to find existing render material
	for (i = 0; i < material_count; ++i)
	{
		TerrainMeshRenderMaterial *material = (*render_materials)[i];
		if (memcmp(material->detail_material_ids, sorted_material.detail_material_ids, 3 * sizeof(U8))==0)
		{
			while (material_merges && material_merges[i] != i)
				i = material_merges[i];
			return i;
		}
	}

	for (j = 2; j >= 0; --j)
	{
		if (sorted_material.detail_material_ids[j] < 255)
			eaiSet(&preferred_color_idxs, sorted_material.detail_material_ids[j], sorted_material.color_idxs[j]);
	}

	for (i = eaSize(render_materials) - 1; i >= 0; --i)
	{
		TerrainMeshRenderMaterial *material = (*render_materials)[i];
		for (j = 2; j >= 0; --j)
		{
			if (material->detail_material_ids[j] < 255)
				eaiSet(&preferred_color_idxs, material->detail_material_ids[j], material->color_idxs[j]);
		}
	}

	assert(!material_merges);

	new_material = malloc(sizeof(TerrainMeshRenderMaterial));
	memcpy(new_material, &sorted_material, sizeof(sorted_material));
	fixupMaterialColorIdxs(new_material, preferred_color_idxs);
	eaiDestroy(&preferred_color_idxs);

	return eaPush(render_materials, new_material);
}

static bool materialIsSubset(const TerrainMeshRenderMaterial *render_material, const TerrainMeshRenderMaterial *render_material_parent)
{
	int i, j;

	for (i = 0; i < 3 && render_material->detail_material_ids[i] < 255; ++i)
	{
		bool found = false;
		
		for (j = 0; j < 3 && render_material_parent->detail_material_ids[j] < 255; ++j)
		{
			if (render_material->detail_material_ids[i] == render_material_parent->detail_material_ids[j])
			{
				found = true;
				break;
			}
		}

		if (!found)
			return false;
	}

	return true;
}

#define SQUIDGE 0.1f

void subdivCreate(SubdivArrays *subdiv, GMesh *mesh)
{
	F32 cell_mul;
	int i, j, k;

	subdivClear(subdiv);

	setVec3same(subdiv->bounds_min, 10e10);
	setVec3same(subdiv->bounds_max,-10e10);
	for (i = 0; i < mesh->vert_count; ++i)
		vec3RunningMinMax(mesh->positions[i], subdiv->bounds_min, subdiv->bounds_max);

	cell_mul = (MAX(subdiv->bounds_max[0], subdiv->bounds_max[2]) - MIN(subdiv->bounds_min[0], subdiv->bounds_min[2])) / SUBDIV_SIZE;
	subdiv->cell_div = 1.f / cell_mul;
	for (k = 0; k < mesh->tri_count; ++k)
	{
		GTriIdx *tri = &mesh->tris[k];
		int x, z, min_x, min_z, max_x, max_z;
		F32 *v0 = mesh->positions[tri->idx[0]];
		F32 *v1 = mesh->positions[tri->idx[1]];
		F32 *v2 = mesh->positions[tri->idx[2]];

		min_x = floorf((v0[0] - subdiv->bounds_min[0] - SQUIDGE) * subdiv->cell_div);
		min_z = floorf((v0[2] - subdiv->bounds_min[2] - SQUIDGE) * subdiv->cell_div);
		max_x = floorf((v0[0] - subdiv->bounds_min[0] + SQUIDGE) * subdiv->cell_div);
		max_z = floorf((v0[2] - subdiv->bounds_min[2] + SQUIDGE) * subdiv->cell_div);

		x = floorf((v1[0] - subdiv->bounds_min[0] - SQUIDGE) * subdiv->cell_div);
		MIN1(min_x, x);
		z = floorf((v1[2] - subdiv->bounds_min[2] - SQUIDGE) * subdiv->cell_div);
		MIN1(min_z, z);

		x = floorf((v1[0] - subdiv->bounds_min[0] + SQUIDGE) * subdiv->cell_div);
		MAX1(max_x, x);
		z = floorf((v1[2] - subdiv->bounds_min[2] + SQUIDGE) * subdiv->cell_div);
		MAX1(max_z, z);

		x = floorf((v2[0] - subdiv->bounds_min[0] - SQUIDGE) * subdiv->cell_div);
		MIN1(min_x, x);
		z = floorf((v2[2] - subdiv->bounds_min[2] - SQUIDGE) * subdiv->cell_div);
		MIN1(min_z, z);

		x = floorf((v2[0] - subdiv->bounds_min[0] + SQUIDGE) * subdiv->cell_div);
		MAX1(max_x, x);
		z = floorf((v2[2] - subdiv->bounds_min[2] + SQUIDGE) * subdiv->cell_div);
		MAX1(max_z, z);

		min_x = CLAMP(min_x, 0, SUBDIV_SIZE-1);
		min_z = CLAMP(min_z, 0, SUBDIV_SIZE-1);
		max_x = CLAMP(max_x, 0, SUBDIV_SIZE-1);
		max_z = CLAMP(max_z, 0, SUBDIV_SIZE-1);

		for (j = min_z; j <= max_z; ++j)
		{
			for (i = min_x; i <= max_x; ++i)
			{
				if (min_x == max_x || min_z == max_z)
				{
					// shortcut
					eaiPush(&subdiv->tri_arrays[j][i], k);
				}
				else
				{
					// CD: this doesn't seem to be working correctly
// 					Vec3 cell_min, cell_size;
// 					setVec3(cell_min, subdiv->bounds_min[0] + cell_mul * i - SQUIDGE*0.5f, subdiv->bounds_min[1] - SQUIDGE*0.5f, subdiv->bounds_min[2] + cell_mul * j - SQUIDGE*0.5f);
// 					setVec3(cell_size, cell_mul + SQUIDGE, subdiv->bounds_max[1] - subdiv->bounds_min[1] + SQUIDGE, cell_mul + SQUIDGE);
// 					if (triCube(cell_min, cell_size, v0, v1, v2))
						eaiPush(&subdiv->tri_arrays[j][i], k);
				}
			}
		}
	}
}

int *subdivGetBucket(SubdivArrays *subdiv, const Vec3 pos)
{
	int x, z;
	x = floorf((pos[0] - subdiv->bounds_min[0]) * subdiv->cell_div);
	z = floorf((pos[2] - subdiv->bounds_min[2]) * subdiv->cell_div);
	x = CLAMP(x, 0, SUBDIV_SIZE-1);
	z = CLAMP(z, 0, SUBDIV_SIZE-1);
	return subdiv->tri_arrays[z][x];
}

void subdivClear(SubdivArrays *subdiv)
{
	int i, j;

	for (j = 0; j < SUBDIV_SIZE; ++j)
	{
		for (i = 0; i < SUBDIV_SIZE; ++i)
		{
			eaiDestroy(&subdiv->tri_arrays[j][i]);
		}
	}

	ZeroStructForce(subdiv);
}

typedef struct RVertCache
{
	int idx;
	Color c;
} RVertCache;

#define MAX_VERT_CACHE 20

TerrainMeshRenderMaterial **terrainConvertHeightMapMeshToRenderable(GMesh *source_mesh, const IVec2 local_pos, GMesh *parent_mesh, 
																	const IVec2 parent_local_pos, GMesh *output_mesh, 
																	int atlas_size, F32 *min_height, F32 *max_height, 
																	bool use_material_fade, bool disable_tangent_space, PerThreadAtlasParams *thread_params)
{
	F32 position_multiplier = 1.f / (atlas_size * GRID_BLOCK_SIZE);
	bool need_tangent_space = (source_mesh->usagebits & (USE_BINORMALS | USE_TANGENTS)) != (USE_BINORMALS | USE_TANGENTS);
	bool has_texcoords = !!(source_mesh->usagebits & USE_TEX1S);
	bool parent_has_texcoords = parent_mesh && (parent_mesh->usagebits & USE_TEX1S);
	Vec3 offset = {0,0,0};
	SubdivArrays subdiv = {0};
	int i, j;
	TerrainMeshRenderMaterial **render_materials = NULL;
	RVertCache *vert_cache;
	U32 default_fpcw, final_fpcw;
	F32 *material_areas = NULL, total_area = 0;
	int *material_merges = NULL, *tex_id_map = NULL;

	PERFINFO_AUTO_START_FUNC();

	SET_FP_CONTROL_WORD_DEFAULT;
	_controlfp_s(&default_fpcw, 0, 0);

	assert(atlas_size);

	if (thread_params)
		timerStart(thread_params->timer_id);

	gmeshSetUsageBits(output_mesh, USE_POSITIONS | (use_material_fade?USE_COLORS:0) | USE_TEX1S | (has_texcoords?USE_TEX2S:0) | USE_NORMALS | USE_BINORMALS | USE_TANGENTS);
	gmeshSetVertCount(output_mesh, source_mesh->vert_count);
	gmeshSetVertCount(output_mesh, 0);
	gmeshEnsureTrisFit(output_mesh, source_mesh->tri_count);

	if (parent_mesh)
	{
		setVec3(offset, 
			local_pos[0] > parent_local_pos[0] ? atlas_size * GRID_BLOCK_SIZE : 0, 
			0, 
			local_pos[1] > parent_local_pos[1] ? atlas_size * GRID_BLOCK_SIZE : 0);

		subdivCreate(&subdiv, parent_mesh);
	}

	if (need_tangent_space)
	{
		if (disable_tangent_space)
		{
			gmeshSetUsageBits(source_mesh, source_mesh->usagebits | USE_TANGENTS | USE_BINORMALS);
			for (i = 0; i < source_mesh->vert_count; ++i)
			{
				setVec3(source_mesh->tangents[i], 1, 0, 0);
				setVec3(source_mesh->binormals[i], 0, 0, 1);
			}
		}
		else
		{
			gmeshAddTangentSpace_dbg(source_mesh, false, "Terrain", LOG_ERRORS, false MEM_DBG_PARMS_INIT); // must create tangent space on source mesh, where we have positions and texcoords in the right places
		}
	}

	if (use_material_fade)
	{
		vert_cache = ScratchAlloc(source_mesh->vert_count * sizeof(RVertCache) * MAX_VERT_CACHE);
		for (i = 0; i < source_mesh->vert_count * MAX_VERT_CACHE; ++i)
			vert_cache[i].idx = -1;
	}

	for (i = 0; i < source_mesh->tri_count; ++i)
	{
		GTriIdx *tri = &source_mesh->tris[i];
		int i0, i1, i2, tex_id;
		U8 *src_varcolors[3];
		F32 tri_area;

		i0 = tri->idx[0];
		i1 = tri->idx[1];
		i2 = tri->idx[2];

		if (source_mesh->varcolors)
		{
			src_varcolors[0] = &source_mesh->varcolors[i0*source_mesh->varcolor_size];
			src_varcolors[1] = &source_mesh->varcolors[i1*source_mesh->varcolor_size];
			src_varcolors[2] = &source_mesh->varcolors[i2*source_mesh->varcolor_size];
		}
		else
		{
			src_varcolors[0] = src_varcolors[1] = src_varcolors[2] = NULL;
		}

		tex_id = getMeshMaterial(src_varcolors, source_mesh->varcolor_size, &render_materials, NULL, use_material_fade);

		if (tex_id >= eafSize(&material_areas))
			eafSetSize(&material_areas, tex_id+1);

		ANALYSIS_ASSUME(material_areas);
		tri_area = triArea3(source_mesh->positions[i0], source_mesh->positions[i1], source_mesh->positions[i2]);
		material_areas[tex_id] += tri_area;
		total_area += tri_area;
	}

	assert(eafSize(&material_areas) == eaSize(&render_materials));
	eaiSetSize(&material_merges, eaSize(&render_materials));
	eaiSetSize(&tex_id_map, eaSize(&render_materials));

	for (i = 0; i < eaSize(&render_materials); ++i)
	{
		material_areas[i] = material_areas[i] / total_area;
		material_merges[i] = i;
		tex_id_map[i] = i;
	}

	for (i = 0; i < eaSize(&render_materials); ++i)
	{
		int tex_count1 = (render_materials[i]->detail_material_ids[1] == 255) ? 1 : ((render_materials[i]->detail_material_ids[2] == 255) ? 2 : 3);

		if (material_areas[i] < 0.2f && tex_count1 == 1 ||
			material_areas[i] < 0.1f && tex_count1 == 2)
		{
			int merge_idx = -1;
			F32 merge_area = -1;

			for (j = 0; j < eaSize(&render_materials); ++j)
			{
				int tex_count2 = (render_materials[j]->detail_material_ids[1] == 255) ? 1 : ((render_materials[j]->detail_material_ids[2] == 255) ? 2 : 3);

				if (i==j)
					continue;
				if (tex_count2 <= tex_count1)
					continue;

				if (!materialIsSubset(render_materials[i], render_materials[j]))
					continue;

				// find best material to merge with based on area and tex count
				if (material_areas[j] / tex_count2 > merge_area)
				{
					merge_area = material_areas[j] / tex_count2;
					merge_idx = j;
				}
			}

			if (merge_idx >= 0 && !disable_terrain_material_merge)
			{
				// merge materials
				material_merges[i] = merge_idx;
				material_areas[merge_idx] += material_areas[i];
				material_areas[i] = 0;
				for (j = i+1; j < eaSize(&render_materials); ++j)
					tex_id_map[j]--;
			}
		}
	}

	for (i = 0; i < source_mesh->tri_count; ++i)
	{
		GTriIdx *tri = &source_mesh->tris[i];
		int i0, i1, i2, tex_id, idx0, idx1, idx2;
		U8 *src_varcolors[3];
		RVertCache *cached_vert;
		Vec3 position;
		Vec2 tex1;
		Color c;

		i0 = tri->idx[0];
		i1 = tri->idx[1];
		i2 = tri->idx[2];

		if (source_mesh->varcolors)
		{
			src_varcolors[0] = &source_mesh->varcolors[i0*source_mesh->varcolor_size];
			src_varcolors[1] = &source_mesh->varcolors[i1*source_mesh->varcolor_size];
			src_varcolors[2] = &source_mesh->varcolors[i2*source_mesh->varcolor_size];
		}
		else
		{
			src_varcolors[0] = src_varcolors[1] = src_varcolors[2] = NULL;
		}

		tex_id = getMeshMaterial(src_varcolors, source_mesh->varcolor_size, &render_materials, material_merges, use_material_fade);

		cached_vert = NULL;
		if (use_material_fade)
		{
			c = transformVarColorToColor(src_varcolors[0], render_materials[tex_id]);
			for (j = 0; j < MAX_VERT_CACHE; ++j)
			{
				if (vert_cache[i0 * MAX_VERT_CACHE + j].idx >= 0 && c.integer_for_equality_only == vert_cache[i0 * MAX_VERT_CACHE + j].c.integer_for_equality_only)
				{
					cached_vert = &vert_cache[i0 * MAX_VERT_CACHE + j];
					break;
				}
			}
		}
		if (cached_vert)
		{
			idx0 = cached_vert->idx;
		}
		else
		{
			setVec3(position, source_mesh->positions[i0][1], 0, 0);
			setVec2(tex1, source_mesh->positions[i0][0] * position_multiplier, source_mesh->positions[i0][2] * position_multiplier);
			MIN1(*min_height, source_mesh->positions[i0][1]);
			MAX1(*max_height, source_mesh->positions[i0][1]);
			idx0 = gmeshAddVert(output_mesh, 
								position, NULL, 
								source_mesh->normals[i0], NULL, 
								source_mesh->binormals?source_mesh->binormals[i0]:NULL, 
								source_mesh->tangents?source_mesh->tangents[i0]:NULL, 
								tex1, has_texcoords?source_mesh->tex1s[i0]:NULL, 
								use_material_fade?&c:NULL, NULL, 
								NULL, NULL, 
								false, false, false);

			if (use_material_fade)
			{
				for (j = 0; j < MAX_VERT_CACHE; ++j)
				{
					if (vert_cache[i0 * MAX_VERT_CACHE + j].idx < 0)
					{
						vert_cache[i0 * MAX_VERT_CACHE + j].idx = idx0;
						vert_cache[i0 * MAX_VERT_CACHE + j].c = c;
						break;
					}
				}
			}
		}

		cached_vert = NULL;
		if (use_material_fade)
		{
			c = transformVarColorToColor(src_varcolors[1], render_materials[tex_id]);
			for (j = 0; j < MAX_VERT_CACHE; ++j)
			{
				if (vert_cache[i1 * MAX_VERT_CACHE + j].idx >= 0 && c.integer_for_equality_only == vert_cache[i1 * MAX_VERT_CACHE + j].c.integer_for_equality_only)
				{
					cached_vert = &vert_cache[i1 * MAX_VERT_CACHE + j];
					break;
				}
			}
		}
		if (cached_vert)
		{
			idx1 = cached_vert->idx;
		}
		else
		{
			setVec3(position, source_mesh->positions[i1][1], 0, 0);
			setVec2(tex1, source_mesh->positions[i1][0] * position_multiplier, source_mesh->positions[i1][2] * position_multiplier);
			MIN1(*min_height, source_mesh->positions[i1][1]);
			MAX1(*max_height, source_mesh->positions[i1][1]);
			idx1 = gmeshAddVert(output_mesh, 
								position, NULL, 
								source_mesh->normals[i1], NULL, 
								source_mesh->binormals?source_mesh->binormals[i1]:NULL, 
								source_mesh->tangents?source_mesh->tangents[i1]:NULL, 
								tex1, has_texcoords?source_mesh->tex1s[i1]:NULL, 
								use_material_fade?&c:NULL, NULL, 
								NULL, NULL, 
								false, false, false);

			if (use_material_fade)
			{
				for (j = 0; j < MAX_VERT_CACHE; ++j)
				{
					if (vert_cache[i1 * MAX_VERT_CACHE + j].idx < 0)
					{
						vert_cache[i1 * MAX_VERT_CACHE + j].idx = idx1;
						vert_cache[i1 * MAX_VERT_CACHE + j].c = c;
						break;
					}
				}
			}
		}

		cached_vert = NULL;
		if (use_material_fade)
		{
			c = transformVarColorToColor(src_varcolors[2], render_materials[tex_id]);
			for (j = 0; j < MAX_VERT_CACHE; ++j)
			{
				if (vert_cache[i2 * MAX_VERT_CACHE + j].idx >= 0 && c.integer_for_equality_only == vert_cache[i2 * MAX_VERT_CACHE + j].c.integer_for_equality_only)
				{
					cached_vert = &vert_cache[i2 * MAX_VERT_CACHE + j];
					break;
				}
			}
		}
		if (cached_vert)
		{
			idx2 = cached_vert->idx;
		}
		else
		{
			setVec3(position, source_mesh->positions[i2][1], 0, 0);
			setVec2(tex1, source_mesh->positions[i2][0] * position_multiplier, source_mesh->positions[i2][2] * position_multiplier);
			MIN1(*min_height, source_mesh->positions[i2][1]);
			MAX1(*max_height, source_mesh->positions[i2][1]);
			idx2 = gmeshAddVert(output_mesh, 
								position, NULL, 
								source_mesh->normals[i2], NULL, 
								source_mesh->binormals?source_mesh->binormals[i2]:NULL, 
								source_mesh->tangents?source_mesh->tangents[i2]:NULL, 
								tex1, has_texcoords?source_mesh->tex1s[i2]:NULL, 
								use_material_fade?&c:NULL, NULL, 
								NULL, NULL, 
								false, false, false);

			if (use_material_fade)
			{
				for (j = 0; j < MAX_VERT_CACHE; ++j)
				{
					if (vert_cache[i2 * MAX_VERT_CACHE + j].idx < 0)
					{
						vert_cache[i2 * MAX_VERT_CACHE + j].idx = idx2;
						vert_cache[i2 * MAX_VERT_CACHE + j].c = c;
						break;
					}
				}
			}
		}

		gmeshAddTri(output_mesh, idx0, idx1, idx2, tex_id_map[tex_id], false);
	}

	if (use_material_fade)
		ScratchFree(vert_cache);

	_controlfp_s(&final_fpcw, 0, 0);
	assert(default_fpcw == final_fpcw);

	if (parent_mesh)
	{
		// find LOD values
		for (i = 0; i < output_mesh->vert_count; ++i)
		{
			// put parent atlas's heights and texcoords into lod values
			F32 lod_height = output_mesh->positions[i][0];
			F32 lod_texcoord_y = has_texcoords?output_mesh->tex2s[i][1]:0;
			Vec3 parent_position;
			int *tri_idxs;

			// only need lod values for edge verts
			if (output_mesh->tex1s[i][0] > 0.1f && output_mesh->tex1s[i][0] < 0.9f && output_mesh->tex1s[i][1] > 0.1f && output_mesh->tex1s[i][1] < 0.9f)
				continue;

			setVec3(parent_position, output_mesh->tex1s[i][0] * atlas_size * GRID_BLOCK_SIZE, output_mesh->positions[i][0], output_mesh->tex1s[i][1] * atlas_size * GRID_BLOCK_SIZE);
			addVec3(parent_position, offset, parent_position);

			tri_idxs = subdivGetBucket(&subdiv, parent_position);

			for (j = 0; j < eaiSize(&tri_idxs); ++j)
			{
				GTriIdx *tri = &parent_mesh->tris[tri_idxs[j]];
				Vec3 barycentric_coords;

				// early out
				if (( parent_position[0] > parent_mesh->positions[tri->idx[0]][0] && 
					  parent_position[0] > parent_mesh->positions[tri->idx[1]][0] && 
					  parent_position[0] > parent_mesh->positions[tri->idx[2]][0]) || 
					 (parent_position[0] < parent_mesh->positions[tri->idx[0]][0] && 
					  parent_position[0] < parent_mesh->positions[tri->idx[1]][0] && 
					  parent_position[0] < parent_mesh->positions[tri->idx[2]][0]) || 
					 (parent_position[2] > parent_mesh->positions[tri->idx[0]][2] && 
					  parent_position[2] > parent_mesh->positions[tri->idx[1]][2] && 
					  parent_position[2] > parent_mesh->positions[tri->idx[2]][2]) || 
					 (parent_position[2] < parent_mesh->positions[tri->idx[0]][2] && 
					  parent_position[2] < parent_mesh->positions[tri->idx[1]][2] && 
					  parent_position[2] < parent_mesh->positions[tri->idx[2]][2]))
				{
					continue;
				}

				if (nearSameVec3XZTol(parent_position, parent_mesh->positions[tri->idx[0]], 0.03f))
				{
					lod_height = parent_mesh->positions[tri->idx[0]][1];
					if (parent_has_texcoords)
						lod_texcoord_y = parent_mesh->tex1s[tri->idx[0]][1];
					break;
				}
				else if (nearSameVec3XZTol(parent_position, parent_mesh->positions[tri->idx[1]], 0.03f))
				{
					lod_height = parent_mesh->positions[tri->idx[1]][1];
					if (parent_has_texcoords)
						lod_texcoord_y = parent_mesh->tex1s[tri->idx[1]][1];
					break;
				}
				else if (nearSameVec3XZTol(parent_position, parent_mesh->positions[tri->idx[2]], 0.03f))
				{
					lod_height = parent_mesh->positions[tri->idx[2]][1];
					if (parent_has_texcoords)
						lod_texcoord_y = parent_mesh->tex1s[tri->idx[2]][1];
					break;
				}
				else if (calcBarycentricCoordsXZProjected(parent_mesh->positions[tri->idx[0]], parent_mesh->positions[tri->idx[1]], parent_mesh->positions[tri->idx[2]], parent_position, barycentric_coords))
				{
					lod_height = barycentric_coords[0] * parent_mesh->positions[tri->idx[0]][1] + barycentric_coords[1] * parent_mesh->positions[tri->idx[1]][1] + barycentric_coords[2] * parent_mesh->positions[tri->idx[2]][1];
					if (parent_has_texcoords)
						lod_texcoord_y = barycentric_coords[0] * parent_mesh->tex1s[tri->idx[0]][1] + barycentric_coords[1] * parent_mesh->tex1s[tri->idx[1]][1] + barycentric_coords[2] * parent_mesh->tex1s[tri->idx[2]][1];
					break;
				}
			}

// 			if (j == eaiSize(&tri_idxs))
// 				lod_height = 900; // (not found)
			if (!FINITE(lod_height))
				lod_height = output_mesh->positions[i][0];
// 			if (ABS(lod_height - output_mesh->positions[i][0]) > 50)
// 				printf("");

			MIN1(*min_height, lod_height);
			MAX1(*max_height, lod_height);

			output_mesh->positions[i][1] = lod_height;
			output_mesh->positions[i][2] = 0; //lod_texcoord_y
		}
	}

	if (parent_mesh)
		subdivClear(&subdiv);

	if (need_tangent_space)
		gmeshSetUsageBits(source_mesh, source_mesh->usagebits & ~(USE_TANGENTS|USE_BINORMALS)); // remove tangent space after writing to output

	for (i = eaSize(&render_materials) - 1; i >= 0; --i)
	{
		if (material_merges[i] != i)
		{
			free(render_materials[i]);
			eaRemove(&render_materials, i);
		}
	}

	eafDestroy(&material_areas);
	eaiDestroy(&material_merges);
	eaiDestroy(&tex_id_map);

	if (thread_params)
		thread_params->timings[TAT_CONVERTING] += timerElapsed(thread_params->timer_id);

	_controlfp_s(&final_fpcw, 0, 0);
	assert(default_fpcw == final_fpcw);

	PERFINFO_AUTO_STOP();

	return render_materials;
}

static void writeModelBin(HeightMapBinAtlas *atlas, int atlas_size, SimpleBufHandle buf, SimpleBufHandle geo_buf, SimpleBufHandle coll_buf, bool use_material_fade, PerThreadAtlasParams *thread_params)
{
	Geo2LoadData *gld = NULL, *coll_gld = NULL;
	GMesh *source_mesh = atlas->mesh;
	GMesh output_mesh = {0};
	int i;

	if (source_mesh)
	{
		TerrainMeshRenderMaterial **render_materials;
		F32 min_height = HEIGHTMAP_MAX_HEIGHT, max_height = HEIGHTMAP_MIN_HEIGHT;
		HeightMapAtlas *atlas_bin_data;
		IVec3 key;

		render_materials = terrainConvertHeightMapMeshToRenderable(source_mesh, atlas->local_pos, SAFE_MEMBER(atlas->parent, mesh), SAFE_MEMBER(atlas->parent, local_pos), &output_mesh, atlas_size, &min_height, &max_height, use_material_fade, false, thread_params);

		timerStart(thread_params->timer_id);

		SimpleBufWriteU8(eaSize(&atlas->detail_material_names), buf);
		for (i = 0; i < eaSize(&atlas->detail_material_names); ++i)
			SimpleBufWriteString(atlas->detail_material_names[i], buf);

		SimpleBufWriteU8(eaSize(&render_materials), buf);
		for (i = 0; i < eaSize(&render_materials); ++i)
		{
			SimpleBufWriteU8(render_materials[i]->detail_material_ids[0], buf);
			SimpleBufWriteU8(render_materials[i]->detail_material_ids[1], buf);
			SimpleBufWriteU8(render_materials[i]->detail_material_ids[2], buf);
			SimpleBufWriteU8(render_materials[i]->color_idxs[0], buf);
			SimpleBufWriteU8(render_materials[i]->color_idxs[1], buf);
			SimpleBufWriteU8(render_materials[i]->color_idxs[2], buf);
		}

		eaDestroyEx(&render_materials, NULL);

		modelAddGMeshToGLD(&gld, &output_mesh, (atlas->lod==0) ? TERRAIN_TILE_MODEL_NAME : TERRAIN_ATLAS_MODEL_NAME, NULL, 0, true, false, false, buf);
		gmeshFreeData(&output_mesh);

		EnterCriticalSection(&terrain_atlas_bins_cs);
		setVec3(key, atlas->local_pos[0], atlas->local_pos[1], atlas->lod);
		if (stashFindPointer(g_AtlasThreadProcInfo.atlases->atlas_hash, key, &atlas_bin_data))
		{
			MIN1(atlas_bin_data->min_height, min_height);
			MAX1(atlas_bin_data->max_height, max_height);
		}
		LeaveCriticalSection(&terrain_atlas_bins_cs);

		thread_params->timings[TAT_WRITING] += timerElapsed(thread_params->timer_id);
	}
	else
	{
		SimpleBufWriteU8(0, buf);
		SimpleBufWriteU8(0, buf);
		modelAddGMeshToGLD(&gld, NULL, (atlas->lod==0) ? TERRAIN_TILE_MODEL_NAME : TERRAIN_ATLAS_MODEL_NAME, NULL, 0, true, false, false, buf);
	}

	timerStart(thread_params->timer_id);

	modelAddGMeshToGLD(&gld, atlas->occlusion_mesh, (atlas->lod==0) ? TERRAIN_TILE_OCC_MODEL_NAME : TERRAIN_ATLAS_OCC_MODEL_NAME, NULL, 0, true, false, false, buf);

	if (gld)
		modelWriteAndFreeBinGLD(gld, geo_buf, false);

	if (coll_buf && source_mesh)
	{
		const char **coll_material_names = NULL;

		gmeshCopy(&output_mesh, source_mesh, false);
		gmeshSetUsageBits(&output_mesh, USE_POSITIONS);
		
		for (i = 0; i < output_mesh.tri_count; ++i)
		{
			int i0 = output_mesh.tris[i].idx[0], i1 = output_mesh.tris[i].idx[1], i2 = output_mesh.tris[i].idx[2];
			int highest_material_id = findHighestMaterialId(&source_mesh->varcolors[i0*source_mesh->varcolor_size], &source_mesh->varcolors[i1*source_mesh->varcolor_size], &source_mesh->varcolors[i2*source_mesh->varcolor_size], source_mesh->varcolor_size);
			int tex_id;
			
			assert(eaSize(&atlas->detail_material_names) > highest_material_id);
			ANALYSIS_ASSUME(atlas->detail_material_names);

			tex_id = eaFind(&coll_material_names, atlas->detail_material_names[highest_material_id]);
			if (tex_id < 0)
				tex_id = eaPush(&coll_material_names, atlas->detail_material_names[highest_material_id]);
			output_mesh.tris[i].tex_id = tex_id;
		}

		gmeshPool(&output_mesh, false, false, false);
		modelAddGMeshToGLD(&coll_gld, &output_mesh, TERRAIN_TILE_COLL_MODEL_NAME, coll_material_names, eaSize(&coll_material_names), false, true, false, buf);
		eaDestroy(&coll_material_names);
		gmeshFreeData(&output_mesh);
		if (coll_gld)
		{
			extern const char *g_debug_write_collision_data;
			if (write_debug_collision_data)
			{
				static char coll_debug_filename[MAX_PATH];
				sprintf(coll_debug_filename, "%s/col/%d_%d.col", fileTempDir(), atlas->local_pos[0], atlas->local_pos[1]);
				g_debug_write_collision_data = coll_debug_filename;
			}
			modelWriteAndFreeBinGLD(coll_gld, coll_buf, true);
			g_debug_write_collision_data = NULL;
		}
	}
	else
	{
		modelAddGMeshToGLD(&coll_gld, NULL, TERRAIN_TILE_COLL_MODEL_NAME, NULL, 0, false, true, false, buf);
	}

	thread_params->timings[TAT_WRITING] += timerElapsed(thread_params->timer_id);
}

static bool checkAtlasVisibilityToLayer(Vec3 layer_min, Vec3 layer_max, Vec2 min, Vec2 max, F32 *min_lod_value)
{
	Vec2 pos;
	F32 xdist, ydist, dist, cur_lod_mul, lod_value;

	layer_min[0] += 10; // undo extra offset
	layer_min[2] += 10; // undo extra offset
	layer_max[0] -= 10; // undo extra offset
	layer_max[2] -= 10; // undo extra offset

	if (layer_min[0] > max[0])
	{
		pos[0] = layer_min[0];
		xdist = layer_min[0] - max[0];
	}
	else if (layer_max[0] < min[0])
	{
		pos[0] = layer_max[0];
		xdist = min[0] - layer_max[0];
	}
	else
	{
		xdist = 0;
	}

	if (layer_min[2] > max[1])
	{
		pos[1] = layer_min[2];
		ydist = layer_min[2] - max[1];
	}
	else if (layer_max[2] < min[1])
	{
		pos[1] = layer_max[2];
		ydist = min[1] - layer_max[2];
	}
	else
	{
		ydist = 0;
	}

	if (xdist == 0 && ydist == 0)
		return true;

	if (xdist && round(pos[0] / GRID_BLOCK_SIZE) % 2 == 0)
	{
		xdist -= GRID_BLOCK_SIZE;
		MAX1(xdist, 0);
	}

	if (ydist && round(pos[1] / GRID_BLOCK_SIZE) % 2 == 0)
	{
		ydist -= GRID_BLOCK_SIZE;
		MAX1(ydist, 0);
	}

	dist = MAX(xdist, ydist);

	lod_value = 0;
	cur_lod_mul = 1;
	while (	dist >= GRID_BLOCK_SIZE * cur_lod_mul && 
		lod_value <= ATLAS_MAX_LOD)
	{
		dist -= GRID_BLOCK_SIZE * cur_lod_mul;
		cur_lod_mul *= 2;
		lod_value += 1;
	}

	MIN1(*min_lod_value, lod_value);
	return false;
}

static bool checkAtlasVisibility(const IVec2 local_pos, int lod, CombinedAtlasBinStruct *info)
{
	ZoneMapLayer **playable_layers = info->playable_layers;
	IVec2 parent_local_pos;
	Vec2 min, max;
	F32 min_lod_value = ATLAS_MAX_LOD;
	int i, parent_lod = lod + 1;

	if (lod >= ATLAS_MAX_LOD)
		return true;

	// check the distance to the parent, because if it subdivides then all children need to exist
	setVec2(parent_local_pos,
		local_pos[0] - (ABS(local_pos[0]) % heightmap_atlas_size[parent_lod]),
		local_pos[1] - (ABS(local_pos[1]) % heightmap_atlas_size[parent_lod]));

	setVec2(min, 
		parent_local_pos[0] * GRID_BLOCK_SIZE, 
		parent_local_pos[1] * GRID_BLOCK_SIZE);
	setVec2(max, 
		(parent_local_pos[0] + heightmap_atlas_size[parent_lod]) * GRID_BLOCK_SIZE, 
		(parent_local_pos[1] + heightmap_atlas_size[parent_lod]) * GRID_BLOCK_SIZE);

	//If there are no playable layers, then 
	if(eaSize(&playable_layers)==0)
	{
		Vec3 layer_min, layer_max;
		layer_min[0] = info->terrain_playable_min[0];
		layer_min[1] = 0;
		layer_min[2] = info->terrain_playable_min[1];
		layer_max[0] = info->terrain_playable_max[0];
		layer_max[1] = 0;
		layer_max[2] = info->terrain_playable_max[1];
		if(checkAtlasVisibilityToLayer(layer_min, layer_max, min, max, &min_lod_value))
			return true;
	}
	else
	{
		for (i = 0; i < eaSize(&playable_layers); i++)
		{
			Vec3 layer_min, layer_max;
			layerGetTerrainBounds(playable_layers[i], layer_min, layer_max);
			if(checkAtlasVisibilityToLayer(layer_min, layer_max, min, max, &min_lod_value))
				return true;
		}
	}

	return min_lod_value < parent_lod;
}

static void mergeHeightMapBinAtlasColors(HeightMapBinAtlas *atlas, HeightMapBinAtlas *source_atlas, int atlas_size)
{
	int x, y;

	// merge color textures
	for (y = 0; y < atlas_size; y++)
	{
		for (x = 0; x < atlas_size; x++)
		{
			int tile_size = COLORMAP_SIZE(atlas->lod) - 1;
			int idx = (x*tile_size)*4 + (y*tile_size)*HEIGHTMAP_ATLAS_COLOR_SIZE*4;
			if (source_atlas->color_array[idx+3]) // check the alpha byte
			{
				int py;
				for (py = 0; py <= tile_size; py++)
				{
					memcpy(&atlas->color_array[idx],
						&source_atlas->color_array[idx],
						(tile_size+1)*4);
					idx += HEIGHTMAP_ATLAS_COLOR_SIZE*4;
				}
			}
		}
	}
}

static void writeAtlasColors(HeightMapBinAtlas *atlas, SimpleBufHandle buf, PerThreadAtlasParams *thread_params)
{
	int b, l, atlas_size = heightmap_atlas_size[atlas->lod];

	if (atlas->lod == 0)
	{
		bool found = false;

		for (l = 0; !found && l < eaSize(&g_AtlasThreadProcInfo.layers); l++)
		{
			ZoneMapLayer *layer = g_AtlasThreadProcInfo.layers[l];
			for (b = 0; b < eaSize(&layer->terrain.blocks); b++)
			{
				TerrainBlockRange *block = layer->terrain.blocks[b];
				if (block && 
					!(	block->range.max_block[0] < atlas->local_pos[0] ||
						block->range.max_block[2] < atlas->local_pos[1] ||
						atlas->local_pos[0] + atlas_size < block->range.min_block[0] ||
						atlas->local_pos[1] + atlas_size < block->range.min_block[2]
					 )
					)
				{
					if (layerLoadBlockAtlas(layer, b, atlas))
					{
						found = true;
						break;
					}
				}
			}
		}
	}
	else
	{
		HeightMapAtlas *atlas_bin_data = NULL;

		atlas->color_array = calloc(color_datasize, sizeof(U8));
		atlas->is_dxt_color = false;

		// merge color textures
		for (l = 0; l < eaSize(&g_AtlasThreadProcInfo.layers); l++)
		{
			ZoneMapLayer *layer = g_AtlasThreadProcInfo.layers[l];
			for (b = 0; b < eaSize(&layer->terrain.blocks); b++)
			{
				TerrainBlockRange *block = layer->terrain.blocks[b];
				if (block && 
					!(	block->range.max_block[0] < atlas->local_pos[0] ||
						block->range.max_block[2] < atlas->local_pos[1] ||
						atlas->local_pos[0] + atlas_size < block->range.min_block[0] ||
						atlas->local_pos[1] + atlas_size < block->range.min_block[2]
					 )
					)
				{
					HeightMapBinAtlas source_atlas = {0};
					copyVec2(atlas->local_pos, source_atlas.local_pos);
					source_atlas.lod = atlas->lod;

					if (layerLoadBlockAtlas(layer, b, &source_atlas))
					{
						mergeHeightMapBinAtlasColors(atlas, &source_atlas, atlas_size);
						atlasBinFreeData(&source_atlas);

						if (!atlas_bin_data)
						{
							atlas_bin_data = StructAlloc(parse_HeightMapAtlas);
							atlas_bin_data->lod = atlas->lod;
							copyVec2(atlas->local_pos, atlas_bin_data->local_pos);
						}

						atlas_bin_data->min_height = HEIGHTMAP_MAX_HEIGHT;
						atlas_bin_data->max_height = HEIGHTMAP_MIN_HEIGHT;
						atlas_bin_data->layer_bitfield |= 1 << l;
					}
				}
			}
		}

		if (atlas_bin_data)
		{
			EnterCriticalSection(&terrain_atlas_bins_cs);
			eaPush(&g_AtlasThreadProcInfo.atlases->all_atlases, atlas_bin_data);
			stashAddPointer(g_AtlasThreadProcInfo.atlases->atlas_hash, atlas_bin_data, atlas_bin_data, false);
			LeaveCriticalSection(&terrain_atlas_bins_cs);
		}
		else
		{
			SAFE_FREE(atlas->color_array);
		}
	}

	if (!terrain_atlas_high_color && atlas->lod == 0)
	{
		SAFE_FREE(atlas->color_array);
	}

	if (atlas->color_array)
	{
		SimpleBufWriteU32(color_dxt_datasize, buf);

		if (atlas->is_dxt_color)
		{
			SimpleBufWrite(atlas->color_array, color_dxt_datasize, buf);
		}
		else
		{
			int dxt_size;
			void *dxt_data = SimpleBufReserve(color_dxt_datasize, buf);
			timerStart(thread_params->timer_id);
			dxt_size = nvdxtCompress(atlas->color_array, dxt_data, HEIGHTMAP_ATLAS_COLOR_SIZE, HEIGHTMAP_ATLAS_COLOR_SIZE, RTEX_DXT1, gConf.iDXTQuality, ATLAS_COLOR_TEXTURE_SIZE);
			assert(dxt_size == color_dxt_datasize);
			thread_params->timings[TAT_COMPRESSING] += timerElapsed(thread_params->timer_id);
		}

#if DEBUG_COLOR_TEXTURES
		{
			char debug_filename[MAX_PATH];
			sprintf(debug_filename, "%s/dstimg/%d_%d_%d.tga", fileTempDir(), atlas->local_pos[0], atlas->local_pos[1], atlas->lod);
			tgaSave(debug_filename, atlas->color_array, HEIGHTMAP_ATLAS_COLOR_SIZE, HEIGHTMAP_ATLAS_COLOR_SIZE, 4);
		}
#endif

		SAFE_FREE(atlas->color_array);
	}
	else
	{
		SimpleBufWriteU32(0, buf);
	}
}

// During client BIN saving, save composited atlas files
// Please update dumpAtlHeader if you modify this file format.
static void writeAtlasBin(HeightMapBinAtlas *atlas, SimpleBufHandle *buf_ptr, SimpleBufHandle *geo_buf_ptr, SimpleBufHandle *coll_buf_ptr, PerThreadAtlasParams *thread_params)
{
	SimpleBufHandle buf, geo_buf, coll_buf = NULL;
	char path[MAX_PATH];

	if (!atlas->mesh || !checkAtlasVisibility(atlas->local_pos, atlas->lod, &g_AtlasThreadProcInfo))
	{
		*buf_ptr = NULL;
		*geo_buf_ptr = NULL;
		*coll_buf_ptr = NULL;
		return;
	}

	sprintf(path, "x%d_z%d.atl", atlas->local_pos[0], atlas->local_pos[1]);
	buf = SimpleBufOpenWrite(path, true, g_AtlasThreadProcInfo.terrain_atlas_hoggs[atlas->lod], true, false);

	SimpleBufWriteU32(TERRAIN_ATLAS_VERSION, buf);

	writeAtlasColors(atlas, buf, thread_params);

	sprintf(path, "x%d_z%d.mset", atlas->local_pos[0], atlas->local_pos[1]);
	geo_buf = SimpleBufOpenWrite(path, true, g_AtlasThreadProcInfo.terrain_model_hoggs[atlas->lod], true, false);

	if (atlas->lod == 0 && atlas->needs_collision)
	{
		sprintf(path, "x%d_z%d.mset", atlas->local_pos[0], atlas->local_pos[1]);
		coll_buf = SimpleBufOpenWrite(path, true, g_AtlasThreadProcInfo.terrain_coll_hogg, true, false);
	}

	writeModelBin(atlas, heightmapAtlasGetLODSize(atlas->lod), buf, geo_buf, coll_buf, atlas->lod <= 3, thread_params);

	if (geo_buf && !SimpleBufTell(geo_buf))
	{
		SimpleBufCloseNoWriteIfEmpty(geo_buf);
		geo_buf = NULL;
	}

	if (coll_buf && !SimpleBufTell(coll_buf))
	{
		SimpleBufCloseNoWriteIfEmpty(coll_buf);
		coll_buf = NULL;
	}

	*buf_ptr = buf;
	*geo_buf_ptr = geo_buf;
	*coll_buf_ptr = coll_buf;
}

static GMesh * optimizedMergeAtlasGMesh(HeightMapBinAtlas *atlas, const HeightMapBinAtlas *child_atlas, 
	GMesh * destMesh, const GMesh * sourceMesh, const Vec3 offset, bool bOcclusion)
{
	int i;

	// merge geometry
	if (!destMesh)
	{
		destMesh = calloc(1, sizeof(GMesh));
		gmeshCopy(destMesh, sourceMesh, false);
		gmeshSetUsageBits(destMesh, destMesh->usagebits & ~(USE_TANGENTS|USE_BINORMALS)); // recalc tangent space after reducing and seaming
		if (!bOcclusion)
			eaCopy(&atlas->detail_material_names, &child_atlas->detail_material_names); // pooled strings
		for (i = 0; i < destMesh->vert_count; ++i)
			addVec3(destMesh->positions[i], offset, destMesh->positions[i]);
	}
	else
	{
		// optimized form of gmeshMerge
		int child_atlas_size = heightmapAtlasGetLODSize(child_atlas->lod);

		F32 border_value = child_atlas_size * GRID_BLOCK_SIZE;
		int *vert_cache = ScratchAlloc(sourceMesh->vert_count * sizeof(int));
		U8 *varcolor;

		memset(vert_cache, 0xffffffff, sourceMesh->vert_count * sizeof(int));

		if (!bOcclusion)
		{
			for (i = 0; i < eaSize(&child_atlas->detail_material_names); ++i)
				eaPushUnique(&atlas->detail_material_names, child_atlas->detail_material_names[i]);
			gmeshSetVarColorSize(destMesh, eaSize(&atlas->detail_material_names));
			varcolor = _alloca(eaSize(&atlas->detail_material_names) * sizeof(U8));
		}

		for (i = 0; i < sourceMesh->tri_count; i++)
		{
			GTriIdx *srctri = &sourceMesh->tris[i];
			int i0 = srctri->idx[0], i1 = srctri->idx[1], i2 = srctri->idx[2];
			bool pool_vert;
			Vec3 pos;

			if (i0 < 0 || i1 < 0 || i2 < 0)
				continue;

			if (vert_cache[i0] < 0)
			{
				if (sourceMesh->varcolors)
					transformVarColor(varcolor, atlas->detail_material_names, &sourceMesh->varcolors[i0*sourceMesh->varcolor_size], child_atlas->detail_material_names);

				addVec3(sourceMesh->positions[i0], offset, pos);
				pool_vert = pos[0] == border_value || pos[2] == border_value;
				vert_cache[i0] = gmeshAddVert(destMesh,
					pos,
					0,
					sourceMesh->normals?sourceMesh->normals[i0]:0,
					0,
					0,
					0,
					sourceMesh->tex1s?sourceMesh->tex1s[i0]:0,
					sourceMesh->tex2s?sourceMesh->tex2s[i0]:0,
					0,
					sourceMesh->varcolors ? varcolor : 0,
					0,
					0,
					pool_vert, false, false);
			}

			if (vert_cache[i1] < 0)
			{
				if (sourceMesh->varcolors)
					transformVarColor(varcolor, atlas->detail_material_names, &sourceMesh->varcolors[i1*sourceMesh->varcolor_size], child_atlas->detail_material_names);

				addVec3(sourceMesh->positions[i1], offset, pos);
				pool_vert = pos[0] == border_value || pos[2] == border_value;
				vert_cache[i1] = gmeshAddVert(destMesh,
					pos,
					0,
					sourceMesh->normals?sourceMesh->normals[i1]:0,
					0,
					0,
					0,
					sourceMesh->tex1s?sourceMesh->tex1s[i1]:0,
					sourceMesh->tex2s?sourceMesh->tex2s[i1]:0,
					0,
					sourceMesh->varcolors ? varcolor : 0,
					0,
					0,
					pool_vert, false, false);
			}

			if (vert_cache[i2] < 0)
			{
				if (sourceMesh->varcolors)
					transformVarColor(varcolor, atlas->detail_material_names, &sourceMesh->varcolors[i2*sourceMesh->varcolor_size], child_atlas->detail_material_names);

				addVec3(sourceMesh->positions[i2], offset, pos);
				pool_vert = pos[0] == border_value || pos[2] == border_value;
				vert_cache[i2] = gmeshAddVert(destMesh,
					pos,
					0,
					sourceMesh->normals?sourceMesh->normals[i2]:0,
					0,
					0,
					0,
					sourceMesh->tex1s?sourceMesh->tex1s[i2]:0,
					sourceMesh->tex2s?sourceMesh->tex2s[i2]:0,
					0,
					sourceMesh->varcolors ? varcolor : 0,
					0,
					0,
					pool_vert, false, false);
			}

			gmeshAddTri(destMesh, 
						vert_cache[i0], 
						vert_cache[i1], 
						vert_cache[i2], 
						srctri->tex_id, false);
		}

		ScratchFree(vert_cache);
	}

	return destMesh;
}

static void mergeHeightMapBinAtlas(HeightMapBinAtlas *atlas, HeightMapBinAtlas *child_atlas, PerThreadAtlasParams *thread_params)
{
	SET_FP_CONTROL_WORD_DEFAULT;

	if (child_atlas && child_atlas->mesh)
	{
		int child_atlas_size = heightmapAtlasGetLODSize(child_atlas->lod);
		Vec3 offset;

		PERFINFO_AUTO_START_FUNC();

		timerStart(thread_params->timer_id);

		setVec3(offset, 
			child_atlas->local_pos[0] > atlas->local_pos[0] ? child_atlas_size * GRID_BLOCK_SIZE : 0, 
			0, 
			child_atlas->local_pos[1] > atlas->local_pos[1] ? child_atlas_size * GRID_BLOCK_SIZE : 0);

		atlas->mesh = optimizedMergeAtlasGMesh(atlas, child_atlas, 
			atlas->mesh, child_atlas->mesh, 
			offset, false);
#if 0 
		// This prototype code is disabled until we can add editor controls, and measure cost/benefit
		atlas->occlusion_mesh = optimizedMergeAtlasGMesh(atlas, child_atlas, 
			atlas->occlusion_mesh, child_atlas->occlusion_mesh, offset, true);
#endif

		atlas->tile_count += child_atlas->tile_count;

		thread_params->timings[TAT_MERGING] += timerElapsedAndStart(thread_params->timer_id);

		PERFINFO_AUTO_STOP_FUNC();
	}
}

#ifdef _FULLDEBUG
static void checkSeamLogMatch(const char *str, FILE *logfile)
{
	char logstr[2048], *s2;
	const char *s1;
	int logstrlen;
	logstr[0] = 0;
	fgets(logstr, ARRAY_SIZE(logstr)-1, logfile);

	logstrlen = (int)strlen(logstr);
	if (logstrlen)
		if (logstr[logstrlen-1] == 10)
			logstr[logstrlen-1] = 0;

	s1 = strstriConst(str, "normal");
	s2 = strstri(logstr, "normal");
	if (s1 && s2)
	{
		ANALYSIS_ASSUME(s1);
		ANALYSIS_ASSUME(s2);
		if (strcmp(s1, s2) != 0)
			_DbgBreak();
	}
}

#define SEAM_LOG(fmt, ...)	if (terrain_seam_log) \
							{ \
								if (terrain_seam_logging == 2) \
								{ \
									char *str = NULL; \
									estrPrintf(&str, fmt, __VA_ARGS__); \
									checkSeamLogMatch(str, terrain_seam_log); \
									estrDestroy(&str); \
								} \
								else \
								{ \
									fprintf(terrain_seam_log, fmt "\n", __VA_ARGS__); \
								} \
							}
#else
#define SEAM_LOG(fmt, ...)
#endif

static void getSortedBorderPositions(GMesh *mesh, int test_subscript, F32 test_value, int compare_subscript, F32 width, 
									 int **idxs_out, int **non_edge_idxs_out, 
									 F32 height0, F32 height1,							// corner average heights
									 const Vec3 normal0, const Vec3 normal1,			// corner average normals
									 const U8 *color0, const U8 *color1,				// corner average varcolors (same array size as corner materials)
									 const char **materials0, const char **materials1,	// corner common materials
									 U8 *tmp_color, const char **common_materials, const char **detail_material_names, 
									 FILE *terrain_seam_log, int srcidx)
{
	int *idxs = NULL;
	int *non_edge_idxs = NULL;
	int i, j;
	int common_msize = eaSize(&common_materials);

	for (i = 0; i < mesh->vert_count; ++i)
	{
		if (mesh->positions[i][test_subscript] == test_value)
		{
			int dir = -1;

			if (mesh->positions[i][compare_subscript] == 0)
			{
				mesh->positions[i][1] = height0;
				if (mesh->usagebits & USE_NORMALS)
					copyVec3(normal0, mesh->normals[i]);
				if (tmp_color)
				{
					transformVarColor(tmp_color, common_materials, color0, materials0);
					if (!VarColorIsZero(tmp_color, common_msize))
						transformVarColor(&mesh->varcolors[i*mesh->varcolor_size], detail_material_names, tmp_color, common_materials);
				}
			}
			else if (mesh->positions[i][compare_subscript] == width)
			{
				mesh->positions[i][1] = height1;
				if (mesh->usagebits & USE_NORMALS)
					copyVec3(normal1, mesh->normals[i]);
				transformVarColor(tmp_color, common_materials, color1, materials1);
				if (tmp_color)
				{
					if (!VarColorIsZero(tmp_color, common_msize))
						transformVarColor(&mesh->varcolors[i*mesh->varcolor_size], detail_material_names, tmp_color, common_materials);
				}
			}

			for (j = 0; j < mesh->tri_count && dir < 0; ++j)
			{
				GTriIdx *tri = &mesh->tris[j];
				if (gmeshTrisMatch1(tri, i))
				{
					if (tri->idx[0] == i)
					{
						if (mesh->positions[tri->idx[1]][test_subscript] == test_value)
							dir = mesh->positions[tri->idx[1]][compare_subscript] > mesh->positions[i][compare_subscript];
						else if (mesh->positions[tri->idx[2]][test_subscript] == test_value)
							dir = mesh->positions[tri->idx[2]][compare_subscript] > mesh->positions[i][compare_subscript];
					}
					else if (tri->idx[1] == i)
					{
						if (mesh->positions[tri->idx[0]][test_subscript] == test_value)
							dir = mesh->positions[tri->idx[0]][compare_subscript] > mesh->positions[i][compare_subscript];
						else if (mesh->positions[tri->idx[2]][test_subscript] == test_value)
							dir = mesh->positions[tri->idx[2]][compare_subscript] > mesh->positions[i][compare_subscript];
					}
					else
					{
						if (mesh->positions[tri->idx[0]][test_subscript] == test_value)
							dir = mesh->positions[tri->idx[0]][compare_subscript] > mesh->positions[i][compare_subscript];
						else if (mesh->positions[tri->idx[1]][test_subscript] == test_value)
							dir = mesh->positions[tri->idx[1]][compare_subscript] > mesh->positions[i][compare_subscript];
					}
				}
			}

			// no triangle along boundary found for this vert, put it in list of verts that need height update
			if (dir == -1)
			{
				for (j = 0; j < eaiSize(&non_edge_idxs) && mesh->positions[i][compare_subscript] >= mesh->positions[non_edge_idxs[j]][compare_subscript]; ++j)
					;
				eaiInsert(&non_edge_idxs, i, j);
				continue;
			}

			// insert position index into the correct place in the idxs array
			if (dir)
			{
				// put at end of duplicates
				for (j = 0; j < eaiSize(&idxs) && mesh->positions[i][compare_subscript] >= mesh->positions[idxs[j]][compare_subscript]; ++j)
					;
			}
			else
			{
				// put at head of duplicates
				for (j = 0; j < eaiSize(&idxs) && mesh->positions[i][compare_subscript] > mesh->positions[idxs[j]][compare_subscript]; ++j)
					;
			}
			eaiInsert(&idxs, i, j);
		}
	}

	*idxs_out = idxs;
	*non_edge_idxs_out = non_edge_idxs;

	if (terrain_seam_log)
	{
		for (i = 0; i < eaiSize(&idxs); ++i)
		{
			SEAM_LOG("%d_%d idx (%f, %f, %f) (%f, %f, %f)", srcidx, idxs[i], mesh->positions[idxs[i]][0], mesh->positions[idxs[i]][1], mesh->positions[idxs[i]][2], mesh->normals[idxs[i]][0], mesh->normals[idxs[i]][1], mesh->normals[idxs[i]][2]);
		}
		for (i = 0; i < eaiSize(&non_edge_idxs); ++i)
		{
			SEAM_LOG("%d_%d idx non edge (%f, %f, %f) (%f, %f, %f)", srcidx, non_edge_idxs[i], mesh->positions[non_edge_idxs[i]][0], mesh->positions[non_edge_idxs[i]][1], mesh->positions[non_edge_idxs[i]][2], mesh->normals[non_edge_idxs[i]][0], mesh->normals[non_edge_idxs[i]][1], mesh->normals[non_edge_idxs[i]][2]);
		}
	}
}

static GTriIdx *getTri(GMesh *mesh, int idx0, int idx1, int *tri_class)
{
	int i;
	for (i = 0; i < mesh->tri_count; ++i)
	{
		if (*tri_class = gmeshTrisMatch(&mesh->tris[i], idx0, idx1))
			return &mesh->tris[i];
	}
	return NULL;
}

static int splitTri(GMesh *dst, const GMesh *src, 
					int dst_pos_idx, int dst_prev_pos_idx, 
					int src_pos_idx, 
					const char **dst_materials, const char **src_materials, 
					const char **common_materials, 
					int test_subscript, int compare_subscript, FILE *terrain_seam_log, int srcidx)
{
	F32 interp_value, avg_height;
	Vec3 position, normal, tangent, binormal;
	Vec2 tex1, tex2;
	GTriIdx *tri;
	int tri_class, common_msize = eaSize(&common_materials), dst_msize = eaSize(&dst_materials);
	U8 *varcolor_common = _alloca(common_msize);
	U8 *dstcolor = NULL;

	if (dst_prev_pos_idx < 0)
		return dst_prev_pos_idx;

	interp_value = (src->positions[src_pos_idx][compare_subscript] - dst->positions[dst_prev_pos_idx][compare_subscript]) / (dst->positions[dst_pos_idx][compare_subscript] - dst->positions[dst_prev_pos_idx][compare_subscript]);

	if (interp_value == 0 || interp_value == 1)
		return dst_prev_pos_idx;

	varcolor_common = _alloca(common_msize * sizeof(U8));
	ZeroStructsForce(varcolor_common, common_msize);

	// average heights with neighbor
	avg_height = 0.5f * (dst->positions[dst_pos_idx][1] * interp_value + dst->positions[dst_prev_pos_idx][1] * (1-interp_value) + src->positions[src_pos_idx][1]);
	src->positions[src_pos_idx][1] = avg_height;

	copyVec3(src->positions[src_pos_idx], position);
	position[test_subscript] = dst->positions[dst_pos_idx][test_subscript];

	if (dst->usagebits & USE_NORMALS)
	{
		// average normals with neighbor
		lerpVec3(dst->normals[dst_pos_idx], interp_value, dst->normals[dst_prev_pos_idx], normal);
		addVec3(normal, src->normals[src_pos_idx], normal);
		SEAM_LOG("normal lerp[(%f, %f, %f), (%f, %f, %f), %f] + (%f, %f, %f) -> (%f, %f, %f)", dst->normals[dst_pos_idx][0], dst->normals[dst_pos_idx][1], dst->normals[dst_pos_idx][2], dst->normals[dst_prev_pos_idx][0], dst->normals[dst_prev_pos_idx][1], dst->normals[dst_prev_pos_idx][2], interp_value, src->normals[src_pos_idx][0], src->normals[src_pos_idx][1], src->normals[src_pos_idx][2], normal[0], normal[1], normal[2]);
		normalVec3(normal);
		SEAM_LOG("%d_%d normal -> %f, %f, %f", srcidx, src_pos_idx, normal[0], normal[1], normal[2]);
		copyVec3(normal, src->normals[src_pos_idx]);
	}
	if (dst->usagebits & USE_VARCOLORS)
	{
		U8 *dstcolor_common = _alloca(common_msize * sizeof(U8));
		U8 *srccolor_common = _alloca(common_msize * sizeof(U8));

		dstcolor = _alloca(dst_msize * sizeof(U8));
		VarColorLerp(&dst->varcolors[dst_pos_idx*dst->varcolor_size], interp_value, &dst->varcolors[dst_prev_pos_idx*dst->varcolor_size], dstcolor, dst_msize);
		transformVarColor(dstcolor_common, common_materials, dstcolor, dst_materials);

		transformVarColor(srccolor_common, common_materials, &src->varcolors[src_pos_idx*src->varcolor_size], src_materials);

		VarColorAverage(srccolor_common, dstcolor_common, varcolor_common, common_msize);

		if (!VarColorIsZero(varcolor_common, common_msize))
			transformVarColor(&src->varcolors[src_pos_idx*src->varcolor_size], src_materials, varcolor_common, common_materials);
	}
	if (dst->usagebits & USE_TANGENTS)
		lerpVec3(dst->tangents[dst_pos_idx], interp_value, dst->tangents[dst_prev_pos_idx], tangent);
	if (dst->usagebits & USE_BINORMALS)
		lerpVec3(dst->binormals[dst_pos_idx], interp_value, dst->binormals[dst_prev_pos_idx], binormal);
	if (dst->usagebits & USE_TEX1S)
		lerpVec2(dst->tex1s[dst_pos_idx], interp_value, dst->tex1s[dst_prev_pos_idx], tex1);
	if (dst->usagebits & USE_TEX2S)
		lerpVec2(dst->tex2s[dst_pos_idx], interp_value, dst->tex2s[dst_prev_pos_idx], tex2);

	tri = getTri(dst, dst_prev_pos_idx, dst_pos_idx, &tri_class);
	if (tri)
	{
		int new_idx;

		if (dst->usagebits & USE_VARCOLORS)
		{
			assert(dstcolor);
			if (!VarColorIsZero(varcolor_common, common_msize))
				transformVarColor(dstcolor, dst_materials, varcolor_common, common_materials);
		}

		new_idx = gmeshAddVert(dst, position, NULL, normal, NULL, binormal, tangent, tex1, tex2, NULL, dstcolor, NULL, NULL, false, false, false);

		SEAM_LOG("%d_%d normal -> %f, %f, %f", !srcidx, new_idx, normal[0], normal[1], normal[2]);

		ANALYSIS_ASSUME(tri_class != 0 && tri_class >= -3 && tri_class <= 3);
		if (tri_class < 0)
		{
			int corner_idx = tri->idx[-tri_class - 1];
			setVec3(tri->idx, dst_pos_idx, new_idx, corner_idx);
			gmeshAddTri(dst, new_idx, dst_prev_pos_idx, corner_idx, tri->tex_id, false);
		}
		else
		{
			int corner_idx = tri->idx[tri_class - 1];
			setVec3(tri->idx, dst_prev_pos_idx, new_idx, corner_idx);
			gmeshAddTri(dst, new_idx, dst_pos_idx, corner_idx, tri->tex_id, false);
		}

		return new_idx;
	}

	return dst_prev_pos_idx;
}

static void matchEdges(HeightMapBinAtlas *atlas, HeightMapBinAtlas *neighbor_atlas, int test_subscript, int compare_subscript, F32 width, 
					   F32 height0, F32 height1,							// corner average heights
					   const Vec3 normal0, const Vec3 normal1,				// corner average normals
					   const U8 *color0, const U8 *color1,					// corner average varcolors (same array size as corner materials)
					   const char **materials0, const char **materials1,	// corner common materials
					   PerThreadAtlasParams *thread_params)
{
	int *pos_idxs, *non_edge_idxs, *neighbor_pos_idxs, *neighbor_non_edge_idxs;
	int i = 0, j = 0, k, prev_idx = -1, prev_neighbor_idx = -1;
	F32 last_compare_val = -1;
	F32 last_height;
	Vec3 last_normal;
	U8 *last_color = NULL, *last_neighbor_color = NULL, *avg_color = NULL, *temp_color0 = NULL, *temp_color1 = NULL;
	GMesh *mesh = SAFE_MEMBER(atlas, mesh);
	GMesh *neighbor = SAFE_MEMBER(neighbor_atlas, mesh);
	const char **common_materials = NULL;
	bool use_colors;
	int msize, neighbor_msize, common_msize;
	FILE *terrain_seam_log = NULL;

	if (!mesh || !neighbor)
		return;

	if (terrain_seam_logging)
	{
		char path[MAX_PATH];
		sprintf(path, "%s/seam_%d_%d_%d_%d.log", fileTempDir(), atlas->lod, atlas->local_pos[0], atlas->local_pos[1], test_subscript);
		terrain_seam_log = fopen(path, (terrain_seam_logging==2)?"rt":"wt");
	}

	SET_FP_CONTROL_WORD_DEFAULT;

	use_colors = mesh->varcolors && (mesh->usagebits & USE_VARCOLORS) && neighbor->varcolors && (neighbor->usagebits & USE_VARCOLORS);

	if (use_colors)
	{
		common_materials = getCommonMaterials(atlas->detail_material_names, neighbor_atlas->detail_material_names);
		msize = eaSize(&atlas->detail_material_names);
		neighbor_msize = eaSize(&neighbor_atlas->detail_material_names);
		common_msize = eaSize(&common_materials);
		last_color = _alloca(msize);
		last_neighbor_color = _alloca(neighbor_msize);
		avg_color = _alloca(common_msize);
		temp_color0 = _alloca(common_msize);
		temp_color1 = _alloca(common_msize);
		assert(msize <= mesh->varcolor_size);
		assert(neighbor_msize <= neighbor->varcolor_size);
	}

	timerStart(thread_params->timer_id);

	getSortedBorderPositions(mesh, test_subscript, width, compare_subscript, width, 
								&pos_idxs, &non_edge_idxs, 
								height0, height1, 
								normal0, normal1, 
								color0, color1, 
								materials0, materials1, 
								avg_color, common_materials, atlas->detail_material_names, 
								terrain_seam_log, 0);

	getSortedBorderPositions(neighbor, test_subscript, 0, compare_subscript, width, 
								&neighbor_pos_idxs, &neighbor_non_edge_idxs, 
								height0, height1, 
								normal0, normal1, 
								color0, color1, 
								materials0, materials1, 
								avg_color, common_materials, neighbor_atlas->detail_material_names, 
								terrain_seam_log, 1);

	while (i < eaiSize(&pos_idxs) && j < eaiSize(&neighbor_pos_idxs))
	{
		int pos_idx = pos_idxs[i];
		int neighbor_pos_idx = neighbor_pos_idxs[j];

		if (mesh->positions[pos_idx][compare_subscript] == last_compare_val)
		{
			mesh->positions[pos_idx][1] = last_height;
			if ((mesh->usagebits & USE_NORMALS) && neighbor->positions[neighbor_pos_idx][compare_subscript] != last_compare_val)
			{
				copyVec3(last_normal, mesh->normals[pos_idx]); // don't copy last normal if both meshes have a normal seam
				SEAM_LOG("0_%d normal -> %f, %f, %f", pos_idx, last_normal[0], last_normal[1], last_normal[2]);
			}
			if (use_colors)
				CopyStructs(&mesh->varcolors[pos_idx*msize], last_color, msize);
		}

		for (k = 0; k < eaiSize(&non_edge_idxs); ++k)
		{
			if (mesh->positions[non_edge_idxs[k]][compare_subscript] == last_compare_val)
			{
				mesh->positions[non_edge_idxs[k]][1] = last_height;
				if ((mesh->usagebits & USE_NORMALS) && neighbor->positions[neighbor_pos_idx][compare_subscript] != last_compare_val)
				{
					copyVec3(last_normal, mesh->normals[non_edge_idxs[k]]); // don't copy last normal if both meshes have a normal seam
					SEAM_LOG("0_%d normal -> %f, %f, %f", non_edge_idxs[k], last_normal[0], last_normal[1], last_normal[2]);
				}
				if (use_colors)
					CopyStructs(&mesh->varcolors[non_edge_idxs[k]*msize], last_color, msize);
			}
		}

		if (neighbor->positions[neighbor_pos_idx][compare_subscript] == last_compare_val)
		{
			neighbor->positions[neighbor_pos_idx][1] = last_height;
			if ((neighbor->usagebits & USE_NORMALS) && mesh->positions[pos_idx][compare_subscript] != last_compare_val)
			{
				copyVec3(last_normal, neighbor->normals[neighbor_pos_idx]); // don't copy last normal if both meshes have a normal seam
				SEAM_LOG("1_%d normal -> %f, %f, %f", neighbor_pos_idx, last_normal[0], last_normal[1], last_normal[2]);
			}
			if (use_colors)
				CopyStructs(&neighbor->varcolors[neighbor_pos_idx*neighbor_msize], last_neighbor_color, neighbor_msize);
		}

		for (k = 0; k < eaiSize(&neighbor_non_edge_idxs); ++k)
		{
			if (neighbor->positions[neighbor_non_edge_idxs[k]][compare_subscript] == last_compare_val)
			{
				neighbor->positions[neighbor_non_edge_idxs[k]][1] = last_height;
				if ((neighbor->usagebits & USE_NORMALS) && mesh->positions[pos_idx][compare_subscript] != last_compare_val)
				{
					copyVec3(last_normal, neighbor->normals[neighbor_non_edge_idxs[k]]); // don't copy last normal if both meshes have a normal seam
					SEAM_LOG("1_%d normal -> %f, %f, %f", neighbor_non_edge_idxs[k], last_normal[0], last_normal[1], last_normal[2]);
				}
				if (use_colors)
					CopyStructs(&neighbor->varcolors[neighbor_non_edge_idxs[k]*neighbor_msize], last_neighbor_color, neighbor_msize);
			}
		}

		if (mesh->positions[pos_idx][compare_subscript] < neighbor->positions[neighbor_pos_idx][compare_subscript])
		{
			// split neighbor triangle
			prev_neighbor_idx = splitTri(	neighbor, mesh, 
											neighbor_pos_idx, prev_neighbor_idx, 
											pos_idx, 
											neighbor_atlas->detail_material_names, atlas->detail_material_names, 
											common_materials, 
											test_subscript, compare_subscript, terrain_seam_log, 0);

			last_height = mesh->positions[pos_idx][1];
			if (mesh->usagebits & USE_NORMALS)
				copyVec3(mesh->normals[pos_idx], last_normal);
			if (use_colors)
			{
				CopyStructs(last_color, &mesh->varcolors[pos_idx*msize], msize);
				CopyStructs(last_neighbor_color, &neighbor->varcolors[prev_neighbor_idx*neighbor_msize], neighbor_msize);
			}

			last_compare_val = mesh->positions[pos_idx][compare_subscript];
			prev_idx = pos_idx;
			++i;
		}
		else if (mesh->positions[pos_idx][compare_subscript] > neighbor->positions[neighbor_pos_idx][compare_subscript])
		{
			// split mesh triangle
			prev_idx = splitTri(mesh, neighbor, 
								pos_idx, prev_idx, 
								neighbor_pos_idx, 
								atlas->detail_material_names, neighbor_atlas->detail_material_names, 
								common_materials, 
								test_subscript, compare_subscript, terrain_seam_log, 1);

			last_height = neighbor->positions[neighbor_pos_idx][1];
			if (neighbor->usagebits & USE_NORMALS)
				copyVec3(neighbor->normals[neighbor_pos_idx], last_normal);
			if (use_colors)
			{
				CopyStructs(last_color, &mesh->varcolors[prev_idx*msize], msize);
				CopyStructs(last_neighbor_color, &neighbor->varcolors[neighbor_pos_idx*neighbor_msize], neighbor_msize);
			}

			last_compare_val = neighbor->positions[neighbor_pos_idx][compare_subscript];
			prev_neighbor_idx = neighbor_pos_idx;
			++j;
		}
		else
		{
			// verts match, average heights with neighbor
			F32 avg_height;
			Vec3 avg_normal;

			if (mesh->positions[pos_idx][compare_subscript] == 0)
			{
				avg_height = height0;
				copyVec3(normal0, avg_normal);
				transformVarColor(avg_color, common_materials, color0, materials0);
			}
			else if (mesh->positions[pos_idx][compare_subscript] == width)
			{
				avg_height = height1;
				copyVec3(normal1, avg_normal);
				transformVarColor(avg_color, common_materials, color1, materials1);
			}
			else
			{
				avg_height = 0.5f * (mesh->positions[pos_idx][1] + neighbor->positions[neighbor_pos_idx][1]);
				if (mesh->usagebits & USE_NORMALS)
				{
					// average normals with neighbor
					addVec3(mesh->normals[pos_idx], neighbor->normals[neighbor_pos_idx], avg_normal);
					normalVec3(avg_normal);
				}
				if (use_colors)
				{
					transformVarColor(temp_color0, common_materials, &mesh->varcolors[pos_idx*msize], atlas->detail_material_names);
					transformVarColor(temp_color1, common_materials, &neighbor->varcolors[neighbor_pos_idx*neighbor_msize], neighbor_atlas->detail_material_names);
					VarColorAverage(temp_color0, temp_color1, avg_color, common_msize);
				}
			}

			mesh->positions[pos_idx][1] = neighbor->positions[neighbor_pos_idx][1] = avg_height;
			last_height = avg_height;

			if (mesh->usagebits & USE_NORMALS)
			{
				copyVec3(avg_normal, mesh->normals[pos_idx]);
				copyVec3(avg_normal, neighbor->normals[neighbor_pos_idx]);
				copyVec3(avg_normal, last_normal);
				SEAM_LOG("0_%d normal -> %f, %f, %f", pos_idx, last_normal[0], last_normal[1], last_normal[2]);
				SEAM_LOG("1_%d normal -> %f, %f, %f", neighbor_pos_idx, last_normal[0], last_normal[1], last_normal[2]);
			}

			if (use_colors)
			{
				if (!VarColorIsZero(avg_color, common_msize))
				{
					transformVarColor(&mesh->varcolors[pos_idx*msize], atlas->detail_material_names, avg_color, common_materials);
					transformVarColor(&neighbor->varcolors[neighbor_pos_idx*neighbor_msize], neighbor_atlas->detail_material_names, avg_color, common_materials);
				}
				CopyStructs(last_color, &mesh->varcolors[pos_idx*msize], msize);
				CopyStructs(last_neighbor_color, &neighbor->varcolors[neighbor_pos_idx*neighbor_msize], neighbor_msize);
			}

			last_compare_val = mesh->positions[pos_idx][compare_subscript];

			prev_idx = pos_idx;
			prev_neighbor_idx = neighbor_pos_idx;
			++i;
			++j;
		}
	}

	eaiDestroy(&pos_idxs);
	eaiDestroy(&neighbor_pos_idxs);
	eaiDestroy(&non_edge_idxs);
	eaiDestroy(&neighbor_non_edge_idxs);
	eaDestroy(&common_materials);

	thread_params->timings[TAT_SEAMING] += timerElapsedAndStart(thread_params->timer_id);

	if (terrain_seam_log)
		fclose(terrain_seam_log);
}

F32 terrainGetDesiredMeshError(int lod)
{
	//if you change this then update collapseEdge
	switch (lod)
	{
		xcase 0:
			return 0.475f;
		xcase 1:
			return 0.6f;
		xcase 2:
			return 0.651f;
	}
	return 0.7f;
}

int terrainGetDesiredMeshTriCount(int lod, int tile_count)
{
	int i, tri_count;
	if(lod==0 && !gConf.bPreventTerrainMeshImprovements)
		tri_count = round(terrain_high_lod_tri_percent * 2 * tile_count * SQR(HEIGHTMAP_SIZE(0))); // 33 percent of the original triangle count by default
	else 
		tri_count = round(terrain_lod_initial_tri_percent * 2 * tile_count * SQR(HEIGHTMAP_SIZE(0))); // 25 percent of the original triangle count by default
	for (i = 1; i <= lod; ++i)
		tri_count >>= 2; // divide by 4
	return tri_count;
}

static void mergeAndReduceAtlas(HeightMapBinAtlas *atlas, PerThreadAtlasParams *thread_params)
{
	int i, j, px, py;

	EnterCriticalSection(&terrain_atlas_bins_cs);

	for (px = 0; px < 2; ++px)
	{
		for (py = 0; py < 2; ++py)
		{
			if (atlas->children[px+py*2] &&
				!(atlas->children[px+py*2]->bottom_seamed && atlas->children[px+py*2]->top_seamed && 
				  atlas->children[px+py*2]->left_seamed && atlas->children[px+py*2]->right_seamed))
			{
				// can only merge if all children are fully seamed
				LeaveCriticalSection(&terrain_atlas_bins_cs);
				return;
			}
		}
	}

	if (!atlas->merged && !atlas->merging)
	{
		atlas->merging = true;
		if (terrain_atlas_log)
			fprintf(terrain_atlas_log, "%d_%d_%d : Started merging children and reducing mesh.\n", atlas->lod, atlas->local_pos[0], atlas->local_pos[1]);
		LeaveCriticalSection(&terrain_atlas_bins_cs);

		// merge geo and reduce (not in critical section)
		for (px = 0; px < 2; ++px)
		{
			for (py = 0; py < 2; ++py)
			{
				mergeHeightMapBinAtlas(atlas, atlas->children[px+py*2], thread_params);
			}
		}

		if (atlas->mesh)
		{
			F32 terrain_min_weight = TERRAIN_MIN_WEIGHT * (atlas->lod+1);
			char path[MAX_PATH];

			timerStart(thread_params->timer_id);

			if (atlas->lod >= 5)
			{
				// remove texcoords and pool verts
				eaDestroy(&atlas->detail_material_names);
				gmeshSetUsageBits(atlas->mesh, USE_POSITIONS | USE_NORMALS);
				gmeshPool(atlas->mesh, false, false, false);
			}

			if (sameVec2(atlas->local_pos, save_debug_terrain_atlas_mesh) && save_debug_terrain_atlas_mesh[2] == atlas->lod)
			{
				SimpleBufHandle buf;
				sprintf(path, "%s/testmesh.msh", fileTempDir());
				buf = SimpleBufOpenWrite(path, true, NULL, false, false);
				gmeshWriteBinData(atlas->mesh, buf);
				SimpleBufClose(buf);
			}

			if (atlas->mesh->varcolors)
			{
				for (i = 0; i < atlas->mesh->vert_count; ++i)
				{
					bool changed = false;
					for (j = 0; j < atlas->mesh->varcolor_size; ++j)
					{
						if (atlas->mesh->varcolors[i*atlas->mesh->varcolor_size+j] <= terrain_min_weight)
						{
							atlas->mesh->varcolors[i*atlas->mesh->varcolor_size+j] = 0;
							changed = true;
						}
					}
					if (changed)
						VarColorNormalize(&atlas->mesh->varcolors[i*atlas->mesh->varcolor_size], atlas->mesh->varcolor_size);
				}
			}

			gmeshSetUsageBits(atlas->mesh, atlas->mesh->usagebits & (~USE_TEX1S));

			sprintf(path, "%d_%d_%d", atlas->lod, atlas->local_pos[0], atlas->local_pos[1]);
			if(atlas->mesh->vert_count) {
				gmeshReduceDebug(
					atlas->mesh, atlas->mesh,
					terrainGetDesiredMeshError(atlas->lod),
					terrainGetDesiredMeshTriCount(atlas->lod, atlas->tile_count),
					TRICOUNT_AND_ERROR_RMETHOD,
					g_TerrainScaleByArea, 0,
					g_TerrainUseOptimalVertPlacement,
					false, true, false, path,
					atlas->layer_filename);
			}

			if (atlas->mesh->varcolors)
			{
				atlas->corner_colors[0] = calloc(atlas->mesh->varcolor_size, sizeof(U8));
				atlas->corner_colors[1] = calloc(atlas->mesh->varcolor_size, sizeof(U8));
				atlas->corner_colors[2] = calloc(atlas->mesh->varcolor_size, sizeof(U8));
				atlas->corner_colors[3] = calloc(atlas->mesh->varcolor_size, sizeof(U8));

				for (i = 0; i < atlas->mesh->vert_count; ++i)
				{
					for (j = 0; j < atlas->mesh->varcolor_size; ++j)
					{
						if (atlas->mesh->varcolors[i*atlas->mesh->varcolor_size+j] <= terrain_min_weight)
							atlas->mesh->varcolors[i*atlas->mesh->varcolor_size+j] = 0;
					}
				}
			}

			for (i = 0; i < atlas->mesh->vert_count; ++i)
			{
				if (atlas->mesh->positions[i][0] == 0)
					px = 0;
				else if (atlas->mesh->positions[i][0] == heightmap_atlas_size[atlas->lod] * GRID_BLOCK_SIZE)
					px = 1;
				else
					continue;
				if (atlas->mesh->positions[i][2] == 0)
					py = 0;
				else if (atlas->mesh->positions[i][2] == heightmap_atlas_size[atlas->lod] * GRID_BLOCK_SIZE)
					py = 1;
				else
					continue;
				atlas->corner_heights[px+py*2] = atlas->corner_avg_heights[px+py*2] = atlas->mesh->positions[i][1];
				if (atlas->mesh->normals)
				{
					copyVec3(atlas->mesh->normals[i], atlas->corner_normals[px+py*2]);
					copyVec3(atlas->mesh->normals[i], atlas->corner_avg_normals[px+py*2]);
				}
				if (atlas->mesh->varcolors)
				{
					CopyStructs(atlas->corner_colors[px+py*2], &atlas->mesh->varcolors[i*atlas->mesh->varcolor_size], atlas->mesh->varcolor_size);
				}
			}

			thread_params->timings[TAT_REDUCING] += timerElapsedAndStart(thread_params->timer_id);
		}
		EnterCriticalSection(&terrain_atlas_bins_cs);
		atlas->merged = true;
		atlas->merging = false;
		if (terrain_atlas_log)
			fprintf(terrain_atlas_log, "%d_%d_%d : Finished merging children and reducing mesh.\n", atlas->lod, atlas->local_pos[0], atlas->local_pos[1]);
	}
	LeaveCriticalSection(&terrain_atlas_bins_cs);
}

static void postSeaming(HeightMapBinAtlas *atlas, PerThreadAtlasParams *thread_params)
{
	HeightMapBinAtlas *parent = atlas->parent;
	int px, py;

	assert(atlas->bottom_seamed && atlas->top_seamed && atlas->left_seamed && atlas->right_seamed);

	// try to merge and reduce parent atlas
	if (parent && !parent->merging && !parent->merged)
		mergeAndReduceAtlas(parent, thread_params);

	// write children
	EnterCriticalSection(&terrain_atlas_bins_cs);
	for (px = 0; px < 2; ++px)
	{
		for (py = 0; py < 2; ++py)
		{
			if (atlas->children[px+py*2] && !atlas->children[px+py*2]->written && !atlas->children[px+py*2]->writing)
			{
				SimpleBuffer *output1 = NULL, *output2 = NULL, *output3 = NULL;
				HeightMapBinAtlas *child = atlas->children[px+py*2];
				child->writing = true;
				if (terrain_atlas_log)
					fprintf(terrain_atlas_log, "%d_%d_%d : Started writing bins.\n", child->lod, child->local_pos[0], child->local_pos[1]);

				assert(child->merged);

				LeaveCriticalSection(&terrain_atlas_bins_cs);

				// write out bins (not in critical section)
				writeAtlasBin(child, &output1, &output2, &output3, thread_params);

				EnterCriticalSection(&terrain_atlas_bins_cs);
				if (output1)
					eaPush(&g_AtlasThreadProcInfo.atlas_output_list, output1);
				if (output2)
					eaPush(&g_AtlasThreadProcInfo.atlas_output_list, output2);
				if (output3)
					eaPush(&g_AtlasThreadProcInfo.atlas_output_list, output3);

				// free data
				eaFindAndRemoveFast(&g_AtlasThreadProcInfo.atlas_list, child);
				atlasBinFreeData(child);

				child->written = true;
				child->writing = false;
				if (terrain_atlas_log)
					fprintf(terrain_atlas_log, "%d_%d_%d : Finished writing bins.\n", child->lod, child->local_pos[0], child->local_pos[1]);
			}
		}
	}
	LeaveCriticalSection(&terrain_atlas_bins_cs);
}

static void atlasSeamWithNeighbors(HeightMapBinAtlas *atlas, HeightMapBinAtlas *bottom_neighbor, HeightMapBinAtlas *top_neighbor, HeightMapBinAtlas *left_neighbor, HeightMapBinAtlas *right_neighbor, PerThreadAtlasParams *thread_params)
{
	int atlas_size;

	if (!atlas || atlas->left_seamed && atlas->right_seamed && atlas->top_seamed && atlas->bottom_seamed)
		return;

	atlas_size = heightmapAtlasGetLODSize(atlas->lod);

	assert(atlas->corner_averaged[1] && atlas->corner_averaged[2] && atlas->corner_averaged[3]);
	assert(atlas->merged);

	if (!left_neighbor || (left_neighbor->merged && !left_neighbor->mesh))
		atlas->left_seamed = true;
	if (!bottom_neighbor || (bottom_neighbor->merged && !bottom_neighbor->mesh))
		atlas->bottom_seamed = true;
	if (!right_neighbor || (right_neighbor->merged && !right_neighbor->mesh))
		atlas->right_seamed = true;
	if (!top_neighbor || (top_neighbor->merged && !top_neighbor->mesh))
		atlas->top_seamed = true;
	if (!atlas->mesh)
	{
		atlas->left_seamed = true;
		atlas->bottom_seamed = true;
		atlas->right_seamed = true;
		atlas->top_seamed = true;
	}

	if (!atlas->right_seamed)
	{
		assert(right_neighbor->corner_averaged[0] && right_neighbor->corner_averaged[2]);
		assert(!right_neighbor->left_seamed);
		assert(right_neighbor->merged);

		if (atlas->mesh)
		{
			matchEdges(	atlas, right_neighbor, 0, 2, atlas_size * GRID_BLOCK_SIZE, 
						atlas->corner_avg_heights[1], atlas->corner_avg_heights[3], 
						atlas->corner_avg_normals[1], atlas->corner_avg_normals[3], 
						atlas->corner_avg_colors[1], atlas->corner_avg_colors[3], 
						atlas->corner_detail_material_names[1], atlas->corner_detail_material_names[3], 
						thread_params);
			gmeshSort(atlas->mesh); // mesh needs to be sorted before merging so that the vert pooling gets the same value
			gmeshSort(right_neighbor->mesh); // mesh needs to be sorted before merging so that the vert pooling gets the same value
		}
		atlas->right_seamed = true;
		right_neighbor->left_seamed = true;
	}

	if (!atlas->top_seamed)
	{
		assert(top_neighbor->corner_averaged[0] && top_neighbor->corner_averaged[1]);
		assert(!top_neighbor->bottom_seamed);
		assert(top_neighbor->merged);

		if (atlas->mesh)
		{
			matchEdges(	atlas, top_neighbor, 2, 0, atlas_size * GRID_BLOCK_SIZE, 
						atlas->corner_avg_heights[2], atlas->corner_avg_heights[3], 
						atlas->corner_avg_normals[2], atlas->corner_avg_normals[3], 
						atlas->corner_avg_colors[2], atlas->corner_avg_colors[3], 
						atlas->corner_detail_material_names[2], atlas->corner_detail_material_names[3], 
						thread_params);
			gmeshSort(atlas->mesh); // mesh needs to be sorted before merging so that the vert pooling gets the same value
			gmeshSort(top_neighbor->mesh); // mesh needs to be sorted before merging so that the vert pooling gets the same value
		}
		atlas->top_seamed = true;
		top_neighbor->bottom_seamed = true;
	}
}

static void atlasComputeCornerAverages(const IVec2 corner, int lod, PerThreadAtlasParams *thread_params)
{
	HeightMapBinAtlas *atlases[4], *primary_atlas = NULL;
	int px, py, atlas_size = heightmapAtlasGetLODSize(lod), msize;
	IVec2 p;
	Vec3 normal = {0};
	F32 height = 0, scale;
	int count = 0, primary_idx;
	const char **detail_material_names = NULL;
	U8 *color, *temp_color;
	int averaged_count = 0;

	for (px = 0; px < 2; ++px)
	{
		for (py = 0; py < 2; ++py)
		{
			setVec2(p, corner[0] + (px-1) * atlas_size, corner[1] + (py-1) * atlas_size);
			atlases[px+py*2] = atlasFindForBinning(&g_AtlasThreadProcInfo.atlas_list, p, lod, thread_params);
			if (atlases[px+py*2])
			{
				assert(atlases[px+py*2]->merged);
				if (atlases[px+py*2]->mesh)
				{
					count++;
					averaged_count += atlases[px+py*2]->corner_averaged[(1-px)+(1-py)*2];
					if (!primary_atlas)
					{
						primary_atlas = atlases[px+py*2];
						primary_idx = px+py*2;
						eaCopy(&detail_material_names, &atlases[px+py*2]->detail_material_names);
					}
					else
					{
						const char **common_detail_materials = getCommonMaterials(detail_material_names, atlases[px+py*2]->detail_material_names);
						eaDestroy(&detail_material_names);
						detail_material_names = common_detail_materials;
					}
				}
			}
		}
	}

	if (averaged_count && averaged_count == count)
	{
		eaDestroy(&detail_material_names);
		return;
	}

	assert(!averaged_count);

	scale = count ? (1.f / count) : 1;

	msize = eaSize(&detail_material_names);
	temp_color = _alloca(msize * sizeof(U8));
	color = _alloca(msize * sizeof(U8));
	ZeroStructsForce(color, msize);

	for (px = 0; px < 2; ++px)
	{
		for (py = 0; py < 2; ++py)
		{
			if (atlases[px+py*2] && atlases[px+py*2]->mesh)
			{
				int corner_idx = (1-px)+(1-py)*2;

				height += atlases[px+py*2]->corner_heights[corner_idx];
				addVec3(atlases[px+py*2]->corner_normals[corner_idx], normal, normal);
				if (atlases[px+py*2]->corner_colors[corner_idx])
				{
					transformVarColor(temp_color, detail_material_names, atlases[px+py*2]->corner_colors[corner_idx], atlases[px+py*2]->detail_material_names);
				}
				else
				{
					ZeroStructsForce(temp_color, msize);
				}
				VarColorScaleAdd(temp_color, scale, color, color, msize);
			}
		}
	}

	height *= scale;
	normalVec3(normal);
	VarColorNormalize(color, msize);

	for (px = 0; px < 2; ++px)
	{
		for (py = 0; py < 2; ++py)
		{
			int corner_idx = (1-px)+(1-py)*2;
			if (atlases[px+py*2] && !atlases[px+py*2]->corner_averaged[corner_idx])
			{
				atlases[px+py*2]->corner_avg_heights[corner_idx] = height;
				copyVec3(normal, atlases[px+py*2]->corner_avg_normals[corner_idx]);

				assert(!atlases[px+py*2]->corner_avg_colors[corner_idx]);
				atlases[px+py*2]->corner_avg_colors[corner_idx] = malloc(msize * sizeof(U8));
				CopyStructs(atlases[px+py*2]->corner_avg_colors[corner_idx], color, msize);

				assert(!atlases[px+py*2]->corner_detail_material_names[corner_idx]);
				eaCopy(&atlases[px+py*2]->corner_detail_material_names[corner_idx], &detail_material_names);

				atlases[px+py*2]->corner_averaged[corner_idx] = true;
			}
		}
	}

	eaDestroy(&detail_material_names);
}

// this function is NOT thread safe
static void doChildSeamingNonThread(PerThreadAtlasParams *thread_params)
{
	int lod = g_AtlasThreadProcInfo.lod, atlas_size, child_atlas_size;
	IVec2 p, p2;

	atlas_size = heightmapAtlasGetLODSize(lod);
	child_atlas_size = atlas_size >> 1;

	for (p[1] = thread_params->min[1]; p[1] < thread_params->max[1]; p[1] += atlas_size)
	{
		for (p[0] = thread_params->min[0]; p[0] < thread_params->max[0]; p[0] += atlas_size)
		{
			HeightMapBinAtlas *atlas;
			int px, py;

			EnterCriticalSection(&terrain_atlas_bins_cs);
			atlas = atlasFindForBinning(&g_AtlasThreadProcInfo.atlas_list, p, lod, thread_params);
			assert(atlas->lod == lod);

			if (lod == HEIGHTMAP_ATLAS_MIN_LOD)
			{
				// load children
				for (px = 0; px < 2; ++px)
				{
					for (py = 0; py < 2; ++py)
					{
						setVec2(p2, p[0] + px * child_atlas_size, p[1] + py * child_atlas_size);
						atlas->children[px+py*2] = atlasFindForBinning(&g_AtlasThreadProcInfo.atlas_list, p2, lod - 1, thread_params);
					}
				}
			}

			// compute child corner averages
			for (px = 0; px < 3; ++px)
			{
				for (py = 0; py < 3; ++py)
				{
					setVec2(p2, p[0] + px * child_atlas_size, p[1] + py * child_atlas_size);
					atlasComputeCornerAverages(p2, lod - 1, thread_params);
				}
			}

			LeaveCriticalSection(&terrain_atlas_bins_cs);

			// seam children
			for (py = 0; py < 2; ++py)
			{
				for (px = 0; px < 2; ++px)
				{
					HeightMapBinAtlas *left_neighbor, *right_neighbor, *bottom_neighbor, *top_neighbor;

					if (!atlas->children[px+py*2])
						continue;

					EnterCriticalSection(&terrain_atlas_bins_cs);

					if (px)
					{
						setVec2(p2, atlas->children[px+py*2]->local_pos[0] + child_atlas_size, atlas->children[px+py*2]->local_pos[1]);
						right_neighbor = atlasFindForBinning(&g_AtlasThreadProcInfo.atlas_list, p2, atlas->children[px+py*2]->lod, thread_params);
						left_neighbor = atlas->children[(px-1)+py*2];
					}
					else
					{
						setVec2(p2, atlas->children[px+py*2]->local_pos[0] - child_atlas_size, atlas->children[px+py*2]->local_pos[1]);
						left_neighbor = atlasFindForBinning(&g_AtlasThreadProcInfo.atlas_list, p2, atlas->children[px+py*2]->lod, thread_params);
						right_neighbor = atlas->children[(px+1)+py*2];
					}

					if (py)
					{
						setVec2(p2, atlas->children[px+py*2]->local_pos[0], atlas->children[px+py*2]->local_pos[1] + child_atlas_size);
						top_neighbor = atlasFindForBinning(&g_AtlasThreadProcInfo.atlas_list, p2, atlas->children[px+py*2]->lod, thread_params);
						bottom_neighbor = atlas->children[px+(py-1)*2];
					}
					else
					{
						setVec2(p2, atlas->children[px+py*2]->local_pos[0], atlas->children[px+py*2]->local_pos[1] - child_atlas_size);
						bottom_neighbor = atlasFindForBinning(&g_AtlasThreadProcInfo.atlas_list, p2, atlas->children[px+py*2]->lod, thread_params);
						top_neighbor = atlas->children[px+(py+1)*2];
					}

					LeaveCriticalSection(&terrain_atlas_bins_cs);

					atlasSeamWithNeighbors(atlas->children[px+py*2], bottom_neighbor, top_neighbor, left_neighbor, right_neighbor, thread_params);
				}
			}
		}
	}
}

static void layerSaveAtlasBinsThreadFunc1(PerThreadAtlasParams *thread_params)
{
	int lod = g_AtlasThreadProcInfo.lod, atlas_size, child_atlas_size;
	IVec2 p;

	atlas_size = heightmapAtlasGetLODSize(lod);
	child_atlas_size = atlas_size >> 1;

	for (p[1] = thread_params->min[1]; p[1] < thread_params->max[1]; p[1] += atlas_size)
	{
		for (p[0] = thread_params->min[0]; p[0] < thread_params->max[0]; p[0] += atlas_size)
		{
			HeightMapBinAtlas *atlas;
			int px, py;

			EnterCriticalSection(&terrain_atlas_bins_cs);
			atlas = atlasFindForBinning(&g_AtlasThreadProcInfo.atlas_list, p, lod, thread_params);
			assert(atlas->lod == lod);
			LeaveCriticalSection(&terrain_atlas_bins_cs);

			for (py = 0; py < 2; ++py)
			{
				for (px = 0; px < 2; ++px)
				{
					if (atlas->children[px+py*2])
						postSeaming(atlas->children[px+py*2], thread_params);
				}
			}

			layerAtlasCalcOcclusion(atlas);

			Sleep(0);
		}
	}
}

// this function is NOT thread safe
static void doSeamingNonThread(PerThreadAtlasParams *thread_params)
{
	int lod = g_AtlasThreadProcInfo.lod, atlas_size, atlas_idx = 0;
	IVec2 p, p2;

	atlas_size = heightmapAtlasGetLODSize(lod);

	assert(lod == ATLAS_MAX_LOD);

	for (p[1] = thread_params->min[1]; p[1] < thread_params->max[1]; p[1] += atlas_size)
	{
		for (p[0] = thread_params->min[0]; p[0] < thread_params->max[0]; p[0] += atlas_size)
		{
			HeightMapBinAtlas *atlas, *left_neighbor, *right_neighbor, *bottom_neighbor, *top_neighbor;
			int px, py;

			EnterCriticalSection(&terrain_atlas_bins_cs);
			atlas = atlasFindForBinning(&g_AtlasThreadProcInfo.atlas_list, p, lod, thread_params);

			// compute corner averages
			for (px = 0; px < 3; ++px)
			{
				for (py = 0; py < 3; ++py)
				{
					setVec2(p2, p[0] + px * atlas_size, p[1] + py * atlas_size);
					atlasComputeCornerAverages(p2, lod, thread_params);
				}
			}

			setVec2(p2, p[0] + atlas_size, p[1]);
			right_neighbor = atlasFindForBinning(&g_AtlasThreadProcInfo.atlas_list, p2, lod, thread_params);

			setVec2(p2, p[0] - atlas_size, p[1]);
			left_neighbor = atlasFindForBinning(&g_AtlasThreadProcInfo.atlas_list, p2, lod, thread_params);

			setVec2(p2, p[0], p[1] + atlas_size);
			top_neighbor = atlasFindForBinning(&g_AtlasThreadProcInfo.atlas_list, p2, lod, thread_params);

			setVec2(p2, p[0], p[1] - atlas_size);
			bottom_neighbor = atlasFindForBinning(&g_AtlasThreadProcInfo.atlas_list, p2, lod, thread_params);

			LeaveCriticalSection(&terrain_atlas_bins_cs);

			atlasSeamWithNeighbors(atlas, bottom_neighbor, top_neighbor, left_neighbor, right_neighbor, thread_params);

			Sleep(0);
		}
	}
}

static void layerSaveAtlasBinsThreadFunc2(PerThreadAtlasParams *thread_params)
{
	int lod = g_AtlasThreadProcInfo.lod, atlas_size, atlas_idx = 0;
	IVec2 p;

	atlas_size = heightmapAtlasGetLODSize(lod);

	assert(lod == ATLAS_MAX_LOD);

	for (p[1] = thread_params->min[1]; p[1] < thread_params->max[1]; p[1] += atlas_size)
	{
		for (p[0] = thread_params->min[0]; p[0] < thread_params->max[0]; p[0] += atlas_size)
		{
			HeightMapBinAtlas *atlas;

			EnterCriticalSection(&terrain_atlas_bins_cs);
			atlas = atlasFindForBinning(&g_AtlasThreadProcInfo.atlas_list, p, lod, thread_params);
			LeaveCriticalSection(&terrain_atlas_bins_cs);

			postSeaming(atlas, thread_params);

			EnterCriticalSection(&terrain_atlas_bins_cs);
			if (!atlas->written && !atlas->writing)
			{
				SimpleBuffer *output1, *output2, *output3;
				atlas->writing = true;
				if (terrain_atlas_log)
					fprintf(terrain_atlas_log, "%d_%d_%d : Started writing bins.\n", atlas->lod, atlas->local_pos[0], atlas->local_pos[1]);

				assert(atlas->merged);
				assert(atlas->bottom_seamed && atlas->top_seamed && atlas->left_seamed && atlas->right_seamed);

				LeaveCriticalSection(&terrain_atlas_bins_cs);

				// write out bins (not in critical section)
				writeAtlasBin(atlas, &output1, &output2, &output3, thread_params);

				EnterCriticalSection(&terrain_atlas_bins_cs);
				if (output1)
					eaPush(&g_AtlasThreadProcInfo.atlas_output_list, output1);
				if (output2)
					eaPush(&g_AtlasThreadProcInfo.atlas_output_list, output2);
				if (output3)
					eaPush(&g_AtlasThreadProcInfo.atlas_output_list, output3);

				// free data
				eaFindAndRemoveFast(&g_AtlasThreadProcInfo.atlas_list, atlas);
				atlasBinFreeData(atlas);

				atlas->written = true;
				atlas->writing = false;
				if (terrain_atlas_log)
					fprintf(terrain_atlas_log, "%d_%d_%d : Finished writing bins.\n", atlas->lod, atlas->local_pos[0], atlas->local_pos[1]);
			}
			LeaveCriticalSection(&terrain_atlas_bins_cs);

			Sleep(0);
		}
	}
}

static DWORD WINAPI layerSaveAtlasBinsThreadProc( LPVOID lpParam )
{
	PerThreadAtlasParams *params;

	EXCEPTION_HANDLER_BEGIN

		SET_FP_CONTROL_WORD_DEFAULT;

		params = (PerThreadAtlasParams *)lpParam;
		params->func(params);

		EnterCriticalSection(&terrain_atlas_bins_cs);
		g_AtlasThreadProcInfo.remaining--;
		LeaveCriticalSection(&terrain_atlas_bins_cs);

	ScratchFreeThisThreadsStack();

	EXCEPTION_HANDLER_END
	return 0;
}

static void launchThreads(PerThreadAtlasParams *thread_params, const IVec2 atlas_min, const IVec2 atlas_max, int block_increment, bool force_single_thread, void (*func)(PerThreadAtlasParams *params))
{
	SET_FP_CONTROL_WORD_DEFAULT;

	if (g_AtlasThreadProcInfo.thread_count == 1 || force_single_thread)
	{
		copyVec2(atlas_min, thread_params[0].min);
		copyVec2(atlas_max, thread_params[0].max);
		MIN1(thread_params[0].max[0], g_AtlasThreadProcInfo.max_local_pos[0]);
		MIN1(thread_params[0].max[1], g_AtlasThreadProcInfo.max_local_pos[1]);
		func(&thread_params[0]);
	}
	else
	{
		ManagedThread **threads;
		int i;

		g_AtlasThreadProcInfo.remaining = g_AtlasThreadProcInfo.thread_count;
		threads = _alloca(g_AtlasThreadProcInfo.thread_count * sizeof(*threads));

		for (i = 0; i < g_AtlasThreadProcInfo.thread_count; i++)
		{
			copyVec2(atlas_min, thread_params[i].min);
			copyVec2(atlas_max, thread_params[i].max);
			if (g_AtlasThreadProcInfo.thread_count == 2)
			{
				thread_params[i].min[1] += (!!(i & 1)) * block_increment / 2;
				thread_params[i].max[1] -= (!(i & 1))  * block_increment / 2;
			}
			else
			{
				assert(g_AtlasThreadProcInfo.thread_count == 4);
				thread_params[i].min[0] += (!!(i & 1)) * block_increment / 2;
				thread_params[i].max[0] -= (!(i & 1))  * block_increment / 2;
				thread_params[i].min[1] += (!!(i & 2)) * block_increment / 2;
				thread_params[i].max[1] -= (!(i & 2))  * block_increment / 2;
			}

			MIN1(thread_params[i].max[0], g_AtlasThreadProcInfo.max_local_pos[0]);
			MIN1(thread_params[i].max[1], g_AtlasThreadProcInfo.max_local_pos[1]);
			thread_params[i].func = func;

			threads[i] = tmCreateThread(layerSaveAtlasBinsThreadProc, &thread_params[i]);
			assert(threads[i]);
		}

		while (g_AtlasThreadProcInfo.remaining > 0)
		{
			Sleep(10);
		}

		for (i = 0; i < g_AtlasThreadProcInfo.thread_count; i++)
			tmDestroyThread(threads[i], false);
	}
}

static int cmpHeightMapAtlas(const HeightMapAtlas **atlas_ptr_a, const HeightMapAtlas **atlas_ptr_b)
{
	int t = (*atlas_ptr_a)->lod - (*atlas_ptr_b)->lod;
	if (t)
		return t;

	t = (*atlas_ptr_a)->local_pos[1] - (*atlas_ptr_b)->local_pos[1];
	if (t)
		return t;

	return (*atlas_ptr_a)->local_pos[0] - (*atlas_ptr_b)->local_pos[0];
}


// Post-processing for atlas bins
BinFileList *terrainSaveRegionAtlases(WorldRegion *region)
{
	int b, i, j, lod;
	char atlas_hogg_filename[ATLAS_MAX_LOD+1][MAX_PATH];
	char model_hogg_filename[ATLAS_MAX_LOD+1][MAX_PATH];
	char light_model_hogg_filename[ATLAS_MAX_LOD+1][MAX_PATH];
	char coll_hogg_filename[MAX_PATH];
	static bool crit_initialized = false;
	int atlas_size = heightmapAtlasGetLODSize(ATLAS_MAX_LOD);
	PerThreadAtlasParams *thread_params;
	ZoneMap *zmap = region->zmap_parent;
	IVec2 atlas_min, atlas_max;
	F32 timings[TAT_COUNT] = {0};
	F32 total_time = 0;
	bool created;
	int err_return;
	char bin_filename_write[MAX_PATH], base_dir[MAX_PATH], filename[MAX_PATH];
	BinFileList *file_list = StructCreate(parse_BinFileList);
	HeightMapAtlasRegionData atlases = {0};

	if (!zmap)
		zmap = worldGetActiveMap();

	assert(zmap);

	loadstart_printf("Processing region atlases for %s...", region->name ? region->name : "Default");
	SendStringToCB(CBSTRING_COMMENT, "Processing terrain atlases for region %s", region->name ? region->name : "Default");

	if (terrain_atlas_do_logging)
	{
		sprintf(coll_hogg_filename, "%s/%s", fileTempDir(), TERRAIN_ATLAS_LOG);
		terrain_atlas_log = fopen(coll_hogg_filename, "wt");
	}

	g_AtlasThreadProcInfo.min_local_pos[0] = 1e8;
	g_AtlasThreadProcInfo.min_local_pos[1] = 1e8;
	g_AtlasThreadProcInfo.max_local_pos[0] = -1e8;
	g_AtlasThreadProcInfo.max_local_pos[1] = -1e8;
	copyVec2(zmap->map_info.terrain_playable_min, g_AtlasThreadProcInfo.terrain_playable_min);
	copyVec2(zmap->map_info.terrain_playable_max, g_AtlasThreadProcInfo.terrain_playable_max);

	atlases.bin_version_number = TERRAIN_ATLAS_VERSION;
	g_AtlasThreadProcInfo.atlases = &atlases;
	atlases.atlas_hash = stashTableCreateFixedSize(64, sizeof(IVec2) + sizeof(U32));

	worldGetClientBaseDir(zmapGetFilename(zmap), SAFESTR(base_dir));

	for (i = 0; i < eaSize(&zmap->layers); i++)
	{
		ZoneMapLayer *layer = zmap->layers[i];
		if (layerGetWorldRegion(layer) == region &&
			eaSize(&layer->terrain.blocks) > 0)
		{
			char tempdir[MAX_PATH], dir[MAX_PATH];

			worldGetClientBaseDir(layer->filename, SAFESTR(dir));
			worldGetTempBaseDir(layer->filename, SAFESTR(tempdir));

			eaPush(&g_AtlasThreadProcInfo.layers, layer);

			eaPush(&file_list->layer_names, StructAllocString(layer->filename));
			eaPush(&file_list->layer_region_names, layer->region_name);
			bflAddSourceFile(file_list, layer->filename);

			// Make a list of the playable layers
			if (!layer->terrain.non_playable)
				eaPush(&g_AtlasThreadProcInfo.playable_layers, layer);

			for (b = 0; b < eaSize(&layer->terrain.blocks); b++)
			{
				MIN1(g_AtlasThreadProcInfo.min_local_pos[0], layer->terrain.blocks[b]->range.min_block[0]);
				MIN1(g_AtlasThreadProcInfo.min_local_pos[1], layer->terrain.blocks[b]->range.min_block[2]);
				MAX1(g_AtlasThreadProcInfo.max_local_pos[0], layer->terrain.blocks[b]->range.max_block[0]);
				MAX1(g_AtlasThreadProcInfo.max_local_pos[1], layer->terrain.blocks[b]->range.max_block[2]);

				sprintf(filename, "%s/%s_terrain.hogg", dir, layer->terrain.blocks[b]->range_name);
				if (fileExists(filename))
					bflAddSourceFile(file_list, filename);

				sprintf(filename, "%s/%s_intermediate.hogg", tempdir, layer->terrain.blocks[b]->range_name);
				if (fileExists(filename))
					bflAddSourceFile(file_list, filename);
			}
		}
	}

	g_AtlasThreadProcInfo.min_local_pos[0] = floor(((float)g_AtlasThreadProcInfo.min_local_pos[0]) / atlas_size) * atlas_size;
	g_AtlasThreadProcInfo.min_local_pos[1] = floor(((float)g_AtlasThreadProcInfo.min_local_pos[1]) / atlas_size) * atlas_size;
	g_AtlasThreadProcInfo.max_local_pos[0] = (floor(((float)g_AtlasThreadProcInfo.max_local_pos[0]) / atlas_size) + 1) * atlas_size;
	g_AtlasThreadProcInfo.max_local_pos[1] = (floor(((float)g_AtlasThreadProcInfo.max_local_pos[1]) / atlas_size) + 1) * atlas_size;

	// force background loads to finish before destroying terrain hogg files
	geoForceBackgroundLoaderToFinish();

	for (lod = 0; lod <= ATLAS_MAX_LOD; ++lod)
    {
		char new_name[MAX_PATH];
		if (region->terrain_atlas_hoggs[lod])
        {
            hogFileDestroy(region->terrain_atlas_hoggs[lod], true);
            region->terrain_atlas_hoggs[lod] = NULL;
        }
		if (region->terrain_model_hoggs[lod])
		{
			hogFileDestroy(region->terrain_model_hoggs[lod], true);
			region->terrain_model_hoggs[lod] = NULL;
		}
		if (region->terrain_light_model_hoggs[lod])
		{
			atlasTraceHoggf("Destroying hogg 0x%p \"%s\"\n", region->terrain_light_model_hoggs[lod], hogFileGetArchiveFileName(region->terrain_light_model_hoggs[lod]));
			hogFileDestroy(region->terrain_light_model_hoggs[lod], true);
			region->terrain_light_model_hoggs[lod] = NULL;
		}

		if (lod == 0)
		{
			if (region->terrain_coll_hogg)
			{
				hogFileDestroy(region->terrain_coll_hogg, true);
				region->terrain_coll_hogg = NULL;
			}
			sprintf(coll_hogg_filename, "%s/terrain_%s_collision.hogg", base_dir, region->name ? region->name : "Default");
			fileLocateWrite(coll_hogg_filename, bin_filename_write);

			strcpy(new_name, bin_filename_write);
			strcat(new_name, ".old");
			if (wl_state.delete_hoggs)
			{
				if (fileExists(bin_filename_write) && rename(bin_filename_write, new_name) != 0)
				{
					Errorf("Failed to remove bin %s!", bin_filename_write);
				}
			}


			g_AtlasThreadProcInfo.terrain_coll_hogg = hogFileReadEx(bin_filename_write, &created, PIGERR_ASSERT, &err_return, HOG_MUST_BE_WRITABLE|HOG_NO_INTERNAL_TIMESTAMPS, 1024);

			// delete contents of hogg file
			hogDeleteAllFiles(g_AtlasThreadProcInfo.terrain_coll_hogg);
		}

		// Delete terrain atlas hoggs...
		sprintf(atlas_hogg_filename[lod], "%s/terrain_%s_atlases_%d.hogg", base_dir, region->name ? region->name : "Default", lod);
        fileLocateWrite(atlas_hogg_filename[lod], bin_filename_write);
		strcpy(new_name, bin_filename_write);
		strcat(new_name, ".old");
		if (wl_state.delete_hoggs)
		{
			if (fileExists(bin_filename_write) && rename(bin_filename_write, new_name) != 0)
			{
				Errorf("Failed to remove bin %s!", bin_filename_write);
			}
		}
		g_AtlasThreadProcInfo.terrain_atlas_hoggs[lod] = hogFileReadEx(bin_filename_write, &created, PIGERR_ASSERT, &err_return, HOG_MUST_BE_WRITABLE|HOG_NO_INTERNAL_TIMESTAMPS, 1024);

		// delete contents of hogg file
		hogDeleteAllFiles(g_AtlasThreadProcInfo.terrain_atlas_hoggs[lod]);


		// Delete terrain model hoggs...
		sprintf(model_hogg_filename[lod], "%s/terrain_%s_models_%d.hogg", base_dir, region->name ? region->name : "Default", lod);
		fileLocateWrite(model_hogg_filename[lod], bin_filename_write);
		strcpy(new_name, bin_filename_write);
		strcat(new_name, ".old");
		if (wl_state.delete_hoggs)
		{
			if (fileExists(bin_filename_write) && rename(bin_filename_write, new_name) != 0)
			{
				Errorf("Failed to remove bin %s!", bin_filename_write);
			}
		}
		g_AtlasThreadProcInfo.terrain_model_hoggs[lod] = hogFileReadEx(bin_filename_write, &created, PIGERR_ASSERT, &err_return, HOG_MUST_BE_WRITABLE|HOG_NO_INTERNAL_TIMESTAMPS, 1024);

		// delete contents of hogg file
		hogDeleteAllFiles(g_AtlasThreadProcInfo.terrain_model_hoggs[lod]);


		// Delete terrain light model hoggs...
		sprintf(light_model_hogg_filename[lod], "%s/terrain_%s_light_models_%d.hogg", base_dir, region->name ? region->name : "Default", lod);
		fileLocateWrite(light_model_hogg_filename[lod], bin_filename_write);
		strcpy(new_name, bin_filename_write);
		strcat(new_name, ".old");
		if (wl_state.delete_hoggs)
		{
			if (fileExists(bin_filename_write) && rename(bin_filename_write, new_name) != 0)
			{
				Errorf("Failed to remove bin %s!", bin_filename_write);
			}
		}
		// The atlas, collision, and model hoggs get created on the
		// server. The lighting hoggs are handled by the client, so
		// don't create them here. But do clean them out if they
		// exist.
		g_AtlasThreadProcInfo.terrain_light_model_hoggs[lod] = hogFileReadEx(bin_filename_write, &created, PIGERR_ASSERT, &err_return, HOG_MUST_BE_WRITABLE|HOG_NO_INTERNAL_TIMESTAMPS|HOG_NOCREATE, 1024);

		// delete contents of hogg file
		if(g_AtlasThreadProcInfo.terrain_light_model_hoggs[lod]) {
			hogDeleteAllFiles(g_AtlasThreadProcInfo.terrain_light_model_hoggs[lod]);
		}

   }

	if (!crit_initialized)
	{
		InitializeCriticalSection(&terrain_atlas_bins_cs);
		crit_initialized = true;
	}

	systemSpecsInit();
	g_AtlasThreadProcInfo.thread_count = MAX(1, system_specs.numVirtualCPUs);
	MIN1(g_AtlasThreadProcInfo.thread_count, 4);
	if (g_AtlasThreadProcInfo.thread_count == 3)
		g_AtlasThreadProcInfo.thread_count = 2;
	if (disable_threaded_binning)
		g_AtlasThreadProcInfo.thread_count = 1;
	thread_params = _alloca(g_AtlasThreadProcInfo.thread_count * sizeof(PerThreadAtlasParams));
	for (i = 0; i < g_AtlasThreadProcInfo.thread_count; ++i)
	{
		ZeroStructForce(&thread_params[i]);
		thread_params[i].timer_id = timerAlloc();
	}

	// process LODs
	for (g_AtlasThreadProcInfo.lod = HEIGHTMAP_ATLAS_MIN_LOD; g_AtlasThreadProcInfo.lod <= ATLAS_MAX_LOD; ++g_AtlasThreadProcInfo.lod)
	{
		int block_increment;

		loadstart_printf("Processing LOD %d...", g_AtlasThreadProcInfo.lod);
		SendStringToCB(CBSTRING_COMMENT, "Processing terrain atlas LOD %d", g_AtlasThreadProcInfo.lod);

		block_increment = heightmapAtlasGetLODSize(g_AtlasThreadProcInfo.lod) * 8; // process in 8x8 atlas blocks

		for (atlas_min[1] = g_AtlasThreadProcInfo.min_local_pos[1]; atlas_min[1] < g_AtlasThreadProcInfo.max_local_pos[1]; atlas_min[1] += block_increment)
		{
			atlas_max[1] = atlas_min[1] + block_increment;

			for (atlas_min[0] = g_AtlasThreadProcInfo.min_local_pos[0]; atlas_min[0] < g_AtlasThreadProcInfo.max_local_pos[0]; atlas_min[0] += block_increment)
			{
				atlas_max[0] = atlas_min[0] + block_increment;

				launchThreads(thread_params, atlas_min, atlas_max, block_increment, true, doChildSeamingNonThread);
				launchThreads(thread_params, atlas_min, atlas_max, block_increment, false, layerSaveAtlasBinsThreadFunc1);

				eaQSortG(g_AtlasThreadProcInfo.atlas_output_list, SimpleBufFilenameComparator);
				eaClearEx(&g_AtlasThreadProcInfo.atlas_output_list, SimpleBufClose);
			}
		}

		// for lowest LOD we need another pass to seam and write bins
		if (g_AtlasThreadProcInfo.lod == ATLAS_MAX_LOD)
		{
			for (atlas_min[1] = g_AtlasThreadProcInfo.min_local_pos[1]; atlas_min[1] < g_AtlasThreadProcInfo.max_local_pos[1]; atlas_min[1] += block_increment)
			{
				atlas_max[1] = atlas_min[1] + block_increment;

				for (atlas_min[0] = g_AtlasThreadProcInfo.min_local_pos[0]; atlas_min[0] < g_AtlasThreadProcInfo.max_local_pos[0]; atlas_min[0] += block_increment)
				{
					atlas_max[0] = atlas_min[0] + block_increment;

					launchThreads(thread_params, atlas_min, atlas_max, block_increment, true, doSeamingNonThread);
					launchThreads(thread_params, atlas_min, atlas_max, block_increment, false, layerSaveAtlasBinsThreadFunc2);

					eaQSortG(g_AtlasThreadProcInfo.atlas_output_list, SimpleBufFilenameComparator);
					eaClearEx(&g_AtlasThreadProcInfo.atlas_output_list, SimpleBufClose);
				}
			}
		}

		loadend_printf(" done.");
	}

	eaDestroyEx(&g_AtlasThreadProcInfo.atlas_list, atlasBinFree);
	eaDestroy(&g_AtlasThreadProcInfo.atlas_output_list);

	for (lod = 0; lod <= ATLAS_MAX_LOD; ++lod)
	{
		if (lod == 0)
		{
			hogFileDestroy(g_AtlasThreadProcInfo.terrain_coll_hogg, true);
			bflUpdateOutputFile(coll_hogg_filename);
			bflAddOutputFile(file_list, coll_hogg_filename);
		}

		hogFileDestroy(g_AtlasThreadProcInfo.terrain_atlas_hoggs[lod], true);
		bflUpdateOutputFile(atlas_hogg_filename[lod]);
		bflAddOutputFile(file_list, atlas_hogg_filename[lod]);
		
		hogFileDestroy(g_AtlasThreadProcInfo.terrain_model_hoggs[lod], true);
		bflUpdateOutputFile(model_hogg_filename[lod]);
		bflAddOutputFile(file_list, model_hogg_filename[lod]);
	}

	stashTableDestroy(atlases.atlas_hash);
	for (i = 0; i < eaSize(&atlases.all_atlases); ++i)
	{
		if (!checkAtlasVisibility(atlases.all_atlases[i]->local_pos, atlases.all_atlases[i]->lod, &g_AtlasThreadProcInfo))
		{
			StructDestroy(parse_HeightMapAtlas, atlases.all_atlases[i]);
			eaRemoveFast(&atlases.all_atlases, i);
			--i;
		}
	}

	eaQSortG(atlases.all_atlases, cmpHeightMapAtlas);
	sprintf(filename, "%s/terrain_%s_atlases.bin", base_dir, region->name ? region->name : "Default");
	ParserWriteBinaryFile(filename, NULL, parse_HeightMapAtlasRegionData, &atlases, NULL, NULL, NULL, NULL, 0, 0, NULL, 0, 0);
	bflUpdateOutputFile(filename);
	bflAddOutputFile(file_list, filename);
	StructDeInit(parse_HeightMapAtlasRegionData, &atlases);

	eaDestroy(&g_AtlasThreadProcInfo.layers);
	eaDestroy(&g_AtlasThreadProcInfo.playable_layers);

	for (i = 0; i < g_AtlasThreadProcInfo.thread_count; ++i)
	{
		timerFree(thread_params[i].timer_id);
		for (j = 0; j < TAT_COUNT; ++j)
		{
			timings[j] += thread_params[i].timings[j];
			thread_params[i].total_time += thread_params[i].timings[j];
			total_time += thread_params[i].timings[j];
		}
	}

	printf("\nAtlas processing timing:\n");
	for (j = 0; j < TAT_COUNT; ++j)
	{
		printf(" %15s: %4.1f", StaticDefineIntRevLookup(TerrainAtlasTimingEnum, j), timings[j]);
		if (g_AtlasThreadProcInfo.thread_count > 1)
		{
			for (i = 0; i < g_AtlasThreadProcInfo.thread_count; ++i)
				printf("     Thread %d: %4.1f", i, thread_params[i].timings[j]);
		}
		printf("\n");
	}
	printf(" %15s: %4.1f", "Total", total_time);
	if (g_AtlasThreadProcInfo.thread_count > 1)
	{
		for (i = 0; i < g_AtlasThreadProcInfo.thread_count; ++i)
			printf("     Thread %d: %4.1f", i, thread_params[i].total_time);
	}
	printf("\n");


	stashTableDestroy(file_list->source_file_hash);
	stashTableDestroy(file_list->output_file_hash);
	file_list->source_file_hash = NULL;
	file_list->source_file_hash = NULL;

	sprintf(filename, "%s/terrain_%s_deps.bin", base_dir, region->name ? region->name : "Default");
	bflFixupAndWrite(file_list, filename, BFLT_TERRAIN_ATLAS);

	if (terrain_atlas_log)
	{
		fclose(terrain_atlas_log);
		terrain_atlas_log = NULL;
	}

	loadend_printf(" done.");

	return file_list;
}

AUTO_COMMAND;
void terrainAtlasTest(void)
{
	int ret;
	HeightMapAtlasRegionData data1 = {0}, data2 = {0};

	ParserOpenReadBinaryFile(NULL, "test/a1.bin", parse_HeightMapAtlasRegionData, &data1, NULL, NULL, NULL, NULL, 0, 0, BINARYREADFLAG_IGNORE_CRC);
	ParserOpenReadBinaryFile(NULL, "test/a2.bin", parse_HeightMapAtlasRegionData, &data2, NULL, NULL, NULL, NULL, 0, 0, BINARYREADFLAG_IGNORE_CRC);

	ret = StructCompare(parse_HeightMapAtlasRegionData, &data1, &data2, 0, 0, 0);

	StructReset(parse_HeightMapAtlasRegionData, &data1);
	StructReset(parse_HeightMapAtlasRegionData, &data2);

	printf("%d", ret);
}


static void atlDumpTempModelLoadHeader(const char * name, SimpleBufHandle header_buf)
{
	ModelLoadHeader model_header;
	if (!tempModelLoadHeader(header_buf, &model_header))
	{
		printf("Temp model header \"%s\" not present\n", name);
		return;
	}

	printf("Temp model header \"%s\" present\n", name);
	printf("Materials: %d\nTris: %d\nVerts: %d\n", model_header.material_count, model_header.tri_count, model_header.vert_count);
	printf("Min: (%f %f %f)\n", model_header.minBBox[0], model_header.minBBox[1], model_header.minBBox[2]);
	printf("Max: (%f %f %f)\n", model_header.maxBBox[0], model_header.maxBBox[1], model_header.maxBBox[2]);
}

static void dumpAtlHeader(SimpleBufHandle header_buf)
{
	U32 atlas_version = 0, color_size = 0;
	U8 detail_material_count = 0, terrain_material_count = 0;
	int i;

	// Following a format from writeAtlasBin
	SimpleBufReadU32(&atlas_version, header_buf);
	printf("Atlas header version %x (vs %x)\n", atlas_version, TERRAIN_ATLAS_VERSION);
	if (atlas_version != TERRAIN_ATLAS_VERSION)
	{
		printf("Mismatched atlas header version\n");
		return;
	}

	SimpleBufReadU32(&color_size, header_buf);
	SimpleBufSeek(header_buf, color_size, SEEK_CUR);

	printf("Color size %d\n", color_size);

	SimpleBufReadU8(&detail_material_count, header_buf);
	printf("Detail materials %d\n", detail_material_count);
	for (i = 0; i < detail_material_count; ++i)
	{
		char *material_name = NULL;
		SimpleBufReadString(&material_name, header_buf);
		printf("  Material %d: %s\n", i, material_name);
	}



	SimpleBufReadU8(&terrain_material_count, header_buf);
	printf("Terrain materials %d\n", detail_material_count);
	for (i = 0; i < terrain_material_count; ++i)
	{
		TerrainMeshRenderMaterial material;
		SimpleBufReadU8(&material.detail_material_ids[0], header_buf);
		SimpleBufReadU8(&material.detail_material_ids[1], header_buf);
		SimpleBufReadU8(&material.detail_material_ids[2], header_buf);
		SimpleBufReadU8(&material.color_idxs[0], header_buf);
		SimpleBufReadU8(&material.color_idxs[1], header_buf);
		SimpleBufReadU8(&material.color_idxs[2], header_buf);
		printf("  Material %d: %d %d %d, %d %d %d\n", i, material.detail_material_ids[0], material.detail_material_ids[1], material.detail_material_ids[2], 
			material.color_idxs[0], material.color_idxs[1], material.color_idxs[2]);
	}

	atlDumpTempModelLoadHeader("Visual", header_buf);
	atlDumpTempModelLoadHeader("Occlusion", header_buf);
	atlDumpTempModelLoadHeader("Collision", header_buf);
	atlDumpTempModelLoadHeader("Geo", header_buf);
}

AUTO_COMMAND;
void dumpAtlasBlock(int x, int z, int lod)
{
	ZoneMap *zmap = worldGetActiveMap();
	ZoneMapInfo *zmapInfo = zmapGetInfo(zmap);
	int k, j;

	if (!zmap)
		return;

	printf("Atlas dump (%d,%d) @ lod %d\n", x, z, lod);
	printf("Map: %s", zmapInfo->filename);

	// Iterate through the regions, check for atlases.
	filePushDiskAccessAllowedInMainThread(true);
	for(k = 0; k < eaSize(&(zmapInfo->regions)); k++) {

		WorldRegion *region = zmapInfo->regions[k];
		if(region->atlases)
		{
			printf("Region %s has %d atlases\n", region->name, eaSize(&region->atlases->all_atlases));
			for(j = 0; j < eaSize(&region->atlases->all_atlases); j++)
			{
				HeightMapAtlas * atlas = region->atlases->all_atlases[j];
				HogFile *atlas_hogg_file, *model_hogg_file = NULL;
				SimpleBuffer * header_buf = NULL;

				if (atlas->local_pos[0] != x || atlas->local_pos[1] != z || atlas->lod != lod)
					continue;

				atlas_hogg_file       = worldRegionGetTerrainHogFile(atlas->region, THOG_ATLAS, atlas->lod);
				if (atlas_hogg_file)
				{
					char relpath[MAX_PATH];
					sprintf(relpath, "x%d_z%d.atl", x, z);

					header_buf = SimpleBufOpenRead(relpath, atlas_hogg_file);
					if (header_buf)
					{
						dumpAtlHeader(header_buf);
						SimpleBufClose(header_buf);
					}
				}
			}
		}
	}	
	filePopDiskAccessAllowedInMainThread();
}
