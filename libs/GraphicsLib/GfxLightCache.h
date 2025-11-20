/***************************************************************************



***************************************************************************/

#ifndef _GFXLIGHTCACHE_H_
#define _GFXLIGHTCACHE_H_

#include "earray.h"
#include "wlLight.h"
#include "RenderLib.h"
#include "GfxGeo.h"
#include "GfxStaticLights.h"

typedef struct WorldRegion WorldRegion;
typedef struct WorldRegionGraphicsData WorldRegionGraphicsData;
typedef struct WorldDrawableEntry WorldDrawableEntry;
typedef struct WorldVolume WorldVolume;
typedef struct Model Model;
typedef struct Room Room;


typedef struct VertexLightData
{
	RdrVertexLight rdr_vertex_light;
	GeoRenderInfo *geo_render_info;
	U32 *vertex_colors;
	const char *name;
} VertexLightData;

__forceinline static VertexLightData *VertexLightDataFromRdrVertexLight(RdrVertexLight *light)
{
	if (!light)
		return NULL;
	return (VertexLightData *)(((U8 *)light) - OFFSETOF(VertexLightData, rdr_vertex_light));
}

typedef struct GfxLightCacheBase
{
	RdrLightParams light_params;
	LightCacheType cache_type;
	Room **rooms;

	U32 invalidated_bitfield:3;
	U32 is_indoors:1;
	U32 id:24;
	U32 is_static_cache_type:1;
} GfxLightCacheBase;

typedef struct GfxDynObjLightCache
{
	GfxLightCacheBase base;

	Vec3 local_min, local_max;
	Mat4 world_matrix;
	Mat4 inv_world_matrix;
	Vec3 world_min, world_max, world_mid;

	WorldRegionGraphicsData *graphics_data;

	WorldVolume *volume[2];
	bool cur_volume;

} GfxDynObjLightCache;

typedef struct GfxStaticObjLightCache
{
	GfxLightCacheBase base;

	U32 disable_vertex_lighting:1;
	U32 need_vertex_light_update:1;
	U32 disable_updates:1;

	WorldDrawableEntry *entry;


	VertexLightData **lod_vertex_light_data;

	// Temporary, for calculating vertex lighting.  Not used in streaming mode.
	GfxLight **secondary_lights;
	WorldRegion *region;

} GfxStaticObjLightCache;



void gfxForceUpdateLightCaches(SA_PRE_OP_RBYTES(sizeof(Vec3)) const Vec3 bounds_min, SA_PRE_OP_RBYTES(sizeof(Vec3)) const Vec3 bounds_max, const Mat4 world_matrix, 
							   bool update_dynamic_caches, bool update_static_caches,
							   LightCacheInvalidateType invalidate_types, GfxLight* remove_light);

void gfxInvalidateLightCache(GfxLightCacheBase *light_cache, LightCacheInvalidateType invalidate_types);

void gfxInvalidateAllLightCaches(void);

void gfxUpdateIndoorVolume(SA_PRE_NN_RBYTES(sizeof(Vec3)) const Vec3 bounds_min, SA_PRE_NN_RBYTES(sizeof(Vec3)) const Vec3 bounds_max, const Mat4 world_matrix, bool streaming_mode);



//////////////////////////////////////////////////////////////////////////
// no light cache
void gfxGetObjectLightsUncached(SA_PRE_NN_FREE SA_POST_NN_VALID RdrLightParams *light_params, SA_PARAM_OP_VALID WorldRegion *region, 
								SA_PRE_NN_RBYTES(sizeof(Vec3)) const Vec3 world_mid, F32 radius, bool is_character);



//////////////////////////////////////////////////////////////////////////
// dynamic object light cache

void gfxCheckDynLightCacheForModifiedLights(GfxDynObjLightCache * light_cache);
GfxDynObjLightCache *gfxCreateDynLightCache(WorldRegionGraphicsData *graphics_data, 
											const Vec3 local_min, const Vec3 local_max, const Mat4 world_matrix, 
											LightCacheType cache_type);

void gfxUpdateDynLightCachePosition(GfxDynObjLightCache *light_cache, WorldRegionGraphicsData *graphics_data, 
									const Vec3 local_min, const Vec3 local_max, const Mat4 world_matrix);

void gfxUpdateDynLightCache(GfxDynObjLightCache *light_cache);
void gfxFreeDynLightCache(SA_PRE_OP_VALID SA_POST_P_FREE GfxDynObjLightCache *light_cache);


__forceinline static const RdrLightParams *gfxDynLightCacheGetLights(GfxDynObjLightCache *light_cache)
{
	if (light_cache->base.invalidated_bitfield)
		gfxUpdateDynLightCache(light_cache);
	else
		gfxCheckDynLightCacheForModifiedLights(light_cache);


	return &light_cache->base.light_params;
}



//////////////////////////////////////////////////////////////////////////
// static object light cache

GfxStaticObjLightCache *gfxCreateStaticLightCache(WorldDrawableEntry *entry, WorldRegion *region);
void gfxUpdateStaticLightCache(GfxStaticObjLightCache *light_cache);
void gfxFreeStaticLightCache(SA_PRE_OP_VALID SA_POST_P_FREE GfxStaticObjLightCache *light_cache);

typedef struct BinFileList BinFileList;
void gfxComputeStaticLightingForBinning(WorldDrawableEntry *entry, WorldRegion *region, BinFileList *file_list);
void gfxCheckStaticLightCacheForModifiedLights(GfxStaticObjLightCache * light_cache);

__forceinline static const RdrLightParams *gfxStaticLightCacheGetLights(GfxStaticObjLightCache *light_cache, int lod, BinFileList *file_list)
{
	RdrLightParams *light_params = &light_cache->base.light_params;

	if (!light_cache->disable_updates)
	{
		if (light_cache->base.invalidated_bitfield)
			gfxUpdateStaticLightCache(light_cache);
		else
			gfxCheckStaticLightCacheForModifiedLights(light_cache);
	}

	if (light_cache->need_vertex_light_update)
		gfxCreateVertexLightData(light_cache, file_list);

	if (lod >= 0 && lod < eaSize(&light_cache->lod_vertex_light_data) && light_cache->lod_vertex_light_data[lod])
	{
		light_params->vertex_light = &light_cache->lod_vertex_light_data[lod]->rdr_vertex_light;
		if (light_params->vertex_light && !light_params->vertex_light->geo_handle_vertex_light)
		{
			light_params->vertex_light->geo_handle_vertex_light = gfxGeoDemandLoad(light_cache->lod_vertex_light_data[lod]->geo_render_info);
		}
	}
	else
	{
		light_params->vertex_light = NULL;
	}

	return light_params;
}

void gfxRemoveUnusedPointLightsFromLightParams(
	const Mat4 worldMat, const Vec3 min, const Vec3 max,
	RdrLightParams *light_params);

#endif //_GFXLIGHTCACHE_H_
