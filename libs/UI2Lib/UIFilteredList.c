#include "inputMouse.h"

#include "earray.h"
#include "estring.h"
#include "GfxClipper.h"
#include "GfxPrimitive.h"
#include "GfxSprite.h"
#include "textparserutils.h"
#include "tokenstore.h"
#include "utils.h"

#include "UIFilteredList.h"
#include "UILabel.h"
#include "UITextEntry.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static UIFilteredList *gCompareList = NULL;
static Color s_BorderColor = {0, 0, 0, 64};

static void ui_FilteredListRegisteredSelectCallback(UIList *pList, UIFilteredList *pFList);

static int CompareStrings(const void** left, const void** right)
{
	return stricmp((char*)*left,(char*)*right);
}

static int CompareRows(const void** left, const void** right)
{
	const char *pchLeft, *pchRight;

	if (gCompareList->pTable)
	{
		pchLeft = TokenStoreGetString(gCompareList->pTable, gCompareList->iFieldIndex, *left, 0, NULL);
		pchRight = TokenStoreGetString(gCompareList->pTable, gCompareList->iFieldIndex, *right, 0, NULL);
	}
	else
	{
		pchLeft = (const char*)*left;
		pchRight = (const char*)*right;
	}

	if (pchLeft && pchRight)
		return stricmp(pchLeft, pchRight);
	else if (!pchLeft && !pchRight)
		return 0;
	else if (!pchLeft)
		return 1;
	else
		return -1;
}

static int MatchFilter(const char *pchText, const char *pchFilter)
{
	return !pchFilter || !pchFilter[0] || strstri(pchText, pchFilter);
}

static void ui_FilteredListTextEnterCallback(UITextEntry *pEntry, UIFilteredList *pFList)
{
	if(ui_ListGetSelectedRow(pFList->pList) >= 0) {
		ui_FilteredListRegisteredSelectCallback(pFList->pList, pFList);
	}
}

static void ui_FilteredListUpKeyCallback(UITextEntry *pEntry, UIFilteredList *pFList)
{
	S32 row = ui_ListGetSelectedRow(pFList->pList);
	if(row > 0)
		ui_ListSetSelectedRow(pFList->pList, row-1);
}

static void ui_FilteredListDownKeyCallback(UITextEntry *pEntry, UIFilteredList *pFList)
{
	S32 row = ui_ListGetSelectedRow(pFList->pList);
	if(row < eaSize(&pFList->eaFilteredModel)-1)
		ui_ListSetSelectedRow(pFList->pList, row+1);
}

static void ui_FilteredListUpdateFilter(UITextEntry *pEntry, UIFilteredList *pFList)
{
	const char *pchFilter;
	void **eaSelectedValues = NULL;
	int i, j, iLowRow = -1;
	const int * const *eaiRows;
	int *eaiNewRows = NULL;
		
	// Get the filter string (if any)
	pchFilter = ui_TextEntryGetText(pFList->pEntry);

	// Get previous selections (if any)
	eaiRows = ui_ListGetSelectedRows(pFList->pList);
	for(i=eaiSize(eaiRows)-1; i>=0; --i)
		eaPush(&eaSelectedValues, pFList->eaFilteredModel[(*eaiRows)[i]]);

	// Clear the target array
	eaClear(&pFList->eaFilteredModel);

	// Iterate the objects to load them into the list
	if (pFList->pTable) 
	{
		for(i=0; i<eaSize(pFList->peaModel); ++i)
		{
			const char *pchValue = TokenStoreGetString(pFList->pTable, pFList->iFieldIndex, (*pFList->peaModel)[i], 0, NULL);
			if (MatchFilter(pchValue, pchFilter) && (!pFList->cbFilter || pFList->cbFilter(pFList,pchValue,pFList->pFilterData)))
				eaPush(&pFList->eaFilteredModel, (*pFList->peaModel)[i]);
		}
	}
	else
	{
		for(i=0; i<eaSize(pFList->peaModel); ++i)
			if (MatchFilter(((char**)(*pFList->peaModel))[i], pchFilter) 
				&& (!pFList->cbFilter || pFList->cbFilter(pFList,((char**)(*pFList->peaModel))[i],pFList->pFilterData)))
				eaPush(&pFList->eaFilteredModel, (*pFList->peaModel)[i]);
	}

	// Sort the values
	gCompareList = pFList;
	if (!pFList->bDontAutoSort)
		eaQSort(pFList->eaFilteredModel, CompareRows);

	// See if the previous selection can be matched
	if (eaSelectedValues)
	{
		for(i=eaSize(&eaSelectedValues)-1; i>=0; --i)
		{
			for(j=eaSize(&pFList->eaFilteredModel)-1; j>=0; --j) 
			{
				if (eaSelectedValues[i] == pFList->eaFilteredModel[j]) 
				{
					eaiPush(&eaiNewRows,j);
					if ((iLowRow == -1) || (j < iLowRow))
						iLowRow = j;
					break;
				}
			}
		}
	}

	if(eaiSize(&eaiNewRows)) {
		ui_ListSetSelectedRows(pFList->pList, &eaiNewRows);
	} else {
		ui_ListSetSelectedRow(pFList->pList, 0);
	}

	if (!pFList->bDontAutoScroll)
		ui_ListScrollToRow(pFList->pList, iLowRow);

	// Clean up
	eaiDestroy(&eaiNewRows);
	eaDestroy(&eaSelectedValues);
}

static void ListGetText(UIList *pList, UIListColumn *pColumn, S32 iRow, UIFilteredList *pFList, char **estrOutput)
{
	if (pFList->pTable)
		estrConcatf(estrOutput, "%s", TokenStoreGetString(pFList->pTable, pFList->iFieldIndex, pFList->eaFilteredModel[iRow], 0, NULL));
	else
		estrConcatf(estrOutput, "%s", (char*)pFList->eaFilteredModel[iRow]);
}

static void ui_FilteredListRegisteredSelectCallback(UIList *pList, UIFilteredList *pFList)
{
	if (pFList->cbSelected)
		pFList->cbSelected(pFList, pFList->pSelectedData);
}

void ui_FilteredListRegisteredActivatedCallback(UIList *pList, UIFilteredList *pFList)
{
	if (pFList->cbActivated)
		pFList->cbActivated(pFList, pFList->pActivatedData);
}

UIFilteredList *ui_FilteredListCreate(const char *pchColumnLabel, UIListColumnType eType, ParseTable *pTable, UIModel peaModel, intptr_t contents, F32 fRowHeight)
{
	UIFilteredList *pFList = calloc(1, sizeof(UIFilteredList));
	F32 fHeight = 0;

	pFList->peaModel = peaModel;
	eaCreate(&pFList->eaFilteredModel);
	if ((eType == UIListPTName) || (eType == UIListPTMessage))
	{
		pFList->pTable = pTable;
		if (!ParserFindColumn(pTable, (char*)contents, &pFList->iFieldIndex)) {
			assertmsg(0,"The field name provided does not exist");
		}
	}
	else if (eType == UIListPTIndex)
	{
		pFList->pTable = pTable;
		pFList->iFieldIndex = (int)contents;
	}

	ui_WidgetInitialize(UI_WIDGET(pFList), ui_FilteredListTick, ui_FilteredListDraw, ui_FilteredListFreeInternal, NULL, NULL);
	pFList->pList = ui_ListCreate(pFList->pTable, &pFList->eaFilteredModel, fRowHeight);
	if (eType == UIListPTMessage)
		ui_ListAppendColumn(pFList->pList, ui_ListColumnCreateParseMessage(pchColumnLabel, (char*)contents, pFList));
	else
		ui_ListAppendColumn(pFList->pList, ui_ListColumnCreateText(pchColumnLabel, ListGetText, pFList));
	if (!pchColumnLabel)
		pFList->pList->fHeaderHeight = 0;
	pFList->pEntry = ui_TextEntryCreate("", 0, 0);
	pFList->pLabel = ui_LabelCreate("Filter", 0, 0);
	ui_TextEntrySetEnterCallback(pFList->pEntry, ui_FilteredListTextEnterCallback, pFList);
	ui_TextEntrySetChangedCallback(pFList->pEntry, ui_FilteredListUpdateFilter, pFList);
	ui_TextEntrySetUpKeyCallback(pFList->pEntry, ui_FilteredListUpKeyCallback, pFList);
	ui_TextEntrySetDownKeyCallback(pFList->pEntry, ui_FilteredListDownKeyCallback, pFList);
	ui_ListSetSelectedCallback(pFList->pList, ui_FilteredListRegisteredSelectCallback, pFList);
	ui_ListSetActivatedCallback(pFList->pList, ui_FilteredListRegisteredActivatedCallback, pFList);
	MAX1(fHeight, UI_WIDGET(pFList->pLabel)->height);
	MAX1(fHeight, UI_WIDGET(pFList->pEntry)->height);
	ui_WidgetSetPositionEx(UI_WIDGET(pFList->pLabel), UI_HSTEP, 0, 0, 0, UITopLeft);
	ui_WidgetSetPositionEx(UI_WIDGET(pFList->pEntry), UI_WIDGET(pFList->pLabel)->width + UI_STEP, 0, 0, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pFList->pEntry), 1.f, UIUnitPercentage);
	ui_WidgetSetPositionEx(UI_WIDGET(pFList->pList), 0, fHeight, 0, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pFList->pList), 1.f, UIUnitPercentage);
	ui_WidgetSetHeightEx(UI_WIDGET(pFList->pList), 1.f, UIUnitPercentage);
	ui_WidgetAddChild(UI_WIDGET(pFList), UI_WIDGET(pFList->pLabel));
	ui_WidgetAddChild(UI_WIDGET(pFList), UI_WIDGET(pFList->pEntry));
	ui_WidgetAddChild(UI_WIDGET(pFList), UI_WIDGET(pFList->pList));
	ui_FilteredListUpdateFilter(pFList->pEntry, pFList);
	return pFList;
}

void ui_FilteredListFreeInternal(UIFilteredList *pFList)
{
	eaDestroy(&pFList->eaFilteredModel);
	eaDestroy(&pFList->eaSelectedInternal);
	eaiDestroy(&pFList->eaiSelectedInternal);
	ui_WidgetFreeInternal(UI_WIDGET(pFList));
}

void ui_FilteredListTick(UIFilteredList *pFList, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pFList);
	UI_TICK_EARLY(pFList, true, true);
	UI_TICK_LATE(pFList);
}

static void ui_FilteredListFillDrawingDescription( UIFilteredList* pFList, UIDrawingDescription* desc )
{
	UISkin* skin = UI_GET_SKIN( pFList );
		
	if( skin->bUseStyleBorders || skin->bUseTextureAssemblies ) {
		const char* descName = skin->astrListInsideStyle;
		if( skin->bUseTextureAssemblies && RefSystem_ReferentFromString( "UITextureAssembly", descName )) {
			desc->textureAssemblyName = descName;
		} else {
			desc->styleBorderName = descName;
		}
	} else {
		desc->textureNameUsingLegacyColor = "white";
	}
}

void ui_FilteredListDraw(UIFilteredList *pFList, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pFList);
	UIDrawingDescription desc = { 0 };
	Color c;
	ui_FilteredListFillDrawingDescription( pFList, &desc );
	
	if (UI_GET_SKIN(pFList))
		c = UI_GET_SKIN(pFList)->background[0];
	else
		c = pFList->widget.color[0];

	// Do before setting clipping
	if (pFList->bDrawBorder)
	{
		gfxDrawLine(x, y, z+.01, x+w-scale, y, s_BorderColor);
		gfxDrawLine(x, y, z+.01, x, y+h-scale, s_BorderColor);
	}

	UI_DRAW_EARLY(pFList);

	if (pFList->bUseBackgroundColor) {
		ui_DrawingDescriptionDraw( &desc, &box, scale, z, 255, c, ColorBlack );
	}

	UI_DRAW_LATE(pFList);
}

const char *ui_FilteredListGetFilter(UIFilteredList *pFList)
{
	return ui_TextEntryGetText(pFList->pEntry);
}

void ui_FilteredListSetFilter(UIFilteredList *pFList, const char *pchFilter)
{
	ui_TextEntrySetText(pFList->pEntry, pchFilter);
	ui_FilteredListUpdateFilter(pFList->pEntry, pFList);
}

void ui_FilteredListRefresh(SA_PARAM_NN_VALID UIFilteredList *pFList)
{
	ui_FilteredListUpdateFilter(pFList->pEntry, pFList);
}

UIList *ui_FilteredListGetList(UIFilteredList *pFList)
{
	return pFList->pList;
}

static int ui_FilteredListFindModelRow(UIFilteredList *pFList, void *pRowData)
{
	int i;
	for(i=eaSize(pFList->peaModel)-1; i>=0; --i)
	{
		if ((*pFList->peaModel)[i] == pRowData)
			return i;
	}

	return -1;
}

static int ui_FilteredListFindFilteredRow(UIFilteredList *pFList, void *pRowData)
{
	int i;
	for(i=eaSize(&pFList->eaFilteredModel)-1; i>=0; --i)
	{
		if (pFList->eaFilteredModel[i] == pRowData)
			return i;
	}

	return -1;
}

int ui_FilteredListGetSelectedRow(UIFilteredList *pFList)
{
	int iRow = ui_ListGetSelectedRow(pFList->pList);
	if (iRow >= 0)
		return ui_FilteredListFindModelRow(pFList, pFList->eaFilteredModel[iRow]);
	else
		return -1;
}

int** ui_FilteredListGetSelectedRows(UIFilteredList *pFList)
{
	const int * const*peaiSelectedRows = ui_ListGetSelectedRows(pFList->pList);
	int i;

	eaiClear(&pFList->eaiSelectedInternal);
	for(i=0; i<eaiSize(peaiSelectedRows); ++i) 
		eaiPush(&pFList->eaiSelectedInternal, ui_FilteredListFindModelRow(pFList, pFList->eaFilteredModel[(*peaiSelectedRows)[i]]));

	return &pFList->eaiSelectedInternal;
}

void ui_FilteredListSetSelectedRow(UIFilteredList *pFList, S32 iRow)
{
	ui_ListSetSelectedRow(pFList->pList, iRow < 0 ? -1 : ui_FilteredListFindFilteredRow(pFList, (*pFList->peaModel)[iRow]));
}

void ui_FilteredListSetSelectedRowAndCallback(UIFilteredList *pFList, S32 iRow)
{
	ui_ListSetSelectedRowAndCallback(pFList->pList, iRow < 0 ? -1 : ui_FilteredListFindFilteredRow(pFList, (*pFList->peaModel)[iRow]));
}

void ui_FilteredListSetSelectedRows(UIFilteredList *pFList, S32 **peaiRows)
{
	int *eaiTemp = NULL;
	int i, r;

	for(i=eaiSize(peaiRows)-1; i>=0; --i)
	{
		r = ui_FilteredListFindFilteredRow(pFList, (*pFList->peaModel)[(*peaiRows)[i]]);
		if (r >= 0)
			eaiPush(&eaiTemp, r);
	}
	ui_ListSetSelectedRows(pFList->pList, &eaiTemp);
	eaiDestroy(&eaiTemp);
}

void ui_FilteredListSetSelectedRowsAndCallback(UIFilteredList *pFList, S32 **peaiRows)
{
	int *eaiTemp = NULL;
	int i, r;

	for(i=eaiSize(peaiRows)-1; i>=0; --i)
	{
		r = ui_FilteredListFindFilteredRow(pFList, (*pFList->peaModel)[(*peaiRows)[i]]);
		if (r >= 0)
			eaiPush(&eaiTemp, r);
	}
	ui_ListSetSelectedRowsAndCallback(pFList->pList, &eaiTemp);
	eaiDestroy(&eaiTemp);
}

char *ui_FilteredListGetSelectedString(SA_PARAM_NN_VALID UIFilteredList *pFList)
{
	ui_FilteredListGetSelectedStrings(pFList);
	if (eaSize(&pFList->eaSelectedInternal)) 
		return (char*)pFList->eaSelectedInternal[0];
	else
		return NULL;
}

char ***ui_FilteredListGetSelectedStrings(SA_PARAM_NN_VALID UIFilteredList *pFList)
{
	const int * const *peaiRows;
	int i;

	// Clear previous selection list
	eaClear(&pFList->eaSelectedInternal);

	// Get selections (if any)
	peaiRows = ui_ListGetSelectedRows(pFList->pList);
	for(i=0; i<eaiSize(peaiRows); ++i)
	{
		// apparently this can return an earray with -1's in it
		if ((*peaiRows)[i] >= 0)
		{
			if (pFList->pTable)
				eaPush(&pFList->eaSelectedInternal, (void*)TokenStoreGetString(pFList->pTable, pFList->iFieldIndex, pFList->eaFilteredModel[(*peaiRows)[i]], 0, NULL));
			else
				eaPush(&pFList->eaSelectedInternal, (char*)pFList->eaFilteredModel[(*peaiRows)[i]]);
		}
	}

	// Sort the values
	if (!pFList->bDontAutoSort)
		eaQSort(pFList->eaSelectedInternal, CompareStrings);

	return (char***)&pFList->eaSelectedInternal;
}

void *ui_FilteredListGetSelectedObject(SA_PARAM_NN_VALID UIFilteredList *pFList)
{
	ui_FilteredListGetSelectedObjects(pFList);
	if (eaSize(&pFList->eaSelectedInternal)) 
		return pFList->eaSelectedInternal[0];
	else
		return NULL;
}

void ***ui_FilteredListGetSelectedObjects(SA_PARAM_NN_VALID UIFilteredList *pFList)
{
	const int * const *peaiRows;
	int i;

	// Clear previous selection list
	eaClear(&pFList->eaSelectedInternal);

	// Get selections (if any)
	peaiRows = ui_ListGetSelectedRows(pFList->pList);
	for(i=0; i<eaiSize(peaiRows); ++i)
		eaPush(&pFList->eaSelectedInternal, pFList->eaFilteredModel[(*peaiRows)[i]]);

	// Sort the values
	gCompareList = pFList;
	if (!pFList->bDontAutoSort)
		eaQSort(pFList->eaSelectedInternal, CompareRows);

	return &pFList->eaSelectedInternal;
}

void ui_FilteredListSetModel(UIFilteredList *pFList, ParseTable *pTable, UIModel peaModel)
{
	pFList->pTable = pTable;
	pFList->peaModel = peaModel;
	pFList->pList->pTable = pTable;
	ui_FilteredListUpdateFilter(pFList->pEntry, pFList);
}

UIModel ui_FilteredListGetModel(const UIFilteredList *pFList)
{
	return pFList->peaModel;
}

void ui_FilteredListScrollToRow(SA_PARAM_NN_VALID UIFilteredList *pFList, S32 iRow)
{
	ui_ListScrollToRow(pFList->pList, iRow < 0 ? -1 : ui_FilteredListFindFilteredRow(pFList, (*pFList->peaModel)[iRow]));
}

void ui_FilteredListScrollToPath(SA_PARAM_NN_VALID UIFilteredList *pFList, F32 x, F32 y, F32 w, F32 h, F32 scale)
{
	ui_ListScrollToPath(pFList->pList, x, y, w, h, scale);
}

void ui_FilteredListSetSelectedCallback(SA_PARAM_NN_VALID UIFilteredList *pFList, UIActivationFunc cbSelected, UserData pSelectedData)
{
	pFList->cbSelected = cbSelected;
	pFList->pSelectedData = pSelectedData;
}

void ui_FilteredListSetActivatedCallback(SA_PARAM_NN_VALID UIFilteredList *pFList, UIActivationFunc cbActivated, UserData pActivatedData)
{
	pFList->cbActivated = cbActivated;
	pFList->pActivatedData = pActivatedData;
}

void ui_FilteredListSetFilterCallback(SA_PARAM_NN_VALID UIFilteredList *pFList, UIFilterListFunc cbFilter, UserData pUserData)
{ 
	pFList->cbFilter = cbFilter;
	pFList->pFilterData = pUserData;
}

void ui_FilteredListHoverProxy(UIList *pList, UIListColumn *pColumn, S32 iRow, UIFilteredList *pFList)
{
	if (pFList->cbHover)
	{
		int iActualRow = -1;
		if (iRow >= 0)
			iActualRow = ui_FilteredListFindModelRow(pFList, pFList->eaFilteredModel[iRow]);
		pFList->cbHover(pFList, pColumn, iActualRow, pFList->pHoverData);
	}
}

void ui_FilteredListSetHoverCallback(SA_PARAM_NN_VALID UIFilteredList *pFList, UIFilteredRowHoverFunc cbHover, UserData pUserData)
{
	pFList->cbHover = cbHover;
	pFList->pHoverData = pUserData;
	if (cbHover)
		ui_ListSetHoverCallback(pFList->pList, ui_FilteredListHoverProxy, pFList);
	else
		ui_ListSetHoverCallback(pFList->pList, NULL, NULL);
}
