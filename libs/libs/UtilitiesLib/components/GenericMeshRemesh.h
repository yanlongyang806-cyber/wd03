#ifndef _GENERICMESHREMESH_H_
#define _GENERICMESHREMESH_H_
GCC_SYSTEM

#include "mathutil.h"
#include "TaskProfile.h"

C_DECLARATIONS_BEGIN

typedef struct HogFile HogFile;
typedef struct GMesh GMesh;
typedef struct SimplygonMaterial SimplygonMaterial;
typedef struct SimplygonMaterialTable SimplygonMaterialTable;
typedef struct ModelClusterSource ModelClusterSource;
typedef struct TextureSwap TextureSwap;
typedef struct MaterialSwap MaterialSwap;
typedef struct StashTableImp StashTableImp;
typedef struct WorldClusterStats WorldClusterStats;
typedef enum RdrTexFormat RdrTexFormat;

#define CLUSTER_DIFFUSE_TEXTURE_CHANNEL		0
#define CLUSTER_SPECULAR_TEXTURE_CHANNEL	1
#define CLUSTER_NORMALMAP_TEXTURE_CHANNEL	2

#define CLUSTER_MAX_TEXTURES 3

AUTO_ENUM;
typedef enum ClusterTargetLOD
{
	ClusterTargetLOD0 = 0,	//Default
	ClusterTargetLOD1 = 1,
	ClusterTargetLOD2 = 2,
	ClusterTargetLOD3 = 3,
	ClusterTargetLOD4 = 4,
	ClusterTargetLOD5 = 5,
	ClusterTargetLOD6 = 6,
	ClusterTargetLOD7 = 7,
	ClusterTargetLOD8 = 8,
} ClusterTargetLOD;
extern StaticDefineInt ClusterTargetLODEnum[];

AUTO_ENUM;
typedef enum ClusterMinLevel
{
	ClusterMinLevel256ft = 0,	//Default
	ClusterMinLevel512ft = 1,
	ClusterMinLevel1024ft = 2,
	ClusterMinLevel2048ft = 3,
	ClusterMinLevel4096ft = 4,
} ClusterMinLevel;
extern StaticDefineInt ClusterMinLevelEnum[];

// worldEntryCreateModelClusterVolume in WorldCellEntry.c sets the default to point to ClusterMaxLODLevel_8
AUTO_ENUM;
typedef enum ClusterMaxLODLevel
{
	ClusterMaxLODLevel_Default = 0,
	ClusterMaxLODLevel_1 = 1,
	ClusterMaxLODLevel_2 = 2,
	ClusterMaxLODLevel_3 = 3,
	ClusterMaxLODLevel_4 = 4,
	ClusterMaxLODLevel_5 = 5,
	ClusterMaxLODLevel_6 = 6,
	ClusterMaxLODLevel_7 = 7,
	ClusterMaxLODLevel_8 = 8,	//Default
} ClusterMaxLODLevel;
extern StaticDefineInt ClusterMaxLODLevelEnum[];

// worldEntryCreateModelClusterVolume in WorldCellEntry.c sets the default to SIMPLYGON_TEXTURE_RESOLUTION
AUTO_ENUM;
typedef enum ClusterTextureResolution
{
	ClusterTextureResolutionDefault = 0,
	ClusterTextureResolution64 = 64,
	ClusterTextureResolution128 = 128,
	ClusterTextureResolution256 = 256,
	ClusterTextureResolution512 = 512,
	ClusterTextureResolution1024 = 1024,
	ClusterTextureResolution2048 = 2048,
	ClusterTextureResolution4096 = 4096,
} ClusterTextureResolution;
extern StaticDefineInt ClusterTextureResolutionEnum[];

// worldEntryCreateModelClusterVolume in WorldCellEntry.c sets the default to SIMPLYGON_DEFAULT_RESOLUTION
AUTO_ENUM;
typedef enum ClusterGeometryResolution
{
	ClusterGeometryResolutionDefault = 0,
	ClusterGeometryResolution64 = 64,
	ClusterGeometryResolution128 = 128,
	ClusterGeometryResolution256 = 256,
	ClusterGeometryResolution512 = 512,
	ClusterGeometryResolution1024 = 1024,
} ClusterGeometryResolution;
extern StaticDefineInt ClusterGeometryResolutionEnum[];

// worldEntryCreateModelClusterVolume in WorldCellEntry.c sets the default to point to ClusterTextureSupersample4x
AUTO_ENUM;
typedef enum ClusterTextureSupersample
{
	ClusterTextureSupersampleDefault = 0,
	ClusterTextureSupersample1 = 1,
	ClusterTextureSupersample4x = 2,	//Default
	ClusterTextureSupersample16x = 4,
} ClusterTextureSupersample;
extern StaticDefineInt ClusterTextureSupersampleEnum[];

AUTO_STRUCT;
typedef struct WorldClusterGeoStats
{
	U32 numMaterials;
	U32 numTextures;
	U32 numVertices;
	U32 numTriangles;
	U32 numMeshes;
	U32 numMeshBytes;
} WorldClusterGeoStats;

AUTO_STRUCT AST_FIXUPFUNC(fixupWorldClusterStats);
typedef struct WorldClusterStats
{
	TaskProfile clusteringTotals;
	TaskProfile netSend;
	TaskProfile netRecv;
	TaskProfile wtexDecompress;
	TaskProfile modelExport;
	TaskProfile pngExport;
	TaskProfile convertGeo;
	TaskProfile remeshGeo;
	TaskProfile **eaCastTextures;
	TaskProfile wtexCompress;
	TaskProfile cacheGeo;
	TaskProfile cacheTex;
	TaskProfile msetBinGeneration;
	TaskProfile wtexBinGeneration;
	TaskProfile clientClusterBinSize;
	WorldClusterGeoStats inputGeo;
	WorldClusterGeoStats outputGeo;
	U32 numBuilds;
	U32 numReuse;
	U32 numCrashes;
	U32 numDisconnects;
} WorldClusterStats;

extern ParseTable parse_WorldClusterStats[];
#define TYPE_parse_WorldClusterStats WorldClusterStats



AUTO_STRUCT;
typedef struct ModelClusterTextures
{
	char	gmesh_texture_file_d[MAX_PATH];
	char	gmesh_texture_file_n[MAX_PATH];
	char	gmesh_texture_file_s[MAX_PATH];
} ModelClusterTextures;

extern ParseTable parse_ModelClusterTextures[];
#define TYPE_parse_ModelClusterTextures ModelClusterTextures

AUTO_STRUCT;
typedef struct NamedTimestamp {
	const char*	name;					AST( POOL_STRING )
	U32			timestamp;
} NamedTimestamp;

extern ParseTable parse_NamedTimestamp[];
#define TYPE_parse_NamedTimestamp NamedTimestamp

AUTO_STRUCT;
typedef struct RemeshAssetSwap
{
	const char *orig_name; AST( POOL_STRING )
	const char *replace_name; AST( POOL_STRING )
} RemeshAssetSwap;

extern ParseTable parse_RemeshAssetSwap[];
#define TYPE_parse_RemeshAssetSwap RemeshAssetSwap

AUTO_STRUCT AST_FIXUPFUNC(fixupModelClusterVolume);
typedef struct ModelClusterVolume
{
	Vec3 volume_min;
	Vec3 volume_max;
	int lod_max;	// This is not the highest lod level allowed, this is the maximum number of lod levels allowed for this area.  So if lod_min is set to 3 and lod_max is set to 2, the highest allowed vis_dist is 4 (3 and 4 are two levels of vis_dist objects).
	int lod_min;
	int target_lod_level;
	int	texture_height;
	int	texture_width;
	int texture_super_sample;
	int geo_resolution;
	bool include_specular;
	bool include_normal;
} ModelClusterVolume;
extern ParseTable parse_ModelClusterVolume[];
#define TYPE_parse_ModelClusterVolume ModelClusterVolume

// Should be used in place of StructInit for ModelClusterVolume objects in almost all cases to setup initial values.
// This sets the default values of a ModelClusterVolume and is not part of the fixup constructor so it will not clobber
// the values being read in from a file.
ModelClusterVolume* modelClusterVolumeCreate();

extern ParseTable parse_ModelClusterVolume[];
#define TYPE_parse_ModelClusterVolume ModelClusterVolume

AUTO_STRUCT AST_FIXUPFUNC(fixupModelClusterObject);
typedef struct ModelClusterObject
{
	const char	*model_filename;				AST( POOL_STRING )
	const char	*mesh_filename;					AST( POOL_STRING )
	U32			model_timestamp;
	GMesh		*gmesh;							NO_AST
	ModelClusterTextures **gmesh_texture_files;
	Mat44		model_matrix;					AST(FORMATSTRING(HIGH_PREC_FLOAT=1))
	F32			radius;							AST(FORMATSTRING(HIGH_PREC_FLOAT=1))
	Vec3		position;						AST(FORMATSTRING(HIGH_PREC_FLOAT=1))
	NamedTimestamp	**tex_stamps;
	NamedTimestamp	**mat_stamps;
	SimplygonMaterial	*material;				NO_AST
	RemeshAssetSwap	**tex_swaps;
	RemeshAssetSwap	**mat_swaps;
} ModelClusterObject;

extern ParseTable parse_ModelClusterObject[];
#define TYPE_parse_ModelClusterObject ModelClusterObject

#define CLUSTER_TOOL_VERSION 10

// Parseable root struct for dependency checking on world clustering bin cache.
AUTO_STRUCT AST_IGNORE(texture_super_sample) AST_FIXUPFUNC(fixupModelClusterSource);
typedef struct ModelClusterSource
{
	int					cluster_tool_version;
	ModelClusterObject	**cluster_models;
	ModelClusterVolume *cluster_volumes_condensed;	// This will hold the information of all of the cluster volumes condensed into a single object with only information relevant to it.
	StashTableImp		*texture_table;			NO_AST
} ModelClusterSource;

extern ParseTable parse_ModelClusterSource[];
#define TYPE_parse_ModelClusterSource ModelClusterSource
#define TYPE_parse_RemeshAssetSwap RemeshAssetSwap
ModelClusterSource* modelClusterSourceCreateDefault();	// Wrapper function for creating a ModelClusterSource with the ModelClusterVolume already attached.  Unless reading it in from a hogg file, this is generally the expected function to be used when creating a ModelClusterSource object.

AUTO_STRUCT;
typedef struct RemeshMaterial
{
	const char	*simplygon_material;	AST(POOL_STRING)
	RemeshAssetSwap	**tex_swaps;
} RemeshMaterial;

extern ParseTable parse_RemeshMaterial[];
#define TYPE_parse_RemeshMaterial RemeshMaterial

U32 pngDumpFromHogFile(HogFile * handle, const char * dest_filename, const char * filename);
ModelClusterSource* modelClusterSourceHoggLoad(HogFile *clusterHogg, const char *tempDir);
void exportGmeshToObj(GMesh *gmesh, char *fileoutName);
bool modelClusterMoveRemeshImagesToHogg(HogFile *hog_file, const char *image_name, const char* image_filename, TaskProfile *dxtStats, TaskProfile *cacheTexStats, float texture_scale, RdrTexFormat dest_format);
int remeshModels(ModelClusterSource *modelCluster, GMesh * gmesh, const char * image_tempdir, const char* image_filename, WorldClusterStats * remeshStats);

int simplygonDefaultResolution();
unsigned int simplygonTextureHeight();
unsigned int simplygonTextureWidth();
int simplygonDefaultSuperSampling();

C_DECLARATIONS_END

#endif//_GENERICMESHREMESH_H_
