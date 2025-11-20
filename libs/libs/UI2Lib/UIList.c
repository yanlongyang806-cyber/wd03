/***************************************************************************



***************************************************************************/

#include "file.h"
#include "Estring.h"
#include "Message.h"
#include "Prefs.h"
#include "tokenstore.h"
#include "UIButton.h"
#include "UICheckButton.h"
#include "UIMenu.h"
#include "UIWindow.h"
#include "UIBoxSizer.h"

#include "inputLib.h"
#include "inputMouse.h"
#include "inputText.h"

#include "GfxClipper.h"
#include "GfxSprite.h"
#include "GfxSpriteText.h"
#include "GfxTexAtlas.h"
#include "GfxPrimitive.h"

#include "smf_render.h"

#include "UIList.h"
#include "UIScrollbar.h"
#include "UIDnD.h"

#include "earray.h"
#include <qsortG.h>
#include "StringUtil.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static void ui_ListPointToRowColumn(SA_PARAM_NN_VALID UIList *pList, F32 w, F32 h, F32 scale, F32 fPointRawX, F32 fPointX, F32 fPointY, SA_PRE_OP_FREE SA_POST_OP_VALID S32 *piRow, SA_PRE_OP_FREE SA_POST_OP_VALID S32 *piColumn, SA_PRE_OP_FREE SA_POST_OP_VALID F32 *pfCellX, SA_PRE_OP_FREE SA_POST_OP_VALID F32 *pfCellY, SA_PRE_OP_FREE SA_POST_OP_VALID CBox *pBox);

static Color s_ColumnBorderColor = {0, 0, 0, 64};

/// If set, then this overrides the default cell size
int g_ListCellSize;
AUTO_CMD_INT(g_ListCellSize, ListCellSize) ACMD_ACCESSLEVEL( 0 );

static void UpdateLastColumn(UIList *pList)
{
	for (pList->iLastColumn = eaSize(&pList->eaColumns) - 1; pList->iLastColumn >= 0; pList->iLastColumn--)
		if (!pList->eaColumns[pList->iLastColumn]->bHidden)
			break;
}

static bool IsLastColumn(UIList *pList, UIListColumn *pColumn)
{
	UIListColumn *pLast = eaGet(&pList->eaColumns, pList->iLastColumn);
	if (!pLast || pLast->bHidden)
	{
		// Last column index is outdated, update it.
		UpdateLastColumn(pList);
		pLast = eaGet(&pList->eaColumns, pList->iLastColumn);
	}
	return (pLast == pColumn) ? true : false;
}

static bool IsLastColumnIndex(UIList *pList, S32 iColumn)
{
	UIListColumn *pLast = eaGet(&pList->eaColumns, pList->iLastColumn);
	if (!pLast || pLast->bHidden)
		UpdateLastColumn(pList);
	return (iColumn == pList->iLastColumn) ? true : false;
}

F32 ui_ListGetTotalHeight(UIList *pList)
{
	F32 fHeight = 0;
	if (eaSize(&pList->eaSubLists) > 0)
	{
		S32 iRow;
		for (iRow = 0; iRow < eaSize(pList->peaModel); iRow++)
			fHeight += ui_ListGetRowHeight(pList, iRow);
	}
	else if (pList->peaModel)
		fHeight = eaSize(pList->peaModel) * pList->fRowHeight;
	return fHeight;
}

F32 ui_ListGetRowHeight(UIList *pList, S32 iRow)
{
	F32 fHeight = pList->fRowHeight;
	S32 i;
	ui_ListSetSubListsModelFromRow(pList, iRow);
	for (i = 0; i < eaSize(&pList->eaSubLists); i++)
	{
		UIList *pSubList = pList->eaSubLists[i];
		fHeight += ui_ListGetTotalHeight(pSubList);
	}
	return fHeight;
}

// Sets what's getting sorted.  Actually sorts at tick time, if SortOnlyOnUIChange is false; otherwise, sorts immediately, but not at all tick time.
void ui_ListSetSortedColumn(UIList *pList, S32 iColIndex)
{
	S32 i, iSortKey = -1;

	if ((pList->eaColumns[iColIndex]->eType == UIListPTName) ||
		(pList->eaColumns[iColIndex]->eType == UIListPTMessage))
	{
		FORALL_PARSETABLE(pList->pTable, i)
		{
			if (!stricmp(pList->pTable[i].name, pList->eaColumns[iColIndex]->contents.pchTableName))
			{
				iSortKey = i;
				break;
			}
		}
	}
	else if (pList->eaColumns[iColIndex]->eType == UIListPTIndex)
		iSortKey = pList->eaColumns[iColIndex]->contents.iTableIndex;
	//else
	//	return;

	if (iSortKey == -1)
	{
		pList->iSortedIndex = -1;
		if(pList->pSortColumn == pList->eaColumns[iColIndex])
			pList->eaColumns[iColIndex]->eSort = (pList->eaColumns[iColIndex]->eSort + 1) % UISortMax;
		else
			pList->eaColumns[iColIndex]->eSort = UISortAscending;
	}
	else
	{
		if (pList->iSortedIndex == iSortKey)
		{
			pList->eaColumns[iColIndex]->eSort = (pList->eaColumns[iColIndex]->eSort + 1) % UISortMax;
		}
		else
		{
			pList->iSortedIndex = iSortKey;
			pList->eaColumns[iColIndex]->eSort = UISortAscending;
		}
	}
	pList->eSort = pList->eaColumns[iColIndex]->eSort;
	pList->pSortColumn = pList->eaColumns[iColIndex];

	if(pList->bSortOnlyOnUIChange)
		ui_ListSort(pList);
}

void ui_ListSetNumLockedColumns(SA_PARAM_NN_VALID UIList *pList, S32 iCount)
{
	pList->iNumLockedColumns = iCount;
}

bool ui_ListVisitColumns(SA_PARAM_NN_VALID UIList *pList, UIVisitColumn fpVisit, UserData data)
{
	FOR_EACH_IN_EARRAY(pList->eaColumns, UIListColumn, pColumn)
		if (fpVisit(pList, pColumn, ipColumnIndex, data))
		{
			return true;
		}
	FOR_EACH_END

	return false;
}

UIListColumn* ui_ListFindColumnByTitleName(SA_PARAM_NN_VALID UIList *pList, SA_PARAM_NN_VALID const char *pchName)
{
	FOR_EACH_IN_EARRAY(pList->eaColumns, UIListColumn, pColumn)
		if (stricmp(ui_ListColumnGetTitle( pColumn ), pchName) == 0)
		{
			return pColumn;
		}
	FOR_EACH_END

	return NULL;
}

// Default just sorts by address
static S32 ui_ListComparatorDefault(UIList *pList, const void **ppA, const void **ppB)
{
	S32 iRet = (*ppA < *ppB) ? -1 : (*ppA > *ppB) ? 1 : 0;
	if (pList->eSort == UISortDescending)
		iRet = -iRet;
	return iRet;
}

static S32 ui_ListComparatorToken(UIList *pList, const void **ppA, const void **ppB)
{
	S32 iRet = (*ppA && *ppB) ? TokenCompare(pList->pTable, pList->iSortedIndex, *ppA, *ppB, 0, 0) : (*ppA ? 1 : (*ppB ? -1 : 0));
	if(iRet == 0)
	{
		int i;
		// Walk through all other tokens to resolve equality and to have a stable sort
		FORALL_PARSETABLE(pList->pTable, i)
			if(iRet = TokenCompare(pList->pTable, i, *ppA, *ppB, 0, 0))
				break;
	}

	if(iRet == 0)
		iRet = (*ppA < *ppB) ? -1 : (*ppA > *ppB) ? 1 : 0;
	if(pList->eSort == UISortDescending)
		iRet = -iRet;

	return iRet;
}

static S32 ui_ListComparatorText(UIList *pList, const void **ppA, const void **ppB)
{
	S32 iRet = 0;
	int iRowA = (char **)(ppA) - (char **)*pList->peaModel;
	int iRowB = (char **)(ppB) - (char **)*pList->peaModel;
	char *pchTextA = NULL;
	char *pchTextB = NULL;

	pList->pSortColumn->contents.cbText(pList, pList->pSortColumn, iRowA, pList->pSortColumn->pDrawData, &pchTextA);
	pList->pSortColumn->contents.cbText(pList, pList->pSortColumn, iRowB, pList->pSortColumn->pDrawData, &pchTextB);

	iRet = (pchTextA && pchTextB) ? stricmp(pchTextA, pchTextB) : (pchTextA ? 1 : (pchTextB ? -1 : 0));

	estrDestroy(&pchTextA);
	estrDestroy(&pchTextB);

	if(iRet == 0)
		iRet = (*ppA < *ppB) ? -1 : (*ppA > *ppB) ? 1 : 0;
	if(pList->eSort == UISortDescending)
		iRet = -iRet;

	return iRet;
}

void ui_ListSortEx(UIList *pList, bool bRememberSelectedPointer)
{
	S32 iRow;
	S32 iList;
	if(!pList->peaModel || eaSize(pList->peaModel) == 0)
		return;
	if(pList->eSort && pList->pSortColumn/* && pList->iSortedIndex >= 0*/)
	{
		// Record selection, and restore it after the sort
		const int * const *eaiRows;
		static void **s_eaSelectedValues = NULL;
		static int *s_eaiNewRows = NULL;
		int i;

		if (bRememberSelectedPointer)
		{
			eaiRows = ui_ListGetSelectedRows(pList);
			for(i=eaiSize(eaiRows)-1; i>=0; --i)
				eaPush(&s_eaSelectedValues, (*pList->peaModel)[(*eaiRows)[i]]);
		}

		// Do the actual sorting
		if(pList->iSortedIndex >= 0)
			eaQSort_s(*pList->peaModel, ui_ListComparatorToken, pList);
		else if(pList->pSortColumn->contents.cbText)
			eaQSort_s(*pList->peaModel, ui_ListComparatorText, pList);
		else
			eaQSort_s(*pList->peaModel, ui_ListComparatorDefault, pList);

		if (bRememberSelectedPointer)
		{
			// Restore the selection
			if (s_eaSelectedValues)
			{
				for(i=eaSize(&s_eaSelectedValues)-1; i>=0; --i)
				{
					int j;
					for(j=eaSize(pList->peaModel)-1; j>=0; --j) 
					{
						if (s_eaSelectedValues[i] == (*pList->peaModel)[j]) 
						{
							eaiPush(&s_eaiNewRows,j);
							break;
						}
					}
				}
			}
			ui_ListSetSelectedRows(pList, &s_eaiNewRows);
			eaClear(&s_eaSelectedValues);
			eaiClear(&s_eaiNewRows);
		}
	}

	//recurse to sort sub lists:
	for (iList = 0; iList < eaSize(&pList->eaSubLists); iList++)
	{
		UIList *pSubList = pList->eaSubLists[iList];
		for (iRow = 0; iRow < eaSize(pList->peaModel); iRow++)
		{
			ui_ListSetSubListModelFromRow(pList, pSubList, iRow);
			ui_ListSort(pSubList);
		}
	}
}

void ui_ListSetSubListModelCallback(SA_PARAM_NN_VALID UIList *pList, UIListModelChangeFunc cbSetModel, UserData pModelData)
{
	pList->cbModelChange = cbSetModel;
	pList->pModelData = pModelData;
}

void ui_ListSetSubListsModelFromRow(UIList *pList, S32 iRow)
{
	S32 i;
	for (i = 0; i < eaSize(&pList->eaSubLists); i++)
		ui_ListSetSubListModelFromRow(pList, pList->eaSubLists[i], iRow);
}

void ui_ListSetSubListModelFromRow(UIList *pList, UIList *pSubList, S32 iRow)
{
	void *pStruct;

	// Set the active sub-row
	pList->iActiveSubRow = iRow;

	// Call the registered callback if defined
	if (pList->cbModelChange)
	{
		// Callback is expected to perform "ui_ListSetModel(pSubList,*,*)" to correct model
		pList->cbModelChange(pList, pSubList, iRow, pList->pModelData);
	}
	else
	{
		// FIXME: Allow different subindices for different sublists.
		// By storing subindices on the sublist rather than the main list.
		pStruct = eaGet(pList->peaModel, iRow);
		if (pStruct)
		{
			UIModel peaSubModel = TokenStoreGetEArray(pList->pTable, pList->iSubIndex, pStruct, NULL);
			ui_ListSetModel(pSubList, pSubList->pTable, peaSubModel);
			pList->iActiveSubRow = iRow;
		}
	}
}

S32 ui_ListGetActiveParentRow(const UIList *pList)
{
	return pList->pParentList ? pList->pParentList->iActiveSubRow : -1;
}

// A cell was clicked on. fX and fY are relative to this cell's top-left, e.g.
// if the click was at the top-left, they are 0, 0.
void ui_ListCellDoSomething(UIList *pList, F32 fW, F32 fH, F32 fScale, S32 iColumn, S32 iRow, F32 fX, F32 fY, F32 fListRawX, F32 fListX, F32 fListY, CBox *pListBox, F32 fEndX, MouseButton eButton, mouseState eState)
{
	F32 fSubX = fListX - pList->fSubListIndent;
	F32 fSubRawX = fListRawX - pList->fSubListIndent;
	F32 fSubY = fY - pList->fRowHeight;
	
	// If we're past the fixed row height, then this was a click into a sublist row.
	if (fSubX > 0 && fSubY > 0)
	{
		F32 fSubCellX, fSubCellY;
		S32 iSubRow, iSubColumn;
		S32 i;
		CBox subListBox;
		ui_ListSetSubListsModelFromRow(pList, iRow);
		// Figure out which sublist we clicked into...
		for (i = 0; i < eaSize(&pList->eaSubLists); i++)
		{
			UIList *pSubList = pList->eaSubLists[i];
			F32 fSubListHeight = ui_ListGetTotalHeight(pSubList);
			if (fSubListHeight > fSubY)
			{
				ui_ListPointToRowColumn(pSubList, fW, fH, fScale, fSubRawX, fSubX, fSubY, &iSubRow, &iSubColumn, &fSubCellX, &fSubCellY, &subListBox);
				if (iSubRow >= 0 && iSubColumn >= 0)
				{
					subListBox.ly += pListBox->ly + pList->fRowHeight;
					subListBox.hy += pListBox->ly + pList->fRowHeight;
					subListBox.lx += pList->fSubListIndent;
					subListBox.hx += pList->fSubListIndent;
					ui_ListCellDoSomething(pSubList, fW, fH, fScale, iSubColumn, iSubRow, fSubCellX, fSubCellY, fSubRawX, fSubX, fSubY, &subListBox, fEndX, eButton, eState);
				}
				break;
			}
			else
				fSubY -= fSubListHeight;
		}
	}
	else
	{
		if (IsLastColumnIndex(pList, iColumn))
			pListBox->hx = fEndX;
		if (eButton == MS_LEFT && eState == MS_DOWN && pList->cbCellClicked)
			pList->cbCellClicked(pList, iColumn, iRow, fX, fY, pListBox, pList->pCellClickedData);
		else if (eButton == MS_RIGHT && eState == MS_DOWN && pList->cbCellContext)
			pList->cbCellContext(pList, iColumn, iRow, fX, fY, pListBox, pList->pCellContextData);
		else if (eButton == MS_LEFT && eState == MS_DBLCLICK && pList->cbCellActivated)
			pList->cbCellActivated(pList, iColumn, iRow, fX, fY, pListBox, pList->pCellActivatedData);
		else if (eButton == MS_MAXBUTTON && pList->cbCellHover)
			pList->cbCellHover(pList, iColumn, iRow, fX, fY, pListBox, pList->pCellHoverData);
	}
}

// Horrible hack for dumping memory budgets to a file.
void ui_ListDoCrazyCSVDumpThing(UIList *pList)
{
	char achFilename[1024];
	char *pchText = NULL;
	FileWrapper *pFile;
	S32 iRow;
	S32 iCol;

	if (!pList->peaModel || !pList->pTable)
		return;

	sprintf(achFilename, "List_Dump_%p.csv", pList);
	pFile = fileOpen(achFilename, "w");
	for (iCol = 0; iCol < eaSize(&pList->eaColumns); iCol++)
	{
		fprintf(pFile, "%s", ui_ListColumnGetTitle(pList->eaColumns[iCol]));
		if (iCol != eaSize(&pList->eaColumns) - 1)
			fprintf(pFile, ", ");
	}
	fprintf(pFile, "\n");

	for (iRow = 0; iRow < eaSize(pList->peaModel); iRow++)
	{
		for (iCol = 0; iCol < eaSize(&pList->eaColumns); iCol++)
		{
			S32 iParseCol = pList->eaColumns[iCol]->iParseIndexCache;
			estrClear(&pchText);
			if (iParseCol > 0 && TokenWriteText(pList->pTable, iParseCol, eaGet(pList->peaModel, iRow), &pchText, true))
				fprintf(pFile, "%s", pchText);
			if (iCol != eaSize(&pList->eaColumns) - 1)
				fprintf(pFile, ", ");
		}
		fprintf(pFile, "\n");
	}
	fclose(pFile);
	estrDestroy(&pchText);
}

bool ui_ListInput(UIList *pList, KeyInput *pKey)
{
	if (pKey->type != KIT_EditKey)
		return false;
	switch (pKey->scancode)
	{
	// FIXME: Handle sublists, column select, etc, etc, etc, giant
	// pile of pain.
	case INP_UP:
	case INP_LSTICK_UP:
	case INP_JOYPAD_UP:
		{
			S32 iRow = ui_ListGetSelectedRow(pList);
			iRow = MAX(iRow - 1, 0);
			ui_ListSetSelectedRowColEx(pList, iRow, -1, inpLevelPeek(INP_SHIFT), inpLevelPeek(INP_CONTROL));
			ui_ListScrollToRow(pList, iRow);
			ui_ListCallSelectionChangedCallback(pList);
			return true;
		}
	case INP_DOWN:
	case INP_LSTICK_DOWN:
	case INP_JOYPAD_DOWN:
		{
			S32 iRow = ui_ListGetSelectedRow(pList);
			iRow = MIN(iRow + 1, eaSize(pList->peaModel) - 1);
			ui_ListSetSelectedRowColEx(pList, iRow, -1, inpLevelPeek(INP_SHIFT), inpLevelPeek(INP_CONTROL));
			ui_ListScrollToRow(pList, iRow);
			ui_ListCallSelectionChangedCallback(pList);
			return true;
		}
	case INP_LEFT:
	case INP_LSTICK_LEFT:
	case INP_JOYPAD_LEFT:
		{
			return false;
		}
	case INP_RIGHT:
	case INP_LSTICK_RIGHT:
	case INP_JOYPAD_RIGHT:
		{
			return false;
		}
	case INP_AB:
	case INP_RETURN:
		// FIXME: Walk the list to find the selection
		if (pList->cbActivated && ui_ListGetSelectedRow(pList) >= 0)
			pList->cbActivated(pList, pList->pActivatedData);
		return true;
	case INP_J:
		if (pKey->attrib & KIA_CONTROL) {
			ui_ListScrollToSelection(pList);
			return true;
		}
	case INP_D:
		if (pKey->attrib & KIA_CONTROL && pKey->attrib & KIA_SHIFT && isDevelopmentMode()) {
			ui_ListDoCrazyCSVDumpThing(pList);
			return true;
		}
	default:
		return false;
	}
}

F32 ui_ListGetHeaderHeight(const UIList *pList, bool bIncludeSublists)
{
	F32 fHeight = pList->fHeaderHeight;
	S32 i;
	if( bIncludeSublists ) { 
		for (i = 0; i < eaSize(&pList->eaSubLists); i++)
			fHeight += ui_ListGetHeaderHeight(pList->eaSubLists[i], true);
	}
	
	for (i = 0; i < eaSize(&pList->eaPickerButtons); i++)
		fHeight = MAX( fHeight, pList->eaPickerButtons[i]->height + 4 );
	return fHeight;
}

F32 ui_ListGetTotalWidth(const UIList *pList)
{
	F32 fWidth = 0.f;
	S32 i;
	for (i = 0; i < eaSize(&pList->eaColumns); i++)
		if (!pList->eaColumns[i]->bHidden)
			fWidth += pList->eaColumns[i]->fWidth;
	for (i = 0; i < eaSize(&pList->eaSubLists); i++)
	{
		F32 fSubWidth = ui_ListGetTotalWidth(pList->eaSubLists[i]);
		MAX1(fWidth, fSubWidth + pList->fSubListIndent);
	}
	return fWidth;
}

static void ui_ListColumnTickResizing(UIList *pList, UIListColumn *pColumn, const CBox *pBox, F32 fScale)
{
	CBox resizeBox = {0, pBox->ly, 2 * UI_LIST_COLUMN_RESIZE_THRESHOLD * fScale, pBox->hy};
	CBoxMoveX(&resizeBox, pBox->hx - CBoxWidth(&resizeBox) / 2);

	if (mouseCollision(&resizeBox) || pColumn->bDragging)
		ui_SetCursorForDirection(UIRight);

	if (mouseDoubleClickHit(MS_LEFT, &resizeBox))
	{
		ui_SetFocus(pList);
		ui_ListColumnSetWidth(pColumn, true, UI_LIST_COLUMN_MIN_WIDTH);
		inpHandled();
	}
	else if (mouseDownHit(MS_LEFT, &resizeBox))
	{
		pColumn->bDragging = true;
		pColumn->bAutoSize = false;
		inpHandled();
	}
	else if (!mouseIsDown(MS_LEFT))
	{
		if(pColumn->bDragging)
		{
			if(pList->iPrefSet>-1)
				PrefStoreFloat(pList->iPrefSet, STACK_SPRINTF("%s.%s", pList->widget.name, ui_ListColumnGetTitle(pColumn)), pColumn->fWidth);
		}
		pColumn->bDragging = false;
	}

	if (pColumn->bDragging)
	{
		F32 fCenterX = (resizeBox.lx + resizeBox.hx) / 2;
		F32 fNewWidth = pColumn->fWidth + (g_ui_State.mouseX - fCenterX) / fScale;
		ui_SetFocus(pList);
		pColumn->fWidth = MAX(UI_LIST_COLUMN_MIN_WIDTH, fNewWidth);
		inpHandled();
	}
}

static void ListAutoColumnContextMenu(UIList *pList);

static F32 ListColumnsTickInternal(UIList *pList, UIList *pParentList, F32 fRawX, UI_MY_ARGS, F32 fEndX)
{
	F32 fWidth;
	F32 fTotalWidth = fEndX - x;
	bool bExpand = false;
	S32 i;
	UpdateLastColumn(pList);
	for (i = 0, fWidth = 0; i < eaSize(&pList->eaColumns); i++)
	{
		UIListColumn *pColumn = pList->eaColumns[i];
		F32 fColumnWidth;
		CBox headerBox;

		if (pColumn->bHidden)
			continue;

		if (IsLastColumnIndex(pList, i) && fWidth < fTotalWidth)
			bExpand = true;

		if (bExpand && i<pList->iNumLockedColumns)
			fColumnWidth = fEndX - fRawX - fWidth;
		else if (bExpand)
			fColumnWidth = fEndX - x - fWidth;
		else
			fColumnWidth = pColumn->fWidth * scale;

		// Locked columns use the raw X instead of the scrollbar adjusted X
		if (i < pList->iNumLockedColumns)
			BuildCBox(&headerBox, fRawX + fWidth, y, fColumnWidth, pList->fHeaderHeight * scale);
		else
			BuildCBox(&headerBox, x + fWidth, y, fColumnWidth, pList->fHeaderHeight * scale);

		if (pColumn->bResizable)
			ui_ListColumnTickResizing(pParentList, pColumn, &headerBox, scale);

		if (mouseDownHit(MS_LEFT, &headerBox))
		{
			ui_SetFocus(pParentList);
			if (pColumn->bSortable/* && (pColumn->eType == UIListPTIndex || pColumn->eType == UIListPTName || pColumn->eType == UIListPTMessage)*/)
				ui_ListSetSortedColumn(pList, i);
			if (pColumn->cbClicked)
				pColumn->cbClicked(pColumn, pColumn->pClickedData);
			inpHandled();
		}

		if (mouseDownHit(MS_RIGHT, &headerBox))
		{
			ui_SetFocus(pParentList);
			if(pColumn->cbContext)
				pColumn->cbContext(pColumn, pColumn->pContextData);
			else if(pParentList->bAutoColumnContextMenu)
				ListAutoColumnContextMenu(pParentList);
		}

		if (mouseCollision(&headerBox))
			inpHandled();

		fWidth += fColumnWidth;
	}

	x += pList->fSubListIndent * scale;
	fRawX += pList->fSubListIndent * scale;
	y += pList->fHeaderHeight * scale;

	for (i = 0; i < eaSize(&pList->eaSubLists); i++)
		y = ListColumnsTickInternal(pList->eaSubLists[i], pParentList, fRawX, UI_MY_VALUES, fEndX);
	return y;
}

void ui_ListColumnsTick(UIList *pList, F32 fRawX, UI_MY_ARGS, F32 fTotalWidth)
{
	UIList *pParentList = pList;
	ListColumnsTickInternal(pList, pParentList, fRawX, UI_MY_VALUES, x + fTotalWidth);
}

void ui_ListPointToRowColumn(UIList *pList, F32 w, F32 h, F32 scale, F32 fPointRawX, F32 fPointX, F32 fPointY, S32 *piRow, S32 *piColumn, F32 *pfCellX, F32 *pfCellY, CBox *pBox)
{
	S32 i;
	switch( pList->eDisplayMode ) {
		xcase UIListRows:
			if (piRow) {
				*piRow = -1;
				if (pList->peaModel && fPointX >= 0) {
					// If this list has a sublist, we can only calculate the row slowly.
					F32 fY = 0;
					if (eaSize(&pList->eaSubLists)) {
						*piRow = -1;
						for (i = 0; i < eaSize(pList->peaModel); i++) {
							F32 fHeight = ui_ListGetRowHeight(pList, i);
							if (fY + fHeight > fPointY) {
								*piRow = i;
								if (pfCellY)
									*pfCellY = fPointY - fY;
								if (pBox) {
									pBox->ly = fY;
									pBox->hy = pBox->ly + pList->fRowHeight;
								}
								break;
							} else
								fY += fHeight;
						}
					} else {
						*piRow = fPointY / pList->fRowHeight;
						if ((*piRow) >= eaSize(pList->peaModel))
							*piRow = -1;
						else if (pfCellY)
							*pfCellY = fPointY - *piRow * pList->fRowHeight;
						if (pBox && *piRow >= 0) {
							pBox->ly = pList->fRowHeight * *piRow;
							pBox->hy = pList->fRowHeight * (*piRow + 1);
						}
					}
				}
			}

			if (piColumn) {
				if (fPointY < 0)
					*piColumn = -1;
				else {
					F32 fX = 0;
					*piColumn = eaSize(&pList->eaColumns) - 1;
					for (i = 0; i < eaSize(&pList->eaColumns); i++) {
						F32 fWidth = pList->eaColumns[i]->fWidth;
						F32 fTestX = (i<pList->iNumLockedColumns) ? fPointRawX : fPointX;
						if (pList->eaColumns[i]->bHidden)
							continue;
						if (fX + fWidth > fTestX || i == *piColumn) {
							*piColumn = i;
							if (pfCellX)
								*pfCellX = fTestX - fX;
							if (pBox) {
								pBox->lx = fX;
								pBox->hx = pBox->lx + fWidth;
							}
							break;
						} else
							fX += fWidth;
					}
				}
			}

		xcase UIListIconGrid:
			if( piRow ) {
				int cellsPerRow = floor( w / (pList->fGridCellWidth * scale) );
				if( fPointY >= 0 && fPointX >= 0 && fPointX < cellsPerRow * pList->fGridCellWidth * scale ) {
					*piRow = ((int)floor( fPointY / (pList->fGridCellHeight * scale) ) * cellsPerRow
							  + floor( fPointX / (pList->fGridCellWidth * scale) ));
				} else {
					*piRow = -1;
				}
			}
			if( piColumn ) {
				*piColumn = pList->iGridColumn;
			}
	}

	if (piRow)
	{
		if (pList->peaModel)
			*piRow = CLAMP(*piRow, -1, eaSize(pList->peaModel) - 1);
		else
			*piRow = -1;
	}
	if (piColumn)
		*piColumn = CLAMP(*piColumn, -1, eaSize(&pList->eaColumns));
}

static void ListTickDragging(UIList *pList, CBox *pBox, UI_MY_ARGS, S32 iMouseOverRow, S32 iMouseOverColumn)
{
	if (pList->iDraggingRow != -1 && mouseUpHit(MS_LEFT, pBox))
	{
		S32 iDragToRow = pList->iDragToRow > pList->iDraggingRow ? pList->iDragToRow - 1 : pList->iDragToRow;
		ui_ListUnsort(pList);
		if (pList->cbDrag)
			pList->cbDrag(pList, pList->iDraggingRow, iDragToRow, pList->pDragData);
		pList->iDraggingRow = -1;
	}

	if (pList->iDraggingRow != -1 && !mouseIsDown(MS_LEFT))
		pList->iDraggingRow = -1;

	if (mouseCollision(pBox))
	{
		if (pList->iDraggingRow == -1 && mouseDrag(MS_LEFT))
		{
			pList->iDraggingRow = iMouseOverRow;
		}
		if (pList->iDraggingRow >= 0 && mouseIsDown(MS_LEFT))
		{
			S32 iItem = iMouseOverRow;
			pList->iDragToRow = iItem;
		}
	}
	else
		pList->iDragToRow = -1;
}

static F32 ListRowYOffset(UIList *pList, S32 iRow)
{
	F32 fY = 0.f;
	if (eaSize(&pList->eaSubLists) > 0)
	{
		S32 i;
		for (i = 0; i < iRow; i++)
			fY += ui_ListGetRowHeight(pList, i);
	}
	else
		fY = pList->fRowHeight * iRow;
	return fY;
}

void ui_ListScrollToPath(UIList *pList, UI_MY_ARGS)
{
	if (eaiSize(&pList->eaiScrollToPath))
	{
		F32 fOffset = UI_WIDGET(pList)->sb->ypos;
		F32 fHeight = h - UI_SCALE(ui_ListGetHeaderHeight(pList, true));
		
		F32 fTop = 0;
		F32 fBottom = 0;
		F32 fMax = 0;
		S32 iRow = pList->eaiScrollToPath[0];
		eaiClear(&pList->eaiScrollToPath);

		if( pList->eDisplayMode == UIListRows ) {
			S32 i;
			for (i = 0; i < iRow; i++) {
				F32 rowHeight = ui_ListGetRowHeight(pList, i);
				fTop += rowHeight;
				fMax += rowHeight;
			}
			for (; i < eaSize(pList->peaModel); i++)
				fMax += ui_ListGetRowHeight(pList, i);
			fBottom = fTop + pList->fRowHeight;
		} else if( pList->eDisplayMode == UIListIconGrid ) {
			int cellsPerRow = floor( w / pList->fGridCellWidth * scale );
			int numRows = (cellsPerRow == 0) ? 1 : RoundUpToGranularity( eaSize( pList->peaModel ), cellsPerRow ) / cellsPerRow;

			fTop = pList->fGridCellHeight * (iRow / cellsPerRow);
			fBottom = fTop + pList->fGridCellHeight;
			fMax = pList->fGridCellHeight * numRows;
		} else {
			assert( 0 );
		}

		fTop = UI_SCALE( fTop );
		fBottom = UI_SCALE( fBottom );
		fMax = UI_SCALE( fMax );

		if (pList->bScrollToCenter)
		{
			fTop = CLAMP(fTop - fHeight/2, 0, fMax - fHeight);
			pList->bScrollToCenter = false;
			UI_WIDGET(pList)->sb->ypos = fTop;
		}
		else // scroll to edge
		{
			if (fTop < fOffset)
				UI_WIDGET(pList)->sb->ypos = fTop;
			else if (fBottom > fOffset + fHeight)
				UI_WIDGET(pList)->sb->ypos = fBottom - fHeight;
		}
	}
}

static void ui_ListSwitchToRowView( UIButton* ignored, UIList* pList )
{
	pList->eDisplayMode = UIListRows;
}

static void ui_ListSwitchToIconGridView( UIButton* ignored, UIList* pList )
{
	pList->eDisplayMode = UIListIconGrid;
}

static void ui_ListInsideFillDrawingDescription( UIList* pList, UIDrawingDescription* desc )
{
	UISkin* skin = UI_GET_SKIN( pList );

	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName;
		if( !pList->bIsComboBoxDropdown ) {
			descName = skin->astrListInsideStyle;
		} else {
			descName = skin->astrComboBoxListStyle;
		}

		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", descName )) {
			desc->textureAssemblyName = descName;
		} else {
			desc->styleBorderName = descName;
		}
	} else {
		desc->textureNameUsingLegacyColor = "white";
		desc->overlayOutlineUsingLegacyColor2 = true;
	}
}

void ui_ListOutsideFillDrawingDescriptionFromSkin( UISkin* skin, UIDrawingDescription* desc, bool bIsComboBoxDropdown )
{
	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName;

		if( !bIsComboBoxDropdown ) {
			descName = skin->astrListOutsideStyle;
		} else {
			descName = NULL;
		}
		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", descName )) {
			desc->textureAssemblyName = descName;
		} else {
			desc->styleBorderName = descName;
		}
	}
}

static void ui_ListOutsideFillDrawingDescription( UIList* pList, UIDrawingDescription* desc )
{
	ui_ListOutsideFillDrawingDescriptionFromSkin( UI_GET_SKIN( pList ), desc, pList->bIsComboBoxDropdown );
}

static void ui_ListSelectedItemFillDrawingDescription( UIList* pList, UIDrawingDescription* desc )
{
	UISkin* skin = UI_GET_SKIN( pList );
		
	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName;
		if( !pList->bIsComboBoxDropdown ) {
			descName = skin->astrListStyleSelectedItem;
		} else {
			descName = skin->astrComboBoxListStyleSelectedItem;
		}
		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", descName )) {
			desc->textureAssemblyName = descName;
		} else {
			desc->styleBorderName = descName;
		}
	} else {
		desc->textureNameUsingLegacyColor = "white";
	}
}

UIStyleFont* ui_ListItemGetFontFromSkinAndWidget( UISkin* skin, UIWidget* pWidget, bool bSelected, bool bHover )
{
	UIStyleFont* pFont = NULL;

	if( bSelected || bHover ) {
		pFont = GET_REF( skin->hListItemFontSelectedItem );
	} else {
		pFont = GET_REF( skin->hListItemFont );
	}

	if( !pFont ) {
		pFont = ui_WidgetGetFont( pWidget );
	}

	return pFont;
}

static UIStyleFont* ui_ListItemGetFont( UIList* pList, bool bSelected, bool bHover )
{
	return ui_ListItemGetFontFromSkinAndWidget( UI_GET_SKIN( pList ), UI_WIDGET( pList ), bSelected, bHover );
}

UIStyleFont* ui_ListHeaderGetFontFromSkinAndWidget( UISkin* skin, UIWidget* pWidget )
{
	UIStyleFont* pFont = NULL;

	pFont = GET_REF( skin->hListHeaderFont );

	// Legacy behavior, so I don't break anything
	if( !pFont ) {
		pFont = GET_REF( skin->hWindowTitleFont );
	}

	if( !pFont ) {
		pFont = ui_WidgetGetFont( pWidget );
	}

	return pFont;
}

static UIStyleFont* ui_ListHeaderGetFont( UIList* pList )
{
	return ui_ListHeaderGetFontFromSkinAndWidget( UI_GET_SKIN( pList ), UI_WIDGET( pList ));
}

static UIStyleFont* ui_ListDefaultGetFont( UIList* list )
{
	UISkin* skin = UI_GET_SKIN( list );
	UIStyleFont* pFont = NULL;

	pFont = GET_REF( skin->hListDefaultFont );

	if( !pFont ) {
		pFont = ui_WidgetGetFont( UI_WIDGET( list ));
	}

	return pFont;
}

void ui_ListTick(UIList *pList, UI_PARENT_ARGS)
{
	UISkin* skin = UI_GET_SKIN(pList);
	UI_GET_COORDINATES(pList);
	F32 fHeaderHeight = ui_ListGetHeaderHeight(pList, true) * scale;
	F32 fDataHeight = 0;
	F32 fTotalWidth = 0;
	F32 fMouseRawX, fMouseX, fMouseY;
	F32 fCellX, fCellY;
	S32 iMouseOverRow, iMouseOverColumn;
	CBox cellBox;
	UIDrawingDescription insideDesc = { 0 };
	UIDrawingDescription outsideDesc = { 0 };
	ui_ListInsideFillDrawingDescription( pList, &insideDesc );
	ui_ListOutsideFillDrawingDescription( pList, &outsideDesc );

	ui_DrawingDescriptionInnerBoxCoords( &outsideDesc, &x, &y, &w, &h, scale );

	switch( pList->eDisplayMode ) {
		xcase UIListRows: {
			fDataHeight = ui_ListGetTotalHeight(pList) * scale;
			fTotalWidth = ui_ListGetTotalWidth(pList) * scale;
			if (UI_WIDGET(pList)->sb->scrollY && (!pList->bIsComboBoxDropdown || h < fDataHeight))
			{
				w -= ui_ScrollbarWidth(UI_WIDGET(pList)->sb) * scale;
				if (UI_WIDGET(pList)->sb->scrollX && w < fTotalWidth)
					h -= ui_ScrollbarHeight(UI_WIDGET(pList)->sb) * scale;

			}
			else if (UI_WIDGET(pList)->sb->scrollX)
			{
				h -= ui_ScrollbarHeight(UI_WIDGET(pList)->sb) * scale;
				if (UI_WIDGET(pList)->sb->scrollY && (!pList->bIsComboBoxDropdown || h < fDataHeight))
					w -= ui_ScrollbarWidth(UI_WIDGET(pList)->sb) * scale;
			}
		}

		xcase UIListIconGrid: {
			int cellsPerRow;
			int numRows;
			w -= ui_ScrollbarWidth(UI_WIDGET(pList)->sb) * scale;
			
			cellsPerRow = floor( w / (pList->fGridCellWidth * scale) );
			numRows = (cellsPerRow == 0) ? 1 : RoundUpToGranularity( eaSize( pList->peaModel ), cellsPerRow ) / cellsPerRow;

			fTotalWidth = cellsPerRow * pList->fGridCellWidth * scale;
			fDataHeight = numRows * pList->fGridCellHeight * scale;
		}
	}

	BuildCBox(&box, x, y, w, h);
	CBoxClipTo(&pBox, &box);

	ui_ListScrollToPath(pList, UI_MY_VALUES);

	MAX1(fTotalWidth, w);

	ui_ScrollbarTick(UI_WIDGET(pList)->sb, x, y, w, h, z, scale, fTotalWidth, fDataHeight + fHeaderHeight);
	if( !pList->bIsComboBoxDropdown ) {
		w -= skin->iListContentExtraPaddingRight;
	}
	BuildCBox( &pList->lastDrawBox, x, y, w, h );

	if(!pList->bSortOnlyOnUIChange)
		ui_ListSort(pList);

	ui_WidgetGroupTick( &pList->eaPickerButtons, UI_MY_VALUES );
	{
		F32 fChildX = x - UI_WIDGET(pList)->sb->xpos;
		F32 fChildY = (y - UI_WIDGET(pList)->sb->ypos) + fHeaderHeight;
		UI_TICK_EARLY_WITH(pList, true, true, fChildX, fChildY, fTotalWidth, fDataHeight, scale);
	}

	ui_ListColumnsTick(pList, x, x - UI_WIDGET(pList)->sb->xpos, y, w, h, scale, fTotalWidth);

	fMouseX = g_ui_State.mouseX - (x - UI_WIDGET(pList)->sb->xpos);
	fMouseRawX = g_ui_State.mouseX - x;
	fMouseY = g_ui_State.mouseY - ((y + fHeaderHeight) - UI_WIDGET(pList)->sb->ypos);

	fMouseX /= scale;
	fMouseRawX /= scale;
	fMouseY /= scale;
	fTotalWidth /= scale;

	ui_ListPointToRowColumn(pList, w, h, scale, fMouseRawX, fMouseX, fMouseY, &iMouseOverRow, &iMouseOverColumn, &fCellX, &fCellY, &cellBox);
	if (IsLastColumnIndex(pList, iMouseOverColumn))
		cellBox.hx = fTotalWidth / scale;

	if (iMouseOverRow >= 0 && iMouseOverColumn >= 0)
	{
		if (mouseCollision(&box))
		{
			pList->hoverCol = pList->eaColumns[iMouseOverColumn];
			pList->hoverRow = iMouseOverRow;
			
			// Separate hover callbacks, so you can do something when the user
			// is hovering over a row in the top-level list, or when the user
			// is hovering over any sublist cell.
			if (iMouseOverColumn < eaSize(&pList->eaColumns))
			{
				if( pList->cbRowHover )
					pList->cbRowHover(pList, pList->hoverCol, pList->hoverRow, pList->pRowHoverData);
				pList->bRowHoverActive = true;
			}
			ui_ListCellDoSomething(pList, w, h, scale, iMouseOverColumn, iMouseOverRow, fCellX, fCellY, fMouseRawX, fMouseX, fMouseY, &cellBox, fTotalWidth, MS_MAXBUTTON, MS_DOWN);
		}
		else if (pList->bRowHoverActive) 
		{
			pList->hoverCol = NULL;
			pList->hoverRow = -1;
			
			if (pList->cbRowHover)
				pList->cbRowHover(pList, NULL, -1, pList->pRowHoverData);
			pList->bRowHoverActive = false;
		}


		if (mouseDownHit(MS_LEFT, &box) && !ui_DragIsActive())
		{
			ui_SetFocus(pList);
			ui_ListCellDoSomething(pList, w, h, scale, iMouseOverColumn, iMouseOverRow, fCellX, fCellY, fMouseRawX, fMouseX, fMouseY, &cellBox, fTotalWidth, MS_LEFT, MS_DOWN);
		}
		if (mouseDownHit(MS_RIGHT, &box))
		{
			ui_SetFocus(pList);
			ui_ListCellDoSomething(pList, w, h, scale, iMouseOverColumn, iMouseOverRow, fCellX, fCellY, fMouseRawX, fMouseX, fMouseY, &cellBox, fTotalWidth, MS_RIGHT, MS_DOWN);
		}
		if (mouseDoubleClickHit(MS_LEFT, &box) && !ui_DragIsActive())
		{
			ui_SetFocus(pList);
			ui_ListCellDoSomething(pList, w, h, scale, iMouseOverColumn, iMouseOverRow, fCellX, fCellY, fMouseRawX, fMouseX, fMouseY, &cellBox, fTotalWidth, MS_LEFT, MS_DBLCLICK);
		}
	}
	else if (pList->bRowHoverActive) 
	{
		pList->hoverCol = NULL;
		pList->hoverRow = -1;
		
		if (pList->cbRowHover)
			pList->cbRowHover(pList, NULL, -1, pList->pRowHoverData);
		pList->bRowHoverActive = false;
	}
	else if(pList->bAutoColumnContextMenu)
	{
		// If all columns have been hidden, show the auto column context menu when right-clicking anywhere in the list area.
		S32 i, visible = 0;
		for(i = 0; i < eaSize(&pList->eaColumns); i++)
			if(!pList->eaColumns[i]->bHidden)
				visible++;
		if(0 == visible && mouseDownHit(MS_RIGHT, &box))
			ListAutoColumnContextMenu(pList);
	}

	if (pList->bRowsDraggable)
		ListTickDragging(pList, &box, UI_MY_VALUES, iMouseOverRow, iMouseOverColumn);

	UI_TICK_LATE(pList);

	if( pList->bShowDisplayModePicker ) {
		int xIt;
		int it;
		if( !eaSize( &pList->eaPickerButtons )) {
			UIButton* button;

			button = ui_ButtonCreateImageOnly( "CF_Button_ListView", 0, 0, ui_ListSwitchToRowView, pList );
			eaPush( &pList->eaPickerButtons, UI_WIDGET( button ));
			button = ui_ButtonCreateImageOnly( "CF_Button_GalleryView", 0, 0, ui_ListSwitchToIconGridView, pList );
			eaPush( &pList->eaPickerButtons, UI_WIDGET( button ));
		}

		xIt = 2;
		for( it = eaSize( &pList->eaPickerButtons ) - 1; it >= 0; --it ) {
			ui_WidgetSetPositionEx( pList->eaPickerButtons[ it ], xIt, 2, 0, 0, UITopRight );
			xIt += pList->eaPickerButtons[ it ]->width;
		}
	} else {
		eaClearEx( &pList->eaPickerButtons, ui_WidgetQueueFree );
	}

	if( g_ListCellSize ) {
		pList->fGridCellWidth = pList->fGridCellHeight = g_ListCellSize;
	}
}

float ui_ListDrawTextInBox(UIDirection eAlignment, UI_MY_ARGS, F32 z, const char *pchText)
{
	S32 iFlags = CENTER_Y;
	F32 fTextWidth = -1;
	CBox box;

	BuildCBox(&box, x, y, w, h);

	gfxfont_Dimensions(g_font_Active, scale, scale, pchText, UTF8GetLength(pchText), &fTextWidth, NULL, NULL, true);

	if (eAlignment == UIRight)
		x += w - (fTextWidth + UI_HSTEP_SC);
	else if (eAlignment == UINoDirection)
	{
		x += w / 2;
		iFlags |= CENTER_X;
	}
	else
		x += UI_HSTEP_SC;

	clipperPushRestrict(&box);
	gfxfont_Printf(x, y + h / 2, z, scale, scale, iFlags, "%s", pchText);
	clipperPop();

	return fTextWidth;
}

void ui_ListDefaultDrawTextFunction(UIList *pList, UIListColumn *pColumn, UI_MY_ARGS, F32 z, CBox *pLogicalBox, S32 iRow, UserData pDrawData)
{
	char *pchText = NULL;
	float textWidth;
	estrStackCreate(&pchText);
	pColumn->contents.cbText(pList, pColumn, iRow, pDrawData, &pchText);
	textWidth = ui_ListDrawTextInBox(pColumn->eAlignment, UI_MY_VALUES, z, pchText);
	if (pColumn->bAutoSize)
		pColumn->fWidth = MAX(pColumn->fWidth, UI_STEP + textWidth / scale);
	estrDestroy(&pchText);
}

void ui_ListDefaultDrawTextureFunction(UIList *pList, UIListColumn *pColumn, UI_MY_ARGS, F32 z, CBox *pLogicalBox, S32 iRow, UserData pDrawData)
{
	char *pchText = NULL;
	AtlasTex *pTex;
	estrStackCreate(&pchText);
	pColumn->contents.cbText(pList, pColumn, iRow, pDrawData, &pchText);
	pTex = (pchText && *pchText) ? atlasLoadTexture(pchText) : NULL;
	if (pTex)
	{
		CBox box = {x, y, x + w, y + h};
		CBox texBox = {x, y, x + pTex->width, y + pTex->height};
		CBoxScaleToFit(&texBox, &box, false);
		CBoxSetCenter(&texBox, x + w / 2, y + h / 2);
		display_sprite_box(pTex, &texBox, z, 0xFFFFFFFF);
	}
	estrDestroy(&pchText);
}

void ui_ListDefaultDrawSMFInBox(UIList *pList, UIListColumn *pColumn, UI_MY_ARGS, F32 z, CBox *pLogicalBox, S32 iRow, UserData pDrawData, const void *pKey, const char *pchText, U8 chAlpha, bool bAutoHeight)
{
	SMFBlock *pBlock = NULL;
	S32 iHeight;
	if (!pColumn->stCache)
		pColumn->stCache = stashTableCreateAddress(32);
	if (!stashFindPointer(pColumn->stCache, eaGet(pList->peaModel, iRow), &pBlock))
	{
		pBlock = smfblock_Create();
		stashAddPointer(pColumn->stCache, pKey, pBlock, false);
	}
	smf_ParseAndDisplay(pBlock, pchText, x, y, z, w, h, false, false, false, NULL, chAlpha, NULL, NULL);
	iHeight = smfblock_GetHeight(pBlock);
	if (bAutoHeight)
	{	
		MAX1(pList->fRowHeight, iHeight);
	}
}

void ui_ListDefaultDrawSMFFunction(UIList *pList, UIListColumn *pColumn, UI_MY_ARGS, F32 z, CBox *pLogicalBox, S32 iRow, UserData pDrawData)
{
	void *pKey = eaGet(pList->peaModel, iRow);
	if (pKey)
	{
		char *pchText = NULL;
		estrStackCreate(&pchText);
		pColumn->contents.cbText(pList, pColumn, iRow, pDrawData, &pchText);
		ui_ListDefaultDrawSMFInBox(pList, pColumn, UI_MY_VALUES, z, pLogicalBox, iRow, pDrawData, pKey, pchText, 255, true);
		estrDestroy(&pchText);
	}
}

void ui_ListDefaultDrawMessageFunction(UIList *pList, UIListColumn *pColumn, UI_MY_ARGS, F32 z, CBox *pLogicalBox, S32 iRow, UserData pDrawData)
{
	S32 iColumn = (S32)(intptr_t)pDrawData;
	ReferenceHandle *pHandle = (ReferenceHandle*)(((char*)eaGet(pList->peaModel,iRow)) + pList->pTable[iColumn].storeoffset);
	Message *pMsg = RefSystem_IsHandleActive(pHandle) ? RefSystem_ReferentFromHandle(pHandle) : NULL;
	const char *pchText = pMsg ? TranslateMessagePtr(pMsg) : NULL;
	float textWidth = 0;
	if (pchText)
		textWidth = ui_ListDrawTextInBox(pColumn->eAlignment, UI_MY_VALUES, z, pchText);
	
	if (pColumn->bAutoSize)
		pColumn->fWidth = MAX(pColumn->fWidth, UI_STEP + textWidth / scale);
}

void ui_ListDefaultDrawFunction(UIList *pList, UIListColumn *pColumn, UI_MY_ARGS, F32 z, CBox *pLogicalBox, S32 iRow, UserData pDrawData)
{
	char *pchText = NULL;
	S32 iColumn = (S32)(intptr_t)pDrawData;
	float textWidth = 0;

	estrStackCreate(&pchText);
	if (TokenWriteText(pList->pTable, iColumn, eaGet(pList->peaModel, iRow), &pchText, true))
		textWidth = ui_ListDrawTextInBox(pColumn->eAlignment, UI_MY_VALUES, z, pchText);
	estrDestroy(&pchText);
	
	if (pColumn->bAutoSize)
		pColumn->fWidth = MAX(pColumn->fWidth, UI_STEP + textWidth / scale);
}

static void ui_ListHeaderFillDrawingDescription( UIList* pList, UIDrawingDescription* desc )
{
	UISkin* skin = UI_GET_SKIN( pList );
		
	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName = skin->astrListHeaderStyle;
		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", descName )) {
			desc->textureAssemblyName = descName;
		} else {
			desc->styleBorderName = descName;
		}
	} else {
		desc->textureAssemblyNameUsingLegacyColor = "Default_Capsule_Filled";
	}
}

static F32 ListDrawColumnHeadersInternal(UIList *pList, UIList *pRootList, bool bLockedOnly, F32 fStartX, F32 fRootStartX, F32 fEndX, F32 fStartY, F32 fScale, F32 fZ, UIStyleFont *pFont, Color headerColor)
{
	UIDrawingDescription desc = { 0 };
	F32 fHeaderHeight = UI_FSCALE( ui_ListGetHeaderHeight( pList, false ));
	F32 fOrigX = fStartX;
	S32 i;
	ui_ListHeaderFillDrawingDescription( pList, &desc );

	if (!fHeaderHeight)
		return fStartY;

	// Blank out area under header if have background color
	if (pRootList->bUseBackgroundColor)
	{
		CBox box = { fRootStartX, fStartY, fEndX, fStartY + fHeaderHeight };
		clipperPushRestrict(&box);
		display_sprite_box((g_ui_Tex.white), &box, fZ + UI_LIST_Z_HEADER_BACKGROUND, RGBAFromColor(pRootList->backgroundColor));
		clipperPop();
	}

	if (pList->pParentList)
		headerColor = ColorLighten(headerColor, 32);

	for (i = 0; i < eaSize(&pList->eaColumns) && (!bLockedOnly || i < pList->iNumLockedColumns); i++)
	{
		UIListColumn *pColumn = pList->eaColumns[i];
		F32 fColWidth = UI_FSCALE(pColumn->fWidth);
		
		if (pColumn->bHidden)
			continue;

		if (IsLastColumnIndex(pList, i))
			fColWidth = MAX(fColWidth, (fEndX - fStartX));
		
		{
			CBox box;
			const char* titleString = ui_ListColumnGetTitle( pColumn );
			BuildCBox(&box, fStartX, fStartY, fColWidth, fHeaderHeight);
			clipperPushRestrict(&box);
			ui_DrawingDescriptionDraw( &desc, &box, fScale, fZ + UI_LIST_Z_HEADER_BACKGROUND, 255, headerColor, ColorBlack );

			if (titleString)
			{
				float textWidth;
				ui_StyleFontUse(pFont, true, UI_WIDGET(pList)->state);
				textWidth = ui_ListDrawTextInBox(pColumn->eHeaderAlignment, fStartX, fStartY, fColWidth, fHeaderHeight, fScale, fZ + UI_LIST_Z_HEADER_TEXT, titleString);
				if (pList->pSortColumn == pColumn && pColumn->eSort > UISortNone)
				{
					AtlasTex *pTex = (pColumn->eSort == UISortDescending) ? (g_ui_Tex.arrowSmallDown) : (g_ui_Tex.arrowSmallUp);
					display_sprite(pTex, box.hx - (pTex->width + UI_HSTEP) * fScale, (box.ly + box.hy) / 2 - pTex->width * fScale / 2, fZ + UI_LIST_Z_HEADER_TEXT, fScale, fScale, RGBAFromColor(ColorLighten(headerColor, 128)));
				}

				if (pColumn->bAutoSize)
				{
					pColumn->fWidth = MAX(pColumn->fWidth, textWidth / fScale + UI_DSTEP);
				}
			}
			clipperPop();
		}
		fStartX += fColWidth;
	}

	fOrigX += UI_FSCALE(pList->fSubListIndent);
	fStartY += fHeaderHeight;

	// If grid and this is a sublist, draw grid line to left of header
	if (pRootList->bDrawGrid && (pList != pRootList))
		gfxDrawLine(fRootStartX, fStartY - fScale, fZ + UI_LIST_Z_HEADER_TEXT, fOrigX, fStartY - fScale, s_ColumnBorderColor);

	for (i = 0; i < eaSize(&pList->eaSubLists); i++)
	{
		if (bLockedOnly)
		{
			// With locked headers adjusted end X based on number of locked columns
			int j;
			F32 fLockWidth = pList->fSubListIndent;
			for (j = 0; j < pList->eaSubLists[i]->iNumLockedColumns; j++)
				fLockWidth += pList->eaSubLists[i]->eaColumns[j]->fWidth;
			fEndX = fRootStartX + fLockWidth * fScale;
		}
		fStartY = ListDrawColumnHeadersInternal(pList->eaSubLists[i], pRootList, bLockedOnly, fOrigX, fRootStartX, fEndX, fStartY, fScale, fZ, pFont, headerColor);
	}

	return fStartY;
}

void ui_ListDrawColumnHeaders(UIList *pList, bool bLockedOnly, UI_MY_ARGS, F32 z)
{
	UIStyleFont *pFont = NULL;
	Color headerColor;
	if (UI_GET_SKIN(pList))
	{
		pFont = ui_ListHeaderGetFont( pList );
		headerColor = UI_GET_SKIN(pList)->titlebar[0];
		if (bLockedOnly)
			headerColor = ColorDarken(headerColor, 32);
	}
	else
		headerColor = UI_WIDGET(pList)->color[2];

	if (bLockedOnly) {
		// With locked headers, adjust the width, which affects the end X
		int i;
		F32 fLockWidth = 0.0;
		for (i = 0; i < pList->iNumLockedColumns; i++)
			fLockWidth += pList->eaColumns[i]->fWidth;
		w = fLockWidth * scale;
	}

	ListDrawColumnHeadersInternal(pList, pList, bLockedOnly, x, x, x + w, y, scale, z, pFont, headerColor);
}

S32 ui_ListColumnGetParseTableIndex(UIList *pList, UIListColumn *pColumn)
{
	if (pColumn->eType == UIListPTIndex)
		return pColumn->contents.iTableIndex;
	else if (pColumn->eType == UIListPTName || pColumn->eType == UIListPTMessage)
	{
		if (pList->pTable != pColumn->pLastTable)
		{
			S32 j;
			FORALL_PARSETABLE(pList->pTable, j)
			{
				if (!stricmp(pList->pTable[j].name, pColumn->contents.pchTableName))
				{
					pColumn->iParseIndexCache = j;
					pColumn->pLastTable = pList->pTable;
					break;
				}
			}
		}
		return pColumn->iParseIndexCache;
	}
	else
	{
		devassertmsg(false, "List column has invalid type to make into a parse table column");
		return -1;
	}
}

static AtlasTex* ui_ListColumnGetCheckTex( UIList* pList, UIListColumn* pColumn, bool bSelected, bool bHover )
{
	UISkin* skin = UI_GET_SKIN( pList );

	if( skin->bUseTextureAssemblies || skin->bUseStyleBorders ) {
		if( bSelected ) {
			return atlasLoadTexture( skin->pchCheckBoxChecked );
		} else if( bHover ) {
			return atlasLoadTexture( skin->pchCheckBoxHighlight );
		} else {
			return atlasLoadTexture( skin->pchCheckBoxUnchecked );
		}
	} else {
		if( bSelected ) {
			return atlasLoadTexture( "eui_tickybox_checked_8x8" );
		} else {
			return atlasLoadTexture( "eui_tickybox_unchecked_8x8" );
		}
	}
}

void ui_ListDrawCell(UIList *pList, UIListColumn *pColumn, S32 iRow, UI_MY_ARGS, F32 z, CBox *pLogicalBox, bool bSelected)
{
	UICellDrawFunc cbDraw = ui_ListDefaultDrawFunction;
	UserData pDrawData = pColumn->pDrawData;
	S32 j;

	bSelected |= ui_ListIsSelected(pList, pColumn, iRow);

	switch (pColumn->eType)
	{
	case UIListPTName:
		if (pList->pTable != pColumn->pLastTable)
		{
			FORALL_PARSETABLE(pList->pTable, j)
			{
				if (!stricmp(pList->pTable[j].name, pColumn->contents.pchTableName))
				{
					pColumn->iParseIndexCache = j;
					pColumn->pLastTable = pList->pTable;
					break;
				}
			}
		}
		pDrawData = (UserData)(intptr_t)pColumn->iParseIndexCache;
		break;
	case UIListPTMessage:
		if (pList->pTable != pColumn->pLastTable)
		{
			FORALL_PARSETABLE(pList->pTable, j)
			{
				if (!stricmp(pList->pTable[j].name, pColumn->contents.pchTableName))
				{
					pColumn->iParseIndexCache = j;
					pColumn->pLastTable = pList->pTable;
					break;
				}
			}
		}
		pDrawData = (UserData)(intptr_t)pColumn->iParseIndexCache;
		cbDraw = ui_ListDefaultDrawMessageFunction;
		break;
	case UIListPTIndex:
		pDrawData = (UserData)(intptr_t)pColumn->contents.iTableIndex;
		break;
	case UIListCallback:
		cbDraw = pColumn->contents.cbDraw;
		break;
	case UIListTextCallback:
		cbDraw = ui_ListDefaultDrawTextFunction;
		break;
	case UIListTextureNameCallback:
		cbDraw = ui_ListDefaultDrawTextureFunction;
		break;
	case UIListSMFCallback:
		cbDraw = ui_ListDefaultDrawSMFFunction;
		break;
	}

	if (cbDraw)
	{
		CBox box = {x, y, x + w, y + h};
		bool bHover = ui_ListIsHovering( pList, NULL, iRow );
		bool bDrawHover = pList->bDrawHover && bHover;
		bool bDrawSelection = pList->bDrawSelection && bSelected && (!pList->bDrawHover || pList->hoverRow == -1);
		UIStyleFont *pFont = ui_ListItemGetFont( pList, bDrawSelection, bDrawHover );

		if (pColumn->bShowCheckBox)
		{
			AtlasTex* pTex = ui_ListColumnGetCheckTex( pList, pColumn, bSelected, bHover );
			display_sprite(pTex, x + UI_HSTEP_SC, floorf(y + h / 2.f - pTex->height * scale / 2.f), z + UI_LIST_Z_DRAW, scale, scale, 0xFFFFFFFF);
			x += UI_HSTEP_SC + pTex->width * scale;
		}

		ui_StyleFontUse(pFont, bDrawSelection, UI_WIDGET(pList)->state);
		clipperPushRestrict(&box);
		cbDraw(pList, pColumn, x, y, w, h, scale, z + UI_LIST_Z_DRAW, pLogicalBox, iRow, pDrawData);
		clipperPop();
		if ( bDrawSelection || bDrawHover )
		{
			UIDrawingDescription desc = { 0 };
			Color c = UI_GET_SKIN(pList)->background[1];
			ui_ListSelectedItemFillDrawingDescription( pList, &desc );
			ui_DrawingDescriptionDraw( &desc, &box, scale, z + UI_LIST_Z_SELECTION, 255, c, ColorBlack );
		}
	}
}

F32 ui_ListGetOffset(UIList *pList)
{
	F32 fOffset = 0;
	for (pList = pList->pParentList; pList; pList = pList->pParentList)
		fOffset += pList->fSubListIndent;
	return fOffset;
}

F32 ui_ListGetScrollPos(UIList *pList)
{
	while(pList->pParentList)
		pList = pList->pParentList;
	return UI_WIDGET(pList)->sb->xpos;
}

F32 ui_ListDrawRow(UIList *pList, bool bLockedOnly, S32 iRow, F32 fTopY, F32 fStartX, F32 fEndX, F32 fStartY, F32 fEndY, F32 fScale, F32 fZ, F32 fLogicalOffsetY)
{
	bool bSelected = ui_ListIsSelected(pList, NULL, iRow);
	bool bHighlightSide = bSelected;
	F32 fHeight = pList->fRowHeight * fScale;
	F32 fWidth = 0;
	S32 i;
	if (fStartY + fHeight >= fTopY)
	{
		if (pList->bUseBackgroundColor)
		{
			CBox backgroundBox = {fStartX, fStartY, fEndX, fStartY + fHeight};
			display_sprite_box( g_ui_Tex.white, &backgroundBox, fZ, RGBAFromColor( pList->backgroundColor ));
		}
		
		if( pList->bLastRowHasSingleColumn && iRow == eaSize( pList->peaModel ) - 1 ) {
			F32 fOffsetX = ui_ListGetOffset(pList) * fScale;
			UIListColumn *pColumn = pList->eaColumns[0];
			F32 fColumnWidth = fEndX;
			CBox logicalBox;

			BuildCBox(&logicalBox, (fOffsetX + fWidth) / fScale, (fStartY - fTopY + fLogicalOffsetY) / fScale, fColumnWidth / fScale, pList->fRowHeight);
			ui_ListDrawCell(pList, pColumn, iRow, fStartX + fWidth, fStartY, fColumnWidth, fHeight, fScale, fZ, &logicalBox, bSelected);
			fWidth += fColumnWidth;
		} else {
			for (i = 0; (i < eaSize(&pList->eaColumns)) && (fStartX + fWidth < fEndX) && (!bLockedOnly || i < pList->iNumLockedColumns); i++)
			{
				F32 fOffsetX = ui_ListGetOffset(pList) * fScale;
				if (bLockedOnly) 
					fOffsetX += ui_ListGetScrollPos(pList);
				for (i = 0; (i < eaSize(&pList->eaColumns)) && (fStartX + fWidth < fEndX) && (!bLockedOnly || i < pList->iNumLockedColumns); i++)
				{
					UIListColumn *pColumn = pList->eaColumns[i];
					F32 fColumnWidth = pColumn->fWidth * fScale;
					CBox logicalBox;

					if (pColumn->bHidden)
						continue;
					if (IsLastColumnIndex(pList, i))
						fColumnWidth = MAX(fColumnWidth, fEndX - (fStartX + fWidth));
					BuildCBox(&logicalBox, (fOffsetX + fWidth) / fScale, (fStartY - fTopY + fLogicalOffsetY) / fScale, fColumnWidth / fScale, pList->fRowHeight);
					ui_ListDrawCell(pList, pColumn, iRow, fStartX + fWidth, fStartY, fColumnWidth, fHeight, fScale, fZ, &logicalBox, bSelected);
					fWidth += fColumnWidth;
					if (i != eaSize(&pList->eaColumns) - 1 && pList->bDrawGrid)
						gfxDrawLine(fStartX + fWidth, fStartY, fZ + UI_LIST_Z_LINES, fStartX + fWidth, fStartY + fHeight, s_ColumnBorderColor);
				}
				if (pList->bDrawGrid)
				{
					F32 fGridStartX = fStartX;
					if (eaSize(&pList->eaSubLists) && eaSize(&pList->eaColumns))
						fGridStartX += (pList->eaColumns[0]->fWidth < pList->fSubListIndent ? pList->eaColumns[0]->fWidth * fScale : pList->fSubListIndent * fScale);
					gfxDrawLine(fGridStartX, fStartY + fHeight - 1, fZ + UI_LIST_Z_LINES, fStartX + fWidth, fStartY + fHeight - 1, s_ColumnBorderColor);
				}
			}
		}
	}

	if (bSelected || ui_ListSelectionIsSelected(ui_ListGetSelectionState(pList), pList, eaGet(&pList->eaColumns, 0), iRow))
		bHighlightSide = false;

	if (pList->iDraggingRow >= 0 && pList->iDragToRow == iRow)
		gfxDrawLine(fStartX, fStartY, fZ + UI_LIST_Z_LINES, fEndX, fStartY, ColorBlack);
	fStartY += fHeight;
	if (eaSize(&pList->eaSubLists))
	{
		F32 fOrigY = fStartY;
		F32 fIndent = pList->fSubListIndent * fScale;
		S32 iList;
		bool bAllHaveLockedCols = true;
		for (iList = 0; iList < eaSize(&pList->eaSubLists); iList++)
		{
			F32 fSubEndX = fEndX;
			F32 fSubOrigY = fStartY;
			ui_ListSetSubListModelFromRow(pList, pList->eaSubLists[iList], iRow);
			if (bLockedOnly) 
			{
				// Find new fSubEndX based on locked columns
				F32 fLockWidth = 0.0;
				int j;
				for (j = 0; j < pList->eaSubLists[iList]->iNumLockedColumns; j++)
					fLockWidth += pList->eaSubLists[iList]->eaColumns[j]->fWidth;
				fSubEndX = fStartX + fLockWidth*fScale;
				if (!pList->eaSubLists[iList]->iNumLockedColumns)
					bAllHaveLockedCols = false;
			}
			fStartY = ui_ListDrawFromRow(pList->eaSubLists[iList], bLockedOnly, 0, fTopY, fStartX + fIndent, fSubEndX, fStartY, fEndY, fScale, fZ, fLogicalOffsetY);
			if ((fStartY >= fTopY) && (!bLockedOnly || pList->eaSubLists[iList]->iNumLockedColumns > 0))
			{
				if (pList->bUseBackgroundColor)
				{
					// Fill in indent space to the left of the sublist with parent list background color
					CBox backgroundBox = {fStartX, fSubOrigY, fStartX + fIndent, fStartY};
					AtlasTex *pWhite = (g_ui_Tex.white);
					display_sprite_box(pWhite, &backgroundBox, fZ, RGBAFromColor(pList->backgroundColor));
				}
				// Sublists have a line on the left edge of the sublist even if no grid
				gfxDrawLine(fStartX + fIndent, fSubOrigY, fZ + UI_LIST_Z_LINES, fStartX + fIndent, fStartY - 1, s_ColumnBorderColor);
			}

		}
		if (bHighlightSide && fStartY > fOrigY && pList->bDrawSelection && fStartY >= fTopY)
		{
			CBox selectionBox = {fStartX, fOrigY, fStartX + fIndent, fStartY};
			Color c = UI_GET_SKIN(pList)->background[1];
			AtlasTex *pWhite = (g_ui_Tex.white);
			display_sprite_box(pWhite, &selectionBox, fZ + UI_LIST_Z_SELECTION, RGBAFromColor(c));
		}
		// If there are sublists and no grid, draw a line above the sublists, under the main list
		if (fOrigY >= fTopY && !pList->bDrawGrid && (!bLockedOnly || bAllHaveLockedCols))
			gfxDrawLine(fStartX + fIndent, fOrigY, fZ + UI_LIST_Z_LINES, fStartX + fWidth, fOrigY, s_ColumnBorderColor);
		// If there are sublists and a grid, draw line at the bottom of the last sublist
		if (pList->bDrawGrid && (!bLockedOnly || bAllHaveLockedCols))
			gfxDrawLine(fStartX, fStartY - 1, fZ + UI_LIST_Z_LINES, fStartX + fIndent, fStartY - 1, s_ColumnBorderColor);
	}
	return fStartY;
}

void ui_ListDrawGrid(UIList* pList, F32 fTopY, F32 fStartX, F32 fEndX, F32 fStartY, F32 fEndY, F32 fScale, F32 fZ )
{
	float cellWidth = pList->fGridCellWidth * fScale;
	float cellHeight = pList->fGridCellHeight * fScale;
	float x = fStartX;
	float y = fStartY;
	int it;
	UIListColumn* pGridColumn = eaGet( &pList->eaColumns, pList->iGridColumn );

	if( pGridColumn ) {
		for( it = 0; it < eaSize( pList->peaModel ); ++it ) {
			CBox cellBox;
			cellBox.lx = x;
			cellBox.ly = y;
			cellBox.hx = x + cellWidth;
			cellBox.hy = y + cellHeight;

			if( cellBox.hy >= fTopY && cellBox.ly <= fEndY ) {
				ui_ListDrawCell( pList, pGridColumn, it,
								 cellBox.lx, cellBox.ly, cellBox.hx - cellBox.lx, cellBox.hy - cellBox.ly, fScale, fZ,
								 &cellBox, ui_ListIsSelected( pList, NULL, it ));
			}

			x += cellWidth;
			if( x + cellWidth > fEndX ) {
				x = fStartX;
				y += cellHeight;
			}
		}
	}
}

F32 ui_ListDrawFromRow(UIList *pList, bool bLockedOnly, S32 iRow, F32 fTopY, F32 fStartX, F32 fEndX, F32 fStartY, F32 fEndY, F32 fScale, F32 fZ, F32 fLogicalOffsetY)
{
	// Sanity check so it doesn't crash if we try to draw from a negative row.
	while (iRow < 0)
	{
		fStartY += pList->fRowHeight * fScale;
		iRow++;
	}

	if (!pList->peaModel)
		return fStartY;

	if (bLockedOnly) {
		// Find new fEndX based on locked columns
		int i;
		F32 fLockWidth = 0.0;

		for (i = 0; i < pList->iNumLockedColumns; i++)
			fLockWidth += pList->eaColumns[i]->fWidth;
		fEndX = fStartX + fLockWidth*fScale;
	}

	for (iRow; iRow < eaSize(pList->peaModel) && fStartY < fEndY; iRow++)
		fStartY = ui_ListDrawRow(pList, bLockedOnly, iRow, fTopY, fStartX, fEndX, fStartY, fEndY, fScale, fZ, fLogicalOffsetY);
	return fStartY;
}

void ui_ListDraw(UIList *pList, UI_PARENT_ARGS)
{
	UISkin* skin = UI_GET_SKIN(pList);
	UI_GET_COORDINATES(pList);
	F32 fHeaderHeight = ui_ListGetHeaderHeight(pList, true) * scale;
	F32 fDataHeight = 0;
	F32 fTotalWidth = 0;
	S32 iStartRow = 0;
	F32 fStartY = 0.f;
	Color borderColor;
	UIDrawingDescription insideDesc = { 0 };
	UIDrawingDescription outsideDesc = { 0 };
	ui_ListInsideFillDrawingDescription( pList, &insideDesc );
	ui_ListOutsideFillDrawingDescription( pList, &outsideDesc );

	if (UI_GET_SKIN(pList))
		borderColor = UI_GET_SKIN(pList)->thinBorder[0];
	else
		borderColor = ColorBlack;

	BuildCBox(&box, x, y, w, h);
	ui_DrawingDescriptionDraw( &outsideDesc, &box, scale, z, 255, pList->backgroundColor, borderColor );
	ui_DrawingDescriptionInnerBoxCoords( &outsideDesc, &x, &y, &w, &h, scale );
	
	switch( pList->eDisplayMode ) {
		xcase UIListRows: {
			fDataHeight = ui_ListGetTotalHeight(pList) * scale;
			fTotalWidth = ui_ListGetTotalWidth(pList) * scale;
			if (UI_WIDGET(pList)->sb->scrollY && (!pList->bIsComboBoxDropdown || h < fDataHeight))
			{
				w -= ui_ScrollbarWidth(UI_WIDGET(pList)->sb) * scale;
				if (UI_WIDGET(pList)->sb->scrollX && w < fTotalWidth)
					h -= ui_ScrollbarHeight(UI_WIDGET(pList)->sb) * scale;

			}
			else if (UI_WIDGET(pList)->sb->scrollX)
			{
				h -= ui_ScrollbarHeight(UI_WIDGET(pList)->sb) * scale;
				if (UI_WIDGET(pList)->sb->scrollY && (!pList->bIsComboBoxDropdown || h < fDataHeight))
					w -= ui_ScrollbarWidth(UI_WIDGET(pList)->sb) * scale;
			}
		}

		xcase UIListIconGrid: {
			int cellsPerRow;
			int numRows;
			w -= ui_ScrollbarWidth(UI_WIDGET(pList)->sb) * scale;
			
			cellsPerRow = floor( w / (pList->fGridCellWidth * scale) );
			numRows = (cellsPerRow == 0) ? 1 : RoundUpToGranularity( eaSize( pList->peaModel ), cellsPerRow ) / cellsPerRow;

			fTotalWidth = cellsPerRow * pList->fGridCellWidth * scale;
			fDataHeight = numRows * pList->fGridCellHeight * scale;
		}
	}
	MAX1(fTotalWidth, w);

	BuildCBox(&box, x, y, w, h);
	CBoxClipTo(&pBox, &box);

	ui_DrawingDescriptionDraw( &insideDesc, &box, scale, z, 255, pList->backgroundColor, borderColor );
	ui_ScrollbarDraw(UI_WIDGET(pList)->sb, x, y, w, h, z, scale, fTotalWidth, fDataHeight + fHeaderHeight);
	if( !pList->bIsComboBoxDropdown ) {
		w -= skin->iListContentExtraPaddingRight;
	}

	UI_DRAW_EARLY(pList);
	BuildCBox( &pList->lastDrawBox, x, y, w, h );

	if( pList->peaModel ) {
		if( !eaSize( pList->peaModel )) {
			const char* widgetText = ui_WidgetGetText( UI_WIDGET( pList ));
			if( widgetText ) {
				ui_StyleFontUse( ui_ListDefaultGetFont( pList ), false, pList->widget.state );
				gfxfont_Printf(x + w / 2, y + fHeaderHeight + (h - fHeaderHeight) / 2, z, scale, scale, CENTER_XY, "%s", widgetText);
			}
		} else {
			fStartY = (y - UI_WIDGET(pList)->sb->ypos) + fHeaderHeight;
			switch( pList->eDisplayMode ) {
				xcase UIListRows:
					ui_ListDrawFromRow(pList, false, iStartRow, y + fHeaderHeight, (x - UI_WIDGET(pList)->sb->xpos), x + w, fStartY, y + h, scale, z, UI_WIDGET(pList)->sb->ypos);

					// Draw locked columns over regular row data
					if (pList->iNumLockedColumns)
						ui_ListDrawFromRow(pList, true, iStartRow, y + fHeaderHeight, x, x + w, fStartY, y + h, scale, ++g_ui_State.drawZ, UI_WIDGET(pList)->sb->ypos);

				xcase UIListIconGrid:
					ui_ListDrawGrid(pList, y + fHeaderHeight, x, x + w, fStartY, y + h, scale, ++g_ui_State.drawZ );
			}
		}
	}

	{
		F32 fChildX = x - UI_WIDGET(pList)->sb->xpos;
		F32 fChildY = (y - UI_WIDGET(pList)->sb->ypos) + fHeaderHeight;
		ui_WidgetGroupDraw(&UI_WIDGET(pList)->children, fChildX, fChildY, fTotalWidth, fDataHeight, scale);
	}

	// Headers need to use a Z value above all children.
	clipperPushRestrict(&box);
	ui_ListDrawColumnHeaders(pList, false, x - UI_WIDGET(pList)->sb->xpos, y, w, h, scale, ++g_ui_State.drawZ);
	clipperPop();
	// Draw locked columns over regular headers
	if (pList->iNumLockedColumns)
		ui_ListDrawColumnHeaders(pList, true, x, y, w, h, scale, ++g_ui_State.drawZ);

	UI_DRAW_LATE_IF(pList, false);
	ui_WidgetGroupDraw( &pList->eaPickerButtons, UI_MY_VALUES );
}

UIList *ui_ListCreate(ParseTable *pTable, UIModel peaModel, F32 fRowHeight)
{
	UIList *pList = (UIList *)calloc(1, sizeof(UIList));
	ui_ListInitialize(pList, pTable, peaModel, fRowHeight);
	pList->iPrefSet = -1;
	return pList;
}

void ui_ListInitialize(UIList *pList, ParseTable *pTable, UIModel peaModel, F32 fRowHeight)
{
	ui_WidgetInitialize(UI_WIDGET(pList), ui_ListTick, ui_ListDraw, ui_ListFreeInternal, ui_ListInput, NULL);
	UI_WIDGET(pList)->sb = ui_ScrollbarCreate(true, true);
	UI_WIDGET(pList)->sb->alwaysScrollX = false;
	pList->pSelection = calloc(1, sizeof(UIListSelectionState));
	pList->pSelection->pList = pList;
	pList->pSelection->stSelected = stashTableCreateFixedSize(32, sizeof(UIListSelectionObject));
	pList->fRowHeight = pList->fHeaderHeight = fRowHeight;
	pList->fGridCellWidth = 96;
	pList->fGridCellHeight = 96;
	pList->bDrawSelection = true;
	pList->iDraggingRow = -1;
	pList->iSortedIndex = -1;
	pList->hoverRow = -1;
	pList->cbCellClicked = ui_ListCellClickedDefault;
	pList->cbCellContext = ui_ListCellContextDefault;
	pList->cbCellActivated = ui_ListCellActivatedDefault;
	ui_ListSetModel(pList, pTable, peaModel);
}

void ui_ListFreeInternal(UIList *pList)
{
	if(pList->pAutoColumnContextMenu)
		ui_WidgetFreeInternal(UI_WIDGET(pList->pAutoColumnContextMenu));
	if(pList->pAutoColumnContextMenuShowMoreWindow)
		ui_WidgetFreeInternal(UI_WIDGET(pList->pAutoColumnContextMenuShowMoreWindow));
	eaDestroy(&pList->eaAutoColumnContextMenuShowMoreWindowCheckButtons);

	eaDestroyEx(&pList->eaColumns, ui_ListColumnFree);
	ui_WidgetGroupFreeInternal( &pList->eaPickerButtons );
	ui_ListSelectionStateFree(pList->pSelection);
	ui_WidgetGroupFreeInternal((UIWidgetGroup *)&pList->eaSubLists);
	eaiDestroy(&pList->eaiSelectedRowsInternal);
	ui_WidgetFreeInternal(UI_WIDGET(pList));
}

UIListColumn *ui_ListColumnCreate(UIListColumnType eType, const char *pchTitle, intptr_t contents, UserData pDrawData)
{
	UIListColumn *pColumn = (UIListColumn *)calloc(1, sizeof(UIListColumn));
	ui_ListColumnSetTitleString(pColumn, pchTitle);
	ui_ListColumnSetType(pColumn, eType, contents, pDrawData);
	ui_ListColumnSetWidth(pColumn, false, 200);
	ui_ListColumnSetResizable(pColumn, true);
	ui_ListColumnSetAlignment(pColumn, UILeft, UILeft);
	ui_ListColumnSetCanHide(pColumn, true);
	return pColumn;
}

UIListColumn *ui_ListColumnCreateMsg(UIListColumnType eType, const char *pchTitle, intptr_t contents, UserData pDrawData)
{
	UIListColumn *pColumn = ui_ListColumnCreate( eType, NULL, contents, pDrawData);
	ui_ListColumnSetTitleMessage(pColumn, pchTitle);
	return pColumn;
}

void ui_ListColumnFree(UIListColumn *pColumn)
{
	if( !pColumn ) {
		return;
	}
	
	stashTableDestroyEx(pColumn->stCache, NULL, smfblock_Destroy);
	SAFE_FREE(pColumn->pchTitleString_USEACCESSOR);
	REMOVE_HANDLE(pColumn->hTitleMessage_USEACCESSOR);
	free(pColumn);
}

void ui_ListColumnSetResizable(UIListColumn *pColumn, bool bResizable)
{
	pColumn->bResizable = bResizable;
}

void ui_ListColumnSetAlignment(UIListColumn *pColumn, UIDirection eHeaderAlignment, UIDirection eBodyAlignment)
{
	pColumn->eHeaderAlignment = eHeaderAlignment;
	pColumn->eAlignment = eBodyAlignment;
}

void ui_ListColumnSetType(UIListColumn *pColumn, UIListColumnType eType, intptr_t contents, UserData pDrawData)
{
	pColumn->eType = eType;
	switch (eType)
	{
	case UIListPTIndex:
		pColumn->contents.iTableIndex = (S32)contents;
		break;
	case UIListPTName:
	case UIListPTMessage:
		pColumn->contents.pchTableName = (const char *)contents;
		break;
	case UIListCallback:
		pColumn->contents.cbDraw = (void *)contents;
		//pColumn->bSortable = false;
		break;
	case UIListTextCallback:
	case UIListTextureNameCallback:
	case UIListSMFCallback:
		pColumn->contents.cbText = (void *)contents;
		//pColumn->bSortable = false;
		break;
	}
	pColumn->pDrawData = pDrawData;
	pColumn->pLastTable = NULL;  // Force recalc of row using new draw data
}

void ui_ListColumnSetWidth(UIListColumn *pColumn, bool bAutoSize, F32 fWidth)
{
	pColumn->fWidth = fWidth;
	pColumn->bAutoSize = bAutoSize;

	//if(pColumn->iPrefSet!=-1)
	//	PrefStoreFloat(pColumn->iPrefSet, pColumn->pchPrefStr, pColumn->fWidth);
}

void ui_ListColumnSetTitleString(UIListColumn *pColumn, const char *pchTitle)
{
	REMOVE_HANDLE( pColumn->hTitleMessage_USEACCESSOR );
	if( !pchTitle ) {
		StructFreeStringSafe( &pColumn->pchTitleString_USEACCESSOR );
	} else {
		StructCopyString( &pColumn->pchTitleString_USEACCESSOR, pchTitle );
	}
}

void ui_ListColumnSetTitleMessage(UIListColumn *pColumn, const char *pchTitle)
{
	StructFreeStringSafe( &pColumn->pchTitleString_USEACCESSOR );
	if( !pchTitle ) {
		REMOVE_HANDLE( pColumn->hTitleMessage_USEACCESSOR );
	} else {
		SET_HANDLE_FROM_STRING( "Message", pchTitle, pColumn->hTitleMessage_USEACCESSOR );
	}
}

const char* ui_ListColumnGetTitle(SA_PARAM_NN_VALID UIListColumn *pColumn)
{
	if( IS_HANDLE_ACTIVE( pColumn->hTitleMessage_USEACCESSOR )) {
		return TranslateMessageRef( pColumn->hTitleMessage_USEACCESSOR );
	} else {
		return pColumn->pchTitleString_USEACCESSOR;
	}
}

void ui_ListColumnSetHidden(UIListColumn *pColumn, bool bHidden)
{
	pColumn->bHidden = pColumn->bCanHide ? bHidden : false;
}

void ui_ListColumnSetSortable(UIListColumn *pColumn, bool bSortable)
{
	//if (!(pColumn->eType == UIListPTName || pColumn->eType == UIListPTIndex || pColumn->eType == UIListPTMessage))
	//	bSortable = false;
	pColumn->bSortable = bSortable;
}

void ui_ListAppendColumn(UIList *pList, UIListColumn *pColumn)
{
	const char* titleString = ui_ListColumnGetTitle(pColumn);
	if(pList->iPrefSet>-1 && !nullStr(titleString))
		pColumn->fWidth = PrefGetFloat(pList->iPrefSet, STACK_SPRINTF("%s.%s", pList->widget.name, titleString), pColumn->fWidth);

	eaPush(&pList->eaColumns, pColumn);
	UpdateLastColumn(pList);
}

void ui_ListRemoveColumn(UIList *pList, UIListColumn *pColumn)
{
	if (eaFindAndRemove(&pList->eaColumns, pColumn) >= 0)
		ui_ListSelectionUnselectIf(ui_ListGetSelectionState(pList), ui_ListSelectionFilterColumnMatches, (UserData)pColumn);
	UpdateLastColumn(pList);
}

void ui_ListAddGridIconColumn(SA_PARAM_NN_VALID UIList* pList, SA_PARAM_NN_VALID UIListColumn* pColumn)
{
	eaPush(&pList->eaColumns, pColumn);
	pList->iGridColumn = eaSize( &pList->eaColumns) - 1;
	pColumn->bCanHide = true;
	pColumn->bHidden = true;
}

void ui_ListSetRowsDraggable(UIList *pList, bool bDraggable)
{
	pList->bRowsDraggable = bDraggable;
}

void ui_ListSetSortOnlyOnUIChange(SA_PARAM_NN_VALID UIList *pList, bool bSortOnlyOnUIChange)
{
	pList->bSortOnlyOnUIChange = bSortOnlyOnUIChange;
}

void ui_ListUnsort(UIList *pList)
{
	if (pList->pSortColumn)
		pList->pSortColumn->eSort = UISortNone;
	pList->pSortColumn = NULL;
	pList->eSort = UISortNone;
	pList->iSortedIndex = -1;

	if(pList->bSortOnlyOnUIChange)
		ui_ListSort(pList);
}

void ui_ListSetModel(UIList *pList, ParseTable *pTable, UIModel peaModel)
{
	if (pTable != pList->pTable)
	{
		ui_ListSelectionStateClear(ui_ListGetSelectionState(pList));
		ui_ListUnsort(pList);
	}
	pList->pTable = pTable;
	pList->peaModel = peaModel;
}

S32 ui_ListGetSelectedRow(UIList *pList)
{
	if (pList->peaModel)
	{
		UIListSelectionState *pState = ui_ListGetSelectionState(pList);
		return ui_ListSelectionStateGetSelectedRow(pState);
	}
	else
		return -1;
}

void *ui_ListGetSelectedObject(UIList *pList)
{
	return pList->peaModel ? eaGet(pList->peaModel, ui_ListGetSelectedRow(pList)) : NULL;
}

void ui_ListGetSelectedRowBox(SA_PARAM_NN_VALID UIList* pList, CBox* box )
{
	int selectedRow = ui_ListGetSelectedRow( pList );

	box->lx = box->ly = box->hx = box->hy = 0;
	if( selectedRow >= 0 ) {
		switch( pList->eDisplayMode ) {
			xcase UIListRows: {
				float fHeaderHeight = ui_ListGetHeaderHeight( pList, true );
				BuildCBox( box,
						   pList->lastDrawBox.lx,
						   pList->lastDrawBox.ly + fHeaderHeight + pList->fRowHeight * selectedRow - pList->widget.sb->ypos,
						   CBoxWidth( &pList->lastDrawBox ),
						   pList->fRowHeight );
			}

			xcase UIListIconGrid: {
				int cellsPerRow = floor( CBoxWidth( &pList->lastDrawBox ) / pList->fGridCellWidth );
				int row;
				int col;

				if( cellsPerRow < 1 ) {
					cellsPerRow = 1;
				}
				row = selectedRow / cellsPerRow;
				col = selectedRow % cellsPerRow;

				BuildCBox( box,
						   col * pList->fGridCellWidth + pList->lastDrawBox.lx,
						   row * pList->fGridCellHeight + pList->lastDrawBox.ly - pList->widget.sb->ypos,
						   pList->fGridCellWidth, pList->fGridCellHeight );
			}
		}
	}
}

void ui_ListSetSelectedRowColEx(UIList *pList, S32 iRow, S32 iCol, bool bExtendSelection, bool bInvertSelection)
{
	UIListSelectionState *pState = ui_ListGetSelectionState(pList);
	UIListColumn *pColumn = eaGet(&pList->eaColumns, iCol);

	if (!pList->bMultiSelect)
		bExtendSelection = false;

	if (!pList->bColumnSelect)
		pColumn = NULL;

	if (bExtendSelection)
	{
		S32 iLastSelected = ui_ListSelectionStateGetSelectedRow(pState);
		if (!bInvertSelection)
			ui_ListSelectionStateClear(pState);
		if (iLastSelected != -1)
		{
			S32 j;
			if (iLastSelected < iRow)
			{
				for (j = iLastSelected; j <= iRow; ++j)
					ui_ListSelectionStateSelect(pState, pList, pColumn, j);
			}
			else
			{
				for (j = iLastSelected; j >= iRow; --j)
					ui_ListSelectionStateSelect(pState, pList, pColumn, j);
			}
		}
		else
		{
			ui_ListSelectionStateSelect(pState, pList, pColumn, iRow);
		}
	}
	else if (bInvertSelection)
	{
		if (!pList->bMultiSelect)
			ui_ListSelectionStateClear(pState);
		ui_ListSelectionStateToggle(pState, pList, pColumn, iRow);
	}
	else
	{
		ui_ListClearEverySelection(pList);
		ui_ListSelectionStateSelect(pState, pList, pColumn, iRow);
	}
}

void ui_ListClearSelected(UIList *pList)
{
	ui_ListSelectionStateClear(ui_ListGetSelectionState(pList));
}

void ui_ListClearEverySelection(UIList *pList)
{
	ui_ListClearSelected(pList);
}

void ui_ListClearInactiveSelections(UIList *pList)
{
	ui_ListSelectionStateClearInactive(ui_ListGetSelectionState(pList), pList);
}

static void ListClearChildSelections(UIList *pList)
{
	S32 i;
	for (i = 0; i < eaSize(&pList->eaSubLists); i++)
		ListClearChildSelections(pList->eaSubLists[i]);
}

static bool UnselectIfNotList(UIListSelectionObject *pSelect, UIList *pList)
{
	return (pSelect->pList == pList) ? false : true;
}

void ui_ListClearParentChildSiblingSelections(UIList *pList)
{
	ui_ListSelectionUnselectIf(ui_ListGetSelectionState(pList), UnselectIfNotList, pList);
}

void ui_ListSetSelectedRowCol(UIList *pList, S32 iRow, S32 iCol)
{
	ui_ListSetSelectedRowColEx(pList, iRow, iCol, false, false);
}

void ui_ListSetSelectedRow(UIList *pList, S32 iRow)
{
	ui_ListSetSelectedRowColEx(pList, iRow, -1, false, false);
}

void ui_ListSetSelectedRowColExAndCallback(UIList *pList, S32 iRow, S32 iCol, bool bExtendSelection, bool bInvertSelection)
{
	ui_ListSetSelectedRowColEx(pList, iRow, iCol, bExtendSelection, bInvertSelection);
	ui_ListCallSelectionChangedCallback(pList);
}

static void ListCallSelectionChangedCallbackInternal(UIList *pList)
{
	S32 i;
	if (pList->cbSelected)
		pList->cbSelected(pList, pList->pSelectedData);
	for (i = 0; i < eaSize(&pList->eaSubLists); i++)
		ListCallSelectionChangedCallbackInternal(pList->eaSubLists[i]);
}

void ui_ListCallSelectionChangedCallback(UIList *pList)
{
	ListCallSelectionChangedCallbackInternal(ui_ListGetParent(pList));
}

void ui_ListSetSelectedRowColAndCallback(UIList *pList, S32 iRow, S32 iCol)
{
	ui_ListSetSelectedRowColExAndCallback(pList, iRow, iCol, false, false);
}

void ui_ListSetSelectedRowAndCallback(UIList *pList, S32 iRow)
{
	ui_ListSetSelectedRowColExAndCallback(pList, iRow, -1, false, false);
}

void ui_ListSetSelectedObject(UIList *pList, const void *pObj)
{
	if (pList->peaModel)
		ui_ListSetSelectedRow(pList, eaFind(pList->peaModel, pObj));
}

void ui_ListSetSelectedObjectAndCallback(UIList *pList, const void *pObj)
{
	if (pList->peaModel)
		ui_ListSetSelectedRowAndCallback(pList, eaFind(pList->peaModel, pObj));
}

void ui_ListResetScrollbar(const UIList *pList)
{
	UI_WIDGET(pList)->sb->ypos = UI_WIDGET(pList)->sb->xpos = 0;
}

void ui_ListGetScrollbarPosition(SA_PARAM_NN_VALID const UIList *pList, F32 *xOut, F32 *yOut)
{
	if (xOut)
	{
		*xOut = UI_WIDGET(pList)->sb->xpos;
	}
	if (yOut)
	{
		*yOut = UI_WIDGET(pList)->sb->ypos;
	}
}
void ui_ListSetScrollbar(SA_PARAM_NN_VALID const UIList *pList, F32 x, F32 y)
{
	UI_WIDGET(pList)->sb->ypos = y;
	UI_WIDGET(pList)->sb->xpos = x;
}

bool ui_ListIsRowSelected(UIList *pList, S32 iRow)
{
	return (ea32Find(ui_ListGetSelectedRows(pList), iRow) >= 0);
}

bool ui_ListIsAnyObjectSelected(SA_PARAM_NN_VALID UIList *pList)
{
	if( pList->peaModel ) {
		const S32*const* peaiSelectedRows = ui_ListGetSelectedRows( pList );
		int it;
		for( it = 0; it != eaiSize( peaiSelectedRows ); ++it ) {
			if( eaGet( pList->peaModel, (*peaiSelectedRows)[ it ])) {
				return true;
			}
		}
	}

	return false;
}

const S32 * const *ui_ListGetSelectedRows(UIList *pList)
{
	UIListSelectionState *pState = ui_ListGetSelectionState(pList);
	ui_ListSelectionStateGetSelectedRows(pState, &pList->eaiSelectedRowsInternal);
	return &pList->eaiSelectedRowsInternal;
}

const S32 * const *ui_ListGetSelectedSubRows(UIList *pList)
{
	UIListSelectionState *pState = ui_ListGetSelectionState(pList);
	ui_ListSelectionStateGetSelectedSubRows(pList, &pList->eaiSelectedRowsInternal);
	return &pList->eaiSelectedRowsInternal;
}

const S32 * const *ui_ListGetSelectedSubRowsSorted(UIList *pList)
{
	UIListSelectionState *pState = ui_ListGetSelectionState(pList);
	ui_ListSelectionStateGetSelectedSubRows(pList, &pList->eaiSelectedRowsInternal);
	eaiQSort(pList->eaiSelectedRowsInternal, intCmp);
	return &pList->eaiSelectedRowsInternal;
}

void ui_ListSetSelectedRows(UIList *pList, S32 **peaiRows)
{
	UIListSelectionState *pState = ui_ListGetSelectionState(pList);
	if(!pList->bMultiSelect && eaiSize(peaiRows) > 1)
	{
		S32 first = eaiGet(peaiRows, 0);
		eaiClearFast(peaiRows);
		eaiPush(peaiRows, first);
	}

	if(eaiSize(peaiRows))
	{
		S32 i;
		ui_ListSelectionStateClear(pState);
		for (i = 0; i < eaiSize(peaiRows); i++)
			ui_ListSelectionStateSelect(pState, pList, NULL, eaiGet(peaiRows, i));
	}
}

//Since this variable stores indices, it needs to update those indices if a row is deleted
void ui_ListNotifySelectionOfRowDeletion(UIList *pList, S32 iRow)
{
	int i;
	ui_ListDeselectRow(pList, iRow);
	for (i = 0; i < eaiSize(&pList->eaiSelectedRowsInternal); i++)
	{
		if (pList->eaiSelectedRowsInternal[i] > iRow)
		{
			pList->eaiSelectedRowsInternal[i]--;
		}
	}
}

void ui_ListSelectOrDeselectRowEx(UIList *pList, S32 iRow, bool bExtendSelection, bool bInvertSelection)
{
	UIListSelectionState *pState = ui_ListGetSelectionState(pList);

	if (!pList->bMultiSelect)
	{
		bExtendSelection = false;
	}

	if (bExtendSelection)
	{
		S32 iLastSelected = ui_ListSelectionStateGetSelectedRow(pState);
		if (!bInvertSelection)
		{
			ui_ListClearSelectedRows(pList);
			ui_ListSelectionStateClear(pState);
		}
		if (iLastSelected != -1)
		{
			S32 j;
			if (iLastSelected < iRow)
			{
				for (j = iLastSelected; j <= iRow; ++j)
				{
					ui_ListSelectRow(pList, j);
				}
			}
			else
			{
				for (j = iLastSelected; j >= iRow; --j)
				{
					ui_ListSelectRow(pList, j);
				}
			}
		}
		else
		{
			ui_ListSelectRow(pList, iRow);
		}
	}
	else if (bInvertSelection)
	{
		if (!pList->bMultiSelect)
		{
			ui_ListClearSelectedRows(pList);
			ui_ListSelectionStateClear(pState);
		}
		ui_ListSelectRowToggle(pList, iRow);
	}
	else
	{
		ui_ListClearSelectedRows(pList);
		ui_ListSelectionStateClear(pState);
		ui_ListSelectRow(pList, iRow);
	}
}

void ui_ListSelectRowToggle(UIList *pList, S32 iRow)
{
	if ((ea32Find(&(pList->eaiSelectedRowsInternal), iRow)) >= 0)
	{
		ui_ListDeselectRow(pList, iRow);
	}
	else
	{
		ui_ListSelectRow(pList, iRow);
	}
}

void ui_ListSelectRow(UIList *pList, S32 iRow)
{
	UIListSelectionState *pState = ui_ListGetSelectionState(pList);
	if ((ea32Find(&(pList->eaiSelectedRowsInternal), iRow)) >= 0)
	{
		return;
	}
	if (devassertmsg(pList->bMultiSelect || eaiSize(&(pList->eaiSelectedRowsInternal)) == 0, "pList is not multiselectable"))
	{
		ui_ListSelectionStateSelect(pState, pList, NULL, iRow);
		ea32Push(&pList->eaiSelectedRowsInternal, iRow);
	}
}

void ui_ListDeselectRow(UIList *pList, S32 iRow)
{
	UIListSelectionState *pState = ui_ListGetSelectionState(pList);
	ui_ListSelectionStateUnselect(pState, pList, NULL, iRow);
	ea32FindAndRemove(&pList->eaiSelectedRowsInternal, iRow);
}

void ui_ListClearSelectedRows(UIList *pList)
{
	UIListSelectionState *pState = ui_ListGetSelectionState(pList);
	ui_ListSelectionStateClear(pState);
	ea32Clear(&pList->eaiSelectedRowsInternal);
}

void ui_ListSetSelectedRowsAndCallback(UIList *pList, S32 **peaiRows)
{
	ui_ListSetSelectedRows(pList, peaiRows);
	ui_ListCallSelectionChangedCallback(pList);
}

void ui_ListSetSelectedObjects(UIList *pList, void ***peaObjs)
{
	UIListSelectionState *pState = ui_ListGetSelectionState(pList);
	if (pList->peaModel && devassertmsg(pList->bMultiSelect || eaSize(peaObjs) <= 1, "pList is not multiselectable"))
	{
		S32 i;
		ui_ListSelectionStateClear(pState);

		for (i = 0; i < eaSize(peaObjs); i++)
		{
			S32 iIndex = eaFind(pList->peaModel, (*peaObjs)[i]);
			if (iIndex >= 0)
				ui_ListSelectionStateSelect(pState, pList, NULL, iIndex);
		}
	}
}

void ui_ListSetSelectedObjectsAndCallback(UIList *pList, void ***peaObjs)
{
	ui_ListSetSelectedObjects(pList, peaObjs);
	ui_ListCallSelectionChangedCallback(pList);
}

void ui_ListSetMultiselect(UIList *pList, bool bMultiSelect)
{
	pList->bMultiSelect = bMultiSelect;
	if (!bMultiSelect)
	{
		UIListSelectionState *pState = ui_ListGetSelectionState(pList);
		ui_ListSelectionStateClear(pState);
	}
}

void ui_ListSetAutoColumnContextMenu(SA_PARAM_NN_VALID UIList *pList, bool bAutoColumnContextMenu)
{
	pList->bAutoColumnContextMenu = bAutoColumnContextMenu;
}

void ui_ListSetSelectedCallback(UIList *pList, UIActivationFunc cbSelected, UserData pSelectedData)
{
	pList->cbSelected = cbSelected;
	pList->pSelectedData = pSelectedData;
}

void ui_ListSetActivatedCallback(UIList *pList, UIActivationFunc cbActivated, UserData pActivatedData)
{
	pList->cbActivated = cbActivated;
	pList->pActivatedData = pActivatedData;
}

void ui_ListSetHoverCallback(UIList *pList, UIRowHoverFunc cbRowHover, UserData pHoverData)
{
	pList->cbRowHover = cbRowHover;
	pList->pRowHoverData = pHoverData;
}

void ui_ListSetColumnsSelectable(SA_PARAM_NN_VALID UIList *pList, bool bSelectable)
{
	pList->bColumnSelect = bSelectable;
}

void ui_ListSetContextCallback(UIList *pList, UIActivationFunc cbContext, UserData pContextData)
{
	ui_WidgetSetContextCallback(UI_WIDGET(pList), cbContext, pContextData);
}

void ui_ListSetDragCallback(UIList *pList, UIListDragFunc cbDrag, UserData pDragData)
{
	pList->cbDrag = cbDrag;
	pList->pDragData = pDragData;
}

void ui_ListColumnSetClickedCallback(UIListColumn *pColumn, UIActivationFunc cbClicked, UserData pClickedData)
{
	pColumn->cbClicked = cbClicked;
	pColumn->pClickedData = pClickedData;
}

void ui_ListColumnSetCanHide(SA_PARAM_NN_VALID UIListColumn *pColumn, bool bCanHide)
{
	pColumn->bCanHide = bCanHide;
	if(!bCanHide)
		pColumn->bHidden = false;
}

void ui_ListColumnSetContextCallback(UIListColumn *pColumn, UIActivationFunc cbContext, UserData pContextData)
{
	pColumn->cbContext = cbContext;
	pColumn->pContextData = pContextData;
}

UIModel ui_ListGetModel(const UIList *pList)
{
	return pList->peaModel;
}

void ui_ListScrollToRow(UIList *pList, S32 iRow)
{
	eaiSetSize(&pList->eaiScrollToPath, 1);
	pList->eaiScrollToPath[0] = iRow;
}

void ui_ListScrollToSelection(UIList *pList)
{
	ui_ListGetSelectedPath(pList, &pList->eaiScrollToPath);
}

void ui_ListAddSubList(UIList *pList, UIList *pSubList, F32 fIndent, const char *pchFieldName)
{
	S32 i;
	eaPush(&pList->eaSubLists, pSubList);
	pList->fSubListIndent = fIndent;

	if (pSubList)
	{
		pSubList->pParentList = pList;
		pList->bRowsDraggable = false;

		if (pchFieldName)
		{
			pList->iSubIndex = -1;
			FORALL_PARSETABLE(pList->pTable, i)
			{
				if (pList->pTable[i].name && !stricmp(pList->pTable[i].name, pchFieldName))
				{
					pList->iSubIndex = i;
					pSubList->pTable = pList->pTable[i].subtable;
				}
			}
			if (pList->iSubIndex < 0)
			{
				// If a field name is provided, check that it exists at this time
				// instead of failing later on during the paint call.
				assertmsg(0,"Sub-list must have a valid index");
			}
		}
	}
}

UIListSelectionState *ui_ListGetSelectionState(const UIList *pList)
{
	while (pList->pParentList)
		pList = pList->pParentList;
	return pList->pSelection;
}

bool ui_ListGetSelectedPath(UIList *pList, S32 **peaiPath)
{
	S32 row = ui_ListGetSelectedRow(pList);
	if (row >= 0) {
		eaiPush(peaiPath, row);
	}
	return true;
}

void ui_ListGetSelectedCells(UIList *pList, const UIListSelectionObject ***peaObjects)
{
	UIListSelectionState *pState = ui_ListGetSelectionState(pList);
	StashTableIterator iter;
	StashElement element;
	stashGetIterator(pState->stSelected, &iter);
	while (stashGetNextElement(&iter, &element))
	{
		UIListSelectionObject *pSelect = stashElementGetKey(element);
		if (pSelect->pModel == pList->peaModel && pSelect->pList == pList)
			eaPush(peaObjects, pSelect);
	}

	if (pList->eaSubLists)
	{
		S32 iRow;
		S32 iList;
		for (iRow = 0; iRow < eaSize(pList->peaModel); iRow++)
		{
			ui_ListSetSubListsModelFromRow(pList, iRow);
			for (iList = 0; iList < eaSize(&pList->eaSubLists); iList++)
			{
				ui_ListGetSelectedCells(pList->eaSubLists[iList], peaObjects);
			}
		}
	}
}

void ui_ListCellClickedDefault(UIList *pList, S32 iColumn, S32 iRow, F32 fMouseX, F32 fMouseY, CBox *pBox, UserData pCellData)
{
	ui_ListSetSelectedRowColExAndCallback(pList, iRow, iColumn, inpLevelPeek(INP_SHIFT), inpLevelPeek(INP_CONTROL) || (pList->bMultiSelect && pList->bToggleSelect));
}

void ui_ListCellContextDefault(UIList *pList, S32 iColumn, S32 iRow, F32 fMouseX, F32 fMouseY, CBox *pBox, UserData pCellData)
{
	ui_ListSetSelectedRowColExAndCallback(pList, iRow, iColumn, inpLevelPeek(INP_SHIFT), inpLevelPeek(INP_CONTROL) || (pList->bMultiSelect && pList->bToggleSelect));
}

void ui_ListCellActivatedDefault(UIList *pList, S32 iColumn, S32 iRow, F32 fMouseX, F32 fMouseY, CBox *pBox, UserData pCellData)
{
	ui_ListSetSelectedRowColExAndCallback(pList, iRow, iColumn, inpLevelPeek(INP_SHIFT), inpLevelPeek(INP_CONTROL));
	if (pList->cbActivated)
		pList->cbActivated(pList, pList->pActivatedData);
}

void ui_ListSetCellClickedCallback(UIList *pList, UIListCellActionFunc cbCellClicked, UserData pCellClickedData)
{
	pList->cbCellClicked = cbCellClicked;
	pList->pCellClickedData = pCellClickedData;
}

void ui_ListSetCellContextCallback(UIList *pList, UIListCellActionFunc cbCellContext, UserData pCellContextData)
{
	pList->cbCellContext = cbCellContext;
	pList->pCellContextData = pCellContextData;
}

void ui_ListSetCellActivatedCallback(UIList *pList, UIListCellActionFunc cbCellActivated, UserData pCellActivatedData)
{
	pList->cbCellActivated = cbCellActivated;
	pList->pCellActivatedData = pCellActivatedData;
}

void ui_ListSetCellHoverCallback(UIList *pList, UIListCellActionFunc cbCellHover, UserData pCellHoverData)
{
	pList->cbCellHover = cbCellHover;
	pList->pCellHoverData = pCellHoverData;
}

UIList *ui_ListGetParent(UIList *pList)
{
	while (pList->pParentList)
		pList = pList->pParentList;
	return pList;
}

static void ListFreeSelectionObject(UIListSelectionObject *pObject)
{
	free(pObject);
}

void ui_ListSelectionStateFree(UIListSelectionState *pState)
{
	pState->pList->pSelection = NULL;
	stashTableDestroyEx(pState->stSelected, ListFreeSelectionObject, NULL);
	free(pState);
}

bool ui_ListSelectionIsSelected(const UIListSelectionState *pState, const UIList *pList, const UIListColumn *pColumn, S32 iRow)
{
	UIListSelectionObject selection = {(UIList *)pList, pList->peaModel, iRow, (UIListColumn *)pColumn};
	return stashFindInt(pState->stSelected, &selection, NULL);
}

void ui_ListSelectionStateSelect(UIListSelectionState *pState, UIList *pList, UIListColumn *pColumn, S32 iRow)
{
	UIListSelectionObject *pSelect = calloc(1, sizeof(UIListSelectionObject));
	pSelect->pList = pList;
	pSelect->pModel = pList->peaModel;
	pSelect->pColumn = pColumn;
	pSelect->iRow = iRow;
	stashAddInt(pState->stSelected, pSelect, 1, true);
}

void ui_ListSelectionStateUnselect(UIListSelectionState *pState, UIList *pList, UIListColumn *pColumn, S32 iRow)
{
	StashElement element;
	UIListSelectionObject selection = {pList, pList->peaModel, iRow, pColumn};
	if (stashFindElement(pState->stSelected, &selection, &element))
	{
		UIListSelectionObject *pSelection = stashElementGetKey(element);
		stashRemoveInt(pState->stSelected, pSelection, NULL);
		ListFreeSelectionObject(pSelection);
	}
}

void ui_ListSelectionStateToggle(UIListSelectionState *pState, UIList *pList, UIListColumn *pColumn, S32 iRow)
{
	if (ui_ListSelectionIsSelected(pState, pList, pColumn, iRow))
		ui_ListSelectionStateUnselect(pState, pList, pColumn, iRow);
	else
		ui_ListSelectionStateSelect(pState, pList, pColumn, iRow);
}

S32 ui_ListSelectionStateGetSelectedRow(UIListSelectionState *pState)
{
	StashTableIterator iter;
	StashElement element;
	stashGetIterator(pState->stSelected, &iter);
	while (stashGetNextElement(&iter, &element))
	{
		UIListSelectionObject *pSelect = stashElementGetKey(element);
		int i;
		if (pSelect->pModel == pState->pList->peaModel)
			return pSelect->iRow;
		for (i = 0; i < eaSize(&pState->pList->eaSubLists); i++)
		{

			if (pSelect->pModel == pState->pList->eaSubLists[i]->peaModel)
				return pSelect->iRow;
		}
	}
	return -1;
}

void ui_ListSelectionStateGetSelectedRows(UIListSelectionState *pState, S32 **peaiRows)
{
	StashTableIterator iter;
	StashElement element;
	stashGetIterator(pState->stSelected, &iter);
	eaiClear(peaiRows);
	while (stashGetNextElement(&iter, &element))
	{
		UIListSelectionObject *pSelect = stashElementGetKey(element);
		if (pSelect->pModel == pState->pList->peaModel)
			eaiPush(peaiRows, pSelect->iRow);
	}
}

void ui_ListSelectionStateGetSelectedSubRows(UIList *pList, S32 **peaiRows)
{
	StashTableIterator iter;
	StashElement element;
	UIListSelectionState *pState = ui_ListGetSelectionState(pList);
	stashGetIterator(pState->stSelected, &iter);
	eaiClear(peaiRows);
	while (stashGetNextElement(&iter, &element))
	{
		UIListSelectionObject *pSelect = stashElementGetKey(element);
		if (pSelect->pList == pList)
			eaiPush(peaiRows, pSelect->iRow);
	}
}

bool ui_ListSelectionFilterColumnMatches(UIListSelectionObject *pSelect, UIListColumn *pColumn)
{
	return (pSelect->pColumn == pColumn) ? true : false;
}

bool ui_ListSelectionModelNotMatches(UIListSelectionObject *pSelect, UIModel pModel)
{
	return (pSelect->pModel == pModel) ? false : true;
}

bool ui_ListSelectionListAndModelNotMatches(UIListSelectionObject *pSelect, UIList *pList)
{
	return (pSelect->pList == pList && pSelect->pModel == pList->peaModel) ? false : true;
}

void ui_ListSelectionUnselectIf(UIListSelectionState *pState, UIListSelectionFilterFunc cbFilter, UserData pData)
{
	StashTableIterator iter;
	StashElement element;
	stashGetIterator(pState->stSelected, &iter);
	while (stashGetNextElement(&iter, &element))
	{
		UIListSelectionObject *pSelect = stashElementGetKey(element);
		if (cbFilter(pSelect, pData))
			ui_ListSelectionStateUnselect(pState, pSelect->pList, pSelect->pColumn, pSelect->iRow);
	}
}

void ui_ListSelectionStateClearInactive(UIListSelectionState *pState, UIList *pList)
{
	ui_ListSelectionUnselectIf(pState, ui_ListSelectionListAndModelNotMatches, pList);
}

void ui_ListSelectionStateClear(UIListSelectionState *pState)
{
	stashTableClearEx(pState->stSelected, ListFreeSelectionObject, NULL);
}

static void ListSelectionSelectIfInternal(UIListSelectionState *pState, UIList *pList, UIListSelectionFilterFunc cbSelect, UserData pSelectData)
{
	S32 iRow;
	if (!pList->peaModel)
		return;
	for (iRow = 0; iRow < eaSize(pList->peaModel); iRow++)
	{
		S32 iSubList;
		S32 iColumn;
		for (iColumn = 0; iColumn < eaSize(&pList->eaColumns); iColumn++)
		{
			UIListColumn *pColumn = pList->eaColumns[iColumn];
			UIListSelectionObject selection = {pList, pList->peaModel, iRow, pColumn};
			if (cbSelect(&selection, pSelectData))
				ui_ListSelectionStateSelect(pState, pList, pColumn, iRow);
		}
		ui_ListSetSubListsModelFromRow(pList, iRow);
		for (iSubList = 0; iSubList < eaSize(&pList->eaSubLists); iSubList++)
		{
			ui_ListSetSubListModelFromRow(pList, pList->eaSubLists[iSubList], iRow);
			ListSelectionSelectIfInternal(pState, pList->eaSubLists[iSubList], cbSelect, pSelectData);
		}
	}
}

void ui_ListSelectionSelectIf(UIListSelectionState *pState, UIListSelectionFilterFunc cbSelect, UserData pSelectData)
{
	UIList *pList = pState->pList;
	ListSelectionSelectIfInternal(pState, pList, cbSelect, pSelectData);
}

static bool ListSelectIfMatchesColumn(UIListSelectionObject *pSelect, UIListColumn *pColumn)
{
	return (pSelect->pColumn == pColumn) ? true : false;
}

void ui_ListSelectColumn(UIList *pList, UIListColumn *pColumn)
{
	if (!pList->bMultiSelect)
		return;
	if (!(inpLevelPeek(INP_CONTROL) || inpLevelPeek(INP_SHIFT)))
		ui_ListSelectionStateClear(ui_ListGetSelectionState(pList));
	ui_ListSelectionSelectIf(ui_ListGetSelectionState(pList), ListSelectIfMatchesColumn, pColumn);
}

void ui_ListColumnSelectCallback(UIListColumn *pColumn, UIList *pList)
{
	ui_ListSelectColumn(pList, pColumn);
	ui_ListCallSelectionChangedCallback(pList);
}

bool ui_ListIsSelected(const UIList *pList, const UIListColumn *pColumn, S32 iRow)
{
	return (ui_ListSelectionIsSelected(ui_ListGetSelectionState(pList), pList, pColumn, iRow) ||
			ui_ListSelectionIsSelected(ui_ListGetSelectionState(pList), pList, NULL, iRow));
}

bool ui_ListIsHovering(const UIList *pList, const UIListColumn *pColumn, S32 iRow)
{
	if( !pColumn ) {
		return pList->hoverRow == iRow;
	} else {
		return pList->hoverCol == pColumn && pList->hoverRow == iRow;
	}
}

void ui_ListDestroyColumns(UIList *pList)
{
	eaDestroyEx(&pList->eaColumns, ui_ListColumnFree);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Automatic Column Context Menu implementation
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void showMoreWindowOkCB(UIButton *unused_Button, UIList *pList)
{
	S32 i;
	for(i = 0; i < eaSize(&pList->eaAutoColumnContextMenuShowMoreWindowCheckButtons); i++)
		pList->eaColumns[i]->bHidden = !pList->eaAutoColumnContextMenuShowMoreWindowCheckButtons[i]->state;

	ui_WindowClose(pList->pAutoColumnContextMenuShowMoreWindow);
}

static void showMoreWindowCancelCB(UIButton *unused_Button, UIList *pList)
{
	ui_WindowClose(pList->pAutoColumnContextMenuShowMoreWindow);
}

static void listColumnShowMoreCB(UIMenuItem *pMenuItem, UIList *pList)
{
	if(!pList->pAutoColumnContextMenuShowMoreWindow)
	{
		F32 sb_width;
		Vec2 minSize;
		S32 i;
		UICheckButton *pCheckButton = NULL;
		UIScrollArea *pScrollArea = NULL;
		UIButton *pButton = NULL;
		UIBoxSizer *verticalBoxSizer = ui_BoxSizerCreate(UIVertical);
		UIBoxSizer *horizontalBoxSizer = ui_BoxSizerCreate(UIHorizontal);
		UIBoxSizer *checkbuttonVerticalBoxSizer = ui_BoxSizerCreate(UIVertical);

		pList->pAutoColumnContextMenuShowMoreWindow = ui_WindowCreate("Choose Columns", 200, 200, 0, 0);
		ui_WindowSetModal(pList->pAutoColumnContextMenuShowMoreWindow, true);
		ui_WidgetSetSizer(UI_WIDGET(pList->pAutoColumnContextMenuShowMoreWindow), UI_SIZER(verticalBoxSizer));

		for(i = 0; i < eaSize(&pList->eaColumns); i++)
		{
			pCheckButton = ui_CheckButtonCreate(0, 0, ui_ListColumnGetTitle(pList->eaColumns[i]), !pList->eaColumns[i]->bHidden);
			eaPush(&pList->eaAutoColumnContextMenuShowMoreWindowCheckButtons, pCheckButton);

			ui_SetActive(UI_WIDGET(pCheckButton), pList->eaColumns[i]->bCanHide);

			ui_BoxSizerAddWidget(checkbuttonVerticalBoxSizer, UI_WIDGET(pCheckButton), 0, UILeft, 0);
		}

		// Create scroll area size to fit the check buttons
		ui_SizerGetMinSize(UI_SIZER(checkbuttonVerticalBoxSizer), minSize);
		pScrollArea = ui_ScrollAreaCreate(/*x=*/0, /*y=*/0, /*w=*/0, /*h=*/0, /*xSize=*/0, /*ySize=*/0, /*xScroll=*/false, /*yScroll=*/true);
		sb_width = ui_ScrollbarWidth(UI_WIDGET(pScrollArea)->sb);
		ui_WidgetSetDimensions(UI_WIDGET(pScrollArea), /*w=*/minSize[0] + sb_width, /*h=*/300);
		ui_ScrollAreaSetSize(pScrollArea, /*xSize=*/minSize[0] + sb_width, /*ySize=*/minSize[1]);

		ui_WidgetSetSizer(UI_WIDGET(pScrollArea), UI_SIZER(checkbuttonVerticalBoxSizer));

		ui_BoxSizerAddFiller(horizontalBoxSizer, 1);

		pButton = ui_ButtonCreate("OK", 0, 0, /*clickedF=*/(UIActivationFunc)showMoreWindowOkCB, /*clickedData=*/pList);
		ui_WidgetSetDimensions(UI_WIDGET(pButton), 60, UI_WIDGET(pButton)->height);
		ui_BoxSizerAddWidget(horizontalBoxSizer, UI_WIDGET(pButton), 0, UINoDirection, 2);

		pButton = ui_ButtonCreate("Cancel", 0, 0, /*clickedF=*/(UIActivationFunc)showMoreWindowCancelCB, /*clickedData=*/pList);
		ui_WidgetSetDimensions(UI_WIDGET(pButton), 60, UI_WIDGET(pButton)->height);
		ui_BoxSizerAddWidget(horizontalBoxSizer, UI_WIDGET(pButton), 0, UINoDirection, 2);

		ui_BoxSizerAddWidget(verticalBoxSizer, UI_WIDGET(pScrollArea), 1, UIWidth, 0);
		ui_BoxSizerAddSizer(verticalBoxSizer, UI_SIZER(horizontalBoxSizer), 0, UIWidth, 0);

		// Set minimum window width to fit the window's sizer contents
		ui_SizerGetMinSize(UI_SIZER(verticalBoxSizer), minSize);
		ui_WindowSetDimensions(pList->pAutoColumnContextMenuShowMoreWindow, minSize[0], 300, minSize[0], minSize[1]);
	}
	else
	{
		S32 i;
		for(i = 0; i < eaSize(&pList->eaColumns); i++)
			pList->eaAutoColumnContextMenuShowMoreWindowCheckButtons[i]->state = !pList->eaColumns[i]->bHidden;
	}

	ui_WindowShow(pList->pAutoColumnContextMenuShowMoreWindow);
}

static void listColumnToggleShownCB(UIMenuItem *pMenuItem, UIListColumn *pListColumn)
{
	pListColumn->bHidden = !pMenuItem->data.state;
}

static void ListAutoColumnContextMenu(UIList *pList)
{
	S32 i;
	UIListColumn **eaListColumns = NULL;

	// Determine which columns are going to be in the popup for hiding/showing
	for(i = 0; i < eaSize(&pList->eaColumns); i++)
	{
		if(i < 10) // always display first 10 columns, whether visible or hidden
			eaPush(&eaListColumns, pList->eaColumns[i]);
		else if(!pList->eaColumns[i]->bHidden) // always display visible columns so they can be hidden
			eaPush(&eaListColumns, pList->eaColumns[i]);
	}

	// Create/Clear the Menu
	if(!pList->pAutoColumnContextMenu)
		pList->pAutoColumnContextMenu = ui_MenuCreate(NULL);
	else
		ui_MenuClearAndFreeItems(pList->pAutoColumnContextMenu);

	for(i = 0; i < eaSize(&eaListColumns); i++)
	{
		UIMenuItem *pMenuItem = ui_MenuItemCreate(ui_ListColumnGetTitle(eaListColumns[i]),
			UIMenuCheckButton, /*callback=*/listColumnToggleShownCB, /*clickedData=*/eaListColumns[i], /*data=*/(void *)!eaListColumns[i]->bHidden);

		pMenuItem->active = eaListColumns[i]->bCanHide;

		ui_MenuAppendItem(pList->pAutoColumnContextMenu, pMenuItem);
	}

	ui_MenuAppendItem(pList->pAutoColumnContextMenu,
		ui_MenuItemCreate("More...", UIMenuCallback, /*callback=*/listColumnShowMoreCB, /*clickedData=*/pList, /*data=*/NULL)
		);

	// Show Popup Menu
	ui_MenuPopupAtCursor(pList->pAutoColumnContextMenu);

	eaDestroy(&eaListColumns);
}
