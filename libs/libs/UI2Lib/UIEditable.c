/***************************************************************************



***************************************************************************/

#include "EString.h"
#include "StringUtil.h"
#include "TextBuffer.h"

#include "UIEditable.h"
#include "timing.h"

#include "inputData.h"
#include "inputText.h"
#include "inputKeyBind.h"
#include "Message.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

void ui_EditableChanged(UIEditable* edit)
{
	PERFINFO_AUTO_START_FUNC();

	if (edit->smfUpdateF)
	{
		PERFINFO_AUTO_START("smfUpdate", 1);
		edit->smfUpdateF(edit, edit->smfUpdateData);
		PERFINFO_AUTO_STOP();
	}

	if (edit->changedF)
	{
		PERFINFO_AUTO_START("changed", 1);
		edit->changedF(edit, edit->changedData);
		PERFINFO_AUTO_STOP();
	}

	PERFINFO_AUTO_STOP();
}

void ui_EditableUndo(UIEditable *edit)
{
	TextBuffer_Undo(edit->pBuffer);
	ui_EditableChanged(edit);
	edit->dirty = true;
}

void ui_EditableRedo(UIEditable *edit)
{
	TextBuffer_Redo(edit->pBuffer);
	ui_EditableChanged(edit);
	edit->dirty = true;
}

void ui_EditableSelectAll(UIEditable *edit)
{
	TextBuffer_SelectAll(edit->pBuffer);
}

void ui_EditableInitialize(UIEditable *edit, const unsigned char *text)
{
	edit->pBuffer = TextBuffer_Create(text);
	edit->dirty = true;
	edit->cursorDirty = true;
	edit->widget.bConsumesEscape = true;
}

void ui_EditableFreeInternal(UIEditable *edit)
{
	// We need to unfocus here rather than in ui_WidgetFreeInternal, since by
	// that time our TextBuffer will be gone.
	if (ui_IsFocused(edit))
	{
		UI_WIDGET(edit)->onUnfocusF = NULL;
		ui_SetFocus(NULL);
	}
	TextBuffer_DestroySafe(&edit->pBuffer);
	estrDestroy(&edit->defaultString_USEACCESSOR);
	REMOVE_HANDLE(edit->defaultMessage_USEACCESSOR);
	ui_WidgetFreeInternal(UI_WIDGET(edit));
}

const unsigned char *ui_EditableGetText(UIEditable *edit)
{
	return TextBuffer_GetText(edit->pBuffer);
}

static bool ui_EditableValidateAndSet(UIEditable *pEdit, unsigned char **ppchOld, int iCursorOld)
{
	char *pchNew = estrStackCreateFromStr(TextBuffer_GetText(pEdit->pBuffer));
	bool bValid = true;
	if (pEdit->validateF)
		bValid = pEdit->validateF(pEdit, ppchOld, &pchNew, pEdit->validateData);
	if(!bValid) {
		TextBuffer_SetCursor(pEdit->pBuffer, iCursorOld);
	}
	bValid = TextBuffer_SetText(pEdit->pBuffer, bValid ? pchNew : *ppchOld);
	estrDestroy(&pchNew);
	estrDestroy(ppchOld);
	pEdit->dirty = true;
	return bValid;
}

bool ui_EditableSetText(UIEditable *edit, const unsigned char *text)
{
	bool ret = true;
	unsigned char *copy = estrStackCreateFromStr(TextBuffer_GetText(edit->pBuffer));
	int cursorOld = TextBuffer_GetCursor(edit->pBuffer);
	if (copy && text && !strcmp(copy, text))
	{
		estrDestroy(&copy);
		return false;
	}
	if(!TextBuffer_SetText(edit->pBuffer, text))
	{
		estrDestroy(&copy);
		return false;
	}
	ret = ui_EditableValidateAndSet(edit, &copy, cursorOld);
	// MJF Jan/21/2013 -- This behavior seems very weird.  Why show
	// the end of data instead of the start?
	//
	// If you really want to re-enable this old behavior, please talk
	// to me first so we can discuss how to support UGC (which never
	// wants this behavior).
	// 
   	// if (!edit->keepCursor)
   	// {
   	// 	if (ui_IsActive(UI_WIDGET(edit)))
   	// 		TextBuffer_CursorToEnd(edit->pBuffer);
   	// 	else
   	// 		TextBuffer_SetCursor(edit->pBuffer, 0);
   	// 	edit->cursorDirty = true;
   	// }
	edit->dirty = true;
	return ret;
}

bool ui_EditableSetTextAndCallback(UIEditable *edit, const unsigned char *text)
{
	bool retval = ui_EditableSetText(edit, text);
	ui_EditableChanged(edit);
	return retval;
}

bool ui_EditableInsertText(UIEditable *edit, U32 at, const unsigned char *text)
{
	char *pchOld;
	int iCursorOld;
	bool bRet;
	if (!(text && *text))
		return false;
	pchOld = estrStackCreateFromStr(TextBuffer_GetText(edit->pBuffer));
	iCursorOld = TextBuffer_GetCursor(edit->pBuffer);
	TextBuffer_InsertText(edit->pBuffer, at, text);
	bRet = ui_EditableValidateAndSet(edit, &pchOld, iCursorOld);
	ui_EditableChanged(edit);
	return bRet;
}

bool ui_EditableDeleteText(UIEditable *edit, U32 from, U32 length)
{
	char *pchOld;
	int iCursorOld;
	bool bRet;
	if (length == 0)
		return false;
	pchOld = estrStackCreateFromStr(TextBuffer_GetText(edit->pBuffer));
	iCursorOld = TextBuffer_GetCursor(edit->pBuffer);
	TextBuffer_RemoveText(edit->pBuffer, from, length);
	bRet = ui_EditableValidateAndSet(edit, &pchOld, iCursorOld);
	ui_EditableChanged(edit);
	return bRet;
}

bool ui_EditableDeleteSelection(UIEditable *edit)
{
	char *pchOld = estrStackCreateFromStr(TextBuffer_GetText(edit->pBuffer));
	int iCursorOld = TextBuffer_GetCursor(edit->pBuffer);
	bool bRet;
	TextBuffer_SelectionDelete(edit->pBuffer);
	bRet = ui_EditableValidateAndSet(edit, &pchOld, iCursorOld);
	ui_EditableChanged(edit);
	return bRet;
}

bool ui_EditableDeletePreviousWord(UIEditable *edit)
{
	char *pchOld = estrStackCreateFromStr(TextBuffer_GetText(edit->pBuffer));
	int iCursorOld = TextBuffer_GetCursor(edit->pBuffer);
	bool bRet;
	TextBuffer_DeletePreviousWord(edit->pBuffer);
	bRet = ui_EditableValidateAndSet(edit, &pchOld, iCursorOld);
	ui_EditableChanged(edit);
	return bRet;
}

bool ui_EditableDeleteNextWord(UIEditable *edit)
{
	char *pchOld = estrStackCreateFromStr(TextBuffer_GetText(edit->pBuffer));
	int iCursorOld = TextBuffer_GetCursor(edit->pBuffer);
	bool bRet;
	TextBuffer_DeleteNextWord(edit->pBuffer);
	bRet = ui_EditableValidateAndSet(edit, &pchOld, iCursorOld);
	ui_EditableChanged(edit);
	return bRet;
}

bool ui_EditableBackspace(UIEditable *edit)
{
	char *pchOld = estrStackCreateFromStr(TextBuffer_GetText(edit->pBuffer));
	int iCursorOld = TextBuffer_GetCursor(edit->pBuffer);
	bool bRet = true;
	if(TextBuffer_Backspace(edit->pBuffer)){
		bRet = ui_EditableValidateAndSet(edit, &pchOld, iCursorOld);
		ui_EditableChanged(edit);
	}else{
		estrDestroy(&pchOld);
	}
	return bRet;
}

bool ui_EditableDelete(UIEditable *edit)
{
	char *pchOld = estrStackCreateFromStr(TextBuffer_GetText(edit->pBuffer));
	int iCursorOld = TextBuffer_GetCursor(edit->pBuffer);
	bool bRet = true;
	if(TextBuffer_Delete(edit->pBuffer)){
		bRet = ui_EditableValidateAndSet(edit, &pchOld, iCursorOld);
		ui_EditableChanged(edit);
	}else{
		estrDestroy(&pchOld);
	}
	return bRet;
}

U32 ui_EditableGetCursorPosition(UIEditable *edit)
{
	return TextBuffer_GetCursor(edit->pBuffer);
}

void ui_EditableSetCursorPosition(UIEditable *edit, U32 cursorPos)
{
	TextBuffer_SetCursor(edit->pBuffer, cursorPos);
}

void ui_EditableSetSelection(UIEditable *edit, U32 from, U32 length)
{
	TextBuffer_SetSelectionBounds(edit->pBuffer, from, from + length);
}

void ui_EditableSelectToPos(UIEditable *edit, U32 toPos)
{
	TextBuffer_SelectToPos(edit->pBuffer, toPos);
}

void ui_EditableSetChangedCallback(UIEditable *edit, UIActivationFunc changedF, UserData changedData)
{
	edit->changedF = changedF;
	edit->changedData = changedData;
}

void ui_EditableSetSMFUpdateCallback(UIEditable *edit, UIActivationFunc smfUpdateF, UserData smfUpdateData)
{
	edit->smfUpdateF = smfUpdateF;
	edit->smfUpdateData = smfUpdateData;
}

void ui_EditableSetValidationCallback(UIEditable *edit, UITextValidateFunc validateF, UserData validateData)
{
	edit->validateF = validateF;
	edit->validateData = validateData;
}

void ui_EditableCopy(UIEditable *edit)
{
	TextBuffer_Copy(edit->pBuffer);
}

bool ui_EditableCut(UIEditable *edit)
{
	char *pchOld = estrStackCreateFromStr(TextBuffer_GetText(edit->pBuffer));
	int iCursorOld = TextBuffer_GetCursor(edit->pBuffer);
	bool bRet;
	TextBuffer_Cut(edit->pBuffer);
	bRet = ui_EditableValidateAndSet(edit, &pchOld, iCursorOld);
	ui_EditableChanged(edit);
	edit->dirty = true;
	return bRet;
}

bool ui_EditablePaste(UIEditable *edit)
{
	char *pchOld = estrStackCreateFromStr(TextBuffer_GetText(edit->pBuffer));
	int iCursorOld = TextBuffer_GetCursor(edit->pBuffer);
	bool bRet;
	TextBuffer_Paste(edit->pBuffer);
	bRet = ui_EditableValidateAndSet(edit, &pchOld, iCursorOld);
	ui_EditableChanged(edit);
	edit->dirty = true;
	return bRet;
}

bool ui_EditableInsertCodepoint(UIEditable *edit, U16 wch)
{
	char *pchOld = estrStackCreateFromStr(TextBuffer_GetText(edit->pBuffer));
	int iCursorOld = TextBuffer_GetCursor(edit->pBuffer);
	bool bRet;
	TextBuffer_InsertCodepoint(edit->pBuffer, wch);
	bRet = ui_EditableValidateAndSet(edit, &pchOld, iCursorOld);
	ui_EditableChanged(edit);
	edit->time = 0;
	edit->dirty = true;
	return bRet;
}

void ui_EditableSetMaxLength(UIEditable *edit, S32 iMaxLength)
{
	TextBuffer_SetMaxLength(edit->pBuffer, iMaxLength);
}

void ui_EditableSetDefaultString(UIEditable *pEditable, const char *pchDefault)
{
	REMOVE_HANDLE( pEditable->defaultMessage_USEACCESSOR );
	if (!pchDefault)
		estrDestroy(&pEditable->defaultString_USEACCESSOR);
	else
		estrCopy2(&pEditable->defaultString_USEACCESSOR, pchDefault);
}

void ui_EditableSetDefaultMessage(UIEditable *pEditable, const char *messageKey)
{
	estrDestroy(&pEditable->defaultString_USEACCESSOR);
	if (!messageKey)
		REMOVE_HANDLE( pEditable->defaultMessage_USEACCESSOR );
	else
		SET_HANDLE_FROM_STRING( "Message", messageKey, pEditable->defaultMessage_USEACCESSOR );
}

const char* ui_EditableGetDefault(SA_PARAM_NN_VALID UIEditable *pEditable)
{
	if( IS_HANDLE_ACTIVE( pEditable->defaultMessage_USEACCESSOR )) {
		return TranslateMessageRef( pEditable->defaultMessage_USEACCESSOR );
	} else {
		return pEditable->defaultString_USEACCESSOR;
	}
}

// Call this inside any UIEditable's input function for the default case. This function will ensure that the UIEditable gobbles up
// most keyboard inputs while allowing to propgate the few that should be handled elsewhere. See its use by UITextEntry and UITextArea.
// [COR-15337] Typing 'E' or 'R' in a text field in an editor occasionally causing the editor to close.
bool ui_EditableInputDefault(SA_PARAM_NN_VALID UIEditable* edit, SA_PARAM_NN_VALID KeyInput *input)
{
	if(KIT_EditKey == input->type)
	{
		// any non-chord is something the user probably doesn't want passed through.
		// any unrecognized chord is probably something they do.
		if (input->attrib & (KIA_CONTROL | KIA_ALT) || input->scancode == INP_TAB || input->scancode == INP_ESCAPE)
			return false;
		else if (key_IsJoystick(input->scancode))
			return false;
	}

	return true;
}
