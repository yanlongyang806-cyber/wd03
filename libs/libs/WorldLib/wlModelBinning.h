#pragma once
GCC_SYSTEM

#include "wlModelEnums.h"

typedef struct ModelHeader ModelHeader;
typedef struct ModelSource ModelSource;
typedef struct Model Model;
typedef struct ModelLODData ModelLODData;
typedef struct ModelLODInfo ModelLODInfo;

typedef struct FileEntry FileEntry;
typedef FileEntry** FileList;

// an opaque handle for functions below
typedef struct Geo2LoadData Geo2LoadData;

void endianSwapArray(void *datap, int count, PackType type);

void geo2UpdateBinsForModel(ModelHeader *modelheader);
void geo2UpdateBinsForGeo(const char *path);

void geo2UpdateBinsForLoadData(ModelHeader * modelheader,Geo2LoadData * gld,FileList *file_list);
Geo2LoadData * geo2LoadModelFromSource(ModelHeader *modelheader,FileList * file_list);
void geo2Destroy(Geo2LoadData *gld);

void geo2UpdateBinsForAllGeos(void);
void geo2VerifyBinsForAllGeos(void);

const char *modelSourceGetName(ModelSource *model);
bool modelSourceHasVerts2(ModelSource *model);
int modelSourceGetTricount(ModelSource *model);
F32 modelSourceGetRadius(ModelSource *model);
F32 *modelSourceGetAutoLODDists(ModelSource *model);
int modelSourceGetProcessTimeFlags(ModelSource *model);

ModelSource *modelSourceFindSibling(ModelSource *model, const char *siblingname);

void geo2PrintFileInfo(const char *fileName); // .geo2 file
bool geo2PrintBinFileInfo(const char *fileName, Geo2VerifyLogMode logLevel, bool bVerify); // .mset file

char *geo2BinNameFromGeo2Name(const char *relpath, char *binpath, int binpath_size);

// Sets an override so instead of using the on-disk LODInfo, it'll use this in-memory one
// Must set it to NULL if this LODInfo is destroyed
void modelBinningLODInfoOverride(const char *geoName, const char *modelname, ModelLODInfo *lod_info);

#define GEO2_OL_BIN_VERSION 11 // Warning: do not change this if at all possible!  Causes all geometry re-built, and a massive patch size!
#define GEO2_CL_BIN_VERSION 11 // Warning: do not change this if at all possible!  Causes all geometry re-built, and a massive patch size!

#define MAX_MODEL_BIN_HEADER_SIZE 800000 // used to detect corrupt data
#define MIN_MODEL_BIN_HEADER_SIZE (4+4+4+2) // used to detect corrupt data

