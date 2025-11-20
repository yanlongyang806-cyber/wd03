#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/

#ifndef UI_TEXTENTRY_H
#define UI_TEXTENTRY_H

#include "UICore.h"
#include "UIEditable.h"

typedef struct UIComboBox UIComboBox;
typedef struct UIPane UIPane;
typedef struct UISMFView UISMFView;
typedef struct UITextArea UITextArea;

//////////////////////////////////////////////////////////////////////////
// A single-line text entry, maybe with a combo box attached.

typedef struct UITextEntry
{
	UI_INHERIT_FROM(UI_WIDGET_TYPE UI_EDITABLE_TYPE);
	
	// Called when enter is pressed. By default, this calls finishedF
	// (by making the text entry lose focus.)
	UIActivationFunc enterF;
	UserData enterData;

	// Called when the text entry widget loses focus, unless it is
	// losing focus because it is being freed.
	UIActivationFunc finishedF;
	UserData finishedData;

	UIActivationFunc upKeyF;
	UserData upKeyData;
	UIActivationFunc downKeyF;
	UserData downKeyData;

	// An attached combo box for options.`
	struct UIComboBox *cb;

	// An attached text area for multiline
	struct UITextArea *area;
	bool areaOpened;
	bool areaCloseOnNextTick;
	UIActivationFunc areaClosedF;
	UserData areaClosedData;

	// An attached SMFView for previews
	UISMFView *smf;
	bool smfOpened;
	bool bHasInputFocus;

	F32 lastDrawnOffset;
	int lastCursorPosition;
	REF_TO(UIStyleFont) font;

	
	const char* pchIndexSeparator;	 // If set, then this string separates indexes instead of space; only valid if field is an EArray

	// If this is true, the selection is the autocomplete text.
	bool selectionIsAutoComplete : 1;

	bool autoComplete : 1;        // Causes name completion.  Requires a combo box to be attached.
	bool comboFinishOnSelect : 1; // Causes selecting from combo to fire enter event instead of remain editing
	bool comboValidate : 1;
	unsigned trimWhitespace : 1;
	bool forceLeftAligned : 1;	  // When not focused it will ignore where the cursor is and left align

	bool magnify : 1;
	U32 readOnly : 1;
	F32 magnifyScale;
	U32 magnifyWidth;

	bool selectOnFocus : 1;
} UITextEntry;

SA_RET_NN_VALID UITextEntry *ui_TextEntryCreateEx(SA_PARAM_OP_STR const unsigned char *text, int x, int y MEM_DBG_PARMS);
#define ui_TextEntryCreate(text, x, y) ui_TextEntryCreateEx(text, x, y MEM_DBG_PARMS_INIT)
void ui_TextEntryInitializeEx(SA_PRE_NN_FREE SA_POST_NN_VALID UITextEntry *entry, SA_PARAM_OP_STR const unsigned char *text, int x, int y MEM_DBG_PARMS);
#define ui_TextEntryInitialize(entry, text, x, y) ui_TextEntryInitializeEx(entry, text, x, y MEM_DBG_PARMS_INIT)
void ui_TextEntryFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UITextEntry *entry);

// This pointer is only good as long as the text entry's text is not modified.
SA_RET_NN_STR const unsigned char *ui_TextEntryGetText(SA_PARAM_NN_VALID UITextEntry *entry);

bool ui_TextEntrySetText(SA_PARAM_NN_VALID UITextEntry *entry, SA_PARAM_NN_STR const unsigned char *text);
bool ui_TextEntrySetTextAndCallback(SA_PARAM_NN_VALID UITextEntry *entry, SA_PARAM_NN_STR const unsigned char *text);
bool ui_TextEntrySetTextIfChanged(SA_PARAM_NN_VALID UITextEntry *entry, SA_PARAM_NN_STR const unsigned char *text);

U32 ui_TextEntryGetCursorPosition(SA_PARAM_NN_VALID UITextEntry *entry);
void ui_TextEntrySetCursorPosition(SA_PARAM_NN_VALID UITextEntry *entry, U32 cursorPos);

// Returns how many characters were actually inserted.
U32 ui_TextEntryInsertTextAt(SA_PARAM_NN_VALID UITextEntry *entry, U32 offset, SA_PARAM_NN_STR const unsigned char *text);
bool ui_TextEntryDeleteTextAt(SA_PARAM_NN_VALID UITextEntry *entry, U32 offset, U32 length);
bool ui_TextEntryDeleteSelection(SA_PARAM_NN_VALID UITextEntry *entry);

void ui_TextEntryCut(SA_PARAM_NN_VALID UITextEntry *entry);
void ui_TextEntryCopy(SA_PARAM_NN_VALID UITextEntry *entry);
void ui_TextEntryPaste(SA_PARAM_NN_VALID UITextEntry *entry);

void ui_TextEntrySetChangedCallback(SA_PARAM_NN_VALID UITextEntry *entry, UIActivationFunc changedF, UserData changedData);
void ui_TextEntrySetFinishedCallback(SA_PARAM_NN_VALID UITextEntry *entry, UIActivationFunc finishedF, UserData finishedData);
void ui_TextEntrySetEnterCallback(SA_PARAM_NN_VALID UITextEntry *entry, UIActivationFunc enterF, UserData enterData);
void ui_TextEntrySetValidateCallback(SA_PARAM_NN_VALID UITextEntry *entry, UITextValidateFunc validateF, UserData validateData);
void ui_TextEntrySetUpKeyCallback(SA_PARAM_NN_VALID UITextEntry *entry, UIActivationFunc upKeyF, UserData upKeyData);
void ui_TextEntrySetDownKeyCallback(SA_PARAM_NN_VALID UITextEntry *entry, UIActivationFunc downKeyF, UserData downKeyData);

void ui_TextEntryDoAutocomplete(SA_PARAM_NN_VALID UITextEntry *entry);

// This does nothing but make the text entry lose focus, which in turn will call
// finishedF.
void ui_TextEntryDefaultEnterCallback(SA_PARAM_NN_VALID UITextEntry *entry, UserData ignored);

void ui_TextEntrySetSelectOnFocus(SA_PARAM_NN_VALID UITextEntry *entry, bool select);

void ui_TextEntrySetWidthInCharacters(SA_PARAM_NN_VALID UITextEntry *entry, S32 iChars);

#define ui_TextEntrySetIgnoreSpace(entry, ignore) ui_TextEntrySetValidateCallback((entry), (ignore) ? ui_TextEntryValidationNoSpaces : NULL, NULL)
bool ui_TextEntryValidationNoSpaces(UITextEntry *entry, unsigned char **oldString, unsigned char **newString, UserData dummy);

#define ui_TextEntrySetIntegerOnly(entry) ui_TextEntrySetValidateCallback((entry), ui_TextEntryValidationIntegerOnly, NULL)
bool ui_TextEntryValidationIntegerOnly(UITextEntry *entry, unsigned char **oldString, unsigned char **newString, UserData dummy);

#define ui_TextEntrySetFloatOnly(entry) ui_TextEntrySetValidateCallback((entry), ui_TextEntryValidationFloatOnly, NULL)
bool ui_TextEntryValidationFloatOnly(UITextEntry *entry, unsigned char **oldString, unsigned char **newString, UserData dummy);

#define ui_TextEntrySetSimpleOnly(entry) ui_TextEntrySetValidateCallback((entry), ui_TextEntryValidationSimpleOnly, NULL)
bool ui_TextEntryValidationSimpleOnly(UITextEntry *entry, unsigned char **oldString, unsigned char **newString, UserData dummy);

// Provide a drop-down list of options for this text entry, using this combo
// box and model. The model must be an EArray of char *s, not a ParseTable.
// If you change the size/position of the entry, you should call this
// function again or the ComboBox will be in the wrong place. This combo box
// MUST NOT be added to other widgets; it becomes a child of the text entry.
void ui_TextEntrySetComboBox(SA_PARAM_NN_VALID UITextEntry *entry, SA_PARAM_OP_VALID struct UIComboBox *cb);

// Provide a text area for this text entry that works like a combo box.
// This text area MUST NOT be added to other widgets; it becomes a child of the text entry.
void ui_TextEntrySetTextArea(SA_PARAM_NN_VALID UITextEntry *entry, SA_PARAM_OP_VALID struct UITextArea *area);

// Provide a SMFView for this text entry that works like a combo box.
// This SMFView MUST NOT be added to other widgets; it becomes a child of the text entry.
void ui_TextEntrySetSMFView(SA_PARAM_NN_VALID UITextEntry *entry, SA_PARAM_OP_VALID UISMFView *smf);

void ui_TextEntrySetPopupAreaOpen(SA_PARAM_NN_VALID UITextEntry *entry, bool opened);
void ui_TextEntrySetPopupSMFOpen(SA_PARAM_NN_VALID UITextEntry *entry, bool opened);

void ui_TextEntryCursorToMouse(SA_PARAM_NN_VALID UITextEntry *entry, F32 x, F32 scale);
U32 ui_TextEntryPixelToCursor(SA_PARAM_NN_VALID UITextEntry *entry, F32 x, F32 scale);

bool ui_TextEntryInput(SA_PARAM_NN_VALID UITextEntry *entry, SA_PARAM_NN_VALID KeyInput *input);
void ui_TextEntryTick(SA_PARAM_NN_VALID UITextEntry *entry, UI_PARENT_ARGS);
void ui_TextEntryDraw(SA_PARAM_NN_VALID UITextEntry *entry, UI_PARENT_ARGS);
void ui_TextEntryFocus(SA_PARAM_NN_VALID UITextEntry *entry, UIAnyWidget *focusitem);
void ui_TextEntryUnfocus(SA_PARAM_NN_VALID UITextEntry *entry, UIAnyWidget *focusitem);
void ui_TextEntrySetAsPasswordEntry(SA_PARAM_NN_VALID UITextEntry *entry, bool isPassword);
void ui_TextEntryComboValidate(UITextEntry *entry);

// These are convenience functions to creates a text entry with combo box
// - Auto complete allows text typing to auto-complete on the best matching selection.
// - Draw selected causes the current selection to be highlighted (non standard but useful in long lists)

// - model should be the address of an eArray of strings
SA_RET_NN_VALID UITextEntry *ui_TextEntryCreateWithStringCombo(SA_PARAM_NN_STR const unsigned char *text, int x, int y, 
														   SA_PARAM_NN_VALID cUIModel model,
														   bool bAutoComplete, bool bDrawSelected, bool bValidate, bool bFiltered);

// - makes a combo box with multi select, based on string model
SA_RET_NN_VALID UITextEntry *ui_TextEntryCreateWithStringMultiCombo(SA_PARAM_NN_STR const unsigned char *text, int x, int y, 
														   SA_PARAM_NN_VALID cUIModel model,
														   bool bAutoComplete, bool bDrawSelected, bool bValidate, bool bFiltered);

// - Model should be the address of an eArray of objects
// - PTable and field specify the parse table and field for the string to display and choose
SA_RET_NN_VALID UITextEntry *ui_TextEntryCreateWithObjectCombo(SA_PARAM_NN_STR const unsigned char *text, int x, int y, 
														   SA_PARAM_NN_VALID cUIModel model,
														   SA_PARAM_NN_VALID ParseTable *pTable, SA_PARAM_NN_STR const unsigned char *field,
														   bool bAutoComplete, bool bDrawSelected, bool bValidate, bool bFiltered);

// - DictionaryNameOrHandle should be the dictionary to use
// - PTable and field specify the parse table and field for the string to display and choose
SA_RET_NN_VALID UITextEntry *ui_TextEntryCreateWithDictionaryCombo(SA_PARAM_NN_STR const unsigned char *text, int x, int y, 
														   SA_PARAM_NN_VALID const void *dictHandleOrName,
														   SA_PARAM_NN_VALID ParseTable *pTable, SA_PARAM_NN_STR const unsigned char *field,
														   bool bAutoComplete, bool bDrawSelected, bool bValidate, bool bFiltered);


// - DictionaryName should be the global dictionary to use
// - field specifies the field within the ResourceDictionaryInfo struct for the string to display and choose
SA_RET_NN_VALID UITextEntry *ui_TextEntryCreateWithGlobalDictionaryCombo(SA_PARAM_NN_STR const unsigned char *text, int x, int y, 
															   SA_PARAM_NN_VALID const char *dictName, SA_PARAM_NN_STR const unsigned char *field,
															   bool bAutoComplete, bool bDrawSelected, bool bValidate, bool bFiltered);

SA_RET_NN_VALID UITextEntry *ui_TextEntryCreateWithEnumCombo(SA_PARAM_NN_STR const unsigned char *text, int x, int y, 
															SA_PARAM_NN_VALID struct StaticDefineInt *enumTable,
															bool bAutoComplete, bool bDrawSelected, bool bValidate, bool bFiltered);

// Creates the text entry with a pull-down text area
SA_RET_NN_VALID UITextEntry *ui_TextEntryCreateWithTextArea(SA_PARAM_NN_STR const unsigned char *text, int x, int y);

// Create the text entry with a SMFView area
SA_RET_NN_VALID UITextEntry *ui_TextEntryCreateWithSMFView(SA_PARAM_NN_STR const unsigned char *text, int x, int y);

#endif
