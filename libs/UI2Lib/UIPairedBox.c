/***************************************************************************



***************************************************************************/


#include "GfxClipper.h"
#include "GfxPrimitive.h"

#include "inputMouse.h"

#include "UIPairedBox.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static void FillControlPoints(Vec2 controlPoints[4], F32 x1, F32 y1, F32 x2, F32 y2,bool bVertical1, bool bVertical2)
{
	F32 dx = CLAMP(0.85 * ABS(x2 - x1), 15, 256);
	F32 dy = CLAMP(0.85 * ABS(y2 - y1), 15, 256);
	controlPoints[0][0] = x1;
	controlPoints[0][1] = y1;
	controlPoints[3][0] = x2;
	controlPoints[3][1] = y2;
	controlPoints[1][1] = y1 - (bVertical1 ? dy : 0);
	controlPoints[2][1] = y2 + (bVertical2 ? dy : 0);
	controlPoints[1][0] = x1 + (!bVertical1 ? dx : 0);
	controlPoints[2][0] = x2 - (!bVertical2 ? dx : 0);
}

void ui_PairedBoxLineDraw(UIPairedBoxLine *line, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(line);
	Vec2 controlPoints[4];

	FillControlPoints(controlPoints, line->source->lastX, line->source->lastY, line->dest->lastX, line->dest->lastY, line->source->bVertical, line->dest->bVertical);
	gfxDrawBezier(controlPoints, z, line->source->color, line->source->color, scale * line->lineScale);
}

void ui_PairedBoxLineTick(UIPairedBoxLine *line, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(line);
	UI_TICK_EARLY(line, false, false);
	UI_TICK_LATE(line);
}

void ui_PairedBoxLineFreeInternal(UIPairedBoxLine *line)
{
	ui_WidgetFreeInternal(UI_WIDGET(line));
}

UIPairedBoxLine *ui_PairedBoxLineCreate(UIPairedBox *source, UIPairedBox *dest)
{
	UIPairedBoxLine *line = calloc(1, sizeof(UIPairedBoxLine));
	ui_WidgetInitialize(UI_WIDGET(line), ui_PairedBoxLineTick, ui_PairedBoxLineDraw, ui_PairedBoxLineFreeInternal, NULL, NULL);
	line->source = source;
	line->dest = dest;
	line->lineScale = 1.0;
	return line;
}


void ui_PairedBoxCreatePair(UIPairedBox **source, UIPairedBox **dest, Color color, UIAnyWidget *lineParent)
{
	*dest = ui_PairedBoxCreate(color);
	*source = ui_PairedBoxCreate(color);
	ui_PairedBoxConnect(*source, *dest, lineParent);
}

UIPairedBox *ui_PairedBoxCreate(Color color)
{
	UIStyleFont *pFont;
	UIPairedBox *box = calloc(1, sizeof(UIPairedBox));
	F32 height;
	ui_WidgetInitialize(UI_WIDGET(box), ui_PairedBoxTick, ui_PairedBoxDraw, ui_PairedBoxFreeInternal, NULL, NULL);
	pFont = GET_REF(UI_GET_SKIN(box)->hNormal);
	box->color = color;
	box->lastX = -1;
	box->lastY = -1;
	height = ui_StyleFontLineHeight(pFont, 1.f) + UI_STEP;
	ui_WidgetSetDimensions(UI_WIDGET(box), height, height);
	return box;
}

void ui_PairedBoxDisable(UIPairedBox *dest)
{
	dest->lastX = -1;
	dest->lastY = -1;
}

void ui_PairedBoxConnect(UIPairedBox *source, UIPairedBox *dest, UIAnyWidget *lineParent)
{
	// Break any previous hook-ups
	if (source->otherBox && (source->otherBox->otherBox == source)) {
		if (source->line) {
			ui_WidgetQueueFree(UI_WIDGET(source->line));
		}
		source->otherBox->otherBox = NULL;
		source->otherBox->line = NULL;
	}
	if (dest->otherBox && (dest->otherBox->otherBox == dest)) {
		if (dest->line) {
			ui_WidgetQueueFree(UI_WIDGET(dest->line));
		}
		dest->otherBox->otherBox = NULL;
		dest->otherBox->line = NULL;
	}

	// Hook them up
	source->otherBox = dest;
	dest->otherBox = source;

	// Create a line
	source->line = dest->line = ui_PairedBoxLineCreate(source, dest);
	source->line->widget.priority = 2; // Need to adapt this if we ever use richer priorities
	ui_WidgetAddChild((UIWidget*)lineParent, UI_WIDGET(source->line));
}

void ui_PairedBoxDisconnect(UIPairedBox *source, UIPairedBox *dest)
{
	if (source->line)
		ui_WidgetQueueFree(UI_WIDGET(source->line));
	source->otherBox = dest->otherBox = NULL;
	source->line = dest->line = NULL;
}

void ui_PairedBoxFreeInternal(UIPairedBox *box)
{
	if (box->otherBox && (box->otherBox->otherBox == box))
	{
		box->otherBox->otherBox = NULL;
		if (box->line && (box->otherBox->line == box->line)) 
		{
			box->otherBox->line = NULL;
			ui_WidgetFreeInternal(UI_WIDGET(box->line));
		}
	}
	ui_WidgetFreeInternal(UI_WIDGET(box));
}

void ui_PairedBoxTick(UIPairedBox *paired, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(paired);
	UI_TICK_EARLY(paired, true, false);

	paired->lastX = x + w / 2;
	paired->lastY = y + w / 2;

	UI_TICK_LATE(paired);
}

void ui_PairedBoxDraw(UIPairedBox *paired, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(paired);
	Color c;
	UI_DRAW_EARLY(paired);

	if (UI_GET_SKIN(paired))
		c = UI_GET_SKIN(paired)->button[0];
	else
		c = paired->widget.color[0];

	if (w && h)
		ui_DrawCapsule(&box, z, c, scale);

	UI_DRAW_LATE(paired);
}

