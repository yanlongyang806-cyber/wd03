/***************************************************************************



***************************************************************************/

#include "WorldCell.h"
#include "WorldCellStreaming.h"
#include "WorldCellClustering.h"
#include "WorldCellEntryPrivate.h"
#include "wlTerrainPrivate.h"
#include "wlState.h"
#include "dynWind.h"
#include "DebugState.h"
#include "partition_enums.h"
#include "wininclude.h"
#include "serialize.h"
#include "bounds.h"
#include "wlModel.h"
#include "hoglib.h"

#include "WorldCellStreamingPrivate_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

static bool g_bNoCloseCellsDueToTempCameraPosition = false;

static int world_cell_entry_count;
static int bin_disable_far_welding, bin_disable_near_bypass, bin_disable_near_fade_welding;

// Disables welding of the lowest LODs in the distance.  Must rebin the map to take effect.
AUTO_CMD_INT(bin_disable_far_welding, binDisableFarWelding) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Disables bypassing putting entries with a near fade distance into the near cells.  Must rebin the map to take effect.
AUTO_CMD_INT(bin_disable_near_bypass, binDisableNearBypass) ACMD_CMDLINE ACMD_CATEGORY(Debug);

// Disables closer welding of near fade entries.  Must rebin the map to take effect.
AUTO_CMD_INT(bin_disable_near_fade_welding, binDisableNearFadeWelding) ACMD_CMDLINE ACMD_CATEGORY(Debug);


AUTO_RUN;
void addWorldCellWatches(void)
{
	dbgAddIntWatch("WorldCellEntryCount", world_cell_entry_count);
}


//////////////////////////////////////////////////////////////////////////
// Misc WorldCell Notes:
//
// - every WorldCellEntry has a level in the WorldCell tree based on its vis distance
// - a WorldCellEntry can be in only one WorldCell at a time
// - a WorldCellEntry can NOT be in multiple WorldCells at different levels of the tree
// - opening a WorldCell creates its WorldCellEntries, which creates collision and drawable information
// - a WorldCell is not required to open all of its children at the same time
// - an open WorldCell must have open parents
// - on the server all WorldCells are open
//

__forceinline static void debugCellPrintf(WorldCell *cell, const char *function_name)
{
	if (cell)
		printf("(%d: %d, %d, %d) %s called\n", cell->vis_dist_level, cell->cell_block_range.min_block[0], cell->cell_block_range.min_block[1], cell->cell_block_range.min_block[2], function_name);
	else
		printf("%s called\n", function_name);
}

#if 0
#define DEBUG_CELL_PRINTF(cell) debugCellPrintf(cell, __FUNCTION__)
#define DEBUG_CELL_BREAK(cell, level, x, y, z) if ((cell) && (cell)->vis_dist_level == (level) && (cell)->block_range.min_block[0] == (x) && (cell)->block_range.min_block[1] == (y) && (cell)->block_range.min_block[2] == (z)) _DbgBreak()
#else
#define DEBUG_CELL_PRINTF(cell) 
#define DEBUG_CELL_BREAK(cell, level, x, y, z) 
#endif

#define MIN_WELD_VIS_DIST_RADIUS	40.f
#define MIN_WELD_VIS_DIST_LEVEL1	1
#define MIN_WELD_VIS_DIST_LEVEL2	2

#define LOAD_DIST_DELTA 50.f
#define CLOSE_DIST_DELTA 75.f

static F32 open_dist_sqrd_table[32], load_dist_sqrd_table[32], close_dist_sqrd_table[32];

void worldCellFreeCullHeaders(WorldCellCullHeader *cull_headers)
{
	int i, count;

	if (!cull_headers)
		return;

	count = cull_headers->skip_offset;
	for (i = 0; i < count; ++i)
	{
		WorldCellCullHeader *cell_header = &cull_headers[i];
		SAFE_FREE(cell_header->entry_headers);
		SAFE_FREE(cell_header->welded_entry_headers);
		SAFE_FREE(cell_header->editor_only_entry_headers);
		cell_header->entries_inited = false;
		cell_header->welded_entries_inited = false;
	}

	free(cull_headers);
}

static U32 worldCellGetVisDistLevelFar(
	WorldRegion *region, F32 vis_dist,
	F32 radius, WorldCellEntryType entry_type,
	WorldCellEntry *entry)
{
	U32 vis_dist_level;

	if (!region->root_world_cell)
		return 0;


	vis_dist += radius + CLOSE_DIST_DELTA;

	if (entry_type == WCENT_COLLISION)
		vis_dist += radius; // collision objects should be inserted based on diameter for efficient hierarchical tests
	else
		MAX1F(vis_dist, GRID_BLOCK_SIZE); // everything else should be at 256 foot resolution (GRID_BLOCK_SIZE) or lower

	vis_dist_level = log2(ceil(vis_dist / CELL_BLOCK_SIZE));
	MIN1(vis_dist_level, region->root_world_cell->vis_dist_level);
	return vis_dist_level;
}

U32 worldCellGetVisDistLevelNear(
	WorldRegion *region, F32 vis_dist,
	WorldCellEntry *entry)
{
	U32 vis_dist_level;

	if (!region || !region->root_world_cell)
		return 0;


	vis_dist -= CLOSE_DIST_DELTA;
	MAX1F(vis_dist, 0);
	vis_dist_level = log2_floor(floor(vis_dist / (2 * CELL_BLOCK_SIZE)));
	MIN1(vis_dist_level, region->root_world_cell->vis_dist_level);
	return vis_dist_level;
}

void worldCellFreeWeldedBin(WorldCellWeldedBin *bin)
{
	PERFINFO_AUTO_START_FUNC();

	eaDestroyEx(&bin->drawable_entries, worldCellEntryFree);
	eaDestroy(&bin->src_entries);
	free(bin);

	PERFINFO_AUTO_STOP();
}

static void worldCellOpenBins(WorldCell *cell)
{
	if (cell->streaming_mode && cell->bins_state == WCS_CLOSED)
		worldCellQueueWeldedBinLoad(cell);

	if (cell->bins_state == WCS_LOADING_FG)
		worldCellFinishWeldedBinLoad(cell);

	if (cell->bins_state == WCS_CLOSED || cell->bins_state == WCS_LOADED)
	{
		int i, j;
		for (i = eaSize(&cell->drawable.bins) - 1; i >= 0; --i)
		{
			for (j = eaSize(&cell->drawable.bins[i]->drawable_entries) - 1; j >= 0; --j)
				worldCellEntryOpenAll(&cell->drawable.bins[i]->drawable_entries[j]->base_entry, cell->region);
		}
		cell->bins_state = WCS_OPEN;
	}
}

void worldCellCloseClusterBins(WorldCell *cell)
{
	if (cell->drawable.gfx_data)
		wl_state.gfx_cluster_close_tex_swaps(cell->drawable.gfx_data);

	if (cell->draw_model_cluster)
	{
		modelLODWaitForLoad(cell->draw_model_cluster, 0);
		tempModelFree(&cell->draw_model_cluster);
	}
	if (cell->cluster_hogg)
	{
		hogFileDestroy(cell->cluster_hogg, true);
		cell->cluster_hogg = NULL;
	}
}

static void worldCellCloseBins(WorldCell *cell)
{
	if (cell->bins_state == WCS_LOADING_FG)
		worldCellCancelWeldedBinLoad(cell);

	if (cell->bins_state == WCS_OPEN || cell->bins_state == WCS_LOADED)
	{
		WorldCellCullHeader *cell_header;

		if (cell->streaming_mode)
		{
			eaDestroyEx(&cell->drawable.bins, worldCellFreeWeldedBin);
		}
		else
		{
			int i, j;
			for (i = eaSize(&cell->drawable.bins) - 1; i >= 0; --i)
			{
				for (j = eaSize(&cell->drawable.bins[i]->drawable_entries) - 1; j >= 0; --j)
					worldCellEntryCloseAndClearAll(&cell->drawable.bins[i]->drawable_entries[j]->base_entry);
			}
		}

		if (cell_header = worldCellGetHeader(cell))
		{
			SAFE_FREE(cell_header->welded_entry_headers);
			cell_header->welded_entries_inited = false;
		}

		cell->bins_state = WCS_CLOSED;
	}
}

static int worldCellClose(WorldCell *cell, bool is_top_level, bool force, U32 timestamp)
{
	WorldCellCullHeader *cell_header;
	bool ok_to_close = true;
	int i, ret = 0;

	if (!cell)
		return 0;

	// Start loading cluster data for this.
	if(wl_state.gfx_check_cluster_loaded) {
		if(!force && cell->cell_state != WCS_CLOSED) {
			wl_state.gfx_check_cluster_loaded(cell, true);
		}
	}

	if (!force && (cell->cell_state == WCS_OPEN || cell->cell_state == WCS_LOADING_BG || cell->cell_state == WCS_LOADING_FG) && cell->close_request_time < timestamp - 1)
	{
		// only close a cell if it is requested two frames in a row
		cell->close_request_time = timestamp;
		return 0;
	}

	cell->close_request_time = timestamp;

	if (is_top_level)
		worldCellOpenBins(cell);
	else
		worldCellCloseBins(cell);

	if (cell->cell_state == WCS_CLOSED || cell->cell_state == WCS_LOADING_BG || cell->bins_state == WCS_LOADING_BG)
		return 0;

	PERFINFO_AUTO_START_FUNC();

	DEBUG_CELL_PRINTF(cell);

	for (i = 0; i < ARRAY_SIZE(cell->children); ++i)
	{
		ret = worldCellClose(cell->children[i], false, true, timestamp) || ret;
		if (cell->children[i] && (cell->children[i]->cell_state != WCS_CLOSED || cell->children[i]->bins_state != WCS_CLOSED))
			ok_to_close = false;
	}

	if (!ok_to_close)
	{
		PERFINFO_AUTO_STOP();
		return ret;
	}

	if (cell->cell_state == WCS_LOADING_FG)
		worldCellCancelLoad(cell);

	if (cell->streaming_mode)
	{
		eaDestroyEx(&cell->nondrawable_entries, worldCellEntryFree);
		eaDestroyEx(&cell->collision.entries, worldCellEntryFree);
		eaDestroyEx(&cell->drawable.drawable_entries, worldCellEntryFree);
		eaDestroyEx(&cell->drawable.near_fade_entries, worldCellEntryFree);
		eaDestroyEx(&cell->drawable.editor_only_entries, worldCellEntryFree);
	}
	else
	{
		eaForEach(&cell->nondrawable_entries, worldCellEntryCloseAll);
		eaForEach(&cell->collision.entries, worldCellEntryCloseAll);
		eaForEach(&cell->drawable.drawable_entries, worldCellEntryCloseAll);
		eaForEach(&cell->drawable.near_fade_entries, worldCellEntryCloseAll);
		eaForEach(&cell->drawable.editor_only_entries, worldCellEntryCloseAll);
	}

	if (cell_header = worldCellGetHeader(cell))
	{
		SAFE_FREE(cell_header->entry_headers);
		SAFE_FREE(cell_header->editor_only_entry_headers);
		cell_header->entries_inited = false;
		cell_header->is_open = false;
	}

	cell->cell_state = WCS_CLOSED;

	PERFINFO_AUTO_STOP();
	return 1;
}

static int world_cell_open_per_frame_quota = -1;
AUTO_CMD_INT(world_cell_open_per_frame_quota, world_cell_open_per_frame_quota);

static int worldCellOpen(int iPartitionIdx, WorldCell *cell, bool gather_entries, bool just_load, bool open_all, bool new_partition)
{
	WorldCellCullHeader *cell_header;
	int i;
	static U32 last_frame_idx = 0;
	static int opens_this_frame = 0;

	if (last_frame_idx != wl_state.frame_count)
	{
		opens_this_frame = 0;
		last_frame_idx = wl_state.frame_count;
	}

	if (!cell)
		return 0;

	if (!just_load)
		worldCellCloseBins(cell);

	cell->close_request_time = 0;

	if (cell->cell_state == WCS_LOADING_BG)
		return 0;
	if (!new_partition && cell->cell_state == WCS_OPEN)
		return 0;

	PERFINFO_AUTO_START_FUNC();

	DEBUG_CELL_PRINTF(cell);

	if (cell->streaming_mode && cell->cell_state == WCS_CLOSED)
	{
		worldCellQueueLoad(cell, open_all);
		if (!open_all)
		{
			PERFINFO_AUTO_STOP();
			return 0;
		}
	}

	if (cell->cell_state == WCS_LOADING_FG)
	{
		if (opens_this_frame < world_cell_open_per_frame_quota || world_cell_open_per_frame_quota < 0)
		{
			worldCellFinishLoad(cell);
			opens_this_frame++;
		}
		else
		{
			PERFINFO_AUTO_STOP();
			return 0;
		}
	}

	if (!just_load || open_all)
	{
		for (i = eaSize(&cell->nondrawable_entries) - 1; i >= 0; --i)
			worldCellEntryOpen(iPartitionIdx, cell->nondrawable_entries[i], cell->region);
		for (i = eaSize(&cell->collision.entries) - 1; i >= 0; --i)
			worldCellEntryOpen(iPartitionIdx, &cell->collision.entries[i]->base_entry, cell->region);
		for (i = eaSize(&cell->drawable.drawable_entries) - 1; i >= 0; --i)
			worldCellEntryOpen(iPartitionIdx, &cell->drawable.drawable_entries[i]->base_entry, cell->region);
		for (i = eaSize(&cell->drawable.near_fade_entries) - 1; i >= 0; --i)
			worldCellEntryOpen(iPartitionIdx, &cell->drawable.near_fade_entries[i]->base_entry, cell->region);
		for (i = eaSize(&cell->drawable.editor_only_entries) - 1; i >= 0; --i)
			worldCellEntryOpen(iPartitionIdx, &cell->drawable.editor_only_entries[i]->base_entry, cell->region);

		cell->cell_state = WCS_OPEN;

		if (cell_header = worldCellGetHeader(cell))
			cell_header->is_open = true;

		PERFINFO_AUTO_STOP();
		return 1;
	}

	PERFINFO_AUTO_STOP();
	return 0;
}

static void worldCellMarkBoundsDirty(WorldCell *cell)
{
	if (!cell || cell->bounds_dirty)
		return;

	cell->region->world_bounds.needs_update = true;

	while (cell)
	{
		cell->bounds_dirty = 1;
		cell = cell->parent;
	}
}

F32 worldCellCalcDrawableRadius(WorldCell *cell)
{
	F32 minLength, maxLength;

	minLength = distance3(cell->drawable.world_mid, cell->drawable.world_min);
	maxLength = distance3(cell->drawable.world_mid, cell->drawable.world_max);
	return max(minLength, maxLength);
}

void worldCellCopyCellBoundsToHeader(WorldCell *cell, WorldCellCullHeader *header)
{
	header->radius = cell->drawable.radius;
	copyVec3(cell->drawable.world_min, header->world_min);
	copyVec3(cell->drawable.world_max, header->world_max);
	copyVec3(cell->drawable.world_mid, header->world_mid);
}

static void worldCellCopyOrModifyBoundsAsAppropriate(WorldCell *cell, Vec3 minVec, Vec3 maxVec)
{
	if (nearf(cell->drawable.radius,0.0))
	{
		copyVec3(maxVec,cell->drawable.world_max);
		copyVec3(minVec,cell->drawable.world_min);
	}
	else
	{
		MAXVEC3(maxVec,cell->drawable.world_max,cell->drawable.world_max);
		MINVEC3(minVec,cell->drawable.world_min,cell->drawable.world_min);
	}
	cell->drawable.radius = worldCellCalcDrawableRadius(cell);
}

void worldCellSetupDrawableBlockRange(WorldCell *cell, WorldCellCullHeader *header)
{
	if (cell->cluster_hogg && cell->draw_model_cluster)
	{
		Vec3 clusterMin;
		Vec3 clusterMax;
		Vec3 clusterMid;

		worldCellGetBlockRangeCenter(cell,cell->drawable.world_mid);

		addVec3(cell->drawable.world_mid,cell->draw_model_cluster->mid,clusterMid);
		addVec3(clusterMid,cell->draw_model_cluster->min,clusterMin);
		addVec3(clusterMid,cell->draw_model_cluster->max,clusterMax);

		worldCellCopyOrModifyBoundsAsAppropriate(cell, clusterMin, clusterMax);
	}
	else if (cell->vis_dist_level && cell->cluster_related)
	{
		Vec3 clusterMin;
		Vec3 clusterMax;
		int i;
		bool minMaxInit = false;

		for (i = 0; i < ARRAY_SIZE(cell->children); i++)
		{
			if (cell->children[i] && cell->children[i]->drawable.header)
			{
				if (!minMaxInit)
				{
					minMaxInit = true;
					copyVec3(cell->children[i]->drawable.world_max,clusterMax);
					copyVec3(cell->children[i]->drawable.world_min,clusterMin);
				}
				else
				{
					MAXVEC3(clusterMax,cell->children[i]->drawable.world_max,clusterMax);
					MINVEC3(clusterMin,cell->children[i]->drawable.world_min,clusterMin);
				}
			}
		}
		if (minMaxInit)
		{
			worldCellCopyOrModifyBoundsAsAppropriate(cell, clusterMin, clusterMax);
		}
	}

	if (header)
		worldCellCopyCellBoundsToHeader(cell, header);
}

void worldCellUpdateBoundsFromChildren(WorldCell *cell)
{
	int i;
	WorldCellCullHeader *header = worldCellGetHeader(cell);

	if (!cell->vis_dist_level)
		return;

	for (i = 0; i < ARRAY_SIZE(cell->children); i++)
	{
		if (cell->children[i])
		{
			MINVEC3(cell->drawable.world_min,cell->children[i]->drawable.world_min,cell->drawable.world_min);
			MAXVEC3(cell->drawable.world_max,cell->children[i]->drawable.world_max,cell->drawable.world_max);
		}
	}
	cell->drawable.radius = worldCellCalcDrawableRadius(cell);

	if (header)
		worldCellCopyCellBoundsToHeader(cell, header);
}

void worldCellUpdateBounds(WorldCell *cell)
{
	Vec3 world_min, world_max, vis_min, vis_max;
	WorldCellCullHeader *cell_header;
	WorldCell *cell_parent;
	int i, j;

	if (!cell || !cell->bounds_dirty)
		return;

	PERFINFO_AUTO_START_FUNC();

	worldCellRefreshBins(cell);

	setVec3same(cell->drawable.world_min, FLT_MAX);
	setVec3same(cell->drawable.world_max, -FLT_MAX);
	setVec3same(cell->collision.world_min, FLT_MAX);
	setVec3same(cell->collision.world_max, -FLT_MAX);
	setVec3same(cell->bounds.world_min, FLT_MAX);
	setVec3same(cell->bounds.world_max, -FLT_MAX);
	cell->is_empty = 0;
	cell->no_drawables = 0;
	cell->no_collision = 0;
	cell->drawable.no_fog = 0;

	for (i = 0; i < ARRAY_SIZE(cell->children); ++i)
	{
		WorldCell *child = cell->children[i];
		if (child)
		{
			worldCellUpdateBounds(child);
			if (!child->no_drawables)
			{
				vec3RunningMin(child->drawable.world_min, cell->drawable.world_min);
				vec3RunningMax(child->drawable.world_max, cell->drawable.world_max);
				cell->drawable.no_fog = cell->drawable.no_fog || child->drawable.no_fog;
			}
			if (!child->no_collision)
			{
				vec3RunningMin(child->collision.world_min, cell->collision.world_min);
				vec3RunningMax(child->collision.world_max, cell->collision.world_max);
			}
			if (!child->is_empty)
			{
				vec3RunningMin(child->bounds.world_min, cell->bounds.world_min);
				vec3RunningMax(child->bounds.world_max, cell->bounds.world_max);
			}
		}
	}

	for (i = eaSize(&cell->drawable.drawable_entries)-1; i >= 0; --i)
	{
		WorldDrawableEntry *draw_entry = cell->drawable.drawable_entries[i];
		WorldCellEntry *entry = &draw_entry->base_entry;
		
		mulBoundsAA(entry->shared_bounds->local_min, entry->shared_bounds->local_max, entry->bounds.world_matrix, world_min, world_max);

		vec3RunningMin(world_min, cell->drawable.world_min);
		vec3RunningMax(world_max, cell->drawable.world_max);

		// the bounds.world_min and bounds.world_max vecs are used to determine if a cell should be opened 
		// (ie if its entries are in vis distance range), so use the fade mid and fade radius
		addVec3same(draw_entry->world_fade_mid, -draw_entry->world_fade_radius, vis_min);
		addVec3same(draw_entry->world_fade_mid, draw_entry->world_fade_radius, vis_max);
		vec3RunningMin(vis_min, cell->bounds.world_min);
		vec3RunningMax(vis_max, cell->bounds.world_max);

		cell->drawable.no_fog = cell->drawable.no_fog || SAFE_MEMBER(draw_entry->draw_list, no_fog);
	}

	for (i = eaSize(&cell->drawable.editor_only_entries)-1; i >= 0; --i)
	{
		WorldDrawableEntry *draw_entry = cell->drawable.editor_only_entries[i];
		WorldCellEntry *entry = &draw_entry->base_entry;

		mulBoundsAA(entry->shared_bounds->local_min, entry->shared_bounds->local_max, entry->bounds.world_matrix, world_min, world_max);

		vec3RunningMin(world_min, cell->drawable.world_min);
		vec3RunningMax(world_max, cell->drawable.world_max);

		// the bounds.world_min and bounds.world_max vecs are used to determine if a cell should be opened 
		// (ie if its entries are in vis distance range), so use the fade mid and fade radius
		addVec3same(draw_entry->world_fade_mid, -draw_entry->world_fade_radius, vis_min);
		addVec3same(draw_entry->world_fade_mid, draw_entry->world_fade_radius, vis_max);
		vec3RunningMin(vis_min, cell->bounds.world_min);
		vec3RunningMax(vis_max, cell->bounds.world_max);

		cell->drawable.no_fog = cell->drawable.no_fog || SAFE_MEMBER(draw_entry->draw_list, no_fog);
	}

	for (cell_parent = cell; cell_parent; cell_parent = cell_parent->parent)
	{
		for (i = eaSize(&cell_parent->drawable.near_fade_entries)-1; i >= 0; --i)
		{
			WorldDrawableEntry *draw_entry = cell_parent->drawable.near_fade_entries[i];
			WorldCellEntry *entry = &draw_entry->base_entry;

			// check if entry affects down to the current cell
			if (!worldNearFadeEntryAffectsCell(draw_entry, cell))
				continue;

			mulBoundsAA(entry->shared_bounds->local_min, entry->shared_bounds->local_max, entry->bounds.world_matrix, world_min, world_max);

			vec3RunningMin(world_min, cell->drawable.world_min);
			vec3RunningMax(world_max, cell->drawable.world_max);

			// the bounds.world_min and bounds.world_max vecs are used to determine if a cell should be opened 
			// (ie if its entries are in vis distance range), so use the fade mid and fade radius
			addVec3same(draw_entry->world_fade_mid, -draw_entry->world_fade_radius, vis_min);
			addVec3same(draw_entry->world_fade_mid, draw_entry->world_fade_radius, vis_max);
			vec3RunningMin(vis_min, cell->bounds.world_min);
			vec3RunningMax(vis_max, cell->bounds.world_max);

			cell->drawable.no_fog = cell->drawable.no_fog || SAFE_MEMBER(draw_entry->draw_list, no_fog);
		}
	}

	for (j = eaSize(&cell->drawable.bins)-1; j >= 0; --j)
	{
		WorldCellWeldedBin *bin = cell->drawable.bins[j];
		
		for (i = eaSize(&bin->drawable_entries)-1; i >= 0; --i)
		{
			WorldDrawableEntry *draw_entry = bin->drawable_entries[i];
			WorldCellEntry *entry = &draw_entry->base_entry;

			mulBoundsAA(entry->shared_bounds->local_min, entry->shared_bounds->local_max, entry->bounds.world_matrix, world_min, world_max);

			vec3RunningMin(world_min, cell->drawable.world_min);
			vec3RunningMax(world_max, cell->drawable.world_max);

			// the bounds.world_min and bounds.world_max vecs are used to determine if a cell should be opened 
			// (ie if its entries are in vis distance range), so use the fade mid and fade radius
			addVec3same(draw_entry->world_fade_mid, -draw_entry->world_fade_radius, vis_min);
			addVec3same(draw_entry->world_fade_mid, draw_entry->world_fade_radius, vis_max);
			vec3RunningMin(vis_min, cell->bounds.world_min);
			vec3RunningMax(vis_max, cell->bounds.world_max);

			cell->drawable.no_fog = cell->drawable.no_fog || SAFE_MEMBER(draw_entry->draw_list, no_fog);
		}
	}

	for (i = eaSize(&cell->collision.entries)-1; i >= 0; --i)
	{
		WorldCellEntry *entry = &cell->collision.entries[i]->base_entry;

		mulBoundsAA(entry->shared_bounds->local_min, entry->shared_bounds->local_max, entry->bounds.world_matrix, world_min, world_max);

		vec3RunningMin(world_min, cell->collision.world_min);
		vec3RunningMax(world_max, cell->collision.world_max);
		vec3RunningMin(world_min, cell->bounds.world_min);
		vec3RunningMax(world_max, cell->bounds.world_max);
	}

	for (i = eaSize(&cell->nondrawable_entries)-1; i >= 0; --i)
	{
		WorldCellEntry *entry = cell->nondrawable_entries[i];

		mulBoundsAA(entry->shared_bounds->local_min, entry->shared_bounds->local_max, entry->bounds.world_matrix, world_min, world_max);

		vec3RunningMin(world_min, cell->bounds.world_min);
		vec3RunningMax(world_max, cell->bounds.world_max);
	}

	if (cell->drawable.world_max[0] < cell->drawable.world_min[0])
	{
		setVec3same(cell->drawable.world_min, 0);
		setVec3same(cell->drawable.world_max, 0);
		cell->no_drawables = 1;
		cell->drawable.no_fog = 0;
	}

	if (cell->collision.world_max[0] < cell->collision.world_min[0])
	{
		setVec3same(cell->collision.world_min, 0);
		setVec3same(cell->collision.world_max, 0);
		cell->no_collision = 1;
	}

	if (cell->bounds.world_max[0] < cell->bounds.world_min[0])
	{
		setVec3same(cell->bounds.world_min, 0);
		setVec3same(cell->bounds.world_max, 0);
		cell->is_empty = 1;
	}

	cell->drawable.radius = boxCalcMid(cell->drawable.world_min, cell->drawable.world_max, cell->drawable.world_mid);
	cell->collision.radius = boxCalcMid(cell->collision.world_min, cell->collision.world_max, cell->collision.world_mid);

	cell->bounds_dirty = 0;

	{
		IVec3 grid_pos;
		worldPosToGridPos(cell->bounds.world_min, grid_pos, CELL_BLOCK_SIZE);
		if (!gridPosInRange(grid_pos, &cell->cell_block_range))
			cell->cell_bounds_invalid = 1;
		else
		{
			worldPosToGridPos(cell->bounds.world_max, grid_pos, CELL_BLOCK_SIZE);
			if (!gridPosInRange(grid_pos, &cell->cell_block_range))
				cell->cell_bounds_invalid = 1;
		}
	}

	if (cell_header = worldCellGetHeader(cell))
	{
		SAFE_FREE(cell_header->entry_headers);
		SAFE_FREE(cell_header->welded_entry_headers);
		SAFE_FREE(cell_header->editor_only_entry_headers);
		cell_header->entries_inited = false;
		cell_header->welded_entries_inited = false;

		worldCellSetupDrawableBlockRange(cell, cell_header);

		cell_header->no_fog = cell->drawable.no_fog;
	}

	PERFINFO_AUTO_STOP();
}

bool worldNearFadeEntryAffectsCell(WorldDrawableEntry *entry, WorldCell *cell)
{
	U32 near_lod_near_vis_dist_level = worldCellGetVisDistLevelNear(cell->region, entry->base_entry.shared_bounds->near_lod_near_dist, NULL);
	IVec3 grid_pos;

	if (cell->vis_dist_level < near_lod_near_vis_dist_level)
		return false;

	worldPosToGridPos(entry->world_fade_mid, grid_pos, CELL_BLOCK_SIZE);
	return gridPosInRange(grid_pos, &cell->cell_block_range);
}

WorldCell *worldCellGetChildForGridPos(WorldCell *cell, const IVec3 grid_pos, int *child_idx, bool create_nonexistent)
{
	BlockRange child_range;
	IVec3 size;
	int i;

	rangeSize(&cell->cell_block_range, size);
	assert(size[0] == size[1]);
	assert(size[0] == size[2]);

	size[0] = size[0] >> 1;
	size[1] = size[1] >> 1;
	size[2] = size[2] >> 1;

	if (size[0] > 0)
	{
		// find appropriate child cell
		for (i = 0; i < ARRAY_SIZE(cell->children); ++i)
		{
			worldCellSetupChildCellRange(cell, &child_range, size, i);

			if (gridPosInRange(grid_pos, &child_range))
			{
				if (!cell->children[i] && create_nonexistent)
				{
					cell->children[i] = worldCellCreate(cell->region, cell, &child_range);
					worldCellFreeCullHeaders(cell->region->world_cell_headers); // added a cell, must recreate the headers
					cell->region->world_cell_headers = NULL;
				}
				if (child_idx)
					*child_idx = i;
				return cell->children[i];
			}
		}
	}

	if (child_idx)
		*child_idx = 0;
	return NULL;
}

WorldCell *worldCellFindLeafMostCellForEntry(WorldCell *cell, const WorldDrawableEntry *entry)
{
	WorldCell *relocated_cell = cell, *next_cell = NULL;
	IVec3 entry_pos;

	if (!cell)
		return NULL;

	worldCellClusterEntryGetClusterPos(entry,entry_pos);

	do
	{
		int childCellIndex = 0;
		for (childCellIndex = 0; childCellIndex < ARRAY_SIZE(cell->children); ++childCellIndex)
		{
			next_cell = relocated_cell->children[childCellIndex];

			if (next_cell && 
				entry_pos[0] >= next_cell->cell_block_range.min_block[0] &&
				entry_pos[1] >= next_cell->cell_block_range.min_block[1] &&
				entry_pos[2] >= next_cell->cell_block_range.min_block[2] &&
				entry_pos[0] <= next_cell->cell_block_range.max_block[0] &&
				entry_pos[1] <= next_cell->cell_block_range.max_block[1] &&
				entry_pos[2] <= next_cell->cell_block_range.max_block[2])
			{
				relocated_cell = next_cell;
				break;
			}
			else
				next_cell = NULL;
		}
	}
	while (next_cell);

	if (relocated_cell->vis_dist_level)
		relocated_cell = NULL;

	return relocated_cell;
}

void worldCellGetBlockRangeCenter(const WorldCell *cell, Vec3 cell_center)
{
	addVec3(cell->cell_block_range.min_block, cell->cell_block_range.max_block, cell_center);
	addVec3same(cell_center, 1, cell_center);
	scaleVec3(cell_center, CELL_BLOCK_SIZE * 0.5f, cell_center);
}

void worldAddCellEntry(WorldRegion *region, WorldCellEntry *entry)
{
	WorldCellEntryData *entry_data = worldCellEntryGetData(entry);
	U32 near_lod_near_vis_dist_level, far_lod_near_vis_dist_level, far_lod_far_vis_dist_level;
	WorldCell *cell;
	IVec3 grid_pos;
	void *bin_key = NULL;
	WorldCellCullHeader *root_headers;
	F32 radius, *world_mid;
	bool editor_only = false, welded = false;

	assert(!entry_data->cell);
	assert(!entry_data->bins);
	assert(entry->shared_bounds->far_lod_far_dist >= entry->shared_bounds->far_lod_near_dist);
	assert(entry->shared_bounds->far_lod_near_dist >= entry->shared_bounds->near_lod_near_dist);
	assert(entry->shared_bounds->ref_count > 0);

	if (entry->type == WCENT_MODEL)
	{
		bin_key = ((WorldDrawableEntry *)entry)->draw_list;

#if 0 // debugging code
		if (((WorldDrawableEntry *)entry)->draw_list && ((WorldDrawableEntry *)entry)->draw_list->drawable_lods &&
			((WorldDrawableEntry *)entry)->draw_list->drawable_lods[0].subobjects && ((WorldDrawableEntry *)entry)->draw_list->drawable_lods[0].subobjects[0]->model &&
			stricmp(((WorldDrawableEntry *)entry)->draw_list->drawable_lods[0].subobjects[0]->model->model->name, "city_center_vista_segment_02")==0)
		{
			((WorldDrawableEntry *)entry)->debug_me = 1;
		}
#endif
	}

	if (entry->type > WCENT_BEGIN_DRAWABLES)
	{
		radius = ((WorldDrawableEntry *)entry)->world_fade_radius;
		world_mid = ((WorldDrawableEntry *)entry)->world_fade_mid;
		editor_only = ((WorldDrawableEntry *)entry)->editor_only;
	}
	else
	{
		radius = entry->shared_bounds->radius;
		world_mid = entry->bounds.world_mid;
	}

	near_lod_near_vis_dist_level = worldCellGetVisDistLevelNear(region, entry->shared_bounds->near_lod_near_dist, entry);
	if (bin_disable_near_bypass || entry->type < WCENT_BEGIN_DRAWABLES || entry_data->parent_entry || editor_only)
		near_lod_near_vis_dist_level = 0;

	far_lod_near_vis_dist_level = worldCellGetVisDistLevelFar(region, entry->shared_bounds->far_lod_near_dist, radius, entry->type, entry);
	if (entry->shared_bounds->far_lod_near_dist == entry->shared_bounds->far_lod_far_dist)
	{
		bin_key = NULL;
		far_lod_far_vis_dist_level = far_lod_near_vis_dist_level;
	}
	else
	{
		// don't weld entries at cells below min_weld_vis_dist_level
		U32 min_weld_vis_dist_level = entry->shared_bounds->radius < MIN_WELD_VIS_DIST_RADIUS ? MIN_WELD_VIS_DIST_LEVEL1 : MIN_WELD_VIS_DIST_LEVEL2;
		MAX1(far_lod_near_vis_dist_level, min_weld_vis_dist_level);

		far_lod_far_vis_dist_level = worldCellGetVisDistLevelFar(region, entry->shared_bounds->far_lod_far_dist, radius, entry->type, entry);

		if (bin_disable_far_welding || !bin_key || far_lod_near_vis_dist_level >= far_lod_far_vis_dist_level || entry_data->parent_entry || editor_only)
		{
			bin_key = NULL;
			far_lod_near_vis_dist_level = far_lod_far_vis_dist_level;
		}

		// Don't weld anything that has skinning information. We can't use them as
		// instanced models.
		if(entry->type > WCENT_BEGIN_DRAWABLES) {
			const Model *pModel = worldDrawableEntryGetModel(
				(WorldDrawableEntry*)entry, NULL, NULL, NULL, NULL);
			if(pModel && pModel->header && pModel->header->bone_names) {
				bin_key = NULL;
				far_lod_near_vis_dist_level = far_lod_far_vis_dist_level;
			}
		}
	}

	if (bin_key && near_lod_near_vis_dist_level > 0 && far_lod_far_vis_dist_level > near_lod_near_vis_dist_level)
	{
		if (entry->shared_bounds->far_lod_near_dist == entry->shared_bounds->near_lod_near_dist && !bin_disable_near_fade_welding)
		{
			// weld near fade entries
			far_lod_near_vis_dist_level = near_lod_near_vis_dist_level;
		}
	}

	worldPosToGridPos(world_mid, grid_pos, CELL_BLOCK_SIZE);

	if (!region->root_world_cell || !gridPosInRange(grid_pos, &region->root_world_cell->cell_block_range) || region->root_world_cell->streaming_mode)
	{
		if (!region->temp_world_cell)
		{
			BlockRange block_range;
			copyVec3(grid_pos, block_range.min_block);
			copyVec3(grid_pos, block_range.max_block);
			region->temp_world_cell = worldCellCreate(region, NULL, &block_range);
			region->temp_world_cell->cell_state = WCS_OPEN;
			region->temp_world_cell->is_empty = 0;
			region->temp_world_cell->no_drawables = 0;
			worldCellFreeCullHeaders(region->temp_world_cell_headers);
			region->temp_world_cell_headers = NULL;
		}

		cell = region->temp_world_cell;

		root_headers = region->temp_world_cell_headers;
	}
	else
	{
		for (cell = region->root_world_cell; 
			 cell->vis_dist_level >= far_lod_near_vis_dist_level || near_lod_near_vis_dist_level > 0 && cell->vis_dist_level >= near_lod_near_vis_dist_level; 
			 )
		{
			if (cell->vis_dist_level <= far_lod_far_vis_dist_level)
			{
				if (bin_key && cell->vis_dist_level >= far_lod_near_vis_dist_level)
				{
					WorldCellWeldedBin *bin = NULL;
					int i;

					for (i = eaSize(&cell->drawable.bins) - 1; i >= 0; --i)
					{
						if (cell->drawable.bins[i]->bin_key == bin_key && cell->drawable.bins[i]->src_type == entry->type)
						{
							bin = cell->drawable.bins[i];
							break;
						}
					}

					if (!bin)
					{
						bin = calloc(1, sizeof(WorldCellWeldedBin));
						bin->cell = cell;
						bin->bin_key = bin_key;
						bin->src_type = entry->type;
						eaPush(&cell->drawable.bins, bin);
					}

					eaPush(&bin->src_entries, (WorldDrawableEntry*)entry);
					eaPush(&entry_data->bins, bin);
					bin->is_dirty = 1;

					cell->bins_dirty = 1;

					welded = true;
				}
				else if (near_lod_near_vis_dist_level > 0)
				{
					break;
				}
			}

			if (cell->vis_dist_level == far_lod_near_vis_dist_level && near_lod_near_vis_dist_level == 0)
				break;

			if (cell->vis_dist_level == 0)
				break;

			cell = worldCellGetChildForGridPos(cell, grid_pos, NULL, true);
			assert(cell);
		}

		root_headers = region->world_cell_headers;
	}

	if (entry->type == WCENT_COLLISION)
	{
		eaPush(&cell->collision.entries, (WorldCollisionEntry*)entry);
	}
	else if (entry->type < WCENT_BEGIN_DRAWABLES)
	{
		eaPush(&cell->nondrawable_entries, entry);
	}
	else
	{
		WorldDrawableEntry *draw = (WorldDrawableEntry*)entry;

		if (!cell->drawable.header)
		{
			if (cell == region->temp_world_cell)
			{
				worldCellFreeCullHeaders(region->temp_world_cell_headers);
				region->temp_world_cell_headers = NULL;
			}
			else
			{
				worldCellFreeCullHeaders(region->world_cell_headers);
				region->world_cell_headers = NULL;
			}
		}

		if (near_lod_near_vis_dist_level > 0 && (cell->vis_dist_level >= near_lod_near_vis_dist_level || welded && far_lod_near_vis_dist_level == near_lod_near_vis_dist_level))
		{
			WorldCell *child_cell;

			for (child_cell = cell; 
				child_cell && child_cell->vis_dist_level >= near_lod_near_vis_dist_level; 
				child_cell = worldCellGetChildForGridPos(child_cell, grid_pos, NULL, true))
			{
				WorldCellCullHeader *cell_header = root_headers ? child_cell->drawable.header : NULL;

				// mark child cells as having dirty bounds
				child_cell->bounds_dirty = 1;

				if (!child_cell->drawable.header)
				{
					if (child_cell == region->temp_world_cell)
					{
						worldCellFreeCullHeaders(region->temp_world_cell_headers);
						region->temp_world_cell_headers = NULL;
					}
					else
					{
						worldCellFreeCullHeaders(region->world_cell_headers);
						region->world_cell_headers = NULL;
					}
				}
				else if (cell_header)
				{
					SAFE_FREE(cell_header->welded_entry_headers); // added an entry, must recreate the headers
					cell_header->welded_entries_inited = false;
				}
			}

			// move it up one cell, since these need to be loaded when the desired cell is CLOSED
			if (cell->parent)
				cell = cell->parent;

			eaPush(&cell->drawable.near_fade_entries, draw);
		}
		else
		{
			WorldCellCullHeader *cell_header = root_headers ? cell->drawable.header : NULL;

			if (draw->editor_only)
			{
				eaPush(&cell->drawable.editor_only_entries, draw);
				if (cell_header)
				{
					SAFE_FREE(cell_header->editor_only_entry_headers); // added an entry, must recreate the headers
					cell_header->entries_inited = false;
				}
			}
			else
			{
				eaPush(&cell->drawable.drawable_entries, draw);

				if (cell_header)
				{
					SAFE_FREE(cell_header->entry_headers); // added an entry, must recreate the headers
					cell_header->entries_inited = false;
				}
			}
		}
	}

	assert(!cell->streaming_mode);

	entry_data->cell = cell;
	entry->owned = 1;
	entry->streaming_mode = cell->streaming_mode;
	worldCellMarkBoundsDirty(cell);

	if (cell->cell_state == WCS_OPEN)
		worldCellEntryOpenAll(entry, cell->region);

	++world_cell_entry_count;
}

void worldAddCellEntryToCell(WorldCell *cell, WorldCellEntry *entry, WorldCellEntryData *entry_data, bool is_near_fade)
{
	assert(entry->shared_bounds->ref_count > 0);

	assert(cell != cell->region->temp_world_cell);

	if (entry->type == WCENT_COLLISION)
	{
		eaPush(&cell->collision.entries, (WorldCollisionEntry*)entry);
	}
	else if (entry->type < WCENT_BEGIN_DRAWABLES)
	{
		eaPush(&cell->nondrawable_entries, entry);
	}
	else
	{
		WorldDrawableEntry *draw = (WorldDrawableEntry*)entry;

		if (!cell->drawable.header)
		{
			worldCellFreeCullHeaders(cell->region->world_cell_headers);
			cell->region->world_cell_headers = NULL;
		}

		if (is_near_fade)
		{
			U32 near_lod_near_vis_dist_level = worldCellGetVisDistLevelNear(cell->region, entry->shared_bounds->near_lod_near_dist, entry);
			WorldCell *child_cell;
			IVec3 grid_pos;

			worldPosToGridPos(draw->world_fade_mid, grid_pos, CELL_BLOCK_SIZE);

			for (child_cell = cell; 
				child_cell && child_cell->vis_dist_level >= near_lod_near_vis_dist_level; 
				child_cell = worldCellGetChildForGridPos(child_cell, grid_pos, NULL, false))
			{
				WorldCellCullHeader *cell_header = cell->region->world_cell_headers ? child_cell->drawable.header : NULL;

				if (!child_cell->drawable.header)
				{
					worldCellFreeCullHeaders(cell->region->world_cell_headers);
					cell->region->world_cell_headers = NULL;
				}
				else if (cell_header)
				{
					SAFE_FREE(cell_header->welded_entry_headers); // added an entry, must recreate the headers
					cell_header->welded_entries_inited = false;
				}
			}

			eaPush(&cell->drawable.near_fade_entries, draw);
		}
		else
		{
			WorldCellCullHeader *cell_header = cell->region->world_cell_headers ? cell->drawable.header : NULL;

			if (draw->editor_only)
			{
				eaPush(&cell->drawable.editor_only_entries, draw);
				if (cell_header)
				{
					SAFE_FREE(cell_header->editor_only_entry_headers); // added an entry, must recreate the headers
					cell_header->entries_inited = false;
				}
			}
			else
			{
				eaPush(&cell->drawable.drawable_entries, draw);

				if (cell_header)
				{
					SAFE_FREE(cell_header->entry_headers); // added an entry, must recreate the headers
					cell_header->entries_inited = false;
				}
			}
		}
	}

	entry_data->cell = cell;
	entry->owned = 1;
	entry->streaming_mode = cell->streaming_mode;

	if (cell->cell_state == WCS_OPEN)
		worldCellEntryOpenAll(entry, cell->region);

	++world_cell_entry_count;
}

WorldCell *worldRemoveCellEntryEx(WorldCellEntry *entry, bool close, bool remove_from_bins)
{
	WorldCellEntryData *entry_data = worldCellEntryGetData(entry);
	WorldCell *cell = entry_data->cell;
	int i;

	if (close)
		worldCellEntryCloseAndClearAll(entry);

	if (!cell)
		return NULL;

	if (!cell->streaming_mode)
	{
		if (remove_from_bins)
		{
			// remove from bins
			for (i = 0; i < eaSize(&entry_data->bins); ++i)
			{
				WorldCellWeldedBin *bin = entry_data->bins[i];
				eaFindAndRemoveFast(&bin->src_entries, (WorldDrawableEntry*)entry);
				bin->is_dirty = 1;
				bin->cell->bins_dirty = 1;
			}
			eaDestroy(&entry_data->bins);
		}

		if (entry->type == WCENT_COLLISION)
		{
			eaFindAndRemoveFast(&cell->collision.entries, (WorldCollisionEntry*)entry);
		}
		else if (entry->type < WCENT_BEGIN_DRAWABLES)
		{
			eaFindAndRemoveFast(&cell->nondrawable_entries, entry);
		}
		else
		{
			U32 near_lod_near_vis_dist_level = worldCellGetVisDistLevelNear(cell->region, entry->shared_bounds->near_lod_near_dist, entry);
			WorldDrawableEntry *draw = (WorldDrawableEntry*)entry;

			if (bin_disable_near_bypass || entry_data->parent_entry || draw->editor_only)
				near_lod_near_vis_dist_level = 0;

			if (near_lod_near_vis_dist_level > 0)
			{
				WorldCell *child_cell;
				IVec3 grid_pos;

				worldPosToGridPos(draw->world_fade_mid, grid_pos, CELL_BLOCK_SIZE);

				for (child_cell = cell; 
					child_cell && child_cell->vis_dist_level >= near_lod_near_vis_dist_level; 
					child_cell = worldCellGetChildForGridPos(child_cell, grid_pos, NULL, false))
				{
					WorldCellCullHeader *cell_header = NULL;

					child_cell->bounds_dirty = 1;

					if ((child_cell == cell->region->temp_world_cell) ? child_cell->region->temp_world_cell_headers : child_cell->region->world_cell_headers)
						cell_header = child_cell->drawable.header;

					if (cell_header)
					{
						SAFE_FREE(cell_header->welded_entry_headers); // removed an entry, must recreate the headers
						cell_header->welded_entries_inited = false;
					}
				}

				eaFindAndRemoveFast(&cell->drawable.near_fade_entries, draw);
			}
			else
			{
				WorldCellCullHeader *cell_header = NULL;

				if ((cell == cell->region->temp_world_cell) ? cell->region->temp_world_cell_headers : cell->region->world_cell_headers)
					cell_header = cell->drawable.header;

				if (draw->editor_only)
				{
					eaFindAndRemoveFast(&cell->drawable.editor_only_entries, draw);
					if (cell_header)
					{
						SAFE_FREE(cell_header->editor_only_entry_headers); // removed an entry, must recreate the headers
						cell_header->entries_inited = false;
					}
				}
				else
				{
					eaFindAndRemoveFast(&cell->drawable.drawable_entries, draw);

					if (cell_header)
					{
						SAFE_FREE(cell_header->entry_headers); // removed an entry, must recreate the headers
						cell_header->entries_inited = false;
					}
				}
			}
		}

		worldCellMarkBoundsDirty(cell);
	}

	entry_data->cell = NULL;
	entry->owned = 0;

	--world_cell_entry_count;

	return cell;
}

// returns 1 if things have changed
static int worldCellOpenAllForCameraPosInternal(int iPartitionIdx, WorldCell *cell, const F32 *camera_positions, int camera_position_count, bool open_all, bool new_partition, U32 timestamp)
{
	static int min_vis_dist_level = -1;
	bool open_cell = false, load_cell = false;
	F32 cell_dist_sqrd;
	int i, changed = 0;

	if (!cell)
		return 0;

	if (min_vis_dist_level < 0)
		min_vis_dist_level = log2(GRID_BLOCK_SIZE / CELL_BLOCK_SIZE);

	// CD: remove once new collision code is in place
	if (cell->vis_dist_level < min_vis_dist_level)
		open_all = true;

	ANALYSIS_ASSUME(cell->vis_dist_level < ARRAY_SIZE(open_dist_sqrd_table));

	cell_dist_sqrd = FLT_MAX;
	for (i = 0; i < camera_position_count; ++i)
	{
		F32 dist_sqrd = distanceToBoxSquared(cell->bounds.world_min, cell->bounds.world_max, &camera_positions[i*3]);
		MIN1F(cell_dist_sqrd, dist_sqrd);
	}

	if (cell->is_empty)
		open_cell = false;
	else if (cell_dist_sqrd <= open_dist_sqrd_table[cell->vis_dist_level])
		open_cell = true;
	else if (cell_dist_sqrd >= close_dist_sqrd_table[cell->vis_dist_level])
		open_cell = false;
	else if (cell->streaming_mode && cell->cell_state != WCS_OPEN && cell_dist_sqrd <= load_dist_sqrd_table[cell->vis_dist_level])
		load_cell = true;
	else if (cell->cell_state == WCS_LOADING_BG || cell->cell_state == WCS_LOADING_FG || cell->cell_state == WCS_LOADED)
		load_cell = true; // maintain current state
	else
		open_cell = cell->cell_state == WCS_OPEN; // maintain current state

	if (open_cell || load_cell || open_all)
	{
		changed |= worldCellOpen(iPartitionIdx, cell, true, load_cell, open_all, new_partition);

		if (!cell->no_drawables && cell_dist_sqrd < dynWindGetSampleGridExtentsSqrd())
		{
			dynWindUpdateCurrentWindParamsForWorldCell(cell, camera_positions, camera_position_count);
		}

		for (i = 0; i < ARRAY_SIZE(cell->children); ++i)
			changed |= worldCellOpenAllForCameraPosInternal(iPartitionIdx, cell->children[i], camera_positions, camera_position_count, open_all, new_partition, timestamp);
	}
	else if (!g_bNoCloseCellsDueToTempCameraPosition)
	{
		changed |= worldCellClose(cell, true, false, timestamp);

		// if a bin is dirty, refresh it
		worldCellRefreshBins(cell);
	}

	return changed;
}

void worldCloseCellOnPartition(int iPartitionIdx, WorldCell *cell)
{
	// Ignore hard closed cells
	if (cell && cell->cell_state == WCS_OPEN)
	{
		int i;

		// Iterate entries within cells
		for (i = eaSize(&cell->nondrawable_entries) - 1; i >= 0; --i)
			worldCellEntryCloseAndClear(iPartitionIdx, cell->nondrawable_entries[i]);
		for (i = eaSize(&cell->collision.entries) - 1; i >= 0; --i)
			worldCellEntryCloseAndClear(iPartitionIdx, &cell->collision.entries[i]->base_entry);
		for (i = eaSize(&cell->drawable.drawable_entries) - 1; i >= 0; --i)
			worldCellEntryCloseAndClear(iPartitionIdx, &cell->drawable.drawable_entries[i]->base_entry);
		for (i = eaSize(&cell->drawable.near_fade_entries) - 1; i >= 0; --i)
			worldCellEntryCloseAndClear(iPartitionIdx, &cell->drawable.near_fade_entries[i]->base_entry);
		for (i = eaSize(&cell->drawable.editor_only_entries) - 1; i >= 0; --i)
			worldCellEntryCloseAndClear(iPartitionIdx, &cell->drawable.editor_only_entries[i]->base_entry);

		// Recurse into child cells
		for (i = 0; i < ARRAY_SIZE(cell->children); ++i) {
			worldCloseCellOnPartition(iPartitionIdx, cell->children[i]);
		}
	}
}

void worldCellCloseForRegion(WorldRegion *region)
{
	int tries = 0;
	if (!region || !region->root_world_cell)
		return;
	while (region->root_world_cell->cell_state != WCS_CLOSED || region->root_world_cell->bins_state != WCS_CLOSED)
	{
		worldCellClose(region->root_world_cell, false, true, 0);
		if (tries)
			Sleep(5); // owned by loading thread, wait
		tries++;
	}
}

// Closes all world cells, causing them to get reopened.
AUTO_COMMAND ACMD_CATEGORY(Debug);
void worldCellCloseAll(void)
{
	worldCloseAllRegionsExcept(NULL);
}

void worldCellGetDrawableEntries(WorldCell *cell, WorldDrawableEntry ***drawable_entries)
{
	if (!cell)
		return;

	eaPushEArray(drawable_entries, &cell->drawable.drawable_entries);
	eaPushEArray(drawable_entries, &cell->drawable.near_fade_entries);
	eaPushEArray(drawable_entries, &cell->drawable.editor_only_entries);
}

WorldCell *worldCellGetParent(WorldCell *cell)
{
	if (cell)
		return cell->parent;
	return NULL;
}

static void loadCellDataRecursive(WorldCell *cell)
{
	int i;

	// don't touch the data if it is in the middle of loading
	if (cell->cell_state != WCS_LOADING_BG && cell->cell_state != WCS_LOADING_FG)
	{
		worldCellForceLoad(cell);
		worldCellForceWeldedBinLoad(cell);
	}

	for (i = 0; i < ARRAY_SIZE(cell->children); ++i)
	{
		if (cell->children[i])
			loadCellDataRecursive(cell->children[i]);
	}
}

int worldGridOpenAllCellsForCameraPosEx(int iPartitionIdx, WorldRegion *region, const F32 *camera_positions, int camera_position_count, 
									   const BlockRange **terrain_editor_blocks, bool load_all, bool new_partition, bool world_only,
									   bool draw_high_detail, bool draw_high_fill_detail, U32 timestamp, F32 cell_scale)
{
	F32 scaled_load_dist = wl_state.world_cell_load_scale * LOAD_DIST_DELTA;
	F32 scaled_close_dist = wl_state.world_cell_load_scale * CLOSE_DIST_DELTA;
	int i, size, changed;

	PERFINFO_AUTO_START_FUNC_PIX();

	if (!region->preloaded_cell_data && region->root_world_cell && 
		region->root_world_cell->streaming_mode && wl_state.keep_cell_data_loaded)
	{
		// this causes a synchronous stall
		loadCellDataRecursive(region->root_world_cell);
		region->preloaded_cell_data = true;
	}

	for (i = 0, size = 1; i < ARRAY_SIZE(open_dist_sqrd_table); ++i, size <<= 1)
	{
		F32 scaled_cell_size = wl_state.lod_scale * CELL_BLOCK_SIZE * size * cell_scale;
		open_dist_sqrd_table[i] = SQR(scaled_cell_size);
		load_dist_sqrd_table[i] = SQR(scaled_cell_size + scaled_load_dist);
		close_dist_sqrd_table[i] = SQR(scaled_cell_size + scaled_close_dist);
	}

	load_all = load_all || wlIsServer();

	wl_state.draw_high_detail = draw_high_detail;
	wl_state.draw_high_fill_detail = draw_high_fill_detail;

	region->last_used_timestamp = timestamp;
	worldCellUpdateBounds(region->root_world_cell);
	PERFINFO_AUTO_START("worldCellOpenAllForCameraPosInternal - outer", 1);
	changed = worldCellOpenAllForCameraPosInternal(iPartitionIdx, region->root_world_cell, camera_positions, camera_position_count, load_all, new_partition, timestamp);
	PERFINFO_AUTO_STOP();
	region->world_bounds.needs_update = false;

	if (region->terrain_bounds.needs_update)
	{
		ZoneMap *zmap = worldGetActiveMap();
		ZoneMapLayer **region_layers = NULL;

		for (i = 0; i < eaSize(&zmap->layers); ++i)
		{
			ZoneMapLayer *layer = zmap->layers[i];
			if (layer && !layer->scratch && layerGetWorldRegion(layer) == region)
				eaPush(&region_layers, layer);
		}

		setVec3same(region->terrain_bounds.world_min, FLT_MAX);
		setVec3same(region->terrain_bounds.world_max, -FLT_MAX);

		for (i = 0; i < eaSize(&region_layers); i++)
		{
			ZoneMapLayer *layer = region_layers[i];
			Vec3 layer_min, layer_max;

			layerGetTerrainBounds(layer, layer_min, layer_max);
			if (layer_min[0] <= layer_max[0] && layer_min[1] <= layer_max[1] && layer_min[2] <= layer_max[2])
			{
				vec3RunningMin(layer_min, region->terrain_bounds.world_min);
				vec3RunningMax(layer_max, region->terrain_bounds.world_max);
			}
		}

		eaDestroy(&region_layers);

		region->terrain_bounds.needs_update = false;
	}

	if (!world_only)
		terrainLoadAtlasesForCameraPos(iPartitionIdx, region->atlases, camera_positions, camera_position_count, terrain_editor_blocks, load_all, timestamp);

	worldCellCollisionEntryCheckCookings();

	PERFINFO_AUTO_STOP_FUNC_PIX();

	return changed;
}

int worldGridOpenAllCellsForCameraPos(int iPartitionIdx, WorldRegion *region, const F32 *camera_positions, int camera_position_count, 
	const BlockRange **terrain_editor_blocks, bool load_all, bool new_partition,
	bool draw_high_detail, bool draw_high_fill_detail, U32 timestamp, F32 cell_scale)
{
	return worldGridOpenAllCellsForCameraPosEx(iPartitionIdx, region, camera_positions, camera_position_count, 
		terrain_editor_blocks, load_all, new_partition, false, 
		draw_high_detail, draw_high_fill_detail, timestamp, cell_scale);
}

static void unloadCellDataRecursive(WorldCell *cell)
{
	int i;

	// don't touch the data if it is in the middle of loading
	if (cell->cell_state != WCS_LOADING_BG && cell->cell_state != WCS_LOADING_FG)
	{
		if (wlIsServer())
		{
			if (cell->server_data_parsed)
			{
				StructDestroySafe(parse_WorldCellServerDataParsed, &cell->server_data_parsed);
			}
		}
		else
		{
			if (cell->client_data_parsed)
			{
				StructDestroySafe(parse_WorldCellClientDataParsed, &cell->client_data_parsed);
			}
		}

		if (cell->welded_data_parsed)
		{
			StructDestroySafe(parse_WorldCellWeldedDataParsed, &cell->welded_data_parsed);
		}
	}

	for (i = 0; i < ARRAY_SIZE(cell->children); ++i)
	{
		if (cell->children[i])
			unloadCellDataRecursive(cell->children[i]);
	}
}

void worldCellUnloadUnusedCellData(void)
{
	WorldRegion **all_regions = worldGetAllWorldRegions();
	int i;

	for (i = 0; i < eaSize(&all_regions); ++i)
	{
		WorldRegion *region = all_regions[i];

		region->preloaded_cell_data = false;

		if (!region->root_world_cell || !region->root_world_cell->streaming_mode)
			continue;

		unloadCellDataRecursive(region->root_world_cell);
	}
}

void worldCellRefreshBins(WorldCell *cell)
{
	if (cell->bins_dirty)
	{
		WorldCellCullHeader *cell_header;
		int i;

		for (i = 0; i < eaSize(&cell->drawable.bins); ++i)
		{
			WorldCellWeldedBin *bin = cell->drawable.bins[i];
			if (eaSize(&bin->src_entries)==0)
			{
				worldCellFreeWeldedBin(bin);
				eaRemoveFast(&cell->drawable.bins, i);
				--i;
			}
			else if (bin->is_dirty)
			{
				eaDestroyEx(&bin->drawable_entries, worldCellEntryFree);
				worldEntryCreateForWeldedBin(bin);
				bin->is_dirty = 0;
			}
		}

		if (cell_header = worldCellGetHeader(cell))
		{
			SAFE_FREE(cell_header->welded_entry_headers);
			cell_header->welded_entries_inited = false;
		}

		cell->bins_dirty = 0;
	}
}

WorldCell *worldCellCreate(WorldRegion *region, WorldCell *parent, BlockRange *cell_block_range)
{
	WorldCell *cell;
	IVec3 size;

	rangeSize(cell_block_range, size);
	assert(size[0] == size[1]);
	assert(size[0] == size[2]);

	cell = calloc(1, sizeof(WorldCell));

	CopyStructs(&cell->cell_block_range, cell_block_range, 1);

	cell->region = region;
	cell->parent = parent;
	cell->vis_dist_level = log2(size[0]);
	cell->is_empty = 1;
	cell->no_drawables = 1;
	cell->cell_state = WCS_CLOSED;

	assert(cell->vis_dist_level < ARRAY_SIZE(open_dist_sqrd_table));
	assert(cell->vis_dist_level < ARRAY_SIZE(load_dist_sqrd_table));
	assert(cell->vis_dist_level < ARRAY_SIZE(close_dist_sqrd_table));

	if (!wlIsServer())
		worldCellQueueClusterModelLoad(cell);	// Must be loaded on client so that bounding boxes for cells are created as expected even when cells possess nothing but the cluster model.

	return cell;
}

void worldCellFree(WorldCell *cell)
{
	int i, tries = 0;

	if (!cell)
		return;

	worldCellCloseClusterBins(cell);	// needs to happen upon cell destruction.

	if (cell->drawable.gfx_data)
	{
		StructDestroyVoid(wl_state.type_worldCellGfxData, cell->drawable.gfx_data);
		cell->drawable.gfx_data = NULL;
	}

	StructDestroySafeVoid(wl_state.type_worldCellGfxData, &cell->drawable.gfx_data);

	if (wl_state.debug.world_cell == cell)
		wl_state.debug.world_cell = NULL;

	while (cell->cell_state != WCS_CLOSED || cell->bins_state != WCS_CLOSED)
	{
		worldCellClose(cell, false, true, 0);
		if (tries)
			Sleep(5); // owned by loading thread, wait
		tries++;
	}

	for (i = 0; i < ARRAY_SIZE(cell->children); ++i)
	{
		WorldCell * child_cell = cell->children[i];
		cell->children[i] = NULL;
		worldCellFree(child_cell);
	}

	if (cell == cell->region->temp_world_cell)
	{
		worldCellFreeCullHeaders(cell->region->temp_world_cell_headers);
		cell->region->temp_world_cell_headers = NULL;
	}
	else
	{
		worldCellFreeCullHeaders(cell->region->world_cell_headers);
		cell->region->world_cell_headers = NULL;
	}

	eaDestroyEx(&cell->drawable.drawable_entries, worldRemoveCellEntry);
	eaDestroyEx(&cell->drawable.near_fade_entries, worldRemoveCellEntry);
	eaDestroyEx(&cell->drawable.editor_only_entries, worldRemoveCellEntry);
	eaDestroyEx(&cell->collision.entries, worldRemoveCellEntry);
	eaDestroyEx(&cell->nondrawable_entries, worldRemoveCellEntry);
	eaDestroyEx(&cell->drawable.bins, worldCellFreeWeldedBin);

	if (wlIsServer())
	{
		if (cell->server_data_parsed)
		{
			StructDestroySafe(parse_WorldCellServerDataParsed, &cell->server_data_parsed);
		}
	}
	else
	{
		if (cell->client_data_parsed)
		{
			StructDestroySafe(parse_WorldCellClientDataParsed, &cell->client_data_parsed);
		}
	}

	if (cell->welded_data_parsed)
	{
		StructDestroySafe(parse_WorldCellWeldedDataParsed, &cell->welded_data_parsed);
	}

	free(cell);
}

static void worldCellResetCacheForCell(WorldCell *cell)
{
	WorldCellCullHeader *cell_header;
	int i;

	if (!cell)
		return;

	// free all cached header data
	if (cell_header = worldCellGetHeader(cell))
	{
		SAFE_FREE(cell_header->entry_headers);
		SAFE_FREE(cell_header->welded_entry_headers);
		SAFE_FREE(cell_header->editor_only_entry_headers);
		cell_header->entries_inited = false;
		cell_header->welded_entries_inited = false;
	}

	// recurse
	for (i = 0; i < ARRAY_SIZE(cell->children); ++i)
		worldCellResetCacheForCell(cell->children[i]);
}

void worldCellResetCachedEntries(void)
{
	WorldRegion **all_regions = worldGetAllWorldRegions();
	int i;

	for (i = 0; i < eaSize(&all_regions); ++i)
		worldCellResetCacheForCell(all_regions[i]->root_world_cell);
}

int worldCellGetChildCountInclusive(WorldCell *cell, bool only_drawables)
{
	int i, count;

	if (!cell)
		return 0;

	if (only_drawables && cell->bounds_dirty)
		worldCellUpdateBounds(cell); // update bounds so the no_drawables flag will be correct

	if (only_drawables && cell->no_drawables && !cell->cluster_related)
		return 0;

	count = 1; // for the cell itself
	for (i = 0; i < ARRAY_SIZE(cell->children); ++i)
		count += worldCellGetChildCountInclusive(cell->children[i], only_drawables);

	return count;
}

void worldCellSetNoCloseCellsDueToTempCameraPosition(bool bNoClose)
{
	g_bNoCloseCellsDueToTempCameraPosition = bNoClose;
}
