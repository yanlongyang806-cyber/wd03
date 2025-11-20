#include "WorldEditorOptions.h"
#include "WorldEditorPrivate.h"
#include "WorldEditorUI.h"
#include "WorldEditorMenus.h"
#include "EditorObjectMenus.h"
#include "EditLibUIUtil.h"
#include "EditorManager.h"
#include "EditorPrefs.h"
#include "WorldLib.h"
#include "WorldGrid.h"
#include "StaticWorld\WorldCellEntry.h"
#include "EString.h"

#include "EditorObjectMenus_h_ast.h"
#include "WorldEditorOptions_h_ast.h"

#ifndef NO_EDITORS

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

/********************
* GENERAL
********************/
typedef struct WleOptionsGeneralUI
{
	UICheckButton *showDefUIDs;
	UICheckButton *showHiddenTrackers;
	UICheckButton *showFrozenTrackers;
	UICheckButton *showSelectedGeo;
	UICheckButton *showSelectedTinted;
	UIColorButton *selectedTintColor;
	UICheckButton *showSelectedBounds;
	UICheckButton *showSelectedWireframe;

	UICheckButton *setWireframeDetail;
	UITextEntry *wireframeDetailVal;
	UISpinner *wireframeDetailSpinner;

	UITextEntry *quickPlaceDelay;
	UISpinner *quickPlaceDelaySpinner;

	UICheckButton *hideTooltips;
} WleOptionsGeneralUI;

static void wleOptionsGeneralRefresh(UIWidget *widget, WleOptionsGeneralUI *generalUI)
{
	int lightDetailVal = ui_SpinnerGetValue(generalUI->wireframeDetailSpinner);
	float quickPlaceDelayVal = ui_SpinnerGetValue(generalUI->quickPlaceDelaySpinner);
	bool showSelectedWireframe = ui_CheckButtonGetState(generalUI->showSelectedWireframe);
	bool autoSet = !showSelectedWireframe || ui_CheckButtonGetState(generalUI->setWireframeDetail);
	char text[16];

	ui_SetActive(UI_WIDGET(generalUI->setWireframeDetail), showSelectedWireframe);
	ui_SetActive(UI_WIDGET(generalUI->wireframeDetailVal), !autoSet);
	ui_SetActive(UI_WIDGET(generalUI->wireframeDetailSpinner), !autoSet);
	sprintf(text, "%i", autoSet ? 0 : lightDetailVal);
	ui_TextEntrySetText(generalUI->wireframeDetailVal, text);

	sprintf(text, "%.2f", quickPlaceDelayVal);
	ui_TextEntrySetText(generalUI->quickPlaceDelay, text);
}

static void wleOptionsGeneralTintColorValChanged( UIColorButton* colorButton, bool finished, WleOptionsGeneralUI *generalUI )
{
	Vec4 tintColor;
	ui_ColorButtonGetColor(colorButton, tintColor);
	worldSetSelectedTintColor(tintColor);
}

static void wleOptionsGeneralWireframeDetailValChanged(UITextEntry *entry, WleOptionsGeneralUI *generalUI)
{
	int val = atoi(ui_TextEntryGetText(entry));
	ui_SpinnerSetValue(generalUI->wireframeDetailSpinner, val);
	wleOptionsGeneralRefresh(NULL, generalUI);
}

static void wleOptionsGeneralQuickPlaceDelayValChanged(UITextEntry *entry, WleOptionsGeneralUI *generalUI)
{
	float val = atof(ui_TextEntryGetText(entry));
	ui_SpinnerSetValue(generalUI->quickPlaceDelaySpinner, val);
	wleOptionsGeneralRefresh(NULL, generalUI);
}

// Load function
static bool wleOptionsGeneralLoad(EMOptionsTab *emTab, UITab *tab)
{
	WleOptionsGeneralUI *generalUI = calloc(1, sizeof(*generalUI));
	UILabel *label;
	UICheckButton *checkButton;
	UITextEntry *entry;
	UISpinner *spinner;
	UIColorButton *colorButton;
	int spinVal;
	Vec4 prefTintColor;
	
	checkButton = ui_CheckButtonCreate(0, 5, "Show UIDs in tracker tree", EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "ShowDefUIDs", 0));
	ui_TabAddChild(tab, checkButton);
	generalUI->showDefUIDs = checkButton;
	checkButton = ui_CheckButtonCreate(0, elUINextY(checkButton), "Show hidden trackers in tracker tree", EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "ShowHiddenTrackers", 1));
	ui_TabAddChild(tab, checkButton);
	generalUI->showHiddenTrackers = checkButton;
	checkButton = ui_CheckButtonCreate(0, elUINextY(checkButton), "Show frozen trackers in tracker tree", EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "ShowFrozenTrackers", 1));
	ui_TabAddChild(tab, checkButton);
	generalUI->showFrozenTrackers = checkButton;
	checkButton = ui_CheckButtonCreate(0, elUINextY(checkButton), "Show geometry of selected objects", EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "ShowSelectedGeo", 1));
	ui_TabAddChild(tab, checkButton);
	generalUI->showSelectedGeo = checkButton;

	checkButton = ui_CheckButtonCreate(0, elUINextY(checkButton), "Show selected models tinted", EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "ShowSelectedTinted", 0));
	ui_CheckButtonSetToggledCallback(checkButton, wleOptionsGeneralRefresh, generalUI);
	ui_TabAddChild(tab, checkButton);
	generalUI->showSelectedTinted = checkButton;

	prefTintColor[0] = EditorPrefGetFloat(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "SelectedTintR", 1.0f);
	prefTintColor[1] = EditorPrefGetFloat(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "SelectedTintG", 0.0f);
	prefTintColor[2] = EditorPrefGetFloat(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "SelectedTintB", 0.0f);
	prefTintColor[3] = EditorPrefGetFloat(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "SelectedTintA", 0.8f);

	colorButton = ui_ColorButtonCreate(0, elUINextY(checkButton), prefTintColor);
	colorButton->liveUpdate = true;
	colorButton->bIsModal = true;
	strcpy(colorButton->title, "Selection Tint Color");
	ui_ColorButtonSetChangedCallback(colorButton, wleOptionsGeneralTintColorValChanged, generalUI);
	ui_WidgetSetWidth(UI_WIDGET(colorButton), 70);
	ui_TabAddChild(tab, colorButton);
	generalUI->selectedTintColor = colorButton;

	checkButton = ui_CheckButtonCreate(0, elUINextY(colorButton), "Show bounding box of selected objects", EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "ShowSelectedBounds", 0));
	ui_TabAddChild(tab, checkButton);
	generalUI->showSelectedBounds = checkButton;
	checkButton = ui_CheckButtonCreate(0, elUINextY(checkButton), "Show selected light, sound, and wind wireframes", EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "ShowSelectedWireframe", 1));
	ui_CheckButtonSetToggledCallback(checkButton, wleOptionsGeneralRefresh, generalUI);
	ui_TabAddChild(tab, checkButton);
	generalUI->showSelectedWireframe = checkButton;

	spinVal = EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "NumSegments", -1);
	label = ui_LabelCreate("Detail level", 20, elUINextY(checkButton));
	ui_TabAddChild(tab, label);
	checkButton = ui_CheckButtonCreate(elUINextX(label) + 5, label->widget.y, "Auto", spinVal == -1);
	ui_CheckButtonSetToggledCallback(checkButton, wleOptionsGeneralRefresh, generalUI);
	ui_TabAddChild(tab, checkButton);
	generalUI->setWireframeDetail = checkButton;
	entry = ui_TextEntryCreate("", elUINextX(checkButton) + 10, checkButton->widget.y);
	ui_TextEntrySetFinishedCallback(entry, wleOptionsGeneralWireframeDetailValChanged, generalUI);
	entry->widget.width = 100;
	ui_TextEntrySetValidateCallback(entry, ui_TextEntryValidationIntegerOnly, NULL);
	ui_TabAddChild(tab, entry);
	generalUI->wireframeDetailVal = entry;
	spinner = ui_SpinnerCreate(elUINextX(entry) + 5, checkButton->widget.y, 5, 30, 1, spinVal, wleOptionsGeneralRefresh, generalUI);
	ui_WidgetSetDimensions(UI_WIDGET(spinner), 15, entry->widget.height);
	ui_TabAddChild(tab, spinner);
	generalUI->wireframeDetailSpinner = spinner;

	spinVal = HARNESS_QUICKPLACE_TIME;
	label = ui_LabelCreate("Quick place click delay", 0, elUINextY(checkButton));
	ui_TabAddChild(tab, label);
	entry = ui_TextEntryCreate("", elUINextX(label) + 5, label->widget.y);
	entry->widget.width = 50;
	ui_TextEntrySetValidateCallback(entry, ui_TextEntryValidationFloatOnly, NULL);
	ui_TextEntrySetFinishedCallback(entry, wleOptionsGeneralQuickPlaceDelayValChanged, generalUI);
	ui_TabAddChild(tab, entry);
	generalUI->quickPlaceDelay = entry;
	spinner = ui_SpinnerCreate(elUINextX(entry) + 5, entry->widget.y, 0.1, 2.0, 0.01, spinVal, wleOptionsGeneralRefresh, generalUI);
	ui_WidgetSetDimensions(UI_WIDGET(spinner), 15, entry->widget.height);
	ui_TabAddChild(tab, spinner);
	generalUI->quickPlaceDelaySpinner = spinner;

	checkButton = ui_CheckButtonCreate(0, elUINextY(entry), "Hide Tooltips", EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "HideTooltips", 0));
	ui_TabAddChild(tab, checkButton);
	generalUI->hideTooltips = checkButton;

	wleOptionsGeneralRefresh(NULL, generalUI);

	emOptionsTabSetData(emTab, generalUI);
	return true;
}

// Cancel function
static void wleOptionsGeneralCancel(EMOptionsTab *emTab, WleOptionsGeneralUI *generalUI)
{
	free(generalUI);
}

// OK function
static void wleOptionsGeneralOk(EMOptionsTab *emTab, WleOptionsGeneralUI *generalUI)
{
	bool showSelectedGeo = ui_CheckButtonGetState(generalUI->showSelectedGeo);
	bool showSelectedTinted = ui_CheckButtonGetState(generalUI->showSelectedTinted);
	Vec4 selectedTintColor;
	ui_ColorButtonGetColor(generalUI->selectedTintColor, selectedTintColor);

	// set the options
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "ShowDefUIDs", ui_CheckButtonGetState(generalUI->showDefUIDs));
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "ShowHiddenTrackers", ui_CheckButtonGetState(generalUI->showHiddenTrackers));
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "ShowFrozenTrackers", ui_CheckButtonGetState(generalUI->showFrozenTrackers));
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "ShowSelectedGeo", showSelectedGeo);
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "ShowSelectedTinted", showSelectedTinted);
	EditorPrefStoreFloat(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "SelectedTintR", selectedTintColor[0]);
	EditorPrefStoreFloat(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "SelectedTintG", selectedTintColor[1]);
	EditorPrefStoreFloat(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "SelectedTintB", selectedTintColor[2]);
	EditorPrefStoreFloat(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "SelectedTintA", selectedTintColor[3]);
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "ShowSelectedBounds", ui_CheckButtonGetState(generalUI->showSelectedBounds));
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "ShowSelectedWireframe", ui_CheckButtonGetState(generalUI->showSelectedWireframe));


	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "NumSegments", ui_CheckButtonGetState(generalUI->setWireframeDetail) ? -1 : ui_SpinnerGetValue(generalUI->wireframeDetailSpinner));
	EditorPrefStoreFloat(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "QuickPlaceDelay", ui_SpinnerGetValue(generalUI->quickPlaceDelaySpinner));

	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "HideTooltips", ui_CheckButtonGetState(generalUI->hideTooltips));

	// apply the options

	worldSetSelectedWireframe(showSelectedGeo, showSelectedTinted);
	wleUITrackerTreeRefresh(NULL);
	wleOptionsGeneralCancel(emTab, generalUI);
}


/********************
* CUSTOM MENU
********************/
typedef struct WleOptionsCustomMenuUI
{
	UICheckButton *hideDisabled;
	UICheckButton *showContext;
	UIList *availCmds;
	UIList *selectCmds;
} WleOptionsCustomMenuUI;

static void wleOptionsCustomMenuListDisplay(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	UIMenuItem *item = emMenuItemGet(&worldEditor, ((char**) *(list->peaModel))[row]);
	if (!item)
		item = emMenuItemGet(NULL, ((char**) *(list->peaModel))[row]);
	if (!item)
		eaRemove(&((char**) *(list->peaModel)), row);
	else
		estrPrintf(output, "%s%s", ui_MenuItemGetText(item), item->type == UIMenuSubmenu ? " >" : "");
}

static int wleOptionsCustomMenuCompare(const char **item1, const char **item2)
{
	UIMenuItem *menuItem1 = emMenuItemGet(&worldEditor, *item1);
	UIMenuItem *menuItem2 = emMenuItemGet(&worldEditor, *item2);

	if (!menuItem1)
		menuItem1 = emMenuItemGet(NULL, *item1);
	if (!menuItem2)
		menuItem2 = emMenuItemGet(NULL, *item2);
	assert(menuItem1 && menuItem2);
	return strcmpi(ui_MenuItemGetText(menuItem1), ui_MenuItemGetText(menuItem2));
}

static void wleOptionsCustomMenuShuttleRight(UIButton *button, WleOptionsCustomMenuUI *customMenuUI)
{
	UIList *left = customMenuUI->availCmds;
	if (ui_ListGetSelectedRow(left) != -1)
	{
		UIList *right = customMenuUI->selectCmds;
		eaPush(right->peaModel, strdup(ui_ListGetSelectedObject(left)));
		ui_ListSetSelectedRowAndCallback(right, eaSize(right->peaModel) - 1);
	}
}

static void wleOptionsCustomMenuShuttleLeft(UIButton *button, WleOptionsCustomMenuUI *customMenuUI)
{
	UIList *right = customMenuUI->selectCmds;
	if (ui_ListGetSelectedRow(right) != -1)
	{
		char *str = eaRemove(right->peaModel, ui_ListGetSelectedRow(right));
		SAFE_FREE(str);
		ui_ListSetSelectedRowAndCallback(right, -1);
	}
}

static void wleOptionsCustomMenuMoveTop(UIButton *button, WleOptionsCustomMenuUI *customMenuUI)
{
	UIList *right = customMenuUI->selectCmds;
	if (ui_ListGetSelectedRow(right) != -1)
	{
		eaMove(right->peaModel, 0, ui_ListGetSelectedRow(right));
		ui_ListSetSelectedRowAndCallback(right, 0);
	}
}

static void wleOptionsCustomMenuMoveUp(UIButton *button, WleOptionsCustomMenuUI *customMenuUI)
{
	UIList *right = customMenuUI->selectCmds;
	if (ui_ListGetSelectedRow(right) != -1)
	{
		int pos = ui_ListGetSelectedRow(right);
		eaMove(right->peaModel, MAX(0, pos - 1), pos);
		ui_ListSetSelectedRowAndCallback(right, MAX(0, pos - 1));
	}
}

static void wleOptionsCustomMenuMoveDown(UIButton *button, WleOptionsCustomMenuUI *customMenuUI)
{
	UIList *right = customMenuUI->selectCmds;
	if (ui_ListGetSelectedRow(right) != -1)
	{
		int pos = ui_ListGetSelectedRow(right);
		int size = eaSize(right->peaModel) - 1;
		eaMove(right->peaModel, MIN(size, pos + 1), pos);
		ui_ListSetSelectedRowAndCallback(right, MIN(size, pos + 1));
	}
}

static void wleOptionsCustomMenuMoveBottom(UIButton *button, WleOptionsCustomMenuUI *customMenuUI)
{
	UIList *right = customMenuUI->selectCmds;
	if (ui_ListGetSelectedRow(right) != -1)
	{
		int size = eaSize(right->peaModel) - 1;
		eaMove(right->peaModel, size, ui_ListGetSelectedRow(right));
		ui_ListSetSelectedRowAndCallback(right, size);
	}
}

static void wleOptionsCustomMenuClear(UIButton *button, WleOptionsCustomMenuUI *customMenuUI)
{
	UIList *right = customMenuUI->selectCmds;
	eaClear(right->peaModel);
	ui_ListSetSelectedRowAndCallback(right, -1);
}

// Create function
static bool wleOptionsCustomMenuLoad(EMOptionsTab *emTab, UITab *tab)
{
	int i;
	UIList *list;
	UIListColumn *listCol;
	UIButton *button;
	UICheckButton *checkButton;
	char ***availCmds = calloc(1, sizeof(char**));
	char ***rightClickCmds = calloc(1, sizeof(char**));
	const int buttonSize = 20, smallButtonSize = 14;
	WleOptionsCustomMenuUI *customMenuUI = calloc(1, sizeof(*customMenuUI));
	StashTableIterator iter;
	StashElement el;

	// miscellaneous options
	checkButton = ui_CheckButtonCreate(0, 0, "Show context menu", false);
	ui_CheckButtonSetStateAndCallback(checkButton, EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "ShowContextMenu", 1));
	checkButton->widget.offsetFrom = UIBottomLeft;
	ui_TabAddChild(tab, checkButton);
	customMenuUI->showContext = checkButton;
	checkButton = ui_CheckButtonCreate(0, elUINextY(checkButton), "Hide disabled entries", false);
	ui_CheckButtonSetStateAndCallback(checkButton, EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "HideDisabledItems", 0));
	checkButton->widget.offsetFrom = UIBottomLeft;
	ui_TabAddChild(tab, checkButton);
	customMenuUI->hideDisabled = checkButton;

	// create the left-side list of the shuttle
	stashGetIterator(worldEditor.menu_items, &iter);
	while (stashGetNextElement(&iter, &el))
		eaPush(availCmds, strdup((char*) stashElementGetKey(el)));

	// make some of the editor manager menu items available for the custom menu
	eaPush(availCmds, strdup("em_save"));
	eaPush(availCmds, strdup("em_saveas"));
	eaPush(availCmds, strdup("em_undo"));
	eaPush(availCmds, strdup("em_redo"));
	eaPush(availCmds, strdup("em_paste"));
	eaPush(availCmds, strdup("em_documentation"));
	eaPush(availCmds, strdup("em_separator"));
	eaPush(availCmds, strdup("em_switchtogame"));
	eaPush(availCmds, strdup("em_switchtogameandspawn"));

	eaQSort(*availCmds, wleOptionsCustomMenuCompare);
	list = ui_ListCreate(NULL, availCmds, 15);
	ui_WidgetSetPositionEx((UIWidget*) list, 0, 0, 0, 0, UITopLeft);
	ui_WidgetSetDimensionsEx((UIWidget*) list, 0.4, 1, UIUnitPercentage, UIUnitPercentage);
	list->widget.topPad = 5;
	list->widget.bottomPad = elUINextY(checkButton);
	listCol = ui_ListColumnCreate(UIListTextCallback, "Available Commands", (intptr_t) wleOptionsCustomMenuListDisplay, NULL);
	ui_ListColumnSetWidth(listCol, true, 1);
	ui_ListAppendColumn(list, listCol);
	ui_TabAddChild(tab, list);
	customMenuUI->availCmds = list;

	// create the right-side list
	for (i = 0; i < eaSize(&wleMenuRightClick->menuItems); i++)
		eaPush(rightClickCmds, strdup(wleMenuRightClick->menuItems[i]));
	list = ui_ListCreate(NULL, rightClickCmds, 15);
	ui_WidgetSetPositionEx((UIWidget*) list, 0, 0, 0.5, 0, UITopLeft);
	ui_WidgetSetDimensionsEx((UIWidget*) list, 0.4, 1, UIUnitPercentage, UIUnitPercentage);
	list->widget.topPad = 5;
	listCol = ui_ListColumnCreate(UIListTextCallback, "Right-Click Menu", (intptr_t) wleOptionsCustomMenuListDisplay, NULL);
	ui_ListColumnSetWidth(listCol, true, 1);
	ui_ListAppendColumn(list, listCol);
	ui_TabAddChild(tab, list);
	customMenuUI->selectCmds = list;

	// shuttling buttons
	button = ui_ButtonCreateImageOnly("eui_arrow_large_right", 0, 0, wleOptionsCustomMenuShuttleRight, customMenuUI);
	ui_WidgetSetPositionEx((UIWidget*) button, -buttonSize / 2, -buttonSize, 0.45, 0.4, UITopLeft);
	ui_WidgetSetDimensions((UIWidget*) button, buttonSize, buttonSize);
	ui_ButtonSetImageStretch(button, true);
	ui_TabAddChild(tab, button);
	button = ui_ButtonCreateImageOnly("eui_arrow_large_left", 0, 0, wleOptionsCustomMenuShuttleLeft, customMenuUI);
	ui_WidgetSetPositionEx((UIWidget*) button, -buttonSize / 2, buttonSize, 0.45, 0.4, UITopLeft);
	ui_WidgetSetDimensions((UIWidget*) button, buttonSize, buttonSize);
	ui_ButtonSetImageStretch(button, true);
	ui_TabAddChild(tab, button);
	button = ui_ButtonCreate("Clear", 0, 0, wleOptionsCustomMenuClear, customMenuUI);
	ui_WidgetSetPositionEx((UIWidget*) button, 0, elUINextY(checkButton), 0.5, 0, UIBottomLeft);
	ui_TabAddChild(tab, button);
	list->widget.bottomPad = elUINextY(button) + 5;

	// reordering buttons
	button = ui_ButtonCreateImageOnly("eui_arrow_large_up", 0, 0, wleOptionsCustomMenuMoveTop, customMenuUI);
	ui_WidgetSetPositionEx((UIWidget*) button, -buttonSize / 2, -60, 0.95, 0.4, UITopLeft);
	ui_WidgetSetDimensions((UIWidget*) button, buttonSize, buttonSize);
	ui_ButtonSetImageStretch(button, true);
	ui_TabAddChild(tab, button);
	button = ui_ButtonCreateImageOnly("eui_arrow_large_up", 0, 0, wleOptionsCustomMenuMoveUp, customMenuUI);
	ui_WidgetSetPositionEx((UIWidget*) button, -smallButtonSize / 2, -27, 0.95, 0.4, UITopLeft);
	ui_WidgetSetDimensions((UIWidget*) button, smallButtonSize, smallButtonSize);
	ui_ButtonSetImageStretch(button, true);
	ui_TabAddChild(tab, button);
	button = ui_ButtonCreateImageOnly("eui_arrow_large_down", 0, 0, wleOptionsCustomMenuMoveDown, customMenuUI);
	ui_WidgetSetPositionEx((UIWidget*) button, -smallButtonSize / 2, 13, 0.95, 0.4, UITopLeft);
	ui_WidgetSetDimensions((UIWidget*) button, smallButtonSize, smallButtonSize);
	ui_ButtonSetImageStretch(button, true);
	ui_TabAddChild(tab, button);
	button = ui_ButtonCreateImageOnly("eui_arrow_large_down", 0, 0, wleOptionsCustomMenuMoveBottom, customMenuUI);
	ui_WidgetSetPositionEx((UIWidget*) button, -buttonSize / 2, 40, 0.95, 0.4, UITopLeft);
	ui_WidgetSetDimensions((UIWidget*) button, buttonSize, buttonSize);
	ui_ButtonSetImageStretch(button, true);
	ui_TabAddChild(tab, button);

	emOptionsTabSetData(emTab, customMenuUI);
	return true;
}

// Cancel function
static void wleOptionsCustomMenuCancel(EMOptionsTab *emTab, WleOptionsCustomMenuUI *customMenuUI)
{
	// destroy allocated memory
	eaDestroy(customMenuUI->selectCmds->peaModel);
	free(customMenuUI->selectCmds->peaModel);
	ui_ListSetModel(customMenuUI->selectCmds, NULL, NULL);
	eaDestroy(customMenuUI->availCmds->peaModel);
	free(customMenuUI->availCmds->peaModel);
	ui_ListSetModel(customMenuUI->availCmds, NULL, NULL);

	free(customMenuUI);
}

// OK function
static void wleOptionsCustomMenuOk(EMOptionsTab *emTab, WleOptionsCustomMenuUI *customMenuUI)
{
	// set the options
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "ShowContextMenu", ui_CheckButtonGetState(customMenuUI->showContext));
	EditorPrefStoreInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "HideDisabledItems", ui_CheckButtonGetState(customMenuUI->hideDisabled));
	edObjCustomMenuEdit(wleMenuRightClick, (char**)(*customMenuUI->selectCmds->peaModel));
	EditorPrefStoreStruct(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "CustomMenu", parse_EdObjCustomMenu, wleMenuRightClick);

	// clean up
	wleOptionsCustomMenuCancel(emTab, customMenuUI);
}

/********************
* FILTERS
********************/
// Criterion Registration
static StashTable wleAllCriteria;
static WleCriterion **wleAllCriteriaArray = NULL;

void wleCriterionRegister(WleCriterion *criterion)
{
	if (!wleAllCriteria)
		wleAllCriteria = stashTableCreateWithStringKeys(16, StashDefault);
	assert(stashAddPointer(wleAllCriteria, criterion->propertyName, criterion, false));
	eaPush(&wleAllCriteriaArray, criterion);
}

WleCriterion *wleCriterionGet(const char *propertyName)
{
	WleCriterion *crit = NULL;
	stashFindPointer(wleAllCriteria, propertyName, &crit);
	return crit;
}

// UI
typedef struct WleOptionsFiltersUI
{
	WleFilter **copiedFilters;

	UITab *parentTab;
	UIList *filterList;
	UIButton *deleteFilterButton;

	// filter widgets
	UIPane *filterPane;
	UITextEntry *nameEntry;
	UICheckButton *ignoreNodeState;
	UIComboBox *affectsCombo;
	UIList *criteriaList;
	UICheckButton *checkSpawnPoints;
	UICheckButton *checkVolumes;
	UICheckButton *checkInteractables;
	UICheckButton *checkLights;
	UICheckButton *checkRooms;
	UICheckButton *checkPatrols;
	UICheckButton *checkPatrolPoints;
	UICheckButton *checkEncounters;
	UICheckButton *checkActors;

	// criterion widgets
	UIButton *deleteCritButton;
	UIComboBox *critProperty;
	UIComboBox *critOp;
	UIWidget *critVal;
} WleOptionsFiltersUI;

StaticDefineInt WleCriterionConditionEnum[] =
{
	DEFINE_INT
	{"equals", WLE_CRIT_EQUAL},
	{"does not equal", WLE_CRIT_NOT_EQUAL},
	{"is less than", WLE_CRIT_LESS_THAN},
	{"is greater than", WLE_CRIT_GREATER_THAN},
	{"contains", WLE_CRIT_CONTAINS},
	{"begins with", WLE_CRIT_BEGINS_WITH},
	{"ends with", WLE_CRIT_ENDS_WITH},
	DEFINE_END
};

#define wleOptionsFilterCritValSetup(w) \
	w->y = filtersUI->critOp->widget.y;\
	w->width = 1;\
	w->widthUnit = UIUnitPercentage;\
	w->leftPad = elUINextX(filtersUI->critOp) + 5;\
	w->rightPad = 5;\
	ui_PaneAddChild(filtersUI->filterPane, w)

static void wleOptionsFilterCritDataChanged(UIWidget *unused, WleOptionsFiltersUI *filtersUI);

static void wleOptionsFilterCritListSelected(UIList *list, WleOptionsFiltersUI *filtersUI)
{
	WleFilterCriterion *criterion = ui_ListGetSelectedObject(filtersUI->criteriaList);

	if (filtersUI->critVal)
	{
		ui_PaneRemoveChild(filtersUI->filterPane, filtersUI->critVal);
		ui_WidgetQueueFree(filtersUI->critVal);
		filtersUI->critVal = NULL;
	}
	if (criterion)
	{
		const char ***model = (char***) filtersUI->critOp->model;
		int i;
		int selection = -1;

		// update property
		ui_ComboBoxSetSelectedObject(filtersUI->critProperty, criterion->criterion);

		// update op combo box model and selection
		if (!model)
		{
			model = calloc(1, sizeof(*model));
			ui_ComboBoxSetModelNoCallback(filtersUI->critOp, NULL, model);
		}
		eaClear(model);
		for (i = 0; i < eaiSize(&criterion->criterion->allConds); i++)
		{
			const char *opText = StaticDefineIntRevLookup(WleCriterionConditionEnum, criterion->criterion->allConds[i]);
			if (opText)
			{
				eaPush(model, opText);
				if (criterion->criterion->allConds[i] == criterion->cond)
					selection = eaSize(model) - 1;
			}
		}
		ui_ComboBoxSetSelected(filtersUI->critOp, selection);

		// update value widget
		if (eaSize(&criterion->criterion->possibleValues) > 0)
		{
			UIComboBox *combo = ui_ComboBoxCreate(0, 0, 0, NULL, &criterion->criterion->possibleValues, NULL);

			filtersUI->critVal = UI_WIDGET(combo);
			wleOptionsFilterCritValSetup(UI_WIDGET(combo));
			ui_ComboBoxSetSelectedCallback(combo, wleOptionsFilterCritDataChanged, filtersUI);
			for (i = 0; i < eaSize(&criterion->criterion->possibleValues); i++)
			{
				if (strcmpi(criterion->criterion->possibleValues[i], criterion->val) == 0)
					ui_ComboBoxSetSelected(combo, i);
			}
		}
		else
		{
			UITextEntry *entry = ui_TextEntryCreate("", 0, 0);
			filtersUI->critVal = UI_WIDGET(entry);
			wleOptionsFilterCritValSetup(UI_WIDGET(entry));
			ui_TextEntrySetFinishedCallback(entry, wleOptionsFilterCritDataChanged, filtersUI);
			ui_TextEntrySetText(entry, criterion->val);
		}
	}
	else
	{
		UITextEntry *entry = ui_TextEntryCreate("", 0, 0);

		ui_ComboBoxSetSelected(filtersUI->critProperty, -1);
		ui_ComboBoxSetSelected(filtersUI->critOp, -1);
		filtersUI->critVal = UI_WIDGET(entry);
		wleOptionsFilterCritValSetup(UI_WIDGET(entry));
		ui_TextEntrySetFinishedCallback(entry, wleOptionsFilterCritDataChanged, filtersUI);
		ui_TextEntrySetText(entry, "");
	}

	ui_SetActive(UI_WIDGET(filtersUI->critOp), !!criterion);
	ui_SetActive(filtersUI->critVal, !!criterion);
	ui_SetActive(UI_WIDGET(filtersUI->deleteCritButton), !!criterion);
}

static void wleOptionsFilterCritDataChanged(UIWidget *unused, WleOptionsFiltersUI *filtersUI)
{
	WleFilter *filter = ui_ListGetSelectedObject(filtersUI->filterList);
	WleFilterCriterion *criterion = ui_ListGetSelectedObject(filtersUI->criteriaList);
	WleCriterion *baseCrit = (WleCriterion*) ui_ComboBoxGetSelectedObject(filtersUI->critProperty);

	if (criterion)
	{
		const char *str = NULL;
		criterion->criterion = baseCrit;
		criterion->cond = StaticDefineIntGetInt(WleCriterionConditionEnum, ui_ComboBoxGetSelectedObject(filtersUI->critOp));
		StructFreeString(criterion->val);
		if (filtersUI->critVal->freeF == ui_TextEntryFreeInternal)
			str = ui_TextEntryGetText((UITextEntry*) filtersUI->critVal);
		else if (filtersUI->critVal->freeF == ui_ComboBoxFreeInternal)
			str = ui_ComboBoxGetSelectedObject((UIComboBox*) filtersUI->critVal);
		criterion->val = StructAllocString(str);
		wleOptionsFilterCritListSelected(NULL, filtersUI);
	}
	else if (filter && baseCrit)
	{
		WleFilterCriterion *newCriterion = StructCreate(parse_WleFilterCriterion);
		newCriterion->criterion = baseCrit;
		newCriterion->cond = baseCrit->allConds[0];
		newCriterion->val = StructAllocString("");
		eaPush(&filter->criteria, newCriterion);
		ui_ListSetSelectedObjectAndCallback(filtersUI->criteriaList, newCriterion);
	}
}

static void wleOptionsFilterCritNewClicked(UIButton *button, WleOptionsFiltersUI *filtersUI)
{
	ui_ListSetSelectedObjectAndCallback(filtersUI->criteriaList, NULL);
}

static void wleOptionsFilterCritDeleteClicked(UIButton *button, WleOptionsFiltersUI *filtersUI)
{
	WleFilter *filter = ui_ListGetSelectedObject(filtersUI->filterList);
	int criterion = ui_ListGetSelectedRow(filtersUI->criteriaList);
	if (filter && criterion >= 0)
	{
		StructDestroy(parse_WleFilterCriterion, eaRemove(&filter->criteria, criterion));
		wleOptionsFilterCritListSelected(NULL, filtersUI);
	}
}

static void wleOptionsFilterCriteriaAttributeText(UIList *list, UIListColumn *col, S32 row, UserData unused, char **output)
{
	if (list->peaModel)
	{
		WleFilterCriterion *criterion = (*list->peaModel)[row];
		estrPrintf(output, "%s", criterion->criterion->propertyName);
	}
}

static void wleOptionsFilterCriteriaOpText(UIList *list, UIListColumn *col, S32 row, UserData unused, char **output)
{
	if (list->peaModel)
	{
		WleFilterCriterion *criterion = (*list->peaModel)[row];
		estrPrintf(output, "%s", StaticDefineIntRevLookup(WleCriterionConditionEnum, criterion->cond));
	}
}

static void wleOptionsFilterCriteriaValueText(UIList *list, UIListColumn *col, S32 row, UserData unused, char **output)
{
	if (list->peaModel)
	{
		WleFilterCriterion *criterion = (*list->peaModel)[row];
		estrPrintf(output, "\"%s\"", criterion->val);
	}
}

static void wleOptionsFilterDataRefresh(WleOptionsFiltersUI *filtersUI)
{
	WleFilter *filter = ui_ListGetSelectedObject(filtersUI->filterList);
	if (filter)
	{
		// set filter data onto widgets
		ui_TextEntrySetText(filtersUI->nameEntry, filter->name);
		ui_CheckButtonSetState(filtersUI->ignoreNodeState, filter->ignoreNodeState);
		ui_ComboBoxSetSelected(filtersUI->affectsCombo, filter->affectType);

		ui_SetActive(UI_WIDGET(filtersUI->checkInteractables), !!filter->affectType);
		ui_SetActive(UI_WIDGET(filtersUI->checkSpawnPoints), !!filter->affectType);
		ui_SetActive(UI_WIDGET(filtersUI->checkVolumes), !!filter->affectType);
		ui_SetActive(UI_WIDGET(filtersUI->checkLights), !!filter->affectType);
		ui_SetActive(UI_WIDGET(filtersUI->checkRooms), !!filter->affectType);
		ui_SetActive(UI_WIDGET(filtersUI->checkPatrols), !!filter->affectType);
		ui_SetActive(UI_WIDGET(filtersUI->checkPatrolPoints), !!filter->affectType);
		ui_SetActive(UI_WIDGET(filtersUI->checkEncounters), !!filter->affectType);
		ui_SetActive(UI_WIDGET(filtersUI->checkActors), !!filter->affectType);

		ui_CheckButtonSetState(filtersUI->checkInteractables, !!(filter->filterTargets & WLE_FILTER_INTERACTABLES));
		ui_CheckButtonSetState(filtersUI->checkSpawnPoints, !!(filter->filterTargets & WLE_FILTER_SPAWNPOINTS));
		ui_CheckButtonSetState(filtersUI->checkVolumes, !!(filter->filterTargets & WLE_FILTER_VOLUMES));
		ui_CheckButtonSetState(filtersUI->checkLights, !!(filter->filterTargets & WLE_FILTER_LIGHTS));
		ui_CheckButtonSetState(filtersUI->checkRooms, !!(filter->filterTargets & WLE_FILTER_ROOMS));
		ui_CheckButtonSetState(filtersUI->checkPatrols, !!(filter->filterTargets & WLE_FILTER_PATROLS));
		ui_CheckButtonSetState(filtersUI->checkPatrolPoints, !!(filter->filterTargets & WLE_FILTER_PATROL_POINTS));
		ui_CheckButtonSetState(filtersUI->checkEncounters, !!(filter->filterTargets & WLE_FILTER_ENCOUNTERS));
		ui_CheckButtonSetState(filtersUI->checkActors, !!(filter->filterTargets & WLE_FILTER_ACTORS));

		ui_ListSetModel(filtersUI->criteriaList, parse_WleFilterCriterion, &filter->criteria);
		ui_ListSetSelectedRowAndCallback(filtersUI->criteriaList, 0);
	}
}

static void wleOptionsFilterDataChanged(UIWidget *widget, WleOptionsFiltersUI *filtersUI)
{
	WleFilter *filter = ui_ListGetSelectedObject(filtersUI->filterList);
	if (filter)
	{
		const char *str;

		// set widget contents onto filter data
		StructFreeString(filter->name);
		str = ui_TextEntryGetText(filtersUI->nameEntry);
		if (!str || !str[0])
			filter->name = StructAllocString("New Filter");
		else
			filter->name = StructAllocString(str);
		filter->ignoreNodeState = ui_CheckButtonGetState(filtersUI->ignoreNodeState);
		filter->affectType = ui_ComboBoxGetSelected(filtersUI->affectsCombo);

		filter->filterTargets = 0;
		if (ui_CheckButtonGetState(filtersUI->checkVolumes))
			filter->filterTargets |= WLE_FILTER_VOLUMES;
		if (ui_CheckButtonGetState(filtersUI->checkInteractables))
			filter->filterTargets |= WLE_FILTER_INTERACTABLES;
		if (ui_CheckButtonGetState(filtersUI->checkSpawnPoints))
			filter->filterTargets |= WLE_FILTER_SPAWNPOINTS;
		if (ui_CheckButtonGetState(filtersUI->checkLights))
			filter->filterTargets |= WLE_FILTER_LIGHTS;
		if (ui_CheckButtonGetState(filtersUI->checkRooms))
			filter->filterTargets |= WLE_FILTER_ROOMS;
		if (ui_CheckButtonGetState(filtersUI->checkPatrols))
			filter->filterTargets |= WLE_FILTER_PATROLS;
		if (ui_CheckButtonGetState(filtersUI->checkPatrolPoints))
			filter->filterTargets |= WLE_FILTER_PATROL_POINTS;
		if (ui_CheckButtonGetState(filtersUI->checkEncounters))
			filter->filterTargets |= WLE_FILTER_ENCOUNTERS;
		if (ui_CheckButtonGetState(filtersUI->checkActors))
			filter->filterTargets |= WLE_FILTER_ACTORS;

		ui_ListSetModel(filtersUI->criteriaList, parse_WleFilterCriterion, &filter->criteria);
	}
	wleOptionsFilterDataRefresh(filtersUI);
}

static void wleOptionsFilterSelected(UIList *list, WleOptionsFiltersUI *filtersUI)
{
	WleFilter *filter = ui_ListGetSelectedObject(list);
	if (filter)
	{
		ui_TabAddChild(filtersUI->parentTab, filtersUI->filterPane);
		wleOptionsFilterDataRefresh(filtersUI);
	}
	else
		ui_TabRemoveChild(filtersUI->parentTab, filtersUI->filterPane);
	ui_SetActive(UI_WIDGET(filtersUI->deleteFilterButton), !!filter);
}

static void wleOptionsNewFilterClicked(UIButton *button, WleOptionsFiltersUI *filtersUI)
{
	WleFilter *newFilter = StructCreate(parse_WleFilter);

	newFilter->name = StructAllocString("New Filter");
	eaPush(&filtersUI->copiedFilters, newFilter);
	ui_ListSetSelectedObjectAndCallback(filtersUI->filterList, newFilter);
	ui_SetFocus(filtersUI->nameEntry);
}

static void wleOptionsDeleteFilterClicked(UIButton *button, WleOptionsFiltersUI *filtersUI)
{
	int selected = ui_ListGetSelectedRow(filtersUI->filterList);
	if (selected >= 0 && selected < eaSize(&filtersUI->copiedFilters))
		StructDestroy(parse_WleFilter, eaRemove(&filtersUI->copiedFilters, selected));
	ui_ListSetModel(filtersUI->criteriaList, NULL, NULL);
	wleOptionsFilterSelected(filtersUI->filterList, filtersUI);
}

// Create function
static UIList *filterList = NULL;

static int wleOptionsCriteriaCompare(const WleCriterion **criterion1, const WleCriterion **criterion2)
{
	return strcmp((*criterion1)->propertyName, (*criterion2)->propertyName);
}

static bool wleOptionsFiltersLoad(EMOptionsTab *emTab, UITab *tab)
{
	WleOptionsFiltersUI *filtersUI = calloc(1, sizeof(*filtersUI));
	UIPane *pane;
	UIList *list;
	UIListColumn *column;
	UIButton *button;
	UITextEntry *entry;
	UILabel *label;
	UICheckButton *check;
	UIComboBox *combo;
	char ***affectsValues = calloc(1, sizeof(*affectsValues));

	eaCopyStructs(&editorUIState->searchFilters->filters, &filtersUI->copiedFilters, parse_WleFilter);
	filtersUI->parentTab = tab;

	// filter list
	list = ui_ListCreate(parse_WleFilter, &filtersUI->copiedFilters, 15);
	filterList = list;
	filtersUI->filterList = list;
	ui_ListSetSelectedCallback(list, wleOptionsFilterSelected, filtersUI);
	ui_WidgetSetDimensionsEx(UI_WIDGET(list), 0.3, 1, UIUnitPercentage, UIUnitPercentage);
	list->widget.leftPad = list->widget.rightPad = list->widget.topPad = 5;
	ui_TabAddChild(tab, list);
	column = ui_ListColumnCreateParseName("Defined Filters", "name", NULL);
	ui_ListColumnSetWidth(column, true, 1);
	ui_ListAppendColumn(list, column);
	button = ui_ButtonCreate("New", 0, 0, wleOptionsNewFilterClicked, filtersUI);
	button->widget.width = 0.15;
	button->widget.widthUnit = UIUnitPercentage;
	button->widget.leftPad = 5;
	button->widget.rightPad = 2;
	button->widget.offsetFrom = UIBottomLeft;
	ui_TabAddChild(tab, button);
	button = ui_ButtonCreate("Delete", 0, 0, wleOptionsDeleteFilterClicked, filtersUI);
	filtersUI->deleteFilterButton = button;
	button->widget.width = 0.15;
	button->widget.widthUnit = UIUnitPercentage;
	button->widget.xPOffset = 0.15;
	button->widget.leftPad = 2;
	button->widget.rightPad = 5;
	button->widget.offsetFrom = UIBottomLeft;
	ui_TabAddChild(tab, button);
	list->widget.bottomPad = elUINextY(button) + 5;

	// filter pane
	eaPush(affectsValues, "Includes all types");
	eaPush(affectsValues, "Includes");
	eaPush(affectsValues, "Excludes");
	pane = ui_PaneCreate(0, 0, 0.7, 1, UIUnitPercentage, UIUnitPercentage, 0);
	filtersUI->filterPane = pane;
	pane->widget.offsetFrom = UITopRight;
	pane->widget.topPad = 5;
	pane->invisible = true;
	label = ui_LabelCreate("Filter name", 5, 5);
	ui_PaneAddChild(pane, label);
	entry = ui_TextEntryCreate("", 0, label->widget.y);
	ui_TextEntrySetSelectOnFocus(entry, true);
	filtersUI->nameEntry = entry;
	ui_TextEntrySetFinishedCallback(entry, wleOptionsFilterDataChanged, filtersUI);
	entry->widget.width = 1;
	entry->widget.widthUnit = UIUnitPercentage;
	entry->widget.leftPad = elUINextX(label) + 5;
	entry->widget.rightPad = 5;
	ui_PaneAddChild(pane, entry);
	check = ui_CheckButtonCreate(elUINextX(label) + 5, elUINextY(label), "Ignore tracker open states", true);
	ui_CheckButtonSetToggledCallback(check, wleOptionsFilterDataChanged, filtersUI);
	filtersUI->ignoreNodeState = check;
	ui_PaneAddChild(pane, check);
	combo = ui_ComboBoxCreate(elUINextX(label) + 5, elUINextY(check), 150, NULL, affectsValues, NULL);
	filtersUI->affectsCombo = combo;
	ui_ComboBoxSetSelectedCallback(combo, wleOptionsFilterDataChanged, filtersUI);
	ui_PaneAddChild(pane, combo);

#define addTypeCheckButton(name, y)\
	check = ui_CheckButtonCreate(0, y, name, false);\
	ui_CheckButtonSetToggledCallback(check, wleOptionsFilterDataChanged, filtersUI);\
	check->widget.width = 0.5;\
	check->widget.widthUnit = UIUnitPercentage;\
	check->widget.leftPad = elUINextX(label) + 25;\
	ui_PaneAddChild(pane, check)
#define addTypeCheckButton2(name, y)\
	addTypeCheckButton(name, y);\
	check->widget.xPOffset = 0.5

	addTypeCheckButton("Interactables", elUINextY(combo) + 5);
	filtersUI->checkInteractables = check;
	addTypeCheckButton2("Lights", elUINextY(combo) + 5);
	filtersUI->checkLights = check;
	addTypeCheckButton("Rooms", elUINextY(check));
	filtersUI->checkRooms = check;
	addTypeCheckButton2("Spawn points", check->widget.y);
	filtersUI->checkSpawnPoints = check;
	addTypeCheckButton("Patrols", elUINextY(check));
	filtersUI->checkPatrols = check;
	addTypeCheckButton2("Patrol points", check->widget.y);
	filtersUI->checkPatrolPoints = check;
	addTypeCheckButton("Encounters", elUINextY(check));
	filtersUI->checkEncounters = check;
	addTypeCheckButton2("Actors", check->widget.y);
	filtersUI->checkActors = check;
	addTypeCheckButton("Volumes", elUINextY(check));
	filtersUI->checkVolumes = check;

	// criteria list
	label = ui_LabelCreate("Filter criteria", 5, elUINextY(check));
	ui_PaneAddChild(pane, label);
	list = ui_ListCreate(parse_WleFilterCriterion, NULL, 15);
	filtersUI->criteriaList = list;
	ui_ListSetSelectedCallback(list, wleOptionsFilterCritListSelected, filtersUI);
	list->widget.leftPad = list->widget.rightPad = 5;
	list->widget.y = elUINextY(label);
	list->widget.width = 1;
	list->widget.widthUnit = UIUnitPercentage;
	list->widget.height = 100;
	ui_PaneAddChild(pane, list);
	column = ui_ListColumnCreateText("Attribute", wleOptionsFilterCriteriaAttributeText, filtersUI);
	ui_ListColumnSetWidth(column, true, 1);
	ui_ListAppendColumn(list, column);
	column = ui_ListColumnCreateText("", wleOptionsFilterCriteriaOpText, filtersUI);
	ui_ListColumnSetWidth(column, false, 100);
	ui_ListAppendColumn(list, column);
	column = ui_ListColumnCreateText("Value", wleOptionsFilterCriteriaValueText, filtersUI);
	ui_ListColumnSetWidth(column, false, 100);
	ui_ListAppendColumn(list, column);
	button = ui_ButtonCreate("New", 0, elUINextY(list) + 5, wleOptionsFilterCritNewClicked, filtersUI);
	button->widget.width = 0.5;
	button->widget.widthUnit = UIUnitPercentage;
	button->widget.leftPad = 5;
	button->widget.rightPad = 2;
	ui_PaneAddChild(pane, button);
	button = ui_ButtonCreate("Delete", 0, elUINextY(list) + 5, wleOptionsFilterCritDeleteClicked, filtersUI);
	filtersUI->deleteCritButton = button;
	button->widget.xPOffset = 0.5;
	button->widget.width = 0.5;
	button->widget.widthUnit = UIUnitPercentage;
	button->widget.leftPad = 2;
	button->widget.rightPad = 5;
	ui_PaneAddChild(pane, button);

	// criterion widgets
	eaQSort(wleAllCriteriaArray, wleOptionsCriteriaCompare);
	label = ui_LabelCreate("Criterion", 5, elUINextY(button) + 10);
	ui_PaneAddChild(pane, label);
	combo = ui_ComboBoxCreate(elUINextX(label) + 5, elUINextY(button) + 10, 200, parse_WleCriterion, &wleAllCriteriaArray, "propertyName");
	filtersUI->critProperty = combo;
	ui_ComboBoxSetSelectedCallback(combo, wleOptionsFilterCritDataChanged, filtersUI);
	ui_PaneAddChild(pane, combo);
	combo = ui_ComboBoxCreate(elUINextX(combo) + 5, elUINextY(button) + 10, 100, NULL, NULL, NULL);
	filtersUI->critOp = combo;
	ui_ComboBoxSetSelectedCallback(combo, wleOptionsFilterCritDataChanged, filtersUI);
	ui_PaneAddChild(pane, combo);

	emOptionsTabSetData(emTab, filtersUI);
	return true;	
}

// Cancel function
static void wleOptionsFiltersCancel(EMOptionsTab *emTab, WleOptionsFiltersUI *filtersUI)
{
	ui_TabRemoveChild(filtersUI->parentTab, filtersUI->filterPane);
	if (filtersUI->critOp->model)
		eaDestroy(filtersUI->critOp->model);
	ui_WidgetQueueFree(UI_WIDGET(filtersUI->filterPane));
	ui_ListSetModel(filtersUI->filterList, NULL, NULL);
	eaDestroyStruct(&filtersUI->copiedFilters, parse_WleFilter);
	free(filtersUI);
}

static int wleOptionsFiltersCompare(const WleFilter **filter1, const WleFilter **filter2)
{
	return strcmp((*filter1)->name, (*filter2)->name);
}

// OK function
static void wleOptionsFiltersOk(EMOptionsTab *emTab, WleOptionsFiltersUI *filtersUI)
{
	int i, j;

	eaDestroyStruct(&editorUIState->searchFilters->filters, parse_WleFilter);
	for (i = 0; i < eaSize(&filtersUI->copiedFilters); i++)
	{
		WleFilter *filter = filtersUI->copiedFilters[i];
		for (j = 0; j < eaSize(&filter->criteria); j++)
		{
			WleFilterCriterion *crit = filter->criteria[j];
			crit->propertyName = StructAllocString(crit->criterion->propertyName);
		}
	}
	eaCopyStructs(&filtersUI->copiedFilters, &editorUIState->searchFilters->filters, parse_WleFilter);
	assert(editorUIState->searchFilters);
	eaQSort(editorUIState->searchFilters->filters, wleOptionsFiltersCompare);
	EditorPrefStoreStruct(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "SearchFilters", parse_WleFilterList, editorUIState->searchFilters);
	wleOptionsFiltersCancel(emTab, filtersUI);
}

void wleOptionsFilterEdit(WleFilter *filter)
{
	emOptionsShowEx("Search Filters");
	ui_ListSetSelectedRowAndCallback(filterList, eaFind(&editorUIState->searchFilters->filters, filter));
}

bool wleFilterApply(EditorObject *obj, WleFilter *filter)
{
	GroupTracker *tracker = NULL;
	int i;

	if (!filter)
		return true;

	if (obj->type->objType == EDTYPE_TRACKER)
		tracker = trackerFromTrackerHandle(obj->obj);

	// filter out non-applicable types
	if (filter->affectType != 0)
	{
		bool found = false;
		bool check;

#define wleFilterCheckTest()\
	if (check)\
	{\
		if (filter->affectType == 2)\
			return false;\
		else\
			found = true;\
	}\

		// include/exclude specified types
		if (!!(filter->filterTargets & WLE_FILTER_VOLUMES))
		{
			check = (tracker && tracker->def && tracker->def->property_structs.volume && !tracker->def->property_structs.volume->bSubVolume);
			wleFilterCheckTest();
		}
		if (!found && !!(filter->filterTargets & WLE_FILTER_ROOMS))
		{
			check = (tracker && tracker->def && SAFE_MEMBER(tracker->def->property_structs.room_properties, eRoomType) == WorldRoomType_Room);
			wleFilterCheckTest();
		}
		if (!found && !!(filter->filterTargets & WLE_FILTER_SPAWNPOINTS))
		{
			check = (tracker && tracker->def && tracker->def->property_structs.spawn_properties);
			wleFilterCheckTest();
		}
		if (!found && !!(filter->filterTargets & WLE_FILTER_INTERACTABLES))
		{
			check = (tracker && tracker->def && tracker->def->property_structs.interaction_properties);
			wleFilterCheckTest();
		}
		if (!found && !!(filter->filterTargets & WLE_FILTER_LIGHTS))
		{
			check = (tracker && tracker->def && tracker->def->property_structs.light_properties);
			wleFilterCheckTest();
		}
		if (!found && !!(filter->filterTargets & WLE_FILTER_PATROLS))
		{
			check = (tracker && tracker->def && tracker->def->property_structs.patrol_properties);
			wleFilterCheckTest();
		}
		if (!found && !!(filter->filterTargets & WLE_FILTER_PATROL_POINTS))
		{
			check = (obj->type->objType == EDTYPE_PATROL_POINT);
			wleFilterCheckTest();
		}
		if (!found && !!(filter->filterTargets & WLE_FILTER_ENCOUNTERS))
		{
			check = (tracker && tracker->def && tracker->def->property_structs.encounter_properties);
			wleFilterCheckTest();
		}
		if (!found && !!(filter->filterTargets & WLE_FILTER_ACTORS))
		{
			check = (obj->type->objType == EDTYPE_ENCOUNTER_ACTOR);
			wleFilterCheckTest();
		}

		if (filter->affectType == 1 && !found)
			return false;
	}

	// apply criteria
	for (i = 0; i < eaSize(&filter->criteria); i++)
	{
		if (!filter->criteria[i]->criterion->checkCallback(obj, filter->criteria[i]->propertyName, filter->criteria[i]->cond, filter->criteria[i]->val ? filter->criteria[i]->val : "", filter->criteria[i]->criterion->checkData))
			return false;
	}

	return true;
}

// Helper functions
bool wleCriterionStringTest(const char *string, const char *critString, WleCriterionCond cond, bool *output)
{
	if (cond == WLE_CRIT_EQUAL || cond == WLE_CRIT_NOT_EQUAL || cond == WLE_CRIT_LESS_THAN || cond == WLE_CRIT_GREATER_THAN)
	{
		int cmp = strcmpi(string, critString);
		switch(cond)
		{
			xcase WLE_CRIT_EQUAL:
				*output = !cmp;
			xcase WLE_CRIT_NOT_EQUAL:
				*output = !!cmp;
			xcase WLE_CRIT_LESS_THAN:
				*output = (cmp < 0);
			xcase WLE_CRIT_GREATER_THAN:
				*output = (cmp > 0);
		}
		return true;
	}
	else if (cond == WLE_CRIT_CONTAINS || cond == WLE_CRIT_BEGINS_WITH || cond == WLE_CRIT_ENDS_WITH)
	{
		char *search = strstri(string, critString);
		if (!search)
			return false;
		switch(cond)
		{
			xcase WLE_CRIT_CONTAINS:
				return !!search;
			xcase WLE_CRIT_BEGINS_WITH:
				return search == string;
			xcase WLE_CRIT_ENDS_WITH:
				return search == (string + strlen(string) - strlen(critString));
		}
		return true;
	}
	return false;
}

bool wleCriterionNumTest(const float val, const float critVal, WleCriterionCond cond, bool *output)
{
	if (cond == WLE_CRIT_EQUAL || cond == WLE_CRIT_NOT_EQUAL || cond == WLE_CRIT_LESS_THAN || cond == WLE_CRIT_GREATER_THAN)
	{
		switch (cond)
		{
			xcase WLE_CRIT_EQUAL:
				*output = (val == critVal);
			xcase WLE_CRIT_NOT_EQUAL:
				*output = (val != critVal);
			xcase WLE_CRIT_LESS_THAN:
				*output = (val < critVal);
			xcase WLE_CRIT_GREATER_THAN:
				*output = (val > critVal);
		}
		return true;
	}
	return false;
}

/********************
* VOLUMES
********************/
typedef struct WleOptionsVolumesUI
{
	UIColorButton *OcclusionVolumeColorButton;
	UIColorButton *AudioVolumeColorButton;
	UIColorButton *SkyFadeVolumeColorButton;
	UIColorButton *NeighborhoodVolumeColorButton;
	UIColorButton *InteractionVolumeColorButton;
	UIColorButton *LandmarkVolumeColorButton;
	UIColorButton *PowerVolumeColorButton;
	UIColorButton *WarpVolumeColorButton;
	UIColorButton *GenesisVolumeColorButton;
	UIColorButton *TerrainFilterVolumeColorButton;
	UIColorButton *TerrainExclusionVolumeColorButton;
} WleOptionsVolumesUI;

static void wleOptions_OcclusionVolumeColorValChanged( UIColorButton* colorButton, bool finished, void *unused )
{
	Vec4 color;
	ui_ColorButtonGetColor(colorButton, color);
	worldSetOcclusionVolumeColor(color);
}

static void wleOptions_AudioVolumeColorValChanged( UIColorButton* colorButton, bool finished, void *unused )
{
	Vec4 color;
	ui_ColorButtonGetColor(colorButton, color);
	worldSetAudioVolumeColor(color);
}

static void wleOptions_SkyFadeVolumeColorValChanged( UIColorButton* colorButton, bool finished, void *unused )
{
	Vec4 color;
	ui_ColorButtonGetColor(colorButton, color);
	worldSetSkyFadeVolumeColor(color);
}

static void wleOptions_NeighborhoodVolumeColorValChanged( UIColorButton* colorButton, bool finished, void *unused )
{
	Vec4 color;
	ui_ColorButtonGetColor(colorButton, color);
	worldSetNeighborhoodVolumeColor(color);
}

static void wleOptions_InteractionVolumeColorValChanged( UIColorButton* colorButton, bool finished, void *unused )
{
	Vec4 color;
	ui_ColorButtonGetColor(colorButton, color);
	worldSetInteractionVolumeColor(color);
}

static void wleOptions_LandmarkVolumeColorValChanged( UIColorButton* colorButton, bool finished, void *unused )
{
	Vec4 color;
	ui_ColorButtonGetColor(colorButton, color);
	worldSetLandmarkVolumeColor(color);
}

static void wleOptions_PowerVolumeColorValChanged( UIColorButton* colorButton, bool finished, void *unused )
{
	Vec4 color;
	ui_ColorButtonGetColor(colorButton, color);
	worldSetPowerVolumeColor(color);
}

static void wleOptions_WarpVolumeColorValChanged( UIColorButton* colorButton, bool finished, void *unused )
{
	Vec4 color;
	ui_ColorButtonGetColor(colorButton, color);
	worldSetWarpVolumeColor(color);
}

static void wleOptions_GenesisVolumeColorValChanged( UIColorButton* colorButton, bool finished, void *unused )
{
	Vec4 color;
	ui_ColorButtonGetColor(colorButton, color);
	worldSetGenesisVolumeColor(color);
}

static void wleOptions_TerrainFilterVolumeColorValChanged( UIColorButton* colorButton, bool finished, void *unused )
{
	Vec4 color;
	ui_ColorButtonGetColor(colorButton, color);
	worldSetTerrainFilterVolumeColor(color);
}

static void wleOptions_TerrainExclusionVolumeColorValChanged( UIColorButton* colorButton, bool finished, void *unused )
{
	Vec4 color;
	ui_ColorButtonGetColor(colorButton, color);
	worldSetTerrainExclusionVolumeColor(color);
}

static UIColorButton *wleOptions_CreateVolumeColorButton(UITab *tab, int y, const char *text, Vec4 color, UIColorChangeFunc changedF)
{
	UIColorButton *colorButton = ui_ColorButtonCreate(5, y, color);
	UILabel *label = ui_LabelCreate(text, elUINextX(colorButton) + 5, colorButton->widget.y);

	colorButton->liveUpdate = true;
	colorButton->bIsModal = true;
	strcpy(colorButton->title, text);
	ui_ColorButtonSetChangedCallback(colorButton, changedF, NULL);
	ui_WidgetSetWidth(UI_WIDGET(colorButton), 70);

	ui_TabAddChild(tab, colorButton);
	
	ui_TabAddChild(tab, label);

	return colorButton;
}

static Vec4 s_DefaultOcclusionVolumeColor;
static Vec4 s_DefaultAudioVolumeColor;
static Vec4 s_DefaultSkyFadeVolumeColor;
static Vec4 s_DefaultNeighborhoodVolumeColor;
static Vec4 s_DefaultInteractionVolumeColor;
static Vec4 s_DefaultLandmarkVolumeColor;
static Vec4 s_DefaultPowerVolumeColor;
static Vec4 s_DefaultWarpVolumeColor;
static Vec4 s_DefaultGenesisVolumeColor;
static Vec4 s_DefaultTerrainFilterVolumeColor;
static Vec4 s_DefaultTerrainExclusionVolumeColor;

static void wleOptions_VolumeRestoreDefaultsClicked(UIButton* button, WleOptionsVolumesUI *volumesUI)
{
	ui_ColorButtonSetColorAndCallback(volumesUI->OcclusionVolumeColorButton, s_DefaultOcclusionVolumeColor);
	ui_ColorButtonSetColorAndCallback(volumesUI->AudioVolumeColorButton, s_DefaultAudioVolumeColor);
	ui_ColorButtonSetColorAndCallback(volumesUI->SkyFadeVolumeColorButton, s_DefaultSkyFadeVolumeColor);
	ui_ColorButtonSetColorAndCallback(volumesUI->NeighborhoodVolumeColorButton, s_DefaultNeighborhoodVolumeColor);
	ui_ColorButtonSetColorAndCallback(volumesUI->InteractionVolumeColorButton, s_DefaultInteractionVolumeColor);
	ui_ColorButtonSetColorAndCallback(volumesUI->LandmarkVolumeColorButton, s_DefaultLandmarkVolumeColor);
	ui_ColorButtonSetColorAndCallback(volumesUI->PowerVolumeColorButton, s_DefaultPowerVolumeColor);
	ui_ColorButtonSetColorAndCallback(volumesUI->WarpVolumeColorButton, s_DefaultWarpVolumeColor);
	ui_ColorButtonSetColorAndCallback(volumesUI->GenesisVolumeColorButton, s_DefaultGenesisVolumeColor);
	ui_ColorButtonSetColorAndCallback(volumesUI->TerrainFilterVolumeColorButton, s_DefaultTerrainFilterVolumeColor);
	ui_ColorButtonSetColorAndCallback(volumesUI->TerrainExclusionVolumeColorButton, s_DefaultTerrainExclusionVolumeColor);
}

// Load function
static bool wleOptionsVolumesLoad(EMOptionsTab *emTab, UITab *tab)
{
	Vec4 color;
	UIColorButton *colorButton;
	UIButton *button;

	WleOptionsVolumesUI *volumesUI = calloc(1, sizeof(*volumesUI));

	worldGetOcclusionVolumeColor(color);
	colorButton = wleOptions_CreateVolumeColorButton(tab, 5, "Occlusion Volume Color", color, wleOptions_OcclusionVolumeColorValChanged);
	volumesUI->OcclusionVolumeColorButton = colorButton;

	worldGetAudioVolumeColor(color);
	colorButton = wleOptions_CreateVolumeColorButton(tab, elUINextY(colorButton) + 5, "Audio Volume Color", color, wleOptions_AudioVolumeColorValChanged);
	volumesUI->AudioVolumeColorButton = colorButton;

	worldGetSkyFadeVolumeColor(color);
	colorButton = wleOptions_CreateVolumeColorButton(tab, elUINextY(colorButton) + 5, "Sky Fade Volume Color", color, wleOptions_SkyFadeVolumeColorValChanged);
	volumesUI->SkyFadeVolumeColorButton = colorButton;

	worldGetNeighborhoodVolumeColor(color);
	colorButton = wleOptions_CreateVolumeColorButton(tab, elUINextY(colorButton) + 5, "Neighborhood Volume Color", color, wleOptions_NeighborhoodVolumeColorValChanged);
	volumesUI->NeighborhoodVolumeColorButton = colorButton;

	worldGetInteractionVolumeColor(color);
	colorButton = wleOptions_CreateVolumeColorButton(tab, elUINextY(colorButton) + 5, "Interaction Volume Color", color, wleOptions_InteractionVolumeColorValChanged);
	volumesUI->InteractionVolumeColorButton = colorButton;

	worldGetLandmarkVolumeColor(color);
	colorButton = wleOptions_CreateVolumeColorButton(tab, elUINextY(colorButton) + 5, "Landmark Volume Color", color, wleOptions_LandmarkVolumeColorValChanged);
	volumesUI->LandmarkVolumeColorButton = colorButton;

	worldGetPowerVolumeColor(color);
	colorButton = wleOptions_CreateVolumeColorButton(tab, elUINextY(colorButton) + 5, "Power Volume Color", color, wleOptions_PowerVolumeColorValChanged);
	volumesUI->PowerVolumeColorButton = colorButton;

	worldGetWarpVolumeColor(color);
	colorButton = wleOptions_CreateVolumeColorButton(tab, elUINextY(colorButton) + 5, "Warp Volume Color", color, wleOptions_WarpVolumeColorValChanged);
	volumesUI->WarpVolumeColorButton = colorButton;

	worldGetGenesisVolumeColor(color);
	colorButton = wleOptions_CreateVolumeColorButton(tab, elUINextY(colorButton) + 5, "Genesis Volume Color", color, wleOptions_GenesisVolumeColorValChanged);
	volumesUI->GenesisVolumeColorButton = colorButton;

	worldGetTerrainFilterVolumeColor(color);
	colorButton = wleOptions_CreateVolumeColorButton(tab, elUINextY(colorButton) + 5, "Terrain Filter Volume Color", color, wleOptions_TerrainFilterVolumeColorValChanged);
	volumesUI->TerrainFilterVolumeColorButton = colorButton;

	worldGetTerrainExclusionVolumeColor(color);
	colorButton = wleOptions_CreateVolumeColorButton(tab, elUINextY(colorButton) + 5, "Terrain Exclusion Volume Color", color, wleOptions_TerrainExclusionVolumeColorValChanged);
	volumesUI->TerrainExclusionVolumeColorButton = colorButton;

	button = ui_ButtonCreate("Restore Defaults", 5, elUINextY(colorButton) + 5, wleOptions_VolumeRestoreDefaultsClicked, volumesUI);
	ui_TabAddChild(tab, button);

	emOptionsTabSetData(emTab, volumesUI);

	return true;
}

static void wleOptionsVolumeColorStorePref(const char* prefName, Vec4 color)
{
	char fullPrefName[256];

	strcpy_s(fullPrefName, sizeof(fullPrefName), prefName);

	strcat_s(fullPrefName, sizeof(fullPrefName), "R");
	EditorPrefStoreFloat(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, fullPrefName, color[0]);
	strcat_s(fullPrefName, sizeof(fullPrefName), "G");
	EditorPrefStoreFloat(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, fullPrefName, color[1]);
	strcat_s(fullPrefName, sizeof(fullPrefName), "B");
	EditorPrefStoreFloat(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, fullPrefName, color[2]);
	strcat_s(fullPrefName, sizeof(fullPrefName), "A");
	EditorPrefStoreFloat(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, fullPrefName, color[3]);
}

void wleOptionsVolumesStorePrefs()
{
	Vec4 color;

	worldGetOcclusionVolumeColor(color);
	wleOptionsVolumeColorStorePref("OcclusionVolume", color);

	worldGetAudioVolumeColor(color);
	wleOptionsVolumeColorStorePref("AudioVolume", color);

	worldGetSkyFadeVolumeColor(color);
	wleOptionsVolumeColorStorePref("SkyFadeVolume", color);

	worldGetNeighborhoodVolumeColor(color);
	wleOptionsVolumeColorStorePref("NeighborhoodVolume", color);

	worldGetInteractionVolumeColor(color);
	wleOptionsVolumeColorStorePref("InteractionVolume", color);

	worldGetLandmarkVolumeColor(color);
	wleOptionsVolumeColorStorePref("LandmarkVolume", color);

	worldGetPowerVolumeColor(color);
	wleOptionsVolumeColorStorePref("PowerVolume", color);

	worldGetWarpVolumeColor(color);
	wleOptionsVolumeColorStorePref("WarpVolume", color);

	worldGetGenesisVolumeColor(color);
	wleOptionsVolumeColorStorePref("GenesisVolume", color);

	worldGetTerrainFilterVolumeColor(color);
	wleOptionsVolumeColorStorePref("TerrainFilterVolume", color);

	worldGetTerrainExclusionVolumeColor(color);
	wleOptionsVolumeColorStorePref("TerrainExclusionVolume", color);
}

static void wleOptionsVolumeColorLoadPref(const char* prefName, Vec4 color, const Vec4 defaultColor)
{
	char fullPrefName[256];

	strcpy_s(fullPrefName, sizeof(fullPrefName), prefName);

	strcat_s(fullPrefName, sizeof(fullPrefName), "R");
	color[0] = EditorPrefGetFloat(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, fullPrefName, defaultColor[0]);
	strcat_s(fullPrefName, sizeof(fullPrefName), "G");
	color[1] = EditorPrefGetFloat(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, fullPrefName, defaultColor[1]);
	strcat_s(fullPrefName, sizeof(fullPrefName), "B");
	color[2] = EditorPrefGetFloat(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, fullPrefName, defaultColor[2]);
	strcat_s(fullPrefName, sizeof(fullPrefName), "A");
	color[3] = EditorPrefGetFloat(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, fullPrefName, defaultColor[3]);
}

void wleOptionsVolumesLoadPrefs()
{
	Vec4 color;

	worldGetOcclusionVolumeColor(s_DefaultOcclusionVolumeColor);
	wleOptionsVolumeColorLoadPref("OcclusionVolume", color, s_DefaultOcclusionVolumeColor);
	worldSetOcclusionVolumeColor(color);

	worldGetAudioVolumeColor(s_DefaultAudioVolumeColor);
	wleOptionsVolumeColorLoadPref("AudioVolume", color, s_DefaultAudioVolumeColor);
	worldSetAudioVolumeColor(color);

	worldGetSkyFadeVolumeColor(s_DefaultSkyFadeVolumeColor);
	wleOptionsVolumeColorLoadPref("SkyFadeVolume", color, s_DefaultSkyFadeVolumeColor);
	worldSetSkyFadeVolumeColor(color);

	worldGetNeighborhoodVolumeColor(s_DefaultNeighborhoodVolumeColor);
	wleOptionsVolumeColorLoadPref("NeighborhoodVolume", color, s_DefaultNeighborhoodVolumeColor);
	worldSetNeighborhoodVolumeColor(color);

	worldGetInteractionVolumeColor(s_DefaultInteractionVolumeColor);
	wleOptionsVolumeColorLoadPref("InteractionVolume", color, s_DefaultInteractionVolumeColor);
	worldSetInteractionVolumeColor(color);

	worldGetLandmarkVolumeColor(s_DefaultLandmarkVolumeColor);
	wleOptionsVolumeColorLoadPref("LandmarkVolume", color, s_DefaultLandmarkVolumeColor);
	worldSetLandmarkVolumeColor(color);

	worldGetPowerVolumeColor(s_DefaultPowerVolumeColor);
	wleOptionsVolumeColorLoadPref("PowerVolume", color, s_DefaultPowerVolumeColor);
	worldSetPowerVolumeColor(color);

	worldGetWarpVolumeColor(s_DefaultWarpVolumeColor);
	wleOptionsVolumeColorLoadPref("WarpVolume", color, s_DefaultWarpVolumeColor);
	worldSetWarpVolumeColor(color);

	worldGetGenesisVolumeColor(s_DefaultGenesisVolumeColor);
	wleOptionsVolumeColorLoadPref("GenesisVolume", color, s_DefaultGenesisVolumeColor);
	worldSetGenesisVolumeColor(color);

	worldGetTerrainFilterVolumeColor(s_DefaultTerrainFilterVolumeColor);
	wleOptionsVolumeColorLoadPref("TerrainFilterVolume", color, s_DefaultTerrainFilterVolumeColor);
	worldSetTerrainFilterVolumeColor(color);

	worldGetTerrainExclusionVolumeColor(s_DefaultTerrainExclusionVolumeColor);
	wleOptionsVolumeColorLoadPref("TerrainExclusionVolume", color, s_DefaultTerrainExclusionVolumeColor);
	worldSetTerrainExclusionVolumeColor(color);

	// The reason for the save after the load is because the default values of the colors are read from the static memory of those colors before attempting to load.
	// This makes sure we do not accidentally load and have different defaults in place.
	wleOptionsVolumesStorePrefs();
}

// Cancel function
static void wleOptionsVolumesCancel(EMOptionsTab *emTab, WleOptionsVolumesUI *volumesUI)
{
	// restore the options to what was previously saved
	wleOptionsVolumesLoadPrefs();

	free(volumesUI);
}

// OK function
static void wleOptionsVolumesOk(EMOptionsTab *emTab, WleOptionsVolumesUI *volumesUI)
{
	// save the options
	wleOptionsVolumesStorePrefs();

	free(volumesUI);
}

/********************
* REGISTRATION
********************/
void wleOptionsRegisterTabs(void)
{
	eaPush(&worldEditor.options_tabs, emOptionsTabCreate("General", wleOptionsGeneralLoad, wleOptionsGeneralOk, wleOptionsGeneralCancel));
	eaPush(&worldEditor.options_tabs, emOptionsTabCreate("Custom Menu", wleOptionsCustomMenuLoad, wleOptionsCustomMenuOk, wleOptionsCustomMenuCancel));
	eaPush(&worldEditor.options_tabs, emOptionsTabCreate("Search Filters", wleOptionsFiltersLoad, wleOptionsFiltersOk, wleOptionsFiltersCancel));
	eaPush(&worldEditor.options_tabs, emOptionsTabCreate("Volumes", wleOptionsVolumesLoad, wleOptionsVolumesOk, wleOptionsVolumesCancel));
}

#endif

#include "WorldEditorOptions_h_ast.c"
