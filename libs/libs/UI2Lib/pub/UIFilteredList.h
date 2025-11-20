#ifndef UI_FILTERED_LIST_H
#define UI_FILTERED_LIST_H
GCC_SYSTEM

#include "UICore.h"
#include "UIList.h"
#include "UIPane.h"

typedef struct UITextEntry UITextEntry;
typedef struct UILabel UILabel;
typedef struct UIFilteredList UIFilteredList;

// Called each frame the mouse is hovering over the list.
typedef void (*UIFilteredRowHoverFunc)(UIFilteredList *pFList, UIListColumn *pColumn, S32 iRow, UserData pHoverData);

typedef struct UIFilteredList
{
	UIWidget widget;

	ParseTable *pTable;
	UIModel peaModel;
	int iFieldIndex;

	void **eaFilteredModel;
	void **eaSelectedInternal; // temp storage for current selection get
	int *eaiSelectedInternal;  // temp storage for current selection get

	bool bUseBackgroundColor : 1;
	bool bDrawBorder : 1;
	bool bDontAutoSort : 1;
	bool bDontAutoScroll : 1;

	// Called when the selection changes.
	UIActivationFunc cbSelected;
	UserData pSelectedData;

	// Called when the user double-left-clicks.
	UIActivationFunc cbActivated;
	UserData pActivatedData;

	// Called for user defined filters
	UIFilterListFunc cbFilter;
	UserData pFilterData;

	// Called for hovering
	UIFilteredRowHoverFunc cbHover;
	UserData pHoverData;

	UIPane *pPane;
	UITextEntry *pEntry;
	UIList *pList;
	UILabel *pLabel;
} UIFilteredList;

SA_RET_NN_VALID UIFilteredList *ui_FilteredListCreate(SA_PARAM_OP_VALID const char *pchColumnLabel, UIListColumnType eType, SA_PARAM_OP_VALID ParseTable *pTable, SA_PARAM_OP_VALID UIModel peaModel, intptr_t contents, F32 fRowHeight);
SA_RET_NN_VALID static __forceinline UIFilteredList *ui_FilteredListCreateParseName(SA_PARAM_OP_VALID const char *pchColumnLabel, SA_PARAM_OP_VALID ParseTable *pTable, SA_PARAM_OP_VALID UIModel peaModel, char *pchField, F32 fRowHeight) { return ui_FilteredListCreate(pchColumnLabel, UIListPTName, pTable, peaModel, (intptr_t)pchField, fRowHeight); }
SA_RET_NN_VALID static __forceinline UIFilteredList *ui_FilteredListCreateParseIndex(SA_PARAM_OP_VALID const char *pchColumnLabel, SA_PARAM_OP_VALID ParseTable *pTable, SA_PARAM_OP_VALID UIModel peaModel, int iFieldIndex, F32 fRowHeight) { return ui_FilteredListCreate(pchColumnLabel, UIListPTIndex, pTable, peaModel, (intptr_t)iFieldIndex, fRowHeight); }
SA_RET_NN_VALID static __forceinline UIFilteredList *ui_FilteredListCreateText(SA_PARAM_OP_VALID const char *pchColumnLabel, SA_PARAM_OP_VALID UIModel peaModel, F32 fRowHeight) { return ui_FilteredListCreate(pchColumnLabel, UIListTextCallback, NULL, peaModel, (intptr_t)NULL, fRowHeight); }

void ui_FilteredListFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UIFilteredList *pFList);
void ui_FilteredListTick(SA_PARAM_NN_VALID UIFilteredList *pFList, UI_PARENT_ARGS);
void ui_FilteredListDraw(SA_PARAM_NN_VALID UIFilteredList *pFList, UI_PARENT_ARGS);

static __forceinline void ui_FilteredListSetMultiselect(SA_PARAM_NN_VALID UIFilteredList *pFList, bool bMultiSelect) { ui_ListSetMultiselect(pFList->pList, bMultiSelect); }

const char *ui_FilteredListGetFilter(SA_PARAM_NN_VALID UIFilteredList *pFList);
void ui_FilteredListSetFilter(SA_PARAM_NN_VALID UIFilteredList *pFList, const char *pchFilter);
void ui_FilteredListRefresh(SA_PARAM_NN_VALID UIFilteredList *pFList);

UIList *ui_FilteredListGetList(SA_PARAM_NN_VALID UIFilteredList *pFList);

int ui_FilteredListGetSelectedRow(SA_PARAM_NN_VALID UIFilteredList *pFList);
int** ui_FilteredListGetSelectedRows(SA_PARAM_NN_VALID UIFilteredList *pFList);

void ui_FilteredListSetSelectedRow(SA_PARAM_NN_VALID UIFilteredList *pFList, S32 iRow);
void ui_FilteredListSetSelectedRowAndCallback(SA_PARAM_NN_VALID UIFilteredList *pFList, S32 iRow);
void ui_FilteredListSetSelectedRows(SA_PARAM_NN_VALID UIFilteredList *pFList, SA_PRE_NN_NN_VALID S32 **peaiRows);
void ui_FilteredListSetSelectedRowsAndCallback(SA_PARAM_NN_VALID UIFilteredList *pFList, SA_PRE_NN_NN_VALID S32 **peaiRows);

// Get selections.  Returns pointer to earray for getting multiple selections
char *ui_FilteredListGetSelectedString(SA_PARAM_NN_VALID UIFilteredList *pFList);
char ***ui_FilteredListGetSelectedStrings(SA_PARAM_NN_VALID UIFilteredList *pFList);
void *ui_FilteredListGetSelectedObject(SA_PARAM_NN_VALID UIFilteredList *pFList);
void ***ui_FilteredListGetSelectedObjects(SA_PARAM_NN_VALID UIFilteredList *pFList);

void ui_FilteredListSetModel(SA_PARAM_NN_VALID UIFilteredList *pFList, SA_PARAM_OP_VALID ParseTable *pTable, SA_PARAM_OP_VALID UIModel peaModel);
SA_RET_OP_VALID UIModel ui_FilteredListGetModel(SA_PARAM_NN_VALID const UIFilteredList *pFList);

void ui_FilteredListScrollToRow(SA_PARAM_NN_VALID UIFilteredList *pFList, S32 iRow);

void ui_FilteredListScrollToPath(SA_PARAM_NN_VALID UIFilteredList *pFList, F32 x, F32 y, F32 w, F32 h, F32 scale);

void ui_FilteredListSetSelectedCallback(SA_PARAM_NN_VALID UIFilteredList *pFList, UIActivationFunc cbSelected, UserData pSelectedData);
void ui_FilteredListSetActivatedCallback(SA_PARAM_NN_VALID UIFilteredList *pFList, UIActivationFunc cbActivated, UserData pActivatedData);
void ui_FilteredListSetFilterCallback(SA_PARAM_NN_VALID UIFilteredList *pFList, UIFilterListFunc cbFilter, UserData pUserData);
void ui_FilteredListSetHoverCallback(SA_PARAM_NN_VALID UIFilteredList *pFList, UIFilteredRowHoverFunc cbHover, UserData pHoverData);


#endif