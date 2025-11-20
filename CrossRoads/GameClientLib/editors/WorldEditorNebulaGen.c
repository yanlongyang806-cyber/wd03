#ifdef ENABLE_NEBULAS // TomY Removing nebula-related code

#ifndef NO_EDITORS

#include "WorldEditorAttributesPrivate.h"

#include "WorldGrid.h"
#include "EditorManager.h"
#include "WorldEditorClientMain.h"
#include "WorldEditorOperations.h"
#include "WorldEditorUtil.h"
#include "ObjectLibrary.h"
#include "EditLibUIUtil.h"

#include "WorldEditorAttributesHelpers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

/********************
* DEFINITIONS
********************/
#define WLE_AE_NEBULAGEN_ALIGN_WIDTH 115

#define wleAENebulaGenSetupParam(param, fieldname)\
	param.entry_align = WLE_AE_NEBULAGEN_ALIGN_WIDTH;\
	param.struct_offset = offsetof(GroupDef, property_structs.nebula_properties);\
	param.struct_pti = parse_WorldNebulaProperties;\
	param.struct_fieldname = fieldname;\
	param.apply_func = wleAENebulaGenApply

#define wleAENebulaGenUpdateInit()\
	GroupTracker *tracker;\
	GroupDef *def;\
	WorldNebulaProperties *properties;\
	assert(obj->type->objType == EDTYPE_TRACKER);\
	tracker = trackerFromTrackerHandle(obj->obj);\
	def = SAFE_MEMBER(tracker, def);\
	properties = SAFE_MEMBER(def, property_structs.nebula_properties)

#define wleAENebulaGenApplyInit()\
	GroupTracker *tracker;\
	GroupDef *def;\
	WorldNebulaProperties *properties;\
	assert(obj->type->objType == EDTYPE_TRACKER);\
	tracker = wleOpPropsBegin(obj->obj);\
	if (!tracker)\
	return;\
	def = SAFE_MEMBER(tracker, def);\
	properties = SAFE_MEMBER(def, property_structs.nebula_properties);\
	if (!properties)\
{\
	wleOpPropsEnd();\
	return;\
}\

typedef struct WleAENebulaGenUI
{
	EMPanel *panel;
	UIRebuildableTree *autoWidget;
	UIScrollArea *scrollArea;
	struct
	{
		WleAEParamInt count;
		WleAEParamFloat length;
		WleAEParamFloat spacing;
		WleAEParamVec3 min_accel;
		WleAEParamVec3 max_accel;

		WleAEParamPicker nebulaPlacedModel;
		WleAEParamPicker nebulaDeformedModel;

	} data;
} WleAENebulaGenUI;

/********************
* GLOBALS
********************/

static WleAENebulaGenUI wleAEGlobalNebulaGenUI;

/********************
* PARAMETER CALLBACKS
********************/

static void wleAENebulaReSeed(void *unused, EditorObject *obj)
{
	wleAENebulaGenApplyInit();
	properties->seed = rand();
	validateNebulaProperties(properties, def, def->filename, def->name_str, def->name_uid);
	wleOpPropsEnd();
}

static void wleAENebulaGenApply(void *unused_param, void *unused, EditorObject *obj)
{
	wleAENebulaGenApplyInit();
	validateNebulaProperties(properties, def, def->filename, def->name_str, def->name_uid);
	wleOpPropsEnd();
}

static void wleAENebulaGenNebulaPlacedModelUpdate(void *unused_param, void *unused, EditorObject *obj)
{
	wleAENebulaGenUpdateInit();

	if (properties)
	{
		wleAEGlobalNebulaGenUI.data.nebulaPlacedModel.object = objectLibraryGetResource(&properties->placed_obj);
		wleAEGlobalNebulaGenUI.data.nebulaPlacedModel.object_parse_table = parse_ResourceInfo;
	}
	else
	{
		wleAEGlobalNebulaGenUI.data.nebulaPlacedModel.object = NULL;
		wleAEGlobalNebulaGenUI.data.nebulaPlacedModel.object_parse_table = NULL;

	}
}

static void wleAENebulaGenNebulaPlacedModelApply(void *unused_param, void *unused, EditorObject *obj)
{
	ResourceInfo *info;
	wleAENebulaGenApplyInit();

	info = wleAEGlobalNebulaGenUI.data.nebulaPlacedModel.object;

	if (info)
	{
		properties->placed_obj.name_str = allocAddString(info->resourceName);
		properties->placed_obj.name_uid = info->resourceID;
	}
	else
	{
		properties->placed_obj.name_str = NULL;
		properties->placed_obj.name_uid = 0;
	}

	validateNebulaProperties(properties, def, def->filename, def->name_str, def->name_uid);

	wleOpPropsEnd();
}

static void wleAENebulaGenNebulaDeformedModelUpdate(void *unused_param, void *unused, EditorObject *obj)
{
	wleAENebulaGenUpdateInit();

	if (properties)
	{
		wleAEGlobalNebulaGenUI.data.nebulaDeformedModel.object = objectLibraryGetResource(&properties->deformed_obj);
		wleAEGlobalNebulaGenUI.data.nebulaDeformedModel.object_parse_table = parse_ResourceInfo;
	}
	else
	{
		wleAEGlobalNebulaGenUI.data.nebulaDeformedModel.object = NULL;
		wleAEGlobalNebulaGenUI.data.nebulaDeformedModel.object_parse_table = NULL;

	}
}

static void wleAENebulaGenNebulaDeformedModelApply(void *unused_param, void *unused, EditorObject *obj)
{
	ResourceInfo *object;
	wleAENebulaGenApplyInit();

	object = wleAEGlobalNebulaGenUI.data.nebulaDeformedModel.object;

	if (object)
	{
		properties->deformed_obj.name_str = allocAllocString(object->resourceName);
		properties->deformed_obj.name_uid = object->resourceID;
	}
	else
	{
		properties->deformed_obj.name_str = NULL;
		properties->deformed_obj.name_uid = 0;
	}

	validateNebulaProperties(properties, def, def->filename, def->name_str, def->name_uid);

	wleOpPropsEnd();
}

static void wleAENebulaRemovePlaced(void *unused, EditorObject *obj)
{
	wleAENebulaGenApplyInit();
	properties->placed_obj.name_str = NULL;
	properties->placed_obj.name_uid = 0;
	validateNebulaProperties(properties, def, def->filename, def->name_str, def->name_uid);
	wleOpPropsEnd();
	wleAENebulaGenNebulaPlacedModelUpdate(NULL, NULL, obj);
}

static void wleAENebulaRemoveDeformed(void *unused, EditorObject *obj)
{
	wleAENebulaGenApplyInit();
	properties->deformed_obj.name_str = NULL;
	properties->deformed_obj.name_uid = 0;
	validateNebulaProperties(properties, def, def->filename, def->name_str, def->name_uid);
	wleOpPropsEnd();
	wleAENebulaGenNebulaDeformedModelUpdate(NULL, NULL, obj);
}


/********************
* MAIN
********************/

int wleAENebulaGenReload(EditorObject *edObj)
{
	static EditorObject *last_object=NULL;
	EditorObject **objects = NULL;
	bool panelActive = true;
	bool notAvailable = false;
	int i;
	Vec3 vec_min, vec_max, vec_step;

	wleAEGetSelectedObjects(&objects);
	for (i = 0; i < eaSize(&objects); i++)
	{
		GroupTracker *tracker;

		assert(objects[i]->type->objType == EDTYPE_TRACKER);
		tracker = trackerFromTrackerHandle(objects[i]->obj);
		if (!tracker || !tracker->def)
		{
			panelActive = false;
			break;
		}

		if (!tracker->def->property_structs.nebula_properties)
		{
			panelActive = false;
			notAvailable = true;
			break;
		}

		if (!wleTrackerIsEditable(objects[i]->obj, false, false, false))
		{
			panelActive = false;
			break;
		}
	}
	eaDestroy(&objects);

	if (notAvailable)
		return 0;

	if(last_object)
		editorObjectDeref(last_object);
	last_object = edObj;
	editorObjectRef(edObj);

	wleAEFloatUpdate(&wleAEGlobalNebulaGenUI.data.length);
	wleAEIntUpdate(&wleAEGlobalNebulaGenUI.data.count);
	wleAEFloatUpdate(&wleAEGlobalNebulaGenUI.data.spacing);
	wleAEVec3Update(&wleAEGlobalNebulaGenUI.data.min_accel);
	wleAEVec3Update(&wleAEGlobalNebulaGenUI.data.max_accel);
	wleAEPickerUpdate(&wleAEGlobalNebulaGenUI.data.nebulaPlacedModel);
	wleAEPickerUpdate(&wleAEGlobalNebulaGenUI.data.nebulaDeformedModel);

	//Rebuild UI
	ui_RebuildableTreeInit(wleAEGlobalNebulaGenUI.autoWidget, &wleAEGlobalNebulaGenUI.scrollArea->widget.children, 0, 0, UIRTOptions_Default);

	wleAEPickerAddWidget(wleAEGlobalNebulaGenUI.autoWidget, "Placed Model", "Rotated and animated.", "nebulaPlacedModel", &wleAEGlobalNebulaGenUI.data.nebulaPlacedModel);
	ui_AutoWidgetAddButton(wleAEGlobalNebulaGenUI.autoWidget->root, " Remove Placed Geo ", wleAENebulaRemovePlaced, edObj, true, "Removed the Placed Object.", NULL);
	wleAEPickerAddWidget(wleAEGlobalNebulaGenUI.autoWidget, "Deformed Model", "Just placed as a child to the curve.", "nebulaDeformedModel", &wleAEGlobalNebulaGenUI.data.nebulaDeformedModel);
	ui_AutoWidgetAddButton(wleAEGlobalNebulaGenUI.autoWidget->root, " Remove Deformed Geo ", wleAENebulaRemoveDeformed, edObj, true, "Removed the Deformed Object.", NULL);

	wleAEFloatAddWidget(wleAEGlobalNebulaGenUI.autoWidget, "Length", "Length of curve.", "length", &wleAEGlobalNebulaGenUI.data.length, 0.0f, 100000.0f, 10.0f);
	wleAEIntAddWidget(wleAEGlobalNebulaGenUI.autoWidget, "Count", "How many key points on the curve.", "count", &wleAEGlobalNebulaGenUI.data.count, 0, 100, 1);
	wleAEFloatAddWidget(wleAEGlobalNebulaGenUI.autoWidget, "Spacing", "Distance between placed objects.", "spacing", &wleAEGlobalNebulaGenUI.data.spacing, 0.0f, 10000.0f, 1.0f);
	setVec3(vec_min, -10.0f, -10.0f, 0.0f);
	setVec3(vec_max,  10.0f,  10.0f, 0.0f);
	setVec3(vec_step, 0.01f,  0.01f, 0.0f);
	wleAEVec3AddWidget(wleAEGlobalNebulaGenUI.autoWidget, "Min Acceleration", "Minimum acceleration relative to velocity vector.", "min_accel", &wleAEGlobalNebulaGenUI.data.min_accel, vec_min, vec_max, vec_step);
	wleAEVec3AddWidget(wleAEGlobalNebulaGenUI.autoWidget, "Max Acceleration", "Maximum acceleration relative to velocity vector.", "max_accel", &wleAEGlobalNebulaGenUI.data.max_accel, vec_min, vec_max, vec_step);
	ui_AutoWidgetAddButton(wleAEGlobalNebulaGenUI.autoWidget->root, " Re-Seed ", wleAENebulaReSeed, edObj, true, "Changes the seed.", NULL);

	ui_RebuildableTreeDoneBuilding(wleAEGlobalNebulaGenUI.autoWidget);
	emPanelSetHeight(wleAEGlobalNebulaGenUI.panel, elUIGetEndY(wleAEGlobalNebulaGenUI.scrollArea->widget.children[0]->children) + 20);
	wleAEGlobalNebulaGenUI.scrollArea->xSize = emGetSidebarScale() * elUIGetEndX(wleAEGlobalNebulaGenUI.scrollArea->widget.children[0]->children) + 5;
	emPanelSetActive(wleAEGlobalNebulaGenUI.panel, panelActive);

	return 2;
}

void wleAENebulaGenCreate(EMPanel *panel)
{
	if (wleAEGlobalNebulaGenUI.autoWidget)
		return;

	wleAEGlobalNebulaGenUI.panel = panel;

	// initialize auto widget and scroll area
	wleAEGlobalNebulaGenUI.autoWidget = ui_RebuildableTreeCreate();
	wleAEGlobalNebulaGenUI.scrollArea = ui_ScrollAreaCreate(0, 0, 1, 1, 0, 0, false, false);
	wleAEGlobalNebulaGenUI.scrollArea->widget.widthUnit = UIUnitPercentage;
	wleAEGlobalNebulaGenUI.scrollArea->widget.heightUnit = UIUnitPercentage;
	emPanelAddChild(panel, wleAEGlobalNebulaGenUI.scrollArea, false);

	wleAENebulaGenSetupParam(wleAEGlobalNebulaGenUI.data.length, "Length");
	wleAENebulaGenSetupParam(wleAEGlobalNebulaGenUI.data.count, "Count");
	wleAENebulaGenSetupParam(wleAEGlobalNebulaGenUI.data.spacing, "Spacing");
	wleAENebulaGenSetupParam(wleAEGlobalNebulaGenUI.data.min_accel, "MinAcceleration");
	wleAENebulaGenSetupParam(wleAEGlobalNebulaGenUI.data.max_accel, "MaxAcceleration");

	wleAEGlobalNebulaGenUI.data.nebulaPlacedModel.entry_align = WLE_AE_NEBULAGEN_ALIGN_WIDTH;
	wleAEGlobalNebulaGenUI.data.nebulaPlacedModel.apply_func = wleAENebulaGenNebulaPlacedModelApply;
	wleAEGlobalNebulaGenUI.data.nebulaPlacedModel.update_func = wleAENebulaGenNebulaPlacedModelUpdate;
	wleAEGlobalNebulaGenUI.data.nebulaPlacedModel.picker = wleGetObjectPicker();
	wleAEGlobalNebulaGenUI.data.nebulaPlacedModel.parse_name_field = "resourceName";

	wleAEGlobalNebulaGenUI.data.nebulaDeformedModel.entry_align = WLE_AE_NEBULAGEN_ALIGN_WIDTH;
	wleAEGlobalNebulaGenUI.data.nebulaDeformedModel.apply_func = wleAENebulaGenNebulaDeformedModelApply;
	wleAEGlobalNebulaGenUI.data.nebulaDeformedModel.update_func = wleAENebulaGenNebulaDeformedModelUpdate;
	wleAEGlobalNebulaGenUI.data.nebulaDeformedModel.picker = wleGetObjectPicker();
	wleAEGlobalNebulaGenUI.data.nebulaDeformedModel.parse_name_field = "resourceName";
}

#endif

#endif // ENABLE_NEBULAS