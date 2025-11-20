#include "inputMouse.h"

#include "GfxClipper.h"

#include "UIFileNameEntry.h"
#include "UIButton.h"
#include "UITextEntry.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static void FileNamePickerSelected(const char *pchDir, const char *pchFile, UIFileNameEntry *pFNEntry)
{
	char buf[MAX_PATH];
	sprintf(buf,"%s/%s", pchDir, pchFile);
	ui_TextEntrySetTextAndCallback(pFNEntry->pEntry, buf);
	ui_WidgetQueueFree(UI_WIDGET(pFNEntry->pFileBrowser));
	pFNEntry->pFileBrowser = NULL;
}

static void FileNamePickerCancel(UIFileNameEntry *pFNEntry)
{
	ui_WidgetQueueFree(UI_WIDGET(pFNEntry->pFileBrowser));
	pFNEntry->pFileBrowser = NULL;
}

static void OpenFileNamePicker(UIButton *pButton, UIFileNameEntry *pFNEntry)
{
	if (!pFNEntry->pFileBrowser)
		pFNEntry->pFileBrowser = ui_FileBrowserCreate(pFNEntry->pchBrowseTitle,
					"Select", pFNEntry->eMode, UIBrowseFiles, false, pFNEntry->pchTopDir,
					pFNEntry->pchStartDir, NULL, pFNEntry->pchDefaultExt, FileNamePickerCancel,
					pFNEntry, FileNamePickerSelected, pFNEntry);
	ui_WindowShow(pFNEntry->pFileBrowser);
}

static void UpdateFileNameAndCallback(UITextEntry *pEntry, UIFileNameEntry *pFNEntry)
{
	if (pFNEntry->cbChanged)
		pFNEntry->cbChanged(pFNEntry, pFNEntry->pChangedData);
}

void ui_FileNameEntrySetBrowseValues(SA_PARAM_NN_VALID UIFileNameEntry *pFNEntry, char *pchBrowseTitle, char *pchTopDir, char *pchStartDir, char *pchDefaultExt, UIBrowserMode eMode)
{
	SAFE_FREE(pFNEntry->pchBrowseTitle);
	SAFE_FREE(pFNEntry->pchTopDir);
	SAFE_FREE(pFNEntry->pchStartDir);
	SAFE_FREE(pFNEntry->pchDefaultExt);

	if (pchBrowseTitle)
		pFNEntry->pchBrowseTitle = strdup(pchBrowseTitle);
	else
		pFNEntry->pchBrowseTitle = strdup("Select File");
	if (pchTopDir)
		pFNEntry->pchTopDir = strdup(pchTopDir);
	if (pchStartDir)
		pFNEntry->pchStartDir = strdup(pchStartDir);
	if (pchDefaultExt)
		pFNEntry->pchDefaultExt = strdup(pchDefaultExt);
	pFNEntry->eMode = eMode;
}

static void ui_FileNameEntryContextProxy(UITextEntry *pEntry, UIFileNameEntry *pFNEntry)
{
	if (pFNEntry->widget.contextF)
		pFNEntry->widget.contextF(pFNEntry, pFNEntry->widget.contextData);	
}

UIFileNameEntry *ui_FileNameEntryCreate(const char *pchFileName, char *pchBrowseTitle, char *pchTopDir, char *pchStartDir, char *pchDefaultExt, UIBrowserMode eMode)
{
	UIFileNameEntry *pFNEntry = calloc(1, sizeof(UIFileNameEntry));
	F32 fHeight = 0;
	ui_FileNameEntrySetBrowseValues(pFNEntry, pchBrowseTitle, pchTopDir, pchStartDir, pchDefaultExt, eMode);
	ui_WidgetInitialize(UI_WIDGET(pFNEntry), ui_FileNameEntryTick, ui_FileNameEntryDraw, ui_FileNameEntryFreeInternal, NULL, NULL);
	pFNEntry->pEntry = ui_TextEntryCreate(pchFileName, 0, 0);
	pFNEntry->pButton = ui_ButtonCreate("...", 0, 0, OpenFileNamePicker, pFNEntry);

	ui_TextEntrySetChangedCallback(pFNEntry->pEntry, UpdateFileNameAndCallback, pFNEntry);
	ui_WidgetSetContextCallback(UI_WIDGET(pFNEntry->pEntry), ui_FileNameEntryContextProxy, pFNEntry);

	ui_WidgetSetPositionEx(UI_WIDGET(pFNEntry->pButton), 0, 0, 0, 0, UITopRight);
	MAX1(fHeight, UI_WIDGET(pFNEntry)->height);
	MAX1(fHeight, UI_WIDGET(pFNEntry->pButton)->height);
	ui_WidgetSetHeight(UI_WIDGET(pFNEntry), fHeight);
	ui_WidgetSetHeightEx(UI_WIDGET(pFNEntry->pButton), 1.f, UIUnitPercentage);
	ui_WidgetSetHeightEx(UI_WIDGET(pFNEntry->pEntry), 1.f, UIUnitPercentage);
	ui_WidgetSetWidthEx(UI_WIDGET(pFNEntry->pEntry), 1.f, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pFNEntry->pEntry), 0, UI_WIDGET(pFNEntry->pButton)->width, 0, 0);
	ui_WidgetAddChild(UI_WIDGET(pFNEntry), UI_WIDGET(pFNEntry->pEntry));
	ui_WidgetAddChild(UI_WIDGET(pFNEntry), UI_WIDGET(pFNEntry->pButton));
	return pFNEntry;
}

void ui_FileNameEntryFreeInternal(UIFileNameEntry *pFNEntry)
{
	SAFE_FREE(pFNEntry->pchBrowseTitle);
	SAFE_FREE(pFNEntry->pchTopDir);
	SAFE_FREE(pFNEntry->pchStartDir);
	SAFE_FREE(pFNEntry->pchDefaultExt);
	ui_WidgetFreeInternal(UI_WIDGET(pFNEntry));
}

void ui_FileNameEntryTick(UIFileNameEntry *pFNEntry, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pFNEntry);
	UI_TICK_EARLY(pFNEntry, true, true);
	UI_TICK_LATE(pFNEntry);
}

void ui_FileNameEntryDraw(UIFileNameEntry *pFNEntry, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pFNEntry);
	UI_DRAW_EARLY(pFNEntry);

	// Make sure changed/inherited flags move into text entry
	ui_SetChanged(UI_WIDGET(pFNEntry->pEntry), ui_IsChanged(UI_WIDGET(pFNEntry)));
	ui_SetInherited(UI_WIDGET(pFNEntry->pEntry), ui_IsInherited(UI_WIDGET(pFNEntry)));

	UI_DRAW_LATE(pFNEntry);
}

const char *ui_FileNameEntryGetFileName(UIFileNameEntry *pFNEntry)
{
	return ui_TextEntryGetText(pFNEntry->pEntry);
}

void ui_FileNameEntrySetFileName(UIFileNameEntry *pFNEntry, const char *pchFileName)
{
	ui_TextEntrySetText(pFNEntry->pEntry, pchFileName);
}

void ui_FileNameEntrySetFileNameAndCallback(UIFileNameEntry *pFNEntry, const char *pchFileName)
{
	ui_TextEntrySetTextAndCallback(pFNEntry->pEntry, pchFileName);
}

void ui_FileNameEntrySetChangedCallback(UIFileNameEntry *pFNEntry, UIActivationFunc cbChanged, UserData pChangedData)
{
	pFNEntry->cbChanged = cbChanged;
	pFNEntry->pChangedData = pChangedData;
}

