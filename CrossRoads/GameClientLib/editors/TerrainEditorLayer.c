#ifndef NO_EDITORS

#include "TerrainEditorPrivate.h"
#include "WorldEditorUI.h"
#include "WorldEditorOperations.h"

#include "ControllerScriptingSupport.h"
#include "EditLibUIUtil.h"
#include "error.h"
#include "estring.h"
#include "FolderCache.h"
#include "ThreadManager.h"
#include "partition_enums.h"
#include "ProgressOverlay.h"
#include "Color.h"
#include "GfxTerrain.h"
#include "WorldEditorClientMain.h"
#include "wlTerrainQueue.h"
#include "WorldCellClustering.h"
#include "WorldLibEnums.h"
#include "GenericMesh.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););
AUTO_RUN_ANON(memBudgetAddMapping("ThreadStack:terrainEditorSourceThreadProc", BUDGET_Editors););

extern ParseTable parse_TerrainExportInfo[];
#define TYPE_parse_TerrainExportInfo TerrainExportInfo

static ManagedThread **load_threads;

int terEdGetMaxLOD(TerrainDoc *doc);

static DWORD WINAPI terrainEditorSourceThreadProc( LPVOID lpParam )
{
	char buf[64];
	U32 progress_id;
	TerrainEditorSourceLayer *layer = (TerrainEditorSourceLayer*)lpParam;
	sprintf(buf, "Loading %s...", layerGetName(layer->layer));
	progress_id = progressOverlayCreate(eaSize(&layer->layer->terrain.blocks), buf);
    terrainSourceLoadLayerData(layer, true, true, progressOverlayIncrement, progress_id);
	progressOverlayRelease(progress_id);
    return 0;
}

static void doLoadThreadCheck(void)
{
	int i;
	for (i = eaSize(&load_threads)-1; i >= 0; --i)
	{
		if (!tmIsStillActive(load_threads[i]))
		{
			tmDestroyThread(load_threads[i], false);
			eaRemoveFast(&load_threads, i);
		}
	}
}

static void doKillLoadingThreads(void)
{
	int i;
	for (i = eaSize(&load_threads)-1; i >= 0; --i)
	{
		tmDestroyThread(load_threads[i], true);
		eaRemoveFast(&load_threads, i);
	}
}

bool terrainLoadSourceLayer(TerrainDoc *doc, ZoneMapLayer *layer, bool asynchronous)
{
	ManagedThread *t = NULL;

	assert(layer->terrain.source_data);

	if (layer->zmap_parent->map_info.genesis_data)
	{
		doc->state.genesis_preview_flag = true;
	}

	if (layer->terrain.source_data->loaded)
		return true;

	if (asynchronous)
		t = tmCreateThread(terrainEditorSourceThreadProc, layer->terrain.source_data);
	if (t)
		eaPush(&load_threads, t);
	else
		terrainSourceLoadLayerData(layer->terrain.source_data, true, true, NULL, 0); // load in foreground if thread did not start
   
	doLoadThreadCheck();

    return true;
}

bool terrainUnloadSourceLayer(TerrainDoc *doc, TerrainEditorSourceLayer *layer)
{
	terrainSourceUnloadLayerData(layer);
	return true;
}

void terrainEditorUpdateSourceDataFilenames(ZoneMapLayer *layer)
{
	int blocknum;
	TerrainEditorSourceLayer *source_layer;

	if (!layer->terrain.source_data)
		return;
	source_layer = layer->terrain.source_data;

	for (blocknum = 0; blocknum < eaSize(&source_layer->blocks); blocknum++)
        layerTerrainUpdateFilenames(layer, source_layer->blocks[blocknum]);
}

bool terrainEditorSaveSourceData(ZoneMapLayer *layer, bool force, bool asynchronous)
{
	if (layer->layer_mode != LAYER_MODE_EDITABLE || !layer->terrain.source_data)
		return false;
	if (!force && !layer->terrain.unsaved_changes)
		return true;
	layer->saving = true;
	terrainSourceBeginSaveLayer(layer->terrain.source_data);
	terrainQueueSave(terEdGetDoc()->state.task_queue, layer->terrain.source_data, force, TERRAIN_TASK_SHOW_PROGRESS | TERRAIN_TASK_UNDOABLE);
	EditUndoStackClear(terEdGetDoc()->state.undo_stack);
	if (!asynchronous)
		terEdWaitForQueuedEvents(terEdGetDoc());
	return true;
}

bool terrainEditorAddSourceData(ZoneMapLayer *layer, IVec2 min, IVec2 max)
{
	if (terrainSourceCreateBlock(PARTITION_CLIENT, layer->terrain.source_data, min, max, true, true))
	{
		TerrainDoc *doc = terEdGetDoc();
		if (doc->state.source->editing_lod == -1)
			doc->state.source->editing_lod = HEIGHTMAP_DEFAULT_LOD;
		terrainSourceLayerResample(layer->terrain.source_data, doc->state.source->editing_lod);
		terrainSourceSetVisibleLOD(doc->state.source, doc->state.source->editing_lod);
		terEdUpdateLayerButtons(doc);
		return true;
	}
	return false;
}

void terrainUpdateTerrainObjectsByDef(GroupDef *changed_def)
{
	TerrainDoc *doc = terEdGetDoc();
	if (doc && doc->state.source)
	{
		int i;
		for (i = 0; i < eaSize(&doc->state.source->layers); i++)
		{
			terrainSourceUpdateObjectsByDef(doc->state.source->layers[i], changed_def);
		}
	}
}

bool terEdCheckForLayerUpdates(TerrainDoc *doc)
{
	int i;
    bool editable = false;
	WorldRegion **regions_to_reload = NULL;
    for (i = 0; i < eaSize(&doc->state.source->layers); i++)
    {
        TerrainEditorSourceLayer *layer = doc->state.source->layers[i];

		if (layer->layer->layer_mode != layer->effective_mode)
		{
			switch (layer->layer->layer_mode)
			{
			xcase LAYER_MODE_GROUPTREE:
				terrainUnloadSourceLayer(doc, layer);
				//terEdUpdateSubDocs(doc);
				terEdUpdateLayerButtons(doc);
				layer->effective_mode = layer->layer->layer_mode;

			xcase LAYER_MODE_TERRAIN:
				if (layer->loaded)
				{
					if (layer->effective_mode < LAYER_MODE_TERRAIN)
						terrainSourceFinishLoadLayerData(PARTITION_CLIENT, layer);		                
					layer->effective_mode = layer->layer->layer_mode;
					if (doc->state.source->visible_lod == HEIGHTMAP_NO_LOD)
						terrainSourceSetVisibleLOD(doc->state.source, layer->loaded_lod);
					terrainSourceSetLOD(layer, doc->state.source->visible_lod);
				}

			xcase LAYER_MODE_EDITABLE:
				if (layer->loaded)
				{
					if (layer->effective_mode < LAYER_MODE_TERRAIN)
						terrainSourceFinishLoadLayerData(PARTITION_CLIENT, layer);
					layer->effective_mode = layer->layer->layer_mode;
					terrainFinishAssociateLayer(doc, layer);
					//terEdUpdateSubDocs(doc);
					terEdUpdateLayerButtons(doc);
				}
			};
		}
    }
	for (i = 0; i < eaSize(&regions_to_reload); ++i)
	{
		ZoneMap *zmap = worldGetPrimaryMap();
		char header_filename[MAX_PATH];
		char client_base_dir[MAX_PATH];
		BinFileList *file_list = StructAlloc(parse_BinFileList);
		worldGetClientBaseDir(zmapGetFilename(zmap), SAFESTR(client_base_dir));
		sprintf(header_filename, "%s/terrain_%s_deps.bin", client_base_dir, regions_to_reload[i]->name ? regions_to_reload[i]->name : "Default");
		if (ParserOpenReadBinaryFile(NULL, header_filename, parse_BinFileList, file_list, NULL, NULL, NULL, NULL, 0, 0, 0))
			terrainLoadRegionAtlases(regions_to_reload[i], file_list);
		StructDestroySafe(parse_BinFileList, &file_list);
	}
	eaDestroy(&regions_to_reload);
    return editable;
}

//// Layer Management routines

void terrainFinishAssociateLayer(TerrainDoc *doc, TerrainEditorSourceLayer *source)
{
	terEdGetMaxLOD(doc);
	terrainSourceSetVisibleLOD(doc->state.source, doc->state.source->visible_lod);
	source->effective_mode = LAYER_MODE_EDITABLE;
	if (source->layer->controller_script_wait_for_edit)
	{
		ControllerScript_Succeeded();
		source->layer->controller_script_wait_for_edit = false;
	}
}

//// Layer management UI

void terEdHighlightBlocks(HeightMapTracker *tracker)
{
	ZoneMapLayer *layer = heightMapGetLayer(tracker->height_map);
    TerrainDoc *doc = terEdGetDoc();
    if (doc)
    {
        int i, j;
		ZoneMapLayer **layer_list = NULL;
		emPanelsGetMapSelectedLayers(&layer_list);
        for (i = 0; i < eaSize(&doc->state.source->layers); i++)
        {
            TerrainEditorSourceLayer *source_layer = doc->state.source->layers[i]; 
            if (layer == source_layer->layer && source_layer->loaded)
            {
                int block;
                int edges[] = { 0, 0, 0, 0 };
                ZoneMapLayer *neighbor_layers[] = { NULL, NULL, NULL, NULL };
                int neighbor_idxs[4];
                int color = 3;
                if (source_layer->effective_mode != LAYER_MODE_EDITABLE)
                    color = 2;
                else
                {
                    if (doc && doc->state.lock_edges)
                    {
                        terrainSourceGetNeighborLayers(doc->state.source, tracker->height_map, neighbor_layers, neighbor_idxs, false);
                        for (j = 0; j < 4; j++)
	                        if (neighbor_layers[j] && neighbor_layers[j]->layer_mode == LAYER_MODE_EDITABLE)
    	                        neighbor_layers[j] = NULL;
                    }
					if (eaFind(&layer_list, layer) != -1)
						color = 1;
                }
                        
                for (block = 0; block < eaSize(&source_layer->blocks); block++)
                {
                    IVec2 min, max;
                    terrainBlockGetExtents(source_layer->blocks[block], min, max);
                    if (tracker->world_pos[1] >= min[1] && tracker->world_pos[1] <= max[1] && 
                        tracker->world_pos[0] == min[0]) edges[1] = 1;
                    if (tracker->world_pos[0] >= min[0] && tracker->world_pos[0] <= max[0] && 
                        tracker->world_pos[1] == min[1]) edges[0] = 1;
                    if (tracker->world_pos[1] >= min[1] && tracker->world_pos[1] <= max[1] && 
                        tracker->world_pos[0] == max[0]) edges[3] = 1;
                    if (tracker->world_pos[0] >= min[0] && tracker->world_pos[0] <= max[0] && 
                        tracker->world_pos[1] == max[1]) edges[2] = 1;
                }
                if (!doc->state.hide_edges && doc->state.has_focus)
                {
                    heightMapSetVisibleEdges(tracker->height_map, 
                                             neighbor_layers[1] ? 4 : edges[0]*color, 
                                             neighbor_layers[3] ? 4 : edges[1]*color,
                                             neighbor_layers[0] ? 4 : edges[2]*color,
                                             neighbor_layers[2] ? 4 : edges[3]*color);
                }
                else
                {
                    heightMapSetVisibleEdges(tracker->height_map, 0, 0, 0, 0);	
                }
				eaDestroy(&layer_list);
                return;
            }
        }
		eaDestroy(&layer_list);
	}
	heightMapSetVisibleEdges(tracker->height_map, 0, 0, 0, 0);
}

void terrainEditorLayerModeChanged(ZoneMapLayer *layer, ZoneMapLayerMode mode, bool asynchronous)
{
	TerrainDoc *doc = terEdGetDoc();

	// Scratch layer should never load terrain
	if (!doc || layer->scratch)
		return;

	if (mode >= LAYER_MODE_TERRAIN)
	{
		// Need source
		if (!doc->state.source)
			doc->state.source = terrainSourceInitialize();
		if (!layer->terrain.source_data)
		{
			layer->terrain.source_data = terrainSourceAddLayer(doc->state.source, layer);
		}
		if (!layer->terrain.source_data->loaded)
		{
			terrainLoadSourceLayer(doc, layer, asynchronous);
		}
	}
	else
	{
		// Don't need source
		if (layer->terrain.source_data)
		{
			assert(!layer->saving);
			// Make sure we're not loading source
			doKillLoadingThreads();
			// Make sure we don't have anything in our queue
			terrainQueueClear(doc->state.task_queue);
			terrainQueueClear(doc->state.task_queue);
			terEdWaitForQueuedEvents(doc);
			terrainSourceUnloadLayerData(layer->terrain.source_data);
		}
	}
}

//// Layer selection

TerrainEditorSourceLayer *terEdGetSelectedLayer(TerrainDoc *doc)
{
	ZoneMapLayer **layer_list = NULL;
	TerrainEditorSourceLayer *ret;
	emPanelsGetMapSelectedLayers(&layer_list);
    if (eaSize(&layer_list) != 1)
    {
		eaDestroy(&layer_list);
        Alertf("Only one layer can be selected for that operation.");
        return NULL;
    }
	ret = layer_list[0]->terrain.source_data;
	eaDestroy(&layer_list);
	return ret;
}

//// Layer Granularity 

static char *granularities[] = { "1 ft", "2 ft", "4 ft", "8 ft", "16 ft", "32 ft", "64 ft", "128 ft", "256 ft" };

int terEdGetMaxLOD(TerrainDoc *doc)
{
    int i, max_lod = -1;
    for (i = 0; i < eaSize(&doc->state.source->layers); i++)
        if (doc->state.source->layers[i]->loaded &&
            (doc->state.source->layers[i]->loaded_lod != HEIGHTMAP_NO_LOD &&
             doc->state.source->layers[i]->loaded_lod > max_lod))
            max_lod = doc->state.source->layers[i]->loaded_lod;
    doc->state.source->editing_lod = max_lod;
    return max_lod;
}

void terEdLayerPlayable(UICheckButton *check, void *unused)
{
	TerrainDoc *doc = terEdGetDoc();
	if (doc)
	{	
        TerrainEditorSourceLayer *layer = terEdGetSelectedLayer(doc);
        if (layer)
        {
            layer->playable = ui_CheckButtonGetState(check);
			terrainSourceLayerSetUnsaved(layer);
			wleSetDocSavedBit();
        }
    }
}

void terEdLayerExclusionVersion(UIComboBox *combo, TerrainExclusionVersion version, void *unused)
{
	TerrainDoc *doc = terEdGetDoc();
	if (doc)
	{	
        TerrainEditorSourceLayer *layer = terEdGetSelectedLayer(doc);
        if (layer)
        {
            layer->exclusion_version = version;
			terrainSourceLayerSetUnsaved(layer);
			wleSetDocSavedBit();
            terrainSourceUpdateAllObjects(layer, true, true);
        }
    }
}

static void terEdColorShift(UISliderTextEntry *slider_text, bool bFinished, void *unused)
{
	TerrainDoc *doc = terEdGetDoc();
	if (doc)
	{	
		TerrainEditorSourceLayer *layer = terEdGetSelectedLayer(doc);
		if (layer)
		{
			layer->color_shift = ui_SliderTextEntryGetValue(slider_text);
			terrainSourceLayerSetUnsaved(layer);
			wleSetDocSavedBit();
			terrainSourceUpdateColors(layer, true);
			terrainSourceUpdateAllObjects(layer, true, true);
		}
	}
}

//// Terrain Debug Window

void terrainDebugBlock(UIButton *button, TerrainDoc *doc)
{
	SetClusterDebugSettings(doc->state.select_block_dims[0]/GRID_BLOCK_SIZE, (doc->state.select_block_dims[0]/GRID_BLOCK_SIZE) + (doc->state.select_block_dims[2]/GRID_BLOCK_SIZE),
		doc->state.select_block_dims[1]/GRID_BLOCK_SIZE, (doc->state.select_block_dims[1]/GRID_BLOCK_SIZE) + (doc->state.select_block_dims[3]/GRID_BLOCK_SIZE),
		ui_CheckButtonGetState(doc->terrain_ui->distributed_remesh_check), ui_CheckButtonGetState(doc->terrain_ui->remesh_higher_precision_check), false);

	wleCmdReloadFromSource();
	doc->state.new_block_mode = false;
	ui_WindowHide(doc->terrain_ui->select_block_window);
}

void terEdLayerDebugTerrain(UIButton *button, void *userdata)
{
	TerrainDoc *doc = terEdGetDoc();
	TerrainEditorSourceLayer *layer = terEdGetSelectedLayer(doc);
	if (!layer || layer->loaded_lod < 0 || layer->effective_mode != LAYER_MODE_EDITABLE)
	{
		return;
	}

	doLoadThreadCheck();

	ui_WindowShow(doc->terrain_ui->select_block_window);
	ui_WindowSetTitle(doc->terrain_ui->select_block_window, "Debug Terrain Cluster");
	ui_ButtonSetCallback(doc->terrain_ui->select_block_button, terrainDebugBlock, doc);
	ui_ButtonSetText(doc->terrain_ui->select_block_button, "Debug");

	ui_TextEntrySetText(doc->terrain_ui->select_block_x_entry, "0");
	ui_SpinnerSetValueAndCallback(doc->terrain_ui->select_block_x, 0);
	doc->state.select_block_dims[0] = 0;

	ui_TextEntrySetText(doc->terrain_ui->select_block_y_entry, "0");
	ui_SpinnerSetValueAndCallback(doc->terrain_ui->select_block_y, 0);
	doc->state.select_block_dims[1] = 0;

	ui_TextEntrySetText(doc->terrain_ui->select_block_width_entry, "256");
	ui_SpinnerSetValueAndCallback(doc->terrain_ui->select_block_width, GRID_BLOCK_SIZE);
	doc->state.select_block_dims[2] = GRID_BLOCK_SIZE;

	ui_TextEntrySetText(doc->terrain_ui->select_block_height_entry, "256");
	ui_SpinnerSetValueAndCallback(doc->terrain_ui->select_block_height, GRID_BLOCK_SIZE);
	doc->state.select_block_dims[3] = GRID_BLOCK_SIZE;

	ui_WindowAddChild(doc->terrain_ui->select_block_window, doc->terrain_ui->distributed_remesh_check);
	doc->terrain_ui->distributed_remesh_check->state = true;

	ui_WindowAddChild(doc->terrain_ui->select_block_window, doc->terrain_ui->remesh_higher_precision_check);

	doc->state.new_block_mode = true;
}

//// New terrain window

void terrainCreateBlock(UIButton *button, TerrainDoc *doc)
{
	bool failure = false;
	int block_min[2], block_max[2], new_idx = -1;
	TerrainEditorSourceLayer *layer = terEdGetSelectedLayer(doc);
	IVec2 offset = { doc->state.select_block_dims[0]/GRID_BLOCK_SIZE, doc->state.select_block_dims[1]/GRID_BLOCK_SIZE };
	IVec2 size = { doc->state.select_block_dims[2]/GRID_BLOCK_SIZE, doc->state.select_block_dims[3]/GRID_BLOCK_SIZE };
	bool overlap = terrainIsOverlappingBlock(offset, size);

	if (!layer || overlap || !doc->state.new_block_mode)
		return;

	for (block_min[1] = offset[1]; block_min[1] <= offset[1]+size[1]-1; block_min[1] += 7)
	{
		for (block_min[0] = offset[0]; block_min[0] <= offset[0]+size[0]-1; block_min[0] += 7)
		{
			block_max[0] = MIN(offset[0] + size[0] - 1, block_min[0]+6);
			block_max[1] = MIN(offset[1] + size[1] - 1, block_min[1]+6);
			failure = !terrainSourceCreateBlock(PARTITION_CLIENT, layer, block_min, block_max, true, true);
			if (failure)
			{
				Alertf("No good, very bad failure while adding terrain block: Failed to check out files! Aborting.");
				break;
			}
		}
		if (failure)
			break;
	}

	if (doc->state.source->editing_lod == HEIGHTMAP_NO_LOD || doc->state.source->editing_lod < 0)
		doc->state.source->editing_lod = HEIGHTMAP_DEFAULT_LOD;

	doLoadThreadCheck();

	terrainSourceLayerResample(layer, doc->state.source->editing_lod);
	terrainSourceSetVisibleLOD(doc->state.source, doc->state.source->editing_lod);
	terEdUpdateLayerButtons(doc);

	doc->state.new_block_mode = false;
	ui_WindowHide(doc->terrain_ui->select_block_window);
}

void terEdLayerAddTerrain(UIButton *button, void *userdata)
{
    TerrainDoc *doc = terEdGetDoc();
	TerrainEditorSourceLayer *layer = terEdGetSelectedLayer(doc);
	if (!layer || layer->loaded_lod < 0 ||
		layer->effective_mode != LAYER_MODE_EDITABLE)
		return;

	doLoadThreadCheck();

	ui_WindowShow(doc->terrain_ui->select_block_window);
	ui_WindowSetTitle(doc->terrain_ui->select_block_window, "Create New Terrain");
    ui_ButtonSetCallback(doc->terrain_ui->select_block_button, terrainCreateBlock, doc);
    ui_ButtonSetText(doc->terrain_ui->select_block_button, "Create");

	ui_TextEntrySetText(doc->terrain_ui->select_block_x_entry, "0");
	ui_SpinnerSetValueAndCallback(doc->terrain_ui->select_block_x, 0);
	doc->state.select_block_dims[0] = 0;

	ui_TextEntrySetText(doc->terrain_ui->select_block_y_entry, "0");
	ui_SpinnerSetValueAndCallback(doc->terrain_ui->select_block_y, 0);
	doc->state.select_block_dims[1] = 0;

	ui_TextEntrySetText(doc->terrain_ui->select_block_width_entry, "256");
	ui_SpinnerSetValueAndCallback(doc->terrain_ui->select_block_width, GRID_BLOCK_SIZE);
	doc->state.select_block_dims[2] = GRID_BLOCK_SIZE;

	ui_TextEntrySetText(doc->terrain_ui->select_block_height_entry, "256");
	ui_SpinnerSetValueAndCallback(doc->terrain_ui->select_block_height, GRID_BLOCK_SIZE);
	doc->state.select_block_dims[3] = GRID_BLOCK_SIZE;

	ui_WindowRemoveChild(doc->terrain_ui->select_block_window, doc->terrain_ui->distributed_remesh_check);

	doc->state.new_block_mode = true;
}


//// Layer Change Resolution Window

void terEdLayerChangeResolution(UIButton *button, void *userdata)
{
	int lod;
    TerrainDoc *doc = terEdGetDoc();
	if (doc->state.source->editing_lod < 0 ||
		doc->state.source->editing_lod == HEIGHTMAP_NO_LOD)
		return;

	doLoadThreadCheck();

	lod = MAX(0, doc->state.source->editing_lod-MIN_TERRAIN_EDIT_LOD);
	ui_ComboBoxSetSelected(doc->terrain_ui->new_granularity_combo, lod);
	ui_LabelSetText(doc->terrain_ui->new_granularity_label, "NO CHANGE to resolution.");
	ui_LabelSetText(doc->terrain_ui->new_granularity_label_2, "");
	ui_WindowShow(doc->terrain_ui->resolution_window);
}

static void terrainNewGranularityCB(UIComboBox *combo, TerrainDoc *doc)
{
	int lod = ui_ComboBoxGetSelected(combo)+MIN_TERRAIN_EDIT_LOD;
	char buf[64];
	U32 res = GRID_LOD(lod);

	if (lod < doc->state.source->editing_lod)
	{
		sprintf(buf, "INCREASE layer resolution to %d x %d.", res, res);
		if (doc->state.memory_limit)
			ui_LabelSetText(doc->terrain_ui->new_granularity_label_2, "WARNING: Insufficient memory!");
		else
			ui_LabelSetText(doc->terrain_ui->new_granularity_label_2, "NOTE: Undo history will be cleared.");
	}
	else if (lod > doc->state.source->editing_lod)
	{
		sprintf(buf, "REDUCE layer resolution to %d x %d.", res, res);
		ui_LabelSetText(doc->terrain_ui->new_granularity_label_2, "WARNING: Data will be lost.");
	}
	else
	{
		sprintf(buf, "NO CHANGE to resolution.");
		ui_LabelSetText(doc->terrain_ui->new_granularity_label_2, "");
	}
	ui_LabelSetText(doc->terrain_ui->new_granularity_label, buf);
}

static void terrainCancelResolution(UIButton *button, TerrainDoc *doc)
{
	ui_WindowHide(doc->terrain_ui->resolution_window);
}

static void terrainDoChangeResolution(UIButton *button, TerrainDoc *doc)
{
	int lod = ui_ComboBoxGetSelected(doc->terrain_ui->new_granularity_combo)+MIN_TERRAIN_EDIT_LOD;

	if (doc->state.source->editing_lod < 0 ||
		doc->state.source->editing_lod == HEIGHTMAP_NO_LOD)
	{
		ui_WindowHide(doc->terrain_ui->resolution_window);
		return;
	}

	if (lod < doc->state.source->editing_lod && doc->state.memory_limit)
	{
		Alertf("Cannot increase resolution! Not enough memory.");
		return;
	}

	if (lod >= MIN_TERRAIN_EDIT_LOD && lod < MAX_TERRAIN_LODS && lod != doc->state.source->editing_lod)
	{
        //terrainQueueLock();
		EditUndoStackClear(doc->state.undo_stack);

		terrainQueueResample(doc->state.task_queue, lod, TERRAIN_TASK_SHOW_PROGRESS | TERRAIN_TASK_UNDOABLE);
	}
	ui_WindowHide(doc->terrain_ui->resolution_window);
}

void terEdCreateChangeResolutionWindow(TerrainDoc *doc)
{
	int i;
	UILabel *label;
	UIButton *button;
	UIComboBox *combo;

	doc->terrain_ui->resolution_window = ui_WindowCreate("Change Layer Resolution", 50, 120, 260, 120);

	label = ui_LabelCreate("New Resolution:", 10, 5);
	ui_WindowAddChild(doc->terrain_ui->resolution_window, label);

	{
		void ***model = calloc(1, sizeof(void **));
		combo = ui_ComboBoxCreate(10, 25, 100, NULL, model, NULL);
		ui_ComboBoxSetSelectedCallback(combo, terrainNewGranularityCB, doc);
		combo->iSelected = MAX(0, doc->state.source->visible_lod-MIN_TERRAIN_EDIT_LOD);

		for (i = MIN_TERRAIN_EDIT_LOD; i < MAX_TERRAIN_LODS; i++)
			eaPush(combo->model, granularities[i]);

		ui_WindowAddChild(doc->terrain_ui->resolution_window, combo);
		doc->terrain_ui->new_granularity_combo = combo;
	}

	label = ui_LabelCreate("NO CHANGE to resolution.", 10, 45);
	ui_WindowAddChild(doc->terrain_ui->resolution_window, label);
	doc->terrain_ui->new_granularity_label = label;

	label = ui_LabelCreate("", 10, 65);
	ui_WindowAddChild(doc->terrain_ui->resolution_window, label);
	doc->terrain_ui->new_granularity_label_2 = label;

	button = ui_ButtonCreate("OK", 10, 85, NULL, NULL);
	ui_WidgetSetDimensions(&button->widget, 80, 25);
	ui_ButtonSetCallback(button, terrainDoChangeResolution, doc);
	ui_WindowAddChild(doc->terrain_ui->resolution_window, button);

	button = ui_ButtonCreate("Cancel", 110, 85, NULL, NULL);
	ui_ButtonSetCallback(button, terrainCancelResolution, doc);
	ui_WidgetSetDimensions(&button->widget, 80, 25);
	ui_WindowAddChild(doc->terrain_ui->resolution_window, button);
}

#endif
//// New layer
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Terrain.CreateNewLayer");
void terrainCreateNewLayer(void)
{
#ifndef NO_EDITORS
	wleUINewLayerDialog();
#endif
}
#ifndef NO_EDITORS

void terEdCreateNewLayerClicked(UIButton *button, void *userdata)
{
    terrainCreateNewLayer();
}

//// Delete layer

void terEdDeleteLayer(UIButton *button, void *userdata)
{
    TerrainDoc *doc = terEdGetDoc();
    TerrainEditorSourceLayer *layer = terEdGetSelectedLayer(doc);
    if (layer)
    {
        wleUIDeleteLayerDialogCreate(layer->layer);
    }
	else
	{
		// Error
	}
}

//// Merge layers

typedef struct MergeLayerWindowInfo
{
    TerrainEditorSourceLayer **layer_list;
    TerrainEditorSourceLayer *dest_layer;
    char *filename;
} MergeLayerWindowInfo;

void terEdDoMergeLayers(MergeLayerWindowInfo *info)
{
    IVec2 min = { -8e8, -8e8 };
    IVec2 max = {  8e8,  8e8 };
    TerrainDoc *doc = terEdGetDoc();
    if (!doc)
        return;

	EditUndoStackClear(doc->state.undo_stack);
    
    terrainSourceModifyLayers(PARTITION_CLIENT, info->layer_list, info->dest_layer, min, max);
    
    eaDestroy(&info->layer_list);
    SAFE_FREE(info);
}

static void terEdMergeLayerDialogFileOk(const char *dir, const char *fileName, MergeLayerWindowInfo *info)
{
    int i;
	char fullName[CRYPTIC_MAX_PATH];

	sprintf(fullName, "%s/%s", dir, fileName);

    for (i = 0; i < eaSize(&info->layer_list); i++)
    {
        if (!stricmp(fullName, layerGetFilename(info->layer_list[i]->layer)))
        {
            info->dest_layer = info->layer_list[i];
            terEdDoMergeLayers(info);
            return;
        }
    }
    Alertf("Layer %s is not one of the selected editable layers.", fileName);
}

static bool terEdMergeLayerDialogTypeCancel(MergeLayerWindowInfo *info)
{
    eaDestroy(&info->layer_list);
	SAFE_FREE(info);
	return true;
}

void terEdLayerCombine(UIButton *button, void *userdata)
{
    int i;
    TerrainDoc *doc = terEdGetDoc();
	ZoneMapLayer **src_layer_list = NULL;
    TerrainEditorSourceLayer **layer_list = NULL;
    MergeLayerWindowInfo *info; 
	char startDir[CRYPTIC_MAX_PATH];
	char topDir[CRYPTIC_MAX_PATH];
	const char *filename;
	UIWindow *browser;

	emPanelsGetMapSelectedLayers(&src_layer_list);

    if (eaSize(&src_layer_list) < 2)
    {
        Alertf("Must select at least 2 layers");
		eaDestroy(&src_layer_list);
        return;
    }
    
    for (i = 0; i < eaSize(&src_layer_list); i++)
    {
        TerrainEditorSourceLayer *layer = src_layer_list[i]->terrain.source_data;
        if (layer->effective_mode != LAYER_MODE_EDITABLE)
        {
            Alertf("Layers must be locked!");
            eaDestroy(&layer_list);
			eaDestroy(&src_layer_list);
            return;
        }
        eaPush(&layer_list, layer);
    }

	eaDestroy(&src_layer_list);

	for (i = 0; i < eaSize(&layer_list); i++)
        if (layer_list[i]->saved == 0)
        {
            Alertf("ERROR: You MUST save your data before continuing.");
            eaDestroy(&layer_list);
            return;
        }
    
    Alertf("Warning: Potentially destructive operation. You should have your data committed to Gimme before continuing.");  

    info = calloc(1, sizeof(MergeLayerWindowInfo));
    info->layer_list = layer_list;
    
	// create a file browser to determine layer file's location
	fileLocateWrite("maps", topDir);
	filename = zmapGetFilename(NULL);
	if (filename)
		fileLocateWrite(filename, startDir);
	else
		strcpy(startDir, topDir);
	getDirectoryName(startDir);
	backSlashes(startDir);
	backSlashes(topDir);
	browser = ui_FileBrowserCreate("Select a layer to merge to", "Save", UIBrowseExisting, UIBrowseFiles, false,
								   topDir, startDir, NULL, "layer", terEdMergeLayerDialogTypeCancel, info,
								   terEdMergeLayerDialogFileOk, info);
	if (browser)
	{
		elUICenterWindow(browser);
		ui_WindowShow(browser);
    }
}

//// Block Removal

void terrainRemoveBlock(UIButton *button, MergeLayerWindowInfo *info)
{
    IVec2 min, max;
    TerrainDoc *doc = terEdGetDoc();
    if (!doc)
        return;
    
    min[0] = doc->state.select_block_dims[0]/GRID_BLOCK_SIZE;
    min[1] = doc->state.select_block_dims[1]/GRID_BLOCK_SIZE;
    max[0] = min[0] + doc->state.select_block_dims[2]/GRID_BLOCK_SIZE - 1;
    max[1] = min[1] + doc->state.select_block_dims[3]/GRID_BLOCK_SIZE - 1;

	EditUndoStackClear(doc->state.undo_stack);

    terrainSourceModifyLayers(PARTITION_CLIENT, info->layer_list, NULL, min, max);
    
    eaDestroy(&info->layer_list);
    SAFE_FREE(info);

	doc->state.remove_block_mode = false;
	ui_WindowHide(doc->terrain_ui->select_block_window);
}

void terEdLayerRemoveTerrain(UIButton *button, void *userdata)
{
    int i;
    TerrainDoc *doc = terEdGetDoc();
    TerrainEditorSourceLayer **layer_list = NULL;
    MergeLayerWindowInfo *info; 
	ZoneMapLayer **src_layer_list = NULL;

	emPanelsGetMapSelectedLayers(&src_layer_list);

    if (eaSize(&src_layer_list) < 1)
    {
        Alertf("Must select a layers");
		eaDestroy(&src_layer_list);
        return;
    }
   
    for (i = 0; i < eaSize(&src_layer_list); i++)
    {
        TerrainEditorSourceLayer *layer = src_layer_list[i]->terrain.source_data;
        if (layer->effective_mode != LAYER_MODE_EDITABLE)
        {
            Alertf("Layers must be locked!");
            eaDestroy(&layer_list);
			eaDestroy(&src_layer_list);
            return;
        }
        eaPush(&layer_list, layer);
    }

	eaDestroy(&src_layer_list);

    for (i = 0; i < eaSize(&layer_list); i++)
        if (layer_list[i]->saved == 0)
        {
            Alertf("Error: Potentially destructive operation. You must save your data (and probably commit to Gimme) before continuing.");
            return;
        }
    
    Alertf("Warning: Potentially destructive operation. You should have your data committed to Gimme before continuing.");  

    info = calloc(1, sizeof(MergeLayerWindowInfo));
    info->layer_list = layer_list;

	ui_WindowShow(doc->terrain_ui->select_block_window);
	ui_WindowSetTitle(doc->terrain_ui->select_block_window, "Remove Terrain");
    ui_ButtonSetCallback(doc->terrain_ui->select_block_button, terrainRemoveBlock, info);
    ui_ButtonSetText(doc->terrain_ui->select_block_button, "Remove");

	ui_TextEntrySetText(doc->terrain_ui->select_block_x_entry, "0");
	ui_SpinnerSetValueAndCallback(doc->terrain_ui->select_block_x, 0);
	doc->state.select_block_dims[0] = 0;

	ui_TextEntrySetText(doc->terrain_ui->select_block_y_entry, "0");
	ui_SpinnerSetValueAndCallback(doc->terrain_ui->select_block_y, 0);
	doc->state.select_block_dims[1] = 0;

	ui_TextEntrySetText(doc->terrain_ui->select_block_width_entry, "256");
	ui_SpinnerSetValueAndCallback(doc->terrain_ui->select_block_width, GRID_BLOCK_SIZE);
	doc->state.select_block_dims[2] = GRID_BLOCK_SIZE;

	ui_TextEntrySetText(doc->terrain_ui->select_block_height_entry, "256");
	ui_SpinnerSetValueAndCallback(doc->terrain_ui->select_block_height, GRID_BLOCK_SIZE);
	doc->state.select_block_dims[3] = GRID_BLOCK_SIZE;

	ui_WindowRemoveChild(doc->terrain_ui->select_block_window, doc->terrain_ui->distributed_remesh_check);

    doc->state.remove_block_mode = true;
}

//// Split layers

void terEdDoSplitLayers(MergeLayerWindowInfo *info)
{
    IVec2 min, max;
    TerrainDoc *doc = terEdGetDoc();
    if (!doc)
        return;
    
    min[0] = doc->state.select_block_dims[0]/GRID_BLOCK_SIZE;
    min[1] = doc->state.select_block_dims[1]/GRID_BLOCK_SIZE;
    max[0] = min[0] + doc->state.select_block_dims[2]/GRID_BLOCK_SIZE - 1;
    max[1] = min[1] + doc->state.select_block_dims[3]/GRID_BLOCK_SIZE - 1;

	EditUndoStackClear(doc->state.undo_stack);

    terrainSourceModifyLayers(PARTITION_CLIENT, info->layer_list, info->dest_layer, min, max);
    
    eaDestroy(&info->layer_list);
    SAFE_FREE(info);
}

static void terEdSplitLayerDialogFileOk(const char *dir, const char *fileName, MergeLayerWindowInfo *info)
{
    int i;
	char fullName[CRYPTIC_MAX_PATH];
    TerrainDoc *doc = terEdGetDoc();

	sprintf(fullName, "%s/%s", dir, fileName);

	ui_WindowHide(doc->terrain_ui->select_block_window);
    
    for (i = 0; i < eaSize(&doc->state.source->layers); i++)
    {
        if (!stricmp(fullName, layerGetFilename(doc->state.source->layers[i]->layer)) &&
            doc->state.source->layers[i]->effective_mode == LAYER_MODE_EDITABLE)
        {
            info->dest_layer = doc->state.source->layers[i];
            terEdDoSplitLayers(info);
            return;
        }
    }
    Alertf("Layer %s is not one of the selected editable layers.", fileName);
}

void terrainSelectSplitLayer(UIButton *button, MergeLayerWindowInfo *info)
{
	char startDir[CRYPTIC_MAX_PATH];
	char topDir[CRYPTIC_MAX_PATH];
	const char *filename;
	UIWindow *browser;
    TerrainDoc *doc = terEdGetDoc();

    doc->state.split_block_mode = false;
    
	// create a file browser to determine layer file's location
	fileLocateWrite("maps", topDir);
	filename = zmapGetFilename(NULL);
	if (filename)
		fileLocateWrite(filename, startDir);
	else
		strcpy(startDir, topDir);
	getDirectoryName(startDir);
	backSlashes(startDir);
	backSlashes(topDir);
	browser = ui_FileBrowserCreate("Select layer to split to", "Save", UIBrowseExisting, UIBrowseFiles, false,
								   topDir, startDir, NULL, "layer", terEdMergeLayerDialogTypeCancel, info,
								   terEdSplitLayerDialogFileOk, info);
	if (browser)
	{
		elUICenterWindow(browser);
		ui_WindowShow(browser);
	}
}

void terEdLayerSplit(UIButton *button, void *userdata)
{
    int i;
    TerrainDoc *doc = terEdGetDoc();
    TerrainEditorSourceLayer **layer_list = NULL;
    MergeLayerWindowInfo *info; 
	ZoneMapLayer **src_layer_list = NULL;

	emPanelsGetMapSelectedLayers(&src_layer_list);

    if (eaSize(&src_layer_list) < 1)
    {
        Alertf("Must select a layers");
		eaDestroy(&src_layer_list);
        return;
    }
   
    for (i = 0; i < eaSize(&src_layer_list); i++)
    {
        TerrainEditorSourceLayer *layer = src_layer_list[i]->terrain.source_data;
        if (layer->effective_mode != LAYER_MODE_EDITABLE)
        {
            Alertf("Layers must be locked!");
            eaDestroy(&layer_list);
			eaDestroy(&src_layer_list);
            return;
        }
        eaPush(&layer_list, layer);
    }

	eaDestroy(&src_layer_list);

    for (i = 0; i < eaSize(&layer_list); i++)
        if (layer_list[i]->saved == 0)
        {
            Alertf("Error: Potentially destructive operation. You must save your data (and probably commit to Gimme) before continuing.");
            return;
        }
    
    Alertf("Warning: Potentially destructive operation. You should have your data committed to Gimme before continuing.");  

    info = calloc(1, sizeof(MergeLayerWindowInfo));
    info->layer_list = layer_list;
    
	ui_WindowShow(doc->terrain_ui->select_block_window);
	ui_WindowSetTitle(doc->terrain_ui->select_block_window, "Select Terrain to Split");
    
    ui_ButtonSetCallback(doc->terrain_ui->select_block_button, terrainSelectSplitLayer, info);
    ui_ButtonSetText(doc->terrain_ui->select_block_button, "Split");

	ui_TextEntrySetText(doc->terrain_ui->select_block_x_entry, "0");
	ui_SpinnerSetValueAndCallback(doc->terrain_ui->select_block_x, 0);
	doc->state.select_block_dims[0] = 0;

	ui_TextEntrySetText(doc->terrain_ui->select_block_y_entry, "0");
	ui_SpinnerSetValueAndCallback(doc->terrain_ui->select_block_y, 0);
	doc->state.select_block_dims[1] = 0;

	ui_TextEntrySetText(doc->terrain_ui->select_block_width_entry, "256");
	ui_SpinnerSetValueAndCallback(doc->terrain_ui->select_block_width, GRID_BLOCK_SIZE);
	doc->state.select_block_dims[2] = GRID_BLOCK_SIZE;

	ui_TextEntrySetText(doc->terrain_ui->select_block_height_entry, "256");
	ui_SpinnerSetValueAndCallback(doc->terrain_ui->select_block_height, GRID_BLOCK_SIZE);
	doc->state.select_block_dims[3] = GRID_BLOCK_SIZE;

	ui_WindowRemoveChild(doc->terrain_ui->select_block_window, doc->terrain_ui->distributed_remesh_check);

    doc->state.split_block_mode = true;
}

//// Export layer data

void terEdDoExportLayer(const char *dir, const char *fileName, void *userdata)
{
    int i, j;
    TerrainDoc *doc = terEdGetDoc();
    TerrainExportInfo info = { 0 };
	ZoneMapLayer **layer_list = NULL;
	char fullName[CRYPTIC_MAX_PATH];
	char heightmapName[CRYPTIC_MAX_PATH];
    char destName[CRYPTIC_MAX_PATH];
    FILE *fOutHeight = NULL, *fOutColor = NULL;
    TerrainBlockRange **blocks = NULL;
	sprintf(fullName, "%s/%s", dir, fileName);

	emPanelsGetMapSelectedLayers(&layer_list);

    if (eaSize(&layer_list) < 1)
    {
        Alertf("Must select at least one layer.");
		eaDestroy(&layer_list);
        return;
    }

    for (i = 0; i < eaSize(&layer_list); i++)
    {
        TerrainEditorSourceLayer *layer = layer_list[i]->terrain.source_data;
        if (layer->effective_mode != LAYER_MODE_EDITABLE)
        {
            Alertf("Layers must be in Edit mode!");
            eaDestroyEx(&info.blocks, NULL);
            eaDestroy(&blocks);
			eaDestroy(&layer_list);
            return;
        }
        for (j = 0; j < eaSize(&layer->blocks); j++)
        {
            TerrainExportInfoBlock *new_block = calloc(1, sizeof(TerrainExportInfoBlock));
			setVec2(new_block->min, layer->blocks[j]->range.min_block[0], layer->blocks[j]->range.min_block[2]); 
			setVec2(new_block->max, layer->blocks[j]->range.max_block[0], layer->blocks[j]->range.max_block[2]); 
            eaPush(&info.blocks, new_block);
            eaPush(&blocks, layer->blocks[j]);
        }
        info.lod = layer->loaded_lod;
    }

	eaDestroy(&layer_list);

    sprintf(heightmapName, "%s_height.tiff", fullName);
    fileLocateWrite(heightmapName, destName);
	fOutHeight = fopen(destName, "wb");
    if (!fOutHeight)
        Errorf("Failed to open file for writing: %s\n", destName);

    sprintf(heightmapName, "%s_color.tiff", fullName);
    fileLocateWrite(heightmapName, destName);
	fOutColor = fopen(destName, "wb");
    if (!fOutColor)
        Errorf("Failed to open file for writing: %s\n", destName);
    
	if (fOutHeight && fOutColor)
	{
        terrainExportTiff(blocks, doc->state.source->heightmaps, info.lod,
                          fOutHeight, fOutColor, &info.height_min, &info.height_max);
	}
    
    if (fOutHeight)
		fclose(fOutHeight);
    if (fOutColor)
		fclose(fOutColor);
    
    ParserWriteTextFile(fullName, parse_TerrainExportInfo, &info, 0, 0);

    StructDeInit(parse_TerrainExportInfo, &info);
    eaDestroyEx(&info.blocks, NULL);
    eaDestroy(&blocks);
}

void terEdLayerExport(UIButton *button, void *userdata)
{
	char startDir[CRYPTIC_MAX_PATH];
	char topDir[CRYPTIC_MAX_PATH];
	const char *filename;
	UIWindow *browser;
    
	// create a file browser to determine layer file's location
	fileLocateWrite("maps", topDir);
	filename = zmapGetFilename(NULL);
	if (filename)
		fileLocateWrite(filename, startDir);
	else
		strcpy(startDir, topDir);
	getDirectoryName(startDir);
	backSlashes(startDir);
	backSlashes(topDir);
	browser = ui_FileBrowserCreate("Export Layer...", "Export", UIBrowseNewOrExisting, UIBrowseFiles, false,
								   topDir, startDir, NULL, "terrain_info", NULL, NULL, terEdDoExportLayer, NULL);
	if (browser)
	{
		elUICenterWindow(browser);
		ui_WindowShow(browser);
	}
}

//// Import auto-reload

void terEdDoImportReload(TerrainDoc *doc)
{
    if (doc->state.color_needs_reload || doc->state.height_needs_reload)
    {
        int i;
        int lod = doc->state.source->editing_lod;

        if (doc->state.color_needs_reload)
        {
            FILE *fColor = NULL;
            char destName[CRYPTIC_MAX_PATH];

            doc->state.color_needs_reload = false;
       
            fileLocateWrite(doc->state.color_import, destName);
            fColor = fopen(destName, "rb");
            if (!fColor)
                Errorf("Failed to open file: %s\n", destName);

            if (fColor)
            {
                if (!terrainImportColorTiff(doc->state.source->heightmaps, doc->state.import_info, fColor, GET_COLOR_LOD(lod)))
                {
                    //Alertf("Import failed, Tiffs are probably corrupted or the wrong size.\n");
                    return;
                }

                terrainSourceRefreshHeightmaps(doc->state.source, false);
				terrainSourceModifyHeightmaps(doc->state.source);
                for (i = 0; i < eaSize(&doc->state.source->layers); i++)
                {
                    terrainSourceUpdateColors(doc->state.source->layers[i], true);
                    doc->state.source->layers[i]->saved = 0;
                }
                EditUndoStackClear(doc->state.undo_stack);
            }

            if (fColor)
                fclose(fColor);
        }
        if (doc->state.height_needs_reload)
        {
            FILE *fHeight = NULL;
            char destName[CRYPTIC_MAX_PATH];

            doc->state.height_needs_reload = false;
        
            fileLocateWrite(doc->state.height_import, destName);
            fHeight = fopen(destName, "rb");
            if (!fHeight)
                Errorf("Failed to open file: %s\n", destName);

            if (fHeight)
            {
                if (!terrainImportHeightTiff(doc->state.source->heightmaps, doc->state.import_info, fHeight, lod))
                {
                    //Alertf("Import failed, Tiffs are probably corrupted or the wrong size.\n");
                    return;
                }

                terrainSourceRefreshHeightmaps(doc->state.source, false);
				terrainSourceModifyHeightmaps(doc->state.source);
                for (i = 0; i < eaSize(&doc->state.source->layers); i++)
                {
                    terrainSourceUpdateNormals(doc->state.source->layers[i], true);
                    doc->state.source->layers[i]->saved = 0;
                }
                EditUndoStackClear(doc->state.undo_stack);
            }

            if (fHeight)
                fclose(fHeight);
        }
    }
}

static void terEdImportUpdate(const char *relpath, int when)
{
    TerrainDoc *doc = terEdGetDoc();
    if (doc &&
        doc->state.import_info &&
        doc->state.color_import &&
        !strcmp(doc->state.color_import, relpath))
    {
        doc->state.color_needs_reload = true;
    }
    if (doc &&
        doc->state.import_info &&
        doc->state.height_import &&
        !strcmp(doc->state.height_import, relpath))
    {
        doc->state.height_needs_reload = true;
    }
}

#endif
AUTO_RUN;
void terEdInitImport(void)
{
#ifndef NO_EDITORS
    FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "maps/*.tiff", terEdImportUpdate);
#endif
}
#ifndef NO_EDITORS

//// Import layer data

void terEdDoImportLayer(const char *dir, const char *fileName, void *userdata)
{
	int i;
    TerrainDoc *doc = terEdGetDoc();
    TerrainExportInfo *info;
	char fullName[CRYPTIC_MAX_PATH];
	char heightmapName[CRYPTIC_MAX_PATH];
    char destName[CRYPTIC_MAX_PATH];
    FILE *fHeight = NULL, *fColor = NULL;
    int lod = 0;
    UIDialogButtons ret;
	sprintf(fullName, "%s/%s", dir, fileName);

    if (doc->state.color_import != NULL)
    {
        // To Do: Must finish prior import first
        return;
    }

    if (doc->state.source->editing_lod < 0 ||
        doc->state.source->editing_lod == HEIGHTMAP_NO_LOD)
    {
        // No terrain to import into.
        return;
    }

    lod = doc->state.source->editing_lod;
   
    ui_ModalDialogSetCustomButtons("Height", "Color", "Height & Color");
    ret = ui_ModalDialog("Import Terrain Data", "Which channels do you want to import?",
                         CreateColorRGB(0, 0, 0), UICustomButton1|UICustomButton2|UICustomButton3|UICancel);
    if (ret == UICancel)
    {
        return;
    }

    info = StructAlloc(parse_TerrainExportInfo);
    
    if (!ParserReadTextFile(fullName, parse_TerrainExportInfo, info, 0))
    {
        Alertf("Cannot read file %s\n", fullName);
        return;
    }

    if (info->lod != lod)
    {
        Alertf("LOD mismatch between exported data and loaded layers.\n");
        StructDestroy(parse_TerrainExportInfo, info);
        return;
    }

    if (ret == UICustomButton1 || ret == UICustomButton3)
    {
        sprintf(heightmapName, "%s_height.tiff", fullName);
        fileLocateWrite(heightmapName, destName);
        fHeight = fopen(destName, "rb");
        if (!fHeight)
            Errorf("Failed to open file: %s\n", destName);
        else
            doc->state.height_import = strdup(heightmapName);
    }

    if (ret == UICustomButton2 || ret == UICustomButton3)
    {
        sprintf(heightmapName, "%s_color.tiff", fullName);
        fileLocateWrite(heightmapName, destName);
        fColor = fopen(destName, "rb");
        if (!fColor)
            Errorf("Failed to open file: %s\n", destName);
        else
            doc->state.color_import = strdup(heightmapName);
    }

    if (fColor || fHeight)
    {
        if ((fColor && !terrainImportColorTiff(doc->state.source->heightmaps, info, fColor, GET_COLOR_LOD(lod))) ||
            (fHeight && !terrainImportHeightTiff(doc->state.source->heightmaps, info, fHeight, lod)))
        {
            Alertf("Import failed, Tiffs are probably corrupted or the wrong size.\n");
            SAFE_FREE(doc->state.height_import);
            doc->state.height_import = NULL;
            SAFE_FREE(doc->state.color_import);
            doc->state.color_import = NULL;
            StructDestroy(parse_TerrainExportInfo, info);
            return;
        }

        doc->state.import_info = info;
        
        terrainSourceRefreshHeightmaps(doc->state.source, false);
		terrainSourceModifyHeightmaps(doc->state.source);
        for (i = 0; i < eaSize(&doc->state.source->layers); i++)
        {
            if (fHeight)
	            terrainSourceUpdateNormals(doc->state.source->layers[i], true);
            if (fColor)
	            terrainSourceUpdateColors(doc->state.source->layers[i], true);
            doc->state.source->layers[i]->saved = 0;
        }
        EditUndoStackClear(doc->state.undo_stack);
        terEdUpdateLayerButtons(doc);
    }
    else
    {
        StructDestroy(parse_TerrainExportInfo, info);
    }

    if (fColor)
        fclose(fColor);
    if (fHeight)
        fclose(fHeight);
}

void terEdLayerImport(UIButton *button, void *userdata)
{
	char startDir[CRYPTIC_MAX_PATH];
	char topDir[CRYPTIC_MAX_PATH];
	const char *filename;
	UIWindow *browser;
    TerrainDoc *doc = terEdGetDoc();

    if (doc->state.import_info != NULL)
    {
        StructDestroy(parse_TerrainExportInfo, doc->state.import_info);
        doc->state.import_info = NULL;
        SAFE_FREE(doc->state.height_import);
        doc->state.height_import = NULL;
        SAFE_FREE(doc->state.color_import);
        doc->state.color_import = NULL;
         terEdUpdateLayerButtons(doc);
        return;
    }
    
	// create a file browser to determine layer file's location
	fileLocateWrite("maps", topDir);
	filename = zmapGetFilename(NULL);
	if (filename)
		fileLocateWrite(filename, startDir);
	else
		strcpy(startDir, topDir);
	getDirectoryName(startDir);
	backSlashes(startDir);
	backSlashes(topDir);
	browser = ui_FileBrowserCreate("Import Layer...", "Import", UIBrowseExisting, UIBrowseFiles, false,
								   topDir, startDir, NULL, "terrain_info", NULL, NULL, terEdDoImportLayer,
								   NULL);
	if (browser)
	{
		elUICenterWindow(browser);
		ui_WindowShow(browser);
	}
}

//// Layers Panel

static TerrainLayerUI terrain_layer_ui = { 0 };

static void terrainGranularityCB(UIComboBox *combo, void *userdata)
{
    TerrainDoc *doc = terEdGetDoc();
	if (doc)
	{
		// Get the base LOD
		int lod = terEdGetMaxLOD(doc);
		if (lod == -1)
			return;
		lod += combo->iSelected;
	    
		if (lod >= 0 && lod <= MAX_TERRAIN_LODS) {
			terrainSourceSetVisibleLOD(doc->state.source, lod);
			terrain_layer_ui.granularity_combo->iSelected = MAX(0, lod-MIN_TERRAIN_EDIT_LOD);
		}
	}
}

static void terrainLayerModeText(UIComboBox *pCombo, S32 iRow, bool bInBox, const char *pchField, char **ppchOutput)
{
	estrClear(ppchOutput);
	switch (iRow)
	{
	xcase 0:
		estrAppend2(ppchOutput, "View Objects");
	xcase 1:
		estrAppend2(ppchOutput, "View Terrain");
	xcase 2:
		estrAppend2(ppchOutput, "Editable");
	xcase 3:
		estrAppend2(ppchOutput, "");
	xcase 4:
		estrAppend2(ppchOutput, "Game");
	xcase 5:
		estrAppend2(ppchOutput, "Loading...");
	xcase 6:
		estrAppend2(ppchOutput, "Saving...");
	xcase 7:
		estrAppend2(ppchOutput, "Binning...");
	}
}

static void terrainLayerModeSelected(UIComboBox *box, void *userdata)
{
    int i, new_mode = ui_ComboBoxGetSelected(box);
	ZoneMapLayer **layer_list = NULL;
	TerrainDoc *doc = terEdGetDoc();
	emPanelsGetMapSelectedLayers(&layer_list);

	for (i = 0; i < eaSize(&layer_list); i++)
	{
		if (doc->state.memory_limit && new_mode > 0 &&
			layer_list[i]->layer_mode < LAYER_MODE_TERRAIN)
		{
			Alertf("Cannot load source data for layer! Not enough memory.");
			return;
		}
		switch (new_mode)
		{
		xcase 0:
			wleUISetLayerMode(layer_list[i], LAYER_MODE_GROUPTREE, false);
		xcase 1:
			wleUISetLayerMode(layer_list[i], LAYER_MODE_TERRAIN, false);
		xcase 2:
			wleUISetLayerMode(layer_list[i], LAYER_MODE_EDITABLE, false);
		}
	}

	eaDestroy(&layer_list);
}

int terEdPopulateMapPanel(EMPanel *panel)
{
	int y = 0;
	UIButton *button;
	UILabel *label;
    UIComboBox *combo;
	UICheckButton *check;
	UISliderTextEntry *slider_text;
    static char **layer_modes = NULL;

	doLoadThreadCheck();

    if (!layer_modes)
    {
        eaPush(&layer_modes, "View Objects");
        eaPush(&layer_modes, "View Terrain");
        eaPush(&layer_modes, "Editing");
    }

	terrain_layer_ui.panel = panel;

	// bottom to top

	button = ui_ButtonCreate("Resample Terrain...", 0, y, terEdLayerChangeResolution, NULL);
	button->widget.width = 0.5;
	button->widget.widthUnit = UIUnitPercentage;
	button->widget.offsetFrom = UIBottomLeft;
	ui_ButtonSetTooltip(button, "Change the grid resolution of the selected layers.");
	emPanelAddChild(panel, button, false);
	terrain_layer_ui.resample_button = button;

	if (simplygonGetEnabled())
	{
		button = ui_ButtonCreate("Debug Terrain...", 0, y, terEdLayerDebugTerrain, NULL);
		button->widget.width = 0.5;
		button->widget.widthUnit = UIUnitPercentage;
		button->widget.xPOffset = 0.5;
		button->widget.offsetFrom = UIBottomLeft;
		ui_ButtonSetTooltip(button, "Debug a group of tiles.");
		emPanelAddChild(panel, button, false);
		terrain_layer_ui.debug_terrain_button = button;
	}

	y +=25;

	button = ui_ButtonCreate("Export Terrain...", 0, y, terEdLayerExport, NULL);
	button->widget.width = 0.5;
	button->widget.widthUnit = UIUnitPercentage;
	button->widget.offsetFrom = UIBottomLeft;
	ui_ButtonSetTooltip(button, "Export height & color data from the selected layers.");
	emPanelAddChild(panel, button, false);
	terrain_layer_ui.export_button = button;

	button = ui_ButtonCreate("Import Terrain...", 0, y, terEdLayerImport, NULL);
	button->widget.width = 0.5;
	button->widget.widthUnit = UIUnitPercentage;
	button->widget.xPOffset = 0.5;
	button->widget.offsetFrom = UIBottomLeft;
	ui_ButtonSetTooltip(button, "Import height & color data into selected layers.");
	emPanelAddChild(panel, button, false);
	terrain_layer_ui.import_button = button;

	y += 22;

	button = ui_ButtonCreate("Split Terrain...", 0, y, terEdLayerSplit, NULL);
	button->widget.width = 0.5;
	button->widget.widthUnit = UIUnitPercentage;
	button->widget.offsetFrom = UIBottomLeft;
	ui_ButtonSetTooltip(button, "Move a subset of terrain from selected layers to another layer.");
	emPanelAddChild(panel, button, false);
	terrain_layer_ui.split_button = button;

	button = ui_ButtonCreate("Combine Terrain...", 0, y, terEdLayerCombine, NULL);
	button->widget.width = 0.5;
	button->widget.widthUnit = UIUnitPercentage;
	button->widget.xPOffset = 0.5;
	button->widget.offsetFrom = UIBottomLeft;
	ui_ButtonSetTooltip(button, "Combine selected layers' terrain into one.");
	emPanelAddChild(panel, button, false);
	terrain_layer_ui.combine_button = button;

	y += 22;

	button = ui_ButtonCreate("Add Terrain...", 0, y, terEdLayerAddTerrain, NULL);
	button->widget.width = 0.5;
	button->widget.widthUnit = UIUnitPercentage;
	button->widget.offsetFrom = UIBottomLeft;
	ui_ButtonSetTooltip(button, "Add a new group of tiles to the selected layer.");
	emPanelAddChild(panel, button, false);
	terrain_layer_ui.add_terrain_button = button;

	button = ui_ButtonCreate("Delete Terrain...", 0, y, terEdLayerRemoveTerrain, NULL);
	button->widget.width = 0.5;
	button->widget.widthUnit = UIUnitPercentage;
	button->widget.xPOffset = 0.5;
	button->widget.offsetFrom = UIBottomLeft;
	ui_ButtonSetTooltip(button, "Remove a block of terrain.");
	emPanelAddChild(panel, button, false);
	terrain_layer_ui.remove_terrain_button = button;

	y += 22;

	button = ui_ButtonCreate("New Layer...", 0, y, terEdCreateNewLayerClicked, NULL);
	button->widget.width = 0.5;
	button->widget.widthUnit = UIUnitPercentage;
	button->widget.offsetFrom = UIBottomLeft;
	ui_ButtonSetTooltip(button, "Create a new terrain layer.");
	emPanelAddChild(panel, button, false);
	terrain_layer_ui.new_button = button;

	button = ui_ButtonCreate("Delete Layer", 0, y, terEdDeleteLayer, NULL);
	button->widget.width = 0.5;
	button->widget.widthUnit = UIUnitPercentage;
	button->widget.xPOffset = 0.5;
	button->widget.offsetFrom = UIBottomLeft;
	ui_ButtonSetTooltip(button, "Delete selected layer.");
	emPanelAddChild(panel, button, false);
	terrain_layer_ui.delete_button = button;

	y += 25;

	label = ui_LabelCreate("Terrain Hue Shift:", 0, y);
	label->widget.offsetFrom = UIBottomLeft;
	emPanelAddChild(panel, label, false);

	slider_text = ui_SliderTextEntryCreate("0", -360.0f, 360.0f, 120, y, 160);
	slider_text->widget.x = 123;
	slider_text->widget.y = y;
	slider_text->widget.xPOffset = 0.0f;
	slider_text->widget.yPOffset = 0.0f;
	slider_text->widget.offsetFrom = UIBottomLeft;
	ui_SliderTextEntrySetChangedCallback(slider_text, terEdColorShift, NULL);
	emPanelAddChild(panel, slider_text, false);
	terrain_layer_ui.hue_shift_slider = slider_text;

	y += 22;

	check = ui_CheckButtonCreate(0, y, "Playable Terrain", false);
	check->widget.offsetFrom = UIBottomLeft;
	ui_CheckButtonSetToggledCallback(check, terEdLayerPlayable, NULL);
	emPanelAddChild(panel, check, false);
	terrain_layer_ui.playable_check = check;

	y += 25;

	label = ui_LabelCreate("Object Exclusion:", 0, y);
	label->widget.offsetFrom = UIBottomLeft;
	emPanelAddChild(panel, label, false);

	combo = ui_ComboBoxCreateWithEnum(123, y, 116, TerrainExclusionVersionEnum, (UIComboBoxEnumFunc)terEdLayerExclusionVersion, NULL);
	combo->widget.offsetFrom = UIBottomLeft;
	emPanelAddChild(panel, combo, false);
	terrain_layer_ui.exclusion_combo = combo;

	y += 25;

	label = ui_LabelCreate("Editing Granularity:", 0, y);
	label->widget.offsetFrom = UIBottomLeft;
	emPanelAddChild(panel, label, false);

	combo = ui_ComboBoxCreate(123, y, 116, NULL, &terrain_layer_ui.granularity_model, NULL);
	combo->widget.offsetFrom = UIBottomLeft;
	ui_ComboBoxSetSelectedCallback(combo, terrainGranularityCB, NULL);
	combo->iSelected = 0;
	emPanelAddChild(panel, combo, false);
	terrain_layer_ui.granularity_combo = combo;

	y += 25;

	label = ui_LabelCreate("Layer Mode:", 0, y);
	label->widget.offsetFrom = UIBottomLeft;
	emPanelAddChild(panel, label, false);

    combo = ui_ComboBoxCreate(123, y, 114, NULL, (UIModel)&layer_modes, NULL);
	combo->widget.offsetFrom = UIBottomLeft;
    ui_ComboBoxSetSelectedCallback(combo, terrainLayerModeSelected, NULL);
	ui_ComboBoxSetTextCallback(combo, terrainLayerModeText, NULL);
    combo->iSelected = 0;
	combo->allowOutOfBoundsSelected = 1;
	emPanelAddChild(panel, combo, false);
    terrain_layer_ui.mode_combo = combo;

	y += 25;

	return y;
}

void terEdUpdateLayerButtons(TerrainDoc *doc)
{
    int i, min_lod, mode = -1;
    bool deselect_layers = false;
    int count = 0;
    bool editing = (eaSize(&doc->state.task_queue->edit_tasks) > 0);
	bool enabled, mode_enabled = true;
    bool found = false;
	ZoneMapLayer **layer_list = NULL;

	emPanelsGetMapSelectedLayers(&layer_list);

    if (!doc->state.import_info)
    {
        for (i = 0; i < eaSize(&layer_list); i++)
        {
			int layer_mode;
            TerrainEditorSourceLayer *layer = layer_list[i]->terrain.source_data;

			if (layer)
			{
				layer_mode = layer->effective_mode;
				assert(layer->layer);
				if (layer_mode != layer->layer->target_mode)
				{
					if (layer->layer->target_mode > layer_mode)
						layer_mode = 7; // "Loading..."
					else if (layer->layer->target_mode == LAYER_MODE_TERRAIN)
						layer_mode = 8; // "Saving..."
					else if (layer->layer->target_mode == LAYER_MODE_GROUPTREE)
						layer_mode = 9; // "Binning..."
					mode_enabled = false;
				}
			}
			else
				layer_mode = 5;

			if (mode == -1 || layer_mode == mode)
				mode = layer_mode;
			else
				mode = 0;

            if (layer_mode == LAYER_MODE_EDITABLE)
                count++;
            else
                count = 0;
        }
    }

	//Genesis UI
	if(doc->terrain_ui && doc->terrain_ui->genesis_panel)
	{
		if (!editing && !doc->state.memory_limit && !zmapInfoHasGenesisData(NULL))
		{
			emPanelSetActive(doc->terrain_ui->genesis_panel, true);

			ui_SetActive(UI_WIDGET(doc->terrain_ui->genesis_import_mapdesc_button), (count>0));
			ui_SetActive(UI_WIDGET(doc->terrain_ui->genesis_import_nodelayout_button), (count>0));
			ui_SetActive(UI_WIDGET(doc->terrain_ui->genesis_move_to_design_button), (count>0));
			ui_SetActive(UI_WIDGET(doc->terrain_ui->genesis_move_to_detail_button), (count>0));
		}
		else
		{
			emPanelSetActive(doc->terrain_ui->genesis_panel, false);
		}
	}

	{
		EMEditorDoc *active_doc = emGetActiveEditorDoc();
		if (active_doc != &doc->base_doc)
		{
			emPanelRemoveChild(terrain_layer_ui.panel, terrain_layer_ui.add_terrain_button, false);
			emPanelRemoveChild(terrain_layer_ui.panel, terrain_layer_ui.remove_terrain_button, false);
			emPanelRemoveChild(terrain_layer_ui.panel, terrain_layer_ui.resample_button, false);
			emPanelRemoveChild(terrain_layer_ui.panel, terrain_layer_ui.split_button, false);
			emPanelRemoveChild(terrain_layer_ui.panel, terrain_layer_ui.combine_button, false);
			emPanelRemoveChild(terrain_layer_ui.panel, terrain_layer_ui.export_button, false);
			emPanelRemoveChild(terrain_layer_ui.panel, terrain_layer_ui.import_button, false);
		}
		else
		{
			emPanelAddChild(terrain_layer_ui.panel, terrain_layer_ui.add_terrain_button, false);
			emPanelAddChild(terrain_layer_ui.panel, terrain_layer_ui.remove_terrain_button, false);
			emPanelAddChild(terrain_layer_ui.panel, terrain_layer_ui.resample_button, false);
			emPanelAddChild(terrain_layer_ui.panel, terrain_layer_ui.split_button, false);
			emPanelAddChild(terrain_layer_ui.panel, terrain_layer_ui.combine_button, false);
			emPanelAddChild(terrain_layer_ui.panel, terrain_layer_ui.export_button, false);
			emPanelAddChild(terrain_layer_ui.panel, terrain_layer_ui.import_button, false);
		}
	}

	enabled = (eaSize(&layer_list) == 1 && !editing);
    ui_SetActive((UIWidget *)terrain_layer_ui.delete_button, enabled);

	enabled = (count == 1 && !editing && mode == LAYER_MODE_EDITABLE);
    ui_SetActive((UIWidget *)terrain_layer_ui.add_terrain_button, enabled && !doc->state.memory_limit);
	ui_SetActive((UIWidget *)terrain_layer_ui.debug_terrain_button, enabled);
    ui_SetActive((UIWidget *)terrain_layer_ui.playable_check, enabled);
    ui_SetActive((UIWidget *)terrain_layer_ui.exclusion_combo, enabled);
	ui_SetActive((UIWidget *)terrain_layer_ui.hue_shift_slider, enabled);
	if (enabled)
	{
		const char *hue_text = ui_SliderTextEntryGetText(terrain_layer_ui.hue_shift_slider);
		F64 hue_val = (terrain_layer_ui.hue_shift_slider->isPercentage ? atof(hue_text)/100.0f : atof(hue_text));
		TerrainEditorSourceLayer *source_layer = layer_list[0]->terrain.source_data;
        if (source_layer && source_layer->effective_mode == LAYER_MODE_EDITABLE)
        {
            ui_CheckButtonSetState(terrain_layer_ui.playable_check, source_layer->playable);
			ui_ComboBoxSetSelected(terrain_layer_ui.exclusion_combo, source_layer->exclusion_version);
			if(	hue_val != source_layer->color_shift &&
				hue_val == ui_SliderTextEntryGetValue(terrain_layer_ui.hue_shift_slider))
				ui_SliderTextEntrySetValue(terrain_layer_ui.hue_shift_slider, source_layer->color_shift);
        }
        else
        {
        	ui_CheckButtonSetState(terrain_layer_ui.playable_check, layerGetPlayable(layer_list[0]));
			ui_ComboBoxSetSelected(terrain_layer_ui.exclusion_combo, layerGetExclusionVersion(layer_list[0]));
			if(	hue_val != layerGetColorShift(layer_list[0]) &&
				hue_val == ui_SliderTextEntryGetValue(terrain_layer_ui.hue_shift_slider))
				ui_SliderTextEntrySetValue(terrain_layer_ui.hue_shift_slider, layerGetColorShift(layer_list[0]));
        }
	}

	ui_SetActive(&terrain_layer_ui.mode_combo->widget, mode_enabled && (eaSize(&layer_list) > 0) );
	ui_ComboBoxSetSelected(terrain_layer_ui.mode_combo, (mode > 1) ? mode-2 : MAX(mode,0)+3);

    ui_ButtonSetText(terrain_layer_ui.import_button, "Import");
    
	enabled = (count > 0 && !editing && mode == LAYER_MODE_EDITABLE);
    ui_SetActive((UIWidget *)terrain_layer_ui.split_button, enabled);
    ui_SetActive((UIWidget *)terrain_layer_ui.remove_terrain_button, enabled);
    ui_SetActive((UIWidget *)terrain_layer_ui.resample_button, enabled);
    ui_SetActive((UIWidget *)terrain_layer_ui.export_button, enabled);
    ui_SetActive((UIWidget *)terrain_layer_ui.import_button, enabled);

	if (doc && doc->terrain_ui)
		ui_SetActive((UIWidget *)doc->terrain_ui->apply_brush_button, count > 0 && mode == LAYER_MODE_EDITABLE && !doc->state.memory_limit);

	ui_SetActive((UIWidget *)terrain_layer_ui.combine_button, (count > 1 && !editing));

    if (doc->state.import_info)
    {
        ui_SetActive((UIWidget *)terrain_layer_ui.mode_combo, false);
        ui_SetActive((UIWidget *)terrain_layer_ui.new_button, false);
        ui_SetActive((UIWidget *)terrain_layer_ui.import_button, true);
        ui_ButtonSetText(terrain_layer_ui.import_button, "Finish Import");
    }
    else
    {
        ui_SetActive((UIWidget *)terrain_layer_ui.new_button, (!editing && !doc->state.memory_limit));
    }

    min_lod = terEdGetMaxLOD(doc);
	ui_SetActive((UIWidget *)terrain_layer_ui.granularity_combo, (min_lod > -1 && !editing));

	// Check to see if we're done editing
    min_lod = terEdGetMaxLOD(doc);

    // Update granularity combo box
    eaClear(&terrain_layer_ui.granularity_model);
    if (min_lod > -1)
    {
        for (i = min_lod; i < MAX_TERRAIN_LODS; i++)
        {
            eaPush(&terrain_layer_ui.granularity_model, granularities[i]);
        }
        if (doc->state.source->visible_lod < min_lod)
        {
            terrainSourceSetVisibleLOD(doc->state.source, min_lod);
        }
        ui_ComboBoxSetSelected(terrain_layer_ui.granularity_combo, doc->state.source->visible_lod - min_lod);
        ui_SetActive((UIWidget *)terrain_layer_ui.granularity_combo, true);
    }
    else
    {
        eaPush(&terrain_layer_ui.granularity_model, "N/A");
        ui_ComboBoxSetSelected(terrain_layer_ui.granularity_combo, 0);
        ui_SetActive((UIWidget *)terrain_layer_ui.granularity_combo, false);
    }
    //printf("New LOD %d (%d)\n", doc->state.source->editing_lod, doc->state.source->visible_lod);

	eaDestroy(&layer_list);
}

#endif
