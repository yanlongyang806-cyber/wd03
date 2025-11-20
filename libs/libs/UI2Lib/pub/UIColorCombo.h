/***************************************************************************



***************************************************************************/

#pragma once
GCC_SYSTEM

#include "UICore.h"
#include "UIButton.h"

typedef struct UIColorSet UIColorSet;

typedef void (*UIColorHoverFunc)(UIAnyWidget *, bool isHover, Vec4 color, UserData);


//////////////////////////////////////////////////////////////////////////
// A grid of palette colors.

typedef struct UIPaleteGrid
{
	UIWidget widget;
	UIColorSet *pColorSet;
	Vec4 vColor;
	int iNumPerRow; // number of items per row

	UIActivationFunc changedF;
	UserData changedData;
	UIColorHoverFunc hoverF;
	UserData hoverData;

	int iSelected;
	int iHovered;
} UIPaletteGrid;

SA_RET_NN_VALID UIPaletteGrid *ui_PaletteGridCreate(F32 x, F32 y, UIColorSet *pColorSet, int iNumPerRow, SA_PRE_NN_RELEMS(4) const Vec4 color);
void ui_PaletteGridInitialize(SA_PRE_NN_FREE SA_POST_NN_VALID UIPaletteGrid *pGrid, F32 x, F32 y, UIColorSet *pColorSet, int iNumPerRow, SA_PRE_NN_RELEMS(4) const Vec4 vColor);
void ui_PaletteGridSetColorSet(SA_PARAM_NN_VALID UIPaletteGrid *pGrid, UIColorSet *pColorSet);
void ui_PaletteGridSetNumPerRow(SA_PARAM_NN_VALID UIPaletteGrid *pGrid, int iNumPerRow);

void ui_PaletteGridSetChangedCallback(SA_PARAM_NN_VALID UIPaletteGrid *pGrid, UIActivationFunc changedF, UserData changedData);
void ui_PaletteGridSetHoverCallback(SA_PARAM_NN_VALID UIPaletteGrid *pGrid, UIColorHoverFunc hoverF, UserData hoverData);

void ui_PaletteGridGetColor(SA_PARAM_NN_VALID UIPaletteGrid *pGrid, SA_PRE_NN_ELEMS(4) SA_POST_OP_VALID Vec4 color);
void ui_PaletteGridSetColor(SA_PARAM_NN_VALID UIPaletteGrid *pGrid, SA_PRE_NN_RELEMS(4) const Vec4 color);
void ui_PaletteGridSetColorAndCallback(SA_PARAM_NN_VALID UIPaletteGrid *pGrid, SA_PRE_NN_RELEMS(4) const Vec4 color);


//////////////////////////////////////////////////////////////////////////
// A button that brings up a PaletteGrid when clicked.

typedef struct UIColorCombo
{
	UI_INHERIT_FROM(UI_WIDGET_TYPE UI_BUTTON_TYPE);
	UIPaletteGrid *pGrid;
	UIColorSet *pColorSet;
	Vec4 vColor;
	int iNumPerRow; // number of items per row

	UIActivationFunc changedF;
	UserData changedData;
	UIColorHoverFunc hoverF;
	UserData hoverData;

	bool bOpened;
	bool bCloseOnNextTick;
} UIColorCombo;

SA_RET_NN_VALID UIColorCombo *ui_ColorComboCreate(F32 x, F32 y, UIColorSet *pColorSet, SA_PRE_NN_RELEMS(4) const Vec4 color);
void ui_ColorComboInitialize(SA_PRE_NN_FREE SA_POST_NN_VALID UIColorCombo *pCombo, F32 x, F32 y, UIColorSet *pColorSet, const Vec4 color);
void ui_ColorComboSetColorSet(SA_PARAM_NN_VALID UIColorCombo *pCombo, UIColorSet *pColorSet);
void ui_ColorComboSetNumPerRow(SA_PARAM_NN_VALID UIColorCombo *pCombo, int num_per_row);

void ui_ColorComboSetChangedCallback(SA_PARAM_NN_VALID UIColorCombo *pCombo, UIActivationFunc changedF, UserData changedData);
void ui_ColorComboSetHoverCallback(SA_PARAM_NN_VALID UIColorCombo *pCombo, UIColorHoverFunc hoverF, UserData hoverData);

void ui_ColorComboGetColor(SA_PARAM_NN_VALID UIColorCombo *pCombo, SA_PRE_NN_ELEMS(4) SA_POST_OP_VALID Vec4 color);
void ui_ColorComboSetColor(SA_PARAM_NN_VALID UIColorCombo *pCombo, SA_PRE_NN_RELEMS(4) const Vec4 color);
void ui_ColorComboSetColorAndCallback(SA_PARAM_NN_VALID UIColorCombo *pCombo, SA_PRE_NN_RELEMS(4) const Vec4 color);


