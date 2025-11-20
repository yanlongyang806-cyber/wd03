#ifndef __WORLDEDITORATTRIBUTESPRIVATE_H__
#define __WORLDEDITORATTRIBUTESPRIVATE_H__
GCC_SYSTEM

#include "WorldEditorAttributes.h"
#include "EditorObject.h"
#include "UILib.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

AUTO_ENUM;
typedef enum WleUIPanelMode
{
	WLE_UI_PANEL_MODE_NORMAL = 0,		ENAMES(Normal)
	WLE_UI_PANEL_MODE_PINNED,			ENAMES(Pinned)
	WLE_UI_PANEL_MODE_FORCE_SHOWN,		ENAMES(ForceShown)
} WleUIPanelMode;

// A WleAEPanel is an attribute editor that will integrated with the world editor's UI.  Examples
// include a "placement" panel for positioning and rotation, and an "appearance" panel for tinting
// and texture/material swaps.
AUTO_STRUCT;
typedef struct WleAEPanel
{
	char *name;									// panel's name

	AST_STOP
#ifndef NO_EDITORS
	WleAEPanelCallback reloadFunc;				// function called when reselecting another EditorObject to load into the panel
	WleAEPanelCreateCallback createFunc;		// function to create the UI
	UIActivationFunc addProps;					// function for creating data related to the panel
	UIActivationFunc removeProps;				// function for creating data related to the panel
	EditorObjectType **types;					// earray of object types for which the panel should be displayed
	EMPanel *panel;

	WleAEPropStructData prop_data;				// Used for hiding and showing data related to the panel

	bool allways_force_shown;					// Don't show in any lists and always show the expanders

	WleUIPanelMode currentPanelMode;			// mode determining how to hide/show panels
#endif
} WleAEPanel;
#ifndef NO_EDITORS

typedef struct WleAECopyData
{
	WleAECopyCallback copyFunc;
	WleAECopyPasteFreeCallback copyFreeFunc;
	UserData copyData;

	bool selected;
} WleAECopyData;

typedef struct WleAEPasteData
{
	WleAEPasteCallback pasteFunc;
	UserData pasteData;
	WleAECopyPasteFreeCallback freeFunc;
} WleAEPasteData;

typedef struct WleAEState
{
	EditorObject *selected;						// current editor object selected
	EditorObject *queuedSelected;				// editor object to set active on the tick
	bool queuedClear;							// flag used in conjunction with queuedSelected to queue the clearing of the active object
	bool changed;								// this boolean is set to false when reloading panel UI's, and is true at all other
												// times; panels can check this to prevent UI callbacks from activating, for instance,
												// on a panel reload as opposed to user activity
	bool applying_data;							// this bool is set to true while applying a param's contents to the actual data; this is to
												// prevent refreshing of the attribute editor in mid application

	WleAEPanel **panels;						// holds all registered panels
	WleAEPanel **validPanels;					// holds all panels that can apply to the currently active object but are not owned
	WleAEPanel **ownedPanels;					// holds all panels that owned by the currently active object

	// copy data
	WleAECopyData **selectedAttributes;
} WleAEState;

extern WleAEState wleAEGlobalState;
extern ParseTable parse_WleAEPanel[];
#define TYPE_parse_WleAEPanel WleAEPanel
extern StaticDefineInt WleUIPanelModeEnum[];

#endif // NO_EDITORS

#endif // __WORLDEDITORATTRIBUTESPRIVATE_H__
