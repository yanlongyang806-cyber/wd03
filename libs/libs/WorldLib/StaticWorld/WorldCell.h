/***************************************************************************



***************************************************************************/

#ifndef _WORLDCELL_H_
#define _WORLDCELL_H_
GCC_SYSTEM

#include "WorldGridPrivate.h"

typedef struct WorldCell WorldCell;
typedef struct ZoneMap ZoneMap;
typedef struct WorldCellWeldedBin WorldCellWeldedBin;
typedef struct WorldRegion WorldRegion;
typedef struct WorldRegionCommonParsed WorldRegionCommonParsed;
typedef struct WorldCellEntry WorldCellEntry;
typedef struct WorldCellEntryData WorldCellEntryData;
typedef struct SimpleBuffer* SimpleBufHandle;
typedef struct WorldCellClientDataParsed WorldCellClientDataParsed;
typedef struct WorldCellServerDataParsed WorldCellServerDataParsed;
typedef struct WorldCellWeldedDataParsed WorldCellWeldedDataParsed;

typedef struct WorldCellWeldedBin
{
	WorldDrawableEntry **drawable_entries;
	U32 is_dirty:1;

	WorldCellEntryType src_type;
	void *bin_key;

	WorldCell *cell;
	WorldDrawableEntry **src_entries;
} WorldCellWeldedBin;


// cell culling data for drawing code (64 bytes on 32 bit architecture)
typedef struct WorldCellCullHeader
{
	// These cull headers are laid out in a skip tree; 
	// this offset tells us how many entries to skip ahead if this cell is culled.
	// It is equal to the number of children of this cell, plus one for itself.
	U16 skip_offset;

	U8 is_open:1;
	U8 entries_inited:1;
	U8 welded_entries_inited:1;
	U8 no_fog:1; // set if any entry or children's entries have the no_fog material flag set

	U8 occluded_bits;
	U32 last_occlusion_update_time;

	Vec3 world_mid;
	F32 radius;
	Vec3 world_min, world_max;

	void *entry_headers;
	void *welded_entry_headers;
	void *editor_only_entry_headers;

	WorldCell *cell;

} WorldCellCullHeader;

#define CULL_HEADER_COUNT(type) ((type) ? *((int*)(type)) : 0)
#define CULL_HEADER_SETCOUNT(type, header_count) *((int*)(type)) = (header_count)
// Note the cull headers must be naturally aligned when allocated
#define CULL_HEADER_START(type) ((type) ? (WorldCellEntryCullHeader*)AlignPointerUpPow2(((int*)(type))+1, DATACACHE_LINE_SIZE_BYTES) : NULL)

typedef struct WorldCell
{
	// cell properties
	U8 vis_dist_level;
	U8 cell_state;
	U8 bins_state;
	U8 streaming_mode:1;
	U8 is_empty:1;
	U8 no_drawables:1;
	U8 no_collision:1;
	U8 bounds_dirty:1;
	U8 cell_bounds_invalid:1;
	U8 bins_dirty:1;
	U8 debug_me:1;
	U8 cluster_related:1;

	// collision data
	struct 
	{
		Vec3 world_mid;
		F32 radius;
		Vec3 world_min, world_max;
		WorldCollisionEntry **entries;
	} collision;

	// drawable data
	struct 
	{
		Vec3 world_mid;
		F32 radius;
		Vec3 world_min, world_max;
		bool no_fog; // set if any entry or children's entries have the no_fog material flag set

		WorldCellCullHeader *header;

		WorldDrawableEntry **drawable_entries;
		WorldDrawableEntry **near_fade_entries;
		WorldDrawableEntry **editor_only_entries;
		WorldCellWeldedBin **bins;

		void				*gfx_data;
	} drawable;

	Model			*draw_model_cluster;
	HogFile			*cluster_hogg;

	// nondrawable data
	WorldCellEntry **nondrawable_entries;

	WorldCell		*children[8];

	struct
	{
		Vec3 world_min, world_max;
	} bounds;

	// used for background loading
	union
	{
		WorldCellClientDataParsed* client_data_parsed;
		WorldCellServerDataParsed* server_data_parsed;
	};
	WorldCellWeldedDataParsed* welded_data_parsed;

	WorldRegionCommonParsed *region_parsed;

	// rarely used, keep at end of struct
	WorldRegion		*region;
	BlockRange		cell_block_range; // uses CELL_BLOCK_SIZE, not GRID_BLOCK_SIZE
	WorldCell		*parent;
	U32				close_request_time;

} WorldCell;

void worldAddCellEntry(SA_PARAM_NN_VALID WorldRegion *region, SA_PARAM_NN_VALID WorldCellEntry *entry);
void worldAddCellEntryToCell(SA_PARAM_NN_VALID WorldCell *cell, SA_PARAM_NN_VALID WorldCellEntry *entry, SA_PARAM_NN_VALID WorldCellEntryData *entry_data, bool is_near_fade);
SA_ORET_OP_VALID WorldCell *worldRemoveCellEntryEx(SA_PARAM_NN_VALID WorldCellEntry *entry, bool close, bool remove_from_bins); // returns old cell
__forceinline static void worldRemoveCellEntry(SA_PARAM_NN_VALID WorldCellEntry *entry) { worldRemoveCellEntryEx(entry, true, true); }

WorldCell *worldCellCreate(SA_PARAM_NN_VALID WorldRegion *region, SA_PARAM_OP_VALID WorldCell *parent, SA_PARAM_NN_VALID BlockRange *node_range);
void worldCellFree(SA_PARAM_OP_VALID WorldCell *cell);

void worldCellFreeCullHeaders(SA_PARAM_OP_VALID WorldCellCullHeader *cull_headers);
void worldCellUpdateBounds(SA_PARAM_OP_VALID WorldCell *cell);
void worldCellRefreshBins(SA_PARAM_NN_VALID WorldCell *cell);
void worldCellFreeWeldedBin(SA_PARAM_NN_VALID WorldCellWeldedBin *bin);

void worldCellCloseForRegion(SA_PARAM_OP_VALID WorldRegion *region);
void worldCloseCellOnPartition(int iPartitionIdx, WorldCell *cell);

U32 worldCellGetVisDistLevelNear(SA_PARAM_OP_VALID WorldRegion *region, F32 vis_dist, WorldCellEntry *entry);

bool worldNearFadeEntryAffectsCell(SA_PARAM_NN_VALID WorldDrawableEntry *entry, SA_PARAM_NN_VALID WorldCell *cell);
WorldCell *worldCellGetChildForGridPos(SA_PARAM_NN_VALID WorldCell *cell, const IVec3 grid_pos, int *child_idx, bool create_nonexistent);
WorldCell *worldCellFindLeafMostCellForEntry(SA_PARAM_NN_VALID WorldCell *cell, SA_PARAM_NN_VALID const WorldDrawableEntry *entry);
void worldCellGetBlockRangeCenter(const WorldCell *cell, Vec3 cell_center);

void worldCellResetCachedEntries(void);
void worldCellUnloadUnusedCellData(void);

int worldCellGetChildCountInclusive(WorldCell *cell, bool only_drawables);

void worldCellSetNoCloseCellsDueToTempCameraPosition(bool bNoClose);
void worldCellCloseClusterBins(WorldCell *cell);
void worldCellSetupDrawableBlockRange(WorldCell *cell, WorldCellCullHeader *header);
void worldCellUpdateBoundsFromChildren(WorldCell *cell);

SA_RET_OP_VALID __forceinline static WorldCellCullHeader *worldCellGetHeader(SA_PARAM_OP_VALID WorldCell *cell)
{
	if (!cell)
		return NULL;
	if ((cell == cell->region->temp_world_cell) ? cell->region->temp_world_cell_headers : cell->region->world_cell_headers)
		return cell->drawable.header;
	return NULL;
}

#endif //_WORLDCELL_H_
