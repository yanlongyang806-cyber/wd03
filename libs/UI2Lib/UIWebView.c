#include "UIWebView.h"

#include "GfxSprite.h"
#include "HTMLViewer.h"
#include "inputMouse.h"
#include "inputText.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

SA_RET_NN_VALID UIWebView* ui_WebViewCreate(const char* homepage, F32 viewportW, F32 viewportH)
{
	UIWebView* view = calloc(1, sizeof(UIWebView));
	ui_WebViewInitialize(view, homepage, viewportW, viewportH);

	return view;
}

void ui_WebViewInitialize(SA_PARAM_NN_VALID UIWebView *view, const char* homepage, F32 viewportW, F32 viewportH)
{
	ui_WidgetInitialize(UI_WIDGET(view), ui_WebViewTick, ui_WebViewDraw, ui_WebViewFreeInternal, ui_WebViewInput, ui_WebViewFocus);

	view->homepage = strdup(homepage);
	view->vW = viewportW;
	view->vH = viewportH;
	if(!homepage)
		homepage = "http://www.google.com";
	hv_CreateViewer(&view->viewer, view->vW, view->vH, homepage);

	hv_Resize(view->viewer, view->vW, view->vH);
	view->lastW = view->vW;
	view->lastH = view->vH;
}

void ui_WebViewS2V(UIWebView *view, UI_MY_ARGS, F32 sx, F32 sy, F32 *vx, F32 *vy)
{
	*vx = (sx - x) / w * view->vW;
	*vy = (sy - y) / h * view->vH;
}

void ui_WebViewV2S(UIWebView *view, UI_MY_ARGS, F32 vx, F32 vy, F32 *sx, F32 *sy)
{
	*sx = 0;
	*sy = 0;
}

bool ui_WebViewInput(UIWebView *view, KeyInput *input)
{
	hv_InjectKey(view->viewer, input);
	
	return true;
}

void ui_WebViewFocus(UIWebView *view, UIAnyWidget* focused)
{
	if(view == focused)
		hv_Focus(view->viewer);
	else
		hv_Unfocus(view->viewer);
}

void ui_WebViewTick(UIWebView *view, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(view);
	ui_WidgetSizerLayout(UI_WIDGET(view), w, h);

	if(view->viewportFill && (fabs(w - view->vW) > 1 || fabs(h - view->vH) > 1))
	{
		hv_Resize(view->viewer, w, h);
		view->vW = w;
		view->vH = h;
	}

	CBoxClipTo(&pBox, &box);                                            
	if(mouseCollision(&box))                                           
	{
		S32 sx, sy;
		F32 vx, vy;
		int key = -1;
		mousePos(&sx, &sy);
		ui_WebViewS2V(view, UI_MY_VALUES, sx, sy, &vx, &vy);
		hv_InjectMousePos(view->viewer, vx, vy);

		if(mouseDown(MS_LEFT))
			key = INP_LBUTTON;
		else if(mouseDown(MS_RIGHT))
			key = INP_RBUTTON;
		else if(mouseDown(MS_MID))
			key = INP_MBUTTON;
		else if(mouseDown(MS_WHEELUP))
			key = INP_MOUSEWHEEL_FORWARD;
		else if(mouseDown(MS_WHEELDOWN))
			key = INP_MOUSEWHEEL_BACKWARD;

		if(key != -1)
		{
			ui_SetFocus(view);
			hv_Focus(view->viewer);
			hv_InjectMouseDown(view->viewer, vx, vy, key);
		}

		if(mouseUp(MS_LEFT))
			hv_InjectMouseUp(view->viewer, vx, vy, INP_LBUTTON);
		else if(mouseUp(MS_RIGHT))
			hv_InjectMouseUp(view->viewer, vx, vy, INP_RBUTTON);
		else if(mouseUp(MS_MID))
			hv_InjectMouseUp(view->viewer, vx, vy, INP_MBUTTON);

		inpHandled();
	}
}

void ui_WebViewFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UIWebView* view)
{
	hv_Destroy(&view->viewer);
	free(view->homepage);
	
	ui_WidgetFreeInternal(UI_WIDGET(view));
}

void ui_WebViewDraw(SA_PARAM_NN_VALID UIWebView* view, UI_PARENT_ARGS)
{
	BasicTexture *tex = NULL;
	UI_GET_COORDINATES(view);
	UI_DRAW_EARLY(pEntry);

	hv_Render(view->viewer, false);

	if(hv_GetTexture(view->viewer, &tex))
		display_sprite_tex(tex, x, y, z, w/view->vW, h/view->vH, 0xffffffff);

	UI_DRAW_LATE(view);
}
