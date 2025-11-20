#pragma once
GCC_SYSTEM

typedef struct GfxSpriteList GfxSpriteList;
typedef struct GfxSpriteListEntry GfxSpriteListEntry;
typedef struct RdrDrawList RdrDrawList;
typedef struct RdrSpriteState RdrSpriteState;
typedef struct RdrSpriteVertex RdrSpriteVertex;
typedef struct AtlasTex AtlasTex;
typedef struct BasicTexture BasicTexture;

GfxSpriteList* gfxCreateSpriteList(U32 mpSize, bool storeTexPointers, bool isSpriteCache);
void gfxDestroySpriteList(GfxSpriteList* sl);
void gfxClearSpriteList(GfxSpriteList* sl);

void gfxMergeSpriteLists(GfxSpriteList* dst, GfxSpriteList* src, bool updateTexHandles);

//The draw list can be null if you dont want to use the linearAllocator
void gfxRenderSpriteList(GfxSpriteList* sl, RdrDrawList* drawList, bool alsoClear);
void gfxRenderSpriteList3D(GfxSpriteList* sl, RdrDrawList* drawList, bool alsoClear);

GfxSpriteListEntry* gfxStartAddSpriteToList(GfxSpriteList* spriteList, F32 zvalue, bool b3D, RdrSpriteState** outSpriteState, RdrSpriteVertex** outFourVerts);
void gfxInsertSpriteListEntry(GfxSpriteList* sl, GfxSpriteListEntry* entry, bool is3D);
void gfxSpriteListHookupTextures(GfxSpriteList* spriteList, GfxSpriteListEntry* entry, AtlasTex *atex1, BasicTexture *btex1, AtlasTex *atex2, BasicTexture *btex2, bool is3D);

GfxSpriteList* gfxGetDefaultSpriteList();

void gfxMaterialsAssertTexNotInSpriteList(GfxSpriteList* sl, BasicTexture* tex);

typedef enum SpriteFlags {
	SPRITE_2D = 0,
	SPRITE_3D = 1 << 0,
	SPRITE_IGNORE_Z_TEST = 1 << 1,
} SpriteFlags;
