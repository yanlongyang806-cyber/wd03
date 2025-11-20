/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "EditorManager.h"
#include "ResourceSearch.h"
#include "EditorPreviewWindow.h"
#include "EditorSearchWindow.h"
#include "EditLibUIUtil.h"
#include "ResourceManagerUI.h"
#include "EString.h"
#include "Color.h"
#include "sysutil.h"
#include "StringUtil.h"
#include "StringCache.h"
#include "error.h"
#include "file.h"

#include "itemCommon.h"

#include "AutoGen/ResourceSearch_h_ast.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

AUTO_ENUM; 
typedef enum SearchFilterType
{
	SEARCH_FILTER_NAME, ENAMES("Filter by Name")
	SEARCH_FILTER_ALL, ENAMES("Filter by All Fields")
	SEARCH_FILTER_TYPE, ENAMES("Filter by Type")
} SearchFilterType;

AUTO_STRUCT;
typedef struct SearchStatusData
{
	char *pcSearchTitle;
	char *pcDescription;

	SearchFilterType filterType;
	char *pcFilterString;
	F32 yScroll;
	bool bSmall;
	bool bShowCostumes; AST(DEFAULT(1))

	ResourceSearchRequest request;
	ResourceSearchResult result;
} SearchStatusData;

#include "AutoGen/EditorSearchWindow_c_ast.c"

#ifndef NO_EDITORS

typedef struct SearchWindowData
{
	// Set up on initial create
	bool bInit;
	UIWindow *pWindow;
	UIPane *pPane;
	UIMenu *pCellContextMenu;
	UIComboBox *pSearchModeCombo;

	UIButton *pBackButton;
	UIButton *pForwardButton;
	UIButton *pRefreshButton;
	UIButton *pExportButton;

	UIComboBox *pFilterModeCombo;
	UITextEntry *pFilterText;
	UICheckButton *pSmallCheckButton;
	UICheckButton *pShowCostCheckButton;

	// Possible boxes for search details
	UIComboBox *pSearchTypeCombo;
	UITextEntry *pSearchDetailsText;	
	UITextEntry *pSearchNameText;

	// These are transient
	UIList *pList;
	bool bWindowVisible;
	const char ** ppSearchTags;

	int iStackLoc;
	SearchStatusData **ppSearchStack;
	ResourceSearchRequest searchRequest;
	ResourceSearchResultRow **ppListRows; // Filtered list rows, shallow copy of rows in the search stack

} SearchWindowData;

SearchWindowData gSearchWindow;

#define SEARCH_HEADER_HEIGHT 75

SearchStatusData *searchGetRequest(U32 id)
{
	int i;
	for (i = eaSize(&gSearchWindow.ppSearchStack) - 1; i >=0; i--)
	{
		if (gSearchWindow.ppSearchStack[i]->request.iRequest == id)
		{
			return gSearchWindow.ppSearchStack[i];
		}
	}
	return NULL;
}

static void searchListDrawType(UIList *list, UIListColumn *column, int row, SearchStatusData *pData, char **output)
{
	ResourceSearchResultRow *pRow = eaGet(&gSearchWindow.ppListRows, row);
	if (pRow)
	{
		estrPrintf(output, "%s", resDictGetItemDisplayName(pRow->pcType));
	}
}


static void searchListDrawInfo(UIList *list, UIListColumn *column, F32 x, F32 y, F32 w, F32 h, F32 scale, F32 z, CBox *logical_box, S32 row, SearchStatusData *pData)
{
	static char *pSMF;
	ResourceSearchResultRow *pRow = eaGet(&gSearchWindow.ppListRows, row);
	if (pRow && pData)
	{
		ResourceInfo *pInfo = resGetInfo(pRow->pcType, pRow->pcName);
		unsigned int oldSize;
		ParseTable *pTable = resDictGetParseTable(pRow->pcType);
		estrClear(&pSMF);
		if (ui_ListIsSelected(list, column, row))
		{
			estrConcatf(&pSMF,"<color white>");
		}
		oldSize = estrLength(&pSMF);
		if (pInfo)
		{
			if (!pData->bSmall && resDrawPreview(pInfo, "HeadshotStyle_PreviewNoFrame", x + 2, y + 2, h - 4, h - 4, scale, z, 255))
			{
				x += h;
				w -= h;
			}
			if (pInfo->resourceDisplayName && pInfo->resourceDisplayName[0])
			{
				estrConcatf(&pSMF, "<b>DisplayName:</b> %s<BR>", pInfo->resourceDisplayName);
			}
			if (pInfo->resourceTags && pInfo->resourceTags[0])
			{
				estrConcatf(&pSMF, "<b>Tags:</b> %s<BR>", pInfo->resourceTags);
			}
			if (pInfo->resourceNotes && pInfo->resourceNotes[0])
			{
				estrConcatf(&pSMF, "<b>Notes: </b>%s<BR>", pInfo->resourceNotes);
			}
			if (pInfo->resourceLocation && pInfo->resourceLocation[0])
			{
				estrConcatf(&pSMF, "<b>Location: </b>%s<BR>", pInfo->resourceLocation);
			}
			if (pData->bShowCostumes && pTable == resDictGetParseTable("ItemDef"))
			{
				if(pRow->pResRef)
				{
					ItemDef *pDef = (ItemDef *)GET_REF(((SearchItemDefRef *)(pRow->pResRef))->hDef);
					int i;
					if(pDef)
					{
						for(i = 0; i < eaSize(&pDef->ppCostumes); ++i)
						{
							if(pDef->ppCostumes[i])
							{
								PlayerCostume *pCostume = GET_REF( pDef->ppCostumes[i]->hCostumeRef);
								if(pCostume)
								{
									ResourceInfo *pCostInfo = resGetInfo(allocAddString("PlayerCostume"), pCostume->pcName);
									if(pCostInfo)
									{
										resDrawPreview(pCostInfo, "HeadshotStyle_PreviewNoFrame", x + 2, y + 2, h - 4, h - 4, scale, z, 255);
										x += h;
										w -= h;
									}
								}
							}
						}
					}
				}
			}
		}
		if (estrLength(&pSMF) == oldSize)
		{
			estrPrintf(&pSMF, "No Info");
		}
		if (ui_ListIsSelected(list, column, 0))
		{
			estrConcatf(&pSMF,"</color>");
		}
		ui_ListDefaultDrawSMFInBox(list, column, x, y, w, h, scale, z, logical_box, row, pData, pRow, pSMF, 255, false);
	}	
}

static void searchContext_Edit(UIMenuItem *pItem, void *userData)
{
	const int * const *peaiRows = ui_ListGetSelectedRows(gSearchWindow.pList);
	int j;
	for (j = 0; j < eaiSize(peaiRows); j++)
	{	
		int i = eaiGet(peaiRows, j);
		SearchStatusData *pData = eaGet(&gSearchWindow.ppSearchStack, gSearchWindow.iStackLoc);
		if (pData)
		{
			ResourceSearchResultRow *pRow = eaGet(&gSearchWindow.ppListRows, i);
			if (pRow)
			{
				emOpenFileEx(pRow->pcName, pRow->pcType);
			}
		}
	}
}

static void searchContext_Preview(UIMenuItem *pItem, void *userData)
{
	const int * const *peaiRows = ui_ListGetSelectedRows(gSearchWindow.pList);
	int j;
	for (j = 0; j < eaiSize(peaiRows); j++)
	{	
		int i = eaiGet(peaiRows, j);
		SearchStatusData *pData = eaGet(&gSearchWindow.ppSearchStack, gSearchWindow.iStackLoc);
		if (pData)
		{
			ResourceSearchResultRow *pRow = eaGet(&gSearchWindow.ppListRows, i);
			if (pRow)
			{
				PreviewResource(pRow->pcType, pRow->pcName);
			}
		}
	}
}


static void searchContext_FindUsage(UIMenuItem *pItem, void *userData)
{
	const int * const *peaiRows = ui_ListGetSelectedRows(gSearchWindow.pList);
	int j;
	for (j = 0; j < eaiSize(peaiRows); j++)
	{	
		int i = eaiGet(peaiRows, j);
		SearchStatusData *pData = eaGet(&gSearchWindow.ppSearchStack, gSearchWindow.iStackLoc);
		if (pData)
		{
			ResourceSearchResultRow *pRow = eaGet(&gSearchWindow.ppListRows, i);
			if (pRow)
			{
				RequestUsageSearch(pRow->pcType, pRow->pcName);
			}
		}
	}
}

static void searchContext_ListReferences(UIMenuItem *pItem, void *userData)
{
	const int * const *peaiRows = ui_ListGetSelectedRows(gSearchWindow.pList);
	int j;
	for (j = 0; j < eaiSize(peaiRows); j++)
	{	
		int i = eaiGet(peaiRows, j);
		SearchStatusData *pData = eaGet(&gSearchWindow.ppSearchStack, gSearchWindow.iStackLoc);
		if (pData)
		{
			ResourceSearchResultRow *pRow = eaGet(&gSearchWindow.ppListRows, i);
			if (pRow)
			{
				RequestReferencesSearch(pRow->pcType, pRow->pcName);
			}
		}
	}
}

static void searchContext_CopyName(UIMenuItem *pItem, void *userData)
{
	static char *nameStr = 0;
	const int * const *peaiRows = ui_ListGetSelectedRows(gSearchWindow.pList);
	int j;
	estrPrintf(&nameStr,"");
	for (j = 0; j < eaiSize(peaiRows); j++)
	{	
		int i = eaiGet(peaiRows, j);
		SearchStatusData *pData = eaGet(&gSearchWindow.ppSearchStack, gSearchWindow.iStackLoc);
		if (pData)
		{
			ResourceSearchResultRow *pRow = eaGet(&gSearchWindow.ppListRows, i);
			if (pRow)
			{
				if (j != 0)
					estrConcatf(&nameStr, ", ");
				estrConcatf(&nameStr, "%s", pRow->pcName);
			}
		}
	}
	winCopyToClipboard(nameStr);
}

static void searchCellContext(UIList *pList, S32 iColumn, S32 iRow, F32 fMouseX, F32 fMouseY, CBox *pBox, SearchStatusData *pData)
{
	ResourceSearchResultRow *pRow = eaGet(&gSearchWindow.ppListRows, iRow);
	if (pRow)
	{
		const int * const *peaiRows;
		if (!gSearchWindow.pCellContextMenu) {
			gSearchWindow.pCellContextMenu = ui_MenuCreate(NULL);
		}
		else
		{
			ui_MenuClearAndFreeItems(gSearchWindow.pCellContextMenu);
		}

		peaiRows = ui_ListGetSelectedRows(gSearchWindow.pList);

		if (eaiSize(peaiRows) <= 1)
		{
			// If 0 or 1 selected, change selection to what you right clicked			
			ui_ListSetSelectedRow(gSearchWindow.pList, iRow);
			peaiRows = ui_ListGetSelectedRows(gSearchWindow.pList);
		}

		if (eaiSize(peaiRows) == 1)
		{		
			ui_MenuAppendItems(gSearchWindow.pCellContextMenu,
					ui_MenuItemCreate("Preview",UIMenuCallback, searchContext_Preview, NULL, NULL),
					ui_MenuItemCreate("Open in Editor",UIMenuCallback, searchContext_Edit, NULL, NULL),
					ui_MenuItemCreate("Copy Name",UIMenuCallback, searchContext_CopyName, NULL, NULL),
					ui_MenuItemCreate("", UIMenuSeparator, NULL, NULL, NULL),
					ui_MenuItemCreate("Find Usage",UIMenuCallback, searchContext_FindUsage, NULL, NULL),
					ui_MenuItemCreate("List References",UIMenuCallback, searchContext_ListReferences, NULL, NULL),
					NULL);
		}
		else
		{
			ui_MenuAppendItems(gSearchWindow.pCellContextMenu,
				ui_MenuItemCreate("Open All in Editor",UIMenuCallback, searchContext_Edit, (void *)((intptr_t)iRow), NULL),
				ui_MenuItemCreate("Copy Names",UIMenuCallback, searchContext_CopyName, (void *)((intptr_t)iRow), NULL),
				NULL);
		}

		ui_MenuPopupAtCursor(gSearchWindow.pCellContextMenu);
	}
}

static void searchCellActivate(UIList *pList, S32 iColumn, S32 iRow, F32 fMouseX, F32 fMouseY, CBox *pBox, SearchStatusData *pData)
{
	ResourceSearchResultRow *pRow = eaGet(&gSearchWindow.ppListRows, iRow);
	if (pRow)
	{
		PreviewResource(pRow->pcType, pRow->pcName);
	}
}



static void searchSetTypeTextCB(UITextEntry *entry, void *userData)
{
	ResourceSearchRequest *pRequest = &gSearchWindow.searchRequest;
	if (pRequest)
	{
		pRequest->pcType = strdup_ifdiff(ui_TextEntryGetText(entry), pRequest->pcType);
	}
}

static void searchSetNameTextCB(UITextEntry *entry, void *userData)
{
	ResourceSearchRequest *pRequest = &gSearchWindow.searchRequest;
	if (pRequest)
	{
		pRequest->pcName = strdup_ifdiff(ui_TextEntryGetText(entry), pRequest->pcName);
	}
}

static void searchSetDetailsTextCB(UITextEntry *entry, void *userData)
{
	ResourceSearchRequest *pRequest = &gSearchWindow.searchRequest;
	if (pRequest)
	{
		pRequest->pcSearchDetails = strdup_ifdiff(ui_TextEntryGetText(entry), pRequest->pcSearchDetails);
	}
}

static void searchTextEnterCB(UITextEntry *entry, void *userData)
{
	ResourceSearchRequest *pRequest = &gSearchWindow.searchRequest;
	if (pRequest)
	{
		pRequest->iRequest = 0;
		RequestResourceSearch(pRequest);
	}
}

static void searchNewSearchButton(UIButton *button, UserData unused)
{
	ResourceSearchRequest *pRequest = &gSearchWindow.searchRequest;
	if (pRequest && pRequest->pcType)
	{
		pRequest->iRequest = 0;
		RequestResourceSearch(pRequest);
	}
}

static void searchExportResultsCB(const char *path, const char *fileName, ResourceSearchRequest *pSearchRequest)
{
	SearchStatusData *pData = searchGetRequest(pSearchRequest->iRequest);
	FILE *pFile = NULL;
	char fullName[CRYPTIC_MAX_PATH];
	char destName[CRYPTIC_MAX_PATH];
	int i;
	const unsigned char utf_bom[3] = {0xEF, 0xBB, 0xBF};
	char filterString[1024] = {0};

	if(!pData)
		return;

	sprintf(fullName, "%s/%s", path, fileName);

	printf("%s\\%s", path, fileName);

	fileLocateWrite(fullName, destName);

	pFile = fopen(destName, "w");

	if(!pFile)
	{
		Errorf("Failed to open file: %s\n", destName);
		return;
	}

	fwrite(utf_bom, 1, sizeof(utf_bom), pFile);

	if (pData->pcFilterString && pData->pcFilterString[0])
	{
		sprintf(filterString, "*%s*", pData->pcFilterString);
	}

	for(i = 0; i < eaSize(&(pData->result.eaRows)); ++i)
	{
		ResourceSearchResultRow *pRow = pData->result.eaRows[i];
		ResourceInfo *pInfo = resGetInfo(pRow->pcType, pRow->pcName);

		if (filterString && filterString[0])
		{
			// Filter out results if needed
			if (pData->filterType == SEARCH_FILTER_NAME || !pInfo)
			{
				if (!matchExact(filterString, pRow->pcName))
					continue;
			}
			else if(pData->filterType == SEARCH_FILTER_TYPE)
			{
				if (!matchExact(filterString, pRow->pcType))
					continue;
			}
			else
			{
				bool bFound = false;
				if (pInfo->resourceName && matchExact(filterString, pInfo->resourceName))
					bFound = true;
				if (pInfo->resourceDisplayName && matchExact(filterString, pInfo->resourceDisplayName))
					bFound = true;
				if (pInfo->resourceTags && matchExact(filterString, pInfo->resourceTags))
					bFound = true;
				if (pInfo->resourceNotes && matchExact(filterString, pInfo->resourceNotes))
					bFound = true;
				if (!bFound)
					continue;
			}
		}

		fprintf(pFile, "%s,%s,\"%s\",%s\n", pRow->pcType, pRow->pcName, pInfo->resourceDisplayName,pInfo->resourceLocation);
	}

	fclose(pFile);
}

static void searchNewExportButton(UIButton *pButton, UserData unused)
{
	ResourceSearchRequest *pRequest = &gSearchWindow.searchRequest;
	UIWindow *pFileBrowser = NULL;

	pFileBrowser = ui_FileBrowserCreate("Export Search Results", "Save CSV", UIBrowseNew, UIBrowseFiles, false, "", "", "", ".csv", NULL, NULL, searchExportResultsCB, pRequest);

	ui_WindowShow(pFileBrowser);
}

static void searchTypeComboCB(UIComboBox *combo, void *unused)
{
	ResourceSearchRequest *pRequest = &gSearchWindow.searchRequest;
	ResourceDictionaryInfo *pInfo = ui_ComboBoxGetSelectedObject(combo);
	if (!pInfo)
	{
		return;
	}
	else if (!pInfo->pDictName)
	{
		ui_ComboBoxSetSelected(combo, -1);
	}
	else
	{		
		pRequest->pcType = strdup_ifdiff(pInfo->pDictName, pRequest->pcType);
		
		if (gSearchWindow.pSearchNameText)
		{
			UITextEntry *pNewNameEntry;
			// We need to recreate the name entry with the new type

			if (pRequest->pcType && resDictGetInfo(pRequest->pcType) && pRequest->eSearchMode != SEARCH_MODE_FIELD_SEARCH)
			{
				ANALYSIS_ASSUME(pRequest->pcType != NULL);
				pNewNameEntry = ui_TextEntryCreateWithGlobalDictionaryCombo(pRequest->pcName, UI_WIDGET(gSearchWindow.pSearchNameText)->x, UI_WIDGET(gSearchWindow.pSearchNameText)->y, 
					pRequest->pcType, "resourceName", true, true, false, true);
			}
			else if (pRequest->eSearchMode == SEARCH_MODE_FIELD_SEARCH)
			{
				static const char **eaFieldNames = NULL;
				int i = 1;
				ParseTable *pTable = resDictGetParseTable(pRequest->pcType);
				char *estrTempField = NULL;
				estrStackCreate(&estrTempField);

				if(eaFieldNames){
					eaClear(&eaFieldNames);
				} else {
					eaCreate(&eaFieldNames);
				}
				while(pTable[++i].type != TOK_END){
					if(pTable[i].type == TOK_IGNORE)
						break;
					estrClear(&estrTempField);
					estrPrintf(&estrTempField, "%s.%s", pTable[0].name, pTable[i].name);
					eaPush(&eaFieldNames, allocAddString(estrTempField));
				}
				pNewNameEntry = ui_TextEntryCreateWithStringCombo(allocAddString("Select a field"), UI_WIDGET(gSearchWindow.pSearchNameText)->x, UI_WIDGET(gSearchWindow.pSearchNameText)->y, &eaFieldNames, true, true, false, true);
				pNewNameEntry->cb->bDontSortList = true;
				estrDestroy(&estrTempField);
			}
			else
			{
				pNewNameEntry = ui_TextEntryCreate(pRequest->pcName, UI_WIDGET(gSearchWindow.pSearchNameText)->x, UI_WIDGET(gSearchWindow.pSearchNameText)->y);
			}

			ui_WidgetSetDimensions(UI_WIDGET(pNewNameEntry), UI_WIDGET(gSearchWindow.pSearchNameText)->width, UI_WIDGET(gSearchWindow.pSearchNameText)->height); 
			ui_WidgetQueueFree(UI_WIDGET(gSearchWindow.pSearchNameText));
			gSearchWindow.pSearchNameText = pNewNameEntry;

			ui_TextEntrySetChangedCallback(gSearchWindow.pSearchNameText, searchSetNameTextCB, NULL);
			ui_TextEntrySetEnterCallback(gSearchWindow.pSearchNameText, searchTextEnterCB, NULL);
			ui_WindowAddChild(gSearchWindow.pWindow, UI_WIDGET(gSearchWindow.pSearchNameText));
		}
		if (gSearchWindow.pSearchDetailsText)
		{
			UITextEntry *pNewDetailsEntry;
			// We need to recreate the Details entry with the new type

			gSearchWindow.ppSearchTags = resGetValidTags(pRequest->pcType);
			if (eaSize(&gSearchWindow.ppSearchTags))
			{			
				pNewDetailsEntry = ui_TextEntryCreateWithStringCombo(pRequest->pcSearchDetails ? pRequest->pcSearchDetails : "", UI_WIDGET(gSearchWindow.pSearchDetailsText)->x, UI_WIDGET(gSearchWindow.pSearchDetailsText)->y, 
					&gSearchWindow.ppSearchTags, true, true, false, false);
			}
			else
			{
				pNewDetailsEntry = ui_TextEntryCreate(pRequest->pcSearchDetails ? pRequest->pcSearchDetails : "", UI_WIDGET(gSearchWindow.pSearchDetailsText)->x, UI_WIDGET(gSearchWindow.pSearchDetailsText)->y);
			}

			ui_WidgetSetDimensions(UI_WIDGET(pNewDetailsEntry), UI_WIDGET(gSearchWindow.pSearchDetailsText)->width, UI_WIDGET(gSearchWindow.pSearchDetailsText)->height); 
			ui_WidgetQueueFree(UI_WIDGET(gSearchWindow.pSearchDetailsText));
			gSearchWindow.pSearchDetailsText = pNewDetailsEntry;

			ui_TextEntrySetChangedCallback(gSearchWindow.pSearchDetailsText, searchSetDetailsTextCB, NULL);
			ui_TextEntrySetEnterCallback(gSearchWindow.pSearchDetailsText, searchTextEnterCB, NULL);
			ui_WindowAddChild(gSearchWindow.pWindow, UI_WIDGET(gSearchWindow.pSearchDetailsText));
		}

	}
}


static void searchResetSearchOptions(void)
{

	ResourceSearchRequest *pRequest = &gSearchWindow.searchRequest;
	if (pRequest)
	{
		int x = 150;
		int y = 25;
		if (gSearchWindow.pSearchTypeCombo)
		{
			ui_WidgetQueueFreeAndNull(&gSearchWindow.pSearchTypeCombo);
		}
		if (gSearchWindow.pSearchDetailsText)
		{
			ui_WidgetQueueFreeAndNull(&gSearchWindow.pSearchDetailsText);
		}
		if (gSearchWindow.pSearchNameText)
		{
			ui_WidgetQueueFreeAndNull(&gSearchWindow.pSearchNameText);
		}

		ui_ComboBoxSetSelectedEnum(gSearchWindow.pSearchModeCombo, pRequest->eSearchMode);

		gSearchWindow.pSearchTypeCombo = resCreateDictionaryComboBox(x, y, 120);
		resDictionaryComboSelectDictionary(gSearchWindow.pSearchTypeCombo, pRequest->pcType, false);
		ui_ComboBoxSetSelectedCallback(gSearchWindow.pSearchTypeCombo, searchTypeComboCB, NULL);		
		ui_WindowAddChild(gSearchWindow.pWindow, UI_WIDGET(gSearchWindow.pSearchTypeCombo));
		x+= 130;

		if (pRequest->eSearchMode == SEARCH_MODE_USAGE || pRequest->eSearchMode == SEARCH_MODE_REFERENCES || pRequest->eSearchMode == SEARCH_MODE_FIELD_SEARCH || pRequest->eSearchMode == SEARCH_MODE_PARENT_USAGE)
		{
			if (pRequest->eSearchMode != SEARCH_MODE_FIELD_SEARCH && pRequest->pcType && resDictGetInfo(pRequest->pcType))
			{			
				gSearchWindow.pSearchNameText = ui_TextEntryCreateWithGlobalDictionaryCombo(pRequest->pcName, x, y, pRequest->pcType, "resourceName", true, true, false, true);
			}
			else if (pRequest->eSearchMode == SEARCH_MODE_FIELD_SEARCH)
			{
				static const char **eaFieldNames = NULL;
				int i = 1;
				ParseTable *pTable = resDictGetParseTable(pRequest->pcType);
				char *estrTempField = NULL;

				if(eaFieldNames){
					eaClear(&eaFieldNames);
				} else {
					eaCreate(&eaFieldNames);
				}

				if(!pTable){
					gSearchWindow.pSearchNameText = ui_TextEntryCreate(pRequest->pcName, x, y);
				} else {
					estrStackCreate(&estrTempField);

					while(pTable[++i].type != TOK_END){
						if(pTable[i].type == TOK_IGNORE)
							break;
						estrClear(&estrTempField);
						estrPrintf(&estrTempField, "%s.%s", pTable[0].name, pTable[i].name);
						eaPush(&eaFieldNames, allocAddString(estrTempField));
					}
					gSearchWindow.pSearchNameText = ui_TextEntryCreateWithStringCombo(pRequest->pcName, x, y, &eaFieldNames, true, true, false, true);
					gSearchWindow.pSearchNameText->cb->bDontSortList = true;
					estrDestroy(&estrTempField);
				}
			}
			else
			{
				gSearchWindow.pSearchNameText = ui_TextEntryCreate(pRequest->pcName, x, y);
			}
			ui_TextEntrySetChangedCallback(gSearchWindow.pSearchNameText, searchSetNameTextCB, NULL);
			ui_TextEntrySetEnterCallback(gSearchWindow.pSearchNameText, searchTextEnterCB, NULL);
			ui_WidgetSetDimensions(UI_WIDGET(gSearchWindow.pSearchNameText), 120, 20);
			ui_WindowAddChild(gSearchWindow.pWindow, UI_WIDGET(gSearchWindow.pSearchNameText));
			x+= 130;
		}

		if (pRequest->eSearchMode == SEARCH_MODE_TAG_SEARCH || pRequest->eSearchMode == SEARCH_MODE_EXPR_SEARCH || pRequest->eSearchMode == SEARCH_MODE_FIELD_SEARCH || pRequest->eSearchMode == SEARCH_MODE_DISP_SEARCH)
		{
			if (pRequest->eSearchMode == SEARCH_MODE_TAG_SEARCH)
			{
				gSearchWindow.ppSearchTags = resGetValidTags(pRequest->pcType);
				if (eaSize(&gSearchWindow.ppSearchTags))
				{
					gSearchWindow.pSearchDetailsText = ui_TextEntryCreateWithStringCombo(pRequest->pcSearchDetails, x, y,
						&gSearchWindow.ppSearchTags, true, true, false, false);
				}
			}
			if (!gSearchWindow.pSearchDetailsText)
			{
				gSearchWindow.pSearchDetailsText = ui_TextEntryCreate(pRequest->pcSearchDetails, x, y);
			}
			ui_TextEntrySetChangedCallback(gSearchWindow.pSearchDetailsText, searchSetDetailsTextCB, NULL);
			ui_TextEntrySetEnterCallback(gSearchWindow.pSearchDetailsText, searchTextEnterCB, NULL);
			ui_WidgetSetDimensions(UI_WIDGET(gSearchWindow.pSearchDetailsText), 120, 20);
			ui_WindowAddChild(gSearchWindow.pWindow, UI_WIDGET(gSearchWindow.pSearchDetailsText));
			x+= 130;
		}

	}
}

static void searchSaveFilterValues(void)
{
	SearchStatusData *pData = eaGet(&gSearchWindow.ppSearchStack, gSearchWindow.iStackLoc);
	if (pData)
	{		
		if (gSearchWindow.pFilterModeCombo)
			pData->filterType = ui_ComboBoxGetSelectedEnum(gSearchWindow.pFilterModeCombo);
		if (gSearchWindow.pFilterText)
			pData->pcFilterString = strdup_ifdiff(ui_TextEntryGetText(gSearchWindow.pFilterText), pData->pcFilterString);
		if (gSearchWindow.pSmallCheckButton)
			pData->bSmall = ui_CheckButtonGetState(gSearchWindow.pSmallCheckButton);
		if (gSearchWindow.pShowCostCheckButton)
			pData->bShowCostumes = ui_CheckButtonGetState(gSearchWindow.pShowCostCheckButton);
		if (gSearchWindow.pList)
		{
			ui_ListGetScrollbarPosition(gSearchWindow.pList, NULL, &pData->yScroll);
		}
		else
		{
			pData->yScroll = 0;
		}
	}
}

static void searchRefresh(void)
{
	SearchStatusData *pData = eaGet(&gSearchWindow.ppSearchStack, gSearchWindow.iStackLoc);
	ui_WidgetGroupQueueFreeAndRemove(&UI_WIDGET(gSearchWindow.pPane)->children);
	gSearchWindow.pList = NULL;
	eaClear(&gSearchWindow.ppListRows);
	if (pData)
	{
		int i;
		UILabel *pLabel;
		char titleString[10240];
		char filterString[1024] = {0};
		sprintf(titleString, "Search: %s (%d results)", pData->pcSearchTitle, eaSize(&pData->result.eaRows));
		ui_WindowSetTitle(gSearchWindow.pWindow, titleString);

		ui_SetActive(UI_WIDGET(gSearchWindow.pBackButton), (gSearchWindow.iStackLoc > 0));
		ui_SetActive(UI_WIDGET(gSearchWindow.pForwardButton), (gSearchWindow.iStackLoc < eaSize(&gSearchWindow.ppSearchStack) - 1));
		ui_SetActive(UI_WIDGET(gSearchWindow.pRefreshButton), true);
		ui_SetActive(UI_WIDGET(gSearchWindow.pShowCostCheckButton), true);
		ui_SetActive(UI_WIDGET(gSearchWindow.pFilterModeCombo), true);
		ui_SetActive(UI_WIDGET(gSearchWindow.pFilterText), true);
		ui_SetActive(UI_WIDGET(gSearchWindow.pSmallCheckButton), true);

		pLabel = ui_LabelCreate(pData->pcDescription, 0, 0);
		ui_WidgetAddChild(UI_WIDGET(gSearchWindow.pPane), UI_WIDGET(pLabel));

		if (gSearchWindow.pFilterModeCombo)
			ui_ComboBoxSetSelectedEnum(gSearchWindow.pFilterModeCombo, pData->filterType);
		if (gSearchWindow.pFilterText)
			ui_TextEntrySetText(gSearchWindow.pFilterText, pData->pcFilterString);
		if (gSearchWindow.pSmallCheckButton)
			ui_CheckButtonSetState(gSearchWindow.pSmallCheckButton, pData->bSmall);
		if (gSearchWindow.pShowCostCheckButton)
			ui_CheckButtonSetState(gSearchWindow.pShowCostCheckButton, pData->bShowCostumes);

		if (pData->pcFilterString && pData->pcFilterString[0])
		{
			sprintf(filterString, "*%s*", pData->pcFilterString);
		}

		for (i = 0; i < eaSize(&pData->result.eaRows); i++)
		{
			ResourceSearchResultRow *pRow = pData->result.eaRows[i];

			if (pData->pcFilterString && pData->pcFilterString[0])
			{
				ResourceInfo *pInfo = resGetInfo(pRow->pcType, pRow->pcName);
				// Filter out results if needed
				if (pData->filterType == SEARCH_FILTER_NAME || !pInfo)
				{
					if (!matchExact(filterString, pRow->pcName))
						continue;
				}
				else if(pData->filterType == SEARCH_FILTER_TYPE)
				{
					if (!matchExact(filterString, pRow->pcType))
						continue;
				}
				else
				{
					bool bFound = false;
					if (pInfo->resourceName && matchExact(filterString, pInfo->resourceName))
						bFound = true;
					if (pInfo->resourceDisplayName && matchExact(filterString, pInfo->resourceDisplayName))
						bFound = true;
					if (pInfo->resourceTags && matchExact(filterString, pInfo->resourceTags))
						bFound = true;
					if (pInfo->resourceNotes && matchExact(filterString, pInfo->resourceNotes))
						bFound = true;
					if (!bFound)
						continue;
				}
			}

			eaPush(&gSearchWindow.ppListRows, pRow);
		}

		if (eaSize(&gSearchWindow.ppListRows))
		{
			UIListColumn *column;
			gSearchWindow.pList = ui_ListCreate(parse_ResourceSearchResultRow, &gSearchWindow.ppListRows, pData->bSmall ? 36: 72);

			ui_WidgetSetPaddingEx(UI_WIDGET(gSearchWindow.pList), 0, 0, 50, 0);
			ui_WidgetSetDimensionsEx(UI_WIDGET(gSearchWindow.pList), 1, 1, UIUnitPercentage, UIUnitPercentage);
			ui_ListSetMultiselect(gSearchWindow.pList,1);
			gSearchWindow.pList->bDrawGrid = 1;
			gSearchWindow.pList->fHeaderHeight = 15;
			ui_ListSetScrollbar(gSearchWindow.pList, 0, pData->yScroll);
			ui_ListSetCellContextCallback(gSearchWindow.pList, searchCellContext, pData);
			ui_ListSetCellActivatedCallback(gSearchWindow.pList, searchCellActivate, pData);

			column = ui_ListColumnCreateText("Type", searchListDrawType, pData);
			ui_ListColumnSetWidth(column, false, 80);
			ui_ListAppendColumn(gSearchWindow.pList, column);

			column = ui_ListColumnCreateParseName("Name", "name", pData);
			ui_ListColumnSetWidth(column, false, 200);
			ui_ListAppendColumn(gSearchWindow.pList, column);

			if (pData->request.eSearchMode == SEARCH_MODE_REFERENCES ||
				pData->request.eSearchMode == SEARCH_MODE_USAGE ||
				pData->request.eSearchMode == SEARCH_MODE_PARENT_USAGE)
			{
				column = ui_ListColumnCreateParseName("Relation", "extraData", pData);
				ui_ListColumnSetWidth(column, false, 100);
				ui_ListAppendColumn(gSearchWindow.pList, column);
			}

			column = ui_ListColumnCreateCallback("Info",
				searchListDrawInfo,
				pData);
			ui_ListAppendColumn(gSearchWindow.pList, column);

			ui_WidgetAddChild(UI_WIDGET(gSearchWindow.pPane), UI_WIDGET(gSearchWindow.pList));
			
			
		}
		else
		{
			pLabel = ui_LabelCreate("No Matching Results!", 0, 50);
			ui_WidgetAddChild(UI_WIDGET(gSearchWindow.pPane), UI_WIDGET(pLabel));
		}

		StructCopy(parse_ResourceSearchRequest, &pData->request, &gSearchWindow.searchRequest, 0, 0, 0);		
	}
	else
	{
		ui_WindowSetTitle(gSearchWindow.pWindow, "Search Window");
	}

	searchResetSearchOptions();
}


static void searchBackButton(UIButton *button, UserData unused)
{
	if (gSearchWindow.iStackLoc > 0)
	{
		searchSaveFilterValues();
		gSearchWindow.iStackLoc--;
		searchRefresh();
	}
}

static void searchForwardButton(UIButton *button, UserData unused)
{
	if (gSearchWindow.iStackLoc < eaSize(&gSearchWindow.ppSearchStack) - 1)
	{
		searchSaveFilterValues();
		gSearchWindow.iStackLoc++;
		searchRefresh();
	}
}

static void searchRefreshButton(UIButton *button, UserData unused)
{
	SearchStatusData *pData = eaGet(&gSearchWindow.ppSearchStack, gSearchWindow.iStackLoc);
	if (pData)
	{
		searchSaveFilterValues();
		RequestResourceSearch(&pData->request);
	}
}


static void searchWindowResize(UIWidget *widget, UserData unused)
{
	ui_WidgetSetPosition(UI_WIDGET(gSearchWindow.pExportButton), ui_WidgetGetWidth(UI_WIDGET(gSearchWindow.pWindow)) - ui_WidgetGetWidth(UI_WIDGET(gSearchWindow.pExportButton)) - 10, 0);
}

static bool searchWindowClose(UIWidget *widget, UserData unused)
{
	gSearchWindow.bWindowVisible = false;
	return true;
}

static void searchWindowModeSelect(UIComboBox *combo, int newvalue, void* cbData)
{
	ResourceSearchRequest *pRequest = &gSearchWindow.searchRequest;
	if (pRequest)
	{
		pRequest->eSearchMode = newvalue;
	}
	searchResetSearchOptions();
}

static void searchWindowFilterModeSelect(UIComboBox *combo, int newvalue, void* cbData)
{
	searchSaveFilterValues();
	searchRefresh();
}

static void searchSetFilterTextCB(UITextEntry *entry, void *userData)
{
	searchSaveFilterValues();
	searchRefresh();
}

static void searchSetSmallCB(UIComboBox *entry, void *userData)
{
	searchSaveFilterValues();
	searchRefresh();
}

static void searchInitWindow(void)
{
	UISkin *pPaneSkin;
	UIButton *pButton;
	if (gSearchWindow.bInit)
	{
		return;
	}

	gSearchWindow.pWindow = ui_WindowCreate("Search Window", 0, 0, 800, 500);
	ui_WindowSetDimensions(gSearchWindow.pWindow, 800, 500, 400, 500);
	elUICenterWindow(gSearchWindow.pWindow);
	ui_WindowSetCloseCallback(gSearchWindow.pWindow, searchWindowClose, NULL);
	ui_WindowSetResizedCallback(gSearchWindow.pWindow, searchWindowResize, NULL);

	pPaneSkin = ui_SkinCreate(NULL);
	ui_SkinSetBackground(pPaneSkin, ColorWhite);

	gSearchWindow.pPane = ui_PaneCreate(0,0,1.0,1.0,UIUnitPercentage, UIUnitPercentage, 0);
	ui_WidgetSetDimensionsEx(UI_WIDGET(gSearchWindow.pPane), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(gSearchWindow.pPane), 0, 0, SEARCH_HEADER_HEIGHT, 0);
	ui_PaneSetStyle(gSearchWindow.pPane, "Default_Capsule_Filled", true, true);
	ui_WidgetSkin(UI_WIDGET(gSearchWindow.pPane), pPaneSkin);
	ui_WindowAddChild(gSearchWindow.pWindow, UI_WIDGET(gSearchWindow.pPane));

	gSearchWindow.pBackButton = ui_ButtonCreate("Back", 10, 0, searchBackButton, NULL);
	ui_WidgetSetTooltipString(UI_WIDGET(gSearchWindow.pBackButton), "Go to the previous search");
	ui_SetActive(UI_WIDGET(gSearchWindow.pBackButton), false);
	ui_WindowAddChild(gSearchWindow.pWindow, UI_WIDGET(gSearchWindow.pBackButton));

	gSearchWindow.pForwardButton = ui_ButtonCreate("Forward", ui_WidgetGetNextX(UI_WIDGET(gSearchWindow.pBackButton)) + 10, 0, searchForwardButton, NULL);
	ui_WidgetSetTooltipString(UI_WIDGET(gSearchWindow.pForwardButton), "Go to the next search");
	ui_SetActive(UI_WIDGET(gSearchWindow.pForwardButton), false);
	ui_WindowAddChild(gSearchWindow.pWindow, UI_WIDGET(gSearchWindow.pForwardButton));

	gSearchWindow.pRefreshButton = ui_ButtonCreate("Refresh", ui_WidgetGetNextX(UI_WIDGET(gSearchWindow.pForwardButton)) + 10, 0, searchRefreshButton, NULL);
	ui_SetActive(UI_WIDGET(gSearchWindow.pRefreshButton), false);
	ui_WindowAddChild(gSearchWindow.pWindow, UI_WIDGET(gSearchWindow.pRefreshButton));

	pButton = ui_ButtonCreate("Search Now", ui_WidgetGetNextX(UI_WIDGET(gSearchWindow.pRefreshButton)) + 10, 0, searchNewSearchButton, NULL);
	ui_WindowAddChild(gSearchWindow.pWindow, UI_WIDGET(pButton));

	gSearchWindow.pExportButton = ui_ButtonCreate("Export Results", ui_WidgetGetNextX(UI_WIDGET(pButton)) + 100, 0, searchNewExportButton, NULL);
	ui_WidgetSetTooltipString(UI_WIDGET(gSearchWindow.pExportButton), "Export the current search results to a CSV file");
	ui_WidgetSetPosition(UI_WIDGET(gSearchWindow.pExportButton), ui_WidgetGetWidth(UI_WIDGET(gSearchWindow.pWindow)) - ui_WidgetGetWidth(UI_WIDGET(gSearchWindow.pExportButton)) - 10, 0);
	ui_WindowAddChild(gSearchWindow.pWindow, UI_WIDGET(gSearchWindow.pExportButton));

	gSearchWindow.pSearchModeCombo = ui_ComboBoxCreateWithEnum(10, 25, 125, ResourceSearchModeEnum, searchWindowModeSelect, NULL);
	ui_ComboBoxSetDefaultDisplayString(gSearchWindow.pSearchModeCombo, "Pick Search Mode");
	ui_WindowAddChild(gSearchWindow.pWindow, UI_WIDGET(gSearchWindow.pSearchModeCombo));

	gSearchWindow.pFilterModeCombo = ui_ComboBoxCreateWithEnum(10, 50, 125, SearchFilterTypeEnum, searchWindowFilterModeSelect, NULL);
	ui_ComboBoxSetDefaultDisplayString(gSearchWindow.pFilterModeCombo, "Pick Filter Mode");
	ui_WindowAddChild(gSearchWindow.pWindow, UI_WIDGET(gSearchWindow.pFilterModeCombo));
	ui_SetActive(UI_WIDGET(gSearchWindow.pFilterModeCombo), false);

	gSearchWindow.pFilterText = ui_TextEntryCreate("", 150, 50);
	ui_TextEntrySetChangedCallback(gSearchWindow.pFilterText, searchSetFilterTextCB, NULL);
	ui_TextEntrySetEnterCallback(gSearchWindow.pFilterText, searchSetFilterTextCB, NULL);
	ui_WidgetSetDimensions(UI_WIDGET(gSearchWindow.pFilterText), 120, 20);
	ui_WindowAddChild(gSearchWindow.pWindow, UI_WIDGET(gSearchWindow.pFilterText));
	ui_SetActive(UI_WIDGET(gSearchWindow.pFilterText), false);
	
	gSearchWindow.pSmallCheckButton = ui_CheckButtonCreate(280, 50, "Small Mode", false);
	ui_CheckButtonSetToggledCallback(gSearchWindow.pSmallCheckButton, searchSetSmallCB, NULL);
	ui_WindowAddChild(gSearchWindow.pWindow, UI_WIDGET(gSearchWindow.pSmallCheckButton));
	ui_SetActive(UI_WIDGET(gSearchWindow.pSmallCheckButton), false);

	gSearchWindow.pShowCostCheckButton = ui_CheckButtonCreate(ui_WidgetGetNextX(UI_WIDGET(gSearchWindow.pSmallCheckButton)) + 10, 50, "Show Costumes", true);
	ui_CheckButtonSetToggledCallback(gSearchWindow.pShowCostCheckButton, searchSetSmallCB, NULL);//TODO change callback
	ui_WindowAddChild(gSearchWindow.pWindow, UI_WIDGET(gSearchWindow.pShowCostCheckButton));
	ui_WidgetSetTooltipString(UI_WIDGET(gSearchWindow.pShowCostCheckButton), "Shows costume renders for items");
	ui_SetActive(UI_WIDGET(gSearchWindow.pShowCostCheckButton), false);
	
	gSearchWindow.bInit = true;

}


void ShowSearchWindow(bool bShow)
{
	searchInitWindow();
	gSearchWindow.bWindowVisible = bShow;

	if (bShow)
	{
		ui_WindowPresent(gSearchWindow.pWindow);
	}
	else
	{
		ui_WindowHide(gSearchWindow.pWindow);
	}
}

bool CheckSearchWindow(void)
{
	return gSearchWindow.bWindowVisible;
}

bool *GetSearchWindowStatus(void)
{
	return &gSearchWindow.bWindowVisible;
}

__forceinline bool SearchStatusData_IsResultAllAssetTypeListSearch(SearchStatusData *pSearchData)
{
	return !pSearchData->request.pcType && pSearchData->request.eSearchMode == SEARCH_MODE_LIST;
}

static void DisplaySearchResourcesResult(ResourceSearchResult *pResult)
{
	SearchStatusData *pSearchData = searchGetRequest(pResult->iRequest);
	// Take the results and display them in Editor Manager

	if (pSearchData)
	{
		ShowSearchWindow(true);
		StructCopy(parse_ResourceSearchResult, pResult, &pSearchData->result, 0, 0, 0);
		searchRefresh();
	}
}

void RequestResourceSearch(ResourceSearchRequest *pRequest)
{
	SearchStatusData *pSearchData;
	static U32 sRequestID = 1;
	char buf[260], buf2[260];

	ShowSearchWindow(true);
	searchSaveFilterValues();

	if (!pRequest->iRequest)
	{
		int i;
		pRequest->iRequest = sRequestID++;
		// Clear out stack ahead of current position
		if (eaSize(&gSearchWindow.ppSearchStack))
		{		
			for (i = gSearchWindow.iStackLoc + 1; i < eaSize(&gSearchWindow.ppSearchStack); i++)
			{	
				StructDestroy(parse_SearchStatusData, gSearchWindow.ppSearchStack[i]);
			}
			eaSetSize(&gSearchWindow.ppSearchStack, gSearchWindow.iStackLoc + 1);
		}
	}

	if (!(pSearchData = searchGetRequest(pRequest->iRequest)))
	{
		SearchStatusData *pOldData;
		pSearchData = StructCreate(parse_SearchStatusData);
		if (eaSize(&gSearchWindow.ppSearchStack))
		{
			pOldData = gSearchWindow.ppSearchStack[eaSize(&gSearchWindow.ppSearchStack) - 1];
			pSearchData->bSmall = pOldData->bSmall;
			pSearchData->filterType = pOldData->filterType;
			//pSearchData->pcFilterString = strdup(pOldData->pcFilterString);
		}
		eaPush(&gSearchWindow.ppSearchStack, pSearchData);
		gSearchWindow.iStackLoc = eaSize(&gSearchWindow.ppSearchStack) - 1;
	}

	assert(pSearchData);
	StructCopy(parse_ResourceSearchRequest, pRequest, &pSearchData->request, 0, 0, 0);

	switch (pRequest->eSearchMode)
	{
		xcase SEARCH_MODE_USAGE:
			sprintf(buf, "This is a list of all the resources that use %s '%s'", resDictGetItemDisplayName(pRequest->pcType), pRequest->pcName);
			sprintf(buf2, "%s '%s' Usage", resDictGetItemDisplayName(pRequest->pcType), pRequest->pcName);
		xcase SEARCH_MODE_PARENT_USAGE:
			sprintf(buf, "This is a list of all the resources that use the parents of %s '%s'", resDictGetItemDisplayName(pRequest->pcType), pRequest->pcName);
			sprintf(buf2, "%s '%s' Parent Usage", resDictGetItemDisplayName(pRequest->pcType), pRequest->pcName);
		xcase SEARCH_MODE_REFERENCES:
			sprintf(buf, "This is a list of all the resources that are referenced by %s '%s'", resDictGetItemDisplayName(pRequest->pcType), pRequest->pcName);
			sprintf(buf2, "%s '%s' References", resDictGetItemDisplayName(pRequest->pcType), pRequest->pcName);
		xcase SEARCH_MODE_LIST:
			sprintf(buf, "This is a list of all %s", resDictGetPluralDisplayName(pRequest->pcType));
			sprintf(buf2, "List of %s", resDictGetPluralDisplayName(pRequest->pcType));
			//I am taking this out because it introduces problems and I don't really see the need for it. -DHOGBERG 1/6/2012
			//if (pRequest->pcSearchDetails)
			//{
			//	pSearchData->pcFilterString = strdup(pRequest->pcSearchDetails);
			//}
		xcase SEARCH_MODE_TAG_SEARCH:
			sprintf(buf, "This is a list of all %s that match the tags '%s'", resDictGetPluralDisplayName(pRequest->pcType), pRequest->pcSearchDetails);
			sprintf(buf2, "Tag Search '%s' in %s", pRequest->pcSearchDetails, resDictGetPluralDisplayName(pRequest->pcType));
		xcase SEARCH_MODE_EXPR_SEARCH:
			sprintf(buf, "This is a list of all %s that contain expressions that match '%s'", resDictGetPluralDisplayName(pRequest->pcType), pRequest->pcSearchDetails);
			sprintf(buf2, "Expression Search '%s' in %s", pRequest->pcSearchDetails, resDictGetPluralDisplayName(pRequest->pcType));
		xcase SEARCH_MODE_DISP_SEARCH:
			sprintf(buf, "This is a list of all %s that contain display names that match '%s'", resDictGetPluralDisplayName(pRequest->pcType), pRequest->pcSearchDetails);
			sprintf(buf2, "Display Name Search '%s' in %s", pRequest->pcSearchDetails, resDictGetPluralDisplayName(pRequest->pcType));
		xcase SEARCH_MODE_FIELD_SEARCH:
			sprintf(buf, "This is a list of all %s that contain fields %s that match '%s'", resDictGetPluralDisplayName(pRequest->pcType), pRequest->pcName, pRequest->pcSearchDetails);
			sprintf(buf2, "Field Search '%s' for field %s in %s", pRequest->pcSearchDetails, pRequest->pcName, resDictGetPluralDisplayName(pRequest->pcType));
	}


	pSearchData->pcDescription = strdup_ifdiff(buf, pSearchData->pcDescription);
	pSearchData->pcSearchTitle = strdup_ifdiff(buf2, pSearchData->pcSearchTitle);

	if (resIsDictionaryFromServer(pRequest->pcType) || 
		SearchStatusData_IsResultAllAssetTypeListSearch(pSearchData))
	{
		ServerCmd_gslSearchResources(pRequest);
	}
	else
	{
		ResourceSearchResult *pResult = handleResourceSearchRequest(pRequest);
		if (pResult)
		{
			DisplaySearchResourcesResult(pResult);
			StructDestroy(parse_ResourceSearchResult, pResult);
		}
	}

}

AUTO_COMMAND ACMD_CLIENTCMD;
void SendSearchResourcesResult(ResourceSearchResult *pResult)
{
	SearchStatusData *pSearchData = searchGetRequest(pResult->iRequest);
	// Take the results and display them in Editor Manager

	if (pSearchData)
	{
		if (SearchStatusData_IsResultAllAssetTypeListSearch(pSearchData))
		{
			// execute the client side part of the search
			continueResourceSearchRequest(&pSearchData->request, pResult);
		}

		DisplaySearchResourcesResult(pResult);
	}
}

AUTO_COMMAND;
void RequestListAll(const char *pDictName)
{
	ResourceDictionaryInfo *pDictInfo = resDictGetInfo(pDictName);
	ResourceSearchRequest resRequest = {0};

	if (!pDictInfo)
	{
		return;
	}

	resRequest.eSearchMode = SEARCH_MODE_LIST;
	resRequest.pcType = (char *)pDictInfo->pDictName;

	RequestResourceSearch(&resRequest);
}

AUTO_COMMAND;
void RequestTagSearch(const char *pDictName, const char *pTagString)
{
	ResourceDictionaryInfo *pDictInfo = resDictGetInfo(pDictName);
	ResourceSearchRequest resRequest = {0};

	if (!pDictInfo)
	{
		return;
	}

	resRequest.eSearchMode = SEARCH_MODE_TAG_SEARCH;
	resRequest.pcType = (char *)pDictInfo->pDictName;
	resRequest.pcSearchDetails = (char *)pTagString;

	RequestResourceSearch(&resRequest);
}

AUTO_COMMAND;
void RequestExpressionSearch(const char *pDictName, const char *pTagString)
{
	ResourceDictionaryInfo *pDictInfo = resDictGetInfo(pDictName);
	ResourceSearchRequest resRequest = {0};

	if (!pDictInfo)
	{
		return;
	}

	resRequest.eSearchMode = SEARCH_MODE_EXPR_SEARCH;
	resRequest.pcType = (char *)pDictInfo->pDictName;
	resRequest.pcSearchDetails = (char *)pTagString;

	RequestResourceSearch(&resRequest);
}

AUTO_COMMAND;
void RequestDisplayNameSearch(const char *pDictName, const char *pValueString)
{
	ResourceDictionaryInfo *pDictInfo = resDictGetInfo(pDictName);
	ResourceSearchRequest resRequest = {0};

	if (!pDictInfo)
	{
		return;
	}

	resRequest.eSearchMode = SEARCH_MODE_DISP_SEARCH;
	resRequest.pcType = (char *)pDictInfo->pDictName;
	resRequest.pcSearchDetails = (char *)pValueString;

	RequestResourceSearch(&resRequest);
}

AUTO_COMMAND;
void RequestFieldSearch(const char *pDictName, const char *pFieldString, const char *pValueString)
{
	ResourceDictionaryInfo *pDictInfo = resDictGetInfo(pDictName);
	ResourceSearchRequest resRequest = {0};

	if (!pDictInfo)
	{
		return;
	}

	resRequest.eSearchMode = SEARCH_MODE_FIELD_SEARCH;
	resRequest.pcType = (char *)pDictInfo->pDictName;
	resRequest.pcName = (char *)pFieldString;
	resRequest.pcSearchDetails = (char *)pValueString;

	RequestResourceSearch(&resRequest);
}



AUTO_COMMAND;
void RequestUsageSearch(const char *pDictName, const char *pResourceName)
{
	ResourceDictionaryInfo *pDictInfo = resDictGetInfo(pDictName);
	ResourceSearchRequest resRequest = {0};

	if (!pDictInfo)
	{
		return;
	}
	
	resRequest.eSearchMode = SEARCH_MODE_USAGE;
	resRequest.pcName = (char *)pResourceName;
	resRequest.pcType = (char *)pDictInfo->pDictName;

	RequestResourceSearch(&resRequest);
}


AUTO_COMMAND;
void RequestReferencesSearch(const char *pDictName, const char *pResourceName)
{
	ResourceDictionaryInfo *pDictInfo = resDictGetInfo(pDictName);
	ResourceSearchRequest resRequest = {0};

	if (!pDictInfo)
	{
		return;
	}

	resRequest.eSearchMode = SEARCH_MODE_REFERENCES;
	resRequest.pcName = (char *)pResourceName;
	resRequest.pcType = (char *)pDictInfo->pDictName;

	RequestResourceSearch(&resRequest);
}


// Searches all assets for any items with the specified name, including partial matches.
AUTO_COMMAND;
void assetSearch(const char * pResourceName)
{
	ResourceSearchRequest resRequest = {0};

	// search all asset types
	resRequest.eSearchMode = SEARCH_MODE_LIST;
	resRequest.pcType = NULL;
	resRequest.pcName = (char *)pResourceName;

	RequestResourceSearch(&resRequest);
}

// Searches a specific type of assets (objectlibrary, dynfxinfo) for any items with the specified name.
AUTO_COMMAND;
void assetSearchType(const char *pDictName, const char * pResourceName)
{
	ResourceDictionaryInfo *pDictInfo = resDictGetInfo(pDictName);
	ResourceSearchRequest resRequest = {0};

	if (!pDictInfo)
	{
		return;
	}

	resRequest.eSearchMode = SEARCH_MODE_LIST;
	resRequest.pcType = (char *)pDictInfo->pDictName;
	resRequest.pcName = (char *)pResourceName;

	RequestResourceSearch(&resRequest);
}




#endif
