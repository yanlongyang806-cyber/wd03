#pragma once
GCC_SYSTEM

#include "UICore.h"

//////////////////////////////////////////////////////////////////////////
// A scrollbar to attach to UIGens. To use it, put a UIGenScrollbar
// in your UIGenInternal subtype with AST(EMBEDDED_FLAT), and a
// UIGenScrollbarState in your UIGenPerTypeState subtype. Then call
// ui_GenTickScrollbar and ui_GenDrawScrollbar during your tick and draw
// functions, and make sure to subtract ui_GenScrollbarWidth as appropriate
// during your layout functions.

AUTO_STRUCT;
typedef struct UIGenScrollbarDef
{
	const char *pchName; AST(KEY STRUCTPARAM REQUIRED)

	const char *pchFilename; AST(CURRENTFILE)

	REF_TO(UITextureAssembly) hTopDefault; AST(NAME(TopDefault) NON_NULL_REF)
	REF_TO(UITextureAssembly) hBottomDefault; AST(NAME(BottomDefault) NON_NULL_REF)
	REF_TO(UITextureAssembly) hHandleDefault; AST(NAME(HandleDefault) NON_NULL_REF)

	REF_TO(UITextureAssembly) hTopHover; AST(NAME(TopHover) NON_NULL_REF)
	REF_TO(UITextureAssembly) hBottomHover; AST(NAME(BottomHover) NON_NULL_REF)
	REF_TO(UITextureAssembly) hHandleHover; AST(NAME(HandleHover) NON_NULL_REF)

	REF_TO(UITextureAssembly) hTopMouseDown; AST(NAME(TopMouseDown) NON_NULL_REF)
	REF_TO(UITextureAssembly) hBottomMouseDown; AST(NAME(BottomMouseDown) NON_NULL_REF)
	REF_TO(UITextureAssembly) hHandleMouseDown; AST(NAME(HandleMouseDown) NON_NULL_REF)

	REF_TO(UITextureAssembly) hTrough; AST(NAME(Trough) NON_NULL_REF)

	S16 iTopHeight; AST(SUBTABLE(UISizeEnum))
	S16 iBottomHeight; AST(SUBTABLE(UISizeEnum))
	S16 iMinHandleHeight; AST(SUBTABLE(UISizeEnum))
	S16 iWidth; AST(SUBTABLE(UISizeEnum) REQUIRED)

	UIDirection eOffsetFrom; AST(DEFAULT(UIRight))

} UIGenScrollbarDef;

AUTO_STRUCT;
typedef struct UIGenScrollbar
{
	REF_TO(UIGenScrollbarDef) hScrollbarDef; AST(NAME(ScrollbarDef))
	REF_TO(UIGenScrollbarDef) hDisabledScrollbarDef; AST(NAME(DisabledScrollbarDef))

	S16 iScrollbarTopMargin; AST(SUBTABLE(UISizeEnum))
	S16 iScrollbarBottomMargin; AST(SUBTABLE(UISizeEnum))
	S16 iScrollbarLeftMargin; AST(SUBTABLE(UISizeEnum))
	S16 iScrollbarRightMargin; AST(SUBTABLE(UISizeEnum))

	// Don't draw the scrollbar if it doesn't have anything to scroll. i.e.
	// The available height is more than the required height.
	bool bScrollbarOnlyShowWhenNeeded : 1;

	// Allow the scrollbar to capture the mouse, even if the parent gen doesn't.
	bool bScrollbarCaptureMouse : 1;

	// If the scrollbar should react to the scroll wheel or not.
	bool bScrollbarUseWheel : 1; AST(DEFAULT(1))

	// If the scrollbar should react to the right analog stick or not.
	bool bScrollbarUseStick : 1; AST(DEFAULT(1))
} UIGenScrollbar;

AUTO_STRUCT;
typedef struct UIGenScrollbarState
{
	F32 fScrollPosition;
	F32 fTotalHeight;

	UITextureAssembly *pTop; AST(UNOWNED)
	UITextureAssembly *pBottom; AST(UNOWNED)
	UITextureAssembly *pHandle; AST(UNOWNED)

	S16 iDraggingOffset;
	S8 iHysteresis;
	bool bHideHandle : 1; AST(DEFAULT(0))
	bool bUnneeded : 1; AST(DEFAULT(0))
	bool bUseDisabled : 1; AST(DEFAULT(0))

} UIGenScrollbarState;

S32 ui_GenScrollbarWidth(UIGenScrollbar *pBar, UIGenScrollbarState *pState);
void ui_GenScrollbarBox(UIGenScrollbar *pBar, UIGenScrollbarState *pState, const CBox* pIn, CBox *pOut, F32 fScale);
void ui_GenLayoutScrollbar(UIGen *pGen, UIGenScrollbar *pBar, UIGenScrollbarState *pState, F32 fTotalHeight);
void ui_GenTickScrollbar(UIGen *pGen, UIGenScrollbar *pBar, UIGenScrollbarState *pState);
void ui_GenDrawScrollbar(UIGen *pGen, UIGenScrollbar *pBar, UIGenScrollbarState *pState);
void ui_GenHideScrollbar(UIGen *pGen, UIGenScrollbar *pBar, UIGenScrollbarState *pState);