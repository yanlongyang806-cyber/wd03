#pragma once
GCC_SYSTEM

#include "wlModelEnums.h"
#include "WorldLibEnums.h"
#include "fileLoader.h" // For FileLoaderPriority enum

typedef struct GeoRenderInfo GeoRenderInfo;
typedef struct ModelHeader ModelHeader;
typedef struct SharedHeapHandle SharedHeapHandle;
typedef struct ModelLODInfo ModelLODInfo;
typedef struct Material Material;
typedef struct Model Model;
typedef struct PSDKCookedMesh PSDKCookedMesh;
typedef struct GMesh GMesh;
typedef struct GMeshParsed GMeshParsed;
typedef struct ScaledCollision ScaledCollision;
typedef struct MemLog MemLog;
typedef struct SimpleBuffer *SimpleBufHandle;
typedef struct ModelLOD ModelLOD;
typedef struct Geo2LoadData Geo2LoadData;

#define FILE FileWrapper
typedef struct FileWrapper FileWrapper;
typedef FILE *(*ModelDataLoadFunc)(Model *model, void *user_data);

#define MODELHEADER_EXTENSION ".modelheader"

#define MODEL_COLLISION_NAME_SUFFIX "_COLL"
#define MODEL_OCCLUSION_NAME_SUFFIX "_OCCL"



typedef struct ModelLoadTracker
{
	U32 last_lod_change_timestamp;
	U32 last_seen_timestamp;
	S8 fade_out_lod;
	S8 fade_in_lod;
	U8 do_fade_in : 1;
	U8 do_fade_out : 1;
} ModelLoadTracker;

typedef struct ModelLoadHeader
{
	U32 material_count;
	U32 tri_count;
	U32 vert_count;

	Vec3 minBBox;
	Vec3 maxBBox;
} ModelLoadHeader;

AUTO_STRUCT;
typedef struct PackData
{
	int		packsize;
	int		unpacksize; // if negative, does not use RLE compression
	union {
		S32 data_offs;
		U8 *data_ptr; NO_AST
	};
} PackData;

typedef Vec4 Weights;
typedef U8 Matidxs[4];

//All the skinning info for a model.
typedef struct BoneInfo 
{
	int			numbones;					//Only has boneinfo if numbones
	union
	{
		int			index;
		const char*	name;
	} bone[MAX_OBJBONES];
} BoneInfo;

AUTO_STRUCT;
typedef struct AltPivot
{
	const char* name; AST(KEY POOL_STRING)
	Mat4 mat;
	Vec3 scale; // this is seperate from the mat, because the current parser code ignores the scale in the mat4
} AltPivot;

// A collection of ModelHeaders in the same file
typedef struct ModelHeaderSet
{
	ModelHeader **model_headers;
	const char *filename;
} ModelHeaderSet;

AUTO_STRUCT AST_IGNORE(has_reductions) AST_IGNORE(AutoLOD_Dists);
typedef struct ModelHeader
{
	const char* modelname; AST(KEY POOL_STRING)
	const char* filename; AST(CURRENTFILE) // Even if not referenced in code, used by saving from dictionary
	AltPivot** altpivot;
	const char** bone_names; AST(POOL_STRING)
	const char* attachment_bone; AST(POOL_STRING)
	F32 radius;
	Vec3 min;
	Vec3 max;

	int tri_count;
	U32 has_verts2:1;
	U32 high_detail_high_lod:1;

	U32 missing_data:1; AST(NO_TEXT_SAVE) // Run-time/bin-time flag - there is no .geo2 file
} ModelHeader;

// On-disk format, cannot change order
typedef enum ModelProcessTimeFlags {
	MODEL_PROCESSED_TRI_OPTIMIZATIONS = 1 << 0,
	MODEL_PROCESSED_HIGH_PRECISCION_TEXCOORDS = 1 << 1,
//	MODEL_PROCESSED_BONEWEIGHTS_AS_DATA_OLD = 1 << 2, //< old, legacy processing
//	MODEL_PROCESSED_BONEWEIGHTS_AS_DATA = 1 << 3, //< old, legacy processing
	MODEL_PROCESSED_HAS_WIND = 1 << 4,
	MODEL_PROCESSED_HAS_TRUNK_WIND = 1 << 5,
	MODEL_PROCESSED_HIGH_DETAIL_HIGH_LOD = 1 << 6,
	MODEL_PROCESSED_ALPHA_TRI_SORT = 1 << 7,
	MODEL_PROCESSED_NO_TANGENT_SPACE = 1 << 8,
	MODEL_PROCESSED_VERT_COLOR_SORT = 1 << 9,

	MODEL_DEBUG_HAS_BEEN_FREED = 0xFEEDCAFE,
} ModelProcessTimeFlags;

AUTO_STRUCT;
typedef struct TexID
{
	U16		id;		//~name (idx of this texture name in texture name list)
	U16		count;  //number of tris in model->sts that use this texture 
} TexID;

AUTO_STRUCT;
typedef struct ModelPackData
{
	PackData		tris;
	PackData		verts;
	PackData		norms;
	PackData		binorms;
	PackData		tangents;
	PackData		sts;
	PackData		sts3;
	PackData		colors;
	PackData		weights;
	PackData		matidxs;
	PackData		verts2;
	PackData		norms2;
} ModelPackData;

typedef struct ModelUnpackData
{
	U32				*tris;
	Vec3			*verts;
	Vec3			*norms;
	Vec3			*binorms;
	Vec3			*tangents;
	Vec2			*sts;
	Vec2			*sts3;
	U8				*colors;
	U8				*matidxs;
	U8				*weights;
	Vec3			*verts2;
	Vec3			*norms2;
} ModelUnpackData;

// Header data which is written to/from disk.  Changing this struct causes geometry .bin files to be rebuilt
AUTO_STRUCT;
typedef struct ModelLODData
{
	int				data_size; // The size of this structure plus all persistent packed data
	int				vert_count;
	int				tri_count;
	int				tex_count;	//number of tex_idxs (sum of all tex_idx->counts == tri_count)
  	F32				texel_density_avg;
  	F32				texel_density_stddev;
	ModelProcessTimeFlags process_time_flags; AST(INT)
	TexID			*tex_idx; NO_AST //array of (materials + number of tris that have it)
	ModelPackData	pack;
	const char		**tex_names; NO_AST //malloc'd array of names indexed into by tex_idx[].id
} ModelLODData;
extern ParseTable parse_ModelLODData[];
#define TYPE_parse_ModelLODData ModelLODData

// Move tex_idx onto ModelLOD so that ModelLODData can be freed when unneeded?  Or realloc away all *but* that?

// A single LOD (possibly the base/highest LOD), directly corresponds to something drawable
typedef struct ModelLOD
{
	// Linked list of all ModelLODs
	ModelLOD				*next;
	ModelLOD				*prev;

	ModelLOD				*actual_lod; // Which LOD to actually draw, this could be another model
	ModelLODData			*data; // All packed data + the header allocated in one big chunk
	Material				**materials; // regular array of size data->tex_count

	GeoRenderInfo			*geo_render_info;

	volatile GeoLoadState	loadstate;

	int						tri_count; // -1 initially, filled in after data is loaded
	int						vert_count; // -1 initially, filled in after data is loaded

	ModelUnpackData 		unpack;
	U32						unpacked_last_used_timestamp;

	U32						data_last_used_timestamp; // Packed data and other header info

	U32						unpacked_in_use; // Only accessed within critical section, boolean, but needs to be 32-bits or packed with other crit-sec-only members
	ModelDataLoadFunc		data_load_callback;
	void*					data_load_user_data;

	Model					*model_parent;
	U32						lod_index:4;
	U32						load_in_foreground:1;
	WLUsageFlagsBitIndex	mem_usage_bitindex:WL_FOR_MAXCOUNT; // Index this memory was tracked to
	U32						is_high_detail_lod:1; // for budgets calculations
	U32						noTangentSpace:1;
	ModelLOD				*high_detail_prev; // for budgets; for loaded high detail lods, we keep a list of them
	ModelLOD				*high_detail_next; // for budgets; for loaded high detail lods, we keep a list of them
	volatile S32			last_mem_usage; // Memory usage currently tracked
	int						reference_count; // How many other models are referencing this ModelLOD, should be 1 for oneself in normal circumstances
	const char				*debug_name; // Not guaranteed to be filled in
	F32						uv_density; // log4 of the minimum triangle uv_area/world_area
} ModelLOD;

// A "model", which is a collection of ModelLODs, which may reference various other Models and their ModelLODs
typedef struct Model
{
	// Values copied from header for better caching
	F32 radius;
	Vec3 min, max, mid;

	const char *name; // Pooled string pointer into the ModelHeader
	ModelHeader *header;
	ModelLOD **model_lods;
	WLUsageFlags use_flags;

	ModelLODInfo *lod_info;

	ScaledCollision **psdkScaledTriangleMeshes;
	ScaledCollision **psdkScaledConvexMeshes;		// used for fx, moving platforms, and interaction costumes

	struct {
		volatile GeoLoadState loadstate;

		int tex_count;
		TexID *tex_idx;
		const char **tex_names; // Not an EArray

		ModelDataLoadFunc		data_load_callback;
		void*					data_load_user_data;

		void* unpacked_data;
		int unpacked_size;

	} collision_data;
	const char				*msetForErrors; //set to data source mset, pooled
} Model;


SA_RET_OP_VALID Model *modelFromHeader(SA_PARAM_NN_VALID ModelHeader *model_header, bool load, WLUsageFlags use_flags);
SA_RET_OP_VALID Model *modelFind(SA_PARAM_NN_STR const char *modelname, bool load, WLUsageFlags use_flags);
SA_RET_OP_VALID Model *modelFindEx(SA_PARAM_OP_STR const char *filename, SA_PARAM_NN_STR const char *modelname, bool load, WLUsageFlags use_flags);  // Searches object_library and character_library (keyed differently)
SA_RET_OP_VALID ModelLOD *modelFindLOD(SA_PARAM_NN_STR const char *modelName, int lod_index, bool load, WLUsageFlags use_flags);
SA_ORET_OP_VALID ModelLOD *modelLoadLOD(SA_PARAM_NN_VALID const Model *model, int lod_index);
SA_RET_OP_VALID ModelLOD *modelLoadColLOD(SA_PARAM_NN_VALID Model *model);
bool modelExists(SA_PARAM_NN_STR const char *modelname); // Doesn't even allocate a Model*

SA_RET_OP_VALID ModelHeaderSet *modelHeaderSetFind(SA_PARAM_NN_STR const char *filename);

bool modelLODIsLoaded(ModelLOD *model_lod);
bool modelLODHasUnpacked(ModelLOD *model_lod);
void modelLODFreeData(ModelLOD *model, bool caller_in_model_unpack_cs);

const char* wlCharacterModelKey(const char* fname, const char* modelname);

void modelFreeData(Model *model);
bool modelDestroy(Model *model);

#define WAIT_FOR_LOAD (true)
#define DONT_WAIT_FOR_LOAD (false)
void modelLODRequestBackgroundLoad(ModelLOD *model);
ModelLOD *modelLODLoadAndMaybeWait(const Model *model, int lod_index, int bWaitForLoad);
__forceinline static ModelLOD *modelLODWaitForLoad(const Model *model, int lod_index)
{
	return modelLODLoadAndMaybeWait(model, lod_index, true);
}

PSDKCookedMesh* geoCookConvexMesh(Model *model, const Vec3 scale, int lod_index, bool no_error_msg);
void geoUnpackDeltas(PackData *pack, void *data, int stride, int count, PackType type, const char *modelname, const char *filename, const char *msetForErrors);
void geoUnpack(PackData *pack, void *data, const char *modelname, const char *filename, const char *msetForErrors);

typedef bool (*ModelCallback)(Model *model, intptr_t userdata); // return false to stop iterating
void modelForEachModel(ModelCallback callback, intptr_t userdata, bool includeTempModels);

typedef bool (*ModelLODCallback)(ModelLOD *model, intptr_t userdata); // return false to stop iterating
void modelForEachModelLOD(ModelLODCallback callback, intptr_t userdata, bool includeTempModels);

typedef void (*Destructor)(void* value);
void modelLODSetDestroyCallback(Destructor destructor);
typedef void (*ModelFreeAllCallback)(void);
void modelSetFreeAllCallback(ModelFreeAllCallback callback);
typedef void (*ModelReloadCallback)(void);
void modelSetReloadCallback(ModelReloadCallback callback);

void modelLODFreeUnpacked(ModelLOD *model);
void modelLODFreePacked(ModelLOD *model);

void modelFreeAllCache(WLUsageFlags unuse_type);

// Vaguely internalish, but needed for GfxModelLOD.c and the LOD Editor:
void modelLODInitFromData(ModelLOD *model_lod);
void modelUnloadCheck(U32 unloadTime);
void modelHeaderReloaded(Model *model);
void modelBinningLODInfoOverride(const char *geoName, const char *modelname, ModelLODInfo *lod_info);

typedef void (__stdcall *GeoCallbackFunc)(GeoRenderInfo *geo_render_info, void *parent, int param);
typedef bool (__stdcall *GeoBoolCallbackFunc)(GeoRenderInfo *geo_render_info, void *parent, int param);
#define geoRequestBackgroundExec(pfnAPC, filename, geo_render_info, parent, param, priority) geoRequestBackgroundExecEx(pfnAPC, filename, geo_render_info, parent, param, priority, __FUNCTION__, #pfnAPC)
void geoRequestBackgroundExecEx(GeoCallbackFunc pfnAPC, const char *filename, GeoRenderInfo *geo_render_info, void *parent, int param, FileLoaderPriority priority, char *caller, char *callee);

void geoForceBackgroundLoaderToFinish_dbg(const char *blamee);
#define geoForceBackgroundLoaderToFinish() {														\
		PERFINFO_AUTO_START(__FUNCTION__, 1);					\
		geoForceBackgroundLoaderToFinish_dbg(__FUNCTION__);	\
		PERFINFO_AUTO_STOP();																		\
	}

long geoLoadsPending(int include_misc);

void geo2PrintFileInfo(const char *fileName); // .geo2 file
bool geo2PrintBinFileInfo(const char *fileName, Geo2VerifyLogMode logLevel, bool bVerify); // .mset file

void modelsReloaded(void);

int modelLODGetBytesCompressed(ModelLOD *model);
int modelLODGetBytesUncompressed(ModelLOD *model);
int modelLODGetBytesUnpacked(ModelLOD *model);
int modelLODGetBytesTotal(ModelLOD *model);
int modelLODGetBytesSystem(ModelLOD *model_lod);
void modelLODUpdateMemUsage(ModelLOD *model_lod);

WLUsageFlags modelGetUseFlags(const Model *model);

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Geometry_Art);); // Now contains model data such as skinning bone names as well as alt pivots

Material *modelGetMaterialByTri(ModelLOD *model, U32 tri);
Material *modelGetCollisionMaterialByTriEx(Model *model, U32 tri, const char **matSwaps);
#define modelGetCollisionMaterialByTri(model, tri) modelGetCollisionMaterialByTriEx(model, tri, NULL)

const char * modelLODGetModelName(ModelLOD *model);
const char * modelLODGetDebugName(ModelLOD *model);

MemLog *geoGetMemLog(void);

// Allocates a temporary model with one LOD, one material
Model *tempModelAlloc_dbg(const char *name, Material **materials, int material_count, ModelDataLoadFunc data_load_callback, void *user_data, bool load_in_foreground, WLUsageFlags use_flags MEM_DBG_PARMS);
#define tempModelAlloc(name, materials, material_count, use_flags) tempModelAlloc_dbg(name, materials, material_count, NULL, NULL, true, use_flags MEM_DBG_PARMS_INIT)
bool tempModelLoadHeader(SimpleBufHandle header_buf, ModelLoadHeader * header);
bool tempModelSkipLoadHeader(SimpleBufHandle header_buf);
Model *tempModelLoad_dbg(const char *name, SimpleBufHandle header_buf, ModelDataLoadFunc data_load_callback, void *user_data, WLUsageFlags use_flags MEM_DBG_PARMS);
#define tempModelLoad(name, header_buf, data_load_callback, user_data, use_flags) tempModelLoad_dbg(name, header_buf, data_load_callback, user_data, use_flags MEM_DBG_PARMS_INIT)
void tempModelFree(Model **model_ptr);
bool modelIsTemp(Model* model);

void modelAddGMeshToGLD(Geo2LoadData **gld_ptr, GMesh *mesh, const char *name, const char **material_names, int material_name_count, bool no_collision, bool collision_only, bool no_tangent_space, SimpleBufHandle header_buf);
void modelWriteAndFreeBinGLD(Geo2LoadData *gld, SimpleBufHandle geo_buf, bool use_texnames);

// Should be a tempModel
GMeshParsed *modelToParsedFormat(Model *model);
// Fills in a tempModel from a GMesh
void modelFromGmesh(Model *model, GMesh *mesh);

void geo2UpdateBinsForGeo(const char *path);

bool geo2BinningModelLODs(void);

SA_RET_OP_VALID ModelHeader* wlModelHeaderFromName(SA_PARAM_NN_VALID const char* modelname );
