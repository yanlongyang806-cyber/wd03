// FIXME(jfw): Somehow this file has resulted in UI2Lib depending on GameClientLib.
// Which is really horrible, and needs to be fixed.

#ifndef NO_EDITORS

#include "inputMouse.h"

#include "GfxClipper.h"
#include "Message.h"
#include "MessageEditor.h"
#include "StringUtil.h"
#include "UIButton.h"
#include "UIMessageEntry.h"
#include "UISkin.h"
#include "UITextArea.h"
#include "UITextEntry.h"
#include "UIWindow.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static void UpdateMessageAndCallback(UITextEntry *pEntry, UIMessageEntry *pMsgEntry)
{
	const char *pcNewString;
	StructFreeString(pMsgEntry->pMessage->pcDefaultString);
	pcNewString = ui_TextEntryGetText(pMsgEntry->pEntry);
	if(pcNewString && pcNewString[0] && StringIsAllWhiteSpace(pcNewString)) {
		pcNewString = "";
		ui_TextEntrySetText(pMsgEntry->pEntry, pcNewString);
	}
	pMsgEntry->pMessage->pcDefaultString = StructAllocString(pcNewString);
	
	if (pMsgEntry->cbChanged)
		pMsgEntry->cbChanged(pMsgEntry, pMsgEntry->pChangedData);
}

static void ui_MessageEntryContextProxy(UITextEntry *pEntry, UIMessageEntry *pMsgEntry)
{
	if (pMsgEntry->widget.contextF)
		pMsgEntry->widget.contextF(pMsgEntry, pMsgEntry->widget.contextData);	
}

static void MessageEditorApply(MessageEditor *pEditor, Message *pMsg, UIMessageEntry *pMsgEntry)
{
	assert( pMsgEntry->pMessage );
	StructCopyAll(parse_Message, pMsg, pMsgEntry->pMessage);

	ui_TextEntrySetText(pMsgEntry->pEntry, pMsg->pcDefaultString);

	UpdateMessageAndCallback(pMsgEntry->pEntry, pMsgEntry);
}

static void MessageEditorClear(MessageEditor *editor, UIMessageEntry *pMsgEntry)
{
	pMsgEntry->pMsgEditor = NULL;
}

static void OpenMessageEditor(UIButton *pButton, UIMessageEntry *pMsgEntry)
{
	assert( pMsgEntry->pMessage );
	
	// Don't open if pre-change returns false
	if (pMsgEntry->cbPreChanged && !(*pMsgEntry->cbPreChanged)(pMsgEntry, pMsgEntry->pPreChangedData))
		return;

	if (pMsgEntry->pMsgEditor)
		ui_WindowPresent(pMsgEntry->pMsgEditor->pWindow);
	else
		pMsgEntry->pMsgEditor = MessageEditorCreate(pMsgEntry->pMessage, MessageEditorApply, MessageEditorClear, pMsgEntry, pMsgEntry->bCanEditKey, pMsgEntry->bCanEditScope);

	ui_WidgetSkin(UI_WIDGET(pMsgEntry->pMsgEditor->pKeyEntry), pMsgEntry->pKeySkin);
	ui_WidgetSkin(UI_WIDGET(pMsgEntry->pMsgEditor->pScopeEntry), pMsgEntry->pScopeSkin);
	ui_WidgetSkin(UI_WIDGET(pMsgEntry->pMsgEditor->pDescEntry), pMsgEntry->pDescSkin);
	ui_WidgetSkin(UI_WIDGET(pMsgEntry->pMsgEditor->pDescEntry->area), pMsgEntry->pDescSkin);
	ui_WidgetSkin(UI_WIDGET(pMsgEntry->pMsgEditor->pStringEntry), pMsgEntry->pDefaultStringSkin);
}

static bool MessageEntryPreChange(UITextEntry *pEntry, UIMessageEntry *pMsgEntry)
{
	if (pMsgEntry->cbPreChanged)
		return (*pMsgEntry->cbPreChanged)(pMsgEntry, pMsgEntry->pPreChangedData);
	else
		return true;
}

static UIMessageEntry *ui_MessageEntryCreateEx(const Message *pMessage, int x, int y, int width, bool useSMF)
{
	UIButton *pButton;
	UIMessageEntry *pMsgEntry = calloc(1, sizeof(UIMessageEntry));
	F32 fHeight = 0;

	ui_WidgetInitialize(UI_WIDGET(pMsgEntry), ui_MessageEntryTick, ui_MessageEntryDraw, ui_MessageEntryFreeInternal, NULL, NULL);
	if( useSMF ) {
		pMsgEntry->pEntry = ui_TextEntryCreateWithSMFView(pMessage?pMessage->pcDefaultString:"", 0, 0);
	} else {
		pMsgEntry->pEntry = ui_TextEntryCreateWithTextArea(pMessage?pMessage->pcDefaultString:"", 0, 0);
	}
	
	pButton = ui_ButtonCreate("...", 0, 0, OpenMessageEditor, pMsgEntry);

	if (pMessage)
		pMsgEntry->pMessage = StructClone(parse_Message, (Message*)pMessage);
	else
		pMsgEntry->pMessage = StructCreate(parse_Message);
	pMsgEntry->bCanEditKey = true;
	pMsgEntry->bCanEditScope = true;

	ui_TextEntrySetFinishedCallback(pMsgEntry->pEntry, UpdateMessageAndCallback, pMsgEntry);
	ui_TextEntrySetChangedCallback(pMsgEntry->pEntry, MessageEntryPreChange, pMsgEntry);
	ui_WidgetSetContextCallback(UI_WIDGET(pMsgEntry->pEntry), ui_MessageEntryContextProxy, pMsgEntry);

	ui_WidgetSetPositionEx(UI_WIDGET(pButton), 0, 0, 0, 0, UITopRight);
	MAX1(fHeight, UI_WIDGET(pMsgEntry)->height);
	MAX1(fHeight, UI_WIDGET(pButton)->height);
	ui_WidgetSetHeight(UI_WIDGET(pMsgEntry), fHeight);
	ui_WidgetSetHeightEx(UI_WIDGET(pButton), 1.f, UIUnitPercentage);
	ui_WidgetSetHeightEx(UI_WIDGET(pMsgEntry->pEntry), 1.f, UIUnitPercentage);
	ui_WidgetSetWidthEx(UI_WIDGET(pMsgEntry->pEntry), 1.f, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pMsgEntry->pEntry), 0, UI_WIDGET(pButton)->width, 0, 0);
	ui_WidgetAddChild(UI_WIDGET(pMsgEntry), UI_WIDGET(pMsgEntry->pEntry));
	ui_WidgetAddChild(UI_WIDGET(pMsgEntry), UI_WIDGET(pButton));

	ui_WidgetSetPosition(UI_WIDGET(pMsgEntry),x,y);
	ui_WidgetSetWidthEx(UI_WIDGET(pMsgEntry),width,UIUnitFixed);

	return pMsgEntry;
}

UIMessageEntry *ui_MessageEntryCreate(const Message *pMessage, int x, int y, int width)
{
	return ui_MessageEntryCreateEx(pMessage, x, y, width, false);
}


UIMessageEntry *ui_MessageEntryCreateWithSMFView(const Message *pMessage, int x, int y, int width)
{
	return ui_MessageEntryCreateEx(pMessage, x, y, width, true);
}

void ui_MessageEntryFreeInternal(UIMessageEntry *pMsgEntry)
{
	if (pMsgEntry->pMsgEditor)
		MessageEditorDestroy(pMsgEntry->pMsgEditor);
	StructDestroy(parse_Message, pMsgEntry->pMessage);
	ui_WidgetFreeInternal(UI_WIDGET(pMsgEntry));
}

void ui_MessageEntryTick(UIMessageEntry *pMsgEntry, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pMsgEntry);
	UI_TICK_EARLY(pMsgEntry, true, true);
	UI_TICK_LATE(pMsgEntry);
}

void ui_MessageEntryDraw(UIMessageEntry *pMsgEntry, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pMsgEntry);
	UI_DRAW_EARLY(pMsgEntry);

	// Make sure changed/inherited flags move into text entry
	ui_SetChanged(UI_WIDGET(pMsgEntry->pEntry), ui_IsChanged(UI_WIDGET(pMsgEntry)));
	ui_SetInherited(UI_WIDGET(pMsgEntry->pEntry), ui_IsInherited(UI_WIDGET(pMsgEntry)));

	UI_DRAW_LATE(pMsgEntry);
}

const Message *ui_MessageEntryGetMessage(UIMessageEntry *pMsgEntry)
{
	const char *pcText = ui_TextEntryGetText(pMsgEntry->pEntry);

	assert( pMsgEntry->pMessage );
	
	if (pMsgEntry->pMessage->pcDefaultString && strcmp(pcText, pMsgEntry->pMessage->pcDefaultString) != 0) {
		StructFreeString(pMsgEntry->pMessage->pcDefaultString);
		pMsgEntry->pMessage->pcDefaultString = NULL;
	}
	if (!pMsgEntry->pMessage->pcDefaultString && !nullStr(pcText)) {
		pMsgEntry->pMessage->pcDefaultString = StructAllocString(pcText);
	}
	return pMsgEntry->pMessage;
}

void ui_MessageEntrySetMessage(UIMessageEntry *pMsgEntry, const Message *pMsg)
{
	assert( pMsgEntry->pMessage );
	
	StructDestroy(parse_Message, pMsgEntry->pMessage);
	if (pMsg)
		pMsgEntry->pMessage = StructClone(parse_Message, (Message*)pMsg);
	else
		pMsgEntry->pMessage = StructCreate(parse_Message);
	ui_TextEntrySetText(pMsgEntry->pEntry, (pMsg?pMsg->pcDefaultString:""));
}

void ui_MessageEntrySetCanEditKey(UIMessageEntry *pMsgEntry, bool bCanEditKey)
{
	pMsgEntry->bCanEditKey = bCanEditKey;
}

void ui_MessageEntrySetCanEditScope(UIMessageEntry *pMsgEntry, bool bCanEditScope)
{
	pMsgEntry->bCanEditScope = bCanEditScope;
}

void ui_MessageEntrySetSkin(UIMessageEntry *pMsgEntry, UISkin *pSkin, bool bKeySkin, bool bScopeSkin, bool bDescSkin, bool bDefaultStringSkin)
{
	if (bKeySkin)
		pMsgEntry->pKeySkin = pSkin;
	if (bScopeSkin)
		pMsgEntry->pScopeSkin = pSkin;
	if (bDescSkin)
		pMsgEntry->pDescSkin = pSkin;
	if (bDefaultStringSkin)
		pMsgEntry->pDefaultStringSkin = pSkin;

	ui_WidgetSkin(UI_WIDGET(pMsgEntry->pEntry), pMsgEntry->pDefaultStringSkin);
	ui_WidgetSkin(UI_WIDGET(pMsgEntry->pEntry->area), pMsgEntry->pDefaultStringSkin);
}

void ui_MessageEntrySetChangedCallback(UIMessageEntry *pMsgEntry, UIActivationFunc cbChanged, UserData pChangedData)
{
	pMsgEntry->cbChanged = cbChanged;
	pMsgEntry->pChangedData = pChangedData;
}

void ui_MessageEntrySetPreChangedCallback(UIMessageEntry *pMsgEntry, UIPreChangeFunc cbPreChanged, UserData pPreChangedData)
{
	pMsgEntry->cbPreChanged = cbPreChanged;
	pMsgEntry->pPreChangedData = pPreChangedData;
}

bool ui_MessageEntryFilterCallback(UIFilteredList *pFList, const char *pchValue, const char *pchFilter)
{
	if(match(pchFilter,pchValue))
		return true;

	return false;
}

#endif
