#ifndef NO_EDITORS

#include "WorldEditorAttributesPrivate.h"

#include "WorldGrid.h"
#include "WorldEditorClientMain.h"
#include "WorldEditorOperations.h"
#include "WorldEditorUtil.h"
#include "groupdbmodify.h"
#include "EditorManager.h"
#include "EditLibUIUtil.h"

#include "WorldEditorAttributesHelpers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

typedef struct WleAEChildrenUI
{
	EMPanel *panel;
	UIScrollArea *scroll_area;
	UIRebuildableTree *ui_auto_widget;

	UITextEntry *seed;
	UIButton **name_buttons;
	UILabel **seed_labels;
	UITextEntry **seed_text_entries;
	UIButton **delete_buttons;
	int child_count;

	GroupDef *old_def;
} WleAEChildrenUI;

static WleAEChildrenUI wleAEGlobalChildrenUI;

static void wleAEChildrenFillData(bool is_refill);

//////////////////////////////////////////////////////////////////////////

static void wleAEChildrenApplyChanges(bool is_specified_changed)
{
	TrackerHandle *handle = SAFE_MEMBER(wleAEGetSelected(), obj);
	GroupTracker *tracker = trackerFromTrackerHandle(handle);
	int i;
	UITextEntry *entry;

	if (!wleAEGlobalState.changed || !tracker)
		return;

	tracker = wleOpPropsBegin(handle);

	if (tracker)
	{
		for(i=0; i < eaSize(&tracker->def->children); i++)
		{
			if(entry = eaGet(&wleAEGlobalChildrenUI.seed_text_entries,i))
			{
				tracker->def->children[i]->seed = atoi(ui_TextEntryGetText(entry));
				groupdbDirtyTracker(tracker, i);
			}
		}

		trackerUpdate(tracker,tracker->def,false);
	}

	wleOpPropsEnd();
}

static void wleAEChildrenDataChanged(UIRTNode *node, UserData userData)
{
	if (userData)
	{
		bool *is_specified = userData;
		*is_specified = true;
	}

	wleAEChildrenApplyChanges(!userData);
	wleAEChildrenFillData(true);
}


static void wleAEChildrenDeleteChild(UIAnyWidget *button_widget, GroupTracker *button_def)
{
	TrackerHandle **handles = NULL;
	TrackerHandle *child_handle = trackerHandleCreate(button_def);

	if (child_handle)
	{
		eaPush(&handles,child_handle);
		wleOpDelete(handles);
		eaDestroy(&handles);
	}	
	trackerHandleDestroy(child_handle);
}

static void wleAEChildrenSelectChild(UIAnyWidget *button_widget, GroupTracker *button_def)
{
	wleTrackerSelect(trackerHandleFromTracker(button_def), false);
}

static void wleAEChildrenRebuildUI(void)
{
	char value[1024];
	int i=0;
	const int indent = 60;
	TrackerHandle *handle = SAFE_MEMBER(wleAEGetSelected(), obj);
	GroupTracker *tracker = trackerFromTrackerHandle(handle);
	UILabel *label=NULL;
	UITextEntry *entry;
	UIButton *button = NULL;
	UIAutoWidgetParams params = {0};
	params.NoLabel = true;
	
	ui_RebuildableTreeInit(wleAEGlobalChildrenUI.ui_auto_widget, &wleAEGlobalChildrenUI.scroll_area->widget.children, 0, 10, UIRTOptions_YScroll);

	//Delete Name Buttons
	for(i = 0; i < eaSize(&wleAEGlobalChildrenUI.name_buttons); i++)
		if(button = eaGet(&wleAEGlobalChildrenUI.name_buttons,i))
		{
			ui_ScrollAreaRemoveChild(wleAEGlobalChildrenUI.ui_auto_widget->scrollArea,(UIWidget*)button);
			ui_WidgetQueueFree((UIWidget*)button);
		}
	eaDestroy(&wleAEGlobalChildrenUI.name_buttons);	

	//Delete Seed Labels
	for(i = 0; i < eaSize(&wleAEGlobalChildrenUI.seed_labels); i++)
		if(label = eaGet(&wleAEGlobalChildrenUI.seed_labels,i))
		{
			ui_ScrollAreaRemoveChild(wleAEGlobalChildrenUI.ui_auto_widget->scrollArea,(UIWidget*)label);
			ui_WidgetQueueFree((UIWidget*)label);
		}
	eaDestroy(&wleAEGlobalChildrenUI.seed_labels);
	
	//Delete Seed Text Entries
	for(i = 0; i < eaSize(&wleAEGlobalChildrenUI.seed_text_entries); i++)
		if(entry = eaGet(&wleAEGlobalChildrenUI.seed_text_entries,i))
		{
			ui_ScrollAreaRemoveChild(wleAEGlobalChildrenUI.ui_auto_widget->scrollArea,(UIWidget*)entry);
			ui_WidgetQueueFree((UIWidget*)entry);
		}
	eaDestroy(&wleAEGlobalChildrenUI.seed_text_entries);
	
	//Delete Delete Buttons
	for(i = 0; i < eaSize(&wleAEGlobalChildrenUI.delete_buttons); i++)
		if(button = eaGet(&wleAEGlobalChildrenUI.delete_buttons,i))
		{
			ui_ScrollAreaRemoveChild(wleAEGlobalChildrenUI.ui_auto_widget->scrollArea,(UIWidget*)button);
			ui_WidgetQueueFree((UIWidget*)button);
		}
	eaDestroy(&wleAEGlobalChildrenUI.delete_buttons);

	wleAEGlobalChildrenUI.child_count = 0;
	button = NULL;
	label = NULL;
	entry = NULL;
	if(tracker && tracker->def)
	{
		wleAEGlobalChildrenUI.child_count = eaSize(&tracker->def->children);
		for(i = 0; i < eaSize(&tracker->def->children); i++)
		{
			GroupChild *child = tracker->def->children[i];
			GroupDef *child_def = groupChildGetDef(tracker->def, child, false);

			//Name Button
			sprintf(value, "%s", child_def?child_def->name_str:"(NULL)");
			button = ui_ButtonCreate(value, 5, (button?elUINextY(button):0)+15, wleAEChildrenSelectChild, &(tracker->children[i]));
			ui_ScrollAreaAddChild(wleAEGlobalChildrenUI.ui_auto_widget->scrollArea,(UIWidget*)button);
			eaPush(&wleAEGlobalChildrenUI.name_buttons,button);

			//Seed Value
			label = ui_LabelCreate("Seed", indent+5, elUINextY(button) + 5);
			ui_ScrollAreaAddChild(wleAEGlobalChildrenUI.ui_auto_widget->scrollArea,(UIWidget*)label);
			entry = ui_TextEntryCreate("", indent<<1, label->widget.y);
			ui_TextEntrySetFinishedCallback(entry, wleAEChildrenDataChanged, NULL);
			ui_SetActive((UIWidget*) entry, false);
			wleAEGlobalChildrenUI.seed = entry;
			ui_ScrollAreaAddChild(wleAEGlobalChildrenUI.ui_auto_widget->scrollArea,(UIWidget*)entry);
			eaPush(&wleAEGlobalChildrenUI.seed_labels,label);
			eaPush(&wleAEGlobalChildrenUI.seed_text_entries,entry);

			//Delete Button
			sprintf(value, "Delete Child");
			button = ui_ButtonCreate(value, indent+5, elUINextY(label)+5, wleAEChildrenDeleteChild, &(tracker->children[i]));
			ui_ScrollAreaAddChild(wleAEGlobalChildrenUI.ui_auto_widget->scrollArea,(UIWidget*)button);
			eaPush(&wleAEGlobalChildrenUI.delete_buttons,button);
		}
	}
	wleAEGlobalChildrenUI.ui_auto_widget->root->h = (button?elUINextY(button):0);
	ui_RebuildableTreeDoneBuilding(wleAEGlobalChildrenUI.ui_auto_widget);
}

static void wleAEChildrenFillData(bool is_refill)
{
	TrackerHandle *handle = SAFE_MEMBER(wleAEGetSelected(), obj);
	GroupTracker *tracker = trackerFromTrackerHandle(handle);
	GroupDef *def = tracker?tracker->def:NULL;
	bool active = false;
	int i;
	char value[1024];
	UITextEntry *entry;

	if (!is_refill && def == wleAEGlobalChildrenUI.old_def)
		is_refill = true;

	wleAEGlobalChildrenUI.old_def = def;

	wleAEChildrenRebuildUI();


	//Clear Seed Values
	for(i = 0; i < eaSize(&wleAEGlobalChildrenUI.seed_text_entries); i++)
		ui_TextEntrySetText(eaGet(&wleAEGlobalChildrenUI.seed_text_entries,i), "");

	if (def)
	{	
		active = tracker && tracker->child_count > 0 && wleTrackerIsEditable(trackerHandleFromTracker(tracker), false, false, true);

		for(i = 0; i < eaSize(&def->children); i++)
		{
			if(entry = eaGet(&wleAEGlobalChildrenUI.seed_text_entries,i))
			{
				//Seed Relitive
				sprintf(value, "%d",def->children[i]->seed);
				ui_TextEntrySetText(entry, value);			
				ui_SetActive((UIWidget*) entry, active);
			}
		}
	}

	emPanelSetActive(wleAEGlobalChildrenUI.panel, active);
}

void wleAEChildrenReload(EditorObject *edObj)
{
	wleAEChildrenFillData(false);
}

void wleAEChildrenCreate(EMPanel *panel)
{
	if (wleAEGlobalChildrenUI.ui_auto_widget)
		return;

	wleAEGlobalChildrenUI.child_count = 0;
	wleAEGlobalChildrenUI.panel = panel;
	emPanelSetHeight(panel, 300);
	wleAEGlobalChildrenUI.scroll_area = ui_ScrollAreaCreate(0, 0, 1, 1, 300, 0, true, false);
	wleAEGlobalChildrenUI.scroll_area->widget.widthUnit = UIUnitPercentage;
	wleAEGlobalChildrenUI.scroll_area->widget.heightUnit = UIUnitPercentage;
	emPanelAddChild(panel, wleAEGlobalChildrenUI.scroll_area, false);

	wleAEGlobalChildrenUI.ui_auto_widget = ui_RebuildableTreeCreate();
}

#endif