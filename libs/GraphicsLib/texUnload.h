#pragma once
GCC_SYSTEM

typedef struct GfxRenderAction GfxRenderAction;
typedef struct BasicTexture BasicTexture;

typedef enum TexUnloadMode
{
	TEXUNLOAD_DISABLE,
	TEXUNLOAD_ENABLE_DEFAULT,
	TEXUNLOAD_ENABLE_FORCE_UNLOAD_ALL_RAW,
	TEXUNLOAD_ENABLE_FORCE_UNLOAD_ALL
} TexUnloadMode;

TexUnloadMode texDynamicUnloadEnabled(void);
void texDynamicUnload(TexUnloadMode enable);
void texUnloadTexturesToFitMemory(void);
void texUnloadAllNotUsedThisFrame(void);
U32 texDesiredLevels(BasicTexture *bind, float distance, float uv_density, const GfxRenderAction *action);

int gfxCompareTimestamps(U32 a, U32 b); // Assumes no timestamps are in the future

// Returns the approximate number of loaded textures
int texPartialSort(void);
