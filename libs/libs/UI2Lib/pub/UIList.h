/***************************************************************************



***************************************************************************/

#ifndef UI_LIST_H
#define UI_LIST_H
GCC_SYSTEM

#include "UICore.h"
#include "CBox.h"

typedef struct StashTableImp * StashTable;

#define UI_LIST_Z_DRAW_BEHIND_SELECTION_OFFSET 0.15

#define UI_LIST_Z_SELECTION 0.1
#define UI_LIST_Z_DRAW 0.2
#define UI_LIST_Z_LINES 0.6
#define UI_LIST_Z_HEADER_BACKGROUND 0.7
#define UI_LIST_Z_HEADERS 0.8
#define UI_LIST_Z_HEADER_TEXT 0.9

//////////////////////////////////////////////////////////////////////////
// A list, backed by a ParseTable and list of structures. A list is made
// up of columns, each of which draws rows. Since the list must be drawn
// lazily (i.e. only what's on screen) to handle large data sets, the row
// height must be known ahead of time.

typedef struct UIList UIList;
typedef struct UIListColumn UIListColumn;
typedef struct UIListSelectionState UIListSelectionState;
typedef struct UIListSelectionObject UIListSelectionObject;
typedef struct UIStyleFont UIStyleFont;

//////////////////////////////////////////////////////////////////////////
// You can tell the list to automatically show a context menu for
// hiding/showing columns when a column is right-clicked.
// See UIList::bAutoColumnContextMenu.
typedef struct UIMenu UIMenu;
typedef struct UIWindow UIWindow;
typedef struct UICheckButton UICheckButton;

// There are four ways to specify a column. Either the ParseTable name, or
// ParseTable index, or a callback function which is itself responsible for
// drawing the widget. The "list text callback", calls a callback that is
// only responsible for filling in an EString. It handles drawing.
// The texture name callback is the same, except the string returned
// is a texture name and not a displayed string.
typedef enum UIListColumnType
{
	UIListPTIndex,
	UIListPTName,
	UIListPTMessage,
	UIListCallback,
	UIListTextCallback,
	UIListTextureNameCallback,
	UIListSMFCallback,
} UIListColumnType;

// A list can have different modes.  Each mode renders slightly differently.
typedef enum UIListDisplayMode
{
	UIListRows,
	UIListIconGrid,

	UIListDisplayModeCount,
} UIListDisplayMode;

// For UIListCallback, called to draw the specified data.
typedef void (*UICellDrawFunc)(UIList *pList, UIListColumn *pColumn, UI_MY_ARGS, F32 z, CBox *pLogicalBox, S32 iRow, UserData pDrawData);

// For UIListTextCallback, called to fill in the output EString.
typedef void (*UITextDrawFunc)(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pDrawData, char **estrOutput);

// Called each frame the mouse is hovering over the list.
typedef void (*UIRowHoverFunc)(UIList *pList, UIListColumn *pColumn, S32 iRow, UserData pHoverData);

// Called when the user drags-and-drops a row in the list. The list will not
// change the model itself, but you can do an eaMove easily enough.
typedef void (*UIListDragFunc)(UIList *pList, S32 from, S32 to, UserData dragData);

// Called for various things that occur inside individuals cells.
// The CBox and mouse location is the logical position within the list
// widget's virtual space (i.e. adjusted by scale and offsets), and does
// not correspond to any physical screen space.
typedef void (*UIListCellActionFunc)(UIList *pList, S32 iColumn, S32 iRow, F32 fMouseX, F32 fMouseY, CBox *pBox, UserData pCellData);

// Used for changing the sublist model based on the parent list row
typedef void (*UIListModelChangeFunc)(UIList *pList, UIList *pSubList, S32 row, UserData pModelData);

typedef bool (*UIListSelectionFilterFunc)(UIListSelectionObject *pSelect, UserData pData);

typedef bool (*UIVisitColumn)(UIList *pSelect, UIListColumn *pColumn, S32 columnIndex, UserData pData);

// List Comparator Callback
typedef S32 (*UIListComparator)(SA_PARAM_NN_VALID UIList *pList, SA_PARAM_NN_VALID const void **ppA, SA_PARAM_NN_VALID const void **ppB);

#define UI_LIST_COLUMN_RESIZE_THRESHOLD UI_HSTEP
#define UI_LIST_COLUMN_MIN_WIDTH (3 * UI_LIST_COLUMN_RESIZE_THRESHOLD)

//////////////////////////////////////////////////////////////////////////
// Selection states are an internal book-keeping structure used by
// the list, to manage selections across multiple models. You should
// always use accessor functions to manage the selection, and not
// access these structs or functions directly.
typedef struct UIListSelectionObject
{
	UIList *pList;
	UIModel pModel;
	S32 iRow;
	UIListColumn *pColumn;
} UIListSelectionObject;

typedef struct UIListSelectionState
{
	UIList *pList;
	StashTable stSelected;
} UIListSelectionState;

UIListSelectionState *ui_ListGetSelectionState(const UIList *pList);
void ui_ListSelectionStateFree(UIListSelectionState *pState);
bool ui_ListSelectionIsSelected(const UIListSelectionState *pState, const UIList *pList, const UIListColumn *pColumn, S32 iRow);
S32 ui_ListSelectionStateGetSelectedRow(UIListSelectionState *pState);
void ui_ListSelectionStateGetSelectedRows(UIListSelectionState *pState, S32 **peaiRows);
void ui_ListSelectionStateGetSelectedSubRows(UIList *pList, S32 **peaiRows);
void ui_ListSelectionStateSelect(UIListSelectionState *pState, UIList *pList, UIListColumn *pColumn, S32 iRow);
void ui_ListSelectionStateUnselect(UIListSelectionState *pState, UIList *pList, UIListColumn *pColumn, S32 iRow);
void ui_ListSelectionStateToggle(UIListSelectionState *pState, UIList *pList, UIListColumn *pColumn, S32 iRow);
void ui_ListSelectionStateClearInactive(UIListSelectionState *pState, UIList *pList);
void ui_ListSelectionStateClear(UIListSelectionState *pState);

bool ui_ListSelectionFilterColumnMatches(UIListSelectionObject *pSelect, UIListColumn *pColumn);
bool ui_ListSelectionListAndModelNotMatches(UIListSelectionObject *pSelect, UIList *pList);
void ui_ListSelectionUnselectIf(UIListSelectionState *pState, UIListSelectionFilterFunc cbFilter, UserData pData);
void ui_ListSelectionSelectIf(UIListSelectionState *pState, UIListSelectionFilterFunc cbSelect, UserData pSelectData);

typedef struct UIListColumn
{
	UIListColumnType eType;
	char *pchTitleString_USEACCESSOR;
	REF_TO(Message) hTitleMessage_USEACCESSOR;
	F32 fWidth;

	bool bResizable : 1;
	bool bAutoSize : 1;

	bool bDragging : 1;

	bool bHidden : 1;

	bool bCanHide : 1;

	bool bShowCheckBox : 1; // Displays checkbox in each row

	bool bSortable : 1;
	UISortType eSort;

	UIDirection eHeaderAlignment;
	UIDirection eAlignment;
	union
	{
		S32 iTableIndex;
		const char *pchTableName;
		UICellDrawFunc cbDraw;
		UITextDrawFunc cbText;
	} contents;

	S32 iParseIndexCache;
	ParseTable *pLastTable;

	StashTable stCache;

	// Only used for UICellDrawFuncs.
	UserData pDrawData;

	UIActivationFunc cbClicked;
	UserData pClickedData;

	UIActivationFunc cbContext;
	UserData pContextData;

} UIListColumn;

typedef struct UIList
{
	UI_INHERIT_FROM(UI_WIDGET_TYPE);

	UIListColumn **eaColumns;
	ParseTable *pTable;
	UIModel peaModel;

	UIListSelectionState *pSelection;
	UIListColumn* hoverCol;
	S32 hoverRow;

	// When this is on and a column header is right-clicked, a context menu is displayed that allows hiding/showing of the list columns.
	bool bAutoColumnContextMenu : 1;

	bool bMultiSelect : 1;
	bool bColumnSelect : 1;
	bool bToggleSelect : 1;
	bool bIsComboBoxDropdown : 1;

	bool bRowsDraggable : 1;
	S32 iDraggingRow;
	// The position we are dragging the current dragRow to.
	S32 iDragToRow;

	// NEVER access this variable directly.
	S32 *eaiSelectedRowsInternal;

	// Called when the user finishes dragging a row.
	UIListDragFunc cbDrag;
	UserData pDragData;

	// Called when the user presses the left mouse button.
	// The default implementation of this changes the selection.
	UIListCellActionFunc cbCellClicked;
	UserData pCellClickedData;

	// Called when the user presses the right mouse button over a cell.
	UIListCellActionFunc cbCellContext;
	UserData pCellContextData;

	// Called when the selection changes.
	UIActivationFunc cbSelected;
	UserData pSelectedData;

	// Called when the user double-left-clicks.
	UIListCellActionFunc cbCellActivated; 
	UserData pCellActivatedData;
	UIActivationFunc cbActivated;
	UserData pActivatedData;

	// Called each frame when hovering.
	UIListCellActionFunc cbCellHover; 
	UserData pCellHoverData;
	UIRowHoverFunc cbRowHover;
	UserData pRowHoverData;
	bool bRowHoverActive; // Internal: tracks hover state

	F32 fRowHeight;
	F32 fGridCellWidth;
	F32 fGridCellHeight;
	int iGridColumn;
	F32 fHeaderHeight;
	S32 *eaiScrollToPath;
	bool bScrollToCenter;

	S32 iLastColumn;
	S32 iNumLockedColumns;

	// For automatic handling of prefs
	int iPrefSet;

	// When true, the model is only sorted when the sort column or direction changes. Otherwise, sorting occurs every tick, which is the default
	// Use this flag to optimize the sorting of large lists when the data rarely, if ever changes.
	bool bSortOnlyOnUIChange;

	UIListColumn *pSortColumn;
	S32 iSortedIndex; // -1 for no sorting, index into parse table, *not* columns
	UISortType eSort;

	bool bDrawSelection : 1;
	bool bDrawHover : 1;
	bool bLastRowHasSingleColumn : 1; // A special case flag for the UGCBugReport list.

	bool bDrawGrid : 1;
	bool bUseBackgroundColor : 1;
	Color backgroundColor;

	// Flags for controlling different modes
	bool bShowDisplayModePicker;
	UIListDisplayMode eDisplayMode;
	UIWidget** eaPickerButtons;

	// TODO: This does not really need to be an entire list, since we don't
	// look at any of its UIWidget properties.
	UIList **eaSubLists;
	UIList *pParentList;
	F32 fSubListIndent;
	S32 iSubIndex; // Index into the parse table to use for submodels.
	S32 iActiveSubRow;
	UIListModelChangeFunc cbModelChange;
	UserData pModelData;

	UIMenu *pAutoColumnContextMenu;
	UIWindow *pAutoColumnContextMenuShowMoreWindow;
	UICheckButton **eaAutoColumnContextMenuShowMoreWindowCheckButtons;

	// Cached data about where things are rendered
	CBox lastDrawBox;
} UIList;

//////////////////////////////////////////////////////////////////////////
// Lists are divided into columns, which present visual and logical
// division between different fields in the list. Columns can be resized,
// used to sort elements, etc. Most columns are defined by a row in the
// parse table, but they can also have totally custom appearances.

// contents should be either an integer, or a char *, or a UICellDrawFunc, depending on
// what you passed in as 'type'.
SA_RET_NN_VALID UIListColumn *ui_ListColumnCreate(UIListColumnType eType, SA_PARAM_OP_STR const char *pchTitle, intptr_t contents, UserData pDrawData);
SA_RET_NN_VALID UIListColumn *ui_ListColumnCreateMsg(UIListColumnType eType, SA_PARAM_OP_STR const char *pchTitle, intptr_t contents, UserData pDrawData);
SA_RET_NN_VALID UIListColumn *ui_ListColumnCreatePref(UIListColumnType eType, SA_PARAM_OP_STR const char* pchTitle, intptr_t contents, UserData pDrawData, int iPrefSet, const char* pchPrefStr, F32 wDef);
SA_RET_NN_VALID static __forceinline UIListColumn *ui_ListColumnCreateParseName(SA_PARAM_OP_STR const char *pchTitle, const char *pchFieldName, UserData pDrawData) { return ui_ListColumnCreate(UIListPTName, pchTitle, (intptr_t)pchFieldName, pDrawData); }
SA_RET_NN_VALID static __forceinline UIListColumn *ui_ListColumnCreateParseIndex(SA_PARAM_OP_STR const char *pchTitle, S32 iFieldIndex, UserData pDrawData) { return ui_ListColumnCreate(UIListPTIndex, pchTitle, (intptr_t)iFieldIndex, pDrawData); }
SA_RET_NN_VALID static __forceinline UIListColumn *ui_ListColumnCreateCallback(SA_PARAM_OP_STR const char *pchTitle, UICellDrawFunc cbDraw, UserData pDrawData) { return ui_ListColumnCreate(UIListCallback, pchTitle, (intptr_t)cbDraw, pDrawData); }
SA_RET_NN_VALID static __forceinline UIListColumn *ui_ListColumnCreateText(SA_PARAM_OP_STR const char *pchTitle, UITextDrawFunc cbText, UserData pDrawData) { return ui_ListColumnCreate(UIListTextCallback, pchTitle, (intptr_t)cbText, pDrawData); }
SA_RET_NN_VALID static __forceinline UIListColumn *ui_ListColumnCreateTextMsg(SA_PARAM_OP_STR const char *pchTitle, UITextDrawFunc cbText, UserData pDrawData) { return ui_ListColumnCreateMsg(UIListTextCallback, pchTitle, (intptr_t)cbText, pDrawData); }
SA_RET_NN_VALID static __forceinline UIListColumn *ui_ListColumnCreateTexture(SA_PARAM_OP_STR const char *pchTitle, UITextDrawFunc cbText, UserData pDrawData) { return ui_ListColumnCreate(UIListTextureNameCallback, pchTitle, (intptr_t)cbText, pDrawData); }
SA_RET_NN_VALID static __forceinline UIListColumn *ui_ListColumnCreateSMF(SA_PARAM_OP_STR const char *pchTitle, UITextDrawFunc cbText, UserData pDrawData) { return ui_ListColumnCreate(UIListSMFCallback, pchTitle, (intptr_t)cbText, pDrawData); }
SA_RET_NN_VALID static __forceinline UIListColumn *ui_ListColumnCreateParseMessage(SA_PARAM_OP_STR const char *pchTitle, const char *pchFieldName, UserData pDrawData) { return ui_ListColumnCreate(UIListPTMessage, pchTitle, (intptr_t)pchFieldName, pDrawData); }

void ui_ListColumnFree(SA_PRE_NN_VALID SA_POST_P_FREE UIListColumn *pColumn);

// Re-set the type of the column.
void ui_ListColumnSetType(SA_PARAM_NN_VALID UIListColumn *pColumn, UIListColumnType eType, intptr_t contents, UserData pDrawData);

// Set the title of this column; NULL means no title.
void ui_ListColumnSetTitleString(SA_PARAM_NN_VALID UIListColumn *pColumn, SA_PARAM_OP_STR const char *pchTitle);

// Set the title of this column; NULL means no title.
void ui_ListColumnSetTitleMessage(SA_PARAM_NN_VALID UIListColumn *pColumn, SA_PARAM_OP_STR const char *pchTitle);

// Get the title of this column
const char* ui_ListColumnGetTitle(SA_PARAM_NN_VALID UIListColumn *pColumn);

// Set whether or not this column is hidden.
void ui_ListColumnSetHidden(SA_PARAM_NN_VALID UIListColumn *pColumn, bool bHidden);

// Set the alignment of the text in this column. UILeft, UINoDirection, or UIRight.
void ui_ListColumnSetAlignment(SA_PARAM_NN_VALID UIListColumn *pColumn, UIDirection eHeaderAlignment, UIDirection eAlignment);

// Set the width of this column, and whether or not to autosize it.
void ui_ListColumnSetWidth(SA_PARAM_NN_VALID UIListColumn *pColumn, bool bAutoSize, F32 fWidth);

// Set if this column is manually resizable or not.
void ui_ListColumnSetResizable(SA_PARAM_NN_VALID UIListColumn *pColumn, bool bResizable);

// This allows the columns to be sorted when the header is clicked. You can
// only sort columns that use parse tables. It uses the TokenCompare functions
// to sort appropriately.
void ui_ListColumnSetSortable(SA_PARAM_NN_VALID UIListColumn *pColumn, bool bSortable);

// Called when the list column header is left-clicked.
void ui_ListColumnSetClickedCallback(SA_PARAM_NN_VALID UIListColumn *pColumn, UIActivationFunc cbClicked, UserData pClickedData);

// Sets whether or not the list column can be hidden. This is used by automatic column context menu to determine what columns the user is allowed to hide.
void ui_ListColumnSetCanHide(SA_PARAM_NN_VALID UIListColumn *pColumn, bool canHide);

// Called when the list column header is right-clicked.
void ui_ListColumnSetContextCallback(SA_PARAM_NN_VALID UIListColumn *pColumn, UIActivationFunc cbContext, UserData pContextData);

// Draw this list column in this list.
void ui_ListColumnDraw(SA_PARAM_NN_VALID UIListColumn *pColumn, SA_PARAM_NN_VALID UIList *pList, F32 fStartX, F32 fEndX, F32 fStartY, S32 iStartRow, S32 iEndRow, F32 scale, F32 z, F32 y);

// Called by the default draw functions, you can use it to write your own.
float ui_ListDrawTextInBox(UIDirection eAlignment, UI_MY_ARGS, F32 z, const char *pchText);
void ui_ListDefaultDrawSMFInBox(SA_PARAM_NN_VALID UIList *pList, SA_PARAM_NN_VALID UIListColumn *pColumn, UI_MY_ARGS, F32 z, SA_PARAM_OP_VALID CBox *pLogicalBox, S32 iRow, UserData pDrawData, SA_PARAM_NN_VALID const void *pKey, SA_PARAM_NN_STR const char *pchText, U8 chAlpha, bool bAutoHeight);

// The draw function used for ParseTable name/index columns.
void ui_ListDefaultDrawFunction(SA_PARAM_NN_VALID UIList *pList, SA_PARAM_NN_VALID UIListColumn *pColumn, UI_MY_ARGS, F32 z, CBox *pLogicalBox, S32 iRow, UserData pDrawData);

// The draw function for UIListColumnDrawText column types.
void ui_ListDefaultDrawTextFunction(SA_PARAM_NN_VALID UIList *pList, SA_PARAM_NN_VALID UIListColumn *pColumn, UI_MY_ARGS, F32 z, CBox *pLogicalBox, S32 iRow, UserData pDrawData);

// Draw function for UIListColumnDrawTextureName.
void ui_ListDefaultDrawTextureFunction(UIList *pList, UIListColumn *pColumn, UI_MY_ARGS, F32 z, CBox *pLogicalBox, S32 iRow, UserData pDrawData);

// Draw function for UIListColumnDrawSMF.
void ui_ListDefaultDrawSMFFunction(UIList *pList, UIListColumn *pColumn, UI_MY_ARGS, F32 z, CBox *pLogicalBox, S32 iRow, UserData pDrawData);

// Select this entire column if possible. Use either by calling the
// ui_ListSelectColumn function directly, or by setting the column's
// clicked callback to ui_ListColumnSelectCallback, with user data of
// the list that the column is in.
void ui_ListSelectColumn(UIList *pList, UIListColumn *pColumn);
void ui_ListColumnSelectCallback(UIListColumn *pColumn, UIList *pList);

//////////////////////////////////////////////////////////////////////////
// Basic UIList functions
SA_RET_NN_VALID UIList *ui_ListCreate(SA_PARAM_OP_VALID ParseTable *pTable, SA_PARAM_OP_VALID UIModel peaModel, F32 fRowHeight);
void ui_ListInitialize(SA_PRE_NN_FREE SA_POST_NN_VALID UIList *pList, SA_PARAM_OP_VALID ParseTable *pTable, SA_PARAM_OP_VALID UIModel peaModel, F32 fRowHeight);
void ui_ListFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UIList *pList);
void ui_ListTick(SA_PARAM_NN_VALID UIList *pList, UI_PARENT_ARGS);
void ui_ListDraw(SA_PARAM_NN_VALID UIList *pList, UI_PARENT_ARGS);
bool ui_ListInput(SA_PARAM_NN_VALID UIList *pList, SA_PARAM_NN_VALID KeyInput *pKey);

//////////////////////////////////////////////////////////////////////////
// Misc. access functions
void ui_ListScrollToRow(SA_PARAM_NN_VALID UIList *pList, S32 iRow);
void ui_ListScrollToSelection(SA_PARAM_NN_VALID UIList *pList);

// Internal function
void ui_ListScrollToPath(UIList *pList, UI_MY_ARGS);

//////////////////////////////////////////////////////////////////////////
// Column management
void ui_ListAppendColumn(SA_PARAM_NN_VALID UIList *pList, SA_PARAM_NN_VALID UIListColumn *pColumn);
void ui_ListRemoveColumn(SA_PARAM_NN_VALID UIList *pList, SA_PARAM_NN_VALID UIListColumn *pColumn);
void ui_ListAddGridIconColumn(SA_PARAM_NN_VALID UIList* pList, SA_PARAM_NN_VALID UIListColumn* pColumn);
void ui_ListSetSortedColumn(SA_PARAM_NN_VALID UIList *pList, S32 iColIndex);
S32 ui_ListColumnGetParseTableIndex(SA_PARAM_NN_VALID UIList *pList, SA_PARAM_NN_VALID UIListColumn *pColumn);

void ui_ListSetNumLockedColumns(SA_PARAM_NN_VALID UIList *pList, S32 iCount);

// Visits each column in descending order. If callback returns true, the iteration stops and returns true
bool ui_ListVisitColumns(SA_PARAM_NN_VALID UIList *pList, UIVisitColumn fpVisit, UserData data);

// Returns the first column found by the given title name, case insensitive 
UIListColumn* ui_ListFindColumnByTitleName(SA_PARAM_NN_VALID UIList *pList, SA_PARAM_NN_VALID const char *pchName);

//////////////////////////////////////////////////////////////////////////
// Model and ParseTable accessors
void ui_ListSetModel(SA_PARAM_NN_VALID UIList *pList, SA_PARAM_OP_VALID ParseTable *pTable, SA_PARAM_OP_VALID UIModel peaModel);
SA_RET_OP_VALID UIModel ui_ListGetModel(SA_PARAM_NN_VALID const UIList *pList);


//////////////////////////////////////////////////////////////////////////
// Callbacks
void ui_ListSetSelectedCallback(SA_PARAM_NN_VALID UIList *pList, UIActivationFunc cbSelected, UserData pSelectedData);
void ui_ListSetActivatedCallback(SA_PARAM_NN_VALID UIList *pList, UIActivationFunc cbActivated, UserData pActivatedData);
void ui_ListSetContextCallback(SA_PARAM_NN_VALID UIList *pList, UIActivationFunc cbContext, UserData pContextData);

// Called when the mouse hovers over an item in the list. This is called every frame.
void ui_ListSetHoverCallback(SA_PARAM_NN_VALID UIList *pList, UIRowHoverFunc cbHover, UserData pHoverData);

// Called when the user drags an item from one location to another.
void ui_ListSetDragCallback(SA_PARAM_NN_VALID UIList *pList, UIListDragFunc cbDrag, UserData pDragData);

// Use this function to call cbChanged. Because of sublists (see below),
// it is not as simple as just calling the function.
void ui_ListCallSelectionChangedCallback(SA_PARAM_NN_VALID UIList *pList);

// The CBoxes that these functions receive are within the list's virtual space.
// That is, they don't correspond to real screen coordinates, so don't use
// these callbacks to draw things there. However, they are useful for positioning
// widgets within the list. If you need screen coordinates, the draw callback
// functions get those.
void ui_ListSetCellClickedCallback(SA_PARAM_NN_VALID UIList *pList, UIListCellActionFunc cbCellClicked, UserData pCellClickedData);
void ui_ListSetCellContextCallback(SA_PARAM_NN_VALID UIList *pList, UIListCellActionFunc cbCellContext, UserData pCellContextData);
void ui_ListSetCellActivatedCallback(SA_PARAM_NN_VALID UIList *pList, UIListCellActionFunc cbCellActivated, UserData pCellActivatedData);
void ui_ListSetCellHoverCallback(SA_PARAM_NN_VALID UIList *pList, UIListCellActionFunc cbCellHover, UserData pCellHoverData);

// The default implementations just change the selection.
void ui_ListCellClickedDefault(UIList *pList, S32 iColumn, S32 iRow, F32 fMouseX, F32 fMouseY, CBox *pCellBox, UserData pCellData);
void ui_ListCellContextDefault(UIList *pList, S32 iColumn, S32 iRow, F32 fMouseX, F32 fMouseY, CBox *pCellBox, UserData pCellData);
void ui_ListCellActivatedDefault(UIList *pList, S32 iColumn, S32 iRow, F32 fMouseX, F32 fMouseY, CBox *pCellBox, UserData pCellData);

//////////////////////////////////////////////////////////////////////////
// Selection management. By default, lists may have one selected row, but
// if multiselect is enabled they can have any number of selected rows.
S32 ui_ListGetSelectedRow(SA_PARAM_NN_VALID UIList *pList);
SA_RET_OP_VALID void *ui_ListGetSelectedObject(SA_PARAM_NN_VALID UIList *pList);
SA_RET_OP_VALID UIListColumn *ui_ListGetSelectedColumn(SA_PARAM_NN_VALID const UIList *pList);
void ui_ListGetSelectedRowBox(SA_PARAM_NN_VALID UIList* pList, CBox* box );

void ui_ListSetSelectedRow(SA_PARAM_NN_VALID UIList *pList, S32 iRow);
void ui_ListSetSelectedRowAndCallback(SA_PARAM_NN_VALID UIList *pList, S32 iRow);

void ui_ListSetSelectedObject(SA_PARAM_NN_VALID UIList *pList, const void *pObj);
void ui_ListSetSelectedObjectAndCallback(SA_PARAM_NN_VALID UIList *pList, const void *pObj);

void ui_ListSetSelectedRowCol(SA_PARAM_NN_VALID UIList *pList, S32 iRow, S32 iCol);
void ui_ListSetSelectedRowColEx(UIList *pList, S32 iRow, S32 iCol, bool bExtendSelection, bool bInvertSelection);
void ui_ListSetSelectedRowColAndCallback(SA_PARAM_NN_VALID UIList *pList, S32 iRow, S32 iCol);
void ui_ListSetSelectedRowColExAndCallback(SA_PARAM_NN_VALID UIList *pList, S32 iRow, S32 iCol, bool bExtendSelection, bool bInvertSelection);

void ui_ListSetMultiselect(SA_PARAM_NN_VALID UIList *pList, bool bMultiSelect);

// Controls automatic column context menu. When this is on and a column header is right-clicked, a context menu is displayed that allows hiding/showing of the list columns.
void ui_ListSetAutoColumnContextMenu(SA_PARAM_NN_VALID UIList *pList, bool bAutoColumnContextMenu);

bool ui_ListIsRowSelected(SA_PARAM_NN_VALID UIList *pList, S32 iRow);
bool ui_ListIsAnyObjectSelected(SA_PARAM_NN_VALID UIList *pList);
SA_ORET_NN_VALID const S32 * const *ui_ListGetSelectedRows(SA_PARAM_NN_VALID UIList *pList);
SA_ORET_NN_VALID const S32 * const *ui_ListGetSelectedSubRows(SA_PARAM_NN_VALID UIList *pList);
SA_ORET_NN_VALID const S32 * const *ui_ListGetSelectedSubRowsSorted(SA_PARAM_NN_VALID UIList *pList);

void ui_ListSetSelectedRows(SA_PARAM_NN_VALID UIList *pList, SA_PRE_NN_OP_VALID S32 **peaiRows);
void ui_ListSetSelectedRowsAndCallback(SA_PARAM_NN_VALID UIList *pList, SA_PRE_NN_OP_VALID S32 **peaiRows);

void ui_ListClearSelectedRows(SA_PARAM_NN_VALID UIList *pList);

void ui_ListNotifySelectionOfRowDeletion(SA_PARAM_NN_VALID UIList *pList, S32 iRow);
void ui_ListSelectOrDeselectRowEx(SA_PARAM_NN_VALID UIList *pList, S32 iRow, bool bExtendSelection, bool bInvertSelection);
void ui_ListSelectRowToggle(SA_PARAM_NN_VALID UIList *pList, S32 iRow);
void ui_ListSelectRow(SA_PARAM_NN_VALID UIList *pList, S32 iRow);
void ui_ListDeselectRow(SA_PARAM_NN_VALID UIList *pList, S32 iRow);

void ui_ListSetSelectedObjects(SA_PARAM_NN_VALID UIList *pList, SA_PARAM_OP_VALID void ***peaObjs);
void ui_ListSetSelectedObjectsAndCallback(SA_PARAM_NN_VALID UIList *pList, SA_PARAM_OP_VALID void ***peaObjs);

void ui_ListClearSelected(SA_PARAM_NN_VALID UIList *pList);

// Whether or not columns are individually selectable.
void ui_ListSetColumnsSelectable(SA_PARAM_NN_VALID UIList *pList, bool bSelectable);

bool ui_ListIsSelected(SA_PARAM_NN_VALID const UIList *pList, SA_PARAM_OP_VALID const UIListColumn *pColumn, S32 iRow);
bool ui_ListIsHovering(SA_PARAM_NN_VALID const UIList *pList, SA_PARAM_OP_VALID const UIListColumn *pColumn, S32 iRow);

//////////////////////////////////////////////////////////////////////////
// Misc. support functions
F32 ui_ListGetTotalWidth(SA_PARAM_NN_VALID const UIList *pList);
F32 ui_ListGetTotalHeight(SA_PARAM_NN_VALID UIList *pList);
F32 ui_ListGetRowHeight(SA_PARAM_NN_VALID UIList *pList, S32 iRow);
void ui_ListSetRowsDraggable(SA_PARAM_NN_VALID UIList *pList, bool bDraggable);
void ui_ListSetSortOnlyOnUIChange(SA_PARAM_NN_VALID UIList *pList, bool bSortOnlyOnUIChange);

void ui_ListResetScrollbar(SA_PARAM_NN_VALID const UIList *pList);
void ui_ListGetScrollbarPosition(SA_PARAM_NN_VALID const UIList *pList, F32 *xOut, F32 *yOut);
void ui_ListSetScrollbar(SA_PARAM_NN_VALID const UIList *pList, F32 x, F32 y);
void ui_ListSortEx(SA_PARAM_NN_VALID UIList *pList, bool bRememberSelectedPointer);
#define ui_ListSort(pList) ui_ListSortEx(pList, true)
void ui_ListUnsort(SA_PARAM_NN_VALID UIList *pList);

F32 ui_ListDrawFromRow(SA_PARAM_NN_VALID UIList *pList, bool bLockedOnly, S32 iRow, F32 fTopY, F32 fStartX, F32 fEndX, F32 fStartY, F32 fEndY, F32 fScale, F32 fZ, F32 fLogicalOffsetY);
F32 ui_ListDrawRow(SA_PARAM_NN_VALID UIList *pList, bool bLockedOnly, S32 iRow, F32 fTopY, F32 fStartX, F32 fEndX, F32 fStartY, F32 fEndY, F32 fScale, F32 fZ, F32 fLogicalOffsetY);
void ui_ListDrawCell(SA_PARAM_NN_VALID UIList *pList, SA_PARAM_NN_VALID UIListColumn *pColumn, S32 iRow, UI_MY_ARGS, F32 z, CBox *pLogicalBox, bool bSelected);


//////////////////////////////////////////////////////////////////////////
// Sublist support. Lists can have sublists (and then start acting like
// a tree). Sublists are defined by a field within the ParseTable of the
// parent list, which must be an EArray (i.e. the sublist's model).
// Sublists do *NOT* work with row dragging.
void ui_ListAddSubList(SA_PARAM_NN_VALID UIList *pList, UIList *pSubList, F32 fIndent, const char *pchFieldName);

// Get the top-most parent list, useful for event handling.
SA_RET_NN_VALID UIList *ui_ListGetParent(SA_PARAM_NN_VALID UIList *pList);

// Call this to set this list's sublist's model based on the given row
// of the parent (pList's) model.
void ui_ListSetSubListModelFromRow(SA_PARAM_NN_VALID UIList *pList, SA_PARAM_NN_VALID UIList *pSubList, S32 iRow);

// Set all sublists from this row.
void ui_ListSetSubListsModelFromRow(SA_PARAM_NN_VALID UIList *pList, S32 iRow);

// Return the active row in this list's parent list (i.e., the row that
// the model for this list came from), or -1 if this list does not have
// a parent list.
S32 ui_ListGetActiveParentRow(SA_PARAM_NN_VALID const UIList *pList);

// Called when the list column header is left-clicked.
void ui_ListSetSubListModelCallback(SA_PARAM_NN_VALID UIList *pList, UIListModelChangeFunc cbSetModel, UserData pModelData);

void ui_ListGetSelectedCells(SA_PARAM_NN_VALID UIList *pList, SA_PARAM_NN_VALID const UIListSelectionObject ***peaObjects);

// Cross-model selection allows you to select items from two different
// sublists on the same level. It is safe, but not necessarily desirable.
void ui_ListSetCrossModelSelect(SA_PARAM_NN_VALID UIList *pList, bool bCrossModelSelect);

// Another way to specify sublist selections is with a selection path; this
// is an earray of integers that are the indexes into the model, e.g.
// {2, 3} is the 3rd row of the 2nd row of the top-level list.
bool ui_ListGetSelectedPath(SA_PARAM_NN_VALID UIList *pList, S32 **peaiPath);
void ui_ListSetSelectedPath(SA_PARAM_NN_VALID UIList *pList, S32 **peaiPath);
void ui_ListSetSelectedPathAndCallback(SA_PARAM_NN_VALID UIList *pList, S32 **peaiPath);

// Clear any selections present in parent lists or sublists.
void ui_ListClearParentChildSiblingSelections(SA_PARAM_NN_VALID UIList *pList);

// Inactive selections are those that are not in currently-selected model.
void ui_ListClearInactiveSelections(SA_PARAM_NN_VALID UIList *pList);

// Clear every selection, whether active or inactive.
void ui_ListClearEverySelection(SA_PARAM_NN_VALID UIList *pList);

void ui_ListDestroyColumns(SA_PARAM_NN_VALID UIList *pList);

// Horrible hack for dumping memory budgets to a file.
void ui_ListDoCrazyCSVDumpThing(SA_PARAM_NN_VALID UIList *pList);

UIStyleFont* ui_ListItemGetFontFromSkinAndWidget( UISkin* skin, UIWidget* pWidget, bool bSelected, bool bHover );
UIStyleFont* ui_ListHeaderGetFontFromSkinAndWidget( UISkin* skin, UIWidget* pWidget );
void ui_ListOutsideFillDrawingDescriptionFromSkin( UISkin* skin, UIDrawingDescription* desc, bool bIsComboBoxDropdown );

#endif
