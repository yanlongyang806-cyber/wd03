
#pragma once
GCC_SYSTEM

typedef struct RdrDrawList RdrDrawList;
typedef struct DynParticle DynParticle;
typedef struct DynSkeleton DynSkeleton;
typedef struct DynClothMesh DynClothMesh;
typedef struct DynFxRegion DynFxRegion;
typedef struct RdrDevice RdrDevice;
typedef U64 TexHandle;

void gfxDynamicsQueueAllWorld(void);
void gfxDynamicsQueueAllUI(SA_PARAM_NN_VALID RdrDrawList* drawList);

void gfxDynamicsStartup(void);

int gfxDynamicsGetParticleMemUsage(DynParticle* pPart);
bool gfxSkeletonIsVisible(DynSkeleton* pSkeleton);
bool gfxParticleIsVisible(DynParticle* pParticle, DynFxRegion* pFxRegion);

void gfxSetSoftParticleDepthTexture(RdrDevice *rdr_device, TexHandle depth_buffer);

void gfxDynDrawModelClearTempMaterials(bool bClearGeos);
void gfxDynDrawModelClearAllBodysocks(void);
