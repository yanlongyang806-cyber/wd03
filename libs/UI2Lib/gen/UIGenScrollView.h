#pragma once
GCC_SYSTEM
#ifndef UI_GEN_SCROLL_VIEW_H
#define UI_GEN_SCROLL_VIEW_H

#include "UIGen.h"
#include "UIGenScrollbar.h"

AUTO_STRUCT;
typedef struct UIGenScrollView
{
	UIGenInternal polyp; AST(POLYCHILDTYPE(kUIGenTypeScrollView))
	UIGenScrollbar Scrollbar; AST(EMBEDDED_FLAT)
	S16 iVirtualTopPadding; AST(SUBTABLE(UISizeEnum))
	S16 iVirtualBottomPadding; AST(SUBTABLE(UISizeEnum))
	S16 iVirtualLeftPadding; AST(SUBTABLE(UISizeEnum))
	S16 iVirtualRightPadding; AST(SUBTABLE(UISizeEnum))
} UIGenScrollView;

AUTO_STRUCT;
typedef struct UIGenScrollViewState
{
	UIGenPerTypeState polyp; AST(POLYCHILDTYPE(kUIGenTypeScrollView))
	UIGenScrollbarState Scrollbar;
	CBox RealScreenBox; NO_AST
} UIGenScrollViewState;

#endif