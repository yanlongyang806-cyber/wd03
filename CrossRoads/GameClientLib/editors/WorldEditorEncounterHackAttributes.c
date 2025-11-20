#ifndef NO_EDITORS

#include "WorldEditorEncounterHackAttributes.h"
#include "WorldEditorAttributesPrivate.h"
#include "WorldEditorAttributesHelpers.h"
#include "WorldGrid.h"
#include "WorldEditorUtil.h"
#include "EditLibUIUtil.h"
#include "EditorManager.h"
#include "StringCache.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

/********************
* DEFINITIONS
********************/
#define WLE_AE_ENCOUNTER_ALIGN_WIDTH 150
#define WLE_AE_ENCOUNTER_ENTRY_WIDTH 200

#define wleAEEncounterHackSetupParam(param, fieldName)\
	param.entry_align = WLE_AE_ENCOUNTER_ALIGN_WIDTH;\
	param.struct_offset = offsetof(GroupDef, property_structs.encounter_hack_properties);\
	param.struct_pti = parse_WorldEncounterHackProperties;\
	param.struct_fieldname = fieldName

typedef struct WleAEEncounterHackUI
{
	EMPanel *panel;
	UIRebuildableTree *autoWidget;
	UIScrollArea *scrollArea;

	struct
	{
		WleAEParamDictionary encounterDef;
		WleAEParamFloat physicalSize;
		WleAEParamFloat agroSize;
	} data;
} WleAEEncounterHackUI;

/********************
* GLOBALS
********************/
static WleAEEncounterHackUI wleAEGlobalEncounterHackUI;

int wleAEEncounterHackReload(EMPanel *panel, EditorObject *edObj)
{
	EditorObject **objects = NULL;
	int i;
	bool panelActive = true;
	bool allEncounters = true;
	WorldScope *closest_scope = NULL;

	// Scan selected objects
	wleAEGetSelectedObjects(&objects);
	for (i = 0; i < eaSize(&objects); i++)
	{
		GroupTracker *tracker;

		assert(objects[i]->type->objType == EDTYPE_TRACKER);
		tracker = trackerFromTrackerHandle(objects[i]->obj);
		if (!tracker || !tracker->def)
		{
			// Hide panel if null tracker
			panelActive = allEncounters = false;
			break;
		}
		if (!tracker->def->property_structs.encounter_hack_properties)
		{
			// Hide panel if not an encounter hack
			allEncounters = false;
			break;
		}

		// Disable panel if not editable
		if (!wleTrackerIsEditable(objects[i]->obj, false, false, false))
			panelActive = false;

		// Hide panel if encounters are not in the same scope
		if (!closest_scope)
			closest_scope = tracker->closest_scope;
		else if (closest_scope != tracker->closest_scope)
			allEncounters = false;
	}
	eaDestroy(&objects);

	if (!allEncounters)
		return WLE_UI_PANEL_INVALID;

	// fill data
	wleAEDictionaryUpdate(&wleAEGlobalEncounterHackUI.data.encounterDef);
	wleAEFloatUpdate(&wleAEGlobalEncounterHackUI.data.physicalSize);
	wleAEFloatUpdate(&wleAEGlobalEncounterHackUI.data.agroSize);

	// rebuild UI
	ui_RebuildableTreeInit(wleAEGlobalEncounterHackUI.autoWidget, &wleAEGlobalEncounterHackUI.scrollArea->widget.children, 0, 0, UIRTOptions_Default);
	wleAEDictionaryAddWidget(wleAEGlobalEncounterHackUI.autoWidget, "Encounter Def", "Encounter Def to place (AutoGen systems only)", "base_def", &wleAEGlobalEncounterHackUI.data.encounterDef);
	wleAEFloatAddWidget(wleAEGlobalEncounterHackUI.autoWidget, "Physical Size", "Size of the encounter including actor spawns (To prevent overlap)", "physical_radius", &wleAEGlobalEncounterHackUI.data.physicalSize, 0.1f, 100.f, 0.1f);
	wleAEFloatAddWidget(wleAEGlobalEncounterHackUI.autoWidget, "Agro Size", "Area this encounter \"covers\" - try to place other encounters this far away.", "agro_radius", &wleAEGlobalEncounterHackUI.data.agroSize, 0.1f, 100.f, 0.1f);
	ui_RebuildableTreeDoneBuilding(wleAEGlobalEncounterHackUI.autoWidget);
	emPanelSetHeight(wleAEGlobalEncounterHackUI.panel, elUIGetEndY(wleAEGlobalEncounterHackUI.scrollArea->widget.children[0]->children) + 20);
	wleAEGlobalEncounterHackUI.scrollArea->xSize = emGetSidebarScale() * elUIGetEndX(wleAEGlobalEncounterHackUI.scrollArea->widget.children[0]->children) + 5;
	emPanelSetActive(wleAEGlobalEncounterHackUI.panel, panelActive);

	return WLE_UI_PANEL_OWNED;
}

/********************
* MAIN
********************/
void wleAEEncounterHackCreate(EMPanel *panel)
{
	int i = 1;

	if (wleAEGlobalEncounterHackUI.autoWidget)
		return;

	wleAEGlobalEncounterHackUI.panel = panel;

	// initialize auto widget and scroll area
	wleAEGlobalEncounterHackUI.autoWidget = ui_RebuildableTreeCreate();
	wleAEGlobalEncounterHackUI.scrollArea = ui_ScrollAreaCreate(0, 0, 1, 1, 0, 0, true, false);
	wleAEGlobalEncounterHackUI.scrollArea->widget.heightUnit = UIUnitPercentage;
	wleAEGlobalEncounterHackUI.scrollArea->widget.widthUnit = UIUnitPercentage;
	wleAEGlobalEncounterHackUI.scrollArea->widget.sb->alwaysScrollX = true;
	emPanelAddChild(panel, wleAEGlobalEncounterHackUI.scrollArea, false);

	wleAEEncounterHackSetupParam(wleAEGlobalEncounterHackUI.data.encounterDef, allocAddString("BaseDef"));
	wleAEGlobalEncounterHackUI.data.encounterDef.dictionary = "EncounterDef";

	wleAEEncounterHackSetupParam(wleAEGlobalEncounterHackUI.data.physicalSize, allocAddString("PhysicalSize"));
	wleAEEncounterHackSetupParam(wleAEGlobalEncounterHackUI.data.agroSize, allocAddString("AgroSize"));
}

#endif
