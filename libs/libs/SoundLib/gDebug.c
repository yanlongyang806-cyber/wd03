
/***************************************************************************



***************************************************************************/

#include "gDebug.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "timing.h"

// For ui stuff
#include "UIComboBox.h"
#include "UIExpander.h"
#include "UIScrollbar.h"
#include "UIWidgetTree.h"
#include "UIWindow.h"

#include "Prefs.h"



typedef struct DebuggerObject DebuggerObject;

typedef int DebuggerFlag;
typedef struct DebuggerRoot DebuggerRoot;

AUTO_STRUCT;
typedef struct Debugger {
	const char* name;
	DebuggerRoot **roots; NO_AST

		GDebuggerInitFunc init_func; NO_AST

		// UI Info
		GDebuggerSettingsAreaFunc settings_func; NO_AST
		GDebuggerSettingPaneTickFunc settings_tick_func; NO_AST
		GDebuggerInfoAreaFunc info_func; NO_AST
		GDebuggerTickFunc tick_func; NO_AST
		GDebuggerUserPaneFunc userpane_func; NO_AST

		UIScrollArea *debugger_settings_area; NO_AST
		UIScrollArea *debugger_main_area; NO_AST
		UIScrollArea *debugger_info_area; NO_AST

		UIWidgetTree *main_widget_tree; NO_AST

		U32 inited : 1;  NO_AST
} Debugger;

typedef struct DebuggerTypeImp {
	int index;
	int group_id;

	DebuggerRoot **debugroots;
	DebuggerObject **objects;
	GDebuggerTypeMsgHandler msghandler;
	GDebuggerDrawObjFunc draw_func;
} DebuggerTypeImp;

typedef struct DebuggerFlagImp {
	int count;
	F32 value;

	U32 set_on_me : 1;
} DebuggerFlagImp;

AUTO_STRUCT;
typedef struct DebuggerFlagBaseImp {
	const char *name;
	int index;
	F32 default_value;

	GDebuggerFlagChangedCallback changed_func; AST(INT)

		U32 trickle_up : 1;
	U32 trickle_down : 1;
	U32 value_based : 1;
	U32 min_value : 1;
	U32 max_value : 1;
} DebuggerFlagBaseImp;

struct DebuggerObject {
	DebuggerType type;
	DebuggerObject **parents;
	DebuggerObject **children;
	void *object_data;
	char *name;

	// For keeping unlinked object data
	void *saved_obj;
	ParseTable *saved_obj_pt;
	U32 unlink_time;

	DebuggerFlagImp **flags;
	UIWidgetTreeNode *tree_node;
	UIExpander *expander;

	U32 isRoot : 1;
	U32 isVisible : 1;
};

struct DebuggerRoot {
	DebuggerObject obj;

	GDebuggerDrawRootFunc draw_func;
	DebuggerObject **object_list;

	DebuggerType *types;
};

struct {
	UIWindow *main_window;
	UIComboBox *debugger_combo;
	UIComboBox *settings_combo;
	UIFlagComboBox *display_combo;

	Debugger *selected;
} gDebugUI;

struct {
	Debugger **debuggers;
	DebuggerTypeImp **types;
	DebuggerFlagBaseImp **flags;

	U32 draw_playing_only : 1;
} gDebugState;

StashTable gDebuggerStash;
StashTable gDebuggerTypeStash;
StashTable gDebuggerFlagStash;

char *debug_remove_orig = NULL;
char *debug_add_orig = NULL;



#ifdef STUB_SOUNDLIB



void gDebugOncePerFrame(void) { }
void gDebugDraw(void) { }

void gDebuggerAddRootByHandle(Debugger *dbger, const char* root_name, GDebuggerDrawRootFunc draw, ReferenceHandle *handle) {}
DebuggerType gDebugRegisterDebugObjectType(const char *name, GDebuggerTypeMsgHandler msghandler) { return (DebuggerType)0; }
void gDebuggerSetInitCallback(Debugger *dbger, GDebuggerInitFunc func) { }
Debugger* gDebugRegisterDebugger(const char *name) { return NULL; }

#else

#include "gDebug_c_ast.h"

void gDebuggerObjectResetFlagNotSelf(DebuggerObject* object, DebuggerFlag flag, F32 value, U32 override_all);
void gDebuggerObjectSetFlagNotSelf(DebuggerObject* object, DebuggerFlag	flag, F32 value);
void gDebuggerObjectSetFlagNotSelfHelper(DebuggerObject *object, DebuggerFlag flag, F32 value);
static void gDebuggerObjectRemoveFlagNotSelfHelper(DebuggerObject *object, DebuggerFlag flag, int count);
static void gDebuggerObjectAddFlagNotSelf(DebuggerObject *object, DebuggerFlag flag, int count);
static void gDebuggerObjectRemoveFlagNotSelf(DebuggerObject *object, DebuggerFlag flag, int count);
static void gDebuggerObjectAddFlagNotSelfHelper(DebuggerObject *object, DebuggerFlag flag, int count);

void gDebuggerObjectRemoveChildInternal(DebuggerObject *parent, DebuggerObject *obj);
void gDebuggerObjectAddChildInternal(DebuggerObject *parent, DebuggerObject *obj);
U32 gDebuggerObjectHasFlagInternal(DebuggerObject *object, DebuggerFlag flag);

DebuggerType gDebuggerObjectGetTypeInternal(DebuggerObject *object);
void* gDebuggerObjectGetDataInternal(DebuggerObject *object);
void* gDebuggerObjectGetPersistDataInternal(DebuggerObject *object);

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););



void gDebuggerShowHide(Debugger *dbger, int show)
{
	if(!show)
	{
		if(dbger->debugger_info_area)
			ui_WindowRemoveChild(gDebugUI.main_window, UI_WIDGET(dbger->debugger_info_area));
		ui_WindowRemoveChild(gDebugUI.main_window, UI_WIDGET(dbger->debugger_main_area));
		if(dbger->debugger_settings_area)
			ui_WindowRemoveChild(gDebugUI.main_window, UI_WIDGET(dbger->debugger_settings_area));
	}
	else
	{
		if(dbger->debugger_info_area)
			ui_WindowAddChild(gDebugUI.main_window, UI_WIDGET(dbger->debugger_info_area));
		ui_WindowAddChild(gDebugUI.main_window, UI_WIDGET(dbger->debugger_main_area));
		if(dbger->debugger_settings_area)
			ui_WindowAddChild(gDebugUI.main_window, UI_WIDGET(dbger->debugger_settings_area));
	}
}

void gDebugCustomDisplay(UIWidgetTreeNode *node, UIPane *pane, DebuggerObject *obj)
{
	
}

void gDebuggerObjectDisplay(UIWidgetTreeNode *node, UIPane *pane, void* userData)
{
    DebuggerObject *obj = (DebuggerObject*)userData;
	if(gDebugUI.selected && gDebugUI.selected->userpane_func)
	{
        DebuggerObjectStruct dos = {0};
		ADD_SIMPLE_POINTER_REFERENCE(dos.object, obj);
		gDebugUI.selected->userpane_func(	&dos.object.__handle_INTERNAL, 
											gDebuggerObjectGetTypeInternal(obj),
											gDebuggerObjectGetDataInternal(obj),
											gDebuggerObjectGetPersistDataInternal(obj),
											pane);
		REMOVE_HANDLE(dos.object);
	}
}

bool gDebuggerObjectIsVisible(DebuggerObject *obj, const S32 **values)
{
	if(eaiSize(values)>0)
	{
		if(!obj->flags)
		{
			return 0;
		}
		else
		{
			int i;
			int flagsize = eaSize(&obj->flags);
			for(i=0; i<eaiSize(values); i++)
			{
				int flag_index = (*values)[i];
				if(flag_index<flagsize)
				{
					DebuggerFlagImp *flag = obj->flags[flag_index];
					if(flag && flag->count>0)
					{
						return 1;
					}
				}
			}
			return 0;
		}		
	}

	return 1;
}

bool gDebuggerTreeNodeVisibleCallback(UIWidgetTreeNode *node, void* userData)
{
    DebuggerObject *obj = (DebuggerObject*)userData;
	return obj->isVisible;
}

void gDebuggerObjectFreeTreeNode(UIAnyWidget *widget)
{
    UIWidgetTreeNode *treeWidget = (UIWidgetTreeNode*)widget;
	DebuggerObject *obj = (DebuggerObject*)treeWidget->contents;

	obj->tree_node = NULL;
}

void gDebuggerObjectFill(UIWidgetTreeNode *parent, void* userData)
{
    DebuggerObject *parent_obj = (DebuggerObject*)userData;
	int i;

	for(i=0; i<eaSize(&parent_obj->children); i++)
	{
		DebuggerObject *obj = parent_obj->children[i];

		obj->tree_node = ui_WidgetTreeNodeCreate(	parent_obj->tree_node->tree, 
													obj->name, 
													0, 
													NULL, 
													obj, 
													gDebuggerObjectFill, 
													obj, 
													gDebuggerObjectDisplay, 
													obj, 
													UI_STEP+6);
		ui_WidgetTreeNodeSetIsVisibleCallback(obj->tree_node, gDebuggerTreeNodeVisibleCallback);
		ui_WidgetTreeNodeAddChild(parent, obj->tree_node);
		ui_WidgetTreeNodeSetFreeCallback(obj->tree_node, gDebuggerObjectFreeTreeNode);
		ui_WidgetSetClickThrough(UI_WIDGET(obj->tree_node), 1);
	}
}

void gDebuggerFillRoots(UIWidgetTreeNode *parent, void* userData)
{
    Debugger *dbger = (Debugger*)userData;
	int i;

	for(i=0; i<eaSize(&dbger->roots); i++)
	{
		DebuggerRoot *root = dbger->roots[i];

		root->obj.tree_node = ui_WidgetTreeNodeCreate(	dbger->main_widget_tree, 
														root->obj.name, 
														0, 
														NULL, 
														root, 
														gDebuggerObjectFill, 
														&root->obj, 
														gDebuggerObjectDisplay, 
														&root->obj, 
														UI_STEP+6);
		//ui_WidgetTreeNodeSetCollapseCallback(root->tree_node, gDebuggerRootCollapse, NULL);
		ui_WidgetTreeNodeAddChild(parent, root->obj.tree_node);
		ui_WidgetTreeNodeSetFreeCallback(root->obj.tree_node, gDebuggerObjectFreeTreeNode);
		ui_WidgetSetClickThrough(UI_WIDGET(root->obj.tree_node), 1);
	}
}

void gDebuggerTreeSelect(UIAnyWidget *widget, void* userData)
{
    Debugger *dbger = (Debugger*)userData;
	UIWidgetTreeNode *selected_node = dbger->main_widget_tree->selected;
	if(dbger->info_func && selected_node)
	{
		DebuggerObject *object = selected_node->displayData;
		dbger->info_func(gDebuggerObjectGetTypeInternal(object),
						dbger->debugger_info_area, 
						gDebuggerObjectGetDataInternal(object),
						gDebuggerObjectGetPersistDataInternal(object),
						0);
	}
}

void gDebuggerCreatePanes(Debugger *dbger)
{
	F32 main_y = 30;
	F32 y_offset = 0;
	F32 main_h = 1.0;
	dbger->debugger_main_area = ui_ScrollAreaCreate(0, 0, 0, 0, 10000, 10000, 1, 1);
	if(dbger->info_func)
	{
		dbger->debugger_info_area = ui_ScrollAreaCreate(0, 0, 0, 0, 10000, 10000, 1, 1);
		ui_WidgetSetPositionEx(UI_WIDGET(dbger->debugger_info_area), 0, 0, 0, 0.8, UITop);
		ui_WidgetSetDimensionsEx(UI_WIDGET(dbger->debugger_info_area), 1.0, 0.2, UIUnitPercentage, UIUnitPercentage);

		dbger->info_func(0, dbger->debugger_info_area, NULL, NULL, 1);

		main_h -= .2;
	}
	if(dbger->settings_func)
	{
		dbger->debugger_settings_area = ui_ScrollAreaCreate(0, 0, 0, 0, 10000, 10000, 1, 1);
		ui_WidgetSetDimensionsEx(UI_WIDGET(dbger->debugger_settings_area), 1.0, 0.3, UIUnitPercentage, UIUnitPercentage);
		ui_WidgetSetPaddingEx(UI_WIDGET(dbger->debugger_settings_area), 0, 0, 30, 0);

		dbger->settings_func(dbger->debugger_settings_area);

		y_offset = 0.3;
		main_h -= .3;
	}

	dbger->debugger_main_area = ui_ScrollAreaCreate(0, 0, 0, 0, 10000, 10000, 1, 1);
	ui_WidgetSetPositionEx(UI_WIDGET(dbger->debugger_main_area), 0, main_y, 0, y_offset, UITop);
	ui_WidgetSetDimensionsEx(UI_WIDGET(dbger->debugger_main_area), 1.0, main_h, UIUnitPercentage, UIUnitPercentage);
	
	dbger->main_widget_tree = ui_WidgetTreeCreate(0, 0, 0, 0);
	ui_WidgetSetClickThrough(UI_WIDGET(dbger->main_widget_tree), 1);
	ui_WidgetSetClickThrough(UI_WIDGET(dbger->main_widget_tree->root), 1);
	ui_WidgetSetDimensionsEx(UI_WIDGET(dbger->main_widget_tree), 1.0, 1.0, UIUnitPercentage, UIUnitPercentage);
	ui_WidgetTreeSetSelectedCallback(dbger->main_widget_tree, gDebuggerTreeSelect, dbger);
	ui_WidgetTreeNodeSetFillCallback(dbger->main_widget_tree->root, gDebuggerFillRoots, dbger);
	
	ui_WidgetTreeNodeExpand(dbger->main_widget_tree->root);
	ui_ScrollAreaAddChild(dbger->debugger_main_area, UI_WIDGET(dbger->main_widget_tree));

	gDebuggerShowHide(dbger, 1);
}

void gDebugUIDebuggerSelected(UIAnyWidget *widget, UserData data)
{
	int index = ui_ComboBoxGetSelected(gDebugUI.debugger_combo);
	Debugger *selected = gDebugState.debuggers[index];

	if(selected!=gDebugUI.selected)
	{
		// Need to change
		if(!selected->inited)
		{
			if(selected->init_func)
			{
				selected->init_func();
			}
			selected->inited = 1;
		}
		if(!selected->debugger_main_area)
		{
			// Need to create
			gDebuggerCreatePanes(selected);
		}

		if(gDebugUI.selected)
			gDebuggerShowHide(gDebugUI.selected, 0);
		gDebuggerShowHide(selected, 1);

		gDebugUI.selected = selected;
	}
}

static void gDebugUpdateVisibilityHelper(DebuggerObject *obj, const S32 **values)
{
	int i;

	obj->isVisible = gDebuggerObjectIsVisible(obj, values);

	if(obj->isVisible)
	{
		for(i=0; i<eaSize(&obj->children); i++)
		{
			DebuggerObject *child = obj->children[i];

			gDebugUpdateVisibilityHelper(child, values);
		}
	}
}

static void gDebugUpdateVisibility(Debugger *dbger)
{
	int i;
	const S32 *values = ui_ComboBoxGetSelecteds(gDebugUI.display_combo);

	for(i=0; i<eaSize(&dbger->roots); i++)
	{
		int j;
		DebuggerRoot *root = dbger->roots[i];

		for(j=0; j<eaSize(&root->obj.children); j++)
		{
			DebuggerObject *child = root->obj.children[j];

			gDebugUpdateVisibilityHelper(child, &values);
		}
	}
}

void gDebugOncePerFrame(void)
{
	if(gDebugUI.selected)
	{
		if(gDebugUI.selected->tick_func)
		{
			gDebugUI.selected->tick_func();
		}

		if(gDebugUI.selected->settings_tick_func)
		{
			gDebugUI.selected->settings_tick_func();
		}

		// Update the user info pane
		if(gDebugUI.selected->main_widget_tree->selected && gDebugUI.selected->info_func)
		{
			DebuggerObject *obj = gDebugUI.selected->main_widget_tree->selected->contents;
			gDebugUI.selected->info_func(	gDebuggerObjectGetTypeInternal(obj),
											gDebugUI.selected->debugger_info_area, 
											gDebuggerObjectGetDataInternal(obj),
											gDebuggerObjectGetPersistDataInternal(obj),
											0);
		}

		// Update the trees visibility
		gDebugUpdateVisibility(gDebugUI.selected);
	}
}

bool gDebugUICloseCallback(UIAnyWidget *widget, UserData unused)
{
	int i;

	for(i=0; i<eaSize(&gDebugState.debuggers); i++)
	{
		Debugger *dbger = gDebugState.debuggers[i];

		dbger->debugger_info_area = NULL;
		dbger->debugger_main_area = NULL;
		dbger->debugger_settings_area = NULL;
	}

	GamePrefStoreFloat("GDebug.x", ui_WidgetGetX((UIWidget*)widget));
	GamePrefStoreFloat("GDebug.y", ui_WidgetGetY((UIWidget*)widget));
	GamePrefStoreFloat("GDebug.w", ui_WidgetGetWidth((UIWidget*)widget));
	GamePrefStoreFloat("GDebug.h", ui_WidgetGetHeight((UIWidget*)widget));
	GamePrefStoreString("GDebug.selected", gDebugUI.selected->name);

	ZeroStruct(&gDebugUI);

	return 1;
}

void gDebugUIToggle(void)
{
	if(gDebugUI.main_window)
	{
		// Destroy it all
		ui_WindowClose(gDebugUI.main_window);
	}
	else
	{
		F32 x, y, w, h;
		const char* selected;

		x = GamePrefGetFloat("GDebug.x", 5);
		y = GamePrefGetFloat("GDebug.y", 5);
		w = GamePrefGetFloat("GDebug.w", 400);
		h = GamePrefGetFloat("GDebug.h", 600);
		selected = GamePrefGetString("GDebug.selected", "");
		gDebugUI.main_window = ui_WindowCreate("Debugging", x, y, w, h);
		gDebugUI.debugger_combo = ui_ComboBoxCreate(UI_STEP, UI_STEP, 100, parse_Debugger, &gDebugState.debuggers, "name");
		ui_ComboBoxSetSelectedCallback(gDebugUI.debugger_combo, gDebugUIDebuggerSelected, NULL);
		gDebugUI.display_combo = ui_ComboBoxCreate(0, 0, 0, parse_DebuggerFlagBaseImp, &gDebugState.flags, "name");
		ui_ComboBoxSetMultiSelect(gDebugUI.display_combo, 1);
		ui_WidgetSetDimensionsEx(UI_WIDGET(gDebugUI.display_combo), 100, 20, UIUnitFixed, UIUnitFixed);
		ui_WidgetSetPosition(UI_WIDGET(gDebugUI.display_combo), ui_WidgetGetNextX(UI_WIDGET(gDebugUI.debugger_combo))+UI_STEP, UI_STEP);

		ui_WindowSetCloseCallback(gDebugUI.main_window, gDebugUICloseCallback, NULL);

		ui_WindowAddChild(gDebugUI.main_window, gDebugUI.debugger_combo);
		ui_WindowAddChild(gDebugUI.main_window, gDebugUI.display_combo);
		ui_WindowShow(gDebugUI.main_window);

		if(selected && selected[0])
			ui_ComboBoxSetSelectedsAsStringAndCallback(gDebugUI.debugger_combo, selected);
	}
}

void gDebugUpdateUI(void)
{

}

static void gDebugDrawRootHelper(DebuggerRoot *root, DebuggerObject *obj)
{
	if(obj->isVisible)
	{
		int i;

		root->draw_func(obj->type, obj->object_data, obj->saved_obj);
		
		for(i=0; i<eaSize(&obj->children); i++)
		{
			gDebugDrawRootHelper(root, obj->children[i]);
		}
	}
}

static void gDebugDrawRoot(DebuggerRoot *root)
{
	int i;

	for(i=0; i<eaSize(&root->obj.children); i++)
	{
		gDebugDrawRootHelper(root, root->obj.children[i]);
	}
}

void gDebugDraw(void)
{
	if(gDebugUI.selected)
	{
		Debugger *dbger = gDebugUI.selected;
		int i;

		for(i=0; i<eaSize(&dbger->roots); i++)
		{
			DebuggerRoot *root = dbger->roots[i];

			gDebugDrawRoot(root);
		}

		if(dbger->main_widget_tree->selected)
		{
			DebuggerObject *obj = (DebuggerObject*)dbger->main_widget_tree->selected->contents;
			DebuggerTypeImp *type = gDebugState.types[obj->type];

			if(type->draw_func)
			{
				type->draw_func(obj->type, obj->object_data, obj->saved_obj);
			}
		}
	}
}

Debugger* gDebugRegisterDebugger(const char *name)
{
	int index = 0;
	Debugger *dbger = NULL;
	if(!gDebuggerStash)
	{
		gDebuggerStash = stashTableCreateWithStringKeys(10, StashDeepCopyKeys_NeverRelease);
	}

	if(stashFindInt(gDebuggerStash, name, &index))
	{
		devassert(index>=0 && index<eaSize(&gDebugState.debuggers));
		return gDebugState.debuggers[index];
	}

	dbger = callocStruct(Debugger);
	dbger->name = StructAllocString(name);

	eaPush(&gDebugState.debuggers, dbger);
	index = eaSize(&gDebugState.debuggers)-1;

	stashAddInt(gDebuggerStash, name, index, 1);
	return dbger;
}

DebuggerType gDebugRegisterDebugObjectType(const char *name, GDebuggerTypeMsgHandler msghandler)
{
	DebuggerTypeImp *type = NULL;

	if(!gDebuggerTypeStash)
	{
		gDebuggerTypeStash = stashTableCreateWithStringKeys(10, StashDeepCopyKeys_NeverRelease);
	}

	if(!stashFindPointer(gDebuggerTypeStash, name, &type))
	{
		type = callocStruct(DebuggerTypeImp);
		eaPush(&gDebugState.types, type);
		type->index = eaSize(&gDebugState.types)-1;
		type->msghandler = msghandler;

		stashAddPointer(gDebuggerTypeStash, name, type, 1);
	}

	return type->index;
}

DebuggerFlagBaseImp* gDebugFindFlag(const char *name)
{
	DebuggerFlagBaseImp *flag = NULL;

	if(!gDebuggerFlagStash)
	{
		gDebuggerFlagStash = stashTableCreateWithStringKeys(10, StashDeepCopyKeys_NeverRelease);
	}

	if(!stashFindPointer(gDebuggerFlagStash, name, &flag))
	{
		flag = callocStruct(DebuggerFlagBaseImp);
		eaPush(&gDebugState.flags, flag);
		flag->name = strdup(name);
		flag->index = eaSize(&gDebugState.flags)-1;

		stashAddPointer(gDebuggerFlagStash, name, flag, 1);
	}

	return flag;
}

DebuggerFlag gDebugRegisterDebugObjectFlag(const char *name, U32 trickle_up, U32 trickle_down)
{
	DebuggerFlagBaseImp *flag = gDebugFindFlag(name);
	
	flag->trickle_up = !!trickle_up;
	flag->trickle_down = !!trickle_down;

	return flag->index;
}

DebuggerFlag gDebugRegisterDebugObjectValueFlag(const char *name, U32 trickle_up, U32 trickle_down, F32 default_value, U32 min_based, U32 max_based)
{
	DebuggerFlagBaseImp *flag = gDebugFindFlag(name);

	flag->trickle_up = !!trickle_up;
	flag->trickle_down = !!trickle_down;
	flag->value_based = 1;
	flag->default_value = default_value;
	flag->min_value = min_based;
	flag->max_value = max_based;

	return flag->index;
}

void gDebuggerFlagSetChangedCallback(DebuggerFlag flag, GDebuggerFlagChangedCallback func)
{
	DebuggerFlagBaseImp *base_imp = gDebugState.flags[flag];

	base_imp->changed_func = func;
}

void gDebuggerSetInitCallback(Debugger *dbger, GDebuggerInitFunc func)
{
	dbger->init_func = func;
}

void gDebuggerSetTickCallback(Debugger *dbger, GDebuggerTickFunc func)
{
	dbger->tick_func = func;
}

void gDebuggerSetPaneCallbacks(Debugger *dbger, GDebuggerSettingsAreaFunc settings_func, GDebuggerInfoAreaFunc info_func)
{
	dbger->info_func = info_func;
	dbger->settings_func = settings_func;
}

void gDebuggerSetSettingsTickCallback(Debugger *dbger, GDebuggerSettingPaneTickFunc settings_tick_func)
{
	dbger->settings_tick_func = settings_tick_func;
}

void gDebuggerSetUserPaneCallback(Debugger *dbger, GDebuggerUserPaneFunc userpane_func)
{
	dbger->userpane_func = userpane_func;
}

void gDebuggerAddRootByHandle(Debugger *dbger, const char* root_name, GDebuggerDrawRootFunc draw, ReferenceHandle *handle)
{
	DebuggerRoot *root = NULL;

	root = callocStruct(DebuggerRoot);
	if(!root)
	{
		return;
	}

	root->obj.name = StructAllocString(root_name);
	root->obj.isRoot = 1;
	root->draw_func = draw;
	eaPush(&dbger->roots, root);
	
	RefSystem_SetHandleFromRefData("nullDictionary", root, handle);
}

void gDebuggerRootAddTypeByHandle(ReferenceHandle *hobject, DebuggerType type)
{
	DebuggerTypeImp *t = gDebugState.types[type];
	DebuggerRoot *root;
	DebuggerObject *object;

	if(!RefSystem_IsHandleActive(hobject))
		return;
	object = RefSystem_ReferentFromHandle(hobject);
	root = (DebuggerRoot*)object;

	if(!object)
		return;

	devassert(type>=0);
	devassertmsg(object->isRoot, "Adding type to non-root debug object.");

	eaiPushUnique(&root->types, type);
	eaPushUnique(&t->debugroots, root);
}

void gDebuggerRootSetDrawFuncByHandle(ReferenceHandle *hobject, GDebuggerDrawRootFunc draw_func)
{
	DebuggerRoot *root;
	DebuggerObject *object;

	if(!RefSystem_IsHandleActive(hobject))
	{
		return;
	}

	object = RefSystem_ReferentFromHandle(hobject);
	root = (DebuggerRoot*)object;

	if(!object)
		return;

	devassertmsg(object->isRoot, "Adding draw func to non-root debug object.");

	root->draw_func = draw_func;
}

void gDebuggerTypeSetDrawFunc(DebuggerType type, GDebuggerDrawObjFunc draw)
{
	DebuggerTypeImp *typeimp = gDebugState.types[type];
	devassert(type>=0);

	typeimp->draw_func = draw;
}

void gDebuggerTypeCreateGroup(DebuggerType type)
{
	static int group_count = 1;
	DebuggerTypeImp *typeimp = gDebugState.types[type];
	devassert(type>=0);

	if(!typeimp->group_id)
	{
		typeimp->group_id = group_count;

		group_count++;
	}
}

void gDebuggerTypeAddToGroup(DebuggerType type1, DebuggerType type2)
{
	DebuggerTypeImp *typeimp1 = gDebugState.types[type1];
	DebuggerTypeImp *typeimp2 = gDebugState.types[type2];

	devassert(type1>=0);
	devassert(type2>=0);

	typeimp2->group_id = typeimp1->group_id;
}

void gDebuggerTypeGetName(DebuggerTypeImp *type, char *strOut, int len, void *obj)
{
	DebuggerObjectMsg msg = {0};

	msg.type = DOMSG_GETNAME;
	msg.obj_data = obj;
	msg.getName.out.len = len;
	msg.getName.out.name = strOut;

	type->msghandler(&msg);
}

U32 gDebuggerObjectIsByHandle(ReferenceHandle *object)
{
	return RefSystem_IsHandleActive(object) && RefSystem_ReferentFromHandle(object);
}

void gDebuggerObjectAddObjectInternal(DebuggerObject *parent, DebuggerType type, void *obj, const char *name, ReferenceHandle *handle)
{
	int i;
	DebuggerObject *dobj = NULL;
	DebuggerTypeImp *typeimp = gDebugState.types[type];
	devassert(type>=0 && type<eaSize(&gDebugState.types));

	if(RefSystem_IsHandleActive(handle))
	{
		// Already a child of something
		DebuggerObject *object = RefSystem_ReferentFromHandle(handle);
		
		devassert(object && stricmp(object->name, name)==0 && object->object_data==obj);
		gDebuggerObjectAddChildInternal(parent, RefSystem_ReferentFromHandle(handle));
	}

	for(i=0; i<eaSize(&parent->children); i++)
	{
		DebuggerObject *child = parent->children[i];

		if(!stricmp(child->name, name) && child->object_data==obj)
		{
			RefSystem_SetHandleFromRefData("nullDictionary", child, handle);
			return;
		}
	}

	dobj = callocStruct(DebuggerObject);
	dobj->type = type;
	dobj->object_data = obj;
	dobj->name = StructAllocString(name);

	eaPush(&typeimp->objects, dobj);

	if(parent->isRoot)
	{
		DebuggerRoot *root = (DebuggerRoot*)parent;

		eaPush(&root->object_list, dobj);
	}

	gDebuggerObjectAddChildInternal(parent, dobj);

	RefSystem_SetHandleFromRefData("nullDictionary", dobj, handle);
}

void gDebuggerObjectAddObjectByHandle(ReferenceHandle *hparent, DebuggerType type, void *obj, ReferenceHandle *handle)
{
	char name[MAX_PATH];
	DebuggerTypeImp *typeimp;
	DebuggerObject *parent;
	devassert(type>=0);

	if(!RefSystem_IsHandleActive(hparent))
	{
		return;
	}
	parent = RefSystem_ReferentFromHandle(hparent);
	devassert(obj);

	typeimp = gDebugState.types[type];
	gDebuggerTypeGetName(typeimp, SAFESTR(name), obj);

	gDebuggerObjectAddObjectInternal(parent, type, obj, name, handle);
}

void gDebuggerObjectAddVirtualObjectByHandle(ReferenceHandle *hparent, DebuggerType type, const char *name, ReferenceHandle *handle)
{
	DebuggerObject *parent;

	if(!RefSystem_IsHandleActive(hparent))
	{
		return;
	}
	parent = RefSystem_ReferentFromHandle(hparent);

	gDebuggerObjectAddObjectInternal(parent, type, NULL, name, handle);
}

void gDebuggerObjectRemoveInternal(DebuggerObject *object)
{
	int i;
	if(object->parents)
	{
		for(i=eaSize(&object->parents)-1; i>=0; i--)
		{
			DebuggerObject *parent = object->parents[i];

			gDebuggerObjectRemoveChildInternal(parent, object);
		}
	}

	// Now destroy self and children
	RefSystem_RemoveReferent(object, 1);
	eaDestroyEx(&object->children, gDebuggerObjectRemoveInternal);
	eaDestroy(&object->parents);
	eaDestroyEx(&object->flags, NULL);
	if(object->saved_obj)
		StructDestroyVoid(object->saved_obj_pt, object->saved_obj);
	free(object);
}

void gDebuggerObjectRemoveByHandle(ReferenceHandle *hobject)
{
	DebuggerObject *object;

	if(!RefSystem_IsHandleActive(hobject))
	{
		return;
	}
	object = RefSystem_ReferentFromHandle(hobject);

	gDebuggerObjectRemoveInternal(object);
}

void gDebuggerObjectRemoveChildrenByHandle(ReferenceHandle *hobject)
{
	int i;
	DebuggerObject *object;

	if(!RefSystem_IsHandleActive(hobject))
	{
		return;
	}
	object = RefSystem_ReferentFromHandle(hobject);

	if(!object)
		return;

	for(i=eaSize(&object->children)-1; i>=0; i--)
	{
		DebuggerObject *child = object->children[i];

		gDebuggerObjectRemoveInternal(child);
	}
}

void gDebuggerObjectAddChildInternal(DebuggerObject *parent, DebuggerObject *obj)
{
	int i;
	devassert(parent!=obj);

	if(eaFind(&parent->children, obj)!=-1)
	{
		return;
	}

	if(eaFind(&parent->parents, obj)!=-1)
	{
		gDebuggerObjectRemoveChildInternal(obj, parent);
	}

	for(i=eaSize(&obj->parents)-1; i>=0; i--)
	{
		DebuggerObject *other_parent = obj->parents[i];
		DebuggerTypeImp *parent_type = gDebugState.types[parent->type];
		DebuggerTypeImp *other_type = gDebugState.types[other_parent->type];

		devassert(parent->type>=0);
		devassert(other_parent->type>=0);

		if(!other_parent->isRoot && !parent->isRoot && (other_type==parent_type || (other_type->group_id==parent_type->group_id && parent_type->group_id!=0)))
		{
			gDebuggerObjectRemoveChildInternal(other_parent, obj);
		}
		if(other_parent->isRoot && !parent->isRoot)
		{
			DebuggerRoot *other_root = (DebuggerRoot*)other_parent;

			if(eaiFind(&other_root->types, parent->type)!=-1)
			{
				gDebuggerObjectRemoveChildInternal(other_parent, obj);
			}
		}
		if(!other_parent->isRoot && parent->isRoot)
		{
			DebuggerRoot *root = (DebuggerRoot*)parent;

			if(eaiFind(&root->types, other_parent->type)!=-1)
			{
				gDebuggerObjectRemoveChildInternal(other_parent, obj);
			}
		}
	}

	eaPush(&parent->children, obj);
	eaPush(&obj->parents, parent);

	if(obj->flags)
	{
		for(i=0;i<eaSize(&obj->flags); i++)
		{
			DebuggerFlagImp *flag = obj->flags[i];
			DebuggerFlagBaseImp *base_imp = gDebugState.flags[i];

			if(flag && gDebuggerObjectHasFlagInternal(obj, base_imp->index))
			{
				if(base_imp->value_based)
				{
					gDebuggerObjectSetFlagNotSelf(obj, base_imp->index, flag->value);
				}
				else
				{
					gDebuggerObjectAddFlagNotSelf(obj, base_imp->index, flag->count);
				}
			}
		}
	}

	if(parent->flags)
	{
		for(i=0; i<eaSize(&parent->flags); i++)
		{
			DebuggerFlagBaseImp *base_imp = gDebugState.flags[i];
			DebuggerFlagImp *flag = parent->flags[i];

			if(!flag || !gDebuggerObjectHasFlagInternal(parent, base_imp->index))
			{
				continue;
			}

			if(base_imp->trickle_down)
			{
				if(base_imp->value_based)
				{
					gDebuggerObjectSetFlagNotSelfHelper(obj, base_imp->index, flag->value);
				}
				else
				{
					gDebuggerObjectAddFlagNotSelfHelper(obj, base_imp->index, flag->count);
				}
			}
		}
	}

	if(parent->tree_node && parent->tree_node->open)
	{
		ui_WidgetTreeNodeExpand(parent->tree_node);
	}
}

void gDebuggerObjectAddChildByHandle(ReferenceHandle *hparent, ReferenceHandle *hobj)
{
	DebuggerObject *parent, *obj;
	if(!RefSystem_IsHandleActive(hparent) || !RefSystem_IsHandleActive(hobj))
	{
		return;
	}
	parent = RefSystem_ReferentFromHandle(hparent);
	obj = RefSystem_ReferentFromHandle(hobj);
	if(obj->parents && eaFind(&obj->parents, parent)!=-1)
	{
		return;
	}

	gDebuggerObjectAddChildInternal(parent, obj);
}

void gDebuggerObjectRemoveChildInternal(DebuggerObject *parent, DebuggerObject *obj)
{
	if(parent->tree_node && obj->tree_node)
	{
		ui_WidgetTreeNodeRemoveChild(parent->tree_node, obj->tree_node);
	}

	if(obj->flags)
	{
		int i;
		for(i=0;i<eaSize(&obj->flags); i++)
		{
			DebuggerFlagImp *flag = obj->flags[i];
			DebuggerFlagBaseImp *base_imp = gDebugState.flags[i];

			if(base_imp->trickle_up)
			{
				if(gDebuggerObjectHasFlagInternal(obj, base_imp->index))
				{
					debug_remove_orig = obj->name;
					gDebuggerObjectRemoveFlagNotSelf(obj, base_imp->index, flag->count);
				}
			}
			if(base_imp->trickle_down)
			{
				if(gDebuggerObjectHasFlagInternal(parent, base_imp->index))
				{
					debug_remove_orig = obj->name;
					gDebuggerObjectRemoveFlagNotSelf(obj, base_imp->index, flag->count);
				}
			}
		}
	}

	eaFindAndRemoveFast(&obj->parents, parent);
	eaFindAndRemoveFast(&parent->children, obj);

	if(parent->tree_node && parent->tree_node->open)
	{
		ui_WidgetTreeNodeExpand(parent->tree_node);
	}
}

void gDebuggerObjectRemoveChildByHandle(ReferenceHandle *hparent, ReferenceHandle *hobj)
{
	DebuggerObject *parent, *obj;
	if(!RefSystem_IsHandleActive(hparent) || !RefSystem_IsHandleActive(hobj))
	{
		return;
	}
	parent = RefSystem_ReferentFromHandle(hparent);
	obj = RefSystem_ReferentFromHandle(hobj);

	gDebuggerObjectRemoveChildInternal(parent, obj);
}

void gDebuggerObjectUnlinkByHandle(ReferenceHandle *hobject, void *saved_obj, ParseTable *pt)
{
	DebuggerObject *object;
	if(!RefSystem_IsHandleActive(hobject))
	{
		return;
	}
	object = RefSystem_ReferentFromHandle(hobject);

	if(!object)
		return;

	object->object_data = NULL;
	object->saved_obj = saved_obj;
	object->saved_obj_pt = pt;
	object->unlink_time = timerCpuTicks();

	RefSystem_RemoveHandle(hobject);
}

DebuggerType gDebuggerObjectGetTypeInternal(DebuggerObject *object)
{
	return object->type;
}

DebuggerType gDebuggerObjectGetTypeByHandle(ReferenceHandle *hobject)
{
	DebuggerObject *object;
	if(!RefSystem_IsHandleActive(hobject))
		return -1;

	object = RefSystem_ReferentFromHandle(hobject);

	if(!object)
		return -1;

	return gDebuggerObjectGetTypeInternal(object);
}

void* gDebuggerObjectGetDataInternal(DebuggerObject *object)
{
	return object->object_data;
}

void* gDebuggerObjectGetDataByHandle(ReferenceHandle *hobject)
{
	DebuggerObject *object;
	if(!RefSystem_IsHandleActive(hobject))
		return NULL;
	
	object = RefSystem_ReferentFromHandle(hobject);

	if(!object)
		return NULL;

	return gDebuggerObjectGetDataInternal(object);
}

void* gDebuggerObjectGetPersistDataInternal(DebuggerObject *object)
{
	return object->saved_obj;
}

void* gDebuggerObjectGetPersistDataByHandle(ReferenceHandle *hobject)
{
	DebuggerObject *object;
	if(!RefSystem_IsHandleActive(hobject))
	{
		return NULL;
	}
	object = RefSystem_ReferentFromHandle(hobject);

	if(!object)
		return NULL;

	return gDebuggerObjectGetPersistDataInternal(object);
}

UIPane* gDebuggerObjectGetUserPaneByHandle(ReferenceHandle *hobject)
{
	DebuggerObject *object;
	if(!RefSystem_IsHandleActive(hobject))
	{
		return NULL;
	}
	object = RefSystem_ReferentFromHandle(hobject);

	if(!object)
		return NULL;

	if(object->tree_node)
	{
		return object->tree_node->user_pane;
	}

	return NULL;
}

U32 gDebuggerObjectHasFlagInternal(DebuggerObject *object, DebuggerFlag flag)
{
	DebuggerFlagImp *imp = NULL;
	DebuggerFlagBaseImp *base_imp = gDebugState.flags[flag];

	if(eaSize(&object->flags)<=flag)
	{
		return 0;
	}

	devassert(object->flags);
	imp = object->flags[flag];

	if(base_imp->value_based)
	{
		return !!imp;
	}

	return imp && imp->count>0;
}

U32 gDebuggerObjectHasFlagByHandle(ReferenceHandle *hobject, DebuggerFlag flag)
{
	DebuggerObject *object;
	if(!RefSystem_IsHandleActive(hobject))
	{
		return 0;
	}
	object = RefSystem_ReferentFromHandle(hobject);

	if(!object)
		return 0;

	return gDebuggerObjectHasFlagInternal(object, flag);
}

F32 gDebuggerObjectGetFlagValueByHandle(ReferenceHandle *hobject, DebuggerFlag flag)
{
	DebuggerFlagImp *imp = NULL;
	DebuggerObject *object;

	if(!RefSystem_IsHandleActive(hobject))
	{
		return 0;
	}
	object = RefSystem_ReferentFromHandle(hobject);

	if(!object)
		return 0;

	if(eaSize(&object->flags)<=flag)
	{
		return 0;
	}

	devassert(object->flags);
	imp = object->flags[flag];

	return imp->value;
}

static void gDebuggerObjectFlagCreate(DebuggerObject *object, DebuggerFlag flag)
{
	DebuggerFlagBaseImp *base_imp = gDebugState.flags[flag];

	if(eaSize(&object->flags)<=flag)
	{
		eaSetSize(&object->flags, flag+1);
	}
	devassert(object->flags);	// Mostly for VS

	if(!object->flags[flag])
	{
		object->flags[flag] = callocStruct(DebuggerFlagImp);

		if(base_imp->value_based)
		{
			object->flags[flag]->value = base_imp->default_value;
		}
	}
}

static void gDebuggerObjectRemoveFlagNotSelfHelper(DebuggerObject *object, DebuggerFlag flag, int count)
{
	DebuggerFlagImp *imp = NULL;
	DebuggerFlagBaseImp *base_imp;

	base_imp = gDebugState.flags[flag];

	gDebuggerObjectFlagCreate(object, flag);
	imp = object->flags[flag];
	imp->count -= count;

	//devassert(imp->count>=0);

	//printf("Removing flag (%d) %s from %s (%d) (%s)\n", count, base_imp->name, object->name, imp->count, debug_remove_orig);

	gDebuggerObjectRemoveFlagNotSelf(object, flag, count);
}

static void gDebuggerObjectRemoveFlagNotSelf(DebuggerObject *object, DebuggerFlag flag, int count)
{
	int i;
	DebuggerFlagBaseImp *base_imp;

	base_imp = gDebugState.flags[flag];

	if(base_imp->trickle_up)
	{
		for(i=eaSize(&object->parents)-1; i>=0; i--)
		{
			DebuggerObject *parent = object->parents[i];

			gDebuggerObjectRemoveFlagNotSelfHelper(parent, flag, count);
		}
	}

	if(base_imp->trickle_down)
	{
		for(i=eaSize(&object->children)-1; i>=0; i--)
		{
			DebuggerObject *child = object->children[i];

			gDebuggerObjectRemoveFlagNotSelfHelper(child, flag, count);
		}
	}
}

void gDebuggerObjectRemoveFlagByHandle(ReferenceHandle *hobject, DebuggerFlag flag)
{
	DebuggerFlagImp *imp = NULL;
	DebuggerFlagBaseImp *base_imp;
	DebuggerObject *object;

	if(!RefSystem_IsHandleActive(hobject))
	{
		return;
	}
	object = RefSystem_ReferentFromHandle(hobject);

	if(!object)
		return;

	base_imp = gDebugState.flags[flag];

	gDebuggerObjectFlagCreate(object, flag);
	imp = object->flags[flag];
	if(!imp->set_on_me)
	{
		return;
	}
	imp->count -= 1;
	imp->set_on_me = 0;

	debug_remove_orig = object->name;

	gDebuggerObjectRemoveFlagNotSelf(object, flag, 1);
}

static void gDebuggerObjectAddFlagNotSelfHelper(DebuggerObject *object, DebuggerFlag flag, int count)
{
	DebuggerFlagBaseImp *base_imp;
	DebuggerFlagImp *imp;

	base_imp = gDebugState.flags[flag];

	gDebuggerObjectFlagCreate(object, flag);
	imp = object->flags[flag];
	imp->count += count;
	//devassert(count>0);

	//printf("Adding flag (%d) %s to %s (%d) (%s)\n", count, base_imp->name, object->name, imp->count, debug_add_orig);

	gDebuggerObjectAddFlagNotSelf(object, flag, count);
}

static void gDebuggerObjectAddFlagNotSelf(DebuggerObject *object, DebuggerFlag flag, int count)
{
	int i;
	DebuggerFlagBaseImp *base_imp;

	base_imp = gDebugState.flags[flag];

	if(base_imp->trickle_up)
	{
		for(i=eaSize(&object->parents)-1; i>=0; i--)
		{
			DebuggerObject *parent = object->parents[i];

			gDebuggerObjectAddFlagNotSelfHelper(parent, flag, count);
		}
	}

	if(base_imp->trickle_down)
	{
		for(i=eaSize(&object->children)-1; i>=0; i--)
		{
			DebuggerObject *child = object->children[i];

			gDebuggerObjectAddFlagNotSelfHelper(child, flag, count);
		}
	}
}

void gDebuggerObjectAddFlagByHandle(ReferenceHandle *hobject, DebuggerFlag flag, int count)
{
	DebuggerFlagBaseImp *base_imp;
	DebuggerFlagImp *imp;
	DebuggerObject *object;

	if(!RefSystem_IsHandleActive(hobject))
	{
		return;
	}
	object = RefSystem_ReferentFromHandle(hobject);
	if(!object)
		return;

	base_imp = gDebugState.flags[flag];

	gDebuggerObjectFlagCreate(object, flag);
	imp = object->flags[flag];
	if(imp->set_on_me)
	{
		if(imp->count==count)
		{
			return;
		}
		else
		{
			int diff = count-imp->count;
			imp->count = count;
			debug_add_orig = object->name;
			gDebuggerObjectAddFlagNotSelf(object, flag, diff);
			return;
		}
	}
	imp->count += count;
	imp->set_on_me = 1;

	debug_add_orig = object->name;
	//printf("Adding flag %s to %s (%d) (setonme)\n", base_imp->name, object->name, imp->count);

	gDebuggerObjectAddFlagNotSelf(object, flag, count);
}

void gDebuggerObjectSetFlagNotSelfHelper(DebuggerObject *object, DebuggerFlag flag, F32 value)
{
	DebuggerFlagImp *imp;
	DebuggerFlagBaseImp *base_imp = gDebugState.flags[flag];

	gDebuggerObjectFlagCreate(object, flag);
	imp = object->flags[flag];

	if(!imp->set_on_me)
	{
		imp->value = value;

		if(base_imp->changed_func)
		{
            DebuggerObjectStruct dos = {0};
			ADD_SIMPLE_POINTER_REFERENCE(dos.object, object);
			base_imp->changed_func(&dos.object.__handle_INTERNAL,
									gDebuggerObjectGetTypeInternal(object),
									gDebuggerObjectGetDataInternal(object),
									gDebuggerObjectGetPersistDataInternal(object), 
									imp->value, 1);
			REMOVE_HANDLE(dos.object);
		}
	}
	else
	{
		return;
	}

	gDebuggerObjectSetFlagNotSelf(object, flag, value);
}

void gDebuggerObjectSetFlagNotSelf(DebuggerObject* object, DebuggerFlag	flag, F32 value)
{
	int i;
	DebuggerFlagBaseImp *base_imp;

	base_imp = gDebugState.flags[flag];

	if(base_imp->trickle_up)
	{
		for(i=eaSize(&object->parents)-1; i>=0; i--)
		{
			DebuggerObject *parent = object->parents[i];

			gDebuggerObjectSetFlagNotSelfHelper(parent, flag, value);
		}
	}

	if(base_imp->trickle_down)
	{
		for(i=eaSize(&object->children)-1; i>=0; i--)
		{
			DebuggerObject *child = object->children[i];

			gDebuggerObjectSetFlagNotSelfHelper(child, flag, value);
		}
	}
}

void gDebuggerObjectSetFlagByHandle(ReferenceHandle *hobject, DebuggerFlag flag, F32 value)
{
	DebuggerFlagImp *imp;
	DebuggerFlagBaseImp *base_imp = gDebugState.flags[flag];
	DebuggerObject *object;

	if(!RefSystem_IsHandleActive(hobject))
	{
		return;
	}
	object = RefSystem_ReferentFromHandle(hobject);
	if(!object)
		return;

	gDebuggerObjectFlagCreate(object, flag);
	imp = object->flags[flag];

	imp->value = value;
	imp->set_on_me = 1;

	if(base_imp->changed_func)
	{
        DebuggerObjectStruct dos = {0};
		ADD_SIMPLE_POINTER_REFERENCE(dos.object, object);
		base_imp->changed_func(&dos.object.__handle_INTERNAL,
									gDebuggerObjectGetTypeInternal(object),
									gDebuggerObjectGetDataInternal(object),
									gDebuggerObjectGetPersistDataInternal(object), 
									imp->value, 0);
		REMOVE_HANDLE(dos.object);
	}
	
	gDebuggerObjectSetFlagNotSelf(object, flag, value);
}

void gDebuggerObjectResetFlagHelper(DebuggerObject *object, DebuggerFlag flag, F32 value, U32 override_all)
{
	DebuggerFlagImp *imp;
	DebuggerFlagBaseImp *base_imp = gDebugState.flags[flag];

	gDebuggerObjectFlagCreate(object, flag);
	imp = object->flags[flag];

	if(override_all)
	{
		imp->set_on_me = 0;
		imp->value = value;

		if(base_imp->changed_func)
		{
            DebuggerObjectStruct dos = {0};
			ADD_SIMPLE_POINTER_REFERENCE(dos.object, object);
			base_imp->changed_func(&dos.object.__handle_INTERNAL,
									gDebuggerObjectGetTypeInternal(object),
									gDebuggerObjectGetDataInternal(object),
									gDebuggerObjectGetPersistDataInternal(object), 
									imp->value, 1);
			REMOVE_HANDLE(dos.object);
		}
	}
	else
	{
		if(!imp->set_on_me)
		{
			imp->value = value;

			if(base_imp->changed_func)
			{
                DebuggerObjectStruct dos = {0};
				ADD_SIMPLE_POINTER_REFERENCE(dos.object, object);
				base_imp->changed_func(&dos.object.__handle_INTERNAL,
									gDebuggerObjectGetTypeInternal(object),
									gDebuggerObjectGetDataInternal(object),
									gDebuggerObjectGetPersistDataInternal(object), 
									imp->value, 1);
				REMOVE_HANDLE(dos.object);
			}
		}
	}

	if(override_all || !imp->set_on_me)
	{
		gDebuggerObjectResetFlagNotSelf(object, flag, value, override_all);
	}
}

void gDebuggerObjectResetFlagNotSelf(DebuggerObject* object, DebuggerFlag flag, F32 value, U32 override_all)
{
	int i;
	DebuggerFlagBaseImp *base_imp;

	base_imp = gDebugState.flags[flag];

	if(base_imp->trickle_up)
	{
		for(i=eaSize(&object->parents)-1; i>=0; i--)
		{
			DebuggerObject *parent = object->parents[i];

			gDebuggerObjectResetFlagHelper(parent, flag, value, override_all);
		}
	}

	if(base_imp->trickle_down)
	{
		for(i=eaSize(&object->children)-1; i>=0; i--)
		{
			DebuggerObject *child = object->children[i];

			gDebuggerObjectResetFlagHelper(child, flag, value, override_all);
		}
	}
}

void gDebuggerObjectResetFlagByHandle(ReferenceHandle *hobject, DebuggerFlag flag, U32 override_all)
{
	DebuggerFlagImp *imp;
	DebuggerFlagBaseImp *base_imp = gDebugState.flags[flag];
	DebuggerObject *object;

	if(!RefSystem_IsHandleActive(hobject))
	{
		return;
	}
	object = RefSystem_ReferentFromHandle(hobject);
	if(!object)
		return;
	
	gDebuggerObjectFlagCreate(object, flag);
	imp = object->flags[flag];
	imp->value = base_imp->default_value;
	imp->set_on_me = 0;

	if(base_imp->trickle_down)
	{
		int i;
		int parent_found = 0;
		
		// Reset from all parents
		for(i=0; i<eaSize(&object->parents); i++)
		{
			DebuggerObject *parent = object->parents[i];
			DebuggerFlagImp *parent_flag = parent->flags[flag];
			if(flag < eaSize(&parent->flags))
			{
				gDebuggerObjectResetFlagNotSelf(parent, flag, parent_flag->value, override_all);
				parent_found = 1;
			}
		}

		if(!parent_found)
		{
			if(base_imp->changed_func)
			{
                DebuggerObjectStruct dos = {0};
				ADD_SIMPLE_POINTER_REFERENCE(dos.object, object);
				base_imp->changed_func(&dos.object.__handle_INTERNAL,
									gDebuggerObjectGetTypeInternal(object),
									gDebuggerObjectGetDataInternal(object),
									gDebuggerObjectGetPersistDataInternal(object), 
									imp->value, 1);
				REMOVE_HANDLE(dos.object);
			}
			gDebuggerObjectResetFlagNotSelf(object, flag, imp->value, override_all);
		}
	}

	if(base_imp->trickle_up)
	{
		int i;
		int child_found = 0;

		// Reset from all children
		for(i=0; i<eaSize(&object->children); i++)
		{
			DebuggerObject *child = object->children[i];
			DebuggerFlagImp *child_flag = child->flags[flag];
			if(flag < eaSize(&child->flags))
			{
				gDebuggerObjectResetFlagNotSelf(child, flag, child_flag->value, override_all);
				child_found = 1;
			}
		}

		if(!child_found)
		{
			if(base_imp->changed_func)
			{
                DebuggerObjectStruct dos = {0};
				ADD_SIMPLE_POINTER_REFERENCE(dos.object, object);
				base_imp->changed_func(&dos.object.__handle_INTERNAL,
									gDebuggerObjectGetTypeInternal(object),
									gDebuggerObjectGetDataInternal(object),
									gDebuggerObjectGetPersistDataInternal(object), 
									imp->value, 1);
				REMOVE_HANDLE(dos.object);
			}
			gDebuggerObjectResetFlagNotSelf(object, flag, imp->value, override_all);
		}
	}
}
#endif

AUTO_COMMAND;
void gDebug(void)
{
#ifndef STUB_SOUNDLIB
	gDebugUIToggle();
#endif
}

#include "gDebug_c_ast.c"
