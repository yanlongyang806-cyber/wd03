#include "qsortG.h"


#include "RdrLightAssembly.h"

#include "GfxLightsPrivate.h"

#include "GfxPostprocess.h"
#include "GfxWorld.h"
#include "GfxGeo.h"
#include "GfxMaterials.h"
#include "GfxTexturesInline.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););


typedef struct LightModelDesc
{
	const char *model_name;
	const char *model_filename;
	GeoHandle geo_handle;
	Model *model;
	ModelLOD *model_lod;
} LightModelDesc;

enum
{
	LMODEL_SPHERE,
	LMODEL_CONE180,
	LMODEL_CONE135,
	LMODEL_CONE90,
	LMODEL_CONE45,
	LMODEL_PROJ180,
	LMODEL_PROJ135,
	LMODEL_PROJ90,
	LMODEL_PROJ45,
};

static LightModelDesc light_models[] = 
{
	{ "sys_light_sphere", "object_library/system/lights/sys_lights.geo", 0, NULL },
	{ "sys_light_cone180", "object_library/system/lights/sys_lights.geo", 0, NULL },
	{ "sys_light_cone135", "object_library/system/lights/sys_lights.geo", 0, NULL },
	{ "sys_light_cone90", "object_library/system/lights/sys_lights.geo", 0, NULL },
	{ "sys_light_cone45", "object_library/system/lights/sys_lights.geo", 0, NULL },
	{ "sys_light_proj180", "object_library/system/lights/sys_lights.geo", 0, NULL },
	{ "sys_light_proj135", "object_library/system/lights/sys_lights.geo", 0, NULL },
	{ "sys_light_proj90", "object_library/system/lights/sys_lights.geo", 0, NULL },
	{ "sys_light_proj45", "object_library/system/lights/sys_lights.geo", 0, NULL },
};

__forceinline static LightModelDesc *getModelDescForConeAngle(F32 cone_angle1, F32 cone_angle2, bool is_projector)
{
	F32 cone_angle = MAX(cone_angle1, cone_angle2);

	if (is_projector)
	{
		if (cone_angle > 135.f)
			return &light_models[LMODEL_PROJ180];
		if (cone_angle > 90.f)
			return &light_models[LMODEL_PROJ135];
		if (cone_angle > 45.f)
			return &light_models[LMODEL_PROJ90];
		return &light_models[LMODEL_PROJ45];
	}

	if (cone_angle > 180.f)
		return &light_models[LMODEL_SPHERE];
	if (cone_angle > 135.f)
		return &light_models[LMODEL_CONE180];
	if (cone_angle > 90.f)
		return &light_models[LMODEL_CONE135];
	if (cone_angle > 45.f)
		return &light_models[LMODEL_CONE90];
	return &light_models[LMODEL_CONE45];
}

void gfxUpdateDeferredLightModels(void)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(light_models); ++i)
	{
		ModelToDraw models[NUM_MODELTODRAWS] = {0};

		light_models[i].model = modelFind(light_models[i].model_name, true, WL_FOR_UTIL);

		// use LOD 0
		if (light_models[i].model && gfxDemandLoadModel(light_models[i].model, models, ARRAY_SIZE(models), 0, 1, 0, NULL, light_models[i].model->radius))
		{
			gfxGeoIncrementUsedCount(models[0].model->geo_render_info, -1, false); // Not actually used
			light_models[i].geo_handle = models[0].geo_handle_primary;
			light_models[i].model_lod = models[0].model;
		} else
			light_models[i].geo_handle = 0;
	}
}

static int cmpLightsDeferred(const GfxLight **light1, const GfxLight **light2)
{
	F32 t = (*light1)->deferred_sort_param - (*light2)->deferred_sort_param;
	if (t < 0)
		return -1;
	if (t > 0)
		return 1;
	return 0;
}

__forceinline static void gfxDoDeferredUnlit(GfxLight *light, const Mat4 view_mat, TexHandle depthBufferTexHandle)
{
	RdrScreenPostProcess ppscreen = {0};
	TexHandle textures[SBUF_MAX];
	RdrSurface *source_surface = gfx_state.currentAction->bufferMRT->surface;
	GfxSpecialShader shader_num = GSS_DEFERRED_UNLIT;
	RdrMaterialShader material_shader = {0};
	const RdrLightDefinition *light_def = NULL;

	textures[0] = rdrSurfaceToTexHandle(source_surface, SBUF_0);
	textures[1] = rdrSurfaceToTexHandle(source_surface, SBUF_1);
	textures[2] = rdrSurfaceToTexHandle(source_surface, SBUF_2);
	textures[3] = rdrSurfaceToTexHandle(source_surface, SBUF_3);
	textures[4] = depthBufferTexHandle;
	ppscreen.material.tex_count = depthBufferTexHandle ? ARRAY_SIZE(textures) : ARRAY_SIZE(textures) - 1;
	ppscreen.material.textures = textures;
	if (light)
	{
		shader_num = GSS_DEFERRED_UNLIT_AND_LIGHT;
		material_shader.lightMask = rdrGetMaterialShaderType(light->rdr_light.light_type, 0);
	}

	ppscreen.shader_handle = gfxDemandLoadSpecialShaderEx(shader_num, 0, ARRAY_SIZE(textures), material_shader, &light_def);

	ppscreen.tex_width = source_surface->width_nonthread;
	ppscreen.tex_height = source_surface->height_nonthread;

	ppscreen.depth_test_mode = RPPDEPTHTEST_LEQUAL;
	ppscreen.write_depth = 1;
	ppscreen.fog = 1;

	if (light && light_def)
	{
		U16 const_count = 0, tex_count = 0;
		rdrLightAddConstAndTexCounts(light_def, light->rdr_light.light_type, &const_count, &tex_count, NULL, NULL, false);
		ppscreen.material.constants = _alloca(const_count * sizeof(Vec4));

		if (rdrIsShadowedLightType(light->rdr_light.light_type))
			ppscreen.material.const_count += rdrLightFillMaterialConstants(ppscreen.material.constants + ppscreen.material.const_count, light_def, &light->rdr_light, light->rdr_light.light_type, view_mat, RLDEFTYPE_SHADOW_TEST, RLCT_WORLD, false);
		ppscreen.material.const_count += rdrLightFillMaterialConstants(ppscreen.material.constants + ppscreen.material.const_count, light_def, &light->rdr_light, light->rdr_light.light_type, view_mat, RLDEFTYPE_NORMAL, RLCT_WORLD, false);
		if (tex_count)
		{
			ppscreen.material.textures = _alloca((ARRAY_SIZE(textures) + tex_count) * sizeof(TexHandle));
			CopyStructs(ppscreen.material.textures, textures, ARRAY_SIZE(textures));
			if (rdrIsShadowedLightType(light->rdr_light.light_type))
				ppscreen.material.tex_count += rdrLightFillMaterialTextures(ppscreen.material.textures + ppscreen.material.tex_count, light_def, &light->rdr_light, light->rdr_light.light_type, RLDEFTYPE_SHADOW_TEST, RLCT_WORLD, false);
			ppscreen.material.tex_count += rdrLightFillMaterialTextures(ppscreen.material.textures + ppscreen.material.tex_count, light_def, &light->rdr_light, light->rdr_light.light_type, RLDEFTYPE_NORMAL, RLCT_WORLD, false);
		}
	}

	gfxPostprocessScreen(&ppscreen);
}

static void gfxDoDeferredDirectionalLight(GfxLight *light, const Mat4 view_mat, TexHandle depthBufferTexHandle)
{
	RdrScreenPostProcess ppscreen = {0};
	TexHandle textures[SBUF_MAX];
	RdrSurface *source_surface = gfx_state.currentAction->bufferMRT->surface;
	const RdrLightDefinition *light_def = NULL;

	textures[0] = rdrSurfaceToTexHandle(source_surface, SBUF_0);
	textures[1] = rdrSurfaceToTexHandle(source_surface, SBUF_1);
	textures[2] = rdrSurfaceToTexHandle(source_surface, SBUF_2);
	textures[3] = rdrSurfaceToTexHandle(source_surface, SBUF_3);
	textures[4] = depthBufferTexHandle;
	ppscreen.material.tex_count = depthBufferTexHandle ? ARRAY_SIZE(textures) : ARRAY_SIZE(textures) - 1;
	ppscreen.material.textures = textures;
	ppscreen.shader_handle = gfxDemandLoadSpecialShaderEx(GSS_DEFERRED_LIGHT, 0, ARRAY_SIZE(textures), getRdrMaterialShader(0,rdrGetMaterialShaderType(light->rdr_light.light_type, 0)), &light_def);

	ppscreen.tex_width = source_surface->width_nonthread;
	ppscreen.tex_height = source_surface->height_nonthread;

	ppscreen.blend_type = RPPBLEND_ADD;

	if (light_def)
	{
		U16 const_count = 0, tex_count = 0;
		rdrLightAddConstAndTexCounts(light_def, light->rdr_light.light_type, &const_count, &tex_count, NULL, NULL, false);
		ppscreen.material.constants = _alloca(const_count * sizeof(Vec4));
		if (rdrIsShadowedLightType(light->rdr_light.light_type))
			ppscreen.material.const_count += rdrLightFillMaterialConstants(ppscreen.material.constants + ppscreen.material.const_count, light_def, &light->rdr_light, light->rdr_light.light_type, view_mat, RLDEFTYPE_SHADOW_TEST, RLCT_WORLD, false);
		ppscreen.material.const_count += rdrLightFillMaterialConstants(ppscreen.material.constants + ppscreen.material.const_count, light_def, &light->rdr_light, light->rdr_light.light_type, view_mat, RLDEFTYPE_NORMAL, RLCT_WORLD, false);
		if (tex_count)
		{
			ppscreen.material.textures = _alloca((tex_count + ARRAY_SIZE(textures)) * sizeof(TexHandle));
			CopyStructs(ppscreen.material.textures, textures, ARRAY_SIZE(textures));
			if (rdrIsShadowedLightType(light->rdr_light.light_type))
				ppscreen.material.tex_count += rdrLightFillMaterialTextures(ppscreen.material.textures + ppscreen.material.tex_count, light_def, &light->rdr_light, light->rdr_light.light_type, RLDEFTYPE_SHADOW_TEST, RLCT_WORLD, false);
			ppscreen.material.tex_count += rdrLightFillMaterialTextures(ppscreen.material.textures + ppscreen.material.tex_count, light_def, &light->rdr_light, light->rdr_light.light_type, RLDEFTYPE_NORMAL, RLCT_WORLD, false);
		}
	}

	gfxPostprocessScreen(&ppscreen);
}

static void gfxDoDeferredPointLight(GfxLight *light, const Mat4 view_mat, TexHandle depthBufferTexHandle)
{
	Vec3 light_vs;
	RdrShapePostProcess ppshape = {0};
	TexHandle textures[SBUF_MAX];
	RdrSurface *source_surface = gfx_state.currentAction->bufferMRT->surface;
	LightModelDesc *model_desc = getModelDescForConeAngle(360, 360, false);
	const RdrLightDefinition *light_def = NULL;

	ppshape.geometry = model_desc->geo_handle;

	if (!ppshape.geometry || light->rdr_light.point_spot_params.outer_radius <= 0 || !model_desc->model_lod->geo_render_info || !(model_desc->model_lod->geo_render_info->geo_loaded & gfx_state.currentRendererFlag))
		return;

	gfxGeoIncrementUsedCount(model_desc->model_lod->geo_render_info, 1, false);

	textures[0] = rdrSurfaceToTexHandle(source_surface, SBUF_0);
	textures[1] = rdrSurfaceToTexHandle(source_surface, SBUF_1);
	textures[2] = rdrSurfaceToTexHandle(source_surface, SBUF_2);
	textures[3] = rdrSurfaceToTexHandle(source_surface, SBUF_3);
	textures[4] = depthBufferTexHandle;
	ppshape.material.tex_count = depthBufferTexHandle ? ARRAY_SIZE(textures) : ARRAY_SIZE(textures) - 1;
	ppshape.material.textures = textures;
	ppshape.shader_handle = gfxDemandLoadSpecialShaderEx(GSS_DEFERRED_LIGHT, 0, ARRAY_SIZE(textures), getRdrMaterialShader(0,rdrGetMaterialShaderType(light->rdr_light.light_type, 0)), &light_def);

	ppshape.tex_width = source_surface->width_nonthread;
	ppshape.tex_height = source_surface->height_nonthread;

	scaleMat3(unitmat, ppshape.world_matrix, light->rdr_light.point_spot_params.outer_radius);
	copyVec3(light->rdr_light.world_mat[3], ppshape.world_matrix[3]);

	ppshape.blend_type = RPPBLEND_ADD;

	if (light_def)
	{
		U16 const_count = 0, tex_count = 0;
		rdrLightAddConstAndTexCounts(light_def, light->rdr_light.light_type, &const_count, &tex_count, NULL, NULL, false);
		ppshape.material.constants = _alloca(const_count * sizeof(Vec4));
		if (rdrIsShadowedLightType(light->rdr_light.light_type))
			ppshape.material.const_count += rdrLightFillMaterialConstants(ppshape.material.constants + ppshape.material.const_count, light_def, &light->rdr_light, light->rdr_light.light_type, view_mat, RLDEFTYPE_SHADOW_TEST, RLCT_WORLD, false);
		ppshape.material.const_count += rdrLightFillMaterialConstants(ppshape.material.constants + ppshape.material.const_count, light_def, &light->rdr_light, light->rdr_light.light_type, view_mat, RLDEFTYPE_NORMAL, RLCT_WORLD, false);
		if (tex_count)
		{
			ppshape.material.textures = _alloca((tex_count + ARRAY_SIZE(textures)) * sizeof(TexHandle));
			CopyStructs(ppshape.material.textures, textures, ARRAY_SIZE(textures));
			if (rdrIsShadowedLightType(light->rdr_light.light_type))
				ppshape.material.tex_count += rdrLightFillMaterialTextures(ppshape.material.textures + ppshape.material.tex_count, light_def, &light->rdr_light, light->rdr_light.light_type, RLDEFTYPE_SHADOW_TEST, RLCT_WORLD, false);
			ppshape.material.tex_count += rdrLightFillMaterialTextures(ppshape.material.textures + ppshape.material.tex_count, light_def, &light->rdr_light, light->rdr_light.light_type, RLDEFTYPE_NORMAL, RLCT_WORLD, false);
		}
	}

	// if the light volume intersects the near plane, use the volume's back faces and don't depth test
	mulVecMat4(light->rdr_light.world_mat[3], view_mat, light_vs);
	if (-light_vs[2] < light->rdr_light.point_spot_params.outer_radius * 1.25f)
		ppshape.draw_back_faces = 1;
	else
		ppshape.depth_test_mode = RPPDEPTHTEST_LEQUAL;
	ppshape.no_offset = 1;

	rdrPostProcessShape(gfx_state.currentDevice->rdr_device, &ppshape);
}

void gfxDoDebugPointLight(GfxLight *light, RdrDrawList *draw_list)
{
	LightModelDesc *model_desc = getModelDescForConeAngle(360, 360, false);
	RdrDrawableGeo *geo_draw;
	int i;

	if (!model_desc->model || !model_desc->geo_handle || light->rdr_light.point_spot_params.outer_radius <= 0 || !model_desc->model_lod->geo_render_info || !(model_desc->model_lod->geo_render_info->geo_loaded & gfx_state.currentRendererFlag))
		return;
	
	gfxGeoIncrementUsedCount(model_desc->model_lod->geo_render_info, 1, false);

	if (geo_draw = rdrDrawListAllocGeo(draw_list, RTYPE_MODEL, model_desc->model_lod, model_desc->model_lod->geo_render_info->subobject_count, 0, 0))
	{
		RdrAddInstanceParams instance_params={0};
		Vec4 instance_param; // Not needed
		geo_draw->geo_handle_primary = model_desc->geo_handle;

		scaleMat3(unitmat, instance_params.instance.world_matrix, light->rdr_light.point_spot_params.outer_radius);
		copyVec3(light->rdr_light.world_mat[3], instance_params.instance.world_matrix[3]);

		instance_params.distance_offset = model_desc->model->radius;
		copyVec3(instance_params.instance.world_matrix[3], instance_params.instance.world_mid);

		copyVec3(light->rdr_light.light_colors[RLCT_WORLD].diffuse, instance_params.instance.color);
		instance_params.instance.color[3] = 0.25;

		setVec3same(instance_params.ambient_multiplier, 1);

		RDRALLOC_SUBOBJECTS(draw_list, instance_params, model_desc->model_lod, i);
		for (i = 0; i < geo_draw->subobject_count; ++i)
			gfxDemandLoadMaterialAtQueueTime(instance_params.subobjects[i], model_desc->model_lod->materials[i], NULL, NULL, NULL, NULL, instance_param, 0.0f, TEX_DEMAND_LOAD_DEFAULT_UV_DENSITY);

		rdrDrawListAddGeoInstance(draw_list, geo_draw, &instance_params, RST_AUTO, ROC_RENDERER, true);
	}
}

static void gfxDoDeferredSpotLight(GfxLight *light, const Mat4 view_mat, TexHandle depthBufferTexHandle)
{
	Vec3 light_vs;
	F32 cone_angle;
	RdrShapePostProcess ppshape = {0};
	TexHandle textures[SBUF_MAX];
	RdrSurface *source_surface = gfx_state.currentAction->bufferMRT->surface;
	LightModelDesc *model_desc;
	const RdrLightDefinition *light_def = NULL;

	cone_angle = 2 * DEG(light->rdr_light.point_spot_params.outer_cone_angle);
	model_desc = getModelDescForConeAngle(cone_angle, cone_angle, false);
	ppshape.geometry = model_desc->geo_handle;

	if (!ppshape.geometry || light->rdr_light.point_spot_params.outer_radius <= 0 || !model_desc->model_lod->geo_render_info || !(model_desc->model_lod->geo_render_info->geo_loaded & gfx_state.currentRendererFlag))
		return;

	gfxGeoIncrementUsedCount(model_desc->model_lod->geo_render_info, 1, false);

	textures[0] = rdrSurfaceToTexHandle(source_surface, SBUF_0);
	textures[1] = rdrSurfaceToTexHandle(source_surface, SBUF_1);
	textures[2] = rdrSurfaceToTexHandle(source_surface, SBUF_2);
	textures[3] = rdrSurfaceToTexHandle(source_surface, SBUF_3);
	textures[4] = depthBufferTexHandle;
	ppshape.material.tex_count = depthBufferTexHandle ? ARRAY_SIZE(textures) : ARRAY_SIZE(textures) - 1;
	ppshape.material.textures = textures;
	ppshape.shader_handle = gfxDemandLoadSpecialShaderEx(GSS_DEFERRED_LIGHT, 0, ARRAY_SIZE(textures), getRdrMaterialShader(0,rdrGetMaterialShaderType(light->rdr_light.light_type, 0)), &light_def);

	ppshape.tex_width = source_surface->width_nonthread;
	ppshape.tex_height = source_surface->height_nonthread;

	scaleMat3(light->rdr_light.world_mat, ppshape.world_matrix, light->rdr_light.point_spot_params.outer_radius);
	copyVec3(light->rdr_light.world_mat[3], ppshape.world_matrix[3]);

	ppshape.blend_type = RPPBLEND_ADD;

	if (light_def)
	{
		U16 const_count = 0, tex_count = 0;
		rdrLightAddConstAndTexCounts(light_def, light->rdr_light.light_type, &const_count, &tex_count, NULL, NULL, false);
		ppshape.material.constants = _alloca(const_count * sizeof(Vec4));
		if (rdrIsShadowedLightType(light->rdr_light.light_type))
			ppshape.material.const_count += rdrLightFillMaterialConstants(ppshape.material.constants + ppshape.material.const_count, light_def, &light->rdr_light, light->rdr_light.light_type, view_mat, RLDEFTYPE_SHADOW_TEST, RLCT_WORLD, false);
		ppshape.material.const_count += rdrLightFillMaterialConstants(ppshape.material.constants + ppshape.material.const_count, light_def, &light->rdr_light, light->rdr_light.light_type, view_mat, RLDEFTYPE_NORMAL, RLCT_WORLD, false);
		if (tex_count)
		{
			ppshape.material.textures = _alloca((tex_count + ARRAY_SIZE(textures)) * sizeof(TexHandle));
			CopyStructs(ppshape.material.textures, textures, ARRAY_SIZE(textures));
			if (rdrIsShadowedLightType(light->rdr_light.light_type))
				ppshape.material.tex_count += rdrLightFillMaterialTextures(ppshape.material.textures + ppshape.material.tex_count, light_def, &light->rdr_light, light->rdr_light.light_type, RLDEFTYPE_SHADOW_TEST, RLCT_WORLD, false);
			ppshape.material.tex_count += rdrLightFillMaterialTextures(ppshape.material.textures + ppshape.material.tex_count, light_def, &light->rdr_light, light->rdr_light.light_type, RLDEFTYPE_NORMAL, RLCT_WORLD, false);
		}
	}

	// if the light volume intersects the near plane, use the volume's back faces and don't depth test
	mulVecMat4(light->rdr_light.world_mat[3], view_mat, light_vs);
	if (-light_vs[2] < light->rdr_light.point_spot_params.outer_radius * 1.75f)
		ppshape.draw_back_faces = 1;
	else
		ppshape.depth_test_mode = RPPDEPTHTEST_LEQUAL;
	ppshape.no_offset = 1;

	rdrPostProcessShape(gfx_state.currentDevice->rdr_device, &ppshape);
}

void gfxDoDebugSpotLight(GfxLight *light, RdrDrawList *draw_list)
{
	LightModelDesc *model_desc;
	RdrDrawableGeo *geo_draw;
	F32 cone_angle;
	int i;

	cone_angle = 2 * DEG(light->rdr_light.point_spot_params.outer_cone_angle);
	model_desc = getModelDescForConeAngle(cone_angle, cone_angle, false);

	if (!model_desc->model || !model_desc->geo_handle || light->rdr_light.point_spot_params.outer_radius <= 0 || !model_desc->model_lod->geo_render_info || !(model_desc->model_lod->geo_render_info->geo_loaded & gfx_state.currentRendererFlag))
		return;

	gfxGeoIncrementUsedCount(model_desc->model_lod->geo_render_info, 1, false);

	if (geo_draw = rdrDrawListAllocGeo(draw_list, RTYPE_MODEL, model_desc->model_lod, model_desc->model_lod->geo_render_info->subobject_count, 0, 0))
	{
		RdrAddInstanceParams instance_params={0};
		Vec4 instance_param; // Not needed
		geo_draw->geo_handle_primary = model_desc->geo_handle;

		scaleMat3(unitmat, instance_params.instance.world_matrix, light->rdr_light.point_spot_params.outer_radius);
		copyVec3(light->rdr_light.world_mat[3], instance_params.instance.world_matrix[3]);

		instance_params.distance_offset = model_desc->model->radius;
		copyVec3(instance_params.instance.world_matrix[3], instance_params.instance.world_mid);

		copyVec3(light->rdr_light.light_colors[RLCT_WORLD].diffuse, instance_params.instance.color);
		instance_params.instance.color[3] = 0.25;

		setVec3same(instance_params.ambient_multiplier, 1);

		RDRALLOC_SUBOBJECTS(draw_list, instance_params, model_desc->model_lod, i);
		for (i = 0; i < geo_draw->subobject_count; ++i)
			gfxDemandLoadMaterialAtQueueTime(instance_params.subobjects[i], model_desc->model_lod->materials[i], NULL, NULL, NULL, NULL, instance_param, 0.0f, TEX_DEMAND_LOAD_DEFAULT_UV_DENSITY);

		rdrDrawListAddGeoInstance(draw_list, geo_draw, &instance_params, RST_AUTO, ROC_RENDERER, true);
	}
}

static void gfxDoDeferredProjectorLight(GfxLight *light, const Mat4 view_mat, TexHandle depthBufferTexHandle)
{
	assertmsg(0, "TODO(CD)");
	/*
	Vec3 light_vs;
	RdrShapePostProcess ppshape = {0};
	TexHandle textures[SBUF_MAX];
	RdrSurface *source_surface = gfx_state.currentAction->bufferMRT->surface;
	LightModelDesc *model_desc;
	const RdrLightDefinition *light_def = NULL;

	model_desc = getModelDescForConeAngle(2 * DEG(light->rdr_light.outer_cone_angle), 2 * DEG(light->rdr_light.outer_cone_angle2), true);
	ppshape.geometry = model_desc->geo_handle;

	if (!ppshape.geometry || light->rdr_light.outer_radius <= 0 || !model_desc->model_lod->geo_render_info || !(model_desc->model_lod->geo_render_info->geo_loaded & gfx_state.currentRendererFlag))
		return;

	gfxGeoIncrementUsedCount(model_desc->model_lod->geo_render_info, 1, false);

	textures[0] = rdrSurfaceToTexHandle(source_surface, SBUF_0);
	textures[1] = rdrSurfaceToTexHandle(source_surface, SBUF_1);
	textures[2] = rdrSurfaceToTexHandle(source_surface, SBUF_2);
	textures[3] = rdrSurfaceToTexHandle(source_surface, SBUF_3);
	textures[4] = depthBufferTexHandle;
	ppshape.material.tex_count = depthBufferTexHandle ? ARRAY_SIZE(textures) : ARRAY_SIZE(textures) - 1;
	ppshape.material.textures = textures;
	ppshape.shader_handle = gfxDemandLoadSpecialShaderEx(GSS_DEFERRED_LIGHT, 0, ARRAY_SIZE(textures), rdrGetMaterialShaderType(light->rdr_light.light_type, 0), &light_def);

	ppshape.tex_width = source_surface->width_nonthread;
	ppshape.tex_height = source_surface->height_nonthread;
	ppshape.tex_vwidth = source_surface->vwidth_nonthread;
	ppshape.tex_vheight = source_surface->vheight_nonthread;

	scaleMat3(light->rdr_light.world_mat, ppshape.world_matrix, light->rdr_light.outer_radius);
	copyVec3(light->rdr_light.world_mat[3], ppshape.world_matrix[3]);

	ppshape.blend_type = RPPBLEND_ADD;

	if (light_def)
	{
		U16 const_count = 0, tex_count = 0;
		rdrLightAddConstAndTexCounts(light_def, light->rdr_light.light_type, &const_count, &tex_count, NULL, NULL, false);
		ppshape.material.constants = _alloca(const_count * sizeof(Vec4));
		ppshape.material.const_count += rdrLightFillMaterialConstants(ppshape.material.constants + ppshape.material.const_count, light_def, &light->rdr_light, light->rdr_light.light_type, view_mat, RLDEFTYPE_NORMAL, RLCT_WORLD, false);
		if (rdrIsShadowedLightType(light->rdr_light.light_type))
			ppshape.material.const_count += rdrLightFillMaterialConstants(ppshape.material.constants + ppshape.material.const_count, light_def, &light->rdr_light, light->rdr_light.light_type, view_mat, RLDEFTYPE_SHADOW_TEST, RLCT_WORLD, false);
		if (tex_count)
		{
			ppshape.material.textures = _alloca((tex_count + ARRAY_SIZE(textures)) * sizeof(TexHandle));
			CopyStructs(ppshape.material.textures, textures, ARRAY_SIZE(textures));
			ppshape.material.tex_count += rdrLightFillMaterialTextures(ppshape.material.textures + ppshape.material.tex_count, light_def, &light->rdr_light, light->rdr_light.light_type, RLDEFTYPE_NORMAL, RLCT_WORLD, false);
			if (rdrIsShadowedLightType(light->rdr_light.light_type))
				ppshape.material.tex_count += rdrLightFillMaterialTextures(ppshape.material.textures + ppshape.material.tex_count, light_def, &light->rdr_light, light->rdr_light.light_type, RLDEFTYPE_SHADOW_TEST, RLCT_WORLD, false);
		}
	}

	// if the light volume intersects the near plane, use the volume's back faces and don't depth test
	mulVecMat4(light->rdr_light.world_mat[3], view_mat, light_vs);
	if (-light_vs[2] < light->rdr_light.outer_radius * 1.75f)
		ppshape.draw_back_faces = 1;
	else
		ppshape.depth_test_mode = RPPDEPTHTEST_LEQUAL;
	ppshape.no_offset = 1;

	rdrPostProcessShape(gfx_state.currentDevice->rdr_device, &ppshape);
	*/
}

void gfxDoDebugProjectorLight(GfxLight *light, RdrDrawList *draw_list)
{
	assertmsg(0, "TODO(CD)");
	/*
	LightModelDesc *model_desc;
	RdrDrawableGeo *geo_draw;
	int i;

	model_desc = getModelDescForConeAngle(2 * DEG(light->rdr_light.outer_cone_angle), 2 * DEG(light->rdr_light.outer_cone_angle2), true);

	if (!model_desc->model || !model_desc->geo_handle || light->rdr_light.outer_radius <= 0 || !model_desc->model_lod->geo_render_info || !(model_desc->model_lod->geo_render_info->geo_loaded & gfx_state.currentRendererFlag))
		return;

	gfxGeoIncrementUsedCount(model_desc->model_lod->geo_render_info, 1, false);

	if (geo_draw = rdrDrawListAllocGeo(draw_list, RTYPE_MODEL, model_desc->model_lod, model_desc->model_lod->geo_render_info->subobject_count, 0, 0))
	{
		RdrAddInstanceParams instance_params={0};
		Vec4 instance_param; // Not needed
		geo_draw->geo_handle_primary = model_desc->geo_handle;

		scaleMat3(unitmat, instance_params.instance.world_matrix, light->rdr_light.outer_radius);
		copyVec3(light->rdr_light.world_mat[3], instance_params.instance.world_matrix[3]);

		instance_params.distance_offset = model_desc->model->radius;
		copyVec3(instance_params.instance.world_matrix[3], instance_params.instance.world_mid);

		copyVec3(light->rdr_light.light_colors[RLCT_WORLD].diffuse, instance_params.instance.color);
		instance_params.instance.color[3] = 0.25;

		setVec3same(instance_params.ambient_multiplier, 1);

		RDRALLOC_SUBOBJECTS(draw_list, instance_params, model_desc->model_lod, i);
		for (i = 0; i < geo_draw->subobject_count; ++i)
			gfxDemandLoadMaterialAtQueueTime(instance_params.subobjects[i], model_desc->model_lod->materials[i], NULL, NULL, NULL, NULL, instance_param, 0.0f, TEX_DEMAND_LOAD_DEFAULT_UV_DENSITY);

		rdrDrawListAddGeoInstance(draw_list, geo_draw, &instance_params, RST_AUTO, ROC_RENDERER, true);
	}
	*/
}

static void gfxDoDeferredLight(GfxLight *light, const Mat4 view_mat, TexHandle depthBufferTexHandle)
{
	if (light->frame_visible != gfx_state.client_frame_timestamp)
		return;

	switch (rdrGetSimpleLightType(light->rdr_light.light_type))
	{
		xcase RDRLIGHT_DIRECTIONAL:
			gfxDoDeferredDirectionalLight(light, view_mat, depthBufferTexHandle);
		xcase RDRLIGHT_POINT:
			gfxDoDeferredPointLight(light, view_mat, depthBufferTexHandle);
		xcase RDRLIGHT_SPOT:
			gfxDoDeferredSpotLight(light, view_mat, depthBufferTexHandle);
		xcase RDRLIGHT_PROJECTOR:
			gfxDoDeferredProjectorLight(light, view_mat, depthBufferTexHandle);
		xdefault:
			assertmsg(0, "Unknown light type!");
	}
}

// Disable the unlit + first light pass (i.e. unlit + directional).
AUTO_CMD_INT(gfx_state.debug.bHideDeferredUnlit, debugHideDeferredUnlit) ACMD_HIDE ACMD_CATEGORY(DEBUG) ACMD_ACCESSLEVEL(0);
// Show only one light in the set of lights after the unlit + first light pass.
AUTO_CMD_INT(gfx_state.debug.bOneDeferredLight, debugOneDeferredLight) ACMD_HIDE ACMD_CATEGORY(DEBUG) ACMD_ACCESSLEVEL(0);

void gfxDoDeferredLighting(const Mat4 view_mat, TexHandle depthBufferTexHandle)
{
	GfxGlobalDrawParams *gdraw = SAFE_MEMBER_ADDR(gfx_state.currentAction, gdraw);
	int i;

	if (!gdraw)
		return;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	eaQSortG(gdraw->this_frame_lights, cmpLightsDeferred);

	gfx_state.debug.frame_counts.lights_drawn = eaSize(&gdraw->this_frame_lights);

	if (!gfx_state.debug.bHideDeferredUnlit)
		gfxDoDeferredUnlit((eaSize(&gdraw->this_frame_lights) > 0) ? gdraw->this_frame_lights[0] : NULL, view_mat, depthBufferTexHandle);

	if (gfx_state.debug.bOneDeferredLight)
	{
		for (i = 0; i < eaSize(&gdraw->this_frame_lights); ++i)
		{
			GfxLight * pLight = gdraw->this_frame_lights[i];
			if (rdrGetSimpleLightType(pLight->orig_light_type) == RDRLIGHT_POINT)
			{
				gfxDoDeferredLight(pLight, view_mat, depthBufferTexHandle);
				break;
			}
		}
	}
	else
	{
		for (i = eaSize(&gdraw->this_frame_lights)-1; i >= 1; --i)
			gfxDoDeferredLight(gdraw->this_frame_lights[i], view_mat, depthBufferTexHandle);
	}

	PERFINFO_AUTO_STOP();
}

