/***************************************************************************



***************************************************************************/

#include "MemoryPool.h"
#include "memlog.h"
#include "partition_enums.h"
#include "qsortG.h"
#include "rgb_hsv.h"
#include "SparseGrid.h"
#include "rand.h"
#include "simplexNoise.h"
#include "mathutil.inl"

#include "GfxLights.h"
#include "GfxLightCache.h"
#include "GfxLightsPrivate.h"
#include "GfxDeferredLighting.h"
#include "GfxStaticLights.h"
#include "GfxShadows.h"
#include "GfxShadowSearch.h"
#include "GfxDeferredShadows.h"
#include "GfxPrimitive.h"
#include "GfxOcclusion.h"
#include "GfxTexturesInline.h"
#include "GfxWorld.h"
#include "GfxGeo.h"
#include "GfxDrawFrame.h"
#include "GfxTerrain.h"
#include "GfxConsole.h"
#include "GfxCamera.h"
#include "GfxFont.h"
#include "GfxSky.h"
#include "RdrShader.h"
#include "bounds.h"

#include "../StaticWorld/WorldCell.h"
#include "RoomConn.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

#define DEBUG_SHADOW_TRANSITIONS 0

#define LIGHT_CACHE_TOLERANCE 0.25f
#define MAX_SHADOWMAPS_PER_FRAME (gfx_state.currentAction->gdraw.do_shadow_buffer?gfx_state.settings.maxShadowedLights:1)		// not to exceed 4, unless some of the lights do not overlap in screen space

#define MIN_LIGHT_VALUE -20
#define MAX_LIGHT_VALUE 20

#define TRACE_LIGHT_CACHE_REFS 0

static F32 shadow_transition_speed = 0.25f;
AUTO_CMD_FLOAT(shadow_transition_speed, shadow_transition_speed);

GfxLightingOptions gfx_lighting_options = {0};
WorldVolumeQueryCache *lights_room_query_cache;
int lights_room_volume_type;
int gMaxLightsPerObject = 2;
int use_new_light_selector = 1;

static SimplexNoiseTable *shadow_pulse_table;
static GfxLight **queued_light_frees;
static U32 last_world_volume_timestamp = (U32)-1;

MP_DEFINE(GfxLight);


// Applies the character light offsets from the sky file to all lights instead of just to the sun.
AUTO_CMD_INT(gfx_lighting_options.apply_character_light_offsets_globally, apply_character_light_offsets_globally) ACMD_CATEGORY(Debug);

static int next_light_id = 1;
static int debug_light_id = 0;


//toggle the new lighting code on or off
AUTO_COMMAND ACMD_NAME(use_new_light_selector) ACMD_CATEGORY(Debug);
void set_use_new_light_selector(int val, Cmd* cmd, CmdContext* cmd_context)
{
	use_new_light_selector = val;
	disable_shadow_caster_graph = val ? 0 : 1;
	gfxClearShadowSearchData();
	gfxInvalidateAllLightCaches();
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(use_new_light_selector);
void get_use_new_light_selector(void)
{
	conPrintf("use_new_light_selector: %i", use_new_light_selector);
}

void gfxSetDebugLight(int light_id)
{
	debug_light_id = light_id;
}

AUTO_RUN_SECOND;
void gfxSetDefaultLightingOptions(void)
{
	if (gfx_lighting_options.max_static_lights_per_object > 0)
	{
		// validate options
		assert(gfx_lighting_options.max_static_lights_per_object <= 2);
		assert(gfx_lighting_options.max_static_lights_per_character > 0);
		assert(gfx_lighting_options.max_static_lights_per_character <= 5);
		assert(gfx_lighting_options.max_shadowed_lights_per_frame <= MAX_SHADOWED_LIGHTS_PER_FRAME);
		return;
	}

	gfx_lighting_options.max_static_lights_per_object = 2;
	gfx_lighting_options.max_static_lights_per_character = 3;
	gfx_lighting_options.max_shadowed_lights_per_frame = 2;
	gfx_lighting_options.use_extra_key_lights_as_vertex_lights = false;
	gfx_lighting_options.bRequireProjectorLights = true;
}

void gfxEnableDiffuseWarp()
{
	gfx_lighting_options.enableDiffuseWarpTex = 1;
	materialEnableDiffuseWarp();
}

AUTO_CMD_INT(gfx_lighting_options.bLockSpecularToDiffuseColor, light_lock_specular_color) ACMD_CATEGORY(Debug);

void gfxLightingLockSpecularToDiffuseColor(int bLock)
{
	gfx_lighting_options.bLockSpecularToDiffuseColor = bLock;
}

__forceinline static bool rdrLightUseCharacterLightOffsets(GfxLight *light, RdrLightColorType rlct )
{
	// For headshots, we want to use exclusively the sky lighting and
	// no other global state.
	if( gfx_state.currentAction && gfx_state.currentAction->action_type == GfxAction_Headshot ) {
		return false;
	}

	return rlct != RLCT_WORLD && (light->is_sun || gfx_lighting_options.apply_character_light_offsets_globally);	
}

void updateRdrLightColors(GfxLight *light, WorldGraphicsData *world_data)
{
	int i;

	PERFINFO_AUTO_START_FUNC_L2();

	for (i = 0; i < RLCT_COUNT; ++i)
	{
		
		if (!rdrLightUseCharacterLightOffsets(light, i))
		{
			if (gfxFeatureEnabled(GFEATURE_LINEARLIGHTING)) {
				hsvToLinearRgb(light->light_colors.hsv_ambient, light->rdr_light.light_colors[i].ambient);
				hsvToLinearRgb(light->light_colors.hsv_diffuse, light->rdr_light.light_colors[i].diffuse);
				hsvToLinearRgb(light->light_colors.hsv_specular, light->rdr_light.light_colors[i].specular);
				hsvToLinearRgb(light->light_colors.hsv_secondary_diffuse, light->rdr_light.light_colors[i].secondary_diffuse);
				hsvToLinearRgb(light->light_colors.hsv_shadow_color, light->rdr_light.light_colors[i].shadow_color);
			} else {
				hsvToRgb(light->light_colors.hsv_ambient, light->rdr_light.light_colors[i].ambient);
				hsvToRgb(light->light_colors.hsv_diffuse, light->rdr_light.light_colors[i].diffuse);
				hsvToRgb(light->light_colors.hsv_specular, light->rdr_light.light_colors[i].specular);
				hsvToRgb(light->light_colors.hsv_secondary_diffuse, light->rdr_light.light_colors[i].secondary_diffuse);
				hsvToRgb(light->light_colors.hsv_shadow_color, light->rdr_light.light_colors[i].shadow_color);
			}
		}
		else
		{
			Vec3 hsv_temp[5];

			addVec3(light->light_colors.hsv_ambient, world_data->character_ambient_light_offset_hsv, hsv_temp[0]);
			hsvMakeLegal(hsv_temp[0], false);

			addVec3(light->light_colors.hsv_diffuse, world_data->character_diffuse_light_offset_hsv, hsv_temp[1]);
			hsvMakeLegal(hsv_temp[1], false);

			addVec3(light->light_colors.hsv_specular, world_data->character_specular_light_offset_hsv, hsv_temp[2]);
			hsvMakeLegal(hsv_temp[2], false);

			addVec3(light->light_colors.hsv_secondary_diffuse, world_data->character_secondary_diffuse_light_offset_hsv, hsv_temp[3]);
			hsvMakeLegal(hsv_temp[3], false);

			addVec3(light->light_colors.hsv_shadow_color, world_data->character_shadow_color_light_offset_hsv, hsv_temp[4]);
			hsvMakeLegal(hsv_temp[4], false);

			if (gfxFeatureEnabled(GFEATURE_LINEARLIGHTING)) {
				hsvToLinearRgb(hsv_temp[0], light->rdr_light.light_colors[i].ambient);
				hsvToLinearRgb(hsv_temp[1], light->rdr_light.light_colors[i].diffuse);
				hsvToLinearRgb(hsv_temp[2], light->rdr_light.light_colors[i].specular);
				hsvToLinearRgb(hsv_temp[3], light->rdr_light.light_colors[i].secondary_diffuse);
				hsvToLinearRgb(hsv_temp[4], light->rdr_light.light_colors[i].shadow_color);
			} else {
				hsvToRgb(hsv_temp[0], light->rdr_light.light_colors[i].ambient);
				hsvToRgb(hsv_temp[1], light->rdr_light.light_colors[i].diffuse);
				hsvToRgb(hsv_temp[2], light->rdr_light.light_colors[i].specular);
				hsvToRgb(hsv_temp[3], light->rdr_light.light_colors[i].secondary_diffuse);
				hsvToRgb(hsv_temp[4], light->rdr_light.light_colors[i].shadow_color);
			}
		}

		// For headshots, we do not want to pulsate the light ever,
		// the picture should ALWAYS look exactly the same.
		if (gfx_state.currentAction && gfx_state.currentAction->action_type == GfxAction_Headshot)
		{
			light->rdr_light.light_colors[i].min_shadow_val = light->light_colors.min_shadow_val;
			light->rdr_light.light_colors[i].max_shadow_val = light->light_colors.max_shadow_val;
		}
		else
		{
			light->rdr_light.light_colors[i].min_shadow_val = lerp(light->light_colors.min_shadow_val, light->light_colors.shadow_fade_val, MAX(light->shadow_fade, light->shadow_pulse));
			light->rdr_light.light_colors[i].max_shadow_val = lerp(light->light_colors.max_shadow_val, light->light_colors.shadow_fade_val, MAX(light->shadow_fade, light->shadow_pulse));
		}

	}

	PERFINFO_AUTO_STOP_L2();
}

__forceinline static void updateLightDisabled(GfxLight *light)
{
	light->disabled = false;

	if (light->tracker)
	{
		GroupTracker *tracker = light->tracker;
		while (tracker)
		{
			if (tracker->invisible)
			{
				light->disabled = true;
				break;
			}
			tracker = tracker->parent;
		}
	}
}

F32 calcProjectorRgn(GfxLight* light, const Vec3 world_mid)
{
	if (light->orig_outer_cone_angle || light->orig_outer_cone_angle2)
	{
		Vec3 lightspace_pos, lightdir_ws;
		F32 light_falloff_term;
		F32 light_inv_dradius = 1.f / (light->rdr_light.point_spot_params.outer_radius - light->rdr_light.point_spot_params.inner_radius);
		subVec3(light->rdr_light.world_mat[3], world_mid, lightdir_ws);
		mulVecMat3(lightdir_ws, light->inv_world_mat, lightspace_pos);

		if (lightspace_pos[1] <= 0)
			return 0;

		light_falloff_term = 1.0 - saturate((lightspace_pos[1] - light->rdr_light.point_spot_params.inner_radius) * light_inv_dradius);
		if (light_falloff_term)
		{
			Vec2 projected_pos;
			F32 scale = 1.f / lightspace_pos[1];
			setVec2(projected_pos, lightspace_pos[0] * scale, lightspace_pos[2] * scale);

			light_falloff_term *= interp(light->rdr_light.projector_params.angular_falloff[1], light->rdr_light.projector_params.angular_falloff[0], ABS(projected_pos[0]));
			light_falloff_term *= interp(light->rdr_light.projector_params.angular_falloff[3], light->rdr_light.projector_params.angular_falloff[2], ABS(projected_pos[1]));
		}
		return light_falloff_term;
	}
	else
	{
		return 0;
	}
}


//1 = inside, 0 = outside, in between = on edge
F32 calcPointSpotRgn(GfxLight* light, const Vec3 world_mid, F32 d)
{
	RdrLight *rlight = &light->rdr_light;

	if (rlight->point_spot_params.outer_radius)
	{
		F32 r;
		if (d < rlight->point_spot_params.inner_radius)
		{
			r = 1;
		}
		else if (d < rlight->point_spot_params.outer_radius)
		{
			r = 1 - saturate((d - rlight->point_spot_params.inner_radius) / (rlight->point_spot_params.outer_radius - rlight->point_spot_params.inner_radius));
		}
		else
		{
			r = 0;
		}
		if (rdrGetSimpleLightType(rlight->light_type) == RDRLIGHT_SPOT)
		{
			Vec3 light_vec, dir;
			F32 cos_angle, angle;
		
			subVec3(rlight->world_mat[3], world_mid, light_vec);
			normalVec3(light_vec);
			scaleVec3(rlight->world_mat[1], -1, dir);
			cos_angle = -dotVec3(light_vec, dir);
			angle = acosf_clamped(cos_angle);
			
			if (angle < rlight->point_spot_params.inner_cone_angle)
			{
				//we are inside so leave r alone
			}
			else if (angle < rlight->point_spot_params.outer_cone_angle)
			{
				r *= 1 - saturate((angle - rlight->point_spot_params.inner_cone_angle) / (rlight->point_spot_params.outer_cone_angle - rlight->point_spot_params.inner_cone_angle));
			}
			else
			{
				r = 0;
			}
		}
		return r;
	}
	else
	{
		return 0;
	}
}

static void calcLightSortParam(GfxLight *light, const Vec3 world_mid)
{
	// figure out light's contribution to the object, weighting colored lights heavier
	RdrLightType simple_light_type;
	RdrLight *rlight = &light->rdr_light;
	F32 dist; 
	PERFINFO_AUTO_START_FUNC_L2();

	dist = distance3(world_mid, light->world_query_mid);
	light->nondeferred_sort_param = ABS(light->light_colors.hsv_ambient[2]) + ABS(light->light_colors.hsv_diffuse[2]) * (1 + light->light_colors.hsv_diffuse[1]);
	setRdrLightType(light);
	simple_light_type = rdrGetSimpleLightType(light->orig_light_type);
	if (simple_light_type == RDRLIGHT_DIRECTIONAL)
	{
		light->nondeferred_sort_param *= 1e5f;
	}
	else if (simple_light_type == RDRLIGHT_PROJECTOR) //make projectors slightly more important than other lights since they are usually used as main lights
	{
		if (light->orig_outer_cone_angle || light->orig_outer_cone_angle2)
		{
			F32 r;
			F32 region = calcProjectorRgn(light, world_mid);
			if (region)
			{
				r = 6.0f * MAXF(region,0.5f);
			}
			else
			{
				r = 4.0f / (1.0f + MAXF(dist - rlight->point_spot_params.outer_radius, 0.0f));
			}
			light->nondeferred_sort_param *= r * ((log2(round(rlight->point_spot_params.outer_radius))+1)*0.5f + 0.5f);
		}
		else
		{
			light->nondeferred_sort_param = 0.0f;
		}
	}
	else
	{
		// attenuate
		if (rlight->point_spot_params.outer_radius)
		{
			F32 r;
			F32 region = calcPointSpotRgn(light, world_mid, dist);
			if (region)
			{
				r = 3.0f * MAXF(region,0.05f);
			}
			else
			{
				r = 1.5f / (1.0f + MAXF(dist - rlight->point_spot_params.outer_radius, 0.0f));
			}
			
			if (simple_light_type == RDRLIGHT_SPOT)
				r *= 2.0f;

			light->nondeferred_sort_param *= r * ((log2(round(rlight->point_spot_params.outer_radius))+1)*0.5f + 0.5f);
		}
		else
		{
			light->nondeferred_sort_param = 0.0f;
		}
	}

	if (simple_light_type != RDRLIGHT_DIRECTIONAL)
	{
		//boost the priority of shadow casters since they are almost always more important
		if (rdrIsShadowedLightType(light->orig_light_type))
		{
			light->nondeferred_sort_param *= 1.5f;
		}

		//apply the attenuation based on distance more strongly since its basically the most important thing
		light->nondeferred_sort_param *= 1.0f/(0.5f + dist);

		//LM: disable this for now, since the light caches update way less often now this causes problems
		/*
		if (light->dynamic && light->frame_visible != gfx_state.client_frame_timestamp)
		{
			// is this a good idea?  it will cause light popping when turning the camera.
			// maybe only do this for lights that are much smaller than the object.
			light->nondeferred_sort_param *= 0.1f;
		}
		*/
	}

	if (light->dynamic)
		light->nondeferred_sort_param *= 0.5f;

	MAX1F(light->nondeferred_sort_param, 0);

	PERFINFO_AUTO_STOP_L2();
}

//LDM: These are here so that the changes can be on a toggle until we can make sure they dont break any stuff
static void calcLightSortParam_OldVersion(GfxLight *light, const Vec3 world_mid)
{
	// figure out light's contribution to the object, weighting colored lights heavier
	RdrLightType simple_light_type;
	RdrLight *rlight = &light->rdr_light;

	PERFINFO_AUTO_START_FUNC_L2();

	light->nondeferred_sort_param = ABS(light->light_colors.hsv_ambient[2]) + ABS(light->light_colors.hsv_diffuse[2]) * (1 + light->light_colors.hsv_diffuse[1]);
	setRdrLightType(light);
	simple_light_type = rdrGetSimpleLightType(light->orig_light_type);
	if (simple_light_type == RDRLIGHT_DIRECTIONAL)
	{
		light->nondeferred_sort_param *= 1e5;
	}
	else
	{
		// attenuate
		if (rlight->point_spot_params.outer_radius)
		{
			F32 r = distance3(world_mid, rlight->world_mat[3]);
			if (r < rlight->point_spot_params.inner_radius)
			{
				r = 2;
			}
			else if (r < rlight->point_spot_params.outer_radius)
			{
				r = 2 - saturate((r - rlight->point_spot_params.inner_radius) / (rlight->point_spot_params.outer_radius - rlight->point_spot_params.inner_radius));
			}
			else
			{
				r = 1 / (1 + r - rlight->point_spot_params.outer_radius);
			}
			light->nondeferred_sort_param *= r * (log2(round(rlight->point_spot_params.outer_radius))+1);
		}
		else
		{
			light->nondeferred_sort_param = 0;
		}
	}

	if (light->dynamic && light->frame_visible != gfx_state.client_frame_timestamp)
	{
		// is this a good idea?  it will cause light popping when turning the camera.
		// maybe only do this for lights that are much smaller than the object.
		light->nondeferred_sort_param *= 0.1f;
	}

	PERFINFO_AUTO_STOP_L2();
}

void sortLights(GfxLight **lights, const Vec3 world_mid)
{
	int i, j;

	PERFINFO_AUTO_START_FUNC_L2();

	// sort
	if (use_new_light_selector)
	{
		for (i = 0; i < eaSize(&lights); ++i)
			calcLightSortParam(lights[i], world_mid);
	}
	else
	{
		for (i = 0; i < eaSize(&lights); ++i)
			calcLightSortParam_OldVersion(lights[i], world_mid);
	}
	
	for (i = 0; i < eaSize(&lights); ++i)
	{
		for (j = i+1; j < eaSize(&lights); ++j)
		{
			if (cmpLightsNonDeferred(&(lights[i]), &(lights[j])) > 0)
				SWAPP(lights[i], lights[j]);
		}
	}

	PERFINFO_AUTO_STOP_L2();
}

void gfxLightsStartup(void)
{
	if (!lights_room_query_cache)
	{
		lights_room_query_cache = wlVolumeQueryCacheCreate(PARTITION_CLIENT, 0, NULL);
		lights_room_volume_type = wlVolumeTypeNameToBitMask("RoomVolume");
	}
}

// Stores set of lights freed when still referenced by light caches.
GfxLight ** eaBadLights = NULL;
// Limits of "leaked" lights before assertion.
static const int MAX_RUNTIME_BAD_LIGHTS = 100;

void gfxLightsDataOncePerFrame(void)
{
	int i;
	WorldGraphicsData *world_data = worldGetWorldGraphicsData();

	PERFINFO_AUTO_START_FUNC();

	if (gfx_state.settings.bloomQuality == GBLOOM_MAX_BLOOM_DEFERRED)
		gfxUpdateDeferredLightModels();

	for (i = 0; i < eaSize(&queued_light_frees); ++i)
	{
#if TRACK_LIGHT_CACHE_REFS
		GfxLight *light = queued_light_frees[i];
		if (light->cache_ref_count)
		{
			char light_desc[ 256 ];
			rdrLightDumpToStr(&light->rdr_light, 0, light_desc, ARRAY_SIZE(light_desc));

			// don't free the light, it's still referenced, but
			// tag it as not allowed so we can assert on the remaining client
#if TRACE_LIGHT_CACHE_REFS
			memlog_printf(NULL, "Skipping delete of light still referenced by a cache! 0x%p %d refs RdrLight 0x%p %s Frm %d\n", light, light->cache_ref_count, &light->rdr_light, light_desc, gfx_state.frame_count);
#endif
			eaPush(&eaBadLights, light);
			assert(eaSize(&eaBadLights) < MAX_RUNTIME_BAD_LIGHTS);

			// also set a high bit to force the light type out of the allowed range
			light->rdr_light.light_type |= RDRLIGHT_DELETING;
			continue;
		}
		else
		if (light->rdr_light.light_type & RDRLIGHT_DELETING)
		{
			char light_desc[ 256 ];
			rdrLightDumpToStr(&light->rdr_light, 0, light_desc, ARRAY_SIZE(light_desc));

#if TRACE_LIGHT_CACHE_REFS
			memlog_printf(NULL, "Delayed deleting of light! 0x%p RdrLight 0x%p %s Frm %d\n", light, &light->rdr_light, light_desc, gfx_state.frame_count);
#endif
		}
#endif
		gfxFreeShadows(queued_light_frees[i]);
		MP_FREE(GfxLight, queued_light_frees[i]);
	}
	eaSetSize(&queued_light_frees, 0);
	eaPushEArray(&queued_light_frees, &eaBadLights);
	eaSetSize(&eaBadLights, 0);

	for (i = 0; i < eaSize(&world_data->override_lights); ++i)
		world_data->override_lights[i]->light_type = RDRLIGHT_NONE;

	world_data->used_override_ambient_lights = 0;

	last_world_volume_timestamp = wlVolumeGetWorldVolumeStateTimestamp(PARTITION_CLIENT);
	PERFINFO_AUTO_STOP_FUNC();
}

static bool testLightOccluded(GfxLight *light, GfxOcclusionBuffer *occlusion_buffer, Frustum *frustum)
{
	int nearClipped;
	Vec4_aligned eye_bounds[8];

	mulBounds(light->static_entry.bounds.min, light->static_entry.bounds.max, frustum->viewmat, eye_bounds);

	nearClipped = frustumCheckBoxNearClipped(frustum, eye_bounds);
	if (nearClipped == 2)
	{
		return true;
	}
	else if (frustumCheckBoxNearClippedInView(frustum,light->static_entry.bounds.min,light->static_entry.bounds.max, frustum->viewmat))
	{
		return false;
	}
	else if (!zoTestBounds(occlusion_buffer, eye_bounds, 1, &light->occlusion_data.last_update_time, &light->occlusion_data.occluded_bits, NULL, NULL, NULL))
	{
		return true;
	}

	return false;
}

static void updateRdrFalloffParams(RdrLight *rdr_light, 
								   F32 inner_radius, F32 outer_radius, 
								   F32 inner_cone_angle, F32 outer_cone_angle, 
								   F32 inner_cone_angle2, F32 outer_cone_angle2, 
								   const Vec3 scale)
{
	RdrLightType light_type = rdrGetSimpleLightType(rdr_light->light_type);

	if (light_type == RDRLIGHT_PROJECTOR)
	{
		inner_radius *= scale[1];
		outer_radius *= scale[1];
		inner_cone_angle *= scale[0];
		outer_cone_angle *= scale[0];
		inner_cone_angle2 *= scale[2];
		outer_cone_angle2 *= scale[2];

		setVec4(rdr_light->projector_params.angular_falloff, 
			tanf(inner_cone_angle), tanf(outer_cone_angle),
			tanf(inner_cone_angle2), tanf(outer_cone_angle2));

		MIN1(inner_cone_angle, inner_cone_angle2);
		MAX1(outer_cone_angle, outer_cone_angle2);
	}
	else if (light_type == RDRLIGHT_SPOT)
	{
		F32 max_scale = MAX(scale[0], scale[2]);

		inner_radius *= scale[1];
		outer_radius *= scale[1];
		inner_cone_angle *= max_scale;
		outer_cone_angle *= max_scale;
	}
	else if (light_type == RDRLIGHT_POINT)
	{
		F32 max_scale = MAX(scale[0], scale[1]);
		MAX1(max_scale, scale[2]);

		inner_radius *= max_scale;
		outer_radius *= max_scale;
	}

	rdr_light->point_spot_params.inner_radius = inner_radius;
	rdr_light->point_spot_params.outer_radius = outer_radius;
	rdr_light->point_spot_params.inner_cone_angle = inner_cone_angle;
	rdr_light->point_spot_params.outer_cone_angle = outer_cone_angle;

	setVec2(rdr_light->point_spot_params.angular_falloff, 
		cosf(inner_cone_angle),
		cosf(outer_cone_angle));
}

static bool updateLightPreDraw(GfxLight *light, bool is_indoors, WorldGraphicsData *world_data)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	RdrLight *rlight = &light->rdr_light;
	F32 time_diff, t;
	Vec2 new_pos;
	int bounce_count = 0;
	RdrLightType simple_light_type;

	light->last_uses = light->use_counter;
	light->use_counter = 0;

	updateLightDisabled(light);

	if (!isLightOk(light, is_indoors, false))
		return false;

	if (!shadow_pulse_table)
		shadow_pulse_table = simplexNoiseTableCreate(256);

	time_diff = gfx_state.client_loop_timer - light->shadow_pulse_update_time;
	light->shadow_pulse_update_time = gfx_state.client_loop_timer;

	if (light->shadow_transition_rate)
	{
		light->shadow_transition += light->shadow_transition_rate * time_diff;
		if (light->shadow_transition <= 0.0f)
		{
#if DEBUG_SHADOW_TRANSITIONS
			OutputDebugStringf("End fade out 0x%p @(%f, %f, %f)\n", light,
				light->static_entry.bounds.mid[0], light->static_entry.bounds.mid[1], light->static_entry.bounds.mid[2]);
#endif
			light->shadow_transition = 0.0f;
			light->shadow_transition_rate = 0.0f;
		}
		else
		if (light->shadow_transition >= 1.0f)
		{
#if DEBUG_SHADOW_TRANSITIONS
			OutputDebugStringf("End fade in 0x%p @(%f, %f, %f)\n", light,
				light->static_entry.bounds.mid[0], light->static_entry.bounds.mid[1], light->static_entry.bounds.mid[2]);
#endif
			light->shadow_transition = 1.0f;
			light->shadow_transition_rate = 0.0f;
		}
	}


	t = time_diff * light->shadow_pulse_rate;
	scaleAddVec2(light->shadow_pulse_dir, t, light->shadow_pulse_pos, new_pos);
	while (t > 0 && (new_pos[0] < 0 || new_pos[0] > 1 || new_pos[1] < 0 || new_pos[1] > 1))
	{
		// bounce position off the "walls" of the [0,1] space
		F32 tx = t, ty = t;

		++bounce_count;
		if (bounce_count > 5)
		{
			new_pos[0] = randomPositiveF32();
			new_pos[1] = randomPositiveF32();
			break;
		}

		if (new_pos[0] < 0)
			tx = t * light->shadow_pulse_pos[0] / (light->shadow_pulse_pos[0] - new_pos[0]);
		else if (new_pos[0] > 1)
			tx = t * (1 - light->shadow_pulse_pos[0]) / (new_pos[0] - light->shadow_pulse_pos[0]);

		if (new_pos[1] < 0)
			ty = t * light->shadow_pulse_pos[1] / (light->shadow_pulse_pos[1] - new_pos[1]);
		else if (new_pos[1] > 1)
			ty = t * (1 - light->shadow_pulse_pos[1]) / (new_pos[1] - light->shadow_pulse_pos[1]);

		if (tx < ty && FINITE(tx))
		{
			scaleAddVec2(light->shadow_pulse_dir, tx, light->shadow_pulse_pos, light->shadow_pulse_pos);
			light->shadow_pulse_pos[0] = saturate(light->shadow_pulse_pos[0]);
			light->shadow_pulse_dir[0] = -light->shadow_pulse_dir[0];

			t -= tx;
			scaleAddVec2(light->shadow_pulse_dir, t, light->shadow_pulse_pos, new_pos);
		}
		else if (FINITE(ty))
		{
			scaleAddVec2(light->shadow_pulse_dir, ty, light->shadow_pulse_pos, light->shadow_pulse_pos);
			light->shadow_pulse_pos[1] = saturate(light->shadow_pulse_pos[1]);
			light->shadow_pulse_dir[1] = -light->shadow_pulse_dir[1];

			t -= ty;
			scaleAddVec2(light->shadow_pulse_dir, t, light->shadow_pulse_pos, new_pos);
		}
		else
		{
			new_pos[0] = randomPositiveF32();
			new_pos[1] = randomPositiveF32();
			break;
		}
	}

	if (new_pos[0] < -2 || new_pos[0] > 2 || new_pos[1] < -2 || new_pos[1] > 2)
	{
		new_pos[0] = randomPositiveF32();
		new_pos[1] = randomPositiveF32();
	}

	copyVec2(new_pos, light->shadow_pulse_pos);
	light->shadow_pulse = light->shadow_pulse_amplitude * (0.5f + 0.5f * simplexNoise2D(shadow_pulse_table, light->shadow_pulse_pos[0], light->shadow_pulse_pos[1]));

	if (gfx_state.currentCameraController->override_disable_shadow_pulse)
		light->shadow_pulse = 0;

	if (light->animation_controller)
	{
		Vec3 scale;
		if (light->animation_controller->last_update_timestamp != gfx_state.client_frame_timestamp)
			worldAnimationEntryUpdate(light->animation_controller, gfx_state.client_loop_timer, gfx_state.client_frame_timestamp);
		if (light->animation_controller->animation_properties.local_space)
			mulMat4Inline(light->controller_relative_matrix, light->animation_controller->full_matrix, rlight->world_mat);
		else
			mulMat4Inline(light->animation_controller->full_matrix, light->controller_relative_matrix, rlight->world_mat);
		extractScale(rlight->world_mat, scale);
		updateRdrFalloffParams(rlight, light->orig_inner_radius, light->orig_outer_radius,
							   light->orig_inner_cone_angle, light->orig_outer_cone_angle,
							   light->orig_inner_cone_angle2, light->orig_outer_cone_angle2,
							   scale);
		addVec3(gdraw->pos_offset, rlight->world_mat[3], rlight->world_mat[3]);
	}
	else
	{
		addVec3(gdraw->pos_offset, light->controller_relative_matrix[3], rlight->world_mat[3]);
	}

	zeroVec3(light->rdr_light.shadow_mask);

	setRdrLightType(light);
	simple_light_type = rdrGetSimpleLightType(rlight->light_type);
	switch (simple_light_type)
	{
		xcase RDRLIGHT_POINT:
		case RDRLIGHT_SPOT:
		case RDRLIGHT_PROJECTOR:
		{
			F32 fade_dist, dist;
			dist = calcLightDist(gdraw->visual_pass_view_z, rlight->world_mat[3]) - rlight->point_spot_params.outer_radius;
			light->deferred_sort_param = dist;

			fade_dist = light->static_entry.bounds.vis_dist * 0.1f;
			MIN1(fade_dist, 50); // max out at 50 foot fade distance
			rlight->fade_out = 1 - saturate((dist - (light->static_entry.bounds.vis_dist - fade_dist)) / fade_dist);

			if (!gfx_state.debug.noZocclusion && gdraw->occlusion_buffer)
				light->occluded = testLightOccluded(light, gdraw->occlusion_buffer, gdraw->visual_pass->frustum);
			else
				light->occluded = 0;
		}

		xcase RDRLIGHT_DIRECTIONAL:
		{
			light->deferred_sort_param = -8e8; // put directional lights at the front of the list
			rlight->fade_out = 1;
			light->occluded = 0;
		}

		xdefault:
		{
			assertmsg(0, "Unknown light type!");
		}
	}

	updateRdrLightColors(light, world_data);

	if (simple_light_type == RDRLIGHT_PROJECTOR)
		invertMat4Copy(rlight->world_mat, light->inv_world_mat);

	if (!rlight->fade_out)
		return false;

	if (!light->occluded)
	{
		light->frame_visible = gfx_state.client_frame_timestamp;
		light->use_counter = 1;
	}

	return true;
}

static int debug_show_light_list = 0;
//show a list of lights found at the beginning of the next frame
AUTO_CMD_INT(debug_show_light_list, debug_show_light_list);

void gfxDoLightPreDraw(void)
{
	int maxRdrLightsPerObject = rdrMaxSupportedObjectLights();
	GfxGlobalDrawParams *gdraw = SAFE_MEMBER_ADDR(gfx_state.currentAction, gdraw);
	WorldGraphicsData *world_data = worldGetWorldGraphicsData();
	int i, light_start;
	bool is_indoors;
	GfxLight * prior_frame_shadow_lights[MAX_SHADOWED_LIGHTS_PER_FRAME] = { 0 };
	ZoneMapInfo *zmap_info = zmapGetInfo(NULL);
	ZoneMapLightOverrideType light_override = zmap_info ? zmap_info->light_override : MAP_LIGHT_OVERRIDE_USE_PRIMARY;

	if (gfx_state.vertexOnlyLighting)
		maxRdrLightsPerObject = 2;
	gMaxLightsPerObject = gfx_state.settings.maxLightsPerObject ? (MIN(maxRdrLightsPerObject, gfx_state.settings.maxLightsPerObject)) : maxRdrLightsPerObject;

	if (!world_data || !gdraw)
		return;

	PERFINFO_AUTO_START_FUNC();


	is_indoors = (gfxActionAllowsIndoors(gfx_state.currentAction)
				  && gfxCheckIsSphereIndoors(gdraw->cam_pos, 0, 0, NULL, NULL, NULL, eaSize(&gdraw->regions) ? gdraw->regions[0] : NULL));

	// save the prior shadow lights to ensure they are allowed to fade out
	for (i = 0; i < eaSize(&gdraw->this_frame_shadowmap_lights); ++i )
		prior_frame_shadow_lights[i] = gdraw->this_frame_shadowmap_lights[i];
	eaSetSize(&gdraw->this_frame_lights, 0);
	eaSetSize(&gdraw->this_frame_shadowmap_lights, 0);

	light_start = 0;
	for (i = 0; i < eaSize(&gdraw->regions); ++i)
	{
		if (!gdraw->region_gdatas[i]->static_light_octree)
			continue;

		if (light_override == MAP_LIGHT_OVERRIDE_USE_PRIMARY &&
			gdraw->regions[i]->zmap_parent != worldGetActiveMap())
			continue;

		if (light_override == MAP_LIGHT_OVERRIDE_USE_SECONDARY &&
			gdraw->regions[i]->zmap_parent == worldGetActiveMap())
			continue;

		gfxSetCurrentRegion(i);

		octreeFindInFrustumEA(gdraw->region_gdatas[i]->static_light_octree, 1, false, &gdraw->this_frame_lights, gdraw->cam_pos, gdraw->visual_pass->frustum, NULL, NULL);

		if (!gfx_state.debug.disableDirLights)
			eaPushEArray(&gdraw->this_frame_lights, &gdraw->region_gdatas[i]->dir_lights);

		for (; light_start < eaSize(&gdraw->this_frame_lights); ++light_start)
		{
			if (!updateLightPreDraw(gdraw->this_frame_lights[light_start], is_indoors, world_data))
			{
				eaRemoveFast(&gdraw->this_frame_lights, light_start);
				--light_start;
			}
		}
	}

	gfxSetCurrentRegion(0);

	// add global lights

	if (!gfx_state.debug.disableDirLights && world_data->sun_light)
		eaPush(&gdraw->this_frame_lights, world_data->sun_light);
	if (!gfx_state.debug.disableDirLights && world_data->sun_light_2)
		eaPush(&gdraw->this_frame_lights, world_data->sun_light_2);
	
	if (!gfx_state.debug.disableDynamicLights)
		sparseGridFindInFrustumEA(world_data->dynamic_lights, gdraw->visual_pass->frustum, NULL, NULL, &gdraw->this_frame_lights);

	// merge in the prior shadow lights to ensure they are allowed to fade out
	for (i = 0; i < MAX_SHADOWED_LIGHTS_PER_FRAME; ++i)
	{
		if (prior_frame_shadow_lights[i])
			eaPushUnique(&gdraw->this_frame_lights, prior_frame_shadow_lights[i]);
	}

	// and update them
	for (; light_start < eaSize(&gdraw->this_frame_lights); ++light_start)
	{
		if (!updateLightPreDraw(gdraw->this_frame_lights[light_start], is_indoors, world_data))
		{
			eaRemoveFast(&gdraw->this_frame_lights, light_start);
			--light_start;
		}
	}

	if (debug_show_light_list)
		OutputDebugStringf("%i frame light list\n", gfx_state.client_frame_timestamp);

	for (i = 0; i < eaSize(&gdraw->this_frame_lights); ++i)
	{
		GfxLight *light = gdraw->this_frame_lights[i];
		RdrLight *rlight = &light->rdr_light;
		int shadowed = 0;
		if (rdrIsShadowedLightType(rlight->light_type) && (light->frame_visible == gfx_state.client_frame_timestamp || 
			light == prior_frame_shadow_lights[0] || light == prior_frame_shadow_lights[1]))
		{
			eaPush(&gdraw->this_frame_shadowmap_lights, light);
			shadowed = 1;
		}

		if (debug_show_light_list)
			OutputDebugStringf("\t%i: Light: %p Type: %i Shadow: %i \n", i, light, (int)rdrGetSimpleLightType(rlight->light_type), shadowed);

		if (gdraw->draw_list && gfx_state.debug.show_light_volumes)
		{
			switch (rdrGetSimpleLightType(rlight->light_type))
			{
				xcase RDRLIGHT_POINT:
					gfxDoDebugPointLight(light, gdraw->draw_list);

				xcase RDRLIGHT_SPOT:
					gfxDoDebugSpotLight(light, gdraw->draw_list);

				xcase RDRLIGHT_PROJECTOR:
					gfxDoDebugProjectorLight(light, gdraw->draw_list);
			}
		}
	}

	debug_show_light_list = 0;
	PERFINFO_AUTO_STOP_FUNC();
}

void gfxDoLightLoading(void)
{
	GfxGlobalDrawParams *gdraw = SAFE_MEMBER_ADDR(gfx_state.currentAction, gdraw);
	int i;

	if (!gdraw)
		return;

	PERFINFO_AUTO_START_FUNC();

	for (i = 0; i < eaSize(&gdraw->this_frame_lights); ++i)
	{
		GfxLight *light = gdraw->this_frame_lights[i];
		RdrLight *rlight = &light->rdr_light;

		if (rdrGetSimpleLightType(light->orig_light_type) == RDRLIGHT_PROJECTOR)
			rlight->projector_params.projected_tex = texDemandLoadFixed(light->texture); // Calculate distance here instead?
		else
			rlight->projector_params.projected_tex = 0;

		if (light->cloud_texture)
			rlight->cloud_texture = texDemandLoadFixed(light->cloud_texture);
		else
			rlight->cloud_texture = texDemandLoadFixed(black_tex);
	}

	PERFINFO_AUTO_STOP_FUNC();
}

void gfxDoLightShadowsPreDraw(const Frustum *camera_frustum, const Vec3 camera_focal_pt, RdrDrawList *draw_list)
{
	GfxGlobalDrawParams *gdraw = SAFE_MEMBER_ADDR(gfx_state.currentAction, gdraw);
	int i;
	int bFadeCurrentShadowmapLights = false;
	F32 highest_inactive_light_priority = 0.0f;

	if (!gdraw)
		return;

	PERFINFO_AUTO_START_FUNC();

	gfxSortShadowcasters(camera_frustum, camera_focal_pt);
	
	if (eaSize(&gdraw->this_frame_shadowmap_lights) > MAX_SHADOWMAPS_PER_FRAME)
	{
		highest_inactive_light_priority = gdraw->this_frame_shadowmap_lights[MAX_SHADOWMAPS_PER_FRAME]->nondeferred_sort_param;

		for (i = MAX_SHADOWMAPS_PER_FRAME; i < eaSize(&gdraw->this_frame_shadowmap_lights); ++i)
		{
			GfxLight *light = gdraw->this_frame_shadowmap_lights[i];
			if (light->shadow_transition && light->shadow_transition_rate >= 0.0f)
			{
				// start fading out
#if DEBUG_SHADOW_TRANSITIONS
				OutputDebugStringf("Start fade out 0x%p @(%f, %f, %f)\n", light,
					light->static_entry.bounds.mid[0], light->static_entry.bounds.mid[1], light->static_entry.bounds.mid[2]);
#endif
				light->shadow_transition_rate = -shadow_transition_speed;
			}
		}
		eaSetSize(&gdraw->this_frame_shadowmap_lights, MAX_SHADOWMAPS_PER_FRAME);
	}

	for (i = 0; i < eaSize(&gdraw->this_frame_shadowmap_lights); ++i)
	{
		GfxLight *light;
		RdrLight *rlight;

		ANALYSIS_ASSUME(gdraw->this_frame_shadowmap_lights && gdraw->this_frame_shadowmap_lights[i]);
		light = gdraw->this_frame_shadowmap_lights[i];
		rlight = &light->rdr_light;

		assert(i < ARRAY_SIZE(light->rdr_light.shadow_mask));
		light->rdr_light.shadow_mask[i] = 1;

		// if the light is not onscreen or has lower priority than an inactive light, start fading it
		if (light->frame_visible != gfx_state.client_frame_timestamp)
		{
#if DEBUG_SHADOW_TRANSITIONS
			OutputDebugStringf("Turn off 0x%p @(%f, %f, %f)\n", light,
				light->static_entry.bounds.mid[0], light->static_entry.bounds.mid[1], light->static_entry.bounds.mid[2]);
#endif
			// turn off instantaneously since it's off screen, to allow another shadow
			// to fade in
			light->shadow_transition = 0.0f;
			light->shadow_transition_rate = 0.0f;
		}
		else
		if (light->nondeferred_sort_param < highest_inactive_light_priority )
		{
			if (light->shadow_transition && light->shadow_transition_rate >= 0.0f)
			{
#if DEBUG_SHADOW_TRANSITIONS
				OutputDebugStringf("Start fade out 0x%p @(%f, %f, %f)\n", light,
					light->static_entry.bounds.mid[0], light->static_entry.bounds.mid[1], light->static_entry.bounds.mid[2]);
#endif
				// start fading out
				light->shadow_transition_rate = -shadow_transition_speed;
			}
		}
		else
		if (!light->shadow_transition)
		{
#if DEBUG_SHADOW_TRANSITIONS
			OutputDebugStringf("Start fade in 0x%p @(%f, %f, %f)\n", light,
				light->static_entry.bounds.mid[0], light->static_entry.bounds.mid[1], light->static_entry.bounds.mid[2]);
#endif
			// start fading in
			light->shadow_transition_rate = shadow_transition_speed;
		}

		switch (rdrGetSimpleLightType(rlight->light_type))
		{
			xcase RDRLIGHT_POINT:
				gfxShadowPointLight(light, camera_frustum, draw_list);

			xcase RDRLIGHT_SPOT:
				gfxShadowSpotLight(light, camera_frustum, draw_list);

			xcase RDRLIGHT_PROJECTOR:
				gfxShadowSpotLight(light, camera_frustum, draw_list);

			xcase RDRLIGHT_DIRECTIONAL:
				gfxShadowDirectionalLight(light, camera_frustum, draw_list);

			xdefault:
				assertmsg(0, "Unknown light type!");
		}
	}

	PERFINFO_AUTO_STOP_FUNC();
}

void gfxDoLightShadowsCalcProjMatrices(const Frustum *camera_frustum)
{
	GfxGlobalDrawParams *gdraw = SAFE_MEMBER_ADDR(gfx_state.currentAction, gdraw);
	int i;

	if (!gdraw)
		return;

	PERFINFO_AUTO_START_FUNC();

	sortLights(gdraw->this_frame_shadowmap_lights, camera_frustum->cammat[3]);

	for (i = 0; i < eaSize(&gdraw->this_frame_shadowmap_lights); ++i)
	{
		GfxLight *light = gdraw->this_frame_shadowmap_lights[i];
		RdrLight *rlight = &light->rdr_light;

		switch (rdrGetSimpleLightType(rlight->light_type))
		{
			xcase RDRLIGHT_POINT:
				gfxShadowPointLightCalcProjMatrix(light, camera_frustum);

			xcase RDRLIGHT_SPOT:
				gfxShadowSpotLightCalcProjMatrix(light, camera_frustum);

			xcase RDRLIGHT_PROJECTOR:
				gfxShadowSpotLightCalcProjMatrix(light, camera_frustum);

			xcase RDRLIGHT_DIRECTIONAL:
				gfxShadowDirectionalLightCalcProjMatrix(light, camera_frustum);
		}
	}

	PERFINFO_AUTO_STOP_FUNC();
}

void gfxDoLightShadowsDraw(const Frustum *camera_frustum, const Mat44 projection_mat, RdrDrawList *draw_list)
{
	GfxGlobalDrawParams *gdraw = SAFE_MEMBER_ADDR(gfx_state.currentAction, gdraw);
	int i;

	if (!gdraw)
		return;

	PERFINFO_AUTO_START_FUNC();

	gfxShadowsBeginFrame(camera_frustum, projection_mat);

	for (i = 0; i < eaSize(&gdraw->this_frame_shadowmap_lights); ++i)
	{
		GfxLight *glight = gdraw->this_frame_shadowmap_lights[i];
		RdrLight *rlight = &glight->rdr_light;

		glight->shadowmap_last_updated = gfx_state.client_frame_timestamp;

		switch (rdrGetSimpleLightType(rlight->light_type))
		{
			xcase RDRLIGHT_POINT:
				gfxShadowPointLightDraw(glight, camera_frustum, draw_list);

			xcase RDRLIGHT_SPOT:
				gfxShadowSpotLightDraw(glight, camera_frustum, draw_list);

			xcase RDRLIGHT_PROJECTOR:
				gfxShadowSpotLightDraw(glight, camera_frustum, draw_list);

			xcase RDRLIGHT_DIRECTIONAL:
				gfxShadowDirectionalLightDraw(glight, camera_frustum, draw_list);

			xdefault:
				assertmsg(0, "Unknown light type!");
		}
	}

	PERFINFO_AUTO_STOP_FUNC();
}

void gfxDoDeferredShadows(const RdrLightDefinition **light_defs, const BlendedSkyInfo *sky_info)
{
	GfxGlobalDrawParams *gdraw = SAFE_MEMBER_ADDR(gfx_state.currentAction, gdraw);

	if (!gdraw)
		return;

	gfxRenderShadowBuffer(gdraw->this_frame_shadowmap_lights, eaSize(&gdraw->this_frame_shadowmap_lights), light_defs, sky_info, 0);
}

//////////////////////////////////////////////////////////////////////////

RdrAmbientLight *gfxUpdateAmbientLight(RdrAmbientLight * light, const Vec3 ambient, const Vec3 sky_light_color_front, const Vec3 sky_light_color_back, const Vec3 sky_light_color_side)
{
	RdrAmbientLight *ambient_light;
	
	if (light)
	{
		ambient_light = light;
	}
	else
	{
		ambient_light = malloc(sizeof(RdrAmbientLight));
		gfxInvalidateAllLightCaches();
	}
	copyVec3(ambient, ambient_light->ambient[RLCT_WORLD]);
	copyVec3(ambient, ambient_light->ambient[RLCT_CHARACTER]);

	copyVec3(sky_light_color_front, ambient_light->sky_light_color_front[RLCT_WORLD]);
	copyVec3(sky_light_color_front, ambient_light->sky_light_color_front[RLCT_CHARACTER]);

	copyVec3(sky_light_color_back, ambient_light->sky_light_color_back[RLCT_WORLD]);
	copyVec3(sky_light_color_back, ambient_light->sky_light_color_back[RLCT_CHARACTER]);

	copyVec3(sky_light_color_side, ambient_light->sky_light_color_side[RLCT_WORLD]);
	copyVec3(sky_light_color_side, ambient_light->sky_light_color_side[RLCT_CHARACTER]);

	return ambient_light;
}

void gfxRemoveAmbientLight(RdrAmbientLight *ambient_light)
{
	gfxInvalidateAllLightCaches();
	free(ambient_light);
}

static GfxLight *gfxAllocLight()
{
	GfxLight * light;
	MP_CREATE(GfxLight, 64);
	light = MP_ALLOC(GfxLight);
	light->id = next_light_id++;
	return light;
}

void gfxFreeGfxLightFromRdrLight(RdrLight * rdr_light)
{
	GfxLight * gfx_light = GfxLightFromRdrLight(rdr_light);
	MP_FREE(GfxLight, gfx_light);
}

static GfxLight *gfxUpdateLightCommon(GfxLight *light, const LightData *data, bool *changed)
{
	F32 outer_radius, inner_radius;
	bool is_new = !light;
	bool was_changed = false;
	int i;

	assert(data);
	

	if (!light)
	{
		light = gfxAllocLight();
	}

	light->texture = NULL;
	light->cloud_texture = NULL;

	switch (data->light_type)
	{
		xcase WL_LIGHT_DIRECTIONAL:
			assert(is_new || rdrGetSimpleLightType(light->orig_light_type) == RDRLIGHT_DIRECTIONAL);
			light->orig_light_type = RDRLIGHT_DIRECTIONAL;
			if (data->cloud_texture_name)
				light->cloud_texture = texLoadBasic(data->cloud_texture_name, TEX_LOAD_IN_BACKGROUND, WL_FOR_WORLD);
			STATIC_INFUNC_ASSERT(ARRAY_SIZE(data->cloud_layers) == MAX_CLOUD_LAYERS);
			for (i = 0; i < MAX_CLOUD_LAYERS; ++i)
			{
				if (data->cloud_layers[i].multiplier)
				{
					light->rdr_light.cloud_layers[i].multiplier = data->cloud_layers[i].multiplier;
					copyVec2(data->cloud_layers[i].scroll_rate, light->cloud_layers[i].scroll_rate);
					light->cloud_layers[i].scale = data->cloud_layers[i].scale;
				}
				else
				{
					ZeroStructForce(&light->cloud_layers[i]);
				}
			}
			assert(!data->dynamic);
		xcase WL_LIGHT_POINT:
			assert(is_new || rdrGetSimpleLightType(light->orig_light_type) == RDRLIGHT_POINT);
			light->orig_light_type = RDRLIGHT_POINT;
		xcase WL_LIGHT_SPOT:
			assert(is_new || rdrGetSimpleLightType(light->orig_light_type) == RDRLIGHT_SPOT);
			light->orig_light_type = RDRLIGHT_SPOT;
		xcase WL_LIGHT_PROJECTOR:
			assert(is_new || rdrGetSimpleLightType(light->orig_light_type) == RDRLIGHT_PROJECTOR);
			light->orig_light_type = RDRLIGHT_PROJECTOR;
			light->texture = texLoadBasic(data->texture_name, TEX_LOAD_IN_BACKGROUND, WL_FOR_WORLD);
			if (!light->texture)
				light->texture = white_tex;
		xdefault:
			assertmsg(0, "Unknown light type!");
	}

	assert(is_new || light->dynamic == data->dynamic); // can't change a light from static to dynamic or vice versa

	if (light->texture)
	{
		if (texIsCubemap(light->texture))
		{
			// not allowed
			printf("Light 0x%p is attempting to use a cubemap texture %s\n", light, light->texture->name);
			light->texture = NULL;
		}
	}

	if (light->cloud_texture)
	{
		if (texIsCubemap(light->cloud_texture))
		{
			// not allowed
			printf("Light 0x%p is attempting to use a cubemap texture for cloud %s\n", light, light->cloud_texture->name);
			light->cloud_texture = NULL;
		}
	}

	if (is_new)
	{
		light->shadow_pulse_pos[0] = randomPositiveF32();
		light->shadow_pulse_pos[1] = randomPositiveF32();

		while (!light->shadow_pulse_dir[0] || !light->shadow_pulse_dir[1])
		{
			light->shadow_pulse_dir[0] = randomF32();
			light->shadow_pulse_dir[1] = randomF32();
			normalVec2(light->shadow_pulse_dir);
		}

		setLightIndoors(light);
	}

	light->shadow_fade_rate = data->shadow_fade_time ? (1.f / data->shadow_fade_time) : 0.2f;
	light->shadow_fade_dark_time = data->shadow_fade_dark_time;
	light->shadow_pulse_amplitude = data->shadow_pulse_amount;
	light->shadow_pulse_rate = data->shadow_pulse_rate;

	outer_radius = MAX(data->outer_radius, 0.01f);
	inner_radius = CLAMP(data->inner_radius, 0, MAX(0, data->outer_radius - 0.01));

	if (!is_new && changed)
	{
		if (outer_radius != light->orig_outer_radius || inner_radius !=light->orig_inner_radius)
			was_changed = true;
		else
		if (!sameVec3InMem(data->world_mat[3], light->rdr_light.world_mat[3]))
			was_changed = true;
		else
		if (rdrGetSimpleLightType(light->orig_light_type) == RDRLIGHT_PROJECTOR)
		{
			if (!sameVec3InMem(light->rdr_light.world_mat[0], data->world_mat[0]) ||
				!sameVec3InMem(light->rdr_light.world_mat[1], data->world_mat[1]) ||
				!sameVec3InMem(light->rdr_light.world_mat[2], data->world_mat[2]) ||
				data->outer_cone_angle != light->orig_outer_cone_angle ||
				data->outer_cone_angle2 != light->orig_outer_cone_angle2)
				was_changed = true;
		}
		if (was_changed)
		{
			*changed = was_changed;
		}
	}

	light->tracker = data->tracker;
	light->is_sun = !!data->is_sun;
	light->dynamic = !!data->dynamic;
	light->key_light = data->key_light || data->is_sun || data->dynamic;
	light->light_affect_type = data->light_affect_type;
	light->infinite_shadows = data->infinite_shadows || data->is_sun;

	copyMat4(data->world_mat, light->rdr_light.world_mat);
	
	if (light->orig_light_type == RDRLIGHT_PROJECTOR)
		invertMat4Copy(data->world_mat, light->inv_world_mat);

	if (light->orig_light_type == RDRLIGHT_DIRECTIONAL)
		scaleVec3(light->rdr_light.world_mat[1], -1, light->rdr_light.world_mat[1]);

	copyVec3(data->ambient_hsv, light->light_colors.hsv_ambient);
	copyVec3(data->diffuse_hsv, light->light_colors.hsv_diffuse);
	{
		const F32 *specular_source_hsv = data->specular_hsv;
		if (gfx_lighting_options.bLockSpecularToDiffuseColor)
			specular_source_hsv = data->diffuse_hsv;
		copyVec3(specular_source_hsv, light->light_colors.hsv_specular);
	}
	copyVec3(data->secondary_diffuse_hsv, light->light_colors.hsv_secondary_diffuse);
	copyVec3(data->shadow_color_hsv, light->light_colors.hsv_shadow_color);

	assert(FINITE(light->light_colors.hsv_ambient[2]));
	assert(FINITE(light->light_colors.hsv_diffuse[2]));
	assert(FINITE(light->light_colors.hsv_specular[2]));
	assert(FINITE(light->light_colors.hsv_secondary_diffuse[2]));
	assert(FINITE(light->light_colors.hsv_shadow_color[2]));
	light->light_colors.hsv_ambient[2] = CLAMP(light->light_colors.hsv_ambient[2], MIN_LIGHT_VALUE, MAX_LIGHT_VALUE);
	light->light_colors.hsv_diffuse[2] = CLAMP(light->light_colors.hsv_diffuse[2], MIN_LIGHT_VALUE, MAX_LIGHT_VALUE);
	light->light_colors.hsv_specular[2] = CLAMP(light->light_colors.hsv_specular[2], MIN_LIGHT_VALUE, MAX_LIGHT_VALUE);
	light->light_colors.hsv_secondary_diffuse[2] = CLAMP(light->light_colors.hsv_secondary_diffuse[2], MIN_LIGHT_VALUE, MAX_LIGHT_VALUE);

	light->light_colors.min_shadow_val = data->min_shadow_val;
	light->light_colors.max_shadow_val = 1;
	light->light_colors.shadow_fade_val = data->shadow_fade_val;

	light->orig_outer_radius = outer_radius;
	light->orig_inner_radius = inner_radius;

	light->orig_inner_cone_angle = CLAMP(data->inner_cone_angle, 0, 89.5f);
	light->orig_inner_cone_angle = RAD(light->orig_inner_cone_angle);
	light->orig_outer_cone_angle = CLAMP(data->outer_cone_angle, 0, 89.5f);
	light->orig_outer_cone_angle = RAD(light->orig_outer_cone_angle);

	if (light->orig_inner_cone_angle < 0)
		light->orig_inner_cone_angle = 0;
	if (light->orig_outer_cone_angle < light->orig_inner_cone_angle + 0.01f)
		light->orig_outer_cone_angle = light->orig_inner_cone_angle + 0.01f;

	light->orig_inner_cone_angle2 = CLAMP(data->inner_cone_angle2, 0, 89.5f);
	light->orig_inner_cone_angle2 = RAD(light->orig_inner_cone_angle2);
	light->orig_outer_cone_angle2 = CLAMP(data->outer_cone_angle2, 0, 89.5f);
	light->orig_outer_cone_angle2 = RAD(light->orig_outer_cone_angle2);

	if (light->orig_inner_cone_angle2 < 0)
		light->orig_inner_cone_angle2 = 0;
	if (light->orig_outer_cone_angle2 < light->orig_inner_cone_angle2 + 0.01f)
		light->orig_outer_cone_angle2 = light->orig_inner_cone_angle2 + 0.01f;

	light->shadow_near_plane = MAX(0, data->shadow_near_plane);

	setRdrLightType(light);
	updateRdrFalloffParams(&light->rdr_light, light->orig_inner_radius, light->orig_outer_radius,
							light->orig_inner_cone_angle, light->orig_outer_cone_angle,
							light->orig_inner_cone_angle2, light->orig_outer_cone_angle2,
							unitvec3);

	copyMat4(light->rdr_light.world_mat, light->controller_relative_matrix);

	if (data->cast_shadows && light->key_light)
		light->orig_light_type |= RDRLIGHT_SHADOWED;

	setRdrLightType(light);

	return light;
}

// TODO: Remove this, and have the feature on by
//   default when we know for sure that it's not
//   going to break anything. -Cliff
static int s_bBlackSunFix = 1;
AUTO_CMD_INT(s_bBlackSunFix, BlackSunFix) ACMD_CATEGORY(Debug);

void gfxCreateSunLight(GfxLight **sun_light, const LightData *data, BlendedSkyInfo *new_sky_info)
{
	assert(data);
	assert(data->light_type == WL_LIGHT_DIRECTIONAL);
	assert(data->is_sun);

	if(s_bBlackSunFix) {

		if((new_sky_info && new_sky_info->sunValues.bDisabled) ||
			(data->diffuse_hsv[2] == 0 &&
			data->secondary_diffuse_hsv[2] == 0 &&
			data->specular_hsv[2] == 0)) {
			
			// Attempting to set a black sun. Just
			//   remove the light so we don't do
			//   extra shadow calculations for
			//   something that doesn't add to the
			//   scene.
			if(*sun_light) {
				gfxRemoveLight(*sun_light);
				gfxInvalidateAllLightCaches();
			}
		} else {
			if(!*sun_light) gfxInvalidateAllLightCaches();
			*sun_light = gfxUpdateLightCommon(*sun_light, data, NULL);
			(*sun_light)->indoors = false;
		}

	} else {
		*sun_light = gfxUpdateLightCommon(*sun_light, data, NULL);
		(*sun_light)->indoors = false;
	}
}

void gfxUpdateSunLight(GfxLight **sun_light, const LightData *data, BlendedSkyInfo *new_sky_info)
{
	gfxCreateSunLight(sun_light, data, new_sky_info);
}

static void gfxUpdateLightRoomOwnership(GfxLight* light);

typedef enum BoxIntersection
{
	BI_None,
	BI_Overlap,
	BI_Contains,
} BoxIntersection;

static __forceinline BoxIntersection boxInsideBox(const Vec3 minA, const Vec3 maxA, const Vec3 minB, const Vec3 maxB)
{
	if (maxB[0] <= minA[0] || 
		minB[0] >= maxA[0] || 
		maxB[1] <= minA[1] || 
		minB[1] >= maxA[1] || 
		maxB[2] <= minA[2] || 
		minB[2] >= maxA[2])
		return BI_None;
	return minB[0] >= minA[0] && 
		minB[1] >= minA[1] && 
		minB[2] >= minA[2] && 
		maxB[0] <= maxA[0] && 
		maxB[1] <= maxA[1] && 
		maxB[2] <= maxA[2] ? BI_Contains : BI_Overlap;
}

static __forceinline void mergeBoxIntoBox(Vec3 minA, Vec3 maxA, const Vec3 minB, const Vec3 maxB)
{
	vec3RunningMin(minB, minA);
	vec3RunningMax(maxB, maxA);
}

static __forceinline void copyBox(const Vec3 minA, const Vec3 maxA, Vec3 minB, Vec3 maxB)
{
	copyVec3(minA, minB);
	copyVec3(maxA, maxB);
}

static __forceinline float boxVolume(const Vec3 min, const Vec3 max)
{
	Vec3 size;
	subVec3(max, min, size);
	return size[0] * size[1] * size[2];
}


int allowMovingBBox = 2;
AUTO_CMD_INT(allowMovingBBox,allowMovingBBox);

bool gfxLightsAllowMovingBBox(void) {
	return (allowMovingBBox == 2);
}

static int updateMovingBoundsAndMaybeInvalidate(GfxLight *light)
{
	BoxIntersection stayedInside;
	if (!allowMovingBBox)
		return true;

	stayedInside = boxInsideBox(light->movingBoundsMin, light->movingBoundsMax, light->static_entry.bounds.min, light->static_entry.bounds.max);

	if (stayedInside == BI_Overlap)
		mergeBoxIntoBox(light->movingBoundsMin, light->movingBoundsMax, light->static_entry.bounds.min, light->static_entry.bounds.max);
	else
	if (stayedInside == BI_None)
		copyBox(light->static_entry.bounds.min, light->static_entry.bounds.max, light->movingBoundsMin, light->movingBoundsMax);

	if (allowMovingBBox == 2)
	{
		return stayedInside;
	}

	return stayedInside != BI_Contains;
}

GfxLight *gfxUpdateLight(GfxLight *light, const LightData *data, F32 vis_dist, WorldAnimationEntry *animation_controller)
{
	WorldGraphicsData *world_data = worldGetWorldGraphicsData();
	bool is_new = !light, changed = false;
	F32 new_vis_dist;
	WorldRegionGraphicsData *graphics_data;
	WorldRegion *region;
	Vec3 old_bounds_min, old_bounds_max;
	Vec3 world_min, world_max;

	PERFINFO_AUTO_START_FUNC();

	light = gfxUpdateLightCommon(light, data, &changed);

	new_vis_dist = vis_dist;
	if (new_vis_dist <= 0)
		new_vis_dist = data->outer_radius * LIGHT_RADIUS_VIS_MULTIPLIER;
	MIN1(new_vis_dist, MAX_LIGHT_DISTANCE);
	if (data->visual_lod_scale)
		new_vis_dist *= data->visual_lod_scale;

	graphics_data = worldRegionGetGraphicsData(data->region);
	assert(graphics_data);
	region = graphics_data->region;

	if (!is_new)
	{
		if (!nearSameF32(light->static_entry.bounds.vis_dist, new_vis_dist))
			changed = true;
		if (light->region_graphics_data != graphics_data)
			changed = true;
	}

	if (allowMovingBBox)
	{
		copyVec3(light->movingBoundsMin, old_bounds_min);
		copyVec3(light->movingBoundsMax, old_bounds_max);
	}
	else
	{
		copyVec3(light->static_entry.bounds.min, old_bounds_min);
		copyVec3(light->static_entry.bounds.max, old_bounds_max);
	}

	light->animation_controller = animation_controller;
	if (light->animation_controller)
	{
		if (!light->animation_controller->animation_properties.local_space)
			worldAnimationEntryInitChildRelativeMatrix(light->animation_controller, light->rdr_light.world_mat, light->controller_relative_matrix);
	}

	if (is_new || changed || light->animation_controller) {
		if (data->light_type == WL_LIGHT_PROJECTOR) {
			Vec3 local_min, local_max, local_mid, local_query_mid;
			F32 u, v, r;

			r = light->orig_outer_radius;
			u = r * tanf(light->orig_outer_cone_angle);
			v = r * tanf(light->orig_outer_cone_angle2);

			setVec3(local_min, -u, -r, -v);
			setVec3(local_max, u, 0, v);

			setVec3(local_query_mid, 0, -light->shadow_near_plane, 0);
			mulVecMat4(local_query_mid, data->world_mat, light->world_query_mid);

			// TODO(CD) find a better mid for the sphere
			light->static_entry.bounds.radius = boxCalcMid(local_min, local_max, local_mid);
			mulVecMat4(local_mid, data->world_mat, light->static_entry.bounds.mid);

			if (light->animation_controller)
			{
				worldAnimationEntryModifyBounds(light->animation_controller, 
					data->world_mat, local_min, local_max, 
					light->static_entry.bounds.mid, &light->static_entry.bounds.radius);
				if (!is_new)
					changed = true;
			}

			mulBoundsAA(local_min, local_max, data->world_mat, light->static_entry.bounds.min, light->static_entry.bounds.max);
		} else if (data->light_type == WL_LIGHT_SPOT) {
			Vec3 endPoint;
			F32 lightLen, discR, discDist;
			Vec3 discMin, discMax, discProj;
			Mat4 animMat;
			const Vec3 *world_mat;

			if (light->animation_controller) {
				world_mat = animMat;

                if (light->animation_controller->last_update_timestamp != gfx_state.client_frame_timestamp)
                    worldAnimationEntryUpdate(light->animation_controller, gfx_state.client_loop_timer, gfx_state.client_frame_timestamp);
                if (light->animation_controller->animation_properties.local_space)
                    mulMat4Inline(light->controller_relative_matrix, light->animation_controller->full_matrix, animMat);
                else
                    mulMat4Inline(light->animation_controller->full_matrix, light->controller_relative_matrix, animMat);

				if (!is_new)
					changed = true;
            } else {
				world_mat = data->world_mat;
			}

			lightLen = light->orig_outer_radius;
			discR = lightLen * sinf(light->orig_outer_cone_angle);
			discDist = lightLen * cosf(light->orig_outer_cone_angle);

			// bound the two end points of the light along the light direction
			copyVec3(world_mat[3], light->static_entry.bounds.min);
			copyVec3(world_mat[3], light->static_entry.bounds.max);

			scaleAddVec3(world_mat[1], -lightLen, world_mat[3], endPoint);
			vec3RunningMinMax(endPoint, light->static_entry.bounds.min, light->static_entry.bounds.max);

			// bound the disc that is the maximum of the light cone
			discProj[0] = sqrtf_clamped(1.0f - world_mat[1][0] * world_mat[1][0]);
			discProj[1] = sqrtf_clamped(1.0f - world_mat[1][1] * world_mat[1][1]);
			discProj[2] = sqrtf_clamped(1.0f - world_mat[1][2] * world_mat[1][2]);

			scaleAddVec3(world_mat[1], -discDist, world_mat[3], endPoint);

            discMin[0] = endPoint[0] - discR * discProj[0];
			discMin[1] = endPoint[1] - discR * discProj[1];
			discMin[2] = endPoint[2] - discR * discProj[2];
		
			discMax[0] = endPoint[0] + discR * discProj[0];
            discMax[1] = endPoint[1] + discR * discProj[1];
			discMax[2] = endPoint[2] + discR * discProj[2];

			// merge the two bounds
			vec3RunningMin(discMin, light->static_entry.bounds.min);
			vec3RunningMax(discMax, light->static_entry.bounds.max);

			// compute mid and radius - this is an overestimate. A tighter sphere could be fit to the
			// kite formed by the endpoints along the direction and the cone maxima, but that is probably
			// too much computation to be worth it.
			light->static_entry.bounds.radius = boxCalcMid(light->static_entry.bounds.min,
                light->static_entry.bounds.max, light->static_entry.bounds.mid);
			copyVec3(data->world_mat[3], light->world_query_mid);
		} else {
			copyVec3(data->world_mat[3], light->static_entry.bounds.mid);
			copyVec3(data->world_mat[3], light->world_query_mid);
			light->static_entry.bounds.radius = light->orig_outer_radius;

			if (light->animation_controller)
			{
				worldAnimationEntryModifyBounds(light->animation_controller, 
					NULL, NULL, NULL, 
					light->static_entry.bounds.mid, &light->static_entry.bounds.radius);
				if (!is_new)
					changed = true;
			}

			subVec3same(light->static_entry.bounds.mid, light->static_entry.bounds.radius, light->static_entry.bounds.min);
			addVec3same(light->static_entry.bounds.mid, light->static_entry.bounds.radius, light->static_entry.bounds.max);
		}
	}

	light->static_entry.bounds.vis_dist = new_vis_dist;
	light->static_entry.node = light;

	if (data->light_type == WL_LIGHT_DIRECTIONAL)
	{
		assert(!light->dynamic);

		if (changed)
		{
			if (light->region_graphics_data != graphics_data)
			{
				if (light->region)
				{
					worldRegionGetBounds(light->region, world_min, world_max);
					gfxForceUpdateLightCaches(world_min, world_max, unitmat, true, true, LCIT_STATIC_LIGHTS, NULL);
				}
				eaFindAndRemoveFast(&light->region_graphics_data->dir_lights, light);

				if (region)
				{
					worldRegionGetBounds(region, world_min, world_max);
					gfxForceUpdateLightCaches(world_min, world_max, unitmat, true, true, LCIT_STATIC_LIGHTS, NULL);
				}
				eaPushUnique(&graphics_data->dir_lights, light);
			}
		}
		else if (is_new)
		{
			if (region)
			{
				worldRegionGetBounds(region, world_min, world_max);
				gfxForceUpdateLightCaches(world_min, world_max, unitmat, true, true, LCIT_STATIC_LIGHTS, NULL);
			}
			eaPush(&graphics_data->dir_lights, light);
		}
	}
	else if (light->dynamic)
	{
		if (changed)
		{
			int light_bounds_state = updateMovingBoundsAndMaybeInvalidate(light);

			if (allowMovingBBox == 2)
			{
				if (light_bounds_state != BI_Contains)
				{
					static const F32 MAX_MOVING_LIGHT_VOLUME_OVERFLOW = 8.0f;

					gfxForceUpdateLightCaches(old_bounds_min, old_bounds_max, unitmat, true, true, LCIT_DYNAMIC_LIGHTS, light);
					sparseGridMoveBox(world_data->dynamic_lights, &light->dynamic_entry, light, light->movingBoundsMin, light->movingBoundsMax);

					if (light_bounds_state == BI_None)
						light->frame_modified = gfx_state.frame_count;
					else
					if (boxVolume(light->movingBoundsMin, light->movingBoundsMax) > boxVolume(light->static_entry.bounds.min, light->static_entry.bounds.max) * MAX_MOVING_LIGHT_VOLUME_OVERFLOW)
					{
						light->frame_modified = gfx_state.frame_count;
						copyBox(light->static_entry.bounds.min, light->static_entry.bounds.max, light->movingBoundsMin, light->movingBoundsMax);
					}

					gfxForceUpdateLightCaches(light->movingBoundsMin, light->movingBoundsMax, unitmat, true, true, LCIT_DYNAMIC_LIGHTS, NULL);
				}
			}
			else
			if (light_bounds_state)
			{
				gfxForceUpdateLightCaches(old_bounds_min, old_bounds_max, unitmat, true, true, LCIT_DYNAMIC_LIGHTS, light);
				sparseGridMoveBox(world_data->dynamic_lights, &light->dynamic_entry, light, light->static_entry.bounds.min, light->static_entry.bounds.max);
				gfxForceUpdateLightCaches(light->static_entry.bounds.min, light->static_entry.bounds.max, unitmat, true, true, LCIT_DYNAMIC_LIGHTS, NULL);
			}
		}
		else if (is_new)
		{
			updateMovingBoundsAndMaybeInvalidate(light);
			sparseGridMoveBox(world_data->dynamic_lights, &light->dynamic_entry, light, light->static_entry.bounds.min, light->static_entry.bounds.max);
			gfxForceUpdateLightCaches(light->static_entry.bounds.min, light->static_entry.bounds.max, unitmat, true, true, LCIT_DYNAMIC_LIGHTS, NULL);
		}
	}
	else
	{
		if (changed)
		{
			if (octreeEntryInUse(&light->static_entry))
			{
				octreeRemove(&light->static_entry);
				gfxForceUpdateLightCaches(old_bounds_min, old_bounds_max, unitmat, true, true, LCIT_STATIC_LIGHTS, light);
			}

			octreeAddEntry(graphics_data->static_light_octree, &light->static_entry, OCT_MEDIUM_GRANULARITY);
			gfxForceUpdateLightCaches(light->static_entry.bounds.min, light->static_entry.bounds.max, unitmat, true, true, LCIT_STATIC_LIGHTS, NULL);
		}
		else if (is_new)
		{
			octreeAddEntry(graphics_data->static_light_octree, &light->static_entry, OCT_MEDIUM_GRANULARITY);
			gfxForceUpdateLightCaches(light->static_entry.bounds.min, light->static_entry.bounds.max, unitmat, true, true, LCIT_STATIC_LIGHTS, NULL);
		}
	}

	light->region = region;
	light->region_graphics_data = graphics_data;

	if (changed || is_new || last_world_volume_timestamp != wlVolumeGetWorldVolumeStateTimestamp(PARTITION_CLIENT))
	{
		gfxUpdateLightRoomOwnership(light);
		setLightIndoors(light);
	}

	PERFINFO_AUTO_STOP_FUNC();

	return light;
}

void gfxRemoveLight(GfxLight *light)
{
	WorldGraphicsData *world_data = worldGetWorldGraphicsData();
	int i;

	if (!light)
		return;

	PERFINFO_AUTO_START_FUNC();

	light->frame_modified = gfx_state.frame_count;

	if (light == world_data->sun_light)
	{
		world_data->sun_light = NULL;
		gfxForceUpdateLightCaches(NULL, NULL, NULL, true, true, LCIT_STATIC_LIGHTS, light);
	}
	else if (light == world_data->sun_light_2)
	{
		world_data->sun_light_2 = NULL;
		gfxForceUpdateLightCaches(NULL, NULL, NULL, true, true, LCIT_STATIC_LIGHTS, light);
	}
	else if (octreeEntryInUse(&light->static_entry))
	{
		octreeRemove(&light->static_entry);
		gfxForceUpdateLightCaches(light->static_entry.bounds.min, light->static_entry.bounds.max, unitmat, true, true, LCIT_STATIC_LIGHTS, light);
	}
	else if (light->dynamic)
	{
		sparseGridRemove(&light->dynamic_entry);
		if (allowMovingBBox == 2)
		{
			gfxForceUpdateLightCaches(light->movingBoundsMin, light->movingBoundsMax, unitmat, true, true, LCIT_DYNAMIC_LIGHTS, light);
		}
		else
		if (allowMovingBBox)
			gfxForceUpdateLightCaches(light->movingBoundsMin, light->movingBoundsMax, unitmat, true, true, LCIT_DYNAMIC_LIGHTS, light);
		else
			gfxForceUpdateLightCaches(light->static_entry.bounds.min, light->static_entry.bounds.max, unitmat, true, true, LCIT_DYNAMIC_LIGHTS, light);
	}
	else if (light->region_graphics_data && rdrGetSimpleLightType(light->orig_light_type) == RDRLIGHT_DIRECTIONAL)
	{
		if (light->region_graphics_data->region)
		{
			Vec3 world_min, world_max;
			worldRegionGetBounds(light->region_graphics_data->region, world_min, world_max);
			gfxForceUpdateLightCaches(world_min, world_max, unitmat, true, true, LCIT_STATIC_LIGHTS, light);
		}
		eaFindAndRemoveFast(&light->region_graphics_data->dir_lights, light);
	}
	else
	{
		// do nothing, already had its region_graphics_data cleared
	}

	if (gfx_state.currentDevice)
	{
		for (i = 0; i < eaSize(&gfx_state.currentDevice->actions); ++i)
		{
			GfxGlobalDrawParams *gdraw = &gfx_state.currentDevice->actions[i]->gdraw;
			eaFindAndRemoveFast(&gdraw->this_frame_lights, light);
			eaFindAndRemoveFast(&gdraw->this_frame_shadowmap_lights, light);
		}
		gfxDeviceRemoveLightFromShadowmapHistory(gfx_state.currentDevice, light);
	}

	if (light->room_assignment_valid && light->owner_room)
	{
		//only do this if it was valid before. If the rooms were remade it will already be removed
		//and we dont want to follow some bogus pointer
		eaFindAndRemoveFast(&light->owner_room->lights_in_room, light);
	}

#if TRACK_LIGHT_CACHE_REFS
	if (light->cache_ref_count)
	{
		char light_desc[ 256 ];
		rdrLightDumpToStr(&light->rdr_light, 0, light_desc, ARRAY_SIZE(light_desc));

		memlog_printf(NULL, "Possibly deleting light still referenced by a cache! 0x%p %d refs RdrLight 0x%p %s Frm %d\n", light, light->cache_ref_count, &light->rdr_light, light_desc, gfx_state.frame_count);
	}
	// also set a bit to indicate the light is being deleted
	light->rdr_light.light_type |= RDRLIGHT_DELETING;
#endif

	eaPush(&queued_light_frees, light);

	if (light->shadow_search_node) //if the node was in the graph we need to build a new one
		gfxClearShadowSearchData();

	PERFINFO_AUTO_STOP_FUNC();
}

static U32 occluder_volume_type;

GfxLight unlit_light = {0};
RdrAmbientLight unlit_ambient_light = {0};

void gfxSetUnlitLightValue(F32 unlit_value)
{
	setVec3same(unlit_ambient_light.ambient[RLCT_WORLD], unlit_value);
	setVec3same(unlit_ambient_light.ambient[RLCT_CHARACTER], unlit_value);
}

void gfxGetUnlitLightEx(RdrLightParams *light_params, bool clear_struct)
{
	if (!unlit_light.rdr_light.light_type)
	{
		unlit_light.rdr_light.light_type = RDRLIGHT_DIRECTIONAL;
		unlit_light.rdr_light.fade_out = 1.0f;
	}

	if (clear_struct)
		ZeroStructForce(light_params);

	if (!gfx_state.debug.force_unlit_value)
	{
		setVec3same(unlit_ambient_light.ambient[RLCT_WORLD], 1);
		setVec3same(unlit_ambient_light.ambient[RLCT_CHARACTER], 1);
	}

	light_params->lights[0] = &unlit_light.rdr_light;
	light_params->ambient_light = &unlit_ambient_light;
}

void gfxGetSunLight(RdrLightParams *light_params, bool is_character)
{
	WorldGraphicsData *world_data = worldGetWorldGraphicsData();

	if (gfx_state.debug.force_unlit_value)
	{
		gfxGetUnlitLight(light_params);
		return;
	}

	ZeroStructForce(light_params);

	light_params->ambient_light = world_data->outdoor_ambient_light;

	if (world_data->sun_light)
		light_params->lights[0] = &world_data->sun_light->rdr_light;
}

void gfxGetOverrideLight(RdrLightParams *light_params, bool is_character, const Vec3 override_light_color, const Vec3 override_light_direction)
{
	WorldGraphicsData *world_data = worldGetWorldGraphicsData();
	RdrLight *override_light = NULL;
	int i;

	if (gfx_state.debug.force_unlit_value)
	{
		gfxGetUnlitLight(light_params);
		return;
	}

	ZeroStructForce(light_params);

	light_params->ambient_light = world_data->outdoor_ambient_light;

	// find available override light
	for (i = 0; i < eaSize(&world_data->override_lights); ++i)
	{
		if (world_data->override_lights[i]->light_type == RDRLIGHT_NONE)
		{
			override_light = world_data->override_lights[i];
			break;
		}
	}

	if (!override_light)
	{
		GfxLight * new_gfx_light = gfxAllocLight();
		override_light = &new_gfx_light->rdr_light;
		eaPush(&world_data->override_lights, override_light);
	}

	// setup override light
	ZeroStructForce(override_light);
	override_light->light_type = RDRLIGHT_DIRECTIONAL;
	override_light->fade_out = 1.0f;

	scaleVec3(override_light_direction, -1, override_light->world_mat[1]);
	copyVec3(override_light_color, override_light->light_colors[RLCT_WORLD].diffuse);
	copyVec3(override_light_color, override_light->light_colors[RLCT_CHARACTER].diffuse);

	light_params->lights[0] = override_light;
}

RdrAmbientLight *gfxGetOverrideAmbientLight(const Vec3 ambient, const Vec3 sky_light_color_front, const Vec3 sky_light_color_back, const Vec3 sky_light_color_side)
{
	WorldGraphicsData *world_data = worldGetWorldGraphicsData();
	RdrAmbientLight *override_ambient = NULL;
	int i;

	// try to find a matching existing light
	for (i = 0; i < world_data->used_override_ambient_lights; ++i)
	{
		ANALYSIS_ASSUME(world_data->override_ambient_lights);
		if (sameVec3(world_data->override_ambient_lights[i]->ambient[RLCT_WORLD], ambient) && 
			sameVec3(world_data->override_ambient_lights[i]->sky_light_color_front[RLCT_WORLD], sky_light_color_front) &&
			sameVec3(world_data->override_ambient_lights[i]->sky_light_color_back[RLCT_WORLD], sky_light_color_back) &&
			sameVec3(world_data->override_ambient_lights[i]->sky_light_color_side[RLCT_WORLD], sky_light_color_side))
		{
			return world_data->override_ambient_lights[i];
		}
	}

	// find a free light or allocate a new one
	if (world_data->used_override_ambient_lights < eaSize(&world_data->override_ambient_lights))
	{
		ANALYSIS_ASSUME(world_data->override_ambient_lights);
		override_ambient = world_data->override_ambient_lights[world_data->used_override_ambient_lights];
		++world_data->used_override_ambient_lights;
	}
	else
	{
		override_ambient = malloc(sizeof(RdrAmbientLight));
		world_data->used_override_ambient_lights = eaPush(&world_data->override_ambient_lights, override_ambient);
	}

	// setup light
	ZeroStructForce(override_ambient);

	copyVec3(ambient, override_ambient->ambient[RLCT_WORLD]);
	copyVec3(ambient, override_ambient->ambient[RLCT_CHARACTER]);

	copyVec3(sky_light_color_front, override_ambient->sky_light_color_front[RLCT_WORLD]);
	copyVec3(sky_light_color_front, override_ambient->sky_light_color_front[RLCT_CHARACTER]);

	copyVec3(sky_light_color_back, override_ambient->sky_light_color_back[RLCT_WORLD]);
	copyVec3(sky_light_color_back, override_ambient->sky_light_color_back[RLCT_CHARACTER]);

	copyVec3(sky_light_color_side, override_ambient->sky_light_color_side[RLCT_WORLD]);
	copyVec3(sky_light_color_side, override_ambient->sky_light_color_side[RLCT_CHARACTER]);

	return override_ambient;
}

static bool checkIndoorVolumes(const WorldVolume **volumes, F32 size, RdrAmbientLight **indoor_ambient_light, F32 *indoor_light_range, bool *can_see_outdoors, U32 indoor_volume_type)
{
	int i;
	for (i = 0; i < eaSize(&volumes); ++i)
	{
		if (wlVolumeIsType(volumes[i], indoor_volume_type) && wlVolumeGetSize(volumes[i]) > size)
		{
			WorldVolumeEntry *entry = wlVolumeGetVolumeData(volumes[i]);

			if (indoor_ambient_light && entry && entry->indoor_ambient_light)
				*indoor_ambient_light = entry->indoor_ambient_light;

			if (indoor_light_range)
			{
				if (entry && entry->client_volume.indoor_volume_properties && entry->client_volume.indoor_volume_properties->light_range)
				{
					*indoor_light_range = entry->client_volume.indoor_volume_properties->light_range;
				}
				else
				{
					*indoor_light_range = 1;
				}
			}

			if (can_see_outdoors)
			{
				if (entry && entry->client_volume.indoor_volume_properties)
				{
					*can_see_outdoors = entry->client_volume.indoor_volume_properties->can_see_outdoors;
				}
				else
				{
					*can_see_outdoors = false;
				}
			}

			return true;
		}
	}

	return false;
}

bool gfxCheckIsSphereIndoors(const Vec3 world_mid, F32 radius, F32 size, RdrAmbientLight **indoor_ambient_light, F32 *indoor_light_range, bool *can_see_outdoors, const WorldRegion * region)
{
	static WorldVolumeQueryCache *query = NULL;
	static U32 indoor_volume_type;
	const WorldVolume **volumes;
	bool ret = false;

	PERFINFO_AUTO_START_FUNC();

	if (!query)
	{
		query = wlVolumeQueryCacheCreate(PARTITION_CLIENT, wlVolumeQueryCacheTypeNameToBitMask("IndoorObject"), NULL);
		indoor_volume_type = wlVolumeTypeNameToBitMask("Indoor");
	}

	volumes = wlVolumeCacheQuerySphereByType(query, world_mid, radius, indoor_volume_type);
	if (volumes)
		ret = checkIndoorVolumes(volumes, size, indoor_ambient_light, indoor_light_range, can_see_outdoors, indoor_volume_type);
	if (!ret && region)
		ret = region->bUseIndoorLighting;

	PERFINFO_AUTO_STOP();
	return ret;
}

bool gfxCheckIsPointIndoors(const Vec3 world_mid, F32 *indoor_light_range, bool *can_see_outdoors)
{
	return gfxCheckIsSphereIndoors(world_mid, 0, 0, NULL, indoor_light_range, can_see_outdoors, NULL);
}

__forceinline static void averageVec3(const Vec3 inA, const Vec3 inB, Vec3 out)
{
	out[0] = (inA[0] + inB[0]) * 0.5f;
	out[1] = (inA[1] + inB[1]) * 0.5f;
	out[2] = (inA[2] + inB[2]) * 0.5f;
}



bool gfxCheckIsBoxIndoors(const Vec3 world_min, const Vec3 world_max, RdrAmbientLight **indoor_ambient_light, F32 *indoor_light_range, bool *can_see_outdoors, const WorldRegion * region)
{
	static Mat4 box_mat;
	static Vec3 local_min;
	static Vec3 local_max;
	static WorldVolumeQueryCache *query = NULL;
	static U32 indoor_volume_type;
	const WorldVolume **volumes;
	Vec3 world_mid;
	bool ret = false;
	F32 size;

	PERFINFO_AUTO_START_FUNC();
	
	averageVec3(world_max, world_min, world_mid);

	if (!query)
	{
		query = wlVolumeQueryCacheCreate(PARTITION_CLIENT, wlVolumeQueryCacheTypeNameToBitMask("IndoorObject"), NULL);
		indoor_volume_type = wlVolumeTypeNameToBitMask("Indoor");
		copyMat3(unitmat, box_mat);
		setVec3same(local_min, 0.0f);
	}

	copyVec3(world_mid, box_mat[3]);
	subVec3(world_min, world_mid, local_min);
	subVec3(world_max, world_mid, local_max);
	size = boxCalcSize(world_min, world_max);
	
	//volumes = wlVolumeCacheQueryBox(query, box_mat, local_min, local_max);
	volumes = wlVolumeCacheQueryBoxByType(query, box_mat, local_min, local_max, indoor_volume_type);
	if (volumes)
		ret = checkIndoorVolumes(volumes, size, indoor_ambient_light, indoor_light_range, can_see_outdoors, indoor_volume_type);
	if (!ret && region)
		ret = region->bUseIndoorLighting;

	PERFINFO_AUTO_STOP();

	return ret;
}

void gfxDrawLightWireframe(LightData *light_data, int nsegs)
{
	Vec3 hsv, rgb;
	Color color;

	copyVec3(light_data->diffuse_hsv, hsv);
	hsv[2] = 1;
	hsvToRgb(hsv, rgb);
	vec3ToColor(&color, rgb);

	switch (light_data->light_type)
	{
		xcase WL_LIGHT_POINT:
		{
			gfxDrawSphere3D(light_data->world_mat[3], light_data->outer_radius, nsegs, color, 1);
			if (light_data->inner_radius)
				gfxDrawSphere3D(light_data->world_mat[3], light_data->inner_radius, nsegs, color, 1);
		}

		xcase WL_LIGHT_SPOT:
		{
			gfxDrawCone3D_min(light_data->world_mat, light_data->inner_radius, light_data->outer_radius,
				RAD(light_data->outer_cone_angle), 0.f, nsegs, color, color, color, false, true);
		}

		xcase WL_LIGHT_PROJECTOR:
		{
			color.a = 20;
			gfxDrawPyramid3D(light_data->world_mat, light_data->shadow_near_plane, light_data->inner_radius,
				RAD(light_data->inner_cone_angle), RAD(light_data->inner_cone_angle2), color, ColorWhite);
			color.a = 50;
			gfxDrawPyramid3D(light_data->world_mat, light_data->shadow_near_plane, light_data->outer_radius,
				RAD(light_data->outer_cone_angle), RAD(light_data->outer_cone_angle2), color, ColorWhite);
		}
	}
}


void gfxClearLightRegionGraphicsData(GfxLight *light)
{
	light->region_graphics_data = NULL;
}

static void foreachLightFunc(MemoryPool pool, GfxLight *data, void *userData)
{
	if (data->room_assignment_valid && data->owner_room)
	{
		//only do this if it was valid before. If the rooms were remade it will already be removed
		//and we dont want to follow some bogus pointer
		eaFindAndRemoveFast(&data->owner_room->lights_in_room, data);
	}
	data->room_assignment_valid = false; //make the light check which room it's in again
}

void gfxUpdateRoomLightOwnership(Room* room)
{
	PERFINFO_AUTO_START_FUNC();

	// Right now just update all lights for every room
	mpForEachAllocation(MP_NAME(GfxLight), foreachLightFunc, 0);
	gfxForceUpdateLightCaches(NULL, NULL, NULL, true, true, LCIT_ROOMS, NULL);

	eaSetSize(&room->lights_in_room, 0);

	gfxClearShadowSearchData();

	PERFINFO_AUTO_STOP_FUNC();
}

static void gfxUpdateLightRoomOwnership(GfxLight* light)
{
	const WorldVolume** eaVolumes;
	Room* oldRoom;
	bool wasValid;

	PERFINFO_AUTO_START_FUNC();

	wasValid = light->room_assignment_valid;
	oldRoom = light->owner_room;
	if(!light->is_sun && gfxActionAllowsIndoors(gfx_state.currentAction)) {
		eaVolumes = wlVolumeCacheQuerySphereByType(lights_room_query_cache, light->world_query_mid, 0.0f, lights_room_volume_type);
	
		light->owner_room = 0;
		FOR_EACH_IN_EARRAY_FORWARDS(eaVolumes, const WorldVolume, vol)
		{
			WorldVolumeEntry *volume_entry = wlVolumeGetVolumeData(vol);
			if (volume_entry && volume_entry->room)
			{
				light->owner_room = volume_entry->room;
				break;
			}
		}
		FOR_EACH_END;
	}
	
	//If the light changed rooms we need to rebuild all the light caches
	if (light->owner_room != oldRoom || (!wasValid && oldRoom)) //if they are both zero and it was invalid we dont need to do anything
	{
		//only do this if it was valid before. If the rooms were remade it will already be removed
		//and we dont want to follow some bogus pointer
		if (wasValid && oldRoom)
			eaFindAndRemoveFast(&oldRoom->lights_in_room, light);
		
		if (light->owner_room)
			eaPush(&light->owner_room->lights_in_room, light);

		if ((light->owner_room && light->owner_room->limit_contained_lights_to_room)
			|| ((wasValid && oldRoom) && oldRoom->limit_contained_lights_to_room))
		{
			// Only invalidate the caches if there is any way they
			// could have used the room info. Also, only update the
			// light caches we absolutely have to.
			
			LightCacheInvalidateType lightCacheType = 
				light->dynamic ? LCIT_DYNAMIC_LIGHTS : LCIT_STATIC_LIGHTS;

			if(light->owner_room) {

				gfxForceUpdateLightCaches(
					light->owner_room->bounds_min,
					light->owner_room->bounds_max,
					NULL, light->dynamic, !light->dynamic,
					lightCacheType,
					NULL);
			}

			if(oldRoom) {

				gfxForceUpdateLightCaches(
					oldRoom->bounds_min,
					oldRoom->bounds_max,
					NULL, light->dynamic, !light->dynamic,
					lightCacheType,			
					NULL);
			}
		}
		gfxClearShadowSearchData();
	}

	light->room_assignment_valid = true;

	PERFINFO_AUTO_STOP_FUNC();
}

Room* gfxGetOrUpdateLightOwnerRoom(GfxLight* light)
{
	if (!light->room_assignment_valid)
		gfxUpdateLightRoomOwnership(light);

	assert(light->room_assignment_valid);
	return light->owner_room;
}
