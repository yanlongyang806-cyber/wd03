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
#include "GfxDebug.h"
#include "GfxTextures.h"

#include "endian.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Geometry_Misc););

#define NUMBER_OF_SECONDS_TO_FADE_IN_MODEL 0.25f
#define NUMBER_OF_SECONDS_TO_CONSIDER_INSTANTANEOUS 0.1f
#define NUMBER_OF_SECONDS_TO_ASSUME_NEW 1.f
#define NULL_MODEL ((void *)(-1))

extern CRITICAL_SECTION gfx_geo_critical_section;

ModelLOD *high_detail_loaded_models;

static bool CALLBACK gfxModelFillVertexBufferObject(GeoRenderInfo *geo_render_info, ModelLOD *model, int unused_param);
static void CALLBACK gfxModelFillVertexBufferObjectFreeMemory(GeoRenderInfo *geo_render_info, ModelLOD *model, int unused_param);

void gfxModelStartup(void)
{
	geoRecentLoadThreshold = timerCpuSpeed() * NUMBER_OF_SECONDS_TO_FADE_IN_MODEL;
	geoRecentLoadEpsilon = (U32)(timerCpuSpeed() * NUMBER_OF_SECONDS_TO_CONSIDER_INSTANTANEOUS);
	geoLastSeenTimeThreshold = timerCpuSpeed() * NUMBER_OF_SECONDS_TO_ASSUME_NEW;
	geoFadeTimeDivisor = 1.f / geoRecentLoadThreshold;
}

U32 gfxModelGetHighDetailAdjust(void)
{
	ModelLOD *walk = high_detail_loaded_models;
	U32 adjust=0;
	PERFINFO_AUTO_START_FUNC();
	while (walk)
	{
		U32 high_size=0;
		U32 low_size=0;
		U32 low_expected_size=0;
		Model *model_parent;
		GeoRenderInfo *geo_render_info = walk->geo_render_info;

		if (geo_render_info)
		{
			high_size = geo_render_info->geo_memory_use;
			if (high_size)
			{
				// high is loaded, it would not be on low end
				// Gather sizes
				model_parent = walk->model_parent;
				if (model_parent && eaSize(&model_parent->model_lods)>=2)
				{
					ModelLOD *low = walk->model_parent->model_lods[1]->actual_lod;
					if (low)
					{
						if ((geo_render_info = low->geo_render_info))
							low_size = geo_render_info->geo_memory_use;
						if (low_size)
							low_expected_size = low_size;
						else
						{
							// Estimate size
							if (low->tri_count != -1)
							{
								low_expected_size = low->tri_count*6 + low->vert_count*40;
							} else {
								// need to load the model to get the size
								printf("");
							}
						}
					}
				}

				// Calc
				adjust += high_size; // don't count it
				if (low_size != low_expected_size)
				{
					// low is not loaded, it should be
					U32 low_delta;
					assert(low_expected_size >= low_size);
					low_delta = low_expected_size - low_size;
					assert(low_delta < high_size);
					adjust -= low_delta;
				}
			}
		}

		walk = walk->high_detail_next;
	}

	//gfxDebugPrintfQueue("mem adjust: %s", friendlyBytes(adjust));
	PERFINFO_AUTO_STOP();
	return adjust;
}

void gfxModelLODDestroyCallback(ModelLOD *model)
{
	PERFINFO_AUTO_START(__FUNCTION__, 1);
	// Free graphics lib data here

	zoNotifyModelFreed(model);
	if (model->geo_render_info)
	{
		gfxFreeGeoRenderInfo(model->geo_render_info, false);
		model->geo_render_info = NULL;
		modelLODUpdateMemUsage(model);
		if (high_detail_loaded_models == model || model->high_detail_prev)
		{
			EnterCriticalSection(&gfx_geo_critical_section);
			// Remove from high detail list
			if (high_detail_loaded_models == model)
				high_detail_loaded_models = model->high_detail_next;
			if (model->high_detail_next)
				model->high_detail_next->high_detail_prev = model->high_detail_prev;
			if (model->high_detail_prev)
				model->high_detail_prev->high_detail_next = model->high_detail_next;
			model->high_detail_next = model->high_detail_prev = NULL;
			LeaveCriticalSection(&gfx_geo_critical_section);
		}
	}
	PERFINFO_AUTO_STOP();
}

void gfxModelFreeAllCallback(void)
{
	zoClearOccluders();
}

void gfxModelReloadCallback(void)
{
	//gfxModelFreeAllLODs(1);
	zoClearOccluders();
}

// This logic in this function should really be platform-aware.  If we go cross-platform, we should refactor.
static void getUsageFromModel(const ModelLOD *model, RdrGeoUsage * pUsage)
{
	ModelHeader *header = model->model_parent->header;

	assert(model->data); // Must have this to determine which usage bits are needed!

	pUsage->bHasSecondary = false;

	// FOR NOW!  All models will put positions and texture coords in the the first two available streams after other components
	// This is for the z-prepass and the shadow buffer
	pUsage->key = RUSE_POSITIONS;

	if (modelHasNorms(model))
		pUsage->key |= RUSE_NORMALS | (gfx_state.noTangentSpace || model->noTangentSpace ? 0 : (RUSE_TANGENTS | RUSE_BINORMALS));

	if (modelHasSts(model))
		pUsage->key |= RUSE_TEXCOORDS;

	if (modelHasSts3(model))
		pUsage->key |= RUSE_TEXCOORD2S;

	if (modelHasColors(model))
		pUsage->key |= RUSE_COLORS;

	if (header && eaSize(&header->bone_names) > 0)
		pUsage->key |= RUSE_BONEWEIGHTS | RUSE_BONEIDS;

	pUsage->iNumPrimaryStreams = 1;
	pUsage->bits[0] = RUSE_POSITIONS;
	pUsage->bits[1] = RUSE_NONE;
	pUsage->bits[2] = RUSE_NONE;
	pUsage->bits[3] = RUSE_NONE;

	if (pUsage->key & (RUSE_TEXCOORDS | RUSE_TEXCOORD2S))
	{
		int iStream = pUsage->iNumPrimaryStreams;
		pUsage->iNumPrimaryStreams++;

		if (pUsage->key & RUSE_TEXCOORDS)
			pUsage->bits[iStream] |= RUSE_TEXCOORDS;
		
		if (pUsage->key & RUSE_TEXCOORD2S)
			pUsage->bits[iStream] |= RUSE_TEXCOORD2S;

		if (model->data->process_time_flags & MODEL_PROCESSED_HIGH_PRECISCION_TEXCOORDS || !(gfx_state.allRenderersFeatures & FEATURE_DECL_F16_2))
			pUsage->bits[iStream] |= RUSE_TEXCOORDS_HI_FLAG;
	}

	if (pUsage->key & (RUSE_NORMALS | RUSE_COLORS | RUSE_BONEWEIGHTS))
	{
		int iStream = pUsage->iNumPrimaryStreams;
		pUsage->iNumPrimaryStreams++;

		if (pUsage->key & RUSE_NORMALS)
			pUsage->bits[iStream] |= RUSE_NORMALS | (gfx_state.noTangentSpace?0:(RUSE_TANGENTS | RUSE_BINORMALS));

		if (pUsage->key & RUSE_COLORS)
			pUsage->bits[iStream] |= RUSE_COLORS;

		if (pUsage->key & RUSE_BONEWEIGHTS)
			pUsage->bits[iStream] |= RUSE_BONEWEIGHTS | RUSE_BONEIDS;
	}

	// now add one more stream if we have "secondary" data
	if (modelHasVerts2(model))
	{
		pUsage->bits[pUsage->iNumPrimaryStreams] |= RUSE_POSITIONS;
		pUsage->bHasSecondary = true;
	}

	if (modelHasNorms2(model))
	{
		pUsage->bits[pUsage->iNumPrimaryStreams] |= RUSE_NORMALS;
		pUsage->bHasSecondary = true;
	}
}

static void modelGetInfo(GeoRenderInfo *geo_render_info, ModelLOD *model, int param_UNUSED, GeoRenderInfoDetails *details)
{
	if(model->model_parent && model->model_parent->header) {
		details->filename = SAFE_MEMBER(model->model_parent->header, filename);
	} else {
		details->filename = NULL;
	}
	details->name = gfxModelLODGetName(model);
	details->sys_mem_packed += modelLODGetBytesCompressed(model);
	details->sys_mem_unpacked += modelLODGetBytesUncompressed(model);
	details->far_dist = loddistFromLODInfo(model->model_parent, NULL, NULL, 1, NULL, false, NULL, NULL);
	details->uv_density = model->uv_density;
	if (model->model_parent && model->model_parent->lod_info && GET_REF(model->model_parent->lod_info->lod_template))
		details->lod_template_name = GET_REF(model->model_parent->lod_info->lod_template)->template_name;
}


void gfxFillModelRenderInfo(ModelLOD *model)
{
	if (!model->geo_render_info)
	{
		EnterCriticalSection(&gfx_geo_critical_section);
		{
			if (!model->geo_render_info)
			{
				RdrGeoUsage usage = {0};
				getUsageFromModel(model,&usage);
				model->geo_render_info = gfxCreateGeoRenderInfo(gfxModelFillVertexBufferObject, gfxModelFillVertexBufferObjectFreeMemory, modelGetInfo, true, model, 0, usage, modelGetUseFlags(model->model_parent), false);
				if (model->load_in_foreground)
					model->geo_render_info->load_in_foreground = true;

				modelLODUpdateMemUsage(model);

				assert(!model->high_detail_next && !model->high_detail_prev);
				if (model->is_high_detail_lod)
				{
					model->high_detail_next = high_detail_loaded_models;
					if (high_detail_loaded_models)
						high_detail_loaded_models->high_detail_prev = model;
					high_detail_loaded_models = model;
				}
			}
		}
		LeaveCriticalSection(&gfx_geo_critical_section);
	}
	assert(model->geo_render_info->parent == model);
}

static __forceinline bool modelWasRecentlyLoaded(ModelLOD *model)
{
	return model->geo_render_info && (((U32)(gfx_state.client_frame_timestamp - model->geo_render_info->geo_loaded_timestamp)) < geoRecentLoadThreshold);
}

static __forceinline bool modelLODIsLoadedToRenderer(ModelLOD *model)
{
	return model->geo_render_info && (model->geo_render_info->geo_loaded & gfx_state.currentRendererFlag);
}

static ModelLOD *findNeighborModelLOD(Model *model, WorldDrawableList *draw_list, ModelLOD *lodmodel, WorldDrawableLod **draw, int *new_lod_index)
{
	int i;
	int myindex=-1;
	int bestindex=-1;
	ModelLOD *bestlod = NULL;
	int numLODs;
	bool high_detail_high_lod;

	if (draw_list)
	{
		numLODs = draw_list->lod_count;
		high_detail_high_lod = draw_list->high_detail_high_lod;
	}
	else
	{
		numLODs = eaSize(&model->lod_info->lods);
		high_detail_high_lod = model->lod_info->high_detail_high_lod;
	}

	for (i = high_detail_high_lod ? 1 : 0; i < numLODs; i++) {
		ModelLOD *lodmodel2 = modelGetLOD(model, i);
		if (!lodmodel2)
			continue;
		if (lodmodel2 == lodmodel) {
			myindex = i;
		} else {
			if (modelLODIsLoadedToRenderer(lodmodel2)) {
				if (!bestlod) {
					// Nothing else found, we're the best
					bestindex = i;
					bestlod = lodmodel2;
				} else if (myindex == -1) {
					// We're before the model we're searching for, so later is better
					//  even if the later one was recently loaded
					//if (!modelWasRecentlyLoaded(lod->cached_model) ||
					//	modelWasRecentlyLoaded(model->lod_info->lods[bestindex]->cached_model))
					{
						bestindex = i;
						bestlod = lodmodel2;
					}
				} else {
					if (ABS_UNS_DIFF(i, myindex) < ABS_UNS_DIFF(bestindex, myindex)) {
						if (!modelWasRecentlyLoaded(lodmodel2) ||
							modelWasRecentlyLoaded(bestlod))
						{
							bestindex = i;
							bestlod = lodmodel2;
						}
					} else {
						// Worse distance, only better if the other was was recently loaded and this was not
						if (!modelWasRecentlyLoaded(lodmodel2) &&
							modelWasRecentlyLoaded(bestlod))
						{
							bestindex = i;
							bestlod = lodmodel2;
						}
					}
				}
			}
		}
	}
	if (!bestlod)
		return NULL;
	*draw = draw_list ? &draw_list->drawable_lods[bestindex] : NULL;
	if (new_lod_index)
		*new_lod_index = bestindex;
	return bestlod;
}

int gfxDemandLoadModelFixup(Model *model, ModelToDraw *models, int models_size, int model_count, WorldDrawableList *draw_list)
{
	U8 maxalpha;

	if (model_count <= 0 || model_count > 2) {
		devassert(0);
		return model_count; // Nothing to do, but can't happen?
	}

	if (!model)
		model = models[0].model->model_parent;

	g_debug_model_fixup = 1;
	if (model_count == 1) {
		if (!modelLODIsLoadedToRenderer(models[0].model)) {
			// Not loaded
			// Find a neighboring, loaded (prefer !recent) model, and put it in models[0]
			models[0].model = findNeighborModelLOD(model, draw_list, models[0].model, &models[0].draw, &models[0].lod_index);
			// If none found, model_count = 0;
			if (!models[0].model)
				return 0;
			models[0].morph = 0;
			// Fill in geo_handle
			gfxDemandLoadModelLOD(models[0].model, &models[0].geo_handle_primary, true);
			assert(models[0].geo_handle_primary);
			return 1; //model_count;
		}
		if (modelWasRecentlyLoaded(models[0].model)) {
			// This model was recently loaded, that means a bit ago we were either
			//   in the case above, or we were at a different LOD completely.
			// Find a neighboring LOD which is loaded (prefer !recent)
			models[1].model = findNeighborModelLOD(model, draw_list, models[0].model, &models[1].draw, &models[1].lod_index);
			// If no other LOD is loaded, pop this one to fully specified alpha (just return)
			if (!models[1].model)
				return 1; //model_count
			// Otherwise, put Other in list, index 1, with Alpha 0, and run code below
			models[1].alpha = 0;
			models[1].morph = 0;
			gfxDemandLoadModelLOD(models[1].model, &models[1].geo_handle_primary, true);
			model_count = 2;
		}
	}
	g_debug_model_fixup = 2;
	assert(model_count == 2);
	maxalpha = MAX(models[0].alpha, models[1].alpha);
	if (!modelLODIsLoadedToRenderer(models[0].model)) {
		if (!modelLODIsLoadedToRenderer(models[1].model)) {
			// Both unloaded, find any loaded model, draw at MAX(alpha), return
			models[0].model = findNeighborModelLOD(model, draw_list, models[0].model, &models[0].draw, &models[0].lod_index);
			if (!models[0].model)
				return 0; // No one loaded at all!
			models[0].alpha = maxalpha;
			models[0].morph = 0;
			gfxDemandLoadModelLOD(models[0].model, &models[0].geo_handle_primary, true);
			assert(models[0].geo_handle_primary);
			g_debug_model_fixup = 3;
			return 1;
		} else {
			// One unloaded, other recently loaded, or 
			// One unloaded, the other fully loaded
			// Draw index 1 at MAX(alpha), return
			models[0] = models[1];
			models[0].alpha = maxalpha;
			g_debug_model_fixup = 4;
			return 1;
		}
	} else if (!modelLODIsLoadedToRenderer(models[1].model)) {
		// One unloaded, other recently loaded, or 
		// One unloaded, the other fully loaded
		// Draw index 0 at MAX(alpha), return
		models[0].alpha = maxalpha;
		g_debug_model_fixup = 5;
		return 1;
	}
	// If we got here, then both models are loaded, at least one of them recently
	// Make index 0 the non-recently loaded one
	if (!modelWasRecentlyLoaded(models[1].model)) {
		ModelToDraw temp = models[0];
		models[0] = models[1];
		models[1] = temp;
		g_debug_model_fixup = 9;
	} else {
		g_debug_model_fixup = 10;
	}
	if (modelWasRecentlyLoaded(models[0].model)) {
		S32 diff = models[1].model->geo_render_info->geo_loaded_timestamp - models[0].model->geo_render_info->geo_loaded_timestamp;
		// Both were recently loaded, if one is older, pretend it's fully loaded, place in index 0
		if ((U32)ABS(diff) <= geoRecentLoadEpsilon) {
			// Both recently loaded at the same time, just draw normally, nothing to do here!
			g_debug_model_fixup = 6;
			return model_count;
		} else if (diff < 0) {
			ModelToDraw temp = models[0];
			models[0] = models[1];
			models[1] = temp;
			g_debug_model_fixup = 7;
		} else {
			// Already in order
			g_debug_model_fixup = 8;
		}
	}
	// Now, index 0 has a fully loaded model (or one we're treating as such), index 1 was recently loaded
	{
		F32 factor = (gfx_state.client_frame_timestamp - models[1].model->geo_render_info->geo_loaded_timestamp) * geoFadeTimeDivisor;
		// 0 to 0.5, have index 0 at maxalpha, and fade index 1 up to what it's supposed to be,
		// 0.5 to 1.0, leave index 1 at what it's supposed to be and fade index 0 to what it's supposed to be.
		assert(factor <= 1.001);
		if (factor < 0.5) {
			models[0].alpha = maxalpha;
			models[1].alpha *= factor * 2.0;
		} else {
			F32 lrpfactor = factor * 2.0 - 1.0;
			models[0].alpha = (1.0f - lrpfactor) * maxalpha + lrpfactor * models[0].alpha;
			//models[1].alpha = models[1].alpha;
		}
	}
	return model_count;
}

bool gfxDemandLoadModelLODCheck(ModelLOD *model_lod)
{
	GeoHandle geo_handle_primary = 0;
	gfxDemandLoadModelLOD(model_lod, &geo_handle_primary, true);
	return !!geo_handle_primary;
}

static bool gfxModelClearAllForDeviceCallback(ModelLOD *model, intptr_t rendererIndex)
{
	gfxGeoClearForDevice(model->geo_render_info, (int)rendererIndex);
	return true;
}

void gfxModelClearAllForDevice(int rendererIndex)
{
	gfxGeoClearAllForDevice(rendererIndex);

	// Unflag any geometry that thinks it's loaded on this device
	modelForEachModelLOD(gfxModelClearAllForDeviceCallback, rendererIndex, true);
}

static const char *ignored_bone_for_2bone_skinning;
void gfxModelSetIgnoredBoneFor2BoneSkinning(const char *bone)
{
	ignored_bone_for_2bone_skinning = allocAddString(bone);
}

static bool CALLBACK gfxModelFillVertexBufferObject(GeoRenderInfo *geo_render_info, ModelLOD *model, int unused_param)
{
	U8 *data1;
	U32 total,vert_total,tri_bytes,tri_bytes32,subobj_bytes;
	U8 *extra_data_ptr;
	U8 *vert_data;
	int idxcount;
	const U32 *tris32;
	int i, tri_counter;
	const Vec2 *texcoords=NULL;
	const Vec3 *normals=NULL;
	bool dynamic_tangent_space;
	ModelLODData *model_data = model->data;
	int iStream;
	bool bHasMorphData;
	int iVertSize;

	assert(geo_render_info);
	assert(model_data);

	idxcount = model_data->tri_count * 3;

	bHasMorphData = modelHasVerts2(model) || modelHasNorms2(model);

	// Moved these conditions up here
	dynamic_tangent_space = (geo_render_info->usage.key & (RUSE_BINORMALS|RUSE_TANGENTS)) == (RUSE_BINORMALS|RUSE_TANGENTS) &&
							!(modelHasBinorms(model) && modelHasTangents(model)) &&
							(geo_render_info->usage.key & (RUSE_NORMALS|RUSE_POSITIONS|RUSE_TEXCOORDS)) == (RUSE_NORMALS|RUSE_POSITIONS|RUSE_TEXCOORDS);

	//////////////////////////////////////////////////////////////////////////
	// primary stream
	iVertSize = rdrGeoGetTotalVertSize(&geo_render_info->usage);
	if (bHasMorphData)
	{
		// add the position and normal for the extra stream
		iVertSize += sizeof(Vec3) + sizeof(Vec3_Packed);
	}

	vert_total = iVertSize * model_data->vert_count;

	tri_bytes	= sizeof(U16) * idxcount;
	tri_bytes += tri_bytes % 4; // align end to 4 byte boundary so floats can be copied (tri_bytes % 4 is 0 or 2)
	tri_bytes32 = sizeof(U32) * idxcount;
	subobj_bytes = model_data->tex_count * sizeof(int);
	total = vert_total + tri_bytes + subobj_bytes + subobj_bytes;

	data1 = ScratchAlloc(MAX(model_data->vert_count * sizeof(Vec4), tri_bytes32));

	geo_render_info->tri_count = model_data->tri_count;
	geo_render_info->vert_count = model_data->vert_count;
	geo_render_info->subobject_count = model_data->tex_count;

	extra_data_ptr = geo_render_info->vbo_data_primary.data_ptr = memrefAlloc(total);
	geo_render_info->vbo_data_primary.data_size = total;

	geo_render_info->vbo_data_primary.tris = (U16 *)extra_data_ptr;
	extra_data_ptr += tri_bytes;

	modelLockUnpacked(model);
	// convert 32 bit idx data to 16 bit idx data
	tris32 = modelGetTris(model);
	for (i = 0; i < idxcount; ++i)
		geo_render_info->vbo_data_primary.tris[i] = (U16)tris32[i];

	geo_render_info->vbo_data_primary.vert_data = vert_data = extra_data_ptr;
	extra_data_ptr += vert_total;

	geo_render_info->vbo_data_primary.subobject_tri_bases = (int*)extra_data_ptr;
	extra_data_ptr += subobj_bytes;

	geo_render_info->vbo_data_primary.subobject_tri_counts = (int*)extra_data_ptr;
	extra_data_ptr += subobj_bytes;

	tri_counter = 0;
	for (i = 0; i < model_data->tex_count; i++)
	{
		geo_render_info->vbo_data_primary.subobject_tri_bases[i] = tri_counter;
		geo_render_info->vbo_data_primary.subobject_tri_counts[i] = model_data->tex_idx[i].count;
		tri_counter += model_data->tex_idx[i].count;
	}


	for (iStream=0;iStream<geo_render_info->usage.iNumPrimaryStreams;iStream++)
	{
		const RdrGeoUsageBits bits = geo_render_info->usage.bits[iStream];
		RdrVertexDeclaration *vdecl = rdrGetVertexDeclaration(bits);

		//TODO: interleave the data on disk so we don't pay the performance penalty here
		if (bits & RUSE_POSITIONS)
		{
			const Vec3 *vdata = modelGetVerts(model);

			for (i = 0; i < model_data->vert_count; i++)
				copyVec3(vdata[i], (F32 *)(vert_data + vdecl->stride * i + vdecl->position_offset));
		}

		if (bits & RUSE_NORMALS)
		{
			normals = modelGetNorms(model);
			for (i = 0; i < model_data->vert_count; i++)
				*(Vec3_Packed*)(vert_data + vdecl->stride * i + vdecl->normal_offset) = Vec3toPacked(normals[i]);
		}

		if (bits & RUSE_TEXCOORDS)
		{
			texcoords = modelGetSts(model);
			for (i = 0; i < model_data->vert_count; i++)
			{
				if (bits & RUSE_TEXCOORDS_HI_FLAG) {
					copyVec2(texcoords[i], (F32 *)(vert_data + vdecl->stride * i + vdecl->texcoord_offset));
				} else {
					((F16 *)(vert_data + vdecl->stride * i + vdecl->texcoord_offset))[0] = F32toF16(texcoords[i][0]);
					((F16 *)(vert_data + vdecl->stride * i + vdecl->texcoord_offset))[1] = F32toF16(texcoords[i][1]);
				}
			}
		}

		if (bits & RUSE_TEXCOORD2S)
		{
			texcoords = modelGetSts3(model);
			devassert(bits & RUSE_TEXCOORDS);
			for (i = 0; i < model_data->vert_count; i++)
			{
				if (bits & RUSE_TEXCOORDS_HI_FLAG) {
					copyVec2(texcoords[i], (F32 *)(vert_data + vdecl->stride * i + vdecl->texcoord2_offset));
				} else {
					((F16 *)(vert_data + vdecl->stride * i + vdecl->texcoord2_offset))[0] = F32toF16(texcoords[i][0]);
					((F16 *)(vert_data + vdecl->stride * i + vdecl->texcoord2_offset))[1] = F32toF16(texcoords[i][1]);
				}
			}
		}

		if (bits & RUSE_COLORS)
		{
			const U8 *vdata = modelGetColors(model);
			for (i = 0; i < model_data->vert_count; i++)
			{
				U32 *color = (U32 *)(vert_data + vdecl->stride * i + vdecl->color_offset);
				U32 v;
#if _PS3
				// RGBA format 
				v = (vdata[i*4+2]<<24) | (vdata[i*4+1]<<16) | (vdata[i*4]<<8) | vdata[i*4+3];
#else
				// BGRA format 
				v = vdata[i*4] | (vdata[i*4+1]<<8) | (vdata[i*4+2]<<16) | (vdata[i*4+3]<<24);
#endif
				*color = v;
			}
		}

		if (bits & RUSE_BONEWEIGHTS)
		{
			if (modelHasWeights(model))
			{
				const U8 *vdata = modelGetWeights(model);
				for (i = 0; i < model_data->vert_count; i++)
				{
					U32 *weight = (U32 *)(vert_data + vdecl->stride * i + vdecl->boneweight_offset);
					U8 weight0 = 255 - vdata[i*4+1] - vdata[i*4+2] - vdata[i*4+3];
					U32 v = weight0 | (vdata[i*4+1]<<8) | (vdata[i*4+2]<<16) | (vdata[i*4+3]<<24);

#if _PS3
					*weight = endianSwapU32(v);
#else
					*weight = v;
#endif
				}
			}
			else
			{
				for (i = 0; i < model_data->vert_count; i++)
				{
					U32 *weight = (U32 *)(vert_data + vdecl->stride * i + vdecl->boneweight_offset);
					U32 v = 255;

#if _PS3
					*weight = endianSwapU32(v);
#else
					*weight = v;
#endif
				}
			}
		}

		if (bits & RUSE_BONEIDS)
		{
			if (modelHasMatidxs(model))
			{
				const U8 *vdata = modelGetMatidxs(model);
				for (i = 0; i < model_data->vert_count; i++)
				{
					U16 *id = (U16 *)(vert_data + vdecl->stride * i + vdecl->boneid_offset);
					id[0] = vdata[i*4+0];
					id[1] = vdata[i*4+1];
					id[2] = vdata[i*4+2];
					id[3] = vdata[i*4+3];
				}
			}
			else
			{
				for (i = 0; i < model_data->vert_count; i++)
				{
					U16 *id = (U16 *)(vert_data + vdecl->stride * i + vdecl->boneid_offset);
					id[0] = 0;
					id[1] = 0;
					id[2] = 0;
					id[3] = 0;
				}
			}
		}

		if ((bits & (RUSE_BONEWEIGHTS|RUSE_BONEIDS)) == (RUSE_BONEWEIGHTS|RUSE_BONEIDS))
		{
			// For sorting, ignore certain bones known to cause problem on 2-bone skinning
			int badid=-1;
			for (i=eaSize(&model->model_parent->header->bone_names)-1; i>=0; i--)
			{
				if (ignored_bone_for_2bone_skinning == model->model_parent->header->bone_names[i])
					badid = i*3;
			}
			// Make sure they're sorted correctly
			for (i=0; i<model_data->vert_count; i++)
			{
				typedef union WeightCast {
					U32 value;
					struct {
#if _XBOX
						U8 w,z,y,x;
#else
						U8 x,y,z,w;
#endif
					};
				} WeightCast;
				U16 t16;
				U8 t8;
				U16 *id = (U16 *)(vert_data + vdecl->stride * i + vdecl->boneid_offset);
				U32 *weight = (U32 *)(vert_data + vdecl->stride * i + vdecl->boneweight_offset);
				WeightCast w;
				w.value = *weight;

#define SW(f0, f1, i0, i1)			\
	if (w.f0 < w.f1 && id[i1]!=badid || id[i0]==badid)	\
				{					\
				t8 = w.f0; w.f0 = w.f1; w.f1 = t8;		\
				t16 = id[i0]; id[i0] = id[i1]; id[i1] = t16;	\
				}

				SW(x, y, 0, 1);
				SW(z, w, 2, 3);
				SW(x, z, 0, 2);
				SW(y, w, 1, 3);
				SW(y, z, 1, 2);

#undef SW

				*weight = w.value;
			}
		}

		if (bits & (RUSE_BINORMALS|RUSE_TANGENTS))
		{
			if (modelHasBinorms(model) && modelHasTangents(model))
			{
				const Vec3 *vdata = modelGetBinorms(model);
				for (i = 0; i < model_data->vert_count; i++)
					*(Vec3_Packed*)(vert_data + vdecl->stride * i + vdecl->binormal_offset) = Vec3toPacked(vdata[i]);

				vdata = modelGetTangents(model);
				for (i = 0; i < model_data->vert_count; i++)
					*(Vec3_Packed*)(vert_data + vdecl->stride * i + vdecl->tangent_offset) = Vec3toPacked(vdata[i]);
				assert(!dynamic_tangent_space);  // Checking consistency with if clauses above
			}
			else if (dynamic_tangent_space)
			{
				TangentSpaceGenData data = {0};

				data.tri_count = model_data->tri_count;
				data.tri_indices = geo_render_info->vbo_data_primary.tris;

				assert(texcoords && normals);

				data.vert_count = model_data->vert_count;
				data.positions = (F32 *)(vert_data + vdecl->position_offset);
				data.texcoords = (F32 *)texcoords;
				data.normals = (F32 *)normals;
				// Output binormals/normals into a scratch buffer, and then interleave in
				data.binormals = (F32*)data1; // (F32 *)(vert_data + vdecl->binormal_offset);
				data.tangents = (F32*)ScratchAlloc(sizeof(Vec3) * model_data->vert_count); // (F32 *)(vert_data + vdecl->tangent_offset);
				if (bits & RUSE_BONEWEIGHTS)
					data.bone_weights = (U32 *)(vert_data + vdecl->boneweight_offset);
				if (bits & RUSE_BONEIDS)
					data.bone_ids = (U16 *)(vert_data + vdecl->boneid_offset);

				data.position_stride = data.bone_weight_stride = data.bone_id_stride = vdecl->stride;
				data.texcoord_stride = sizeof(Vec2);
				data.normal_stride = sizeof(Vec3);
				data.binormal_stride = data.tangent_stride = sizeof(Vec3);

				addTangentSpace(&data,true);
				for (i=0; i<model_data->vert_count; i++) {
					*(Vec3_Packed*)(vert_data + vdecl->stride * i + vdecl->tangent_offset) = Vec3toPacked(((Vec3*)data.tangents)[i]);
					*(Vec3_Packed*)(vert_data + vdecl->stride * i + vdecl->binormal_offset) = Vec3toPacked(((Vec3*)data.binormals)[i]);
				}
				ScratchFree(data.tangents);
			} else {
				assert(!dynamic_tangent_space);  // Checking consistency with if clauses above
			}
		} else {
			assert(!dynamic_tangent_space);  // Checking consistency with if clauses above
		}

		vert_data += vdecl->stride * model_data->vert_count;
	}

	//////////////////////////////////////////////////////////////////////////
	// secondary stream
	if (bHasMorphData)
	{
		RdrVertexDeclaration * vdecl;

		vdecl = rdrGetVertexDeclaration(geo_render_info->usage.bits[iStream]);

		//TODO: interleave the data on disk so we don't pay the performance penalty here

		if (modelHasVerts2(model))
		{
			const Vec3 *vdata = modelGetVerts2(model);
			for (i = 0; i < model_data->vert_count; i++)
				copyVec3(vdata[i], (F32 *)(vert_data + vdecl->stride * i));
		}

		if (modelHasNorms2(model))
		{
			const Vec3 *vdata = modelGetNorms2(model);
			for (i = 0; i < model_data->vert_count; i++)
				*(Vec3_Packed*)(vert_data + vdecl->stride * i + sizeof(Vec3)) = Vec3toPacked(vdata[i]);
		}
	}

	//do some checking here so that we can spit out an error for incorrectly setup wind models
	//instead of having weird failures later
	if ((model_data->process_time_flags & MODEL_PROCESSED_HAS_WIND) && (model_data->process_time_flags & MODEL_PROCESSED_HAS_TRUNK_WIND))
	{
		InvalidDataErrorFilenamef(model->model_parent->header->filename, "Model has both !!TRUNKWIND and !!WIND specified. This makes no sense, please only use one.");
	}
	else if ((model_data->process_time_flags & MODEL_PROCESSED_HAS_WIND) && (!modelHasColors(model)))
	{
		InvalidDataErrorFilenamef(model->model_parent->header->filename, "Model with !!WIND specified has no vertex colors. There will be no wind effect. "
			" Please use !!TRUNKWIND or add vertex colors as documented");
	}

	ScratchFree(data1);

	modelUnlockUnpacked(model);

	// Don't need unpacked data anymore, free it?
	// Note: might need it in a bit for collision/zocclusion - keep it around for that?
	// Another thread (zocclusion) might be in the middle of using this data though :(

	return true;
}

static void CALLBACK gfxModelFillVertexBufferObjectFreeMemory(GeoRenderInfo *geo_render_info, ModelLOD *model, int unused_param)
{
	// No data to free here!
}

static struct {
	int used_count;
	int used_size_geo;
	int used_size_model;
	char *match_string;
	FILE *file;
} modelShowUsageData;

static bool showUsage(ModelLOD *model, intptr_t userData_UNUSED)
{
	GeoRenderInfo *geo_render_info;
	int geo_mem=0, model_mem=0;
	if (modelShowUsageData.match_string && modelShowUsageData.match_string[0] && !strstri(model->model_parent->name, modelShowUsageData.match_string) && !strstri(model->model_parent->header->filename, modelShowUsageData.match_string))
		return true;
	if (modelLODIsLoaded(model)) { // This might not be the right check...
		model_mem = modelLODGetBytesSystem(model);
	}
	if ((geo_render_info=model->geo_render_info)) {
		if (geo_render_info->geo_loaded) {
			geo_mem = geo_render_info->geo_memory_use;
		}
	}
	if (geo_mem || model_mem) {
		char		buf[1000];
		sprintf(buf,"Sys:%6d Vid:%6d [V:%5d T:%5d] %s/%s",model_mem, geo_mem, model->vert_count, model->tri_count, model->model_parent->header->filename, model->model_parent->name);
		conPrintf("%s\n", buf);
		if (modelShowUsageData.file)
			fprintf(modelShowUsageData.file,"%s\n",buf);
	}

	modelShowUsageData.used_size_geo += geo_mem;
	modelShowUsageData.used_size_model += model_mem;
	modelShowUsageData.used_count+=!!(geo_mem || model_mem);
	return true;
}

// Prints out model memory usage to the console and to c:\geousage.txt.  Pass in a string to search, or "" for everything.
AUTO_COMMAND ACMD_CATEGORY(Performance);
void modelShowUsage(char *match_string)
{
	char buf1[64], buf2[64];
	modelShowUsageData.used_count = 0;
	modelShowUsageData.used_size_geo = 0;
	modelShowUsageData.used_size_model = 0;
	modelShowUsageData.match_string = match_string;
	modelShowUsageData.file = fopen("c:/geousage.txt","wt");
	modelForEachModelLOD(showUsage, 0, true);
	conPrintf("Total: %d models in system:%s  video:%s\n", modelShowUsageData.used_count, friendlyBytesBuf(modelShowUsageData.used_size_model, buf1), friendlyBytesBuf(modelShowUsageData.used_size_geo, buf2));
	if (modelShowUsageData.file) {
		fprintf(modelShowUsageData.file, "Total: %d models in system:%s  video:%s\n", modelShowUsageData.used_count, friendlyBytesBuf(modelShowUsageData.used_size_model, buf1), friendlyBytesBuf(modelShowUsageData.used_size_geo, buf2));
		fclose(modelShowUsageData.file);
	}

}




