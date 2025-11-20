/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NO_EDITORS

#include "EditorManagerPrivate.h"
#include "EditorPrefs.h"
#include "MRUList.h"
#include "StringUtil.h"
#include "EditorSearchWindow.h"
#include "EditorPreviewWindow.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

/********************
* STRUCTS
********************/
// the main menu item struct
typedef struct EMMenuItem
{
	EMMenuItemCheckFunc check_func;
	UserData check_func_data;
	UIMenuItem *ui_item;
} EMMenuItem;


/********************
* FORWARD DECLARATIONS
********************/
void showAllWindows(UIMenuItem *item, EMEditorDoc *doc);
void hideAllWindows(UIMenuItem *item, EMEditorDoc *doc);
void emOpenDoc(void *unused, EMEditorDoc *doc);
void emOpenRecent(UIAnyWidget *widget, UserData data_UNUSED);


/********************
* UTIL
********************/
/******
* This function takes a command string and attempts to find a keybind on the keybind stack
* that matches the string, returning a string that represents the keypress that activates the
* specified command.
* PARAMS:
*   editor - EMEditor whose keybind profile will be searched; if this is NULL, search is
*            performed on the rest of the stack
*   command_str - string command to search for
* RETURNS:
*   string corresponding to the textual representation of the keypress for the command
******/
static const char *emMenuItemCommandFindKeybind(EMEditor *editor, const char *command_str)
{
	KeyBindProfileIterator iter;
	KeyBindProfile *profile;
	int i;
	bool found = false;

	if (!command_str)
		return NULL;

	if (editor && editor->keybinds)
	{
		// search editor's keybinds
		for (i = 0; i < eaSize(&editor->keybinds->eaBinds); i++)
		{
			KeyBind *bind = editor->keybinds->eaBinds[i];
			if (strcmpi(bind->pchCommand, command_str) == 0)
				return bind->pchKey;
		}
	}

	// search rest of keybind profile stack
	keybind_NewProfileIterator(&iter);
	while (!found && (profile = keybind_ProfileIteratorNext(&iter)))
	{
		for (i = 0; i < eaSize(&profile->eaBinds); i++)
		{
			KeyBind *bind = profile->eaBinds[i];
			if (bind->pchCommand && strcmpi(bind->pchCommand, command_str) == 0)
				return bind->pchKey;
		}
	}

	return NULL;
}

/******
* This function is used to replace substrings in bind strings with a shorter
* or easier-to-read string.
* PARAMS:
*   bind_str - original bind string to search through; this will be altered
*   buffer_len - size_t length of the bind string buffer
*   search_str - string to look for (case-insensitive)
*   replace_str - string that will replace the first instance of search_str
******/
static void emMenuItemBindStrReplace(char *bind_str, size_t buffer_len, const char *search_str, const char *replace_str)
{
	char *c = strstri(bind_str, search_str);

	if (c)
	{
		size_t search_len = strlen(search_str);
		char appendix[32];

		strcpy(appendix, c + search_len);
		strcpy_s(c, buffer_len - (c - bind_str), replace_str);
		strcat_s(bind_str, buffer_len, appendix);
	}
}

/******
* This function takes a menu item and sets its right text according to the keybind associated with
* its command string.  If the menu item is not of the UIMenuCommand type, then this function does nothing.
* PARAMS:
*   item - EMMenuItem for which to set the keybind string
******/
static void emMenuItemSetKeybind(EMEditor *editor, EMMenuItem *item)
{
	const char *bind_str;
	
	if (item->ui_item->type != UIMenuCommand)
		return;

	bind_str = emMenuItemCommandFindKeybind(editor, (char *) item->ui_item->clickedData);
	if (bind_str)
	{
		// clean up the string
		char final_text[32];
		strcpy(final_text, bind_str);
		string_toupper(final_text);
		emMenuItemBindStrReplace(SAFESTR(final_text), "control", "Ctrl");
		emMenuItemBindStrReplace(SAFESTR(final_text), "alt", "Alt");
		emMenuItemBindStrReplace(SAFESTR(final_text), "shift", "Shift");
		ui_MenuItemSetRightText(item->ui_item, final_text);
	}
}


/********************
* MENU ITEM MANAGEMENT
********************/
/******
* This function creates a menu item and associates it with a particular editor.  The item can later be
* retrieved using the specified indexed_text.  The item, by default, invokes a specified command string
* when clicked.  Command strings should be used as often as possible instead of callbacks, as they
* allow users to create keybinds for those menu items.
* PARAMS:
*   editor - EMEditor to which the menu item will be associated; if NULL, menu item is globallly associated
*            to the asset manager
*   indexed_text - string key by which the menu item will be indexed
*   item_name - string display name which appears as the menu item text
*   check_func - EMMenuItemCheckFunc that is polled every frame to determine whether to enable
*                or disable the menu item; if NULL, item will always be enabled
*   check_func_data - UserData passed to the check_func
*   command_str - string command called when the item is clicked
******/
void emMenuItemCreate(EMEditor *editor, const char *indexed_text, const char *item_name,
					  EMMenuItemCheckFunc check_func, UserData check_func_data, const char *command_str)
{
	EMMenuItem *new_item = calloc(1, sizeof(EMMenuItem));
	char *cmd_str_cpy = strdup(command_str);

	new_item->check_func = check_func;
	new_item->check_func_data = check_func_data;

	// initialize hash tables if necessary
	if (!editor && !em_data.menu_items)
		em_data.menu_items = stashTableCreateWithStringKeys(32, StashDeepCopyKeys);
	else if (editor && !editor->menu_items)
		editor->menu_items = stashTableCreateWithStringKeys(32, StashDeepCopyKeys);

	// create a new menu item
	new_item->ui_item = ui_MenuItemCreate(item_name ? item_name : "[NULL]", UIMenuCommand, NULL, cmd_str_cpy, NULL);
	free(cmd_str_cpy);

	// register menu item with the editor, overwriting any old data with same indexed_text
	emMenuItemDestroy(editor, indexed_text);
	assert(stashAddPointer(editor ? editor->menu_items : em_data.menu_items, indexed_text, new_item, false));
}

/******
* This function creates menu items exactly like emMenuItemCreate, except with the parameters arranged
* in a table of EMMenuItemDef's.
* PARAMS:
*   editor - EMEditor with which to associate the created items; if NULL, menu item is globally associated
*            to the asset manager
*   menu_item_table - EMMenuItemDef array defining each of the menu items to create
*   num_entries - size of the menu_item_table
******/
void emMenuItemCreateFromTable(EMEditor *editor, const EMMenuItemDef *menu_item_table, size_t num_entries)
{
	size_t i;
	for (i = 0; i < num_entries; i++)
		emMenuItemCreate(editor, menu_item_table[i].indexed_text, menu_item_table[i].item_name, menu_item_table[i].check_func, menu_item_table[i].check_func_data, menu_item_table[i].command_str);
}

/******
* This function destroys a menu item.  Be sure the menu item is not used in any menus before doing this.
* PARAMS:
*   editor - EMEditor to which the menu item is currently associated; NULL corresponds to global, asset
*            manager associations
*   indexed_text - string key corresponding to the menu item to destroy
******/
void emMenuItemDestroy(EMEditor *editor, const char *indexed_text)
{
	EMMenuItem *menu_item;
	if (stashRemovePointer(editor ? editor->menu_items : em_data.menu_items, indexed_text, &menu_item))
	{
		ui_MenuItemFree(menu_item->ui_item);
		free(menu_item);
	}
}

/******
* This function returns the UIMenuItem associated to a particular editor, keyed by the specified text.
* PARAMS:
*   editor - EMEditor to which the item is associated; if NULL, searches the global asset manager items
*   indexed_text - string key by which to search for the item
* RETURNS:
*   UIMenuItem corresponding to the indexed menu item; NULL if no match is found
******/
UIMenuItem *emMenuItemGet(EMEditor *editor, const char *indexed_text)
{
	EMMenuItem *menu_item;
	if (stashFindPointer(editor ? editor->menu_items : em_data.menu_items, indexed_text, &menu_item))
		return menu_item->ui_item;
	else
		return NULL;
}

/******
* This function sets the UIMenuItem for a particular indexed menu item.  It frees any previously associated
* UIMenuItem as well.  This is primarily used to override an EMMenuItem's displayed UIMenuItem if, for
* instance, the menu item requires a UIMenuCallback type instead of a command, or if the menu item
* is a check box or a submenu.
* PARAMS:
*   editor - EMEditor to which the EMMenuItem is associated
*   indexed_text - string key to the EMMenuItem
*   override_item - UIMenuItem to override the existing UIMenuItem
******/
void emMenuItemSet(EMEditor *editor, const char *indexed_text, UIMenuItem *override_item)
{
	EMMenuItem *menu_item;
	if (stashFindPointer(editor ? editor->menu_items : em_data.menu_items, indexed_text, &menu_item) && menu_item->ui_item != override_item)
	{
		ui_MenuItemFree(menu_item->ui_item);
		menu_item->ui_item = override_item;
	}
}


/********************
* MENU MANAGEMENT
********************/
/******
* This creates a UIMenu, taking the menu name, the editor for which it will be created, and a list
* of text keys to each of the menu items that should belong to the menu.  UIMenus can be created
* by hand without using this method if you need to have a lot of non-EMMenuItem items in it.  This
* function just simplifies menu creation if the menu items are EMMenuItems.
* PARAMS:
*   editor - EMEditor to which the menu will belong
*   menu_name - string menu name, which is displayed on the UI
*   ... - NULL-terminated list of strings of keys to EMMenuItems that are to be added to the menu,
*         in order of appearance; if the string does not refer to an item registered with the editor,
*         this function will attempt to find a registered item on the asset manager and use that
*         instead
* RETURNS:
*   UIMenu containing the specified items; this is NOT automatically registered with the editor
******/
UIMenu *emMenuCreate(EMEditor *editor, const char *menu_name, ...)
{
	UIMenu *menu = ui_MenuCreate(menu_name);
	va_list va;
	char *indexed_text;

	va_start(va, menu_name);
	while (indexed_text = va_arg(va, char*))
	{
		UIMenuItem *ui_item = NULL;
		if ((ui_item = emMenuItemGet(editor, indexed_text))
			|| (editor && (ui_item = emMenuItemGet(NULL, indexed_text))))
			ui_MenuAppendItem(menu, ui_item);
	}
	va_end(va);

	return menu;
}

/******
* This function appends EMMenuItems to the specified menu, given a list of registered names.
* This function behaves similarly to emMenuCreate, except that it appends items to an existing
* menu.  The function searches for the registered names in the editor first, and then the 
* editor manager if the registered name doesn't exist in the editor.
* PARAMS:
*   editor - EMEditor to which the menu will belong
*   menu - UIMenu to which the registered menu items will be appended
*   ... - NULL-terminated list of strings of keys to EMMenuItems that are to be added to the menu
******/
void emMenuAppendItems(EMEditor *editor, UIMenu *menu, ...)
{
	va_list va;
	char *indexed_text;

	va_start(va, menu);
	while (indexed_text = va_arg(va, char*))
	{
		UIMenuItem *ui_item = NULL;
		if ((ui_item = emMenuItemGet(editor, indexed_text))
			|| (editor && (ui_item = emMenuItemGet(NULL, indexed_text))))
			ui_MenuAppendItem(menu, ui_item);
	}
	va_end(va);
}

/******
* This function registers a UIMenu with an editor.
* PARAMS:
*   editor - EMEditor with which the menu will be registered
*   menu - UIMenu being registered
******/
void emMenuRegister(EMEditor *editor, UIMenu *menu)
{
	if (!editor->menus)
		editor->menus = stashTableCreateWithStringKeys(8, StashDeepCopyKeys);

	stashAddPointer(editor->menus, ui_WidgetGetText(UI_WIDGET(menu)), menu, false);
	eaPush(&editor->ui_menus, menu);
}


/********************
* MAIN
********************/
// check functions
static bool emMenuActiveDocCheck(UserData unused)
{
	return !!emGetActiveEditorDoc();
}

static bool emMenuNewCheck(UserData unused)
{
	return (!!emGetActiveEditorDoc() || (em_data.active_workspace && eaSize(&em_data.active_workspace->editors) > 0));
}

static bool emMenuOpenCheck(UserData unused)
{
	return (!!emGetActiveEditorDoc() || (em_data.active_workspace && eaSize(&em_data.active_workspace->editors) > 0));
}

static bool emMenuNewCurrentCheck(UserData unused)
{
	EMEditorDoc *active_doc = emGetActiveEditorDoc();
	int count = 0;
	int i;
	EMEditor *editor = NULL;

	if (!em_data.active_workspace)
		return false;
	if (active_doc)
		return true;

	// Ignore global editors
	count = eaSize(&em_data.active_workspace->editors);
	for (i = eaSize(&em_data.active_workspace->editors) - 1; i >= 0; --i)
	{
		if (em_data.active_workspace->editors[i]->type == EM_TYPE_GLOBAL)
			--count;
		else
			editor = em_data.active_workspace->editors[i];
	}

	// Enable if exactly one editor
	return (count == 1);
}

static bool emMenuOpenCurrentCheck(UserData unused)
{
	EMEditorDoc *active_doc = emGetActiveEditorDoc();
	int count = 0;
	int i;
	EMEditor *editor = NULL;

	if (!em_data.active_workspace)
		return false;
	if (active_doc)
		return (eaSize(&active_doc->editor->pickers) == 1);

	// Ignore global editors
	count = eaSize(&em_data.active_workspace->editors);
	for (i = eaSize(&em_data.active_workspace->editors) - 1; i >= 0; --i)
	{
		if (em_data.active_workspace->editors[i]->type == EM_TYPE_GLOBAL)
			--count;
		else
			editor = em_data.active_workspace->editors[i];
	}

	// Enable if exactly one editor (or have last editor) and it has only one picker
	return ((count == 1) && editor && (eaSize(&editor->pickers) == 1));
}

static bool emMenuHasWindowsCheck(UserData unused)
{
	EMEditorDoc *doc = emGetActiveEditorDoc();
	return doc && (eaSize(&doc->ui_windows) > 0);
}

static bool emMenuSaveCheck(UserData unused)
{
	EMEditorDoc *doc = emGetActiveEditorDoc();

	if (doc)
	{
		int i;
		for (i = 0; i < eaSize(&doc->sub_docs); i++)
			if (!doc->sub_docs[i]->saved)
				return true;
	}

	return (doc && !doc->saved);
}

static bool emMenuCloseCheck(UserData unused)
{
	EMEditorDoc *doc = emGetActiveEditorDoc();

	return (doc && !doc->editor->primary_editor && !(doc->editor->always_open && eaSize(&doc->editor->open_docs) <= 1));
}

static bool emMenuUndoCheck(UserData unused)
{
	EMEditorDoc *doc = emGetActiveEditorDoc();
	return (doc && doc->edit_undo_stack);
}

static bool emMenuPasteCheck(UserData unused)
{
	return em_data.clipboard.parse_table || em_data.clipboard.custom_type[0];
}

static bool emMenuDocumentationCheck(UserData unused)
{
	return (em_data.current_editor && em_data.current_editor->editor_name[0]);
}

// UI callbacks
static void emFileMenuNew(UIMenuItem *item, EMRegisteredType *type)
{
	if (type)
		emNewDoc(type->type_name, NULL);
}

static void emFileMenuOpenPicker(UIMenuItem *item, EMPicker *picker)
{
	if (picker)
		emPickerShow(picker, NULL, true, NULL, NULL);
}

static void emWindowMenuWinShow(UIMenuItem *item, UIWindow *window)
{
	if (*item->data.statePtr)
		emQueueFunctionCall(emWinShow, window);
}

void emDocumentMenuOpenRecentDoc(UIWidget *widget, UserData unused)
{
	UIMenuItem *item = (UIMenuItem *)widget;
	char buf[1024];
	char *s;

	// determine file name and type
	strcpy(buf, ui_MenuItemGetText(item));
	s = strchr(buf, ':');
	if (!s)
		return;
	*s = '\0';
	s++;

	// open the file
	emOpenFileEx(s, buf);
}

static void emDocumentMenuOpenDoc(UIWidget *widget, EMEditorDoc *doc)
{
	emQueueFunctionCall2(emSetActiveEditorDoc, doc, NULL);
}


static void emToggleSearchWindow(UIMenuItem *pItem, void *pUser)
{
	// Apply the change made by the menu
	ShowSearchWindow(CheckSearchWindow());
}

static void emTogglePreviewWindow(UIMenuItem *pItem, void *pUser)
{
	// Apply the change made by the menu
	ShowPreviewWindow(CheckPreviewWindow());
}

static void emShowZeniWindow(UIMenuItem *ignored, void *ignored2)
{
	emShowZeniPicker();
}


/******
* This is the list of menu items to register with the asset manager.
******/
static EMMenuItemDef em_menu_items[] = {
	{"em_new", NULL, emMenuNewCheck, NULL},
	{"em_newcurrented", NULL, emMenuNewCurrentCheck, NULL, "EM.NewDoc"},
	{"em_open", NULL, emMenuOpenCheck, NULL},
	{"em_opencurrented", NULL, emMenuOpenCurrentCheck, NULL, "EM.OpenDoc"},
	{"em_save", "Save", emMenuSaveCheck, NULL, "EM.Save"},
	{"em_saveas", "Save as...", emMenuActiveDocCheck, NULL, "EM.SaveAs"},
	{"em_close", "Close", emMenuCloseCheck, NULL, "EM.Close"},
	{"em_undo", "Undo", emMenuUndoCheck, NULL, "EM.Undo"},
	{"em_redo", "Redo", emMenuUndoCheck, NULL, "EM.Redo"},
	{"em_cut", "Cut", NULL, NULL, "EM.Cut"},
	{"em_copy", "Copy", NULL, NULL, "EM.Copy"},
	{"em_paste", "Paste", emMenuPasteCheck, NULL, "EM.Paste"},
	{"em_options", "Options...", NULL, NULL, "EM.Options"},
	{"em_winshowall", "Show All", emMenuHasWindowsCheck, NULL, "EM.ShowAllWins"},
	{"em_winhideall", "Hide All", emMenuHasWindowsCheck, NULL, "EM.HideAllWins"},
	{"em_toolbars"},
	{"em_desktopviewscale", "Desktop Scale", NULL, NULL, "EM.SetScale 0"},
	{"em_editorviewscale", "Editor Scale", emMenuActiveDocCheck, NULL, "EM.SetScale 1"},
	{"em_sidebarviewscale", "Sidebar Scale", NULL, NULL, "EM.SetScale 2"},
	{"em_recent"},
	{"em_documentation", "Documentation", emMenuDocumentationCheck, NULL, "EM.Documentation"},
	{"em_separator", "[Separator]"},
	{"em_custombegin"},
	{"em_customend"},
	{"em_switchtogame", "Game mode", NULL, NULL, "EditMode 0"},
	{"em_switchtogameandspawn", "Game mode and spawn", NULL, NULL, "EM.ExitAndSpawn"},
	{"em_togglesearchwindow"},
	{"em_togglepreviewwindow"},
	{"em_zeniwindow"},
};

/******
* This register the asset manager's menu items and creates the various asset manager menus.
******/
void emMenuInit(void)
{
	UIMenuItem *item;

	// register all menu items
	emMenuItemCreateFromTable(NULL, em_menu_items, ARRAY_SIZE_CHECKED(em_menu_items));

	// override non-auto-commanded items
	// TODO: change as many of these to auto-commands as possible
	emMenuItemSet(NULL, "em_new", ui_MenuItemCreate("New", UIMenuCallback, NULL, NULL, NULL));
	emMenuItemSet(NULL, "em_open", ui_MenuItemCreate("Open", UIMenuCallback, NULL, NULL, NULL));
	emMenuItemSet(NULL, "em_recent", ui_MenuItemCreate("Recent Files", UIMenuSubmenu, NULL, NULL, NULL));
	emMenuItemSet(NULL, "em_toolbars", ui_MenuItemCreate("Toolbars", UIMenuSubmenu, NULL, NULL, NULL));
	emMenuItemSet(NULL, "em_separator", ui_MenuItemCreate("[Separator]", UIMenuSeparator, NULL, NULL, NULL));
	emMenuItemSet(NULL, "em_custombegin", ui_MenuItemCreate("", UIMenuSeparator, NULL, NULL, NULL));
	emMenuItemSet(NULL, "em_customend", ui_MenuItemCreate("", UIMenuSeparator, NULL, NULL, NULL));
	emMenuItemSet(NULL, "em_togglesearchwindow", ui_MenuItemCreate("Search Window", UIMenuCheckRefButton, emToggleSearchWindow, NULL, GetSearchWindowStatus()));
	emMenuItemSet(NULL, "em_togglepreviewwindow", ui_MenuItemCreate("Preview Window", UIMenuCheckRefButton, emTogglePreviewWindow, NULL, GetPreviewWindowStatus()));
	emMenuItemSet(NULL, "em_zeniwindow", ui_MenuItemCreate("Object Window", UIMenuCallback, emShowZeniWindow, NULL, NULL));

	// initialize the static, non-overrideable menus
	item = emMenuItemGet(NULL, "em_recent");
	assert(item);
	em_data.title.open_recent_menu = item->data.menu = ui_MenuCreate("Open Recent");
	item = emMenuItemGet(NULL, "em_toolbars");
	assert(item);
	em_data.title.toolbars_menu = item->data.menu = ui_MenuCreate("Open Toolbars");

	emMenusShow(NULL, NULL);
}

/******
* This function searches the specified editor for a menu with a particular name.  If such
* a menu exists, then its items are appended to the specified menu.
* PARAMS:
*   editor - EMEditor to search for an existing menu
*   menu - UIMenu where the existing items will be appended
*   menu_name - string name of the existing menu to find
* RETURNS:
*   UIMenu from the specified editor's menus that matches the specified name
******/
static UIMenu *emMenuAddCustomItems(EMEditor *editor, UIMenu *menu, const char *menu_name)
{
	UIMenu *temp_menu;
	int i;

	if (editor && stashFindPointer(editor->menus, menu_name, &temp_menu))
	{
		UIMenuItem *separator = emMenuItemGet(NULL, "em_custombegin");

		assert(separator);
		ui_MenuAppendItem(menu, separator); 
		for (i = 0; i < eaSize(&temp_menu->items); i++)
			ui_MenuAppendItem(menu, temp_menu->items[i]); 
		return temp_menu;
	}

	return NULL;
}

/******
* This function uses the custom section delimiter separators to remove the custom
* (i.e. editor-specific) items from an existing editor manager menu.  The custom delimiters
* are also removed.
* PARAMS:
*   menu - UIMenu from which to remove editor-customized items
******/
static void emMenuRemoveCustomItems(UIMenu *menu)
{
	UIMenuItem *custom_begin = emMenuItemGet(NULL, "em_custombegin");
	UIMenuItem *custom_end = emMenuItemGet(NULL, "em_customend");
	bool in_custom = false;
	int i;

	for (i = 0; i < eaSize(&menu->items); i++)
	{
		UIMenuItem *temp = NULL;

		if (menu->items[i] == custom_begin)
			in_custom = true;

		if (in_custom)
			temp = eaRemove(&menu->items, i--);

		if (temp == custom_end)
			in_custom = false;
	}
}

static void emToolbarViewStateChanged(UIMenuItem *item, void *unused)
{
	bool state = ui_MenuItemGetCheckState(item);
	if(!em_data.current_editor)
		return;
	EditorPrefStoreInt(em_data.current_editor->editor_name, TOOLBAR_VIS_PREF, ui_MenuItemGetText(item), state);
	emEditorToolbarDisplay(em_data.current_editor);
	emEditorToolbarStack(em_data.current_editor);
}

/******
* This function rebuilds the document menu.  This is most often called when new docs are created or
* docs are loaded.
******/
void emDocumentMenuRebuild(void)
{
	UIMenuItem *item = NULL;
	int i;

	// clear menu contents
	emMenuRemoveCustomItems(em_data.title.doc_menu);
	for (i = eaSize(&em_data.title.doc_menu->items) - 1; i >= 0; i--)
		ui_MenuItemFree(eaRemove(&em_data.title.doc_menu->items, i));
	ui_MenuClear(em_data.title.doc_menu);

	// append the open documents
	for (i = 0; i < eaSize(&em_data.open_docs); i++) {
		char menu_name[256];
		sprintf(menu_name, "%s: %s", em_data.open_docs[i]->doc_type, em_data.open_docs[i]->doc_display_name);
		ui_MenuAppendItem(em_data.title.doc_menu, ui_MenuItemCreate(menu_name, UIMenuCallback, emDocumentMenuOpenDoc, em_data.open_docs[i], NULL));
	}
	emMenuAddCustomItems(em_data.current_editor, em_data.title.doc_menu, "Document");

	// rebuild the recent submenu
	if (em_data.current_editor)
		item = emMenuItemGet(em_data.current_editor, "em_recent");
	if (!item)
	{
		eaDestroyEx(&em_data.title.open_recent_menu->items, ui_MenuItemFree);
		for (i = em_data.open_recent->count - 1; i >= 0; i--)
			ui_MenuAppendItem(em_data.title.open_recent_menu, ui_MenuItemCreate(em_data.open_recent->values[i], UIMenuCallback, emDocumentMenuOpenRecentDoc, NULL, NULL));
	}

	// rebuild the toolbars submenu
	if (em_data.current_editor)
		item = emMenuItemGet(em_data.current_editor, "em_toolbars");
	if (!item)
	{
		eaDestroyEx(&em_data.title.toolbars_menu->items, ui_MenuItemFree);
		if(em_data.current_editor) {
			for ( i=0; i < eaSize(&em_data.current_editor->toolbars); i++ ) {
				const char *name_id = emToolbarGetNameId(em_data.current_editor->toolbars[i]);
				if(name_id) {
					UIMenuItem *menu_item = ui_MenuItemCreate(name_id, UIMenuCheckButton, emToolbarViewStateChanged, NULL, NULL);
					bool state = EditorPrefGetInt(em_data.current_editor->editor_name, TOOLBAR_VIS_PREF, name_id, 1);
					ui_MenuItemSetCheckState(menu_item, state);
					ui_MenuAppendItem(em_data.title.toolbars_menu, menu_item);
				}
			}
		}
	}
}


/****
* This compares menu items so they can be sorted by name
*/
int emCompareMenuItems(const UIMenuItem **left, const UIMenuItem **right)
{
	return stricmp(ui_MenuItemGetText(*left), ui_MenuItemGetText(*right));
}

/******
* This is the primary function that recreates all menus whenever focus changes between documents.
* PARAMS:
*   doc - EMEditorDoc for which the menus should be shown; this affects the Window menu
*   editor - EMEditor whose menus should be displayed; if NULL, this will be determined from the
*            doc's menu; if this and doc are NULL, only asset manager menus will appear
******/
void emMenusShow(EMEditorDoc *doc, EMEditor *editor)
{
	int i, j;
	UIMenu *new_submenu = NULL, *open_submenu = NULL;
	UIMenu *temp_menu;
	UIMenuItem *item;
	char item_text[128];
	UIMenu **editor_menus = NULL;
	EMEditor **editors = NULL;
	bool add_separator;
	EMEditor *menu_editor;
	EMRegisteredType *pDefaultType = NULL;
	EMPicker *pDefaultPicker = NULL;

	assert(!doc || !editor || doc->editor == editor);
	if (doc && !editor)
		editor = doc->editor;
	if (editor)
		eaCopy(&editor_menus, &editor->ui_menus);

	// clear current menus from the bar
	ui_MenuBarRemoveAllMenus(em_data.title.menu_bar);

	// compile list of editors that must be considered
	if (em_data.active_workspace)
		eaCopy(&editors, &em_data.active_workspace->editors);
	if (editor)
		eaPushUnique(&editors, editor);
	for (i = 0; i < eaSize(&editors); i++)
		if (editors[i]->type == EM_TYPE_GLOBAL)
			eaRemove(&editors, i--);

	// Determine effective editor for file menu new/open
	menu_editor = editor;
	if (!menu_editor && (eaSize(&editors) == 1))
		menu_editor = editors[0];

	// FILE MENU
	if (!em_data.title.file_menu)
		em_data.title.file_menu = ui_MenuCreate("File");
	ui_MenuClear(em_data.title.file_menu);

	if (menu_editor)
	{
		EMRegisteredType *type = NULL;

		item = emMenuItemGet(NULL, "em_newcurrented");
		if (menu_editor->default_type && stashFindPointer(em_data.registered_file_types, menu_editor->default_type, &type) && type->display_name)
			sprintf(item_text, "New %s", type->display_name);
		else
			sprintf(item_text, "New %s document", menu_editor->editor_name);
		if (menu_editor->custom_new_func)
			strcat(item_text, "...");
		ui_MenuItemSetTextString(item, item_text);
		emMenuAppendItems(menu_editor, em_data.title.file_menu, "em_newcurrented", NULL);
		
		if(type)
			pDefaultType = type;
	}

	// modify default new submenu
	item = NULL;
	if (menu_editor)
		item = emMenuItemGet(menu_editor, "em_new");
	if (!item)
	{
		item = emMenuItemGet(NULL, "em_new");
		assert(item);

		// cleanup old submenu contents
		if (item->type == UIMenuSubmenu)
			ui_MenuFreeInternal(item->data.menu);

		item->type = UIMenuSubmenu;
		item->clickedF = NULL;
		item->clickedData = NULL;

		new_submenu = ui_MenuCreate("editors");
		for (i = 0; i < eaSize(&editors); i++)
		{
			if (!editors[i]->allow_multiple_docs && eaSize(&editors[i]->open_docs) > 0)
				continue;

			for (j = 0; j < eaSize(&editors[i]->registered_types); j++)
			{
				EMRegisteredType *type = editors[i]->registered_types[j];
				if(type == pDefaultType)
					continue;

				if (type->display_name)
				{
					sprintf(item_text, "%s", type->display_name);
					ui_MenuAppendItem(new_submenu, ui_MenuItemCreate(item_text, UIMenuCallback, emFileMenuNew, type, NULL));
				}
			}
		}
		eaQSort(new_submenu->items, emCompareMenuItems);
		item->data.menu = new_submenu;
	}

	if (new_submenu && eaSize(&new_submenu->items) > 0)
		emMenuAppendItems(menu_editor, em_data.title.file_menu, "em_new", NULL);

	if (menu_editor && eaSize(&menu_editor->pickers) == 1)
	{
		item = emMenuItemGet(NULL, "em_opencurrented");
		sprintf(item_text, "Open %s", menu_editor->pickers[0]->picker_name);
		ui_MenuItemSetTextString(item, item_text);
		emMenuAppendItems(menu_editor, em_data.title.file_menu, "em_opencurrented", NULL);
		pDefaultPicker = menu_editor->pickers[0];
	}
	else if (menu_editor && emMenuItemGet(menu_editor, "em_opencurrented"))
		emMenuAppendItems(menu_editor, em_data.title.file_menu, "em_opencurrented", NULL);

	// modify default open submenu
	item = NULL;
	if (editor)
		item = emMenuItemGet(menu_editor, "em_open");
	if (!item)
	{
		EMPicker **pickers = NULL;

		item = emMenuItemGet(NULL, "em_open");
		assert(item);

		// cleanup old submenu contents
		if (item->type == UIMenuSubmenu)
			ui_MenuFreeInternal(item->data.menu);

		// compile list of valid pickers
		for (i = 0; i < eaSize(&editors); i++)
		{
			EMEditor *curr_editor = editors[i];
			for (j = 0; j < eaSize(&curr_editor->pickers); j++) {
				if(curr_editor->pickers[j] != pDefaultPicker)
					eaPushUnique(&pickers, curr_editor->pickers[j]);
			}
		}

		// create submenu
		item->type = UIMenuSubmenu;
		item->clickedF = NULL;
		item->clickedData = NULL;

		open_submenu = ui_MenuCreate("pickers");
		for (i = 0; i < eaSize(&pickers); i++)
		{
			sprintf(item_text, "%s", pickers[i]->picker_name);
			ui_MenuAppendItem(open_submenu, ui_MenuItemCreate(item_text, UIMenuCallback, emFileMenuOpenPicker, pickers[i], NULL));
		}
		eaQSort(open_submenu->items, emCompareMenuItems);
		item->data.menu = open_submenu;
		eaDestroy(&pickers);
	}

	if (open_submenu && eaSize(&open_submenu->items) > 0)
		emMenuAppendItems(menu_editor, em_data.title.file_menu, "em_open", NULL);

	if (!editor || !editor->disable_single_doc_menus) 
	{
		emMenuAppendItems(editor, em_data.title.file_menu,
			"em_save",
			"em_saveas",
			NULL);
	}

	temp_menu = emMenuAddCustomItems(editor, em_data.title.file_menu, "File");
	if (temp_menu)
	{
		eaFindAndRemove(&editor_menus, temp_menu);
		emMenuAppendItems(NULL, em_data.title.file_menu, "em_customend", NULL);
	}
	emMenuAppendItems(editor, em_data.title.file_menu, "em_recent", "em_separator", NULL);
	emMenuAppendItems(editor, em_data.title.file_menu, "em_switchtogame", NULL);
	if (editor && !editor->hide_world && !editor->force_editor_cam)
		emMenuAppendItems(editor, em_data.title.file_menu, "em_switchtogameandspawn", NULL);
	if (editor && (!editor->disable_single_doc_menus || emMenuItemGet(editor, "em_close")))
		emMenuAppendItems(editor, em_data.title.file_menu, "em_close", NULL);

	eaDestroy(&editors);
	ui_MenuBarAppendMenu(em_data.title.menu_bar, em_data.title.file_menu);

	// EDIT MENU
	if (!em_data.title.edit_menu)
		em_data.title.edit_menu = ui_MenuCreate("Edit");
	ui_MenuClear(em_data.title.edit_menu);
	emMenuAppendItems(NULL, em_data.title.edit_menu, "em_undo", "em_redo", NULL);
	if (editor)
	{
		bool canCut, canCopy, canPaste;

		canCut = !!editor->copy_func || emMenuItemGet(editor, "em_cut");
		canCopy = !!editor->copy_func || emMenuItemGet(editor, "em_copy");
		canPaste = !!editor->paste_func || emMenuItemGet(editor, "em_paste");
		if (canCut || canCopy || canPaste)
			emMenuAppendItems(NULL, em_data.title.edit_menu, "em_separator", NULL);
		if (canCut)
			emMenuAppendItems(editor, em_data.title.edit_menu, "em_cut", NULL);
		if (canCopy)
			emMenuAppendItems(editor, em_data.title.edit_menu, "em_copy", NULL);
		if (canPaste)
			emMenuAppendItems(editor, em_data.title.edit_menu, "em_paste", NULL);
	}
	temp_menu = emMenuAddCustomItems(editor, em_data.title.edit_menu, "Edit");
	if (temp_menu)
		eaFindAndRemove(&editor_menus, temp_menu);
	ui_MenuBarAppendMenu(em_data.title.menu_bar, em_data.title.edit_menu);

	// VIEW MENU
	if (!em_data.title.view_menu)
		em_data.title.view_menu = ui_MenuCreate("View");
	ui_MenuClear(em_data.title.view_menu);
	emMenuAppendItems(editor, em_data.title.view_menu, "em_desktopviewscale", "em_editorviewscale", "em_sidebarviewscale", NULL);
	emMenuAppendItems(editor, em_data.title.view_menu, "em_separator", "em_toolbars", NULL);
	temp_menu = emMenuAddCustomItems(editor, em_data.title.view_menu, "View");
	if (temp_menu)
		eaFindAndRemove(&editor_menus, temp_menu);
	ui_MenuBarAppendMenu(em_data.title.menu_bar, em_data.title.view_menu);

	// TOOLS MENU
	if (!em_data.title.tools_menu)
		em_data.title.tools_menu = ui_MenuCreate("Tools");
	ui_MenuClear(em_data.title.tools_menu);
	emMenuAppendItems(editor, em_data.title.tools_menu, "em_options", NULL);
	temp_menu = emMenuAddCustomItems(editor, em_data.title.tools_menu, "Tools");
	if (temp_menu)
		eaFindAndRemove(&editor_menus, temp_menu);

	// WINDOW MENU
	if (!em_data.title.window_menu)
		em_data.title.window_menu = ui_MenuCreate("Window");
	emMenuRemoveCustomItems(em_data.title.window_menu);
	for (i = eaSize(&em_data.title.window_menu->items) - 1; i >= 0; i--)
	{
		item = em_data.title.window_menu->items[i];
		eaRemove(&em_data.title.window_menu->items, i);
		if (item == emMenuItemGet(NULL, "em_separator"))
			break;
	}
	eaDestroyEx(&em_data.title.window_menu->items, ui_MenuItemFree);

	add_separator = false;
	if (editor)
	{
		if (editor->type == EM_TYPE_SINGLEDOC)
		{
			for (i = 0; i < eaSize(&editor->shared_windows); i++)
			{
				UIWindow *window = editor->shared_windows[i];
				ui_MenuAppendItem(em_data.title.window_menu, ui_MenuItemCreate(ui_WidgetGetText(UI_WIDGET(window)), UIMenuCheckRefButton, emWindowMenuWinShow, window, &window->show));
				add_separator = true;
			}
		}
	}
	if (doc)
	{
		FORALL_DOCWINDOWS(doc, i)
		{
			UIWindow *window = DOCWINDOW(doc, i);
			if (window != doc->primary_ui_window && !DOCWINDOW_IS_PRIVATE(doc, i))
			{
				if (i == 0 && add_separator)
					emMenuAppendItems(NULL, em_data.title.window_menu, "em_separator", NULL);
				else
					add_separator = true;

				ui_MenuAppendItem(em_data.title.window_menu, ui_MenuItemCreate(ui_WidgetGetText(UI_WIDGET(window)), UIMenuCheckRefButton, emWindowMenuWinShow, window, &window->show));
			}
		}
	}
	if (add_separator)
		emMenuAppendItems(NULL, em_data.title.window_menu, "em_separator", NULL);
	emMenuAppendItems(editor, em_data.title.window_menu, "em_winshowall", "em_winhideall", "em_togglesearchwindow", "em_togglepreviewwindow", "em_zeniwindow", NULL);
	temp_menu = emMenuAddCustomItems(editor, em_data.title.window_menu, "Window");
	if (temp_menu)
		eaFindAndRemove(&editor_menus, temp_menu);

	// DOCUMENT MENU
	if (!em_data.title.doc_menu)
		em_data.title.doc_menu = ui_MenuCreate("Document");
	emDocumentMenuRebuild();
	if (editor && stashFindPointer(editor->menus, "Document", &temp_menu))
		eaFindAndRemove(&editor_menus, temp_menu);

	// HELP MENU
	if (!em_data.title.help_menu)
		em_data.title.help_menu = ui_MenuCreate("Help");
	ui_MenuClear(em_data.title.help_menu);
	emMenuAppendItems(editor, em_data.title.help_menu, "em_documentation", NULL);
	temp_menu = emMenuAddCustomItems(editor, em_data.title.help_menu, "Help");
	if (temp_menu)
		eaFindAndRemove(&editor_menus, temp_menu);

	// recreate the menu bar
	if (editor)
	{
		// add editor specific menus to menu bar
		for (i = 0; i < eaSize(&editor_menus); ++i)
			ui_MenuBarAppendMenu(em_data.title.menu_bar, editor_menus[i]);
	}
	ui_MenuBarAppendMenu(em_data.title.menu_bar, em_data.title.tools_menu);
	ui_MenuBarAppendMenu(em_data.title.menu_bar, em_data.title.window_menu);
	ui_MenuBarAppendMenu(em_data.title.menu_bar, em_data.title.doc_menu);
	ui_MenuBarAppendMenu(em_data.title.menu_bar, em_data.title.help_menu);

	eaDestroy(&editor_menus);
}

/******
* This function refreshes the active state on all registered menu items for the specified editor
* and the asset manager's menu items.  This is generally called once per frame for the active editor.
* The function uses each EMMenuItem's check_func to determine whether to make it active or gray it
* out.
* PARAMS:
*   editor - EMEditor whose items should be refreshed
******/
void emMenuItemsRefresh(EMEditor *editor)
{
	StashTableIterator iter;
	StashElement el;

	// poll editor's menu items
	if (editor)
	{
		stashGetIterator(editor->menu_items, &iter);
		while (stashGetNextElement(&iter, &el))
		{
			EMMenuItem *menu_item = stashElementGetPointer(el);
			if (!menu_item->ui_item)
				continue;

			if (!menu_item->check_func || menu_item->check_func(menu_item->check_func_data))
				menu_item->ui_item->active = true;
			else
				menu_item->ui_item->active = false;
		}
	}

	// poll asset manager's menu items
	stashGetIterator(em_data.menu_items, &iter);
	while (stashGetNextElement(&iter, &el))
	{
		EMMenuItem *menu_item = stashElementGetPointer(el);
		if (!menu_item->ui_item)
			continue;

		if (!menu_item->check_func || menu_item->check_func(menu_item->check_func_data))
			menu_item->ui_item->active = true;
		else
			menu_item->ui_item->active = false;
	}
}


/******
* This function refreshes the keybinds displayed on the menu items for the specified editor.  If no
* editor is specified, this function will just refresh the internal EM menu items.
* PARAMS:
*   editor - EMEditor whose keybinds are to be refreshed
******/
void emMenuItemsRefreshBinds(EMEditor *editor)
{
	StashTableIterator iter;
	StashElement el;

	if (editor)
	{
		stashGetIterator(editor->menu_items, &iter);
		while (stashGetNextElement(&iter, &el))
		{
			EMMenuItem *item = stashElementGetPointer(el);
			emMenuItemSetKeybind(editor, item);
		}
	}

	stashGetIterator(em_data.menu_items, &iter);
	while (stashGetNextElement(&iter, &el))
	{
		EMMenuItem *item = stashElementGetPointer(el);
		emMenuItemSetKeybind(NULL, item);
	}
}

#endif
