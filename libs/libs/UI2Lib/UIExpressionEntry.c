#include "inputMouse.h"

#include "ExpressionEditor.h"

#include "Expression.h"
#include "GfxClipper.h"
#include "UIButton.h"
#include "UIExpressionEntry.h"
#include "UITextEntry.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static void FinishExpressionAndCallback(UITextEntry *pEntry, UIExpressionEntry *pExprEntry)
{
	if (pExprEntry->cbChanged)
		pExprEntry->cbChanged(pExprEntry, pExprEntry->pChangedData);
}

static void EnterExpressionAndCallback(UITextEntry *pEntry, UIExpressionEntry *pExprEntry)
{
	if (pExprEntry->cbEnter)
		pExprEntry->cbEnter(pExprEntry, pExprEntry->pEnterData);
	ui_TextEntryDefaultEnterCallback(pEntry, NULL);
}

static void ExpressionEditorApply(Expression *pExpr, UIExpressionEntry *pExprEntry)
{
	char *exprString = pExpr ? exprGetCompleteString(pExpr) : NULL;

	// Get out the string
	if (exprString && exprString[0]) {
		ui_TextEntrySetTextAndCallback(pExprEntry->pEntry, exprGetCompleteString(pExpr));
	} else {
		ui_TextEntrySetTextAndCallback(pExprEntry->pEntry, "");
	}

	// Make sure apply callback gets called
	FinishExpressionAndCallback(pExprEntry->pEntry, pExprEntry);
}

static void OpenExpressionEditor(UIButton *pButton, UIExpressionEntry *pExprEntry)
{
	// Create temp expression to launch the editor
	Expression *pExpr = exprCreateFromString(ui_TextEntryGetText(pExprEntry->pEntry), NULL);

	pExprEntry->pExprEditor = exprEdOpen(ExpressionEditorApply, pExpr, pExprEntry, pExprEntry->pContext, -1);

	exprDestroy(pExpr);
}

static void ui_ExpressionEntryContextProxy(UITextEntry *pEntry, UIExpressionEntry *pExprEntry)
{
	if (pExprEntry->widget.contextF)
		pExprEntry->widget.contextF(pExprEntry, pExprEntry->widget.contextData);	
}

UIExpressionEntry *ui_ExpressionEntryCreate(const char *pExprText, ExprContext *pContext)
{
	UIExpressionEntry *pExprEntry = calloc(1, sizeof(UIExpressionEntry));
	F32 fHeight = 0;
	ui_WidgetInitialize(UI_WIDGET(pExprEntry), ui_ExpressionEntryTick, ui_ExpressionEntryDraw, ui_ExpressionEntryFreeInternal, NULL, NULL);
	pExprEntry->pEntry = ui_TextEntryCreateWithTextArea(pExprText, 0, 0);
	pExprEntry->pButton = ui_ButtonCreate("...", 0, 0, OpenExpressionEditor, pExprEntry);
	pExprEntry->pContext = pContext;

	ui_TextEntrySetFinishedCallback(pExprEntry->pEntry, FinishExpressionAndCallback, pExprEntry);
	ui_TextEntrySetEnterCallback(pExprEntry->pEntry, EnterExpressionAndCallback, pExprEntry);
	ui_WidgetSetContextCallback(UI_WIDGET(pExprEntry->pEntry), ui_ExpressionEntryContextProxy, pExprEntry);

	ui_WidgetSetPositionEx(UI_WIDGET(pExprEntry->pButton), 0, 0, 0, 0, UITopRight);
	MAX1(fHeight, UI_WIDGET(pExprEntry)->height);
	MAX1(fHeight, UI_WIDGET(pExprEntry->pButton)->height);
	ui_WidgetSetHeight(UI_WIDGET(pExprEntry), fHeight);
	ui_WidgetSetHeightEx(UI_WIDGET(pExprEntry->pButton), 1.f, UIUnitPercentage);
	ui_WidgetSetHeightEx(UI_WIDGET(pExprEntry->pEntry), 1.f, UIUnitPercentage);
	ui_WidgetSetWidthEx(UI_WIDGET(pExprEntry->pEntry), 1.f, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pExprEntry->pEntry), 0, UI_WIDGET(pExprEntry->pButton)->width, 0, 0);
	ui_WidgetAddChild(UI_WIDGET(pExprEntry), UI_WIDGET(pExprEntry->pEntry));
	ui_WidgetAddChild(UI_WIDGET(pExprEntry), UI_WIDGET(pExprEntry->pButton));
	return pExprEntry;
}

void ui_ExpressionEntryFreeInternal(UIExpressionEntry *pExprEntry)
{
	ui_WidgetFreeInternal(UI_WIDGET(pExprEntry));
}

void ui_ExpressionEntryTick(UIExpressionEntry *pExprEntry, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pExprEntry);
	UI_TICK_EARLY(pExprEntry, true, true);
	UI_TICK_LATE(pExprEntry);
}

void ui_ExpressionEntryDraw(UIExpressionEntry *pExprEntry, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pExprEntry);
	UI_DRAW_EARLY(pExprEntry);

	// Make sure changed/inherited flags move into text entry
	ui_SetChanged(UI_WIDGET(pExprEntry->pEntry), ui_IsChanged(UI_WIDGET(pExprEntry)));
	ui_SetInherited(UI_WIDGET(pExprEntry->pEntry), ui_IsInherited(UI_WIDGET(pExprEntry)));

	UI_DRAW_LATE(pExprEntry);
}

const char *ui_ExpressionEntryGetText(UIExpressionEntry *pExprEntry)
{
	return ui_TextEntryGetText(pExprEntry->pEntry);
}

void ui_ExpressionEntrySetText(UIExpressionEntry *pExprEntry, const char *pchExprText)
{
	ui_TextEntrySetText(pExprEntry->pEntry, pchExprText);
}

void ui_ExpressionEntrySetFileNameAndCallback(UIExpressionEntry *pExprEntry, const char *pchExprText)
{
	ui_TextEntrySetTextAndCallback(pExprEntry->pEntry, pchExprText);
}

void ui_ExpressionEntrySetChangedCallback(UIExpressionEntry *pExprEntry, UIActivationFunc cbChanged, UserData pChangedData)
{
	pExprEntry->cbChanged = cbChanged;
	pExprEntry->pChangedData = pChangedData;
}

void ui_ExpressionEntrySetEnterCallback(UIExpressionEntry *pExprEntry, UIActivationFunc cbEnter, UserData pEnterData)
{
	pExprEntry->cbEnter = cbEnter;
	pExprEntry->pEnterData = pEnterData;
}