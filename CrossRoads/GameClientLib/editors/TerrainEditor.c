#define GENESIS_ALLOW_OLD_HEADERS
#ifndef NO_EDITORS

#include "wlTerrainSource.h"
#include "wlGenesis.h"
#include "wlGenesisExterior.h"
#include "wlGenesisExteriorNode.h"
#include "wlGenesisExteriorDesign.h"
#include "wlGenesisMissions.h"
#include "TerrainEditor.h"
#include "TerrainEditorPrivate.h"
#include "WorldLib.h"
#include "ObjectLibrary.h"
#include "MaterialEditor2EM.h"
#include "EditorPrefs.h"
#include "Rand.h"
#include "EditorObject.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "ProgressOverlay.h"
#include "ControllerScriptingSupport.h"

#include "WorldColl.h"
#include "inputLib.h"
#include "cmdparse.h"
#include "Materials.h"
#include "StringCache.h"
#include "ThreadManager.h"
#include "groupdbmodify.h"
#include "partition_enums.h"

#include "GfxSpriteText.h"
#include "GfxTerrain.h"
#include "GfxTexAtlas.h"
#include "GfxSprite.h"
#include "GfxCommandParse.h"

#include "SplineEditUI.h"
#include "GfxPrimitive.h"

#include "WorldEditorUI.h"
#include "WorldEditorOperations.h"

extern ParseTable parse_TerrainBrushStringRef[];
#define TYPE_parse_TerrainBrushStringRef TerrainBrushStringRef
extern ParseTable parse_TerrainBrushValues[];
#define TYPE_parse_TerrainBrushValues TerrainBrushValues

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););
AUTO_RUN_ANON(memBudgetAddMapping("ThreadStack:terrainEditorThreadProc", BUDGET_Editors););

static EMEditor terrain_editor;
static TerrainUI *global_terrain_ui=NULL;
static TerrainEditorPersistentState *global_terrain_state=NULL;
//static TerrainTask **terrain_tasks = NULL;

extern StaticDefineInt TerrainEditorViewModesEnum[];

static TerrainDoc *global_terrain_doc = NULL;

static bool bTerrainEditorThreaded = true;
static ManagedThread *terrainEditorThread;
static bool bTerrainPaintingThisFrame = false;
static HeightMapBackupList *pCurrentBackupList = NULL;
static TerrainMaterialChangeList *pCurrentMaterialList = NULL;
int iTerrainMemoryUsage = 0;
static U32 iLastClickTime = 0;

bool terrainOverrideMemoryLockout = false;

#define MAX_BRUSH_RATE 35 // About 30 fps

TerrainDoc *terEdGetDoc()
{
    return global_terrain_doc;
}

AUTO_COMMAND ACMD_HIDE;
void terrainOverrideMemoryLockoutMakingItReallyEasyToCrashMyPC(void)
{
	terrainOverrideMemoryLockout = true;
}

static void terrainGenesisCreateObjects(GenesisZoneNodeLayout *node_layout)
{
	TerrainDoc *doc = terEdGetDoc();
	GenesisMissionRequirements **mission_reqs = NULL;
	GenesisMissionRequirements *mission_req;
	GenesisToPlaceState to_place = { 0 };
	if(!doc)
		return;

	mission_req = StructCreate(parse_GenesisMissionRequirements);
	mission_req->missionName = StructAllocString("Default");
	eaPush(&mission_reqs, mission_req);
	genesisNodeDoObjectPlacement(PARTITION_CLIENT, NULL, &to_place, node_layout, mission_reqs, true, false);
	terEdDeleteAndPopulateGroup(doc->state.source, &to_place, true, false);
	eaDestroyStruct(&mission_reqs, parse_GenesisMissionRequirements);
}


static void terrainGenesisLoadLayerObjects()
{
	int i;
	for (i = zmapGetLayerCount(NULL)-1; i >= 0; --i)
	{
		ZoneMapLayer *layer = zmapGetLayer(NULL, i);
		if (layerGetMode(layer) < LAYER_MODE_GROUPTREE)
			layerSetMode(layer, LAYER_MODE_GROUPTREE, false, false, false);
	}
}

static void terrainGenesisMapDescSelectCB(const char *path, const char *filename, void *unused)
{
	char fullfilename[MAX_PATH];
	GenesisMapDescriptionFile *map_desc_file=NULL;

	sprintf(fullfilename, "%s\\%s", path, filename);

	EditorPrefStoreString(TER_ED_NAME, "Genesis", "MapDescName", path);

	map_desc_file = StructCreate(parse_GenesisMapDescriptionFile);
	if (ParserReadTextFile(fullfilename, parse_GenesisMapDescriptionFile, map_desc_file, 0) && map_desc_file->map_desc->exterior_layout)
	{
		GenesisZoneExterior *concrete = StructCreate(parse_GenesisZoneExterior);
		GenesisZoneNodeLayout *node_layout;
		U32 seed = randomU32();
		U32 detail_seed = randomU32();
		int itr=0;

		genesisTransmogrifyExterior(seed, detail_seed, map_desc_file->map_desc, map_desc_file->map_desc->exterior_layout, concrete);

		while(!(node_layout = genesisExteriorMoveRoomsToNodes(concrete, seed, seed, true, true, true)))
		{
			printf("Failed\n");
			seed++;
			itr++;
			if(itr > 100)
			{
				Alertf("No solution found for this room layout");
				break;
			}
		}

		if (node_layout)
		{
			char text[256];
			sprintf(text, "Import done: %d nodes, %d connections loaded.", eaSize(&node_layout->nodes), eaSize(&node_layout->connection_groups));
			ui_DialogPopup("Import completed", text);
			terrainGenesisCreateObjects(node_layout);
		}

		StructDestroy(parse_GenesisZoneExterior, concrete);
	}
	else
	{
		emStatusPrintf("Import Failed: Unable to read file or Map Description is not of type Exterior.");
	}
	StructDestroy(parse_GenesisMapDescriptionFile, map_desc_file);
}

static void terrainGenesisImportMapDescCB(void *unused, TerrainDoc *doc)
{
	UIWindow *browser;
	const char *start_dir;

	start_dir = EditorPrefGetString(TER_ED_NAME, "Genesis", "MapDescName", "genesis/mapdescriptions");
	browser = ui_FileBrowserCreate("Import a Map Description...", "Import", UIBrowseExisting, UIBrowseFiles, false,
								   "genesis/mapdescriptions", start_dir, NULL, ".mapdesc", NULL, NULL, terrainGenesisMapDescSelectCB, NULL);
	ui_WindowShow(browser);
}

static void terrainGenesisImportNodeLayoutSelectCB(const char *path, const char *filename, void *unused)
{
	char fullfilename[MAX_PATH];
	GenesisZoneNodeLayout *node_layout;

	sprintf(fullfilename, "%s\\%s", path, filename);

	EditorPrefStoreString(TER_ED_NAME, "Genesis", "NodeLayoutName", path);

	node_layout = StructCreate(parse_GenesisZoneNodeLayout);
	if (ParserReadTextFile(fullfilename, parse_GenesisZoneNodeLayout, node_layout, 0))
	{
		char text[256];
		sprintf(text, "Import done: %d nodes, %d connections loaded.", eaSize(&node_layout->nodes), eaSize(&node_layout->connection_groups));
		ui_DialogPopup("Import completed", text);
		genesisNodesFixup(node_layout);
		terrainGenesisCreateObjects(node_layout);
	}
	else
	{
		emStatusPrintf("Import Failed: Unable to read file.");
	}
	StructDestroy(parse_GenesisZoneNodeLayout, node_layout);
}

static void terrainGenesisImportNodeLayoutCB(void *unused, TerrainDoc *doc)
{
	UIWindow *browser;
	const char *start_dir;

	start_dir = EditorPrefGetString(TER_ED_NAME, "Genesis", "NodeLayoutName", "maps");
	browser = ui_FileBrowserCreate("Import a Node Layout...", "Import", UIBrowseExisting, UIBrowseFiles, false,
								   "maps", start_dir, NULL, ".nodelayout", NULL, NULL, terrainGenesisImportNodeLayoutSelectCB, NULL);
	ui_WindowShow(browser);
}

/*
static void terrainGenesisExportNodeLayoutSelectCB(const char *path, const char *filename, void *unused)
{
	char fullfilename[MAX_PATH];
	GenesisZoneNodeLayout *node_layout;

	sprintf(fullfilename, "%s\\%s", path, filename);

	EditorPrefStoreString(TER_ED_NAME, "Genesis", "NodeLayoutName", path);

	node_layout = genesisCreateNodeLayoutFromWorld();
	ParserWriteTextFile(fullfilename, parse_GenesisZoneNodeLayout, node_layout, 0, 0);
	Alertf("%d nodes, %d connections saved.", eaSize(&node_layout->nodes), eaSize(&node_layout->connections));
	StructDestroy(parse_GenesisZoneNodeLayout, node_layout);
}

static void terrainGenesisExportNodeLayoutCB(void *unused, TerrainDoc *doc)
{
	UIWindow *browser;
	const char *start_dir;

	terrainGenesisLoadLayerObjects();

	start_dir = EditorPrefGetString(TER_ED_NAME, "Genesis", "NodeLayoutName", "maps");
	browser = ui_FileBrowserCreate("Save a Node Layout...", "Export", UIBrowseNewOrExisting, UIBrowseFiles, false,
		"maps", start_dir, ".nodelayout", NULL, NULL, terrainGenesisExportNodeLayoutSelectCB, NULL);
	ui_WindowShow(browser);
}*/

static bool terrainGenesisGeotypeSelectedCB(EMPicker *picker, EMPickerSelection **selections, TerrainDoc *doc)
{
	if (eaSize(&selections) == 1)
	{
		GenesisGeotype *geotype;
		char final_name[256];
		strcpy(final_name, getFileName(selections[0]->doc_name));
		*strrchr(final_name, '.') = '\0';
		printf("Geotype: %s\n", final_name);
		geotype = RefSystem_ReferentFromString(GENESIS_GEOTYPE_DICTIONARY, final_name);

		if (geotype)
		{
			genesisMoveNodesToDesign_Temp(doc->state.task_queue, doc->state.source, geotype, TERRAIN_TASK_SHOW_PROGRESS | TERRAIN_TASK_UNDOABLE);
			return true;
		}
	}
	return true;
}

static void terrainGenesisMoveToDesignCB(void *unused, TerrainDoc *doc)
{
	terrainGenesisLoadLayerObjects();
	emPickerShow(doc->terrain_ui->geotype_picker, "Load", false, terrainGenesisGeotypeSelectedCB, doc);
}

void terEdFinishMouseUp(TerrainDoc *doc);

static int terrainGenesisCountObjects(GroupDef *root_def, GroupDef **def)
{
	GroupDef *obj_def = objectLibraryGetGroupDef(root_def->name_uid, false);
	if (obj_def)
	{
		if (!*def)
			*def = obj_def;
		return 1;
	}
	else
	{
		int i, count = 0;
		for (i = 0; i < eaSize(&root_def->children); i++)
		{
			GroupDef *child_def = groupChildGetDef(root_def, root_def->children[i], false);
			if (child_def)
				count += terrainGenesisCountObjects(child_def, def);
		}
		return count;
	}
}

static void terrainGetObjectCountCB(UIButton *button, TerrainDoc *doc)
{
	char buffer[10240];
	char buf[256];
	int total_count = 0;
	strcpy(buffer, "Object counts:\n\n");
	if (doc && doc->state.source)
	{
		int i, j;
		for (i = 0; i < eaSize(&doc->state.source->layers); i++)
		{
			TerrainEditorSourceLayer *layer = doc->state.source->layers[i];
			for (j = 0; j < eaSize(&layer->dummy_layer->terrain.object_defs); j++)
			{
				GroupDef *def = NULL;
				int count = terrainGenesisCountObjects(layer->dummy_layer->terrain.object_defs[j]->root_def, &def);
				if (def)
				{
					sprintf(buf, "%s: %d\n", def->name_str, count);
					strcat(buffer, buf);
					total_count += count;
				}
			}
		}
	}
	sprintf(buf, "Genesis Object Summary: %d total objects\n", total_count);
	printf("%s%s", buf, buffer);
	ui_ModalDialog(buf, buffer, ColorBlack, UIOk);
}

bool terrainGenesisEcosystemSelectedCB(EMPicker *picker, EMPickerSelection **selections, TerrainDoc *doc)
{
	if (eaSize(&selections) == 1)
	{
		GenesisEcosystem *ecosystem;
		char final_name[256];
		strcpy(final_name, getFileName(selections[0]->doc_name));
		*strrchr(final_name, '.') = '\0';
		printf("Ecosystem: %s\n", final_name);
		ecosystem = RefSystem_ReferentFromString(GENESIS_ECOTYPE_DICTIONARY, final_name);

		if (ecosystem)
		{
			GenesisToPlaceState to_place = { 0 };
			if (doc->state.genesis_create_detail_objects)
			{
				genesisMakeDetailObjects(&to_place, ecosystem, NULL, false, true);
				terEdDeleteAndPopulateGroup(doc->state.source, &to_place, false, true);
			}

			genesisMoveDesignToDetail(doc->state.task_queue, doc->state.source,
				ecosystem, TERRAIN_TASK_SHOW_PROGRESS | TERRAIN_TASK_UNDOABLE,
				NULL, NULL);
		}
	}
	return true;
}

static void terrainGenesisMoveToDetailCB(void *unused, TerrainDoc *doc)
{
	emPickerShow(doc->terrain_ui->ecosystem_picker, "Load", false, terrainGenesisEcosystemSelectedCB, doc);
}

static void terrainGenesisCreateDetailObjectsCB(UICheckButton *check, TerrainDoc *doc)
{
	doc->state.genesis_create_detail_objects = ui_CheckButtonGetState(check);
}

void terEdCreateGenesisPanel(TerrainDoc *doc)
{
	EMPanel *panel;
	UIButton *button;
	UICheckButton *check;
	F32 y = 5;

	panel = emPanelCreate("Brush", "Genesis", 0);
	doc->terrain_ui->genesis_panel = panel;

	button = ui_ButtonCreate("Import Map Description...", 10, y, terrainGenesisImportMapDescCB, doc);
	ui_WidgetSetWidth(UI_WIDGET(button), 235);
	emPanelAddChild(panel, button, true);
	doc->terrain_ui->genesis_import_mapdesc_button = button;
	y += 25;
	button = ui_ButtonCreate("Import Node Layout...", 10, y, terrainGenesisImportNodeLayoutCB, doc);
	ui_WidgetSetWidth(UI_WIDGET(button), 235);
	emPanelAddChild(panel, button, true);
	doc->terrain_ui->genesis_import_nodelayout_button = button;
	/*y += 25;
	button = ui_ButtonCreate("Export Node Layout...", 10, y, terrainGenesisExportNodeLayoutCB, doc);
	ui_WidgetSetWidth(UI_WIDGET(button), 235);
	emPanelAddChild(panel, button, true);
	doc->terrain_ui->genesis_export_nodelayout_button = button;*/
	y += 35;


	button = ui_ButtonCreate("Move to Design", 10, y, terrainGenesisMoveToDesignCB, doc);
	ui_WidgetSetWidth(UI_WIDGET(button), 235);
	emPanelAddChild(panel, button, true);
	doc->terrain_ui->genesis_move_to_design_button = button;
	y += 25;
	button = ui_ButtonCreate("Move to Detail", 10, y, terrainGenesisMoveToDetailCB, doc);
	ui_WidgetSetWidth(UI_WIDGET(button), 235);
	emPanelAddChild(panel, button, true);
	doc->terrain_ui->genesis_move_to_detail_button = button;
	y += 25;
	check = ui_CheckButtonCreate(10, y, "Generate Walls & Water", false);
	ui_CheckButtonSetToggledCallback(check, terrainGenesisCreateDetailObjectsCB, doc);
	emPanelAddChild(panel, check, true);

	y += 35;
	button = ui_ButtonCreate("Object Counts", 10, y, terrainGetObjectCountCB, doc);
	ui_WidgetSetWidth(UI_WIDGET(button), 235);
	emPanelAddChild(panel, button, true);

	terEdRefreshUI(doc);
}

void terrainBakeTrackerIntoTerrain(GroupTracker *tracker)
{
	int i;
	Mat4 mat;
	TerrainDoc *doc = terEdGetDoc();
	VaccuformObject **objects = NULL;

	if (!doc || !tracker->def)
		return;

	trackerGetMat(tracker, mat);

	terrainQueueFindObjectsToVaccuformInDef(tracker->parent_layer, tracker->def, mat, &objects);
	for (i = 0; i < eaSize(&objects); i++)
		terrainQueueVaccuformObject(doc->state.task_queue, doc->state.source, objects[i], TERRAIN_TASK_SHOW_PROGRESS | TERRAIN_TASK_UNDOABLE, false);
	eaDestroyEx(&objects, NULL);

	terrainQueueFinishTask(doc->state.task_queue, NULL, NULL);
}


static DWORD WINAPI terrainEditorThreadProc( LPVOID lpParam )
{
	EXCEPTION_HANDLER_BEGIN
        
    while (true)
    {
		TerrainDoc *doc = terEdGetDoc();
        if (!doc)
		{
			SleepEx(500, TRUE);
            continue;
		}
        terEdDoImportReload(doc);
		if (!doc->state.import_info)
			terrainQueueDoActions(doc->state.task_queue, true, doc->state.source, terrain_state.show_terrain_occlusion);
        SleepEx(50, TRUE);
    }
	EXCEPTION_HANDLER_END
	return 0;
}

#endif
AUTO_COMMAND;
void terrainEditorToggleThreaded(void)
{
#ifndef NO_EDITORS
    bTerrainEditorThreaded = !bTerrainEditorThreaded;
    if (!terrainEditorThread && bTerrainEditorThreaded)
	{
		terrainEditorThread = tmCreateThread(terrainEditorThreadProc, NULL);
	}
	else if (terrainEditorThread && !bTerrainEditorThreaded)
	{
		tmDestroyThread(terrainEditorThread, true);
		terrainEditorThread = NULL;
		printf("Killed thread.\n");
	}
#endif
}
#ifndef NO_EDITORS

void terEdDeleteAndPopulateGroup(TerrainEditorSource *source, GenesisToPlaceState *to_place, bool nodes, bool detail)
{
	int i, j;
	TrackerHandle *handle = NULL;
	GroupTracker *parent, *default_parent = NULL;
	assert(nodes ^ detail);
	for (i = 0; i < eaSize(&source->layers); i++)
	{
		if (source->layers[i]->effective_mode == LAYER_MODE_EDITABLE)
		{
			GroupTracker *layer_tracker = layerGetTracker(source->layers[i]->layer);
			for (j = 0; j < layer_tracker->child_count; j++)
			{
				WorldGenesisProperties *props = SAFE_MEMBER(layer_tracker->children[j]->def, property_structs.genesis_properties);
				if (props && ((nodes && props->bNode) || (detail && props->bDetail)))
				{
					handle = trackerHandleCopy(trackerHandleFromTracker(layer_tracker->children[j]));
				}
			}
			if (!default_parent)
				default_parent = layer_tracker;
						
		}
	}
	if (handle)
	{
		GroupTracker *tracker = trackerFromTrackerHandle(handle);
		TrackerHandle **handle_list = NULL;

		parent = tracker->parent;

		// Delete old group
		eaPush(&handle_list, handle);
		wleOpDelete(handle_list);
		eaDestroyEx(&handle_list, trackerHandleDestroy);
	}
	else
	{
		parent = default_parent;
	}

	if (default_parent && to_place->objects)
	{
		F32 scale;
		GroupDef **defs = NULL;
		TrackerHandle **newHandles = NULL;
		TrackerHandle *root_handle;
		Mat4 id_mat;
		GroupDef *autogen_def;
		GroupDef *root_def = parent->def;
		GroupDefLib *def_lib;
		char def_name[256];

		// Create a root def
		def_lib = root_def->def_lib;
		groupLibMakeGroupName(def_lib, (nodes ? "GENESIS_NODES" : "GENESIS_DETAIL"), SAFESTR(def_name), 0);
		autogen_def = groupLibNewGroupDef(def_lib, NULL, 0, def_name, 0, false, true);
		if(autogen_def->property_structs.genesis_properties)
			autogen_def->property_structs.genesis_properties = StructCreate(parse_WorldGenesisProperties);
		if(nodes)
			autogen_def->property_structs.genesis_properties->bNode = 1;
		else 
			autogen_def->property_structs.genesis_properties->bDetail = 1;
		groupDefModify(autogen_def, UPDATE_GROUP_PROPERTIES, true);

		// Place all the objects inside our root def (deletes the contents of to_place)
		genesisPlaceObjects(zmapGetInfo(NULL), to_place, autogen_def);

		root_handle = trackerHandleCreate(parent);

		// Place the root def
		identityMat4(id_mat);
		scale = 1;
		eaPush(&defs, autogen_def);
		wleOpCreateEx(root_handle, defs, (Mat4*)&id_mat[0][0], &scale, -1, &newHandles);

		eaDestroyEx(&newHandles, trackerHandleDestroy);
		trackerHandleDestroy(root_handle);
		edObjSelectionClear(EDTYPE_NONE);
	}
}

void terEdFinishMouseUp(TerrainDoc *doc)
{
    int l;
	TrackerHandle **handle_list=NULL;
	TerrainEditorSourceLayer *objects_layer = NULL;

    doc->state.source->lock_cursor_position = false;

    // Update trackers with physics
    for (l = 0; l < eaSize(&doc->state.source->layers); l++)
    {
        terrainSourceUpdateAllObjects(doc->state.source->layers[l], false, true);
        terrainSourceClearTouchedBits(doc->state.source->layers[l]);
    }
    terrainSourceUpdateTrackers(doc->state.source, true, true);

	terrainQueueClearMouseUp(doc->state.task_queue);

	wleSetDocSavedBit();
}

static bool bPreviewOverride = false;

AUTO_COMMAND;
void editorShowTerrainPreview(bool enabled)
{
	bPreviewOverride = enabled;
}

// Called once per frame while the terrain editor is running
void terEdOncePerFrame( F32 delta_time )
{
	int i, l;
	TerrainDoc *doc = terEdGetDoc();
	static bool bClientOnlyMode = false;
	bool lockedLayer = false;

	PERFINFO_AUTO_START_FUNC();

	if (doc && doc->state.source)
	{
		if (eaSize(&doc->state.persistent->objectList) != eaSize(&doc->state.source->object_table))
		{
			terrainLock();
			eaDestroyStruct(&doc->state.persistent->objectList, parse_TerrainObjectEntry);
			eaCopyStructs(&doc->state.source->object_table, &doc->state.persistent->objectList, parse_TerrainObjectEntry);
			terrainUnlock();
		}
		if (eaSize(&doc->state.persistent->materialsList) != eaSize(&doc->state.source->material_table))
		{
			terrainLock();
			eaDestroyEx(&doc->state.persistent->materialsList, NULL);
			for (i = 0; i < eaSize(&doc->state.source->material_table); ++i)
			{
				eaPush(&doc->state.persistent->materialsList, strdup(doc->state.source->material_table[i]));
			}
			terrainUnlock();
		}

		if(terrainQueueIsEmpty(doc->state.task_queue))
		{
			eaDestroy(&doc->state.source->loaded_objects);
			doc->state.source->loaded_objects = NULL;
		}
		for( i=0; i < eaSize(&doc->state.source->loaded_objects); i++ )
		{
			GroupDef *def = doc->state.source->loaded_objects[i];
			if(def->model)
				modelLODLoadAndMaybeWait(def->model, 0, true);
		}
	}
	else if (doc)
	{
		eaDestroyStruct(&doc->state.persistent->objectList, parse_TerrainObjectEntry);
		eaDestroyEx(&doc->state.persistent->materialsList, NULL);
	}

	if (doc && doc->state.source)
	{
		doc->state.editable = terEdCheckForLayerUpdates(doc);
		doc->base_doc.saved = true;

		// Update edges
		for (l = 0; l < eaSize(&doc->state.source->layers); l++)
			for (i = 0; i < eaSize(&doc->state.source->layers[l]->heightmap_trackers); i++)
				terEdHighlightBlocks(doc->state.source->layers[l]->heightmap_trackers[i]);

		if (terrainGetProcessMemory() > TERRAIN_MAX_MEMORY && !terrainOverrideMemoryLockout)
			doc->state.memory_limit = true;
		else
			doc->state.memory_limit = false;

		if (doc->state.genesis_preview_flag &&
			eaSize(&doc->state.source->layers) > 0 &&
			doc->state.source->layers[0]->loaded)
		{
			terrainQueueClear(doc->state.task_queue);
			genesisExteriorPaintTerrain(PARTITION_CLIENT, doc->state.source->layers[0]->layer->zmap_parent,
				doc->state.source->layers[0], doc->state.task_queue, TERRAIN_TASK_UNDOABLE | TERRAIN_TASK_SHOW_PROGRESS, true);
			doc->state.genesis_preview_flag = false;
		}

		if (eaSize(&doc->state.source->finish_save_layers) > 0)
        {
            TerrainEditorSourceLayer *layer;
            terrainQueueLock();
            layer = eaRemove(&doc->state.source->finish_save_layers, 0);
            terrainQueueUnlock();
            terrainSourceFinishSaveLayer(layer);

			if (--layer->source->unlock_on_save == 0)
			{
				for (i = 0; i < eaSize(&layer->source->layers); i++)
				{
					TerrainEditorSourceLayer *other_layer = layer->source->layers[i];
					if (other_layer->layer->unlock_on_save)
					{
						other_layer->layer->unlock_on_save = false;
						wleUISetLayerMode(other_layer->layer, other_layer->layer->target_mode, false);
					}
				}
			}
		}
        if (!terrainEditorThread)
        {
	        terEdDoImportReload(doc);    
			if (!doc->state.import_info)
				terrainQueueDoActions(doc->state.task_queue, false, doc->state.source, terrain_state.show_terrain_occlusion);
        }

        terEdUpdateLayerButtons(doc);
        
        if (terrainQueueNeedsUpdate(doc->state.task_queue))
        {
            terrainSourceUpdateTrackers(doc->state.source, false, false);
            terrainQueueLock();
            terrainQueueClearUpdate(doc->state.task_queue);
            terrainQueueUnlock();
        }
        else if (terrainQueueNeedsMouseUp(doc->state.task_queue))
        {
			terEdFinishMouseUp(doc);
        }

        for (i = 0; i < eaSize(&doc->state.source->layers); i++)
		{
            if (doc->state.source->layers[i]->effective_mode == LAYER_MODE_EDITABLE)
			{
				if (!doc->state.source->layers[i]->saved)
	            {
					doc->base_doc.saved = false;
				}
				if (eaSize(&doc->state.source->layers[i]->blocks) > 0)
					lockedLayer = true;
            }
		}
	}

	if (zmapInfoHasGenesisData(NULL) && zmapGetGenesisEditType(NULL) == GENESIS_EDIT_EDITING)
		lockedLayer = true;

	if (bPreviewOverride)
	{
		lockedLayer = false;
		terrain_state.source_data = NULL;
	}
	else
	{
		terrain_state.source_data = doc ? doc->state.source : NULL;
	}

	if (lockedLayer && !bClientOnlyMode)
	{
		bClientOnlyMode = true;
		globCmdParse("Physics.NoSync 1");
	}
	if (!lockedLayer && bClientOnlyMode)
	{
		bClientOnlyMode = false;
		globCmdParse("Physics.NoSync 0");
	}
	{
		int width, height;
		gfxfont_SetFontEx(&g_font_Sans, 0, 1, 1, 0, 0xBB0000FF, 0xBB0000FF);
		gfxGetActiveSurfaceSize(&width, &height);
		if (isProductionEditMode())
		{
			if (bClientOnlyMode)
				gfxfont_Printf(width / 2, 50, 100, 1, 1, CENTER_X, "EDITING MAP");
			else if (!g_ui_State.bInUGCEditor) // Don't show text if actually in the editor, for example editing costumes
				gfxfont_Printf(width / 2, 50, 100, 1, 1, CENTER_X, "PREVIEWING MAP");
		}
		else if (bClientOnlyMode && !emIsEditorActive())
			gfxfont_Printf(width / 2, 50, 100, 1, 1, CENTER_X, "PREVIEW MODE");
		if(editorUIState && editorUIState->showingScratchLayer)
			gfxfont_Printf(width / 2, 140, 100, 1, 1, CENTER_X, "SHOWING SCRATCH LAYER");
	}

	PERFINFO_AUTO_STOP();
}

//// Task callbacks & undo/redo

void terEdWaitForQueuedEvents(TerrainDoc *doc)
{
	TerrainTaskQueue *queue;
	if (!doc)
		doc = terEdGetDoc();
	queue = doc->state.task_queue;
	while (eaSize(&queue->edit_tasks) > 0)
	{
		SleepEx(10, TRUE);
		terEdOncePerFrame(0.0f);
	}
}

void terrainUndoAction(TerrainEditorSource *source, TerrainChangeList *change_list)
{
	TerrainDoc *doc = terEdGetDoc();
	terrainQueueClear(doc->state.task_queue);
	terEdWaitForQueuedEvents(doc);
	terrainQueueUndo(doc->state.task_queue, change_list, TERRAIN_TASK_SHOW_PROGRESS);
	//printf("terrainUndoAction %X\n", (int)(intptr_t)change_list);
}

void terrainRedoAction(TerrainEditorSource *source, TerrainChangeList *change_list)
{
	TerrainDoc *doc = terEdGetDoc();
	terrainQueueClear(doc->state.task_queue);
	terEdWaitForQueuedEvents(doc);
	terrainQueueRedo(doc->state.task_queue, change_list, TERRAIN_TASK_SHOW_PROGRESS);
	//printf("terrainRedoAction %X\n", (int)(intptr_t)change_list);
}

void terrainFreeUndo(TerrainEditorSource *source, TerrainChangeList *change_list)
{
	iTerrainMemoryUsage -= terrainChangeListGetMemorySize(change_list);
	//printf("terrainFreeUndo %X\n", (int)(intptr_t)change_list);
	if (--change_list->ref_count == 0)
	{
		//printf("terrainSourceChangeListDestroy %X\n", (int)(intptr_t)change_list);
		terrainSourceChangeListDestroy(change_list);
	}
}

void terrainEditorTaskInit(TerrainTask *new_task, TerrainSubtask *first_subtask, int flags)
{
	TerrainDoc *doc = terEdGetDoc();
	assert(doc);

	if (flags & TERRAIN_TASK_SHOW_PROGRESS)
	{
		const char *label = terrainQueueGetSubtaskLabel(first_subtask);
		new_task->progress_id = progressOverlayCreate(0, label);
	}
	else
		new_task->progress_id = -1;

    if (flags & TERRAIN_TASK_UNDOABLE)
    {
        new_task->undo_id = -2;
    }
	else
	{
		new_task->undo_id = -1;
	}
} 

void terrainEditorSubtaskInit(TerrainTask *task, TerrainSubtask *subtask, int flags)
{
	S32 subtask_count = progressOverlayGetSize(task->progress_id);
	progressOverlaySetSize(task->progress_id, subtask_count+1);
}

void terrainEditorTaskBegin(TerrainTask *task)
{
	TerrainDoc *doc = terEdGetDoc();
	assert(doc);

	//printf("Starting task.\n");
	if (task->undo_id == -2)
	{
		//printf("Creating undo!\n");
		task->undo_id = EditCreateUndoCustom(doc->state.undo_stack, terrainUndoAction, terrainRedoAction, terrainFreeUndo, task->change_list);
		task->change_list->ref_count++;
        assert(task->undo_id >= 0);
	}
}

void terrainEditorTaskComplete(TerrainTask *task)
{
	//printf("Completed task.\n");
	progressOverlayRelease(task->progress_id);
	task->progress_id = -1;
	if (task->undo_id >= 0)
	{
		// Change list is already in the undo queue - don't let the task free it
		iTerrainMemoryUsage += terrainChangeListGetMemorySize(task->change_list);
		if (--task->change_list->ref_count == 0)
		{
			//printf("terrainSourceChangeListDestroy %X\n", (int)(intptr_t)task->change_list);
			terrainSourceChangeListDestroy(task->change_list);
		}
		task->change_list = NULL;
	}
	if (task->flags & TERRAIN_TASK_CONTROLLERSCRIPT)
	{
		ControllerScript_Succeeded();
	}
}

void terrainEditorSubtaskComplete(TerrainTask *task, TerrainSubtask *subtask)
{
	progressOverlayIncrement(task->progress_id, 1);
}

void terrainUpdateTaskMemory()
{
    /*int i, size = 0;
    for (i = 0; i < eaSize(&terrain_tasks); i++)
    {
        size += terrainQueueGetTaskMemory(terrain_tasks[i]);
    }
	iTerrainMemoryUsage = size;*/
    //printf("Task stack memory: %d\n", size);
}

/*void terrainQueueUndoTask(TerrainEditorSource *source, TerrainTask *task)
{
    TerrainDoc *doc = terEdGetDoc();
	TerrainTaskQueue *queue = doc->state.task_queue;
    terrainQueueLock();
    if (!task->in_queue)
    {
        // Task is no longer in our queue
        //printf("REINSERT UNDO %d\n", task->undo_id);
        task->in_queue = true;
        task->queue_undo = true;
        assert(task->change_list);
        eaPush(&queue->edit_tasks, task);
    }
    else
    {
        // Task is still in our queue
        if (task->started)
        {
            //printf("IN-PLACE UNDO %d\n", task->undo_id);
			task->queue_undo = true;
            task->completed = true;
            eaDestroyEx(&task->subtasks, terrainQueueFreeSubtask);
			terrainEditorTaskComplete(task);
        }
        else
        {
            int idx;
            // Just remove it
            //printf("REMOVE UNDO %d\n", task->undo_id);
            for (idx = 0; idx < eaSize(&queue->edit_tasks); idx++)
                if (queue->edit_tasks[idx] == task)
                {
            		eaRemove(&queue->edit_tasks, idx);
                    break;
                }
            eaDestroyEx(&task->subtasks, terrainQueueFreeSubtask);
            task->in_queue = false;
            EditUndoCompleteTransient(doc->state.undo_stack, task->undo_id);
			terrainEditorTaskComplete(task);
        }
    }
    terrainQueueUnlock();
}

void terrainQueueRedoTask(TerrainEditorSource *source, TerrainTask *task)
{
    TerrainDoc *doc = terEdGetDoc();
	TerrainTaskQueue *queue = doc->state.task_queue;
    terrainQueueLock();
    assert(doc);
    eaPush(&queue->edit_tasks, task);
    task->in_queue = true;
    task->queue_redo = true;
    terrainQueueUnlock();
    //printf("REDOING TASK %d\n", task->undo_id);
}*/

// Brush-related functions

void terEdUseEyeDropper(TerrainEditorState* state, F32 x, F32 z)
{
	RefDictIterator it;
	TerrainDefaultBrush *brush;

	if(!state->selected_brush)
		return;

	RefSystem_InitRefDictIterator(DEFAULT_BRUSH_DICTIONARY, &it);
	while(brush = (TerrainDefaultBrush*)RefSystem_GetNextReferentFromIterator(&it)) 
	{
		switch(state->selected_brush->brush_template.channel)
		{
		case TBC_Height:
			if(stricmp(brush->name, "Flatten") == 0)
			{
				terEdSetBrushHeight(state, brush, x, z);
				return;
			}
			break;
		case TBC_Color:
			if(stricmp(brush->name, "Color") == 0)
			{
				terEdSetBrushColor(state, brush, x, z);
				return;
			}
			break;
		case TBC_Material:
			if(stricmp(brush->name, "Material") == 0)
			{
				terEdSetBrushMaterial(state, brush, x, z);
				return;
			}
			break;
		case TBC_Object:
			if(stricmp(brush->name, "Object") == 0)
			{
				terEdSetBrushObject(state, brush, x, z);
				return;
			}
			break;
		case TBC_Soil:
			if(stricmp(brush->name, "Soil Set") == 0)
			{
				terEdSetBrushSoilDepth(state, brush, x, z);
				return;
			}
			break;
		default:
			return;
		}
	}
}

void terEdCompileBrush(TerrainDoc *doc)
{
    //printf("Recompiling brush!\n");
    if (!doc->state.compiled_multibrush)
	{
        doc->state.compiled_multibrush = calloc(1, sizeof(TerrainCompiledMultiBrush));
		if (!doc->state.compiled_multibrush)
			return;
	}

	terrainBrushCompile(doc->state.compiled_multibrush, doc->state.selected_brush,
		(doc->state.persistent && doc->state.persistent->expanded_multi_brush) ? doc->state.persistent->expanded_multi_brush : NULL);

	if(doc->terrain_ui && doc->terrain_ui->filters_vis_refresh_button)
		ui_WidgetSetFont(UI_WIDGET(doc->terrain_ui->filters_vis_refresh_button), "TerrainEditor_NeedsUpdate");

}

F32 terEdGetFilterVisValue(TerrainEditorState* state, F32 x, F32 y)
{
	int j;
	if(eaSize(&state->compiled_multibrush->brushes) > 0)
	{
		F32 value = 1.0f;
		TerrainCompiledBrushOp **brush_ops;
		TerrainBrushFilterBuffer filter = { 0 };
		TerrainBrushFalloff falloff_values = { 0 };
		TerrainBrushState brush_state = { 0 };
		bool uses_color = state->compiled_multibrush->brushes[0]->uses_color;
		filter.buffer = &value;
		filter.height = 1;
		filter.width = 1;
		filter.x_offset = x;
		filter.y_offset = y;
		filter.rel_x_cntr = x;
		filter.rel_y_cntr = y;
		filter.lod = GET_COLOR_LOD(state->source->visible_lod);        
		filter.invert = (!state->invert_filters != !state->compiled_multibrush->brushes[0]->falloff_values.invert_filters); // Logical XOR

		brush_state.visible_lod = state->source->visible_lod;

		//Do Optimized Filters
		brush_ops = state->compiled_multibrush->brushes[0]->bucket[TBK_OptimizedFilter].brush_ops;
		if(eaSize(&brush_ops) > 0)
			terEdApplyOptimizedBrush(state->source, &brush_state, brush_ops, &filter, &falloff_values, x, y, TBS_Square, TE_FALLOFF_SCURVE, false, false, uses_color, true);

		//Do Normal Filters
		brush_ops = state->compiled_multibrush->brushes[0]->bucket[TBK_RegularFilter].brush_ops;
		for(j=0; j < eaSize(&brush_ops); j++)
		{
			((terrainRegularBrushFunction)(brush_ops[j]->draw_func))(state->source, &brush_state, brush_ops[j]->values_copy, &filter, brush_ops[j]->channel, &falloff_values, x, y, TBS_Square, TE_FALLOFF_SCURVE, false, false, uses_color);
		}

		if (brush_state.curve_list)
		{
			eaDestroyEx(&brush_state.curve_list->curves, splineDestroy);
			eafDestroy(&brush_state.curve_list->lengths);
			SAFE_FREE(brush_state.curve_list);
			brush_state.curve_list = NULL;
		}
		if (brush_state.blob_list_inited)
		{
			eaDestroyEx(&brush_state.blob_list, NULL);
			brush_state.blob_list_inited = false;
		}
		return value;
	}
	return 0.0f;
}

TerrainCommonBrushParams *terEdGetBrushParams(TerrainDoc *doc, TerrainCommonBrushParams *params)
{
	TerrainCommonBrushParams *common_params = calloc(1, sizeof(TerrainCommonBrushParams));
    if (params)
    {
        memcpy(common_params, params, sizeof(TerrainCommonBrushParams));
    }
    else
    {
        if (doc->state.selected_brush)
            memcpy(common_params, &doc->state.selected_brush->common, sizeof(TerrainCommonBrushParams));
        else
            memcpy(common_params, &doc->state.multi_brush_common, sizeof(TerrainCommonBrushParams));
        common_params->invert_filters = doc->state.invert_filters;
		common_params->lock_edges = doc->state.lock_edges;
	}
	return common_params;
}

TerrainCompiledMultiBrush *terEdGetMultibrush(TerrainDoc *doc, TerrainCompiledMultiBrush *multibrush)
{
	if (multibrush)
		return terEdCopyCompiledMultiBrush(multibrush);
	return terEdCopyCompiledMultiBrush(doc->state.compiled_multibrush);
}

void terEdQueueFillWithBrush(UIButton *button, void *unused)
{
	TerrainDoc *doc = terEdGetDoc();     
	if (doc && doc->state.compiled_multibrush && doc->state.compiled_multibrush->brushes != NULL)
	{
		bool optimize = true;
        int iterations, i;
        iterations = ui_SliderTextEntryGetValue(doc->terrain_ui->apply_iterations_slider);
        if (iterations <= 0)
            return;
		for( i=0; i < eaSize(&doc->state.compiled_multibrush->brushes); i++ )
		{
			TerrainCompiledBrush *brush = doc->state.compiled_multibrush->brushes[i];
			if(	eaSize(&brush->bucket[TBK_RegularFilter].brush_ops) > 0 ||
				eaSize(&brush->bucket[TBK_RegularBrush].brush_ops) > 0 )
			{
				optimize = false;
				break;
			}
		}
        for (i = 0; i < iterations; i++)
        {
			terrainQueueFillWithMultiBrush(doc->state.task_queue, 
				terEdGetMultibrush(doc, NULL), terEdGetBrushParams(doc, NULL), 
				optimize, doc->state.source->visible_lod, doc->state.keep_high_res, 
				TERRAIN_TASK_SHOW_PROGRESS | TERRAIN_TASK_UNDOABLE);
        }
        terrainQueueFinishTask(doc->state.task_queue, NULL, NULL);
	}
}

void terEdQueueStitchNeighbors(UIButton *button, void *unused)
{
	TerrainDoc *doc = terEdGetDoc();     
	if (doc)
	{
		terrainQueueStitchNeighbors(doc->state.task_queue, doc->state.source->visible_lod, TERRAIN_TASK_SHOW_PROGRESS | TERRAIN_TASK_UNDOABLE);
        terrainQueueFinishTask(doc->state.task_queue, NULL, NULL);
	}
}

#endif
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Terrain.CancelAction");
void terrainEditorCancelAction()
{
#ifndef NO_EDITORS
    TerrainDoc *doc = terEdGetDoc();
    if (doc)
    {
        if (doc->state.using_eye_dropper)
        {
            doc->state.using_eye_dropper = false;
            return;
        }
        terrainQueueClear(doc->state.task_queue);
    }
#endif
}
#ifndef NO_EDITORS

void terEdRefreshUI(TerrainDoc *doc)
{
	int i;
	//Panels
	eaClear(&doc->base_doc.em_panels);

	//eaPush(&doc->base_doc.em_panels, doc->terrain_ui->panel_layers);
	eaPush(&doc->base_doc.em_panels, doc->terrain_ui->panel_occlusion);
	eaPush(&doc->base_doc.em_panels, doc->terrain_ui->panel_options);
	if(doc->terrain_ui->genesis_panel)
		eaPush(&doc->base_doc.em_panels, doc->terrain_ui->genesis_panel);

	if(terrain_state.view_params->view_mode == TE_Extreme_Angles)
		eaPush(&doc->base_doc.em_panels, doc->terrain_ui->panel_extreme_angle_vis);
	else if(terrain_state.view_params->view_mode == TE_Material_Weight)
		eaPush(&doc->base_doc.em_panels, doc->terrain_ui->panel_materials_vis);
	else if(terrain_state.view_params->view_mode == TE_Object_Density)
		eaPush(&doc->base_doc.em_panels, doc->terrain_ui->panel_objects_vis);
	else if(terrain_state.view_params->view_mode == TE_Filters)
		eaPush(&doc->base_doc.em_panels, doc->terrain_ui->panel_filters_vis);

    eaPush(&doc->base_doc.em_panels, doc->terrain_ui->panel_actions);
    
	if(doc->state.seleted_image)
		eaPush(&doc->base_doc.em_panels, doc->terrain_ui->panel_image_trans);

	if(doc->state.selected_brush)
		eaPush(&doc->base_doc.em_panels, doc->terrain_ui->panel_global_filters);
	else
		eaPush(&doc->base_doc.em_panels, doc->terrain_ui->panel_multi_brush);

    eaPush(&doc->base_doc.em_panels, doc->terrain_ui->panel_rivers);
	
	//Toolbars
	eaClear(&terrain_editor.toolbars);
	for(i=0; i < eaSize(&doc->terrain_ui->system_tool_bars); i++)
	{
		eaPush(&terrain_editor.toolbars, doc->terrain_ui->system_tool_bars[i]);
	}
	eaPush(&terrain_editor.toolbars, global_terrain_ui->tool_bar_brush);

	if(doc->state.selected_brush && doc->state.selected_brush->toolbar)
	{
		if(doc->state.selected_brush->toolbar)
			eaPush(&terrain_editor.toolbars, doc->state.selected_brush->toolbar);
	}

	terEdRefreshBrushUI(doc);
}

void heightMapDrawFiltersVisFunction(HeightMap *height_map, U8 *data, int display_size, int lod)
{
	F32 x, y;
	int i, j;
	IVec2 map_local_pos;
	Vec3 world_pos;
	TerrainDoc *doc = terEdGetDoc();
	F32 interp=0.0f;

	if(heightMapGetMapLocalPos(height_map, map_local_pos))
	{
		world_pos[0] = map_local_pos[0] * GRID_BLOCK_SIZE;
		world_pos[1] = 0;
		world_pos[2] = map_local_pos[1] * GRID_BLOCK_SIZE;
	}
	else
	{
		doc = NULL;
	}

	if(doc)
		interp = terrain_state.view_params->view_mode_interp;

	for(j=0; j < display_size; j++)
	{
		for(i=0; i < display_size; i++)
		{
			F32 value = 0.0f;
			if(doc)
			{
				x = world_pos[0] + (i<<lod);
				y = world_pos[2] + (j<<lod);
				value = terEdGetFilterVisValue(&doc->state, x, y);
			}

			value = (1.0f-value);
			data[(i+j*display_size)*3+0] = (data[(i+j*display_size)*3+0]*value*interp) + value*255*(1-interp);
			data[(i+j*display_size)*3+1] = (data[(i+j*display_size)*3+1]*interp) + 255.0f*(1-interp);
			data[(i+j*display_size)*3+2] = (data[(i+j*display_size)*3+2]*value*interp) + value*255*(1-interp);
		}
    }
}

static void terrainViewModeUpdateShaders(void)
{
	static bool terrain_shaders_in_view_mode=false;
	if ((terrain_state.view_params->view_mode!=TE_Regular) != terrain_shaders_in_view_mode)
	{
		if (terrain_state.view_params->view_mode!=TE_Regular)
		{
			ShaderTestN(3, "TERRAINNX_COLOR");
		} else {
			ShaderTestN(3, NULL);
		}
		terrain_shaders_in_view_mode = (terrain_state.view_params->view_mode!=TE_Regular);
	}
}

static void terrainViewModesCB(UIComboBox *combo, int i, TerrainDoc *doc)
{
	terrain_state.view_params->view_mode = i;
	terrainViewModeUpdateShaders();
	terrain_state.view_params->filter_cb = heightMapDrawFiltersVisFunction;
	terEdRefreshUI(doc);
    terrainSourceRefreshHeightmaps(doc->state.source, true);

	if(i == TE_Filters)
        ui_WidgetSetFont(UI_WIDGET(doc->terrain_ui->filters_vis_refresh_button), NULL);
}

static void terrainViewModeAngleCB(UITextEntry *entry, TerrainDoc *doc)
{
	char buf[20];
	F32 value;

	if (sscanf(ui_TextEntryGetText(entry), "%f", &value))
		terrain_state.view_params->walk_angle = CLAMP(value, 0, 90);

	sprintf(buf, "%d", terrain_state.view_params->walk_angle);
	ui_TextEntrySetText(entry, buf);

	if(terrain_state.view_params->view_mode == TE_Extreme_Angles)
        terrainSourceRefreshHeightmaps(doc->state.source, true);
}

static void terrainViewModeFiltersCB(UIButton *button, TerrainDoc *doc)
{
    terrainSourceRefreshHeightmaps(doc->state.source, true);
    ui_WidgetSetFont(UI_WIDGET(button), NULL);
}

static void terrainViewModeMaterialCB(UIComboBox *combo, TerrainDoc *doc)
{
	int value;
	char *mat_name = ui_ComboBoxGetSelectedObject(combo);

	if(mat_name)
	{
		value = terrainSourceGetMaterialIndex(doc->state.source, mat_name, false);
		terrain_state.view_params->material_type = value;

		if(terrain_state.view_params->view_mode == TE_Material_Weight)
            terrainSourceRefreshHeightmaps(doc->state.source, true);
    }
}

static void terrainViewModeObjectsCB(UIComboBox *combo, TerrainDoc *doc)
{
	int value;
	TerrainObjectEntry *object = ui_ComboBoxGetSelectedObject(combo);

	if(object)
	{
		value = terrainSourceGetObjectIndex(doc->state.source, object->objInfo.name_uid, 0, false);
		terrain_state.view_params->object_type = value;

		if(terrain_state.view_params->view_mode == TE_Object_Density)
            terrainSourceRefreshHeightmaps(doc->state.source, true);
    }
}

static void terEdViewInterpSliderCB(UISliderTextEntry *slider_entry, bool bFinished, TerrainDoc *doc)
{
	terrain_state.view_params->view_mode_interp = ui_SliderTextEntryGetValue(slider_entry);
	EditorPrefStoreFloat(TER_ED_NAME, "GlobalOptions", "ViewModeInterp", terrain_state.view_params->view_mode_interp);
    terrainSourceRefreshHeightmaps(doc->state.source, true);
}


void terEdToggleHideObjectsCB(UICheckButton *button, TerrainDoc *doc)
{
    int i;
	if(!doc->state.source)
		return;
    doc->state.source->objects_hidden = ui_CheckButtonGetState(button);
    for (i = 0; i < eaSize(&doc->state.source->layers); i++)
        terrainSourceUpdateAllObjects(doc->state.source->layers[i], true, true);
}

AUTO_COMMAND;
void terrainForceObjectRefresh(void)
{
	int i;
	TerrainDoc *doc = terEdGetDoc();
	if (!doc)
		return;
    for (i = 0; i < eaSize(&doc->state.source->layers); i++)
        terrainSourceUpdateAllObjects(doc->state.source->layers[i], true, true);
	ControllerScript_Succeeded();
}

#endif
AUTO_COMMAND ACMD_CATEGORY(World);
void terrainClose(void)
{
#ifndef NO_EDITORS
	emCloseDoc(emGetActiveEditorDoc());
#endif
}
#ifndef NO_EDITORS

void terEdToggleKeepHighResCB(UICheckButton *button, TerrainDoc *doc)
{
	doc->state.keep_high_res = ui_CheckButtonGetState(button);
}

void terEdToggleHideDetailCB(UICheckButton *button, TerrainDoc *doc)
{
	gfxSetHideDetailInEditorBit(ui_CheckButtonGetState(button));
}

void terEdToggleTerrainStateCB(UICheckButton *button, bool *val)
{
	(*val) = ui_CheckButtonGetState(button);
}

void terEdToggleTerrainShowErodeCB(UICheckButton *button, bool *val)
{
	(*val) = ui_CheckButtonGetState(button);
	EditorPrefStoreInt(TER_ED_NAME, "GlobalOptions", "ShowErode", (*val));
}

void terEdToggleTerrainHideEdgesCB(UICheckButton *button, bool *val)
{
	(*val) = ui_CheckButtonGetState(button);
	EditorPrefStoreInt(TER_ED_NAME, "GlobalOptions", "HideEdges", (*val));
}

void terEdToggleTerrainInverseFilterCB(UICheckButton *button, bool *val)
{
	TerrainDoc *doc = terEdGetDoc();
	if(!doc)
		return;

	(*val) = ui_CheckButtonGetState(button);
	terEdCompileBrush(doc);
}

void terEdToggleEdgeLockCB(UICheckButton *button, TerrainDoc *doc)
{
	doc->state.lock_edges = ui_CheckButtonGetState(button);
}

void terEdToggleHideEdgesCB(UICheckButton *button, TerrainDoc *doc)
{
	doc->state.hide_edges = ui_CheckButtonGetState(button);
}

void terEdToggleShowErodeCB(UICheckButton *button, TerrainDoc *doc)
{
	doc->state.show_erode = ui_CheckButtonGetState(button);
}

EMPanel *terEdCreateExtremeAnglesVisPanel(TerrainDoc *doc)
{
	EMPanel *panel;
	UILabel *label;
	UITextEntry *entry;

	panel = emPanelCreate("Brush", "Extreme Angles Visualization", 0);

	label = ui_LabelCreate("Angle:", 10, 0);
	emPanelAddChild(panel, label, true);

	entry = ui_TextEntryCreate("45", 110, 0);
	ui_WidgetSetDimensions(&entry->widget, 140, 20);
	terrain_state.view_params->view_mode = 0;
	terrainViewModeUpdateShaders();
	terrain_state.view_params->walk_angle = 45;
	ui_TextEntrySetFinishedCallback(entry, terrainViewModeAngleCB, doc);
	emPanelAddChild(panel, entry, true);

	doc->terrain_ui->panel_extreme_angle_vis = panel;
	return panel;
}

EMPanel *terEdCreateFiltersVisPanel(TerrainDoc *doc)
{
	EMPanel *panel;
	UIButton *button;

	panel = emPanelCreate("Brush", "Active Filters Visualization", 0);

	button = ui_ButtonCreate("Refresh Visualization", 10, 0, terrainViewModeFiltersCB, doc);
	ui_WidgetSetDimensions(&button->widget, 235, 20);
	doc->terrain_ui->filters_vis_refresh_button = button;
	emPanelAddChild(panel, button, true);

	doc->terrain_ui->panel_filters_vis = panel;
	return panel;
}

EMPanel *terEdCreateMaterialsVisPanel(TerrainDoc *doc)
{
	EMPanel *panel;
	UILabel *label;
	UIComboBox *combo;

	panel = emPanelCreate("Brush", "Materials Visualization", 0);

	label = ui_LabelCreate("Material:", 10, 0);
	emPanelAddChild(panel, label, true);

	combo = ui_ComboBoxCreate(110, 0, 140, NULL, &doc->state.persistent->materialsList, NULL);
	ui_ComboBoxSetSelectedCallback(combo, terrainViewModeMaterialCB, doc);
	emPanelAddChild(panel, combo, true);
	doc->terrain_ui->material_vis_combo = combo;

	doc->terrain_ui->panel_materials_vis = panel;
	return panel;
}

EMPanel *terEdCreateObjectsVisPanel(TerrainDoc *doc)
{
	EMPanel *panel;
	UILabel *label;
	UIComboBox *combo;

	panel = emPanelCreate("Brush", "Terrain Objects Visualization", 0);

	label = ui_LabelCreate("Objects:", 10, 0);
	emPanelAddChild(panel, label, true);

	combo = ui_ComboBoxCreate(110, 0, 140, NULL, &doc->state.source->object_table, NULL);
	ui_ComboBoxSetTextCallback(combo, terrainObjectsComboMakeText, NULL);
	ui_ComboBoxSetSelectedCallback(combo, terrainViewModeObjectsCB, doc);
	emPanelAddChild(panel, combo, true);
	doc->terrain_ui->object_vis_combo = combo;

	doc->terrain_ui->panel_objects_vis = panel;
	return panel;
}

EMPanel *terEdCreateOptionsPanel(TerrainDoc *doc)
{
	int y;
	EMPanel *panel;
	UICheckButton *check;
	UILabel *label;
	UIComboBox *combo;
	UISliderTextEntry *slider_entry;

	panel = emPanelCreate("Brush", "Global Options", 0);

	y=0;

	label = ui_LabelCreate("View Mode:", 10, y);
	emPanelAddChild(panel, label, true);

	{
		combo = ui_ComboBoxCreate(110,y,140,NULL,NULL,NULL);
		ui_ComboBoxSetEnum(combo, TerrainEditorViewModesEnum, terrainViewModesCB, doc);
		ui_ComboBoxSetSelectedEnum(combo, TE_Regular);
		emPanelAddChild(panel, combo, true);
		doc->terrain_ui->view_mode_combo = combo; 
	}

	y+=25;

	label = ui_LabelCreate("Multiply Percent:", 10, y);
	emPanelAddChild(panel, label, true);

	slider_entry = ui_SliderTextEntryCreate("", 0.0, 1.0, 110, y, 140);
	ui_WidgetSetPosition(UI_WIDGET(slider_entry), 110, y);
	ui_WidgetSetHeight(UI_WIDGET(slider_entry), 20);
	ui_SliderTextEntrySetRange(slider_entry, 0.0, 1.0, 0.01);
	ui_SliderTextEntrySetAsPercentage(slider_entry, true);
	ui_SliderTextEntrySetChangedCallback(slider_entry, terEdViewInterpSliderCB, doc);
	ui_SliderTextEntrySetValueAndCallback(slider_entry, EditorPrefGetFloat(TER_ED_NAME, "GlobalOptions", "ViewModeInterp", 0.0f));
	ui_WidgetSetTooltipString(UI_WIDGET(slider_entry), "0% = Display only the visualization's color.<br>100% = Multiply the color of the visualization with the true color.");
	emPanelAddChild(panel, slider_entry, true);

	y+=30;

	check = ui_CheckButtonCreate(10, y, "Hide Detail", false);
	ui_CheckButtonSetState(check, gfxGetHideDetailInEditorBit());
	ui_CheckButtonSetToggledCallback(check, terEdToggleHideDetailCB, doc);
	emPanelAddChild(panel, check, true);

	check = ui_CheckButtonCreate(150, y, "Hide Edges", false);
	ui_CheckButtonSetToggledCallback(check, terEdToggleTerrainHideEdgesCB, &doc->state.hide_edges);
	ui_CheckButtonSetStateAndCallback(check, EditorPrefGetInt(TER_ED_NAME, "GlobalOptions", "HideEdges", false));
	emPanelAddChild(panel, check, true);

	y+=20;

	check = ui_CheckButtonCreate(10, y, "Hide Objects", false);
	ui_CheckButtonSetToggledCallback(check, terEdToggleHideObjectsCB, doc);
	emPanelAddChild(panel, check, true);
    doc->terrain_ui->hide_objects_check = check;

	check = ui_CheckButtonCreate(150, y, "Show Erosion", false);
	ui_CheckButtonSetToggledCallback(check, terEdToggleTerrainShowErodeCB, &doc->state.show_erode);
	ui_CheckButtonSetStateAndCallback(check, EditorPrefGetInt(TER_ED_NAME, "GlobalOptions", "ShowErode", true));
	emPanelAddChild(panel, check, true);

	y+=30;

	check = ui_CheckButtonCreate(10, y, "Lock Edges", false);
	ui_CheckButtonSetToggledCallback(check, terEdToggleTerrainStateCB, &doc->state.lock_edges);
	emPanelAddChild(panel, check, true);

	check = ui_CheckButtonCreate(150, y, "Invert Filters", false);
	ui_CheckButtonSetToggledCallback(check, terEdToggleTerrainInverseFilterCB, &doc->state.invert_filters);
	emPanelAddChild(panel, check, true);

	y+=20;

	check = ui_CheckButtonCreate(10, y, "Retain High Res Height Data", false);
	ui_CheckButtonSetToggledCallback(check, terEdToggleKeepHighResCB, doc);
	ui_CheckButtonSetStateAndCallback(check, false);
	emPanelAddChild(panel, check, true);

	doc->terrain_ui->panel_options = panel;
	return panel;
}

void terEdToggleOcclusionVisible(UICheckButton *check, void *usedata)
{
    terrain_state.show_terrain_occlusion = ui_CheckButtonGetState(check);
}

EMPanel *terEdCreateOcclusionPanel(TerrainDoc *doc)
{
    int y;
    EMPanel *panel;
    UICheckButton *check;
    
    panel = emPanelCreate("Brush", "Occlusion", 0);

    y = 0;

    check = ui_CheckButtonCreate(10, y, "Edit Occlusion Mesh", false);
    ui_CheckButtonSetToggledCallback(check, terEdToggleOcclusionVisible, NULL);
    emPanelAddChild(panel, check, true);
    
	doc->terrain_ui->panel_occlusion = panel;
    return panel;    
}

static void terrainBrushImagesFolderCacheCB(const char* relpath, int when)
{
	int i;
	TerrainDoc *doc = terEdGetDoc();
	TerrainEditorSource *source;
	if(!doc)
		return;
	source = doc->state.source;
	if(!source)
		return;

	terrainQueueLock();
	for( i=0; i < eaSize(&source->brush_images) ; i++ )
	{
		TerrainImageBuffer *brush_image = source->brush_images[i];
		if(stricmp(brush_image->file_name, relpath) == 0)
		{
			brush_image->needs_reload++;
			break;
		}
	}
	terrainQueueUnlock();

	fileWaitForExclusiveAccess( relpath );
}

static char* terrainMaterialPickerFilter( const char* path )
{
	const MaterialData *material_data;
	char material_name[ MAX_PATH ];

	if(strstri(path, "/Templates/" ))
		return NULL;

	getFileNameNoExt(material_name, path);
	material_data = materialFindData(material_name);
	if(!material_data)
		return NULL;

	if (strEndsWith(material_data->material_name, "_default"))
		return NULL;

	if (stricmp(material_data->graphic_props.default_fallback.shader_template_name, "TerrainMaterial")!=0) 
		return NULL;

	return StructAllocString( material_name );
}

static EMEditorDoc *terrainNewDoc(const char *type, void *unused)
{
	TerrainDoc *new_doc = calloc(1, sizeof(TerrainDoc));
	ZoneMap *zmap = worldGetPrimaryMap();
	int i;

    assert(global_terrain_doc == NULL);
    global_terrain_doc = new_doc;

	if(global_terrain_state == NULL)
		global_terrain_state = calloc(1, sizeof(TerrainEditorPersistentState));
	new_doc->state.persistent = global_terrain_state;
	
	// Fill Multibrush Common Params
	terEdFillCommonBrushParams(&new_doc->state.multi_brush_common, MULTI_BRUSH_NAME);

	// Create Image Gizmo
	terEdInitImageGizmo(new_doc);

	new_doc->state.source = terrainSourceInitialize();
	terrain_state.source_data = new_doc->state.source;

	if (isProductionEditMode() && zmap)
	{
		genesisRebuildLayers(PARTITION_CLIENT, zmap, false);
	}
    
	terrain_state.view_params = calloc(1, sizeof(GfxTerrainViewMode));

	// Create Undo Stacks
	new_doc->state.image_undo_stack = EditUndoStackCreate();
	new_doc->state.undo_stack = EditUndoStackCreate();
	EditUndoSetContext(new_doc->state.undo_stack, new_doc->state.source);
	new_doc->base_doc.edit_undo_stack = new_doc->state.undo_stack;

	new_doc->state.river_point_selected = -1;
	new_doc->state.has_focus = true;

	// Create task queue
	new_doc->state.task_queue = terrainQueueCreate();
	new_doc->state.task_queue->new_task_cb = terrainEditorTaskInit;
	new_doc->state.task_queue->new_subtask_cb = terrainEditorSubtaskInit;
	new_doc->state.task_queue->begin_task_cb = terrainEditorTaskBegin;
	new_doc->state.task_queue->finish_task_cb = terrainEditorTaskComplete;
	new_doc->state.task_queue->finish_subtask_cb = terrainEditorSubtaskComplete;

	// Fill our Op Names List
	terEdFillOpNamesList(new_doc);

	strcpy(new_doc->base_doc.doc_display_name, "Terrain Editor");

	// Create UI
	if(global_terrain_ui == NULL)
	{
		global_terrain_ui = calloc(1, sizeof(TerrainUI));
		new_doc->terrain_ui = global_terrain_ui;

		// Fill system toolbars list
		for(i=0; i < eaSize(&terrain_editor.toolbars); i++)
		{
			eaPush(&global_terrain_ui->system_tool_bars, terrain_editor.toolbars[i]);
		}

		terEdInitBrushUI(new_doc);
	}
	else
	{
		new_doc->terrain_ui = global_terrain_ui;

		// Refresh Brush Toolbar UI
		terEdRefreshDefaultBrushesUI();
	}

	// Create Material Picker
	new_doc->terrain_ui->material_picker = emEasyPickerCreate( "Material", ".Material", "materials/", terrainMaterialPickerFilter );
	emEasyPickerSetColorFunc( new_doc->terrain_ui->material_picker, mated2MaterialPickerColor );
	emEasyPickerSetTexFunc( new_doc->terrain_ui->material_picker, mated2MaterialPickerPreview );

	// Create genesis pickers
	new_doc->terrain_ui->ecosystem_picker = emEasyPickerCreate("Ecosystem", ".ecosystem", "genesis/ecosystems/", NULL);
	new_doc->terrain_ui->geotype_picker = emEasyPickerCreate("Geotype", ".geotype", "genesis/geotypes/", NULL);

	// Create memory usage bar
	new_doc->terrain_ui->mem_progress = ui_ProgressBarCreate(0, 0, 200);
	new_doc->terrain_ui->mem_progress->widget.pOverrideSkin = NULL;
	new_doc->terrain_ui->mem_progress->widget.width = 600;
	setVec4(new_doc->terrain_ui->mem_progress->widget.color[0].rgba, 0x80, 0x80, 0x80, 0x40);
	setVec4(new_doc->terrain_ui->mem_progress->widget.color[1].rgba, 0, 0xff, 0, 0x60);
	ui_ProgressBarSet(new_doc->terrain_ui->mem_progress, 0);

	// Create Non-Persistent Panels
	//terEdCreateLayersPanel(new_doc);
	terEdCreateGenesisPanel(new_doc);
    terEdCreateOcclusionPanel(new_doc);
	terEdCreateOptionsPanel(new_doc);
	terEdCreateExtremeAnglesVisPanel(new_doc);
	terEdCreateFiltersVisPanel(new_doc);
	terEdCreateMaterialsVisPanel(new_doc);
	terEdCreateObjectsVisPanel(new_doc);
    terEdCreateRiversPanel(new_doc);

	// Create Popup Windows
	terEdCreateChangeResolutionWindow(new_doc);
	terEdCreateBlockSelectWindow(new_doc);

	// Show UI
	terEdRefreshUI(new_doc);

	// Populate the layers, select the first one, update granularity list if necessary

    terEdUpdateLayerButtons(new_doc);

	new_doc->base_doc.saved = true;

    if (!terrainEditorThread && bTerrainEditorThreaded)
		terrainEditorThread = tmCreateThread(terrainEditorThreadProc, NULL);

	terrainSelectBrush("Add");

	return &new_doc->base_doc;
}

static bool terrainCloseDocCheck(EMEditorDoc *doc_in, bool quitting)
{
	return 1;
}

static void terrainDestroyBrushStringRef(TerrainBrushStringRef *str_ref)
{
	StructDestroy(parse_TerrainBrushStringRef, str_ref);
}

static void terrainCloseDoc(EMEditorDoc *doc_in)
{
	int i;
	TerrainDoc *doc = (TerrainDoc*)doc_in;
	ZoneMapLayer **layers = NULL;
	ASSERT_CALLED_IN_SINGLE_THREAD;

	deinit_rivers(doc);

	//Delete Gizmos
	if(doc->state.image_rotate_gizmo)
		RotateGizmoDestroy(doc->state.image_rotate_gizmo);
	if(doc->state.image_translate_gizmo)
		TranslateGizmoDestroy(doc->state.image_translate_gizmo);
	StructDestroy(parse_TerrainBrushValues, doc->state.image_orig_vals);

	//Delete Non-Persistent Panels
	//emPanelFree(doc->terrain_ui->panel_layers);
	emPanelFree(doc->terrain_ui->panel_occlusion);
	emPanelFree(doc->terrain_ui->panel_options);
	emPanelFree(doc->terrain_ui->panel_filters_vis);
	emPanelFree(doc->terrain_ui->panel_extreme_angle_vis);
	emPanelFree(doc->terrain_ui->panel_materials_vis);
	emPanelFree(doc->terrain_ui->panel_objects_vis);

	//Delete Windows
	ui_WindowFreeInternal(doc->terrain_ui->resolution_window);
	ui_WindowFreeInternal(doc->terrain_ui->select_block_window);

	//Delete Expanded Multibrush
	terEdExpandActiveMultiBrush(doc, NULL);
	terEdRefreshActiveMultiBrushUI(doc, NULL);

	//Delete Compiled Brush
	doc->state.selected_brush = NULL;
	doc->terrain_ui->filters_vis_refresh_button = NULL;
	terEdCompileBrush(doc);

	//Delete Undo Stacks
	EditUndoStackDestroy(doc->state.image_undo_stack);
	EditUndoStackDestroy(doc->state.undo_stack);

	// Delete material picker
	emEasyPickerDestroy(doc->terrain_ui->material_picker);

	// Delete genesis pickers
	emEasyPickerDestroy(doc->terrain_ui->ecosystem_picker);
	emEasyPickerDestroy(doc->terrain_ui->geotype_picker);

	//Delete Operation Names Lists
	for (i=0; i < TBC_NUM_CHANNELS-1; i++)
	{
		eaDestroyEx(&doc->state.channel_ops[i].op_refs, terrainDestroyBrushStringRef);
	}

	if (terrainEditorThread)
	{
		tmDestroyThread(terrainEditorThread, true);
		terrainEditorThread = NULL;
		printf("Killed thread.\n");
	}

	if (doc->state.source)
		terrainSourceDestroy(doc->state.source);
    doc->state.source = NULL;
	eaDestroyStruct(&doc->state.persistent->objectList, parse_TerrainObjectEntry);
	eaDestroyEx(&doc->state.persistent->materialsList, NULL);
	terrain_state.source_data = NULL;
    eaDestroy(&doc->base_doc.sub_docs);

    global_terrain_doc = NULL;

    SAFE_FREE(terrain_state.view_params);
    terrain_state.view_params = NULL;
}

static EMTaskStatus terrainSave(EMEditorDoc *doc)
{
	if (zmapIsSaving(NULL))
		return EM_TASK_INPROGRESS;
	if (zmapCheckFailedValidation(NULL))
		return EM_TASK_FAILED;
	wleCmdSave();
	return zmapIsSaving(NULL) ? EM_TASK_INPROGRESS : EM_TASK_SUCCEEDED;
}

static void terrainDrawGhosts(EMEditorDoc *doc_in)
{
}

bool gDebugHeightmap = false;

#endif
AUTO_COMMAND;
void terrainDebugHeightmap()
{
#ifndef NO_EDITORS
	gDebugHeightmap = true;
#endif
}
#ifndef NO_EDITORS

void terEdDumpDebugInfo(TerrainDoc *doc, Vec3 world_pos)
{
	//int i;
	IVec2 pos;
	HeightMap *height_map = NULL;
	//TerrainBuffer **buffers;

	pos[0] = floorf( world_pos[0] / (GRID_BLOCK_SIZE) );
	pos[1] = floorf( world_pos[2] / (GRID_BLOCK_SIZE) );

	height_map = terrainSourceGetHeightMap(doc->state.source, pos);
	if (!height_map)
	{
		printf("No heightmap found.\n");
		return;
	}

	printf("Height map at %d, %d.\n", pos[0], pos[1]);
	printf("Height map LODs: %d / %d\n", heightMapGetLevelOfDetail(height_map), heightMapGetLoadedLOD(height_map));
	/*buffers = heightMapGetBuffers(height_map);
	printf("Buffers (%d):\n", eaSize(&buffers));
	for (i = 0; i < eaSize(&buffers); i++)
	{
		printf("Buffer %d: %s (%d x %d) LOD %d\n", i, TerrainBufferGetTypeName(buffers[i]->type),
			buffers[i]->size, buffers[i]->size, buffers[i]->lod);
            }*/
}

void terEdDrawSlopeBrushPreview(TerrainSlopeBrushParams *params, TerrainCommonBrushParams *falloff)
{
    Vec3 draw_start;
    Vec3 draw_end;
    Vec3 slope_vec;
    F32 hardness = falloff->brush_hardness;
    F32 radius = falloff->brush_diameter/2.f;
                    
    //Calculate slope vector
    subVec3(params->brush_end_pos, params->brush_start_pos, slope_vec);
    normalVec3(slope_vec);

    //Main Line
    copyVec3(params->brush_start_pos, draw_start);
    copyVec3(params->brush_end_pos, draw_end);
    draw_start[1] += 2.0f;
    draw_end[1] += 2.0f;
    gfxDrawLine3DARGB(draw_start, draw_end, 0xffff0000 );

    //Start Point Bar
    draw_end[1] = draw_start[1];
    draw_end[0] = draw_start[0] - slope_vec[2]*radius*hardness;
    draw_end[2] = draw_start[2] + slope_vec[0]*radius*hardness;
    gfxDrawLine3DARGB(draw_start, draw_end, 0xffffff00 );
    draw_end[0] = draw_start[0] + slope_vec[2]*radius*hardness;
    draw_end[2] = draw_start[2] - slope_vec[0]*radius*hardness;
    gfxDrawLine3DARGB(draw_start, draw_end, 0xffffff00 );
    draw_end[0] = draw_start[0] - slope_vec[2]*radius;
    draw_end[2] = draw_start[2] + slope_vec[0]*radius;
    gfxDrawLine3DARGB(draw_start, draw_end, 0xffffffff );
    draw_end[0] = draw_start[0] + slope_vec[2]*radius;
    draw_end[2] = draw_start[2] - slope_vec[0]*radius;
    gfxDrawLine3DARGB(draw_start, draw_end, 0xffffffff );

    //End Point Bar
    copyVec3(params->brush_end_pos, draw_start);
    draw_start[1] += 2.0f;
    draw_end[1] = draw_start[1];
    draw_end[0] = draw_start[0] - slope_vec[2]*radius*hardness;
    draw_end[2] = draw_start[2] + slope_vec[0]*radius*hardness;
    gfxDrawLine3DARGB(draw_start, draw_end, 0xffffff00 );
    draw_end[0] = draw_start[0] + slope_vec[2]*radius*hardness;
    draw_end[2] = draw_start[2] - slope_vec[0]*radius*hardness;
    gfxDrawLine3DARGB(draw_start, draw_end, 0xffffff00 );
    draw_end[0] = draw_start[0] - slope_vec[2]*radius;
    draw_end[2] = draw_start[2] + slope_vec[0]*radius;
    gfxDrawLine3DARGB(draw_start, draw_end, 0xffffffff );
    draw_end[0] = draw_start[0] + slope_vec[2]*radius;
    draw_end[2] = draw_start[2] - slope_vec[0]*radius;
    gfxDrawLine3DARGB(draw_start, draw_end, 0xffffffff );
}

void genesisPathNodeGetPosition(GenesisNodeConnection *conn, F32 offset, Vec3 out_pos, Vec3 out_up, Vec3 out_tan);
//void genesisGetNodeObjectMatrix(NodeObject *object, Vec3 node_pos, Mat3 node_rot, GroupDef *def, Mat4 placement_mat);

void memoryBarDraw(TerrainDoc *doc)
{
	F32 w, h, x, y;
	F32 mem;
	S32 mbytes;

	mbytes = terrainGetProcessMemory();
	mem = (F32)mbytes / TERRAIN_MAX_MEMORY;
	mbytes /= 1048576;
	ui_ProgressBarSet(doc->terrain_ui->mem_progress, MIN(mem, 1.f));

	emGetCanvasSize(&x, &y, &w, &h);
	ui_StyleFontUse(NULL, false, kWidgetModifier_None);
	gfxfont_SetColor(ColorWhite, ColorWhite);

	if (mem < 0.6f)
		setVec4(doc->terrain_ui->mem_progress->widget.color[1].rgba, 0, 0xff, 0, 0x60);
	else if (mem < 0.8f)
		setVec4(doc->terrain_ui->mem_progress->widget.color[1].rgba, 0xff, 0xa0, 0, 0x60);
	else
		setVec4(doc->terrain_ui->mem_progress->widget.color[1].rgba, 0xff, 0, 0, 0x60);

	ui_ProgressBarDraw(doc->terrain_ui->mem_progress, (x+w/2)-300, y+h-16, 600, 16, 1.f);
	gfxfont_Printf((x+w/2)-300+5, y+h-16+8, (++g_ui_State.drawZ), 1.f, 1.f, CENTER_Y, 
		"Memory usage: %d of %d MB", mbytes, TERRAIN_MAX_MEMORY/1048576);

	if (mem >= 1.f)
	{
        int width, height;
        gfxfont_SetFontEx(&g_font_Sans, 0, 1, 1, 0, 0xBB0000FF, 0xBB0000FF);
        gfxGetActiveSurfaceSize(&width, &height);
        gfxfont_Printf(width / 2, 100, 1000000, 1, 1, CENTER_X, "MEMORY LIMIT EXCEEDED! PAINTING DISABLED.");
	}
 }

void genesisDrawNodeLayout(GenesisZoneNodeLayout *layout)
{
	int i, cg;
	Vec3 start, end;
	Vec3 nearest_hit_pos;
	F32 nearest_node_dist=15000;
	U32 nearest_uid = 0;
	bool node_hit = false;
	Color border_color;
	setVec4(border_color.rgba, 0, 100, 255, 255);

	editLibCursorRay(start, end);

	for( i=0; i < eaSize(&layout->nodes) ; i++ )
	{
		//int k, n;
		Vec3 hit_pos;
		GenesisNode *node = layout->nodes[i];
		if (node->node_type == GENESIS_NODE_Nature)
		{
			gfxDrawSphere3DARGB(node->pos, 50, 10, 0xff00ffff, 0);
			if(sphereLineCollisionWithHitPoint(start, end, node->pos, 50, hit_pos))
			{
				F32 hit_dist = distance3(hit_pos, start);
				node_hit = true;
				if(hit_dist < nearest_node_dist)
				{
					copyVec3(hit_pos, nearest_hit_pos);
					nearest_node_dist = hit_dist;
					nearest_uid = node->uid;
				}
			}
		}
		else
		{
			gfxDrawSphere3DARGB(node->pos, MAX(node->draw_size, 2.0f), 10, 0x6000ff00, 0);
			gfxDrawSphere3DARGB(node->pos, MAX(node->actual_size, 2.0f), 10, 0x30c0c000, 0);

			if(sphereLineCollisionWithHitPoint(start, end, node->pos, MAX(node->actual_size, 2.0f), hit_pos))
			{
				F32 hit_dist = distance3(hit_pos, start);
				node_hit = true;
				if(hit_dist < nearest_node_dist)
				{
					copyVec3(hit_pos, nearest_hit_pos);
					nearest_node_dist = hit_dist;
					nearest_uid = node->uid;
				}
			}
		}
		/* TomY TODO
		for (k = 0; k < eaSize(&node->objects); k++)
		{
		Mat3 rot;
		Mat4 placement_mat;
		GroupDef *def = objectLibraryGetGroupDefByName(node->objects[k]->name, true);
		if (def)
		{
		identityMat3(rot);
		for (j = 0; j < eaSize(&doc->state.source->node_layout->connections); j++)
		if (doc->state.source->node_layout->connections[j]->end_uid == node->uid)
		{
		for (n = 0; n < eaSize(&doc->state.source->node_layout->nodes); n++)
		if (doc->state.source->node_layout->nodes[n]->uid == doc->state.source->node_layout->connections[j]->start_uid)
		{
		subVec3(node->pos, doc->state.source->node_layout->nodes[n]->pos, rot[2]);
		break;
		}
		rot[2][1] = 0.f;
		normalVec3(rot[2]);
		break;
		}
		crossVec3(rot[1], rot[2], rot[0]);
		genesisGetNodeObjectMatrix(node->objects[k], node->pos, rot, def, placement_mat);
		gfxDrawBox3D(def->bounds.min, def->bounds.max, placement_mat, colorFromRGBA(0xffffffff), 0);
		}
		}*/

		if(node_hit)
		{
			Vec2 screen_pos;
			editLibGetScreenPos(nearest_hit_pos, screen_pos);
			gfxfont_SetColorRGBA(0xFFFF00FF, 0xFFFF00FF);
			gfxfont_Printf(screen_pos[0], screen_pos[1], 1, 1, 1, 0, "Node UID: %d", nearest_uid);
		}

	}
	for ( i=0; i < eaSize(&layout->node_borders); i++ )
	{
		int j;
		GenesisNodeBorder *border = layout->node_borders[i];
		Vec3 last_pos, this_pos;
		copyVec3(border->start, last_pos);
		copyVec3(border->start, this_pos);
		for ( j=0; j < eafSize(&border->heights); j++ )
		{
			if(border->horiz)
				this_pos[0] += border->step;
			else
				this_pos[2] += border->step;
			this_pos[1] = border->heights[j];
			gfxDrawCylinder3D(last_pos, this_pos, 10, 8, 1, border_color, 0);
			copyVec3(this_pos, last_pos);
		}
		copyVec3(border->end, this_pos);
		gfxDrawCylinder3D(last_pos, this_pos, 10, 8, 1, border_color, 0);
	}
	for ( cg=0; cg < eaSize(&layout->connection_groups); cg++ )
	{
		int k, l;
		GenesisNodeConnectionGroup *connection_group = layout->connection_groups[cg];
		for (k = 0; k < eaSize(&connection_group->missions); k++)
		{
			for (l = 0; l < eaSize(&connection_group->missions[k]->objects); l++)
			{
				Vec3 pos, up, tan;
				GenesisNodeObject *path_node = connection_group->missions[k]->objects[l];
				assert(path_node->path_idx < eaSize(&connection_group->connections));
				genesisPathNodeGetPosition(connection_group->connections[path_node->path_idx], path_node->offset, pos, up, tan);
				gfxDrawSphere3DARGB(pos, MAX(path_node->draw_size, 2.0f), 10, 0x600000ff, 0);
				gfxDrawSphere3DARGB(pos, MAX(path_node->actual_size, 2.0f), 10, 0x3000c0c0, 0);
			}
		}
		for( i=0; i < eaSize(&connection_group->connections) ; i++ )
		{
			GenesisNodeConnection *connection = connection_group->connections[i];
			splineUIDrawCurve( connection->path, false );
		}
	}
}

static void terrainDrawEditor(EMEditorDoc *doc_in)
{
    int i, j;
	TerrainDoc *doc = (TerrainDoc *)doc_in;
	static F32 saving_bar_val = 0.;
	ASSERT_CALLED_IN_SINGLE_THREAD;

	if(!doc->state.source) {
		Errorf("Error: Terrain Source is mising!");
		return;
	}

    bTerrainPaintingThisFrame = false;
	doc->state.last_cursor_heightmap = NULL;

	wleUIDrawCompass();
	progressOverlayDraw();

	memoryBarDraw(doc);
	if (doc->state.memory_limit)
		return; // Not allowed to do anything else

    if (doc->state.import_info)
    {
        int width, height;
        gfxfont_SetFontEx(&g_font_Sans, 0, 1, 1, 0, 0xBB0000FF, 0xBB0000FF);
        gfxGetActiveSurfaceSize(&width, &height);
        if (doc->state.color_import && !doc->state.height_import)
	        gfxfont_Printf(width / 2, 100, 1000000, 1, 1, CENTER_X, "IMPORTING COLOR CHANNEL");
        else if (doc->state.height_import && !doc->state.color_import)
	        gfxfont_Printf(width / 2, 100, 1000000, 1, 1, CENTER_X, "IMPORTING HEIGHT CHANNEL");
        else if (doc->state.height_import && doc->state.color_import)
	        gfxfont_Printf(width / 2, 100, 1000000, 1, 1, CENTER_X, "IMPORTING HEIGHT & COLOR CHANNELS");
    }

	if (doc->state.new_block_mode || doc->state.split_block_mode || doc->state.remove_block_mode)
	{
		IVec2 b_offset = { doc->state.select_block_dims[0]/GRID_BLOCK_SIZE, doc->state.select_block_dims[1]/GRID_BLOCK_SIZE };
		IVec2 b_size = { doc->state.select_block_dims[2]/GRID_BLOCK_SIZE, doc->state.select_block_dims[3]/GRID_BLOCK_SIZE };
        bool overlap = false;
        if (doc->state.new_block_mode)
        {
            overlap = terrainIsOverlappingBlock(b_offset, b_size);
            if (overlap)
                ui_LabelSetText(doc->terrain_ui->select_block_label, "ERROR: Overlapping Tile Groups!");
            else
                ui_LabelSetText(doc->terrain_ui->select_block_label, "Valid Tile Groups.");
        }
		terEdDrawNewBlock(zerovec3, doc->state.select_block_dims, !overlap);

		wleRayCollideUpdate();
		if (!inpCheckHandled())
		{
			static bool selecting = false;
			static Vec3 selection_start = {0,0,0};
			Vec3 ray_start, ray_end;
			Vec3 min, max;
			Vec3 coll_pos;

			setVec3(min, -15000, 0, -15000);
			setVec3(max,  15000, 0,  15000);
			editLibCursorRay(ray_start, ray_end);
			if (lineBoxCollision( ray_start, ray_end, min, max, coll_pos ))
			{
				if(mouseIsDown(MS_LEFT))
				{
					if(!selecting)
					{
						selecting = true;
						copyVec3(coll_pos, selection_start);
					}
					vec3MinMax(selection_start, coll_pos, min, max);
				}
				else
				{
					selecting = false;
					copyVec3(coll_pos, min);
					copyVec3(coll_pos, max);
				}

				min[0] = floor(min[0]/GRID_BLOCK_SIZE)*GRID_BLOCK_SIZE;
				min[2] = floor(min[2]/GRID_BLOCK_SIZE)*GRID_BLOCK_SIZE;
				max[0] = ceil(max[0]/GRID_BLOCK_SIZE)*GRID_BLOCK_SIZE;
				max[2] = ceil(max[2]/GRID_BLOCK_SIZE)*GRID_BLOCK_SIZE;

				if(selecting)
				{
					ui_SpinnerSetValueAndCallback(doc->terrain_ui->select_block_x, min[0]);
					doc->state.select_block_dims[0] = min[0];
					ui_SpinnerSetValueAndCallback(doc->terrain_ui->select_block_y, min[2]);
					doc->state.select_block_dims[1] = min[2];
					ui_SpinnerSetValueAndCallback(doc->terrain_ui->select_block_width, max[0]-min[0]);
					doc->state.select_block_dims[2] = max[0]-min[0];
					ui_SpinnerSetValueAndCallback(doc->terrain_ui->select_block_height, max[2]-min[2]);
					doc->state.select_block_dims[3] = max[2]-min[2];
				}

				terEdDrawNewBlockSelection(min, max, true);
			}
		}
		return;
    }

	//Handle selected image Gizmos
	if( doc->state.seleted_image && doc->state.seleted_image->image_ref )
	{	
		static Mat4 last_mat; 
		Vec3 pyr;
		TerrainBrushValues *values = doc->state.seleted_image;
		Color color;
		Vec3 min = { doc->state.seleted_image->image_ref->width*values->float_7/-2.0f, 0, doc->state.seleted_image->image_ref->height*values->float_7/-2.0f};
		Vec3 max = { doc->state.seleted_image->image_ref->width*values->float_7/2.0f,  1, doc->state.seleted_image->image_ref->height*values->float_7/2.0f};
		setVec4(color.rgba, 0, 0, 0, 255);

		if(doc->state.gizmo_mode == TGM_Translate)
		{
			TranslateGizmoUpdate(doc->state.image_translate_gizmo);
			TranslateGizmoDraw(doc->state.image_translate_gizmo);
			TranslateGizmoGetMatrix(doc->state.image_translate_gizmo, doc->state.gizmo_matrix);
			RotateGizmoSetMatrix(doc->state.image_rotate_gizmo, doc->state.gizmo_matrix);
		}
		else
		{
			RotateGizmoUpdate(doc->state.image_rotate_gizmo);
			RotateGizmoDraw(doc->state.image_rotate_gizmo);
			RotateGizmoGetMatrix(doc->state.image_rotate_gizmo, doc->state.gizmo_matrix);
			TranslateGizmoSetMatrix(doc->state.image_translate_gizmo, doc->state.gizmo_matrix);
		}

		if(	!nearSameVec4(doc->state.gizmo_matrix[0], last_mat[0]) ||
			!nearSameVec4(doc->state.gizmo_matrix[1], last_mat[1]) ||
			!nearSameVec4(doc->state.gizmo_matrix[2], last_mat[2]) ||
			!nearSameVec4(doc->state.gizmo_matrix[3], last_mat[3]) )
		{
			char buf[255];
			copyMat4(doc->state.gizmo_matrix, last_mat);

			values->float_1 = doc->state.gizmo_matrix[3][0];//X
			values->float_2 = doc->state.gizmo_matrix[3][1];//Y
			values->float_3 = doc->state.gizmo_matrix[3][2];//Z
			getMat3YPR(doc->state.gizmo_matrix, pyr);
			values->float_4 = pyr[0];//P
			values->float_5 = pyr[1];//Y
			values->float_6 = pyr[2];//R

			sprintf(buf, "%g", values->float_1);
			ui_TextEntrySetText(doc->terrain_ui->image_pos_x_text, buf);
			sprintf(buf, "%g", values->float_2);
			ui_TextEntrySetText(doc->terrain_ui->image_pos_y_text, buf);
			sprintf(buf, "%g", values->float_3);
			ui_TextEntrySetText(doc->terrain_ui->image_pos_z_text, buf);
			sprintf(buf, "%g", values->float_4*180.0f/PI);
			ui_TextEntrySetText(doc->terrain_ui->image_rot_p_text, buf);
			sprintf(buf, "%g", values->float_5*180.0f/PI);
			ui_TextEntrySetText(doc->terrain_ui->image_rot_y_text, buf);
			sprintf(buf, "%g", values->float_6*180.0f/PI);
			ui_TextEntrySetText(doc->terrain_ui->image_rot_r_text, buf);
		}

		gfxDrawBox3D(min, max, doc->state.gizmo_matrix, color, 1);
	}

	//Alpha Button
	if(doc->terrain_ui->channel_button_list[TBC_Alpha])
	{
		if(doc->state.source->visible_lod > 2)
		{
			if(doc->terrain_ui->channel_button_list[TBC_Alpha]->active)
				terrainSelectBrush("Add");
			ui_SetActive(UI_WIDGET(doc->terrain_ui->channel_button_list[TBC_Alpha]), false);
		}
		else
			ui_SetActive(UI_WIDGET(doc->terrain_ui->channel_button_list[TBC_Alpha]), true);
	}

	wleRayCollideUpdate();

	//Draw nodes if they exist
	{
		GenesisZoneNodeLayout *layout = genesisGetLastNodeLayout();
		if (layout)
		{
			genesisDrawNodeLayout(layout);
		}
	}

	if (!inpCheckHandled())
	{
		Vec3 start, end, coll_pos;

		editLibCursorRay(start, end);

		for (i = 0; i < eaSize(&doc->state.river_curves); i++)
			terrainUIDrawCurve(doc->state.river_curves[i], false);
		/*if (doc->state.river_mode)
		{
			if (mouseDown(MS_LEFT))
			{
				F32 collide_offset;
				Vec3 collide_pos;
                doc->state.river_point_selected = -1;
                for (i = 0; i < eaSize(&doc->state.river_curves); i++)
                {
                    int k;
                    RiverCurve *curve = doc->state.river_curves[i];
                    for (j = 0; j < eaiSize(&curve->point_indices); j++)
                    {
                        S32 idx = curve->point_indices[j];
                        Vec3 min = { -1, -1, -1 };
                        Vec3 max = { 1, 1, 1 };
                        Mat3 mat, mat_inv;
                        Vec3 start2, end2, temp;
                        Color color = { 255, 0, 255, 255 };
                        subVec3(&curve->l_points[idx*3], &curve->points[idx*3], mat[0]);
                        setVec3(mat[1], 0, 1, 0);
                        crossVec3(mat[0], mat[1], mat[2]);
                        normalVec3(mat[2]);
                        invertMat3(mat, mat_inv);
                        subVec3(start, &curve->points[idx*3], temp);
                        mulVecMat3(temp, mat_inv, start2);
                        subVec3(end, &curve->points[idx*3], temp);
                        mulVecMat3(temp, mat_inv, end2);
                        if (lineBoxCollision( start2, end2, min, max, temp ))
                        {
                            for (k = 0; k < eaSize(&doc->state.river_curves); k++)
                                doc->state.river_curves[k]->selected = (k == i);
                            doc->state.river_point_selected = j;
                            return;
                        }
                    }
                }
				for (i = 0; i < eaSize(&doc->state.river_curves); i++)
					if (splineCollideFull(start, end, &doc->state.river_curves[i]->curve->spline, &collide_offset, collide_pos, 10.f))
					{
						doc->state.river_curves[i]->selected = !doc->state.river_curves[i]->selected;
						break;
					}
			}
			return;
            }*/

        if (!doc->state.import_info)
        {
			bool valid_position = false;
			if (doc->state.source->lock_cursor_position)
			{
				copyVec3(doc->state.last_cursor_position, coll_pos);
				copyVec2(doc->state.mouse_pos, doc->state.last_mouse_pos);
				doc->state.mouse_pos[0] = g_ui_State.mouseX;
				doc->state.mouse_pos[1] = g_ui_State.mouseY;
				valid_position = true;
			}
			else if (terrainSourceDoRayCast(doc->state.source, start, end, coll_pos, &doc->state.last_cursor_heightmap) &&
				layerGetMode(heightMapGetLayer(doc->state.last_cursor_heightmap)) == LAYER_MODE_EDITABLE)
			{
				copyVec3(coll_pos, doc->state.last_cursor_position);
				doc->state.mouse_pos[0] = g_ui_State.mouseX;
				doc->state.mouse_pos[1] = g_ui_State.mouseY;
				copyVec2(doc->state.mouse_pos, doc->state.last_mouse_pos);
				valid_position = true;
			}

			// protect against background terrain queue processing clearing complete tasks/subtasks while
			// we draw the terrain editor brushes
			terrainQueueModifyQueueLock();
            for (i = 0; i < eaSize(&doc->state.task_queue->edit_tasks); i++)
            {
                for (j = 0; j < eaSize(&doc->state.task_queue->edit_tasks[i]->subtasks); j++)
                {
                    TerrainSubtask *action = doc->state.task_queue->edit_tasks[i]->subtasks[j];
                    if (action->op_type == TE_ACTION_PAINT)
                    {
                        Vec3 top = { action->PaintOp.world_pos[0], action->PaintOp.world_pos[1]+50.f, action->PaintOp.world_pos[2] };
                        gfxDrawLine3DARGB( action->PaintOp.world_pos, top, 0xff00ffff );
                    }
                    else if (action->op_type == TE_ACTION_SLOPE_BRUSH)
                    {
                        terEdDrawSlopeBrushPreview(action->SlopeBrushOp.params, doc->state.task_queue->edit_tasks[i]->common_params);
                    }
                }
            }
			terrainQueueModifyQueueUnlock();
            
			if(valid_position)
            {
                Vec3 cursor_world_pos;
                copyVec3(coll_pos, cursor_world_pos);
                terEdDrawCursor(&doc->state, cursor_world_pos);

				if (doc->state.slope_brush_params)
				{
					TerrainSlopeBrushParams *params = doc->state.slope_brush_params;
					copyVec3(coll_pos, params->brush_end_pos);

					if(!sameVec3(params->brush_start_pos, params->brush_end_pos))
					{
						terEdDrawSlopeBrushPreview(params, &doc->state.multi_brush_common);
					}
				}

                if ((doc->state.painting && mouseIsDown(MS_LEFT)) ||
                    mouseDown(MS_LEFT))
                {
                    if (gDebugHeightmap)
                    {
                        terEdDumpDebugInfo(doc, coll_pos);
                        gDebugHeightmap = false;
                    }
                    else if(doc->state.using_eye_dropper)
                    {
                        terEdUseEyeDropper(&doc->state, coll_pos[0], coll_pos[2]);
                        bTerrainPaintingThisFrame = true;
                        doc->state.painting = true;
                    }
                    else
                    {
                        U32 current_time = timeGetTime();
                        
                        if (!doc->state.slope_brush_params &&
                            terEdDoesBrushHaveSlope(doc->state.compiled_multibrush))
                        {
                            doc->state.slope_brush_params = calloc(1, sizeof(TerrainSlopeBrushParams));
                            copyVec3(coll_pos, doc->state.slope_brush_params->brush_start_pos);
                        }

                        if (current_time - iLastClickTime > MAX_BRUSH_RATE)
                        {
							iLastClickTime = current_time;
							if (doc->state.compiled_multibrush && doc->state.compiled_multibrush->brushes != NULL)
							{
								terrainQueuePaint(doc->state.task_queue, coll_pos, inpLevelPeek(INP_SHIFT), doc->state.painting,
									terEdGetMultibrush(doc, NULL), terEdGetBrushParams(doc, NULL), doc->state.source->visible_lod,
									doc->state.keep_high_res, TERRAIN_TASK_UNDOABLE | TERRAIN_TASK_SHOW_PROGRESS);
							}
                        }
                        
                        bTerrainPaintingThisFrame = true;
                        doc->state.painting = true;
                    }
                    inpHandled();
                }
            }
            else
            {
                if (mouseIsDown(MS_LEFT) && doc->state.editable)
                {
                    bTerrainPaintingThisFrame = true;
                    inpHandled();
                }
            }
        }
    }

	terrainQueueSetVerticalOffset(doc->state.task_queue, doc->state.last_mouse_pos[1] - doc->state.mouse_pos[1]);

    if (!bTerrainPaintingThisFrame && doc->state.painting)
	{
        if (doc->state.using_eye_dropper)
        {
            doc->state.using_eye_dropper = false;
        }
        else
        {
            if (doc->state.slope_brush_params)
			{
                terrainQueueSlopeBrush( doc->state.task_queue, doc->state.slope_brush_params,
									terEdGetBrushParams(doc, NULL),
									doc->state.source->visible_lod, doc->state.keep_high_res,
									TERRAIN_TASK_UNDOABLE | TERRAIN_TASK_SHOW_PROGRESS);
				doc->state.slope_brush_params = NULL;
			}
            terrainQueueFinishTask(doc->state.task_queue, NULL, NULL);
        }
        doc->state.painting = false;
    }
}

static void terrainEditorGotFocus(EMEditorDoc *doc_in)
{
	// global terrain locking flags
	TerrainDoc *doc = (TerrainDoc *)doc_in;
	if(doc->terrain_ui && doc->terrain_ui->brush_pane)
	{
		ui_PaneWidgetAddToDevice(UI_WIDGET(doc->terrain_ui->brush_pane), NULL);
		eaMove(ui_PaneWidgetGroupForDevice(NULL), eaSize(ui_PaneWidgetGroupForDevice(NULL)) - 1, 0);
	}
	doc->state.has_focus = true;
}

static void terrainEditorLostFocus(EMEditorDoc *doc_in)
{
	// global terrain locking flags
	TerrainDoc *doc = (TerrainDoc *)doc_in;
	if(doc->terrain_ui && doc->terrain_ui->brush_pane)
		ui_WidgetRemoveFromGroup(UI_WIDGET(doc->terrain_ui->brush_pane));
	doc->state.has_focus = false;
}

static void terrainEditorInit(EMEditor *editor)
{
	EMMenuItemDef menu_item_table[] =
	{
		{"em_newcurrented", "New terrain layer...", NULL, NULL, "Terrain.CreateNewLayer"},
	};

	// Create menus
	emMenuItemCreateFromTable(editor, menu_item_table, ARRAY_SIZE(menu_item_table));

	//Register Info Window Entries
	wleUIRegisterInfoWinEntries(editor);
	terEdUIRegisterInfoWinEntries(editor);

	terEdMultiBrushInit();
}

// Keybind commands

#endif
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Terrain.FocusCamera");
void terrainFocusCamera(void)
{
#ifndef NO_EDITORS
	int i;
	Vec3 local_min, local_max;
	GfxCameraController *camera = gfxGetActiveCameraController();
	float distance;
	ZoneMapLayer **layer_list = NULL;
	TerrainDoc *doc = terEdGetDoc();
	assert(doc);

    if (eaSize(&doc->state.source->layers) == 0)
        return;

	setVec3same(local_min, 8e16);
	setVec3same(local_max, -8e16);

	emPanelsGetMapSelectedLayers(&layer_list);
    for (i = 0; i < eaSize(&layer_list); i++)
    {
        TerrainEditorSourceLayer *layer = layer_list[i]->terrain.source_data;
		if (layer)
		{
			if (layer->effective_mode < LAYER_MODE_TERRAIN)
			{
				Vec3 layer_min, layer_max;
				layerGetTerrainBounds(layer->layer, layer_min, layer_max);
				vec3RunningMin(layer_min, local_min);
				vec3RunningMax(layer_max, local_max);
			}
			else
			{
				int block;
				for (block = 0; block < eaSize(&layer->blocks); ++block)
				{
					Vec3 block_min, block_max;
					block_min[0] = layer->blocks[block]->range.min_block[0]*GRID_BLOCK_SIZE;
					block_min[2] = layer->blocks[block]->range.min_block[2]*GRID_BLOCK_SIZE;
					block_max[0] = layer->blocks[block]->range.max_block[0]*GRID_BLOCK_SIZE;
					block_max[2] = layer->blocks[block]->range.max_block[2]*GRID_BLOCK_SIZE;
					vec3RunningMin(block_min, local_min);
					vec3RunningMax(block_max, local_max);
				}
				terrainSourceGetHeight(doc->state.source, local_min[0], local_min[2], &local_min[1], NULL );
				local_max[1] = local_min[1] + 10.f;
			}
		}
    }
	eaDestroy(&layer_list);

	distance = distance3(local_min, local_max);

	addVec3(local_min, local_max, local_min);
	scaleVec3(local_min, 0.5f, local_min);

	gfxCameraControllerSetTarget(camera, local_min);
	//camera->camdist = distance;
#endif
}

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Terrain.FocusCameraOnCursor");
void terrainFocusCameraOnCursor(void)
{
#ifndef NO_EDITORS
    Vec3 start, end;
    WorldCollCollideResults results;
	GfxCameraController *camera = gfxGetActiveCameraController();

    editLibCursorRay(start, end);

    if (worldCollideRay(PARTITION_CLIENT, start, end, WC_FILTER_BIT_HEIGHTMAP, &results) &&
        wcoGetUserPointer(results.wco, heightMapCollObjectMsgHandler, NULL))
    {
		gfxCameraControllerSetTarget(camera, results.posWorldImpact);
    }
#endif
}
#ifndef NO_EDITORS

//////////////////////////////////////////////////////////////////////////
// registration with editor manager

void terEdMapChanged(void *unused, bool is_reset)
{
    TerrainDoc *doc = terEdGetDoc();
    if (!doc)
        return;

    if (is_reset)
    	emForceCloseDoc(&doc->base_doc);
}

void terEdMapUnloading(void)
{
    TerrainDoc *doc = terEdGetDoc();
    if (!doc)
        return;

	if (terrainEditorThread)
	{
		tmDestroyThread(terrainEditorThread, true);
		terrainEditorThread = NULL;
		printf("Killed thread.\n");
	}

	terrainQueueLock();
	terEdExpandActiveMultiBrush(doc, NULL);
	terEdRefreshActiveMultiBrushUI(doc, NULL);
	if (doc->state.compiled_multibrush)
		terEdDestroyCompiledMultiBrush(doc->state.compiled_multibrush);
	doc->state.compiled_multibrush = NULL;
	terrainQueueUnlock();

	terrainSourceDestroy(doc->state.source);
	doc->state.source = NULL;
	eaDestroyStruct(&doc->state.persistent->objectList, parse_TerrainObjectEntry);
	eaDestroyEx(&doc->state.persistent->materialsList, NULL);
	terrain_state.source_data = NULL;
    eaDestroy(&doc->base_doc.sub_docs); 
}

#endif
AUTO_RUN;
int terrainRegister(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;

    strcpy(terrain_editor.editor_name, "Terrain Editor");
	terrain_editor.allow_multiple_docs = 0;
	terrain_editor.allow_save = 1;
	terrain_editor.disable_auto_checkout = 1;
    terrain_editor.always_open = 1;
	terrain_editor.hide_file_toolbar = 1;

	strcpy(terrain_editor.default_workspace, "Environment Editors");
    
	terrain_editor.init_func = terrainEditorInit;
	terrain_editor.new_func = terrainNewDoc;
	terrain_editor.close_check_func = terrainCloseDocCheck;
	terrain_editor.close_func = terrainCloseDoc;
	terrain_editor.custom_save_func = terrainSave;
	terrain_editor.draw_func = terrainDrawEditor;
	terrain_editor.ghost_draw_func = terrainDrawGhosts;
	terrain_editor.got_focus_func = terrainEditorGotFocus;
	terrain_editor.lost_focus_func = terrainEditorLostFocus;

	terrain_editor.keybinds_name = "TerrainEditor";
    terrain_editor.keybind_version = 1;

	terrain_editor.primary_editor = 0;

	emRegisterEditor(&terrain_editor);
	
	emRegisterFileType("tlayer", "Terrain Layer", "Terrain Editor");
	terrain_editor.default_type = "tlayer";

	emAddMapChangeCallback(terEdMapChanged, NULL);

	worldLibRegisterMapUnloadCallback(terEdMapUnloading);

	return 1;
#else
	return 0;
#endif
}
#ifndef NO_EDITORS

#endif
