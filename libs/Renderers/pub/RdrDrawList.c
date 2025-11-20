#include "RdrDrawList.h"
#include "RdrDrawListPrivate.h"
#include "RdrDrawable.h"
#include "RdrState.h"
#include "RdrLightAssembly.h"

#include "timing.h"
#include "Color.h"
#include "Frustum.h"
#include "earray.h"
#include "StashTable.h"
#include "LinearAllocator.h"
#include "DebugState.h"
#include "qsortG.h"
#include "HashFunctions.h"
#include "ScratchStack.h"
#include "memref.h"
#include "memlog.h"
// for isDevelopmentMode
#include "file.h"
#include "sortedArray.h"

#include "AutoGen/RdrDrawable_h_ast.c"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););

PreloadedLightCombo **preloaded_light_combos;

#define ULTRA_INSTANCE_DEBUG 0
#define UNLIT_HDR_CONTRIBUTION_MULTIPLIER 10.f
#define OBJECT_RADIUS_TOO_SMALL_TO_ZSORT 30.0f

static float HDR_DEPTH_BIAS = -0.000008f;
static float HDR_SLOPESCALE_DEPTH_BIAS = -0.0956412f;

void rdrSetHDRSlopeScaleDepthBias(F32 bias)
{
	HDR_SLOPESCALE_DEPTH_BIAS = bias;
}

void rdrSetHDRDepthBias(F32 bias)
{
	HDR_DEPTH_BIAS = bias;
}

F32 rdrGetHDRSlopeScaleDepthBias()
{
	return HDR_SLOPESCALE_DEPTH_BIAS;
}

F32 rdrGetHDRDepthBias()
{
	return HDR_DEPTH_BIAS;
}

// cap memory usage at around 2M or 1M per RdrDrawListData
#define MAX_MEMORY_USAGE (2 * 1024 * 1024)
#define MAX_MEMORY_USAGE_SMALL (1 * 1024 * 1024)

// allocate in 256K or 16K blocks
#define MEM_BLOCK_SIZE (256 * 1024)
#define MEM_BLOCK_SIZE_SMALL (16 * 1024)

#define DRAW_MAX_VS_CONSTANTS 10
#define DRAW_MAX_VS_TEXTURES 4
#define DRAW_MAX_BONES 256
#define DRAW_MAX_PRIM_VERTS 16384
#define DRAW_MAX_PRIM_STRIPS 256
#define DRAW_MAX_PARTICLE_COUNT 4096
#define DRAW_MAX_PARTICLE_BONES 50
#define DRAW_MAX_STRIP_VERTS 1024
#define DRAW_MAX_CLOTH_VERTS 2048
#define DRAW_MAX_CLOTH_STRIPS 256
#define DRAW_MAX_CYLINDER_VERTS 1024
#define DRAW_MAX_CYLINDER_INDICES 4096
#define DRAW_MAX_CYLINDER_VS_CONSTANTS 150

// Check light type for possibly invalid data and log it, to track
// use of dangling GfxLight pointers in mishandled light caches.
#define TRACK_BAD_LIGHTS 1

static RdrMaterialFillShaderCallback fillShaderCallback;
static RdrDrawListDataSkinningMatDecFunc skinningMatDecCallback;
static void sortHDRSortBuckets(RdrDrawListData *data, RdrDrawListPassData *pass_data);
RdrDrawListDebugState draw_list_debug_state;

ShaderGraphRenderInfo *g_selected_graph_render_info;
GeoHandle g_selected_geo_handle;

void rdrDrawListSelectByShaderGraph(ShaderGraphRenderInfo *graph_render_info)
{
	g_selected_graph_render_info = graph_render_info;
}
void rdrDrawListSelectByGeoHandle(GeoHandle geo_handle)
{
	g_selected_geo_handle = geo_handle;
}


static RdrSortBucketType sort_type_to_bucket_type[] = 
{
	RSBT_OPAQUE_PRE_GRAB, // Unused
	RSBT_OPAQUE_ONEPASS, // Unused
	RSBT_OPAQUE_PRE_GRAB, // Unused
	RSBT_COUNT, // Unused
	RSBT_COUNT, // Unused
	RSBT_COUNT, // Unused
	RSBT_COUNT, // Unused
	RSBT_ALPHA,
	RSBT_ALPHA_LOW_RES_ALPHA, // Unused
	RSBT_ALPHA_LOW_RES_ADDITIVE, // Unused
	RSBT_ALPHA_LOW_RES_SUBTRACTIVE, // Unused
	RSBT_ALPHA_LATE,		   // Unused
	RSBT_ALPHA_NEED_GRAB_LATE, // Unused
	RSBT_COUNT, // Really means _AUTO
};

STATIC_ASSERT(ARRAY_SIZE(sort_type_to_bucket_type) == RST_MAX);

typedef struct RdrGeoBucketParams
{
	RdrDrawListData *data;
	RdrDrawableGeo *draw;
	RdrSortNode *local_sort_node; // common sort node if no instancing
	RdrInstance *instance;
	RdrSubobject *subobject;
	U32 skyDepth:1;
	U8 alphaU8;
	F32 zdist;
} RdrGeoBucketParams;

void rdrSetFillShaderCallback(RdrMaterialFillShaderCallback callback)
{
	fillShaderCallback = callback;
}

void rdrSetSkinningMatDecFunc(RdrDrawListDataSkinningMatDecFunc callback)
{
	skinningMatDecCallback = callback;
}


F32 rdrDrawListCalcZDist(SA_PARAM_NN_VALID RdrDrawList *draw_list, const Vec3 world_mid)
{
	F32 zdist;
	if ( FINITEVEC3(world_mid) )
    {
	    zdist = dotVec3(draw_list->current_data->visual_pass_view_z, world_mid) + draw_list->current_data->visual_pass_view_z[3];
	    zdist = -zdist;
        return zdist;
    }
    else
    {
        devassert(FINITEVEC3(world_mid));
        return 0.0f;
    }
}


//////////////////////////////////////////////////////////////////////////

static __inline U32 hashLightList(const RdrLightList *light_list, int hashSeed)
{
	STATIC_ASSERT(sizeof(light_list->comparator) % sizeof(U32) == 0);
	return burtle3_hashword((void *)&light_list->comparator, sizeof(light_list->comparator) / sizeof(U32), hashSeed);
}

static int cmpLightList(const RdrLightList *light_list1, const RdrLightList *light_list2)
{
	return memcmp(&light_list1->comparator, &light_list2->comparator, sizeof(light_list1->comparator));
}

RdrDrawList *rdrCreateDrawList(bool is_primary_drawlist)
{
	RdrDrawList *draw_list = calloc(1, sizeof(*draw_list));
	assert(draw_list);
	InitializeCriticalSection(&draw_list->release_critical_section);
	draw_list->is_primary_drawlist = is_primary_drawlist;
	return draw_list;
}

static void freeDrawListData(RdrDrawListData *data)
{
	RdrDrawList *draw_list = data->draw_list;
	int i, j;

	if (draw_list)
	{
		EnterCriticalSection(&draw_list->release_critical_section);
		if (data->device)
		{
			data->draw_list = NULL;
			LeaveCriticalSection(&draw_list->release_critical_section);
			return;
		}
		LeaveCriticalSection(&draw_list->release_critical_section);
	}

	for (i = 0; i < data->pass_max; ++i)
	{
		RdrDrawListPassData *pass_data = &data->pass_data[i];
		for (j = 0; j < RSBT_COUNT; ++j)
			eaDestroy(&pass_data->sort_node_buckets[j].sort_nodes);
	}

	free(data->pass_data);
	data->pass_count = 0;
	data->pass_max = 0;
	eaDestroy(&data->pass_earray);

	linAllocDestroy(data->allocator);
	sortedArrayDestroy(data->light_hashtable);
	sortedArrayDestroy(data->ambient_light_hashtable);
	stashTableDestroy(data->light_list_hashtable);
	sortedArrayDestroy(data->material_hashtable);

	eaDestroyEx((void***)&data->skinning_mat_sets_to_dec_ref, skinningMatDecCallback);

	free(data);
}

void rdrFreeDrawList(RdrDrawList *draw_list)
{
	if (!draw_list)
		return;

	rdrDrawListEndFrame(draw_list);

	eaDestroyEx(&draw_list->data_ptrs, freeDrawListData);
	DeleteCriticalSection(&draw_list->release_critical_section);
	free(draw_list);
}

void rdrReleaseDrawListDataDirect(RdrDevice *device, RdrDrawListData **data_ptr, WTCmdPacket *packet)
{
	RdrDrawListData *data = *data_ptr;
	RdrDrawList *draw_list = data->draw_list;

	if (draw_list)
	{
		EnterCriticalSection(&draw_list->release_critical_section);
		CopyStructs(&draw_list->last_stats, &data->stats, 1);
		eaClearEx((void***)&data->skinning_mat_sets_to_dec_ref, skinningMatDecCallback);
		data->device = NULL;
		LeaveCriticalSection(&draw_list->release_critical_section);
	}
	else
	{
		freeDrawListData(data);
	}
}

static int rdrDrawListAddPassInternal(SA_PARAM_NN_VALID RdrDrawListData *data, SA_PARAM_OP_VALID Frustum *frustum, RdrShaderMode shader_mode, SA_PARAM_OP_VALID RdrLight *shadow_caster, int shadowmap_idx, U32 frustum_bit, U64 frustum_flag, bool depth_only, bool disable_opaque_depth_writes, int parent_pass_idx, bool is_underexposed_pass) // FRUSTUM_CLIP_TYPE
{
	RdrDrawListPassData *pass_data = dynArrayAddStruct_no_memset(data->pass_data, data->pass_count, data->pass_max);
	RdrDrawListPassData *parent_pass = parent_pass_idx >= 0 ? &data->pass_data[parent_pass_idx] : NULL;
	int i;

	assert(shader_mode < RDRSHDM_COUNT);

	pass_data->shader_mode = shader_mode;

	// shadow pass data
	if (shadow_caster)
	{
		pass_data->shadow_light = shadow_caster;
		pass_data->shadowmap_idx = shadowmap_idx;
	}
	else
	{
		pass_data->shadow_light = NULL;
		pass_data->shadowmap_idx = 0;
	}

	// frustum data
	if (frustum)
	{
		pass_data->frustum = frustum;
		copyMat4(frustum->viewmat, pass_data->viewmat);
	}
	else
	{
		pass_data->frustum = NULL;
		copyMat4(unitmat, pass_data->viewmat);
	}

	pass_data->frustum_set_bit = frustum_bit;
	pass_data->frustum_clear_bits = ~pass_data->frustum_set_bit;
	pass_data->frustum_partial_clip_flag = frustum_flag;
	pass_data->frustum_trivial_accept_flag = frustum_flag << 1;
	pass_data->frustum_flag_test_mask = pass_data->frustum_partial_clip_flag | pass_data->frustum_trivial_accept_flag;
	pass_data->frustum_flag_clear_mask = ~pass_data->frustum_flag_test_mask;
	pass_data->depth_bias = 0;
	pass_data->slope_scale_depth_bias = 0;
	pass_data->depth_only = depth_only || (shader_mode == RDRSHDM_ZPREPASS);
	pass_data->disable_opaque_depth_writes = disable_opaque_depth_writes && !rdr_state.dontDisableDepthWritesAfterZPrepass;
	pass_data->is_underexposed_pass = is_underexposed_pass;
	pass_data->owned_by_thread = false;

	if (parent_pass)
	{
		parent_pass->frustum_clear_bits &= pass_data->frustum_clear_bits;
		parent_pass->frustum_flag_clear_mask &= pass_data->frustum_flag_clear_mask;
	}

	if (shader_mode == RDRSHDM_VISUAL_HDR)
	{
		pass_data->depth_bias = HDR_DEPTH_BIAS;
		pass_data->slope_scale_depth_bias = HDR_SLOPESCALE_DEPTH_BIAS;
	}

	if (shader_mode == RDRSHDM_VISUAL)
	{
		getMatRow(pass_data->viewmat, 2, data->visual_pass_view_z);
		assert(FINITEVEC3(data->visual_pass_view_z));
		assert(FINITE(data->visual_pass_view_z[3]));
	}

	// refigure visual_pass_data, since these live in a dynarray and it could be in a different memory location
	for (i = 0; i < data->pass_count; ++i)
	{
		if (data->pass_data[i].shader_mode == RDRSHDM_VISUAL)
			data->visual_pass_data = &data->pass_data[i];
	}

	return data->pass_count - 1;
}

int rdrDrawListAddPass(RdrDrawList *draw_list, Frustum *frustum, RdrShaderMode shader_mode, RdrLight *shadow_caster, int shadowmap_idx, int parent_pass_idx, bool depth_only)
{
	RdrDrawListData *data = draw_list->current_data;
	U32 frustum_bit;
	U64 frustum_flag; // FRUSTUM_CLIP_TYPE
	int i, ret;

	PERFINFO_AUTO_START_L3(__FUNCTION__,1);

	assert(shader_mode != RDRSHDM_ZPREPASS);

	if (shader_mode == RDRSHDM_VISUAL)
	{
		assert(!data->visual_pass_data);

		for (i = 0; i < data->pass_count; ++i)
		{
			assert(data->pass_data[i].shader_mode != RDRSHDM_VISUAL);
			assert(data->pass_data[i].shader_mode != RDRSHDM_VISUAL_HDR);
			assert(data->pass_data[i].shader_mode != RDRSHDM_ZPREPASS);
		}

		assert(!shadow_caster);
	}

	frustum_bit = data->next_frustum_bit;
	frustum_flag = data->next_frustum_flag;
	data->next_frustum_bit = data->next_frustum_bit << 1;
	data->next_frustum_flag = data->next_frustum_flag << 2;

	ret = rdrDrawListAddPassInternal(data, frustum, shader_mode, shadow_caster, shadowmap_idx, frustum_bit, frustum_flag, depth_only,
									 shader_mode == RDRSHDM_VISUAL && draw_list->zprepass && draw_list->zprepass_test,
									 parent_pass_idx, false);
	
	if (shader_mode == RDRSHDM_VISUAL)
	{
		int idx;
		if (draw_list->zprepass)
		{
			idx = rdrDrawListAddPassInternal(data, frustum, RDRSHDM_ZPREPASS, NULL, shadowmap_idx, frustum_bit, frustum_flag, depth_only, false, parent_pass_idx, false);
			data->pass_data[idx].frustum = NULL;
		}
		if (draw_list->separate_hdr_pass)
		{
			idx = rdrDrawListAddPassInternal(data, frustum, RDRSHDM_VISUAL_HDR, NULL, shadowmap_idx, frustum_bit, frustum_flag, depth_only, false, parent_pass_idx, true);
			data->pass_data[idx].frustum = NULL;
		}
		if (draw_list->aux_visual_pass)
		{
			idx = rdrDrawListAddPassInternal(data, frustum, RDRSHDM_TARGETING, NULL, shadowmap_idx, frustum_bit, frustum_flag, depth_only, false, parent_pass_idx, true);
			data->pass_data[idx].frustum = NULL;
		}
	}

	if (frustum)
		frustumResetBounds(frustum);

	assert(data->pass_count <= MAX_RENDER_PASSES);

	PERFINFO_AUTO_STOP_L3();

	return ret;
}

RdrDrawListPassData *rdrDrawListGetPasses(RdrDrawList *draw_list, RdrDrawListPassData ***passes, Vec4 visual_pass_view_z)
{
	RdrDrawListData *data = draw_list->current_data;
	int i;

	eaSetSize(&data->pass_earray, 0);
	for (i = 0; i < data->pass_count; ++i)
	{
		RdrDrawListPassData *pass_data = &data->pass_data[i];
		if (pass_data->shader_mode == RDRSHDM_ZPREPASS || 
			pass_data->shader_mode == RDRSHDM_VISUAL_HDR ||
			pass_data->shader_mode == RDRSHDM_TARGETING)
			continue;
		eaPush(&data->pass_earray, pass_data);
	}

	*passes = data->pass_earray;

	copyVec4(data->visual_pass_view_z, visual_pass_view_z);
	return data->visual_pass_data;
}

void rdrDrawListBeginFrame(RdrDrawList *draw_list, bool zprepass, bool zprepass_test, bool outlining, 
						   bool sort_opaque_models, bool use_shadow_buffer, bool globally_using_shadow_buffer, bool use_ssao, RdrDrawListLightingMode light_mode, 
						   bool separate_hdr_pass, bool has_hdr_texture, F32 bloom_brightness, F32 dof_distance, F32 mem_multiplier,
						   bool aux_visual_pass)
{
	int i, j;
	RdrDrawListData *data = NULL;

	PERFINFO_AUTO_START_FUNC();

	rdrDrawListEndFrame(draw_list);

	assert(!draw_list->current_data);

	draw_list->zprepass = !!zprepass;
	draw_list->zprepass_test = !!zprepass_test;
	draw_list->outlining = !!outlining;
	draw_list->light_mode = light_mode;
	draw_list->separate_hdr_pass = !!separate_hdr_pass;
	draw_list->aux_visual_pass = !!aux_visual_pass;
	draw_list->has_hdr_texture = !!has_hdr_texture;
	draw_list->bloom_brightness = bloom_brightness;

	draw_list->need_texture_screen_color = false;
	draw_list->need_texture_screen_depth = false;
	draw_list->need_texture_screen_color_late = false;
	draw_list->need_texture_screen_depth_late = false;
	draw_list->need_texture_screen_color_blurred = false;

	// sanity check
	assert(eaSize(&draw_list->data_ptrs) < 10);

	// look for an available RdrDrawListData
	for (i = 0; i < eaSize(&draw_list->data_ptrs); ++i)
	{
		if (!draw_list->data_ptrs[i]->device)
		{
			data = draw_list->data_ptrs[i];
			break;
		}
	}

	MAX1(mem_multiplier, 1);
	mem_multiplier = ((int)(mem_multiplier * 5)) * 0.2f;

	if (!data)
	{
		data = calloc(1, sizeof(RdrDrawListData));
		data->allocator = linAllocCreateEx(	draw_list->is_primary_drawlist?MEM_BLOCK_SIZE:MEM_BLOCK_SIZE_SMALL, 
											(draw_list->is_primary_drawlist?MAX_MEMORY_USAGE:MAX_MEMORY_USAGE_SMALL) * mem_multiplier, 
											true);
		data->light_hashtable = sortedArrayCreate(1024);
		data->ambient_light_hashtable = sortedArrayCreate(128);
		data->light_list_hashtable = stashTableCreateExternalFunctions(1024, StashDefault, hashLightList, cmpLightList);
		// we know Champions can drive this over 2000
		data->material_hashtable = sortedArrayCreate(4000);
		data->draw_list = draw_list;
		eaPush(&draw_list->data_ptrs, data);
	}
	data->sort_opaque = !!sort_opaque_models;
	data->use_shadow_buffer = !!use_shadow_buffer;
	data->globally_using_shadow_buffer = !!globally_using_shadow_buffer;
	data->use_ssao = !!use_ssao;
	data->sent_materials = false;
	data->sent_lights = false;
	data->sent_light_shadow_params = false;
	data->dof_distance = dof_distance;

	// reset data
	for (i = 0; i < data->pass_count; ++i)
	{
		RdrDrawListPassData *pass_data = &data->pass_data[i];
		for (j = 0; j < RSBT_COUNT; ++j)
		{
			eaSetSize(&pass_data->sort_node_buckets[j].sort_nodes, 0);
			pass_data->sort_node_buckets[j].total_tri_count = 0;
		}
	}

	data->pass_count = 0;
	data->visual_pass_data = NULL;
	data->next_frustum_bit = 1;
	data->next_frustum_flag = 1;

	linAllocClear(data->allocator);
	linAllocSetMemoryLimit(data->allocator, (draw_list->is_primary_drawlist?MAX_MEMORY_USAGE:MAX_MEMORY_USAGE_SMALL) * mem_multiplier);

	sortedArrayClear(data->light_hashtable);
	sortedArrayClear(data->ambient_light_hashtable);
	stashTableClear(data->light_list_hashtable);
	sortedArrayClear(data->material_hashtable);

	ZeroStructForce(&data->stats);
	ZeroStructForce(&data->fg_stats);
	CopyStructs(&draw_list->stats, &draw_list->last_stats, 1);

	ZeroStructForce(&data->surface_handle_fixup);

	if (draw_list->max_bloom_brightness <= 0)
		draw_list->max_bloom_brightness = 2;
	draw_list->bloom_brightness_multiplier = 1023.f / draw_list->max_bloom_brightness;
	draw_list->max_bloom_brightness = 0;

	// set current
	draw_list->current_data = data;

	PERFINFO_AUTO_STOP();
}

static void rdrProcessRdrLight(RdrDrawListData *data, const RdrLightDefinition **light_defs, 
   const RdrLight * light, RdrLightType dst_light_type, RdrLightData * light_data, RdrDrawListPassData *pass_data, bool shadow_only)
{
	const RdrLightDefinition *light_def;
	int i;

#if TRACK_BAD_LIGHTS
	if (light->light_type <= 0 || light->light_type > RDRLIGHT_MASK)
	{
		memlog_printf(NULL, "Bad RdrLight at process time 0x%p\n", light);
	}
#endif

	light_def = light_defs[rdrGetSimpleLightType(dst_light_type)];

	if (shadow_only)
	{
		for (i = 0; i < RLCT_COUNT; ++i)
		{
			if (light_data->normal[i].const_count)
				rdrLightFillMaterialConstants(light_data->normal[i].constants, light_def, light, dst_light_type, pass_data->viewmat, RLDEFTYPE_NORMAL, i, true);
			if (light_data->normal[i].tex_count)
				rdrLightFillMaterialTextures(light_data->normal[i].tex_handles, light_def, light, dst_light_type, RLDEFTYPE_NORMAL, i, true);

			if (light_data->simple[i].const_count)
				rdrLightFillMaterialConstants(light_data->simple[i].constants, light_def, light, dst_light_type, pass_data->viewmat, RLDEFTYPE_SIMPLE, i, true);
			if (light_data->simple[i].tex_count)
				rdrLightFillMaterialTextures(light_data->simple[i].tex_handles, light_def, light, dst_light_type, RLDEFTYPE_SIMPLE, i, true);
		}

		if (light_data->shadow_test.const_count)
			rdrLightFillMaterialConstants(light_data->shadow_test.constants, light_def, light, dst_light_type, pass_data->viewmat, RLDEFTYPE_SHADOW_TEST, RLCT_WORLD, true);
		if (light_data->shadow_test.tex_count)
			rdrLightFillMaterialTextures(light_data->shadow_test.tex_handles, light_def, light, dst_light_type, RLDEFTYPE_SHADOW_TEST, RLCT_WORLD, true);
	}
	else
	{
		U16 dummy=0;
		assert(light->light_type != RDRLIGHT_NONE);

		light_data->normal[0].const_count = 0;
		light_data->normal[0].tex_count = 0;
		light_data->simple[0].const_count = 0;
		light_data->simple[0].tex_count = 0;
		light_data->shadow_test.const_count = 0;
		light_data->shadow_test.tex_count = 0;

		light_data->light_type = light->light_type;

		rdrLightAddConstAndTexCounts(light_def, dst_light_type,
			&light_data->simple[0].const_count, &light_data->simple[0].tex_count,
			&dummy, &dummy,
			true);

		rdrLightAddConstAndTexCounts(light_def, dst_light_type,
			&light_data->normal[0].const_count, &light_data->normal[0].tex_count,
			&light_data->shadow_test.const_count, &light_data->shadow_test.tex_count,
			false);

		for (i = 0; i < RLCT_COUNT; ++i)
		{
			if (i > 0)
			{
				light_data->normal[i].const_count = light_data->normal[0].const_count;
				light_data->normal[i].tex_count = light_data->normal[0].tex_count;
			}

			if (light_data->normal[i].const_count)
			{
				light_data->normal[i].constants = linAlloc(data->allocator, sizeof(Vec4) * light_data->normal[i].const_count);
				rdrLightFillMaterialConstantsEx(light_data->normal[i].constants, light_def, light, dst_light_type, pass_data->viewmat, RLDEFTYPE_NORMAL, i, &light_data->direction_const_idx, &light_data->position_const_idx, &light_data->color_const_idx, false);
				linAllocDone(data->allocator);
			}
			else
			{
				light_data->normal[i].constants = NULL;
			}

			if (light_data->normal[i].tex_count)
			{
				light_data->normal[i].tex_handles = linAlloc(data->allocator, sizeof(TexHandle) * light_data->normal[i].tex_count);
				rdrLightFillMaterialTextures(light_data->normal[i].tex_handles, light_def, light, dst_light_type, RLDEFTYPE_NORMAL, i, false);
				linAllocDone(data->allocator);
			}
			else
			{
				light_data->normal[i].tex_handles = NULL;
			}


			// simple lighting:

			if (i > 0)
			{
				light_data->simple[i].const_count = light_data->simple[0].const_count;
				light_data->simple[i].tex_count = light_data->simple[0].tex_count;
			}

			if (light_data->simple[i].const_count)
			{
				light_data->simple[i].constants = linAlloc(data->allocator, sizeof(Vec4) * light_data->simple[i].const_count);
				rdrLightFillMaterialConstants(light_data->simple[i].constants, light_def, light, dst_light_type, pass_data->viewmat, RLDEFTYPE_SIMPLE, i, false);
				linAllocDone(data->allocator);
			}
			else
			{
				light_data->simple[i].constants = NULL;
			}

			if (light_data->simple[i].tex_count)
			{
				light_data->simple[i].tex_handles = linAlloc(data->allocator, sizeof(TexHandle) * light_data->simple[i].tex_count);
				rdrLightFillMaterialTextures(light_data->simple[i].tex_handles, light_def, light, dst_light_type, RLDEFTYPE_SIMPLE, i, false);
				linAllocDone(data->allocator);
			}
			else
			{
				light_data->simple[i].tex_handles = NULL;
			}

			// vertex lighting:
			ZeroStructForce(&light_data->vertex_lighting[i]);
			rdrLightFillMaterialConstants(light_data->vertex_lighting[i].constants, light_def, light, dst_light_type, pass_data->viewmat, RLDEFTYPE_VERTEX_LIGHTING, i, false);

			// single dir light:
			ZeroStructForce(&light_data->single_dir_light[i]);
			rdrLightFillMaterialConstants(light_data->single_dir_light[i].constants, light_def, light,dst_light_type,  pass_data->viewmat, RLDEFTYPE_SINGLE_DIR_LIGHT, i, false);
		}

		if (light_data->shadow_test.const_count)
		{
			light_data->shadow_test.constants = linAlloc(data->allocator, sizeof(Vec4) * light_data->shadow_test.const_count);
			rdrLightFillMaterialConstants(light_data->shadow_test.constants, light_def, light, dst_light_type, pass_data->viewmat, RLDEFTYPE_SHADOW_TEST, RLCT_WORLD, false);
			linAllocDone(data->allocator);
		}
		else
		{
			light_data->shadow_test.constants = NULL;
		}

		if (light_data->shadow_test.tex_count)
		{
			light_data->shadow_test.tex_handles = linAlloc(data->allocator, sizeof(TexHandle) * light_data->shadow_test.tex_count);
			rdrLightFillMaterialTextures(light_data->shadow_test.tex_handles, light_def, light, dst_light_type, RLDEFTYPE_SHADOW_TEST, RLCT_WORLD, false);
			linAllocDone(data->allocator);
		}
		else
		{
			light_data->shadow_test.tex_handles = NULL;
		}
	}
}

static void rdrProcessRdrSuperLights(RdrDrawListData *data, const RdrLightDefinition **light_defs, 
	const RdrLight * light, RdrLightSuperData * light_super_data, RdrDrawListPassData *pass_data, bool shadow_only)
{
	int i;
	for (i=0; i<ARRAY_SIZE(light_super_data->light_data); i++)
	{
		if (light_super_data->light_data[i])
			rdrProcessRdrLight(data, light_defs, light, i, light_super_data->light_data[i], pass_data, shadow_only);
	}
}

RdrNonPixelMaterial* CreateCopyRdrNonPixelMaterial(const RdrNonPixelMaterial *nonPixelMaterial, LinearAllocator *allocator)
{
	char* mem;
	RdrNonPixelMaterial *retMat;
	size_t tex_handle_size;
	size_t constant_size;

	if (!nonPixelMaterial)
		return NULL;

	tex_handle_size = sizeof(TexHandle) * nonPixelMaterial->tex_count;
	constant_size = sizeof(Vec4) * nonPixelMaterial->const_count;

	if (allocator)
		mem = linAlloc(allocator,(int)(sizeof(RdrNonPixelMaterial) + tex_handle_size + constant_size));
	else
		mem = calloc(1,sizeof(RdrNonPixelMaterial) + tex_handle_size + constant_size);

	retMat = (RdrNonPixelMaterial *)mem;
	memcpy_intrinsic(retMat, nonPixelMaterial, sizeof(RdrNonPixelMaterial));

	mem += sizeof(RdrNonPixelMaterial);
	if (tex_handle_size) {
		retMat->textures = (TexHandle *)mem;
		memcpy_fast(retMat->textures, nonPixelMaterial->textures, tex_handle_size);
	}
	mem += tex_handle_size;
	if (constant_size) {
		retMat->constants = (Vec4 *)mem;
		memcpy_fast(retMat->constants, nonPixelMaterial->constants, constant_size);
	}
	mem += constant_size;

	if (allocator)
		linAllocDone(allocator);

	return retMat;
}

static void rdrDrawListSendMaterials(RdrDrawListData *data)
{
	if (!data->sent_materials)
	{
		int iMaterials = data->material_hashtable->iNumVals;
		int i;
		data->sent_materials = true;

		// copy materials
		for (i=0;i<iMaterials;i++)
		{
			RdrMaterial *src_material = sortedArrayGetKey(data->material_hashtable,i);
			RdrMaterial *dst_material = sortedArrayGetVal(data->material_hashtable,i);

			// CD: not sure how this can happen, but I've seen it in a few crash dumps.
			if (!src_material || !dst_material)
				continue;

			memcpy_intrinsic(dst_material, src_material, sizeof(RdrMaterial));

			if (dst_material->tex_count)
			{
				dst_material->textures = linAlloc(data->allocator, sizeof(TexHandle) * dst_material->tex_count);
				memcpy_fast(dst_material->textures, src_material->textures, sizeof(TexHandle) * dst_material->tex_count);
				if (dst_material->surface_texhandle_fixup)
				{
					int fixup_handle;
					for (fixup_handle = RDR_FIRST_SURFACE_FIXUP_TEXTURE; fixup_handle <= RDR_LAST_SURFACE_FIXUP_TEXTURE; ++fixup_handle )
					{
						U32 tex_handle;
						for (tex_handle = 0; tex_handle < dst_material->tex_count; ++tex_handle )
						{
							if (dst_material->textures[tex_handle] == fixup_handle)
								dst_material->textures[tex_handle] = data->surface_handle_fixup[fixup_handle];
						}
					}
				}
				linAllocDone(data->allocator);
			}
			else
			{
				dst_material->textures = NULL;
			}

			if (dst_material->const_count)
			{
				dst_material->constants = linAlloc(data->allocator, sizeof(Vec4) * dst_material->const_count);
				memcpy_fast(dst_material->constants, src_material->constants, sizeof(Vec4) * dst_material->const_count);
				linAllocDone(data->allocator);
			}
			else
			{
				dst_material->constants = NULL;
			}

			if (dst_material->drawable_const_count)
			{
				dst_material->drawable_constants = linAlloc(data->allocator, sizeof(RdrPerDrawableConstantMapping) * dst_material->drawable_const_count);
				memcpy_fast(dst_material->drawable_constants, src_material->drawable_constants, sizeof(RdrPerDrawableConstantMapping) * dst_material->drawable_const_count);
				linAllocDone(data->allocator);
			}
			else
			{
				dst_material->drawable_constants = NULL;
			}

			dst_material->tessellation_material = CreateCopyRdrNonPixelMaterial(src_material->tessellation_material,data->allocator);
		}
	}
}

void rdrDrawListSendLightsToRenderer(RdrDevice *device, RdrDrawList *draw_list, const RdrLightDefinition **light_defs)
{
	RdrDrawListData *data = draw_list->current_data;
	int i;

	if (!data || data->sent_lights)
		return;

	PERFINFO_AUTO_START_FUNC();

	for (i = 0; i < data->pass_count; ++i)
	{
		RdrDrawListPassData *pass_data = &data->pass_data[i];
		if (pass_data->shader_mode == RDRSHDM_VISUAL && !data->sent_lights)
		{
			int iEntries = data->light_hashtable->iNumVals;
			int j;

			for (j=0;j<iEntries;j++)
			{
				// fill light constants and texture handles
				RdrLight *light_src = sortedArrayGetKey(data->light_hashtable,j);
				RdrLightSuperData *light_super_data = sortedArrayGetVal(data->light_hashtable,j);
				if (light_super_data)
				{
					assert(light_src);
					rdrProcessRdrSuperLights(data, light_defs, light_src, light_super_data, pass_data, false);
				}
			}

			// copy ambient light data
			iEntries = data->ambient_light_hashtable->iNumVals;
			for (j=0;j<iEntries;j++)
			{
				RdrAmbientLight *light_src = sortedArrayGetKey(data->ambient_light_hashtable,j);
				RdrAmbientLight *light_dst = sortedArrayGetVal(data->ambient_light_hashtable,j);
				if (light_dst)
				{
					assert(light_src);
					CopyStructs(light_dst, light_src, 1);
				}
			}

			data->sent_lights = true;
		}
	}

	PERFINFO_AUTO_STOP();
}

void rdrDrawListSendVisualPassesToRenderer(RdrDevice *device, RdrDrawList *draw_list, const RdrLightDefinition **light_defs)
{
	RdrDrawListData *data = draw_list->current_data;
	int i;

	if (!data)
		return;

	PERFINFO_AUTO_START_FUNC();

	rdrDrawListSendMaterials(data);

	for (i = 0; i < data->pass_count; ++i)
	{
		RdrDrawListPassData *pass_data = &data->pass_data[i];

		if (pass_data->shader_mode == RDRSHDM_VISUAL_HDR && !pass_data->owned_by_thread)
			sortHDRSortBuckets(data, pass_data);

		if (pass_data->shader_mode == RDRSHDM_VISUAL && !data->sent_light_shadow_params)
		{
			int iEntries = data->light_hashtable->iNumVals;
			int j;

			for (j=0;j<iEntries;j++)
			{
				RdrLight *light_src = sortedArrayGetKey(data->light_hashtable,j);
				RdrLightSuperData *light_super_data = sortedArrayGetVal(data->light_hashtable,j);
				if (light_super_data)
				{
					assert(light_src);
					rdrProcessRdrSuperLights(data, light_defs, light_src, light_super_data, pass_data, data->sent_lights);
				}
			}

			// copy ambient light data
			iEntries = data->ambient_light_hashtable->iNumVals;
			for (j=0;j<iEntries;j++)
			{
				RdrAmbientLight *light_src = sortedArrayGetKey(data->ambient_light_hashtable,j);
				RdrAmbientLight *light_dst = sortedArrayGetVal(data->ambient_light_hashtable,j);
				if (light_dst)
				{
					assert(light_src);
					CopyStructs(light_dst, light_src, 1);
				}
			}

			data->sent_lights = true;
			data->sent_light_shadow_params = true;
		}
	}

	// send to thread for sorting
	assert(device);
	if (!data->device)
		data->device = device;

	for (i = 0; i < data->pass_count; ++i)
	{
		RdrDrawListPassData *pass_data = &data->pass_data[i];
		if (rdrIsVisualOrZpreShaderMode(pass_data->shader_mode))
		{
			pass_data->owned_by_thread = true;
			rdrSortDrawListData(device, data, i);
		}
	}

	PERFINFO_AUTO_STOP();
}

void rdrDrawListSendShadowPassesToRenderer(RdrDevice *device, RdrDrawList *draw_list, const RdrLightDefinition **light_defs)
{
	RdrDrawListData *data = draw_list->current_data;
	int i;

	if (!data)
		return;

	PERFINFO_AUTO_START_FUNC();

	rdrDrawListSendMaterials(data);

	for (i = 0; i < data->pass_count; ++i)
	{
		RdrDrawListPassData *pass_data = &data->pass_data[i];

		// process shadow pass lights
		if (pass_data->shadow_light)
		{
			RdrLightData *light_data = linAlloc(data->allocator, sizeof(RdrLightData));
			if (light_data)
			{
				pass_data->shadow_light_data = light_data;
				rdrProcessRdrLight(data, light_defs, pass_data->shadow_light, pass_data->shadow_light->light_type, light_data, pass_data, false);
				linAllocDone(data->allocator);
			}
		}
	}

	// send to thread for sorting
	assert(device);
	if (!data->device)
		data->device = device;

	for (i = 0; i < data->pass_count; ++i)
	{
		RdrDrawListPassData *pass_data = &data->pass_data[i];
		if (rdrIsShadowShaderMode(pass_data->shader_mode))
		{
			pass_data->owned_by_thread = true;
			rdrSortDrawListData(device, data, i);
		}
	}

	PERFINFO_AUTO_STOP();
}

void rdrDrawListDrawAuxVisualPassObjects(RdrDevice *device, RdrDrawList *draw_list)
{
	int i;
	RdrDrawListData *data = draw_list->current_data;

	if (!data)
		return;

	PERFINFO_AUTO_START_FUNC();

	assert(device);

	if (!data->device)
		data->device = device;

	for (i = 0; i < data->pass_count; ++i)
	{
		RdrDrawListPassData *pass_data = &data->pass_data[i];
		if (pass_data->shader_mode == RDRSHDM_TARGETING)
			rdrDrawDrawListData(device, draw_list, data, RSBT_OPAQUE_ONEPASS, i, false, false);
	}

	PERFINFO_AUTO_STOP();
}

int rdrDrawListDrawObjects(RdrDevice *device, RdrDrawList *draw_list, RdrSortType sort_type, bool hdr_pass, bool is_low_res_edge_pass, bool manual_depth_test)
{
	RdrDrawListData *data = draw_list->current_data;
	int i, total_tri_count = 0;

	if (!data)
		return total_tri_count;

	PERFINFO_AUTO_START_FUNC();

	assert(device);
	if (!data->device)
		data->device = device;

	for (i = 0; i < data->pass_count; ++i)
	{
		RdrDrawListPassData *pass_data = &data->pass_data[i];
		if ((pass_data->shader_mode == RDRSHDM_VISUAL_HDR) != hdr_pass) // Only do RDRSHDM_VISUAL_HDR if hdr_pass is true
			continue;
		if (pass_data->shader_mode == RDRSHDM_VISUAL || pass_data->shader_mode == RDRSHDM_VISUAL_HDR)
		{
			switch (sort_type)
			{
				xcase RST_OPAQUE_ONEPASS:
					total_tri_count += rdrDrawDrawListData(device, draw_list, data, RSBT_OPAQUE_ONEPASS, i, is_low_res_edge_pass, manual_depth_test);

				xcase RST_DEFERRED:
					total_tri_count += rdrDrawDrawListData(device, draw_list, data, RSBT_OPAQUE_PRE_GRAB, i, is_low_res_edge_pass, manual_depth_test);
					total_tri_count += rdrDrawDrawListData(device, draw_list, data, RSBT_DECAL_PRE_GRAB, i, is_low_res_edge_pass, manual_depth_test);

				xcase RST_NONDEFERRED:
					total_tri_count += rdrDrawDrawListData(device, draw_list, data, RSBT_OPAQUE_POST_GRAB_NO_ZPRE, i, is_low_res_edge_pass, manual_depth_test);
					total_tri_count += rdrDrawDrawListData(device, draw_list, data, RSBT_OPAQUE_POST_GRAB_IN_ZPRE, i, is_low_res_edge_pass, manual_depth_test);
					total_tri_count += rdrDrawDrawListData(device, draw_list, data, RSBT_DECAL_POST_GRAB, i, is_low_res_edge_pass, manual_depth_test);

				xcase RST_ALPHA_PREDOF:
					total_tri_count += rdrDrawDrawListData(device, draw_list, data, RSBT_ALPHA_PRE_DOF, i, is_low_res_edge_pass, manual_depth_test);

				xcase RST_ALPHA_PREDOF_NEEDGRAB:
					total_tri_count += rdrDrawDrawListData(device, draw_list, data, RSBT_ALPHA_NEED_GRAB_PRE_DOF, i, is_low_res_edge_pass, manual_depth_test);

				xcase RST_ALPHA:
					total_tri_count += rdrDrawDrawListData(device, draw_list, data, RSBT_ALPHA_NEED_GRAB, i, is_low_res_edge_pass, manual_depth_test);
					total_tri_count += rdrDrawDrawListData(device, draw_list, data, RSBT_ALPHA, i, is_low_res_edge_pass, manual_depth_test);

				xcase RST_ALPHA_LATE:
					total_tri_count += rdrDrawDrawListData(device, draw_list, data, RSBT_ALPHA_LATE, i, is_low_res_edge_pass, manual_depth_test);

				xcase RST_ALPHA_POST_GRAB_LATE:
					total_tri_count += rdrDrawDrawListData(device, draw_list, data, RSBT_ALPHA_NEED_GRAB_LATE, i, is_low_res_edge_pass, manual_depth_test);
					total_tri_count += rdrDrawDrawListData(device, draw_list, data, RSBT_WIREFRAME, i, is_low_res_edge_pass, manual_depth_test);

				xcase RST_ALPHA_LOW_RES_ALPHA:
					total_tri_count += rdrDrawDrawListData(device, draw_list, data, RSBT_ALPHA_LOW_RES_ALPHA, i, is_low_res_edge_pass, manual_depth_test);
					
				xcase RST_ALPHA_LOW_RES_ADDITIVE:
					total_tri_count += rdrDrawDrawListData(device, draw_list, data, RSBT_ALPHA_LOW_RES_ADDITIVE, i, is_low_res_edge_pass, manual_depth_test);
					
				xcase RST_ALPHA_LOW_RES_SUBTRACTIVE:
					total_tri_count += rdrDrawDrawListData(device, draw_list, data, RSBT_ALPHA_LOW_RES_SUBTRACTIVE, i, is_low_res_edge_pass, manual_depth_test);

				xcase RST_ZPREPASS_NO_OUTLINE: case RST_ZPREPASS:
					// only for zprepass
					
				xdefault:
					FatalErrorf("Missing rendering code for sort type %d.", sort_type);
			}
		}
		else if (pass_data->shader_mode == RDRSHDM_ZPREPASS)
		{
			switch (sort_type)
			{
				xcase RST_ZPREPASS_NO_OUTLINE:
					total_tri_count += rdrDrawDrawListData(device, draw_list, data, RSBT_ZPREPASS_NO_OUTLINE, i, is_low_res_edge_pass, manual_depth_test);

				xcase RST_ZPREPASS:
					total_tri_count += rdrDrawDrawListData(device, draw_list, data, RSBT_ZPREPASS, i, is_low_res_edge_pass, manual_depth_test);

				xcase RST_OPAQUE_ONEPASS:
				case RST_DEFERRED:
				case RST_NONDEFERRED:
				case RST_ALPHA_PREDOF_NEEDGRAB:
				case RST_ALPHA_PREDOF:
				case RST_ALPHA:
				case RST_ALPHA_LATE:
				case RST_ALPHA_POST_GRAB_LATE:
				case RST_ALPHA_LOW_RES_ALPHA:
				case RST_ALPHA_LOW_RES_ADDITIVE:
				case RST_ALPHA_LOW_RES_SUBTRACTIVE:
					// only for visual

				xdefault:
					FatalErrorf("Missing rendering code for sort type %d.", sort_type);
			}
		}
	}

	PERFINFO_AUTO_STOP();
	return total_tri_count;
}

void rdrDrawListDrawShadowPassObjects(RdrDevice *device, RdrDrawList *draw_list, int pass_num, F32 depth_bias, F32 slope_scale_depth_bias)
{
	RdrDrawListData *data = draw_list->current_data;

	if (!data)
		return;

	PERFINFO_AUTO_START_FUNC();

	assert(pass_num >= 0 && pass_num < data->pass_count);
	assert(rdrIsShadowShaderMode(data->pass_data[pass_num].shader_mode));

	assert(device);
	if (!data->device)
		data->device = device;

	data->pass_data[pass_num].depth_bias = depth_bias;
	data->pass_data[pass_num].slope_scale_depth_bias = slope_scale_depth_bias;

	rdrDrawDrawListData(device, draw_list, data, RSBT_SHADOWMAP, pass_num, false, false);

	PERFINFO_AUTO_STOP();
}

void rdrDrawListEndFrame(RdrDrawList *draw_list)
{
	if (!draw_list || !draw_list->current_data)
		return;

	PERFINFO_AUTO_START_FUNC();

	if (draw_list->current_data->device)
		rdrReleaseDrawListData(draw_list->current_data->device, draw_list->current_data);
	else
		rdrReleaseDrawListDataDirect(NULL, &draw_list->current_data, NULL);
	draw_list->current_data = NULL;

	PERFINFO_AUTO_STOP();
}

void rdrDrawListGetForegroundStats(RdrDrawList *draw_list, 
						 		   int *objects_drawn, int *triangles_drawn, 
						 		   int *prepass_objects_drawn, int *prepass_triangles_drawn,
						 		   int *shadow_objects_drawn, int *shadow_triangles_drawn,
						 		   int *hdr_objects_drawn, int *hdr_triangles_drawn)
{
	RdrDrawListData *data = draw_list->current_data;

	if (!data)
		return;

	if (objects_drawn)
		*objects_drawn = data->fg_stats.pass_stats[RDRSHDM_VISUAL].opaque_objects_drawn + data->fg_stats.pass_stats[RDRSHDM_VISUAL].alpha_objects_drawn;

	if (prepass_objects_drawn)
		*prepass_objects_drawn = data->fg_stats.pass_stats[RDRSHDM_ZPREPASS].opaque_objects_drawn + data->fg_stats.pass_stats[RDRSHDM_ZPREPASS].alpha_objects_drawn;

	if (shadow_objects_drawn)
		*shadow_objects_drawn = data->fg_stats.pass_stats[RDRSHDM_SHADOW].opaque_objects_drawn + data->fg_stats.pass_stats[RDRSHDM_SHADOW].alpha_objects_drawn;

	if (hdr_objects_drawn)
		*hdr_objects_drawn = data->fg_stats.pass_stats[RDRSHDM_VISUAL_HDR].opaque_objects_drawn + data->fg_stats.pass_stats[RDRSHDM_VISUAL_HDR].alpha_objects_drawn;

	if (triangles_drawn)
		*triangles_drawn = data->fg_stats.pass_stats[RDRSHDM_VISUAL].opaque_triangles_drawn + data->fg_stats.pass_stats[RDRSHDM_VISUAL].alpha_triangles_drawn;

	if (prepass_triangles_drawn)
		*prepass_triangles_drawn = data->fg_stats.pass_stats[RDRSHDM_ZPREPASS].opaque_triangles_drawn + data->fg_stats.pass_stats[RDRSHDM_ZPREPASS].alpha_triangles_drawn;

	if (shadow_triangles_drawn)
		*shadow_triangles_drawn = data->fg_stats.pass_stats[RDRSHDM_SHADOW].opaque_triangles_drawn + data->fg_stats.pass_stats[RDRSHDM_SHADOW].alpha_triangles_drawn;

	if (hdr_triangles_drawn)
		*hdr_triangles_drawn = data->fg_stats.pass_stats[RDRSHDM_VISUAL_HDR].opaque_triangles_drawn + data->fg_stats.pass_stats[RDRSHDM_VISUAL_HDR].alpha_triangles_drawn;
}

void rdrDrawListAddStats(RdrDrawList *draw_list, RdrDrawListStats *stats)
{
	int i, j;

	if (!draw_list)
		return;

	for (j = 0; j < RDRSHDM_COUNT; ++j)
	{
#if RDR_ENABLE_DRAWLIST_HISTOGRAMS
		for (i = 0; i < RDR_DEPTH_BUCKET_COUNT; ++i)
			stats->pass_stats[j].depth_histogram[i] += draw_list->stats.pass_stats[j].depth_histogram[i];
		for (i = 0; i < RDR_SIZE_BUCKET_COUNT; ++i)
			stats->pass_stats[j].size_histogram[i] += draw_list->stats.pass_stats[j].size_histogram[i];
#endif

		for (i = 0; i < ROC_COUNT; ++i)
		{
			stats->pass_stats[j].objects_drawn[i] += draw_list->stats.pass_stats[j].objects_drawn[i];
			stats->pass_stats[j].triangles_drawn[i] += draw_list->stats.pass_stats[j].triangles_drawn[i];
		}
		stats->pass_stats[j].opaque_objects_drawn += draw_list->stats.pass_stats[j].opaque_objects_drawn;
		stats->pass_stats[j].alpha_objects_drawn += draw_list->stats.pass_stats[j].alpha_objects_drawn;
		stats->pass_stats[j].opaque_triangles_drawn += draw_list->stats.pass_stats[j].opaque_triangles_drawn;
		stats->pass_stats[j].alpha_triangles_drawn += draw_list->stats.pass_stats[j].alpha_triangles_drawn;
	}

	stats->opaque_instanced_objects += draw_list->stats.opaque_instanced_objects;
	stats->alpha_instanced_objects += draw_list->stats.alpha_instanced_objects;

	stats->failed_draw_this_frame += draw_list->stats.failed_draw_this_frame;

	ZeroStructForce(&draw_list->stats);
}

static int rdrDrawListPassRawObjectCount(RdrDrawList *draw_list, int pass_num)
{
	int count = 0;
	int j;
	RdrDrawListData *data = draw_list->current_data;

	if (!data)
		return 0;

	if (pass_num >= 0 && pass_num < data->pass_count)
	{
		RdrDrawListPassData *pass_data = &data->pass_data[pass_num];
		for (j = 0; j < RSBT_COUNT; ++j)
			count += eaSize(&pass_data->sort_node_buckets[j].sort_nodes);
	}

	return count;
}

int rdrDrawListPassObjectCount(RdrDrawList *draw_list, int pass_num)
{
	int count = 0;
	int j;
	RdrDrawListData *data = draw_list->current_data;

	if (!data)
		return 0;

	if (pass_num >= 0 && pass_num < data->pass_count)
	{
		RdrDrawListPassData *pass_data = &data->pass_data[pass_num];
		if (pass_data->shader_mode == RDRSHDM_VISUAL_HDR && !pass_data->owned_by_thread)
			sortHDRSortBuckets(data, pass_data);
		for (j = 0; j < RSBT_COUNT; ++j)
			count += eaSize(&pass_data->sort_node_buckets[j].sort_nodes);
	}

	return count;
}

int rdrDrawListObjectCount(RdrDrawList *draw_list)
{
	int count = 0;
	int i, j;
	RdrDrawListData *data = draw_list->current_data;

	if (!data)
		return 0;

	for (i = 0; i < data->pass_count; ++i)
	{
		RdrDrawListPassData *pass_data = &data->pass_data[i];
		if (pass_data->shader_mode == RDRSHDM_VISUAL_HDR && !pass_data->owned_by_thread)
			sortHDRSortBuckets(data, pass_data);
		for (j = 0; j < RSBT_COUNT; ++j)
			count += eaSize(&pass_data->sort_node_buckets[j].sort_nodes);
	}

	return count;
}

int rdrDrawListIsEmpty(RdrDrawList *draw_list)
{
	return rdrDrawListObjectCount(draw_list)==0;
}

bool rdrDrawListSortBucketIsEmpty(SA_PARAM_NN_VALID RdrDrawList *draw_list, RdrSortBucketType sbt)
{
	int i;
	RdrDrawListData *data = draw_list->current_data;

	if (!data)
		return true;

	for (i = 0; i < data->pass_count; ++i)
	{
		RdrDrawListPassData *pass_data = &data->pass_data[i];
		if (pass_data->shader_mode == RDRSHDM_VISUAL_HDR && !pass_data->owned_by_thread)
			sortHDRSortBuckets(data, pass_data);
		if (eaSize(&pass_data->sort_node_buckets[sbt].sort_nodes))
			return false;
	}

	return true;
}

int rdrDrawListHasAuxPassObjects(RdrDrawList *draw_list)
{
	int i;
	RdrDrawListData *data = draw_list->current_data;
	if (!draw_list->aux_visual_pass || !data)
		return false;

	for (i = 0; i < data->pass_count; ++i)
	{
		RdrDrawListPassData *pass_data = &data->pass_data[i];
		if (pass_data->shader_mode == RDRSHDM_TARGETING)
		{
			return rdrDrawListPassRawObjectCount(draw_list, i);
		}
	}

	return false;
}

void rdrSetDrawListSurfaceTexHandleFixup(SA_PARAM_NN_VALID RdrDrawList *draw_list, int surface_fixup_index, 
										 TexHandle fixedup_surface_texture)
{
	if (surface_fixup_index >= RDR_FIRST_SURFACE_FIXUP_TEXTURE && surface_fixup_index <= RDR_LAST_SURFACE_FIXUP_TEXTURE)
	{
		draw_list->current_data->surface_handle_fixup[surface_fixup_index] = fixedup_surface_texture;
	}
}

int rdrDrawListNeedsScreenGrab(RdrDrawList *draw_list)
{
	return draw_list->need_texture_screen_color || draw_list->need_texture_screen_depth;
}

int rdrDrawListNeedsBlurredScreenGrab(RdrDrawList *draw_list)
{
	return draw_list->need_texture_screen_color_blurred;
}


int rdrDrawListNeedsScreenGrabLate(RdrDrawList *draw_list)
{
	return (draw_list->need_texture_screen_color_late || draw_list->need_texture_screen_depth_late);
// 			|| !rdrDrawListSortBucketIsEmpty(draw_list, RSBT_ALPHA_LOW_RES_ALPHA)
// 			|| !rdrDrawListSortBucketIsEmpty(draw_list, RSBT_ALPHA_LOW_RES_ADDITIVE)
// 			|| !rdrDrawListSortBucketIsEmpty(draw_list, RSBT_ALPHA_LOW_RES_SUBTRACTIVE));
}

int rdrDrawListNeedsScreenDepthGrab(RdrDrawList *draw_list)
{
	return (draw_list->need_texture_screen_depth || draw_list->need_texture_screen_depth_late
			|| !rdrDrawListSortBucketIsEmpty(draw_list, RSBT_ALPHA_LOW_RES_ALPHA)
			|| !rdrDrawListSortBucketIsEmpty(draw_list, RSBT_ALPHA_LOW_RES_ADDITIVE)
			|| !rdrDrawListSortBucketIsEmpty(draw_list, RSBT_ALPHA_LOW_RES_SUBTRACTIVE));
}


/////////////////////////////////////////////////////////////////////////

__forceinline static int cmpLightTypes(const RdrLight **light1, const RdrLight **light2)
{
	if (!(*light2))
		return -1;
	if (!(*light1))
		return 1;
	if (((*light2)->light_type & RDRLIGHT_SHADOWED) != ((*light1)->light_type & RDRLIGHT_SHADOWED))
		return ((*light2)->light_type & RDRLIGHT_SHADOWED) - ((*light1)->light_type & RDRLIGHT_SHADOWED);
	return ((int)(*light1)->light_type) - ((int)(*light2)->light_type);
}

void rdrSortLights(RdrLightParams *light_params)
{
	int i, j;

	for (i = 0; i < MAX_NUM_OBJECT_LIGHTS; ++i)
	{
		for (j = i+1; j < MAX_NUM_OBJECT_LIGHTS; ++j)
		{
			if (cmpLightTypes(&light_params->lights[i], &light_params->lights[j]) > 0)
			{
				RdrLight *temp = light_params->lights[i];
				light_params->lights[i] = light_params->lights[j];
				light_params->lights[j] = temp;
			}
		}
	}
}

typedef struct GfxLight GfxLight;

void checkRdrLight(RdrLight* l);

__forceinline static RdrLightList *allocLightList(RdrDrawListData *data, const RdrLightList *light_list)
{
	RdrLightList *ret=NULL;
	U32 uiIndex;
	U32 uiHash = hashLightList(light_list,DEFAULT_HASH_SEED);
	if (light_list->vertex_light || !stashFindPointerDirect(data->light_list_hashtable, light_list, uiHash, &ret, &uiIndex))
	{
		ret = linAlloc(data->allocator, sizeof(RdrLightList));
		*ret = *light_list;
		linAllocDone(data->allocator);
		if (!light_list->vertex_light)
			stashAddPointerDirect(data->light_list_hashtable, ret, uiIndex, ret);
	}

	return ret;
}

static __forceinline bool rdrIsLightOn(const RdrLight *light)
{
	return light->fade_out != 0.0f;
}

__forceinline static bool lightFits(RdrLightType actual_light, RdrLightType combo_light, bool using_shadowbuffer)
{
	actual_light &= ~RDRLIGHT_DELETING;
	if (using_shadowbuffer)
		actual_light &= ~RDRLIGHT_SHADOWED; // Shadowbuffered shadowed lights fit in a non-shadowed shader
	if ((actual_light & RDRLIGHT_SHADOWED) != (combo_light & RDRLIGHT_SHADOWED))
		return false;
	if (actual_light & RDRLIGHT_SHADOWED) // Both shadowed
	{
		// Spot fits trivially into projector (identical shadowing code)
		if (rdrGetSimpleLightType(actual_light) == RDRLIGHT_SPOT && rdrGetSimpleLightType(combo_light) == RDRLIGHT_PROJECTOR)
			return true;
		// Directional can be packed into spot/projector, with loss of cascaded shadow maps
		if (rdrGetSimpleLightType(actual_light) == RDRLIGHT_DIRECTIONAL && (
			rdrGetSimpleLightType(combo_light) == RDRLIGHT_SPOT ||
			rdrGetSimpleLightType(combo_light) == RDRLIGHT_PROJECTOR))
			return true;
		return actual_light == combo_light;  // Must be identical otherwise (point packs into nothing)
	} else {
		return actual_light <= combo_light;
	}
}

__forceinline static bool lightsFitSimple(const RdrLight * const lights[MAX_NUM_OBJECT_LIGHTS], const PreloadedLightCombo *light_combo, bool using_shadowbuffer, int remap[MAX_NUM_OBJECT_LIGHTS])
{
	int i;
	for (i=0; i<MAX_NUM_OBJECT_LIGHTS; i++)
	{
		const RdrLight *light = lights[i];
		remap[i] = i;
		if (!light)
			continue;
		assert(light->light_type != RDRLIGHT_NONE); // should just be NULL light
		if (light_combo->light_type[i] == RDRLIGHT_NONE)
			return false;
		if (!lightFits(light->light_type, light_combo->light_type[i], using_shadowbuffer || i > 0))
			return false;
	}
	return true;
}

__forceinline static bool lightsFit(const RdrLight * const lights[MAX_NUM_OBJECT_LIGHTS], const PreloadedLightCombo *light_combo, bool using_shadowbuffer, int remap[MAX_NUM_OBJECT_LIGHTS])
{
	int i;
	int remap_used[MAX_NUM_OBJECT_LIGHTS] = {0};
	int remap_used_any_index=0;

	int iFirstActiveLight = -1;

	for (i=0; i<MAX_NUM_OBJECT_LIGHTS; i++)
	{
		if (lights[i] && rdrIsLightOn(lights[i]))
		{
			iFirstActiveLight = i;
			break;
		}
	}

	// If not using shadowbuffer, the shadowedness of the first light must match,
	// If using shadowbuffer, the light combo must not be for a shadowed light
	if (iFirstActiveLight >= 0 &&
		((light_combo->light_type[0] & RDRLIGHT_SHADOWED) !=
		 (using_shadowbuffer ? 0 : (lights[iFirstActiveLight]->light_type & RDRLIGHT_SHADOWED))
		 ))
		 return false;

	for (i=0; i<MAX_NUM_OBJECT_LIGHTS; i++)
	{
		const RdrLight *light = lights[i];
		if (!light || !rdrIsLightOn(light))
		{
			// Map into the last available slot, in case any of the rest of the lights are not blank.
			// This can happen when a light is off because it is "faded out" completely
			for (remap_used_any_index = MAX_NUM_OBJECT_LIGHTS-1; remap_used_any_index >= 0; --remap_used_any_index)
			{
				if (!remap_used[remap_used_any_index])
				{
					remap[remap_used_any_index] = i;
					remap_used[remap_used_any_index] = 1;
					break;
				}
			}
			assert(remap_used_any_index >= 0);
			continue;
		}
		if (!remap_used[0] && !using_shadowbuffer && (light->light_type & RDRLIGHT_SHADOWED))
		{
			// First light is shadowed, must fit into first slot
			if (!lightFits(light->light_type, light_combo->light_type[0], false))
				return false;
			remap[0] = i;
			remap_used[0] = 1;
		} else {
			// Find the simplest light type which can match our light type
			// Simplest light types are sorted first
			// In theory, this is only going to move around the light at index 1, which might be a complicated shadowed light
			int j;
			RdrLightType light_type = rdrGetSimpleLightType(lights[i]->light_type);
			assert(light_type != RDRLIGHT_NONE); // should just be NULL light
			for (j=0; j<MAX_NUM_OBJECT_LIGHTS; j++)
			{
				if (!remap_used[j])
				{
					assert(!(light_combo->light_type[j] & RDRLIGHT_SHADOWED));
					if (light_type <= light_combo->light_type[j])
					{
						remap[j] = i;
						remap_used[j] = 1;
						break;
					}
				}
			}
			if (j == MAX_NUM_OBJECT_LIGHTS)
				return false; // Didn't find one
		}
	}
	return true;
}


bool rdrDrawListLightsFit(const RdrLight * const lights[MAX_NUM_OBJECT_LIGHTS], const PreloadedLightCombo *light_combo, bool using_shadowbuffer)
{
	int remap[MAX_NUM_OBJECT_LIGHTS];
	return lightsFit(lights, light_combo, using_shadowbuffer, remap);
}


static RdrLight black_light[16];

AUTO_RUN_EARLY;
void rdrInitBlackLightData(void)
{
	int i, j;
	for (i=0; i<RDRLIGHT_TYPE_MAX; i++)
	{
		for (j=0; j<2; j++)
		{
			RdrLightType my_light_type = i | (j?RDRLIGHT_SHADOWED:0);
			RdrLight *light = &black_light[my_light_type];
			light->light_type = my_light_type;
			copyMat3(unitmat, light->world_mat);

			// This just prevents a divide-by-zero error later.
			light->point_spot_params.outer_radius = 1;
			light->point_spot_params.angular_falloff[0] = 1;
			light->point_spot_params.angular_falloff[1] = 0.5;
			{
				int k;
				for(k = 0; k < 4; k++) {
					light->projector_params.angular_falloff[k] = k % 2;
				}
			}
		}
	}
}

static RdrLight *getBlackLight(RdrLightType light_type)
{
	return &black_light[light_type];
}

struct {
	RdrMaterialShader light_shader;
	PreloadedLightCombo *combo;
	int remap[MAX_NUM_OBJECT_LIGHTS];
} light_combo_cache[4];
int light_combo_cache_hit1, light_combo_cache_hit2, light_combo_cache_miss;

void lightComboClearCache(void)
{
	ZeroStruct(&light_combo_cache);
}

StashTable stLightComboUsage;

StashTable rdrDrawListGetLightComboUsage(void)
{
	return stLightComboUsage;
}

static int getLightComboRecordUsageKey(RdrMaterialShader light_shader_num)
{
	return light_shader_num.lightMask | ((light_shader_num.shaderMask & MATERIAL_SHADER_SHADOW_BUFFER) ? LIGHT_MATERIAL_SHADER_SHADOW_BUFFER : 0);
}

static void lightComboRecordUsage(RdrMaterialShader light_shader_num)
{
	int count=0;
	int key = getLightComboRecordUsageKey(getRdrMaterialShaderByKey(light_shader_num.key))+1;
	if (!stLightComboUsage)
		stLightComboUsage = stashTableCreateInt(64);
	
	stashIntFindInt(stLightComboUsage, key, &count);
	count++;
	stashIntAddInt(stLightComboUsage, key, count, true);
}

__forceinline int rdrLightTypeValid(RdrLightType light_type)
{
	return light_type >= 0 && light_type <= (RDRLIGHT_MASK | (1 << RDRLIGHT_DELETING));
}

static PreloadedLightCombo *findBestLightCombo(const RdrLight * const lights[MAX_NUM_OBJECT_LIGHTS], bool using_shadowbuffer, int remap[MAX_NUM_OBJECT_LIGHTS])
{
	PreloadedLightCombo *best_light_combo=NULL;

	// Check cache
	int i;
	RdrMaterialShader light_shader_num;
	
	light_shader_num.lightMask = 0;
	light_shader_num.shaderMask = using_shadowbuffer?MATERIAL_SHADER_SHADOW_BUFFER:0;
	
	PERFINFO_AUTO_START_FUNC_L2();

	for (i=0; i<MAX_NUM_OBJECT_LIGHTS; i++)
	{
		const RdrLight *light = lights[i];
		if (light && rdrIsLightOn(light))
		{
#if TRACK_BAD_LIGHTS
			// TODO DJR - remove after tracking down [COR-9003]
			if (light->light_type & RDRLIGHT_DELETING)
				// Not necessarily fatal, since the light will not actually be deleted.
				// But informative for tracking cases of code deleting lights that are
				// being queued this frame, to restructure the code
				memlog_printf(NULL, "Using deleting RdrLight at queue time 0x%p %d\n", light, i);
			devassertmsgf(rdrLightTypeValid(light->light_type), "Using bad RdrLight at queue time 0x%p\n", light);
#endif
			light_shader_num.lightMask |= rdrGetMaterialShaderType(light->light_type, i);
		}
	}

	if (rdr_state.cclightingStats)
		lightComboRecordUsage(light_shader_num);

	if (light_shader_num.key)
	{
		if (light_shader_num.key == light_combo_cache[0].light_shader.key)
		{
			light_combo_cache_hit1++;
			memcpy(remap, light_combo_cache[0].remap, sizeof(remap[0])*MAX_NUM_OBJECT_LIGHTS);
			PERFINFO_AUTO_STOP_L2();
			return light_combo_cache[0].combo;
		}
		for (i=1; i<ARRAY_SIZE(light_combo_cache); i++)
		{
			if (light_combo_cache[i].light_shader.key == light_shader_num.key)
			{
				int j;
				int k;
				light_combo_cache_hit2++;
				best_light_combo = light_combo_cache[i].combo;
				memcpy(remap, light_combo_cache[i].remap, sizeof(remap[0])*MAX_NUM_OBJECT_LIGHTS);
				if (i == ARRAY_SIZE(light_combo_cache)-1)
				{
					for (j=i; j; j--)
					{
						SWAP64(light_combo_cache[j-1].light_shader.key, light_combo_cache[j].light_shader.key);
						SWAPP(light_combo_cache[j-1].combo, light_combo_cache[j].combo);
						for (k=0; k<MAX_NUM_OBJECT_LIGHTS; k++)
						{
							SWAP32(light_combo_cache[j-1].remap[k], light_combo_cache[j].remap[k]);
						}
					}
				}
				PERFINFO_AUTO_STOP_L2();
				return best_light_combo;
			}
		}
		light_combo_cache_miss++;
	}

	// Find cheapest light combo that fits
	// If these are shadowed, but shadowbuffer is on, we should fit in non-shadowed lights.
	FOR_EACH_IN_EARRAY_FORWARDS(preloaded_light_combos, PreloadedLightCombo, light_combo)
	{
		if (lightsFit(lights, light_combo, using_shadowbuffer, remap))
		{
			best_light_combo = light_combo;
			break;
		}
	}
	FOR_EACH_END;
	if (best_light_combo)
	{
		CopyStructsFromOffset(&light_combo_cache[1], -1, ARRAY_SIZE(light_combo_cache)-1);
		light_combo_cache[0].combo = best_light_combo;
		light_combo_cache[0].light_shader = light_shader_num;
		memcpy(light_combo_cache[0].remap, remap, sizeof(remap[0])*MAX_NUM_OBJECT_LIGHTS);
	}

	PERFINFO_AUTO_STOP_L2();
	return best_light_combo;
}

static void downGradeLights(RdrLightParams * light_params)
{
	if (light_params->lights[0] && light_params->lights[0]->light_type == ( RDRLIGHT_PROJECTOR | RDRLIGHT_SHADOWED ) &&
		light_params->lights[1] && (light_params->lights[1]->light_type & ~RDRLIGHT_SHADOWED) == RDRLIGHT_PROJECTOR &&
		light_params->lights[2] && (light_params->lights[2]->light_type & ~RDRLIGHT_SHADOWED) == RDRLIGHT_PROJECTOR &&
		light_params->lights[3] && light_params->lights[3]->light_type == RDRLIGHT_PROJECTOR &&
		light_params->lights[4] && light_params->lights[4]->light_type == RDRLIGHT_PROJECTOR)
	{
		// five projector lights requested, with first needing shadows.
		// downgrade lowest-priority remaining shadowed projector light to spot light
		// and remove all other shadows
		light_params->lights[1]->light_type &= ~RDRLIGHT_SHADOWED;
		light_params->lights[2]->light_type &= ~RDRLIGHT_SHADOWED;
		light_params->lights[3]->light_type &= ~RDRLIGHT_SHADOWED;
		light_params->lights[4]->light_type = RDRLIGHT_SPOT;
	}
}

static RdrLightList *getLightList(RdrDrawList *draw_list, const RdrLightParams *light_params, const Vec3 ambient_offset, const Vec3 ambient_multiplier, bool disable_vertex_light, bool using_shadowbuffer)
{
	RdrDrawListData *data = draw_list->current_data;
	RdrLightList local_light_list = {0};
	int i;
	int remap[MAX_NUM_OBJECT_LIGHTS] = { 0, 1, 2, 3, 4 };
	RdrLightList * pReturnList;

	//PERFINFO_AUTO_START_FUNC();
	// assumes lights are already sorted with rdrSortLights

	copyVec3(ambient_offset, local_light_list.ambient_offset);
	copyVec3(ambient_multiplier, local_light_list.ambient_multiplier);

	// 23 is an arbitrary number here.
	local_light_list.comparator.ambient_offset[0] = (U8)(ambient_offset[0]*23.0f);
	local_light_list.comparator.ambient_offset[1] = (U8)(ambient_offset[1]*23.0f);
	local_light_list.comparator.ambient_offset[2] = (U8)(ambient_offset[2]*23.0f);
	local_light_list.comparator.ambient_multiplier[0] = (U8)(ambient_multiplier[0]*23.0f);
	local_light_list.comparator.ambient_multiplier[1] = (U8)(ambient_multiplier[1]*23.0f);
	local_light_list.comparator.ambient_multiplier[2] = (U8)(ambient_multiplier[2]*23.0f);
	local_light_list.comparator.light_color_type = RLCT_WORLD;

	if (light_params)
	{
		local_light_list.comparator.light_color_type = light_params->light_color_type;

		// Calculate light_shader_num (exactly equal to lights, or quantized for light_combos)
		if (draw_list->light_mode == RDLLM_CCLIGHTING)
		{
			const PreloadedLightCombo *best_light_combo;
			int combo_light_mask = 0;
			best_light_combo = findBestLightCombo(light_params->lights, using_shadowbuffer, remap);

			if (best_light_combo)
				combo_light_mask = best_light_combo->light_mask;
			else
			{
				static RdrLightParams downgrade_light_params = { 0 };

				// attempt to gracefully downgrade light request by converting all-projector combos like:
				// Proj0 + shad, Proj1 + shad, Proj2 + shad, Proj3, Proj4
				// to:
				// Proj0 + shad, Proj1, Proj2, Proj3, Spot4
				downgrade_light_params = *light_params;
				light_params = &downgrade_light_params;
				downGradeLights(&downgrade_light_params);

				best_light_combo = findBestLightCombo(downgrade_light_params.lights, using_shadowbuffer, remap);

				if (best_light_combo)
					combo_light_mask = best_light_combo->light_mask;
				else
				{
					// as a last option, assume all projectors, no shadows, to ensure we have something
					// to light the object.
					combo_light_mask = (RDRLIGHT_PROJECTOR << MATERIAL_SHADER_LIGHT0_OFFSET) |
						(RDRLIGHT_PROJECTOR << MATERIAL_SHADER_LIGHT1_OFFSET) |
						(RDRLIGHT_PROJECTOR << MATERIAL_SHADER_LIGHT2_OFFSET) |
						(RDRLIGHT_PROJECTOR << MATERIAL_SHADER_LIGHT3_OFFSET) |
						(RDRLIGHT_PROJECTOR << MATERIAL_SHADER_LIGHT4_OFFSET);
				}
			}
			local_light_list.light_shader_num.lightMask = combo_light_mask;
		}
		else
		{
			for (i = 0; i < MAX_NUM_OBJECT_LIGHTS; ++i)
			{
				RdrLight *light = light_params->lights[i];
				remap[i] = i;

				if (!light || !rdrIsLightOn(light))
					break;
				local_light_list.light_shader_num.lightMask |= rdrGetMaterialShaderType(light->light_type, i);
			}
		}

		for (i = 0; i < MAX_NUM_OBJECT_LIGHTS; ++i)
		{
			RdrLightSuperData *super_data;
			RdrLight *light = light_params->lights[remap[i]];
			RdrLightType light_type;
			int iInsertPos;

			// Some lights will have been promoted in findBestLightCombo due to the choices we've made in data\client\LightCombos.txt
			light_type = rdrGetLightType(local_light_list.light_shader_num.lightMask, i);

			if (!light || !rdrIsLightOn(light))
			{
				if (light_type != RDRLIGHT_NONE)
					light = getBlackLight(light_type);
				else
					break;
			}

			if (!sortedArrayFindValue(data->light_hashtable,light, &super_data,&iInsertPos))
			{
				if (sortedArrayIsFull(data->light_hashtable))
				{
					// don't crash.  TODO - hook up an alarm here
					continue;
				}
					
				assert(!data->sent_lights);
				// Lights allocated here and filled in in rdrDrawListSendLightsToRenderer and rdrDrawListSendVisualPassesToRenderer
				super_data = linAlloc(data->allocator, sizeof(RdrLightSuperData));

				sortedArrayAddValueAtKnownPosition(data->light_hashtable, light, super_data, iInsertPos);
			}

			assert(light_type > 0 && light_type < ARRAY_SIZE(super_data->light_data));

			// This array handles all the different types of light this light might have been promoted to in findBestLightCombo.
			// It's very sparse.
			if (!super_data->light_data[light_type])
				super_data->light_data[light_type] = linAlloc(data->allocator, sizeof(RdrLightData));
			local_light_list.lights[i] = super_data->light_data[light_type];

			// compressing the light data
			local_light_list.comparator.lights[i] = (U16)local_light_list.lights[i];

			local_light_list.light_contribution[0] += vec3MaxComponent(light->light_colors[light_params->light_color_type].ambient);
			local_light_list.light_contribution[0] += vec3MaxComponent(light->light_colors[light_params->light_color_type].diffuse);
			local_light_list.light_contribution[1] += vec3MaxComponent(light->light_colors[light_params->light_color_type].specular);
		}

		if (light_params->ambient_light)
		{
			Vec3 ambient_color, sky_values;
			int iInsertPos;

			if (!sortedArrayFindValue(data->ambient_light_hashtable,light_params->ambient_light, &local_light_list.ambient_light,&iInsertPos))
			{
				if (!sortedArrayIsFull(data->light_hashtable))
				{
					assert(!data->sent_lights);
					local_light_list.ambient_light = linAlloc(data->allocator, sizeof(RdrAmbientLight));
					sortedArrayAddValueAtKnownPosition(data->ambient_light_hashtable, light_params->ambient_light, local_light_list.ambient_light, iInsertPos);
				}
			}

			local_light_list.comparator.ambient_light = (U16)local_light_list.ambient_light;

			mulVecVec3(ambient_multiplier, light_params->ambient_light->ambient[light_params->light_color_type], ambient_color);
			addVec3(ambient_offset, ambient_color, ambient_color);
			setVec3(sky_values, vec3MaxComponent(light_params->ambient_light->sky_light_color_front[light_params->light_color_type]),
				vec3MaxComponent(light_params->ambient_light->sky_light_color_back[light_params->light_color_type]),
				vec3MaxComponent(light_params->ambient_light->sky_light_color_side[light_params->light_color_type]));

			local_light_list.light_contribution[0] += vec3MaxComponent(ambient_color) + vec3MaxComponent(sky_values);
		}

		if (light_params->vertex_light && !disable_vertex_light)
		{
			local_light_list.vertex_light = linAlloc(data->allocator, sizeof(RdrVertexLight));
			CopyStructs(local_light_list.vertex_light, light_params->vertex_light, 1);

			local_light_list.light_contribution[0] += light_params->vertex_light->vlight_offset + light_params->vertex_light->vlight_multiplier;
		}
	}
	
	local_light_list.comparator.use_vertex_only_lighting = draw_list->light_mode == RDLLM_VERTEX_ONLY;
	local_light_list.comparator.use_single_dirlight = (draw_list->light_mode != RDLLM_UBERLIGHTING || !rdr_state.noSimpleDirLightShader) && rdrIsSingleDirectionalLight(local_light_list.light_shader_num.lightMask) && !local_light_list.comparator.use_vertex_only_lighting;

	pReturnList = allocLightList(data, &local_light_list);

	//PERFINFO_AUTO_STOP();

	return pReturnList;
}

__forceinline static RdrMaterial *copyMaterial(RdrDrawListData *data, RdrMaterial *src_material)
{
	RdrMaterial *dst_material;
	int iInsertPos;
	if (!sortedArrayFindValue(data->material_hashtable, src_material, &dst_material,&iInsertPos))
	{
		if (sortedArrayIsFull(data->material_hashtable))
			return NULL;

		assert(!data->sent_materials);
		dst_material = linAlloc(data->allocator, sizeof(RdrMaterial));
		// stage 1 of the copy, just copy the flags and other data that is available at this time
		memcpy_intrinsic(dst_material, src_material, sizeof(RdrMaterial));
		linAllocDone(data->allocator);
		sortedArrayAddValueAtKnownPosition(data->material_hashtable, src_material, dst_material, iInsertPos);
	}
	return dst_material;
}

__forceinline static void initGeo(RdrDrawableGeo *draw, RdrDrawListData *data, RdrGeometryType draw_type, int material_count, int num_vs_constants, int num_vs_textures)
{
	MAX1(material_count, 1);
	assert(material_count < 256);
	MAX1(num_vs_constants, 0);
	assert(num_vs_constants < 256);

	draw->base_drawable.draw_type = draw_type;
	draw->subobject_count = material_count;
	draw->num_vertex_shader_constants = num_vs_constants;
	draw->num_vertex_textures = num_vs_textures;
	if (draw->num_vertex_shader_constants)
		draw->vertex_shader_constants = linAlloc(data->allocator, sizeof(Vec4) * draw->num_vertex_shader_constants);
	if (draw->num_vertex_textures)
		draw->vertex_textures = linAlloc(data->allocator, sizeof(TexHandle) * draw->num_vertex_textures);
}

#define NEED_DOF (bucket_params.zdist > data->dof_distance)

static RdrSortBucketType determineSortBucket(RdrDrawList *draw_list, RdrMaterial *material, RdrSortType type, U8 alphaU8, F32 zdist, RdrMaterialFlags extra_flags, bool force_no_outlining, bool no_zprepass, bool need_dof, bool needs_late_alpha_pass_if_need_grab)
{
	RdrSortBucketType sort_type = sort_type_to_bucket_type[type];
	RdrMaterialFlags effective_flags = (material?material->flags:0)|extra_flags;
	bool is_decal = (effective_flags & RMATERIAL_DECAL) && !rdr_state.disableSeparateDecalRenderBucket;
	bool no_outlining = force_no_outlining || (effective_flags & (RMATERIAL_NONDEFERRED|RMATERIAL_NOZWRITE|RMATERIAL_ALPHA_TO_COVERAGE));

	// JE: I think this only gets called with AUTO or ALPHA
	assert(type == RST_AUTO || type == RST_ALPHA); // TODO: simplify this stuff if we only want alpha vs auto.

	// nozwrite materials should never go into the zprepass or they will not get drawn, because of the depth equal testing
	no_zprepass = no_zprepass || (effective_flags & (RMATERIAL_NOZWRITE|( rdr_state.msaaQuality > 1 ? RMATERIAL_ALPHA_TO_COVERAGE : 0))) || !draw_list->zprepass;

	if (draw_list_debug_state.overridePerMaterialFlags) {
		effective_flags = 0;
	}

	if (material && (material->need_texture_screen_color || material->need_texture_screen_depth || material->need_texture_screen_color_blurred))
	{
		if (sort_type == RSBT_COUNT || sort_type == RSBT_ALPHA) {
			if (needs_late_alpha_pass_if_need_grab && !material->need_texture_screen_color_blurred)
				sort_type = RSBT_ALPHA_NEED_GRAB_LATE;
			else if (material->need_texture_screen_color_blurred)
				sort_type = RSBT_ALPHA_NEED_GRAB;
			else if (rdr_state.alphaInDOF)
				sort_type = RSBT_ALPHA_NEED_GRAB_PRE_DOF;
			else
				sort_type = RSBT_ALPHA_NEED_GRAB;
		}
	}

	if (sort_type == RSBT_ALPHA || sort_type == RSBT_ALPHA_NEED_GRAB_PRE_DOF || sort_type == RSBT_ALPHA_NEED_GRAB || sort_type == RSBT_ALPHA_PRE_DOF || 
		sort_type == RSBT_ALPHA_NEED_GRAB_LATE ||
		(material && material->need_alpha_sort) || alphaU8 < 255 || 
		(effective_flags & (RMATERIAL_ADDITIVE | RMATERIAL_SUBTRACTIVE)))
	{
		// Alpha objects
		if (sort_type == RSBT_COUNT)
			sort_type = RSBT_ALPHA;

		if (sort_type == RSBT_ALPHA)
		{
			if (is_decal)
				sort_type = RSBT_DECAL_POST_GRAB; // MUST be post grab because we don't know which pass the object this is laying on top of will be drawn in
			else if (need_dof && !(effective_flags & RMATERIAL_ALPHA_NO_DOF))
				sort_type = RSBT_ALPHA_PRE_DOF;
			else if (rdrLowResAlphaEnabled())
			{
				if ((effective_flags & RMATERIAL_LOW_RES_ALPHA) && rdr_state.lowResAlphaMinDist < zdist && zdist < rdr_state.lowResAlphaMaxDist)
				{
					if (effective_flags & RMATERIAL_ADDITIVE)
						sort_type = RSBT_ALPHA_LOW_RES_ADDITIVE;
					else if (effective_flags & RMATERIAL_SUBTRACTIVE)
						sort_type = RSBT_ALPHA_LOW_RES_SUBTRACTIVE;
					else
						sort_type = RSBT_ALPHA_LOW_RES_ALPHA;
				}
				else if (zdist < rdr_state.lowResAlphaMaxDist)
					sort_type = RSBT_ALPHA_LATE;
			}

			if ((sort_type == RSBT_ALPHA || sort_type == RSBT_ALPHA_LATE) &&
				(rdr_state.fastParticlesInPreDOF || !needs_late_alpha_pass_if_need_grab) &&
				rdr_state.alphaInDOF &&
				!(effective_flags & RMATERIAL_ALPHA_NO_DOF))
			{
				sort_type = RSBT_ALPHA_PRE_DOF; // Early-pass alpha objects into the pre-dof buffer
			}
		}

	}
	else
	{
		// Opaque
		assert(sort_type == RSBT_COUNT); // Only auto gets here
		// Auto
		if (no_outlining) {
			if (no_zprepass)
				sort_type = RSBT_OPAQUE_POST_GRAB_NO_ZPRE;
			else
				sort_type = RSBT_OPAQUE_POST_GRAB_IN_ZPRE; // + RSBT_ZPREPASS_NO_OUTLINE
		} else {
			if (no_zprepass)
				sort_type = RSBT_OPAQUE_ONEPASS;
			else
				sort_type = RSBT_OPAQUE_PRE_GRAB; // + RSBT_ZPREPASS
		}

		if (is_decal)
			sort_type = RSBT_DECAL_POST_GRAB; // MUST be post grab because we don't know which pass the object this is laying on top of will be drawn in
	}

	if (draw_list_debug_state.overrideSortBucket)
		sort_type = draw_list_debug_state.overrideSortBucketValue;

	if (!draw_list->outlining)
	{
		// merge pre and post opaque buckets if we don't need them
		if (sort_type == RSBT_OPAQUE_POST_GRAB_IN_ZPRE)
			sort_type = RSBT_OPAQUE_PRE_GRAB;
		else if (sort_type == RSBT_DECAL_POST_GRAB)
			sort_type = RSBT_DECAL_PRE_GRAB;
	}

	if (material && material->alpha_pass_only && (sort_type < RSBT_ALPHA_PRE_DOF && (sort_type != RSBT_DECAL_POST_GRAB && sort_type != RSBT_DECAL_PRE_GRAB)))
		sort_type = RSBT_ALPHA;

	return sort_type;
}

__forceinline static RdrSortNode *createSortNode(const RdrGeoBucketParams *bucket_params, RdrSortBucketType sort_type)
{
	RdrSortNode *sort_node;

	assert(sort_type < RSBT_COUNT);
	if (sort_type == RSBT_ALPHA || sort_type == RSBT_ALPHA_NEED_GRAB_PRE_DOF || sort_type == RSBT_ALPHA_NEED_GRAB || sort_type == RSBT_ALPHA_PRE_DOF || sort_type == RSBT_ALPHA_NEED_GRAB_LATE ||
		sort_type == RSBT_ALPHA_LOW_RES_ALPHA || sort_type == RSBT_ALPHA_LOW_RES_SUBTRACTIVE || sort_type == RSBT_ALPHA_LOW_RES_ADDITIVE ||
		sort_type == RSBT_ALPHA_LATE)
	{
		RdrAlphaSortNode *alpha_sort_node = linAlloc(bucket_params->data->allocator, sizeof(RdrAlphaSortNode));
		alpha_sort_node->alpha = bucket_params->alphaU8;
		alpha_sort_node->zdist = bucket_params->zdist;
		sort_node = &alpha_sort_node->base_sort_node;
	}
	else
	{
		sort_node = linAlloc(bucket_params->data->allocator, sizeof(RdrSortNode));
	}

	if (bucket_params->local_sort_node)
		memcpy_intrinsic(sort_node, bucket_params->local_sort_node, sizeof(RdrSortNode));


	// Tessellation requires that some aspects of the geometry be preserved even for something as simple as writing depth.
	if (bucket_params->subobject && bucket_params->subobject->material && bucket_params->subobject->material->flags & RMATERIAL_TESSELLATE) {
		sort_node->add_material_flags |= RMATERIAL_TESSELLATE;
		sort_node->domain_material = bucket_params->subobject->material->tessellation_material;
	}

	linAllocDone(bucket_params->data->allocator);
	sort_node->skyDepth = bucket_params->skyDepth;

	return sort_node;
}

static RdrSortNode *addInstanceToBucket(RdrSortNodeLinkList **sort_nodes_head, U32 *uniqueCount, RdrAddInstanceParams *params, const RdrGeoBucketParams *bucket_params, int pass_idx, RdrSortBucketType sort_type)
{
	RdrDrawListPassData *pass_data = &bucket_params->data->pass_data[pass_idx];
	RdrSortNodeList *sort_node_list = &pass_data->sort_node_buckets[sort_type];
	RdrSortNode *sort_node;
	bool is_new = true;

	PERFINFO_AUTO_START_FUNC_L2();

	if (!bucket_params->local_sort_node->do_instancing)
	{

		sort_node = bucket_params->local_sort_node;

		// JE: Need to make dummy instance to hold per_drawable_data and hdr_contribution
		if (!sort_node->instances)
		{
			 RdrInstanceLinkList *instance_link;
			sort_node->instances = instance_link = linAlloc(bucket_params->data->allocator, sizeof(RdrInstanceLinkList));
			//instance_link->instance = bucket_params->instance;
			instance_link->hdr_contribution = bucket_params->local_sort_node->hdr_contribution;
			//instance_link->next = sort_node->instances;
			instance_link->instance = bucket_params->instance;
			instance_link->count = 1;
			if (params && params->per_drawable_data)
				instance_link->per_drawable_data = *&params->per_drawable_data[bucket_params->local_sort_node->subobject_idx];
			else
				setVec4same(instance_link->per_drawable_data.instance_param, 1);
			linAllocDone(bucket_params->data->allocator);
		}
		if (uniqueCount)
			(*uniqueCount)++;
	}
	else
	{
		RdrInstanceLinkList *instance_link;

		if (sort_type == RSBT_ALPHA || sort_type == RSBT_ALPHA_NEED_GRAB_PRE_DOF || sort_type == RSBT_ALPHA_NEED_GRAB || sort_type == RSBT_ALPHA_PRE_DOF || sort_type == RSBT_ALPHA_LATE ||
			sort_type == RSBT_ALPHA_NEED_GRAB_LATE || sort_type == RSBT_ALPHA_LOW_RES_ALPHA || sort_type == RSBT_ALPHA_LOW_RES_ADDITIVE ||
			sort_type == RSBT_ALPHA_LOW_RES_SUBTRACTIVE)
		{
			sort_node = createSortNode(bucket_params, sort_type);
			if (uniqueCount)
				(*uniqueCount)++;
		}
		else
		{
			RdrSortNodeLinkList *sn_link;

			sort_node = NULL;
			for (sn_link = *sort_nodes_head; sn_link; sn_link = sn_link->next)
			{
				if (sn_link->sort_bucket_type == sort_type && 
					sn_link->pass_idx == pass_idx && 
					rdrSortNodesCanInstance(sn_link->sort_node, bucket_params->local_sort_node) && 
					sn_link->sort_node->tri_count + bucket_params->local_sort_node->tri_count < MAX_SORT_NODE_TRI_COUNT
					)
				{
					sort_node = sn_link->sort_node;
					break;
				}
			}

			if (!sort_node)
			{
				sort_node = createSortNode(bucket_params, sort_type);
				sn_link = linAlloc(bucket_params->data->allocator, sizeof(RdrSortNodeLinkList));
				sn_link->sort_node = sort_node;
				sn_link->sort_bucket_type = sort_type;
				sn_link->pass_idx = pass_idx;
				sn_link->next = *sort_nodes_head;
				linAllocDone(bucket_params->data->allocator);
				*sort_nodes_head = sn_link;
				if (uniqueCount)
					(*uniqueCount)++;
			}
			else
			{
#if ULTRA_INSTANCE_DEBUG
				assert(sort_node->tri_count + bucket_params->local_sort_node->tri_count < MAX_SORT_NODE_TRI_COUNT);
#endif

				is_new = false;
				MAX1(sort_node->hdr_contribution, bucket_params->local_sort_node->hdr_contribution);
				sort_node->tri_count += bucket_params->local_sort_node->tri_count;
				MIN1(sort_node->zbucket, bucket_params->local_sort_node->zbucket);

				++bucket_params->data->stats.opaque_instanced_objects;

#if ULTRA_INSTANCE_DEBUG
				assert(sort_node->draw_type == bucket_params->local_sort_node->draw_type);
				assert(sort_node->geo_handle_primary == bucket_params->local_sort_node->geo_handle_primary);
				assert(sort_node->drawable == bucket_params->local_sort_node->drawable);
				assert(sort_node->material == bucket_params->local_sort_node->add_material_flags);
				assert(sort_node->add_material_flags == bucket_params->local_sort_node->material);
				assert(sort_node->subobject_idx == bucket_params->local_sort_node->subobject_idx);
				assert(sort_node->subobject_count == bucket_params->local_sort_node->subobject_count);
				assert(sort_node->has_transparency == bucket_params->local_sort_node->has_transparency);
				assert(sort_node->uses_shadowbuffer == bucket_params->local_sort_node->uses_shadowbuffer);
				assert(sort_node->force_no_shadow_receive == bucket_params->local_sort_node->force_no_shadow_receive);
				assert(sort_node->uses_far_depth_range == bucket_params->local_sort_node->uses_far_depth_range);
				assert(sort_node->camera_centered == bucket_params->local_sort_node->camera_centered);
				assert(sort_node->debug_me == bucket_params->local_sort_node->debug_me);
				assert(sort_node->lights == bucket_params->local_sort_node->lights);
				assert(sort_node->geo_handle_vertex_light == bucket_params->local_sort_node->geo_handle_vertex_light);
#endif
			}
		}

		instance_link = linAlloc(bucket_params->data->allocator, sizeof(RdrInstanceLinkList));
		instance_link->instance = bucket_params->instance;
		instance_link->hdr_contribution = bucket_params->local_sort_node->hdr_contribution;
		instance_link->next = sort_node->instances;
		if (params && params->per_drawable_data)
			instance_link->per_drawable_data = *&params->per_drawable_data[bucket_params->local_sort_node->subobject_idx];
		else
			setVec4same(instance_link->per_drawable_data.instance_param, 1);
		linAllocDone(bucket_params->data->allocator);
		sort_node->instances = instance_link;
		sort_node->instances->count = 1 + (sort_node->instances->next ? sort_node->instances->next->count : 0);
	}

	assert(!pass_data->owned_by_thread);

	sort_node->skyDepth = bucket_params->skyDepth;
	if (is_new)
		eaPush(&sort_node_list->sort_nodes, sort_node);
	sort_node_list->total_tri_count += bucket_params->local_sort_node->tri_count;

#ifdef _FULLDEBUG
	assert(eaFind(&sort_node_list->sort_nodes, sort_node) >= 0);
#endif

	PERFINFO_AUTO_STOP_L2();

	return sort_node;
}

static int cmpSortNodeHDR(const RdrSortNode **sort_node1, const RdrSortNode **sort_node2)
{
	return (int)(*sort_node2)->hdr_contribution - (int)(*sort_node1)->hdr_contribution;
}

static int cmpInstanceLinkListHDR(const RdrInstanceLinkList **link1, const RdrInstanceLinkList **link2)
{
	return (int)(*link2)->hdr_contribution - (int)(*link1)->hdr_contribution;
}

static void reduceLists(RdrSortNodeList *pre, RdrSortNodeList *post, int draw_call_max, int tri_count_max, int pre_multiplier, int *instanced_counter)
{
	int pre_size = eaSize(&pre->sort_nodes);
	int post_size = eaSize(&post->sort_nodes);
	int draw_call_over = pre_multiplier * pre_size + post_size - draw_call_max;
	int tri_count_over = pre_multiplier * pre->total_tri_count + post->total_tri_count - tri_count_max;

	if (draw_call_over > 0 || tri_count_over > 0)
	{
		// sort nodes by hdr contribution
		eaQSortG(pre->sort_nodes, cmpSortNodeHDR);
		eaQSortG(post->sort_nodes, cmpSortNodeHDR);

		while (draw_call_over > 0 || tri_count_over > 0)
		{
			RdrSortNodeList *list_over;
			RdrSortNode *node_over;
			int *list_size;
			int multiplier;

			assert(pre_size || post_size);

			// figure out which sort node contributes the least to hdr
			if (!pre_size || (post_size && pre->sort_nodes[pre_size-1]->hdr_contribution > post->sort_nodes[post_size-1]->hdr_contribution))
			{
				ANALYSIS_ASSUME(post->sort_nodes);
				list_over = post;
				node_over = post->sort_nodes[post_size-1];
				list_size = &post_size;
				multiplier = 1;
			}
			else
			{
				ANALYSIS_ASSUME(pre->sort_nodes);
				list_over = pre;
				node_over = pre->sort_nodes[pre_size-1];
				list_size = &pre_size;
				multiplier = pre_multiplier;
			}

			if (draw_call_over > 0 || tri_count_over >= multiplier * (int)node_over->tri_count || !node_over->instances || node_over->instances->count == 1)
			{
				// remove lowest contributing sort node entirely
				--(*list_size);
				draw_call_over -= multiplier;
				list_over->total_tri_count -= node_over->tri_count;
				tri_count_over -= multiplier * node_over->tri_count;
				if (node_over->instances)
					*instanced_counter -= node_over->instances->count-1;
				eaSetSize(&list_over->sort_nodes, *list_size);
			}
			else
			{
				// remove instances from the lowest contributing sort node
				int orig_count = node_over->instances->count;
				int per_instance_tri_count = node_over->tri_count / orig_count;
				int remove_count = round(ceilf(multiplier * tri_count_over / per_instance_tri_count));

				if (remove_count == orig_count)
					--remove_count; // we should keep at least one instance around

				assert(remove_count < orig_count);

				if (remove_count > 0)
				{
					RdrInstanceLinkList *link, **instance_list = ScratchAlloc(orig_count * sizeof(RdrInstanceLinkList));
					int i, new_count = orig_count - remove_count;

					// sort instances by hdr contribution
					for (i = 0, link = node_over->instances; link; ++i, link = link->next)
						instance_list[i] = link;
					qsortG(instance_list, orig_count, sizeof(RdrInstanceLinkList *), cmpInstanceLinkListHDR);

					// reorder the linked list and trim, orphaning the rest of the instances
					for (i = 0; i < new_count; ++i)
					{
						instance_list[i]->next = instance_list[i+1];
						instance_list[i]->count = new_count - i;
					}
					instance_list[new_count-1]->next = NULL;
					node_over->instances = instance_list[0];

					ScratchFree(instance_list);

					node_over->tri_count -= remove_count * per_instance_tri_count;
					list_over->total_tri_count -= remove_count * per_instance_tri_count;
					*instanced_counter -= remove_count;
				}

				tri_count_over = 0;
			}
		}
	}

}

static void sortHDRSortBuckets(RdrDrawListData *data, RdrDrawListPassData *pass_data)
{
	PERFINFO_AUTO_START_FUNC();

	assert(!pass_data->owned_by_thread); // can't mess with the sort node earrays if the thread owns them

	// The HDR pass should only be putting things into the OPAQUE_ONEPASS sort bucket, so
	//  the other 3 opaque buckets should all be empty.
	assert(pass_data->sort_node_buckets[RSBT_OPAQUE_POST_GRAB_IN_ZPRE].total_tri_count == 0); // Shouldn't have any in here for this pass... I hope
	assert(pass_data->sort_node_buckets[RSBT_OPAQUE_POST_GRAB_NO_ZPRE].total_tri_count == 0); // Shouldn't have any in here for this pass... I hope
	assert(pass_data->sort_node_buckets[RSBT_OPAQUE_PRE_GRAB].total_tri_count == 0); // Shouldn't have any in here for this pass... I hope

	reduceLists(&pass_data->sort_node_buckets[RSBT_OPAQUE_ONEPASS], &pass_data->sort_node_buckets[RSBT_OPAQUE_PRE_GRAB], 
				rdr_state.maxOpaqueHDRDrawCalls, rdr_state.maxOpaqueHDRTriangles, 1, &data->stats.opaque_instanced_objects);

	reduceLists(&pass_data->sort_node_buckets[RSBT_DECAL_PRE_GRAB], &pass_data->sort_node_buckets[RSBT_DECAL_POST_GRAB], 
		rdr_state.maxDecalHDRDrawCalls, rdr_state.maxDecalHDRTriangles, 1, &data->stats.opaque_instanced_objects);

	// CD: ignore ALPHA_NEED_GRAB for now, I guess
	reduceLists(&pass_data->sort_node_buckets[RSBT_ALPHA_PRE_DOF], &pass_data->sort_node_buckets[RSBT_ALPHA], 
				rdr_state.maxAlphaHDRDrawCalls, rdr_state.maxAlphaHDRTriangles, rdr_state.disable_two_pass_depth_alpha?1:2, &data->stats.alpha_instanced_objects);

	PERFINFO_AUTO_STOP();
}

__forceinline static RdrSortBucketType getEffectiveSortTypeForShaderMode(RdrShaderMode shader_mode, RdrSortBucketType sort_type)
{
	if (shader_mode == RDRSHDM_VISUAL_HDR && 
		(sort_type == RSBT_OPAQUE_ONEPASS || sort_type == RSBT_OPAQUE_PRE_GRAB ||
		sort_type == RSBT_OPAQUE_POST_GRAB_IN_ZPRE || sort_type == RSBT_OPAQUE_POST_GRAB_NO_ZPRE))
	{
		// Combine all opaque passes for the HDR pass
		return RSBT_OPAQUE_ONEPASS;
	}
	else if (shader_mode == RDRSHDM_ZPREPASS)
	{
		bool no_outlines = sort_type == RSBT_OPAQUE_POST_GRAB_IN_ZPRE || sort_type == RSBT_OPAQUE_POST_GRAB_NO_ZPRE;
		return no_outlines ? RSBT_ZPREPASS_NO_OUTLINE : RSBT_ZPREPASS;
	}
	else if (shader_mode == RDRSHDM_SHADOW)
	{
		return RSBT_SHADOWMAP;
	}
	else
	if (shader_mode == RDRSHDM_TARGETING)
	{
		// Combine all passes for the targeting pass
		return RSBT_OPAQUE_ONEPASS;
	}

	return sort_type;
}

static void addToBuckets(RdrAddInstanceParams *params, const RdrGeoBucketParams *bucket_params,
									   RdrSortBucketType sort_type, RdrMaterialShader shader_num, RdrMaterialShader hdr_shader_num,
									   RdrMaterialShader depth_shader_num, 
									   bool ok_in_depth_bucket, bool ok_in_zprepass)
{
	RdrDrawListData *data = bucket_params->data;
	ShaderGraphRenderInfo *graph = bucket_params->subobject->graph_render_info;
	int i;
	RdrMaterialShader lra_high_res_shader_num;

	PERFINFO_AUTO_START_FUNC_L2();

	lra_high_res_shader_num = shader_num;
	if (rdr_state.lowResAlphaHighResNeedsManualDepthTest)
		if (sort_type == RSBT_ALPHA_LOW_RES_ALPHA || sort_type == RSBT_ALPHA_LOW_RES_ADDITIVE || sort_type == RSBT_ALPHA_LOW_RES_SUBTRACTIVE)
			lra_high_res_shader_num.shaderMask |= MATERIAL_SHADER_MANUAL_DEPTH_TEST;

	assert(fillShaderCallback);

	for (i = 0; i < data->pass_count; ++i)
	{
		RdrDrawListPassData *pass_data = &data->pass_data[i];
		ANALYSIS_ASSUME(pass_data->shader_mode < RDRSHDM_COUNT);
		if (params->frustum_visible & pass_data->frustum_set_bit)
		{
			RdrSortBucketType eff_sort_type;

			if ((pass_data->shader_mode == RDRSHDM_VISUAL_HDR && hdr_shader_num.key == -1) ||
				(pass_data->shader_mode == RDRSHDM_TARGETING && !params->aux_visual_pass))
				continue;

			eff_sort_type = getEffectiveSortTypeForShaderMode(pass_data->shader_mode, sort_type);
			if (rdrIsVisualShaderMode(pass_data->shader_mode))
			{
				if (params->wireframe && pass_data->shader_mode != RDRSHDM_VISUAL_HDR)
					addInstanceToBucket(&bucket_params->subobject->opaque_sort_nodes, NULL, params, bucket_params, i, RSBT_WIREFRAME);

				if (params->wireframe != 2)
				{
					RdrSortNode *sort_node;

					sort_node = addInstanceToBucket(&bucket_params->subobject->opaque_sort_nodes, (pass_data->shader_mode == RDRSHDM_VISUAL)?&params->uniqueDrawCount:NULL, params, bucket_params, i, eff_sort_type);
					if (params->aux_visual_pass)
					{
						sort_node->stencil_value = 255;
						sort_node->add_material_flags |= rdrMaterialFlagsPackStencilMode(RDRSTENCILMODE_WRITEVALUE);
					}

					if (!sort_node->draw_shader_handle[pass_data->shader_mode])
					{
						if (pass_data->shader_mode == RDRSHDM_VISUAL_HDR)
							sort_node->draw_shader_handle[pass_data->shader_mode] = fillShaderCallback(graph, hdr_shader_num, bucket_params->local_sort_node->tri_count);
						else
							sort_node->draw_shader_handle[pass_data->shader_mode] = fillShaderCallback(graph, shader_num, bucket_params->local_sort_node->tri_count);
					}

					if (!sort_node->draw_shader_handle[RDRSHDM_VISUAL_LRA_HIGH_RES])
					{
						sort_node->draw_shader_handle[RDRSHDM_VISUAL_LRA_HIGH_RES] = fillShaderCallback(graph, lra_high_res_shader_num, bucket_params->local_sort_node->tri_count);
					}
#if !PLATFORM_CONSOLE
					if (eff_sort_type == RSBT_ALPHA_NEED_GRAB_PRE_DOF || eff_sort_type == RSBT_ALPHA_NEED_GRAB || eff_sort_type == RSBT_ALPHA_NEED_GRAB_LATE)
					{
						if (!sort_node->draw_shader_handle[RDRSHDM_ZPREPASS])
						{
							// On PC, we render depth of the need grab objects in a separate pass, so
							//  we need a Z-prepass shader for them.
							sort_node->draw_shader_handle[RDRSHDM_ZPREPASS] = (depth_shader_num.shaderMask & MATERIAL_SHADER_NOALPHAKILL) ?
								-1 : fillShaderCallback(graph, depth_shader_num, bucket_params->local_sort_node->tri_count);
						}
					}
#endif
				}
			}
			else if (ok_in_depth_bucket)
			{
				if (pass_data->shader_mode == RDRSHDM_ZPREPASS && ok_in_zprepass)
				{
					RdrSortNode *sort_node;
					assert(!params->no_outlining || eff_sort_type == RSBT_ZPREPASS_NO_OUTLINE); // if caller set no_outlining, they need to be using the post_grab bucket + shader

					sort_node = addInstanceToBucket(&bucket_params->subobject->opaque_sort_nodes, NULL, params, bucket_params, i, eff_sort_type);

					if (!sort_node->draw_shader_handle[pass_data->shader_mode])
					{
						assert(pass_data->depth_only);
						sort_node->draw_shader_handle[pass_data->shader_mode] = (depth_shader_num.shaderMask & MATERIAL_SHADER_NOALPHAKILL) ?
							-1 : fillShaderCallback(graph, depth_shader_num, bucket_params->local_sort_node->tri_count);
					}
				}
				else if (pass_data->shader_mode == RDRSHDM_SHADOW)
				{
					RdrSortNode *sort_node = addInstanceToBucket(&bucket_params->subobject->opaque_sort_nodes, NULL, params, bucket_params, i, eff_sort_type);

					if (!sort_node->draw_shader_handle[pass_data->shader_mode])
					{
						assert(pass_data->depth_only);
						sort_node->draw_shader_handle[pass_data->shader_mode] = (depth_shader_num.shaderMask & MATERIAL_SHADER_NOALPHAKILL) ?
							-1 : fillShaderCallback(graph, depth_shader_num, bucket_params->local_sort_node->tri_count);
					}
				}
			}
		}
	}

	PERFINFO_AUTO_STOP_L2();
}

__forceinline static void addToVisualBucket(const RdrGeoBucketParams *bucket_params, RdrSortBucketType sort_type, int wireframe, RdrMaterialShader shader_num, RdrMaterialShader hdr_shader_num, bool has_hdr, RdrAddInstanceParams *params)
{
	RdrDrawListData *data = bucket_params->data;
	int i;
	RdrMaterialShader lra_high_res_shader_num;

	PERFINFO_AUTO_START_FUNC_L2();

	lra_high_res_shader_num = shader_num;
	if (rdr_state.lowResAlphaHighResNeedsManualDepthTest)
		lra_high_res_shader_num.shaderMask |= MATERIAL_SHADER_MANUAL_DEPTH_TEST;
	

	for (i = 0; i < data->pass_count; ++i)
	{
		RdrDrawListPassData *pass_data = &data->pass_data[i];

		if (pass_data->shader_mode == RDRSHDM_VISUAL_HDR && !has_hdr)
			continue;

		if (pass_data->shader_mode == RDRSHDM_VISUAL || pass_data->shader_mode == RDRSHDM_VISUAL_HDR)
		{
			if (wireframe && pass_data->shader_mode != RDRSHDM_VISUAL_HDR)
				addInstanceToBucket(&bucket_params->subobject->opaque_sort_nodes, NULL, NULL, bucket_params, i, RSBT_WIREFRAME);

			if (wireframe != 2)
			{
				RdrSortNode *sort_node = addInstanceToBucket(&bucket_params->subobject->opaque_sort_nodes, NULL, params, bucket_params, i, getEffectiveSortTypeForShaderMode(pass_data->shader_mode, sort_type));

				if (pass_data->shader_mode == RDRSHDM_VISUAL_HDR && hdr_shader_num.key != -1)
				{
					if (!sort_node->draw_shader_handle[RDRSHDM_VISUAL_HDR])
						sort_node->draw_shader_handle[RDRSHDM_VISUAL_HDR] = fillShaderCallback(bucket_params->subobject->graph_render_info, hdr_shader_num, bucket_params->local_sort_node->tri_count);
				}
				else if (pass_data->shader_mode == RDRSHDM_VISUAL && shader_num.key != -1)
				{
					if (!sort_node->draw_shader_handle[RDRSHDM_VISUAL])
						sort_node->draw_shader_handle[RDRSHDM_VISUAL] = fillShaderCallback(bucket_params->subobject->graph_render_info, shader_num, bucket_params->local_sort_node->tri_count);
					if (!sort_node->draw_shader_handle[RDRSHDM_VISUAL_LRA_HIGH_RES])
						sort_node->draw_shader_handle[RDRSHDM_VISUAL_LRA_HIGH_RES] = fillShaderCallback(bucket_params->subobject->graph_render_info, lra_high_res_shader_num, bucket_params->local_sort_node->tri_count);
				}
			}
		}
	}

	PERFINFO_AUTO_STOP_L2();
}

__forceinline static void addToDepthBuckets(RdrAddInstanceParams *params, const RdrGeoBucketParams *bucket_params, RdrSortBucketType sort_type, bool ok_in_zprepass)
{
	RdrDrawListData *data = bucket_params->data;
	int i;

	PERFINFO_AUTO_START_FUNC_L2();

	for (i = 0; i < data->pass_count; ++i)
	{
		RdrDrawListPassData *pass_data = &data->pass_data[i];
		ANALYSIS_ASSUME(pass_data->shader_mode < RDRSHDM_COUNT);
		if ((params->frustum_visible & pass_data->frustum_set_bit) && 
			!rdrIsVisualShaderMode(pass_data->shader_mode))
		{
			RdrSortBucketType eff_sort_type = getEffectiveSortTypeForShaderMode(pass_data->shader_mode, sort_type);

			assert(pass_data->depth_only);

			if (pass_data->shader_mode == RDRSHDM_ZPREPASS && ok_in_zprepass)
			{
				RdrSortNode *sort_node;
				assert(!params->no_outlining || eff_sort_type == RSBT_ZPREPASS_NO_OUTLINE); // if caller set no_outlining, they need to be using the post_grab bucket + shader
				sort_node = addInstanceToBucket(&bucket_params->draw->depth_sort_nodes, NULL, params, bucket_params, i, eff_sort_type);
				sort_node->draw_shader_handle[pass_data->shader_mode] = -1;
			}
			else if (pass_data->shader_mode == RDRSHDM_SHADOW)
			{
				RdrSortNode *sort_node = addInstanceToBucket(&bucket_params->draw->depth_sort_nodes, NULL, params, bucket_params, i, eff_sort_type);
				sort_node->draw_shader_handle[pass_data->shader_mode] = -1;
			}
		}
	}

	PERFINFO_AUTO_STOP_L2();
}

__forceinline static RdrMaterialShader determineShader(RdrDrawList *draw_list, RdrSortBucketType sort_type, RdrLightList *light_list, RdrMaterialFlags material_flags, bool aux_visual_pass)
{
	RdrMaterialShader shader_num = {0};

	if (!aux_visual_pass && draw_list->zprepass && draw_list->zprepass_test && (sort_type == RSBT_OPAQUE_PRE_GRAB || sort_type == RSBT_OPAQUE_POST_GRAB_IN_ZPRE) )
		shader_num.shaderMask = MATERIAL_RENDERMODE_OPAQUE_AFTER_ZPRE;
	else
		shader_num.shaderMask = MATERIAL_RENDERMODE_HAS_ALPHA;

	// Still using SHADOW_BUFFER shader even when doing alpha to reduce number of unique shaders (decal uses alpha shader but needs shadowbuffer)
	if (draw_list->current_data->globally_using_shadow_buffer
		&& !(sort_type == RSBT_ALPHA_LOW_RES_ALPHA || sort_type == RSBT_ALPHA_LOW_RES_ADDITIVE || sort_type == RSBT_ALPHA_LOW_RES_SUBTRACTIVE)/*&& shader_num != MATERIAL_RENDERMODE_HAS_ALPHA*/)
		shader_num.shaderMask |= MATERIAL_SHADER_SHADOW_BUFFER;

	if (light_list->comparator.use_vertex_only_lighting)
	{
		shader_num.shaderMask |= MATERIAL_SHADER_VERTEX_ONLY_LIGHTING;
	}
	else if (draw_list->light_mode == RDLLM_UBERLIGHTING)
	{
		if (!rdr_state.noSimpleDirLightShader && rdrIsSingleDirectionalLight(light_list->light_shader_num.lightMask) &&
			!((sort_type == RSBT_DECAL_PRE_GRAB || sort_type == RSBT_DECAL_POST_GRAB) && (shader_num.shaderMask &MATERIAL_SHADER_SHADOW_BUFFER) && !(material_flags&RMATERIAL_UNLIT))) // Decals with shadowbuffer need to use uberlighting since the alpha version of single-directional light does not do shadow buffer
		{
			shader_num.key |= light_list->light_shader_num.key;
		} else {
			shader_num.shaderMask |= MATERIAL_SHADER_UBERLIGHT;
		}
	} else {
		shader_num.key |= light_list->light_shader_num.key;
		if (draw_list->light_mode == RDLLM_SIMPLE)
			shader_num.shaderMask |= MATERIAL_SHADER_SIMPLE_LIGHTING;
	}
	return shader_num;
}

__forceinline static bool sortTypeShouldForceHDR(RdrSortType sort_type)
{
	return	sort_type == RSBT_ALPHA || sort_type == RSBT_ALPHA_PRE_DOF || sort_type == RSBT_ALPHA_LATE || 
			sort_type == RSBT_ALPHA_LOW_RES_ADDITIVE || sort_type == RSBT_ALPHA_LOW_RES_ALPHA || sort_type == RSBT_ALPHA_LOW_RES_SUBTRACTIVE;
}

__forceinline static RdrMaterialShader calcHDRContribution(RdrDrawList *draw_list, RdrSortNode *sort_node, const RdrMaterial *material, RdrMaterialFlags material_effective_flags, const Vec2 light_contribution, const Vec4 tint_color, F32 screen_area, RdrMaterialShader shader_num, RdrSortBucketType sort_type, bool should_force_hdr)
{
	RdrMaterialShader retVal;
	if (!material->no_hdr && !(material_effective_flags & RMATERIAL_NOBLOOM))
	{
		F32 bloom_brightness = UNLIT_HDR_CONTRIBUTION_MULTIPLIER * material->lighting_contribution[0] + dotVec2(&material->lighting_contribution[1], light_contribution);
		if (!material->no_tint_for_hdr)
		{
			// Adjust by tint color
			F32 max_brightness = MAXF(tint_color[0], tint_color[1]);
			MAX1F(max_brightness, tint_color[2]);
			bloom_brightness *= tint_color[3] * max_brightness;
		}
		bloom_brightness -= draw_list->bloom_brightness;
		bloom_brightness *= saturate(5 * sqrtf(screen_area));
		MAX1F(draw_list->max_bloom_brightness, bloom_brightness);
		if (bloom_brightness > 0 || sortTypeShouldForceHDR(sort_type))
		{
			bloom_brightness *= draw_list->bloom_brightness_multiplier;
			if (should_force_hdr)
				bloom_brightness = MAX_HDR_CONTRIBUTION;
			else
				bloom_brightness = CLAMPF32(bloom_brightness, 0, MAX_HDR_CONTRIBUTION);
			sort_node->hdr_contribution = round(bloom_brightness);
			retVal.shaderMask = MATERIAL_RENDERMODE_HAS_ALPHA | (shader_num.shaderMask & ~MATERIAL_SHADER_RENDERMODE);
			retVal.lightMask = shader_num.lightMask;
			return retVal;
		}
	}

	retVal.key = -1;
	return retVal;
}

__forceinline static bool calcHDRContributionNoMaterial(RdrDrawList *draw_list, RdrSortNode *sort_node, F32 bloom_brightness, F32 alpha, F32 screen_area, RdrSortBucketType sort_type)
{
	bloom_brightness *= alpha;
	bloom_brightness -= draw_list->bloom_brightness;
	bloom_brightness *= saturate(20 * sqrtf(screen_area));
	MAX1F(draw_list->max_bloom_brightness, bloom_brightness);
	if (bloom_brightness > 0 || sortTypeShouldForceHDR(sort_type))
	{
		bloom_brightness *= draw_list->bloom_brightness_multiplier;
		bloom_brightness = CLAMPF32(bloom_brightness, 0, MAX_HDR_CONTRIBUTION);
		sort_node->hdr_contribution = round(bloom_brightness);
		return true;
	}

	return false;
}

__forceinline void addDepthSortNode(RdrGeoBucketParams *bucket_params, RdrSortNode *depth_sort_node, RdrSortBucketType *depth_sort_type, RdrDrawListData *data, RdrAddInstanceParams *params, bool *depth_ok_in_zprepass, bool *has_depth_sort_node)
{
	bucket_params->local_sort_node = depth_sort_node;
	if (!depth_sort_node->do_instancing)
	{
		/* create a sort node that is shared amongst all passes */
		bucket_params->local_sort_node = createSortNode(bucket_params, *depth_sort_type);
		bucket_params->local_sort_node->instances = linAlloc(data->allocator, sizeof(RdrInstanceLinkList));
		bucket_params->local_sort_node->instances->instance = bucket_params->instance;
		bucket_params->local_sort_node->instances->count = 1;
		linAllocDone(data->allocator);
	}
	addToDepthBuckets(params, bucket_params, *depth_sort_type, *depth_ok_in_zprepass);
	*has_depth_sort_node = false;
	*depth_sort_type = -1;
}

#define ADD_DEPTH_SORT_NODE addDepthSortNode(&bucket_params, &depth_sort_node, &depth_sort_type, data, params, &depth_ok_in_zprepass, &has_depth_sort_node);

RdrSubobject *rdrDrawListAllocSubobject(RdrDrawList *draw_list, int tri_count)
{
	RdrDrawListData *data = draw_list->current_data;
	RdrSubobject *subobject;
	subobject = linAlloc(data->allocator, sizeof(RdrSubobject));
	subobject->tri_count = tri_count;
	return subobject;
}

static void initSubobjectMaterial(RdrDrawListData *data, RdrSubobject *subobject)
{
	subobject->material = copyMaterial(data, subobject->material);
	subobject->inited = true;
}



static RdrLightParams *rdrGetUnlitLight(void)
{
	static RdrLightParams light_params;
	static RdrLight rdr_unlit_light;
	static RdrAmbientLight rdr_unlit_ambient_light;

	if (!rdr_unlit_light.light_type)
	{
		rdr_unlit_light.light_type = RDRLIGHT_DIRECTIONAL;
		rdr_unlit_light.fade_out = 1.0f;
		setVec3same(rdr_unlit_ambient_light.ambient[RLCT_WORLD], 1);
		setVec3same(rdr_unlit_ambient_light.ambient[RLCT_CHARACTER], 1);
		light_params.lights[0] = &rdr_unlit_light;
		light_params.ambient_light = &rdr_unlit_ambient_light;
	}
	return &light_params;
}

static void addGeoInstance(RdrDrawList *draw_list, RdrDrawableGeo *draw, RdrAddInstanceParams *params, RdrSortType type, RdrObjectCategory category, bool do_instancing)
{
	RdrDrawListData *data = draw_list->current_data;
	RdrMaterialShader uberlight_shader_num = {0};
	int i, zbucket;
	int alphaint = round(params->instance.color[3] * 255);
	RdrGeoBucketParams bucket_params = {0};
	RdrSortNode depth_sort_node = {0};
	RdrSortBucketType depth_sort_type = -1;
	bool has_depth_sort_node = false;
	RdrLightList *light_list_orig = NULL;
	RdrLightList *light_list_no_shadowbuffer = NULL;
	RdrLightList *light_list_no_single_dir_light = NULL;
	bool in_visual_pass = data->visual_pass_data && (params->frustum_visible & data->visual_pass_data->frustum_set_bit);
	bool depth_ok_in_zprepass = false;
	bool need_dof;

	bucket_params.alphaU8 = CLAMP(alphaint, 0, 255);
	depth_sort_node.skyDepth = params->skyDepth;

	if (!bucket_params.alphaU8)
		return;

	PERFINFO_AUTO_START_FUNC();

	assert(data);
	assert(params);
	
	bucket_params.data = data;
	bucket_params.draw = draw;

	assert(FINITEVEC3(params->instance.world_mid));
	assert(FINITE(params->distance_offset));

	// All callers to this are probably calculating the zdist themselves for texture loading, just remove this and pass it on down?
	bucket_params.zdist = -(dotVec3(data->visual_pass_view_z, params->instance.world_mid) + data->visual_pass_view_z[3]);
	
	zbucket = round(bucket_params.zdist) + 256;
	zbucket = CLAMP(zbucket, 0, ZBUCKET_MAX);

#if RDR_ENABLE_DRAWLIST_HISTOGRAMS
	histogramAccum(data->stats.pass_stats[0].depth_histogram, RDR_DEPTH_BUCKET_COUNT, bucket_params.zdist, &rdr_state.depthHistConfig);
	histogramAccum(data->stats.pass_stats[0].size_histogram, RDR_SIZE_BUCKET_COUNT, params->distance_offset, &rdr_state.depthHistConfig);
#endif

	// if distant enough, force the object to be in the outline pass of the zprepass.
	if (type == RST_AUTO && zbucket > 1200 || !draw_list->outlining)
		params->no_outlining = 0;

	if (params->distance_offset < OBJECT_RADIUS_TOO_SMALL_TO_ZSORT)
		zbucket = ZBUCKET_MAX;

	// larger positive zdist is farther in front of the camera
	// push zdist out by distance offset (better for planar alpha objects like lakes and warwalls)
	bucket_params.zdist += params->distance_offset;
	assert(FINITE(bucket_params.zdist));
	need_dof = NEED_DOF || params->need_dof;

	// Definitely need the light list if doing UBERLIGHTING, otherwise grabbed dynamically
	if (draw_list->light_mode == RDLLM_UBERLIGHTING)
		light_list_orig = getLightList(draw_list, params->light_params, params->ambient_offset, params->ambient_multiplier, params->disable_vertex_light, true);

	do_instancing = do_instancing && !rdr_state.disableSWInstancing;

	if (draw_list->light_mode == RDLLM_UBERLIGHTING && !light_list_orig->comparator.use_vertex_only_lighting)
	{
		RdrMaterialShader light_shader = light_list_orig->light_shader_num;
		if (params->force_no_shadow_receive)
		{
			light_shader.lightMask &= ~(
				(RDRLIGHT_SHADOWED<<MATERIAL_SHADER_LIGHT0_OFFSET)|
				(RDRLIGHT_SHADOWED<<MATERIAL_SHADER_LIGHT1_OFFSET)|
				(RDRLIGHT_SHADOWED<<MATERIAL_SHADER_LIGHT2_OFFSET)|
				(RDRLIGHT_SHADOWED<<MATERIAL_SHADER_LIGHT3_OFFSET)|
				(RDRLIGHT_SHADOWED<<MATERIAL_SHADER_LIGHT4_OFFSET)
				);
		}
		uberlight_shader_num.shaderMask = light_shader.shaderMask | MATERIAL_SHADER_UBERLIGHT;
		uberlight_shader_num.lightMask = light_shader.lightMask;
	}

	if (g_selected_geo_handle)
	{
		if (g_selected_geo_handle == draw->geo_handle_primary)
			params->wireframe = !params->wireframe;
		else if (rdr_state.disableUnselectedGeo)
		{
			PERFINFO_AUTO_STOP();
			return;
		}
	}

	bucket_params.instance = linAlloc(data->allocator, sizeof(RdrInstance));
	memcpy_intrinsic(bucket_params.instance, &params->instance, sizeof(RdrInstance));
	linAllocDone(data->allocator);

	assert(params->per_drawable_data);

	//PERFINFO_AUTO_START("draw->subobject_count",1);
	for (i = 0; i < draw->subobject_count; ++i)
	{
		RdrLightList *light_list_use;
		RdrSortBucketType sort_type;
		RdrMaterialShader shader_num;
		RdrMaterialShader hdr_shader_num; // Shader to use in the HDR pass (only bloomQuality == 2)
		RdrMaterialShader depth_shader_num;
		RdrSortNode local_sort_node = {0};
		bool ok_in_depth_bucket, ok_in_zprepass;
		int wireframe_saved = params->wireframe;
		RdrMaterialFlags effective_flags;
		RdrMaterialFlags add_material_flags = params->add_material_flags;
		RdrMaterial *material;

		depth_shader_num.shaderMask = MATERIAL_RENDERMODE_DEPTH_ONLY;
		depth_shader_num.lightMask = 0;

		bucket_params.subobject = params->subobjects[i];
		if (!bucket_params.subobject->inited)
			initSubobjectMaterial(data, bucket_params.subobject);
		material = bucket_params.subobject->material;

		if (material == NULL)
			continue;

		effective_flags = material->flags | add_material_flags;
		local_sort_node.skyDepth = params->skyDepth;

		if (params->needs_late_alpha_pass_if_need_grab)
		{
			draw_list->need_texture_screen_color_late |= material->need_texture_screen_color;
			draw_list->need_texture_screen_depth_late |= material->need_texture_screen_depth;
		} else {
			draw_list->need_texture_screen_color |= material->need_texture_screen_color;
			draw_list->need_texture_screen_depth |= material->need_texture_screen_depth;
		}
		draw_list->need_texture_screen_color_blurred |= material->need_texture_screen_color_blurred;

		local_sort_node.do_instancing = do_instancing && !(effective_flags & RMATERIAL_NOINSTANCE);

		if (g_selected_graph_render_info && g_selected_graph_render_info == bucket_params.subobject->graph_render_info)
			params->wireframe = !params->wireframe;

		//////////////////////////////////////////////////////////////////////////
		// determine sort type
		{
			float effectiveZDist = bucket_params.zdist;
			if (category == ROC_SKY)
				effectiveZDist = 16000;
			sort_type = determineSortBucket(draw_list, material, type, bucket_params.alphaU8, effectiveZDist, add_material_flags, params->no_outlining, params->no_zprepass, need_dof, params->needs_late_alpha_pass_if_need_grab);
		}

		if (sort_type >= RSBT_ALPHA_PRE_DOF && sort_type <= RSBT_ALPHA_NEED_GRAB_LATE)
		{
			params->queued_as_alpha = true;
		}

		if (material->flags & RMATERIAL_UNLIT)
		{
			RdrLightParams *light_params_unlit = rdrGetUnlitLight();
			RdrLightList *light_list_unlit;
			light_list_unlit = getLightList(draw_list, light_params_unlit, params->ambient_offset, params->ambient_multiplier, params->disable_vertex_light, false);
			light_list_use = light_list_unlit;
			shader_num = determineShader(draw_list, sort_type, light_list_unlit, material->flags, params->aux_visual_pass);
		} else {
			// For light_combos, determine which light list to use
			if (draw_list->light_mode == RDLLM_CCLIGHTING)
			{
				if (draw_list->zprepass && draw_list->zprepass_test &&
					(sort_type == RSBT_OPAQUE_PRE_GRAB || sort_type == RSBT_OPAQUE_POST_GRAB_IN_ZPRE || sort_type == RSBT_DECAL_POST_GRAB || sort_type == RSBT_DECAL_PRE_GRAB) &&
					draw_list->current_data->globally_using_shadow_buffer)
				{
					// we're using a shadow buffer, use the condensed light list, no shadowed light shaders
					if (!light_list_orig)
						light_list_orig = getLightList(draw_list, params->light_params, params->ambient_offset, params->ambient_multiplier, params->disable_vertex_light, true);
					light_list_use = light_list_orig;
				} else {
					if (!light_list_no_shadowbuffer)
						light_list_no_shadowbuffer = getLightList(draw_list, params->light_params, params->ambient_offset, params->ambient_multiplier, params->disable_vertex_light, false);
					light_list_use = light_list_no_shadowbuffer;
				}
			} else
			{
				if (!light_list_orig)
					light_list_orig = getLightList(draw_list, params->light_params, params->ambient_offset, params->ambient_multiplier, params->disable_vertex_light, true);
				light_list_use = light_list_orig;
			}

			//////////////////////////////////////////////////////////////////////////
			// determine shader
			shader_num = determineShader(draw_list, sort_type, light_list_use, material->flags, params->aux_visual_pass);

			if ((shader_num.shaderMask & MATERIAL_SHADER_UBERLIGHT) && light_list_use->comparator.use_single_dirlight)
			{
				if (!light_list_no_single_dir_light)
				{
					RdrLightList local_light_list = *light_list_use;
					local_light_list.comparator.use_single_dirlight = 0;
					light_list_no_single_dir_light = allocLightList(data, &local_light_list);
				}
				light_list_use = light_list_no_single_dir_light;
			}
		}

		// TODO get has_transparency to be valid on texture swapped materials
		if (draw_list_debug_state.overrideNoAlphaKill)
		{
			if (draw_list_debug_state.overrideNoAlphaKillValue)
			{
				depth_shader_num.shaderMask |= MATERIAL_SHADER_NOALPHAKILL;
				//shader_num = MATERIAL_RENDERMODE_OPAQUE_AFTER_ZPRE | (shader_num & ~MATERIAL_SHADER_RENDERMODE);
				local_sort_node.has_transparency = false;
			}
			else
			{
				//depth_shader_num &= ~MATERIAL_SHADER_NOALPHAKILL;
				//shader_num = MATERIAL_RENDERMODE_HAS_ALPHA | (shader_num & ~MATERIAL_SHADER_RENDERMODE);
				local_sort_node.has_transparency = true;
			}
		}
		else if (!material->has_transparency && 
			sort_type != RSBT_ALPHA && sort_type != RSBT_ALPHA_NEED_GRAB_PRE_DOF && sort_type != RSBT_ALPHA_NEED_GRAB && sort_type != RSBT_ALPHA_PRE_DOF &&
			sort_type != RSBT_ALPHA_LATE && sort_type != RSBT_ALPHA_NEED_GRAB_LATE)
		{
			depth_shader_num.shaderMask |= MATERIAL_SHADER_NOALPHAKILL;
			local_sort_node.has_transparency = false;
		}
		else 
		{
			local_sort_node.has_transparency = true;
		}

		if (params->engine_fade)
			shader_num.shaderMask |= MATERIAL_SHADER_COVERAGE_DISABLE;

		if (draw_list->has_hdr_texture)
			shader_num.shaderMask |= MATERIAL_SHADER_HAS_HDR_TEXTURE;


		//////////////////////////////////////////////////////////////////////////
		// create sort node
		local_sort_node.drawable = &draw->base_drawable;
		local_sort_node.geo_handle_primary = draw->geo_handle_primary;
		assert(i < MAX_SUBOBJECT_COUNT);
		local_sort_node.subobject_idx = i;

		local_sort_node.draw_type = draw->base_drawable.draw_type;
		local_sort_node.material = material;
		local_sort_node.domain_material = (material?material->tessellation_material:NULL);
		local_sort_node.add_material_flags = add_material_flags;
		local_sort_node.camera_centered = params->camera_centered;
		local_sort_node.debug_me = params->debug_me;
		local_sort_node.lights = light_list_use;
		local_sort_node.uberlight_shader_num.key = ((shader_num.shaderMask & MATERIAL_SHADER_UBERLIGHT) && uberlight_shader_num.key) ? uberlight_shader_num.key : getRdrMaterialShader(shader_num.shaderMask & MATERIAL_SHADER_SIMPLE_LIGHTING,0).key;
		local_sort_node.force_no_shadow_receive = params->force_no_shadow_receive;
		local_sort_node.uses_far_depth_range = params->uses_far_depth_range;
		local_sort_node.uses_shadowbuffer = !local_sort_node.force_no_shadow_receive && 
											(shader_num.shaderMask & MATERIAL_SHADER_SHADOW_BUFFER) && 
											(sort_type == RSBT_OPAQUE_PRE_GRAB || sort_type == RSBT_OPAQUE_POST_GRAB_IN_ZPRE || (effective_flags & RMATERIAL_DECAL));
		local_sort_node.zbucket = zbucket;
		local_sort_node.tri_count = bucket_params.subobject->tri_count;
		local_sort_node.category = category;
		copyVec4(params->wind_params, local_sort_node.wind_params);
		local_sort_node.has_wind = params->has_wind;
		local_sort_node.has_trunk_wind = params->has_trunk_wind;
		local_sort_node.ignore_vertex_colors = params->ignore_vertex_colors;
		local_sort_node.two_bone_skinning = (rdr_state.forceTwoBonedSkinning || params->two_bone_skinning) && !params->no_two_bone_skinning;

		// Cannot be both uberlighting and single dir light! or vertex only lighting!
		assert(!(((local_sort_node.uberlight_shader_num.shaderMask & MATERIAL_SHADER_UBERLIGHT) || (shader_num.shaderMask & MATERIAL_SHADER_UBERLIGHT))&& 
			(local_sort_node.lights->comparator.use_single_dirlight || local_sort_node.lights->comparator.use_vertex_only_lighting)));

		//////////////////////////////////////////////////////////////////////////
		// determine which buckets it should go in

		ok_in_depth_bucket = depth_shader_num.key && (params->wireframe != 2) && (params->force_shadow_cast || (sort_type < RSBT_ALPHA_PRE_DOF && sort_type != RSBT_DECAL_PRE_GRAB && sort_type != RSBT_DECAL_POST_GRAB));
		ok_in_zprepass = ok_in_depth_bucket && (sort_type == RSBT_OPAQUE_PRE_GRAB || sort_type == RSBT_OPAQUE_POST_GRAB_IN_ZPRE);
		if (ok_in_depth_bucket && 
			(depth_shader_num.shaderMask & MATERIAL_SHADER_NOALPHAKILL) && 
			!(effective_flags & (RMATERIAL_DOUBLESIDED|RMATERIAL_NOZWRITE|RMATERIAL_NOZTEST|RMATERIAL_BACKFACE|RMATERIAL_DEPTHBIAS|RMATERIAL_FORCEFARDEPTH)) && 
			!rdr_state.disable_depth_sort_nodes)
		{
			if (has_depth_sort_node && (depth_sort_type != sort_type || depth_sort_node.do_instancing != local_sort_node.do_instancing || ok_in_zprepass != depth_ok_in_zprepass))
			{
				ADD_DEPTH_SORT_NODE
			}

			if (!has_depth_sort_node)
			{
				ZeroStructForce(&depth_sort_node);
				depth_sort_node.drawable = local_sort_node.drawable;
				depth_sort_node.geo_handle_primary = local_sort_node.geo_handle_primary;
				depth_sort_node.draw_type = local_sort_node.draw_type;
				depth_sort_node.zbucket = local_sort_node.zbucket;
				depth_sort_node.do_instancing = local_sort_node.do_instancing;
				depth_sort_node.uses_far_depth_range = local_sort_node.uses_far_depth_range;
				depth_sort_node.camera_centered = local_sort_node.camera_centered;
				depth_sort_node.debug_me = local_sort_node.debug_me;
				depth_sort_node.has_trunk_wind = local_sort_node.has_trunk_wind;
				depth_sort_node.has_wind = local_sort_node.has_wind;
				depth_sort_node.ignore_vertex_colors = local_sort_node.ignore_vertex_colors;
				depth_sort_node.two_bone_skinning = local_sort_node.two_bone_skinning;
				copyVec4(local_sort_node.wind_params, depth_sort_node.wind_params);

				assert(i < MAX_SUBOBJECT_COUNT);
				depth_sort_node.subobject_idx = i; // start index

				depth_sort_type = sort_type;
				depth_ok_in_zprepass = ok_in_zprepass;
				has_depth_sort_node = true;
			}

			assert(depth_sort_node.subobject_count+1 < MAX_SUBOBJECT_COUNT);
			depth_sort_node.subobject_count++;
			depth_sort_node.tri_count += local_sort_node.tri_count;
			ok_in_depth_bucket = false;
			ok_in_zprepass = false;
		}
		else if (has_depth_sort_node)
		{
			ADD_DEPTH_SORT_NODE
		}

		if (draw_list->separate_hdr_pass && in_visual_pass)
			hdr_shader_num = calcHDRContribution(draw_list, &local_sort_node, material, effective_flags, light_list_use->light_contribution, bucket_params.instance->color, params->screen_area, shader_num, sort_type, params->force_hdr_pass);
		else
			hdr_shader_num.key = -1;

		if (in_visual_pass || ok_in_depth_bucket)
		{
			// do this after the depth sort node is created, since it only affects the visual pass
			if (in_visual_pass && local_sort_node.lights->vertex_light)
				local_sort_node.do_instancing = false; // vertex lights not currently supported by instancing

			//////////////////////////////////////////////////////////////////////////
			// add to buckets

			bucket_params.local_sort_node = &local_sort_node;
			if (!local_sort_node.do_instancing)
			{
				// create a sort node that is shared amongst all passes
				bucket_params.local_sort_node = createSortNode(&bucket_params, sort_type);
			}

			addToBuckets(params, &bucket_params, sort_type, shader_num, 
				hdr_shader_num, depth_shader_num, ok_in_depth_bucket, ok_in_zprepass);
		}

		params->wireframe = wireframe_saved;
	}

	//PERFINFO_AUTO_STOP();

	if (has_depth_sort_node)
	{
		ADD_DEPTH_SORT_NODE
	}

	PERFINFO_AUTO_STOP();
}

RdrDrawableGeo *rdrDrawListAllocGeo(RdrDrawList *draw_list, RdrGeometryType draw_type, ModelLOD *debug_model_backpointer, int material_count, int num_vs_constants, int num_vs_textures)
{
	RdrDrawListData *data = draw_list->current_data;
	RdrDrawableGeo *draw = NULL;

	PERFINFO_AUTO_START_L2(__FUNCTION__,1);

	assert(data);
	assert( draw_type == RTYPE_MODEL ||
			draw_type == RTYPE_TERRAIN ||
			draw_type == RTYPE_STARFIELD);

	if (material_count < MAX_SUBOBJECT_COUNT &&
		num_vs_constants < DRAW_MAX_VS_CONSTANTS &&
		num_vs_textures < DRAW_MAX_VS_TEXTURES &&
		(draw = linAllocEx(data->allocator, sizeof(RdrDrawableGeo), true)))
	{
		initGeo(draw, data, draw_type, material_count, num_vs_constants, num_vs_textures);
		draw->debug_model_backpointer = debug_model_backpointer;
	}
	else
	{
		data->stats.failed_draw_this_frame++;
	}

	PERFINFO_AUTO_STOP_L2();

	return draw;
}

void rdrDrawListAddGeoInstance(RdrDrawList *draw_list, RdrDrawableGeo *draw, RdrAddInstanceParams *instance_params, RdrSortType type, RdrObjectCategory category, bool allow_instance)
{
	RdrGeometryType draw_type = draw->base_drawable.draw_type;
	bool do_instancing;

	PERFINFO_AUTO_START_L2(__FUNCTION__,1);

	assert( draw_type == RTYPE_MODEL ||
			draw_type == RTYPE_TERRAIN ||
			draw_type == RTYPE_STARFIELD);

	do_instancing = allow_instance && draw_type == RTYPE_MODEL && !instance_params->instance.morph;
	instance_params->uniqueDrawCount = 0;
	assert(draw->geo_handle_primary);
	addGeoInstance(draw_list, draw, instance_params, type, category, do_instancing);

	PERFINFO_AUTO_STOP_L2();
}

void rdrDrawListClaimSkinningMatSet(RdrDrawList* draw_list, DynSkinningMatSet* skinning_mat_set)
{
	RdrDrawListData *data = draw_list->current_data;
	eaPush((void***)&data->skinning_mat_sets_to_dec_ref, skinning_mat_set); // so we decrement it when we're done with the draw list
}

RdrDrawableSkinnedModel *rdrDrawListAllocSkinnedModel(RdrDrawList *draw_list, RdrGeometryType draw_type, ModelLOD *debug_model_backpointer, int material_count, int num_vs_constants, int num_bones, SkinningMat4* external_skinning_mat_array)
{
	RdrDrawListData *data = draw_list->current_data;
	RdrDrawableSkinnedModel *draw = NULL;

	PERFINFO_AUTO_START_L2(__FUNCTION__,1);

	assert(data);
	assert(	draw_type == RTYPE_SKINNED_MODEL || 
			draw_type == RTYPE_CURVED_MODEL);

	if (material_count < MAX_SUBOBJECT_COUNT &&
		num_vs_constants < DRAW_MAX_VS_CONSTANTS &&
		num_bones < DRAW_MAX_BONES &&
		(draw = linAllocEx(data->allocator, sizeof(RdrDrawableSkinnedModel), true)))
	{
		initGeo(&draw->base_geo_drawable, data, draw_type, material_count, num_vs_constants, 0);
		draw->base_geo_drawable.debug_model_backpointer = debug_model_backpointer;

		MAX1(num_bones, 0);
		assert(num_bones < 256);

		draw->num_bones = num_bones;
		if (draw->num_bones)
		{
			if (external_skinning_mat_array	)
			{
				// Align the alloc'd size to 4 bytes
				U32 uiSize = (sizeof(U8) * draw->num_bones);
				if (uiSize & 3)
					uiSize = (uiSize & ~3) + 4;
				draw->skinning_mat_indices = linAlloc(data->allocator, uiSize); 
				draw->skinning_mat_array = external_skinning_mat_array;
			}
			else
			{
				draw->skinning_mat_array = AlignPointerUpPow2(linAlloc(data->allocator, sizeof(SkinningMat4) * draw->num_bones + VEC_ALIGNMENT), VEC_ALIGNMENT);
				draw->skinning_mat_indices = NULL;
			}
		}
	}
	else
	{
		data->stats.failed_draw_this_frame++;
	}

	PERFINFO_AUTO_STOP_L2();

	return draw;
}

void rdrDrawListAddSkinnedModel(RdrDrawList *draw_list, RdrDrawableSkinnedModel *draw, RdrAddInstanceParams *instance_params, RdrSortType type, RdrObjectCategory category)
{
	RdrGeometryType draw_type = draw->base_geo_drawable.base_drawable.draw_type;

	PERFINFO_AUTO_START_L2(__FUNCTION__,1);

	assert(	draw_type == RTYPE_SKINNED_MODEL || 
			draw_type == RTYPE_CURVED_MODEL);

	assert(draw->base_geo_drawable.geo_handle_primary);
	addGeoInstance(draw_list, &draw->base_geo_drawable, instance_params, type, category, false);

	PERFINFO_AUTO_STOP_L2();
}

RdrDrawablePrimitive *rdrDrawListAllocPrimitive(RdrDrawList *draw_list, RdrGeometryType draw_type, bool sprite_state_alloc)
{
	RdrDrawListData *data = draw_list->current_data;
	RdrDrawablePrimitive *draw = NULL;
	U32 data_size = sizeof(RdrDrawablePrimitive);
	char* pData;

	PERFINFO_AUTO_START_L2(__FUNCTION__,1);

	assert(data);
	assert(draw_type == RTYPE_PRIMITIVE);

	if (sprite_state_alloc)
		data_size += sizeof(RdrSpriteState);

	if (pData = linAllocEx(data->allocator, data_size, true))
	{
		draw = (RdrDrawablePrimitive *)pData;
		pData += sizeof(RdrDrawablePrimitive);
		draw->base_drawable.draw_type = draw_type;
		if (sprite_state_alloc)
			draw->sprite_state = (RdrSpriteState*)pData;
	}
	else
	{
		data->stats.failed_draw_this_frame++;
	}

	PERFINFO_AUTO_STOP_L2();

	return draw;
}

void rdrDrawListAddPrimitive(RdrDrawList *draw_list, RdrDrawablePrimitive *draw, RdrSortType type, RdrObjectCategory category)
{
	RdrDrawListData *data = draw_list->current_data;
	Vec3 world_mid;
	int tri_count, alphaint;
	F32 alpha = 1;
	RdrGeometryType draw_type = draw->base_drawable.draw_type;
	RdrSortBucketType sort_type;
	RdrGeoBucketParams bucket_params = {0};

	if (!data->visual_pass_data)
		return;

	PERFINFO_AUTO_START_L2(__FUNCTION__,1);

	assert(data);
	assert(draw_type == RTYPE_PRIMITIVE);

	if (draw->type == RP_LINE)
	{
		addVec3(draw->vertices[0].pos, draw->vertices[1].pos, world_mid);
		scaleVec3(world_mid, 0.5f, world_mid);
		MIN1(alpha, draw->vertices[0].color[3]);
		MIN1(alpha, draw->vertices[1].color[3]);
		tri_count = 1;
	}
	else if (draw->type == RP_TRI)
	{
		addVec3(draw->vertices[0].pos, draw->vertices[1].pos, world_mid);
		addVec3(world_mid, draw->vertices[2].pos, world_mid);
		scaleVec3(world_mid, 0.33333f, world_mid);
		MIN1(alpha, draw->vertices[0].color[3]);
		MIN1(alpha, draw->vertices[1].color[3]);
		MIN1(alpha, draw->vertices[2].color[3]);
		tri_count = 1;
	}
	else
	{
		addVec3(draw->vertices[0].pos, draw->vertices[1].pos, world_mid);
		addVec3(world_mid, draw->vertices[2].pos, world_mid);
		addVec3(world_mid, draw->vertices[3].pos, world_mid);
		scaleVec3(world_mid, 0.25f, world_mid);
		MIN1(alpha, draw->vertices[0].color[3]);
		MIN1(alpha, draw->vertices[1].color[3]);
		MIN1(alpha, draw->vertices[2].color[3]);
		MIN1(alpha, draw->vertices[3].color[3]);
		tri_count = 2;
	}

	assert(FINITEVEC3(world_mid));

	bucket_params.data = data;
	bucket_params.zdist = dotVec3(data->visual_pass_view_z, world_mid) + data->visual_pass_view_z[3];
	bucket_params.zdist = -bucket_params.zdist;
	assert(FINITE(bucket_params.zdist));

	alphaint = round(alpha * 255);
	bucket_params.alphaU8 = CLAMP(alphaint, 0, 255);
	sort_type = determineSortBucket(draw_list, NULL, type, bucket_params.alphaU8, bucket_params.zdist, 0, true, true, NEED_DOF, false);

	//////////////////////////////////////////////////////////////////////////
	// create sort node
	bucket_params.local_sort_node = createSortNode(&bucket_params, sort_type);
	bucket_params.local_sort_node->drawable = &draw->base_drawable;
	bucket_params.local_sort_node->geo_handle_primary = -1;
	bucket_params.local_sort_node->tri_count = tri_count;
	bucket_params.local_sort_node->category = category;

	addToVisualBucket(&bucket_params, sort_type, 0, getRdrMaterialShaderByKey(-1), getRdrMaterialShaderByKey(-1), draw->tonemapped, NULL);

	PERFINFO_AUTO_STOP_L2();
}

RdrDrawableMeshPrimitive *rdrDrawListAllocMeshPrimitive(RdrDrawList *draw_list, RdrGeometryType draw_type, U32 num_verts, U32 num_strips, bool bMallocVerts)
{
	RdrDrawListData *data = draw_list->current_data;
	RdrDrawableMeshPrimitive *draw = NULL;

	PERFINFO_AUTO_START_L2(__FUNCTION__,1);

	assert(data);
	assert(draw_type == RTYPE_PRIMITIVE_MESH);

	if (num_verts < DRAW_MAX_PRIM_VERTS &&
		num_strips < DRAW_MAX_PRIM_STRIPS &&
		(draw = linAllocEx(data->allocator, sizeof(RdrDrawableMeshPrimitive), true)))
	{
		draw->base_drawable.draw_type = draw_type;

		draw->num_strips = num_strips;
		draw->strips = linAlloc(data->allocator, sizeof(RdrDrawableMeshPrimitiveStrip) * num_strips);

		draw->num_verts = num_verts;
		if (bMallocVerts)
		{
			draw->verts = malloc(sizeof(RdrPrimitiveVertex) * num_verts);
			draw->owns_verts = true;
		}
		else
		{
			draw->verts = linAlloc(data->allocator, sizeof(RdrPrimitiveVertex) * num_verts);
			draw->owns_verts = false;
		}
	}
	else
	{
		data->stats.failed_draw_this_frame++;
	}

	PERFINFO_AUTO_STOP_L2();

	return draw;
}

RdrDrawableMeshPrimitiveStrip* rdrDrawListAllocMeshPrimitiveStrip(RdrDrawList *draw_list, RdrDrawableMeshPrimitive* draw, U32 strip_index, U32 num_indices)
{
	RdrDrawListData *data = draw_list->current_data;
	RdrDrawableMeshPrimitiveStrip* strip;
	assert(data);
	assert(strip_index < draw->num_strips);
	strip = &draw->strips[strip_index];
	strip->indices = linAlloc(data->allocator, sizeof(U16) * num_indices);
	strip->num_indices = num_indices;
	return strip;
}

void rdrDrawListAddMeshPrimitive(RdrDrawList *draw_list, RdrDrawableMeshPrimitive *draw, const Mat4 world_matrix, RdrSortType type, RdrObjectCategory category)
{
	RdrDrawListData *data = draw_list->current_data;
	int tri_count = 0, alphaint;
	F32 alpha = 1;
	RdrGeometryType draw_type = draw->base_drawable.draw_type;
	RdrSortBucketType sort_type;
	U32 i;
	RdrGeoBucketParams bucket_params = {0};
	RdrInstanceLinkList *instance_link;

	if (!data->visual_pass_data)
		return;

	PERFINFO_AUTO_START_L2(__FUNCTION__,1);

	assert(data);
	assert(draw_type == RTYPE_PRIMITIVE_MESH);

	for (i=0; i<draw->num_strips; ++i)
	{
		RdrDrawableMeshPrimitiveStrip* strip = &draw->strips[i];
		switch(strip->type)
		{
			xcase RP_TRILIST:
				tri_count += strip->num_indices/3;
			xcase RP_TRISTRIP:
				tri_count += MAX(strip->num_indices-2, 0);
		}
	}

	for (i=0; i<draw->num_verts; ++i)
		MIN1(alpha, draw->verts[i].color[3]);

	assert(FINITEVEC3(world_matrix[3]));

	bucket_params.data = data;
	bucket_params.zdist = dotVec3(data->visual_pass_view_z, world_matrix[3]) + data->visual_pass_view_z[3];
	bucket_params.zdist = -bucket_params.zdist;
	assert(FINITE(bucket_params.zdist));

	alphaint = round(alpha * 255);
	bucket_params.alphaU8 = CLAMP(alphaint, 0, 255);
	sort_type = determineSortBucket(draw_list, NULL, type, bucket_params.alphaU8, bucket_params.zdist, 0, true, true, NEED_DOF, false);

	//////////////////////////////////////////////////////////////////////////
	// create sort node
	bucket_params.local_sort_node = createSortNode(&bucket_params, sort_type);
	bucket_params.local_sort_node->drawable = &draw->base_drawable;
	bucket_params.local_sort_node->geo_handle_primary = -1;
	bucket_params.local_sort_node->tri_count = tri_count;
	bucket_params.local_sort_node->category = category;

	bucket_params.local_sort_node->instances = instance_link = linAlloc(data->allocator, sizeof(RdrInstanceLinkList));
	instance_link->instance = linAlloc(data->allocator, sizeof(RdrInstance));
	copyMat4(world_matrix, instance_link->instance->world_matrix);
	setVec4same(instance_link->instance->color, 1);
	instance_link->count = 1;
	setVec4(instance_link->per_drawable_data.instance_param, 1,1,1, 1);

	addToVisualBucket(&bucket_params, sort_type, 0, getRdrMaterialShaderByKey(-1), getRdrMaterialShaderByKey(-1), draw->tonemapped, NULL);
	linAllocDone(data->allocator);

	PERFINFO_AUTO_STOP_L2();
}

RdrDrawableFastParticles *rdrDrawListAllocFastParticles(RdrDrawList *draw_list, RdrGeometryType draw_type, int particle_count, bool streak, int num_bones)
{
	RdrDrawListData *data = draw_list->current_data;
	RdrDrawableFastParticles *draw = NULL;

	PERFINFO_AUTO_START_L2(__FUNCTION__,1);

	assert(data);
	assert(draw_type == RTYPE_FASTPARTICLES);

	if (particle_count < DRAW_MAX_PARTICLE_COUNT &&
		num_bones < DRAW_MAX_PARTICLE_BONES &&
		(draw = linAllocEx(data->allocator, sizeof(RdrDrawableFastParticles), true)))
	{
		draw->base_drawable.draw_type = draw_type;

		MAX1(particle_count, 0);

		draw->particle_count = particle_count;
		if (draw->particle_count)
		{
			if (streak)
				draw->streakverts = linAlloc16(data->allocator, sizeof(RdrFastParticleStreakVertex) * draw->particle_count * 4);
			else
				draw->verts = linAlloc16(data->allocator, sizeof(RdrFastParticleVertex) * draw->particle_count * 4);
		}

		if (num_bones > 0)
		{
			draw->num_bones = num_bones;
			draw->bone_infos = linAlloc(data->allocator, sizeof(SkinningMat4) * num_bones);
		}
	}
	else
	{
		data->stats.failed_draw_this_frame++;
	}

	PERFINFO_AUTO_STOP_L2();

	return draw;
}

void rdrDrawListAddFastParticles(RdrDrawList *draw_list, RdrDrawableFastParticles *draw, RdrAddFastParticleParams *params, RdrSortType type, RdrObjectCategory category)
{
	RdrDrawListData *data = draw_list->current_data;
	RdrGeometryType draw_type = draw->base_drawable.draw_type;
	int alphaint;
	F32 alpha = 1;
	RdrSortBucketType sort_type;
	RdrGeoBucketParams bucket_params = {0};
	bool has_hdr = false;

	if (!data->visual_pass_data)
		return;

	PERFINFO_AUTO_START_L2(__FUNCTION__,1);

	assert(data);
	assert(draw_type == RTYPE_FASTPARTICLES);

	bucket_params.data = data;
	bucket_params.zdist = params->zdist;
	assert(FINITE(bucket_params.zdist));

	alphaint = round(alpha * 255);
	bucket_params.alphaU8 = CLAMP(alphaint, 0, 255);
	sort_type = determineSortBucket(draw_list, NULL, type, bucket_params.alphaU8, bucket_params.zdist, draw->blendmode, false, false, NEED_DOF,
		!rdr_state.disableTwoPassRefraction);
	if (  sort_type != RSBT_ALPHA_LOW_RES_ALPHA
		  && sort_type != RSBT_ALPHA_LOW_RES_ADDITIVE
		  && sort_type != RSBT_ALPHA_LOW_RES_SUBTRACTIVE) {
		bucket_params.zdist -= params->zbias;
	}
	assert(sort_type < RSBT_COUNT);

	//////////////////////////////////////////////////////////////////////////
	// create sort node
	bucket_params.local_sort_node = createSortNode(&bucket_params, sort_type);
	bucket_params.local_sort_node->drawable = &draw->base_drawable;
	bucket_params.local_sort_node->geo_handle_primary = -2;
	bucket_params.local_sort_node->tri_count = draw->particle_count * 2;
	bucket_params.local_sort_node->material = (void *)(intptr_t)draw->tex_handle;
	bucket_params.local_sort_node->category = category;
	bucket_params.local_sort_node->uses_far_depth_range = params->uses_far_depth_range;

	if (draw_list->separate_hdr_pass && (sortTypeShouldForceHDR(sort_type) || !draw->no_tonemap))
	{
		if (draw->no_tonemap) // And also is alpha
		{
			has_hdr = true; // Needs to be drawn in underexposed pass
			bucket_params.local_sort_node->hdr_contribution = round(draw_list->max_bloom_brightness);
		} else
			has_hdr = calcHDRContributionNoMaterial(draw_list, bucket_params.local_sort_node, params->brightness, alpha, params->screen_area, sort_type);
	}

	if (draw->soft_particles)
		draw_list->need_texture_screen_depth = 1;

	addToVisualBucket(&bucket_params, sort_type, params->wireframe, getRdrMaterialShaderByKey(-1), getRdrMaterialShaderByKey(-1), has_hdr, NULL);

	PERFINFO_AUTO_STOP_L2();
}

RdrDrawableTriStrip *rdrDrawListAllocTriStrip(RdrDrawList *draw_list, RdrGeometryType draw_type, int vert_count)
{
	RdrDrawListData *data = draw_list->current_data;
	RdrDrawableTriStrip *draw = NULL;

	PERFINFO_AUTO_START_L2(__FUNCTION__,1);

	assert(data);
	assert(draw_type == RTYPE_TRISTRIP);

	if (vert_count < DRAW_MAX_STRIP_VERTS &&
		(draw = linAllocEx(data->allocator, sizeof(RdrDrawableTriStrip), true)))
	{
		draw->base_drawable.draw_type = draw_type;

		MAX1(vert_count, 0);
		assert(vert_count < (1<<30));

		draw->vert_count = vert_count;
		if (draw->vert_count)
			draw->verts = linAlloc(data->allocator, sizeof(RdrParticleVertex) * draw->vert_count);
	}
	else
	{
		data->stats.failed_draw_this_frame++;
	}

	PERFINFO_AUTO_STOP_L2();

	return draw;
}

void rdrDrawListAddTriStrip(RdrDrawList *draw_list, RdrDrawableTriStrip *draw, RdrAddInstanceParams *instance_params, const Vec3 world_mid, RdrSortType type, RdrObjectCategory category)
{
	RdrDrawListData *data = draw_list->current_data;
	RdrGeometryType draw_type = draw->base_drawable.draw_type;
	int alphaint;
	F32 alpha = 1;
	RdrSortBucketType sort_type;
	RdrGeoBucketParams bucket_params = {0};
	RdrMaterialShader shader_num, hdr_shader_num;
	F32 brightness = 0;

	hdr_shader_num.key = -1;

	if (!data->visual_pass_data)
		return;

	PERFINFO_AUTO_START_L2(__FUNCTION__,1);

	assert(data);
	assert(draw_type == RTYPE_TRISTRIP);

	assert(FINITEVEC3(world_mid));

	bucket_params.data = data;
	bucket_params.zdist = instance_params->zdist;
	bucket_params.subobject = instance_params->subobjects[0];
	if (!bucket_params.subobject->inited)
		initSubobjectMaterial(data, bucket_params.subobject);

	assert(FINITE(bucket_params.zdist));

	alphaint = round(alpha * 255);
	bucket_params.alphaU8 = CLAMP(alphaint, 0, 255);
	sort_type = determineSortBucket(draw_list, bucket_params.subobject->material, type, bucket_params.alphaU8, bucket_params.zdist, draw->add_material_flags, false, false, NEED_DOF, instance_params->needs_late_alpha_pass_if_need_grab && !rdr_state.disableTwoPassRefraction);

	if (sort_type == RSBT_COUNT || sort_type == RSBT_ALPHA || sort_type == RSBT_ALPHA_NEED_GRAB_PRE_DOF ||
		sort_type == RSBT_ALPHA_NEED_GRAB ||
		sort_type == RSBT_ALPHA_PRE_DOF || sort_type == RSBT_ALPHA_LATE ||
		sort_type == RSBT_ALPHA_NEED_GRAB_LATE || sort_type == RSBT_ALPHA_LOW_RES_ALPHA ||
		sort_type == RSBT_ALPHA_LOW_RES_ADDITIVE || sort_type == RSBT_ALPHA_LOW_RES_SUBTRACTIVE)
	{
		U32 i;
		F32 min_alpha = alpha;

		for (i = 0; i < draw->vert_count; ++i)
		{
			MAX1(brightness, draw->verts[i].color[0] * draw->verts[i].color[3]);
			MAX1(brightness, draw->verts[i].color[1] * draw->verts[i].color[3]);
			MAX1(brightness, draw->verts[i].color[2] * draw->verts[i].color[3]);
			MIN1(min_alpha, draw->verts[i].color[3]);
		}

		// redetermine
		alphaint = round(min_alpha * 255);
		bucket_params.alphaU8 = CLAMP(alphaint, 0, 255);
		sort_type = determineSortBucket(draw_list, bucket_params.subobject->material, type, bucket_params.alphaU8, bucket_params.zdist, draw->add_material_flags, false, false, NEED_DOF, instance_params->needs_late_alpha_pass_if_need_grab && !rdr_state.disableTwoPassRefraction);
	}
	assert(sort_type<RSBT_COUNT);

	//////////////////////////////////////////////////////////////////////////
	// determine shader
	{
		RdrLightList light_list = {0};
		light_list.comparator.use_vertex_only_lighting = draw_list->light_mode == RDLLM_VERTEX_ONLY; // TODO: use this always if we have no normalmap for cheaper fill?  Need to preload it too though!
		shader_num = determineShader(draw_list, sort_type, &light_list, bucket_params.subobject->material->flags, instance_params->aux_visual_pass);
	}


	//////////////////////////////////////////////////////////////////////////
	// create sort node
	bucket_params.local_sort_node = createSortNode(&bucket_params, sort_type);
	bucket_params.local_sort_node->drawable = &draw->base_drawable;
	bucket_params.local_sort_node->geo_handle_primary = -2;
	bucket_params.local_sort_node->tri_count = MAX(0, draw->vert_count - 2);
	bucket_params.local_sort_node->material = bucket_params.subobject->material;
	bucket_params.local_sort_node->category = category;

 	if (draw_list->separate_hdr_pass)
		hdr_shader_num = calcHDRContribution(draw_list, bucket_params.local_sort_node, bucket_params.subobject->material, bucket_params.subobject->material->flags, zerovec4, draw->verts[0].color, instance_params->screen_area, shader_num, sort_type, false);

	if (sort_type == RSBT_ALPHA_NEED_GRAB_LATE)
	{
		draw_list->need_texture_screen_color_late |= bucket_params.subobject->material->need_texture_screen_color;
		draw_list->need_texture_screen_depth_late |= bucket_params.subobject->material->need_texture_screen_depth;
	} else {
		draw_list->need_texture_screen_color |= bucket_params.subobject->material->need_texture_screen_color;
		draw_list->need_texture_screen_depth |= bucket_params.subobject->material->need_texture_screen_depth;
	}
	draw_list->need_texture_screen_color_blurred |= bucket_params.subobject->material->need_texture_screen_color_blurred;

	addToVisualBucket(&bucket_params, sort_type, instance_params->wireframe, shader_num, hdr_shader_num, hdr_shader_num.key != -1, instance_params);

	PERFINFO_AUTO_STOP_L2();
}

RdrDrawableClothMesh *rdrDrawListAllocClothMesh(RdrDrawList *draw_list, RdrGeometryType draw_type, ModelLOD *debug_model_backpointer, int vert_count, int strip_count, int material_count, int num_vs_constants)
{
	RdrDrawListData *data = draw_list->current_data;
	RdrDrawableClothMesh *draw = NULL;

	PERFINFO_AUTO_START_L2(__FUNCTION__,1);

	assert(data);
	assert(draw_type == RTYPE_CLOTHMESH);

	if (vert_count < DRAW_MAX_CLOTH_VERTS &&
		strip_count < DRAW_MAX_CLOTH_STRIPS &&
		(draw = linAllocEx(data->allocator, sizeof(RdrDrawableClothMesh), true)))
	{
		initGeo(&draw->base_geo_drawable, data, draw_type, material_count, num_vs_constants, 0);
		draw->base_geo_drawable.debug_model_backpointer = debug_model_backpointer;

		MAX1(vert_count, 0);
		assert(vert_count < (1<<30));

		draw->vert_count = vert_count;
		if (draw->vert_count)
			draw->verts = linAlloc16(data->allocator, sizeof(RdrClothMeshVertex) * draw->vert_count);

		draw->strip_count = strip_count;
		draw->tri_strips = linAlloc16(data->allocator, sizeof(RdrDrawableIndexedTriStrip) * draw->strip_count);
	}
	else
	{
		data->stats.failed_draw_this_frame++;
	}

	PERFINFO_AUTO_STOP_L2();

	return draw;
}

RdrDrawableIndexedTriStrip* rdrDrawListAllocClothMeshStripIndices(RdrDrawList *draw_list, RdrDrawableClothMesh* draw, U32 strip_index, U32 num_indices)
{
	RdrDrawListData *data = draw_list->current_data;
	RdrDrawableIndexedTriStrip* strip;
	assert(data);
	assert(strip_index < draw->strip_count);
	strip = &draw->tri_strips[strip_index];
	strip->indices = linAlloc(data->allocator, sizeof(U16) * num_indices);
	strip->num_indices = num_indices;
	return strip;
}

void rdrDrawListAddClothMesh(RdrDrawList *draw_list, RdrDrawableClothMesh *draw, RdrAddInstanceParams *instance_params, RdrSortType type, RdrObjectCategory category)
{
	RdrGeometryType draw_type = draw->base_geo_drawable.base_drawable.draw_type;

	PERFINFO_AUTO_START_L2(__FUNCTION__,1);

	assert(draw_type == RTYPE_CLOTHMESH);

	addGeoInstance(draw_list, &draw->base_geo_drawable, instance_params, type, category, false);

	PERFINFO_AUTO_STOP_L2();
}

RdrDrawableCylinderTrail *rdrDrawListAllocCylinderTrail(RdrDrawList *draw_list, RdrGeometryType draw_type, int vert_count, int index_count, int num_vertex_shader_constants)
{
	RdrDrawListData *data = draw_list->current_data;
	RdrDrawableCylinderTrail *draw = NULL;

	PERFINFO_AUTO_START_L2(__FUNCTION__,1);

	assert(data);
	assert(draw_type == RTYPE_CYLINDERTRAIL);

	if (vert_count <= DRAW_MAX_CYLINDER_VERTS &&
		index_count <= DRAW_MAX_CYLINDER_INDICES &&
		num_vertex_shader_constants <= DRAW_MAX_CYLINDER_VS_CONSTANTS &&
		(draw = linAllocEx(data->allocator, sizeof(RdrDrawableCylinderTrail), true)))
	{
		draw->base_drawable.draw_type = draw_type;

		MAX1(vert_count, 0);
		assert(vert_count < (1<<16));
		MAX1(index_count, 0);
		assert(index_count < (1<<16));
		MAX1(num_vertex_shader_constants, 0);
		assert(num_vertex_shader_constants < (1<<16));

		draw->vert_count = vert_count;
		if (draw->vert_count)
			draw->verts = linAlloc(data->allocator, sizeof(RdrCylinderTrailVertex) * draw->vert_count);

		draw->index_count = index_count;
		if (draw->index_count)
			draw->idxs = linAlloc(data->allocator, sizeof(U16) * draw->index_count);

		draw->num_constants = num_vertex_shader_constants;
		if (draw->num_constants)
			draw->vertex_shader_constants = linAlloc(data->allocator, sizeof(Vec4) * draw->num_constants);
	}
	else
	{
		data->stats.failed_draw_this_frame++;
	}

	PERFINFO_AUTO_STOP_L2();

	return draw;
}

void rdrDrawListAddCylinderTrail(RdrDrawList *draw_list, RdrDrawableCylinderTrail *draw, RdrAddInstanceParams* instance_params, const Vec3 world_mid, RdrSortType type, RdrObjectCategory category)
{
	RdrDrawListData *data = draw_list->current_data;
	RdrGeometryType draw_type = draw->base_drawable.draw_type;
	int alphaint;
	F32 alpha = 1;
	RdrSortBucketType sort_type;
	RdrGeoBucketParams bucket_params = {0};
	RdrMaterialShader shader_num, hdr_shader_num;
	F32 brightness = 0;

	hdr_shader_num.key = -1;

	if (!data->visual_pass_data)
		return;

	PERFINFO_AUTO_START_L2(__FUNCTION__,1);

	assert(data);
	assert(draw_type == RTYPE_CYLINDERTRAIL);

	assert(FINITEVEC3(world_mid));

	bucket_params.data = data;
	bucket_params.zdist = instance_params->zdist;
	bucket_params.subobject = instance_params->subobjects[0];
	assert(FINITE(bucket_params.zdist));
	if (!bucket_params.subobject->inited)
		initSubobjectMaterial(data, bucket_params.subobject);

	alphaint = round(alpha * 255);
	bucket_params.alphaU8 = CLAMP(alphaint, 0, 255);
	sort_type = determineSortBucket(draw_list, bucket_params.subobject->material, type, bucket_params.alphaU8, bucket_params.zdist, draw->add_material_flags, false, false, NEED_DOF, instance_params->needs_late_alpha_pass_if_need_grab && !rdr_state.disableTwoPassRefraction);

	if (sort_type == RSBT_COUNT || sort_type == RSBT_ALPHA || sort_type == RSBT_ALPHA_NEED_GRAB_PRE_DOF ||
		sort_type == RSBT_ALPHA_NEED_GRAB ||
		sort_type == RSBT_ALPHA_PRE_DOF || sort_type == RSBT_ALPHA_LATE ||
		sort_type == RSBT_ALPHA_NEED_GRAB_LATE || sort_type == RSBT_ALPHA_LOW_RES_ALPHA ||
		sort_type == RSBT_ALPHA_LOW_RES_ADDITIVE || sort_type == RSBT_ALPHA_LOW_RES_SUBTRACTIVE)
	{
		U32 i;
		F32 min_alpha = alpha;
		U32 numPoints = draw->num_constants / 3;

		for (i = 0; i < numPoints; ++i)
		{
			F32* vColor = draw->vertex_shader_constants[i * 3 + 2];
			MAX1(brightness, vColor[0] * vColor[3]);
			MAX1(brightness, vColor[1] * vColor[3]);
			MAX1(brightness, vColor[2] * vColor[3]);
			MIN1(min_alpha, vColor[3]);
		}

		// redetermine
		alphaint = round(min_alpha * 255);
		bucket_params.alphaU8 = CLAMP(alphaint, 0, 255);
		sort_type = determineSortBucket(draw_list, bucket_params.subobject->material, type, bucket_params.alphaU8, bucket_params.zdist, draw->add_material_flags, false, false, NEED_DOF, instance_params->needs_late_alpha_pass_if_need_grab && !rdr_state.disableTwoPassRefraction);
	}
	assert(sort_type<RSBT_COUNT);

	//////////////////////////////////////////////////////////////////////////
	// determine shader
	{
		RdrLightList light_list = {0};
		light_list.comparator.use_vertex_only_lighting = draw_list->light_mode == RDLLM_VERTEX_ONLY; // TODO: use this always if we have no normalmap for cheaper fill?  Need to preload it too though!
		shader_num = determineShader(draw_list, sort_type, &light_list, bucket_params.subobject->material->flags, instance_params->aux_visual_pass);
	}

	//////////////////////////////////////////////////////////////////////////
	// create sort node
	bucket_params.local_sort_node = createSortNode(&bucket_params, sort_type);
	bucket_params.local_sort_node->drawable = &draw->base_drawable;
	bucket_params.local_sort_node->geo_handle_primary = -2;
	bucket_params.local_sort_node->tri_count = draw->index_count / 3;
	bucket_params.local_sort_node->material = bucket_params.subobject->material;
	bucket_params.local_sort_node->category = category;

 	if (draw_list->separate_hdr_pass)
		hdr_shader_num = calcHDRContribution(draw_list, bucket_params.local_sort_node, bucket_params.subobject->material, bucket_params.subobject->material->flags, zerovec4, draw->vertex_shader_constants[2], instance_params->screen_area, shader_num, sort_type, false);

	if (sort_type == RSBT_ALPHA_NEED_GRAB_LATE)
	{
		draw_list->need_texture_screen_color_late |= bucket_params.subobject->material->need_texture_screen_color;
		draw_list->need_texture_screen_depth_late |= bucket_params.subobject->material->need_texture_screen_depth;
	} else {
		draw_list->need_texture_screen_color |= bucket_params.subobject->material->need_texture_screen_color;
		draw_list->need_texture_screen_depth |= bucket_params.subobject->material->need_texture_screen_depth;
	}
	draw_list->need_texture_screen_color_blurred |= bucket_params.subobject->material->need_texture_screen_color_blurred;

	addToVisualBucket(&bucket_params, sort_type, instance_params->wireframe, shader_num, hdr_shader_num, hdr_shader_num.key != -1, instance_params);

	PERFINFO_AUTO_STOP_L2();
}

RdrDrawableParticle *rdrDrawListAllocParticle(RdrDrawList *draw_list, RdrGeometryType draw_type)
{
	RdrDrawListData *data = draw_list->current_data;
	RdrDrawableParticle *draw;

	PERFINFO_AUTO_START_L2(__FUNCTION__,1);

	assert(data);
	assert(draw_type == RTYPE_PARTICLE);

	if (draw = linAllocEx(data->allocator, sizeof(RdrDrawableParticle), true))
	{
		draw->base_drawable.draw_type = draw_type;

		draw->particles = linAlloc(data->allocator, sizeof(RdrDrawableParticleLinkList));
		draw->particles->particle_count = 1;
	}
	else
	{
		data->stats.failed_draw_this_frame++;
	}

	PERFINFO_AUTO_STOP_L2();

	return draw;
}

void rdrDrawListAddParticleLink(RdrDrawList *draw_list, RdrDrawableParticle *draw)
{
	RdrDrawListData *data = draw_list->current_data;
	RdrDrawableParticleLinkList *link;
	link = linAlloc(data->allocator, sizeof(RdrDrawableParticleLinkList));
	link->next = draw->particles;
	draw->particles = link;
	draw->particles->particle_count = 1 + draw->particles->next->particle_count;
}

void rdrDrawListAddParticle(RdrDrawList *draw_list, RdrDrawableParticle *draw, RdrAddInstanceParams *instance_params, RdrSortType type, RdrObjectCategory category, F32 zdist)
{
	RdrDrawListData *data = draw_list->current_data;
	RdrGeometryType draw_type = draw->base_drawable.draw_type;
	int alphaint;
	F32 alpha = 1;
	RdrSortBucketType sort_type;
	RdrMaterialShader shader_num, hdr_shader_num;
	RdrGeoBucketParams bucket_params = {0};

	hdr_shader_num.key = -1;
	if (!data->visual_pass_data)
		return;

	PERFINFO_AUTO_START_L2(__FUNCTION__,1);

	assert(data);
	assert(draw_type == RTYPE_PARTICLE);

	MIN1(alpha, draw->particles->verts[0].color[3]);
	MIN1(alpha, draw->particles->verts[1].color[3]);
	MIN1(alpha, draw->particles->verts[2].color[3]);
	MIN1(alpha, draw->particles->verts[3].color[3]);

	bucket_params.data = data;
	bucket_params.zdist = zdist;
	assert(FINITE(bucket_params.zdist));

	bucket_params.subobject = instance_params->subobjects[0];
	if (!bucket_params.subobject->inited)
		initSubobjectMaterial(data, bucket_params.subobject);

	if (!bucket_params.subobject->material)
		return;

	alphaint = round(alpha * 255);
	bucket_params.alphaU8 = CLAMP(alphaint, 0, 255);
	sort_type = determineSortBucket(draw_list, bucket_params.subobject->material, type, bucket_params.alphaU8, bucket_params.zdist, draw->blend_flags, false, false, NEED_DOF, instance_params->needs_late_alpha_pass_if_need_grab && !rdr_state.disableTwoPassRefraction);

	//////////////////////////////////////////////////////////////////////////
	// determine shader
	{
		RdrLightList light_list = {0};
		light_list.comparator.use_vertex_only_lighting = draw_list->light_mode == RDLLM_VERTEX_ONLY; // TODO: use this always if we have no normalmap for cheaper fill?  Need to preload it too though!
		shader_num = determineShader(draw_list, sort_type, &light_list, bucket_params.subobject->material->flags, instance_params->aux_visual_pass);
	}

	//////////////////////////////////////////////////////////////////////////
	// create sort node
	bucket_params.local_sort_node = createSortNode(&bucket_params, sort_type);
	bucket_params.local_sort_node->drawable = &draw->base_drawable;
	bucket_params.local_sort_node->geo_handle_primary = -3;
	bucket_params.local_sort_node->tri_count = 2;
	bucket_params.local_sort_node->draw_type = draw->base_drawable.draw_type;
	bucket_params.local_sort_node->material = bucket_params.subobject->material;
	bucket_params.local_sort_node->category = category;
	bucket_params.local_sort_node->uses_far_depth_range = instance_params->uses_far_depth_range;

	if (draw_list->separate_hdr_pass)
		hdr_shader_num = calcHDRContribution(draw_list, bucket_params.local_sort_node, bucket_params.subobject->material, bucket_params.subobject->material->flags, zerovec4, draw->particles->verts[0].color, instance_params->screen_area, shader_num, sort_type, false);

	if (sort_type == RSBT_ALPHA_NEED_GRAB_LATE)
	{
		draw_list->need_texture_screen_color_late |= bucket_params.subobject->material->need_texture_screen_color;
		draw_list->need_texture_screen_depth_late |= bucket_params.subobject->material->need_texture_screen_depth;
	} else {
		draw_list->need_texture_screen_color |= bucket_params.subobject->material->need_texture_screen_color;
		draw_list->need_texture_screen_depth |= bucket_params.subobject->material->need_texture_screen_depth;
	}
	draw_list->need_texture_screen_color_blurred |= bucket_params.subobject->material->need_texture_screen_color_blurred;

	addToVisualBucket(&bucket_params, sort_type, instance_params->wireframe, shader_num, hdr_shader_num, hdr_shader_num.key != -1, instance_params);

	PERFINFO_AUTO_STOP_L2();
}

void rdrDrawListCheckLinearAllocator(RdrDrawList *draw_list)
{
	linAllocCheck(draw_list->current_data->allocator);
}

static const char * light_types[RDRLIGHT_TYPE_MAX] =
{
	"Non",
	"Dir",
	"Pnt",
	"Spt",
	"Prj"
};

const char * rdrLightGetTypeString(const RdrLight * light)
{
	return light_types[rdrGetSimpleLightType(light->light_type)];
}

void rdrLightDumpToStr( const RdrLight * rdr_light, int light_num, char * string, int max_len )
{
	snprintf_s(string, max_len, "Light %d: 0x%08p %s Sh:%c @(%f, %f, %f)", light_num, rdr_light, 
		rdrLightGetTypeString(rdr_light), 
		rdrIsShadowedLightType(rdr_light->light_type) ? 'Y' : 'N',
		rdr_light->world_mat[3][0], rdr_light->world_mat[3][1], rdr_light->world_mat[3][2]);
}

bool rdrDrawListHasData(const RdrDrawList *list) {
	return list && list->current_data;
}

void rdrSetDebugBuffer(RdrDevice *device, Vec4 parms) {
	Vec4 *data = wtAllocCmd(device->worker_thread, RDRCMD_DEBUG_BUFFER, sizeof(*data));
	copyVec4(parms,*data);
	wtSendCmd(device->worker_thread);
}
