/***************************************************************************



***************************************************************************/
#define GENESIS_ALLOW_OLD_HEADERS
#include "WorldCellStreaming.h"

#include "RoomConn.h"
#include "UGCProjectUtils.h"
#include "WorldCell.h"
#include "WorldCellBinning.h"
#include "WorldCellEntryPrivate.h"
#include "WorldCellStreamingPrivate.h"
#include "WorldGrid.h"
#include "dynAnimInterface.h"
#include "dynWind.h"
#include "wcoll/collcache.h"
#include "wlBeacon.h"
#include "wlEncounter.h"
#include "wlGenesis.h"
#include "wlModelBinning.h"
#include "wlState.h"
#include "wlTerrainPrivate.h"
#include "wlUGC.h"
#include "wlVolumes.h"

#include "WorldCellEntry_h_ast.h"
#include "WorldCellClustering.h"

#include "timing.h"
#include "FolderCache.h"
#include "hoglib.h"
#include "fileutil.h"
#include "fileutil2.h"
#include "serialize.h"
#include "utilitiesLib.h"
#include "StructPack.h"
#include "trivia.h"
#include "logging.h"
#include "StringCache.h"
#include "tokenstore.h"
#include "TimedCallback.h"
#include "crypt.h"
#include "structInternals.h"
#include "bounds.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

typedef struct WorldCellParsed WorldCellParsed;
typedef struct GeoRenderInfo GeoRenderInfo;


#include "AutoGen/WorldCellStreamingPrivate_h_ast.c"

//////////////////////////////////////////////////////////////////////////

static int disable_background_loading = 0;
static int force_rebin;
static int force_create_terrain_atlases;
static int bin_set_timestamps_to_gimme = 1;
static int rebuild_bins_with_errors;
static int rebuilt_bins = 0;
static int write_encounter_info = 1;
static int force_write_encounter_info = 0;

bool world_created_bins;

char last_load_error[1024];

AUTO_CMD_INT(disable_background_loading, worldCellDisableBackgroundStreaming) ACMD_CATEGORY(Debug);

// forces the world bins to get rebuilt
AUTO_CMD_INT(force_rebin, worldCellForceRebin) ACMD_CMDLINE ACMD_CATEGORY(Debug) ACMD_CATEGORY(CommandLine);

// forces the terrain atlases to get processed
AUTO_CMD_INT(force_create_terrain_atlases, terrainForceAtlasProcess) ACMD_CMDLINE ACMD_CATEGORY(Debug) ACMD_CATEGORY(CommandLine);

// deprecated, does nothing
AUTO_CMD_INT(bin_set_timestamps_to_gimme, worldForceBinGimmeTimestamps) ACMD_CMDLINE ACMD_CATEGORY(CommandLine);

// write encounter info if we are binning or it doesn't exist
AUTO_CMD_INT(write_encounter_info, worldWriteEncounterInfo) ACMD_CMDLINE ACMD_CATEGORY(Debug) ACMD_CATEGORY(CommandLine);

// force encounter info to rebuild even if it exists
AUTO_CMD_INT(force_write_encounter_info, worldForceWriteEncounterInfo) ACMD_CMDLINE ACMD_CATEGORY(Debug) ACMD_CATEGORY(CommandLine);


// forces world cell bins that had errors in them to get rebuilt
AUTO_COMMAND ACMD_CMDLINE ACMD_CATEGORY(CommandLine);
void worldCellRebuildBinsWithErrors(int iSet)
{
#if !PLATFORM_CONSOLE
	rebuild_bins_with_errors = iSet;
#endif
}

static void __stdcall backgroundFreeStruct(void *structptr, void *pti, int unused)
{
	//destory the struct in the background thread. All references will be alreay destroyed
	PERFINFO_AUTO_START_FUNC();
	StructDestroyVoid(pti, structptr);
	PERFINFO_AUTO_STOP();
}

void backgroundRefOperationPump(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	void** tempUserData;
	void* structptr;
	void* pti;
	//we are in the main thread now so we can flush the operations
	TokenStoreFlushQueuedReferenceLookups();

	tempUserData = (void**)userData;
	pti = tempUserData[0];
	structptr = tempUserData[1];
	//fire off the background thread again since we have now destroyed all the refs
	geoRequestBackgroundExec(backgroundFreeStruct, NULL, (GeoRenderInfo *)structptr, pti, 0, FILE_HIGHEST_PRIORITY);
	
	//free the userdata packet from the main thread
	free(userData);
}

static void __stdcall backgroundQueueRefDestruction(void *structptr, void *pti, int unused)
{
	void** tempUserData;
	
	PERFINFO_AUTO_START_FUNC();
	//queue up all the ref destructions
	ParserClearAndDestroyRefs(pti, structptr);

	//we must flush the ref operation queue in the main thread before we free the structs
	tempUserData = malloc(sizeof(void*)*2);
	tempUserData[0] = pti;
	tempUserData[1] = structptr;
	TimedCallback_Run(backgroundRefOperationPump, tempUserData, 0);
	PERFINFO_AUTO_STOP();
}

static void	StructDestroySafeInBackground(ParseTable pti[], SA_PRE_NN_VALID SA_POST_FREE void** structptr)
{
	if (!structptr || !*structptr)
		return;

	//we are kind of abusing the pointer casting here, but should be ok since we supply the callback
	geoRequestBackgroundExec(backgroundQueueRefDestruction, NULL, (GeoRenderInfo *)*structptr, pti, 0, FILE_HIGHEST_PRIORITY);
	*structptr = NULL;
}

static WorldCell *findCell(WorldRegion *region, BlockRange *extents)
{
	WorldCell *cell = region->root_world_cell;

	while (cell)
	{
		int child_idx = 0;
		IVec3 size;

		if (sameVec3(cell->cell_block_range.min_block, extents->min_block) && sameVec3(cell->cell_block_range.max_block, extents->max_block))
			return cell;

		if (cell->vis_dist_level == 0)
			return NULL;

		rangeSize(&cell->cell_block_range, size);
		assert(size[0] == size[1]);
		assert(size[0] == size[2]);

		size[0] = size[0] >> 1;
		size[1] = size[1] >> 1;
		size[2] = size[2] >> 1;

		if (extents->min_block[0] >= cell->cell_block_range.min_block[0] + size[0])
			child_idx |= 1;

		if (extents->min_block[1] >= cell->cell_block_range.min_block[1] + size[1])
			child_idx |= 2;

		if (extents->min_block[2] >= cell->cell_block_range.min_block[2] + size[2])
			child_idx |= 4;

		if (!cell->children[child_idx])
		{
			BlockRange child_range;

			if (child_idx&1)
				child_range.min_block[0] = cell->cell_block_range.min_block[0] + size[0];
			else
				child_range.min_block[0] = cell->cell_block_range.min_block[0];
			child_range.max_block[0] = child_range.min_block[0] + size[0]-1;

			if (child_idx&2)
				child_range.min_block[1] = cell->cell_block_range.min_block[1] + size[1];
			else
				child_range.min_block[1] = cell->cell_block_range.min_block[1];
			child_range.max_block[1] = child_range.min_block[1] + size[1]-1;

			if (child_idx&4)
				child_range.min_block[2] = cell->cell_block_range.min_block[2] + size[2];
			else
				child_range.min_block[2] = cell->cell_block_range.min_block[2];
			child_range.max_block[2] = child_range.min_block[2] + size[2]-1;

			cell->children[child_idx] = worldCellCreate(region, cell, &child_range);
			worldCellFreeCullHeaders(region->world_cell_headers); // added a cell, must recreate the headers
			region->world_cell_headers = NULL;
		}

		cell = cell->children[child_idx];
	}

	return NULL;
}

static WorldCellParsed *findParsedCell(WorldRegionCommonParsed *region_parsed, BlockRange *extents)
{
	int i;
	for (i = 0; i < eaSize(&region_parsed->cells); ++i)
	{
		WorldCellParsed *cell = region_parsed->cells[i];
		if (sameVec3(cell->block_range.min_block, extents->min_block) && sameVec3(cell->block_range.max_block, extents->max_block))
			return cell;
	}

	return NULL;
}

static int findParsedCellIdx(WorldRegionCommonParsed *region_parsed, BlockRange *extents)
{
	int i;
	for (i = 0; i < eaSize(&region_parsed->cells); ++i)
	{
		WorldCellParsed *cell = region_parsed->cells[i];
		if (sameVec3(cell->block_range.min_block, extents->min_block) && sameVec3(cell->block_range.max_block, extents->max_block))
			return i;
	}

	return -1;
}

void worldCellSetupChildCellRange(WorldCell *cell_dst, BlockRange *child_range, IVec3 size, int index)
{
	if (index&1)
		child_range->min_block[0] = cell_dst->cell_block_range.min_block[0] + size[0];
	else
		child_range->min_block[0] = cell_dst->cell_block_range.min_block[0];
	child_range->max_block[0] = child_range->min_block[0] + size[0]-1;

	if (index&2)
		child_range->min_block[1] = cell_dst->cell_block_range.min_block[1] + size[1];
	else
		child_range->min_block[1] = cell_dst->cell_block_range.min_block[1];
	child_range->max_block[1] = child_range->min_block[1] + size[1]-1;

	if (index&4)
		child_range->min_block[2] = cell_dst->cell_block_range.min_block[2] + size[2];
	else
		child_range->min_block[2] = cell_dst->cell_block_range.min_block[2];
	child_range->max_block[2] = child_range->min_block[2] + size[2]-1;
}

static void fillCellData(WorldCell *cell_dst, WorldCellParsed *cell_src, WorldRegionCommonParsed *region_parsed)
{
	IVec3 size;
	int i;

	if (!cell_dst || !cell_src)
		return;

	cell_dst->streaming_mode = 1;
	cell_dst->is_empty = cell_src->is_empty;
	cell_dst->no_drawables = cell_src->no_drawables;
	cell_dst->no_collision = cell_src->no_collision;
	cell_dst->region_parsed = region_parsed;
	cell_dst->cluster_related = cell_src->contain_cluster;

	if (cell_dst->cluster_related)
		cell_dst->region->bWorldGeoClustering = true;

	copyVec3(cell_src->draw_min, cell_dst->drawable.world_min);
	copyVec3(cell_src->draw_max, cell_dst->drawable.world_max);
	cell_dst->drawable.radius = boxCalcMid(cell_dst->drawable.world_min, cell_dst->drawable.world_max, cell_dst->drawable.world_mid);

	copyVec3(cell_src->coll_min, cell_dst->collision.world_min);
	copyVec3(cell_src->coll_max, cell_dst->collision.world_max);
	cell_dst->collision.radius = boxCalcMid(cell_dst->collision.world_min, cell_dst->collision.world_max, cell_dst->collision.world_mid);

	copyVec3(cell_src->bounds_min, cell_dst->bounds.world_min);
	copyVec3(cell_src->bounds_max, cell_dst->bounds.world_max);

	if (!cell_dst->vis_dist_level)	// no point in going any further if we're at the bottom
	{
		return;
	}

	// recurse

	rangeSize(&cell_dst->cell_block_range, size);
	assert(size[0] == size[1]);
	assert(size[0] == size[2]);

	size[0] = size[0] >> 1;
	size[1] = size[1] >> 1;
	size[2] = size[2] >> 1;

	if (size[0] > 0)
	{
		for (i = 0; i < ARRAY_SIZE(cell_dst->children); ++i)
		{
			BlockRange child_range;
			WorldCellParsed *child_cell_src;

			worldCellSetupChildCellRange(cell_dst, &child_range, size, i);

			child_cell_src = findParsedCell(region_parsed, &child_range);
			if (child_cell_src)
			{
				cell_dst->children[i] = worldCellCreate(cell_dst->region, cell_dst, &child_range);
				fillCellData(cell_dst->children[i], child_cell_src, region_parsed);
			}
		}
	}
}

static void loadCellEntries(WorldRegion *region, WorldRegionCommonParsed *region_parsed, int parsed_cell_idx, void** out_parsed_data)
{
	WorldCellParsed *cell_parsed = parsed_cell_idx >= 0 ? region_parsed->cells[parsed_cell_idx] : NULL;
	char filename[MAX_PATH];

	if (!cell_parsed)
		return;

	sprintf(filename, "d%d", cell_parsed->cell_id);
	if (hogFileFind(region_parsed->hog_file, filename)!=HOG_INVALID_INDEX)
	{
		SimpleBufHandle data_file;
		data_file = ParserOpenBinaryFile(region_parsed->hog_file, filename, NULL, BINARYREADFLAG_IGNORE_CRC, NULL);
		if (wlIsServer())
		{
			WorldCellServerDataParsed* server_data_parsed = StructAlloc(parse_WorldCellServerDataParsed);
			ParserReadBinaryFile(data_file, parse_WorldCellServerDataParsed, server_data_parsed, NULL, NULL, NULL, NULL, 0,0, true);
			*out_parsed_data = server_data_parsed;
		}
		else
		{
			WorldCellClientDataParsed* client_data_parsed = StructAlloc(parse_WorldCellClientDataParsed);
			ParserReadBinaryFile(data_file, parse_WorldCellClientDataParsed, client_data_parsed, NULL, NULL, NULL, NULL, 0,0, true);
			*out_parsed_data = client_data_parsed;
		}
	}
}

static void createDrawablesFromParsed(WorldRegion *region, WorldCell *cell, WorldCellDrawableDataParsed *data, WorldCellWeldedBin *welded_bin_dst, bool parsed_will_be_freed)
{
	ZoneMap *zmap = region->zmap_parent;
	int j;

	for (j = 0; j < eaSize(&data->occlusion_entries); ++j)
	{
		WorldCellEntry *entry = createWorldOcclusionEntryFromParsed(zmap, data->occlusion_entries[j]);
		if (welded_bin_dst)
			worldWeldedBinAddEntryFromParsed(welded_bin_dst, entry);
		else
			worldEntryInitFromParsed(zmap, region, cell, entry, &data->occlusion_entries[j]->base_data, false);
	}

	for (j = 0; j < eaSize(&data->model_entries); ++j)
	{
		WorldCellEntry *entry = createWorldModelEntryFromParsed(zmap, data->model_entries[j], cell, parsed_will_be_freed);
		if (welded_bin_dst)
			worldWeldedBinAddEntryFromParsed(welded_bin_dst, entry);
		else
			worldEntryInitFromParsed(zmap, region, cell, entry, &data->model_entries[j]->base_drawable.base_data, data->model_entries[j]->base_drawable.is_near_fade);
	}

	for (j = 0; j < eaSize(&data->model_instance_entries); ++j)
	{
		WorldCellEntry *entry = createWorldModelInstanceEntryFromParsed(zmap, data->model_instance_entries[j], parsed_will_be_freed);
		if (welded_bin_dst)
			worldWeldedBinAddEntryFromParsed(welded_bin_dst, entry);
		else
			worldEntryInitFromParsed(zmap, region, cell, entry, &data->model_instance_entries[j]->base_drawable.base_data, data->model_instance_entries[j]->base_drawable.is_near_fade);
	}

	for (j = 0; j < eaSize(&data->spline_entries); ++j)
	{
		WorldCellEntry *entry = createWorldSplinedModelEntryFromParsed(zmap, data->spline_entries[j], parsed_will_be_freed);
		if (welded_bin_dst)
			worldWeldedBinAddEntryFromParsed(welded_bin_dst, entry);
		else
			worldEntryInitFromParsed(zmap, region, cell, entry, &data->spline_entries[j]->base_drawable.base_data, data->spline_entries[j]->base_drawable.is_near_fade);
	}
}

static void createCellEntries(WorldRegion *region, WorldCell *cell, void* data_parsed, HogFile * region_cell_hog, bool create_all, bool free_parsed)
{
	ZoneMap *zmap = region->zmap_parent;
	WorldStreamingInfo *streaming_info = zmap->world_cell_data.streaming_info;
	WorldStreamingPooledInfo *streaming_pooled_info = zmap->world_cell_data.streaming_pooled_info;
	int j;

	if (wlIsServer())
	{
		if (data_parsed)
		{
			WorldCellServerDataParsed* data = (WorldCellServerDataParsed*)data_parsed;
			
			//we need to support the null cell case but if you pass a cell, make sure things are sane so we dont leak stuff
			assert(!cell || cell->server_data_parsed == data_parsed);

			TokenStoreFlushQueuedReferenceLookups(); //this was loaded in the background thread so flush everything before we use it

			// interaction entries must be created first
			for (j = 0; j < eaSize(&data->interaction_entries); ++j)
			{
				WorldCellEntry *entry = createWorldInteractionEntryFromServerParsed(streaming_pooled_info, data->interaction_entries[j], cell && cell->streaming_mode, free_parsed);
				worldEntryInitFromParsed(zmap, region, cell, entry, &data->interaction_entries[j]->base_data, false);
				addWorldInteractionEntryPostParse(zmap, (WorldInteractionEntry *)entry, data->interaction_entries[j]->uid);
			}

			for (j = 0; j < eaSize(&data->volume_entries); ++j)
			{
				WorldCellEntry *entry = createWorldVolumeEntryFromServerParsed(zmap, streaming_info, streaming_pooled_info, data->volume_entries[j], create_all, free_parsed);
				if (entry)
				{
					worldEntryInitFromParsed(zmap, region, cell, entry, &data->volume_entries[j]->base_data, false);
					if (data->volume_entries[j]->attached_to_parent)
					{
						WorldCellEntryData *entry_data = worldCellEntryGetData(entry);
						if (SAFE_MEMBER(entry_data, parent_entry))
							entry_data->parent_entry->attached_volume_entry = (WorldVolumeEntry*)entry;
					}
				}
			}

			for (j = 0; j < eaSize(&data->collision_entries); ++j)
			{
				WorldCellEntry *entry = createWorldCollisionEntryFromParsed(zmap, streaming_pooled_info, data->collision_entries[j]);
				worldEntryInitFromParsed(zmap, region, cell, entry, &data->collision_entries[j]->base_data, false);
			}

			for (j = 0; j < eaSize(&data->altpivot_entries); ++j)
			{
				WorldCellEntry *entry = createWorldAltPivotEntryFromParsed(streaming_pooled_info, data->altpivot_entries[j]);
				worldEntryInitFromParsed(zmap, region, cell, entry, &data->altpivot_entries[j]->base_data, false);
			}

			if (free_parsed)
			{
				StructDestroySafeInBackground(parse_WorldCellServerDataParsed, &data_parsed);
				if (cell)
					cell->server_data_parsed = NULL;
			}
		}
	}
	else
	{
		if (data_parsed)
		{
			WorldCellClientDataParsed* data = (WorldCellClientDataParsed*)data_parsed;
			WorldRegion *light_region = worldCellGetLightRegion(region);

			//we need to support the null cell case but if you pass a cell, make sure things are sane so we dont leak stuff
			assert(!cell || cell->client_data_parsed == data_parsed);

			TokenStoreFlushQueuedReferenceLookups(); //this was loaded in the background thread so flush everything before we use it

			// interaction entries must be created first
			for (j = 0; j < eaSize(&data->interaction_entries); ++j)
			{
				WorldCellEntry *entry = createWorldInteractionEntryFromClientParsed(streaming_pooled_info, data->interaction_entries[j], cell && cell->streaming_mode, free_parsed);
				worldEntryInitFromParsed(zmap, region, cell, entry, &data->interaction_entries[j]->base_data, false);
				addWorldInteractionEntryPostParse(zmap, (WorldInteractionEntry *)entry, data->interaction_entries[j]->uid);
			}

			for (j = 0; j < eaSize(&data->volume_entries); ++j)
			{
				WorldCellEntry *entry = createWorldVolumeEntryFromClientParsed(zmap, streaming_info, streaming_pooled_info, data->volume_entries[j], create_all, free_parsed);
				if (entry)
					worldEntryInitFromParsed(zmap, region, cell, entry, &data->volume_entries[j]->base_data, false);
			}

			for (j = 0; j < eaSize(&data->collision_entries); ++j)
			{
				WorldCellEntry *entry = createWorldCollisionEntryFromParsed(zmap, streaming_pooled_info, data->collision_entries[j]);
				worldEntryInitFromParsed(zmap, region, cell, entry, &data->collision_entries[j]->base_data, false);
			}

			for (j = 0; j < eaSize(&data->altpivot_entries); ++j)
			{
				WorldCellEntry *entry = createWorldAltPivotEntryFromParsed(streaming_pooled_info, data->altpivot_entries[j]);
				worldEntryInitFromParsed(zmap, region, cell, entry, &data->altpivot_entries[j]->base_data, false);
			}

			// fx and animation entries must be created before drawable entries, animation before anything else
			// CD - this is not true anymore now that they use reference dictionaries

			for (j = 0; j < eaSize(&data->animation_entries); ++j)
			{
				WorldCellEntry *entry = createWorldAnimationEntryFromParsed(zmap, data->animation_entries[j]);
				worldEntryInitFromParsed(zmap, region, cell, entry, &data->animation_entries[j]->base_data, false);
			}

			for (j = 0; j < eaSize(&data->fx_entries); ++j)
			{
				WorldCellEntry *entry = createWorldFXEntryFromParsed(zmap, data->fx_entries[j]);
				worldEntryInitFromParsed(zmap, region, cell, entry, &data->fx_entries[j]->base_data, false);
			}

			for (j = 0; j < eaSize(&data->sound_entries); ++j)
			{
				WorldCellEntry *entry = createWorldSoundEntryFromParsed(streaming_pooled_info, data->sound_entries[j]);
				worldEntryInitFromParsed(zmap, region, cell, entry, &data->sound_entries[j]->base_data, false);
			}

			for (j = 0; j < eaSize(&data->light_entries); ++j)
			{
				WorldCellEntry *entry = createWorldLightEntryFromParsed(zmap, region, data->light_entries[j], free_parsed);
				worldEntryInitFromParsed(zmap, light_region, cell, entry, &data->light_entries[j]->base_data, false);
			}

			for (j = 0; j < eaSize(&data->wind_source_entries); ++j)
			{
				WorldCellEntry *entry = createWorldWindSourceEntryFromParsed(zmap, region, data->wind_source_entries[j]);
				worldEntryInitFromParsed(zmap, region, cell, entry, &data->wind_source_entries[j]->base_data, false);
			}

			createDrawablesFromParsed(region, cell, &data->drawables, NULL, free_parsed);

			if (free_parsed)
			{
				StructDestroySafeInBackground(parse_WorldCellClientDataParsed, &data_parsed);
				if (cell)
					cell->client_data_parsed = NULL;
			}
		}
	}
}

static void loadWeldedCellEntries(WorldRegionCommonParsed *region_parsed, int parsed_cell_idx, WorldCellWeldedDataParsed **welded_data)
{
	WorldCellParsed *cell_parsed = parsed_cell_idx >= 0 ? region_parsed->cells[parsed_cell_idx] : NULL;
	char welded_filename[MAX_PATH];

	if (!cell_parsed)
		return;

	sprintf(welded_filename, "w%d", cell_parsed->cell_id);
	if (hogFileFind(region_parsed->hog_file, welded_filename)!=HOG_INVALID_INDEX)
	{
		SimpleBufHandle data_file;
		data_file = ParserOpenBinaryFile(region_parsed->hog_file, welded_filename, parse_WorldCellWeldedDataParsed, BINARYREADFLAG_IGNORE_CRC, NULL);
		*welded_data = StructAlloc(parse_WorldCellWeldedDataParsed);
		ParserReadBinaryFile(data_file, parse_WorldCellWeldedDataParsed, *welded_data, NULL, NULL, NULL, NULL, 0,0, true);
	}
}

static void createWeldedCellEntries(WorldRegion *region, WorldCell *cell, bool free_data)
{
	ZoneMap *zmap = region->zmap_parent;
	WorldStreamingInfo *streaming_info = zmap->world_cell_data.streaming_info;
	int i;

	if (cell && cell->welded_data_parsed)
	{
		TokenStoreFlushQueuedReferenceLookups(); //this was loaded in the background thread so flush everything before we use it
		for (i = 0; i < eaSize(&cell->welded_data_parsed->bins); ++i)
		{
			WorldCellDrawableDataParsed *welded_bin = cell->welded_data_parsed->bins[i];
			WorldCellWeldedBin *welded_bin_dst;
			
			welded_bin_dst = worldWeldedBinInitFromParsed(cell, welded_bin);
			createDrawablesFromParsed(region, cell, welded_bin, welded_bin_dst, free_data);
		}
		if (free_data)
			StructDestroySafeInBackground(parse_WorldCellWeldedDataParsed, &cell->welded_data_parsed);
	}
}

static void createAllRegionCellEntries(WorldRegion *region, WorldRegionCommonParsed *region_parsed)
{
	int i;
	for (i = 0; i < eaSize(&region_parsed->cells); ++i)
	{
		void* data_parsed = NULL; //this is either an WorldCellServerDataParsed or a WorldCellClientDataParsed
		loadCellEntries(region, region_parsed, i, &data_parsed);
		createCellEntries(region, NULL, data_parsed, region_parsed->hog_file, true, true);
	}
}

static void deleteOldBins(SA_PARAM_OP_VALID BinFileList *file_list, const char *header_filename)
{
	if (file_list && isDevelopmentMode())
	{
		char abs_name[MAX_PATH], new_name[MAX_PATH];
		int i;
		loadstart_printf("Removing old bin files...");
		for (i = 0; i < eaSize(&file_list->output_files); ++i)
		{
#if !_PS3
			if (strEndsWith(file_list->output_files[i]->filename, "map_snap.hogg"))
			{
				//save map snap images for comparing later
				if (fileExists(file_list->output_files[i]->filename) && !isProductionEditMode() )
				{
					char temp[MAX_PATH];
					char destPath[MAX_PATH];
					sprintf(destPath, "%s/%s", fileTempDir(), file_list->output_files[i]->filename);
					makeDirectoriesForFile(destPath);
					fileCopy(fileLocateWrite(file_list->output_files[i]->filename, temp), destPath);
				}
			}
			else
			if (strEndsWith(file_list->output_files[i]->filename, "_cluster.hogg"))
				continue;
#endif
			fileLocateWrite(file_list->output_files[i]->filename, abs_name);
			strcpy(new_name, abs_name);
			strcat(new_name, ".old");
			if (fileExists(new_name) && fileForceRemove(new_name) < 0)
			{
				Errorf("Failed to remove old bin: %s", new_name);
			}
			if (fileExists(abs_name) && rename(abs_name, new_name) != 0)
			{
				Errorf("Failed to remove bin: %s", file_list->output_files[i]->filename);
			}
		}
		fileLocateWrite(header_filename, abs_name);
		strcpy(new_name, abs_name);
		strcat(new_name, ".old");
		if (fileExists(new_name) && fileForceRemove(new_name) < 0)
		{
			Errorf("Failed to remove old bin: %s", new_name);
		}
		if (fileExists(abs_name) && rename(abs_name, new_name) != 0)
		{
			Errorf("Failed to remove bin: %s", header_filename);
		}
		loadend_printf(" done.");
	}
}

SA_RET_OP_VALID static BinFileList *needsRebinning(const char *header_filename, ZoneMap *zmap, bool terrain_was_rebinned, bool delete_bins)
{
	BinFileList *file_list;
	int i, j;

	// UGC policy: If we're generating a dummy ZoneMapInfo, always bin
	if (isProductionEditMode() && zmap->map_info.from_ugc_file)
	{
		return NULL;
	}

	file_list  = StructAlloc(parse_BinFileList);

	binNotifyTouchedOutputFile(header_filename);

	if (isDevelopmentMode() &&
		!ParserOpenReadBinaryFile(NULL, header_filename, parse_BinFileList, file_list, NULL, NULL, NULL, NULL, 0, 0, 0))
	{
		sprintf(last_load_error, "Failed to load bin header file \"%s\".", header_filename);
		filelog_printf("world_binning.log", "Rebinning map \"%s\", reason: %s", zmapGetFilename(zmap), last_load_error);
		StructDestroySafe(parse_BinFileList, &file_list);
		return NULL;
	}

	if (force_rebin)
	{
		sprintf(last_load_error, "Command line forced rebinning.");
		filelog_printf("world_binning.log", "Rebinning map \"%s\", reason: %s", zmapGetFilename(zmap), last_load_error);
		if (isDevelopmentMode() && delete_bins)
			deleteOldBins(file_list, header_filename);
		StructDestroySafe(parse_BinFileList, &file_list);
		return NULL;
	}

	if (terrain_was_rebinned)
	{
		sprintf(last_load_error, "Terrain made bins.");
		filelog_printf("world_binning.log", "Rebinning map \"%s\", reason: %s", zmapGetFilename(zmap), last_load_error);
		if (isDevelopmentMode() && delete_bins)
			deleteOldBins(file_list, header_filename);
		StructDestroySafe(parse_BinFileList, &file_list);
		return NULL;
	}

	if (isDevelopmentMode())
	{
		bool failed = false;

		if (!wlIsServer())
		{
			if (worldGetMapClustered(zmap) && worldCellClusterIsRemeshEnabled(NULL))
			{
				char clusterName[MAX_PATH];
				ClusterDependency cluster_dependencies;

				zoneMapClusterDepFileName(zmap,SAFESTR(clusterName));
				ParserReadTextFile(clusterName, parse_ClusterDependency, &cluster_dependencies, 0); 

				if (!ParserReadTextFile(clusterName, parse_ClusterDependency, &cluster_dependencies, 0) ||
					(cluster_dependencies.cluster_version != CLUSTER_TOOL_VERSION))
				{
					// write out the correct parser file here
					if (!fileExists(clusterName))
						sprintf(last_load_error, "Cluster.Dep file does not exist.");
					else
						sprintf(last_load_error, "Cluster version is not current.");

					if (worldCellClusteringFeatureEnabled()) {
						filelog_printf("world_binning.log", "Rebinning map \"%s\", reason: %s", zmapGetFilename(zmap), last_load_error);
						if (delete_bins)
							deleteOldBins(file_list, header_filename);
						StructDestroySafe(parse_BinFileList, &file_list);
						return NULL;
					} else {
						filelog_printf("world_binning.log", "WorldCellClustering is not enabled but would otherwise Rebin map \"%s\", reason: %s", zmapGetFilename(zmap), last_load_error);
					}
				}
			} else {
				char clusterName[MAX_PATH];
				zoneMapClusterDepFileName(zmap,SAFESTR(clusterName));
				if (fileExists(clusterName) && fileForceRemove(clusterName) < 0)
				{
					Errorf("Failed to remove cluster dependency file: %s", clusterName);
					sprintf(last_load_error, "Failed to remove cluster.dep file.");
					filelog_printf("world_binning.log", "Error in map \"%s\", reason: %s", zmapGetFilename(zmap), last_load_error);
				}
			}
		}

		for (i = 0; i < eaSize(&file_list->output_files); ++i)
		{
			if (fileIsAbsolutePath(file_list->output_files[i]->filename)) // Checking for old full-paths to heatmap templates
			{
				sprintf(last_load_error, "Bin file \"%s\" is an absolute path!", file_list->output_files[i]->filename);
				filelog_printf("world_binning.log", "Rebinning map \"%s\", reason: %s", zmapGetFilename(zmap), last_load_error);
				failed = true;
			}
			else
			{
				U32 timestamp = bflFileLastChanged(file_list->output_files[i]->filename);
            	U32 header_ts = file_list->output_files[i]->timestamp & ~IGNORE_EXIST_TIMESTAMP_BIT;

				if (timestamp != header_ts && ABS_UNS_DIFF(timestamp, header_ts) != 3600 && 
					(!(file_list->output_files[i]->timestamp & IGNORE_EXIST_TIMESTAMP_BIT) || timestamp != 0))
				{
					sprintf(last_load_error, "Bin file \"%s\" failed timestamp check (on disk: %d   in header: %d).", file_list->output_files[i]->filename, timestamp, header_ts);
					filelog_printf("world_binning.log", "Rebinning map \"%s\", reason: %s", zmapGetFilename(zmap), last_load_error);
					failed = true;
				}
			}
		}

#if !_PS3
		for (i = 0; i < eaSize(&file_list->source_files); ++i)
		{
			U32 timestamp = bflFileLastChanged(file_list->source_files[i]->filename);
            U32 header_ts = file_list->source_files[i]->timestamp;

			if (timestamp != header_ts && ABS_UNS_DIFF(timestamp, header_ts) != 3600)
			{
				sprintf(last_load_error, "Source file \"%s\" failed timestamp check (on disk: %d   in header: %d).", file_list->source_files[i]->filename, timestamp, header_ts);
				filelog_printf("world_binning.log", "Rebinning map \"%s\", reason: %s", zmapGetFilename(zmap), last_load_error);
				failed = true;
			}
		}
#endif

		if (failed)
		{
			if (delete_bins)
				deleteOldBins(file_list, header_filename);
			StructDestroySafe(parse_BinFileList, &file_list);
			return NULL;
		}

		for (i = 0; i < eaSize(&zmap->layers); ++i)
		{
			bool found = false;

			for (j = 0; j < eaSize(&file_list->layer_names); ++j)
			{
				if (stricmp(zmap->layers[i]->filename, file_list->layer_names[j]) == 0)
					found = true;
			}

			if (!found)
			{
				sprintf(last_load_error, "Map contains a layer \"%s\" that is not in the bins.", zmap->layers[i]->filename);
				filelog_printf("world_binning.log", "Rebinning map \"%s\", reason: %s", zmapGetFilename(zmap), last_load_error);
				if (delete_bins)
					deleteOldBins(file_list, header_filename);
				StructDestroySafe(parse_BinFileList, &file_list);
				return NULL;
			}
		}

		for (i = 0; i < eaSize(&file_list->layer_names); ++i)
		{
			bool found = false, region_ok = false;

			for (j = 0; j < eaSize(&zmap->layers); ++j)
			{
				if (stricmp(file_list->layer_names[i], zmap->layers[j]->filename) == 0)
				{
					found = true;

					// check that the layer is in the same region as when it was binned
					if (file_list->layer_region_names[i] == zmap->layers[j]->region_name)
						region_ok = true;
				}
			}

			if (!found)
				sprintf(last_load_error, "Layer \"%s\" is in bins but not in the source map.", file_list->layer_names[i]);
			else if (!region_ok)
				sprintf(last_load_error, "Layer \"%s\" is in a different region than in the bins.", file_list->layer_names[i]);

			if (!found || !region_ok)
			{
				filelog_printf("world_binning.log", "Rebinning map \"%s\", reason: %s", zmapGetFilename(zmap), last_load_error);
				if (delete_bins)
					deleteOldBins(file_list, header_filename);
				StructDestroySafe(parse_BinFileList, &file_list);
				return NULL;
			}
		}
	}

	return file_list;
}

SA_RET_OP_VALID static BinFileList *terrainRegionNeedsAtlasing(WorldRegion *region, const char *client_base_dir, bool delete_bins)
{
	BinFileList *file_list = StructAlloc(parse_BinFileList);
	ZoneMap *zmap = region->zmap_parent;
	char header_filename[MAX_PATH], atlas_filename[MAX_PATH];
	HeightMapAtlasRegionData atlases = {0};
	int i, j;

	sprintf(header_filename, "%s/terrain_%s_deps.bin", client_base_dir, region->name ? region->name : "Default");
	binNotifyTouchedOutputFile(header_filename);

	if (!ParserOpenReadBinaryFile(NULL, header_filename, parse_BinFileList, file_list, NULL, NULL, NULL, NULL, 0, 0, 0))
	{
		sprintf(last_load_error, "Failed to parse terrain atlas header file \"%s\".", header_filename);
		StructDestroySafe(parse_BinFileList, &file_list);
		return NULL;
	}

	if (force_create_terrain_atlases)
	{
		sprintf(last_load_error, "Command line forced re-atlasing.");
		if (wlIsServer() && isDevelopmentMode() && delete_bins)
			deleteOldBins(file_list, header_filename);
		StructDestroySafe(parse_BinFileList, &file_list);
		return NULL;
	}

	if (isDevelopmentMode())
	{
		for (i = 0; i < eaSize(&file_list->output_files); ++i)
		{
			U32 timestamp = bflFileLastChanged(file_list->output_files[i]->filename);
            U32 header_ts = file_list->output_files[i]->timestamp & ~IGNORE_EXIST_TIMESTAMP_BIT;

			if (timestamp != header_ts && ABS_UNS_DIFF(timestamp, header_ts) != 3600 && 
				(!(file_list->output_files[i]->timestamp & IGNORE_EXIST_TIMESTAMP_BIT) || timestamp != 0))
			{
				sprintf(last_load_error, "Bin file \"%s\" failed timestamp check (on disk: %d   in header: %d).", file_list->output_files[i]->filename, timestamp, header_ts);
				if (wlIsServer() && delete_bins)
					deleteOldBins(file_list, header_filename);
				StructDestroySafe(parse_BinFileList, &file_list);
				return NULL;
			}
		}
#if !_PS3
		for (i = 0; i < eaSize(&file_list->source_files); ++i)
		{
			U32 timestamp = bflFileLastChanged(file_list->source_files[i]->filename);
            U32 header_ts = file_list->source_files[i]->timestamp;

			if (timestamp != header_ts && ABS_UNS_DIFF(timestamp, header_ts) != 3600)
			{
				sprintf(last_load_error, "Source file \"%s\" failed timestamp check (on disk: %d   in header: %d).", file_list->source_files[i]->filename, timestamp, header_ts);
				if (wlIsServer() && delete_bins)
					deleteOldBins(file_list, header_filename);
				StructDestroySafe(parse_BinFileList, &file_list);
				return NULL;
			}
		}
#endif
		for (i = 0; i < eaSize(&zmap->layers); ++i)
		{
			bool found = false;

			if (!eaSize(&zmap->layers[i]->terrain.blocks) ||
				layerGetWorldRegion(zmap->layers[i]) != region)
			{
				continue;
			}

			for (j = 0; j < eaSize(&file_list->layer_names); ++j)
			{
				if (stricmp(zmap->layers[i]->filename, file_list->layer_names[j]) == 0)
					found = true;
			}

			if (!found)
			{
				sprintf(last_load_error, "Map contains a layer \"%s\" that is not in the bins.", zmap->layers[i]->filename);
				if (wlIsServer() && delete_bins)
					deleteOldBins(file_list, header_filename);
				StructDestroySafe(parse_BinFileList, &file_list);
				return NULL;
			}
		}

		for (i = 0; i < eaSize(&file_list->layer_names); ++i)
		{
			bool found = false, region_ok = false;

			for (j = 0; j < eaSize(&zmap->layers); ++j)
			{
				if (stricmp(file_list->layer_names[i], zmap->layers[j]->filename) == 0)
				{
					found = true;

					// check that the layer is in the same region as when it was binned
					if (file_list->layer_region_names[i] == zmap->layers[j]->region_name)
						region_ok = true;
				}
			}

			if (!found)
				sprintf(last_load_error, "Layer \"%s\" is in bins but not in the source map.", file_list->layer_names[i]);
			else if (!region_ok)
				sprintf(last_load_error, "Layer \"%s\" is in a different region than in the bins.", file_list->layer_names[i]);

			if (!found || !region_ok)
			{
				if (wlIsServer() && delete_bins)
					deleteOldBins(file_list, header_filename);
				StructDestroySafe(parse_BinFileList, &file_list);
				return NULL;
			}
		}
	}

	sprintf(atlas_filename, "%s/terrain_%s_atlases.bin", client_base_dir, region->name ? region->name : "Default");
	if (!ParserOpenReadBinaryFile(NULL, atlas_filename, parse_HeightMapAtlasRegionData, &atlases, NULL, NULL, NULL, NULL, 0, 0, 0) || 
		atlases.bin_version_number != TERRAIN_ATLAS_VERSION)
	{
		sprintf(last_load_error, "Failed to parse terrain atlas bin file \"%s\", or the version number is incorrect.", atlas_filename);
		StructDeInit(parse_HeightMapAtlasRegionData, &atlases);
		if (wlIsServer() && isDevelopmentMode() && delete_bins)
			deleteOldBins(file_list, header_filename);
		StructDestroySafe(parse_BinFileList, &file_list);
		return NULL;
	}

	StructDeInit(parse_HeightMapAtlasRegionData, &atlases);

	return file_list;
}

void freeStreamingPooledInfoSafe(ZoneMap *zmap, WorldStreamingPooledInfo **streaming_pooled_info_ptr)
{
	WorldStreamingPooledInfo *streaming_pooled_info = *streaming_pooled_info_ptr;
	int i;

	if (!streaming_pooled_info)
		return;

	if (streaming_pooled_info->strings)
	{
		stashTableDestroy(streaming_pooled_info->strings->string_hash);
		streaming_pooled_info->strings->string_hash = NULL;
		eaiDestroy(&streaming_pooled_info->strings->available_idxs);
	}

	if (zmap)
	{
		for (i = 0; i < eaSize(&streaming_pooled_info->material_draws); ++i)
			removeMaterialDrawRefDbg(&zmap->world_cell_data.drawable_pool, streaming_pooled_info->material_draws[i] MEM_DBG_PARMS_INIT);
		eaDestroy(&streaming_pooled_info->material_draws);

		for (i = 0; i < eaSize(&streaming_pooled_info->model_draws); ++i)
			removeModelDrawRef(&zmap->world_cell_data.drawable_pool, streaming_pooled_info->model_draws[i] MEM_DBG_PARMS_INIT);
		eaDestroy(&streaming_pooled_info->model_draws);

		for (i = 0; i < eaSize(&streaming_pooled_info->subobjects); ++i)
			removeDrawableSubobjectRefDbg(&zmap->world_cell_data.drawable_pool, streaming_pooled_info->subobjects[i] MEM_DBG_PARMS_INIT);
		eaDestroy(&streaming_pooled_info->subobjects);

		eaDestroyEx(&streaming_pooled_info->drawable_lists, removeDrawableListRefCB);
		eaDestroyEx(&streaming_pooled_info->instance_param_lists, removeInstanceParamListRefCB);
		eaDestroyEx(&streaming_pooled_info->packed_info->shared_bounds, removeSharedBoundsRef);

		for (i = 0; i < eaSize(&streaming_pooled_info->interaction_costumes); ++i)
			removeInteractionCostumeRef(streaming_pooled_info->interaction_costumes[i], 0);
		eaDestroy(&streaming_pooled_info->interaction_costumes);
	}

	StructDestroySafe(parse_WorldStreamingPackedInfo, &streaming_pooled_info->packed_info);

	PackedStructStreamDeinit(streaming_pooled_info->packed_data);
	SAFE_FREE(streaming_pooled_info->packed_data);

	StructDestroySafe(parse_WorldStreamingPooledInfo, streaming_pooled_info_ptr);
}

static WorldStreamingPooledInfo *loadStreamingPooledInfo(const char *filename)
{
	WorldStreamingPooledInfo *streaming_pooled_info = StructAlloc(parse_WorldStreamingPooledInfo);

	if (!ParserOpenReadBinaryFile(NULL, filename, parse_WorldStreamingPooledInfo, streaming_pooled_info, NULL, NULL, NULL, NULL, 0, 0, BINARYREADFLAG_NONMATCHING_SIGNATURE_NON_FATAL) || 
		!streaming_pooled_info->packed_info_offset || 
		streaming_pooled_info->packed_info_crc != ParseTableCRC(parse_WorldStreamingPackedInfo, NULL, 0))
	{
		StructDestroy(parse_WorldStreamingPooledInfo, streaming_pooled_info);
		return NULL;
	}

	assert(!streaming_pooled_info->packed_data);
	assert(streaming_pooled_info->packed_data_serialize);
	streaming_pooled_info->packed_data = calloc(sizeof(*streaming_pooled_info->packed_data), 1);
	PackedStructStreamDeserialize(streaming_pooled_info->packed_data, streaming_pooled_info->packed_data_serialize);
	StructDestroySafe(parse_SerializablePackedStructStream, &streaming_pooled_info->packed_data_serialize);

	streaming_pooled_info->packed_info = StructUnpack(parse_WorldStreamingPackedInfo, streaming_pooled_info->packed_data, streaming_pooled_info->packed_info_offset - 1);

	return streaming_pooled_info;
}

static void freeWorldInfo(ZoneMap *zmap)
{
	WorldStreamingInfo *streaming_info = zmap->world_cell_data.streaming_info;
	WorldStreamingPooledInfo *streaming_pooled_info = zmap->world_cell_data.streaming_pooled_info;

	if (streaming_info || streaming_pooled_info)
		geoForceBackgroundLoaderToFinish();

	if (streaming_info)
	{
		int i;
		for (i = 0; i < eaSize(&streaming_info->regions_parsed); ++i)
		{
			WorldRegionCommonParsed *region = streaming_info->regions_parsed[i];
			if (region->hog_file)
			{
				// clear the hog pointers in both the runtime and parsed region
				WorldRegion *region_runtime = zmapGetWorldRegionByName(zmap, region->region_name);
				region_runtime->world_cell_hogg = NULL;

				hogFileDestroy(region->cluster_hog_file, true);
				region->cluster_hog_file = NULL;

				hogFileDestroy(region->hog_file, true);
				region->hog_file = NULL;
			}
		}

		if (wlIsServer())
			eaDestroyStructVoid(&streaming_info->regions_parsed, parse_WorldRegionServerParsed);
		else
			eaDestroyStructVoid(&streaming_info->regions_parsed, parse_WorldRegionClientParsed);

		stashTableDestroy(streaming_info->id_to_named_volume);
		stashTableDestroy(streaming_info->id_to_interactable);

		PackedStructStreamDeinit(streaming_info->packed_data);
		SAFE_FREE(streaming_info->packed_data);
		StructDestroySafe(parse_WorldStreamingInfo, &zmap->world_cell_data.streaming_info);
	}

	if (streaming_pooled_info)
		freeStreamingPooledInfoSafe(zmap, &zmap->world_cell_data.streaming_pooled_info);
}

void worldCellEntryReset(ZoneMap *zmap)
{
	freeWorldInfo(zmap);
	worldCellFXReset(zmap);
	worldAnimationEntryResetIDCounter(zmap);
	worldCellInteractionReset(zmap);
	worldDrawableListPoolReset(&zmap->world_cell_data.drawable_pool);
	worldCellEntryResetSharedBounds(zmap);
}

static bool loadWorldInfo(ZoneMap *zmap, const char *bin_filename, const char *pooled_bin_filename)
{
	int succeeded;

	assert(!zmap->world_cell_data.streaming_info);
	zmap->world_cell_data.streaming_info = StructAlloc(parse_WorldStreamingInfo);
	
	succeeded = ParserOpenReadBinaryFile(NULL, bin_filename, parse_WorldStreamingInfo, zmap->world_cell_data.streaming_info, NULL, NULL, NULL, NULL, 0, 0, 0);
	if (succeeded && zmap->world_cell_data.streaming_info->parse_table_crcs != getWorldCellParseTableCRC(false))
	{
		verbose_printf("Parse table CRCs have changed.");
		succeeded = false;
	}
	if (!succeeded)
	{
		verbose_printf("Failed to load %s\n", bin_filename);
		freeWorldInfo(zmap);
		return false;
	}

	assert(!zmap->world_cell_data.streaming_info->packed_data);
	assert(zmap->world_cell_data.streaming_info->packed_data_serialize);
	zmap->world_cell_data.streaming_info->packed_data = calloc(sizeof(*zmap->world_cell_data.streaming_info->packed_data), 1);
	PackedStructStreamDeserialize(zmap->world_cell_data.streaming_info->packed_data, zmap->world_cell_data.streaming_info->packed_data_serialize);
	StructDestroySafe(parse_SerializablePackedStructStream, &zmap->world_cell_data.streaming_info->packed_data_serialize);

	zmap->world_cell_data.streaming_pooled_info = loadStreamingPooledInfo(pooled_bin_filename);
	if (!zmap->world_cell_data.streaming_pooled_info)
	{
		verbose_printf("Failed to load %s\n", pooled_bin_filename);
		freeWorldInfo(zmap);
		return false;
	}

	if (false)
	{
		WorldStreamingInfo *old_streaming_info;
		char old_bin_filename[MAX_PATH];
		char *z = strstri(bin_filename, ".zone/");
		strcpy(old_bin_filename, bin_filename);
		old_bin_filename[z - bin_filename] = 0;
		strcat(old_bin_filename, ".zone.bak/");
		strcat(old_bin_filename, z + 6);
		old_streaming_info = StructAlloc(parse_WorldStreamingInfo);
		if (ParserOpenReadBinaryFile(NULL, old_bin_filename, parse_WorldStreamingInfo, old_streaming_info, NULL, NULL, NULL, NULL, 0, 0, 0))
		{
			int c = StructCompare(parse_WorldStreamingInfo, zmap->world_cell_data.streaming_info, old_streaming_info, 0, 0, 0);
		}
		StructDestroySafe(parse_WorldStreamingInfo, &old_streaming_info);
	}

	return true;
}

char *worldGetStreamedString(WorldStreamingPooledInfo *streaming_pooled_info, int idx)
{
	WorldInfoStringTable *string_table = SAFE_MEMBER(streaming_pooled_info, strings);

	if (!string_table)
		return NULL;

	if (idx < 0 || idx >= eaSize(&string_table->strings))
		return NULL;

	return string_table->strings[idx];
}

Mat4ConstPtr worldGetStreamedMatrix(WorldStreamingPooledInfo *streaming_pooled_info, int idx)
{
	if (!streaming_pooled_info || idx < 0 || idx >= eaSize(&streaming_pooled_info->packed_info->pooled_matrices))
		return unitmat;
	return streaming_pooled_info->packed_info->pooled_matrices[idx]->matrix;
}

static LayerBounds *getLayerBounds(WorldStreamingInfo *streaming_info, const char *layer_name)
{
	int i;

	assert(eaSize(&streaming_info->layer_names) == eaSize(&streaming_info->layer_bounds));

	for (i = 0; i < eaSize(&streaming_info->layer_names); ++i)
	{
		if (stricmp(layer_name, streaming_info->layer_names[i])==0)
			return streaming_info->layer_bounds[i];
	}

	return NULL;
}

static FileScanAction worldCellCheckSourceDirectory(char* dir, struct _finddata32_t* data, BinFileListWithCRCs **file_list)
{
	int i;
	char rel_filename[MAX_PATH];
	if (!strEndsWith(data->name, ".encounterlayer") &&
		!strEndsWith(data->name, ".gaelayer"))
	{
		// Continue searching files
		return FSA_NO_EXPLORE_DIRECTORY;
	}

	// Is it already in our dependencies list?
	for (i = 0; i < eaSize(&(*file_list)->file_list->source_files); i++)
	{
		getFileNameNoDir(rel_filename, (*file_list)->file_list->source_files[i]->filename);
		if (!stricmp(rel_filename, data->name))
		{
			return FSA_NO_EXPLORE_DIRECTORY;
		}
	}

	// If not, we need to rebin
	StructDestroySafe(parse_BinFileListWithCRCs, file_list);
	return FSA_STOP;
}

static U32 g_EncounterLayerCRC = 0;
void worldSetEncounterLayerCRC(U32 crc)
{
	g_EncounterLayerCRC = crc;
}

static U32 g_GAELayerCRC = 0;
void worldSetGAELayerCRC(U32 crc)
{
	g_GAELayerCRC = crc;
}

void worldGetClientBaseDir(const char *zmapFilename, char *base_dir, size_t base_dir_size)
{
	char nameSpace[RESOURCE_NAME_MAX_SIZE];
	char baseObjectName[RESOURCE_NAME_MAX_SIZE];
	if (resExtractNameSpace(zmapFilename, nameSpace, baseObjectName))
	{
		sprintf_s(SAFESTR2(base_dir), NAMESPACE_PATH"%s/bin/geobin/%s", nameSpace, baseObjectName);
	}
	else
	{
		sprintf_s(SAFESTR2(base_dir), "bin/geobin/%s", zmapFilename);
	}
}

void worldGetTempBaseDir(const char *zmapFilename, char *base_dir, size_t base_dir_size)
{
	char nameSpace[RESOURCE_NAME_MAX_SIZE];
	char baseObjectName[RESOURCE_NAME_MAX_SIZE];
	if (resExtractNameSpace(zmapFilename, nameSpace, baseObjectName))
	{
		sprintf_s(SAFESTR2(base_dir), NAMESPACE_PATH"%s/tempbin/%s", nameSpace, baseObjectName);
	}
	else
	{
		sprintf_s(SAFESTR2(base_dir), "tempbin/%s", zmapFilename);
	}
}

void worldGetServerBaseDir(const char *zmapFilename, char *base_dir, size_t base_dir_size)
{
	char nameSpace[RESOURCE_NAME_MAX_SIZE];
	char baseObjectName[RESOURCE_NAME_MAX_SIZE];
	if (resExtractNameSpace(zmapFilename, nameSpace, baseObjectName))
	{
		sprintf_s(SAFESTR2(base_dir), NAMESPACE_PATH"%s/server/bin/geobin/%s", nameSpace, baseObjectName);
	}
	else
	{
		sprintf_s(SAFESTR2(base_dir), "server/bin/geobin/%s", zmapFilename);
	}
}

bool worldCellCheckNeedsBins(ZoneMapInfo *zminfo)
{
	int i, j;
	char base_dir[MAX_PATH];
	char temp_dir[MAX_PATH];
	char client_base_dir[MAX_PATH];
	const char *zmapFilename = zmapInfoGetFilename(zminfo);
	char bin_filename[MAX_PATH];
	char pooled_bin_filename[MAX_PATH];
	char header_filename[MAX_PATH];
	char deps_filename[MAX_PATH];
	ZoneMap *zmap;
	WorldRegion **terrain_regions = NULL;
	BinFileListWithCRCs *deps_file_list;
	BinFileList *file_list;
	BinFileList **terrain_file_lists = NULL;

	worldGetClientBaseDir(zmapFilename, SAFESTR(client_base_dir));
	if (wlIsServer())
		worldGetServerBaseDir(zmapFilename, SAFESTR(base_dir));
	else
		worldGetClientBaseDir(zmapFilename, SAFESTR(base_dir));
	worldGetTempBaseDir(zmapFilename, SAFESTR(temp_dir));

	// Check external dependencies
	sprintf(deps_filename, "%s.external_deps.bin", base_dir);
	deps_file_list = StructCreate(parse_BinFileListWithCRCs);
	if (!ParserOpenReadBinaryFile(NULL, deps_filename, parse_BinFileListWithCRCs, deps_file_list, NULL, NULL, NULL, NULL, 0, 0, 0))
	{
		filelog_printf("world_binning.log", "Rebinning map \"%s\", reason: Failed to load bin header file \"%s\".", zmapFilename, deps_filename);
		StructDestroySafe(parse_BinFileListWithCRCs, &deps_file_list);
		return true;
	}

	// Check CRCs
	if (deps_file_list->world_crc != getWorldCellParseTableCRC(false))
	{
		filelog_printf("world_binning.log", "Rebinning map \"%s\", reason: World cell CRC mismatch.", zmapFilename);
		StructDestroySafe(parse_BinFileListWithCRCs, &deps_file_list);
		return true;
	}
	if (deps_file_list->encounterlayer_crc && deps_file_list->encounterlayer_crc != g_EncounterLayerCRC)
	{
		filelog_printf("world_binning.log", "Rebinning map \"%s\", reason: Encounter Layer CRC mismatch.", zmapFilename);
		StructDestroySafe(parse_BinFileListWithCRCs, &deps_file_list);
		return true;
	}
	if (deps_file_list->gaelayer_crc && deps_file_list->gaelayer_crc != g_GAELayerCRC)
	{
		filelog_printf("world_binning.log", "Rebinning map \"%s\", reason: GAE Layer CRC mismatch.", zmapFilename);
		StructDestroySafe(parse_BinFileListWithCRCs, &deps_file_list);
		return true;
	}

	{
		char dir_name[MAX_PATH];
		strcpy(dir_name, zmapFilename);
		getDirectoryName(dir_name);
		fileScanAllDataDirs(dir_name, worldCellCheckSourceDirectory, &deps_file_list);
		if (!deps_file_list)
		{
			filelog_printf("world_binning.log", "Rebinning map \"%s\", reason: New encounterlayer or gaelayer file found.", zmapFilename);
			return true;
		}
	}

	if (isDevelopmentMode() && deps_file_list->file_list)
	{
#if !_PS3
		for (i = 0; i < eaSize(&deps_file_list->file_list->source_files); ++i)
		{
			U32 timestamp = bflFileLastChanged(deps_file_list->file_list->source_files[i]->filename);
			U32 header_ts = deps_file_list->file_list->source_files[i]->timestamp;

			if (timestamp != header_ts && ABS_UNS_DIFF(timestamp, header_ts) != 3600)
			{
				filelog_printf("world_binning.log", "Rebinning map \"%s\", reason: Source file \"%s\" failed timestamp check (on disk: %d   in header: %d).", zmapFilename, deps_file_list->file_list->source_files[i]->filename, timestamp, header_ts);
				StructDestroySafe(parse_BinFileListWithCRCs, &deps_file_list);
				return true;
			}
		}
#endif
	}

	StructDestroySafe(parse_BinFileListWithCRCs, &deps_file_list);

	sprintf(bin_filename, "%s/world_cells.info", base_dir);
	sprintf(pooled_bin_filename, "%s/world_cells_shared.info", base_dir);
	if (wlIsServer())
		sprintf(header_filename, "%s/server_world_cells_deps.bin", temp_dir);
	else
		sprintf(header_filename, "%s/client_world_cells_deps.bin", temp_dir);

	zmap = zmapLoad(zminfo);

	// Check terrain layers
	for (i = 0; i < eaSize(&zmap->layers); i++)
	{
		if (layerNeedsTerrainBins(zmap->layers[i]))
		{
			for (j = 0; j < eaSize(&zmap->layers[i]->terrain.blocks); j++)
				deinitTerrainBlock(zmap->layers[i]->terrain.blocks[j]);
			zmapUnload(zmap);
			eaDestroy(&terrain_regions);
			return true;
		}
		if(eaSize(&zmap->layers[i]->terrain.blocks))
		{
			eaPushUnique(&terrain_regions, layerGetWorldRegion(zmap->layers[i]));
		}
		for (j = 0; j < eaSize(&zmap->layers[i]->terrain.blocks); j++)
			deinitTerrainBlock(zmap->layers[i]->terrain.blocks[j]);
	}

	for (i = 0; i < eaSize(&terrain_regions); ++i)
	{
		// check if region needs re-atlasing
		file_list = terrainRegionNeedsAtlasing(terrain_regions[i], client_base_dir, false);
		if (!file_list)
		{
			eaDestroy(&terrain_regions);
			eaDestroyStruct(&terrain_file_lists, parse_BinFileList);
			zmapUnload(zmap);
			return true;
		}
		eaPush(&terrain_file_lists, file_list);
	}
	eaDestroy(&terrain_regions);

	file_list = needsRebinning(header_filename, zmap, false, false);
	if (!file_list)
	{
		eaDestroyStruct(&terrain_file_lists, parse_BinFileList);
		zmapUnload(zmap);
		return true;
	}

	if (!loadWorldInfo(zmap, bin_filename, pooled_bin_filename) || 
		zmap->world_cell_data.streaming_info->cell_size != CELL_BLOCK_SIZE ||
		zmap->world_cell_data.streaming_info->has_errors ||
		zmap->world_cell_data.streaming_info->bin_version_number != WORLD_STREAMING_BIN_VERSION + genesisGetBinVersion(zmap))
	{
		freeWorldInfo(zmap);
		eaDestroyStruct(&terrain_file_lists, parse_BinFileList);
		StructDestroySafe(parse_BinFileList, &file_list);
		zmapUnload(zmap);
		return true;
	}

	// Don't need bins re-made
	freeWorldInfo(zmap);
	
	ugcZoneMapLayerTouchUGCData(zmap);

	// Touch all the output files so we don't delete them
	for (i = 0; i < eaSize(&terrain_file_lists); ++i)
	{
		for (j = 0; j < eaSize(&terrain_file_lists[i]->output_files); ++j)
			binNotifyTouchedOutputFile(terrain_file_lists[i]->output_files[j]->filename);
	}
	for (i = 0; i < eaSize(&file_list->source_files); ++i)
	{
		if (strStartsWith(file_list->source_files[i]->filename, "tempbin/"))
			binNotifyTouchedOutputFile(file_list->source_files[i]->filename);
	}
	for (i = 0; i < eaSize(&file_list->output_files); ++i)
		binNotifyTouchedOutputFile(file_list->output_files[i]->filename);

	eaDestroyStruct(&terrain_file_lists, parse_BinFileList);
	StructDestroySafe(parse_BinFileList, &file_list);
	zmapUnload(zmap);
	return false;
}

//returns a CRC which, if it changes, means that rebinning generally needs to happen.

U32 worldCellGetOverrideRebinCRC(void)
{
	static U32 iRetVal = 0;
	U32 iTemp;

	if (!iRetVal)
	{
		cryptAdler32Init();
		iTemp = getWorldCellParseTableCRC(true);
		cryptAdler32Update((void*)&iTemp, 4);
		iTemp = WORLD_STREAMING_BIN_VERSION;
		cryptAdler32Update((void*)&iTemp, 4);
		iTemp = WORLD_EXTERNAL_BIN_VERSION;
		cryptAdler32Update((void*)&iTemp, 4);
		iTemp = g_EncounterLayerCRC;
		cryptAdler32Update((void*)&iTemp, 4);
		iTemp = g_GAELayerCRC;
		cryptAdler32Update((void*)&iTemp, 4);
		iRetVal = cryptAdler32Final();
	}

	return iRetVal;
}
		

static bool worldZeniUpToDate(const char* filename)
{
	ZoneMapEncounterInfo info = { 0 };
	if(!ParserLoadSingleDictionaryStruct(filename, "ZoneMapEncounterInfo", &info, 0)) {
		StructDeInit( parse_ZoneMapEncounterInfo, &info );
		return false;
	}

	{
		bool result = (info.version == ZENI_CURRENT_VERSION) && FileListAllFilesUpToDate( &info.deps );
		StructDeInit( parse_ZoneMapEncounterInfo, &info );
		return result;
	} 
}

static bool worldZeniSnapUpToDate(const char* filename)
{
	ZoneMapExternalMapSnap info = { 0 };
	if(!ParserLoadSingleDictionaryStruct(filename, "ZoneMapExternalMapSnap", &info, 0)) {
		StructDeInit( parse_ZoneMapExternalMapSnap, &info );
		return false;
	}

	{
		bool result = (info.version == ZENISNAP_CURRENT_VERSION) && FileListAllFilesUpToDate( &info.deps );
		StructDeInit( parse_ZoneMapExternalMapSnap, &info );
		return result;
	} 
}

#ifdef MATERIAL_UNPACK_LOGGING
#include "structDefines.h"
static void materialUnpackLog(Material* mat)
{
	OutputDebugStringf("Material %s: quality %s, override index %d\n",
		mat->material_name, StaticDefineIntRevLookup( ShaderGraphQualityEnum, mat->shader_quality ),
		mat->override_fallback_index);
}
#endif

void worldCellLoadBins(ZoneMap *zmap, bool force_create_bins, bool load_from_source, bool is_secondary)
{
	char base_dir[MAX_PATH];
	char client_base_dir[MAX_PATH];
	char temp_dir[MAX_PATH];
	char bin_filename[MAX_PATH];
	char pooled_bin_filename[MAX_PATH];
	char header_filename[MAX_PATH];
	char bounds_filename[MAX_PATH];
	char encounter_info_filename[MAX_PATH];
	char external_map_snap_filename[MAX_PATH];
	int i, j, tries = 0;
	BinFileList *file_list;
	bool succeeded = false, terrain_need_bins = false;
	bool *force_terrain_atlasing;
	bool genesis_data_generated = false;
	int layers_to_bin = 0, current_layer = 0;
	WorldStreamingPooledInfo *old_pooled_data = NULL;
	WorldStreamingPooledInfo *streaming_pooled_info;
	bool force_cache_update = false;

	if (!zmap)
		return;

	world_created_bins = false;

	genesisDestroyStateData();

	worldSetActiveMap(zmap);

	worldCellEntryReset(zmap);

	if (load_from_source)
	{
		triviaPrintf("ZoneMapState", "Loaded from source");

		for (i = 0; i < eaSize(&zmap->layers); ++i)
		{
			ZoneMapLayer *layer = zmap->layers[i];
			layerLoadGroupSource(layer, layer->zmap_parent, NULL, false);
			layer->target_mode = layer->layer_mode = LAYER_MODE_EDITABLE;
			setVec3same(layer->bounds.local_min, 8e16);
			setVec3same(layer->bounds.local_max, -8e16);
			setVec3same(layer->bounds.visible_geo_min, 8e16);
			setVec3same(layer->bounds.visible_geo_max, -8e16);
			layerUpdateBounds(layer);
		}

		loadstart_printf("Creating cell entries...");
		for (i = 0; i < eaSize(&zmap->layers); ++i)
		{
			ZoneMapLayer *layer = zmap->layers[i];
			layerTrackerOpen(layer);
		}
		loadend_printf(" done.");

		worldUpdateBounds(false, false);
		return;
	}

	{
		const char *zmapFilename = zmapGetFilename(zmap);
		worldGetClientBaseDir(zmapFilename, SAFESTR(client_base_dir));
		if (wlIsServer())
			worldGetServerBaseDir(zmapFilename, SAFESTR(base_dir));
		else
			worldGetClientBaseDir(zmapFilename, SAFESTR(base_dir));
		worldGetTempBaseDir(zmapFilename, SAFESTR(temp_dir));
	}

	sprintf(bin_filename, "%s/world_cells.info", base_dir);
	sprintf(pooled_bin_filename, "%s/world_cells_shared.info", base_dir);
	if (wlIsServer())
		sprintf(header_filename, "%s/server_world_cells_deps.bin", temp_dir);
	else
		sprintf(header_filename, "%s/client_world_cells_deps.bin", temp_dir);
	sprintf(bounds_filename, "%s/world_cells.region_bounds", temp_dir);
	sprintf(encounter_info_filename, "%s.zeni", temp_dir);
	sprintf(external_map_snap_filename, "%s.zeni_snap", temp_dir);

	FolderCacheRequestTree(folder_cache, base_dir);

	if (isDevelopmentMode())
	{
		BinFileList **terrain_file_lists = NULL;
		WorldRegion **terrain_regions = NULL;

		// load terrain layers
		loadstart_printf("Loading terrain layers...");
		triviaPrintf("ZoneMapState", "Loading terrain");
		force_terrain_atlasing = _alloca(sizeof(bool)*eaSize(&zmap->layers));
		memset(force_terrain_atlasing, 0, sizeof(bool)*eaSize(&zmap->layers));
		for (i = 0; i < eaSize(&zmap->layers); ++i)
		{
			ZoneMapLayer *layer = zmap->layers[i];
			layerLoadStreaming(layer, zmap, NULL);
			if (force_create_bins)
			{
				for (j = 0; j < eaSize(&layer->terrain.blocks); j++)
					layer->terrain.blocks[j]->need_bins = true;
				layer->terrain.need_bins = true;
			}
			if (layer->terrain.need_bins)
				layers_to_bin++;
		}
		loadend_printf(" done.");

		if (zmap->map_info.genesis_data &&
			genesisDataHasTerrain(zmap->map_info.genesis_data))
		{		
			bool need_bins = false;

			loadstart_printf("Generating genesis terrain...");
			for (i = 0; i < eaSize(&zmap->layers); ++i)
			{
				ZoneMapLayer *layer = zmap->layers[i];
				if (layer->terrain.need_bins)
				{
					int idx = eaPushUnique(&terrain_regions, layerGetWorldRegion(layer));
					force_terrain_atlasing[idx] = true;
					need_bins = true;
				}
				else if(eaSize(&layer->terrain.blocks))
				{
					eaPushUnique(&terrain_regions, layerGetWorldRegion(layer));
				}
			}
			if (need_bins)
			{
				#ifndef NO_EDITORS
				{
					objectLibraryLoad(); // Need library for binning

					wl_state.genesis_generate_func(worldGetAnyCollPartitionIdx(), zmap, false, false);
					genesis_data_generated = true;
				
					genesisGenerateTerrain(worldGetAnyCollPartitionIdx(), zmap, false);
					terrain_need_bins = true;
				}
				#else
				{
					Alertf( "Genesis thinks it needs bins with NO_EDITORS.  This should not happen.");
				}
				#endif
			}
			loadend_printf(" done.");
		}

		loadstart_printf("Saving terrain bins...");
		triviaPrintf("ZoneMapState", "Binning terrain");
		{
			for (i = 0; i < eaSize(&zmap->layers); ++i)
			{
				ZoneMapLayer *layer = zmap->layers[i];
				bool made_bins = false;
				if(layerIsGenesis(layer))
				{
					if (force_create_bins)
					{
						terrain_need_bins = true;
					}
					continue;
				}
				if (layer->terrain.need_bins)
				{
					if (isProductionMode())
						assertmsg(resHasNamespace(zmapInfoGetPublicName(zmapGetInfo(zmap))), "Trying to bin a non-namespace map in production mode!");
					objectLibraryLoad(); // Need library for binning
					terrain_need_bins = true;
					if (!(isDevelopmentMode()|| isProductionEditMode()))
						FatalErrorf("Failed to load data for layer \"%s\": terrain bins are not up to date.", layer->filename);
					assert(wlIsServer());
					terrainSaveBins(layer, current_layer, layers_to_bin);
					current_layer++;
					made_bins = true;
				}
				if (eaSize(&layer->terrain.blocks))
				{
					int idx = eaPushUnique(&terrain_regions, layerGetWorldRegion(layer));
					if (made_bins || force_create_terrain_atlases)
						force_terrain_atlasing[idx] = true;
				}
			}
		}
		loadend_printf(" done.");

		for (i = 0; i < eaSize(&terrain_regions); ++i)
		{
			// check if region needs re-atlasing
			loadstart_printf("Checking if terrain for region \"%s\" needs binning...", terrain_regions[i]->name ? terrain_regions[i]->name : "Default");
			file_list = terrainRegionNeedsAtlasing(terrain_regions[i], client_base_dir, true);
			if (force_terrain_atlasing[i])
				StructDestroySafe(parse_BinFileList, &file_list);
			if (!file_list)
			{
				loadend_printf(" done, needs bins.");
				verbose_printf("%s\n", last_load_error);
				objectLibraryLoad(); // Need library for binning
				filelog_printf("world_binning.log", "Rebinning terrain atlases for map \"%s\", reason: %s", zmapGetFilename(zmap), last_load_error);
				if (!(isDevelopmentMode()|| isProductionEditMode()) || (gbMakeBinsAndExit && wlIsClient()))
					FatalErrorf("Failed to load terrain data for map \"%s\": %s", zmapGetFilename(zmap), last_load_error);
				else if (wlIsClient())
					Errorf("Terrain bins need to be created by a server loading this map before it can be loaded on the client! (%s)", last_load_error);
				else
				{
					file_list = terrainSaveRegionAtlases(terrain_regions[i]);
					world_created_bins = true;
				}
			}
			else
			{
				loadend_printf(" done.");
			}
			eaPush(&terrain_file_lists, file_list);
		}

		for (i = 0; i < eaSize(&zmap->layers); ++i)
		{
			ZoneMapLayer *layer = zmap->layers[i];
			layerUnload(layer);
		}

		for (i = 0; i < eaSize(&terrain_regions); ++i)
		{
			if (terrain_file_lists[i])
			{
				terrainLoadRegionAtlases(terrain_regions[i], terrain_file_lists[i]);
				for (j = 0; j < eaSize(&terrain_file_lists[i]->output_files); ++j)
					binNotifyTouchedOutputFile(terrain_file_lists[i]->output_files[j]->filename);
			}
		}
		eaDestroy(&terrain_regions);
		eaDestroyStruct(&terrain_file_lists, parse_BinFileList);
	}

	old_pooled_data = loadStreamingPooledInfo(pooled_bin_filename);

	loadstart_printf("Checking if world needs binning...");
	file_list = needsRebinning(header_filename, zmap, terrain_need_bins, true);
	if (!file_list)
	{
		rebuilt_bins = 1;
		loadend_printf(" done, needs bins.");
		verbose_printf("%s\n", last_load_error);
		if (isProductionMode())
			assertmsg(resHasNamespace(zmapInfoGetPublicName(zmapGetInfo(zmap))), "Trying to bin a non-namespace map in production mode!");
	}
	else
	{
		rebuilt_bins = 0;
		loadend_printf(" done.");
	}

	if (!file_list)
	{
		if (!(isDevelopmentMode()|| isProductionEditMode()))
		{
			ErrorDetailsf("filename=%s:error=%s", zmapGetFilename(zmap), last_load_error);
			FatalErrorf("Failed to load data for map, see trivia for map name and error");
		}
		if (zmap->map_info.genesis_data && !genesis_data_generated)
		{
			wl_state.genesis_generate_func(worldGetAnyCollPartitionIdx(), zmap, false, false);
		}

		objectLibraryLoad(); // Need library for binning

		file_list = worldCellSaveBins(base_dir, bin_filename, pooled_bin_filename, old_pooled_data, header_filename, bounds_filename);
		if (file_list && isProductionEditMode())
		{
			force_cache_update = true;
		}
		world_created_bins = true;
		tries = 1;
	}

	// UGC: If we're loading a dummy ZoneMapInfo, force loading bins from disk
	if (isProductionEditMode() && zmap->map_info.from_ugc_file)
		force_cache_update = true;

	if (isProductionMode() || file_list)
	{
		if (force_cache_update)
		{
			char map_snap_hog_filename[MAX_PATH];
			sprintf(map_snap_hog_filename, "%s/map_snap.hogg", base_dir);
			FolderCacheForceUpdate(folder_cache, map_snap_hog_filename);

			FolderCacheForceUpdate(folder_cache, bin_filename);
			FolderCacheForceUpdate(folder_cache, pooled_bin_filename);
		}
		succeeded = loadWorldInfo(zmap, bin_filename, pooled_bin_filename);

		if (!succeeded || 
			zmap->world_cell_data.streaming_info->cell_size != CELL_BLOCK_SIZE ||
			(zmap->world_cell_data.streaming_info->has_errors && rebuild_bins_with_errors && !tries && (isDevelopmentMode()|| isProductionEditMode())) || // JE: rebuild_bins_with_errors will always FatalErrorf() in production mode
			zmap->world_cell_data.streaming_info->bin_version_number != WORLD_STREAMING_BIN_VERSION + genesisGetBinVersion(zmap))
		{
			if (succeeded && !tries)
			{
				if (zmap->world_cell_data.streaming_info->has_errors && rebuild_bins_with_errors && (isDevelopmentMode()|| isProductionEditMode()))
					filelog_printf("world_binning.log", "Rebinning map \"%s\", reason: %s", zmapGetFilename(zmap), "map had errors.");
				else if (zmap->world_cell_data.streaming_info->bin_version_number != WORLD_STREAMING_BIN_VERSION + genesisGetBinVersion(zmap))
					filelog_printf("world_binning.log", "Rebinning map \"%s\", reason: %s", zmapGetFilename(zmap), "bin version number changed.");
				else if (zmap->world_cell_data.streaming_info->cell_size != CELL_BLOCK_SIZE)
					filelog_printf("world_binning.log", "Rebinning map \"%s\", reason: %s", zmapGetFilename(zmap), "cell size define changed.");
				else
					filelog_printf("world_binning.log", "Rebinning map \"%s\", reason: %s", zmapGetFilename(zmap), "bin files failed to load.");
			}

			succeeded = 0;
			verbose_printf("Bin file \"%s\" failed to load or has an old version number.\n", bin_filename);

			deleteOldBins(file_list, header_filename);
			StructDestroySafe(parse_BinFileList, &file_list);
			freeWorldInfo(zmap);

			// Allow binning only in development more or when we're editing this specific map in production
			if ( ! (isDevelopmentMode() || (isProductionEditMode() && zmap->map_info.from_ugc_file) ) )
			{
				ErrorDetailsf("map \"%s\"", zmapGetFilename(zmap));
				FatalErrorf("Failed to load data for map: either the data does not exist on disk (failed to patch) or the crc or version number of the data on disk does not match what the code is expecting.");
			}

			if (!tries)
			{
				if (zmap->map_info.genesis_data)
				{
					wl_state.genesis_generate_func(worldGetAnyCollPartitionIdx(), zmap, false, false);
				}

				objectLibraryLoad(); // Need library for binning

				file_list = worldCellSaveBins(base_dir, bin_filename, pooled_bin_filename, old_pooled_data, header_filename, bounds_filename);
				if (file_list)
					succeeded = loadWorldInfo(zmap, bin_filename, pooled_bin_filename);
				tries++;
				world_created_bins = true;
			}
		}
		else
		{
			for (i = 0; i < eaSize(&file_list->source_files); ++i)
			{
				if (strStartsWith(file_list->source_files[i]->filename, "tempbin/"))
					binNotifyTouchedOutputFile(file_list->source_files[i]->filename);
			}

			ugcZoneMapLayerTouchUGCData(zmap);
		}
	}

	if (!succeeded)
	{
		if (!(isDevelopmentMode()|| isProductionEditMode()))
			FatalErrorf("Failed to load data for map \"%s\"", zmapGetFilename(zmap));

		if (stricmp(zmapGetName(zmap), "emptymap") != 0)
		{
			if (gbMakeBinsAndExit)
				FatalErrorf("Failed to read bin file \"%s\", loading from source.", bin_filename);
			else
				Errorf("Failed to read bin file \"%s\", loading from source.", bin_filename);
		}

		triviaPrintf("ZoneMapState", "Loaded from source");

		// load layers, recalc bounds, build unpopulated cell tree
		for (i = 0; i < eaSize(&zmap->layers); ++i)
		{
			ZoneMapLayer *layer = zmap->layers[i];
			layerLoadGroupSource(layer, zmap, NULL, false);
			layer->target_mode = layer->layer_mode = LAYER_MODE_EDITABLE;
			setVec3same(layer->bounds.local_min, 8e16);
			setVec3same(layer->bounds.local_max, -8e16);
			setVec3same(layer->bounds.visible_geo_min, 8e16);
			setVec3same(layer->bounds.visible_geo_max, -8e16);
			layerLoadTerrainObjects(layer);
		}

		zmapRecalcBounds(zmap, true);
		worldBuildCellTree(zmap, true);

		deleteOldBins(file_list, header_filename);
		freeStreamingPooledInfoSafe(NULL, &old_pooled_data);
		StructDestroySafe(parse_BinFileList, &file_list);
		freeWorldInfo(zmap);

		return;
	}

	triviaPrintf("ZoneMapState", "Streaming from bins");

	if (zmap->world_cell_data.streaming_info->id_to_named_volume)
		stashTableDestroy(zmap->world_cell_data.streaming_info->id_to_named_volume);
	zmap->world_cell_data.streaming_info->id_to_named_volume = stashTableCreateInt(64);

	if (zmap->world_cell_data.streaming_info->id_to_interactable)
		stashTableDestroy(zmap->world_cell_data.streaming_info->id_to_interactable);
	zmap->world_cell_data.streaming_info->id_to_interactable = stashTableCreateInt(64);

	worldFXSetIDCounter(zmap, zmap->world_cell_data.streaming_info->fx_entry_id_counter);
	worldAnimationEntrySetIDCounter(zmap, zmap->world_cell_data.streaming_info->animation_entry_id_counter);

	for (i = 0; i < eaiSize(&zmap->world_cell_data.streaming_info->region_data_offsets); ++i)
	{
		WorldRegionCommonParsed *region_parsed;
		WorldRegion *region;

		if (wlIsServer())
		{
			WorldRegionServerParsed *server_region = StructUnpack(parse_WorldRegionServerParsed, zmap->world_cell_data.streaming_info->packed_data, zmap->world_cell_data.streaming_info->region_data_offsets[i]);
			region_parsed = &server_region->common;
		}
		else
		{
			WorldRegionClientParsed *client_region = StructUnpack(parse_WorldRegionClientParsed, zmap->world_cell_data.streaming_info->packed_data, zmap->world_cell_data.streaming_info->region_data_offsets[i]);
			region_parsed = &client_region->common;
		}

		region = zmapGetWorldRegionByName(zmap, region_parsed->region_name);

		j = eaPush(&zmap->world_cell_data.streaming_info->regions_parsed, region_parsed);
		assert(i == j);

		worldCellFree(region->root_world_cell);
		worldCellFree(region->temp_world_cell);
		region->root_world_cell = NULL;
		region->temp_world_cell = NULL;
		if (wl_state.free_region_graphics_data)
			wl_state.free_region_graphics_data(region, false);
		region->graphics_data = NULL;
		region->world_bounds.needs_update = true;
		region->preloaded_cell_data = false;

		copyVec3(region_parsed->cell_extents.min_block, region->binned_bounds.cell_extents.min_block);
		copyVec3(region_parsed->cell_extents.max_block, region->binned_bounds.cell_extents.max_block);
	}

	loadstart_printf("Loading terrain layers...");
	// fill layer bounds, load terrain layers
	for (i = 0; i < eaSize(&zmap->layers); ++i)
	{
		LayerBounds *bounds = getLayerBounds(zmap->world_cell_data.streaming_info, zmap->layers[i]->filename);
		layerLoadStreaming(zmap->layers[i], zmap, NULL);
		if (bounds)
		{
			memcpy(&zmap->layers[i]->bounds,bounds,sizeof(*bounds));
		}
	}
	loadend_printf(" done.");

	eaDestroyStruct(&zmap->world_cell_data.streaming_info->layer_bounds, parse_LayerBounds);

	zmapRecalcBounds(zmap, true);
	worldBuildCellTree(zmap, true);

	streaming_pooled_info = zmap->world_cell_data.streaming_pooled_info;

	for (i = 0; i < eaSize(&streaming_pooled_info->packed_info->shared_bounds); ++i)
		streaming_pooled_info->packed_info->shared_bounds[i] = lookupSharedBoundsFromParsed(zmap, streaming_pooled_info->packed_info->shared_bounds[i]);

	{
		ShaderGraphQuality actual_quality = wl_state.desired_quality;

		// Push max quality shaders
		wl_state.desired_quality = SGRAPH_QUALITY_MAX_VALUE;
		if (wl_state.gfx_material_reload_all)
			wl_state.gfx_material_reload_all();

		for (i = 0; i < eaSize(&streaming_pooled_info->packed_info->material_draws_parsed); ++i)
		{
				MaterialDraw *material_draw = createMaterialDrawFromParsed(zmap, streaming_pooled_info->packed_info->material_draws_parsed[i]);
				eaPush(&streaming_pooled_info->material_draws, material_draw);
		}
		eaDestroyStruct(&streaming_pooled_info->packed_info->material_draws_parsed, parse_MaterialDrawParsed);

		// pop the quality level we expect and then make sure they are loaded.
		wl_state.desired_quality = actual_quality;
		if (wl_state.gfx_material_reload_all)
			wl_state.gfx_material_reload_all();
	}

	for (i = 0; i < eaSize(&streaming_pooled_info->packed_info->model_draws_parsed); ++i)
		eaPush(&streaming_pooled_info->model_draws, createModelDrawFromParsed(zmap, streaming_pooled_info->packed_info->model_draws_parsed[i]));
	eaDestroyStruct(&streaming_pooled_info->packed_info->model_draws_parsed, parse_ModelDrawParsed);

	for (i = 0; i < eaSize(&streaming_pooled_info->packed_info->subobjects_parsed); ++i)
		eaPush(&streaming_pooled_info->subobjects, createDrawableSubobjectFromParsed(zmap, streaming_pooled_info->packed_info->subobjects_parsed[i]));
	eaDestroyStruct(&streaming_pooled_info->packed_info->subobjects_parsed, parse_WorldDrawableSubobjectParsed);

	for (i = 0; i < eaSize(&streaming_pooled_info->packed_info->drawable_lists_parsed); ++i)
		eaPush(&streaming_pooled_info->drawable_lists, createDrawableListFromParsed(zmap, streaming_pooled_info->packed_info->drawable_lists_parsed[i]));
	eaDestroyStruct(&streaming_pooled_info->packed_info->drawable_lists_parsed, parse_WorldDrawableListParsed);

	for (i = 0; i < eaSize(&streaming_pooled_info->packed_info->instance_param_lists_parsed); ++i)
		eaPush(&streaming_pooled_info->instance_param_lists, createInstanceParamListFromParsed(zmap, streaming_pooled_info->packed_info->instance_param_lists_parsed[i]));
	eaDestroyStruct(&streaming_pooled_info->packed_info->instance_param_lists_parsed, parse_WorldInstanceParamListParsed);

	for (i = 0; i < eaSize(&streaming_pooled_info->packed_info->interaction_costumes_parsed); ++i)
		eaPush(&streaming_pooled_info->interaction_costumes, createInteractionCostumeFromParsed(zmap, streaming_pooled_info->packed_info->interaction_costumes_parsed[i], i));
	eaDestroyStruct(&streaming_pooled_info->packed_info->interaction_costumes_parsed, parse_WorldInteractionCostumeParsed);

	for (i = 0; i < eaSize(&zmap->world_cell_data.streaming_info->regions_parsed); ++i)
	{
		WorldRegionCommonParsed *region_parsed = zmap->world_cell_data.streaming_info->regions_parsed[i];
		WorldRegion *region = zmapGetWorldRegionByName(zmap, region_parsed->region_name);

 		if (eaSize(&region_parsed->cells) > 0)
 			fillCellData(region->root_world_cell, region_parsed->cells[0], region_parsed);

		if (wlIsServer())
		{
			WorldRegionServerParsed *server_region = (WorldRegionServerParsed *)region_parsed;

			assert(!region->room_conn_graph); // There are multiple regions of the same name in the bins!

			// load room connectivity graph data
			if (server_region->conn_graph)
				region->room_conn_graph = roomConnGraphFromServerParsed(server_region->conn_graph, zmap->world_cell_data.streaming_info->layer_names);

			if (server_region->world_civilian_generators)
				eaCopyStructs(&server_region->world_civilian_generators, &region->world_civilian_generators, parse_WorldCivilianGenerator);

			if (server_region->world_forbidden_positions)
				eaCopyStructs(&server_region->world_forbidden_positions, &region->world_forbidden_positions, parse_WorldForbiddenPosition);
		}
		else
		{
			WorldRegionClientParsed *client_region = (WorldRegionClientParsed *)region_parsed;

			// load room connectivity graph data
			if (client_region->conn_graph)
				region->room_conn_graph = roomConnGraphFromClientParsed(client_region->conn_graph, zmap->world_cell_data.streaming_info->layer_names);

			if(client_region->world_path_nodes)
				eaCopyStructs(&client_region->world_path_nodes, &region->world_path_nodes, parse_WorldPathNode);

			region->mapsnap_data = client_region->mapsnap_data;
		}

		eaCopyStructs(&region_parsed->tag_locations, &region->tag_locations, parse_WorldTagLocation);

		if (!region_parsed->hog_file)
		{
			char region_hog_filename[MAX_PATH];
			const char * region_name_or_default = region_parsed->region_name ? region_parsed->region_name : "Default";
			sprintf(region_hog_filename, "%s/world_%s_entries.hogg", base_dir, region_name_or_default);
			if (force_cache_update)
			{
				FolderCacheForceUpdate(folder_cache, region_hog_filename);
			}
			region_parsed->hog_file = hogFileReadEx(region_hog_filename, NULL, PIGERR_ASSERT, NULL, HOG_NOCREATE | HOG_READONLY | HOG_NO_INTERNAL_TIMESTAMPS, 1024);
			if (!region_parsed->hog_file)
			{
				// deprecated naming scheme
				char region_hog_filename_old[MAX_PATH];
				sprintf(region_hog_filename_old, "%s/region%d.hogg", base_dir, i);
				region_parsed->hog_file = hogFileReadEx(region_hog_filename_old, NULL, PIGERR_ASSERT, NULL, HOG_NOCREATE | HOG_READONLY | HOG_NO_INTERNAL_TIMESTAMPS, 1024);
			}
			if (!region_parsed->hog_file)
				Errorf("Failed to load %s", region_hog_filename);

			sprintf(region_hog_filename, "%s/world_%s_cluster.hogg", base_dir, region_name_or_default);
			region_parsed->cluster_hog_file = hogFileReadEx(region_hog_filename, NULL, PIGERR_ASSERT, NULL, HOG_NOCREATE | HOG_READONLY | HOG_NO_INTERNAL_TIMESTAMPS, 1024);
		}
	}

	// load scope data
	if (zmap->zmap_scope)
	{
		worldZoneMapScopeDestroy(zmap->zmap_scope);
		zmap->zmap_scope = NULL;
	}
	if (zmap->world_cell_data.streaming_info->zmap_scope_data_offset)
	{
		zmap->zmap_scope = StructUnpack(parse_WorldZoneMapScope, zmap->world_cell_data.streaming_info->packed_data, zmap->world_cell_data.streaming_info->zmap_scope_data_offset - 1);
		worldZoneMapScopeProcessBinData(zmap->zmap_scope, zmap->world_cell_data.streaming_info->id_to_named_volume, zmap->world_cell_data.streaming_info->id_to_interactable);
	}
	else
	{
		zmap->zmap_scope = worldZoneMapScopeCreate();
	}

	// Server only - calculate map snap values
	// We can just calculate this at load time, currently - there may be a more logical place to do this [RMARR - 2/1/12]
	if( wlIsServer() )
	{
		for (i = 0; i < eaSize(&zmap->world_cell_data.streaming_info->regions_parsed); ++i)
		{
			WorldRegionCommonParsed *region_parsed = zmap->world_cell_data.streaming_info->regions_parsed[i];
			WorldRegion *region = zmapGetWorldRegionByName(zmap, region_parsed->region_name);

			mapSnapCalculateRegionData(region);
		}
	}
	else
	{
		for (i = 0; i < eaSize(&zmap->world_cell_data.streaming_info->regions_parsed); ++i)
		{
			WorldRegionCommonParsed *region_parsed = zmap->world_cell_data.streaming_info->regions_parsed[i];
			WorldRegion *region = zmapGetWorldRegionByName(zmap, region_parsed->region_name);
			region->world_cell_hogg = region_parsed->cluster_hog_file;
		}
	}

	if (!is_secondary && !isProductionMode())
	{
		if( wlIsServer() ) {
			if (areEditorsAllowed() && write_encounter_info
				&& (!worldZeniUpToDate(encounter_info_filename) || world_created_bins || terrain_need_bins || force_write_encounter_info)
				&& wl_state.create_encounter_info_callback)
			{
				wl_state.disable_game_callbacks = true;
				worldCheckForNeedToOpenCells(); // Open world cells to get interactable data
				wl_state.disable_game_callbacks = false;
				wl_state.create_encounter_info_callback(zmap, encounter_info_filename);
			}
			binNotifyTouchedOutputFile( encounter_info_filename );
		} else {
			if (areEditorsAllowed() && write_encounter_info
				&& (!worldZeniSnapUpToDate(external_map_snap_filename) || world_created_bins || terrain_need_bins || force_write_encounter_info)
				&& wl_state.create_encounter_info_callback)
			{
				wl_state.disable_game_callbacks = true;
				worldCheckForNeedToOpenCells(); // Open world cells to get interactable data
				wl_state.disable_game_callbacks = false;
				wl_state.create_encounter_info_callback(zmap, external_map_snap_filename);
			}
			binNotifyTouchedOutputFile( external_map_snap_filename );
		}
	}

	for (i = 0; i < eaSize(&file_list->output_files); ++i)
		binNotifyTouchedOutputFile(file_list->output_files[i]->filename);

	freeStreamingPooledInfoSafe(NULL, &old_pooled_data);
	StructDestroySafe(parse_BinFileList, &file_list);
}

int worldRebuiltBins(void)
{
	return rebuilt_bins;
}

AUTO_COMMAND ACMD_CATEGORY(Debug);
void worldCellSetEditable(void)
{
	bool left_streaming_mode = false;
	int i, j;

	assert(!isProductionMode() || isProductionEditMode());

	triviaPrintf("ZoneMapState", "Editing");

	collCacheSetDisabled(1);
	for (j = 0; j < eaSize(&world_grid.maps); ++j)
	{
		ZoneMap *zmap = world_grid.maps[j];

		if (!zmap || !zmap->world_cell_data.streaming_info)
			continue;

		if (!left_streaming_mode)
		{
			dynDrawSkeletonResetDrawableLists();
			loadstart_printf("Exiting streaming mode...");
		}
		left_streaming_mode = true;

		interactionCostumeLeaveStreamingMode(zmap);

		// rebuild the stash to hold volume entry ids to named volumes
		if (zmap->world_cell_data.streaming_info->id_to_named_volume)
			stashTableDestroy(zmap->world_cell_data.streaming_info->id_to_named_volume);
		zmap->world_cell_data.streaming_info->id_to_named_volume = stashTableCreateInt(64);

		// rebuild the stash to hold interactables ids to named interactables
		if (zmap->world_cell_data.streaming_info->id_to_interactable)
			stashTableDestroy(zmap->world_cell_data.streaming_info->id_to_interactable);
		zmap->world_cell_data.streaming_info->id_to_interactable = stashTableCreateInt(64);

		// reload scope data from bins
		if (zmap->zmap_scope)
		{
			worldZoneMapScopeDestroy(zmap->zmap_scope);
			zmap->zmap_scope = NULL;
		}

		if (zmap->world_cell_data.streaming_info->zmap_scope_data_offset)
		{
			// recreate the zonemap scope and transfer binned data to it
			zmap->zmap_scope = StructUnpack(parse_WorldZoneMapScope, zmap->world_cell_data.streaming_info->packed_data, zmap->world_cell_data.streaming_info->zmap_scope_data_offset - 1);
			worldZoneMapScopeProcessBinData(zmap->zmap_scope, zmap->world_cell_data.streaming_info->id_to_named_volume, zmap->world_cell_data.streaming_info->id_to_interactable);
		}
		else
		{
			zmap->zmap_scope = worldZoneMapScopeCreate();
		}

		// free the existing cells (and thus the entries already in existence)
		worldBuildCellTree(zmap, true);

		if (zmap->world_cell_data.interaction_node_hash)
			assert(stashGetCount(zmap->world_cell_data.interaction_node_hash) == 0);

		loadstart_printf("Loading all bin files...");

		// load all entries from bins and insert them into the cell trees
		for (i = 0; i < eaSize(&zmap->world_cell_data.streaming_info->regions_parsed); ++i)
		{
			WorldRegionCommonParsed *region_parsed = zmap->world_cell_data.streaming_info->regions_parsed[i];
			WorldRegion *region = zmapGetWorldRegionByName(zmap, region_parsed->region_name);

			// recreate cell entries
			createAllRegionCellEntries(region, region_parsed);

			// reload room conn graph data
			if (wlIsServer())
			{
				WorldRegionServerParsed *server_region = (WorldRegionServerParsed *)region_parsed;

				// load room connectivity graph data
				if (server_region->conn_graph)
					region->room_conn_graph = roomConnGraphFromServerParsed(server_region->conn_graph, zmap->world_cell_data.streaming_info->layer_names);
			}
			else
			{
				WorldRegionClientParsed *client_region = (WorldRegionClientParsed *)region_parsed;

				// load room connectivity graph data
				if (client_region->conn_graph)
					region->room_conn_graph = roomConnGraphFromClientParsed(client_region->conn_graph, zmap->world_cell_data.streaming_info->layer_names);

					region->mapsnap_data = client_region->mapsnap_data;
			}

			eaCopyStructs(&region_parsed->tag_locations, &region->tag_locations, parse_WorldTagLocation);
		}

		loadend_printf(" done.");

		// free streaming info
		freeWorldInfo(zmap);
	}

	if (left_streaming_mode)
		loadend_printf(" done.");
}

static void __stdcall backgroundLoadCell(GeoRenderInfo *dummy_info, void *parent_unused, int desired_cell_state)
{
	WorldCell *cell = (WorldCell *)dummy_info;
	ZoneMap *zmap = cell->region->zmap_parent;

	if (!zmap)
		return;

	PERFINFO_AUTO_START_FUNC();

	assert(cell->region_parsed);
	
	// fill in parsed data
	//LDM: technically cell->server_data_parsed and cell->client_data_parsed are in a union but just incase somebody changes that in the future
	if (wlIsServer())
		loadCellEntries(cell->region, cell->region_parsed, findParsedCellIdx(cell->region_parsed, &cell->cell_block_range), &cell->server_data_parsed);
	else
		loadCellEntries(cell->region, cell->region_parsed, findParsedCellIdx(cell->region_parsed, &cell->cell_block_range), &cell->client_data_parsed);

	// mark as loaded, gives ownership back to main thread
	cell->cell_state = desired_cell_state;

	PERFINFO_AUTO_STOP();
}

static void __stdcall backgroundLoadCellWeldedBins(GeoRenderInfo *dummy_info, void *parent_unused, int desired_bin_state)
{
	WorldCell *cell = (WorldCell *)dummy_info;
	ZoneMap *zmap = cell->region->zmap_parent;

	if (!zmap)
		return;

	PERFINFO_AUTO_START_FUNC();

	assert(cell->region_parsed);

	// fill in parsed data
	loadWeldedCellEntries(cell->region_parsed, findParsedCellIdx(cell->region_parsed, &cell->cell_block_range), &cell->welded_data_parsed);

	// mark as loaded, gives ownership back to main thread
	cell->bins_state = desired_bin_state;

	PERFINFO_AUTO_STOP();
}

void worldCellForceLoad(WorldCell *cell)
{
	assert(!cell->server_data_parsed && !cell->client_data_parsed);
	filePushDiskAccessAllowedInMainThread(true);
	backgroundLoadCell((GeoRenderInfo *)cell, NULL, cell->cell_state);
	filePopDiskAccessAllowedInMainThread();
}

// TODO SIMPLYGON move to wlModelLoad.c,h
extern Model * tempModelLoadFromHog_dbg(HogFile * hog_file, const char *header_name, const char *modelset_name, const char *model_name, 
	WLUsageFlags use_flags MEM_DBG_PARMS);

void worldCellQueueClusterModelLoad(WorldCell *cell) {

	if(!cell || !cell->region) {
		return;
	}

	if (wl_state.type_worldCellGfxData)
	{
		char cell_cluster_name[MAX_PATH];
		char header_name[MAX_PATH];
		char mset_name[MAX_PATH];
		char cellClusterHoggPath[MAX_PATH];

		if (!cell->drawable.gfx_data)
			cell->drawable.gfx_data = StructCreateVoid(wl_state.type_worldCellGfxData);

		worldCellGetClusterName(SAFESTR(cell_cluster_name), cell);
		worldCellGetClusterBinHoggPath(SAFESTR(cellClusterHoggPath), cell);
		cell->cluster_hogg = hogFileRead(cellClusterHoggPath, NULL, PIGERR_ASSERT, NULL, HOG_NOCREATE);
		if (cell->cluster_hogg)
		{
			sprintf(header_name, "%s.mhdr", cell_cluster_name);
			sprintf(mset_name, "%s.mset", cell_cluster_name);

			assert(cell->region);

			cell->draw_model_cluster = tempModelLoadFromHog_dbg(
				cell->cluster_hogg,
				header_name, mset_name,
				TERRAIN_WORLD_CLUSTER_MODEL_NAME, WL_FOR_WORLD MEM_DBG_PARMS_INIT);
		}
	}
}

void worldCellQueueLoad(WorldCell *cell, bool foreground_load)
{
	ZoneMap *zmap = cell->region->zmap_parent;

	// mark as loading, gives ownership to loading thread
	cell->cell_state = WCS_LOADING_BG;

	if (!zmap)
	{
		worldCellFinishLoad(cell);
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	assert(zmap->world_cell_data.streaming_info);

	if (cell->client_data_parsed || cell->server_data_parsed)
	{
		cell->cell_state = WCS_LOADING_FG;
	}
	else if (foreground_load || disable_background_loading)
	{
		filePushDiskAccessAllowedInMainThread(true);
		backgroundLoadCell((GeoRenderInfo *)cell, NULL, WCS_LOADING_FG);
		filePopDiskAccessAllowedInMainThread();
		worldCellFinishLoad(cell);
	}
	else
	{
		geoRequestBackgroundExec(backgroundLoadCell, hogFileGetArchiveFileName(cell->region_parsed->hog_file), (GeoRenderInfo *)cell, NULL, WCS_LOADING_FG, FILE_MEDIUM_PRIORITY);
	}

	PERFINFO_AUTO_STOP();
}

void worldCellFinishLoad(WorldCell *cell)
{
	ZoneMap *zmap = cell->region->zmap_parent;

	if (!zmap)
	{
		cell->cell_state = WCS_LOADED;
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	assert(zmap->world_cell_data.streaming_info);
	assert(cell->cell_state == WCS_LOADING_FG);

//	filePushDiskAccessAllowedInMainThread(true);
	if (wlIsServer())
		createCellEntries(cell->region, cell, cell->server_data_parsed, NULL, false, !wl_state.keep_cell_data_loaded);
	else
		createCellEntries(cell->region, cell, cell->client_data_parsed, cell->region->world_cell_hogg, false, !wl_state.keep_cell_data_loaded);
//	filePopDiskAccessAllowedInMainThread();

	cell->cell_state = WCS_LOADED;

	PERFINFO_AUTO_STOP();
}

void worldCellCancelLoad(WorldCell *cell)
{
	PERFINFO_AUTO_START_FUNC();

	assert(cell->cell_state == WCS_LOADING_FG);

	if (wlIsServer())
	{
		if (cell->server_data_parsed && !wl_state.keep_cell_data_loaded)
		{
			StructDestroySafeInBackground(parse_WorldCellServerDataParsed, &cell->server_data_parsed);
		}
	}
	else
	{
		if (cell->client_data_parsed && !wl_state.keep_cell_data_loaded)
		{
			StructDestroySafeInBackground(parse_WorldCellClientDataParsed, &cell->client_data_parsed);
		}
	}

	cell->cell_state = WCS_LOADED;

	PERFINFO_AUTO_STOP();
}

void worldCellForceWeldedBinLoad(WorldCell *cell)
{
	if (!cell->welded_data_parsed)
	{
		filePushDiskAccessAllowedInMainThread(true);
		backgroundLoadCellWeldedBins((GeoRenderInfo *)cell, NULL, cell->bins_state);
		filePopDiskAccessAllowedInMainThread();
	}
}

void worldCellQueueWeldedBinLoad(WorldCell *cell)
{
	ZoneMap *zmap = cell->region->zmap_parent;

	if (!zmap)
	{
		cell->bins_state = WCS_LOADED;
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	assert(zmap->world_cell_data.streaming_info);

	if (wlIsClient())
	{
		// mark as loading, gives ownership to loading thread
		cell->bins_state = WCS_LOADING_BG;

		if (cell->welded_data_parsed)
		{
			cell->bins_state = WCS_LOADING_FG;
		}
		else if (disable_background_loading)
		{
			filePushDiskAccessAllowedInMainThread(true);
			backgroundLoadCellWeldedBins((GeoRenderInfo *)cell, NULL, WCS_LOADING_FG);
			filePopDiskAccessAllowedInMainThread();
			worldCellFinishWeldedBinLoad(cell);
		}
		else
		{
			geoRequestBackgroundExec(backgroundLoadCellWeldedBins, NULL, (GeoRenderInfo *)cell, NULL, WCS_LOADING_FG, FILE_MEDIUM_PRIORITY);
		}
	}
	else
	{
		cell->bins_state = WCS_LOADED;
	}

	PERFINFO_AUTO_STOP();
}

void worldCellFinishWeldedBinLoad(WorldCell *cell)
{
	ZoneMap *zmap = cell->region->zmap_parent;

	if (!zmap)
	{
		cell->bins_state = WCS_LOADED;
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	assert(zmap->world_cell_data.streaming_info);
	assert(cell->bins_state == WCS_LOADING_FG);

//	filePushDiskAccessAllowedInMainThread(true);
	createWeldedCellEntries(cell->region, cell, !wl_state.keep_cell_data_loaded);
//	filePopDiskAccessAllowedInMainThread();

	cell->bins_state = WCS_LOADED;

	PERFINFO_AUTO_STOP();
}

void worldCellCancelWeldedBinLoad(WorldCell *cell)
{
	PERFINFO_AUTO_START_FUNC();

	assert(cell->bins_state == WCS_LOADING_FG);

	if (cell->welded_data_parsed && !wl_state.keep_cell_data_loaded)
	{
		StructDestroySafeInBackground(parse_WorldCellWeldedDataParsed, &cell->welded_data_parsed);
	}

	cell->bins_state = WCS_LOADED;

	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND;
void worldCellTest(void)
{
	WorldCellClientDataParsed data1 = {0}, data2 = {0};
	char filename[MAX_PATH];

	ParserOpenReadBinaryFile(NULL, "test/d0.1", parse_WorldCellClientDataParsed, &data1, NULL, NULL, NULL, NULL, 0, 0, BINARYREADFLAG_IGNORE_CRC);
	ParserOpenReadBinaryFile(NULL, "test/d0.2", parse_WorldCellClientDataParsed, &data2, NULL, NULL, NULL, NULL, 0, 0, BINARYREADFLAG_IGNORE_CRC);

	if (StructCompare(parse_WorldCellClientDataParsed, &data1, &data2, 0, 0, 0))
	{
		sprintf(filename, "%s/test/d0.1.txt", fileLocalDataDir());
		ParserWriteTextFile(filename, parse_WorldCellClientDataParsed, &data1, 0, 0);
		sprintf(filename, "%s/test/d0.2.txt", fileLocalDataDir());
		ParserWriteTextFile(filename, parse_WorldCellClientDataParsed, &data2, 0, 0);
	}

	StructReset(parse_WorldCellClientDataParsed, &data1);
	StructReset(parse_WorldCellClientDataParsed, &data2);
}

AUTO_COMMAND;
void worldCellInfoTest(void)
{
	WorldStreamingInfo data1 = {0}, data2 = {0};
	char filename[MAX_PATH];
	int i;

	ParserOpenReadBinaryFile(NULL, "test/world_cells.info.1", parse_WorldStreamingInfo, &data1, NULL, NULL, NULL, NULL, 0, 0, BINARYREADFLAG_IGNORE_CRC);
	ParserOpenReadBinaryFile(NULL, "test/world_cells.info.2", parse_WorldStreamingInfo, &data2, NULL, NULL, NULL, NULL, 0, 0, BINARYREADFLAG_IGNORE_CRC);

	if (StructCompare(parse_WorldStreamingInfo, &data1, &data2, 0, 0, 0))
	{
		sprintf(filename, "%s/test/world_cells.info.1.txt", fileLocalDataDir());
		ParserWriteTextFile(filename, parse_WorldStreamingInfo, &data1, 0, 0);

		sprintf(filename, "%s/test/world_cells.info.2.txt", fileLocalDataDir());
		ParserWriteTextFile(filename, parse_WorldStreamingInfo, &data2, 0, 0);


		data1.packed_data = calloc(sizeof(*data1.packed_data), 1);
		PackedStructStreamDeserialize(data1.packed_data, data1.packed_data_serialize);

		data2.packed_data = calloc(sizeof(*data2.packed_data), 1);
		PackedStructStreamDeserialize(data2.packed_data, data2.packed_data_serialize);

		for (i = 0; i < eaiSize(&data1.region_data_offsets) && i < eaiSize(&data2.region_data_offsets); ++i)
		{
			WorldRegionClientParsed *region1 = StructUnpack(parse_WorldRegionClientParsed, data1.packed_data, data1.region_data_offsets[i]);
			WorldRegionClientParsed *region2 = StructUnpack(parse_WorldRegionClientParsed, data2.packed_data, data2.region_data_offsets[i]);
			
			if (StructCompare(parse_WorldRegionClientParsed, region1, region2, 0, 0, 0))
			{
				sprintf(filename, "%s/test/world_cells.info.region%d.1.txt", fileLocalDataDir(), i);
				ParserWriteTextFile(filename, parse_WorldRegionClientParsed, region1, 0, 0);

				sprintf(filename, "%s/test/world_cells.info.region%d.2.txt", fileLocalDataDir(), i);
				ParserWriteTextFile(filename, parse_WorldRegionClientParsed, region2, 0, 0);
			}

			StructDestroy(parse_WorldRegionClientParsed, region1);
			StructDestroy(parse_WorldRegionClientParsed, region2);
		}

		if (data1.zmap_scope_data_offset && data2.zmap_scope_data_offset)
		{
			WorldZoneMapScope *scope1 = StructUnpack(parse_WorldZoneMapScope, data1.packed_data, data1.zmap_scope_data_offset - 1);
			WorldZoneMapScope *scope2 = StructUnpack(parse_WorldZoneMapScope, data1.packed_data, data2.zmap_scope_data_offset - 1);

			if (StructCompare(parse_WorldZoneMapScope, scope1, scope2, 0, 0, 0))
			{
				sprintf(filename, "%s/test/world_cells.info.scope.1.txt", fileLocalDataDir());
				ParserWriteTextFile(filename, parse_WorldZoneMapScope, scope1, 0, 0);

				sprintf(filename, "%s/test/world_cells.info.scope.2.txt", fileLocalDataDir());
				ParserWriteTextFile(filename, parse_WorldZoneMapScope, scope2, 0, 0);
			}

			StructDestroy(parse_WorldZoneMapScope, scope1);
			StructDestroy(parse_WorldZoneMapScope, scope2);
		}

		PackedStructStreamDeinit(data1.packed_data);
		free(data1.packed_data);

		PackedStructStreamDeinit(data2.packed_data);
		free(data2.packed_data);
	}

	StructReset(parse_WorldStreamingInfo, &data1);
	StructReset(parse_WorldStreamingInfo, &data2);
}

AUTO_COMMAND;
void worldTerrainAtlasTest(void)
{
	HeightMapAtlasRegionData data1 = {0}, data2 = {0};
	char filename[MAX_PATH];

	ParserOpenReadBinaryFile(NULL, "test/terrain_Default_atlases.bin.1", parse_HeightMapAtlasRegionData, &data1, NULL, NULL, NULL, NULL, 0, 0, BINARYREADFLAG_IGNORE_CRC);
	ParserOpenReadBinaryFile(NULL, "test/terrain_Default_atlases.bin.2", parse_HeightMapAtlasRegionData, &data2, NULL, NULL, NULL, NULL, 0, 0, BINARYREADFLAG_IGNORE_CRC);

	if (StructCompare(parse_HeightMapAtlasRegionData, &data1, &data2, 0, 0, 0))
	{
		sprintf(filename, "%s/test/terrain_Default_atlases.bin.1.txt", fileLocalDataDir());
		ParserWriteTextFile(filename, parse_HeightMapAtlasRegionData, &data1, 0, 0);

		sprintf(filename, "%s/test/terrain_Default_atlases.bin.2.txt", fileLocalDataDir());
		ParserWriteTextFile(filename, parse_HeightMapAtlasRegionData, &data2, 0, 0);
	}

	StructReset(parse_HeightMapAtlasRegionData, &data1);
	StructReset(parse_HeightMapAtlasRegionData, &data2);
}

static bool worldBinUnpackHogFile(HogFile *handle, HogFileIndex index, const char* filename, char *out_dir)
{
	char out_filename[CRYPTIC_MAX_PATH];
	bool extracted = false;

	if (strEndsWith(out_dir, "_entries.hogg/"))
	{
		if (filename[0] == 'D' || filename[0] == 'd')
		{
			WorldCellClientDataParsed data = { 0 };
			if (ParserOpenReadBinaryFile(handle, filename, parse_WorldCellClientDataParsed, &data, NULL, NULL, NULL, NULL, 0, 0, BINARYREADFLAG_IGNORE_CRC))
			{
				sprintf(out_filename, "%s/%s.txt", out_dir, filename);
				ParserWriteTextFile(out_filename, parse_WorldCellClientDataParsed, &data, 0, 0);
			}
			else
			{
				printf("Failed to open %s.\n", filename);
			}
			extracted = true;
		}
		else if (filename[0] == 'W' || filename[0] == 'w')
		{
			WorldCellWeldedDataParsed data = { 0 };
			if (ParserOpenReadBinaryFile(handle, filename, parse_WorldCellWeldedDataParsed, &data, NULL, NULL, NULL, NULL, 0, 0, BINARYREADFLAG_IGNORE_CRC))
			{
				sprintf(out_filename, "%s/%s.txt", out_dir, filename);
				ParserWriteTextFile(out_filename, parse_WorldCellWeldedDataParsed, &data, 0, 0);
			}
			else
			{
				printf("Failed to open %s.\n", filename);
			}
			extracted = true;
		}
	}
	if (!extracted)
	{
		void *data;
		U32 count;
		bool checksum_valid;
		sprintf(out_filename, "%s/%s", out_dir, filename);
		mkdirtree(out_filename);
		data = hogFileExtract(handle, index, &count, &checksum_valid);
		if (data)
		{
			FILE *fOut = fopen(out_filename, "wb");
			fwrite(data, 1, count, fOut);
			fclose(fOut);
			SAFE_FREE(data);
		}
	}
	return true;
}

static void worldBinUnpackFile(char *filename, char *short_filename, char *out_dir)
{
	char out_filename[CRYPTIC_MAX_PATH];
	if (strEndsWith(filename, "_atlases.bin"))
	{
		HeightMapAtlasRegionData data1 = {0};
		if (ParserOpenReadBinaryFile(NULL, filename, parse_HeightMapAtlasRegionData, &data1, NULL, NULL, NULL, NULL, 0, 0, BINARYREADFLAG_IGNORE_CRC))
		{
			sprintf(out_filename, "%s/%s.txt", out_dir, short_filename);
			ParserWriteTextFile(out_filename, parse_HeightMapAtlasRegionData, &data1, 0, 0);
		}
		else
		{
			printf("Failed to open %s.\n", filename);
		}
	}
	if (strEndsWith(filename, "world_cells.info"))
	{
		int i;
		WorldStreamingInfo data1 = {0};
		if (ParserOpenReadBinaryFile(NULL, filename, parse_WorldStreamingInfo, &data1, NULL, NULL, NULL, NULL, 0, 0, BINARYREADFLAG_IGNORE_CRC))
		{
			sprintf(out_filename, "%s/%s.txt", out_dir, short_filename);
			ParserWriteTextFile(out_filename, parse_WorldStreamingInfo, &data1, 0, 0);

			data1.packed_data = calloc(sizeof(*data1.packed_data), 1);
			PackedStructStreamDeserialize(data1.packed_data, data1.packed_data_serialize);

			for (i = 0; i < eaiSize(&data1.region_data_offsets); ++i)
			{
				WorldRegionClientParsed *region1 = StructUnpack(parse_WorldRegionClientParsed, data1.packed_data, data1.region_data_offsets[i]);

				sprintf(out_filename, "%s/%s.region%d.txt", out_dir, short_filename, i);
				ParserWriteTextFile(out_filename, parse_WorldRegionClientParsed, region1, 0, 0);

				StructDestroy(parse_WorldRegionClientParsed, region1);
			}

			if (data1.zmap_scope_data_offset)
			{
				WorldZoneMapScope *scope1 = StructUnpack(parse_WorldZoneMapScope, data1.packed_data, data1.zmap_scope_data_offset - 1);

				sprintf(out_filename, "%s/%s.scope.txt", out_dir, short_filename);
				ParserWriteTextFile(out_filename, parse_WorldZoneMapScope, scope1, 0, 0);

				StructDestroy(parse_WorldZoneMapScope, scope1);
			}

			PackedStructStreamDeinit(data1.packed_data);
			free(data1.packed_data);
		}
		else
		{
			printf("Failed to open %s.\n", filename);
		}
	}
	if (strEndsWith(filename, "world_cells_shared.info"))
	{
		WorldStreamingPooledInfo data1 = {0};
		if (ParserOpenReadBinaryFile(NULL, filename, parse_WorldStreamingPooledInfo, &data1, NULL, NULL, NULL, NULL, 0, 0, BINARYREADFLAG_IGNORE_CRC))
		{
			sprintf(out_filename, "%s/%s.txt", out_dir, short_filename);
			ParserWriteTextFile(out_filename, parse_WorldStreamingPooledInfo, &data1, 0, 0);

			data1.packed_data = calloc(sizeof(*data1.packed_data), 1);
			PackedStructStreamDeserialize(data1.packed_data, data1.packed_data_serialize);

			data1.packed_info = StructUnpack(parse_WorldStreamingPackedInfo, data1.packed_data, data1.packed_info_offset - 1);
			sprintf(out_filename, "%s/%s.packed.txt", out_dir, short_filename);
			ParserWriteTextFile(out_filename, parse_WorldStreamingPackedInfo, data1.packed_info, 0, 0);

			PackedStructStreamDeinit(data1.packed_data);
			free(data1.packed_data);
		}
		else
		{
			printf("Failed to open %s.\n", filename);
		}
	}
	if (strEndsWith(filename, "world_cells_deps.bin"))
	{
		BinFileList file_list = {0};
		if (ParserOpenReadBinaryFile(NULL, filename, parse_BinFileList, &file_list, NULL, NULL, NULL, NULL, 0, 0, BINARYREADFLAG_IGNORE_CRC))
		{
			sprintf(out_filename, "%s/%s.txt", out_dir, short_filename);
			ParserWriteTextFile(out_filename, parse_BinFileList, &file_list, 0, 0);
		}
		else
		{
			printf("Failed to open %s.\n", filename);
		}
	}
	if (strEndsWith(filename, "world_cells.deps"))
	{
		WorldDependenciesList list = { 0 };
		if (ParserLoadFiles(NULL, filename, NULL, 0, parse_WorldDependenciesList, &list))
		{
			sprintf(out_filename, "%s/%s.txt", out_dir, short_filename);
			ParserWriteTextFile(out_filename, parse_WorldDependenciesList, &list, 0, 0);
		}
		else
		{
			printf("Failed to open %s.\n", filename);
		}
	}
	if (strEndsWith(filename, "world_cells.region_bounds"))
	{
		ZoneMapRegionBounds bounds = { 0 };
		if (ParserLoadFiles(NULL, filename, NULL, 0, parse_ZoneMapRegionBounds, &bounds))
		{
			sprintf(out_filename, "%s/%s.txt", out_dir, short_filename);
			ParserWriteTextFile(out_filename, parse_ZoneMapRegionBounds, &bounds, 0, 0);
		}
		else
		{
			printf("Failed to open %s.\n", filename);
		}
	}
	if (strEndsWith(filename, ".hogg"))
	{
		char hog_out_dir[CRYPTIC_MAX_PATH];
		HogFile *hogfile = hogFileRead(filename, NULL, PIGERR_PRINTF, NULL, HOG_NOCREATE);
		sprintf(hog_out_dir, "%s/%s/", out_dir, short_filename);
		hogScanAllFiles(hogfile, worldBinUnpackHogFile, hog_out_dir);
		hogFileDestroy(hogfile, true);
	}
}

AUTO_COMMAND;
void worldBinUnpackDirectory(char *dir)
{
	int i;
	char out_dir[CRYPTIC_MAX_PATH];
	char **list = fileScanDir(dir);
	int offset = (int)strlen(dir);

	sprintf(out_dir, "%s.unpack", dir);
	for (i = 0; i < eaSize(&list); i++)
	{
		worldBinUnpackFile(list[i], &list[i][offset], out_dir);
	}
}

AUTO_COMMAND;
void worldCellDepsFixup(const char *log_filename, const char *build_root, int is_server)
{
	char str[2048], map_filename[MAX_PATH], deps_filename[MAX_PATH], deps_out_filename[MAX_PATH], bin_fullpath[MAX_PATH], *server_str;
	FILE *f;

	if (!log_filename || !build_root)
		return;

	f = fopen(log_filename, "rt");
	if (!f)
		return;

	server_str = is_server ? "server/" : "";

	while (fgets(str, sizeof(str), f))
	{
		BinFileList file_list = {0};
		char *s, *s2;
		int i;

		// find map filename
		s = strchr(str, '"');
		if (!s)
			continue;
		s++;
		s2 = strchr(s, '"');
		if (!s2)
			continue;
		*s2 = 0;
		s2++;
		strcpy(map_filename, s);

		// read deps file
		// world_cells_deps has been moved to tempbin. If you need this to work, it will have to be updated. -TomY
		sprintf(deps_filename, "%s/%sbin/geobin/%s/world_cells_deps.bin", build_root, server_str, map_filename);
		sprintf(deps_out_filename, "c:/temp/DepsFixup/%sbin/geobin/%s/world_cells_deps.bin", server_str, map_filename);
		if (!ParserOpenReadBinaryFile(NULL, deps_filename, parse_BinFileList, &file_list, NULL, NULL, NULL, NULL, 0, 0, 0))
		{
			StructDeInit(parse_BinFileList, &file_list);
			printf("Could not open deps file.\n");
			continue;
		}

		// fixup all entries
		for (i = 0; i < eaSize(&file_list.output_files); ++i)
		{
			U32 timestamp;
			forwardSlashes(file_list.output_files[i]->filename);
			sprintf(bin_fullpath, "%s/%s", build_root, file_list.output_files[i]->filename);
			timestamp = fileLastChangedAbsolute(bin_fullpath);
			if (timestamp)
			{
				file_list.output_files[i]->timestamp = timestamp;
				if (strstri(file_list.output_files[i]->filename, "HeatMapTemplates"))
					file_list.output_files[i]->timestamp |= IGNORE_EXIST_TIMESTAMP_BIT;
			}
		}

		// write deps file back out
		ParserWriteBinaryFile(deps_out_filename, NULL, parse_BinFileList, &file_list, NULL, NULL, NULL, NULL, 0, 0, NULL, 0, 0);

		// cleanup
		StructDeInit(parse_BinFileList, &file_list);
	}

	fclose(f);
}

AUTO_COMMAND;
void worldCellMaterialTemplateReport(bool include_fallbacks)
{
	ZoneMapInfo *zminfo, **zminfos = NULL;
	RefDictIterator zmap_iter;
	StashTable template_hash;
	StashTableIterator iter;
	StashElement elem;
	char deps_filename[MAX_PATH];
	int i, j, k, f;

	worldGetZoneMapIterator(&zmap_iter);
	while (zminfo = worldGetNextZoneMap(&zmap_iter)) // worldGetNextZoneMap does the private map filtering
	{
		const char *public_name = zmapInfoGetPublicName(zminfo);
		if (!public_name || eaSize(&zminfo->private_to))
			continue;
		eaPush(&zminfos, zminfo);
	}

	template_hash = stashTableCreateAddress(1024);

	for (i = 0; i < eaSize(&zminfos); ++i)
	{
		WorldDependenciesList *list = StructAlloc(parse_WorldDependenciesList);
		zminfo = zminfos[i];
		sprintf(deps_filename, "bin/geobin/%s/world_cells.deps", zmapInfoGetFilename(zminfo));
		if (!ParserReadTextFile(deps_filename, parse_WorldDependenciesList, list, 0))
		{
			StructDestroy(parse_WorldDependenciesList, list);
			continue;
		}

		for (j = 0; j < eaSize(&list->deps); ++j)
		{
			for (k = 0; k < eaSize(&list->deps[j]->material_deps); ++k)
			{
				const MaterialData *mat_data = materialFindData(list->deps[j]->material_deps[k]);
				ShaderTemplate *shader;

				if (!mat_data)
					continue;

				if (shader = materialGetTemplateByName(mat_data->graphic_props.default_fallback.shader_template_name))
				{
					int count;
					if (!stashFindInt(template_hash, shader, &count))
						count = 0;
					count++;
					stashAddInt(template_hash, shader, count, true);
				}

				if (include_fallbacks)
				{
					for (f = 0; f < eaSize(&mat_data->graphic_props.fallbacks); ++f)
					{
						if (shader = materialGetTemplateByName(mat_data->graphic_props.fallbacks[f]->shader_template_name))
						{
							int count;
							if (!stashFindInt(template_hash, shader, &count))
								count = 0;
							count++;
							stashAddInt(template_hash, shader, count, true);
						}
					}
				}
			}
		}

		StructDestroy(parse_WorldDependenciesList, list);
	}

	eaDestroy(&zminfos);

	printf("\n\n");

	stashGetIterator(template_hash, &iter);
	while (stashGetNextElement(&iter, &elem))
	{
		ShaderTemplate *shader = stashElementGetKey(elem);
		int count = stashElementGetInt(elem);
		if (shader)
			printf("%6d : %s\n", count, shader->filename);
	}
	stashTableDestroy(template_hash);
}

AUTO_COMMAND;
void worldCellMaterialTemplateReportMaps(bool include_fallbacks, const char *template_name)
{
	ZoneMapInfo *zminfo, **zminfos = NULL;
	RefDictIterator zmap_iter;
	char deps_filename[MAX_PATH];
	int i, j, k, f;

	worldGetZoneMapIterator(&zmap_iter);
	while (zminfo = worldGetNextZoneMap(&zmap_iter)) // worldGetNextZoneMap does the private map filtering
	{
		const char *public_name = zmapInfoGetPublicName(zminfo);
		if (!public_name || eaSize(&zminfo->private_to))
			continue;
		eaPush(&zminfos, zminfo);
	}

	printf("\n\n");

	for (i = 0; i < eaSize(&zminfos); ++i)
	{
		WorldDependenciesList *list = StructAlloc(parse_WorldDependenciesList);
		zminfo = zminfos[i];
		sprintf(deps_filename, "bin/geobin/%s/world_cells.deps", zmapInfoGetFilename(zminfo));
		if (!ParserReadTextFile(deps_filename, parse_WorldDependenciesList, list, 0))
		{
			StructDestroy(parse_WorldDependenciesList, list);
			continue;
		}

		for (j = 0; j < eaSize(&list->deps); ++j)
		{
			for (k = 0; k < eaSize(&list->deps[j]->material_deps); ++k)
			{
				const MaterialData *mat_data = materialFindData(list->deps[j]->material_deps[k]);

				if (!mat_data)
					continue;

				if (stricmp(mat_data->graphic_props.default_fallback.shader_template_name, template_name)==0)
					printf("%s  :  %s\n", zmapInfoGetFilename(zminfo), mat_data->material_name);

				if (include_fallbacks)
				{
					for (f = 0; f < eaSize(&mat_data->graphic_props.fallbacks); ++f)
					{
						if (stricmp(mat_data->graphic_props.fallbacks[f]->shader_template_name, template_name)==0)
							printf("%s  :  %s\n", zmapInfoGetFilename(zminfo), mat_data->material_name);
					}
				}
			}
		}

		StructDestroy(parse_WorldDependenciesList, list);
	}

	eaDestroy(&zminfos);
}
