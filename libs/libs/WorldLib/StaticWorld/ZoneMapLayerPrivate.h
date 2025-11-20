/***************************************************************************



***************************************************************************/

#ifndef _ZONEMAPLAYERPRIVATE_H_
#define _ZONEMAPLAYERPRIVATE_H_
GCC_SYSTEM

#include "file.h"
#include "WorldGrid.h" // for MAX_TERRAIN_LODS
#include "wlRoad.h"

typedef struct GroupDef				GroupDef;
typedef struct GroupTracker			GroupTracker;
typedef struct ZoneMapLayer			ZoneMapLayer;
typedef struct StashTableImp *		StashTable;
typedef struct HeightMap			HeightMap;
typedef struct HogFile				HogFile;
typedef struct TerrainEditorSourceLayer TerrainEditorSourceLayer;

AUTO_STRUCT;
typedef struct BlockRange
{
	IVec3 min_block;
	IVec3 max_block;
} BlockRange;

//////////////////////////////////////////////////////////////////////////
// Group Tree Layer Type

typedef struct ZoneMapGroupTreeLayer
{
	GroupDef			*root_def;
	GroupTracker		*root_tracker;
	GroupDefLib			*def_lib;
	bool				unsaved_changes;
} ZoneMapGroupTreeLayer;


//////////////////////////////////////////////////////////////////////////
// Terrain Layer Type
//

typedef struct TerrainFile
{
	WorldFile file_base;
	ZoneMapLayer *layer;

	IVec2 size;
	FILE *file;

} TerrainFile;

typedef struct TerrainFileList
{
	TerrainFile heightmap;
	TerrainFile holemap;
	TerrainFile colormap;
	TerrainFile tiffcolormap;
	TerrainFile materialmap;
	TerrainFile objectmap;
	TerrainFile ecomap;
	TerrainFile soildepthmap;
} TerrainFileList;

typedef struct TerrainBlockRange
{
	BlockRange range;
	char *range_name;

	int block_idx;

	TerrainFileList source_files;

	bool need_bins;

	HogFile *interm_file;

	F32 *map_ranges_array;

	HeightMap **map_cache;

} TerrainBlockRange;

AUTO_STRUCT;
typedef struct RiverParamPoint
{
	S32 index;
	F32 t;
	F32 width;
} RiverParamPoint;

AUTO_STRUCT;
typedef struct River
{
	RiverParamPoint **param_points;
} River;

AUTO_STRUCT;
typedef struct RiverList
{
	River **list;
} RiverList;

typedef struct TerrainBinnedObject
{
	S32 group_id;
	U32 x;
	U32 z;
	U32 seed;
	Vec3 position;
	Vec3 normal;
	F32 scale;
	F32 rotation;
	F32 intensity;
	U8 tint[3];
	U8 weld;
	U8 weld_group;
} TerrainBinnedObject;

typedef struct TerrainBinnedObjectGroup
{
	TerrainBinnedObject **objects;
	IVec2 rel_pos;
} TerrainBinnedObjectGroup;

typedef struct TerrainObjectWrapper
{
	ZoneMapLayer *layer;
	GroupDefLib *def_lib;
    GroupDef *root_def;
    U32 id;
} TerrainObjectWrapper;

AUTO_STRUCT;
typedef struct ZoneMapTerrainLayer
{
	U32					layer_timestamp;					// Used to keep header up-to-date
    bool				non_playable;

	char				**material_table;
	TerrainObjectEntry	**object_table;

	TerrainBlockRange	**blocks;							AST( STRUCT(parse_TerrainBlockRange) )
	
	AST_STOP												// NO_AST stuff not saved in bins

    TerrainExclusionVersion	exclusion_version;

	F32					color_shift;
	bool				layer_hidden;

	Vec3				heightmaps_min;
	Vec3				heightmaps_max;

	TerrainBinnedObjectGroup** binned_instance_groups;		// Instances loaded from bin instead of source

	int					loaded_lod;
	
	TerrainObjectWrapper **object_defs;						// EArray of dummy layers, one for each object in object_table

	bool				need_bins;
	bool				unsaved_changes;

	TerrainEditorSourceLayer *source_data;					// Source data used in EditLib

} ZoneMapTerrainLayer;


extern ParseTable parse_TerrainBlockRange[];
#define TYPE_parse_TerrainBlockRange TerrainBlockRange


__forceinline static bool trangesOverlap(SA_PARAM_NN_VALID const TerrainBlockRange *range1, SA_PARAM_NN_VALID const TerrainBlockRange *range2)
{
	if (range1->range.min_block[0] > range2->range.max_block[0])
		return false;
	if (range1->range.min_block[2] > range2->range.max_block[2])
		return false;
	if (range2->range.min_block[0] > range1->range.max_block[0])
		return false;
	if (range2->range.min_block[2] > range1->range.max_block[2])
		return false;
	return true;
}

__forceinline static int trangeWidth(SA_PARAM_NN_VALID const TerrainBlockRange *range)
{
	return range->range.max_block[0] - range->range.min_block[0] + 1;
}

__forceinline static int trangeHeight(SA_PARAM_NN_VALID const TerrainBlockRange *range)
{
	return range->range.max_block[2] - range->range.min_block[2] + 1;
}

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

#endif //_ZONEMAPLAYERPRIVATE_H_

