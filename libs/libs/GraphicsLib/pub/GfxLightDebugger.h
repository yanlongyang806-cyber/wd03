/***************************************************************************



***************************************************************************/

#ifndef _GFXLIGHTDEBUGGER_H_
#define _GFXLIGHTDEBUGGER_H_
GCC_SYSTEM

#include "RdrEnums.h"

typedef struct GfxLightCacheBase GfxLightCacheBase;


AUTO_STRUCT;
typedef struct GfxShadowLightDebug
{
	int drawn_shadow_caster_count;		AST( NAME(ShadowCasterObjs) )
	int drawn_shadow_caster_tri_count;	AST( NAME(ShadowCasterTris) )
	F32 shadowmap_quality;				AST( NAME(ShadowmapQuality) )
} GfxShadowLightDebug;

AUTO_STRUCT;
typedef struct GfxLightDebug
{
	char *name;
	char *light_type;
	char *affects;

	Vec3 position;					AST( NAME(Position) )
	Vec3 direction;					AST( NAME(Direction) )
	int distance;					AST( NAME(Distance) )
	int radius;						AST( NAME(Radius) )
	int use_count;					AST( NAME(UseCount) )

	Vec3 ambient_color;				AST( FORMAT_HSV NAME(AmbientColor) )
	Vec3 diffuse_color;				AST( FORMAT_HSV NAME(DiffuseColor) )
	Vec3 specular_color;			AST( FORMAT_HSV NAME(SpecularColor) )

	const char *projected_texture;	AST( POOL_STRING )

	bool casts_shadows;
	bool is_dynamic;
	bool casts_shadows_this_frame;
	bool occluded;
	bool indoors;

	GfxShadowLightDebug *shadowed;

	char *owner_room_name;			AST( NAME(Room) )
	bool owner_room_limits_lights;	AST( NAME(RoomLimitsLight) )

	F32 shadow_sort_val;

	AST_STOP

	RdrLightType simple_light_type;
	F32 radius_float;
	Mat4 world_matrix;
	Vec3 bound_min;
	Vec3 bound_max;
	Vec3 moving_bound_min;
	Vec3 moving_bound_max;
	F32 angle1;
	F32 angle2;
	int light_id;

} GfxLightDebug;

typedef struct GfxLightDebugger
{
	GfxLightCacheBase *cur_cache;

} GfxLightDebugger;

extern GfxLightDebugger gfx_light_debugger;

extern ParseTable parse_GfxShadowLightDebug[];
#define TYPE_parse_GfxShadowLightDebug GfxShadowLightDebug
extern ParseTable parse_GfxLightDebug[];
#define TYPE_parse_GfxLightDebug GfxLightDebug

GfxLightDebug **gfxGetKeyLights(SA_PARAM_OP_VALID const GfxLightCacheBase *light_cache);
GfxLightDebug **gfxGetShadowedLights(void);

// in GfxLights.c
void gfxSetDebugLight(int light_id);

// in GfxLightCache.c
void gfxSetDebugLightCache(int light_cache_id);
void gfxGetLightCacheMidPoint(SA_PARAM_NN_VALID const GfxLightCacheBase *light_cache, Vec3 out_mid);
void gfxLightDebuggerClear();
bool gfxLightDebuggerIsCacheIndoors(SA_PARAM_NN_VALID const GfxLightCacheBase *light_cache);
char * gfxLightDebuggerGetCacheRoomList(SA_PARAM_NN_VALID const GfxLightCacheBase *light_cache);

__forceinline void gfxLightDebuggerNotifyCacheDestroy(const GfxLightCacheBase * light_cache)
{
	if (gfx_light_debugger.cur_cache == light_cache)
		gfx_light_debugger.cur_cache = NULL;
}

#endif //_GFXLIGHTDEBUGGER_H_
