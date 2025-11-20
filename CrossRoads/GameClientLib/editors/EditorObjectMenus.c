#include "EditorObjectMenus.h"
#include "WorldEditorOptions.h"
#include "EditorPrefs.h"
#ifndef NO_EDITORS

#include "EditorManager.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

/******
* SETUP AND MANAGEMENT
******/
/******
* This function creates a new custom menu, which needs to be passed around in order to do anything
* useful.
* RETURNS:
*   EdObjCustomMenu new menu
******/
EdObjCustomMenu *edObjCustomMenuCreate(EMEditor *editor)
{
	EdObjCustomMenu *menu = calloc(1, sizeof(EdObjCustomMenu));
	menu->editor = editor;
	return menu;
}

/******
* This function clears memory used by a particular context menu.
* PARAMS:
*   contextMenu - EdObjContextMenu to destroy
******/
static void edObjContextMenuDestroy(EdObjContextMenu *contextMenu)
{
	ui_MenuClear(contextMenu->menu);
	ui_MenuItemFree(contextMenu->menuItem);
	eaDestroyEx(&contextMenu->contextItems, ui_MenuItemFree);
}

/******
* UTIL
******/
/******
* This function is called to rebuild the actual UIMenu with the custom and context items in the
* specified EdObjCustomMenu.
* PARAMS:
*   menu - EdObjCustomMenu used to build the UIMenu
*   hideDisabledItems - bool indicating whether inactive custom menu items should be displayed or not
******/
static void edObjCustomMenuBuild(EdObjCustomMenu *menu, EditorObject *context, bool hideDisabledItems)
{	
	int i;
	bool deleteSeparator = true;

	// clear old menu contents
	if (!menu->menu)
		menu->menu = ui_MenuCreate("Custom Menu");
	ui_MenuClear(menu->menu);

	for (i = 0; i < eaSize(&menu->menuItems); i++)
	{
		UIMenuItem *item = emMenuItemGet(menu->editor, menu->menuItems[i]);
		if (!item)
			item = emMenuItemGet(NULL, menu->menuItems[i]);
		if (item && (!hideDisabledItems || item->active))
		{
			// we ensure separators are not at the beginning or end of the menu, and consecutive
			// separators are not shown
			if (strcmpi(menu->menuItems[i], "em_separator") != 0 || !deleteSeparator)
				ui_MenuAppendItem(menu->menu, item);

			if (strcmpi(menu->menuItems[i], "em_separator") != 0)
				deleteSeparator = false;
			else if (!deleteSeparator)
				deleteSeparator = true;
		}
	}

	if (!!EditorPrefGetInt(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "ShowContextMenu", 1))
	{
		if (eaSize(&menu->contextMenus) > 0)
		{
			UIMenuItem *separator = emMenuItemGet(NULL, "em_separator");
			if (separator)
				ui_MenuAppendItem(menu->menu, separator);
			for (i = 0; i < eaSize(&menu->contextMenus); i++)
				ui_MenuAppendItem(menu->menu, menu->contextMenus[i]->menuItem);
		}
	}
}

/********************
* CUSTOM MENU
********************/
/******
* This function resets the specified menu's custom items to be those specified.
* PARAMS:
*   menu - EdObjCustomMenu to customize
*   newCmds - string EArray of commands to put into the menu
******/
void edObjCustomMenuEdit(EdObjCustomMenu *menu, const char **newCmds)
{
	int i;

	// clear out the old items
	eaDestroyEx(&menu->menuItems, StructFreeString);

	// add the specified items
	for (i = 0; i < eaSize(&newCmds); i++)
		eaPush(&menu->menuItems, StructAllocString(newCmds[i]));
}

/******
* MAIN
******/
/******
* This function pops up the specified custom menu, given the specified context, at the user's mouse
* cursor.
* PARAMS:
*   menu - EdObjCustomMenu to pop up
*   context - EditorObject EArray comprising the context of the menu
*   hideDisabledItems - bool indicating whether disabled custom menu items should be hidden or not
******/
UIMenu *edObjMenuPopupAtCursor(EdObjCustomMenu *menu, EditorObject **context, bool hideDisabledItems)
{
	int i, j;

	// clear the menu's context-based contents
	eaDestroyEx(&menu->contextMenus, edObjContextMenuDestroy);

	for (i = 0; i < eaSize(&context); i++)
	{
		if (context[i] && context[i]->type->menuFunc)
		{
			EdObjContextMenu *contextMenu = calloc(1, sizeof(EdObjContextMenu));

			// invoke menu function on each context's type to generate the context menu items
			contextMenu->menu = ui_MenuCreate("Context menu");
			contextMenu->menuItem = ui_MenuItemCreate(context[i]->name, UIMenuSubmenu, NULL, NULL, (void*) contextMenu->menu);
			context[i]->type->menuFunc(context[i], &contextMenu->contextItems);

			for (j = 0; j < eaSize(&contextMenu->contextItems); j++)
				ui_MenuAppendItem(contextMenu->menu, contextMenu->contextItems[j]);

			eaPush(&menu->contextMenus, contextMenu);
		}
	}

	// reassemble the custom menu and display it
	edObjCustomMenuBuild(menu, NULL, hideDisabledItems);
	if (eaSize(&menu->menu->items) > 0)
		ui_MenuPopupAtCursor(menu->menu);

	return menu->menu;
}

/******
* This function hides the specified custom menu.
* PARAMS:
*   menu - EdObjCustomMenu to hide
******/
void edObjMenuHide(EdObjCustomMenu *menu)
{
	if (menu && menu->menu)
		ui_WidgetRemoveFromGroup(UI_WIDGET(menu->menu));
}

#endif

#include "EditorObjectMenus_h_ast.c"