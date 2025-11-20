#include "earray.h"
#include "UIGenScrollView.h"
#include "UICore_h_ast.h"
#include "UIGen_h_ast.h"
#include "UIGenScrollView_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

void ui_GenLayoutEarlyScrollView(UIGen *pGen)
{
	UIGenScrollView *pView = UI_GEN_RESULT(pGen, ScrollView);
	UIGenScrollViewState *pState = UI_GEN_STATE(pGen, ScrollView);
	pState->RealScreenBox = pGen->ScreenBox;
	ui_GenScrollbarBox(&pView->Scrollbar, &pState->Scrollbar, &pGen->ScreenBox,  &pGen->ScreenBox, pGen->fScale);
	pGen->ScreenBox.lx += pView->iVirtualLeftPadding * pGen->fScale;
	pGen->ScreenBox.hx -= pView->iVirtualRightPadding * pGen->fScale;
	CBoxMoveY(&pGen->ScreenBox, pGen->ScreenBox.ly - (pState->Scrollbar.fScrollPosition - pView->iVirtualTopPadding * pGen->fScale));
	pGen->bUseEstimatedSize = true;
}

static bool GenScrollViewFindBottom(UIGen *pChild, F32 *pfMax)
{
	MAX1(*pfMax, pChild->UnpaddedScreenBox.hy);
	return false;
}

void ui_GenLayoutLateScrollView(UIGen *pGen)
{
	UIGenScrollView *pView = UI_GEN_RESULT(pGen, ScrollView);
	UIGenScrollViewState *pState = UI_GEN_STATE(pGen, ScrollView);
	F32 fBottom = 0;
	F32 fTotalHeight = 0.f;
	ui_GenInternalForEachChild(&pView->polyp, GenScrollViewFindBottom, &fBottom, UI_GEN_LAYOUT_ORDER);
	fTotalHeight = (fBottom + (pView->iVirtualTopPadding + pView->iVirtualBottomPadding) * pGen->fScale) - pGen->ScreenBox.ly;
	pGen->ScreenBox = pState->RealScreenBox;
	ui_GenLayoutScrollbar(pGen, &pView->Scrollbar, &pState->Scrollbar, fTotalHeight);
}

void ui_GenTickEarlyScrollView(UIGen *pGen)
{
	UIGenScrollView *pView = UI_GEN_RESULT(pGen, ScrollView);
	UIGenScrollViewState *pState = UI_GEN_STATE(pGen, ScrollView);
	ui_GenTickScrollbar(pGen, &pView->Scrollbar, &pState->Scrollbar);
}

void ui_GenDrawEarlyScrollView(UIGen *pGen)
{
	UIGenScrollView *pView = UI_GEN_RESULT(pGen, ScrollView);
	UIGenScrollViewState *pState = UI_GEN_STATE(pGen, ScrollView);
	ui_GenDrawScrollbar(pGen, &pView->Scrollbar, &pState->Scrollbar);
}

void ui_GenHideScrollView(UIGen *pGen)
{
	UIGenScrollView *pView = UI_GEN_RESULT(pGen, ScrollView);
	UIGenScrollViewState *pState = UI_GEN_STATE(pGen, ScrollView);
	ui_GenHideScrollbar(pGen, pView ? &pView->Scrollbar : NULL, &pState->Scrollbar);
}

AUTO_RUN;
void ui_GenRegisterScrollView(void)
{
	ui_GenRegisterType(kUIGenTypeScrollView, 
		UI_GEN_NO_VALIDATE, 
		UI_GEN_NO_POINTERUPDATE,
		UI_GEN_NO_UPDATE,
		ui_GenLayoutEarlyScrollView, 
		ui_GenLayoutLateScrollView,
		ui_GenTickEarlyScrollView, 
		UI_GEN_NO_TICKLATE,
		ui_GenDrawEarlyScrollView, 
		UI_GEN_NO_FITCONTENTSSIZE, 
		UI_GEN_NO_FITPARENTSIZE, 
		ui_GenHideScrollView, 
		UI_GEN_NO_INPUT, 
		UI_GEN_NO_UPDATECONTEXT, 
		UI_GEN_NO_QUEUERESET);
}


#include "UIGenScrollView_h_ast.c"