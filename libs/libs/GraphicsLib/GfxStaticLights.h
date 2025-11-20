#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/

#ifndef _GFXSTATICLIGHTS_H_
#define _GFXSTATICLIGHTS_H_

#define WORLD_CELL_GRAPHICS_VERTEX_LIGHT_MULTIPLIER 10.0f

typedef struct GfxStaticObjLightCache GfxStaticObjLightCache;
typedef struct VertexLightData VertexLightData;
typedef struct WorldDrawableEntry WorldDrawableEntry;
typedef struct BinFileList BinFileList;
typedef struct GMesh GMesh;
typedef struct Model Model;
typedef struct GeoRenderInfo GeoRenderInfo;

extern bool gStaticLightForBin;

void gfxCreateVertexLightData(GfxStaticObjLightCache *light_cache, BinFileList *file_list);
void gfxCreateVertexLightDataStreaming(GfxStaticObjLightCache *light_cache, WorldDrawableEntry *draw_entry);
void gfxFreeVertexLight(VertexLightData *vertex_light);
void gfxCalcVertexLightingForTransformedGMesh(GMesh *mesh);
__forceinline void gfxCreateVertexLightGeoRenderInfo(GeoRenderInfo **renderInfo, GfxStaticObjLightCache *light_cache, const unsigned int lod);

#endif //_GFXSTATICLIGHTS_H_

