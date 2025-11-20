#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/

#ifndef UI_PANE_H
#define UI_PANE_H

#include "UICore.h"

#define UI_PANE(widget) (&widget->pane)
#define UI_PANE_TYPE UIPane pane;

#define	UI_PANE_VP_TOP		1		// restrict viewport to top of pane
#define UI_PANE_VP_BOTTOM	2		// restrict viewport to bottom of pane
#define UI_PANE_VP_LEFT		4		// restrict viewport to left of pane
#define UI_PANE_VP_RIGHT	8		// restrict viewport to right of pane

typedef struct UIPane
{
	UIWidget widget;

	// If non-zero, draw a title border at the top of the pane
	F32 titleHeight;

	const char* astrStyleOverride_USE_ACCESSOR;
	const char* astrTitleStyleOverride_USE_ACCESSOR;
	bool bOverrideUseTextureAssemblies_USE_ACCESSOR;
	bool bOverrideUseLegacyColor_USE_ACCESSOR;

	// If true, no border or background will be drawn; only the children.
	// Unlike e.g. the 'show' window property, the pane will still exist and
	// its children will get events if it is invisible.
	bool invisible : 1;

	unsigned drawEvenIfInvisible : 1;

	// Directions that you are allowed to resize the pane.  Default is none.
	UIDirection resizable;
	// What direction the pane is currently resizing in, if any.
	UIDirection resizing;
	int grabbedX, grabbedY;

	// Minimum size for the pane, not including decorations
	F32 minW, minH;

	// Called when the user finishes resizing the pane
	UIActivationFunc resizedF;
	UserData resizedData;

	// The pane will restrict the viewport after it is drawn according to some OR'ed combination
	// of the UI_PANE_VP flags
	U8 viewportPane;
} UIPane;

SA_RET_NN_VALID UIPane *ui_PaneCreateEx(F32 x, F32 y, F32 width, F32 height, UIUnitType widthUnit, UIUnitType heightUnit, U8 viewportPane MEM_DBG_PARMS);
#define ui_PaneCreate(x, y, width, height, widthUnit, heightUnit, viewportPane) ui_PaneCreateEx(x, y, width, height, widthUnit, heightUnit, viewportPane MEM_DBG_PARMS_INIT)
void ui_PaneInitialize(SA_PRE_NN_FREE SA_POST_NN_VALID UIPane *pane, F32 x, F32 y, F32 width, F32 height, UIUnitType widthUnit, UIUnitType heightUnit, U8 viewportPane MEM_DBG_PARMS);
void ui_PaneFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UIPane *pane);

// Keeps a local copy of the border, so you can pass in a stack variable and/or
// free it after the function is done.
void ui_PaneSetStyle(SA_PARAM_NN_VALID UIPane *pane, SA_PARAM_OP_STR const char *pchStyleName, bool bUseTextureAssemblies, bool bUseLegacyColor);
void ui_PaneSetStyleEx(SA_PARAM_NN_VALID UIPane *pane, SA_PARAM_OP_STR const char *pchStyleName, SA_PARAM_OP_STR const char *pchTitleStyleName, bool bUseTextureAssemblies, bool bUseLegacyColor);

void ui_PaneSetInvisible(SA_PARAM_NN_VALID UIPane *pane, bool invisible);

void ui_PaneSetTitleHeight(SA_PARAM_NN_VALID UIPane *pane, F32 height);

void ui_PaneSetResizable(SA_PARAM_NN_VALID UIPane *pane, UIDirection resizable, F32 minW, F32 minH);

void ui_PaneTick(SA_PARAM_NN_VALID UIPane *pane, UI_PARENT_ARGS);
void ui_PaneDraw(SA_PARAM_NN_VALID UIPane *pane, UI_PARENT_ARGS);

void ui_PaneAddChild(SA_PARAM_NN_VALID UIPane *pane, SA_PRE_NN_BYTES(sizeof(UIWidget)) SA_POST_NN_VALID UIAnyWidget *child);
void ui_PaneRemoveChild(SA_PARAM_NN_VALID UIPane *pane, SA_PRE_NN_BYTES(sizeof(UIWidget)) SA_POST_NN_VALID UIAnyWidget *child);

#endif
