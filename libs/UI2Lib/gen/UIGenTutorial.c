#include "UIGenTutorial.h"
#include "UIGen.h"
#include "UIGenPrivate.h"

#include "UIGenBox.h"
#include "UIGenList.h"
#include "UIGenLayoutBox.h"
#include "UIGenTabGroup.h"

#include "Expression.h"

#include "file.h"
#include "ResourceManager.h"

#include "AutoGen/UIGen_h_ast.h"
#include "AutoGen/UIGenBox_h_ast.h"
#include "AutoGen/UIGenTutorial_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

DictionaryHandle g_hUIGenTutorialDict;
UIGenTutorialState *g_pUIGenActiveTutorial;
static S32 s_iUIGenTutorialVersion = 1;

static UIGenTutorialState **s_eaTutorialStates = NULL;
static REF_TO(UIGen) s_hFakeTutorialRoot;

static UIGenTutorialResolveCB *s_cbResolvers = NULL;
static S32 s_iResolvers = 0;

static ui_GenTutorialStartStopCB s_cbStartStop = NULL;

static UIGen *ui_GenTutorialGetRoot(void)
{
	if (!GET_REF(s_hFakeTutorialRoot))
	{
		UIGen *pFakeTutorialRoot = ui_GenFind("Tutorial_Root", kUIGenTypeNone);

		if (!pFakeTutorialRoot)
		{
			pFakeTutorialRoot = ui_GenCreate("Tutorial_Root", kUIGenTypeBox);
			pFakeTutorialRoot->bIsRoot = true;
			pFakeTutorialRoot->chLayer = kUIGenLayerModal;
			pFakeTutorialRoot->chAlpha = 255;
			pFakeTutorialRoot->bTopLevelChildren = true;
			pFakeTutorialRoot->pBase->pos.Width.fMagnitude = 1.f;
			pFakeTutorialRoot->pBase->pos.Width.eUnit = UIUnitPercentage;
			pFakeTutorialRoot->pBase->pos.Height.fMagnitude = 1.f;
			pFakeTutorialRoot->pBase->pos.Height.eUnit = UIUnitPercentage;
			ui_GenReset(pFakeTutorialRoot);
			RefSystem_AddReferent(UI_GEN_DICTIONARY, pFakeTutorialRoot->pchName, pFakeTutorialRoot);
		}

		SET_HANDLE_FROM_REFERENT(UI_GEN_DICTIONARY, pFakeTutorialRoot, s_hFakeTutorialRoot);
	}

	return GET_REF(s_hFakeTutorialRoot);
}

static void ui_GenTutorialRunAction(UIGenAction *pAction)
{
	UIGen *pGen = ui_GenTutorialGetRoot();

	if (pGen)
		ui_GenRunAction(pGen, pAction);
}

void ui_GenTutorialOncePerFrameEarly(void)
{
	UIGen *pGen = ui_GenTutorialGetRoot();

	// "Update" Tutorial_Root here, at least until Tutorial_Root becomes a real layer.
	pGen->uiFrameLastUpdate = g_ui_State.uiFrameCount;
	pGen->uiTimeLastUpdateInMs = g_ui_State.totalTimeInMs;
}

void ui_GenTutorialOncePerFrame(void)
{
	static UIGen **s_eaGens = NULL;
	static UIGen **s_eaUpdatedGens = NULL;
	UIGenTutorialState *pState = g_pUIGenActiveTutorial;
	UIGenTutorial *pTutorial;
	UIGenTutorialStep *pStep;
	S32 i, j, iGens;

	if (!pState)
		return;

	if (pState->iVersion < s_iUIGenTutorialVersion)
		ui_GenTutorialReset(pState);

	pTutorial = GET_REF(pState->hTutorial);
	if (!pTutorial)
		return;

	pStep = eaGet(&pTutorial->eaSteps, pState->iStep);

	if (pStep)
	{
		for (i = eaSize(&pStep->eaTutorialGens) - 1; i >= 0; i--)
		{
			UIGenTutorialInfo *pTutorialInfo = pStep->eaTutorialGens[i];
			ui_GenTutorialResolve(&s_eaGens, GET_REF(pTutorialInfo->hTutorialGen), pTutorialInfo->eaSelectors, false);

			// Determine how many gens to actually resolve
			iGens = eaSize(&s_eaGens);
			if (pTutorialInfo->iSelectLimit && iGens > pTutorialInfo->iSelectLimit)
				iGens = pTutorialInfo->iSelectLimit;

			// Update the tutorial info
			for (j = 0; j < iGens; j++)
			{
				UIGen *pGen = s_eaGens[j];
				if (UI_GEN_READY(pGen))
				{
					if (!pGen->pTutorialInfo)
						ui_GenTutorialAddInfo(pState, pGen, pTutorialInfo);
					else if (pGen->pTutorialInfo != pTutorialInfo || pState->bDirty)
						ui_GenTutorialSetInfo(pState, pGen, pTutorialInfo);
					eaPush(&s_eaUpdatedGens, pGen);
				}
			}

			eaClearFast(&s_eaGens);
		}
	}

	for (i = eaSize(&pState->eaInfoState) - 1; i >= 0; i--)
	{
		if (eaFind(&s_eaUpdatedGens, pState->eaInfoState[i]->pGen) < 0)
			ui_GenTutorialReleaseInfo(pState, pState->eaInfoState[i]->pGen);
	}

	eaClearFast(&s_eaUpdatedGens);

	pState->bDirty = false;
}

bool ui_GenTutorialStart(UIGenTutorialState *pState, UIGenTutorial *pTutorial)
{
	if (!pTutorial)
	{
		ui_GenTutorialStop(pState);
		return false;
	}

	if (!pState)
	{
		// Stop existing tutorial
		if (g_pUIGenActiveTutorial)
			ui_GenTutorialStop(NULL);

		g_pUIGenActiveTutorial = pState = StructCreate(parse_UIGenTutorialState);
	}

	if (!IS_HANDLE_ACTIVE(pState->hTutorial))
		pState->iStep = -1;
	SET_HANDLE_FROM_REFERENT(g_hUIGenTutorialDict, pTutorial, pState->hTutorial);
	if (ui_GenTutorialReset(pState))
	{
		if (pTutorial->pOnEnter)
			ui_GenTutorialRunAction(pTutorial->pOnEnter);

		if (s_cbStartStop)
			s_cbStartStop(pTutorial, true);
		return true;
	}

	return false;
}

bool ui_GenTutorialReset(UIGenTutorialState *pState)
{
	if (!pState)
		pState = g_pUIGenActiveTutorial;

	if (pState)
	{
		pState->iVersion = s_iUIGenTutorialVersion;
		ui_GenTutorialSetStep(pState, 0);
		while (eaSize(&pState->eaInfoState) > 0)
			ui_GenTutorialReleaseInfo(pState, pState->eaInfoState[0]->pGen);
		pState->bDirty = true;
		return true;
	}

	return false;
}

bool ui_GenTutorialStop(UIGenTutorialState *pState)
{
	if (!pState)
		pState = g_pUIGenActiveTutorial;

	while (pState && eaSize(&pState->eaInfoState) > 0)
		ui_GenTutorialReleaseInfo(pState, pState->eaInfoState[0]->pGen);

	if (pState && pState == g_pUIGenActiveTutorial)
	{
		UIGenTutorial *pTutorial = GET_REF(pState->hTutorial);

		if (pState->iStep < eaSize(&pTutorial->eaSteps))
		{
			UIGenTutorialStep *pStep = eaGet(&pTutorial->eaSteps, pState->iStep);
			if (pStep && pStep->pOnExit)
				ui_GenTutorialRunAction(pStep->pOnExit);

			if (s_cbStartStop)
				s_cbStartStop(pTutorial, false);
		}

		StructDestroySafe(parse_UIGenTutorialState, &g_pUIGenActiveTutorial);
		return true;
	}
	else if (pState)
	{
		UIGenTutorial *pTutorial = GET_REF(pState->hTutorial);

		if (pTutorial && pState->iStep < eaSize(&pTutorial->eaSteps))
		{
			UIGenTutorialStep *pStep = eaGet(&pTutorial->eaSteps, pState->iStep);
			if (pStep && pStep->pOnExit)
				ui_GenTutorialRunAction(pStep->pOnExit);

			if (s_cbStartStop)
				s_cbStartStop(pTutorial, false);

			pState->iStep = eaSize(&pTutorial->eaSteps);
			pState->bDirty = true;
		}

		if (pTutorial)
			REMOVE_HANDLE(pState->hTutorial);
		return true;
	}

	return false;
}

bool ui_GenTutorialPrevious(UIGenTutorialState *pState)
{
	if (!pState)
		pState = g_pUIGenActiveTutorial;

	if (pState)
	{
		UIGenTutorial *pTutorial = GET_REF(pState->hTutorial);

		if (pState->iStep > 0)
		{
			UIGenTutorialStep *pStep = pTutorial ? eaGet(&pTutorial->eaSteps, pState->iStep) : NULL;
			if (pStep && pStep->pOnExit)
				ui_GenTutorialRunAction(pStep->pOnExit);

			pState->iStep--;

			pStep = pTutorial ? eaGet(&pTutorial->eaSteps, pState->iStep) : NULL;
			if (pStep && pStep->pOnEnter)
				ui_GenTutorialRunAction(pStep->pOnEnter);

			pState->bDirty = true;
		}
		return true;
	}

	return false;
}

bool ui_GenTutorialNext(UIGenTutorialState *pState)
{
	if (!pState)
		pState = g_pUIGenActiveTutorial;

	if (pState)
	{
		UIGenTutorial *pTutorial = GET_REF(pState->hTutorial);
		if (!pTutorial)
			return false;

		if (pState->iStep < eaSize(&pTutorial->eaSteps) - 1)
		{
			UIGenTutorialStep *pStep = eaGet(&pTutorial->eaSteps, pState->iStep);
			if (pStep && pStep->pOnExit)
				ui_GenTutorialRunAction(pStep->pOnExit);

			pState->iStep++;

			pStep = eaGet(&pTutorial->eaSteps, pState->iStep);
			if (pStep && pStep->pOnEnter)
				ui_GenTutorialRunAction(pStep->pOnEnter);

			pState->bDirty = true;
		}
		else
			return ui_GenTutorialStop(pState);

		return true;
	}

	return false;
}

bool ui_GenTutorialSetStep(UIGenTutorialState *pState, S32 iStep)
{
	if (!pState)
		pState = g_pUIGenActiveTutorial;

	if (pState)
	{
		UIGenTutorial *pTutorial = GET_REF(pState->hTutorial);
		if (!pTutorial)
			return false;

		if (iStep < 0)
			return false;

		if (iStep < eaSize(&pTutorial->eaSteps) - 1 || pState->iStep < 0)
		{
			UIGenTutorialStep *pStep = eaGet(&pTutorial->eaSteps, pState->iStep);
			if (pStep && pStep->pOnExit)
				ui_GenTutorialRunAction(pStep->pOnExit);

			pState->iStep = iStep;

			pStep = eaGet(&pTutorial->eaSteps, pState->iStep);
			if (pStep && pStep->pOnEnter)
				ui_GenTutorialRunAction(pStep->pOnEnter);

			pState->bDirty = true;
		}
		else
			return ui_GenTutorialStop(pState);

		return true;
	}

	return false;
}

static bool ui_GenTutorialInfoCanHearEvent(UIGenTutorialInfo *pInfo, UIGenTutorialEventType eType)
{
	if (!pInfo->peListenEvents || !eaiSize(&pInfo->peListenEvents))
		return true;
	if (eaiFind(&pInfo->peListenEvents, eType) < 0)
		return false;
	return true;
}

void ui_GenTutorialEvent(UIGenTutorialState *pState, UIGen *pGen, UIGenTutorialEventType eType)
{
	if (pGen->eType == kUIGenTypeList && eType == kUIGenTutorialEventTypeSelect)
	{
		UIGenListState *pListState = UI_GEN_STATE(pGen, List);
		UIGen *pRow;
		S32 i, n;

		if (eaSize(&pListState->eaCols))
		{
			n = eaSize(&pListState->eaCols);
			for (i = 0; i < n; i++)
			{
				UIGenListColumnState *pColState = UI_GEN_STATE(pListState->eaCols[i], ListColumn);
				pRow = pColState ? eaGet(&pColState->eaRows, pListState->iSelectedRow) : NULL;
				if (pRow && pRow->pTutorialInfo && ui_GenTutorialInfoCanHearEvent(pRow->pTutorialInfo, eType))
				{
					ui_GenTutorialNext(pState);
					return;
				}
			}
		}
		else
		{
			pRow = eaGet(&pListState->eaRows, pListState->iSelectedRow);
			if (pRow && pRow->pTutorialInfo && ui_GenTutorialInfoCanHearEvent(pRow->pTutorialInfo, eType))
			{
				ui_GenTutorialNext(pState);
				return;
			}
		}
	}
	else if (pGen->eType == kUIGenTypeLayoutBox && eType == kUIGenTutorialEventTypeSelect)
	{
		UIGenLayoutBoxState *pLayoutState = UI_GEN_STATE(pGen, LayoutBox);
		UIGen *pInstance = eaGet(&pLayoutState->eaInstanceGens, pLayoutState->iSelected);
		if (pInstance && pInstance->pTutorialInfo && ui_GenTutorialInfoCanHearEvent(pInstance->pTutorialInfo, eType))
		{
			ui_GenTutorialNext(pState);
			return;
		}
	}
	else if (pGen->eType == kUIGenTypeTabGroup && eType == kUIGenTutorialEventTypeSelect)
	{
		UIGenTabGroupState *pTabState = UI_GEN_STATE(pGen, TabGroup);
		UIGen *pTab = eaGet(&pTabState->eaTabs, pTabState->iSelectedTab);
		if (pTab && pTab->pTutorialInfo && ui_GenTutorialInfoCanHearEvent(pTab->pTutorialInfo, eType))
		{
			ui_GenTutorialNext(pState);
			return;
		}
	}

	if (pGen->pTutorialInfo && ui_GenTutorialInfoCanHearEvent(pGen->pTutorialInfo, eType))
	{
		// TODO: See if this is an auto-advance event
		if (pGen->eType == kUIGenTypeButton && eType == kUIGenTutorialEventTypeClick
			|| pGen->eType == kUIGenTypeListRow && eType == kUIGenTutorialEventTypeSelect
			|| eType == kUIGenTutorialEventTypeManualNext)
		{
			ui_GenTutorialNext(pState);
		}
	}
}

bool ui_GenTutorialAddInfo(UIGenTutorialState *pState, UIGen *pGen, UIGenTutorialInfo *pInfo)
{
	UIGenTutorialInfoState *pInfoState;
	S32 i;

	if (!pInfo)
		return false;

	if (!pState)
		pState = g_pUIGenActiveTutorial;

	if (pState)
	{
		for (i = eaSize(&pState->eaInfoState) - 1; i >= 0; i--)
		{
			pInfoState = pState->eaInfoState[i];
			if (pInfoState->pGen == pGen)
				return false;
		}

		pInfoState = StructCreate(parse_UIGenTutorialInfoState);
		pInfoState->pGen = pGen;
		pInfoState->pInfo = pInfo;
		pGen->pTutorialInfo = pInfo;
		eaPush(&pState->eaInfoState, pInfoState);

		if (pInfo->pOnEnter)
			ui_GenRunAction(pGen, pInfo->pOnEnter);
		if (pInfo->pOverride)
			ui_GenMarkDirty(pGen);
		return true;
	}

	return false;
}

bool ui_GenTutorialSetInfo(UIGenTutorialState *pState, UIGen *pGen, UIGenTutorialInfo *pInfo)
{
	S32 i;

	if (!pInfo)
		return false;

	if (!pState)
		pState = g_pUIGenActiveTutorial;

	if (pState)
	{
		for (i = eaSize(&pState->eaInfoState) - 1; i >= 0; i--)
		{
			UIGenTutorialInfoState *pInfoState = pState->eaInfoState[i];
			if (pInfoState->pGen == pGen)
			{
				if (pInfoState->pInfo != pInfo)
				{
					// Exit old info
					if (pInfoState->pInfo->pOnExit)
					{
						pGen->pTutorialInfo = pInfoState->pInfo;
						ui_GenRunAction(pGen, pInfoState->pInfo->pOnExit);
					}
					if (pInfoState->pInfo->pOverride)
						ui_GenMarkDirty(pGen);

					// Update info pointers
					pInfoState->pInfo = pInfo;
					pGen->pTutorialInfo = pInfo;

					// Enter new info
					if (pInfo->pOnEnter)
						ui_GenRunAction(pGen, pInfo->pOnEnter);
					if (pInfo->pOverride)
						ui_GenMarkDirty(pGen);
				}
				else
					pGen->pTutorialInfo = pInfo;
				return true;
			}
		}
	}

	if (pGen->pTutorialInfo)
		pGen->pTutorialInfo = NULL;

	return false;
}

UIGenTutorialInfo *ui_GenTutorialGetInfo(UIGenTutorialState *pState, UIGen *pGen)
{
	S32 i;

	if (!pState)
		pState = g_pUIGenActiveTutorial;

	if (pState)
	{
		for (i = eaSize(&pState->eaInfoState) - 1; i >= 0; i--)
		{
			UIGenTutorialInfoState *pInfoState = pState->eaInfoState[i];
			if (pInfoState->pGen == pGen)
			{
				if (!devassertmsg(pInfoState->pInfo == pGen->pTutorialInfo, "UIGen tutorial info mismatch"))
					pGen->pTutorialInfo = pInfoState->pInfo;
				return pInfoState->pInfo;
			}
		}
	}

	return NULL;
}

void ui_GenTutorialReleaseInfo(UIGenTutorialState *pState, UIGen *pGen)
{
	UIGenTutorialInfo *pInfo = NULL;
	S32 i;

	if (!pState)
		pState = g_pUIGenActiveTutorial;

	if (pState)
	{
		for (i = eaSize(&pState->eaInfoState) - 1; i >= 0; i--)
		{
			UIGenTutorialInfoState *pInfoState = pState->eaInfoState[i];
			if (pInfoState->pGen == pGen)
			{
				pInfo = pInfoState->pInfo;
				StructDestroy(parse_UIGenTutorialInfoState, eaRemove(&pState->eaInfoState, i));
				break;
			}
		}
	}

	if (pInfo)
	{
		if (pInfo->pOnExit)
		{
			pGen->pTutorialInfo = pInfo;
			ui_GenRunAction(pGen, pInfo->pOnExit);
		}
		if (pInfo->pOverride)
		{
			UIGen *pCur = NULL;

			ui_GenMarkDirty(pGen);

			// Remove created inline children
			for (i = eaSize(&pInfo->pOverride->eaInlineChildren) - 1; i >= 0; i--)
			{
				UIGen *pChild = pInfo->pOverride->eaInlineChildren[i];
				UIGen *pBorrow = eaIndexedRemoveUsingString(&pGen->eaBorrowedInlineChildren, pChild->pchName);
				if (pBorrow)
					eaPush(&g_GenState.eaFreeQueue, pBorrow);
			}

			// Ensure pGen updates next frame
			for (pCur = pGen; pCur != NULL; pCur = pCur->pParent)
			{
				if (pCur->pSpriteCache)
					pCur->pSpriteCache->iAccumulate = 0;
			}
		}
	}

	pGen->pTutorialInfo = NULL;
}

static UIGen *GenTutorialResolveNone(S32 *piIndex, UIGen *pGen, UIGenTutorialSelectChild *pSelector, bool bValidate)
{
	if ((*piIndex) == 0)
	{
		(*piIndex)++;
		return pGen;
	}

	return NULL;
}

static void GenInternalTutorialGetChildren(UIGen ***peaChildren, UIGenInternal *pInt)
{
	S32 i;

	if (!pInt)
		return;

	for (i = 0; i < eaSize(&pInt->eaChildren); i++)
	{
		UIGen *pChild = GET_REF(pInt->eaChildren[i]->hChild);
		if (pChild)
			eaPush(peaChildren, pChild);
	}

	eaPushEArray(peaChildren, &pInt->eaInlineChildren);
}

static UIGen *GenTutorialResolveChild(S32 *piIndex, UIGen *pGen, UIGenTutorialSelectChild *pSelector, bool bValidate)
{
	S32 i, j, iPos = 0;

	if (!pGen)
		return NULL;

	if (bValidate)
	{
		UIGen **eaChildren = NULL;

		if (*piIndex == 0)
			ui_GenInitializeBorrows(pGen);

		// Get known children of UIGen directly
		GenInternalTutorialGetChildren(&eaChildren, pGen->pBase);
		for (i = 0; i < eaSize(&pGen->eaStates); i++)
			GenInternalTutorialGetChildren(&eaChildren, pGen->eaStates[i]->pOverride);
		for (i = 0; i < eaSize(&pGen->eaComplexStates); i++)
			GenInternalTutorialGetChildren(&eaChildren, pGen->eaComplexStates[i]->pOverride);
		GenInternalTutorialGetChildren(&eaChildren, pGen->pLast);

		for (j = 0; j < eaSize(&pGen->eaBorrowed); j++)
		{
			UIGen *pBorrow = GET_REF(pGen->eaBorrowed[j]->hGen);

			// Get known children of borrows
			GenInternalTutorialGetChildren(&eaChildren, pBorrow->pBase);
			for (i = 0; i < eaSize(&pBorrow->eaStates); i++)
				GenInternalTutorialGetChildren(&eaChildren, pBorrow->eaStates[i]->pOverride);
			for (i = 0; i < eaSize(&pBorrow->eaComplexStates); i++)
				GenInternalTutorialGetChildren(&eaChildren, pBorrow->eaComplexStates[i]->pOverride);
			GenInternalTutorialGetChildren(&eaChildren, pBorrow->pLast);
		}

		for (; *piIndex < eaSize(&eaChildren); (*piIndex)++)
		{
			UIGen *pChild = eaChildren[*piIndex];
			if (pChild->pchName == pSelector->pchChildName)
			{
				eaDestroy(&eaChildren);
				return pChild;
			}
		}

		if (*piIndex == 0)
			ErrorFilenamef(pSelector->polyp.pchFilename, "TutorialGen can't resolve Child '%s' of %s on Line %d", NULL_TO_EMPTY(pSelector->pchChildName), pGen->pchName, pSelector->polyp.iLineNumber);

		eaDestroy(&eaChildren);
	}
	else if (pGen->pResult)
	{
		UIGenInternal *pResult = pGen->pResult;
		S32 iInline = eaSize(&pResult->eaInlineChildren);
		S32 iChildren = eaSize(&pResult->eaChildren);
		S32 iIndex = *piIndex;

		for (; iIndex < iInline; iIndex++)
		{
			UIGen *pChild = pResult->eaInlineChildren[iInline - 1 - iIndex];
			if (pChild->pchName == pSelector->pchChildName)
			{
				*piIndex = iIndex + 1;
				return pChild;
			}
		}

		iIndex -= iInline;
		for (; iIndex < iChildren; iIndex++)
		{
			UIGen *pChild = GET_REF(pResult->eaChildren[iChildren - 1 - iIndex]->hChild);
			if (pChild && pChild->pchName == pSelector->pchChildName)
			{
				*piIndex = iIndex + iInline + 1;
				return pChild;
			}
		}

		*piIndex = iIndex + iInline;
	}

	return NULL;
}

static bool GenInternalTutorialGetTemplates(UIGen ***peaTemplates, UIGenInternal *pInt, bool bColumns, bool bInitializeBorrows)
{
	UIGenList *pList;
	UIGenListColumn *pListColumn;
	UIGenLayoutBox *pLayoutBox;
	UIGenTabGroup *pTabGrup;
	S32 i, j, k;
	bool bExpressionTemplate = false;

	if (!pInt || (pInt->eType != kUIGenTypeList
			&& pInt->eType != kUIGenTypeListColumn
			&& pInt->eType != kUIGenTypeLayoutBox
			&& pInt->eType != kUIGenTypeTabGroup))
		return bExpressionTemplate;

	switch (pInt->eType)
	{
	case kUIGenTypeList:
		pList = (UIGenList *)pInt;
		if (GET_REF(pList->hRowTemplate))
			eaPush(peaTemplates, GET_REF(pList->hRowTemplate));
		for (i = 0; i < eaSize(&pList->eaTemplateChild); i++)
			eaPush(peaTemplates, GET_REF(pList->eaTemplateChild[i]->hChild));
		if (pList->pRowTemplateExpr)
			bExpressionTemplate |= true;

		if (bColumns)
		{
			for (k = 0; k < eaSize(&pList->eaColumnChildren); k++)
			{
				UIGen *pGen = GET_REF(pList->eaColumnChildren[k]->hChild);

				if (bInitializeBorrows)
					ui_GenInitializeBorrows(pGen);

				bExpressionTemplate |= GenInternalTutorialGetTemplates(peaTemplates, pGen->pBase, bColumns, bInitializeBorrows);
				for (i = 0; i < eaSize(&pGen->eaStates); i++)
					bExpressionTemplate |= GenInternalTutorialGetTemplates(peaTemplates, pGen->eaStates[i]->pOverride, bColumns, bInitializeBorrows);
				for (i = 0; i < eaSize(&pGen->eaComplexStates); i++)
					bExpressionTemplate |= GenInternalTutorialGetTemplates(peaTemplates, pGen->eaComplexStates[i]->pOverride, bColumns, bInitializeBorrows);
				bExpressionTemplate |= GenInternalTutorialGetTemplates(peaTemplates, pGen->pLast, bColumns, bInitializeBorrows);

				for (j = 0; j < eaSize(&pGen->eaBorrowed); j++)
				{
					UIGen *pBorrow = GET_REF(pGen->eaBorrowed[j]->hGen);

					// Get known children of borrows
					bExpressionTemplate |= GenInternalTutorialGetTemplates(peaTemplates, pBorrow->pBase, bColumns, bInitializeBorrows);
					for (i = 0; i < eaSize(&pBorrow->eaStates); i++)
						bExpressionTemplate |= GenInternalTutorialGetTemplates(peaTemplates, pBorrow->eaStates[i]->pOverride, bColumns, bInitializeBorrows);
					for (i = 0; i < eaSize(&pBorrow->eaComplexStates); i++)
						bExpressionTemplate |= GenInternalTutorialGetTemplates(peaTemplates, pBorrow->eaComplexStates[i]->pOverride, bColumns, bInitializeBorrows);
					bExpressionTemplate |= GenInternalTutorialGetTemplates(peaTemplates, pBorrow->pLast, bColumns, bInitializeBorrows);
				}
			}
		}
		break;

	case kUIGenTypeListColumn:
		pListColumn = (UIGenListColumn *)pInt;
		if (GET_REF(pListColumn->hCellTemplate))
			eaPush(peaTemplates, GET_REF(pListColumn->hCellTemplate));
		for (i = 0; i < eaSize(&pListColumn->eaTemplateChild); i++)
			eaPush(peaTemplates, GET_REF(pListColumn->eaTemplateChild[i]->hChild));
		break;

	case kUIGenTypeLayoutBox:
		pLayoutBox = (UIGenLayoutBox *)pInt;
		if (GET_REF(pLayoutBox->hTemplate))
			eaPush(peaTemplates, GET_REF(pLayoutBox->hTemplate));
		for (i = 0; i < eaSize(&pLayoutBox->eaTemplateChild); i++)
			eaPush(peaTemplates, GET_REF(pLayoutBox->eaTemplateChild[i]->hChild));
		if (pLayoutBox->pTemplateExpr)
			bExpressionTemplate |= true;
		break;

	case kUIGenTypeTabGroup:
		pTabGrup = (UIGenTabGroup *)pInt;
		if (GET_REF(pTabGrup->hTabTemplate))
			eaPush(peaTemplates, GET_REF(pTabGrup->hTabTemplate));
		for (i = 0; i < eaSize(&pTabGrup->eaTemplateChild); i++)
			eaPush(peaTemplates, GET_REF(pTabGrup->eaTemplateChild[i]->hChild));
		break;
	}

	return bExpressionTemplate;
}

static UIGen *GenTutorialResolveListRow(S32 *piIndex, UIGen *pGen, UIGenTutorialSelectListRow *pSelector, bool bValidate)
{
	S32 i, j;

	if (!pGen)
		return NULL;

	if (pGen->eType != kUIGenTypeList && (!bValidate || pGen->eType != kUIGenTypeNone))
	{
		if (bValidate && *piIndex == 0)
			ErrorFilenamef(pSelector->polyp.pchFilename, "TutorialGen can't resolve a ListRow on Line %d, because '%s' is not a List", pSelector->polyp.iLineNumber, pGen->pchName);
		(*piIndex)++;
		return NULL;
	}
	else if (pGen->eType == kUIGenTypeNone)
	{
		if (*piIndex == 0)
			ui_GenInitializeBorrows(pGen);

		// Search borrows for type
		for (i = eaSize(&pGen->eaBorrowed) - 1; i >= 0; i--)
		{
			UIGen *pBorrow = GET_REF(pGen->eaBorrowed[i]->hGen);
			if (pBorrow->eType == kUIGenTypeList)
				break;
		}
		if (i < 0)
		{
			if (bValidate && *piIndex == 0)
				ErrorFilenamef(pSelector->polyp.pchFilename, "TutorialGen can't resolve a ListRow on Line %d, because '%s' is not a List", pSelector->polyp.iLineNumber, pGen->pchName);
			(*piIndex)++;
			return NULL;
		}
	}

	if (bValidate)
	{
		UIGen **eaTemplates = NULL;
		bool bExpressionTemplate = false;
		S32 iTemplates;

		if (*piIndex == 0)
			ui_GenInitializeBorrows(pGen);

		// Resolve through RowTemplates, TemplateChildren, Columns
		// TODO: and TemplateExpressions
		bExpressionTemplate |= GenInternalTutorialGetTemplates(&eaTemplates, pGen->pBase, true, *piIndex == 0);
		for (i = 0; i < eaSize(&pGen->eaStates); i++)
			bExpressionTemplate |= GenInternalTutorialGetTemplates(&eaTemplates, pGen->eaStates[i]->pOverride, true, *piIndex == 0);
		for (i = 0; i < eaSize(&pGen->eaComplexStates); i++)
			bExpressionTemplate |= GenInternalTutorialGetTemplates(&eaTemplates, pGen->eaComplexStates[i]->pOverride, true, *piIndex == 0);
		bExpressionTemplate |= GenInternalTutorialGetTemplates(&eaTemplates, pGen->pLast, true, *piIndex == 0);

		for (j = 0; j < eaSize(&pGen->eaBorrowed); j++)
		{
			UIGen *pBorrow = GET_REF(pGen->eaBorrowed[j]->hGen);

			// Get known children of borrows
			bExpressionTemplate |= GenInternalTutorialGetTemplates(&eaTemplates, pBorrow->pBase, true, *piIndex == 0);
			for (i = 0; i < eaSize(&pBorrow->eaStates); i++)
				bExpressionTemplate |= GenInternalTutorialGetTemplates(&eaTemplates, pBorrow->eaStates[i]->pOverride, true, *piIndex == 0);
			for (i = 0; i < eaSize(&pBorrow->eaComplexStates); i++)
				bExpressionTemplate |= GenInternalTutorialGetTemplates(&eaTemplates, pBorrow->eaComplexStates[i]->pOverride, true, *piIndex == 0);
			bExpressionTemplate |= GenInternalTutorialGetTemplates(&eaTemplates, pBorrow->pLast, true, *piIndex == 0);
		}

		iTemplates = eaSize(&eaTemplates);

		for (; (*piIndex) < eaSize(&eaTemplates); (*piIndex)++)
		{
			UIGen *pTemplate = eaTemplates[*piIndex];

			if (eaSize(&pSelector->eapchName) > 0 && eaFind(&pSelector->eapchName, pTemplate->pchName) < 0)
				continue;

			eaDestroy(&eaTemplates);
			return pTemplate;
		}

		eaDestroy(&eaTemplates);

		for (; (*piIndex) - iTemplates < eaSize(&pSelector->eapchName); (*piIndex)++)
		{
			S32 iIndex = (*piIndex) - iTemplates;
			if (bExpressionTemplate)
			{
				UIGen *pExprListRow = ui_GenFind(pSelector->eapchName[iIndex], kUIGenTypeNone);
				if (pExprListRow)
				{
					(*piIndex)++;
					return pExprListRow;
				}

				ErrorFilenamef(pSelector->polyp.pchFilename, "TutorialGen can't resolve a ListRow on Line %d, because '%s' is not in the Dictionary", pSelector->polyp.iLineNumber, pSelector->eapchName[iIndex]);
			}
			else
				ErrorFilenamef(pSelector->polyp.pchFilename, "TutorialGen can't resolve a ListRow on Line %d, because '%s' is not a RowTemplate", pSelector->polyp.iLineNumber, pSelector->eapchName[iIndex]);
		}

		return NULL;
	}
	else if (pGen->pResult)
	{
		UIGen ***peaInstances = ui_GenGetInstances(pGen);

		if (peaInstances)
		{
			// Resolve direct ListRow
			for (; (*piIndex) < eaSize(peaInstances); (*piIndex)++)
			{
				UIGen *pInstance = (*peaInstances)[*piIndex];

				if (eaSize(&pSelector->eapchName) > 0 && eaFind(&pSelector->eapchName, pInstance->pchName) < 0)
					continue;

				if (pSelector->pFilter && !ui_GenEvaluate(pInstance, pSelector->pFilter, NULL))
					continue;

				(*piIndex)++;
				return pInstance;
			}
		}
		else
		{
			// TODO: Resolve through ListColumns
		}
	}

	return NULL;
}

static void GenInternalTutorialGetColumns(UIGen ***peaColumns, UIGenInternal *pInt)
{
	UIGenList *pList;
	S32 i;

	if (!pInt || pInt->eType != kUIGenTypeList)
		return;

	pList = (UIGenList *)pInt;
	for (i = 0; i < eaSize(&pList->eaColumnChildren); i++)
		eaPush(peaColumns, GET_REF(pList->eaColumnChildren[i]->hChild));
}

static UIGen *GenTutorialResolveListColumn(S32 *piIndex, UIGen *pGen, UIGenTutorialSelectListColumn *pSelector, bool bValidate)
{
	// TODO: implement this
	if (bValidate)
		ErrorFilenamef(pSelector->polyp.pchFilename, "TutorialGen can't resolve a ListColumn on Line %d, because Joshua hasn't implemented it yet.", pSelector->polyp.iLineNumber);
	return NULL;
}

static UIGen *GenTutorialResolveInstance(S32 *piIndex, UIGen *pGen, UIGenTutorialSelectInstance *pSelector, bool bValidate)
{
	// TODO: implement this
	if (bValidate)
		ErrorFilenamef(pSelector->polyp.pchFilename, "TutorialGen can't resolve an Instance on Line %d, because Joshua hasn't implemented it yet.", pSelector->polyp.iLineNumber);
	return NULL;
}

static UIGen *GenTutorialResolveTab(S32 *piIndex, UIGen *pGen, UIGenTutorialSelectTab *pSelector, bool bValidate)
{
	// TODO: implement this
	if (bValidate)
		ErrorFilenamef(pSelector->polyp.pchFilename, "TutorialGen can't resolve a Tab on Line %d, because Joshua hasn't implemented it yet.", pSelector->polyp.iLineNumber);
	return NULL;
}

static UIGen *GenTutorialResolveJail(S32 *piIndex, UIGen *pGen, UIGenTutorialSelectJail *pSelector, bool bValidate)
{
	// TODO: implement this
	ErrorFilenamef(pSelector->polyp.pchFilename, "TutorialGen can't resolve a Jail on Line %d, because Joshua hasn't implemented it yet, nor does he really have a clue how it's supposed to work.", pSelector->polyp.iLineNumber);
	return NULL;
}

void ui_GenTutorialAddResolver(UIGenTutorialSelectType eType, UIGenTutorialResolveCB cbResolver)
{
	if (s_iResolvers <= eType)
	{
		UIGenTutorialResolveCB *pNewResolvers = realloc(s_cbResolvers, sizeof(UIGenTutorialResolveCB) * (eType + 1));
		if (!pNewResolvers)
			return;

		// Zero new values
		memset(pNewResolvers + s_iResolvers, 0, sizeof(UIGenTutorialResolveCB) * (eType + 1 - s_iResolvers));

		// Update values
		s_cbResolvers = pNewResolvers;
		s_iResolvers = eType + 1;
	}

	s_cbResolvers[eType] = cbResolver;
}

bool ui_GenTutorialResolve(UIGen ***peaResolved, UIGen *pGen, UIGenTutorialSelect **eaSelectors, bool bValidate)
{
	S32 iDepth = 0, iCount = 0, n = eaSize(&eaSelectors), *piCounters;
	UIGen **ppGens;

	if (peaResolved)
		eaClearFast(peaResolved);

	// Perform simplest UIGen resolution
	if (n == 0)
	{
		if (!pGen)
			return false;

		if (peaResolved)
			eaPush(peaResolved, pGen);
		return true;
	}

	// Perform advanced UIGen resolution
	piCounters = alloca(sizeof(S32) * n);
	ppGens = alloca(sizeof(UIGen *) * n);

	ppGens[iDepth] = pGen; // ppGens = the gen to inspect
	piCounters[iDepth] = 0;
	while (iDepth >= 0)
	{
		UIGenTutorialSelect *pSelector = eaSelectors[iDepth];
		UIGen *pFrom = ppGens[iDepth];
		UIGen *pTo = NULL;

		if (pSelector->eType < s_iResolvers && s_cbResolvers[pSelector->eType])
			pTo = (s_cbResolvers[pSelector->eType])(&piCounters[iDepth], pFrom, eaSelectors[iDepth], bValidate);

		if (pTo && iDepth == n - 1)
		{
			// At the deepest level of the selectors, keep the resolved gen
			if (peaResolved)
				eaPush(peaResolved, pTo);
			if (bValidate)
				return true;
			++iCount;
		}
		else if (pTo)
		{
			// Push deeper into the selectors
			++iDepth;
			ppGens[iDepth] = pTo;
			piCounters[iDepth] = 0;
		}
		else
		{
			// Reached the end of this level, pop off the current resolver
			--iDepth;
		}
	}

	return iCount > 0;
}

void ui_GenTutorialSetStartStopHandler(ui_GenTutorialStartStopCB cbStartStopHandler)
{
	s_cbStartStop = cbStartStopHandler;
}

static bool TutorialResValidatePostTextReading(UIGenTutorial *pTutorial)
{
	UIGen *pGen = ui_GenTutorialGetRoot();
	S32 iStep, iGen;

	assertmsg(pGen, "UIGen Tutorial_Root was not created");

	// Generate expressions
	ParserScanForSubstruct(parse_UIGenTutorial, pTutorial, parse_Expression, 0, 0, ui_GenGenerateExpr, ui_GenGetContext(pGen));

	for (iStep = 0; iStep < eaSize(&pTutorial->eaSteps); iStep++)
	{
		UIGenTutorialStep *pStep = pTutorial->eaSteps[iStep];

		// Fixup info
		for (iGen = 0; iGen < eaSize(&pStep->eaTutorialGens); iGen++)
		{
			UIGenTutorialInfo *pInfo = pStep->eaTutorialGens[iGen];

			// Fixup overrides
			if (pInfo->pOverride)
				ui_GenInternalResourceFixup(pInfo->pOverride);
		}
	}

	// Force updates of the states
	s_iUIGenTutorialVersion++;

	return true;
}

static bool TutorialResValidatePostBinning(UIGenTutorial *pTutorial)
{
	S32 iStep, iGen;

	for (iStep = 0; iStep < eaSize(&pTutorial->eaSteps); iStep++)
	{
		UIGenTutorialStep *pStep = pTutorial->eaSteps[iStep];

		// Fixup info
		for (iGen = 0; iGen < eaSize(&pStep->eaTutorialGens); iGen++)
		{
			UIGenTutorialInfo *pInfo = pStep->eaTutorialGens[iGen];

			// Fixup overrides
			if (pInfo->pOverride)
				ui_GenInternalResourcePostBinning(pInfo->pOverride);
		}
	}

	return true;
}

static bool TutorialResValidateCheckReferences(UIGenTutorial *pTutorial)
{
	S32 iStep, iGen;

	for (iStep = 0; iStep < eaSize(&pTutorial->eaSteps); iStep++)
	{
		UIGenTutorialStep *pStep = pTutorial->eaSteps[iStep];

		// Fixup info
		for (iGen = 0; iGen < eaSize(&pStep->eaTutorialGens); iGen++)
		{
			UIGenTutorialInfo *pInfo = pStep->eaTutorialGens[iGen];

			// Validate selector
			if (!ui_GenTutorialResolve(NULL, GET_REF(pInfo->hTutorialGen), pInfo->eaSelectors, true))
				ErrorFilenamef(pTutorial->pchFilename, "%s: Unable to fully resolve UIGen selectors for TutorialGen on Line %d", pTutorial->pchName, pInfo->iLineNumber);

			// Fixup overrides
			if (pInfo->pOverride)
				ui_GenInternalResourceValidate(pTutorial->pchName, "tutorial", pInfo->pOverride);
		}
	}

	return true;
}

static int TutorialResValidate(enumResourceValidateType eType, const char *pDictName, const char *pTutorialName, UIGenTutorial *pTutorial, U32 iUserID)
{
	switch (eType)
	{
	case RESVALIDATE_POST_TEXT_READING:
		TutorialResValidatePostTextReading(pTutorial);
		return VALIDATE_HANDLED;
	case RESVALIDATE_POST_BINNING:
		TutorialResValidatePostBinning(pTutorial);
		return VALIDATE_HANDLED;
	case RESVALIDATE_CHECK_REFERENCES:
		TutorialResValidateCheckReferences(pTutorial);
		return VALIDATE_HANDLED;
	}

	return VALIDATE_NOT_HANDLED;
}

void ui_GenTutorialLoad(void)
{
	resDictManageValidation(g_hUIGenTutorialDict, TutorialResValidate);
	if (isDevelopmentMode())
		resDictMaintainInfoIndex(g_hUIGenTutorialDict, NULL, NULL, NULL, NULL, NULL);
	//resDictRegisterEventCallback(g_hUIGenTutorialDict, TutorialResValidate, NULL);

	resLoadResourcesFromDisk(g_hUIGenTutorialDict, "ui/gens/Tutorials", ".tutorial", NULL, PARSER_CLIENTSIDE | PARSER_OPTIONALFLAG);
}

AUTO_RUN;
void ui_GenTutorialInit(void)
{
	g_hUIGenTutorialDict = RefSystem_RegisterSelfDefiningDictionary("UIGenTutorial", false, parse_UIGenTutorial, true, false, NULL);

	ui_GenTutorialAddResolver(kUIGenTutorialSelectTypeNone,             GenTutorialResolveNone);
	ui_GenTutorialAddResolver(kUIGenTutorialSelectTypeChild,            GenTutorialResolveChild);
	ui_GenTutorialAddResolver(kUIGenTutorialSelectTypeListRow,          GenTutorialResolveListRow);
	ui_GenTutorialAddResolver(kUIGenTutorialSelectTypeListColumn,       GenTutorialResolveListColumn);
	ui_GenTutorialAddResolver(kUIGenTutorialSelectTypeInstance,         GenTutorialResolveInstance);
	ui_GenTutorialAddResolver(kUIGenTutorialSelectTypeTab,              GenTutorialResolveTab);
	ui_GenTutorialAddResolver(kUIGenTutorialSelectTypeJail,             GenTutorialResolveJail);
}

AUTO_FIXUPFUNC;
TextParserResult ui_GenTutorialStateParserFixup(UIGenTutorialState *pState, enumTextParserFixupType eType, void *pExtraData)
{
	// Really hacky management of tutorial states
	switch (eType)
	{
	case FIXUPTYPE_CONSTRUCTOR:
		eaPushUnique(&s_eaTutorialStates, pState);
		break;

	case FIXUPTYPE_DESTRUCTOR:
		eaFindAndRemove(&s_eaTutorialStates, pState);
		break;
	}

	return PARSERESULT_SUCCESS;
}

static void GenTutorialInfoRelease(UIGenTutorialState *pState, UIGenTutorialInfo *pInfo)
{
	S32 i;

	if (!pState || !pInfo)
		return;

	for (i = eaSize(&pState->eaInfoState) - 1; i >= 0; i--)
	{
		if (pState->eaInfoState[i]->pInfo == pInfo)
			ui_GenTutorialReleaseInfo(pState, pState->eaInfoState[i]->pGen);
	}
}

AUTO_FIXUPFUNC;
TextParserResult ui_GenTutorialInfoParserFixup(UIGenTutorialInfo *pInfo, enumTextParserFixupType eType, void *pExtraData)
{
	S32 i;

	switch (eType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		for (i = eaSize(&s_eaTutorialStates) - 1; i >= 0; i--)
			GenTutorialInfoRelease(s_eaTutorialStates[i], pInfo);
		break;
	}

	return PARSERESULT_SUCCESS;
}

AUTO_FIXUPFUNC;
TextParserResult ui_GenTutorialInfoStateParserFixup(UIGenTutorialInfoState *pInfoState, enumTextParserFixupType eType, void *pExtraData)
{
	S32 i;

	switch (eType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		if (pInfoState->pGen && pInfoState->pGen->pTutorialInfo)
		{
			for (i = eaSize(&s_eaTutorialStates) - 1; i >= 0; i--)
			{
				if (eaFind(&s_eaTutorialStates[i]->eaInfoState, pInfoState) >= 0)
					ui_GenTutorialReleaseInfo(s_eaTutorialStates[i], pInfoState->pGen);
			}
		}
		break;
	}

	return PARSERESULT_SUCCESS;
}

#include "AutoGen/UIGenTutorial_h_ast.c"
