#ifndef __EDITOROBJECTMENUS_H__
#define __EDITOROBJECTMENUS_H__
GCC_SYSTEM

#include "EditorObject.h"

typedef struct UIMenu UIMenu;
typedef struct UIMenuItem UIMenuItem;
typedef struct EMEditor EMEditor;
typedef struct StashTableImp *StashTable;
typedef struct EdObjContextMenu EdObjContextMenu;

#ifndef NO_EDITORS

// Definitions
typedef struct EdObjContextMenu
{
	UIMenu *menu;
	UIMenuItem *menuItem;
	UIMenuItem **contextItems;
} EdObjContextMenu;

#endif
AUTO_STRUCT AST_STARTTOK(StartMenu) AST_ENDTOK(EndMenu);
typedef struct EdObjCustomMenu
{
	char **menuItems;			AST(NAME(MenuItem))
	AST_STOP

	UIMenu *menu;
	EdObjContextMenu **contextMenus;
	EMEditor *editor;
} EdObjCustomMenu;
#ifndef NO_EDITORS

#define EDOBJ_MENU_SEPARATOR "separator"

// Setup and Management
SA_RET_NN_VALID EdObjCustomMenu *edObjCustomMenuCreate(EMEditor *editor);

// Custom Menu
void edObjCustomMenuEdit(SA_PARAM_NN_VALID EdObjCustomMenu *menu, const char **newCmds);

// Main
UIMenu *edObjMenuPopupAtCursor(EdObjCustomMenu *menu, EditorObject **context, bool hideDisabledItems);
void edObjMenuHide(EdObjCustomMenu *menu);

extern StashTable edObjAllMenuItems;

#endif // _EDITOROBJECTMENUS_H_

#endif // NO_EDITORS