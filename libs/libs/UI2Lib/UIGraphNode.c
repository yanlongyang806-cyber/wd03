/***************************************************************************



***************************************************************************/


#include "earray.h"
#include "Color.h"

#include "GfxClipper.h"
#include "GfxSprite.h"
#include "GfxTexAtlas.h"
#include "GraphicsLib.h"

#include "inputLib.h"
#include "inputMouse.h"
#include "inputKeyBind.h"
#include "inputText.h"

#include "UIDnD.h"
#include "UIFocus.h"
#include "UIGraphNode.h"
#include "UISkin.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

void ui_GraphNodeDraw(UIGraphNode *pGraphNode, UI_PARENT_ARGS);
bool ui_GraphNodeInput(UIGraphNode *pGraphNode, KeyInput *key);
void ui_GraphNodeTickInternal(UIGraphNode *pGraphNode, UI_PARENT_ARGS);

UIGraphNode *ui_GraphNodeCreate(const char *title, F32 x, F32 y, F32 w, F32 h, UISkin *pSelSkin, UISkin *pNoSelSkin)
{
	UIGraphNode *pGraphNode = calloc(1, sizeof(UIGraphNode));

	//pGraphNode->window = ui_WindowCreate(title, x, y, w, h);

	ui_GraphNodeInitialize(pGraphNode, title, x, y, w, h);

	pGraphNode->pSelSkin = pSelSkin;
	pGraphNode->pNoSelSkin = pNoSelSkin;

	ui_GraphNodeSetSelected(pGraphNode, false);

	return pGraphNode;
}

void ui_GraphNodeInitialize(UIGraphNode *pGraphNode, const char *title, F32 x, F32 y, F32 w, F32 h)
{
	ui_WindowInitializeEx(&pGraphNode->window, title, x, y, w, h MEM_DBG_PARMS_INIT);

	// override callbacks
	ui_WidgetInitialize(&pGraphNode->window.widget, ui_GraphNodeTickInternal, ui_GraphNodeDraw, ui_GraphNodeFreeInternal, ui_GraphNodeInput, NULL);
}

void ui_GraphNodeFreeInternal(UIGraphNode *pGraphNode)
{
	ui_WindowFreeInternal(&pGraphNode->window);

	//free(pGraphNode);
}

void ui_GraphNodeSetSelected(UIGraphNode *pGraphNode, bool bIsSelected)
{
	pGraphNode->bIsSelected = bIsSelected;

	if(bIsSelected)
	{
		pGraphNode->window.widget.pOverrideSkin = pGraphNode->pSelSkin;
	}
	else
	{
		pGraphNode->window.widget.pOverrideSkin = pGraphNode->pNoSelSkin;
	}
}

void ui_GraphNodeClose(UIGraphNode *pGraphNode)
{
	ui_WindowClose(&pGraphNode->window);
}

void ui_GraphNodeSetOnInputCallback(UIGraphNode *pGraphNode, UIInputFunction fnInput, void *userData)
{
	pGraphNode->fnOnInput = fnInput;
	pGraphNode->onInputUserData = userData;
}

void ui_GraphNodeSetOnMouseDownCallback(UIGraphNode *pGraphNode, UIGraphNodeOnMouseDownFunc fnInput, void *userData)
{
	pGraphNode->fnOnMouseDown = fnInput;
	pGraphNode->onMouseDownUserData = userData;
}

void ui_GraphNodeSetOnMouseDragCallback(UIGraphNode *pGraphNode, UIGraphNodeOnMouseDragFunc fnInput, void *userData)
{
	pGraphNode->fnOnMouseDrag = fnInput;
	pGraphNode->onMouseDragUserData = userData;
}

void ui_GraphNodeSetOnMouseUpCallback(UIGraphNode *pGraphNode, UIGraphNodeOnMouseUpFunc fnInput, void *userData)
{
	pGraphNode->fnOnMouseUp = fnInput;
	pGraphNode->onMouseUpUserData = userData;
}

bool ui_GraphNodeInput(UIGraphNode *pGraphNode, KeyInput *key)
{
	UIWindow *window = &pGraphNode->window;
	bool bResult = false;

	if(pGraphNode->fnOnInput)
	{
		bResult = pGraphNode->fnOnInput(pGraphNode, key);
	}

	return !bResult ? ui_WindowInput(window, key) : bResult;
}

void ui_GraphNodeTitleDraw(UIGraphNode *pGraphNode, UI_MY_ARGS, F32 z)
{
	UIWindow *window = &pGraphNode->window;

	bool bActive = (UI_WIDGET(window)->group != NULL && eaGet(UI_WIDGET(window)->group, 0) == UI_WIDGET(window)) ? true : false;
	UIStyleBorder *pBorder = RefSystem_ReferentFromString( "UIStyleBorder", "Default_WindowFrame" );
	UIStyleBar *pBar = GET_REF(UI_GET_SKIN(window)->hTitlebarBar);
	UIStyleFont *pFont = ui_StyleBarGetFont(pBar);
	F32 fHeight = ui_StyleBarGetHeight(pBar) * scale;
	F32 fTopPad = ui_StyleBorderTopSize(pBorder) * scale;

	Color button = UI_GET_SKIN(window)->button[bActive];
	CBox barBox = {x - ui_StyleBarGetLeftPad(pBar) * scale, y - (fTopPad + fHeight), x + w + ui_StyleBarGetRightPad(pBar) * scale, y - fTopPad};
	F32 fCenterY = (barBox.ly + barBox.hy) / 2;
	F32 fRightSide = x + w;
	S32 i;

	//if(pBar && pGraphNode->bIsSelected)
	//{
	//	pBar->uiLeftEmptyColor = pBar->uiLeftFilledColor = 0x0099ffFF;
	//	pBar->uiRightEmptyColor = pBar->uiRightFilledColor = 0x0099ffFF;
	//}
	//else if(pBar)
	//{
	//	pBar->uiLeftEmptyColor = pBar->uiLeftFilledColor = 0x999999FF;
	//	pBar->uiRightEmptyColor = pBar->uiRightFilledColor = 0x999999FF;
	//}

	if (pBar)
		ui_StyleBarDraw(pBar, &barBox, bActive ? 1 : 0, -1, -1, 0, ui_WidgetGetText(UI_WIDGET(window)), z, 0xFF, true, scale, NULL);

	for (i = eaSize(&window->buttons) - 1; i >= 0; i--)
	{
		UITitleButton *tbutton = window->buttons[i];
		AtlasTex *texture = ui_WindowGetButtonTex(window, tbutton);
		CBox buttonBox;
		fRightSide -= texture->width * scale;
		BuildCBox(&buttonBox, fRightSide, fCenterY - texture->height * scale / 2,
			texture->width * scale, texture->height * scale);
		display_sprite_box(texture, &buttonBox, z + 0.2, RGBAFromColor(button));
	}
}

void ui_GraphNodeDraw(UIGraphNode *pGraphNode, UI_PARENT_ARGS)
{
	UIWindow *window = &pGraphNode->window;
//	ui_WindowDraw(window, UI_PARENT_VALUES);

	UI_GET_COORDINATES(window);
	UI_WINDOW_SANITY_CHECK(window);
	bool bUseColor = true;
	UIStyleBorder *pBorder = RefSystem_ReferentFromString("UIStyleBorder", "Default_WindowFrame");
	F32 bottom_y = h + y;
	F32 right_x = w + x;
	F32 leftWidth = ui_StyleBorderLeftSize(pBorder) * scale;
	F32 rightWidth = ui_StyleBorderRightSize(pBorder) * scale;
	F32 bottomHeight = ui_StyleBorderBottomSize(pBorder) * scale;
	F32 topHeight = ui_StyleBorderTopSize(pBorder) * scale;
	int mouseX = g_ui_State.mouseX, mouseY = g_ui_State.mouseY;
	Color borderColor, bgColor;
	bool active = (UI_WIDGET(window)->group && (UIWindow *)(*UI_WIDGET(window)->group)[0] == window);
	CBox saneBox = {x,y,x+w,y+h};
	const char* widgetText = ui_WidgetGetText( UI_WIDGET( pGraphNode ));

	UIStyleBar *pBar = GET_REF(UI_GET_SKIN(window)->hTitlebarBar);
	pGraphNode->topPad = ((widgetText ? ui_StyleBarGetHeight(pBar) : 0) + ui_StyleBorderTopSize(pBorder)) * scale;

	if (!window->show)
		return;

	if (window->shaded)
	{
		ui_GraphNodeTitleDraw(pGraphNode, UI_MY_VALUES, z);
		return;
	}

	if (window->modal)
	{
		int width, height;
		gfxGetActiveSurfaceSize(&width, &height);
		display_sprite(white_tex_atlas, 0, 0, z-0.001, width/(F32)white_tex_atlas->width, height/(F32)white_tex_atlas->height, 0x00000050);
	}

	if (UI_GET_SKIN(window))
	{
		bgColor = (UI_GET_SKIN(window)->background[0]);
		borderColor = (UI_GET_SKIN(window)->border[!active]);
	}
	else
	{
		if (active)
		{
			bgColor = UI_WIDGET(window)->color[0];
			borderColor = UI_WIDGET(window)->color[1];
		}
		else
		{
			bgColor = ColorDarken(UI_WIDGET(window)->color[0], 64);
			borderColor = ColorDarken(UI_WIDGET(window)->color[1], 64);
		}
	}

	//if(pGraphNode->bIsSelected)
	//{
	//	//bgColor = ARGBToColor(0xFF0099ff);
	//	borderColor = ARGBToColor(0xFF0099ff);
	//}
	//else
	//{
	//	//bgColor = ARGBToColor(0xFF999999);
	//	borderColor = ARGBToColor(0xFF999999);
	//}

	if(bUseColor)
		ui_StyleBorderDrawOutside(pBorder, &saneBox, RGBAFromColor(borderColor), RGBAFromColor(bgColor), z, scale, 255);
	else 
		ui_StyleBorderDrawMagicOutside(pBorder, &saneBox, z, scale, 255);

	if (widgetText)
		ui_GraphNodeTitleDraw(pGraphNode, UI_MY_VALUES, z);

	clipperPushRestrict(&saneBox);
	ui_WidgetGroupDraw(&UI_WIDGET(window)->children, UI_MY_VALUES);
	clipperPop();
}

void ui_GraphNodeTitleTick(UIGraphNode *pGraphNode, UI_PARENT_ARGS, UI_MY_ARGS, F32 fullHeight)
{
	UIWindow *window = &pGraphNode->window;
	bool bActive = (UI_WIDGET(window)->group != NULL && eaGet(UI_WIDGET(window)->group, 0) == UI_WIDGET(window)) ? true : false;
	UIStyleBorder *pBorder = RefSystem_ReferentFromString("UIStyleBorder", "Default_WindowFrame");
	UIStyleBar *pBar = GET_REF(UI_GET_SKIN(window)->hTitlebarBar);
	UIStyleFont *pFont = ui_StyleBarGetFont(pBar);
	const char* widgetText = ui_WidgetGetText( UI_WIDGET( pGraphNode ));
	F32 leftPad = ui_StyleBarGetLeftPad(pBar) * scale,
		rightPad = ui_StyleBarGetRightPad(pBar) * scale,
		topPad = ((widgetText ? ui_StyleBarGetHeight(pBar) : 0) + ui_StyleBorderTopSize(pBorder)) * scale,
		bottomPad = ui_StyleBorderBottomSize(pBorder) * scale,
		height = ui_StyleBarGetHeight(pBar) * scale;
	CBox title = {x - leftPad, y - topPad, x + w + ui_StyleBarGetRightPad(pBar), y - ui_StyleBorderTopSize(pBorder) * scale};
	F32 top = y - topPad;

	if (widgetText)
	{
		S32 i;
		F32 rightSide = x + w;
		for (i = eaSize(&window->buttons) - 1; i >= 0; i--)
		{
			UITitleButton *button = window->buttons[i];
			AtlasTex *texture = ui_WindowGetButtonTex(window, button);
			CBox buttonBox;
			rightSide -= texture->width * scale;
			BuildCBox(&buttonBox, rightSide, top + height/2 - texture->height * scale / 2,
				texture->width * scale, texture->height * scale);
			if (mouseClickHit(MS_LEFT, &buttonBox) && button->callback)
			{
				button->callback(window, button->callbackData);
				inpHandled();
			}
			else if (mouseCollision(&buttonBox))
				ui_CursorLock();
		}
	}
	
	if(mouseDownHit(MS_MID, &title))
	{
		// mouse down test
		if(pGraphNode->fnOnMouseDown && !pGraphNode->bIsMiddleMouseDown)
		{	
			Vec2 mouseDownPoint;

			mouseDownPoint[0] = g_ui_State.mouseX;
			mouseDownPoint[1] = g_ui_State.mouseY;
			pGraphNode->bIsMiddleMouseDown = true;

			// mouse down only once
			pGraphNode->fnOnMouseDown(pGraphNode, mouseDownPoint, pGraphNode->onMouseDownUserData);
		}

		window->dragging = true;
	}

	if (widgetText && mouseDoubleClickHit(MS_LEFT, &title) && window->shadable && !window->modal)
	{
		//ui_WindowToggleShadedAndCallback(window);
		inpHandled();
	}
	else if (window->movable && mouseDownHit(MS_LEFT, &title))
	{
		ui_SetFocus(window);
		window->dragging = true;
		window->grabbedX = g_ui_State.mouseX - x;
		window->grabbedY = g_ui_State.mouseY - y;

		// mouse down test
		if(pGraphNode->fnOnMouseDown && !pGraphNode->bLeftMouseDown)
		{	
			Vec2 mouseDownPoint;

			mouseDownPoint[0] = g_ui_State.mouseX;
			mouseDownPoint[1] = g_ui_State.mouseY;
			pGraphNode->bLeftMouseDown = true;
			pGraphNode->bLeftMouseDoubleClicked = mouseDoubleClick(MS_LEFT);

			// mouse down only once
			pGraphNode->fnOnMouseDown(pGraphNode, mouseDownPoint, pGraphNode->onMouseDownUserData);
		}

		ui_SetCursorForDirection(UIAnyDirection);
		inpHandled();
	}
	else if (window->dragging)
	{
		if(pGraphNode->fnOnMouseDrag)
		{	
			Vec2 dragPoint;

			pGraphNode->pScale = pScale;

			dragPoint[0] = g_ui_State.mouseX;
			dragPoint[1] = g_ui_State.mouseY;

			pGraphNode->fnOnMouseDrag(pGraphNode, dragPoint, pGraphNode->onMouseDragUserData);
		}

		if(pGraphNode->bIsMiddleMouseDown)
		{
			if(!mouseIsDown(MS_MID))
			{
				Vec2 mouseUpPoint;

				window->dragging = false;

				mouseUpPoint[0] = g_ui_State.mouseX;
				mouseUpPoint[1] = g_ui_State.mouseY;

				// clear state flags
				pGraphNode->bIsMiddleMouseDown = false;

				// Mouse Up Callback (only once)
				if(pGraphNode->fnOnMouseUp)
				{
					pGraphNode->fnOnMouseUp(pGraphNode, mouseUpPoint, pGraphNode->onMouseUpUserData);
				}
			}
		}
		else
		{
			if(!mouseIsDown(MS_LEFT))
			{
				Vec2 mouseUpPoint;

				window->dragging = false;

				mouseUpPoint[0] = g_ui_State.mouseX;
				mouseUpPoint[1] = g_ui_State.mouseY;

				// clear state flags
				pGraphNode->bLeftMouseDown = false;
				pGraphNode->bLeftMouseDoubleClicked = false;

				// Mouse Up Callback (only once)
				if(pGraphNode->fnOnMouseUp)
				{
					pGraphNode->fnOnMouseUp(pGraphNode, mouseUpPoint, pGraphNode->onMouseUpUserData);
				}
			}
		}

		ui_SetCursorForDirection(UIAnyDirection);
		inpHandled();
	}
	else if (UI_WIDGET(window)->contextF && mouseClickHit(MS_RIGHT, &title))
	{
		UI_WIDGET(window)->contextF(UI_WIDGET(window), UI_WIDGET(window)->contextData);
		inpHandled();
	}
	else if (mouseCollision(&title))
	{
		if (window->movable)
			ui_SetCursorForDirection(UIAnyDirection);
		inpHandled();
	}

	// Sanity check the window position.
	if (window->resizable)
	{
		// Resizable windows stay so that the title bar is in bounds vertically
		// and so at least some margin is in bounds to the left and right
		if (UI_WIDGET(window)->offsetFrom == UITopRight ||
			UI_WIDGET(window)->offsetFrom == UIBottomRight ||
			UI_WIDGET(window)->offsetFrom == UIRight)
		{
			UI_WIDGET(window)->x = CLAMP(UI_WIDGET(window)->x, (MAX(3*UI_STEP,rightPad) - w) / pScale, (pW - MAX(3*UI_STEP,leftPad)) / pScale);
		} else {
			UI_WIDGET(window)->x = CLAMP(UI_WIDGET(window)->x, (MAX(3*UI_STEP,leftPad) - w) / pScale, (pW - MAX(3*UI_STEP,rightPad)) / pScale);
		}
		if (UI_WIDGET(window)->offsetFrom == UIBottomLeft ||
			UI_WIDGET(window)->offsetFrom == UIBottomRight ||
			UI_WIDGET(window)->offsetFrom == UIBottom)
		{
			UI_WIDGET(window)->y = CLAMP(UI_WIDGET(window)->y, bottomPad / pScale, (pH - (h + topPad)) / pScale);
		} else {
			UI_WIDGET(window)->y = CLAMP(UI_WIDGET(window)->y, topPad / pScale, pH / pScale);
		}
	}
	else if (widgetText)
	{
		// Non-resizable windows with a title stay entirely without the bounds of the parent
		UI_WIDGET(window)->x = CLAMP(UI_WIDGET(window)->x, 0.f, (pW - w) / pScale);
		UI_WIDGET(window)->y = CLAMP(UI_WIDGET(window)->y, topPad, (pH - h) / pScale);			
	}
	else
	{
		// Non-resizable windows without a title stay entirely without the bounds of the parent
		UI_WIDGET(window)->x = CLAMP(UI_WIDGET(window)->x, 0.f, (pW - w) / pScale);
		UI_WIDGET(window)->y = CLAMP(UI_WIDGET(window)->y, 0.f, (pH - h) / pScale);
	}
}

void ui_GraphNodeTickInternal(UIGraphNode *pGraphNode, UI_PARENT_ARGS)
{
	UIWindow *window = &pGraphNode->window;

	UI_GET_COORDINATES(window);
	UI_WINDOW_SANITY_CHECK(window);
	bool bTopModal = false;

	if (!window->show)
		return;

	CBoxClipTo(&pBox, &box);
	mouseClipPushRestrict(&box);

	devassertmsg(w < 100 * g_ui_State.screenWidth, "GraphNode width is out of control!");
	devassertmsg(h < 100 * g_ui_State.screenHeight, "GraphNode height is out of control!");

	if (window->shaded)
	{
		mouseClipPop();
		ui_GraphNodeTitleTick(pGraphNode, UI_PARENT_VALUES, x, y, w, 0, scale, h);
		//ui_WindowTitleTick(window, UI_PARENT_VALUES, x, y, w, 0, scale, h);
		ui_WindowCheckResizing(window, UI_PARENT_VALUES, x, y, w, 0, scale);
		if (bTopModal)
		{
			ui_CursorLock();
			inpHandled();
		}
		return;
	}
	else if (mouseDownHit(MS_LEFT, &box) && UI_WIDGET(window)->group && (UIWindow *)(*UI_WIDGET(window)->group)[0] != window)
	{
		ui_WidgetGroupSteal(UI_WIDGET(window)->group, UI_WIDGET(window));
		if (window->raisedF)
			window->raisedF(window, window->raisedData);
	}

	mouseClipPop();

	//ui_WindowTitleTick(window, UI_PARENT_VALUES, UI_MY_VALUES, h);
	ui_GraphNodeTitleTick(pGraphNode, UI_PARENT_VALUES, UI_MY_VALUES, h);
	ui_WindowCheckResizing(window, UI_PARENT_VALUES, UI_MY_VALUES);

	mouseClipPushRestrict(&box);

	ui_WidgetGroupTick(&UI_WIDGET(window)->children, UI_MY_VALUES);
	ui_WidgetCheckStartDrag(UI_WIDGET(window), &box);
	ui_WidgetCheckDrop(UI_WIDGET(window), &box);

	if (mouseDownHit(MS_LEFT, &box) && window->clickedF)
		window->clickedF(window, window->clickedData);

	if (UI_WIDGET(window)->contextF && mouseClickHit(MS_RIGHT, &box))
	{
		UI_WIDGET(window)->contextF(UI_WIDGET(window), UI_WIDGET(window)->contextData);
		inpHandled();
	}

	if (mouseCollision(&box))
	{
		ui_CursorLock();
		inpHandled();
	}

	mouseClipPop();

	if (bTopModal)
	{
		ui_CursorLock();
		inpHandled();
	}
}
