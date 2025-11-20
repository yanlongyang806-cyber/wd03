/***************************************************************************



***************************************************************************/

#ifndef UI_COMBOBOX_H
#define UI_COMBOBOX_H
GCC_SYSTEM

#include "UICore.h"

typedef struct UIComboBox UIComboBox;
typedef struct UIList UIList;
typedef struct UIFilteredList UIFilteredList;

//////////////////////////////////////////////////////////////////////////
// A button with a drop-down menu, and a callback when a new item is
// selected. A combo box can be embedded within a text entry; see
// UITextEntry.h for more information.

// Widget, drawData, row number, x, y, z, w, h, scale, inBox. If inBox is true, this
// is drawing inside the actual combo box (and so might need to look
// different); otherwise it's in a drop-down list.
typedef void (*UIComboBoxDrawFunc)(UIComboBox *cb, UserData drawData, S32 row, F32 x, F32 y, F32 z, F32 w, F32 h, F32 scale, bool inBox);

// For cbText, called to fill in the output EString.
typedef void (*UIComboBoxTextFunc)(UIComboBox *pCombo, S32 iRow, bool bInBox, UserData userData, char **ppchOutput);

typedef void (*UIComboBoxRowFunc)(UIComboBox *cb, S32 row, UserData selectedData);

typedef void (*UIComboBoxPopupShowFunc)(UIComboBox *cb, bool bShow, bool bFocus);
typedef void (*UIComboBoxPopupUpdateFunc)(UIComboBox *cb);
typedef void (*UIComboBoxPopupTickFunc)(UIComboBox *cb, UI_PARENT_ARGS);
typedef void (*UIComboBoxPopupFreeFunc)(UIComboBox *cb);
typedef void (*UIComboBoxHoverFunc)(UIComboBox *cb, S32 iRow, UserData pHoverData);

#define UI_COMBO_BOX(w) (&((w)->combo))
#define UI_COMBO_BOX_TYPE UIComboBox combo;

typedef struct UIComboBox
{
	UI_INHERIT_FROM(UI_WIDGET_TYPE);

	// A combo box model is an EArray of whatever kind of structure your
	// draw and selected callbacks know how to handle. The default draw
	// function can use either a ParseTable (and use the column name as
	// the drawData), or an EArray of strings.
	ParseTable *table;
	UIModel model;

	// widget.width is the width of the combo box itself. openedWidth is the
	// width of the drop-down selection. By default, this is the same as the
	// width of the combo box, but you might want a small combo box with
	// a larger drop-down.
	F32 openedWidth;

	// The height of each row in the drop-down.
	F32 rowHeight;

	// The currently selected row in the model.
	S32 iSelected;

	bool bMultiSelect;
	S32 *eaiSelected;

	// Callback to draw data in the drop-down or the box itself.
	UIComboBoxDrawFunc drawF;
	UserData drawData;

	// Callback when the currently-selected row changes.
	UIActivationFunc selectedF;
	UserData selectedData;

	// Callback when a row is clicked. This is called before selectedF.
	UIComboBoxRowFunc cbItemActivated;
	UserData pItemActivatedData;

	// Callback to get text based on the row
	UIComboBoxTextFunc cbText;
	UserData pTextData;

	// Callbacks to manage the popup
	UIComboBoxPopupShowFunc cbPopupShow;
	UIComboBoxPopupUpdateFunc cbPopupUpdate;
	UIComboBoxPopupTickFunc cbPopupTick;
	UIComboBoxPopupFreeFunc cbPopupFree;

	UIFilteredList* pPopupFilteredList;
	UIList* pPopupList;

	// Callbacks to manage the filter
	UIFilterListFunc cbFilterFunc;
	UserData pFilterData;

	// Callback to manage hover
	UIComboBoxHoverFunc cbHover;
	UserData pHoverData;

	// Default display string to show when nothing is selected
	char *defaultDisplay;
//	bool showDefault : 1;

	// If true, this combo box does not clamp selections to the model
	// size. This is used for flag-based combo boxes.
	bool allowOutOfBoundsSelected : 1;

	bool bUseMessage : 1; // True if should treat text parser column as a message reference
	bool bStringAsMessageKey : 1; // If true, the string output as for this column should be a message ref
	bool opened : 1;
	bool scrollToSelectedOnNextTick : 1;
	bool closeOnNextTick : 1;
	bool drawSelected : 1; // If true, the current selection is highlighted
	bool bIsFlagCombo : 1; // True if created as a flag combo
	bool bShowCheckboxes : 1; // If true, checkboxes are shown on entries in the pulldown
	bool bDontSortList : 1; // If true, filtered combo should not sort data
} UIComboBox;

// Create a combo box with the given parse table, model, and draw text from
// the given column. If table is NULL, model is assumed to be an EArray of
// string pointers.
SA_RET_NN_VALID UIComboBox *ui_ComboBoxCreate(F32 x, F32 y, F32 w, SA_PARAM_OP_VALID ParseTable *table, SA_PARAM_OP_VALID cUIModel model, SA_PARAM_OP_STR const char *field);
void ui_ComboBoxInitialize(SA_PRE_NN_FREE SA_POST_NN_VALID UIComboBox *cb, F32 x, F32 y, F32 w, SA_PARAM_OP_VALID ParseTable *table, SA_PARAM_OP_VALID cUIModel model, SA_PARAM_OP_STR const char *field);
void ui_ComboBoxFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UIComboBox *cb);

// Convenience function to create a combo box from a dictionary
SA_RET_NN_VALID UIComboBox *ui_ComboBoxCreateWithDictionary(F32 x, F32 y, F32 w, SA_PARAM_NN_VALID const void *dictHandleOrName, SA_PARAM_NN_VALID ParseTable *table, SA_PARAM_OP_STR const char *field);

// Convenience function to create a combo box from a global object dictionary
SA_RET_NN_VALID UIComboBox *ui_ComboBoxCreateWithGlobalDictionary(F32 x, F32 y, F32 w, SA_PARAM_NN_VALID const char *globalDictName, SA_PARAM_OP_STR const char *field);

void ui_ComboBoxSetMultiSelect(SA_PARAM_NN_VALID UIComboBox *cb, bool bMultiSelect);

void ui_ComboBoxSetSelectedCallback(SA_PARAM_NN_VALID UIComboBox *cb, UIActivationFunc selectedF, UserData selectedData);
void ui_ComboBoxSetDrawFunc(SA_PARAM_NN_VALID UIComboBox *cb, UIComboBoxDrawFunc func, UserData drawData);
UserData ui_ComboBoxGetSelectedData(UIComboBox *cb);

void ui_ComboBoxSetTextCallback(SA_PARAM_NN_VALID UIComboBox *pCombo, UIComboBoxTextFunc cbText, UserData pTextData);

// The default cbItemActivated callback. It sets the selected index, or
// toggles it if multiselection is enabled.
void ui_ComboBoxDefaultRowCallback(SA_PARAM_NN_VALID UIComboBox *cb, S32 selected, UserData dummy);

// Called when the mouse hovers over an item in the list. This is called every frame.
void ui_ComboBoxSetHoverCallback(SA_PARAM_NN_VALID UIComboBox *cb, UIComboBoxHoverFunc cbHover, UserData pHoverData);

// Display this string by default when the selected row is -1. If you set a
// default display string and a custom draw callback, your callback may
// get called to draw a row of -1! If this isn't what you want, make sure
// you don't set a default display string.
//void ui_ComboBoxSetDefaultDisplayString(SA_PARAM_NN_VALID UIComboBox *cb, SA_PARAM_OP_STR const char* dispStr, bool display);
// UPDATE: The ability to display this string as a row in the combo box is no longer present
void ui_ComboBoxSetDefaultDisplayString(SA_PARAM_NN_VALID UIComboBox *cb, SA_PARAM_OP_STR const char* dispStr);

S32 ui_ComboBoxGetSelected(SA_PARAM_NN_VALID UIComboBox *list);
SA_RET_OP_VALID void *ui_ComboBoxGetSelectedObject(SA_PARAM_NN_VALID UIComboBox *list);

SA_RET_NN_VALID const S32 * ui_ComboBoxGetSelecteds(SA_PARAM_NN_VALID UIComboBox *list);

void ui_ComboBoxRowToString(SA_PARAM_NN_VALID UIComboBox *cb, S32 iRow, char **ppchOut);
void ui_ComboBoxGetSelectedAsString(SA_PARAM_NN_VALID UIComboBox *cb, char **ppchOut);
void ui_ComboBoxGetSelectedsAsString(SA_PARAM_NN_VALID UIComboBox *cb, char **ppchOut);
void ui_ComboBoxGetSelectedsAsStringEx(SA_PARAM_NN_VALID UIComboBox *cb, char **ppchOut, const char* pchIndexSeparator);

void ui_ComboBoxSetSelectedsAsString(SA_PARAM_NN_VALID UIComboBox *cb, const char *pchValue);
void ui_ComboBoxSetSelectedsAsStringAndCallback(SA_PARAM_NN_VALID UIComboBox *cb, const char *pchValue);

void ui_ComboBoxSetSelected(SA_PARAM_NN_VALID UIComboBox *cb, S32 i);
void ui_ComboBoxSetSelecteds(SA_PARAM_NN_VALID UIComboBox *cb, SA_PRE_NN_OP_VALID const S32 * const *i);

void ui_ComboBoxSetSelectedAndCallback(SA_PARAM_NN_VALID UIComboBox *cb, S32 i);
void ui_ComboBoxSetSelectedsAndCallback(SA_PARAM_NN_VALID UIComboBox *cb, SA_PRE_NN_OP_VALID const S32 * const *i);

void ui_ComboBoxSetSelectedObject(SA_PARAM_NN_VALID UIComboBox *cb, const void *obj);
void ui_ComboBoxSetSelectedObjectAndCallback(SA_PARAM_NN_VALID UIComboBox *cb, const void *obj);

void ui_ComboBoxToggleSelection(SA_PARAM_NN_VALID UIComboBox *cb, S32 iRow);
void ui_ComboBoxToggleSelectionAndCallback(SA_PARAM_NN_VALID UIComboBox *cb, S32 iRow);

// Change the model. If the previously selected object exists in the new model,
// it will be selected now; otherwise the selection is lost.
void ui_ComboBoxSetModel(SA_PARAM_NN_VALID UIComboBox *cb, SA_PARAM_OP_VALID ParseTable *table, SA_PARAM_NN_VALID cUIModel model);

void ui_ComboBoxSetModelNoCallback(SA_PARAM_NN_VALID UIComboBox *cb, SA_PARAM_OP_VALID ParseTable *table, SA_PARAM_NN_VALID cUIModel model);

// Set the openedWidth value to the width of the longest string in the
// combo box (this only works if the combo box uses strings). If the combo
// box can't figure out the width of its items, it sets openedWidth to -1.
void ui_ComboBoxCalculateOpenedWidth(SA_PARAM_NN_VALID UIComboBox *cb);

typedef void (*UIComboBoxEnumFunc)(UIComboBox *, int, UserData);
typedef struct ComboBoxEnumProxy
{
	UserData pEnumSelectedData;
	UIComboBoxEnumFunc cbEnumSelected;
	int *eaiValues;
	const char **eachNames;
} ComboBoxEnumProxy;

SA_RET_NN_VALID UIComboBox *ui_ComboBoxCreateWithEnum(F32 x, F32 y, F32 w, struct StaticDefineInt *enumTable, UIComboBoxEnumFunc selectedF, UserData selectedData);

void ui_ComboBoxSetEnum(SA_PARAM_NN_VALID UIComboBox *cb, struct StaticDefineInt *enumTable, UIComboBoxEnumFunc selectedF, UserData selectedData);
void ui_ComboBoxSetSelectedEnumCallback(UIComboBox *cb, UIComboBoxEnumFunc selectedF, UserData selectedData);
void ui_ComboBoxRemoveEnumDuplicates(SA_PARAM_NN_VALID UIComboBox *cb);
void ui_ComboBoxSortEnum(SA_PARAM_NN_VALID UIComboBox *cb);
void ui_ComboBoxEnumInsertValue(SA_PARAM_NN_VALID UIComboBox *cb, char *pchLabel, S32 iValue);	// Add the value to the correct place in enum value order
void ui_ComboBoxEnumAddValue(SA_PARAM_NN_VALID UIComboBox *cb, const char *pchLabel, S32 iValue);
void ui_ComboBoxEnumRemoveValueInt(SA_PARAM_NN_VALID UIComboBox *cb, S32 iEnumValue);
void ui_ComboBoxEnumRemoveValueString(SA_PARAM_NN_VALID UIComboBox *cb, const char *pchValueName);
void ui_ComboBoxEnumRemoveAllValues(SA_PARAM_NN_VALID UIComboBox *cb);

S32 ui_ComboBoxGetSelectedEnum(SA_PARAM_NN_VALID UIComboBox *cb);
void ui_ComboBoxGetSelectedEnums(SA_PARAM_NN_VALID UIComboBox *cb, S32 **peaiValues);
int ui_ComboBoxEnumRowToVal(UIComboBox *cb, int row);

void ui_ComboBoxSetSelectedEnum(SA_PARAM_NN_VALID UIComboBox *cb, S32 iValue);
void ui_ComboBoxSetSelectedEnums(SA_PARAM_NN_VALID UIComboBox *cb, SA_PRE_NN_NN_VALID S32 **peaiValues);

void ui_ComboBoxSetSelectedEnumAndCallback(SA_PARAM_NN_VALID UIComboBox *cb, S32 iValue);
void ui_ComboBoxSetSelectedEnumsAndCallback(SA_PARAM_NN_VALID UIComboBox *cb, SA_PRE_NN_NN_VALID S32 **peaiValues);

void ui_ComboBoxSetMultiSelect(UIComboBox *cb, bool bMultiSelect);

// Default implementation for drawF; simply draws the text of a ParseTable,
// or if table is NULL, the model's row interpreted as char *.
void ui_ComboBoxDefaultDraw(SA_PARAM_NN_VALID UIComboBox *cb, UserData dummy, S32 row, F32 x, F32 y, F32 z, F32 w, F32 h, F32 scale, bool inBox);

// Default implementation of textF. It assumes that the user data is the
// field name, and if it's NULL, that the model is of strings.
void ui_ComboBoxDefaultText(SA_PARAM_NN_VALID UIComboBox *pCombo, S32 iRow, bool bInBox, SA_PARAM_OP_STR const char *pchField, SA_PRE_NN_NN_STR char **ppchOutput);

void ui_ComboBoxSetPopupOpenEx(SA_PARAM_NN_VALID UIComboBox *cb, bool opened, bool focus);
#define ui_ComboBoxSetPopupOpen(cb, opened) ui_ComboBoxSetPopupOpenEx(cb, opened, true);

void ui_ComboBoxDefaultPopupShow(SA_PARAM_NN_VALID UIComboBox *cb, bool bShow, bool bFocus);
void ui_ComboBoxDefaultPopupUpdate(SA_PARAM_NN_VALID UIComboBox *cb);
void ui_ComboBoxDefaultPopupTick(SA_PARAM_NN_VALID UIComboBox *cb, UI_PARENT_ARGS);
void ui_ComboBoxDefaultPopupFree(SA_PARAM_NN_VALID UIComboBox *cb);

bool ui_ComboBoxInput(SA_PARAM_NN_VALID UIComboBox *cb, SA_PARAM_NN_VALID KeyInput *input);
void ui_ComboBoxTick(SA_PARAM_NN_VALID UIComboBox *cb, UI_PARENT_ARGS);
void ui_ComboBoxDraw(SA_PARAM_NN_VALID UIComboBox *cb, UI_PARENT_ARGS);
void ui_ComboBoxScrollToSelectedInTick(SA_PARAM_NN_VALID UIComboBox *cb, UI_PARENT_ARGS);

// FlagComboBox
typedef UIComboBox UIFlagComboBox;
UIFlagComboBox *ui_FlagComboBoxCreate(StaticDefineInt *pDefines);
void ui_FlagComboBoxFree(UIFlagComboBox *cb);
void ui_FlagComboBoxRowCallback(SA_PARAM_NN_VALID UIFlagComboBox *cb, S32 selected, ComboBoxEnumProxy *proxy);
void ui_FlagComboBoxDraw(SA_PARAM_NN_VALID UIFlagComboBox *cb, ComboBoxEnumProxy *proxy, S32 row, F32 x, F32 y, F32 z, F32 w, F32 h, F32 scale, bool inBox);
void ui_FlagComboBoxRemoveDuplicates(SA_PARAM_NN_VALID UIComboBox *cb);
void ui_FlagComboBoxSort(SA_PARAM_NN_VALID UIComboBox *cb);

// FilteredComboBox
typedef UIComboBox UIFilteredComboBox;
SA_RET_NN_VALID UIFilteredComboBox *ui_FilteredComboBoxCreate(F32 x, F32 y, F32 w, SA_PARAM_OP_VALID ParseTable *table, SA_PARAM_OP_VALID cUIModel model, SA_PARAM_OP_STR const char *field);
SA_RET_NN_VALID UIFilteredComboBox *ui_FilteredComboBoxCreateWithDictionary(F32 x, F32 y, F32 w, SA_PARAM_NN_VALID const void *dictHandleOrName, SA_PARAM_NN_VALID ParseTable *table, SA_PARAM_OP_STR const char *field);
SA_RET_NN_VALID UIFilteredComboBox *ui_FilteredComboBoxCreateWithGlobalDictionary(F32 x, F32 y, F32 w, SA_PARAM_NN_VALID const char *dictName, SA_PARAM_OP_STR const char *field);
SA_RET_NN_VALID UIFilteredComboBox *ui_FilteredComboBoxCreateWithEnum(F32 x, F32 y, F32 w, StaticDefineInt *enumTable, UIComboBoxEnumFunc selectedF, UserData selectedData);

void ui_FilteredComboBoxPopupShow(SA_PARAM_NN_VALID UIComboBox *cb, bool bShow, bool bFocus);
void ui_FilteredComboBoxPopupUpdate(SA_PARAM_NN_VALID UIComboBox *cb);
void ui_FilteredComboBoxPopupTick(SA_PARAM_NN_VALID UIComboBox *cb, UI_PARENT_ARGS);
void ui_FilteredComboBoxPopupFree(SA_PARAM_NN_VALID UIComboBox *cb);

void ui_FilteredComboBoxSetFilterCallback(SA_PARAM_NN_VALID UIComboBox *cb, UIFilterListFunc cbFilterFunc, UserData pUserData);

// Exposed for customization functions
UIStyleFont* ui_ComboBoxGetFont( UIComboBox* cb );

#endif
