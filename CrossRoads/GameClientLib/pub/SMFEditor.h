#pragma once
GCC_SYSTEM
#ifndef SMF_EDITOR_H
#define SMF_EDITOR_H

#include "UICore.h"
#include "UIPane.h"

typedef struct KeyInput KeyInput;
typedef struct UITextArea UITextArea;
typedef struct UISMFView UISMFView;

typedef struct UISMFEditor
{
	UI_INHERIT_FROM(UI_WIDGET_TYPE UI_PANE_TYPE);

	UITextArea *pTextArea;
	UISMFView *pView;

	UIActivationFunc changedF;
	UserData changedData;
} UISMFEditor;

UISMFEditor *ui_SMFEditorCreate(bool bVertical);
UITextArea *ui_SMFEditorGetTextArea(UISMFEditor *pEditor);
void ui_SMFEditorInitialize(UISMFEditor *pEditor, bool bVertical);
bool ui_SMFEditorTextAreaInput(UITextArea *pArea, KeyInput *pKey);
void ui_SMFEditorFree(UISMFEditor *pEditor);

void ui_SMFEditorSetText(UISMFEditor *pEditor, const char *pchText);
void ui_SMFEditorSetTextAndCallback(UISMFEditor *pEditor, const char *pchText);

void ui_SMFEditorSetChangedCallback(UISMFEditor *pEditor, UIActivationFunc changedF, UserData changedData);

#endif