#pragma once
GCC_SYSTEM


// A lot of the structs in this file are private by default, not by design, and
//   could be moved to wlModel.h to be made public.


#include "WorldLibEnums.h"
#include "wlModelEnums.h"

typedef struct Model				Model;
typedef struct ModelLOD				ModelLOD;
typedef struct ModelLODData			ModelLODData;
typedef struct StashTableImp*		StashTable;
typedef struct PSDKCookedMesh		PSDKCookedMesh;
typedef struct GroupSplineParams	GroupSplineParams;
typedef struct ModelHeader			ModelHeader;
typedef struct ScaledCollision		ScaledCollision;
typedef struct WorldCollisionEntry	WorldCollisionEntry;
typedef struct WorldCollStoredModelData WorldCollStoredModelData;
typedef struct SimpleBuffer			SimpleBuffer;
typedef struct SimpleBuffer			*SimpleBufHandle;
#define FILE FileWrapper
typedef struct FileWrapper FileWrapper;
typedef struct PackData PackData;

#define sizeOfVersion16GeoLoadDataHeader 4

typedef struct GeoMeshTempData {
	ModelLOD *model;
	const Vec3*	verts;
	S32		vert_count;
	const S32*	tris;
	S32		tri_count;
	const Vec2*	sts; // tex coords
	const Vec3*	norms;
	const U8*		weights; // bone weights
	const U8*		boneidxs; // bone weight indexes
	const F32		sphereRadius;
	Vec3	boxSize;
} GeoMeshTempData;

typedef void (*GeoMeshHandler)(	void* userPointer,
								const GeoMeshTempData* meshTempData);

void geoUncompress(void* outbuf, U32 outsize, const void* inbuf, U32 insize, const char *modelname, const char *filename, const char *msetForErrors);

void geoLoadInit(void);
void geoLoadCheckThreadLoader(void);
void geoGetCookedMeshName(char *buffer, int buffer_size, char *detail_buf, int detail_buf_size, const Model *model, const GroupSplineParams* spline, const Vec3 scale);
void geoScaledCollisionRemoveRef(WorldCollisionEntry *entry);
PSDKCookedMesh *geoScaledCollisionGetCookedMesh(ScaledCollision *col);
PSDKCookedMesh* geoCookConvexMesh(Model *model, const Vec3 scale, int lod_index, bool no_error_msg);
PSDKCookedMesh* geoCookMesh(Model *model, const Vec3 scale, const GroupSplineParams* spline, WorldCollisionEntry *entry, bool bDoCooking, bool bWaitForLoad);
PSDKCookedMesh* geoCookSphere(F32 radius);
PSDKCookedMesh* geoCookCapsule(F32 radius, F32 height);
PSDKCookedMesh* geoCookBox(const Vec3 boxSize);
void wlLoadModelHeaders(void);
SA_RET_OP_VALID ModelHeader* wlModelHeaderFromNameEx(SA_PARAM_NN_VALID const char* filename, SA_PARAM_NN_VALID const char *modelname);

void modelHeaderAddToSet(ModelHeader *model_header); // For GetVRML only


FILE *geo2OpenLODDataFileByName(const char *filename);
void geo2CloseDataFile(FILE *f);

void geoProcessTempData(GeoMeshHandler meshHandler,
						void* userPointer,
						const Model *model,
						int lod_index,
						const Vec3 scale,
						S32 useTris,
						S32 useTexCoords,
						S32 useWeights,
						S32 useNormals,
						const GroupSplineParams* spline);

const char* wlCharacterModelKey(const char* fname, const char* modelname);

SA_RET_OP_RELEMS_VAR(*length) U8 *wlCompressDeltas(const void *data, int *length, int stride, int count, PackType pack_type, F32 float_scale, F32 inv_float_scale);
int wlUncompressDeltas(void *dst, const U8 *src, int stride, int count, PackType pack_type);

bool modelLODDestroy(ModelLOD *model);
void modelLODFreeData(ModelLOD *model, bool caller_in_model_unpack_cs);

void modelDestroyScaledCollision(ScaledCollision *col);

//ModelLOD *modelLODWaitForLoad(Model *model, int lod_index);
ModelLOD *modelAllocLOD(Model *model, int lod_index MEM_DBG_PARMS);

ModelLODData *geo2LoadLODData(FILE *f, const char *modelname, int lod_index, const char *filename);
void geo2LoadColData(FILE *f, const char *modelname, Model *model, PackData* outPackedData);

void modelUnloadCollisionsCheck(U32 unloadTime);
bool modelInitLODs(Model *model, intptr_t userdata);

Model * modelGetCollModel(Model * pRenderModel);

typedef const void *DictionaryHandle;
extern DictionaryHandle hModelHeaderDict;
extern ModelLOD *model_lod_list_head; // List of regular ModelLODs (not tempModels)
extern ModelLOD *model_lod_list_tail; // List of regular ModelLODs (not tempModels)
extern ModelLOD *model_lod_list_cursor; // For unloading, always NULL or pointing to a valid model
