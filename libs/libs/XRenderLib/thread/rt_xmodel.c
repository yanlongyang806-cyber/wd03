#include "memlog.h"
#include "ScratchStack.h"
#include "Color.h"
#include "qsortG.h"
#include "HashFunctions.h"
#include "systemspecs.h"
#include "file.h"

#include "RdrDrawList.h"
#include "rt_xmodel.h"
#include "rt_xdrawmode.h"
#include "rt_xgeo.h"
#include "rt_xdevice.h"

extern RdrSurfaceStateDX *current_state;

static const RdrVertexBufferObj null_vertex_buffer = { NULL };
static const RdrIndexBufferObj null_index_buffer = { NULL };


#if 0
	#define _FORCEINLINE __forceinline
#else
	// CD: looks like these functions are not getting any benefit from inlining.
	// In fact, they probably get slower because of code cache misses.
	#define _FORCEINLINE
#endif

_FORCEINLINE static void setRenderSettings(RdrDeviceDX *device, RdrSortNode *sort_node, RdrMaterialFlags material_flags, int depthLevel)
{
	int stencil_mode = 0;

	if (!material_flags)
		return;

	if (material_flags & RMATERIAL_NOFOG)
		rxbxFogPush(device, 0);

	if (material_flags & RMATERIAL_ADDITIVE)
	{
		rxbxBlendFuncPush(device, true, D3DBLEND_SRCALPHA, D3DBLEND_ONE, D3DBLENDOP_ADD);
		rxbxFogColorPush(device, zerovec4, zerovec4);
	}
	else if (material_flags & RMATERIAL_SUBTRACTIVE)
	{
		rxbxBlendFuncPush(device, true, D3DBLEND_SRCALPHA, D3DBLEND_ONE, D3DBLENDOP_REVSUBTRACT);
		rxbxFogColorPush(device, zerovec4, zerovec4);
	}

	if (material_flags & RMATERIAL_DOUBLESIDED)
		rxbxSetCullMode(device, CULLMODE_NONE);
	else if (material_flags & RMATERIAL_BACKFACE)
		rxbxSetCullMode(device, CULLMODE_FRONT);

	if (material_flags & RMATERIAL_NOZTEST)
		rxbxDepthTest(device, DEPTHTEST_OFF);
	else if (material_flags & RMATERIAL_DEPTH_EQUALS)
		rxbxDepthTest(device, DEPTHTEST_EQUAL);

	if (material_flags & RMATERIAL_ALPHA_TO_COVERAGE)
		rxbxAlphaToCoverage(device, true);

	if (material_flags & RMATERIAL_NOZWRITE)
		rxbxDepthWritePush(device, FALSE);

	if (material_flags & RMATERIAL_NOCOLORWRITE)
		rxbxColorWritePush(device, FALSE);

	if (material_flags & RMATERIAL_DEPTHBIAS)
	{
		F32 bias;
		if (material_flags & RMATERIAL_DECAL)
			bias = -1e-4f;
		else
			bias = -0.000001f;
		rxbxDepthBiasPushAdd(device, bias, -0.01f);
	}

#ifndef FORCEFARDEPTH_IN_VS
	if (material_flags & RMATERIAL_FORCEFARDEPTH)
		rxbxSetForceFarDepth(device, true, depthLevel);
#endif

	stencil_mode = rdrMaterialFlagsUnpackStencilMode(material_flags);
	if (stencil_mode)
		rxbxStencilMode(device, stencil_mode, sort_node->stencil_value);

}

_FORCEINLINE static void unsetRenderSettings(RdrDeviceDX *device, RdrMaterialFlags material_flags)
{
	int stencil_mode = 0;

	if (!material_flags)
		return;

	if (material_flags & RMATERIAL_NOFOG)
	{
		rxbxFogPop(device);
	}

	if (material_flags & (RMATERIAL_ADDITIVE|RMATERIAL_SUBTRACTIVE))
	{
		rxbxBlendFuncPop(device);
		rxbxFogColorPop(device);
	}

	if (material_flags & (RMATERIAL_DOUBLESIDED|RMATERIAL_BACKFACE))
		rxbxSetCullMode(device, CULLMODE_BACK);

	if (material_flags & (RMATERIAL_NOZTEST | RMATERIAL_DEPTH_EQUALS))
		rxbxDepthTest(device, DEPTHTEST_LEQUAL);

	if (material_flags & RMATERIAL_ALPHA_TO_COVERAGE)
		rxbxAlphaToCoverage(device, false);

	if (material_flags & RMATERIAL_NOZWRITE)
		rxbxDepthWritePop(device);

	if (material_flags & RMATERIAL_NOCOLORWRITE)
		rxbxColorWritePop(device);

	if (material_flags & RMATERIAL_DEPTHBIAS)
		rxbxDepthBiasPop(device);

#ifndef FORCEFARDEPTH_IN_VS
	if (material_flags & RMATERIAL_FORCEFARDEPTH)
		rxbxSetForceFarDepth(device, false, 0);
#endif

	stencil_mode = rdrMaterialFlagsUnpackStencilMode(material_flags);
	if (stencil_mode)
		rxbxStencilMode(device, RDRSTENCILMODE_NONE, 0);

}

_FORCEINLINE static void setCameraAxisOrientedMatrix(Mat4 in_out_world_matrix, bool axis_camera_facing)
{
	Vec3 scale;

	// TODO - Optimize this function! It gets called a LOT in grass heavy areas.

	PERFINFO_AUTO_START_FUNC();

	extractScale(in_out_world_matrix, scale);

	subVec3(in_out_world_matrix[3], current_state->inv_viewmat[3], in_out_world_matrix[2]);
	normalVec3(in_out_world_matrix[2]);

	crossVec3Up(in_out_world_matrix[2], in_out_world_matrix[0]);
	normalVec3(in_out_world_matrix[0]);

	crossVec3(in_out_world_matrix[2], in_out_world_matrix[0], in_out_world_matrix[1]);

	if(axis_camera_facing)
	{
		F32 pitch = asinf(in_out_world_matrix[2][1]);
		F32 actual_pitch = pitch * 0.5f;
		Mat4 temp_mat, temp_in_mat;
		Vec3 pyr;

		setVec3(pyr, -actual_pitch, 0.0f, 0.0f);
		createMat3YPR(temp_mat, pyr);
		copyMat4(in_out_world_matrix,temp_in_mat);
		zeroVec3(temp_mat[3]);
		mulMat4Inline(temp_in_mat, temp_mat, in_out_world_matrix);
	}

	scaleMat3Vec3(in_out_world_matrix, scale);

	PERFINFO_AUTO_STOP();
}

_FORCEINLINE static void getInstanceMatrixRows(RdrInstance *instance, Vec4 world_matrix_x, Vec4 world_matrix_y, Vec4 world_matrix_z)
{
	if (instance->camera_facing || instance->axis_camera_facing)
		setCameraAxisOrientedMatrix(instance->world_matrix, instance->axis_camera_facing);

	getMatRow(instance->world_matrix, 0, world_matrix_x);
	getMatRow(instance->world_matrix, 1, world_matrix_y);
	getMatRow(instance->world_matrix, 2, world_matrix_z);
}

_FORCEINLINE static void setInstanceParams(RdrDeviceDX *device, RdrInstanceLinkList *instance_link, const Vec4 tint_color, bool affect_state_directly)
{
	if (instance_link->instance->camera_facing || instance_link->instance->axis_camera_facing)
		setCameraAxisOrientedMatrix(instance_link->instance->world_matrix, instance_link->instance->axis_camera_facing);

	rxbxSetInstancedParams(device, instance_link->instance->world_matrix, tint_color, instance_link->per_drawable_data.instance_param, affect_state_directly);
}

__forceinline static void pushInstanceModelMatrix(RdrDeviceDX *device, RdrInstance *instance, bool is_skinned, bool camera_centered)
{
	if (instance->camera_facing || instance->axis_camera_facing)
		setCameraAxisOrientedMatrix(instance->world_matrix, instance->axis_camera_facing);

	rxbxPushModelMatrix(device, instance->world_matrix, is_skinned, camera_centered);
}

_FORCEINLINE static void getTintColor(Vec4 tint_color, RdrMaterialFlags material_flags, RdrDrawableGeo *draw, RdrSortNode *sort_node, RdrInstance *instance, RdrSortBucketType sort_bucket_type, const Vec3 override_color)
{
	static Vec3 tint_alpha = { 1,0,1 };
	static Vec3 tint_alpha_to_coverage = { 1,1,0 };
	static Vec3 tint_opaque = { 0,1,0 };
	static Vec3 tint_cutout = { 0,1,1 };
	static Vec3 tint_double_sided = { 1,0,1 };
	static Vec3 tint_single_sided = { 0,1,1 };

	//////////////////////////////////////////////////////////////////////////
	// color tinting
	if (rdr_state.show_alpha)
	{
		if (sort_bucket_type == RSBT_ALPHA || sort_bucket_type == RSBT_ALPHA_NEED_GRAB_PRE_DOF || sort_bucket_type == RSBT_ALPHA_NEED_GRAB ||
			sort_bucket_type == RSBT_ALPHA_PRE_DOF || sort_bucket_type == RSBT_ALPHA_LATE ||
			sort_bucket_type == RSBT_ALPHA_NEED_GRAB_LATE || sort_bucket_type == RSBT_ALPHA_LOW_RES_ALPHA ||
			sort_bucket_type == RSBT_ALPHA_LOW_RES_ADDITIVE || sort_bucket_type == RSBT_ALPHA_LOW_RES_SUBTRACTIVE)
			copyVec3(tint_alpha, tint_color);
		else if (sort_node->has_transparency && (material_flags & RMATERIAL_ALPHA_TO_COVERAGE))
			copyVec3(tint_alpha_to_coverage, tint_color);
		else if (!sort_node->has_transparency)
			copyVec3(tint_opaque, tint_color);
		else
			copyVec3(tint_cutout, tint_color);
	}
	else if (rdr_state.show_double_sided)
	{
		if (material_flags & RMATERIAL_DOUBLESIDED)
			copyVec3(tint_double_sided, tint_color);
		else
			copyVec3(tint_single_sided, tint_color);
	}
	else if (material_flags & RMATERIAL_NOTINT) // If we don't want tinting, overwrite the colors
	{
		setVec3same(tint_color, 1);
	}
	else if (override_color)
	{
		copyVec3(override_color, tint_color);
	}
	else
	{
		copyVec3(instance->color, tint_color);
	}

	tint_color[3] = instance->color[3];
}

__forceinline static const RdrLightData *findSunLight(const RdrLightList *light_list)
{
	int i;

	for (i = 0; i < MAX_NUM_OBJECT_LIGHTS; ++i)
	{
		if (light_list->lights[i] && light_list->lights[i]->direction_const_idx >= 0)
			return light_list->lights[i];
	}
	
	// if no directional light found, find the first light with a viewspace position
	for (i = 0; i < MAX_NUM_OBJECT_LIGHTS; ++i)
	{
		if (light_list->lights[i] && light_list->lights[i]->position_const_idx >= 0)
			return light_list->lights[i];
	}

	return NULL;
}

static void rxbxFillDrawableConstants(RdrDeviceDX *device, const RdrLightList *light_list, RdrInstanceLinkList *instance, RdrMaterial *material, bool is_visual_pass)
{
	int constant, max_constant = material->drawable_const_count;
	const RdrLightData *sun_light = NULL;

	for (constant = 0; constant < max_constant; ++constant )
	{
		int dest_constant = material->drawable_constants[constant].constant_index;
		int dest_constant_subindex = material->drawable_constants[constant].constant_subindex;
		switch (material->drawable_constants[constant].data_type)
		{
			xcase RDMP_INVMODELVIEWMATRIX:
			{
				Mat4 invModelView;
				Mat4 invModel;

				invertMat4ScaledCopy(instance->instance->world_matrix, invModel);
				mulMat4(invModel, current_state->inv_viewmat, invModelView);

				getMatRow(invModelView, 0, material->constants[dest_constant+0]);
				getMatRow(invModelView, 1, material->constants[dest_constant+1]);
				getMatRow(invModelView, 2, material->constants[dest_constant+2]);
			}

			xcase RDMP_VOLUMEFOGMATRIX:
				if (current_state->fog.volume)
				{
					getMatRow(current_state->fog_mat, 0, material->constants[dest_constant+0]);
					getMatRow(current_state->fog_mat, 1, material->constants[dest_constant+1]);
					getMatRow(current_state->fog_mat, 2, material->constants[dest_constant+2]);
				}

			xcase RDMP_SUNDIRECTIONVIEWSPACE:
				if (is_visual_pass)
				{
					if (!sun_light)
						sun_light = findSunLight(light_list);

					if (sun_light)
					{
						if (sun_light->direction_const_idx >= 0)
						{
							copyVec3(sun_light->normal[light_list->comparator.light_color_type].constants[sun_light->direction_const_idx], &material->constants[dest_constant][dest_constant_subindex]);
						}
						else
						{
							Vec4 model_pos;
							mulVecMat44(current_state->modelmat[3], current_state->viewmat, model_pos);
							subVec3(sun_light->normal[light_list->comparator.light_color_type].constants[sun_light->position_const_idx], model_pos, &material->constants[dest_constant][dest_constant_subindex]);
							normalVec3(&material->constants[dest_constant][dest_constant_subindex]);
						}
						material->constants[dest_constant][dest_constant_subindex+3] = 0;
						if ((dest_constant*4 + dest_constant_subindex) == material->instance_param_index)
							copyVec4(&material->constants[dest_constant][dest_constant_subindex], instance->per_drawable_data.instance_param);
					}
				}

			xcase RDMP_SUNCOLOR:
				if (is_visual_pass)
				{
					if (!sun_light)
						sun_light = findSunLight(light_list);

					if (sun_light)
					{
						copyVec3(sun_light->normal[light_list->comparator.light_color_type].constants[sun_light->color_const_idx], &material->constants[dest_constant][dest_constant_subindex]);
						material->constants[dest_constant][dest_constant_subindex + 3] = 0;
						if ((dest_constant*4 + dest_constant_subindex) == material->instance_param_index)
							copyVec4(&material->constants[dest_constant][dest_constant_subindex], instance->per_drawable_data.instance_param);
					}
				}

			xcase RDMP_MODELPOSITIONVIEWSPACE:
				mulVecMat44(current_state->modelmat[3], current_state->viewmat, &material->constants[dest_constant][dest_constant_subindex]);
				material->constants[dest_constant][dest_constant_subindex + 3] = dotVec3(&material->constants[dest_constant][dest_constant_subindex], &material->constants[dest_constant][dest_constant_subindex]);

			xcase RDMP_UNIQUEOFFSET:
			{
				U32 newid = MurmurHash2Int(instance->instance->instance_uid);
				material->constants[dest_constant][dest_constant_subindex] = newid & 0x3f;
				material->constants[dest_constant][dest_constant_subindex+1] = (newid >> 6) & 0x3f;
				if ((dest_constant*4 + dest_constant_subindex) == material->instance_param_index)
					copyVec2(&material->constants[dest_constant][dest_constant_subindex], instance->per_drawable_data.instance_param);
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////

__forceinline static RdrLightData ** getLightDataForMode(	RdrLightList *light_list, 
													 		RdrLightData **temp_array, 
													 		RdrDrawListPassData *pass_data, 
													 		RdrMaterialShader uberlight_shader_num, 
													 		RdrMaterialShader *uberlight_shader_num_out)
{
	if (rdrIsVisualShaderMode(pass_data->shader_mode))
	{
		uberlight_shader_num_out->key = uberlight_shader_num.key;
		return light_list->lights;
	}

	uberlight_shader_num_out->key = 0;

	if (pass_data->depth_only)
		return NULL;

	// fill in the temp array
	temp_array[0] = pass_data->shadow_light_data;
	return temp_array;
}

_FORCEINLINE static void drawModelStandard(RdrDeviceDX *device, RdrDrawableGeo *draw, RdrSortNode *sort_node, 
	RdrGeometryDataDX *geo_data, RdrMaterialFlags material_flags, RdrSortBucketType sort_bucket_type, 
	RdrDrawListPassData *pass_data, DrawModeBits draw_bits, bool set_params, bool use_hardware_instancing,
	bool manual_depth_test)
{
	RdrLightData *temp_shadow_array[MAX_NUM_SHADER_LIGHTS] = {0};
	RdrLightData **lights_for_mode=NULL;
	RdrMaterialShader uberlight_shader_num={0};
	bool is_visual_pass = pass_data->shader_mode == RDRSHDM_VISUAL || pass_data->shader_mode == RDRSHDM_VISUAL_HDR;
	RdrInstanceLinkList *primary_instance = sort_node->instances;
	RdrLightList *light_list = sort_node->lights;

	int i;
	int iResult;

	int vert_offset = 0;
	int tri_offset = 0;

	PERFINFO_AUTO_START_FUNC();

	PERFINFO_AUTO_START("rxbxFillDrawableConstants", 1);
	rxbxFillDrawableConstants(device, light_list, primary_instance, sort_node->material, is_visual_pass);

	PERFINFO_AUTO_STOP_START("rxbxBindMaterial", 1);

	ANALYSIS_ASSUME(pass_data->shader_mode < RDRSHDM_COUNT);

	if (!(draw_bits & DRAWBIT_VERTEX_ONLY_LIGHTING)) // Not pixel-shader lights
		lights_for_mode = getLightDataForMode(light_list, temp_shadow_array, pass_data, sort_node->uberlight_shader_num, &uberlight_shader_num);

	if (pass_data->shader_mode == RDRSHDM_SHADOW || pass_data->shader_mode == RDRSHDM_ZPREPASS)
	{
		iResult = rxbxBindMaterialForDepth(device, sort_node->material, sort_node->draw_shader_handle[pass_data->shader_mode] );	
	}
	else
	{
		iResult = rxbxBindMaterial(device, sort_node->material, lights_for_mode, light_list->comparator.light_color_type,
			sort_node->draw_shader_handle[manual_depth_test ? RDRSHDM_VISUAL_LRA_HIGH_RES : pass_data->shader_mode], uberlight_shader_num, sort_node->uses_shadowbuffer && is_visual_pass, sort_node->force_no_shadow_receive, manual_depth_test);
	}

	if (iResult == 0)
	{
		memlog_printf(0, "Missing pixel shader for model 0x%p 0x%p\n", draw, geo_data);

		//#define DO_NOT_DRAW_IF_NO_SHADER_COMPILED
#ifdef DO_NOT_DRAW_IF_NO_SHADER_COMPILED
		PERFINFO_AUTO_STOP();
		return;
#else
		// May match better than whatever shader was bound last...
		rxbxSetLoadingPixelShader(device);
#endif
	}

	PERFINFO_AUTO_STOP_START("vertex parameters", 1);

	for (i=0;i<geo_data->base_data.vert_usage.iNumPrimaryStreams;i++)
	{
		rxbxSetVertexStreamSource(device, i, geo_data->aVertexBufferInfos[i].buffer, geo_data->aVertexBufferInfos[i].iStride, 0);
	}
	rxbxSetIndices(device, geo_data->index_buffer, false);

	rxbxSetSpecialShaderParameters(device, draw->num_vertex_shader_constants, draw->vertex_shader_constants);

	if (set_params)
	{
		Vec4 tint_color;
		getTintColor(tint_color, material_flags, draw, sort_node, primary_instance->instance, sort_bucket_type, NULL);
		rxbxColorf(device, tint_color);
		rxbxInstanceParam(device, primary_instance->per_drawable_data.instance_param);
	}

	// After rxbxBindMaterial because that resets texture states
	for (i = 0; i < draw->num_vertex_textures; ++i)
	{
		PERFINFO_AUTO_STOP_START("rxbxBindVertexTexture", 1);
		rxbxBindVertexTexture(device, i, draw->vertex_textures[i]);
	}

	if (is_visual_pass)
	{
		rxbxSetupAmbientLight(	device, light_list->ambient_light, 
								light_list->ambient_multiplier, light_list->ambient_offset, 
								NULL, light_list->comparator.light_color_type);
	}

	PERFINFO_AUTO_STOP_START("drawTriangles", 1);
	rxbxDrawIndexedTriangles(device, 
		geo_data->base_data.subobject_tri_bases[sort_node->subobject_idx] + tri_offset,
		geo_data->base_data.subobject_tri_counts[sort_node->subobject_idx], 
		geo_data->base_data.vert_count, vert_offset, true );

	if (draw->num_vertex_textures)
		rxbxUnbindVertexTextures(device);

	PERFINFO_AUTO_STOP();
	PERFINFO_AUTO_STOP();
}

_FORCEINLINE static void drawModelDepthStandard(RdrDeviceDX *device, RdrDrawableGeo *draw, RdrSortNode *sort_node, 
												 RdrGeometryDataDX *geo_data, RdrDrawListPassData *pass_data, bool use_hardware_instancing)
{
	int i, tri_count = 0;

	int tri_offset = 0;
	int vert_offset = 0;

	PERFINFO_AUTO_START_FUNC();

	assert(sort_node->draw_shader_handle[pass_data->shader_mode] == -1);

	PERFINFO_AUTO_START("vertex parameters", 1);
		
	for (i=0;i<geo_data->base_data.vert_usage.iNumPrimaryStreams;i++)
	{
		rxbxSetVertexStreamSource(device, i, geo_data->aVertexBufferInfos[i].buffer, geo_data->aVertexBufferInfos[i].iStride, 0);
	}
	rxbxSetIndices(device, geo_data->index_buffer, false);

	rxbxSetSpecialShaderParameters(device, draw->num_vertex_shader_constants, draw->vertex_shader_constants);

	PERFINFO_AUTO_STOP_START("bind pixel shader", 1);
	rxbxMarkTexturesUnused(device, TEXTURE_PIXELSHADER);
	rxbxSetShadowBufferTextureActive(device, false);
	rxbxSetCubemapLookupTextureActive(device, false);
	if (!rxbxBindPixelShader(device, sort_node->draw_shader_handle[pass_data->shader_mode], NULL))
	{
		PERFINFO_AUTO_STOP();
		PERFINFO_AUTO_STOP();
		return;
	}
	rxbxUnbindUnusedTextures(device, TEXTURE_PIXELSHADER);

	// After rxbxBindMaterial because that resets texture states (actually, we don't call that in this function - RMARR)
	for (i = 0; i < draw->num_vertex_textures; ++i)
	{
		PERFINFO_AUTO_STOP_START("rxbxBindVertexTexture", 1);
		rxbxBindVertexTexture(device, i, draw->vertex_textures[i]);
	}

	PERFINFO_AUTO_STOP_START("drawTriangles", 1);
	for (i = 0; i < (int)sort_node->subobject_count; ++i)
		tri_count += geo_data->base_data.subobject_tri_counts[sort_node->subobject_idx + i];

	rxbxDrawIndexedTriangles(device, 
		geo_data->base_data.subobject_tri_bases[sort_node->subobject_idx] + tri_offset,
		tri_count, 
		geo_data->base_data.vert_count, vert_offset, true );

	if (draw->num_vertex_textures)
		rxbxUnbindVertexTextures(device);

	PERFINFO_AUTO_STOP();
	PERFINFO_AUTO_STOP();
}

_FORCEINLINE static void drawWireframeStandard(RdrDeviceDX *device, RdrDrawableGeo *draw, RdrSortNode *sort_node, RdrGeometryDataDX *geo_data, bool set_color)
{
	RdrInstance *primary_instance = sort_node->instances->instance;
	int i;

	PERFINFO_AUTO_START_FUNC();

	rxbxSetWireframePixelShader(device);
	rxbxBindWhiteTexture(device, 0);

	for (i = 0; i < draw->num_vertex_textures; ++i)
		rxbxBindVertexTexture(device, i, draw->vertex_textures[i]);

	for (i=0;i<geo_data->base_data.vert_usage.iNumPrimaryStreams;i++)
	{
		rxbxSetVertexStreamSource(device, i, geo_data->aVertexBufferInfos[i].buffer, geo_data->aVertexBufferInfos[i].iStride, 0);
	}
	rxbxSetIndices(device, geo_data->index_buffer, false);

	rxbxSetSpecialShaderParameters(device, draw->num_vertex_shader_constants, draw->vertex_shader_constants);

	rxbxFogPush(device, 0);
	rxbxSetCullMode(device, CULLMODE_NONE);
	rxbxSetFillMode(device, D3DFILL_WIREFRAME);

	if (set_color)
		rxbxColorf(device, unitvec4);

	if (rdr_state.disableWireframeDepthTest)
		rxbxDepthTest(device, DEPTHTEST_OFF);

	rxbxDrawIndexedTriangles(device, 
		geo_data->base_data.subobject_tri_bases[sort_node->subobject_idx],
		geo_data->base_data.subobject_tri_counts[sort_node->subobject_idx], 
		geo_data->base_data.vert_count, 0, true );

	if (draw->num_vertex_textures)
		rxbxUnbindVertexTextures(device);

	if (rdr_state.disableWireframeDepthTest)
		rxbxDepthTest(device, DEPTHTEST_LEQUAL);

	rxbxSetCullMode(device, CULLMODE_BACK);
	rxbxSetFillMode(device, D3DFILL_SOLID);
	rxbxFogPop(device);

	PERFINFO_AUTO_STOP();
}

//////////////////////////////////////////////////////////////////////////

static int rdrInstanceDistCmp(const RdrInstanceLinkList **instA, const RdrInstanceLinkList **instB)
{
	F32 f = (*instA)->zdist - (*instB)->zdist; // sort front to back
	if (f)
		return SIGN(f);
	return (intptr_t)(*instA) - (intptr_t)(*instB);
}

__forceinline static RdrInstanceLinkList **getSortedInstances(RdrInstanceLinkList *instance_link_list)
{
	RdrInstanceLinkList **instance_list;
	int i = 0, count;
	Vec4 view_z;

	PERFINFO_AUTO_START_FUNC();

	count = instance_link_list->count;

	instance_list = ScratchAlloc(count * sizeof(instance_list[0]));

	getMatRow(current_state->viewmat, 2, view_z);

	while (instance_link_list)
	{
		assert(i < count); // TODO(CD) removeme after crash is found and fixed
		instance_list[i] = instance_link_list;
		instance_list[i]->zdist = -(dotVec3(view_z, instance_list[i]->instance->world_mid) + view_z[3]);

		instance_link_list = instance_link_list->next;
		i++;
	}

	qsortG(instance_list, i, sizeof(instance_list[0]), rdrInstanceDistCmp);

	PERFINFO_AUTO_STOP();

	return instance_list;
}

static void setWorldTexParams(RdrDeviceDX *device, RdrMaterialFlags material_flags)
{
	Vec4 vecs[2] = {0};
	int c=0;
	if (material_flags & RMATERIAL_WORLD_TEX_COORDS_XZ)
	{
		c++;
		vecs[0][0]+=1;
		vecs[1][2]+=1;
	}
	if (material_flags & RMATERIAL_WORLD_TEX_COORDS_XY)
	{
		c++;
		vecs[0][0]+=1;
		vecs[1][1]+=-1;
	}
	if (material_flags & RMATERIAL_WORLD_TEX_COORDS_YZ)
	{
		c++;
		vecs[0][2]+=1;
		vecs[1][1]+=-1;
	}
	if (c>1) { // Normalize
		normalVec3(vecs[0]);
		normalVec3(vecs[1]);
	}
	rxbxSetWorldTexParams(device, vecs);
}

void getInstanceParam(Vec4 instance_param, RdrLightList *light_list, RdrInstanceLinkList *instance_link, RdrMaterial *material)
{
	if (material && material->drawable_const_count && material->instance_param_index >= 0)
	{
		// Only update the value if it was a drawable constant - this could be optimized quite a bit, I suspect, since most things won't have drawable constants
		rxbxFillDrawableConstants(NULL, light_list, instance_link, material, true);
	}
	copyVec4(instance_link->per_drawable_data.instance_param, instance_param);
}

// Shared between wireframe and normal drawing
_FORCEINLINE static void drawModelEx(RdrDeviceDX *device, RdrDrawableGeo *draw, RdrSortNode *sort_node, RdrDrawableSkinnedModel *draw_skin, RdrMaterialFlags material_flags, RdrSortBucketType sort_bucket_type, RdrDrawListPassData *pass_data, bool manual_depth_test, bool is_wireframe)
{
	DrawModeBits draw_bits = draw_skin ? (DRAWBIT_SKINNED | (sort_node->two_bone_skinning?DRAWBIT_SKINNED_ONLY_TWO_BONES:0)) : 0;
	RdrGeometryDataDX *geo_data_primary;
	RdrGeometryDataDX *geo_data_vertex_light = NULL;
	RdrInstance *primary_instance = sort_node->instances->instance;
	int do_morph = 0, vertex_lights = 0, num_instances = sort_node->instances->count;
	F32 morph_amt = 0, vlight_multiplier = 0, vlight_offset = 0;
	RdrLightList *light_list = sort_node->lights;
	bool use_hardware_instancing = false;
	RdrInstanceLinkList **sorted_instances = NULL;
	RdrInstanceLinkList *instance_link = NULL;
	RdrVertexBufferObj instance_vbo = { NULL };
	int instance_vbo_offset = 0;
	int depthLevel = 0;
	int iMaxSetStream;
	RdrGeoUsage usage;

	if (rdr_state.tessellateEverything)
		material_flags |= RMATERIAL_TESSELLATE;
	if (rdr_state.disableTessellation || !(device->rdr_caps_new.features_supported & FEATURE_TESSELLATION))
		material_flags &= ~RMATERIAL_TESSELLATE;
	if (sort_node->do_instancing)
	{
		// I found that on the GPU, instancing for fewer than 40 objects is not "worth it" on a GTX 260.  This could conceivably be different
		// on different cards, and there is also a CPU cost.
		static int iSmartNumber = 40;
		bool bSmartInstance = (rdr_state.hwInstancingMode == 1) || ((rdr_state.hwInstancingMode == 2) && num_instances > iSmartNumber);

		if (!is_wireframe && num_instances > 1 && !rdr_state.disableInstanceSorting && !sort_node->disable_instance_sorting)
			sorted_instances = getSortedInstances(sort_node->instances);
		else
			instance_link = sort_node->instances;

		// We continue to use the dumb number also, cause using instancing to render one object is just wrongheaded, even if the instancing mode is "always"
		use_hardware_instancing = bSmartInstance && num_instances > 1 && rdrSupportsFeature(&device->device_base, FEATURE_INSTANCING); // only try hardware instancing if the sort node supports it and there is more than one instance

		if (use_hardware_instancing)
		{
			int stride = sizeof(RdrInstanceBuffer);
			U8 *real_scratch_buffer = ScratchAlloc(num_instances * stride+0xf);
			U8 *instance_data;
			RdrInstanceLinkList *instance_link_orig = instance_link;
			int i;

			// memcpy is fast on aligned data
			instance_data = AlignPointerUpPow2(real_scratch_buffer,16);

			PERFINFO_AUTO_START("setup instance params", num_instances);
			for (i = 0; i < num_instances; ++i)
			{
				RdrInstanceLinkList *instance = sorted_instances ? sorted_instances[i] : instance_link;
				RdrInstanceBuffer *instance_buffer = (RdrInstanceBuffer *)(instance_data + i * stride);
				getInstanceMatrixRows(instance->instance, instance_buffer->world_matrix_x, instance_buffer->world_matrix_y, instance_buffer->world_matrix_z);
				if (is_wireframe)
				{
					setVec4same(instance_buffer->tint_color, 1);
				} else {
					getTintColor(instance_buffer->tint_color, material_flags, draw, sort_node, instance->instance, sort_bucket_type, NULL);
					getInstanceParam(instance_buffer->instance_param, light_list, instance, sort_node->material);
				}

				if (instance_link)
					instance_link = instance_link->next;
			}
			PERFINFO_AUTO_STOP();

			instance_link = instance_link_orig; // reset the link list head in case the VBO alloc fails

			use_hardware_instancing = rxbxAllocTempVBOMemory(device, instance_data, num_instances * stride, &instance_vbo, &instance_vbo_offset, false);
			ScratchFree(real_scratch_buffer);
		}
	}

	if (use_hardware_instancing)
		draw_bits |= DRAWBIT_INSTANCED;

	if (material_flags & (RMATERIAL_WORLD_TEX_COORDS_XZ|RMATERIAL_WORLD_TEX_COORDS_XY|RMATERIAL_WORLD_TEX_COORDS_YZ))
	{
		draw_bits |= DRAWBIT_WORLD_TEX_COORDS;
		setWorldTexParams(device, material_flags);
	} else if (material_flags & (RMATERIAL_SCREEN_TEX_COORDS))
		draw_bits |= DRAWBIT_SCREEN_TEX_COORDS;

#ifdef FORCEFARDEPTH_IN_VS
	if (material_flags & RMATERIAL_FORCEFARDEPTH) {
		if (sort_node->skyDepth)
			depthLevel = 2;
		else
			depthLevel = 1;
	}
#endif

	if (material_flags & RMATERIAL_ALPHA_FADE_PLANE)
		draw_bits |= DRAWBIT_ALPHA_FADE_PLANE;

	if (material_flags & RMATERIAL_VS_TEXCOORD_SPLAT)
		draw_bits |= DRAWBIT_VS_TEXCOORD_SPLAT;

	if (draw->base_drawable.draw_type == RTYPE_CURVED_MODEL)
	{
		draw_bits |= DRAWBIT_BEND;
		draw_bits &= ~(DRAWBIT_SKINNED|DRAWBIT_SKINNED_ONLY_TWO_BONES);
	}

	geo_data_primary = rxbxGetGeoDataDirect(device, sort_node->geo_handle_primary);
	if (!geo_data_primary)
	{
		if (sorted_instances)
			ScratchFree(sorted_instances);
		return;
	}

	if (primary_instance->morph > 0 && geo_data_primary->base_data.vert_usage.bHasSecondary)
	{
		assert(!sort_node->do_instancing); // morph not supported with instancing
		draw_bits |= DRAWBIT_MORPH;
		do_morph = 1;
		morph_amt = primary_instance->morph;
	}

	if (!is_wireframe && sort_node->draw_shader_handle[manual_depth_test ? RDRSHDM_VISUAL_LRA_HIGH_RES : pass_data->shader_mode] == -1)
	{
		draw_bits |= DRAWBIT_NOPIXELSHADER;
	}
	else if (!is_wireframe && pass_data->depth_only)
	{
		draw_bits |= DRAWBIT_DEPTHONLY;
	}
	else
	{
		if (!is_wireframe && light_list->comparator.use_single_dirlight)
		{
			draw_bits |= DRAWBIT_SINGLE_DIRLIGHT;
			rxbxSetSingleDirectionalLightParameters(device, light_list->lights[0], light_list->comparator.light_color_type);
		}
		else if (!is_wireframe && light_list->comparator.use_vertex_only_lighting)
		{
			draw_bits |= DRAWBIT_VERTEX_ONLY_LIGHTING|DRAWBIT_NO_NORMALMAP;
			rxbxSetAllVertexLightParameters(device, 0, light_list->lights[0], light_list->comparator.light_color_type);
			if (light_list->lights[1])
				rxbxSetAllVertexLightParameters(device, 1, light_list->lights[1], light_list->comparator.light_color_type);
			else
				draw_bits |= DRAWBIT_VERTEX_ONLY_LIGHTING_ONLY_ONE_LIGHT;
		}

		if (sort_node->material)
			if (sort_node->material->no_normalmap)
				draw_bits |= DRAWBIT_NO_NORMALMAP;

		if (!is_wireframe && light_list->vertex_light)
		{
			geo_data_vertex_light = rxbxGetGeoDataDirect(device, light_list->vertex_light->geo_handle_vertex_light);
			if (!geo_data_vertex_light)
			{
				// No vertex light data, might still be loading, but we should draw the object anyway!
			} else {
				if (geo_data_vertex_light->base_data.vert_count == geo_data_primary->base_data.vert_count)
				{
					sort_node->ignore_vertex_colors |= light_list->vertex_light->ignoreVertColor;
					draw_bits |= DRAWBIT_VERTEX_LIGHT;
					vertex_lights = 1;
					vlight_multiplier = light_list->vertex_light->vlight_multiplier;
					vlight_offset = light_list->vertex_light->vlight_offset;
				}
				else
				{
					static int shown_debug_msg = 0;
					
					if (!shown_debug_msg)
					{
						if (isDevelopmentMode() || IsDebuggerPresent())
						{
							ErrorDetailsf("Model: \"%s\" Vertex light \"%s\"", geo_data_primary->base_data.debug_name, geo_data_vertex_light->base_data.debug_name);
							ErrorDeferredf("%s", "The model bins you are using are not the same as when the map was binned. This may cause incorrect lighting.\n\n"
								"To fix this either delete the map bins or clear and re-get data/bin/geobin/ol from gimme.");
						} else {
							printf("The model bins you are using are not the same as when the map was binned. This may cause incorrect lighting.\n\n"
								"To fix this either delete the map bins or clear and re-get data/bin/geobin/ol from gimme.");
						}
						shown_debug_msg = 1;
					}
				}
			}
		}
	}

	if (!geo_data_primary)
	{
		if (sorted_instances)
			ScratchFree(sorted_instances);
		return;
	}

	if ((geo_data_primary->base_data.vert_usage.key & RUSE_COLORS) && !sort_node->ignore_vertex_colors)
	{
		draw_bits |= DRAWBIT_VERTEX_COLORS;
	}

	if (sort_node->has_wind && geo_data_primary->base_data.vert_usage.key & RUSE_COLORS)
	{
		draw_bits |= DRAWBIT_VERTEX_COLORS;
		draw_bits |= DRAWBIT_WIND;
		rxbxSetWindParams(device, sort_node->wind_params);
	}

	if ((geo_data_primary->base_data.vert_usage.key & (RUSE_TANGENTS | RUSE_BINORMALS)) != (RUSE_TANGENTS | RUSE_BINORMALS))
		draw_bits |= DRAWBIT_NO_NORMALMAP;

	if (sort_node->has_trunk_wind)
	{
		draw_bits |= DRAWBIT_TRUNK_WIND;
		rxbxSetWindParams(device, sort_node->wind_params);
	}

	if(!(geo_data_primary->base_data.vert_usage.key & RUSE_NORMALS) &&
	   !(geo_data_primary->base_data.vert_usage.key & RUSE_TEXCOORDS)) {
		draw_bits |= DRAWBIT_NO_NORMAL_NO_TEXCOORD;
	}

#if !PLATFORM_CONSOLE
	// Tessellation needs normals, so process full shader during z-prepass.
	if (material_flags & RMATERIAL_TESSELLATE && device->d3d11_device)
		draw_bits |= DRAWBIT_TESSELLATION;
	if ((draw_bits & DRAWBIT_TESSELLATION) &&
		(draw_bits & (DRAWBIT_NOPIXELSHADER|DRAWBIT_DEPTHONLY)))
	{
		draw_bits &= ~(DRAWBIT_NOPIXELSHADER|DRAWBIT_DEPTHONLY);
	}
	// For just the BEND shader, the depth does not match between the color and z-prepass, so use the
	//  full shader for z-prepass here.
	if (system_specs.videoCardVendorID == VENDOR_NV)
	{
		if ((draw_bits & DRAWBIT_BEND) &&
			(draw_bits & (DRAWBIT_NOPIXELSHADER|DRAWBIT_DEPTHONLY)))
		{
			draw_bits &= ~(DRAWBIT_NOPIXELSHADER|DRAWBIT_DEPTHONLY);
		}
	}
#endif

	usage = geo_data_primary->base_data.vert_usage;

	// these bits will determine which DX vertex declaration is chosen, and how it is built
	if (do_morph)
		usage.key |= RUSE_KEY_MORPH;
	if (vertex_lights && sort_node->material && geo_data_vertex_light)
		usage.key |= RUSE_KEY_VERTEX_LIGHTS;
	if (use_hardware_instancing)
		usage.key |= RUSE_KEY_INSTANCE;

	if (!rxbxSetupNormalDrawMode(device, &usage, draw_bits))
	{
		if (sorted_instances)
			ScratchFree(sorted_instances);
		return;
	}

	// the "main" streams will actually be set below, in a call to drawModelStandard, or what have you,
	// but we are going to set up the bonus streams here
	iMaxSetStream = geo_data_primary->base_data.vert_usage.iNumPrimaryStreams-1;

	if (do_morph)
	{
		assert(!sort_node->do_instancing); // morph not supported with instancing
		iMaxSetStream++;
		assert(iMaxSetStream < MAX_VERTEX_STREAMS);
		// the morph data is stored in the primary geo with the other data
		rxbxSetVertexStreamSource(device, iMaxSetStream, geo_data_primary->aVertexBufferInfos[iMaxSetStream].buffer, geo_data_primary->aVertexBufferInfos[iMaxSetStream].iStride, 0);
	}

	if (vertex_lights && sort_node->material && geo_data_vertex_light)
	{
		assert(!sort_node->do_instancing); // vertex lights not yet supported with instancing
		iMaxSetStream++;
		assert(iMaxSetStream < MAX_VERTEX_STREAMS);
		// vertex light data is stored in another geo in the light
		rxbxSetVertexStreamSource(device, iMaxSetStream, geo_data_vertex_light->aVertexBufferInfos[0].buffer, geo_data_vertex_light->aVertexBufferInfos[0].iStride, 0);
	}

	{
		int i;
		//clear the rest of the streams
		for (i=iMaxSetStream+1;i<MAX_VERTEX_STREAMS;i++)
		{
			rxbxSetVertexStreamSource(device, i, null_vertex_buffer, 0, 0);
		}
	}

	rxbxSetMorphAndVertexLightParams(device, morph_amt, vlight_multiplier, vlight_offset);

	if (draw_skin)
	{
		assert(!sort_node->do_instancing); // skinning not supported with instancing
		rxbxSetupSkinning(device, draw_skin, true);
		ADD_MISC_COUNT(draw_skin->num_bones,"Bones");
	}

	if (device->d3d11_device)
		rxbxBindTessellationShaders(device, sort_node->domain_material, draw_bits);

	if (!sort_node->do_instancing)
	{
		if (is_wireframe)
			drawWireframeStandard(device, draw, sort_node, geo_data_primary, true);
		else if (sort_node->material)
			drawModelStandard(device, draw, sort_node, geo_data_primary, material_flags, sort_bucket_type, pass_data, draw_bits, true, false, manual_depth_test);
		else
			drawModelDepthStandard(device, draw, sort_node, geo_data_primary, pass_data, false);
	}
	else if (use_hardware_instancing)
	{
		int i;
		assert(iMaxSetStream+1 < MAX_VERTEX_STREAMS);
		iMaxSetStream++;

		for (i=0;i<iMaxSetStream;i++)
		{
			rxbxSetVertexStreamIndexed(device, i, num_instances);
		}
		rxbxSetVertexStreamInstanced(device, iMaxSetStream);
		rxbxSetVertexStreamSource(device, iMaxSetStream, instance_vbo, sizeof(RdrInstanceBuffer), instance_vbo_offset);

		if (is_wireframe)
			drawWireframeStandard(device, draw, sort_node, geo_data_primary, false);
		else if (sort_node->material)
			drawModelStandard(device, draw, sort_node, geo_data_primary, material_flags, sort_bucket_type, pass_data, draw_bits, false, true, manual_depth_test);
		else
			drawModelDepthStandard(device, draw, sort_node, geo_data_primary, pass_data, true);

		for (i=0;i<=iMaxSetStream;i++)
		{
			rxbxSetVertexStreamNormal(device, i);
		}

	}
	else
	{
		int i, tri_count;

		if (sort_node->material)
		{
			tri_count = geo_data_primary->base_data.subobject_tri_counts[sort_node->subobject_idx];
		}
		else
		{
			tri_count = 0;
			for (i = 0; i < (int)sort_node->subobject_count; ++i)
				tri_count += geo_data_primary->base_data.subobject_tri_counts[sort_node->subobject_idx + i];
		}

		for (i = 0; i < num_instances; ++i)
		{
			RdrInstanceLinkList *instance = sorted_instances ? sorted_instances[i] : instance_link;
			Vec4 tint_color;
			assert(instance->instance); // TODO(CD) removeme after crash is found and fixed
			getInstanceParam(instance->per_drawable_data.instance_param, light_list, instance, sort_node->material);
			if (is_wireframe)
				setInstanceParams(device, instance, unitvec4, i > 0);
			else {
				getTintColor(tint_color, material_flags, draw, sort_node, instance->instance, sort_bucket_type, NULL);
				setInstanceParams(device, instance, tint_color, i > 0);
			}

			if (i==0)
			{
				// Setup and draw.
				if (is_wireframe)
					drawWireframeStandard(device, draw, sort_node, geo_data_primary, false);
				else if (sort_node->material)
					drawModelStandard(device, draw, sort_node, geo_data_primary, material_flags, sort_bucket_type, pass_data, draw_bits, false, false, manual_depth_test);
				else
					drawModelDepthStandard(device, draw, sort_node, geo_data_primary, pass_data, false);
			}
			else
			{
				// Just draw.
				rxbxDrawIndexedTriangles(device, 
					geo_data_primary->base_data.subobject_tri_bases[sort_node->subobject_idx],
					tri_count, 
					geo_data_primary->base_data.vert_count, 0, false );
			}

			if (instance_link)
				instance_link = instance_link->next;
		}
	}

	if (sorted_instances)
		ScratchFree(sorted_instances);

	{
		int i;
		for (i=1;i<=iMaxSetStream;i++)
		{
			rxbxSetVertexStreamSource(device, i, null_vertex_buffer, 0, 0);
		}
	}
}

void rxbxDrawModelDirect(RdrDeviceDX *device, RdrDrawableGeo *draw, RdrSortNode *sort_node, 
	 RdrSortBucketType sort_bucket_type, RdrDrawListPassData *pass_data, bool manual_depth_test)
{
	RdrInstance *primary_instance = SAFE_MEMBER(sort_node->instances, instance);
	RdrMaterialFlags material_flags = SAFE_MEMBER(sort_node->material, flags) | sort_node->add_material_flags;
	int depthLevel = 0;

	CHECKTHREAD;
	CHECKDEVICELOCK(device);
	
	if (!primary_instance || (rdr_state.dbg_type_flags & RDRTYPE_NONSKINNED))
		return;

	PERFINFO_AUTO_START("rxbxSet3DMode", 1);
	rxbxSet3DMode(device, material_flags & (RMATERIAL_WORLD_TEX_COORDS_XZ|RMATERIAL_WORLD_TEX_COORDS_XY|RMATERIAL_WORLD_TEX_COORDS_YZ));

	PERFINFO_AUTO_STOP_START("RenderState", 1);

	pushInstanceModelMatrix(device, primary_instance, false, sort_node->camera_centered);
	material_flags &= ~device->material_disable_flags;

	if (sort_node->uses_far_depth_range) {
		material_flags |= RMATERIAL_FORCEFARDEPTH;
	}

	if (material_flags & RMATERIAL_FORCEFARDEPTH)
		if (sort_node->skyDepth)
			depthLevel = 2;
		else
			depthLevel = 1;

	setRenderSettings(device, sort_node, material_flags, depthLevel);

	PERFINFO_AUTO_STOP_START("drawModelEx", 1);
	if (sort_bucket_type == RSBT_WIREFRAME)
		drawModelEx(device, draw, sort_node, NULL, material_flags, sort_bucket_type, pass_data, manual_depth_test, true);
	else
		drawModelEx(device, draw, sort_node, NULL, material_flags, sort_bucket_type, pass_data, manual_depth_test, false);

	PERFINFO_AUTO_STOP_START("unset state", 1);
	unsetRenderSettings(device, material_flags);
	rxbxPopModelMatrix(device);
	PERFINFO_AUTO_STOP();
}

void rxbxDrawSkinnedModelDirect(RdrDeviceDX *device, RdrDrawableSkinnedModel *draw_skin, RdrSortNode *sort_node, 
								RdrSortBucketType sort_bucket_type, RdrDrawListPassData *pass_data, bool manual_depth_test)
{
	RdrDrawableGeo *draw = &draw_skin->base_geo_drawable;
	RdrInstance *primary_instance = SAFE_MEMBER(sort_node->instances, instance);
	RdrMaterialFlags material_flags = SAFE_MEMBER(sort_node->material, flags) | sort_node->add_material_flags;

	CHECKTHREAD;
	CHECKDEVICELOCK(device);
	
	if (!primary_instance || (rdr_state.dbg_type_flags & RDRTYPE_SKINNED))
		return;

	rxbxSet3DMode(device, material_flags & (RMATERIAL_WORLD_TEX_COORDS_XZ|RMATERIAL_WORLD_TEX_COORDS_XY|RMATERIAL_WORLD_TEX_COORDS_YZ));

	pushInstanceModelMatrix(device, primary_instance, true, sort_node->camera_centered);
	material_flags &= ~device->material_disable_flags;
	if (sort_node->skyDepth)
		setRenderSettings(device, sort_node, material_flags, 2);
	else
		setRenderSettings(device, sort_node, material_flags, 0);

	if (sort_bucket_type == RSBT_WIREFRAME)
		drawModelEx(device, draw, sort_node, draw_skin, material_flags, sort_bucket_type, pass_data, manual_depth_test, true);
	else
		drawModelEx(device, draw, sort_node, draw_skin, material_flags, sort_bucket_type, pass_data, manual_depth_test, false);


	unsetRenderSettings(device, material_flags);
	rxbxPopModelMatrix(device);
}

__forceinline static void drawHeightMap(RdrDeviceDX *device, RdrDrawableGeo *draw, RdrSortNode *sort_node, RdrMaterialFlags material_flags, RdrSortBucketType sort_bucket_type, RdrDrawListPassData *pass_data)
{
	int draw_bits = 0;
	RdrGeometryDataDX *geo_data;
	RdrGeometryDataDX *terrainlight_geo_data = NULL;
	bool useGeoForStaticLights = false;
	RdrGeoUsage usage;

	if(draw->geo_handle_terrainlight && sort_node->material) {
		terrainlight_geo_data = rxbxGetGeoDataDirect(device, draw->geo_handle_terrainlight);
		if(terrainlight_geo_data) {
			useGeoForStaticLights = true;
		}
	}
	geo_data = rxbxGetGeoDataDirect(device, sort_node->geo_handle_primary);

	if (!geo_data)
		return;

	if (sort_node->draw_shader_handle[pass_data->shader_mode] == -1)
		draw_bits |= DRAWBIT_NOPIXELSHADER;
	else if (pass_data->depth_only)
		draw_bits |= DRAWBIT_DEPTHONLY;
	else {
		RdrLightList *light_list = sort_node->lights;
		if (light_list->comparator.use_single_dirlight) {
			draw_bits |= DRAWBIT_SINGLE_DIRLIGHT;
			rxbxSetSingleDirectionalLightParameters(device, light_list->lights[0], light_list->comparator.light_color_type);
		}
		else if (light_list->comparator.use_vertex_only_lighting)
		{
			draw_bits |= DRAWBIT_VERTEX_ONLY_LIGHTING|DRAWBIT_NO_NORMALMAP;
			rxbxSetAllVertexLightParameters(device, 0, light_list->lights[0], light_list->comparator.light_color_type);
			if (light_list->lights[1])
				rxbxSetAllVertexLightParameters(device, 1, light_list->lights[1], light_list->comparator.light_color_type);
			else
				draw_bits |= DRAWBIT_VERTEX_ONLY_LIGHTING_ONLY_ONE_LIGHT;
		}

		if (geo_data->base_data.vert_usage.key & RUSE_COLORS)
			draw_bits |= DRAWBIT_VERTEX_COLORS;
		if (sort_node->material && sort_node->material->no_normalmap)
			draw_bits |= DRAWBIT_NO_NORMALMAP;

		// Terrain vertex lighting.
		if(sort_node->material) {
			if(useGeoForStaticLights) {
				int iLightStream = terrainlight_geo_data->base_data.vert_usage.iNumPrimaryStreams;
				draw_bits |= DRAWBIT_MORPH;  // not really

				// We have a whole second geo right now for the light data, even though we only want one stream out of it.  :(
				// We stuff it into the stream slot after the ones from the base geo
				rxbxSetVertexStreamSource(device, geo_data->base_data.vert_usage.iNumPrimaryStreams, terrainlight_geo_data->aVertexBufferInfos[iLightStream].buffer, terrainlight_geo_data->aVertexBufferInfos[iLightStream].iStride, 0);
			}
		}
	}

	usage = geo_data->base_data.vert_usage;
	if (useGeoForStaticLights)
	{
		// we are shoe-horning an extra stream in from the other geo, and PRETENDING it's morph data, for the purposes of building the vertex decl.
		// This is the heightmap render path, though, so it doesn't try to use the ACTUAL morph data (which is only installed in drawModelEx)
		usage.key |= RUSE_KEY_MORPH;
	}
	if (rxbxSetupTerrainDrawMode(device, &usage, draw_bits))
	{
		if (sort_node->material)
			drawModelStandard(device, draw, sort_node, geo_data, material_flags, sort_bucket_type, pass_data, draw_bits, true, false, false);
		else
			drawModelDepthStandard(device, draw, sort_node, geo_data, pass_data, false);
	}
}


__forceinline static void drawHeightMapWireframe(RdrDeviceDX *device, RdrDrawableGeo *draw, RdrSortNode *sort_node)
{
	int draw_bits = 0;
	RdrGeoUsage usage;
	RdrGeometryDataDX *geo_data = rxbxGetGeoDataDirect(device, sort_node->geo_handle_primary);
	if (!geo_data)
		return;

	if (geo_data->base_data.vert_usage.key & RUSE_COLORS)
		draw_bits |= DRAWBIT_VERTEX_COLORS;

	usage = geo_data->base_data.vert_usage;
	usage.key |= RUSE_KEY_MORPH;
	if (rxbxSetupTerrainDrawMode(device, &usage, draw_bits))
	{
		drawWireframeStandard(device, draw, sort_node, geo_data, true);
	}
}

void rxbxDrawHeightMapDirect(RdrDeviceDX *device, RdrDrawableGeo *draw, RdrSortNode *sort_node, 
							 RdrSortBucketType sort_bucket_type, RdrDrawListPassData *pass_data)
{
	RdrInstance *primary_instance = SAFE_MEMBER(sort_node->instances, instance);
	RdrMaterialFlags material_flags = SAFE_MEMBER(sort_node->material, flags) | sort_node->add_material_flags;

	CHECKTHREAD;
	CHECKDEVICELOCK(device);

	if (!primary_instance || (rdr_state.dbg_type_flags & RDRTYPE_TERRAIN))
		return;

	rxbxSet3DMode(device, 0);

	pushInstanceModelMatrix(device, primary_instance, false, sort_node->camera_centered);
	material_flags &= ~device->material_disable_flags;
	if (sort_node->skyDepth)
		setRenderSettings(device, sort_node, material_flags, 2);
	else
		setRenderSettings(device, sort_node, material_flags, 0);

	if (sort_bucket_type == RSBT_WIREFRAME)
		drawHeightMapWireframe(device, draw, sort_node);
	else
		drawHeightMap(device, draw, sort_node, material_flags, sort_bucket_type, pass_data);

	unsetRenderSettings(device, material_flags);
	rxbxPopModelMatrix(device);
}

__forceinline static void drawStarField(RdrDeviceDX *device, RdrDrawableGeo *draw, RdrSortNode *sort_node, RdrMaterialFlags material_flags, RdrSortBucketType sort_bucket_type, RdrDrawListPassData *pass_data)
{
	RdrGeometryDataDX *geo_data = rxbxGetGeoDataDirect(device, sort_node->geo_handle_primary);
	RdrInstance *primary_instance = sort_node->instances->instance;
	int draw_bits = DRAWBIT_ZERO;

	if (!geo_data)
		return;

	if (SAFE_MEMBER(sort_node->material,no_normalmap))
		draw_bits |= DRAWBIT_NO_NORMALMAP;
	if (SAFE_MEMBER(sort_node->lights,comparator.use_single_dirlight))
		draw_bits |= DRAWBIT_SINGLE_DIRLIGHT;
	if (SAFE_MEMBER(sort_node->lights,comparator.use_vertex_only_lighting))
		draw_bits |= DRAWBIT_VERTEX_ONLY_LIGHTING|DRAWBIT_NO_NORMALMAP;
	//assert(SAFE_MEMBER(sort_node->lights,comparator.use_single_dirlight));

	rxbxSetupStarFieldDrawMode(device, &geo_data->base_data.vert_usage, primary_instance->camera_facing, SAFE_MEMBER(sort_node->lights,comparator.use_vertex_only_lighting));

	if (sort_node->material)
		drawModelStandard(device, draw, sort_node, geo_data, material_flags, sort_bucket_type, pass_data, 0, true, false, false);
	else
		drawModelDepthStandard(device, draw, sort_node, geo_data, pass_data, false);
}

__forceinline static void drawStarFieldWireframe(RdrDeviceDX *device, RdrDrawableGeo *draw, RdrSortNode *sort_node)
{
	RdrGeometryDataDX *geo_data = rxbxGetGeoDataDirect(device, sort_node->geo_handle_primary);
	RdrInstance *primary_instance = sort_node->instances->instance;

	if (!geo_data)
		return;

	rxbxSetupStarFieldDrawMode(device, &geo_data->base_data.vert_usage, primary_instance->camera_facing, false);

	drawWireframeStandard(device, draw, sort_node, geo_data, true);
}

void rxbxDrawStarFieldDirect(RdrDeviceDX *device, RdrDrawableGeo *draw, RdrSortNode *sort_node, 
							 RdrSortBucketType sort_bucket_type, RdrDrawListPassData *pass_data)
{
	RdrInstance *primary_instance = SAFE_MEMBER(sort_node->instances, instance);
	RdrMaterialFlags material_flags = SAFE_MEMBER(sort_node->material, flags) | sort_node->add_material_flags;

	CHECKTHREAD;
	CHECKDEVICELOCK(device);

	if (!primary_instance || (rdr_state.dbg_type_flags & RDRTYPE_NONSKINNED))
		return;

	rxbxSet3DMode(device, 0);

	rxbxPushModelMatrix(device, primary_instance->world_matrix, false, sort_node->camera_centered);
	material_flags &= ~device->material_disable_flags;
	setRenderSettings(device, sort_node, material_flags, 2);

	if (sort_bucket_type == RSBT_WIREFRAME)
		drawStarFieldWireframe(device, draw, sort_node);
	else
		drawStarField(device, draw, sort_node, material_flags, sort_bucket_type, pass_data);

	unsetRenderSettings(device, material_flags);
	rxbxPopModelMatrix(device);
}

static VertexComponentInfo velements_clothmesh[] = 
{
	{ offsetof(RdrClothMeshVertex,point), VPOS },
	{ offsetof(RdrClothMeshVertex,texcoord), VTEXCOORD32 },
	{ offsetof(RdrClothMeshVertex,normal), VNORMAL32 },
	{ offsetof(RdrClothMeshVertex,binormal), VBINORMAL32 },
	{ offsetof(RdrClothMeshVertex,tangent), VTANGENT32 },
	{ 0, VTERMINATE },
};


void rxbxDrawClothMeshDirect(RdrDeviceDX *device, RdrDrawableClothMesh *cloth_mesh, RdrSortNode *sort_node, RdrSortBucketType sort_bucket_type, RdrDrawListPassData *pass_data)
{
	int draw_bits = 0;
	RdrDrawableGeo* draw = &cloth_mesh->base_geo_drawable;
	RdrInstance *primary_instance = SAFE_MEMBER(sort_node->instances, instance);
	RdrInstanceLinkList *primary_instance_list = sort_node->instances;
	RdrMaterialFlags material_flags = SAFE_MEMBER(sort_node->material, flags) | sort_node->add_material_flags;

	material_flags &= ~RMATERIAL_DOUBLESIDED;
	material_flags &= ~RMATERIAL_BACKFACE;


	if (
		cloth_mesh->base_geo_drawable.subobject_count == 1 // if there is only one drawable subobject, it's trying to use the same material for both sides, so double side it
		|| sort_node->subobject_count == 2 // alternatively, if it's trying to draw both at the same time, it's probably z-prepass and just doublesiding it will be sufficient
		)
		material_flags |= RMATERIAL_DOUBLESIDED;
	else if (sort_node->subobject_idx == 0)
		material_flags |= RMATERIAL_BACKFACE;

	PERFINFO_AUTO_START_FUNC();

	CHECKTHREAD;
	CHECKDEVICELOCK(device);

	if (!primary_instance || (rdr_state.dbg_type_flags & RDRTYPE_PARTICLE))
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	assert(!sort_node->do_instancing);

	// set state
	rxbxSet3DMode(device, material_flags & (RMATERIAL_WORLD_TEX_COORDS_XZ|RMATERIAL_WORLD_TEX_COORDS_XY|RMATERIAL_WORLD_TEX_COORDS_YZ));

	material_flags &= ~device->material_disable_flags;
	if (sort_node->skyDepth)
		setRenderSettings(device, sort_node, material_flags, 2);
	else
		setRenderSettings(device, sort_node, material_flags, 0);

	if (material_flags & (RMATERIAL_WORLD_TEX_COORDS_XZ|RMATERIAL_WORLD_TEX_COORDS_XY|RMATERIAL_WORLD_TEX_COORDS_YZ))
	{
		draw_bits |= DRAWBIT_WORLD_TEX_COORDS;
		setWorldTexParams(device, material_flags);
	} else if (material_flags & (RMATERIAL_SCREEN_TEX_COORDS))
		draw_bits |= DRAWBIT_SCREEN_TEX_COORDS;

	if (material_flags & RMATERIAL_ALPHA_FADE_PLANE)
		draw_bits |= DRAWBIT_ALPHA_FADE_PLANE;

	if (material_flags & RMATERIAL_VS_TEXCOORD_SPLAT)
		draw_bits |= DRAWBIT_VS_TEXCOORD_SPLAT;

	if (sort_node->draw_shader_handle[pass_data->shader_mode] == -1)
	{
		draw_bits |= DRAWBIT_NOPIXELSHADER;
	}
	else if (pass_data->depth_only)
	{
		draw_bits |= DRAWBIT_DEPTHONLY;
	}
	else
	{
		RdrLightList *light_list = sort_node->lights;
		if (light_list->comparator.use_single_dirlight)
		{
			draw_bits |= DRAWBIT_SINGLE_DIRLIGHT;
			rxbxSetSingleDirectionalLightParameters(device, light_list->lights[0], light_list->comparator.light_color_type);
		}
		else if (light_list->comparator.use_vertex_only_lighting)
		{
			draw_bits |= DRAWBIT_VERTEX_ONLY_LIGHTING|DRAWBIT_NO_NORMALMAP;
			rxbxSetAllVertexLightParameters(device, 0, light_list->lights[0], light_list->comparator.light_color_type);
			if (light_list->lights[1])
				rxbxSetAllVertexLightParameters(device, 1, light_list->lights[1], light_list->comparator.light_color_type);
			else
				draw_bits |= DRAWBIT_VERTEX_ONLY_LIGHTING_ONLY_ONE_LIGHT;
		}

		ANALYSIS_ASSUME(sort_node->material); // Only depth_only/null pixel shaders do not have a material
		if (sort_node->material->no_normalmap)
			draw_bits |= DRAWBIT_NO_NORMALMAP;
	}

	if (!rxbxSetupNormalClothDrawMode(
			device, velements_clothmesh, draw_bits,
			(draw_bits & DRAWBIT_NO_NORMALMAP) ?
			&device->cloth_mesh_vertex_declaration_no_normalmap :
			&device->cloth_mesh_vertex_declaration))
	{
		unsetRenderSettings(device, material_flags);
		PERFINFO_AUTO_STOP();
		return;
	}

	if (sort_bucket_type == RSBT_WIREFRAME)
	{
		// cribbed from drawWireframeStandard

		rxbxSetWireframePixelShader(device);
		rxbxBindWhiteTexture(device, 0);

		rxbxSetSpecialShaderParameters(device, draw->num_vertex_shader_constants, draw->vertex_shader_constants);

		rxbxFogPush(device, 0);
		rxbxSetCullMode(device, CULLMODE_NONE);
		rxbxSetFillMode(device, D3DFILL_WIREFRAME);

		rxbxColorf(device, unitvec4);

		// draw
	}
	else
	{
		// cribbed from drawModelStandard
		if (sort_node->material)
		{
			RdrLightData *temp_shadow_array[MAX_NUM_SHADER_LIGHTS] = {0};
			RdrLightData **lights_for_mode=NULL;
			RdrMaterialShader uberlight_shader_num={0};
			bool is_visual_pass = pass_data->shader_mode == RDRSHDM_VISUAL || pass_data->shader_mode == RDRSHDM_VISUAL_HDR;
			Vec4 tint_color;
			RdrLightList *light_list = sort_node->lights;

			PERFINFO_AUTO_START("rxbxFillDrawableConstants", 1);
			rxbxFillDrawableConstants(device, light_list, primary_instance_list, sort_node->material, is_visual_pass);

			PERFINFO_AUTO_STOP_START("vertex parameters", 1);
			rxbxSetSpecialShaderParameters(device, draw->num_vertex_shader_constants, draw->vertex_shader_constants);
			getTintColor(tint_color, material_flags, draw, sort_node, primary_instance, sort_bucket_type, sort_node->subobject_idx > 0 ? cloth_mesh->color2 : NULL);
			rxbxColorf(device, tint_color);
			rxbxInstanceParam(device, primary_instance_list->per_drawable_data.instance_param);

			PERFINFO_AUTO_STOP_START("rxbxBindMaterial", 1);

			ANALYSIS_ASSUME(pass_data->shader_mode < RDRSHDM_COUNT);
			if (!(draw_bits & DRAWBIT_VERTEX_ONLY_LIGHTING)) // Not pixel-shader lights
				lights_for_mode = getLightDataForMode(light_list, temp_shadow_array, pass_data, sort_node->uberlight_shader_num, &uberlight_shader_num);
			if (0==rxbxBindMaterial(device, sort_node->material, lights_for_mode, light_list->comparator.light_color_type,
				sort_node->draw_shader_handle[pass_data->shader_mode], uberlight_shader_num, sort_node->uses_shadowbuffer && is_visual_pass, sort_node->force_no_shadow_receive, false))
			{
				memlog_printf(0, "Missing pixel shader for cloth mesh model 0x%p\n", draw);

#ifdef DO_NOT_DRAW_IF_NO_SHADER_COMPILED
				unsetRenderSettings(device, material_flags);
				PERFINFO_AUTO_STOP();
				return;
#else
				// May match better than whatever shader was bound last...
				rxbxSetLoadingPixelShader(device);
#endif
			}

			if (pass_data->shader_mode == RDRSHDM_VISUAL || pass_data->shader_mode == RDRSHDM_VISUAL_HDR)
			{
				rxbxSetupAmbientLight(	device, light_list->ambient_light, 
										light_list->ambient_multiplier, light_list->ambient_offset, 
										NULL, light_list->comparator.light_color_type);
			}

			PERFINFO_AUTO_STOP();
		}
		else // cribbed from drawModelDepthStandard
		{
			assert(sort_node->draw_shader_handle[pass_data->shader_mode] == -1);

			PERFINFO_AUTO_START("vertex parameters", 1);
			rxbxSetSpecialShaderParameters(device, draw->num_vertex_shader_constants, draw->vertex_shader_constants);

			PERFINFO_AUTO_STOP_START("bind pixel shader", 1);
			rxbxMarkTexturesUnused(device, TEXTURE_PIXELSHADER);
			rxbxSetShadowBufferTextureActive(device, false);
			rxbxSetCubemapLookupTextureActive(device, false);
			if (!rxbxBindPixelShader(device, sort_node->draw_shader_handle[pass_data->shader_mode], NULL))
			{
				unsetRenderSettings(device, material_flags);
				PERFINFO_AUTO_STOP();
				PERFINFO_AUTO_STOP();
				return;
			}
			rxbxUnbindUnusedTextures(device, TEXTURE_PIXELSHADER);

			PERFINFO_AUTO_STOP();

		}
	}

	// draw strips
	{
		U32 strip_index;
		for (strip_index=0; strip_index<cloth_mesh->strip_count; ++strip_index)
		{
			RdrDrawableIndexedTriStrip* strip = &cloth_mesh->tri_strips[strip_index];
			rxbxDrawIndexedTrianglesUP(device, strip->num_indices/3, strip->indices, cloth_mesh->vert_count, cloth_mesh->verts, sizeof(RdrClothMeshVertex));
		}
	}

	if (sort_bucket_type == RSBT_WIREFRAME)
	{

		rxbxSetCullMode(device, CULLMODE_BACK);
		rxbxSetFillMode(device, D3DFILL_SOLID);
		rxbxFogPop(device);
	}

	// unset state
	unsetRenderSettings(device, material_flags);

	PERFINFO_AUTO_STOP();
}
