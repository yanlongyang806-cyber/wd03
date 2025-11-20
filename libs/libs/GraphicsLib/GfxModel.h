#ifndef GFXMODEL_H
#define GFXMODEL_H
GCC_SYSTEM

typedef struct Model Model;
typedef struct ModelLOD ModelLOD;
typedef struct ModelToDraw ModelToDraw;
typedef struct WorldDrawableList WorldDrawableList;
typedef int GeoHandle;

void gfxModelStartup(void);
void gfxModelLODDestroyCallback(ModelLOD *model);
void gfxModelFreeAllCallback(void);
void gfxModelClearAllForDevice(int rendererIndex);
void gfxModelReloadCallback(void);

void gfxFillModelRenderInfo(ModelLOD *model);

bool gfxDemandLoadModelLODCheck(ModelLOD *model_lod);
int gfxDemandLoadModelFixup(Model *model, ModelToDraw *models, int models_size, int model_count, WorldDrawableList *draw_list);

#endif
