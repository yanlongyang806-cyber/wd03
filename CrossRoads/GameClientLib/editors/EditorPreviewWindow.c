/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#ifndef NO_EDITORS

#include "EditorManager.h"
#include "EditorPreviewWindow.h"
#include "EditorSearchWindow.h"
#include "EditLibUIUtil.h"
#include "EditorManagerUtils.h"
#include "ResourceManagerUI.h"
#include "EString.h"
#include "Color.h"
#include "GFXSpriteText.h"
#include "GFXSprite.h"
#include "UIGimmeButton.h"
#include "GfxTexAtlas.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

typedef struct PreviewWindowData
{
	// Set up on initial create
	bool bInit;
	UIWindow *pWindow;
	UITabGroup *pTabGroup;

	UIComboBox *pSearchTypeCombo;
	UITextEntry *pSearchNameText;
	UIMenuButton *pMenuButton;

	// These are transient
	UITab *pSummaryTab;
	UITextEntry *pNameEntry;
	UITextEntry *pScopeEntry;
	UITextEntry *pLocationEntry;
	UITextEntry *pNotesEntry;
	UITextEntry *pDisplayNameEntry;
	UITextEntry *pTagsEntry;
	const char *pRequestDictName;

	bool bWindowVisible;
	const char *pDictName;
	const char *pResourceName;

} PreviewWindowData;

PreviewWindowData gPreviewWindow = {
	false,NULL,NULL,
	NULL,NULL,NULL,
	NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
	false,NULL,NULL
};

static bool previewWindowClose(UIWidget *widget, UserData unused)
{
	gPreviewWindow.bWindowVisible = false;
	return true;
}

#define PREVIEW_HEADER 25

static void previewWindowDrawTick(UIWindow *pWindow, UI_PARENT_ARGS)
{	
	bool bDrewPreview = false;
	ResourceInfo *pInfo = resGetInfo(gPreviewWindow.pDictName, gPreviewWindow.pResourceName);	
	UI_GET_COORDINATES( pWindow );
	UI_WINDOW_SANITY_CHECK(pWindow);
	
	ui_StyleFontUse(NULL, false, kWidgetModifier_None);
	gfxfont_SetColor(ColorBlack, ColorBlack);

	ui_WindowDraw(pWindow, UI_PARENT_VALUES);

	if (!pWindow->shaded)
	{
		y += PREVIEW_HEADER;
		h -= PREVIEW_HEADER;

		BuildCBox(&box, x + 5, y + 5, w/2 - 10, h - 10);
		display_sprite_box(g_ui_Tex.white, &box, z + 1, 0xFFFFFFFF);

		if (pInfo)
		{
			bDrewPreview = resDrawPreview(pInfo, "HeadshotStyle_PreviewNoFrame", x + 5, y + 5, w/2 - 10, h - 10, scale, z + 2, 255);
		}

		if (!bDrewPreview)
		{
			gfxfont_Printf(x + w/4, y + h/2, z + 2, scale, scale, CENTER_XY, "No Preview");
		}
	}
}

static void previewTextEnterCB(UITextEntry *entry, void *userData)
{
	PreviewResource(gPreviewWindow.pRequestDictName, ui_TextEntryGetText(entry));
}


static void previewTypeComboCB(UIComboBox *combo, void *unused)
{
	ResourceDictionaryInfo *pInfo = ui_ComboBoxGetSelectedObject(combo);
	if (!pInfo || !pInfo->pDictName)
	{
		ui_ComboBoxSetSelected(combo, -1);
		if (gPreviewWindow.pSearchNameText)
		{
			ui_WidgetQueueFree(UI_WIDGET(gPreviewWindow.pSearchNameText));
			gPreviewWindow.pSearchNameText = NULL;
		}
		gPreviewWindow.pRequestDictName = NULL;
	}
	else
	{
		UITextEntry *pNewNameEntry;
		gPreviewWindow.pRequestDictName = pInfo->pDictName;

		pNewNameEntry = ui_TextEntryCreateWithGlobalDictionaryCombo(gPreviewWindow.pResourceName, 130, 0,
			gPreviewWindow.pRequestDictName, "resourceName", true, true, false, true);

		if (gPreviewWindow.pSearchNameText)
		{
			ui_WidgetQueueFree(UI_WIDGET(gPreviewWindow.pSearchNameText));			
		}
		gPreviewWindow.pSearchNameText = pNewNameEntry;
		ui_WidgetSetDimensions(UI_WIDGET(gPreviewWindow.pSearchNameText), 250, 22);
		ui_TextEntrySetEnterCallback(gPreviewWindow.pSearchNameText, previewTextEnterCB, NULL);
		ui_WindowAddChild(gPreviewWindow.pWindow, UI_WIDGET(gPreviewWindow.pSearchNameText));
	}
}


static void searchContext_Edit(UIMenuItem *pItem, void *userData)
{
	if (gPreviewWindow.pDictName && gPreviewWindow.pResourceName)
	{
		emOpenFileEx(gPreviewWindow.pResourceName, gPreviewWindow.pDictName);
	}
}

static void searchContext_FindUsage(UIMenuItem *pItem, void *userData)
{
	if (gPreviewWindow.pDictName && gPreviewWindow.pResourceName)
	{
		RequestUsageSearch(gPreviewWindow.pDictName, gPreviewWindow.pResourceName);
	}
}

static void searchContext_ListReferences(UIMenuItem *pItem, void *userData)
{
	if (gPreviewWindow.pDictName && gPreviewWindow.pResourceName)
	{
		RequestReferencesSearch(gPreviewWindow.pDictName, gPreviewWindow.pResourceName);
	}
}

static void searchContext_OpenFile(UIMenuItem *pItem, void *userData)
{
	ResourceInfo *pInfo = resGetInfo(gPreviewWindow.pDictName, gPreviewWindow.pResourceName);
	if(pInfo) {
		emuOpenFile(pInfo->resourceLocation);

	}
}

static void searchContext_OpenFolder(UIMenuItem *pItem, void *userData)
{
	ResourceInfo *pInfo = resGetInfo(gPreviewWindow.pDictName, gPreviewWindow.pResourceName);
	if(pInfo) {
		emuOpenContainingDirectory(pInfo->resourceLocation);
	}
}

static void previewInitWindow(void)
{
	if (gPreviewWindow.bInit)
	{
		return;
	}

	gPreviewWindow.pWindow = ui_WindowCreate("Preview Window", 0, 0, 800, 500);
	ui_WindowSetDimensions(gPreviewWindow.pWindow, 800, 400, 500, 400);
	elUICenterWindow(gPreviewWindow.pWindow);
	ui_WindowSetCloseCallback(gPreviewWindow.pWindow, previewWindowClose, NULL);
	UI_WIDGET(gPreviewWindow.pWindow)->drawF = previewWindowDrawTick;

	gPreviewWindow.pTabGroup = ui_TabGroupCreate(0,0,0,0);
	ui_WidgetSetDimensionsEx(UI_WIDGET(gPreviewWindow.pTabGroup), 0.5, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPositionEx(UI_WIDGET(gPreviewWindow.pTabGroup), 0, 0, .5, 0, UITopLeft);
	ui_WidgetSetPaddingEx(UI_WIDGET(gPreviewWindow.pTabGroup), 0, 0, PREVIEW_HEADER, 0);
	gPreviewWindow.pTabGroup->eStyle = UITabStyleFoldersWithBorder;
	ui_WindowAddChild(gPreviewWindow.pWindow, UI_WIDGET(gPreviewWindow.pTabGroup));

	gPreviewWindow.pSearchTypeCombo = resCreateDictionaryComboBox(0, 0, 120);	
	ui_ComboBoxSetSelectedCallback(gPreviewWindow.pSearchTypeCombo, previewTypeComboCB, NULL);		
	ui_ComboBoxSetDefaultDisplayString(gPreviewWindow.pSearchTypeCombo, "Pick Dictionary");
	ui_WindowAddChild(gPreviewWindow.pWindow, UI_WIDGET(gPreviewWindow.pSearchTypeCombo));

	gPreviewWindow.pMenuButton = ui_MenuButtonCreateWithItems(0,0,
		ui_MenuItemCreate("Open in Editor",UIMenuCallback, searchContext_Edit, NULL, NULL),
		ui_MenuItemCreate("Find Usage",UIMenuCallback, searchContext_FindUsage, NULL, NULL),
		ui_MenuItemCreate("List References",UIMenuCallback, searchContext_ListReferences, NULL, NULL),
		ui_MenuItemCreate("", UIMenuSeparator,NULL,NULL,NULL),
		ui_MenuItemCreate("Open File", UIMenuCallback, searchContext_OpenFile, NULL, NULL),
		ui_MenuItemCreate("Open Folder", UIMenuCallback, searchContext_OpenFolder, NULL, NULL),
		NULL);
	ui_WidgetSetPositionEx(UI_WIDGET(gPreviewWindow.pMenuButton), 0, 0, 0, 0, UITopRight);
	ui_SetActive(UI_WIDGET(gPreviewWindow.pMenuButton), false);
	ui_WindowAddChild(gPreviewWindow.pWindow, UI_WIDGET(gPreviewWindow.pMenuButton));

	gPreviewWindow.bInit = true;

}

void ShowPreviewWindow(bool bShow)
{
	previewInitWindow();
	gPreviewWindow.bWindowVisible = bShow;

	if (bShow)
	{
		ui_WindowPresent(gPreviewWindow.pWindow);
	}
	else
	{
		ui_WindowHide(gPreviewWindow.pWindow);
	}
}

bool CheckPreviewWindow(void)
{
	return gPreviewWindow.bWindowVisible;
}

bool *GetPreviewWindowStatus(void)
{
	return &gPreviewWindow.bWindowVisible;
}

static void previewSetSMFText(UISMFView *pView, ResourceInfo *pInfo)
{
	static char *pSMF;
	unsigned int oldSize;
	estrClear(&pSMF);
	oldSize = estrLength(&pSMF);
	if (pInfo)
	{
		if (pInfo->resourceName && pInfo->resourceName[0])
		{
			estrConcatf(&pSMF, "<b>Name:</b> %s<BR>", pInfo->resourceName);
		}
		if (pInfo->resourceDict && pInfo->resourceDict[0])
		{
			estrConcatf(&pSMF, "<b>Type:</b> %s<BR>", pInfo->resourceDict);
		}
		if (pInfo->resourceScope && pInfo->resourceScope[0])
		{
			estrConcatf(&pSMF, "<b>Scope:</b> %s<BR>", pInfo->resourceScope);
		}
		if (pInfo->resourceDisplayName && pInfo->resourceDisplayName[0])
		{
			estrConcatf(&pSMF, "<b>DisplayName:</b> %s<BR>", pInfo->resourceDisplayName);
		}
		if (pInfo->resourceTags && pInfo->resourceTags[0])
		{
			estrConcatf(&pSMF, "<b>Tags:</b> %s<BR>", pInfo->resourceTags);
		}
		if (pInfo->resourceNotes && pInfo->resourceNotes[0])
		{
			estrConcatf(&pSMF, "<b>Notes: </b>%s<BR>", pInfo->resourceNotes);
		}
		if (pInfo->resourceLocation && pInfo->resourceLocation[0])
		{
			estrConcatf(&pSMF, "<b>Location: </b>%s<BR>", pInfo->resourceLocation);
		}
	}
	if (estrLength(&pSMF) == oldSize)
	{
		estrPrintf(&pSMF, "No Info");
	}
	ui_SMFViewSetText(pView, pSMF, NULL);
}

static void EditButtonTick(UIButton *pEditButton, UI_PARENT_ARGS)
{
	if (resIsWritable(gPreviewWindow.pDictName, gPreviewWindow.pResourceName))
	{
		ui_SetActive(UI_WIDGET(pEditButton), true);
	}
	else
	{
		ui_SetActive(UI_WIDGET(pEditButton), false);
	}
	ui_ButtonTick(pEditButton, UI_PARENT_VALUES);
}

static bool TagEditDialogCallback(UIDialog *pDialog, UIDialogButton eButton, void *userData)
{
	ResourceInfo *pInfo = resGetInfo(gPreviewWindow.pDictName, gPreviewWindow.pResourceName);
	if (!pInfo)
	{
		return true;
	}
	if (eButton == kUIDialogButton_Ok)
	{	
		resCallback_HandleSimpleEdit *cb = resGetSimpleEditCB(pInfo->resourceDict, kResEditType_EditTags);
		cb(kResEditType_EditTags, pInfo, ui_TextEntryGetText((UITextEntry *)pDialog->pExtraWidget));
	}
	return true;
}

static void TagEditButtonHit(UITextEntry *entry, void *userData)
{
	char temp[1024];
	UIDialog *pDialog;
	ResourceInfo *pInfo = resGetInfo(gPreviewWindow.pDictName, gPreviewWindow.pResourceName);
	UITextEntry *pTagEdit;
	static const char **ppSearchTags;
	if (!pInfo)
	{
		return;
	}
	ppSearchTags = resGetValidTags(gPreviewWindow.pDictName);
	if (eaSize(&ppSearchTags))
	{
		pTagEdit = ui_TextEntryCreateWithStringMultiCombo(pInfo->resourceTags, 0, 0, 
			&ppSearchTags, true, true, false, false);
	}
	else
	{
		pTagEdit = ui_TextEntryCreate(pInfo->resourceTags, 0, 0);
	}
	ui_WidgetSetDimensionsEx(UI_WIDGET(pTagEdit), 200, 20, UIUnitFixed, UIUnitFixed);
	sprintf(temp,"Modify the tags for object %s %s", resDictGetItemDisplayName(gPreviewWindow.pDictName), gPreviewWindow.pResourceName);

	pDialog = ui_DialogCreateEx("Edit Tags", temp, TagEditDialogCallback, NULL, UI_WIDGET(pTagEdit), 
		"Save", kUIDialogButton_Ok, "Cancel", kUIDialogButton_Cancel, NULL);

	ui_WindowShow(UI_WINDOW(pDialog));
}

static void RefreshPreviewWindow(void)
{
	ResourceInfo *pInfo = resGetInfo(gPreviewWindow.pDictName, gPreviewWindow.pResourceName);
	if (gPreviewWindow.pSummaryTab)
	{
		ui_TabFree(gPreviewWindow.pSummaryTab);
		gPreviewWindow.pSummaryTab = NULL;
	}

	gPreviewWindow.pNameEntry = gPreviewWindow.pScopeEntry = gPreviewWindow.pLocationEntry = 
		gPreviewWindow.pNotesEntry = gPreviewWindow.pDisplayNameEntry = gPreviewWindow.pTagsEntry = NULL;

	if (pInfo)
	{		
		char windowTitle[1024];
		UILabel *pLabel;
		F32 y = 5;
		
		gPreviewWindow.pSummaryTab = ui_TabCreate("Summary");
		ui_TabGroupAddTab(gPreviewWindow.pTabGroup, gPreviewWindow.pSummaryTab);
#pragma warning(suppress:6001) // don't want to disable this, but cannot figure out how to properly clean up the warning: "Using uninitialized memory '*gPreviewWindow[24]'"
		ui_TabGroupSetActive(gPreviewWindow.pTabGroup, gPreviewWindow.pSummaryTab);		

		sprintf(windowTitle, "Preview: %s %s", resDictGetItemDisplayName(pInfo->resourceDict), pInfo->resourceName);
		ui_WindowSetTitle(gPreviewWindow.pWindow, windowTitle);

		pLabel = ui_LabelCreate("Name:", 0, y);
		ui_TabAddChild(gPreviewWindow.pSummaryTab, UI_WIDGET(pLabel));

		gPreviewWindow.pNameEntry = ui_TextEntryCreate(pInfo->resourceName, 120, y);
		ui_WidgetSetPaddingEx(UI_WIDGET(gPreviewWindow.pNameEntry), 0, 20, 0, 0);
		ui_WidgetSetDimensionsEx(UI_WIDGET(gPreviewWindow.pNameEntry), 1.0, 20, UIUnitPercentage, UIUnitFixed);
		ui_SetActive(UI_WIDGET(gPreviewWindow.pNameEntry), false);
		ui_TabAddChild(gPreviewWindow.pSummaryTab, gPreviewWindow.pNameEntry);
		y += 25;

		pLabel = ui_LabelCreate("Scope:", 0, y);
		ui_TabAddChild(gPreviewWindow.pSummaryTab, UI_WIDGET(pLabel));

		gPreviewWindow.pScopeEntry = ui_TextEntryCreate(pInfo->resourceScope, 120, y);
		ui_WidgetSetPaddingEx(UI_WIDGET(gPreviewWindow.pScopeEntry), 0, 20, 0, 0);
		ui_WidgetSetDimensionsEx(UI_WIDGET(gPreviewWindow.pScopeEntry), 1.0, 20, UIUnitPercentage, UIUnitFixed);
		ui_SetActive(UI_WIDGET(gPreviewWindow.pScopeEntry), false);
		ui_TabAddChild(gPreviewWindow.pSummaryTab, gPreviewWindow.pScopeEntry);
		y += 25;

		pLabel = ui_LabelCreate("Tags:", 0, y);
		ui_TabAddChild(gPreviewWindow.pSummaryTab, UI_WIDGET(pLabel));

		gPreviewWindow.pTagsEntry = ui_TextEntryCreate(pInfo->resourceTags, 120, y);
		ui_WidgetSetPaddingEx(UI_WIDGET(gPreviewWindow.pTagsEntry), 0, 20, 0, 0);
		ui_WidgetSetDimensionsEx(UI_WIDGET(gPreviewWindow.pTagsEntry), 1.0, 20, UIUnitPercentage, UIUnitFixed);
		ui_SetActive(UI_WIDGET(gPreviewWindow.pTagsEntry), false);
		ui_TabAddChild(gPreviewWindow.pSummaryTab, gPreviewWindow.pTagsEntry);

		if (resGetSimpleEditCB(pInfo->resourceDict, kResEditType_EditTags))
		{
			UIButton *pButton = ui_ButtonCreate("...", 0, 0, TagEditButtonHit, NULL);
			ui_WidgetSetDimensionsEx(UI_WIDGET(pButton), 20, 20, UIUnitFixed, UIUnitFixed);			
			ui_WidgetSetPositionEx(UI_WIDGET(pButton), 0, y, 0, 0, UITopRight);
			UI_WIDGET(pButton)->tickF = EditButtonTick;
			ui_TabAddChild(gPreviewWindow.pSummaryTab, pButton);
		}
		y += 25;

		pLabel = ui_LabelCreate("Display Name:", 0, y);
		ui_TabAddChild(gPreviewWindow.pSummaryTab, UI_WIDGET(pLabel));

		gPreviewWindow.pDisplayNameEntry = ui_TextEntryCreate(pInfo->resourceDisplayName, 120, y);
		ui_WidgetSetPaddingEx(UI_WIDGET(gPreviewWindow.pDisplayNameEntry), 0, 20, 0, 0);
		ui_WidgetSetDimensionsEx(UI_WIDGET(gPreviewWindow.pDisplayNameEntry), 1.0, 20, UIUnitPercentage, UIUnitFixed);
		ui_SetActive(UI_WIDGET(gPreviewWindow.pDisplayNameEntry), false);
		ui_TabAddChild(gPreviewWindow.pSummaryTab, gPreviewWindow.pDisplayNameEntry);
		y += 25;

		pLabel = ui_LabelCreate("Location:", 0, y);
		ui_TabAddChild(gPreviewWindow.pSummaryTab, UI_WIDGET(pLabel));

		gPreviewWindow.pLocationEntry = ui_TextEntryCreate(pInfo->resourceLocation, 120, y);
		ui_WidgetSetPaddingEx(UI_WIDGET(gPreviewWindow.pLocationEntry), 0, 20, 0, 0);
		ui_WidgetSetDimensionsEx(UI_WIDGET(gPreviewWindow.pLocationEntry), 1.0, 20, UIUnitPercentage, UIUnitFixed);
		ui_SetActive(UI_WIDGET(gPreviewWindow.pLocationEntry), false);
		ui_TabAddChild(gPreviewWindow.pSummaryTab, gPreviewWindow.pLocationEntry);

		if (resGetSimpleEditCB(pInfo->resourceDict, kResEditType_EditTags))
		{
			UIGimmeButton *pButton = ui_GimmeButtonCreate(0, 0, pInfo->resourceDict, pInfo->resourceName, NULL);
			ui_WidgetSetDimensionsEx(UI_WIDGET(pButton), 20, 20, UIUnitFixed, UIUnitFixed);			
			ui_WidgetSetPositionEx(UI_WIDGET(pButton), 0, y, 0, 0, UITopRight);
			ui_TabAddChild(gPreviewWindow.pSummaryTab, pButton);
		}
		y += 25;

		pLabel = ui_LabelCreate("Notes:", 0, y);
		ui_TabAddChild(gPreviewWindow.pSummaryTab, UI_WIDGET(pLabel));

		gPreviewWindow.pNotesEntry = ui_TextEntryCreate(pInfo->resourceNotes, 120, y);
		ui_WidgetSetPaddingEx(UI_WIDGET(gPreviewWindow.pNotesEntry), 0, 20, 0, 0);
		ui_WidgetSetDimensionsEx(UI_WIDGET(gPreviewWindow.pNotesEntry), 1.0, 20, UIUnitPercentage, UIUnitFixed);
		ui_SetActive(UI_WIDGET(gPreviewWindow.pNotesEntry), false);
		ui_TabAddChild(gPreviewWindow.pSummaryTab, gPreviewWindow.pNotesEntry);
		y += 25;

		ui_SetActive(UI_WIDGET(gPreviewWindow.pMenuButton), true);	
	}
	else
	{
		ui_SetActive(UI_WIDGET(gPreviewWindow.pMenuButton), false);
	}

	resDictionaryComboSelectDictionary(gPreviewWindow.pSearchTypeCombo, gPreviewWindow.pDictName, true);

	if (gPreviewWindow.pSearchNameText)
	{
		ui_TextEntrySetText(gPreviewWindow.pSearchNameText, gPreviewWindow.pResourceName? gPreviewWindow.pResourceName : "");
	}
}

static void PreviewWindowChangeCB(enumResourceEventType eType, const char *pDictName, const char *name, void* pResource, void *userData)
{
	if (gPreviewWindow.pDictName && stricmp(gPreviewWindow.pDictName, pDictName) == 0 &&
		gPreviewWindow.pResourceName && stricmp(gPreviewWindow.pResourceName, name) == 0)
	{
		if ((eType == RESEVENT_RESOURCE_ADDED) ||
			(eType == RESEVENT_RESOURCE_MODIFIED) ||
			(eType == RESEVENT_RESOURCE_REMOVED) ||
			(eType == RESEVENT_INDEX_MODIFIED))
		{		
			RefreshPreviewWindow();
		}
	}
}

AUTO_COMMAND;
void PreviewResource(const char *pDictName, const char *pResourceName)
{
	ResourceInfo *pInfo = resGetInfo(pDictName, pResourceName);
	ShowPreviewWindow(true);

	if (gPreviewWindow.pDictName)
	{	
		resDictRemoveEventCallback(gPreviewWindow.pDictName, PreviewWindowChangeCB);
	}

	gPreviewWindow.pDictName = gPreviewWindow.pResourceName = NULL;

	if (pInfo)
	{
		resDictRegisterEventCallback(pInfo->resourceDict, PreviewWindowChangeCB, NULL);
		gPreviewWindow.pDictName = pInfo->resourceDict;
		gPreviewWindow.pResourceName = pInfo->resourceName;
	}

	RefreshPreviewWindow();
	
}

#endif
