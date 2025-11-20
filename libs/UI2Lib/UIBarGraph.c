#include "textparser.h"

#include "inputMouse.h"

#include "GfxClipper.h"
#include "GfxPrimitive.h"
#include "GfxSpriteText.h"

#include "UIBarGraph.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

UIBarGraph *ui_BarGraphCreate(const char *pchLabelX, const char *pchLabelY, const Vec2 v2Lower, const Vec2 v2Upper)
{
	UIBarGraph *pGraph = calloc(1, sizeof(UIBarGraph));
	ui_WidgetInitialize(UI_WIDGET(pGraph), ui_BarGraphTick, ui_BarGraphDraw, ui_BarGraphFreeInternal, NULL, NULL);
	ui_BarGraphSetBounds(pGraph, v2Lower, v2Upper);
	ui_BarGraphSetLabels(pGraph, pchLabelX, pchLabelY);
	return pGraph;
}

void ui_BarGraphFreeInternal(UIBarGraph *pGraph)
{
	ui_WidgetFreeInternal(UI_WIDGET(pGraph));
}

// Convert fX and fY from the box coordinates to the graph coordinates.
// Slightly tricky since the Y coordinate is inverted.
void ui_BarGraphToGraphCoords(UIBarGraph *pGraph, CBox *pBox, F32 fX, F32 fY, F32 *pfOutX, F32 *pfOutY)
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
void ui_BarGraphToBoxCoords(UIBarGraph *pGraph, CBox *pBox, F32 fX, F32 fY, F32 *pfOutX, F32 *pfOutY)
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

#define ui_BarGraph_AXIS_MARGIN (32.f * scale)
#define ui_BarGraph_MARGIN_WIDTH (6.f)

void ui_BarGraphTick(UIBarGraph *pGraph, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pGraph);
	F32 fAxisMargin = ui_BarGraph_AXIS_MARGIN;
	CBox graphBox = {x + fAxisMargin, y + fAxisMargin, x + w - fAxisMargin, y + h - fAxisMargin};
	CBox pointBox = {0, 0, ui_BarGraph_MARGIN_WIDTH, ui_BarGraph_MARGIN_WIDTH};

	UI_TICK_EARLY(pGraph, false, true);
	
	UI_TICK_LATE(pGraph);
}

void ui_BarGraphDraw(UIBarGraph *pGraph, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pGraph);
	UIStyleFont *pFont = GET_REF(UI_GET_SKIN(pGraph)->hNormal);
	Color baseColor = ui_StyleFontGetColor(pFont);
	Color red = {0xFF,0x00,0x00,0xFF};
	// For axis names, add a margin to the other axis (because X-axis legend needs y to move up)
	F32 fxAxisMargin = pGraph->pchLabelX ? ui_BarGraph_AXIS_MARGIN : 0;
	F32 fyAxisMargin = pGraph->pchLabelY ? ui_BarGraph_AXIS_MARGIN : 0;
	CBox graphBox = {x + fxAxisMargin, y + fyAxisMargin, x + w, y + h};
	F32 perGraphDiv = (graphBox.hy - graphBox.ly) / pGraph->model_stride;
	CBox pointBox = {0, 0, 4, 4};
	F32 fLastX = -1, fLastY = -1;
	S32 i, j;

	UI_DRAW_EARLY(pGraph);

	if (pGraph->pchLabelY)
		gfxfont_Printf(x + UI_HSTEP_SC, y + ui_BarGraph_AXIS_MARGIN / 2, z, scale, scale, CENTER_Y, "%s", pGraph->pchLabelY);
	if (pGraph->pchLabelX)
		gfxfont_Printf(x + w / 2, y + h - ui_BarGraph_AXIS_MARGIN / 2, z, scale, scale, CENTER_XY, "%s", pGraph->pchLabelX);

	if(pGraph->fmodel)
	{
		F32 leftover = 0;
		F32 valsPerLine = (pGraph->length/pGraph->model_stride)/(graphBox.hx-graphBox.lx);
		for(i=0; i<pGraph->model_stride; i++)
		{
			CBox innerGraphBox =   {graphBox.lx, 
									graphBox.ly+perGraphDiv*i, 
									graphBox.hx, 
									graphBox.hy-perGraphDiv*(pGraph->model_stride-(i+1))};
			F32 gx = 0;
			F32 midy = (innerGraphBox.ly + innerGraphBox.hy)/2;
			F32 yscale = /*pGraph->scale*/ (innerGraphBox.ly-innerGraphBox.hy)/(pGraph->v2UpperBound[1]-pGraph->v2LowerBound[1]);

			clipperPushRestrict(&innerGraphBox);

			gfxDrawLine(innerGraphBox.lx, innerGraphBox.ly, z, innerGraphBox.lx, innerGraphBox.hy, baseColor);
			gfxDrawLine(innerGraphBox.lx, midy, z, innerGraphBox.hx, midy, red);

			leftover = 0;
			for(j=i; j<pGraph->length; j+=pGraph->model_stride)
			{
				int perLine;
				F32 vals = valsPerLine+leftover;
				int valsToDo = vals;
				F32 valAvg = 0;
				F32 gy;

				for(perLine = 0; perLine+1<valsToDo && j<pGraph->length; perLine++)
				{
					if(pGraph->model_float)
					{
						valAvg += pGraph->fmodel[j];
					}
					else
					{
						valAvg += pGraph->imodel[j];
					}

					if(perLine<valsToDo)
					{
						j += pGraph->model_stride;
					}
				}
				valAvg /= perLine;
				if(leftover>1) leftover -= 1;
				leftover += valsPerLine - floor(valsPerLine);

				if(pGraph->model_float)
				{
					gy = valAvg - pGraph->fmid;
				}
				else
				{
					gy = valAvg - pGraph->imid;
				}

				gy = -gy;  // Invert line

				gfxDrawLine(innerGraphBox.lx+gx, midy, z, innerGraphBox.lx+gx, midy+gy*yscale, baseColor);

				gx++;

				if(innerGraphBox.lx+gx>innerGraphBox.hx)
				{
					break;  // No point drawing unseen things
				}
			}

			clipperPop();
		}	
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

void ui_BarGraphSetModel(UIBarGraph *pGraph, void* model, int stride, int length, U32 floats)
{
	if(floats)
	{
		pGraph->fmodel = model;
	}
	else
	{
		pGraph->imodel = model;
	}

	pGraph->model_float = floats;
	pGraph->model_stride = stride;
	pGraph->length = length;
}

void ui_BarGraphSetResolution(UIBarGraph *pGraph, U8 chResolutionX, U8 chResolutionY)
{
	pGraph->chResolutionX = chResolutionX;
	pGraph->chResolutionY = chResolutionY;
}

void ui_BarGraphSetBounds(UIBarGraph *pGraph, const Vec2 v2Lower, const Vec2 v2Upper)
{
	copyVec2(v2Lower, pGraph->v2LowerBound);
	copyVec2(v2Upper, pGraph->v2UpperBound);
}

void ui_BarGraphSetDrawScale(UIBarGraph *pGraph, bool bDrawScale)
{
	pGraph->bDrawScale = bDrawScale;
}

void ui_BarGraphSetLabels(UIBarGraph *pGraph, const char *pchLabelX, const char *pchLabelY)
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
