#ifndef NO_EDITORS

#include "ChoiceTableEditor.h"

#include "ChoiceTable_common.h"
#include "EditorPrefs.h"
#include "EString.h"
#include "ExpressionFunc.h"
#include "MultiEditField.h"
#include "StringCache.h"
#include "UIGimmeButton.h"
#include "GfxSprite.h"
#include "GfxTexAtlas.h"
#include "UIScrollbar.h"
#include "sysutil.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

// This magic line is required if you run in Full Debug to avoid assert
AUTO_RUN_ANON( memBudgetAddMapping( __FILE__, BUDGET_Editors ););


//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------
typedef struct ChoiceEd_UndoData
{
	ChoiceTable* pPreChoiceTable;
	ChoiceTable* pPostChoiceTable;
} ChoiceEd_UndoData;

#define X_OFFSET_BASE		4
#define X_OFFSET_INDENT		15
#define X_OFFSET_CONTROL	125

#define LABEL_ROW_HEIGHT	20
#define STANDARD_ROW_HEIGHT 28
#define TINY_COLUMN_WIDTH 80
#define STANDARD_COLUMN_WIDTH 160
#define LARGE_COLUMN_WIDTH 250
#define SEPARATOR_HEIGHT    11

static bool gInitializedEditor = false;
static bool gInitializedEditorData = false;
static UISkin *gBoldExpanderSkin;

static ChoiceEntry g_ChoiceEdClipboardEntry;

static char **geaScopes = NULL;


//---------------------------------------------------------------------------------------------------
// Callbacks
//---------------------------------------------------------------------------------------------------
static ChoiceEd_Doc *choiceEd_InitDoc(ChoiceTable *pChoiceTable, bool bCreated);
static void choiceEd_InitDisplay(EMEditor *pEditor, ChoiceEd_Doc *pDoc);
static UIWindow *choiceEd_InitMainWindow(ChoiceEd_Doc *pDoc);
static void choiceEd_InitToolbarsAndMenus(EMEditor *pEditor);

static void choiceEd_PostOpenFixup(ChoiceTable* pChoiceTable);
static void choiceEd_PreSaveFixup(ChoiceTable* pChoiceTable);

static void choiceEd_AddFieldToParent(MEField *pField, UIWidget *pParent, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight, ChoiceEd_Doc *pDoc);

static void choiceEd_UpdateDisplay(ChoiceEd_Doc* pDoc);
static void choiceEd_SetNameCB(MEField *pField, bool bFinished, ChoiceEd_Doc *pDoc);
static void choiceEd_SetScopeCB(MEField *pField, bool bFinished, ChoiceEd_Doc *pDoc);
static bool choiceEd_FieldPreChangeCB(MEField *pField, bool bFinished, ChoiceEd_Doc *pDoc);
static void choiceEd_FieldChangedCB(MEField *pField, bool bFinished, ChoiceEd_Doc *pDoc);
static void choiceEd_ChoiceTableChanged(ChoiceEd_Doc* pDoc, bool bUndoable);
static void choiceEd_UndoCB(ChoiceEd_Doc *pDoc, ChoiceEd_UndoData *pData);
static void choiceEd_RedoCB(ChoiceEd_Doc *pDoc, ChoiceEd_UndoData *pData);
static void choiceEd_UndoFreeCB(ChoiceEd_Doc *pDoc, ChoiceEd_UndoData *pData);

static UIExpander* choiceEd_CreateExpander(UIExpanderGroup *pGroup, const char* pcName, int index);
static void choiceEd_RefreshLabel(UILabel** ppLabel, const char* text, const char* tooltip, F32 x, F32 y, UIWidget *pParent);
static void choiceEd_RefreshButton(UIButton** ppButton, const char* text, const char* tooltip, F32 x, F32 y, UIWidget *pParent,
								   UIActivationFunc fn, void* pData, bool bEnabled);
static void choiceEd_RefreshFieldSimple( MEField** ppField, MEFieldType eType, void* pOld, void* pNew,
										 int x, int y, F32 w, UIWidget* pParent, ChoiceEd_Doc* pDoc,
										 ParseTable* pTable, const char* pchField );
static void choiceEd_RefreshFieldSimpleDataProvided(
		MEField** ppField, MEFieldType eType, void* pOld, void* pNew,
		int x, int y, F32 w, UIWidget* pParent, ChoiceEd_Doc* pDoc,
		ParseTable* pTable, const char* pchField,
		ParseTable *pComboParseTable, cUIModel peaComboModel, const char *pchComboField );
static void choiceEd_RefreshFieldSimpleEnum( MEField** ppField, MEFieldType eType, void* pOld, void* pNew,
											 int x, int y, F32 w, UIWidget* pParent, ChoiceEd_Doc* pDoc,
											 ParseTable* pTable, const char* pchField, StaticDefineInt* pEnum );
static void choiceEd_RefreshFieldSimpleGlobalDictionary( MEField** ppField, MEFieldType eType, void* pOld, void* pNew,
														 int x, int y, F32 w, UIWidget* pParent, ChoiceEd_Doc* pDoc,
														 ParseTable* pTable, const char* pchField,
														 const char* pchDictName );

static void choiceEd_RefreshData(ChoiceEd_Doc *pDoc);

static F32 choiceEd_RefreshDefGroupInfrastructure( ChoiceEd_Doc *pDoc, ChoiceEd_DefGroup *pGroup, int index, ChoiceTableValueDef *def );
static void choiceEd_RefreshDefGroup(ChoiceEd_Doc *pDoc, ChoiceEd_DefGroup *pGroup, int index, ChoiceTableValueDef *origDef, ChoiceTableValueDef *def);
static void choiceEd_FreeDefGroup(ChoiceEd_DefGroup *pGroup);
static void choiceEd_AddDef(UIButton* pButton, ChoiceEd_Doc *pDoc);
static void choiceEd_RemoveDef(UIButton* pButton, ChoiceEd_DefGroup *pGroup);
static void choiceEd_DefMoveUp(UIButton* pButton, ChoiceEd_DefGroup *pGroup);
static void choiceEd_DefMoveDown(UIButton* pButton, ChoiceEd_DefGroup *pGroup);

static F32 choiceEd_RefreshEntryGroupInfrastructure( ChoiceEd_Doc *pDoc, ChoiceEd_EntryGroup *pGroup, int index, ChoiceEntry *entry );
static void choiceEd_RefreshEntryGroup(ChoiceEd_Doc *pDoc, ChoiceEd_EntryGroup *pGroup, int index, ChoiceEntry *origEntry, ChoiceEntry *entry);
static void choiceEd_FreeEntryGroup(ChoiceEd_EntryGroup *pGroup);
static void choiceEd_AddEntry(UIButton* pButton, ChoiceEd_Doc *pDoc);
static void choiceEd_RemoveEntry(UIButton* pButton, ChoiceEd_EntryGroup *pGroup);
static void choiceEd_EntryMoveUp( void* ignored1, ChoiceEd_EntryGroup* pGroup );
static void choiceEd_EntryMoveDown( void* ignored1, ChoiceEd_EntryGroup* pGroup );
static void choiceEd_ActiveEntryClone( void* ignored1, ChoiceEd_EntryGroup* pGroup );
static void choiceEd_ActiveEntryCut( void* ignored1, ChoiceEd_EntryGroup* pGroup );
static void choiceEd_ActiveEntryCopy( void* ignored1, ChoiceEd_EntryGroup* pGroup );
static void choiceEd_ActiveEntryPaste( void* ignored1, ChoiceEd_EntryGroup* pGroup );
static void choiceEd_RefreshEntryHeader(ChoiceEd_Doc *pDoc);

static void choiceEd_ColumnSort(void* ignored1, ChoiceEd_DefGroup* columnDef);
static void choiceEd_ColumnCopy(void* ignored1, ChoiceEd_DefGroup* columnDef);
static void choiceEd_ColumnPaste(void* ignored1, ChoiceEd_DefGroup* columnDef);

static void choiceEd_RefreshValueGroupInfrastructure( ChoiceEd_Doc *pDoc, F32 x, F32 y, ChoiceEd_ValueGroup *pGroup, int index, ChoiceValue *value );
static void choiceEd_RefreshValueGroup(ChoiceEd_Doc *pDoc, F32 x, F32 y, ChoiceEd_ValueGroup *pGroup, int index, ChoiceValue *origValue, ChoiceValue *value);
static void choiceEd_FreeValueGroup(ChoiceEd_ValueGroup *pGroup);

static void choiceEd_PaneDrawDarkChoice( UIPane* pane, UI_PARENT_ARGS );
static void choiceEd_PaneDrawDarkValue( UIPane* pane, UI_PARENT_ARGS );
static void choiceEd_PaneDrawLightChoice( UIPane* pane, UI_PARENT_ARGS );
static void choiceEd_PaneDrawLightValue( UIPane* pane, UI_PARENT_ARGS );

ChoiceEd_Doc *choiceEd_InitDoc(ChoiceTable *pChoiceTable, bool bCreated)
{
	ChoiceEd_Doc *pDoc;
	char nameBuf[260];

	// Initialize the structure
	pDoc = calloc(1,sizeof(ChoiceEd_Doc));

	// Fill in the map description data
	if (bCreated) {
		pDoc->pChoiceTable = StructCreate(parse_ChoiceTable);
		emMakeUniqueDocName(&pDoc->emDoc, "New_Choice_Table", "ChoiceTable", "ChoiceTable");
		pDoc->pChoiceTable->pchName = StructAllocString(pDoc->emDoc.doc_name);
		sprintf(nameBuf, "defs/choice/%s.Choice", pDoc->pChoiceTable->pchName);
		pDoc->pChoiceTable->pchFileName = allocAddFilename(nameBuf);
		choiceEd_PostOpenFixup(pDoc->pChoiceTable);
	} else {
		pDoc->pChoiceTable = StructClone(parse_ChoiceTable, pChoiceTable);
		choiceEd_PostOpenFixup(pDoc->pChoiceTable);
		pDoc->pOrigChoiceTable = StructClone(parse_ChoiceTable, pDoc->pChoiceTable);
	}

	// Set up the undo stack
	pDoc->emDoc.edit_undo_stack = EditUndoStackCreate();
	EditUndoSetContext(pDoc->emDoc.edit_undo_stack, pDoc);
	pDoc->pNextUndoChoiceTable = StructClone(parse_ChoiceTable, pDoc->pChoiceTable);

	return pDoc;
}

void choiceEd_InitDisplay(EMEditor *pEditor, ChoiceEd_Doc *pDoc)
{
	// Create the window (ignore field change callbacks during init)
	pDoc->bIgnoreFieldChanges = true;
	pDoc->bIgnoreFilenameChanges = true;
	pDoc->pMainWindow = choiceEd_InitMainWindow( pDoc );
	pDoc->bIgnoreFilenameChanges = false;
	pDoc->bIgnoreFieldChanges = false;

	// Show the window
	ui_WindowPresent(pDoc->pMainWindow);

	// Editor Manager needs to be told about the windows used
	pDoc->emDoc.primary_ui_window = pDoc->pMainWindow;
	eaPush(&pDoc->emDoc.ui_windows, pDoc->pMainWindow);

	// Update the rest of the UI
	choiceEd_UpdateDisplay(pDoc);
}

UIWindow *choiceEd_InitMainWindow(ChoiceEd_Doc *pDoc)
{
	UIWindow *pWin;
	UILabel *pLabel;
	UIButton *pButton;
	MEField *pField;
	F32 y = 0;
	F32 fBottomY = 0;
	F32 fTopY = 0;

	// Create the window
	pWin = ui_WindowCreate(pDoc->pChoiceTable->pchName, 15, 50, 950, 600);
	pWin->minW = 950;
	pWin->minH = 400;
	EditorPrefGetWindowPosition(CHOICE_TABLE_EDITOR, "Window Position", "Main", pWin);

	// Name
	pLabel = ui_LabelCreate("Name", 0, y);
	ui_WindowAddChild(pWin, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDoc->pOrigChoiceTable, pDoc->pChoiceTable, parse_ChoiceTable, "Name");
	choiceEd_AddFieldToParent(pField, UI_WIDGET(pWin), X_OFFSET_CONTROL, y, 0, 0.4, UIUnitPercentage, 21, pDoc);
	MEFieldSetChangeCallback(pField, choiceEd_SetNameCB, pDoc);
	eaPush(&pDoc->eaDocFields, pField);

	y += STANDARD_ROW_HEIGHT;

	// Scope
	pLabel = ui_LabelCreate("Scope", 0, y);
	ui_WindowAddChild(pWin, pLabel);
	pField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pDoc->pOrigChoiceTable, pDoc->pChoiceTable, parse_ChoiceTable, "Scope", NULL, &geaScopes, NULL);
	choiceEd_AddFieldToParent(pField, UI_WIDGET(pWin), X_OFFSET_CONTROL, y, 0, 0.4, UIUnitPercentage, 21, pDoc);
	MEFieldSetChangeCallback(pField, choiceEd_SetScopeCB, pDoc);
	eaPush(&pDoc->eaDocFields, pField);
	
	y += STANDARD_ROW_HEIGHT;

	// File Name
	pLabel = ui_LabelCreate("File Name", 0, y);
	ui_WindowAddChild(pWin, pLabel);
	pDoc->pFileButton = ui_GimmeButtonCreate(X_OFFSET_CONTROL, y, "ChoiceTable", pDoc->pChoiceTable->pchName, pDoc->pChoiceTable);
	ui_WindowAddChild(pWin, pDoc->pFileButton);
	pLabel = ui_LabelCreate(pDoc->pChoiceTable->pchFileName, X_OFFSET_CONTROL+20, y);
	ui_WindowAddChild(pWin, pLabel);
	ui_WidgetSetWidthEx(UI_WIDGET(pLabel), 0.4, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pLabel), 0, 21, 0, 0);
	pDoc->pFilenameLabel = pLabel;

	y += STANDARD_ROW_HEIGHT;

	// Entry Selection Type
	pLabel = ui_LabelCreate("Entry Selection", 0, y);
	ui_WindowAddChild(pWin, pLabel);
	pField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pDoc->pOrigChoiceTable, pDoc->pChoiceTable, parse_ChoiceTable, "SelectType", ChoiceSelectTypeEnum);
	choiceEd_AddFieldToParent(pField, UI_WIDGET(pWin), X_OFFSET_CONTROL, y, 0, 0.4, UIUnitPercentage, 21, pDoc);
	eaPush(&pDoc->eaDocFields, pField);

	y += STANDARD_ROW_HEIGHT;

	// Entry Selection Time Settings
	pLabel = ui_LabelCreate("Time Interval", X_OFFSET_INDENT, y);
	ui_WindowAddChild(pWin, pLabel);
	eaPush(&pDoc->eaTimedRandomWidgets, UI_WIDGET(pLabel));
	pField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pDoc->pOrigChoiceTable, pDoc->pChoiceTable, parse_ChoiceTable, "TimeInterval", TimeIntervalEnum);
	choiceEd_AddFieldToParent(pField, UI_WIDGET(pWin), X_OFFSET_INDENT + X_OFFSET_CONTROL, y, 0, 0.4, UIUnitPercentage, 21, pDoc);
	eaPush(&pDoc->eaTimedRandomWidgets, pField->pUIWidget);
	eaPush(&pDoc->eaDocFields, pField);

	y += STANDARD_ROW_HEIGHT;

	pLabel = ui_LabelCreate("Values Per Interval", X_OFFSET_INDENT, y);
	ui_WindowAddChild(pWin, pLabel);
	eaPush(&pDoc->eaTimedRandomWidgets, UI_WIDGET(pLabel));
	pField = MEFieldCreateSimple(kMEFieldType_ValidatedTextEntry, pDoc->pOrigChoiceTable, pDoc->pChoiceTable, parse_ChoiceTable, "ValuesPerInterval");
	choiceEd_AddFieldToParent(pField, UI_WIDGET(pWin), X_OFFSET_INDENT + X_OFFSET_CONTROL, y, 0, 0.4, UIUnitPercentage, 21, pDoc);
	eaPush(&pDoc->eaTimedRandomWidgets, pField->pUIWidget);
	eaPush(&pDoc->eaDocFields, pField);

	y += STANDARD_ROW_HEIGHT;

	pLabel = ui_LabelCreate("Unique Intervals", X_OFFSET_INDENT, y);
	ui_WindowAddChild(pWin, pLabel);
	eaPush(&pDoc->eaTimedRandomWidgets, UI_WIDGET(pLabel));
	pField = MEFieldCreateSimple(kMEFieldType_ValidatedTextEntry, pDoc->pOrigChoiceTable, pDoc->pChoiceTable, parse_ChoiceTable, "NumUniqueIntervals");
	choiceEd_AddFieldToParent(pField, UI_WIDGET(pWin), X_OFFSET_INDENT + X_OFFSET_CONTROL, y, 0, 0.4, UIUnitPercentage, 21, pDoc);
	eaPush(&pDoc->eaTimedRandomWidgets, pField->pUIWidget);
	eaPush(&pDoc->eaDocFields, pField);

	y += STANDARD_ROW_HEIGHT;

	fTopY = MAX( fTopY, y + SEPARATOR_HEIGHT );

	/// LAYOUT DEFINITION GOES HERE
	y = fTopY;
	{
		pLabel = ui_LabelCreate( "Value Definitions", 0, y );
		pDoc->pValueDefinitionsLabel = pLabel;
		ui_LabelSetFont( pLabel, RefSystem_ReferentFromString(g_ui_FontDict, "Default_Bold"));
		ui_WindowAddChild( pWin, pLabel );
		y += LABEL_ROW_HEIGHT;

		pDoc->pChoiceValueDefExpGroup = ui_ExpanderGroupCreate();
		ui_WidgetSetPosition( UI_WIDGET( pDoc->pChoiceValueDefExpGroup ), 0, y );
		ui_WidgetSetPaddingEx( UI_WIDGET( pDoc->pChoiceValueDefExpGroup ), 0, 0, 0, STANDARD_ROW_HEIGHT );
		ui_WidgetSetDimensionsEx( UI_WIDGET( pDoc->pChoiceValueDefExpGroup ), 275, 1.0, UIUnitFixed, UIUnitPercentage );
		UI_SET_STYLE_BORDER_NAME( pDoc->pChoiceValueDefExpGroup->hBorder, "Default_MiniFrame_Empty" );
		ui_WindowAddChild( pWin, pDoc->pChoiceValueDefExpGroup );

		pButton = ui_ButtonCreate( "Add Def", 0, 0, choiceEd_AddDef, pDoc );
		ui_WidgetSetWidth( UI_WIDGET( pButton ), 100 );
		ui_WidgetSetPositionEx( UI_WIDGET( pButton ), 5, 0, 0, 0, UIBottomLeft );
		ui_WindowAddChild( pWin, pButton );
	}

	y = fTopY;
	{
		pLabel = ui_LabelCreate( "Choices", 277, y );
		pDoc->pChoicesLabel = pLabel;
		ui_LabelSetFont( pLabel, RefSystem_ReferentFromString(g_ui_FontDict, "Default_Bold"));
		ui_WindowAddChild( pWin, pLabel );
		y += LABEL_ROW_HEIGHT;

		pDoc->pChoiceEntryPane = ui_PaneCreate( 277, y, 1, 1, UIUnitPercentage, UIUnitPercentage, 0 );
		ui_PaneSetStyle( pDoc->pChoiceEntryPane, "Default_MiniFrame_Empty", true, true );
		ui_WidgetSetPaddingEx( UI_WIDGET( pDoc->pChoiceEntryPane ), 0, 0, 0, STANDARD_ROW_HEIGHT );
		
		pDoc->pChoiceEntryScrollArea = ui_ScrollAreaCreate( 0, 0, 0, 0, 0, 0, true, true );
		ui_ScrollAreaSetDraggable( pDoc->pChoiceEntryScrollArea, true );
		ui_WidgetSetDimensionsEx( UI_WIDGET(pDoc->pChoiceEntryScrollArea), 1, 1, UIUnitPercentage, UIUnitPercentage );
		pDoc->pChoiceEntryScrollArea->autosize = true;
		ui_WindowAddChild( pWin, pDoc->pChoiceEntryPane );
		ui_PaneAddChild( pDoc->pChoiceEntryPane, pDoc->pChoiceEntryScrollArea );

		pButton = ui_ButtonCreate( "Add Entry", 0, 0, choiceEd_AddEntry, pDoc );
		ui_WidgetSetWidth( UI_WIDGET( pButton ), 100 );
		ui_WidgetSetPositionEx( UI_WIDGET( pButton ), 277, 0, 0, 0, UIBottomLeft );
		ui_WindowAddChild( pWin, pButton );
	}
	
	return pWin;
}

void choiceEd_InitToolbarsAndMenus(EMEditor *pEditor)
{
	EMToolbar *pToolbar;

	// Toolbar
	pToolbar = emToolbarCreateFileToolbar(EM_FILE_TOOLBAR_NEW|EM_FILE_TOOLBAR_OPEN|EM_FILE_TOOLBAR_SAVE);
	eaPush(&pEditor->toolbars, pToolbar);
	eaPush(&pEditor->toolbars, emToolbarCreateWindowToolbar());

	// File menu
	emMenuItemCreate(pEditor, "choiceEd_Revert", "Revert", NULL, NULL, "choiceEd_Revert");
	emMenuRegister(pEditor, emMenuCreate(pEditor, "File", "choiceEd_Revert", NULL));
}

void choiceEd_PostOpenFixup(ChoiceTable* pChoiceTable)
{
	// make sure all messages are editor copies
	langMakeEditorCopy( parse_ChoiceTable, pChoiceTable, true );
}

void choiceEd_PreSaveFixup(ChoiceTable* pChoiceTable)
{
	//default any N/A fields so that they don't show up in the output file
	{
		int it;
		for( it = 0; it != eaSize( &pChoiceTable->eaEntry ); ++it ) {
			ChoiceEntry* pChoiceEntry = pChoiceTable->eaEntry[ it ];

			switch( pChoiceEntry->eType ) {
				case CET_Value:
					REMOVE_HANDLE( pChoiceEntry->hChoiceTable );
					eaSetSizeStruct( &pChoiceEntry->eaValues, parse_ChoiceValue, eaSize( &pChoiceTable->eaDefs ));
					{
						int valueIt;
						for( valueIt = 0; valueIt != eaSize( &pChoiceEntry->eaValues ); ++valueIt ) {
							ChoiceTableValueDef* def = pChoiceTable->eaDefs[ valueIt ];
							ChoiceValue* value = pChoiceEntry->eaValues[ valueIt ];

							switch( value->eType ) {
								case CVT_Value:
									REMOVE_HANDLE( value->hChoiceTable );
									value->pchChoiceName = NULL;
									value->value.eType = def->eType;
									value->value.pcName = NULL;
									worldVariableCleanup( &value->value );
									
								xcase CVT_Choice:
									StructReset( parse_WorldVariable, &value->value );
							}
						}
					}

				xcase CET_Include:
					pChoiceEntry->fWeight = 1;
					eaDestroyStruct( &pChoiceEntry->eaValues, parse_ChoiceValue );
			}
		}
	}

	// Fixup message keys
	{
		int defIt;
		int rowIt;

		for( defIt = 0; defIt != eaSize( &pChoiceTable->eaDefs ); ++defIt ) {
			ChoiceTableValueDef* def = pChoiceTable->eaDefs[ defIt ];
			
			if( def->eType != WVAR_MESSAGE ) {
				continue;
			}
			for( rowIt = 0; rowIt != eaSize( &pChoiceTable->eaEntry ); ++rowIt ) {
				ChoiceEntry* row = pChoiceTable->eaEntry[ rowIt ];
				ChoiceValue* value;

				if( row->eType != CET_Value ) {
					continue;
				}
				if( eaSize( &row->eaValues ) <= defIt ) {
					continue;
				}

				value = row->eaValues[ defIt ];
				if( value->eType != CVT_Value ) {
					continue;
				}
				if( value->value.eType != WVAR_MESSAGE ) {
					continue;
				}

				// Okay, everything looks valid
				{
					char keyBuffer[ RESOURCE_NAME_MAX_SIZE ];
					char scopeBuffer[ RESOURCE_NAME_MAX_SIZE ];

					sprintf( keyBuffer, "ChoiceTable.%s.%s.%d", pChoiceTable->pchName, def->pchName, rowIt );
					sprintf( scopeBuffer, "ChoiceTable/%s", pChoiceTable->pchName );
					
					langFixupMessage( value->value.messageVal.pEditorCopy, keyBuffer, "A generic message in a ChoiceTable.", scopeBuffer );
				}
			}
		}
	}
}

void choiceEd_AddFieldToParent(MEField *pField, UIWidget *pParent, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight, ChoiceEd_Doc *pDoc)
{
	MEFieldAddToParent(pField, pParent, x, y);
	ui_WidgetSetPositionEx(pField->pUIWidget, x, y, xPercent, 0, UITopLeft);
	ui_WidgetSetWidthEx(pField->pUIWidget, w, wUnit);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, padRight, 0, 0);
	MEFieldSetChangeCallback(pField, choiceEd_FieldChangedCB, pDoc);
	MEFieldSetPreChangeCallback(pField, choiceEd_FieldPreChangeCB, pDoc);
}

void choiceEd_UpdateDisplay(ChoiceEd_Doc* pDoc)
{
	int i;
	F32 y = 0;

	// Ignore changes while UI refreshes
	pDoc->bIgnoreFieldChanges = true;

	// Refresh data
	choiceEd_RefreshData(pDoc);

	// Refresh doc-level fields
	for(i=eaSize(&pDoc->eaDocFields)-1; i>=0; --i) {
		MEFieldSetAndRefreshFromData(pDoc->eaDocFields[i], pDoc->pOrigChoiceTable, pDoc->pChoiceTable);
	}

	// Hide/unhide doc-level fields
	for (i = 0; i < eaSize(&pDoc->eaTimedRandomWidgets); i++)
	{
		if (pDoc->pChoiceTable->eSelectType == CST_TimedRandom)
		{
			ui_WidgetAddChild(UI_WIDGET(pDoc->pMainWindow), pDoc->eaTimedRandomWidgets[i]);
			y = !i ? pDoc->eaTimedRandomWidgets[i]->y + STANDARD_ROW_HEIGHT + SEPARATOR_HEIGHT : MAX(y, pDoc->eaTimedRandomWidgets[i]->y + STANDARD_ROW_HEIGHT + SEPARATOR_HEIGHT);
		}
		else 
		{
			ui_WidgetRemoveChild(UI_WIDGET(pDoc->pMainWindow), pDoc->eaTimedRandomWidgets[i]);
			y = !i ? pDoc->eaTimedRandomWidgets[i]->y + SEPARATOR_HEIGHT : MIN(y, pDoc->eaTimedRandomWidgets[i]->y + SEPARATOR_HEIGHT);
		}
	}
	// Adjust bottom expander groups/labels on current size of main body
	pDoc->pChoicesLabel->widget.y = y;
	pDoc->pValueDefinitionsLabel->widget.y = y;
	pDoc->pChoiceValueDefExpGroup->widget.y = y + LABEL_ROW_HEIGHT;
	pDoc->pChoiceEntryPane->widget.y = y + LABEL_ROW_HEIGHT;
	
	ui_GimmeButtonSetName(pDoc->pFileButton, pDoc->pChoiceTable->pchName);
	ui_GimmeButtonSetReferent(pDoc->pFileButton, pDoc->pChoiceTable);
	ui_LabelSetText(pDoc->pFilenameLabel, pDoc->pChoiceTable->pchFileName);

	// Update saved flag
	pDoc->emDoc.saved = pDoc->pOrigChoiceTable && (StructCompare(parse_ChoiceTable, pDoc->pOrigChoiceTable, pDoc->pChoiceTable, 0, 0, 0) == 0);

	// Refresh the value defs
	{
		int numDefs = eaSize( &pDoc->pChoiceTable->eaDefs );
		int it;

		for( it = eaSize( &pDoc->eaChoiceValueDefGroups ) - 1; it >= numDefs; --it ) {
			choiceEd_FreeDefGroup( pDoc->eaChoiceValueDefGroups[ it ]);
			eaRemove( &pDoc->eaChoiceValueDefGroups, it );
		}

		for( it = 0; it != numDefs; ++it ) {
			ChoiceTableValueDef* def = pDoc->pChoiceTable->eaDefs[ it ];
			ChoiceTableValueDef* origDef = NULL;

			if( eaSize(&pDoc->eaChoiceValueDefGroups) <= it ) {
				ChoiceEd_DefGroup* defGroup = calloc( 1, sizeof( ChoiceEd_DefGroup ));

				defGroup->pDoc = pDoc;
				defGroup->pExpander = choiceEd_CreateExpander( pDoc->pChoiceValueDefExpGroup, "Def", it );

				eaPush( &pDoc->eaChoiceValueDefGroups, defGroup );
			}

			if( pDoc->pOrigChoiceTable && eaSize( &pDoc->pOrigChoiceTable->eaDefs ) > it ) {
				origDef = pDoc->pOrigChoiceTable->eaDefs[ it ];
			}

			choiceEd_RefreshDefGroup( pDoc, pDoc->eaChoiceValueDefGroups[ it ], it, origDef, def );
		}
	}

	// Refresh the entries
	{
		int numEntries = eaSize( &pDoc->pChoiceTable->eaEntry );
		int it;

		for( it = eaSize( &pDoc->eaChoiceEntryGroups ) - 1; it >= numEntries; --it ) {
			choiceEd_FreeEntryGroup( pDoc->eaChoiceEntryGroups[ it ]);
			eaRemove( &pDoc->eaChoiceEntryGroups, it );
		}

		y = 0;
		choiceEd_RefreshEntryHeader( pDoc );
		y += STANDARD_ROW_HEIGHT;
		
		for( it = 0; it != numEntries; ++it ) {
			ChoiceEntry* entry = pDoc->pChoiceTable->eaEntry[ it ];
			ChoiceEntry* origEntry = NULL;

			if( eaSize(&pDoc->eaChoiceEntryGroups ) <= it ) {
				ChoiceEd_EntryGroup* entryGroup = calloc( 1, sizeof( ChoiceEd_EntryGroup ));

				entryGroup->pDoc = pDoc;
				entryGroup->pPane = ui_PaneCreate( 0, 0, 0, 0, UIUnitFixed, UIUnitFixed, 0 );
				entryGroup->pPane->invisible = true;
				ui_ScrollAreaAddChild( pDoc->pChoiceEntryScrollArea, entryGroup->pPane );

				eaPush( &pDoc->eaChoiceEntryGroups, entryGroup );
			}

			if( pDoc->pOrigChoiceTable && eaSize( &pDoc->pOrigChoiceTable->eaEntry ) > it ) {
				origEntry = pDoc->pOrigChoiceTable->eaEntry[ it ];
			}

			ui_WidgetSetPosition( UI_WIDGET(pDoc->eaChoiceEntryGroups[ it ]->pPane), 0, y );
			choiceEd_RefreshEntryGroup( pDoc, pDoc->eaChoiceEntryGroups[ it ], it, origEntry, entry );
			y += ui_WidgetGetHeight( UI_WIDGET( pDoc->eaChoiceEntryGroups[ it ]->pPane));
		}

		for( it = 0; it != eaSize( &pDoc->eaFooterPane ); ++it ) {
			UIWidget* footerPane = UI_WIDGET( pDoc->eaFooterPane[ it ]);
			ui_WidgetSetPosition( footerPane, ui_WidgetGetX( footerPane ), y );
			ui_WidgetSetHeightEx( footerPane, 1, UIUnitPercentage );
		}
	}

	// Start paying attention to changes again
	pDoc->bIgnoreFieldChanges = FALSE;

	/*
	// Updating previews -- right now it's on the console, but it
	// should be in the editor
	{
		int seed = rand();
		int it;

		for( it = 0; it != eaSize( &pDoc->pChoiceTable->eaDefs ); ++it ) {
			ServerCmd_choice_ChooseValueForEditor( pDoc->pChoiceTable, pDoc->pChoiceTable->eaDefs[ it ]->pchName, seed );
		}
	}
	*/
}

void choiceEd_SetNameCB(MEField *pField, bool bFinished, ChoiceEd_Doc *pDoc)
{
	MEFieldFixupNameString(pField, &pDoc->pChoiceTable->pchName);

	// when the name changes, change the title of the window
	ui_WindowSetTitle(pDoc->pMainWindow, pDoc->pChoiceTable->pchName);

	// make sure the browser picks up the new choice table name if the name changed
	sprintf(pDoc->emDoc.doc_name, "%s", pDoc->pChoiceTable->pchName);
	sprintf(pDoc->emDoc.doc_display_name, "%s", pDoc->pChoiceTable->pchName);
	pDoc->emDoc.name_changed = 1;

	// call the scope function to avoid duplicating logic
	choiceEd_SetScopeCB(pField, bFinished, pDoc);
}

void choiceEd_SetScopeCB(MEField *pField, bool bFinished, ChoiceEd_Doc *pDoc)
{
	if (!pDoc->bIgnoreFilenameChanges) {
		// update the filename appropriately
		resFixFilename(g_hChoiceTableDict, pDoc->pChoiceTable->pchName, pDoc->pChoiceTable);
	}

	// call on to do regular updates
	choiceEd_FieldChangedCB(pField, bFinished, pDoc);
}

bool choiceEd_FieldPreChangeCB(MEField *pField, bool bFinished, ChoiceEd_Doc *pDoc)
{
	// make sure the resource is checked out of gimme
	return emDocIsEditable(&pDoc->emDoc, true);
}

void choiceEd_FieldChangedCB(MEField *pField, bool bFinished, ChoiceEd_Doc *pDoc)
{
	choiceEd_ChoiceTableChanged(pDoc, bFinished);
}

void choiceEd_ChoiceTableChanged(ChoiceEd_Doc* pDoc, bool bUndoable)
{
	if (!pDoc->bIgnoreFieldChanges) {
		choiceEd_UpdateDisplay(pDoc);

		if (bUndoable) {
			ChoiceEd_UndoData *pData = calloc(1, sizeof(ChoiceEd_UndoData));
			pData->pPreChoiceTable = pDoc->pNextUndoChoiceTable;
			pData->pPostChoiceTable = StructClone(parse_ChoiceTable, pDoc->pChoiceTable);
			EditCreateUndoCustom(pDoc->emDoc.edit_undo_stack, choiceEd_UndoCB, choiceEd_RedoCB, choiceEd_UndoFreeCB, pData);
			pDoc->pNextUndoChoiceTable = StructClone(parse_ChoiceTable, pDoc->pChoiceTable);
		}
	}
}

void choiceEd_UndoCB(ChoiceEd_Doc *pDoc, ChoiceEd_UndoData *pData)
{
	// put the undo choice table into the editor
	StructDestroy(parse_ChoiceTable, pDoc->pChoiceTable);
	pDoc->pChoiceTable = StructClone(parse_ChoiceTable, pData->pPreChoiceTable);
	if (pDoc->pNextUndoChoiceTable) {
		StructDestroy(parse_ChoiceTable, pDoc->pNextUndoChoiceTable);
	}
	pDoc->pNextUndoChoiceTable = StructClone(parse_ChoiceTable, pDoc->pChoiceTable);

	// update the ui
	choiceEd_ChoiceTableChanged(pDoc, false);
}

void choiceEd_RedoCB(ChoiceEd_Doc *pDoc, ChoiceEd_UndoData *pData)
{
	// put the undo choice table into the editor
	StructDestroy(parse_ChoiceTable, pDoc->pChoiceTable);
	pDoc->pChoiceTable = StructClone(parse_ChoiceTable, pData->pPostChoiceTable);
	if (pDoc->pNextUndoChoiceTable) {
		StructDestroy(parse_ChoiceTable, pDoc->pNextUndoChoiceTable);
	}
	pDoc->pNextUndoChoiceTable = StructClone(parse_ChoiceTable, pDoc->pChoiceTable);

	// update the UI
	choiceEd_ChoiceTableChanged(pDoc, false);
}

void choiceEd_UndoFreeCB(ChoiceEd_Doc *pDoc, ChoiceEd_UndoData *pData)
{
	// free the memory
	StructDestroy(parse_ChoiceTable, pData->pPreChoiceTable);
	StructDestroy(parse_ChoiceTable, pData->pPostChoiceTable);
	free(pData);
}

/// WIDGET CREATION
UIExpander* choiceEd_CreateExpander(UIExpanderGroup *pGroup, const char* pcName, int index)
{
	UIExpander* accum = ui_ExpanderCreate( pcName, 0 );
	ui_WidgetSkin( UI_WIDGET( accum ), gBoldExpanderSkin );
	ui_ExpanderGroupInsertExpander( pGroup, accum, index );
	ui_ExpanderSetOpened( accum, true );

	return accum;
}

void choiceEd_RefreshLabel(UILabel** ppLabel, const char* text, const char* tooltip, F32 x, F32 y, UIWidget *pParent)
{
	if( !*ppLabel ) {
		*ppLabel = ui_LabelCreate(text, x, y);
		ui_WidgetAddChild(pParent, UI_WIDGET(*ppLabel));
	}
	ui_LabelSetText(*ppLabel, text);
	ui_WidgetSetPosition(UI_WIDGET(*ppLabel), x, y);
	ui_WidgetSetTooltipString(UI_WIDGET(*ppLabel), tooltip);
	ui_LabelEnableTooltips(*ppLabel);
}

void choiceEd_RefreshButton(UIButton** ppButton, const char* text, const char* tooltip, F32 x, F32 y, UIWidget *pParent,
							UIActivationFunc fn, void* pData, bool bEnabled)
{
	if( !*ppButton ) {
		*ppButton = ui_ButtonCreate( text, x, y, NULL, NULL );
		ui_WidgetAddChild(pParent, UI_WIDGET(*ppButton));
	}
	ui_ButtonSetText( *ppButton, text );
	ui_ButtonSetTooltip( *ppButton, tooltip );
	ui_WidgetSetPosition(UI_WIDGET(*ppButton), x, y );
	ui_ButtonSetCallback(*ppButton, fn, pData);
	ui_SetActive( UI_WIDGET(*ppButton), bEnabled );
}

void choiceEd_RefreshFieldSimple( MEField** ppField, MEFieldType eType, void* pOld, void* pNew,
								  int x, int y, F32 w, UIWidget* pParent, ChoiceEd_Doc* pDoc,
								  ParseTable* pTable, const char* pchField )
{
	if( !*ppField ) {
		*ppField = MEFieldCreateSimple( eType, pOld, pNew, pTable, pchField );
		MEFieldAddToParent( *ppField, pParent, x, y );
		ui_WidgetSetWidthEx( (*ppField)->pUIWidget, w, w <= 1 ? UIUnitPercentage : UIUnitFixed );
		ui_WidgetSetPaddingEx( (*ppField)->pUIWidget, 0, 5, 0, 0 );
		MEFieldSetChangeCallback( *ppField, choiceEd_FieldChangedCB, pDoc );
		MEFieldSetPreChangeCallback( *ppField, choiceEd_FieldPreChangeCB, pDoc );
	} else {
		ui_WidgetSetPosition( (*ppField)->pUIWidget, x, y );
		MEFieldSetAndRefreshFromData( *ppField, pOld, pNew );
	}
}

void choiceEd_RefreshFieldSimpleDataProvided(
		MEField** ppField, MEFieldType eType, void* pOld, void* pNew,
		int x, int y, F32 w, UIWidget* pParent, ChoiceEd_Doc* pDoc,
		ParseTable* pTable, const char* pchField,
		ParseTable *pComboParseTable, cUIModel peaComboModel, const char *pchComboField )
{
	if( !*ppField ) {
		*ppField = MEFieldCreateSimpleDataProvided( eType, pOld, pNew, pTable, pchField, pComboParseTable, peaComboModel, pchComboField );
		MEFieldAddToParent( *ppField, pParent, x, y );
		ui_WidgetSetWidthEx( (*ppField)->pUIWidget, w, w <= 1 ? UIUnitPercentage : UIUnitFixed );
		ui_WidgetSetPaddingEx( (*ppField)->pUIWidget, 0, 5, 0, 0 );
		MEFieldSetChangeCallback( *ppField, choiceEd_FieldChangedCB, pDoc );
		MEFieldSetPreChangeCallback( *ppField, choiceEd_FieldPreChangeCB, pDoc );
	} else {
		ui_WidgetSetPosition( (*ppField)->pUIWidget, x, y );
		MEFieldSetAndRefreshFromData( *ppField, pOld, pNew );
	}
}

void choiceEd_RefreshFieldSimpleEnum( MEField** ppField, MEFieldType eType, void* pOld, void* pNew,
									  int x, int y, F32 w, UIWidget* pParent, ChoiceEd_Doc* pDoc,
									  ParseTable* pTable, const char* pchField, StaticDefineInt* pEnum )
{
	if( !*ppField ) {
		*ppField = MEFieldCreateSimpleEnum( eType, pOld, pNew, pTable, pchField, pEnum );
		MEFieldAddToParent( *ppField, pParent, x, y );
		ui_WidgetSetWidthEx( (*ppField)->pUIWidget, w, w <= 1 ? UIUnitPercentage : UIUnitFixed );
		ui_WidgetSetPaddingEx( (*ppField)->pUIWidget, 0, 5, 0, 0 );
		MEFieldSetChangeCallback( *ppField, choiceEd_FieldChangedCB, pDoc );
		MEFieldSetPreChangeCallback( *ppField, choiceEd_FieldPreChangeCB, pDoc );
	} else {
		ui_WidgetSetPosition( (*ppField)->pUIWidget, x, y );
		MEFieldSetAndRefreshFromData( *ppField, pOld, pNew );
	}
}

void choiceEd_RefreshFieldSimpleGlobalDictionary( MEField** ppField, MEFieldType eType, void* pOld, void* pNew,
												  int x, int y, F32 w, UIWidget* pParent, ChoiceEd_Doc* pDoc,
												  ParseTable* pTable, const char* pchField,
												  const char* pchDictName )
{
	if( !*ppField ) {
		*ppField = MEFieldCreateSimpleGlobalDictionary( eType, pOld, pNew, pTable, pchField, pchDictName, "ResourceName" );
		MEFieldAddToParent( *ppField, pParent, x, y );
		ui_WidgetSetWidthEx( (*ppField)->pUIWidget, w, w <= 1 ? UIUnitPercentage : UIUnitFixed );
		ui_WidgetSetPaddingEx( (*ppField)->pUIWidget, 0, 5, 0, 0 );
		MEFieldSetChangeCallback( *ppField, choiceEd_FieldChangedCB, pDoc );
		MEFieldSetPreChangeCallback( *ppField, choiceEd_FieldPreChangeCB, pDoc );
	} else {
		ui_WidgetSetPosition( (*ppField)->pUIWidget, x, y );
		MEFieldSetAndRefreshFromData( *ppField, pOld, pNew );
	}
}

void choiceEd_RefreshData(ChoiceEd_Doc *pDoc)
{
	;
}

/// ChoiceEd_DefGroup interface
F32 choiceEd_RefreshDefGroupInfrastructure( ChoiceEd_Doc *pDoc, ChoiceEd_DefGroup *pGroup, int index, ChoiceTableValueDef *def )
{
	pGroup->index = index;

	{
		char buffer[ 256 ];
		sprintf( buffer, "Def: %s", def->pchName );
		ui_WidgetSetTextString( UI_WIDGET( pGroup->pExpander ), buffer );
	}
	
	choiceEd_RefreshButton( &pGroup->pUpButton, "Up", "Move this Def up", 0, 0, UI_WIDGET( pGroup->pExpander ),
							choiceEd_DefMoveUp, pGroup, pGroup->index > 0 );
	ui_WidgetSetDimensions( UI_WIDGET( pGroup->pUpButton ), 40, 18 );
	ui_WidgetSetPositionEx( UI_WIDGET( pGroup->pUpButton ), 62, 0, 0, 0, UITopRight );

	choiceEd_RefreshButton( &pGroup->pDownButton, "Down", "Move this Def down", 0, 0, UI_WIDGET( pGroup->pExpander ),
							choiceEd_DefMoveDown, pGroup, pGroup->index < eaSize( &pDoc->pChoiceTable->eaDefs ) - 1 );
	ui_WidgetSetDimensions( UI_WIDGET( pGroup->pDownButton ), 40, 18 );
	ui_WidgetSetPositionEx( UI_WIDGET( pGroup->pDownButton ), 20, 0, 0, 0, UITopRight );

	choiceEd_RefreshButton( &pGroup->pDelButton, "X", "Delete this Def", 0, 0, UI_WIDGET( pGroup->pExpander ),
							choiceEd_RemoveDef, pGroup, true );
	ui_WidgetSetDimensions( UI_WIDGET( pGroup->pDelButton ), 16, 18 );
	ui_WidgetSetPositionEx( UI_WIDGET( pGroup->pDelButton ), 2, 0, 0, 0, UITopRight );

	return 20;
}

void choiceEd_RefreshDefGroup(ChoiceEd_Doc *pDoc, ChoiceEd_DefGroup *pGroup, int index, ChoiceTableValueDef *origDef, ChoiceTableValueDef *def)
{
	F32 y = choiceEd_RefreshDefGroupInfrastructure( pDoc, pGroup, index, def );

	// Update the name
	choiceEd_RefreshLabel( &pGroup->pNameLabel, "Name", "The name for this value.", X_OFFSET_BASE, y, UI_WIDGET( pGroup->pExpander ));
	choiceEd_RefreshFieldSimple( &pGroup->pNameField, kMEFieldType_TextEntry, origDef, def,
								 X_OFFSET_CONTROL, y, 1, UI_WIDGET( pGroup->pExpander ), pGroup->pDoc,
								 parse_ChoiceTableValueDef, "Name" );
	y += STANDARD_ROW_HEIGHT;
   
	// Update the type
	choiceEd_RefreshLabel( &pGroup->pTypeLabel, "Type", "The type for this value.", X_OFFSET_BASE, y, UI_WIDGET( pGroup->pExpander ));
	choiceEd_RefreshFieldSimpleEnum( &pGroup->pTypeField, kMEFieldType_Combo, origDef, def,
									 X_OFFSET_CONTROL, y, 1, UI_WIDGET( pGroup->pExpander ), pGroup->pDoc,
									 parse_ChoiceTableValueDef, "Type", WorldVariableTypeEnum );
	y += STANDARD_ROW_HEIGHT;

	ui_ExpanderSetHeight( pGroup->pExpander, y );
}

void choiceEd_FreeDefGroup(ChoiceEd_DefGroup *pGroup)
{
	MEFieldSafeDestroy( &pGroup->pNameField );
	MEFieldSafeDestroy( &pGroup->pTypeField );

	ui_ExpanderGroupRemoveExpander( pGroup->pDoc->pChoiceValueDefExpGroup, pGroup->pExpander );
	ui_WidgetQueueFreeAndNull( &pGroup->pExpander );

	free( pGroup );
}

void choiceEd_AddDef(UIButton* pButton, ChoiceEd_Doc *pDoc)
{
	ChoiceTableValueDef* defAccum;

	if( !emDocIsEditable( &pDoc->emDoc, true )) {
		return;
	}

	defAccum = StructCreate( parse_ChoiceTableValueDef );
	eaPush( &pDoc->pChoiceTable->eaDefs, defAccum );
	
	// Refresh the UI
	choiceEd_ChoiceTableChanged( pDoc, true );
}

void choiceEd_RemoveDef(UIButton* pButton, ChoiceEd_DefGroup *pGroup)
{
	ChoiceEd_Doc* pDoc = pGroup->pDoc;

	if( !emDocIsEditable( &pDoc->emDoc, true )) {
		return;
	}

	eaRemove( &pDoc->pChoiceTable->eaDefs, pGroup->index );

	// Refresh the UI
	choiceEd_ChoiceTableChanged( pDoc, true );
}

void choiceEd_DefMoveUp(UIButton* pButton, ChoiceEd_DefGroup *pGroup)
{
	ChoiceEd_Doc* pDoc = pGroup->pDoc;

	if( !emDocIsEditable( &pDoc->emDoc, true )) {
		return;
	}

	eaMove( &pDoc->pChoiceTable->eaDefs, pGroup->index - 1, pGroup->index );

	// Refresh the UI
	choiceEd_ChoiceTableChanged( pDoc, true );
}

void choiceEd_DefMoveDown(UIButton* pButton, ChoiceEd_DefGroup *pGroup)
{
	ChoiceEd_Doc* pDoc = pGroup->pDoc;

	if( !emDocIsEditable( &pDoc->emDoc, true )) {
		return;
	}

	eaMove( &pDoc->pChoiceTable->eaDefs, pGroup->index + 1, pGroup->index );

	// Refresh the UI
	choiceEd_ChoiceTableChanged( pDoc, true );
}


/// ChoiceEd_EntryGroup interface
F32 choiceEd_RefreshEntryGroupInfrastructure( ChoiceEd_Doc *pDoc, ChoiceEd_EntryGroup *pGroup, int index, ChoiceEntry *entry )
{
	pGroup->index = index;
	
	if( !pGroup->pPopupMenuButton ) {
		pGroup->pPopupMenuButton = ui_MenuButtonCreate( 0, 0 );
		ui_MenuButtonAppendItems(
				pGroup->pPopupMenuButton,
				ui_MenuItemCreate("Move Up", UIMenuCallback, choiceEd_EntryMoveUp, pGroup, NULL ),
				ui_MenuItemCreate("Move Down", UIMenuCallback, choiceEd_EntryMoveDown, pGroup, NULL ),
				ui_MenuItemCreate("---", UIMenuSeparator, NULL, NULL, NULL ),
				ui_MenuItemCreate("Delete", UIMenuCallback, choiceEd_RemoveEntry, pGroup, NULL ),
				ui_MenuItemCreate("---", UIMenuSeparator, NULL, NULL, NULL ),
				ui_MenuItemCreate("Clone", UIMenuCallback, choiceEd_ActiveEntryClone, pGroup, NULL ),
				ui_MenuItemCreate("Cut", UIMenuCallback, choiceEd_ActiveEntryCut, pGroup, NULL ),
				ui_MenuItemCreate("Copy", UIMenuCallback, choiceEd_ActiveEntryCopy, pGroup, NULL ),
				ui_MenuItemCreate("Paste", UIMenuCallback, choiceEd_ActiveEntryPaste, pGroup, NULL ),
				NULL );
		ui_PaneAddChild( pGroup->pPane, pGroup->pPopupMenuButton );
	}
	ui_WidgetSetDimensions( UI_WIDGET( pGroup->pPopupMenuButton ), 16, 18 );
	ui_WidgetSetPositionEx( UI_WIDGET( pGroup->pPopupMenuButton ), 0, 3, 0, 0, UITopLeft );
	
	return 0;
}

void choiceEd_RefreshEntryGroup(ChoiceEd_Doc *pDoc, ChoiceEd_EntryGroup *pGroup, int index, ChoiceEntry *origEntry, ChoiceEntry *entry)
{
	F32 x = X_OFFSET_BASE + 16;
	F32 y = choiceEd_RefreshEntryGroupInfrastructure( pDoc, pGroup, index, entry );
	F32 maxY = STANDARD_ROW_HEIGHT;

	// Update the the name
	{
		char buffer[ 256 ];
		sprintf( buffer, "Entry: #%d", index + 1 );
		choiceEd_RefreshLabel( &pGroup->pNameLabel, buffer, NULL, x, y, UI_WIDGET( pGroup->pPane ));
		ui_LabelSetFont( pGroup->pNameLabel, RefSystem_ReferentFromString(g_ui_FontDict, "Default_Bold"));
	}
	x += TINY_COLUMN_WIDTH;
	
	// Update the type
	choiceEd_RefreshFieldSimpleEnum( &pGroup->pTypeField, kMEFieldType_Combo, origEntry, entry,
									 x, y, TINY_COLUMN_WIDTH, UI_WIDGET( pGroup->pPane ), pGroup->pDoc,
									 parse_ChoiceEntry, "Type", ChoiceEntryTypeEnum );

	x += TINY_COLUMN_WIDTH;

	if( entry->eType == CET_Include ) {
		choiceEd_RefreshLabel( &pGroup->pChoiceTableLabel, "Include Table", "All entries from this table will be processed as if they were specified directly.", x, y, UI_WIDGET( pGroup->pPane ));
		x += 100;
		
		choiceEd_RefreshFieldSimpleGlobalDictionary(
				&pGroup->pChoiceTableField, kMEFieldType_TextEntry, origEntry, entry,
				x, y, LARGE_COLUMN_WIDTH, UI_WIDGET( pGroup->pPane ), pGroup->pDoc,
				parse_ChoiceEntry, "ChoiceTable", "ChoiceTable" );
		x += LARGE_COLUMN_WIDTH;
	} else {
		ui_WidgetQueueFreeAndNull( &pGroup->pChoiceTableLabel );
		MEFieldSafeDestroy( &pGroup->pChoiceTableField );
	}

	if( entry->eType == CET_Value ) {
		// Update the weight
		if (pDoc->pChoiceTable->eSelectType == CST_Random) {
			choiceEd_RefreshFieldSimple( &pGroup->pWeightField, kMEFieldType_TextEntry, origEntry, entry,
				x, y, TINY_COLUMN_WIDTH, UI_WIDGET( pGroup->pPane ), pGroup->pDoc,
				parse_ChoiceEntry, "Weight" );
			x += TINY_COLUMN_WIDTH;
		} else {
			MEFieldSafeDestroy( &pGroup->pWeightField );
		}

		// Update each entry
		{
			int numDefs = eaSize( &pDoc->pChoiceTable->eaDefs );
			int it;

			eaSetSizeStruct( &entry->eaValues, parse_ChoiceValue, numDefs );
			for( it = eaSize( &pGroup->eaValueGroups ) - 1; it >= numDefs; --it ) {
				choiceEd_FreeValueGroup( pGroup->eaValueGroups[ it ]);
				eaRemove( &pGroup->eaValueGroups, it );
			}

			for( it = 0; it != numDefs; ++it ) {
				ChoiceValue* value = entry->eaValues[ it ];
				ChoiceValue* origValue = NULL;

				if( eaSize(&pGroup->eaValueGroups) <= it ) {
					ChoiceEd_ValueGroup* valueGroup = calloc( 1, sizeof( ChoiceEd_ValueGroup ));

					valueGroup->pEntry = pGroup;
					valueGroup->pPane = ui_PaneCreate( 0, 0, 0, 0, UIUnitFixed, UIUnitFixed, 0 );
					ui_PaneAddChild( pGroup->pPane, valueGroup->pPane );

					eaPush( &pGroup->eaValueGroups, valueGroup );
				}

				if( origEntry && eaSize( &origEntry->eaValues ) > it ) {
					origValue = origEntry->eaValues[ it ];
				}

				choiceEd_RefreshValueGroup( pDoc, x, y, pGroup->eaValueGroups[ it ], it, origValue, value );
				x += ui_WidgetGetWidth( UI_WIDGET( pGroup->eaValueGroups[ it ]->pPane ));
				maxY = MAX( maxY, ui_WidgetGetHeight( UI_WIDGET( pGroup->eaValueGroups[ it ]->pPane )));
			}

			// Fixup height, so that the shading done all looks right
			for( it = 0; it != numDefs; ++it ) {
				ui_WidgetSetHeight( UI_WIDGET( pGroup->eaValueGroups[ it ]->pPane ), maxY );
			}
		}
	} else {
		MEFieldSafeDestroy( &pGroup->pWeightField );

		{
			int it;
			for( it = eaSize( &pGroup->eaValueGroups ) - 1; it >= 0; --it ) {
				choiceEd_FreeValueGroup( pGroup->eaValueGroups[ it ]);
				eaRemove( &pGroup->eaValueGroups, it );
			}
		}
	}

	y += maxY;
	ui_WidgetSetDimensions( UI_WIDGET(pGroup->pPane), x, y );
}

void choiceEd_FreeEntryGroup(ChoiceEd_EntryGroup *pGroup)
{
	eaDestroyEx( &pGroup->eaValueGroups, choiceEd_FreeValueGroup );
	
	MEFieldSafeDestroy( &pGroup->pTypeField );
	MEFieldSafeDestroy( &pGroup->pChoiceTableField );
	MEFieldSafeDestroy( &pGroup->pWeightField );

	ui_WidgetQueueFreeAndNull( &pGroup->pPane );

	free( pGroup );
}

void choiceEd_AddEntry(UIButton* pButton, ChoiceEd_Doc *pDoc)
{
	ChoiceEntry* entryAccum;

	if( !emDocIsEditable( &pDoc->emDoc, true )) {
		return;
	}

	entryAccum = StructCreate( parse_ChoiceEntry );
	entryAccum->eType = CET_Value;
	eaPush( &pDoc->pChoiceTable->eaEntry, entryAccum );
	
	// Refresh the UI
	choiceEd_ChoiceTableChanged( pDoc, true );
}

void choiceEd_RemoveEntry(UIButton* pButton, ChoiceEd_EntryGroup *pGroup)
{
	ChoiceEd_Doc* pDoc = pGroup->pDoc;

	if( !emDocIsEditable( &pDoc->emDoc, true )) {
		return;
	}

	eaRemove( &pDoc->pChoiceTable->eaEntry, pGroup->index );

	// Refresh the UI
	choiceEd_ChoiceTableChanged( pDoc, true );
}
void choiceEd_EntryMoveUp( void* ignored1, ChoiceEd_EntryGroup* pGroup )
{
	ChoiceEd_Doc* pDoc = pGroup->pDoc;

	if( !emDocIsEditable( &pDoc->emDoc, true ) || pGroup->index <= 0 ) {
		return;
	}

	eaMove( &pDoc->pChoiceTable->eaEntry, pGroup->index - 1, pGroup->index );

	// Refresh the UI
	choiceEd_ChoiceTableChanged( pDoc, true );
}

void choiceEd_EntryMoveDown( void* ignored1, ChoiceEd_EntryGroup* pGroup )
{
	ChoiceEd_Doc* pDoc = pGroup->pDoc;

	if( !emDocIsEditable( &pDoc->emDoc, true ) || pGroup->index >= eaSize( &pDoc->pChoiceTable->eaEntry ) - 1 ) {
		return;
	}

	eaMove( &pDoc->pChoiceTable->eaEntry, pGroup->index + 1, pGroup->index );

	// Refresh the UI
	choiceEd_ChoiceTableChanged( pDoc, true );
}

void choiceEd_ActiveEntryClone( void* ignored1, ChoiceEd_EntryGroup* pGroup )
{
	ChoiceEntry* pEntry;
	ChoiceEd_Doc* pDoc = pGroup->pDoc;
	if( !emDocIsEditable( &pDoc->emDoc, true )) {
		return;
	}
	pEntry = pDoc->pChoiceTable->eaEntry[ pGroup->index ];

	// Perform the operation
	eaInsert( &pDoc->pChoiceTable->eaEntry, StructClone( parse_ChoiceEntry, pEntry ), pGroup->index + 1 );

	// Refresh the UI
	choiceEd_ChoiceTableChanged( pDoc, true );
}

void choiceEd_ActiveEntryCut( void* ignored1, ChoiceEd_EntryGroup* pGroup )
{
	ChoiceEntry* pEntry;
	ChoiceEd_Doc* pDoc = pGroup->pDoc;
	if( !emDocIsEditable( &pDoc->emDoc, true )) {
		return;
	}
	pEntry = pDoc->pChoiceTable->eaEntry[ pGroup->index ];

	// Perform the operation
	StructCopyAll( parse_ChoiceEntry, pEntry, &g_ChoiceEdClipboardEntry );
	StructDestroy( parse_ChoiceEntry, pEntry );
	eaRemove( &pDoc->pChoiceTable->eaEntry, pGroup->index );

	// Refresh the UI
	choiceEd_ChoiceTableChanged( pDoc, true );
}

void choiceEd_ActiveEntryCopy( void* ignored1, ChoiceEd_EntryGroup* pGroup )
{
	ChoiceEntry* pEntry;
	ChoiceEd_Doc* pDoc = pGroup->pDoc;
	pEntry = pDoc->pChoiceTable->eaEntry[ pGroup->index ];

	// Perform the operation
	StructCopyAll( parse_ChoiceEntry, pEntry, &g_ChoiceEdClipboardEntry );
}

void choiceEd_ActiveEntryPaste( void* ignored1, ChoiceEd_EntryGroup* pGroup )
{
	ChoiceEntry* pEntry;
	ChoiceEd_Doc* pDoc = pGroup->pDoc;
	if( !emDocIsEditable( &pDoc->emDoc, true )) {
		return;
	}
	pEntry = pDoc->pChoiceTable->eaEntry[ pGroup->index ];

	// Perform the operation
	StructCopyAll( parse_ChoiceEntry, &g_ChoiceEdClipboardEntry, pEntry );

	// Refresh the UI
	choiceEd_ChoiceTableChanged( pDoc, true );
}

void choiceEd_RefreshEntryHeader(ChoiceEd_Doc *pDoc)
{
	F32 x = X_OFFSET_BASE + 16;
	F32 y = 0;

	// space from the name
	x += TINY_COLUMN_WIDTH;
	
	// Update the type
	choiceEd_RefreshLabel( &pDoc->pHeaderTypeLabel, "Type", "How this entry will be specified.  Include includes a whole table, Value specifieds the entry's value explicitly.", x, y, UI_WIDGET( pDoc->pChoiceEntryScrollArea ));
	ui_LabelSetFont( pDoc->pHeaderTypeLabel, RefSystem_ReferentFromString(g_ui_FontDict, "Default_Bold"));
	x += TINY_COLUMN_WIDTH;
	
	// Update the weight
	if (pDoc->pChoiceTable->eSelectType == CST_Random) {
		choiceEd_RefreshLabel( &pDoc->pHeaderWeightLabel, "Weight", "The weight for this entry.  Higher weight will happen more often.", x, y, UI_WIDGET( pDoc->pChoiceEntryScrollArea ));
		ui_LabelSetFont( pDoc->pHeaderWeightLabel, RefSystem_ReferentFromString(g_ui_FontDict, "Default_Bold"));
		x += TINY_COLUMN_WIDTH;
	}
	else {
		ui_WidgetQueueFreeAndNull(&pDoc->pHeaderWeightLabel);
	}

	// Update each entry
	{
		int numDefs = eaSize( &pDoc->pChoiceTable->eaDefs );
		int it;
		
		for( it = eaSize( &pDoc->eaHeaderValueLabel ) - 1; it >= numDefs; --it ) {
			ui_WidgetQueueFreeAndNull( &pDoc->eaHeaderValueLabel[ it ]);
			ui_WidgetQueueFreeAndNull( &pDoc->eaHeaderValueButton[ it ]);
			ui_WidgetQueueFreeAndNull( &pDoc->eaHeaderValuePane[ it ]);
			eaRemove( &pDoc->eaHeaderValueLabel, it );
			eaRemove( &pDoc->eaHeaderValueButton, it );
			eaRemove( &pDoc->eaHeaderValuePane, it );
			eaRemove( &pDoc->eaFooterPane, it );
		}
		eaSetSize( &pDoc->eaHeaderValueLabel, numDefs );
		eaSetSize( &pDoc->eaHeaderValueButton, numDefs );
		eaSetSize( &pDoc->eaHeaderValuePane, numDefs );
		eaSetSize( &pDoc->eaFooterPane, numDefs );

		for( it = 0; it != numDefs; ++it ) {
			ChoiceTableValueDef* def = pDoc->pChoiceTable->eaDefs[ it ];

			if( !pDoc->eaHeaderValueButton[ it ]) {
				pDoc->eaHeaderValueButton[ it ] = ui_MenuButtonCreate( x + TINY_COLUMN_WIDTH + STANDARD_COLUMN_WIDTH - 20, y + 4 );
				ui_MenuButtonAppendItems( pDoc->eaHeaderValueButton[ it ],
										  ui_MenuItemCreate( "Copy", UIMenuCallback, choiceEd_ColumnCopy, pDoc->eaChoiceValueDefGroups[ it ], NULL ),
										  ui_MenuItemCreate( "Paste", UIMenuCallback, choiceEd_ColumnPaste, pDoc->eaChoiceValueDefGroups[ it ], NULL ),
										  NULL );
				pDoc->eaHeaderValueButton[ it ]->widget.priority = 10;
				ui_ScrollAreaAddChild( pDoc->pChoiceEntryScrollArea, pDoc->eaHeaderValueButton[ it ]);
			}
			else
				pDoc->eaHeaderValueButton[it]->widget.x = x + TINY_COLUMN_WIDTH + STANDARD_COLUMN_WIDTH - 20;

			if( !pDoc->eaHeaderValuePane[ it ]) {
				pDoc->eaHeaderValuePane[ it ] = ui_PaneCreate( x, y, TINY_COLUMN_WIDTH + STANDARD_COLUMN_WIDTH, STANDARD_ROW_HEIGHT,
															   UIUnitFixed, UIUnitFixed, 0 );
				pDoc->eaHeaderValuePane[ it ]->invisible = true;
				ui_ScrollAreaAddChild( pDoc->pChoiceEntryScrollArea, pDoc->eaHeaderValuePane[ it ]);
				
				pDoc->eaFooterPane[ it ] = ui_PaneCreate( x, y, TINY_COLUMN_WIDTH + STANDARD_COLUMN_WIDTH, STANDARD_ROW_HEIGHT,
														  UIUnitFixed, UIUnitFixed, 0 );
				pDoc->eaFooterPane[ it ]->invisible = true;
				ui_ScrollAreaAddChild( pDoc->pChoiceEntryScrollArea, pDoc->eaFooterPane[ it ]);
			}
			else
			{
				pDoc->eaHeaderValuePane[it]->widget.x = x;
				pDoc->eaFooterPane[it]->widget.x = x;
			}
			if( it % 2 == 0 ) {
				pDoc->eaHeaderValuePane[ it ]->widget.drawF = choiceEd_PaneDrawDarkValue;
				pDoc->eaFooterPane[ it ]->widget.drawF = choiceEd_PaneDrawDarkValue;
			} else {
				pDoc->eaHeaderValuePane[ it ]->widget.drawF = choiceEd_PaneDrawLightValue;
				pDoc->eaFooterPane[ it ]->widget.drawF = choiceEd_PaneDrawLightValue;
			}

			{
				char buffer[ 256 ];
				sprintf( buffer, "%s (%s)", def->pchName, StaticDefineIntRevLookup( WorldVariableTypeEnum, def->eType ));
				choiceEd_RefreshLabel( &pDoc->eaHeaderValueLabel[ it ], buffer, NULL, 2, 0, UI_WIDGET( pDoc->eaHeaderValuePane[ it ]));
			}
			ui_LabelSetFont( pDoc->eaHeaderValueLabel[ it ], RefSystem_ReferentFromString(g_ui_FontDict, "Default_Bold"));
			x += TINY_COLUMN_WIDTH + STANDARD_COLUMN_WIDTH;
		}
	}
}

void choiceEd_ColumnSort(void* ignored1, ChoiceEd_DefGroup* columnDef)
{
	;
}

void choiceEd_ColumnCopy(void* ignored1, ChoiceEd_DefGroup* columnDef)
{
	ChoiceEd_Doc* pDoc = columnDef->pDoc;
	ChoiceEntry** tableEntries = pDoc->pChoiceTable->eaEntry;
	char* accum = NULL;
	ChoiceTableValueDef* def = pDoc->pChoiceTable->eaDefs[ columnDef->index ];

	{
		int it;
		for( it = 0; it != eaSize( &tableEntries ); ++it ) {
			ChoiceEntry* entry = tableEntries[ it ];

			if( entry->eType != CET_Value ) {
				estrConcatStatic( &accum, "-- NOT A VALUE --\n" );
			} else if( entry->eaValues[ columnDef->index ]->eType == CVT_Value ) {
				WorldVariable* var = &entry->eaValues[ columnDef->index ]->value;
				char* varValue = NULL;
				var->eType = def->eType;
								
				if( var->eType == WVAR_MESSAGE ) {
					estrPrintf( &varValue, "%s", var->messageVal.pEditorCopy->pcDefaultString );
				} else {
					worldVariableToEString( var, &varValue );
				}
				estrConcatf( &accum, "%s\n", (varValue ? varValue : "") );
				estrDestroy( &varValue );
			} else if( entry->eaValues[ columnDef->index ]->eType == CVT_Choice ) {
				ChoiceValue* value = entry->eaValues[ columnDef->index ];
				
				estrConcatf( &accum, "choice:%s,%s\n",
							 REF_STRING_FROM_HANDLE(value->hChoiceTable),
							 value->pchChoiceName ? value->pchChoiceName : "" );
			}
		}
	}

	// copy to clipboard
	winCopyToClipboard( accum );
	estrDestroy( &accum );
}

void choiceEd_ColumnPaste(void* ignored1, ChoiceEd_DefGroup* columnDef)
{
	ChoiceEd_Doc* pDoc = columnDef->pDoc;
	ChoiceEntry*** tableEntries = &pDoc->pChoiceTable->eaEntry;
	char** data = NULL;
	ChoiceTableValueDef* def = pDoc->pChoiceTable->eaDefs[ columnDef->index ];
	
	if( !emDocIsEditable( &pDoc->emDoc, true )) {
		return;
	}

	if( eaSize( tableEntries ) == 0 ){
		Alertf( "Can not paste into an empty table." );
		return;
	}
	
	DivideString( winCopyFromClipboard(), "\n", &data, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS );

	if( eaSize( &data ) < eaSize( tableEntries )) {
		Alertf( "Pasting less rows than the whole table.  This probably won't do what you want." );
		eaDestroyEx( &data, NULL );
		return;
	}

	{
		int it;
		for( it = 0; it != eaSize( &data ); ++it ) {
			ChoiceEntry* entry; 
			
			if( it < eaSize( tableEntries )) {
				entry = (*tableEntries)[ it ];
			} else {
				entry = StructClone( parse_ChoiceEntry, (*tableEntries)[ eaSize( tableEntries ) - 1 ]);
				eaPush( tableEntries, entry );
			}

			if( entry->eType != CET_Value ) {
				continue;
			}

			if( strStartsWith( data[it], "choice:" )) {
				char* table = data[it] + strlen( "choice:" );
				char* comma = strchr( table, ',' );
				char* name = NULL;

				if( comma ) {
					*comma = '\0';
					name = comma + 1;
				}
				
				entry->eaValues[ columnDef->index ]->eType = CVT_Choice;
				SET_HANDLE_FROM_STRING( "ChoiceTable", table, entry->eaValues[ columnDef->index ]->hChoiceTable );
				entry->eaValues[ columnDef->index ]->pchChoiceName = strdup( name );
			} else {
				WorldVariable* var = &entry->eaValues[ columnDef->index ]->value;
				var->eType = def->eType;
				entry->eaValues[ columnDef->index ]->eType = CVT_Value;
				if( var->eType == WVAR_MESSAGE ) {
					if( !var->messageVal.pEditorCopy ) {
						var->messageVal.pEditorCopy = StructCreate(parse_Message);
						var->messageVal.bEditorCopyIsServer = true;
					}
					StructCopyString(&var->messageVal.pEditorCopy->pcDefaultString, data[ it ]);
				} else {
					worldVariableFromString( var, data[ it ], NULL );
				}
			}
		}
	}

	eaDestroyEx( &data, NULL );

	// Refresh the UI
	choiceEd_ChoiceTableChanged( pDoc, true );
}

/// ChoiceEd_ValueGroup functionality
void choiceEd_RefreshValueGroupInfrastructure( ChoiceEd_Doc *pDoc, F32 x, F32 y, ChoiceEd_ValueGroup *pGroup, int index, ChoiceValue *value )
{
	pGroup->index = index;
	
	ui_WidgetSetPosition( UI_WIDGET( pGroup->pPane ), x, y );

	{
		bool isDark = (index % 2 == 0);
		bool isChoice = (value->eType == CVT_Choice);

		if( isDark && isChoice ) {
			pGroup->pPane->widget.drawF = choiceEd_PaneDrawDarkChoice;
		} else if( isDark && !isChoice ) {
			pGroup->pPane->widget.drawF = choiceEd_PaneDrawDarkValue;
		} else if( !isDark && isChoice ) {
			pGroup->pPane->widget.drawF = choiceEd_PaneDrawLightChoice;
		} else if( !isDark && !isChoice ) {
			pGroup->pPane->widget.drawF = choiceEd_PaneDrawLightValue;
		}
	}
	pGroup->pPane->invisible = true;
}

void choiceEd_RefreshValueGroup(ChoiceEd_Doc *pDoc, F32 x, F32 y, ChoiceEd_ValueGroup *pGroup, int index, ChoiceValue *origValue, ChoiceValue *value)
{
	ChoiceTableValueDef* def = pDoc->pChoiceTable->eaDefs[ index ];
	int numLines = 1;
	
	choiceEd_RefreshValueGroupInfrastructure( pDoc, x, y, pGroup, index, value );

	x = 0;
	y = 0;
	
	// Update the type
	choiceEd_RefreshFieldSimpleEnum( &pGroup->pTypeField, kMEFieldType_Combo, origValue, value,
									 x, y, TINY_COLUMN_WIDTH, UI_WIDGET( pGroup->pPane ), pDoc,
									 parse_ChoiceValue, "Type", ChoiceValueTypeEnum );
	x += TINY_COLUMN_WIDTH;

	if( value->eType == CVT_Value ) {
		// VARIABLE_TYPES: Add code below if add to the available
		// variable types.
		// 
		// This can't use the GameEditorShared.c version because it
		// has a very different layout.
		if( def->eType == WVAR_INT ) {
			choiceEd_RefreshFieldSimple( &pGroup->pIntValueField, kMEFieldType_TextEntry, SAFE_MEMBER_ADDR( origValue, value ), &value->value,
										 x, y, STANDARD_COLUMN_WIDTH, UI_WIDGET( pGroup->pPane ), pDoc,
										 parse_WorldVariable, "IntVal" );
		} else {
			MEFieldSafeDestroy( &pGroup->pIntValueField );
		}
		if( def->eType == WVAR_FLOAT ) {
			choiceEd_RefreshFieldSimple( &pGroup->pFloatValueField, kMEFieldType_TextEntry, SAFE_MEMBER_ADDR( origValue, value ), &value->value,
										 x, y, STANDARD_COLUMN_WIDTH, UI_WIDGET( pGroup->pPane ), pDoc,
										 parse_WorldVariable, "FloatVal" );
		} else {
			MEFieldSafeDestroy( &pGroup->pFloatValueField );
		}
		if( def->eType == WVAR_STRING || def->eType == WVAR_LOCATION_STRING ) {
			choiceEd_RefreshFieldSimple( &pGroup->pStringValueField, kMEFieldType_TextEntry, SAFE_MEMBER_ADDR( origValue, value ), &value->value,
										 x, y, STANDARD_COLUMN_WIDTH, UI_WIDGET( pGroup->pPane ), pDoc,
										 parse_WorldVariable, "StringVal" );
		} else {
			MEFieldSafeDestroy( &pGroup->pStringValueField );
		}
		if( def->eType == WVAR_MESSAGE ) {
			choiceEd_RefreshFieldSimple( &pGroup->pMessageValueField, kMEFieldType_Message, SAFE_MEMBER_ADDR( origValue, value.messageVal ), &value->value.messageVal,
										 x, y, STANDARD_COLUMN_WIDTH, UI_WIDGET( pGroup->pPane ), pDoc,
										 parse_DisplayMessage, "EditorCopy" );
		} else {
			MEFieldSafeDestroy( &pGroup->pMessageValueField );
		}
		if( def->eType == WVAR_ANIMATION ) {
			choiceEd_RefreshFieldSimpleGlobalDictionary(
					&pGroup->pAnimationValueField, kMEFieldType_TextEntry, SAFE_MEMBER_ADDR( origValue, value ), &value->value,
					x, y, STANDARD_COLUMN_WIDTH, UI_WIDGET( pGroup->pPane ), pDoc,
					parse_WorldVariable, "StringVal", "AIAnimList" );
		} else {
			MEFieldSafeDestroy( &pGroup->pAnimationValueField );
		}
		if( def->eType == WVAR_CRITTER_DEF ) {
			choiceEd_RefreshFieldSimpleGlobalDictionary(
					&pGroup->pCritterDefValueField, kMEFieldType_TextEntry, SAFE_MEMBER_ADDR( origValue, value ), &value->value,
					x, y, STANDARD_COLUMN_WIDTH, UI_WIDGET( pGroup->pPane ), pDoc,
					parse_WorldVariable, "CritterDef", "CritterDef" );
		} else {
			MEFieldSafeDestroy( &pGroup->pCritterDefValueField );
		}
		if( def->eType == WVAR_CRITTER_GROUP ) {
			choiceEd_RefreshFieldSimpleGlobalDictionary(
					&pGroup->pCritterGroupValueField, kMEFieldType_TextEntry, SAFE_MEMBER_ADDR( origValue, value ), &value->value,
					x, y, STANDARD_COLUMN_WIDTH, UI_WIDGET( pGroup->pPane ), pDoc,
					parse_WorldVariable, "CritterGroup", "CritterGroup" );
		} else {
			MEFieldSafeDestroy( &pGroup->pCritterGroupValueField );
		}
		if( def->eType == WVAR_MAP_POINT ) {
			choiceEd_RefreshLabel( &pGroup->pZoneMapValueLabel, "ZMap", NULL, x, y, UI_WIDGET( pGroup->pPane ));
			choiceEd_RefreshFieldSimpleGlobalDictionary(
					&pGroup->pZoneMapValueField, kMEFieldType_TextEntry, SAFE_MEMBER_ADDR( origValue, value ), &value->value,
					x + 40, y, STANDARD_COLUMN_WIDTH - 40, UI_WIDGET( pGroup->pPane ), pDoc,
					parse_WorldVariable, "ZoneMap", "ZoneMap" );

			choiceEd_RefreshLabel( &pGroup->pSpawnPointValueLabel, "Spawn", NULL, x, y + STANDARD_ROW_HEIGHT, UI_WIDGET( pGroup->pPane ));
			choiceEd_RefreshFieldSimple(
					&pGroup->pSpawnPointValueField, kMEFieldType_TextEntry, SAFE_MEMBER_ADDR( origValue, value ), &value->value,
					x + 40, y + STANDARD_ROW_HEIGHT, STANDARD_COLUMN_WIDTH - 40, UI_WIDGET( pGroup->pPane ), pDoc,
					parse_WorldVariable, "StringVal" );
			numLines = 2;
		} else {
			ui_WidgetQueueFreeAndNull( &pGroup->pZoneMapValueLabel );
			ui_WidgetQueueFreeAndNull( &pGroup->pSpawnPointValueLabel );
			MEFieldSafeDestroy( &pGroup->pZoneMapValueField );
			MEFieldSafeDestroy( &pGroup->pSpawnPointValueField );
		}
		if( def->eType == WVAR_ITEM_DEF ) {
			choiceEd_RefreshFieldSimpleGlobalDictionary(
					&pGroup->pItemDefValueField, kMEFieldType_TextEntry, SAFE_MEMBER_ADDR( origValue, value ), &value->value,
					x, y, STANDARD_COLUMN_WIDTH, UI_WIDGET( pGroup->pPane ), pDoc,
					parse_WorldVariable, "StringVal", "ItemDef" );
		} else {
			MEFieldSafeDestroy( &pGroup->pItemDefValueField );
		}
		if( def->eType == WVAR_MISSION_DEF ) {
			choiceEd_RefreshFieldSimpleGlobalDictionary(
				&pGroup->pMissionDefValueField, kMEFieldType_TextEntry, SAFE_MEMBER_ADDR( origValue, value ), &value->value,
				x, y, STANDARD_COLUMN_WIDTH, UI_WIDGET( pGroup->pPane ), pDoc,
				parse_WorldVariable, "StringVal", "Mission" );
		} else {
			MEFieldSafeDestroy( &pGroup->pMissionDefValueField );
		}
		
		x += STANDARD_COLUMN_WIDTH;
	} else {
		ui_WidgetQueueFreeAndNull( &pGroup->pZoneMapValueLabel );
		ui_WidgetQueueFreeAndNull( &pGroup->pSpawnPointValueLabel );
		MEFieldSafeDestroy( &pGroup->pIntValueField );
		MEFieldSafeDestroy( &pGroup->pFloatValueField );
		MEFieldSafeDestroy( &pGroup->pStringValueField );
		MEFieldSafeDestroy( &pGroup->pMessageValueField );
		MEFieldSafeDestroy( &pGroup->pAnimationValueField );
		MEFieldSafeDestroy( &pGroup->pCritterDefValueField );
		MEFieldSafeDestroy( &pGroup->pCritterGroupValueField );
		MEFieldSafeDestroy( &pGroup->pZoneMapValueField );
		MEFieldSafeDestroy( &pGroup->pSpawnPointValueField );
		MEFieldSafeDestroy( &pGroup->pItemDefValueField );
		MEFieldSafeDestroy( &pGroup->pMissionDefValueField );
	}

	if( value->eType == CVT_Choice ) {
		ChoiceTable *includeTable = GET_REF(value->hChoiceTable);

		// Update the table
		choiceEd_RefreshLabel( &pGroup->pChoiceTableLabel, "Table", NULL, x, y, UI_WIDGET( pGroup->pPane ));
		choiceEd_RefreshFieldSimpleGlobalDictionary( &pGroup->pChoiceTableField, kMEFieldType_TextEntry, origValue, value,
													 x + 40, y, STANDARD_COLUMN_WIDTH - 40, UI_WIDGET( pGroup->pPane ), pDoc,
													 parse_ChoiceValue, "ChoiceTable", "ChoiceTable" );
		
		// Update the name
		if (includeTable) {
			eaCopyStructs( &includeTable->eaDefs, &pGroup->eaChoiceTableValueDefs, parse_ChoiceTableValueDef );
		} else {
			eaClearStruct( &pGroup->eaChoiceTableValueDefs, parse_ChoiceTableValueDef );
		}
		
		choiceEd_RefreshLabel( &pGroup->pChoiceNameLabel, "Name", NULL, x, y + STANDARD_ROW_HEIGHT, UI_WIDGET( pGroup->pPane ));
		choiceEd_RefreshFieldSimpleDataProvided(
				&pGroup->pChoiceNameField, kMEFieldType_TextEntry, origValue, value,
				x + 40, y + STANDARD_ROW_HEIGHT, STANDARD_COLUMN_WIDTH - 40, UI_WIDGET( pGroup->pPane ), pDoc,
				parse_ChoiceValue, "ChoiceName", parse_ChoiceTableValueDef, &pGroup->eaChoiceTableValueDefs, "Name" );

		numLines = 2;

		// Update the index
		if (includeTable && includeTable->eSelectType == CST_TimedRandom) {
			choiceEd_RefreshLabel( &pGroup->pChoiceIndexLabel, "Index", NULL, x, y + 2 * STANDARD_ROW_HEIGHT, UI_WIDGET( pGroup->pPane ));
			choiceEd_RefreshFieldSimple(
				&pGroup->pChoiceIndexField, kMEFieldType_Spinner, origValue, value,
				x + 40, y + 2 * STANDARD_ROW_HEIGHT, STANDARD_COLUMN_WIDTH - 40, UI_WIDGET( pGroup->pPane ), pDoc,
				parse_ChoiceValue, "ChoiceIndex");
			ui_SpinnerEntrySetBounds((UISpinnerEntry*) pGroup->pChoiceIndexField->pUIWidget, 1, choice_TimedRandomValuesPerInterval(includeTable), 1);

			numLines++;
		}

		x += STANDARD_COLUMN_WIDTH;
	} else {
		ui_WidgetQueueFreeAndNull( &pGroup->pChoiceTableLabel );
		MEFieldSafeDestroy( &pGroup->pChoiceTableField );
		ui_WidgetQueueFreeAndNull( &pGroup->pChoiceNameLabel );
		MEFieldSafeDestroy( &pGroup->pChoiceNameField );
		ui_WidgetQueueFreeAndNull( &pGroup->pChoiceIndexLabel );
		MEFieldSafeDestroy( &pGroup->pChoiceIndexField );
		eaDestroyStruct( &pGroup->eaChoiceTableValueDefs, parse_ChoiceTableValueDef );
	}

	ui_WidgetSetDimensions( UI_WIDGET(pGroup->pPane), TINY_COLUMN_WIDTH + STANDARD_COLUMN_WIDTH,
							STANDARD_ROW_HEIGHT * numLines);
}

void choiceEd_FreeValueGroup(ChoiceEd_ValueGroup *pGroup)
{

	MEFieldSafeDestroy( &pGroup->pTypeField );

	MEFieldSafeDestroy( &pGroup->pIntValueField );
	MEFieldSafeDestroy( &pGroup->pFloatValueField );
	MEFieldSafeDestroy( &pGroup->pStringValueField );
	MEFieldSafeDestroy( &pGroup->pMessageValueField );
	MEFieldSafeDestroy( &pGroup->pAnimationValueField );
	MEFieldSafeDestroy( &pGroup->pCritterDefValueField );
	MEFieldSafeDestroy( &pGroup->pCritterGroupValueField );
	MEFieldSafeDestroy( &pGroup->pZoneMapValueField );
	MEFieldSafeDestroy( &pGroup->pSpawnPointValueField );
	MEFieldSafeDestroy( &pGroup->pMissionDefValueField );

	MEFieldSafeDestroy( &pGroup->pChoiceTableField );
	eaDestroyStruct( &pGroup->eaChoiceTableValueDefs, parse_ChoiceTableValueDef );

	MEFieldSafeDestroy( &pGroup->pChoiceNameField );
	
	ui_WidgetQueueFreeAndNull( &pGroup->pPane );

	free( pGroup );
}

/// Tinted background drawing
void choiceEd_PaneDrawDarkChoice( UIPane* pane, UI_PARENT_ARGS )
{
	UI_GET_COORDINATES( pane );
	
	display_sprite( white_tex_atlas, x, y, z, w / white_tex_atlas->width, h / white_tex_atlas->height, 0x62571730 );
	ui_PaneDraw( pane, UI_PARENT_VALUES );
}

void choiceEd_PaneDrawDarkValue( UIPane* pane, UI_PARENT_ARGS )
{
	UI_GET_COORDINATES( pane );
	
	display_sprite( white_tex_atlas, x, y, z, w / white_tex_atlas->width, h / white_tex_atlas->height, 0x00000030 );
	ui_PaneDraw( pane, UI_PARENT_VALUES );
}

void choiceEd_PaneDrawLightChoice( UIPane* pane, UI_PARENT_ARGS )
{
	UI_GET_COORDINATES( pane );

	display_sprite( white_tex_atlas, x, y, z, w / white_tex_atlas->width, h / white_tex_atlas->height, 0xFFFF8C30 );
	ui_PaneDraw( pane, UI_PARENT_VALUES );
}

void choiceEd_PaneDrawLightValue( UIPane* pane, UI_PARENT_ARGS )
{
	ui_PaneDraw( pane, UI_PARENT_VALUES );
}


/// EM functionality
ChoiceEd_Doc* choiceEd_Open(EMEditor* pEditor, char* pcName)
{
	ChoiceEd_Doc *pDoc = NULL;
	ChoiceTable *pChoiceTable = NULL;
	bool bCreated = false;

	if (pcName && resIsEditingVersionAvailable(g_hChoiceTableDict, pcName)) {
		// Simply open the object since it is in the dictionary
		pChoiceTable = RefSystem_ReferentFromString(g_hChoiceTableDict, pcName);
	} else if (pcName) {
		// Wait for the object to show up so we can open it
		resSetDictionaryEditMode(g_hChoiceTableDict, true);
		emSetResourceState(pEditor, pcName, EMRES_STATE_OPENING);
		resRequestOpenResource(g_hChoiceTableDict, pcName);
	} else {
		// Create a new object since it is not in the dictionary
		bCreated = true;
	}

	if (pChoiceTable || bCreated) {
		pDoc = choiceEd_InitDoc(pChoiceTable, bCreated);
		choiceEd_InitDisplay(pEditor, pDoc);
		resFixFilename(g_hChoiceTableDict, pDoc->pChoiceTable->pchName, pDoc->pChoiceTable);
	}
	
	return pDoc;
}

void choiceEd_Revert(ChoiceEd_Doc* pDoc)
{
	ChoiceTable *pChoiceTable;

	if (!pDoc->emDoc.orig_doc_name[0]) {
		// Cannot revert if no original
		return;
	}

	pChoiceTable = RefSystem_ReferentFromString(g_hChoiceTableDict, pDoc->emDoc.orig_doc_name);
	if (pChoiceTable) {
		// Revert the choice table
		StructDestroy(parse_ChoiceTable, pDoc->pChoiceTable);
		StructDestroy(parse_ChoiceTable, pDoc->pOrigChoiceTable);
		pDoc->pChoiceTable = StructClone(parse_ChoiceTable, pChoiceTable);
		choiceEd_PostOpenFixup(pDoc->pChoiceTable);
		pDoc->pOrigChoiceTable = StructClone(parse_ChoiceTable, pDoc->pChoiceTable);

		// Clear the undo stack on revert
		EditUndoStackClear(pDoc->emDoc.edit_undo_stack);
		StructDestroy(parse_ChoiceTable, pDoc->pNextUndoChoiceTable);
		pDoc->pNextUndoChoiceTable = StructClone(parse_ChoiceTable, pDoc->pChoiceTable);

		// Refresh the UI
		pDoc->bIgnoreFieldChanges = true;
		pDoc->bIgnoreFilenameChanges = true;
		choiceEd_UpdateDisplay(pDoc);
		pDoc->bIgnoreFieldChanges = false;
		pDoc->bIgnoreFilenameChanges = false;
	}
}

void choiceEd_Close(ChoiceEd_Doc* pDoc)
{
	// Free the objects
	StructDestroy(parse_ChoiceTable, pDoc->pChoiceTable);
	if (pDoc->pOrigChoiceTable) {
		StructDestroy(parse_ChoiceTable, pDoc->pOrigChoiceTable);
	}
	StructDestroy(parse_ChoiceTable, pDoc->pNextUndoChoiceTable);

	eaDestroyEx(&pDoc->eaChoiceEntryGroups, choiceEd_FreeEntryGroup);
	eaDestroyEx(&pDoc->eaChoiceValueDefGroups, choiceEd_FreeDefGroup);
	eaDestroyEx(&pDoc->eaDocFields, MEFieldDestroy);

	// Close the window
	ui_WindowHide(pDoc->pMainWindow);
	ui_WidgetQueueFree(UI_WIDGET(pDoc->pMainWindow));
}

EMTaskStatus choiceEd_Save(ChoiceEd_Doc* pDoc, bool bSaveAsNew)
{
	EMTaskStatus status;
	const char *pcName;
	ChoiceTable *pChoiceTableCopy;

	// Deal with state changes
	pcName = pDoc->pChoiceTable->pchName;
	if (emHandleSaveResourceState(pDoc->emDoc.editor, pcName, &status)) {
		return status;
	}

	// Do cleanup before validation
	pChoiceTableCopy = StructClone(parse_ChoiceTable, pDoc->pChoiceTable);
	assert( pChoiceTableCopy );
	choiceEd_PreSaveFixup( pChoiceTableCopy );

	// Perform validation
	if (!choice_Validate( pChoiceTableCopy )) {
		StructDestroy(parse_ChoiceTable, pChoiceTableCopy);
		return EM_TASK_FAILED;
	}

	// Do the save (which will free the copy)
	status = emSmartSaveDoc(&pDoc->emDoc, pChoiceTableCopy, pDoc->pOrigChoiceTable, bSaveAsNew);

	return status;
}

void choiceEd_Init(EMEditor *pEditor)
{
	if (pEditor && !gInitializedEditor) {
		choiceEd_InitToolbarsAndMenus(pEditor);

		// Have Editor Manager handle a lot of change tracking
		emAutoHandleDictionaryStateChange(pEditor, "ChoiceTable", true, NULL, NULL, NULL, NULL, NULL);
		
		resGetUniqueScopes(g_hChoiceTableDict, &geaScopes);

		gInitializedEditor = true;
	}

	if (!gInitializedEditorData) {
		gBoldExpanderSkin = ui_SkinCreate(NULL);
		SET_HANDLE_FROM_STRING(g_ui_FontDict, "Default_Bold", gBoldExpanderSkin->hNormal);
		
		gInitializedEditorData = true;
	}
}

#endif
