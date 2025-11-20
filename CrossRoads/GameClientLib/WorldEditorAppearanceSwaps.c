#ifndef NO_EDITORS

#include "WorldEditorAppearanceAttributes.h"
#include "WorldEditorAttributesPrivate.h"
#include "EditorManager.h"
#include "EditLibUIUtil.h"
#include "EString.h"
#include "GfxTexAtlas.h"
#include "GfxTextureTools.h"
#include "StringCache.h"
#include "MaterialEditor2EM.h" //< for the material previews

#include "TexWordsEditor.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#define ADD_CHILD(w) if (ui->panel) emPanelAddChild(ui->panel, w, false); else if(ui->tab) ui_TabAddChild(ui->tab, w); else ui_WidgetAddChild(ui->parent, &(w)->widget);
#define REMOVE_CHILD(w) if (ui->panel) emPanelRemoveChild(ui->panel, w, false); else if(ui->tab) ui_TabRemoveChild(ui->tab, w); else ui_WidgetRemoveChild(ui->parent, &(w)->widget);
#define wleQSort(x,y) if (x) eaQSort((*x->peaModel), y)

static LocalSwap *wleLocalSwapCreate(const char *origName, const char *replaceName)
{
	LocalSwap *locSwap = calloc(1, sizeof(LocalSwap));
	locSwap->origName = allocAddString(origName);
	if (replaceName)
		locSwap->replaceName = allocAddString(replaceName);
	else
		locSwap->replaceName = allocAddString("");

	return locSwap;
}

static void wleLocalSwapFree(LocalSwap *locSwap)
{
	free(locSwap);
}

/// A picker for material swaps.
///
/// TODO: Make its preview not be a generic material picker 
static EMPicker* wleMaterialSwapPicker = NULL;
static EMPicker* wleTextureSwapPicker = NULL;

/******
* This function frees all LocalSwaps set on a list.  This is generally called when refreshing the swap lists.
* PARAMS:
*   ui - WleAESwapsUI whose lists' swaps are to be freed
******/
void wleAESwapsFreeSwaps(WleAESwapUI *ui)
{
	if (ui->texSwapList)
		eaDestroyEx(ui->texSwapList->peaModel, wleLocalSwapFree);
	if (ui->matSwapList)
		eaDestroyEx(ui->matSwapList->peaModel, wleLocalSwapFree);
}

void wleAESwapsFreeData(WleAESwapUI *ui)
{
	wleAESwapsFreeSwaps(ui);

	if (ui->matRClickMenu)
		ui_WidgetQueueFree(UI_WIDGET(ui->matRClickMenu));
	if (ui->texRClickMenu)
		ui_WidgetQueueFree(UI_WIDGET(ui->texRClickMenu));
}

/********************
* UI CALLBACKS
********************/
// combo box callback
static void wleAESwapsSwapTypeSelected(UIComboBox *cb, WleAESwapUI *ui)
{
	const char *swapType = ui_ComboBoxGetSelectedObject(cb);

	if (!swapType)
		return;
	if (strcmpi(swapType, "Material Swaps") == 0)
	{
		REMOVE_CHILD(ui->texSwapList);
		ADD_CHILD(ui->matSwapList);
	}
	else
	{
		REMOVE_CHILD(ui->matSwapList);
		ADD_CHILD(ui->texSwapList);
	}
}

// clear all callback
static void wleAESwapsClearAllClicked(UIButton *button, WleAESwapUI *ui)
{
	const char **matSwaps = NULL;
	const char **texSwaps = NULL;
	const char *swapType = ui_ComboBoxGetSelectedObject(ui->swapType);
	int i;

	if (strcmpi(swapType, "Material Swaps") == 0)
	{
		for (i = 0; i < eaSize(ui->matSwapList->peaModel); i++)
		{
			const char *oldName = ((LocalSwap*)(*ui->matSwapList->peaModel)[i])->origName;
			eaPush(&matSwaps, oldName);
			eaPush(&matSwaps, allocAddString(""));
		}
	}
	else
	{
		for (i = 0; i < eaSize(ui->texSwapList->peaModel); i++)
		{
			const char *oldName = ((LocalSwap*)(*ui->texSwapList->peaModel)[i])->origName;
			eaPush(&texSwaps, oldName);
			eaPush(&texSwaps, allocAddString(""));
		}
	}

	ui->setSwapsF(texSwaps, matSwaps, ui->userData);
	eaDestroy(&matSwaps);
	eaDestroy(&texSwaps);
}

// browser callbacks
static bool wleAESwapsBrowseMatOk(EMPicker *picker, EMPickerSelection **filenames, WleAESwapUI *ui)
{
	LocalSwap *currSwap = (LocalSwap*) ui_ListGetSelectedObject(ui->matSwapList);
	char *matName;
	const char **materialSwaps = NULL;
	char buffer[MAX_PATH];
	bool ret;

	if (!currSwap || !eaSize(&filenames))
		return false;

	getFileNameNoExt(buffer, filenames[0]->doc_name);
	matName = strdup(buffer);
	eaPush(&materialSwaps, currSwap->origName);
	eaPush(&materialSwaps, allocAddString(matName));
	free(matName);
	ret = ui->setSwapsF(NULL, materialSwaps, ui->userData);
	eaDestroy(&materialSwaps);

	return ret;
}

static bool wleAESwapsBrowseTexOk1(const char* texName, WleAESwapUI *ui)
{
	LocalSwap *currSwap = (LocalSwap*) ui_ListGetSelectedObject(ui->texSwapList);
	const char **textureSwaps = NULL;
	bool ret;

	if (!currSwap)
		return false;

	eaPush(&textureSwaps, currSwap->origName);
	eaPush(&textureSwaps, allocAddString(texName));
	ret = ui->setSwapsF(textureSwaps, NULL, ui->userData);
	eaDestroy(&textureSwaps);

	return ret;
}

static bool wleAESwapsBrowseTexOk(EMPicker* picker, EMPickerSelection **texNames, WleAESwapUI *ui)
{
	char texName[ MAX_PATH ];

	if (!eaSize(&texNames))
		return false;

	getFileNameNoExt(texName, texNames[0]->doc_name);
	return wleAESwapsBrowseTexOk1( texName, ui );
}

// list callbacks
static void wleAESwapsDisplayOldSwapText(UIList *list, UIListColumn *col, int row, UserData unused, char **output)
{
	estrPrintf(output, "%s", ((LocalSwap*)(*list->peaModel)[row])->origName);
}

static void wleAESwapsDisplayNewSwapText(UIList *list, UIListColumn *col, int row, UserData unused, char **output)
{
	estrPrintf(output, "%s", ((LocalSwap*)(*list->peaModel)[row])->replaceName);
}

static bool asc = false;

static int wleAESwapsOldColCmp(const LocalSwap **swap1, const LocalSwap **swap2)
{
	if (asc)
		return strcmp((*swap1)->origName, (*swap2)->origName);
	else
		return strcmp((*swap2)->origName, (*swap1)->origName);
}

static int wleAESwapsNewColCmp(const LocalSwap **swap1, const LocalSwap **swap2)
{
	if (asc)
		return strcmp((*swap1)->replaceName, (*swap2)->replaceName);
	else
		return strcmp((*swap2)->replaceName, (*swap1)->replaceName);
}

static void wleAESwapsOldColClicked(UIListColumn *col, WleAESwapUI *ui)
{
	if (!ui->sortNewCol)
		ui->colAsc = !ui->colAsc;
	else
		ui->sortNewCol = false;
	asc = ui->colAsc;
	wleQSort(ui->texSwapList, wleAESwapsOldColCmp);
	wleQSort(ui->matSwapList, wleAESwapsOldColCmp);
}

static void wleAESwapsNewColClicked(UIListColumn *col, WleAESwapUI *ui)
{
	if (ui->sortNewCol)
		ui->colAsc = !ui->colAsc;
	else
		ui->sortNewCol = true;
	asc = ui->colAsc;
	wleQSort(ui->texSwapList, wleAESwapsNewColCmp);
	wleQSort(ui->matSwapList, wleAESwapsNewColCmp);
}

static void wleAESwapsMatListSelected(UIList *list, WleAESwapUI *ui)
{
	LocalSwap *swap = ui_ListGetSelectedObject(list);
	char tooltip[2048];

	if (!swap)
	{
		ui_WidgetSetTooltipString(UI_WIDGET(list), NULL);
		return;
	}

	sprintf(tooltip, "Orig: %s<br>Swap: %s", swap->origName, swap->replaceName && swap->replaceName[0] ? swap->replaceName : "[NONE]");
	ui_WidgetSetTooltipString(UI_WIDGET(list), tooltip);
}

static void wleAESwapsTexListSelected(UIList *list, WleAESwapUI *ui)
{
	LocalSwap *swap = ui_ListGetSelectedObject(list);
	AtlasTex *tex;
	char tooltip[2048];

	if (!swap)
	{
		ui_WidgetSetTooltipString(UI_WIDGET(list), NULL);
		return;
	}

	tex = atlasLoadTexture(swap->origName);
	if (!tex)
		return;

	sprintf(tooltip, "Orig: %s<br><img src=\"%s\" width=%i height=50><br>Swap: %s", swap->origName, swap->origName, tex->width * 50 / tex->height, swap->replaceName && swap->replaceName[0] ? swap->replaceName : "[NONE]");
	if (swap->replaceName && swap->replaceName[0])
	{
		tex = atlasLoadTexture(swap->replaceName);
		if (tex)
			strcatf(tooltip, "<br><img src=\"%s\" width=%i height=50>", swap->replaceName, tex->width * 50 / tex->height);
	}
	ui_WidgetSetTooltipString(UI_WIDGET(list), tooltip);
}

static char* wleMaterialNoTemplateFilter( const char* path )
{
	if( strstri( path, "/Templates/" )) {
		return NULL;
	} else {
		char buffer[ MAX_PATH ];

		getFileNameNoExt( buffer, path );
		return StructAllocString( buffer );
	}
}

static void wleAESwapsMatListActivate(UIWidget *unused, WleAESwapUI *ui)
{
	LocalSwap *swap;

	swap = ui_ListGetSelectedObject(ui->matSwapList);
	if (swap)
	{
		if( !wleMaterialSwapPicker ) {
			wleMaterialSwapPicker = emEasyPickerCreate( "Select New Material", ".Material", "materials/", wleMaterialNoTemplateFilter );
			emEasyPickerSetColorFunc( wleMaterialSwapPicker, mated2MaterialPickerColor );
			emEasyPickerSetTexFunc( wleMaterialSwapPicker, mated2MaterialPickerPreview );
		}
		emPickerShow( wleMaterialSwapPicker, "Set Swap", false, wleAESwapsBrowseMatOk, ui );
	}
}

static void wleAESwapsTexListActivate(UIWidget *unused, WleAESwapUI *ui)
{
	LocalSwap *swap;

	swap = ui_ListGetSelectedObject(ui->texSwapList);
	if (swap)
	{
		EMPicker* texPicker = emPickerGetByName( "Texture Picker" );
		if( texPicker )
			emPickerShow( texPicker, "Set Swap", false, wleAESwapsBrowseTexOk, ui );
	}
}

typedef struct SwapToTexWordData
{
	UIWindow *file_browser;
	const char *baseTexture;
	WleAESwapUI *ui;
} SwapToTexWordData;

static void texwordSwapSaveAsCancel(UserData userData)
{
	SwapToTexWordData *texword_swap_data = (SwapToTexWordData*)userData;
	ui_FileBrowserFree();
}

static bool texwordSwapSaveAsSelectFile(const char *dir, const char *fileName_in, UserData userData)
{
	bool bRet;
	SwapToTexWordData *texword_swap_data = (SwapToTexWordData*)userData;

	bRet = texWordsEdit_trySaveAsNewDynamic(dir, fileName_in, texword_swap_data->baseTexture);
	if (bRet)
	{
		char texName[MAX_PATH];
		getFileNameNoExt(texName, fileName_in);
		wleAESwapsBrowseTexOk1(texName, texword_swap_data->ui);
	}
	ui_FileBrowserFree();
	return bRet;
}

static void wleAESwapsTexSwapToTexWord(UIWidget *unused, WleAESwapUI *ui)
{
	LocalSwap *swap;

	swap = ui_ListGetSelectedObject(ui->texSwapList);
	if (swap)
	{
		const char *oldName = (swap->replaceName && swap->replaceName[0]) ? swap->replaceName : swap->origName;
		static SwapToTexWordData texword_swap_data;
		// Open a Save As dialog to choose a new location to save to, create the TexWord, set it as a swap
		texword_swap_data.ui = ui;
		texword_swap_data.baseTexture = oldName;
		texword_swap_data.file_browser = ui_FileBrowserCreate("Choose new file name", "Save As New Dynamic TexWord",
															  UIBrowseNewNoOverwrite, UIBrowseFiles, false,
															  "texts/English/texture_library/Dynamic", "texts/English/texture_library/Dynamic", NULL,
															  ".TexWord", texwordSwapSaveAsCancel, &texword_swap_data, texwordSwapSaveAsSelectFile, &texword_swap_data);
		ui_WindowShow(texword_swap_data.file_browser);
	}
}

static void wleAESwapsTexEditTexWord(UIWidget *unused, WleAESwapUI *ui)
{
	EditorObject *edObj = wleAEGetSelected();
	LocalSwap *swap;

	if (!edObj || !ui->bForWorldEditor)
		return;

	swap = ui_ListGetSelectedObject(ui->texSwapList);
	if (swap)
	{
		const char *oldName = (swap->replaceName && swap->replaceName[0]) ? swap->replaceName : swap->origName;
		if (!texWordFind(oldName, 0))
			return;

		emOpenFileEx(oldName, "TexWord");
	}
}

static void wleAESwapsMatSwapClear(UIMenuItem *item, WleAESwapUI *ui)
{
	LocalSwap *swap = ui_ListGetSelectedObject(ui->matSwapList);
	const char **matSwaps = NULL;

	if (!swap || (ui->bForWorldEditor && !wleAEGetSelected()))
		return;

	eaPush(&matSwaps, swap->origName);
	eaPush(&matSwaps, allocAddString(""));
	ui->setSwapsF(NULL, matSwaps, ui->userData);
	eaDestroy(&matSwaps);
}

static void wleAESwapsMatListRClick(UIList *list, WleAESwapUI *ui)
{
	if (!ui_ListGetSelectedObject(list))
		return;

	if (!ui->matRClickMenu)
	{
		ui->matRClickMenu = ui_MenuCreateWithItems(NULL,
							ui_MenuItemCreate("Set material swap...", UIMenuCallback, wleAESwapsMatListActivate, ui, NULL),
							ui_MenuItemCreate("Clear material swap", UIMenuCallback, wleAESwapsMatSwapClear, ui, NULL),
							NULL);
	}

	ui_MenuPopupAtCursor(ui->matRClickMenu);
}

static void wleAESwapsTexSwapClear(UIMenuItem *item, WleAESwapUI *ui)
{
	LocalSwap *swap = ui_ListGetSelectedObject(ui->texSwapList);
	const char **texSwaps = NULL;

	if (!swap || (ui->bForWorldEditor && !wleAEGetSelected()))
		return;

	eaPush(&texSwaps, swap->origName);
	eaPush(&texSwaps, allocAddString(""));
	ui->setSwapsF(texSwaps, NULL, ui->userData);
	eaDestroy(&texSwaps);
}

static void wleAESwapsTexListRClick(UIList *list, WleAESwapUI *ui)
{
	const char *oldName;
	LocalSwap *swap;

	if (!(swap = ui_ListGetSelectedObject(list)))
		return;

	if (!ui->texRClickMenu)
	{
		ui->texRClickMenu = ui_MenuCreateWithItems(NULL,
				ui_MenuItemCreate("Set texture swap...", UIMenuCallback, wleAESwapsTexListActivate, ui, NULL),
				(ui->item_clear = ui_MenuItemCreate("Clear texture swap", UIMenuCallback, wleAESwapsTexSwapClear, ui, NULL)),
				(ui->item_new_texword = ui_MenuItemCreate("Texture swap to new TexWord...", UIMenuCallback, wleAESwapsTexSwapToTexWord, ui, NULL)),
				(ui->item_edit_texword = ui_MenuItemCreate("Edit existing TexWord...", UIMenuCallback, wleAESwapsTexEditTexWord, ui, NULL)),
				NULL);
	}

	// Enable/disable appropriate items
	oldName = (swap->replaceName && swap->replaceName[0]) ? swap->replaceName : swap->origName;
	ui->item_clear->active = !!(swap->replaceName && swap->replaceName[0]);
	ui->item_edit_texword->active = ui->bForWorldEditor && !!texWordFind(oldName, 0);
	ui->item_new_texword->active = ui->bForWorldEditor && ((swap->replaceName && swap->replaceName[0]) ? !texWordFind(swap->replaceName, 0) : 1);
	
	ui_MenuPopupAtCursor(ui->texRClickMenu);
}

void wleAESwapsRebuildUI(WleAESwapUI *ui, StashTable materials, StashTable textures)
{
	StashTableIterator iter;
	StashElement el;

	// set the attribute editor lists to the stash contents
	wleAESwapsFreeSwaps(ui);
	if (ui->texSwapList) {
		stashGetIterator(textures, &iter);
		while (stashGetNextElement(&iter, &el))
			eaPush(ui->texSwapList->peaModel, wleLocalSwapCreate(stashElementGetKey(el), stashElementGetPointer(el)));
	}
	if (ui->matSwapList) {
		stashGetIterator(materials, &iter);
		while (stashGetNextElement(&iter, &el))
			eaPush(ui->matSwapList->peaModel, wleLocalSwapCreate(stashElementGetKey(el), stashElementGetPointer(el)));
	}

	// sort contents
	asc = ui->colAsc;
	if (ui->sortNewCol)
	{
		wleQSort(ui->texSwapList, wleAESwapsNewColCmp);
		wleQSort(ui->matSwapList, wleAESwapsNewColCmp);
	}
	else
	{
		wleQSort(ui->texSwapList, wleAESwapsOldColCmp);
		wleQSort(ui->matSwapList, wleAESwapsOldColCmp);
	}

	if (ui->matSwapList)
		wleAESwapsMatListSelected(ui->matSwapList, ui);
	if (ui->texSwapList)
		wleAESwapsTexListSelected(ui->texSwapList, ui);
}

F32 wleAESwapsUICreate(WleAESwapUI *ui, EMPanel *panel, UITab *tab, UIWidget *parent, WleAESwapFunc swapF, void *userData, wleSwapOptions swapOptions, bool bForWorldEditor, F32 x, F32 startY)
{
	UIButton *button;
	UIList *list = NULL;
	UIListColumn *col;
	UIComboBox *cb;

	ui->panel = panel;
	ui->tab = tab;
	ui->parent = parent;
	ui->bForWorldEditor = bForWorldEditor;
	ui->colAsc = true;

	assert((panel || tab || parent) && swapOptions);

	// Listing of combobox options should be in order of priority on what should appear first.
	// common swap UI
	if (swapOptions & MATERIAL_SWAP)
		eaPush(&ui->eaSwapType, "Material Swaps");
	if (swapOptions & TEXTURE_SWAP)
		eaPush(&ui->eaSwapType, "Texture Swaps");
	cb = ui_ComboBoxCreate(x, startY, 120, NULL, &ui->eaSwapType, NULL);
	ui_ComboBoxSetSelected(cb, 0);
	ui_ComboBoxSetSelectedCallback(cb, wleAESwapsSwapTypeSelected, ui);
	ADD_CHILD(cb);
	ui->swapType = cb;
	button = ui_ButtonCreate("Clear All", x, cb->widget.y, wleAESwapsClearAllClicked, ui);
	button->widget.offsetFrom = UITopRight;
	ADD_CHILD(button);

	// listing of options should be in reverse order of priority in terms of what should initially be displayed (so the opposite order of combobox options)

	// texture swapper
	if (swapOptions & TEXTURE_SWAP)
	{
		eaCreate(&ui->eaTexSwap);
		list = ui_ListCreate(NULL, &ui->eaTexSwap, 15);
		list->widget.x = x;
		list->widget.y = elUINextY(cb) + 5;
		ui_WidgetSetDimensionsEx((UIWidget*) list, 1, 150, UIUnitPercentage, UIUnitFixed);
		col = ui_ListColumnCreate(UIListTextCallback, "Old Texture", (intptr_t) wleAESwapsDisplayOldSwapText, NULL);
		ui_ListColumnSetClickedCallback(col, wleAESwapsOldColClicked, ui);
		col->fWidth = 150;
		ui_ListAppendColumn(list, col);
		col = ui_ListColumnCreate(UIListTextCallback, "New Texture", (intptr_t) wleAESwapsDisplayNewSwapText, NULL);
		ui_ListColumnSetClickedCallback(col, wleAESwapsNewColClicked, ui);
		col->fWidth = 150;
		ui_ListAppendColumn(list, col);
		ui_ListSetActivatedCallback(list, wleAESwapsTexListActivate, ui);
		ui_ListSetContextCallback(list, wleAESwapsTexListRClick, ui);
		ui_ListSetSelectedCallback(list, wleAESwapsTexListSelected, ui);
		ui->texSwapList = list;
	}

	// material swapper
	if (swapOptions & MATERIAL_SWAP)
	{
		eaCreate(&ui->eaMatSwap);
		list = ui_ListCreate(NULL, &ui->eaMatSwap, 15);
		list->widget.x = x;
		list->widget.y = elUINextY(cb) + 5;
		ui_WidgetSetDimensionsEx((UIWidget*) list, 1, 150, UIUnitPercentage, UIUnitFixed);
		col = ui_ListColumnCreate(UIListTextCallback, "Old Material", (intptr_t) wleAESwapsDisplayOldSwapText, NULL);
		ui_ListColumnSetClickedCallback(col, wleAESwapsOldColClicked, ui);
		col->fWidth = 150;
		ui_ListAppendColumn(list, col);
		col = ui_ListColumnCreate(UIListTextCallback, "New Material", (intptr_t) wleAESwapsDisplayNewSwapText, NULL);
		ui_ListColumnSetClickedCallback(col, wleAESwapsNewColClicked, ui);
		col->fWidth = 150;
		ui_ListAppendColumn(list, col);
		ui_ListSetActivatedCallback(list, wleAESwapsMatListActivate, ui);
		ui_ListSetContextCallback(list, wleAESwapsMatListRClick, ui);
		ui_ListSetSelectedCallback(list, wleAESwapsMatListSelected, ui);
		ui->matSwapList = list;
	}
	assert(list);
	ADD_CHILD(list);

	ui->setSwapsF = swapF;
	ui->userData = userData;

	return elUINextY(list);
}

#endif
