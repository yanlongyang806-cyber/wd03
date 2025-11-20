#include "textparser.h"

#include "inputMouse.h"
#include "inputLib.h"

#include "GfxClipper.h"
#include "GfxPrimitive.h"
#include "GfxSprite.h"
#include "GfxSpriteText.h"

#include "UIGraph.h"
#include "UITextEntry.h"
#include "UISpinner.h"
#include "UILabel.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static void ui_GraphSetMarginAndCallback(UIGraph* pGraph, UIGraphPoint* pPoint, F32 fMargin);

UIGraphPoint *ui_GraphPointCreate(Vec2 v2Point, F32 fMargin)
{
	UIGraphPoint *pPoint = calloc(1, sizeof(UIGraphPoint));
	copyVec2(v2Point, pPoint->v2Position);
	pPoint->fMargin = fMargin;
	return pPoint;
}

void ui_GraphPointFree(UIGraphPoint *pPoint)
{
	free(pPoint);
}

UIGraph *ui_GraphCreate(const char *pchLabelX, const char *pchLabelY, const Vec2 v2Lower, const Vec2 v2Upper, U8 chMaxPoints, bool bMargins)
{
	UIGraph *pGraph = calloc(1, sizeof(UIGraph));
	ui_WidgetInitialize(UI_WIDGET(pGraph), ui_GraphTick, ui_GraphDraw, ui_GraphFreeInternal, NULL, NULL);
	ui_GraphSetBounds(pGraph, v2Lower, v2Upper);
	ui_GraphSetMaxPoints(pGraph, chMaxPoints);
	ui_GraphSetLabels(pGraph, pchLabelX, pchLabelY);
	ui_GraphSetMargins(pGraph, bMargins);
	return pGraph;
}

void ui_GraphFreeInternal(UIGraph *pGraph)
{
	eaDestroyEx(&pGraph->eaPoints, ui_GraphPointFree);
	ui_WidgetFreeInternal(UI_WIDGET(pGraph));
}

// Convert fX and fY from the box coordinates to the graph coordinates.
// Slightly tricky since the Y coordinate is inverted.
void ui_GraphToGraphCoords(UIGraph *pGraph, CBox *pBox, F32 fX, F32 fY, F32 *pfOutX, F32 *pfOutY)
{
	if (pfOutX)
	{
		F32 fPercentX = (fX - pBox->lx) / CBoxWidth(pBox);
		*pfOutX = fPercentX * (pGraph->v2UpperBound[0] - pGraph->v2LowerBound[0]) + pGraph->v2LowerBound[0];
	}
	if (pfOutY)
	{
		F32 fPercentY = (fY - pBox->ly) / CBoxHeight(pBox);
		*pfOutY = pGraph->v2UpperBound[1] - fPercentY * (pGraph->v2UpperBound[1] - pGraph->v2LowerBound[1]);
	}
}

// Convert fX and fY from graph coordinates to the box coordinates.
// Slightly tricky since the Y coordinate is inverted.
void ui_GraphToBoxCoords(UIGraph *pGraph, CBox *pBox, F32 fX, F32 fY, F32 *pfOutX, F32 *pfOutY)
{
	if (pfOutX)
	{
		F32 fPercentX = (fX - pGraph->v2LowerBound[0]) / (pGraph->v2UpperBound[0] - pGraph->v2LowerBound[0]);
		*pfOutX = fPercentX * CBoxWidth(pBox) + pBox->lx;
	}
	if (pfOutY)
	{
		F32 fPercentY = (fY - pGraph->v2LowerBound[1]) / (pGraph->v2UpperBound[1] - pGraph->v2LowerBound[1]);
		*pfOutY = pBox->hy - fPercentY * CBoxHeight(pBox);
	}
}

#define UI_GRAPH_AXIS_MARGIN (32.f * scale)
#define UI_GRAPH_MARGIN_WIDTH (6.f)

void ui_GraphTick(UIGraph *pGraph, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pGraph);
	F32 fAxisMargin = UI_GRAPH_AXIS_MARGIN;
	CBox graphBox = {x + fAxisMargin, y + fAxisMargin, x + w - fAxisMargin, y + h - fAxisMargin};
	CBox pointBox = {0, 0, UI_GRAPH_MARGIN_WIDTH, UI_GRAPH_MARGIN_WIDTH};
	S32 i;

	UI_TICK_EARLY(pGraph, false, true);
	for (i = 0; i < eaSize(&pGraph->eaPoints); i++)
	{
		UIGraphPoint *pPoint = pGraph->eaPoints[i];
		F32 fPointCenterX, fPointCenterY;
		CBox marginBox;
		ui_GraphToBoxCoords(pGraph, &graphBox, pPoint->v2Position[0], pPoint->v2Position[1], &fPointCenterX, &fPointCenterY);
		CBoxSetCenter(&pointBox, fPointCenterX, fPointCenterY);

		marginBox.lx = pointBox.lx;
		marginBox.hx = pointBox.hx;
		ui_GraphToBoxCoords(pGraph, &graphBox, pPoint->v2Position[0], pPoint->v2Position[1] + pPoint->fMargin, NULL, &marginBox.ly);
		ui_GraphToBoxCoords(pGraph, &graphBox, pPoint->v2Position[0], pPoint->v2Position[1] - pPoint->fMargin, NULL, &marginBox.hy);
		if (mouseDownHit(MS_LEFT, &pointBox))
		{
			if (inpLevelPeek(INP_SHIFT) && pGraph->bMargins)
			{
				pGraph->bDraggingMargin = true;
				pGraph->chDraggingPoint = i;
			}
			else if (inpLevelPeek(INP_CONTROL))
			{
				pPoint->bSelected = !pPoint->bSelected;
			}
			else
			{
				if (!pGraph->bDragging && pPoint->bSelected)
				{
					FOR_EACH_IN_EARRAY(pGraph->eaPoints, UIGraphPoint, pSelectedPoint)
					{
						subVec2(pSelectedPoint->v2Position, pPoint->v2Position, pSelectedPoint->v2DraggingDiff);
					}
					FOR_EACH_END;
				}

				pGraph->bDragging = true;
				pGraph->chDraggingPoint = i;
			}
			ui_SetFocus(pGraph);
			inpHandled();
			break;
		}
		else if (mouseDownHit(MS_LEFT, &marginBox) && pGraph->bMargins)
		{
			F32 fY;
			ui_GraphToGraphCoords(pGraph, &graphBox, g_ui_State.mouseX, g_ui_State.mouseY, NULL, &fY);
			pGraph->bDraggingMargin = true;
			pGraph->chDraggingPoint = i;
			ui_SetFocus(pGraph);
			inpHandled();
		}
		else if (mouseClickHit(MS_RIGHT, &pointBox) && eaSize(&pGraph->eaPoints) > pGraph->chMinPoints)
		{
			if (inpLevelPeek(INP_SHIFT))
			{
				if (pPoint->bSelected)
				{
					FOR_EACH_IN_EARRAY(pGraph->eaPoints, UIGraphPoint, pSelectedPoint)
					{
						if (pSelectedPoint->bSelected)
							ui_GraphSetMarginAndCallback(pGraph, pSelectedPoint, 0.0f);

					}
					FOR_EACH_END;
				}
				else
					ui_GraphSetMarginAndCallback(pGraph, pPoint, 0.0f);
			}
			else
			{
				ui_GraphRemovePointAndCallback(pGraph, pPoint);
				ui_GraphPointFree(pPoint);
			}
			ui_SetFocus(pGraph);
			inpHandled();
			break;
		}
		else if (mouseCollision(&pointBox))
		{
			if (inpLevelPeek(INP_SHIFT) && pGraph->bMargins)
				ui_SetCursorByName("Resize_Margin");
			else if (inpLevelPeek(INP_CONTROL) && pGraph->bMargins)
				ui_SetCursorByName("Resize_Selection");
			else if (inpLevelPeek(INP_ALT) && pGraph->bMargins)
				ui_SetCursorForDirection(UITop);
			else
				ui_SetCursorForDirection(UIAnyDirection);
			ui_CursorLock();
		}
		else if (mouseCollision(&marginBox) && pGraph->bMargins)
		{
			ui_SetCursorByName("Resize_Margin");
			ui_CursorLock();
		}
	}

	if (mouseDownHit(MS_LEFT, &graphBox) && !inpLevelPeek(INP_SHIFT) && !inpLevelPeek(INP_CONTROL))
	{
		S32 iMouseX, iMouseY;
		mouseDownPos(MS_LEFT, &iMouseX, &iMouseY);

		if (iMouseX >= 0 && iMouseY >= 0)
		{
			UIGraphPoint *pPoint = NULL;
			Vec2 v2Pos;
			ui_GraphToGraphCoords(pGraph, &graphBox, iMouseX, iMouseY, &v2Pos[0], &v2Pos[1]);
			// If we can still make new points, make a new one -- otherwise, try
			// to find the closest existing one and move it.
			if (eaSize(&pGraph->eaPoints) < pGraph->chMaxPoints || pGraph->chMaxPoints == 0)
				pPoint = ui_GraphPointCreate(v2Pos, 0);
			else
			{
				F32 fBestDist = FLT_MAX;
				for (i = 0; i < eaSize(&pGraph->eaPoints); i++)
				{
					F32 fDist = fabs(v2Pos[0] - pGraph->eaPoints[i]->v2Position[0]);
					if (fDist < fBestDist)
					{
						pPoint = pGraph->eaPoints[i];
						fBestDist = fDist;
					}					
				}
			}
			if (pPoint)
			{
				ui_GraphAddPoint(pGraph, pPoint);
				pGraph->chDraggingPoint = eaFind(&pGraph->eaPoints, pPoint);
				pGraph->bDragging = true;
				if (pPoint->bSelected)
				{
					FOR_EACH_IN_EARRAY(pGraph->eaPoints, UIGraphPoint, pSelectedPoint)
					{
						subVec2(pSelectedPoint->v2Position, pPoint->v2Position, pSelectedPoint->v2DraggingDiff);
					}
					FOR_EACH_END;
				}

			}
			inpHandled();
		}
		ui_SetFocus(pGraph);
	}

	if (!mouseIsDown(MS_LEFT))
	{
		if ((pGraph->bDragging || pGraph->bDraggingMargin) && pGraph->cbChanged)
			pGraph->cbChanged(pGraph, pGraph->pChangedData);
		pGraph->bDragging = false;
		pGraph->bDraggingMargin = false;
	}

	if (pGraph->bDragging)
	{
		UIGraphPoint *pPoint;
		Vec2 v2Pos;
		ui_GraphToGraphCoords(pGraph, &graphBox, g_ui_State.mouseX, g_ui_State.mouseY, &v2Pos[0], &v2Pos[1]);
		pPoint = ui_GraphGetPoint(pGraph, pGraph->chDraggingPoint);
		if (pPoint && pPoint->bSelected)
		{
			FOR_EACH_IN_EARRAY(pGraph->eaPoints, UIGraphPoint, pSelectedPoint)
			{
				if (pSelectedPoint->bSelected)
				{
					Vec2 v2New;
					addVec2(pSelectedPoint->v2DraggingDiff, v2Pos, v2New);

					if (ipSelectedPointIndex == 0 && pGraph->bLockToX0)
						v2New[0] = pGraph->v2LowerBound[0];

					if (inpLevelPeek(INP_ALT))
					{
						v2New[0] = pSelectedPoint->v2Position[0];
					}

					ui_GraphMovePoint(pGraph, pSelectedPoint, v2New, pSelectedPoint->fMargin);
					if (pGraph->cbDragging)
						pGraph->cbDragging(pGraph, pGraph->pDraggingData);
				}
			}
			FOR_EACH_END;
		}
		else
		{
			if (pGraph->chDraggingPoint == 0 && pGraph->bLockToX0)
				v2Pos[0] = pGraph->v2LowerBound[0];
			if (pPoint)
			{
				if (inpLevelPeek(INP_ALT))
				{
					v2Pos[0] = pPoint->v2Position[0];
				}
				ui_GraphMovePoint(pGraph, pPoint, v2Pos, pPoint->fMargin);
				if (pGraph->cbDragging)
					pGraph->cbDragging(pGraph, pGraph->pDraggingData);
			}
		}
		if (inpLevelPeek(INP_ALT))
			ui_SetCursorForDirection(UITop);
		else
			ui_SetCursorForDirection(UIAnyDirection);
		ui_CursorLock();
		inpHandled();
	}
	else if (pGraph->bDraggingMargin)
	{
		UIGraphPoint *pPoint;
		F32 fY;
		ui_GraphToGraphCoords(pGraph, &graphBox, g_ui_State.mouseX, g_ui_State.mouseY, NULL, &fY);
		pPoint = ui_GraphGetPoint(pGraph, pGraph->chDraggingPoint);
		if (pPoint && pPoint->bSelected)
		{
			FOR_EACH_IN_EARRAY(pGraph->eaPoints, UIGraphPoint, pSelectedPoint)
			{
				if (pSelectedPoint->bSelected)
				{
					ui_GraphMovePoint(pGraph, pSelectedPoint, pSelectedPoint->v2Position, fabs(pPoint->v2Position[1] - fY));
					if (pGraph->cbDragging)
						pGraph->cbDragging(pGraph, pGraph->pDraggingData);
				}
			}
			FOR_EACH_END;
		}
		else if (pPoint)
		{
			ui_GraphMovePoint(pGraph, pPoint, pPoint->v2Position, fabs(pPoint->v2Position[1] - fY));
			if (pGraph->cbDragging)
				pGraph->cbDragging(pGraph, pGraph->pDraggingData);
		}
		ui_SetCursorByName("Resize_Margin");
		ui_CursorLock();
		inpHandled();
	}

	UI_TICK_LATE(pGraph);
}

void ui_GraphDraw(UIGraph *pGraph, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pGraph);
	UIStyleFont *pFont = GET_REF(UI_GET_SKIN(pGraph)->hNormal);
	Color baseColor = ui_StyleFontGetColor(pFont);
	Color selectedColor = ColorRed;
	U32 rgbaSelectedColor = 0xFF0000FF;
	F32 fAxisMargin = UI_GRAPH_AXIS_MARGIN;
	CBox graphBox = {x + fAxisMargin, y + fAxisMargin, x + w - fAxisMargin, y + h - fAxisMargin};
	CBox pointBox = {0, 0, 4, 4};
	F32 fLastX = -1, fLastY = -1;
	S32 i;
	UIGraphPoint *pPoint = NULL;

	UI_DRAW_EARLY(pGraph);

	if (pGraph->pchLabelY)
		gfxfont_Printf(x + UI_HSTEP_SC, y + UI_GRAPH_AXIS_MARGIN / 2, z, scale, scale, CENTER_Y, "%s", pGraph->pchLabelY);
	if (pGraph->pchLabelX)
		gfxfont_Printf(x + w / 2, y + h - UI_GRAPH_AXIS_MARGIN / 2, z, scale, scale, CENTER_XY, "%s", pGraph->pchLabelX);

	gfxDrawLine(graphBox.lx, graphBox.ly, z, graphBox.lx, graphBox.hy, baseColor);
	gfxDrawLine(graphBox.lx, graphBox.hy, z, graphBox.hx, graphBox.hy, baseColor);

	for (i = 0; i < eaSize(&pGraph->eaPoints); i++)
	{
		AtlasTex *pWhite = (g_ui_Tex.white);
		UIGraphPoint *pPrevPoint = i>0?pGraph->eaPoints[i-1]:NULL;
		F32 fPointCenterX, fPointCenterY;
		pPoint = pGraph->eaPoints[i];
		ui_GraphToBoxCoords(pGraph, &graphBox, pPoint->v2Position[0], pPoint->v2Position[1], &fPointCenterX, &fPointCenterY);
		CBoxSetCenter(&pointBox, fPointCenterX, fPointCenterY);
		display_sprite_box(pWhite, &pointBox, z + 0.1, RGBAFromColor(pPoint->bSelected?selectedColor:baseColor));

		if (pPoint->fMargin)
		{
			Vec2 v2Top;
			Vec2 v2Bottom;
			Color marginColor = pPoint->bSelected?selectedColor:baseColor;
			ui_GraphToBoxCoords(pGraph, &graphBox, pPoint->v2Position[0], pPoint->v2Position[1] + pPoint->fMargin, &v2Top[0], &v2Top[1]);
			ui_GraphToBoxCoords(pGraph, &graphBox, pPoint->v2Position[0], pPoint->v2Position[1] - pPoint->fMargin, &v2Bottom[0], &v2Bottom[1]);

			marginColor.a *= 0.75;
			gfxDrawLine(v2Top[0], v2Top[1], z, v2Bottom[0], v2Bottom[1], marginColor);
			gfxDrawLine(v2Top[0] - UI_GRAPH_MARGIN_WIDTH / 2, v2Top[1], z, v2Top[0] + UI_GRAPH_MARGIN_WIDTH / 2 + 1, v2Top[1], marginColor);
			gfxDrawLine(v2Bottom[0] - UI_GRAPH_MARGIN_WIDTH / 2, v2Bottom[1], z, v2Bottom[0] + UI_GRAPH_MARGIN_WIDTH / 2 + 1, v2Bottom[1], marginColor);
		}


		if (pGraph->bDrawConnection && i > 0)
			gfxDrawLine2(fPointCenterX, fPointCenterY, z, fLastX, fLastY, pPoint->bSelected?selectedColor:baseColor, pPrevPoint->bSelected?selectedColor:baseColor);
		fLastX = fPointCenterX;
		fLastY = fPointCenterY;
	}

	if (pGraph->bConnectToEnd && fLastX >= 0 && fLastY >= 0 && pPoint)
		gfxDrawLine(fLastX, fLastY, z, graphBox.hx, fLastY, pPoint->bSelected?selectedColor:baseColor);

	if (pGraph->bDrawScale)
	{
		F32 fSmallScale = 0.75 * scale;
		gfxfont_Printf(x + UI_GRAPH_AXIS_MARGIN / 2, graphBox.ly, z, fSmallScale, fSmallScale * scale, CENTER_XY, "%.1f", (double)pGraph->v2UpperBound[1]);
		gfxfont_Printf(x + UI_GRAPH_AXIS_MARGIN / 2, graphBox.hy, z, fSmallScale, fSmallScale * scale, CENTER_XY, "%.1f", (double)pGraph->v2LowerBound[1]);

		gfxfont_Printf(graphBox.lx, graphBox.hy + UI_GRAPH_AXIS_MARGIN / 2, z, fSmallScale, fSmallScale, CENTER_XY, "%.1f", (double)pGraph->v2LowerBound[0]);
		gfxfont_Printf(graphBox.hx, graphBox.hy + UI_GRAPH_AXIS_MARGIN / 2, z, fSmallScale, fSmallScale, CENTER_XY, "%.1f", (double)pGraph->v2UpperBound[0]);
	}

	for (i = 0; i < pGraph->chResolutionX; i++)
	{
		F32 fX = graphBox.lx + (CBoxWidth(&graphBox) * (i + 1)) / pGraph->chResolutionX;
		gfxDrawLine(fX, graphBox.hy, z, fX, graphBox.hy + 5 * scale, baseColor);
	}
	for (i = 0; i < pGraph->chResolutionY; i++)
	{
		F32 fY = graphBox.hy - (CBoxHeight(&graphBox) * (i + 1)) / pGraph->chResolutionY;
		gfxDrawLine(graphBox.lx - 5 * scale, fY, z, graphBox.lx, fY, baseColor);
	}

	UI_DRAW_LATE(pGraph);
}

UIGraphPoint *ui_GraphGetPoint(UIGraph *pGraph, S32 iPoint)
{
	return eaGet(&pGraph->eaPoints, iPoint);
}

void ui_GraphAddPoint(UIGraph *pGraph, UIGraphPoint *pPoint)
{
	if (pGraph->chMaxPoints && eaSize(&pGraph->eaPoints) >= pGraph->chMaxPoints)
		return;
	if (eaSize(&pGraph->eaPoints) == 0 && pGraph->bLockToX0)
		pPoint->v2Position[0] = pGraph->v2LowerBound[0];
	eaPushUnique(&pGraph->eaPoints, pPoint);
	if (pGraph->bSort)
		ui_GraphSort(pGraph);
}

void ui_GraphAddPointAndCallback(UIGraph *pGraph, UIGraphPoint *pPoint)
{
	ui_GraphAddPoint(pGraph, pPoint);
	if (pGraph->cbChanged)
		pGraph->cbChanged(pGraph, pGraph->pChangedData);
}

void ui_GraphRemovePoint(UIGraph *pGraph, UIGraphPoint *pPoint)
{
	if (eaSize(&pGraph->eaPoints) > pGraph->chMinPoints)
		eaFindAndRemove(&pGraph->eaPoints, pPoint);
	if (pGraph->bLockToX0 && eaSize(&pGraph->eaPoints) > 0)
		pGraph->eaPoints[0]->v2Position[0] = pGraph->v2LowerBound[0];
}

void ui_GraphRemovePointAndCallback(UIGraph *pGraph, UIGraphPoint *pPoint)
{
	ui_GraphRemovePoint(pGraph, pPoint);
	if (pGraph->cbChanged)
		pGraph->cbChanged(pGraph, pGraph->pChangedData);
}

void ui_GraphRemovePointIndex(UIGraph *pGraph, S32 iPoint)
{
	UIGraphPoint *pPoint = ui_GraphGetPoint(pGraph, iPoint);
	if (pPoint)
		ui_GraphRemovePoint(pGraph, pPoint);
}

void ui_GraphRemovePointIndexAndCallback(UIGraph *pGraph, S32 iPoint)
{
	ui_GraphRemovePointIndex(pGraph, iPoint);
	if (pGraph->cbChanged)
		pGraph->cbChanged(pGraph, pGraph->pChangedData);
}

static void ui_GraphSetMarginAndCallback(UIGraph* pGraph, UIGraphPoint* pPoint, F32 fMargin)
{
	pPoint->fMargin = fMargin;
	if (pGraph->cbChanged)
		pGraph->cbChanged(pGraph, pGraph->pChangedData);
}

void ui_GraphMovePoint(UIGraph *pGraph, UIGraphPoint *pPoint, const Vec2 v2Position, F32 fMargin)
{
	copyVec2(v2Position, pPoint->v2Position);
	if (pPoint == eaGet(&pGraph->eaPoints, 0) && pGraph->bLockToX0)
		pPoint->v2Position[0] = pGraph->v2LowerBound[0];
	else if (pGraph->bLockToIndex)
	{
		S32 iIndex = eaFind(&pGraph->eaPoints, pPoint);
		UIGraphPoint *pPrevious = eaGet(&pGraph->eaPoints, iIndex - 1);
		UIGraphPoint *pNext = eaGet(&pGraph->eaPoints, iIndex + 1);
		if (pPrevious)
			MAX1(pPoint->v2Position[0], pPrevious->v2Position[0] + 0.0001);
		if (pNext)
			MIN1(pPoint->v2Position[0], pNext->v2Position[0] - 0.0001);
	}
	pPoint->fMargin = fMargin;
	if (pGraph->bSort)
		ui_GraphSort(pGraph);
	ui_GraphEnforceBounds(pGraph);
}

void ui_GraphMovePointAndCallback(UIGraph *pGraph, UIGraphPoint *pPoint, const Vec2 v2Position, F32 fMargin)
{
	ui_GraphMovePoint(pGraph, pPoint, v2Position, fMargin);
	if (pGraph->cbChanged)
		pGraph->cbChanged(pGraph, pGraph->pChangedData);
}

void ui_GraphMovePointIndex(UIGraph *pGraph, S32 iPoint, const Vec2 v2Position, F32 fMargin)
{
	UIGraphPoint *pPoint = ui_GraphGetPoint(pGraph, iPoint);
	if (pPoint)
		ui_GraphMovePoint(pGraph, pPoint, v2Position, fMargin);
}

void ui_GraphMovePointIndexAndCallback(UIGraph *pGraph, S32 iPoint, const Vec2 v2Position, F32 fMargin)
{
	UIGraphPoint *pPoint = ui_GraphGetPoint(pGraph, iPoint);
	if (pPoint)
		ui_GraphMovePointAndCallback(pGraph, pPoint, v2Position, fMargin);
}

void ui_GraphSetChangedCallback(UIGraph *pGraph, UIActivationFunc cbChanged, UserData pChangedData)
{
	pGraph->cbChanged = cbChanged;
	pGraph->pChangedData = pChangedData;
}

void ui_GraphSetDraggingCallback(UIGraph *pGraph, UIActivationFunc cbDragging, UserData pDraggingData)
{
	pGraph->cbDragging = cbDragging;
	pGraph->pDraggingData = pDraggingData;
}

void ui_GraphSetMaxPoints(UIGraph *pGraph, U8 chMaxPoints)
{
	pGraph->chMaxPoints = chMaxPoints;
	if (chMaxPoints && eaSize(&pGraph->eaPoints) > chMaxPoints)
	{
		while (eaSize(&pGraph->eaPoints) > chMaxPoints)
			ui_GraphPointFree(eaPop(&pGraph->eaPoints));
		if (pGraph->cbChanged)
			pGraph->cbChanged(pGraph, pGraph->pChangedData);
	}
}

void ui_GraphSetMinPoints(UIGraph *pGraph, U8 chMinPoints)
{
	Vec2 v2Center = {(pGraph->v2UpperBound[0] - pGraph->v2LowerBound[0]) / 2,
		(pGraph->v2UpperBound[1] - pGraph->v2LowerBound[1]) / 2 };
	pGraph->chMinPoints = chMinPoints;
	while (eaSize(&pGraph->eaPoints) < chMinPoints)
		ui_GraphAddPointAndCallback(pGraph, ui_GraphPointCreate(v2Center, 0.f));
}

void ui_GraphSetResolution(UIGraph *pGraph, U8 chResolutionX, U8 chResolutionY)
{
	pGraph->chResolutionX = chResolutionX;
	pGraph->chResolutionY = chResolutionY;
}

void ui_GraphSetBounds(UIGraph *pGraph, const Vec2 v2Lower, const Vec2 v2Upper)
{
	copyVec2(v2Lower, pGraph->v2LowerBound);
	copyVec2(v2Upper, pGraph->v2UpperBound);
	ui_GraphEnforceBounds(pGraph);
}

void ui_GraphEnforceBounds(UIGraph *pGraph)
{
	S32 i;
	for (i = 0; i < eaSize(&pGraph->eaPoints); i++)
	{
		UIGraphPoint *pPoint = pGraph->eaPoints[i];
		pPoint->v2Position[0] = CLAMP(pPoint->v2Position[0], pGraph->v2LowerBound[0], pGraph->v2UpperBound[0]);
		pPoint->v2Position[1] = CLAMP(pPoint->v2Position[1], pGraph->v2LowerBound[1], pGraph->v2UpperBound[1]);
	}
}

void ui_GraphEnforceBoundsAndCallback(UIGraph *pGraph)
{
	ui_GraphEnforceBounds(pGraph);
	if (pGraph->cbChanged)
		pGraph->cbChanged(pGraph, pGraph->pChangedData);
}

void ui_GraphSetSort(UIGraph *pGraph, bool bSort)
{
	pGraph->bSort = bSort;
	if (bSort)
		ui_GraphSortAndCallback(pGraph);
}

static S32 CompareGraphPoints(const UIGraphPoint **pA, const UIGraphPoint **pB)
{
	if ((*pA)->v2Position[0] < (*pB)->v2Position[0])
		return -1;
	else if ((*pA)->v2Position[0] > (*pB)->v2Position[0])
		return 1;
	else
	{
		if (*pA < *pB)
			return -1;
		else if (*pA > *pB)
			return 1;
		else
			return 0;
	}
}

void ui_GraphSort(UIGraph *pGraph)
{
	S32 iDragging = pGraph->bDragging ? pGraph->chDraggingPoint : -1;
	UIGraphPoint *pDragging = ui_GraphGetPoint(pGraph, iDragging);
	eaQSort(pGraph->eaPoints, CompareGraphPoints);
	if (pDragging)
		pGraph->chDraggingPoint = eaFind(&pGraph->eaPoints, pDragging);
}

void ui_GraphSortAndCallback(UIGraph *pGraph)
{
	ui_GraphSort(pGraph);
	if (pGraph->cbChanged)
		pGraph->cbChanged(pGraph, pGraph->pChangedData);
}

void ui_GraphSetLockToX0(UIGraph *pGraph, bool bLockToX0)
{
	pGraph->bLockToX0 = bLockToX0;
}

void ui_GraphSetLockToIndex(UIGraph *pGraph, bool bLockToIndex)
{
	pGraph->bLockToIndex = bLockToIndex;
}

void ui_GraphSetDrawConnection(UIGraph *pGraph, bool bDrawConnection, bool bConnectToEnd)
{
	pGraph->bDrawConnection = bDrawConnection;
	pGraph->bConnectToEnd = bConnectToEnd;
}

void ui_GraphSetMargins(UIGraph *pGraph, bool bMargins)
{
	pGraph->bMargins = bMargins;
	if (!bMargins)
	{
		bool bChanged = false;
		S32 i;
		for (i = 0; i < eaSize(&pGraph->eaPoints); i++)
		{
			if (pGraph->eaPoints[i]->fMargin)
				bChanged = true;
			pGraph->eaPoints[i]->fMargin = 0;
		}

		if (bChanged && pGraph->cbChanged)
			pGraph->cbChanged(pGraph, pGraph->pChangedData);
	}
}

void ui_GraphSetDrawScale(UIGraph *pGraph, bool bDrawScale)
{
	pGraph->bDrawScale = bDrawScale;
}

void ui_GraphSetLabels(UIGraph *pGraph, const char *pchLabelX, const char *pchLabelY)
{
	if (pGraph->pchLabelX != pchLabelX)
	{
		StructFreeString(pGraph->pchLabelX);
		pGraph->pchLabelX = StructAllocString(pchLabelX);
	}

	if (pGraph->pchLabelY != pchLabelY)
	{
		StructFreeString(pGraph->pchLabelY);
		pGraph->pchLabelY = StructAllocString(pchLabelY);
	}
}

void ui_GraphSetXValues(UIGraph *pGraph, F32 **eafValues)
{
	S32 i;
	for (i = 0; i < eaSize(&pGraph->eaPoints) && i < eafSize(eafValues); i++)
		pGraph->eaPoints[i]->v2Position[0] = (*eafValues)[i];
	ui_GraphEnforceBounds(pGraph);
	if (pGraph->bSort)
		ui_GraphSort(pGraph);
	if (pGraph->bLockToX0 && eaSize(&pGraph->eaPoints))
		pGraph->eaPoints[0]->v2Position[0] = pGraph->v2LowerBound[0];
}

void ui_GraphSetXValuesAndCallback(UIGraph *pGraph, F32 **eafValues)
{
	ui_GraphSetXValues(pGraph, eafValues);
	if (pGraph->cbChanged)
		pGraph->cbChanged(pGraph, pGraph->cbChanged);
}

S32 ui_GraphGetPointCount(UIGraph *pGraph)
{
	return eaSize(&pGraph->eaPoints);
}

U32 ui_GraphGetSelectedPointBitField(SA_PARAM_NN_VALID UIGraph* pGraph)
{
	U32 uiSelectedPoints = 0;
	FOR_EACH_IN_EARRAY(pGraph->eaPoints, UIGraphPoint, pPoint)
	{
		if (pPoint->bSelected)
			SETB(&uiSelectedPoints, ipPointIndex);
	}
	FOR_EACH_END;
	return uiSelectedPoints;
}

void ui_GraphSetSelectedPointBitField(SA_PARAM_NN_VALID UIGraph* pGraph, U32 uiSelectedPoints)
{
	FOR_EACH_IN_EARRAY(pGraph->eaPoints, UIGraphPoint, pPoint)
	{
		pPoint->bSelected = !!(TSTB(&uiSelectedPoints, ipPointIndex));
	}
	FOR_EACH_END;
}

UIGraphPane *ui_GraphPaneCreate(const char *pchLabelX, const char *pchLabelY, const Vec2 v2Lower, const Vec2 v2Upper, U8 chMaxPoints, bool bMargins, bool bXEntries, bool bYEntries)
{
	UIGraphPane *pGraphPane = calloc(1, sizeof(UIGraphPane));
	ui_GraphPaneInitialize(pGraphPane, pchLabelX, pchLabelY, v2Lower, v2Upper, chMaxPoints, bMargins, bXEntries, bYEntries);
	return pGraphPane;
}

void ui_GraphPaneInitialize(UIGraphPane *pGraphPane, const char *pchLabelX, const char *pchLabelY, const Vec2 v2Lower, const Vec2 v2Upper, U8 chMaxPoints, bool bMargins, bool bXEntries, bool bYEntries)
{
	ui_PaneInitialize(UI_PANE(pGraphPane), 0, 0, 1.f, 1.f, UIUnitPercentage, UIUnitPercentage, false MEM_DBG_PARMS_INIT);
	UI_WIDGET(pGraphPane)->tickF = ui_GraphPaneTick;
	UI_WIDGET(pGraphPane)->freeF = ui_GraphPaneFreeInternal;
	pGraphPane->pGraph = ui_GraphCreate(pchLabelX, pchLabelY, v2Lower, v2Upper, chMaxPoints, bMargins);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pGraphPane->pGraph), 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetAddChild(UI_WIDGET(pGraphPane), UI_WIDGET(pGraphPane->pGraph));
	ui_GraphPaneSetEntries(pGraphPane, bXEntries, bYEntries);
	ui_GraphSetChangedCallback(pGraphPane->pGraph, ui_GraphPaneGraphChangedCallback, pGraphPane);
	ui_GraphSetDraggingCallback(pGraphPane->pGraph, ui_GraphPaneGraphDraggingCallback, pGraphPane);
}

void ui_GraphPaneFreeInternal(UIGraphPane *pGraphPane)
{
	ui_WidgetGroupFreeInternal((UIWidgetGroup *)&pGraphPane->eaMarginEntries);
	ui_WidgetGroupFreeInternal((UIWidgetGroup *)&pGraphPane->eaXEntries);
	ui_WidgetGroupFreeInternal((UIWidgetGroup *)&pGraphPane->eaYEntries);
	if (pGraphPane->pXLowerBound)
		UI_WIDGET(pGraphPane->pXLowerBound)->freeF(pGraphPane->pXLowerBound);
	if (pGraphPane->pYLowerBound)
		UI_WIDGET(pGraphPane->pYLowerBound)->freeF(pGraphPane->pYLowerBound);
	if (pGraphPane->pXUpperBound)
		UI_WIDGET(pGraphPane->pXUpperBound)->freeF(pGraphPane->pXUpperBound);
	if (pGraphPane->pYUpperBound)
		UI_WIDGET(pGraphPane->pYUpperBound)->freeF(pGraphPane->pYUpperBound);
	ui_WidgetFreeInternal(UI_WIDGET(pGraphPane));
}

void ui_GraphPaneTick(UIGraphPane *pGraphPane, UI_PARENT_ARGS)
{
	F32 fWidth = ui_WidgetWidth(UI_WIDGET(pGraphPane), pW / pScale, 1.f);
	F32 fRightMargin = 0.f;
	F32 fHeight = 0;
	F32 fHeightBase = 0;
	S32 i;

	// If we can scale the axes, we want to put those controls before
	// the individual points, but we also want to use the width
	// of the point controls to decide how big they should be. So, Y
	// offset now, but position and X offset later.

	if (pGraphPane->pXLowerBound)
		fHeightBase += UI_WIDGET(pGraphPane->pXLowerBound)->height + UI_HSTEP;
	else if (pGraphPane->pXUpperBound)
		fHeightBase += UI_WIDGET(pGraphPane->pXUpperBound)->height + UI_HSTEP;

	if (pGraphPane->pYLowerBound)
		fHeightBase += UI_WIDGET(pGraphPane->pYLowerBound)->height + UI_HSTEP;
	else if (pGraphPane->pYUpperBound)
		fHeightBase += UI_WIDGET(pGraphPane->pYUpperBound)->height + UI_HSTEP;

	if (pGraphPane->pGraph->bMargins && pGraphPane->bYEntries && eaSize(&pGraphPane->eaMarginEntries))
	{
		fHeight = fHeightBase;
		if (pGraphPane->pMarginLabel)
		{
			UIWidget* pLabel = UI_WIDGET(pGraphPane->pMarginLabel);
			F32 fLabelX = fRightMargin + ( UI_WIDGET(pGraphPane->eaMarginEntries[0])->width - pLabel->height) * 0.5f;
			ui_WidgetSetPositionEx(pLabel, fLabelX, fHeightBase, 0, 0, UITopRight);
			ui_WidgetAddChild(UI_WIDGET(pGraphPane), pLabel);
			fHeight += pLabel->height;
		}
		for (i = 0; i < eaSize(&pGraphPane->eaMarginEntries) && i < eaSize(&pGraphPane->pGraph->eaPoints); i++)
		{
			ui_WidgetSetPositionEx(UI_WIDGET(pGraphPane->eaMarginEntries[i]), fRightMargin, fHeight, 0.f, 0.f, UITopRight);
			ui_WidgetAddChild(UI_WIDGET(pGraphPane), UI_WIDGET(pGraphPane->eaMarginEntries[i]));
			fHeight += UI_WIDGET(pGraphPane->eaMarginEntries[i])->height;
		}
		fRightMargin += UI_WIDGET(pGraphPane->eaMarginEntries[0])->width + UI_HSTEP;
	}

	if (pGraphPane->bYEntries && eaSize(&pGraphPane->eaYEntries))
	{
		fHeight = fHeightBase;
		if (pGraphPane->pYLabel)
		{
			UIWidget* pLabel = UI_WIDGET(pGraphPane->pYLabel);
			F32 fLabelX = fRightMargin + ( UI_WIDGET(pGraphPane->eaYEntries[0])->width - pLabel->height) * 0.5f;
			ui_WidgetSetPositionEx(pLabel, fLabelX, fHeightBase, 0, 0, UITopRight);
			ui_WidgetAddChild(UI_WIDGET(pGraphPane), pLabel);
			fHeight += pLabel->height;
		}
		for (i = 0; i < eaSize(&pGraphPane->eaYEntries) && i < eaSize(&pGraphPane->pGraph->eaPoints); i++)
		{
			ui_WidgetSetPositionEx(UI_WIDGET(pGraphPane->eaYEntries[i]), fRightMargin, fHeight, 0.f, 0.f, UITopRight);
			ui_WidgetAddChild(UI_WIDGET(pGraphPane), UI_WIDGET(pGraphPane->eaYEntries[i]));
			fHeight += UI_WIDGET(pGraphPane->eaYEntries[i])->height;
		}
		fRightMargin += UI_WIDGET(pGraphPane->eaYEntries[0])->width + UI_HSTEP;
	}

	if (pGraphPane->bXEntries && eaSize(&pGraphPane->eaXEntries))
	{
		fHeight = fHeightBase;
		if (pGraphPane->pXLabel)
		{
			UIWidget* pLabel = UI_WIDGET(pGraphPane->pXLabel);
			F32 fLabelX = fRightMargin + ( UI_WIDGET(pGraphPane->eaXEntries[0])->width - pLabel->height) * 0.5f;
			ui_WidgetSetPositionEx(pLabel, fLabelX, fHeightBase, 0, 0, UITopRight);
			ui_WidgetAddChild(UI_WIDGET(pGraphPane), pLabel);
			fHeight += pLabel->height;
		}
		for (i = 0; i < eaSize(&pGraphPane->eaXEntries) && i < eaSize(&pGraphPane->pGraph->eaPoints); i++)
		{
			ui_WidgetSetPositionEx(UI_WIDGET(pGraphPane->eaXEntries[i]), fRightMargin, fHeight, 0.f, 0.f, UITopRight);
			ui_WidgetAddChild(UI_WIDGET(pGraphPane), UI_WIDGET(pGraphPane->eaXEntries[i]));
			fHeight += UI_WIDGET(pGraphPane->eaXEntries[i])->height;
		}
		fRightMargin += UI_WIDGET(pGraphPane->eaXEntries[0])->width + UI_HSTEP;
	}

	if (fHeight > UI_WIDGET(pGraphPane)->height && UI_WIDGET(pGraphPane)->height == UIUnitFixed)
		ui_WidgetSetHeight(UI_WIDGET(pGraphPane), fHeight);

	fHeight = 0;
	if (pGraphPane->pXLowerBound)
	{
		ui_WidgetSetPositionEx(UI_WIDGET(pGraphPane->pXLowerBoundEntry), fWidth - fRightMargin, fHeight, 0, 0, UITopLeft);
		ui_WidgetSetPositionEx(UI_WIDGET(pGraphPane->pXLowerBound), (fWidth - fRightMargin) + UI_WIDGET(pGraphPane->pXLowerBoundEntry)->width, fHeight, 0, 0, UITopLeft);
	}
	if (pGraphPane->pXUpperBound)
	{
		ui_WidgetSetPositionEx(UI_WIDGET(pGraphPane->pXUpperBound), 0, fHeight, 0, 0, UITopRight);
		ui_WidgetSetPositionEx(UI_WIDGET(pGraphPane->pXUpperBoundEntry), UI_WIDGET(pGraphPane->pXUpperBound)->width, fHeight, 0, 0, UITopRight);
	}
	if (pGraphPane->pXLowerBound)
		fHeight += UI_WIDGET(pGraphPane->pXLowerBound)->height + UI_HSTEP;
	else if (pGraphPane->pXUpperBound)
		fHeight += UI_WIDGET(pGraphPane->pXUpperBound)->height + UI_HSTEP;

	if (pGraphPane->pYLowerBound)
	{
		ui_WidgetSetPositionEx(UI_WIDGET(pGraphPane->pYLowerBoundEntry), fWidth - fRightMargin, fHeight, 0, 0, UITopLeft);
		ui_WidgetSetPositionEx(UI_WIDGET(pGraphPane->pYLowerBound), (fWidth - fRightMargin) + UI_WIDGET(pGraphPane->pYLowerBoundEntry)->width, fHeight, 0, 0, UITopLeft);
	}
	if (pGraphPane->pYUpperBound)
	{
		ui_WidgetSetPositionEx(UI_WIDGET(pGraphPane->pYUpperBound), 0, fHeight, 0, 0, UITopRight);
		ui_WidgetSetPositionEx(UI_WIDGET(pGraphPane->pYUpperBoundEntry), UI_WIDGET(pGraphPane->pYUpperBound)->width, fHeight, 0, 0, UITopRight);
	}

	UI_WIDGET(pGraphPane->pGraph)->rightPad = fRightMargin;

	ui_PaneTick(UI_PANE(pGraphPane), UI_PARENT_VALUES);
}

UIGraph *ui_GraphPaneGetGraph(UIGraphPane *pGraphPane)
{
	return pGraphPane->pGraph;
}

void ui_GraphPaneSetChangedCallback(UIGraphPane *pGraphPane, UIActivationFunc cbChanged, UserData pChangedData)
{
	pGraphPane->cbChanged = cbChanged;
	pGraphPane->pChangedData = pChangedData;
}

void ui_GraphPaneSetDraggingCallback(UIGraphPane *pGraphPane, UIActivationFunc cbDragging, UserData pDraggingData)
{
	pGraphPane->cbDragging = cbDragging;
	pGraphPane->pDraggingData = pDraggingData;
}

void ui_GraphPaneSetEntries(UIGraphPane *pGraphPane, bool bXEntries, bool bYEntries)
{
	pGraphPane->bXEntries = bXEntries;
	pGraphPane->bYEntries = bYEntries;
}

static void CreateOrUpdateSpinner(UIGraphPane *pGraphPane, UISpinner **ppSpinner, UITextEntry **ppEntry, F32 fMin, F32 fMax, F32 fValue)
{
	devassertmsg(!(*ppSpinner || *ppEntry) || (*ppSpinner && *ppEntry),
		"Either the spinner is made and the entry isn't, or vice versa.");
	// If the minimum and maximum values of a bound are not the same, create the spinner widget for it.
	if (fMin != fMax)
	{
		if (*ppSpinner)
		{
			ui_SpinnerSetBounds(*ppSpinner, fMin, fMax, 0);
		}
		else
		{
			*ppSpinner = ui_SpinnerCreate(0, 0, fMin, fMax, 0, fValue, ui_GraphPaneSpinnerCallback, pGraphPane);
			*ppEntry = ui_TextEntryCreate("", 0, 0);
			ui_TextEntrySetWidthInCharacters(*ppEntry, 4);
			ui_TextEntrySetFloatOnly(*ppEntry);
			ui_WidgetSetDimensions(UI_WIDGET(*ppSpinner), 8, UI_WIDGET(*ppEntry)->height);
		}
		ui_WidgetAddChild(UI_WIDGET(pGraphPane), UI_WIDGET(*ppSpinner));
		ui_WidgetAddChild(UI_WIDGET(pGraphPane), UI_WIDGET(*ppEntry));
	}
	else if (*ppSpinner)
	{
		ui_WidgetDestroy(ppSpinner);
		ui_WidgetDestroy(ppEntry);
	}
}

static void UpdateSpinnerEntries(UIGraphPane *pGraphPane)
{
	F32 afValues[] = { pGraphPane->pGraph->v2LowerBound[0], pGraphPane->pGraph->v2LowerBound[1], pGraphPane->pGraph->v2UpperBound[0], pGraphPane->pGraph->v2UpperBound[1] };
	UITextEntry *apEntries[] = {pGraphPane->pXLowerBoundEntry, pGraphPane->pYLowerBoundEntry, pGraphPane->pXUpperBoundEntry, pGraphPane->pYUpperBoundEntry };
	char ach[100];
	S32 i;
	devassertmsg(ARRAY_SIZE(afValues) == ARRAY_SIZE(apEntries), "Array size mismatch");
	for (i = 0; i < ARRAY_SIZE(afValues); i++)
	{
		if (apEntries[i])
		{
			sprintf(ach, "%.4g", (double)afValues[i]);
			ui_TextEntrySetText(apEntries[i], ach);
		}
	}
}

void ui_GraphPaneSetBounds(UIGraphPane *pGraphPane, const Vec2 v2MinMin, const Vec2 v2MaxMin, const Vec2 v2MinMax, const Vec2 v2MaxMax)
{
	CreateOrUpdateSpinner(pGraphPane, &pGraphPane->pXLowerBound, &pGraphPane->pXLowerBoundEntry, v2MinMin[0], v2MaxMin[0], pGraphPane->pGraph->v2LowerBound[0]);
	CreateOrUpdateSpinner(pGraphPane, &pGraphPane->pYLowerBound, &pGraphPane->pYLowerBoundEntry, v2MinMin[1], v2MaxMin[1], pGraphPane->pGraph->v2LowerBound[1]);
	CreateOrUpdateSpinner(pGraphPane, &pGraphPane->pXUpperBound, &pGraphPane->pXUpperBoundEntry, v2MinMax[0], v2MaxMax[0], pGraphPane->pGraph->v2LowerBound[0]);
	CreateOrUpdateSpinner(pGraphPane, &pGraphPane->pYUpperBound, &pGraphPane->pYUpperBoundEntry, v2MinMax[1], v2MaxMax[1], pGraphPane->pGraph->v2LowerBound[1]);
	UpdateSpinnerEntries(pGraphPane);
}

static void DestroyEntries(UIGraphPane *pGraphPane, S32 iMaxEntries)
{
	while (eaSize(&pGraphPane->eaYEntries) > iMaxEntries)
		ui_WidgetQueueFree(eaPop(&pGraphPane->eaYEntries));
	while (eaSize(&pGraphPane->eaXEntries) > iMaxEntries)
		ui_WidgetQueueFree(eaPop(&pGraphPane->eaXEntries));
	while (eaSize(&pGraphPane->eaMarginEntries) > iMaxEntries)
		ui_WidgetQueueFree(eaPop(&pGraphPane->eaMarginEntries));
}

static void CreateEntries(UIGraphPane *pGraphPane, S32 iAtLeast)
{
	if (!pGraphPane->pXLabel)
	{
		pGraphPane->pXLabel = ui_LabelCreate("Time", 0, 0);
		pGraphPane->pYLabel = ui_LabelCreate("Value", 0, 0);
		pGraphPane->pMarginLabel = ui_LabelCreate("Jitter", 0, 0);
	}
	// Make sure we have text entries for each point in the graph.
	while (eaSize(&pGraphPane->eaXEntries) < iAtLeast)
	{
		UITextEntry *pEntry = ui_TextEntryCreate("", 0, 0);
		ui_TextEntrySetChangedCallback(pEntry, ui_GraphPaneEntryChangedCallback, pGraphPane);
		ui_TextEntrySetFinishedCallback(pEntry, ui_GraphPaneEntryFinishedCallback, pGraphPane);
		ui_TextEntrySetWidthInCharacters(pEntry, 4);
		ui_TextEntrySetFloatOnly(pEntry);
		eaPush(&pGraphPane->eaXEntries, pEntry);
	}
	while (eaSize(&pGraphPane->eaYEntries) < iAtLeast)
	{
		UITextEntry *pEntry = ui_TextEntryCreate("", 0, 0);
		ui_TextEntrySetChangedCallback(pEntry, ui_GraphPaneEntryChangedCallback, pGraphPane);
		ui_TextEntrySetFinishedCallback(pEntry, ui_GraphPaneEntryFinishedCallback, pGraphPane);
		ui_TextEntrySetWidthInCharacters(pEntry, 4);
		ui_TextEntrySetFloatOnly(pEntry);
		eaPush(&pGraphPane->eaYEntries, pEntry);
	}
	while (eaSize(&pGraphPane->eaMarginEntries) < iAtLeast)
	{
		UITextEntry *pEntry = ui_TextEntryCreate("", 0, 0);
		ui_TextEntrySetChangedCallback(pEntry, ui_GraphPaneEntryChangedCallback, pGraphPane);
		ui_TextEntrySetFinishedCallback(pEntry, ui_GraphPaneEntryFinishedCallback, pGraphPane);
		ui_TextEntrySetWidthInCharacters(pEntry, 4);
		ui_TextEntrySetFloatOnly(pEntry);
		eaPush(&pGraphPane->eaMarginEntries, pEntry);
	}
}

static void UpdateEntries(UIGraphPane *pGraphPane, UIGraphPoint *pPoint, S32 i)
{
	UITextEntry *pXEntry, *pYEntry, *pMarginEntry;
	char achValue[100];
	pXEntry = pGraphPane->eaXEntries[i];
	pYEntry = pGraphPane->eaYEntries[i];
	pMarginEntry = pGraphPane->eaMarginEntries[i];

	sprintf(achValue, "%.3g", (double)pPoint->v2Position[0]);
	ui_TextEntrySetText(pXEntry, achValue);
	sprintf(achValue, "%.3g", (double)pPoint->v2Position[1]);
	ui_TextEntrySetText(pYEntry, achValue);
	sprintf(achValue, "%.3g", (double)pPoint->fMargin);
	ui_TextEntrySetText(pMarginEntry, achValue);
}

// The graph was updated, we need to reset the text entries.
void ui_GraphPaneGraphResetTextEntries(UIGraph *pGraph, UIGraphPane *pGraphPane)
{
	UIGraphPoint *pPoint;
	S32 i;
	CreateEntries(pGraphPane, eaSize(&pGraph->eaPoints));
	DestroyEntries(pGraphPane, eaSize(&pGraph->eaPoints));
	for (i = 0; pPoint = ui_GraphGetPoint(pGraph, i); i++)
		UpdateEntries(pGraphPane, pPoint, i);
}

void ui_GraphPaneGraphDraggingCallback(UIGraph *pGraph, UIGraphPane *pGraphPane)
{
	ui_GraphPaneGraphResetTextEntries(pGraph, pGraphPane);
	if (pGraphPane->cbDragging)
		pGraphPane->cbDragging(pGraphPane, pGraphPane->pDraggingData);
}

void ui_GraphPaneGraphChangedCallback(UIGraph *pGraph, UIGraphPane *pGraphPane)
{
	ui_GraphPaneGraphResetTextEntries(pGraph, pGraphPane);
	if (pGraphPane->cbChanged)
		pGraphPane->cbChanged(pGraphPane, pGraphPane->pChangedData);
}

// The text entries were updated, we need to update the points.
static void GraphPaneEntryCallback(UITextEntry *pEntry, UIGraphPane *pGraphPane)
{
	S32 i;
	UIGraph *pGraph = ui_GraphPaneGetGraph(pGraphPane);
 	UIGraphPoint **eaPoints = NULL;
	S32 iCount = eaSize(&pGraph->eaPoints);
	// Since moving the points may resort them, we need to iterate over a copy.
	eaCopy(&eaPoints, &pGraph->eaPoints);
	iCount = min(iCount, eaSize(&pGraphPane->eaXEntries));
	iCount = min(iCount, eaSize(&pGraphPane->eaYEntries));
	iCount = min(iCount, eaSize(&pGraphPane->eaMarginEntries));
	if (iCount) // make /analyze shut up
	{
		assert(pGraphPane->eaXEntries);
		assert(pGraphPane->eaYEntries);
		assert(pGraphPane->eaMarginEntries);
	}
	for (i = 0; i < iCount; i++)
	{
		F32 fX = atof(ui_TextEntryGetText(pGraphPane->eaXEntries[i]));
		F32 fY = atof(ui_TextEntryGetText(pGraphPane->eaYEntries[i]));
		F32 fMargin = atof(ui_TextEntryGetText(pGraphPane->eaMarginEntries[i]));
		Vec2 v2Pos = {fX, fY};
		ui_GraphMovePoint(pGraph, eaPoints[i], v2Pos, fMargin);
	}
	eaDestroy(&eaPoints);
}

void ui_GraphPaneEntryChangedCallback(UITextEntry *pEntry, UIGraphPane *pGraphPane)
{
	GraphPaneEntryCallback(pEntry, pGraphPane);
	if (pGraphPane->cbDragging)
		pGraphPane->cbDragging(pGraphPane, pGraphPane->pDraggingData);
}

void ui_GraphPaneEntryFinishedCallback(UITextEntry *pEntry, UIGraphPane *pGraphPane)
{
	GraphPaneEntryCallback(pEntry, pGraphPane);
	if (pGraphPane->cbChanged)
		pGraphPane->cbChanged(pGraphPane, pGraphPane->pChangedData);
}

// The bounds were updated, we need to update the graph.
void ui_GraphPaneSpinnerCallback(UISpinner *pSpinner, UIGraphPane *pGraphPane)
{
	UIGraph *pGraph = ui_GraphPaneGetGraph(pGraphPane);
	if (pSpinner == pGraphPane->pXLowerBound)
		pGraph->v2LowerBound[0] = ui_SpinnerGetValue(pSpinner);
	else if (pSpinner == pGraphPane->pYLowerBound)
		pGraph->v2LowerBound[1] = ui_SpinnerGetValue(pSpinner);
	else if (pSpinner == pGraphPane->pXUpperBound)
		pGraph->v2UpperBound[0] = ui_SpinnerGetValue(pSpinner);
	else if (pSpinner == pGraphPane->pYUpperBound)
		pGraph->v2UpperBound[1] = ui_SpinnerGetValue(pSpinner);
	ui_GraphEnforceBounds(pGraph);
	UpdateSpinnerEntries(pGraphPane);
	if (pGraphPane->cbChanged)
		pGraphPane->cbChanged(pGraphPane, pGraphPane->pChangedData);
}
