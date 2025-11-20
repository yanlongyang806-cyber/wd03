#ifndef NO_EDITORS

#pragma once
GCC_SYSTEM

#include "UICore.h"

typedef struct Message Message;
typedef struct MessageEditor MessageEditor;
typedef struct UIButton UIButton;
typedef struct UITextEntry UITextEntry;
typedef struct UIWindow UIWindow;
typedef struct DisplayMessage DisplayMessage;
typedef struct UIFilteredList UIFilteredList;
typedef struct UIMessageEntry UIMessageEntry;

typedef bool (*UIPreChangeFunc)(UIMessageEntry *pEntry, UserData pData);

//typedef void (*MessageEditorApplyCallback)(MessageEditor *, Message *, UserData);
//typedef void (*MessageEditorCloseCallback)(MessageEditor *, void *);

typedef struct UIMessageEntry
{
	// This is required at the start of the widget
	UIWidget widget;

	// Child widgets and such
	UITextEntry *pEntry;
	MessageEditor *pMsgEditor;

	// Callback data
	UIActivationFunc cbChanged;
	UserData pChangedData;
	UIPreChangeFunc cbPreChanged;
	UserData pPreChangedData;

	// The message
	Message *pMessage;
	bool bCanEditKey;
	bool bCanEditScope;

	// Skin info
	UISkin *pKeySkin;
	UISkin *pScopeSkin;
	UISkin *pDescSkin;
	UISkin *pDefaultStringSkin;
} UIMessageEntry;

SA_RET_NN_VALID UIMessageEntry *ui_MessageEntryCreate(const Message *pMessage, int x, int y, int width);
SA_RET_NN_VALID UIMessageEntry *ui_MessageEntryCreateWithSMFView(const Message *pMessage, int x, int y, int width);
void ui_MessageEntryFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UIMessageEntry *pMsgEntry);
void ui_MessageEntryTick(SA_PARAM_NN_VALID UIMessageEntry *pMsgEntry, UI_PARENT_ARGS);
void ui_MessageEntryDraw(SA_PARAM_NN_VALID UIMessageEntry *pMsgEntry, UI_PARENT_ARGS);

const Message *ui_MessageEntryGetMessage(SA_PARAM_NN_VALID UIMessageEntry *pMsgEntry);
void ui_MessageEntrySetMessage(SA_PARAM_NN_VALID UIMessageEntry *pMsgEntry, const Message *pMsgText);

void ui_MessageEntrySetCanEditKey(SA_PARAM_NN_VALID UIMessageEntry *pMsgEntry, bool bCanEditKey);
void ui_MessageEntrySetCanEditScope(SA_PARAM_NN_VALID UIMessageEntry *pMsgEntry, bool bCanEditScope);
void ui_MessageEntrySetSkin(SA_PARAM_NN_VALID UIMessageEntry *pMsgEntry, SA_PARAM_NN_VALID UISkin *pSkin, bool bKeySkin, bool bScopeSkin, bool bDescSkin, bool bDefaultStringSkin);

void ui_MessageEntrySetChangedCallback(SA_PARAM_NN_VALID UIMessageEntry *pMsgEntry, UIActivationFunc cbChanged, UserData pChangedData);
void ui_MessageEntrySetPreChangedCallback(SA_PARAM_NN_VALID UIMessageEntry *pMsgEntry, UIPreChangeFunc cbPreChanged, UserData pPreChangedData);

#endif