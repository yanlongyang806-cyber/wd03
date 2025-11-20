#include "WorldCellClustering.h"
#include "WorldCellClustering_h_ast.h"
#include "WorldCell.h"
#include "WorldCellEntryPrivate.h"
#include "WorldCellBinning.h"
#include "WorldCellStreaming.h"
#include "WorldCellStreamingPrivate.h"
#include "GenericMesh.h"
#include "GenericMeshRemesh.h"
#include "wlState.h"
#include "fileCache.h"
#include "wlTerrainPrivate.h"
#include "wlModelBinningLOD.h"
#include "wlModelBinning.h"
#include "wlModelBinningPrivate.h"
#include "GenericMeshRemesh.h"
#include "TaskServerClientInterface.h"

#include "utilitiesLib.h"
#include "hoglib.h"
#include "hogutil.h"
#include "Serialize.h"
#include "error.h"
#include "timing.h"
#include "logging.h"
#include "fileutil.h"
#include "utils.h"
#include "wininclude.h"
#include "structHist.h"
#include "ImageTypes.h"
#include <errno.h>

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

#define DEFAULT_DISTRIBUTED_REMESH_TIMEOUT 300

// SIMPLYGON TODO move to gmesh? or WorldCellClustering?
GMesh * gmeshLoadFromHogFile(HogFile * handle, const char * filename);
bool hogFileCopyFileFromHogFile(HogFile *destHogFile, HogFile *sourceHogFile, const char *fileName, TaskProfile *copyOperation);
bool worldCellClusterAddFileToHogg(HogFile *destHogFile, const char *file_name, const char *file_hogg_name, TaskProfile *operationStats);
void modelClusterAddGeoStats(WorldClusterGeoStats *geoStats, GMesh *mesh);
bool checkClusterLODDependenciesUpToDate(const WorldCell *cell_src, const char *clusterBaseName, HogFile * cluster_bins);
void gatherInputClusters(ModelClusterSource * cluster, const WorldRegion *region, const WorldCell * cell, WorldClusterState *cluster_options, HogFile * cluster_bins);
bool checkClusterMeshDependenciesUpToDate(worldCellClusterGenerateCellClusterData *clusterData, ModelClusterSource * cluster_source, const char * remesh_file, const char *clusterBaseFilename, HogFile * cluster_bins);
void modelClusterSourceHoggCreate(worldCellClusterGenerateCellClusterData *clusterData, ModelClusterSource *newClusterSource, HogFile* regionHogg, WorldRegion *region, char * hoggFileName, int hoggFileName_size, bool cleanDirectory, int lod);
bool modelClusterSourceExportTextures(ModelClusterSource *modelCluster, const WorldClusterState * cluster_options, HogFile *destRemeshPackOptional, TaskProfile *exportTexStats);
bool remeshCluster(worldCellClusterGenerateCellClusterData * clusterData);
bool updateClusterRemeshCacheAndRetrieveClusterMesh(WorldRegion *region, const WorldCell * cell, const char *baseClusterName, HogFile * cluster_bins, WorldClusterState *cluster_options, worldCellClusterGenerateCellClusterData *clusterData);

// SIMPLYGON TODO move to WorldClusterDebug struct
int debug_preserve_simplygon_images = 0;
AUTO_CMD_INT(debug_preserve_simplygon_images, preserveSimplygonImages) ACMD_CMDLINEORPUBLIC;

// All const arrays for clustering referring to default textures should reflect the order of this enum.
typedef enum ClusterTextureIndex {
	CLUSTER_TEXTURE_DIFFUSE_INDEX = 0,
	CLUSTER_TEXTURE_SPECULAR_INDEX = 1,
	CLUSTER_TEXTURE_NORMAL_INDEX = 2,
} ClusterTextureIndex;

const char * ClusterTextureSuffices[CLUSTER_MAX_TEXTURES] =
{
	CLUSTER_TEXTURE_FILENAME_DIFFUSE_SUFFIX,
	CLUSTER_TEXTURE_FILENAME_SPECULAR_SUFFIX,
	CLUSTER_TEXTURE_FILENAME_NORMAL_SUFFIX
};

const char * ClusterTextureDefaults[CLUSTER_MAX_TEXTURES] =
{
	CLUSTER_TEXTURE_DEFAULT_DIFFUSE,
	CLUSTER_TEXTURE_DEFAULT_SPECULAR,
	CLUSTER_TEXTURE_DEFAULT_NORMAL
};

// This needs to always reflect the textures being used within the Cluster_Default material.  Also, order of textures must follow ClusterTextureIndex
const char* cluster_default_textures[] = { "white", "black", "Default_Flatnormal_Nx" };

static WorldClusterDebug cluster_debug_settings = { 0, CBDL_DisableRemesh, 0, 0, 0, 0, { 0, 0, 0, 0 }, 0, 64, 4, 2400, false, false };

const WorldClusterDebug * worldCellGetClusterDebugState()
{
	return &cluster_debug_settings;
}

bool worldCellClusterIsRemeshEnabled(const WorldClusterDebug * debug_state)
{
	if (debug_state)
		return debug_state->disableWorldCluster < CBDL_DisableRemesh;

	return cluster_debug_settings.disableWorldCluster < CBDL_DisableRemesh;
}

void SetClusterDebugSettings(int x1, int x2, int z1, int z2, bool distRemesh, bool highPrecisionRemesh, bool cluster_all_levels)
{
	cluster_debug_settings.cluster_range[0] = x1;
	cluster_debug_settings.cluster_range[1] = x2;
	cluster_debug_settings.cluster_range[2] = z1;
	cluster_debug_settings.cluster_range[3] = z2;
	cluster_debug_settings.testWorldCluster = 1;
	cluster_debug_settings.high_precision_remesh = highPrecisionRemesh;
	cluster_debug_settings.cluster_all_levels = cluster_all_levels;

	if (distRemesh)
	{
		cluster_debug_settings.disableWorldCluster = CBDL_Enable;
		cluster_debug_settings.max_distributed_remesh_tasks = 64;
		setAllowLocalRemesh(0);
	}
}

// Enable clustering/remeshing on maps that do not have regions with clustering enabled. 
// For testing build processes without modifying the map.
AUTO_CMD_INT(cluster_debug_settings.testWorldCluster, debug_testWorldCluster) ACMD_CATEGORY(DEBUG) ACMD_CMDLINEORPUBLIC;
// Totally disable clustering/remeshing on maps. Takes precedence over all other settings. Even
// maps that do have regions with clustering enabled and debug_testWorldCluster will not override
// this setting. This just disables binning the data!
AUTO_CMD_INT(cluster_debug_settings.disableWorldCluster, cluster_disable_build) ACMD_CMDLINEORPUBLIC;
AUTO_CMD_INT(cluster_debug_settings.obj_atlas_export, debug_obj_atlas_export) ACMD_CMDLINEORPUBLIC;
AUTO_CMD_INT(cluster_debug_settings.max_cluster_lod, debug_max_cluster_lod) ACMD_CMDLINEORPUBLIC;
AUTO_CMD_INT(cluster_debug_settings.ignore_remesh_atlas_hogg, debug_ignore_remesh_atlas_hogg) ACMD_CMDLINEORPUBLIC;
AUTO_CMD_INT(cluster_debug_settings.dumpWorldClusterSource, debug_dumpWorldClusterSource) ACMD_CMDLINEORPUBLIC;
// turns on/off the gathering of objects containing materials with transparency for the remesh clusters.
AUTO_CMD_INT(cluster_debug_settings.dont_cluster_alpha_objects, debug_dont_cluster_alpha_objects) ACMD_CATEGORY(Debug) ACMD_CMDLINE;
// Tunes the number of distributed jobs allowed.
AUTO_CMD_INT(cluster_debug_settings.max_distributed_remesh_tasks, debug_cluster_max_distributed_tasks) ACMD_CMDLINEORPUBLIC;
AUTO_CMD_INT(cluster_debug_settings.high_precision_remesh, debug_cluster_leaf_level) ACMD_CMDLINEORPUBLIC;

// Tunes the number of times the client may reattempt failed remesh tasks for each world cell cluster.
AUTO_CMD_INT(cluster_debug_settings.max_retries, debug_cluster_max_retries) ACMD_CMDLINEORPUBLIC;

// Tunes the timeout, in seconds, before the client automatically retries a distributed task
AUTO_CMD_INT(cluster_debug_settings.distributed_remesh_retry_timeout, debug_cluster_distributed_remesh_retry_timeout) ACMD_CMDLINEORPUBLIC;

AUTO_COMMAND ACMD_NAME(debug_cluster_range) ACMD_CMDLINEORPUBLIC;
void debug_cluster_range_cmd(int x1, int x2, int z1, int z2)
{
	cluster_debug_settings.cluster_range[0] = x1;
	cluster_debug_settings.cluster_range[1] = x2;
	cluster_debug_settings.cluster_range[2] = z1;
	cluster_debug_settings.cluster_range[3] = z2;
}

void worldGetClusterName(char *dest_str, int dest_str_size, const char *strRegionName, const BlockRange *cellBlockRange, int vis_dist_level)
{
	sprintf_s(SAFESTR2(dest_str), "cluster_%s_%dX_%dY_%dZ__%dX_%dY_%dZ_L%d", 
		strRegionName ? strRegionName : "Default",
		cellBlockRange->min_block[0], cellBlockRange->min_block[1], cellBlockRange->min_block[2],
		cellBlockRange->max_block[0], cellBlockRange->max_block[1], cellBlockRange->max_block[2],
		vis_dist_level
		);
}

void worldGetClusterBinHoggName(char *dest_str, int dest_str_size, const char *strRegionName, const BlockRange *cellRange, int vis_dist_level)
{
	char clusterName[MAX_PATH];
	worldGetClusterName(SAFESTR(clusterName), strRegionName, cellRange, vis_dist_level);
	sprintf_s(SAFESTR2(dest_str), "world_%s_cluster.hogg", clusterName);
}

void worldGetClusterBinHoggPath(char *dest_str, int dest_str_size, const WorldRegion *pRegion, const BlockRange *cellRange, int vis_dist_level)
{
	char base_dir[MAX_PATH];
	char clusterName[MAX_PATH];
	worldGetClientBaseDir(zmapGetFilename(pRegion->zmap_parent), SAFESTR(base_dir));
	worldGetClusterBinHoggName(SAFESTR(clusterName), pRegion->name, cellRange, vis_dist_level);

	sprintf_s(SAFESTR2(dest_str), "%s/%s", base_dir, clusterName);
}

void worldCellGetClusterName(char *dest_str, int dest_str_size, const WorldCell *cell_src)
{
	worldGetClusterName(dest_str, dest_str_size, cell_src->region->name, &cell_src->cell_block_range, cell_src->vis_dist_level);
}

void worldCellGetClusterBinHoggName(char *dest_str, int dest_str_size, const WorldCell *cell_src)
{
	char clusterName[MAX_PATH];
	worldCellGetClusterName(SAFESTR(clusterName), cell_src);
	sprintf_s(SAFESTR2(dest_str), "world_%s_cluster.hogg", clusterName);
}

void worldCellGetClusterBinHoggPath(char *dest_str, int dest_str_size, const WorldCell *cell_src)
{
	char base_dir[MAX_PATH];
	char clusterName[MAX_PATH];
	worldGetClientBaseDir(zmapGetFilename(cell_src->region->zmap_parent), SAFESTR(base_dir));
	worldCellGetClusterBinHoggName(SAFESTR(clusterName), cell_src);

	sprintf_s(SAFESTR2(dest_str), "%s/%s", base_dir, clusterName);
}

void worldCellClusterGetTempDir(const char *zmapFilename, char *base_dir, size_t base_dir_size)
{
	worldGetTempBaseDir(zmapFilename, base_dir, base_dir_size);
	strcat_s(base_dir,base_dir_size,"/cluster_resources/");
}

void worldCellClusterMakeTextureName(char *textureName, int textureName_size, const char *basePath, const char *clusterName, const char *imageExtension, int textureIndex)
{
	assert(textureIndex >= 0 && textureIndex < CLUSTER_MAX_TEXTURES);
	sprintf_s(SAFESTR2(textureName), "%s%s%s.%s", basePath ? basePath : "", clusterName, 
		ClusterTextureSuffices[textureIndex], imageExtension);
}

/// Fixup function for WorldClusterState
TextParserResult fixupWorldClusterState(WorldClusterState *worldClusterState, enumTextParserFixupType eType, void *pExtraData)
{
	if (eType == FIXUPTYPE_DESTRUCTOR) {
		if (worldClusterState->regionTextureTable)
			stashTableDestroy(worldClusterState->regionTextureTable);
	}
	return PARSERESULT_SUCCESS;
}

bool worldCellClusteringFeatureEnabled()
{
	return simplygonGetEnabled() && wlIsClient() && (cluster_debug_settings.disableWorldCluster != CBDL_DisableCompletely);
}

bool worldCellClusteringIsRegionClustered(const WorldRegion * region)
{
	return worldCellClusteringFeatureEnabled() && (region->bWorldGeoClustering || cluster_debug_settings.testWorldCluster);
}

void worldCellClusteringSetupRegionClustering(ZoneMap *zmap)
{
	int k;

	// Iterate through the regions, check for clustering options & world cell trees
	for (k = 0; k < eaSize(&zmap->map_info.regions); k++)
	{
		WorldRegion *region = zmap->map_info.regions[k];
		StructDestroy(parse_WorldClusterState, region->cluster_options);
		region->cluster_options = NULL;

		// check, or region option changed
		if (region->root_world_cell && (worldRegionGetWorldGeoClustering(region) || cluster_debug_settings.testWorldCluster))
		{
			const char *zmapFilename = zmapGetFilename(zmap);
			WorldClusterState * region_cluster_options = NULL;
			char *tempBinBreak;
			const char *regionNameStr = region->name ? region->name : "Default";

			region->cluster_options = region_cluster_options = StructCreateDefault(WorldClusterState);
			region_cluster_options->debug = cluster_debug_settings;
			if (!region_cluster_options->debug.distributed_remesh_retry_timeout)
				region_cluster_options->debug.distributed_remesh_retry_timeout = DEFAULT_DISTRIBUTED_REMESH_TIMEOUT;
			worldGetClientBaseDir(zmapFilename, SAFESTR(region_cluster_options->clusterBinPath));
			worldCellClusterGetTempDir(zmapFilename, SAFESTR(region_cluster_options->clusterTempBinPath));

			tempBinBreak = strpbrk(region_cluster_options->clusterTempBinPath,"/\\");
			assert(tempBinBreak != NULL);
			tempBinBreak ++;
			sprintf(region_cluster_options->clusterCacheHoggFile,
				"clusterInfo/%s/remesh_%s_models.hogg",
				tempBinBreak, regionNameStr);

			sprintf(region_cluster_options->clusterTempImagePath, "%s/remesh_images/", region_cluster_options->clusterTempBinPath);
			fileLocateWrite(region_cluster_options->clusterTempImagePath, region_cluster_options->clusterTempImageFullPath);

			sprintf(region_cluster_options->clusterClientRegionBinFile, "%s/world_%s_cluster.hogg", region_cluster_options->clusterBinPath, regionNameStr);
			fileLocateWrite(region_cluster_options->clusterClientRegionBinFile, region_cluster_options->clusterClientRegionBinFileFullPath);

			getRegionClusterBlockRange(zmap, k, region->cluster_options);
		}
	}
}

ModelClusterSource * worldCellClusterGetOrCreateBucket(WorldClusterState * clustering_options, const IVec3 entry_pos)
{
	ModelClusterSource *bucket = NULL;
	if (entry_pos[0] >= clustering_options->start_i && entry_pos[0] < clustering_options->end_i && 
		entry_pos[2] >= clustering_options->start_j && entry_pos[2] < clustering_options->end_j)
	{
		int cellSizeX = clustering_options->end_i - clustering_options->start_i;
		int cellSizeY = clustering_options->end_y - clustering_options->start_y;
		int cellSizeZ = clustering_options->end_j - clustering_options->start_j;

		int posX = entry_pos[0] - clustering_options->start_i;
		int posY = entry_pos[1] - clustering_options->start_y;
		int posZ = entry_pos[2] - clustering_options->start_j;

		size_t bucket_index = posX + posZ * cellSizeX + posY * cellSizeX * cellSizeZ;
		bucket = clustering_options->ea_cluster_buckets[bucket_index];
		if (!bucket) {
			bucket = clustering_options->ea_cluster_buckets[bucket_index] = modelClusterSourceCreateDefault();
		}
	}
	return bucket;
}

__forceinline void worldCellClusterEntryGetPos(const WorldDrawableEntry *entry, Vec3 pos) {
	copyVec3(entry->base_entry.bounds.world_matrix[3],pos);
}

void worldCellCalcEntryPos(const Vec3 pos, IVec3 entry_pos)
{
	entry_pos[0] = floor(pos[0] / GRID_BLOCK_SIZE);
	entry_pos[1] = floor(pos[1] / GRID_BLOCK_SIZE);
	entry_pos[2] = floor(pos[2] / GRID_BLOCK_SIZE);
}

__forceinline void worldCellClusterEntryGetClusterPos(const WorldDrawableEntry *entry, IVec3 pos) {
	worldCellCalcEntryPos(entry->base_entry.bounds.world_mid,pos);
}

ModelClusterSource * worldCellClusterGetOrCreateBucketForEntry(WorldClusterState *clustering_options, const WorldDrawableEntry *entry)
{
	ModelClusterSource *bucket = NULL;
	IVec3 entry_pos;

	worldCellClusterEntryGetClusterPos(entry,entry_pos);
	return worldCellClusterGetOrCreateBucket(clustering_options, entry_pos);
}

static void worldCellClusterCheckNeedUpdate(WorldCell *cell_src, WorldClusterState *simplygonMeshes, ModelClusterSource *clusterSource)
{
	char *estrClusterDebugOut = NULL;
	char baseClusterFilename[MAX_PATH];

	worldCellGetClusterName(SAFESTR(baseClusterFilename), cell_src);

	modelClusterProcessVolumes(clusterSource, simplygonMeshes, cell_src);

	if (checkClusterDependenciesUpToDate(clusterSource, baseClusterFilename, simplygonMeshes->remesh_model_hogg_file, cell_src->region, cell_src->vis_dist_level))
	{
		if (!cell_src->vis_dist_level && modelClusterIsEmpty(clusterSource))	// Check is only valid for lowest vis_dist_level since levels above zero never hold ModelClusterObjects and so is not indicative of whether the cell is truly "empty".
		{
			char remeshName[MAX_PATH];
			HogFileIndex existing_remesh_file = HOG_INVALID_INDEX;
			sprintf(remeshName, "%s.msh", baseClusterFilename);

			// empty cluster implies delete cached remesh, if it exists
			existing_remesh_file = hogFileFind(simplygonMeshes->remesh_model_hogg_file, remeshName);
			if (existing_remesh_file != HOG_INVALID_INDEX)
				hogFileModifyDeleteNamed(simplygonMeshes->remesh_model_hogg_file, remeshName);
		}
	}

	if (simplygonMeshes->debug.dumpWorldClusterSource)
	{
		estrClear(&estrClusterDebugOut);
		ParserWriteText(&estrClusterDebugOut, parse_ModelClusterSource, clusterSource, 0, 0, 0);
		OutputDebugStringf("%s\n", estrClusterDebugOut);
		estrClear(&estrClusterDebugOut);
	}

	estrDestroy(&estrClusterDebugOut);
}

static void updateClusterLODs(WorldCell *cell_src, WorldClusterState *simplygonMeshes)
{
	ModelClusterSource *newClusterSource = modelClusterSourceCreateDefault();

	worldCellClusterCheckNeedUpdate(cell_src, simplygonMeshes, newClusterSource);

	StructDestroy(parse_ModelClusterSource, newClusterSource);
}

static void updateWorldBlockCluster(WorldCell *cell_src, WorldClusterState *simplygonMeshes)
{
	ModelClusterSource *newClusterSource = NULL;

	newClusterSource = worldCellClusterGetOrCreateBucket(simplygonMeshes, cell_src->cell_block_range.min_block);

	if (newClusterSource)
		cell_src->cluster_related = 1;

	worldCellClusterCheckNeedUpdate(cell_src, simplygonMeshes, newClusterSource);
}

void scaleMat44ColsByVec3(Mat44 a, const Vec3 scale)
{
	scaleVec4(a[0], scale[0], a[0]);
	scaleVec4(a[1], scale[1], a[1]);
	scaleVec4(a[2], scale[2], a[2]);
}

ModelClusterObject *createFromDrawableEntry(const WorldDrawableEntry *drawableEntry, const WorldClusterState * clustering_options, int *totalTexSize)
{
	ModelClusterObject *modelObject = NULL;
	Mat4	scaleMat = {0};
	Model	*model = NULL;
	ModelLOD *lod0 = NULL;
	Vec3 cell_center;
	IVec3 cluster_pos;
	int		i;
	const WorldCellEntryData *entryData = worldCellEntryGetData((WorldCellEntry*)&drawableEntry->base_entry);

	if (!drawableEntry->base_entry.shared_bounds->model)
		return NULL;
	if (drawableEntry->high_detail || drawableEntry->high_fill_detail)
		return NULL;

	model = modelFind(drawableEntry->base_entry.shared_bounds->model->name,false,WL_FOR_WORLD);
	lod0 = modelLODLoadAndMaybeWait(model, 0, true);

	if (!lod0 || !lod0->data)
		return NULL;

	modelObject = StructCreateDefault(ModelClusterObject);
	modelObject->model_filename = drawableEntry->base_entry.shared_bounds->model->name;
	modelObject->model_timestamp = fileLastChanged(modelObject->model_filename);
	{
		// fill up materials
		for (i = 0; i < lod0->data->tex_count; i++) {
			NamedTimestamp *matStamp = StructCreateDefault(NamedTimestamp);
			Material *lodMaterialI = lod0->materials[i];
			const MaterialData *lodMaterialIData = materialGetData(lodMaterialI);

			eaPush(&modelObject->mat_stamps,matStamp);
			matStamp->name = lodMaterialI->material_name;
			matStamp->timestamp = fileLastChanged(lodMaterialIData->filename);
			assert(wl_state.gfx_material_get_textures);
			{
				BasicTexture **matTextures = NULL;
				int j = wl_state.gfx_material_get_textures(lodMaterialI, &matTextures) - 1;
				// fill up textures
				for (; j >= 0; j--) {
					const char *texName = allocAddString(wl_state.tex_fullname_func(matTextures[j]));
					NamedTimestamp *texStamp = StructCreateDefault(NamedTimestamp);

					texStamp->name = texName;
					texStamp->timestamp = fileLastChanged(texName);
					eaPush(&modelObject->tex_stamps,texStamp);
				}
			}
		}
	}
	worldCellClusterEntryGetClusterPos(drawableEntry,cluster_pos);
	worldCellClusterEntryGetPos(drawableEntry,modelObject->position);
	modelObject->radius = drawableEntry->base_entry.shared_bounds->radius;
	mat43to44(drawableEntry->base_entry.bounds.world_matrix,modelObject->model_matrix);
	if (drawableEntry->base_entry.shared_bounds->use_model_bounds)
		scaleMat44ColsByVec3(modelObject->model_matrix, drawableEntry->base_entry.shared_bounds->model_scale);
	worldCellGetBlockRangeCenter(entryData->cell, cell_center);
	for (i = 0; i < 3; i++)
		cell_center[i] = (cluster_pos[i] + 0.5) * GRID_BLOCK_SIZE;
	subVec3(modelObject->model_matrix[3], cell_center, modelObject->model_matrix[3]);
	return modelObject;
}

void worldCellClusterBucketDrawableEntry(WorldClusterState * clustering_options, const WorldDrawableEntry * entry, TextureSwap **tex_swaps, MaterialSwap **mat_swaps, const char* material_replace_name)
{
	ModelClusterSource * mcs_bucket = worldCellClusterGetOrCreateBucketForEntry(clustering_options, entry);
	if (mcs_bucket)
	{
		ModelClusterObject *mcObj = createFromDrawableEntry(entry, clustering_options, NULL);
		if (mcObj) {
			int i;
			for (i = 0; i < eaSize(&tex_swaps); i++) {
				RemeshAssetSwap *mcoTexSwap = StructCreateDefault(RemeshAssetSwap);
				mcoTexSwap->orig_name = tex_swaps[i]->orig_name;
				mcoTexSwap->replace_name = tex_swaps[i]->replace_name;
				eaPush(&mcObj->tex_swaps,mcoTexSwap);
			}
			for (i = 0; i < eaSize(&mat_swaps); i++) {
				RemeshAssetSwap *mcoMatSwap = StructCreateDefault(RemeshAssetSwap);	// RemeshMaterialSwap is RemeshTextureSwap object
				mcoMatSwap->orig_name = mat_swaps[i]->orig_name;
				mcoMatSwap->replace_name = mat_swaps[i]->replace_name;
				eaPush(&mcObj->mat_swaps,mcoMatSwap);
			}
			eaPush(&mcs_bucket->cluster_models, mcObj);
		}
	}
}

static int countCellLOD0ClusterSources(const WorldCell *cell_src, const WorldClusterState *clusterState)
{
	int i;
	int count_lod0_clusters = 0;

	if (!cell_src)
		return 0;

	// recurse first so child cells exist when going to update their bounds for near fade entries
	for (i = 0; i < ARRAY_SIZE(cell_src->children); ++i)
	{
		const WorldCell * child_cell = cell_src->children[i];
		if (child_cell)
		{
			count_lod0_clusters += countCellLOD0ClusterSources(child_cell, clusterState);
		}
	}

	if (cell_src->vis_dist_level == 0)
	{
		if ( cell_src->cell_block_range.min_block[0] >= clusterState->start_i && cell_src->cell_block_range.max_block[0] < clusterState->end_i &&
			cell_src->cell_block_range.min_block[2] >= clusterState->start_j && cell_src->cell_block_range.max_block[2] < clusterState->end_j)
		{
			++count_lod0_clusters;
		}
	}
	return count_lod0_clusters;
}

void getRegionClusterBlockRange(const ZoneMap *zmap, int k, WorldClusterState * clustering_options)
{
	WorldRegion * region = zmap->map_info.regions[k];

	eaDestroy(&clustering_options->ea_cluster_buckets);
	clustering_options->start_i = region->world_bounds.world_min[0] / 256 + (region->world_bounds.world_min[0] < 0 ? -1 : 0);
	clustering_options->end_i = region->world_bounds.world_max[0] / 256 + (region->world_bounds.world_max[0] < 0 ? 0 : 1);
	clustering_options->start_y = region->world_bounds.world_min[1] / 256 + (region->world_bounds.world_min[1] < 0 ? -1 : 0);
	clustering_options->end_y = region->world_bounds.world_max[1] / 256 + (region->world_bounds.world_max[1] < 0 ? 0 : 1);
	clustering_options->start_j = region->world_bounds.world_min[2] / 256 + (region->world_bounds.world_min[2] < 0 ? -1 : 0);
	clustering_options->end_j = region->world_bounds.world_max[2] / 256 + (region->world_bounds.world_max[2] < 0 ? 0 : 1);
	if (clustering_options->debug.cluster_range[0] < clustering_options->debug.cluster_range[1] && 
		clustering_options->debug.cluster_range[2] < clustering_options->debug.cluster_range[3])
	{
		clustering_options->start_i = clustering_options->debug.cluster_range[0];
		clustering_options->end_i = clustering_options->debug.cluster_range[1];
		clustering_options->start_j = clustering_options->debug.cluster_range[2];
		clustering_options->end_j = clustering_options->debug.cluster_range[3];
	}
	eaSetSize(&clustering_options->ea_cluster_buckets,
		(clustering_options->end_i - clustering_options->start_i) * 
		(clustering_options->end_y - clustering_options->start_y) * 
		(clustering_options->end_j - clustering_options->start_j));

	clustering_options->processed_blocks = 1;
	clustering_options->total_blocks = countCellLOD0ClusterSources(region->root_world_cell, clustering_options);
	clustering_options->current_region_idx = k + 1;
	clustering_options->total_regions = eaSize(&zmap->map_info.regions);
	clustering_options->current_region_ptr = region;
	clustering_options->gatherFromLeafLevel = clustering_options->debug.high_precision_remesh;
	clustering_options->cluster_all_levels = clustering_options->debug.cluster_all_levels;
	region->cluster_options = clustering_options;
}

static void updateCellClusterSources(WorldCell *cell_src, WorldClusterState *clusterState)
{
	int i;

	if (!cell_src)
		return;

	// recurse first so child cells exist when going to update their bounds for near fade entries
	for (i = 0; i < ARRAY_SIZE(cell_src->children); ++i)
	{
		if (cell_src->children[i])
		{
			updateCellClusterSources(cell_src->children[i], clusterState);
			cell_src->cluster_related |= cell_src->children[i]->cluster_related;
		}
	}

	if ( cell_src->cell_block_range.min_block[0] < clusterState->end_i && cell_src->cell_block_range.max_block[0] >= clusterState->start_i &&
		cell_src->cell_block_range.min_block[2] < clusterState->end_j && cell_src->cell_block_range.max_block[2] >= clusterState->start_j)
	{
		if (cell_src->vis_dist_level == 0)
		{
			updateWorldBlockCluster(cell_src, clusterState);
		}
		else
		{
			updateClusterLODs(cell_src, clusterState);
		}
	}
}

void worldCellClusteringFinishRegionClusterGather(ZoneMap *zmap)
{
	int k;

	// Iterate through the regions
	for(k = 0; k < eaSize(&(zmap->map_info.regions)); k++) {
		WorldRegion *region = zmap->map_info.regions[k];
		bool worldCellsChangedFlag = false;
		WorldClusterState * clustering_options = region->cluster_options;
		bool bCreatedHog = false;
		int hogReadResult = 0;

		// Need to either check or delete the output world cell dependency hogg for the region, depending on clustering
		// being enabled.

		if (!clustering_options)
			continue;

		if (!clustering_options->remesh_model_hogg_file)
		{
			// Update the model cluster sources in the model cache hogg. Note we don't list this as an output dependency because we don't want it destroyed
			// automatically on world cell rebin, because if the world cells change only slightly, the clusters may still be mostly
			// identical, and we performed detailed dependency checking on the cluster data since it is so expensive.
			worldCellClusteringOpenRegionClusterHog(clustering_options, region, clustering_options->clusterTempBinPath);

			if (clustering_options->debug.ignore_remesh_atlas_hogg) {
				// wipe all files contained within and pretend the hogg file was just created if debug flag is set.
				hogDeleteAllFiles(clustering_options->remesh_model_hogg_file);
			}
		}

		if (region->root_world_cell)
		{
			// All cells that clusters are dependent upon will have cluster_load set to 1.
			updateCellClusterSources(region->root_world_cell, clustering_options);
		}

		worldCellClusteringCloseRegionClusterHog(clustering_options);
	}
}

void worldCellClusteringCleanupRegionClustering(ZoneMap *zmap, BinFileList *file_list)
{
	int k;

	// Iterate through the regions, check for root world cells.
	for (k = 0; k < eaSize(&(zmap->map_info.regions)); k++)
	{
		WorldRegion *region = zmap->map_info.regions[k];
		WorldClusterState *clusterState = region->cluster_options;

		if (clusterState)
		{
			StructDestroy(parse_WorldClusterState, clusterState);
			region->cluster_options = NULL;
		}
	}
}

bool worldCellClusterQueueEntryChangeState(worldCellClusterGenerateCellClusterData *clusterQueueEntry,
	ClusterRemeshState finalState, ClusterRemeshState expectedCurrentState)
{
	U32 priorState = InterlockedCompareExchange(&clusterQueueEntry->remeshState, finalState, expectedCurrentState);
	return priorState == expectedCurrentState ? true : false;
}

bool worldCellClusterQueueEntryCompleteLocal(worldCellClusterGenerateCellClusterData *clusterQueueEntry)
{
	return worldCellClusterQueueEntryChangeState(clusterQueueEntry, CRS_Complete, CRS_Remeshing);
}

bool worldCellClusterQueueEntryCompleteDistributed(worldCellClusterGenerateCellClusterData *clusterQueueEntry)
{
	return worldCellClusterQueueEntryChangeState(clusterQueueEntry, CRS_Complete, CRS_RemeshingDistributed);
}

void worldCellClusterQueueEntryForceRetry(worldCellClusterGenerateCellClusterData *clusterQueueEntry)
{
	if (worldCellClusterQueueEntryChangeState(clusterQueueEntry, CRS_NotQueued, CRS_RemeshingDistributed))
		InterlockedDecrement(&clusterQueueEntry->clusterState->numDistributedRemeshes);
}

void worldCellClusteringCloseRegionClusterHog(WorldClusterState *clusterState)
{
	if (clusterState->remesh_model_hogg_file)
	{
		hogFileDestroy(clusterState->remesh_model_hogg_file, false);
		clusterState->remesh_model_hogg_file = false;
	}
}

void worldCellClusteringOpenRegionClusterHog(WorldClusterState *clusterState, const WorldRegion *region, const char *baseTempDir)
{
	char clusterCacheHoggFilenameAbsolute[MAX_PATH];
	int error = 0;

	if (clusterState->remesh_model_hogg_file)
		worldCellClusteringCloseRegionClusterHog(clusterState);

	//////////////////////////////////////////////////
	// BTH: Once remesh files are no longer patched into gimme, strip the two uncommented lines and uncomment the commented one.
	//fileLocateWrite(clusterState->clusterCacheHoggFile, clusterCacheHoggFilenameAbsolute);
	sprintf(clusterCacheHoggFilenameAbsolute,"%s/%s",fileLocalDataDir(),clusterState->clusterCacheHoggFile);
	mkdirtree_const(clusterCacheHoggFilenameAbsolute);
	//////////////////////////////////////////////////
	clusterState->remesh_model_hogg_file = hogFileRead(clusterCacheHoggFilenameAbsolute, NULL, PIGERR_ASSERT, &error, HOG_MUST_BE_WRITABLE);
}

void worldCellClusterCleanupMesh(worldCellClusterGenerateCellClusterData *clusteringData)
{
	// Clean up our temporary mesh.
	if (clusteringData->remeshedClusterMesh)
	{
		gmeshFree(clusteringData->remeshedClusterMesh);
		clusteringData->remeshedClusterMesh = NULL;
	}
}

void worldCellClusterGenerateCellCluster(worldCellClusterGenerateCellClusterData *clusteringData)
{
	const WorldCell *cell_src = clusteringData->cell_src;
	WorldClusterState * clusterState = clusteringData->clusterState;
	ModelClusterSource * source_models = clusteringData->source_models;
	WorldRegion *region = cell_src->region;
	char cell_cluster_name[MAX_PATH];
	int cluster_lod = cell_src->vis_dist_level;
	IVec3 cluster_pos;
	S64 timeStart,timeEnd;
	bool bDistributed = clusteringData->remeshState == CRS_RemeshingDistributed;

	copyVec3(cell_src->cell_block_range.min_block, cluster_pos);
	worldCellGetClusterName(cell_cluster_name, ARRAY_SIZE(cell_cluster_name), cell_src);

	if (cluster_lod == 0)
	{
		if ( cell_src->cell_block_range.min_block[0] >= clusterState->start_i && cell_src->cell_block_range.max_block[0] < clusterState->end_i &&
			cell_src->cell_block_range.min_block[2] >= clusterState->start_j && cell_src->cell_block_range.max_block[2] < clusterState->end_j)
		{
			loadstart_printf("Clustering region \"%s\", %d of %d, world cells block (%d, %d, %d) - (%d, %d, %d) @L%d, %d of %d: (%d, %d) - (%d, %d)...", 
				clusterState->current_region_ptr->name ? clusterState->current_region_ptr->name : "Default",
				clusterState->current_region_idx, clusterState->total_regions,
				cell_src->cell_block_range.min_block[0], cell_src->cell_block_range.min_block[1], cell_src->cell_block_range.min_block[2],
				cell_src->cell_block_range.max_block[0], cell_src->cell_block_range.max_block[1], cell_src->cell_block_range.max_block[2],
				cell_src->vis_dist_level,
				clusterState->processed_blocks, clusterState->total_blocks,
				clusterState->start_i, clusterState->start_j, clusterState->end_i, clusterState->end_j);

			log_printf(LOG_SIMPLYGON,"\tProcessing Cluster (%d, %d, %d) @%d lod\n", cluster_pos[0], cluster_pos[1], cluster_pos[2], cluster_lod);
			GET_CPU_TICKS_64(timeStart);

			++clusterState->processed_blocks;
			if (!updateClusterRemeshCacheAndRetrieveClusterMesh(cell_src->region,
				cell_src,
				cell_cluster_name,
				clusterState->remesh_model_hogg_file,
				clusterState,
				clusteringData))
			{
				log_printf(LOG_SIMPLYGON,"\tFailed to process cluster\n");
				ErrorDetailsf("Cluster (%d, %d, %d) @%d lod", cluster_pos[0], cluster_pos[1], cluster_pos[2], cluster_lod);
				Errorf("Failed to remesh world cluster chunk!");
			} else {
				GET_CPU_TICKS_64(timeEnd);

				log_printf(LOG_SIMPLYGON,"\tCluster (%d, %d, %d) @%d lod processed in %f milliseconds\n", cluster_pos[0], cluster_pos[1], cluster_pos[2], cluster_lod, timerSeconds64(timeEnd - timeStart));
			}

			loadend_printf(" done.");
		}
		else
			worldCellClusterQueueNotifyRemeshComplete(clusteringData, NULL, false);
	}
	else
	if (cell_src->vis_dist_level <= clusterState->debug.max_cluster_lod || !clusterState->debug.max_cluster_lod)
	{
		// see if the lower level LOD is available
		loadstart_printf("Clustering \"%s\" LOD %d, block (%d, %d, %d), %d of %d...", 
			region->name ? region->name : "Default",
			cluster_lod, cluster_pos[0], cluster_pos[1], cluster_pos[2],
			clusterState->processed_blocks, clusterState->total_blocks);

		++clusterState->processed_blocks;
		GET_CPU_TICKS_64(timeStart);

		log_printf(LOG_SIMPLYGON,"\tBegin loading/creating mesh for Cluster (%d, %d, %d) @%d lod\n", cluster_pos[0], cluster_pos[1], cluster_pos[2], cluster_lod);

		if (!updateClusterRemeshCacheAndRetrieveClusterMesh(region, cell_src, cell_cluster_name, 
			clusterState->remesh_model_hogg_file, clusterState, 
			clusteringData))
		{
			ErrorDetailsf("Cluster LOD (%d, %d, %d) @%d lod", cluster_pos[0], cluster_pos[1], cluster_pos[2], cluster_lod);
			Errorf("Failed to remesh world cluster LOD chunk!");
			log_printf(LOG_SIMPLYGON,"\tCluster (%d, %d, %d) @%d lod failed\n", cluster_pos[0], cluster_pos[1], cluster_pos[2], cluster_lod);
		} else {
			GET_CPU_TICKS_64(timeEnd);
			log_printf(LOG_SIMPLYGON,"\tCluster (%d, %d, %d) @%d lod: %f milliseconds\n", cluster_pos[0], cluster_pos[1], cluster_pos[2], cluster_lod, timerSeconds64(timeEnd - timeStart));
		}

		loadend_printf(" done.");
	}
	else
		worldCellClusterQueueNotifyRemeshComplete(clusteringData, NULL, false);

	worldCellClusterWriteResultsToMapBins(clusteringData);
}

static bool worldCellClusterTestExclusionFromMapBin(worldCellClusterGenerateCellClusterData *clusterData)
{
	if (!clusterData->source_models)
		return true;	// nothing to process, so yes, it should be excluded.

	if (clusterData->cell_src->vis_dist_level < clusterData->source_models->cluster_volumes_condensed->lod_min)
		return true;

	if (	clusterData->source_models->cluster_volumes_condensed->lod_max &&
			clusterData->cell_src->vis_dist_level >=
			(clusterData->source_models->cluster_volumes_condensed->lod_max + clusterData->source_models->cluster_volumes_condensed->lod_min))
		return true;

	return false;
}

bool worldCellClusterWriteResultsToMapBins(worldCellClusterGenerateCellClusterData *clusterData)
{
	bool bWriteResultBinsSuccess = true;
	char bin_hogg_name[MAX_PATH];
	bool excludedFromMapBin = worldCellClusterTestExclusionFromMapBin(clusterData);
	worldCellGetClusterBinHoggPath(SAFESTR(bin_hogg_name), clusterData->cell_src);
	fileLocateWrite(bin_hogg_name, bin_hogg_name);

	if (clusterData->remeshedClusterMesh && !excludedFromMapBin)
	{
		WorldClusterState *clusterState = clusterData->clusterState;
		HogFile *dest_bin_hog_file = NULL;
		const char * cell_cluster_name = clusterData->clusterBaseName;
		char mset_header_name[MAX_PATH];
		char mset_name[MAX_PATH];
		SimpleBufHandle headerBuf = NULL;
		SimpleBufHandle modelBuf = NULL;
		Geo2LoadData *gld = NULL;
		int textureIndex = 0;
		int hogErrorResult = 0;
		bool hoggCreated = false;

		hogErrorResult = fileForceRemove(bin_hogg_name);
		if (hogErrorResult != 0)
		{
			int removeErrno = errno;
			if (removeErrno != ENOENT)
				log_printf(LOG_ERRORS, "Cannot remove existing client cluster bin hog \"%s\"; errno = %d.\n", bin_hogg_name, removeErrno);
		}
		hogErrorResult = 0;
		dest_bin_hog_file = hogFileReadEx(bin_hogg_name, &hoggCreated, PIGERR_ASSERT, &hogErrorResult, HOG_MUST_BE_WRITABLE|HOG_NO_INTERNAL_TIMESTAMPS, 1024);
		assert(hoggCreated);

		// Write textures: move wtex from remesh cache over to the world cell hogg
		for (textureIndex = 0; textureIndex < CLUSTER_MAX_TEXTURES; ++textureIndex)
		{
			char texName[MAX_PATH];
			if ( textureIndex == 0 ||
				(clusterData->source_models->cluster_volumes_condensed->include_normal && textureIndex == CLUSTER_TEXTURE_NORMAL_INDEX) ||
				(clusterData->source_models->cluster_volumes_condensed->include_specular && textureIndex == CLUSTER_TEXTURE_SPECULAR_INDEX)
				)
			{
				worldCellClusterMakeTextureName(SAFESTR(texName), NULL, clusterData->clusterBaseName, "wtex", textureIndex);
				if (!hogFileCopyFileFromHogFile(dest_bin_hog_file, clusterState->remesh_model_hogg_file, texName,
					&clusterData->buildStats.wtexBinGeneration))
				{
					log_printf(LOG_ERRORS, "Missing remesh output texture \"%s\"; using the default texture \"%s\".\n", texName, ClusterTextureDefaults[textureIndex]);
					bWriteResultBinsSuccess = worldCellClusterAddFileToHogg(dest_bin_hog_file, 
						ClusterTextureDefaults[textureIndex], texName, &clusterData->buildStats.wtexBinGeneration);
				}
			}
		}

		taskStartTimer(&clusterData->buildStats.msetBinGeneration);
		sprintf(mset_header_name, "%s.mhdr", cell_cluster_name);
		sprintf(mset_name, "%s.mset", cell_cluster_name);

		// Open the header and model separately.
		headerBuf = SimpleBufOpenWrite(mset_header_name, true, dest_bin_hog_file, true, false);
		modelBuf  = SimpleBufOpenWrite(mset_name,  true, dest_bin_hog_file, true, false);

		// Write model
		modelAddGMeshToGLD(&gld, clusterData->remeshedClusterMesh, TERRAIN_WORLD_CLUSTER_MODEL_NAME, NULL, 0, true, false, !clusterData->source_models->cluster_volumes_condensed->include_normal, headerBuf);
		modelWriteAndFreeBinGLD(gld, modelBuf, false);

		taskAttributeWriteIO(&clusterData->buildStats.msetBinGeneration, SimpleBufGetSize(headerBuf) + SimpleBufGetSize(modelBuf));

		// Close everything.
		SimpleBufClose(headerBuf);
		SimpleBufClose(modelBuf);

		hogFileDestroy(dest_bin_hog_file, true);
		dest_bin_hog_file = NULL;
		// Please see the WorldCellBinning.c comment where the map snap system uses this function for
		// potentially important details. Clustering will not happen on UGC maps on end-user computers.
		// DJR - I disabled this until we can synchronize main thread vs task server comm thread.
		//hogDefrag(bin_hogg_name, 1024, HogDefrag_Tight);

		taskAttributeWriteIO(&clusterData->buildStats.clientClusterBinSize, fileSize(bin_hogg_name));

		clusterData->bWroteClientBins = true;
		taskStopTimer(&clusterData->buildStats.msetBinGeneration);
	}
	else
	{
		// If task distributed, this is normal and delete the hogg if it is present. Otherwise, errors must have been reported.
		if (excludedFromMapBin && fileExists(bin_hogg_name))
			fileForceRemove(bin_hogg_name);
	}

	worldCellClusterCleanupMesh(clusterData);

	return bWriteResultBinsSuccess;
}

GMesh * gmeshLoadFromHogFile(HogFile * handle, const char * filename)
{
	GMesh * mesh = NULL;
	SimpleBufHandle gmeshBufHandle = SimpleBufOpenRead(filename, handle);
	if (gmeshBufHandle)
	{
		mesh = gmeshFromBinData(gmeshBufHandle);
		SimpleBufClose(gmeshBufHandle);
	}
	return mesh;
}

static void modelClusterSourceAddSourceGMesh(ModelClusterSource * cluster, const Mat4 transform, GMesh * source_gmesh, const ModelClusterTextures * textures)
{
	ModelClusterObject * newGMeshObject = StructCreateDefault(ModelClusterObject);
	Vec3 meshBBoxMin = { 0 }, meshBBoxMax = { 0 }, meshBBoxMid = { 0 };
	ModelClusterTextures *mcTextures;

	if (!cluster->texture_table)
		cluster->texture_table = stashTableCreateWithStringKeys(64,StashDefault);

	mcTextures = StructCreateDefault(ModelClusterTextures);
	newGMeshObject->gmesh = source_gmesh;
	strcpy(mcTextures->gmesh_texture_file_d, textures->gmesh_texture_file_d);
	stashAddPointer(cluster->texture_table,mcTextures->gmesh_texture_file_d,mcTextures->gmesh_texture_file_d,false);	// store the textures to save them out later.
	strcpy(mcTextures->gmesh_texture_file_n, textures->gmesh_texture_file_n);
	stashAddPointer(cluster->texture_table,mcTextures->gmesh_texture_file_n,mcTextures->gmesh_texture_file_n,false);	// store the textures to save them out later.
	strcpy(mcTextures->gmesh_texture_file_s, textures->gmesh_texture_file_s);
	stashAddPointer(cluster->texture_table,mcTextures->gmesh_texture_file_s,mcTextures->gmesh_texture_file_s,false);	// store the textures to save them out later.
	eaPush(&newGMeshObject->gmesh_texture_files,mcTextures);


	if (!transform)
		transform = unitmat;
	mat43to44(transform, newGMeshObject->model_matrix);
	copyVec3(transform[3], newGMeshObject->position);
	newGMeshObject->radius = gmeshGetBounds(source_gmesh, meshBBoxMin, meshBBoxMax, meshBBoxMid);

	eaPush(&cluster->cluster_models, newGMeshObject);
}

static void recursiveGatherInput(
	ModelClusterSource * cluster, const WorldCell *cell, const WorldCell *childCell,
	const WorldRegion *region, HogFile * cluster_bins, bool gatherLeafLevel,
	S64 *timeStart, S64 *timeEnd, S64 *meshDelta, S64 *texDelta,
	U32 *textureSize, U32 *meshSize)
{
	char clusterBaseFilename[MAX_PATH];
	char clusterMeshFilename[MAX_PATH];
	GMesh * cluster_mesh = NULL;
	ModelClusterTextures cluster_source_tex_filenames;
	Mat4 transform;
	Vec3 cellCenter;
	ModelClusterVolume *mcv = cluster->cluster_volumes_condensed;
	bool normalActive = mcv->include_normal;
	bool specActive = mcv->include_specular;

	copyMat4(unitmat, transform);

	if (gatherLeafLevel && childCell->vis_dist_level > 0) {
		int i;
		for (i = 0; i < ARRAY_SIZE(childCell->children); i++) {
			if (childCell->children[i])
				recursiveGatherInput(cluster,cell,childCell->children[i],region,cluster_bins,gatherLeafLevel,timeStart,timeEnd,meshDelta,texDelta,textureSize,meshSize);
		}
	} else {
		worldCellGetClusterName(SAFESTR(clusterBaseFilename), childCell);
		sprintf(clusterMeshFilename, "%s.msh", clusterBaseFilename);
		sprintf(cluster_source_tex_filenames.gmesh_texture_file_d, "%s_D.png", clusterBaseFilename);

		sprintf(cluster_source_tex_filenames.gmesh_texture_file_n, "%s_N.png", clusterBaseFilename);

		sprintf(cluster_source_tex_filenames.gmesh_texture_file_s, "%s_S.png", clusterBaseFilename);

		GET_CPU_TICKS_64(*timeStart);
		cluster_mesh = gmeshLoadFromHogFile(cluster_bins, clusterMeshFilename);
		GET_CPU_TICKS_64(*timeEnd);
		if (cluster_mesh)
		{
			char image_filename[MAX_PATH];
			ModelClusterTextures image_fullpaths;
			char base_dir[MAX_PATH];
			const char* hoggName = hogFileGetArchiveFileName(cluster_bins);


			meshDelta += *timeEnd - *timeStart;
			gmeshRemoveAttributes(cluster_mesh, USE_COLORS | USE_BINORMALS | USE_TANGENTS);

			worldCellClusterGetTempDir(zmapGetFilename(region->zmap_parent), SAFESTR(base_dir));
			sprintf(image_filename, "%s/remesh_images/%s_%s", base_dir, (region->name?region->name:"Default"), cluster_source_tex_filenames.gmesh_texture_file_d);
			fileLocateWrite(image_filename, image_fullpaths.gmesh_texture_file_d);

			if (normalActive && hogFileFind(cluster_bins, cluster_source_tex_filenames.gmesh_texture_file_n) != HOG_INVALID_INDEX) {
				sprintf(image_filename, "%s/remesh_images/%s_%s", base_dir, (region->name?region->name:"Default"), cluster_source_tex_filenames.gmesh_texture_file_n);
				fileLocateWrite(image_filename, image_fullpaths.gmesh_texture_file_n);
			} else {
				normalActive = false;
				sprintf(cluster_source_tex_filenames.gmesh_texture_file_n, "%s.png", cluster_default_textures[CLUSTER_TEXTURE_NORMAL_INDEX]);
				sprintf(image_filename, "%s/remesh_images/%s", base_dir, cluster_source_tex_filenames.gmesh_texture_file_n);
				fileLocateWrite(image_filename, image_fullpaths.gmesh_texture_file_n);
			}

			if (specActive && hogFileFind(cluster_bins, cluster_source_tex_filenames.gmesh_texture_file_s) != HOG_INVALID_INDEX) {
				sprintf(image_filename, "%s/remesh_images/%s_%s", base_dir, (region->name?region->name:"Default"), cluster_source_tex_filenames.gmesh_texture_file_s);
				fileLocateWrite(image_filename, image_fullpaths.gmesh_texture_file_s);
			} else {
				specActive = false;
				sprintf(cluster_source_tex_filenames.gmesh_texture_file_s, "%s.png", cluster_default_textures[CLUSTER_TEXTURE_NORMAL_INDEX]);
				sprintf(image_filename, "%s/remesh_images/%s", base_dir, cluster_source_tex_filenames.gmesh_texture_file_s);
				fileLocateWrite(image_filename, image_fullpaths.gmesh_texture_file_s);
			}

			mkdirtree(image_fullpaths.gmesh_texture_file_d);
			GET_CPU_TICKS_64(*timeStart);
			*textureSize += pngDumpFromHogFile(cluster_bins, image_fullpaths.gmesh_texture_file_d, cluster_source_tex_filenames.gmesh_texture_file_d);
			if (normalActive)
				*textureSize += pngDumpFromHogFile(cluster_bins, image_fullpaths.gmesh_texture_file_n, cluster_source_tex_filenames.gmesh_texture_file_n);
			if (specActive)
				*textureSize += pngDumpFromHogFile(cluster_bins, image_fullpaths.gmesh_texture_file_s, cluster_source_tex_filenames.gmesh_texture_file_s);
			GET_CPU_TICKS_64(*timeEnd);

			*texDelta += *timeEnd - *timeStart;
			// translate clustered child cell to position it inside its cell, then subtract the parent cell's center,
			// because the cell will translate it during rendering
			worldCellGetBlockRangeCenter(childCell, transform[3]);
			worldCellGetBlockRangeCenter(cell, cellCenter);
			subVec3(transform[3], cellCenter, transform[3]);
			modelClusterSourceAddSourceGMesh(cluster, transform, cluster_mesh, &image_fullpaths);
			meshSize += gmeshGetBinSize(cluster_mesh);
		}
	}
}

// Test whether the volumes overlap.
static bool axisAlignedVolumesIntersect(const Vec3 vol_a_min, const Vec3 vol_a_max, const Vec3 vol_b_min, const Vec3 vol_b_max)
{
	if (	(vol_a_max[0] > vol_b_min[0]) && (vol_a_max[1] > vol_b_min[1]) && (vol_a_max[2] > vol_b_min[2]) &&
			(vol_a_min[0] < vol_b_max[0]) && (vol_a_min[1] < vol_b_max[1]) && (vol_a_min[2] < vol_b_max[2]))
		return true;
	return false;
}

void calcWorldCellBounds(const WorldCell *cell, Vec3 cell_min, Vec3 cell_max)
{
	int i;

	for (i = 0; i < 3; i++) {
		cell_min[i] = cell->cell_block_range.min_block[i] * CELL_BLOCK_SIZE;
		cell_max[i] = (cell->cell_block_range.max_block[i] + 1) * CELL_BLOCK_SIZE;
	}
}

static void modelClusterCalcBounds(const WorldCell *cell, Vec3 dest_min, Vec3 dest_max)
{
	calcWorldCellBounds(cell, dest_min, dest_max);
}

void modelClusterProcessVolumes(ModelClusterSource *cluster, WorldClusterState *cluster_options, const WorldCell *cell)
{
	int i;

	if (!cluster || (!cluster->cluster_models && cell->vis_dist_level == 0))
		return;	// empty cell as far as clustering is concerned

	for (i = 0; i < eaSize(&cluster_options->cluster_volumes); i++) {
		Vec3 worldMin,worldMax;
		ModelClusterVolume *mcVolume = cluster_options->cluster_volumes[i];

		if (mcVolume->target_lod_level > cell->vis_dist_level)
			continue;

		modelClusterCalcBounds(cell, worldMin, worldMax);

		if (axisAlignedVolumesIntersect(worldMin, worldMax,	mcVolume->volume_min, mcVolume->volume_max)) {
			ModelClusterVolume *cluster_volume = cluster->cluster_volumes_condensed;
			if (mcVolume->target_lod_level > cluster_volume->target_lod_level) {	// Higher target lods should supersede anything below them, so just copy data over if we've been working with lower target lod data.
				// Due to the nature of how these values are for pruning cluster cells, lod_min and lod_max are only valid if the target_lod_level is 0.
				int lod_min = cluster_volume->lod_min;
				int lod_max = cluster_volume->lod_max;
				StructCopy(parse_ModelClusterVolume,mcVolume,cluster_volume,0,0,0);
				cluster_volume->lod_min = lod_min;
				cluster_volume->lod_max = lod_max;
			} else if (mcVolume->target_lod_level == cluster_volume->target_lod_level) {	// If same target lod level, take the higher quality settings.
				if (mcVolume->geo_resolution > cluster_volume->geo_resolution)
					cluster_volume->geo_resolution = mcVolume->geo_resolution;

				if (mcVolume->texture_height * mcVolume->texture_width > cluster_volume->texture_height * cluster_volume->texture_width) {
					cluster_volume->texture_height = mcVolume->texture_height;
					cluster_volume->texture_width = mcVolume->texture_width;
					cluster_volume->texture_super_sample = mcVolume->texture_super_sample;
				} else if (mcVolume->texture_height * mcVolume->texture_width == cluster_volume->texture_height * cluster_volume->texture_width) {
					if (mcVolume->texture_super_sample > cluster_volume->texture_super_sample) {
						cluster_volume->texture_super_sample = mcVolume->texture_super_sample;
					}
				}

				cluster_volume->include_normal |= mcVolume->include_normal;
				cluster_volume->include_specular |= mcVolume->include_specular;
			}
			if (mcVolume->target_lod_level == 0){
				// Due to the nature of how these values are for pruning cluster cells, lod_min and lod_max are only valid if the target_lod_level is 0.
				cluster_volume->lod_min = (mcVolume->lod_min < cluster_volume->lod_min ? mcVolume->lod_min : cluster_volume->lod_min);

				// Decisions are made here by allowing a greater number of lod levels concerning conflicting volumes.
				cluster_volume->lod_max = (mcVolume->lod_max > cluster_volume->lod_max ? mcVolume->lod_max : cluster_volume->lod_max);
			}
		}
	}
}

void gatherInputClusters(ModelClusterSource * cluster, const WorldRegion *region, const WorldCell * cell, WorldClusterState *cluster_options, HogFile * cluster_bins)
{
	int j;
	S64 timeStart,timeEnd,meshDelta = 0,texDelta = 0;
	U32 textureSize = 0, meshSize = 0;

	for (j = 0; j < ARRAY_SIZE(cell->children); ++j)
	{
		const WorldCell *childCell = cell->children[j];
		if (childCell)
		{
			recursiveGatherInput(cluster,cell,childCell,region,cluster_bins,cluster_options->gatherFromLeafLevel,&timeStart,&timeEnd,&meshDelta,&texDelta,&textureSize,&meshSize);
		}
	}
	log_printf(LOG_SIMPLYGON,"\t\tReading gmeshes from hog: %f milliseconds\n",timerSeconds64(meshDelta));
	log_printf(LOG_SIMPLYGON,"\t\tReading textures from hog: %f milliseconds\n",timerSeconds64(texDelta));
	log_printf(LOG_SIMPLYGON,"\t\tTotal texture size for cluster: %d bytes\n",textureSize);
	log_printf(LOG_SIMPLYGON,"\t\tTotal mesh size for cluster: %d bytes\n",meshSize);
}


//---------------------------------------------------------------------------------------------------

bool modelClusterIsEmpty(const ModelClusterSource * newClusterSource)
{
	return eaSize(&newClusterSource->cluster_models) == 0;
}

bool checkClusterDataUpToDate(const ModelClusterSource * original, const ModelClusterSource * new_cluster)
{
	return StructCompare(parse_ModelClusterSource, original, new_cluster, COMPAREFLAG_EMPTY_STRINGS_MATCH_NULL_STRINGS, 0, 0) == 0;
}

static U32 hogFileGetFileTimestampNamed(HogFile * handle, const char * hogg_relpath)
{
	U32 timestamp = 0;
	int hogFileIndex = hogFileFind(handle, hogg_relpath);
	if (hogFileIndex != HOG_INVALID_INDEX)
		timestamp = hogFileGetFileTimestamp(handle, hogFileIndex);
	return timestamp;
}

bool checkClusterDependenciesUpToDate(ModelClusterSource * source, const char *clusterBaseFilename, HogFile * cluster_bins, WorldRegion *region, int vis_dist_level)
{
	char cluster_source_filename_text[MAX_PATH];
	ModelClusterSource original_source = { 0 };
	SimpleBufHandle cluster_buf_handle = NULL;
	bool upToDate = false;
	HogFileIndex source_text_hog_index = HOG_INVALID_INDEX;

	sprintf(cluster_source_filename_text, "%s.clustersource", clusterBaseFilename);

	source_text_hog_index = hogFileFind(cluster_bins, cluster_source_filename_text);
	if (source_text_hog_index != HOG_INVALID_INDEX)
	{
		ParserReadTextFileFromHogg(
			cluster_source_filename_text,
			parse_ModelClusterSource,
			&original_source,
			cluster_bins);

		upToDate = checkClusterDataUpToDate(&original_source, source);
	}

	if (!upToDate)
	{
		if (!vis_dist_level && modelClusterIsEmpty(source))
		{
			char mesh_filename[MAX_PATH];

			if (source_text_hog_index != HOG_INVALID_INDEX)
				hogFileModifyDeleteNamed(cluster_bins, cluster_source_filename_text);

			// SIMPLYGON TODO: delete all cluster files for the missing cluster, not just the mesh
			sprintf(mesh_filename,"%s.Msh", clusterBaseFilename);	// This check only take place on lod 0
			if ((source_text_hog_index = hogFileFind(cluster_bins,mesh_filename)) != HOG_INVALID_INDEX)
				hogFileModifyDeleteNamed(cluster_bins,mesh_filename);
		}
		else
		{
#define PARSERWRITE_DEFAULT 0
			ParserWriteTextFileToHogg(cluster_source_filename_text, parse_ModelClusterSource, source, cluster_bins);
		}
	}
	return upToDate;
}

#define INT_DIG 10
#define INT_MAX_CHAR 11

bool checkClusterMeshDependenciesUpToDate(worldCellClusterGenerateCellClusterData *clusterData, ModelClusterSource * cluster_source, const char * remesh_file, const char *clusterBaseFilename, HogFile * cluster_bins)
{
	char cluster_source_filename[MAX_PATH];
	bool upToDate = true;
	int textureIndex = 0;
	U32 cluster_source_time;
	U32 cluster_output_mesh_time = 0;
	U32 cluster_output_png_times[CLUSTER_MAX_TEXTURES];
	U32 cluster_output_wtex_times[CLUSTER_MAX_TEXTURES];
	char outputTimestamps[(INT_MAX_CHAR + 5) * (CLUSTER_MAX_TEXTURES * 2 + 1)];
	char updateReason[128];

	sprintf(cluster_source_filename, "%s.clustersource", clusterBaseFilename);
	cluster_source_time = hogFileGetFileTimestampNamed(cluster_bins, cluster_source_filename);

	strcpy(updateReason, "");
	strcpy(outputTimestamps, "");
	for (textureIndex = 0; textureIndex < CLUSTER_MAX_TEXTURES; ++textureIndex)
	{
		char textureName[MAX_PATH];

		if ((textureIndex == CLUSTER_TEXTURE_SPECULAR_INDEX && (!cluster_source->cluster_volumes_condensed->include_specular)) ||
			(textureIndex == CLUSTER_TEXTURE_NORMAL_INDEX && (!cluster_source->cluster_volumes_condensed->include_normal)))
			continue;
		worldCellClusterMakeTextureName(SAFESTR(textureName), NULL, clusterBaseFilename, "png", textureIndex);
		cluster_output_png_times[textureIndex] = hogFileGetFileTimestampNamed(cluster_bins, textureName);

		worldCellClusterMakeTextureName(SAFESTR(textureName), NULL, clusterBaseFilename, "wtex", textureIndex);
		cluster_output_wtex_times[textureIndex] = hogFileGetFileTimestampNamed(cluster_bins, textureName);

		upToDate = upToDate && cluster_output_png_times[textureIndex] >= cluster_source_time &&
			cluster_output_wtex_times[textureIndex] >= cluster_source_time;

		strcatf(outputTimestamps,"%d %d ", cluster_output_png_times[textureIndex], cluster_output_wtex_times[textureIndex]);
	}
	if (!upToDate)
		strcatf(updateReason, "output texture(s) older than source");

	cluster_output_mesh_time = hogFileGetFileTimestampNamed(cluster_bins, remesh_file);
	strcatf(outputTimestamps,"%d", cluster_output_mesh_time);
	upToDate = upToDate && (cluster_output_mesh_time >= cluster_source_time && cluster_source_time && cluster_output_mesh_time);
	if (upToDate)
	{
		loadupdate_printf("Cluster mesh \"%s\" up to date (output ts %s input ts %d); reusing current remesh of \"%s\"\n", 
			remesh_file, outputTimestamps, cluster_source_time, cluster_source_filename);
	}
	else
	{
		SimpleBufHandle cluster_buf_handle = NULL;
		HogFileIndex source_bin_hog_index = HOG_INVALID_INDEX;

		const char * out_of_date_reason = "";
		if (cluster_output_mesh_time < cluster_source_time)
			out_of_date_reason = "cluster mesh ts not newer or same as input ts";
		else
		if (!cluster_source_time && !cluster_output_mesh_time)
			out_of_date_reason = "no timestamp on input or mesh output";
		else
		if (!cluster_source_time)
			out_of_date_reason = "no timestamp on input";
		else
		if (!strlen(updateReason))
			out_of_date_reason = "unknown timestamp state, please debug";
		strcatf(updateReason, "%s%s", strlen(updateReason) && out_of_date_reason ? ", and " : "", out_of_date_reason);
		loadupdate_printf("Cluster mesh \"%s\" out of date because %s (output ts %s input ts %d); loading source \"%s\" and remeshing\n", 
			remesh_file, updateReason, outputTimestamps, cluster_source_time, cluster_source_filename);

		source_bin_hog_index = hogFileFind(cluster_bins, cluster_source_filename);
		if (source_bin_hog_index != HOG_INVALID_INDEX)
		{
			ParserReadTextFileFromHogg(
				cluster_source_filename,
				parse_ModelClusterSource,
				cluster_source,
				cluster_bins);
		}
	}

	return upToDate;
}

typedef struct ClusterLODDependencies
{
	U32 childTimestamp[8];
} ClusterLODDependencies;

extern ParseTable parse_ClusterLODDependencies[];

void loadClusterLODDependencies(const WorldCell * cell, ClusterLODDependencies * deps, HogFile * cluster_bins)
{
	char clusterBaseFilename[MAX_PATH];
	char clusterMeshFilename[MAX_PATH];
	int j;

	for (j = 0; j < ARRAY_SIZE(cell->children); ++j)
	{
		const WorldCell *childCell = cell->children[j];

		deps->childTimestamp[j] = 0;

		if (!childCell)
			continue;

		worldCellGetClusterName(SAFESTR(clusterBaseFilename), cell->children[j]);
		sprintf(clusterMeshFilename, "%s.msh", clusterBaseFilename);

		deps->childTimestamp[j] = hogFileGetFileTimestampNamed(cluster_bins, clusterMeshFilename);
	}
}

bool checkOutputTimestampUpToDate(U32 outputTimestamp, const ClusterLODDependencies * childDeps)
{
	int i;
	bool bAnyValidDepTimestamp = false;
	for (i = 0; i < ARRAY_SIZE(childDeps->childTimestamp); ++i)
	{
		if (outputTimestamp < childDeps->childTimestamp[i])
			return false;
		if (childDeps->childTimestamp[i])
			bAnyValidDepTimestamp = true;
	}
	return bAnyValidDepTimestamp;
}

bool checkClusterLODDependenciesUpToDate(const WorldCell *cell_src, const char *clusterBaseName, HogFile * cluster_bins)
{
	char clusterBaseFilename[MAX_PATH];
	U32 output_timestamp;
	ClusterLODDependencies source_deps = { 0 };

	// find timestamp of child clusters, current parent cluster must have same or newer timestamp
	loadClusterLODDependencies(cell_src, &source_deps, cluster_bins);

	// get timestamp of output (parent) cluster mesh
	worldCellGetClusterName(SAFESTR(clusterBaseFilename), cell_src);
	strcat(clusterBaseFilename, ".msh");

	output_timestamp = hogFileGetFileTimestampNamed(cluster_bins, clusterBaseFilename);

	return checkOutputTimestampUpToDate(output_timestamp, &source_deps);
}

void transferTextureFromRegionHoggToModelClusterDir(HogFile *regionHogg, HogFile *clusterHogg, const char* texture_file, const char* tempDir)
{
	int k = 0, sleepCounter = 0;
	char dest_filename[MAX_PATH];
	char just_file_name[MAX_PATH];
	char *region_cropped_file_name;

	fileGetFilename(texture_file,just_file_name);
	while (just_file_name[k] != '_')
		k++;
	region_cropped_file_name = &just_file_name[k+1];
	sprintf(dest_filename,"%s/%s",tempDir,just_file_name);
	pngDumpFromHogFile(regionHogg,dest_filename,region_cropped_file_name);
}

bool worldCellClusterAddFileToHogg(HogFile *destHogFile, const char *file_name, const char *file_hogg_name, TaskProfile *operationStats)
{
	int file_size;
	U8* file_data = NULL;

	taskStartTimer(operationStats);
	file_data = fileAlloc(file_name, &file_size);

	assert(file_data);
	taskAttributeReadIO(operationStats, file_size);

	hogFileModifyUpdateNamed(destHogFile, file_hogg_name, file_data, file_size, 0, NULL);	// do not free file_data since this function frees it for us when done processing.
	taskAttributeWriteIO(operationStats, file_size);
	taskStopTimer(operationStats);
	return file_data != NULL;
}

bool worldCellClusterWriteGMeshBinToHogg(HogFile *destHogFile, const GMesh *gmesh, const char *gmeshHogFileName, TaskProfile *operationStats)
{
	SimpleBufHandle meshBuf = NULL;

	taskStartTimer(operationStats);

	meshBuf = SimpleBufOpenWrite(gmeshHogFileName, true, destHogFile, false, false);

	if (meshBuf)
	{
		gmeshWriteBinData(gmesh, meshBuf);
		taskAttributeWriteIO(operationStats, SimpleBufGetSize(meshBuf));
	}

	SimpleBufClose(meshBuf);

	taskStopTimer(operationStats);
	return meshBuf != NULL;
}

bool modelClusterObjectProcessModelGeo(ModelClusterObject *modelObject, ModelLOD *lod, const char *modelName, const char *tempDir, ModelHeader *mh, HogFile *clusterHogg,
	worldCellClusterGenerateCellClusterData *clusterData)
{
	int j;
	FileList fileList = {0};
	Geo2LoadData *gld = geo2LoadModelFromSource(mh, &fileList);
	bool bProcessModelGeoSuccess = true;

	for (j = 0; j < eaSize(&gld->models); j++) {

		if (stricmp(gld->models[j]->name, modelName) == 0) {
			char modelBufName[MAX_PATH];
			GMesh *gmesh;

			gmesh = calloc(1,sizeof(GMesh));

			taskStartTimer(&clusterData->buildStats.modelExport);
			sourceModelToGMesh(gmesh, gld->models[j], USE_COLORS);
			modelClusterAddGeoStats(&clusterData->buildStats.inputGeo, gmesh);

			// Add tangent space data if the model doesn't already have it.
			if (!(gmesh->usagebits & (USE_BINORMALS | USE_TANGENTS))) {
				gmeshAddTangentSpace(
					gmesh, false,
					gld->filename,
					LOG_ERRORS);
			}
			taskStopTimer(&clusterData->buildStats.modelExport);

			if (clusterHogg) {
				bool bWriteGMeshSuccess = false;
				sprintf(modelBufName, "%s.msh", modelName);
				bWriteGMeshSuccess = worldCellClusterWriteGMeshBinToHogg(clusterHogg, gmesh, modelBufName, &clusterData->buildStats.modelExport);
				gmeshFree(gmesh);
				gmesh = NULL;
				if (!bWriteGMeshSuccess)
				{
					bProcessModelGeoSuccess = false;
					break;
				}
			}
			else
			{
				modelObject->gmesh = gmesh;
			}

			modelObject->mesh_filename = allocAddString(modelBufName);
			break;
		}
	}
	geo2Destroy(gld);

	return bProcessModelGeoSuccess;
}
 
void modelClusterObjectProcessModelMaterial(ModelClusterSource *modelSource, U32 object_index, ModelLOD *lod, const char* tempDir,
	WorldClusterStats *buildStats)
{
	Material **eaMaterialList = NULL;
	int j;

	// If there's no input material list or it's incomplete, fill in the rest with defaults.
	for(j = 0; j < lod->data->tex_count; j++) {
		eaPush(&eaMaterialList, lod->materials[j]);
	}

	buildStats->inputGeo.numMaterials += lod->data->tex_count;

	if (eaMaterialList) {

		// If the number of materials on the model doesn't match the number of material inputs, then we have a
		// problem.
		assert(eaSize(&eaMaterialList) == lod->data->tex_count);

		for (j = 0; j < eaSize(&eaMaterialList); j++) {
			if (!modelSource->texture_table)
				modelSource->texture_table = stashTableCreateWithStringKeys(64,StashDefault);
			wl_state.get_material_cluster_textures_from_material(
				eaMaterialList[j],
				modelSource->cluster_models[object_index]->tex_swaps,
				modelSource->cluster_models[object_index]->mat_swaps,
				tempDir,
				&modelSource->cluster_models[object_index]->gmesh_texture_files,
				modelSource->texture_table,
				&buildStats->pngExport,
				&buildStats->inputGeo);
		}
	}
	eaDestroy(&eaMaterialList);	// Do not need to clear or destroy the contents of the list
}

static void modelClusterObjectConvertModelToGMesh(ModelClusterSource *modelSource, U32 object_index, 
	const char * modelName, const char * tempDir, HogFile *clusterHogg,
	worldCellClusterGenerateCellClusterData *clusterData)
{
	ModelHeader *mh;
	REF_TO(ModelHeader) hInfo = {0};
	ModelLOD *lod0;
	Model *model;

	model = modelFind(modelSource->cluster_models[object_index]->model_filename,false,WL_FOR_WORLD);
	lod0 = modelLODLoadAndMaybeWait(model, 0, true);

	SET_HANDLE_FROM_STRING("ModelHeader", modelName, hInfo);

	if(REF_IS_VALID(hInfo)) {
		mh = GET_REF(hInfo);
		if (mh) {
			bool bConvertToGMeshSuccess = false;
			if(lod0) {
				modelClusterObjectProcessModelMaterial(modelSource, object_index, lod0, tempDir, &clusterData->buildStats);
			}
			else
				Errorf("Converting source model \"%s\" for remesh failed; couldn't access LOD 0 for materials.", modelName);

			bConvertToGMeshSuccess = modelClusterObjectProcessModelGeo(modelSource->cluster_models[object_index], lod0, modelName, tempDir, mh, clusterHogg, clusterData);
			if (!bConvertToGMeshSuccess)
				Errorf("Converting source model \"%s\" for remesh failed; possibly storing GMesh failed.", modelName);
		}
		else
			Errorf("Converting source model \"%s\" for remesh failed; couldn't retrieve header.", modelName);
	}
	else
		Errorf("Converting source model \"%s\" for remesh failed; couldn't find reference handle.", modelName);
	REMOVE_HANDLE(hInfo);
}

void modelClusterAddGeoStats(WorldClusterGeoStats *geoStats, GMesh *mesh)
{
	++geoStats->numMeshes;
	geoStats->numTriangles += mesh->tri_count;
	geoStats->numVertices += mesh->vert_count;
	geoStats->numMeshBytes += gmeshGetBinSize(mesh);
}

void modelClusterSourceHoggCreate(worldCellClusterGenerateCellClusterData *clusterData, ModelClusterSource *newClusterSource, HogFile* regionHogg, WorldRegion *region, char * hoggFileName, int hoggFileName_size, bool cleanDirectory, int lod)
{
	int i;
	char tempDir[MAX_PATH];
	char base_dir[MAX_PATH];
	HogFile *clusterHogg = NULL;
	bool hoggCreated = false;
	int hoggError;

	// Following block is for taking of more global things such as creation and clean up of the hogg file for the cluster.
	{
		int dirResult;

		worldCellClusterGetTempDir(zmapGetFilename(region->zmap_parent), SAFESTR(base_dir));

		sprintf(tempDir, "%s/%s.hogg", base_dir, clusterData->clusterBaseName);
		fileLocateWrite(tempDir, clusterData->taskParameterHoggFile);
		strcpy_s(SAFESTR2(hoggFileName), clusterData->taskParameterHoggFile);
		clusterHogg = hogFileRead(hoggFileName, &hoggCreated, PIGERR_ASSERT, &hoggError, HOG_MUST_BE_WRITABLE);

		if (!hoggCreated)
			hogDeleteAllFiles(clusterHogg);	// Wipe clusterHogg contents.

		sprintf(tempDir, "%s/%s", base_dir, clusterData->clusterBaseName);
		fileLocateWrite(tempDir, tempDir);
		dirResult = mkdir(tempDir);
	}

	for (i = 0; i < eaSize(&newClusterSource->cluster_models); i++)
	{
		ModelClusterObject *modelObject = newClusterSource->cluster_models[i];

		if (modelObject->gmesh) {
			char modelBufName[MAX_PATH];
			int j;
			bool bWriteGMeshSuccess = false;

			sprintf(modelBufName,"model_%d.msh",i);
			bWriteGMeshSuccess = worldCellClusterWriteGMeshBinToHogg(clusterHogg, modelObject->gmesh, modelBufName, &clusterData->buildStats.modelExport);
			modelObject->mesh_filename = allocAddString(modelBufName);

			modelClusterAddGeoStats(&clusterData->buildStats.inputGeo, modelObject->gmesh);

			// Handle textures here.
			for (j = 0; j < eaSize(&modelObject->gmesh_texture_files); j++) {
				ModelClusterTextures *mct = modelObject->gmesh_texture_files[j];

				if (mct->gmesh_texture_file_d[0]) {
					transferTextureFromRegionHoggToModelClusterDir(regionHogg, clusterHogg, mct->gmesh_texture_file_d, tempDir);
				}
				if (mct->gmesh_texture_file_s[0]) {
					transferTextureFromRegionHoggToModelClusterDir(regionHogg, clusterHogg, mct->gmesh_texture_file_s, tempDir);
				}
				if (mct->gmesh_texture_file_n[0]) {
					transferTextureFromRegionHoggToModelClusterDir(regionHogg, clusterHogg, mct->gmesh_texture_file_n, tempDir);
				}
				clusterData->buildStats.inputGeo.numTextures += CLUSTER_MAX_TEXTURES;
			}
		} else {
			modelClusterObjectConvertModelToGMesh(newClusterSource, i, modelObject->model_filename, tempDir, clusterHogg, clusterData);
		}
	}

	if (!modelClusterSourceExportTextures(newClusterSource, clusterData->clusterState, clusterHogg, &clusterData->buildStats.pngExport))
	{
		Errorf("Exporting model textures for \"%s\" failed.", clusterData->clusterBaseName);
	}

	ParserWriteTextFileToHogg(
		"Cluster_Source.mcst", parse_ModelClusterSource,
		newClusterSource, clusterHogg);

	hogFileDestroy(clusterHogg,true);

	if (cleanDirectory)
		rmdirtreeEx(tempDir,false);
}

RemeshAssetSwap *createRemeshTextureSwap(const char *filename, const char *orig_name, const char *replace_name)
{
	RemeshAssetSwap *dts = StructCreateDefault(RemeshAssetSwap);
	// the original names must be kept around so that they will be saved even if not found
	dts->orig_name = allocAddString(orig_name);
	dts->replace_name = allocAddString(replace_name);
	return dts;
}

bool hogFileCopyFileFromHogFile(HogFile *destHogFile, HogFile *sourceHogFile, const char *fileName, TaskProfile *copyOperation)
{
	bool bCopySuccess = false;
	SimpleBufHandle fileDataBufHandle = NULL;
	U32 ouputFileSize = 0;

	taskStartTimer(copyOperation);
	fileDataBufHandle = SimpleBufOpenRead(fileName, sourceHogFile);
	if (fileDataBufHandle)
	{
		int fileLength = 0;
		void *fileData = SimpleBufGetDataAndClose(fileDataBufHandle, &fileLength);
		taskAttributeReadIO(copyOperation, fileLength);
		taskAttributeWriteIO(copyOperation, fileLength);

		if (!hogFileModifyUpdateNamed(destHogFile, fileName, fileData, fileLength, _time32(NULL), NULL))
			bCopySuccess = true;
	}
	taskStopTimer(copyOperation);

	return bCopySuccess;
}

bool clusterStatsLoadFromHogFile(HogFile * handle, const char * filename, WorldClusterStats * stats)
{
	SimpleBufHandle statBufHandle = ParserOpenBinaryFile(handle, filename, NULL, BINARYREADFLAG_IGNORE_CRC, NULL);
	if (statBufHandle)
	{
		ParserReadBinaryFile(statBufHandle, parse_WorldClusterStats, stats, NULL, NULL, NULL, NULL, 0, 0, true);
	}
	return statBufHandle != NULL;
}

void clusterStatsMerge(WorldClusterStats *destStats, WorldClusterStats *additionalStats)
{
	shDoOperation(STRUCTOP_ADD, parse_TaskProfile, &destStats->netSend, &additionalStats->netSend);
	shDoOperation(STRUCTOP_ADD, parse_TaskProfile, &destStats->netRecv, &additionalStats->netRecv);
	shDoOperation(STRUCTOP_ADD, parse_TaskProfile, &destStats->wtexDecompress, &additionalStats->wtexDecompress);
	shDoOperation(STRUCTOP_ADD, parse_TaskProfile, &destStats->modelExport, &additionalStats->modelExport);
	shDoOperation(STRUCTOP_ADD, parse_TaskProfile, &destStats->pngExport, &additionalStats->pngExport);
	shDoOperation(STRUCTOP_ADD, parse_TaskProfile, &destStats->convertGeo, &additionalStats->convertGeo);
	shDoOperation(STRUCTOP_ADD, parse_TaskProfile, &destStats->remeshGeo, &additionalStats->remeshGeo);
	FOR_EACH_IN_EARRAY_FORWARDS(destStats->eaCastTextures, TaskProfile, texStats);
	{
		TaskProfile *mergeTexStat = eaGet(&additionalStats->eaCastTextures, itexStatsIndex);
		if (mergeTexStat)
			shDoOperation(STRUCTOP_ADD, parse_TaskProfile, texStats, mergeTexStat);
	}
	FOR_EACH_END;
	shDoOperation(STRUCTOP_ADD, parse_TaskProfile, &destStats->wtexCompress, &additionalStats->wtexCompress);
	shDoOperation(STRUCTOP_ADD, parse_TaskProfile, &destStats->cacheGeo, &additionalStats->cacheGeo);
	shDoOperation(STRUCTOP_ADD, parse_TaskProfile, &destStats->cacheTex, &additionalStats->cacheTex);
	shDoOperation(STRUCTOP_ADD, parse_TaskProfile, &destStats->msetBinGeneration, &additionalStats->msetBinGeneration);
	shDoOperation(STRUCTOP_ADD, parse_TaskProfile, &destStats->wtexBinGeneration, &additionalStats->wtexBinGeneration);
	shDoOperation(STRUCTOP_ADD, parse_TaskProfile, &destStats->inputGeo, &additionalStats->inputGeo);
	shDoOperation(STRUCTOP_ADD, parse_TaskProfile, &destStats->outputGeo, &additionalStats->outputGeo);
	destStats->numBuilds += additionalStats->numBuilds;
	destStats->numReuse += additionalStats->numReuse;
}

void cbTaskServerRemeshTaskCompleted(TaskServerRequestStatus status, TaskClientTaskPacket * task, SpawnRequestData *response, void *userData)
{
	worldCellClusterGenerateCellClusterData * clusterData = (worldCellClusterGenerateCellClusterData*)userData;
	bool retryRemesh = false;
	printf( "%s\n", status == TASKSERVER_NOT_RUNNING ? "Server not running" : "Got response");
	if (response && response->label)
		printf( "%s\n", response->label);

	if (!worldCellClusterQueueEntryChangeState(clusterData, CRS_ProcessingDistributedResult, CRS_RemeshingDistributed))
	{
		printf( "Task already failed or canceled on main thread; ignoring result for cluster %s\n", clusterData->clusterBaseName);
		return;
	}

	InterlockedDecrement(&clusterData->clusterState->numDistributedRemeshes);

	if (!response)
	{
		loadupdate_printf("Remesh of \"%s\" was ongoing when taskServer disconnected.\n", clusterData->clusterBaseName);
		log_printf(LOG_SIMPLYGON, "Remesh of \"%s\" was ongoing when taskServer disconnected.\n", clusterData->clusterBaseName);

		++clusterData->buildStats.numDisconnects;

		// tell task server infrastructure to leave the bad input
		taskHeaderLeaveResultAttachmentFile(task);
	}
	else if ((int)response->remeshSystemExitCode == (int)-1)
	{
		char paramFileFullPath[MAX_PATH];
		char paramFileBackupFullPath[MAX_PATH];
		loadupdate_printf("Remesh of \"%s\" crashed or was otherwise terminated by CrypticError.\n", clusterData->clusterBaseName);
		log_printf(LOG_SIMPLYGON, "Remesh of \"%s\" crashed or was otherwise terminated by CrypticError.\n", clusterData->clusterBaseName);

		++clusterData->buildStats.numCrashes;

		// back up task parameter file
		fileLocateWrite(clusterData->taskParameterHoggFile, paramFileFullPath);
		sprintf(paramFileBackupFullPath, "%s/remesh_crashes/%s", fileTempDir(), fileGetFilenameSubstrPtr(clusterData->taskParameterHoggFile));
		fileLocateWrite(paramFileBackupFullPath, paramFileBackupFullPath);
		mkdirtree(paramFileBackupFullPath);
		fileCopy(paramFileFullPath, paramFileBackupFullPath);

		// tell task server infrastructure to leave the bad input
		taskHeaderLeaveResultAttachmentFile(task);
	}

	if (task->fileAttachmentFull[0])
	{
		const char *parseString = ParserGetTableName(parse_RemeshMaterial);
		S64 timeTexStart,timeTexEnd;
		WorldClusterStats distributedRemeshStats = { 0 };

		RemeshMaterial * remeshMaterial = clusterData->remeshClusterMaterial;
		WorldClusterState *cluster_options = clusterData->clusterState;
		HogFile * remesh_result_bins = NULL;
		HogFile * cluster_bins = cluster_options->remesh_model_hogg_file;
		GMesh * mesh = NULL;

		remesh_result_bins = hogFileRead(task->fileAttachmentFull, NULL, PIGERR_ASSERT, NULL, HOG_READONLY);
		mesh = clusterData->remeshedClusterMesh = gmeshLoadFromHogFile(remesh_result_bins, clusterData->remeshGMeshName);

		if (!clusterStatsLoadFromHogFile(remesh_result_bins, "buildStats.bin", &distributedRemeshStats))
			loadupdate_printf("Remesh of \"%s\" failed to include stats.\n", clusterData->clusterBaseName);
		else
			clusterStatsMerge(&clusterData->buildStats, &distributedRemeshStats);

		shDoOperation(STRUCTOP_ADD, parse_TaskProfile, &clusterData->buildStats.netSend, &task->sendStats);
		shDoOperation(STRUCTOP_ADD, parse_TaskProfile, &clusterData->buildStats.netRecv, &task->recvStats);

		if (!mesh)
		{
			loadupdate_printf("Remesh of \"%s\" failed due to result GMesh missing.\n", clusterData->clusterBaseName);
			retryRemesh = true;
		}
		else
		{
			bool bWriteGMeshSuccess = false;
			mesh->parse_table = parseString;
			remeshMaterial->simplygon_material = allocAddString("Cluster_Default");
			mesh->parse_struct_data = remeshMaterial;
			mesh->usagebits |= USE_OPTIONAL_PARSE_STRUCT;

			assert(wl_state.calculate_vertex_lighting_for_gmesh);
			wl_state.calculate_vertex_lighting_for_gmesh(mesh);

			bWriteGMeshSuccess = worldCellClusterWriteGMeshBinToHogg(cluster_bins, mesh, clusterData->remeshGMeshName, 
				&clusterData->buildStats.cacheGeo);
			if (!bWriteGMeshSuccess)
			{
				loadupdate_printf("Remesh of \"%s\" failed to store remesh in cache.\n", clusterData->clusterBaseName);
				retryRemesh = true;
			}

			// move png and wtex files into the remesh cache
			GET_CPU_TICKS_64(timeTexStart);
			{
				int textureIndex;
				for (textureIndex = 0; textureIndex < CLUSTER_MAX_TEXTURES; ++textureIndex)
				{
					char texName[MAX_PATH];

					if ((textureIndex == CLUSTER_TEXTURE_SPECULAR_INDEX && !clusterData->source_models->cluster_volumes_condensed->include_specular) ||
						(textureIndex == CLUSTER_TEXTURE_NORMAL_INDEX && !clusterData->source_models->cluster_volumes_condensed->include_normal))
					{
						continue;
					}
					worldCellClusterMakeTextureName(SAFESTR(texName), NULL, clusterData->clusterBaseName, "png", textureIndex);
					if (!hogFileCopyFileFromHogFile(cluster_bins, remesh_result_bins, texName, &clusterData->buildStats.cacheTex))
					{
						retryRemesh = true;
						loadupdate_printf("Remesh of \"%s\" failed due to missing result PNG \"%s\".\n", clusterData->clusterBaseName, texName);
					}

					worldCellClusterMakeTextureName(SAFESTR(texName), NULL, clusterData->clusterBaseName, "wtex", textureIndex);
					if (!hogFileCopyFileFromHogFile(cluster_bins, remesh_result_bins, texName, &clusterData->buildStats.cacheTex))
					{
						retryRemesh = true;
						loadupdate_printf("Remesh of \"%s\" failed due to missing result WTEX \"%s\".\n", clusterData->clusterBaseName, texName);
					}
				}
				modelClusterAddGeoStats(&clusterData->buildStats.outputGeo, mesh);
				++clusterData->buildStats.outputGeo.numMaterials;
				clusterData->buildStats.outputGeo.numTextures += CLUSTER_MAX_TEXTURES;
			}
			GET_CPU_TICKS_64(timeTexEnd);
			log_printf(LOG_SIMPLYGON, "\t\tCreated wtex files for %s and moved to hogg: %f milliseconds\n", clusterData->clusterBaseName, timerSeconds64(timeTexEnd - timeTexStart));

			worldCellClusterWriteResultsToMapBins(clusterData);

			// clean out the cluster hoggs from the tempbin directory.
			remove(	clusterData->clusterBaseName );
		}

		hogFileDestroy(remesh_result_bins, true);
		remesh_result_bins = NULL;
	}
	else
	{
		retryRemesh = true;
		loadupdate_printf("Remesh of \"%s\" failed due to no result hogg file attached to task request response.\n", clusterData->clusterBaseName);
	}

	worldCellClusterQueueNotifyDistributedRemeshComplete(clusterData, task->fileAttachmentFull, retryRemesh);
}

bool updateClusterRemeshCacheAndRetrieveClusterMesh(WorldRegion *region, const WorldCell * cell, const char *baseClusterName, HogFile * cluster_bins, WorldClusterState *cluster_options, worldCellClusterGenerateCellClusterData *clusterData)
{
	ModelClusterSource *newClusterSource = NULL;
	bool loadedOrProcessedMesh = false;
	char remeshName[MAX_PATH];
	bool reuseOriginalClusterMesh = false;
	char image_prefix[MAX_PATH];
	int lod = cell->vis_dist_level;
	IVec3 pos;
	copyVec3(cell->cell_block_range.min_block, pos);
	newClusterSource = clusterData->source_models;

	if (!cluster_options->regionTextureTable)
		cluster_options->regionTextureTable = stashTableCreateWithStringKeys(64,StashDefault);

	strcpy(image_prefix, baseClusterName);
	sprintf(remeshName, "%s.msh", image_prefix);

	if (lod)
	{
		if (checkClusterLODDependenciesUpToDate(cell, baseClusterName, cluster_bins) &&
			checkClusterMeshDependenciesUpToDate(clusterData, newClusterSource, remeshName, baseClusterName, cluster_bins))
		{
			reuseOriginalClusterMesh = true;
		}
		else
		{
			gatherInputClusters(newClusterSource, region, cell, cluster_options, cluster_bins);
		}
	}
	else
	{
		if (checkClusterMeshDependenciesUpToDate(clusterData, newClusterSource, remeshName, baseClusterName, cluster_bins))
		{
			reuseOriginalClusterMesh = true;
		}
	}

	if (reuseOriginalClusterMesh)
	{
		// else cache is up to date, read cached remesh
		clusterData->remeshedClusterMesh = gmeshLoadFromHogFile(cluster_bins, remeshName);
		++clusterData->buildStats.numReuse;
		assert(clusterData->remeshedClusterMesh);
		if (clusterData->remeshedClusterMesh)
			loadedOrProcessedMesh = true;
	}

	// if we couldn't load an existing mesh, and the source model data is not empty, make a mesh
	if (!loadedOrProcessedMesh)
	{
		++clusterData->buildStats.numBuilds;
		if (modelClusterIsEmpty(newClusterSource))
		{
			// empty mesh (NULL) is correct in this case, so don't treat as an error
			loadedOrProcessedMesh = true;
			worldCellClusterQueueNotifyRemeshComplete(clusterData, NULL, false);
		}
		else
		{
			if (clusterData->remeshState == CRS_RemeshingDistributed)
			{
				SpawnRequestData * remeshRequest = StructAllocVoid(parse_SpawnRequestData);
				char hoggFileName[MAX_PATH];
				char taskName[MAX_PATH];

				loadstart_printf("Preparing cluster data hogg.\n");
				modelClusterSourceHoggCreate(clusterData, newClusterSource, cluster_bins, region, SAFESTR(hoggFileName), true, lod);
				loadend_printf(" done.\n");

				sprintf(taskName, "Remesh %s", clusterData->clusterBaseName);
				remeshRequest->label = StructAllocString(taskName);
				clusterData->timeTaskRequestIssued = _time32(NULL);
				clusterData->timeTaskAutoRetry = clusterData->timeTaskRequestIssued + 
					clusterData->clusterState->debug.distributed_remesh_retry_timeout;

				InterlockedIncrement(&cluster_options->numDistributedRemeshes);

				taskServerRequestExec(remeshRequest, hoggFileName, cbTaskServerRemeshTaskCompleted, clusterData);

				// not a failure, even though we didn't load a mesh, we are processing it
				loadedOrProcessedMesh = true;
			}
			else
			{
				loadedOrProcessedMesh = remeshCluster(clusterData);

				worldCellClusterQueueNotifyRemeshComplete(clusterData, NULL, !loadedOrProcessedMesh);
			}
		}
	}
	else
	{
		worldCellClusterQueueNotifyRemeshComplete(clusterData, NULL, false);
	}

	return loadedOrProcessedMesh;
}

bool worldCellClusterSetupImagePaths(worldCellClusterGenerateCellClusterData * clusterData)
{
	const WorldRegion * region = clusterData->cell_src->region;
	WorldClusterState *cluster_options = clusterData->clusterState;
	char image_filename[MAX_PATH];
	RemeshMaterial *remeshMaterial = NULL;
	int makeTempImageDirResult = 0;
	bool normalActive = clusterData->source_models->cluster_volumes_condensed->include_normal;
	bool specActive = clusterData->source_models->cluster_volumes_condensed->include_specular;

	remeshMaterial = clusterData->remeshClusterMaterial = StructCreateVoid(parse_RemeshMaterial);

	sprintf(clusterData->remeshGMeshName,"%s.msh", clusterData->clusterBaseName);

	sprintf(clusterData->diffuseTexName,"%s_D.png", clusterData->clusterBaseName);

	if (specActive)
		sprintf(clusterData->specularTexName,"%s_S.png", clusterData->clusterBaseName);
	else
		sprintf(clusterData->specularTexName,"%s.png",cluster_default_textures[CLUSTER_TEXTURE_SPECULAR_INDEX]);

	if (normalActive)
		sprintf(clusterData->normalTexName,"%s_N.png", clusterData->clusterBaseName);
	else
		sprintf(clusterData->normalTexName,"%s.png",cluster_default_textures[CLUSTER_TEXTURE_NORMAL_INDEX]);

	// The following line creates the location of where simplygon should dump the diffuse png file.  The extension is already included from the above line.
	{
		const char * clusterDefaultMaterialSwappedTextures[3] = { clusterData->diffuseTexName, clusterData->specularTexName, clusterData->normalTexName };
		int defaultTexIndex;

		for (defaultTexIndex = 0; defaultTexIndex < CLUSTER_MAX_TEXTURES; ++defaultTexIndex)
		{
			RemeshAssetSwap *tex_swap;
			const char * origTex = cluster_default_textures[defaultTexIndex];
			const char * swapTex = clusterDefaultMaterialSwappedTextures[defaultTexIndex];

			sprintf(image_filename, "%s/%s", cluster_options->clusterTempImageFullPath, swapTex);
			tex_swap = createRemeshTextureSwap("wlTerrainBin",origTex,image_filename);
			eaPush(&remeshMaterial->tex_swaps,tex_swap);
		}
	}

	mkdirtree(cluster_options->clusterTempImageFullPath);

	return true;
}

void modelClusterSourceExportDefaultTextures(WorldClusterState * cluster_options, TaskProfile *exportTexStats)
{
	int i;
	char tex_file_path[MAX_PATH];
	char tex_file_name[MAX_PATH];

	if (!cluster_options->regionTextureTable)
		cluster_options->regionTextureTable = stashTableCreateWithStringKeys(64,StashDefault);

	for (i = 0; i < ARRAY_SIZE(cluster_default_textures); i++) {
		sprintf(tex_file_name, "%s.png", cluster_default_textures[i]);
		sprintf(tex_file_path, "%s\\%s", cluster_options->clusterTempImageFullPath, tex_file_name);
		stashAddPointer(cluster_options->regionTextureTable,cluster_default_textures[i],cluster_default_textures[i],false);
		wl_state.gfx_texture_save_as_png(cluster_default_textures[i], tex_file_path, true, exportTexStats);
	}
}

bool modelClusterSourceExportTextures(ModelClusterSource *modelCluster, const WorldClusterState * cluster_options, HogFile *destRemeshPackOptional,
	TaskProfile *exportTexStats)
{
	StashTableIterator material_texture_iterator;
	StashElement tex_elem;
	bool bExportSuccess = true;

	stashGetIterator(modelCluster->texture_table, &material_texture_iterator);
	while (stashGetNextElement(&material_texture_iterator, &tex_elem))
	{
		char tex_file_path[MAX_PATH];
		char tex_file_name[MAX_PATH];
		char *source_tex_name = stashElementGetPointer(tex_elem);
		char *fileNameStart = (char*)fileGetFilenameSubstrPtr(source_tex_name);
		char *fileExtensionStart = strrchr(fileNameStart, '.');
		bool sendToPackHogg = false;

		if (fileExtensionStart)
		{
			// source_tex_name is a file already on disk
			sendToPackHogg = destRemeshPackOptional != NULL;
		}
		else
		{
			bool bAlreadyExported = false;
			// source_tex_name is a BasicTexture asset
			sprintf(tex_file_name, "%s.png", source_tex_name);
			sprintf(tex_file_path, "%s\\%s", cluster_options->clusterTempImageFullPath, tex_file_name);
			// If the name of the texture will not be added to the region stash, then it was already exported by an earlier cluster.
			bAlreadyExported = !stashAddPointer(cluster_options->regionTextureTable,source_tex_name,source_tex_name,false);
			if (bAlreadyExported ||	wl_state.gfx_texture_save_as_png(source_tex_name, tex_file_path, true, exportTexStats)) {	// If there isn't a way to store the names of the textures, spit them out
				sendToPackHogg = destRemeshPackOptional != NULL;
				fileNameStart = tex_file_name;
				source_tex_name = tex_file_path;
			}
			else
			{
				bExportSuccess = false;
			}
		}


		if (sendToPackHogg)
			bExportSuccess = worldCellClusterAddFileToHogg(destRemeshPackOptional, source_tex_name, fileNameStart, exportTexStats);
	}

	return bExportSuccess;
}

bool remeshCluster(worldCellClusterGenerateCellClusterData * clusterData)
{
	bool remeshSuccess;
	int i;
	GMesh * mesh = NULL;
	ModelClusterSource * newClusterSource = clusterData->source_models;
	WorldClusterState * cluster_options = clusterData->clusterState;
	RemeshMaterial *remeshMaterial = clusterData->remeshClusterMaterial;


	for (i = 0; i < eaSize(&newClusterSource->cluster_models); i++)
	{
		ModelClusterObject *modelObject = newClusterSource->cluster_models[i];
		if (!modelObject->gmesh)
			modelClusterObjectConvertModelToGMesh(newClusterSource, i, modelObject->model_filename, 
				cluster_options->clusterTempImageFullPath, NULL, clusterData);
		else
		{
			// cluster LODs have implicit materials and textures, since they have no source Model
			++clusterData->buildStats.inputGeo.numMaterials;
			clusterData->buildStats.inputGeo.numTextures += CLUSTER_MAX_TEXTURES;
		}
		modelClusterAddGeoStats(&clusterData->buildStats.inputGeo, modelObject->gmesh);
	}

	if (!modelClusterSourceExportTextures(newClusterSource, cluster_options, NULL, &clusterData->buildStats.pngExport))
	{
		Errorf("Exporting model textures for \"%s\" failed.", clusterData->clusterBaseName);
	}

	if (cluster_options->debug.dumpWorldClusterSource)
	{
		char *estrClusterDebugOut = NULL;
		estrClear(&estrClusterDebugOut);
		ParserWriteText(&estrClusterDebugOut, parse_ModelClusterSource, newClusterSource, 0, 0, 0);
		OutputDebugStringf("%s\n", estrClusterDebugOut);
		estrDestroy(&estrClusterDebugOut);
	}

	mesh = clusterData->remeshedClusterMesh = calloc(1, sizeof(GMesh));
	remeshSuccess = remeshModels(newClusterSource, mesh, cluster_options->clusterTempImageFullPath, clusterData->clusterBaseName, &clusterData->buildStats);

	if (remeshSuccess)
	{
		S64 timeTexStart,timeTexEnd;
		HogFile * cluster_bins = cluster_options->remesh_model_hogg_file;
		bool bDXTCompressAndTransferSuccess = true;

		// move png files into the temp remesh hogg.
		GET_CPU_TICKS_64(timeTexStart);
		{
			const char * clusterTextureNames[3] = { clusterData->diffuseTexName, clusterData->specularTexName, clusterData->normalTexName};
			int defaultTexIndex;
			for (defaultTexIndex = 0; defaultTexIndex < CLUSTER_MAX_TEXTURES; ++defaultTexIndex)
			{
				const char * texName = clusterTextureNames[defaultTexIndex];
				char image_fullpath[MAX_PATH];
				float rescale_amount;

				rescale_amount = 1.0 / clusterData->source_models->cluster_volumes_condensed->texture_super_sample;

				if (defaultTexIndex == 1 && !clusterData->source_models->cluster_volumes_condensed->include_specular)
					continue;

				if (defaultTexIndex == 2 && !clusterData->source_models->cluster_volumes_condensed->include_normal)
					continue;

				sprintf(image_fullpath, "%s/%s", cluster_options->clusterTempImageFullPath, texName);
				fixDoubleSlashes(image_fullpath);
				bDXTCompressAndTransferSuccess = modelClusterMoveRemeshImagesToHogg(cluster_bins, texName, image_fullpath,
					&clusterData->buildStats.wtexCompress, &clusterData->buildStats.cacheTex,
					rescale_amount, defaultTexIndex == CLUSTER_TEXTURE_DIFFUSE_INDEX ? RTEX_DXT1 : RTEX_DXT5);
				if (!bDXTCompressAndTransferSuccess)
				{
					loadupdate_printf("Remesh of \"%s\" failed due to missing result PNG or DXT compression failure \"%s\".\n", clusterData->clusterBaseName, texName);
					break;
				}
			}
		}
		GET_CPU_TICKS_64(timeTexEnd);
		log_printf(LOG_SIMPLYGON, "\t\tCreated wtex files for %s and moved to hogg: %f milliseconds\n", clusterData->clusterBaseName, timerSeconds64(timeTexEnd - timeTexStart));

		if (bDXTCompressAndTransferSuccess)
		{
			const char *parseString = ParserGetTableName(parse_RemeshMaterial);

			mesh->parse_table = parseString;
			remeshMaterial->simplygon_material = allocAddString("Cluster_Default");
			mesh->parse_struct_data = remeshMaterial;
			mesh->usagebits |= USE_OPTIONAL_PARSE_STRUCT;

			assert(wl_state.calculate_vertex_lighting_for_gmesh);
			wl_state.calculate_vertex_lighting_for_gmesh(mesh);

			if (!worldCellClusterWriteGMeshBinToHogg(cluster_bins, mesh, clusterData->remeshGMeshName, &clusterData->buildStats.cacheGeo))
			{
				remeshSuccess = false;
				loadupdate_printf("Remesh of \"%s\" failed writing remesh GMesh to cache hogg.\n", clusterData->clusterBaseName);
			}

			modelClusterAddGeoStats(&clusterData->buildStats.outputGeo, mesh);
			++clusterData->buildStats.outputGeo.numMaterials;
			clusterData->buildStats.outputGeo.numTextures += CLUSTER_MAX_TEXTURES;
		}
		else
			remeshSuccess = false;
	}
	else
	{
		loadupdate_printf("Remesh of \"%s\" failed to produce output mesh. See remeshing log.\n", clusterData->clusterBaseName);
	}

	return remeshSuccess;
}

// 0 = A, ..., 25 = Z, 26 = AA, 27 = AB, ..., 26*26 = BA
char *numToColumnBase26(char * base26Text, int base26Text_size, int number)
{
	char *destDigit = base26Text;
	char *lastWriteDest = base26Text + base26Text_size - 1;

	int maxNum = 26;
	int numDigits = 1;
	int writtenDigits = 0;
	while (number >= maxNum)
	{
		number -= maxNum;
		maxNum *= 26;
		++numDigits;
	}

	strcpy_s(SAFESTR2(base26Text), "");

	do
	{
		int digitValue = number % 26;
		*destDigit = 'A' + digitValue;
		if (destDigit < lastWriteDest)
			++destDigit;
		number = number / 26;
	}
	while (number);
	for (numDigits -= destDigit - base26Text; numDigits; --numDigits)
	{
		*destDigit = 'A';
		if (destDigit < lastWriteDest)
			++destDigit;
	}
	*destDigit = '\0';
	_strrev(base26Text);
	return base26Text;
}

void worldCellClusterWriteBuildStatsCSV(worldCellClusterGenerateCellClusterData **cellClusterQueue)
{
	F32 timeScale = GetCPUTicksMsScale();
	char csvLine[1024];
	char csvColumnHeader[1024];
	char csvTexCastTiming[64];
	WorldRegion *region = NULL;
	const char * mapName = NULL;
	const char * regionName = NULL;
	int firstClusterDataRow = 0;
	int lastClusterDataRow = 0;
	int currentRow = 1;
	int column;

	if (!eaSize(&cellClusterQueue))
		return;

	region = cellClusterQueue[0]->cell_src->region;
	mapName =  zmapGetName(region->zmap_parent);
	regionName = worldRegionGetRegionName(region);
	regionName = regionName ? regionName : "Default";

	filelog_printf(WORLD_CLUSTER_LOG, "%s,  %s\n", mapName, regionName);
	++currentRow;
	sprintf(csvColumnHeader,
		"%s,  %s, %s, %s, %s, %s, %s,  %s, %s, %s, %s,  %s, %s, %s, %s, %s, %s,  %s, %s,  %s, %s,  %s, %s,  %s, %s,  %s, %s,  %s,  %s,  %s, %s, %s,  %s, %s,  "
			"%s, %s, %s,  %s, %s, %s,  %s, %s, %s,  %s, %s, %s,  %s",
		"Cluster Name", 
		"Input Obj", 
		"Input Tris", 
		"Input Verts", 
		"Input Tex", 
		"Input Materials", 
		"Input Mesh Bytes", 
		"Output Tris", 
		"Output Verts", 
		"Output Tex", 
		"Output Mesh Bytes", 

		"Tex Width",
		"Tex Height",
		"Tex Supersample", 
		"Geometry Res", 
		"Specular Map", 
		"Normal Map", 

		"netSend ms", "netSend bytes",
		"netRecv ms", "netRecv bytes",
		"WTex decomp time", "WTex decomp bytes", 
		"model export ms", "model export bytes",
		"png export ms", "png export bytes",
		"convert geo ms",
		"remesh geo ms",
		"cast diff tex ms",
		"cast nrm tex ms",
		"cast spec tex ms",
		"DXT comp ms",
		"DXT write bytes",
		"cache geo ms",
		"cache geo read bytes",
		"cache geo write bytes",
		"cache tex ms",
		"cache tex read bytes",
		"cache tex write bytes",
		"MSet bin ms",
		"MSet bin read bytes",
		"MSet bin write bytes",
		"WTex bin ms",
		"WTex bin read bytes",
		"WTex bin write bytes",
		"Client bin size"
		);
	filelog_printf(WORLD_CLUSTER_LOG, "%s\n", csvColumnHeader);
	++currentRow;

	firstClusterDataRow = currentRow;
	FOR_EACH_IN_EARRAY_FORWARDS(cellClusterQueue, worldCellClusterGenerateCellClusterData, currentCellCluster);
	{
		const WorldClusterStats * stats = &currentCellCluster->buildStats;
		const ModelClusterVolume * settings = currentCellCluster->source_models->cluster_volumes_condensed;
		int texIndex = 0;

#define FS64d "%"FORM_LL"d"
		strcpy(csvTexCastTiming, "");
		for (texIndex = 0; texIndex < CLUSTER_MAX_TEXTURES; ++texIndex)
		{
			TaskProfile texCast = { 0 };
			if (texIndex < eaSize(&stats->eaCastTextures))
				StructCopyAll(parse_TaskProfile, stats->eaCastTextures[texIndex], &texCast);
			strcatf(csvTexCastTiming, "%.2f, ", texCast.totalTaskTime.totalTimeAccum * timeScale);
		}

		sprintf(csvLine,
			"%s,  %d, %d, %d, %d, %d, %d,  %d, %d, %d, %d,  %d, %d, %d, %d, %c, %c,  %.2f, "FS64d",  %.2f, "FS64d",  %.2f, "FS64d",  %.2f, "FS64d",  %.2f, "FS64d",  %.2f,  %.2f,   %s   %.2f, "FS64d",  "
			"%.2f, "FS64d", "FS64d",  %.2f, "FS64d", "FS64d",  %.2f, "FS64d", "FS64d",  %.2f, "FS64d", "FS64d", "FS64d,
			currentCellCluster->clusterBaseName, 

			stats->inputGeo.numMeshes, stats->inputGeo.numTriangles, stats->inputGeo.numVertices, stats->inputGeo.numMaterials, stats->inputGeo.numTextures, stats->inputGeo.numMeshBytes,
			stats->outputGeo.numTriangles, stats->outputGeo.numVertices, stats->outputGeo.numTextures, stats->outputGeo.numMeshBytes,

			settings->texture_width,
			settings->texture_height,
			settings->texture_super_sample,
			settings->geo_resolution,
			settings->include_specular ? 'Y' : 'N',
			settings->include_normal ? 'Y' : 'N',

			stats->netSend.totalTaskTime.totalTimeAccum * timeScale, stats->netSend.bytesWrite,
			stats->netRecv.totalTaskTime.totalTimeAccum * timeScale, stats->netRecv.bytesRead,
			stats->wtexDecompress.totalTaskTime.totalTimeAccum * timeScale, stats->wtexDecompress.bytesRead, 
			stats->modelExport.totalTaskTime.totalTimeAccum * timeScale, stats->modelExport.bytesWrite, 
			stats->pngExport.totalTaskTime.totalTimeAccum * timeScale, stats->pngExport.bytesWrite, 
			stats->convertGeo.totalTaskTime.totalTimeAccum * timeScale,
			stats->remeshGeo.totalTaskTime.totalTimeAccum * timeScale,

			csvTexCastTiming,

			stats->wtexCompress.totalTaskTime.totalTimeAccum * timeScale, stats->wtexCompress.bytesWrite, 
			stats->cacheGeo.totalTaskTime.totalTimeAccum * timeScale, stats->cacheGeo.bytesRead, stats->cacheGeo.bytesWrite, 
			stats->cacheTex.totalTaskTime.totalTimeAccum * timeScale, stats->cacheTex.bytesRead, stats->cacheTex.bytesWrite, 

			stats->msetBinGeneration.totalTaskTime.totalTimeAccum * timeScale, stats->msetBinGeneration.bytesRead, stats->msetBinGeneration.bytesWrite, 
			stats->wtexBinGeneration.totalTaskTime.totalTimeAccum * timeScale, stats->wtexBinGeneration.bytesRead, stats->wtexBinGeneration.bytesWrite,
			stats->clientClusterBinSize.bytesWrite
			);
		filelog_printf(WORLD_CLUSTER_LOG, "%s\n", csvLine);
		++currentRow;
	}
	FOR_EACH_END;

	lastClusterDataRow = currentRow - 1;
	strcpy(csvLine, "");
	for (column = 0; column < 40; ++column)
	{
		char columnName[16];
		numToColumnBase26(SAFESTR(columnName), column + 1);
		strcatf(csvLine, ",=sum(%s%d:%s%d)", columnName, firstClusterDataRow, columnName, lastClusterDataRow);
	}
	filelog_printf(WORLD_CLUSTER_LOG, "%s\n", csvLine);
	filelog_printf(WORLD_CLUSTER_LOG, "%s\n", csvColumnHeader);


	filelog_printf(WORLD_CLUSTER_LOG, "\n");
	filelog_printf(WORLD_CLUSTER_LOG, "Cluster Crashes,\n");
	FOR_EACH_IN_EARRAY_FORWARDS(cellClusterQueue, worldCellClusterGenerateCellClusterData, currentCellCluster);
	{
		const WorldClusterStats * stats = &currentCellCluster->buildStats;
		if (stats->numCrashes)
			filelog_printf(WORLD_CLUSTER_LOG, "%s\n", currentCellCluster->clusterBaseName);
	}
	FOR_EACH_END;

	filelog_printf(WORLD_CLUSTER_LOG, "\n");
	filelog_printf(WORLD_CLUSTER_LOG, "Cluster Retries/Failures,\nCluster,Build Count, Crashes, Disconnects,\n");
	FOR_EACH_IN_EARRAY_FORWARDS(cellClusterQueue, worldCellClusterGenerateCellClusterData, currentCellCluster);
	{
		const WorldClusterStats * stats = &currentCellCluster->buildStats;
		if (stats->numBuilds > 1)
		{
			filelog_printf(WORLD_CLUSTER_LOG, "%s,%d,%d,%d,\n", currentCellCluster->clusterBaseName, 
				currentCellCluster->buildStats.numBuilds, currentCellCluster->buildStats.numCrashes, 
				currentCellCluster->buildStats.numDisconnects);
		}
	}
	FOR_EACH_END;
	filelog_printf(WORLD_CLUSTER_LOG, "\n");
}

#include "WorldCellClustering_h_ast.c"
