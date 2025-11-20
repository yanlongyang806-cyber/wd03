/***************************************************************************



***************************************************************************/

#include "SparseGrid.h"
#include "EventTimingLog.h"
#include "qsortG.h"
#include "Octree.h"
#include "partition_enums.h"

#include "wlModelInline.h"
#include "wlAutoLOD.h"
#include "WorldCellClustering.h"
#include "dynWind.h"
#include "Materials.h"
#include "../StaticWorld/WorldCell.h"
#include "GfxWorld.h"
#include "GfxMaterials.h"
#include "GfxLightCache.h"
#include "GfxLights.h"
#include "GfxOcclusion.h"
#include "GfxTerrain.h"
#include "GfxPrimitive.h"
#include "GfxPrimitivePrivate.h"
#include "GfxDebug.h"
#include "GfxDynamics.h"
#include "GfxGeo.h"
#include "GfxSky.h"
#include "GfxShadows.h"
#include "GfxDrawFrame.h"
#include "GfxCommonSnap.h"
#include "GfxMapSnap.h"
#include "GfxTextures.h"
#include "GfxTexturesInline.h"
#include "bounds.h"
#include "WorldLib.h"
#include "hoglib.h"

// TODO DJR - for debug hammering of the RdrLight in the sun GfxLight
#include "GfxLightsPrivate.h"
#include "memlog.h"

#include "dynFxInterface.h"

#if _XBOX
#define memcpy memcpy_fast
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););


// TODO DJR standardize this code
#define WORLD_TRAVERSAL_STATS 1
#if WORLD_TRAVERSAL_STATS
#define INC_FRAME_COUNT(M_sCounter) ++gfx_state.debug.frame_counts. M_sCounter
#else
#define INC_FRAME_COUNT(M_sCounter)
#endif

// int debug_cluster_draw_only = 0;
// // 0 (default) means draw all world geo; 1 means draw only world cell cluster models, 2 means draw only world cell entries, not clusters
// AUTO_CMD_INT(debug_cluster_draw_only, debug_cluster_draw_only) ACMD_CMDLINEORPUBLIC;

int debug_cluster_show_clusters = 1;
AUTO_CMD_INT(debug_cluster_show_clusters, debug_cluster_show_clusters) ACMD_CMDLINEORPUBLIC;

int debug_cluster_show_nonclusters = 1;
AUTO_CMD_INT(debug_cluster_show_nonclusters, debug_cluster_show_nonclusters) ACMD_CMDLINEORPUBLIC;

// Enable or disable drawing of cluster objects or non-cluster objects. Pass in -1 to toggle.
AUTO_COMMAND ACMD_CATEGORY(Debug) ACMD_CATEGORY(Performance) ACMD_CMDLINE;
void debug_cluster_draw_only(int enabled)
{
	switch(enabled) {
		case -1:
			debug_cluster_show_nonclusters = !debug_cluster_show_nonclusters;
			debug_cluster_show_clusters = !debug_cluster_show_clusters;
			break;
		case 0:
			debug_cluster_show_nonclusters = 1;
			debug_cluster_show_clusters = 1;
			break;
		case 1:
			debug_cluster_show_clusters = 1;
			debug_cluster_show_nonclusters = 0;
			break;
		case 2:
			debug_cluster_show_clusters = 0;
			debug_cluster_show_nonclusters = 1;
			break;
		case 3:
			debug_cluster_show_clusters = 0;
			debug_cluster_show_nonclusters = 0;
			break;
	}
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(debug_cluster_draw_only);
void debug_cluster_draw_only_error(CmdContext *cmd)
{
	int outNum = 0;

	if(debug_cluster_show_clusters && !debug_cluster_show_nonclusters) {
		outNum = 1;
	} else if(debug_cluster_show_nonclusters && !debug_cluster_show_clusters) {
		outNum = 2;
	} else if(!debug_cluster_show_nonclusters && !debug_cluster_show_clusters) {
		outNum = 3;
	}

	estrPrintf(cmd->output_msg, "debug_cluster_draw_only %d", outNum);
}

int debug_cluster_wireframe = 0;
AUTO_CMD_INT(debug_cluster_wireframe, debug_cluster_wireframe) ACMD_CMDLINEORPUBLIC;

int gfxShowClustering = 0;
AUTO_CMD_INT(gfxShowClustering, gfxShowClustering);

void gfxWorldCellGraphicsFreeClusterTexSwaps(WorldCellGraphicsData *pWCGD)
{
	// every other texture is a dynamic texture.
	int j;
	for (j = eaSize(&pWCGD->cluster_tex_swaps) - 1; j >= 0; --j)
	{
		if (j & 1)
			texUnregisterDynamic(pWCGD->cluster_tex_swaps[j]->name);
		pWCGD->cluster_tex_swaps[j] = NULL;
	}
	eaDestroy(&pWCGD->cluster_tex_swaps);
}

AUTO_FIXUPFUNC;
TextParserResult fixupWorldCellGraphicsData(WorldCellGraphicsData *pWCGD, enumTextParserFixupType eFixupType, void *pExtraData)
{
	if (eFixupType == FIXUPTYPE_DESTRUCTOR)
	{
		gfxWorldCellGraphicsFreeClusterTexSwaps(pWCGD);
		if (pWCGD->light_cache)
		{
			gfxFreeStaticLightCache(pWCGD->light_cache);
			pWCGD->light_cache = NULL;
		}
	}

	return PARSERESULT_SUCCESS;
}

__forceinline static void fillLightInfo(
	RdrAddInstanceParams *params, F32 radius, int lod_index,
	const WorldDrawableEntry *draw, RdrLightParams *dest_light_params)
{
	WorldFXEntry *fx_parent;

	if (!draw->unlit && (fx_parent = GET_REF(draw->fx_parent_handle)))
		copyVec3(fx_parent->ambient_offset, params->ambient_offset);

	setVec3same(params->ambient_multiplier, 1);

	if (draw->light_cache) {

		if(dest_light_params) {

			// Copy over light params.
			*dest_light_params = *gfxStaticLightCacheGetLights(draw->light_cache, lod_index, NULL);
			params->light_params = dest_light_params;

			// Now strip out any unused point lights that might be in the light cache, but
			// not actually in range.
			gfxRemoveUnusedPointLightsFromLightParams(
				draw->base_entry.bounds.world_matrix,
				draw->base_entry.shared_bounds->local_min,
				draw->base_entry.shared_bounds->local_max,
				dest_light_params);


		} else {

			params->light_params = gfxStaticLightCacheGetLights(draw->light_cache, lod_index, NULL);
		}
	}
}

__forceinline static void setCommonInstanceParams(RdrAddInstanceParams *instance_params, WorldDrawableEntry *draw, int frustum_visible, F32 entry_screen_space)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	WorldCellEntryBounds *bounds = &draw->base_entry.bounds;
	WorldCellEntrySharedBounds *shared_bounds = draw->base_entry.shared_bounds;
	addVec3(bounds->world_mid, gdraw->pos_offset, instance_params->instance.world_mid);
	instance_params->zdist = rdrDrawListCalcZDist(gdraw->draw_list, instance_params->instance.world_mid);
	instance_params->distance_offset = shared_bounds->radius;
	instance_params->frustum_visible = frustum_visible;
	instance_params->screen_area = entry_screen_space;
	instance_params->wireframe = (gdraw->global_wireframe || !gdraw->in_editor) ? gdraw->global_wireframe : draw->wireframe;
	instance_params->instance.camera_facing = draw->camera_facing;
	instance_params->instance.axis_camera_facing = draw->axis_camera_facing;
	instance_params->force_no_shadow_receive = draw->no_shadow_receive;
	instance_params->uses_far_depth_range = gdraw->use_far_depth_range;
}

__forceinline static void setInstanceSelectionParams(RdrAddInstanceParams *instance_params, const WorldDrawableEntry *draw, const GfxGlobalDrawParams *gdraw)
{
	if (draw->wireframe >= 2 && gdraw->in_editor)
	{
		copyVec4(gdraw->selectedTintColor, instance_params->instance.color);
		if (draw->wireframe == 2)
			instance_params->wireframe = 0;
	}
}


__forceinline static F32 calcWindFade(F32 radius, F32 far_dist, F32 dist, F32 lod_scale)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	F32 wind_near, wind_far, wind_dist;
	
	far_dist *= lod_scale;

	wind_far = gdraw->wind_maxdistance_from_cam * lod_scale;
	MIN1F(wind_far, far_dist);

	wind_dist = (radius + 5) * lod_scale;
	
	wind_near = wind_far - wind_dist;
	MIN1F(wind_near, wind_far);
	MAX1F(wind_near, 0);

	return 1 - saturate((dist - wind_near) / wind_dist);
}

__forceinline static void modelInstanceWindHelper(F32 model_height, const Vec4 wind_params, RdrAddInstanceParams* params)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;

	params->wind_params[0] = gdraw->wind_current_client_loop_timer;
	params->wind_params[1] = wind_params[2]; //pivot offset
	params->wind_params[2] = model_height;
	params->wind_params[3] = 0;
}

int getMaterialDrawIndex(CONST_EARRAY_OF(MaterialDraw) material_draws)
{
	S32 idx=0;
	S32 draw_count = eaSize(&material_draws);
	S32 desired_quality = materialGetDesiredQuality();

	// Find the first one which is compatible with this video card (which, since pre-swapped, will have mis-matched preswapped data)
	while ((idx < (draw_count - 1)) && material_draws[idx]->material->incompatible)
		idx++;
	if (gConf.bEnableShaderQualitySlider == SHADER_QUALITY_SLIDER_LABEL)
	{
		while ((idx < (draw_count - 1)) && (material_draws[idx]->material->shader_quality > desired_quality)) {
			idx++;
			if (material_draws[idx]->material->incompatible)
			{
				idx--;
				break;
			}
		}
	}
	return idx;
}

static bool last_queued_as_alpha;

static void drawModelEntryHelper(WorldModelEntry *entry, F32 dist_sqr, Material* replacement_material, int frustum_visible, F32 entry_screen_space, RdrMaterialFlags add_material_flags, F32 fx_alpha, int bAuxPass)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	WorldDrawableEntry *draw = &entry->base_drawable_entry;
	WorldCellEntryBounds *bounds = &draw->base_entry.bounds;
	WorldCellEntrySharedBounds *shared_bounds = draw->base_entry.shared_bounds;
	bool did_lights = false, did_occluder_check = false;
	ModelToDraw models[NUM_MODELTODRAWS];
	RdrAddInstanceParams instance_params={0};
	RdrLightParams local_light_params={0};
	RdrInstancePerDrawableData per_drawable_data;
	int model_count;
	int i, j;
	Mat4 instance_matrix_copy;
	F32 fDist = sqrtf(dist_sqr);
	F32 fTexDist;
	F32 fUvDensity;
	WorldAnimationEntry *animation_controller;
	F32 wind_fade = 0, model_radius;

	PERFINFO_AUTO_START_FUNC_L2();

	last_queued_as_alpha = false;
	instance_params.aux_visual_pass = bAuxPass;
	instance_params.add_material_flags = add_material_flags;

	if (!draw->draw_list)
	{
		PERFINFO_AUTO_STOP_CHECKED_L2(__FUNCTION__);
		return;
	}

	model_count = gfxDemandLoadPreSwappedModel(draw->draw_list, models, ARRAY_SIZE(models), fDist, gdraw->lod_scale, -1, &entry->model_tracker, draw->world_fade_radius, draw->should_cluster);
	if (!model_count)
	{
		PERFINFO_AUTO_STOP_CHECKED_L2(__FUNCTION__);
		return;
	}

	model_radius = models[0].model->model_parent->radius;

	if (!entry->uid) // Move this to cell opening/entry creation?
	{
		static U32 uid_gen=0;
		entry->uid = ++uid_gen;
	}
	instance_params.instance.instance_uid = entry->uid;

	setCommonInstanceParams(&instance_params, draw, frustum_visible, entry_screen_space);
	copyVec3(draw->color, instance_params.instance.color);
	setInstanceSelectionParams(&instance_params, draw, gdraw);
	if (draw->is_camera_fading)
	{		
		int k;

		instance_params.distance_offset = 0;
		instance_params.needs_late_alpha_pass_if_need_grab = 1; // Force these alpha objects into the late bucket so they get sorted relative to FX
		instance_params.engine_fade = 1;

		// Find the original alpha, and only force the shadow if it was
		// something that was originally at full alpha.
		for(k = 0; k < eaSize(&g_ppClientOverridenWorldDrawableEntries); k++) {
			if(g_ppClientOverridenWorldDrawableEntries[k] && g_ppClientOverridenWorldDrawableEntries[k]->pDrawableEntry == &(entry->base_drawable_entry)) {
				if(g_ppClientOverridenWorldDrawableEntries[k]->fOriginalAlpha >= 1.0f) {
					instance_params.force_shadow_cast = true;
				}
				break;
			}
		}
	}

	if (frustum_visible & gdraw->visual_frustum_bit)
	{
		if (draw->no_vertex_lighting && draw->light_cache)
		{
			if (draw->light_cache->lod_vertex_light_data)
			{
				eaDestroyEx(&draw->light_cache->lod_vertex_light_data, gfxFreeVertexLight);
				draw->light_cache->lod_vertex_light_data = NULL;
			}
			draw->light_cache->base.light_params.vertex_light = NULL;
			draw->light_cache->need_vertex_light_update = 0;
		}
		fillLightInfo(&instance_params, model_radius, models[0].lod_index, draw, &local_light_params);
	}

	did_occluder_check = !draw->occluder;

	if (animation_controller = GET_REF(draw->animation_controller_handle))
	{
		if (animation_controller->last_update_timestamp != gfx_state.client_frame_timestamp)
			worldAnimationEntryUpdate(animation_controller, gfx_state.client_loop_timer, gfx_state.client_frame_timestamp);
		if (animation_controller->animation_properties.local_space)
		{
			mulMat4Inline(bounds->world_matrix, animation_controller->full_matrix, instance_params.instance.world_matrix);
		}
		else
		{
			if (!draw->controller_relative_matrix_inited)
				draw->controller_relative_matrix_inited = worldAnimationEntryInitChildRelativeMatrix(animation_controller, bounds->world_matrix, draw->controller_relative_matrix);
			mulMat4Inline(animation_controller->full_matrix, draw->controller_relative_matrix, instance_params.instance.world_matrix);
		}
	}
	else
	{
		memcpy_intrinsic(&instance_params.instance.world_matrix[0][0], &bounds->world_matrix[0][0], sizeof(Mat4));
	}

	copyMat4(instance_params.instance.world_matrix, instance_matrix_copy);
	if (entry->scaled)
	{
		scaleMat3Vec3(instance_params.instance.world_matrix, entry->model_scale);

		if(gfx_state.texLoadNearCamFocus) {

			F32 fCamDist;
			F32 fFocusDist;

			// Need distance to actual object midpoint and not LOD Controller
			fCamDist = distance3(bounds->world_mid, gdraw->cam_pos) - model_radius * entry->model_scale[0];
			fFocusDist = 2.0f * (distance3(instance_params.instance.world_mid, gfx_state.currentCameraFocus) - model_radius);
			fTexDist = MINF(fFocusDist, fCamDist);
		}
		{
			WorldFXEntry *fx_parent = GET_REF(entry->base_drawable_entry.fx_parent_handle);
			if (fx_parent && fx_parent->material) {
				// animated material. Assume the worst and that the uvs are animating
				fUvDensity = TEX_DEMAND_LOAD_DEFAULT_UV_DENSITY;
			} else {
                // approximate actual scale with a worst-case uniform scale
                F32 scale = MAXF(MAXF(fabsf(entry->model_scale[0]), fabsf(entry->model_scale[1])), fabsf(entry->model_scale[2]));

				fUvDensity = models[0].model->uv_density;
				if (scale > 0.0f) {
					fUvDensity -= log2f(scale);
				}
			}
		}
	} else {

		F32 fCamDist = distance3(bounds->world_mid, gdraw->cam_pos) - model_radius;

		if(gfx_state.texLoadNearCamFocus) {

			F32 fFocusDist = 2.0f * (distance3(instance_params.instance.world_mid, gfx_state.currentCameraFocus) - model_radius);
			fTexDist = MINF(fFocusDist, fCamDist);

		} else {

			fTexDist = fCamDist;
		}

		{
			WorldFXEntry *fx_parent = GET_REF(entry->base_drawable_entry.fx_parent_handle);
			if (fx_parent && fx_parent->material) {
				// animated material. Assume the worst and that the uvs are animating
				fUvDensity = TEX_DEMAND_LOAD_DEFAULT_UV_DENSITY;
			} else {
				fUvDensity = models[0].model->uv_density;
			}
		}
	}

	addVec3(instance_params.instance.world_matrix[3], gdraw->pos_offset, instance_params.instance.world_matrix[3]);

	if (!gfx_state.debug.noOnepassObjects)
	{
		if ((fDist - shared_bounds->radius) >= gfx_state.debug.onepassDistance*gdraw->lod_scale && instance_params.screen_area < gfx_state.debug.onepassScreenSpace)
			instance_params.no_zprepass = 1;
	}

	// Draw each LOD model
	for (j=0; j<model_count; j++) 
	{
		WorldDrawableLod *lod = models[j].draw;
		RdrDrawableGeo *geo_draw = lod->subobjects[0]->model->temp_geo_draw;
		bool has_wind = false, bInvalidPreswap;
		bool bAllowInstance=true;

		if (!models[j].geo_handle_primary)
		{
			devassert(0);
			continue;
		}

		instance_params.has_wind = false;
		instance_params.has_trunk_wind = false;

		if (models[j].model->data->process_time_flags & MODEL_PROCESSED_HAS_WIND)
		{
			instance_params.ignore_vertex_colors = true;
			if (!gdraw->wind_disabled)
			{
				wind_fade = calcWindFade(shared_bounds->radius, lod->far_dist, fDist, gdraw->lod_scale);
				instance_params.has_wind = has_wind = (wind_fade >= 0.01f && models[j].lod_index == 0);
			}
		}
		else if (!gdraw->wind_disabled && ((models[j].model->data->process_time_flags & MODEL_PROCESSED_HAS_TRUNK_WIND) || entry->base_drawable_entry.force_trunk_wind))
		{
			wind_fade = calcWindFade(shared_bounds->radius, lod->far_dist, fDist, gdraw->lod_scale);
			instance_params.has_trunk_wind = has_wind = (wind_fade >= 0.01f && models[j].lod_index == 0);
		}

		if (!geo_draw)
		{
			geo_draw = rdrDrawListAllocGeo(gdraw->draw_list, RTYPE_MODEL, models[j].model, models[j].model->data->tex_count, 0, 0);

			if (!geo_draw)
				continue;

			geo_draw->geo_handle_primary = models[j].geo_handle_primary;

			lod->subobjects[0]->model->temp_geo_draw = geo_draw;
		}

		bInvalidPreswap = lod->subobject_count != geo_draw->subobject_count;

		if (replacement_material || bInvalidPreswap)
		{
			SETUP_INSTANCE_PARAMS;
			RDRALLOC_SUBOBJECTS(gdraw->draw_list, instance_params, models[j].model, i);
			for (i = 0; i < geo_draw->subobject_count; ++i)
			{
				if (replacement_material)
				{
					gfxDemandLoadMaterialAtQueueTime(instance_params.subobjects[i], replacement_material, NULL, NULL, NULL, NULL, instance_params.per_drawable_data[i].instance_param, fTexDist, fUvDensity);
				}
				else// if (bInvalidPreswap)
				{
					// This will lose any swaps that were applied, but at least it will not crash
					gfxDemandLoadMaterialAtQueueTime(instance_params.subobjects[i], models[j].model->materials[i], NULL, NULL, NULL, NULL, instance_params.per_drawable_data[i].instance_param, fTexDist, fUvDensity);
				}
			}
		}
		else
		{
			//instance_params.per_drawable_data = draw->instance_data[models[j].lod_index].instance_params;
			SETUP_INSTANCE_PARAMS;
			RDRALLOC_SUBOBJECT_PTRS(instance_params, geo_draw->subobject_count);
			for (i = 0; i < geo_draw->subobject_count; ++i)
			{
				if (!lod->subobjects[i]->temp_rdr_subobject)
				{
					lod->subobjects[i]->temp_rdr_subobject = rdrDrawListAllocSubobject(gdraw->draw_list, models[j].model->data->tex_idx[i].count);
					lod->subobjects[i]->fallback_idx = getMaterialDrawIndex(lod->subobjects[i]->material_draws);
					lod->subobjects[i]->material_render_info = gfxDemandLoadPreSwappedMaterialAtQueueTime(lod->subobjects[i]->temp_rdr_subobject, lod->subobjects[i]->material_draws[lod->subobjects[i]->fallback_idx], fTexDist, fUvDensity);
				} else {
					MIN1F(lod->subobjects[i]->material_render_info->material_min_draw_dist[gfx_state.currentAction->action_type], fTexDist);
					MIN1F(lod->subobjects[i]->material_render_info->material_min_uv_density, fUvDensity);
				}
				memcpy(instance_params.per_drawable_data[i].instance_param, 
					draw->instance_param_list->lod_params[models[j].lod_index].subobject_params[i].fallback_params[lod->subobjects[i]->fallback_idx].instance_param, sizeof(Vec4));
				instance_params.subobjects[i] = lod->subobjects[i]->temp_rdr_subobject;
			}
		}

		instance_params.debug_me = draw->debug_me;
		instance_params.instance.morph = models[j].morph;

		if (replacement_material)
			setVec4(instance_params.instance.color, 1, 1, 1, models[j].alpha);
		else
			instance_params.instance.color[3] = draw->color[3] * models[j].alpha;
		instance_params.need_dof = models[j].alpha < 1;

		instance_params.instance.color[3] *= fx_alpha;
		setInstanceSelectionParams(&instance_params, draw, gdraw);

		if (has_wind)
		{
			F32 pivotOffset = entry->wind_params[2];
			F32 bbHeight = (models[0].model->model_parent->max[1] - pivotOffset);
			modelInstanceWindHelper(bbHeight,
									entry->wind_params, &instance_params);
			for (i = 0; i < geo_draw->subobject_count; ++i)
			{
				copyVec3(entry->current_wind_parameters, instance_params.per_drawable_data[i].instance_param);
				instance_params.per_drawable_data[i].instance_param[3] = wind_fade * entry->current_wind_parameters[3];
			}
		}

		if (modelHasMatidxs(models[j].model)) // Can't instance skinned objects
			bAllowInstance = false;

		if (gdraw->disable_vertex_lights) 
			instance_params.disable_vertex_light = true;

		rdrDrawListAddGeoInstance(gdraw->draw_list, geo_draw, &instance_params, RST_AUTO, draw->editor_only?ROC_EDITOR_ONLY:((draw->high_detail||draw->high_fill_detail)?ROC_WORLD_HIGH_DETAIL:ROC_WORLD), bAllowInstance);

		last_queued_as_alpha |= instance_params.queued_as_alpha;

		if (instance_params.uniqueDrawCount)
			gfxGeoIncrementUsedCount(models[j].model->geo_render_info, instance_params.uniqueDrawCount, true);

		// occluder check
		if ((frustum_visible & gdraw->visual_frustum_bit) && !did_occluder_check && models[j].alpha == 1 && gdraw->occlusion_buffer && lod->occlusion_materials &&
			instance_params.instance.color[3] >= 1.0f)
		{
			zoCheckAddOccluder(gdraw->occlusion_buffer, models[j].model, instance_params.instance.world_matrix, lod->occlusion_materials, draw->double_sided_occluder);
			did_occluder_check = true;
		}
	}

	if (model_count)
	{
		Vec3 world_min, world_max;
		mulBoundsAA(shared_bounds->local_min, shared_bounds->local_max, instance_params.instance.world_matrix, world_min, world_max);
		for (i = 0; i < gdraw->pass_count; ++i)
		{
			if (frustum_visible & gdraw->passes[i]->frustum_set_bit)
				frustumUpdateBounds(gdraw->passes[i]->frustum, world_min, world_max);
		}
	}

	PERFINFO_AUTO_STOP_CHECKED_L2(__FUNCTION__);
}

static void drawModelEntry(WorldModelEntry *entry, F32 dist_sqr, int frustum_visible, F32 entry_screen_space)
{
	// See if we need to draw it with a replacement material or draw it twice
	WorldFXEntry *fx_parent = GET_REF(entry->base_drawable_entry.fx_parent_handle);
	int bAux = 0;
	F32 fAlpha = 1.0f;
	RdrMaterialFlags base_flags=0;
	bool bDrewBaseAlready=false;
	bool bFxHidden = false;

	// Debug drawing mode - draw clustered and non-clustered objects as two different materials.
	if(gfxShowClustering && entry->base_drawable_entry.should_cluster) { // should_cluster is zero on unlocked layers
		Material *clusteredMaterial = materialFind(
			(entry->base_drawable_entry.should_cluster == CLUSTERED) ? "Blue8x8" : "Red8x8", WL_FOR_WORLD);
		drawModelEntryHelper(entry, dist_sqr, clusteredMaterial, frustum_visible, entry_screen_space, base_flags, fAlpha, bAux);
		return;
	}

	if (fx_parent)
	{
		if(fx_parent->fx_manager) {
			
			GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
			const char *pcModelName = NULL;
			bAux = dynFxNeedsAuxPass(fx_parent->fx_manager);
			bFxHidden = dtFxUpdateAndCheckModel(fx_parent->fx_manager, &entry->base_drawable_entry);
			
			// FX is making this model invisible.
			if(bFxHidden && !gdraw->in_editor) return;
		}

		if (fx_parent->material)
		{
			fAlpha = fx_parent->fx_alpha;
			// If it's "Add", draw it with changing the materials
			if (fx_parent->add_material)
			{
				RdrMaterialFlags flags = 0;
				if (fx_parent->dissolve_material && !(fx_parent->material->graphic_props.flags & (RMATERIAL_DECAL|RMATERIAL_DEPTHBIAS)))
				{
					flags = RMATERIAL_NOCOLORWRITE;
					base_flags = RMATERIAL_DEPTH_EQUALS|RMATERIAL_NOZWRITE;
				} else if (!(fx_parent->material->graphic_props.flags & (RMATERIAL_DECAL|RMATERIAL_DEPTHBIAS)))
				{

					if (!(fx_parent->material->graphic_props.flags & RMATERIAL_NOCOLORWRITE))
					{
						// draw base to test if it is alpha
						drawModelEntryHelper(entry, dist_sqr, NULL, frustum_visible, entry_screen_space, base_flags, fAlpha, bAux);
						bDrewBaseAlready = true;
						if (!last_queued_as_alpha)
							flags = RMATERIAL_DEPTH_EQUALS;
					}
				} else {
					if (!(fx_parent->material->graphic_props.flags & RMATERIAL_NOCOLORWRITE))
						flags = RMATERIAL_DEPTHBIAS|RMATERIAL_DECAL;
				}

				drawModelEntryHelper(entry, dist_sqr, fx_parent->material, frustum_visible, entry_screen_space, flags, fAlpha, 0);
			}
		}
	}
	if (!bDrewBaseAlready)
		drawModelEntryHelper(entry, dist_sqr, NULL, frustum_visible, entry_screen_space, base_flags, fAlpha, bAux);
}

static void drawModelInstanceEntry(WorldModelInstanceEntry *entry, F32 dist_sqr, int frustum_visible, F32 entry_screen_space, int entry_partial_clipped)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	WorldDrawableEntry *draw = &entry->base_drawable_entry;
	WorldCellEntryBounds *bounds = &draw->base_entry.bounds;
	WorldCellEntrySharedBounds *shared_bounds = draw->base_entry.shared_bounds;
	RdrDrawableGeo *geo_draw;
	RdrAddInstanceParams instance_params={0};
	RdrLightParams local_light_params={0};
	ModelToDraw models[NUM_MODELTODRAWS];
	int model_count;
	int i, k, num_instances;
	F32 inst_near_dist, inst_far_dist, near_dist, far_dist, near_fade_dist, far_fade_dist;
	bool no_near_distance_culling, no_far_distance_culling, no_fading, has_wind = false;
	F32 wind_fade = 0, model_radius;
	WorldDrawableLod *lod;
	RdrInstancePerDrawableData per_drawable_data;
	F32 fDist = sqrtf(dist_sqr);
	bool bInvalidPreswap;

	PERFINFO_AUTO_START_FUNC_L2();

	// use LOD 0
	model_count = gfxDemandLoadPreSwappedModel(draw->draw_list, models, ARRAY_SIZE(models), 0, 1, entry->lod_idx, NULL, 0, draw->should_cluster);
	if (!model_count || !eaSize(&entry->instances))
	{
		PERFINFO_AUTO_STOP_CHECKED_L2(__FUNCTION__);
		return;
	}

	setCommonInstanceParams(&instance_params, draw, frustum_visible, entry_screen_space);

	if (gdraw->disable_vertex_lights)
		instance_params.disable_vertex_light = true;

	if (frustum_visible & gdraw->visual_frustum_bit)
		fillLightInfo(
			&instance_params, shared_bounds->radius,
			instance_params.disable_vertex_light ? -1 : models[0].lod_index,
			draw, &local_light_params);


	lod = models[0].draw;
	geo_draw = lod->subobjects[0]->model->temp_geo_draw;

	if (!geo_draw)
	{
		geo_draw = rdrDrawListAllocGeo(gdraw->draw_list, RTYPE_MODEL, models[0].model, lod->subobject_count, 0, 0);

		if (!geo_draw)
		{
			PERFINFO_AUTO_STOP_CHECKED_L2(__FUNCTION__);
			return;
		}

		geo_draw->geo_handle_primary = models[0].geo_handle_primary;
		lod->subobjects[0]->model->temp_geo_draw = geo_draw;
	}

	bInvalidPreswap = lod->subobject_count != geo_draw->subobject_count;

	SETUP_INSTANCE_PARAMS;

	if (bInvalidPreswap)
	{
		RDRALLOC_SUBOBJECTS(gdraw->draw_list, instance_params, models[0].model, i);
		for (i = 0; i < geo_draw->subobject_count; ++i)
		{
			// This will lose any swaps that were applied, but at least it will not crash
			gfxDemandLoadMaterialAtQueueTime(instance_params.subobjects[i], models[0].model->materials[i], NULL, NULL, NULL, NULL,
				instance_params.per_drawable_data[i].instance_param, instance_params.zdist - instance_params.distance_offset, models[0].model->uv_density);
		}
	}
	else
	{
		RDRALLOC_SUBOBJECT_PTRS(instance_params, geo_draw->subobject_count);
		for (i = 0; i < geo_draw->subobject_count; ++i)
		{
			if (!lod->subobjects[i]->temp_rdr_subobject)
			{
				lod->subobjects[i]->temp_rdr_subobject = rdrDrawListAllocSubobject(gdraw->draw_list, models[0].model->data->tex_idx[i].count);
				lod->subobjects[i]->fallback_idx = getMaterialDrawIndex(lod->subobjects[i]->material_draws);
				lod->subobjects[i]->material_render_info = gfxDemandLoadPreSwappedMaterialAtQueueTime(lod->subobjects[i]->temp_rdr_subobject,
					lod->subobjects[i]->material_draws[lod->subobjects[i]->fallback_idx], instance_params.zdist - instance_params.distance_offset, models[0].model->uv_density); // Actual distance re-evaluated below
			}
			instance_params.subobjects[i] = lod->subobjects[i]->temp_rdr_subobject;
		}
	}

	instance_params.debug_me = draw->debug_me;

	model_radius = models[0].model->model_parent->radius;

	if (entry->lod_idx)
	{
		near_dist = draw->draw_list->drawable_lods[entry->lod_idx].near_dist * gdraw->lod_scale;
		far_dist = draw->draw_list->drawable_lods[entry->lod_idx].far_dist * gdraw->lod_scale;
		near_fade_dist = far_fade_dist = draw->draw_list->drawable_lods[entry->lod_idx].no_fade ? 0 : (FADE_DIST_BUFFER * gdraw->lod_scale);
	}
	else
	{
		near_dist = draw->draw_list->drawable_lods[0].near_dist * gdraw->lod_scale;
		far_dist = draw->draw_list->drawable_lods[draw->draw_list->lod_count-1].far_dist * gdraw->lod_scale;
		near_fade_dist = far_fade_dist = draw->draw_list->drawable_lods[draw->draw_list->lod_count-1].no_fade ? 0 : (FADE_DIST_BUFFER * gdraw->lod_scale);
	}

	if (near_dist < near_fade_dist)
		near_fade_dist = near_dist;
	inst_near_dist = near_dist - near_fade_dist;
	inst_far_dist = far_dist + far_fade_dist;

	no_near_distance_culling = !near_dist || (fDist - draw->world_fade_radius > inst_near_dist); // distance to near side of bounding sphere is greater than the near dist, so nothing inside can be less than the near distance
	no_far_distance_culling = fDist + draw->world_fade_radius < inst_far_dist; // distance to far side of bounding sphere is less than the far distance, so nothing inside can be greater than the far distance
	no_fading = far_fade_dist == 0;

	//////////////////////////////////////////////////////////////////////////
	// add instances

	INC_FRAME_COUNT(welded_entry_hits);
	
	num_instances = 0;
	for (i = 0; i < eaSize(&entry->instances); ++i)
	{
		WorldModelInstanceInfo *instance_info = entry->instances[i];
		F32 inst_dist, alpha, zdist;
		int partial_clip_bit = 1;

		INC_FRAME_COUNT(welded_instance_hits);

		setMatRow(instance_params.instance.world_matrix, 0, instance_info->world_matrix_x);
		setMatRow(instance_params.instance.world_matrix, 1, instance_info->world_matrix_y);
		setMatRow(instance_params.instance.world_matrix, 2, instance_info->world_matrix_z);
		addVec3(instance_params.instance.world_matrix[3], gdraw->pos_offset, instance_params.instance.world_matrix[3]);
		addVec3(instance_info->world_mid, gdraw->pos_offset, instance_params.instance.world_mid);


		if (!no_near_distance_culling || !no_far_distance_culling)
		{
			inst_dist = distance3(instance_params.instance.world_mid, gdraw->cam_pos);
			inst_dist -= instance_info->inst_radius;
		}

		if (!no_near_distance_culling)
		{
			INC_FRAME_COUNT(welded_instance_tests);
			if (inst_dist < inst_near_dist)
			{
				INC_FRAME_COUNT(welded_instance_dist_culls);
				continue;
			}
		}

		if (!no_far_distance_culling)
		{
			INC_FRAME_COUNT(welded_instance_tests);
			if (inst_dist > inst_far_dist)
			{
				INC_FRAME_COUNT(welded_instance_dist_culls);
				continue;
			}
		}

		instance_params.frustum_visible = frustum_visible;

		for (k = 0; k < gdraw->pass_count; ++k, partial_clip_bit <<= 1)
		{
			RdrDrawListPassData *pass = gdraw->passes[k];

			if (!(instance_params.frustum_visible & pass->frustum_set_bit))
				continue;

			if (entry_partial_clipped & partial_clip_bit)
			{
				INC_FRAME_COUNT(welded_instance_tests);
				if (!frustumCheckSphereWorld(pass->frustum, instance_params.instance.world_mid, instance_info->inst_radius))
				{
					INC_FRAME_COUNT(welded_instance_culls);
					instance_params.frustum_visible &= pass->frustum_clear_bits;
				}
			}
		}

		if (!instance_params.frustum_visible)
			continue;

		alpha = 1;
		if (!no_fading)
		{
			if (!no_near_distance_culling)
			{
				F32 inst_dist_fade = near_dist - inst_dist;
				alpha = 1.f - saturate(inst_dist_fade / near_fade_dist);
			}

			if (!no_far_distance_culling)
			{
				F32 inst_dist_fade = inst_dist - far_dist;
				alpha *= 1.f - saturate(inst_dist_fade / far_fade_dist);
			}
		}

		zdist = rdrDrawListCalcZDist(gdraw->draw_list, instance_params.instance.world_mid);
		MIN1F(instance_params.zdist, zdist);

		if (gfx_state.debug.show_instanced_objects)
		{
			setVec4(instance_params.instance.color, 1, 0, 0, alpha);
		}
		else
		{
			mulVecVec4(instance_info->tint_color, draw->color, instance_params.instance.color);
			instance_params.instance.color[3] *= alpha;
		}
		setInstanceSelectionParams(&instance_params, draw, gdraw);

		instance_params.instance.camera_facing = draw->camera_facing || instance_info->camera_facing;
		instance_params.instance.axis_camera_facing = draw->axis_camera_facing || instance_info->axis_camera_facing;

		if (instance_info->instance_param_list)
		{
			for (k=0; k<geo_draw->subobject_count; k++)
			{
				memcpy(instance_params.per_drawable_data[k].instance_param, 
					instance_info->instance_param_list->lod_params[models[0].lod_index].subobject_params[k].fallback_params[lod->subobjects[k]->fallback_idx].instance_param, sizeof(Vec4));
			}
		}
		else
		if (!bInvalidPreswap)
		{
			for (k=0; k<geo_draw->subobject_count; k++)
			{
				memcpy(instance_params.per_drawable_data[k].instance_param, 
					draw->instance_param_list->lod_params[models[0].lod_index].subobject_params[k].fallback_params[lod->subobjects[k]->fallback_idx].instance_param, sizeof(Vec4));
			}
		}

		instance_params.has_wind = false;
		instance_params.has_trunk_wind = false;
		has_wind = false;
		if (models[0].model->data->process_time_flags & MODEL_PROCESSED_HAS_WIND)
		{
			instance_params.ignore_vertex_colors = true;
			if (!gdraw->wind_disabled)
			{
				wind_fade = calcWindFade(model_radius, lod->far_dist, fDist, gdraw->lod_scale);
				instance_params.has_wind = has_wind = (wind_fade >= 0.01f && models[0].lod_index == 0);
			}
		}
		else if (!gdraw->wind_disabled && ((models[0].model->data->process_time_flags & MODEL_PROCESSED_HAS_TRUNK_WIND) || entry->base_drawable_entry.force_trunk_wind))
		{
			wind_fade = calcWindFade(model_radius, lod->far_dist, fDist, gdraw->lod_scale);
			instance_params.has_trunk_wind = has_wind = (wind_fade >= 0.01f && models[0].lod_index == 0);
		}

		if (has_wind)
		{
			F32 pivotOffset = entry->wind_params[2];
			F32 bbHeight = (models[0].model->model_parent->max[1] - pivotOffset);

			modelInstanceWindHelper(bbHeight, entry->wind_params, &instance_params);

			for (k = 0; k < geo_draw->subobject_count; k++)
			{
				copyVec3(instance_info->current_wind_parameters, instance_params.per_drawable_data[k].instance_param);
				instance_params.per_drawable_data[k].instance_param[3] = wind_fade * instance_info->current_wind_parameters[3];
			}
		}

		INC_FRAME_COUNT(welded_instance_draws);
		++num_instances;

		rdrDrawListAddGeoInstance(gdraw->draw_list, geo_draw, &instance_params, RST_AUTO, draw->editor_only?ROC_EDITOR_ONLY:((draw->high_detail||draw->high_fill_detail)?ROC_WORLD_HIGH_DETAIL:ROC_WORLD), true);
	}

	if (!bInvalidPreswap)
	{
		for (i = 0; i < geo_draw->subobject_count; ++i)
		{
			MIN1F(lod->subobjects[i]->material_render_info->material_min_draw_dist[gfx_state.currentAction->action_type],
                MAX(instance_params.zdist - instance_params.distance_offset, 0.0f));
		}
	}

	if (instance_params.uniqueDrawCount)
		gfxGeoIncrementUsedCount(models[0].model->geo_render_info, instance_params.uniqueDrawCount, true);

	PERFINFO_AUTO_STOP_CHECKED_L2(__FUNCTION__);
}

static void drawSplinedModelEntry(WorldSplinedModelEntry *entry, F32 dist_sqr, int frustum_visible, F32 entry_screen_space)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	WorldDrawableEntry *draw = &entry->base_drawable_entry;
	WorldCellEntryBounds *bounds = &draw->base_entry.bounds;
	WorldCellEntrySharedBounds *shared_bounds = draw->base_entry.shared_bounds;
	bool did_lights = false, did_occluder_check = false;
	ModelToDraw models[NUM_MODELTODRAWS];
	RdrAddInstanceParams instance_params={0};
	RdrLightParams local_light_params={0};
	int model_count;
	Vec3 world_min, world_max;
	int i, j;
	F32 fDist = sqrtf(dist_sqr), model_radius;
	RdrInstancePerDrawableData per_drawable_data;

	PERFINFO_AUTO_START_FUNC_L2();

	model_count = gfxDemandLoadPreSwappedModel(draw->draw_list, models, ARRAY_SIZE(models), fDist, gdraw->lod_scale, -1, &entry->model_tracker, draw->world_fade_radius, false);
	if (!model_count)
	{
		PERFINFO_AUTO_STOP_L2();
		return;
	}

	model_radius = models[0].model->model_parent->radius;

	setCommonInstanceParams(&instance_params, draw, frustum_visible, entry_screen_space);
	copyVec3(draw->color, instance_params.instance.color);
	setInstanceSelectionParams(&instance_params, draw, gdraw);
	memcpy_intrinsic(&instance_params.instance.world_matrix[0][0], &bounds->world_matrix[0][0], sizeof(Mat4));
	addVec3(instance_params.instance.world_matrix[3], gdraw->pos_offset, instance_params.instance.world_matrix[3]);

	if (frustum_visible & gdraw->visual_frustum_bit)
		fillLightInfo(&instance_params, model_radius, models[0].lod_index, draw, &local_light_params);

	did_occluder_check = !draw->occluder;

	// Draw each LOD model
	for (j=0; j<model_count; j++) 
	{
		RdrDrawableSkinnedModel *skin_draw;
		RdrDrawableGeo *geo_draw;

		if (!models[j].geo_handle_primary)
		{
			devassert(0);
			continue;
		}

		skin_draw = rdrDrawListAllocSkinnedModel(gdraw->draw_list, RTYPE_CURVED_MODEL, models[j].model, models[j].draw->subobject_count, 0, ARRAY_SIZE(entry->spline_mats), NULL);

		if (!skin_draw)
			continue;

		memcpy(skin_draw->skinning_mat_array, entry->spline_mats, skin_draw->num_bones * sizeof(SkinningMat4));

		// Do some matrix transposition to make the vertex shader faster
		{
			SkinningMat4 *mat = skin_draw->skinning_mat_array+1;
			transposeMat34_33(mat[0]);
			mat[0][2][0] = -1.f/mat[0][2][0]; // geom_length -> -inv_geom_length
		}

		geo_draw = &skin_draw->base_geo_drawable;
		geo_draw->geo_handle_primary = models[j].geo_handle_primary;

		SETUP_INSTANCE_PARAMS;
		//instance_params.per_drawable_data = draw->instance_data[models[j].lod_index].instance_params;

		RDRALLOC_SUBOBJECTS(gdraw->draw_list, instance_params, models[j].model, i);
		for (i = 0; i < geo_draw->subobject_count; ++i)
		{
			int idx = getMaterialDrawIndex(models[j].draw->subobjects[i]->material_draws);
			gfxDemandLoadPreSwappedMaterialAtQueueTime(instance_params.subobjects[i], models[j].draw->subobjects[i]->material_draws[idx], 
				fDist - instance_params.distance_offset, models[j].model->uv_density);
			memcpy(instance_params.per_drawable_data[i].instance_param, 
				draw->instance_param_list->lod_params[models[j].lod_index].subobject_params[i].fallback_params[idx].instance_param, sizeof(Vec4));
		}
		
		instance_params.debug_me = draw->debug_me;
		instance_params.instance.color[3] = draw->color[3] * models[j].alpha;

		if (gdraw->disable_vertex_lights)
			instance_params.disable_vertex_light = true;

		rdrDrawListAddSkinnedModel(gdraw->draw_list, skin_draw, &instance_params, RST_AUTO, draw->editor_only?ROC_EDITOR_ONLY:((draw->high_detail||draw->high_fill_detail)?ROC_WORLD_HIGH_DETAIL:ROC_WORLD));
	}

	if (model_count)
	{
		mulBoundsAA(shared_bounds->local_min, shared_bounds->local_max, instance_params.instance.world_matrix, world_min, world_max);
		for (i = 0; i < gdraw->pass_count; ++i)
		{
			if (frustum_visible & gdraw->passes[i]->frustum_set_bit)
				frustumUpdateBounds(gdraw->passes[i]->frustum, world_min, world_max);
		}
	}

	PERFINFO_AUTO_STOP_L2();
}

static void drawOcclusionEntry(WorldOcclusionEntry *entry, F32 dist_sqr, int frustum_visible, F32 entry_screen_space)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	WorldDrawableEntry *draw = &entry->base_drawable_entry;
	WorldCellEntryBounds *bounds = &draw->base_entry.bounds;
	WorldCellEntrySharedBounds *shared_bounds = draw->base_entry.shared_bounds;
	Model *model = entry->model;
	ModelLOD *model_lod = model?modelGetLOD(model, 0):NULL;
	bool hide_volume = true;

	if (!(frustum_visible & gdraw->visual_frustum_bit))
		return;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	if (gdraw->in_editor)
	{
		bool all_types_hidden = false;
		if (entry->type_flags & ~VOL_TYPE_TINTED)
		{
			all_types_hidden = true;
			if ((entry->type_flags & VOL_TYPE_OCCLUDER) && !gfx_state.debug.vis_settings.hide_occlusion_volumes)
				all_types_hidden = false;
			else if ((entry->type_flags & VOL_TYPE_SOUND) && !gfx_state.debug.vis_settings.hide_audio_volumes)
				all_types_hidden = false;
			else if ((entry->type_flags & VOL_TYPE_SKY) && !gfx_state.debug.vis_settings.hide_skyfade_volumes)
				all_types_hidden = false;
			else if ((entry->type_flags & VOL_TYPE_NEIGHBORHOOD) && !gfx_state.debug.vis_settings.hide_neighborhood_volumes)
				all_types_hidden = false;
			else if ((entry->type_flags & VOL_TYPE_INTERACTION) && !gfx_state.debug.vis_settings.hide_interaction_volumes)
				all_types_hidden = false;
			else if ((entry->type_flags & VOL_TYPE_LANDMARK) && !gfx_state.debug.vis_settings.hide_landmark_volumes)
				all_types_hidden = false;
			else if ((entry->type_flags & VOL_TYPE_POWER) && !gfx_state.debug.vis_settings.hide_power_volumes)
				all_types_hidden = false;
			else if ((entry->type_flags & VOL_TYPE_WARP) && !gfx_state.debug.vis_settings.hide_warp_volumes)
				all_types_hidden = false;
			else if ((entry->type_flags & VOL_TYPE_EXCLUSION) && !gfx_state.debug.vis_settings.hide_exclusion_volumes)
				all_types_hidden = false;
			else if (((entry->type_flags & VOL_TYPE_GENESIS) || (entry->type_flags & VOL_TYPE_BLOB_FILTER)) &&
				!gfx_state.debug.vis_settings.hide_genesis_volumes)
				all_types_hidden = false;
		}
		else if (model && gfx_state.debug.vis_settings.hide_occlusion_volumes)
		{
			all_types_hidden = true;
		}
		else if (gfx_state.debug.vis_settings.hide_untyped_volumes)
		{
			all_types_hidden = true;		
		}

		// volume should only be hidden if ALL OF ITS TYPES are set to be hidden
		hide_volume = gfx_state.debug.vis_settings.hide_all_volumes || all_types_hidden;

		if(!hide_volume && !(entry->type_flags & VOL_TYPE_TINTED))
		{
			if (entry->type_flags & VOL_TYPE_OCCLUDER) 
				worldGetOcclusionVolumeColor(draw->color);
			else if (entry->type_flags & VOL_TYPE_SKY) 
				worldGetSkyFadeVolumeColor(draw->color);
			else if (entry->type_flags & VOL_TYPE_NEIGHBORHOOD)
				worldGetNeighborhoodVolumeColor(draw->color);
			else if (entry->type_flags & VOL_TYPE_INTERACTION)
				worldGetInteractionVolumeColor(draw->color);
			else if (entry->type_flags & VOL_TYPE_LANDMARK)
				worldGetLandmarkVolumeColor(draw->color);
			else if (entry->type_flags & VOL_TYPE_POWER)
				worldGetPowerVolumeColor(draw->color);
			else if (entry->type_flags & VOL_TYPE_WARP)
				worldGetWarpVolumeColor(draw->color);
			else if (entry->type_flags & VOL_TYPE_SOUND) 
				worldGetAudioVolumeColor(draw->color);
			else if (entry->type_flags & VOL_TYPE_GENESIS)
				worldGetGenesisVolumeColor(draw->color);
			else if (entry->type_flags & VOL_TYPE_BLOB_FILTER)
				worldGetTerrainFilterVolumeColor(draw->color);
			else if (entry->type_flags & VOL_TYPE_EXCLUSION)
				worldGetTerrainExclusionVolumeColor(draw->color);
			else // Any other volume type
				setVec4(draw->color, 1.0f, 0, 0.5f, 0.5f); // R_b
		}
	}

	if (!model_lod)
	{
		if (draw->occluder && !entry->volume_radius && gdraw->occlusion_buffer)
			zoCheckAddOccluderVolume(gdraw->occlusion_buffer, entry->volume_min, entry->volume_max, entry->volume_faces, bounds->world_matrix);

		if (!hide_volume)
		{
			int wireframe = (gdraw->global_wireframe || !gdraw->in_editor) ? gdraw->global_wireframe : draw->wireframe;
			Color clr;
			vec4ToColor(&clr, draw->color);

			if (entry->volume_radius)
			{
				if (wireframe != 2)
					gfxDrawSphere3D(bounds->world_mid, entry->volume_radius, 0, clr, 0);
				if (wireframe)
					gfxDrawSphere3D(bounds->world_mid, entry->volume_radius, 0, colorFromRGBA(0xffffffff), 1);
			}
			else
			{
				if (wireframe != 2)
					gfxDrawBox3DEx(entry->volume_min, entry->volume_max, bounds->world_matrix, clr, 0, entry->volume_faces);
				if (wireframe)
					gfxDrawBox3D(entry->volume_min, entry->volume_max, bounds->world_matrix, colorFromRGBA(0xffffffff), 1);
			}
		}

		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	
	if (!modelLODIsLoaded(model_lod))
	{
		if (model_lod->loadstate == GEO_NOT_LOADED)
			modelLODRequestBackgroundLoad(model_lod);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	if (gdraw->occlusion_buffer) {

		Mat4 worldMatrixWithScale;
		Vec3 scale;

		copyMat4(bounds->world_matrix, worldMatrixWithScale);
		worldOcclusionEntryGetScale(entry, scale);
		scaleMat3Vec3(worldMatrixWithScale, scale);

		if(!entry->looked_for_drawable && entry->base_entry_data.cell)
		{
			// Note, if a occluder is not in the same cell or below the cell of it's drawable, this will not work
			entry->pMatchingDrawable = worldCellFindDrawableForOccluder(entry->base_entry_data.cell, entry );
			entry->looked_for_drawable = 1;
		}
		 
		if(!entry->pMatchingDrawable || entry->pMatchingDrawable->is_camera_fading == 0)
		{
			zoCheckAddOccluder(
				gdraw->occlusion_buffer, model_lod, worldMatrixWithScale,
				0xffffffff, draw->double_sided_occluder);
		}
	}

	if (!hide_volume)
	{
		ModelToDraw models[NUM_MODELTODRAWS];
		RdrAddInstanceParams instance_params={0};
		RdrInstancePerDrawableData per_drawable_data;
		int model_count;
		int i, j;
		Vec3 modelScale;

		setCommonInstanceParams(&instance_params, draw, frustum_visible, entry_screen_space);
		copyVec3(draw->color, instance_params.instance.color);
		setInstanceSelectionParams(&instance_params, draw, gdraw);
		memcpy_intrinsic(&instance_params.instance.world_matrix[0][0], &bounds->world_matrix[0][0], sizeof(Mat4));

		worldOcclusionEntryGetScale(entry, modelScale);
		scaleMat3Vec3(instance_params.instance.world_matrix, modelScale);

		addVec3(instance_params.instance.world_matrix[3], gdraw->pos_offset, instance_params.instance.world_matrix[3]);

		setVec3same(instance_params.ambient_multiplier, 1);

		// Draw each LOD model
		model_count = gfxDemandLoadModel(model, models, ARRAY_SIZE(models), 0, 1, 0, NULL, model->radius);
		for (j=0; j<model_count; j++) 
		{
			RdrDrawableGeo *geo_draw;

			if (!models[j].geo_handle_primary)
			{
				devassert(0);
				continue;
			}

			geo_draw = rdrDrawListAllocGeo(gdraw->draw_list, RTYPE_MODEL, models[j].model, models[j].model->geo_render_info->subobject_count, 0, 0);
			
			if (!geo_draw)
				continue;

			geo_draw->geo_handle_primary = models[j].geo_handle_primary;

			SETUP_INSTANCE_PARAMS;

			RDRALLOC_SUBOBJECTS(gdraw->draw_list, instance_params, models[j].model, i);
			for (i = 0; i < geo_draw->subobject_count; ++i)
				gfxDemandLoadMaterialAtQueueTime(instance_params.subobjects[i], models[j].model->materials[i], NULL, NULL, NULL, NULL,
					instance_params.per_drawable_data[i].instance_param, instance_params.zdist - instance_params.distance_offset, models[j].model->uv_density);

			instance_params.debug_me = draw->debug_me;
			instance_params.add_material_flags = draw->double_sided_occluder?RMATERIAL_DOUBLESIDED:0;
			instance_params.add_material_flags |= RMATERIAL_NOZWRITE;
			instance_params.instance.morph = models[j].morph;
			instance_params.instance.color[3] = draw->color[3] * models[j].alpha;

			rdrDrawListAddGeoInstance(gdraw->draw_list, geo_draw, &instance_params, RST_AUTO, ROC_EDITOR_ONLY, true);

			gfxGeoIncrementUsedCount(models[j].model->geo_render_info, models[j].model->geo_render_info->subobject_count, true);
		}
	}

	PERFINFO_AUTO_STOP_FUNC();
}

__forceinline static bool checkCellBoundsNotVisible(WorldCellCullHeader *cell_header, RdrDrawListPassData *pass, U8 clipped, U8 *inherited_bits, GfxGlobalDrawParams *gdraw)
{
	if (pass == gdraw->visual_pass && gdraw->occlusion_buffer)
	{
		int nearClipped;
		Vec4_aligned eye_bounds[8];

		mulBounds(cell_header->world_min, cell_header->world_max, pass->viewmat, eye_bounds);

		nearClipped = frustumCheckBoxNearClipped(pass->frustum, eye_bounds);
		if (nearClipped == 2)
		{
			zoSetOccluded(gdraw->occlusion_buffer, &cell_header->last_occlusion_update_time, &cell_header->occluded_bits);
			return true;
		}
		else if (frustumCheckBoxNearClippedInView(pass->frustum,cell_header->world_min, cell_header->world_max, pass->viewmat))
		{
			return false;
		}
		// only test non-leaf nodes if they are not clipped by the near plane
		else if (!nearClipped && !zoTestBounds(gdraw->occlusion_buffer, eye_bounds, nearClipped, &cell_header->last_occlusion_update_time, &cell_header->occluded_bits, inherited_bits, NULL, NULL))
		{
			return true;
		}

		return false;
	}

	return clipped != FRUSTUM_CLIP_NONE && !frustumCheckBoxWorld(pass->frustum, clipped, cell_header->world_min, cell_header->world_max, NULL, false);
}

#define FRUSTUM_ALL_PLANES (FRUSTUM_CLIP_TOP|FRUSTUM_CLIP_BOTTOM|FRUSTUM_CLIP_LEFT|FRUSTUM_CLIP_RIGHT|FRUSTUM_CLIP_NEAR|FRUSTUM_CLIP_FAR)

static int debugShowBounds = 0;
AUTO_CMD_INT(debugShowBounds, zoDrawWorldEntryBounds);
static WorldDrawableEntry * g_pDebugDrawEntry = NULL;
static int updateDebugDrawEntry = 0;
AUTO_CMD_INT(updateDebugDrawEntry, zoUpdateDebugDrawEntry);
extern int debugShowZRect;

__forceinline static bool checkEntryBoundsNotVisible(WorldDrawableEntry *entry, RdrDrawListPassData *pass, U8 clipped, U8 inherited_bits, F32 *screen_space, GfxGlobalDrawParams *gdraw)
{
	const WorldCellEntryBounds * bounds = &entry->base_entry.bounds;
	const WorldCellEntrySharedBounds *shared_bounds = entry->base_entry.shared_bounds;

	debugShowZRect = 0;

	if (pass == gdraw->visual_pass)
	{
		int nearClipped;
		Vec4_aligned eye_bounds[8];
		Mat4 modelviewmat;

		if (debugShowBounds)
		{
			if (updateDebugDrawEntry == 2)
			{
				g_pDebugDrawEntry = entry;
				updateDebugDrawEntry = 0;
			}
			if (!g_pDebugDrawEntry || g_pDebugDrawEntry == entry)
			{
				if (updateDebugDrawEntry == 1)
				{
					updateDebugDrawEntry = 2;
				}
				gfxDrawBox3DARGB(shared_bounds->local_min,shared_bounds->local_max, bounds->world_matrix,0x500000FF, 0.0f);
				debugShowZRect = 1;
			}
		}

		mulMat4Inline(pass->viewmat, bounds->world_matrix, modelviewmat);
		mulBounds(shared_bounds->local_min, shared_bounds->local_max, modelviewmat, eye_bounds);

		nearClipped = frustumCheckBoxNearClipped(pass->frustum, eye_bounds);

		if (gdraw->occlusion_buffer)
		{
			// See if the bounding box intersects the near plane and skip the fancy test

			if (nearClipped == 2)
			{
				zoSetOccluded(gdraw->occlusion_buffer, &entry->occlusion_data.last_update_time, &entry->occluded_bits);
				return true;
			}
			else if (frustumCheckBoxNearClippedInView(pass->frustum,shared_bounds->local_min,shared_bounds->local_max,modelviewmat))
			{
				return false;
			}
			else if (!zoTestBounds(gdraw->occlusion_buffer, eye_bounds, nearClipped, &entry->occlusion_data.last_update_time, &entry->occluded_bits, &inherited_bits, screen_space, NULL))
			{
				return true;
			}
		}
		else if (nearClipped == 2 || !gfxGetScreenSpace(pass->frustum, gdraw->nextVisProjection, nearClipped, eye_bounds, screen_space))
		{
			return true;
		}

		return false;
	}

	return clipped != FRUSTUM_CLIP_NONE && !frustumCheckBoxWorld(pass->frustum, clipped, shared_bounds->local_min, shared_bounds->local_max, bounds->world_matrix, false);
}

__forceinline static bool checkEntryBoundsNotVisibleSimple(WorldDrawableEntry *entry, RdrDrawListPassData *pass, U8 clipped, const Vec3 * min_max, U8 inherited_bits, F32 *screen_space, GfxGlobalDrawParams *gdraw)
{
	if (pass == gdraw->visual_pass)
	{
		int nearClipped;
		Vec4_aligned eye_bounds[8];
		mulBounds(min_max[0], min_max[1], pass->viewmat, eye_bounds);

		nearClipped = clipped & FRUSTUM_CLIP_NEAR ? 1 : 0;

		if (gdraw->occlusion_buffer)
		{
			if (!zoTestBounds(gdraw->occlusion_buffer, eye_bounds, nearClipped, &entry->occlusion_data.last_update_time, &entry->occluded_bits, &inherited_bits, screen_space, NULL))
			{
				return true;
			}
		}
	}
	return false;
}

static void setupEntryCulHeaderBBoxCornerOffset(WorldCellEntryCullHeader *entry_header, const WorldDrawableEntry *entry)
{
	Vec3 newmin, newmax;
	mulBoundsAA(entry->base_entry.shared_bounds->local_min, entry->base_entry.shared_bounds->local_max, 
		entry->base_entry.bounds.world_matrix, newmin, newmax);
	subVec3(newmax, entry_header->world_mid, newmax);

	entry_header->world_corner[0] = (U16)ceil(newmax[0]);
	entry_header->world_corner[1] = (U16)ceil(newmax[1]);
	entry_header->world_corner[2] = (U16)ceil(newmax[2]);
}

#define CLUSTER_MIN_FAR_LOD_DIST 10000

static void initEntryCullHeader(WorldCellEntryCullHeader *entry_header, WorldDrawableEntry *entry, F32 lod_scale)
{
	F32 near_lod_dist = lod_scale * entry->base_entry.shared_bounds->near_lod_near_dist - 20;
	F32 far_lod_dist = lod_scale * entry->base_entry.shared_bounds->far_lod_far_dist + 20;
	const WorldCellEntryData *entry_data =  worldCellEntryGetData(&entry->base_entry);

	if (entry->should_cluster != CLUSTERED)
	{
		entry_header->leaf_cell = NULL;
	}
	else
	{
		entry_header->leaf_cell = worldCellFindLeafMostCellForEntry(entry_data->cell->region->root_world_cell, entry);

		if (fabs(entry->base_entry.bounds.world_mid[0]) >= SHRT_MAX || fabs(entry->base_entry.bounds.world_mid[1]) >= SHRT_MAX || 
			fabs(entry->base_entry.bounds.world_mid[2]) >= SHRT_MAX)
		{
			Errorf("Entry world_fade_mid (%f, %f, %f) is too large.", entry->world_fade_mid[0], entry->world_fade_mid[1], entry->world_fade_mid[2]);
		}
		far_lod_dist = max(CLUSTER_MIN_FAR_LOD_DIST, far_lod_dist);
	}
	copyVec3(entry->world_fade_mid, entry_header->world_fade_mid);
	setVec3(entry_header->world_mid,
		(S16)round(entry->base_entry.bounds.world_mid[0]),
		(S16)round(entry->base_entry.bounds.world_mid[1]),
		(S16)round(entry->base_entry.bounds.world_mid[2]));

	if (near_lod_dist > 0)
	{
		if (entry->base_entry.type == WCENT_MODELINSTANCED)
		{
			near_lod_dist -= entry->world_fade_radius;
			entry_header->scaled_near_vis_dist = (U16)ceil(MAXF(near_lod_dist, 0));
		}
		else
		{
			entry_header->scaled_near_vis_dist = (U16)MIN(0xffff,(U32)ceil(near_lod_dist + entry->world_fade_radius));
		}
	}
	else
	{
		entry_header->scaled_near_vis_dist = 0;
	}
	entry_header->scaled_far_vis_dist = (U16)MIN(0xffff,(U32)ceil(far_lod_dist + entry->world_fade_radius));
	setupEntryCulHeaderBBoxCornerOffset(entry_header, entry);
	entry_header->entry_and_flags = (intptr_t)entry;
	assert(!(entry_header->entry_and_flags & ~ENTRY_CULL_POINTER_MASK));
	if (worldCellEntryIsPartitionOpen(&entry->base_entry, PARTITION_CLIENT) && !worldCellEntryIsPartitionDisabled(&entry->base_entry, PARTITION_CLIENT))
		entry_header->entry_and_flags |= ENTRY_CULL_IS_ACTIVE_MASK;
}

static int cmpEntriesForDrawing(const WorldDrawableEntry **entry1, const WorldDrawableEntry **entry2)
{
	int t;
	t = ((int)(*entry1)->base_entry.type) - ((int)(*entry2)->base_entry.type);
	if (t)
		return t;
	return ((intptr_t)(*entry1)->draw_list) - ((intptr_t)(*entry2)->draw_list);
}

static void * createEntryCullHeadersBlock(int count_headers)
{
	void * entry_header_and_count_block;
	entry_header_and_count_block = malloc(sizeof(int) + count_headers * sizeof(WorldCellEntryCullHeader) + DATACACHE_LINE_SIZE_BYTES);
	CULL_HEADER_SETCOUNT(entry_header_and_count_block, count_headers );
	return entry_header_and_count_block;
}


static void initEntryCullHeaders(WorldCellCullHeader *cell_header, F32 lod_scale, bool draw_high_detail, bool draw_high_fill_detail)
{
	// copy hot data - the flags & bounds
	WorldCell *cell = cell_header->cell;
	WorldDrawableEntry **sorted_entries = NULL;
	WorldCellEntryCullHeader *headers;
	int i;

	for (i = 0; i < eaSize(&cell->drawable.drawable_entries); ++i)
	{
		WorldDrawableEntry *draw = cell->drawable.drawable_entries[i];
		if (draw->high_detail && !draw_high_detail || draw->low_detail && draw_high_detail || draw->high_fill_detail && !draw_high_fill_detail)
			continue;
		eaPush(&sorted_entries, draw);
	}

	if (!cell_header->entry_headers && eaSize(&sorted_entries))
	{
		cell_header->entry_headers = createEntryCullHeadersBlock(eaSize(&sorted_entries));

		eaQSortG(sorted_entries, cmpEntriesForDrawing);

		headers = CULL_HEADER_START(cell_header->entry_headers);
		for (i = 0; i < eaSize(&sorted_entries); ++i)
			initEntryCullHeader(&headers[i], sorted_entries[i], lod_scale);
	}

	if (!cell_header->editor_only_entry_headers && eaSize(&cell->drawable.editor_only_entries))
	{
		cell_header->editor_only_entry_headers = createEntryCullHeadersBlock(eaSize(&cell->drawable.editor_only_entries));

		eaCopy(&sorted_entries, &cell->drawable.editor_only_entries);
		eaQSortG(sorted_entries, cmpEntriesForDrawing);

		headers = CULL_HEADER_START(cell_header->editor_only_entry_headers);
		for (i = 0; i < eaSize(&sorted_entries); ++i)
			initEntryCullHeader(&headers[i], sorted_entries[i], lod_scale);
	}

	cell_header->entries_inited = true;
	eaDestroy(&sorted_entries);
}

static void initWeldedEntryCullHeaders(WorldCellCullHeader *cell_header, F32 lod_scale, bool draw_high_detail, bool draw_high_fill_detail)
{
	// copy hot data - the flags & bounds
	WorldCell *cell = cell_header->cell, *parent_cell;
	WorldDrawableEntry **sorted_entries = NULL;
	WorldCellEntryCullHeader *headers;
	int i, j;

	for (i = 0; i < eaSize(&cell->drawable.bins); ++i)
	{
		for (j = 0; j < eaSize(&cell->drawable.bins[i]->drawable_entries); ++j)
		{
			WorldDrawableEntry *draw = cell->drawable.bins[i]->drawable_entries[j];
			if (draw->high_detail && !draw_high_detail ||
					draw->low_detail && draw_high_detail ||
					draw->high_fill_detail && !draw_high_fill_detail ||
					draw->should_cluster == CLUSTERED)
				continue;
			eaPush(&sorted_entries, draw);
		}
	}

	// start at the parent since the near fade drawable entries at this cell level are already in the welded bins
	for (parent_cell = cell->parent; parent_cell; parent_cell = parent_cell->parent)
	{
		for (i = 0; i < eaSize(&parent_cell->drawable.near_fade_entries); ++i)
		{
			// check if entry is within range of the current cell
			if (worldNearFadeEntryAffectsCell(parent_cell->drawable.near_fade_entries[i], cell) &&
					!parent_cell->drawable.near_fade_entries[i]->should_cluster == CLUSTERED)
				eaPush(&sorted_entries, parent_cell->drawable.near_fade_entries[i]);
		}
	}

	if (!cell_header->welded_entry_headers && eaSize(&sorted_entries))
	{
		cell_header->welded_entry_headers = createEntryCullHeadersBlock(eaSize(&sorted_entries));
		*((int*)cell_header->welded_entry_headers) = eaSize(&sorted_entries);

		eaQSortG(sorted_entries, cmpEntriesForDrawing);

		headers = CULL_HEADER_START(cell_header->welded_entry_headers);
		for (i = 0; i < eaSize(&sorted_entries); ++i)
			initEntryCullHeader(&headers[i], sorted_entries[i], lod_scale);
	}

	cell_header->welded_entries_inited = true;
	eaDestroy(&sorted_entries);
}

static void clearHeaderPointers(WorldCell *cell)
{
	int i;
	cell->drawable.header = NULL;
	for (i = 0; i < ARRAY_SIZE(cell->children); ++i)
	{
		if (cell->children[i])
			clearHeaderPointers(cell->children[i]);
	}
}

static int initHeader(WorldCell *cell, WorldCellCullHeader *header)
{
	int i, total_count, count = worldCellGetChildCountInclusive(cell, true);
	WorldCellCullHeader *myHeader = header;

	assert(count < USHRT_MAX); // skip offset is a U16
	assert(!header->skip_offset);

	header->skip_offset = count;

	header->cell = cell;
	cell->drawable.header = header;
	header->is_open = cell->cell_state == WCS_OPEN;
	header->no_fog = cell->drawable.no_fog;

	worldCellSetupDrawableBlockRange(cell, myHeader);

	header++;
	total_count = 1;

	for (i = 0; i < ARRAY_SIZE(cell->children); ++i)
	{
		if (cell->children[i])
		{
			if (cell->children[i]->no_drawables  && !cell->children[i]->cluster_related)
			{
				clearHeaderPointers(cell->children[i]);
			}
			else
			{
				int child_count = initHeader(cell->children[i], header);
				header += child_count;
				total_count += child_count;
			}
		}
	}

	worldCellSetupDrawableBlockRange(cell, myHeader);

	assert(total_count == count);

	return count;
}

static WorldCellCullHeader *initWorldCellCullHeaders(WorldCell *root_cell)
{
	int count = 0, total_count = worldCellGetChildCountInclusive(root_cell, true);
	WorldCellCullHeader *headers = calloc(MAX(1,total_count), sizeof(WorldCellCullHeader));

	if (root_cell)
	{
		if (root_cell->no_drawables)
			clearHeaderPointers(root_cell);
		else
			count += initHeader(root_cell, headers);
	}

	assert(total_count == count);

	return headers;
}

typedef struct WorldCellQueuedHeader
{
	WorldCellCullHeader *cell_header;
	U8 cell_clipped[MAX_RENDER_PASSES];
	U8 inherited_bits;
} WorldCellQueuedHeader;

typedef struct WorldCellQueuedHeaderArray
{
	WorldCellQueuedHeader *headers;
	int header_count, header_max;
} WorldCellQueuedHeaderArray;

typedef struct WorldCellCullStack
{
	U64 not_frustum_clipped;			// FRUSTUM_CLIP_TYPE
	U8 inherited_bits;					// occlusion inherited visibility bits
	WorldCellCullHeader *root;
	WorldCellCullHeader *end;			// when we reach this header we need to pop the stack
} WorldCellCullStack;

static void cullWorldCells(WorldCellCullHeader *root_cell_header, U64 not_frustum_clipped_init, WorldCellQueuedHeaderArray *queued_headers)
{
	static WorldCellCullStack *cull_stack;
	static int cull_stack_count, cull_stack_max;

	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	WorldCellCullStack *cull_stack_top;
	WorldCellCullHeader *cell_header = root_cell_header;
	int i;

	cull_stack_count = 0;
	cull_stack_top = dynArrayAddStruct_no_memset(cull_stack, cull_stack_count, cull_stack_max);

	cull_stack_top->inherited_bits = 0;
	cull_stack_top->not_frustum_clipped = not_frustum_clipped_init;
	cull_stack_top->root = cell_header;
	cull_stack_top->end = cell_header + cell_header->skip_offset;

	while (cull_stack_count)
	{
		WorldCellQueuedHeader *queued_header;
		U64 not_frustum_clipped; // FRUSTUM_CLIP_TYPE
		F32 dist_sqr;

		while (cell_header >= cull_stack_top->end)
		{
			// pop stack
			cull_stack_count--;
			if (!cull_stack_count)
				break;
			cull_stack_top = &cull_stack[cull_stack_count-1];
		}
		if (!cull_stack_count)
			break;

		PREFETCH(cell_header + 1);
		PREFETCH(cell_header + cell_header->skip_offset);

#ifdef _FULLDEBUG
		if (cell_header->cell->debug_me)
			gfxXYprintf(1, 17, "Debug cell hit.");
#endif

		INC_FRAME_COUNT(cell_hits);

		dist_sqr = distance3Squared(cell_header->world_mid, gdraw->cam_pos);

		if (gdraw->clip_by_distance && 
			(!gdraw->clip_only_below_camera || cell_header->world_mid[1] + cell_header->radius < gdraw->cam_pos[1]) && 
			dist_sqr > SQR(gdraw->clip_distance + cell_header->radius) &&
			!cell_header->no_fog)
		{
			INC_FRAME_COUNT(cell_fog_culls);

			// skip children
			cell_header += cell_header->skip_offset;
			continue;
		}

		// queue the cell for entry traversal (may be unqueued later if frustum culled)
		queued_header = dynArrayAddStruct_no_memset(queued_headers->headers, queued_headers->header_count, queued_headers->header_max);
		queued_header->cell_header = cell_header;
		queued_header->inherited_bits = cull_stack_top->inherited_bits;

		// 543210
		// tctctc t set = trivially accepted, c set = partially clipped, neither set = culled on that frustum
		not_frustum_clipped = cull_stack_top->not_frustum_clipped;
		for (i = 0; i < gdraw->pass_count; ++i)
		{
			RdrDrawListPassData *pass = gdraw->passes[i];
			int cell_clipped_frustum;

			queued_header->cell_clipped[i] = 0;

			if ((not_frustum_clipped & pass->frustum_flag_test_mask) != pass->frustum_partial_clip_flag)
			{
				if (not_frustum_clipped & pass->frustum_trivial_accept_flag)
					queued_header->cell_clipped[i] = FRUSTUM_CLIP_NONE;
				// trivially accepted or rejected by this frustum, no need to test
				continue;
			}

			INC_FRAME_COUNT(cell_tests);
			if (!(cell_clipped_frustum = frustumCheckSphereWorld(pass->frustum, cell_header->world_mid, cell_header->radius)))
			{
				// remove clipped bit for the current frustum and all dependent frustums
				not_frustum_clipped &= pass->frustum_flag_clear_mask;
				INC_FRAME_COUNT(cell_culls);
				continue;
			}

			INC_FRAME_COUNT(cell_tests);
			if (checkCellBoundsNotVisible(cell_header, pass, cell_clipped_frustum, &queued_header->inherited_bits, gdraw))
			{
				// remove clipped bit for the current frustum and all dependent frustums
				not_frustum_clipped &= pass->frustum_flag_clear_mask;
				INC_FRAME_COUNT(cell_zoculls);
				continue;
			}

			// set clipped bit for the current frustum
			not_frustum_clipped &= ~pass->frustum_flag_test_mask;
			not_frustum_clipped |= (cell_clipped_frustum == FRUSTUM_CLIP_NONE) ? (pass->frustum_trivial_accept_flag) : pass->frustum_partial_clip_flag;
			queued_header->cell_clipped[i] = cell_clipped_frustum;
		}
		if (!not_frustum_clipped)
		{
			// not visible in any frustum
			// skip children
			queued_headers->header_count--; // unqueue this cell
			cell_header += cell_header->skip_offset;
			continue;
		}

#ifdef _FULLDEBUG
		if (cell_header->cell->debug_me)
		{
			gfxDrawBox3D(cell_header->world_min, cell_header->world_max, unitmat, ColorOrange, 1);
			gfxXYprintf(1, 18, "Debug cell visible.");
		}
#endif

		if (cell_header->is_open || gfx_state.cluster_load)
		{
			if (cell_header->skip_offset > 1 && cell_header != root_cell_header)
			{
				// has children, push stack and descend
				cull_stack_top = dynArrayAddStruct_no_memset(cull_stack, cull_stack_count, cull_stack_max);
				cull_stack_top->inherited_bits = queued_header->inherited_bits;
				cull_stack_top->not_frustum_clipped = not_frustum_clipped_init;
				cull_stack_top->root = cell_header;
				cull_stack_top->end = cell_header + cell_header->skip_offset;
			}

			cell_header++;
		}
		else
		{
			// skip children
			cell_header += cell_header->skip_offset;
		}
	}
}

static F32 poly_size = 30.0f;

#define DEBUG_CLIP_PLANES 0

#if DEBUG_CLIP_PLANES
static int debug_show_planes_pass = -1;

static void gfxDisplayPlane(const Vec4H * plane, const Vec4H * near_point, Color color)
{
#if !PLATFORM_CONSOLE
	Vec4H center, a, b, c, d;

	Vec4H scaleVec;
	scaleVec = vecSameConstant4H(poly_size);

	dotVec4H(*near_point, *plane, center);
	vecSplatX(center, center);
	scaleSubVec4H(*near_point, *plane, center, center);

	scaleAddVec4H((*plane), scaleVec, center, a);
	
	gfxDrawLine3D(Vec4HToVec4(center), Vec4HToVec4(a), color);

	vecAnyNormalTo3H(*plane, a);
	vecVecMul4H(a, scaleVec, a);
	crossVec4H(*plane, a, b);

	subVec4H(center, a, c);
	subVec4H(center, b, d);
	addVec4H(center, a, a);
	addVec4H(center, b, b);

	gfxDrawQuad3D(Vec4HToVec4(a), Vec4HToVec4(b), Vec4HToVec4(c), Vec4HToVec4(d), color, 2.0f);
#endif
}
#endif

__forceinline static int gfxAddFrustumPlane(CachedFrustumDataAligned * frustum_aligned, int num_planes, const Vec4H * plane)
{
	copyVec4H(*plane, frustum_aligned->clip_planes[num_planes / 4].v[ num_planes % 4 ]);
	return num_planes + 1;
}

static void gfxSetupCameraPlanes(const Frustum * frustum, int pass_index, CachedFrustumDataAligned * frustum_cache, int debug_showPlanes)
{
	Vec4H planeL, planeR, planeT, planeB, planeN, planeF;
	float normZ;
	int num_planes = frustum_cache->num_planes;
	int first_plane = num_planes;

	mat4toInvTranspose44H(frustum->inv_viewmat, frustum_cache->matrices[pass_index].cammat);

	if (frustum->use_hull)
	{
		int plane, plane_count;
		Vec4 * hull_planes = frustum->hull.planes;
		for ( plane = 0, plane_count = frustum->hull.count; plane < plane_count; ++plane, ++hull_planes )
		{
			frustum_cache->plane_volume_index[num_planes] = pass_index;

			vec3WtoVec4H(hull_planes[0], -hull_planes[0][3], &planeL);

			planeL = mulVec4HMat44H(planeL, &frustum_cache->matrices[pass_index].cammat);
			num_planes = gfxAddFrustumPlane(frustum_cache, num_planes, &planeL);

#if DEBUG_CLIP_PLANES
			if (debug_showPlanes)
			{
				Color c = { plane * 24, 0, 0, 255 };
				gfxDisplayPlane(&planeL, frustum_cache->matrices[pass_index].cammat + 3, c);
			}
#endif
		}
	}
	else
	{
		normZ = 1.0f / sqrtf( 1 + SQR(frustum->htan));
		setVec4H(planeL, normZ, 0.0f, -frustum->htan * normZ, 0.0f);
		setVec4H(planeR, -normZ, 0.0f, -frustum->htan * normZ, 0.0f);

		normZ = 1.0f / sqrtf( 1 + SQR(frustum->vtan));
		setVec4H(planeT, 0.0f, -normZ, -frustum->vtan * normZ, 0.0f);
		setVec4H(planeB, 0.0f, normZ, -frustum->vtan * normZ, 0.0f);

		setVec4H(planeN, 0.0f, 0.0f, -1.0f, frustum->znear);
		setVec4H(planeF, 0.0f, 0.0f, 1.0f, -frustum->zfar);

		planeL = mulVec4HMat44H(planeL, &frustum_cache->matrices[pass_index].cammat);
		planeR = mulVec4HMat44H(planeR, &frustum_cache->matrices[pass_index].cammat);
		planeT = mulVec4HMat44H(planeT, &frustum_cache->matrices[pass_index].cammat);
		planeB = mulVec4HMat44H(planeB, &frustum_cache->matrices[pass_index].cammat);
		planeN = mulVec4HMat44H(planeN, &frustum_cache->matrices[pass_index].cammat);
		planeF = mulVec4HMat44H(planeF, &frustum_cache->matrices[pass_index].cammat);

		num_planes = gfxAddFrustumPlane(frustum_cache, num_planes, &planeL);
		num_planes = gfxAddFrustumPlane(frustum_cache, num_planes, &planeR);
		num_planes = gfxAddFrustumPlane(frustum_cache, num_planes, &planeT);
		num_planes = gfxAddFrustumPlane(frustum_cache, num_planes, &planeB);
		num_planes = gfxAddFrustumPlane(frustum_cache, num_planes, &planeN);
		num_planes = gfxAddFrustumPlane(frustum_cache, num_planes, &planeF);

#if DEBUG_CLIP_PLANES
		if (debug_showPlanes)
		{
			Color c = { 255, 0, 0, 255 };
			gfxDisplayPlane(&planeL, frustum_cache->matrices[pass_index].cammat + 3, c);
			c.r /= 2;
			gfxDisplayPlane(&planeR, frustum_cache->matrices[pass_index].cammat + 3, c);

			c.r = 0;
			c.g = 255;
			gfxDisplayPlane(&planeT, frustum_cache->matrices[pass_index].cammat + 3, c);
			c.g /= 2;
			gfxDisplayPlane(&planeB, frustum_cache->matrices[pass_index].cammat + 3, c);

			c.g = 0;
			c.b = 255;
			gfxDisplayPlane(&planeN, frustum_cache->matrices[pass_index].cammat + 3, c);
			c.b /= 2;
			gfxDisplayPlane(&planeF, frustum_cache->matrices[pass_index].cammat + 3, c);
		}
#endif
	}
	frustum_cache->num_planes = num_planes;

	mat4to44H(frustum->cammat, frustum_cache->matrices[pass_index].cammat);
	mat4to44H(frustum->viewmat, frustum_cache->matrices[pass_index].viewmat);
}

__forceinline static int gfxFrustumClipPlaneCount(const Frustum *frustum)
{
	return frustum->use_hull ? frustum->hull.count : 6;
}

static int gfxGlobalDrawCountClipPlanes(CachedFrustumDataAligned * frustum_cache, const GfxGlobalDrawParams * gdraw)
{
	int pass_index;
	int total_planes = 0;
	for (pass_index = 0; pass_index < gdraw->pass_count; ++pass_index)
	{
		int plane_count = gfxFrustumClipPlaneCount(gdraw->passes[pass_index]->frustum);
		frustum_cache->plane_counts[pass_index] = plane_count;
		total_planes += plane_count;
	}
	return total_planes;
}

static void gfxCacheFrustumDataAligned(GfxGlobalDrawParams * gdraw)
{
	int pass_index;
	int clip_plane_count;

	CachedFrustumDataAligned * frustum_cache = calloc(sizeof(CachedFrustumDataAligned), 1);
	gdraw->frustum_data_aligned = frustum_cache;
	frustum_cache->plane_counts = aligned_calloc(sizeof(int) * gdraw->pass_count, 1, VEC_ALIGNMENT);

	clip_plane_count = gfxGlobalDrawCountClipPlanes(frustum_cache, gdraw);
	assert(clip_plane_count < MAX_CLIP_PLANES);

	frustum_cache->matrices = aligned_calloc(sizeof(CamViewMat) * gdraw->pass_count + 
		sizeof(Vec4PH) * RoundUpToGranularity(clip_plane_count, 16) / 4, 1, VEC_ALIGNMENT);
	frustum_cache->clip_planes = (Vec4PH*)(frustum_cache->matrices + gdraw->pass_count);

	for ( pass_index = 0; pass_index < gdraw->pass_count; ++pass_index )
	{
		gfxSetupCameraPlanes(gdraw->passes[pass_index]->frustum, pass_index, 
			frustum_cache, 
#if DEBUG_CLIP_PLANES
			pass_index == debug_show_planes_pass
#else
			0
#endif
			);
	}

	// fill in remaining planes with zero vector
	while (clip_plane_count % 4)
	{
		frustum_cache->plane_volume_index[clip_plane_count] = pass_index - 1;
		copyVec4H(g_vec4ZeroH, frustum_cache->clip_planes[clip_plane_count / 4].v[clip_plane_count % 4]);
		++clip_plane_count;
	}

	// convert AoS to SoA
	clip_plane_count /= 4;
	for ( pass_index = 0; pass_index < clip_plane_count; ++pass_index )
		transposeVec44PH(frustum_cache->clip_planes + pass_index);
}

static void gfxUncacheFrustumDataAligned(GfxGlobalDrawParams * gdraw)
{
	if (gdraw->frustum_data_aligned)
	{
		CachedFrustumDataAligned * frustum_data = gdraw->frustum_data_aligned;
		gdraw->frustum_data_aligned = NULL;
		free(frustum_data->matrices);
		free(frustum_data->plane_counts);
		free(frustum_data);
	}
}

int frustumCheckBoxWorldH(const CachedFrustumDataAligned * frustum, const Vec3 min_max_unaligned,
	const Mat4 local_to_world, const U8 * clip_inherit, U8 * clip_results)
{
	Vec4H vec_zero, accum_accept_word, accum_reject_word, accum_reject_half, accum_accept_half;
	Vec4H reject[ 16 ], accept[ 16 ];
	Vec4H min, max;
	Vec4H min_xxxx, max_xxxx;
	Vec4H min_yyyy, max_yyyy;
	Vec4H min_zzzz, max_zzzz;
	Vec4H p000, p001, p010, p011, p100, p101, p110, p111;
	Mat44H mat;

	int plane_set, plane_set_count, clip_code_set;
	U8 any_frustum_visible = 0;

	vec3W1toVec4H(min_max_unaligned + 0, &min);
	vec3W1toVec4H(min_max_unaligned + 3, &max);

	vecZero4H(vec_zero);
	mat4to44H(local_to_world, mat);

	// splats
	vecSplatX(min, min_xxxx);
	vecSplatX(max, max_xxxx);
	vecSplatY(min, min_yyyy);
	vecSplatY(max, max_yyyy);
	vecSplatZ(min, min_zzzz);
	vecSplatZ(max, max_zzzz);

	// clip test all eight bbox vertices vs four of the clip planes
	p000 = mulSplattedVec3HMat44H(min_xxxx, min_yyyy, min_zzzz, &mat);
	p001 = mulSplattedVec3HMat44H(max_xxxx, min_yyyy, min_zzzz, &mat);
	p010 = mulSplattedVec3HMat44H(min_xxxx, max_yyyy, min_zzzz, &mat);
	p011 = mulSplattedVec3HMat44H(max_xxxx, max_yyyy, min_zzzz, &mat);

	p100 = mulSplattedVec3HMat44H(min_xxxx, min_yyyy, max_zzzz, &mat);
	p101 = mulSplattedVec3HMat44H(max_xxxx, min_yyyy, max_zzzz, &mat);
	p110 = mulSplattedVec3HMat44H(min_xxxx, max_yyyy, max_zzzz, &mat);
	p111 = mulSplattedVec3HMat44H(max_xxxx, max_yyyy, max_zzzz, &mat);

	plane_set_count = ( frustum->num_planes + 3 ) / 4;
	for (plane_set = 0, clip_code_set = 0; plane_set < plane_set_count; ++plane_set)
	{
		Vec4H clip_accept, clip_reject;

		vecCmpEq4H(vec_zero, vec_zero, clip_reject);
		copyVec4H(vec_zero, clip_accept);

		if (clip_inherit[frustum->plane_volume_index[plane_set*4+0]] |
			clip_inherit[frustum->plane_volume_index[plane_set*4+1]] |
			clip_inherit[frustum->plane_volume_index[plane_set*4+2]] |
			clip_inherit[frustum->plane_volume_index[plane_set*4+3]] )
		{
			// intermediate products
			Vec4H d000, d001, d010, d011, d100, d101, d110, d111;

			INC_FRAME_COUNT(entry_tests);

			// distances for bounding box vertices
			d000 = vec4PlaneDistancePH(&p000, frustum->clip_planes + plane_set);
			d001 = vec4PlaneDistancePH(&p001, frustum->clip_planes + plane_set);
			d010 = vec4PlaneDistancePH(&p010, frustum->clip_planes + plane_set);
			d011 = vec4PlaneDistancePH(&p011, frustum->clip_planes + plane_set);

			d100 = vec4PlaneDistancePH(&p100, frustum->clip_planes + plane_set);
			d101 = vec4PlaneDistancePH(&p101, frustum->clip_planes + plane_set);
			d110 = vec4PlaneDistancePH(&p110, frustum->clip_planes + plane_set);
			d111 = vec4PlaneDistancePH(&p111, frustum->clip_planes + plane_set);

			// check if vertices are clipped against each of the four planes
			d000 = vecCmpLessEqExp4H(d000, vec_zero);
			d001 = vecCmpLessEqExp4H(d001, vec_zero);
			d010 = vecCmpLessEqExp4H(d010, vec_zero);
			d011 = vecCmpLessEqExp4H(d011, vec_zero);

			d100 = vecCmpLessEqExp4H(d100, vec_zero);
			d101 = vecCmpLessEqExp4H(d101, vec_zero);
			d110 = vecCmpLessEqExp4H(d110, vec_zero);
			d111 = vecCmpLessEqExp4H(d111, vec_zero);

			// combine clip flags to get trivial reject/accept
			vecVecAnd4H(
				vecAndExp4H(vecAndExp4H(d000, d001), vecAndExp4H(d010, d011)),
				vecAndExp4H(vecAndExp4H(d100, d101), vecAndExp4H(d110, d111)), clip_reject);
			vecVecOr4H(
				vecOrExp4H(vecOrExp4H(d000, d001), vecOrExp4H(d010, d011)),
				vecOrExp4H(vecOrExp4H(d100, d101), vecOrExp4H(d110, d111)), clip_accept);
		}

		// clip_reject contains four planes worth of clip tests. 
		// if an element is non-zero the box is trivially rejected: all vertices of the box are clipped vs that plane

		// clip_accept contains four planes worth of clip tests.
		// if an element is zero the box is trivially accepted: no vertices of the box are clipped vs that plane

		// cyclically accumulate and pack 32-bit 0/~0 flags into byte flags
		if (plane_set & 1)
		{
			if (plane_set & 2)
			{
				// pack into bytes and store
				vecPackS32S16H(accum_reject_word, clip_reject, accum_reject_word);
				vecPackS32S16H(accum_accept_word, clip_accept, accum_accept_word);

				vecPackS16S8H(accum_reject_half, accum_reject_word, reject[clip_code_set]);
				vecPackS16S8H(accum_accept_half, accum_accept_word, accept[clip_code_set]);
				++clip_code_set;
			}
			else
			{
				vecPackS32S16H(accum_reject_word, clip_reject, accum_reject_half);
				vecPackS32S16H(accum_accept_word, clip_accept, accum_accept_half);
			}
		}
		else
		{
			accum_reject_word = clip_reject;
			accum_accept_word = clip_accept;
		}
	}

	if (plane_set & 3)
	{
		// finish packing
		if (plane_set & 1)
		{
			if (plane_set & 2)
			{
				// we already have a vector of half word flags, so pack into the upper qword
				vecPackS32S16H(accum_reject_word, vec_zero, accum_reject_word);
				vecPackS32S16H(accum_accept_word, vec_zero, accum_accept_word);
			}
			else
			{
				// accum_reject_word is all we have, so overwrite the packed half word flags
				vecPackS32S16H(accum_reject_word, vec_zero, accum_reject_half);
				vecPackS32S16H(accum_accept_word, vec_zero, accum_accept_half);

				copyVec4H(vec_zero, accum_reject_word);
				copyVec4H(vec_zero, accum_accept_word);
			}
		}
		else
		if (plane_set & 2)
		{
			// there is just the vector of half word flags
			copyVec4H(vec_zero, accum_reject_word);
			copyVec4H(vec_zero, accum_accept_word);
		}

		// pack into bytes and store
		vecPackS16S8H(accum_reject_half, accum_reject_word, reject[clip_code_set]);
		vecPackS16S8H(accum_accept_half, accum_accept_word, accept[clip_code_set]);
		++clip_code_set;
	}
#if !PLATFORM_CONSOLE
	_mm_empty(); // After all vecPack* calls, before any floating point math
#endif

	{
		const U8 * reject_planes = (const U8*)&reject[0];
		const U8 * accept_planes = (const U8*)&accept[0];

		// combine per-plane clip results into trivial reject, trivial accept for each clip volume
		plane_set_count = frustum->num_planes;
		for (plane_set = 0; plane_set_count; ++plane_set)
		{
			int plane, plane_count = frustum->plane_counts[plane_set];
			U8 anyReject = 0, allAccept = ~0, clipPartialNear = 0;

			if (plane_count == 6)
				clipPartialNear = accept_planes[4] & FRUSTUM_CLIP_NEAR;

			for (plane = 0; plane < plane_count; ++plane, ++reject_planes, ++accept_planes)
			{
				allAccept &= ~accept_planes[0];
				anyReject |= reject_planes[0];
			}


			// anyReject ? 0 : ( allAccept ? FRUSTUM_CLIP_NONE : FRUSTUM_CLIP_SPHERE_PARTIAL );
			clip_results[plane_set] = (~anyReject) & (
				( allAccept & FRUSTUM_CLIP_NONE ) | ( ~allAccept & FRUSTUM_CLIP_SPHERE_PARTIAL ) | clipPartialNear );
			any_frustum_visible |= ~anyReject;

			plane_set_count -= plane_count;
		}
	}

	return any_frustum_visible;
}

int useCachedAlignedFrustum = 0;
AUTO_CMD_INT(useCachedAlignedFrustum,useCachedAlignedFrustum) ACMD_CATEGORY(Debug);

__forceinline static float square(float x)
{
	return x * x;
}

__forceinline static float distanceSVec3Vec3Squared(const S16 a[3], const Vec3 b)
{
	return square(a[0] - b[0]) + square(a[1] - b[1]) + square(a[2] - b[2]);
}


static void drawEntries(WorldCellQueuedHeader *queued_header, void *entry_header_array, bool hide_map_snap_hidden, GfxGlobalDrawParams *gdraw)
{
	WorldCellEntryCullHeader *entry_headers = CULL_HEADER_START(entry_header_array);
	int i, j, entry_count = CULL_HEADER_COUNT(entry_header_array);
	U8 entry_clipped[MAX_RENDER_PASSES];
	F32 dist_sqr;

	// make a 44 for each of the frustra
	Mat44H * pPassMatrices = AlignPointerUpPow2(_alloca(sizeof(Mat44H)*gdraw->pass_count+0x10),16);
	for (i = 0; i < gdraw->pass_count; ++i)
	{
		RdrDrawListPassData *pass = gdraw->passes[i];
		mat4to44H(pass->frustum->viewmat,pPassMatrices[i]);
	}

	for (j = 0; j < entry_count; ++j)
	{
		WorldCellEntryCullHeader *entry_hdr = &entry_headers[j];
		WorldDrawableEntry *entry;
		int entry_frustum_visible;
		U8 inherited_bits;
		F32 entry_screen_space;
		F32 entry_radius;
		Vec3 min_max[ 2 ], world_mid;

		INC_FRAME_COUNT(entry_hits);

#ifdef _FULLDEBUG
		entry = ENTRY_HEADER_ENTRY_PTR(entry_hdr);
		if (entry->debug_me)
			gfxXYprintf(1, 6, "Debug drawable hit.");
#endif

		if (!ENTRY_HEADER_IS_ACTIVE(entry_hdr))
			continue;

		if (ENTRY_HEADER_LAST_VISIBLE(entry_hdr))
			PREFETCH(ENTRY_HEADER_ENTRY_PTR(entry_hdr));

#if PLATFORM_CONSOLE
		// prefetch 4 cull entries ahead since an entry_hdr is 32-bytes
		if (!(j % 4) && j+4 < entry_count)
			PREFETCH(entry_hdr+4);
#else
		// prefetch the next cull entry
		PREFETCH(entry_hdr+1);
#endif

		dist_sqr = distanceSVec3Vec3Squared(entry_hdr->world_fade_mid, gdraw->cam_pos);
		entry_hdr->entry_and_flags &= ~ENTRY_CULL_LAST_VISIBLE_MASK;

		if (dist_sqr > SQR((F32)entry_hdr->scaled_far_vis_dist)) // scaled_far_vis_dist = lod_scale * far_lod_far_dist + fade_radius
		{
			INC_FRAME_COUNT(entry_dist_culls);
			continue;
		}

		if (entry_hdr->scaled_near_vis_dist && dist_sqr < SQR((F32)entry_hdr->scaled_near_vis_dist)) // scaled_near_vis_dist = lod_scale * near_lod_near_dist + fade_radius
		{
			INC_FRAME_COUNT(entry_dist_culls);
			continue;
		}

		// Delay dereferencing the entry until after this point.
		entry = ENTRY_HEADER_ENTRY_PTR(entry_hdr);

		copyVec3(entry_hdr->world_mid, world_mid);
		copyVec3(entry_hdr->world_corner, min_max[1]);
		entry_radius = lengthVec3(min_max[1]);

		if (gdraw->clip_by_distance &&
			(!gdraw->clip_only_below_camera || world_mid[1] + entry_radius < gdraw->cam_pos[1]) && 
			dist_sqr > SQR(gdraw->clip_distance + entry->world_fade_radius) &&
			!SAFE_MEMBER(entry->draw_list, no_fog))
		{
			INC_FRAME_COUNT(entry_fog_culls);
			continue;
		}

		if (gfx_state.cluster_load && entry_hdr->leaf_cell && entry_hdr->leaf_cell->cell_state != WCS_OPEN)
			continue;

		if (gdraw->hidden_object_id && entry->base_entry.bounds.object_tag == gdraw->hidden_object_id)
			continue;

		if (hide_map_snap_hidden && entry->map_snap_hidden)
			continue;

		inherited_bits = queued_header->inherited_bits;
		entry_frustum_visible = gdraw->all_frustum_bits;
		entry_screen_space = 0;

		if (gdraw->shadow_frustum_bits && gfxIsThinShadowCaster(entry->base_entry.shared_bounds->local_min, entry->base_entry.shared_bounds->local_max)) {
			entry_frustum_visible &= ~gdraw->shadow_frustum_bits;
		}

		if (useCachedAlignedFrustum)
		{
			subVec3(world_mid, min_max[1], min_max[0]);
			addVec3(world_mid, min_max[1], min_max[1]);

			frustumCheckBoxWorldH(gdraw->frustum_data_aligned,
				min_max[0],
				unitmat,
				queued_header->cell_clipped,
				entry_clipped);

			for (i = 0; i < gdraw->pass_count; ++i)
			{
				RdrDrawListPassData *pass = gdraw->passes[i];

				if ((entry_frustum_visible & pass->frustum_set_bit) == 0) {
					continue;
				}

				if (!entry_clipped[i] || pass->shadow_light && (entry->no_shadow_cast || entry->editor_only))
				{
					INC_FRAME_COUNT(entry_culls);
					entry_clipped[i] = 0;
					entry_frustum_visible &= pass->frustum_clear_bits;
					continue;
				}

				if (i == 0 && checkEntryBoundsNotVisibleSimple(entry, pass, entry_clipped[0], min_max, queued_header->inherited_bits, &entry_screen_space, gdraw))
				{
					INC_FRAME_COUNT(entry_zoculls);
					entry_clipped[i] = 0;
					entry_frustum_visible &= pass->frustum_clear_bits;
					continue;
				}
			}
		}
		else
		for (i = 0; i < gdraw->pass_count; ++i)
		{
			RdrDrawListPassData *pass = gdraw->passes[i];
			entry_clipped[i] = queued_header->cell_clipped[i];

			if ((entry_frustum_visible & pass->frustum_set_bit) == 0) {
				continue;
			}

			if (!entry_clipped[i] || pass->shadow_light && (entry->no_shadow_cast || entry->editor_only))
			{
				entry_clipped[i] = 0;
				entry_frustum_visible &= pass->frustum_clear_bits;
				continue;
			}

			if (entry_clipped[i] != FRUSTUM_CLIP_NONE)
			{
				INC_FRAME_COUNT(entry_tests);
				if (!(entry_clipped[i] = frustumCheckSphereWorldFast(pass->frustum, &pPassMatrices[i], world_mid, entry_radius)))
				{
					INC_FRAME_COUNT(entry_culls);
					entry_frustum_visible &= pass->frustum_clear_bits;
					continue;
				}
			}

			INC_FRAME_COUNT(entry_tests);
			if (checkEntryBoundsNotVisible(entry, pass, entry_clipped[i], queued_header->inherited_bits, &entry_screen_space, gdraw))
			{
				INC_FRAME_COUNT(entry_zoculls);
				entry_clipped[i] = 0;
				entry_frustum_visible &= pass->frustum_clear_bits;
				continue;
			}
		}
#if TRACK_FRUSTUM_VISIBILITY_HISTOGRAM
		if (entry_frustum_visible & 0xf)
			INC_FRAME_COUNT(cell_frustum_overlap_hist[entry_frustum_visible & 0xf]);
#endif

		if (entry_frustum_visible)
		{
			int per_pass_partial_clip = 0;
			if (entry->base_entry.type == WCENT_MODELINSTANCED)
			{
				int partial_clip_bit = 1;
				for (i = 0; i < gdraw->pass_count; ++i, partial_clip_bit <<= 1)
				{
					if (entry_clipped[i] != FRUSTUM_CLIP_NONE)
						per_pass_partial_clip |= partial_clip_bit;
				}
			}

			entry_hdr->entry_and_flags |= ENTRY_CULL_LAST_VISIBLE_MASK;

#ifdef _FULLDEBUG
			if (entry->debug_me)
			{
				gfxDrawBox3D(entry->base_entry.shared_bounds->local_min, entry->base_entry.shared_bounds->local_max, 
					entry->base_entry.bounds.world_matrix, ColorPurple, 1);
				gfxXYprintf(1, 7, "Debug drawable drawn (0x%x).", entry_frustum_visible);
			}
#endif

			switch (entry->base_entry.type)
			{
				xcase WCENT_MODEL:
					drawModelEntry((WorldModelEntry *)entry, dist_sqr, entry_frustum_visible, entry_screen_space);
				xcase WCENT_MODELINSTANCED:
					drawModelInstanceEntry((WorldModelInstanceEntry *)entry, dist_sqr, entry_frustum_visible, entry_screen_space, per_pass_partial_clip);
				xcase WCENT_SPLINE:
					drawSplinedModelEntry((WorldSplinedModelEntry *)entry, dist_sqr, entry_frustum_visible, entry_screen_space);
				xcase WCENT_OCCLUSION:
					drawOcclusionEntry((WorldOcclusionEntry *)entry, dist_sqr, entry_frustum_visible, entry_screen_space);
			}
		}
	}
}

void gfxWorldCellClusterModelSetupTextureSwaps(WorldCellGraphicsData *gfx_data, const char* hogFileName, const char* cluster_prefix)
{
	char texFileName[MAX_PATH];
	char texFullName[MAX_PATH];
	HogFile *hogHandle = hogFileRead(hogFileName, NULL, PIGERR_ASSERT, NULL, HOG_DEFAULT);
	// Setup texture swaps here
	{
		sprintf(texFileName,"%s_D.wtex", cluster_prefix);
		sprintf(texFullName,"#%s#%s",hogFileName,texFileName);
		eaPush(&gfx_data->cluster_tex_swaps,white_tex);
		eaPush(&gfx_data->cluster_tex_swaps,texRegisterDynamic(texFullName));
	}
	{
		sprintf(texFileName,"%s_S.wtex", cluster_prefix);
		if (hogFileFind(hogHandle,texFileName) != HOG_INVALID_INDEX) {
			sprintf(texFullName,"#%s#%s",hogFileName,texFileName);
			eaPush(&gfx_data->cluster_tex_swaps,black_tex);
			eaPush(&gfx_data->cluster_tex_swaps,texRegisterDynamic(texFullName));
		}
	}
	{
		sprintf(texFileName,"%s_N.wtex", cluster_prefix);
		if (hogFileFind(hogHandle,texFileName) != HOG_INVALID_INDEX) {
			sprintf(texFullName,"#%s#%s",hogFileName,texFileName);
			eaPush(&gfx_data->cluster_tex_swaps,texFindAndFlag( "Default_Flatnormal_Nx", false, WL_FOR_WORLD ));
			eaPush(&gfx_data->cluster_tex_swaps,texRegisterDynamic(texFullName));
		}
	}
	hogFileDestroy(hogHandle,true);
}

bool gfxCheckClusterLoaded(WorldCell *cell, bool startLoad) {

	int i;
	WorldCellGraphicsData *pWorldCellGfxData;
	bool ret = true;

	if(!cell || !cell->region || !gfx_state.cluster_load) {
		if (cell->cluster_hogg)
			worldCellCloseClusterBins(cell);
		return false;
	}

	if (!cell->cluster_hogg && !cell->vis_dist_level)
		worldCellQueueClusterModelLoad(cell);

	if(!cell->draw_model_cluster || !gfx_state.currentAction) {
		// Don't even have the model on the cell. Can't do anything else without that.
		// The cluster loading will be queued by the loading of the cell bins.
		// Or, no action so we can't do anything.  in the case of a UI Modal dialog, for example.
		return false;
	}

	pWorldCellGfxData = (WorldCellGraphicsData*)cell->drawable.gfx_data;

	if(!pWorldCellGfxData) {
		return false;
	}

	if (!pWorldCellGfxData->cluster_tex_swaps)
	{
		char cellClusterName[MAX_PATH];
		char cellClusterHoggPath[MAX_PATH];

		worldCellGetClusterName(SAFESTR(cellClusterName), cell);
		worldCellGetClusterBinHoggPath(SAFESTR(cellClusterHoggPath), cell);

		gfxWorldCellClusterModelSetupTextureSwaps(pWorldCellGfxData, cellClusterHoggPath, cellClusterName);
	}

	if(startLoad) {
		for(i = 1; i < eaSize(&pWorldCellGfxData->cluster_tex_swaps); i += 2) {
			F32 fTexDist = 0.0f;
			F32 uv_density = TEX_DEMAND_LOAD_DEFAULT_UV_DENSITY;
			if (cell->draw_model_cluster)
			{
				fTexDist = distance3(cell->draw_model_cluster->mid, gfx_state.currentAction->gdraw.cam_pos) - cell->draw_model_cluster->radius;
				uv_density = cell->draw_model_cluster->model_lods[0]->uv_density;
			}
			texDemandLoad(pWorldCellGfxData->cluster_tex_swaps[i], fTexDist, uv_density, NULL);
		}
	}

	for(i = 1; i < eaSize(&pWorldCellGfxData->cluster_tex_swaps); i += 2) {
		if(!texIsFullyLoadedInline(pWorldCellGfxData->cluster_tex_swaps[i])) {
			// Some texture isn't loaded.
			ret = false;
		}
	}

	if(cell->draw_model_cluster) {

		ModelToDraw cluster_models[NUM_MODELTODRAWS];
		if(!gfxDemandLoadModel(cell->draw_model_cluster, cluster_models, ARRAY_SIZE(cluster_models), 0, 1, 0, NULL, 0)) {
			// Model isn't loaded.
			ret = false;
		}
	}

	return ret;
}

void drawClusterModel(const WorldCell * cell, Model * model, int entry_frustum_visible)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	ModelToDraw cluster_models[NUM_MODELTODRAWS];
	RdrDrawableGeo *geo_draw;
	RdrAddInstanceParams instance_params={0};
	RdrLightParams local_light_params={0};
	int i;
	Material * material = model->model_lods[0]->noTangentSpace ? materialFind("Cluster_Default_Nonormal_Nospec", WL_FOR_WORLD) : materialFind("Cluster_Default", WL_FOR_WORLD);
	float fTexDist = distance3(model->mid, gdraw->cam_pos) - model->radius;

	if (!gfxDemandLoadModel(model, cluster_models, ARRAY_SIZE(cluster_models), 0, 1, 0, NULL, 0))
		return;

	geo_draw = rdrDrawListAllocGeo(gdraw->draw_list, RTYPE_MODEL, cluster_models[0].model, 0, 0, 0);

	if (geo_draw)
	{
		WorldCellGraphicsData *pWorldCellGfxData = (WorldCellGraphicsData*)cell->drawable.gfx_data;
		geo_draw->geo_handle_primary = cluster_models[0].geo_handle_primary;

		instance_params.per_drawable_data = _alloca(geo_draw->subobject_count * sizeof(instance_params.per_drawable_data[0]));

		RDRALLOC_SUBOBJECTS(gdraw->draw_list, instance_params, cluster_models[0].model, i);


		for (i = 0; i < geo_draw->subobject_count; ++i)
		{
			gfxDemandLoadMaterialAtQueueTimeEx(instance_params.subobjects[i], material, 
				pWorldCellGfxData->cluster_tex_swaps, 
				NULL, NULL, NULL, false, instance_params.per_drawable_data[i].instance_param, fTexDist, cluster_models[0].model->uv_density);
		}

		setVec4same(instance_params.instance.color, 1);
		setVec3same(instance_params.ambient_multiplier, 1);

		copyMat3(unitmat, instance_params.instance.world_matrix);
		worldCellGetBlockRangeCenter(cell, instance_params.instance.world_matrix[3]);


		instance_params.distance_offset = 1000.f;
		instance_params.frustum_visible = entry_frustum_visible;
		instance_params.wireframe = gdraw->global_wireframe ? gdraw->global_wireframe : debug_cluster_wireframe;

		if (entry_frustum_visible & gdraw->visual_frustum_bit)
		{
			F32 model_radius = cluster_models[0].model->model_parent->radius;
			if (!pWorldCellGfxData->proxy_entry.base_drawable_entry.light_cache)
			{
				GfxStaticObjLightCache *tempLightCache;
				pWorldCellGfxData->shared_bounds.model = cell->draw_model_cluster;
				copyVec3(cell->draw_model_cluster->min, pWorldCellGfxData->shared_bounds.local_min);
				copyVec3(cell->draw_model_cluster->max, pWorldCellGfxData->shared_bounds.local_max);
				pWorldCellGfxData->shared_bounds.radius = cell->draw_model_cluster->radius;
				pWorldCellGfxData->proxy_entry.base_drawable_entry.base_entry.shared_bounds = &pWorldCellGfxData->shared_bounds;
				pWorldCellGfxData->proxy_entry.base_drawable_entry.base_entry.type = WCENT_MODEL;
				tempLightCache = pWorldCellGfxData->proxy_entry.base_drawable_entry.light_cache = pWorldCellGfxData->light_cache = gfxCreateStaticLightCache(&pWorldCellGfxData->proxy_entry.base_drawable_entry, cell->region);

				{
					int j;
 					const U8 *colors = modelGetColors(model->model_lods[0]);
					int vertColorCount = model->model_lods[0]->vert_count;
					VertexLightData *vertLightData = calloc(1,sizeof(VertexLightData));
					eaSetSize(&tempLightCache->lod_vertex_light_data, 1);
					tempLightCache->lod_vertex_light_data[0] = vertLightData;
					gfxCreateVertexLightGeoRenderInfo(&vertLightData->geo_render_info, tempLightCache, 0);
					vertLightData->geo_render_info->vert_count = vertColorCount;
					vertLightData->geo_render_info->is_auxiliary = true;
					vertLightData->rdr_vertex_light.vlight_multiplier = WORLD_CELL_GRAPHICS_VERTEX_LIGHT_MULTIPLIER;
					vertLightData->rdr_vertex_light.vlight_offset = 0;
					ea32SetSize(&vertLightData->vertex_colors, vertColorCount);
					for (j = 0; j < vertColorCount; j++) {
						vertLightData->vertex_colors[j] = colors[j*4] | (((int)colors[j*4+1])<<8) | (((int)colors[j*4+2])<<16);
					}
					// fill_callback fills the vertex buffer with the data contained in vertex_colors
					vertLightData->geo_render_info->fill_callback(vertLightData->geo_render_info, tempLightCache, 0);
					vertLightData->rdr_vertex_light.ignoreVertColor = true;
				}

			}
			fillLightInfo(
				&instance_params, model_radius, cluster_models[0].lod_index,
				&pWorldCellGfxData->proxy_entry.base_drawable_entry, &local_light_params);
		}

		rdrDrawListAddGeoInstance(gdraw->draw_list, geo_draw, &instance_params, RST_AUTO, ROC_WORLD, true);
		gfxGeoIncrementUsedCount(cluster_models[0].model->geo_render_info, cluster_models[0].model->geo_render_info->subobject_count, true);
	}

	// occluder check
	if ((entry_frustum_visible & gdraw->visual_frustum_bit) && gdraw->occlusion_buffer)
	{
		zoCheckAddOccluderCluster(gdraw->occlusion_buffer, cluster_models[0].model, instance_params.instance.world_matrix, ~0, false);
	}
}

static void drawQueuedCells(WorldCellQueuedHeaderArray *queued_cells, bool draw_high_detail, bool draw_high_fill_detail, bool hide_map_snap_hidden, GfxGlobalDrawParams *gdraw)
{
	int i;
	for (i = 0; i < queued_cells->header_count; ++i)
	{
		WorldCellQueuedHeader *queued_header = &queued_cells->headers[i];
		WorldCellCullHeader *cell_header = queued_header->cell_header;

		if (cell_header->is_open)
		{
			if (debug_cluster_show_nonclusters)
			{
				if (!cell_header->entries_inited)
					initEntryCullHeaders(cell_header, gdraw->lod_scale, draw_high_detail, draw_high_fill_detail);

				PREFETCH(cell_header->entry_headers);
				if (cell_header->entry_headers)
					drawEntries(queued_header, cell_header->entry_headers, hide_map_snap_hidden, gdraw);

				PREFETCH(cell_header->editor_only_entry_headers);
				if (cell_header->editor_only_entry_headers && gdraw->in_editor)
					drawEntries(queued_header, cell_header->editor_only_entry_headers, hide_map_snap_hidden, gdraw);
			}
		}
		else
		{
			if (gfx_state.cluster_load)
				gfxCheckClusterLoaded(cell_header->cell, true);

			if (!cell_header->welded_entries_inited)
				initWeldedEntryCullHeaders(cell_header, gdraw->lod_scale, draw_high_detail, draw_high_fill_detail);

			PREFETCH(cell_header->welded_entry_headers);
			if (cell_header->welded_entry_headers && debug_cluster_show_nonclusters)
				drawEntries(queued_header, cell_header->welded_entry_headers, hide_map_snap_hidden, gdraw);

			if (gfx_state.cluster_load && cell_header->cell->draw_model_cluster && debug_cluster_show_clusters)
			{
				Model * cluster_mesh = cell_header->cell->draw_model_cluster;

				if (gfxCheckClusterLoaded(cell_header->cell, true)) {

					// Cluster model and textures are loaded. Draw that.
					drawClusterModel(cell_header->cell, cluster_mesh, ~0);

				} else {

					// We can't use the actual model and textures for the cluster right now, so we need to find
					// something to work as a stand-in to reduce popping.

					// Draw as many of the children as possible.
					int k;
					for(k = 0; k < 8; k++) {
						if(cell_header->cell->children[k]) {
							if(gfxCheckClusterLoaded(cell_header->cell->children[k], false)) {
								Model * cluster_mesh2 = cell_header->cell->children[k]->draw_model_cluster;
								if(cluster_mesh2) {
									drawClusterModel(cell_header->cell->children[k], cluster_mesh2, ~0);
								}
							}
						}
					}

				}
			}
		}
	}
}

extern void gfxTestBudgetsRender();

void gfxDrawWorldCombined(void)
{
	static WorldCellQueuedHeaderArray queued_cells;
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;

	if (!gdraw->draw_list || !eaSize(&gdraw->regions))
		return;

	PERFINFO_AUTO_START_FUNC_PIX();

	//fill wind parameters
	gdraw->wind_disabled = !dynWindIsWindOn();
	if (!gdraw->wind_disabled)
	{
		gdraw->wind_maxdistance_from_cam = dynWindGetSampleGridExtents();
		gdraw->wind_current_client_loop_timer = gfxGetClientLoopTimer();
	}

	gdraw->disable_vertex_lights = gfx_state.debug.disable_vertex_lights;

	if (useCachedAlignedFrustum)
		gfxCacheFrustumDataAligned(gdraw);
	gfxDrawTerrainCellsCombined();

	PERFINFO_AUTO_START_PIX("Queue world objects", 1);
	etlAddEvent(gfx_state.currentDevice->event_timer, "Queue world objects", ELT_CODE, ELTT_BEGIN);

	if (!gfx_state.debug.no_draw_static_world)
	{
		
		bool draw_high_detail = gfx_state.settings.draw_high_detail;
		bool draw_high_fill_detail = gfx_state.settings.draw_high_fill_detail;
		bool hide_map_snap_hidden = gfxSnapGetHidingMapSnapObscuringObjects();
		int i, j;

		for (i = 0; i < eaSize(&gdraw->regions); ++i)
		{
			WorldRegion *region = gdraw->regions[i];
			U64 not_frustum_clipped_init = 0; // FRUSTUM_CLIP_TYPE
			
			gfxSetCurrentRegion(i); //sets gdraw->use_far_depth_range

			for (j = 0; j < gdraw->pass_count; ++j)
			{
				RdrDrawListPassData *pass = gdraw->passes[j];
				if (pass->shader_mode == RDRSHDM_SHADOW && gdraw->use_far_depth_range)
				{
					//don't set the bit, all show casters should be rejected in this case
				}
				else
				{
					not_frustum_clipped_init |= pass->frustum_partial_clip_flag; // initialize to partially clipped so they will get tested
				}
			}
			
			queued_cells.header_count = 0;

			if (region->temp_world_cell)
			{
				worldCellUpdateBounds(region->temp_world_cell);
				if (!region->temp_world_cell_headers)
					region->temp_world_cell_headers = initWorldCellCullHeaders(region->temp_world_cell);
				cullWorldCells(region->temp_world_cell_headers, not_frustum_clipped_init, &queued_cells);
			}

			if (region->root_world_cell)
			{
				if (!region->world_cell_headers)
					region->world_cell_headers = initWorldCellCullHeaders(region->root_world_cell);
				cullWorldCells(region->world_cell_headers, not_frustum_clipped_init, &queued_cells);
			}

			drawQueuedCells(&queued_cells, draw_high_detail, draw_high_fill_detail, hide_map_snap_hidden, gdraw);
		}
	}

	zoFinishRejectedOccluders(gdraw->occlusion_buffer);

	etlAddEvent(gfx_state.currentDevice->event_timer, "Queue world objects", ELT_CODE, ELTT_END);
	PERFINFO_AUTO_STOP_PIX();

	gfxSetCurrentRegion(0);

	if (!gfx_state.debug.no_draw_dynamics)
		gfxDynamicsQueueAllWorld();

	worldCellEntryClearTempGeoDraws();
	gfxDynDrawModelClearTempMaterials(true);
	dynSortBucketClearCache();
	gfxPrimitiveClearDrawables();
	worldFXClearPostDrawing();

	if (useCachedAlignedFrustum)
		gfxUncacheFrustumDataAligned(gdraw);

	//gfxTestBudgetsRender();

	PERFINFO_AUTO_STOP_FUNC_PIX();
}

//////////////////////////////////////////////////////////////////////////

WorldRegionGraphicsData *gfxAllocWorldRegionGraphicsData(WorldRegion *region, const Vec3 min, const Vec3 max)
{
	WorldRegionGraphicsData *data = calloc(1,sizeof(*data));
	Vec3 center;
	F32 size = 0;

	if(	min[0] <= max[0] &&
		min[1] <= max[1] &&
		min[2] <= max[2])
	{
		size = 0.5f * distance3(min, max);
		size = pow2(round(size+1));
	}

	MAX1F(size, 512);

	centerVec3(min, max, center);

 	data->static_light_octree = octreeCreateEx(center, size, 0);

	data->region = region;

	return data;
}

WorldRegion *gcl_camera_last_region;

void gfxFreeWorldRegionGraphicsData(WorldRegion *region, bool remove_sky_group)
{
	if (!region)
		return;

	if (gcl_camera_last_region == region)
		gcl_camera_last_region = NULL;

	if (region->graphics_data)
	{
		if (region->graphics_data->static_light_octree)
		{
			octreeForEachEntry(region->graphics_data->static_light_octree, (OctreeForEachCallback)gfxClearLightRegionGraphicsData);
			octreeDestroy(region->graphics_data->static_light_octree);
		}

		eaDestroyEx(&region->graphics_data->dir_lights, gfxClearLightRegionGraphicsData);

		SAFE_FREE(region->graphics_data);
	}

	if (remove_sky_group && region->sky_group)
	{
		// remove from all sky datas
		gfxSkyRemoveSkyGroup(NULL, region->sky_group);
	}
}

void gfxSetAmbientLight( RdrAmbientLight* light )
{
	WorldGraphicsData* world_data = worldGetWorldGraphicsData();

		
	if (!light)
		world_data->outdoor_ambient_light = world_data->main_outdoor_ambient_light;
	else
		world_data->outdoor_ambient_light = light;
}

WorldGraphicsData *gfxAllocWorldGraphicsData(void)
{
	LightData sun_light_data = {0};
	WorldGraphicsData *data;

	sun_light_data.light_type = WL_LIGHT_DIRECTIONAL;
	sun_light_data.cast_shadows = 1;
	sun_light_data.is_sun = 1;
	copyMat4(unitmat, sun_light_data.world_mat);

	data = calloc(1,sizeof(*data));
	data->dynamic_lights = sparseGridCreate(32, 512);
	data->main_outdoor_ambient_light = calloc(1, sizeof(*data->outdoor_ambient_light));
	gfxCreateSunLight(&data->sun_light, &sun_light_data, NULL);
	gfxCreateSunLight(&data->sun_light_2, &sun_light_data, NULL);

	data->outdoor_ambient_light = data->main_outdoor_ambient_light;

	return data;
}

void gfxTickSkyData(void)
{
	if (gfx_state.currentDevice)
		gfxSkyUpdate(gfx_state.currentDevice->primaryCameraView, gfxGetTime());
}

// TODO - hold onto various parts of the lighting information
// for inspection in rdrProcessRdrLight crashes
WorldGraphicsData *last_data = NULL;
RdrAmbientLight * last_outdoor_amb_light = NULL;
GfxLight * last_sun_light = NULL;
GfxLight * last_sun_light_2 = NULL;

void gfxFreeWorldGraphicsData(WorldGraphicsData *data)
{
	if (!data)
		return;

	// TODO DJR - store the last sun light pointer to verify it's being freed then read later
	// by the draw list
	last_sun_light = data->sun_light;
	gfxRemoveLight(data->sun_light);
	// TODO DJR - hammer the light type value to trap access in RdrDrawList.c
	if (last_sun_light)
	{
		memlog_printf(NULL, "Last freed sun light 0x%p 0x%p\n", last_sun_light, &last_sun_light->rdr_light);
		last_sun_light->rdr_light.light_type = 0xffaaffaa;
	}
	else
		memlog_printf(NULL, "Last sun light NULL\n");

	// TODO DJR - store the last sun light pointer to verify it's being freed then read later
	// by the draw list
	last_sun_light_2 = data->sun_light_2;
	gfxRemoveLight(data->sun_light_2);
	// TODO DJR - hammer the light type value to trap access in RdrDrawList.c
	if (last_sun_light_2)
	{
		memlog_printf(NULL, "Last freed secondary sun light 0x%p 0x%p\n", last_sun_light_2, &last_sun_light_2->rdr_light);
		last_sun_light_2->rdr_light.light_type = 0xffaaffaa;
	}
	else
		memlog_printf(NULL, "Last secondary sun light NULL\n");

	gfxClearCurrentDeviceShadowmapLightHistory();
	eaDestroyEx(&data->override_lights, gfxFreeGfxLightFromRdrLight);
	eaDestroyEx(&data->override_ambient_lights, NULL);
	sparseGridDestroy(data->dynamic_lights);

	last_outdoor_amb_light = data->main_outdoor_ambient_light;

	// TODO DJR - finally free the prior WGD
	if (last_data)
	{
		free(data->main_outdoor_ambient_light);
		SAFE_FREE(last_data);
	}
	last_data = data;
	// TODO DJR - hold onto this memory to allow later inspection - it will be freed if
	// we make another map transfer, see above. It may leak, and for the moment this is necessary
	// for debugging.
	//free(data->main_outdoor_ambient_light);
	//SAFE_FREE(data);
}


#include "GfxWorld_h_ast.c"
