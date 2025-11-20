#pragma once
GCC_SYSTEM
#ifndef UI_GEN_MOVABLE_BOX_H
#define UI_GEN_MOVABLE_BOX_H

#include "UIGen.h"

extern StaticDefineInt MouseButtonEnum[];

//////////////////////////////////////////////////////////////////////////
// A movable box behaves like a UIGenBox but it has another UIPosition
// object that it uses to store and override position information. As
// a result, the user can move and resize MovableBoxes.

AUTO_STRUCT;
typedef struct UIGenMovableBox
{
	UIGenInternal polyp; AST(POLYCHILDTYPE(kUIGenTypeMovableBox))
	MouseButton eMovable; AST(DEFAULT(-1) SUBTABLE(MouseButtonEnum))
	MouseButton eResizableHorizontal; AST(DEFAULT(-1) SUBTABLE(MouseButtonEnum))
	MouseButton eResizableVertical; AST(DEFAULT(-1) SUBTABLE(MouseButtonEnum))
	UISizeSpec *pMovableWidth;
	UISizeSpec *pMovableHeight;

	bool bRaise;

	// The area around the edge where dragging triggers a resize.
	F32 fResizeBorder;

	// Version number of this gen. Increase this to invalidate the user's
	// preferred size and position of the widget (useful if its contents
	// change drastically).
	S32 iVersion;
} UIGenMovableBox;

AUTO_STRUCT;
typedef struct UIGenMovableBoxState
{
	UIGenPerTypeState polyp; AST(POLYCHILDTYPE(kUIGenTypeMovableBox))
	UIPosition Original;
	UIPosition *pOverride;
	bool bMoving;
	UIDirection eResizing;
	S32 iGrabbedX;
	S32 iGrabbedY;
	U8 chPriority;
} UIGenMovableBoxState;

void ui_GenMovableBoxRaise(UIGen *pGen);

#endif

