/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef __EDITORMANAGERUIMENUS_H__
#define __EDITORMANAGERUIMENUS_H__
GCC_SYSTEM

#ifndef NO_EDITORS

typedef struct EMEditor EMEditor;
typedef struct EMEditorDoc EMEditorDoc;
typedef struct UIMenuItem UIMenuItem;
typedef struct UIMenu UIMenu;

/******
* Editor Manager menus begin with a customized version of a menu item - EMMenuItems.  Creating these allows
* you to do several things: 1) simple reusability of a menu item (as they are stored in an indexed-string-to-UIMenuItem hash
* table); 2) conditional enabling/disabling - EMMenuItems can be specified with a "check function" that is evaluated every
* frame to determine whether the menu item should be enabled or disabled; 3) AUTO_COMMAND keybind display - the keybind matching
* the specified command string is displayed on the right side of the menu item.  If you don't need any of these features,
* you do NOT need to create EMMenuItems.  You can simply create a menu with regular UIMenuItems.
*
* To properly create menus for your editor, you have to do a few things:
*   1) create EMMenuItems with emMenuItemCreate or emMenuItemCreateFromTable; these will become accessible via their indexed
*      text using emMenuItemGet, and the UIMenuItem can be overwritten (if, for example, you want a custom UIMenuCallback type
*      of UIMenuItem) with emMenuItemSet.
*   2) create a UIMenu (either manually or with emMenuCreate, which takes a list of stash lookup strings)
*   3) register the menu with emMenuRegister
*
* Note that by defining EMMenuItems with indexed text that matches any of the base Editor Manager EMMenuItems (like "new", "save",
* "open", "close", etc.), the editor can actually override the menu item being used in the base Editor Manager menus.  Toolbar
* buttons mirroring these menu items will also be redirected to call the custom callbacks/commands. Conversely, specifying base
* Editor Manager indexed text strings in emMenuCreate will use the Editor Manager EMMenuItems if the string is not associated
* with any items on the editor itself.
******/

void emDocumentMenuRebuild(void);
void emMenuInit(void);
void emMenusShow(SA_PARAM_OP_VALID EMEditorDoc *doc, SA_PARAM_OP_VALID EMEditor *editor);
void emMenuItemsRefresh(SA_PARAM_OP_VALID EMEditor *editor);
void emMenuItemsRefreshBinds(SA_PARAM_OP_VALID EMEditor *editor);

#endif // NO_EDITORS

#endif // __EDITORMANAGERUIMENUS_H__