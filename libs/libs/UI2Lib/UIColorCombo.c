/***************************************************************************



***************************************************************************/

#include "Color.h"
#include "GfxSprite.h"
#include "GfxSpriteText.h"
#include "GfxTexAtlas.h"
#include "GraphicsLib.h"
#include "inputMouse.h"
#include "rgb_hsv.h"
#include "UIColor.h"
#include "UIColorCombo.h"
#include "UISkin.h"
#include "UITextureAssembly.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););


#define GRID_COL_WIDTH    15
#define GRID_ROW_HEIGHT   15

static Vec4 vZero = { 0,0,0,0 };

void ui_PaletteGridDraw(UIPaletteGrid *pGrid, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pGrid);
	int i;
	int iRow = 0, iCol = 0;
	Color bg = { 100, 100, 100, 255 };

	//scaling here would only changes the roundness of the corners.
	//also the pop-up widget is easier to use when it doesn't scale.
	ui_DrawCapsule(&box, z, bg, 1);//scale);

	if (pGrid->pColorSet) 
	{
		for(i=0; i<eaSize(&pGrid->pColorSet->eaColors); ++i) 
		{
			Color color;
			CBox cellBox;

			color.r = CLAMP(pGrid->pColorSet->eaColors[i]->color[0], 0, 255);
			color.g = CLAMP(pGrid->pColorSet->eaColors[i]->color[1], 0, 255);
			color.b = CLAMP(pGrid->pColorSet->eaColors[i]->color[2], 0, 255);
			color.a = 255; //ui_IsActive(UI_WIDGET(grid)) ? 255 : 96;

			// Set up the box
			CBoxSet(&cellBox, x+(iCol*GRID_COL_WIDTH)+UI_HSTEP, y+(iRow*GRID_ROW_HEIGHT)+UI_HSTEP, x+((iCol+1)*GRID_COL_WIDTH)+UI_HSTEP-3, y+((iRow+1)*GRID_ROW_HEIGHT)+UI_HSTEP-3);

			// Draw the capsule
			ui_DrawCapsule(&cellBox, z+0.2, (color), 1);

			// Draw selection/hover highlight
			if ((i == pGrid->iSelected) || (i == pGrid->iHovered))
			{
				Color highlight = { 255, 255, 255, 255 };
				CBoxSet(&cellBox, cellBox.left-2, cellBox.top-2, cellBox.right+2, cellBox.bottom+2);
				ui_DrawCapsule(&cellBox, z+0.1, highlight, 1);
			}

			// Move to next position
			++iCol;
			if (iCol >= pGrid->iNumPerRow)
			{
				iCol = 0;
				++iRow;
			}
		}
	}
}

void ui_PaletteGridTick(UIPaletteGrid *pGrid, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pGrid);
	int iOldHovered = pGrid->iHovered;

	UI_TICK_EARLY(pGrid, false, true);

	// Figure out cell hovered over
	pGrid->iHovered = -1;
	if (pGrid->pColorSet)
	{
		F32 fMouseX, fMouseY;
		int iRow, iCol, iIndex;

		// Find mouse position
		fMouseX = (g_ui_State.mouseX - x);
		fMouseY = (g_ui_State.mouseY - y);

		if ((fMouseX >= 0) && (fMouseX <= w) && (fMouseY >=0) && (fMouseY <= h))
		{
			// Find proper entry that is being hovered
			iCol = (fMouseX - UI_HSTEP + 1) / GRID_COL_WIDTH;
			if ((iCol >= 0) && (iCol <= pGrid->iNumPerRow))
			{
				iRow = (fMouseY - UI_HSTEP + 1) / GRID_ROW_HEIGHT;
				if (iRow >= 0)
				{
					iIndex = (iRow * pGrid->iNumPerRow) + iCol;
					if (iIndex < eaSize(&pGrid->pColorSet->eaColors))
					{
						// Set hover
						pGrid->iHovered = iIndex;

						// Call hover function every tick
						if (pGrid->hoverF) 
						{
							(pGrid->hoverF)(pGrid, true, pGrid->pColorSet->eaColors[iIndex]->color, pGrid->hoverData);
						}

						// If also got click, then set current color
						if (mouseClick(MS_LEFT)) 
						{
							ui_PaletteGridSetColorAndCallback(pGrid, pGrid->pColorSet->eaColors[iIndex]->color);
						}
					}
				}
			}
		}
	}
	if (pGrid->hoverF && (iOldHovered >= 0) && (pGrid->iHovered == -1)) 
	{
		(pGrid->hoverF)(pGrid, false, vZero, pGrid->hoverData);
	}

	UI_TICK_LATE(pGrid);
}

static void ui_PaletteGridFindColor(UIPaletteGrid *pGrid)
{
	// Find the index of the selected color
	if (pGrid->pColorSet) 
	{
		int i;

		for(i=eaSize(&pGrid->pColorSet->eaColors)-1; i>=0; --i) 
		{
			if (sameVec4(pGrid->vColor, pGrid->pColorSet->eaColors[i]->color))
			{
				pGrid->iSelected = i;
				return;
			}
		}
	}
	pGrid->iSelected = -1;
}

UIPaletteGrid *ui_PaletteGridCreate(F32 x, F32 y, UIColorSet *pColorSet, int iNumPerRow, const Vec4 color)
{
	UIPaletteGrid *pGrid = (UIPaletteGrid *)calloc(1, sizeof(UIPaletteGrid));
	ui_PaletteGridInitialize(pGrid, x, y, pColorSet, iNumPerRow, color);
	return pGrid;
}

void ui_PaletteGridFreeInternal(UIPaletteGrid *pGrid)
{
	ui_WidgetFreeInternal(UI_WIDGET(pGrid));
}

void ui_PaletteGridInitialize(UIPaletteGrid *pGrid, F32 x, F32 y, UIColorSet *pColorSet, int iNumPerRow, const Vec4 color)
{
	ui_WidgetInitialize(UI_WIDGET(pGrid), ui_PaletteGridTick, ui_PaletteGridDraw, ui_PaletteGridFreeInternal, NULL, NULL);
	ui_WidgetSetDimensions(UI_WIDGET(pGrid), 64, 32 + UI_STEP);
	ui_WidgetSetPosition(UI_WIDGET(pGrid), x, y);
	pGrid->pColorSet = pColorSet;
	pGrid->iNumPerRow = iNumPerRow;
	pGrid->iHovered = -1;
	pGrid->widget.drawF = ui_PaletteGridDraw;
	pGrid->widget.freeF = ui_PaletteGridFreeInternal;
	pGrid->widget.tickF = ui_PaletteGridTick;
	ui_PaletteGridSetColor(pGrid, color);
}

void ui_PaletteGridGetColor(UIPaletteGrid *pGrid, Vec4 color)
{
	copyVec4(pGrid->vColor, color);
}

void ui_PaletteGridSetColor(UIPaletteGrid *pGrid, const Vec4 color) 
{
	copyVec4(color, pGrid->vColor);
	ui_PaletteGridFindColor(pGrid);
}

void ui_PaletteGridSetColorAndCallback(UIPaletteGrid *pGrid, const Vec4 color)
{
	ui_PaletteGridSetColor(pGrid, color);
	if (pGrid->changedF)
		pGrid->changedF(pGrid, pGrid->changedData);
}

void ui_PaletteGridSetChangedCallback(UIPaletteGrid *pGrid, UIActivationFunc changedF, UserData changedData)
{
	pGrid->changedF = changedF;
	pGrid->changedData = changedData;
}

void ui_PaletteGridSetHoverCallback(UIPaletteGrid *pGrid, UIColorHoverFunc hoverF, UserData hoverData)
{
	pGrid->hoverF = hoverF;
	pGrid->hoverData = hoverData;
}

void ui_PaletteGridSetColorSet(SA_PARAM_NN_VALID UIPaletteGrid *pGrid, UIColorSet *pColorSet)
{
	pGrid->pColorSet = pColorSet;
	ui_PaletteGridFindColor(pGrid);
}

void ui_PaletteGridSetNumPerRow(SA_PARAM_NN_VALID UIPaletteGrid *pGrid, int iNumPerRow)
{
	pGrid->iNumPerRow = iNumPerRow;
}


// -------------

void ui_ColorComboDraw(UIColorCombo *pCombo, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pCombo);
	Color color;

	color.r = CLAMP(pCombo->vColor[0], 0, 255);
	color.g = CLAMP(pCombo->vColor[1], 0, 255);
	color.b = CLAMP(pCombo->vColor[2], 0, 255);
	color.a = 255;

	CBoxClipTo(&pBox, &box);
	if( !ui_IsActive( UI_WIDGET( pCombo ))) {
		Color4 tint = { -1, -1, -1, -1 };
		ui_TextureAssemblyDraw( GET_REF(g_ui_Tex.hCapsuleDisabledAssembly), &box, NULL, scale, z, z + 0.001, 255, &tint );
	} else {
		ui_DrawCapsule(&box, z, color, scale);
	}

	ui_WidgetGroupDraw(&pCombo->widget.children, UI_MY_VALUES);
}

static void ui_ColorComboClose(UIColorCombo *pCombo)
{
	// unattach from top to hide it
	ui_WidgetRemoveFromGroup((UIWidget*)pCombo->pGrid);
	pCombo->bOpened = false;
}

// Callback on popup's tick so we can dismiss the window on unhandled mouse input
static void ui_ColorComboGridTick(UIPaletteGrid *pGrid, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pGrid);

	// First pass to list
	ui_PaletteGridTick(pGrid, pX, pY, pW, pH, pScale);

	printf( "mouseDown(MS_LEFT)=%d, mouseDownHit(MS_LEFT)=%d\n",
			mouseDown( MS_LEFT ), mouseDownHit(MS_LEFT, &box));

	// If unhandled mouse action outside the combo parent, then hide
	if (  (UI_WIDGET(pGrid)->group && eaGet(UI_WIDGET(pGrid)->group, 0) != UI_WIDGET(pGrid))
		  || (mouseDown(MS_LEFT) && !mouseDownHit(MS_LEFT, &box)))
	{
		((UIColorCombo*)pGrid->changedData)->bCloseOnNextTick = true;
		ui_ColorComboClose((UIColorCombo*)pGrid->changedData);
	}
}

int ui_ColorComboActualNumPerRow(UIColorCombo *pCombo)
{
	if (pCombo->iNumPerRow)
	{
		return pCombo->iNumPerRow;
	}
	if (pCombo->pColorSet)
	{
		int size = eaSize(&pCombo->pColorSet->eaColors);
		if (pCombo->pColorSet->rowSize) {
			return pCombo->pColorSet->rowSize;
		}
		if (size <= 32)
		{
			return 8;
		}
		if (size <= 72)
		{
			return 12;
		}
		if (size <= 128)
		{
			return 16;
		}
		return 24;
	}
	return 8;
}

void ui_ColorComboTick(UIColorCombo *pCombo, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pCombo);
	UI_TICK_EARLY(pCombo, false, true);

	ui_ButtonTick(UI_BUTTON(pCombo), UI_PARENT_VALUES);

	if (pCombo->bOpened && pCombo->pColorSet)
	{
		UIPaletteGrid *pGrid = pCombo->pGrid;
		F32 fSize = eaSize(&pCombo->pColorSet->eaColors);
		F32 fPopupHeight = (ceilf(fSize / pGrid->iNumPerRow) * GRID_ROW_HEIGHT) + UI_HSTEP + 1;
		F32 fPopupWidth = (pGrid->iNumPerRow * GRID_COL_WIDTH) + UI_HSTEP + 1;
		F32 fPopupX = x;
		F32 fPopupY = y + h;

		// Check for vertical re-arrangement
		if (fPopupY + fPopupHeight >= g_ui_State.screenHeight)
		{
			if (y - fPopupHeight - 1 >= 0)
				fPopupY = y - fPopupHeight - 1;
		}

		// Check for horizontal re-arrangement
		if (fPopupX + fPopupWidth >= g_ui_State.screenWidth)
		{
			fPopupX = g_ui_State.screenWidth - fPopupWidth - 1;
		}
		if (fPopupX < 0) 
		{
			fPopupX = 0;
		}

		// Set position of popup
 		ui_WidgetSetPosition(UI_WIDGET(pGrid), fPopupX/g_ui_State.scale, fPopupY/g_ui_State.scale);
 		ui_WidgetSetWidth(UI_WIDGET(pGrid), fPopupWidth/pScale);
 		ui_WidgetSetHeight(UI_WIDGET(pGrid), fPopupHeight/pScale);
 		UI_WIDGET(pGrid)->scale = pScale / g_ui_State.scale;
	}

	if (pCombo->bCloseOnNextTick) 
	{
		pCombo->bCloseOnNextTick = false;
		if (!mouseDown(MS_LEFT) || !mouseDownHit(MS_LEFT, &box)) // Don't close on mouse down on this button
		{
			ui_ColorComboClose(pCombo);
		}
	}

	UI_TICK_LATE(pCombo);
}

static void ui_ColorComboGridChangeCB(UIPaletteGrid *palette, UIColorCombo *pCombo)
{
	Vec4 vColor;

	// Collect color information
	ui_PaletteGridGetColor(pCombo->pGrid, vColor);

	// Close up
	ui_ColorComboClose(pCombo);

	// Set color and trigger callback
	ui_ColorComboSetColorAndCallback(pCombo, vColor);
}

static void ui_ColorComboGridHoverCB(UIPaletteGrid *palette, bool bIsHover, Vec4 color, UIColorCombo *pCombo)
{
	if (pCombo->hoverF)
		pCombo->hoverF(pCombo, bIsHover, color, pCombo->hoverData);
}

void ui_ColorComboClick(UIColorCombo *pCombo, UserData dummy)
{
	if (pCombo->bOpened) 
	{
		// Need to close
		ui_ColorComboClose(pCombo);
	} 
	else 
	{
		// Need to open
		if (!pCombo->pGrid) {
			pCombo->pGrid = ui_PaletteGridCreate(0, 0, pCombo->pColorSet, ui_ColorComboActualNumPerRow(pCombo), pCombo->vColor);
		}

		pCombo->pGrid->widget.tickF = ui_ColorComboGridTick;
		ui_PaletteGridSetColor(pCombo->pGrid, pCombo->vColor);
		ui_PaletteGridSetChangedCallback(pCombo->pGrid, ui_ColorComboGridChangeCB, pCombo);
		if (pCombo->hoverF) {
			ui_PaletteGridSetHoverCallback(pCombo->pGrid, ui_ColorComboGridHoverCB, pCombo);
		}

		if (UI_GET_SKIN(pCombo))
		{
			ui_WidgetSkin(UI_WIDGET(pCombo->pGrid), UI_GET_SKIN(pCombo));
		}

		// Add to priority widgets
		UI_WIDGET(pCombo->pGrid)->priority = UI_HIGHEST_PRIORITY;
		ui_TopWidgetAddToDevice(UI_WIDGET(pCombo->pGrid), NULL);

		pCombo->bOpened = true;
	}
}

UIColorCombo *ui_ColorComboCreate(F32 x, F32 y, UIColorSet *pColorSet, const Vec4 initial)
{
	UIColorCombo *pCombo = (UIColorCombo *)calloc(1, sizeof(UIColorCombo));
	ui_ColorComboInitialize(pCombo, x, y, pColorSet, initial);
	return pCombo;
}

void ui_ColorComboFreeInternal(UIColorCombo *pCombo)
{
	if( pCombo->pGrid ) {
		ui_PaletteGridFreeInternal(pCombo->pGrid);
	}
	ui_ButtonFreeInternal(UI_BUTTON(pCombo));
}

void ui_ColorComboInitialize(UIColorCombo *pCombo, F32 x, F32 y, UIColorSet *pColorSet, const Vec4 color)
{
	ui_ButtonInitialize(&pCombo->button, NULL, x, y, ui_ColorComboClick, NULL MEM_DBG_PARMS_INIT);
	ui_WidgetSetDimensions(UI_WIDGET(pCombo), 64, 32 + UI_STEP);
	ui_WidgetSetPosition(UI_WIDGET(pCombo), x, y);
	pCombo->pColorSet = pColorSet;
	pCombo->widget.drawF = ui_ColorComboDraw;
	pCombo->widget.freeF = ui_ColorComboFreeInternal;
	pCombo->widget.tickF = ui_ColorComboTick;
	ui_ColorComboSetColor(pCombo, color);
}

void ui_ColorComboGetColor(UIColorCombo *pCombo, Vec4 color)
{
	copyVec4(pCombo->vColor, color);
}

void ui_ColorComboSetColor(UIColorCombo *pCombo, const Vec4 color) 
{
	copyVec4(color, pCombo->vColor);
}

void ui_ColorComboSetColorAndCallback(UIColorCombo *pCombo, const Vec4 color)
{
	ui_ColorComboSetColor(pCombo, color);
	if (pCombo->changedF)
		pCombo->changedF(pCombo, pCombo->changedData);
}

void ui_ColorComboSetChangedCallback(UIColorCombo *pCombo, UIActivationFunc changedF, UserData changedData)
{
	pCombo->changedF = changedF;
	pCombo->changedData = changedData;
}

void ui_ColorComboSetHoverCallback(UIColorCombo *pCombo, UIColorHoverFunc hoverF, UserData hoverData)
{
	pCombo->hoverF = hoverF;
	pCombo->hoverData = hoverData;

	// Update grid hover callback
	if (pCombo->pGrid) {
		if (hoverF) {
			ui_PaletteGridSetHoverCallback(pCombo->pGrid, ui_ColorComboGridHoverCB, pCombo);
		} else {
			ui_PaletteGridSetHoverCallback(pCombo->pGrid, NULL, NULL);
		}
	}
}

void ui_ColorComboSetColorSet(SA_PARAM_NN_VALID UIColorCombo *pCombo, UIColorSet *pColorSet)
{
	pCombo->pColorSet = pColorSet;
}

void ui_ColorComboSetNumPerRow(SA_PARAM_NN_VALID UIColorCombo *pCombo, int iNumPerRow)
{
	pCombo->iNumPerRow = iNumPerRow;
}

