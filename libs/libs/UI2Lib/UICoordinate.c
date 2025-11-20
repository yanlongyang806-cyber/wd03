/***************************************************************************



***************************************************************************/


#include "GfxClipper.h"
#include "GfxSprite.h"
#include "inputMouse.h"

#include "UICoordinate.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

UICoordinate *ui_CoordinateCreate(void)
{
	UICoordinate *pCoord = calloc(1, sizeof(UICoordinate));
	ui_CoordinateInitialize(pCoord);
	return pCoord;
}

void ui_CoordinateInitialize(UICoordinate *pCoord)
{
	ui_WidgetInitialize(UI_WIDGET(pCoord), ui_CoordinateTick, ui_CoordinateDraw, ui_CoordinateFreeInternal, NULL, NULL);
	pCoord->fX = 0.5f;
	pCoord->fY = 0.5f;
}

void ui_CoordinateTick(UICoordinate *pCoord, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pCoord);
	UI_TICK_EARLY(pCoord, false, true);

	if (mouseIsDown(MS_LEFT) && mouseCollision(&box))
	{
		F32 fX = (g_ui_State.mouseX - x) / w;
		F32 fY = (g_ui_State.mouseY - y) / h;

		if (fX != pCoord->fX || fY != pCoord->fY)
			ui_CoordinateSetLocationAndCallback(pCoord, fX, fY);
	}
	if (mouseDoubleClick(MS_LEFT) && mouseCollision(&box))
	{
		F32 fX = (g_ui_State.mouseX - x) / w;
		F32 fY = (g_ui_State.mouseY - y) / h;
		if ( pCoord->doubleClickF )
			pCoord->doubleClickF(pCoord, fX, fY, pCoord->doubleClickData);
	}

	UI_TICK_LATE(pCoord);
}
void ui_CoordinateDraw(UICoordinate *pCoord, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pCoord);
	CBox locationBox = {0, 0, 5 * scale, 5 * scale};
	UI_DRAW_EARLY(pCoord);

	if (pCoord->bDrawCenter)
	{
		CBoxSetCenter(&locationBox, x + w / 2, y + h / 2);
		display_sprite_box((g_ui_Tex.white), &locationBox, z + 0.1, 0x77);
	}

	CBoxSetCenter(&locationBox, x + pCoord->fX  * w, y + pCoord->fY * h);
	display_sprite_box((g_ui_Tex.white), &locationBox, z + 0.2, 0xFF);

	UI_DRAW_LATE(pCoord);
}

void ui_CoordinateFreeInternal(UICoordinate *pCoord)
{
	ui_WidgetFreeInternal(UI_WIDGET(pCoord));
}

void ui_CoordinateDrawCenter(UICoordinate *pCoord, bool bDrawCenter)
{
	pCoord->bDrawCenter = bDrawCenter;
}

void ui_CoordinateGetLocation(UICoordinate *pCoord, F32 *pfX, F32 *pfY)
{
	if (pfX)
		*pfX = pCoord->fX;
	if (pfY)
		*pfY = pCoord->fY;
}

void ui_CoordinateSetLocation(UICoordinate *pCoord, F32 fX, F32 fY)
{
	pCoord->fX = CLAMP(fX, 0.f, 1.f);
	pCoord->fY = CLAMP(fY, 0.f, 1.f);
}

void ui_CoordinateSetLocationAndCallback(UICoordinate *pCoord, F32 fX, F32 fY)
{
	ui_CoordinateSetLocation(pCoord, fX, fY);
	if (pCoord->movedF)
		pCoord->movedF(pCoord, pCoord->movedData);
}

void ui_CoordinateSetMovedCallback(UICoordinate *pCoord, UIActivationFunc movedF, UserData movedData)
{
	pCoord->movedF = movedF;
	pCoord->movedData = movedData;
}

void ui_CoordinateSetDoubleClickCallback(SA_PARAM_NN_VALID UICoordinate *pCoord, UICoordinateDoubleClickCallback doubleClickF, UserData doubleClickData)
{
	pCoord->doubleClickF = doubleClickF;
	pCoord->doubleClickData = doubleClickData;
}
