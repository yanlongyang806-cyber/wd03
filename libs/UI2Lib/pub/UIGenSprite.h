#pragma once
GCC_SYSTEM
#ifndef UI_GEN_SPRITE_H
#define UI_GEN_SPRITE_H

#include "UIGen.h"

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UIGenSprite
{
	UIGenInternal polyp; AST(POLYCHILDTYPE(kUIGenTypeSprite))
	UIGenBundleTexture TextureBundle; AST(EMBEDDED_FLAT)

	// Additional Layers
	UIGenBundleTexture **eaLowerLayer; AST(NAME(LowerLayer))
	UIGenBundleTexture **eaLayer; AST(NAME(Layer))
} UIGenSprite;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UIGenSpriteState
{
	UIGenPerTypeState polyp; AST(POLYCHILDTYPE(kUIGenTypeSprite))
	UIGenBundleTextureState TextureState; AST(EMBEDDED_FLAT)

	// Additional Layers
	UIGenBundleTextureState **eaLowerLayer;
	UIGenBundleTextureState **eaLayer;
	S32 iBackgroundLayer;
} UIGenSpriteState;

#endif