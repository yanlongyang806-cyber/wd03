#include "inputMouse.h"

#include "GfxClipper.h"
#include "UIButton.h"
#include "UIDictionaryEntry.h"
#include "UITextEntry.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static void FinishedDictionaryAndCallback(UITextEntry *pEntry, UIDictionaryEntry *pDictEntry)
{
	if (pDictEntry->cbFinished)
		pDictEntry->cbFinished(pDictEntry, pDictEntry->pFinishedData);
}

static void ChangedDictionaryAndCallback(UITextEntry *pEntry, UIDictionaryEntry *pDictEntry)
{
	if (pDictEntry->cbChanged)
		pDictEntry->cbChanged(pDictEntry, pDictEntry->pChangedData);
}

static void EnterDictionaryAndCallback(UITextEntry *pEntry, UIDictionaryEntry *pDictEntry)
{
	if (pDictEntry->cbEnter)
		pDictEntry->cbEnter(pDictEntry, pDictEntry->pEnterData);
	ui_TextEntryDefaultEnterCallback(pEntry, NULL);
}

static void OpenEditor(UIButton *pButton, UIDictionaryEntry *pDictEntry)
{
	if (pDictEntry->cbOpen)
		pDictEntry->cbOpen(pDictEntry, pDictEntry->pOpenData);
}

static void ui_DictionaryEntryContextProxy(UITextEntry *pEntry, UIDictionaryEntry *pDictEntry)
{
	if (pDictEntry->widget.contextF)
		pDictEntry->widget.contextF(pDictEntry, pDictEntry->widget.contextData);	
}

UIDictionaryEntry *ui_DictionaryEntryCreate(const char *pchDictText, const char *pchDictHandleOrName, bool bFiltered, bool bOpenButton)
{
	UIDictionaryEntry *pDictEntry = calloc(1, sizeof(UIDictionaryEntry));
	pDictEntry->pchDictHandleOrName = pchDictHandleOrName;
	ui_WidgetInitialize(UI_WIDGET(pDictEntry), ui_DictionaryEntryTick, ui_DictionaryEntryDraw, ui_DictionaryEntryFreeInternal, NULL, NULL);

	pDictEntry->pEntry = ui_TextEntryCreateWithGlobalDictionaryCombo(pchDictText, 0, 0, pchDictHandleOrName, "resourceName", true, true, true, bFiltered);
	ui_TextEntrySetSelectOnFocus(pDictEntry->pEntry, true);

	ui_TextEntrySetFinishedCallback(pDictEntry->pEntry, FinishedDictionaryAndCallback, pDictEntry);
	ui_TextEntrySetChangedCallback(pDictEntry->pEntry, ChangedDictionaryAndCallback, pDictEntry);
	ui_TextEntrySetEnterCallback(pDictEntry->pEntry, EnterDictionaryAndCallback, pDictEntry);
	ui_WidgetSetContextCallback(UI_WIDGET(pDictEntry->pEntry), ui_DictionaryEntryContextProxy, pDictEntry);

	pDictEntry->cbOpen = NULL;
	pDictEntry->pOpenData = NULL;

	if(bOpenButton)
	{
		pDictEntry->pButton = ui_ButtonCreateImageOnly("button_center", 0, 0, OpenEditor, pDictEntry);
		ui_WidgetSetPositionEx(UI_WIDGET(pDictEntry->pButton), 0, 0, 0, 0, UITopRight);
		ui_WidgetSetWidth(UI_WIDGET(pDictEntry->pButton), 24);
		ui_WidgetSetHeightEx(UI_WIDGET(pDictEntry->pButton), 1.f, UIUnitPercentage);

		ui_WidgetSetWidth(UI_WIDGET(pDictEntry), ui_WidgetGetWidth(UI_WIDGET(pDictEntry->pEntry)) - 24);
	}
	else
	{
		pDictEntry->pButton = NULL;

		ui_WidgetSetWidth(UI_WIDGET(pDictEntry), ui_WidgetGetWidth(UI_WIDGET(pDictEntry->pEntry)));
	}

	ui_WidgetSetHeight(UI_WIDGET(pDictEntry), ui_WidgetGetHeight(UI_WIDGET(pDictEntry->pEntry)));
	
	ui_WidgetSetHeightEx(UI_WIDGET(pDictEntry->pEntry), 1.f, UIUnitPercentage);
	ui_WidgetSetWidthEx(UI_WIDGET(pDictEntry->pEntry), 1.f, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pDictEntry->pEntry), 0, pDictEntry->pButton ? UI_WIDGET(pDictEntry->pButton)->width : 0, 0, 0);
	ui_WidgetAddChild(UI_WIDGET(pDictEntry), UI_WIDGET(pDictEntry->pEntry));

	if(pDictEntry->pButton)
	{
		ui_WidgetAddChild(UI_WIDGET(pDictEntry), UI_WIDGET(pDictEntry->pButton));

		ui_SetActive(UI_WIDGET(pDictEntry->pButton), false);
	}

	return pDictEntry;
}

void ui_DictionaryEntryFreeInternal(UIDictionaryEntry *pDictEntry)
{
	ui_WidgetFreeInternal(UI_WIDGET(pDictEntry));
}

void ui_DictionaryEntryTick(UIDictionaryEntry *pDictEntry, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pDictEntry);
	UI_TICK_EARLY(pDictEntry, true, true);
	UI_TICK_LATE(pDictEntry);
}

void ui_DictionaryEntryDraw(UIDictionaryEntry *pDictEntry, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pDictEntry);
	UI_DRAW_EARLY(pDictEntry);

	// Make sure changed/inherited flags move into text entry
	ui_SetChanged(UI_WIDGET(pDictEntry->pEntry), ui_IsChanged(UI_WIDGET(pDictEntry)));
	ui_SetInherited(UI_WIDGET(pDictEntry->pEntry), ui_IsInherited(UI_WIDGET(pDictEntry)));

	UI_DRAW_LATE(pDictEntry);
}

const char *ui_DictionaryEntryGetText(UIDictionaryEntry *pDictEntry)
{
	return ui_TextEntryGetText(pDictEntry->pEntry);
}

void ui_DictionaryEntrySetText(UIDictionaryEntry *pDictEntry, const char *pchDictText)
{
	ui_TextEntrySetText(pDictEntry->pEntry, pchDictText);
}

const char *ui_DictionaryEntryGetDictionaryNameOrHandle(UIDictionaryEntry *pDictEntry)
{
	return pDictEntry->pchDictHandleOrName;
}

void ui_DictionaryEntrySetFileNameAndCallback(UIDictionaryEntry *pDictEntry, const char *pchDictText)
{
	ui_TextEntrySetTextAndCallback(pDictEntry->pEntry, pchDictText);
}

void ui_DictionaryEntrySetChangedCallback(UIDictionaryEntry *pDictEntry, UIActivationFunc cbChanged, UserData pChangedData)
{
	pDictEntry->cbChanged = cbChanged;
	pDictEntry->pChangedData = pChangedData;
}

void ui_DictionaryEntrySetFinishedCallback(UIDictionaryEntry *pDictEntry, UIActivationFunc cbFinished, UserData pFinishedData)
{
	pDictEntry->cbFinished = cbFinished;
	pDictEntry->pFinishedData = pFinishedData;
}

void ui_DictionaryEntrySetEnterCallback(UIDictionaryEntry *pDictEntry, UIActivationFunc cbEnter, UserData pEnterData)
{
	pDictEntry->cbEnter = cbEnter;
	pDictEntry->pEnterData = pEnterData;
}

void ui_DictionaryEntrySetOpenCallback(UIDictionaryEntry *pDictEntry, UIActivationFunc cbOpen, UserData pOpenData)
{
	if(pDictEntry->pButton)
	{
		pDictEntry->cbOpen = cbOpen;
		pDictEntry->pOpenData = pOpenData;

		ui_SetActive(UI_WIDGET(pDictEntry->pButton), !!pDictEntry->cbOpen);
	}
}
