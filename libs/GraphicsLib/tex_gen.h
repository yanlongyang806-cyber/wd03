#pragma once
GCC_SYSTEM

#include "stdtypes.h"
#include "RdrEnums.h"
#include "RdrTextureEnums.h"
#include "WorldLibEnums.h"
#include "GfxTextureEnums.h"

typedef struct BasicTexture BasicTexture;
typedef struct RdrSurface RdrSurface;

// pixel_format must be RTEX_RGB_U8, RTEX_RGBA_U8, or compressed types if not doing a sub-region

BasicTexture *texGenNewEx(int width, int height, int depth, const char *name, TexGenMode tex_gen_mode, WLUsageFlags use_category);
BasicTexture *texGenNew(int width, int height, const char *name, TexGenMode tex_gen_mode, WLUsageFlags use_category);
void texGenFree(BasicTexture *bind);
void texGenFinalizeFree(BasicTexture *bind);
void texGenFreeNextFrame(BasicTexture *bind);
void texGenDoDelayFree(void);

void texGenUpdate_dbg(BasicTexture *bind, U8 *tex_data, RdrTexType tex_type, RdrTexFormat pixel_format, int mipcount, bool clamp, bool mirror, bool pointsample, bool refcount_data MEM_DBG_PARMS);
#define texGenUpdate(bind, tex_data, tex_type, pixel_format, mipcount, clamp, mirror, pointsample, refcount_data) texGenUpdate_dbg(bind, tex_data, tex_type, pixel_format, mipcount, clamp, mirror, pointsample, refcount_data MEM_DBG_PARMS_INIT)

void texGenUpdateRegion_dbg(BasicTexture *bind, U8 *tex_data, int x, int y, int z, int width, int height, int depth, RdrTexType tex_type, RdrTexFormat pixel_format MEM_DBG_PARMS);
#define texGenUpdateRegion(bind, tex_data, x, y, z, width, height, depth, tex_type, pixel_format) texGenUpdateRegion_dbg(bind, tex_data, x, y, z, width, height, depth, tex_type, pixel_format MEM_DBG_PARMS_INIT)

void texGenUpdateFromWholeSurface(BasicTexture *bind, RdrSurface *src_surface, RdrSurfaceBuffer buffer_num); // Note: does NOT work on Xbox currently

void texGenDestroyVolatile(void);
void texGenDoFrame(void);
void texGenClearAllForDevice(int rendererIndex);

typedef void (*TextureCallback)(BasicTexture *bind, void *userData);
// index should be -1 for all, or between 0 and TEX_NUM_DIVISIONS-1
void texGenForEachTexture(TextureCallback callback, void *userData, int index);

