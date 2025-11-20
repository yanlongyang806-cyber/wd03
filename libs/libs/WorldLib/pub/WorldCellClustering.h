#pragma once

#include "mathutil.h"
#include "GenericMeshRemesh.h"

#define CLUSTER_TEXTURE_FILENAME_DIFFUSE_SUFFIX "_D"
#define CLUSTER_TEXTURE_FILENAME_SPECULAR_SUFFIX "_S"
#define CLUSTER_TEXTURE_FILENAME_NORMAL_SUFFIX "_N"

#define CLUSTER_TEXTURE_DEFAULT_DIFFUSE "texture_library\\system\\Engine\\white.wtex"
#define CLUSTER_TEXTURE_DEFAULT_SPECULAR "texture_library\\system\\Engine\\black.wtex"
#define CLUSTER_TEXTURE_DEFAULT_NORMAL "texture_library\\system\\Templates\\Default_Nx.wtex"

extern const char * ClusterTextureSuffices[CLUSTER_MAX_TEXTURES];
extern const char * ClusterTextureDefaults[CLUSTER_MAX_TEXTURES];

typedef struct HogFile HogFile;
typedef struct WorldRegion WorldRegion;
typedef struct WorldCell WorldCell;
typedef struct BlockRange BlockRange;
typedef struct WorldCellParsed WorldCellParsed;
typedef struct WorldDrawableEntry WorldDrawableEntry;
typedef struct WorldRegionCommonParsed WorldRegionCommonParsed;
typedef struct ModelClusterSource ModelClusterSource;
typedef struct ZoneMap ZoneMap;
typedef struct BinFileList BinFileList;
typedef struct GMesh GMesh;
typedef struct RemeshMaterial RemeshMaterial;
typedef struct worldCellClusterGenerateCellClusterData worldCellClusterGenerateCellClusterData;
typedef struct TextureSwap TextureSwap;
typedef struct MaterialSwap MaterialSwap;
typedef struct StashTableImp StashTableImp;
typedef StashTableImp* StashTable;

typedef enum ClusterBuildDisableLevel
{
	CBDL_Enable,
	CBDL_DisableRemesh,
	CBDL_DisableCompletely,
} ClusterBuildDisableLevel;

typedef struct WorldClusterDebug
{
	int testWorldCluster;
	int disableWorldCluster;						// Various restrictions to cluster build set by ClusterBuildDisableLevel.
	int obj_atlas_export;
	int max_cluster_lod;
	int ignore_remesh_atlas_hogg;
	int dumpWorldClusterSource;
	IVec4 cluster_range;
	int dont_cluster_alpha_objects;
	U32 max_distributed_remesh_tasks;
	U32 max_retries;
	int distributed_remesh_retry_timeout;
	bool high_precision_remesh;
	bool cluster_all_levels;
} WorldClusterDebug;

AUTO_STRUCT AST_FIXUPFUNC(fixupWorldClusterState);
typedef struct WorldClusterState
{
	int start_i;
	int end_i;
	int start_j;
	int end_j;
	int start_y;
	int end_y;
	HogFile * remesh_model_hogg_file;				NO_AST
	bool worldCellsChangedFlag;
	bool gatherFromLeafLevel;
	bool cluster_all_levels;

	ModelClusterSource ** ea_cluster_buckets;

	U32 timeClusteringStart;
	U32 timeClusteringComplete;
	U32 numDistributedRemeshes;

	int processed_blocks;
	int total_blocks;
	WorldRegion * current_region_ptr;				NO_AST
	int current_region_idx;
	int total_regions;

	// World cell cluster folders
	char clusterTempBinPath[MAX_PATH];
	char clusterBinPath[MAX_PATH];
	char clusterTempImagePath[MAX_PATH];
	char clusterTempImageFullPath[MAX_PATH];

	// Cluster hogg files
	char clusterCacheHoggFile[MAX_PATH];
	char clusterClientRegionBinFile[MAX_PATH];
	char clusterClientRegionBinFileFullPath[MAX_PATH];
	ModelClusterVolume **cluster_volumes;

	WorldClusterDebug debug;						NO_AST
	StashTable			regionTextureTable;			NO_AST
} WorldClusterState;

extern ParseTable parse_WorldClusterState[];
#define TYPE_parse_WorldClusterState WorldClusterState


typedef enum ClusterRemeshState
{
	CRS_NotQueued,
	CRS_Remeshing,
	CRS_RemeshingDistributed,
	CRS_ProcessingDistributedResult,
	CRS_Complete,
	CRS_Canceled,
	CRS_Final
} ClusterRemeshState;

AUTO_STRUCT;
typedef struct worldCellClusterGenerateCellClusterData
{
	const WorldCell *cell_src;								NO_AST
	const WorldRegionCommonParsed *region_dst;				NO_AST
	WorldClusterState *clusterState;						NO_AST
	ModelClusterSource *source_models;
	U32 unprocessedChildrenRemaining;
	volatile U32 remeshState;
	__time32_t timeTaskRequestIssued;
	__time32_t timeTaskAutoRetry;
	worldCellClusterGenerateCellClusterData *parent;		NO_AST
	bool bWroteClientBins;
	bool excludeFromBins;									NO_AST	// belongs here?
	GMesh * remeshedClusterMesh;							NO_AST
	RemeshMaterial *remeshClusterMaterial;

	WorldClusterStats buildStats;

	// filename parts
	char clusterBaseName[MAX_PATH];

	char remeshGMeshName[MAX_PATH];
	char diffuseTexName[MAX_PATH];
	char normalTexName[MAX_PATH];
	char specularTexName[MAX_PATH];

	char taskParameterHoggFile[MAX_PATH];
	char taskResultHoggFile[MAX_PATH];
} worldCellClusterGenerateCellClusterData;

extern ParseTable parse_worldCellClusterGenerateCellClusterData[];
#define TYPE_parse_worldCellClusterGenerateCellClusterData worldCellClusterGenerateCellClusterData

const WorldClusterDebug * worldCellGetClusterDebugState();
bool worldCellClusterIsRemeshEnabled(const WorldClusterDebug * debug_state);
void SetClusterDebugSettings(int x1, int x2, int z1, int z2, bool distRemesh, bool highPrecisionRemesh, bool leaf_only_clustering);

bool worldCellClusteringFeatureEnabled();

void worldCellClusterMakeTextureName(char *textureName, int textureName_size, const char *basePath, const char *clusterName, const char *imageExtension, int textureIndex);

void worldCellClusterBucketDrawableEntry(WorldClusterState * clustering_options, const WorldDrawableEntry * entry, TextureSwap **tex_swaps, MaterialSwap **mat_swaps, const char* material_replace_name);
bool modelClusterIsEmpty(const ModelClusterSource * newClusterSource);
void modelClusterProcessVolumes(ModelClusterSource *cluster, WorldClusterState *cluster_options, const WorldCell *cell);
bool checkClusterDependenciesUpToDate(ModelClusterSource * source, const char *clusterBaseFilename, HogFile * cluster_bins, WorldRegion *region, int vis_dist_level);

void getRegionClusterBlockRange(const ZoneMap *zmap, int k, WorldClusterState * clustering_options);

void worldCellClusteringSetupRegionClustering(ZoneMap *zmap);
void worldCellClusteringFinishRegionClusterGather(ZoneMap *zmap);
void worldCellClusteringCleanupRegionClustering(ZoneMap *zmap, BinFileList *file_list);

void worldCellClusterQueueEntryForceRetry(worldCellClusterGenerateCellClusterData *clusterQueueEntry);
void worldCellClusterQueueNotifyRemeshComplete(worldCellClusterGenerateCellClusterData *clusterQueueEntry, const char *taskResultFile, bool retryRemeshing);
void worldCellClusterQueueNotifyDistributedRemeshComplete(worldCellClusterGenerateCellClusterData *clusterQueueEntry, const char *taskResultFile, bool retryRemeshing);

bool worldCellClusterQueueEntryChangeState(worldCellClusterGenerateCellClusterData *clusterQueueEntry,
	ClusterRemeshState finalState, ClusterRemeshState expectedCurrentState);
bool worldCellClusterQueueEntryCompleteLocal(worldCellClusterGenerateCellClusterData *clusterQueueEntry);
bool worldCellClusterQueueEntryCompleteDistributed(worldCellClusterGenerateCellClusterData *clusterQueueEntry);
void worldCellClusterQueueEntryForceRetry(worldCellClusterGenerateCellClusterData *clusterQueueEntry);

bool worldCellClusterSetupImagePaths(worldCellClusterGenerateCellClusterData * clusterData);
void worldCellClusterGenerateCellCluster(worldCellClusterGenerateCellClusterData *clusteringData);
bool worldCellClusterWriteResultsToMapBins(worldCellClusterGenerateCellClusterData *clusteringData);
void worldCellClusteringOpenRegionClusterHog(WorldClusterState *clusterState, const WorldRegion *region, const char *baseTempDir);
void worldCellClusteringCloseRegionClusterHog(WorldClusterState *clusterState);
void worldCellCalcEntryPos(const Vec3 pos, IVec3 entry_pos);
void worldCellClusterGetTempDir(const char *zmapFilename, char *base_dir, size_t base_dir_size);

void worldGetClusterName(char *dest_str, int dest_str_size, const char *strRegionName, const BlockRange *cellRange, int vis_dist_level);
void worldGetClusterBinHoggName(char *dest_str, int dest_str_size, const char *strRegionName, const BlockRange *cellRange, int vis_dist_level);
void worldGetClusterBinHoggPath(char *dest_str, int dest_str_size, const WorldRegion *pRegion, const BlockRange *cellRange, int vis_dist_level);
void worldCellGetClusterName(char *dest_str, int dest_str_size, const WorldCell *cell_src);
void worldCellGetClusterBinHoggName(char *dest_str, int dest_str_size, const WorldCell *cell_src);
void worldCellGetClusterBinHoggPath(char *dest_str, int dest_str_size, const WorldCell *cell_src);
void worldCellQueueClusterModelLoad(WorldCell *cell);
bool worldCellClusteringIsRegionClustered(const WorldRegion * region);

void worldCellClusterWriteBuildStatsCSV(worldCellClusterGenerateCellClusterData **cellClusterQueue);

void modelClusterSourceExportDefaultTextures(WorldClusterState * cluster_options, TaskProfile *exportTexStats);

void worldCellClusterEntryGetPos(const WorldDrawableEntry *entry, Vec3 pos);
void worldCellClusterEntryGetClusterPos(const WorldDrawableEntry *entry, IVec3 pos);

extern const char* cluster_default_textures[];
