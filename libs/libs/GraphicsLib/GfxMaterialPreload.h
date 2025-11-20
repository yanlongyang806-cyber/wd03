#pragma once
GCC_SYSTEM

typedef struct SingleModelParams SingleModelParams;

void gfxMaterialFlagPreloadedGlobalTemplates(void);
void gfxMaterialFlagPreloadedMapSpecificTemplates(bool bEarly);
void gfxMaterialFlagPreloadedAllTemplates(void);

void gfxMaterialPreloadTemplates(void); // Starts the loading.  Must call gfxMaterialPreloadOncePerFrame() until gfxMaterialPreloadGetLoadingCount() returns 0

int gfxMaterialPreloadGetLoadingCount(void);

void gfxMaterialPreloadOncePerFrame(bool bEarly); // If not early, we're past the login screen

void preloadBeginEndFrame(int begin, SingleModelParams *params, bool bSkinned);
