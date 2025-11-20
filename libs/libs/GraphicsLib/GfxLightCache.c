/***************************************************************************



***************************************************************************/

#include "GfxLightCache.h"

#include "error.h"
#include "earray.h"
#include "MemoryPool.h"
#include "Octree.h"
#include "qsortG.h"
#include "rgb_hsv.h"
#include "DebugState.h"
#include "SparseGrid.h"
#include "partition_enums.h"

#include "wlVolumes.h"
#include "wlTerrainSource.h"
#include "../StaticWorld/WorldCell.h"
#include "../wlState.h"
#include "RoomConn.h"
#include "WorldBounds.h"

#include "GfxLights.h"
#include "GfxLightsPrivate.h"
#include "GfxStaticLights.h"

#include "GfxWorld.h"
#include "GfxLoadScreens.h"
#include "GfxTerrain.h"
#include "GfxLightDebugger.h"
#include "bounds.h"

#include "memlog.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););


#define TRACE_LIGHT_CACHE_REFS 0

#if TRACK_LIGHT_CACHE_REFS
static __forceinline void gfxLightCacheAddRef(GfxLightCacheBase *light_cache, int light)
{
	if (light_cache->light_params.lights[light])
	{
		GfxLight * gfx_light = GfxLightFromRdrLight(light_cache->light_params.lights[light]);
		++gfx_light->cache_ref_count;
	}
}
#endif

static __forceinline void gfxLightCacheReleaseRef(GfxLightCacheBase *light_cache, int light)
{
#if TRACK_LIGHT_CACHE_REFS
	if (light_cache->light_params.lights[light])
	{
		GfxLight * gfx_light = GfxLightFromRdrLight(light_cache->light_params.lights[light]);
		--gfx_light->cache_ref_count;
		light_cache->light_params.lights[light] = NULL;
	}
#else
	light_cache->light_params.lights[light] = NULL;
#endif
}

static __forceinline void gfxLightCacheSetLight(GfxLightCacheBase *light_cache, int light, GfxLight * gfx_light)
{
#if TRACK_LIGHT_CACHE_REFS
	++gfx_light->cache_ref_count;
	if (light_cache->light_params.lights[light])
	{
		GfxLight * cur_light = GfxLightFromRdrLight(light_cache->light_params.lights[light]);
		--cur_light->cache_ref_count;
		assert(cur_light->cache_ref_count >= 0);
	}
#endif
	light_cache->light_params.lights[light] = gfx_light ? &gfx_light->rdr_light : NULL;
}

static __forceinline int gfxLightCacheContainsLight(const GfxLightCacheBase *light_cache, const GfxLight * gfx_light)
{
	int light;
	for (light = 0; light < ARRAY_SIZE(light_cache->light_params.lights); ++light)
	{
		const RdrLight * rdr_light = light_cache->light_params.lights[light];
		if (rdr_light == &gfx_light->rdr_light)
			return light + 1;
	}

	return 0;
}

#define LIGHT_CACHE_DEBUG 1

static Vec3 debug_light_cache_min = { 7034, -3800, -1261 };
static Vec3 debug_light_cache_max = { 7210, -3700, -1188 };
int bDebugLightCache = 0;

AUTO_CMD_INT(bDebugLightCache,bDebugLightCache) ACMD_CATEGORY(Debug);

// Sets lower bounds of light cache debug region
AUTO_COMMAND ACMD_CATEGORY(Debug);
void debug_set_light_cache_min( F32 x, F32 y, F32 z )
{
	setVec3(debug_light_cache_min, x, y, z);
}

// Sets upper bounds of light cache debug region
AUTO_COMMAND ACMD_CATEGORY(Debug);
void debug_set_light_cache_max( F32 x, F32 y, F32 z )
{
	setVec3(debug_light_cache_max, x, y, z);
}

#if LIGHT_CACHE_DEBUG
static void dumpRdrLight( const RdrLight * rdr_light, int light_num )
{
	OutputDebugStringf("  Light %d: 0x%08p %s Sh:%c @(%f, %f, %f)\n", light_num, rdr_light, 
		rdrLightGetTypeString(rdr_light), 
		rdrIsShadowedLightType(rdr_light->light_type) ? 'Y' : 'N',
		rdr_light->world_mat[3][0], rdr_light->world_mat[3][1], rdr_light->world_mat[3][2]);
}

static void dumpLightCandidates(const GfxStaticObjLightCache *light_cache, const Vec3 world_mid, GfxLight ** eaLights )
{
	int light;
	OutputDebugStringf("LC: 0x%08p @(%f, %f, %f)\n", light_cache, world_mid[0], world_mid[1], world_mid[2]);
	for (light = 0; light < eaSize(&eaLights); ++light)
	{
		const RdrLight * rdr_light = &eaLights[light]->rdr_light;
		if (!rdr_light)
			continue;
		dumpRdrLight( rdr_light, light );
	}
}

static void dumpLightCache(const GfxStaticObjLightCache *light_cache, const Vec3 world_mid)
{
	int light;
	OutputDebugStringf("LC final: 0x%08p @(%f, %f, %f)\n", light_cache, world_mid[0], world_mid[1], world_mid[2]);
	for (light = 0; light < ARRAY_SIZE(light_cache->base.light_params.lights); ++light)
	{
		const RdrLight * rdr_light = light_cache->base.light_params.lights[light];
		if (!rdr_light)
			continue;
		dumpRdrLight( rdr_light, light );
	}
	OutputDebugStringf("LC vtx\n");
	for (light = 0; light < eaSize(&light_cache->secondary_lights); ++light)
	{
		const RdrLight * rdr_light = &light_cache->secondary_lights[light]->rdr_light;
		dumpRdrLight( rdr_light, light );
	}
}

static void dumpDynLightCache(const GfxDynObjLightCache *light_cache, const Vec3 world_mid)
{
	int light;
	OutputDebugStringf("DLC final: 0x%08p @(%f, %f, %f)\n", light_cache, world_mid[0], world_mid[1], world_mid[2]);
	for (light = 0; light < ARRAY_SIZE(light_cache->base.light_params.lights); ++light)
	{
		const RdrLight * rdr_light = light_cache->base.light_params.lights[light];
		if (!rdr_light)
			continue;
		dumpRdrLight( rdr_light, light );
	}
}
#endif



static void gfxUpdateLightCacheRooms(GfxLightCacheBase *light_cache);

#define LIGHT_CACHE_TOLERANCE 0.25f

static U32 occluder_volume_type, light_cache_volume_type, light_cache_query_type;
static int next_light_cache_id = 1, debug_light_cache_id;

static GfxLight **lights_temp, **secondary_lights_temp;
static Room **room_list_temp;

#define LIGHT_INVALIDATE_RAD_FUDGE_FACTOR 1.01f

void gfxSetDebugLightCache(int light_cache_id)
{
	debug_light_cache_id = light_cache_id;
}

static __forceinline bool gfxIsDynLightCache(const GfxLightCacheBase *light_cache)
{
	return !light_cache->is_static_cache_type && 
		(light_cache->cache_type == LCT_CHARACTER || light_cache->cache_type == LCT_INTERACTION_ENTITY || 
		light_cache->cache_type == LCT_TERRAIN);
}

void gfxGetLightCacheMidPoint(const GfxLightCacheBase *light_cache, Vec3 out_mid)
{
	if (gfxIsDynLightCache(light_cache))
	{
		GfxDynObjLightCache *dyn_cache = (GfxDynObjLightCache *)light_cache;
		copyVec3(dyn_cache->world_mid, out_mid);
	}
	else
	{
		GfxStaticObjLightCache *static_cache = (GfxStaticObjLightCache *)light_cache;
		copyVec3(static_cache->entry->base_entry.bounds.world_mid, out_mid);
	}
}


static int lightIDCmp(const GfxLight **light1, const GfxLight **light2)
{
	return (*light1)->id - (*light2)->id;
}

static bool lightCacheIsVertexLight(LightCacheType cache_type, GfxLight* pLight)
{
	return (!pLight->key_light && cache_type != LCT_CHARACTER);
}


#ifdef LIGHTCACHE_ASSERT_NO_DUPES

static void assertNoDupes(
	GfxLight ***lights,
	GfxLight ***secondary_lights,
	RdrLight *rdrLights[MAX_NUM_OBJECT_LIGHTS]) {

	int i;
	int j;

	for(i = 0; i < eaSize(lights); i++) {

		for(j = 0; j < eaSize(lights); j++) {
			if(i != j) {
				assert((*lights)[i] != (*lights)[j]);
			}
		}

		if(secondary_lights) {
			for(j = 0; j < eaSize(secondary_lights); j++) {
				assert((*lights)[i] != (*secondary_lights)[j]);
			}
		}

		if(rdrLights) {
			for(j = 0; j < MAX_NUM_OBJECT_LIGHTS; j++) {
				assert(rdrLights[j] != &((*lights)[i]->rdr_light));
			}
		}

	}
}

#define ASSERT_NO_DUPES(x, y, z) assertNoDupes(x, y, z)
#else
#define ASSERT_NO_DUPES(x, y, z)
#endif

static bool checkLightInBounds(GfxLight *light,const Vec3 local_min, const Vec3 local_max,const Mat4 world_matrix, const Mat4 inv_world_matrix)
{
	if (local_min)
	{
		if(light->dynamic && gfxLightsAllowMovingBBox()) {
			if(!orientBoxBoxCollision(
				   local_min, local_max, world_matrix,
				   light->movingBoundsMin, light->movingBoundsMax, unitmat)) {

				return false;
			}

			return true;
		}
		else if (rdrGetSimpleLightType(light->orig_light_type) == RDRLIGHT_POINT)
		{
			// this is much better than using a bounding box
			if (!sphereOrientBoxCollision(light->rdr_light.world_mat[3], light->rdr_light.point_spot_params.outer_radius, local_min, local_max, world_matrix, inv_world_matrix))
			{
				return false;
			}
		}
		else if (rdrGetSimpleLightType(light->orig_light_type) != RDRLIGHT_DIRECTIONAL)
		{
			if (!orientBoxBoxCollision(local_min, local_max, world_matrix, light->static_entry.bounds.min, light->static_entry.bounds.max, unitmat))
			{
				return false;
			}
		}
	}

	return true;
}

static void getStaticLights(GfxLightCacheBase* light_cache, RdrLightParams *light_params, LightCacheType cache_type, 
							bool is_indoors, Room **rooms,
							WorldGraphicsData* world_data, WorldRegionGraphicsData *graphics_data, GfxLight ***secondary_lights, 
							const Vec3 world_mid, F32 radius, 
							const Vec3 local_min, const Vec3 local_max, 
							const Mat4 world_matrix, const Mat4 inv_world_matrix)
{
	int max_static_lights = (cache_type == LCT_CHARACTER) ? gfx_lighting_options.max_static_lights_per_character : gfx_lighting_options.max_static_lights_per_object;
	int i, index, count, total_count;
	Mat4 inv_world_matrix_stack;
	bool exclude_sun = worldRegionGetNoSkySun(graphics_data->region) && !SAFE_MEMBER(gfx_state.currentAction, gdraw.hide_world);
	bool indoor_sun  = SAFE_MEMBER(gfx_state.currentAction, use_sun_indoors);

	PERFINFO_AUTO_START_FUNC();

	if (!occluder_volume_type)
		occluder_volume_type = wlVolumeTypeNameToBitMask("Occluder");

	if (world_matrix && !inv_world_matrix)
	{
		// calculate the inverse matrix and redirect the pointer to it
		invertMat4Copy(world_matrix, inv_world_matrix_stack);
		inv_world_matrix = inv_world_matrix_stack;
	}

	// Clear lists of lights.

	if (secondary_lights && *secondary_lights)
		eaSetSize(secondary_lights, 0);

	if (!lights_temp)
		eaCreate(&lights_temp); // make sure memory gets tracked to this file
	eaSetSize(&lights_temp, 0);

	// Add sun lights.

	if (!gfx_state.debug.disableDirLights && ((!is_indoors && !exclude_sun) || indoor_sun) && world_data->sun_light)
		eaPush(&lights_temp, world_data->sun_light);
	if (!gfx_state.debug.disableDirLights &&  ((!is_indoors && !exclude_sun) || indoor_sun) && world_data->sun_light_2)
		eaPush(&lights_temp, world_data->sun_light_2);

	if (eaSize(&lights_temp) < max_static_lights || secondary_lights)
	{

		// Add other directional lights.

		if (!gfx_state.debug.disableDirLights)
		{
			for (i = 0; i < eaSize(&graphics_data->dir_lights); ++i)
			{
				if (!graphics_data->dir_lights[i]->dynamic)
					eaPush(&lights_temp, graphics_data->dir_lights[i]);
			}
		}

		ASSERT_NO_DUPES(&lights_temp, secondary_lights, light_params->lights);

		// Add other lights.

		if (graphics_data->static_light_octree)
		{
			if (local_min)
			{
				octreeFindInBoxEA(graphics_data->static_light_octree, &lights_temp, local_min, local_max, world_matrix, inv_world_matrix);
				ASSERT_NO_DUPES(&lights_temp, secondary_lights, light_params->lights);
			}
			else
			{
				assert(!light_cache); //we can only use sphere lookups for uncached lights
				octreeFindInSphereEA(graphics_data->static_light_octree, &lights_temp, world_mid, radius, NULL, NULL);
				ASSERT_NO_DUPES(&lights_temp, secondary_lights, light_par&ams->lights);
			}
		}

		ASSERT_NO_DUPES(&lights_temp, secondary_lights, light_params->lights);
	}

#if LIGHT_CACHE_DEBUG
		if (bDebugLightCache == 2 && cache_type == LCT_WORLD)
		{
			if (pointBoxCollision(world_mid, debug_light_cache_min, debug_light_cache_max))
			{
				dumpLightCandidates((GfxStaticObjLightCache*)light_cache, world_mid, lights_temp);
			}
		}
#endif

	ASSERT_NO_DUPES(&lights_temp, secondary_lights, light_params->lights);

	for (i = 0; i < eaSize(&lights_temp); ++i)
	{
		GfxLight *light = lights_temp[i];
		Room *owner_room;

		if (!isLightOk(light, is_indoors, false))
		{
			eaRemoveFast(&lights_temp, i);
			--i;
			continue;
		}

		if (light->light_affect_type == WL_LIGHTAFFECT_STATIC && cache_type == LCT_CHARACTER)
		{
			eaRemoveFast(&lights_temp, i);
			--i;
			continue;
		}

		if (light->light_affect_type == WL_LIGHTAFFECT_DYNAMIC && cache_type != LCT_CHARACTER)
		{
			eaRemoveFast(&lights_temp, i);
			--i;
			continue;
		}

		owner_room = gfxGetOrUpdateLightOwnerRoom(light);
		if (owner_room && owner_room->limit_contained_lights_to_room)
		{
			if (eaFind(&rooms, owner_room) < 0)
			{
				//the light is limited to it's room and we're not in it
				eaRemoveFast(&lights_temp, i);
				--i;
				continue;
			}
		}

		if (!checkLightInBounds(light,local_min,local_max,world_matrix, inv_world_matrix))
		{
			eaRemoveFast(&lights_temp, i);
			--i;
			continue;
		}
		
		if (cache_type == LCT_CHARACTER && !light->key_light)
		{
			// do occluder ray cast
			if (rdrGetSimpleLightType(light->orig_light_type) == RDRLIGHT_DIRECTIONAL)
			{
				Vec3 ray_start;
				scaleAddVec3(light->rdr_light.world_mat[1], -1000, world_mid, ray_start);
				if (wlVolumeRayCollide(PARTITION_CLIENT, ray_start, world_mid, occluder_volume_type, NULL))
				{
					eaRemoveFast(&lights_temp, i);
					--i;
					continue;
				}
			}
			else if (wlVolumeRayCollide(PARTITION_CLIENT, light->rdr_light.world_mat[3], world_mid, occluder_volume_type, NULL))
			{
				eaRemoveFast(&lights_temp, i);
				--i;
				continue;
			}
		}

		if (lightCacheIsVertexLight(cache_type,light))
		{
			// vertex lights always go in the secondary list
			if (secondary_lights)
				eaPush(secondary_lights, light);

			eaRemoveFast(&lights_temp, i);
			--i;
			continue;
		}
	}

	ASSERT_NO_DUPES(&lights_temp, secondary_lights, light_params->lights);

	sortLights(lights_temp, world_mid);

	ASSERT_NO_DUPES(&lights_temp, secondary_lights, light_params->lights);

#if LIGHT_CACHE_DEBUG
	if (bDebugLightCache == 3 && cache_type == LCT_WORLD)
	{
		if (pointBoxCollision(world_mid, debug_light_cache_min, debug_light_cache_max))
		{
			dumpLightCandidates((GfxStaticObjLightCache*)light_cache, world_mid, lights_temp);
		}
	}
#endif

	total_count = 0;
	for (i = 0; i < ARRAY_SIZE(light_params->lights); ++i)
	{
		if (light_params->lights[i])
			total_count++;
	}

	ASSERT_NO_DUPES(&lights_temp, secondary_lights, light_params->lights);

	count = 0;
	for (i = 0; i < eaSize(&lights_temp); ++i)
	{
		GfxLight *light = lights_temp[i];

		// find empty slot
		for (index = 0; index < ARRAY_SIZE(light_params->lights); ++index)
		{
			if (!light_params->lights[index])
				break;
		}
		if (index >= ARRAY_SIZE(light_params->lights))
			break;

		if ((count + 1 <= max_static_lights) && 
			(total_count + 1 <= gMaxLightsPerObject))
		{
			if (light_cache)
				gfxLightCacheSetLight(light_cache, index, light);
			else
				light_params->lights[index] = &light->rdr_light;
			light->use_counter++;
			
			count++;
			total_count++;

			eaRemove(&lights_temp, i);
			--i;
		}
	}

	if (secondary_lights && eaSize(&lights_temp) && gfx_lighting_options.use_extra_key_lights_as_vertex_lights)
		eaPushEArray(secondary_lights, &lights_temp);

	PERFINFO_AUTO_STOP();
}

static void getDynamicLights(GfxLightCacheBase* light_cache, RdrLightParams *light_params, LightCacheType cache_type, 
							 bool is_indoors, Room **rooms, 
							 WorldGraphicsData* world_data, WorldRegionGraphicsData *graphics_data, 
							 const Vec3 world_mid, F32 radius, 
							 const Vec3 local_min, const Vec3 local_max, 
							 const Mat4 world_matrix, const Mat4 inv_world_matrix)
{
	int i, index, total_count;
	Mat4 inv_world_matrix_stack;

	PERFINFO_AUTO_START_FUNC();

	if (gfx_state.debug.disableDynamicLights) {
		PERFINFO_AUTO_STOP();
		return;
	}

	total_count = 0;
	for (i = 0; i < gMaxLightsPerObject; ++i)
	{
		if (light_params->lights[i])
			total_count++;
	}

	if (!lights_temp)
		eaCreate(&lights_temp); // make sure memory gets tracked to this file
	eaSetSize(&lights_temp, 0);

	if (!gfx_state.debug.disableDirLights)
	{
		for (i = 0; i < eaSize(&graphics_data->dir_lights); ++i)
		{
			if (graphics_data->dir_lights[i]->dynamic)
				eaPush(&lights_temp, graphics_data->dir_lights[i]);
		}
	}

	if (gfxActionAllowsIndoors(gfx_state.currentAction))
	{
		if (local_min)
		{
			sparseGridFindInBoxEA(world_data->dynamic_lights, local_min, local_max, world_matrix, &lights_temp);
		}
		else
		{
			assert(!light_cache); //only non-cached lookups can use sphere tests
			sparseGridFindInSphereEA(world_data->dynamic_lights, world_mid, radius, &lights_temp);
		}
	}

	if (!eaSize(&lights_temp))
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	if (total_count && total_count == gMaxLightsPerObject) {
		int cam_idx = -1;

        // look for a cam light
        for (i = 0; i < eaSize(&lights_temp); ++i) {
            GfxLight *light = lights_temp[i];
            if (light->key_override) {
                cam_idx = i;
                break;
            }
        }

        // the cam light can replace an existing static light.
        // we assume the existing lights are sorted and therefore
        // replace the last light in the list
        if (cam_idx > -1) {
			GfxLight *cam_light = lights_temp[cam_idx];

            if (light_cache) {
				gfxLightCacheSetLight(light_cache, total_count - 1, cam_light);
			} else {
				light_params->lights[total_count - 1] = &cam_light->rdr_light;
			}
        }

		// no need to continue as there are no more slots for other dynamic lights
		PERFINFO_AUTO_STOP();
		return;
	}

	if (world_matrix && !inv_world_matrix)
	{
		// calculate the inverse matrix and redirect the pointer to it
		invertMat4Copy(world_matrix, inv_world_matrix_stack);
		inv_world_matrix = inv_world_matrix_stack;
	}

	// remove disabled lights and lights that are not in the same indoor/outdoor state
	for (i = 0; i < eaSize(&lights_temp); ++i)
	{
		GfxLight *light = lights_temp[i];
		Room *owner_room;

		if (!isLightOk(light, is_indoors, false))
		{
			eaRemoveFast(&lights_temp, i);
			--i;
			continue;
		}

		if (light->light_affect_type == WL_LIGHTAFFECT_STATIC && cache_type == LCT_CHARACTER)
		{
			eaRemoveFast(&lights_temp, i);
			--i;
			continue;
		}
		
		if (light->light_affect_type == WL_LIGHTAFFECT_DYNAMIC && cache_type != LCT_CHARACTER)
		{
			eaRemoveFast(&lights_temp, i);
			--i;
			continue;
		}

		owner_room = gfxGetOrUpdateLightOwnerRoom(light);
		if (owner_room && owner_room->limit_contained_lights_to_room)
		{
			if (eaFind(&rooms, owner_room) < 0)
			{
				//the light is limited to it's room and we're not in it
				eaRemoveFast(&lights_temp, i);
				--i;
				continue;
			}
		}

		if (!checkLightInBounds(light,local_min,local_max,world_matrix,inv_world_matrix))
		{
			eaRemoveFast(&lights_temp, i);
			--i;
			continue;
		}
	}

	sortLights(lights_temp, world_mid);

	for (i = 0; i < eaSize(&lights_temp); ++i)
	{
		GfxLight *light = lights_temp[i];

		// find empty slot
		for (index = 0; index < ARRAY_SIZE(light_params->lights); ++index)
		{
			if (!light_params->lights[index])
				break;
		}
		if (index >= ARRAY_SIZE(light_params->lights))
			break;

		if (total_count < gMaxLightsPerObject)
		{
			if (light_cache)
				gfxLightCacheSetLight(light_cache, index, light);
			else
				light_params->lights[index] = &light->rdr_light;
			light->use_counter++;

			total_count++;
		}
	}

	PERFINFO_AUTO_STOP_FUNC();
}


//////////////////////////////////////////////////////////////////////////
// No light cache
//////////////////////////////////////////////////////////////////////////

void gfxGetObjectLightsUncached(RdrLightParams *light_params, WorldRegion *region, const Vec3 world_mid, F32 radius, bool is_character)
{
	WorldGraphicsData *world_data;
	WorldRegionGraphicsData *region_graphics_data;
	const WorldVolume **eaVolumes;
	bool is_indoors;
	int i;

	ZeroStructForce(light_params);

	if (gfx_state.debug.force_unlit_value)
	{
		gfxGetUnlitLightNoClear(light_params);
		return;
	}

	world_data = worldGetWorldGraphicsData();
	if (!region)
		region = worldGetWorldRegionByPos(world_mid);
	region_graphics_data = worldRegionGetGraphicsData(region);

	light_params->light_color_type = is_character ? RLCT_CHARACTER : RLCT_WORLD;

	is_indoors = (gfxActionAllowsIndoors(gfx_state.currentAction)
				  && !gfx_state.currentAction->gdraw.hide_world && gfxCheckIsSphereIndoors(world_mid, radius, 
											 1.33333333f * PI * radius * radius * radius, 
											 gfxActionUseIndoorAmbient(gfx_state.currentAction) ? &light_params->ambient_light : NULL,
											 NULL, NULL, region));
	
	if (!light_params->ambient_light)
		light_params->ambient_light = world_data->outdoor_ambient_light;

	eaSetSize(&room_list_temp, 0);
	eaVolumes = wlVolumeCacheQuerySphereByType(lights_room_query_cache, world_mid, radius, lights_room_volume_type);
	for (i = 0; i < eaSize(&eaVolumes); ++i)
	{
		WorldVolumeEntry *volume_entry = wlVolumeGetVolumeData(eaVolumes[i]);
		if (volume_entry && volume_entry->room)
			eaPush(&room_list_temp, volume_entry->room);
	}

	getStaticLights(NULL, light_params, is_character ? LCT_CHARACTER : LCT_WORLD_EDITOR, 
					is_indoors, room_list_temp, 
					world_data, region_graphics_data, NULL, 
					world_mid, radius, 
					NULL, NULL, NULL, NULL);

	getDynamicLights(NULL, light_params, is_character ? LCT_CHARACTER : LCT_WORLD_EDITOR, 
					 is_indoors, room_list_temp, 
					 world_data, region_graphics_data, 
					 world_mid, radius, 
					 NULL, NULL, NULL, NULL);

	rdrSortLights(light_params);
}

static void initBaseLightCache(GfxLightCacheBase *light_cache, LightCacheType cache_type)
{
	light_cache->cache_type = cache_type;
	light_cache->light_params.light_color_type = (cache_type == LCT_CHARACTER) ? RLCT_CHARACTER : RLCT_WORLD;
	light_cache->id = next_light_cache_id++;
	light_cache->invalidated_bitfield = LCIT_ALL;
}

__forceinline static void releaseInvalidatedLights(GfxLightCacheBase *light_cache)
{
	int invalidated = light_cache->invalidated_bitfield;
	if ((invalidated & (LCIT_STATIC_LIGHTS|LCIT_DYNAMIC_LIGHTS)) == (LCIT_STATIC_LIGHTS|LCIT_DYNAMIC_LIGHTS))
	{
		int light;
		for (light = 0; light < ARRAY_SIZE(light_cache->light_params.lights); ++light)
			gfxLightCacheReleaseRef(light_cache, light);
	}
	else if (invalidated & LCIT_STATIC_LIGHTS)
	{
		int i;
		for (i = 0; i < ARRAY_SIZE(light_cache->light_params.lights); ++i)
		{
			GfxLight *light = GfxLightFromRdrLight(light_cache->light_params.lights[i]);
			if (light && !light->dynamic)
				gfxLightCacheReleaseRef(light_cache, i);
		}
	}
	else if (invalidated & LCIT_DYNAMIC_LIGHTS)
	{
		int i;
		for (i = 0; i < ARRAY_SIZE(light_cache->light_params.lights); ++i)
		{
			GfxLight *light = GfxLightFromRdrLight(light_cache->light_params.lights[i]);
			if (light && light->dynamic)
				gfxLightCacheReleaseRef(light_cache, i);
		}
	}
}

static bool updateBaseLightCache(GfxLightCacheBase *light_cache, bool *static_lights_invalidated, bool *dynamic_lights_invalidated)
{
	PERFINFO_AUTO_START_FUNC();

	if(light_cache->invalidated_bitfield & LCIT_ROOMS) {
		light_cache->invalidated_bitfield |= LCIT_DYNAMIC_LIGHTS;
		light_cache->invalidated_bitfield |= LCIT_STATIC_LIGHTS;
	}

	*static_lights_invalidated = !!(light_cache->invalidated_bitfield & LCIT_STATIC_LIGHTS);
	*dynamic_lights_invalidated = !!(light_cache->invalidated_bitfield & LCIT_DYNAMIC_LIGHTS);

	if (light_cache->id == debug_light_cache_id)
		printf("Updated debug light cache.\n");

	if (light_cache->invalidated_bitfield & LCIT_ROOMS)
	{
		gfxUpdateLightCacheRooms(light_cache);
		*static_lights_invalidated = true;
		*dynamic_lights_invalidated = true;
	}

	releaseInvalidatedLights(light_cache);

	light_cache->invalidated_bitfield = 0;

	if (gfx_state.debug.force_unlit_value)
	{
		gfxGetUnlitLight(&light_cache->light_params);
		PERFINFO_AUTO_STOP_FUNC();
		return false;
	}

	PERFINFO_AUTO_STOP_FUNC();
	return true;
}

void gfxCheckStaticLightCacheForModifiedLights(GfxStaticObjLightCache * light_cache)
{
	Vec3 world_min, world_max;
	int i;
	bool bounds_calced = false;
	for (i = 0; i < ARRAY_SIZE(light_cache->base.light_params.lights); ++i)
	{
		GfxLight *light = GfxLightFromRdrLight(light_cache->base.light_params.lights[i]);
		if (light && light->rdr_light.light_type & RDRLIGHT_DELETING)
		{
#if TRACE_LIGHT_CACHE_REFS
			char light_desc[ 256 ];
			rdrLightDumpToStr(&light->rdr_light, 0, light_desc, ARRAY_SIZE(light_desc));

			// high bit to indicates the light is being deleted
			if (light_cache->base.id == debug_light_cache_id)
			{
				OutputDebugStringf("Light cache %p dropping light %p %s (%d refs) due to deleted flag\n",
					light_cache, light, light_desc, light->cache_ref_count);
			}
			memlog_printf(0, "Light cache %p dropping light %p %s (%d refs) due to deleted flag\n", 
				light_cache, light, light_desc, light->cache_ref_count);
#endif
			gfxLightCacheReleaseRef(&light_cache->base, i);
			continue;
		}

		devassert(!light || (light->rdr_light.light_type > 0 && light->rdr_light.light_type <= RDRLIGHT_MASK));
		if (light && light->frame_modified == gfx_state.frame_count)
		{
			if (!bounds_calced)
			{
				WorldCellEntry *base_entry;
				base_entry = &light_cache->entry->base_entry;
				mulBoundsAA(base_entry->shared_bounds->local_min, base_entry->shared_bounds->local_max, base_entry->bounds.world_matrix, world_min, world_max);
				bounds_calced = true;
			}
			if (!boxBoxCollision(world_min, world_max, light->static_entry.bounds.min, light->static_entry.bounds.max))
			{
#if TRACE_LIGHT_CACHE_REFS
				if (light_cache->base.id == debug_light_cache_id)
				{
					char light_desc[ 256 ];
					rdrLightDumpToStr(&light->rdr_light, 0, light_desc, ARRAY_SIZE(light_desc));

					OutputDebugStringf("Light cache %p dropping light %p %s (%d refs) due to empty overlap\n", light_cache, light, light_desc, light->cache_ref_count);
				}
#endif
				gfxLightCacheReleaseRef(&light_cache->base, i);
			}
		}
	}
}

void gfxCheckDynLightCacheForModifiedLights(GfxDynObjLightCache * light_cache)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(light_cache->base.light_params.lights); ++i)
	{
		GfxLight *light = GfxLightFromRdrLight(light_cache->base.light_params.lights[i]);

		if (light && light->rdr_light.light_type & RDRLIGHT_DELETING)
		{
#if TRACE_LIGHT_CACHE_REFS
			char light_desc[ 256 ];
			rdrLightDumpToStr(&light->rdr_light, 0, light_desc, ARRAY_SIZE(light_desc));

			// high bit to indicates the light is being deleted
			if (light_cache->base.id == debug_light_cache_id)
			{
				OutputDebugStringf("DynLightCache %p dropping light %p %s (%d refs) due to deleted flag\n",
					light_cache, light, light_desc, light->cache_ref_count);
			}
			memlog_printf(0, "DynLightCache %p dropping light %p %s (%d refs) due to deleted flag\n", 
				light_cache, light, light_desc, light->cache_ref_count);
#endif
			gfxLightCacheReleaseRef(&light_cache->base, i);
			continue;
		}

		devassert(!light || (light->rdr_light.light_type > 0 && light->rdr_light.light_type <= RDRLIGHT_MASK));
		if (light && light->frame_modified == gfx_state.frame_count)
		{
			if (!boxBoxCollision(light_cache->world_min, light_cache->world_max, light->static_entry.bounds.min, light->static_entry.bounds.max))
				gfxLightCacheReleaseRef(&light_cache->base, i);
		}
	}
}


//////////////////////////////////////////////////////////////////////////
// Dynamic light caches
//////////////////////////////////////////////////////////////////////////

MP_DEFINE(GfxDynObjLightCache);
static GfxDynObjLightCache * gfxAllocDOLC()
{
	MP_CREATE(GfxDynObjLightCache, 64);
	return MP_ALLOC(GfxDynObjLightCache);
}
static void gfxFreeDOLC(GfxDynObjLightCache * pCache)
{
	MP_FREE(GfxDynObjLightCache, pCache);
}

GfxDynObjLightCache *gfxCreateDynLightCache(WorldRegionGraphicsData *graphics_data, 
											const Vec3 local_min, const Vec3 local_max, const Mat4 world_matrix, 
											LightCacheType cache_type)
{
	GfxDynObjLightCache *light_cache;

	PERFINFO_AUTO_START_FUNC();

	assert(cache_type == LCT_CHARACTER || cache_type == LCT_TERRAIN || cache_type == LCT_INTERACTION_ENTITY);

	if (!light_cache_volume_type)
		light_cache_volume_type = wlVolumeTypeNameToBitMask("LightCache");

	if (!world_matrix)
		world_matrix = unitmat;

	light_cache = gfxAllocDOLC();
	initBaseLightCache(&light_cache->base, cache_type);

	copyVec3(local_min, light_cache->local_min);
	copyVec3(local_max, light_cache->local_max);
	copyMat4(world_matrix, light_cache->world_matrix);
	invertMat4(world_matrix, light_cache->inv_world_matrix);
	mulBoundsAA(local_min, local_max, world_matrix, light_cache->world_min, light_cache->world_max);
	addVec3(light_cache->world_min, light_cache->world_max, light_cache->world_mid);
	scaleVec3(light_cache->world_mid, 0.5f, light_cache->world_mid);

	if (cache_type == LCT_CHARACTER || cache_type == LCT_INTERACTION_ENTITY)
	{
		light_cache->volume[0] = wlVolumeCreateBox(PARTITION_CLIENT, light_cache_volume_type, light_cache, world_matrix, local_min, local_max, VOLFACE_ALL);
		light_cache->volume[1] = wlVolumeCreateBox(PARTITION_CLIENT, light_cache_volume_type, light_cache, world_matrix, local_min, local_max, VOLFACE_ALL);
	}

	PERFINFO_AUTO_STOP_FUNC();

	return light_cache;
}

void gfxUpdateDynLightCachePosition(GfxDynObjLightCache *light_cache, WorldRegionGraphicsData *graphics_data, 
									const Vec3 local_min, const Vec3 local_max, const Mat4 world_matrix)
{
	WorldGraphicsData *world_data = worldGetWorldGraphicsData();
	bool needs_update = false;

	if (!light_cache || !world_data)
		return;

	if (light_cache->graphics_data != graphics_data)
		needs_update = true;
	if (!needs_update && !nearSameVec3Tol(light_cache->local_min, local_min, LIGHT_CACHE_TOLERANCE))
		needs_update = true;
	if (!needs_update && !nearSameVec3Tol(light_cache->local_max, local_max, LIGHT_CACHE_TOLERANCE))
		needs_update = true;
	if (!needs_update && !nearSameMat4Tol(light_cache->world_matrix, world_matrix, 0.2f, LIGHT_CACHE_TOLERANCE))
		needs_update = true;

	if (needs_update)
	{
		int vol_num;
		copyVec3(local_min, light_cache->local_min);
		copyVec3(local_max, light_cache->local_max);
		copyMat4(world_matrix, light_cache->world_matrix);
		invertMat4(world_matrix, light_cache->inv_world_matrix);
		mulBoundsAA(local_min, local_max, world_matrix, light_cache->world_min, light_cache->world_max);
		addVec3(light_cache->world_min, light_cache->world_max, light_cache->world_mid);
		scaleVec3(light_cache->world_mid, 0.5f, light_cache->world_mid);

		 //use two volumes: current position and previous position. This makes sure that no matter how stuff moves it cache is always found
		light_cache->cur_volume = !light_cache->cur_volume;
		vol_num = light_cache->cur_volume ? 1 : 0;

		if (light_cache->volume[vol_num])
			wlVolumeUpdateBox(light_cache->volume[vol_num], world_matrix, local_min, local_max);

		light_cache->graphics_data = graphics_data;
		light_cache->base.invalidated_bitfield = LCIT_ALL;
	}
}

void gfxUpdateDynLightCache(GfxDynObjLightCache *light_cache)
{
	WorldGraphicsData *world_data = worldGetWorldGraphicsData();
	RdrLightParams *light_params = &light_cache->base.light_params;
	bool is_indoors, static_lights_invalidated, dynamic_lights_invalidated;

	PERFINFO_AUTO_START_FUNC();

	if (!updateBaseLightCache(&light_cache->base, &static_lights_invalidated, &dynamic_lights_invalidated))
	{
		PERFINFO_AUTO_STOP();
		return;
	}
	light_params->ambient_light = NULL;
	is_indoors = (gfxActionAllowsIndoors(gfx_state.currentAction)
				  && gfxCheckIsBoxIndoors(light_cache->world_min,light_cache->world_max, 
						gfxActionUseIndoorAmbient(gfx_state.currentAction) ? &light_params->ambient_light : NULL,
						NULL, NULL, light_cache->graphics_data->region));

	light_cache->base.is_indoors = is_indoors;

	if (!light_params->ambient_light)
		light_params->ambient_light = world_data->outdoor_ambient_light;

	if (static_lights_invalidated)
	{
		getStaticLights(&light_cache->base, light_params, light_cache->base.cache_type, 
						is_indoors, light_cache->base.rooms, 
						world_data, light_cache->graphics_data, NULL, 
						light_cache->world_mid, 0, 
						light_cache->local_min, light_cache->local_max, 
						light_cache->world_matrix, light_cache->inv_world_matrix);
	}

	if (dynamic_lights_invalidated)
	{
		getDynamicLights(&light_cache->base, light_params, light_cache->base.cache_type, 
						 is_indoors, light_cache->base.rooms, 
						 world_data, light_cache->graphics_data, 
						 light_cache->world_mid, 0, 
						 light_cache->local_min, light_cache->local_max, 
						 light_cache->world_matrix, light_cache->inv_world_matrix);
	}

	rdrSortLights(light_params);

#if LIGHT_CACHE_DEBUG
	if (bDebugLightCache == 1)
	{
		if (pointBoxCollision(light_cache->world_mid, debug_light_cache_min, debug_light_cache_max))
		{
			dumpDynLightCache(light_cache, light_cache->world_mid);
		}
	}
#endif

	PERFINFO_AUTO_STOP();
}

void gfxFreeDynLightCache(GfxDynObjLightCache *light_cache)
{
	int light;
	if (!light_cache)
		return;

	PERFINFO_AUTO_START_FUNC();
	gfxLightDebuggerNotifyCacheDestroy(&light_cache->base);
	for (light = 0; light < ARRAY_SIZE(light_cache->base.light_params.lights); ++light)
		gfxLightCacheReleaseRef(&light_cache->base, light);

	wlVolumeFree(light_cache->volume[0]);
	wlVolumeFree(light_cache->volume[1]);
	light_cache->volume[0] = NULL;
	light_cache->volume[1] = NULL;

	eaDestroy(&light_cache->base.rooms);

	gfxFreeDOLC(light_cache);

	PERFINFO_AUTO_STOP_FUNC();
}



//////////////////////////////////////////////////////////////////////////
// Static light caches
//////////////////////////////////////////////////////////////////////////

MP_DEFINE(GfxStaticObjLightCache);
static GfxStaticObjLightCache * gfxAllocSOLC()
{
	MP_CREATE(GfxStaticObjLightCache, 64);
	return MP_ALLOC(GfxStaticObjLightCache);
}
static void gfxFreeSOLC(GfxStaticObjLightCache * pCache)
{
	MP_FREE(GfxStaticObjLightCache, pCache);
}

void gfxCBCheckLCB(MemoryPool pool, void *data, void *userData)
{
#if TRACE_LIGHT_CACHE_REFS
	GfxLightCacheBase * pSOLC = (GfxLightCacheBase*)data;
	GfxLight * pLight = (GfxLight*)userData;
	int which_light = gfxLightCacheContainsLight(pSOLC, pLight);
	if (which_light)
	{
		OutputDebugStringf("Found bad LC ref 0x%p[ %d ] -> %p", pSOLC, which_light - 1, pLight);
	}
#endif
}

void gfxCheckAllCachesForLight(GfxLight * pLight)
{
#if TRACE_LIGHT_CACHE_REFS
	mpForEachAllocation(MP_NAME(GfxDynObjLightCache), gfxCBCheckLCB, pLight);
	mpForEachAllocation(MP_NAME(GfxStaticObjLightCache), gfxCBCheckLCB, pLight);
#endif
}

GfxStaticObjLightCache *gfxCreateStaticLightCache(WorldDrawableEntry *entry, WorldRegion *region)
{
	GfxStaticObjLightCache *light_cache;
	const Model *model;
	int lod_idx;
	LightCacheType cache_type;

	PERFINFO_AUTO_START_FUNC();

	if (entry->editor_only)
		cache_type = LCT_WORLD_EDITOR;
	else if (gStaticLightForBin)
		cache_type = LCT_WORLD_BINNING;
	else
	if (entry->use_character_lighting)
		cache_type = LCT_CHARACTER;
	else
		cache_type = LCT_WORLD;

	model = worldDrawableEntryGetModel(entry, &lod_idx, NULL, NULL, NULL);

	if (model && !entry->base_entry.streaming_mode)
	{
		ModelLOD *model_lod = modelLoadLOD(model, lod_idx);
		if (cache_type == LCT_WORLD_BINNING)
			modelLODRequestBackgroundLoad(model_lod);
	}

	light_cache = gfxAllocSOLC();
	light_cache->entry = entry;
	light_cache->region = region;
	light_cache->disable_vertex_lighting = (region->bDisableVertexLighting || entry->no_vertex_lighting);
	light_cache->base.is_static_cache_type = 1;

	initBaseLightCache(&light_cache->base, cache_type);

  	if (entry->base_entry.streaming_mode && entry->lod_vertex_light_colors)
		gfxCreateVertexLightDataStreaming(light_cache, entry);

	PERFINFO_AUTO_STOP_FUNC();

	return light_cache;
}

void gfxUpdateStaticLightCache(GfxStaticObjLightCache *light_cache)
{
	WorldRegionGraphicsData *region_graphics_data;
	WorldCellEntryData *base_entry_data;
	WorldGraphicsData *world_data;
	WorldCellEntry *base_entry;
	RdrLightParams *light_params;
	bool is_indoors, use_vertex_lights;
	bool static_lights_invalidated, dynamic_lights_invalidated;
	int i;
	Vec3 world_min, world_max;

	PERFINFO_AUTO_START_FUNC();

	if (!updateBaseLightCache(&light_cache->base, &static_lights_invalidated, &dynamic_lights_invalidated))
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	light_params = &light_cache->base.light_params;
	base_entry = &light_cache->entry->base_entry;

	world_data = worldGetWorldGraphicsData();
	base_entry_data = worldCellEntryGetData(base_entry);
	region_graphics_data = worldRegionGetGraphicsData(light_cache->region);


	light_params->ambient_light = NULL;
	mulBoundsAA(base_entry->shared_bounds->local_min, base_entry->shared_bounds->local_max, base_entry->bounds.world_matrix, world_min, world_max);
	is_indoors = (gfxActionAllowsIndoors(gfx_state.currentAction)
				  && gfxCheckIsBoxIndoors(world_min, world_max,
				  gfxActionUseIndoorAmbient(gfx_state.currentAction) ? &light_params->ambient_light : NULL,
				  NULL, NULL, light_cache->region));
	
	light_cache->base.is_indoors = is_indoors;

	if (!light_params->ambient_light)
		light_params->ambient_light = world_data->outdoor_ambient_light;

	use_vertex_lights = !base_entry->streaming_mode && light_cache->base.cache_type != LCT_WORLD_EDITOR && !light_cache->disable_vertex_lighting;

	if (static_lights_invalidated)
	{
		if (use_vertex_lights)
			eaSetSize(&secondary_lights_temp, 0);

		getStaticLights(&light_cache->base, light_params, light_cache->base.cache_type, 
						is_indoors, light_cache->base.rooms, 
						world_data, region_graphics_data, use_vertex_lights ? &secondary_lights_temp : NULL, 
						base_entry->bounds.world_mid, base_entry->shared_bounds->radius, 
						base_entry->shared_bounds->local_min, base_entry->shared_bounds->local_max, 
						base_entry->bounds.world_matrix, NULL);
	}

	if (dynamic_lights_invalidated)
	{
		getDynamicLights(&light_cache->base, light_params, light_cache->base.cache_type, 
						 is_indoors, light_cache->base.rooms, 
						 world_data, region_graphics_data, 
						 base_entry->bounds.world_mid, base_entry->shared_bounds->radius, 
						 base_entry->shared_bounds->local_min, base_entry->shared_bounds->local_max, 
						 base_entry->bounds.world_matrix, NULL);
	}

	rdrSortLights(light_params);

	if (static_lights_invalidated && use_vertex_lights)
	{
		eaQSortG(secondary_lights_temp, lightIDCmp);

		if (eaSize(&secondary_lights_temp) != eaSize(&light_cache->secondary_lights))
		{
			light_cache->need_vertex_light_update = true;
		}
		else
		{
			for (i = 0; i < eaSize(&secondary_lights_temp); ++i)
			{
				ANALYSIS_ASSUME(light_cache->secondary_lights);
				if (secondary_lights_temp[i]->id != light_cache->secondary_lights[i]->id)
				{
					light_cache->need_vertex_light_update = true;
					break;
				}
			}
		}

		if (light_cache->need_vertex_light_update)
		{

			eaSetSize(&light_cache->secondary_lights, eaSize(&secondary_lights_temp));
			memcpy(light_cache->secondary_lights, secondary_lights_temp, eaSize(&secondary_lights_temp) * sizeof(GfxLight *));

			for (i = 0; i < eaSize(&light_cache->secondary_lights); ++i)
			{
				updateRdrLightColors(light_cache->secondary_lights[i], world_data);
			}
		}
	}

#if LIGHT_CACHE_DEBUG
	if (bDebugLightCache == 1)
	{
		if (pointBoxCollision(base_entry->bounds.world_mid, debug_light_cache_min, debug_light_cache_max))
		{
			dumpLightCache(light_cache, base_entry->bounds.world_mid);
		}
	}
#endif

	PERFINFO_AUTO_STOP();
}

void gfxFreeStaticLightCache(GfxStaticObjLightCache *light_cache)
{
	int light;
	if (!light_cache)
		return;

	gfxLightDebuggerNotifyCacheDestroy(&light_cache->base);
	for (light = 0; light < ARRAY_SIZE(light_cache->base.light_params.lights); ++light)
		gfxLightCacheReleaseRef(&light_cache->base, light);

	light_cache->base.light_params.vertex_light = NULL;

	eaDestroy(&light_cache->base.rooms);

	eaDestroy(&light_cache->secondary_lights);
	eaDestroyEx(&light_cache->lod_vertex_light_data, gfxFreeVertexLight);

	gfxFreeSOLC(light_cache);
}


static void waitForLightCacheUpdated(GfxStaticObjLightCache *light_cache, BinFileList *file_list)
{
	const RdrLightParams *light_params;
	for (light_params = gfxStaticLightCacheGetLights(light_cache, -1, file_list); light_cache->need_vertex_light_update; light_params = gfxStaticLightCacheGetLights(light_cache, -1, file_list))
	{
		geoForceBackgroundLoaderToFinish();
	}
}

void gfxComputeStaticLightingForBinning(WorldDrawableEntry *entry, WorldRegion *region, BinFileList *file_list)
{
	bool force_outdoors = false, no_lights = false;
	GfxStaticObjLightCache *light_cache;
	int i;

	assert(!entry->lod_vertex_light_colors);

	if (entry->base_entry.type != WCENT_MODEL && entry->base_entry.type != WCENT_SPLINE)
		return;

	PERFINFO_AUTO_START_FUNC();

	gStaticLightForBin = true;
	light_cache = gfxCreateStaticLightCache(entry, region);
	ASSERT_COMPLETES(240, waitForLightCacheUpdated(light_cache, file_list));
	gStaticLightForBin = false;

	for (i = 0; i < eaSize(&light_cache->lod_vertex_light_data); i++)
	{
		WorldDrawableEntryVertexLightColors* vert_cols;
		
		if (light_cache->lod_vertex_light_data[i] == NULL)
			continue;

		vert_cols = calloc(1,sizeof(WorldDrawableEntryVertexLightColors));
		vert_cols->multipler = light_cache->lod_vertex_light_data[i]->rdr_vertex_light.vlight_multiplier;
		vert_cols->offset = light_cache->lod_vertex_light_data[i]->rdr_vertex_light.vlight_offset;
		ea32PushArray(&vert_cols->vertex_light_colors, &light_cache->lod_vertex_light_data[i]->vertex_colors);

		eaPush(&entry->lod_vertex_light_colors, vert_cols);
	}
	
	entry->light_cache = light_cache;
	light_cache->disable_updates = true;

	PERFINFO_AUTO_STOP_FUNC();
}

//////////////////////////////////////////////////////////////////////////


void gfxInvalidateLightCache(GfxLightCacheBase *light_cache, LightCacheInvalidateType invalidate_types)
{
	light_cache->invalidated_bitfield |= invalidate_types;
	releaseInvalidatedLights(light_cache);
}

static void lightCacheForceRemoveLight(GfxLightCacheBase *light_cache, GfxLight* remove_light)
{
	int i;
	RdrLight** light_list = light_cache->light_params.lights;

	if (lightCacheIsVertexLight(light_cache->cache_type,remove_light))
	{
		// I cannot remove this type of light here. I need to let the update function take care of it
		light_cache->invalidated_bitfield |= LCIT_STATIC_LIGHTS;
		return;
	}
	
	for(i = 0; i < MAX_NUM_OBJECT_LIGHTS; i++)
	{
		if (light_list[i] == &remove_light->rdr_light)
		{
			gfxLightCacheReleaseRef(light_cache, i);

			light_cache->invalidated_bitfield |= (LCIT_STATIC_LIGHTS | LCIT_DYNAMIC_LIGHTS);
		}
	}
}

static void staticLightCacheForceRemoveLight(GfxStaticObjLightCache *light_cache, GfxLight* remove_light)
{
	if (lightCacheIsVertexLight(light_cache->base.cache_type,remove_light))
	{
		// I cannot remove this type of light here. I need to let the update function take care of it
		light_cache->base.invalidated_bitfield |= LCIT_STATIC_LIGHTS;
		return;
	}

	lightCacheForceRemoveLight(&light_cache->base, remove_light);
	if (eaFindAndRemoveFast(&light_cache->secondary_lights, remove_light) != -1)
	{
		light_cache->base.invalidated_bitfield |= (LCIT_STATIC_LIGHTS | LCIT_DYNAMIC_LIGHTS);
	}
}

/* DJR - enable for debugging light cache problems
int debug_clear_all_cells = 1;
AUTO_CMD_INT(debug_clear_all_cells,debug_clear_all_cells);
int forced_all_cells = 0;
*/

static void invalidateDrawableLightCaches(WorldCell *cell, const Vec3 world_min, const Vec3 world_max, 
										  const Vec3 local_min, const Vec3 local_max, 
										  const Mat4 world_matrix, const Mat4 inv_world_matrix, 
										  LightCacheInvalidateType invalidate_types, GfxLight* remove_light)
{
	int i, j;
	int cell_box_coll_failed = 0;

	if (!cell)
		return;

	// check overlap with cell
	if (!boxBoxCollision(world_min, world_max, cell->bounds.world_min, cell->bounds.world_max))
	{
		return;

		/* DJR - enable for debugging light cache problems
		if (!debug_clear_all_cells)
			return;
		cell_box_coll_failed = 1;
		++forced_all_cells;
		*/
	}

	for (i = 0; i < eaSize(&cell->drawable.drawable_entries); ++i)
	{
		WorldDrawableEntry *entry = cell->drawable.drawable_entries[i];
		GfxStaticObjLightCache *light_cache = entry->light_cache;
		if (light_cache && (((light_cache->base.invalidated_bitfield & invalidate_types) != invalidate_types) || remove_light))
		{
			if (orientBoxBoxCollision(entry->base_entry.shared_bounds->local_min, entry->base_entry.shared_bounds->local_max, 
									  entry->base_entry.bounds.world_matrix, 
									  local_min, local_max, world_matrix))
			{
#if TRACE_LIGHT_CACHE_REFS
				/* DJR - enable for debugging light cache problems
				if (forced_all_cells && gfxLightCacheContainsLight(&light_cache->base, remove_light))
				{
					char light_desc[ 256 ];
					rdrLightDumpToStr(&remove_light->rdr_light, 0, light_desc, ARRAY_SIZE(light_desc));
					memlog_printf(0, "Static light cache (0x%p) for entry (0x%p) still contains light 0x%p %s but parent cell bounds don't intersect\n", 
						light_cache, entry, remove_light, light_desc);
				}
				*/
#endif

				if (remove_light)
					staticLightCacheForceRemoveLight(light_cache, remove_light);
				else
					light_cache->base.invalidated_bitfield |= invalidate_types;
			}
#if TRACE_LIGHT_CACHE_REFS
			else
			if (gfxLightCacheContainsLight(&light_cache->base, remove_light))
			{
				char light_desc[ 256 ];
				rdrLightDumpToStr(&remove_light->rdr_light, 0, light_desc, ARRAY_SIZE(light_desc));
				memlog_printf(0, "Static light cache (0x%p) for entry (0x%p) still contains light 0x%p %s but bounds don't intersect\n", 
					light_cache, entry, remove_light, light_desc);
			}
#endif
		}
		

	}

	for (i = 0; i < eaSize(&cell->drawable.near_fade_entries); ++i)
	{
		WorldDrawableEntry *entry = cell->drawable.near_fade_entries[i];
		GfxStaticObjLightCache *light_cache = entry->light_cache;
		if (light_cache && (((light_cache->base.invalidated_bitfield & invalidate_types) != invalidate_types) || remove_light))
		{
			if (orientBoxBoxCollision(entry->base_entry.shared_bounds->local_min, entry->base_entry.shared_bounds->local_max, 
									  entry->base_entry.bounds.world_matrix, 
									  local_min, local_max, world_matrix))
			{
				if (remove_light)
					staticLightCacheForceRemoveLight(light_cache, remove_light);
				else
					light_cache->base.invalidated_bitfield |= invalidate_types;
			}
		}
	}

	for (i = 0; i < eaSize(&cell->drawable.editor_only_entries); ++i)
	{
		WorldDrawableEntry *entry = cell->drawable.editor_only_entries[i];
		GfxStaticObjLightCache *light_cache = entry->light_cache;
		if (light_cache && (((light_cache->base.invalidated_bitfield & invalidate_types) != invalidate_types) || remove_light))
		{
			if (orientBoxBoxCollision(entry->base_entry.shared_bounds->local_min, entry->base_entry.shared_bounds->local_max, 
									  entry->base_entry.bounds.world_matrix, 
									  local_min, local_max, world_matrix))
			{
				if (remove_light)
					staticLightCacheForceRemoveLight(light_cache, remove_light);
				else
					light_cache->base.invalidated_bitfield |= invalidate_types;
			}
		}
	}

	for (j = 0; j < eaSize(&cell->drawable.bins); ++j)
	{
		for (i = 0; i < eaSize(&cell->drawable.bins[j]->drawable_entries); ++i)
		{
			WorldDrawableEntry *entry = cell->drawable.bins[j]->drawable_entries[i];
			GfxStaticObjLightCache *light_cache = entry->light_cache;
			if (light_cache && (((light_cache->base.invalidated_bitfield & invalidate_types) != invalidate_types) || remove_light))
			{
				if (orientBoxBoxCollision(entry->base_entry.shared_bounds->local_min, entry->base_entry.shared_bounds->local_max, 
										  entry->base_entry.bounds.world_matrix, 
										  local_min, local_max, world_matrix))
				{
					if (remove_light)
						staticLightCacheForceRemoveLight(light_cache, remove_light);
					else
						light_cache->base.invalidated_bitfield |= invalidate_types;
				}
			}
		}
	}

	for (i = 0; i < ARRAY_SIZE(cell->children); ++i)
		invalidateDrawableLightCaches(cell->children[i], world_min, world_max, local_min, local_max, world_matrix, inv_world_matrix, invalidate_types, remove_light);

	/* DJR - enable for debugging light cache problems
	if (cell_box_coll_failed)
		--forced_all_cells;
	*/
}

static void invalidateTerrainLightCaches(HeightMapAtlas *atlas, const Vec3 world_min, const Vec3 world_max, 
										 const Vec3 local_min, const Vec3 local_max, 
										 const Mat4 world_matrix,
										 LightCacheInvalidateType invalidate_types, GfxLight* remove_light)
{
	GfxDynObjLightCache *light_cache;
	int i;

	if (!atlas)
		return;

	light_cache = SAFE_MEMBER(atlas->data, client_data.light_cache);
	if (light_cache && (((light_cache->base.invalidated_bitfield & invalidate_types) != invalidate_types) || remove_light))
	{
		if (orientBoxBoxCollision(light_cache->local_min, light_cache->local_max, 
								  light_cache->world_matrix, 
								  local_min, local_max, world_matrix))
		{
			if (remove_light)
				lightCacheForceRemoveLight(&light_cache->base, remove_light);
			else
				light_cache->base.invalidated_bitfield |= invalidate_types;

			// Mark atlas as needing vertex light calculations. (Only
			// actually calculated on source meshes in the editor.)
			atlas->needs_static_light_update = true;
		}
	}

	if (!atlas->atlas_active)
	{
		for (i = 0; i < ARRAY_SIZE(atlas->children); ++i)
			invalidateTerrainLightCaches(atlas->children[i], world_min, world_max, local_min, local_max, world_matrix, invalidate_types, remove_light);
	}
}

void gfxForceUpdateLightCaches(const Vec3 bounds_min, const Vec3 bounds_max, const Mat4 world_matrix, 
							   bool update_dynamic_caches, bool update_static_caches,
							   LightCacheInvalidateType invalidate_types, GfxLight* remove_light)
{
	static Vec3 all_bounds_min = {-FLT_MAX_WORLD, -FLT_MAX_WORLD, -FLT_MAX_WORLD}, all_bounds_max = {FLT_MAX_WORLD, FLT_MAX_WORLD, FLT_MAX_WORLD};

	PERFINFO_AUTO_START_FUNC();

	if (!bounds_min)
		bounds_min = all_bounds_min;
	if (!bounds_max)
		bounds_max = all_bounds_max;
	if (!world_matrix)
		world_matrix = unitmat;


	if (update_dynamic_caches || remove_light)
	{
		WorldVolumeQueryCache *query_cache;
		const WorldVolume **volumes;
		int i;
		WorldRegion **all_regions = worldGetAllWorldRegions();
		Vec3 world_min, world_max;

		if (!light_cache_volume_type)
			light_cache_volume_type = wlVolumeTypeNameToBitMask("LightCache");

		if (!light_cache_query_type)
			light_cache_query_type = wlVolumeQueryCacheTypeNameToBitMask("LightCache");

		// invalidate dynamic light caches (LCT_CHARACTER and LCT_INTERACTION_ENTITY)
		query_cache = wlVolumeQueryCacheCreate(PARTITION_CLIENT, light_cache_query_type, NULL);
		volumes = wlVolumeCacheQueryBoxByType(query_cache, world_matrix, bounds_min, bounds_max, light_cache_volume_type);
		for (i = 0; i < eaSize(&volumes); ++i)
		{
			GfxDynObjLightCache *light_cache = wlVolumeGetVolumeData(volumes[i]);
			if (remove_light)
				lightCacheForceRemoveLight(&light_cache->base, remove_light);
			else
				light_cache->base.invalidated_bitfield |= invalidate_types;
		}
		wlVolumeQueryCacheFree(query_cache);

		//update terrain
		mulBoundsAA(bounds_min, bounds_max, world_matrix, world_min, world_max);

		for (i = 0; i < eaSize(&all_regions); ++i)
		{
			WorldRegion *region = all_regions[i];
			int j;
			// invalidate terrain light caches (LCT_TERRAIN)
			if (region->atlases)
			{
				for (j = 0; j < eaSize(&region->atlases->root_atlases); ++j)
				{
					invalidateTerrainLightCaches(region->atlases->root_atlases[j], world_min, world_max, 
						bounds_min, bounds_max, world_matrix, 
						invalidate_types, remove_light);
				}
			}
		}

		#ifndef NO_EDITORS
		{
			//check for terrain trackers that are being edited
			if (terrain_state.source_data)
			{
				for (i = 0; i < eaSize(&terrain_state.source_data->layers); i++)
				{
					TerrainEditorSourceLayer *layer = terrain_state.source_data->layers[i];
					int idx;
					if (layer->loaded)
					{
						if ((idx = eaFind(&all_regions, zmapGetWorldRegionByName(NULL, layer->layer->region_name))) >= 0 &&
							eaSize(&layer->heightmap_trackers) > 0)
						{
							int j;
							for (j = eaSize(&layer->heightmap_trackers) - 1; j >= 0; --j)
							{
								if (layer->heightmap_trackers[j])
								{
									invalidateTerrainLightCaches(layer->heightmap_trackers[j]->atlas, world_min, world_max, 
																 bounds_min, bounds_max, world_matrix, 
																 invalidate_types, remove_light);
								}
							}
						}
					}
				}
			}
		}
		#endif

	}


	if (update_static_caches || remove_light)
	{
		WorldRegion **all_regions = worldGetAllWorldRegions();
		Vec3 world_min, world_max;
		Mat4 inv_world_matrix;
		int i;

		invertMat4(world_matrix, inv_world_matrix);
		mulBoundsAA(bounds_min, bounds_max, world_matrix, world_min, world_max);

		for (i = 0; i < eaSize(&all_regions); ++i)
		{
			WorldRegion *region = all_regions[i];
			
			// invalidate static world light caches (LCT_WORLD and LCT_WORLD_EDITOR)
			invalidateDrawableLightCaches(region->root_world_cell, world_min, world_max, 
										  bounds_min, bounds_max, world_matrix, inv_world_matrix, 
										  invalidate_types, remove_light);
			invalidateDrawableLightCaches(region->temp_world_cell, world_min, world_max, 
										  bounds_min, bounds_max, world_matrix, inv_world_matrix, 
										  invalidate_types, remove_light);
		}
	}

	PERFINFO_AUTO_STOP();
}

void gfxUpdateIndoorVolume(const Vec3 bounds_min, const Vec3 bounds_max, const Mat4 world_matrix, bool streaming_mode)
{
	WorldGraphicsData *world_data = worldGetWorldGraphicsData();
	WorldRegionGraphicsData *graphics_data;
	Vec3 local_mid, world_mid;
	F32 radius;
	int i;

	PERFINFO_AUTO_START_FUNC();

	radius = boxCalcMid(bounds_min, bounds_max, local_mid);
	mulVecMat4(local_mid, world_matrix, world_mid);

	graphics_data = worldRegionGetGraphicsData(worldGetWorldRegionByPos(world_mid));

	if (!graphics_data)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	if (streaming_mode)
	{
		bool changed = false;

		eaSetSize(&lights_temp, 0);
		sparseGridFindInBoxEA(world_data->dynamic_lights, bounds_min, bounds_max, world_matrix, &lights_temp);

		for (i = 0; i < eaSize(&graphics_data->dir_lights); ++i)
		{
			if (graphics_data->dir_lights[i]->dynamic)
				eaPush(&lights_temp, graphics_data->dir_lights[i]);
		}

		// update the indoor bit on all dynamic lights within the bounds
		for (i = 0; i < eaSize(&lights_temp); ++i)
		{
			bool old_indoors = lights_temp[i]->indoors;
			setLightIndoors(lights_temp[i]);
			if (old_indoors != lights_temp[i]->indoors)
				changed = true;
		}

		// update all dynamic light caches within the bounds (must update even if no lights changed to change the caches' indoor states)
		gfxForceUpdateLightCaches(bounds_min, bounds_max, world_matrix, true, false, LCIT_DYNAMIC_LIGHTS | LCIT_STATIC_LIGHTS, NULL);

		// update all dynamic lights on static light caches within the bounds
		if (changed)
			gfxForceUpdateLightCaches(bounds_min, bounds_max, world_matrix, false, true, LCIT_DYNAMIC_LIGHTS, NULL);
	}
	else
	{
		eaSetSize(&lights_temp, 0);
		sparseGridFindInBoxEA(world_data->dynamic_lights, bounds_min, bounds_max, world_matrix, &lights_temp);

		eaPushEArray(&lights_temp, &graphics_data->dir_lights);

		if (graphics_data->static_light_octree)
			octreeFindInSphereEA(graphics_data->static_light_octree, &lights_temp, world_mid, radius, NULL, NULL);

		// update the indoor bit on all lights within the bounds
		for (i = 0; i < eaSize(&lights_temp); ++i)
			setLightIndoors(lights_temp[i]);

		// update all light caches within the bounds
		gfxForceUpdateLightCaches(bounds_min, bounds_max, world_matrix, true, true, LCIT_DYNAMIC_LIGHTS | LCIT_STATIC_LIGHTS, NULL);
	}

	PERFINFO_AUTO_STOP();
}

// TODO DJR permanently enable, remove option
static int lightAllowAllContactedRooms = 1;
AUTO_CMD_INT(lightAllowAllContactedRooms, lightAllowAllContactedRooms);

static void gfxUpdateLightCacheRooms(GfxLightCacheBase *light_cache)
{
	const WorldVolume** eaVolumes;

	PERFINFO_AUTO_START_FUNC();

	//update the room pointers
	eaSetSize(&light_cache->rooms, 0);

	if (!gfxActionAllowsIndoors(gfx_state.currentAction))
	{
		eaVolumes = NULL;
	}
	//if it's a character we want the box, otherwise just use the midpoint to prevent wall pieces from accidentally getting other rooms
	else if (!light_cache->is_static_cache_type && light_cache->cache_type == LCT_CHARACTER)
	{
		GfxDynObjLightCache *dyn_light_cache = (GfxDynObjLightCache *)light_cache;
		Mat4 tempMat;
		Vec3 tempMin, tempMax, tempCenter, addOffset;

		copyMat4(dyn_light_cache->world_matrix, tempMat);

		addVec3(dyn_light_cache->local_min, dyn_light_cache->local_max, tempCenter);

		scaleVec3(tempCenter, 0.5f, tempCenter);
		mulVecMat4(tempCenter, tempMat, addOffset);
		addVec3(tempMat[3], addOffset, tempMat[3]);
		subVec3(dyn_light_cache->local_min, tempCenter, tempMin);
		subVec3(dyn_light_cache->local_max, tempCenter, tempMax);

		scaleMat3(tempMat, tempMat, 0.5f); //shrink the bounding box a little so you don't get lights through a wall you walk by

		eaVolumes = wlVolumeCacheQueryBoxByType(lights_room_query_cache, tempMat, tempMin, tempMax, lights_room_volume_type);
	}
	else if (!light_cache->is_static_cache_type && (light_cache->cache_type == LCT_INTERACTION_ENTITY || light_cache->cache_type == LCT_TERRAIN))
	{
		GfxDynObjLightCache *dyn_light_cache = (GfxDynObjLightCache *)light_cache;
		eaVolumes = wlVolumeCacheQuerySphereByType(lights_room_query_cache, dyn_light_cache->world_mid, 
			lightAllowAllContactedRooms ? distance3(dyn_light_cache->world_min, dyn_light_cache->world_mid) : 0.0f,
			lights_room_volume_type);
	}
	else
	{
		WorldDrawableEntry *entry = ((GfxStaticObjLightCache *)light_cache)->entry;
		eaVolumes = wlVolumeCacheQuerySphereByType(lights_room_query_cache, entry->base_entry.bounds.world_mid, 
			lightAllowAllContactedRooms ? entry->base_entry.shared_bounds->radius : 0.0f,
			lights_room_volume_type);
	}

	FOR_EACH_IN_EARRAY(eaVolumes, const WorldVolume, vol);
	{
		WorldVolumeEntry *volume_entry = wlVolumeGetVolumeData(vol);
		if (volume_entry && volume_entry->room)
		{
			eaPush(&light_cache->rooms, volume_entry->room);
		}
	}
	FOR_EACH_END;

	light_cache->invalidated_bitfield &= ~LCIT_ROOMS;

	PERFINFO_AUTO_STOP_FUNC();
}

// Clears all light caches, causing them to get refilled.
AUTO_COMMAND ACMD_NAME(recalcLights) ACMD_CATEGORY(Performance) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void gfxInvalidateAllLightCaches(void)
{
	gfxForceUpdateLightCaches(NULL, NULL, NULL, true, true, LCIT_ALL, NULL);
}

void gfxRemoveUnusedPointLightsFromLightParams(
	const Mat4 worldMat, const Vec3 min, const Vec3 max,
	RdrLightParams *light_params) {

	PERFINFO_AUTO_START_FUNC();

	if(gfxLightsAllowMovingBBox()) {

		int i;
		for(i = 0; i < ARRAY_SIZE(light_params->lights); i++) {

			if(light_params->lights[i]) {

				RdrLight *light = light_params->lights[i];

				if(light->light_type == RDRLIGHT_POINT) {

					if(!sphereOrientBoxCollision(
						   light->world_mat[3],
						   light->point_spot_params.outer_radius,
						   min, max, worldMat,
						   NULL)) {

						light_params->lights[i] = NULL;
					}

				} else if(light->light_type == RDRLIGHT_PROJECTOR ||
						  light->light_type == RDRLIGHT_SPOT) {

					Frustum w = {0};
					int result = 0;
					Mat4 axisFlipMat;
					Mat4 flippedWorldMat;

					// We need to rotate around the X axis, or swap Y/Z. Light seems to
					// point along negative Y axis but frustum expects Z.
					copyMat4(unitmat, axisFlipMat);
					axisFlipMat[1][1] = 0.0f;
					axisFlipMat[2][1] = 1.0f;
					axisFlipMat[1][2] = -1.0f;
					axisFlipMat[2][2] = 0.0f;
					mulMat4(light->world_mat, axisFlipMat, flippedWorldMat);

					frustumSet(
						&w, DEG(light->point_spot_params.outer_cone_angle * 2.0f),
						1.0f, 0.00001f, light->point_spot_params.outer_radius);

					frustumSetCameraMatrix(
						&w, flippedWorldMat);

					result = frustumCheckBoxWorld(
						&w, (FRUSTUM_CLIP_NONE - 1),
						min, max,
						worldMat,
						false);

					if(!result) {
						light_params->lights[i] = NULL;
					}
				}
			}
		}
	}

	PERFINFO_AUTO_STOP_FUNC();
}


