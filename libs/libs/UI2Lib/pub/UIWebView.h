#pragma once 

GCC_SYSTEM

#include "UICore.h"

typedef struct HTMLViewer HTMLViewer;

typedef struct UIWebView
{
	UI_INHERIT_FROM(UI_WIDGET_TYPE);

	char *homepage;
	HTMLViewer *viewer;
	F32 vW;
	F32 vH;

	F32 lastW;
	F32 lastH;
	U32 viewportFill : 1;
} UIWebView;

#define UI_WEBVIEW_TYPE UIWebView webview;

SA_RET_NN_VALID UIWebView* ui_WebViewCreate(const char* homepage, F32 viewportW, F32 viewportH);
void ui_WebViewInitialize(SA_PARAM_NN_VALID UIWebView *view, const char* homepage, F32 viewportW, F32 viewportH);
void ui_WebViewFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UIWebView* view);

void ui_WebViewDraw(SA_PARAM_NN_VALID UIWebView* view, UI_PARENT_ARGS);
void ui_WebViewTick(UIWebView *view, UI_PARENT_ARGS);
bool ui_WebViewInput(UIWebView *view, KeyInput *input);
void ui_WebViewFocus(UIWebView *view, UIAnyWidget* focused);

void ui_WebViewSetViewDims(SA_PARAM_NN_VALID UIWebView *view, F32 viewportW, F32 viewportH);