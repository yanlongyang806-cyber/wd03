#include "UIRectangularSelection.h"
#include "GfxPrimitive.h"
#include "inputMouse.h"
#include "inputLib.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););


static void drawHighlightedLine(UIRectangularSelection *rect, F32 x1, F32 y1, F32 z, F32 x2, F32 y2, bool doHightlight)
{
	gfxDrawLineEx(x1, y1, z, x2, y2, doHightlight?rect->rolloverColor:rect->color, doHightlight?rect->rolloverColor:rect->color, 1, false);
	if (doHightlight) {
		Color color2 = rect->rolloverColor;
		color2.a /= 2;
		gfxDrawLineEx(x1-1, y1-1, z, x2-1, y2-1, color2, color2, 1, false);
		gfxDrawLineEx(x1+1, y1+1, z, x2+1, y2+1, color2, color2, 1, false);
	}
}

void ui_RectangularSelectionDraw(UIRectangularSelection *rect, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(rect);
	F32 x1 = x, y1 = y, x2 = x+w, y2 = y+h;

	drawHighlightedLine(rect, x1, y1, z, x2, y1, !!(rect->would_be_resizing & UIRF_Top));
	drawHighlightedLine(rect, x2, y1, z, x2, y2, !!(rect->would_be_resizing & UIRF_Right));
	drawHighlightedLine(rect, x2, y2, z, x1, y2, !!(rect->would_be_resizing & UIRF_Bottom));
	drawHighlightedLine(rect, x1, y2, z, x1, y1, !!(rect->would_be_resizing & UIRF_Left));
}

void ui_RectangularSelectionTick(UIRectangularSelection *rect, UI_PARENT_ARGS)
{
	CBox bbox;
	UI_GET_COORDINATES(rect);
#define BORDER 8

	BuildCBox(&bbox, x - BORDER/2, y - BORDER/2, w+BORDER, h+BORDER);

	if (rect->resizing) {
		if (!mouseIsDown(MS_LEFT))
		{
			rect->resizing = 0;

			if (rect->resizeFinishedF)
			{
				rect->resizeFinishedF( rect, rect->resizeFinishedData );
			}
		} else {
			// Update values!
			int i;
			F32 dx, dy;
			struct {
				UIResizingFlag edge;
				UIResizingFlag oppositeEdge;
				F32 oldvalue;
				int xfact, yfact;
				F32 newvalue;
			} edges[] = {
				{UIRF_Left, UIRF_Right, rect->orig_widget.x, 1, 0},
				{UIRF_Right, UIRF_Left, rect->orig_widget.x + rect->orig_widget.width, 1, 0},
				{UIRF_Top, UIRF_Bottom, rect->orig_widget.y, 0, 1},
				{UIRF_Bottom, UIRF_Top, rect->orig_widget.y + rect->orig_widget.height, 0, 1},
			};
			dx = g_ui_State.mouseX - rect->grabbedX;
			dy = g_ui_State.mouseY - rect->grabbedY;
			if (inpLevelPeek(INP_SHIFT)) { // Fixed aspect ratio
				if ((rect->resizing & (UIRF_Left | UIRF_Right | UIRF_Top | UIRF_Bottom)) == (UIRF_Left | UIRF_Right | UIRF_Top | UIRF_Bottom)) {
					F32 ratio = (F32)ABS(dx)/(F32)((ABS(dy)+ABS(dx))?ABS(dy)+ABS(dx):1);
					if (ratio > 0.667) {
						dy = 0;
					} else if (ratio > 0.333) {
						dx = dy = (dx + dy) / 2;
					} else {
						dx = 0;
					}
				} else {
					dx = dy = (dx + dy) / 2;
				}
			}
			for (i=0; i<ARRAY_SIZE(edges); i++) {
				if (edges[i].edge & rect->resizing) {
					edges[i].newvalue = edges[i].oldvalue + edges[i].xfact * dx + edges[i].yfact * dy;
				} else if (inpLevelPeek(INP_ALT) && edges[i].oppositeEdge & rect->resizing) {
					edges[i].newvalue = edges[i].oldvalue - edges[i].xfact * dx - edges[i].yfact * dy;
				} else {
					edges[i].newvalue = edges[i].oldvalue;
				}
			}

			rect->widget.x = edges[0].newvalue;
			rect->widget.width = CLAMP(edges[1].newvalue - edges[0].newvalue, rect->minWidth, rect->maxWidth);
			rect->widget.y = edges[2].newvalue;
			rect->widget.height = CLAMP(edges[3].newvalue - edges[2].newvalue, rect->minHeight, rect->maxHeight);

			ui_SetCursorForDirection(rect->cursor_direction);
			ui_CursorLock();
		}
	}
	if (!rect->resizing)
	{
		// Check for starting dragging
		if (mouseCollision(&bbox)) {
			UIResizingFlag new_resizing=0;

			if (g_ui_State.mouseX > bbox.right - BORDER) {
				new_resizing |= UIRF_Right;
			} else if (g_ui_State.mouseX < bbox.left + BORDER) {
				new_resizing |= UIRF_Left;
			}
			if (g_ui_State.mouseY > bbox.bottom - BORDER) {
				new_resizing |= UIRF_Bottom;
			} else if (g_ui_State.mouseY < bbox.top + BORDER) {
				new_resizing |= UIRF_Top;
			}
			if (!new_resizing) {
				new_resizing = ~0; // All of them, move it!
			}
#define ALLSET(flags) ((new_resizing & (flags)) == (flags))
			if (!!(new_resizing & UIRF_Left) +
				!!(new_resizing & UIRF_Right) +
				!!(new_resizing & UIRF_Top) +
				!!(new_resizing & UIRF_Bottom) > 2)
			{
				// More than a trivial case
				rect->cursor_direction = UIAnyDirection;
			} else {
				static UIDirection directions[3][3] = {
					{UITopLeft, UITop, UITopRight},
					{UILeft, UINoDirection, UIRight},
					{UIBottomLeft, UIBottom, UIBottomRight},
				};
				int xi = (new_resizing & UIRF_Left)?0:((new_resizing & UIRF_Right)?2:1);
				int yi = (new_resizing & UIRF_Top)?0:((new_resizing & UIRF_Bottom)?2:1);
				rect->cursor_direction = directions[yi][xi];
				assert(rect->cursor_direction != UINoDirection);
			}

			ui_SetCursorForDirection(rect->cursor_direction);
			ui_CursorLock();
			if (mouseDownHit(MS_LEFT, &bbox)) {
				rect->resizing = new_resizing;
				rect->grabbedX = g_ui_State.mouseX;
				rect->grabbedY = g_ui_State.mouseY;
				//rect->orig_widget = rect->widget;
				memcpy(&rect->orig_widget, &rect->widget, sizeof(UIWidget));
			}
			if (mouseDownHit(MS_RIGHT, &bbox) && rect->widget.contextF) {
				rect->widget.contextF( rect, rect->widget.contextData );
			}
			rect->would_be_resizing = new_resizing;
		} else {
			rect->cursor_direction = UINoDirection;
			rect->would_be_resizing = 0;
		}
	}
}

UIRectangularSelection *ui_RectangularSelectionCreate(F32 x, F32 y, F32 w, F32 h, Color color, Color rolloverColor)
{
	UIRectangularSelection *rect = (UIRectangularSelection *)calloc(1, sizeof(UIRectangularSelection));
	ui_RectangularSelectionInitialize(rect, x, y, w, h, color, rolloverColor);
	return rect;
}

void ui_RectangularSelectionInitialize(UIRectangularSelection *rect, F32 x, F32 y, F32 w, F32 h, Color color, Color rolloverColor)
{
	rect->color = color;
	rect->rolloverColor = rolloverColor;
	rect->minWidth = rect->minHeight = 1;
	rect->maxWidth = rect->maxHeight = 9e9;
	ui_WidgetInitialize((UIWidget *)rect, ui_RectangularSelectionTick, ui_RectangularSelectionDraw, ui_RectangularSelectionFreeInternal, NULL, ui_WidgetDummyFocusFunc);
	ui_WidgetSetPosition((UIWidget *)rect, x, y);
	ui_WidgetSetDimensions((UIWidget *)rect, w, h);
}

void ui_RectangularSelectionFreeInternal(UIRectangularSelection *rect)
{
	ui_WidgetFreeInternal((UIWidget *)rect);
}

void ui_RectangularSelectionSetResizeFinishedCallback(SA_PRE_NN_VALID SA_POST_P_FREE UIRectangularSelection *rect, UIActivationFunc resizeFinishedF, UserData resizeFinishedData)
{
	rect->resizeFinishedF = resizeFinishedF;
	rect->resizeFinishedData = resizeFinishedData;
}
