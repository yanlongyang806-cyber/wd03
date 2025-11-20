#pragma once
GCC_SYSTEM
#ifndef UI_GEN_SLIDER_H
#define UI_GEN_SLIDER_H

#include "UIGen.h"
#include "UIStyle.h"

#define UIGEN_SLIDER_DEFAULT_MAX		1.f
#define UIGEN_SLIDER_DEFAULT_NOTCH		-1

AUTO_STRUCT;
typedef struct UIGenSliderTweenInfo
{
	UITweenType eType; AST(STRUCTPARAM REQUIRED)
	F32 fTweenSpeed; AST(STRUCTPARAM REQUIRED)
	F32 fDelay; AST(STRUCTPARAM)
	UIStyleBarDirection eDirection; AST(DEFAULT(kUIStyleBarBoth))
} UIGenSliderTweenInfo;

AUTO_STRUCT;
typedef struct UIGenSlider
{
	UIGenInternal polyp; AST(POLYCHILDTYPE(kUIGenTypeSlider))
	Expression *pValue; AST(NAME(ValueBlock) REDUNDANT_STRUCT(Value, parse_Expression_StructParam) LATEBIND)
	Expression *pNotch; AST(NAME(NotchBlock) REDUNDANT_STRUCT(Notch, parse_Expression_StructParam) LATEBIND)
	UIGenAction *pOnChanged;
	UIGenAction *pOnStoppedDragging;
	Expression *pMax; AST(NAME(MaxBlock) REDUNDANT_STRUCT(Max, parse_Expression_StructParam) LATEBIND)
	//REF_TO(UIStyleBar) hBar; AST(NAME(Bar) NON_NULL_REF)
	UIGenBundleStyleBar StyleBarBundle; AST(EMBEDDED_FLAT)
	UIStyleBar *pInlineBar;
	UIGenSliderTweenInfo *pSliderTween;
	F32 fRowHorizontalOffset;
	S32 iTickCount; AST(DEFAULT(-1))
	Expression *pTickCountExpression; AST(NAME(TickCountBlock) REDUNDANT_STRUCT(TickCountExpr, parse_Expression_StructParam) LATEBIND)
	U8 chRows; AST(DEFAULT(1) NAME("Rows"))
	
	bool bValueInteractive : 1;
	bool bNotchInteractive : 1;
	bool bSnap : 1;
	bool bDragMoveMode : 1;
		//if this is set, the slider will only respond to dragging (not clicking)

} UIGenSlider;

AUTO_STRUCT;
typedef struct UIGenSliderState
{
	UIGenPerTypeState polyp; AST(POLYCHILDTYPE(kUIGenTypeSlider))
	F32 fValue;
	F32 fDisplayValue;
	F32 fMovingOverlayAlpha;
	F32 fDelay;
	F32 fNotch; AST(DEFAULT(UIGEN_SLIDER_DEFAULT_NOTCH))
	F32 fMax; AST(DEFAULT(UIGEN_SLIDER_DEFAULT_MAX))
	S32 iTickCount;
	S32 iLastMousePosition;
	U32 bHandledMouseDown : 1;
	U32 bInitialized : 1;
	REF_TO(UIStyleBar) hStyleBar;
} UIGenSliderState;


#endif