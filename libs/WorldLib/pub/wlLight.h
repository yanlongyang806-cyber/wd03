#ifndef _WLLIGHT_H_
#define _WLLIGHT_H_
GCC_SYSTEM

#include "WorldLibEnums.h"

C_DECLARATIONS_BEGIN

//AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

typedef struct GfxLight GfxLight;
typedef struct GroupDef GroupDef;
typedef struct GroupTracker GroupTracker;
typedef struct WorldRegion WorldRegion;
typedef struct WorldLightProperties WorldLightProperties;

AUTO_STRUCT AST_STRIP_UNDERSCORES;
typedef struct ShadowRules
{
	F32 time; // necessary for animation/interpolation in SkyInfo-specified values

	F32 camera_falloff_dist;
	bool camera_scale_falloff;

	F32 three_split_distances[3];
	F32 four_split_distances[4];
} ShadowRules;

extern ParseTable parse_ShadowRules[];
#define TYPE_parse_ShadowRules ShadowRules

AUTO_STRUCT;
typedef struct CamLightRules {
	bool enabled;
	bool keyOverride;
	F32 offset[3];
	Vec3 ambientHsv;						AST( NAME("AmbientHSV") FORMAT_HSV )
	Vec3 diffuseHsv;						AST( NAME("DiffuseHSV") FORMAT_HSV )
	Vec3 specularHsv;						AST( NAME("SpecularHSV") FORMAT_HSV )
	F32 innerRadius;
	F32 outerRadius;
} CamLightRules;

typedef enum LightCacheType
{
	LCT_WORLD,
	LCT_WORLD_EDITOR,
	LCT_WORLD_BINNING,
	LCT_TERRAIN,
	LCT_CHARACTER,
	LCT_INTERACTION_ENTITY,
} LightCacheType;

typedef enum LightCacheInvalidateType
{
	LCIT_STATIC_LIGHTS = 1<<0,
	LCIT_DYNAMIC_LIGHTS = 1<<1,
	LCIT_ROOMS = 1<<2,
	// if you add a type you must make the invalidated_bitfield bit field bigger in GfxLightCacheBase
	LCIT_ALL = (1<<3)-1,
} LightCacheInvalidateType;

AUTO_STRUCT;
typedef struct LightCloudLayer
{
	Vec2 scroll_rate;
	F32 scale;
	F32 multiplier;
} LightCloudLayer;

AUTO_STRUCT;
typedef struct LightData
{
	LightType light_type;
	LightAffectType light_affect_type;
	Mat4 world_mat;
	Vec3 ambient_hsv;						AST( FORMAT_HSV )
	Vec3 diffuse_hsv;						AST( FORMAT_HSV )
	Vec3 specular_hsv;						AST( FORMAT_HSV )
	Vec3 secondary_diffuse_hsv;				AST( FORMAT_HSV )
	Vec3 shadow_color_hsv;					NO_AST // AST( FORMAT_HSV )
	F32 inner_radius;
	F32 outer_radius;
	F32 inner_cone_angle;
	F32 outer_cone_angle;
	F32 inner_cone_angle2;
	F32 outer_cone_angle2;
	F32 min_shadow_val;
	F32 shadow_near_plane;
	F32 shadow_fade_val;					// value for shadow to fade to when going "cloudy" to hide shadow vector transitions
	F32 shadow_fade_time;					// time it takes for shadows to fade to cloudy when hiding shadow vector transitions
	F32 shadow_fade_dark_time;				// time for shadow to stay cloudy when hiding shadow vector transitions
	F32 shadow_pulse_amount;				// amount to pulse global shadow values towards cloudy
	F32 shadow_pulse_rate;					// rate at which to pulse global shadow values towards cloudy
	const char *texture_name;
	const char *cloud_texture_name;			AST( POOL_STRING )
	LightCloudLayer cloud_layers[2];		AST( AUTO_INDEX(cloud_layer) )
	WorldRegion *region;					NO_AST	// only for world lights
	GroupTracker *tracker;					NO_AST	// only in edit mode
	bool cast_shadows;
	bool dynamic;
	bool is_sun;
	bool key_light;
	bool infinite_shadows;
	F32 visual_lod_scale;					// Scales maximum visible distance of the light. Allow some lights to illuminate distance objects, such as important landmarks.
} LightData;

GroupTracker *groupGetLightPropertyBool(SA_PARAM_OP_VALID GroupTracker *tracker, GroupDef **def_chain, GroupDef *def, SA_PARAM_NN_STR const char *property_name, SA_PARAM_NN_VALID bool *val);
GroupTracker *groupGetLightPropertyFloat(SA_PARAM_OP_VALID GroupTracker *tracker, GroupDef **def_chain, GroupDef *def, SA_PARAM_NN_STR const char *property_name, SA_PARAM_NN_VALID F32 *val);
GroupTracker *groupGetLightPropertyVec3(SA_PARAM_OP_VALID GroupTracker *tracker, GroupDef **def_chain, GroupDef *def, SA_PARAM_NN_STR const char *property_name, SA_PRE_NN_ELEMS(3) SA_POST_OP_VALID Vec3 val);
GroupTracker *groupGetLightPropertyString(SA_PARAM_OP_VALID GroupTracker *tracker, GroupDef **def_chain, GroupDef *def, SA_PARAM_NN_STR const char *property_name, SA_PARAM_NN_VALID const char **val);
GroupTracker *groupGetLightPropertyInt(SA_PARAM_OP_VALID GroupTracker *tracker, GroupDef **def_chain, GroupDef *def, SA_PARAM_NN_STR const char *property_name, SA_PARAM_NN_VALID int *val);
const WorldLightProperties* groupGetDefaultLightProperties();

extern ParseTable parse_LightCloudLayer[];
#define TYPE_parse_LightCloudLayer LightCloudLayer
extern ParseTable parse_LightData[];
#define TYPE_parse_LightData LightData

C_DECLARATIONS_END

#endif //_WLLIGHT_H_
