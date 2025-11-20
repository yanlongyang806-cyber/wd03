#pragma once
GCC_SYSTEM
#ifndef UI_GEN_BUTTON_H
#define UI_GEN_BUTTON_H

#include "UIGen.h"

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UIGenButton
{
	UIGenInternal polyp; AST(POLYCHILDTYPE(kUIGenTypeButton))
	UIGenAction *pOnClicked;
	const char *pchTexture; AST(POOL_STRING RESOURCEDICT(Texture))
	UIGenBundleText TextBundle; AST(EMBEDDED_FLAT)
	REF_TO(Message) hTruncate; AST(NAME(Truncate) NON_NULL_REF)
	U32 uiColor; AST(FORMAT_COLOR SUBTABLE(ColorEnum) NAME("Color"))

	U16 iTextureWidth; AST(NAME(TextureWidth))
	U16 iTextureHeight; AST(NAME(TextureHeight))

	// Position of text relative to texture.
	// Currently supported: Left, Right, Center, Above, Below.
	U8 eTextureAlignment; AST(SUBTABLE(UIDirectionEnum) DEFAULT(UILeft))

	U8 chSpacing; AST(NAME(Spacing) SUBTABLE(UISizeEnum) DEFAULT(8))

	bool bTextureFromCommand : 1;
	bool bSkinningOverride : 1;
} UIGenButton;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UIGenButtonState
{
	UIGenPerTypeState polyp; AST(POLYCHILDTYPE(kUIGenTypeButton))
	char *pchString; AST(ESTRING)
	AtlasTex *pTex; NO_AST
	bool bPressed;
	UIGenBundleTruncateState Truncate;
	UIStyleFont *pTextFont; NO_AST
	Vec2 v2TextSize;
} UIGenButtonState;

void ui_GenButtonClick(UIGen *pGen);

#endif