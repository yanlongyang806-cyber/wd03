#include "RdrLightAssembly.h"
#include "RenderLib.h"
#include "textparser.h"
#include "earray.h"
#include "systemspecs.h"
#include "RdrState.h"
#include "rdrEnums_h_ast.h"
#include "RdrLightAssembly_h_ast.c"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););

RdrUberLightParameters rdr_uber_params[UBER_TYPE_COUNT] = {0};


void rdrInitUberLightParams(void)
{
	rdr_uber_params[UBER_LIGHTING].chunk_const_count = SHADER_PER_LIGHT_CONST_COUNT;
	rdr_uber_params[UBER_LIGHTING].chunk_tex_count = 1;
	rdr_uber_params[UBER_LIGHTING].max_lights = MAX_NUM_SHADER_LIGHTS;
	rdr_uber_params[UBER_LIGHTING].max_textures_total = 3;
	rdr_uber_params[UBER_LIGHTING].shadow_test_const_count = 20;
	rdr_uber_params[UBER_LIGHTING].shadow_test_tex_count = 2;

	rdr_uber_params[UBER_SHADOWS].chunk_const_count = rdr_uber_params[UBER_LIGHTING].shadow_test_const_count;
	rdr_uber_params[UBER_SHADOWS].chunk_tex_count = rdr_uber_params[UBER_LIGHTING].shadow_test_tex_count;
	rdr_uber_params[UBER_SHADOWS].max_lights = MAX_NUM_SHADER_LIGHTS;
	rdr_uber_params[UBER_SHADOWS].max_textures_total = rdr_uber_params[UBER_SHADOWS].chunk_tex_count * rdr_uber_params[UBER_SHADOWS].max_lights;
	rdr_uber_params[UBER_SHADOWS].shadow_test_const_count = 0;
	rdr_uber_params[UBER_SHADOWS].shadow_test_tex_count = 0;

	rdr_uber_params[UBER_LIGHTING_2LIGHTS] = rdr_uber_params[UBER_LIGHTING];
	rdr_uber_params[UBER_LIGHTING_2LIGHTS].chunk_const_count = SHADER_PER_LIGHT_CONST_COUNT_SIMPLE;
	rdr_uber_params[UBER_LIGHTING_2LIGHTS].max_lights = 2;
	rdr_uber_params[UBER_LIGHTING_2LIGHTS].simple_lighting = true;

	rdr_uber_params[UBER_SHADOWS_2LIGHTS] = rdr_uber_params[UBER_SHADOWS];
	rdr_uber_params[UBER_SHADOWS_2LIGHTS].max_lights = 2;
	rdr_uber_params[UBER_SHADOWS_2LIGHTS].simple_lighting = true;
	rdr_uber_params[UBER_SHADOWS_2LIGHTS].max_textures_total = rdr_uber_params[UBER_SHADOWS_2LIGHTS].chunk_tex_count * rdr_uber_params[UBER_SHADOWS_2LIGHTS].max_lights;
}

static const struct {
	RdrLightParameter param;
	bool for_shadows;
	char *name;
	bool exclude_from_simple;
} name_mapping[] = {

	// vec3
	{LIGHTPARAM_position_vs,							false,	"light.pos_vs"},
	{LIGHTPARAM_direction_vs,							false,	"light.dir_vs"},
	{LIGHTPARAM_ambient_color,							false,	"light.ambient"},
	{LIGHTPARAM_diffuse_color,							false,	"light.diffuse"},
	{LIGHTPARAM_secondary_diffuse_color,				false,	"light.secondary_diffuse"},
	{LIGHTPARAM_specular_color,							false,	"light.specular", true}, // not used in simple lighting

	{LIGHTPARAM_shadow_color,							true,	"light.shadow_color", true},
	{LIGHTPARAM_shadow_mask,							true,	"light.shadow_mask"},

	{LIGHTPARAM_inv_world_view_x,						false,	"light.inv_world_view_x"},
	{LIGHTPARAM_inv_world_view_y,						false,	"light.inv_world_view_y"},
	{LIGHTPARAM_inv_world_view_z,						false,	"light.inv_world_view_z"},

	{LIGHTPARAM_direction_vs_scaled_one_thousand,		false,	"light.dir_vs_scaled_one_thousand"},
	{LIGHTPARAM_zero_vec,								false,	"zero_vec"},

	{LIGHTPARAM_BEGIN_SCALARS,							false,	"BEGIN_SCALARS_DUMMY"},

	// scalar
	{LIGHTPARAM_one_plus_inner_radius_over_dradius,		false,	"light.one_plus_inner_radius_over_dradius"},
	{LIGHTPARAM_neg_inv_dradius,						false,	"light.neg_inv_dradius"},

	{LIGHTPARAM_one_plus_inner_radius_over_dradius_scaled,	false,	"light.one_plus_inner_radius_over_dradius_scaled"},
	{LIGHTPARAM_neg_inv_dradius_scaled,					false,	"light.neg_inv_dradius_scaled"},

	{LIGHTPARAM_neg_cos_outer_angle_over_d_cos_angle,	false,	"light.neg_cos_outer_angle_over_d_cos_angle"},
	{LIGHTPARAM_inv_d_cos_angle,						false,	"light.inv_d_cos_angle"},

	{LIGHTPARAM_min_tan_x_over_delta,					false,	"light.min_tan_x_over_delta"},
	{LIGHTPARAM_neg_inv_delta_tan_x,					false,	"light.neg_inv_delta_tan_x"},

	{LIGHTPARAM_min_tan_y_over_delta,					false,	"light.min_tan_y_over_delta"},
	{LIGHTPARAM_neg_inv_delta_tan_y,					false,	"light.neg_inv_delta_tan_y"},

	{LIGHTPARAM_orig_light_type,						false,	"light.light_type", true},

	{LIGHTPARAM_min_shadow_val,							true,	"light.min_shadow_val"},
	{LIGHTPARAM_max_shadow_val,							true,	"light.max_shadow_val"},

	{LIGHTPARAM_shadowmap0_near_fade,					true,	"shadowmap0.near_fade"},
	{LIGHTPARAM_shadowmap0_far_fade,					true,	"shadowmap0.far_fade"},
	{LIGHTPARAM_shadowmap1_near_fade,					true,	"shadowmap1.near_fade"},
	{LIGHTPARAM_shadowmap1_far_fade,					true,	"shadowmap1.far_fade"},
	{LIGHTPARAM_shadowmap2_near_fade,					true,	"shadowmap2.near_fade"},
	{LIGHTPARAM_shadowmap2_far_fade,					true,	"shadowmap2.far_fade"},
	{LIGHTPARAM_clouds_multiplier0,						true,	"clouds.multiplier0"},
	{LIGHTPARAM_clouds_multiplier1,						true,	"clouds.multiplier1"},

	{LIGHTPARAM_one,									false,	"one"},
	{LIGHTPARAM_negative_one,							false,	"negative_one"},
	{LIGHTPARAM_zero,									false,	"zero"},
	{LIGHTPARAM_one_million,							false,	"one_million"},

	{LIGHTPARAM_BEGIN_4VECTORS,							false,	"BEGIN_4VECTORS_DUMMY"},

	// vec4
	{LIGHTPARAM_shadowmap_texture_size,					true,	"shadowmap.texture_size"},
	{LIGHTPARAM_shadowmap0_camera_to_shadowmap_x,		true,	"shadowmap0.camera_to_shadowmap_x"},
	{LIGHTPARAM_shadowmap0_camera_to_shadowmap_y,		true,	"shadowmap0.camera_to_shadowmap_y"},
	{LIGHTPARAM_shadowmap0_camera_to_shadowmap_z,		true,	"shadowmap0.camera_to_shadowmap_z"},
	{LIGHTPARAM_shadowmap0_camera_to_shadowmap_w,		true,	"shadowmap0.camera_to_shadowmap_w"},
	{LIGHTPARAM_shadowmap1_camera_to_shadowmap_x,		true,	"shadowmap1.camera_to_shadowmap_x"},
	{LIGHTPARAM_shadowmap1_camera_to_shadowmap_y,		true,	"shadowmap1.camera_to_shadowmap_y"},
	{LIGHTPARAM_shadowmap1_camera_to_shadowmap_z,		true,	"shadowmap1.camera_to_shadowmap_z"},
	{LIGHTPARAM_shadowmap1_camera_to_shadowmap_w,		true,	"shadowmap1.camera_to_shadowmap_w"},
	{LIGHTPARAM_shadowmap2_camera_to_shadowmap_x,		true,	"shadowmap2.camera_to_shadowmap_x"},
	{LIGHTPARAM_shadowmap2_camera_to_shadowmap_y,		true,	"shadowmap2.camera_to_shadowmap_y"},
	{LIGHTPARAM_shadowmap2_camera_to_shadowmap_z,		true,	"shadowmap2.camera_to_shadowmap_z"},
	{LIGHTPARAM_shadowmap2_camera_to_shadowmap_w,		true,	"shadowmap2.camera_to_shadowmap_w"},
	{LIGHTPARAM_clouds_camera_to_texture0_x,			true,	"clouds.camera_to_texture0_x"},
	{LIGHTPARAM_clouds_camera_to_texture0_y,			true,	"clouds.camera_to_texture0_y"},
	{LIGHTPARAM_clouds_camera_to_texture1_x,			true,	"clouds.camera_to_texture1_x"},
	{LIGHTPARAM_clouds_camera_to_texture1_y,			true,	"clouds.camera_to_texture1_y"},

	{LIGHTPARAM_BEGIN_TEXTURES,							false,	"BEGIN_TEXTURES_DUMMY"},

	// texture
	{LIGHTPARAM_projected_texture,						false,	"light.projected_texture"},
	{LIGHTPARAM_shadowmap_texture,						true,	"shadowmap.texture"},
	{LIGHTPARAM_clouds_texture,							true,	"clouds.texture"},
};

__forceinline static bool rdrLightParameterIsForShadows(RdrLightParameter param)
{
	assert(param < ARRAY_SIZE(name_mapping));
	return name_mapping[param].for_shadows;
}

bool rdrLightParameterExcludeFromSimple(RdrLightParameter param)
{
	assert(param < ARRAY_SIZE(name_mapping));
	return name_mapping[param].exclude_from_simple;
}

RdrLightParameter rdrLightParameterFromName(const char *name)
{
	int i;
	for (i=0; i<ARRAY_SIZE(name_mapping); i++) {
		if (stricmp(name, name_mapping[i].name)==0) {
			assert(name_mapping[i].param == i);
			return name_mapping[i].param;
		}
	}
//	assertmsgf(0, "Unknown light parameter name \"%s\"!", name);
	return -1;
}

RdrLightType rdrLightTypeFromName(const char *name)
{
	static const struct {
		RdrLightType type;
		const char *name;
	} type_mapping[] = {
		{RDRLIGHT_DIRECTIONAL, "directional_light"},
		{RDRLIGHT_POINT, "point_light"},
		{RDRLIGHT_SPOT, "spot_light"},
		{RDRLIGHT_PROJECTOR, "projector_light"},
	};
	int i;
	for (i=0; i<ARRAY_SIZE(type_mapping); i++) {
		if (stricmp(name, type_mapping[i].name)==0) {
			return type_mapping[i].type;
		}
	}
	return -1;
}

int rdrLightFillMaterialConstantsEx(Vec4 *constants, const RdrLightDefinition *light_definition, const RdrLight *light, RdrLightType dst_light_type, const Mat4 viewmat, RdrLightDefinitionType light_def_type, RdrLightColorType color_type, S16 *direction_idx, S16 *position_idx, S16 *color_idx, bool shadows_only)
{
	const RdrLightDefinitionData *light_def_data;
	Mat4 inv_world_view_mat;
	int i;
	RdrLightType orig_light_type;
	bool bSomethingElsePackedIntoProjector = false;

	if (!light_definition || !light)
		return 0;

	if (direction_idx)
		*direction_idx = -1;
	if (color_idx)
		*color_idx = -1;
	if (position_idx)
		*position_idx = -1;

	dst_light_type = rdrGetSimpleLightType(dst_light_type); // Nothing in here should care about shadows
	orig_light_type = rdrGetSimpleLightType(light->light_type);

	if (orig_light_type == RDRLIGHT_PROJECTOR)
	{
		Mat4 world_view_mat, light_world_mat_inv_y;
		copyMat4(light->world_mat, light_world_mat_inv_y);
		scaleVec3(light_world_mat_inv_y[1], -1, light_world_mat_inv_y[1]);
		mulMat4Inline(viewmat, light_world_mat_inv_y, world_view_mat);
		invertMat4Copy(world_view_mat, inv_world_view_mat);
	} else if (dst_light_type == RDRLIGHT_PROJECTOR) {
		bSomethingElsePackedIntoProjector = true;
	}
	
	light_def_data = rdrGetLightDefinitionData(light_definition, light_def_type);

	for (i = 0; i < light_def_data->num_params; ++i)
	{
		RdrLightDefinitionMap *param = &light_def_data->mappings[i];
		F32 *paramvec = &constants[param->relative_index][0];
		Vec4 tempvec;
		Vec4 tempvec2;
		F32 tempval;

		if (param->type > LIGHTPARAM_BEGIN_TEXTURES)
			continue;

		if (shadows_only && !rdrLightParameterIsForShadows(param->type))
			continue;

#define MAPVEC(v) ((param->swizzle[0]<4?(paramvec[param->swizzle[0]]=(v)[0]):0), (param->swizzle[1]<4?(paramvec[param->swizzle[1]]=(v)[1]):0), (param->swizzle[2]<4?(paramvec[param->swizzle[2]]=(v)[2]):0))
#define MAPVEC4(v) ((param->swizzle[0]<4?(paramvec[param->swizzle[0]]=(v)[0]):0), (param->swizzle[1]<4?(paramvec[param->swizzle[1]]=(v)[1]):0), (param->swizzle[2]<4?(paramvec[param->swizzle[2]]=(v)[2]):0), (param->swizzle[3]<4?(paramvec[param->swizzle[3]]=(v)[3]):0))
#define MAPVAL(v) ((param->swizzle[0]<4?(paramvec[param->swizzle[0]]=(v)):0))

		switch (param->type)
		{
			// vec3s:

			xcase LIGHTPARAM_position_vs:
				if (orig_light_type == RDRLIGHT_DIRECTIONAL)
				{
					scaleVec3(light->world_mat[1], -1000000, tempvec2);
					mulVecMat4(tempvec2, viewmat, tempvec);
				} else {
					mulVecMat4(light->world_mat[3], viewmat, tempvec);
				}
				MAPVEC(tempvec);
				if (position_idx && dst_light_type == RDRLIGHT_POINT)
					*position_idx = param->relative_index;

			xcase LIGHTPARAM_direction_vs:
				{
					Vec3 scaledvec;
					scaleVec3(light->world_mat[1], -1, scaledvec);
					mulVecMat3(scaledvec, viewmat, tempvec);
				}
				MAPVEC(tempvec);
				if (direction_idx)
					*direction_idx = param->relative_index;

			xcase LIGHTPARAM_ambient_color:
				if (orig_light_type == RDRLIGHT_DIRECTIONAL) // Directional packed into anything, secondary_diffuse into ambient
					scaleVec3(light->light_colors[color_type].secondary_diffuse, light->fade_out, tempvec);
				else
					scaleVec3(light->light_colors[color_type].ambient, light->fade_out, tempvec);
				MAPVEC(tempvec);

			xcase LIGHTPARAM_diffuse_color:	
				scaleVec3(light->light_colors[color_type].diffuse, light->fade_out, tempvec);
				MAPVEC(tempvec);
				if (color_idx)
					*color_idx = param->relative_index;

			xcase LIGHTPARAM_secondary_diffuse_color:
				scaleVec3(light->light_colors[color_type].secondary_diffuse, light->fade_out, tempvec);
				MAPVEC(tempvec);

			xcase LIGHTPARAM_specular_color:
				scaleVec3(light->light_colors[color_type].specular, light->fade_out, tempvec);
				MAPVEC(tempvec);

			xcase LIGHTPARAM_shadow_color:
				scaleVec3(light->light_colors[color_type].shadow_color, light->fade_out, tempvec);
				MAPVEC(tempvec);

			xcase LIGHTPARAM_shadow_mask:
				MAPVEC(light->shadow_mask);

			xcase LIGHTPARAM_inv_world_view_x:
				if (bSomethingElsePackedIntoProjector)
				{
					// Not actually a projector
					// packing in shadow color for [directional, point, spot] -> projector promotion
					scaleVec3(light->light_colors[color_type].shadow_color, light->fade_out, tempvec);
				} else {
					getMatRow(inv_world_view_mat, 0, tempvec);
				}
				MAPVEC(tempvec);

			xcase LIGHTPARAM_inv_world_view_y:
				if (bSomethingElsePackedIntoProjector)
				{
					// Not actually a projector
					// packing in lightdir_vs for spot -> projector promotion
					// point and dir lights basically ignore this value (falloff is forced to 1.0)
					Vec3 scaledvec;
					scaleVec3(light->world_mat[1], -1, scaledvec);
					mulVecMat3(scaledvec, viewmat, tempvec);
				} else {
					getMatRow(inv_world_view_mat, 1, tempvec);
				}
				MAPVEC(tempvec);

			xcase LIGHTPARAM_inv_world_view_z:
				if (bSomethingElsePackedIntoProjector)
				{
					// Not actually a projector
					// packing in ambient color for [directional, point, spot] -> projector promotion
					// (actually secondary_diffuse for directional)
					if (orig_light_type == RDRLIGHT_DIRECTIONAL)
						scaleVec3(light->light_colors[color_type].secondary_diffuse, light->fade_out, tempvec);
					else
						scaleVec3(light->light_colors[color_type].ambient, light->fade_out, tempvec);
				} else {
					getMatRow(inv_world_view_mat, 2, tempvec);
				}
				MAPVEC(tempvec);

			xcase LIGHTPARAM_direction_vs_scaled_one_thousand:
				{
					Vec3 scaledvec;
					scaleVec3(light->world_mat[1], -1000, scaledvec);
					mulVecMat3(scaledvec, viewmat, tempvec);
				}
				MAPVEC(tempvec);

			xcase LIGHTPARAM_zero_vec:
				MAPVEC(zerovec3);


			// scalars:

			xcase LIGHTPARAM_one_plus_inner_radius_over_dradius: // 1 + inner_radius / (outer_radius - inner_radius)
																 // (used by calcDistanceFalloff)
				if (orig_light_type == RDRLIGHT_DIRECTIONAL)
					tempval = 0;
				else
					tempval = 1 + light->point_spot_params.inner_radius / (light->point_spot_params.outer_radius - light->point_spot_params.inner_radius);
				MAPVAL(tempval);

			xcase LIGHTPARAM_neg_inv_dradius: // -1 / (outer_radius - inner_radius)
				if (orig_light_type == RDRLIGHT_DIRECTIONAL)
					tempval = 1.f;
				else
					tempval = -1.f / (light->point_spot_params.outer_radius - light->point_spot_params.inner_radius);
				MAPVAL(tempval);

			xcase LIGHTPARAM_one_plus_inner_radius_over_dradius_scaled:
				{
					// Only used in vertex lights getting projector light packed in
					// need to scale the radius up because this is emulating a projector light's falloff
					F32 inner_radius = light->point_spot_params.inner_radius / light->point_spot_params.angular_falloff[0];
					F32 outer_radius = light->point_spot_params.outer_radius / light->point_spot_params.angular_falloff[1];
					tempval = 1 + inner_radius / (outer_radius - inner_radius);
					MAPVAL(tempval);
				}

			xcase LIGHTPARAM_neg_inv_dradius_scaled:
				{
					// Only used in vertex lights getting projector light packed in
					// need to scale the radius up because this is emulating a projector light's falloff
					F32 inner_radius = light->point_spot_params.inner_radius / light->point_spot_params.angular_falloff[0];
					F32 outer_radius = light->point_spot_params.outer_radius / light->point_spot_params.angular_falloff[1];
					tempval = -1.f / (outer_radius - inner_radius);
					MAPVAL(tempval);
				}

			xcase LIGHTPARAM_neg_cos_outer_angle_over_d_cos_angle: // -cos(outer)/(cos(inner) - cos(outer))
				if (orig_light_type == RDRLIGHT_DIRECTIONAL || orig_light_type == RDRLIGHT_POINT)
					tempval = 1000000.f; // Packing into spot
				else
					tempval = -light->point_spot_params.angular_falloff[1] / (light->point_spot_params.angular_falloff[0] - light->point_spot_params.angular_falloff[1]);
				MAPVAL(tempval);
				
			xcase LIGHTPARAM_inv_d_cos_angle: // 1/(cos(inner) - cos(outer))
				if (orig_light_type == RDRLIGHT_DIRECTIONAL || orig_light_type == RDRLIGHT_POINT)
					tempval = -1000000.f; // Packing into spot
				else
					tempval = 1 / (light->point_spot_params.angular_falloff[0] - light->point_spot_params.angular_falloff[1]);
				MAPVAL(tempval);

			xcase LIGHTPARAM_min_tan_x_over_delta:
				if (bSomethingElsePackedIntoProjector)
				{
					// Not actually a projector
					if (orig_light_type == RDRLIGHT_SPOT)
					{
						// for spot -> projector 
						MAPVAL(-light->point_spot_params.angular_falloff[1] / (light->point_spot_params.angular_falloff[0] - light->point_spot_params.angular_falloff[1]));
					} else {
						// Point, Directional
						MAPVAL(1000000.f);
					}
				} else {
					MAPVAL(light->projector_params.angular_falloff[1] / (light->projector_params.angular_falloff[1] - light->projector_params.angular_falloff[0]));
				}

			xcase LIGHTPARAM_neg_inv_delta_tan_x:
				if (bSomethingElsePackedIntoProjector)
				{
					// Not actually a projector
					if (orig_light_type == RDRLIGHT_SPOT)
					{
						// for spot -> projector 
						MAPVAL(1 / (light->point_spot_params.angular_falloff[0] - light->point_spot_params.angular_falloff[1]));
					} else {
						MAPVAL(1000000.f);
					}
				} else {
					MAPVAL(-1.f / (light->projector_params.angular_falloff[1] - light->projector_params.angular_falloff[0]));
				}

			xcase LIGHTPARAM_min_tan_y_over_delta:
				if (bSomethingElsePackedIntoProjector)
				{
					MAPVAL(1000000.f);
				} else {
					MAPVAL(light->projector_params.angular_falloff[3] / (light->projector_params.angular_falloff[3] - light->projector_params.angular_falloff[2]));
				}

			xcase LIGHTPARAM_neg_inv_delta_tan_y:
				if (bSomethingElsePackedIntoProjector)
				{
					MAPVAL(1000000.f);
				} else {
					MAPVAL(-1.f / (light->projector_params.angular_falloff[3] - light->projector_params.angular_falloff[2]));
				}

			xcase LIGHTPARAM_orig_light_type:
				MAPVAL(orig_light_type-1);

			xcase LIGHTPARAM_min_shadow_val:
				MAPVAL(1 - light->light_colors[color_type].min_shadow_val);

			xcase LIGHTPARAM_max_shadow_val:
				MAPVAL(1 - light->light_colors[color_type].max_shadow_val);

			xcase LIGHTPARAM_shadowmap0_near_fade:
				MAPVAL(light->shadowmap[0].near_fade);

			xcase LIGHTPARAM_shadowmap0_far_fade:
				if (orig_light_type == RDRLIGHT_DIRECTIONAL && dst_light_type != RDRLIGHT_DIRECTIONAL)
					MAPVAL(light->shadowmap[2].far_fade);
				else
					MAPVAL(light->shadowmap[0].far_fade);

			xcase LIGHTPARAM_shadowmap1_near_fade:
				MAPVAL(light->shadowmap[1].near_fade);

			xcase LIGHTPARAM_shadowmap1_far_fade:
				MAPVAL(light->shadowmap[1].far_fade);

			xcase LIGHTPARAM_shadowmap2_near_fade:
				MAPVAL(light->shadowmap[2].near_fade);

			xcase LIGHTPARAM_shadowmap2_far_fade:
				MAPVAL(light->shadowmap[2].far_fade);

			xcase LIGHTPARAM_clouds_multiplier0:
				MAPVAL(light->cloud_layers[0].multiplier);

			xcase LIGHTPARAM_clouds_multiplier1:
				MAPVAL(light->cloud_layers[1].multiplier);

			xcase LIGHTPARAM_one:
				MAPVAL(1.f);

			xcase LIGHTPARAM_negative_one:
				MAPVAL(-1.f);

			xcase LIGHTPARAM_zero:
				MAPVAL(0.f);

			xcase LIGHTPARAM_one_million:
				MAPVAL(1000000.f);


			// vec4s:

			xcase LIGHTPARAM_shadowmap_texture_size:
				setVec4(tempvec, light->shadowmap_texture_size[0], light->shadowmap_texture_size[1], 1.f / AVOID_DIV_0(light->shadowmap_texture_size[0]), 1.f / AVOID_DIV_0(light->shadowmap_texture_size[1]));
				MAPVEC4(tempvec);

			xcase LIGHTPARAM_shadowmap0_camera_to_shadowmap_x:
				if (orig_light_type == RDRLIGHT_DIRECTIONAL && dst_light_type != RDRLIGHT_DIRECTIONAL)
					getMatRow(light->shadowmap[2].camera_to_shadowmap, 0, tempvec);
				else
					getMatRow(light->shadowmap[0].camera_to_shadowmap, 0, tempvec);
				MAPVEC4(tempvec);

			xcase LIGHTPARAM_shadowmap0_camera_to_shadowmap_y:
				if (orig_light_type == RDRLIGHT_DIRECTIONAL && dst_light_type != RDRLIGHT_DIRECTIONAL)
					getMatRow(light->shadowmap[2].camera_to_shadowmap, 1, tempvec);
				else
					getMatRow(light->shadowmap[0].camera_to_shadowmap, 1, tempvec);
				MAPVEC4(tempvec);

			xcase LIGHTPARAM_shadowmap0_camera_to_shadowmap_z:
				if (orig_light_type == RDRLIGHT_DIRECTIONAL && dst_light_type != RDRLIGHT_DIRECTIONAL)
					getMatRow(light->shadowmap[2].camera_to_shadowmap, 2, tempvec);
				else
					getMatRow(light->shadowmap[0].camera_to_shadowmap, 2, tempvec);
				MAPVEC4(tempvec);

			xcase LIGHTPARAM_shadowmap0_camera_to_shadowmap_w:
				if (orig_light_type == RDRLIGHT_DIRECTIONAL && dst_light_type != RDRLIGHT_DIRECTIONAL)
					getMatRow(light->shadowmap[2].camera_to_shadowmap, 3, tempvec);
				else
					getMatRow(light->shadowmap[0].camera_to_shadowmap, 3, tempvec);
				MAPVEC4(tempvec);

			xcase LIGHTPARAM_shadowmap1_camera_to_shadowmap_x:
				getMatRow(light->shadowmap[1].camera_to_shadowmap, 0, tempvec);
				MAPVEC4(tempvec);

			xcase LIGHTPARAM_shadowmap1_camera_to_shadowmap_y:
				getMatRow(light->shadowmap[1].camera_to_shadowmap, 1, tempvec);
				MAPVEC4(tempvec);

			xcase LIGHTPARAM_shadowmap1_camera_to_shadowmap_z:
				getMatRow(light->shadowmap[1].camera_to_shadowmap, 2, tempvec);
				MAPVEC4(tempvec);

			xcase LIGHTPARAM_shadowmap1_camera_to_shadowmap_w:
				getMatRow(light->shadowmap[1].camera_to_shadowmap, 3, tempvec);
				MAPVEC4(tempvec);

			xcase LIGHTPARAM_shadowmap2_camera_to_shadowmap_x:
				getMatRow(light->shadowmap[2].camera_to_shadowmap, 0, tempvec);
				MAPVEC4(tempvec);

			xcase LIGHTPARAM_shadowmap2_camera_to_shadowmap_y:
				getMatRow(light->shadowmap[2].camera_to_shadowmap, 1, tempvec);
				MAPVEC4(tempvec);

			xcase LIGHTPARAM_shadowmap2_camera_to_shadowmap_z:
				getMatRow(light->shadowmap[2].camera_to_shadowmap, 2, tempvec);
				MAPVEC4(tempvec);

			xcase LIGHTPARAM_shadowmap2_camera_to_shadowmap_w:
				getMatRow(light->shadowmap[2].camera_to_shadowmap, 3, tempvec);
				MAPVEC4(tempvec);

			xcase LIGHTPARAM_clouds_camera_to_texture0_x:
				getMatRow(light->cloud_layers[0].camera_to_cloud_texture, 0, tempvec);
				MAPVEC4(tempvec);

			xcase LIGHTPARAM_clouds_camera_to_texture0_y:
				getMatRow(light->cloud_layers[0].camera_to_cloud_texture, 1, tempvec);
				MAPVEC4(tempvec);

			xcase LIGHTPARAM_clouds_camera_to_texture1_x:
				getMatRow(light->cloud_layers[1].camera_to_cloud_texture, 0, tempvec);
				MAPVEC4(tempvec);

			xcase LIGHTPARAM_clouds_camera_to_texture1_y:
				getMatRow(light->cloud_layers[1].camera_to_cloud_texture, 1, tempvec);
				MAPVEC4(tempvec);


			// unknown:

			xdefault:
				assertmsg(0, "Unknown light parameter type!");
		}

#undef MAPVEC
#undef MAPVEC4
#undef MAPVAL

	}

	return light_def_data->num_vectors;
}

int rdrLightFillMaterialTextures(TexHandle *textures, const RdrLightDefinition *light_definition, const RdrLight *light, RdrLightType dst_light_type, RdrLightDefinitionType light_def_type, RdrLightColorType color_type, bool shadows_only)
{
	const RdrLightDefinitionData *light_def_data;
	int i;

	if (!light_definition || !light)
		return 0;

	light_def_data = rdrGetLightDefinitionData(light_definition, light_def_type);

	for (i = 0; i < light_def_data->num_params; ++i)
	{
		RdrLightDefinitionMap *param = &light_def_data->mappings[i];

		if (param->type < LIGHTPARAM_BEGIN_TEXTURES)
			continue;

		if (shadows_only && !rdrLightParameterIsForShadows(param->type))
			continue;

		// Default to not-comparison test.
		rdrAddRemoveTexHandleFlags(
			&(textures[param->relative_index]),
			0,
			RTF_COMPARISON_LESS_EQUAL);

		switch (param->type)
		{
			xcase LIGHTPARAM_projected_texture:
				if (!(light->light_type & RDRLIGHT_PROJECTOR)) // Something else packed into a projector light
					textures[param->relative_index] = rdr_state.white_tex_handle;
				else
					textures[param->relative_index] = light->projector_params.projected_tex;

			xcase LIGHTPARAM_shadowmap_texture:
				textures[param->relative_index] = light->shadowmap_texture;

				// Switch on comparison sampling for this.
				rdrAddRemoveTexHandleFlags(
					&(textures[param->relative_index]),
					RTF_COMPARISON_LESS_EQUAL,
					0);

			xcase LIGHTPARAM_clouds_texture:
				textures[param->relative_index] = light->cloud_texture;

			xdefault:
				assertmsg(0, "Unknown light parameter type!");
		}
	}

	return light_def_data->num_textures;
}

void rdrLightAddConstAndTexCounts(const RdrLightDefinition *light_definition, RdrLightType dst_light_type, U16 *normal_const_count, U16 *normal_tex_count, U16 *shadowed_const_count, U16 *shadowed_tex_count, bool simple_lighting)
{
	const RdrLightDefinitionData *light_def_data;

	if (!light_definition || !dst_light_type)
		return;

	light_def_data = rdrGetLightDefinitionData(light_definition, simple_lighting ? RLDEFTYPE_SIMPLE : RLDEFTYPE_NORMAL);
	*normal_const_count += light_def_data->num_vectors;
	*normal_tex_count += light_def_data->num_textures;

	if (rdrIsShadowedLightType(dst_light_type))
	{
		light_def_data = rdrGetLightDefinitionData(light_definition, RLDEFTYPE_SHADOW_TEST);
		
		if (shadowed_const_count)
			*shadowed_const_count += light_def_data->num_vectors;
		else
			*normal_const_count += light_def_data->num_vectors;

		if (shadowed_tex_count)
			*shadowed_tex_count += light_def_data->num_textures;
		else
			*normal_tex_count += light_def_data->num_textures;
	}
}
