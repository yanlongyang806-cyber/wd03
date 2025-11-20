/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "EditorManagerUI.h"

#ifndef NO_EDITORS
GCC_SYSTEM

#include "EditorPrefs.h"
#include "EString.h"
#include "crypt.h"
#include "EditLibUIUtil.h"
#include "FolderCache.h"
#include "Color.h"

#include "GfxHeadshot.h"
#include "GfxTexAtlas.h"
#include "GfxDebug.h"
#include "GraphicsLib.h"
#include "inputLib.h"
#include "inputMouse.h"

#include "UIAuxDevice.h"
#include "EditorManagerUIInfoWin.h"
#include "EditorManagerUIMotD.h"
#include "EditorManagerPrivate.h"
#include "EditorManagerUI_h_ast.h"
#include "gclUIGen.h"

#include "CostumeCommonLoad.h"
#include "ObjectLibrary.h"
#include "WorldGrid.h"
#include "ResourceManagerUI.h"
#include "sysutil.h"
#include "InputLib.h"

#define MIN_SIDEBAR_WIDTH 350
#define MAX_SIDEBAR_WIDTH (g_ui_State.screenWidth - 350)
#define DOC_TAB_HEIGHT 20
#define MIN_PANE_THRESH 20
#define EM_MIN_PANE_WINS_PER_ROW 10
#define EM_TRANS_SKIN_ALPHA 0x00000088

#define METableEditorList "Algo Pet Editor, Critter Editor, Critter Override Editor, CritterGroup Editor, DonationTask Editor, Game Progression Node Editor, GroupProject Editor, GroupProjectNumeric Editor, GroupProjectUnlock Editor, Item Assignment Editor, Item Gen Editor, Item Editor, Item Power Editor, MicroTransaction Editor, MissionSet Editor, PlayerStatDef Editor, PowerStore Editor, Powers Editor, Queue Editor, Reward Table Editor, Store Editor"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

typedef struct ResourcePreviewTexture
{
	BasicTexture* texture;
	U32 completeTime;
} ResourcePreviewTexture;

static BasicTexture* resourceDefaultPreview;

static EMPicker objectPicker;
static StashTable objectPickerPreviews;
static StashTable objectPickerLargePreviews;

static StashTable costumePickerPreviews;

/*******************
* FORWARD DECLARATIONS
*******************/
typedef struct EMMinimizedWin EMMinimizedWin;

static void emWinRestore(EMMinimizedWin *win);
static void emModeSelectorRefresh(void);
static void emEditorDocFocus(EMEditorDoc *doc, EMEditor *editor);
static void objectPickerInit(EMPicker *picker);
static void objectPickerPreview(EMPicker *picker, void *selected_data, ParseTable *parse_info, BasicTexture** out_tex, Color* out_mod_color);


/*******************
* DIALOGS
*******************/
static bool emDialogReloadPromptCancel(UIWidget *widget, UIWindow *window)
{
	EMEditorDoc *doc = em_data.modal_data.doc;
	EMFile **changed_files = NULL;
	int i;

	emDocGetChangedFiles(doc, &changed_files, true);
	for (i = 0; i < eaSize(&changed_files); i++)
		emDocUpdateFile(doc, changed_files[i]);

	elUIWindowClose(NULL, window);
	doc->saved = false;
	em_data.modal_data.active = false;
	return true;
}

static void emDialogReloadPromptOk(UIButton *button, UIWindow *window)
{
	emQueueFunctionCall(emReloadDoc, em_data.modal_data.doc);
	
	ui_WindowSetCloseCallback(window, NULL, NULL);
	elUIWindowClose(NULL, window);
	em_data.modal_data.active = false;
}

/******
* This function creates a reload prompt dialog, asking the user whether they wish to reload the 
* document.  If they click "Ok", then the document reloads using the editor's reload function.  Otherwise,
* the document keeps its current changes.  In both cases, the file association timestamps are updated so
* that the reload prompt doesn't appear again until the file changes on disk again.
* PARAMS:
*   doc - EMEditorDoc to prompt for reload
******/
void emDialogReloadPrompt(EMEditorDoc *doc)
{
	UIWindow *win;
	UILabel *label;
	int i;
	char *text = NULL;
	EMFile **changed_files = NULL;
	
	if (doc != em_data.current_doc || em_data.modal_data.active)
		return;

	emDocGetChangedFiles(doc, &changed_files, true);
	if (eaSize(&changed_files) == 0)
		return;

	win = ui_WindowCreate("Confirm reload", 0, 0, 0, 0);

	estrStackCreate(&text);

	estrPrintf(&text, "The following files have changed on disk:");
	label = ui_LabelCreate(text, 5, 5);
	ui_WindowAddChild(win, label);

	for (i = 0; i < eaSize(&changed_files); i++)
	{
		estrPrintf(&text, "%s", changed_files[i]->filename);
		label = ui_LabelCreate(text, 15, elUINextY(label));
		ui_WindowAddChild(win, label);
	}

	estrPrintf(&text, "Would you like to reload the document?");
	label = ui_LabelCreate(text, 5, elUINextY(label));
	ui_WindowAddChild(win, label);

	win->widget.width = elUIGetEndX(win->widget.children) + 5;
	win->widget.height = elUIGetEndY(win->widget.children) + 30;

	elUIAddCancelOkButtons(win, emDialogReloadPromptCancel, win, emDialogReloadPromptOk, win);
	ui_WindowSetCloseCallback(win, emDialogReloadPromptCancel, win);
	elUICenterWindow(win);
	ui_WidgetSetFamily(UI_WIDGET(win), UI_FAMILY_EDITOR);
	ui_WindowSetModal(win, true);
	ui_WindowShow(win);
	em_data.modal_data.doc = doc;
	em_data.modal_data.active = true;

	estrDestroy(&text);
	eaDestroy(&changed_files);
}

/******
* This function refreshes the progress bar window for a document.
* PARAMS:
*   doc - EMEditorDoc whose progress is to be refreshed
******/
void emDialogProgressRefresh(EMEditorDoc *doc)
{

	if (em_data.current_doc->process->progress >= 1)
		ui_WindowHide(em_data.progress.window);
	else
	{
		ui_WindowShow(em_data.progress.window);
		ui_ProgressBarSet(em_data.progress.progress, doc->process->progress);
	}
}


/*******************
* CAMERA
*******************/
/******
* This function applies the current preferences to the freecam's parameters.
******/
void emFreecamApplyPrefs(void)
{
	em_data.worldcam->start_speed = EditorPrefGetFloat("Editor Manager", "Camera", "Start Speed", em_data.worldcam->start_speed);
	em_data.worldcam->max_speed = EditorPrefGetFloat("Editor Manager", "Camera", "Max Speed", em_data.worldcam->max_speed);
	em_data.worldcam->acc_delay = EditorPrefGetFloat("Editor Manager", "Camera", "Accel Delay", em_data.worldcam->acc_delay);
	em_data.worldcam->acc_rate = EditorPrefGetFloat("Editor Manager", "Camera", "Accel Rate", em_data.worldcam->acc_rate);

	if (em_data.current_editor && (em_data.current_editor->hide_world || em_data.current_editor->force_editor_cam) && em_data.current_editor->use_em_cam_keybinds)
	{
		em_data.current_editor->camera->start_speed = em_data.worldcam->start_speed;
		em_data.current_editor->camera->max_speed = em_data.worldcam->max_speed;
		em_data.current_editor->camera->acc_delay = em_data.worldcam->acc_delay;
		em_data.current_editor->camera->acc_rate = em_data.worldcam->acc_rate;
	}
}

/******
* This function sets the freecam camera control mode.
* PARAMS:
*   freecam - bool indicating whether world camera control should be in freecam mode (true) or standard
*             cam mode (false)
******/
void emSetFreecam(bool freecam)
{
	EMEditorDoc *doc = emGetActiveEditorDoc();

	emCameraInit(&em_data.worldcam);
	gfxCameraSwitchMode(em_data.worldcam, freecam);

	if (em_data.current_editor && (em_data.current_editor->hide_world || em_data.current_editor->force_editor_cam) && em_data.current_editor->use_em_cam_keybinds)
		gfxCameraSwitchMode(em_data.current_editor->camera, freecam);
	emFreecamApplyPrefs();
	emUICameraButtonRefresh();
	EditorPrefStoreInt("Editor Manager", "Camera", "Freecam", em_data.worldcam->mode_switch);

	if (!doc || ((doc->editor->hide_world || doc->editor->force_editor_cam) && !doc->editor->use_em_cam_keybinds) || em_data.worldcam->multi_drag)
		return;

	if (freecam)
	{
		keybind_PopProfileEx(em_data.standardcam_profile, InputBindPriorityDevelopment);
		keybind_PushProfileEx(em_data.freecam_profile, InputBindPriorityDevelopment);
	}
	else
	{
		keybind_PopProfileEx(em_data.freecam_profile, InputBindPriorityDevelopment);
		keybind_PushProfileEx(em_data.standardcam_profile, InputBindPriorityDevelopment);
	}
}

AUTO_COMMAND ACMD_NAME("EM.Freecam");
void emCmdToggleFreecam(void)
{
	emSetFreecam(!em_data.worldcam->mode_switch);
}

AUTO_COMMAND ACMD_NAME("EM.StandardCamZoomStepIn");
void emCmdStandardCamZoomStepIn(void)
{
	GfxCameraController *current_camera;
	float step = EditorPrefGetFloat("Editor Manager", "Camera", "Zoom Step", 20);
	if (em_data.current_editor && (em_data.current_editor->hide_world || em_data.current_editor->force_editor_cam) && em_data.current_editor->use_em_cam_keybinds)
		current_camera = em_data.current_editor->camera;
	else
		current_camera = em_data.worldcam; 
	current_camera->zoomstep = (inpLevelPeek(INP_SHIFT) ? -3 * step : -step);
}

AUTO_COMMAND ACMD_NAME("EM.StandardCamZoomStepOut");
void emCmdStandardCamZoomStepOut(void)
{
	GfxCameraController *current_camera;
	float step = EditorPrefGetFloat("Editor Manager", "Camera", "Zoom Step", 20);
	if (em_data.current_editor && (em_data.current_editor->hide_world || em_data.current_editor->force_editor_cam) && em_data.current_editor->use_em_cam_keybinds)
		current_camera = em_data.current_editor->camera;
	else
		current_camera = em_data.worldcam; 
	current_camera->zoomstep = (inpLevelPeek(INP_SHIFT) ? 3 * step : step);
}

/*******************
* SIDEBAR
*******************/
/******
* This function hides or shows the panel sidebar pane.
* PARAMS:
*   show - bool indicating whether to hide or show the sidebar
******/
void emSidebarShow(bool show)
{
	em_data.sidebar.show = show;
	ui_WidgetRemoveFromGroup(UI_WIDGET(em_data.sidebar.pane));
	ui_WidgetRemoveFromGroup(UI_WIDGET(em_data.sidebar.hidden_pane));

	if (show)
		ui_PaneWidgetAddToDevice(UI_WIDGET(em_data.sidebar.pane), NULL);
	else if (!em_data.sidebar.disable)
		ui_PaneWidgetAddToDevice(UI_WIDGET(em_data.sidebar.hidden_pane), NULL);

	// Store sidebar state for this workspace as a preference
	EditorPrefStoreInt("Editor Manager", em_data.active_workspace ? em_data.active_workspace->name : "No Workspace", "Sidebar Open", em_data.sidebar.show);
}

static void emSidebarHideShowClicked(UIButton *button, void *show)
{
	if (!em_data.sidebar.resize_mode)
		emSidebarShow(!!show);
}

static void emSidebarExpandDrag(UIButton *button, UserData unused)
{
	int x, y;

	ui_DragStart(UI_WIDGET(button), "em_sidebar_expand", NULL, atlasLoadTexture("eui_pointer_arrows_horiz"));
	mousePos(&x, &y);
	em_data.sidebar.resize_mode = 1;
	em_data.sidebar.resize_start_x = x;
	em_data.sidebar.resize_start_width = em_data.sidebar.pane->widget.width;
}

/*******************
* WINDOW MANAGEMENT
*******************/
typedef struct EMMinimizedWin
{
	UIWindow *win;
	UIButton *button;

	// cached data
	int x, y, scale;
	UIDirection offset_from;
	UIActivationFunc clicked_func, mouse_over_func, mouse_leave_func;
	void *clicked_data, *mouse_over_data, *mouse_leave_data;
	bool movable;
	UISkin *skin;
} EMMinimizedWin;

/******
* This disables the minimized window preview by hiding the preview window.
* PARAMS:
*   min_win - EMMinimizedWin to unpreview
******/
static void emWinUnpreview(EMMinimizedWin *min_win)
{
	min_win->win->show = false;
}

/******
* This previews the minimized window in a slightly transparent form.
* PARAMS:
*   min_win - EMMinimizedWin to preview
******/
static void emWinPreview(EMMinimizedWin *min_win)
{
	min_win->win->show = true;
}

/******
* This function refreshes the minimized window pane by placing each of the buttons adjacent to each
* other, left-to-right, then top-to-bottom.
******/
static void emMinWinPaneRefresh(void)
{
	int i = 0, col = 0, row = 0;
	int min_win_count = eaSize(&em_data.minimized_windows.active_min_pane->widget.children);

	// move the buttons around to the correct spot in the order in which they were minimized
	while (i < min_win_count)
	{
		// rearrange a row of buttons at a time
		for (col = 0; col < MIN(min_win_count - row * EM_MIN_PANE_WINS_PER_ROW, EM_MIN_PANE_WINS_PER_ROW); col++)
		{
			UIWidget *min_win = em_data.minimized_windows.active_min_pane->widget.children[i + col];
			min_win->x = 0;
			min_win->y = min_win->height * row;
			min_win->xPOffset = ((float) col) / ((float) EM_MIN_PANE_WINS_PER_ROW);
			min_win->width = 1.0f / ((float) EM_MIN_PANE_WINS_PER_ROW);
			min_win->widthUnit = UIUnitPercentage;
		}
		i += EM_MIN_PANE_WINS_PER_ROW;
		row++;
	}

	// if there are no minimized windows, ensure the minized window pane does not appear
	if (min_win_count == 0)
		em_data.minimized_windows.active_min_pane->widget.height = 0;
	else
		em_data.minimized_windows.active_min_pane->widget.height = row * em_data.minimized_windows.active_min_pane->widget.children[0]->height + 5;
}

static void emMinWinClicked(UIWidget *unused, EMMinimizedWin *min_win)
{
	emQueueFunctionCall(emWinRestore, min_win);
}

static void emMinWinMouseOver(UIWindow *win, EMMinimizedWin *min_win)
{
	emQueueFunctionCall(emWinPreview, min_win);
}

static void emMinWinMouseLeave(UIWindow *win, EMMinimizedWin *min_win)
{
	emQueueFunctionCall(emWinUnpreview, min_win);
}

static void emWinMinimizeClicked(UIButton *button, UIWindow *win)
{
	emQueueFunctionCall(emWinMinimize, win);
}

static void createNewWindow(UIMenuItem *pItem, UserData pDummy)
{
	ui_AuxDeviceCreate(NULL, NULL, NULL);
}

static bool emWinClose(UIWindow *window, UserData unused)
{
	emWinHide(window);
	return false;
}


/******
* This function restores the specified EMMinimizedWin.
* PARAMS:
*   win - EMMinimizedWin to restore
******/
static void emWinRestore(EMMinimizedWin *win)
{
	// restore cached data and move window to viewport
	assert(eaFind(&em_data.minimized_windows.active_min_pane->widget.children, (UIWidget*)win->button) != -1);
	win->win->widget.x = win->x;
	win->win->widget.y = win->y;
	win->win->widget.scale = win->scale;
	win->win->widget.offsetFrom = win->offset_from;
	ui_WindowSetMovable(win->win, win->movable);
	ui_WidgetSkin(UI_WIDGET(win->win), win->skin);
	ui_SetActive(UI_WIDGET(win->win), true);

	// remove the button from the minimized pane
	ui_WidgetRemoveChild(UI_WIDGET(em_data.minimized_windows.active_min_pane), UI_WIDGET(win->button));
	ui_ButtonFreeInternal(win->button);

	// show the original window again
	emWinShow(win->win);
	emMinWinPaneRefresh();

	// cleanup
	free(win);
}

/******
* This function minimizes the specified window to the editor manager's minimized window pane.
* PARAMS:
*   win - UIWindow to minimize
******/
void emWinMinimize(UIWindow *win)
{
	// create minimized window and cache data
	EMMinimizedWin *min_win = calloc(1, sizeof(EMMinimizedWin));

	min_win->win = win;
	min_win->x = win->widget.x;
	min_win->y = win->widget.y;
	min_win->scale = win->widget.scale;
	min_win->offset_from = win->widget.offsetFrom;
	min_win->clicked_func = win->widget.onFocusF;//clickedF;
	min_win->clicked_data = win->widget.onFocusData;//clickedData;
	min_win->mouse_over_func = win->widget.mouseOverF;
	min_win->mouse_over_data = win->widget.mouseOverData;
	min_win->mouse_leave_func = win->widget.mouseLeaveF;
	min_win->mouse_leave_data = win->widget.mouseLeaveData;
	min_win->movable = win->movable;
	min_win->skin = win->widget.pOverrideSkin;

	// hide the existing window
	emWinHide(win);

	// create the button for the minimized pane
	min_win->button = ui_ButtonCreate(ui_WidgetGetText(UI_WIDGET(win)), 0, 0, NULL, NULL);
	min_win->button->widget.scale = emGetSidebarScale();
	ui_ButtonSetCallback(min_win->button, emMinWinClicked, min_win);
	ui_WidgetSetMouseOverCallback(UI_WIDGET(min_win->button), emMinWinMouseOver, min_win);
	ui_WidgetSetMouseLeaveCallback(UI_WIDGET(min_win->button), emMinWinMouseLeave, min_win);
	ui_WindowSetMovable(win, false);
	ui_WidgetSkin(UI_WIDGET(win), em_data.transparent_skin);
	ui_SetActive(UI_WIDGET(win), false);
	ui_WidgetAddChild(UI_WIDGET(em_data.minimized_windows.active_min_pane), UI_WIDGET(min_win->button));
	emMinWinPaneRefresh();
}

/******
* This function hides the specified window.
* PARAMS:
*   window - UIWindow to hide
******/
void emWinHide(UIWindow *window)
{
	window->show = false;
	ui_WidgetUnfocusAll(UI_WIDGET(window));
}

/******
* This function shows the specified window, restoring it from the minimized pane if necessary.
* PARAMS:
*   window - UIWindow to show
******/
void emWinShow(UIWindow *window)
{
	int i;

	// if this window is in the minimized pane, restore it
	for (i = 0; i < eaSize(&em_data.minimized_windows.active_min_pane->widget.children); i++)
	{
		UIButton *button = (UIButton*) em_data.minimized_windows.active_min_pane->widget.children[i];
		EMMinimizedWin *min_win = button->clickedData;
		if (min_win->win == window)
		{
			emWinRestore(min_win);
			return;
		}
	}

	// otherwise, just show it
	ui_WindowShow(window);
	ui_SetFocus(window);
}

/******
* This function hides all windows associated with the document.
* PARAMS:
*   doc - EMEditorDoc whose windows are to be hidden
******/
void emWinHideAll(EMEditorDoc *doc)
{
	if (doc)
	{
		int i;
		FORALL_DOCWINDOWS(doc, i)
		{
			if (DOCWINDOW(doc, i) != doc->primary_ui_window)
				emWinHide(DOCWINDOW(doc, i));
		}
	}
	// TODO: deal with non-document-specific windows (i.e. editor and EM windows)
}

/******
* This function shows all windows associated with a specified document.
* PARAMS:
*   doc - EMEditorDoc - whose windows are to be shown
******/
void emWinShowAll(EMEditorDoc *doc)
{
	if (doc)
	{
		int i;
		FORALL_DOCWINDOWS(doc, i)
			emWinShow(DOCWINDOW(doc, i));
	}
	// TODO: deal with non-document-specific windows (i.e. editor and EM windows)(
}

/******
* This function creates a minimized window pane that can be used by any of the various editor modes.
* RETURNS:
*   UIPane to hold minimized windows
******/
UIPane *emMinPaneCreate(void)
{
	UIPane *min_pane = ui_PaneCreate(0, 0, 1, 0, UIUnitPercentage, UIUnitFixed, 0);
	min_pane->widget.offsetFrom = UIBottomLeft;
	ui_WidgetSetFamily(UI_WIDGET(min_pane), UI_FAMILY_EDITOR);
	ui_PaneSetStyle(min_pane, "AssetManager_MinimizedPane", true, true);
	return min_pane;
}

/******
* This command shows all of the active document's windows.
******/
AUTO_COMMAND ACMD_NAME("EM.ShowAllWins");
void emWinActiveDocShowAll(void)
{
	EMEditorDoc *doc = emGetActiveEditorDoc();
	if (doc)
		emWinShowAll(doc);
}

/******
* This command hides all of the active document's windows.
******/
AUTO_COMMAND ACMD_NAME("EM.HideAllWins");
void emWinActiveDocHideAll(void)
{
	EMEditorDoc *doc = emGetActiveEditorDoc();
	if (doc)
		emWinHideAll(doc);
}


/*******************
* DOCUMENT MANAGEMENT
*******************/
static void emDocTabClicked(UITabGroup *group, void *unused)
{
	UITab *tab;
	if (tab = ui_TabGroupGetActive(group))
		emQueueFunctionCall2(emSetActiveEditorDoc, tab->pContextData, NULL);
}

static void emDocTabRightClickSave(void *unused, EMEditorDoc *doc)
{
	if (!doc)
		return;
	emQueueFunctionCallStatus(NULL, NULL, emSaveDoc, doc, -1);
}

static void emDocTabRightClickSaveAs(void *unused, EMEditorDoc *doc)
{
	if (!doc)
		return;
	emQueueFunctionCallStatus(NULL, NULL, emSaveDocAs, doc, -1);
}

static void emDocTabRightClickCopyName(void *unused, EMEditorDoc *doc)
{
	if (!doc)
		return;
	winCopyToClipboard(doc->doc_name);
}

static void emDocTabRightClickClose(void *unused, EMEditorDoc *doc)
{
	if (!doc)
		return;
	emQueueFunctionCall(emCloseDoc, doc);
}

static void emDocTabRightClickMoveWorkspace(UIMenuItem *item, EMEditorDoc *doc)
{
	EMWorkspace *workspace = item->data.voidPtr;

	emQueueFunctionCall2(emMoveDocToWorkspace, doc, workspace->name);
}

static void emDocTabRightClick(UITab *clickedTab, EMEditorDoc *doc)
{
	if (em_data.title.tabs_menu)
		ui_MenuClear(em_data.title.tabs_menu);
	else
		em_data.title.tabs_menu = ui_MenuCreate("");

	ui_MenuAppendItem(em_data.title.tabs_menu, ui_MenuItemCreate("Save", UIMenuCallback, emDocTabRightClickSave, doc, NULL));
	ui_MenuAppendItem(em_data.title.tabs_menu, ui_MenuItemCreate("Save as", UIMenuCallback, emDocTabRightClickSaveAs, doc, NULL));
	ui_MenuAppendItem(em_data.title.tabs_menu, ui_MenuItemCreate("Copy name", UIMenuCallback, emDocTabRightClickCopyName, doc, NULL));
	ui_MenuAppendItem(em_data.title.tabs_menu, ui_MenuItemCreate("Close", UIMenuCallback, emDocTabRightClickClose, doc, NULL));

	assert(doc->editor);
	if (doc->editor->type == EM_TYPE_MULTIDOC)
	{
		UIMenu *submenu = ui_MenuCreate("");
		int i;

		ui_MenuAppendItem(em_data.title.tabs_menu, ui_MenuItemCreate("", UIMenuSeparator, NULL, NULL, NULL));
		for (i = 0; i < eaSize(&em_data.workspaces); i++)
			if (em_data.workspaces[i] != em_data.active_workspace)
				ui_MenuAppendItem(submenu, ui_MenuItemCreate(em_data.workspaces[i]->name, UIMenuCallback, emDocTabRightClickMoveWorkspace, doc, em_data.workspaces[i]));
		ui_MenuAppendItem(em_data.title.tabs_menu, ui_MenuItemCreate("Move to...", UIMenuSubmenu, NULL, NULL, submenu));
	}

	if (eaSize(&em_data.title.tabs_menu->items))
		ui_MenuPopupAtCursor(em_data.title.tabs_menu);
}

static void emModeSelected(UIComboBox *mode, UserData unused)
{
	EMWorkspace *workspace = ui_ComboBoxGetSelectedObject(mode);

	if (workspace)
	{
		emQueueFunctionCall2(emWorkspaceSetActive, workspace, NULL);
		emQueueFunctionCall2(emSetActiveEditorDoc, eaSize(&workspace->open_docs) > 0 ? workspace->open_docs[0] : NULL, NULL);
	}
}

static void emDocPrimaryWinFocus(UIWindow *window, EMEditorDoc *doc)
{
	emQueueFunctionCall2(emSetActiveEditorDoc, doc, NULL);
}

static bool emDocPrimaryWinClose(UIWindow *window, EMEditorDoc *doc)
{
	emQueueFunctionCall(emCloseDoc, doc);
	return false;
}

static void emPrevButtonClicked(UIWidget *widget, void *unused)
{
	emHistorySelectPreviousItem();
}

static void emNextButtonClicked(UIWidget *widget, void *unused)
{
	emHistorySelectNextItem();
}

static void emCloseButtonClicked(UIWidget *widget, void *unused)
{
	EMEditorDoc *doc = emGetActiveEditorDoc();
	if (!doc)
		return;
	emQueueFunctionCall(emCloseDoc, doc);
}

static int emModeSelectorCompare(const EMWorkspace **ws1, const EMWorkspace **ws2)
{
	return strcmpi((*ws1)->name, (*ws2)->name);
}

/******
* This refreshes the mode selector combo box to show all possible modes, including any
* custom doc groups.
******/
static void emModeSelectorRefresh(void)
{
	eaQSort(em_data.workspaces, emModeSelectorCompare);
}

/******
* This function creates a custom tab group, ready to be swapped in with the active tab group for display.
* RETURNS:
*   UITabGroup for multi-doc editor documents
******/
UITabGroup *emDocTabGroupCreate(void)
{
	UITabGroup *group = ui_TabGroupCreate(0, 5, 1, DOC_TAB_HEIGHT);
	group->widget.leftPad = elUINextX(em_data.title.mode) + 5;
	group->widget.rightPad = 5*4 + DOC_TAB_HEIGHT*3;
	group->widget.widthUnit = UIUnitPercentage;
	group->cbChanged = emDocTabClicked;
	group->bFitToSize = true;
	ui_WidgetSetFamily(UI_WIDGET(group), UI_FAMILY_EDITOR);
	return group;
}

/******
* This function creates the tab for a document.
* PARAMS:
*   doc - EMEditorDoc whose tab is to be created
******/
void emDocTabCreate(EMEditorDoc *doc)
{
	EMWorkspace *workspace;
	char buffer[128];

	// document's tab is already created
	if (!doc || doc->ui_tab)
		return;

	workspace = emWorkspaceFindDoc(doc);
	sprintf(buffer, "%s%s", doc->doc_display_name, !emGetDocSaved(doc) ? "*" : "");
	doc->ui_tab = ui_TabCreate(buffer);
	doc->ui_tab->cbContext = emDocTabRightClick;
	doc->ui_tab->pContextData = doc;

	assert(workspace);
	ui_TabGroupAddTab(workspace->tab_group, doc->ui_tab);
}

/******
* This function is used to focus the UI on a particular document.  If parameters are passed as NULL, then
* this function will assume focus is being set to the current custom asset tab group with no editor focused.
* PARAMS:
*   doc - EMEditorDoc to focus
*   editor - EMEditor in which doc is opened; can be NULL if doc is not NULL
******/
static void emEditorDocFocus(EMEditorDoc *doc, EMEditor *editor)
{
	EMWorkspace *workspace = emWorkspaceFindDoc(doc);

	assert(!doc || !editor || doc->editor == editor);
	if (doc && !editor)
		editor = doc->editor;

	if (workspace)
		emWorkspaceSetActive(workspace, doc);

	// focus primary window
	if (editor && editor->type == EM_TYPE_MULTIDOC && doc)
	{
		// momentarily disable focus function to prevent infinite loop
		UIActivationFunc temp_func = doc->primary_ui_window->widget.onFocusF;
		UserData temp_data = doc->primary_ui_window->widget.onFocusData;
		ui_WidgetSetFocusCallback(UI_WIDGET(doc->primary_ui_window), NULL, NULL);
		ui_SetFocus(doc->primary_ui_window);
		ui_WidgetSetFocusCallback(UI_WIDGET(doc->primary_ui_window), temp_func, temp_data);
	}
}

void emDocApplyWindowScale(EMEditorDoc *doc, F32 scale)
{
	int i;
	doc->editor->scale = scale;

	if (doc->primary_ui_window)
	{
		doc->primary_ui_window->widget.scale = doc->editor->scale / g_ui_State.scale;
	}

	FORALL_DOCWINDOWS(doc, i)
	{
		UIWindow *win = DOCWINDOW(doc, i);
		win->widget.scale = doc->editor->scale / g_ui_State.scale;
	}

	for(i = eaSize(&doc->editor->shared_windows)-1; i >=0; i--)
	{
		UIWindow *win = doc->editor->shared_windows[i];
		win->widget.scale = doc->editor->scale / g_ui_State.scale;
	}
}

/******
* This function applies window position and show preferences to a document when it is opened.
* PARAMS:
*   doc - EMEditorDoc to apply window prefs to
******/
void emDocApplyWindowPrefs(EMEditorDoc *doc, bool force_position)
{
	int i;

	if (!doc)
		return;

	// Apply window position preferences if not multi-doc or if this is the first
	// document.  Leave window positions alone when switching between multi-docs
	if (force_position || (doc->editor->type != EM_TYPE_MULTIDOC))
	{
		// Apply primary window
		if (doc->primary_ui_window)
		{
			EditorPrefGetWindowPosition(doc->editor->editor_name, "Window Position", "Primary Window", doc->primary_ui_window);
			doc->primary_ui_window->show = EditorPrefGetInt(doc->editor->editor_name, "Window Show", "Primary Window", doc->primary_ui_window->show);
		}

		// Apply main and private windows
		FORALL_DOCWINDOWS(doc, i)
		{
			UIWindow *win = DOCWINDOW(doc, i);
			if (win != doc->primary_ui_window)
			{
				const char* widgetText = ui_WidgetGetText(UI_WIDGET(win));
				EditorPrefGetWindowPosition(doc->editor->editor_name, "Window Position", widgetText, win);
				win->show = EditorPrefGetInt(doc->editor->editor_name, "Window Show", widgetText, win->show);
			}
		}
	}
	
	// Apply shared windows prefs if this is the first one of this type opened
	// otherwise, leave them alone
	if (eaSize(&doc->editor->open_docs) <= 1)
	{
		for(i = eaSize(&doc->editor->shared_windows)-1; i >=0; i--)
		{
			UIWindow *win = doc->editor->shared_windows[i];
			const char* widgetText = ui_WidgetGetText(UI_WIDGET(win));
			EditorPrefGetWindowPosition(doc->editor->editor_name, "Window Position", widgetText, win);
			win->show = EditorPrefGetInt(doc->editor->editor_name, "Window Show", widgetText, win->show);
		}
	}

	emDocApplyWindowScale(doc, EditorPrefGetFloat(doc->editor->editor_name, "Option", "Scale", 
		EditorPrefGetFloat("Editor Manager", "Option", "Scale", 1.0f)));
}

/******
* This function saves window position and show preferences to a document.
* PARAMS:
*   doc - EMEditorDoc to save window prefs for
******/
void emDocSaveWindowPrefs(EMEditorDoc *doc)
{
	int i;

	if (!doc)
		return;

	// Save primary window
	if (doc->primary_ui_window)
	{
		EditorPrefStoreWindowPosition(doc->editor->editor_name, "Window Position", "Primary Window", doc->primary_ui_window);
		EditorPrefStoreInt(doc->editor->editor_name, "Window Show", "Primary Window", doc->primary_ui_window->show);
	}

	// Save main and private windows
	FORALL_DOCWINDOWS(doc, i)
	{
		UIWindow *win = DOCWINDOW(doc, i);
		if (win != doc->primary_ui_window)
		{
			const char* widgetText = ui_WidgetGetText(UI_WIDGET(win));
			EditorPrefStoreWindowPosition(doc->editor->editor_name, "Window Position", widgetText, win);
			EditorPrefStoreInt(doc->editor->editor_name, "Window Show", widgetText, win->show);
		}
	}
	
	// Save shared windows prefs
	for(i = eaSize(&doc->editor->shared_windows)-1; i >=0; i--)
	{
		UIWindow *win = doc->editor->shared_windows[i];
		const char* widgetText = ui_WidgetGetText(UI_WIDGET(win));
		EditorPrefStoreWindowPosition(doc->editor->editor_name, "Window Position", widgetText, win);
		EditorPrefStoreInt(doc->editor->editor_name, "Window Show", widgetText, win->show);
	}
}

static int emSortDocs(const EMEditor **left, const EMEditor **right) 
{ 
	return ((*left)->sort_order - (*right)->sort_order); 
}

/******
* This function hides/shows all of a doc's windows and expanders; this should not be called once
* per frame - only when the UI for a document changes, or if the document focus changes.
* PARAMS:
*   doc - EMEditorDoc whose UI is to be shown; if NULL, editor must be specified
*   editor - EMEditor to which the document belongs
*   show - bool indicating whether to hide or show the document's non-tab UI
******/
void emShowDocumentUI(EMEditorDoc *doc, EMEditor *editor, int show, int give_focus)
{
	int i, j;

	assert(!doc || !editor || doc->editor == editor);
	if (doc && !editor)
		editor = doc->editor;

	// disable close button on primary editor
	ui_SetActive(UI_WIDGET(em_data.title.close_button), show && !(doc && doc->editor->primary_editor)
		&& !(doc && doc->editor->always_open && eaSize(&doc->editor->open_docs) <= 1));

	ui_SetActive(UI_WIDGET(em_data.title.prev_button), em_data.history_idx < eaSize(&em_data.history)-1);
	ui_SetActive(UI_WIDGET(em_data.title.next_button), em_data.history_idx > 0);

	if (doc)
	{
		// create a tab for the document if not created yet
		if (show)
			emDocTabCreate(doc);

		// document windows
		FORALL_DOCWINDOWS(doc, i)
		{
			UIWindow *window = DOCWINDOW(doc, i);
			if (show)
			{
				// add minimize buttons; TODO: fix minimization and uncomment this
//				if (eaSize(&window->buttons) == 1)
//					ui_WindowAddTitleButton(window, ui_WindowTitleButtonCreate("eui_arrow_large_down", emWinMinimizeClicked, window));

				if (window == doc->primary_ui_window)
					ui_WindowSetCloseCallback(window, emDocPrimaryWinClose, doc);
				else if (!DOCWINDOW_IS_PRIVATE(doc, i))
					ui_WindowSetCloseCallback(window, emWinClose, doc);
				if( window->show ) {
					ui_WindowShow(window);
				}
				ui_WidgetSetFamily(UI_WIDGET(window), UI_FAMILY_EDITOR);
			}
			else if (!em_data.current_doc || em_data.current_doc->editor != doc->editor || doc->editor->type != EM_TYPE_MULTIDOC || window != doc->primary_ui_window)
				ui_WindowHide(window);
		}
	}

	if (editor)
	{
		// multi-doc editors have to show other windows aside from the ones on the doc
		if (editor->type == EM_TYPE_MULTIDOC || editor->allow_external_docs)
		{
			EMEditor **sorted_doc = NULL;
			eaCopy(&sorted_doc, &em_data.editors);
			eaQSort(sorted_doc, emSortDocs);
			// primary UI window on other open docs for multi-doc editors
			for (i = 0; i < eaSize(&sorted_doc); i++)
			{
				EMEditor *curr_editor = sorted_doc[i];
				if (curr_editor->type == EM_TYPE_MULTIDOC || curr_editor->show_without_focus)
				{
					for (j = 0; j < eaSize(&curr_editor->open_docs); j++)
					{
						UIWindow *window = curr_editor->open_docs[j]->primary_ui_window;
						if (!window)
							continue;
						if (show && emWorkspaceFindDoc(curr_editor->open_docs[j]) == emWorkspaceFindDoc(doc))
						{
							ui_WindowShow(window);
							ui_WindowSetRaisedCallback(window, emDocPrimaryWinFocus, curr_editor->open_docs[j]);
							ui_WidgetSetFocusCallback(UI_WIDGET(window), emDocPrimaryWinFocus, curr_editor->open_docs[j]);
							ui_WidgetSetFamily(UI_WIDGET(window), UI_FAMILY_EDITOR);
						}
						else
							ui_WindowHide(window);
					}
				}
			}
			eaDestroy(&sorted_doc);
		}
		// single-doc editors can show shared windows
		if (editor->type == EM_TYPE_SINGLEDOC)
		{
			// shared editor windows
			for (i = 0; i < eaSize(&editor->shared_windows); i++)
			{
				UIWindow *window = editor->shared_windows[i];
				ui_WindowSetCloseCallback(window, emWinClose, doc);
				if (show)
				{
					ui_WindowShow(window);
					ui_WidgetSetFamily(UI_WIDGET(window), UI_FAMILY_EDITOR);
				}
				else
					ui_WindowHide(window);
			}
		}
	}

	if (show && give_focus)
		emEditorDocFocus(doc, editor);

	if (show)
		emMenusShow(doc, editor);
	else
		emMenusShow(NULL, NULL);

	// show editor's toolbars
	if (show)
		emEditorToolbarDisplay(editor);

	if (show)
		emPanelsShow(doc, editor);
}

/******
* This function clears out any persistent UI used for a document.  This should generally only be called
* when the document is closed.
******/
void emRemoveDocumentUI(EMEditorDoc *doc)
{
	if (doc && doc->ui_tab)
	{
		EMWorkspace *workspace = emWorkspaceFindDoc(doc);
		
		assert(workspace);

		// set last_doc to closest document to the one being removed
		if (workspace->open_docs[0] == doc)
		{
			UITabGroup *tabs = workspace->tab_group;
			int i;
			int src_idx;
			
			for (i = 0; i < eaSize(&tabs->eaTabs); i++)
			{
				if (tabs->eaTabs[i]->pContextData == doc)
				{
					if (i < eaSize(&tabs->eaTabs) - 1)
					{
						src_idx = eaFind(&workspace->open_docs, tabs->eaTabs[i + 1]->pContextData);
						assert(src_idx > -1);
						eaMove(&workspace->open_docs, 0, src_idx);
					}
					else if (i > 0)
					{
						src_idx = eaFind(&workspace->open_docs, tabs->eaTabs[i - 1]->pContextData);
						assert(src_idx > -1);
						eaMove(&workspace->open_docs, 0, src_idx);
					}
				}
			}
		}
		ui_TabGroupRemoveTab(workspace->tab_group, doc->ui_tab);
		eaFindAndRemove(&workspace->open_docs, doc);

		ui_TabFree(doc->ui_tab);
		doc->ui_tab = NULL;
	}
}

/******
* This function ensures that the all of the current document's UI elements are redrawn.
******/
void emRefreshDocumentUI(void)
{
	em_data.last_crc = 0;
}


/********************
* WORKSPACES
********************/
/******
* This function returns the workspace corresponding to the specified name.  If no matching 
* workspace is found, one is created with the specified name, if desired.
* PARAMS:
*   name - string name of the workspace to find/create
*   create - bool indicating whether to create a workspace if a matching one is not found
* RETURNS:
*   EMWorkspace with specified name
******/
EMWorkspace *emWorkspaceGet(const char *name, bool create)
{
	EMWorkspace *workspace;
	int i;

	// search existing workspaces
	for (i = 0; i < eaSize(&em_data.workspaces); i++)
	{
		if (strcmpi(em_data.workspaces[i]->name, name) == 0)
			return em_data.workspaces[i];
	}

	// if no workspaces are found, create a new one if necessary
	if (create)
	{
		workspace = calloc(1, sizeof(EMWorkspace));
		strcpy(workspace->name, name);
		workspace->min_pane = emMinPaneCreate();
		workspace->tab_group = emDocTabGroupCreate();
		eaPush(&em_data.workspaces, workspace);
		return workspace;
	}
	else
		return NULL;
}

/******
* This function returns the workspace that contains the specified doc.
* PARAMS:
*   doc - EMEditorDoc to search for
* RETURNS:
*   EMWorkspace containing the doc
******/
EMWorkspace *emWorkspaceFindDoc(EMEditorDoc *doc)
{
	int i;

	// search existing workspaces
	for (i = 0; i < eaSize(&em_data.workspaces); i++)
	{
		if (eaFind(&em_data.workspaces[i]->open_docs, doc) >= 0)
			return em_data.workspaces[i];
	}

	return NULL;
}

/******
* This function sets a workspace to be active, displaying its minimized window pane and
* tab groups.
* PARAMS:
*   workspace - EMWorkspace to activate
*   doc - EMEditorDoc within workspace to set active
******/
void emWorkspaceSetActive(EMWorkspace *workspace, EMEditorDoc *doc)
{
	if (!workspace)
		return;

	if (!doc)
		doc = eaSize(&workspace->open_docs) > 0 ? workspace->open_docs[0] : NULL;
	else
		assert(eaFind(&workspace->open_docs, doc) >= 0);

	// hide the current tab group and minimized window pane
	if (em_data.active_workspace != workspace)
	{
		if (em_data.active_workspace)
		{
			ui_WidgetRemoveFromGroup(UI_WIDGET(em_data.active_workspace->tab_group));
			ui_WidgetRemoveFromGroup(UI_WIDGET(em_data.active_workspace->min_pane));
		}	

		// display the workspace
		ui_ComboBoxSetSelectedObject(em_data.title.mode, workspace);
		ui_WidgetAddToDevice(UI_WIDGET(workspace->tab_group), NULL);
		em_data.active_workspace = workspace;
		em_data.minimized_windows.active_min_pane = workspace->min_pane;

		// Set sidebar open/shut and its width to workspace's preference
		emSidebarShow(EditorPrefGetInt("Editor Manager", em_data.active_workspace ? em_data.active_workspace->name : "No Workspace", "Sidebar Open", em_data.sidebar.show));
		em_data.sidebar.pane->widget.width = EditorPrefGetInt("Editor Manager", em_data.active_workspace ? em_data.active_workspace->name : "No Workspace", "Sidebar Width", em_data.sidebar.pane->widget.width);
		em_data.sidebar.pane->widget.width = MIN(MAX_SIDEBAR_WIDTH, em_data.sidebar.pane->widget.width);

		// Store new active workspace as a preference
		EditorPrefStoreString("Editor Manager", "Option", "Active Workspace", em_data.active_workspace->name);
	}
	if (doc && ui_TabGroupGetActive(workspace->tab_group) != doc->ui_tab)
	{
		// set active tab without calling focus function to prevent looping
		UIActivationFunc temp_func = workspace->tab_group->cbChanged;
		workspace->tab_group->cbChanged = NULL;
		ui_TabGroupSetActive(workspace->tab_group, doc->ui_tab);
		workspace->tab_group->cbChanged = temp_func;
	}
	ui_ColorWindowOrphanAll();
}


/*******************
* KEYBINDS
*******************/
static void emKeybindsFolderCacheCallback(const char *relpath, int when)
{
	char *c;
	c = strrchr(relpath, '/');
	if (c)
	{
		EMEditor *editor;
		char *editor_name;

		c++;
		strdup_alloca(editor_name, c);
		c = strrchr(editor_name, '.');
		if (c)
			*c = '\0';
		if (strcmpi(editor_name, "Standard Camera") == 0)
		{
			eaDestroyStruct(&em_data.standardcam_profile->eaBinds, parse_KeyBind);
			keybind_LoadProfile(em_data.standardcam_profile, relpath);
		}
		else if (strcmpi(editor_name, "Free Camera") == 0)
		{
			eaDestroyStruct(&em_data.freecam_profile->eaBinds, parse_KeyBind);
			keybind_LoadProfile(em_data.freecam_profile, relpath);
		}
		else
		{
			editor = emGetEditorByName(editor_name);
			if (editor && editor->inited)
			{
				eaDestroyStruct(&editor->keybinds->eaBinds, parse_KeyBind);
				keybind_LoadProfile(editor->keybinds, relpath);
				emMenuItemsRefreshBinds(editor);
			}
		}
	}
}


/*******************
* MAIN
*******************/
/******
* This function initializes all of the editor manager UI.
******/
void emUIInit(void)
{
	StashTableIterator iter;
	StashElement el;
	int i;

	// set up camera and keybinds
	emCameraInit(&em_data.worldcam);
	em_data.freecam_profile = StructCreate(parse_KeyBindProfile);
	em_data.freecam_profile->pchName = StructAllocString("FreecamKeybinds");
	keybind_CopyBindsFromName("FreeCamera", em_data.freecam_profile);

	// do not use free camera's spawn player bind
	for (i = 0; i < eaSize(&em_data.freecam_profile->eaBinds); i++)
	{
		KeyBind *bind = em_data.freecam_profile->eaBinds[i];
		if (strcmpi(bind->pchCommand, "FreeCamera.spawn_player") == 0)
		{
			StructDestroy(parse_KeyBind, bind);
			eaRemove(&em_data.freecam_profile->eaBinds, i);
			break;
		}
	}

	em_data.standardcam_profile = StructCreate(parse_KeyBindProfile);
	em_data.standardcam_profile->pchName = StructAllocString("StandardcamKeybinds");
	keybind_CopyBindsFromName("EditorCamera", em_data.standardcam_profile);
	emSetFreecam(EditorPrefGetInt("Editor Manager", "Camera", "Freecam", 0));

	// load custom user camera keybinds
	if (em_data.freecam_profile)
	{
		char kb_path[MAX_PATH];
		sprintf(kb_path, "%s/editor/Free Camera.userbinds", fileLocalDataDir());
		if (fileExists(kb_path) && EditorPrefGetInt("Editor Manager", "Freecam", "Version", 0) >= em_data.freecam_keybind_version)
		{
			eaDestroyStruct(&em_data.freecam_profile->eaBinds, parse_KeyBind);
			keybind_LoadProfile(em_data.freecam_profile, kb_path);
		}
		else
		{
			keybind_SaveProfile(em_data.freecam_profile, kb_path);
			EditorPrefStoreInt("Editor Manager", "Freecam", "Version", em_data.freecam_keybind_version);
		}
	}
	if (em_data.standardcam_profile)
	{
		char kb_path[MAX_PATH];
		sprintf(kb_path, "%s/editor/Standard Camera.userbinds", fileLocalDataDir());
		if (fileExists(kb_path) && EditorPrefGetInt("Editor Manager", "Standardcam", "Version", 0) >= em_data.standardcam_keybind_version)
		{
			eaDestroyStruct(&em_data.standardcam_profile->eaBinds, parse_KeyBind);
			keybind_LoadProfile(em_data.standardcam_profile, kb_path);
		}
		else
		{
			keybind_SaveProfile(em_data.standardcam_profile, kb_path);
			EditorPrefStoreInt("Editor Manager", "Standardcam", "Version", em_data.standardcam_keybind_version);
		}
	}

	// initialize skins
	if (!em_data.sidebar.expander_group_skin)
	{
		em_data.sidebar.expander_group_skin = ui_SkinCreate(NULL);
		SET_HANDLE_FROM_STRING(g_ui_FontDict, "EditorManager_PanelName", em_data.sidebar.expander_group_skin->hNormal);
	}

	// build menus, toolbar pane, and document management UI
	if (!em_data.title.pane)
	{
		em_data.title.pane = ui_PaneCreate(0, 0, 1.f, 0, UIUnitPercentage, UIUnitFixed, UI_PANE_VP_BOTTOM);
		ui_WidgetSetFamily(UI_WIDGET(em_data.title.pane), UI_FAMILY_EDITOR);
		ui_PaneSetStyle(em_data.title.pane, "AssetManager_TitlePane", true, true);

		em_data.title.menu_bar = ui_MenuBarCreate(NULL);
		ui_WidgetSetWidthEx(UI_WIDGET(em_data.title.menu_bar), 1.0f, UIUnitPercentage);
		em_data.title.menu_bar->widget.offsetFrom = UITopLeft;
		ui_WidgetAddChild(UI_WIDGET(em_data.title.pane), UI_WIDGET(em_data.title.menu_bar));

		em_data.title.toolbar = emToolbarPaneCreate();
		ui_WidgetAddChild(UI_WIDGET(em_data.title.pane), UI_WIDGET(em_data.title.toolbar));

		em_data.title.mode = ui_ComboBoxCreate(5, 5, 200, parse_EMWorkspace, &em_data.workspaces, "name");
		em_data.title.mode->widget.height = DOC_TAB_HEIGHT;

		ui_ComboBoxSetSelectedCallback(em_data.title.mode, emModeSelected, NULL);
		ui_WidgetSetFamily(UI_WIDGET(em_data.title.mode), UI_FAMILY_EDITOR);
		ui_WidgetAddToDevice(UI_WIDGET(em_data.title.mode), NULL);

		em_data.title.global_toolbar = emToolbarCreateFileToolbar(EM_FILE_TOOLBAR_NEW | EM_FILE_TOOLBAR_OPEN);
		em_data.title.camera_toolbar = emToolbarCreateCameraToolbar();
		em_data.title.window_toolbar = emToolbarCreateWindowToolbar();

		em_data.title.prev_button = ui_ButtonCreateImageOnly("eui_arrow_large_left", 2*(5+DOC_TAB_HEIGHT)+5, 5, emPrevButtonClicked, NULL);
		em_data.title.prev_button->widget.offsetFrom = UITopRight;
		ui_WidgetSetDimensions(UI_WIDGET(em_data.title.prev_button), DOC_TAB_HEIGHT, DOC_TAB_HEIGHT);
		ui_ButtonSetImageStretch(em_data.title.prev_button, true);
		ui_WidgetSetFamily(UI_WIDGET(em_data.title.prev_button), UI_FAMILY_EDITOR);
		ui_WidgetAddToDevice(UI_WIDGET(em_data.title.prev_button), NULL);
		ui_SetActive(UI_WIDGET(em_data.title.prev_button), false);

		em_data.title.next_button = ui_ButtonCreateImageOnly("eui_arrow_large_right", 5+DOC_TAB_HEIGHT+5, 5, emNextButtonClicked, NULL);
		em_data.title.next_button->widget.offsetFrom = UITopRight;
		ui_WidgetSetDimensions(UI_WIDGET(em_data.title.next_button), DOC_TAB_HEIGHT, DOC_TAB_HEIGHT);
		ui_ButtonSetImageStretch(em_data.title.next_button, true);
		ui_WidgetSetFamily(UI_WIDGET(em_data.title.next_button), UI_FAMILY_EDITOR);
		ui_WidgetAddToDevice(UI_WIDGET(em_data.title.next_button), NULL);
		ui_SetActive(UI_WIDGET(em_data.title.next_button), false);

		em_data.title.close_button = ui_ButtonCreateImageOnly("eui_button_close", 5, 5, emCloseButtonClicked, NULL);
		em_data.title.close_button->widget.offsetFrom = UITopRight;
		ui_WidgetSetDimensions(UI_WIDGET(em_data.title.close_button), DOC_TAB_HEIGHT, DOC_TAB_HEIGHT);
		ui_ButtonSetImageStretch(em_data.title.close_button, true);
		ui_WidgetSetFamily(UI_WIDGET(em_data.title.close_button), UI_FAMILY_EDITOR);
		ui_WidgetAddToDevice(UI_WIDGET(em_data.title.close_button), NULL);
		ui_SetActive(UI_WIDGET(em_data.title.close_button), false);

		em_data.title.pane->widget.height = elUINextY(em_data.title.toolbar) + 5;
	}

	// build panel sidebar pane
	if (!em_data.sidebar.pane)
	{
		em_data.sidebar.show = true;
		em_data.sidebar.pane = ui_PaneCreate(0, 0, MIN_SIDEBAR_WIDTH, 1, UIUnitFixed, UIUnitPercentage, UI_PANE_VP_LEFT);
		em_data.sidebar.pane->widget.offsetFrom = UIRight;
		em_data.sidebar.pane->widget.scale = emGetSidebarScale();
		ui_WidgetSetFamily(UI_WIDGET(em_data.sidebar.pane), UI_FAMILY_EDITOR);
		ui_PaneSetStyle(em_data.sidebar.pane, "AssetManager_SidebarPane", true, true);

		em_data.sidebar.tab_group = ui_TabGroupCreate(0, 0, 1, 1);
		ui_WidgetSetDimensionsEx(UI_WIDGET(em_data.sidebar.tab_group), 1, 1, UIUnitPercentage, UIUnitPercentage);
		em_data.sidebar.tab_group->widget.bottomPad = 5;
		ui_TabGroupSetChangedCallback(em_data.sidebar.tab_group, emTabChanged, NULL);
		ui_WidgetAddChild(UI_WIDGET(em_data.sidebar.pane), UI_WIDGET(em_data.sidebar.tab_group));

		// hide/show controls
		em_data.sidebar.hide_button = ui_ButtonCreateImageOnly("eui_arrow_large_right", 0, -12, emSidebarHideShowClicked, NULL);
		em_data.sidebar.hide_button->widget.yPOffset = 0.5;
		em_data.sidebar.hide_button->widget.offsetFrom = UITopLeft;
		ui_WidgetSetDragCallback(UI_WIDGET(em_data.sidebar.hide_button), emSidebarExpandDrag, NULL);
		ui_WidgetAddChild(UI_WIDGET(em_data.sidebar.pane), UI_WIDGET(em_data.sidebar.hide_button));

		em_data.sidebar.hidden_pane = ui_PaneCreate(0, 0, 0, 1, UIUnitFixed, UIUnitPercentage, UI_PANE_VP_LEFT);
		em_data.sidebar.hidden_pane->widget.offsetFrom = UIRight;
		ui_WidgetSetFamily(UI_WIDGET(em_data.sidebar.hidden_pane), UI_FAMILY_EDITOR);
		ui_PaneSetStyle(em_data.sidebar.hidden_pane, "AssetManager_SidebarPane", true, true);

		em_data.sidebar.show_button = ui_ButtonCreateImageOnly("eui_arrow_large_left", 0, -12, emSidebarHideShowClicked, NULL);
		em_data.sidebar.show_button->clickedData = em_data.sidebar.show_button;
		em_data.sidebar.show_button->widget.yPOffset = 0.5;
		ui_WidgetAddChild(UI_WIDGET(em_data.sidebar.hidden_pane), UI_WIDGET(em_data.sidebar.show_button));

		emSidebarApplyCurrentScale();
		emSidebarShow(true);
	}

	emPanelsInit();

	// build progress UI
	if (!em_data.progress.window)
	{
		em_data.progress.window = ui_WindowCreate("Progress", 50, 120, 220, 65);
		ui_WidgetSetFamily(UI_WIDGET(em_data.progress.window), UI_FAMILY_EDITOR);

		em_data.progress.label = ui_LabelCreate("Please wait", 10, 5);
		ui_WindowAddChild(em_data.progress.window, em_data.progress.label);

		em_data.progress.progress = ui_ProgressBarCreate(10, 30, 104);
		ui_WidgetSetDimensions(&em_data.progress.progress->widget, 204, 25);
		ui_WindowAddChild(em_data.progress.window, em_data.progress.progress);
	}

	// build window management UI
	// create the transparent skin for window minimization
	em_data.transparent_skin = ui_SkinCreate(NULL);
	i = RGBAFromColor(ui_GetActiveSkin()->background[0]);
	i &= 0xFFFFFF00;
	i |= EM_TRANS_SKIN_ALPHA;
	ui_SkinSetBackground(em_data.transparent_skin, colorFromRGBA(i));
	i = RGBAFromColor(ui_GetActiveSkin()->titlebar[0]);
	i &= 0xFFFFFF00;
	i |= EM_TRANS_SKIN_ALPHA;
	ui_SkinSetTitleBar(em_data.transparent_skin, colorFromRGBA(i));
	em_data.minimized_windows.visible = false;

	// build menus
	emMenuInit();

	// initialize workspaces
	stashGetIterator(em_data.registered_editors, &iter);
	while (stashGetNextElement(&iter, &el))
	{
		EMWorkspace *workspace;
		EMEditor *editor = stashElementGetPointer(el);

		// do not process invalid editors
		if (!emIsEditorOk(editor))
			continue;

		if (!editor->default_workspace[0])
			strcpy(editor->default_workspace, editor->editor_name);
		workspace = emWorkspaceGet(editor->default_workspace, true);
		assert(workspace);
		eaPushUnique(&workspace->editors, editor);
	}
	emModeSelectorRefresh();

	// create folder cache callback for keybinds
	if (!em_data.local_settings_cache)
	{
		char file_spec[MAX_PATH];
		em_data.local_settings_cache = FolderCacheCreate();
		sprintf(file_spec, "%s/editor/", fileLocalDataDir());
		FolderCacheAddFolder(em_data.local_settings_cache, file_spec, 0, NULL, false);
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "*.userbinds", emKeybindsFolderCacheCallback);
	}

	// Create the object picker
	strcpy(objectPicker.picker_name, "Object Picker");
	strcpy(objectPicker.default_type, OBJECT_LIBRARY_DICT);
	objectPicker.allow_outsource = true;
	emPickerManage(&objectPicker);
	objectPicker.init_func = objectPickerInit;
	
	emPickerRegister(&objectPicker);

	// Create the texture picker
	{
		EMPicker* texturePicker = emTexturePickerCreateForType( "wtex" );
		texturePicker->allow_outsource = true;
		emPickerRegister( texturePicker );
	}

	em_data.message_of_the_day.current_message = NULL;
	em_data.message_of_the_day.registered_messages_by_keyname = stashTableCreateWithStringKeys(32, StashCaseInsensitive);

	// Register some Messages about the Message of the Day system
	emRegisterMotD(					2012, 3,  15,	"RelevantMessages", 													"Messages such as this one will be displayed that are relevant to the Editor you have open and to the Group you are in.");
	emRegisterMotD(					2012, 3,  15,	"MessageLeftClick", 													"When a Message such as this one is displayed in the Editor, simply left-click on it to dismiss it.");
	emRegisterMotD(					2012, 3,  15,	"MessagesHideInViewport",												"You can prevent all Messages such as this one from displaying in the Editor by going to the Tools | Options menu, then select the Messages tab. Uncheck the \"Show in viewport\" checkbox.");
	emRegisterMotD(					2012, 3,  15,	"MessageHide",															"You can prevent an individual Message such as this one from displaying in the Editor again by going to the Tools | Options menu, then select the Messages tab. The current Message will be at the top and selected. Simply double-left-click to hide it permanently.");
	emRegisterMotDForGroup(			2012, 3,  16,	"SoftwareRegisterMessage",								"Software",		"If you want to your own Message to appear for other Editor users, please use one of the emRegisterMotD*** functions to do so.");
	emRegisterMotD(					2012, 3,  19,	"MessageRightClick",													"When a Message such as this one is displayed in the Editor, right-click on it to display Options for Messages.");
	emRegisterMotDForEditor(		2012, 3,  19,	"CutSceneTimelineFrameDragResize",	"Cut Scene Editor",					"You can resize any resizable keyframe in the timeline view by dragging its left or right edge.");
	emRegisterMotDForEditor(		2012, 8,  1,	"TimeToEditMultipleThings",			"World Editor",						"In the World Editor, you can edit fields for multiple items without taking up hours of your time due to a recent code fix.");
	emRegisterMotDForEditor(		2012, 8,  1,	"IgnoreFogClip",					"Sky Editor",						"You can now set a sky file to ignore it's own fog clip setting. Instead, it will use whichever sky file's setting it's being blended on top of.");
	emRegisterMotDForEditorAndGroup(2012, 8,  1,	"CarlosList00",						"World Editor",		"Environment",	"You can use the 'J' key to unhide all hidden things.");
	emRegisterMotDForEditorAndGroup(2012, 8,  1,	"CarlosList01",						"World Editor",		"Environment",	"You can use the 'H' key to hide your selection.");
	emRegisterMotDForEditorAndGroup(2012, 8,  1,	"CarlosList02",						"World Editor",		"Environment",	"In Tools -> Options -> Info Window you can create a custom window that shows information for whatever you mouse over.");
	emRegisterMotDForEditorAndGroup(2012, 8,  1,	"CarlosList03",						"World Editor",		"Environment",	"Did you know you can use Tools -> Graphics Options to view Static Collision Meshes?");
	emRegisterMotDForEditorAndGroup(2012, 8,  1,	"CarlosList04",						"World Editor",		"Environment",	"Remember, Editing the terrain will add ~100 objects to the draw list until it is re-binned. Your budgets will not be accurate until you initmap.");
	emRegisterMotDForEditor(		2012, 8,  1,	"CarlosList05",						"World Editor",						"Be sure to set your Region Type for all game maps! Is it Ground, Indoor, or something else?");
	emRegisterMotDForEditor(		2012, 8,  1,	"CarlosList06",						"World Editor",						"You can use Ctrl + D to duplicate your selection.");
	emRegisterMotDForEditor(		2012, 8,  1,	"CarlosList07",						"World Editor",						"You can use the Right-click -> Place menu to quickly place lights, fx nodes, volumes and more.");
	emRegisterMotDForEditor(		2012, 8,  1,	"CarlosList08",						"World Editor",						"In Tools -> Options -> Custom Menu you can customize your right click menu.");
	emRegisterMotDForEditor(		2012, 8,  1,	"CarlosList09",						"World Editor",						"Use Ctrl + Mouse Wheel to travel up and down the Selection Hierarchy in the Tracker Tree.");
	emRegisterMotDForEditor(		2012, 8,  1,	"CarlosList10",						"World Editor",						"When in Auto-Snap mode, use Shift + Left Click to quickly snap an object to a surface normal");
	emRegisterMotDForEditor(		2012, 8,  1,	"CarlosList11",						"World Editor",						"Be sure to go to View -> Toolbars and disable any toolbars you don't ordinarily need!");
	emRegisterMotDForEditor(		2012, 8,  1,	"CarlosList12",						"World Editor",						"Use the 'F' key to focus your camera on your selection.");
	emRegisterMotDForEditor(		2012, 8,  1,	"CarlosList13",						"World Editor",						"In the Tracker Tree, Black is a group or editable object.  Red is an uneditable Object Library piece.  Blue is an editable Object Library piece.  Be careful changing anything blue, as it will affect other instances of that object in the game.");
	emRegisterMotDForEditor(		2012, 8,  1,	"CarlosList14",						"World Editor",						"Be careful using vertex lights.  While they are technically cheaper than dynamic lights, they break instancing of objects, and can greatly impact the budgets and performance of a map.");
	emRegisterMotDForEditor(		2012, 8,  1,	"CarlosList15",						"World Editor",						"Use F4 to cycle through wireframes. This can come in handy when trying to see if your occlusion is working properly.");
	emRegisterMotDForEditor(		2012, 8,  1,	"CarlosList16",						"World Editor",						"Did you know you can use the Scratch Layer to do things like copy objects between maps, or keep a personal collection of objects you like to use?");
	emRegisterMotDForEditor(		2012, 8,  1,	"CarlosList17",						"World Editor",						"You can change a Groups pivot by switching Pivot mode from Object to Pivot");
	emRegisterMotDForEditor(		2012, 8,  1,	"CarlosList18",						"World Editor",						"Keep your layers organized!  Your work will be touched by others and organized work will help everyone be more efficient!");
	emRegisterMotDForEditor(		2012, 8,  1,	"CarlosList19",						"World Editor",						"Always use a Master Skyfile as your base skyfile on a region!");
	emRegisterMotDForEditor(		2012, 8,  1,	"CarlosList20",						"World Editor",						"Use Ctrl + W to reset an objects rotation");
	emRegisterMotDForEditor(		2012, 8,  1,	"CarlosList21",						"Terrain Editor",					"Though you can technically use more, try to limit yourself to four or less materials per terrain block. Your budgets will thank you.");
	emRegisterMotDForEditor(		2012, 8,  1,	"CarlosList22",						"Terrain Editor",					"Use the Tools -> Options -> Info Window to be able to mouse over your terrain and see what Terrain materials are being used per-block!");
	emRegisterMotDForEditor(		2012, 8,  1,	"CarlosList23",						"Terrain Editor",					"When editing heightmaps that have objects painted on them, ALWAYS HIDE TERRAIN OBJECTS until you are done! This speeds things up immensely!");
	emRegisterMotDForEditor(		2012, 8,  1,	"CarlosList24",						"Terrain Editor",					"Having performance trouble while editing terrain? Perhaps you should consider splitting your terrain up into several layers - this will take up less RAM while editing and allow you to work faster.");
	emRegisterMotDForEditor(		2012, 8,  1,	"CarlosList25",						"Terrain Editor",					"Did you know that shortcut keys for the terrain editor are identical to photoshop? Use the bracket keys to change brush size, shift+brackets to change brush hardness, and more!");
	emRegisterMotDForEditor(		2012, 8,  1,	"CarlosList26",						"Terrain Editor",					"Did you know that the Terrain editor has an underlying system for tracking Soil depth, and using erosion brushes actually simulates moving soil around from one place to another? Try using the soil brushes and soil visualization to effect how erosion brushes work!");
	emRegisterMotDForEditor(		2012, 8,  1,	"CarlosList27",						"Terrain Editor",					"Did you know that you can paint a selection, use the Selection Visualization to see it, and use the Selection Filter to limit your actions to the painted selection? Very useful!");
	emRegisterMotDForEditor(		2012, 8,  1,	"CarlosList28",						"Terrain Editor",					"When working with a large amount of Terrain, always start with a low resolution sampling of terrain and increase the resolution as you progress.  Just like Zbrush, this makes it easier to control broad and small features.");
	emRegisterMotDForEditor(		2012, 8,  1,	"CarlosList29",						"Terrain Editor",					"Did you know that you can use the Material Replace Brush to completely replace one material with another? This is useful when trying to reduce the amount of Terrain materials you are using!");
	emRegisterMotDForEditor(		2012, 8,  1,	"CarlosList30",						"Sky Editor",						"Sky Dome blocks don't transition or hide well - be careful when overlapping skyfiles with multiple sky domes.");
	emRegisterMotDForEditor(		2012, 8,  1,	"CarlosList31",						"Sky Editor",						"When creating an override skyfile, only use the blocks you absolutely need to!");
	emRegisterMotD(					2012, 8,  1,	"CarlosList32",															"In any editor simply press F1 to open the Confluence help-page for that editor!");
	emRegisterMotD(					2012, 8,  1,	"CarlosList33",															"Press F2 to cycle between 'Standard' and 'Freecam' camera modes.");
	emRegisterMotDForEditorAndGroup(2012, 8,  1,	"CarlosList34",						"World Editor",		"Environment",	"Having a slow time tinting objects or adjusting light colors? Hide terrain objects!");
	emRegisterMotDForEditor(		2012, 8,  2,	"SelectMulti",						"All",								"You can now select multiple rows in the table-based Game Design Editors (using shift or ctrl). Currently supported things to do with those rows: Everything except for Edit Row, Open file, Open Folder, Find Usage & List References.");
	emRegisterMotDForEditor(		2012, 8,  3,	"CarlosList35",						"World Editor",						"If you use the Standard Camera, 'Q' will toggle between move and rotate modes.");
	emRegisterMotDForEditor(		2012, 8,  3,	"CarlosList36",						"World Editor",						"You can select multiple Attributes Panels to display at once, by CTRL Clicking, or Shift Clicking the different attributes.");
	emRegisterMotDForEditor(		2012, 8,  3,	"CarlosList37",						"World Editor",						"The Grid is your friend.  Work on the grid for large object placement.  Details and props can be placed off grid.");
	emRegisterMotDForEditor(		2012, 8,  3,	"CarlosList38",						"World Editor",						"Do not check in map files while you are editing that map.  This can cause a space time vortex that confuses the editor.");
	emRegisterMotDForEditor(		2012, 8,  3,	"CarlosList39",						"World Editor",						"You can assign any Layer or Group to be the Default Parent, by right clicking and selecting 'Set Default Parent.'  The Default Parent is where new objects pulled from the Object Library, will be placed on creation.");
	emRegisterMotDForEditor(		2012, 8,  3,	"CarlosList40",						"World Editor",						"You can rename a group or object by double clicking it's name in the Tracker Tree, or below that in the Name Field.");
	emRegisterMotDForEditor(		2012, 8,  3,	"CarlosList41",						"World Editor",						"Organize your layers.  Grouping objects by area/type, and maintaining reasonable naming conventions within your layer will greatly aid others who have to deal with your work at a later date.");
	emRegisterMotDForEditor(		2012, 8,  3,	"CarlosList42",						"World Editor",						"Adding Simple Animations to your map can breathe life into a level.  However don't layer more than 3 animations on top of each other or your computer will be sad.");
	emRegisterMotDForEditor(		2012, 8,  3,	"CarlosList43",						"World Editor",						"Any maps in the TestMaps folder need to be made private.  In the 'Map Tab at the top right, put your login name (jdoe) in the 'Private To' field.");
	emRegisterMotDForEditor(		2012, 8,  3,	"CarlosList44",						"Material Editor",					"Don't forget to set your new material's Physical Property! (Properties>Material Properties)");
	emRegisterMotDForEditor(		2012, 8,  3,	"CarlosList45",						"Sky Editor",						"You can add new keyframes to any sky block by right clicking in the timeline at the time that you want your new keyframe.");
	emRegisterMotD(					2012, 8,  7,	"CarlosList46",															"SAVE OFTEN! Ctrl + S!");
	emRegisterMotDForEditor(		2012, 8,  7,	"CarlosList47",						"World Editor",						"With the color picker up, you can hold SHIFT to select a color from anywhere on screen.");
	emRegisterMotDForEditor(		2012, 8,  7,	"CarlosList48",						"World Editor",						"M and N will increase and decrease the rotational snap angles.  < and > will increase and decrease the movement grid spacing.");
	emRegisterMotDForEditor(		2012, 8,  7,	"CarlosList49",						"World Editor",						"Don't forget to test your stuff in medium and low end modes periodically.  (Hit ~, type \"Budgets\" and then select the buttons along the top)");
	emRegisterMotDForEditor(		2012, 8,  7,	"CarlosList50",						"World Editor",						"Use the console command 'editor_visscale' to set how far you editor draws the world.");
	emRegisterMotDForEditor(		2012, 8,  7,	"CarlosList51",						"World Editor",						"All layer files are automatically backed up every 10 minutes while they are being edited. Check your maps directory for autobackups - just in case you need them!");
	emRegisterMotDForEditor(		2012, 8,  7,	"CarlosList52",						"Sky Editor",						"Don't forget to set your \"Clip Fog\" and \"Clip Background\" colors to be similar to your fog color.  On low end machines these will be used automatically, and if they are drastically different it will look broken.");
	emRegisterMotDForEditor(		2012, 8,  7,	"CarlosList53",						"Sky Editor",						"With the color picker up, you can hold SHIFT to select a color from anywhere on screen.");
}

/******
* This function can be called on a doc once per frame to handle changes to particular attributes of
* the document.
* PARAMS:
*   doc - EMEditorDoc document UI to display
******/
void emDocUIOncePerFrame(EMEditorDoc *doc)
{
	bool active;
	int i;

	if (doc && !emGetDocSaved(doc))
	{
		// autosave if the interval has been reached
		if (doc->editor->autosave_interval && 
			em_data.timestamp > (__time32_t)(doc->last_autosave_time + doc->editor->autosave_interval * 60))
		{
			doc->editor->autosave_func(doc);
			doc->last_autosave_time = em_data.timestamp;
		}
	}

	// do not continue further if not in editor mode
	if (!em_data.editor_mode)
		return;

	// validate the doc exists
	if (!doc)
	{
		em_data.last_crc = 0;
		return;
	}

	// validate that the doc is open
	if (eaFind(&em_data.open_docs, doc) < 0)
		return;

	// render the current doc differently by showing its full UI
	active = (em_data.current_doc && doc == em_data.current_doc && (!em_data.current_doc->process || !em_data.current_doc->process->active));

	// handle changes to the doc name on the tab
	if (doc->ui_tab && (doc->name_changed || emGetDocSaved(doc) != doc->prev_saved))
	{
		char buffer[128];
		
		doc->prev_saved = emGetDocSaved(doc);
		sprintf(buffer, "%s%s", doc->doc_display_name, !doc->prev_saved ? "*" : "");
		ui_TabSetTitleString(doc->ui_tab, buffer);
		doc->name_changed = 0;
	}

	if (active)
	{
		U32 crc = 0;

		// crc the following to determine whether to recreate the document's UI (windows, tabs, panels, etc):
		// -every window's name (to recreate window menu)
		// -panel name and tab name
		// -toolbar pointers (TODO: add name to toolbar and use that)
		// -file timestamp
		cryptAdler32Init();
		cryptAdler32Update((U8 *)&doc, sizeof(doc));
		FORALL_DOCWINDOWS(doc, i)
		{
			UIWindow *window = DOCWINDOW(doc, i);
			const char* widgetText = ui_WidgetGetText(UI_WIDGET(window));
			if (widgetText)
				cryptAdler32Update(widgetText, (int)strlen(widgetText));
		}
		for (i = 0; i < eaSize(&doc->em_panels); i++)
		{
			const char* expanderText = ui_WidgetGetText( UI_WIDGET( doc->em_panels[i]->ui_expander ));
			if (doc->em_panels[i])
			{
				cryptAdler32Update(doc->em_panels[i]->tab_name, (int) strlen(doc->em_panels[i]->tab_name));
				cryptAdler32Update(expanderText, (int) strlen(expanderText));
			}
		}
		for (i = 0; i < eaSize(&doc->editor->toolbars); i++)
		{
			if (doc->editor->toolbars[i])
				cryptAdler32Update((U8*) &doc->editor->toolbars[i], (int) sizeof(EMToolbar *));
		}
		crc = cryptAdler32Final();

		// if the document has changed sufficiently (or if active document changed entirely), rebuild the UI
		if (crc != em_data.last_crc)
		{
			em_data.last_crc = crc;
			emShowDocumentUI(doc, NULL, 1, false);
		}
	}
}

/******
* This function is called once per frame and handles a variety of generic editor manager UI updating
* and maintenance.
******/
void emUIOncePerFrame(void)
{
	int i, x, y, w, h;
	EMEditorDoc* current_doc = em_data.current_doc;

	// deal with sidebar resizing
	if (em_data.sidebar.resize_mode)
	{
		if (mouseIsDown(MS_LEFT))
		{
			int mx, my;

			mousePos(&mx, &my);
			em_data.sidebar.pane->widget.width = MAX(MIN_SIDEBAR_WIDTH, em_data.sidebar.resize_start_width + ((em_data.sidebar.resize_start_x - mx) / em_data.sidebar.scale));
			em_data.sidebar.pane->widget.width = MIN(MAX_SIDEBAR_WIDTH, em_data.sidebar.pane->widget.width);
		}
		else
		{
			em_data.sidebar.resize_mode = 0;
			EditorPrefStoreInt("Editor Manager", em_data.active_workspace ? em_data.active_workspace->name : "No Workspace", "Sidebar Width", em_data.sidebar.pane->widget.width);
		}
	}

	// check toolbars for stacking
	if (current_doc)
		emEditorToolbarStack(current_doc->editor);
	
	// update flashed/highlighted UI elements
	emMsgLogRefresh();

	// update Message of the Day
	emMotDRefresh();

	// use check function to update all menu items in the editor and editor manager
	emMenuItemsRefresh(em_data.current_editor);

	// determine whether to show minimized window pane
	gfxGetActiveDeviceSize(&w, &h);
	mousePos(&x, &y);
	if (em_data.minimized_windows.active_min_pane)
	{
		ui_WidgetRemoveFromGroup(UI_WIDGET(em_data.minimized_windows.active_min_pane));
		if (!em_data.minimized_windows.visible && !mouseIsDown(MS_LEFT) && !mouseIsDown(MS_RIGHT) && !mouseIsDown(MS_MID)
			&& y > h - MIN_PANE_THRESH && x > 0 && x < w && eaSize(&em_data.minimized_windows.active_min_pane->widget.children) > 0)
			em_data.minimized_windows.visible = true;
		else if (em_data.minimized_windows.visible &&
			(y < h - em_data.minimized_windows.active_min_pane->widget.height || x < 0 || x > w))
		{
			em_data.minimized_windows.visible = false;
			for (i = 0; i < eaSize(&em_data.minimized_windows.active_min_pane->widget.children); i++)
			{
				UIWidget *min_win = em_data.minimized_windows.active_min_pane->widget.children[i];
				min_win->mouseLeaveF(min_win, min_win->mouseLeaveData);
			}
		}
		if (em_data.minimized_windows.visible)
		{
			ui_PaneWidgetAddToDevice(UI_WIDGET(em_data.minimized_windows.active_min_pane), NULL);
			ui_WidgetGroupSteal(ui_PaneWidgetGroupForDevice(NULL), UI_WIDGET(em_data.title.pane));
			ui_WidgetGroupSteal(ui_PaneWidgetGroupForDevice(NULL), UI_WIDGET(em_data.sidebar.pane));
		}
	}

	// render info window
	emInfoWinDraw();
}

AUTO_COMMAND ACMD_NAME("Editor.AdjustCamDistance");
void CmdEditorAdjustCamDistance(S32 iDelta)
{
	EMEditorDoc* emDoc = emGetActiveEditorDoc();
	EMEditor* editor = emDoc ? emDoc->editor : NULL;

	if (editor && (editor->hide_world || editor->force_editor_cam) && editor->camera)
	{
		GfxCameraController *pCamera = editor->camera;

		// Hardcode distance settings of 2 and 500 for now
		pCamera->camdist += iDelta;
		if (pCamera->camdist < 2.0) {
			pCamera->camdist = 2.0;
		} else if (pCamera->camdist > 500.0) {
			pCamera->camdist = 500.0;
		}
	}
}

// For compatibility with older keybinds
AUTO_COMMAND ACMD_NAME("CostumeEditor.AdjustCamDistance");
void CmdCEAdjustCamDistance(int keyDown) 
{
	CmdEditorAdjustCamDistance(keyDown);
}


/// Object picker

bool objectPickerNodeSelected(EMPicker *picker, EMPickerSelection *selected)
{
	sprintf(selected->doc_name, "%s,%s", ((ResourceInfo*)selected->data)->resourceName, ((ResourceInfo*)selected->data)->resourceLocation);
	strcpy(selected->doc_type, "model");
	return true;
}

// filters out private "_"-prefixed object library nodes
bool objectPickerFilterPublicCheck(void *node_data, ParseTable *parse_table)
{
	if (parse_table == parse_ResourceInfo)
	{
		ResourceInfo *entry = (ResourceInfo *) node_data;
		if (entry->resourceName[0] == '_')
			return false;
	}
	return true;
}

// filters out all private object library pieces
bool objectPickerFilterCheck(void *node_data, ParseTable *parse_table)
{
	if (parse_table == parse_ResourceInfo)
	{
		ResourceInfo *entry = (ResourceInfo *) node_data;
		GroupDef *def = objectLibraryGetGroupDefByName(entry->resourceName, false);
		if (groupIsPrivate(def))
			return false;
	}
	return true;
}

void emClearPreviews(void)
{
	FOR_EACH_IN_STASHTABLE( objectPickerPreviews, ResourcePreviewTexture, it ) {
		gfxHeadshotRelease( it->texture );
		free( it );
	} FOR_EACH_END;
	stashTableClear( objectPickerPreviews );

	FOR_EACH_IN_STASHTABLE( objectPickerLargePreviews, ResourcePreviewTexture, it ) {
		gfxHeadshotRelease( it->texture );
		free( it );
	} FOR_EACH_END;
	stashTableClear( objectPickerLargePreviews );

	FOR_EACH_IN_STASHTABLE( costumePickerPreviews, ResourcePreviewTexture, it ) {
		gfxHeadshotRelease( it->texture );
		free( it );
	} FOR_EACH_END;
	stashTableClear( costumePickerPreviews );
}

void emObjectPickerRefresh(void)
{
	ResourceGroup *root = objectLibraryGetRoot();

	if(objectPicker.tree_ui_control)
		objectPicker.tree_ui_control->root.contents = root;
	objectPicker.display_data_root = root;
	emPickerRefresh(&objectPicker);

	emClearPreviews();
}

void objectPickerInit(EMPicker *picker)
{
	EMPickerDisplayType *displayType;
	EMPickerFilter *filter;
	

	// ResourceGroup nodes
	displayType = calloc(1, sizeof(*displayType));
	displayType->parse_info = parse_ResourceGroup;
	displayType->display_name_parse_field = "name";
	displayType->is_leaf = false;
	eaPush(&objectPicker.display_types, displayType);

	// ResourceInfo nodes
	displayType = calloc(1, sizeof(*displayType));
	displayType->parse_info = parse_ResourceInfo;
	displayType->display_name_parse_field = "resourceNotes";
	displayType->is_leaf = true;
	displayType->tex_func = objectPickerPreview;
	displayType->selected_func = objectPickerNodeSelected;
	eaPush(&objectPicker.display_types, displayType);

	// public filter
	filter = StructCreate(parse_EMPickerFilter);
	filter->display_text = StructAllocString("Public");
	filter->checkF = objectPickerFilterPublicCheck;
	eaPush(&objectPicker.filters, filter);

	// global filter
	objectPicker.filter_func = objectPickerFilterCheck;

	if (!objectPickerPreviews)
		objectPickerPreviews = stashTableCreateInt( 32 );
	if (!objectPickerLargePreviews)
		objectPickerLargePreviews = stashTableCreateInt( 32 );
	resourceDefaultPreview = texAllocateScratch( "resourceDefaultPreview", 96, 96, WL_FOR_UTIL );
}

#define SMALL_SIZE 128
#define LARGE_SIZE 512

void objectLibraryGetPreview(const ResourceInfo *res_info, const void* pData, const ObjectLibraryPreviewParams *params, F32 size, BasicTexture** out_tex, Color* out_mod_color)
{
	GroupDef* group;
	ResourcePreviewTexture* previewTexture;
	static U32 lastFrameObjlibPreviewRequested = -1;
	StashTable pStash;
	F32 textureSize;
	U32 res_crc = res_info->resourceID;

	if (params)
	{
		res_crc ^= StructCRC(parse_ObjectLibraryPreviewParams, (void*)params);
	}

	if (size <= SMALL_SIZE)
	{
		if (!objectPickerPreviews)
			objectPickerPreviews = stashTableCreateInt( 32 );
		pStash = objectPickerPreviews;
		textureSize = SMALL_SIZE;
	}
	else
	{
		if (!objectPickerLargePreviews)
			objectPickerLargePreviews = stashTableCreateInt( 32 );
		pStash = objectPickerLargePreviews;
		textureSize = LARGE_SIZE;
	}

	if (!resourceDefaultPreview)
		resourceDefaultPreview = texAllocateScratch( "resourceDefaultPreview", 96, 96, WL_FOR_UTIL );

	if( stashIntFindPointer( pStash, res_crc, &previewTexture )) {
		assert( previewTexture && previewTexture->texture );

		*out_tex = previewTexture->texture;
		
		if( gfxHeadshotRaisePriority( previewTexture->texture )) {
			U32 curTime = timerCpuMs();
			if( !previewTexture->completeTime ) {
				previewTexture->completeTime = curTime;
			}
			
			*out_mod_color = ColorWhite;
			out_mod_color->a = CLAMP(lerp(0, 255, (curTime - previewTexture->completeTime) * 0.002f), 0, 255);
		} else {
			*out_mod_color = ColorTransparent;
			previewTexture->completeTime = 0;
		}
		return;
	}

	// Getting an groupdef can take extremely long, so I need to
	// throttle that requesting to make the editors more responsive.
	if(   inpDeltaTimeToLastInputEdge() < 500
		  || gfxGetFrameCount() - lastFrameObjlibPreviewRequested < 3 ) {
		*out_tex = resourceDefaultPreview;
		*out_mod_color = ColorTransparent;
		return;
	}
	lastFrameObjlibPreviewRequested = gfxGetFrameCount();

	if (!res_info) {
		*out_tex = resourceDefaultPreview;
		*out_mod_color = ColorTransparent;
		return;
	}

	model_override_use_flags = WL_FOR_PREVIEW_INTERNAL;
	//loadstart_printf( "Loading resource %s...", res_info->resourceName );
	group = objectLibraryGetGroupDef( res_info->resourceID, true );
	assert(group);
	//loadend_printf( "done" );
	model_override_use_flags = 0;

	previewTexture = calloc( 1, sizeof( *previewTexture ));
	previewTexture->texture = gfxHeadshotCaptureGroup( "emObjectPickerPreview", textureSize, textureSize, group, NULL, 
			(params && params->useAlpha) ? ColorTransparent : ColorBlack,
			params ? params->camType : GFX_HEADSHOT_OBJECT_AUTO,
			NULL,
			params ? params->skyName : NULL,
			(params && params->useNearPlane) ? params->nearPlane : -10000,
			params ? params->enableShadows : false,
			true,
			NULL, NULL);
	if( previewTexture->texture ) {
		stashIntAddPointer( pStash, res_crc, previewTexture, true );
		*out_tex = previewTexture->texture;
	} else {
		free( previewTexture );
		*out_tex = resourceDefaultPreview;
	}
	*out_mod_color = ColorTransparent;

	return;
}

void objectPickerPreview(EMPicker *picker, void *selected_data, ParseTable *parse_info, BasicTexture **out_tex, Color *out_mod_color)
{
	ResourceInfo* res_info = (ResourceInfo*)selected_data;
	
	if (parse_info != parse_ResourceInfo)
	{
		*out_tex = NULL;
		*out_mod_color = ColorTransparent;
		return;
	}
	if (!selected_data || stricmp(res_info->resourceDict, OBJECT_LIBRARY_DICT) != 0)
	{
		*out_tex = NULL;
		*out_mod_color = ColorTransparent;
		return;
	}

	objectLibraryGetPreview(res_info, NULL, NULL, 512, out_tex, out_mod_color);
}

void playerCostumeGetPreview(const ResourceInfo *res_info, const void* pData, const void* pExtraData, F32 size, BasicTexture** out_tex, Color* out_mod_color)
{
	const PlayerCostume* costume;
	ResourcePreviewTexture* previewTexture = NULL;
	static U32 lastFrameObjlibPreviewRequested = -1;
	F32 textureSize;
	char headshotKey[ 512 ];

	if (!costumePickerPreviews)
		costumePickerPreviews = stashTableCreateWithStringKeys( 32, StashDeepCopyKeys );
	textureSize = LARGE_SIZE;

	if (!resourceDefaultPreview)
		resourceDefaultPreview = texAllocateScratch( "resourceDefaultPreview", 96, 96, WL_FOR_UTIL );

	if (res_info) {
		sprintf( headshotKey, "%s+%s", res_info->resourceName, (pExtraData ? pExtraData : "") );
	} else if (pData) {
		const PlayerCostume* costumeData = pData;
		sprintf( headshotKey, "%s+%s", costumeData->pcName, (pExtraData ? pExtraData : "") );
	}	
	stashFindPointer(costumePickerPreviews, headshotKey, &previewTexture);

	if( previewTexture ) {
		assert( previewTexture->texture );

		*out_tex = previewTexture->texture;
		
		if( gfxHeadshotRaisePriority( previewTexture->texture )) {
			U32 curTime = timerCpuMs();
			if( !previewTexture->completeTime ) {
				previewTexture->completeTime = curTime;
			}
			
			*out_mod_color = ColorWhite;
			out_mod_color->a = CLAMP(lerp(0, 255, (curTime - previewTexture->completeTime) * 0.002f), 0, 255);
		} else {
			*out_mod_color = ColorTransparent;
			previewTexture->completeTime = 0;
		}
		return;
	}

	if (res_info) {
		costume = RefSystem_ReferentFromString( g_hPlayerCostumeDict, res_info->resourceName );
	} else if (pData) {
		costume = pData;
	} else {
		costume = NULL;
	}

	if (!costume) {
		*out_tex = resourceDefaultPreview;
		*out_mod_color = ColorTransparent;
		return;
	}

	previewTexture = calloc( 1, sizeof( *previewTexture ));
	previewTexture->texture = gclHeadshotFromCostume(pExtraData, costume, NULL, textureSize, textureSize, NULL, NULL);
	stashAddPointer( costumePickerPreviews, headshotKey, previewTexture, true );

	*out_tex = previewTexture->texture;
	*out_mod_color = ColorTransparent;
	return;
}

void textureGetPreview(const ResourceInfo *res_info, const void* pData, const void* pExtraData, F32 size, BasicTexture** out_tex, Color* out_mod_color)
{
	BasicTexture* tex;

	if (res_info) {
		tex = texFind( res_info->resourceName, false );
	} else {
		tex = NULL;
	}

	*out_tex = tex;
	*out_mod_color = ColorWhite;
}

/// Clear the object picker preview cache for testing responsiveness.
///
/// (Yes, this leaks resources.  No, it should never be called in
/// production code.)
AUTO_COMMAND;
void objectPickerPreviewCacheClear( void )
{
	stashTableClear( objectPickerPreviews );
	stashTableClear( objectPickerLargePreviews );
	stashTableClear( costumePickerPreviews );
}

/* PICKER TEST CODE
static void objectPickerSelect(EMPicker *picker, EMPickerSelection **selections, ResourceInfo *entry)
{

}

AUTO_COMMAND ACMD_NAME("Editor.Picker");
void objectPickerOpen(void)
{
emPickerShow(objectPicker, "test", false, objectPicker, NULL);
}
*/

#endif

// Intentionally do nothing
static void doNothing(void)
{
}

AUTO_RUN_LATE;
void RegisterDictionaryPreviews(void)
{
#ifndef NO_EDITORS
	resRegisterPreviewCallback(OBJECT_LIBRARY_DICT, objectLibraryGetPreview, NULL, emClearPreviews);
	resRegisterPreviewCallback(g_hPlayerCostumeDict, playerCostumeGetPreview, NULL, emClearPreviews);
	resRegisterPreviewCallback("Texture", textureGetPreview, NULL, doNothing);
#endif
}

#include "EditorManagerUI_h_ast.c"
