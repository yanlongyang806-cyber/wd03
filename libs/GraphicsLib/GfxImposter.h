#pragma once
GCC_SYSTEM

typedef struct GfxImposterDebug
{
	U32 uiNumImposters;
	U32 uiNumImposterAtlas;
	U32 uiPeakNumImposterAtlas;
} GfxImposterDebug;

extern GfxImposterDebug gfxImposterDebug;


typedef struct WLCostume WLCostume;
typedef struct BasicTexture BasicTexture;

void gfxImposterAtlasDrawBodysockAtlas(void);
BasicTexture* gfxImposterAtlasGetBodysockTexture(WLCostume* pCostume, S32* piSectionIndex, Vec4 vTexXFrm);
bool gfxImposterAtlasReleaseBodysockTexture(BasicTexture* pTexture, S32 iSectionIndex);
void gfxImposterAtlasOncePerFrame(void);
bool gfxImposterAtlasDumpBodysockAtlasToFile(const char* pcFileName);