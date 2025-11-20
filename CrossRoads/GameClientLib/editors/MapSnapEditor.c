
#include "EditLib.h"
#include "EditorManager.h"
#include "EditorPrefs.h"
#include "GfxClipper.h"
#include "GfxMapSnap.h"
#include "GfxHeadshot.h"
#include "GfxPrimitive.h"
#include "GfxSprite.h"
#include "GfxSpriteText.h"
#include "GfxTexAtlas.h"
#include "GroupDBModify.h"
#include "InputLib.h"
#include "RoomConn.h"
#include "StringCache.h"
#include "WorldEditorUI.h"
#include "WorldEditorOperations.h"
#include "WorldGrid.h"

#include "MapSnapEditor.h"

#include "AutoGen/MapSnapEditor_h_ast.c"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#ifndef NO_EDITORS

//Defines
#define MS_UI_LEFT_OFFSET 10
#define MS_UI_LEFT_OFFSET_2 100
#define MS_UI_GAP 5
#define MS_UI_SLIDER_SPEED 1.0f

//Global Data
static EMEditor gMapSnap;
static mapSnapDoc *gMapSnapDoc = NULL;
static EMToolbar *gMapSnapToolbar = NULL;
static UIButtonCombo *gMapSnapToolbarSelectionButtonCombo = NULL;

//////////////////////////////////////////////////////////////////////////
// Camera Setup
//////////////////////////////////////////////////////////////////////////

static void mapSnapSetupCameraFromMinMidMax(mapSnapDoc *doc, Vec3 cam_min, Vec3 cam_mid, Vec3 cam_max)
{
	static bool bOverride = false;
	static F32 fFar = 0.0f;
	static F32 fNear = 0.0f;

	doc->base_doc.editor->camera->campyr[0] = -PI;
	doc->base_doc.editor->camera->campyr[2] = -PI;
	copyVec3(cam_mid, doc->base_doc.editor->camera->camcenter);
	doc->base_doc.editor->camera->camcenter[0] += doc->position_offset[0];
	doc->base_doc.editor->camera->camcenter[2] += doc->position_offset[1];
	doc->base_doc.editor->camera->camdist = 0;
	doc->base_doc.editor->camera->override_hide_editor_objects = true;
	doc->base_doc.editor->camera->ortho_mode_ex = true;
	doc->base_doc.editor->camera->ortho_aspect = -1;
	doc->base_doc.editor->camera->ortho_width = doc->padding;
	doc->base_doc.editor->camera->ortho_height = doc->padding;
	doc->base_doc.editor->camera->ortho_cull_width = cam_max[0] - cam_min[0];
	doc->base_doc.editor->camera->ortho_cull_height = cam_max[2] - cam_min[2];
	doc->base_doc.editor->camera->ortho_near = doc->base_doc.editor->camera->camcenter[1] - ui_SliderTextEntryGetValue(doc->near_plane_slider);
	doc->base_doc.editor->camera->ortho_far = doc->base_doc.editor->camera->camcenter[1] - ui_SliderTextEntryGetValue(doc->far_plane_slider);

	if (bOverride)
	{
		doc->base_doc.editor->camera->ortho_near = fNear;
		doc->base_doc.editor->camera->ortho_far = fFar;
	}
}

static void mapSnapSetupCamera(mapSnapDoc *doc, mapSnapPartition *partition)
{
	if(!partition)
		mapSnapSetupCameraFromMinMidMax(doc, doc->reg_min, doc->reg_mid, doc->reg_max);
	else
		mapSnapSetupCameraFromMinMidMax(doc, partition->bounds_min, partition->bounds_mid, partition->bounds_max);
}

static void mapSnapSetAllPanelsActive(mapSnapDoc *doc, bool set_active)
{
	emPanelSetActive(doc->view_panel, set_active);
	emPanelSetActive(doc->action_panel, set_active);
	emPanelSetActive(doc->global_panel, set_active);
	emPanelSetActive(doc->preview_panel, set_active);
//	emPanelSetActive(doc->fow_panel, set_active);
}

static void mapSnapClearSelection(mapSnapDoc *doc)
{
	eafClear(&doc->point_list);
	setVec2same(doc->selection_min, 0.0f);
	setVec2same(doc->selection_max, 0.0f);
	doc->point_list_closed = false;
}

//////////////////////////////////////////////////////////////////////////
// Auto Commands
//////////////////////////////////////////////////////////////////////////

#endif
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("MapSnap.DebugActivatePanels");
void mapSnapDebugActivatePanels()
{
#ifndef NO_EDITORS
	mapSnapDoc *doc = gMapSnapDoc;
	if(!doc)
		return;
	mapSnapSetAllPanelsActive(doc, true);
#endif
}
#ifndef NO_EDITORS

#endif
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("MapSnap.DebugDisableSliderClamps");
void mapSnapDebugDisableSliderClamps()
{
#ifndef NO_EDITORS
	mapSnapDoc *doc = gMapSnapDoc;
	if(!doc)
		return;
	ui_SliderTextEntrySetRange(doc->far_plane_slider, -30000, 30000, 0.1f);
	ui_SliderTextEntrySetRange(doc->near_plane_slider, -30000, 30000, 0.1f);
#endif
}
#ifndef NO_EDITORS

#endif
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("MapSnap.NearSliderAdd");
void mapSnapNearSliderAdd(bool mode)
{
#ifndef NO_EDITORS
	mapSnapDoc *doc = gMapSnapDoc;
	if(!doc)
		return;
	doc->key_near_add = (mode ? 1 : 0);
#endif
}
#ifndef NO_EDITORS

#endif
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("MapSnap.NearSliderSub");
void mapSnapNearSliderSub(bool mode)
{
#ifndef NO_EDITORS
	mapSnapDoc *doc = gMapSnapDoc;
	if(!doc)
		return;
	doc->key_near_sub = (mode ? 1 : 0);
#endif
}
#ifndef NO_EDITORS

#endif
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("MapSnap.FarSliderAdd");
void mapSnapFarSliderAdd(bool mode)
{
#ifndef NO_EDITORS
	mapSnapDoc *doc = gMapSnapDoc;
	if(!doc)
		return;
	doc->key_far_add = (mode ? 1 : 0);
#endif
}
#ifndef NO_EDITORS

#endif
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("MapSnap.FarSliderSub");
void mapSnapFarSliderSub(bool mode)
{
#ifndef NO_EDITORS
	mapSnapDoc *doc = gMapSnapDoc;
	if(!doc)
		return;
	doc->key_far_sub = (mode ? 1 : 0);
#endif
}
#ifndef NO_EDITORS

#endif
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("MapSnap.ViewModePreview");
void mapSnapViewModePreview(void)
{
#ifndef NO_EDITORS
	mapSnapDoc *doc = gMapSnapDoc;
	if(!doc)
		return;
	doc->view_mode = MSNAP_Preview;
#endif
}
#ifndef NO_EDITORS

#endif
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("MapSnap.ViewModeImage");
void mapSnapViewModeImage(void)
{
#ifndef NO_EDITORS
	mapSnapDoc *doc = gMapSnapDoc;
	if(!doc)
		return;
	doc->view_mode = MSNAP_Image;
#endif
}
#ifndef NO_EDITORS

#endif
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("MapSnap.ViewModeAllImages");
void mapSnapViewModeAllImages(void)
{
#ifndef NO_EDITORS
	mapSnapDoc *doc = gMapSnapDoc;
	if(!doc)
		return;
	doc->view_mode = MSNAP_AllImages;
#endif
}
#ifndef NO_EDITORS

#endif
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("MapSnap.SelectionAdd");
void mapSnapSelectionModeAdd(bool mode)
{
#ifndef NO_EDITORS
	mapSnapDoc *doc = gMapSnapDoc;
	if(!doc)
		return;
	doc->key_shift = mode;
#endif
}
#ifndef NO_EDITORS

#endif
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("MapSnap.SelectionSub");
void mapSnapSelectionModeSub(bool mode)
{
#ifndef NO_EDITORS
	mapSnapDoc *doc = gMapSnapDoc;
	if(!doc)
		return;
	doc->key_ctrl = mode;
#endif
}
#ifndef NO_EDITORS

#endif
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("MapSnap.ZoomOut");
void mapSnapZoomOut(void)
{
#ifndef NO_EDITORS
	mapSnapDoc *doc = gMapSnapDoc;
	if(!doc)
		return;
	doc->padding = MIN(10000, doc->padding*1.5f);
	EditorPrefStoreFloat("MapSnap","Zoom","Padding",doc->padding);
	mapSnapSetupCamera(doc, doc->selected_partition);
#endif
}
#ifndef NO_EDITORS


#endif
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("MapSnap.ZoomIn");
void mapSnapZoomIn(void)
{
#ifndef NO_EDITORS
	mapSnapDoc *doc = gMapSnapDoc;
	if(!doc)
		return;
	doc->padding = MAX(10, doc->padding/1.5f);
	EditorPrefStoreFloat("MapSnap","Zoom","Padding",doc->padding);
	mapSnapSetupCamera(doc, doc->selected_partition);
#endif
}
#ifndef NO_EDITORS

//////////////////////////////////////////////////////////////////////////
// Utility Functions
//////////////////////////////////////////////////////////////////////////

static RoomPartition* mapSnapGetPartition(mapSnapPartition *map_snap_partition)
{
	int i,j;
	WorldRegion *region;
	ZoneMapLayer *layer;
	RoomConnGraph *room_conn_graph;

	if(!map_snap_partition)
		return NULL;

	layer = zmapGetLayerByName(NULL, map_snap_partition->layer_name);
	if(!layer)
		return NULL;

	region = layerGetWorldRegion(layer);
	if(!region)
		return NULL;

	room_conn_graph = worldRegionGetRoomConnGraph(region);
	if(!room_conn_graph)
		return NULL;

	for( i=0; i < eaSize(&room_conn_graph->rooms); i++ )
	{
		Room *room = room_conn_graph->rooms[i];
		for( j=0; j < eaSize(&room->partitions); j++ )
			if(stricmp(room->partitions[j]->zmap_scope_name, map_snap_partition->unique_name)==0)
				return room->partitions[j];
	}

	return NULL;
}

static bool mapSnapLockLayer(UIDialog *pDialog, UIDialogButton eButton, ZoneMapLayer *layer)
{
	if(eButton == kUIDialogButton_Ok)
		wleUISetLayerMode(layer, LAYER_MODE_EDITABLE, false);
	return true;
}

static bool mapSnapCheckLayerLocked(mapSnapDoc *doc)
{
	ZoneMapLayer *layer;
	mapSnapPartition *map_snap_partition = doc->selected_partition;
	const char *zmap_name = zmapInfoGetPublicName(NULL);

	if(!map_snap_partition || !zmap_name)
		return false;

	layer = zmapGetLayerByName(NULL, map_snap_partition->layer_name);
	if(!layer)
		return false;
	if(layerGetMode(layer) != LAYER_MODE_EDITABLE)
	{
		ui_WindowShow(UI_WINDOW(ui_DialogCreateEx("Unlock", "The layer for this partition is not yet locked, would you like to lock it?", mapSnapLockLayer, layer, NULL,
			"Cancel", kUIDialogButton_Cancel, "Lock", kUIDialogButton_Ok, NULL)));
		return false;
	}
	return true;
}

//////////////////////////////////////////////////////////////////////////
// Create/Destroy Data Functions
//////////////////////////////////////////////////////////////////////////

static void mapSnapPartitionDestroy(mapSnapPartition *map_snap_partition)
{
	if(map_snap_partition->buffer)
		free(map_snap_partition->buffer);
	StructDestroy(parse_mapSnapPartition, map_snap_partition);
}

//////////////////////////////////////////////////////////////////////////
// Init/Load/Fill Functions
//////////////////////////////////////////////////////////////////////////

static AtlasTex *mapSnapGetPartitionTex(mapSnapPartition *map_snap_partition, bool *does_not_take)
{
	RoomPartition *partition;
	partition = mapSnapGetPartition(map_snap_partition);
	if(partition)
	{
		if(does_not_take && partition->partition_data)
			(*does_not_take) = partition->partition_data->no_photo;

		if(eaSize(&partition->tex_list) == 1)
			return partition->tex_list[0];
		else
			return partition->overview_tex;
	}
	return NULL;
}

//Add to the MasterPartitionList all the partitions in a given region
static void mapSnapFillMasterPartitionList(const char *region_name, mapSnapDoc *doc)
{
	int i, j;
	char entry[64];
	WorldRegion *region;
	RoomConnGraph *room_conn_graph;

	if(!region_name)
		return;
	region = zmapGetWorldRegionByName(NULL, region_name);

	if(!region)
		return;
	room_conn_graph = worldRegionGetRoomConnGraph(region);

	if(!room_conn_graph)
		return;

	for( i=0; i < eaSize(&room_conn_graph->rooms); i++ )
	{
		int cnt=0;
		Room *room = room_conn_graph->rooms[i];
		for( j=0; j < eaSize(&room->partitions) ; j++ )
		{
			mapSnapPartition *map_snap_partition;

			if(nearSameVec3(room->partitions[j]->bounds_min, room->partitions[j]->bounds_max))
				continue;
			cnt++;

			map_snap_partition = StructCreate(parse_mapSnapPartition);
			if(eaSize(&room->partitions) > 1)
				sprintf(entry, "%s : Part %2d", room->def_name, cnt);
			else
				sprintf(entry, "%s", room->def_name);
			map_snap_partition->display_name = StructAllocString(entry);
			map_snap_partition->layer_name = StructAllocString(layerGetFilename(room->partitions[j]->parent_room->layer));
			map_snap_partition->unique_name = StructAllocString(room->partitions[j]->zmap_scope_name);
			map_snap_partition->region_name = region_name;
			copyVec3(room->partitions[j]->bounds_min, map_snap_partition->bounds_min);
			copyVec3(room->partitions[j]->bounds_mid, map_snap_partition->bounds_mid);
			copyVec3(room->partitions[j]->bounds_max, map_snap_partition->bounds_max);
			eaPush(&doc->master_partition_list, map_snap_partition);
		}
	}
}

//Init our list of region names and fill our MasterPartitionList
static void mapSnapGetRegionNames(mapSnapDoc *doc)
{
	int i;
	WorldRegion **region_list;

	region_list = worldGetAllWorldRegions();
	for( i=0; i < eaSize(&region_list) ; i++ )
	{
		if(!worldRegionIsEditorRegion(region_list[i]) &&
			worldRegionGetZoneMap(region_list[i]))
		{
			const char *name = worldRegionGetRegionName(region_list[i]);
			if(!name)
				name = allocAddString("Default");
			eaPush(&doc->region_names, name);
			mapSnapFillMasterPartitionList(name, doc);
		}
	}
}

static void mapSnapFillPartitionList(const char *region_name, mapSnapDoc *doc)
{
	int i;
	eaDestroy(&doc->current_partition_list);
	doc->current_partition_list = NULL;

	if(!region_name)
		return;
	for( i=0; i < eaSize(&doc->master_partition_list); i++ )
	{
		if(	doc->master_partition_list[i]->region_name == region_name ||
			(!doc->master_partition_list[i]->region_name && stricmp(region_name, "Default")==0))
			eaPush(&doc->current_partition_list, doc->master_partition_list[i]);
	}
}

static void mapSnapFillActionList(mapSnapPartition *map_snap_partition, mapSnapDoc *doc)
{
	int i;
	char action_name[64];
	RoomPartition *partition;

	eaDestroyEx(&doc->actions, StructFreeString);

	partition = mapSnapGetPartition(map_snap_partition);
	if(!partition || !partition->partition_data)
		return;

	for( i=0; i < eaSize(&partition->partition_data->actions); i++ )
	{
		RoomInstanceMapSnapAction *map_action = partition->partition_data->actions[i];
		F32 near_plane = map_action->near_plane + partition->bounds_min[1];
		F32 far_plane = map_action->far_plane + partition->bounds_min[1];
		if(nearSameVec2(map_action->min_sel, map_action->max_sel))
			sprintf(action_name, " F: %7.1f N: %7.1f", far_plane, near_plane);
		else if(map_action->action_type == MSNAP_Rectangle)
			sprintf(action_name, " F: %7.1f N: %7.1f Rectangle", far_plane, near_plane);
		else if(map_action->action_type == MSNAP_Ellipse)
			sprintf(action_name, " F: %7.1f N: %7.1f Ellipse", far_plane, near_plane);
		else if(map_action->action_type == MSNAP_Free)
			sprintf(action_name, " F: %7.1f N: %7.1f Polygon", far_plane, near_plane);
		else 
			sprintf(action_name, "ERROR");
		eaPush(&doc->actions, StructAllocString(action_name));
	}
}

//////////////////////////////////////////////////////////////////////////
// UI Callbacks
//////////////////////////////////////////////////////////////////////////

//Called when the Rectangle Selection button in the toolbar is clicked
static void mapSnapRectangleCB(UIButton *button, void *unused)
{
	mapSnapDoc *doc = gMapSnapDoc;
	if(!doc)
		return;

	doc->selection_state = MSNAP_Rectangle;

	mapSnapClearSelection(doc);

	EditorPrefStoreInt("MapSnap","ToolBar","SelectionState",doc->selection_state);
}

//Called when the Ellipse Selection button in the toolbar is clicked
static void mapSnapEllipseCB(UIButton *button, void *unused)
{
	mapSnapDoc *doc = gMapSnapDoc;
	if(!doc)
		return;

	doc->selection_state = MSNAP_Ellipse;

	mapSnapClearSelection(doc);

	EditorPrefStoreInt("MapSnap","ToolBar","SelectionState",doc->selection_state);
}

//Called when the Polygon Selection button in the toolbar is clicked
static void mapSnapFreeFormCB(UIButton *button, void *unused)
{
	mapSnapDoc *doc = gMapSnapDoc;
	if(!doc)
		return;

	doc->selection_state = MSNAP_Free;

	mapSnapClearSelection(doc);

	EditorPrefStoreInt("MapSnap","ToolBar","SelectionState",doc->selection_state);
}

static void mapSnapViewRegion(mapSnapDoc *doc)
{
	WorldRegion *region;
	int reg_idx = ui_ComboBoxGetSelected(doc->region_combo);
	Vec3 reg_min, reg_mid, reg_max;

	setVec2same(doc->position_offset, 0.0f);

	worldUpdateBounds(false, false);
	if(reg_idx >= 0 && (region = zmapGetWorldRegionByName(NULL, doc->region_names[reg_idx]))) {
		worldRegionGetBounds(region, reg_min, reg_max);
	} else {
		setVec2same(reg_min, 0.0f);
		setVec2same(reg_max, 0.0f);
	}

	addVec3(reg_min, reg_max, reg_mid);
	scaleVec3(reg_mid, 0.5f, reg_mid);
	
	copyVec3(reg_min, doc->reg_min);
	copyVec3(reg_mid, doc->reg_mid);
	copyVec3(reg_max, doc->reg_max);
	mapSnapSetupCameraFromMinMidMax(doc, reg_min, reg_mid, reg_max);

	ui_SliderTextEntrySetRange(doc->far_plane_slider, reg_min[1]-10, reg_max[1]+10, 0.1f);
	ui_SliderTextEntrySetValueAndCallback(doc->far_plane_slider, reg_min[1]-10);
	ui_SliderTextEntrySetRange(doc->near_plane_slider, reg_min[1]-10, reg_max[1]+10, 0.1f);
	ui_SliderTextEntrySetValueAndCallback(doc->near_plane_slider, reg_max[1]+10);
}

//Called when a region is selected from the combo box
static void mapSnapSelectRegionCB(UIComboBox *combo, mapSnapDoc *doc)
{
	const char *region_name = ui_ComboBoxGetSelectedObject(combo);
	bool veiw_whole_region = ui_CheckButtonGetState(doc->whole_region_check);
	if(!region_name)
		return;
	if(veiw_whole_region)
		mapSnapViewRegion(doc);
	mapSnapFillPartitionList(region_name, doc);
	doc->selected_partition = NULL;
	ui_ListSetSelectedRow(doc->room_list_box, -1);
}

//Called when a Partition is selected from the list
static void mapSnapSelectPartitionCB(UIList *list, mapSnapDoc *doc)
{
	bool reset_planes = true;
	mapSnapPartition *map_snap_partition = ui_ListGetSelectedObject(list);
	if(!map_snap_partition)
		return;

//	mapSnapClearSelection(doc);

	if(	doc->selected_partition &&
		doc->selected_partition->bounds_min[1] == map_snap_partition->bounds_min[1] &&
		doc->selected_partition->bounds_max[1] == map_snap_partition->bounds_max[1] )
		reset_planes = false;

	doc->selected_partition = map_snap_partition;
	setVec2same(doc->position_offset, 0.0f);

	mapSnapClearSelection(doc);

	mapSnapSetupCamera(doc, map_snap_partition);

	if(reset_planes)
	{
		ui_SliderTextEntrySetRange(doc->far_plane_slider, map_snap_partition->bounds_min[1]-10, map_snap_partition->bounds_max[1]+10, 0.1f);
		ui_SliderTextEntrySetValueAndCallback(doc->far_plane_slider, map_snap_partition->bounds_min[1]-10);
		ui_SliderTextEntrySetRange(doc->near_plane_slider, map_snap_partition->bounds_min[1]-10, map_snap_partition->bounds_max[1]+10, 0.1f);
		ui_SliderTextEntrySetValueAndCallback(doc->near_plane_slider, map_snap_partition->bounds_max[1]+10);
	}

	mapSnapFillActionList(map_snap_partition, doc);
	ui_ListSetSelectedRow(doc->action_list_box, -1);
// 	mapSnapDemandLoadTGAData(map_snap_partition);
// 	doc->picture_ui->texture = map_snap_partition->texture;

//	mapSnapRefreshFOWList(doc);
}

static void mapSnapGlobalNearPlaneCB(UISliderTextEntry *pSlider, bool bFinished, mapSnapDoc *doc);
static bool mapSnapSetGlobalNearPlane(ZoneMapInfo *zminfo, bool success, UISliderTextEntry *pSlider)
{
	PhotoOptions *photo_options = zmapInfoGetPhotoOptions(NULL, true);
	mapSnapDoc *doc = gMapSnapDoc;
	if (success && photo_options)
	{
		photo_options->near_plane_offset = ui_SliderTextEntryGetValue(pSlider);
		doc->last_near_offset = photo_options->near_plane_offset;
		zmapInfoSetModified(zminfo);
		emSetDocUnsaved(&doc->base_doc, false);
	}
	else
	{
		pSlider->cbChanged = NULL;
		ui_SliderTextEntrySetValueAndCallback(pSlider, doc->last_near_offset);
		pSlider->cbChanged = mapSnapGlobalNearPlaneCB;
	}
	return true;
}

//Called when the near place slider is changed
static void mapSnapGlobalNearPlaneCB(UISliderTextEntry *pSlider, bool bFinished, mapSnapDoc *doc)
{
	wleOpLockZoneMap(mapSnapSetGlobalNearPlane, pSlider);
}

static void mapSnapGlobalFarPlaneCB(UISliderTextEntry *pSlider, bool bFinished, mapSnapDoc *doc);
static bool mapSnapSetGlobalFarPlane(ZoneMapInfo *zminfo, bool success, UISliderTextEntry *pSlider)
{
	PhotoOptions *photo_options = zmapInfoGetPhotoOptions(NULL, true);
	mapSnapDoc *doc = gMapSnapDoc;
	if (success && photo_options)
	{
		photo_options->far_plane_offset = ui_SliderTextEntryGetValue(pSlider);
		doc->last_far_offset = photo_options->far_plane_offset;
		zmapInfoSetModified(zminfo);
		emSetDocUnsaved(&doc->base_doc, false);
	}
	else
	{
		pSlider->cbChanged = NULL;
		ui_SliderTextEntrySetValueAndCallback(pSlider, doc->last_far_offset);
		pSlider->cbChanged = mapSnapGlobalFarPlaneCB;
	}
	return true;
}

//Called when the far place slider is changed
static void mapSnapGlobalFarPlaneCB(UISliderTextEntry *pSlider, bool bFinished, mapSnapDoc *doc)
{
	wleOpLockZoneMap(mapSnapSetGlobalFarPlane, pSlider);
}

//Called when the near place slider is changed
static void mapSnapNearPlaneCB(UISliderTextEntry *pSlider, bool bFinished, mapSnapDoc *doc)
{
	doc->base_doc.editor->camera->ortho_near = doc->base_doc.editor->camera->camcenter[1] - ui_SliderTextEntryGetValue(pSlider);
}

//Called when the far place slider is changed
static void mapSnapFarPlaneCB(UISliderTextEntry *pSlider, bool bFinished, mapSnapDoc *doc)
{
	doc->base_doc.editor->camera->ortho_far = doc->base_doc.editor->camera->camcenter[1] - ui_SliderTextEntryGetValue(pSlider);
}

//Function for drawing a string inside a UIList
static void mapSnapDisplayListString(UIList *uiList, UIListColumn *col, UI_MY_ARGS, F32 z, CBox *pBox, int index, void *drawData)
{
	gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s", (char*)(*uiList->peaModel)[index]);
}

static void mapSnapSelectActionCB(UIList *list, mapSnapDoc *doc)
{
	int i, list_size;
	int idx = ui_ListGetSelectedRow(list);
	mapSnapPartition *map_snap_partition = doc->selected_partition;
	RoomPartition *partition;
	RoomInstanceMapSnapAction *action;
	F32 near_plane, far_plane;
	if(!map_snap_partition)
		return;
	partition = mapSnapGetPartition(map_snap_partition);
	if(!partition || !partition->partition_data)
		return;
	if(idx < 0 || idx >= eaSize(&partition->partition_data->actions))
		return;
	action = partition->partition_data->actions[idx];

	ui_ButtonComboSetSelected(gMapSnapToolbarSelectionButtonCombo, action->action_type);

	copyVec2(action->min_sel, doc->selection_min);
	copyVec2(action->max_sel, doc->selection_max);

	near_plane = action->near_plane + partition->bounds_min[1];
	far_plane = action->far_plane + partition->bounds_min[1];

	eafClear(&doc->point_list);
	list_size = eafSize(&action->points);
	for(i = 0; i < list_size; i++)
		eafPush(&doc->point_list, action->points[i]);
	doc->point_list_closed = true;

	ui_SliderTextEntrySetValueAndCallback(doc->near_plane_slider, near_plane);
	ui_SliderTextEntrySetValueAndCallback(doc->far_plane_slider, far_plane);
}

static void mapSnapMoveActionUpCB(UIButton *button, mapSnapDoc *doc)
{
	int idx;
	
	RoomInstanceData *room_inst_data = NULL;
	GroupTracker *layer_tracker = NULL;
	mapSnapPartition *map_snap_partition = doc->selected_partition;
	RoomPartition *partition = NULL;

	if(!mapSnapCheckLayerLocked(doc))
		return;

	partition = mapSnapGetPartition(map_snap_partition);
	if(!partition || !partition->tracker)
	{
		// TODO: does this happen any more?
		Alertf("Make sure you Save then InitMap before adding an action to a partition.");
		return;
	}

	layer_tracker = layerGetTracker(partition->tracker->parent_layer);
	if(!layer_tracker)
		return; // TODO: what does this mean?

	if(!partition->tracker->def->property_structs.room_properties)
		return; // TODO: consider making this an assert

	room_inst_data = partition->tracker->def->property_structs.room_properties->room_instance_data;
	if(!room_inst_data)
		return; // TODO: Should this be an assert?

	idx = ui_ListGetSelectedRow(doc->action_list_box);
	if(idx < 1 || idx >= eaSize(&room_inst_data->actions))
		return;

	layerSetUnsaved(partition->parent_room->layer, true);
	eaSwap(&room_inst_data->actions, idx, idx-1);

	groupdbDirtyDef(layer_tracker->def,-1);
	zmapTrackerUpdate(NULL, false, false);

	mapSnapFillActionList(map_snap_partition, doc);
	emSetDocUnsaved(&doc->base_doc, false);
	ui_ListSetSelectedRow(doc->action_list_box, idx-1);
}
static void mapSnapMoveActionDownCB(UIButton *button, mapSnapDoc *doc)
{
	int idx;
	
	RoomInstanceData *room_inst_data = NULL;
	GroupTracker *layer_tracker = NULL;
	mapSnapPartition *map_snap_partition = doc->selected_partition;
	RoomPartition *partition = NULL;

	if(!mapSnapCheckLayerLocked(doc))
		return;

	partition = mapSnapGetPartition(map_snap_partition);
	if(!partition || !partition->tracker)
	{
		// TODO: does this happen any more?
		Alertf("Make sure you Save then InitMap before adding an action to a partition.");
		return;
	}

	layer_tracker = layerGetTracker(partition->tracker->parent_layer);
	if(!layer_tracker)
		return; // TODO: what does this mean?

	if(!partition->tracker->def->property_structs.room_properties)
		return; // TODO: consider making this an assert

	room_inst_data = partition->tracker->def->property_structs.room_properties->room_instance_data;
	if(!room_inst_data)
		return; // TODO: Should this be an assert?

	idx = ui_ListGetSelectedRow(doc->action_list_box);
	if(idx < 0 || idx >= eaSize(&room_inst_data->actions)-1)
		return;
	layerSetUnsaved(partition->parent_room->layer, true);
	eaSwap(&room_inst_data->actions, idx, idx+1);

	groupdbDirtyDef(layer_tracker->def,-1);
	zmapTrackerUpdate(NULL, false, false);

	mapSnapFillActionList(map_snap_partition, doc);
	emSetDocUnsaved(&doc->base_doc, false);
	ui_ListSetSelectedRow(doc->action_list_box, idx+1);
}

static void mapSnapAddActionCB(UIButton *button, mapSnapDoc *doc)
{
	int i, list_size;
	RoomInstanceMapSnapAction *new_map_action = NULL;

	RoomInstanceData *room_inst_data = NULL;
	GroupTracker *layer_tracker = NULL;
	mapSnapPartition *map_snap_partition = doc->selected_partition;
	RoomPartition *partition = NULL;

	if(!mapSnapCheckLayerLocked(doc))
		return;

	partition = mapSnapGetPartition(map_snap_partition);
	if(!partition || !partition->tracker)
	{
		// TODO: does this happen any more?
		Alertf("Make sure you Save then InitMap before adding an action to a partition.");
		return;
	}

	layer_tracker = layerGetTracker(partition->tracker->parent_layer);
	if(!layer_tracker)
		return; // TODO: what does this mean?

	if(!partition->tracker->def->property_structs.room_properties)
		return; // TODO: consider making this an assert

	// Ensure the RoomInstanceData has been setup
	room_inst_data = partition->tracker->def->property_structs.room_properties->room_instance_data;
	if(!room_inst_data)
		room_inst_data = partition->tracker->def->property_structs.room_properties->room_instance_data = StructCreate(parse_RoomInstanceData);

	layerSetUnsaved(partition->parent_room->layer, true);

	new_map_action = StructCreate(parse_RoomInstanceMapSnapAction);
	new_map_action->action_type = doc->selection_state;
	copyVec2(doc->selection_min, new_map_action->min_sel);
	copyVec2(doc->selection_max, new_map_action->max_sel);

	// It seems somewhat virtuous to store these relative to the partition that contains them, though if the partition is moved or resized, it will be broken anyway
	new_map_action->near_plane = ui_SliderTextEntryGetValue(doc->near_plane_slider) - partition->bounds_min[1];
	new_map_action->far_plane = ui_SliderTextEntryGetValue(doc->far_plane_slider) - partition->bounds_min[1];
	
	eafClear(&new_map_action->points);
	list_size = eafSize(&doc->point_list);
	for(i = 0; i < list_size; i++)
		eafPush(&new_map_action->points, doc->point_list[i]);
	doc->point_list_closed = true;

	eaPush(&room_inst_data->actions, new_map_action);

	groupdbDirtyDef(layer_tracker->def, -1);
	zmapTrackerUpdate(NULL, false, false);

	mapSnapFillActionList(map_snap_partition, doc);
	emSetDocUnsaved(&doc->base_doc, false);
	ui_ListSetSelectedRow(doc->action_list_box, -1);
}

static void mapSnapRemoveActionCB(UIButton *button, mapSnapDoc *doc)
{
	int idx;

	RoomInstanceData *room_inst_data = NULL;
	GroupTracker *layer_tracker = NULL;
	mapSnapPartition *map_snap_partition = doc->selected_partition;
	RoomPartition *partition = NULL;

	if(!mapSnapCheckLayerLocked(doc))
		return;

	partition = mapSnapGetPartition(map_snap_partition);
	if(!partition || !partition->tracker)
	{
		// TODO: does this happen any more?
		Alertf("Make sure you Save then InitMap before adding an action to a partition.");
		return;
	}

	layer_tracker = layerGetTracker(partition->tracker->parent_layer);
	if(!layer_tracker)
		return; // TODO: what does this mean?

	if(!partition->tracker->def->property_structs.room_properties)
		return; // TODO: consider making this an assert

	room_inst_data = partition->tracker->def->property_structs.room_properties->room_instance_data;
	if(!room_inst_data)
		return; // TODO: Should this be an assert?

	idx = ui_ListGetSelectedRow(doc->action_list_box);
	if(idx < 0 || idx >= eaSize(&room_inst_data->actions))
		return;

	layerSetUnsaved(partition->parent_room->layer, true);
	StructDestroy(parse_RoomInstanceMapSnapAction, eaRemove(&room_inst_data->actions, idx));

	groupdbDirtyDef(layer_tracker->def,-1);
	zmapTrackerUpdate(NULL, false, false);

	mapSnapFillActionList(map_snap_partition, doc);
	emSetDocUnsaved(&doc->base_doc, false);
	ui_ListSetSelectedRow(doc->action_list_box, -1);
}

//Draw function for custom UIWidget mapSnapUIPicture
static void mapSnapUIPictureDraw(mapSnapUIPicture *pEntry, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(pEntry);
	UI_DRAW_EARLY(pEntry);

	if(pEntry->atlas_tex || pEntry->basic_tex)
	{
		F32 tex_w;
		F32 tex_h;

		if(pEntry->atlas_tex)
		{
			tex_w = pEntry->atlas_tex->width;
			tex_h = pEntry->atlas_tex->height;
		}
		else
		{
			tex_w = texWidth(pEntry->basic_tex);
			tex_h = texHeight(pEntry->basic_tex);
		}

		if(w > 0 && h > 0 && tex_w > 0 && tex_h > 0)
		{
			F32 w_scale = w / tex_w;
			F32 h_scale = h / tex_h;
			F32 tex_scale = MIN(w_scale, h_scale);
			F32 x_pos = x + (w/2.0f) - (w/2.0f) * (tex_scale/w_scale);
			F32 y_pos = y + (h/2.0f) - (h/2.0f) * (tex_scale/h_scale);

			if(pEntry->atlas_tex)
				display_sprite(pEntry->atlas_tex, x_pos, y_pos, z, tex_scale, tex_scale, 0xFFFFFFFF);
			else
				display_sprite_tex(pEntry->basic_tex, x_pos, y_pos, z, tex_scale, tex_scale, 0xFFFFFFFF);
		}
	}

	UI_DRAW_LATE(pEntry);
}

//Free function for custom UIWidget mapSnapUIPicture
static void mapSnapUIPictureFreeInternal(mapSnapUIPicture *pEntry)
{
	if(pEntry->basic_tex)
		texGenFreeNextFrame(pEntry->basic_tex);
	ui_WidgetFreeInternal(UI_WIDGET(pEntry));
}

static void mapSnapPreviewCB(UIButton *button, mapSnapDoc *doc)
{
	mapSnapPartition *map_snap_partition = doc->selected_partition;
	RoomPartition *partition;

	if(doc->room_photo_being_taken)
		return;
	if(!map_snap_partition)
		return;
	partition = mapSnapGetPartition(map_snap_partition);
	if(!partition)
		return;

	doc->action.headshot_tex = NULL;
	doc->room_photo_being_taken = gfxMakeMapPhoto(NULL, NULL, zmapGetWorldRegionByName(NULL, map_snap_partition->region_name), partition->bounds_min, partition->bounds_mid, partition->bounds_max, (partition->partition_data ? partition->partition_data->actions : NULL), layerGetFilename(partition->parent_room->layer), partition->parent_room->def_name, false, NULL);
	mapSnapUpdateRegion(doc->room_photo_being_taken->region);
	if(doc->room_photo_being_taken)
	{
		if(eaSize(&doc->room_photo_being_taken->cells) > 1)
			doc->room_photo_being_taken->pOverviewTextureBuffer = gfxMapSnapCreateTempSurface(MS_OV_IMAGE_SIZE, MS_OV_IMAGE_SIZE);

// 		doc->photo_idx = 0;
// 		doc->taking_photo = false;
		doc->room_photo_being_taken->started = false;
		doc->picture_frame_count = MAX_FRAMES_PER_PIC;
		mapSnapSetAllPanelsActive(doc, false);
	}
}

//////////////////////////////////////////////////////////////////////////
// Editor Manager Callbacks
//////////////////////////////////////////////////////////////////////////


static void mapSnapCloseDoc(EMEditorDoc *pDoc)
{
	mapSnapDoc *doc = (mapSnapDoc*)pDoc;
	if(!doc)
		return;

	EditUndoStackClear(doc->base_doc.edit_undo_stack);
	eaDestroy(&doc->region_names);
	eaDestroy(&doc->current_partition_list);
	eaDestroyEx(&doc->master_partition_list, mapSnapPartitionDestroy);
	//ShawnF TODO: need to free toolbars
	SAFE_FREE(pDoc);
	gMapSnapDoc = NULL;
}

void mapSnapMapChanged(void *unused, bool is_reset)
{
	PhotoOptions *photo_options = zmapInfoGetPhotoOptions(NULL, false);
	mapSnapDoc *doc = gMapSnapDoc;
	if(!doc)
		return;

	if(doc->g_near_plane_slider && doc->g_far_plane_slider)
	{
		doc->g_near_plane_slider->cbChanged = NULL;
		doc->g_far_plane_slider->cbChanged = NULL;
		if(photo_options)
		{
			doc->last_near_offset = photo_options->near_plane_offset;
			doc->last_far_offset = photo_options->far_plane_offset;
			ui_SliderTextEntrySetValueAndCallback(doc->g_near_plane_slider, photo_options->near_plane_offset);
			ui_SliderTextEntrySetValueAndCallback(doc->g_far_plane_slider, photo_options->far_plane_offset);
		}
		else
		{
			doc->last_near_offset = 0;
			doc->last_far_offset = 0;
			ui_SliderTextEntrySetValueAndCallback(doc->g_near_plane_slider, 0);
			ui_SliderTextEntrySetValueAndCallback(doc->g_far_plane_slider, 0);
		}
		doc->g_near_plane_slider->cbChanged = mapSnapGlobalNearPlaneCB;
		doc->g_far_plane_slider->cbChanged = mapSnapGlobalFarPlaneCB;
	}

	EditUndoStackClear(doc->base_doc.edit_undo_stack);
	eaDestroy(&doc->region_names);
	eaDestroy(&doc->current_partition_list);
	eaDestroyEx(&doc->master_partition_list, mapSnapPartitionDestroy);

	mapSnapGetRegionNames(doc);
	ui_ComboBoxSetSelectedAndCallback(doc->region_combo, 0);
}

//Called when a cell is finished on a preview image
static void mapSnapPreviewCellFinished(mapRoomPhoto *room_photo, mapRoomPhotoCell *cell, int idx)
{
}

//Compile Preview Image
static void mapSnapGetPreviewImage(mapSnapDoc *doc)
{
	mapRoomPhoto *room_photo = doc->room_photo_being_taken;
	if(!room_photo)
		return;

	assert(eaSize(&room_photo->cells) != 0);

	if(doc->picture_ui->basic_tex)
		texGenFreeNextFrame(doc->picture_ui->basic_tex);
	doc->picture_ui->basic_tex = NULL;


	if(eaSize(&room_photo->cells) == 1)
	{
		if(room_photo->cells[0]->raw_data)
		{
			doc->picture_ui->basic_tex = texGenNew(room_photo->cells[0]->w, room_photo->cells[0]->h, "MapSnapPreview", TEXGEN_NORMAL, WL_FOR_UI);
			texGenUpdate(doc->picture_ui->basic_tex, room_photo->cells[0]->raw_data, RTEX_2D, RTEX_BGRA_U8, 1, true, false, false, false);
		}
	}
	else
	{
		if(room_photo->ov_raw_data)
		{
			doc->picture_ui->basic_tex = texGenNew(MS_OV_IMAGE_SIZE, MS_OV_IMAGE_SIZE, "MapSnapPreview", TEXGEN_NORMAL, WL_FOR_UI);
			texGenUpdate(doc->picture_ui->basic_tex, room_photo->ov_raw_data, RTEX_2D, RTEX_BGRA_U8, 1, true, false, false, false);
		}
	}
}

static void mapSnapConvertScreenToWorldSpace(Vec2 screen_min, Vec2 screen_max, Vec3 room_min, Vec3 room_max, IVec2 screen_in, Vec3 *world_out)
{
	F32 world_w = room_max[0]-room_min[0];
	F32 world_h = room_max[2]-room_min[2];
	F32 screen_w = screen_max[0] - screen_min[0];
	F32 screen_h = screen_max[1] - screen_min[1];
	(*world_out)[0] = (screen_in[0]-screen_min[0])*(world_w/screen_w) + room_min[0];
	(*world_out)[2] = (screen_in[1]-screen_min[1])*(world_h/screen_h) + room_min[2];
}

//Once Per Frame Callback for Map Snap
static void mapSnapDraw(EMEditorDoc *doc_in)
{
	int i;
	static bool left_mouse_down = false;
	static bool right_mouse_down = false;
	mapSnapDoc *doc = (mapSnapDoc*)doc_in;
	mapSnapPartition *new_partition = NULL;
	bool view_whole_region = ui_CheckButtonGetState(doc->whole_region_check);

// 	if(!doc->taking_picture)
// 		mapSnapSetActiveFOWUI(doc);

	if(doc->room_photo_being_taken)
	{
		if(gfxMapPhotoProcess(doc->base_doc.editor->camera, doc->room_photo_being_taken, &doc->action, &doc->picture_frame_count, mapSnapPreviewCellFinished))
		{
			//Process Picture.
			gfxMapFinishOverviewPhoto(doc->room_photo_being_taken);
			mapSnapGetPreviewImage(doc);
			gfxMapRoomPhotoDestroy(doc->room_photo_being_taken);
			doc->room_photo_being_taken = NULL;

			mapSnapSetupCamera(doc, doc->selected_partition);
			mapSnapSetAllPanelsActive(doc, true);
		}
		else if(doc->picture_frame_count < 0)
		{
			//Stop taking picture
			if(doc->action.headshot_tex)
				gfxHeadshotRelease(doc->action.headshot_tex);
			doc->action.headshot_tex = NULL;
			gfxMapRoomPhotoDestroy(doc->room_photo_being_taken);
			doc->room_photo_being_taken = NULL;

			mapSnapSetupCamera(doc, doc->selected_partition);
			mapSnapSetAllPanelsActive(doc, true);
		}
	}
	else if(doc->selected_partition || view_whole_region)
	{
		int selected_action;
		Vec2 room_min, room_max;
		Vec2 room_size;
		Vec2 selection_min, selection_max;
		Color color;
		Vec2 min_vec, max_vec;
		Vec3 part_min, part_mid, part_max;

		if(doc->selected_partition) {
			copyVec3(doc->selected_partition->bounds_min, part_min);
			copyVec3(doc->selected_partition->bounds_mid, part_mid);
			copyVec3(doc->selected_partition->bounds_max, part_max);
		} else {
			copyVec3(doc->reg_min, part_min);
			copyVec3(doc->reg_mid, part_mid);
			copyVec3(doc->reg_max, part_max);
		}

		//Near and Far Plane Keys
		if(doc->key_near_add)
			ui_SliderTextEntrySetValueAndCallback(doc->near_plane_slider, ui_SliderTextEntryGetValue(doc->near_plane_slider)+MS_UI_SLIDER_SPEED/(doc->key_shift ? 10.0f : 1.0f));
		if(doc->key_near_sub)
			ui_SliderTextEntrySetValueAndCallback(doc->near_plane_slider, ui_SliderTextEntryGetValue(doc->near_plane_slider)-MS_UI_SLIDER_SPEED/(doc->key_shift ? 10.0f : 1.0f));
		if(doc->key_far_add)
			ui_SliderTextEntrySetValueAndCallback(doc->far_plane_slider, ui_SliderTextEntryGetValue(doc->far_plane_slider)+MS_UI_SLIDER_SPEED/(doc->key_shift ? 10.0f : 1.0f));
		if(doc->key_far_sub)
			ui_SliderTextEntrySetValueAndCallback(doc->far_plane_slider, ui_SliderTextEntryGetValue(doc->far_plane_slider)-MS_UI_SLIDER_SPEED/(doc->key_shift ? 10.0f : 1.0f));

		editLibGetScreenPosOthro(part_min, room_min);
		editLibGetScreenPosOthro(part_max, room_max);
		vec2MinMax(room_min, room_max, min_vec, max_vec);
		copyVec2(min_vec, room_min);
		copyVec2(max_vec, room_max);
		subVec2(room_max, room_min, room_size);

		if(room_size[0] != 0 && room_size[1] != 0)
		{
			S32 x, y;
			Vec2 pos;

			mousePos(&x, &y);

			pos[0] = ((F32)x-room_min[0])/room_size[0];
			pos[1] = ((F32)y-room_min[1])/room_size[1];
			pos[1] = 1.0f - pos[1];

			if(mouseClick(MS_LEFT) && !inpCheckHandled())
			{
				if(doc->selection_state == MSNAP_Free)
				{
					if(doc->point_list_closed)
					{
						eafClear(&doc->point_list);
						doc->point_list_closed = false;
					}

					if(eafSize(&doc->point_list) == 0)
					{
						doc->selection_min[0] = pos[0];
						doc->selection_min[1] = pos[1];
						doc->selection_max[0] = pos[0];
						doc->selection_max[1] = pos[1];
					}
					else
					{
						if(pos[0] < doc->selection_min[0])
							doc->selection_min[0] = pos[0];
						if(pos[0] > doc->selection_max[0])
							doc->selection_max[0] = pos[0];
						if(pos[1] < doc->selection_min[1])
							doc->selection_min[1] = pos[1];
						if(pos[1] > doc->selection_max[1])
							doc->selection_max[1] = pos[1];
					}

					eafPush2(&doc->point_list, pos);
				}
				inpHandled();
			}
			else if(mouseIsDown(MS_LEFT) && !inpCheckHandled())
			{
				if(doc->selection_state != MSNAP_Free)
				{
					if(!left_mouse_down)
					{
						doc->selection_min[0] = pos[0];
						doc->selection_min[1] = pos[1];
					}
					doc->selection_max[0] = pos[0];
					doc->selection_max[1] = pos[1];
				}
				left_mouse_down = true;
				inpHandled();
			}
			else if(left_mouse_down)
			{
				if(	(doc->selection_min[0] < 0.0f && doc->selection_max[0] < 0.0f) ||
					(doc->selection_min[0] > 1.0f && doc->selection_max[0] > 1.0f) ||
					(doc->selection_min[1] < 0.0f && doc->selection_max[1] < 0.0f) ||
					(doc->selection_min[1] > 1.0f && doc->selection_max[1] > 1.0f) )
				{
					int windowWidth, windowHeight;
					IVec2 pixel_pos;
					Vec3 world_pos;
					mousePos(&(pixel_pos[0]), &(pixel_pos[1]));
					gfxGetActiveDeviceSize(&windowWidth, &windowHeight);
					pixel_pos[1] = windowHeight - pixel_pos[1];
					mapSnapConvertScreenToWorldSpace(room_min, room_max, part_min, part_max, pixel_pos, &world_pos);

// 					world_pos[0] -= 2*doc->position_offset[0];
					world_pos[2] += 2*doc->position_offset[1];

					for( i=0; i < eaSize(&doc->current_partition_list) ; i++ )
					{
						world_pos[1] = doc->current_partition_list[i]->bounds_mid[1];
						if(boxSphereCollision(	doc->current_partition_list[i]->bounds_min, 
												doc->current_partition_list[i]->bounds_max,
												world_pos, 0))
						{
							new_partition = doc->current_partition_list[i];
							break;
						}
					}

					mapSnapClearSelection(doc);
				}
				left_mouse_down = false;
			}

			if(mouseIsDown(MS_RIGHT) && !inpCheckHandled())
			{
				static S32 last_x, last_y;

				if(doc->selection_state == MSNAP_Free)
					doc->point_list_closed = true;

				if(right_mouse_down)
				{
					doc->position_offset[0] -= (x-last_x)*(part_max[0]-part_min[0])/(room_max[0]-room_min[0]);
					doc->position_offset[1] += (y-last_y)*(part_max[2]-part_min[2])/(room_max[1]-room_min[1]);
					mapSnapSetupCameraFromMinMidMax(doc, part_min, part_mid, part_max);
				}
				right_mouse_down = true;
				last_x = x;
				last_y = y;
				inpHandled();
			}
			else
			{
				right_mouse_down = false;
			}

			//Draw bounding box
			color.g = color.a = 255.0f;
			color.r = color.b = 0.0f;
			gfxDrawBox(room_min[0], room_min[1], room_max[0], room_max[1], 0, color);

// 			//Draw Plus or Minus
// 			color.r = color.g = color.a = 255.0f;
// 			color.b = 0.0f;
// 			if(doc->sel_mode_sub || doc->sel_mode_add)
// 				gfxDrawLine(x+20, y-30, 3, x+40, y-30, color);
// 			if(doc->sel_mode_add)
// 				gfxDrawLine(x+30, y-20, 3, x+30, y-40, color);

			//Draw New Selection Outline
// 			if(doc->selection_state == MSNAP_Free)
// 			{
// 				for( i=2; i < eaiSize(&doc->point_list) ; i+=2 )
// 				{
// 					gfxDrawLine(doc->point_list[i-2], doc->point_list[i-1], 3, doc->point_list[i], doc->point_list[i+1], color);
// 				}
// 			}
// 			else if(left_mouse_down)
			{
				selection_min[0] = room_size[0]*(doc->selection_min[0]);
				selection_min[1] = room_size[1]*(1.0f-doc->selection_min[1]);
				addVec2(room_min, selection_min, selection_min);
				selection_max[0] = room_size[0]*(doc->selection_max[0]);
				selection_max[1] = room_size[1]*(1.0f-doc->selection_max[1]);

				addVec2(room_min, selection_max, selection_max);
				if(doc->selection_state == MSNAP_Rectangle)
					gfxDrawBox(selection_min[0], selection_min[1], selection_max[0], selection_max[1], 3, color);
				else if(doc->selection_state == MSNAP_Ellipse)
					gfxDrawEllipse(selection_min[0], selection_min[1], selection_max[0], selection_max[1], 3, 25, color);
				else
				{
					const int list_size = eafSize(&doc->point_list);
					if(list_size > 0)
					{
						Vec2 p0, pi;
						int j;

						p0[0] = room_size[0]*(doc->point_list[0]);
						p0[1] = room_size[1]*(1.0f-doc->point_list[1]);
						addVec2(room_min, p0, p0);

						copyVec2(p0, pi);

						for(j = 2; j < (list_size - 1); j += 2)
						{
							Vec2 pj;
							pj[0] = room_size[0]*(doc->point_list[j]);
							pj[1] = room_size[1]*(1.0f-doc->point_list[j + 1]);
							addVec2(room_min, pj, pj);

							gfxDrawLine(pi[0], pi[1], 3, pj[0], pj[1], color);

							copyVec2(pj, pi);
						}
						if(list_size > 4 && doc->point_list_closed)
							gfxDrawLine(pi[0], pi[1], 3, p0[0], p0[1], color);
						else if(!doc->point_list_closed)
							gfxDrawLine(pi[0], pi[1], 3, x, y, color);
					}
				}
			}

// 			//Draw Selection Mask
// 			if(doc->sel_tex)
// 			{
// 				display_sprite_tex(	doc->sel_tex, room_min[0], room_min[1], 3, 
// 									(room_max[0]-room_min[0])/texWidth(doc->sel_tex),
// 									(room_max[1]-room_min[1])/texHeight(doc->sel_tex),
// 									0x00000077);
// 			}

// 			//Draw Fog of War
// 			if(eaSize(&doc->selected_fow_data) > 0 && doc->selected_partition->texture)
// 			{
// 				const int * const *selected_rows;
// 				selected_rows = ui_ListGetSelectedRows(doc->fow_list);
// 				if(eaiSize(selected_rows) > 0 && (*selected_rows)[0] != -1)
// 				{
// 					for( i=0; i < eaiSize(selected_rows); i++ )
// 					{
// 						Vec3 tex_pos_world;
// 						Vec2 tex_pos_screen;
// 						mapSnapFOW *fow_data = doc->selected_fow_data[(*selected_rows)[i]];
// 						mapSnapConvertPixelToWorldSpace(doc->selected_partition->texture,
// 														doc->selected_partition->bounds_min, 
// 														doc->selected_partition->bounds_max, 
// 														fow_data->pixel_pos, &tex_pos_world);
// 						tex_pos_world[1] = doc->selected_partition->bounds_mid[1];
// 						editLibGetScreenPosOthro(tex_pos_world, tex_pos_screen);
// 						tex_pos_screen[1] = room_min[1] + (room_max[1]-tex_pos_screen[1]);
// 						display_sprite_tex(	fow_data->texture, tex_pos_screen[0], tex_pos_screen[1], 2, 
// 											(room_max[0]-room_min[0])/texWidth(doc->selected_partition->texture),
// 											(room_max[1]-room_min[1])/texHeight(doc->selected_partition->texture),
// 											0xFFFFFFFF);
// 					}
// 				}
// 			}

			//Draw Overlay Images
			if(doc->selected_partition)
			{
				if(doc->view_mode == MSNAP_Image)
				{
					AtlasTex *map_tex = mapSnapGetPartitionTex(doc->selected_partition, NULL);
					if(map_tex)
					{
						display_sprite(		map_tex, room_min[0], room_min[1], 1, 
							(room_max[0]-room_min[0])/(map_tex->width),
							(room_max[1]-room_min[1])/(map_tex->height),
							0xFFFFFFFF);
					}
				}
				else if(doc->view_mode == MSNAP_AllImages)
				{
					for( i=0; i < eaSize(&doc->current_partition_list); i++ )
					{
						bool does_not_take = false;
						AtlasTex *map_tex = mapSnapGetPartitionTex(doc->current_partition_list[i], &does_not_take);
						editLibGetScreenPosOthro(doc->current_partition_list[i]->bounds_min, room_min);
						editLibGetScreenPosOthro(doc->current_partition_list[i]->bounds_max, room_max);
						vec2MinMax(room_min, room_max, min_vec, max_vec);
						copyVec2(min_vec, room_min);
						copyVec2(max_vec, room_max);
						//					mapSnapDemandLoadTGAData(doc->current_partition_list[i]);
						if(map_tex)
						{
							display_sprite(		map_tex, room_min[0], room_min[1], 1, 
								(room_max[0]-room_min[0])/(map_tex->width),
								(room_max[1]-room_min[1])/(map_tex->height),
								0xFFFFFFFF);
						}
						else if(!does_not_take)
						{
							display_sprite(	white_tex_atlas, room_min[0], room_min[1], 2, 
								(room_max[0]-room_min[0]) / white_tex_atlas->width, 
								(room_max[1]-room_min[1]) / white_tex_atlas->height, 
								0xFF000077);
						}
					}
				}
			}
		}

		selected_action = ui_ListGetSelectedRow(doc->action_list_box);
		ui_SetActive(UI_WIDGET(doc->action_down_button), selected_action < eaSize(&doc->actions)-1 && selected_action >= 0);
		ui_SetActive(UI_WIDGET(doc->action_up_button), selected_action < eaSize(&doc->actions) && selected_action > 0);
		ui_SetActive(UI_WIDGET(doc->action_add_button), true);
		ui_SetActive(UI_WIDGET(doc->action_remove_button), selected_action >= 0);
	}
	else
	{
		ui_SetActive(UI_WIDGET(doc->action_up_button), false);
		ui_SetActive(UI_WIDGET(doc->action_down_button), false);
		ui_SetActive(UI_WIDGET(doc->action_add_button), false);
		ui_SetActive(UI_WIDGET(doc->action_remove_button), false);
	}

	if(new_partition)
	{
		ui_ListSetSelectedObjectAndCallback(doc->room_list_box, new_partition);
	}
}

static void mapSnapUpdateUI(mapSnapDoc *doc)
{
	bool veiw_whole_region = ui_CheckButtonGetState(doc->whole_region_check);

	eaClear(&doc->base_doc.em_panels);
	eaPush(&doc->base_doc.em_panels, doc->view_panel);
	if(!veiw_whole_region) {
		eaPush(&doc->base_doc.em_panels, doc->action_panel);
		eaPush(&doc->base_doc.em_panels, doc->global_panel);
		eaPush(&doc->base_doc.em_panels, doc->preview_panel);
	}

	ui_SetActive(UI_WIDGET(doc->room_list_box), !veiw_whole_region);
}

static void mapSnapViewWholeRegionCheckCB(UICheckButton *check_button, mapSnapDoc *doc)
{
	bool veiw_whole_region = ui_CheckButtonGetState(check_button);
	EditorPrefStoreInt("MapSnap", "View", "ViewWholeRegion", veiw_whole_region);

	if(veiw_whole_region)
	{
		doc->selected_partition = NULL;
		mapSnapViewRegion(doc);
		ui_ListSetSelectedRow(doc->room_list_box, -1);
	} else {
		ui_ListSetSelectedRowAndCallback(doc->room_list_box, 0);
	}

	mapSnapUpdateUI(doc);
}

//Create a new Doc
static EMEditorDoc *mapSnapNewDoc(const char *type, void *unused)
{
	char buf[256];
	PhotoOptions *photo_options = zmapInfoGetPhotoOptions(NULL, false);
	mapSnapDoc *new_doc = calloc(1, sizeof(*new_doc));
	EMToolbar *tool_bar;
	EMPanel *panel;
	UIComboBox *combo;
	UIListColumn *column;
	UIList *list;
	UICheckButton *check_button;
	UILabel *label;
	UIButton *button;
	UIButtonCombo *button_combo;
//	UITextEntry *entry;
	UISliderTextEntry *slider_text;
	mapSnapUIPicture *picture;
	int y = 0;

	gMapSnapDoc = new_doc;
	new_doc->base_doc.saved = 1;
	strcpy(new_doc->base_doc.doc_name, "Map Snap Plus");
	strcpy(new_doc->base_doc.doc_display_name, "Map Snap Plus");
	new_doc->base_doc.edit_undo_stack = EditUndoStackCreate();
	EditUndoSetContext(new_doc->base_doc.edit_undo_stack, new_doc);

	setVec3same(new_doc->reg_min, 0.0f);
	setVec3same(new_doc->reg_mid, 0.0f);
	setVec3same(new_doc->reg_max, 0.0f);

	//Init Lists
	mapSnapGetRegionNames(new_doc);

	new_doc->padding = EditorPrefGetFloat("MapSnap","Zoom","Padding",500.0f);

	//Toolbar
	if(!gMapSnapToolbar)
	{
		tool_bar = emToolbarCreate(0);
		gMapSnapToolbar = tool_bar;
		eaPush(&gMapSnap.toolbars, tool_bar);

		button_combo = ui_ButtonComboCreate(2, 2, 20, 20, POP_DOWN, false);
		gMapSnapToolbarSelectionButtonCombo = button_combo;
		ui_ButtonComboSetChangedCallback(button_combo, NULL, NULL);
		button = ui_ButtonComboAddItem(button_combo, "eui_select_rectangle.tga", NULL, NULL, mapSnapRectangleCB, NULL);
		sprintf(buf, "Rectangle Selection");
		ui_WidgetSetTooltipString(UI_WIDGET(button), buf);
		button = ui_ButtonComboAddItem(button_combo, "eui_select_ellipse.tga", NULL, NULL, mapSnapEllipseCB, NULL);
		sprintf(buf, "Ellipse Selection");
		ui_WidgetSetTooltipString(UI_WIDGET(button), buf);
 		button = ui_ButtonComboAddItem(button_combo, "eui_select_laso.tga", NULL, NULL, mapSnapFreeFormCB, NULL);
 		sprintf(buf, "Polygon Selection (right-click to close)");
 		ui_WidgetSetTooltipString(UI_WIDGET(button), buf);
		ui_ButtonComboSetActive(button_combo, true);
		emToolbarAddChild(tool_bar, button_combo, true);
	}
	new_doc->selection_state = EditorPrefGetInt("MapSnap","ToolBar","SelectionState",MSNAP_Rectangle);
	ui_ButtonComboSetSelected(gMapSnapToolbarSelectionButtonCombo, new_doc->selection_state);

	//Setup Panel
	panel = emPanelCreate("Map Snap Plus", "View", 0);
	new_doc->view_panel = panel;
	eaPush(&new_doc->base_doc.em_panels, panel);

	//Region Combo
	combo = ui_ComboBoxCreate(MS_UI_LEFT_OFFSET, y, 1, NULL, &new_doc->region_names, NULL);
	ui_ComboBoxSetSelectedCallback(combo, mapSnapSelectRegionCB, new_doc);
	ui_WidgetSetWidthEx(UI_WIDGET(combo), 1, UIUnitPercentage);
	emPanelAddChild(panel, combo, true);
	new_doc->region_combo = combo;
	y += UI_WIDGET(combo)->height + MS_UI_GAP;

	//Whole Region Check
	check_button = ui_CheckButtonCreate(MS_UI_LEFT_OFFSET, y, "View Whole Region", EditorPrefGetInt("MapSnap", "View", "ViewWholeRegion", 1));
	ui_CheckButtonSetToggledCallback(check_button, mapSnapViewWholeRegionCheckCB, new_doc);
	ui_WidgetSetWidthEx(UI_WIDGET(check_button), 1, UIUnitPercentage);
	emPanelAddChild(panel, check_button, true);
	new_doc->whole_region_check = check_button;
	y += UI_WIDGET(check_button)->height + MS_UI_GAP;

	//Partition List
	list = ui_ListCreate(parse_mapSnapPartition, &new_doc->current_partition_list, 15);
	ui_ListSetSelectedCallback(list, mapSnapSelectPartitionCB, new_doc);
	column = ui_ListColumnCreateParseName("Partition", "Name", NULL);
	ui_ListAppendColumn(list, column);
	ui_WidgetSetPosition(UI_WIDGET(list), MS_UI_LEFT_OFFSET, y);
	ui_WidgetSetHeight(UI_WIDGET(list), 150);
	ui_WidgetSetWidthEx(UI_WIDGET(list), 1, UIUnitPercentage);
	new_doc->room_list_box = list;
	emPanelAddChild(panel, list, true);
	y += UI_WIDGET(list)->height + MS_UI_GAP;
	//ui_ComboBoxSetSelectedAndCallback(new_doc->region_combo, 0);

	//Far Plane Slider
	label = ui_LabelCreate("Far Plane: ", MS_UI_LEFT_OFFSET, y);
	emPanelAddChild(panel, label, true);
	slider_text = ui_SliderTextEntryCreate("0", -50000.0f, 50000.0f, MS_UI_LEFT_OFFSET_2, y, 1);
	ui_WidgetSetPosition(UI_WIDGET(slider_text), MS_UI_LEFT_OFFSET_2, y);
	ui_WidgetSetWidthEx(UI_WIDGET(slider_text), 1, UIUnitPercentage);
	ui_SliderTextEntrySetPolicy(slider_text, UISliderContinuous);
	ui_SliderTextEntrySetChangedCallback(slider_text, mapSnapFarPlaneCB, new_doc);
	emPanelAddChild(panel, slider_text, true);
	new_doc->far_plane_slider = slider_text;
	y += UI_WIDGET(slider_text)->height + MS_UI_GAP;

	//Near Plane Slider
	label = ui_LabelCreate("Near Plane: ", MS_UI_LEFT_OFFSET, y);
	emPanelAddChild(panel, label, true);
	slider_text = ui_SliderTextEntryCreate("0", -50000.0f, 50000.0f, MS_UI_LEFT_OFFSET_2, y, 1);
	ui_WidgetSetPosition(UI_WIDGET(slider_text), MS_UI_LEFT_OFFSET_2, y);
	ui_WidgetSetWidthEx(UI_WIDGET(slider_text), 1, UIUnitPercentage);
	ui_SliderTextEntrySetPolicy(slider_text, UISliderContinuous);
	ui_SliderTextEntrySetChangedCallback(slider_text, mapSnapNearPlaneCB, new_doc);
	emPanelAddChild(panel, slider_text, true);
	new_doc->near_plane_slider = slider_text;
	y += UI_WIDGET(slider_text)->height + MS_UI_GAP;

	y += MS_UI_GAP;

	//Setup Panel
	panel = emPanelCreate("Map Snap Plus", "Actions", 0);
	new_doc->action_panel = panel;
	eaPush(&new_doc->base_doc.em_panels, panel);
	y=0;

	//Action List
	list = ui_ListCreate(NULL, &new_doc->actions, 15);
	ui_ListSetSelectedCallback(list, mapSnapSelectActionCB, new_doc);
	column = ui_ListColumnCreateCallback("Action", mapSnapDisplayListString, NULL);
	ui_ListAppendColumn(list, column);
	ui_WidgetSetPosition(UI_WIDGET(list), MS_UI_LEFT_OFFSET, y);
	ui_WidgetSetHeight(UI_WIDGET(list), 150);
	ui_WidgetSetWidthEx(UI_WIDGET(list), 1, UIUnitPercentage);
	new_doc->action_list_box = list;
	emPanelAddChild(panel, list, true);
	y += UI_WIDGET(list)->height + MS_UI_GAP;

	//Move Up
	button = ui_ButtonCreate("Up", MS_UI_LEFT_OFFSET*2, y, mapSnapMoveActionUpCB, new_doc);
	ui_WidgetSetWidthEx(UI_WIDGET(button), 0.45, UIUnitPercentage);
	new_doc->action_up_button = button;
	emPanelAddChild(panel, button, true);

	//Move Down
	button = ui_ButtonCreate("Down", MS_UI_LEFT_OFFSET, y, mapSnapMoveActionDownCB, new_doc);
	ui_WidgetSetPositionEx(UI_WIDGET(button), MS_UI_LEFT_OFFSET, y, 0, 0, UITopRight);
	ui_WidgetSetWidthEx(UI_WIDGET(button), 0.45, UIUnitPercentage);
	new_doc->action_down_button = button;
	emPanelAddChild(panel, button, true);
	y += UI_WIDGET(button)->height + MS_UI_GAP;

	//Add
	button = ui_ButtonCreate("Add", MS_UI_LEFT_OFFSET*2, y, mapSnapAddActionCB, new_doc);
	ui_WidgetSetWidthEx(UI_WIDGET(button), 0.45, UIUnitPercentage);
	new_doc->action_add_button = button;
	emPanelAddChild(panel, button, true);

	//Remove
	button = ui_ButtonCreate("Remove", MS_UI_LEFT_OFFSET, y, mapSnapRemoveActionCB, new_doc);
	ui_WidgetSetPositionEx(UI_WIDGET(button), MS_UI_LEFT_OFFSET, y, 0, 0, UITopRight);
	ui_WidgetSetWidthEx(UI_WIDGET(button), 0.45, UIUnitPercentage);
	new_doc->action_remove_button = button;
	emPanelAddChild(panel, button, true);
	y += UI_WIDGET(button)->height + MS_UI_GAP;

	//Zone Map Panel
	panel = emPanelCreate("Map Snap Plus", "Global Options", 0);
	new_doc->global_panel = panel;
	eaPush(&new_doc->base_doc.em_panels, panel);
	y=0;

	//Zone Map Far Plane Slider
	label = ui_LabelCreate("Far Offset: ", MS_UI_LEFT_OFFSET, y);
	emPanelAddChild(panel, label, true);
	slider_text = ui_SliderTextEntryCreate("0", 0, 1000.0f, MS_UI_LEFT_OFFSET_2, y, 1);
	ui_WidgetSetPosition(UI_WIDGET(slider_text), MS_UI_LEFT_OFFSET_2, y);
	ui_WidgetSetWidthEx(UI_WIDGET(slider_text), 1, UIUnitPercentage);
//	ui_SliderTextEntrySetPolicy(slider_text, UISliderContinuous);
	ui_SliderTextEntrySetChangedCallback(slider_text, mapSnapGlobalFarPlaneCB, new_doc);
	emPanelAddChild(panel, slider_text, true);
	new_doc->g_far_plane_slider = slider_text;
	if(photo_options)
	{
		new_doc->last_far_offset = photo_options->far_plane_offset;
		ui_SliderTextEntrySetValue(slider_text, photo_options->far_plane_offset);
	}
	y += UI_WIDGET(slider_text)->height + MS_UI_GAP;

	//Zone Map Near Plane Slider
	label = ui_LabelCreate("Near Offset: ", MS_UI_LEFT_OFFSET, y);
	emPanelAddChild(panel, label, true);
	slider_text = ui_SliderTextEntryCreate("0", 0, 1000.0f, MS_UI_LEFT_OFFSET_2, y, 1);
	ui_WidgetSetPosition(UI_WIDGET(slider_text), MS_UI_LEFT_OFFSET_2, y);
	ui_WidgetSetWidthEx(UI_WIDGET(slider_text), 1, UIUnitPercentage);
//	ui_SliderTextEntrySetPolicy(slider_text, UISliderContinuous);
	ui_SliderTextEntrySetChangedCallback(slider_text, mapSnapGlobalNearPlaneCB, new_doc);
	emPanelAddChild(panel, slider_text, true);
	new_doc->g_near_plane_slider = slider_text;
	if(photo_options)
	{
		new_doc->last_near_offset = photo_options->near_plane_offset;
		ui_SliderTextEntrySetValue(slider_text, photo_options->near_plane_offset);
	}
	y += UI_WIDGET(slider_text)->height + MS_UI_GAP;

	//Image Panel
	panel = emPanelCreate("Map Snap Plus", "Preview", 0);
	new_doc->preview_panel = panel;
	eaPush(&new_doc->base_doc.em_panels, panel);
	y=0;

	//Take Image Button
	button = ui_ButtonCreate("Snap", MS_UI_LEFT_OFFSET, y, mapSnapPreviewCB, new_doc);
	ui_WidgetSetWidthEx(UI_WIDGET(button), 1, UIUnitPercentage);
	emPanelAddChild(panel, button, true);
	y += UI_WIDGET(button)->height + MS_UI_GAP;

	//Preview Image
	picture = calloc(1, sizeof(mapSnapUIPicture));
	ui_WidgetInitialize(UI_WIDGET(picture), NULL, mapSnapUIPictureDraw, mapSnapUIPictureFreeInternal, NULL, NULL);
	ui_WidgetSetPosition(UI_WIDGET(picture), MS_UI_LEFT_OFFSET, y);
	ui_WidgetSetHeight(UI_WIDGET(picture), 300);
	ui_WidgetSetWidthEx(UI_WIDGET(picture), 1, UIUnitPercentage);
	new_doc->picture_ui = picture;
	emPanelAddChild(panel, picture, true);

// 	//Fog of War Panel
// 	panel = emPanelCreate("Map Snap Plus", "Fog of War", 0);
// 	new_doc->fow_panel = panel;
// 	eaPush(&new_doc->base_doc.em_panels, panel);
// 	y=0;
// 
// 	//Section List
// 	list = ui_ListCreate(parse_mapSnapFOW, &new_doc->selected_fow_data, 15);
// 	ui_ListSetMultiselect(list, true);
// 	ui_ListSetSelectedCallback(list, mapSnapFOWSelectedCB, new_doc);
// 	column = ui_ListColumnCreateParseName("Fog of War", "DisplayName", NULL);
// 	ui_ListAppendColumn(list, column);
// 	ui_WidgetSetPosition(UI_WIDGET(list), MS_UI_LEFT_OFFSET, y);
// 	ui_WidgetSetHeight(UI_WIDGET(list), 150);
// 	ui_WidgetSetWidthEx(UI_WIDGET(list), 1, UIUnitPercentage);
// 	new_doc->fow_list = list;
// 	emPanelAddChild(panel, list, true);
// 	y += UI_WIDGET(list)->height + MS_UI_GAP;
// 
// 	//New Section
// 	button = ui_ButtonCreate("Cut", MS_UI_LEFT_OFFSET, y, mapSnapCutCB, new_doc);
// 	ui_WidgetSetWidthEx(UI_WIDGET(button), 0.45, UIUnitPercentage);
// 	emPanelAddChild(panel, button, true);
// 
// 	//Remove Section
// 	button = ui_ButtonCreate("Merge", MS_UI_LEFT_OFFSET, y, mapSnapMergeCB, new_doc);
// 	ui_WidgetSetPositionEx(UI_WIDGET(button), MS_UI_LEFT_OFFSET, y, 0, 0, UITopRight);
// 	ui_WidgetSetWidthEx(UI_WIDGET(button), 0.45, UIUnitPercentage);
// 	emPanelAddChild(panel, button, true);
// 	y += UI_WIDGET(button)->height + MS_UI_GAP;
// 
// 	y += MS_UI_GAP;
// 
// 	//Name
// 	label = ui_LabelCreate("Name: ", MS_UI_LEFT_OFFSET, y);
// 	emPanelAddChild(panel, label, true);
// 	entry = ui_TextEntryCreate("", MS_UI_LEFT_OFFSET_2, y);
// 	ui_WidgetSetWidthEx(UI_WIDGET(entry), 1, UIUnitPercentage);
// 	new_doc->fow_name_entry = entry;
// 	emPanelAddChild(panel, entry, true);
// 	y += UI_WIDGET(entry)->height + MS_UI_GAP;

	mapSnapUpdateUI(new_doc);
	return &new_doc->base_doc;
}

static EMTaskStatus mapSnapSave(EMEditorDoc *doc_in)
{
	mapSnapDoc *doc = (mapSnapDoc*)doc_in;
	EMTaskStatus ret = EM_TASK_SUCCEEDED;

	wleOpSave();

	return ret;
}

//Register Map Snap 
#endif
AUTO_RUN;
void mapSnapRegister(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return;

	// Editor Setup
	strcpy(gMapSnap.editor_name, "Map Snap Plus");
	gMapSnap.type = EM_TYPE_SINGLEDOC;
	gMapSnap.hide_world = 0;
	gMapSnap.force_editor_cam = 1;
	gMapSnap.allow_save = 1;
	gMapSnap.allow_multiple_docs = 0;
	gMapSnap.always_open = 1;
	gMapSnap.disable_auto_checkout = 1;
	gMapSnap.disable_single_doc_menus = 1;
	gMapSnap.hide_file_toolbar = 1;
	gMapSnap.default_type = "mapsnap";
	strcpy(gMapSnap.default_workspace, "Environment Editors");

	// Registering Callbacks
	gMapSnap.new_func = mapSnapNewDoc;
	gMapSnap.close_func = mapSnapCloseDoc;
	gMapSnap.save_func = mapSnapSave;
	gMapSnap.draw_func = mapSnapDraw;

	gMapSnap.keybinds_name = "MapSnap";
	gMapSnap.keybind_version = 1;

	emAddMapChangeCallback(mapSnapMapChanged, NULL);

	// Registering Editor
	emRegisterEditor(&gMapSnap);
	emRegisterFileType("mapsnap", "MapSnap", gMapSnap.editor_name);
#endif  //For NO_EDITORS
	return;
}
