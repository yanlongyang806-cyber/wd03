#pragma once

#include "UIGen.h"

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UIGenWebView
{
	UIGenInternal polyp; AST(POLYCHILDTYPE(kUIGenTypeWebView))

	const char* pchWebSite;
} UIGenWebView;

AUTO_STRUCT;
typedef struct UIGenWebViewState
{
	UIGenPerTypeState polyp; AST(POLYCHILDTYPE(kUIGenTypeWebView))

	HTMLViewer *view;	NO_AST
	F32 vW;
	F32 vH;
	char *pchLastWebSite;
	
	U32 hidden : 1;
} UIGenWebViewState;
