/***************************************************************************



***************************************************************************/


#include "earray.h"
#include "Cbox.h"

#include "UICore.h"
#include "UIFocus.h"
#include "UIInternal.h"

static bool findWidgetPos(UIWidget *groupWidget, UIWidget *search, F32 *retx, F32 *rety, F32 *retw, F32 *reth, UI_PARENT_ARGS)
{
	int i;
	UI_GET_COORDINATES((UIWidgetWidget*)groupWidget);
	if (groupWidget == search) {
		*retx = x;
		*rety = y;
		if (retw)
			*retw = w;
		if (reth)
			*reth = h;
		return true;
	}
	for (i=eaSize(&groupWidget->children)-1; i>=0; i--)
		if (groupWidget->children[i] && findWidgetPos(groupWidget->children[i], search, retx, rety, retw, reth, UI_MY_VALUES))
			return true;
	return false;
}

typedef struct UIWidgetSearchState
{
	UIWidget *best;
	F32 bestx, besty;
} UIWidgetSearchState;

static void findNextWidgetByPos(UIWidget *groupWidget, F32 searchx, F32 searchy, UIWidget *ignoreWidget, UIWidgetSearchState *searchState, bool next, UI_PARENT_ARGS)
{
	int i;
	UI_GET_COORDINATES((UIWidgetWidget*)groupWidget);
	if (!ui_IsActive(groupWidget))
		return;
	if (groupWidget != ignoreWidget && groupWidget->focusF) {
		if (next && (y == searchy && x > searchx ||
			y > searchy) ||
			!next && (y == searchy && x < searchx ||
			y < searchy))
		{
			// Valid to be "next"
			if (!searchState->best ||
				next && (y < searchState->besty ||
				y == searchState->besty && x < searchState->bestx) ||
				!next && (y > searchState->besty ||
				y == searchState->besty && x > searchState->bestx))
			{
				searchState->best = groupWidget;
				searchState->bestx = x;
				searchState->besty = y;
			}
		}
	}
	if (groupWidget->childrenInactive)
		return;
	for (i=eaSize(&groupWidget->children)-1; i>=0; i--)
		if (groupWidget->children[i])
			findNextWidgetByPos(groupWidget->children[i], searchx, searchy, ignoreWidget, searchState, next, UI_MY_VALUES);
}


static UIWidget *ui_FocusNextPrevInGroup(UIWidget *groupWidget, UIWidget *oldFocus, bool next)
{
	F32 retx=-1, rety=-1;
	F32 x=0, y=0, scale=1;
	int w = g_ui_State.screenWidth, h = g_ui_State.screenHeight;
	UIWidgetSearchState searchState = {0};
	// Find x/y position of this widget relative to the group
	if (!findWidgetPos(groupWidget, oldFocus, &retx, &rety, NULL, NULL, UI_MY_VALUES)) {
		// This widget was not found in the tree, just get the first one
		retx=rety=0;
		oldFocus = NULL;
	}
	// Find the next widget based on those coords
	ZeroStruct(&searchState);
	findNextWidgetByPos(groupWidget, retx, rety, oldFocus, &searchState, next, UI_MY_VALUES);
	if (!searchState.best) {
		ZeroStruct(&searchState);
		findNextWidgetByPos(groupWidget, next?0:w, next?0:h, oldFocus, &searchState, next, UI_MY_VALUES);
	}
	return searchState.best?searchState.best:oldFocus; // Stay with self if nothing was found
}

UIWidget *ui_FocusNextInGroup(UIWidget *groupWidget, UIWidget *oldFocus)
{
	return ui_FocusNextPrevInGroup(groupWidget, oldFocus, true);
}

UIWidget *ui_FocusPrevInGroup(UIWidget *groupWidget, UIWidget *oldFocus)
{
	return ui_FocusNextPrevInGroup(groupWidget, oldFocus, false);
}

static void ui_FindNearestCullByDirection(UIWidgetGroup *pGroup, UIWidget *pOrigin, Vec2 v2Origin, UIWidget **ppBest, Vec2 v2Best, UIDirection eDirection, UI_PARENT_ARGS)
{
	S32 i;
	for (i = 0; i < eaSize(pGroup); i++)
	{
		UIWidget *pCheckWidget = (*pGroup)[i];
		F32 fCheckX, fCheckY, fCheckW, fCheckH, fCheckScale;
		Vec2 v2Check;
		bool bBeatsBest = false;
		if (!pCheckWidget || !ui_IsVisible(pCheckWidget) || !ui_IsActive(pCheckWidget))
			continue;

		fCheckX = ui_WidgetXPosition(pCheckWidget, pX, pW, 1.f);
		fCheckY = ui_WidgetYPosition(pCheckWidget, pY, pH, 1.f);
		fCheckW = ui_WidgetWidth(pCheckWidget, pW, 1.f);
		fCheckH = ui_WidgetHeight(pCheckWidget, pH, 1.f);
		fCheckScale = pCheckWidget->scale * pScale;

		if (!pCheckWidget->childrenInactive)
			ui_FindNearestCullByDirection(&pCheckWidget->children, pOrigin, v2Origin, ppBest, v2Best, eDirection, fCheckX, fCheckY, fCheckW, fCheckH, fCheckScale);

		switch (eDirection)
		{
		case UITop:
			// Ignore things that have a center below the origin.
			// Prefer things with a bottom near the origin.
			if (fCheckY + fCheckH / 2 >= v2Origin[1])
				continue;
			v2Check[0] = fCheckX + fCheckW / 2;
			v2Check[1] = fCheckY + fCheckH;
			break;
		case UIBottom:
			// Ignore things that have a center above the origin.
			// Prefer things with a top near the origin.
			if (fCheckY + fCheckH / 2 <= v2Origin[1])
				continue;
			v2Check[0] = fCheckX + fCheckW / 2;
			v2Check[1] = fCheckY;
			break;
		case UILeft:
			// Ignore things that have a center right of the origin.
			// Prefer things with a left side near the origin.
			if (fCheckX + fCheckW / 2 >= v2Origin[0])
				continue;
			v2Check[0] = fCheckX;
			v2Check[1] = fCheckY + fCheckH / 2;
			break;
		case UIRight:
			// Ignore things that have a center left of the origin.
			// Prefer things with a right side near the origin.
			if (fCheckX + fCheckW / 2 <= v2Origin[0])
				continue;
			v2Check[0] = fCheckX + fCheckW;
			v2Check[1] = fCheckY + fCheckH / 2;
			break;
		default:
			devassertmsg(false, "No cull direction given, focus will result in an infinite loop");
		}

		bBeatsBest = (distance2Squared(v2Best, v2Origin) > distance2Squared(v2Check, v2Origin));

		if (bBeatsBest && pCheckWidget != pOrigin && (pCheckWidget->inputF || pCheckWidget->focusF || pCheckWidget->onFocusF))
		{
			copyVec2(v2Check, v2Best);
			*ppBest = pCheckWidget;
		}
	}
}

static bool ui_FocusFindOrigin(UIWidget *pWidget, UIWidgetGroup *pGroup, F32 fParentWidth, F32 fParentHeight, Vec2 v2Result, UIDirection eDirection)
{
	F32 fStartX = 0.f, fStartY = 0.f, fStartW = 0.f, fStartH = 0.f;
	UIWidget pGroupDummy;
	ZeroStruct(&pGroupDummy);
	ui_WidgetInitialize(&pGroupDummy, NULL, NULL, ui_WidgetFreeInternal, NULL, NULL);
	ui_WidgetSetDimensions(&pGroupDummy, fParentWidth, fParentHeight);
	pGroupDummy.children = *pGroup;
	if (findWidgetPos(&pGroupDummy, pWidget, &fStartX, &fStartY, &fStartW, &fStartH, 0.f, 0.f, fParentWidth, fParentHeight, 1.f))
	{
		switch (eDirection)
		{
		case UITop:
			v2Result[0] = fStartX + fStartW / 2;
			v2Result[1] = fStartY;
			break;
		case UIBottom:
			v2Result[0] = fStartX + fStartW / 2;
			v2Result[1] = fStartY + fStartH;
			break;
		case UILeft:
			v2Result[0] = fStartX;
			v2Result[1] = fStartY + fStartH / 2;
			break;
		case UIRight:
			v2Result[0] = fStartX + fStartW;
			v2Result[1] = fStartY + fStartH / 2;
			break;
		}
		return true;
	}
	else
	{
		return false;
	}
}

UIWidget *ui_FocusUp(UIWidget *pWidget, UIWidgetGroup *pGroup, F32 fParentWidth, F32 fParentHeight)
{
	UIWidget *pBestWidget = pWidget;
	Vec2 v2Origin;
	Vec2 v2Start = {9e9, 9e9};
	if (!ui_FocusFindOrigin(pWidget, pGroup, fParentWidth, fParentHeight, v2Origin, UITop))
		return pWidget;
	ui_FindNearestCullByDirection(pGroup, pWidget, v2Origin, &pBestWidget, v2Start, UITop, 0.f, 0.f, fParentWidth, fParentHeight, 1.f);
	return pBestWidget;
}

UIWidget *ui_FocusDown(UIWidget *pWidget, UIWidgetGroup *pGroup, F32 fParentWidth, F32 fParentHeight)
{
	UIWidget *pBestWidget = pWidget;
	Vec2 v2Origin;
	Vec2 v2Start = {9e9, -9e9};
	if (!ui_FocusFindOrigin(pWidget, pGroup, fParentWidth, fParentHeight, v2Origin, UIBottom))
		return pWidget;
	ui_FindNearestCullByDirection(pGroup, pWidget, v2Origin, &pBestWidget, v2Start, UIBottom, 0.f, 0.f, fParentWidth, fParentHeight, 1.f);
	return pBestWidget;
}

UIWidget *ui_FocusLeft(UIWidget *pWidget, UIWidgetGroup *pGroup, F32 fParentWidth, F32 fParentHeight)
{
	UIWidget *pBestWidget = pWidget;
	Vec2 v2Origin;
	Vec2 v2Start = {9e9, 9e9};
	if (!ui_FocusFindOrigin(pWidget, pGroup, fParentWidth, fParentHeight, v2Origin, UILeft))
		return pWidget;
	ui_FindNearestCullByDirection(pGroup, pWidget, v2Origin, &pBestWidget, v2Start, UILeft, 0.f, 0.f, fParentWidth, fParentHeight, 1.f);
	return pBestWidget;
}

UIWidget *ui_FocusRight(UIWidget *pWidget, UIWidgetGroup *pGroup, F32 fParentWidth, F32 fParentHeight)
{
	UIWidget *pBestWidget = pWidget;
	Vec2 v2Origin;
	Vec2 v2Start = {-9e9, 9e9};
	if (!ui_FocusFindOrigin(pWidget, pGroup, fParentWidth, fParentHeight, v2Origin, UIRight))
		return pWidget;
	ui_FindNearestCullByDirection(pGroup, pWidget, v2Origin, &pBestWidget, v2Start, UIRight, 0.f, 0.f, fParentWidth, fParentHeight, 1.f);
	return pBestWidget;
}

AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_NAME("ui_FocusUp");
void ui_FocusUpCmd(void)
{
	if (g_ui_State.focused)
	{
		UIWidgetGroup *pMainGroup = ui_WidgetGroupForDevice(g_ui_State.device);
		UIWidget *pWidget = ui_FocusUp(g_ui_State.focused, pMainGroup, g_ui_State.screenWidth, g_ui_State.screenHeight);
		if (pWidget)
			ui_SetFocus(pWidget);
	}
}

AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_NAME("ui_FocusDown");
void ui_FocusDownCmd(void)
{
	if (g_ui_State.focused)
	{
		UIWidgetGroup *pMainGroup = ui_WidgetGroupForDevice(g_ui_State.device);
		UIWidget *pWidget = ui_FocusDown(g_ui_State.focused, pMainGroup, g_ui_State.screenWidth, g_ui_State.screenHeight);
		if (pWidget)
			ui_SetFocus(pWidget);
	}
}

AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_NAME("ui_FocusLeft");
void ui_FocusLeftCmd(void)
{
	if (g_ui_State.focused)
	{
		UIWidgetGroup *pMainGroup = ui_WidgetGroupForDevice(g_ui_State.device);
		UIWidget *pWidget = ui_FocusLeft(g_ui_State.focused, pMainGroup, g_ui_State.screenWidth, g_ui_State.screenHeight);
		if (pWidget)
			ui_SetFocus(pWidget);
	}
}

AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_NAME("ui_FocusRight");
void ui_FocusRightCmd(void)
{
	if (g_ui_State.focused)
	{
		UIWidgetGroup *pMainGroup = ui_WidgetGroupForDevice(g_ui_State.device);
		UIWidget *pWidget = ui_FocusRight(g_ui_State.focused, pMainGroup, g_ui_State.screenWidth, g_ui_State.screenHeight);
		if (pWidget)
			ui_SetFocus(pWidget);
	}
}
