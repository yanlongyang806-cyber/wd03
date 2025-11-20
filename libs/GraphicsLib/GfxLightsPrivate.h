/***************************************************************************



***************************************************************************/

#ifndef _GFXLIGHTSPRIVATE_H_
#define _GFXLIGHTSPRIVATE_H_
GCC_SYSTEM

#include "Frustum.h"
#include "Octree.h"
#include "RenderLib.h"
#include "ReferenceSystem.h"
#include "wlLight.h"
#include "GfxLightOptions.h"
#include "GraphicsLibPrivate.h"

typedef struct GfxTempSurface GfxTempSurface;
typedef struct RdrDrawList RdrDrawList;
typedef struct GfxOcclusionBuffer GfxOcclusionBuffer;
typedef struct BasicTexture BasicTexture;
typedef struct GroupTracker GroupTracker;
typedef struct GeoRenderInfo GeoRenderInfo;
typedef struct WorldRegion WorldRegion;
typedef struct WorldRegionGraphicsData WorldRegionGraphicsData;
typedef struct SparseGridEntry SparseGridEntry;
typedef struct WorldVolume WorldVolume;
typedef struct WorldAnimationEntry WorldAnimationEntry;
typedef struct Model Model;
typedef struct Room Room;
typedef struct ShadowSearchNode ShadowSearchNode;
typedef struct WorldGraphicsData WorldGraphicsData;
typedef struct WorldVolumeQueryCache WorldVolumeQueryCache;


typedef struct GfxShadowHullDebugData
{
	Vec3 region_min, region_max;
	Mat4 camera_matrix;
	F32 fovx, fovy;
	F32 fNear, fFar, fScale;
	Vec3 extrude;
	Mat4 light_view_matrix;
	int object_count;
} GfxShadowHullDebugData;

typedef struct GfxShadowMap
{
	Mat44 render_projection_matrix;
	Mat44 receiver_projection_matrix;
	int shadow_pass;

	// in light-space
	Vec3 vReceiverMin;
	Vec3 vReceiverMax;
	F32 fShadowMapRes; // eliminates swimming, wastes resolution

	GPolySet tempshadowcaster_hull, shadowcaster_hull;
	GPolySet shadowreceiver_hull;
	Frustum light_frustum;
	GfxOcclusionBuffer *occlusion_buffer;

	F32 near_fade, far_fade, depth_range;

	U32 use_unitviewrot:1;
	U32 use_dxoffset:1;
	U32 use_lightspace_transform:1;
	U32 use_texel_offset_only:1;
	U32 use_shadowmap_used_ratio_only:1;

	GfxShadowHullDebugData hull_debug[3];
} GfxShadowMap;

typedef struct GfxLightShadowsPerAction
{
	GfxTempSurface *shadow_surface;
	F32 shadow_quality; // 0-1 multiplier for the shadowmap size
	IVec2 used_surface_size;
	Vec2 used_surface_size_ratio;
	int shadowmap_size;
	TexHandle shadow_tex_handle;

	int shadowmap_count;
	GfxShadowMap *shadowmaps[MAX_LIGHT_SHADOW_PASSES];

	Frustum global_frustum;

	Vec2 cloud_scroll[MAX_CLOUD_LAYERS];	// tracks current scroll values

	U32 last_update_frame;
	int last_draw_count;
	int last_tri_count;

} GfxLightShadowsPerAction;

typedef struct GfxLightShadows
{
	Vec3 light_vec;					// towards light
	Mat4 lightspace_matrix;			// world->lightspace

} GfxLightShadows;

typedef struct GfxLightColors
{
	Vec3 hsv_ambient;
	Vec3 hsv_diffuse;
	Vec3 hsv_specular;
	Vec3 hsv_secondary_diffuse;
	Vec3 hsv_shadow_color;
	F32 min_shadow_val;
	F32 max_shadow_val;
	F32 shadow_fade_val;
} GfxLightColors;

typedef struct GfxLightCloudLayer
{
	Vec2 scroll_rate;
	F32 scale;
} GfxLightCloudLayer;

typedef struct GfxLightCacheBase GfxLightCacheBase;

#define TRACK_LIGHT_CACHE_REFS 1

typedef struct GfxLight
{
	OctreeEntry static_entry;
	SparseGridEntry *dynamic_entry;
	F32 deferred_sort_param;
	F32 nondeferred_sort_param;
	F32 debug_shadow_sort_param;
	int id;
	U32 frame_visible;
	U32 frame_modified;
	U32 shadowmap_last_updated;
	F32 orig_inner_radius, orig_outer_radius;
	F32 orig_inner_cone_angle, orig_outer_cone_angle;
	F32 orig_inner_cone_angle2, orig_outer_cone_angle2;
	LightAffectType light_affect_type;
	RdrLightType orig_light_type;
	RdrLight rdr_light;
	BasicTexture *texture;
	WorldAnimationEntry	*animation_controller;
	Mat4 controller_relative_matrix;
	Vec3 world_query_mid;
	Mat4 inv_world_mat; //this is only updated for projector lights
	Vec3 movingBoundsMin, movingBoundsMax;

	BasicTexture *cloud_texture;
	GfxLightCloudLayer cloud_layers[MAX_CLOUD_LAYERS];

	F32 shadow_fade_rate;
	F32 shadow_fade_dark_time;
	F32 shadow_pulse_amplitude;
	F32 shadow_pulse_rate;

	F32 shadow_near_plane;

	GfxLightColors light_colors;

	F32 shadow_fade;

	F32 shadow_pulse;
	F32 shadow_pulse_update_time;
	Vec2 shadow_pulse_pos;
	Vec2 shadow_pulse_dir;

	F32 shadow_transition;
	F32 shadow_transition_rate;

	GfxLightShadowsPerAction **shadow_data_per_action;
	GfxLightShadows shadow_data;

	GroupTracker *tracker;
	WorldRegion *region;
	WorldRegionGraphicsData *region_graphics_data;
	U32 dynamic:1;
	U32 key_light:1;
	U32 disabled:1;
	U32 occluded:1;
	U32 indoors:1;
	U32 is_sun:1;
	U32 key_override:1;
	U32 infinite_shadows:1;
	U32 room_assignment_valid : 1;

	struct 
	{
		U32	last_update_time;
		U8	occluded_bits;
	} occlusion_data;

	int use_counter;
	int last_uses;

	//The room doesn't actually own the light in terms of memory or anything,
	//just for light assignment
	Room* owner_room;

	ShadowSearchNode* shadow_search_node;

#if TRACK_LIGHT_CACHE_REFS
	int cache_ref_count;
#endif
} GfxLight;


void sortLights(GfxLight **lights, const Vec3 world_mid);
void updateRdrLightColors(GfxLight *light, WorldGraphicsData *world_data);
F32 calcProjectorRgn(GfxLight *light, const Vec3 world_mid);
F32 calcPointSpotRgn(GfxLight* light, const Vec3 world_mid, F32 d);
bool gfxCheckIsBoxIndoors(const Vec3 world_min, const Vec3 world_max, RdrAmbientLight **indoor_ambient_light, F32 *indoor_light_range, bool *can_see_outdoors, const WorldRegion * region);
bool gfxCheckIsSphereIndoors(const Vec3 world_mid, F32 radius, F32 size, RdrAmbientLight **indoor_ambient_light, F32 *indoor_light_range, bool *can_see_outdoors, const WorldRegion * region);
void gfxCalcSimpleLightValueForPoint(const Vec3 vPos, Vec3 vColor);

__forceinline static GfxLight *GfxLightFromRdrLight(RdrLight *light)
{
	if (!light)
		return NULL;
	return (GfxLight *)(((U8 *)light) - OFFSETOF(GfxLight, rdr_light));
}

__forceinline static bool isLightOk(GfxLight *light, bool is_indoors, bool key_lights_only)
{
	if(light->disabled) {
	
		return false;
	
	} else {

		bool indoorOutdoorTest =
			(gfx_state.currentAction && gfx_state.currentAction->use_sun_indoors && light->is_sun) || 
			!(is_indoors != (light->indoors && !light->is_sun));

		return indoorOutdoorTest || !(key_lights_only && !light->key_light);
	}
}

__forceinline static void setLightIndoors(GfxLight *light)
{
	light->indoors = !light->is_sun && !!gfxCheckIsSphereIndoors(light->world_query_mid, 0, 0, NULL, NULL, NULL, light->region);
}

__forceinline static void setRdrLightType(GfxLight *light)
{
	int deleting_flag_state = light->rdr_light.light_type & RDRLIGHT_DELETING;
	if (!gfx_state.currentAction || !gfx_state.currentAction->gdraw.do_shadows)
		light->rdr_light.light_type = rdrGetSimpleLightType(light->orig_light_type);
	else
		light->rdr_light.light_type = light->orig_light_type;
	light->rdr_light.light_type |= deleting_flag_state;
}

__forceinline static F32 calcLightDist(const Vec4 vViewZ, const Vec3 lightPos)
{
	return -(dotVec3(vViewZ, lightPos) + vViewZ[3]);
}

__forceinline static F32 interp(F32 minval, F32 maxval, F32 val)
{
	float bottom = maxval - minval;
	if (bottom < 0)
		return 1 - saturate((val - maxval) / (-bottom));
	else if (bottom > 0)
		return saturate((val - minval) / bottom);
	return 0;
}

__forceinline static int cmpLightsNonDeferred(const GfxLight **light1, const GfxLight **light2)
{
	// DJR much faster to use integer comparison of floats, when the values are
	// still in memory, and we know both are positive or negative
	return *(int*)&(*light2)->nondeferred_sort_param - *(int*)&(*light1)->nondeferred_sort_param;
}


extern GfxLight unlit_light;
extern RdrAmbientLight unlit_ambient_light;
extern WorldVolumeQueryCache *lights_room_query_cache;
extern int lights_room_volume_type;
extern int gMaxLightsPerObject;
extern int disable_shadow_caster_graph, use_new_light_selector;

#endif //_GFXLIGHTSPRIVATE_H_

