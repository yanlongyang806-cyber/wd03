/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "EditorManagerPrivate.h"
#include "EditorPrefs.h"
#include "EditLibUIUtil.h"
#include "estring.h"
#include "inputMouse.h"

#define EM_TOOLBAR_HEIGHT 30
#define TOOLBAR_DRAG_TYPE "EM_TOOLBAR"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

/********************
* STRUCTS
********************/
// the main toolbar struct
typedef struct EMToolbar
{
	char *name_id;
	UIPane outer_pane;				// encloses entire toolbar
	UIButton *reorder_button;		// used for dragging toolbars around
	UIPane *custom_pane;			// where the user's UI widgets go
} EMToolbar;

/********************
* FORWARD DECLARATIONS
********************/
void emEditorToolbarDisplay(EMEditor *editor);

/********************
* GLOBAL TOOLBARS
********************/
// global chooser callbacks
static void emUIChooserCancelButtonClicked(UIButton *button, void *unused)
{
	ui_WindowHide(em_data.toolbar_chooser.window);
	ui_WidgetQueueFree(UI_WIDGET(em_data.toolbar_chooser.window));
}

static bool emUIChooserWindowClosed(UIWindow *window, void *unused)
{
	emUIChooserCancelButtonClicked(NULL, NULL);
	return true;
}

// new chooser
static void emUINewChooserAction(UIWidget *widget, void *unused)
{
	int pos;

	EditorPrefStoreWindowPosition("Editor Manager", "Window Position", "New Chooser", em_data.toolbar_chooser.window);
	ui_WindowHide(em_data.toolbar_chooser.window);
	ui_WidgetQueueFree(UI_WIDGET(em_data.toolbar_chooser.window));

	// figure out which editor was selected
	pos = ui_ListGetSelectedRow(em_data.toolbar_chooser.list);
	if (pos >= 0)
		emNewDoc(em_data.toolbar_chooser.new_types[pos]->type_name, NULL);
}

static void emUINewChooserDrawText(UIList *list, UIListColumn *column, S32 row, void *unused, char **estring)
{
	estrAppend2(estring, em_data.toolbar_chooser.new_types[row]->display_name);
}

static int emUINewChooserCompareTypes(const EMRegisteredType **type1, const EMRegisteredType **type2)
{
	return stricmp((*type1)->display_name, (*type2)->display_name);
}

static void emUINewChooserShow(void)
{
	UIWindow *window;
	UIButton *button;
	UIList *list;
	UIListColumn *column;
	int i, j;

	// clear out previous names
	eaDestroy(&em_data.toolbar_chooser.new_types);

	if (!em_data.active_workspace)
		return;

	// set up names
	for(i = eaSize(&em_data.active_workspace->editors) - 1; i >= 0; i--)
	{
		if (!em_data.active_workspace->editors[i]->allow_multiple_docs && eaSize(&em_data.active_workspace->editors[i]->open_docs) > 0)
			continue;

		for (j = 0; j < eaSize(&em_data.active_workspace->editors[i]->registered_types); j++)
		{
			EMRegisteredType *type = em_data.active_workspace->editors[i]->registered_types[j];
			if (type->display_name)
				eaPush(&em_data.toolbar_chooser.new_types, type);
		}
	}
	eaQSort(em_data.toolbar_chooser.new_types, emUINewChooserCompareTypes);

	// set up window
	window = ui_WindowCreate("New Document", 100, 100, 200, 200);
	EditorPrefGetWindowPosition("Editor Manager", "Window Position", "New Chooser", window);

	list = ui_ListCreate(NULL, &em_data.toolbar_chooser.new_types, 14);
	column = ui_ListColumnCreateText("Document Type", emUINewChooserDrawText, NULL);
	ui_ListColumnSetWidth(column, false, 1);
	ui_ListAppendColumn(list, column);
	ui_ListSetActivatedCallback(list, emUINewChooserAction, NULL);
	ui_WidgetSetDimensionsEx(UI_WIDGET(list), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(list), 0, 0, 0, 26);
	ui_WindowAddChild(window, list);
	em_data.toolbar_chooser.list = list;

	button = ui_ButtonCreate("New", 0, 0, emUINewChooserAction, NULL);
	ui_WidgetSetPositionEx(UI_WIDGET(button), 90, 0, 0, 0, UIBottomRight);
	ui_WidgetSetWidth(UI_WIDGET(button), 80);
	ui_WindowAddChild(window, button);

	button = ui_ButtonCreate("Cancel", 0, 0, emUIChooserCancelButtonClicked, NULL);
	ui_WidgetSetPositionEx(UI_WIDGET(button), 0, 0, 0, 0, UIBottomRight);
	ui_WidgetSetWidth(UI_WIDGET(button), 80);
	ui_WindowAddChild(window, button);

	ui_WindowSetClosable(window, true);
	ui_WindowSetCloseCallback(window, emUIChooserWindowClosed, NULL);
	ui_WindowSetModal(window, true);
	ui_WindowPresent(window);
	ui_SetFocus(UI_WIDGET(em_data.toolbar_chooser.list));

	em_data.toolbar_chooser.window = window;
}

// open chooser
static void emUIOpenChooserAction(UIWidget *widget, void *unused)
{
	int pos;

	EditorPrefStoreWindowPosition("Editor Manager", "Window Position", "Open Chooser", em_data.toolbar_chooser.window);
	ui_WindowHide(em_data.toolbar_chooser.window);
	ui_WidgetQueueFree(UI_WIDGET(em_data.toolbar_chooser.window));

	// figure out which picker was selected
	pos = ui_ListGetSelectedRow(em_data.toolbar_chooser.list);
	if (pos >= 0)
		emPickerShow(em_data.toolbar_chooser.pickers[pos], NULL, true, NULL, NULL);
}

static void emUIOpenChooserDrawText(UIList *list, UIListColumn *column, S32 row, void *unused, char **estring)
{
	estrAppend2(estring, em_data.toolbar_chooser.pickers[row]->picker_name);
}

static int emUIOpenChooserComparePickers(const EMPicker **picker1, const EMPicker **picker2)
{
	return stricmp((*picker1)->picker_name, (*picker2)->picker_name);
}

static void emUIOpenChooserShow()
{
	UIWindow *window;
	UIButton *button;
	UIList *list;
	UIListColumn *column;
	int i;

	// clear out previous pickers
	eaDestroy(&em_data.toolbar_chooser.pickers);

	if (!em_data.active_workspace)
		return;

	// set up pickers
	for(i = eaSize(&em_data.active_workspace->editors) - 1; i >= 0; i--)
		eaPushEArray(&em_data.toolbar_chooser.pickers, &em_data.active_workspace->editors[i]->pickers);
	eaQSort(em_data.toolbar_chooser.pickers, emUIOpenChooserComparePickers);

	// Set up window
	window = ui_WindowCreate("Open Picker", 100, 100, 200, 200);
	EditorPrefGetWindowPosition("Editor Manager", "Window Position", "Open Chooser", window);

	list = ui_ListCreate(NULL, &em_data.toolbar_chooser.pickers, 14);
	column = ui_ListColumnCreateText("Library", emUIOpenChooserDrawText, NULL);
	ui_ListColumnSetWidth( column, false, 1 );
	ui_ListAppendColumn(list, column);
	ui_ListSetActivatedCallback(list, emUIOpenChooserAction, NULL);
	ui_WidgetSetDimensionsEx(UI_WIDGET(list), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(list), 0, 0, 0, 26);
	ui_WindowAddChild(window, list);
	em_data.toolbar_chooser.list = list;

	button = ui_ButtonCreate("Open", 0, 0, emUIOpenChooserAction, NULL);
	ui_WidgetSetPositionEx(UI_WIDGET(button), 90, 0, 0, 0, UIBottomRight);
	ui_WidgetSetWidth(UI_WIDGET(button), 80);
	ui_WindowAddChild(window, button);

	button = ui_ButtonCreate("Cancel", 0, 0, emUIChooserCancelButtonClicked, NULL);
	ui_WidgetSetPositionEx(UI_WIDGET(button), 0, 0, 0, 0, UIBottomRight);
	ui_WidgetSetWidth(UI_WIDGET(button), 80);
	ui_WindowAddChild(window, button);

	ui_WindowSetClosable(window, true);
	ui_WindowSetCloseCallback(window, emUIChooserWindowClosed, NULL);
	ui_WindowSetModal(window, true);
	ui_WindowPresent(window);
	ui_SetFocus(UI_WIDGET(em_data.toolbar_chooser.list));

	em_data.toolbar_chooser.window = window;
}

// toolbar callbacks
static void emUINewButtonClicked(UIButton *button, UserData unused)
{
	EMEditor *editor = NULL;

	if (em_data.active_workspace && eaSize(&em_data.active_workspace->editors) == 1)
		emNewDoc(em_data.active_workspace->editors[0]->default_type, NULL);
	else
		emUINewChooserShow();
}

static void emUIOpenButtonClicked(UIButton *button, UserData unused)
{
	EMEditor *editor = NULL;

	if (em_data.active_workspace 
		&& eaSize(&em_data.active_workspace->editors) == 1
		&& em_data.active_workspace->editors[0]->pickers)
	{
		if (eaSize(&em_data.active_workspace->editors[0]->pickers) > 0)
			emPickerShow(em_data.active_workspace->editors[0]->pickers[0], NULL, true, NULL, NULL);
	}
	else
		emUIOpenChooserShow();
}

static void emUISaveButtonClicked(UIButton *button, UserData unused)
{
	EMEditorDoc *doc = emGetActiveEditorDoc();
	if (!doc)
		return;
	emQueueFunctionCallStatus(NULL, NULL, emSaveDoc, doc, -1);
}

static void emUISaveAllButtonClicked(UIButton *button, UserData unused) {
	EMEditorDoc *doc = emGetActiveEditorDoc();

	if (!doc)
		return;

	FOR_EACH_IN_EARRAY(doc->ui_tab->pTabGroup->eaTabs,UITab,currentTab) {
		emQueueFunctionCallStatus(NULL, NULL, emSaveDoc, (EMEditorDoc*)currentTab->pContextData, -1);
	}
	FOR_EACH_END;
}

void emUICameraButtonRefresh(void)
{
	if (em_data.title.freecam_button)
	{
		ui_ButtonSetText(em_data.title.freecam_button, em_data.worldcam->mode_switch ? "Free camera" : "Standard camera");
		ui_WidgetSetTooltipString(UI_WIDGET(em_data.title.freecam_button), em_data.worldcam->mode_switch ? "Free camera control" : "Standard camera control");
	}
}

static void emUICameraButtonClicked(UIButton *button, UserData unused)
{
	emSetFreecam(!em_data.worldcam->mode_switch);
}

/******
* This creates the file toolbar in a standard way.  The flags indicate which buttons should be available.
* PARAMS:
*   flags - The buttons to make available on the toolbar
******/
EMToolbar *emToolbarCreateFileToolbar(U32 flags)
{
	EMToolbar *toolbar;
	UIButton *button;
	F32 x = 0;

	toolbar = emToolbarCreate(10);

	if (flags & EM_FILE_TOOLBAR_NEW)
	{
		button = ui_ButtonCreate("New", x, 0, emUINewButtonClicked, NULL);
		button->widget.height = emToolbarGetHeight(toolbar);
		x += button->widget.width + 5;
		emToolbarAddChild(toolbar, button, true);
	}
	if (flags & EM_FILE_TOOLBAR_OPEN)
	{
		button = ui_ButtonCreate("Open", x, 0, emUIOpenButtonClicked, NULL);
		button->widget.height = emToolbarGetHeight(toolbar);
		x += button->widget.width + 5;
		emToolbarAddChild(toolbar, button, true);
	}
	if (flags & EM_FILE_TOOLBAR_SAVE)
	{
		button = ui_ButtonCreate("Save", x, 0, emUISaveButtonClicked, NULL);
		button->widget.height = emToolbarGetHeight(toolbar);
		x += button->widget.width + 5;
		emToolbarAddChild(toolbar, button, true);
	}
	if (flags & EM_FILE_TOOLBAR_SAVE_ALL)
	{
		button = ui_ButtonCreate("Save All", x, 0, emUISaveAllButtonClicked, NULL);
		button->widget.height = emToolbarGetHeight(toolbar);
		x += button->widget.width + 5;
		emToolbarAddChild(toolbar, button, true);
	}
	emToolbarSetWidth(toolbar,x);

	return toolbar;
}

static void emUIObjectWindowClicked( UIButton* ignored, UserData ignored2 )
{
	emShowZeniPicker();
}

EMToolbar* emToolbarCreateWindowToolbar(void)
{
	EMToolbar* toolbar;
	UIButton* button;
	F32 x = 0;
	
	toolbar = emToolbarCreate(10);
	
	button = ui_ButtonCreate("Object Window", x, 0, emUIObjectWindowClicked, NULL);
	button->widget.height = emToolbarGetHeight(toolbar);
	x += button->widget.width + 5;
	emToolbarAddChild(toolbar, button, true);

	emToolbarSetWidth( toolbar, x );

	return toolbar;
}

/******
* This creates the camera toolbar that is displayed for world-rendering editors.
* RETURNS:
*   EMToolbar with the camera control buttons
******/
EMToolbar *emToolbarCreateCameraToolbar(void)
{
	EMToolbar *toolbar;

	toolbar = emToolbarCreate(0);
	if (!em_data.title.freecam_button)
	{
		em_data.title.freecam_button = ui_ButtonCreate("Standard camera", 0, 0, emUICameraButtonClicked, NULL);
		em_data.title.freecam_button->widget.height = emToolbarGetHeight(toolbar);
	}
	emUICameraButtonRefresh();
	emToolbarAddChild(toolbar, em_data.title.freecam_button, true);
	return toolbar;
}


/********************
* TOOLBAR MANAGEMENT
********************/
/******
* This function overrides each toolbar's tick function and is used to determine destinations
* when dragging toolbars around.
* PARAMS:
*   pane - UIPane of the toolbar being ticked
******/
static void emToolbarTick(UIPane *pane, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pane);

	UIDnDPayload *payload = ui_DragIsActive();
	UIPane *src_pane;
	int mX, mY, dest, src, dest_idx, src_idx;

	// call usual tick function
	ui_PaneTick(pane, pX, pY, pW, pH, pScale);

	// if the pane is being dragged, add custom functionality
	if (!payload || strcmp(payload->type, TOOLBAR_DRAG_TYPE) != 0)
		return;

	// Skip if there is no editor right now
	if (!em_data.current_editor)
		return;

	// find where the dragged toolbar would reasonably end up given the current position
	// of the mouse
	src_pane = (UIPane*) payload->payload;
	dest = eaFind(&em_data.current_editor->toolbars, (EMToolbar*)pane);
	src = eaFind(&em_data.current_editor->toolbars, (EMToolbar*)src_pane);
	dest_idx = eaiFind(&em_data.current_editor->toolbar_order, dest);
	src_idx = eaiFind(&em_data.current_editor->toolbar_order, src);

	mousePos(&mX, &mY);
	if (dest == src)
		return;
	else if (dest_idx < src_idx)
		box.hx -= MAX(0, pane->widget.width - src_pane->widget.width);
	else if (dest_idx > src_idx)
		box.lx += MAX(0, pane->widget.width - src_pane->widget.width);

	if (point_cbox_clsn(mX, mY, &box))
	{
		assert(src_idx >= 0 && dest_idx >= 0);
		eaiMove(&em_data.current_editor->toolbar_order, dest_idx, src_idx);

		// redraw the toolbars in the new order
		emEditorToolbarDisplay(em_data.current_editor);
	}
}

/******
* This function is called when the user drags from the toolbar reorganize button.
* PARAMS:
*    button - UIButton reorganize button
*    toolbar - EMToolbar being dragged
******/
static void emToolbarDrag(UIButton *button, EMToolbar *toolbar)
{
	// TODO: reimplement this later
//	ui_DragStart(UI_WIDGET(&toolbar->outer_pane), TOOLBAR_DRAG_TYPE, toolbar, atlasLoadTexture("eui_handle_vert"));
}

/******
* This function creates a new toolbar, which can be added to any editor and handled automatically
* by the Editor Manager.
* PARAMS:
*   width - int width of the toolbar in pixels
* RETURNS:
*   EMToolbar ready for use with the Editor Manager.
******/
EMToolbar *emToolbarCreateEx(const char *name_id, int width)
{
	EMToolbar *tb = calloc(1, sizeof(EMToolbar));

	if(name_id && name_id[0])
		tb->name_id = StructAllocString(name_id);
	tb->reorder_button = ui_ButtonCreate("", 2, 0, NULL, NULL);
	ui_WidgetSetDimensionsEx(UI_WIDGET(tb->reorder_button), 7, 1, UIUnitFixed, UIUnitPercentage);
	ui_WidgetSetDragCallback(UI_WIDGET(tb->reorder_button), emToolbarDrag, tb);
	ui_PaneInitialize(&tb->outer_pane, 0, 0, tb->reorder_button->widget.width + 4, EM_TOOLBAR_HEIGHT, UIUnitFixed, UIUnitFixed, 0 MEM_DBG_PARMS_INIT);
	tb->outer_pane.widget.tickF = emToolbarTick;
	ui_WidgetSetFamily(UI_WIDGET(&tb->outer_pane), UI_FAMILY_EDITOR);
	ui_WidgetAddChild(UI_WIDGET(&tb->outer_pane), UI_WIDGET(tb->reorder_button));
	tb->custom_pane = ui_PaneCreate(elUINextX(tb->reorder_button) + 5, 0, 0, 1, UIUnitFixed, UIUnitPercentage, 0);
	tb->custom_pane->invisible = true;
	ui_WidgetSetFamily(UI_WIDGET(tb->custom_pane), UI_FAMILY_EDITOR);
	ui_WidgetAddChild(UI_WIDGET(&tb->outer_pane), UI_WIDGET(tb->custom_pane));
	emToolbarSetWidth(tb, width);
	return tb;
}

/******
* This function destroys an EMToolbar.  Do NOT call this until you've removed the toolbar from the editor's
* toolbar EArray.  Also note that this will free the widgets inside of the toolbar.
* PARAMS:
*   toolbar - EMToolbar to destroy
******/
void emToolbarDestroy(EMToolbar *toolbar)
{
	StructFreeStringSafe(&toolbar->name_id);
	ui_WidgetGroupFreeInternal(&toolbar->outer_pane.widget.children);
	free(toolbar);
}

/******
* This gets the drawable height within a toolbar pane (in case implementers need square buttons and want
* to know the height dimension).
* RETURNS:
*    int height of drawable area in toolbar pane
******/
int emToolbarGetHeight(EMToolbar *toolbar)
{
	return EM_TOOLBAR_HEIGHT - 10;
}

const char* emToolbarGetNameId(EMToolbar *toolbar)
{
	return toolbar->name_id;
}

/******
* This function adds a widget to a toolbar.  This can optionally update the toolbar's
* width, as well.
* PARAMS:
*   toolbar - EMToolbar to which the child should be added
*   widget - UIWidget to add as a child
*   update_width - bool indicating whether the toolbar's width should be changed according
*                  to the width of the widgets in the toolbar.
******/
void emToolbarAddChild(EMToolbar *toolbar, UIAnyWidget *widget, bool update_width)
{
	ui_WidgetAddChild(UI_WIDGET(toolbar->custom_pane), (UIWidget*) widget);
	if (update_width)
		emToolbarSetWidth(toolbar, elUIGetEndX(toolbar->custom_pane->widget.children) + 5);
}

/******
* This function adds a widget to a toolbar.  This can optionally update the toolbar's
* width, as well.
* PARAMS:
*   toolbar - EMToolbar to which the child should be removed
*   widget - UIWidget to remove
*   update_width - bool indicating whether the toolbar's width should be changed according
*                  to the width of the widgets left in the toolbar.
******/
void emToolbarRemoveChild(EMToolbar *toolbar, UIAnyWidget *widget, bool update_width)
{
	ui_WidgetRemoveChild(UI_WIDGET(toolbar->custom_pane), (UIWidget*) widget);
	if (update_width)
		emToolbarSetWidth(toolbar, elUIGetEndX(toolbar->custom_pane->widget.children) + 5);
}

/******
* This sets a toolbar's width.
* PARAMS:
*    toolbar - EMToolbar to modify
*    width - int new width
******/
void emToolbarSetWidth(EMToolbar *toolbar, int width)
{
	toolbar->custom_pane->widget.width = width;
	toolbar->outer_pane.widget.width = elUINextX(toolbar->custom_pane) + 5;
}

/******
* This enables/disables a toolbar.
* PARAMS:
*   toolbar - EMToolbar to modify
*   active - bool indicating whether to enable or disable the toolbar
******/
void emToolbarSetActive(EMToolbar *toolbar, bool active)
{
	ui_SetActive(UI_WIDGET(toolbar->custom_pane), active);
}


/********************
* MAIN
********************/
/******
* This function creates the main toolbar pane used in the Editor Manager, where all of the toolbars
* from the active editor will reside.
* RETURNS:
*   UIPane that was created
******/
UIPane *emToolbarPaneCreate(void)
{
	UIPane *pane = ui_PaneCreate(0, 30, 1, EM_TOOLBAR_HEIGHT, UIUnitPercentage, UIUnitFixed, 0);
	ui_WidgetSetFamily(UI_WIDGET(pane), UI_FAMILY_EDITOR);
	pane->widget.rightPad = pane->widget.leftPad = 5;
	pane->widget.y = 25;
	pane->invisible = true;
	return pane;
}

/******
* This function deals with stacking toolbars on top of each other in case the screen space doesn't allow all of them to
* appear on the same row.
* PARAMS:
*   editor - EMEditor whose toolbars are to be stacked
******/
void emEditorToolbarStack(EMEditor *editor)
{
	// probably don't need to stack global/default toolbar(s)
	if (editor)
	{
		int i;
		int x = 0;
		int y = 0;
		int toolbar_pane_end = g_ui_State.screenWidth - EditorPrefGetFloat("Sidebar", "Option", "Scale", 1.0f) * (em_data.sidebar.show ? em_data.sidebar.pane->widget.width : em_data.sidebar.hidden_pane->widget.width);

		for (i = 0; i < eaSize(&editor->toolbars); i++)
		{
			EMToolbar *toolbar = editor->toolbars[i];
			if(toolbar->name_id) {
				bool state = EditorPrefGetInt(editor->editor_name, TOOLBAR_VIS_PREF, toolbar->name_id, 1);
				if(!state)
					continue;
			}
			if (x != 0 && ((x + 15 + toolbar->outer_pane.widget.width) * g_ui_State.scale) > toolbar_pane_end)
			{
				x = 0;
				y += EM_TOOLBAR_HEIGHT;
			}
			toolbar->outer_pane.widget.x = x;
			toolbar->outer_pane.widget.y = y;
			x += toolbar->outer_pane.widget.width;
		}

		em_data.title.toolbar->widget.height = y + EM_TOOLBAR_HEIGHT;
		em_data.title.pane->widget.height = elUINextY(em_data.title.toolbar) + 5;
	}
}

/******
* This function renders the toolbars on the main toolbar pane for a specified editor.
* PARAMS:
*   editor - EMEditor whose toolbars will be rendered
******/
void emEditorToolbarDisplay(EMEditor *editor)
{
	int i, x = 0;
	for (i = eaSize(&em_data.title.toolbar->widget.children) - 1; i >= 0; i--)
		ui_WidgetRemoveChild(UI_WIDGET(em_data.title.toolbar), em_data.title.toolbar->widget.children[i]);

	if (!editor)
	{
		// add global toolbar
		ui_WidgetAddChild(UI_WIDGET(em_data.title.toolbar), UI_WIDGET(&em_data.title.global_toolbar->outer_pane));
		em_data.title.global_toolbar->outer_pane.widget.x = 0;
		
		ui_WidgetAddChild(UI_WIDGET(em_data.title.toolbar), UI_WIDGET(&em_data.title.window_toolbar->outer_pane));
		em_data.title.window_toolbar->outer_pane.widget.x = em_data.title.global_toolbar->outer_pane.widget.width;
	}
	else
	{
		// add editor toolbars in order
		// TODO: reenable toolbar ordering according to names and not indexes
		/*for (i = 0; i < eaiSize(&editor->toolbar_order); i++)
		{
			int next_tb = editor->toolbar_order[i];
			UIPane *next_pane;
			if (next_tb >= eaSize(&editor->toolbars))
				continue;
			next_pane = &editor->toolbars[next_tb]->outer_pane;
			next_pane->widget.x = x;
			x += next_pane->widget.width;
			ui_WidgetAddChild(UI_WIDGET(em_data.title.toolbar), UI_WIDGET(next_pane));
		}*/
		for (i = 0; i < eaSize(&editor->toolbars); i++)
		{
			EMToolbar *toolbar = editor->toolbars[i];
			UIPane *next_pane = &toolbar->outer_pane;
			if(toolbar->name_id) {
				bool state = EditorPrefGetInt(editor->editor_name, TOOLBAR_VIS_PREF, toolbar->name_id, 1);
				if(!state)
					continue;
			}
			next_pane->widget.x = x;
			x += next_pane->widget.width;
			ui_WidgetAddChild(UI_WIDGET(em_data.title.toolbar), UI_WIDGET(next_pane));
		}
	}
}


UIWidget* emToolbarGetPaneWidget(EMToolbar* toolbar)
{
	return UI_WIDGET(toolbar->custom_pane);
}

#endif
