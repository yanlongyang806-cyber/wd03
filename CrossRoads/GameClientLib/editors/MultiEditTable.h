//
// MultiEditTable.h
//

#pragma once
GCC_SYSTEM

#ifndef NO_EDITORS

#include "EditorManager.h"
#include "MultiEditCommon.h"
#include "MultiEditField.h"
#include "TimedCallback.h"

struct METable;

// Returns 0 on success and non-zero on failure to validate
typedef int (*MEValidateFunc)(struct METable *pTable, void *pObject, void *pUserData);

// Returns 1 if reorder succeeds and 0 if it fails
typedef int (*MEReorderFunc)(struct METable *pTable, void *pObject, void ***peaSubArray, int pos1, int pos2);

typedef void (*MEOrderFunc)(struct METable *pTable, void ***peaOrigSubArray, void ***peaOrderedSubArray);

typedef void *(*MECreateObjectFunc)(struct METable *pTable, void *pObjectToClone, bool bCloneKeepsKeys);

typedef void *(*MESubCreateFunc)(struct METable *pTable, void *pMainObject, void *pSubObjectToClone, void *pBeforeSubObject, void *pAfterSubObject);

typedef void (*METableChangeFunc)(struct METable *pTable, void *pObject, void *pUserData, bool bInitNotify);

typedef void (*METableSubChangeFunc)(struct METable *pTable, void *pObject, void *pSubObject, void *pUserData, bool bInitNotify);

typedef void (*MEPreSaveFunc)(struct METable *pTable, void *pObject);

typedef void (*MEPostOpenFunc)(struct METable *pTable, void *pObject, void *pOrigObject);

typedef void (*MEInheritanceFixFunc)(struct METable *pTable, void *pObject);

typedef EMSearchResult* (*MEFindFunc)(struct METable *pTable, void *pObject);

typedef void (*MEExitFunc)(struct METable *pTable, void *pUserData);

typedef char** (*METableDataFunc)(struct METable *pTable, void *pObject);

typedef void (*METableCustomMenuCallback)(struct METable *pTable, void *pObject, void *pUserData);


typedef char MEPromptFlags;
typedef char MEObjectFlags;

#define ME_EXPAND       1
#define ME_COLLAPSE     2

typedef struct ExprContext ExprContext;
typedef struct METable METable;
typedef struct MEWindow MEWindow;
typedef struct TimedCallback TimedCallback;
typedef struct UIButton UIButton;
typedef struct UIExpanderGroup UIExpanderGroup;
typedef struct UILabel UILabel;
typedef struct UIList UIList;
typedef struct UIMenu UIMenu;
typedef struct UIWindow UIWindow;

typedef struct MultiEditEMDoc {
	EMEditorDoc emDoc;
	struct MEWindow *pWindow;
} MultiEditEMDoc;

typedef struct MultiEditEMSubDoc {
	EMEditorSubDoc emSubDoc;
	void *pObject;
} MultiEditEMSubDoc;

#endif

// The AUTO_STRUCT must be present even if NO_EDITORS

// This struct exists as temp store for the parent name
AUTO_STRUCT;
typedef struct MEParentName {
	char *pcParentName;
} MEParentName;

#ifndef NO_EDITORS

typedef struct MEColData {
	struct METable *pTable;
	UIListColumn *pListColumn;
	F32 fDefaultWidth;

	char *pcGroup;
	char *pcPTName;
	char **eaExtraNames;
	S32 iColNum;
	bool bGroupHidden;
	bool bSmartHidden;
	bool bGroupAltTint;

	MEFieldType eType;
	StaticDefineInt *pEnum;
	ExprContext *pExprContext;
	bool bParentCol;
	int flags;
	MEFileBrowseData *pFileData;

	METableChangeFunc cbChange;
	void *pChangeUserData;

	const char *pcDictName;
	const char *pcGlobalDictName;
	ParseTable *pDictParseTable;
	const char *pcDictField;
	char **eaDictNames;
	METableDataFunc cbDataFunc;
} MEColData;

typedef struct MESubColData {
	struct METable *pTable;
	int iSubTableId;

	UIListColumn *pListColumn;
	F32 fDefaultWidth;

	char *pcGroup;
	char *pcPTName;
	ParseTable *pColParseTable;
	char **eaExtraNames;
	S32 iColNum;
	bool bGroupHidden;
	bool bSmartHidden;
	bool bGroupAltTint;

	MEFieldType eType;
	StaticDefineInt *pEnum;
	ExprContext *pExprContext;
	int flags;
	MEFileBrowseData *pFileData;

	METableSubChangeFunc cbChange;
	void *pChangeUserData;

	const char *pcDictName;
	const char *pcGlobalDictName;
	ParseTable *pDictParseTable;
	const char *pcDictField;
	char **eaDictNames;
	METableDataFunc cbDataFunc;
} MESubColData;

typedef struct METableSubRow {
	// The fields 
	MEField **eaFields;

	// Inheritance tracking
	bool bInherited;
} METableSubRow;

typedef struct METableSubTableData {
	void **eaObjects;
	METableSubRow **eaRows;
	METableSubRow **eaFakeModel;
	bool bHidden;
} METableSubTableData;

typedef struct METableRow {
	METable *pTable; // Ref to parent table

	// The data objects
	MultiEditEMSubDoc *pEditorSubDoc;
	void *pObject;
	void *pOrigObject;
	void *pParentObject;

	// The fields
	MEField **eaFields;
	METableSubTableData **eaSubData;

	// State
	int eExpand;
	bool bSaved;
	bool bCheckDirty;
	bool bDirty;
	bool bSaveOverwrite;
	bool bSaveRename;

	// Other row data
	EMFile *pEMFile;
} METableRow;

typedef struct MESubTable {
	int id;
	char *pcDisplayName;

	// Information relative to parent parse table
	char *pcSubPTName;
	int iNameIndexes[4]; // Allow up to two levels of nesting with -1 terminator

	// Sub list's parse table
	ParseTable *pParseTable;
	char *pcKeyPTName;
	int iKeyIndex;

	UIList *pList;

	MESubColData **eaCols;

	bool bHidden;

	MEOrderFunc cbOrder;
	MEReorderFunc cbReorder;
	MESubCreateFunc cbSubCreate;
} MESubTable;

typedef struct METable {
	MultiEditEMDoc *pEditorDoc;

	// The dictionary
	DictionaryHandle hDict;
	char *pcDisplayName;
	char *pcTypeName;
	char *pcDefaultFilePath;

	// Information on the columns
	ParseTable *pParseTable;
	char *pcNamePTName;
	int iNameIndex;
	char *pcFilePTName;
	int iFileIndex;
	char *pcScopePTName;
	int iScopeIndex;
	UIList *pList;
	UIMenu *pHeaderMenu;
	MEColData **eaCols;
	bool bHasParentCol;
	bool bHideUnused;

	// Information on the sub-columns
	MESubTable **eaSubTables;

	// The actual data
	METableRow **eaRows;

	// Used for row or cell selection
	UIList *pLastListClicked;

	// Tracking dictionary changes
	const char **eaChangedObjectNames;
	const char **eaMessageChangedObjectNames;
	bool bIgnoreChanges;
	bool bCheckSmartHidden;
 
	// Tracking the currently editable field
	MEField *pEditField;
	UIWidget *pEditWidget;

	// Tracking the current menu
	int iMenuTableRow;
	int iMenuTableCol;
	int iMenuSubTableId;
	int iMenuSubTableRow;
	UIMenu *pTableMenu;
	UIMenu *pSubTableMenu;
	int iSortDir;

	// Copy/Paste Data
	void **pPasteObject;
	int iPasteSubTableId;

	// Rows sometimes need to be freed on next tick
	TimedCallback *pTickCallback;
	METableRow **eaRowsToFreeOnNextTick;

	// Edit Row Window
	UIWindow *pEditWindow;
	UIExpanderGroup *pEditExpanderGroup;
	UIButton *pEditToggleButton;
	UILabel **eaEditLabels;
	UIWidget **eaEditWidgets;
	int iEditRow;
	int iEditSubTable;
	int iEditSubRow;
	bool bEditSubRow;
	bool bRefreshEditWindow;

	// Registered Callbacks
	MEValidateFunc cbValidate;
	void *pValidateUserData;
	METableCustomMenuCallback cbCustomMenu;
	void *pCustomMenuUserData;
	MECreateObjectFunc cbCreateObject;
	MEPreSaveFunc cbPreSave;
	MEPostOpenFunc cbPostOpen;
	MEInheritanceFixFunc cbFixInheritance;
} METable;

// Create a multi-edit table
// pSubParseTable may be NULL.  If it is, then pcSubPTName is ignored.
METable *METableCreate(char *pcDisplayName, char *pcTypeName, DictionaryHandle hDict, ParseTable *pParseTable, char *pcNamePTName, char *pcFilePTName, char *pcDefaultFilePath, MultiEditEMDoc *pEditorDoc);

// Release memory for the multi-edit table
void METableDestroy(METable *pTable);

// Get the multi-edit table's widget so UI facets can be set on it
UIWidget* METableGetWidget(METable *pTable);

// Create a sublist and returns a sublist ID
int METableCreateSubTable(METable *pTable, char *pcDisplayName,
						  char *pcSubPTName, ParseTable *pSubParseTable, char *pcSubKeyPTName,
						  MEOrderFunc cbOrder, MEReorderFunc cbReorder, MESubCreateFunc cbSubCreate);

// Get information on subtables
void METableGetSubTableInfo(METable *pTable, int **peaiIds, char ***peaTableNames);

// Define a column for the table
void METableAddColumn(METable *pTable, char *pcColLabel, char *pcColPTName, float fColWidth, char *pcColGroupName,
					  MEFieldType eType, StaticDefineInt *pEnum, ExprContext *pExprContext,
					  const char *pDictName, ParseTable *pDictParseTable, char *pcDictNamePTName, const char *pGlobalDictName,
					  METableDataFunc cbDataFunc);
void METableAddSimpleColumn(METable *pTable, char *pcColLabel, char *pcColPTName, float fColWidth, 
							char *pcColGroupName, MEFieldType eType);
void METableAddDictColumn(METable *pTable, char *pcColLabel, char *pcColPTName, float fColWidth, char *pcColGroupName,
						  MEFieldType eType, const char *pDictName, ParseTable *pDictParseTable, char *pcDictNamePTName);
void METableAddGlobalDictColumn(METable *pTable, char *pcColLabel, char *pcColPTName, float fColWidth, char *pcColGroupName,
						  MEFieldType eType, const char *pDictName, char *pcDictNamePTName);
void METableAddEnumColumn(METable *pTable, char *pcColLabel, char *pcColPTName, float fColWidth, 
						  char *pcColGroupName, MEFieldType eType, StaticDefineInt *pEnum);
void METableAddExprColumn(METable *pTable, char *pcColLabel, char *pcColPTName, float fColWidth, 
						  char *pcColGroupName, ExprContext *pExprContext);
void METableAddGameActionBlockColumn(METable *pTable, char *pcColLabel, char *pcColPTName, float fColWidth, 
						  char *pcColGroupName, ExprContext *pExprContext);
void METableAddGameEventColumn(METable *pTable, char *pcColLabel, char *pcColPTName, float fColWidth, 
									 char *pcColGroupName, ExprContext *pExprContext);
void METableAddFileNameColumn(METable *pTable, char *pcColLabel, char *pcColPTName, float fColWidth, 
							  char *pcColGroupName, char *pcBrowseTitle, char *pcTopDir, char *pcStartDir, char *pcExtension, UIBrowserMode eMode);
void METableAddScopeColumn(METable *pTable, char *pcColLabel, char *pcColPTName, float fColWidth, char *pcColGroupName, MEFieldType eType);

// Define a column for the table that holds an inheritance parent
void METableAddParentColumn(METable *pTable, char *pcColLabel, float fColWidth, char *pcColGroupName, bool bGlobalDictionary);

// Define a sub-column for the table.  pcColGroupName may be NULL.
void METableAddSubColumn(METable *pTable, int iSubTableId, 
						 char *pcColLabel, char *pcColPTName, ParseTable *pcColParseTable, float fColWidth, char *pcColGroupName,
						 MEFieldType eType, StaticDefineInt *pEnum, ExprContext *pExprContext,
						 const char *pDictName, ParseTable *pDictParseTable, char *pcDictNamePTName, const char *pGlobalDictName,
						 METableDataFunc cbDataFunc);
void METableAddSimpleSubColumn(METable *pTable, int iSubTableId, 
							   char *pcColLabel, char *pcColPTName, float fColWidth, char *pcColGroupName,
							   MEFieldType eType);
void METableAddDictSubColumn(METable *pTable, int iSubTableId, 
							 char *pcColLabel, char *pcColPTName, float fColWidth, char *pcColGroupName, MEFieldType eType,
							 const char *pDictName, ParseTable *pDictParseTable, char *pcDictNamePTName);
void METableAddGlobalDictSubColumn(METable *pTable, int iSubTableId, 
							 char *pcColLabel, char *pcColPTName, float fColWidth, char *pcColGroupName, MEFieldType eType,
							 const char *pDictName, char *pcDictNamePTName);
void METableAddEnumSubColumn(METable *pTable, int iSubTableId, 
							 char *pcColLabel, char *pcColPTName, float fColWidth, char *pcColGroupName, 
							 MEFieldType eType, StaticDefineInt *pEnum);
void METableAddExprSubColumn(METable *pTable, int iSubTableId, 
							 char *pcColLabel, char *pcColPTName, float fColWidth, char *pcColGroupName, 
							 ExprContext *pExprContext);
void METableAddGameActionBlockSubColumn(METable *pTable, int iSubTableId, 
							 char *pcColLabel, char *pcColPTName, float fColWidth, char *pcColGroupName, 
							 ExprContext *pExprContext);
void METableAddGameEventSubColumn(METable *pTable, int iSubTableId, 
										char *pcColLabel, char *pcColPTName, float fColWidth, char *pcColGroupName, 
										ExprContext *pExprContext);

void METableFinishColumns(METable *pTable);

// Control the locking of columns
// Note: All locked columns should have no column group or else problems can occur
void METableSetNumLockedColumns(METable *pTable, int iCount);
void METableSetNumLockedSubColumns(METable *pTable, int id, int iCount);

// Get information on columns
void METableGetColGroupNames(METable *pTable, char ***peaColGroups);
void METableGetSubColGroupNames(METable *pTable, int id, char ***peaColGroups);

// Set the state of a column
void METableSetColumnState(METable *pTable, char *pcColName, int columnStateFlags);

// Set the state of a sub-column
void METableSetSubColumnState(METable *pTable, int id, char *pcColName, int columnStateFlags);

// Add an alternate object path for the column
void METableAddColumnAlternatePath(METable *pTable, char *pcColName, char *pcAltPath);

// Add an alternate object path for the sub-column
void METableAddSubColumnAlternatePath(METable *pTable, int id, char *pcColName, char *pcAltPath);

// Add a row of data to the table.  The name is used to look up in the dictionary.
void METableAddRow(METable *pTable, char *pcName, bool bTop, bool bScrollTo);

// Add a newly created row that is not in the dictionary to the table
bool METableAddRowByObject(METable *pTable, void *pObject, bool bTop, bool bScrollTo);

// Add a child of the given object
void METableAddChildOfObject(METable *pTable, void *pObject, bool bTop, bool bScrollTo);

// Detect if an object is open already
bool METableIsObjectOpen(METable *pTable, char *pcName);

// Close all rows
void METableCloseAll(METable *pTable);

// Close one row
void METableCloseObject(METable *pTable, void *pObject);

// Revert all rows
void METableRevertAll(METable *pTable);

// Close one row
void METableRevertObject(METable *pTable, void *pObject);

// Save all rows
EMTaskStatus METableSaveAll(METable *pTable);

// Save one row
EMTaskStatus METableSaveObject(METable *pTable, void *pObject);

// Check out all rows
void METableCheckOutAll(METable *pTable);

// Undo check out all rows
void METableUndoCheckOutAll(METable *pTable);

// Check out all rows
void METableExpandAllRows(METable *pTable, int eExpand);

// Refresh the row from the data
void METableRefreshRow(METable *pTable, void *pObject);

// Regenerate the row completely to account for external changes
// Returns the new version of the object
void *METableRegenerateRow(METable *pTable, void *pObject);

// Deletes a sub-row from the object
void METableDeleteSubRow(METable *pTable, void *pObject, int id, void *pSubObject);

// Hide a sub-table
void METableHideSubTable(METable *pTable, void *pObject, int id, bool bHide);

// Hide a column group
void METableHideColGroup(METable *pTable, char *pcGroupName, bool bHide, bool bAltTint);
void METableHideSubColGroup(METable *pTable, int id, char *pcGroupName, bool bHide, bool bAltTint);

// Toggle a field's "not applicable" state
void METableSetFieldNotApplicable(METable *pTable, void *pObject, char *pcColName, bool bNotApplicable);

// Toggle a Subtable's field "not applicable" state
void METableSetSubFieldNotApplicable(METable *pTable, void *pObject, int id, void *pSubObject, const char *pcColName, bool bNotApplicable);

// Toggle all field's "not applicable" state within row's column group
void METableSetGroupNotApplicable(METable *pTable, void *pObject, char *pcGroupName, bool bNotApplicable);

// Toggle all field's "not applicable" state within a subrow's column group
void METableSetSubGroupNotApplicable(METable *pTable, void *pObject, int id, void *pSubObject, char *pcGroupName, bool bNotApplicable);

void METableSetFieldScale(METable *pTable, void *pObject, char *pcColName, F32 fScale);

void METableSetSubFieldScale(METable *pTable, void *pObject, int id, void *pSubObject, char *pcColName, F32 fScale);

// Set whether or not to hide unused columns
void METableSetHideUnused(METable *pTable, bool bHide);

// Convenience for making a new name
char *METableMakeNewName(METable *pTable, const char *pcBaseName);
const char *METableMakeNewNameShared(METable *pTable, const char *pcBaseName, bool stripTrailingDigits);

// Test if a given operation can be performed on the currently selected fields
bool METableCanEditFields(METable *pTable);
bool METableCanRevertFields(METable *pTable);
bool METableCanInheritFields(METable *pTable);
bool METableCanNoInheritFields(METable *pTable);
bool METableCanOpenFieldsInEditor(METable *pTable);

// Perform operation on currently selected fields
void METableEditFields(METable *pTable);
void METableRevertFields(METable *pTable);
void METableInheritFields(METable *pTable);
void METableNoInheritFields(METable *pTable);
void METableOpenFieldsInEditor(METable *pTable);

//Allows you to get a list of actual objects corresponding to all rows with cells that are selected.
void METableGetAllObjectsWithSelectedFields(METable *pTable, void*** peaObjects);

// Called by editor to report a dictionary change
void METableDictChanged(METable *pTable, enumResourceEventType eType, void * pReferent, const char * pRefData);
void METableMessageChangedRefresh(METable *pTable, const char *pcMessageKey);

// Sets the callback for validating an object
void METableSetValidateCallback(METable *pTable, MEValidateFunc cbValidate, void *pUserData);

// Sets the callback for validating an object
void METableSetCreateCallback(METable *pTable, MECreateObjectFunc cbCreate);

// Sets the post-open callback for object cleanup after opening
void METableSetPostOpenCallback(METable *pTable, MEPostOpenFunc cbPostOpen);

// Sets the callback for object cleanup prior to a save
void METableSetPreSaveCallback(METable *pTable, MEPreSaveFunc cbPreSave);

// Sets the inheritance fix callback
void METableSetInheritanceFixCallback(METable *pTable, MEInheritanceFixFunc cbFixInheritance);

// Sets the callback for changes to a column
void METableSetColumnChangeCallback(METable *pTable, char *pcColName, METableChangeFunc cbChange, void *pUserData);

// Sets the callback for changes to a sub-column
void METableSetSubColumnChangeCallback(METable *pTable, int id, char *pcColName, METableSubChangeFunc cbChange, void *pUserData);

// Saves prefs
void METableSavePrefs(METable *pTable);

// Close the Edit Row dialog
void METableCloseEditRow(METable *pTable);

// Add a custom menu item
// Can only be called once per table
void METableAddCustomAction(METable *pTable, const char *pcText, METableCustomMenuCallback cbCustomMenu, void *pCustomMenuUserData);

int met_getFieldData(ParseTable *pParseTable, char *pcField, void *pData, ParseTable **ppResultParseTable, int *piResultCol, void **ppResultData);
const char* met_getObjectName(METable *pTable, int iRow);

#define IS_HIDDEN(pCol)  ((pCol)->bGroupHidden || (pCol)->bSmartHidden || ((pCol)->flags & ME_STATE_HIDDEN))

#endif
