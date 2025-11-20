#ifndef NO_EDITORS

#include "EditorManagerOptions.h"
#include "EditorManagerPrivate.h"
#include "EditorManagerUIInfoWin.h"
#include "EditorManagerUIMotD.h"
#include "EditLibUIUtil.h"
#include "EditorPrefs.h"
#include "EString.h"
#include "qsortG.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

/********************
* UI
********************/
static bool emOptionsCancelClicked(UIWidget *widget, UserData unused)
{
	int i;
	if (em_data.options.active_editor)
	{
		for (i = 0; i < eaSize(&em_data.options.active_editor->options_tabs); i++)
		{
			EMOptionsTab *tab = em_data.options.active_editor->options_tabs[i];
			if (tab->cancel_func && tab->show)
				tab->cancel_func(tab, tab->data);
		}
	}
	for (i = 0; i < eaSize(&em_data.options.tabs); i++)
	{
		EMOptionsTab *tab = em_data.options.tabs[i];
		if (tab->cancel_func && tab->show)
			tab->cancel_func(tab, tab->data);
	}

	EditorPrefStoreWindowPosition("Editor Manager", "Options", "Window Location", em_data.options.win);
	return elUIWindowClose(NULL, em_data.options.win);
}

static void emOptionsOkClicked(UIWidget *widget, UserData unused)
{
	int i;
	if (em_data.options.active_editor)
	{
		for (i = 0; i < eaSize(&em_data.options.active_editor->options_tabs); i++)
		{
			EMOptionsTab *tab = em_data.options.active_editor->options_tabs[i];
			if (tab->ok_func && tab->show)
				tab->ok_func(tab, tab->data);
		}
	}
	for (i = 0; i < eaSize(&em_data.options.tabs); i++)
	{
		EMOptionsTab *tab = em_data.options.tabs[i];
		if (tab->ok_func && tab->show)
			tab->ok_func(tab, tab->data);
	}

	EditorPrefStoreWindowPosition("Editor Manager", "Options", "Window Location", em_data.options.win);
	elUIWindowClose(NULL, em_data.options.win);
}

static void emOptionsTabChanged(UITabGroup *tabs, UserData unused)
{
	UITab *active_tab = ui_TabGroupGetActive(tabs);
	if (active_tab)
		EditorPrefStoreString(em_data.options.active_editor ? em_data.options.active_editor->editor_name : "Editor Manager", "Options", "Default Tab", ui_TabGetTitle(active_tab));
}

void emOptionsShowEx(const char *tab_name)
{
	UIWindow *window = ui_WindowCreate("Options", 0, 0, 500, 300);
	UITabGroup *tabs = ui_TabGroupCreate(0, 0, 1, 1);
	UIButton *button;
	char *default_tab_pref;
	UITab *default_tab = NULL;
	int i;

	em_data.options.win = window;
	em_data.options.active_editor = em_data.current_editor;
	if (tab_name && tab_name[0])
	{
		strdup_alloca(default_tab_pref, tab_name);
	}
	else
	{
		strdup_alloca(default_tab_pref, EditorPrefGetString(em_data.options.active_editor ? em_data.options.active_editor->editor_name : "Editor Manager", "Options", "Default Tab", ""));
	}

	ui_WindowAddChild(window, tabs);
	tabs->widget.widthUnit = tabs->widget.heightUnit = UIUnitPercentage;
	tabs->widget.leftPad = tabs->widget.rightPad = tabs->widget.topPad = 5;

	// create editor tabs
	if (em_data.options.active_editor)
	{
		for (i = 0; i < eaSize(&em_data.options.active_editor->options_tabs); i++)
		{
			EMOptionsTab *tab = em_data.options.active_editor->options_tabs[i];
			UITab *ui_tab = ui_TabCreate(tab->tab_name);

			if (tab->load_func && tab->load_func(tab, ui_tab))
			{
				ui_TabGroupAddTab(tabs, ui_tab);
				tab->show = true;
				if (!default_tab || strcmpi(default_tab_pref, tab->tab_name) == 0)
					default_tab = ui_tab;
			}
			else
			{
				ui_TabFree(ui_tab);
				tab->show = false;
			}
		}
	}

	// create internal tabs
	for (i = 0; i < eaSize(&em_data.options.tabs); i++)
	{
		for (i = 0; i < eaSize(&em_data.options.tabs); i++)
		{
			EMOptionsTab *tab = em_data.options.tabs[i];
			UITab *ui_tab = ui_TabCreate(tab->tab_name);

			if (tab->load_func && tab->load_func(tab, ui_tab))
			{
				ui_TabGroupAddTab(tabs, ui_tab);
				tab->show = true;
				if (!default_tab || strcmpi(default_tab_pref, tab->tab_name) == 0)
					default_tab = ui_tab;
			}
			else
			{
				ui_TabFree(ui_tab);
				tab->show = false;
			}
		}
	}

	// set default tab preference
	if (default_tab)
		ui_TabGroupSetActive(tabs, default_tab);
	ui_TabGroupSetChangedCallback(tabs, emOptionsTabChanged, NULL);

	ui_WindowSetModal(window, true);
	elUICenterWindow(window);
	EditorPrefGetWindowPosition("Editor Manager", "Options", "Window Location", window);
	button = elUIAddCancelOkButtons(window, emOptionsCancelClicked, NULL, emOptionsOkClicked, NULL);
	tabs->widget.bottomPad = elUINextY(button) + 5;
	ui_WindowSetCloseCallback(window, emOptionsCancelClicked, NULL);	
	ui_WindowShow(window);
}

AUTO_COMMAND ACMD_NAME("EM.Options");
void emOptionsShow(void)
{
	emOptionsShowEx(NULL);
}

/********************
* OPTIONS TAB MANAGEMENT
********************/
EMOptionsTab *emOptionsTabCreate(const char *tab_name, EMOptionsLoadFunc load_func, EMOptionsTabFunc ok_func, EMOptionsTabFunc cancel_func)
{
	EMOptionsTab *tab = calloc(1, sizeof(*tab));

	strcpy(tab->tab_name, tab_name);
	tab->load_func = load_func;
	tab->cancel_func = cancel_func;
	tab->ok_func = ok_func;

	return tab;
}

void emOptionsTabSetData(EMOptionsTab *tab, void *data)
{
	tab->data = data;
}

/********************
* CAMERA OPTIONS
********************/

typedef struct EMCameraOptions
{
	UITextEntry *start_speed;
	UITextEntry *max_speed;
	UITextEntry *acc_delay;
	UITextEntry *acc_rate;
	UITextEntry *zoom_step;
} EMCameraOptions;

static bool emCameraOptionsLoad(EMOptionsTab *options_tab, UITab *tab)
{
	EMCameraOptions *options;
	UILabel *label;
	UITextEntry *entry;
	char float_val[16];
	int max_x = 0;

	if (!em_data.current_editor || ((em_data.current_editor->hide_world || em_data.current_editor->force_editor_cam) && !em_data.current_editor->use_em_cam_keybinds))
		return false;
	
	options = calloc(1, sizeof(*options));

	label = ui_LabelCreate("Start speed (ft/s)", 5, 5);
	ui_TabAddChild(tab, label);
	max_x = MAX(max_x, elUINextX(label));
	entry = ui_TextEntryCreate("", 0, label->widget.y);
	ui_TextEntrySetFloatOnly(entry);
	sprintf(float_val, "%f", EditorPrefGetFloat("Editor Manager", "Camera", "Start Speed", em_data.worldcam->start_speed));
	ui_TextEntrySetText(entry, float_val);
	ui_TabAddChild(tab, entry);
	options->start_speed = entry;

	label = ui_LabelCreate("Max speed (ft/s)", label->widget.x,  elUINextY(label) + 5);
	ui_TabAddChild(tab, label);
	max_x = MAX(max_x, elUINextX(label));
	entry = ui_TextEntryCreate("", 0, label->widget.y);
	ui_TextEntrySetFloatOnly(entry);
	sprintf(float_val, "%f", EditorPrefGetFloat("Editor Manager", "Camera", "Max Speed", em_data.worldcam->max_speed));
	ui_TextEntrySetText(entry, float_val);
	ui_TabAddChild(tab, entry);
	options->max_speed = entry;

	label = ui_LabelCreate("Acceleration delay (secs)", label->widget.x,  elUINextY(label) + 5);
	ui_TabAddChild(tab, label);
	max_x = MAX(max_x, elUINextX(label));
	entry = ui_TextEntryCreate("", 0, label->widget.y);
	ui_TextEntrySetFloatOnly(entry);
	sprintf(float_val, "%f", EditorPrefGetFloat("Editor Manager", "Camera", "Accel Delay", em_data.worldcam->acc_delay));
	ui_TextEntrySetText(entry, float_val);
	ui_TabAddChild(tab, entry);
	options->acc_delay = entry;

	label = ui_LabelCreate("Acceleration rate (ft/s/s)", label->widget.x,  elUINextY(label) + 5);
	ui_TabAddChild(tab, label);
	max_x = MAX(max_x, elUINextX(label));
	entry = ui_TextEntryCreate("", 0, label->widget.y);
	ui_TextEntrySetFloatOnly(entry);
	sprintf(float_val, "%f", EditorPrefGetFloat("Editor Manager", "Camera", "Accel Rate", em_data.worldcam->acc_rate));
	ui_TextEntrySetText(entry, float_val);
	ui_TabAddChild(tab, entry);
	options->acc_rate = entry;

	label = ui_LabelCreate("Zoom step (ft)", label->widget.x,  elUINextY(label) + 5);
	ui_TabAddChild(tab, label);
	max_x = MAX(max_x, elUINextX(label));
	entry = ui_TextEntryCreate("", 0, label->widget.y);
	ui_TextEntrySetFloatOnly(entry);
	sprintf(float_val, "%f", EditorPrefGetFloat("Editor Manager", "Camera", "Zoom Step", 20));
	ui_TextEntrySetText(entry, float_val);
	ui_TabAddChild(tab, entry);
	options->zoom_step = entry;

	max_x += 5;
	options->start_speed->widget.x = max_x;
	options->max_speed->widget.x = max_x;
	options->acc_delay->widget.x = max_x;
	options->acc_rate->widget.x = max_x;
	options->zoom_step->widget.x = max_x;

	emOptionsTabSetData(options_tab, options);
	return true;
}

static void emCameraOptionsCancel(EMOptionsTab *tab, EMCameraOptions *options)
{
	SAFE_FREE(options);
}

static void emCameraOptionsOk(EMOptionsTab *tab, EMCameraOptions *options)
{
	const char *text;
	float f, f2;

	text = ui_TextEntryGetText(options->start_speed);
	f = atof(text);
	if (f < 0)
		f = 0;
	text = ui_TextEntryGetText(options->max_speed);
	f2 = atof(text);
	if (f2 < 0)
		f2 = 0;
	if (f > f2)
		f = f2;
	EditorPrefStoreFloat("Editor Manager", "Camera", "Start Speed", f);
	EditorPrefStoreFloat("Editor Manager", "Camera", "Max Speed", f2);
	text = ui_TextEntryGetText(options->acc_delay);
	f = atof(text);
	if (f < 0)
		f = 0;
	EditorPrefStoreFloat("Editor Manager", "Camera", "Accel Delay", f);
	text = ui_TextEntryGetText(options->acc_rate);
	f = atof(text);
	if (f < 0)
		f = 0;
	EditorPrefStoreFloat("Editor Manager", "Camera", "Accel Rate", f);
	text = ui_TextEntryGetText(options->zoom_step);
	f = atof(text);
	if (f < 0)
		f = 0;
	EditorPrefStoreFloat("Editor Manager", "Camera", "Zoom Step", f);
	emFreecamApplyPrefs();

	SAFE_FREE(options);
}

/********************
* INFO WINDOW OPTIONS
********************/

typedef struct EMInfoWinOptions
{
	UICheckButton *enabled;
	UIList *availData;
	UIList *selectData;
	UICheckButton *alwaysInFront;
	UIWidgetGroup grayables;

	int oldX, oldY, oldWidth, oldHeight;

	EMEditor *active_editor;
} EMInfoWinOptions;

static void emInfoWinOptionsEnabledToggle(UICheckButton *button, EMInfoWinOptions *options)
{
	ui_WidgetGroupSetActive(&options->grayables, ui_CheckButtonGetState(button));
}

static void emInfoWinOptionsListDisplay(UIList *list, UIListColumn *column, int row, UserData userData, char **output)
{
	estrPrintf(output, "%s", ((EMInfoWinEntry**)(*list->peaModel))[row]->display_name);
}

static void emInfoWinOptionsShuttleRight(UIButton *button, EMInfoWinOptions *options)
{
	UIList *left = options->availData;
	char *selected = ui_ListGetSelectedObject(left);
	if (selected)
	{
		UIList *right = options->selectData;
		if (eaFind(right->peaModel, selected) == -1)
		{
			eaPush(right->peaModel, selected);
			ui_ListSetSelectedRowAndCallback(right, eaSize(right->peaModel) - 1);
		}
	}
}

static void emInfoWinOptionsShuttleLeft(UIButton *button, EMInfoWinOptions *options)
{
	UIList *right = options->selectData;
	if (ui_ListGetSelectedRow(right) != -1)
	{
		eaRemove(right->peaModel, ui_ListGetSelectedRow(right));
		ui_ListSetSelectedRowAndCallback(right, -1);
	}
}

static void emInfoWinOptionsClear(UIButton *button, EMInfoWinOptions *options)
{
	UIList *right = options->selectData;
	eaDestroy(right->peaModel);
	ui_ListSetSelectedRowAndCallback(right, -1);
}

static void emInfoWinOptionsMoveTop(UIButton *button, EMInfoWinOptions *options)
{
	UIList *right = options->selectData;
	eaMove(right->peaModel, 0, ui_ListGetSelectedRow(right));
	ui_ListSetSelectedRowAndCallback(right, 0);
}

static void emInfoWinOptionsMoveUp(UIButton *button, EMInfoWinOptions *options)
{
	UIList *right = options->selectData;
	int dest = MAX(0, ui_ListGetSelectedRow(right) - 1);
	eaMove(right->peaModel, dest, ui_ListGetSelectedRow(right));
	ui_ListSetSelectedRowAndCallback(right, dest);
}

static void emInfoWinOptionsMoveDown(UIButton *button, EMInfoWinOptions *options)
{
	UIList *right = options->selectData;
	int dest = MIN(eaSize(right->peaModel) - 1, ui_ListGetSelectedRow(right) + 1);
	eaMove(right->peaModel, dest, ui_ListGetSelectedRow(right));
	ui_ListSetSelectedRowAndCallback(right, dest);
}

static void emInfoWinOptionsMoveBottom(UIButton *button, EMInfoWinOptions *options)
{
	UIList *right = options->selectData;
	int dest = eaSize(right->peaModel) - 1;
	eaMove(right->peaModel, dest, ui_ListGetSelectedRow(right));
	ui_ListSetSelectedRowAndCallback(right, dest);
}

static void emInfoWinOptionsSetLocOK(UIButton *button, UIWindow *window)
{
	EditorPrefStoreInt("Editor Manager", "Info Win", "X", window->widget.x);
	EditorPrefStoreInt("Editor Manager", "Info Win", "Y", window->widget.y);
	EditorPrefStoreInt("Editor Manager", "Info Win", "Width", window->widget.width);
	EditorPrefStoreInt("Editor Manager", "Info Win", "Height", window->widget.height);
	ui_WidgetQueueFree((UIWidget*) window);
	ui_WindowSetModal(em_data.options.win, true);
}

static bool emInfoWinOptionsSetLocClose(UIWindow *window, UserData unused)
{
	ui_WidgetQueueFree((UIWidget*) window);
	ui_WindowSetModal(em_data.options.win, true);
	return true;
}

static void emInfoWinOptionsSetLocation(UIButton *widget, UserData unused)
{
	UIWindow *window = ui_WindowCreate("Set Location/Dimensions",
		EditorPrefGetInt("Editor Manager", "Info Win", "X", 100),
		EditorPrefGetInt("Editor Manager", "Info Win", "Y", 100),
		EditorPrefGetInt("Editor Manager", "Info Win", "Width", 200),
		EditorPrefGetInt("Editor Manager", "Info Win", "Height", 100));
	UILabel *label = ui_LabelCreate("Position/resize this window", 0, 0);
	UIButton *button = ui_ButtonCreate("OK", 0, 0, emInfoWinOptionsSetLocOK, window);

	ui_WidgetSetPositionEx((UIWidget*) label, 0, -18, 0, 0.5, UITop);
	ui_WindowAddChild(window, label);
	ui_WidgetSetPositionEx((UIWidget*) button, 0, 0, 0, 0.5, UITop);
	ui_WindowAddChild(window, button);
	ui_WindowSetModal(em_data.options.win, false);
	ui_WindowSetCloseCallback(window, emInfoWinOptionsSetLocClose, NULL);
	ui_WindowSetModal(window, true);
	ui_WindowShow(window);
}

static int emInfoWinEntryCompare(const EMInfoWinEntry **entry1, const EMInfoWinEntry **entry2)
{
	return strcmpi((*entry1)->display_name, (*entry2)->display_name);
}

static bool emInfoWinOptionsLoad(EMOptionsTab *emTab, UITab *tab)
{
	EMInfoWinOptions *options;
	UICheckButton *check;
	UIButton *button;
	UIList *list;
	UIListColumn *list_col;
	EMInfoWinEntry ***avail_info;
	EMInfoWinEntry ***selected_info;
	EMInfoWin *displayed_entries;
	StashTableIterator iter;
	StashElement el;
	const int button_size = 20, small_button_size = 14;
	int i, top_pad, bottom_pad = 0;

	// if no editor is active, or the active editor has no available info win entries, do not show this tab
	if (!em_data.current_editor || stashGetCount(em_data.current_editor->avail_entries) == 0)
		return false;

	// allocate memory
	options = calloc(1, sizeof(*options));
	avail_info = calloc(1, sizeof(*avail_info));
	selected_info = calloc(1, sizeof(*selected_info));
	options->active_editor = em_data.current_editor;

	// cache old settings
	options->oldX = EditorPrefGetInt("Editor Manager", "Info Win", "X", 100);
	options->oldY = EditorPrefGetInt("Editor Manager", "Info Win", "Y", 100);
	options->oldWidth = EditorPrefGetInt("Editor Manager", "Info Win", "Width", 200);
	options->oldHeight = EditorPrefGetInt("Editor Manager", "Info Win", "Height", 100);

	// load settings
	displayed_entries = StructCreate(parse_EMInfoWin);
	EditorPrefGetStruct(options->active_editor->editor_name, "Info Win", "Contents", parse_EMInfoWin, displayed_entries);

	// build UI
	eaDestroy(&options->grayables);
	check = ui_CheckButtonCreate(0, 5, "Enable info window", EditorPrefGetInt("Editor Manager", "Info Win", "Enabled", 0));
	ui_CheckButtonSetToggledCallback(check, emInfoWinOptionsEnabledToggle, options);
	ui_TabAddChild(tab, check);
	options->enabled = check;
	top_pad = elUINextY(check);

	check = ui_CheckButtonCreate(0, 0, "Always in front", EditorPrefGetInt("Editor Manager", "Info Win", "In Front", 0));
	check->widget.offsetFrom = UIBottomLeft;
	ui_TabAddChild(tab, check);
	eaPush(&options->grayables, (UIWidget*) check);
	options->alwaysInFront = check;
	button = ui_ButtonCreate("Set location...", 0, elUINextY(check), emInfoWinOptionsSetLocation, NULL);
	button->widget.offsetFrom = UIBottomLeft;
	ui_TabAddChild(tab, button);
	eaPush(&options->grayables, (UIWidget*) button);

	stashGetIterator(options->active_editor->avail_entries, &iter);
	while (stashGetNextElement(&iter, &el))
		eaPush(avail_info, stashElementGetPointer(el));
	eaQSort(*avail_info, emInfoWinEntryCompare);
	list = ui_ListCreate(NULL, (EArrayHandle*) avail_info, 15);
	ui_WidgetSetPositionEx((UIWidget*) list, 0, 0, 0, 0, UITopLeft);
	ui_WidgetSetDimensionsEx((UIWidget*) list, 0.4, 1, UIUnitPercentage, UIUnitPercentage);
	list->widget.topPad = top_pad;
	list->widget.bottomPad = elUINextY(button) + 5;
	list_col = ui_ListColumnCreate(UIListTextCallback, "Available Info", (intptr_t) emInfoWinOptionsListDisplay, NULL);
	ui_ListColumnSetWidth(list_col, true, 1);
	ui_ListAppendColumn(list, list_col);
	ui_TabAddChild(tab, list);
	eaPush(&options->grayables, (UIWidget*) list);
	options->availData = list;

	for (i = 0; i < eaSize(&displayed_entries->entry_indexes); i++)
	{
		EMInfoWinEntry *entry = emInfoWinEntryGet(options->active_editor, displayed_entries->entry_indexes[i]);
		if (entry)
			eaPush(selected_info, entry);
	}
	list = ui_ListCreate(NULL, (EArrayHandle*) selected_info, 15);
	ui_WidgetSetPositionEx((UIWidget*) list, 0, 0, 0.5, 0, UITopLeft);
	ui_WidgetSetDimensionsEx((UIWidget*) list, 0.4, 1, UIUnitPercentage, UIUnitPercentage);
	list->widget.topPad = top_pad;
	list_col = ui_ListColumnCreate(UIListTextCallback, "Info Window Data", (intptr_t) emInfoWinOptionsListDisplay, NULL);
	ui_ListColumnSetWidth(list_col, true, 1);
	ui_ListAppendColumn(list, list_col);
	ui_TabAddChild(tab, list);
	eaPush(&options->grayables, (UIWidget*) list);
	options->selectData = list;
	button = ui_ButtonCreate("Clear", 0, elUINextY(button) + 5, emInfoWinOptionsClear, options);
	ui_WidgetSetPositionEx((UIWidget*) button, 0, button->widget.y, 0.5, 0, UIBottomLeft);
	ui_TabAddChild(tab, button);
	eaPush(&options->grayables, (UIWidget*) button);
	list->widget.bottomPad = elUINextY(button) + 5;

	// shuttling buttons
	button = ui_ButtonCreateImageOnly("eui_arrow_large_right", 0, 0, emInfoWinOptionsShuttleRight, options);
	ui_WidgetSetPositionEx((UIWidget*) button, -button_size / 2, -button_size, 0.45, 0.4, UITopLeft);
	ui_WidgetSetDimensions((UIWidget*) button, button_size, button_size);
	ui_ButtonSetImageStretch( button, true );
	ui_TabAddChild(tab, button);
	eaPush(&options->grayables, (UIWidget*) button);
	button = ui_ButtonCreateImageOnly("eui_arrow_large_left", 0, 0, emInfoWinOptionsShuttleLeft, options);
	ui_WidgetSetPositionEx((UIWidget*) button, -button_size / 2, button_size, 0.45, 0.4, UITopLeft);
	ui_WidgetSetDimensions((UIWidget*) button, button_size, button_size);
	ui_ButtonSetImageStretch( button, true );
	ui_TabAddChild(tab, button);
	eaPush(&options->grayables, (UIWidget*) button);

	// reordering buttons
	button = ui_ButtonCreateImageOnly("eui_arrow_large_up", 0, 0, emInfoWinOptionsMoveTop, options);
	ui_WidgetSetPositionEx((UIWidget*) button, -button_size / 2, -60, 0.95, 0.4, UITopLeft);
	ui_WidgetSetDimensions((UIWidget*) button, button_size, button_size);
	ui_ButtonSetImageStretch( button, true );
	ui_TabAddChild(tab, button);
	eaPush(&options->grayables, (UIWidget*) button);
	button = ui_ButtonCreateImageOnly("eui_arrow_large_up", 0, 0, emInfoWinOptionsMoveUp, options);
	ui_WidgetSetPositionEx((UIWidget*) button, -small_button_size / 2, -27, 0.95, 0.4, UITopLeft);
	ui_WidgetSetDimensions((UIWidget*) button, small_button_size, small_button_size);
	ui_ButtonSetImageStretch( button, true );
	ui_TabAddChild(tab, button);
	eaPush(&options->grayables, (UIWidget*) button);
	button = ui_ButtonCreateImageOnly("eui_arrow_large_down", 0, 0, emInfoWinOptionsMoveDown, options);
	ui_WidgetSetPositionEx((UIWidget*) button, -small_button_size / 2, 13, 0.95, 0.4, UITopLeft);
	ui_WidgetSetDimensions((UIWidget*) button, small_button_size, small_button_size);
	ui_ButtonSetImageStretch( button, true );
	ui_TabAddChild(tab, button);
	eaPush(&options->grayables, (UIWidget*) button);
	button = ui_ButtonCreateImageOnly("eui_arrow_large_down", 0, 0, emInfoWinOptionsMoveBottom, options);
	ui_WidgetSetPositionEx((UIWidget*) button, -button_size / 2, 40, 0.95, 0.4, UITopLeft);
	ui_WidgetSetDimensions((UIWidget*) button, button_size, button_size);
	ui_ButtonSetImageStretch( button, true );
	ui_TabAddChild(tab, button);
	eaPush(&options->grayables, (UIWidget*) button);

	// gray items as necessary
	emInfoWinOptionsEnabledToggle(options->enabled, options);

	// set the data pointer for the UI
	emOptionsTabSetData(emTab, options);

	// cleanup
	StructDestroy(parse_EMInfoWin, displayed_entries);

	return true;
}

static void emInfoWinOptionsCancel(EMOptionsTab *emTab, EMInfoWinOptions *options)
{
	// restore old values for the info window position/dimensions
	EditorPrefStoreInt("Editor Manager", "Info Win", "X", options->oldX);
	EditorPrefStoreInt("Editor Manager", "Info Win", "Y", options->oldY);
	EditorPrefStoreInt("Editor Manager", "Info Win", "Width", options->oldWidth);
	EditorPrefStoreInt("Editor Manager", "Info Win", "Height", options->oldHeight);

	// free allocated memory
	eaDestroy(options->availData->peaModel);
	free(options->availData->peaModel);
	ui_ListSetModel(options->availData, NULL, NULL);
	eaDestroy(options->selectData->peaModel);
	free(options->selectData->peaModel);
	ui_ListSetModel(options->selectData, NULL, NULL);

	free(options);
}

static void emInfoWinOptionsOk(EMOptionsTab *emTab, EMInfoWinOptions *options)
{
	EMInfoWin *displayed_entries;
	int i;

	// set the options
	EditorPrefStoreInt("Editor Manager", "Info Win", "Enabled", ui_CheckButtonGetState(options->enabled));
	EditorPrefStoreInt("Editor Manager", "Info Win", "In Front", ui_CheckButtonGetState(options->alwaysInFront));

	displayed_entries = StructCreate(parse_EMInfoWin);
	for (i = 0; i < eaSize(options->selectData->peaModel); i++)
		eaPush(&displayed_entries->entry_indexes, StructAllocString((*(EMInfoWinEntry***)options->selectData->peaModel)[i]->indexed_name));

	// save the contents
	EditorPrefStoreStruct(options->active_editor->editor_name, "Info Win", "Contents", parse_EMInfoWin, displayed_entries);

	// free allocated memory
	eaDestroy(options->availData->peaModel);
	free(options->availData->peaModel);
	ui_ListSetModel(options->availData, NULL, NULL);
	eaDestroy(options->selectData->peaModel);
	free(options->selectData->peaModel);
	ui_ListSetModel(options->selectData, NULL, NULL);
	StructDestroy(parse_EMInfoWin, displayed_entries);

	free(options);
}

/********************
* MESSAGE OF THE DAY OPTIONS
********************/

void emMotDStorePrefs(void)
{
	bool boolValue;

	boolValue = emMotDGetShowInViewport();
	EditorPrefStoreInt("Editor", "Options", "ShowInViewport", boolValue);
}

static bool s_bDefaultShowInViewport;

void emMotDLoadPrefs(void)
{
	bool boolValue;

	s_bDefaultShowInViewport = emMotDGetShowInViewport();
	boolValue = EditorPrefGetInt("Editor", "Messages", "ShowInViewport", s_bDefaultShowInViewport);
	emMotDSetShowInViewport(boolValue);

	// The reason for the save after the load is because the default values are read from static memory before attempting to load.
	// This makes sure we do not accidentally load and have different defaults in place.
	emMotDStorePrefs();
}

typedef struct EMMessagesOptions
{
	UICheckButton *show_in_viewport;
	UILabel *instructions;
	UIList *message_list;
} EMMessagesOptions;

static void emMessagesOptionsShowInViewportToggle(UICheckButton *button, EMMessagesOptions *options)
{
	emMotDSetShowInViewport(ui_CheckButtonGetState(button));
}

static void emMessageOfTheDayCellActivated(UIList *pList, S32 iColumn, S32 iRow, F32 fMouseX, F32 fMouseY, CBox *pBox, EMMessagesOptions *pOptions)
{
	if(iRow >= 0 && iRow < eaSize(pList->peaModel))
	{
		EMMessageOfTheDay* motd = ((EMMessageOfTheDay**) *pList->peaModel)[iRow];
		motd->never_show_again = !motd->never_show_again;

		EditorPrefStoreInt("Editor", "Messages.NeverShowAgain", motd->keyname, motd->never_show_again);
	}

	ui_ListCellClickedDefault(pList, iColumn, iRow, fMouseX, fMouseY, pBox, NULL);
}

static int cmpMessages(const EMMessageOfTheDay** lhs, const EMMessageOfTheDay** rhs)
{
	return -((*lhs)->last_shown_timestamp - (*rhs)->last_shown_timestamp); // we're sorting be most recent first, hence the negation
}

static void emMessageOfTheDayDrawHide(UIList *list, UIListColumn *column, S32 row, UserData userData, char **output)
{
	bool selected = ui_ListIsSelected(list, NULL, row);

	estrClear(output);
	if(selected)
		estrConcatf(output, "<font color=White>");
	estrConcatf(output, "%s", ((EMMessageOfTheDay**) *list->peaModel)[row]->never_show_again ? "True" : "False");
	if(selected)
		estrConcatf(output, "</font>");
}

typedef struct tm TimeStruct;

static void emMessageOfTheDayDrawShown(UIList *list, UIListColumn *column, S32 row, UserData userData, char **output)
{
	char date_buf[32];
	TimeStruct time_struct;
	EMMessageOfTheDay *motd = ((EMMessageOfTheDay**)*list->peaModel)[row];

	bool selected = ui_ListIsSelected(list, NULL, row);

	if(motd->last_shown_timestamp != 0)
	{
		_localtime32_s(&time_struct, &motd->last_shown_timestamp);
		strftime(date_buf, sizeof(date_buf), "%x", &time_struct);
	}
	else
		snprintf_s(date_buf, sizeof(date_buf), "Never");

	estrClear(output);
	if(selected)
		estrConcatf(output, "<font color=White>");
	estrConcatf(output, "%s", date_buf);
	if(selected)
		estrConcatf(output, "</font>");
}

static void emMessageOfTheDayDrawCreated(UIList *list, UIListColumn *column, S32 row, UserData userData, char **output)
{
	char date_buf[32];
	TimeStruct time_struct;

	bool selected = ui_ListIsSelected(list, NULL, row);

	_localtime32_s(&time_struct, &((EMMessageOfTheDay**) *list->peaModel)[row]->registration_timestamp);
	strftime(date_buf, sizeof(date_buf), "%x", &time_struct);

	estrClear(output);
	if(selected)
		estrConcatf(output, "<font color=White>");
	estrConcatf(output, "%s", date_buf);
	if(selected)
		estrConcatf(output, "</font>");
}

static void emMessageOfTheDayDrawText(UIList *list, UIListColumn *column, S32 row, UserData userData, char **output)
{
	bool selected = ui_ListIsSelected(list, NULL, row);

	estrClear(output);
	if(selected)
		estrConcatf(output, "<font color=White>");
	estrConcatf(output, "%s", ((EMMessageOfTheDay**) *list->peaModel)[row]->text);
	if(selected)
		estrConcatf(output, "</font>");
}

static void emMessageOfTheDayDrawEditors(UIList *list, UIListColumn *column, S32 row, UserData userData, char **output)
{
	EMMessageOfTheDay* motd = ((EMMessageOfTheDay**) *list->peaModel)[row];
	int i, count = eaSize(&motd->relevant_editors);
	bool selected = ui_ListIsSelected(list, NULL, row);

	estrClear(output);
	if(selected)
		estrConcatf(output, "<font color=White>");
	if(count > 0)
	{
		for(i = 0; i < count; i++)
		{
			if(i > 0)
				estrConcatf(output, ", ");
			estrConcatf(output, "%s", motd->relevant_editors[i]);
		}
	}
	else
		estrConcatf(output, "All");
	if(selected)
		estrConcatf(output, "</font>");
}

static void emMessageOfTheDayDrawGroups(UIList *list, UIListColumn *column, S32 row, UserData userData, char **output)
{
	EMMessageOfTheDay* motd = ((EMMessageOfTheDay**) *list->peaModel)[row];
	int i, count = eaSize(&motd->relevant_groups);
	bool selected = ui_ListIsSelected(list, NULL, row);

	estrClear(output);
	if(selected)
		estrConcatf(output, "<font color=White>");
	if(count > 0)
	{
		for(i = 0; i < count; i++)
		{
			if(i > 0)
				estrConcatf(output, ", ");
			estrConcatf(output, "%s", motd->relevant_groups[i]);
		}
	}
	else
		estrConcatf(output, "All");
	if(selected)
		estrConcatf(output, "</font>");
}

static bool emMessagesOptionsLoad(EMOptionsTab *options_tab, UITab *tab)
{
	EMMessagesOptions *options = NULL;
	UICheckButton *check_button = NULL;
	UIList *list = NULL;
	UILabel *label = NULL;
	int top = 0;

	options = calloc(1, sizeof(*options));

	check_button = ui_CheckButtonCreate(5, top, "Show in viewport", emMotDGetShowInViewport());
	options->show_in_viewport = check_button;
	ui_CheckButtonSetToggledCallback(check_button, emMessagesOptionsShowInViewportToggle, options);
	ui_TabAddChild(tab, check_button);
	top = elUINextY(check_button);

	label = ui_LabelCreate("Double-click Messages below to toggle their Hide status", 5, top);
	options->instructions = label;
	ui_TabAddChild(tab, label);
	top = elUINextY(label);

	list = ui_ListCreate(NULL, NULL, 15);
	options->message_list = list;
	list->fRowHeight = 100;
	ui_ListSetCellActivatedCallback(list, emMessageOfTheDayCellActivated, options);
	ui_WidgetSetPositionEx(UI_WIDGET(list), 5, top, 0, 0, UITopLeft);
	ui_WidgetSetDimensionsEx(UI_WIDGET(list), 1, 1, UIUnitPercentage, UIUnitPercentage);
	eaQSortG(em_data.message_of_the_day.registered_messages, cmpMessages);
	ui_ListSetModel(list, NULL, &em_data.message_of_the_day.registered_messages);
	ui_ListAppendColumn(list, ui_ListColumnCreate(UIListSMFCallback, "Hide", (intptr_t)emMessageOfTheDayDrawHide, (UserData)0));
	ui_ListAppendColumn(list, ui_ListColumnCreate(UIListSMFCallback, "Shown", (intptr_t)emMessageOfTheDayDrawShown, (UserData)2));
	ui_ListAppendColumn(list, ui_ListColumnCreate(UIListSMFCallback, "Created", (intptr_t)emMessageOfTheDayDrawCreated, (UserData)2));
	ui_ListAppendColumn(list, ui_ListColumnCreate(UIListSMFCallback, "Text", (intptr_t)emMessageOfTheDayDrawText, (UserData)3));
	ui_ListAppendColumn(list, ui_ListColumnCreate(UIListSMFCallback, "Editors", (intptr_t)emMessageOfTheDayDrawEditors, (UserData)4));
	ui_ListAppendColumn(list, ui_ListColumnCreate(UIListSMFCallback, "Groups", (intptr_t)emMessageOfTheDayDrawGroups, (UserData)5));
	list->eaColumns[0]->fWidth = 40;
	list->eaColumns[1]->fWidth = 60;
	list->eaColumns[2]->fWidth = 60;
	list->eaColumns[3]->fWidth = 400;
	list->eaColumns[4]->fWidth = 80;
	list->eaColumns[5]->fWidth = 80;
	ui_ListSetSelectedRow(list, eaSize(&em_data.message_of_the_day.registered_messages) > 0 ? 0 : -1);
	ui_TabAddChild(tab, list);
	top = elUINextY(list);

	emOptionsTabSetData(options_tab, options);

	return true;
}

static void emMessagesOptionsCancel(EMOptionsTab *tab, EMMessagesOptions *options)
{
	emMotDLoadPrefs();

	free(options);
}

static void emMessagesOptionsOk(EMOptionsTab *tab, EMMessagesOptions *options)
{
	emMotDStorePrefs();

	free(options);
}

/********************
* MAIN
********************/
void emOptionsInit(void)
{
	eaPush(&em_data.options.tabs, emOptionsTabCreate("Camera", emCameraOptionsLoad, emCameraOptionsOk, emCameraOptionsCancel));
	eaPush(&em_data.options.tabs, emOptionsTabCreate("Info Window", emInfoWinOptionsLoad, emInfoWinOptionsOk, emInfoWinOptionsCancel));
	eaPush(&em_data.options.tabs, emOptionsTabCreate("Messages", emMessagesOptionsLoad, emMessagesOptionsOk, emMessagesOptionsCancel));

	emMotDLoadPrefs();
}

#endif // NO_EDITORS
