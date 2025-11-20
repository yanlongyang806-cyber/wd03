//
// MessageEditor.h
//

#ifndef NO_EDITORS

#include "UICore.h"

#pragma once
GCC_SYSTEM

typedef struct Message Message;
typedef struct MessageEditor MessageEditor;
typedef struct UICheckButton UICheckButton;
typedef struct UITextArea UITextArea;
typedef struct UITextEntry UITextEntry;
typedef struct UIWindow UIWindow;

typedef struct MessageEditor
{
	// The message being edited
	Message *pMessage;
	bool bCanEditKey;
	bool bCanEditScope;

	// The window and the widgets
	UIWindow *pWindow;
	UITextEntry *pKeyEntry;
	UITextEntry *pScopeEntry;
	UITextEntry *pDescEntry;
	UITextArea *pStringEntry;
	UICheckButton *pFinal;
	UICheckButton *pNoTranslate;

	// Callback info
	MessageEditorApplyCallback cbApply;    // Apply callback
	MessageEditorCloseCallback cbClose;  // Window close/destroy callback
	void *pUserData;
} MessageEditor;


// Create the editor and show it
// After the callback passes the message, the window is closed and the message is freed
MessageEditor *MessageEditorCreate(Message *pMessage, MessageEditorApplyCallback cbApply, MessageEditorCloseCallback cbClose, void *pUserData, bool bCanEditKey, bool bCanEditScope);

// Destroy the editor
void MessageEditorDestroy(MessageEditor *pEditor);

#endif