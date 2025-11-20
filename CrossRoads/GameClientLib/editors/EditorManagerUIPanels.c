/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "EditorManagerUIPanels.h"
#include "EditorManagerUIMotD.h"
#include "EditorManagerPrivate_h_ast.h"
#include "EditorManagerPrivate.h"
#include "EditorPrefs.h"
#include "EditLibUIUtil.h"
#include "EditLibClipboard.h"
#include "TerrainEditorPrivate.h"
#include "GraphicsLib.h"
#include "GfxTexAtlas.h"
#include "GfxSprite.h"
#include "GfxSpriteText.h"
#include "EString.h"
#include "GfxClipper.h"

#define EM_MSGLOG_FLASH_MAX 10
#define EM_MSGLOG_FLASH_FRAMES 180

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););


/********************
* HELPER FUNCTIONS
********************/
static void emPanelMoveWidgets(UIWidget *to_widget, UIWidget *from_widget)
{
	while (eaSize(&from_widget->children) != 0)
	{
		UIWidget *widget = from_widget->children[0];
		ui_WidgetRemoveChild(from_widget, widget);
		ui_WidgetAddChild(to_widget, widget);
	}
}

static void emPanelEditorPrefStoreIntOrWindow(UIExpander *expander, const char *string, int new_value, UIWindow *window)
{
	int i;
	bool found = false;
	const char* expanderText = ui_WidgetGetText( UI_WIDGET( expander ));

	// Save preference change on expander when it is toggled
	if (em_data.current_doc)
	{
		for(i = eaSize(&em_data.current_doc->em_panels)-1; i >= 0; i--)
		{
			if (em_data.current_doc->em_panels[i]->ui_expander == expander)
			{
				if(window)
					EditorPrefStoreWindowPosition(em_data.current_doc->editor->editor_name, string, expanderText, window);
				else
					EditorPrefStoreInt(em_data.current_doc->editor->editor_name, string, expanderText, new_value);
				found = true;
				break;
			}
		}
	}
	if (!found) 
	{
		for(i = eaSize(&em_data.sidebar.panels)-1; i >= 0; i--)
		{
			if (em_data.sidebar.panels[i]->ui_expander == expander)
			{
				if(window)
					EditorPrefStoreWindowPosition("Editor Manager", string, expanderText, window);
				else
					EditorPrefStoreInt("Editor Manager", string, expanderText, new_value);
				break;
			}
		}
	}
}

/********************
* PANEL MANAGEMENT
********************/
static void emPanelExpandChanged(UIExpander *expander, void *unused)
{
	emPanelEditorPrefStoreIntOrWindow(expander, "Sidebar Panel Open", ui_ExpanderIsOpened(expander), NULL);
}

static void emPanelMoveToWindow(void *unused, EMPanel *panel)
{
	emPanelEditorPrefStoreIntOrWindow(panel->ui_expander, "Sidebar Panel Windowed", true, NULL);
	panel->windowed = true;
	emPanelsShow(em_data.current_doc, NULL);
	ui_SetFocus(panel->ui_window);
}

static void emPanelExpanderRightClickCB(void *unused, EMPanel *panel)
{
	int i;
	static UIMenu *emPanleRightClickMenu;
	if (!emPanleRightClickMenu) {
		emPanleRightClickMenu = ui_MenuCreateWithItems("Panel Right Click Menu",
			ui_MenuItemCreate("Move to window", UIMenuCallback, emPanelMoveToWindow, panel, NULL),
			NULL);
	}
	for ( i=0; i < eaSize(&emPanleRightClickMenu->items); i++ )
		emPanleRightClickMenu->items[i]->clickedData = panel;
	ui_MenuPopupAtCursor(emPanleRightClickMenu);
}

static void emPanelMoveToExpander(void *unused, EMPanel *panel)
{
	emPanelEditorPrefStoreIntOrWindow(panel->ui_expander, "Sidebar Panel Windowed", false, NULL);
	panel->windowed = false;
	emPanelsShow(em_data.current_doc, NULL);
}

static bool emPanelWindowClosed(void *unused, EMPanel *panel)
{
	emPanelEditorPrefStoreIntOrWindow(panel->ui_expander, "Sidebar Panel Window Position", 0, panel->ui_window);
	emPanelMoveToExpander(NULL, panel);
	return true;
}

static void emPanelWindowChanged(void *unused, EMPanel *panel)
{
	emPanelEditorPrefStoreIntOrWindow(panel->ui_expander, "Sidebar Panel Window Position", 0, panel->ui_window);
}

static void emPanelWindowRightClickCB(UIExpander *expander, EMPanel *panel)
{
	int i;
	static UIMenu *emPanleRightClickMenu;
	if (!emPanleRightClickMenu) {
		emPanleRightClickMenu = ui_MenuCreateWithItems("Panel Right Click Menu",
			ui_MenuItemCreate("Move to side panel", UIMenuCallback, emPanelMoveToExpander, panel, NULL),
			NULL);
	}
	for ( i=0; i < eaSize(&emPanleRightClickMenu->items); i++ )
		emPanleRightClickMenu->items[i]->clickedData = panel;
	ui_MenuPopupAtCursor(emPanleRightClickMenu);
}

/******
* This function creates a new panel for a specified tab.
* PARAMS:
*   tab_name - string name of the tab where the panel will go
*   expander_name - string name for the panel that appears on the expander
*   expander_height - height of the expander
* RETURNS:
*   EMPanel created
******/
EMPanel *emPanelCreate(const char *tab_name, const char *expander_name, int expander_height)
{
	char window_name[256];
	EMPanel *panel = calloc(1, sizeof(*panel));
	panel->tab_name = strdup(tab_name);
	panel->active = true;
	panel->ui_expander = ui_ExpanderCreate(expander_name, expander_height);
	ui_ExpanderSetHeaderContextCallback(panel->ui_expander, emPanelExpanderRightClickCB, panel);
	ui_WidgetSetClickThrough(UI_WIDGET(panel->ui_expander), true);
	ui_WidgetSkin(UI_WIDGET(panel->ui_expander), em_data.sidebar.expander_group_skin);
	ui_ExpanderSetExpandCallback(panel->ui_expander, emPanelExpandChanged, NULL);

	sprintf(window_name, "%s - %s", tab_name, expander_name);
	panel->ui_window = ui_WindowCreate(window_name, 25, 75, 1, 1);
	ui_WidgetSetContextCallback(UI_WIDGET(panel->ui_window), emPanelWindowRightClickCB, panel);
	ui_WindowSetMovedCallback(panel->ui_window, emPanelWindowChanged, panel);
	ui_WindowSetResizedCallback(panel->ui_window, emPanelWindowChanged, panel);
	ui_WindowSetDimensions(panel->ui_window, 350, expander_height, 350, 100);
	ui_WindowSetCloseCallback(panel->ui_window, emPanelWindowClosed, panel);
	ui_WidgetSetFamily(UI_WIDGET(panel->ui_window), UI_FAMILY_EDITOR);

	panel->ui_scroll = ui_ScrollAreaCreate(0,0,1,1,1,1,false,true);
	panel->ui_scroll->autosize = true;
	ui_WidgetSetDimensionsEx(UI_WIDGET(panel->ui_scroll), 1, 1, UIUnitPercentage, UIUnitPercentage);
	ui_WindowAddChild(panel->ui_window, panel->ui_scroll);

	panel->ui_pane = ui_PaneCreate(0,0,1,expander_height, UIUnitPercentage, UIUnitFixed, 0);
	ui_PaneSetInvisible(panel->ui_pane, true);
	ui_ScrollAreaAddChild(panel->ui_scroll, UI_WIDGET(panel->ui_pane));
	return panel;
}

/******
* This function frees a particular EMPanel.  Care should be taken to ensure the freed panel
* is removed from the document's EArray first or that the document is being closed.
* PARAMS:
*   panel - EMPanel to free
******/
void emPanelFree(EMPanel *panel)
{
	if (!panel)
		return;

	SAFE_FREE(panel->tab_name);
	ui_WidgetQueueFree(UI_WIDGET(panel->ui_expander));
	ui_WidgetQueueFree(UI_WIDGET(panel->ui_window));
	free(panel);
}

/******
* This function returns the name of the EMPanel (i.e. the name shown on the expander).
* PARAMS:
*   panel - EMPanel whose name is to be found
* RETURNS:
*   string name of the panel shown on its expander
******/
const char *emPanelGetName(EMPanel *panel)
{
	assert(panel->ui_expander);
	return ui_WidgetGetText(UI_WIDGET(panel->ui_expander));
}

/******
* This function changes the name of the panel.
* PARAMS:
*   panel - pointer to the panel
*   expander_name - string name for the panel that appears on the expander
******/

void emPanelSetName(EMPanel *panel, const char* expander_name)
{
	ui_ExpanderSetName(panel->ui_expander, expander_name);
}

/******
* This function adds a widget to a particular panel, optionally updating the height of the panel.
* PARAMS:
*   panel - EMPanel where the child widget will be added
*   widget - UIWidget to add to the panel
*   update_height - bool to reset the height of the panel according to its widget contents
******/
void emPanelAddChild(EMPanel *panel, UIAnyWidget *widget, bool update_height)
{
	assert(panel->ui_expander);
	if(panel->windowed)
		ui_PaneAddChild(panel->ui_pane, widget);
	else
		ui_ExpanderAddChild(panel->ui_expander, widget);
	if (update_height)
		emPanelUpdateHeight(panel);
}

/******
* This function forces an update of the panel's height
* PARAMS:
*   panel - EMPanel to update the height
******/
void emPanelUpdateHeight(SA_PARAM_NN_VALID EMPanel *panel)
{
	int height;
	if(panel->windowed)
		height = elUIGetEndY(panel->ui_pane->widget.children);
	else
		height = elUIGetEndY(panel->ui_expander->widget.children);
	ui_ExpanderSetHeight(panel->ui_expander, height);
	ui_WidgetSetHeight(UI_WIDGET(panel->ui_pane), height);
}

/******
* This function removes a widget from a particular panel, optionally updating the height of the panel.
* PARAMS:
*   panel - EMPanel from which the child widget will be taken
*   widget - UIWidget to remove
*   update_height - bool to reset the height of the panel according to its remaining widget contents
******/
void emPanelRemoveChild(EMPanel *panel, UIAnyWidget *widget, bool update_height)
{
	assert(panel->ui_expander && panel->ui_pane);
	ui_ExpanderRemoveChild(panel->ui_expander, widget);
	ui_PaneRemoveChild(panel->ui_pane, widget);
	if (update_height)
		emPanelUpdateHeight(panel);
}

/******
* This function sets the height of a panel.
* PARAMS:
*   panel - EMPanel whose height will be set
*   height - int height in pixels
******/
void emPanelSetHeight(EMPanel *panel, int height)
{
	assert(panel->ui_expander);
	ui_ExpanderSetHeight(panel->ui_expander, height);
	ui_WidgetSetHeight(UI_WIDGET(panel->ui_pane), height);
}

/******
* This function returns the height of a panel.
* PARAMS:
*   panel - EMPanel whose height will be returned
* RETURNS:
*   int height of panel
******/
int emPanelGetHeight(EMPanel *panel)
{
	return panel->ui_expander->openedHeight;
}

/******
* This enables or disables the panels contents without removing the panel.
* PARAMS:
*   panel - EMPanel to modify
*   active - bool whether to enable or disable the panel
******/
void emPanelSetActive(SA_PARAM_NN_VALID EMPanel *panel, bool active)
{
	assert(panel->ui_expander);
	ui_WidgetGroupSetActive(&panel->ui_expander->widget.children, active);
	ui_WidgetGroupSetActive(&panel->ui_pane->widget.children, active);
	panel->active = active;
}


/******
* This opens or closes a panel's expander
* PARAMS:
*   panel - EMPanel to modify
*   opened - bool whether to open or close the expander
******/
void emPanelSetOpened(SA_PARAM_NN_VALID EMPanel *panel, bool opened)
{
	assert(panel->ui_expander);
	ui_ExpanderSetOpened(panel->ui_expander, opened);
}


/********************
* GLOBAL PANEL CALLBACKS
********************/
typedef enum EMMapLayerColumn
{
	EM_MAP_LAYER_NAME,
	EM_MAP_LAYER_TYPE,
} EMMapLayerColumn;

/******
* This function increments the index into the status message memlog, wrapping around as necessary.
* TODO: this logic looks wrong
* PARAMS:
*   oldIdx - int old index
*   inc - int amount to increment
* RETURNS:
*   int new index
******/
static int emMsgLogIncIdx(int oldIdx, int inc)
{	
	int new_idx = oldIdx + inc;

	assert(inc < MEMLOG_NUM_LINES && inc > -MEMLOG_NUM_LINES);
	assert(oldIdx >= 0);

	// return 0 if log is empty
	if (em_data.status.log.tail == 0 && !em_data.status.log.wrapped)
		return 0;
	assert(oldIdx < (int)(em_data.status.log.tail % MEMLOG_NUM_LINES) || em_data.status.log.wrapped);

	if (inc == 0)
		return oldIdx;

	else if (inc > 0)
	{
		// if the index wrapped around the end of the log
		if (oldIdx >= (int)(em_data.status.log.tail % MEMLOG_NUM_LINES))
		{
			if (new_idx >= MEMLOG_NUM_LINES)
				new_idx -= MEMLOG_NUM_LINES;
			else
				return new_idx;
		}

		return MIN(new_idx, (int)((em_data.status.log.tail - 1) % MEMLOG_NUM_LINES));
	}
	else // inc < 0
	{
		if (oldIdx < (int)(em_data.status.log.tail % MEMLOG_NUM_LINES))
		{
			if (new_idx < 0 && em_data.status.log.wrapped)
				new_idx += MEMLOG_NUM_LINES;
			else if (new_idx < 0)
				return 0;
			else
				return new_idx;
		}

		return MAX(new_idx, (int)(em_data.status.log.tail % MEMLOG_NUM_LINES));
	}
}

/******
* This function decrements the index into the status message memlog, wrapping around as necessary.
* TODO: this logic looks wrong
* PARAMS:
*   oldIdx - int old index
*   inc - int amount to decrement
* RETURNS:
*   int new index
******/
static int emMsgLogDecIdx(int oldIdx, int dec)
{
	int new_idx = oldIdx - dec;

	if (oldIdx < 0)
		return oldIdx;

	if (oldIdx >= (int)(em_data.status.log.tail % MEMLOG_NUM_LINES))
		new_idx = MAX(new_idx, (int)(em_data.status.log.tail % MEMLOG_NUM_LINES));
	else if (new_idx < 0)
	{
		if (em_data.status.log.wrapped)
			new_idx = MEMLOG_NUM_LINES-1;
		else
			new_idx = 0;
	}

	return new_idx;
}

/******
* This function deals with updating the text widgets in the message panel with the appropriate "window" of text
* from the stored message log.  This function also handles the red flashing of new messages.
******/
void emMsgLogRefresh(void)
{
	static int old_idx = 0;
	UIStyleFont *panelFont= NULL;
	UIStyleFont *viewportFont = NULL;
	int i;

	// check to see if topmost position being displayed has changed
	if (old_idx != em_data.status.idx)
	{
		int last_idx = -1;

		// set the text for each of the message text widgets
		for (i = 0; i < EM_STATUS_NUMLINES; i++)
		{
			int temp_idx = emMsgLogIncIdx(em_data.status.idx, -i);

			assert(temp_idx >= 0 && temp_idx < ARRAY_SIZE(em_data.status.log.log));
			if (last_idx != temp_idx || i == 0)
				ui_LabelSetText(em_data.status.label[i], em_data.status.log.log[temp_idx].text);
			else
				ui_LabelSetText(em_data.status.label[i], "");
			last_idx = temp_idx;
		}

		old_idx = em_data.status.idx;
		em_data.status.scroll_area->xSize = emGetSidebarScale() * elUIGetEndX(em_data.status.scroll_area->widget.children);
	}

	// flash msgs on main viewport
	if (eaSize(&em_data.status.msgs) > 0)
	{
		viewportFont = ui_StyleFontGet("EditorManager_ViewportAlert");
		assert(viewportFont);

		for (i = 0; i < eaSize(&em_data.status.msgs); i++)
		{
			EMMsgLogFlash *flash = em_data.status.msgs[i];
			CBox cbox = {g_ui_State.viewportMin[0], g_ui_State.viewportMin[1], g_ui_State.viewportMax[0], g_ui_State.viewportMax[1]};
			int rgba = lerpRGBAColors(0x00FF0000, 0x00FF00FF, (float) flash->frames_left / (float) EM_MSGLOG_FLASH_FRAMES);

			viewportFont->uiColor = rgba;
			ui_StyleFontUse(viewportFont, false, 0);
			clipperPushRestrict(&cbox);
			gfxfont_Printf(g_ui_State.viewportMin[0] + 10, g_ui_State.viewportMax[1] - (15 * (eaSize(&em_data.status.msgs) - i)), 10000, 1, 1, 0, "%s", flash->msg);
			clipperPop();
			flash->frames_left--;

			if (flash->frames_left <= 0)
			{
				SAFE_FREE(flash->msg);
				free(flash);
				eaRemove(&em_data.status.msgs, i--);
			}
		}
	}
}

static void emMsgLogUpButtonClicked(UIButton *button, void *unused)
{
	em_data.status.idx = emMsgLogIncIdx(em_data.status.idx, 1);
	emMsgLogRefresh();
}

static void emMsgLogDownButtonClicked(UIButton *button, void *unused)
{
	if ((em_data.status.log.tail % MEMLOG_NUM_LINES) > EM_STATUS_NUMLINES)
		em_data.status.idx = emMsgLogIncIdx(emMsgLogIncIdx(emMsgLogIncIdx(em_data.status.idx, -1), -(EM_STATUS_NUMLINES - 1)), (EM_STATUS_NUMLINES - 1));
	emMsgLogRefresh();
}

static void emMapLayerActivated(UIList *maps, void *unused)
{
	EMMapLayerType *map_layer = ui_ListGetSelectedObject(em_data.map_layers.list);
	if (map_layer)
		emMapLayerSetVisible(map_layer, !map_layer->visible);
}

static void emMapLayerContextCustom(UIMenuItem *item, void *data)
{
	EMMapLayerType *type = item->data.voidPtr;
	EMMapLayerFunc callback = (EMMapLayerFunc) data;

	callback(type, type->user_layer_ptr);
}

static void emMapLayerContextOpenFolder(void *unused, void *unused2)
{
	EMMapLayerType *map_layer = ui_ListGetSelectedObject(em_data.map_layers.list);
	if (map_layer && map_layer->file)
		emuOpenContainingDirectory(map_layer->file->filename);
}

static void emMapLayerContextCheckout(void *unused, void *unused2)
{
	EMMapLayerType *map_layer = ui_ListGetSelectedObject(em_data.map_layers.list);
	if (map_layer && map_layer->file)
		emuCheckoutFile(map_layer->file);
}

static void emMapLayerContextGetLatest(void *unused, void *unused2)
{
	EMMapLayerType *map_layer = ui_ListGetSelectedObject(em_data.map_layers.list);
	if (map_layer && map_layer->file)
		emuGetLatest(map_layer->file);
}

static void emMapLayerContextStat(void *unused, void *unused2)
{
	EMMapLayerType *map_layer = ui_ListGetSelectedObject(em_data.map_layers.list);
	if (map_layer && map_layer->file)
		emuCheckRevisions(map_layer->file);
}

static void emMapLayerContextMenu(UIList *list, void *unused)
{
	EMMapLayerType *layer = ui_ListGetSelectedObject(em_data.map_layers.list);
	if (layer)
	{
		int i;

		if (em_data.map_layers.context_menu)
			ui_WidgetQueueFree(UI_WIDGET(em_data.map_layers.context_menu));

		em_data.map_layers.context_menu = ui_MenuCreate("");
		for (i = 0; i < eaSize(&layer->menu_item_names); i++)
			ui_MenuAppendItem(em_data.map_layers.context_menu, ui_MenuItemCreate(layer->menu_item_names[i], UIMenuCallback, emMapLayerContextCustom, (void*) layer->menu_funcs[i], layer));
		if (i)
			ui_MenuAppendItem(em_data.map_layers.context_menu, ui_MenuItemCreate("", UIMenuSeparator, NULL, NULL, NULL));
		
		ui_MenuAppendItems(em_data.map_layers.context_menu,
			ui_MenuItemCreate("Open containing folder", UIMenuCallback, emMapLayerContextOpenFolder, NULL, NULL),
			ui_MenuItemCreate("Checkout", UIMenuCallback, emMapLayerContextCheckout, NULL, NULL),
			ui_MenuItemCreate("Get Latest", UIMenuCallback, emMapLayerContextGetLatest, NULL, NULL),
			ui_MenuItemCreate("Stat", UIMenuCallback, emMapLayerContextStat, NULL, NULL),
			NULL);

		em_data.map_layers.context_menu->widget.scale = emGetSidebarScale() / g_ui_State.scale;
		ui_MenuPopupAtCursor(em_data.map_layers.context_menu);
	}
}

static void emMapLayerStatusDisplay(struct UIList *uiList, struct UIListColumn *col, UI_MY_ARGS, F32 z, CBox *box, int index, void *data)
{
	EMMapLayerType *map_layer = (*uiList->peaModel)[index];
	AtlasTex *tex;

	if (!map_layer || !map_layer->file)
		return;

	tex = atlasLoadTexture(map_layer->file->read_only?"eui_gimme_readonly":"eui_gimme_ok");

	if (tex)
	{
		int age = (int)((em_data.timestamp - map_layer->file->file_timestamp) / (60 * 60 * 24));
		F32 age_factor = (float)age / age_delta_to_old;
		age_factor = CLAMP(age_factor, 0, 1);
		display_sprite(tex, x + 5 * scale, y + ((tex->height - 2) * 0.5f * scale), z, scale, scale, lerpRGBAColors(age_color_new, age_color_old, age_factor));
	}
}

static int emMapLayerCmpName(const EMMapLayerType **layer1, const EMMapLayerType **layer2)
{
	int ret = strcmpi((*layer1)->layer_name, (*layer2)->layer_name);
	return em_data.map_layers.sort_reverse ? -ret : ret;
}

static int emMapLayerCmpType(const EMMapLayerType **layer1, const EMMapLayerType **layer2)
{
	int ret = strcmpi((*layer1)->layer_type, (*layer2)->layer_type);
	return em_data.map_layers.sort_reverse ? -ret : ret;
}

static void emMapLayerFilterSelected(UIComboBox *cb, UserData unused)
{
	emMapLayerListRefresh();
}

static void emMapLayerColumnText(UIList *list, UIListColumn *col, S32 row, UserData unuse3d, char **output)
{
	EMMapLayerType *layer = ((EMMapLayerType**) *list->peaModel)[row];
	const char *fmt = "  -%s";

	if (layer->file && layer->file->filename && layer->file->filename[0])
	{
		char zmapDir[MAX_PATH];
		char *c;

		sprintf(zmapDir, "%s", zmapGetFilename(NULL));
		c = strrchr(zmapDir, '\\');
		if (!c)
			c = strrchr(zmapDir, '/');
		if (c)
		{
			*(c + 1) = '\0';
			if (strstri(layer->file->filename, zmapDir))
				fmt = "%s";
		}
	}
	estrPrintf(output, FORMAT_OK(fmt), layer->layer_name);
}

static void emMapLayerColumnClicked(UIListColumn *col, UserData data)
{
	EMMapLayerColumn col_enum = (intptr_t) data;
	if (col_enum == (EMMapLayerColumn)em_data.map_layers.sort_column)
		em_data.map_layers.sort_reverse = !em_data.map_layers.sort_reverse;
	else
	{
		em_data.map_layers.sort_column = col_enum;
		em_data.map_layers.sort_reverse = 0;
	}
	emMapLayerListRefresh();

	// store preference
	EditorPrefStoreInt("Editor Manager", "Map Layers", "Sort Column", em_data.map_layers.sort_column);
	EditorPrefStoreInt("Editor Manager", "Map Layers", "Sort Reverse", em_data.map_layers.sort_reverse);
}

static void emMapLayerHideShowClicked(UIButton *button, UserData show)
{
	int i;
	for (i = 0; i < eaSize(&em_data.map_layers.filtered_layers); i++)
		emMapLayerSetVisible(em_data.map_layers.filtered_layers[i], !!show);
}

static void emMapLayerSearchChanged(UITextEntry *entry, UserData unused)
{
	emMapLayerListRefresh();
}

void emMapLayerListRefresh(void)
{
	int i;

	if (em_data.map_layers.context_menu) {
		ui_WidgetQueueFree(UI_WIDGET(em_data.map_layers.context_menu));
		em_data.map_layers.context_menu = NULL;
	}

	eaClear(&em_data.map_layers.filtered_layers);
	for (i = 0; i < eaSize(&em_data.map_layers.layers); i++)
	{
		char *type = em_data.map_layers.filter ? ui_ComboBoxGetSelectedObject(em_data.map_layers.filter) : "All";
		const char *search = em_data.map_layers.search ? ui_TextEntryGetText(em_data.map_layers.search) : "";
		if (type && (strcmpi(type, "All") == 0 || strcmpi(type, em_data.map_layers.layers[i]->layer_type) == 0) && (strstri(em_data.map_layers.layers[i]->layer_name, search)))
			eaPush(&em_data.map_layers.filtered_layers, em_data.map_layers.layers[i]);
	}

	// sort according to last sort order
	switch (em_data.map_layers.sort_column)
	{
		xcase EM_MAP_LAYER_NAME:
			eaQSort(em_data.map_layers.filtered_layers, emMapLayerCmpName);
		xcase EM_MAP_LAYER_TYPE:
			eaQSort(em_data.map_layers.filtered_layers, emMapLayerCmpType);
	}
}

static void emFileManagerDrawText(UIList *list, UIListColumn *column, S32 row, UserData userData, char **output)
{
	char rel_path[MAX_PATH];
	fileRelativePath(((EMFileAssoc**) *list->peaModel)[row]->file->filename, rel_path);
	estrPrintf(output, "%s", rel_path);
}

static void emFileManagerCheckout(UIWidget *unused, UIList *list)
{
	EMFileAssoc *assoc = ui_ListGetSelectedObject(list);
	
	if (assoc)
		emuCheckoutFile(assoc->file);
}

static void emFileManagerUndoCheckout(UIWidget *unused, UIList *list)
{
	EMFileAssoc *assoc = ui_ListGetSelectedObject(list);

	if (assoc)
		emuUndoCheckout(assoc->file);
}

static void emFileManagerUpdate(UIWidget *unused, UIList *list)
{
	EMFileAssoc *assoc = ui_ListGetSelectedObject(list);

	if (assoc)
		emuGetLatest(assoc->file);
}

static void emFileManagerRevert(UIWidget *unused, UIList *list)
{
	EMFileAssoc *assoc = ui_ListGetSelectedObject(list);

	if (assoc)
	{
		emuUndoCheckout(assoc->file);
		emuCheckoutFile(assoc->file);
	}
}

static void emFileManagerStat(UIWidget *unused, UIList *list)
{
	EMFileAssoc *assoc = ui_ListGetSelectedObject(list);

	if (assoc)
		emuCheckRevisions(assoc->file);
}

static void emFileManagerOpenFolder(UIWidget *unused, UIList *list)
{
	EMFileAssoc *assoc = ui_ListGetSelectedObject(list);

	if (assoc && assoc->file)
		emuOpenContainingDirectory(assoc->file->filename);
}

void emSidebarSetScale(F32 scale)
{	
	F32 real_scale = scale / g_ui_State.scale;
	em_data.sidebar.scale = scale;

	em_data.sidebar.tab_group->widget.leftPad = (EM_UI_SIDEBAR_MARGIN_WIDTH + 2) / scale;
	ui_WidgetSetDimensions(UI_WIDGET(em_data.sidebar.hide_button), EM_UI_SIDEBAR_MARGIN_WIDTH / scale, 25 / scale);
	ui_ButtonSetImageStretch(em_data.sidebar.hide_button, true);
	em_data.sidebar.hidden_pane->widget.width = (EM_UI_SIDEBAR_MARGIN_WIDTH + 8) / scale;
	ui_WidgetSetDimensions(UI_WIDGET(em_data.sidebar.show_button), EM_UI_SIDEBAR_MARGIN_WIDTH / scale, 25 / scale);
	ui_ButtonSetImageStretch(em_data.sidebar.show_button, true);

	em_data.sidebar.pane->widget.scale = real_scale;
	em_data.sidebar.hidden_pane->widget.scale = real_scale;
}

void emSidebarApplyCurrentScale(void)
{
	F32 scale = EditorPrefGetFloat("Sidebar", "Option", "Scale", 1.0f);
	emSidebarSetScale(scale);
	EditorPrefStoreFloat("Sidebar", "Option", "Scale", em_data.sidebar.scale);
}


/******
* This function prints a string to the message log.
* PARAMS:
*   fmt - string format
*   ... - params to the format
******/
#undef emStatusPrintf
void emStatusPrintf(const char *fmt, ...)
{
	static int em_log_num = 0;
	va_list va;
	char buffer[512];
	EMMsgLogFlash *msg_flash;

	va_start(va, fmt);
	vsprintf(buffer, fmt, va);
	va_end(va);

	// print string to the beginning of the memlog (i.e. newest messages stored first)
	memlog_printf(&em_data.status.log, "[%i] %s", em_log_num++, buffer);
	em_data.status.idx = ((em_data.status.log.tail - 1) % MEMLOG_NUM_LINES);

	emMsgLogRefresh();

	// add new message to draw list for flashed msgs
	if (eaSize(&em_data.status.msgs) < EM_MSGLOG_FLASH_MAX)
	{
		msg_flash = calloc(1, sizeof(*msg_flash));
		msg_flash->msg = strdup(buffer);
		msg_flash->frames_left = EM_MSGLOG_FLASH_FRAMES;
		eaPush(&em_data.status.msgs, msg_flash);
	}
}

/********************
* MAIN
********************/
/******
* This function initializes the panel system and creates the global panels.
******/
void emPanelsInit(void)
{
	// build layer panel
	if (!em_data.map_layers.panel)
	{
		int y;
		UILabel *label;
		UIButton *button;
		UIListColumn *column;

		em_data.map_layers.panel = emPanelCreate("Map", "Map Layers", 500);

		// load preferences
		em_data.map_layers.sort_column = EditorPrefGetInt("Editor Manager", "Map Layers", "Sort Column", EM_MAP_LAYER_NAME);
		em_data.map_layers.sort_reverse = EditorPrefGetInt("Editor Manager", "Map Layers", "Sort Reverse", 0);

		eaPush(&em_data.map_layers.type_list, "All");
		em_data.map_layers.filter = ui_ComboBoxCreate(0, 0, 1, NULL, &em_data.map_layers.type_list, NULL);
		ui_ComboBoxSetSelectedCallback(em_data.map_layers.filter, emMapLayerFilterSelected, NULL);
		ui_ComboBoxSetSelectedAndCallback(em_data.map_layers.filter, 0);
		em_data.map_layers.filter->widget.widthUnit = UIUnitPercentage;
		emPanelAddChild(em_data.map_layers.panel, em_data.map_layers.filter, false);

		y = terEdPopulateMapPanel(em_data.map_layers.panel);

		button = ui_ButtonCreate("Show All", 0, y, emMapLayerHideShowClicked, (void *)(intptr_t) 1);
		button->widget.width = 0.5;
		button->widget.widthUnit = UIUnitPercentage;
		button->widget.offsetFrom = UIBottomLeft;
		emPanelAddChild(em_data.map_layers.panel, button, false);

		button = ui_ButtonCreate("Hide All", 0, y, emMapLayerHideShowClicked, NULL);
		button->widget.width = 0.5;
		button->widget.widthUnit = UIUnitPercentage;
		button->widget.xPOffset = 0.5;
		button->widget.offsetFrom = UIBottomLeft;
		emPanelAddChild(em_data.map_layers.panel, button, false);

		label = ui_LabelCreate("Find", 0, elUINextY(button) + 5);
		label->widget.offsetFrom = UIBottomLeft;
		emPanelAddChild(em_data.map_layers.panel, label, false);
		em_data.map_layers.search = ui_TextEntryCreate("", 0, label->widget.y);
		em_data.map_layers.search->widget.leftPad = elUINextX(label) + 5;
		em_data.map_layers.search->widget.width = 1;
		em_data.map_layers.search->widget.widthUnit = UIUnitPercentage;
		em_data.map_layers.search->widget.offsetFrom = UIBottomLeft;
		ui_TextEntrySetChangedCallback(em_data.map_layers.search, emMapLayerSearchChanged, NULL);
		emPanelAddChild(em_data.map_layers.panel, em_data.map_layers.search, false);

		em_data.map_layers.list = ui_ListCreate(parse_EMMapLayerType, &em_data.map_layers.filtered_layers, 18);
		em_data.map_layers.list->widget.width = 1;
		em_data.map_layers.list->widget.widthUnit = UIUnitPercentage;
		em_data.map_layers.list->widget.height = 1;
		em_data.map_layers.list->widget.heightUnit = UIUnitPercentage;
		em_data.map_layers.list->widget.topPad = elUINextY(em_data.map_layers.filter) + 5;
		em_data.map_layers.list->widget.bottomPad = elUINextY(em_data.map_layers.search) + 5;
		em_data.map_layers.list->cbActivated = emMapLayerActivated;
		em_data.map_layers.list->widget.contextF = emMapLayerContextMenu;
		ui_ListSetMultiselect(em_data.map_layers.list, true);
		emPanelAddChild(em_data.map_layers.panel, em_data.map_layers.list, false);

		column = ui_ListColumnCreateCallback("", emMapLayerStatusDisplay, NULL);
		column->fWidth = 20;
		ui_ListAppendColumn(em_data.map_layers.list, column);
		column = ui_ListColumnCreateText("Layer", emMapLayerColumnText, NULL);
		ui_ListColumnSetClickedCallback(column, emMapLayerColumnClicked, (void*) (intptr_t) EM_MAP_LAYER_NAME);
		column->fWidth = 200;
		ui_ListAppendColumn(em_data.map_layers.list, column);
		column = ui_ListColumnCreate(UIListPTName, "", (intptr_t)"visible", NULL);
		column->fWidth = 20;
		ui_ListAppendColumn(em_data.map_layers.list, column);

		eaPush(&em_data.sidebar.panels, em_data.map_layers.panel);
	}

	// build file manager panel
	if (!em_data.file_manager.panel)
	{
		int y;

		em_data.file_manager.panel = emPanelCreate("Files", "File List", 200);

		em_data.file_manager.file_list = ui_ListCreate(NULL, NULL, 15);
		ui_WidgetSetDimensionsEx(UI_WIDGET(em_data.file_manager.file_list), 1, 1, UIUnitPercentage, UIUnitPercentage);
		ui_ListAppendColumn(em_data.file_manager.file_list, ui_ListColumnCreate(UIListTextCallback, "Filename", (intptr_t) emFileManagerDrawText, NULL));
		emPanelAddChild(em_data.file_manager.panel, em_data.file_manager.file_list, false);

		em_data.file_manager.revert = ui_ButtonCreate("Revert", 0, 0, emFileManagerRevert, em_data.file_manager.file_list);
		em_data.file_manager.revert->widget.scale = 0.85;
		em_data.file_manager.revert->widget.width = 0.33;
		em_data.file_manager.revert->widget.height /= 0.6;
		em_data.file_manager.revert->widget.widthUnit = UIUnitPercentage;
		em_data.file_manager.revert->widget.offsetFrom = UIBottomLeft;	
		em_data.file_manager.revert->widget.xPOffset = 0;
		emPanelAddChild(em_data.file_manager.panel, em_data.file_manager.revert, false);
		em_data.file_manager.stat = ui_ButtonCreate("Stat", 0, 0, emFileManagerStat, em_data.file_manager.file_list);
		em_data.file_manager.stat->widget.scale = 0.85;
		em_data.file_manager.stat->widget.width = 0.33;
		em_data.file_manager.stat->widget.height /= 0.6;
		em_data.file_manager.stat->widget.widthUnit = UIUnitPercentage;
		em_data.file_manager.stat->widget.offsetFrom = UIBottomLeft;	
		em_data.file_manager.stat->widget.xPOffset = 0.33;
		emPanelAddChild(em_data.file_manager.panel, em_data.file_manager.stat, false);
		em_data.file_manager.open_folder = ui_ButtonCreate("Open Folder", 0, 0, emFileManagerOpenFolder, em_data.file_manager.file_list);
		em_data.file_manager.open_folder->widget.scale = 0.85;
		em_data.file_manager.open_folder->widget.width = 0.33;
		em_data.file_manager.open_folder->widget.height /= 0.6;
		em_data.file_manager.open_folder->widget.widthUnit = UIUnitPercentage;
		em_data.file_manager.open_folder->widget.offsetFrom = UIBottomLeft;	
		em_data.file_manager.open_folder->widget.xPOffset = 0.66;
		emPanelAddChild(em_data.file_manager.panel, em_data.file_manager.open_folder, false);
		y = elUINextY(em_data.file_manager.open_folder);
	
		em_data.file_manager.checkout = ui_ButtonCreate("Checkout", 0, y, emFileManagerCheckout, em_data.file_manager.file_list);
		em_data.file_manager.checkout->widget.scale = 0.85;
		em_data.file_manager.checkout->widget.width = 0.33;
		em_data.file_manager.checkout->widget.height /= 0.6;
		em_data.file_manager.checkout->widget.widthUnit = UIUnitPercentage;
		em_data.file_manager.checkout->widget.offsetFrom = UIBottomLeft;	
		em_data.file_manager.checkout->widget.xPOffset = 0;
		emPanelAddChild(em_data.file_manager.panel, em_data.file_manager.checkout, false);
		em_data.file_manager.undo_checkout = ui_ButtonCreate("Undo Checkout", 0, y, emFileManagerUndoCheckout, em_data.file_manager.file_list);
		em_data.file_manager.undo_checkout->widget.scale = 0.85;
		em_data.file_manager.undo_checkout->widget.width = 0.33;
		em_data.file_manager.undo_checkout->widget.height /= 0.6;
		em_data.file_manager.undo_checkout->widget.widthUnit = UIUnitPercentage;
		em_data.file_manager.undo_checkout->widget.offsetFrom = UIBottomLeft;	
		em_data.file_manager.undo_checkout->widget.xPOffset = 0.33;
		emPanelAddChild(em_data.file_manager.panel, em_data.file_manager.undo_checkout, false);
		em_data.file_manager.update = ui_ButtonCreate("Update", 0, y, emFileManagerUpdate, em_data.file_manager.file_list);
		em_data.file_manager.update->widget.scale = 0.85;
		em_data.file_manager.update->widget.width = 0.33;
		em_data.file_manager.update->widget.height /= 0.6;
		em_data.file_manager.update->widget.widthUnit = UIUnitPercentage;
		em_data.file_manager.update->widget.offsetFrom = UIBottomLeft;	
		em_data.file_manager.update->widget.xPOffset = 0.66;
		emPanelAddChild(em_data.file_manager.panel, em_data.file_manager.update, false);

		em_data.file_manager.file_list->widget.bottomPad = elUINextY(em_data.file_manager.update) + 5;

		emPanelSetOpened(em_data.file_manager.panel, true);

		eaPush(&em_data.sidebar.panels, em_data.file_manager.panel);
	}

	// build status panel
	if (!em_data.status.panel)
	{
		UIButton *button;
		UIScrollArea *area;
		int i;

		em_data.status.panel = emPanelCreate("Status", "Log Messages", 20 + (EM_STATUS_NUMLINES * 15));

		area = ui_ScrollAreaCreate(0, 0, 1, 0, 0, 0, true, false);
		area->widget.widthUnit = UIUnitPercentage;
		em_data.status.scroll_area = area;
		emPanelAddChild(em_data.status.panel, area, false);

		for (i = 0; i < EM_STATUS_NUMLINES; i++)
		{
			em_data.status.label[i] = ui_LabelCreate("", 0, i * 15);
			ui_ScrollAreaAddChild(area, UI_WIDGET(em_data.status.label[i]));
		}

		memlog_init(&em_data.status.log);
		em_data.status.idx = 0;

		button = ui_ButtonCreateImageOnly("eui_arrow_large_up", 5, 0, emMsgLogUpButtonClicked, NULL);
		button->widget.offsetFrom = UITopRight;
		emPanelAddChild(em_data.status.panel, button, false);

		button = ui_ButtonCreateImageOnly("eui_arrow_large_down", 5, ((EM_STATUS_NUMLINES - 1) * 15), emMsgLogDownButtonClicked, NULL);
		button->widget.offsetFrom = UITopRight;
		emPanelAddChild(em_data.status.panel, button, false);
		area->widget.rightPad = elUINextX(button) + 5;
		area->widget.height = area->ySize = i * 15 + 20;

		emPanelSetOpened(em_data.status.panel, true);

		eaPush(&em_data.sidebar.panels, em_data.status.panel);
	}

	// add clipboard panel
	eaPushUnique(&em_data.sidebar.panels, ecbGetPanel());

	emSidebarApplyCurrentScale();
}

void emPanelsGetMapSelectedLayers(ZoneMapLayer ***layer_list)
{
	int i;
	const S32 * const *iRows = ui_ListGetSelectedRows(em_data.map_layers.list);
	for (i = 0; i < eaiSize(iRows); i++)
	{
		EMMapLayerType *filtered_layer = eaGet(&em_data.map_layers.filtered_layers, (*iRows)[i]);
		if (filtered_layer && stricmp(filtered_layer->layer_type, "Encounters"))
		{
			ZoneMapLayer *layer = (ZoneMapLayer*)filtered_layer->user_layer_ptr;
			if (!strcmp(filtered_layer->layer_type, "World"))
				eaPush(layer_list, layer);
		}
	}
}

static EMPanel* emFindPanelFromExpanderGroup(EMEditorDoc *doc, UIExpanderGroup *expander_group)
{
	int i, j;
	for (j = 0; j < 2; j++) {
		EMPanel **panels;
		if (j)
			panels = em_data.sidebar.panels;
		else if (doc)
			panels = doc->em_panels;
		else
			continue;

		for (i = 0; i < eaSize(&panels); i++) {
			if(panels[i]->ui_expander_group == expander_group)
				return panels[i];
		}
	}
	return NULL;
}

/******
* This function refreshes the sidebar's panel contents, showing the panels belonging to
* a specified document in a specified editor.
* PARAMS:
*   doc - EMEditorDoc whose panels will be displayed; if NULL, only the generic Editor Manager
*         panels will be displayed
*   editor - EMEditor for which panels should be displayed; this can be set to NULL to use
*            the doc's editor by default; if doc is NULL, this value can still be set to
*            a particular editor, as editors have information that affects the display of certain
*            Editor Manager panels.
******/
void emPanelsShow(EMEditorDoc *doc, EMEditor *editor)
{
	int i, j;
	StashTable tab_stash = stashTableCreateWithStringKeys(16, StashDeepCopyKeys);
	UITab *found_tab = ui_TabGroupGetActive(em_data.sidebar.tab_group);
	char *active_tab = found_tab ? strdup(ui_TabGetTitle(found_tab)) : NULL;
	UIExpanderGroup *expander_group;
	const char *pref_tab;

	assert(!doc || !editor || doc->editor == editor);
	if (doc && !editor)
		editor = doc->editor;

	// hide old windows
	for (i = 0; i < eaSize(&em_data.sidebar.windows); i++)
		ui_WindowHide(em_data.sidebar.windows[i]);
	eaClear(&em_data.sidebar.windows);

	// clear the old expanders
	for (i = 0; i < eaSize(&em_data.sidebar.expander_groups); i++)
	{
		EMPanel *panel;
		expander_group = em_data.sidebar.expander_groups[i];
		panel = emFindPanelFromExpanderGroup(doc, expander_group);
		if(panel)
			panel->y_scroll = expander_group->widget.sb->ypos;
		for (j = 0; j < eaSize(&expander_group->childrenInOrder); j++)
		{
			UIExpander *expander = (UIExpander*) expander_group->childrenInOrder[j];
			ui_ExpanderGroupRemoveExpander(expander_group, expander);
			j--;
		}

		eaRemove(&em_data.sidebar.expander_groups, i);
		i--;
	}
	for (i = eaSize(&em_data.sidebar.tab_group->eaTabs) - 1; i >= 0; i--)
	{
		UITab *deleted_tab = em_data.sidebar.tab_group->eaTabs[i];
		ui_TabGroupRemoveTab(em_data.sidebar.tab_group, deleted_tab);
		emQueueFunctionCall(ui_TabFree, deleted_tab);
	}

	// re-add panels, first from the document, then from the editor manager
	if (doc)
		ui_ListSetModel(em_data.file_manager.file_list, NULL, &doc->all_files);
	else
		ui_ListSetModel(em_data.file_manager.file_list, NULL, &em_data.file_manager.empty_list);
	for (j = 0; j < 2; j++)
	{
		EMPanel **panels;
		if (j)
			panels = em_data.sidebar.panels;
		else if (doc)
			panels = doc->em_panels;
		else
			continue;

		for (i = 0; i < eaSize(&panels); i++)
		{
			const char* widgetText = ui_WidgetGetText(UI_WIDGET(panels[i]->ui_expander));
			
			// TODO: ask Stephen about panel visibility...
			//if (!panels[i]->active)
			//	continue;
			if (j)
			{
				if (strcmpi(widgetText, "Map Layers") == 0 
					&& (!editor || editor->hide_world))
					continue;
				if (strcmpi(widgetText, "Clipboard") == 0
					&& (!editor || !editor->enable_clipboard))
					continue;
			}

			if (j) {
				panels[i]->windowed = EditorPrefGetInt("Editor Manager", "Sidebar Panel Windowed", widgetText, panels[i]->windowed);
				EditorPrefGetWindowPosition("Editor Manager", "Sidebar Panel Window Position", widgetText, panels[i]->ui_window);
			} else {
				panels[i]->windowed = EditorPrefGetInt(doc->editor->editor_name, "Sidebar Panel Windowed", widgetText, panels[i]->windowed);	
				EditorPrefGetWindowPosition(doc->editor->editor_name, "Sidebar Panel Window Position", widgetText, panels[i]->ui_window);
			}

			if(panels[i]->windowed)
			{
				emPanelMoveWidgets(UI_WIDGET(panels[i]->ui_pane), UI_WIDGET(panels[i]->ui_expander));
				if(!em_data.ui_hidden) {
					ui_WindowShow(panels[i]->ui_window);
					eaPush(&em_data.sidebar.windows, panels[i]->ui_window);
				}
			}
			else
			{
				if (!stashFindPointer(tab_stash, panels[i]->tab_name, &found_tab))
				{
					found_tab = ui_TabCreate(panels[i]->tab_name);
					expander_group = ui_ExpanderGroupCreate();
					ui_WidgetSetClickThrough(UI_WIDGET(expander_group), true);
					ui_WidgetSetDimensionsEx(UI_WIDGET(expander_group), 1, 1, UIUnitPercentage, UIUnitPercentage);
					ui_TabGroupAddTab(em_data.sidebar.tab_group, found_tab);
					stashAddPointer(tab_stash, panels[i]->tab_name, found_tab, false);
					ui_TabAddChild(found_tab, expander_group);
					eaPush(&em_data.sidebar.expander_groups, expander_group);
					expander_group->widget.sb->ypos = panels[i]->y_scroll;
				}
				else
					expander_group = (UIExpanderGroup*) found_tab->eaChildren[0];

				panels[i]->ui_expander_group = expander_group;

				emPanelMoveWidgets(UI_WIDGET(panels[i]->ui_expander), UI_WIDGET(panels[i]->ui_pane));
				ui_ExpanderGroupAddExpander(expander_group, panels[i]->ui_expander);
			}

			// Set expander state based on preferences
			if (j)
				ui_ExpanderSetOpened(panels[i]->ui_expander, EditorPrefGetInt("Editor Manager", "Sidebar Panel Open", widgetText, ui_ExpanderIsOpened(panels[i]->ui_expander)));
			else
				ui_ExpanderSetOpened(panels[i]->ui_expander, EditorPrefGetInt(doc->editor->editor_name, "Sidebar Panel Open", widgetText, ui_ExpanderIsOpened(panels[i]->ui_expander)));
		}
	}

	// Use preference tab if available
	if (editor)
		pref_tab = EditorPrefGetString(editor->editor_name, "Option", "Active Tab", active_tab);
	else 
		pref_tab = active_tab;

	// set previously selected tab to be active
	if (pref_tab && stashFindPointer(tab_stash, pref_tab, &found_tab))
		ui_TabGroupSetActive(em_data.sidebar.tab_group, found_tab);
	else if (eaSize(&em_data.sidebar.tab_group->eaTabs) > 0)
		ui_TabGroupSetActiveIndex(em_data.sidebar.tab_group, 0);

	// cleanup
	SAFE_FREE(active_tab);
	stashTableDestroy(tab_stash);
}

/******
* This function sets focus to a particular panel, opening it as necessary.
* PARAMS:
*   panel - EMPanel where focus will be set
******/
void emPanelFocus(EMPanel *panel)
{
	int i;

	for (i = 0; i < eaSize(&em_data.sidebar.tab_group->eaTabs); i++)
	{
		UITab *tab = em_data.sidebar.tab_group->eaTabs[i];
		if (eaFind(&tab->eaChildren, (UIWidget*)panel->ui_expander->group) != -1)
		{
			ui_TabGroupSetActive(em_data.sidebar.tab_group, tab);
			ui_ExpanderSetOpened(panel->ui_expander, true);
			break;
		}
	}
}

/******
* This function gets the expander for the panel.
* PARAMS:
*   panel - EMPanel where focus will be set
******/
UIWidget *emPanelGetUIContainer(SA_PARAM_NN_VALID EMPanel *panel)
{
	assert(panel->ui_expander);
	if(panel->windowed)
		return UI_WIDGET(panel->ui_pane);
	return UI_WIDGET(panel->ui_expander);
}

/******
* This function gets the expander for the panel.
* PARAMS:
*   panel - EMPanel where focus will be set
******/
UIExpander *emPanelGetExpander(SA_PARAM_NN_VALID EMPanel *panel)
{
	return panel->ui_expander;
}

/******
* This function gets the expander group for the panel.
* PARAMS:
*   panel - EMPanel where focus will be set
******/
UIExpanderGroup *emPanelGetExpanderGroup(SA_PARAM_NN_VALID EMPanel *panel)
{
	return panel->ui_expander_group;
}

UIPane *emPanelGetPane(SA_PARAM_NN_VALID EMPanel *panel)
{
	return panel->ui_pane;
}

/******
* This function sets the expander group for the panel.
* PARAMS:
*   panel - EMPanel where focus will be set
*   expander_group - the value to set from
******/
void emPanelSetExpanderGroup(SA_PARAM_NN_VALID EMPanel *panel, SA_PARAM_NN_VALID UIExpanderGroup *expander_group)
{
	panel->ui_expander_group = expander_group;
}

/******
* This function is called by the UI system when the current tab changes.
******/
void emTabChanged(UITabGroup *tab_group, void *unused)
{
	UITab *active_tab = ui_TabGroupGetActive(tab_group);

	// Store preference for tab
	if (active_tab)
	{
		if (em_data.current_doc)
			EditorPrefStoreString(em_data.current_doc->editor->editor_name, "Option", "Active Tab", ui_TabGetTitle(active_tab));
		else
			EditorPrefStoreString("Editor Manager", "Option", "Active Tab", ui_TabGetTitle(active_tab));
	}
}


/********************
* PANEL COMMANDS
********************/
AUTO_COMMAND ACMD_NAME("EM.TabFocus");
void emTabFocus(int tab_idx)
{
	if (tab_idx - 1 < eaSize(&em_data.sidebar.tab_group->eaTabs))
		ui_TabGroupSetActiveIndex(em_data.sidebar.tab_group, tab_idx - 1);
}

#endif
