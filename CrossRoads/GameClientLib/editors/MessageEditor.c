//
// MessageEditor.c
//

#ifndef NO_EDITORS

#include "EditorPrefs.h"
#include "Message.h"
#include "MessageEditor.h"
#include "StringCache.h"
#include "UIButton.h"
#include "UICheckButton.h"
#include "UILabel.h"
#include "UITextEntry.h"
#include "UITextArea.h"
#include "UIWindow.h"



AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static void me_applyChanges(UIButton *pButton, MessageEditor *pEditor)
{
	// Get data from controls to populate message
	StructFreeString(pEditor->pMessage->pcDescription);
	StructFreeString(pEditor->pMessage->pcDefaultString);

	pEditor->pMessage->pcMessageKey = allocAddString(ui_TextEntryGetText(pEditor->pKeyEntry));
	pEditor->pMessage->pcScope = allocAddString(ui_TextEntryGetText(pEditor->pScopeEntry));
	pEditor->pMessage->pcDescription = StructAllocString(ui_TextEntryGetText(pEditor->pDescEntry));
	pEditor->pMessage->pcDefaultString = StructAllocString(ui_TextAreaGetText(pEditor->pStringEntry));
	pEditor->pMessage->bFinal = ui_CheckButtonGetState(pEditor->pFinal);
	pEditor->pMessage->bDoNotTranslate = ui_CheckButtonGetState(pEditor->pNoTranslate);

	// Issue apply callback
	if(pEditor->cbApply)
		pEditor->cbApply(pEditor, pEditor->pMessage, pEditor->pUserData);

	// Clean up
	MessageEditorDestroy(pEditor);
}


static void me_cancelChanges(void *unused, MessageEditor *pEditor)
{
	MessageEditorDestroy(pEditor);
}

static bool me_closeWindowCB(void *unused, MessageEditor *pEditor)
{
	MessageEditorDestroy(pEditor);
	return true;
}

static void me_createWindow(MessageEditor *pEditor, Message *pMessage)
{
	UIButton *pButton;
	UILabel *pLabel;
	F32 y = 0;

	// Create new Message if none provided
	if(!pMessage)
	{
		pMessage = StructCreate(parse_Message);
		pEditor->pMessage = pMessage;
	}

	// Create the window
	pEditor->pWindow = ui_WindowCreate("Edit Message", 200, 200, 350, 250);
	ui_WidgetSetFamily(UI_WIDGET(pEditor->pWindow), UI_FAMILY_EDITOR);
	EditorPrefGetWindowPosition("Message Editor", "Window Position", "Message", pEditor->pWindow);
	ui_WindowSetCloseCallback(pEditor->pWindow, me_closeWindowCB, pEditor);

	// Set up the controls
	pLabel = ui_LabelCreate("Message Key", 0, y);
	ui_WindowAddChild(pEditor->pWindow, pLabel);
	pEditor->pKeyEntry = ui_TextEntryCreate(pMessage->pcMessageKey ? pMessage->pcMessageKey : "", 100, y);
	ui_WidgetSetWidthEx(UI_WIDGET(pEditor->pKeyEntry), 1, UIUnitPercentage);
	ui_WindowAddChild(pEditor->pWindow, pEditor->pKeyEntry);
	if (!pEditor->bCanEditKey) {
		ui_SetActive(UI_WIDGET(pEditor->pKeyEntry), false);
	}

	y += 28;

	pLabel = ui_LabelCreate("Scope", 0, y);
	ui_WindowAddChild(pEditor->pWindow, pLabel);
	pEditor->pScopeEntry = ui_TextEntryCreate(pMessage->pcScope ? pMessage->pcScope : "", 100, y);
	ui_WidgetSetWidthEx(UI_WIDGET(pEditor->pScopeEntry), 1, UIUnitPercentage);
	ui_WindowAddChild(pEditor->pWindow, pEditor->pScopeEntry);
	if (!pEditor->bCanEditScope) {
		ui_SetActive(UI_WIDGET(pEditor->pScopeEntry), false);
	}

	y += 28;

	pLabel = ui_LabelCreate("Description", 0, y);
	ui_WindowAddChild(pEditor->pWindow, pLabel);
	pEditor->pDescEntry = ui_TextEntryCreateWithTextArea(pMessage->pcDescription ? pMessage->pcDescription : "", 100, y);
	ui_WidgetSetWidthEx(UI_WIDGET(pEditor->pDescEntry), 1, UIUnitPercentage);
	ui_WindowAddChild(pEditor->pWindow, pEditor->pDescEntry);

	y += 28;

	pLabel = ui_LabelCreate("Default String", 0, y);
	ui_WindowAddChild(pEditor->pWindow, pLabel);
	pEditor->pStringEntry = ui_TextAreaCreate(pMessage->pcDefaultString ? pMessage->pcDefaultString : "");
	ui_WidgetSetPosition(UI_WIDGET(pEditor->pStringEntry), 100, y);
	ui_WidgetSetWidthEx(UI_WIDGET(pEditor->pStringEntry), 1, UIUnitPercentage);
	ui_WidgetSetHeightEx(UI_WIDGET(pEditor->pStringEntry), 1, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pEditor->pStringEntry), 0, 0, 0, 28);
	ui_WindowAddChild(pEditor->pWindow, pEditor->pStringEntry);

	// The rest are bottom relative

	pEditor->pFinal = ui_CheckButtonCreate(0, 28, "Final", pMessage->bFinal);
	ui_WidgetSetWidth(UI_WIDGET(pEditor->pFinal), 100);
	ui_WidgetSetPositionEx(UI_WIDGET(pEditor->pFinal), 0, 28, 0, 0, UIBottomLeft);
	ui_WindowAddChild(pEditor->pWindow, pEditor->pFinal);

	pEditor->pNoTranslate = ui_CheckButtonCreate(0, 0, "Do not translate", pMessage->bDoNotTranslate);
	ui_WidgetSetPositionEx(UI_WIDGET(pEditor->pNoTranslate), 0, 0, 0, 0, UIBottomLeft);
	ui_WindowAddChild(pEditor->pWindow, pEditor->pNoTranslate);

	pButton = ui_ButtonCreate("OK", 90, 0, me_applyChanges, pEditor);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 80);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 90, 0, 0, 0, UIBottomRight);
	ui_WindowAddChild(pEditor->pWindow, pButton);

	pButton = ui_ButtonCreate("Cancel", 0, 0, me_cancelChanges, pEditor);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 80);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 0, 0, 0, 0, UIBottomRight);
	ui_WindowAddChild(pEditor->pWindow, pButton);
}

// Create the editor and show it
MessageEditor *MessageEditorCreate(Message *pMessage, MessageEditorApplyCallback cbApply, MessageEditorCloseCallback cbClose, void *pUserData, bool bCanEditKey, bool bCanEditScope)
{
	// Create the editor structure
	MessageEditor *pEditor = calloc(1, sizeof(MessageEditor));

	// Initialize the data
	pEditor->pMessage = StructClone(parse_Message, pMessage);
	pEditor->bCanEditKey = bCanEditKey;
	pEditor->bCanEditScope = bCanEditScope;
	pEditor->cbApply = cbApply;
	pEditor->cbClose = cbClose;
	pEditor->pUserData = pUserData;

	// Show the window
	me_createWindow(pEditor, pEditor->pMessage);
	ui_WindowPresent(pEditor->pWindow);

	return pEditor;
}

// Destroy the editor
void MessageEditorDestroy(MessageEditor *pEditor)
{
	EditorPrefStoreWindowPosition("Message Editor", "Window Position", "Message", pEditor->pWindow);

	if (pEditor->cbClose) {
		pEditor->cbClose(pEditor, pEditor->pUserData);
	}

	ui_WindowHide(pEditor->pWindow);
	ui_WidgetQueueFree((UIWidget*)pEditor->pWindow);
	StructDestroy(parse_Message, pEditor->pMessage);
	free(pEditor);
}

#endif