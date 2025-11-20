#ifndef GFXTEXTURES_H
#define GFXTEXTURES_H
#pragma once
GCC_SYSTEM

#include "stdtypes.h"
#include "GfxTextureEnums.h"
#include "GfxTexturesPublic.h"
#include "WorldLibEnums.h"
#include "MaterialEnums.h"
#include "RdrTextureEnums.h"
#include "RdrEnums.h"
#include "GenericMesh.h"
#include "systemspecs.h"

//#define TexLoadHowFromTexture(bind) ((!TEX_HAS_MIPDATA(bind) && (((bind)->use_category) & (WL_FOR_UI|WL_FOR_UTIL)))?TEX_LOAD_NOW_CALLED_FROM_MAIN_THREAD:TEX_LOAD_IN_BACKGROUND)
#define TexLoadHowFromTexture(bind) TEX_LOAD_IN_BACKGROUND

#define TEX_DEMAND_LOAD_DEFAULT_UV_DENSITY	GMESH_LOG_MIN_UV_DENSITY

void gfxLoadTextures(void); // Loads headers of individual texture files

void texMakeWhite(void);
void texMakeDummyCubemap(void);
void texMakeDummies(void);

BasicTexture *texFindAndFlag(const char *name, int isRequired, WLUsageFlags use_category);
#define texFind(name, isRequired) texFindAndFlag(name, isRequired, (WLUsageFlags)0)
BasicTexture *texAllocateScratch(const char* name, int width, int height, WLUsageFlags use);
void texStealSurfaceSnapshot(BasicTexture* bind, RdrSurface* surf, RdrSurfaceBuffer buffer_num, int snapshot_idx);

// Using wrapper functions so we can try making them a parallel hashtable, etc
static __forceinline BasicTextureRareData *texGetRareData(BasicTexture *bind)
{
	if (!bind->has_rare)
	{
		return NULL;
	} else {
		return bind->rare;
	}
}

static __forceinline const BasicTextureRareData *texGetRareDataConst(const BasicTexture *bind) {
	if (!bind->has_rare)
	{
		return NULL;
	} else {
		return bind->rare;
	}
}

// Access to "rare" data
BasicTextureRareData *texAllocRareData(BasicTexture *bind);
void texRareDataOptionallyRelease(BasicTexture *bind); // If no rare members used, get rid of it
void texRareDataForceRelease(BasicTexture *bind);
U16 texGetOrigWidth(const BasicTexture *bind);
U16 texGetOrigHeight(const BasicTexture *bind);
U16 texGetDepth(const BasicTexture *bind);
TexWord *texGetTexWord(BasicTexture *bind);
TexWordParams *texGetTexWordParams(BasicTexture *bind);
U32 texGetRawReferenceCount(BasicTexture *bind);
U32 texGetDynamicReferenceCount(BasicTexture *bind);

// Access to loaded-only data
void texLoadedDataRelease(BasicTexture *bind);
void texLoadedDataOptionallyRelease(BasicTexture *bind);
BasicTextureLoadedData *texAllocLoadedData(BasicTexture *bind);

void gfxTexReduce(BasicTexture *tex, int new_levels);
void texFreeAll(void);
void texFreeAllNonUI(void);
void texFreeAll3D(void);
void texFree( BasicTexture *bind, int freeRawData );
#if ENABLE_TEXTURE_LIFETIME_EXTENDED_TRACE
void logBasicTexUse(BasicTexture *bind, void *pCookie);
#endif
bool texIsFreed(const BasicTexture *bind);
void texResetBinds(void);
void texSetupParametersFromBase(BasicTexture *bind);
void texClearAllForDevice(int rendererIndex);
void texOncePerFramePerDevice(void);
BasicTexture *texLoadBasic(const char *name, TexLoadHow mode, WLUsageFlags use_category);
void texLoadBasicInternal(BasicTexture *bind, TexLoadHow mode, WLUsageFlags use_category, U32 num_levels, int rawData);
BasicTexture *texLoadRawData(const char *name, TexLoadHow mode, WLUsageFlags use_category);
void texLoadRawDataInternal(BasicTexture *bind, TexLoadHow mode, WLUsageFlags use_category);
void texUnloadRawData(BasicTexture *texBind);
TexHandle texDemandLoad(BasicTexture *texbind, F32 dist, F32 uv_density, BasicTexture *errortex);
TexHandle texDemandLoadLowMIPs(BasicTexture *texbind, BasicTexture *error_bind);
void texResetAnisotropic(void);
void texResetReduceTimestamps(void);
void texFixName(const char *s,char *res, int size);

// Dynamic TexWord textures
BasicTexture *texFindDynamic(const char *layoutName, TexWordParams *params, WLUsageFlags use_category, const char *blameFileName);
bool texUnloadDynamic( BasicTexture *basicBind );

// Dynamically existing textures
BasicTexture *texRegisterDynamic(const char *texturepath);
void texUnregisterDynamic(const char *texturepath);

BasicTexture *texAllocateDynamic(const char *layoutName, bool addToHashtable);

void texFindFullName(const BasicTexture *texBind, char *filename, size_t filename_size);

const char *texGetName(const BasicTexture *texBind);
const char *texGetFullname(const BasicTexture *texBind);
bool texIsAlphaBordered(const BasicTexture *texBind);

long texLoadsPending(int include_misc);
void texEnableThreadedLoading(void);
void texDisableThreadedLoading(void);
void texSetSearchPath(char *searchPath);
void texCheckSwapList(void);
int texRenderLoadsPending();
int texLoadsQuota();
int texSentThisFrame();

void texAddToProcessingListFromBind(BasicTexture *bind);

void texGetMemUsage(SA_PRE_NN_FREE SA_POST_NN_VALID TexMemUsage *usage, WLUsageFlags flags_for_total);
const char *texMemMonitorNameFromFlags(WLUsageFlags use_flags);

BasicTexture *basicTextureCreate(void);
void basicTextureDestroy(BasicTexture *bind);

MemLog *texGetMemLog(void);

void texSetVolumeGlobalParams(bool needF16, U32 shrinkmask);

void texRecordNewMemUsage(BasicTexture *bind, TexMemIndex mem_index, U32 new_size);

void texReadInfoMemRefAlloc_dbg(TexReadInfo * info, size_t sizeBytes, const BasicTexture *pBasicTexture, const char *funcName MEM_DBG_PARMS);
void texReadInfoAssignMemRefAlloc_dbg(TexReadInfo * info, void *data_refcounted, const BasicTexture *pBasicTexture, const char *funcName MEM_DBG_PARMS);
int texReadInfoMemRefIncrement_dbg(TexReadInfo * info, const BasicTexture *pBasicTexture, const char *funcName MEM_DBG_PARMS);
int texReadInfoMemRefDecrement_dbg(TexReadInfo * info, const BasicTexture *pBasicTexture, const char *funcName MEM_DBG_PARMS);

#define texReadInfoMemRefAlloc(info, sizeBytes, pBasicTexture) texReadInfoMemRefAlloc_dbg((info), (sizeBytes), (pBasicTexture), __FUNCTION__ MEM_DBG_PARMS_INIT)
#define texReadInfoAssignMemRefAlloc(info, _data_refcounted, pBasicTexture) texReadInfoAssignMemRefAlloc_dbg((info), (_data_refcounted), (pBasicTexture), __FUNCTION__ MEM_DBG_PARMS_INIT)
#define texReadInfoMemRefIncrement(info, pBasicTexture) texReadInfoMemRefIncrement_dbg((info), (pBasicTexture), __FUNCTION__ MEM_DBG_PARMS_INIT)
#define texReadInfoMemRefDecrement(info, pBasicTexture) texReadInfoMemRefDecrement_dbg((info), (pBasicTexture), __FUNCTION__ MEM_DBG_PARMS_INIT)

#define DEFAULT_TEX_MEMORY_ALLOWED_32_BIT_OS (448 * 1024*1024)
#define DEFAULT_TEX_MEMORY_ALLOWED_3GBVA_BOOT_OPTION_32_BIT_OS (448 * 1024*1024)
#define DEFAULT_TEX_MEMORY_ALLOWED_64_BIT_OS (800 * 1024*1024)

extern U32 tex_memory_allowed;
extern int g_needTextureBudgetInfo;
extern volatile U32 texMemoryUsage[TEX_MEM_MAX];
extern BasicTexture *white_tex, *black_tex;
extern BasicTexture *default_env_cubetex, *default_env_spheretex;
extern BasicTexture **g_basicTextures;
extern BasicTexture *tex_from_sky_file;
extern BasicTexture *tex_use_pn_tris;

static __forceinline bool texOverMemory(U32 anticipated)
{
	return texMemoryUsage[TEX_MEM_LOADING] + texMemoryUsage[TEX_MEM_VIDEO] + texMemoryUsage[TEX_MEM_RAW] + anticipated > tex_memory_allowed;
}

#endif
