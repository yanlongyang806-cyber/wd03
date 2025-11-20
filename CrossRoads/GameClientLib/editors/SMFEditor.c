#include "EString.h"

#include "inputText.h"
#include "inputLib.h"

#include "TextBuffer.h"
#include "SMFEditor.h"
#include "UISMFView.h"
#include "UITextArea.h"
#include "UIWindow.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

static void SetTextFromTextArea(UITextArea *pText, UISMFEditor *pEditor)
{
	UISMFView *pView = pEditor->pView;
	char *text = NULL;
	estrStackCreate(&text);
	estrPrintf(&text, "%s", ui_TextAreaGetText(pText));
	ui_SMFViewSetText(pView, text, NULL);
	estrDestroy(&text);

	if (pEditor->changedF)
		pEditor->changedF(pEditor, pEditor->changedData);
}

static void SurroundSelection(UITextArea *pText, const char *pchOpen, const char *pchClose)
{
	unsigned const char *pchText = ui_TextAreaGetText(pText);
	// Commented out until I make this editor not suck anyway.
// 	if (pText->editable.selectionStart != pText->editable.selectionEnd)
// 	{
// 
// 		const char *pchStart = UTF8GetCodepoint(pText->editable.text, pText->editable.selectionStart);
// 		const char *pchEnd = UTF8GetCodepoint(pText->editable.text, pText->editable.selectionEnd);
// 		if (pchStart && pchEnd)
// 		{
// 			S32 iOldStart = pText->editable.selectionStart;
// 			char *pchNewStr = NULL;
// 			char *pchSelected = NULL;
// 	
// 			estrStackCreate(&pchNewStr);
// 			estrStackCreate(&pchSelected);
// 
// 			estrConcat(&pchSelected, pchStart, pchEnd - pchStart);
// 			if (strStartsWith(pchSelected, pchOpen) && strEndsWith(pchSelected, pchClose))
// 				estrConcat(&pchNewStr, pchSelected + strlen(pchOpen), (S32)(strlen(pchSelected) - (strlen(pchClose) + strlen(pchOpen))));
// 			else
// 				estrPrintf(&pchNewStr, "%s%s%s", pchOpen, pchSelected, pchClose);
// 			ui_TextAreaDeleteSelection(pText);
// 			pText->editable.cursorPos += ui_TextAreaInsertTextAt(pText, pText->editable.cursorPos, pchNewStr);
// 			ui_EditableSetSelection(UI_EDITABLE(pText), iOldStart, UTF8GetLength(pchNewStr));
// 			estrDestroy(&pchNewStr);
// 			estrDestroy(&pchSelected);
// 		}
// 	}
}

UISMFEditor *ui_SMFEditorCreate(bool bVertical)
{
	UISMFEditor *pEditor = calloc(1, sizeof(UISMFEditor));
	ui_SMFEditorInitialize(pEditor, bVertical);
	return pEditor;
}

void ui_SMFEditorInitialize(UISMFEditor *pEditor, bool bVertical)
{
	UITextArea *pText = ui_TextAreaCreate("");
	UISMFView *pView = ui_SMFViewCreate(0, 0, 0, 0);

	ui_PaneInitialize(&pEditor->pane, 0, 0, 0, 0, 0, 0, false MEM_DBG_PARMS_INIT);

	if (bVertical)
	{
		pView->widget.xPOffset = 0.5;
		ui_WidgetSetDimensionsEx(UI_WIDGET(pText), 0.5f, 1.f, UIUnitPercentage, UIUnitPercentage);
		ui_WidgetSetDimensionsEx(UI_WIDGET(pView), 0.5f, 1.f, UIUnitPercentage, UIUnitPercentage);
		pText->widget.rightPad = UI_HSTEP;
		pView->widget.leftPad = UI_HSTEP;
	}
	else
	{
		pView->widget.yPOffset = 0.5;
		ui_WidgetSetDimensionsEx(UI_WIDGET(pText), 1.f, 0.5f, UIUnitPercentage, UIUnitPercentage);
		ui_WidgetSetDimensionsEx(UI_WIDGET(pView), 1.f, 0.5f, UIUnitPercentage, UIUnitPercentage);
		pText->widget.bottomPad = UI_HSTEP;
		pView->widget.topPad = UI_HSTEP;
	}

	ui_WidgetAddChild(UI_WIDGET(pEditor), UI_WIDGET(pText));
	ui_WidgetAddChild(UI_WIDGET(pEditor), UI_WIDGET(pView));

	pText->widget.inputF = ui_SMFEditorTextAreaInput;
	ui_TextAreaSetChangedCallback(pText, SetTextFromTextArea, pEditor);
	pEditor->pTextArea = pText;
	pEditor->pView = pView;
}

UITextArea *ui_SMFEditorGetTextArea(UISMFEditor *pEditor)
{
	return pEditor->pTextArea;
}

bool ui_SMFEditorTextAreaInput(UITextArea *pText, KeyInput *pKey)
{
	if (pKey->attrib & KIA_CONTROL)
	{
		switch (pKey->scancode)
		{
		case INP_B:
			SurroundSelection(pText, "<b>", "</b>");
			break;
		case INP_I:
			SurroundSelection(pText, "<i>", "</i>");
			break;
		case INP_UP:
			SurroundSelection(pText, "<scale 1.2>", "</scale>");
			break;
		case INP_DOWN:
			SurroundSelection(pText, "<scale 0.8>", "</scale>");
			break;
		default:
			return ui_TextAreaInput(pText, pKey);
		}
		return true;
	}
	else if (pKey->character == '\n' || pKey->character == '\r' || pKey->scancode == INP_RETURN)
	{
		ui_TextAreaDeleteSelection(pText);
		TextBuffer_InsertTextAtCursor(UI_EDITABLE(pText)->pBuffer, "<br>\n");
		return true;
	}
	else
		return ui_TextAreaInput(pText, pKey);
}

void ui_SMFEditorFree(UISMFEditor *pEditor)
{
	ui_PaneFreeInternal(&pEditor->pane);
}

void ui_SMFEditorSetText(UISMFEditor *pEditor, const char *pchText)
{
	ui_TextAreaSetText(pEditor->pTextArea, pchText);
}

void ui_SMFEditorSetTextAndCallback(UISMFEditor *pEditor, const char *pchText)
{
	ui_TextAreaSetTextAndCallback(pEditor->pTextArea, pchText);
}

void ui_SMFEditorSetChangedCallback(UISMFEditor *pEditor, UIActivationFunc changedF, UserData changedData)
{
	pEditor->changedF = changedF;
	pEditor->changedData = changedData;
}

AUTO_COMMAND ACMD_CLIENTONLY;
void SMFEditor(bool bVertical)
{
	UIWindow *pWindow = ui_WindowCreate("SMF Editor", 0, 0, 300, 400);
	UISMFEditor *pEditor = ui_SMFEditorCreate(bVertical);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pEditor), 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPadding(UI_WIDGET(pEditor), UI_HSTEP, UI_HSTEP);
	ui_WindowSetCloseCallback(pWindow, ui_WindowFreeOnClose, NULL);
	ui_WindowAddChild(pWindow, pEditor);
	ui_WindowAddToDevice(pWindow, NULL);
}
