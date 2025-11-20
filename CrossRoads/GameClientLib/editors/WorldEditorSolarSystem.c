// #ifndef NO_EDITORS
// 
// #include "WorldEditorAttributesPrivate.h"
// 
// #include "WorldGrid.h"
// #include "WorldEditorOperations.h"
// #include "WorldEditorUtil.h"
// #include "EditorManager.h"
// #include "EditLibUIUtil.h"
// #include "wlSolarSystem.h"
// 
// #include "WorldEditorAttributesHelpers.h"
// 
// AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););
// 
// /********************
// * DEFINITIONS
// ********************/
// #define WLE_AE_SOLARSYSTEMGEN_ALIGN_WIDTH 115
// #define MAX_POIS 12
// // #define MAX_HEIGHT 50
// 
// #define wleAESolarSystemGenUpdateInit()\
// 	GroupTracker *tracker;\
// 	GroupDef *def;\
// 	WorldSubMapProperties *sub_map_properties;\
// 	WorldSolarSystemSubMap *properties;\
// 	assert(obj->type->objType == EDTYPE_TRACKER);\
// 	tracker = trackerFromTrackerHandle(obj->obj);\
// 	def = SAFE_MEMBER(tracker, def);\
// 	sub_map_properties = SAFE_MEMBER(def, sub_map_properties);\
// 	properties = SAFE_MEMBER(sub_map_properties, solar_system_info);\
// 
// #define wleAESolarSystemGenApplyInit()\
// 	GroupTracker *tracker;\
// 	GroupDef *def;\
// 	WorldSubMapProperties *sub_map_properties;\
// 	WorldSolarSystemSubMap *properties;\
// 	assert(obj->type->objType == EDTYPE_TRACKER);\
// 	tracker = wleOpPropsBegin(obj->obj);\
// 	if (!tracker)\
// 		return;\
// 	def = SAFE_MEMBER(tracker, def);\
// 	sub_map_properties = SAFE_MEMBER(def, sub_map_properties);\
// 	properties = SAFE_MEMBER(sub_map_properties, solar_system_info);\
// 	if (!properties)\
// 	{\
// 		wleOpPropsEnd();\
// 		return;\
// 	}\
// 
// typedef struct WleAESolarSystemPOIUI
// {
// 	WleAEParamPicker poiFile;
// 	char *fileEntryName;
// 	WleAEParamBool noShoeBox;
// 	WleAEParamInt orbit;
// 	WleAEParamFloat scale;
// 	WleAEParamBool startPOI;
// } WleAESolarSystemPOIUI;
// 
// typedef struct WleAESolarSystemGenUI
// {
// 	EMPanel *panel;
// 	UIRebuildableTree *autoWidget;
// 	UIScrollArea *scrollArea;
// 	struct  
// 	{
// 		WleAEParamText subMapName;
// 		WleAEParamInt poiCount;
// 		int poiMax;
// 		WleAESolarSystemPOIUI *pois;
// 	} data;
// } WleAESolarSystemGenUI;
// 
// /********************
// * GLOBALS
// ********************/
// static WleAESolarSystemGenUI wleAEGlobalSolarSystemGenUI;
// static EMPicker gPOIFilePicker;
// static ResourceGroup s_POIPickerTree;
// 
// /********************
// * PARAMETER CALLBACKS
// ********************/
// 
// static void wleAESolarSystemGenPOIUpdate(void *unused_param, void *poi_idx_p, EditorObject *obj)
// {
// 	int poi_idx = (intptr_t)poi_idx_p;
// 	wleAESolarSystemGenUpdateInit();
// 
// 	StructFreeString(wleAEGlobalSolarSystemGenUI.data.pois[poi_idx].fileEntryName);
// 	wleAEGlobalSolarSystemGenUI.data.pois[poi_idx].fileEntryName = NULL;
// 
// 	if (properties)
// 	{
// 		POIFile *poi_file;
// 
// 		assert(poi_idx >= 0 && poi_idx < eaSize(&properties->poi_list));
// 
// 		poi_file = GET_REF(properties->poi_list[poi_idx]->poi_file_ref);
// 		if(poi_file && poi_file->filename_no_path)
// 			wleAEGlobalSolarSystemGenUI.data.pois[poi_idx].fileEntryName = StructAllocString(poi_file->filename_no_path);
// 		wleAEGlobalSolarSystemGenUI.data.pois[poi_idx].poiFile.object = resGetInfo(POI_FILE_DICTIONARY, wleAEGlobalSolarSystemGenUI.data.pois[poi_idx].fileEntryName);
// 		wleAEGlobalSolarSystemGenUI.data.pois[poi_idx].poiFile.object_parse_table = parse_ResourceInfo;
// 		wleAEGlobalSolarSystemGenUI.data.pois[poi_idx].noShoeBox.boolvalue = properties->poi_list[poi_idx]->no_shoe_box;
// 		wleAEGlobalSolarSystemGenUI.data.pois[poi_idx].orbit.intvalue = properties->poi_list[poi_idx]->orbit;
// 		wleAEGlobalSolarSystemGenUI.data.pois[poi_idx].scale.floatvalue = properties->poi_list[poi_idx]->scale;
// 		wleAEGlobalSolarSystemGenUI.data.pois[poi_idx].startPOI.boolvalue = (poi_idx == properties->start_poi);
// 	}
// 	else
// 	{
// 		wleAEGlobalSolarSystemGenUI.data.pois[poi_idx].poiFile.object = NULL;
// 		wleAEGlobalSolarSystemGenUI.data.pois[poi_idx].poiFile.object_parse_table = NULL;
// 		wleAEGlobalSolarSystemGenUI.data.pois[poi_idx].noShoeBox.boolvalue = false;
// 		wleAEGlobalSolarSystemGenUI.data.pois[poi_idx].orbit.intvalue = poi_idx*10;
// 		wleAEGlobalSolarSystemGenUI.data.pois[poi_idx].scale.floatvalue = 1.0f;
// 		wleAEGlobalSolarSystemGenUI.data.pois[poi_idx].startPOI.boolvalue = false;
// 	}
// }
// 
// static void wleAESolarSystemGenPOINoShoeBoxApply(void *unused_param, void *poi_idx_p, EditorObject *obj)
// {
// 	int poi_idx = (intptr_t)poi_idx_p;
// 	wleAESolarSystemGenApplyInit();
// 
// 	assert(poi_idx >= 0 && poi_idx < eaSize(&properties->poi_list));
// 
// 	properties->poi_list[poi_idx]->no_shoe_box = wleAEGlobalSolarSystemGenUI.data.pois[poi_idx].noShoeBox.boolvalue;
// 	validateSubMapProperties(sub_map_properties, def, (def && def->file) ? gfileGetFilename(def->file) : NULL);
// 	wleOpPropsEnd();
// }
// 
// static void wleAESolarSystemGenPOIOrbitApply(void *unused_param, void *poi_idx_p, EditorObject *obj)
// {
// 	int poi_idx = (intptr_t)poi_idx_p;
// 	wleAESolarSystemGenApplyInit();
// 
// 	assert(poi_idx >= 0 && poi_idx < eaSize(&properties->poi_list));
// 
// 	properties->poi_list[poi_idx]->orbit = wleAEGlobalSolarSystemGenUI.data.pois[poi_idx].orbit.intvalue;
// 	validateSubMapProperties(sub_map_properties, def, (def && def->file) ? gfileGetFilename(def->file) : NULL);
// 	wleOpPropsEnd();
// }
// 
// static void wleAESolarSystemGenPOIScaleApply(void *unused_param, void *poi_idx_p, EditorObject *obj)
// {
// 	int poi_idx = (intptr_t)poi_idx_p;
// 	wleAESolarSystemGenApplyInit();
// 
// 	assert(poi_idx >= 0 && poi_idx < eaSize(&properties->poi_list));
// 
// 	properties->poi_list[poi_idx]->scale = wleAEGlobalSolarSystemGenUI.data.pois[poi_idx].scale.floatvalue;
// 	validateSubMapProperties(sub_map_properties, def, (def && def->file) ? gfileGetFilename(def->file) : NULL);
// 	wleOpPropsEnd();
// }
// 
// static void wleAESolarSystemGenStartPOIApply(void *unused_param, void *poi_idx_p, EditorObject *obj)
// {
// 	int i;
// 	int poi_idx = (intptr_t)poi_idx_p;
// 	wleAESolarSystemGenApplyInit();
// 
// 	assert(poi_idx >= 0 && poi_idx < eaSize(&properties->poi_list));
// 
// 	properties->start_poi = poi_idx;
// 	validateSubMapProperties(sub_map_properties, def, (def && def->file) ? gfileGetFilename(def->file) : NULL);
// 	wleOpPropsEnd();
// 
// 	for (i = 0; i < wleAEGlobalSolarSystemGenUI.data.poiCount.intvalue; ++i)
// 	{
// 		wleAEFloatUpdate(&wleAEGlobalSolarSystemGenUI.data.pois[i].scale);
// 	}
// }
// 
// static void wleAESolarSystemGenPOIFileApply(void *unused_param, void *poi_idx_p, EditorObject *obj)
// {
// 	ResourceInfo *data;
// 	POIFile *poi_file = NULL;
// 	int poi_idx = (intptr_t)poi_idx_p;
// 	wleAESolarSystemGenApplyInit();
// 
// 	assert(poi_idx >= 0 && poi_idx < eaSize(&properties->poi_list));
// 
// 	data = wleAEGlobalSolarSystemGenUI.data.pois[poi_idx].poiFile.object;
// 	if(data && data->resourceName)
// 		poi_file = RefSystem_ReferentFromString(POI_FILE_DICTIONARY, data->resourceName);
// 
// 	REMOVE_HANDLE(properties->poi_list[poi_idx]->poi_file_ref);
// 	if(poi_file)
// 		SET_HANDLE_FROM_REFERENT(POI_FILE_DICTIONARY, poi_file, properties->poi_list[poi_idx]->poi_file_ref);
// 
// 	validateSubMapProperties(sub_map_properties, def, (def && def->file) ? gfileGetFilename(def->file) : NULL);
// 
// 	wleOpPropsEnd();
// }
// 
// static void wleAESolarSystemGenPOICountUpdate(WleAEParamInt *param, void *unused, EditorObject *obj)
// {
// 	wleAESolarSystemGenUpdateInit();
// 	if (properties)
// 	{
// 		param->intvalue = eaSize(&properties->poi_list);
// 
// 		if (wleAEGlobalSolarSystemGenUI.data.poiMax < param->intvalue)
// 		{
// 			int i;
// 
// 			wleAEGlobalSolarSystemGenUI.data.pois = realloc(wleAEGlobalSolarSystemGenUI.data.pois, param->intvalue * sizeof(*wleAEGlobalSolarSystemGenUI.data.pois));
// 			memset(wleAEGlobalSolarSystemGenUI.data.pois + wleAEGlobalSolarSystemGenUI.data.poiMax, 0, (param->intvalue - wleAEGlobalSolarSystemGenUI.data.poiMax) * sizeof(*wleAEGlobalSolarSystemGenUI.data.pois));
// 
// 			for (i = 0; i < param->intvalue; ++i)
// 			{
// 				wleAEGlobalSolarSystemGenUI.data.pois[i].poiFile.entry_align = WLE_AE_SOLARSYSTEMGEN_ALIGN_WIDTH;
// 				wleAEGlobalSolarSystemGenUI.data.pois[i].poiFile.apply_func = wleAESolarSystemGenPOIFileApply;
// 				wleAEGlobalSolarSystemGenUI.data.pois[i].poiFile.update_func = wleAESolarSystemGenPOIUpdate;
// 				wleAEGlobalSolarSystemGenUI.data.pois[i].poiFile.update_data = (void *)(intptr_t)i;
// 				wleAEGlobalSolarSystemGenUI.data.pois[i].poiFile.apply_data = (void *)(intptr_t)i;
// 				wleAEGlobalSolarSystemGenUI.data.pois[i].poiFile.picker = &gPOIFilePicker;
// 				wleAEGlobalSolarSystemGenUI.data.pois[i].poiFile.parse_name_field = "resourceDisplayName";
// 
// 				wleAEGlobalSolarSystemGenUI.data.pois[i].noShoeBox.entry_align = WLE_AE_SOLARSYSTEMGEN_ALIGN_WIDTH;
// 				wleAEGlobalSolarSystemGenUI.data.pois[i].noShoeBox.apply_func = wleAESolarSystemGenPOINoShoeBoxApply;
// 				wleAEGlobalSolarSystemGenUI.data.pois[i].noShoeBox.update_func = wleAESolarSystemGenPOIUpdate;
// 				wleAEGlobalSolarSystemGenUI.data.pois[i].noShoeBox.update_data = (void *)(intptr_t)i;
// 				wleAEGlobalSolarSystemGenUI.data.pois[i].noShoeBox.apply_data = (void *)(intptr_t)i;
// 
// 				wleAEGlobalSolarSystemGenUI.data.pois[i].orbit.entry_align = WLE_AE_SOLARSYSTEMGEN_ALIGN_WIDTH;
// 				wleAEGlobalSolarSystemGenUI.data.pois[i].orbit.apply_func = wleAESolarSystemGenPOIOrbitApply;
// 				wleAEGlobalSolarSystemGenUI.data.pois[i].orbit.update_func = wleAESolarSystemGenPOIUpdate;
// 				wleAEGlobalSolarSystemGenUI.data.pois[i].orbit.update_data = (void *)(intptr_t)i;
// 				wleAEGlobalSolarSystemGenUI.data.pois[i].orbit.apply_data = (void *)(intptr_t)i;
// 
// 				wleAEGlobalSolarSystemGenUI.data.pois[i].scale.entry_align = WLE_AE_SOLARSYSTEMGEN_ALIGN_WIDTH;
// 				wleAEGlobalSolarSystemGenUI.data.pois[i].scale.apply_func = wleAESolarSystemGenPOIScaleApply;
// 				wleAEGlobalSolarSystemGenUI.data.pois[i].scale.update_func = wleAESolarSystemGenPOIUpdate;
// 				wleAEGlobalSolarSystemGenUI.data.pois[i].scale.update_data = (void *)(intptr_t)i;
// 				wleAEGlobalSolarSystemGenUI.data.pois[i].scale.apply_data = (void *)(intptr_t)i;
// 
// 				wleAEGlobalSolarSystemGenUI.data.pois[i].startPOI.entry_align = WLE_AE_SOLARSYSTEMGEN_ALIGN_WIDTH;
// 				wleAEGlobalSolarSystemGenUI.data.pois[i].startPOI.apply_func = wleAESolarSystemGenStartPOIApply;
// 				wleAEGlobalSolarSystemGenUI.data.pois[i].startPOI.update_func = wleAESolarSystemGenPOIUpdate;
// 				wleAEGlobalSolarSystemGenUI.data.pois[i].startPOI.update_data = (void *)(intptr_t)i;
// 				wleAEGlobalSolarSystemGenUI.data.pois[i].startPOI.apply_data = (void *)(intptr_t)i;
// 			}
// 
// 			wleAEGlobalSolarSystemGenUI.data.poiMax = param->intvalue;
// 		}
// 	}
// 	else
// 	{
// 		param->intvalue = 0;
// 	}
// }
// 
// static void wleAESolarSystemGenPOICountApply(WleAEParamInt *param, void *unused, EditorObject *obj)
// {
// 	int i;
// 
// 	wleAESolarSystemGenApplyInit();
// 
// 	param->intvalue = CLAMP(param->intvalue, 1, MAX_POIS);
// 
// 	// delete extra layers
// 	for (i = eaSize(&properties->poi_list) - 1; i >= param->intvalue; --i)
// 	{
// 		WorldSolarSystemPOI *layer = eaPop(&properties->poi_list);
// 		StructDestroy(parse_WorldSolarSystemPOI, layer);
// 	}
// 
// 	// add new layers
// 	assert(eaSize(&properties->poi_list) > 0);
// 	for (i = eaSize(&properties->poi_list); i < param->intvalue; ++i)
// 	{
// 		WorldSolarSystemPOI *poi = StructCreate(parse_WorldSolarSystemPOI);
// 		if(i>=1)
// 		{
// 			POIFile *poi_file = GET_REF(properties->poi_list[i-1]->poi_file_ref);
// 			if(poi_file)
// 				SET_HANDLE_FROM_REFERENT(POI_FILE_DICTIONARY, poi_file, poi->poi_file_ref);
// 		}
// 		poi->scale = 1.0f;
// 		eaPush(&properties->poi_list, poi);
// 	}
// 	if(properties->start_poi >= eaUSize(&properties->poi_list))
// 		properties->start_poi = eaSize(&properties->poi_list)-1;
// 
// 	validateSubMapProperties(sub_map_properties, def, (def && def->file) ? gfileGetFilename(def->file) : NULL);
// 
// 	wleOpPropsEnd();
// }
// 
// static void wleAESolarSystemGenSubMapNameApply(WleAEParamText *param, void *unused, EditorObject *obj)
// {
// 	wleAESolarSystemGenApplyInit();
// 
// 	StructFreeString(sub_map_properties->sub_map_name);
// 	sub_map_properties->sub_map_name = NULL;
// 	if(param->stringvalue)
// 		sub_map_properties->sub_map_name = StructAllocString(param->stringvalue);
// 
// 	validateSubMapProperties(sub_map_properties, def, (def && def->file) ? gfileGetFilename(def->file) : NULL);
// 	wleOpPropsEnd();
// }
// 
// static void wleAESolarSystemGenSubMapNameUpdate(WleAEParamText *param, void *unused, EditorObject *obj)
// {
// 	wleAESolarSystemGenUpdateInit();
// 
// 	StructFreeString(param->stringvalue);
// 	param->stringvalue = NULL;
// 	if (sub_map_properties && sub_map_properties->sub_map_name)
// 		param->stringvalue = StructAllocString(sub_map_properties->sub_map_name);
// }
// 
// /********************
// * Picker Functions
// ********************/
// 
// static bool wleAESolarSystemEMPickerSelected(EMPicker *pPicker, EMPickerSelection *pSelection)
// {
// 	assert(pSelection->table == parse_ResourceInfo);
// 
// 	sprintf(pSelection->doc_name, "%s", ((ResourceInfo*)pSelection->data)->resourceName);
// 	strcpy(pSelection->doc_type, "poi");
// 
// 	return true;
// }
// 
// static void wleAESolarSystemEMPickerEnter(EMPicker *pPicker)
// {
// 	// Load the list
// 	resBuildGroupTree(POI_FILE_DICTIONARY, &s_POIPickerTree);
// 	emPickerRefresh(&gPOIFilePicker);
// }
// static void wleAESolarSystemEMPickerLeave(EMPicker *pPicker)
// {
// 
// }
// 
// static void wleAESolarSystemEMPickerInit(EMPicker *pPicker)
// {
// 	EMPickerDisplayType *pDispType;
// 
// 	pPicker->display_data_root = &s_POIPickerTree;
// 	pPicker->display_parse_info_root = parse_ResourceGroup;
// 
// 	pDispType = calloc(1, sizeof(EMPickerDisplayType));
// 	pDispType->parse_info = parse_ResourceGroup;
// 	pDispType->display_name_parse_field = "Name";
// 	pDispType->color = CreateColorRGB(0, 0, 0);
// 	pDispType->selected_color = CreateColorRGB(255, 255, 255);
// 	pDispType->is_leaf = false;
// 	eaPush(&pPicker->display_types, pDispType);
// 
// 	pDispType = calloc(1, sizeof(EMPickerDisplayType));
// 	pDispType->parse_info = parse_ResourceInfo;
// 	pDispType->display_name_parse_field = "resourceName";
// 	pDispType->display_notes_parse_field = "Notes";
// 	pDispType->selected_func = wleAESolarSystemEMPickerSelected;
// 	pDispType->color = CreateColorRGB(0,0,80);
// 	pDispType->selected_color = CreateColorRGB(255, 255, 255);
// 	pDispType->is_leaf = true;
// 	eaPush(&pPicker->display_types, pDispType);
// }
// 
// /********************
// * MAIN
// ********************/
// bool wleAESolarSystemGenReload(EditorObject *edObj)
// {
// 	EditorObject **objects = NULL;
// 	bool panelActive = true;
// 	bool notAvailable = false;
// 	int i;
// 
// 	wleAEGetSelectedObjects(&objects);
// 	for (i = 0; i < eaSize(&objects); i++)
// 	{
// 		GroupTracker *tracker;
// 
// 		assert(objects[i]->type->objType == EDTYPE_TRACKER);
// 		tracker = trackerFromTrackerHandle(objects[i]->obj);
// 		if (!tracker || !tracker->def)
// 		{
// 			panelActive = false;
// 			break;
// 		}
// 
// 		if (!tracker->def->sub_map_properties || !tracker->def->sub_map_properties->solar_system_info)
// 		{
// 			panelActive = false;
// 			notAvailable = true;
// 			break;
// 		}
// 
// 		if (!wleTrackerIsEditable(objects[i]->obj, false, false, false))
// 		{
// 			panelActive = false;
// 			break;
// 		}
// 	}
// 	eaDestroy(&objects);
// 
// 	if (notAvailable)
// 		return false;
// 
// 	// fill data
// 	wleAETextUpdate(&wleAEGlobalSolarSystemGenUI.data.subMapName);
// 	wleAEIntUpdate(&wleAEGlobalSolarSystemGenUI.data.poiCount);
// 	for (i = 0; i < wleAEGlobalSolarSystemGenUI.data.poiCount.intvalue; ++i)
// 	{
// 		wleAEPickerUpdate(&wleAEGlobalSolarSystemGenUI.data.pois[i].poiFile);
// 		wleAEBoolUpdate(&wleAEGlobalSolarSystemGenUI.data.pois[i].noShoeBox);
// 		wleAEIntUpdate(&wleAEGlobalSolarSystemGenUI.data.pois[i].orbit);
// 		wleAEFloatUpdate(&wleAEGlobalSolarSystemGenUI.data.pois[i].scale);
// 		wleAEBoolUpdate(&wleAEGlobalSolarSystemGenUI.data.pois[i].startPOI);
// 	}
// 
// 	// rebuild UI
// 	ui_RebuildableTreeInit(wleAEGlobalSolarSystemGenUI.autoWidget, &wleAEGlobalSolarSystemGenUI.scrollArea->widget.children, 0, 0, UIRTOptions_Default);
// 
// 	wleAETextAddWidget(wleAEGlobalSolarSystemGenUI.autoWidget, "Sub Map Name", "Unique name to be used for the name of the sub map", "subMapName", &wleAEGlobalSolarSystemGenUI.data.subMapName);
// 
// 	wleAEIntAddWidget(wleAEGlobalSolarSystemGenUI.autoWidget, "Points of Interest", "The number of points of interest in this solar system.", "poiCount", &wleAEGlobalSolarSystemGenUI.data.poiCount, 1, MAX_POIS, 1);
// 
// 	for (i = 0; i < wleAEGlobalSolarSystemGenUI.data.poiCount.intvalue; ++i)
// 	{
// 		char param_name[1024];
// 
// 		sprintf(param_name, "NL_%d", i+1);
// 		ui_RebuildableTreeAddLabelKeyed(wleAEGlobalSolarSystemGenUI.autoWidget->root, "", param_name, NULL, true);
// 
// 		sprintf(param_name, "Point %d", i+1);
// 		ui_RebuildableTreeAddLabelKeyed(wleAEGlobalSolarSystemGenUI.autoWidget->root, param_name, param_name, NULL, true);
// 
// 		sprintf(param_name, "poiFile%d", i);
// 		wleAEPickerAddWidget(wleAEGlobalSolarSystemGenUI.autoWidget, "POI File", "File to use for the point of interest.", param_name, &wleAEGlobalSolarSystemGenUI.data.pois[i].poiFile);
// 
// 		sprintf(param_name, "startPOI%d", i);
// 		wleAEBoolAddWidget(wleAEGlobalSolarSystemGenUI.autoWidget, "Starting POI", "If set, then this is the POI you will enter from sector space.", param_name, &wleAEGlobalSolarSystemGenUI.data.pois[i].startPOI);
// 
// 		sprintf(param_name, "noShoeBox%d", i);
// 		wleAEBoolAddWidget(wleAEGlobalSolarSystemGenUI.autoWidget, "No Shoe Box", "If set, then the point will not be able to be traveled to.", param_name, &wleAEGlobalSolarSystemGenUI.data.pois[i].noShoeBox);
// 
// 		sprintf(param_name, "orbit%d", i);
// 		wleAEIntAddWidget(wleAEGlobalSolarSystemGenUI.autoWidget, "Orbit", "How far from the center of the system.", param_name, &wleAEGlobalSolarSystemGenUI.data.pois[i].orbit, 0, 25, 1);
// 
// 		sprintf(param_name, "scale%d", i);
// 		wleAEFloatAddWidget(wleAEGlobalSolarSystemGenUI.autoWidget, "Scale", "Scale the poi on the sector map.", param_name, &wleAEGlobalSolarSystemGenUI.data.pois[i].scale, 0.1, 100, 0.01);
// 	}
// 
// 	ui_RebuildableTreeDoneBuilding(wleAEGlobalSolarSystemGenUI.autoWidget);
// 	emPanelSetHeight(wleAEGlobalSolarSystemGenUI.panel, elUIGetEndY(wleAEGlobalSolarSystemGenUI.scrollArea->widget.children[0]->children) + 20);
// 	wleAEGlobalSolarSystemGenUI.scrollArea->xSize = emGetSidebarScale() * elUIGetEndX(wleAEGlobalSolarSystemGenUI.scrollArea->widget.children[0]->children) + 5;
// 	emPanelSetActive(wleAEGlobalSolarSystemGenUI.panel, panelActive);
// 
// 	return true;
// }
// 
// void wleAESolarSystemGenCreate(EMPanel *panel)
// {
// 	if (wleAEGlobalSolarSystemGenUI.autoWidget)
// 		return;
// 
// 	wleAEGlobalSolarSystemGenUI.panel = panel;
// 
// 	// initialize auto widget and scroll area
// 	wleAEGlobalSolarSystemGenUI.autoWidget = ui_RebuildableTreeCreate();
// 	wleAEGlobalSolarSystemGenUI.scrollArea = ui_ScrollAreaCreate(0, 0, 1, 1, 0, 0, false, false);
// 	wleAEGlobalSolarSystemGenUI.scrollArea->widget.widthUnit = UIUnitPercentage;
// 	wleAEGlobalSolarSystemGenUI.scrollArea->widget.heightUnit = UIUnitPercentage;
// 	emPanelAddChild(panel, wleAEGlobalSolarSystemGenUI.scrollArea, false);
// 
// 	// set parameter settings
// 	wleAEGlobalSolarSystemGenUI.data.subMapName.entry_align = WLE_AE_SOLARSYSTEMGEN_ALIGN_WIDTH;
// 	wleAEGlobalSolarSystemGenUI.data.subMapName.apply_func = wleAESolarSystemGenSubMapNameApply;
// 	wleAEGlobalSolarSystemGenUI.data.subMapName.update_func = wleAESolarSystemGenSubMapNameUpdate;
// 
// 	wleAEGlobalSolarSystemGenUI.data.poiCount.entry_align = WLE_AE_SOLARSYSTEMGEN_ALIGN_WIDTH;
// 	wleAEGlobalSolarSystemGenUI.data.poiCount.apply_func = wleAESolarSystemGenPOICountApply;
// 	wleAEGlobalSolarSystemGenUI.data.poiCount.update_func = wleAESolarSystemGenPOICountUpdate;
// 
// 	// Register the picker
// 	solarSystemCheckLoadPOILib();
// 	gPOIFilePicker.allow_outsource = 0;
// 	strcpy(gPOIFilePicker.picker_name, "Point of Interest Picker");
// 	gPOIFilePicker.init_func = wleAESolarSystemEMPickerInit;
// 	gPOIFilePicker.enter_func = wleAESolarSystemEMPickerEnter;
// 	gPOIFilePicker.leave_func = wleAESolarSystemEMPickerLeave;
// 	strcpy(gPOIFilePicker.default_type, "poi");
// 	emPickerRegister(&gPOIFilePicker);
// }
// 
// #endif