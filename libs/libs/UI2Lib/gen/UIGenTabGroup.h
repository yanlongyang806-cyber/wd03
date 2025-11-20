#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/

#ifndef UI_GEN_TAB_GROUP_H
#define UI_GEN_TAB_GROUP_H

#include "UIGen.h"


AUTO_ENUM;
typedef enum UIGenTabScrollerVisibilty {
	kGenTabScrollerVisibleNever,
	kGenTabScrollerVisibleIfNeeded,
	kGenTabScrollerVisibleAlways
} UIGenTabScrollerVisibilty;

//////////////////////////////////////////////////////////////////////////
// A tab group. A tab group contains two major areas.  The first is the 
// header row.  The header row is subdivided into the tab header buttons
// (left aligned) and custom buttons (right aligned).  Tab header buttons
// behave like radio buttons in that only one may be selected at any time.
// The second major area is the tab panel.  The tab panel displays the 
// child associated with the currently selected tab header button. This
// child may be arbitrary.
AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UIGenTabGroup {
	UIGenInternal polyp; AST(POLYCHILDTYPE(kUIGenTypeTabGroup))
	Expression *pGetModel; AST(NAME(ModelBlock) REDUNDANT_STRUCT(Model, parse_Expression_StructParam) LATEBIND)

	REF_TO(UIGen) hTabTemplate; AST(NAME(TabTemplate))
	UIGenChild **eaTemplateChild; AST(NAME(TemplateChild) NAME(TemplateChildren))

	const char *pchScrollTabLeft; AST(POOL_STRING)
	const char *pchScrollTabRight; AST(POOL_STRING)
	const char *pchScrollTabLeftMouseOver; AST(POOL_STRING)
	const char *pchScrollTabRightMouseOver; AST(POOL_STRING)
	const char *pchScrollTabLeftPressed; AST(POOL_STRING)
	const char *pchScrollTabRightPressed; AST(POOL_STRING)

	U32 uiScrollTabColor; AST(NAME(ScrollTabColor) FORMAT_COLOR SUBTABLE(ColorEnum))
	U32 uiScrollTabDisabledColor; AST(NAME(ScrollTabDisabledColor) FORMAT_COLOR SUBTABLE(ColorEnum))
	
	// Spacing of tabs
	S16 sLeftPad; AST(SUBTABLE(UISizeEnum) NAME(LeftPad)) // Padding between tabs and left margin
	S16 sTabSpacing; AST(SUBTABLE(UISizeEnum) NAME(TabSpacing)) // Space between tabs
	S16 sTabToScrollMinSpacing; AST(SUBTABLE(UISizeEnum) NAME(TabToScrollMinSpacing)) // Minimum spacing between tabs and scroll buttons
	S16 sScrollSpacing; AST(SUBTABLE(UISizeEnum) NAME(ScrollSpacing)) // Space between scroll buttons
	S16 sRightPad; AST(SUBTABLE(UISizeEnum) NAME(RightPad)) // Padding between scroll buttons (if visible, otherwise tabs) & right margin

	U8 eVerticalScrollButtonAlignment; AST(SUBTABLE(UIDirectionEnum))
	U8 eTabScrollerVisibility; AST(DEFAULT(kGenTabScrollerVisibleIfNeeded) SUBTABLE(UIGenTabScrollerVisibiltyEnum))

	bool bStretchTabsToFit : 1;
	bool bScrollTabUseWheel : 1; AST(DEFAULT(1)) //If the tab selection should react to the scroll wheel or not
	bool bAppendTemplateChildren : 1; // If this is set then TemplateChildren are appended to the list model
} UIGenTabGroup;

// Persisted state for UIGenTabGroups
AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UIGenTabGroupState {
	UIGenPerTypeState polyp; AST(POLYCHILDTYPE(kUIGenTypeTabGroup))

	UIGen *pTabTemplate; AST(UNOWNED STRUCT_NORECURSE)
	
	UIGen **eaTabs; AST(STRUCT_NORECURSE NO_INDEX UNOWNED)
	UIGen **eaOwnedTabs; AST(STRUCT_NORECURSE NO_INDEX)
	UIGenChild **eaTemplateGens; AST(NO_INDEX)

	const char *pchScrollTabLeft; AST(POOL_STRING)
	const char *pchScrollTabRight; AST(POOL_STRING)

	S32 iSelectedTab;
	F32 fScrollPosition;
	U32 iLastTabScrollTime;
	UIGenState eLastScrollState;

	bool bForceShowSelectedTab : 1;
	bool bScrollButtonsVisible : 1;
	bool bScrollButtonClickedLastFrame : 1;
} UIGenTabGroupState;

AUTO_STRUCT;
typedef struct TabGroupTabTitle
{
	char *pchTitle;		AST(NAME(Title) ESTRING)
} TabGroupTabTitle;

extern bool ui_GenTabGroupMayDecrementSelectedTab(UIGen *pGen, UIGenTabGroupState *pState);
extern bool ui_GenTabGroupMayIncrementSelectedTab(UIGen *pGen, UIGenTabGroupState *pState);
extern bool ui_GenTabGroupDecrementSelectedTab(UIGen *pGen, UIGenTabGroupState *pState);
extern bool ui_GenTabGroupIncrementSelectedTab(UIGen *pGen, UIGenTabGroupState *pState);
extern bool ui_GenTabGroupSetSelectedTabIndex(UIGen *pGen, UIGenTabGroupState *pState, S32 iTab);

#endif