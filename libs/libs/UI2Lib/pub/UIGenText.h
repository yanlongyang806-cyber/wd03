#pragma once
GCC_SYSTEM
#ifndef UI_GEN_TEXT_H
#define UI_GEN_TEXT_H

#include "UIGen.h"

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UIGenText
{
	UIGenInternal polyp; AST(POLYCHILDTYPE(kUIGenTypeText))
	UIGenBundleText TextBundle; AST(EMBEDDED_FLAT)
	REF_TO(Message) hTruncate; AST(NAME(Truncate) NON_NULL_REF)
} UIGenText;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UIGenTextState
{
	UIGenPerTypeState polyp; AST(POLYCHILDTYPE(kUIGenTypeText))
	char* pchStaticString; AST(ESTRING)

	// Update params
	char* pchString; AST(ESTRING)
	UIGenBundleTruncateState Truncate;
	UIStyleFont *pTextFont; NO_AST
	Vec2 v2TextSize;

	// Draw params
	char* pchDrawFinalText; AST(ESTRING)
	Vec2 v2DrawFinalSize;
	F32 fDrawFinalScale;
	UIStyleFont *pDrawTextFont; NO_AST
	F32 fDrawInputScale;
	Vec2 v2DrawAreaSize;
} UIGenTextState;

#endif