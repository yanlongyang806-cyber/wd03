#ifndef _TEXTUREATLAS_H
#define _TEXTUREATLAS_H
#pragma once
GCC_SYSTEM

#include "stdtypes.h"
#include "RdrTextureEnums.h"

typedef struct TTFMCacheElement TTFMCacheElement;
typedef U64 TexHandle;

// a texture that is part of an atlas
typedef struct AtlasTex
{
	int width;
	int height;
	const char *name;

	intptr_t atlas_sort_id;

	//LDM: moved these here so we can inline atlasGetModifiedUVs
	float u_mult;
	float v_mult;
	float u_offset;
	float v_offset;

	struct AtlasTexInternal *data;
} AtlasTex;


typedef enum
{
	// pixel type
	PIX_RGB = 1,
	PIX_RGBA = 2,
	PIX_MASK = 3,
	// bit flags
	PIX_DONTATLAS = 1<<3,
	PIX_NO_VFLIP = 1<<4,
	PIX_QUEUE_FOR_BTEX = 1<<5,
} PixelType;


AtlasTex *atlasLoadTexture(const char *sprite_name);
AtlasTex *atlasFindTexture(const char *sprite_name); // Doesn't load
AtlasTex *atlasGenTextureEx(U8 *src_bitmap, U32 width, U32 height, PixelType pixel_type, const char *name, TTFMCacheElement *font_element);
__forceinline static AtlasTex *atlasGenTexture(U8 *src_bitmap, U32 width, U32 height, PixelType pixel_type, const char *name) { return atlasGenTextureEx(src_bitmap, width, height, pixel_type, name, 0); }
void atlasUpdateTexture(AtlasTex *tex, U8 *src_bitmap);
void atlasUpdateTextureFromScreen(AtlasTex *tex, U32 x, U32 y, U32 width, U32 height);
void atlasFreeAll(void);
void atlasPurge(const char *sprite_name);
void atlasTexIsReloaded(const char *sprite_name);
bool atlasTexIsFullyLoaded(const AtlasTex * tex);
AtlasTex *atlasMakeWhiteCopy(const char *name);
TexHandle atlasDemandLoadTexture(AtlasTex *tex);

__forceinline static void atlasGetModifiedUVs(AtlasTex *tex, float u, float v, float *uprime, float *vprime)
{
	if (!tex)
		return;

	if (uprime)
		*uprime = u * tex->u_mult + tex->u_offset;
	if (vprime)
		*vprime = v * tex->v_mult + tex->v_offset;
}

#define atlasDemandLoadTextureByName(pchName) atlasDemandLoadTexture(atlasLoadTexture(pchName))

void atlasPushAllowAsync(bool allowAsync);
void atlasPopAllowAsync(void);

extern AtlasTex *white_tex_atlas;

#endif //_TEXTUREATLAS_H

