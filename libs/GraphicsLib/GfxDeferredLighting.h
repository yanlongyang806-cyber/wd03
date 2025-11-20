#pragma once
GCC_SYSTEM

typedef struct GfxLight GfxLight;
typedef struct RdrDrawList RdrDrawList;


void gfxUpdateDeferredLightModels(void);

void gfxDoDebugPointLight(GfxLight *light, RdrDrawList *draw_list);
void gfxDoDebugSpotLight(GfxLight *light, RdrDrawList *draw_list);
void gfxDoDebugProjectorLight(GfxLight *light, RdrDrawList *draw_list);

