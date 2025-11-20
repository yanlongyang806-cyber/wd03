/***************************************************************************



***************************************************************************/

#ifndef _GFXLIGHTS_H_
#define _GFXLIGHTS_H_
#pragma once
GCC_SYSTEM

#include "wlLight.h"

typedef struct Frustum Frustum;
typedef struct RdrDrawList RdrDrawList;
typedef struct WorldDrawableEntry WorldDrawableEntry;
typedef struct WorldAnimationEntry WorldAnimationEntry;
typedef struct RdrLightDefinition RdrLightDefinition;
typedef struct RdrLightParams RdrLightParams;
typedef struct RdrAmbientLight RdrAmbientLight;
typedef struct BlendedSkyInfo BlendedSkyInfo;
typedef struct WorldGraphicsData WorldGraphicsData;
typedef struct Model Model;
typedef struct Room Room;
typedef struct RdrLight RdrLight;
typedef U64 TexHandle;

void gfxLightsStartup(void);

void gfxLightsDataOncePerFrame(void);

void gfxDoLightPreDraw(void);
void gfxDoLightLoading(void);
void gfxDoLightShadowsPreDraw(const Frustum *camera_frustum, const Vec3 camera_focal_pt, RdrDrawList *draw_list);
void gfxDoLightShadowsCalcProjMatrices(const Frustum *camera_frustum);
void gfxDoLightShadowsDraw(const Frustum *camera_frustum, const Mat44 projection_mat, RdrDrawList *draw_list);
void gfxDoDeferredShadows(const RdrLightDefinition **light_defs, const BlendedSkyInfo *sky_info);
void gfxDoDeferredLighting(const Mat4 view_mat, TexHandle depthBufferTexHandle);

void gfxCreateSunLight(GfxLight **sun_light, const LightData *data, BlendedSkyInfo *new_sky_info);
void gfxUpdateSunLight(GfxLight **sun_light, const LightData *data, BlendedSkyInfo *new_sky_info);

GfxLight *gfxUpdateLight(GfxLight *light, const LightData *data, F32 vis_dist, WorldAnimationEntry *animation_controller); // adds a new light if light is NULL
void gfxRemoveLight(GfxLight *light);
void gfxFreeGfxLightFromRdrLight(RdrLight * rdr_light);

void gfxSetUnlitLightValue(F32 unlit_value);
void gfxGetUnlitLightEx(RdrLightParams *light_params, bool clear_struct);
#define gfxGetUnlitLightNoClear(light_params) gfxGetUnlitLightEx(light_params, false)
#define gfxGetUnlitLight(light_params) gfxGetUnlitLightEx(light_params, true)
void gfxGetSunLight(SA_PRE_NN_FREE SA_POST_NN_VALID RdrLightParams *light_params, bool is_character);
void gfxGetOverrideLight(SA_PRE_NN_FREE SA_POST_NN_VALID RdrLightParams *light_params, bool is_character, const Vec3 override_light_color, const Vec3 override_light_direction);
RdrAmbientLight *gfxGetOverrideAmbientLight(const Vec3 ambient, const Vec3 sky_light_color_front, const Vec3 sky_light_color_back, const Vec3 sky_light_color_side);

RdrAmbientLight *gfxUpdateAmbientLight(RdrAmbientLight * light, const Vec3 ambient, const Vec3 sky_light_color_front, const Vec3 sky_light_color_back, const Vec3 sky_light_color_side);
void gfxRemoveAmbientLight(RdrAmbientLight *ambient_light);

bool gfxCheckIsPointIndoors(const Vec3 world_mid, F32 *indoor_light_range, bool *can_see_outdoors);

Room* gfxGetOrUpdateLightOwnerRoom(GfxLight* light);
void gfxUpdateRoomLightOwnership(Room* room);

void gfxClearLightRegionGraphicsData(GfxLight *light);

void gfxClearShadowSearchData();
void gfxDebugDrawShadowGraph();

bool gfxLightsAllowMovingBBox(void);

#endif //_GFXLIGHTS_H_

