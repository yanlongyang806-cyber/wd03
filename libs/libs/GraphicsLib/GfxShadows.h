#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/

#ifndef _GFXSHADOWS_H_
#define _GFXSHADOWS_H_

#include "stdtypes.h"

typedef struct GfxShadowMap GfxShadowMap;
typedef struct ZoneMap ZoneMap;
typedef struct GfxLight GfxLight;
typedef struct Frustum Frustum;
typedef struct RdrSurface RdrSurface;

#define MAX_SHADOWED_LIGHTS_PER_FRAME 3

void gfxFreeShadows(GfxLight *light);

void gfxShadowsBeginFrame(const Frustum *camera_frustum, const Mat44 projection_mat);
void gfxShadowsEndFrame(void);

void gfxGetPSSMSettingsFromSkyOrRegion(const ShadowRules* sky_shadow_rules, WorldRegionType region_type, PSSMSettings *settings);

void gfxShadowDirectionalLight(GfxLight *dir_light, const Frustum *camera_frustum, RdrDrawList *draw_list);
void gfxShadowDirectionalLightCalcProjMatrix(GfxLight *dir_light, const Frustum *camera_frustum);
void gfxShadowDirectionalLightDraw(GfxLight *dir_light, const Frustum *camera_frustum, RdrDrawList *draw_list);

void gfxShadowPointLight(GfxLight *point_light, const Frustum *camera_frustum, RdrDrawList *draw_list);
void gfxShadowPointLightCalcProjMatrix(GfxLight *point_light, const Frustum *camera_frustum);
void gfxShadowPointLightDraw(GfxLight *point_light, const Frustum *camera_frustum, RdrDrawList *draw_list);

void gfxShadowSpotLight(GfxLight *spot_light, const Frustum *camera_frustum, RdrDrawList *draw_list);
void gfxShadowSpotLightCalcProjMatrix(GfxLight *spot_light, const Frustum *camera_frustum);
void gfxShadowSpotLightDraw(GfxLight *spot_light, const Frustum *camera_frustum, RdrDrawList *draw_list);

void gfxSetCubemapLookupTexture(RdrDevice *rdr_device, bool active);
void gfxShadowFlushCubemapLookupSurfaceOnDeviceLoss(void);

bool gfxIsThinShadowCaster(const Vec3 bmin, const Vec3 bmax);

#endif //_GFXSHADOWS_H_

