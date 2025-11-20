/***************************************************************************



***************************************************************************/

#include "MemoryPool.h"
#include "SystemSpecs.h"

#include "GfxLightsPrivate.h"
#include "GfxPostprocess.h"
#include "GfxDebug.h"
#include "GfxOcclusion.h"
#include "GfxOcclusionTypes.h"
#include "GfxWorld.h"
#include "GfxSurface.h"
#include "GfxTexturesInline.h"
#include "GfxPrimitive.h"
#include "GfxConsole.h"
#include "bounds.h"

#include "RdrState.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););


#define ONE_THIRD (1.f/3.f)



MP_DEFINE(GfxShadowMap);
MP_DEFINE(GfxLightShadowsPerAction);

int disable_dir_shadow_threshold = 0; // externed in GraphicsLib.c
AUTO_CMD_INT(disable_dir_shadow_threshold, disable_dir_shadow_threshold);

static int dbg_shadow_hull = 0;
AUTO_CMD_INT(dbg_shadow_hull, shadowHullDebug);

static int force_full_shadowmap_use = 0;
AUTO_CMD_INT(force_full_shadowmap_use, force_full_shadowmap_use);

AUTO_COMMAND;
void clearShadowHullDebug(void)
{
	gfx_state.debug.polyset = NULL;
}

static int dbg_shadow_spheres;
AUTO_CMD_INT(dbg_shadow_spheres, shadowSphereDebug);

static int show_shadow_sphere_info;
AUTO_CMD_INT(show_shadow_sphere_info, shadowSphereDisplayInfo);


static int debug_shadow_hull_creation = 0;
AUTO_CMD_INT(debug_shadow_hull_creation, debugShadowHullCreation);

static GPolySet shadow_debug_polyset = {0};

static float csaaMinDepthBiasPoint = 0.0006f;
AUTO_CMD_FLOAT(csaaMinDepthBiasPoint, csaaMinDepthBiasPoint);

static float csaaMinDepthBiasSpot = 0.002f;
AUTO_CMD_FLOAT(csaaMinDepthBiasSpot, csaaMinDepthBiasSpot);

static float atiMinDepthBiasSpot = 0.002f;
static float atiMinDepthBiasPoint = 0.0003f;

static float shadowmapZNear = 0.17f;
AUTO_CMD_FLOAT(shadowmapZNear, shadowmapZNear);

static bool allow_shadow_map_resize = 0;
AUTO_CMD_INT(allow_shadow_map_resize, allow_shadow_map_resize);

//////////////////////////////////////////////////////////////////////////


#define DIR_SHADOW_MAP_DEPTH_BIAS 0.1f
#define DIR_SHADOW_MAP_DEPTH_BIAS2 0.2f
#define DIR_SHADOW_MAP_SLOPE_SCALE_DEPTH_BIAS 2.0f
#define SLOPE_SCALE_DEPTH_BIAS 2.0f
#define SHADOWMAP_ZNEAR shadowmapZNear

#define SPOT_SHADOW_MAP_DEPTH_BIAS ((light_dist * 0.01f + 5.0f) / pow(smap->depth_range, 2))
#define POINT_SHADOW_MAP_DEPTH_BIAS ((light_dist * 0.01f + 0.25f) / pow(smap->depth_range, 2))

// a light taking up half the screen or more should be at highest quality
#define SCREEN_AREA_TO_SHADOW_QUALITY 2

static const Mat3 point_light_dir_mat[6] = 
{
	// +y
	{
		{ -1, 0, 0},
		{ 0, 0,-1},
		{ 0,-1, 0},
	},

	// +x
	{
		{ 0, 1, 0},
		{ 0, 0,-1},
		{-1, 0, 0},
	},

	// -y
	{
		{ 1, 0, 0},
		{ 0, 0,-1},
		{ 0, 1, 0},
	},

	// -z
	{
		{ 1, 0, 0},
		{ 0,-1, 0},
		{ 0, 0,-1},
	},

	// -x
	{
		{ 0, 0, 1},
		{ 0,-1, 0},
		{ 1, 0, 0},
	},

	// +z
	{
		{-1, 0, 0},
		{ 0,-1, 0},
		{ 0, 0, 1},
	},
};

// x, width, y, height
static const Vec4 point_light_viewport[6] = 
{
	// +y
	{0.f, ONE_THIRD, 0.f, 0.5f},

	// +x
	{ONE_THIRD, ONE_THIRD, 0.f, 0.5f},

	// -y
	{2*ONE_THIRD, ONE_THIRD, 0.f, 0.5f},

	// -z
	{0.f, ONE_THIRD, 0.5f, 0.5f},

	// -x
	{ONE_THIRD, ONE_THIRD, 0.5f, 0.5f},

	// +z
	{2*ONE_THIRD, ONE_THIRD, 0.5f, 0.5f},
};

__forceinline static GfxLightShadowsPerAction *getShadowData(GfxLight *light)
{
	while (gfx_state.currentActionIdx >= eaSize(&light->shadow_data_per_action))
	{
		GfxLightShadowsPerAction *shadow_data;
		MP_CREATE(GfxLightShadowsPerAction, 32);
		shadow_data = MP_ALLOC(GfxLightShadowsPerAction);
		if (eaSize(&light->shadow_data_per_action))
			shadow_data->shadowmap_size = light->shadow_data_per_action[0]->shadowmap_size;
		if (!shadow_data->shadowmap_size)
			shadow_data->shadowmap_size = gfx_state.pssm_shadowmap_size; // just a guess, but keeps the shadows from flickering
		eaPush(&light->shadow_data_per_action, shadow_data);
	}

	ANALYSIS_ASSUME(light->shadow_data_per_action);
	return light->shadow_data_per_action[gfx_state.currentActionIdx];
}

static void freeShadowData(GfxLightShadowsPerAction *shadow_data)
{
	int i;

	if (!shadow_data)
		return;

	if (shadow_data->shadow_surface)
	{
		gfxReleaseTempSurface(shadow_data->shadow_surface);
		shadow_data->shadow_surface = NULL;
	}

	for (i = 0; i < shadow_data->shadowmap_count; ++i)
	{
		if (shadow_data->shadowmaps[i])
		{
			hullFreeData(&shadow_data->shadowmaps[i]->light_frustum.hull);
			gpsetFreeData(&shadow_data->shadowmaps[i]->tempshadowcaster_hull);
			gpsetFreeData(&shadow_data->shadowmaps[i]->shadowcaster_hull);
			gpsetFreeData(&shadow_data->shadowmaps[i]->shadowreceiver_hull);
			MP_FREE(GfxShadowMap, shadow_data->shadowmaps[i]);
			shadow_data->shadowmaps[i] = NULL;
		}
	}

	hullFreeData(&shadow_data->global_frustum.hull);
	MP_FREE(GfxLightShadowsPerAction, shadow_data);
}

void gfxFreeShadows(GfxLight *light)
{
	eaDestroyEx(&light->shadow_data_per_action, freeShadowData);
}

void gfxShadowsBeginFrame(const Frustum *camera_frustum, const Mat44 projection_mat)
{
	GfxGlobalDrawParams *gdraw = SAFE_MEMBER_ADDR(gfx_state.currentAction, gdraw);
	RdrSurfaceParams surfaceparams = {0};
	GfxLightShadowsPerAction *best_point_light_shadow_data = NULL;
	int i, cur_surface_idx = 0;
	int pssm_shadowmap_size;

	if (!gdraw)
		return;

	if (gfx_state.pssm_shadowmap_size_override) {
		pssm_shadowmap_size = gfx_state.pssm_shadowmap_size_override;
	} else {
		pssm_shadowmap_size = gfx_state.pssm_shadowmap_size;
	}

	surfaceparams.name = "Shadowmap";
	surfaceparams.depth_bits = 24;
	surfaceparams.desired_multisample_level = 1;
	surfaceparams.required_multisample_level = 1;
	surfaceparams.flags = SF_DEPTHONLY | SF_DEPTH_TEXTURE | SF_SHADOW_MAP;
	// minimize the size of the wasted visual surface
	surfaceparams.buffer_types[0] = SBT_RGB16;
	rdrSetDefaultTexFlagsForSurfaceParams(&surfaceparams);
	if (gfxStereoscopicActive())
		surfaceparams.stereo_option = SURF_STEREO_FORCE_OFF;

	// assign quality settings to lights
	for (i = 0; i < eaSize(&gdraw->this_frame_shadowmap_lights); ++i)
	{
		GfxLight *light = gdraw->this_frame_shadowmap_lights[i];
		GfxLightShadowsPerAction *shadow_data = getShadowData(light);
		RdrLightType light_type = rdrGetSimpleLightType(light->orig_light_type);

		if (light_type == RDRLIGHT_DIRECTIONAL)
		{
			shadow_data->shadow_quality = 1;
		}
		else
		{
			Vec4_aligned eye_bounds[8];
			F32 screen_area = 0;

			mulBounds(light->static_entry.bounds.min, light->static_entry.bounds.max, camera_frustum->viewmat, eye_bounds);
			gfxGetScreenSpace(camera_frustum, projection_mat, 1, eye_bounds, &screen_area);

			shadow_data->shadow_quality = CLAMP(screen_area * SCREEN_AREA_TO_SHADOW_QUALITY, 0.2f, 1.f);

			if (light_type == RDRLIGHT_POINT && shadow_data->shadow_quality >= 0.666666f && (!best_point_light_shadow_data || best_point_light_shadow_data->shadow_quality < shadow_data->shadow_quality))
				best_point_light_shadow_data = shadow_data;
		}
	}

	// assign surfaces to lights
	for (i = 0; i < eaSize(&gdraw->this_frame_shadowmap_lights); ++i)
	{
		GfxLight *light = gdraw->this_frame_shadowmap_lights[i];
		GfxLightShadowsPerAction *shadow_data = getShadowData(light);
		RdrLightType light_type = rdrGetSimpleLightType(light->orig_light_type);
		int x_multiplier = 1, y_multiplier = 1;
		int max_width, max_height;

		if (force_full_shadowmap_use) {
			shadow_data->shadow_quality = 1.0f;
		} else if (gfx_state.debug.forceShadowQuality > 0) {
			shadow_data->shadow_quality = CLAMP(gfx_state.debug.forceShadowQuality, 0.2f, 1.f);
		}

		switch (light_type)
		{
			xcase RDRLIGHT_DIRECTIONAL:
				surfaceparams.name = "dir shadowmap";
				x_multiplier = 2;
				y_multiplier = 2;

			xcase RDRLIGHT_SPOT:
				surfaceparams.name = "spot shadowmap";

			xcase RDRLIGHT_PROJECTOR:
				surfaceparams.name = "projector shadowmap";

			xcase RDRLIGHT_POINT:
				surfaceparams.name = "point shadowmap";

				if (!best_point_light_shadow_data) {
					best_point_light_shadow_data = shadow_data;
				}

                x_multiplier = best_point_light_shadow_data == shadow_data ? 3 : 2;
				y_multiplier = 2;
		}

		max_width = x_multiplier * pssm_shadowmap_size;
		max_height = y_multiplier * pssm_shadowmap_size;
		setVec2(shadow_data->used_surface_size, 
			round(floorf(shadow_data->shadow_quality * max_width)), 
			round(floorf(shadow_data->shadow_quality * max_height)));

		shadow_data->used_surface_size[0] += shadow_data->used_surface_size[0] % x_multiplier;
		shadow_data->used_surface_size[1] += shadow_data->used_surface_size[1] % y_multiplier;

		if (allow_shadow_map_resize) {
			rdrSurfaceParamSetSizeSafe(&surfaceparams,
				ALIGNUP(shadow_data->used_surface_size[0], pssm_shadowmap_size),
				ALIGNUP(shadow_data->used_surface_size[1], pssm_shadowmap_size));
		} else {
			rdrSurfaceParamSetSizeSafe(&surfaceparams, 
				pssm_shadowmap_size * x_multiplier,
				pssm_shadowmap_size * y_multiplier);
		}

		shadow_data->shadow_surface = gfxGetTempSurface(&surfaceparams);

		shadow_data->used_surface_size_ratio[0] = saturate(shadow_data->used_surface_size[0] / ((F32)surfaceparams.width));
		shadow_data->used_surface_size_ratio[1] = saturate(shadow_data->used_surface_size[1] / ((F32)surfaceparams.height));

		shadow_data->shadowmap_size = MAX(shadow_data->used_surface_size[0] / x_multiplier, shadow_data->used_surface_size[1] / y_multiplier);
	}
}

void gfxShadowsEndFrame(void)
{
	GfxGlobalDrawParams *gdraw = SAFE_MEMBER_ADDR(gfx_state.currentAction, gdraw);
	int i;

	if (!gdraw)
		return;

	for (i = 0; i < eaSize(&gdraw->this_frame_shadowmap_lights); ++i)
	{
		GfxLight *light = gdraw->this_frame_shadowmap_lights[i];
		GfxLightShadowsPerAction *shadow_data = getShadowData(light);

		if (shadow_data->shadow_surface)
		{
			gfxReleaseTempSurface(shadow_data->shadow_surface);
			shadow_data->shadow_surface = NULL;
		}
	}
}

static float g_shadowThinCasterSize = 0.75f;
AUTO_CMD_FLOAT(g_shadowThinCasterSize, thin_shadow_caster_size) ACMD_CATEGORY(Debug);

bool gfxIsThinShadowCaster(const Vec3 bmin, const Vec3 bmax)
{
	Vec3 diag;
	float smallestDim;

	subVec3(bmax, bmin, diag);

	smallestDim = MINF(MINF(diag[0], diag[1]), diag[2]);
	return smallestDim <= g_shadowThinCasterSize;
}

static GfxShadowMap *getShadowmapForLight(GfxLight *light, int smap_idx, bool create_if_not_found)
{
	GfxLightShadowsPerAction *shadow_data = getShadowData(light);

	assert(smap_idx >= 0 && smap_idx < ARRAY_SIZE(shadow_data->shadowmaps));
	
	if (smap_idx >= shadow_data->shadowmap_count)
		shadow_data->shadowmap_count = smap_idx+1;

	if (!shadow_data->shadowmaps[smap_idx] && create_if_not_found)
	{
		MP_CREATE(GfxShadowMap, 64);
		shadow_data->shadowmaps[smap_idx] = MP_ALLOC(GfxShadowMap);

		//if (smap_idx == 0 && rdrGetSimpleLightType(light->orig_light_type) == RDRLIGHT_DIRECTIONAL)
		//	shadow_data->shadowmaps[smap_idx]->occlusion_buffer = zoCreate(&shadow_data->shadowmaps[smap_idx]->light_frustum, false);
	}

	return shadow_data->shadowmaps[smap_idx];
}

int gfxDebugShadowFix = 1;
AUTO_CMD_INT(gfxDebugShadowFix, gfxDebugShadowFix);

float gfxDebugShadowBiasScale = 0.999899f;
AUTO_CMD_FLOAT(gfxDebugShadowBiasScale, gfxDebugShadowBiasScale);

float gfxDebugShadowBiasOffset = 0.009999f;
AUTO_CMD_FLOAT(gfxDebugShadowBiasOffset, gfxDebugShadowBiasOffset);

static void updateRdrLight(GfxLight *light, const Frustum *camera_frustum, int shadow_idx)
{
	GfxLightShadowsPerAction *shadow_data = getShadowData(light);
	GfxShadowMap *smap = shadow_data->shadowmaps[shadow_idx];
	RdrShadowmap *rsmap = &light->rdr_light.shadowmap[shadow_idx];
	Mat44 worldToLightspace44, cameraToWorld44, cameraToLightspace44, cameraToProjected44, dxScaleOffset44, worldToClouds;
	int i;

	assert(shadow_idx < ARRAY_SIZE(shadow_data->shadowmaps));
	assert(shadow_idx < ARRAY_SIZE(light->rdr_light.shadowmap));

	rsmap->near_fade = smap->near_fade;
	rsmap->far_fade = smap->far_fade;

	mat43to44(camera_frustum->inv_viewmat, cameraToWorld44);

	if (shadow_data->shadow_tex_handle)
	{
		setVec2(light->rdr_light.shadowmap_texture_size, shadow_data->shadow_surface->surface->width_nonthread, shadow_data->shadow_surface->surface->height_nonthread);

		// this matrix fits the range to 0-1 and offsets by half a texel
		setVec4(dxScaleOffset44[0], 0.5f * shadow_data->used_surface_size_ratio[0], 0, 0, 0);
		setVec4(dxScaleOffset44[1], 0, 0.5f * shadow_data->used_surface_size_ratio[1], 0, 0);
		setVec4(dxScaleOffset44[2], 0, 0, 1, 0);
#if _PS3
		setVec4(dxScaleOffset44[3], 0.5f * shadow_data->used_surface_size_ratio[0], 
			0.5f * shadow_data->used_surface_size_ratio[1], 
			0, 1);
#else
		setVec4(dxScaleOffset44[3], 0.5f * shadow_data->used_surface_size_ratio[0] + 0.5f / light->rdr_light.shadowmap_texture_size[0], 
			0.5f * shadow_data->used_surface_size_ratio[1] + 0.5f / light->rdr_light.shadowmap_texture_size[1], 
			0, 1);
#endif
		mat43to44(smap->light_frustum.viewmat, worldToLightspace44);

		if (smap->use_unitviewrot)
		{
			Mat4 cammat, viewmat, inv_viewmat;
			copyVec3(smap->light_frustum.cammat[3], cammat[3]);
			setVec3(cammat[0], -1, 0, 0);
			setVec3(cammat[1], 0, 1, 0);
			setVec3(cammat[2], 0, 0, -1);
			makeViewMatrix(cammat, viewmat, inv_viewmat);
			mat43to44(viewmat, worldToLightspace44);
		}

		mulMat44Inline(worldToLightspace44, cameraToWorld44, cameraToLightspace44);

		if (smap->use_lightspace_transform)
			mulMat44Inline(smap->receiver_projection_matrix, cameraToLightspace44, cameraToProjected44);
		else
			copyMat44(smap->receiver_projection_matrix, cameraToProjected44);

		if (smap->use_dxoffset)
			mulMat44Inline(dxScaleOffset44, cameraToProjected44, rsmap->camera_to_shadowmap);
		else
			copyMat44(cameraToProjected44, rsmap->camera_to_shadowmap);


		if(gfxDebugShadowFix &&
		   (rdrGetSimpleLightType(light->orig_light_type) == RDRLIGHT_PROJECTOR ||
			rdrGetSimpleLightType(light->orig_light_type) == RDRLIGHT_SPOT)) {

			// Add a bias AND scale the Z-value for the shadow tests.
			int k;
			rsmap->camera_to_shadowmap[3][2] -= gfxDebugShadowBiasOffset;
			for(k = 0; k < 3; k++) {
				rsmap->camera_to_shadowmap[k][2] *= gfxDebugShadowBiasScale;
			}
		}

		if (smap->use_texel_offset_only)
		{
#if _PS3
			rsmap->camera_to_shadowmap[0][0] = 0;
			rsmap->camera_to_shadowmap[1][0] = 0;
#else
			rsmap->camera_to_shadowmap[0][0] = 0.5f / light->rdr_light.shadowmap_texture_size[0];
			rsmap->camera_to_shadowmap[1][0] = 0.5f / light->rdr_light.shadowmap_texture_size[1];
#endif
		}

		if (smap->use_shadowmap_used_ratio_only)
		{
			rsmap->camera_to_shadowmap[0][0] = shadow_data->used_surface_size_ratio[0];
			rsmap->camera_to_shadowmap[1][0] = shadow_data->used_surface_size_ratio[1];
		}

		light->rdr_light.shadowmap_texture = shadow_data->shadow_tex_handle;
	}
	else
	{
		light->rdr_light.shadowmap_texture = 0;
	}

	// clouds:
	for (i = 0; i < MAX_CLOUD_LAYERS; ++i)
	{
		F32 inv_scale = 1.f/(light->cloud_layers[i].scale?light->cloud_layers[i].scale:1);

		shadow_data->cloud_scroll[i][0] = light->cloud_layers[i].scroll_rate[0] * gfx_state.client_loop_timer;
		shadow_data->cloud_scroll[i][0] -= round(0.5f * shadow_data->cloud_scroll[i][0]) - 1; // Range of [-1,1]
		shadow_data->cloud_scroll[i][1] = light->cloud_layers[i].scroll_rate[1] * gfx_state.client_loop_timer;
		shadow_data->cloud_scroll[i][1] -= round(0.5f * shadow_data->cloud_scroll[i][1]) - 1; // Range of [-1,1]

		setVec4(worldToClouds[0], inv_scale, 0, 0, 0);
		setVec4(worldToClouds[1], 0, 0, inv_scale, 0);
		setVec4(worldToClouds[2], 0, inv_scale, 0, 0);
		setVec4(worldToClouds[3], shadow_data->cloud_scroll[i][0], shadow_data->cloud_scroll[i][1], 0, 1);
		mulMat44Inline(worldToClouds, cameraToWorld44, light->rdr_light.cloud_layers[i].camera_to_cloud_texture);
	}
}

static void updateRdrStats(GfxLight *light, RdrDrawList *draw_list, int start_draw_count, int start_tri_count)
{
	GfxLightShadowsPerAction *shadow_data = getShadowData(light);
	int end_draw_count, end_tri_count;

	shadow_data->last_update_frame = gfx_state.currentDevice->frame_count_of_last_update;

	rdrDrawListGetForegroundStats(draw_list, NULL, NULL, NULL, NULL, &end_draw_count, &end_tri_count, NULL, NULL);
	shadow_data->last_draw_count = end_draw_count - start_draw_count;
	shadow_data->last_tri_count = end_tri_count - start_tri_count;
}

//////////////////////////////////////////////////////////////////////////
// point light
#define OFFSET 0.003876f	// 1 pixel buffer at shadowmap size of 256, 4 pixel buffer at shadowmap size of 1024
static int cubemap_lookup_size = 256;

AUTO_CMD_INT(cubemap_lookup_size, cubemap_lookup_size) ACMD_CATEGORY(Debug);

void gfxSetCubemapLookupTexture(RdrDevice *rdr_device, bool active)
{
	RdrSurface *cubemap_surface = NULL;
	if (!rdr_device || !rdr_device->is_locked_nonthread)
		return;
	if (active)
		cubemap_surface = SAFE_MEMBER2(gfx_state.currentDevice, cubemap_lookup_surface, surface);
	if (!cubemap_surface || gfx_state.currentSurface == cubemap_surface)
		rdrSetCubemapLookupTexture(gfx_state.currentDevice->rdr_device, RDR_NULL_TEXTURE);
	else
		rdrSetCubemapLookupTexture(gfx_state.currentDevice->rdr_device, rdrSurfaceToTexHandle(cubemap_surface, SBUF_0));
}

static void fillCubemapFace(RdrSurface *surface, RdrSurfaceFace face, const Mat44 post_coord_transform, F32 xoffset, F32 yoffset)
{
	RdrScreenPostProcess ppscreen = {0};
	Vec4 constants[5];
	Mat44 pre_coord_transform;

	if (face == RSF_POSITIVE_Y)
	{
		setVec4(pre_coord_transform[0], 2, 0, 0, 0);
		setVec4(pre_coord_transform[1], 0, 2, 0, 0);
		setVec4(pre_coord_transform[2], 0, 0, 1, 0);
		setVec4(pre_coord_transform[3], -1 + xoffset, -1 + yoffset, 0, 1);
	}
	else if (face == RSF_NEGATIVE_Y)
	{
		setVec4(pre_coord_transform[0], -2, 0, 0, 0);
		setVec4(pre_coord_transform[1], 0, -2, 0, 0);
		setVec4(pre_coord_transform[2], 0, 0, 1, 0);
		setVec4(pre_coord_transform[3], 1 + xoffset, 1 + yoffset, 0, 1);
	}
	else
	{
		setVec4(pre_coord_transform[0], 0, -2, 0, 0);
		setVec4(pre_coord_transform[1], 2, 0, 0, 0);
		setVec4(pre_coord_transform[2], 0, 0, 1, 0);
		setVec4(pre_coord_transform[3], -1 + xoffset, 1 + yoffset, 0, 1);
	}

	gfxSetActiveSurfaceEx(surface, 0, face);

	setVec4(constants[0], xoffset, yoffset, 0, 0);
	getMatRow(post_coord_transform, 0, constants[1]);
	getMatRow(post_coord_transform, 1, constants[2]);
	getMatRow(pre_coord_transform, 0, constants[3]);
	getMatRow(pre_coord_transform, 1, constants[4]);

	ppscreen.tex_width = surface->width_nonthread;
	ppscreen.tex_height = surface->height_nonthread;
	ppscreen.material.const_count = ARRAY_SIZE(constants);
	ppscreen.material.constants = constants;
	ppscreen.shader_handle = gfxDemandLoadSpecialShader(GSS_FILL_CUBEMAP);
	ppscreen.blend_type = RPPBLEND_REPLACE;
	gfxPostprocessScreen(&ppscreen);
}

static void updateCubemapLookupSurface(void)
{
	Mat44 row_proj, column_rotate, receiver_matrix, temp_matrix, range_compress;
	Vec3 mn, mx;

	if (!gfx_state.currentDevice->cubemap_lookup_surface || gfx_state.currentDevice->cubemap_lookup_surface->surface->width_nonthread != cubemap_lookup_size)
	{
		//////////////////////////////////////////////////////////////////////////
		// create surface
		RdrSurfaceParams surfaceparams = {0};
		surfaceparams.desired_multisample_level = 1;
		surfaceparams.required_multisample_level = 1;
		surfaceparams.flags = SF_CUBEMAP;
		rdrSurfaceParamSetSizeSafe(&surfaceparams, cubemap_lookup_size, cubemap_lookup_size);
#if _PS3
        surfaceparams.buffer_types[0] = SBT_RGBA;
#else
		surfaceparams.buffer_types[0] = SBT_RG_FIXED;
#endif
		rdrSetDefaultTexFlagsForSurfaceParams(&surfaceparams);
		surfaceparams.buffer_default_flags[0] &= ~(RTF_MAG_POINT|RTF_MIN_POINT); // linear filter
		surfaceparams.name = "Shadow cubemap lookup texture";

		if (gfx_state.currentDevice->cubemap_lookup_surface)
			gfxReleaseTempSurface(gfx_state.currentDevice->cubemap_lookup_surface);
		gfx_state.currentDevice->cubemap_lookup_surface = gfxGetTempSurface(&surfaceparams);

		gfx_state.currentDevice->cubemap_rendered_frame_start = gfx_state.currentDevice->per_device_frame_count;
	}

	// render once for each GPU so that they all have the data
	if ((gfx_state.allRenderersFeatures & FEATURE_DX11_RENDERER) || (gfx_state.currentDevice->per_device_frame_count - gfx_state.currentDevice->cubemap_rendered_frame_start < rdr_state.gpu_count))
	{
		//////////////////////////////////////////////////////////////////////////
		// fill cubemap data: row 1
		setVec4(range_compress[0], 0.5f, 0, 0, 0);
		setVec4(range_compress[1], 0, 0.5f, 0, 0);
		setVec4(range_compress[2], 0, 0, 1, 0);
		setVec4(range_compress[3], 0.5f, 0.5f, 0, 1);
		setVec3(mn, -3, -1, -1);
		setVec3(mx, 3, 1, 0);
		createScaleTranslateFitMat(temp_matrix, mn, mx, 
			-1, 1, 
			-OFFSET, -1, // flip the y axis
			1, 0);
		mulMat44Inline(range_compress, temp_matrix, receiver_matrix);

		fillCubemapFace(gfx_state.currentDevice->cubemap_lookup_surface->surface, RSF_POSITIVE_Y, receiver_matrix, -2, 0);
		fillCubemapFace(gfx_state.currentDevice->cubemap_lookup_surface->surface, RSF_POSITIVE_X, receiver_matrix, 0, 0);
		fillCubemapFace(gfx_state.currentDevice->cubemap_lookup_surface->surface, RSF_NEGATIVE_Y, receiver_matrix, 2, 0);


		//////////////////////////////////////////////////////////////////////////
		// fill cubemap data: row 2
		setVec3(mn, -3, -1, -1);
		setVec3(mx, 3, 1, 0);
		createScaleTranslateFitMat(row_proj, mn, mx, 
			-1, 1, 
			1, OFFSET, // flip the y axis
			1, 0);

		// 90 degree counterclockwise rotation
		copyMat44(unitmat44, column_rotate);
		setVec4(column_rotate[0], 0, 1, 0, 0);
		setVec4(column_rotate[1], -1, 0, 0, 0);
		mulMat44Inline(row_proj, column_rotate, temp_matrix);
		mulMat44Inline(range_compress, temp_matrix, receiver_matrix);

		fillCubemapFace(gfx_state.currentDevice->cubemap_lookup_surface->surface, RSF_NEGATIVE_Z, receiver_matrix, 0, 2);
		fillCubemapFace(gfx_state.currentDevice->cubemap_lookup_surface->surface, RSF_NEGATIVE_X, receiver_matrix, 0, 0);
		fillCubemapFace(gfx_state.currentDevice->cubemap_lookup_surface->surface, RSF_POSITIVE_Z, receiver_matrix, 0, -2);
	}
}

void gfxShadowFlushCubemapLookupSurfaceOnDeviceLoss(void)
{
	gfxReleaseTempSurface(gfx_state.currentDevice->cubemap_lookup_surface);
	gfx_state.currentDevice->cubemap_lookup_surface = NULL;
}

void gfxShadowPointLight(GfxLight *point_light, const Frustum *camera_frustum, RdrDrawList *draw_list)
{
	GfxLightShadowsPerAction *shadow_data = getShadowData(point_light);
	int i, map_index, dummy_shadow_pass;
	Mat4 lightspace_matrix;

	assert(shadow_data->shadowmap_count >= 0 && shadow_data->shadowmap_count <= ARRAY_SIZE(shadow_data->shadowmaps));
	for (map_index = 0; map_index < shadow_data->shadowmap_count; ++map_index)
	{
		GfxShadowMap *smap = getShadowmapForLight(point_light, map_index, false);
		if (smap)
			smap->shadow_pass = -1;
	}

	if (gfx_state.debug.no_render_shadowmaps || !shadow_data->shadowmap_size)
		return;

	PERFINFO_AUTO_START_FUNC();

	point_light->light_colors.min_shadow_val = 1.0f - point_light->shadow_transition;
	for (i = 0; i < RLCT_COUNT; ++i)
		point_light->rdr_light.light_colors[i].min_shadow_val = point_light->light_colors.min_shadow_val;

	shadow_data->shadowmap_count = 6;
	assert(shadow_data->shadowmap_count >= 0 && shadow_data->shadowmap_count <= ARRAY_SIZE(shadow_data->shadowmaps));

	// build lightspace basis
	copyMat3(unitmat, point_light->shadow_data.lightspace_matrix);
	copyVec3(point_light->rdr_light.world_mat[3], point_light->shadow_data.lightspace_matrix[3]);

	// setup frustum
	copyMat3(point_light_dir_mat[0], lightspace_matrix);
	copyVec3(point_light->shadow_data.lightspace_matrix[3], lightspace_matrix[3]);
	frustumSetCameraMatrix(&shadow_data->global_frustum, lightspace_matrix);
	frustumSetSphere(&shadow_data->global_frustum, point_light->rdr_light.point_spot_params.outer_radius);
	dummy_shadow_pass = rdrDrawListAddPass(draw_list, &shadow_data->global_frustum, RDRSHDM_DUMMY, &point_light->rdr_light, -1, -1, true);

	if (gfx_state.debug.disable_multiple_point_light_shadow_passes)
		dummy_shadow_pass = -1;

	assert(shadow_data->shadowmap_count >= 0 && shadow_data->shadowmap_count <= ARRAY_SIZE(shadow_data->shadowmaps));
	for (map_index = 0; map_index < shadow_data->shadowmap_count; ++map_index)
	{
		GfxShadowMap *smap = getShadowmapForLight(point_light, map_index, true);

		smap->shadow_pass = -1;
		smap->depth_range = point_light->rdr_light.point_spot_params.outer_radius;

		// setup frustum
		copyMat3(point_light_dir_mat[map_index], lightspace_matrix);
		copyVec3(point_light->shadow_data.lightspace_matrix[3], lightspace_matrix[3]);
		frustumSetCameraMatrix(&smap->light_frustum, lightspace_matrix);
		if (dummy_shadow_pass >= 0)
			frustumSet(&smap->light_frustum, 90, 1.0f, SHADOWMAP_ZNEAR, smap->depth_range);
		else
			frustumSetSphere(&smap->light_frustum, smap->depth_range);

		// get shadow casters
		if (dummy_shadow_pass >= 0)
		{
			Vec3 bounds_min, bounds_max;
			gpsetMakeFrustum(&smap->shadowcaster_hull, lightspace_matrix, 90, 90, SHADOWMAP_ZNEAR, smap->depth_range, 1);
			gpsetBounds(&smap->shadowcaster_hull, bounds_min, bounds_max);
			if (frustumCheckBoundingBox(camera_frustum, bounds_min, bounds_max, NULL, false))
				smap->shadow_pass = rdrDrawListAddPass(draw_list, &smap->light_frustum, RDRSHDM_SHADOW, &point_light->rdr_light, map_index, dummy_shadow_pass, true);
		}
		else if (map_index == 0)
		{
			smap->shadow_pass = rdrDrawListAddPass(draw_list, &smap->light_frustum, RDRSHDM_SHADOW, &point_light->rdr_light, map_index, -1, true);
		}
		else
		{
			smap->shadow_pass = shadow_data->shadowmaps[0]->shadow_pass;
		}
	}

	PERFINFO_AUTO_STOP();

}

void gfxShadowPointLightCalcProjMatrix(GfxLight *point_light, const Frustum *camera_frustum)
{
	GfxLightShadowsPerAction *shadow_data = getShadowData(point_light);
// 	F32 offset = BUFFER_SIZE / ((F32)(2 * BUFFER_SIZE + shadow_data->shadowmap_size));
	F32 buffer_size = OFFSET * shadow_data->shadowmap_size / (1 - 2 * OFFSET);
	F32 fovy = 2 * DEG(atan(1 + buffer_size / (shadow_data->shadowmap_size * 0.5f)));
	F32 fovx = 90.f;
	int map_index;

	if (gfx_state.debug.no_render_shadowmaps || !shadow_data->shadowmap_size)
		return;

	PERFINFO_AUTO_START_FUNC();

	assert(shadow_data->shadowmap_count >= 0 && shadow_data->shadowmap_count <= ARRAY_SIZE(shadow_data->shadowmaps));
	for (map_index = 0; map_index < shadow_data->shadowmap_count; ++map_index)
	{
		GfxShadowMap *smap = shadow_data->shadowmaps[map_index];
		Mat44 render_proj_matrix, scale_matrix;
		Vec3 mn, mx;

		// compute matrices for drawing into the shadowmap, setting up projection so there is a buffer between the two rows:
		rdrSetupPerspectiveProjection(render_proj_matrix, fovy, fovx/fovy, SHADOWMAP_ZNEAR, smap->depth_range);
		if (map_index < 3)
		{
			// row 1
			setVec3(mn, -1, -1, -1);
			setVec3(mx, 1, 1 - OFFSET, 1);
			createScaleTranslateFitMat(scale_matrix, mn, mx, -1, 1, -1, 1, -1, 1);
			mulMat44Inline(scale_matrix, render_proj_matrix, smap->render_projection_matrix);
		}
		else
		{
			// row 2
			setVec3(mn, -1, -1 + OFFSET, -1);
			setVec3(mx, 1, 1, 1);
			createScaleTranslateFitMat(scale_matrix, mn, mx, -1, 1, -1, 1, -1, 1);
			mulMat44Inline(scale_matrix, render_proj_matrix, smap->render_projection_matrix);
		}

		// compute matrices for shadowmap lookup in the shader:
		if (map_index == 0)
		{
			// lightspace matrix, no projection
			copyMat44(unitmat44, smap->receiver_projection_matrix);
			smap->use_unitviewrot = 1;
			smap->use_dxoffset = 0;
			smap->use_lightspace_transform = 1;
			smap->use_texel_offset_only = 0;
			smap->use_shadowmap_used_ratio_only = 0;
		}
		else if (map_index == 1)
		{
			// row 1
			setVec3(mn, -3, -1, -smap->depth_range);
			setVec3(mx, 3, 1, 0);
			createScaleTranslateFitMat(smap->receiver_projection_matrix, mn, mx, 
				-1, 1, 
				-OFFSET, -1, // flip the y axis
				1, 0);
			smap->use_unitviewrot = 0;
			smap->use_dxoffset = 0;
			smap->use_lightspace_transform = 0;
			smap->use_texel_offset_only = 0;
			smap->use_shadowmap_used_ratio_only = 1;

			// values for converting z to depth
			smap->near_fade = -smap->render_projection_matrix[2][2]; // negative because our shader z values will be positive
			smap->far_fade = smap->render_projection_matrix[3][2];
		}
		else if (map_index == 2)
		{
			// row 2
			Mat44 row_proj, column_rotate;
			setVec3(mn, -3, -1, -smap->depth_range);
			setVec3(mx, 3, 1, 0);
			createScaleTranslateFitMat(row_proj, mn, mx, 
				-1, 1, 
				1, OFFSET, // flip the y axis
				1, 0);

			// 90 degree counterclockwise rotation
			copyMat44(unitmat44, column_rotate);
			setVec4(column_rotate[0], 0, 1, 0, 0);
			setVec4(column_rotate[1], -1, 0, 0, 0);

			mulMat44Inline(row_proj, column_rotate, smap->receiver_projection_matrix);

			smap->use_unitviewrot = 0;
			smap->use_dxoffset = 1;
			smap->use_lightspace_transform = 0;
			smap->use_texel_offset_only = 0;
			smap->use_shadowmap_used_ratio_only = 0;

			// values for converting z to depth
			smap->near_fade = -smap->render_projection_matrix[2][3]; // negative because our shader z values will be positive
			smap->far_fade = smap->render_projection_matrix[3][3];
		}
	}

	PERFINFO_AUTO_STOP();
}

__forceinline static float rdrGetPointShadowDepthBias(float depthBias)
{
	// for some reason doing a depth resolve on the main surface with MSAA/CSAA shifts stuff a 
	// little so we always need a bit larger bias
#if !PLATFORM_CONSOLE
	if (rdrNvidiaCSAAEnabled() || gfx_state.settings.minDepthBiasFix)
	{
		MAX1F(depthBias, csaaMinDepthBiasPoint);
	}
	/*
	// DJR - If at some point we find ATI requires a specific depth bias for point light 
	// shadow maps, enable this code and tune atiMinDepthBiasPoint appropriately.
	else
	if (system_specs.videoCardVendorID == VENDOR_ATI && gfx_state.msaa > 1)
		// ATI cards with MSAA on need larger bias
		MAX1F(depthBias, atiMinDepthBiasPoint);
	*/
#endif
	return depthBias;
}

__forceinline static float rdrGetSpotShadowDepthBias(float depthBias)
{
	// for some reason doing a depth resolve on the main surface with MSAA/CSAA shifts stuff a 
	// little so we always need a bit larger bias
#if !PLATFORM_CONSOLE
	if (rdrNvidiaCSAAEnabled() || gfx_state.settings.minDepthBiasFix)
	{
		MAX1F(depthBias, csaaMinDepthBiasSpot);
	}
	else
	if (system_specs.videoCardVendorID == VENDOR_ATI && gfx_state.antialiasingQuality > 1)
		// ATI cards with MSAA on need larger bias
		MAX1F(depthBias, atiMinDepthBiasSpot);
#endif
	return depthBias;
}

void gfxShadowPointLightDraw(GfxLight *point_light, const Frustum *camera_frustum, RdrDrawList *draw_list)
{
	GfxLightShadowsPerAction *shadow_data = getShadowData(point_light);
	int map_index, start_draw_count=0, start_tri_count=0;
	F32 light_dist;
	bool do_update;

	if (!shadow_data)
		return;

	updateCubemapLookupSurface();

	do_update = !gfx_state.debug.no_render_shadowmaps && shadow_data->shadow_surface && shadow_data->shadowmap_size;

	PERFINFO_AUTO_START_FUNC();
	gfxBeginSection("Point shadowmaps");

	if (do_update)
	{
		// draw into shadowmap
		gfxSetActiveSurfaceEx(shadow_data->shadow_surface->surface, MASK_SBUF_DEPTH, 0);
		rdrSurfaceUpdateMatricesFromFrustum(shadow_data->shadow_surface->surface, unitmat44, unitmat, camera_frustum, 0, 1, 0, 1, camera_frustum->cammat[3]);
		rdrClearActiveSurface(gfx_state.currentDevice->rdr_device, MASK_SBUF_DEPTH, unitvec4, 1);
	}

	if (shadow_data->shadow_surface)
		shadow_data->shadow_tex_handle = rdrSurfaceToTexHandleEx(shadow_data->shadow_surface->surface, SBUF_DEPTH, 0, 0, false);
	else
		shadow_data->shadow_tex_handle = 0;

#if _PS3
	rdrAddRemoveTexHandleFlags(&shadow_data->shadow_tex_handle, RTF_CLAMP_U|RTF_CLAMP_V, RTF_MAG_POINT|RTF_MIN_POINT);
#else
	if (system_specs.videoCardVendorID == VENDOR_NV)
	{
		// this is the nvidia specific way to get PCF filtering
		rdrAddRemoveTexHandleFlags(&shadow_data->shadow_tex_handle, 0, RTF_MAG_POINT|RTF_MIN_POINT);
	}
#endif

	if (do_update)
		rdrDrawListGetForegroundStats(draw_list, NULL, NULL, NULL, NULL, &start_draw_count, &start_tri_count, NULL, NULL);

	light_dist = distance3(point_light->rdr_light.world_mat[3], camera_frustum->cammat[3]) - point_light->rdr_light.point_spot_params.outer_radius;
	MAX1(light_dist, 0);

	assert(shadow_data->shadowmap_count >= 0 && shadow_data->shadowmap_count <= ARRAY_SIZE(shadow_data->shadowmaps));
	assert(shadow_data->shadowmap_count <= 6);
	for (map_index = 0; map_index < shadow_data->shadowmap_count; ++map_index)
	{
		GfxShadowMap *smap = shadow_data->shadowmaps[map_index];

		if (!smap)
			continue;

		if (shadow_data->shadow_surface)
		{
			rdrSurfaceUpdateMatricesFromFrustum(shadow_data->shadow_surface->surface, smap->render_projection_matrix, unitmat, &smap->light_frustum,
				point_light_viewport[map_index][0] * shadow_data->used_surface_size_ratio[0], // viewport x offset
				point_light_viewport[map_index][1] * shadow_data->used_surface_size_ratio[0], // viewport width
				point_light_viewport[map_index][2] * shadow_data->used_surface_size_ratio[1], // viewport y offset
				point_light_viewport[map_index][3] * shadow_data->used_surface_size_ratio[1], // viewport height
				camera_frustum->cammat[3]);
		}

		updateRdrLight(point_light, camera_frustum, map_index);

		if (do_update && smap->shadow_pass >= 0)
		{
			float depthBias = POINT_SHADOW_MAP_DEPTH_BIAS;
			depthBias = rdrGetPointShadowDepthBias(depthBias);
			rdrDrawListDrawShadowPassObjects(gfx_state.currentDevice->rdr_device, draw_list, smap->shadow_pass, depthBias, SLOPE_SCALE_DEPTH_BIAS);
		}
	}

	if (do_update)
		updateRdrStats(point_light, draw_list, start_draw_count, start_tri_count);

	if (do_update && gfx_state.debug.shadow_debug)
		gfxDebugThumbnailsAddSurface(shadow_data->shadow_surface->surface, SBUF_DEPTH, 0, "Point Light Shadowmap", 0);

	gfxEndSection();
	PERFINFO_AUTO_STOP();
}

//////////////////////////////////////////////////////////////////////////
// spot light
void gfxShadowSpotLight(GfxLight *spot_light, const Frustum *camera_frustum, RdrDrawList *draw_list)
{
	GfxLightShadowsPerAction *shadow_data = getShadowData(spot_light);
	GfxShadowMap *smap;
	F32 angle_deg, near_plane;
	int i;

	PERFINFO_AUTO_START_FUNC();

	if (shadow_data->shadowmap_count > 0)
	{
		smap = getShadowmapForLight(spot_light, 0, false);
		if (smap)
			smap->shadow_pass = -1;
	}

	if (gfx_state.debug.no_render_shadowmaps || !shadow_data->shadowmap_size)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	spot_light->light_colors.min_shadow_val = 1.0f - spot_light->shadow_transition;
	for (i = 0; i < RLCT_COUNT; ++i)
		spot_light->rdr_light.light_colors[i].min_shadow_val = spot_light->light_colors.min_shadow_val;

	shadow_data->shadowmap_count = 1;
	assert(shadow_data->shadowmap_count >= 0 && shadow_data->shadowmap_count <= ARRAY_SIZE(shadow_data->shadowmaps));

	// build lightspace basis
	copyVec3(spot_light->rdr_light.world_mat[1], spot_light->shadow_data.light_vec);
	orientMat3(spot_light->shadow_data.lightspace_matrix, spot_light->shadow_data.light_vec);
	copyVec3(spot_light->rdr_light.world_mat[3], spot_light->shadow_data.lightspace_matrix[3]);

	smap = getShadowmapForLight(spot_light, 0, true);
	smap->depth_range = spot_light->rdr_light.point_spot_params.outer_radius;
	smap->near_fade = 0.0f;
	smap->far_fade = spot_light->rdr_light.point_spot_params.outer_radius;

	// setup frustum
	angle_deg = spot_light->rdr_light.point_spot_params.outer_cone_angle;
	angle_deg = 2 * DEG(angle_deg);
	frustumSetCameraMatrix(&smap->light_frustum, spot_light->shadow_data.lightspace_matrix);
	near_plane = CLAMP(spot_light->shadow_near_plane, SHADOWMAP_ZNEAR, smap->depth_range);
	frustumSet(&smap->light_frustum, angle_deg, 1.0f, near_plane, smap->depth_range);

	// get shadow casters
	smap->shadow_pass = rdrDrawListAddPass(draw_list, &smap->light_frustum, RDRSHDM_SHADOW, &spot_light->rdr_light, 0, -1, true);

	PERFINFO_AUTO_STOP();
}

void gfxShadowSpotLightCalcProjMatrix(GfxLight *spot_light, const Frustum *camera_frustum)
{
	GfxLightShadowsPerAction *shadow_data = getShadowData(spot_light);
	GfxShadowMap *smap;
	Mat44 yflip_mat;
	F32 angle_deg, near_plane;

	if (gfx_state.debug.no_render_shadowmaps || !shadow_data->shadowmap_size)
		return;

	PERFINFO_AUTO_START_FUNC();

	smap = shadow_data->shadowmaps[0];

	angle_deg = spot_light->rdr_light.point_spot_params.outer_cone_angle;
	angle_deg = 2 * DEG(angle_deg);
	near_plane = CLAMP(spot_light->shadow_near_plane, SHADOWMAP_ZNEAR, smap->depth_range);
	rdrSetupPerspectiveProjection(smap->render_projection_matrix, angle_deg, 1, near_plane, smap->depth_range);

	copyMat44(unitmat44, yflip_mat);
	yflip_mat[1][1] = -1;
	mulMat44Inline(yflip_mat, smap->render_projection_matrix, smap->receiver_projection_matrix);
	smap->use_unitviewrot = 0;
	smap->use_dxoffset = 1;
	smap->use_lightspace_transform = 1;
	smap->use_texel_offset_only = 0;
	smap->use_shadowmap_used_ratio_only = 0;

	PERFINFO_AUTO_STOP();
}

void gfxShadowSpotLightDraw(GfxLight *spot_light, const Frustum *camera_frustum, RdrDrawList *draw_list)
{
	GfxLightShadowsPerAction *shadow_data = getShadowData(spot_light);
	int start_draw_count=0, start_tri_count=0;
	GfxShadowMap *smap;
	F32 light_dist;
	bool do_update;

	if (!shadow_data)
		return;

	do_update = !gfx_state.debug.no_render_shadowmaps && shadow_data->shadow_surface && shadow_data->shadowmap_size;

	PERFINFO_AUTO_START_FUNC();
	gfxBeginSection("Spot/Projector shadowmap");

	if (do_update)
	{
		// draw into shadowmap
		gfxSetActiveSurfaceEx(shadow_data->shadow_surface->surface, MASK_SBUF_DEPTH, 0);
		rdrSurfaceUpdateMatricesFromFrustum(shadow_data->shadow_surface->surface, unitmat44, unitmat, camera_frustum, 0, 1, 0, 1, camera_frustum->cammat[3]);
		rdrClearActiveSurface(gfx_state.currentDevice->rdr_device, MASK_SBUF_DEPTH, unitvec4, 1);
	}

	if (shadow_data->shadow_surface)
		shadow_data->shadow_tex_handle = rdrSurfaceToTexHandleEx(shadow_data->shadow_surface->surface, SBUF_DEPTH, 0, 0, false);
	else
		shadow_data->shadow_tex_handle = 0;

#if _PS3
	rdrAddRemoveTexHandleFlags(&shadow_data->shadow_tex_handle, RTF_CLAMP_U|RTF_CLAMP_V, RTF_MAG_POINT|RTF_MIN_POINT);
#else
	if (system_specs.videoCardVendorID == VENDOR_NV)
	{
		// this is the nvidia specific way to get PCF filtering
		rdrAddRemoveTexHandleFlags(&shadow_data->shadow_tex_handle, 0, RTF_MAG_POINT|RTF_MIN_POINT);
	}
#endif

	if (do_update)
		rdrDrawListGetForegroundStats(draw_list, NULL, NULL, NULL, NULL, &start_draw_count, &start_tri_count, NULL, NULL);

	smap = shadow_data->shadowmaps[0];
	if (smap)
	{
		if (shadow_data->shadow_surface)
		{
			rdrSurfaceUpdateMatricesFromFrustum(shadow_data->shadow_surface->surface, smap->render_projection_matrix, unitmat, &smap->light_frustum,
				0, // viewport x offset
				shadow_data->used_surface_size_ratio[0], // viewport width
				0, // viewport y offset
				shadow_data->used_surface_size_ratio[1], // viewport height
				camera_frustum->cammat[3]);
		}

		updateRdrLight(spot_light, camera_frustum, 0);

		light_dist = distance3(spot_light->rdr_light.world_mat[3], camera_frustum->cammat[3]) - spot_light->rdr_light.point_spot_params.outer_radius;
		MAX1(light_dist, 0);

		if (do_update && smap->shadow_pass >= 0)
		{
			float depthBias = SPOT_SHADOW_MAP_DEPTH_BIAS;
			depthBias = rdrGetSpotShadowDepthBias(depthBias);
			rdrDrawListDrawShadowPassObjects(gfx_state.currentDevice->rdr_device, draw_list, smap->shadow_pass, depthBias, SLOPE_SCALE_DEPTH_BIAS);
		}
	}

	if (do_update)
		updateRdrStats(spot_light, draw_list, start_draw_count, start_tri_count);

	if (do_update && gfx_state.debug.shadow_debug)
		gfxDebugThumbnailsAddSurface(shadow_data->shadow_surface->surface, SBUF_DEPTH, 0, "Spot/Projector Light Shadowmap", 0);

	gfxEndSection();
	PERFINFO_AUTO_STOP();
}

//////////////////////////////////////////////////////////////////////////
// directional light
// Sets the shadowmap texture size for PSSM shadows.  Must be a power of 2.
AUTO_COMMAND ACMD_COMMANDLINE;
void pssmForceShadowmapSize(int shadowmap_size)
{
	int old_shadowmap_size = gfx_state.pssm_shadowmap_size_override ? gfx_state.pssm_shadowmap_size_override : gfx_state.pssm_shadowmap_size;
	shadowmap_size = pow2(shadowmap_size);
	MAX1(shadowmap_size, 64);
	MIN1(shadowmap_size, 2048);

	gfx_state.pssm_shadowmap_size_override = shadowmap_size;

	if (old_shadowmap_size != shadowmap_size)
	{
		U32 size[2] = {old_shadowmap_size*3, old_shadowmap_size*2};
		gfxFreeSpecificSizedTempSurfaces(gfx_state.currentDevice, size, false);
		size[0] = size[1] = old_shadowmap_size*2;
		gfxFreeSpecificSizedTempSurfaces(gfx_state.currentDevice, size, false);
	}
}

#define ANGLE_DIFF_THRESHOLD				(0.1f)
#define Y_DIFF_THRESHOLD					(0.05f)

static void doLightDirThresholding(GfxLight *dir_light)
{
	Vec3 new_light_vec;
	F32 angle_diff, angle_old, angle_new, y_diff;

	// only change the light angle if it has to move more than a certain threshold to reduce angle-change induced shadow edge swimming
	scaleVec3(dir_light->rdr_light.world_mat[1], -1, new_light_vec);
	angle_old = acosf(CLAMPF32(dotVec3(upvec, dir_light->shadow_data.light_vec), -1, 1));
	angle_new = acosf(CLAMPF32(dotVec3(upvec, new_light_vec), -1, 1));
	angle_diff = max(subAngle(angle_old, angle_new), acosf(CLAMPF32(dotVec3(dir_light->shadow_data.light_vec, new_light_vec), -1, 1)));
	y_diff = dir_light->shadow_data.light_vec[1] - new_light_vec[1];

	//		gfxXYprintf(5, 24, "AngleDiff:     %f", ABS(angle_diff));
	//		gfxXYprintf(5, 25, "YDiff:         %f", ABS(y_diff));

	if (disable_dir_shadow_threshold || sameVec3(dir_light->shadow_data.light_vec, zerovec3))
	{
		copyVec3(new_light_vec, dir_light->shadow_data.light_vec);
	}
	else
	{
		if (ABS(angle_diff) > ANGLE_DIFF_THRESHOLD || ABS(y_diff) > Y_DIFF_THRESHOLD)
		{
			F32 max_fade = 1 + 0.5f * dir_light->shadow_fade_dark_time * dir_light->shadow_fade_rate;

			dir_light->shadow_fade += gfx_state.frame_time * dir_light->shadow_fade_rate;
			MIN1(dir_light->shadow_fade, max_fade);

			if (dir_light->shadow_fade >= max_fade)
				copyVec3(new_light_vec, dir_light->shadow_data.light_vec);
		}
		else if (dir_light->shadow_fade)
		{
			dir_light->shadow_fade -= gfx_state.frame_time * dir_light->shadow_fade_rate;
			MAX1(dir_light->shadow_fade, 0);
		}
	}
}

static F32 quantScale(F32 scale)
{
	// quantize to 0.1 increments
	return round(scale * 10.f) * 0.1f;
}

void gfxGetPSSMSettingsFromSkyOrRegion(const ShadowRules* sky_shadow_rules, WorldRegionType region_type, PSSMSettings *settings)
{
#if PSSM_SPLIT_COUNT > 3
	bool sky_has_shadow_rules = ((sky_shadow_rules->four_split_distances[0] > FLT_EPSILON) || (sky_shadow_rules->four_split_distances[1] > FLT_EPSILON) || (sky_shadow_rules->four_split_distances[2] > FLT_EPSILON) || (sky_shadow_rules->four_split_distances[3] > FLT_EPSILON));
#else
	bool sky_has_shadow_rules = ((sky_shadow_rules->three_split_distances[0] > FLT_EPSILON) || (sky_shadow_rules->three_split_distances[1] > FLT_EPSILON) || (sky_shadow_rules->three_split_distances[2] > FLT_EPSILON));
#endif
	GfxCameraController *camera = gfxGetActiveCameraController();
	const ShadowRules *shadow_rules;
	F32 near_dist_scale, near_dist_scale2;

	if (sky_has_shadow_rules)
	{
		shadow_rules = sky_shadow_rules;
	}
	else
	{
		const WorldRegionRules *region_rules = worldRegionGetRulesByType(region_type);
		shadow_rules = worldRegionRulesGetShadowRules(region_rules);
	}

	near_dist_scale = (!camera->do_shadow_scaling || !shadow_rules->camera_falloff_dist || camera->last_camdist > shadow_rules->camera_falloff_dist) ? 1 : 0.5f;
	near_dist_scale2 = shadow_rules->camera_scale_falloff ? sqrtf(near_dist_scale) : near_dist_scale;

	settings->near_distances[0] = 0.5f;
#if PSSM_SPLIT_COUNT > 3
	settings->near_distances[1] = settings->far_distances[0] = shadow_rules->four_split_distances[0] * near_dist_scale;
	settings->near_distances[2] = settings->far_distances[1] = shadow_rules->four_split_distances[1] * near_dist_scale2;
	settings->near_distances[3] = settings->far_distances[2] = shadow_rules->four_split_distances[2];
	settings->far_distances[3] = shadow_rules->four_split_distances[3];
#else
	settings->near_distances[1] = settings->far_distances[0] = shadow_rules->three_split_distances[0] * near_dist_scale;
	settings->near_distances[2] = settings->far_distances[1] = shadow_rules->three_split_distances[1] * near_dist_scale2;
	settings->far_distances[2] = shadow_rules->three_split_distances[2];
#endif
}

static void calculateSplits(GfxLight *dir_light, const Frustum *camera_frustum, const PSSMSettings *settings)
{
	GfxLightShadowsPerAction *shadow_data = getShadowData(dir_light);
	int i;

	for (i = 0; i < shadow_data->shadowmap_count; ++i)
	{
		GfxShadowMap *smap = getShadowmapForLight(dir_light, i, true);
		Vec3 vPosA,vPosB,vPosC;

		frustumGetSliceAABB(camera_frustum, smap->light_frustum.viewmat ,settings->near_distances[i], settings->far_distances[i], smap->vReceiverMin, smap->vReceiverMax);

		// My gut tells me this calculation can be improved by taking into account the light direction and the camera roll (which is almost always 0)
		// of course, if we ever allowed camera roll, it would introduce artifacts. [RMARR - 4/12/12]
		vPosA[0] = -camera_frustum->htan*settings->far_distances[i];
		vPosA[1] = -camera_frustum->vtan*settings->far_distances[i];
		vPosA[2] = settings->far_distances[i];

		vPosB[0] = camera_frustum->htan*settings->near_distances[i];
		vPosB[1] = camera_frustum->vtan*settings->near_distances[i];
		vPosB[2] = settings->near_distances[i];
		
		vPosC[0] = camera_frustum->htan*settings->far_distances[i];
		vPosC[1] = camera_frustum->vtan*settings->far_distances[i];
		vPosC[2] = settings->far_distances[i];
		smap->fShadowMapRes = MAX(distance3(vPosA,vPosB),distance3(vPosA,vPosC));
	}
}

void gfxShadowDirectionalLight(GfxLight *dir_light, const Frustum *camera_frustum, RdrDrawList *draw_list)
{
	GfxGlobalDrawParams *gdraw = SAFE_MEMBER_ADDR(gfx_state.currentAction, gdraw);
	GfxLightShadowsPerAction *shadow_data = getShadowData(dir_light);
	F32 regiondiam;
	int map_index;
	F32 fScale = 1.0f;//1.19f;

	assert(shadow_data->shadowmap_count >= 0 && shadow_data->shadowmap_count <= ARRAY_SIZE(shadow_data->shadowmaps));
	for (map_index = 0; map_index < shadow_data->shadowmap_count; ++map_index)
	{
		GfxShadowMap *smap = getShadowmapForLight(dir_light, map_index, false);
		if (smap)
			smap->shadow_pass = -1;
	}

	if (gfx_state.debug.no_render_shadowmaps || !gdraw || !shadow_data->shadowmap_size)
		return;

	PERFINFO_AUTO_START_FUNC();

	if (gdraw->region_min[0] < gdraw->region_max[0] && gdraw->region_min[1] < gdraw->region_max[1] && gdraw->region_min[2] < gdraw->region_max[2])
		regiondiam = distance3(gdraw->region_min, gdraw->region_max) + 20;
	else
		regiondiam = 5000;

	shadow_data->shadowmap_count = PSSM_SPLIT_COUNT;
	assert(shadow_data->shadowmap_count >= 0 && shadow_data->shadowmap_count <= ARRAY_SIZE(shadow_data->shadowmaps));

	doLightDirThresholding(dir_light);
	orientMat3(dir_light->shadow_data.lightspace_matrix, dir_light->shadow_data.light_vec);
	zeroVec3(dir_light->shadow_data.lightspace_matrix[3]);

	calculateSplits(dir_light, camera_frustum, &gdraw->pssm_settings);

	assert(shadow_data->shadowmap_count >= 0 && shadow_data->shadowmap_count <= ARRAY_SIZE(shadow_data->shadowmaps));
	for (map_index = 0; map_index < shadow_data->shadowmap_count; ++map_index)
	{
		GfxShadowMap *smap = shadow_data->shadowmaps[map_index];
		Vec3 extrude;

		// near and far planes for current frustum split
		F32 fNear = gdraw->pssm_settings.near_distances[map_index];
		F32 fFar = gdraw->pssm_settings.far_distances[map_index];

		if (debug_shadow_hull_creation)
		{
			if (smap->hull_debug[2].object_count > 10 && smap->hull_debug[1].object_count <= 1)
			{
				GPolySet temp_polyset={0};
				// one frame was bad, break and try to recreate the bad condition
				_DbgBreak();
				gpsetMakeFrustum(&temp_polyset, smap->hull_debug[1].camera_matrix, smap->hull_debug[1].fovx, smap->hull_debug[1].fovy, smap->hull_debug[1].fNear, smap->hull_debug[1].fFar, smap->hull_debug[1].fScale);
				if (smap->hull_debug[1].region_min[0] < smap->hull_debug[1].region_max[0] && smap->hull_debug[1].region_min[1] < smap->hull_debug[1].region_max[1] && smap->hull_debug[1].region_min[2] < smap->hull_debug[1].region_max[2])
					gpsetClipBox(&temp_polyset, smap->hull_debug[1].region_min, smap->hull_debug[1].region_max);
				gpsetExtrudeConvexHull(&shadow_debug_polyset, &temp_polyset, smap->hull_debug[1].extrude);
				if (smap->hull_debug[1].region_min[0] < smap->hull_debug[1].region_max[0] && smap->hull_debug[1].region_min[1] < smap->hull_debug[1].region_max[1] && smap->hull_debug[1].region_min[2] < smap->hull_debug[1].region_max[2])
					gpsetClipBox(&shadow_debug_polyset, smap->hull_debug[1].region_min, smap->hull_debug[1].region_max);
				gpsetToConvexHullTransformed(&shadow_debug_polyset, &smap->light_frustum.hull, 1, smap->hull_debug[1].light_view_matrix);
				gfx_state.debug.polyset = &shadow_debug_polyset;
				gpsetFreeData(&temp_polyset);
			}

			CopyStructs(&smap->hull_debug[2], &smap->hull_debug[1], 1);
			CopyStructs(&smap->hull_debug[1], &smap->hull_debug[0], 1);
			copyVec3(gdraw->region_min, smap->hull_debug[0].region_min);
			copyVec3(gdraw->region_max, smap->hull_debug[0].region_max);
			copyMat4(camera_frustum->cammat, smap->hull_debug[0].camera_matrix);
			smap->hull_debug[0].fovx = camera_frustum->fovx;
			smap->hull_debug[0].fovy = camera_frustum->fovy;
			smap->hull_debug[0].fNear = fNear;
			smap->hull_debug[0].fFar = fFar;
			smap->hull_debug[0].fScale = fScale;
		}

		smap->near_fade = fFar * 0.9f;
		smap->far_fade = fFar;

		// make frustum hull, clip to scene boundary
		if (map_index > 0 && gdraw->occlusion_buffer && zoIsReadyForRead(gdraw->occlusion_buffer))
		{
			// see if we can do better
			Vec2 vMin,vMax;
			Vec3 vTemp;
			Vec3 avPoints[8];
			F32 fLeft,fRight,fTop,fBot;
			F32 tanhalffovx = tanf(camera_frustum->fovx * PI / 360.0);
			F32 tanhalffovy = tanf(camera_frustum->fovy * PI / 360.0);
			zoLimitsScreenRect(gdraw->occlusion_buffer,fNear,vMin,vMax);
			if (vMin[0] > vMax[0])
			{
				//nothing to see here
				smap->shadow_pass = -1;
				continue;
			}

			fLeft = vMin[0]*fNear*tanhalffovx;
			fRight = vMax[0]*fNear*tanhalffovx;
			fBot = vMin[1]*fNear*tanhalffovy;
			fTop = vMax[1]*fNear*tanhalffovy;

			setVec3(vTemp, -fRight, fTop, -fNear);
			mulVecMat4(vTemp, camera_frustum->cammat, avPoints[0]);

			setVec3(vTemp, -fLeft, fTop, -fNear);
			mulVecMat4(vTemp, camera_frustum->cammat, avPoints[1]);

			setVec3(vTemp, -fLeft, fBot, -fNear);
			mulVecMat4(vTemp, camera_frustum->cammat, avPoints[2]);

			setVec3(vTemp, -fRight, fBot, -fNear);
			mulVecMat4(vTemp, camera_frustum->cammat, avPoints[3]);

			fLeft = vMin[0]*fFar*tanhalffovx;
			fRight = vMax[0]*fFar*tanhalffovx;
			fBot = vMin[1]*fFar*tanhalffovy;
			fTop = vMax[1]*fFar*tanhalffovy;

			setVec3(vTemp, -fRight, fTop, -fFar);
			mulVecMat4(vTemp, camera_frustum->cammat, avPoints[4]);

			setVec3(vTemp, -fLeft, fTop, -fFar);
			mulVecMat4(vTemp, camera_frustum->cammat, avPoints[5]);

			setVec3(vTemp, -fLeft, fBot, -fFar);
			mulVecMat4(vTemp, camera_frustum->cammat, avPoints[6]);

			setVec3(vTemp, -fRight, fBot, -fFar);
			mulVecMat4(vTemp, camera_frustum->cammat, avPoints[7]);

			gpsetMakeFrustumFromPoints(&smap->shadowreceiver_hull,avPoints);
		}
		else
		{
			gpsetMakeFrustum(&smap->shadowreceiver_hull, camera_frustum->cammat, camera_frustum->fovx, camera_frustum->fovy, fNear, fFar, fScale);
		}

		// set surface and frustum view matrices
		frustumSetCameraMatrix(&smap->light_frustum, dir_light->shadow_data.lightspace_matrix);

		gpsetCopy(&smap->tempshadowcaster_hull, &smap->shadowreceiver_hull);
		if (gdraw->region_min[0] < gdraw->region_max[0] && gdraw->region_min[1] < gdraw->region_max[1] && gdraw->region_min[2] < gdraw->region_max[2])
			gpsetClipBox(&smap->tempshadowcaster_hull, gdraw->region_min, gdraw->region_max);

		// extend points towards light
		scaleVec3(dir_light->shadow_data.light_vec, regiondiam, extrude);
		gpsetExtrudeConvexHull(&smap->shadowcaster_hull, &smap->tempshadowcaster_hull, extrude);
		if (gdraw->region_min[0] < gdraw->region_max[0] && gdraw->region_min[1] < gdraw->region_max[1] && gdraw->region_min[2] < gdraw->region_max[2])
			gpsetClipBox(&smap->shadowcaster_hull, gdraw->region_min, gdraw->region_max);

		// make convex hull in light view space
		gpsetToConvexHullTransformed(&smap->shadowcaster_hull, &smap->light_frustum.hull, 1, smap->light_frustum.viewmat);
		smap->light_frustum.use_hull = 1;

		if (debug_shadow_hull_creation)
		{
			copyVec3(extrude, smap->hull_debug[0].extrude);
			copyMat4(smap->light_frustum.viewmat, smap->hull_debug[0].light_view_matrix);
		}

		// get shadow casters
		smap->shadow_pass = rdrDrawListAddPass(draw_list, &smap->light_frustum, RDRSHDM_SHADOW, &dir_light->rdr_light, map_index, -1, true);

		if (dbg_shadow_hull==map_index + 1) // debug
		{
			gpsetCopy(&shadow_debug_polyset, &smap->shadowcaster_hull);
			gfx_state.debug.polyset = &shadow_debug_polyset;
			dbg_shadow_hull = 0;
		}
	}

	PERFINFO_AUTO_STOP();
}

void gfxShadowDirectionalLightCalcProjMatrix(GfxLight *dir_light, const Frustum *camera_frustum)
{
	GfxLightShadowsPerAction *shadow_data = getShadowData(dir_light);
	int map_index;

	if (gfx_state.debug.no_render_shadowmaps || !shadow_data->shadowmap_size)
		return;

	PERFINFO_AUTO_START_FUNC();

	assert(shadow_data->shadowmap_count >= 0 && shadow_data->shadowmap_count <= ARRAY_SIZE(shadow_data->shadowmaps));
	for (map_index = 0; map_index < shadow_data->shadowmap_count; ++map_index)
	{
		GfxShadowMap *smap = shadow_data->shadowmaps[map_index];
		Vec3 ls_castmin, ls_castmax, ls_recvmin, ls_recvmax;
		F32 shadowmap_resolution;
		F32 fLightZMin,fLightZMax;
		Vec2 texel_size;

		shadowmap_resolution = smap->fShadowMapRes;

		// get the Z-extents in light frustum space of the shadowreceiver hull
		gpsetTransformToBounds(&smap->shadowreceiver_hull, smap->light_frustum.viewmat, ls_recvmin, ls_recvmax);
		fLightZMin = ls_recvmin[2];
		fLightZMax = ls_recvmax[2];

		// use the already calculated receiver min and max (basically, should be a slice of the shadow hull)
		copyVec3(smap->vReceiverMin,ls_recvmin);
		copyVec3(smap->vReceiverMax,ls_recvmax);

		ls_recvmin[2] = fLightZMin;
		ls_recvmax[2] = fLightZMax;

		// prevent swimming by fixing offset to texel boundaries and using fixed resolution shadowmaps
		// we are losing a lot of resolution doing this
		setVec2same(texel_size, shadowmap_resolution / shadow_data->shadowmap_size);
		ls_recvmin[0] = floor(ls_recvmin[0] / AVOID_DIV_0(texel_size[0])) * texel_size[0];
		ls_recvmin[1] = floor(ls_recvmin[1] / AVOID_DIV_0(texel_size[1])) * texel_size[1];
		ls_recvmax[0] = ls_recvmin[0] + shadowmap_resolution + texel_size[0];
		ls_recvmax[1] = ls_recvmin[1] + shadowmap_resolution + texel_size[1];

		// clip the shadowcaster hull to the light frustum bounds
		if (smap->light_frustum.world_min[0] < smap->light_frustum.world_max[0] && smap->light_frustum.world_min[1] < smap->light_frustum.world_max[1] && smap->light_frustum.world_min[2] < smap->light_frustum.world_max[2])
			gpsetClipBox(&smap->shadowcaster_hull, smap->light_frustum.world_min, smap->light_frustum.world_max);

		// transform the shadowcaster hull into light space.  Do this only to obtain the maximum Z-value
		gpsetTransformToBounds(&smap->shadowcaster_hull, smap->light_frustum.viewmat, ls_castmin, ls_castmax);
		MAX1(ls_recvmax[2], ls_castmax[2] + 1); // use the near distance of the casters (plus a small offset) if it is closer
		if (!dir_light->infinite_shadows)
		{
			Vec3 ls_origin;
			mulVecMat4(dir_light->rdr_light.world_mat[3], smap->light_frustum.viewmat, ls_origin);
			MIN1(ls_recvmax[2], ls_origin[2]);
		}
		smap->depth_range = ls_recvmax[2] - ls_recvmin[2];

		// construct a matrix that makes all the receiver volume fit in [-1..1]
		createScaleTranslateFitMat(smap->render_projection_matrix, ls_recvmin, ls_recvmax, -1, 1, -1, 1, 1, 0);
		// construct another matrix that makes all the receiver volume fit in a single texture map, possibly flipped.
		createScaleTranslateFitMat(smap->receiver_projection_matrix, ls_recvmin, ls_recvmax, 
			(map_index&1)?0:-1, (map_index&1)?1:0, 
			(map_index&2)?1:0, (map_index&2)?0:-1, // flip the y axis
			1, 0);
		smap->use_unitviewrot = 0;
		smap->use_dxoffset = 1;
		smap->use_lightspace_transform = 1;
		smap->use_texel_offset_only = 0;
		smap->use_shadowmap_used_ratio_only = 0;

		if (dbg_shadow_hull==map_index + 1) // debug
		{
			gpsetCopy(&shadow_debug_polyset, &smap->shadowcaster_hull);
			gfx_state.debug.polyset = &shadow_debug_polyset;
			dbg_shadow_hull = 0;
		}
	}

	PERFINFO_AUTO_STOP();
}

void gfxShadowDirectionalLightDraw(GfxLight *dir_light, const Frustum *camera_frustum, RdrDrawList *draw_list)
{
	GfxLightShadowsPerAction *shadow_data = getShadowData(dir_light);
	int map_index, start_draw_count=0, start_tri_count=0, last_draw_count;
	bool do_update;

	if (!shadow_data)
		return;

	do_update = !gfx_state.debug.no_render_shadowmaps && shadow_data->shadow_surface && shadow_data->shadowmap_size;

	PERFINFO_AUTO_START_FUNC();
	gfxBeginSection("Dir shadowmaps");

	if (do_update)
	{
		// draw into shadowmap
		gfxSetActiveSurfaceEx(shadow_data->shadow_surface->surface, MASK_SBUF_DEPTH, 0);
		rdrSurfaceUpdateMatricesFromFrustum(shadow_data->shadow_surface->surface, unitmat44, unitmat, camera_frustum, 0, 1, 0, 1, camera_frustum->cammat[3]);
		rdrClearActiveSurface(gfx_state.currentDevice->rdr_device, MASK_SBUF_DEPTH, unitvec4, 1);
	}

	if (shadow_data->shadow_surface)
		shadow_data->shadow_tex_handle = rdrSurfaceToTexHandleEx(shadow_data->shadow_surface->surface, SBUF_DEPTH, 0, 0, false);
	else
		shadow_data->shadow_tex_handle = 0;

#if _PS3
	rdrAddRemoveTexHandleFlags(&shadow_data->shadow_tex_handle, RTF_CLAMP_U|RTF_CLAMP_V, RTF_MAG_POINT|RTF_MIN_POINT);
#else
	if (system_specs.videoCardVendorID == VENDOR_NV)
	{
		// this is the nvidia specific way to get PCF filtering
		rdrAddRemoveTexHandleFlags(&shadow_data->shadow_tex_handle, 0, RTF_MAG_POINT|RTF_MIN_POINT);
	}
#endif

	if (do_update)
		rdrDrawListGetForegroundStats(draw_list, NULL, NULL, NULL, NULL, &start_draw_count, &start_tri_count, NULL, NULL);

	last_draw_count = start_draw_count;

	assert(shadow_data->shadowmap_count >= 0 && shadow_data->shadowmap_count <= ARRAY_SIZE(shadow_data->shadowmaps));
	assert(shadow_data->shadowmap_count <= 4); // if it goes higher the mapping to the shadow surface will have to change
	for (map_index = 0; map_index < shadow_data->shadowmap_count; ++map_index)
	{
		GfxShadowMap *smap = shadow_data->shadowmaps[map_index];

		if (!smap)
			continue;

		if (shadow_data->shadow_surface)
		{
			rdrSurfaceUpdateMatricesFromFrustum(shadow_data->shadow_surface->surface, smap->render_projection_matrix, unitmat, &smap->light_frustum,
				((map_index&1)?0.5f:0) * shadow_data->used_surface_size_ratio[0], // viewport x offset
				0.5f * shadow_data->used_surface_size_ratio[0], // viewport width
				((map_index&2)?0.5f:0) * shadow_data->used_surface_size_ratio[1], // viewport y offset
				0.5f * shadow_data->used_surface_size_ratio[1], // viewport height
				camera_frustum->cammat[3]);
		}

		updateRdrLight(dir_light, camera_frustum, map_index);

		if (do_update && smap->shadow_pass >= 0)
		{
			rdrDrawListDrawShadowPassObjects(gfx_state.currentDevice->rdr_device, draw_list, smap->shadow_pass, (map_index==0?DIR_SHADOW_MAP_DEPTH_BIAS:DIR_SHADOW_MAP_DEPTH_BIAS2) / smap->depth_range, DIR_SHADOW_MAP_SLOPE_SCALE_DEPTH_BIAS);
			if (debug_shadow_hull_creation)
			{
				int end_draw_count;
				rdrDrawListGetForegroundStats(draw_list, NULL, NULL, NULL, NULL, &end_draw_count, NULL, NULL, NULL);
				smap->hull_debug[0].object_count = end_draw_count - last_draw_count;
				last_draw_count = end_draw_count;
			}
		}
	}

	if (do_update)
		updateRdrStats(dir_light, draw_list, start_draw_count, start_tri_count);

	if (do_update && gfx_state.debug.shadow_debug)
		gfxDebugThumbnailsAddSurface(shadow_data->shadow_surface->surface, SBUF_DEPTH, 0, "Directional Light Shadowmap", 0);

	gfxEndSection();
	PERFINFO_AUTO_STOP();
}

