
#include "UIButton.h"
#include "UIDialog.h"
#include "UILabel.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

typedef struct UIDialogButtonData
{
	UIDialog *pDialog;
	UIDialogButton eButton;
} UIDialogButtonData;

static bool ui_DialogCloseCallback(UIWidget *pWidget, UIDialogButtonData *pData)
{
	if (pData->pDialog->cbResponse)
	{
		bool bDone = pData->pDialog->cbResponse(pData->pDialog, pData->eButton, pData->pDialog->pResponseData);
		if (bDone)
			ui_WidgetDestroy((UIAnyWidget *)(&pData->pDialog));
		return bDone;
	}
	return true;
}

static void ui_DialogButtonCallback(UIWidget *pWidget, UIDialogButtonData *pData)
{
	if (pData->pDialog->cbResponse)
	{
		if (pData->pDialog->cbResponse(pData->pDialog, pData->eButton, pData->pDialog->pResponseData))
			ui_WidgetDestroy((UIAnyWidget *)(&pData->pDialog));
	}
	else
		ui_WidgetDestroy((UIAnyWidget *)(&pData->pDialog));
}

static void ui_DialogButtonFree(UIButton *pButton)
{
	SAFE_FREE(pButton->clickedData);
}

static void ui_DialogFreeInternal(UIDialog *pDialog)
{
	SAFE_FREE(UI_WINDOW(pDialog)->closeData);
	ui_WindowFreeInternal(UI_WINDOW(pDialog));
}

#define UI_DEFAULT_DIALOG_WIDTH 350
#define UI_DIALOG_BUTTON_MIN_WIDTH 80

static F32 ui_DialogAddButton(UIDialog *pDialog, const char *pchText, UIDialogButton eResponse, F32 *pfMaxHeight, F32 fX, bool bCenter)
{
	UIDialogButtonData *pData = calloc(1, sizeof(UIDialogButtonData));
	UIButton *pButton = ui_ButtonCreate(pchText, 0, 0, ui_DialogButtonCallback, pData);
	pData->eButton = eResponse;
	pData->pDialog = pDialog;
	ui_WidgetSetFreeCallback(UI_WIDGET(pButton), ui_DialogButtonFree);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), fX, 0, 0, 0, bCenter ? UIBottom : UIBottomRight);
	ui_WidgetSetWidth(UI_WIDGET(pButton), max(UI_WIDGET(pButton)->width, UI_DIALOG_BUTTON_MIN_WIDTH));
	fX += UI_WIDGET(pButton)->width + UI_STEP;
	*pfMaxHeight = max(*pfMaxHeight, UI_WIDGET(pButton)->height);
	ui_WindowAddChild(UI_WINDOW(pDialog), pButton);
	return fX;
}

static F32 ui_DialogAddButtonMsg(UIDialog *pDialog, const char *pchText, UIDialogButton eResponse, F32 *pfMaxHeight, F32 fX, bool bCenter)
{
	UIDialogButtonData *pData = calloc(1, sizeof(UIDialogButtonData));
	UIButton *pButton = ui_ButtonCreate(NULL, 0, 0, ui_DialogButtonCallback, pData);
	ui_WidgetSetTextMessage( UI_WIDGET( pButton ), pchText );
	ui_ButtonResize( pButton );
	pData->eButton = eResponse;
	pData->pDialog = pDialog;
	ui_WidgetSetFreeCallback(UI_WIDGET(pButton), ui_DialogButtonFree);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), fX, 0, 0, 0, bCenter ? UIBottom : UIBottomRight);
	ui_WidgetSetWidth(UI_WIDGET(pButton), max(UI_WIDGET(pButton)->width, UI_DIALOG_BUTTON_MIN_WIDTH));
	fX += UI_WIDGET(pButton)->width + UI_STEP;
	*pfMaxHeight = max(*pfMaxHeight, UI_WIDGET(pButton)->height);
	ui_WindowAddChild(UI_WINDOW(pDialog), pButton);
	return fX;
}

UIDialog *ui_DialogCreateEx(const char *pchTitle, const char *pchText, UIDialogResponseCallback cbResponse, UserData pResponseData, UIWidget *pExtraWidget, ...)
{
	UIDialog *pDialog = calloc(1, sizeof(UIDialog));
	const char *pchButtonText = NULL;
	S32 iButtonCount = 0;
	UILabel *pLabel = ui_LabelCreate(pchText, 0, 0);
	F32 fButtonHeight = 0.f;
	F32 fWidth;
	F32 fX = 0.f;
	F32 fExtraHeight = 0.0;
	va_list va;

	ui_WindowInitializeEx(UI_WINDOW(pDialog), pchTitle ? pchTitle : "", 0, 0, 0, 0 MEM_DBG_PARMS_INIT);
	UI_WIDGET(pDialog)->freeF = ui_DialogFreeInternal;

	pDialog->cbResponse = cbResponse;
	pDialog->pResponseData = pResponseData;

	va_start(va, pExtraWidget);
	while ( (pchButtonText = va_arg(va, const char *)) )
	{
		UIDialogButton eResponse = va_arg(va, UIDialogButton);
		UIButton *pButton = NULL;
		if (devassertmsg(eResponse, "Trying to create a UIDialog with an incomplete button definition"))
			fX = ui_DialogAddButton(pDialog, pchButtonText, eResponse, &fButtonHeight, fX, false);
		else
			break;
	}
	va_end(va);

	if (fX == 0)
		fX = ui_DialogAddButton(pDialog, "OK", kUIDialogButton_Ok, &fButtonHeight, fX, true);

	fWidth = max(UI_DEFAULT_DIALOG_WIDTH, fX);

	ui_LabelSetWordWrap(pLabel, true);
	ui_LabelUpdateDimensionsForWidth(pLabel, fWidth);

	if (pExtraWidget)
	{
		ui_WidgetSetPositionEx(pExtraWidget, 0, fButtonHeight + UI_DSTEP, 0, 0, UIBottom);
		fExtraHeight = ui_WidgetGetHeight(pExtraWidget) + UI_DSTEP;
		ui_WindowAddChild(UI_WINDOW(pDialog), pExtraWidget);
		pDialog->pExtraWidget = pExtraWidget;
	}
	ui_WindowSetDimensions(UI_WINDOW(pDialog), fWidth, fButtonHeight + fExtraHeight +  UI_WIDGET(pLabel)->height + UI_DSTEP,
		fWidth, fButtonHeight + UI_WIDGET(pLabel)->height + UI_DSTEP);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pLabel), 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);
	ui_WindowAddChild(UI_WINDOW(pDialog), pLabel);

	{
		UIDialogButtonData *pData = calloc(1, sizeof(UIDialogButtonData));
		pData->eButton = kUIDialogButton_Close;
		pData->pDialog = pDialog;
		ui_WindowSetCloseCallback(UI_WINDOW(pDialog), ui_DialogCloseCallback, pData);
	}

	ui_WindowSetModal(UI_WINDOW(pDialog), true);

	ui_WidgetSetPosition(UI_WIDGET(pDialog), (g_ui_State.screenWidth - UI_WIDGET(pDialog)->width) / 2, 
		(g_ui_State.screenHeight - UI_WIDGET(pDialog)->height) / 2);
	return pDialog;
}

UIDialog *ui_DialogCreateMsgEx(const char *pchTitle, const char *pchText, UIDialogResponseCallback cbResponse, UserData pResponseData, UIWidget *pExtraWidget, ...)
{
	UIDialog *pDialog = calloc(1, sizeof(UIDialog));
	const char *pchButtonText = NULL;
	S32 iButtonCount = 0;
	UILabel *pLabel = ui_LabelCreate(NULL, 0, 0);
	F32 fButtonHeight = 0.f;
	F32 fWidth;
	F32 fX = 0.f;
	F32 fExtraHeight = 0.0;
	va_list va;
	ui_WidgetSetTextMessage( UI_WIDGET( pLabel ), pchText );

	ui_WindowInitializeEx(UI_WINDOW(pDialog), "", 0, 0, 0, 0 MEM_DBG_PARMS_INIT);
	ui_WidgetSetTextMessage( UI_WIDGET( pDialog ), pchTitle );
	UI_WIDGET(pDialog)->freeF = ui_DialogFreeInternal;

	pDialog->cbResponse = cbResponse;
	pDialog->pResponseData = pResponseData;

	va_start(va, pExtraWidget);
	while ( (pchButtonText = va_arg(va, const char *)) )
	{
		UIDialogButton eResponse = va_arg(va, UIDialogButton);
		UIButton *pButton = NULL;
		if (devassertmsg(eResponse, "Trying to create a UIDialog with an incomplete button definition"))
			fX = ui_DialogAddButtonMsg(pDialog, pchButtonText, eResponse, &fButtonHeight, fX, false);
		else
			break;
	}
	va_end(va);

	fWidth = max(UI_DEFAULT_DIALOG_WIDTH, fX);

	ui_LabelSetWordWrap(pLabel, true);
	ui_LabelUpdateDimensionsForWidth(pLabel, fWidth);

	if (pExtraWidget)
	{
		ui_WidgetSetPositionEx(pExtraWidget, 0, fButtonHeight + UI_DSTEP, 0, 0, UIBottom);
		fExtraHeight = ui_WidgetGetHeight(pExtraWidget) + UI_DSTEP;
		ui_WindowAddChild(UI_WINDOW(pDialog), pExtraWidget);
		pDialog->pExtraWidget = pExtraWidget;
	}
	ui_WindowSetDimensions(UI_WINDOW(pDialog), fWidth, fButtonHeight + fExtraHeight +  UI_WIDGET(pLabel)->height + UI_DSTEP,
		fWidth, fButtonHeight + UI_WIDGET(pLabel)->height + UI_DSTEP);
	ui_WidgetSetDimensionsEx(UI_WIDGET(pLabel), 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);
	ui_WindowAddChild(UI_WINDOW(pDialog), pLabel);

	{
		UIDialogButtonData *pData = calloc(1, sizeof(UIDialogButtonData));
		pData->eButton = kUIDialogButton_Close;
		pData->pDialog = pDialog;
		ui_WindowSetCloseCallback(UI_WINDOW(pDialog), ui_DialogCloseCallback, pData);
	}

	ui_WindowSetModal(UI_WINDOW(pDialog), true);

	ui_WidgetSetPosition(UI_WIDGET(pDialog), (g_ui_State.screenWidth - UI_WIDGET(pDialog)->width) / 2, 
		(g_ui_State.screenHeight - UI_WIDGET(pDialog)->height) / 2);
	return pDialog;
}
