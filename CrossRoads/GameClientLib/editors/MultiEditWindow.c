//
// MultiEditWindow.c
//

#ifndef NO_EDITORS

#include "EditorPrefs.h"
#include "EString.h"
#include "Message.h"
#include "MultiEditTable.h"
#include "MultiEditWindow.h"
#include "MultiEditWindow_h_ast.h"

#include "CSVExport.h"
#include "CSVExport_h_ast.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

// This magic line is required if you run in Full Debug to avoid assert
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


//---------------------------------------------------------------------------------------------------
// Toolbars
//---------------------------------------------------------------------------------------------------

static void mew_refreshColGroupsToolbars(MEWindow *pWindow)
{
	int i,j;
	char buf[260];
	const int *eaiSelecteds = NULL;

	for(i=eaSize(&pWindow->eaColumnInfos)-1; i>=0; --i) {
		MEColumnSetInfo *pInfo = pWindow->eaColumnInfos[i];
		eaiSelecteds = ui_ComboBoxGetSelecteds(pInfo->pCombo);

		sprintf(buf, "ColumnSet-%s", pInfo->pcName);
		for(j=eaSize(&pInfo->eaColGroups)-1; j>=0; --j) {
			int iSelected = eaiFind(&eaiSelecteds, j);
			bool bHide = (iSelected < 0);
			bool bAltTint = !bHide && (iSelected & 1);
			if (pInfo->index == -1) {
				METableHideColGroup(pWindow->pTable, pInfo->eaColGroups[j], bHide, bAltTint);
			} else {
				METableHideSubColGroup(pWindow->pTable, pWindow->eaiSubTableIds[pInfo->index], pInfo->eaColGroups[j], bHide, bAltTint);
			}

			// Save preference
			EditorPrefStoreInt(pWindow->pcDisplayName, buf, pInfo->eaColGroups[j], !bHide);
		}
	}
}


static void mew_UIColumnsToolbarShowAll(UIButton *pButton, MEColumnSetInfo *pInfo)
{
	int i;
	int *eaiSelecteds = NULL;

	for(i=eaSize(&pInfo->eaColGroups)-1; i>=0; --i) {
		eaiPush(&eaiSelecteds, i);
	}
	ui_ComboBoxSetSelecteds(pInfo->pCombo, &eaiSelecteds);
	eaiDestroy(&eaiSelecteds);

	mew_refreshColGroupsToolbars(pInfo->pWindow);
}


static void mew_UIColumnsToolbarHideAll(UIButton *pButton, MEColumnSetInfo *pInfo)
{
	int *eaiSelecteds = NULL;
	ui_ComboBoxSetSelecteds(pInfo->pCombo, &eaiSelecteds);

	mew_refreshColGroupsToolbars(pInfo->pWindow);
}


static void mew_UIColumnsToolbarLeft(UIButton *pButton, MEColumnSetInfo *pInfo)
{
	int i;
	int pos = -1;
	const int *eaiSelecteds;

	// find leftmost checked item
	eaiSelecteds = ui_ComboBoxGetSelecteds(pInfo->pCombo);
	for(i=0; i<eaiSize(&eaiSelecteds); ++i) {
		if ((pos < 0) || (eaiSelecteds[i] < pos)) {
			pos = eaiSelecteds[i];
		}
	}

	// Set position to one left of found item
	if (pos < 0) {
		pos = 0;
	} else if (pos == 0) {
		pos = eaSize(&pInfo->eaColGroups)-1;
	} else {
		--pos;
	}

	// Make only that item be set
	ui_ComboBoxSetSelected(pInfo->pCombo, pos);

	mew_refreshColGroupsToolbars(pInfo->pWindow);
}


static void mew_UIColumnsToolbarRight(UIButton *pButton, MEColumnSetInfo *pInfo)
{
	int i;
	int pos = -1;
	const int *eaiSelecteds;

	// find leftmost checked item
	eaiSelecteds = ui_ComboBoxGetSelecteds(pInfo->pCombo);
	for(i=0; i<eaiSize(&eaiSelecteds); ++i) {
		if ((pos < 0) || (eaiSelecteds[i] < pos)) {
			pos = eaiSelecteds[i];
		}
	}

	// Set position to one right of found item
	if (pos < 0) {
		pos = 0;
	} else if (pos == eaSize(&pInfo->eaColGroups)-1) {
		pos = 0;
	} else {
		++pos;
	}

	// Make only that item be set
	ui_ComboBoxSetSelected(pInfo->pCombo, pos);

	mew_refreshColGroupsToolbars(pInfo->pWindow);
}


static void mew_UIColumnsToolbarToggleColumn(UIComboBox *pCombo, MEColumnSetInfo *pInfo)
{
	mew_refreshColGroupsToolbars(pInfo->pWindow);
}


static void mew_UIHideUnusedChanged(UICheckButton *pCheck, MEWindow *pWindow)
{
	EditorPrefStoreInt(pWindow->pcDisplayName, "Options", "HideUnused", pCheck->state);
	METableSetHideUnused(pWindow->pTable, pCheck->state);
}


static F32 mew_addColumnsToolbarItems(MEWindow *pWindow, EMToolbar *pToolbar, F32 x, int index, char *pcName, char **eaColGroups)
{
	UILabel *pLabel;
	UIButton *pButton;
	UIComboBox *pCombo;
	MEColumnSetInfo *pInfo;
	int i;
	char buf[260];
	int *eaiSelecteds = NULL;

	pInfo = calloc(1, sizeof(MEColumnSetInfo));
	pInfo->pWindow = pWindow;
	pInfo->index = index;
	pInfo->pcName = strdup(pcName);
	pInfo->eaColGroups = eaColGroups;
	eaPush(&pWindow->eaColumnInfos, pInfo);

	pLabel = ui_LabelCreate(pcName, x, 0);
	emToolbarAddChild(pToolbar, pLabel, true);
	x += pLabel->widget.width + 5;

	pButton = ui_ButtonCreate("Show", x, 0, mew_UIColumnsToolbarShowAll, pInfo);
	ui_WidgetSetHeightEx(UI_WIDGET(pButton), 1.0, UIUnitPercentage);
	emToolbarAddChild(pToolbar, pButton, true);
	x += pButton->widget.width + 5;

	pButton = ui_ButtonCreate("Hide", x, 0, mew_UIColumnsToolbarHideAll, pInfo);
	ui_WidgetSetHeightEx(UI_WIDGET(pButton), 1.0, UIUnitPercentage);
	emToolbarAddChild(pToolbar, pButton, true);
	x += pButton->widget.width + 5;

	// Create combo even if not going to show it
	pCombo = ui_ComboBoxCreate(x, 0, 80, NULL, &pInfo->eaColGroups, NULL);
	ui_ComboBoxSetMultiSelect(pCombo, true);
	ui_ComboBoxSetSelectedCallback(pCombo, mew_UIColumnsToolbarToggleColumn, pInfo);
	pCombo->bShowCheckboxes = true;
	sprintf(buf, "ColumnSet-%s", pcName);
	for(i=0; i<eaSize(&eaColGroups); ++i) {
		if (EditorPrefGetInt(pWindow->pcDisplayName, buf, eaColGroups[i], 1)) {
			eaiPush(&eaiSelecteds, i);
		}
	}
	ui_ComboBoxSetSelecteds(pCombo, &eaiSelecteds);
	eaiDestroy(&eaiSelecteds);
	pInfo->pCombo = pCombo;

	if (eaSize(&eaColGroups) > 1) {
		// This sets up the combo to be shown only if more than one item
		ui_WidgetSetHeightEx(UI_WIDGET(pCombo), 1.0, UIUnitPercentage);
		emToolbarAddChild(pToolbar, pCombo, true);
		x += pCombo->widget.width + 5;

		pButton = ui_ButtonCreate("<", x, 0, mew_UIColumnsToolbarLeft, pInfo);
		ui_WidgetSetHeightEx(UI_WIDGET(pButton), 1.0, UIUnitPercentage);
		emToolbarAddChild(pToolbar, pButton, true);
		x += pButton->widget.width + 5;

		pButton = ui_ButtonCreate(">", x, 0, mew_UIColumnsToolbarRight, pInfo);
		ui_WidgetSetHeightEx(UI_WIDGET(pButton), 1.0, UIUnitPercentage);
		emToolbarAddChild(pToolbar, pButton, true);
		x += pButton->widget.width + 5;

	}

	mew_refreshColGroupsToolbars(pWindow);
	return x + 10;
}


static void mew_initEditorToolbars(MEWindow *pWindow)
{
	EMToolbar *pToolbar;

	// File toolbar
	pToolbar = emToolbarCreateFileToolbar(EM_FILE_TOOLBAR_NEW|EM_FILE_TOOLBAR_OPEN);
	eaPush(&pWindow->pEditorDoc->emDoc.editor->toolbars, pToolbar);
	eaPush(&pWindow->pEditorDoc->emDoc.editor->toolbars, emToolbarCreateWindowToolbar());
}


//---------------------------------------------------------------------------------------------------
// Menus
//---------------------------------------------------------------------------------------------------


static MEWindow *mew_getWindowFromEM(void)
{
	MultiEditEMDoc *pDoc = (MultiEditEMDoc*)emGetActiveEditorDoc();
	if (pDoc && pDoc->pWindow) {
		return pDoc->pWindow;
	}
	return NULL;
}

#endif

AUTO_COMMAND;
void ME_CheckoutAll(void)
{
#ifndef NO_EDITORS
	MEWindow *pWindow = mew_getWindowFromEM();
	if (pWindow) {
		METableCheckOutAll(pWindow->pTable);
	}
#endif
}


AUTO_COMMAND;
void ME_UndoCheckoutAll(void)
{
#ifndef NO_EDITORS
	MEWindow *pWindow = mew_getWindowFromEM();
	if (pWindow) {
		METableUndoCheckOutAll(pWindow->pTable);
	}
#endif
}


AUTO_COMMAND;
void ME_RevertAll(void)
{
#ifndef NO_EDITORS
	MEWindow *pWindow = mew_getWindowFromEM();
	if (pWindow) {
		METableRevertAll(pWindow->pTable);
	}
#endif
}


AUTO_COMMAND;
void ME_SaveAll(void)
{
#ifndef NO_EDITORS
	EMEditorDoc *pDoc = emGetActiveEditorDoc();
	emSaveDoc(pDoc);
#endif
}


AUTO_COMMAND;
void ME_CloseAll(void)
{
#ifndef NO_EDITORS
	EMEditorDoc *pDoc = emGetActiveEditorDoc();
	emCloseAllSubDocs(pDoc);
#endif
}


AUTO_COMMAND;
void ME_EditField(void)
{
#ifndef NO_EDITORS
	MEWindow *pWindow = mew_getWindowFromEM();
	if (pWindow) {
		METableEditFields(pWindow->pTable);
	}
#endif
}


AUTO_COMMAND;
void ME_RevertField(void)
{
#ifndef NO_EDITORS
	MEWindow *pWindow = mew_getWindowFromEM();
	if (pWindow) {
		METableRevertFields(pWindow->pTable);
	}
#endif
}


AUTO_COMMAND;
void ME_InheritField(void)
{
#ifndef NO_EDITORS
	MEWindow *pWindow = mew_getWindowFromEM();
	if (pWindow) {
		METableInheritFields(pWindow->pTable);
	}
#endif
}


AUTO_COMMAND;
void ME_NoInheritField(void)
{
#ifndef NO_EDITORS
	MEWindow *pWindow = mew_getWindowFromEM();
	if (pWindow) {
		METableNoInheritFields(pWindow->pTable);
	}
#endif
}

#ifndef NO_EDITORS

static void mew_initEditorMenus(MEWindow *pWindow)
{
	char buf[260];

	// Register the menu options against this editor
	sprintf(buf, "Checkout All %s", pWindow->pcDisplayNamePlural);
	emMenuItemCreate(pWindow->pEditorDoc->emDoc.editor, "me_checkoutall", buf, NULL, NULL, "ME_CheckoutAll");
	sprintf(buf, "Undo Checkout All %s", pWindow->pcDisplayNamePlural);
	emMenuItemCreate(pWindow->pEditorDoc->emDoc.editor, "me_undocheckoutall", buf, NULL, NULL, "ME_UndoCheckoutAll");
	sprintf(buf, "Revert All %s", pWindow->pcDisplayNamePlural);
	emMenuItemCreate(pWindow->pEditorDoc->emDoc.editor, "me_revertall", buf, NULL, NULL, "ME_RevertAll");
	sprintf(buf, "Close All %s", pWindow->pcDisplayNamePlural);
	emMenuItemCreate(pWindow->pEditorDoc->emDoc.editor, "me_closeall", buf, NULL, NULL, "ME_CloseAll");
	sprintf(buf, "Save All %s", pWindow->pcDisplayNamePlural);
	emMenuItemCreate(pWindow->pEditorDoc->emDoc.editor, "me_saveall", buf, NULL, NULL, "ME_SaveAll");

	emMenuItemCreate(pWindow->pEditorDoc->emDoc.editor, "me_editfield", "Edit Cells", METableCanEditFields, pWindow->pTable, "ME_EditField");
	emMenuItemCreate(pWindow->pEditorDoc->emDoc.editor, "me_revertfield", "Revert Cells", METableCanRevertFields, pWindow->pTable, "ME_RevertField");
	emMenuItemCreate(pWindow->pEditorDoc->emDoc.editor, "me_inheritfield", "Inherit from Parent", METableCanInheritFields, pWindow->pTable, "ME_InheritField");
	emMenuItemCreate(pWindow->pEditorDoc->emDoc.editor, "me_noinheritfield", "Don't Inherit from Parent", METableCanNoInheritFields, pWindow->pTable, "ME_NoInheritField");
	emMenuItemCreate(pWindow->pEditorDoc->emDoc.editor, "me_openfieldineditor", "Open in Editor", METableCanOpenFieldsInEditor, pWindow->pTable, "ME_OpenFieldInEditor");

	// Register the menus against this editor
	emMenuRegister(pWindow->pEditorDoc->emDoc.editor, emMenuCreate(pWindow->pEditorDoc->emDoc.editor, "File", "me_checkoutall", "me_undocheckoutall", "me_revertall", "me_closeall", "me_saveall", NULL));
	emMenuRegister(pWindow->pEditorDoc->emDoc.editor, emMenuCreate(pWindow->pEditorDoc->emDoc.editor, "Edit", "me_editfield", "me_revertfield", "me_inheritfield", "me_noinheritfield", "me_openfieldineditor", NULL));
}


//---------------------------------------------------------------------------------------------------
// Main List
//---------------------------------------------------------------------------------------------------


static bool mew_UICloseWindow(UIWindow *pUIWindow, MEWindow *pWindow)
{
	// Have EM close the doc and window
	emCloseDoc((EMEditorDoc*)pWindow->pEditorDoc);
	return false;
}


static void mew_initList(MEWindow *pWindow, char *pcDisplayName, char *pcTypeName, DictionaryHandle hDict, 
						 ParseTable *pParseTable, char *pcNamePTName, char *pcFilePTName, char *pcScopePTName)
{
	UIWidget *pWidget;

	// Create the table
	pWindow->pTable = METableCreate(pcDisplayName, pcTypeName, hDict, pParseTable, pcNamePTName, pcFilePTName, pcScopePTName, pWindow->pEditorDoc);

	// Add table widget to the window
	pWidget = METableGetWidget(pWindow->pTable);
	pWidget->x = 0;
	pWidget->y = 0;
	pWidget->width = pWidget->height = 1.f;
	pWidget->widthUnit = pWidget->heightUnit = UIUnitPercentage;

	ui_WindowAddChild(pWindow->pUIWindow,pWidget);
}



//---------------------------------------------------------------------------------------------------
// General API Calls
//---------------------------------------------------------------------------------------------------

bool met_deleteRowContinue(EMEditor *pEditor, const char *pcObjName, void *pObject, EMResourceState eState, METable *pTable, bool bSuccess);
bool met_saveRowContinue(EMEditor *pEditor, const char *pcObjName, void *pObject, EMResourceState eState, METable *pTable, bool bSuccess);


MEWindow *MEWindowCreate(char *pcWinTitle, char *pcDisplayName, char *pcDisplayNamePlural, char *pcTypeName,
						 DictionaryHandle hDict, 
						 ParseTable *pParseTable, char *pcNamePTName, char *pcFilePTName,
						 char *pcScopePTName, MultiEditEMDoc *pEditorDoc)
{
	MEWindow *pWindow;
	int i;

	// Initialize the structure
	pWindow = (MEWindow*)calloc(sizeof(MEWindow),1);
	pWindow->pEditorDoc = pEditorDoc;
	pWindow->pUIWindow = ui_WindowCreate(pcWinTitle, 150, 200, 750, 600);
	pWindow->pParseTable = pParseTable;
	pWindow->pcDisplayName = pcDisplayName;
	pWindow->pcDisplayNamePlural = pcDisplayNamePlural;
	pWindow->hDict = hDict;
	pWindow->pcNamePTName = pcNamePTName;
	pWindow->iNameIndex;
	ui_WindowSetCloseCallback(pWindow->pUIWindow, mew_UICloseWindow, pWindow);

	mew_initList(pWindow, pcDisplayName, pcTypeName, hDict, pParseTable, pcNamePTName, pcFilePTName, pcScopePTName);
	mew_initEditorMenus(pWindow);
	mew_initEditorToolbars(pWindow);

	// Figure out parse table index for the name column
	if (pWindow->pcNamePTName) {
		FORALL_PARSETABLE(pWindow->pParseTable, i) {
			if (!stricmp(pWindow->pParseTable[i].name, pWindow->pcNamePTName)) {
				pWindow->iNameIndex = i;
				break;
			}
		}
		if (pWindow->iNameIndex < 0) {
			assertmsg(0,"The parse table field name provided to an MEWindow does not exist");
		}
	}

	if (pWindow->pEditorDoc && pWindow->pEditorDoc->emDoc.editor && pWindow->pTable->pcTypeName)
	{
		emAddDictionaryStateChangeHandler(pWindow->pEditorDoc->emDoc.editor, pWindow->pTable->pcTypeName, NULL, NULL, met_saveRowContinue, met_deleteRowContinue, pWindow->pTable);
	}
	return pWindow;
}


void MEWindowDestroy(MEWindow *pWindow)
{
	// Free the table
	METableDestroy(pWindow->pTable);

	// Free the window
	ui_WidgetQueueFree((UIWidget*)pWindow->pUIWindow);

	// Free the ids
	eaiDestroy(&pWindow->eaiSubTableIds);

	// Release this object's memory
	free(pWindow);
}


void MEWindowExit(MEWindow *pWindow)
{
	// Close objects and hide the window
	METableCloseAll(pWindow->pTable);

	ui_WindowHide(pWindow->pUIWindow);
}


void MEWindowLostFocus(MEWindow *pWindow)
{
	METableCloseEditRow(pWindow->pTable);
}


UIWindow *MEWindowGetUIWindow(MEWindow *pWindow)
{
	return pWindow->pUIWindow;
}


void MEWindowOpenObject(MEWindow *pWindow, char *pcObjName)
{
	if (pcObjName && resIsEditingVersionAvailable(pWindow->hDict, pcObjName)) {
		METableAddRow(pWindow->pTable, pcObjName, 1, 1);
	} else {
		resSetDictionaryEditMode(pWindow->pTable->hDict, true);
		resSetDictionaryEditMode(gMessageDict, true);
		if (pcObjName) {
			emSetResourceState(pWindow->pEditorDoc->emDoc.editor, pcObjName, EMRES_STATE_OPENING);
		}
		resRequestOpenResource(pWindow->pTable->hDict, pcObjName);
	}
}


EMTaskStatus MEWindowSaveAll(MEWindow *pWindow)
{
	return METableSaveAll(pWindow->pTable);
}


EMTaskStatus MEWindowSaveObject(MEWindow *pWindow, void *pObject)
{
	return METableSaveObject(pWindow->pTable, pObject);
}


void MEWindowCloseObject(MEWindow *pWindow, void *pObject)
{
	METableCloseObject(pWindow->pTable, pObject);
}


void MEWindowRevertObject(MEWindow *pWindow, void *pObject)
{
	METableRevertObject(pWindow->pTable, pObject);
}


void MEWindowSetCreateCallback(MEWindow *pWindow, MECreateFunc cbCreate)
{
	pWindow->cbCreate = cbCreate;
}


void MEWindowInitTableMenus(MEWindow *pWindow)
{
	char **eaColGroups = NULL;
	char **eaTableNames = NULL;
	int i;
	F32 x = 0.0;
	EMToolbar *pToolbar;
	UICheckButton *pCheck;

	// Column Toolbar
	pToolbar = emToolbarCreate(200);

	// Get column groups from the main table
	METableGetColGroupNames(pWindow->pTable, &eaColGroups);
	x = mew_addColumnsToolbarItems(pWindow, pToolbar, x, -1, "Columns", eaColGroups);
	eaColGroups = NULL;

	// Get sub tables
	pWindow->eaiSubTableIds = NULL;
	METableGetSubTableInfo(pWindow->pTable, &pWindow->eaiSubTableIds, &eaTableNames);
	for(i=0; i<eaiSize(&pWindow->eaiSubTableIds); ++i) {
		METableGetSubColGroupNames(pWindow->pTable, pWindow->eaiSubTableIds[i], &eaColGroups);
		x = mew_addColumnsToolbarItems(pWindow, pToolbar, x, i, eaTableNames[i], eaColGroups);
		eaColGroups = NULL;
	}

	pCheck = ui_CheckButtonCreate(x, 0, "Hide unused", EditorPrefGetInt(pWindow->pcDisplayName, "Options", "HideUnused", true));
	ui_CheckButtonSetToggledCallback(pCheck, mew_UIHideUnusedChanged, pWindow);
	emToolbarAddChild(pToolbar, pCheck, true);
	x += pCheck->widget.width + 5;
	METableSetHideUnused(pWindow->pTable, pCheck->state);

	eaPush(&pWindow->pEditorDoc->emDoc.editor->toolbars, pToolbar);

	// Clean up
	eaDestroy(&eaTableNames);
}

//////////////////////////////////////////////////////////////////////////
// CSV Export Stuff
//////////////////////////////////////////////////////////////////////////

void csvExportSetup(MEWindow *pWindow, char ***peaPowerDefList, CSVColumn ***peaColumns, CSVExportType eExportType, ColumnsExport eColumns)
{
	int rowIdx, colIdx;

	const int numCols = eaSize(&pWindow->pTable->eaCols);
	const int numRows = eaSize(&pWindow->pTable->eaRows);

	//The columns that are selected, only used if eColumns == Selected
	bool *bSelectedCol = calloc(numCols, sizeof(bool));
	memset(bSelectedCol, 1, sizeof(bool) * numCols);

	if(eExportType == kCSVExport_Open || eColumns == kColumns_Selected)
	{
		for(rowIdx = 0; rowIdx < numRows; rowIdx++)
		{
			METableRow *pRow = (METableRow*)pWindow->pTable->eaRows[rowIdx];


			//Push all open powers on the list of powers to export
			if(pRow && pRow->pObject && eExportType == kCSVExport_Open)
			{
				eaPush(peaPowerDefList, StructAllocString(met_getObjectName(pWindow->pTable, rowIdx)));
			}

			//If you're only interested in selected, find all the fully selected columns
			if(eColumns == kColumns_Selected)
			{
				for(colIdx=0; colIdx<numCols; ++colIdx)
				{
					if(bSelectedCol[colIdx] == false)
						continue;

					if(!ui_ListIsSelected(pWindow->pTable->pList, pWindow->pTable->eaCols[colIdx]->pListColumn, rowIdx))
					{
						bSelectedCol[colIdx] = false;
					}
				}
			}
		}
	}

	for(colIdx=0; colIdx<numCols; ++colIdx)
	{
		MEColData *pCol = pWindow->pTable->eaCols[colIdx];
		CSVColumn *pColumn;

		//Ignore the key field
		if(pCol->flags & ME_STATE_HIDDEN)
			continue;

		//Ignore the hidden columns if you don't want to export them
		if( eColumns != kColumns_All &&
			(pCol->bGroupHidden || pCol->bSmartHidden))
		{
			continue;
		}

		//If we're only interested in selected, ignore those that aren't
		if( eColumns == kColumns_Selected && !bSelectedCol[colIdx])
		{
			continue;
		}

		pColumn = StructCreate(parse_CSVColumn);
		pColumn->pchTitle = StructAllocString( ui_ListColumnGetTitle(pCol->pListColumn) );	

		//If this column is populated by a parse table name, find which column it is in the parse table
		if (pCol->pcPTName)
		{
			ParseTable *pFieldParseTable = NULL;
			int iFieldCol = -1;
			void *pFieldData = NULL, *pFieldOrigData = NULL, *pFieldParentData = NULL;
			char buf[1024];
			char *pos;

			strcpy(buf, pCol->pcPTName);

			//if this is the editor copy of the message, deleted it.
			pos = strstri(buf, ".editorcopy");
			if(pos)
			{
				*pos = '\0';
				strcat(buf, ".message");
			}

			pColumn->pchObjPath = StructAllocString(buf);

			switch(pCol->eType)
			{
			case kMEFieldType_FlagCombo:
				pColumn->eType = kCSVColumn_Flag;
				break;
			case kMEFieldType_Message:
				pColumn->eType = kCSVColumn_Message;
				break;
			case kMEFieldType_BooleanCombo:
				pColumn->eType = kCSVColumn_Boolean;
				break;
			default:
				{
					if(pCol->eType == kMEFieldTypeEx_Expression)
					{
						pColumn->eType = kCSVColumn_Expression;
					}
					else
					{
						pColumn->eType = kCSVColumn_Text;
					}
					break;
				}
			}

			eaPush(peaColumns, pColumn);
		}
		else if(pCol->bParentCol)
		{
			pColumn->pchObjPath = NULL;
			pColumn->eType = kCSVColumn_Parent;
			eaPush(peaColumns, pColumn);
		}
	}

	free(bSelectedCol);
}


void csvExportOkayClicked(UIButton *pButton, UserData data)
{
	char *pchGroup = NULL;
	CSVConfigWindow *pCSVWindow = (CSVConfigWindow*)data;

	CSVConfig *pServerCSVConfig = StructCreate(parse_CSVConfig);

	UIRadioButton *pRadioButton;
	CSVExportType eExportType = kCSVExport_Open;
	ColumnsExport eColumns = kColumns_All;

	pServerCSVConfig->pchFileName = StructAllocString(ui_FileNameEntryGetFileName(pCSVWindow->pFileEntry));

	estrStackCreate(&pchGroup);
	ui_ComboBoxGetSelectedAsString(pCSVWindow->pGroupCombo, &pchGroup);
	pServerCSVConfig->pchScope = StructAllocString(pchGroup);
	estrDestroy(&pchGroup);

	pRadioButton = ui_RadioButtonGroupGetActive(pCSVWindow->ExportTypeGroup);
	if(pRadioButton)
		eExportType = *((CSVExportType*)pRadioButton->toggledData);

	pRadioButton = ui_RadioButtonGroupGetActive(pCSVWindow->ColumnsGroup);
	if(pRadioButton)
		eColumns = *((ColumnsExport*)pRadioButton->toggledData);

	csvExportSetup(	pCSVWindow->pMEWindow,
		&pServerCSVConfig->eaRefList, 
		&pServerCSVConfig->eaColumns,
		eExportType,
		eColumns);
	if(pCSVWindow->okayClickedf)
		pCSVWindow->okayClickedf(pServerCSVConfig);

	//Tell the server to export the powers/columns matching the filter
	ServerCmd_referent_CSVExport(pServerCSVConfig);

	StructDestroy(parse_CSVConfig, pServerCSVConfig);

	ui_WindowClose(pCSVWindow->pWindow);
}

void csvExportCancelClicked(UIButton *pButton, UserData data)
{
	ui_WindowClose(((CSVConfigWindow*)data)->pWindow);
}


//Puts in all the UI elements
void initCSVConfigWindow(CSVConfigWindow *pCSVWindow)
{
	F32 y = 0;
	F32 x = 0;
	S32 i;

	UIFileNameEntry *pFileEntry;
	UIButton *pOkayButton;
	UIButton *pCancelButton;
	UILabel *pFileNameLabel, *pExportLabel, *pColumnLabel;
	UIRadioButtonGroup *pExportTypeRBGroup;
	UIRadioButtonGroup *pColumnRBGroup;
	UIRadioButton *pRadioButton;
	UIComboBox *pGroupCombo;
	UIPane *pExportPane;
	UIPane *pColumnPane;

	F32 fBorderWidth = 8;

	char chDocumentsDir[1024];
	char chFileName[1024];

	__time32_t now = _time32(NULL);
	struct tm today;

	_localtime32_s(&today, &now);
	
	CSV_GetDocumentsDir(chDocumentsDir);

	sprintf(chFileName,"%s/%s.%4d%02d%02d.%02d%02d%02d.csv",
		chDocumentsDir,
		pCSVWindow->pchBaseFilename ? pCSVWindow->pchBaseFilename : "CSVExport",
		today.tm_year+1900,
		today.tm_mon+1,
		today.tm_mday,
		today.tm_hour,
		today.tm_min,
		today.tm_sec);

	//START file entry
	y+= 10;
	pFileNameLabel = ui_LabelCreate("File", x, y);
	ui_WidgetSetWidthEx(UI_WIDGET(pFileNameLabel), 0.12, UIUnitPercentage);
	ui_WindowAddChild(pCSVWindow->pWindow, pFileNameLabel);

	pFileEntry = ui_FileNameEntryCreate(chFileName, "CSV output file", "C:/", chDocumentsDir, ".csv", UIBrowseNewOrExisting);
	ui_WidgetSetPositionEx(UI_WIDGET(pFileEntry), 0, y, 0.12, 0, UITopLeft);
	ui_WidgetSetWidthEx(UI_WIDGET(pFileEntry), 0.88, UIUnitPercentage);
	ui_WindowAddChild(pCSVWindow->pWindow, pFileEntry);
	x=0; y+= pFileEntry->widget.height + 5;
	pCSVWindow->pFileEntry = pFileEntry;
	//END file entry

	//START Export Pane
	pExportLabel = ui_LabelCreate("Export Settings", x, y);
	ui_WidgetSetWidthEx(UI_WIDGET(pExportLabel), 0.2, UIUnitPercentage);
	ui_WindowAddChild(pCSVWindow->pWindow, pExportLabel);

	x=0; y+= pExportLabel->widget.height + 2;

	pExportPane = ui_PaneCreate(x+2, y, pCSVWindow->pWindow->widget.width - 4, 50, UIUnitFixed,UIUnitFixed,0);
	ui_PaneSetStyle(pExportPane, "Game_FrameBasic", true, true);
	ui_WindowAddChild(pCSVWindow->pWindow, pExportPane);

	x=fBorderWidth;
	pExportTypeRBGroup = ui_RadioButtonGroupCreate();
	for(i=0; i<kCSVExport_COUNT;i++)
	{
		pRadioButton = ui_RadioButtonCreate(x, y, StaticDefineIntRevLookup(CSVExportTypeEnum, i), pExportTypeRBGroup);
		pRadioButton->toggledData = malloc(sizeof(CSVExportType));
		*(CSVExportType*)(pRadioButton->toggledData) = i;
		pRadioButton->toggledF = NULL;
		ui_WindowAddChild(pCSVWindow->pWindow, pRadioButton);

		if(i == pCSVWindow->eDefaultExportType)
		{
			ui_RadioButtonGroupSetActive(pExportTypeRBGroup, pRadioButton);
		}

		if(i == kCSVExport_Group)
		{
			x+= pRadioButton->widget.width + 2;
			pCSVWindow->peaModel = calloc(1, sizeof(char**));
			eaInsert(pCSVWindow->peaModel, StructAllocString("*"), 0);
			pGroupCombo = ui_ComboBoxCreate(x, y, pCSVWindow->pWindow->widget.width - x - fBorderWidth, NULL, pCSVWindow->peaModel, NULL);
			pCSVWindow->pGroupCombo = pGroupCombo;
			ui_WindowAddChild(pCSVWindow->pWindow, pGroupCombo);
		}

		x=fBorderWidth; y+= pRadioButton->widget.height+2;
	}

	pCSVWindow->ExportTypeGroup = pExportTypeRBGroup;

	y = pExportPane->widget.y + pExportPane->widget.height;
	//END Export Pane

	x=0; y+=3;

	//START Column Pane
	pColumnLabel = ui_LabelCreate("Columns to Export", x, y);
	ui_WidgetSetWidthEx(UI_WIDGET(pColumnLabel), 0.2, UIUnitPercentage);
	ui_WindowAddChild(pCSVWindow->pWindow, pColumnLabel);

	x=0; y+= pColumnLabel->widget.height + 2;

	pColumnPane = ui_PaneCreate(x+2, y, 0.50f, 65, UIUnitPercentage,UIUnitFixed,0);
	ui_PaneSetStyle(pColumnPane, "Game_FrameBasic", true, true);
	ui_WindowAddChild(pCSVWindow->pWindow, pColumnPane);

	x=fBorderWidth;

	pColumnRBGroup = ui_RadioButtonGroupCreate();

	for(i=0; i<kColumns_COUNT;i++)
	{
		pRadioButton = ui_RadioButtonCreate(x, y, StaticDefineIntRevLookup(ColumnsExportEnum, i), pColumnRBGroup);
		pRadioButton->toggledData = malloc(sizeof(ColumnsExport));
		*(ColumnsExport*)(pRadioButton->toggledData) = i;
		pRadioButton->toggledF = NULL;
		ui_WindowAddChild(pCSVWindow->pWindow, pRadioButton);

		if(i == pCSVWindow->eDefaultExportColumns)
		{
			ui_RadioButtonGroupSetActive(pColumnRBGroup, pRadioButton);
		}

		x=fBorderWidth; y+= pRadioButton->widget.height+2;
	}

	pCSVWindow->ColumnsGroup = pColumnRBGroup;

	y = pColumnPane->widget.y + pColumnPane->widget.height;
	//END Column Pane

	x=0; y+= 3;


	//Add the okay/cancel buttons
	pCancelButton = ui_ButtonCreate("Cancel", 0, 0, csvExportCancelClicked, pCSVWindow);
	ui_WidgetSetPositionEx(UI_WIDGET(pCancelButton), 0, 0, 0, 0,UIBottomRight);
	ui_WindowAddChild(pCSVWindow->pWindow, pCancelButton);
	pOkayButton = ui_ButtonCreate("Okay", 0, 0, csvExportOkayClicked, pCSVWindow);
	ui_WidgetSetPositionEx(UI_WIDGET(pOkayButton), pCancelButton->widget.width+2, 0, 0, 0,UIBottomRight);
	ui_WindowAddChild(pCSVWindow->pWindow, pOkayButton);
}

//Does any setup, maybe detects selections in the list
void setupCSVConfigWindow(CSVConfigWindow *pCSVWindow, MEWindow *pMEWindow, OkayClickedFunc okayClickedFunc)
{
	//Setup the file entry's default
	char chDocumentsDir[1024];
	char chFileName[1024];

	__time32_t now = _time32(NULL);
	struct tm today;

	_localtime32_s(&today, &now);

	CSV_GetDocumentsDir(chDocumentsDir);

	sprintf(chFileName,"%s/%s.%4d%02d%02d.%02d%02d%02d.csv",
		chDocumentsDir,
		pCSVWindow->pchBaseFilename ? pCSVWindow->pchBaseFilename : "CSVExport",
		today.tm_year+1900,
		today.tm_mon+1,
		today.tm_mday,
		today.tm_hour,
		today.tm_min,
		today.tm_sec);

	ui_FileNameEntrySetBrowseValues(pCSVWindow->pFileEntry, "CSV output file", "C:/", chDocumentsDir, ".csv", UIBrowseNewOrExisting);
	ui_FileNameEntrySetFileName(pCSVWindow->pFileEntry, chFileName);

	//Select one of the radio buttons if none are selected
	if(	eaSize(&pCSVWindow->ColumnsGroup->buttons) && 
		ui_RadioButtonGroupGetActive(pCSVWindow->ColumnsGroup) == NULL)
	{
		ui_RadioButtonGroupSetActive(pCSVWindow->ColumnsGroup, pCSVWindow->ColumnsGroup->buttons[0]);
	}

	if(	eaSize(&pCSVWindow->ExportTypeGroup->buttons) && 
		ui_RadioButtonGroupGetActive(pCSVWindow->ExportTypeGroup) == NULL)
	{
		ui_RadioButtonGroupSetActive(pCSVWindow->ExportTypeGroup, pCSVWindow->ExportTypeGroup->buttons[0]);
	}

	//Update the scope of the powers
	resGetUniqueScopes(pMEWindow->pTable->hDict, pCSVWindow->peaModel);
	eaInsert(pCSVWindow->peaModel, StructAllocString("*"), 0);
	ui_ComboBoxSetModel(pCSVWindow->pGroupCombo, NULL, pCSVWindow->peaModel);
	ui_ComboBoxSetSelectedsAsString(pCSVWindow->pGroupCombo, "*");
	
	//Set the multi-edit window that we're operating on
	pCSVWindow->pMEWindow = pMEWindow;
	
	//Setup the callback
	pCSVWindow->okayClickedf = okayClickedFunc;
}

#include "MultiEditWindow_h_ast.c"

#endif
