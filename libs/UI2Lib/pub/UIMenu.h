/***************************************************************************



***************************************************************************/

#ifndef UI_MENU_H
#define UI_MENU_H
GCC_SYSTEM

#include "UICore.h"

typedef struct UISprite UISprite;

//////////////////////////////////////////////////////////////////////////
// A drop-down menu. Menus contains items. Items may in turn contain submenus,
// as well as being activatable.

//////////////////////////////////////////////////////////////////////////
// Menu items can be one of several types. They can call a callback, have
// a check box, run a command, contain a submenu, or be a drawn separator.
typedef enum UIMenuItemType
{
	UIMenuCallback,
	UIMenuCommand,
	UIMenuCheckButton,
	UIMenuCheckRefButton,
	UIMenuSubmenu,
	UIMenuSeparator,
} UIMenuItemType;

typedef struct UIMenuItem
{
	char *text_USEACCESSOR;
	REF_TO(Message) message_USEACCESSOR;
	
	char *rightText;
	UIMenuItemType type;

	// This is called when the type is UIMenuCallback or UIMenuCheckButton.
	// It is also used internally when the type is UIMenuCommand.
	UIActivationFunc clickedF;
	UserData clickedData;

	bool active : 1;

	union {
		bool state;
		bool *statePtr;
		struct UIMenu *menu;	//for sub-menus, which must be explicitly created.
		void *voidPtr;
	} data;

} UIMenuItem;

// If type is UIMenuCommand, pass the command name as the clickedData, and
// NULL as the callback.
SA_RET_NN_VALID UIMenuItem *ui_MenuItemCreate(SA_PARAM_NN_STR const char *text, UIMenuItemType type,
										  UIActivationFunc callback, UserData clickedData, void *data);
SA_RET_NN_VALID UIMenuItem *ui_MenuItemCreateMessage(SA_PARAM_NN_STR const char *text, UIMenuItemType type,
													 UIActivationFunc callback, UserData clickedData, void *data);
SA_RET_NN_VALID UIMenuItem *ui_MenuItemCreate2(SA_PARAM_NN_STR const char *text, SA_PARAM_OP_STR const char* rtext, UIMenuItemType type,
										   UIActivationFunc callback, UserData clickedData, void *data);

#define ui_MenuItemCreateSeparator() ui_MenuItemCreate("", UIMenuSeparator, NULL, NULL, NULL)

void ui_MenuItemFree(SA_PRE_NN_VALID SA_POST_P_FREE UIMenuItem *item);

void ui_MenuItemSetTextString(SA_PARAM_NN_VALID UIMenuItem *item, SA_PARAM_OP_STR const char* text );
void ui_MenuItemSetTextMessage(SA_PARAM_NN_VALID UIMenuItem *item, SA_PARAM_OP_STR const char* text );
const char *ui_MenuItemGetTextMessage(UIMenuItem *item);
const char *ui_MenuItemGetText(SA_PARAM_NN_VALID const UIMenuItem *item);
void ui_MenuItemSetRightText(SA_PARAM_NN_VALID UIMenuItem *item, SA_PARAM_OP_STR const char *rightText);
void ui_MenuItemSetCheckState(SA_PARAM_NN_VALID UIMenuItem *item, bool state);
bool ui_MenuItemGetCheckState(SA_PARAM_NN_VALID UIMenuItem *item);

//////////////////////////////////////////////////////////////////////////
// Menus contains items, and can be parented to a window or a menu bar.
// They can also be "transient", e.g. appearing on a right-click.
typedef enum UIMenuType 
{
	// Normal menus drop down from a text string, and can be parented to
	// any window. The menu manages all its own events.
	UIMenuNormal = 0,

	// Menus in menu bars let the menu bar handle some of their events, such
	// as Z and focus ordering.
	UIMenuInMenuBar,

	// Transient menus are in the top-level window group and have no parent,
	// and themselves when they are parented.
	UIMenuTransient,
} UIMenuType;

typedef struct UIMenu
{
	UIWidget widget;
	UIMenuItem **items;
	F32 itemHeight, itemWidth;

	bool opened;
	bool ignoreNextUp;
	F32 openedX, openedY;
	UIMenuType type;
	struct UIMenu *submenu;

	// This is called just before the menu is displayed.
	UIActivationFunc preopenF;
	UserData preopenData;

} UIMenu;

SA_RET_NN_VALID UIMenu *ui_MenuCreate(SA_PARAM_OP_STR const char *title);
SA_RET_NN_VALID UIMenu *ui_MenuCreateMessage(SA_PARAM_OP_STR const char *title);
SA_RET_NN_VALID UIMenu *ui_MenuCreateWithItems(SA_PARAM_OP_STR const char *title, ...);
void ui_MenuInitialize(SA_PRE_NN_FREE SA_POST_NN_VALID UIMenu *menu, SA_PARAM_OP_STR const char *title, bool titleIsMessage);
void ui_MenuClear(SA_PARAM_NN_VALID UIMenu *menu);
void ui_MenuClearAndFreeItems(SA_PARAM_NN_VALID UIMenu *menu);
void ui_MenuFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UIMenu *menu);

void ui_MenuPopupAtCursor(SA_PARAM_NN_VALID UIMenu *menu);
void ui_MenuPopupAtCursorOrWidgetBox(SA_PARAM_NN_VALID UIMenu *menu);
void ui_MenuPopupAtBox(UIMenu *menu, SA_PARAM_NN_VALID CBox* box);

void ui_MenuAppendItem(SA_PARAM_NN_VALID UIMenu *menu, SA_PARAM_NN_VALID UIMenuItem *item);
void ui_MenuAppendItems(SA_PARAM_NN_VALID UIMenu *menu, ...);
void ui_MenuCalculateWidth(SA_PARAM_NN_VALID UIMenu *menu);

void ui_MenuDrawPopup(SA_PARAM_NN_VALID UIMenu *menu, UI_MY_ARGS, F32 z);
void ui_MenuTickPopup(SA_PARAM_NN_VALID UIMenu *menu, UI_MY_ARGS);

void ui_MenuDraw(SA_PARAM_NN_VALID UIMenu *menu, UI_PARENT_ARGS);
void ui_MenuTick(SA_PARAM_NN_VALID UIMenu *menu, UI_PARENT_ARGS);

void ui_MenuSort(SA_PARAM_NN_VALID UIMenu *menu);

void ui_MenuSetPreopenCallback(SA_PARAM_NN_VALID UIMenu *menu, UIActivationFunc callback, UserData preopenData);


//////////////////////////////////////////////////////////////////////////
// MenuButton is a button that contain a menu
typedef struct UIMenuButton
{
	UIWidget widget;
	UIMenu *pMenu;

	bool pressed : 1;

	REF_TO(UIStyleBorder) hBorder;
	REF_TO(UIStyleBorder) hFocusedBorder;
} UIMenuButton;

SA_RET_NN_VALID UIMenuButton *ui_MenuButtonCreate(F32 x, F32 y);
SA_RET_NN_VALID UIMenuButton *ui_MenuButtonCreateWithItems(F32 x, F32 y, ...);
void ui_MenuButtonInitialize(SA_PRE_NN_FREE SA_POST_NN_VALID UIMenuButton *pMenuButton, F32 x, F32 y);
void ui_MenuButtonFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UIMenuButton *pMenuButton);

void ui_MenuButtonAppendItem(SA_PARAM_NN_VALID UIMenuButton *pMenuButton, SA_PARAM_NN_VALID UIMenuItem *item);
void ui_MenuButtonAppendItems(SA_PARAM_NN_VALID UIMenuButton *pMenuButton, ...);

void ui_MenuButtonDraw(SA_PARAM_NN_VALID UIMenuButton *pMenuButton, UI_PARENT_ARGS);
void ui_MenuButtonTick(SA_PARAM_NN_VALID UIMenuButton *pMenuButton, UI_PARENT_ARGS);

void ui_MenuButtonSetPreopenCallback(SA_PARAM_NN_VALID UIMenuButton *pMenuButton, UIActivationFunc callback, UserData preopenData);


//////////////////////////////////////////////////////////////////////////
// Menu bars contain multiple menus and arrange them horizontally. They can
// be easily parented to the top of the screen or a window.
typedef struct UIMenuBar
{
	UIWidget widget;
	UIMenu **menus;
} UIMenuBar;

SA_RET_NN_VALID UIMenuBar *ui_MenuBarCreate(SA_PARAM_OP_VALID UIMenu **menus);
void ui_MenuBarInitialize(SA_PRE_NN_FREE SA_POST_NN_VALID UIMenuBar *menubar, SA_PARAM_OP_VALID UIMenu **menus);
void ui_MenuBarFree(SA_PRE_NN_VALID SA_POST_P_FREE UIMenuBar *menubar);
void ui_MenuBarResize(SA_PARAM_OP_VALID UIMenuBar *menubar);

void ui_MenuBarRemoveMenu(SA_PARAM_NN_VALID UIMenuBar *menubar, SA_PARAM_NN_VALID UIMenu *menu);
void ui_MenuBarRemoveAllMenus(SA_PARAM_NN_VALID UIMenuBar *menubar);
void ui_MenuBarAppendMenu(SA_PARAM_NN_VALID UIMenuBar *menubar, SA_PARAM_NN_VALID UIMenu *menu);

void ui_MenuBarTick(SA_PARAM_NN_VALID UIMenuBar *menubar, UI_PARENT_ARGS);
void ui_MenuBarDraw(SA_PARAM_NN_VALID UIMenuBar *menubar, UI_PARENT_ARGS);

SA_RET_OP_VALID UIMenuItem *ui_MenuFindItem(SA_PARAM_NN_VALID UIMenu *menu, SA_PARAM_NN_STR const char *item_name); // Can be explicit ("File|Save") or implicit ("Save") name
SA_RET_OP_VALID UIMenuItem *ui_MenuListFindItem(SA_PARAM_NN_VALID UIMenu **menus, SA_PARAM_NN_STR const char *item_name); // Can be explicit ("File|Save") or implicit ("Save") name

void MenuItemCmdParseCallback(UIMenuItem *button, const char *cmd);

#endif
