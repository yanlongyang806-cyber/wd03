#include "StringCache.h"
#include "Expression.h"
#include "earray.h"
#include "error.h"

#include "UICore_h_ast.h"
#include "UIGen_h_ast.h"
#include "UIGenLayoutBox.h"
#include "UIGenLayoutBox_h_ast.h"

#include "UIGenPrivate.h"
#include "UIGenTutorial.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static const char *s_pchLayoutBox;
static const char *s_GenInstanceRowString;
static const char *s_GenInstanceColumnString;
static const char *s_GenInstanceDataString;
static const char *s_GenInstanceNumberString;
static const char *s_GenInstanceRowCountString;
static const char *s_GenInstanceColumnCountString;
static const char *s_GenInstanceCountString;
static int s_LayoutBoxHandle = 0;
static int s_GenInstanceRowHandle = 0;
static int s_GenInstanceColumnHandle = 0;
static int s_GenInstanceDataHandle = 0;
static int s_GenInstanceNumberHandle = 0;
static int s_GenInstanceRowCountHandle = 0;
static int s_GenInstanceColumnCountHandle = 0;
static int s_GenInstanceCountHandle = 0;

//////////////////////////////////////////////////////////////////////////
// Need the columns out of the parse table to poke values to instances.
static S32 s_iGenColumnX;
static S32 s_iGenColumnPercentX;
static S32 s_iGenColumnLeftMargin;
static S32 s_iGenColumnRightMargin;
static S32 s_iGenColumnWidth;
static S32 s_iGenColumnY;
static S32 s_iGenColumnPercentY;
static S32 s_iGenColumnTopMargin;
static S32 s_iGenColumnBottomMargin;
static S32 s_iGenColumnHeight;
static S32 s_iGenColumnOffsetFrom;


__forceinline static bool ui_GenForEachLayoutBoxInstanceInPriority(UIGenLayoutBoxInstance ***peaGens, UIGenUserFunc cbForEach, UserData pData, bool bReverse)
{
	bool bStop = false;
	if (peaGens)
	{
		S32 iSize = eaSize(peaGens);
		S8 *achPriorities = _alloca(iSize);
		S8 chMaxPriority = 0;
		S8 chPriority = 0;
		S32 iDiff = bReverse ? -1 : 1;
		S32 i;

		if (iSize == 0)
			return bStop;

		for (i = 0; i < iSize; i++)
		{
			UIGen *pGen = (*peaGens)[i]->pGen ? (*peaGens)[i]->pGen : (*peaGens)[i]->pTemplate;
			achPriorities[i] = pGen->chPriority;
			MAX1(chMaxPriority, achPriorities[i]);
		}

		for (chPriority = bReverse ? chMaxPriority : 0; (bReverse ? (chPriority >= 0) : (chPriority <= chMaxPriority)) && !bStop; chPriority += iDiff)
		{
			for (i = bReverse ? (iSize - 1) : 0; (bReverse ? (i >= 0) : (i < iSize)) && !bStop; i += iDiff)
			{
				UIGen *pGen = (*peaGens)[i]->pGen ? (*peaGens)[i]->pGen : (*peaGens)[i]->pTemplate;
				if (achPriorities[i] == chPriority)
					bStop = cbForEach(pGen, pData);
			}
		}
	}
	return bStop;
}

__forceinline static bool ui_GenForEachLayoutBoxInstance(UIGenLayoutBoxInstance ***peaGens, UIGenUserFunc cbForEach, UserData pData, bool bReverse)
{
	S32 i;
	S32 iSize;
	bool bStop = false;
	S32 iDiff = bReverse ? -1 : 1;

	iSize = eaSize(peaGens);
	for (i = bReverse ? (iSize - 1) : 0; bReverse ? (i >= 0) : (i < iSize) && !bStop; i += iDiff)
		bStop = cbForEach((*peaGens)[i]->pGen ? (*peaGens)[i]->pGen : (*peaGens)[i]->pTemplate, pData);
	return bStop;
}

// Find the UIGenInternal pBase that has the actual positioning information in it.
static UIGenInternal *GenLayoutGetBase(UIGen *pGen, UIGen *pTemplate)
{
	UIGenLayoutBoxState *pState = UI_GEN_STATE(pGen, LayoutBox);
	UIGenNamedInternal *pCachedInternal = eaIndexedGetUsingString(&pState->eaInternals, pTemplate->pchName);
	UIGenInternal *pBase = pTemplate ? pTemplate->pBase : NULL;
	UIGenInternal *pInternal;
	UIPosition *pPos = pBase ? &pBase->pos : NULL;
	static S32 s_iStart;
	static S32 s_iEnd;
	S32 i;
	S32 j;

	if (!s_iStart)
	{
		s_iStart = ParserFindColumnFromOffset(parse_UIGenInternal, offsetof(UIGenInternal, pos));
		s_iEnd = ParserFindColumnFromOffset(parse_UIGenInternal, offsetof(UIGenInternal, pos.ausScaleAsIf));
		assert(s_iStart < s_iEnd);
	}

	if (pCachedInternal)
	{
		return pCachedInternal->pInternal;
	}
	else
	{
		pInternal = StructCreate(parse_UIGenInternal);
		pCachedInternal = StructCreate(parse_UIGenNamedInternal);
		pCachedInternal->pchName = pTemplate->pchName;
		pCachedInternal->pInternal = pInternal;
		if (!pState->eaInternals)
			eaIndexedEnable(&pState->eaInternals, parse_UIGenNamedInternal);
		eaPush(&pState->eaInternals, pCachedInternal);
		for (i = 0; i < eaSize(&pTemplate->eaBorrowed); i++)
		{
			UIGen *pBorrow = GET_REF(pTemplate->eaBorrowed[i]->hGen);
			if (!pBorrow || !pBorrow->pBase)
				continue;
			for (j = s_iStart; j <= s_iEnd; j++)
				if (TSTB(pBorrow->pBase->bf, j))
					TokenCopy(parse_UIGenInternal, j, pInternal, pBorrow->pBase, false);
		}
		if (pTemplate->pBase)
			for (j = s_iStart; j <= s_iEnd; j++)
				if (TSTB(pTemplate->pBase->bf, j))
					TokenCopy(parse_UIGenInternal, j, pInternal, pTemplate->pBase, false);
		
		return pInternal;
	}
}

__forceinline static void GenLayoutCreateInstances(UIGen *pGen, UIGenLayoutBox *pLayout, UIGenLayoutBoxState *pState)
{
	static UIGen **s_eaTemplateChildren;
	UIGen *pTemplate = GET_REF(pLayout->hTemplate);
	ParseTable *pTable;
	void ***peaModel = NULL;
	S32 iCount = 0;
	S32 i, iTemplateChildrenCount;

	PERFINFO_AUTO_START_FUNC();

	PERFINFO_AUTO_START("Evaluating model", 1);
	if (pLayout->pModel)
	{
		ui_GenTimeEvaluate(pGen, pLayout->pModel, NULL, "Model");
		if (pGen->pCode && pGen->pCode->uiFrameLastUpdate != g_ui_State.uiFrameCount)
		{
			ui_GenSetList(pGen, NULL, NULL);
			ErrorFilenamef(pGen->pchFilename, "%s: Model expression ran but did not update the list this frame.\n\n%s", 
				pGen->pchName, exprGetCompleteString(pLayout->pModel));
		}
	}
	else
	{
		ui_GenSetList(pGen, NULL, NULL);
	}
	PERFINFO_AUTO_STOP();

	peaModel = ui_GenGetList(pGen, NULL, &pTable);
	if (pLayout->pFilter)
	{
		ExprContext *pContext = ui_GenGetContext(pGen);
		Expression *pFilter = pLayout->pFilter;
		void ***peaFilteredList = &pState->eaFilteredList;
		eaClearFast(peaFilteredList);

		exprContextSetPointerVarPooledCached(pContext, s_pchLayoutBox, pGen, parse_UIGen, true, true, &s_LayoutBoxHandle);
		exprContextSetIntVarPooledCached(pContext, s_GenInstanceRowString, -1, &s_GenInstanceRowHandle);
		exprContextSetIntVarPooledCached(pContext, s_GenInstanceColumnString, -1, &s_GenInstanceColumnHandle);

		for (i = 0; i < eaSize(peaModel); i++)
		{
			void *pData = (*peaModel)[i];
			MultiVal mv = {0};
			exprContextSetPointerVarPooledCached(pContext, s_GenInstanceDataString, pData, pTable, true, true, &s_GenInstanceDataHandle);
			exprContextSetPointerVarPooledCached(pContext, g_ui_pchGenData, pData, pTable, true, true, &g_ui_iGenData);
			exprContextSetIntVarPooledCached(pContext, s_GenInstanceNumberString, i, &s_GenInstanceNumberHandle);

			ui_GenTimeEvaluateWithContext(pGen, pFilter, &mv, pContext, "Filter");
			if (MultiValToBool(&mv))
			{
				eaPush(peaFilteredList, pData);
			}
		}

		peaModel = peaFilteredList;
	}
	else if (pState->eaFilteredList)
	{
		eaDestroy(&pState->eaFilteredList);
	}

	if (!eaSize(peaModel) && !eaSize(&pLayout->eaTemplateChild))
	{
		eaClearFast(&pState->eaInstanceGens);
		while (eaSize(&pState->eaInstances) > 0)
			StructDestroy(parse_UIGenLayoutBoxInstance, eaPop(&pState->eaInstances));
		eaDestroyStruct(&pState->eaInternals, parse_UIGenNamedInternal);
	}

	if (!pLayout->bAppendTemplateChildren)
	{
		for (i = 0; i < eaSize(&pLayout->eaTemplateChild); i++)
		{
			UIGen *pTemplateChild = GET_REF(pLayout->eaTemplateChild[i]->hChild);
			if (pTemplateChild && eaFind(&s_eaTemplateChildren, pTemplateChild) < 0)
			{
				UIGenLayoutBoxInstance *pInstance = eaGetStruct(&pState->eaInstances, parse_UIGenLayoutBoxInstance, iCount++);
				if (pInstance->pGen)
					StructDestroySafe(parse_UIGen, &pInstance->pGen);
				if (pInstance->pTemplate != pTemplateChild)
				{
					pInstance->pTemplate = pTemplateChild;
					pInstance->bNewInstance = true;
					++pState->iNewInstances;
				}
				pInstance->iInstanceNumber = iCount - 1;
				pInstance->iDataNumber = -1;
				eaPush(&s_eaTemplateChildren, pTemplateChild);
			}
		}
	}

	iTemplateChildrenCount = iCount;

	if (pLayout->pTemplateExpr)
	{
		ExprContext *pContext = ui_GenGetContext(pGen);

		PERFINFO_AUTO_START("Evaluating TemplateExpr", 1);
		exprContextSetPointerVarPooledCached(pContext, s_pchLayoutBox, pGen, parse_UIGen, true, true, &s_LayoutBoxHandle);
		for (i = 0; i < eaSize(peaModel); i++)
		{
			UIGenLayoutBoxInstance *pInstance = eaGet(&pState->eaInstances, iCount);
			void *pData = (*peaModel)[i];
			MultiVal mv = {0};
			char **estr = NULL;
			pTemplate = NULL;

			exprContextSetPointerVarPooledCached(pContext, s_GenInstanceDataString, pData, pTable, true, true, &s_GenInstanceDataHandle);
			exprContextSetPointerVarPooledCached(pContext, g_ui_pchGenData, pData, pTable, true, true, &g_ui_iGenData);
			exprContextSetIntVarPooledCached(pContext, s_GenInstanceNumberString, i + iTemplateChildrenCount, &s_GenInstanceNumberHandle);

			ui_GenTimeEvaluateWithContext(pGen, pLayout->pTemplateExpr, &mv, pContext, "TemplateExpr");
			if (MultiValIsString(&mv) && mv.str && *mv.str)
			{
				pTemplate = RefSystem_ReferentFromString(g_GenState.hGenDict, mv.str);
				if (!pTemplate && eaPushUnique(&pState->eaBadTemplateNames, allocAddString(mv.str)) < 0)
					ErrorFilenamef(pGen->pchFilename, "%s: Invalid gen name specified (%s)", pGen->pchName, mv.str);
			}

			if (pTemplate)
			{
				if (!pInstance || pInstance->pTemplate != pTemplate)
				{
					UIGen *pNewInstanceGen;
					PERFINFO_AUTO_START("Creating Template Instance", 1);
					pNewInstanceGen = ui_GenClone(pTemplate);
					if (!pInstance)
						pInstance = StructCreate(parse_UIGenLayoutBoxInstance);
					StructDestroy(parse_UIGen, pInstance->pGen);
					pInstance->pTemplate = pTemplate;
					pInstance->pGen = pNewInstanceGen;
					pInstance->bNewInstance = true;
					++pState->iNewInstances;
					eaSet(&pState->eaInstances, pInstance, iCount);
					eaSet(&pState->eaInstanceGens, pNewInstanceGen, iCount);
					
					PERFINFO_AUTO_STOP();
				}
				pInstance->iInstanceNumber = iCount++;
				pInstance->iDataNumber = i;
			}
			else if (pInstance && pInstance->iDataNumber == i)
			{
				StructDestroy(parse_UIGenLayoutBoxInstance, eaRemove(&pState->eaInstances, iCount));
				eaRemove(&pState->eaInstanceGens, iCount);
			}
		}
		PERFINFO_AUTO_STOP();
	}
	else if (pTemplate)
	{
		UIGenLayoutBoxInstance *pInstance;
		S32 iUpdate;
		iCount += eaSize(peaModel);
		iUpdate = MIN(eaSize(peaModel), eaSize(&pState->eaInstances) - iTemplateChildrenCount);
		for (i = 0; i < iUpdate; i++)
		{
			pInstance = pState->eaInstances[i + iTemplateChildrenCount];
			if (pInstance->pTemplate != pTemplate)
			{
				StructDestroySafe(parse_UIGen, &pInstance->pGen);
				pInstance->pGen = ui_GenClone(pTemplate);
				pInstance->pTemplate = pTemplate;
				pInstance->iInstanceNumber = i + iTemplateChildrenCount;
				pInstance->iDataNumber = i;
				pInstance->bNewInstance = true;
				++pState->iNewInstances;
				ui_GenGetBase(pInstance->pGen, true);
			}
		}
		while (iCount > eaSize(&pState->eaInstances))
		{
			UIGen *pNewChild = ui_GenClone(pTemplate);
			UIGenInternal *pChildBase = ui_GenGetBase(pNewChild, true);
			pInstance = StructCreate(parse_UIGenLayoutBoxInstance);
			pInstance->pGen = pNewChild;
			pInstance->pTemplate = pTemplate;
			pInstance->iInstanceNumber = eaSize(&pState->eaInstances);
			pInstance->iDataNumber = pInstance->iInstanceNumber - iTemplateChildrenCount;
			pInstance->bNewInstance = true;
			++pState->iNewInstances;
			eaPush(&pState->eaInstances, pInstance);
			eaPush(&pState->eaInstanceGens, pNewChild);
		}
	}

	if (pLayout->bAppendTemplateChildren)
	{
		for (i = 0; i < eaSize(&pLayout->eaTemplateChild); i++)
		{
			UIGen *pTemplateChild = GET_REF(pLayout->eaTemplateChild[i]->hChild);
			if (pTemplateChild && eaFind(&s_eaTemplateChildren, pTemplateChild) < 0)
			{
				UIGenLayoutBoxInstance *pInstance = eaGetStruct(&pState->eaInstances, parse_UIGenLayoutBoxInstance, iCount++);
				if (pInstance->pGen)
					StructDestroySafe(parse_UIGen, &pInstance->pGen);
				if (pInstance->pTemplate != pTemplateChild)
				{
					pInstance->pTemplate = pTemplateChild;
					pInstance->bNewInstance = true;
					++pState->iNewInstances;
				}
				pInstance->iInstanceNumber = iCount - 1;
				pInstance->iDataNumber = -1;
				eaPush(&s_eaTemplateChildren, pTemplateChild);
			}
		}
	}

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

	while (iCount < eaSize(&pState->eaInstances))
	{
		StructDestroy(parse_UIGenLayoutBoxInstance, eaPop(&pState->eaInstances));
		eaPop(&pState->eaInstanceGens);
	}

	for (i = 0; i < eaSize(&pState->eaInstances); i++)
	{
		UIGen *pInstanceGen = pState->eaInstances[i]->pGen ? pState->eaInstances[i]->pGen : pState->eaInstances[i]->pTemplate;
		ui_GenStates(pInstanceGen,
			kUIGenStateFirst, i == 0,
			kUIGenStateLast, i == (eaSize(&pState->eaInstances) - 1),
			kUIGenStateEven, (i & 1) == 0,
			0);
		if (pState->iSelected != UI_GEN_LAYOUTBOX_NO_SELECTION)
		{
			ui_GenState(pInstanceGen, kUIGenStateSelected, i == pState->iSelected);
		}
	}

	ui_GenStates(pGen,
		kUIGenStateEmpty, !i,
		kUIGenStateFilled, i,
		0);

	PERFINFO_AUTO_STOP_FUNC();
}

__forceinline static void GenLayoutBoxCalcRowsColumns(UIGen *pGen, UIGenLayoutBox *pLayout)
{
	UIGenLayoutBoxState *pState = UI_GEN_STATE(pGen, LayoutBox);
	F32 fScale = pGen->fScale;
	UIGen *pTemplate = 
		GET_REF(pLayout->hTemplate) 
			? GET_REF(pLayout->hTemplate) 
			: eaSize(&pState->eaInstances) 
				? (pState->eaInstances[0]->pGen ? pState->eaInstances[0]->pGen : pState->eaInstances[0]->pTemplate)
				: NULL;
	UIGenInternal *pTemplateBase = pTemplate ? GenLayoutGetBase(pGen, pTemplate) : NULL;
	S32 iInstanceCount = eaSize(&pState->eaInstances);
	if (!iInstanceCount || !pTemplateBase)
	{
		pState->iRows = 0;
		pState->iColumns = 0;
		return;
	}
	pState->eLayoutDirection = pLayout->eLayoutDirection ? pLayout->eLayoutDirection : pTemplateBase ? pTemplateBase->pos.eOffsetFrom : UITopLeft;
	switch (pState->eLayoutDirection)
	{
	case UILeft:
	case UIRight:
		if (pLayout->bVariableSize)
		{
			pState->iRows = 0;
			pState->iColumns = 0;
		}
		else
		{
			pState->iRows = 1;
			pState->iColumns = iInstanceCount;
		}
		break;
	case UITop:
	case UIBottom:
		if (pLayout->bVariableSize)
		{
			pState->iRows = 0;
			pState->iColumns = 0;
		}
		else
		{
			pState->iColumns = 1;
			pState->iRows = iInstanceCount;
		}
		break;
	default:
		// If it's an unknown/unexpected direction, use the TopLeft
		// size computation behavior.
		{
			if (pLayout->eLayoutDirection)
			{
				ErrorFilenamef(pTemplate->pchFilename, "%s: Layout box must use LayoutDirection Left, Right, Top, Bottom, TopLeft, TopRight, BottomLeft, or BottomRight, not %s", pTemplate->pchName,
					StaticDefineIntRevLookup(UIDirectionEnum, pLayout->eLayoutDirection));
			}
			else if (pTemplateBase)
			{
				ErrorFilenamef(pTemplate->pchFilename, "%s: Layout box instances must use OffsetFrom Left, Right, Top, Bottom, TopLeft, TopRight, BottomLeft, or BottomRight, not %s", pTemplate->pchName,
					StaticDefineIntRevLookup(UIDirectionEnum, pTemplateBase->pos.eOffsetFrom));
			}
		}
	case UITopLeft:
	case UITopRight:
	case UIBottomLeft:
	case UIBottomRight:
		{
			MultiVal mv;
			F32 fAspectRatio = pLayout->fAspectRatio;
			if (pLayout->bVariableSize)
			{
				pState->iRows = pLayout->uiRows;
				pState->iColumns = 0;
				if (pLayout->pColumns)
				{
					ui_GenEvaluate(pGen, pLayout->pColumns, &mv);
					pState->iColumns = max(1, MultiValGetInt(&mv, NULL));
				}
			}
			else if (pLayout->uiRows)
			{
				pState->iRows = pLayout->uiRows;
				pState->iColumns = ceil(((float)iInstanceCount) / pState->iRows);
			}
			else if (pLayout->pColumns)
			{
				ui_GenEvaluate(pGen, pLayout->pColumns, &mv);
				pState->iColumns = max(1, MultiValGetInt(&mv, NULL));
				pState->iRows = ceil(((float)iInstanceCount) / pState->iColumns);
			}
			else if (!fAspectRatio)
			{
				CBox dummy = {0, 0, 0, 0};
				CBox MyBox = {0, 0, 0, 0};
				CBox TemplateBox = {0, 0, 0, 0};
				ui_GenPositionToCBox(&pLayout->polyp.pos, &pGen->pParent->ScreenBox, pGen->pParent->fScale, pGen->fScale, &MyBox, &dummy, &dummy, NULL);
				ui_GenPositionToCBox(&pTemplateBase->pos, &MyBox, pGen->fScale, pTemplateBase->pos.fScale, &TemplateBox, &dummy, &dummy, NULL);
				if (MyBox.lx != MyBox.hx && CBoxWidth(&TemplateBox))
				{
					// Constrained by width.
					F32 fWidth = ceil(MyBox.hx - MyBox.lx);
					pState->iColumns = fWidth / (CBoxWidth(&TemplateBox) + pLayout->fHorizontalSpacing * pGen->fScale);
					MAX1(pState->iColumns, 1);
					pState->iRows = ceil(((float)iInstanceCount) / pState->iColumns);
				}
				else if (MyBox.ly != MyBox.hy && CBoxHeight(&TemplateBox) > 0)
				{
					// Constrained by height.
					F32 fHeight = ceil(MyBox.hy - MyBox.ly);
					pState->iRows = fHeight / (CBoxHeight(&TemplateBox) + pLayout->fVerticalSpacing * pGen->fScale);
					MAX1(pState->iRows, 1);
					pState->iColumns = ceil(((float)iInstanceCount) / pState->iRows);
				}
				else
				{
					// Unconstrained in either direction! Assume AspectRatio = 1.
					fAspectRatio = 1.f;
				}
			}
			if (fAspectRatio && !pLayout->bVariableSize)
			{
				if (iInstanceCount) {
					pState->iRows = ceil(sqrt((F64)iInstanceCount / fAspectRatio));
					MAX1(pState->iRows, 1);
					pState->iColumns = ceil((F64)iInstanceCount / (F64)pState->iRows);
				} else {
					pState->iRows = 0;
					pState->iColumns = 0;
				}
			}
		}
		break;
	}
}

void ui_GenPointerUpdateLayoutBox(UIGen *pGen)
{
	UIGenLayoutBox *pLayout = UI_GEN_RESULT(pGen, LayoutBox);
	if (pLayout)
	{
		UIGenLayoutBoxState *pState = UI_GEN_STATE(pGen, LayoutBox);
		GenLayoutCreateInstances(pGen, pLayout, pState);
		GenLayoutBoxCalcRowsColumns(pGen, pLayout);
		ui_GenForEachLayoutBoxInstance(&pState->eaInstances, ui_GenPointerUpdateCB, pGen, UI_GEN_UPDATE_ORDER);
	}
}

__forceinline static void GenLayoutInstances(UIGen *pGen, UIGenLayoutBox *pLayout, UIGenLayoutBoxState *pState, UIGenLayoutBoxInstance ***peaInstances)
{
	UIGen *pTemplate = 
		GET_REF(pLayout->hTemplate) 
			? GET_REF(pLayout->hTemplate) 
			: eaSize(&pState->eaInstances) 
				? (pState->eaInstances[0]->pGen ? pState->eaInstances[0]->pGen : pState->eaInstances[0]->pTemplate)
				: NULL;
	UIGenInternal *pTemplateBase = pTemplate && eaSize(&pState->eaInstances) ? GenLayoutGetBase(pGen, pTemplate) : NULL;
	CBox templateBox = {0, 0, 0, 0};
	F32 fScale = pGen->fScale;
	F32 fPercentX = 0.f;
	F32 fPercentY = 0.f;
	F32 fX = 0.f;
	F32 fY = 0.f;
	Vec2 v2MaxDimension = {0, 0};
	Vec2 v2Start = {0, 0};
	Vec2 v2RunningStart = {0, 0};
	Vec2 v2GenSize = {CBoxWidth(&pGen->ScreenBox) / fScale, CBoxHeight(&pGen->ScreenBox) / fScale};
	bool bColumnMajor = false;
	bool bVariableSize = pLayout->bVariableSize;
	S32 i;
	S32 iColumn = 0;
	S32 iRow = 0;
	CBox Size = {0, 0, 0, 0};
	CBox FitContents = {0, 0, 0, 0};
	CBox FitParent = {0, 0, 0, 0};
	S32 iMaxColumn = 0;
	S32 iMaxRow = 0;
	if (!pTemplate || !pTemplateBase)
		return;

	// Determine if it's column major or minor
	switch (pLayout->eLayoutOrder)
	{
	case UIHorizontal:
		bColumnMajor = false;
		break;
	case UIVertical:
		bColumnMajor = true;
		break;
	default:
		bColumnMajor = false;
		ErrorFilenamef(pTemplate->pchFilename, "%s: Layout box must use LayoutOrder Horizontal or Vertical, not %s", pTemplate->pchName,
			StaticDefineIntRevLookup(UIDirectionEnum, pLayout->eLayoutOrder));
		break;
	}

	iMaxRow = MAX(0, pState->iRows);
	iMaxColumn = MAX(0, pState->iColumns);
	switch (pState->eLayoutDirection)
	{
	case UILeft:
	case UIRight:
		iMaxRow = 1;
		break;
	case UITop:
	case UIBottom:
		iMaxColumn = 1;
		break;
	}

	pState->fBoundWidth = 0;
	pState->fBoundHeight = 0;

	ui_GenPositionToCBox(&pTemplateBase->pos, &pGen->ScreenBox, 1.f, pTemplateBase->pos.fScale, &templateBox, &templateBox, &templateBox, NULL);

	for (i = 0; i < eaSize(peaInstances); i++)
	{
		UIGenLayoutBoxInstance *pInstance = (*peaInstances)[i];
		UIGen *pChild;
		UIGenInternal *pChildBase, *pPositionInstance;

		if (pInstance->pGen)
		{
			pChild = pInstance->pGen;
			pChildBase = ui_GenGetBase(pChild, true);
			pPositionInstance = pChildBase;
		}
		else
		{
			pChild = pInstance->pTemplate;
			pChildBase = ui_GenGetBase(pChild, true);
			pPositionInstance = ui_GenGetCodeOverrideEarly(pChild, true);
		}

		if (pChild && bVariableSize)
		{
			UIGenInternal *pChildResult = pChild->pResult;
			UIGenInternal *pFitContentsInternal = pChildResult ? pChildResult : pChildBase;
			Vec2 v2Size;

			// Calculate size of instance
			if (pFitContentsInternal->pos.Width.eUnit == UIUnitFitContents || pFitContentsInternal->pos.Height.eUnit == UIUnitFitContents ||
				pFitContentsInternal->pos.MinimumWidth.eUnit == UIUnitFitContents || pFitContentsInternal->pos.MinimumHeight.eUnit == UIUnitFitContents ||
				pFitContentsInternal->pos.MaximumWidth.eUnit == UIUnitFitContents || pFitContentsInternal->pos.MaximumHeight.eUnit == UIUnitFitContents)
				ui_GenFitContentsSize(pChild, pFitContentsInternal, &FitContents);
			ui_GenPositionToCBox(pChildResult ? &pChildResult->pos : &pChildBase->pos, &pGen->ScreenBox, fScale, pChildResult ? pChildResult->pos.fScale : pChildBase->pos.fScale, &Size, &FitContents, &FitParent, NULL);

			v2Size[0] = CBoxWidth(&Size) / fScale;
			v2Size[1] = CBoxHeight(&Size) / fScale;

			if (bColumnMajor)
			{
				if (v2RunningStart[1] + v2Size[1] > v2GenSize[1] || iMaxRow && iRow >= iMaxRow)
				{
					if (!iMaxColumn || iColumn + 1 < iMaxColumn)
					{
						// Next column
						v2Start[0] += v2MaxDimension[0] + pLayout->fHorizontalSpacing * fScale;
						MAX1(pState->fBoundWidth, v2Start[0] + v2MaxDimension[0]);
						MAX1(pState->fBoundHeight, v2Start[1] + v2RunningStart[1]);
						v2MaxDimension[0] = 0;
						v2RunningStart[1] = 0;
						iColumn++;
						iRow = 0;
					}
				}

				fX = round(v2Start[0]);
				fY = round(v2RunningStart[1]);
				iRow++;
			}
			else
			{
				if (v2RunningStart[0] + v2Size[0] > v2GenSize[0] || iMaxColumn && iColumn >= iMaxColumn)
				{
					if (!iMaxRow || iRow + 1 < iMaxRow)
					{
						// Next row
						v2Start[1] += v2MaxDimension[1] + pLayout->fVerticalSpacing * fScale;
						MAX1(pState->fBoundWidth, v2Start[0] + v2RunningStart[0]);
						MAX1(pState->fBoundHeight, v2Start[1] + v2MaxDimension[1]);
						v2MaxDimension[1] = 0;
						v2RunningStart[0] = 0;
						iRow++;
						iColumn = 0;
					}
				}

				fX = round(v2RunningStart[0]);
				fY = round(v2Start[1]);
				iColumn++;
			}

			// Poke values into base
			if (!(UI_GEN_NEARF(fX, pPositionInstance->pos.iX) && UI_GEN_NEARF(fY, pPositionInstance->pos.iY) &&
				pPositionInstance->pos.eOffsetFrom == pState->eLayoutDirection))
			{
				SETB(pPositionInstance->bf, s_iGenColumnX);
				SETB(pPositionInstance->bf, s_iGenColumnY);
				SETB(pPositionInstance->bf, s_iGenColumnOffsetFrom);
				pPositionInstance->pos.iX = fX;
				pPositionInstance->pos.iY = fY;
				pPositionInstance->pos.eOffsetFrom = pState->eLayoutDirection;
				ui_GenMarkDirty(pChild);
			}

			// Update counters
			MAX1(v2MaxDimension[0], v2Size[0]);
			v2RunningStart[0] += round(v2Size[0] + pLayout->fHorizontalSpacing * fScale);
			MAX1(v2MaxDimension[1], v2Size[1]);
			v2RunningStart[1] += round(v2Size[1] + pLayout->fVerticalSpacing * fScale);
		}
		else if (pChild)
		{
			iColumn = bColumnMajor ? pInstance->iInstanceNumber / pState->iRows : pInstance->iInstanceNumber % pState->iColumns;
			iRow = bColumnMajor ? pInstance->iInstanceNumber % pState->iRows : pInstance->iInstanceNumber / pState->iColumns;

			if (pTemplateBase->pos.Width.eUnit == UIUnitPercentage)
			{
				F32 fWidth = 1.f / pState->iColumns;
				S32 iLeftMargin = (iColumn > 0) ? pLayout->fHorizontalSpacing / 2 : 0;
				S32 iRightMargin = (iColumn < pState->iColumns - 1) ? pLayout->fHorizontalSpacing / 2 : 0;
				fPercentX = ((F32)iColumn / pState->iColumns);
				if (!(nearf(fWidth, pPositionInstance->pos.Width.fMagnitude) &&
					nearf(fPercentX, pPositionInstance->pos.fPercentX) &&
					(iLeftMargin == pPositionInstance->pos.iLeftMargin) &&
					(iRightMargin == pPositionInstance->pos.iRightMargin)))
				{
					SETB(pPositionInstance->bf, s_iGenColumnWidth);
					SETB(pPositionInstance->bf, s_iGenColumnPercentX);
					SETB(pPositionInstance->bf, s_iGenColumnLeftMargin);
					SETB(pPositionInstance->bf, s_iGenColumnRightMargin);
					pPositionInstance->pos.Width.fMagnitude = fWidth;
					pPositionInstance->pos.Width.eUnit = UIUnitPercentage;
					pPositionInstance->pos.fPercentX = fPercentX;
					pPositionInstance->pos.iLeftMargin = iLeftMargin;
					pPositionInstance->pos.iRightMargin = iRightMargin;
					ui_GenMarkDirty(pChild);
				}
			}
			else
			{
				fX = (pLayout->fHorizontalSpacing + CBoxWidth(&templateBox)) * iColumn;
				if (pLayout->pCalculateX)
				{
					MultiVal mv;
					ui_GenSetFloatVar("GenInstanceX", fX);
					ui_GenEvaluate(pChild, pLayout->pCalculateX, &mv);
					fX = MultiValGetFloat(&mv, NULL);
				}
				if (!UI_GEN_NEARF(pPositionInstance->pos.iX, fX))
				{
					SETB(pPositionInstance->bf, s_iGenColumnX);
					pPositionInstance->pos.iX = fX;
					ui_GenMarkDirty(pChild);
				}
			}

			if (pTemplateBase->pos.Height.eUnit == UIUnitPercentage)
			{
				F32 fHeight = 1.f / pState->iRows;
				S32 iTopMargin = (iRow > 0) ? pLayout->fVerticalSpacing / 2 : 0;
				S32 iBottomMargin = (iRow < pState->iRows - 1) ? pLayout->fVerticalSpacing / 2 : 0;
				fPercentY = ((F32)iRow / pState->iRows);
				if (!(nearf(fHeight, pPositionInstance->pos.Height.fMagnitude) &&
					nearf(fPercentY, pPositionInstance->pos.fPercentY) &&
					(iTopMargin == pPositionInstance->pos.iTopMargin) &&
					(iBottomMargin == pPositionInstance->pos.iBottomMargin)))
				{
					SETB(pPositionInstance->bf, s_iGenColumnHeight);
					SETB(pPositionInstance->bf, s_iGenColumnPercentY);
					SETB(pPositionInstance->bf, s_iGenColumnTopMargin);
					SETB(pPositionInstance->bf, s_iGenColumnBottomMargin);
					pPositionInstance->pos.Height.fMagnitude = fHeight;
					pPositionInstance->pos.Height.eUnit = UIUnitPercentage;
					pPositionInstance->pos.fPercentY = fPercentY;
					pPositionInstance->pos.iTopMargin = iTopMargin;
					pPositionInstance->pos.iBottomMargin = iBottomMargin;
					ui_GenMarkDirty(pChild);
				}
			}
			else
			{
				fY = (pLayout->fVerticalSpacing + CBoxHeight(&templateBox)) * iRow;
				if (pLayout->pCalculateY)
				{
					MultiVal mv;
					ui_GenSetFloatVar("GenInstanceY", fY);
					ui_GenEvaluate(pChild, pLayout->pCalculateY, &mv);
					fY = MultiValGetFloat(&mv, NULL);
				}
				if (!UI_GEN_NEARF(pPositionInstance->pos.iY, fY))
				{
					SETB(pPositionInstance->bf, s_iGenColumnY);
					pPositionInstance->pos.iY = fY;
					ui_GenMarkDirty(pChild);
				}
			}

			// Poke values into base
			if (pPositionInstance->pos.eOffsetFrom != pState->eLayoutDirection)
			{
				SETB(pPositionInstance->bf, s_iGenColumnOffsetFrom);
				pPositionInstance->pos.eOffsetFrom = pState->eLayoutDirection;
				ui_GenMarkDirty(pChild);
			}
		}

		pInstance->iColumn = iColumn;
		pInstance->iRow = iRow;
	}

	if (bVariableSize)
	{
		if (bColumnMajor)
		{
			MAX1(pState->fBoundWidth, v2Start[0] + v2MaxDimension[0]);
			MAX1(pState->fBoundHeight, v2Start[1] + v2RunningStart[1]);
		}
		else
		{
			MAX1(pState->fBoundWidth, v2Start[0] + v2RunningStart[0]);
			MAX1(pState->fBoundHeight, v2Start[1] + v2MaxDimension[1]);
		}
	}

	ui_GenForEachLayoutBoxInstance(peaInstances, ui_GenLayoutCB, pGen, UI_GEN_LAYOUT_ORDER);
}

void ui_GenUpdateLayoutBox(UIGen *pGen)
{
	UIGenLayoutBox *pLayout = UI_GEN_RESULT(pGen, LayoutBox);
	UIGenLayoutBoxState *pState = UI_GEN_STATE(pGen, LayoutBox);
	if (pState->iNewInstances > 0)
	{
		static UIGenLayoutBoxInstance **s_eaNewInstances;
		S32 i, n = eaSize(&pState->eaInstances);
		for (i = 0; i < n; i++)
		{
			UIGenLayoutBoxInstance *pInstance = pState->eaInstances[i];
			if (pInstance->bNewInstance)
			{
				eaPush(&s_eaNewInstances, pInstance);
				pInstance->bNewInstance = false;
			}
		}
		GenLayoutInstances(pGen, pLayout, pState, &s_eaNewInstances);
		eaClearFast(&s_eaNewInstances);
		pState->iNewInstances = 0;
	}
	ui_GenForEachLayoutBoxInstance(&pState->eaInstances, ui_GenUpdateCB, pGen, UI_GEN_UPDATE_ORDER);
}

void ui_GenLayoutLateLayoutBox(UIGen *pGen)
{
	UIGenLayoutBox *pLayout = UI_GEN_RESULT(pGen, LayoutBox);
	UIGenLayoutBoxState *pState = UI_GEN_STATE(pGen, LayoutBox);
	GenLayoutInstances(pGen, pLayout, pState, &pState->eaInstances);
}

void ui_GenTickEarlyLayoutBox(UIGen *pGen)
{
	UIGenLayoutBoxState *pState = UI_GEN_STATE(pGen, LayoutBox);
	if (pState)
		ui_GenForEachLayoutBoxInstanceInPriority(&pState->eaInstances, ui_GenTickCB, pGen, UI_GEN_TICK_ORDER);
}

void ui_GenDrawEarlyLayoutBox(UIGen *pGen)
{
	UIGenLayoutBoxState *pState = UI_GEN_STATE(pGen, LayoutBox);
	if (pState)
		ui_GenForEachLayoutBoxInstanceInPriority(&pState->eaInstances, ui_GenDrawCB, pGen, UI_GEN_DRAW_ORDER);
}

void ui_GenHideLayoutBox(UIGen *pGen)
{
	UIGenLayoutBoxState *pState = UI_GEN_STATE(pGen, LayoutBox);
	if (pState)
	{
		eaDestroy(&pState->eaFilteredList);
		eaDestroy(&pState->eaInstanceGens);
		while (eaSize(&pState->eaInstances) > 0)
			StructDestroy(parse_UIGenLayoutBoxInstance, eaPop(&pState->eaInstances));
		while (eaSize(&pState->eaTemplateGens) > 0)
		{
			UIGenChild *pChild = eaPop(&pState->eaTemplateGens);
			UIGen *pChildGen = GET_REF(pChild->hChild);
			if (pChildGen)
				ui_GenReset(pChildGen);
		}
	}
}

void ui_GenUpdateContextLayoutBox(UIGen *pGen, ExprContext *pContext, UIGen *pFor)
{
	UIGenLayoutBox *pLayout = UI_GEN_RESULT(pGen, LayoutBox);
	UIGenLayoutBoxState *pState = UI_GEN_STATE(pGen, LayoutBox);
	UIGen *pIter;
	if (!pState || !pLayout)
		return;
	for (pIter = pFor; pIter && pIter != pGen; pIter = pIter->pParent)
	{
		int i;
		for (i = 0; i < eaSize(&pState->eaInstances); i++)
		{
			UIGenLayoutBoxInstance *pInstance = pState->eaInstances[i];
			if (pInstance->pGen == pIter)
			{
				ParseTable *pTable;
				void ***peaModel = ui_GenGetList(pGen, NULL, &pTable);
				void *pData = eaGet(pState->eaFilteredList ? &pState->eaFilteredList : peaModel, pInstance->iDataNumber);

				exprContextSetIntVarPooledCached(pContext, s_GenInstanceRowString, pInstance->iRow, &s_GenInstanceRowHandle);
				exprContextSetIntVarPooledCached(pContext, s_GenInstanceColumnString, pInstance->iColumn, &s_GenInstanceColumnHandle);
				exprContextSetPointerVarPooledCached(pContext, s_GenInstanceDataString, pData, pTable, true, true, &s_GenInstanceDataHandle);
				exprContextSetPointerVarPooledCached(pContext, g_ui_pchGenData, pData, pTable, true, true, &g_ui_iGenData);
				exprContextSetIntVarPooledCached(pContext, s_GenInstanceNumberString, i, &s_GenInstanceNumberHandle);
				goto doublebreak;
			}
			else if (!pInstance->pGen && pInstance->pTemplate == pIter)
			{
				exprContextSetIntVarPooledCached(pContext, s_GenInstanceRowString, pInstance->iRow, &s_GenInstanceRowHandle);
				exprContextSetIntVarPooledCached(pContext, s_GenInstanceColumnString, pInstance->iColumn, &s_GenInstanceColumnHandle);
				exprContextSetIntVarPooledCached(pContext, s_GenInstanceNumberString, i, &s_GenInstanceNumberHandle);
				goto doublebreak;
			}
		}
	}

doublebreak:
	exprContextSetPointerVarPooledCached(pContext, s_pchLayoutBox, pGen, parse_UIGen, true, true, &s_LayoutBoxHandle);
	exprContextSetIntVarPooledCached(pContext, s_GenInstanceRowCountString, pLayout->bVariableSize ? 0 : pState->iRows, &s_GenInstanceRowCountHandle);
	exprContextSetIntVarPooledCached(pContext, s_GenInstanceColumnCountString, pLayout->bVariableSize ? 0 : pState->iColumns, &s_GenInstanceColumnCountHandle);
	exprContextSetIntVarPooledCached(pContext, s_GenInstanceCountString, eaSize(&pState->eaInstances), &s_GenInstanceCountHandle);
}

void ui_GenFitContentsSizeLayoutBox(UIGen *pGen, UIGenLayoutBox *pLayout, CBox *pOut)
{
	UIGenLayoutBoxState *pState = UI_GEN_STATE(pGen, LayoutBox);
	if (pLayout->bVariableSize)
	{
		BuildCBox(pOut, 0, 0, pState->fBoundWidth, pState->fBoundHeight);
	}
	else
	{
		UIGen *pTemplate = GET_REF(pLayout->hTemplate);
		UIGenInternal *pTemplateBase = pTemplate ? GenLayoutGetBase(pGen, pTemplate) : NULL;
		CBox templateBox = {0, 0, 0, 0};
		CBox templateNativeBox = {0, 0, 0, 0};
		if (!pTemplateBase)
			return;

		ui_GenPositionToCBox(&pTemplateBase->pos, &templateNativeBox, 1.f, pTemplateBase->pos.fScale, &templateBox, &templateNativeBox, &templateNativeBox, NULL);
		BuildCBox(pOut, 0, 0,
			(CBoxWidth(&templateBox) * pState->iColumns) + (pLayout->fHorizontalSpacing * (pState->iColumns - 1)),
			(CBoxHeight(&templateBox) * pState->iRows) + (pLayout->fVerticalSpacing * (pState->iRows - 1))
			);
	}
}

void ui_GenQueueResetLayoutBox(UIGen *pGen)
{
	UIGenLayoutBoxState *pState = UI_GEN_STATE(pGen, LayoutBox);
	ui_GenForEachLayoutBoxInstance(&pState->eaInstances, ui_GenQueueResetChildren, pGen, false);
}

// Set the selected element of a layout box, by index. If clamp is true, then
// out of bounds indices will be forced in-bounds.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenLayoutBoxSetSelectedIndex);
void uiexpr_GenLayoutBoxSetSelectedIndex(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, S32 iIndex, bool bClamp)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeLayoutBox))
	{
		UIGenLayoutBoxState *pState = UI_GEN_STATE(pGen, LayoutBox);
		if (pState)
		{
			if (bClamp)
			{
				if (eaSize(&pState->eaInstances))
					iIndex = CLAMP(iIndex, 0, eaSize(&pState->eaInstances) - 1);
				else
					iIndex = -1;
			}
			if (pState->iSelected != iIndex)
			{
				pState->iSelected = iIndex;
				ui_GenTutorialEvent(NULL, pGen, kUIGenTutorialEventTypeSelect);
			}
		}
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenLayoutBoxSetSelectedIndex: %s is not a layout box", pGen->pchName);
	}
}

// Set the selected element of a layout box, by instance. If required is true,
// then an error will occur if the instance was not found.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenLayoutBoxSetSelectedGen);
void uiexpr_GenLayoutBoxSetSelectedGen(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID UIGen *pInstance, bool bRequired)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeLayoutBox))
	{
		UIGenLayoutBoxState *pState = UI_GEN_STATE(pGen, LayoutBox);
		if (pState)
		{
			int i;
			for (i = 0; i < eaSize(&pState->eaInstances); i++)
			{
				if (pInstance == pState->eaInstances[i]->pGen)
				{
					if (pState->iSelected != i)
					{
						pState->iSelected = i;
						ui_GenTutorialEvent(NULL, pGen, kUIGenTutorialEventTypeSelect);
					}
					return;
				}
			}
			ErrorFilenamef(pGen->pchFilename, "%s: No instance %s.", pGen->pchName, pInstance ? pInstance->pchName : NULL);
		}
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenLayoutBoxSetSelectedGen: %s is not a layout box", pGen->pchName);
	}
}

// Get the selected element of a layout box.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenLayoutBoxGetSelectedGen);
SA_RET_OP_VALID UIGen *uiexpr_GenLayoutBoxGetSelectedGen(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeLayoutBox))
	{
		UIGenLayoutBoxState *pState = UI_GEN_STATE(pGen, LayoutBox);
		if (pState)
		{
			UIGenLayoutBoxInstance *pInstance = eaGet(&pState->eaInstances, pState->iSelected);
			return SAFE_MEMBER(pInstance, pGen);
		}
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenLayoutBoxGetSelectedGen: %s is not a layout box", pGen->pchName);
	}
	return NULL;
}

S32 ui_GenLayoutBoxGetSelectedIndex(UIGen *pGen)
{
	UIGenLayoutBoxState *pState = UI_GEN_STATE(pGen, LayoutBox);
	if (pState)
		return pState->iSelected;
	return 0;
}

// Get the selected index of a layout box.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenLayoutBoxGetSelectedIndex);
S32 uiexpr_GenLayoutBoxGetSelectedIndex(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeLayoutBox))
	{
		UIGenLayoutBoxState *pState = UI_GEN_STATE(pGen, LayoutBox);
		if (pState)
			return pState->iSelected;
	}
	else
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "GenLayoutBoxGetSelectedIndex: %s is not a layout box", pGen->pchName);
	}
	return 0;
}

bool ui_GenValidateLayoutBox(UIGen *pGen, UIGenInternal *pInternal, const char *pchDescriptor)
{
	UIGenLayoutBox *pLayoutBox = UI_GEN_INTERNAL(pInternal, LayoutBox);

	UI_GEN_FAIL_IF_RETURN(pGen, IS_HANDLE_ACTIVE(pLayoutBox->hTemplate) && !GET_REF(pLayoutBox->hTemplate), 0,
		"Gen uses undefined Template %s", REF_STRING_FROM_HANDLE(pLayoutBox->hTemplate));

	return true;
}


AUTO_RUN;
void ui_GenRegisterLayoutBox(void)
{
	s_GenInstanceRowString = allocAddStaticString("GenInstanceRow");
	s_GenInstanceColumnString = allocAddStaticString("GenInstanceColumn");
	s_GenInstanceDataString = allocAddStaticString("GenInstanceData");
	s_GenInstanceNumberString = allocAddStaticString("GenInstanceNumber");
	s_GenInstanceRowCountString = allocAddStaticString("GenInstanceRowCount");
	s_GenInstanceColumnCountString = allocAddStaticString("GenInstanceColumnCount");
	s_GenInstanceCountString = allocAddStaticString("GenInstanceCount");
	s_pchLayoutBox = allocAddStaticString("LayoutBox");

	ui_GenInitPointerVar(s_GenInstanceDataString, NULL);
	ui_GenInitPointerVar(s_pchLayoutBox, parse_UIGen);
	ui_GenInitIntVar(s_GenInstanceNumberString, -1);
	ui_GenInitIntVar(s_GenInstanceRowString, -1);
	ui_GenInitIntVar(s_GenInstanceColumnString, -1);
	ui_GenInitIntVar(s_GenInstanceRowCountString, -1);
	ui_GenInitIntVar(s_GenInstanceColumnCountString, -1);
	ui_GenInitIntVar(s_GenInstanceCountString, -1);
	ui_GenInitFloatVar("GenInstanceX");
	ui_GenInitFloatVar("GenInstanceY");

	ui_GenRegisterType(kUIGenTypeLayoutBox, 
		ui_GenValidateLayoutBox, 
		ui_GenPointerUpdateLayoutBox,
		ui_GenUpdateLayoutBox, 
		UI_GEN_NO_LAYOUTEARLY, 
		ui_GenLayoutLateLayoutBox, 
		ui_GenTickEarlyLayoutBox, 
		UI_GEN_NO_TICKLATE, 
		ui_GenDrawEarlyLayoutBox,
		ui_GenFitContentsSizeLayoutBox, 
		UI_GEN_NO_FITPARENTSIZE, 
		ui_GenHideLayoutBox, 
		UI_GEN_NO_INPUT, 
		ui_GenUpdateContextLayoutBox, 
		ui_GenQueueResetLayoutBox);

	assertmsg((s_iGenColumnX = ParserFindColumnFromOffset(parse_UIGenInternal, offsetof(UIGenInternal, pos.iX))) > 0, "Unable to find column for X");
	assertmsg((s_iGenColumnPercentX = ParserFindColumnFromOffset(parse_UIGenInternal, offsetof(UIGenInternal, pos.fPercentX))) > 0, "Unable to find column for PercentX");
	assertmsg((s_iGenColumnLeftMargin = ParserFindColumnFromOffset(parse_UIGenInternal, offsetof(UIGenInternal, pos.iLeftMargin))) > 0, "Unable to find column for LeftMargin");
	assertmsg((s_iGenColumnRightMargin = ParserFindColumnFromOffset(parse_UIGenInternal, offsetof(UIGenInternal, pos.iRightMargin))) > 0, "Unable to find column for RightMargin");
	assertmsg((s_iGenColumnWidth = ParserFindColumnFromOffset(parse_UIGenInternal, offsetof(UIGenInternal, pos.Width))) > 0, "Unable to find column for Width");

	assertmsg((s_iGenColumnY = ParserFindColumnFromOffset(parse_UIGenInternal, offsetof(UIGenInternal, pos.iY))) > 0, "Unable to find column for Y");
	assertmsg((s_iGenColumnPercentY = ParserFindColumnFromOffset(parse_UIGenInternal, offsetof(UIGenInternal, pos.fPercentY))) > 0, "Unable to find column for PercentY");
	assertmsg((s_iGenColumnTopMargin = ParserFindColumnFromOffset(parse_UIGenInternal, offsetof(UIGenInternal, pos.iTopMargin))) > 0, "Unable to find column for TopMargin");
	assertmsg((s_iGenColumnBottomMargin = ParserFindColumnFromOffset(parse_UIGenInternal, offsetof(UIGenInternal, pos.iBottomMargin))) > 0, "Unable to find column for BottomMargin");
	assertmsg((s_iGenColumnHeight = ParserFindColumnFromOffset(parse_UIGenInternal, offsetof(UIGenInternal, pos.Height))) > 0, "Unable to find column for Height");

	assertmsg((s_iGenColumnOffsetFrom = ParserFindColumnFromOffset(parse_UIGenInternal, offsetof(UIGenInternal, pos.eOffsetFrom))) > 0, "Unable to find column for OffsetFrom");
}

#include "UIGenLayoutBox_h_ast.c"