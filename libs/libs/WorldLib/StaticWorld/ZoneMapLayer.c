/***************************************************************************



***************************************************************************/


#include "HogLib.h"
#include "timing.h"
#include "StringCache.h"
#include "serialize.h"
#include "utilitiesLib.h"
#include "gimmeDLLWrapper.h"

#include "ZoneMapLayer.h"
#include "wlGroupPropertyStructs.h"
#include "wlState.h"
#include "wlTerrainPrivate.h"
#include "wlTerrainSource.h"
#include "WorldCell.h"
#include "WorldGridLoadPrivate.h"
#include "WorldCellStreaming.h"
#include "RoomConn.h"
#include "UGCProjectUtils.h"

#include "wlEncounter.h"
#include "wlCurve.h"
#include "wlUGC.h"

#include "ZoneMapLayerPrivate_h_ast.c"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

AUTO_RUN;
int initTerrainBlockRangeParseInfo(void)
{
	ParserSetTableInfo(parse_TerrainBlockRange, sizeof(TerrainBlockRange), "TerrainBlockRange", NULL, __FILE__, SETTABLEINFO_ALLOW_CRC_CACHING);
	return 1;
}

void deinitTerrainBlock(TerrainBlockRange *block);

ParseTable parse_TerrainBlockRange[] =
{
	{ "",			TOK_STRUCTPARAM | TOK_INT(TerrainBlockRange, range.min_block[0],0)	},
	{ "",			TOK_STRUCTPARAM | TOK_INT(TerrainBlockRange, range.min_block[2],0)	},
	{ "",			TOK_STRUCTPARAM | TOK_INT(TerrainBlockRange, range.max_block[0],0)	},
	{ "",			TOK_STRUCTPARAM | TOK_INT(TerrainBlockRange, range.max_block[2],0)	},
	{ "",			TOK_STRUCTPARAM | TOK_STRING(TerrainBlockRange, range_name,0)	},
	{ "\n",			TOK_END, 0 },
	{ "", 0, 0 }
};

void layerGetHeaderBinFile(char *filename, int filename_size, ZoneMapLayer *layer)
{
	if (isProductionEditMode() && resNamespaceIsUGC(layer->zmap_parent->map_info.map_name))
	{
		const char *layername = layer->filename;
		if (strrchr(layername, ':'))
			layername = strrchr(layername, ':')+1;
		sprintf_s(SAFESTR2(filename), "%s/%s_header.bin", fileTempDir(), layername);
	}
	else
	{
		char base_name[MAX_PATH];
		worldGetTempBaseDir(layer->filename, SAFESTR(base_name));
		sprintf_s(SAFESTR2(filename), "%s_header.bin", base_name);
	}
}


void layerGetGenesisDir(char *filename, int filename_size, ZoneMapLayer *layer)
{
	if (isProductionEditMode())
	{
		const char *layername = layer->filename;
		if (strrchr(layername, ':'))
			layername = strrchr(layername, ':')+1;
		sprintf_s(SAFESTR2(filename), "%s/%s/AUTOGEN.layer", fileTempDir(), layername);
	}
	else
	{
		char base_name[MAX_PATH];
		worldGetTempBaseDir(layer->filename, SAFESTR(base_name));
		sprintf_s(SAFESTR2(filename), "%s/AUTOGEN.layer", base_name);
	}
}

// initializes the layer
// after calling this the layer is ready for data loading or to be used as a new layer
void createLayerData(ZoneMapLayer *layer, bool create_def_lib)
{
	if (layer->grouptree.def_lib)
		return;

	if (create_def_lib)
	{
		layer->grouptree.def_lib = groupLibCreate(false);
		layer->grouptree.def_lib->zmap_layer = layer;
	}
	else
	{
		layer->grouptree.def_lib = NULL;
	}

	layer->terrain.need_bins = false;
	layer->terrain.loaded_lod = -1;
	layer->terrain.non_playable = false;
	layer->terrain.exclusion_version = EXCLUSION_SIMPLE;
	layer->terrain.color_shift = 0.0f;

	setVec3same(layer->bounds.local_min, 8e16);
	setVec3same(layer->bounds.local_max, -8e16);
	setVec3same(layer->bounds.visible_geo_min, 8e16);
	setVec3same(layer->bounds.visible_geo_max, -8e16);

	if (layer->reserved_unique_names)
		stashTableDestroy(layer->reserved_unique_names);
	layer->reserved_unique_names = stashTableCreateWithStringKeys(16, StashDeepCopyKeys_NeverRelease);
}

static void freeLayerCellEntries(ZoneMapLayer *layer)
{
	WorldRegion *region = layerGetWorldRegion(layer);
	int i;

	for (i = 0; i < eaSize(&layer->tag_locations); ++i)
	{
		eaFindAndRemoveFast(&region->tag_locations, layer->tag_locations[i]);
		free(layer->tag_locations[i]);
	}
	eaDestroy(&layer->tag_locations);

	eaDestroyEx(&layer->terrain.object_defs, layerFreeObjectWrapper);
	eaDestroyEx(&layer->cell_entries, worldCellEntryFree);
}

static void freeLayerData(ZoneMapLayer *layer)
{
	int i;

	if (wl_state.layer_mode_callback)
		wl_state.layer_mode_callback(layer, LAYER_MODE_NOT_LOADED, false);

	trackerClose(layer->grouptree.root_tracker);
	trackerFree(layer->grouptree.root_tracker);
	layer->grouptree.root_tracker = NULL;
	if (layer->grouptree.def_lib)
		groupLibFree(layer->grouptree.def_lib);
	layer->grouptree.def_lib = NULL;
	SAFE_FREE(layer->name);
	layer->grouptree.root_def = NULL;

	for (i = 0; i < eaSize(&layer->terrain.blocks); ++i)
		deinitTerrainBlock(layer->terrain.blocks[i]);

	eaDestroyEx(&layer->terrain.binned_instance_groups, terrainFreeTerrainBinnedObjectGroup);
	StructDeInit(parse_ZoneMapTerrainLayer, &layer->terrain);
	layer->terrain.need_bins = false;

	freeLayerCellEntries(layer);
	roomConnGraphUnloadLayer(layer);

	// unload scoped data
	if(world_grid.active_map)
		worldZoneMapScopeUnloadLayer(world_grid.active_map->zmap_scope, layer);
	stashTableDestroy(layer->reserved_unique_names);
	layer->reserved_unique_names = NULL;
}

ZoneMapLayer *layerNew(ZoneMap *zmap, const char *layer_filename)
{
	ZoneMapLayer *layer;

	layer = StructCreate(parse_ZoneMapLayer);
	layer->filename = allocAddFilename(layer_filename);

	layer->zmap_parent = zmap;

	createLayerData(layer, true);
	layer->terrain.exclusion_version = EXCLUSION_SIMPLE;
	layer->terrain.color_shift = 0.0f;
	layer->grouptree.unsaved_changes = true;

	return layer;
}

static void layerStripFilename(char layer_name_buf[MAX_PATH], const char *filename)
{
	const char *sc = strrchr(filename, '/');
	char *s;

	if (sc)
		++sc;
	else
		sc = filename;

	strcpy_s(layer_name_buf, MAX_PATH, sc);

	s = strrchr(layer_name_buf, '.');
	if (s)
		*s = 0;
}

void layerReloadTerrainBins(ZoneMap *zmap, ZoneMapLayer *layer)
{
	char header_filename[MAX_PATH];
	WorldRegion *region = layerGetWorldRegion(layer);
	BinFileList *file_list = StructAlloc(parse_BinFileList);
	char base_dir[MAX_PATH];
	worldGetClientBaseDir(zmapGetFilename(zmap), SAFESTR(base_dir));
	sprintf(header_filename, "%s/terrain_%s_deps.bin", base_dir, region->name ? region->name : "Default");
	if (fileExists(header_filename) && ParserOpenReadBinaryFile(NULL, header_filename, parse_BinFileList, file_list, NULL, NULL, NULL, NULL, 0, 0, 0))
		terrainLoadRegionAtlases(region, file_list);
	StructDestroySafe(parse_BinFileList, &file_list);
}

bool layerNeedsTerrainBins(ZoneMapLayer *layer)
{
	bool ret = false;
	char bin_filename[256];

	layer->terrain.need_bins = false;
	layer->terrain.loaded_lod = -1;
	layer->terrain.non_playable = false;
	layer->terrain.exclusion_version = EXCLUSION_SIMPLE;
	layer->terrain.color_shift = 0.0f;

	freeLayerCellEntries(layer);

	// Attempt to load header bin file
	layerGetHeaderBinFile(bin_filename, ARRAY_SIZE_CHECKED(bin_filename), layer);
	binNotifyTouchedOutputFile(bin_filename);
	if (!ParserReadTextFile(bin_filename, parse_ZoneMapTerrainLayer, &layer->terrain, 0) ||
		(layer->terrain.layer_timestamp != 0 &&
		layer->terrain.layer_timestamp != bflFileLastChanged(layer->filename))) // Successfully loaded terrain data
	{
		return true;
	}
	else
	{
		int i;
		initTerrainBlocks(layer, true);
		for (i = 0; i < eaSize(&layer->terrain.blocks); i++)
			if (layer->terrain.blocks[i]->need_bins)
			{
				return true;
			}
	}
	return false;
}

void layerLoadStreaming(ZoneMapLayer *layer, ZoneMap *zmap, const char *layer_name)
{
	char bin_filename[256];

	if (layer->layer_mode > LAYER_MODE_NOT_LOADED)
		return;

	layer->zmap_parent = zmap;
	createLayerData(layer, false);

	freeLayerCellEntries(layer);

	// Attempt to load header bin file
	layerGetHeaderBinFile(bin_filename, ARRAY_SIZE_CHECKED(bin_filename), layer);
	binNotifyTouchedOutputFile(bin_filename);
	if (ParserReadTextFile(bin_filename, parse_ZoneMapTerrainLayer, &layer->terrain, 0) &&
		(isProductionMode() || layer->terrain.layer_timestamp == 0 || layer->terrain.layer_timestamp == bflFileLastChanged(layer->filename))) // Successfully loaded terrain data
	{
		initTerrainBlocks(layer, true);

		if (wlIsServer())
			updateTerrainBlocks(layer);
	}
	else
	{
		if (wlIsServer())
			layer->terrain.need_bins = true;
	}

	layerReloadTerrainBins(zmap, layer);

	layer->layer_mode = LAYER_MODE_STREAMING;	 
	if (layer->target_mode == LAYER_MODE_NOT_LOADED)
		layer->target_mode = LAYER_MODE_STREAMING;
}

void layerLoadGroupSource(ZoneMapLayer *layer, ZoneMap *zmap, const char *layer_name, bool binning)
{
	char layer_name_buf[MAX_PATH];
	int i;
	LibFileLoad *loaded_struct = NULL;
	bool params_loaded = false;
	char genesis_bin_file[MAX_PATH];

	if (layer->layer_mode > LAYER_MODE_NOT_LOADED)
		return;

	layer->zmap_parent = zmap;
	createLayerData(layer, true);

	if (!layer_name)
	{
		layerStripFilename(layer_name_buf, layer->filename);
		layer_name = layer_name_buf;
	}
	layer->name = strdup(layer_name);

	if (zmap->map_info.genesis_data && !layer->scratch)
	{
		layerGetGenesisDir(genesis_bin_file, ARRAY_SIZE_CHECKED(genesis_bin_file), layer);
		if (fileExists(genesis_bin_file)) {
			loaded_struct = worldLoadLayerGroupFile(layer, allocAddFilename(genesis_bin_file));
			binNotifyTouchedOutputFile(genesis_bin_file);
		}
	}
	else
	{
		loaded_struct = worldLoadLayerGroupFile(layer, layer->filename);
	}
	layerResetRootGroupDef(layer, true);

	// Copy loaded information from .layer file
	if (loaded_struct)
	{
		layer->terrain.material_table = loaded_struct->material_table;
		loaded_struct->material_table = NULL;

		layer->terrain.object_table = loaded_struct->object_table;
		loaded_struct->object_table = NULL;

		layer->terrain.blocks = loaded_struct->blocks;
		loaded_struct->blocks = NULL;

		layer->terrain.non_playable = loaded_struct->non_playable;
		layer->terrain.exclusion_version = loaded_struct->exclusion_version;
		layer->terrain.color_shift = loaded_struct->color_shift;

		StructDestroy(parse_LibFileLoad, loaded_struct);
	}

	// Fixup objects
	for (i = 0; i < eaSize(&layer->terrain.object_table); i++)
	{
		// Makes sure that models are loaded and bounds are set
		objectLibraryGetGroupDefFromRef(&layer->terrain.object_table[i]->objInfo, true);
	}

	initTerrainBlocks(layer, false);

	if(eaSize(&layer->terrain.material_table) == 0)
	{
		char *new_entry = StructAllocString("TerrainDefault");
		eaPush(&layer->terrain.material_table, new_entry);
	}

	//Fixup Terrain Object UIDs
    if (!isProductionMode())
    {
        for(i = 0; i < eaSize(&layer->terrain.object_table); i++)
        {	
            GroupDef *child_group;
            child_group = objectLibraryGetGroupDefFromRef(&layer->terrain.object_table[i]->objInfo, true);
            if(child_group && child_group->name_uid != layer->terrain.object_table[i]->objInfo.name_uid)
            {
				layer->terrain.object_table[i]->objInfo.name_uid = child_group->name_uid;
                //layer->terrain.objects_file->was_fixed_up = 1;
				// TomY TODO signal this somehow?
            }
        }
    }

	if (eaSize(&layer->terrain.blocks) > 0)
	{
		updateTerrainBlocks(layer);

		for (i = 0; i < eaSize(&layer->terrain.blocks); i++)
		{
			if (layer->terrain.blocks[i]->interm_file)
			{
				hogFileDestroy(layer->terrain.blocks[i]->interm_file, true);
				layer->terrain.blocks[i]->interm_file = NULL;
			}
		}

		if (!binning)
		{
			layerLoadTerrainObjects(layer);
		}
	}

	groupLibMarkBadChildren(layer->grouptree.def_lib);

	layer->layer_mode = LAYER_MODE_GROUPTREE;
	if (layer->target_mode == LAYER_MODE_NOT_LOADED)
		layer->target_mode = LAYER_MODE_GROUPTREE;
}

void layerUnload(ZoneMapLayer *layer)
{
	if (layer->layer_mode == LAYER_MODE_NOT_LOADED)
		return;

	freeLayerData(layer);
	layer->layer_mode = LAYER_MODE_NOT_LOADED;
}

// this functions clears out all non-parsed data
void layerClear(ZoneMapLayer *layer)
{
	ZoneMapLayer layer_copy = {0};
	ZoneMap *zmap_parent = layer->zmap_parent;

	assert(layer->layer_mode == LAYER_MODE_NOT_LOADED);

	StructCopy(parse_ZoneMapLayer, layer, &layer_copy, 0, 0, 0);
	StructDeInit(parse_ZoneMapLayer, layer);
	ZeroStructForce(layer);
	StructCopy(parse_ZoneMapLayer, &layer_copy, layer, 0, 0, 0);
	StructDeInit(parse_ZoneMapLayer, &layer_copy);

	layer->zmap_parent = zmap_parent;
}

ZoneMapLayerMode layerGetMode(ZoneMapLayer *layer)
{
	return layer->layer_mode;
}

ZoneMapLayerMode layerGetTargetMode(ZoneMapLayer *layer)
{
	return layer->target_mode;
}

void layerSetMode(ZoneMapLayer *layer, ZoneMapLayerMode mode, bool reload_game_data, bool binning, bool asynchronous)
{
	if (layer->layer_mode == mode)
		return;

	assert(mode >= LAYER_MODE_GROUPTREE &&		// Cannot unload geometry once we've loaded it
		mode < LAYER_MODE_EDITABLE);			// Cannot set a layer editable directly - must request a lock first

	if (reload_game_data && wl_state.unload_map_game_callback)
		wl_state.unload_map_game_callback();
    
	switch (mode)
	{
		xcase LAYER_MODE_GROUPTREE:
			loadstart_printf("Setting layer to geometry mode...");
		xcase LAYER_MODE_TERRAIN:
			//assert(!wlIsServer());
			loadstart_printf("Setting layer to terrain mode...");
	}

	// These are set whenever any layer leaves streaming mode
	worldCellSetEditable();

	layerUnload(layer);

	layerLoadGroupSource(layer, layer->zmap_parent, NULL, binning);
	if (mode == LAYER_MODE_GROUPTREE)
		layerReloadTerrainBins(layer->zmap_parent, layer);

	if (!binning)
	{
		bool game_callback_disable;

		setVec3same(layer->bounds.local_min, 8e16);
		setVec3same(layer->bounds.local_max, -8e16);
		setVec3same(layer->bounds.visible_geo_min, 8e16);
		setVec3same(layer->bounds.visible_geo_max, -8e16);
		layerUpdateBounds(layer);
		worldUpdateBounds(false, false);
		loadstart_printf("Creating cell entries...");
		layerTrackerOpen(layer);
		loadend_printf(" done.");
		layerHideTerrain(layer, (mode >= LAYER_MODE_TERRAIN));
		roomConnGraphUpdateAllRegions(world_grid.active_map);

		game_callback_disable = wl_state.disable_game_callbacks;
		wl_state.disable_game_callbacks = true;
		worldCheckForNeedToOpenCells();
		wl_state.disable_game_callbacks = game_callback_disable;
	}
	world_grid.file_reload_count++;

	if(reload_game_data && wl_state.load_map_game_callback)
		wl_state.load_map_game_callback(worldGetPrimaryMap());

	if (wl_state.layer_mode_callback)
		wl_state.layer_mode_callback(layer, mode, asynchronous);

	layer->layer_mode = mode;

	loadend_printf(" done.");
}

void layerReload(ZoneMapLayer *layer, bool load_terrain)
{
	if (layer->layer_mode == LAYER_MODE_NOT_LOADED)
		return;

	assert( isDevelopmentMode() );
	wl_state.HACK_disable_game_callbacks = true;
	layerUnload(layer);
	layerSetMode(layer, load_terrain ? LAYER_MODE_TERRAIN : LAYER_MODE_GROUPTREE, true, false, false);
	wl_state.HACK_disable_game_callbacks = false;
}

void layerFree(ZoneMapLayer *layer)
{
	if (!layer)
		return;

	freeLayerData(layer);
	StructDestroy(parse_ZoneMapLayer, layer);
}

bool layerIsReference(const ZoneMapLayer *layer)
{
	char zoneDirName[MAX_PATH];
	char layerDirName[MAX_PATH];

	if(!layer->zmap_parent || !layer->zmap_parent->map_info.filename || layer->zmap_parent->map_info.filename[0] == '\0' || !layer->filename || layer->filename[0] == '\0')
		return false;

	if(layer->zmap_parent->map_info.filename)
	{
		char *s;
		strcpy(zoneDirName, layer->zmap_parent->map_info.filename);
		s = strrchr(zoneDirName, '/');
		if(s)
			*(s+1) = 0;
	}

	if(layer->filename)
	{
		char *s;
		strcpy(layerDirName, layer->filename);
		s = strrchr(layerDirName, '/');
		if(s)
			*(s+1) = 0;
	}

	return stricmp(zoneDirName, layerDirName) != 0;
}

const char *layerGetFilename(const ZoneMapLayer *layer)
{
	return layer->filename;
}

const char *layerGetName(const ZoneMapLayer *layer)
{
	return getFileNameConst(layer->filename);
}

bool layerGetUnsaved(ZoneMapLayer *layer)
{
	if (zmapInfoHasGenesisData(zmapGetInfo(layer->zmap_parent)) && !layer->scratch)
		return false;
	return layer->terrain.unsaved_changes || layer->grouptree.unsaved_changes;
}

void layerSetUnsaved(ZoneMapLayer *layer, bool unsaved)
{
 	layer->grouptree.unsaved_changes = unsaved;
}

void layerChangeFilename(ZoneMapLayer *layer, const char *filename)
{
	int i;
	GroupDef **defs = groupLibGetDefEArray(layer->grouptree.def_lib);

	// Changing a layer's filename occurs during Map Save As operations. When performing a Save As, we must ensure that no 2 layer filenames are the same.
	// This can occur when saving as without preserving layer references. Layers can be referenced that both have the name "Default.Layer", even though their paths
	// are distinct. To prevent 2 Default.Layer filenames in the same directory, we start numbering them. After Default.Layer, we get Default_2.Layer, then Default_3.Layer,
	// and so on. The end result will be an actual filename that we can use.
	char actual_filename[MAX_PATH];
	{
		ZoneMapLayer *existing_layer = layerByFilename(layer->zmap_parent, filename);
		strcpy_s(actual_filename, MAX_PATH, filename);
		if(existing_layer)
		{
			if(existing_layer != layer)
			{
				int counter = 1;
				char filename_ext[MAX_PATH];
				char filename_without_ext[MAX_PATH];
				char *last_dot = strrchr(filename, '.');
				if(last_dot)
				{
					strncpy_s(filename_ext, MAX_PATH, last_dot, strlen(last_dot));
					strncpy_s(filename_without_ext, MAX_PATH, filename, strlen(filename) - strlen(last_dot));
				}
				else
				{
					strcpy(filename_ext, "");
					strcpy(filename_without_ext, filename);
				}

				do
				{
					devassertmsgf(counter <= eaSize(&layer->zmap_parent->layers), "Cannot change layer filename from %s to %s in zone %s.", layer->filename, filename, layer->zmap_parent->map_info.filename);
					counter++;
					snprintf_s(actual_filename, MAX_PATH, "%s_%d%s", filename_without_ext, counter, filename_ext);
				} while(layerByFilename(layer->zmap_parent, actual_filename));
			}
		}
	}

	layer->filename = allocAddFilename(actual_filename);
	layer->grouptree.unsaved_changes = true;

	for ( i=0; i < eaSize(&defs); i++ ) {
		defs[i]->filename = layer->filename;
	}

	if (wl_state.rename_terrain_source_callback)
		wl_state.rename_terrain_source_callback(layer);
}

bool terrainStartSavingBlock(TerrainBlockRange *block)
{
    TerrainFileList *list = &block->source_files;
    
    if (worldFileBlock(&list->heightmap.file_base) &&
	    worldFileBlock(&list->holemap.file_base) &&
    	worldFileBlock(&list->colormap.file_base) &&
	    worldFileBlock(&list->tiffcolormap.file_base) &&
    	worldFileBlock(&list->materialmap.file_base) &&
	    worldFileBlock(&list->objectmap.file_base) &&
    	worldFileBlock(&list->soildepthmap.file_base))
        return true;

    return false;
}

bool terrainDoneSavingBlock(TerrainBlockRange *block)
{
    TerrainFileList *list = &block->source_files;
    
    if (worldFileUnblock(&list->heightmap.file_base) &&
        worldFileUnblock(&list->holemap.file_base) &&
        worldFileUnblock(&list->colormap.file_base) &&
        worldFileUnblock(&list->tiffcolormap.file_base) &&
        worldFileUnblock(&list->materialmap.file_base) &&
        worldFileUnblock(&list->objectmap.file_base) &&
        worldFileUnblock(&list->soildepthmap.file_base))
        return true;

    return false;
}

bool checkoutTerrainFiles(WorldFile **files)
{
    int i;
    for (i = 0; i < eaSize(&files); i++)
		files[i]->client_owned = !wlIsServer();
	if (!worldFileCheckoutList(files))
	{
		return false;
	}
    for (i = 0; i < eaSize(&files); i++)
		if (fileIsReadOnly(files[i]->fullname))
        {
            //Alertf("Couldn't check out file: %s", files[i]->fullname);
            return false;
        }
	return true;
}

bool terrainCheckoutBlock(TerrainBlockRange *block)
{
    int i;
    WorldFile **file_list = NULL;
    TerrainFileList *list = &block->source_files;
    
    eaPush(&file_list, &list->heightmap.file_base);
    eaPush(&file_list, &list->holemap.file_base);
    eaPush(&file_list, &list->colormap.file_base);
    eaPush(&file_list, &list->tiffcolormap.file_base);
    eaPush(&file_list, &list->materialmap.file_base);
    eaPush(&file_list, &list->objectmap.file_base);
    eaPush(&file_list, &list->soildepthmap.file_base);

    if (!checkoutTerrainFiles(file_list))
    {
        for (i = 0; i < eaSize(&file_list); i++)
        {
            const char *lockee = worldFileGetLockee(file_list[i]);
            if (lockee)
            {
                printf("File \"%s\" already checked out by \"%s\" through Gimme!", file_list[i]->fullname, lockee);
                eaDestroy(&file_list);
                return false;
            }
        }
        printf("Someone has already checked out this terrain through Gimme!");
        eaDestroy(&file_list);
        return false;
    }

    eaDestroy(&file_list);

    return true;
}

bool deleteTerrainFiles(WorldFile **files)
{
    int i;
    for (i = 0; i < eaSize(&files); i++)
		files[i]->client_owned = !wlIsServer();
    if (!worldFileCheckoutList(files))
    {
        return false;
    }
	if (!worldFileDeleteList(files))
	{
		return false;
	}
	return true;
}

bool terrainDeleteBlock(TerrainBlockRange *block)
{
    WorldFile **file_list = NULL;
    TerrainFileList *list = &block->source_files;
    
    eaPush(&file_list, &list->heightmap.file_base);
    eaPush(&file_list, &list->holemap.file_base);
    eaPush(&file_list, &list->colormap.file_base);
    eaPush(&file_list, &list->tiffcolormap.file_base);
    eaPush(&file_list, &list->materialmap.file_base);
    eaPush(&file_list, &list->objectmap.file_base);
    eaPush(&file_list, &list->soildepthmap.file_base);

    if (!deleteTerrainFiles(file_list))
    {
        printf("Failed to delete terrain block!");
        eaDestroy(&file_list);
        return false;
    }

    eaDestroy(&file_list);

    return true;
}

bool terrainCheckoutBlocks(TerrainBlockRange **blocks)
{
    int i;
    WorldFile **file_list = NULL;

    for (i = 0; i < eaSize(&blocks); i++)
    {
        TerrainFileList *list = &blocks[i]->source_files;
        eaPush(&file_list, &list->heightmap.file_base);
        eaPush(&file_list, &list->holemap.file_base);
        eaPush(&file_list, &list->colormap.file_base);
        eaPush(&file_list, &list->tiffcolormap.file_base);
        eaPush(&file_list, &list->materialmap.file_base);
        eaPush(&file_list, &list->objectmap.file_base);
        eaPush(&file_list, &list->soildepthmap.file_base);
    }

    if (!checkoutTerrainFiles(file_list))
    {
        for (i = 0; i < eaSize(&file_list); i++)
        {
            const char *lockee = worldFileGetLockee(file_list[i]);
            if (lockee)
            {
                printf("File \"%s\" already checked out by \"%s\" through Gimme!", file_list[i]->fullname, lockee);
                eaDestroy(&file_list);
                return false;
            }
        }
        printf("Someone has already checked out this terrain through Gimme!");
        eaDestroy(&file_list);
        return false;
    }

    eaDestroy(&file_list);

    return true;
}

bool terrainCheckoutLayer(ZoneMapLayer *layer, char *lockee_buf, int lockee_size, bool query_only)
{
    int i;
    WorldFile **file_list = NULL;
	WorldFile layer_file = { 0 };

	layer_file.fullname = layer->filename;
	eaPush(&file_list, &layer_file);

    for (i = 0; i < eaSize(&layer->terrain.blocks); i++)
    {
        TerrainFileList *list = &layer->terrain.blocks[i]->source_files;
        layerTerrainUpdateFilenames(layer, layer->terrain.blocks[i]);
        eaPush(&file_list, &list->heightmap.file_base);
        eaPush(&file_list, &list->holemap.file_base);
        eaPush(&file_list, &list->colormap.file_base);
        eaPush(&file_list, &list->tiffcolormap.file_base);
        eaPush(&file_list, &list->materialmap.file_base);
        eaPush(&file_list, &list->objectmap.file_base);
        eaPush(&file_list, &list->soildepthmap.file_base);
    }

    eaDestroy(&file_list);

    return true;
}

bool layerAttemptLock(ZoneMapLayer *layer, char *lockee_buf, int lockee_size, bool query_only)
{
	layer->target_mode = LAYER_MODE_GROUPTREE;
	layerSetMode(layer, LAYER_MODE_GROUPTREE, true, false, true);
	return terrainCheckoutLayer(layer, lockee_buf, lockee_size, query_only);
}

bool layerSaveAs(ZoneMapLayer *layer, const char *filename, bool force, bool asynchronous, bool save_terrain, bool fixup_messages)
{
	bool ret = true;
	LibFileLoad *lib = NULL;
	assert(layer->layer_mode == LAYER_MODE_EDITABLE && !layer->saving);
	assert(layer->grouptree.def_lib);

	// Scratch layer never has terrain so don't try and save it
	if (save_terrain && !layer->scratch)
	{
		// Terrain ignores the passed-in filename. See zmapSaveAs for how to do this.
		if (wl_state.save_terrain_source_callback)
		{
			if (!wl_state.save_terrain_source_callback(layer, force, asynchronous))
			{
				return false;
			}
		}
	}

	if (layer->grouptree.unsaved_changes || save_terrain || force)
	{
		int i;

		// Before messages are fixed up, the group def must have the new file name since message keys use the file name.
		if (fixup_messages) {
			const char *filename_pooled = allocAddFilename(filename);
			GroupDef **lib_defs = groupLibGetDefEArray(layer->grouptree.def_lib);
			for (i = 0; i < eaSize(&lib_defs); i++) {
				const char *old_filename = lib_defs[i]->filename;
				lib_defs[i]->filename = filename_pooled;
				groupDefFixupMessages(lib_defs[i]);
				lib_defs[i]->filename = old_filename;
			}
		}
		
		lib = StructCreate(parse_LibFileLoad);

		// Copy terrain data
		lib->non_playable = layer->terrain.non_playable;
		lib->exclusion_version = layer->terrain.exclusion_version;
		lib->color_shift = layer->terrain.color_shift;

		for (i = 0; i < eaSize(&layer->terrain.material_table); i++)
			eaPush(&lib->material_table, StructAllocString(layer->terrain.material_table[i]));

		for (i = 0; i < eaSize(&layer->terrain.object_table); i++)
			eaPush(&lib->object_table, StructClone(parse_TerrainObjectEntry, layer->terrain.object_table[i]));

		for (i = 0; i < eaSize(&layer->terrain.blocks); i++)
			eaPush(&lib->blocks, StructClone(parse_TerrainBlockRange, layer->terrain.blocks[i]));

		if (!saveGroupFileAs(lib, layer->grouptree.def_lib, filename))
			return false;
	}

	if (!strEndsWith(filename, ".autosave"))
	{
		layer->last_change_time = fileLastChanged(filename);
		layerSetUnsaved(layer, false);
	}

	return true;
}


bool layerSave(ZoneMapLayer *layer, bool force, bool asynchronous)
{
	if (layer->layer_mode != LAYER_MODE_EDITABLE || layer->saving)
		return true;

	return layerSaveAs(layer, layer->filename, force, asynchronous, true, true);
}

bool layerIsSaving(ZoneMapLayer *layer)
{
	return layer->saving;
}

void layerSetHACKDisableGameCallbacks(bool value)
{
	wl_state.HACK_disable_game_callbacks = value;
}

// Infuriatingly, does not update the "bounds" field on the layer
void layerUpdateBounds(ZoneMapLayer *layer)
{
	int i;

	setVec3same(layer->terrain.heightmaps_min, 8e16);
	setVec3same(layer->terrain.heightmaps_max, -8e16);

	if (layer->grouptree.root_def)
	{
		layer->grouptree.root_tracker->idx_in_parent = layerIdxInParent(layer);
		groupSetBounds(layer->grouptree.root_def, false);
	}

	updateTerrainBlocks(layer);

    for (i = eaSize(&layer->terrain.blocks)-1; i >= 0; --i)
    {
        int j;
        TerrainBlockRange *range = layer->terrain.blocks[i];
        Vec3 range_min, range_max;
        setVec3(range_min, range->range.min_block[0]*GRID_BLOCK_SIZE, 8e16, range->range.min_block[2]*GRID_BLOCK_SIZE);
        setVec3(range_max, (range->range.max_block[0]+1)*GRID_BLOCK_SIZE, -8e16, (range->range.max_block[2]+1)*GRID_BLOCK_SIZE);

		if (range->map_ranges_array)
		{
			for (j = (range->range.max_block[0]+1-range->range.min_block[0])*(range->range.max_block[2]+1-range->range.max_block[2]) - 1; j >= 0; --j)
			{
				if (range_min[1] > range->map_ranges_array[j*2])
					range_min[1] = range->map_ranges_array[j*2];
				if (range_max[1] < range->map_ranges_array[j*2+1])
					range_max[1] = range->map_ranges_array[j*2+1];
			}
		}
		vec3RunningMin(range_min, layer->terrain.heightmaps_min);
		vec3RunningMax(range_max, layer->terrain.heightmaps_max);
    }

    // Not Reliable!
	for (i = 0; i < eaSize(&layer->terrain.object_defs); i++)
	{
		groupSetBounds(layer->terrain.object_defs[i]->root_def, false);
	}

	// Reliable, but wrong.
    layer->terrain.heightmaps_min[0] -= 10.f;
    layer->terrain.heightmaps_min[1] -= 50.f;
    layer->terrain.heightmaps_min[2] -= 10.f;
    layer->terrain.heightmaps_max[0] += 10.f;
    layer->terrain.heightmaps_max[1] += 50.f;
    layer->terrain.heightmaps_max[2] += 10.f;

	// This function does not seem to update the "layer bounds".  However, the function below, layerGetBounds, combines the root groupdef
	// bounds with the other bounds.  My purpose here is to find the VISIBLE bounds right now.
	if (layer->grouptree.root_def)
	{
		groupGetVisibleBounds(layer->grouptree.root_def, unitmat, 1.0f, layer->bounds.visible_geo_min, layer->bounds.visible_geo_max);
	}
}

void layerGetBounds(ZoneMapLayer *layer, Vec3 local_min, Vec3 local_max)
{
	int i;

	if(vec3IsZero(layer->bounds.local_min) && vec3IsZero(layer->bounds.local_max)) {
		setVec3same(local_min, 8e16);
		setVec3same(local_max, -8e16);
	} else {
		copyVec3(layer->bounds.local_min, local_min);
		copyVec3(layer->bounds.local_max, local_max);
	}

	if (layer->grouptree.root_def)
	{
		groupSetBounds(layer->grouptree.root_def, false);
		if (!layer->grouptree.root_def->bounds_null)
		{
			vec3RunningMin(layer->grouptree.root_def->bounds.min, local_min);
			vec3RunningMax(layer->grouptree.root_def->bounds.max, local_max);
		}
	}

	for (i = 0; i < eaSize(&layer->terrain.object_defs); i++)
	{
		groupSetBounds(layer->terrain.object_defs[i]->root_def, false);
		if (!layer->terrain.object_defs[i]->root_def->bounds_null)
		{
			vec3RunningMin(layer->terrain.object_defs[i]->root_def->bounds.min, local_min);
			vec3RunningMax(layer->terrain.object_defs[i]->root_def->bounds.max, local_max);
		}
	}

	//If we have valid bounds
	if (local_min[0] <= local_max[0] && local_min[1] <= local_max[1] && local_min[2] <= local_max[2])
	{
		#define MAX_WORLD_BOUNDS ((1<<15)-1)
		if(	local_min[0] <= -MAX_WORLD_BOUNDS || 
			local_min[1] <= -MAX_WORLD_BOUNDS ||
			local_min[2] <= -MAX_WORLD_BOUNDS ||
			local_max[0] >=  MAX_WORLD_BOUNDS ||
			local_max[1] >=  MAX_WORLD_BOUNDS ||
			local_max[2] >=  MAX_WORLD_BOUNDS )
		{
			MAX1(local_min[0], -MAX_WORLD_BOUNDS);
			MAX1(local_min[1], -MAX_WORLD_BOUNDS);
			MAX1(local_min[2], -MAX_WORLD_BOUNDS);
			MIN1(local_max[0],  MAX_WORLD_BOUNDS);
			MIN1(local_max[1],  MAX_WORLD_BOUNDS);
			MIN1(local_max[2],  MAX_WORLD_BOUNDS);
		}
	}
// 	vec3RunningMin(layer->terrain.heightmaps_min, local_min);
// 	vec3RunningMax(layer->terrain.heightmaps_max, local_max);
}

void layerGetTerrainBounds(ZoneMapLayer *layer, Vec3 local_min, Vec3 local_max)
{
	setVec3same(local_min, 8e16);
	setVec3same(local_max, -8e16);

	layerUpdateBounds(layer);
	vec3RunningMin(layer->terrain.heightmaps_min, local_min);
	vec3RunningMax(layer->terrain.heightmaps_max, local_max);
}

void layerGetVisibleBounds(ZoneMapLayer *layer, Vec3 local_min, Vec3 local_max)
{
	int i;

	if(vec3IsZero(layer->bounds.local_min) && vec3IsZero(layer->bounds.local_max)) {
		setVec3same(local_min, 8e16);
		setVec3same(local_max, -8e16);
	} else {
		copyVec3(layer->bounds.visible_geo_min, local_min);
		copyVec3(layer->bounds.visible_geo_max, local_max);
	}

	// terrain is presumably visible
	for (i = 0; i < eaSize(&layer->terrain.object_defs); i++)
	{
		groupSetBounds(layer->terrain.object_defs[i]->root_def, false);
		if (!layer->terrain.object_defs[i]->root_def->bounds_null)
		{
			vec3RunningMin(layer->terrain.object_defs[i]->root_def->bounds.min, local_min);
			vec3RunningMax(layer->terrain.object_defs[i]->root_def->bounds.max, local_max);
		}
	}
}

ZoneMap *layerGetZoneMap(ZoneMapLayer *layer)
{
	return layer->zmap_parent;
}

int layerIdxInParent(ZoneMapLayer *layer)
{
	int i;

	if (!layer || !layer->zmap_parent)
		return -1;

	for (i = 0; i < eaSize(&layer->zmap_parent->layers); ++i)
	{
		if (layer->zmap_parent->layers[i] == layer)
			return i;
	}

	return -1;
}

ZoneMapLayer *layerByFilename(ZoneMap *zmap, const char *filename)
{
	int i;
	for(i = 0; i < eaSize(&zmap->layers); i++)
		if(strcmpi(zmap->layers[i]->filename, filename) == 0)
			return zmap->layers[i];
	return NULL;
}

void layerSetWorldRegion(ZoneMapLayer *layer, const char *region_name)
{
	if (region_name && strlen(region_name) > 0 && strcmpi(region_name, "Default") != 0)
		layer->region_name = allocAddString(region_name);
	else
		layer->region_name = NULL;
}

WorldRegion *layerGetWorldRegion(ZoneMapLayer *layer)
{
	if (!layer)
		return worldGetTempWorldRegionByName(NULL);
	if (!layer->zmap_parent)
		return worldGetTempWorldRegionByName(layer->region_name);
	return zmapGetWorldRegionByName(layer->zmap_parent, layer->region_name);
}

const char *layerGetWorldRegionString(ZoneMapLayer *layer)
{
	return SAFE_MEMBER(layer, region_name);
}

void layerHide(ZoneMapLayer *layer, bool hide)
{
	trackerSetInvisible(layer->grouptree.root_tracker, hide);
}

void layerHideTerrain(ZoneMapLayer *layer, bool hide)
{
	WorldRegion *region = layerGetWorldRegion(layer);
	if (layer->terrain.layer_hidden != hide)
	{
		layer->terrain.layer_hidden = hide;
		layerTrackerUpdate(layer, true, false);
	}
}

void layerGetTexturesAndGeosAndEvents(ZoneMapLayer *layer, GathererData *gather_data)
{
	int blocknum, x, y, i;

	// terrain detail textures
	for (blocknum = 0; blocknum < eaSize(&layer->terrain.blocks); ++blocknum)
	{
		TerrainBlockRange* range = layer->terrain.blocks[blocknum];

		for (y = range->range.min_block[2]; y <= range->range.max_block[2]; ++y)
		{
			for (x = range->range.min_block[0]; x <= range->range.max_block[0]; ++x)
			{
				int local_pos[2] = {x, y};
				HeightMap *height_map;
				const char **detail_textures;
				height_map = terrainGetHeightMapForLocalGridPos(layer, local_pos);
				if (height_map)
				{
					detail_textures = heightMapGetDetailTextures(height_map);
					for (i = 0; i < NUM_MATERIALS; ++i)
					{
						if (detail_textures[i])
							stashAddPointer(gather_data->textures, detail_textures[i], NULL, false);
					}
				}
			}
		}
	}
}

bool layerIsGenesis(SA_PARAM_NN_VALID ZoneMapLayer *layer)
{
	return layer->genesis;
}

U32 layerGetLocked(ZoneMapLayer *layer)
{
	return layerIsGenesis(layer) ? 3 : layer->locked;
}

void layerSetLocked(ZoneMapLayer *layer, U32 locked)
{
	layer->locked = locked;
}

void layerIncrementModTime(ZoneMapLayer *layer)
{
	if (layer->dummy_layer)
		return;
	if (!layerIsGenesis(layer) && !layerGetLocked(layer))
	{
		Alertf("Layer: %s -- Attempting to update a non-locked layer.",
			   layer->name);
		return;
	}
	worldIncModTime();
	layerSetUnsaved(layer, true);
}

//////////////////////////////////////////////////////////////////////////
// GroupTree layer contents

GroupDefLib *layerGetGroupDefLib(ZoneMapLayer *layer)
{
	if (!layer)
		return NULL;

	return layer->grouptree.def_lib;
}

GroupTracker *layerGetTracker(ZoneMapLayer *layer)
{
	if (!layer)
		return NULL;

	return layer->grouptree.root_tracker;
}

GroupDef *layerGetDef(ZoneMapLayer *layer)
{
	if (!layer)
		return NULL;

	return layer->grouptree.root_def;
}

void layerResetRootGroupDef(ZoneMapLayer *layer, bool make_tracker)
{
	GroupDef *layer_def;
	if (!layer)
		return;
	assert(layer->grouptree.def_lib);

	layer_def = groupLibFindGroupDef(layer->grouptree.def_lib, 1, false);
	if (!layer_def || layer_def != layer->grouptree.root_def)
	{
 		if (!layer_def)
 		{
			layer_def = groupLibNewGroupDef(layer->grouptree.def_lib, layer->filename, 1, layer->name, 0, false, true);
			if (!layer_def)
			{
				char new_name[256];
				sprintf(new_name, "%s_NAMECOLLISION", layer->name);
				layer_def = groupLibNewGroupDef(layer->grouptree.def_lib, layer->filename, 1, new_name, 0, false, true);
				assertmsg(layer_def, "Layer name collision!");
			}
			layer_def->is_layer = 1;
 		}

		layer->grouptree.root_def = layer_def;
		if (layer->grouptree.root_tracker)
			trackerClose(layer->grouptree.root_tracker);
		else
			layer->grouptree.root_tracker = trackerAlloc();
		layer->grouptree.root_tracker->def = layer->grouptree.root_def;
		layer->grouptree.root_tracker->idx_in_parent = layerIdxInParent(layer);

		if (make_tracker)
			trackerInit(layer->grouptree.root_tracker);
		layerUpdateBounds(layer);
	}
}

static ZoneMapLayer **s_eaLayersToUpdatePathNodeTrackers = NULL;

static void layerUpdatePathNodeTrackers(ZoneMapLayer *layer)
{
	eaPushUnique(&s_eaLayersToUpdatePathNodeTrackers, layer);
}

static void layerUpdatePathNodeTrackers_Actual(ZoneMapLayer *layer)
{
	StashTable stLayerTrackersByID = stashTableCreateInt( 256 );

	PERFINFO_AUTO_START_FUNC();

	groupTrackerBuildPathNodeTrackerTable( layer->grouptree.root_tracker, stLayerTrackersByID );

	FOR_EACH_IN_STASHTABLE( stLayerTrackersByID, GroupTracker, tracker1 ) {
		FOR_EACH_IN_EARRAY( tracker1->def->property_structs.path_node_properties->eaConnections, WorldPathEdge, pathEdge ) {
			GroupTracker *tracker2 = NULL;
			stashIntFindPointer( stLayerTrackersByID, pathEdge->uOther, &tracker2 );

			if (tracker2) {
				bool bFoundEdge = false;

				FOR_EACH_IN_EARRAY( tracker2->def->property_structs.path_node_properties->eaConnections, WorldPathEdge, pathEdge2 ) {
					if (pathEdge2->uOther == tracker1->def->name_uid) {
						bFoundEdge = true;
					}
				} FOR_EACH_END;

				if (!bFoundEdge) {
					WorldPathEdge *newEdge = StructCreate(parse_WorldPathEdge);
					newEdge->uOther = tracker1->def->name_uid;
					eaPush(&tracker2->def->property_structs.path_node_properties->eaConnections, newEdge);
				}
			} else {
				eaRemove(&tracker1->def->property_structs.path_node_properties->eaConnections,
					FOR_EACH_IDX( tracker1->def->property_structs.path_node_properties->eaConnections, pathEdge ));
			}
		} FOR_EACH_END;
	} FOR_EACH_END;

	stashTableDestroy( stLayerTrackersByID );

	PERFINFO_AUTO_STOP_FUNC();
}

void layerCheckUpdatePathNodeTrackers()
{
	while (eaSize(&s_eaLayersToUpdatePathNodeTrackers))
	{
		layerUpdatePathNodeTrackers_Actual(s_eaLayersToUpdatePathNodeTrackers[0]);
		eaRemove(&s_eaLayersToUpdatePathNodeTrackers, 0);
	}
}

void layerTrackerUpdate(ZoneMapLayer *layer, bool force, bool terrain_only)
{
	WorldRegion *region;
	int i;

	if (!layer)
		return;

	PERFINFO_AUTO_START_FUNC();

	if (layer->grouptree.root_tracker)
	{
		layer->grouptree.root_tracker->idx_in_parent = layerIdxInParent(layer);
		trackerUpdate(layer->grouptree.root_tracker, layer->grouptree.root_def, force);
	}

    if (layer->terrain.layer_hidden)
	{
		for (i = 0; i < eaSize(&layer->terrain.object_defs); i++)
		{
			layerFreeObjectWrapperEntries(layer->terrain.object_defs[i]);
		}
	}
	else
	{
		for (i = 0; i < eaSize(&layer->terrain.object_defs); i++)
			layerUpdateObjectWrapperEntries(layer->terrain.object_defs[i]);
	}

	if(!terrain_only)
	{
		region = layerGetWorldRegion(layer);
		for (i = 0; i < eaSize(&layer->cell_entries); ++i)
		{
			WorldCellEntry *entry = layer->cell_entries[i];
			if (!entry->owned)
				worldAddCellEntry(region, entry);
		}

		layerUpdatePathNodeTrackers(layer);
	}

	PERFINFO_AUTO_STOP_FUNC();
}

void layerTrackerOpen(ZoneMapLayer *layer)
{
	WorldRegion *region;
	int i;

	if (!layer)
		return;

	PERFINFO_AUTO_START_FUNC();

	trackerOpen(layer->grouptree.root_tracker);

	if (layer->terrain.layer_hidden)
        return;
    
	for (i = 0; i < eaSize(&layer->terrain.object_defs); i++)
		layerFreeObjectWrapperEntries(layer->terrain.object_defs[i]);

	region = layerGetWorldRegion(layer);
	for (i = 0; i < eaSize(&layer->cell_entries); ++i)
	{
		WorldCellEntry *entry = layer->cell_entries[i];
		if (!entry->owned)
			worldAddCellEntry(region, entry);
	}

	PERFINFO_AUTO_STOP_FUNC();
}

void layerTrackerClose(ZoneMapLayer *layer)
{
	int i;

	if (!layer)
		return;

	PERFINFO_AUTO_START_FUNC();

	trackerClose(layer->grouptree.root_tracker);
	trackerInit(layer->grouptree.root_tracker);

	for (i = 0; i < eaSize(&layer->terrain.object_defs); i++)
		layerFreeObjectWrapperEntries(layer->terrain.object_defs[i]);

	PERFINFO_AUTO_STOP_FUNC();
}

void layerGroupTreeTraverse(ZoneMapLayer *layer, GroupTreeTraverserCallback callback, void *user_data, bool in_editor, bool terrain_objects)
{
	int i;

	if (!layer)
		return;

	PERFINFO_AUTO_START_FUNC();

	groupTreeTraverse(layer, layer->grouptree.root_def, NULL, callback, NULL, user_data, in_editor, false);

	if (terrain_objects)
		for (i = 0; i < eaSize(&layer->terrain.object_defs); i++)
			groupTreeTraverse(layer, layer->terrain.object_defs[i]->root_def, NULL, callback, NULL, user_data, in_editor, false);

	PERFINFO_AUTO_STOP_FUNC();
}

bool layerFindCellEntry(ZoneMapLayer *layer, WorldCellEntry *entry)
{
	return (eaFind(&layer->cell_entries, entry) != -1);
}

void layerCreateCellEntries(ZoneMapLayer *layer)
{
	int i;

	if (!layer)
		return;

	PERFINFO_AUTO_START_FUNC();

	worldEntryCreateForDefTree(layer, layer->grouptree.root_def, NULL);

	for (i = 0; i < eaSize(&layer->terrain.object_defs); i++)
		layerUpdateObjectWrapperEntries(layer->terrain.object_defs[i]);

	PERFINFO_AUTO_STOP_FUNC();
}

char **layerGetDependentFileNames(ZoneMapLayer *layer)
{
	StashTable filename_hash, def_hash;
	char **filenames = NULL;
	GroupDef **defs = NULL;
	StashTableIterator iter;
	StashElement elem;
	char base_dir[MAX_PATH];
	int i, j;

	if (!layer)
		return NULL;

	filename_hash = stashTableCreateWithStringKeys(512, StashDeepCopyKeys_NeverRelease);
	def_hash = stashTableCreateAddress(2048);

	if (layer->grouptree.def_lib)
	{
		GroupDef **lib_defs = groupLibGetDefEArray(layer->grouptree.def_lib);
		eaPushEArray(&defs, &lib_defs);
	}

	for (i = 0; i < eaSize(&layer->terrain.object_defs); i++)
	{
		GroupDef **lib_defs = groupLibGetDefEArray(layer->terrain.object_defs[i]->def_lib);
		eaPushEArray(&defs, &lib_defs);
	}

	worldGetTempBaseDir(layer->filename, SAFESTR(base_dir));

	for (i = 0; i < eaSize(&layer->terrain.blocks); ++i)
	{
		TerrainBlockRange *block = layer->terrain.blocks[i];
		char hog_filename[MAX_PATH];

		if(!layerIsGenesis(layer))
		{
			if (block->source_files.ecomap.file_base.fullname)
				stashAddPointer(filename_hash, block->source_files.ecomap.file_base.fullname, block->source_files.ecomap.file_base.fullname, false);

			if (block->source_files.heightmap.file_base.fullname)
				stashAddPointer(filename_hash, block->source_files.heightmap.file_base.fullname, block->source_files.heightmap.file_base.fullname, false);

			if (block->source_files.holemap.file_base.fullname)
				stashAddPointer(filename_hash, block->source_files.holemap.file_base.fullname, block->source_files.holemap.file_base.fullname, false);

			if (block->source_files.materialmap.file_base.fullname)
				stashAddPointer(filename_hash, block->source_files.materialmap.file_base.fullname, block->source_files.materialmap.file_base.fullname, false);

			if (block->source_files.soildepthmap.file_base.fullname)
				stashAddPointer(filename_hash, block->source_files.soildepthmap.file_base.fullname, block->source_files.soildepthmap.file_base.fullname, false);

			if (block->source_files.objectmap.file_base.fullname)
				stashAddPointer(filename_hash, block->source_files.objectmap.file_base.fullname, block->source_files.objectmap.file_base.fullname, false);

			if (block->source_files.colormap.file_base.fullname)
				stashAddPointer(filename_hash, block->source_files.colormap.file_base.fullname, block->source_files.colormap.file_base.fullname, false);

			if (block->source_files.tiffcolormap.file_base.fullname)
				stashAddPointer(filename_hash, block->source_files.tiffcolormap.file_base.fullname, block->source_files.tiffcolormap.file_base.fullname, false);
		}

		// terrain object bins
		sprintf(hog_filename, "%s/%s_intermediate.hogg", base_dir, block->range_name);
		stashAddPointer(filename_hash, hog_filename, hog_filename, false);
	}

	// put the initial set of defs into the hash table
	for (i = 0; i < eaSize(&defs); ++i)
	{
		GroupDef *def = defs[i];
		if (def && !stashFindPointer(def_hash, def, NULL))
			stashAddPointer(def_hash, def, def, false);
	}

	// Iterate all the defs
	for (i = 0; i < eaSize(&defs); ++i)
	{
		GroupDef *def = defs[i];
		GroupChild **def_children = groupGetChildren(def);

		// If it is an object library piece, depend on that library file
		if (def->def_lib && !def->def_lib->dummy && !def->def_lib->zmap_layer && def->filename)
		{
			if (!stashFindPointer(filename_hash, def->filename, NULL))
				stashAddPointer(filename_hash, def->filename, def->filename, false);
		}

		// If it is an encounter, depend on the encounter template file
		if (def->property_structs.encounter_properties)
		{
			WorldEncounterTemplateHeader *pTemplate = (WorldEncounterTemplateHeader *)GET_REF(def->property_structs.encounter_properties->hTemplate);
			if (pTemplate)
			{
				if (!stashFindPointer(filename_hash, pTemplate->pcFilename, NULL))
					stashAddPointer(filename_hash, pTemplate->pcFilename, pTemplate->pcFilename, false);
			}
		}

		// If it is a building, we need to depend on the buildings
		if (def->property_structs.building_properties)
		{
			WorldBuildingProperties *building_props = def->property_structs.building_properties;
			for ( j=0; j < eaSize(&building_props->layers); j++ )
			{
				WorldBuildingLayerProperties *layer_props = building_props->layers[j];
				GroupDef *child_def = GET_REF(layer_props->group_ref);
				if (child_def && !stashFindPointer(def_hash, child_def, NULL))
				{
					eaPush(&defs, child_def);
					stashAddPointer(def_hash, child_def, child_def, false);
				}
			}
		}

		// If it is a debris field, we need to depend on the debris
		if (def->property_structs.debris_field_properties)
		{
			GroupDef *child_def = GET_REF(def->property_structs.debris_field_properties->group_ref);
			if (child_def && !stashFindPointer(def_hash, child_def, NULL))
			{
				eaPush(&defs, child_def);
				stashAddPointer(def_hash, child_def, child_def, false);
			}			
		}

		// Add sound dependencies if client bins
		if(wl_state.sound_get_project_file_by_event_func)
		{
			if (def->property_structs.sound_sphere_properties)
			{
				char *sound_file_name;
				const char *sound_event = def->property_structs.sound_sphere_properties->pcEventName;
				if(sound_event && wl_state.sound_get_project_file_by_event_func(sound_event, &sound_file_name))
				{
					if (!stashFindPointer(filename_hash, sound_file_name, NULL))
						stashAddPointer(filename_hash, sound_file_name, sound_file_name, false);					
				}
			}
		}

		// Simulate recursion by pushing children to the end of the list
		for (j = 0; j < eaSize(&def_children); ++j)
		{
			GroupDef *child_def = groupChildGetDef(def, def_children[j], false);
			if (child_def && !stashFindPointer(def_hash, child_def, NULL))
			{
				eaPush(&defs, child_def);
				stashAddPointer(def_hash, child_def, child_def, false);
			}
		}
	}

	eaDestroy(&defs);

	stashGetIterator(filename_hash, &iter);
	while (stashGetNextElement(&iter, &elem))
	{
		char *key;
		if (key = stashElementGetKey(elem))
			eaPush(&filenames, strdup(key));
	}

	stashTableDestroy(filename_hash);
	stashTableDestroy(def_hash);

	return filenames;
}

//////////////////////////////////////////////////////////////////////////
// Terrain layer contents

void getTerrainSourceFilename(char *filename_out, int filename_out_size, const TerrainFileDef *def, TerrainBlockRange *block, const IVec2 pos, ZoneMapLayer *layer)
{
	char ext[MAX_PATH];
	char dir[MAX_PATH];

	if (def->lod_size < 0)
		sprintf(ext, ".%s", def->ext);
	else
		sprintf(ext, "_%d.%s", def->lod_size, def->ext);

	strcpy(dir, layer->filename);
	getDirectoryName(dir);
	sprintf_s(SAFESTR2(filename_out), "%s/%s%s", dir, block->range_name, ext);
}

void updateTerrainFileSource(const TerrainFileDef *def, TerrainBlockRange *block, TerrainFile *file)
{
	char filename[MAX_PATH];

	getTerrainSourceFilename(SAFESTR(filename), def, block, NULL, file->layer);
	file->file_base.fullname = allocAddFilename(filename);
}

bool openTerrainFileWriteSource(TerrainFile *file, bool validate)
{
	char filename[MAX_PATH];
	if (validate)
	{
		sprintf(filename, "%s.validate", file->file_base.fullname);
	}
	else
	{
		strcpy(filename, file->file_base.fullname);
	}
	file->file = fileOpen(filename, "wb");
	if (!file->file) {
		Alertf("Couldn't open file for writing: %s", file->file_base.fullname);
		return false;
	}
	return true;
}

bool openTerrainFileReadSource(TerrainFile *file, bool validate)
{
	char filename[MAX_PATH];
	if (validate)
	{
		sprintf(filename, "%s.validate", file->file_base.fullname);
	}
	else
	{
		strcpy(filename, file->file_base.fullname);
	}
	file->file = fileOpen(filename, "rb");
	if (!file->file) {
		return false;
	}
	return true;
}

bool commitTerrainFileSource(TerrainFile *file)
{
	char filename[MAX_PATH];
	char src_write[MAX_PATH], dest_write[MAX_PATH];
	sprintf(filename, "%s.validate", file->file_base.fullname);

	fileLocateWrite(file->file_base.fullname, dest_write);
	fileLocateWrite(filename, src_write);

	// File we're supposed to keep exists?
	if (!fileExists(src_write))
		return false;

	// Remove the file we're replacing
	if (fileExists(dest_write) && fileForceRemove(dest_write) < 0)
	{
		Alertf("Could not replace file: %s", file->file_base.fullname);
		return false;
	}

	if (rename(src_write, dest_write) != 0)
	{
		Alertf("Could not rename file: %s", file->file_base.fullname);
		return false;
	}

	return true;
}

void deleteTerrainFileSource(TerrainFile *file)
{
	if (file->file)
	{
		Alertf("Attempting to delete open file: %s", file->file_base.fullname);
		return;
	}

	if (!fileExists(file->file_base.fullname))
		return;

	if (fileForceRemove(file->file_base.fullname) < 0)
	{
		Alertf("Could not delete file: %s", file->file_base.fullname);
	}
}

static int terrain_source_error_count = 0;

SimpleBufHandle readTerrainFileHogg(ZoneMapLayer *layer, int blocknum, char *relpath)
{
	TerrainBlockRange *range = layer->terrain.blocks[blocknum];
	char filename[MAX_PATH];
	HogFile *hog_file = NULL;

	terrainLock();
	if (!range->interm_file)
	{
		bool created;
		int err_return;
		char base_dir[MAX_PATH];

		worldGetTempBaseDir(layer->filename, SAFESTR(base_dir));
		sprintf(filename, "%s/%s_intermediate.hogg", base_dir, layer->terrain.blocks[blocknum]->range_name);
		binNotifyTouchedOutputFile(filename);
		hog_file = hogFileReadEx(filename, &created, PIGERR_ASSERT, &err_return, HOG_NOCREATE | HOG_READONLY | HOG_NO_INTERNAL_TIMESTAMPS, 1024);
		if (hog_file)
		{
			range->interm_file = hog_file;
		}
	}
	else
	{
		hog_file = range->interm_file;
	}
	terrainUnlock();

	if (hog_file && relpath)
		return SimpleBufOpenRead(relpath, hog_file);

	//verbose_printf("Failed to open terrain bin %s\n", filename);

	return NULL;
}

void layerTerrainUpdateFilenames(ZoneMapLayer *layer, TerrainBlockRange *block)
{
	block->source_files.heightmap.layer = layer;
	updateTerrainFileSource(&heightmap_def, block, &block->source_files.heightmap);

	block->source_files.objectmap.layer = layer;
	updateTerrainFileSource(&objectmap_def, block, &block->source_files.objectmap);

	block->source_files.colormap.layer = layer;
	updateTerrainFileSource(&colormap_def, block, &block->source_files.colormap);

	block->source_files.tiffcolormap.layer = layer;
	updateTerrainFileSource(&tiffcolormap_def, block, &block->source_files.tiffcolormap);

	block->source_files.soildepthmap.layer = layer;
	updateTerrainFileSource(&soildepthmap_def, block, &block->source_files.soildepthmap);

	block->source_files.materialmap.layer = layer;
	updateTerrainFileSource(&materialmap_def, block, &block->source_files.materialmap);

	block->source_files.holemap.layer = layer;
	updateTerrainFileSource(&holemap_def, block, &block->source_files.holemap);
}

bool layerTerrainAddBlock(ZoneMapLayer *layer, IVec2 min_block, IVec2 max_block)
{
	assert(wl_state.add_terrain_source_callback);
	return wl_state.add_terrain_source_callback(layer, min_block, max_block);
}

static void initTerrainBlock(ZoneMapLayer *layer, TerrainBlockRange *block, int blocknum, bool from_bins)
{
	bool check_timestamps = false;
	block->block_idx = blocknum;
	block->need_bins = false;

	if (!from_bins)
	{
		if (!block->range_name || !strcmp(block->range_name, ""))
		{
			char default_name[256];
			sprintf(default_name, "%s_B%d", layer->name, blocknum);
			if (block->range_name)
				StructFreeString(block->range_name);
			block->range_name = StructAllocString(default_name);
		}

		layerTerrainUpdateFilenames(layer, block);
	}

	if (wlIsServer())
	{
		if (isDevelopmentMode())
			check_timestamps = true;
		else if (isProductionEditMode())
			check_timestamps = resNamespaceIsUGC(layer->zmap_parent->map_info.map_name);
	}
	if (check_timestamps &&
		terrainBlockCheckTimestamps(layer, block) != 0)
	{
		layer->terrain.need_bins = true;
		block->need_bins = true;
	}
}

void initTerrainBlocks(ZoneMapLayer *layer, bool from_bins)
{
	int i;
	for (i = 0; i < eaSize(&layer->terrain.blocks); ++i)
	{
		initTerrainBlock(layer, layer->terrain.blocks[i], i, from_bins);
	}
}

void updateTerrainBlocks(ZoneMapLayer *layer)
{
	int i;
	for (i = 0; i < eaSize(&layer->terrain.blocks); ++i)
	{
		TerrainBlockRange *block = layer->terrain.blocks[i];
		if (!block->map_ranges_array)
		{
			SimpleBufHandle buf;
			if (buf = readTerrainFileHogg(layer, block->block_idx, "bounds.dat"))
			{
				int ranges_array_size = (block->range.max_block[0]+1-block->range.min_block[0])*(block->range.max_block[2]+1-block->range.min_block[2]); 
				block->map_ranges_array = malloc(ranges_array_size * 2 * sizeof(F32));
				SimpleBufReadF32Array(block->map_ranges_array, ranges_array_size * 2, buf);
				SimpleBufClose(buf);
			}
		}
	}
	return;
}

void deinitTerrainBlock(TerrainBlockRange *block)
{
	if (block->interm_file)
	{
		//printf("Closing read-only intermediate %s %p\n", hogFileGetArchiveFileName(block->interm_file), block);
		hogFileDestroy(block->interm_file, true);
	}
	block->interm_file = NULL;
	SAFE_FREE(block->map_ranges_array);
}

int layerGetTerrainBlockCount(ZoneMapLayer *layer)
{
	return eaSize(&layer->terrain.blocks);
}

// Terrain Objects

TerrainObjectWrapper *layerCreateObjectWrapper(ZoneMapLayer *layer, int index)
{
	char layer_name[64];
	TerrainObjectWrapper *wrapper = (TerrainObjectWrapper *)calloc(1, sizeof(TerrainObjectWrapper));

    wrapper->layer = (ZoneMapLayer*)calloc(1, sizeof(ZoneMapLayer));
	wrapper->layer->layer_mode = LAYER_MODE_GROUPTREE;
	wrapper->layer->dummy_layer = layer;
	wrapper->layer->region_name = layer->region_name;
	wrapper->layer->zmap_parent = layer->zmap_parent;
	sprintf(layer_name, "TERRAIN_OBJECT_DUMMY_%d", index);
	wrapper->layer->filename = strdup(layer_name); // strdupping into a pooled string field...

	wrapper->def_lib = groupLibCreate(false);
	wrapper->def_lib->zmap_layer = wrapper->layer;

	wrapper->root_def = StructCreate(parse_GroupDef);
	wrapper->root_def->name_uid = 1;
	wrapper->root_def->name_str = allocAddString("dummy");
	wrapper->root_def->is_layer = 1;
	groupLibAddGroupDef(wrapper->def_lib, wrapper->root_def, NULL);

	return wrapper;
}

void layerFreeObjectWrapperEntries(TerrainObjectWrapper *wrapper)
{
	freeLayerCellEntries(wrapper->layer);
}

void layerUpdateObjectWrapperEntries(TerrainObjectWrapper *wrapper)
{
	freeLayerCellEntries(wrapper->layer);
	worldEntryCreateForDefTree(wrapper->layer, wrapper->root_def, NULL);
}

void layerFreeObjectWrapper(TerrainObjectWrapper *wrapper)
{
	layerFreeObjectWrapperEntries(wrapper);
	groupLibFree(wrapper->def_lib);
	SAFE_FREE(wrapper->layer);
	SAFE_FREE(wrapper);
}

void layerInitObjectWrappers(ZoneMapLayer *layer, int table_size)
{
	int i;
	eaDestroyEx(&layer->terrain.object_defs, layerFreeObjectWrapper);
	for (i = 0; i < table_size; i++)
	{
		eaPush(&layer->terrain.object_defs, layerCreateObjectWrapper(layer->dummy_layer ? layer->dummy_layer : layer, i));
	}
}

void layerLoadTerrainObjects(ZoneMapLayer *layer)
{
	IVec2 rel_pos;
	int i, count = 0;

	if (eaSize(&layer->terrain.blocks) == 0)
		return;

	loadstart_printf("Loading terrain objects...");

	layerInitObjectWrappers(layer, eaSize(&layer->terrain.object_table));

    eaDestroyEx(&layer->terrain.binned_instance_groups, terrainFreeTerrainBinnedObjectGroup);

	for (i = 0; i < eaSize(&layer->terrain.blocks); i++)
	{
		TerrainBlockRange *range = layer->terrain.blocks[i];
		IVec2 local_pos;

		if (range->need_bins)
			continue;

		for (local_pos[1] = range->range.min_block[2]; local_pos[1] <= range->range.max_block[2]; ++local_pos[1])
		{
			for (local_pos[0] = range->range.min_block[0]; local_pos[0] <= range->range.max_block[0]; ++local_pos[0])
			{
				rel_pos[0] = local_pos[0] - range->range.min_block[0];
				rel_pos[1] = local_pos[1] - range->range.min_block[2];
				count += heightMapLoadTerrainObjects(layer, range, rel_pos, false);
			}
		}
	}

	terrainUpdateObjectGroups(layer, true);

	loadend_printf(" done (%d instances).", count);
}

TerrainBlockRange *layerGetTerrainBlock(ZoneMapLayer *layer, int block_idx)
{
	if (block_idx < 0 || block_idx >= eaSize(&layer->terrain.blocks))
		return NULL;

	return layer->terrain.blocks[block_idx];
}

int layerGetTerrainBlockForLocalPos(ZoneMapLayer *layer, IVec2 local_pos)
{
	TerrainBlockRange test_range;
	int i;

	setVec3(test_range.range.min_block, local_pos[0], 0, local_pos[1]);
	setVec3(test_range.range.max_block, local_pos[0], 0, local_pos[1]);

	for (i = 0; i < eaSize(&layer->terrain.blocks); i++)
	{
		if (trangesOverlap(&test_range, layer->terrain.blocks[i]))
			return i;
	}

	return -1;
}

bool layerIsOverlappingTerrainBlock(ZoneMapLayer *layer, IVec2 local_pos, IVec2 size)
{
	TerrainBlockRange test_range;
	TerrainBlockRange **terrain_blocks = NULL;
	int i;

	setVec3(test_range.range.min_block, local_pos[0], 0, local_pos[1]);
	test_range.range.max_block[0] = local_pos[0]+size[0]-1;
	test_range.range.max_block[2] = local_pos[1]+size[1]-1;

	if(layer->terrain.source_data)
		terrain_blocks = layer->terrain.source_data->blocks;
	else
		terrain_blocks = layer->terrain.blocks;

	for (i = 0; i < eaSize(&terrain_blocks); i++)
	{
		if (trangesOverlap(&test_range, terrain_blocks[i]))
			return true;
	}
	return false;
}

bool layerGetTerrainBlockExtents(ZoneMapLayer *layer, int block_idx, IVec2 min, IVec2 max)
{
	if (block_idx < 0 ||
		block_idx >= eaSize(&layer->terrain.blocks))
		return false;

	setVec2(min, layer->terrain.blocks[block_idx]->range.min_block[0],
				layer->terrain.blocks[block_idx]->range.min_block[2]);
	setVec2(max, layer->terrain.blocks[block_idx]->range.max_block[0],
				layer->terrain.blocks[block_idx]->range.max_block[2]);

	return true;
}

const char *layerGetTerrainObjectEntryName(TerrainObjectEntry *object_info)
{
	return object_info->objInfo.name_str;
}

void layerUpdateGeometry(ZoneMapLayer *layer, bool update_trackers)
{
	terrainUpdateObjectGroups(layer, false);

	if (update_trackers)
		layerTrackerUpdate(layer, false, false);
}

bool layerGetPlayable(SA_PARAM_NN_VALID ZoneMapLayer *layer)
{
	return !layer->terrain.non_playable;
}

void layerSetPlayable(SA_PARAM_NN_VALID ZoneMapLayer *layer, bool playable)
{
    layer->terrain.non_playable = !playable;
	layerIncrementModTime(layer);        
}

TerrainExclusionVersion layerGetExclusionVersion(SA_PARAM_NN_VALID ZoneMapLayer *layer)
{
	return layer->terrain.exclusion_version;
}

F32 layerGetColorShift(SA_PARAM_NN_VALID ZoneMapLayer *layer)
{
	return layer->terrain.color_shift;
}
