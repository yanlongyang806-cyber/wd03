/***************************************************************************



***************************************************************************/

#ifndef UI_EDITABLE_H
#define UI_EDITABLE_H
GCC_SYSTEM

#include "UICore.h"

typedef bool (*UITextValidateFunc)(UIAnyWidget *, unsigned char **oldString, unsigned char **newString, UserData validateData);

typedef struct UIUndoAction UIUndoAction;

//////////////////////////////////////////////////////////////////////////
// This provides a basic "abstract" class for text editing widgets.
// It stores the text string in the UIWidget->text member, as UTF-8.
// All offsets are in Unicode codepoints, which for the purposes of
// our games so far means characters.

#define UI_EDITABLE_TYPE UIEditable editable;
#define UI_EDITABLE(widget) (&(widget)->editable)

typedef struct TextBuffer TextBuffer;

typedef struct UIEditable
{
	UIWidget widget;

	TextBuffer *pBuffer;

	bool selecting : 1;

	// True if the text changed within the last frame, used to reflow
	// text in multiline editables.
	bool dirty : 1;

	// True if the cursor position changed within the last frame,
	// used to reposition visible areas of the text.
	bool cursorDirty : 1;

	//True if you don't want refreshes to change the cursor position
	bool keepCursor : 1;

	// DO NOT CALL THIS DIRECTLY.  YOU PROBABLY SHOULD CALL
	// ui_EditableChanged.
	// 
	// Called when the text is changed. Sometimes it may be called
	// when the text is unchanged (for example, if someone sets text
	// to the same value, or if someone deletes a 0 length substring).
	UIActivationFunc changedF;
	UserData changedData;

	// Called alongside changedF, to allow editables to update an SMF
	// preview.  Any call to this that does not call changedF is a
	// bug.  This exists because much code sets and gets changedF and
	// changedData directly instead of through wrapper functions.
	UIActivationFunc smfUpdateF;
	UserData smfUpdateData;
	
	UITextValidateFunc validateF;
	UserData validateData;

	// For cursor blinking.
	F32 time;

	char *defaultString_USEACCESSOR;
	REF_TO(Message) defaultMessage_USEACCESSOR;
} UIEditable;

void ui_EditableInitialize(SA_PARAM_NN_VALID UIEditable *edit, SA_PARAM_NN_STR const unsigned char *text);
void ui_EditableFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UIEditable *edit);

SA_RET_NN_STR const unsigned char *ui_EditableGetText(SA_PARAM_NN_VALID UIEditable *edit);
bool ui_EditableSetText(SA_PARAM_NN_VALID UIEditable *edit, SA_PARAM_NN_STR const unsigned char *text);
bool ui_EditableSetTextAndCallback(SA_PARAM_NN_VALID UIEditable *edit, SA_PARAM_NN_STR const unsigned char *text);

// These functions all call the changedF callback if it is set.
// Talk to Stephen if you need them to not call it.
bool ui_EditableInsertText(SA_PARAM_NN_VALID UIEditable *edit, U32 at, SA_PARAM_NN_STR const unsigned char *text);
bool ui_EditableDeleteText(SA_PARAM_NN_VALID UIEditable *edit, U32 from, U32 length);
bool ui_EditableDeleteSelection(SA_PARAM_NN_VALID UIEditable *edit);
bool ui_EditableDeletePreviousWord(UIEditable *edit);
bool ui_EditableDeleteNextWord(UIEditable *edit);
bool ui_EditableBackspace(UIEditable *edit);
bool ui_EditableDelete(UIEditable *edit);

U32 ui_EditableGetCursorPosition(SA_PARAM_NN_VALID UIEditable *edit);
void ui_EditableSetCursorPosition(SA_PARAM_NN_VALID UIEditable *edit, U32 cursorPos);
void ui_EditableSetSelection(SA_PARAM_NN_VALID UIEditable *edit, U32 from, U32 length);
void ui_EditableSelectToPos(UIEditable *edit, U32 toPos);

void ui_EditableSetChangedCallback(SA_PARAM_NN_VALID UIEditable *edit, UIActivationFunc changedF, UserData changedData);
void ui_EditableSetSMFUpdateCallback(SA_PARAM_NN_VALID UIEditable *edit, UIActivationFunc smfUpdateF, UserData smfUpdateData);
void ui_EditableSetValidationCallback(SA_PARAM_NN_VALID UIEditable *edit, UITextValidateFunc validateF, UserData validateData);

void ui_EditableSetDefaultString(SA_PARAM_NN_VALID UIEditable *pEditable, SA_PARAM_OP_STR const char *pchDefault);
void ui_EditableSetDefaultMessage(SA_PARAM_NN_VALID UIEditable *pEditable, SA_PARAM_OP_STR const char *messageKey);
const char* ui_EditableGetDefault(SA_PARAM_NN_VALID UIEditable *pEditable);

void ui_EditableCopy(SA_PARAM_NN_VALID UIEditable *edit);
bool ui_EditableCut(SA_PARAM_NN_VALID UIEditable *edit);
bool ui_EditablePaste(SA_PARAM_NN_VALID UIEditable *edit);
void ui_EditableUndo(UIEditable *edit);
void ui_EditableRedo(UIEditable *edit);
void ui_EditableSelectAll(UIEditable *edit);
bool ui_EditableInsertCodepoint(UIEditable *edit, U16 wch);

// If iMaxLength <= 0, then the maximum length is unbounded.
void ui_EditableSetMaxLength(SA_PARAM_NN_VALID UIEditable *edit, S32 iMaxLength);

void ui_EditableChanged(UIEditable* edit);

// Call this inside any UIEditable's input function for the default case. This function will ensure that the UIEditable gobbles up
// most keyboard inputs while allowing to propgate the few that should be handled elsewhere. See its use by UITextEntry and UITextArea.
// [COR-15337] Typing 'E' or 'R' in a text field in an editor occasionally causing the editor to close.
bool ui_EditableInputDefault(SA_PARAM_NN_VALID UIEditable* edit, SA_PARAM_NN_VALID KeyInput *input);

#endif
