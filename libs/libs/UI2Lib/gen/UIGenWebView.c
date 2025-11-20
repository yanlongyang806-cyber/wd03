#include "UICore_h_ast.h"
#include "UIGen.h"
#include "UIGen_h_ast.h"

#include "Expression.h"
#include "GfxSprite.h"
#include "HTMLViewer.h"
#include "inputMouse.h"
#include "inputText.h"
#include "MemoryPool.h"

#include "UIGenWebView.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

MP_DEFINE(UIGenWebView);

void ui_GenFitContentsSizeWebView(UIGen *pGen, UIGenWebView *pView, CBox *pOut)
{
	// The native size of a webview is the size of the web window.
	Vec2 size;
	UIGenWebViewState *pState = UI_GEN_STATE(pGen, WebView);

	if(pState->view)
	{
		hv_GetDimensions(pState->view, size);
		BuildCBox(pOut, 0, 0, size[0], size[1]);
	}
	else 
		BuildCBox(pOut, 0, 0, 0, 0);
}

void ui_GenUpdateWebView(UIGen *pGen)
{
	UIGenWebView *pView = UI_GEN_RESULT(pGen, WebView);
	UIGenWebViewState *pState = UI_GEN_STATE(pGen, WebView);

	if (pView->pchWebSite && (!pState->pchLastWebSite || strcmp(pView->pchWebSite, pState->pchLastWebSite)))
	{
		StructCopyString(&pState->pchLastWebSite, pView->pchWebSite);
		if(pState->view)
			hv_LoadRemote(pState->view, pState->pchLastWebSite);
	}

	if(pState->hidden)
		pState->hidden = false;
}

void ui_GenLayoutLateWebView(UIGen *pGen)
{
	UIGenWebViewState *pState = UI_GEN_STATE(pGen, WebView);
	F32 x, y, w, h;

	x = pGen->ScreenBox.lx;
	y = pGen->ScreenBox.ly;
	w = MAX(pGen->ScreenBox.hx - x, 4);
	h = MAX(pGen->ScreenBox.hy - y, 4);

	if(!pState->view)
	{
		pState->vW = w;
		pState->vH = h;
		hv_CreateViewer(&pState->view, pState->vW, pState->vH, pState->pchLastWebSite);
	}
	else if((fabs(w - pState->vW) > 1 || fabs(h - pState->vH) > 1))
	{
		hv_Resize(pState->view, w, h);
		pState->vW = w;
		pState->vH = h;
	}
}

void ui_GenWebViewS2V(UIGenWebViewState *view, F32 x, F32 y, F32 w, F32 h, F32 sx, F32 sy, F32 *vx, F32 *vy)
{
	*vx = (sx - x) / w * view->vW;
	*vy = (sy - y) / h * view->vH;
}

void ui_GenWebViewV2S(UIGenWebViewState *view, F32 x, F32 y, F32 w, F32 h, F32 vx, F32 vy, F32 *sx, F32 *sy)
{
	*sx = 0;
	*sy = 0;
}

void ui_GenTickEarlyWebView(UIGen *pGen)
{
	UIGenWebView *pView = UI_GEN_RESULT(pGen, WebView);
	UIGenWebViewState *pState = UI_GEN_STATE(pGen, WebView);
	F32 x, y, w, h;

	if (!pState->view)
		return;

	x = pGen->ScreenBox.lx;
	y = pGen->ScreenBox.ly;
	w = pGen->ScreenBox.hx - x;
	h = pGen->ScreenBox.hy - y;

	if(mouseCollision(&pGen->ScreenBox))                                           
	{
		S32 sx, sy;
		F32 vx, vy;
		int key = -1;
		mousePos(&sx, &sy);
		ui_GenWebViewS2V(pState, x, y, w, h, sx, sy, &vx, &vy);
		hv_InjectMousePos(pState->view, vx, vy);

		if(mouseDown(MS_LEFT))
			key = INP_LBUTTON;
		else if(mouseDown(MS_RIGHT))
			key = INP_RBUTTON;
		else if(mouseDown(MS_MID))
			key = INP_MBUTTON;
		else if(mouseClick(MS_WHEELUP))
			key = INP_MOUSEWHEEL_FORWARD;
		else if(mouseClick(MS_WHEELDOWN))
			key = INP_MOUSEWHEEL_BACKWARD;

		if(key != -1)
		{
			ui_GenSetFocus(pGen);
			hv_Focus(pState->view);
			hv_InjectMouseDown(pState->view, vx, vy, key);
		}

		if(mouseUp(MS_LEFT))
			hv_InjectMouseUp(pState->view, vx, vy, INP_LBUTTON);
		else if(mouseUp(MS_RIGHT))
			hv_InjectMouseUp(pState->view, vx, vy, INP_RBUTTON);
		else if(mouseUp(MS_MID))
			hv_InjectMouseUp(pState->view, vx, vy, INP_MBUTTON);
	}
}

bool ui_GenInputWebView(UIGen *pGen, KeyInput *pKey)
{
	UIGenWebViewState *pState = UI_GEN_STATE(pGen, WebView);

	if (!pState->view)
		return false;

	hv_InjectKey(pState->view, pKey);

	return true;
}

void ui_GenDrawEarlyWebView(UIGen *pGen)
{
	UIGenWebViewState *pState = UI_GEN_STATE(pGen, WebView);
	BasicTexture *tex = NULL;
	F32 x, y, w, h, z;

	x = pGen->ScreenBox.lx;
	y = pGen->ScreenBox.ly;
	w = pGen->ScreenBox.hx - x;
	h = pGen->ScreenBox.hy - y;
	z = UI_GET_Z();
	
	hv_Render(pState->view, false);

	if(hv_GetTexture(pState->view, &tex))
	{
		display_sprite_tex(tex, x, y, z, w/pState->vW, h/pState->vH, 0xffffffff);
	}
}

void ui_GenHideWebVew(UIGen *pGen)
{
	UIGenWebViewState *pState = UI_GEN_STATE(pGen, WebView);

	pState->hidden = true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenWebViewLoadRemoteURL);
ExprFuncReturnVal ui_GenExprWebViewLoadRemoteURL(SA_PARAM_NN_VALID UIGen *pGen, const char* url)
{
	UIGenWebViewState *pState = UI_GEN_IS_TYPE(pGen, kUIGenTypeWebView) ? UI_GEN_STATE(pGen, WebView) : NULL;

	if(!pState)
		return ExprFuncReturnError;

	hv_LoadRemote(pState->view, url);

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenWebViewLoadLocalURL);
ExprFuncReturnVal ui_GenExprWebViewLoadLocalURL(SA_PARAM_NN_VALID UIGen *pGen, const char* url)
{
	UIGenWebViewState *pState = UI_GEN_IS_TYPE(pGen, kUIGenTypeWebView) ? UI_GEN_STATE(pGen, WebView) : NULL;

	if(!pState)
		return ExprFuncReturnError;

	hv_LoadLocal(pState->view, url);

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenWebViewGetURL);
const char *ui_GenExprWebViewGetURL(SA_PARAM_NN_VALID UIGen *pGen)
{
	UIGenWebViewState *pState = UI_GEN_IS_TYPE(pGen, kUIGenTypeWebView) ? UI_GEN_STATE(pGen, WebView) : NULL;
	const char *url = NULL;

	if(pState && pState->view)
		hv_GetMainURL(pState->view, &url);

	return url;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenWebViewReload);
void ui_GenExprWebViewReload(SA_PARAM_NN_VALID UIGen *pGen, bool bNoCache)
{
	//hv_Reload(pState->view);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenWebViewStopLoading);
void ui_GenExprWebViewStopLoading(SA_PARAM_NN_VALID UIGen *pGen)
{
	//hv_StopLoading(pState->view);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenWebViewIsLoading);
bool ui_GenExprWebViewIsLoading(SA_PARAM_NN_VALID UIGen *pGen)
{
	UIGenWebViewState *pState = UI_GEN_IS_TYPE(pGen, kUIGenTypeWebView) ? UI_GEN_STATE(pGen, WebView) : NULL;
	return pState && pState->view && hv_IsLoading(pState->view);
}

AUTO_EXPR_FUNC(util) ACMD_NAME(GenWebViewIsEnabled);
bool ui_GenExprWebViewIsEnabled(void)
{
	return gConf.bHTMLViewerEnabled;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenWebViewAdjustHistory);
void ui_GenExprWebViewMoveHistory(SA_PARAM_NN_VALID UIGen *pGen, int iAmount)
{
	// iAmount == 1 := forward 1 page
	// iAmount == -1 := backward 1 page
	//hv_Back(pState->view);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenWebViewCanAdjustHistory);
bool ui_GenExprWebViewCanBack(SA_PARAM_NN_VALID UIGen *pGen, int iAmount)
{
	// iAmount == 1 := forward 1 page
	// iAmount == -1 := backward 1 page
	//return hv_HasHistoryBack(pState->view);
	return false;
}

AUTO_RUN;
void ui_GenRegisterWebView2(void)
{
	MP_CREATE(UIGenWebView, 64);
	ui_GenRegisterType(	kUIGenTypeWebView, 
						UI_GEN_NO_VALIDATE, 
						UI_GEN_NO_POINTERUPDATE,
						ui_GenUpdateWebView, 
						UI_GEN_NO_LAYOUTEARLY, 
						ui_GenLayoutLateWebView, 
						ui_GenTickEarlyWebView, 
						UI_GEN_NO_TICKLATE, 
						ui_GenDrawEarlyWebView,
						ui_GenFitContentsSizeWebView, 
						UI_GEN_NO_FITPARENTSIZE, 
						ui_GenHideWebVew, 
						ui_GenInputWebView, 
						UI_GEN_NO_UPDATECONTEXT, 
						UI_GEN_NO_QUEUERESET);
}

#include "UIGenWebView_h_ast.c"