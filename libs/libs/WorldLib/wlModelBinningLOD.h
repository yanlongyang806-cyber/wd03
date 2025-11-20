#pragma once
GCC_SYSTEM

typedef struct ModelSource ModelSource;
typedef struct GMesh GMesh;
typedef struct Geo2LoadData Geo2LoadData;
typedef enum ReductionMethod ReductionMethod;
typedef enum GMeshAttributeUsage GMeshAttributeUsage;
typedef struct ModelClusterSource ModelClusterSource;

ModelSource *wlModelBinningGenerateLOD(ModelSource *srcmodel, F32 error, F32 upscale, ReductionMethod method, int lod_index);
void sourceModelFromGMesh(ModelSource *model, const GMesh *mesh, Geo2LoadData *gld);
int sourceModelToGMesh(GMesh *mesh, ModelSource *srcmodel, GMeshAttributeUsage ignoredAttributes);
