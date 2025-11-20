#include "EString.h"
#include "Message.h"
#include "MessageExpressions.h"
#include "Expression.h"
#include "StringCache.h"

#include "inputMouse.h"
#include "GfxPrimitive.h"
#include "GfxClipper.h"
#include "GfxSpriteText.h"
#include "GfxTexAtlas.h"

#include "smf_render.h"
#include "smf/smf_format.h"
#include "UIGen.h"
#include "UIGenPrivate.h"
#include "UIGenList.h"
#include "UIGenTutorial.h"
#include "MemoryPool.h"
#include "TextFilter.h"

#include "UICore_h_ast.h"
#include "UIGen_h_ast.h"
#include "UIGenList_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static UIGenGetListOrder s_cbGetList;
static UIGenSetListOrder s_cbSetList;

static const char *s_pchRowData;
static const char *s_pchRowNumber;
static const char *s_pchRowCount;
static const char *s_pchList;
static const char *s_pchListRow;
static int s_iRowData;
static int s_iRowNumber;
static int s_iRowCount;
static int s_iListPtr;
static int s_iListRowPtr;

MP_DEFINE(UIGenList);
MP_DEFINE(UIGenListRow);
MP_DEFINE(UIGenListColumn);
MP_DEFINE(UIGenListState);
MP_DEFINE(UIGenListRowState);
MP_DEFINE(UIGenListColumnState);
MP_DEFINE(UIGenListSelectionGroup);

StashTable stListGroups = NULL;

S32 ui_GenListGetSize(UIGenListState *pState)
{
	S32 i, n, iMaxRows = 0;

	if (eaSize(&pState->eaRows) > 0)
	{
		return eaSize(&pState->eaRows);
	}

	n = eaSize(&pState->eaCols);
	for (i = 0; i < n; i++)
	{
		UIGenListColumnState *pColState = UI_GEN_STATE(pState->eaCols[i], ListColumn);
		MAX1(iMaxRows, eaSize(&pColState->eaRows));
	}

	return iMaxRows;
}

static void ui_GenListSaveColumnOrder(UIGen *pGen, UIGenListState *pState)
{
	UIGenList *pList = UI_GEN_RESULT(pGen, List);
	if (pList->bPreserveOrder)
	{
		s_cbSetList(pGen->pchName, &pState->eaColumnOrder, pList->iVersion);
	}
}

static void ui_GenListColumns(UIGen *pGen, UIGenList *pList, UIGenListState *pState)
{
	if (GET_REF(pList->hRowTemplate) == NULL)
	{
		S32 i;
		int iColCount = eaSize(&pList->eaColumnChildren);
		static UIGen **s_eaColTemplates = NULL;
		bool bReset = false;

		int iTemplateCount = eaSize(&pState->eaColumnOrder);
		
		if (!iTemplateCount && pList->bPreserveOrder && s_cbGetList)
		{
			s_cbGetList(pGen->pchName, &pState->eaColumnOrder, pList->iVersion);
			iTemplateCount = eaSize(&pState->eaColumnOrder);
		}

		if (eaSize(&pState->eaCols))
		{
			if (iTemplateCount != iColCount)
			{
				bReset = true;
			}
			else
			{
				for(i = 0; i < iColCount; ++i)
				{
					UIGen *pColTemplate = GET_REF(pList->eaColumnChildren[i]->hChild);
					if (!pColTemplate || !devassertmsg(UI_GEN_NON_NULL(pColTemplate) && pColTemplate->eType == kUIGenTypeListColumn, "Gen column type is incorrect or missing"))
						continue;
					if (pState->eaColumnOrder[i]->pchColName != pColTemplate->pchName)
					{
						bReset = true;
						break;
					}
				}
			}
		}

		if (bReset)
		{
			eaDestroyUIGens(&pState->eaCols);

			if (!pList->bMovableColumns)
			{
				eaDestroyStruct(&pState->eaColumnOrder, parse_UIColumn);
			}
		}

		eaIndexedEnable(&s_eaColTemplates, parse_UIGen);

		for(i = 0; i < eaSize(&pList->eaColumnChildren); ++i)
		{
			UIGen *pColTemplate = GET_REF(pList->eaColumnChildren[i]->hChild);
			
			if (!pColTemplate || !devassertmsg(UI_GEN_NON_NULL(pColTemplate) && pColTemplate->eType == kUIGenTypeListColumn, "Gen column type is incorrect or missing"))
					continue;

			eaPush(&s_eaColTemplates, pColTemplate);
		}

		for(i = 0; i < eaSize(&pState->eaColumnOrder); i++)
		{
			S32 iCol = eaIndexedFindUsingString(&s_eaColTemplates, pState->eaColumnOrder[i]->pchColName);
			if (iCol >=0)
			{
				if (eaSize(&pState->eaCols) <= i)
				{
					UIGen *pCol = ui_GenClone(s_eaColTemplates[iCol]);
					UIGenListColumnState *pColState = UI_GEN_STATE(pCol, ListColumn);
					pCol->pParent = pGen;
					pColState->pList = pGen;
					pColState->iColumn = i;
					eaPush(&pState->eaCols, pCol);
				}
				eaRemove(&s_eaColTemplates, iCol);
			}
		}

		for(i = 0; i < eaSize(&pList->eaColumnChildren); ++i)
		{
			UIGen *pColTemplate = GET_REF(pList->eaColumnChildren[i]->hChild);
			if (pColTemplate && eaIndexedGetUsingString(&s_eaColTemplates, pColTemplate->pchName) != NULL)
			{
				UIGen *pCol = ui_GenClone(pColTemplate);
				UIGenListColumnState *pColState = UI_GEN_STATE(pCol, ListColumn);
				UIColumn *pColOrder = StructCreate(parse_UIColumn);
				pCol->pParent = pGen;
				pColState->pList = pGen;
				pColState->iColumn = eaSize(&pState->eaCols);
				eaPush(&pState->eaCols, pCol);
			
				pColOrder->pchColName = pCol->pchName;
				pColOrder->fPercentWidth = 0;
				eaPush(&pState->eaColumnOrder, pColOrder);
			}
		}

		eaClearFast(&s_eaColTemplates);
	}	
}

static void ui_GenSortModel(UIGen *pGen, UIGenList *pList, UIGenListState *pState)
{
	if(pList->iDefaultSortCol >= 0 && pState->iSortCol < 0)
	{
		pState->iSortCol = pList->iDefaultSortCol;
		pState->eSortMode = pList->eDefaultSortMode;
	}
	if (pState->iSortCol >= 0 && pState->iSortCol < eaSize(&pState->eaCols))
	{
		UIGen *pColGen = eaGet(&pState->eaCols, pState->iSortCol);
		UIGenListColumn *pCol = pColGen ? UI_GEN_RESULT(pColGen, ListColumn) : NULL;
		UIGenListColumnState *pColState = pCol && pColGen ? UI_GEN_STATE(pColGen, ListColumn) : NULL;
		ParseTable *pTable = NULL;
		// This runs before the list gets filtered, so it should operate on the list directly.
		void ***peaModel = ui_GenGetList(pGen, NULL, &pTable);

		if (pCol && pColState && pTable)
		{
			devassert(pCol->bSortable);

			if (pCol->pchTPIField && *pCol->pchTPIField &&
				pTable && pColState->iTPICol == -1)
				ParserFindColumn(pTable, pCol->pchTPIField, &pColState->iTPICol);

			if (pColState->iTPICol != -1)
			{
				pGen->pCode->iSortColumn = pColState->iTPICol;
				pGen->pCode->eSort = pState->eSortMode;
			}
		}
	}

	ui_GenListSort(pGen);
}

static void ui_GenSyncRowsWithModel(UIGen *pGen,
								 UIGen *pListGen,
								 UIGenList *pList,
								 UIGenListState *pState,
								 UIGen ***peaRows,
								 UIGen ***peaOwnedRows,
								 Expression *pRowTemplateExpr,
								 UIGen* pRowTemplate,
								 S16 iColumn,
								 UIGenChild ***peaTemplateChild,
								 UIGenChild ***peaTemplateGens,
								 bool bAppendTemplateChildren)
{
	static UIGen **s_eaTemplateChildren = NULL;
	ParseTable *pTable = NULL;
	void ***peaModel = ui_GenGetList(pListGen, NULL, &pTable);
	S32 iModelSize, iTemplateChildrenSize = peaTemplateChild ? eaSize(peaTemplateChild) : 0;
	S32 i, iSyncRow, iCount = 0;

	if (pState->eaFilteredList)
		peaModel = &pState->eaFilteredList;

	iModelSize = peaModel ? eaSize(peaModel) : 0;

	if (pList->pchSelectionGroup)
	{
		UIGenListSelectionGroup* pListGroup = NULL;
		if (stashFindPointer(stListGroups, pList->pchSelectionGroup, &pListGroup) && pListGroup)
		{
			if (pGen == pListGroup->pvGen)
				pState->iSelectedRow = pListGroup->iSelectedRow;
			else
				pState->iSelectedRow = -1;
		}
	}

	iSyncRow = pState->iSelectedRow;
	if (pList->bSlowSync)
		pState->iSelectedRow = -1;

	if (!pRowTemplateExpr && !pRowTemplate || pRowTemplate && !devassertmsg(UI_GEN_NON_NULL(pRowTemplate) && pRowTemplate->eType == kUIGenTypeListRow, "Gen row type is incorrect or missing"))
	{
		iModelSize = 0;
	}

	if (pList->bSlowSync && pRowTemplate) // Slow sync mode; try to insert rows in the correct place
	{
		for (i = 0; i < iModelSize; i++)
		{
			int iRow, numRows = eaSize(peaOwnedRows);
			for (iRow = i; iRow < numRows; ++iRow)
			{
				UIGenListRowState *pRowState = UI_GEN_STATE((*peaOwnedRows)[iRow], ListRow);
				if (pRowState->pModelData == (*peaModel)[i])
				{
					eaInsert(peaOwnedRows, eaRemove(peaOwnedRows, iRow), i);
					if (iSyncRow == pRowState->iRow && !iTemplateChildrenSize)
						pState->iSelectedRow = i;
					break;
				}
			}
			if (iRow == numRows) // Matching row was not found
			{
				UIGen *pRow = ui_GenClone(pRowTemplate);
				eaInsert(peaOwnedRows, pRow, i);
			}
		}
		eaSetSizeStruct(peaOwnedRows, parse_UIGen, iModelSize);
	}
	else // Faster sync mode; simply remove or destroy rows at the end of the list
	{
		if (pRowTemplateExpr)
		{
			int iIndex = 0;
			ExprContext *pContext = ui_GenGetContext(pGen);
			pRowTemplate = NULL;
			exprContextSetPointerVarPooledCached(pContext, s_pchList, pGen, parse_UIGen, true, true, &s_iListPtr);
			exprContextSetPointerVarPooledCached(pContext, s_pchListRow, NULL, parse_UIGen, true, true, &s_iListRowPtr);	
			for (i = 0; i < iModelSize; i++)
			{
				UIGen *pRow = eaGet(peaOwnedRows, iIndex);
				UIGenListRowState *pRowState = pRow ? UI_GEN_STATE(pRow, ListRow) : NULL;
				void *pData = (*peaModel)[i];
				MultiVal mv = {0};
				char **estr = NULL;

				exprContextSetPointerVarPooledCached(pContext, s_pchRowData, pData, pTable, true, true, &s_iRowData);
				exprContextSetPointerVarPooledCached(pContext, g_ui_pchGenData, pData, pTable, true, true, &g_ui_iGenData);
				exprContextSetIntVarPooledCached(pContext, s_pchRowNumber, i, &s_iRowNumber);

				ui_GenTimeEvaluateWithContext(pGen, pRowTemplateExpr, &mv, pContext, "RowTemplateExpr");
				if (MultiValIsString(&mv) && mv.str && *mv.str)
				{
					pRowTemplate = RefSystem_ReferentFromString(g_GenState.hGenDict, mv.str);
					if (!pRowTemplate && eaPushUnique(&pState->eaBadTemplateNames, allocAddString(mv.str)) < 0)
					{
						ErrorFilenamef(pGen->pchFilename, "%s: Invalid gen name specified (%s)", pGen->pchName, mv.str);
					}
				}
				else
				{
					pRowTemplate = NULL;
				}

				if (pRowTemplate)
				{
					if (!pRowState || pRowState->pRowTemplate != pRowTemplate)
					{
						UIGen *pNewRow = ui_GenClone(pRowTemplate);
						UIGenListRowState *pNewRowState = UI_GEN_STATE(pNewRow, ListRow);
						eaSet(peaOwnedRows, pNewRow, iIndex);
						pNewRowState->pRowTemplate = pRowTemplate;
						pNewRowState->iDataRow = i;
						StructDestroy(parse_UIGen, pRow);
					}
					else
					{
						pRowState->iDataRow = i;
					}
					iIndex++;
				}
				else if (pRowState && pRowState->iDataRow == i)
				{
					StructDestroy(parse_UIGen, eaRemove(peaOwnedRows, iIndex));
				}
			}
			iModelSize = iIndex;
		}
		else
		{
			if (pRowTemplate)
			{
				while (iModelSize > eaSize(peaOwnedRows))
					eaPush(peaOwnedRows, ui_GenClone(pRowTemplate));
			}
		}
		while (iModelSize < eaSize(peaOwnedRows))
			StructDestroy(parse_UIGen, eaPop(peaOwnedRows));
		
	}

	eaSetCapacity(peaRows, iModelSize + iTemplateChildrenSize);

	// Prepend the children
	if (!bAppendTemplateChildren)
	{
		for (i = 0; i < iTemplateChildrenSize; i++)
		{
			UIGen *pRowGen = GET_REF((*peaTemplateChild)[i]->hChild);
			if (pRowGen && eaFind(&s_eaTemplateChildren, pRowGen) < 0)
			{
				pRowGen->pParent = pGen;

				if (UI_GEN_IS_TYPE(pRowGen, kUIGenTypeListRow))
				{
					UIGenListRowState *pRowState = UI_GEN_STATE(pRowGen, ListRow);
					pRowState->pList = pListGen;
					if (pList->bSlowSync && iSyncRow == pRowState->iRow)
						pState->iSelectedRow = iCount;
					pRowState->iRow = iCount;
					pRowState->iCol = iColumn;
					pRowState->iDataRow = -1;
					pRowState->pModelData = NULL;
				}

				if (iCount > 0)
					ui_GenState((*peaRows)[iCount - 1], kUIGenStateLast, false);
				ui_GenStates(pRowGen,
					kUIGenStateSelected, iCount == pState->iSelectedRow && !pState->bSelectedRowNotSelected,
					kUIGenStateFirst, iCount == 0,
					kUIGenStateEven, (iCount & 1) == 0,
					0);

				eaSet(peaRows, pRowGen, iCount++);
				eaPush(&s_eaTemplateChildren, pRowGen);
			}
		}
	}

	for (i = 0; i < iModelSize; i++)
	{
		UIGen *pRowGen = (*peaOwnedRows)[i];
		UIGenListRowState *pRowState = UI_GEN_STATE(pRowGen, ListRow);

		pRowGen->pParent = pListGen;
		pRowState->pList = pListGen;
		if (pList->bSlowSync && iSyncRow == pRowState->iRow)
			pState->iSelectedRow = iCount;
		pRowState->iRow = iCount;
		pRowState->iCol = iColumn;
		if (!pRowTemplateExpr)
			pRowState->iDataRow = i;
		pRowState->pModelData = (*peaModel)[pRowState->iDataRow];

		if (iCount > 0)
			ui_GenState((*peaRows)[iCount - 1], kUIGenStateLast, false);
		ui_GenStates(pRowGen,
			kUIGenStateSelected, iCount == pState->iSelectedRow && !pState->bSelectedRowNotSelected,
			kUIGenStateFirst, iCount == 0,
			kUIGenStateEven, (iCount & 1) == 0,
			0);

		eaSet(peaRows, pRowGen, iCount++);
	}

	// Append the children
	if (bAppendTemplateChildren)
	{
		for (i = 0; i < iTemplateChildrenSize; i++)
		{
			UIGen *pRowGen = GET_REF((*peaTemplateChild)[i]->hChild);
			if (pRowGen && eaFind(&s_eaTemplateChildren, pRowGen) < 0)
			{
				pRowGen->pParent = pGen;

				if (UI_GEN_IS_TYPE(pRowGen, kUIGenTypeListRow))
				{
					UIGenListRowState *pRowState = UI_GEN_STATE(pRowGen, ListRow);
					pRowState->pList = pListGen;
					if (pList->bSlowSync && iSyncRow == pRowState->iRow)
						pState->iSelectedRow = iCount;
					pRowState->iRow = iCount;
					pRowState->iCol = iColumn;
					pRowState->iDataRow = -1;
					pRowState->pModelData = NULL;
				}

				if (iCount > 0)
					ui_GenState((*peaRows)[iCount - 1], kUIGenStateLast, false);
				ui_GenStates(pRowGen,
					kUIGenStateSelected, iCount == pState->iSelectedRow && !pState->bSelectedRowNotSelected,
					kUIGenStateFirst, iCount == 0,
					kUIGenStateEven, (iCount & 1) == 0,
					0);

				eaSet(peaRows, pRowGen, iCount++);
				eaPush(&s_eaTemplateChildren, pRowGen);
			}
		}
	}

	// Reset old template gens
	if (eaSize(peaTemplateGens) || eaSize(&s_eaTemplateChildren))
	{
		for (i = 0; i < eaSize(&s_eaTemplateChildren); i++)
		{
			UIGen *pTemplateChild = s_eaTemplateChildren[i];
			S32 iExisting = i;
			for (; iExisting < eaSize(peaTemplateGens); iExisting++)
			{
				if (GET_REF((*peaTemplateGens)[iExisting]->hChild) == pTemplateChild)
				{
					if (iExisting != i)
						eaSwap(peaTemplateGens, i, iExisting);
					break;
				}
			}
			if (iExisting >= eaSize(peaTemplateGens))
			{
				UIGenChild *pChildHolder = StructCreate(parse_UIGenChild);
				SET_HANDLE_FROM_REFERENT(g_GenState.hGenDict, pTemplateChild, pChildHolder->hChild);
				eaInsert(peaTemplateGens, pChildHolder, i);
			}
		}
		while (eaSize(peaTemplateGens) > i)
		{
			UIGenChild *pOldGen = eaPop(peaTemplateGens);
			UIGen *pOldChildGen = GET_REF(pOldGen->hChild);
			if (pOldChildGen)
				ui_GenReset(pOldChildGen);
		}
		eaClearFast(&s_eaTemplateChildren);
	}

	if (iCount > 0)
		ui_GenState((*peaRows)[iCount - 1], kUIGenStateLast, true);

	eaSetSize(peaRows, iCount);
}

bool ui_GenListSyncWithModel(UIGen *pGen, UIGenList *pList, UIGenListState *pState)
{
	S32 iModelSize, i;
	ParseTable *pTable = NULL;
	void ***peaModel = NULL;

	if (pList->pGetModel)
	{
		ui_GenTimeEvaluate(pGen, pList->pGetModel, NULL, "Model");
		if (pGen->pCode && pGen->pCode->uiFrameLastUpdate != g_ui_State.uiFrameCount)
		{
			ui_GenSetList(pGen, NULL, NULL);
			ErrorFilenamef(pGen->pchFilename, "%s: Model expression ran but did not update the list this frame.\n\n%s", 
				pGen->pchName, exprGetCompleteString(pList->pGetModel));
		}
	}
	else
	{
		ui_GenSetList(pGen, NULL, NULL);
	}

	peaModel = ui_GenGetList(pGen, NULL, &pTable);
	iModelSize = peaModel ? eaSize(peaModel) : 0;

	// Sort the rows so that when paginating, it flows nicely.
	ui_GenSortModel(pGen, pList, pState);

	// Build the filtered list.
	if (pList->pFilter)
	{
		void ***peaFilteredRows = &pState->eaFilteredList;
		ExprContext *pContext = ui_GenGetContext(pGen);
		Expression *pFilter = pList->pFilter;
		eaClearFast(peaFilteredRows);

		// Make sure the array exists, since that is how the other code knows it should use the filtered list.
		if (!*peaFilteredRows)
		{
			eaCreate(peaFilteredRows);
		}

		exprContextSetPointerVarPooledCached(pContext, s_pchList, pGen, parse_UIGen, true, true, &s_iListPtr);
		exprContextSetPointerVarPooledCached(pContext, s_pchListRow, NULL, parse_UIGen, true, true, &s_iListRowPtr);

		for (i = 0; i < iModelSize; ++i)
		{
			void *pData = (*peaModel)[i];
			MultiVal mv = {0};
			exprContextSetPointerVarPooledCached(pContext, s_pchRowData, pData, pTable, true, true, &s_iRowData);
			exprContextSetPointerVarPooledCached(pContext, g_ui_pchGenData, pData, pTable, true, true, &g_ui_iGenData);
			exprContextSetIntVarPooledCached(pContext, s_pchRowNumber, i, &s_iRowNumber);

			ui_GenTimeEvaluateWithContext(pGen, pFilter, &mv, pContext, "Filter");
			if (MultiValToBool(&mv))
			{
				eaPush(peaFilteredRows, pData);
			}
		}

		peaModel = &pState->eaFilteredList;
		iModelSize = eaSize(peaModel);
	}
	else if (pState->eaFilteredList)
	{
		eaDestroy(&pState->eaFilteredList);
	}

	// Update the Filled/Empty states against the filtered list.
	ui_GenStates(pGen, 
		kUIGenStateEmpty, iModelSize == 0,
		kUIGenStateFilled, iModelSize > 0,
		0);


	//The list is a bunch of rows
	if (GET_REF(pList->hRowTemplate) || pList->pRowTemplateExpr || eaSize(&pList->eaTemplateChild))
	{
		ui_GenSyncRowsWithModel(pGen, pGen, pList, pState, &pState->eaRows, &pState->eaOwnedRows, pList->pRowTemplateExpr, GET_REF(pList->hRowTemplate), 0, &pList->eaTemplateChild, &pState->eaTemplateGens, pList->bAppendTemplateChildren);
	}
	else if (eaSize(&pList->eaColumnChildren) == 0)
	{
		eaDestroy(&pState->eaRows);
		eaDestroyUIGens(&pState->eaOwnedRows);
	}

	MIN1(pState->iSelectedRow, ui_GenListGetSize(pState) - 1);
	
	return !!peaModel;
}

static bool ui_GenListIsSelectable(UIGen *pGen, UIGenList *pList, UIGenListState *pState, S32 iRow)
{
	// This used to be more complicated
	S32 iSize = ui_GenListGetSize(pState);
	if (iRow >= iSize || iRow < 0)
		return false;
	else if (!pList->pAllowSelect)
		return true;
	else
	{
		UIGen *pRow = NULL;
		if (eaSize(&pState->eaRows) > 0)
		{
			pRow = eaGet(&pState->eaRows, iRow);
		}
		else
		{
			UIGen *pCol = eaGet(&pState->eaCols, 0);
			if (pCol)
			{
				UIGenListColumnState *pColState = UI_GEN_STATE(pCol, ListColumn);
				pRow = eaGet(&pColState->eaRows, iRow);
			}
		}
		return ui_GenEvaluate(pRow, pList->pAllowSelect, NULL);
	}
}

S32 ui_GenListNextSelectableRow(UIGen *pGen, UIGenList *pList, UIGenListState *pState, S32 iRow)
{
	S32 iSize = ui_GenListGetSize(pState);

	if (pState->bSelectedRowNotSelected && pState->iSelectedRow == 0)
		return 0;

	while (++iRow < iSize)
		if (ui_GenListIsSelectable(pGen, pList, pState, iRow))
			return iRow;

	return -1;
}

S32 ui_GenListPreviousSelectableRow(UIGen *pGen, UIGenList *pList, UIGenListState *pState, S32 iRow)
{
	while (--iRow >= 0)
		if (ui_GenListIsSelectable(pGen, pList, pState, iRow))
			return iRow;
	return -1;
}

S32 ui_GenListNearSelectableRow(UIGen *pGen, UIGenList *pList, UIGenListState *pState, S32 iRow)
{
	S32 iOrigRow = iRow;
	if (ui_GenListIsSelectable(pGen, pList, pState, iOrigRow))
		return iOrigRow;
	iRow = ui_GenListPreviousSelectableRow(pGen, pList, pState, iRow);
	if (iRow < 0)
		iRow = ui_GenListNextSelectableRow(pGen, pList, pState, iRow);
	return iRow;
}

static F32 ui_GenLayoutColumnRows(UIGen *pGen, UIGenListColumnState *pColState, UIGen *pListGen, UIGenList *pList, UIGenListState *pListState)
{
	CBox OrigBox = pGen->ScreenBox;
	CBox OrigUnpaddedBox = pGen->UnpaddedScreenBox;
	F32 fTopMargin = pList->iListTopMargin * pListGen->fScale;
	F32 fFakeTop = -pListState->scrollbar.fScrollPosition + OrigUnpaddedBox.hy;
	F32 fDrawY = fFakeTop + fTopMargin;
	F32 fTotalHeight = 0;
	
	S32 i;

	//Make the screen box of the column be height of the list gen's minus the height of the column header
	pGen->ScreenBox.hy = pListGen->ScreenBox.hy;
	pGen->UnpaddedScreenBox.hy = pListGen->ScreenBox.hy;
	pGen->ScreenBox.ly = OrigUnpaddedBox.hy;
	pGen->UnpaddedScreenBox.ly = OrigUnpaddedBox.hy;

	for (i = 0; i < eaSize(&pColState->eaRows); i++)
	{
		if (UI_GEN_NON_NULL(pColState->eaRows[i]))
		{
			F32 fRowHeight = 0.f;
			CBoxMoveY(&pGen->ScreenBox, fDrawY);
			CBoxMoveY(&pGen->UnpaddedScreenBox, fDrawY);
			ui_GenLayoutCB(pColState->eaRows[i], pGen);
			fRowHeight = CBoxHeight(&pColState->eaRows[i]->UnpaddedScreenBox);
			if (pListState->bForceShowSelectedRow && i == pListState->iSelectedRow)
			{
				F32 fTopY = fDrawY - fFakeTop;
				F32 fBottomY = fTopY + fRowHeight;
				if (fTopY < pListState->scrollbar.fScrollPosition)
					pListState->scrollbar.fScrollPosition = fTopY;
				else if (fBottomY > pListState->scrollbar.fScrollPosition + CBoxHeight(&pGen->ScreenBox))
					pListState->scrollbar.fScrollPosition = fBottomY - CBoxHeight(&pGen->ScreenBox);
				pListState->bForceShowSelectedRow = false;
			}
			fDrawY = pColState->eaRows[i]->UnpaddedScreenBox.hy + pList->iRowSpacing * pGen->fScale;
		}
	}

	pGen->ScreenBox = OrigBox;
	pGen->UnpaddedScreenBox = OrigUnpaddedBox;

	if (eaSize(&pColState->eaRows))
	{
		UIGen *pFirst = pColState->eaRows[0];
		UIGen *pLast = pColState->eaRows[eaSize(&pColState->eaRows) - 1];
		fTotalHeight = pLast->UnpaddedScreenBox.hy - pFirst->UnpaddedScreenBox.ly;
	}
	return (fTotalHeight);
}

//Lays out the columns and the rows inside
static F32 ui_GenListLayoutCols(UIGen *pGen, UIGenList *pList, UIGenListState *pListState)
{
	CBox origScreenBox = pGen->ScreenBox;
	CBox origUnpaddedScreenBox = pGen->UnpaddedScreenBox;
	F32 fDrawX = pGen->ScreenBox.lx;
	F32 fTotalHeight = 0;
	F32 fTotalColumnHeight = 0;
	S32 iNCols = eaSize(&pListState->eaCols);
	S32 i;
	S32 iFirstStretchyColumn = iNCols;
	F32 fFirstStretchyColumnX = 0;
	F32 fNonStretchyWidth = 0;
	F32 fStretchyWidthRatio = 0;
	F32 fTotalStretchyRatio = 0;

	ui_GenScrollbarBox(&pList->scrollbar, &pListState->scrollbar, &pGen->ScreenBox, &pGen->ScreenBox, pGen->fScale);
	ui_GenScrollbarBox(&pList->scrollbar, &pListState->scrollbar, &pGen->UnpaddedScreenBox, &pGen->UnpaddedScreenBox, pGen->fScale);

	// Layout each column, to its desired width (that may change later)
	// Makes the list the right total size
	for(i = 0; i < iNCols; i++)
	{
		if (UI_GEN_NON_NULL(pListState->eaCols[i]))
		{
			F32 fColWidth = 0.f;
			UIGenListColumnState *pColState = UI_GEN_STATE(pListState->eaCols[i], ListColumn);
			UIColumn *pColOrder = pListState->eaColumnOrder[i];
			UIGenListColumn *pCol = UI_GEN_RESULT(pListState->eaCols[i], ListColumn);

			fColWidth = CBoxWidth(&pGen->UnpaddedScreenBox) * pColOrder->fPercentWidth;

			// If the last column is resizable, and nothing else is stretchy so far, stretch the last column the remaining width
			if (i == iNCols-1 && pCol->bResizable && fTotalStretchyRatio == 0)
			{
				pListState->eaCols[i]->UnpaddedScreenBox.lx = fDrawX;
				pListState->eaCols[i]->UnpaddedScreenBox.hx = pGen->UnpaddedScreenBox.hx;

				pCol->polyp.pos.Width.eUnit = UIUnitPercentage;
				pCol->polyp.pos.Width.fMagnitude = CBoxWidth(&pListState->eaCols[i]->UnpaddedScreenBox) / CBoxWidth(&pGen->UnpaddedScreenBox);
			}
			else if (pColOrder->fPercentWidth && pCol->bResizable)
			{
				pCol->polyp.pos.Width.eUnit = UIUnitPercentage;
				pCol->polyp.pos.Width.fMagnitude = pColOrder->fPercentWidth;
			}

			ui_GenLayoutCB(pListState->eaCols[i], pGen);
			fColWidth = CBoxWidth(&pListState->eaCols[i]->UnpaddedScreenBox);

			if( pCol->iStretchyRatio )
			{
				fTotalStretchyRatio += pCol->iStretchyRatio;
				if(iFirstStretchyColumn == iNCols )
				{
					iFirstStretchyColumn = i;
					fFirstStretchyColumnX = fDrawX;
				}
			}
			else
			{
				fNonStretchyWidth += fColWidth;
			}

			fDrawX += fColWidth;
			CBoxMoveX(&pGen->ScreenBox, fDrawX);
		}
	}

	// Recalculate sizes of columns
	// The last column has already done its layout, so if it's the only stretchy column, don't layout it again.
	if( iFirstStretchyColumn < iNCols-1 )
	{
		fDrawX = fFirstStretchyColumnX;
		CBoxMoveX(&pGen->ScreenBox, fDrawX);
		fStretchyWidthRatio = (CBoxWidth(&pGen->ScreenBox) - fNonStretchyWidth) / fTotalStretchyRatio;
		for(i = iFirstStretchyColumn; i < iNCols; i++)
		{
			if (UI_GEN_NON_NULL(pListState->eaCols[i]))
			{
				F32 fColWidth = 0.f;
				UIGenListColumnState *pColState = UI_GEN_STATE(pListState->eaCols[i], ListColumn);
				UIGenListColumn *pCol = UI_GEN_RESULT(pListState->eaCols[i], ListColumn);

				if( pCol->iStretchyRatio )
				{
					pCol->polyp.pos.Width.eUnit = UIUnitFixed;
					pCol->polyp.pos.Width.fMagnitude = fStretchyWidthRatio * (F32)pCol->iStretchyRatio;
				}
				ui_GenLayoutCB(pListState->eaCols[i], pGen);
				fColWidth = CBoxWidth(&pListState->eaCols[i]->UnpaddedScreenBox);

				fDrawX += fColWidth;
				CBoxMoveX(&pGen->ScreenBox, fDrawX);
			}
		}
	}

	// Layout Rows in each column
	for(i = 0; i < iNCols; i++)
	{
		if (UI_GEN_NON_NULL(pListState->eaCols[i]))
		{
			UIGenListColumnState *pColState = UI_GEN_STATE(pListState->eaCols[i], ListColumn);
			fTotalColumnHeight = ui_GenLayoutColumnRows(pListState->eaCols[i], pColState, pGen, pList, pListState);
			fTotalColumnHeight += CBoxHeight(&pListState->eaCols[i]->UnpaddedScreenBox);
			fTotalHeight = MAX(fTotalHeight, fTotalColumnHeight);
		}
	}
	
	pGen->ScreenBox = origScreenBox;
	pGen->UnpaddedScreenBox = origUnpaddedScreenBox;
	return((pList->iListBottomMargin + pList->iListTopMargin) * pGen->fScale + fTotalHeight);
}

static UIGen* ui_GenListGetParent(UIGen *pGen)
{
	if ( UI_GEN_IS_TYPE(pGen->pParent,kUIGenTypeListRow) )
	{
		if ( UI_GEN_IS_TYPE(pGen->pParent->pParent,kUIGenTypeList) )
		{
			return ui_GenListGetParent(pGen->pParent->pParent);
		}
	}
	return pGen;
}

// Lay out rows and return the total height of them.
static F32 ui_GenListLayoutRows(UIGen *pGen, UIGenList *pList, UIGenListState *pListState)
{
	CBox origBox = pGen->ScreenBox;
	F32 fTopMargin = pList->iListTopMargin * pGen->fScale;
	F32 fFakeTop = -pListState->scrollbar.fScrollPosition + pGen->ScreenBox.ly;
	F32 fDrawY = fFakeTop + fTopMargin;
	F32 fTotalHeight = 0;
	S32 i;

	ui_GenScrollbarBox(&pList->scrollbar, &pListState->scrollbar, &pGen->ScreenBox, &pGen->ScreenBox, pGen->fScale);

	for (i = 0; i < eaSize(&pListState->eaRows); i++)
	{
		if (UI_GEN_NON_NULL(pListState->eaRows[i]))
		{
			F32 fRowHeight = 0.f;
			CBoxMoveY(&pGen->ScreenBox, fDrawY);
			ui_GenLayoutCB(pListState->eaRows[i], pGen);
			fRowHeight = CBoxHeight(&pListState->eaRows[i]->UnpaddedScreenBox);
			if (pListState->bForceShowSelectedRow && i == pListState->iSelectedRow)
			{
				UIGen *pGenListParent;
				F32 fTopY = fDrawY - fFakeTop;
				F32 fBottomY = fTopY + fRowHeight;
				if (fTopY < pListState->scrollbar.fScrollPosition)
					pListState->scrollbar.fScrollPosition = fTopY;
				else if (fBottomY > pListState->scrollbar.fScrollPosition + CBoxHeight(&pGen->ScreenBox))
					pListState->scrollbar.fScrollPosition = fBottomY - CBoxHeight(&pGen->ScreenBox);

				if (pList->bUpdateParentScrollBar && (pGenListParent=ui_GenListGetParent(pGen)) != pGen)
				{
					UIGenList *pListParent = UI_GEN_RESULT(pGenListParent, List);
					UIGenListState *pListParentState = UI_GEN_STATE(pGenListParent, List);
					F32 fTopDiff, fBotDiff;
					if ((fTopDiff=pListState->eaRows[i]->ScreenBox.ly-pGenListParent->UnpaddedScreenBox.ly)<0)
						pListParentState->scrollbar.fScrollPosition += fTopDiff;
					else if ((fBotDiff=pListState->eaRows[i]->ScreenBox.hy-pGenListParent->UnpaddedScreenBox.hy)>0)
						pListParentState->scrollbar.fScrollPosition += fBotDiff;
				}
				pListState->bForceShowSelectedRow = false;
			}
			fDrawY = pListState->eaRows[i]->UnpaddedScreenBox.hy + pList->iRowSpacing * pGen->fScale;
		}
	}

	pGen->ScreenBox = origBox;
	if (eaSize(&pListState->eaRows))
	{
		UIGen *pFirst = pListState->eaRows[0];
		UIGen *pLast = pListState->eaRows[eaSize(&pListState->eaRows) - 1];
		fTotalHeight = pLast->UnpaddedScreenBox.hy - pFirst->UnpaddedScreenBox.ly;
	}
	return ((pList->iListBottomMargin + pList->iListTopMargin) * pGen->fScale + fTotalHeight);
}

static bool ui_GenTickListRow(UIGen *pRow, UIGen *pList)
{
	if (UI_GEN_READY(pRow) && pRow->UnpaddedScreenBox.hy >= pList->UnpaddedScreenBox.ly && pRow->UnpaddedScreenBox.ly < pList->UnpaddedScreenBox.hy)
	{
		return ui_GenTickCB(pRow, pRow->pParent);
	}
	else
		return false;
}

static void ui_GenPointerUpdateList(UIGen *pGen)
{
	UIGenList *pList = UI_GEN_RESULT(pGen, List);
	if (pList)
	{
		UIGenListState *pState = UI_GEN_STATE(pGen, List);

		// First chance to set initial selection
		if (pState->iDesiredRowSelection >= 0)
		{
			ui_GenListSetSelectedRow(pGen, pState->iDesiredRowSelection);
			pState->iDesiredRowSelection = -1;
		}

		ui_GenListSyncWithModel(pGen, pList, pState);
		ui_GenForEach(&pState->eaCols, ui_GenPointerUpdateCB, pGen, UI_GEN_UPDATE_ORDER);
		ui_GenForEach(&pState->eaRows, ui_GenPointerUpdateCB, pGen, UI_GEN_UPDATE_ORDER);
	}
}

static void ui_GenUpdateList(UIGen *pGen)
{
	UIGenList *pList = UI_GEN_RESULT(pGen, List);
	UIGenListState *pState = UI_GEN_STATE(pGen, List);
	S32 iRow;
	ui_GenListColumns(pGen, pList, pState);
	ui_GenForEach(&pState->eaCols, ui_GenUpdateCB, pGen, UI_GEN_UPDATE_ORDER);
	ui_GenForEach(&pState->eaRows, ui_GenUpdateCB, pGen, UI_GEN_UPDATE_ORDER);
	iRow = ui_GenListNearSelectableRow(pGen, pList, pState, pState->iSelectedRow);
	if (iRow >= 0)
		pState->iSelectedRow = iRow;
}

static void ui_GenLayoutLateList(UIGen *pGen)
{
	UIGenList *pList = UI_GEN_RESULT(pGen, List);
	UIGenListState *pState = UI_GEN_STATE(pGen, List);
	F32 fTotalHeight = 0;
	if (eaSize(&pState->eaRows))
	{
		fTotalHeight = ui_GenListLayoutRows(pGen, pList, pState);
	}
	else if (eaSize(&pState->eaCols))
	{
		fTotalHeight = ui_GenListLayoutCols(pGen, pList, pState);
	}

	ui_GenLayoutScrollbar(pGen, &pList->scrollbar, &pState->scrollbar, fTotalHeight);

	pState->SortAscIndicatorBundleTexture.pchTexture = pList->pchSortAscIndicatorTexture;
	//pState->SortAscIndicatorBundleTexture.eAlignment = pList->eSortAscIndicatorAlignment;
	pState->SortAscIndicatorBundleTexture.uiTopLeftColor = 0xFFFFFFFF;
	pState->SortDesIndicatorBundleTexture.pchTexture = pList->pchSortDesIndicatorTexture;
	//pState->SortDesIndicatorBundleTexture.eAlignment = pList->eSortDesIndicatorAlignment;
	pState->SortDesIndicatorBundleTexture.uiTopLeftColor = 0xFFFFFFFF;

	ui_GenBundleTextureUpdate(pGen, &pState->SortAscIndicatorBundleTexture, &pState->SortAscIndicatorBundle);
	ui_GenBundleTextureUpdate(pGen, &pState->SortDesIndicatorBundleTexture, &pState->SortDesIndicatorBundle);
}

static void ui_GenTickEarlyList(UIGen *pGen)
{
	UIGenList *pList = UI_GEN_RESULT(pGen, List);
	UIGenListState *pState = UI_GEN_STATE(pGen, List);
	UIGen *pMousedGen = NULL;

	pState->iMouseInsideCol = 0;
	pState->iMouseInsideRow = -1;

	if (pList->polyp.bClipInput)
		mouseClipPushRestrict(&pGen->ScreenBox);
	if (eaSize(&pState->eaRows))
	{
		ui_GenForEachInPriority(&pState->eaRows, ui_GenTickListRow, pGen, UI_GEN_TICK_ORDER);
	}
	else
	{
		ui_GenForEachInPriority(&pState->eaCols, ui_GenTickCB, pGen, UI_GEN_TICK_ORDER);
	}
	
	ui_GenTickScrollbar(pGen, &pList->scrollbar, &pState->scrollbar);
	if (pList->polyp.bClipInput)
		mouseClipPop();
}

static bool GenDrawRow(UIGen *pRow, UIGen *pList)
{
	if (UI_GEN_READY(pRow) && pRow->UnpaddedScreenBox.hy >= pList->UnpaddedScreenBox.ly && pRow->UnpaddedScreenBox.ly < pList->UnpaddedScreenBox.hy)
		return ui_GenDrawCB(pRow, pList);
	else
		return false;
}

static bool ui_GenDrawColumnRow(UIGen *pRow, UIGen *pCol)
{
	UIGenListColumnState *pColState = UI_GEN_STATE(pCol, ListColumn);
	if (UI_GEN_READY(pRow) && pColState && 
		pRow->UnpaddedScreenBox.hy >= pCol->UnpaddedScreenBox.hy && 
		pRow->UnpaddedScreenBox.ly < pColState->pList->UnpaddedScreenBox.hy)
		return ui_GenDrawCB(pRow, pCol);
	else
		return false;
}

static void ui_GenDrawEarlyList(UIGen *pGen)
{
	UIGenList *pList = UI_GEN_RESULT(pGen, List);
	UIGenListState *pState = UI_GEN_STATE(pGen, List);

	if (pList->polyp.bClip)
		clipperPushRestrict(&pGen->ScreenBox);

	ui_GenDrawScrollbar(pGen, &pList->scrollbar, &pState->scrollbar);

	if (eaSize(&pState->eaRows))
	{
		ui_GenForEachInPriority(&pState->eaRows, GenDrawRow, pGen, UI_GEN_DRAW_ORDER);
	}
	else
	{
		ui_GenForEachInPriority(&pState->eaCols, ui_GenDrawCB, pGen, UI_GEN_DRAW_ORDER);
	}

	if (pState && pState->iDestCol != -1)
	{
		F32 fZ = UI_GET_Z();
		CBox *cbox = &pState->eaCols[pState->iDestCol]->UnpaddedScreenBox;
		int x = (int)(pState->bDestDrawHigh ? cbox->hx-1 : cbox->lx+1);
		
		gfxDrawLineWidth(	x,
							(int)cbox->ly, 
							fZ, 
							x,
							(int)cbox->hy,
							colorFromRGBA(ui_StyleColorPaletteIndex(pList->uMoveColor)),
							3.f);
	}

	if (pList->polyp.bClip)
	 	clipperPop();
}

static void ui_GenFitContentsSizeList(UIGen *pGen, UIGenList *pList, CBox *pOut)
{
	UIGenListState *pState = UI_GEN_STATE(pGen, List);
	Vec2 v2Size = {0, 0};
	if (eaSize(&pState->eaRows))
	{
		S32 i;
		if (CBoxWidth(&pState->eaRows[0]->UnpaddedScreenBox) && CBoxWidth(&eaTail(&pState->eaRows)->UnpaddedScreenBox))
		{
			UIGen *pFirstRow = pState->eaRows[0];
			UIGen *pLastRow = eaTail(&pState->eaRows);
			for (i = 0; i < eaSize(&pState->eaRows); i++)
			{
				UIGen *pRow = pState->eaRows[i];
				MAX1F(v2Size[0], (pRow->UnpaddedScreenBox.hx - pRow->UnpaddedScreenBox.lx) / pGen->fScale);
			}
			v2Size[1] = (pLastRow->UnpaddedScreenBox.hy - pFirstRow->UnpaddedScreenBox.ly) / pGen->fScale;
		}
		else
		{
			// Determine sizes based on fixed size information
			ui_GenForEach(&pState->eaRows, ui_GenAddBoundsHeight, v2Size, false);
			for (i = 0; i < eaSize(&pState->eaRows); i++)
			{
				Vec2 v2 = {0, 0};
				ui_GenGetBounds(pState->eaRows[i], v2);
				MAX1(v2Size[0], v2[0]);
			}
			v2Size[1] += (eaSize(&pState->eaRows) - 1) * pList->iRowSpacing;
		}
	}
	else if (eaSize(&pState->eaCols))
	{
		// It is the responsibility of the author to understand that fit contents when using
		// columns only sizes based on the first column
		UIGenListColumnState *pColState = UI_GEN_STATE(pState->eaCols[0], ListColumn);
		ui_GenForEach(&pState->eaCols, ui_GenAddBoundsWidth, v2Size, false);
		ui_GenAddBoundsHeight(pState->eaCols[0], v2Size);
		if (pColState && eaSize(&pColState->eaRows))
		{
			ui_GenForEach(&pColState->eaRows, ui_GenAddBoundsHeight, v2Size, false);
			v2Size[1] += ((eaSize(&pColState->eaRows) - 1) * pList->iRowSpacing);
		}
	}
	v2Size[1] = ceilf(v2Size[1]) + pList->iListTopMargin + pList->iListBottomMargin;
	v2Size[0] = ceilf(v2Size[0]) + ui_GenScrollbarWidth(&pList->scrollbar, &pState->scrollbar);
	BuildCBox(pOut, 0, 0, v2Size[0], v2Size[1]);
}

static void ui_GenHideList(UIGen *pGen)
{
	UIGenListState *pState = UI_GEN_STATE(pGen, List);
	if (pState)
	{
		UIGenList *pList = UI_GEN_RESULT(pGen, List);
		ui_GenHideScrollbar(pGen, pList ? &pList->scrollbar : NULL, &pState->scrollbar);
		eaDestroy(&pState->eaRows);
		eaDestroyUIGens(&pState->eaOwnedRows);
		eaDestroyUIGens(&pState->eaCols);
		while (eaSize(&pState->eaTemplateGens) > 0)
		{
			UIGenChild *pChild = eaPop(&pState->eaTemplateGens);
			UIGen *pChildGen = GET_REF(pChild->hChild);
			if (pChildGen)
				ui_GenReset(pChildGen);
		}
		eaDestroyStruct(&pState->eaColumnOrder, parse_UIColumn);
		eaDestroy(&pState->eaFilteredList);
	}
}

void ui_GenListActivateSelectedRow(UIGen *pGen)
{
	if (UI_GEN_IS_TYPE(pGen, kUIGenTypeList) && UI_GEN_READY(pGen))
	{
		UIGenList *pList = UI_GEN_RESULT(pGen, List);
		UIGenListState *pListState = UI_GEN_STATE(pGen, List);
		if (GET_REF(pList->hRowTemplate) || pList->pRowTemplateExpr)
		{
			UIGen *pRowGen = eaGet(&pListState->eaRows, pListState->iSelectedRow);
			if (pRowGen && UI_GEN_READY(pRowGen) && !pListState->bSelectedRowNotSelected)
			{
				UIGenListRow *pRow = UI_GEN_IS_TYPE(pRowGen, kUIGenTypeListRow) ? UI_GEN_RESULT(pRowGen, ListRow) : NULL;
				if (pRow && pRow->pOnActivated)
					ui_GenRunAction(pRowGen, pRow->pOnActivated);
				else
					ui_GenSetState(pRowGen, kUIGenStateLeftMouseDoubleClick);
			}
		}
		else
		{
			S32 i;
			for(i = 0; i < eaSize(&pListState->eaCols); i++)
			{
				UIGenListColumnState *pColState = UI_GEN_STATE(pListState->eaCols[i], ListColumn);
				UIGen *pRowGen = eaGet(&pColState->eaRows, pListState->iSelectedRow);
				if (UI_GEN_READY(pRowGen) && !pListState->bSelectedRowNotSelected)
				{
					UIGenListRow *pRow = UI_GEN_IS_TYPE(pRowGen, kUIGenTypeListRow) ? UI_GEN_RESULT(pRowGen, ListRow) : NULL;
					if (pRow && pRow->pOnActivated)
						ui_GenRunAction(pRowGen, pRow->pOnActivated);
					else
						ui_GenSetState(pRowGen, kUIGenStateLeftMouseDoubleClick);
				}
			}
		}
	}
}

static void ui_GenTickEarlyListColumn(UIGen *pGen)
{
	UIGenListColumn *pCol = UI_GEN_RESULT(pGen, ListColumn);
	UIGenListColumnState *pColState = UI_GEN_STATE(pGen, ListColumn);
	UIGenListState *pListState = pColState->pList ? UI_GEN_STATE(pColState->pList, List) : NULL;
	UIGenList *pList = pColState->pList ? UI_GEN_RESULT(pColState->pList, List) : NULL;
	S32 iPosX, iPosY;
	CBox SortBox = pGen->UnpaddedScreenBox;
	SortBox.hx = SortBox.hx - 5;
	SortBox.lx = SortBox.lx + 5;

	if (!pList)
		return;

	if (pCol->bSortable && CBoxWidth(&SortBox) > 2 && mouseClickHit(MS_LEFT, &SortBox))
	{
		if (pListState->iSortCol == pColState->iColumn)
		{
			pListState->eSortMode = (pListState->eSortMode == UISortAscending ? UISortDescending : UISortAscending);
		}
		else
		{
			pListState->iSortCol = pColState->iColumn;
			pListState->eSortMode = UISortAscending;
		}
		pListState->iMoveCol = -1;
	}
	else if (pListState->iResizeCol == -1 && pListState->iMoveCol == -1)
	{
		CBox leftBox, rightBox;
		leftBox = rightBox = pGen->UnpaddedScreenBox;
		leftBox.hx = leftBox.lx+5;
		rightBox.lx = rightBox.hx-5;


		mousePos(&iPosX, &iPosY);
		
		if (pColState->iColumn && mouseDragHit(MS_LEFT, &leftBox))
		{
			pListState->iResizeCol = pColState->iColumn-1;

			if ((eaSize(&pListState->eaCols) == pListState->iResizeCol))
			{
				//if this is the last col and not resizable then don't handle resizing
				if (!pCol->bResizable)
				{
					pListState->iResizeCol = -1;
				}
			}
			else
			{
				UIGenListColumn *pOtherCol = UI_GEN_RESULT(pListState->eaCols[pListState->iResizeCol], ListColumn);

				//if this column and the other are not resizable then don't handle resizing
				if (!pCol->bResizable && !pOtherCol->bResizable)
				{
					pListState->iResizeCol = -1;
				}
			}
		}
		else if (mouseDragHit(MS_LEFT, &rightBox))
		{
			pListState->iResizeCol = pColState->iColumn;

			if (eaSize(&pListState->eaCols) == pListState->iResizeCol+1)
			{
				//if this is the last col and not resizable then don't handle resizing
				if (!pCol->bResizable)
				{
					pListState->iResizeCol = -1;
				}
			}
			else
			{
				UIGenListColumn *pOtherCol = UI_GEN_RESULT(pListState->eaCols[pListState->iResizeCol+1], ListColumn);

				//if this column and the other are not resizable then don't handle resizing
				if (!pCol->bResizable && !pOtherCol->bResizable)
				{
					pListState->iResizeCol = -1;
				}
			}
		}
		else if (pList->bMovableColumns && mouseDragHit(MS_LEFT, &pGen->UnpaddedScreenBox))
		{
			pListState->iMoveCol = pColState->iColumn;
		}


		if (pCol->bResizable && (mouseCollision(&leftBox) || mouseCollision(&rightBox)))
		{
			ui_SetCursorForDirection(UILeft);
		}
	}

	//The 'left' column is responsible for resizing
	if (pListState->iResizeCol == pColState->iColumn)
	{
		UIGen *pOtherGen; 
		UIGenListColumn *pOtherCol;
		UIColumn *pColOrder = pListState->eaColumnOrder[pListState->iResizeCol];
		UIColumn *pOtherColOrder;
		F32 fMinX, fMaxX, fMaxRightX, fMaxLeftX;
		bool bCanMoveRight = false;
		F32 fScrollWidth = ui_GenScrollbarWidth(&pList->scrollbar, &pListState->scrollbar) * pColState->pList->fScale;
		F32 fListWidth = CBoxWidth(&pColState->pList->ScreenBox) - fScrollWidth;

		mousePos(&iPosX, &iPosY);

		if ((eaSize(&pListState->eaCols) == pListState->iResizeCol+1))
		{
			//if this is the last column, allow resizing up to the right edge of the list minus the scrollbar width
			pOtherGen = (pColState->pList);
			pOtherCol = NULL;
			pOtherColOrder = NULL;
			fMinX = pGen->UnpaddedScreenBox.lx+1;
			fMaxX = pOtherGen->ScreenBox.hx-1 - fScrollWidth;
		}
		else //otherwise move the selected columns to the right (if possible) or just resize up to the right column
		{	
			S32 iCurCol = pListState->iResizeCol + 1;

			pOtherGen = pListState->eaCols[iCurCol];
			pOtherCol = UI_GEN_RESULT(pOtherGen, ListColumn);
			pOtherColOrder = pListState->eaColumnOrder[iCurCol];
			fMinX = pGen->UnpaddedScreenBox.lx+1;
			fMaxX = pOtherGen->UnpaddedScreenBox.hx-1;

			if (iPosX < pGen->UnpaddedScreenBox.hx)
			{
				fMaxLeftX = pColState->pList->ScreenBox.lx+1;

				while (iCurCol > 0)
				{
					Vec2 v2MinimumSize = { 0, 0 };
					if (iCurCol == pListState->iResizeCol + 1)
						ui_GenGetBounds(pListState->eaCols[--iCurCol], v2MinimumSize);
					else
						v2MinimumSize[0] = CBoxWidth(&pListState->eaCols[--iCurCol]->UnpaddedScreenBox);
					fMaxLeftX += v2MinimumSize[0];
				}

				fMinX = MAX(fMinX, fMaxLeftX);
			}
			else if (iPosX > pGen->UnpaddedScreenBox.hx)
			{
				fMaxRightX = pColState->pList->ScreenBox.hx-1 - fScrollWidth;

				while (iCurCol < eaSize(&pListState->eaCols))
				{
					Vec2 v2MinSize = { 0, 0 };
					if (iCurCol == pListState->iResizeCol + 1 || iCurCol == eaSize(&pListState->eaCols) - 1)
						ui_GenGetBounds(pListState->eaCols[iCurCol++], v2MinSize);
					else
						v2MinSize[0] = CBoxWidth(&pListState->eaCols[iCurCol++]->UnpaddedScreenBox);
					fMaxRightX -= v2MinSize[0];
				}

				if (fMaxRightX < pOtherGen->UnpaddedScreenBox.lx)
				{
					bCanMoveRight = true;
				}

				fMaxX = MIN(fMaxX, fMaxRightX);
			}
		}

		devassert(pCol != pOtherCol);
		
		//TODO: a better scheme would be to separate resizing and moving functionally so that the user
		//can specifically do one or the other instead of forcing one action in a certain circumstance
		//NOTE: so far this only works when the left column is resizable
		if (pCol->bResizable)
		{
			CBox newBox;
			if (iPosX < pGen->UnpaddedScreenBox.hx || bCanMoveRight == false)
			{
				F32 clampedX = CLAMP(iPosX, fMinX, fMaxX);
				newBox = pGen->UnpaddedScreenBox;
				newBox.hx = clampedX;
				pCol->polyp.pos.Width.fMagnitude = pColOrder->fPercentWidth = CBoxWidth(&newBox) / fListWidth;
				pCol->polyp.pos.Width.eUnit = UIUnitPercentage;

				if (pOtherCol && pOtherCol->bResizable)
				{
					//if the other col can be resized, move the other col's left edge over to the mouse x
					newBox = pOtherGen->UnpaddedScreenBox;
					newBox.lx = clampedX;
					pOtherCol->polyp.pos.Width.fMagnitude = pOtherColOrder->fPercentWidth = CBoxWidth(&newBox) / fListWidth;
					pOtherCol->polyp.pos.Width.eUnit = UIUnitPercentage;
				}
			}
			else if (iPosX > pGen->ScreenBox.hx)
			{
				//if the column can be moved to the right, then move all columns 
				newBox = pGen->UnpaddedScreenBox;
				newBox.hx = MIN(iPosX,fMaxRightX);
				pCol->polyp.pos.Width.fMagnitude = pColOrder->fPercentWidth = CBoxWidth(&newBox) / fListWidth;
				pCol->polyp.pos.Width.eUnit = UIUnitPercentage;
			}
		}

		if (!mouseIsDown(MS_LEFT))
		{
			ui_GenListSaveColumnOrder(pColState->pList, pListState);
			pListState->iResizeCol = -1;
		}
	}

	//if moving columns
	else if (pListState->iMoveCol == pColState->iColumn)
	{
		S32 iCol = -1;
		mousePos(&iPosX, &iPosY);
		pListState->iDestCol = -1;
		
		if (iPosX < pGen->UnpaddedScreenBox.lx)
		{
			
			for (iCol = pListState->iMoveCol - 1; iCol >= 0; iCol--)
			{
				if (iPosX >= pListState->eaCols[iCol]->UnpaddedScreenBox.lx &&
					iPosX - pListState->eaCols[iCol]->UnpaddedScreenBox.lx <= 5)
				{
					pListState->iDestCol = iCol;
					pListState->bDestDrawHigh = false;
					break;
				}
				else if (iPosX <= pListState->eaCols[iCol]->UnpaddedScreenBox.hx && 
						 pListState->eaCols[iCol]->UnpaddedScreenBox.hx - iPosX <= 5)
				{
					if (++iCol != pListState->iMoveCol)
					{
						pListState->iDestCol = iCol;
						pListState->bDestDrawHigh = false;
					}
					break;
				}
			}
		}
		else if (iPosX > pGen->ScreenBox.hx)
		{
			
			for (iCol = pListState->iMoveCol + 1; iCol < eaSize(&pListState->eaCols); iCol++)
			{
				if (iPosX >= pListState->eaCols[iCol]->UnpaddedScreenBox.lx &&
					iPosX - pListState->eaCols[iCol]->UnpaddedScreenBox.lx <= 5)
				{
					if (--iCol != pListState->iMoveCol)
					{
						pListState->iDestCol = iCol;
						pListState->bDestDrawHigh = true;
					}
					break;
				}
				else if (iPosX <= pListState->eaCols[iCol]->UnpaddedScreenBox.hx && 
						 pListState->eaCols[iCol]->UnpaddedScreenBox.hx - iPosX <= 5)
				{
					pListState->iDestCol = iCol;
					pListState->bDestDrawHigh = true;
					break;
				}
			}
		}

		//After letting go of the mouse, check to see where that was
		if (!mouseIsDown(MS_LEFT))
		{
			if (iCol >=0
				&& iCol != pListState->iMoveCol
				&& iCol < eaSize(&pListState->eaCols))
			{
				S32 i;
				eaMove(&pListState->eaCols, iCol, pListState->iMoveCol);
				eaMove(&pListState->eaColumnOrder, iCol, pListState->iMoveCol);

				for (i = eaSize(&pListState->eaCols)-1; i >= 0; i--)
				{
					if (UI_GEN_NON_NULL(pListState->eaCols[i]))
					{
						UIGenListColumnState *pColumnState = UI_GEN_STATE(pListState->eaCols[i], ListColumn);
						pColumnState->iColumn = i;
					}
				}
				ui_GenListSaveColumnOrder(pColState->pList, pListState);

				//Fix the sort column
				if(	 pListState->iSortCol == pListState->iMoveCol )
				{
					pListState->iSortCol = iCol;
				}
				else if(pListState->iSortCol == iCol)
				{
					pListState->iSortCol = pListState->iMoveCol;
				}
			}

			pListState->iDestCol = -1;
			pListState->iMoveCol = -1;
		}
	}

	if (eaSize(&pColState->eaRows))
	{
		CBox ClipBox = pGen->pParent->UnpaddedScreenBox;
		const CBox *pOldBox = mouseClipGet();
		CBox OldBox;
		ClipBox.ly = pGen->UnpaddedScreenBox.hy;
		// Column boxes describe the column header, but we actually want
		// to clip row children to the list, not the header.
		if (pOldBox && pCol->polyp.bClipInput)
		{
			OldBox = *pOldBox;
			mouseClipPop();
		}
		mouseClipPush(&ClipBox);
		ui_GenForEachInPriority(&pColState->eaRows, ui_GenTickListRow, pColState->pList, UI_GEN_TICK_ORDER);
		mouseClipPop();
		if (pOldBox && pCol->polyp.bClipInput)
			mouseClipPush(&OldBox);
	}
}

void ui_GenListSetSelectedRow(UIGen *pGen, S32 iRow)
{
	UIGenList *pList = UI_GEN_RESULT(pGen, List);
	UIGenListState *pState = UI_GEN_STATE(pGen, List);
	bool bNewSelection = false;

	if (pState->iSelectedRow != iRow)
	{
		pState->iSelectedRow = iRow;
		bNewSelection = true;
	}
	if (pState->bSelectedRowNotSelected)
	{
		pState->bSelectedRowNotSelected = false;
		bNewSelection = true;
	}
	if (pState->bUnselected)
	{
		pState->bUnselected = false;
		bNewSelection = true;
	}

	if (pList->bShowSelectedRow)
		pState->bForceShowSelectedRow = true;

	if (pList->pchSelectionGroup)
	{
		UIGenListSelectionGroup* pListGroup = NULL;
		if (!stashFindPointer(stListGroups, pList->pchSelectionGroup, &pListGroup))
		{
			stashAddPointer(stListGroups, pList->pchSelectionGroup, pListGroup = StructCreate(parse_UIGenListSelectionGroup), false);
		}
		pListGroup->pvGen = pGen;
		pListGroup->iSelectedRow = iRow;
	}

	if (bNewSelection)
		ui_GenTutorialEvent(NULL, pGen, kUIGenTutorialEventTypeSelect);
}

static void ui_GenTickEarlyListRow(UIGen *pGen) //TODO: clip 
{
	UIGenListRow *pRow = UI_GEN_RESULT(pGen, ListRow);
	UIGenListRowState *pState = UI_GEN_STATE(pGen, ListRow);
	UIGen *pGenList = (pRow && pState) ? pState->pList : NULL;
	UIGenList *pList = pGenList ? UI_GEN_RESULT(pGenList, List): NULL;
	UIGenListState *pListState = pGenList ? UI_GEN_STATE(pGenList, List): NULL;
	S32 iRow = pState->iRow;

	if (!pList)
		return;

	if (mouseCollision(&pGen->UnpaddedScreenBox) && ui_GenListIsSelectable(pGenList, pList, pListState, iRow))
	{
		if (mouseDownHit(MS_LEFT, &pGen->UnpaddedScreenBox))
		{
			ui_GenListSetSelectedRow(pGenList, iRow);

			// Remove soon
			ui_GenRunAction(pGen, pRow->pOnSelected);
			if (pList->bShowSelectedRow)
				pListState->bForceShowSelectedRow = true;
		}
		if (mouseDoubleClickHit(MS_LEFT, &pGen->UnpaddedScreenBox))
		{
			// Remove soon
			ui_GenRunAction(pGen, pRow->pOnSelected);
			ui_GenListActivateSelectedRow(pState->pList);
			if (pList->bShowSelectedRow)
				pListState->bForceShowSelectedRow = true;
		}	
	}
	if (pState->pAttribs && pState->pBlock && pRow->bAllowInteract)
	{
		S32 iWidth = smfblock_GetWidth(pState->pBlock);
		S32 iMinWidth = smfblock_GetMinWidth(pState->pBlock);
		S32 iHeight = smfblock_GetHeight(pState->pBlock);
		CBox box = pGen->ScreenBox;
		CBox smfBox = {0, 0, iMinWidth, iHeight};
		ui_AlignCBox(&box, &smfBox, pRow->eAlignment);
		smf_Interact(pState->pBlock, pState->pAttribs, smfBox.lx, smfBox.ly, g_SMFNavigateCallback, g_SMFHoverCallback, pGen);
	}
}

static void ui_GenHideListRow(UIGen *pGen)
{
	UIGenListRowState *pState = UI_GEN_STATE(pGen, ListRow);
	if (pState)
	{
		smfblock_Destroy(pState->pBlock);
		pState->pBlock = NULL;
		SAFE_FREE(pState->pAttribs);
	}
}

static void ui_GenHideListColumn(UIGen *pGen)
{
	UIGenListColumnState *pState = UI_GEN_STATE(pGen, ListColumn);
	if (pState)
	{
		smfblock_Destroy(pState->pBlock);
		pState->pBlock = NULL;
		SAFE_FREE(pState->pAttribs);
		eaDestroy(&pState->eaRows);
		eaDestroyUIGens(&pState->eaOwnedRows);
		while (eaSize(&pState->eaTemplateGens) > 0)
		{
			UIGenChild *pChild = eaPop(&pState->eaTemplateGens);
			UIGen *pChildGen = GET_REF(pChild->hChild);
			if (pChildGen)
				ui_GenReset(pChildGen);
		}
	}
}

static void ui_GenDrawEarlyListRow(UIGen *pGen)
{
	static unsigned char *s_estrText;
	UIGenListRow *pRow = UI_GEN_RESULT(pGen, ListRow);
	UIGenListRowState *pState = UI_GEN_STATE(pGen, ListRow);
	bool bHaveText = (pRow->pTextExpr || TranslateMessageRef(pRow->hSMF));

	if (pState->estrPlainString && *pState->estrPlainString && bHaveText)
	{
		UIStyleFont *pFont = GET_REF(pRow->hFont);
		CBox *pBox = &pGen->ScreenBox;
		F32 fXScale = pGen->fScale;
		F32 fYScale = pGen->fScale;
		F32 fX;
		F32 fY;
		S32 iFlags = 0;
		const char *pchText = pState->estrPlainString;

		if (ui_GenBundleTruncate(&pState->Truncate, pFont, GET_REF(pRow->hTruncate), CBoxWidth(pBox), fXScale, pchText, &s_estrText))
		{
			pchText = s_estrText;
		}

		if (pRow->bScaleToFit || pRow->bShrinkToFit)
		{
			F32 fWidth;
			F32 fHeight;
			ui_StyleFontDimensions(pFont, 1.f, pchText, &fWidth, &fHeight, true);
			fXScale = CBoxWidth(pBox) / fWidth;
			fYScale = CBoxHeight(pBox) / fHeight;
			fXScale = MIN(fXScale, fYScale);
			fYScale = fXScale;

			if (pRow->bShrinkToFit && fXScale > pGen->fScale)
			{
				fXScale = pGen->fScale;
				fYScale = pGen->fScale;
			}
		}

		if (pRow->eAlignment & UILeft)
			fX = pBox->lx;
		else if (pRow->eAlignment & UIRight)
			fX = pBox->hx - ui_StyleFontWidth(pFont, fXScale, pchText);
		else
		{
			fX = (pBox->lx + pBox->hx) / 2;
			iFlags |= CENTER_X;
		}

		if (pRow->eAlignment & UITop)
			fY = pBox->ly + ui_StyleFontLineHeight(pFont, fYScale);
		else if (pRow->eAlignment & UIBottom)
			fY = pBox->hy;
		else
		{
			fY = (pBox->ly + pBox->hy) / 2;
			iFlags |= CENTER_Y;
		}

		ui_StyleFontUse(pFont, false, kWidgetModifier_None);
		gfxfont_MultiplyAlpha(pGen->chAlpha);
		gfxfont_Print(fX, fY, UI_GET_Z(), fXScale, fYScale, iFlags, pchText);
	}
	else if (pState->pBlock && bHaveText)
	{
		S32 iWidth = smfblock_GetWidth(pState->pBlock);
		S32 iMinWidth = smfblock_GetMinWidth(pState->pBlock);
		S32 iHeight = smfblock_GetHeight(pState->pBlock);
		CBox smf = {0, 0, iMinWidth, iHeight};
		F32 fZ = UI_GET_Z();
		ui_AlignCBox(&pGen->ScreenBox, &smf, pRow->eAlignment);
		smf_ParseAndDisplay(pState->pBlock, NULL, smf.lx, smf.ly, fZ, iWidth, iHeight, false, false, false, pState->pAttribs, pGen->chAlpha, NULL, GenSpritePropGetCurrent());
	}
}

static void ui_GenDrawEarlyListColumn(UIGen *pGen)
{
	static char *s_estrText;
	UIGenListColumn *pColumn = UI_GEN_RESULT(pGen, ListColumn);
	UIGenListColumnState *pState = UI_GEN_STATE(pGen, ListColumn);
	UIGenList *pList = pState->pList ? UI_GEN_RESULT(pState->pList, List) : NULL;
	UIGenListState *pListState = pState->pList ? UI_GEN_STATE(pState->pList, List) : NULL;
	bool bHaveText = (pColumn->pTextExpr || TranslateMessageRef(pColumn->hSMF));

	if (!pList)
		return;

	if (pState->estrPlainString && *pState->estrPlainString && bHaveText)
	{
		UIStyleFont *pFont = GET_REF(pColumn->hFont);
		CBox *pBox = &pGen->ScreenBox;
		F32 fXScale = pGen->fScale;
		F32 fYScale = pGen->fScale;
		F32 fX;
		F32 fY;
		S32 iFlags = 0;
		const char *pchText = pState->estrPlainString;

		if (ui_GenBundleTruncate(&pState->Truncate, pFont, GET_REF(pColumn->hTruncate), CBoxWidth(pBox), fXScale, pchText, &s_estrText))
		{
			pchText = s_estrText;
		}

		if (pColumn->bScaleToFit || pColumn->bShrinkToFit)
		{
			F32 fWidth;
			F32 fHeight;
			ui_StyleFontDimensions(pFont, 1.f, pchText, &fWidth, &fHeight, true);
			fXScale = CBoxWidth(pBox) / fWidth;
			fYScale = CBoxHeight(pBox) / fHeight;
			fXScale = MIN(fXScale, fYScale);
			fYScale = fXScale;

			if (pColumn->bShrinkToFit && fXScale > pGen->fScale)
			{
				fXScale = pGen->fScale;
				fYScale = pGen->fScale;
			}
		}

		if (pColumn->eAlignment & UILeft)
			fX = pBox->lx;
		else if (pColumn->eAlignment & UIRight)
			fX = pBox->hx - ui_StyleFontWidth(pFont, fXScale, pchText);
		else
		{
			fX = (pBox->lx + pBox->hx) / 2;
			iFlags |= CENTER_X;
		}

		if (pColumn->eAlignment & UITop)
			fY = pBox->ly + ui_StyleFontLineHeight(pFont, fYScale);
		else if (pColumn->eAlignment & UIBottom)
			fY = pBox->hy;
		else
		{
			fY = (pBox->ly + pBox->hy) / 2;
			iFlags |= CENTER_Y;
		}

		ui_StyleFontUse(pFont, false, kWidgetModifier_None);
		gfxfont_MultiplyAlpha(pGen->chAlpha);
		gfxfont_Print(fX, fY, UI_GET_Z(), fXScale, fYScale, iFlags, pchText);
	}
	else if (pState->pBlock && bHaveText)
	{
		S32 iWidth = smfblock_GetWidth(pState->pBlock);
		S32 iMinWidth = smfblock_GetMinWidth(pState->pBlock);
		S32 iHeight = smfblock_GetHeight(pState->pBlock);
		CBox smf = {0, 0, iMinWidth, iHeight};
		F32 fZ = UI_GET_Z();
		ui_AlignCBox(&pGen->ScreenBox, &smf, pColumn->eAlignment);

		smf_ParseAndDisplay(pState->pBlock, NULL, smf.lx, smf.ly, fZ, iWidth, iHeight, false, false, false, pState->pAttribs, pGen->chAlpha, NULL, GenSpritePropGetCurrent());
	}

	if (pListState && pListState->SortAscIndicatorBundle.pTexture && pState->iColumn == pListState->iSortCol)
	{
		AtlasTex *pTex = pListState->SortAscIndicatorBundle.pTexture;
		CBox sortBox = { 0, 0, pTex->width * pGen->fScale, pTex->height * pGen->fScale };
		
		if (pListState->eSortMode == UISortAscending)
		{
			ui_AlignCBox(&pGen->ScreenBox, &sortBox, pList->eSortAscIndicatorAlignment);
			CBoxMoveX(&sortBox, sortBox.lx + pList->iSortAscIndicatorX * pGen->fScale);
			CBoxMoveY(&sortBox, sortBox.ly + pList->iSortAscIndicatorY * pGen->fScale);
			ui_GenBundleTextureDraw(pGen, pGen->pResult, &pListState->SortAscIndicatorBundleTexture, &sortBox, 0, 0, false, false, &pListState->SortAscIndicatorBundle, NULL);
		}
		else if (pListState->eSortMode == UISortDescending)
		{
			ui_AlignCBox(&pGen->ScreenBox, &sortBox, pList->eSortDesIndicatorAlignment);
			CBoxMoveX(&sortBox, sortBox.lx + pList->iSortDesIndicatorX * pGen->fScale);
			CBoxMoveY(&sortBox, sortBox.ly + pList->iSortDesIndicatorY * pGen->fScale);
			ui_GenBundleTextureDraw(pGen, pGen->pResult, &pListState->SortDesIndicatorBundleTexture, &sortBox, 0, 0, false, false, &pListState->SortDesIndicatorBundle, NULL);
		}
	}

	if (eaSize(&pState->eaRows))
	{
		CBox ClipBox = pGen->pParent->UnpaddedScreenBox;
		const CBox *pOldBox = clipperGetCurrentCBox();
		CBox OldBox;
		ClipBox.ly = pGen->UnpaddedScreenBox.hy;
		// Column boxes describe the column header, but we actually want
		// to clip row children to the list, not the header.
		if (pOldBox && pColumn->polyp.bClip)
		{
			OldBox = *pOldBox;
			clipperPop();
		}
		clipperPushRestrict(&ClipBox);
		ui_GenForEachInPriority(&pState->eaRows, ui_GenDrawColumnRow, pGen, UI_GEN_DRAW_ORDER);
		clipperPop();
		if (pOldBox && pColumn->polyp.bClip)
			clipperPush(&OldBox);
	}
}

static void ui_GenUpdateContextListRow(UIGen *pGen, ExprContext *pContext, UIGen *pFor)
{
	UIGenListRowState *pState = UI_GEN_STATE(pGen, ListRow);
	UIGen *pGenList = pState ? pState->pList : NULL;
	UIGenListState *pListState = pGenList ? UI_GEN_STATE(pGenList, List): NULL;
	void *pData = NULL;
	ParseTable *pTable = NULL;
	void ***peaModel = pGenList ? ui_GenGetList(pGenList, NULL, &pTable) : NULL;
	if (pListState && pListState->eaFilteredList)
		peaModel = &pListState->eaFilteredList;
	if (pState && pListState && peaModel)
		pData = eaGet(peaModel, pState->iDataRow);
	exprContextSetPointerVarPooledCached(pContext, s_pchRowData, pData, pTable, true, true, &s_iRowData);
	exprContextSetPointerVarPooledCached(pContext, g_ui_pchGenData, pData, pTable, true, true, &g_ui_iGenData);
	exprContextSetIntVarPooledCached(pContext, s_pchRowNumber, pState ? pState->iRow : 0, &s_iRowNumber);
	exprContextSetPointerVarPooledCached(pContext, s_pchList, pGenList, parse_UIGen, true, true, &s_iListPtr);
	exprContextSetPointerVarPooledCached(pContext, s_pchListRow, pGen, parse_UIGen, true, true, &s_iListRowPtr);
}

static __forceinline UIGen *ui_GenGetRowGen(UIGen *pGen, UIGen *pFor)
{
	UIGen *pParent;
	for (pParent = pFor; pParent && pParent->pParent != pGen; pParent = pParent->pParent)
	{
	}
	return pParent;
}

static __forceinline void ui_GenSetTemplateChildVar(ExprContext *pContext, UIGen *pGenList, UIGen *pGen, UIGen ***peaRows)
{
	S32 iRow = eaFind(peaRows, pGen);
	exprContextSetIntVarPooledCached(pContext, s_pchRowNumber, iRow, &s_iRowNumber);
	exprContextSetPointerVarPooledCached(pContext, s_pchList, pGenList, parse_UIGen, true, true, &s_iListPtr);
	exprContextSetPointerVarPooledCached(pContext, s_pchListRow, pGen, parse_UIGen, true, true, &s_iListRowPtr);
}

static void ui_GenUpdateContextList(UIGen *pGen, ExprContext *pContext, UIGen *pFor)
{
	UIGenListState *pState = UI_GEN_STATE(pGen, List);
	UIGen *pRowGen;

	if (!pState)
		return;

	if (pGen == pFor)
	{
		// Set variables for the list

		if (eaSize(&pState->eaCols))
		{
			// Use the maximum column count
			exprContextSetIntVarPooledCached(pContext, s_pchRowCount, ui_GenListGetSize(pState), &s_iRowCount);
		}
		else
		{
			exprContextSetIntVarPooledCached(pContext, s_pchRowCount, eaSize(&pState->eaRows), &s_iRowCount);
		}

		return;
	}

	// If there are columns, the column should have filled in the context already.
	if (eaSize(&pState->eaCols))
		return;

	pRowGen = ui_GenGetRowGen(pGen, pFor);
	if (!pRowGen)
		return;

	exprContextSetIntVarPooledCached(pContext, s_pchRowCount, eaSize(&pState->eaRows), &s_iRowCount);

	// Need to fill in list row vars here for non ListRow template children
	if (!UI_GEN_IS_TYPE(pGen, kUIGenTypeListRow))
		ui_GenSetTemplateChildVar(pContext, pGen, pRowGen, &pState->eaRows);
}

static void ui_GenUpdateContextListColumn(UIGen *pGen, ExprContext *pContext, UIGen *pFor)
{
	UIGenListColumnState *pState = UI_GEN_STATE(pGen, ListColumn);
	UIGen *pRowGen;

	if (!pState)
		return;

	pRowGen = ui_GenGetRowGen(pGen, pFor);
	if (!pRowGen)
		return;

	// Set the row count to the number of rows in this column
	exprContextSetIntVarPooledCached(pContext, s_pchRowCount, eaSize(&pState->eaRows), &s_iRowCount);

	// Need to fill in list row vars here for non ListRow template children
	if (!UI_GEN_IS_TYPE(pGen, kUIGenTypeListRow))
		ui_GenSetTemplateChildVar(pContext, pState->pList, pRowGen, &pState->eaRows);
}

static void ui_GenFitContentsSizeListRow(UIGen *pGen, UIGenListRow *pRow, CBox *pOut)
{
	UIGenListRowState *pState = UI_GEN_STATE(pGen, ListRow);
	Vec2 v2Size = {0, 0};
	Vec2 v2Temp;
	bool bHaveText = (pRow->pTextExpr || TranslateMessageRef(pRow->hSMF));

	if (pState->estrPlainString && bHaveText)
	{
		UIStyleFont *pFont = GET_REF(pRow->hFont);
		v2Size[0] = ui_StyleFontWidth(pFont, 1.f, pState->estrPlainString);
		v2Size[1] = ui_StyleFontLineHeight(pFont, 1.f);
	}
	else if (pState->pBlock && bHaveText)
	{
		v2Size[0] = smfblock_GetMinWidth(pState->pBlock);
		v2Size[0] /= (pGen->pResult && pGen->pResult->pos.Width.eUnit == UIUnitFitContents && pGen->fScale > 0) ? pGen->fScale : 1.0f;
		v2Size[1] = smfblock_GetHeight(pState->pBlock);
		v2Size[1] /= (pGen->pResult && pGen->pResult->pos.Height.eUnit == UIUnitFitContents && pGen->fScale > 0) ? pGen->fScale : 1.0f;
	}

	v2Temp[0] = v2Size[0];
	v2Temp[1] = v2Size[1];
	ui_GenChildForEachInPriority(&pRow->polyp.eaChildren, ui_GenGetBounds, v2Size, UI_GEN_LAYOUT_ORDER);
	ui_GenForEachInPriority(&pRow->polyp.eaInlineChildren, ui_GenGetBounds, v2Size, UI_GEN_LAYOUT_ORDER);
	MAX1(v2Size[0], v2Temp[0]);
	MAX1(v2Size[1], v2Temp[1]);
	BuildCBox(pOut, 0, 0, v2Size[0], v2Size[1]);

}

static void ui_GenFitParentSizeListRow(UIGen *pGen, UIGenListRow *pRow, CBox *pOut)
{
	// Native size for a list row is 100% of its parent list width, and enough
	// height to fill up the parent list if each row was 100% native height.
	UIGenListRowState *pState = UI_GEN_STATE(pGen, ListRow);
	UIGen *pList = pState?pState->pList : NULL;
	if (UI_GEN_READY(pList))
	{
		UIGenList *pListResult = UI_GEN_RESULT(pList, List);
		UIGenListState *pListState = UI_GEN_STATE(pList, List);
		CBox *pParentBox = &pGen->pParent->ScreenBox;
		S32 iSize = ui_GenListGetSize(pListState);
		if (!iSize)
			iSize = 1;
		switch (pListResult->FitParentMode.eMode)
		{
			xcase kUIGenListFitParentMode_Accordion:
			{
				F32 fHeight = pListResult->FitParentMode.fValue;
				if (ui_GenInState(pGen, kUIGenStateSelected))
				{
					fHeight = CBoxHeight(pParentBox) - fHeight * (iSize - 1) - pListResult->iRowSpacing * (iSize - 1);
					fHeight = MAX(fHeight, pListResult->FitParentMode.fValue);
				}
				BuildCBox(pOut, 0, 0, CBoxHeight(pParentBox), fHeight);
			}
			xdefault:
			{
				BuildCBox(pOut, 0, 0, CBoxWidth(pParentBox), ((CBoxHeight(pParentBox) - pListResult->iListTopMargin - pListResult->iListBottomMargin) / iSize) - pListResult->iRowSpacing);
			}
		}
	}
}


//TODO: This should check the size of all its rows to get the max and set it to that.
static void ui_GenFitContentsSizeListColumn(UIGen *pGen, UIGenListColumn *pCol, CBox *pOut)
{
	UIGenListColumnState *pState = UI_GEN_STATE(pGen, ListColumn);
	Vec2 v2Size = {0, 0};
	bool bHaveText = (pCol->pTextExpr || TranslateMessageRef(pCol->hSMF));

	if (pState->estrPlainString && bHaveText)
	{
		UIStyleFont *pFont = GET_REF(pCol->hFont);
		pOut->lx = 0.f;
		pOut->ly = 0.f;
		pOut->hx = ui_StyleFontWidth(pFont, 1.f, pState->estrPlainString);
		pOut->hy = ui_StyleFontLineHeight(pFont, 1.f);
	}
	else if (pState->pBlock && bHaveText)
	{
		pOut->hx = smfblock_GetWidth(pState->pBlock);
		pOut->hx /= (pGen->pResult && pGen->pResult->pos.Width.eUnit == UIUnitFitContents && pGen->fScale > 0) ? pGen->fScale : 1.0f;
		pOut->hy = smfblock_GetHeight(pState->pBlock);
		pOut->hy /= (pGen->pResult && pGen->pResult->pos.Height.eUnit == UIUnitFitContents && pGen->fScale > 0) ? pGen->fScale : 1.0f;
	}
	else 
	{
		ui_GenChildForEachInPriority(&pCol->polyp.eaChildren, ui_GenGetBounds, v2Size, UI_GEN_LAYOUT_ORDER);
		ui_GenForEachInPriority(&pCol->polyp.eaInlineChildren, ui_GenGetBounds, v2Size, UI_GEN_LAYOUT_ORDER);
		BuildCBox(pOut, 0, 0, v2Size[0], v2Size[1]);
	}
}
static void ui_GenFitParentSizeListColumn(UIGen *pGen, UIGenListColumn *pCol, CBox *pOut)
{
	UIGenListColumnState *pState = UI_GEN_STATE(pGen, ListColumn);
	UIGen *pList = pState?pState->pList : NULL;

	if (pList && UI_GEN_READY(pList))
	{
		ParseTable *pTable = NULL;
		void ***peaModel = ui_GenGetList(pList, NULL, &pTable);
		UIGenListState *pListState = UI_GEN_STATE(pList, List);
		CBox *pParentBox = &pGen->pParent->ScreenBox;
		if (pListState->eaFilteredList)
			peaModel = &pListState->eaFilteredList;
		BuildCBox(pOut, 0, 0, (CBoxWidth(pParentBox)/eaSize(&pListState->eaCols)) , (CBoxHeight(pParentBox) / eaSize(peaModel)));
	}
}

static bool ui_GenValidateListRow(UIGen *pGen, UIGenInternal *pInt, const char *pchDescriptor)
{
	UIGenListRow *pBase = (UIGenListRow*)pGen->pBase;
	UIGenListRow *pListRow = (UIGenListRow*)pInt;
	UIGenStateDef *pSelected = eaIndexedGetUsingInt(&pGen->eaStates, kUIGenStateSelected);
	UIGenStateDef *pActivated = eaIndexedGetUsingInt(&pGen->eaStates, kUIGenStateLeftMouseDoubleClick);

	//UI_GEN_FAIL_IF_RETURN(pGen, pListRow->pOnSelected && SAFE_MEMBER(pSelected, pOnEnter), 0, 
	//	"Gen has both OnSelected action and StateDef Selected OnEnter action. This is no longer valid.");

	//UI_GEN_FAIL_IF_RETURN(pGen, pListRow->pOnActivated && SAFE_MEMBER(pActivated, pOnEnter), 0, 
	//	"Gen has both OnActivated action and StateDef LeftMouseDoubleClick OnEnter action. This is no longer valid.");

	//if (pListRow->pOnSelected)
	//{
	//	if (!pSelected)
	//	{
	//		pSelected = StructCreate(parse_UIGenStateDef);
	//		pSelected->eState = kUIGenStateMouseClick;
	//		eaPush(&pGen->eaStates, pSelected);
	//	}
	//	pSelected->pOnEnter = pListRow->pOnSelected;
	//	pListRow->pOnSelected = NULL;
	//}

	//if (pListRow->pOnActivated)
	//{
	//	if (!pActivated)
	//	{
	//		pActivated = StructCreate(parse_UIGenStateDef);
	//		pActivated->eState = kUIGenStateMouseClick;
	//		eaPush(&pGen->eaStates, pActivated);
	//	}
	//	pActivated->pOnEnter = pListRow->pOnActivated;
	//	pListRow->pOnActivated = NULL;
	//}

	return true;
}

static void ui_GenUpdateListRow(UIGen *pGen)
{
	UIGenListRow *pRow = UI_GEN_RESULT(pGen, ListRow);
	UIGenListRowState *pState = UI_GEN_STATE(pGen, ListRow);

	if (!pState->pList)
	{
		ErrorFilenamef(pGen->pchFilename, "UIGenListRow %s is a child of %s which is not of type List or Column.",
			pGen->pchName, SAFE_MEMBER(pGen->pParent, pchName));
		return;
	}

	if (pRow->pTextExpr || TranslateMessageRef(pRow->hSMF))
	{
		CBox *pBox = pGen->pParent == pState->pList ? &pGen->ScreenBox : &pState->pList->ScreenBox;
		static char *s_pchResult = NULL;
		F32 fTextWidth;
		F32 fTextHeight;

		estrClear(&s_pchResult);

		fTextWidth = CBoxWidth(pBox);
		fTextHeight = CBoxHeight(pBox);

		ui_GenGetTextFromExprMessage(pGen, pRow->pTextExpr, GET_REF(pRow->hSMF), NULL, (unsigned char **)&s_pchResult, pRow->bFilterProfanity);

		if (pRow->bPlainText)
		{
			if (pState->pBlock)
			{
				smfblock_Destroy(pState->pBlock);
				pState->pBlock = NULL;
			}

			estrCopy(&pState->estrPlainString, &s_pchResult);
		}
		else
		{
			if (pState->estrPlainString)
				estrDestroy(&pState->estrPlainString);

			if (!pState->pBlock)
				pState->pBlock = smfblock_Create();

			{
				F32 fCurrentWidth = smfblock_GetWidth(pState->pBlock);
				F32 fCurrentHeight = smfblock_GetHeight(pState->pBlock);
				if (!fTextWidth || UI_GEN_NEARF(fCurrentWidth, fTextWidth))
					fTextWidth = fCurrentWidth;
				if (!fTextHeight || UI_GEN_NEARF(fCurrentHeight, fTextHeight) || fCurrentHeight < fTextHeight)
					fTextHeight = fCurrentHeight;
			}

			if(pRow->bScaleToFit || pRow->bShrinkToFit)
			{
				pState->pBlock->bScaleToFit = true;
				pState->pBlock->fMinRenderScale = 0.0001f;
				if(pRow->bShrinkToFit)
				{
					pState->pBlock->fMaxRenderScale = 1.0f;
				}
				else
				{
					pState->pBlock->fMaxRenderScale = FLT_MAX;
				}
			}
			pState->pBlock->bNoWrap = pRow->bNoWrap;

			pState->pAttribs = smf_TextAttribsFromFont(pState->pAttribs, GET_REF(pRow->hFont));
			pState->pAttribs->ppScale = U32_TO_PTR((U32)(pGen->fScale * SMF_FONT_SCALE));
			smf_ParseAndFormat(pState->pBlock, s_pchResult, pBox->lx, pBox->ly, 0, fTextWidth, fTextHeight, false, false, pRow->bSafeMode, pState->pAttribs);
		}
	}
}

static UIGenType ui_GenDetermineType(UIGen *pGen)
{
	S32 i;
	UIGenType eBestType = kUIGenTypeNone;
	if (!pGen)
		return kUIGenTypeNone;
	if (pGen->eType != kUIGenTypeNone)
		return pGen->eType;
	ui_GenInitializeBorrows(pGen);
	for (i = 0; i < eaSize(&pGen->eaBorrowed); i++)
	{
		UIGen *pBorrowed = GET_REF(pGen->eaBorrowed[i]->hGen);
		if (pBorrowed)
		{
			if (pBorrowed->eType != kUIGenTypeNone && pBorrowed->eType != kUIGenTypeBox)
				return pBorrowed->eType;
			if (eBestType != pBorrowed->eType)
				eBestType = pBorrowed->eType;
		}
	}
	return eBestType;
}

static bool ui_GenValidateListColumn(UIGen *pGen, UIGenInternal *pInt, const char *pchDescriptor)
{
	UIGenListColumn *pBase = UI_GEN_INTERNAL(pGen->pBase, ListColumn);
	UIGenListColumn *pListColumn = UI_GEN_INTERNAL(pInt, ListColumn);
	UIGenStateDef *pSelected = eaIndexedGetUsingInt(&pGen->eaStates, kUIGenStateSelected);
	UIGenStateDef *pActivated = eaIndexedGetUsingInt(&pGen->eaStates, kUIGenStateLeftMouseDoubleClick);
	UIGen *pCellTemplate;

	if (IS_HANDLE_ACTIVE(pListColumn->hCellTemplate))
	{
		UI_GEN_FAIL_IF_RETURN(pGen, !GET_REF(pListColumn->hCellTemplate), 0,
			"Gen uses undefined CellTemplate %s", REF_STRING_FROM_HANDLE(pListColumn->hCellTemplate));

		pCellTemplate = GET_REF(pListColumn->hCellTemplate);
		UI_GEN_FAIL_IF_RETURN(pGen, ui_GenDetermineType(pCellTemplate) != kUIGenTypeListRow, 0,
			"Gen %s uses %s of type %s for CellTemplate", pGen->pchName, REF_STRING_FROM_HANDLE(pListColumn->hCellTemplate),
			StaticDefineIntRevLookup(UIGenTypeEnum, ui_GenDetermineType(pCellTemplate)));
	}

	//UI_GEN_FAIL_IF_RETURN(pGen, pListColumn->pOnSelected && SAFE_MEMBER(pSelected, pOnEnter), 0, 
	//	"Gen has both OnSelected action and StateDef Selected OnEnter action. This is no longer valid.");

	//UI_GEN_FAIL_IF_RETURN(pGen, pListColumn->pOnActivated && SAFE_MEMBER(pActivated, pOnEnter), 0, 
	//	"Gen has both OnActivated action and StateDef LeftMouseDoubleClick OnEnter action. This is no longer valid.");

	//if (pListColumn->pOnSelected)
	//{
	//	UI_GEN_FAIL_IF_RETURN(pGen, stricmp(pchDescriptor, "base"), 0, 
	//		"Gen has OnSelected outside of the Base. This is no longer valid.");

	//	if (!pSelected)
	//	{
	//		pSelected = StructCreate(parse_UIGenStateDef);
	//		pSelected->eState = kUIGenStateMouseClick;
	//		eaPush(&pGen->eaStates, pSelected);
	//	}
	//	pSelected->pOnEnter = pListColumn->pOnSelected;
	//	pListColumn->pOnSelected = NULL;
	//}

	//if (pListColumn->pOnActivated)
	//{
	//	UI_GEN_FAIL_IF_RETURN(pGen, stricmp(pchDescriptor, "base"), 0, 
	//		"Gen has OnActivated outside of the Base. This is no longer valid.");

	//	if (!pActivated)
	//	{
	//		pActivated = StructCreate(parse_UIGenStateDef);
	//		pActivated->eState = kUIGenStateMouseClick;
	//		eaPush(&pGen->eaStates, pActivated);
	//	}
	//	pActivated->pOnEnter = pListColumn->pOnActivated;
	//	pListColumn->pOnActivated = NULL;
	//}

	return true;
}

static void ui_GenPointerUpdateListColumn(UIGen *pGen)
{
	UIGenListColumn *pCol = UI_GEN_RESULT(pGen, ListColumn);
	UIGenListColumnState *pState = UI_GEN_STATE(pGen, ListColumn);
	UIGenListState *pListState = pState->pList ? UI_GEN_STATE(pState->pList, List) : NULL;
	UIGenList *pList = pState->pList ? UI_GEN_RESULT(pState->pList, List) : NULL;
	UIGen *pRowTemplate = (pCol && pState) ? GET_REF(pCol->hCellTemplate) : NULL;
	if (pCol)
	{
		ui_GenSyncRowsWithModel(pGen, pState->pList, pList, pListState, &pState->eaRows, &pState->eaOwnedRows, NULL, pRowTemplate, pState->iColumn, &pCol->eaTemplateChild, &pState->eaTemplateGens, pList->bAppendTemplateChildren);
		ui_GenForEach(&pState->eaRows, ui_GenPointerUpdateCB, pGen, UI_GEN_UPDATE_ORDER);
	}
}

static void ui_GenUpdateListColumn(UIGen *pGen)
{
	UIGenListColumn *pCol = UI_GEN_RESULT(pGen, ListColumn);
	UIGenListColumnState *pState = UI_GEN_STATE(pGen, ListColumn);
	UIGenListState *pListState = pState->pList ? UI_GEN_STATE(pState->pList, List) : NULL;
	UIGenList *pList = pState->pList ? UI_GEN_RESULT(pState->pList, List) : NULL;
	UIGen *pRowTemplate = (pCol && pState) ? GET_REF(pCol->hCellTemplate) : NULL;

	if (!pList)
	{
		ErrorFilenamef(pGen->pchFilename, "UIGenListColumn %s is a child of %s which is not of type List.",
			pGen->pchName, SAFE_MEMBER(pGen->pParent, pchName));
		eaClearStruct(&pState->eaRows, parse_UIGen);
		return;
	}

	if (!pRowTemplate || !devassertmsg(UI_GEN_NON_NULL(pRowTemplate) && pRowTemplate->eType == kUIGenTypeListRow, "Gen row type is incorrect or missing"))
		return;

	if (pState->pCellTemplate != pRowTemplate)
	{
		eaDestroy(&pState->eaRows);
		eaDestroyUIGens(&pState->eaOwnedRows);
	}

	//Layout the text
	if (pCol->pTextExpr || TranslateMessageRef(pCol->hSMF))
	{
		CBox box = pGen->ScreenBox;
		static char *s_pchResult = NULL;
		F32 fTextWidth;
		F32 fTextHeight;

		estrClear(&s_pchResult);

		fTextWidth = (S32)CBoxWidth(&box);
		fTextHeight = (S32)CBoxHeight(&box);

		ui_GenGetTextFromExprMessage(pGen, pCol->pTextExpr, GET_REF(pCol->hSMF), NULL, (unsigned char **)&s_pchResult, pCol->bFilterProfanity);

		if (pCol->bPlainText)
		{
			if (pState->pBlock)
			{
				smfblock_Destroy(pState->pBlock);
				pState->pBlock = NULL;
			}

			estrCopy(&pState->estrPlainString, &s_pchResult);
		}
		else
		{
			if (pState->estrPlainString)
				estrDestroy(&pState->estrPlainString);

			if (!pState->pBlock)
				pState->pBlock = smfblock_Create();

			{
				F32 fCurrentWidth = smfblock_GetWidth(pState->pBlock);
				F32 fCurrentHeight = smfblock_GetHeight(pState->pBlock);
				if (!fTextWidth || UI_GEN_NEARF(fCurrentWidth, fTextWidth))
					fTextWidth = fCurrentWidth;
				if (!fTextHeight || UI_GEN_NEARF(fCurrentHeight, fTextHeight) || fCurrentHeight < fTextHeight)
					fTextHeight = fCurrentHeight;
			}

			if(pCol->bScaleToFit || pCol->bShrinkToFit)
			{
				pState->pBlock->bScaleToFit = true;
				pState->pBlock->fMinRenderScale = 0.0001f;
				if(pCol->bShrinkToFit)
				{
					pState->pBlock->fMaxRenderScale = 1.0f;
				}
				else
				{
					pState->pBlock->fMaxRenderScale = FLT_MAX;
				}
			}
			pState->pBlock->bNoWrap = pCol->bNoWrap;

			pState->pAttribs = smf_TextAttribsFromFont(pState->pAttribs, GET_REF(pCol->hFont));
			pState->pAttribs->ppScale = U32_TO_PTR((U32)(pGen->fScale * SMF_FONT_SCALE));
			smf_ParseAndFormat(pState->pBlock, s_pchResult, box.lx, box.ly, 0, fTextWidth, fTextHeight, false, false, pCol->bSafeMode, pState->pAttribs);
		}
	}

	pState->pCellTemplate = pRowTemplate;
	ui_GenForEach(&pState->eaRows, ui_GenUpdateCB, pGen, UI_GEN_UPDATE_ORDER);
}

void ui_GenListOrderRegisterCallbacks(UIGenGetListOrder cbGetList, UIGenSetListOrder cbSetList)
{
	s_cbGetList = cbGetList;
	s_cbSetList = cbSetList;
}

static void ui_GenQueueResetList(UIGen *pGen)
{
	UIGenListState *pState = UI_GEN_STATE(pGen, List);
	ui_GenForEach(&pState->eaCols, ui_GenQueueResetChildren, pGen, false);
	ui_GenForEach(&pState->eaRows, ui_GenQueueResetChildren, pGen, false);
}

static void ui_GenQueueResetListColumn(UIGen *pGen)
{
	UIGenListColumnState *pState = UI_GEN_STATE(pGen, ListColumn);
	ui_GenForEach(&pState->eaRows, ui_GenQueueResetChildren, pGen, false);
}

bool ui_GenValidateList(UIGen *pGen, UIGenInternal *pInternal, const char *pchDescriptor)
{
	UIGenList *pList = UI_GEN_INTERNAL(pInternal, List);
	UIGen *pRowTemplate;
	S32 i;

	if (IS_HANDLE_ACTIVE(pList->hRowTemplate))
	{
		UI_GEN_FAIL_IF_RETURN(pGen, !GET_REF(pList->hRowTemplate), 0,
			"Gen uses undefined RowTemplate %s", REF_STRING_FROM_HANDLE(pList->hRowTemplate));

		pRowTemplate = GET_REF(pList->hRowTemplate);
		UI_GEN_FAIL_IF_RETURN(pGen, ui_GenDetermineType(pRowTemplate) != kUIGenTypeListRow, 0,
			"Gen %s uses %s of type %s for RowTemplate", pGen->pchName, REF_STRING_FROM_HANDLE(pList->hRowTemplate),
			StaticDefineIntRevLookup(UIGenTypeEnum, ui_GenDetermineType(pRowTemplate)));
	}

	for (i = 0; i < eaSize(&pList->eaColumnChildren); i++)
	{
		UIGen *pColumn = GET_REF(pList->eaColumnChildren[i]->hChild);
		UI_GEN_FAIL_IF_RETURN(pGen, !pColumn || ui_GenDetermineType(pColumn) != kUIGenTypeListColumn, 0,
			"Gen %s uses %s of type %s for Column", pGen->pchName, REF_STRING_FROM_HANDLE(pList->eaColumnChildren[i]->hChild),
			StaticDefineIntRevLookup(UIGenTypeEnum, ui_GenDetermineType(pColumn)));
	}

	return true;
}

AUTO_RUN;
void ui_GenListRegister(void)
{
	MP_CREATE(UIGenList, 32);
	MP_CREATE(UIGenListRow, 32);
	MP_CREATE(UIGenListColumn, 32);
	MP_CREATE(UIGenListState, 32);
	MP_CREATE(UIGenListRowState, 32);
	MP_CREATE(UIGenListColumnState, 32);
	MP_CREATE(UIGenListSelectionGroup, 32);

	ui_GenRegisterType(kUIGenTypeList, 
		ui_GenValidateList,
		ui_GenPointerUpdateList,
		ui_GenUpdateList, 
		UI_GEN_NO_LAYOUTEARLY, 
		ui_GenLayoutLateList, 
		ui_GenTickEarlyList, 
		UI_GEN_NO_TICKLATE, 
		ui_GenDrawEarlyList,
		ui_GenFitContentsSizeList, 
		UI_GEN_NO_FITPARENTSIZE, 
		ui_GenHideList, 
		UI_GEN_NO_INPUT, 
		ui_GenUpdateContextList, 
		ui_GenQueueResetList);

	ui_GenRegisterType(kUIGenTypeListRow, 
		ui_GenValidateListRow, 
		UI_GEN_NO_POINTERUPDATE,
		ui_GenUpdateListRow, 
		UI_GEN_NO_LAYOUTEARLY, 
		UI_GEN_NO_LAYOUTLATE, 
		ui_GenTickEarlyListRow, 
		UI_GEN_NO_TICKLATE, 
		ui_GenDrawEarlyListRow,
		ui_GenFitContentsSizeListRow, 
		ui_GenFitParentSizeListRow, 
		ui_GenHideListRow, 
		UI_GEN_NO_INPUT, 
		ui_GenUpdateContextListRow, 
		UI_GEN_NO_QUEUERESET);

	ui_GenRegisterType(kUIGenTypeListColumn, 
		ui_GenValidateListColumn, 
		ui_GenPointerUpdateListColumn,
		ui_GenUpdateListColumn, 
		UI_GEN_NO_LAYOUTEARLY, 
		UI_GEN_NO_LAYOUTLATE, 
		ui_GenTickEarlyListColumn, 
		UI_GEN_NO_TICKLATE, 
		ui_GenDrawEarlyListColumn,
		ui_GenFitContentsSizeListColumn, 
		ui_GenFitParentSizeListColumn, 
		ui_GenHideListColumn, 
		UI_GEN_NO_INPUT, 
		ui_GenUpdateContextListColumn, 
		ui_GenQueueResetListColumn);

	ui_GenInitPointerVar("List", parse_UIGen);
	ui_GenInitPointerVar("ListRow", parse_UIGen);
	ui_GenInitPointerVar("RowData", NULL);
	ui_GenInitIntVar("RowNumber", 0);
	ui_GenInitIntVar("RowCount", 0);

	s_pchRowData = allocFindString("RowData");
	s_pchRowNumber = allocAddString("RowNumber");
	s_pchRowCount = allocAddString("RowCount");
	s_pchList = allocAddString("List");
	s_pchListRow = allocAddString("ListRow");

	stListGroups = stashTableCreateWithStringKeys(32, StashDefault);
}

#include "UIGenList_h_ast.c"
