#ifndef _AUTOLOD_H_
#define _AUTOLOD_H_
GCC_SYSTEM

typedef struct GMeshReductions GMeshReductions;
typedef struct ModelLODInfo ModelLODInfo;
typedef struct AutoLOD AutoLOD;
typedef struct ModelSource ModelSource;
typedef struct ModelLODTemplate ModelLODTemplate;


extern ParseTable parse_AutoLOD[];
#define TYPE_parse_AutoLOD AutoLOD


typedef void (*LodReloadCallback)(const char *path);

void lodinfoLoad(void);
void lodinfoVerify(void);
void checkLODInfoReload(void);
void lodinfoLoadPostProcess(void);
void lodinfoReloadPostProcess(void);
int getNumLODReloads(void);
void lodinfoSetReloadCallback(LodReloadCallback callback);

void writeLODInfo(SA_PARAM_NN_VALID ModelLODInfo *info, SA_PARAM_NN_STR const char *geoname);

SA_RET_NN_STR const char *getLODFileName(SA_PARAM_NN_STR const char *geo_fname, bool full_path);
SA_RET_NN_VALID AutoLOD *allocAutoLOD(void);
void freeModelLODInfoData(SA_PARAM_OP_VALID ModelLODInfo *info);

typedef bool (*ExistsCallback)(void *userData, const char *modelname);
bool modelExistsWrapper(void *unused, const char *modelName);
void lodinfoFillInAutoData(SA_PARAM_NN_VALID ModelLODInfo *lod_info, bool no_lod, const char *geoFilename, int source_tri_count, 
						   F32 radius, bool processed_high_detail_high_lod, ExistsCallback existsCallback, void *userData, 
						   bool has_verts2, bool bUseDictionaries);

int lodsDifferent(SA_PARAM_NN_VALID ModelLODInfo *info1, SA_PARAM_NN_VALID ModelLODInfo *info2);

#endif //_AUTOLOD_H_

