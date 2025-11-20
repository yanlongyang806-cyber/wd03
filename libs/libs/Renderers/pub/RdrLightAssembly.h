#pragma once
GCC_SYSTEM

#include "RdrEnums.h"

typedef struct RdrLight RdrLight;
typedef U64 TexHandle;

typedef enum RdrLightParameter
{
	LIGHTPARAM_position_vs,
	LIGHTPARAM_direction_vs,
	LIGHTPARAM_ambient_color,
	LIGHTPARAM_diffuse_color,	
	LIGHTPARAM_secondary_diffuse_color,
	LIGHTPARAM_specular_color,

	LIGHTPARAM_shadow_color,
	LIGHTPARAM_shadow_mask,

	LIGHTPARAM_inv_world_view_x,
	LIGHTPARAM_inv_world_view_y,
	LIGHTPARAM_inv_world_view_z,

	LIGHTPARAM_direction_vs_scaled_one_thousand,
	LIGHTPARAM_zero_vec,

	LIGHTPARAM_BEGIN_SCALARS, // All scalars must be after this, 3 component vectors before

	LIGHTPARAM_one_plus_inner_radius_over_dradius,
	LIGHTPARAM_neg_inv_dradius, // -1 / (outer_radius - inner_radius)

	LIGHTPARAM_one_plus_inner_radius_over_dradius_scaled,
	LIGHTPARAM_neg_inv_dradius_scaled,

	LIGHTPARAM_neg_cos_outer_angle_over_d_cos_angle,
	LIGHTPARAM_inv_d_cos_angle,

	LIGHTPARAM_min_tan_x_over_delta,
	LIGHTPARAM_neg_inv_delta_tan_x,

	LIGHTPARAM_min_tan_y_over_delta,
	LIGHTPARAM_neg_inv_delta_tan_y,

	LIGHTPARAM_orig_light_type, // original light type (for light_combos)

	LIGHTPARAM_min_shadow_val,
	LIGHTPARAM_max_shadow_val,

	LIGHTPARAM_shadowmap0_near_fade,
	LIGHTPARAM_shadowmap0_far_fade,
	LIGHTPARAM_shadowmap1_near_fade,
	LIGHTPARAM_shadowmap1_far_fade,
	LIGHTPARAM_shadowmap2_near_fade,
	LIGHTPARAM_shadowmap2_far_fade,
	LIGHTPARAM_clouds_multiplier0,
	LIGHTPARAM_clouds_multiplier1,

	LIGHTPARAM_one,
	LIGHTPARAM_negative_one,
	LIGHTPARAM_zero,
	LIGHTPARAM_one_million,

	LIGHTPARAM_BEGIN_4VECTORS, // All 4 component vectors must be after this, scalars before

	LIGHTPARAM_shadowmap_texture_size,
	LIGHTPARAM_shadowmap0_camera_to_shadowmap_x,
	LIGHTPARAM_shadowmap0_camera_to_shadowmap_y,
	LIGHTPARAM_shadowmap0_camera_to_shadowmap_z,
	LIGHTPARAM_shadowmap0_camera_to_shadowmap_w,
	LIGHTPARAM_shadowmap1_camera_to_shadowmap_x,
	LIGHTPARAM_shadowmap1_camera_to_shadowmap_y,
	LIGHTPARAM_shadowmap1_camera_to_shadowmap_z,
	LIGHTPARAM_shadowmap1_camera_to_shadowmap_w,
	LIGHTPARAM_shadowmap2_camera_to_shadowmap_x,
	LIGHTPARAM_shadowmap2_camera_to_shadowmap_y,
	LIGHTPARAM_shadowmap2_camera_to_shadowmap_z,
	LIGHTPARAM_shadowmap2_camera_to_shadowmap_w,
	LIGHTPARAM_clouds_camera_to_texture0_x,
	LIGHTPARAM_clouds_camera_to_texture0_y,
	LIGHTPARAM_clouds_camera_to_texture1_x,
	LIGHTPARAM_clouds_camera_to_texture1_y,

	LIGHTPARAM_BEGIN_TEXTURES, // All texture must be after this, 4 component vectors before

	LIGHTPARAM_projected_texture,
	LIGHTPARAM_shadowmap_texture,
	LIGHTPARAM_clouds_texture,

} RdrLightParameter;

typedef enum RdrLightDefinitionType
{
	RLDEFTYPE_NORMAL,
	RLDEFTYPE_SIMPLE,
	RLDEFTYPE_SHADOW_TEST,
	RLDEFTYPE_VERTEX_LIGHTING,
	RLDEFTYPE_SINGLE_DIR_LIGHT,

	RLDEFTYPE_COUNT,
} RdrLightDefinitionType;

typedef enum RdrUberLightType
{
	UBER_LIGHTING,
	UBER_SHADOWS,
	UBER_LIGHTING_2LIGHTS,
	UBER_SHADOWS_2LIGHTS,

	UBER_TYPE_COUNT,
} RdrUberLightType;

typedef struct RdrLightDefinitionMap
{
	RdrLightParameter type;
	int relative_index;
	char swizzle[4]; // Let 4 -> ' ', One of {0, 1, 2, 3} (xyzw), {0, 1, 2, 4} (xyz ), {0-3, 4, 4, 4} (e.g. "w   ")
} RdrLightDefinitionMap;

typedef struct RdrLightDefinitionData
{
	char *text;								// pointer to the text in RdrLightDefinition, for convenience
	int text_length;						// After processing

	int num_params;
	RdrLightDefinitionMap *mappings;		// shared mapping table for vectors and textures

	int num_vectors;						// How many vectors are needed to pack these parameters
	int num_textures;						// Do we need a mapping table for the textures, too?
} RdrLightDefinitionData;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("\n") AST_STRIP_UNDERSCORES;
typedef struct RdrLightDefinitionParamConstant
{
	char **param_names;		AST( STRUCTPARAM )
} RdrLightDefinitionParamConstant;

AUTO_STRUCT AST_STRIP_UNDERSCORES;
typedef struct RdrLightDefinitionParams
{
	RdrLightDefinitionParamConstant **params;		AST( NAME(Param) )
} RdrLightDefinitionParams;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndLight) AST_STRIP_UNDERSCORES;
typedef struct RdrLightDefinition
{
	char *device_id;
	const char *filename; AST(CURRENTFILE)
	
	char *normal_text;
	char *simple_text; NO_AST
	char *shadow_test_text;
	RdrLightType light_type;

	RdrLightDefinitionParams vertex_light_params;
	RdrLightDefinitionParams single_dir_light_params;

	RdrLightDefinitionData definitions[RLDEFTYPE_COUNT]; NO_AST
} RdrLightDefinition;

typedef struct RdrUberLightParameters
{
	int chunk_const_count;
	int chunk_tex_count;
	int max_lights;
	int max_textures_total;
	int shadow_test_const_count;
	int shadow_test_tex_count;
	bool simple_lighting;
} RdrUberLightParameters;

extern RdrUberLightParameters rdr_uber_params[UBER_TYPE_COUNT];

void rdrInitUberLightParams(void);

RdrLightType rdrLightTypeFromName(const char *name);
RdrLightParameter rdrLightParameterFromName(const char *name);
bool rdrLightParameterExcludeFromSimple(RdrLightParameter param);
int rdrLightFillMaterialConstantsEx(Vec4 *constants, const RdrLightDefinition *light_definition, const RdrLight *light, RdrLightType dst_light_type, const Mat4 viewmat, RdrLightDefinitionType light_def_type, RdrLightColorType color_type, S16 *direction_idx, S16 *position_idx, S16 *color_idx, bool shadows_only);
__forceinline static int rdrLightFillMaterialConstants(Vec4 *constants, const RdrLightDefinition *light_definition, const RdrLight *light, RdrLightType dst_light_type, const Mat4 viewmat, RdrLightDefinitionType light_def_type, RdrLightColorType color_type, bool shadows_only)
{
	return rdrLightFillMaterialConstantsEx(constants, light_definition, light, dst_light_type, viewmat, light_def_type, color_type, NULL, NULL, NULL, shadows_only);
}

int rdrLightFillMaterialTextures(TexHandle *textures, const RdrLightDefinition *light_definition, const RdrLight *light, RdrLightType dst_light_type, RdrLightDefinitionType light_def_type, RdrLightColorType color_type, bool shadows_only);
void rdrLightAddConstAndTexCounts(const RdrLightDefinition *light_definition, RdrLightType dst_light_type, U16 *normal_const_count, U16 *normal_tex_count, U16 *shadowed_const_count, U16 *shadowed_tex_count, bool simple_lighting);

__forceinline static const RdrLightDefinitionData *rdrGetLightDefinitionData(const RdrLightDefinition *light_definition, RdrLightDefinitionType type)
{
	return SAFE_MEMBER_ADDR(light_definition, definitions[type]);
}


#define LIGHTDEF_VARIABLE_LENGTH (ARRAY_SIZE("$ightParam00.xyzw") - 1)
#define LIGHTTEX_VARIABLE_LENGTH (ARRAY_SIZE("$ightSampler00") - 1)

// Sets the maximum number of constants a light type can be used and still fit in one light slot, otherwise uses 2 slots
// larger lights *must* be first
#define UBERLIGHT_BITS_PER_LIGHT 3

