/***************************************************************************



***************************************************************************/

#include "inputLib.h"
#include "UICore_h_ast.h"
#include "UIGen.h"
#include "UIGenPrivate.h"
#include "UIGenTutorial.h"
#include "UIGen_h_ast.h"
#include "MemoryPool.h"
#include "GfxSprite.h"
#include "GfxTexAtlas.h"
#include "Expression.h"
#include "StringCache.h"
#include "inputMouse.h"
#include "GfxClipper.h"

#include "UIGenTabGroup.h"

#define UI_GEN_TAB_SCROLL_TIME ((U32)125) // ms

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

MP_DEFINE(UIGenTabGroup);
MP_DEFINE(UIGenTabGroupState);

static const char *s_pchTabDataString;
static const char *s_pchTabNumberString;
static const char *s_pchTabGroupString;
static const char *s_pchTabCountString;

bool ui_GenTabGroupMayDecrementSelectedTab(UIGen *pGen, UIGenTabGroupState *pState) 
{
	if (UI_GEN_READY(pGen))
	{
		if (pState == NULL || pState->eaTabs == NULL || eaSize(&pState->eaTabs) == 0) {
			return false;
		}

		if (pState->iSelectedTab < 0) {
			// We got into a bad state, allow decrement to fix it in a later call by returning true
			return true;
		}

		if (pState->iSelectedTab == 0) {
			return false;
		}
	}

	return true;
}

bool ui_GenTabGroupMayIncrementSelectedTab(UIGen *pGen, UIGenTabGroupState *pState)
{
	if (UI_GEN_READY(pGen))
	{
		if (pState == NULL || pState->eaTabs == NULL || eaSize(&pState->eaTabs) == 0) {
			return false;
		}
		
		if (pState->iSelectedTab < 0) {
			// We got into a bad state, allow increment to fix it in a later call by returning true
			return true;
		}

		if (pState->iSelectedTab+1 >= eaSize(&pState->eaTabs)) {
			return false;
		}
	}

	return true;
}

bool ui_GenTabGroupDecrementSelectedTab(UIGen *pGen, UIGenTabGroupState *pState)
{
	if (UI_GEN_READY(pGen))
	{
		if (pState == NULL || pState->eaTabs == NULL || eaSize(&pState->eaTabs) == 0) {
			return false;
		}
		
		if (pState->iSelectedTab < 0) {
			// Current selected tab wasn't found, but we need to make sure something is selected
			pState->iSelectedTab = 0;
			ui_GenTutorialEvent(NULL, pGen, kUIGenTutorialEventTypeSelect);
			return true;
		}

		// Decrement the tab and check bounds
		if (pState->iSelectedTab == 0) {

			// If we're at the start of the list, wrap around to the end.
			pState->iSelectedTab = eaSize(&pState->eaTabs) - 1;
			ui_GenTutorialEvent(NULL, pGen, kUIGenTutorialEventTypeSelect);
			return true;
		}
	}

	// Update the selection
	pState->iSelectedTab--;
	ui_GenTutorialEvent(NULL, pGen, kUIGenTutorialEventTypeSelect);
	return true;
}

bool ui_GenTabGroupIncrementSelectedTab(UIGen *pGen, UIGenTabGroupState *pState)
{
	if (UI_GEN_READY(pGen))
	{
		if (pState == NULL || pState->eaTabs == NULL || eaSize(&pState->eaTabs) == 0) {
			return false;
		}
		
		if (pState->iSelectedTab < 0) {
			// Current selected tab wasn't found, but we need to make sure something is selected
			pState->iSelectedTab = 0;
			ui_GenTutorialEvent(NULL, pGen, kUIGenTutorialEventTypeSelect);
			return true;
		}

		if (pState->iSelectedTab >= eaSize(&pState->eaTabs) - 1) {

			// If we're at the end of the list, wrap around to the start.
			pState->iSelectedTab = 0;
			ui_GenTutorialEvent(NULL, pGen, kUIGenTutorialEventTypeSelect);
			return true;
		}
	}

	// Update the selection
	pState->iSelectedTab++;
	ui_GenTutorialEvent(NULL, pGen, kUIGenTutorialEventTypeSelect);
	return true;
}

bool ui_GenTabGroupSetSelectedTabIndex(UIGen *pGen, UIGenTabGroupState *pState, S32 iTab)
{
	if (UI_GEN_READY(pGen))
	{
		if (pState == NULL || pState->eaTabs == NULL || eaSize(&pState->eaTabs) == 0) {
			return false;
		}

		if (iTab < 0 || iTab >= eaSize(&pState->eaTabs)) {
			return false;
		}
	}

	pState->iSelectedTab = iTab;
	ui_GenTutorialEvent(NULL, pGen, kUIGenTutorialEventTypeSelect);
	return true;
}

static bool GenTabGroupSyncWithModel(UIGen *pGen, UIGenTabGroup *pTabGroup, UIGenTabGroupState *pState)
{
	static UIGen **s_eaTemplateChildren;
	UIGen *pTabTemplate = GET_REF(pTabGroup->hTabTemplate);
	ParseTable *pTable = NULL;
	void ***peaModel = NULL;
	S32 iModelSize = 0, iTemplateChildrenSize = eaSize(&pTabGroup->eaTemplateChild);
	S32 i, iIndex = 0;

	if (pTabGroup->pGetModel)
	{
		ui_GenTimeEvaluate(pGen, pTabGroup->pGetModel, NULL, "Model");
		if (pGen->pCode && pGen->pCode->uiFrameLastUpdate != g_ui_State.uiFrameCount)
		{
			ui_GenSetList(pGen, NULL, NULL);
			ErrorFilenamef(pGen->pchFilename, "%s: Model expression ran but did not update the list this frame.\n\n%s", 
				pGen->pchName, exprGetCompleteString(pTabGroup->pGetModel));
		}
	}
	else
	{
		ui_GenSetList(pGen, NULL, NULL);
	}

	if (pTabTemplate)
	{
		peaModel = ui_GenGetList(pGen, NULL, &pTable);
		iModelSize = eaSize(peaModel);
	}

	// If the tab template changed, destroy everything and start fresh
	if (pState->pTabTemplate != pTabTemplate) {
		eaDestroyUIGens(&pState->eaOwnedTabs);
	}

	//todo: create slow implementation in order to maintain the tab
	//      selection....i.e. the selected tab should, if possible,
	//      use the same exact gen it did before and should appear
	//      in the proper position (tab order), is possible

	// Alloc/dealloc space for tabs as needed
	while (iModelSize > eaSize(&pState->eaOwnedTabs)) {
		UIGen *pTab = ui_GenClone(pTabTemplate);
		eaPush(&pState->eaOwnedTabs, pTab);
	}
	eaSetSizeStruct(&pState->eaOwnedTabs, parse_UIGen, iModelSize);

	// Set the tab array capacity
	eaSetCapacity(&pState->eaTabs, iTemplateChildrenSize + iModelSize);

	// Prepend the children
	if (!pTabGroup->bAppendTemplateChildren)
	{
		for (i = 0; i < iTemplateChildrenSize; i++)
		{
			UIGen *pTab = GET_REF(pTabGroup->eaTemplateChild[i]->hChild);
			if (pTab && eaFind(&s_eaTemplateChildren, pTab) < 0)
			{
				if (iIndex > 0)
					ui_GenState(pState->eaTabs[iIndex - 1], kUIGenStateLast, true);
				ui_GenStates(pTab,
					kUIGenStateSelected, iIndex == pState->iSelectedTab,
					kUIGenStateFirst, iIndex == 0,
					kUIGenStateEven, (iIndex & 1) == 0,
					0);
				pTab->pParent = pGen;
				eaSet(&pState->eaTabs, pTab, iIndex++);
				eaPush(&s_eaTemplateChildren, pTab);
			}
		}
	}

	// Populate the tabs
	for (i = 0; i < iModelSize; i++) {
		UIGen *pTab = pState->eaOwnedTabs[i];
		if (iIndex > 0)
			ui_GenState(pState->eaTabs[iIndex - 1], kUIGenStateLast, true);
		ui_GenStates(pTab,
			kUIGenStateSelected, iIndex == pState->iSelectedTab,
			kUIGenStateFirst, iIndex == 0,
			kUIGenStateEven, (iIndex & 1) == 0,
			0);
		pTab->pParent = pGen;
		eaSet(&pState->eaTabs, pTab, iIndex++);
	}

	// Append the children
	if (pTabGroup->bAppendTemplateChildren)
	{
		for (i = 0; i < iTemplateChildrenSize; i++)
		{
			UIGen *pTab = GET_REF(pTabGroup->eaTemplateChild[i]->hChild);
			if (pTab && eaFind(&s_eaTemplateChildren, pTab) < 0)
			{
				if (iIndex > 0)
					ui_GenState(pState->eaTabs[iIndex - 1], kUIGenStateLast, true);
				ui_GenStates(pTab,
					kUIGenStateSelected, iIndex == pState->iSelectedTab,
					kUIGenStateFirst, iIndex == 0,
					kUIGenStateEven, (iIndex & 1) == 0,
					0);
				pTab->pParent = pGen;
				eaSet(&pState->eaTabs, pTab, iIndex++);
				eaPush(&s_eaTemplateChildren, pTab);
			}
		}
	}

	if (iIndex > 0)
		ui_GenState(pState->eaTabs[iIndex - 1], kUIGenStateLast, true);

	eaSetSize(&pState->eaTabs, iIndex);

	// Reset old template gens
	if (eaSize(&pState->eaTemplateGens) || eaSize(&s_eaTemplateChildren))
	{
		for (i = 0; i < eaSize(&s_eaTemplateChildren); i++)
		{
			UIGen *pTemplateChild = s_eaTemplateChildren[i];
			S32 iExisting = i;
			for (; iExisting < eaSize(&pState->eaTemplateGens); iExisting++)
			{
				if (GET_REF(pState->eaTemplateGens[iExisting]->hChild) == pTemplateChild)
				{
					if (iExisting != i)
						eaSwap(&pState->eaTemplateGens, i, iExisting);
					break;
				}
			}
			if (iExisting >= eaSize(&pState->eaTemplateGens))
			{
				UIGenChild *pChildHolder = StructCreate(parse_UIGenChild);
				SET_HANDLE_FROM_REFERENT(g_GenState.hGenDict, pTemplateChild, pChildHolder->hChild);
				eaInsert(&pState->eaTemplateGens, pChildHolder, i);
			}
		}
		while (eaSize(&pState->eaTemplateGens) > i)
		{
			UIGenChild *pOldGen = eaPop(&pState->eaTemplateGens);
			UIGen *pOldChildGen = GET_REF(pOldGen->hChild);
			if (pOldChildGen)
				ui_GenReset(pOldChildGen);
		}
		eaClearFast(&s_eaTemplateChildren);
	}

	// There should always be at least one tab selected
	if (pState->iSelectedTab < 0 && iIndex > 0) {
		pState->iSelectedTab = 0;
	}

	pState->pTabTemplate = pTabTemplate;

	ui_GenStates(pGen,
		kUIGenStateEmpty, !iIndex,
		kUIGenStateFilled, iIndex,
		0);

	return !!peaModel;
}

static F32 GenTabGroupHScrollbarWidth(UIGenTabGroup *pTabGroup)
{
	const char *apchTextures[] = {
		pTabGroup->pchScrollTabLeft, pTabGroup->pchScrollTabRight,
	};
	F32 fWidth = 0.f;
	S32 i;

	for (i = 0; i < ARRAY_SIZE(apchTextures); i++) {
		if (apchTextures[i]) {
			AtlasTex *pTex = atlasLoadTexture(apchTextures[i]);
			fWidth += pTex->width;
		}
	}

	return fWidth + pTabGroup->sScrollSpacing;
}

static F32 GenTabGroupHScrollbarHeight(UIGenTabGroup *pTabGroup)
{
	const char *apchTextures[] = {
		pTabGroup->pchScrollTabLeft, pTabGroup->pchScrollTabRight,
	};
	F32 fMaxHeight = 0.f;
	S32 i;

	for (i = 0; i < ARRAY_SIZE(apchTextures); i++) {
		if (apchTextures[i]) {
			AtlasTex *pTex = atlasLoadTexture(apchTextures[i]);
			MAX1(fMaxHeight, pTex->height);
		}
	}

	return fMaxHeight;
}

static void CalculateScrollButtonBoxes(UIGen *pGen, UIGenTabGroup *pTabGroup, 
									   UIGenTabGroupState *pState, 
									   AtlasTex *pLeft, AtlasTex *pRight,
									   CBox *pLeftBox, CBox *pRightBox)
{
	UIGenInternal *pResult = pGen->pResult;
	F32 fRight = pGen->ScreenBox.hx - pTabGroup->sRightPad * pGen->fScale;
	F32 fTop = pGen->ScreenBox.ly;
	F32 fHeight = CBoxHeight(&pGen->ScreenBox);
	F32 fScale = pGen->fScale;
	F32 fZ = UI_GET_Z();
	F32 fRightWidth = SAFE_MEMBER(pRight, width);
	F32 fRightHeight = SAFE_MEMBER(pRight, height);
	F32 fLeftWidth = SAFE_MEMBER(pLeft, width);
	F32 fLeftHeight = SAFE_MEMBER(pLeft, height);
	
	// Calculate right scroll button box
	{
		F32 x = fRight - (fRightWidth * fScale);
		F32 y;
		
		// If None or both of Bottom & Top are specified, then center the button
		if ((pTabGroup->eVerticalScrollButtonAlignment & UIHeight) == UIHeight ||
					(pTabGroup->eVerticalScrollButtonAlignment & UIHeight) == 0) {
			// Centered within tab group height
			y = fTop + (fHeight - fRightHeight * fScale) / 2; 
		} else if ((pTabGroup->eVerticalScrollButtonAlignment & UITop) > 0) {
			// Align with top of tab group
			y = fTop;
		} else {
			// Align with bottom of tab group
			y = fTop + (fHeight - fRightHeight) * fScale;
		}

		CBoxSetX(pRightBox, x, fRightWidth * fScale);
		CBoxSetY(pRightBox, y, fHeight);

		// Bump down fRight so the left button will be adjacent (to the left) of the right button
		fRight -= (fRightHeight + pTabGroup->sScrollSpacing) * fScale;
	}

	// Calculate left scroll button box
	{
		F32 x = fRight - (fLeftWidth * fScale);
		F32 y;

		// If None or both of Bottom & Top are specified, then center the button
		if ((pTabGroup->eVerticalScrollButtonAlignment & UIHeight) == UIHeight ||
					(pTabGroup->eVerticalScrollButtonAlignment & UIHeight) == 0) {
			// Centered within tab group height
			y = fTop + (fHeight - fLeftHeight * fScale) / 2; 
		} else if ((pTabGroup->eVerticalScrollButtonAlignment & UITop) > 0) {
			// Align with top of tab group
			y = fTop;
		} else {
			// Align with bottom of tab group
			y = fTop + (fHeight - fLeftHeight) * fScale;
		}

		CBoxSetX(pLeftBox, x, fLeftWidth * fScale);
		CBoxSetY(pLeftBox, y, fHeight);
	}
}

static CBox GenTabGroupGetTabAreaBox(UIGen *pGen, UIGenTabGroup *pTabGroup, UIGenTabGroupState *pState)
{
	CBox tabAreaBox = pGen->ScreenBox;
	F32 fRightPad = pTabGroup->sRightPad * pGen->fScale;
	F32 fLeftPad = pTabGroup->sLeftPad * pGen->fScale;
	F32 fTabToScrollMinSpacing = pTabGroup->sTabToScrollMinSpacing * pGen->fScale;
	F32 fScrollWidth = GenTabGroupHScrollbarWidth(pTabGroup) * pGen->fScale;
	F32 fMaxTabGroupX = pGen->ScreenBox.hx - fRightPad;

	if (pState->bScrollButtonsVisible) {
		fMaxTabGroupX -= fScrollWidth + fTabToScrollMinSpacing;
	}
	
	tabAreaBox.lx += fLeftPad;
	tabAreaBox.hx = floorf(fMaxTabGroupX);
	return tabAreaBox;
}

// Lay out tabs
static void GenTabGroupLayoutTabs(UIGen *pGen, UIGenTabGroup *pTabGroup, UIGenTabGroupState *pState)
{
	CBox origBox = pGen->ScreenBox;
	F32 fRightPad = (pTabGroup->sRightPad) * pGen->fScale;
	F32 fLeftPad = (pTabGroup->sLeftPad) * pGen->fScale;
	F32 fTabToScrollMinSpacing = pTabGroup->sTabToScrollMinSpacing * pGen->fScale;
	F32 fScrollWidth = GenTabGroupHScrollbarWidth(pTabGroup) * pGen->fScale;
	F32 fFakeLeft = floorf(-pState->fScrollPosition + origBox.lx + fLeftPad);
	F32 fDrawX = fFakeLeft;
	S32 i;
	F32 fTabSpacing = pTabGroup->sTabSpacing * pGen->fScale;
	UIGen *pFirstTab = NULL;
	UIGen *pLastTab = NULL;
	CBox tabAreaBox;
	S32 iTabLayoutCount = 0;
	F32 fTabWidths = 0;

	if (!pState || !pState->eaTabs) {
		return;
	}

	tabAreaBox = GenTabGroupGetTabAreaBox(pGen, pTabGroup, pState); // This should be done after the !pState check

	// Once all of tabs are laid out, then tabs that are beyond the 
	// bounds of the tab group and/or overlap with the scroll buttons 
	// (if visible) need to be pushed out to the bogusly high coordinates
	// so they are also not visible.  If possible, tabs that partially 
	// cross over the tab group edge or scroll buttons should be partially
	// drawn, but if not then they should be completely obscured.

	if (eaSize(&pState->eaTabs) && UI_GEN_NON_NULL(pState->eaTabs[0]))
		pFirstTab = pState->eaTabs[0];

	for (i = 0; i < eaSize(&pState->eaTabs); i++) {
		UIGen *pTab = pState->eaTabs[i];
		if (UI_GEN_NON_NULL(pTab)) {
			pLastTab = pTab;

			CBoxMoveX(&pGen->ScreenBox, ceilf(fDrawX));
			ui_GenLayoutCB(pTab, pGen);

			// Update scroll position to make sure the selected tab is always visible
			// It will become visible on the next frame
			if (i == pState->iSelectedTab) {
				if (pTab->UnpaddedScreenBox.lx <= floorf(tabAreaBox.lx)) {
					pState->fScrollPosition = floorf(pTab->UnpaddedScreenBox.lx) - fFakeLeft;
				} else if (ceilf(pTab->UnpaddedScreenBox.hx) > tabAreaBox.hx) {
					pState->fScrollPosition += ceilf(pTab->UnpaddedScreenBox.hx) - tabAreaBox.hx;
					if (pState->fScrollPosition < 0) {
						pState->fScrollPosition = 0;
					}
				}
			}

			fDrawX = pTab->UnpaddedScreenBox.hx + fTabSpacing;
			iTabLayoutCount++;
			fTabWidths += ceilf(CBoxWidth(&pTab->UnpaddedScreenBox));
		}
	}

	// Adjust the scroll position if space is available on the right
	// The adjustment will be visible next frame
	if (pState->fScrollPosition > 0 && pLastTab && pLastTab->UnpaddedScreenBox.hx < tabAreaBox.hx) {
		// If the last tab is visible and there is space between
		// it and end of the tabAreaBox, then adjust fScrollPosition so that
		// the tab is right aligned within tabAreaBox...unless this 
		// causes fScrollPosition to be less than 0.
		pState->fScrollPosition += ceilf(pLastTab->UnpaddedScreenBox.hx) - tabAreaBox.hx;

		if (pState->fScrollPosition < 0) {
			pState->fScrollPosition = 0;
		}
	}

	pState->fScrollPosition = floorf(pState->fScrollPosition);

	// Determine visibility of the scroll buttons
	switch (pTabGroup->eTabScrollerVisibility) {
		case kGenTabScrollerVisibleAlways:
			pState->bScrollButtonsVisible = true;
			break;
		case kGenTabScrollerVisibleNever:
			pState->bScrollButtonsVisible = false;
			break;
		case kGenTabScrollerVisibleIfNeeded:
			if (fTabWidths <= CBoxWidth(&pGen->ScreenBox))
			{
				pState->bScrollButtonsVisible = false;
				pState->fScrollPosition = 0;
			}
			else
			{
				pState->bScrollButtonsVisible = true;
			}
			break;
	}

	// If stretch to fit is on, adjust the tab boxes,
	// if and only if all of the tabs are able to fit at the same time.
	if (pTabGroup->bStretchTabsToFit && iTabLayoutCount > 0) {
		F32 ftabAreaMaxX = tabAreaBox.hx - (pState->bScrollButtonsVisible ? fScrollWidth : 0);
		if (pState->fScrollPosition == 0 && pLastTab && pLastTab->UnpaddedScreenBox.hx <= ftabAreaMaxX) {
			F32 fDiff = ftabAreaMaxX - pLastTab->UnpaddedScreenBox.hx;
			F32 iStretchPerTab = fDiff / (F32) iTabLayoutCount; 

			fDrawX = fFakeLeft;

			// FIXME(jfw): Doing two full layout passes is crappy.
			for (i = 0; i < eaSize(&pState->eaTabs); i++) {
				UIGen *pTab = pState->eaTabs[i];
					// fNewWidth (without dividing by the scale) is the
					// desired absolute width.  The call to ui_GenLayoutCB
					// will multiply the parent's scale by the width, so to
					// keep it constant, we need to divide the desired width
					// by the parent's scale.
					F32 fNewWidth = (pTab->UnpaddedScreenBox.hx - pTab->UnpaddedScreenBox.lx + iStretchPerTab) / pGen->fScale;
					F32 fOldWidth = pTab->pResult->pos.Width.fMagnitude;
					UIUnitType eOldUnit = pTab->pResult->pos.Width.eUnit;
					pTab->pResult->pos.Width.fMagnitude = fNewWidth;
					pTab->pResult->pos.Width.eUnit = UIUnitFixed;
					CBoxMoveX(&pGen->ScreenBox, fDrawX);
					ui_GenLayoutCB(pTab, pGen);
					pTab->pResult->pos.Width.fMagnitude = fOldWidth;
					pTab->pResult->pos.Width.eUnit = eOldUnit;

					fDrawX = pTab->UnpaddedScreenBox.hx + fTabSpacing;
			}
		}
	}

	// Restore the screen box
	pGen->ScreenBox = origBox;
}


void ui_GenFitContentsSizeTabGroup(UIGen *pGen, UIGenTabGroup *pTabGroup, CBox *pOut)
{
	UIGenTabGroupState *pState = UI_GEN_STATE(pGen, TabGroup);
	Vec2 v2Size = {0, 0};
	S32 i;
	for (i = 0; i < eaSize(&pState->eaTabs); i++)
	{
		Vec2 v2 = {0, 0};
		ui_GenGetBounds(pState->eaTabs[i], v2);
		v2Size[0] += v2[0];
		MAX1(v2Size[1], v2[1]);
	}
	if (eaSize(&pState->eaTabs))
	{
		v2Size[0] += ((eaSize(&pState->eaTabs) - 1) * pTabGroup->sTabSpacing);
	}
	v2Size[0] += pTabGroup->sLeftPad + pTabGroup->sRightPad;
	BuildCBox(pOut, 0, 0, v2Size[0], v2Size[1]);
}

static bool GenTickTab(UIGen *pTab, UIGen *pTabGroup)
{
	UIGenTabGroupState *pState = UI_GEN_STATE(pTabGroup, TabGroup);
	if (UI_GEN_READY(pTab) && 
			pTab->ScreenBox.hx >= pTabGroup->ScreenBox.lx && 
			pTab->ScreenBox.lx <  pTabGroup->ScreenBox.hx) {
		return ui_GenTickCB(pTab, pTabGroup);
	}
	else
		return false;
}

static bool GenTickScrollButtons(UIGen *pGen, UIGenTabGroup *pTabGroup, UIGenTabGroupState *pState)
{
	CBox leftBox, rightBox;
	U32 currTime = timerCpuMs();
	bool bHandled = false;

	if (pState->bScrollButtonsVisible)
	{
		AtlasTex *pLeft = UI_GEN_TEXTURE(pTabGroup->pchScrollTabLeft);
		AtlasTex *pRight = UI_GEN_TEXTURE(pTabGroup->pchScrollTabRight);
		bool bLeftMouseOver;
		bool bLeftPressed;
		bool bRightMouseOver;
		bool bRightPressed;
		CalculateScrollButtonBoxes(pGen, pTabGroup, pState, pLeft, pRight, &leftBox, &rightBox);

		bLeftMouseOver = mouseCollision(&leftBox);
		bLeftPressed = bLeftMouseOver ? mouseDownHit(MS_LEFT, &leftBox) : false;
		bRightMouseOver = mouseCollision(&rightBox);
		bRightPressed = bRightMouseOver ? mouseDownHit(MS_LEFT, &rightBox) : false;

		if (pLeft) {
			// tick left
			ui_GenState(pGen, kUIGenStateScrollingUp, bLeftPressed);
		}

		if (pRight)	{
			// tick right
			ui_GenState(pGen, kUIGenStateScrollingDown, bRightPressed);
		}

		pState->pchScrollTabLeft = NULL;
		pState->pchScrollTabRight = NULL;
		if (bLeftPressed && pTabGroup->pchScrollTabLeftPressed)
			pState->pchScrollTabLeft = pTabGroup->pchScrollTabLeftPressed;
		if (!pState->pchScrollTabLeft && bLeftMouseOver && pTabGroup->pchScrollTabLeftMouseOver)
			pState->pchScrollTabLeft = pTabGroup->pchScrollTabLeftMouseOver;
		if (!pState->pchScrollTabLeft)
			pState->pchScrollTabLeft = pTabGroup->pchScrollTabLeft;

		if (bRightPressed && pTabGroup->pchScrollTabRightPressed)
			pState->pchScrollTabRight = pTabGroup->pchScrollTabRightPressed;
		if (!pState->pchScrollTabRight && bRightMouseOver && pTabGroup->pchScrollTabRightMouseOver)
			pState->pchScrollTabRight = pTabGroup->pchScrollTabRightMouseOver;
		if (!pState->pchScrollTabRight)
			pState->pchScrollTabRight = pTabGroup->pchScrollTabRight;

		bHandled = bLeftMouseOver || bRightMouseOver;
	}
	else
	{
		// Make sure the scrolling states are cleared in the rare event that the
		// scroll buttons disappear while the states are still set.
		ui_GenStates(pGen,
			kUIGenStateScrollingDown, false,
			kUIGenStateScrollingUp, false,
			kUIGenStateNone);
	}

	if (ui_GenInState(pGen, kUIGenStateScrollingUp)) 
	{
		bool bScrollButtonClickedThisFrame = (pState->eLastScrollState != kUIGenStateScrollingUp);
		if (pState->bScrollButtonClickedLastFrame)
		{
			pState->iLastTabScrollTime = currTime;
			pState->bScrollButtonClickedLastFrame = false;
		}
		if (pState->eLastScrollState != kUIGenStateScrollingUp ||
			(!pState->bScrollButtonClickedLastFrame && (currTime - pState->iLastTabScrollTime > UI_GEN_TAB_SCROLL_TIME))) {
			ui_GenTabGroupDecrementSelectedTab(pGen, pState);

			pState->bScrollButtonClickedLastFrame = false;
			pState->iLastTabScrollTime = currTime;
			pState->eLastScrollState = kUIGenStateScrollingUp;
			bHandled = true;
		}
		if (bScrollButtonClickedThisFrame)
			pState->bScrollButtonClickedLastFrame = true;
	}
	else if (ui_GenInState(pGen, kUIGenStateScrollingDown))
	{
		bool bScrollButtonClickedThisFrame = (pState->eLastScrollState != kUIGenStateScrollingDown);
		if (pState->bScrollButtonClickedLastFrame)
		{
			pState->iLastTabScrollTime = currTime;
			pState->bScrollButtonClickedLastFrame = false;
		}
		if (pState->eLastScrollState != kUIGenStateScrollingDown ||
			(!pState->bScrollButtonClickedLastFrame && (currTime - pState->iLastTabScrollTime > UI_GEN_TAB_SCROLL_TIME))) {
			ui_GenTabGroupIncrementSelectedTab(pGen, pState);

			pState->bScrollButtonClickedLastFrame = false;
			pState->iLastTabScrollTime = currTime;
			pState->eLastScrollState = kUIGenStateScrollingDown;
			bHandled = true;
		}
		if (bScrollButtonClickedThisFrame)
			pState->bScrollButtonClickedLastFrame = true;
	}
	else
	{
		pState->eLastScrollState = kUIGenStateNone;
		pState->bScrollButtonClickedLastFrame = false;
	}

	if (pTabGroup->bScrollTabUseWheel)
	{
		if (mouseClickHit(MS_WHEELUP, &pGen->ScreenBox))
		{
			bHandled = true;
			ui_GenTabGroupDecrementSelectedTab(pGen, pState);
		}
		else if (mouseClickHit(MS_WHEELDOWN, &pGen->ScreenBox))
		{
			bHandled = true;
			ui_GenTabGroupIncrementSelectedTab(pGen, pState);
		}
	}

	return bHandled;
}

static bool GenDrawTab(UIGen *pTab, UIGen *pTabGroup)
{
	UIGenTabGroupState *pState = UI_GEN_STATE(pTabGroup, TabGroup);
	if (UI_GEN_READY(pTab) && 
			pTab->ScreenBox.hx >= pTabGroup->ScreenBox.lx && 
			pTab->ScreenBox.lx <  pTabGroup->ScreenBox.hx) {
		return ui_GenDrawCB(pTab, pTabGroup);
	} else {
		return false;
	}
}

static S32 GenGetDisabledColor(S32 iColor) {
	// This grays out the color using the NTSC conversion formula
	// iColor is in RGBA format
	const F32 fDarkness = 0.66; // 1 = Dark, 0 = Light.  0.66 == magic based on experimentation
	S32 iGray = (S32)
		( (0.30 * ((iColor >> 24) & 0xff) + 
           0.59 * ((iColor >> 16) & 0xff) + 
           0.11 * ((iColor >>  8) & 0xff)) / 3);

	// Apply darkness, but overall make gray a little brighter
	iGray = 255 - ((255 - iGray) * fDarkness);
	if (iGray < 0) {
		iGray = 0;
	} else if (iGray > 255) {
		iGray = 255;
	}

	return (iGray << 24) | (iGray << 16) | (iGray << 8) | (iColor & 0xff); // RGBA
}

static void GenDrawScrollButtons(UIGen *pGen, UIGenTabGroup *pTabGroup, UIGenTabGroupState *pState)
{
	AtlasTex *pLeft = UI_GEN_TEXTURE(pState->pchScrollTabLeft);
	AtlasTex *pRight = UI_GEN_TEXTURE(pState->pchScrollTabRight);
	S32 iEnabledColor = ColorRGBAMultiplyAlpha(ui_StyleColorPaletteIndex(pTabGroup->uiScrollTabColor), pGen->chAlpha);
	S32 iDisabledColor = ColorRGBAMultiplyAlpha(ui_StyleColorPaletteIndex(pTabGroup->uiScrollTabDisabledColor), pGen->chAlpha);
	F32 fScale = pGen->fScale;
	F32 fZ = UI_GET_Z();
	CBox leftBox, rightBox;

	if (pTabGroup->uiScrollTabDisabledColor == 0xffffffff) {
		iDisabledColor = GenGetDisabledColor(iEnabledColor);
	}

	if (pState->bScrollButtonsVisible) {
		S32 iSelectedTab = pState->iSelectedTab;

		CalculateScrollButtonBoxes(pGen, pTabGroup, pState, pLeft, pRight, &leftBox, &rightBox);

		if (pRight)
		{
			S32 iColor = iSelectedTab+1 < eaSize(&pState->eaTabs) ? iEnabledColor : iDisabledColor;
			display_sprite(pRight, rightBox.lx, rightBox.ly, fZ, fScale, fScale, iColor);
		}

		if (pLeft)
		{
			S32 iColor = iSelectedTab > 0 ? iEnabledColor : iDisabledColor;
			display_sprite(pLeft, leftBox.lx, leftBox.ly, fZ, fScale, fScale, iColor);
		}
	}
}

//
// Update callback handlers
//

void ui_GenPointerUpdateTabGroup(UIGen *pGen)
{
	UIGenTabGroup *pTabGroup = UI_GEN_RESULT(pGen, TabGroup);
	if (pTabGroup)
	{
		UIGenTabGroupState *pState = UI_GEN_STATE(pGen, TabGroup);
		GenTabGroupSyncWithModel(pGen, pTabGroup, pState);
		ui_GenForEach(&pState->eaTabs, ui_GenPointerUpdateCB, pGen, UI_GEN_POINTER_UPDATE_ORDER);
	}
}

void ui_GenUpdateTabGroup(UIGen *pGen)
{
	UIGenTabGroup *pTabGroup = UI_GEN_RESULT(pGen, TabGroup);
	UIGenTabGroupState *pState = UI_GEN_STATE(pGen, TabGroup);

	ui_GenForEach(&pState->eaTabs, ui_GenUpdateCB, pGen, UI_GEN_UPDATE_ORDER);
}

void ui_GenLayoutLateTabGroup(UIGen *pGen)
{
	UIGenTabGroup *pTabGroup = UI_GEN_RESULT(pGen, TabGroup);
	UIGenTabGroupState *pState = UI_GEN_STATE(pGen, TabGroup);
	
	GenTabGroupLayoutTabs(pGen, pTabGroup, pState);
}

void ui_GenTickEarlyTabGroup(UIGen *pGen)
{
	// gen is guaranteed to be ready
	UIGenTabGroup *pTabGroup = UI_GEN_RESULT(pGen, TabGroup);
	UIGenTabGroupState *pState = UI_GEN_STATE(pGen, TabGroup);
	CBox origBox = pGen->ScreenBox;

	// Scroll buttons get priority over tabs for ticking
	GenTickScrollButtons(pGen, pTabGroup, pState);

	// Tick tabs -- clip to tab box area
	//   Doing the clipping here instead of in GenTickTab is cheaper
	pGen->ScreenBox = GenTabGroupGetTabAreaBox(pGen, pTabGroup, pState);
	mouseClipPushRestrict(&pGen->ScreenBox);
	ui_GenForEachInPriority(&pState->eaTabs, GenTickTab, pGen, true);
	mouseClipPop();
	pGen->ScreenBox = origBox;
}

void ui_GenDrawEarlyTabGroup(UIGen *pGen)
{
	// gen is guaranteed to be ready
	UIGenTabGroup *pTabGroup = UI_GEN_RESULT(pGen, TabGroup);
	UIGenTabGroupState *pState = UI_GEN_STATE(pGen, TabGroup);
	CBox origBox = pGen->ScreenBox;

	// Draw tabs -- clip to tab box area
	//   Doing the clipping here instead of in GenDrawTab is cheaper
	pGen->ScreenBox = GenTabGroupGetTabAreaBox(pGen, pTabGroup, pState);
	clipperPushRestrict(&pGen->ScreenBox);
	ui_GenForEachInPriority(&pState->eaTabs, GenDrawTab, pGen, false);
	clipperPop();
	pGen->ScreenBox = origBox;

	// Draw scroll buttons
	GenDrawScrollButtons(pGen, pTabGroup, pState);
}

void ui_GenHideTabGroup(UIGen *pGen)
{
	// gen is NOT guaranteed to be ready
	UIGenTabGroupState *pState = UI_GEN_STATE(pGen, TabGroup);
	if (pState)
	{
		eaDestroy(&pState->eaTabs);
		eaDestroyUIGens(&pState->eaOwnedTabs);
		while (eaSize(&pState->eaTemplateGens) > 0)
		{
			UIGenChild *pChild = eaPop(&pState->eaTemplateGens);
			UIGen *pChildGen = GET_REF(pChild->hChild);
			if (pChildGen)
				ui_GenReset(pChildGen);
		}
		pState->pTabTemplate = NULL;
	}
}

// Returns tab index the given pChild lives within.  If it
// is not within the tab group or is the tab group, then this
// method will return -1;
static S32 GenTabGroupGetTabIndex(UIGen *pTabGroup, UIGen *pFor, S32 *piTrueIndex)
{
	UIGenTabGroup *pResult;
	UIGenTabGroupState *pState;
	UIGen *pTab;
	S32 iIndex;

	// Find the gen that is an immediate child of the tab group
	for (pTab = pFor; pTab && pTab->pParent != pTabGroup; pTab = pTab->pParent) {
		// Do nothing
	}

	if (pTab == NULL) {
		if (piTrueIndex)
			*piTrueIndex = -1;
		return -1;
	}

	// Return the index of pTab in the pTabGroup's 
	pState = UI_GEN_STATE(pTabGroup, TabGroup);
	pResult = UI_GEN_READY(pTabGroup) ? UI_GEN_RESULT(pTabGroup, TabGroup) : NULL;

	iIndex = eaFind(&pState->eaTabs, pTab);
	if (piTrueIndex)
		*piTrueIndex = iIndex;
	if (iIndex < 0 || pResult && pResult->bAppendTemplateChildren)
		return iIndex;

	return iIndex - (eaSize(&pState->eaTabs) - eaSize(&pState->eaOwnedTabs));
}

void ui_GenUpdateContextTabGroup(UIGen *pGen, ExprContext *pContext, UIGen *pFor)
{
	static int s_iTabDataCache = 0;
	static int s_iTabNumberCache = 0;
	static int s_iTabCountCache = 0;

	UIGenTabGroupState *pState = UI_GEN_STATE(pGen, TabGroup);
	ParseTable *pTable = NULL;
	void *pData = NULL;
	S32 iTrueNumber;
	S32 iTabNumber = GenTabGroupGetTabIndex(pGen, pFor, &iTrueNumber);

	if (pState && pState->eaTabs && iTabNumber >= 0) {
		void ***peaModel = ui_GenGetList(pGen, NULL, &pTable);
		pData = eaGet(peaModel, iTabNumber);
		exprContextSetPointerVarPooledCached(pContext, s_pchTabDataString, pData, pTable, true, true, &s_iTabDataCache);
		exprContextSetPointerVarPooledCached(pContext, g_ui_pchGenData, pData, pTable, true, true, &g_ui_iGenData);
	}

	if (iTrueNumber >= 0) {
		exprContextSetIntVarPooledCached(pContext, s_pchTabNumberString, iTrueNumber, &s_iTabNumberCache);
	}

	exprContextSetIntVarPooledCached(pContext, s_pchTabCountString, pState ? eaSize(&pState->eaTabs) : 0, &s_iTabCountCache);
	exprContextSetPointerVarPooled(pContext, s_pchTabGroupString, pGen, parse_UIGen, true, true);
}

void ui_GenQueueResetTabGroup(UIGen *pGen)
{
	UIGenTabGroupState *pState = UI_GEN_STATE(pGen, TabGroup);
	ui_GenForEach(&pState->eaTabs, ui_GenQueueResetChildren, pGen, false);
}

AUTO_RUN;
void ui_GenRegisterTabGroup(void)
{
	MP_CREATE(UIGenTabGroup, 32);
	MP_CREATE(UIGenTabGroupState, 32);

	s_pchTabDataString = allocAddStaticString("TabData");
	s_pchTabNumberString = allocAddStaticString("TabNumber");
	s_pchTabGroupString = allocAddStaticString("TabGroup");
	s_pchTabCountString = allocAddStaticString("TabCount");

	ui_GenInitPointerVar(s_pchTabGroupString, parse_UIGen);
	ui_GenInitPointerVar(s_pchTabDataString, NULL);
	ui_GenInitIntVar(s_pchTabNumberString, 0);
	ui_GenInitIntVar(s_pchTabCountString, 0);

	ui_GenRegisterType(kUIGenTypeTabGroup, 
		UI_GEN_NO_VALIDATE, 
		ui_GenPointerUpdateTabGroup,
		ui_GenUpdateTabGroup, 
		UI_GEN_NO_LAYOUTEARLY, 
		ui_GenLayoutLateTabGroup, 
		ui_GenTickEarlyTabGroup, 
		UI_GEN_NO_TICKLATE, 
		ui_GenDrawEarlyTabGroup, 
		ui_GenFitContentsSizeTabGroup, 
		UI_GEN_NO_FITPARENTSIZE, 
		ui_GenHideTabGroup, 
		UI_GEN_NO_INPUT, 
		ui_GenUpdateContextTabGroup,
		ui_GenQueueResetTabGroup);
}

#include "UIGenTabGroup_h_ast.c"