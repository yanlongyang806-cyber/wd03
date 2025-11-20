#include "GfxModel.h"
#include "GfxModelOpt.h"
#include "GfxGeo.h"
#include "GfxModelInline.h"
#include "GfxOcclusion.h"
#include "wlAutoLOD.h"
#include "WorldGrid.h"
#include "MemRef.h"
#include "GenericMesh.h"
#include "GfxConsole.h"
#include "UnitSpec.h"
#include "ScratchStack.h"

#include "endian.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Geometry_Misc););

U32 geoRecentLoadThreshold;
U32 geoRecentLoadEpsilon;
U32 geoLastSeenTimeThreshold;
F32 geoFadeTimeDivisor=1;

int g_debug_model_fixup;

void gfxDemandLoadModelLOD(ModelLOD *model, GeoHandle *handle_primary, bool increment_used_count)
{
	GeoRenderInfo *geo_render_info;
	*handle_primary = 0;

	if (model->tri_count == 0)
		return;

	if ((geo_render_info = model->geo_render_info) && (geo_render_info->geo_loaded & gfx_state.currentRendererFlag))
	{
		*handle_primary = geo_render_info->geo_handle_primary;
		if (increment_used_count)
			gfxGeoIncrementUsedCount(geo_render_info, geo_render_info->subobject_count, false);
		geo_render_info->geo_last_used_timestamp = gfx_state.client_frame_timestamp; // Flags child/LOD model as being used
	}
	else 
	{
		GeoLoadState model_loadstate = model->loadstate;
		if (modelLODIsLoaded(model))
		{
			if (model_loadstate == GEO_LOADED_NULL_DATA)
			{
				// Nothing to do
			} else {
				gfxFillModelRenderInfo(model);
				assert(model->geo_render_info);
				model->geo_render_info->geo_last_used_timestamp = gfx_state.client_frame_timestamp;
				// Send to video card (may asynchronously generate data)
				if (gfxGeoSetupVertexObject(model->geo_render_info))
				{
					*handle_primary = model->geo_render_info->geo_handle_primary;
				}
			}
		}
		else if (model_loadstate == GEO_LOAD_FAILED)
		{
			// Failed for some reason, draw nothing
		}
		else if (model_loadstate == GEO_LOADING)
		{
			// We're waiting for it to load
		}
		else if (model_loadstate == GEO_LOADED_NULL_DATA || model_loadstate == GEO_LOADED_NEED_INIT)
		{
			// Should only happen for a couple of frames and then get a good ModelLOD *
			printf("");
		}
		else
		{
			assert(model->loadstate == GEO_NOT_LOADED);
			modelLODRequestBackgroundLoad(model);
		}
	}
}

static __forceinline bool modelWasRecentlyLoaded(ModelLOD *model)
{
	return model->geo_render_info && (((U32)(gfx_state.client_frame_timestamp - model->geo_render_info->geo_loaded_timestamp)) < geoRecentLoadThreshold);
}

__forceinline static bool calcAlphaAndMorph(F32 *alpha, F32 *morph, F32 dist, F32 lod_near, F32 lod_far, F32 lod_farmorph, F32 lod_scale, bool is_last_lod)
{
	F32 lod_farfade = is_last_lod ? FADE_DIST_BUFFER * lod_scale : 0;
	lod_near *= lod_scale;
	lod_far *= lod_scale;
	lod_farmorph *= lod_scale;

	*alpha = 1;
	*morph = 0;

	if (dist < lod_near)
	{
		if ((lod_near - dist) >= 0)
			return false;
	}
	else if (dist > lod_far)
	{
		if ((dist - lod_far) >= lod_farfade)
			return false;
		*alpha = 1.f - (dist - lod_far) / lod_farfade;
		if (lod_farmorph > 0)
			*morph = 1;
	}
	else if (dist > lod_far - lod_farmorph)
	{
		*morph = 1.f - (lod_far - dist) / lod_farmorph;
	}

	return true;
}

__forceinline static void calcMorph(F32 *morph, F32 dist, F32 lod_far, F32 lod_farmorph, F32 lod_scale)
{
	if (lod_farmorph <= 0)
	{
		*morph = 0;
		return;
	}

	lod_far *= lod_scale;
	lod_farmorph *= lod_scale;

	if (dist > lod_far)
		*morph = 1;
	else if (dist > lod_far - lod_farmorph)
		*morph = 1.f - (lod_far - dist) / lod_farmorph;
	else
		*morph = 0;
}

__forceinline static bool isVisible(F32 dist, F32 lod_near, F32 lod_far, F32 lod_scale, bool is_last_desired, bool is_last_lod, bool is_clustered)
{
	lod_near *= lod_scale;
	lod_far *= lod_scale;

	if (is_last_lod && is_clustered)
		return true;

	if ((lod_near - dist) > 0)
		return false;

	if ((dist - lod_far) > 0)
		return false;

	return true;
}

int gfxDemandLoadPreSwappedModel(WorldDrawableList *draw_list, 
								 ModelToDraw *models, int models_size, 
								 F32 dist, F32 lod_scale, S32 force_lod_level, 
								 ModelLoadTracker *model_tracker, 
								 F32 radius, bool is_clustered)
{
	int i;
	int model_count=0;

	PERFINFO_AUTO_START_FUNC_L2();

	assert(models_size>=NUM_MODELTODRAWS);
	assert(FINITE(dist));
	assert(FINITE(lod_scale));

	// LODs should be calculated from radius of object
	dist -= radius;
	MAX1F(dist, 0);

	if (gfx_state.debug.model_lod_force >= 0)
		force_lod_level = gfx_state.debug.model_lod_force;

	if (model_tracker)
	{
		int num_lods = draw_list->lod_count;
		int lod_index = -1;
		F32 fade_factor;
		MIN1(model_tracker->fade_in_lod, num_lods-1); // Can happen during LOD Editing

		if (force_lod_level >= 0)
		{
			lod_index = CLAMP(force_lod_level, 0, num_lods-1);
		}
		else
		{
			for (i = 0; i < num_lods; ++i)
			{
				WorldDrawableLod *draw = &draw_list->drawable_lods[i];
				if (!isVisible(dist, draw->near_dist, draw->far_dist, lod_scale, model_tracker->fade_in_lod == i, i == num_lods - 1, is_clustered))
					continue;
				if (i == 0 && draw_list->high_detail_high_lod && !gfx_state.settings.draw_high_detail && num_lods > 1)
					i = 1;
				lod_index = i;
				break;
			}
		}

		if(lod_index>=0)
		{
			lod_index += gfx_state.debug.model_lod_offset;
			lod_index = CLAMP(lod_index, 0, num_lods - 1);
		}

		if (model_tracker->fade_in_lod != lod_index)
		{
			GeoHandle geo_handle_primary = 0;
			ModelLOD *model_lod = NULL;

			if (lod_index >= 0 && draw_list->drawable_lods[lod_index].subobject_count)
				model_lod = modelLoadLOD(draw_list->drawable_lods[lod_index].subobjects[0]->model->model, draw_list->drawable_lods[lod_index].subobjects[0]->model->lod_idx);

			if (model_lod)
				gfxDemandLoadModelLOD(model_lod, &geo_handle_primary, false);

			if (lod_index < 0 || !model_lod || geo_handle_primary)
			{
				bool no_fade_out = false, no_fade_in = false;

				if (model_tracker->fade_in_lod >= 0)
					no_fade_out = draw_list->drawable_lods[model_tracker->fade_in_lod].no_fade;

				if (lod_index >= 0)
					no_fade_in = draw_list->drawable_lods[lod_index].no_fade;

				model_tracker->do_fade_in = !no_fade_in && model_tracker->fade_in_lod < 0; // fade in if we were previously not drawing any LOD
				model_tracker->do_fade_out = !no_fade_out && lod_index < 0; // fade out if we are now not drawing any LOD

				// TODO(CD) modify the fade in and fade out parameters based on the LOD settings ?

				if ((gfx_state.client_frame_timestamp - model_tracker->last_seen_timestamp) > geoLastSeenTimeThreshold)
				{
					// haven't seen it for a while, assume this model just came into view and do not fade
					model_tracker->do_fade_in = false;
					model_tracker->do_fade_out = false;
				}

				model_tracker->last_lod_change_timestamp = gfx_state.client_frame_timestamp;
				model_tracker->fade_out_lod = model_tracker->fade_in_lod;
				model_tracker->fade_in_lod = lod_index;
			}
		}

		// compute alpha
		fade_factor = (gfx_state.client_frame_timestamp - model_tracker->last_lod_change_timestamp) * geoFadeTimeDivisor;

		if (model_tracker->fade_out_lod >= 0 && model_tracker->do_fade_out)
		{
			ModelLOD *model_lod = NULL;

			if (draw_list->drawable_lods[model_tracker->fade_out_lod].subobject_count)
				model_lod = modelGetLOD(draw_list->drawable_lods[model_tracker->fade_out_lod].subobjects[0]->model->model, draw_list->drawable_lods[model_tracker->fade_out_lod].subobjects[0]->model->lod_idx);

			assert(model_count < models_size);
			models[model_count].alpha = saturate(1 + model_tracker->do_fade_in - fade_factor); // if fading in a different LOD, wait until it is fully in before fading this one out
			if (models[model_count].alpha <= 0)
				model_tracker->do_fade_out = 0;
			//if (model_tracker->do_fade_out > 0 && !model_lod) // Need to look up/start to load model/LOD
			//	gfxDemandLoadModelLodInternal(model, lod, model_tracker->fade_out_lod);
			if (model_lod && model_tracker->do_fade_out)
			{
				models[model_count].draw = &draw_list->drawable_lods[model_tracker->fade_out_lod];
				calcMorph(&models[model_count].morph, dist, models[model_count].draw->far_dist, models[model_count].draw->far_morph, lod_scale);

				models[model_count].model = model_lod;
				gfxDemandLoadModelLOD(model_lod, &models[model_count].geo_handle_primary, true);
				models[model_count].lod_index = model_tracker->fade_out_lod;
				if (models[model_count].geo_handle_primary)
					model_count++;
			}
		}

		if (model_tracker->fade_in_lod >= 0)
		{
			ModelLOD *model_lod = NULL;

			if (draw_list->drawable_lods[model_tracker->fade_in_lod].subobject_count)
				model_lod = modelGetLOD(draw_list->drawable_lods[model_tracker->fade_in_lod].subobjects[0]->model->model, draw_list->drawable_lods[model_tracker->fade_in_lod].subobjects[0]->model->lod_idx);

			assert(model_count < models_size);
			models[model_count].alpha = model_tracker->do_fade_in ? saturate(fade_factor) : 1;
			if (models[model_count].alpha >= 1)
				model_tracker->do_fade_in = 0;
			//if (models[model_count].alpha > 0 && !lod->cached_model) // Need to look up/start to load model/LOD
			//	madeAnLOD = gfxDemandLoadModelLodInternal(model, lod, model_tracker->fade_in_lod) || madeAnLOD;
			if (model_lod && models[model_count].alpha > 0)
			{
				models[model_count].draw = &draw_list->drawable_lods[model_tracker->fade_in_lod];
				calcMorph(&models[model_count].morph, dist, models[model_count].draw->far_dist, models[model_count].draw->far_morph, lod_scale);

				models[model_count].model = model_lod;
				gfxDemandLoadModelLOD(model_lod, &models[model_count].geo_handle_primary, true);
				models[model_count].lod_index = model_tracker->fade_in_lod;
				if (models[model_count].geo_handle_primary)
					model_count++;
			}
		}

		model_tracker->last_seen_timestamp = gfx_state.client_frame_timestamp;
	}
	else
	{
		int num_lods = draw_list->lod_count;
		bool needFixup = false;

		if (!num_lods)
			return 0;

		if (force_lod_level >= 0)
		{
			// We're forcing a LOD level rather than calculating it here
			int lod_index;
			ModelLOD *model_lod = NULL;

			lod_index = CLAMP(force_lod_level, 0, num_lods-1);
			if (draw_list->drawable_lods[lod_index].subobject_count)
				model_lod = modelGetLOD(draw_list->drawable_lods[lod_index].subobjects[0]->model->model, draw_list->drawable_lods[lod_index].subobjects[0]->model->lod_idx);
			if (!model_lod)
				return 0;

			models[model_count].draw = &draw_list->drawable_lods[lod_index];
			models[model_count].model = model_lod;
			gfxDemandLoadModelLOD(model_lod, &models[model_count].geo_handle_primary, true);
			models[model_count].alpha = 1.0f;
			models[model_count].morph = 0.0f;
			models[model_count].lod_index = lod_index;
			if (!models[model_count].geo_handle_primary || modelWasRecentlyLoaded(model_lod))
				needFixup = true;
			model_count++;
		}
		else
		{
			for (i = 0; i < num_lods; ++i)
			{
				WorldDrawableLod *draw = &draw_list->drawable_lods[i];
				ModelLOD *model_lod = NULL;

				if (!calcAlphaAndMorph(&models[model_count].alpha, &models[model_count].morph, dist, draw->near_dist, draw->far_dist, draw->far_morph, lod_scale, i == num_lods - 1))
					continue;

				if (i == 0 && draw_list->high_detail_high_lod && !gfx_state.settings.draw_high_detail && num_lods > 1)
				{
					i = 1;
					draw = &draw_list->drawable_lods[i];
				}

				assert(model_count < models_size);
				if (draw->subobject_count)
					model_lod = modelGetLOD(draw->subobjects[0]->model->model, draw->subobjects[0]->model->lod_idx);
				if (!model_lod)
					continue;

				models[model_count].draw = draw;
				models[model_count].model = model_lod;
				gfxDemandLoadModelLOD(model_lod, &models[model_count].geo_handle_primary, true);
				models[model_count].lod_index = i;
				if (!models[model_count].geo_handle_primary || modelWasRecentlyLoaded(model_lod))
					needFixup = true;
				model_count++;
			}
		}

		if (needFixup) {
			// Put logic to do cool stuff with fading LODs when they're loaded here.
			model_count = gfxDemandLoadModelFixup(NULL, models, models_size, model_count, draw_list);
		} else {
			g_debug_model_fixup = 0;
		}
	}

	if (gfx_state.debug.no_fade_in)
		models[0].alpha = 1.0f;

	PERFINFO_AUTO_STOP_L2();

	return model_count;
}

// If data is not loaded, it queues it up loading
// If data is loaded, it sends it to the video card
// Returns the number of models to draw
int gfxDemandLoadModel(Model *model, 
					   ModelToDraw *models, int models_size, 
					   F32 dist, F32 lod_scale, S32 force_lod_level, 
					   ModelLoadTracker *model_tracker, 
					   F32 radius)
{
	int i;
	int model_count=0;
	int lod_count_from_model;

	if (!model)
		return 0;

	PERFINFO_AUTO_START_FUNC_L2();
// Done per-LOD now:
// 	if (model->loadstate != GEO_LOADED)
// 	{
// 		if (model->loadstate == GEO_NOT_LOADED)
// 			modelRequestBackgroundLoad(model);
// 		if (model_tracker)
// 		{
// 			model_tracker->fade_out_lod = -1;
// 			model_tracker->fade_in_lod = -1;
// 		}
// 		return 0;
// 	}

	assert(models_size>=NUM_MODELTODRAWS);
	assert(FINITE(dist));
	assert(FINITE(lod_scale));

	// LODs should be calculated from radius of object
	dist -= radius;
	MAX1F(dist, 0);

	if (gfx_state.debug.model_lod_force >= 0)
		force_lod_level = gfx_state.debug.model_lod_force;

	lod_count_from_model = eaSize(&model->model_lods);
	assert(lod_count_from_model > 0);
	assert(!model->lod_info || eaSize(&model->lod_info->lods) == lod_count_from_model);
	if (!model->lod_info)
	{
		ModelLOD *lod = modelLoadLOD(model, 0);
		assert(lod);
		models[model_count].model = lod;
		gfxDemandLoadModelLOD(lod, &models[model_count].geo_handle_primary, true);
		models[model_count].alpha = 1.f;
		models[model_count].morph = 0;
		models[model_count].draw = NULL;
		models[model_count].lod_index = 0;
		if (models[model_count].geo_handle_primary)
			model_count++;
	}
	else if (model_tracker)
	{
		int num_lods = eaSize(&model->lod_info->lods);
		int lod_index = -1;
		F32 fade_factor;

		MIN1(model_tracker->fade_in_lod, num_lods-1); // Can happen during LOD Editing

		if (force_lod_level >= 0)
		{
			lod_index = CLAMP(force_lod_level, 0, num_lods-1);
		}
		else
		{
			for (i = 0; i < num_lods; ++i)
			{
				F32 lod_near, lod_far;
				lodinfoGetDistances(model, NULL, 1, NULL, i, &lod_near, &lod_far);
				if (!isVisible(dist, lod_near, lod_far, lod_scale, model_tracker->fade_in_lod == i, i == num_lods - 1, false))
					continue;
				if (i == 0 && model->lod_info->high_detail_high_lod && !gfx_state.settings.draw_high_detail && num_lods > 1)
					i = 1;
				lod_index = i;
				break;
			}
		}

		if(lod_index>=0)
		{
			lod_index += gfx_state.debug.model_lod_offset;
			lod_index = CLAMP(lod_index, 0, num_lods - 1);
		}

		if (model_tracker->fade_in_lod != lod_index)
		{
			GeoHandle geo_handle_primary = 0;
			ModelLOD *lod = lod_index < 0 ? NULL : modelLoadLOD(model, lod_index);

			if (lod)
			{
				gfxDemandLoadModelLOD(lod, &geo_handle_primary, false);
			}

			if (lod_index < 0 || !lod || geo_handle_primary)
			{
				bool no_fade_out = false, no_fade_in = false;

				if (model_tracker->fade_in_lod >= 0)
					no_fade_out = model->lod_info->lods[model_tracker->fade_in_lod]->no_fade;

				if (lod_index >= 0)
					no_fade_in = model->lod_info->lods[lod_index]->no_fade;

				model_tracker->do_fade_in = !no_fade_in && model_tracker->fade_in_lod < 0; // fade in if we were previously not drawing any LOD
				model_tracker->do_fade_out = !no_fade_out && lod_index < 0; // fade out if we are now not drawing any LOD

				// TODO(CD) modify the fade in and fade out parameters based on the LOD settings ?

				if ((gfx_state.client_frame_timestamp - model_tracker->last_seen_timestamp) > geoLastSeenTimeThreshold)
				{
					// haven't seen it for a while, assume this model just came into view and do not fade
					model_tracker->do_fade_in = false;
					model_tracker->do_fade_out = false;
				}

				model_tracker->last_lod_change_timestamp = gfx_state.client_frame_timestamp;
				model_tracker->fade_out_lod = model_tracker->fade_in_lod;
				model_tracker->fade_in_lod = lod_index;
			}
		}

		// compute alpha
		fade_factor = (gfx_state.client_frame_timestamp - model_tracker->last_lod_change_timestamp) * geoFadeTimeDivisor;

		if (model_tracker->fade_out_lod >= 0 && model_tracker->do_fade_out)
		{
			AutoLOD *lod = model->lod_info->lods[model_tracker->fade_out_lod];
			ModelLOD *model_lod = modelGetLOD(model, model_tracker->fade_out_lod);
			assert(model_count < models_size);
			models[model_count].alpha = saturate(1 + model_tracker->do_fade_in - fade_factor); // if fading in a different LOD, wait until it is fully in before fading this one out
			if (models[model_count].alpha <= 0)
				model_tracker->do_fade_out = 0;
			//if (model_tracker->do_fade_out > 0 && !model_lod) // Need to look up/start to load model/LOD
			//	gfxDemandLoadModelLodInternal(model, lod, model_tracker->fade_out_lod);
			if (model_lod && model_tracker->do_fade_out)
			{
				F32 lod_near ,lod_far;
				models[model_count].draw = NULL;
				lodinfoGetDistances(model, NULL, 1, NULL, model_tracker->fade_out_lod, &lod_near, &lod_far);
				calcMorph(&models[model_count].morph, dist, lod_far, lod->lod_farmorph, lod_scale);

				models[model_count].model = model_lod;
				gfxDemandLoadModelLOD(model_lod, &models[model_count].geo_handle_primary, true);
				models[model_count].lod_index = model_tracker->fade_out_lod;
				if (models[model_count].geo_handle_primary)
					model_count++;
			}
		}

		if (model_tracker->fade_in_lod >= 0)
		{
			AutoLOD *lod = model->lod_info->lods[model_tracker->fade_in_lod];
			ModelLOD *model_lod = modelGetLOD(model, model_tracker->fade_in_lod);
			assert(model_count < models_size);
			models[model_count].alpha = model_tracker->do_fade_in ? saturate(fade_factor) : 1;
			if (models[model_count].alpha >= 1)
				model_tracker->do_fade_in = 0;
			//if (models[model_count].alpha > 0 && !lod->cached_model) // Need to look up/start to load model/LOD
			//	madeAnLOD = gfxDemandLoadModelLodInternal(model, lod, model_tracker->fade_in_lod) || madeAnLOD;
			if (model_lod && models[model_count].alpha > 0)
			{
				F32 lod_near, lod_far;
				models[model_count].draw = NULL;
				lodinfoGetDistances(model, NULL, 1, NULL, model_tracker->fade_in_lod, &lod_near, &lod_far);
				calcMorph(&models[model_count].morph, dist, lod_far, lod->lod_farmorph, lod_scale);

				models[model_count].model = model_lod;
				gfxDemandLoadModelLOD(model_lod, &models[model_count].geo_handle_primary, true);
				models[model_count].lod_index = model_tracker->fade_in_lod;
				if (models[model_count].geo_handle_primary)
					model_count++;
			}
		}

		model_tracker->last_seen_timestamp = gfx_state.client_frame_timestamp;
	}
	else
	{
		int num_lods = eaSize(&model->lod_info->lods);
		bool needFixup=false;

		if (force_lod_level >= 0)
		{
			// We're forcing a LOD level rather than calculating it here
			int lod_index;
			AutoLOD *lod;
			ModelLOD *model_lod;

			lod_index = CLAMP(force_lod_level, 0, num_lods-1);
			assert(model->lod_info->lods);
			lod = model->lod_info->lods[lod_index];
			model_lod = modelGetLOD(model, lod_index);

			if (!model_lod)
				return 0;
			models[model_count].draw = NULL;
			models[model_count].model = model_lod;
			gfxDemandLoadModelLOD(model_lod, &models[model_count].geo_handle_primary, true);
			models[model_count].alpha = 1.0f;
			models[model_count].morph = 0.0f;
			if (!models[model_count].geo_handle_primary || modelWasRecentlyLoaded(model_lod))
				needFixup=true;
			models[model_count].lod_index = lod_index;
			model_count++;
		}
		else
		{
			for (i=0; i<num_lods; i++)
			{
				AutoLOD *lod = model->lod_info->lods[i];
				ModelLOD *model_lod;
				F32 lod_near, lod_far;

				lodinfoGetDistances(model, NULL, 1, NULL, i, &lod_near, &lod_far);

				if (!calcAlphaAndMorph(&models[model_count].alpha, &models[model_count].morph, dist, lod_near, lod_far, lod->lod_farmorph, lod_scale, i == num_lods - 1))
					continue;

				if (i == 0 && model->lod_info->high_detail_high_lod && !gfx_state.settings.draw_high_detail && num_lods > 1)
				{
					i = 1;
					lod = model->lod_info->lods[i];
				}
				i += gfx_state.debug.model_lod_offset;
				i = CLAMP(i, 0, num_lods - 1);

				assert(model_count < models_size);
				model_lod = modelGetLOD(model, i);
				if (!model_lod)
					continue;
				models[model_count].draw = NULL;
				models[model_count].model = model_lod;
				gfxDemandLoadModelLOD(model_lod, &models[model_count].geo_handle_primary, true);
				if (!models[model_count].geo_handle_primary || modelWasRecentlyLoaded(model_lod))
					needFixup=true;
				models[model_count].lod_index = i;
				model_count++;
			}
		}

		if (needFixup) {
			// Put logic to do cool stuff with fading LODs when they're loaded here.
			model_count = gfxDemandLoadModelFixup(model, models, models_size, model_count, NULL);
		} else {
			g_debug_model_fixup = 0;
		}
	}

	if(gfx_state.debug.no_fade_in)
		models[0].alpha = 1.0f;

	PERFINFO_AUTO_STOP_L2();

	return model_count;
}
