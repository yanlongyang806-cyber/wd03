/***************************************************************************



***************************************************************************/


//////////////////////////////////////////////////////////////////////////
//
// The WorldGrid, ZoneMap, and ZoneMapLayer structures define data in 2 different
//     spaces that are offset from each other in x and z by some number of world cells.
//
// Primary Map Space
//  - The primary map's location is at 0,0
//  - Most computation is done in this space, as well as most storage:
//    - WorldCell quadtree
//    - collision
//    - drawing
//    - entity and fx positions
//
// Zone Map Space
//  - Each zone map has it's own local space in which its data is stored.
//  - A zone can be moved to any location in World Grid Space without changing the map's data.
//  - The following data is in Zone Map Space:
//    - map block ranges
//    - group defs in the map's layers
//    - terrain blocks
//    - curve data
//
// Note that for the primary map, Primary Map Space and Zone Map Space are the same.
//
//////////////////////////////////////////////////////////////////////////
#define GENESIS_ALLOW_OLD_HEADERS

#include "fileutil2.h"

#include "crypt.h"
#include "gimmeDLLWrapper.h"
#include "StringCache.h"
#include "qsortG.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "SimpleParser.h"
#include "DebugState.h"
#include "trivia.h"
#include "hoglib.h"
#include "ControllerScriptingSupport.h"
#include "ContinuousBuilderSupport.h"
#include "utilitiesLib.h"
#include "logging.h"
#include "TimedCallback.h"

#include "wlState.h"
#include "wlTerrainPrivate.h"
#include "wlVolumes.h"
#include "WorldBounds.h"
#include "WorldCell.h"
#include "worldCellEntryPrivate.h"
#include "WorldGridLoadPrivate.h"
#include "ObjectLibrary.h"
#include "wlEncounter.h"
#include "WorldCellStreaming.h"
#include "wlTime.h"
#include "expression.h"
#include "dynFxInterface.h"
#include "dynThread.h"
#include "dynNodeInline.h"
#include "beaconConnection.h"
#include "beacon.h"
#include "RoomConn.h"
#include "MapDescription.h"
#include "MapDescription_h_ast.h"
#include "mutex.h"
#include "GlobalEnums_h_ast.h"
#include "wininclude.h"
#include "wlGenesis.h"
#include "WorldCellStreamingPrivate.h"
#include "structInternals.h"
#include "StringUtil.h"

static ZoneMapInfo *zone_load_async;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););


#define VALIDATE_ZMAP(zmap) if (!(zmap)) (zmap) = world_grid.active_map; if (!(zmap)) return;
#define VALIDATE_ZMAP_RET(zmap, ret) if (!(zmap)) (zmap) = world_grid.active_map; if (!(zmap)) return (ret);

#define EDITOR_REGION_NAME "EditorRegion"

AUTO_STRUCT;
typedef struct WorldRegionRulesArray
{
	WorldRegionRules **region_rules;	AST( NAME(WorldRegionRules) )
} WorldRegionRulesArray;

WorldGrid world_grid;

static WorldRegionRulesArray world_region_rules;

static int last_temp_tracker_count;

static ZoneMapLoadedCallback zone_loaded_callback;
static void *zone_loaded_callback_userdata;
static TimedCallback *zone_load_time_callback = NULL;

int detailed_error_on_region_overlap = 0;
AUTO_CMD_INT(detailed_error_on_region_overlap, LogOverlapObjs);


//////////////////////////////////////////////////////////////////////////

AUTO_RUN;
void initTempTrackerDebugWatch(void)
{
	dbgAddIntWatch("TempTrackerCount", last_temp_tracker_count);
}

#include "WorldGrid_h_ast.c"
#include "WorldGridPrivate_h_ast.c"
#include "WorldGrid_c_ast.c"

F32 g_worldBoundsMaxDist = 0;

ZoneMap *worldGetActiveMap(void)
{
	return world_grid.active_map;
}

bool worldGetMapClustered(ZoneMap *zMap)
{
	int i;

	if (!zMap)
		zMap = worldGetActiveMap();

	for (i = 0; i < eaSize(&zMap->map_info.regions); i++)
		if (zMap->map_info.regions[i]->bWorldGeoClustering)
			return true;
	return false;
}

ZoneMap *worldGetPrimaryMap(void)
{
	return (eaSize(&world_grid.maps) > 0) ? world_grid.maps[0] : NULL;
}

WorldGraphicsData *worldGetWorldGraphicsData(void)
{
	if (!world_grid.graphics_data && wl_state.alloc_world_graphics_data)
		world_grid.graphics_data = wl_state.alloc_world_graphics_data();
	return world_grid.graphics_data;
}

void worldCreatePartition(int iPartitionIdx, bool bCreateScenes)
{
	WorldColl *pColl;

	PERFINFO_AUTO_START_FUNC();

 	pColl = eaGet(&world_grid.eaWorldColls, iPartitionIdx);
	if (pColl) {
		PERFINFO_AUTO_STOP();
		return;
	}

	// Create the collision space
	wcCreate(&pColl);
	eaSet(&world_grid.eaWorldColls, pColl, iPartitionIdx);

	// Create the volume data
	wlVolumeCreatePartition(iPartitionIdx);

	if (bCreateScenes) {
		// Create physics scenes
		wcCreateAllScenes(pColl);
	}

	PERFINFO_AUTO_STOP();
}

void worldDestroyPartition(int iPartitionIdx)
{
	WorldColl *pColl;

	PERFINFO_AUTO_START_FUNC();

	pColl = eaGet(&world_grid.eaWorldColls, iPartitionIdx);
	if (!pColl) {
		PERFINFO_AUTO_STOP();
		return;
	}

	// Destroy the world partition
	wcDestroy(&pColl);
	eaSet(&world_grid.eaWorldColls, NULL, iPartitionIdx);

	PERFINFO_AUTO_STOP();
}

WorldColl* worldGetAnyActiveColl(void)
{
	int i;
	for(i=0; i<eaSize(&world_grid.eaWorldColls); ++i) {
		if (world_grid.eaWorldColls[i]) {
			return world_grid.eaWorldColls[i];
		}
	}
	assertmsg(0, "Attempting to get a valid world collision space index and there isn't one created");
}

WorldColl* worldGetActiveColl(int iPartitionIdx)
{
	WorldColl *pColl;

	pColl = eaGet(&world_grid.eaWorldColls, iPartitionIdx);

	if(beaconIsBeaconizer())
	{
		pColl = beaconGetWorldColl(pColl);
		assertmsgf(pColl, "World collision data not initialized for beaconizer");
		return pColl;
	}
	else
	{
		assertmsgf(pColl, "Partition %d is not initialized for the world collision data", iPartitionIdx);
		return pColl;
	}
}

S32	worldGetPartitionIdxByColl(WorldColl* wc)
{
	return eaFind(&world_grid.eaWorldColls, wc);
}

int worldGetAnyCollPartitionIdx(void)
{
	int i;
	for(i=0; i<eaSize(&world_grid.eaWorldColls); ++i) {
		if (world_grid.eaWorldColls[i]) {
			return i;
		}
	}
	if(beaconIsBeaconizer() && beaconIsClient())
		return 0;

	assertmsg(0, "Attempting to get a valid world collision space index and there isn't one created");
}

bool worldIsValidPartitionIdx(int iPartitionIdx)
{
	return (eaGet(&world_grid.eaWorldColls, iPartitionIdx) != NULL);
}

void worldCreateAllCollScenes(void)
{
	int i;
	for(i=0; i<eaSize(&world_grid.eaWorldColls); ++i) {
		if (world_grid.eaWorldColls[i]) {
			wcCreateAllScenes(world_grid.eaWorldColls[i]);
		}
	}
}

static ExprContext *s_pTempChoiceContext = NULL;

void tempPuppetChoice_Generate(TempPuppetChoice *pPuppetChoice)
{
	if(pPuppetChoice->pEvalExpression)
		exprGenerate(pPuppetChoice->pEvalExpression,s_pTempChoiceContext);
}

ExprContext **worldGetTempChoiceContext(void)
{
	return &s_pTempChoiceContext;
}

void initWorldRegion(WorldRegion *region)
{
	if (region->name && stricmp(region->name, EDITOR_REGION_NAME)==0)
		region->is_editor_region = true;

	setVec3same(region->world_bounds.world_min, MAX_PLAYABLE_COORDINATE);
	setVec3same(region->world_bounds.world_max, -MAX_PLAYABLE_COORDINATE);
	setVec3same(region->world_bounds.world_visible_geo_min, MAX_PLAYABLE_COORDINATE);
	setVec3same(region->world_bounds.world_visible_geo_max, -MAX_PLAYABLE_COORDINATE);
	setVec3same(region->world_bounds.cell_extents.min_block, S32_MAX);
	setVec3same(region->world_bounds.cell_extents.max_block, S32_MIN);

	setVec3same(region->binned_bounds.cell_extents.min_block, S32_MAX);
	setVec3same(region->binned_bounds.cell_extents.max_block, S32_MIN);

	setVec3same(region->terrain_bounds.world_min, MAX_PLAYABLE_COORDINATE);
	setVec3same(region->terrain_bounds.world_max, -MAX_PLAYABLE_COORDINATE);
	setVec3same(region->terrain_bounds.cell_extents.min_block, S32_MAX);
	setVec3same(region->terrain_bounds.cell_extents.max_block, S32_MIN);

	if (dtInitialized() && GetAppGlobalType() == GLOBALTYPE_CLIENT)
		dynFxRegionInit(&region->fx_region, region);

	region->world_bounds.needs_update = true;
	region->terrain_bounds.needs_update = true;
	region->room_conn_graph = calloc(1, sizeof(*region->room_conn_graph));

	eaPushUnique(&world_grid.all_regions, region);

	{
		int i;
		for(i=0;i<eaSize(&region->pRegionRulesOverride.ppTempPuppets);i++)
		{
			tempPuppetChoice_Generate(region->pRegionRulesOverride.ppTempPuppets[i]);
		}
	}
}

void uninitWorldRegion(WorldRegion *region)
{
	int i;

	if (!region)
		return;

	if (dtInitialized() && GetAppGlobalType() == GLOBALTYPE_CLIENT)
		dynFxRegionDestroy(&region->fx_region);

	freeHeightMapAtlasRegionData(region->atlases);
	region->atlases = NULL;

	// force background loads to finish before destroying terrain hogg files
	geoForceBackgroundLoaderToFinish();

	for (i = 0; i <= ATLAS_MAX_LOD; ++i)
	{
		if (region->terrain_atlas_hoggs[i])
		{
			hogFileDestroy(region->terrain_atlas_hoggs[i], true);
			region->terrain_atlas_hoggs[i] = NULL;
		}
		if (region->terrain_model_hoggs[i])
		{
			hogFileDestroy(region->terrain_model_hoggs[i], true);
			region->terrain_model_hoggs[i] = NULL;
		}
		if (region->terrain_light_model_hoggs[i])
		{
			hogFileDestroy(region->terrain_light_model_hoggs[i], true);
			region->terrain_light_model_hoggs[i] = NULL;
		}
	}

	if (region->terrain_coll_hogg)
	{
		hogFileDestroy(region->terrain_coll_hogg, true);
		region->terrain_coll_hogg = NULL;
	}

	worldCellFree(region->root_world_cell);
	region->root_world_cell = NULL;
	region->preloaded_cell_data = false;
	
	worldCellFree(region->temp_world_cell);
	region->temp_world_cell = NULL;

	if (wl_state.free_region_graphics_data)
		wl_state.free_region_graphics_data(region, true);

	// free dummy layer
	layerFree(region->temp_layer);
	region->temp_layer = NULL;

	// free room connectivity graph
	if (region->room_conn_graph)
		roomConnGraphDestroy(region->room_conn_graph);

	eaDestroyStruct(&region->world_path_nodes, parse_WorldPathNode);

	eaDestroyStruct(&region->world_path_nodes_editor_only, parse_WorldPathNode);

	eaDestroyEx(&region->tag_locations, NULL);

	StructDestroySafe(parse_WorldRegionSkyOverride, &region->sky_override);

	eaFindAndRemoveFast(&world_grid.all_regions, region);
}

WorldRegion *createWorldRegion(ZoneMap *zmap, const char *name)
{
	WorldRegion *region = StructCreate(parse_WorldRegion);

	if (name && (name[0] == 0 || stricmp(name, "default")==0))
		name = NULL;

	region->name = name?allocAddString(name):NULL;
	region->type = WRT_Ground;

	initWorldRegion(region);

	region->zmap_parent = zmap;

	if (name)
	{
		if (zmap)
			eaPush(&zmap->map_info.regions, region);
		else
			eaPush(&world_grid.temp_regions, region);
	}
	else
	{
		if (zmap)
			eaInsert(&zmap->map_info.regions, region, 0);
		else
			eaInsert(&world_grid.temp_regions, region, 0);
	}

	return region;
}

HogFile *worldRegionGetTerrainHogFile(WorldRegion *region, TerrainHogFileType type, int lod)
{
	char filename[MAX_PATH];
	bool created;
	int err_return;
	char base_dir[MAX_PATH];

	assert(region->zmap_parent);
	assert(lod >= 0 && lod <= ATLAS_MAX_LOD);

	worldGetClientBaseDir(zmapGetFilename(region->zmap_parent), SAFESTR(base_dir));

	switch (type)
	{
		xcase THOG_ATLAS:
		{
			if (!region->terrain_atlas_hoggs[lod])
			{
				sprintf(filename, "%s/terrain_%s_atlases_%d.hogg", base_dir, region->name ? region->name : "Default", lod);

				binNotifyTouchedOutputFile(filename);
				region->terrain_atlas_hoggs[lod] = hogFileRead(filename, &created, PIGERR_ASSERT, &err_return, HOG_NOCREATE | HOG_READONLY | HOG_NO_INTERNAL_TIMESTAMPS);
			}

			return region->terrain_atlas_hoggs[lod];
		}

		xcase THOG_MODEL:
		{
			if (!region->terrain_model_hoggs[lod])
			{
				sprintf(filename, "%s/terrain_%s_models_%d.hogg", base_dir, region->name ? region->name : "Default", lod);

				binNotifyTouchedOutputFile(filename);
				region->terrain_model_hoggs[lod] = hogFileRead(filename, &created, PIGERR_ASSERT, &err_return, HOG_NOCREATE | HOG_READONLY | HOG_NO_INTERNAL_TIMESTAMPS);
			}

			return region->terrain_model_hoggs[lod];
		}

		xcase THOG_LIGHTMODEL:
		{
			if (!region->terrain_light_model_hoggs[lod])
			{
				sprintf(filename, "%s/terrain_%s_light_models_%d.hogg", base_dir, region->name ? region->name : "Default", lod);

				binNotifyTouchedOutputFile(filename);
				region->terrain_light_model_hoggs[lod] = hogFileRead(filename, &created, PIGERR_ASSERT, &err_return, HOG_NOCREATE | HOG_READONLY | HOG_NO_INTERNAL_TIMESTAMPS);
			}

			return region->terrain_light_model_hoggs[lod];
		}

		xcase THOG_COLL:
		{
			if (!region->terrain_coll_hogg)
			{
				sprintf(filename, "%s/terrain_%s_collision.hogg", base_dir, region->name ? region->name : "Default");

				binNotifyTouchedOutputFile(filename);
				region->terrain_coll_hogg = hogFileRead(filename, &created, PIGERR_ASSERT, &err_return, HOG_NOCREATE | HOG_READONLY | HOG_NO_INTERNAL_TIMESTAMPS);
			}

			return region->terrain_coll_hogg;
		}
	}

	assert(0);

	return NULL;
}

WorldRegion *worldGetTempWorldRegionByName(const char *name)
{
	WorldRegion *region = NULL;
	int i;

	if (name && (name[0] == 0 || stricmp(name, "default")==0))
		name = NULL;

	if (!name)
	{
		// return default temp region
		assert(eaSize(&world_grid.temp_regions));
		assert(!world_grid.temp_regions[0]->zmap_parent);
		return world_grid.temp_regions[0];
	}

	name = allocAddString(name);
	for (i = 0; i < eaSize(&world_grid.temp_regions); ++i)
	{
		if (world_grid.temp_regions[i]->name == name)
		{
			region = world_grid.temp_regions[i];
			break;
		}
	}

	if (!region)
		region = createWorldRegion(NULL, name);

	assert(!region->zmap_parent);
	return region;
}

WorldRegion *worldGetWorldRegionByPos(const Vec3 pos)
{
	WorldRegion *region = NULL;
	WorldRegion **region_list;
	int i;

	if (world_grid.active_map)
		region_list = world_grid.active_map->map_info.regions;
	else
		region_list = world_grid.temp_regions;

	assert(eaSize(&region_list));

	for (i = eaSize(&region_list) - 1; i > 0; --i)
	{
		Vec3 world_min, world_max;
		WorldRegion* cur_region = region_list[i];
		if (!cur_region)
			continue;
		if(worldRegionGetBounds(cur_region, world_min, world_max))
			if (pointBoxCollision(pos, world_min, world_max))
			{
				region = cur_region;
				break;
			}
	}

	if (!region)
		region = region_list[0];

	return region;
}

bool worldHasWorldRegions(void)
{
	WorldRegion **region_list;

	if (world_grid.active_map)
		region_list = world_grid.active_map->map_info.regions;
	else
		region_list = world_grid.temp_regions;

	return !!eaSize(&region_list);
}

WorldRegion *worldGetEditorWorldRegion(void)
{
	return worldGetTempWorldRegionByName(EDITOR_REGION_NAME);
}

bool worldRegionIsEditorRegion(const WorldRegion *region)
{
	return region->is_editor_region;
}

bool worldRegionIsTemporary(SA_PARAM_NN_VALID const WorldRegion *region)
{
	return region->is_editor_region || eaFind(&world_grid.temp_regions, region) != -1;
}

void worldCloseAllRegionsExcept(const WorldRegion *region)
{
	WorldRegion **all_regions = worldGetAllWorldRegions();
	int i;

	for (i = 0; i < eaSize(&all_regions); ++i)
	{
		if (all_regions[i] == region)
			continue;
		if (all_regions[i]->is_editor_region)
			continue;
		worldCellCloseForRegion(all_regions[i]);
	}
}

void worldCloseAllOldRegions(U32 timestamp)
{
	WorldRegion **all_regions = worldGetAllWorldRegions();
	int i;

	PERFINFO_AUTO_START_FUNC_PIX();

	for (i = 0; i < eaSize(&all_regions); ++i)
	{
		if (all_regions[i]->last_used_timestamp >= timestamp)
			continue;
		if (all_regions[i]->is_editor_region)
			continue;
		worldCellCloseForRegion(all_regions[i]);
		terrainUnloadAtlases(all_regions[i]->atlases);
	}

	PERFINFO_AUTO_STOP_FUNC_PIX();
}

WorldRegionGraphicsData *worldRegionGetGraphicsData(WorldRegion *region)
{
	if (!region)
	{
		if (world_grid.active_map)
		{
			assert(eaSize(&world_grid.active_map->map_info.regions));
			region = world_grid.active_map->map_info.regions[0];
		}
		else
		{
			assert(eaSize(&world_grid.temp_regions));
			region = world_grid.temp_regions[0];
		}
	}

	if (!region->graphics_data && wl_state.alloc_region_graphics_data)
	{
		Vec3 world_min, world_max;
		worldRegionGetBounds(region, world_min, world_max);
		region->graphics_data = wl_state.alloc_region_graphics_data(region, world_min, world_max);
	}

	return region->graphics_data;
}

int worldRegionGetVisibleBounds(const WorldRegion *region, Vec3 world_min, Vec3 world_max)
{
	int anyBounds = 1;
	MINVEC3(region->world_bounds.world_visible_geo_min, region->terrain_bounds.world_min, world_min);
	MAXVEC3(region->world_bounds.world_visible_geo_max, region->terrain_bounds.world_max, world_max);

	if (world_min[0] > world_max[0] || world_min[1] > world_max[1] || world_min[2] > world_max[2])
	{
		zeroVec3(world_min);
		zeroVec3(world_max);
		anyBounds = 0;
	}

	return anyBounds;
}

int worldRegionGetBounds(const WorldRegion *region, Vec3 world_min, Vec3 world_max)
{
	int anyBounds = 1;
	MINVEC3(region->world_bounds.world_min, region->terrain_bounds.world_min, world_min);
	MAXVEC3(region->world_bounds.world_max, region->terrain_bounds.world_max, world_max);

	if (world_min[0] > world_max[0] || world_min[1] > world_max[1] || world_min[2] > world_max[2])
	{
		zeroVec3(world_min);
		zeroVec3(world_max);
		anyBounds = 0;
	}

// 	if (in_world_space)
// 	{
// 		Vec3 offset;
// 		zmapGetOffset(region_list[i]->zmap_parent, offset);
// 		addVec3(world_min, offset, world_min);
// 		addVec3(world_max, offset, world_max);
// 	}
	return anyBounds;
}

const WorldRegionRules *worldRegionGetRulesByType(WorldRegionType region_type)
{
	static WorldRegionRules default_rules = {0};
	int i;
	
	for (i = 0; i < eaSize(&world_region_rules.region_rules); ++i)
	{
		if (world_region_rules.region_rules[i]->region_type == region_type)
			return world_region_rules.region_rules[i];
	}

	if (!default_rules.lod_scale)
	{
		StructInit(parse_WorldRegionRules, &default_rules); //setup defaults
	}
	return &default_rules;
}

const WorldRegionRules *worldRegionGetRules(const WorldRegion *region)
{
	return worldRegionGetRulesByType(region ? region->type : WRT_None);
}

const CamLightRules *worldRegionRulesGetCamLightRules(const WorldRegionRules *region_rules)
{
	return &region_rules->camLight;
}

const ShadowRules *worldRegionRulesGetShadowRules(const WorldRegionRules *region_rules)
{
	return &region_rules->shadows;
}
const WorldRegionLODSettings *worldRegionRulesGetLODSettings(const WorldRegionRules *region_rules)
{
	if (region_rules->lod_settings.uiNumLODLevels > 0)
		return &region_rules->lod_settings;
	else
		return &defaultLODSettings;
}

void worldRegionLODSettingsVerify(void)
{
	FOR_EACH_IN_EARRAY(world_region_rules.region_rules, WorldRegionRules, region_rules)
	{
		WorldRegionLODSettings* lod_settings = &region_rules->lod_settings;
		if (lod_settings->uiNumLODLevels > 0)
		{
			// Verify counts and pre-process any calculations
			lod_settings->uiMaxLODLevel = lod_settings->uiNumLODLevels - 1;
			lod_settings->uiWindLODLevel = lod_settings->uiIKLODLevel = 2; // hardcoded for now

			if (lod_settings->uiNumLODLevels > MAX_WORLD_REGION_LOD_LEVELS)
			{
				ErrorFilenamef(region_rules->filename, "%d NumLevels exceeds max of %d", lod_settings->uiNumLODLevels, MAX_WORLD_REGION_LOD_LEVELS);
				lod_settings->uiNumLODLevels = 0;
				continue;
			}

			if (ea32Size(&lod_settings->eaLodDistance) != lod_settings->uiNumLODLevels)
			{
				ErrorFilenamef(region_rules->filename, "%d Distances specified, should be same as NumLevels %d", ea32Size(&lod_settings->eaLodDistance), lod_settings->uiNumLODLevels);
				lod_settings->uiNumLODLevels = 0;
				continue;
			}

			if (ea32Size(&lod_settings->eaDefaultMaxLODSkelSlots) != lod_settings->uiNumLODLevels)
			{
				ErrorFilenamef(region_rules->filename, "%d PerLevelMax specified, should be same as NumLevels %d", ea32Size(&lod_settings->eaDefaultMaxLODSkelSlots), lod_settings->uiNumLODLevels);
				lod_settings->uiNumLODLevels = 0;
				continue;
			}

			if (lod_settings->eaLodDistance[0] != 0.0f)
			{
				ErrorFilenamef(region_rules->filename, "First Distance must be 0");
				lod_settings->uiNumLODLevels = 0;
				continue;
			}

			{
				U32 uiIndex;
				F32 fLastDistance = -1.0f;
				bool bDistancesIncreasing = true;
				for (uiIndex=0; uiIndex<lod_settings->uiNumLODLevels; ++uiIndex)
				{
					lod_settings->LodDistance[uiIndex] = lod_settings->eaLodDistance[uiIndex];
					lod_settings->MaxLODSkelSlots[uiIndex] = lod_settings->DefaultMaxLODSkelSlots[uiIndex] = lod_settings->eaDefaultMaxLODSkelSlots[uiIndex];

					if (lod_settings->eaLodDistance[uiIndex] <= fLastDistance)
					{
						bDistancesIncreasing = false;
						break;
					}
					fLastDistance = lod_settings->eaLodDistance[uiIndex];
				}

				if (!bDistancesIncreasing)
				{
					ErrorFilenamef(region_rules->filename, "Distances must start at 0 and always increase");
					lod_settings->uiNumLODLevels = 0;
					continue;
				}
			}
		}
	}
	FOR_EACH_END;
}


const WorldRegionWindRules *worldRegionRulesGetWindRules(const WorldRegionRules *region_rules)
{
	return &region_rules->wind;
}

F32 worldRegionGetEffectiveScale(const WorldRegion *region)
{
	const WorldRegionRules *region_rules = worldRegionGetRules(region);
	return region_rules->effective_scale;
}

F32 worldRegionGetLodScale(const WorldRegion *region)
{
	const WorldRegionRules *region_rules = worldRegionGetRules(region);
	return region_rules->lod_scale;
}

bool worldRegionGetNoSkySun(const WorldRegion *region)
{
	const WorldRegionRules *region_rules = worldRegionGetRules(region);
	return region_rules->no_sky_sun;
}

const char *worldRegionGetRegionName(const WorldRegion *region)
{
	return region->name;
}

WorldRegionType worldRegionGetType(const WorldRegion *region)
{
	if (!region)
		return WRT_None;
	return region->type;
}

bool worldRegionGetIndoorLighting(WorldRegion *region)
{
	return region->bUseIndoorLighting;
}

RegionRulesOverride *worldRegionGetOverrides(SA_PARAM_NN_VALID WorldRegion *region)
{
	return &region->pRegionRulesOverride;
}

ZoneMap *worldRegionGetZoneMap(WorldRegion *region)
{
	return region->zmap_parent;
}

SkyInfoGroup *worldRegionGetSkyGroup(WorldRegion *region)
{
	return region->sky_group;
}

const char *worldRegionGetOverrideCubeMap(WorldRegion *region)
{
	return region->override_cubemap;
}

S32 worldRegionGetAllowedPetsPerPlayer(WorldRegion *region)
{
	if ( region->pRegionRulesOverride.iAllowedPetsPerPlayer < 0 )
		return -1;

	return region->pRegionRulesOverride.iAllowedPetsPerPlayer;
}

S32 worldRegionGetUnteamedPetsPerPlayer(WorldRegion *region)
{
	if ( region->pRegionRulesOverride.iUnteamedPetsPerPlayer < 0 )
		return -1;

	return region->pRegionRulesOverride.iUnteamedPetsPerPlayer;
}

S32 worldRegionGetVehicleRules(WorldRegion *region)
{
	if(region->pRegionRulesOverride.eVehicleRules < 0)
		return -1;

	return region->pRegionRulesOverride.eVehicleRules;
}

void worldRegionSetType(WorldRegion *region, WorldRegionType type)
{
	WorldRegionType old_type = region->type;
	region->type = type;
}

void worldRegionSetIndoorLighting(WorldRegion *region, bool bIndoorLightingMode)
{
	region->bUseIndoorLighting = bIndoorLightingMode;

	if (wl_state.update_indoor_volume_func && region->root_world_cell && (bIndoorLightingMode != region->bUseIndoorLighting))
	{
		wl_state.update_indoor_volume_func(region->world_bounds.world_min, region->world_bounds.world_max, unitmat, region->root_world_cell->streaming_mode);
	}
}

bool worldRegionGetWorldGeoClustering(WorldRegion *region)
{
	return region->bWorldGeoClustering;
}

void worldRegionSetWorldGeoClustering(WorldRegion *region, bool bWorldGeoClustering)
{
	region->bWorldGeoClustering = bWorldGeoClustering;
}


void worldRegionSetAllowedPetsPerPlayer(SA_PARAM_NN_VALID WorldRegion *region, S32 iAllowedPetsPerPlayer)
{
	if ( iAllowedPetsPerPlayer == 0 )
	{
		region->pRegionRulesOverride.iAllowedPetsPerPlayer = 0;
	}
	else
	{
		region->pRegionRulesOverride.iAllowedPetsPerPlayer = iAllowedPetsPerPlayer;
	}
}

void worldRegionSetUnteamedPetsPerPlayer(SA_PARAM_NN_VALID WorldRegion *region, S32 iUnteamedPetsPerPlayer)
{
	if ( iUnteamedPetsPerPlayer == 0 )
	{
		region->pRegionRulesOverride.iUnteamedPetsPerPlayer = 0;
	}
	else
	{
		region->pRegionRulesOverride.iUnteamedPetsPerPlayer = iUnteamedPetsPerPlayer;
	}
}

void worldRegionSetVehicleRules(SA_PARAM_NN_VALID WorldRegion *region, S32 eVehicleRules)
{
	region->pRegionRulesOverride.eVehicleRules = eVehicleRules;
}

void worldRegionGetAssociatedRegionList(WorldRegion ***regions, F32 **camera_positions, int **hidden_object_ids)
{
	Vec3 world_cam_pos;
	int i, j, k;

	assert(eaSize(regions) == eaiSize(hidden_object_ids));
	assert(eaSize(regions) * 3 == eafSize(camera_positions));

	ANALYSIS_ASSUME(camera_positions && *camera_positions);

	copyVec3((*camera_positions), world_cam_pos);

	for (i = 0; i < eaSize(regions); ++i)
	{
		WorldRegion *region = (*regions)[i];
		F32 *cam_pos = &(*camera_positions)[i*3];
		Vec3 offset;

		if (!region->zmap_parent)
			continue;

		// camera positions must be converted into map local space
		zmapGetOffset(region->zmap_parent, offset);
		subVec3(cam_pos, offset, cam_pos);

		if (!wl_state.debug.disable_associated_regions)
		{
			for (j = 0; j < eaSize(&region->dependents); ++j)
			{
				DependentWorldRegion *dependent = region->dependents[j];
				WorldRegion *child_region = NULL;

				for (k = 0; k < eaSize(&region->zmap_parent->map_info.regions); ++k)
				{
					if (!region->zmap_parent->map_info.regions[k]->is_editor_region && region->zmap_parent->map_info.regions[k]->name == dependent->name)
					{
						child_region = region->zmap_parent->map_info.regions[k];
						break;
					}
				}

				if (child_region)
				{
					if (eaFind(regions, child_region) < 0)
					{
						Vec3 child_cam_pos;
						if (dependent->hidden_object_id)
						{
							for (k = 0; k < eaSize(&child_region->tag_locations); ++k)
							{
								if (child_region->tag_locations[k]->tag_id == dependent->hidden_object_id)
								{
									addVec3(dependent->camera_offset, child_region->tag_locations[k]->position, child_cam_pos);
									break;
								}
							}

							if (k == eaSize(&child_region->tag_locations))
								zeroVec3(child_cam_pos);
						}
						else
						{
							addVec3(dependent->camera_offset, cam_pos, child_cam_pos);
						}

						eaPush(regions, child_region);
						eafPush3(camera_positions, child_cam_pos);
						eaiPush(hidden_object_ids, dependent->hidden_object_id);
					}
				}
			}
		}

		if (!region->name || !region->name[0] || stricmp(region->name, "default")==0)
		{
			// default region, find all other default regions from other maps
			for (j = 0; j < eaSize(&world_grid.all_regions); ++j)
			{
				WorldRegion *child_region = world_grid.all_regions[j];
				if (child_region == region || !child_region->zmap_parent || child_region->is_editor_region)
					continue;
				if (child_region->name && child_region->name[0] && stricmp(child_region->name, "default")!=0)
					continue;
				if (eaFind(regions, child_region) < 0)
				{
					eaPush(regions, child_region);
					eafPush3(camera_positions, world_cam_pos);
					eaiPush(hidden_object_ids, 0);
				}
			}
		}
	}
}

const char* worldRegionSwapFX(const WorldRegion *region, const char* pcFxName)
{
	if (region->fx_swap_table)
	{
		const char* pcResult;
		if (stashFindPointerConst(region->fx_swap_table, pcFxName, &pcResult))
			return pcResult;
		return pcFxName;
	}

	// If no stash table, the count should be < 4, so just iterate through the list.
	FOR_EACH_IN_EARRAY(region->fx_swaps, WorldRegionFXSwap, pFXSwap)
		if (stricmp(pcFxName, REF_STRING_FROM_HANDLE(pFXSwap->hOldFx))==0 && GET_REF(pFXSwap->hNewFx))
			return REF_STRING_FROM_HANDLE(pFXSwap->hNewFx);
	FOR_EACH_END;
	return pcFxName;
}

DynFxRegion *worldRegionGetFXRegion(WorldRegion *region)
{
	return SAFE_MEMBER_ADDR(region, fx_region);
}

dtFxManager worldRegionGetGlobalFXManager(WorldRegion *region)
{
	return dynFxManGetGUID(SAFE_MEMBER(region, fx_region.pGlobalFxManager));
}

MapSnapRegionData * worldRegionGetMapSnapData(WorldRegion *region)
{
	return &region->mapsnap_data;
}

static void destroyTempTracker(GroupTracker *tracker)
{
	if (tracker)
	{
		PERFINFO_AUTO_START_FUNC();
		trackerClose(tracker);
		groupDefFree(tracker->def);
		trackerFree(tracker);
		PERFINFO_AUTO_STOP_FUNC();
	}
}

void worldResetDefPools(bool destroy_memory_pools)
{
	WorldRegion **regions = worldGetAllWorldRegions();
	int i;
// 
// 	stashTableDestroy(world_grid.obj_lib.alloced_defs);
// 	world_grid.obj_lib.alloced_defs = stashTableCreateInt(2048);

	for (i = 0; i < eaSize(&regions); i++)
	{
		if (!(regions[i]->is_editor_region || regions[i]->zmap_parent == world_grid.active_map))
			continue;

		// free room connectivity graph data from the region
		if (regions[i]->room_conn_graph)
			roomConnGraphDestroy(regions[i]->room_conn_graph);
		regions[i]->room_conn_graph = NULL;
	}

	if (destroy_memory_pools)
		modelFreeAllCache(WL_FOR_WORLD); // Free all loaded models' data, and release world models
}

void worldResetWorldGrid(void)
{
    int i;
	int		*eaiHadCollisionSpaces = NULL;
	U32		last_mod_time = worldGetModTime();
	U32		last_map_reset_count = world_grid.map_reset_count;
	U32		last_file_reload_count = world_grid.file_reload_count;

	loadstart_printf("Unloading map...");

	//if(isDevelopmentMode())
		//assertHeapValidateAll();

	for (i = 0; i < eaSize((cEArrayHandle*)&wl_state.unload_map_callbacks); i++)
		wl_state.unload_map_callbacks[i]();

	if (wl_state.unload_map_game_callback)
		wl_state.unload_map_game_callback();

	if (wl_state.unload_map_civ_callback)
		wl_state.unload_map_civ_callback();

	if (wl_state.unload_map_bcn_callback)
		wl_state.unload_map_bcn_callback();

	// stop all fx
	if (dtInitialized())
		dtFxKillAll();

	last_temp_tracker_count = eaSize(&world_grid.temp_trackers);
	eaClearEx(&world_grid.temp_trackers, destroyTempTracker);

	// remove all collision and drawables
	eaForEach(&world_grid.temp_regions, uninitWorldRegion);
	eaDestroyStruct(&world_grid.temp_regions, parse_WorldRegion);

	// free interaction nodes
	wlInteractionFreeAllNodes();

	// free maps
	eaDestroyEx(&world_grid.maps, zmapUnload);
	eafDestroy(&world_grid.map_offsets);
	world_grid.active_map = NULL;

	eaDestroy(&world_grid.all_regions);

	if (world_grid.dummy_lib)
		groupLibClear(world_grid.dummy_lib);
	
	if (world_grid.graphics_data && wl_state.free_world_graphics_data)
		wl_state.free_world_graphics_data(world_grid.graphics_data);

	// free physics
	eaiSetSize(&eaiHadCollisionSpaces, eaSize(&world_grid.eaWorldColls));
	for(i=eaSize(&world_grid.eaWorldColls)-1; i>=0; --i) {
		if (world_grid.eaWorldColls[i]) {
			worldDestroyPartition(i);
			eaiHadCollisionSpaces[i] = 1; // Mark entries that were once full so we can re-create further down
		}
	}

	modelFreeAllCache(WL_FOR_WORLD); // Free all loaded models' data, and release world models
	objectLibraryFreeModelsAndEditingLib();

	genesisResetLayout();

	wcStoredModelDataDestroyAll();
#if !PLATFORM_CONSOLE
	beaconDestroyObjects();
#endif

	// initialize struct
	ZeroStruct(&world_grid);
	worldSetModTime_AsyncOK(last_mod_time+1);
	world_grid.map_reset_count = last_map_reset_count+1;
	world_grid.file_reload_count = last_file_reload_count+1;

	// Re-create physics
	for(i=eaiSize(&eaiHadCollisionSpaces)-1; i>=0; --i) {
		if (eaiHadCollisionSpaces[i]) { // Note that ones we should create were marked with 0x1 earlier
			worldCreatePartition(i, false);
		}
	}
	eaiDestroy(&eaiHadCollisionSpaces);

	// Refresh the movement managers on all existing entities
	if (wl_state.ent_refresh_callback) {
		(wl_state.ent_refresh_callback)();
	}

	createWorldRegion(NULL, NULL); // this seems to be creating a default TEMP region, which might be totally unused now.

	//if(isDevelopmentMode())
		//assertHeapValidateAll();

	loadend_printf(" done.");
}

void worldReloadMap(void)
{
	const char *name = zmapInfoGetPublicName(NULL);
	if (name)
	{
		loadstart_printf("Reloading map...");
		worldLoadZoneMapByName(name);
		loadend_printf("done.");
	}
}

void worldLoadEmptyMap(void)
{
	loadstart_printf("Loading empty map...");
	worldLoadZoneMapByName("EmptyMap");
	loadend_printf(" done.");
}

typedef struct BoundsType
{
	Vec3 world_min, world_max;
} BoundsType;

//Note that this includes temp world regions.  You likely want 
//zmapInfoGetAllWorldRegions(NULL) instead.
WorldRegion **worldGetAllWorldRegions(void)
{
	return world_grid.all_regions;
}

static void updateRegionBounds(bool check_overlapping_regions)
{
	ZoneMap *primary_map = worldGetPrimaryMap();
	WorldRegion **all_regions = NULL;
	ZoneMapLayer **all_layers = NULL;
	BoundsType *layer_bounds;
	int i, j;

	all_regions = worldGetAllWorldRegions();

	for (i = 0; i < eaSize(&world_grid.maps); ++i)
	{
		ZoneMap *zmap = world_grid.maps[i];

		for (j = 0; j < eaSize(&zmap->layers); ++j)
		{
			if (zmap->layers[j])
				eaPush(&all_layers, zmap->layers[j]);
		}
	}

	for (i = 0; i < eaSize(&all_regions); ++i)
	{
		WorldRegion *region = all_regions[i];
		setVec3same(region->world_bounds.world_min, 8e16);
		setVec3same(region->world_bounds.world_max, -8e16);
		setVec3same(region->terrain_bounds.world_min, 8e16);
		setVec3same(region->terrain_bounds.world_max, -8e16);
	}

	layer_bounds = _alloca(eaSize(&all_layers) * sizeof(BoundsType));
	for (i = 0; i < eaSize(&all_layers); i++)
	{
		ZoneMapLayer *layer = all_layers[i];
		Vec3 layer_min, layer_max;
		Vec3 layer_visible_min, layer_visible_max;
		WorldRegion *region = layerGetWorldRegion(layer);

		setVec3same(layer_bounds[i].world_min, 8e16);
		setVec3same(layer_bounds[i].world_max, -8e16);

		// world objects
		layerGetBounds(layer, layer_min, layer_max);
		layerGetVisibleBounds(layer, layer_visible_min, layer_visible_max); // includes terrain bounds
		if (layer_min[0] <= layer_max[0] && layer_min[1] <= layer_max[1] && layer_min[2] <= layer_max[2])
		{
			// We don't want the scratch layer to cause overlapping
			if(!layer->scratch) {
				// update the actual world region bounds
				vec3RunningMin(layer_min, region->world_bounds.world_min);
				vec3RunningMax(layer_max, region->world_bounds.world_max);
				vec3RunningMin(layer_visible_min, region->world_bounds.world_visible_geo_min);
				vec3RunningMax(layer_visible_max, region->world_bounds.world_visible_geo_max);
			}
			vec3RunningMin(layer_min, layer_bounds[i].world_min);
			vec3RunningMax(layer_max, layer_bounds[i].world_max);
		}

		// terrain
		layerGetTerrainBounds(layer, layer_min, layer_max);
		if (layer_min[0] <= layer_max[0] && layer_min[1] <= layer_max[1] && layer_min[2] <= layer_max[2])
		{
			// We don't want the scratch layer to cause overlapping
			if(!layer->scratch) {
				vec3RunningMin(layer_min, region->terrain_bounds.world_min);
				vec3RunningMax(layer_max, region->terrain_bounds.world_max);
			}
			vec3RunningMin(layer_min, layer_bounds[i].world_min);
			vec3RunningMax(layer_max, layer_bounds[i].world_max);
		}
	}

	// check for overlapping regions
	if (check_overlapping_regions)
	{
		for (i = 0; i < eaSize(&all_regions); ++i)
		{
			WorldRegion *region1 = all_regions[i];
			Vec3 world_min1, world_max1;

			if (worldRegionIsTemporary(region1))
				continue;

			if(!worldRegionGetBounds(region1, world_min1, world_max1))
				continue;

			if (region1->is_editor_region)
				continue;

			for (j = i+1; j < eaSize(&all_regions); ++j)
			{
				WorldRegion *region2 = all_regions[j];
				Vec3 world_min2, world_max2;

				if (worldRegionIsTemporary(region2))
					continue;

				if(!worldRegionGetBounds(region2, world_min2, world_max2))
					continue;

				if (region2->is_editor_region)
					continue;

				if (!region1->name && !region2->name)
					continue;

				if (region1->zmap_parent != region2->zmap_parent)
					continue;

				if (boxBoxCollision(world_min1, world_max1, world_min2, world_max2))
				{
					int k, l;
					bool found_layers = false;

					for (k = 0; k < eaSize(&all_layers); ++k)
					{
						if (layerGetWorldRegion(all_layers[k]) != region1)
							continue;

						for (l = k + 1; l < eaSize(&all_layers); ++l)
						{
							if (layerGetWorldRegion(all_layers[l]) != region2)
								continue;

							if (boxBoxCollision(layer_bounds[k].world_min, layer_bounds[k].world_max, layer_bounds[l].world_min, layer_bounds[l].world_max))
							{
								if(!g_isContinuousBuilder)
								{
									ErrorFilenameTwof(all_layers[k]->filename, all_layers[l]->filename, "Overlapping regions \"%s\" and \"%s\"", region1->name?region1->name:"default", region2->name?region2->name:"default");

									if(detailed_error_on_region_overlap)
									{
										static GroupDef **defs = NULL;

										if(all_layers[l]->grouptree.root_def)
										{
											eaClear(&defs);
											eaPush(&defs, all_layers[l]->grouptree.root_def);

											while(eaSize(&defs))
											{
												GroupDef *def = eaPop(&defs);

												if(boxBoxCollision(layer_bounds[k].world_min, layer_bounds[k].world_max, def->bounds.min, def->bounds.max))
												{
													if(eaSize(&def->children))
													{
														// We only want to error on root objects
														FOR_EACH_IN_EARRAY(def->children, GroupChild, child)
														{
															GroupDef *childdef = groupChildGetDef(def, child, true);
															if(childdef->name_uid != -1)
																eaPush(&defs, childdef);
														}
														FOR_EACH_END;
													}
													else if(def->name_uid != -1)
													{
														filelog_printf(	"overlappingregionobjects.log", 
															"%s overlapped by %s (%d) at %.2f %.2f %.2f",
															all_layers[k]->filename,
															def->name_str,
															def->name_uid,
															vecParamsXYZ(def->bounds.mid));
													}
												}
											}
										}

										if(all_layers[k]->grouptree.root_def)
										{
											eaClear(&defs);
											eaPush(&defs, all_layers[k]->grouptree.root_def);

											while(eaSize(&defs))
											{
												GroupDef *def = eaPop(&defs);

												if(boxBoxCollision(layer_bounds[l].world_min, layer_bounds[l].world_max, def->bounds.min, def->bounds.max))
												{
													if(eaSize(&def->children))
													{
														// We only want to error on root objects
														FOR_EACH_IN_EARRAY(def->children, GroupChild, child)
														{
															GroupDef *childdef = groupChildGetDef(def, child, true);
															if(childdef->name_uid != -1)
																eaPush(&defs, childdef);
														}
														FOR_EACH_END;
													}
													else if(def->name_uid != -1)
													{
														filelog_printf(	"overlappingregionobjects.log", 
																		"%s overlapped by %s (%d) at %.2f %.2f %.2f",
																		all_layers[l]->filename,
																		def->name_str,
																		def->name_uid,
																		vecParamsXYZ(def->bounds.mid));
													}
												}
											}
										}
									}
								}
								filelog_printf("overlappingRegions.log", "File1: %s, File2: %s, Overlapping regions \"%s\" and \"%s\"", all_layers[k]->filename, all_layers[l]->filename, region1->name?region1->name:"default", region2->name?region2->name:"default");
								found_layers = true;
							}
						}
					}

					if (!found_layers && primary_map)
					{
						if(!g_isContinuousBuilder)
							ErrorFilenamef(zmapGetFilename(primary_map), "Overlapping regions \"%s\" and \"%s\" found!", region1->name?region1->name:"default", region2->name?region2->name:"default");
						filelog_printf("overlappingRegions.log", "File: %s, Overlapping regions \"%s\" and \"%s\" found!", zmapGetFilename(primary_map), region1->name?region1->name:"default", region2->name?region2->name:"default");
					}
				}
			}
		}
	}

	eaDestroy(&all_layers);
}

static void clearCellTrees(ZoneMap *zmap, bool open_trackers)
{
	WorldRegion **all_regions = NULL;
	int i, j;

	all_regions = worldGetAllWorldRegions();

	// This has to be done in two steps in case data changed from one region to another.
	// First, free all world cells that might be referencing the regions.
	for (i = 0; i < eaSize(&world_grid.active_map->layers); i++)
		layerTrackerClose(world_grid.active_map->layers[i]);
	for (i = 0; i < eaSize(&all_regions); ++i)
	{
		WorldRegion *region = all_regions[i];
		if (!region->zmap_parent || !zmap || zmap == region->zmap_parent)
		{
			worldCellFree(region->root_world_cell);
			worldCellFree(region->temp_world_cell);
			region->root_world_cell = NULL;
			region->temp_world_cell = NULL;
			region->preloaded_cell_data = false;
		}
	}

	// Now clear the region data.
	for (i = 0; i < eaSize(&all_regions); ++i)
	{
		WorldRegion *region = all_regions[i];
		if (wl_state.free_region_graphics_data)
			wl_state.free_region_graphics_data(region, false);
		region->graphics_data = NULL;
		region->world_bounds.needs_update = true;
		region->terrain_bounds.needs_update = true;

		if (!region->zmap_parent || !zmap || zmap == region->zmap_parent)
		{
			if (region->room_conn_graph)
				roomConnGraphDestroy(region->room_conn_graph);
			region->room_conn_graph = NULL;
		}
	}

	updateRegionBounds(true);

	for (i = 0; i < eaSize(&all_regions); ++i)
	{
		WorldRegion *region = all_regions[i];

		// world objects
		if (region->world_bounds.world_min[0] > region->world_bounds.world_max[0] || 
			region->world_bounds.world_min[1] > region->world_bounds.world_max[1] || 
			region->world_bounds.world_min[2] > region->world_bounds.world_max[2])
		{
// 			setVec3same(region->world_bounds.world_min, 0);
// 			setVec3same(region->world_bounds.world_max, 0);
		}
		else if (!region->zmap_parent || !zmap || zmap == region->zmap_parent)
		{
			region->root_world_cell = worldCellCreate(region, NULL, &region->world_bounds.cell_extents);
		}
	}

	if (open_trackers)
	{
		for (i = 0; i < eaSize(&world_grid.maps); i++)
		{
			for (j = 0; j < eaSize(&world_grid.maps[i]->layers); ++j)
			{
				if (world_grid.maps[i]->layers[j])
					layerTrackerOpen(world_grid.maps[i]->layers[j]);
			}
		}
	}
}

void worldBuildCellTree(ZoneMap *zmap, bool open_trackers)
{
	WorldRegion **all_regions = NULL;
	int i, j;


	// make sure all regions are created
	for (i = 0; i < eaSize(&world_grid.maps); i++)
	{
		for (j = 0; j < eaSize(&world_grid.maps[i]->layers); ++j)
		{
			if (world_grid.maps[i]->layers[j])
				layerGetWorldRegion(world_grid.maps[i]->layers[j]);
		}
	}

	all_regions = worldGetAllWorldRegions();

	// validate data, calculate world extents in primary grid space

	loadstart_printf("Building WorldCell tree...");

	for (i = 0; i < eaSize(&all_regions); ++i)
	{
		WorldRegion *region = all_regions[i];
		copyVec3(region->binned_bounds.cell_extents.min_block, region->world_bounds.cell_extents.min_block);
		copyVec3(region->binned_bounds.cell_extents.max_block, region->world_bounds.cell_extents.max_block);
		setVec3same(region->terrain_bounds.cell_extents.min_block, S32_MAX);
		setVec3same(region->terrain_bounds.cell_extents.max_block, S32_MIN);
	}

	for (i = 0; i < eaSize(&world_grid.maps); i++)
	{
		for (j = 0; j < eaSize(&world_grid.maps[i]->layers); ++j)
		{
			if (world_grid.maps[i]->layers[j])
			{
				Vec3 layer_min, layer_max;
				WorldRegion *region = layerGetWorldRegion(world_grid.maps[i]->layers[j]);

				// world objects
				layerGetBounds(world_grid.maps[i]->layers[j], layer_min, layer_max);
				if (layer_min[0] <= layer_max[0] && layer_min[1] <= layer_max[1] && layer_min[2] <= layer_max[2])
				{
					BlockRange range = {0};
					worldPosToGridPos(layer_min, range.min_block, CELL_BLOCK_SIZE);
					worldPosToGridPos(layer_max, range.max_block, CELL_BLOCK_SIZE);

					MINVEC3(region->world_bounds.cell_extents.min_block, range.min_block, region->world_bounds.cell_extents.min_block);
					MAXVEC3(region->world_bounds.cell_extents.max_block, range.max_block, region->world_bounds.cell_extents.max_block);
				}

				// terrain
				layerGetTerrainBounds(world_grid.maps[i]->layers[j], layer_min, layer_max);
				if (layer_min[0] <= layer_max[0] && layer_min[1] <= layer_max[1] && layer_min[2] <= layer_max[2])
				{
					BlockRange range = {0};
					worldPosToGridPos(layer_min, range.min_block, GRID_BLOCK_SIZE);
					worldPosToGridPos(layer_max, range.max_block, GRID_BLOCK_SIZE);

					MINVEC3(region->terrain_bounds.cell_extents.min_block, range.min_block, region->terrain_bounds.cell_extents.min_block);
					MAXVEC3(region->terrain_bounds.cell_extents.max_block, range.max_block, region->terrain_bounds.cell_extents.max_block);
				}
			}
		}
	}
	
	for (i = 0; i < eaSize(&all_regions); ++i)
	{
		WorldRegion *region = all_regions[i];
		IVec3 range_size;
		int size;

		// world objects
		if (region->world_bounds.cell_extents.min_block[0] > region->world_bounds.cell_extents.max_block[0] ||
			region->world_bounds.cell_extents.min_block[1] > region->world_bounds.cell_extents.max_block[1] ||
			region->world_bounds.cell_extents.min_block[2] > region->world_bounds.cell_extents.max_block[2])
		{
			setVec3same(region->world_bounds.cell_extents.min_block, 0);
			setVec3same(region->world_bounds.cell_extents.max_block, 0);
		}

		rangeSize(&region->world_bounds.cell_extents, range_size);
		size = MAX(range_size[0], range_size[1]);
		MAX1(size, range_size[2]);
		region->world_bounds.cell_extents.max_block[0] += pow2(size) - range_size[0];
		region->world_bounds.cell_extents.max_block[1] += pow2(size) - range_size[1];
		region->world_bounds.cell_extents.max_block[2] += pow2(size) - range_size[2];

		// terrain
		if (region->terrain_bounds.cell_extents.min_block[0] > region->terrain_bounds.cell_extents.max_block[0] ||
			region->terrain_bounds.cell_extents.min_block[2] > region->terrain_bounds.cell_extents.max_block[2])
		{
			setVec3same(region->terrain_bounds.cell_extents.min_block, 0);
			setVec3same(region->terrain_bounds.cell_extents.max_block, 0);
		}

		rangeSize(&region->terrain_bounds.cell_extents, range_size);
		size = MAX(range_size[0], range_size[2]);
		region->terrain_bounds.cell_extents.max_block[0] += pow2(size) - range_size[0];
		region->terrain_bounds.cell_extents.min_block[1] = 0;
		region->terrain_bounds.cell_extents.max_block[1] = 0;
		region->terrain_bounds.cell_extents.max_block[2] += pow2(size) - range_size[2];
	}

	// reset cell tree
	clearCellTrees(zmap, open_trackers);

	loadend_printf(" done.");
}

void worldUpdateBounds(bool recalc_all, bool close_trackers)
{
	WorldRegion **all_regions = NULL;
	int i, j, k;
	BlockRange *world_cell_extents;
	BlockRange *terrain_cell_extents;
	bool need_rebuild = false;

	worldCellSetEditable();

	// ensure all named regions exist
	for (i = 0; i < eaSize(&world_grid.maps); i++)
	{
		for (j = 0; j < eaSize(&world_grid.maps[i]->layers); ++j)
			layerGetWorldRegion(world_grid.maps[i]->layers[j]);
	}

	all_regions = worldGetAllWorldRegions();

	world_cell_extents = _alloca(eaSize(&all_regions) * sizeof(BlockRange));
	terrain_cell_extents = _alloca(eaSize(&all_regions) * sizeof(BlockRange));

	for (i = 0; i < eaSize(&all_regions); ++i)
	{
		copyVec3(all_regions[i]->binned_bounds.cell_extents.min_block, world_cell_extents[i].min_block);
		copyVec3(all_regions[i]->binned_bounds.cell_extents.max_block, world_cell_extents[i].max_block);
		setVec3same(terrain_cell_extents[i].min_block, S32_MAX);
		setVec3same(terrain_cell_extents[i].max_block, S32_MIN);
	}

	for (i = 0; i < eaSize(&world_grid.maps); i++)
	{
		ZoneMap *zmap = world_grid.maps[i];

		if (recalc_all)
		{
			zmapRecalcBounds(zmap, close_trackers);
		}
		else
		{
			zmapUpdateBounds(zmap);
			if (close_trackers)
			{
				for (k = 0; k < eaSize(&zmap->layers); k++)
					layerTrackerUpdate(zmap->layers[k], true, false);
			}
		}

		for (j = 0; j < eaSize(&zmap->layers); ++j)
		{
			Vec3 layer_min, layer_max;
			WorldRegion *region = layerGetWorldRegion(zmap->layers[j]);
			int idx = eaFind(&all_regions, region);

			if(!zmap->layers[j])
				continue;

			// world objects
			layerGetBounds(zmap->layers[j], layer_min, layer_max);
			if (layer_min[0] <= layer_max[0] && layer_min[1] <= layer_max[1] && layer_min[2] <= layer_max[2])
			{
				BlockRange range = {0};
				worldPosToGridPos(layer_min, range.min_block, CELL_BLOCK_SIZE);
				worldPosToGridPos(layer_max, range.max_block, CELL_BLOCK_SIZE);

				MINVEC3(world_cell_extents[idx].min_block, range.min_block, world_cell_extents[idx].min_block);
				MAXVEC3(world_cell_extents[idx].max_block, range.max_block, world_cell_extents[idx].max_block);
			}

			// terrain
			layerGetTerrainBounds(zmap->layers[j], layer_min, layer_max);
			if (layer_min[0] <= layer_max[0] && layer_min[1] <= layer_max[1] && layer_min[2] <= layer_max[2])
			{
				BlockRange range = {0};
				worldPosToGridPos(layer_min, range.min_block, GRID_BLOCK_SIZE);
				worldPosToGridPos(layer_max, range.max_block, GRID_BLOCK_SIZE);

				MINVEC3(terrain_cell_extents[idx].min_block, range.min_block, terrain_cell_extents[idx].min_block);
				MAXVEC3(terrain_cell_extents[idx].max_block, range.max_block, terrain_cell_extents[idx].max_block);
			}
		}
	}

	for (i = 0; i < eaSize(&all_regions); ++i)
	{
		WorldRegion *region = all_regions[i];
		IVec3 range_size;
		int size;

		if (region->is_editor_region)
			continue;

		// world objects
		if (world_cell_extents[i].min_block[0] > world_cell_extents[i].max_block[0] ||
			world_cell_extents[i].min_block[1] > world_cell_extents[i].max_block[1] ||
			world_cell_extents[i].min_block[2] > world_cell_extents[i].max_block[2])
		{
			setVec3same(world_cell_extents[i].min_block, 0);
			setVec3same(world_cell_extents[i].max_block, 0);
		}

		rangeSize(&world_cell_extents[i], range_size);
		size = MAX(range_size[0], range_size[1]);
		MAX1(size, range_size[2]);
		world_cell_extents[i].max_block[0] += pow2(size) - range_size[0];
		world_cell_extents[i].max_block[1] += pow2(size) - range_size[1];
		world_cell_extents[i].max_block[2] += pow2(size) - range_size[2];

		if (!sameVec3(world_cell_extents[i].min_block, region->world_bounds.cell_extents.min_block) ||
			!sameVec3(world_cell_extents[i].max_block, region->world_bounds.cell_extents.max_block))
		{
			copyVec3(world_cell_extents[i].min_block, region->world_bounds.cell_extents.min_block);
			copyVec3(world_cell_extents[i].max_block, region->world_bounds.cell_extents.max_block);
			need_rebuild = true;
		}

		// terrain
		if (terrain_cell_extents[i].min_block[0] > terrain_cell_extents[i].max_block[0] ||
			terrain_cell_extents[i].min_block[1] > terrain_cell_extents[i].max_block[1] ||
			terrain_cell_extents[i].min_block[2] > terrain_cell_extents[i].max_block[2])
		{
			setVec3same(terrain_cell_extents[i].min_block, 0);
			setVec3same(terrain_cell_extents[i].max_block, 0);
		}

		rangeSize(&terrain_cell_extents[i], range_size);
		size = MAX(range_size[0], range_size[2]);
		terrain_cell_extents[i].max_block[0] += pow2(size) - range_size[0];
		terrain_cell_extents[i].min_block[1] = 0;
		terrain_cell_extents[i].max_block[1] = 0;
		terrain_cell_extents[i].max_block[2] += pow2(size) - range_size[2];

		if (!sameVec3(terrain_cell_extents[i].min_block, region->terrain_bounds.cell_extents.min_block) ||
			!sameVec3(terrain_cell_extents[i].max_block, region->terrain_bounds.cell_extents.max_block))
		{
			copyVec3(terrain_cell_extents[i].min_block, region->terrain_bounds.cell_extents.min_block);
			copyVec3(terrain_cell_extents[i].max_block, region->terrain_bounds.cell_extents.max_block);
			need_rebuild = true;
		}
	}

	if (need_rebuild || close_trackers)
		clearCellTrees(world_grid.active_map, true);
	else
		updateRegionBounds(false);

	if (close_trackers)
		++world_grid.file_reload_count;
}

void worldSetWorldLimits()
{
	F32 dynNodeLimitSqr;
	if (zmapInfoHasSpaceRegion(NULL))
	{
		dynNodeLimitSqr = MAX_PLAYABLE_DIST_ORIGIN_SQR_WITH_ANIMATION_IN_SPACE;
		g_worldBoundsMaxDist = MAX_PLAYABLE_COORDINATE_IN_SPACE;
	}
	else
	{
		dynNodeLimitSqr = MAX_PLAYABLE_DIST_ORIGIN_SQR;
		g_worldBoundsMaxDist = MAX_PLAYABLE_COORDINATE;
	}
	dynNodeSetWorldRangeLimit(dynNodeLimitSqr);
}

bool worldLoadZoneMapEx(ZoneMapInfo *zminfo, bool force_create_bins, bool load_from_source, bool clear_manifest_cache_after_loading)
{
	int i;
	ZoneMap *zmap;
	ThreadAgnosticMutex loadMutex = 0;
	char mutexName[MAX_PATH];
	char mutexNameLegal[MAX_PATH];

	
	if (wl_state.load_map_begin_end_callback)
		wl_state.load_map_begin_end_callback(true);

	if (!zminfo)
	{
		if (wl_state.controller_script_wait_map_load)
		{
			ControllerScript_Failed("Failed to load map.");
			wl_state.controller_script_wait_map_load = false;
		}

		return false;
	}

	if (wl_state.gfx_get_cluster_load_setting_callback && wl_state.gfx_cluster_set_cluster_state)
		wl_state.gfx_cluster_set_cluster_state(wl_state.gfx_get_cluster_load_setting_callback());

	triviaPrintf("ZoneMap", "%s", zmapInfoGetFilename(zminfo));
	loadstart_printf("Loading %s...", zminfo->map_name);

	sprintf(mutexName, "worldLoadZoneMap_%s", zminfo->filename);
	makeLegalMutexName(mutexNameLegal, mutexName);
	loadMutex = acquireThreadAgnosticMutex(mutexNameLegal);

	worldResetWorldGrid();

	if (!(zmap = zmapLoad(zminfo)))
	{
		worldResetWorldGrid();
		if (wl_state.controller_script_wait_map_load)
		{
			ControllerScript_Failed("Failed to load map.");
			wl_state.controller_script_wait_map_load = false;
		}
		if (loadMutex)
			releaseThreadAgnosticMutex(loadMutex);
		loadend_printf("failed.");
		return false;
	}


	// set active and primary maps
	eaPush(&world_grid.maps, zmap);
	eafPush3(&world_grid.map_offsets, zerovec3);

	assert(eaSize(&world_grid.maps)==1);
	assert(eafSize(&world_grid.map_offsets)==3);

	for (i = 0; i < eaSize(&zmap->map_info.secondary_maps); ++i)
	{
		ZoneMapInfo *zminfo2;
		ZoneMap *zmap2;

		PERFINFO_AUTO_START("Secondary Map", 1);
		zminfo2 = worldGetZoneMapByPublicName(zmap->map_info.secondary_maps[i]->map_name);
		if (!zminfo2)
		{
			PERFINFO_AUTO_STOP();
			continue;
		}
		if (!(zmap2 = zmapLoad(zminfo2)))
		{
			PERFINFO_AUTO_STOP();
			continue;
		}

		eaPush(&world_grid.maps, zmap2);
		if (!sameVec3(zmap->map_info.secondary_maps[i]->offset, zerovec3))
		{
			ErrorFilenamef(zmap->map_info.filename, "Secondary map \"%s\" has a non-zero offset, this is not yet supported.", zmap->map_info.secondary_maps[i]->map_name);
			eafPush3(&world_grid.map_offsets, zerovec3);
		}
		else
		{
			eafPush3(&world_grid.map_offsets, zmap->map_info.secondary_maps[i]->offset);
		}
		PERFINFO_AUTO_STOP();
	}

	PERFINFO_AUTO_START("LoadBins", 1);
	for (i = eaSize(&world_grid.maps) - 1; i >= 0; --i)
		worldCellLoadBins(world_grid.maps[i], i==0 && force_create_bins, load_from_source, i > 0);
	PERFINFO_AUTO_STOP();

	worldSetWorldLimits();

	if (clear_manifest_cache_after_loading)
		bflFreeManifestCache();

	worldSetActiveMap(zmap);
	
	beaconPauseDynConnQueueCheck(1);

	// Disable game callbacks during this nested running of the world grid for one frame
	// This avoids a problem where the game is falsely notified during
	wl_state.disable_game_callbacks = true;
	worldGridOncePerFrame();
	wl_state.disable_game_callbacks = false;

	beaconPauseDynConnQueueCheck(0);

	if(wlIsServer())
		worldCreateAllCollScenes();

	// Load beacons after scenes are created to avert asserts in beaconizer
	worldLoadBeacons();

	PERFINFO_AUTO_START("other callbacks", 1);

	for (i = 0; i < eaSize((cEArrayHandle*)&wl_state.load_map_callbacks); ++i)
		wl_state.load_map_callbacks[i](zmap);

	if (wl_state.load_map_game_callback)
		wl_state.load_map_game_callback(zmap);

	if (wl_state.load_map_civ_callback)
		wl_state.load_map_civ_callback(zmap);

	if (wl_state.load_map_bcn_callback)
		wl_state.load_map_bcn_callback(zmap);

	if(wl_state.load_map_editorserver_callback)
		wl_state.load_map_editorserver_callback(zmap);

	if (wl_state.load_map_begin_end_callback)
		wl_state.load_map_begin_end_callback(false);
	PERFINFO_AUTO_STOP();

	// Save external dependencies
	if (wlIsServer() && !isProductionMode())
	{
		char filename[MAX_PATH];
		char base_dir[MAX_PATH];
		BinFileListWithCRCs *list;
		PERFINFO_AUTO_START("Save external dependencies", 1);
		list = zmapGetExternalDepsList(zmap);
		worldGetServerBaseDir(zmapGetFilename(zmap), SAFESTR(base_dir));
		sprintf(filename, "%s.external_deps.bin", base_dir);
		ParserWriteBinaryFile(filename, NULL, parse_BinFileListWithCRCs, list, NULL, NULL, NULL, NULL, 0, 0, NULL, 0, 0);
		StructDestroy(parse_BinFileListWithCRCs, list);
		zmap->external_dependencies = NULL;
		binNotifyTouchedOutputFile(filename);
		PERFINFO_AUTO_STOP();
	}

	if (wlIsServer())
	{
		int time_column, usedfield_column;

		PERFINFO_AUTO_START("if server", 1);

		beaconRebuildBlocks(0, 1, 0);

		// This is the last place on the server in which we need uncompressed MaterialDatas
		materialDataReleaseAll();

		assert(ParserFindColumn(parse_ZoneMapInfo, "Time", &time_column));
		assert(ParserFindColumn(parse_ZoneMapInfo, "bfParamsSpecified", &usedfield_column));

		if (TokenIsSpecified(parse_ZoneMapInfo, time_column, zminfo, usedfield_column))
		{
			wlTimeSetScale(0);
			wlTimeSet(zmap->map_info.time);
		}
		else
		{
			wlTimeSetScale(1);
		}
		PERFINFO_AUTO_STOP();
	}

	if (wl_state.controller_script_wait_map_load)
	{
		ControllerScript_Succeeded();
		wl_state.controller_script_wait_map_load = false;
	}

	if (loadMutex)
		releaseThreadAgnosticMutex(loadMutex);
	loadend_printf("Done loading %s", zminfo->map_name);
	return true;
}

void worldControllerScriptWaitForMapLoad()
{
	wl_state.controller_script_wait_map_load = true;
}

void worldClearWorldLocks(U32 UID)
{
	bool need_reload = false;
	int i, j;

	// do nothing in production mode unless production editing is enabled
	if (isProductionMode() || isProductionEditMode())
		return;

	for (i = 0; i < eaSize(&world_grid.maps); ++i)
	{
		if (zmapInfoLocked(&world_grid.maps[i]->map_info) && zmapInfoGetLockOwner(&world_grid.maps[i]->map_info) == UID)
			need_reload = true;
		for (j = 0; j < eaSize(&world_grid.maps[i]->layers); ++j)
		{
			if (world_grid.maps[i]->layers[j]->lock_owner == UID)
				need_reload = true;
		}
	}

	if (need_reload)
	{
		printf("Agent logout. Reloading map.\n");
		worldLoadZoneMapEx(worldGetZoneMapByPublicName(zmapInfoGetPublicName(NULL)), false, false, false);
	}
}

//////////////////////////////////////////////////////////////////////////
// WorldFile

bool worldFileCanSave(WorldFile *file, const char *filename_override, bool do_timestamp_check)
{
	if (!filename_override)
		filename_override = file->fullname;

	if (!gimmeDLLQueryIsFileLockedByMeOrNew(filename_override))
	{
		Alertf("File \"%s\" is not checked out, not saving!", filename_override);
		return false;
	}

	if (do_timestamp_check && file->timestamp && file->timestamp != fileLastChanged(filename_override))
	{
		Alertf("File \"%s\" has changed on disk since it was loaded, not saving!", filename_override);
		return false;
	}

	return true;
}

void worldFileSetSaved(WorldFile *file)
{
	file->timestamp = bflFileLastChanged(file->fullname);
	file->unsaved = 0;
}

bool worldFileBlock(WorldFile *file)
{
    GimmeErrorValue ret;
	char save_filename[MAX_PATH];

	if (strEndsWith(file->fullname, MODELNAMES_EXTENSION))
		changeFileExt(file->fullname, ROOTMODS_EXTENSION, save_filename);
	else
		strcpy(save_filename, file->fullname);

    ret = gimmeDLLBlockFile(save_filename, "Operation in progress");
    if (ret != GIMME_NO_ERROR && ret != GIMME_ERROR_NOT_IN_DB && ret != GIMME_ERROR_ALREADY_DELETED)
    {
        Alertf("File \"%s\" could not be blocked.", file->fullname);
        return false;
    }

    return true;
}

bool worldFileUnblock(WorldFile *file)
{
    GimmeErrorValue ret;
	char save_filename[MAX_PATH];

	if (strEndsWith(file->fullname, MODELNAMES_EXTENSION))
		changeFileExt(file->fullname, ROOTMODS_EXTENSION, save_filename);
	else
		strcpy(save_filename, file->fullname);

    ret = gimmeDLLUnblockFile(save_filename);
    if (ret != GIMME_NO_ERROR && ret != GIMME_ERROR_NOT_IN_DB && ret != GIMME_ERROR_ALREADY_DELETED)
    {
        Alertf("File \"%s\" could not be unblocked.", file->fullname);
        return false;
    }

    return true;
}

bool worldFileCheckout(WorldFile *file)
{
	GimmeErrorValue ret;
	char save_filename[MAX_PATH];
	bool owned_locally = wlIsServer() == !file->client_owned;

	assert(owned_locally);
	assert(file->fullname);

	if (file->checked_out)
		return true;

	if (file->timestamp && file->timestamp != fileLastChanged(file->fullname))
	{
		Alertf("File \"%s\" has changed on disk since it was loaded, not checking out!", file->fullname);
		return false;
	}

	if (strEndsWith(file->fullname, MODELNAMES_EXTENSION))
		changeFileExt(file->fullname, ROOTMODS_EXTENSION, save_filename);
	else
		strcpy(save_filename, file->fullname);

	ret = gimmeDLLDoOperation(save_filename, GIMME_CHECKOUT, GIMME_QUIET);

	if (ret != GIMME_NO_ERROR && ret != GIMME_ERROR_NOT_IN_DB && ret != GIMME_ERROR_ALREADY_DELETED)
	{
		const char *lockee;
		if(file->failed_check_out)
			return false;
		if (ret == GIMME_ERROR_ALREADY_CHECKEDOUT && (lockee = gimmeDLLQueryIsFileLocked(save_filename))) {
			Alertf("File \"%s\" unable to be checked out, currently checked out by %s", save_filename, lockee);
		} else {
			Alertf("File \"%s\" unable to be checked out (%s)", save_filename, gimmeDLLGetErrorString(ret));
		}
		file->failed_check_out = 1;
		return false;
	}

	printf("Checked out file %s\n", save_filename);
	file->checked_out = 1;
	file->failed_check_out = 0;
	return true;
}


bool worldFileUndoCheckout(WorldFile *file)
{
	GimmeErrorValue ret;
	char save_filename[MAX_PATH];
	bool owned_locally = wlIsServer() == !file->client_owned;

	assert(owned_locally);
	assert(file->fullname);

	if (!file->checked_out)
		return true;

	if (strEndsWith(file->fullname, MODELNAMES_EXTENSION))
		changeFileExt(file->fullname, ROOTMODS_EXTENSION, save_filename);
	else
		strcpy(save_filename, file->fullname);

	ret = gimmeDLLDoOperation(save_filename, GIMME_UNDO_CHECKOUT, GIMME_QUIET);

	if (ret != GIMME_NO_ERROR)
	{
		Alertf("File \"%s\" unable to be reverted (%s)", save_filename, gimmeDLLGetErrorString(ret));
		return false;
	}

	printf("Undid check out on file %s\n", save_filename);
	file->checked_out = 0;
	file->failed_check_out = 0;
	return true;
}

bool worldFileCheckoutList(WorldFile **files)
{
    int i;
    WorldFile **files_array = NULL;
    char **names_array = NULL;
    GimmeErrorValue ret;
    for (i = 0; i < eaSize(&files); i++)
    {
        char save_filename[MAX_PATH];
        bool owned_locally = wlIsServer() == !files[i]->client_owned;

        assert(owned_locally);

        if (files[i]->checked_out)
            continue;

        if (files[i]->timestamp && files[i]->timestamp != fileLastChanged(files[i]->fullname))
        {
            Alertf("File \"%s\" has changed on disk since it was loaded, not checking out!", files[i]->fullname);
            return false;
        }

        if (strEndsWith(files[i]->fullname, MODELNAMES_EXTENSION))
            changeFileExt(files[i]->fullname, ROOTMODS_EXTENSION, save_filename);
        else
            strcpy(save_filename, files[i]->fullname);

        eaPush(&files_array, files[i]);
        eaPush(&names_array, strdup(save_filename));
    }

    ret = gimmeDLLDoOperations(names_array, GIMME_CHECKOUT, GIMME_QUIET);

    if (ret != GIMME_NO_ERROR && ret != GIMME_ERROR_NOT_IN_DB && ret != GIMME_ERROR_ALREADY_DELETED)
    {
        const char *lockee;
        for (i = 0; i < eaSize(&files_array); i++)
        {
            if(files[i]->failed_check_out)
                continue;
            if (ret == GIMME_ERROR_ALREADY_CHECKEDOUT)
            {
                if (!gimmeDLLQueryIsFileLockedByMeOrNew(names_array[i]))
                {
	                lockee = gimmeDLLQueryIsFileLocked(names_array[i]);
                    Alertf("File \"%s\" unable to be checked out, currently checked out by %s", names_array[i], lockee);
                }
            }
            files[i]->failed_check_out = 1;
        }
        if (ret != GIMME_ERROR_ALREADY_CHECKEDOUT)
        {
            Alertf("Files unable to be checked out (%s)", gimmeDLLGetErrorString(ret));
        }
        return false;
    }

    for (i = 0; i < eaSize(&files_array); i++)
    {
        printf("Checked out file %s\n", names_array[i]);
        files_array[i]->checked_out = 1;
        files_array[i]->failed_check_out = 0;
    }

    eaDestroy(&files_array);
    eaDestroyEx(&names_array, NULL);
        
    return true;
}

bool worldFileDeleteList(WorldFile **files)
{
    int i;
    WorldFile **files_array = NULL;
    char **names_array = NULL;
    GimmeErrorValue ret;
    for (i = 0; i < eaSize(&files); i++)
    {
        char save_filename[MAX_PATH];
        bool owned_locally = wlIsServer() == !files[i]->client_owned;

        assert(owned_locally);

        if (!files[i]->checked_out)
        {
            Alertf("File \"%s\" cannot be deleted before being checked out!!", files[i]->fullname);
            return false;
        }

        if (files[i]->timestamp && files[i]->timestamp != fileLastChanged(files[i]->fullname))
        {
            Alertf("File \"%s\" has changed on disk since it was loaded, not deleting!", files[i]->fullname);
            return false;
        }

        if (strEndsWith(files[i]->fullname, MODELNAMES_EXTENSION))
            changeFileExt(files[i]->fullname, ROOTMODS_EXTENSION, save_filename);
        else
            strcpy(save_filename, files[i]->fullname);

        eaPush(&files_array, files[i]);
        eaPush(&names_array, strdup(save_filename));
    }

    ret = gimmeDLLDoOperations(names_array, GIMME_DELETE, GIMME_QUIET);

    if (ret != GIMME_NO_ERROR && ret != GIMME_ERROR_NOT_IN_DB && ret != GIMME_ERROR_ALREADY_DELETED)
    {
        Alertf("Files unable to be deleted (%s)", gimmeDLLGetErrorString(ret));
        return false;
    }

    for (i = 0; i < eaSize(&files_array); i++)
    {
        printf("Deleted file %s\n", names_array[i]);
    }

    eaDestroy(&files_array);
    eaDestroyEx(&names_array, NULL);
        
    return true;
}

const char *worldFileGetLockee(WorldFile *file)
{
	char save_filename[MAX_PATH];
	bool owned_locally = wlIsServer() == !file->client_owned;

	assert(owned_locally);

	if (file->checked_out)
		return NULL;

	if (file->timestamp && file->timestamp != fileLastChanged(file->fullname))
	{
		return NULL;
	}

	if (strEndsWith(file->fullname, MODELNAMES_EXTENSION))
		changeFileExt(file->fullname, ROOTMODS_EXTENSION, save_filename);
	else
		strcpy(save_filename, file->fullname);

    return  gimmeDLLQueryIsFileLocked(save_filename);
}

bool worldFileLoad(WorldFile *file, ParseTable *table, void *data, const char *parent_filename, bool exists_error, bool persist)
{
	if (isDevelopmentMode() && !fileExists(file->fullname))
	{
		if (!exists_error)
			return false;
		if (parent_filename)
			ErrorFilenamef(parent_filename, "File not found: \"%s\"", file->fullname);
		else
			Alertf("File not found: \"%s\"", file->fullname);
		return false;
	}

	if (ParserLoadFiles(NULL, file->fullname, persist?worldMakeBinName(file->fullname):NULL, PARSER_BINS_ARE_SHARED, table, data))
	{
		worldFileSetSaved(file);
		return true;
	}

	return false;
}

bool worldFileSaveAs(WorldFile *file, ParseTable *table, void *data, const char *filename, bool force)
{
	const char *orig_filename = NULL;
	bool same_file = true;

	if (!filename)
		filename = file->fullname;

	if (!force && !file->unsaved)
		return true;

	if (!force && !worldFileCanSave(file, NULL, true))
		return false;

	if (stricmp(file->fullname, filename) != 0)
	{
		same_file = false;
		orig_filename = file->fullname;
		file->fullname = allocAddFilename(filename);
	}

	if (ParserWriteTextFile(filename, table, data, 0, 0))
	{
		bool owned_locally = wlIsServer() == !file->client_owned;
		if (same_file && owned_locally)
			worldFileSetSaved(file);
		if (!same_file)
			file->fullname = orig_filename;
		return true;
	}

	Alertf("Unable to write to file \"%s\"!", filename);
	if (!same_file)
		file->fullname = orig_filename;
	return false;
}

bool worldFileSave(WorldFile *file, ParseTable *table, void *data)
{
	return worldFileSaveAs(file, table, data, NULL, false);
}

void worldFileModify(WorldFile *file, bool increment_mod_time, bool do_checkout)
{
	bool owned_locally = wlIsServer() == !file->client_owned;

	if (!world_grid.loading && do_checkout && owned_locally)
	{
		worldFileCheckout(file);
	}
	if (increment_mod_time && wlIsServer())
	{
		assert(owned_locally);
		worldIncModTime();
	}
	if (!world_grid.loading)
		file->unsaved = 1;
	file->mod_time = worldGetModTime();
}

//////////////////////////////////////////////////////////////////////////

ZoneMap *worldGetLoadedZoneMapByName(const char *zmap_name)
{
	bool is_filename;
	int i;

	if (!zmap_name)
		return NULL;

	is_filename = !!strchr(zmap_name, '/');
	for (i = 0; i < eaSize(&world_grid.maps); ++i)
	{
		ZoneMap *zmap = world_grid.maps[i];
		const char *name;
		if (!zmap)
			continue;
		name = is_filename ? zmapGetFilename(zmap) : zmapInfoGetPublicName(zmapGetInfo(zmap));
		if (name && stricmp(name, zmap_name)==0)
			return zmap;
	}

	return NULL;
}

ZoneMap *worldGetLoadedZoneMapByIndex(int index)
{
	assert(index >= 0 && index < eaSize(&world_grid.maps));
	return world_grid.maps[index];
}

int worldGetLoadedZoneMapCount(void)
{
	return eaSize(&world_grid.maps);
}

void worldSetActiveMap(ZoneMap *zmap)
{
	if (!zmap)
	{
		int i;
		for (i = eaSize(&world_grid.maps)-1; i >= 0; --i)
		{
			if (world_grid.maps[i])
				zmap = world_grid.maps[i];
		}
	}

	world_grid.active_map = zmap;
}

void worldSetActiveMapByName(const char *zmap_name)
{
	if (zmap_name)
	{
		bool is_filename = !!strchr(zmap_name, '/');
		int i;

		for (i = 0; i < eaSize(&world_grid.maps); ++i)
		{
			ZoneMap *zmap = world_grid.maps[i];
			const char *name;
			if (!zmap)
				continue;
			name = is_filename ? zmapGetFilename(zmap) : zmapInfoGetPublicName(zmapGetInfo(zmap));
			if (name && stricmp(name, zmap_name)==0)
			{
				worldSetActiveMap(zmap);
				return;
			}
		}

		Alertf("%s: map %s not found.", __FUNCTION__, zmap_name);
	}

	worldSetActiveMap(NULL);
}

//////////////////////////////////////////////////////////////////////////

int worldIncrementDefAccessTime(void)
{
	return ++world_grid.def_access_time;
}

int worldIsLoading(void)
{
	return world_grid.loading;
}

U32 worldGetModTime(void)
{
	return world_grid.mod_time;
}

U32 worldIncModTime(void)
{
	return ++world_grid.mod_time_MUTABLE;
}

void worldSetModTime(U32 iTime)
{
	world_grid.mod_time_MUTABLE = iTime;
}

//DO NOT USE THIS unless you have talked to Tom and are very certain you know what you're doing
void worldSetModTime_AsyncOK(U32 iTime)
{
	world_grid.mod_time_MUTABLE = iTime;
}
//////////////////////////////////////////////////////////////////////////

static bool isReservedUID(int uid)
{
	return uid == 0 || uid == 1;
}

static int makeIntValidUID( int name_uid, bool is_objlib, bool is_core )
{
	if( is_objlib ) {
		name_uid |= 0x80000000;
	} else {
		name_uid &= ~0x80000000;
	}
	if( is_core ) {
		name_uid |= 1;
	} else {
		name_uid &= ~1;
	}
	assert(groupIsObjLibUID(name_uid) == is_objlib);
	assert(groupIsCoreUID(name_uid) == is_core);

	return name_uid;
}

static U32 g_prevTimeSecs = 0;
int worldGenerateDefNameUID(const char *name_str, GroupDef *def, bool is_objlib, bool is_core)
{
	const char* hostName = getHostName();
	U32 time_secs, hash, tries = 0;
	int name_uid;
	cryptAdler32Init();
	cryptAdler32Update(name_str, (int)strlen(name_str));
	time_secs = timeSecondsSince2000();
	if (time_secs <= g_prevTimeSecs)
	{
		time_secs = g_prevTimeSecs + 1;
	}
	g_prevTimeSecs = time_secs;
	cryptAdler32Update((void *)&time_secs, sizeof(time_secs));
	cryptAdler32Update((void *)hostName, (int)strlen(hostName));
	cryptAdler32Update((void *)&tries, sizeof(tries));
	hash = cryptAdler32Final();
	tries++;
	name_uid = makeIntValidUID( *((int *)&hash), is_objlib, is_core );

	while (  isReservedUID(name_uid) || 
			 (is_objlib && objectLibraryGetGroupDef(name_uid, false)) ||
			(def && groupLibFindGroupDef(def->def_lib, name_uid, true)))
	{
		cryptAdler32Init();
		cryptAdler32Update((void *)&name_uid, sizeof(name_uid));
		time_secs = randInt(S32_MAX);
		cryptAdler32Update((void *)&time_secs, sizeof(time_secs));
		cryptAdler32Update((void *)hostName, (int)strlen(hostName));
		cryptAdler32Update((void *)&tries, sizeof(tries));
		hash = cryptAdler32Final();
		tries++;
		name_uid = makeIntValidUID( *((int *)&hash), is_objlib, is_core );

		assert(tries < 500);
	}

	return name_uid;
}

static void countDefs(GroupDef *def, StashTable def_ref_count_table, bool objlib)
{
	int count, i;
	GroupChild **def_children;

	if (!def)
		return;
	if (!objlib && def->name_uid <= 0)
		return;

	def_children = groupGetChildren(def);


	if (!objlib || groupIsPrivate(def))
	{
		if (!stashIntFindInt(def_ref_count_table, def->name_uid, &count))
			count = 0;
		count++;
		stashIntAddInt(def_ref_count_table, def->name_uid, count, true);
	}

	for (i = 0; i < eaSize(&def_children); ++i)
	{
		GroupDef *child_def = groupChildGetDef(def, def_children[i], false);
		if (!objlib || groupIsPrivate(child_def))
			countDefs(child_def, def_ref_count_table, objlib);
	}
}

void groupDefRename(GroupDef *def, const char *new_def_name)
{
	GroupDef *objlib_def = NULL;

	PERFINFO_AUTO_START_FUNC();

	assert(!strchr(new_def_name,'/') && !strchr(new_def_name,'\\'));

	if (def->name_str)
	{
		// Remove from library
		stashRemoveInt(def->def_lib->def_name_table, def->name_str, NULL);
	}

	// Rename
	def->name_str = allocAddString(new_def_name);
	assert(def->name_str);

	if (def->root_id == 0)
	{
		// Add back into library
		assert(stashAddInt(def->def_lib->def_name_table, def->name_str, def->name_uid, false));
	}

	groupDefFixupMessages(def);

	PERFINFO_AUTO_STOP();
}

// void gfileModify(GroupFile *file, bool increment_mod_time, bool do_checkout)
// {
// 	bool owned_locally = file && (wlIsServer() == !file->file_base.client_owned);
// 
// 	if (file && file->dummy) return;
// 
// 	if (wlIsClient() && file && !file->zmap_layer && !validateFileLock(file))
// 	{
// 		Alertf("\"%s\" has not been checked out!", gfileGetFilename(file));
// 		return;
// 	}
// 
// 	if (file && !world_grid.loading && do_checkout && wlIsServer())
// 	{
// 		checkoutGroupFile(file);
// 	}
// 	if (increment_mod_time)
// 	{
// 		assert(file);
// 		if (wlIsServer())
// 			world_grid.mod_time++;
// 		else if (file->zmap_layer)
// 			layerIncrementModTime(file->zmap_layer);
// 		else
// 			world_grid.obj_lib.lock_mod_time++;
// 	}
// 	if (file)
// 	{
// 		if (wlIsServer())
// 			file->file_base.mod_time = world_grid.mod_time;
// 		else if (file->zmap_layer)
// 			file->file_base.mod_time = file->zmap_layer->lock_mod_time;
// 		else
// 			file->file_base.mod_time = world_grid.obj_lib.lock_mod_time;
// 
// 		if (!world_grid.loading)
// 			file->file_base.unsaved = 1;
// 	}
// }

U32 worldCountGroupDefs(GroupDef *def)
{
	int i, j;
	U32 total = 0;

	for (i = 0; i < eaSize(&world_grid.maps); i++)
	{
		ZoneMap *zmap = world_grid.maps[i];
		for (j = 0; j < eaSize(&zmap->layers); ++j)
		{
			ZoneMapLayer *layer = zmap->layers[j];
			GroupDef *layer_def = layerGetDef(layer);
			if (layer_def)
				total += defContainCount(layer_def, def);
		}
	}

	return total;
}

void groupDefRefresh(GroupDef *def)
{
	static U32 sGroupRefreshTime = 0;

	def->group_refresh_time = sGroupRefreshTime++;
	def->bounds_valid = 0;
}

void groupDefModify(GroupDef *def, int child_idx, bool user_change)
{
	if (user_change && wlIsClient() && def && !groupIsEditable(def))
	{
		Alertf("\"%s\" has not been checked out!", def->filename);
		return;
	}

	worldIncModTime();

	if (def)
	{
		if (def->def_lib->zmap_layer && user_change)
			layerIncrementModTime(def->def_lib->zmap_layer);

		if (child_idx == UPDATE_GROUP_PROPERTIES)
		{
			def->group_mod_time = worldGetModTime();
		}
		else if (child_idx >= 0)
		{
			assert(child_idx < eaSize(&def->children));
			def->children[child_idx]->child_mod_time = worldGetModTime();
		}
		def->all_mod_time = world_grid.mod_time;
		def->bounds_valid = 0;
		if (user_change)
			def->save_mod_time = world_grid.mod_time;
	}
}

static void freeInstanceBuffer(GroupInstanceBuffer *instance_buffer)
{
	if (!instance_buffer)
		return;
	eaDestroyEx(&instance_buffer->instances, worldFreeModelInstanceInfo);
	free(instance_buffer);
}

void groupDefClear(GroupDef *def)
{
	if (!def)
		return;

	PERFINFO_AUTO_START_FUNC();

	eaDestroyEx(&def->texture_swaps, freeTextureSwap);
	eaDestroyEx(&def->material_swaps, freeMaterialSwap);
	stashTableDestroy(def->path_to_name);
	stashTableDestroy(def->name_to_path);
	stashTableDestroy(def->name_to_instance_data);

	if (def->property_structs.client_volume.sky_volume_properties && wl_state.notify_sky_group_freed_func)
		wl_state.notify_sky_group_freed_func(&def->property_structs.client_volume.sky_volume_properties->sky_group);

	eaDestroyEx(&def->spline_params, NULL);
	eaDestroyEx(&def->instance_buffers, freeInstanceBuffer);

	PERFINFO_AUTO_STOP_FUNC();
}

void groupDefFree(GroupDef *def)
{
	GroupDefLib *def_lib;

	if (!def)
		return;

	assert(!def->def_lib || def->def_lib->zmap_layer || def->def_lib->editing_lib || def->def_lib->dummy);

	PERFINFO_AUTO_START_FUNC();

	def_lib = def->def_lib;
	if (def_lib)
	{
		stashIntRemovePointer(def_lib->defs, def->name_uid, NULL);
		if (def->name_str)
			stashRemoveInt(def_lib->def_name_table, def->name_str, NULL);
	}

	groupDefClear(def);
	StructDestroy(parse_GroupDef, def);

	PERFINFO_AUTO_STOP_FUNC();
}

const char *groupDefGetFilename(GroupDef *def)
{
	return def->filename;
}

U32 groupGenerateGroupChildUID(GroupDef *parent, GroupChild **def_children, int idx_in_parent)
{
	U32 uid, max_uid = 0;
	bool is_ok = true;
	int i;

	if (!def_children)
		def_children = parent->children;

	uid = idx_in_parent + 1;

	for (i = 0; i < eaSize(&def_children); i++)
	{
		if (i == idx_in_parent)
			continue;
		if (def_children[i]->uid_in_parent == uid)
			is_ok = false;
		MAX1(max_uid, def_children[i]->uid_in_parent);
	}

	if (!is_ok)
		uid = max_uid + 1;

	return uid;
}

void groupDefSetChildCount(GroupDef *parent, GroupChild ***def_children, int new_child_count)
{
	// use eaSetSizeStruct?
	if (!def_children)
		def_children = &parent->children;

	if (new_child_count < 0)
		return;

	if (new_child_count < eaSize(def_children))
	{
		int i;
		for (i = new_child_count; i < eaSize(def_children); i++)
		{
			StructDestroy(parse_GroupChild, (*def_children)[i]);
		}
		eaSetSize(def_children, new_child_count);
		return;
	}
	else if (new_child_count > eaSize(def_children))
	{
		while (eaSize(def_children) < new_child_count)
		{
			GroupChild *child = StructCreate(parse_GroupChild);
			child->uid_in_parent = eaSize(def_children);
			eaPush(def_children, child);
		}
	}
	
 	parent->bounds_valid = 0; // This really needs to be recursive up, but that's not actually possible.
}

GroupChild *groupChildInitialize(GroupDef *parent, int child_idx, GroupDef *child, const Mat4 mat, F32 scale, U32 seed, U32 uid_in_parent)
{
	GroupChild *groupChild;
	GroupChild ***def_children = &parent->children;
	assert(def_children);

	// There is no evidence that a fixup here is necessary.  It's very very slow, and if we find a case, it should
	// be handled elsewhere.  In general, THIS is the function that is responsible for setting up a GroupChild, and
	// so we should never receive a parent that has any un-fixed children other than child_idx.  RMARR [1/25/13]
	//groupFixupChildren(parent,child_idx);
	
	if (child_idx < 0)
		child_idx = eaSize(def_children);

	if (child_idx >= eaSize(def_children))
		groupDefSetChildCount(parent, def_children, child_idx + 1);

	groupChild = (*def_children)[child_idx];
	assert(groupChild);

	if (mat)
		copyMat4(mat, groupChild->mat);
	else
		identityMat4(groupChild->mat);

	groupChild->scale = scale;

	groupChild->seed = seed;
	groupChild->uid_in_parent = uid_in_parent?uid_in_parent:groupGenerateGroupChildUID(parent, *def_children, child_idx);
	if (child)
	{
		GroupDef *child_ret;
		groupChild->name_uid = child->name_uid;
		groupChild->name = NULL; // Name field is now deprecated; remove it
		groupChild->debug_name = child->name_str;

		 // Ensure that at creation time at least, the reference is valid
		child_ret = groupChildGetDef(parent, groupChild, true);
		if (groupIsObjLib(child) &&
			(parent->def_lib->editing_lib || parent->def_lib->zmap_layer || parent->def_lib->dummy))
		{
			GroupDef* tmp_child = objectLibraryGetEditingCopy(child, true, false);
			assert(tmp_child == child_ret);
			child = tmp_child;
		}
		assert(child_ret == child);
	}

	// sanity check
	assert(validateMat4(groupChild->mat));
	assert(isNonZeroMat3(groupChild->mat));
	devassert(groupChild->mat[3][0] > -1e6);
	devassert(groupChild->mat[3][1] > -1e6);
	devassert(groupChild->mat[3][2] > -1e6);
	devassert(groupChild->mat[3][0] < 1e6);
	devassert(groupChild->mat[3][1] < 1e6);
	devassert(groupChild->mat[3][2] < 1e6);

	return groupChild;
}


// inputs are in primary map space
S32 worldCollideRay(int iPartitionIdx,
					const Vec3 source,
				    const Vec3 target,
				    U32 filterBits,
				    WorldCollCollideResults* results)
{
	return wcRayCollide(worldGetActiveColl(iPartitionIdx), source, target, filterBits, results);
}

// inputs are in primary map space
S32 worldCollideRayMultiResult(	int iPartitionIdx,
								const Vec3 source,
								const Vec3 target,
								U32 filterBits,
								WorldCollCollideResultsCB callback,
								void* userPointer)
{
	return wcRayCollideMultiResult(	worldGetActiveColl(iPartitionIdx),
									source,
									target,
									filterBits,
									callback,
									userPointer,
									NULL);
}

// inputs are in primary map space
S32 worldCollideCapsule(int iPartitionIdx,
						const Vec3 source,
						const Vec3 target,
						U32 filterBits,
						WorldCollCollideResults* results)
{
	return wcCapsuleCollide(worldGetActiveColl(iPartitionIdx), source, target, filterBits, results);
}

S32 worldCollideCapsuleEx(int iPartitionIdx,
						  const Vec3 source,
						  const Vec3 target,
						  U32 filterBits,
						  F32 height,
						  F32 radius,
						  WorldCollCollideResults* results)
{
	return wcCapsuleCollideHR(worldGetActiveColl(iPartitionIdx), source, target, filterBits, height, radius, results);
}

// returns whether it called the edit callback
bool worldCheckForNeedToOpenCells(void)
{
	if (wlIsServer())
	{
		bool cellsOpened = false;
		WorldRegion **all_regions = worldGetAllWorldRegions();
		int i;

		for (i = 0; i < eaSize(&all_regions); ++i)
		{
			if (all_regions[i]->world_bounds.needs_update || all_regions[i]->terrain_bounds.needs_update)
			{
				int iPartitionIdx;
				for(iPartitionIdx=0; iPartitionIdx<eaSize(&world_grid.eaWorldColls); ++iPartitionIdx) {
					if (!world_grid.eaWorldColls[iPartitionIdx]) 
					{
						continue;
					}

					loadstart_printf("Opening all world cells...");
					worldGridOpenAllCellsForCameraPos(iPartitionIdx, all_regions[i], wl_state.last_camera_frustum.cammat[3], 1, NULL, true, false, false, false, 0, 1.0f);
					loadend_printf(" done.");

					cellsOpened = true;
				}
			}
		}

		// Callback after change
		if (cellsOpened && !wl_state.disable_game_callbacks && wl_state.edit_map_game_callback)
		{
			wl_state.edit_map_game_callback(worldGetPrimaryMap());
			return true;
		}
	}

	return false;
}

void worldCheckForNeedToOpenCellsOnPartition(int iPartitionIdx)
{
	WorldRegion **all_regions;
	int i;

	PERFINFO_AUTO_START_FUNC();
	loadstart_printf("Opening all world cells on partition %d...", iPartitionIdx);

	all_regions = worldGetAllWorldRegions();
	for (i = 0; i < eaSize(&all_regions); ++i)
	{
		worldGridOpenAllCellsForCameraPos(iPartitionIdx, all_regions[i], wl_state.last_camera_frustum.cammat[3], 1, NULL, true, true, false, false, 0, 1.0f);
	}

	loadend_printf(" done.");
	PERFINFO_AUTO_STOP();
}

void worldCloseCellsOnPartition(int iPartitionIdx)
{
	WorldRegion **all_regions;
	int i;

	PERFINFO_AUTO_START_FUNC();
	loadstart_printf("Closing all world cells on partition %d...", iPartitionIdx);

	all_regions = worldGetAllWorldRegions();
	for (i = 0; i < eaSize(&all_regions); ++i)
	{
		WorldRegion *region = all_regions[i];
		worldCloseCellOnPartition(iPartitionIdx, region->root_world_cell); 

		terrainCloseAtlasesForPartition(iPartitionIdx, region->atlases);
	}

	loadend_printf(" done.");
	PERFINFO_AUTO_STOP();
}

void worldGridClearTempTrackers(void)
{
	last_temp_tracker_count = eaSize(&world_grid.temp_trackers);
	eaClearEx(&world_grid.temp_trackers, destroyTempTracker);
}

void worldGridOncePerFrame(void)
{
	int i;

	PERFINFO_AUTO_START_FUNC();

	if (world_grid.deferred_load_map)
	{
		if (worldLoadZoneMapByName(world_grid.deferred_load_map))
		{
			SAFE_FREE(world_grid.deferred_load_map);
		}
	}

	objectLibraryOncePerFrame();

	if (world_grid.active_map)
	{
		roomConnGraphUpdateAllRegions(world_grid.active_map);

		if (world_grid.active_map->tracker_update_time != world_grid.mod_time)
			zmapTrackerUpdate(world_grid.active_map, false, false);

		if (wlIsServer())
		{
			WorldRegion **region_list = NULL;
			for (i = 0; i < eaSize(&world_grid.active_map->layers); i++)
				if (world_grid.active_map->layers[i]->reload_pending)
				{
					ZoneMapLayer *layer = world_grid.active_map->layers[i];
					printf("Reloading layer \"%s\".\n", layer->filename);

					layerReload(layer, false);
					if (layer->terrain.need_bins)
					{
						WorldRegion *region = layerGetWorldRegion(layer);
						terrainSaveBins(layer, i, eaSize(&world_grid.active_map->layers));
						if (eaFind(&region_list, region) == -1)
							eaPush(&region_list, region);
						layerReload(layer, false);
					}
					else if (layer->bin_status_callback)
					{
						layer->bin_status_callback(layer->bin_status_userdata, 1, 1);
						layer->bin_status_userdata = layer->bin_status_callback = NULL;
					}
					layer->reload_pending = false;
				}
			for (i = 0; i < eaSize(&region_list); i++)
			{
				BinFileList *file_list;
				file_list = terrainSaveRegionAtlases(region_list[i]);
				terrainLoadRegionAtlases(region_list[i], file_list);
				StructDestroySafe(parse_BinFileList, &file_list);
			}
			eaDestroy(&region_list);
			for (i = 0; i < eaSize(&world_grid.binning_callbacks); i++)
			{
				world_grid.binning_callbacks[i]->callback(world_grid.binning_callbacks[i]->userdata);
				SAFE_FREE(world_grid.binning_callbacks[i]);
			}
			eaClear(&world_grid.binning_callbacks);
		}
	}

	worldCheckForNeedToOpenCells();
	worldCellCollisionEntryCheckCookings();

	worldGridClearTempTrackers();

	if(wlIsServer())
		beaconOncePerFrame();

	PERFINFO_AUTO_STOP_FUNC();
}

int worldGetResetCount(bool include_reloads)
{
	return world_grid.map_reset_count + (include_reloads?world_grid.file_reload_count:0);
}

//////////////////////////////////////////////////////////////////////////

static void validateZoneMap(ZoneMapInfo *zminfo)
{
	int i, j;

	if (!zminfo->genesis_data)
	{
		for (j = 0; j < eaSize(&zminfo->layers); ++j)
		{
			const char *layer_filename = zmapInfoGetLayerPath(zminfo, j);
			if (strstr(layer_filename, "/_"))
				ErrorFilenamef(zmapInfoGetFilename(zminfo), "ZoneMap refers to layer (\"%s\") inside a hidden (underscore) directory, this is not allowed!", layer_filename);
			else if (!fileExists(layer_filename))
				ErrorFilenamef(zmapInfoGetFilename(zminfo), "ZoneMap refers to layer (\"%s\") that does not exist!", layer_filename);
		}
	}

	if (eaSize(&zminfo->regions) > 0 && zminfo->regions[0]->name != NULL && stricmp(zminfo->regions[0]->name, "Default"))
		ErrorFilenamef(zminfo->filename, "Map has no default region or default region is not first in the region list.");

	for (j = 0; j < eaSize(&zminfo->regions); ++j)
	{
		if ((zminfo->regions[j]->name == NULL || stricmp(zminfo->regions[j]->name, "Default") == 0) && j > 0)
			ErrorFilenamef(zminfo->filename, "Default region MUST be first in the region list.");

		for (i = j+1; i < eaSize(&zminfo->regions); ++i)
		{
			if (zminfo->regions[j]->name == zminfo->regions[i]->name)
				ErrorFilenamef(zminfo->filename, "Map has multiple regions with the same name (%s), this will cause geometry to not load!", zminfo->regions[i]->name ? zminfo->regions[i]->name : "<Default>");
		}
	}

	if (!eaSize(&zminfo->private_to) && strstri(zminfo->filename, "/TestMaps/"))
		ErrorFilenamef(zminfo->filename, "Maps in the TestMaps folder must be made private!");
}

void worldValidateZoneMaps(void)
{
	int i;
	ZoneMapInfo *zminfo, **zone_maps = NULL;
	RefDictIterator iter;

	worldGetZoneMapIterator(&iter);
	
	while (zminfo = worldGetNextZoneMap(&iter))
	{
		const char *map_name1;

		if (!zminfo)
			continue;

		validateZoneMap(zminfo);

		map_name1 = zmapInfoGetPublicName(zminfo);
		if (!map_name1)
			continue;

		for (i = 0; i < eaSize(&zone_maps); i++)
		{
			const char *map_name2 = zmapInfoGetPublicName(zone_maps[i]);
			if (map_name2 && stricmp(map_name1, map_name2)==0)
				ErrorFilenameDup(zmapInfoGetFilename(zminfo), zmapInfoGetFilename(zone_maps[i]), map_name1, "ZoneMap Public Name");
		}

		eaPush(&zone_maps, zminfo);
	}
	eaDestroy(&zone_maps);
}

static int cmpZoneMapName(const ZoneMap **pmap1, const ZoneMap **pmap2)
{
	char name1[MAX_PATH], name2[MAX_PATH];
	const char *s;

	s = zmapInfoGetPublicName(zmapGetInfo((ZoneMap*)*pmap1));
	strcpy(name1, s?s:"");

	s = zmapInfoGetPublicName(zmapGetInfo((ZoneMap*)*pmap2));
	strcpy(name2, s?s:"");

	return stricmp(name1, name2);
}

static bool allow_private_maps = false;

void worldLoadZoneMaps(void)
{
	if (!RefSystem_DoesDictionaryExist("SkyInfo"))
		createServerSkyDictionary(); // need a SkyInfo dictionary for the ZoneMaps to parse correctly

#if PLATFORM_CONSOLE
	allow_private_maps = isDevelopmentMode();
#else
	allow_private_maps = isDevelopmentMode() || gimmeDLLQueryExists();
#endif

	ParserLoadFiles(NULL, "environment/WorldRegionRules.txt", "WorldRegionRules.bin", 0, parse_WorldRegionRulesArray, &world_region_rules);

	worldRegionLODSettingsVerify();

	worldResetWorldGrid();

	#ifndef NO_EDITORS
	if (!IsClient() || gbMakeBinsAndExit)
	{
		genesisLoadAllLibraries();
	}
	#endif

	zmapLoadDictionary();

	if (isDevelopmentMode())
		worldValidateZoneMaps();
}

U32 worldGetZoneMapCount()
{
	return RefSystem_GetDictionaryNumberOfReferents(g_ZoneMapDictionary);
}

void worldGetZoneMapIterator(RefDictIterator *iter)
{
	RefSystem_InitRefDictIterator(g_ZoneMapDictionary, iter);

}

ZoneMapInfo *worldGetNextZoneMap(RefDictIterator *iter)
{
	ZoneMapInfo *info;
	PERFINFO_AUTO_START_FUNC();
	do
	{
		info = RefSystem_GetNextReferentFromIterator(iter);
	} while (info && !zmapInfoIsAvailable(info, allow_private_maps));

	PERFINFO_AUTO_STOP();
	return info;
}

// simple version does not support filenames or matching public name within full namespace names
ZoneMapInfo *worldGetZoneMapByPublicNameSimple(const char *public_name)
{
	ZoneMapInfo *pRetVal;

	if (!public_name)
		return NULL;

	PERFINFO_AUTO_START_FUNC();

	// just look for the exact name in the resource dictionary
	pRetVal = RefSystem_ReferentFromString(g_ZoneMapDictionary, public_name);

	PERFINFO_AUTO_STOP();

	return pRetVal;
}

ZoneMapInfo *worldGetZoneMapByPublicName(const char *public_name) // may be filename or public name
{
	bool is_filename;

	if (!public_name)
		return NULL;
		
	PERFINFO_AUTO_START_FUNC();

	is_filename = !!strchr(public_name, '/');

	if (!is_filename)
	{
		//first pass... try to just look the string up in the normal fashion. This should catch the vast majority
		//of "normal" cases, particularly in production mode
		ZoneMapInfo *pRetVal = RefSystem_ReferentFromString(g_ZoneMapDictionary, public_name);

		if (pRetVal)
		{
			PERFINFO_AUTO_STOP();
			return pRetVal;
		}
	}


	FOR_EACH_IN_REFDICT(g_ZoneMapDictionary, ZoneMapInfo, zmap)
	{
		const char *name;
		name = is_filename ? zmap->filename : zmap->map_name;
		if (name)
		{
			if (stricmp(name, public_name)==0)
			{
				PERFINFO_AUTO_STOP();
				return zmap;
			}
			// Try without the namespace
			if (strrchr(name, ':'))
			{
				name = strrchr(name, ':')+1;
				if (stricmp(name, public_name)==0)
				{
					PERFINFO_AUTO_STOP();
					return zmap;
				}
			}
		}
	}
	FOR_EACH_END;
	
	PERFINFO_AUTO_STOP();

	return NULL;
}

const char *worldGetZoneMapFilenameByPublicName(const char *public_name)
{
	ZoneMapInfo *zmap = worldGetZoneMapByPublicName(public_name);
	return zmap ? zmap->filename : NULL;
}

void worldGetZoneMapsThatStartWith(const char *public_name, ZoneMapInfo ***zmaps_out)
{
	if (!public_name)
		return;
	FOR_EACH_IN_REFDICT(g_ZoneMapDictionary, ZoneMapInfo, zmap)
	{
		const char *name;
		name = zmapInfoGetPublicName(zmap);
		if (name && strStartsWith(name, public_name))
			eaPush(zmaps_out, zmap);
	}
	FOR_EACH_END;
}

void worldGetZoneMapsInNamespace(const char *name_space, ZoneMapInfo ***zminfos_out)
{
	FOR_EACH_IN_REFDICT(g_ZoneMapDictionary, ZoneMapInfo, zmap)
	{
		char userNameSpace[RESOURCE_NAME_MAX_SIZE], baseName[RESOURCE_NAME_MAX_SIZE];
		if (resExtractNameSpace(zmap->map_name, userNameSpace, baseName) && stricmp(userNameSpace, name_space)==0)
			eaPush(zminfos_out, zmap);
	}
	FOR_EACH_END;
}

bool worldIsZoneMapInNamespace(ZoneMapInfo *zmap_info)
{
	if(!zmap_info) {
		if(!world_grid.active_map)
			return false;		
		return resHasNamespace(world_grid.active_map->map_info.map_name);
	}
	return resHasNamespace(zmap_info->map_name);
}

bool worldIsSameMap(const char *map_name_1, const char *map_name_2)
{
	ZoneMapInfo *zmap1;
	ZoneMapInfo *zmap2;
	PERFINFO_AUTO_START_FUNC();
	zmap1 = worldGetZoneMapByPublicName(map_name_1);
	zmap2 = worldGetZoneMapByPublicName(map_name_2);
	PERFINFO_AUTO_STOP();
	return zmap1 && zmap2 && zmap1 == zmap2;
}

bool worldLoadZoneMapByName(const char *map_name)
{
	ZoneMapInfo *zminfo = worldGetZoneMapByPublicName(map_name);
	if (!zminfo)
	{
		char ns[MAX_PATH], base[MAX_PATH];
		if (!resExtractNameSpace(map_name, ns, base))
			Errorf("No ZoneMap with the public name \"%s\" exists.", map_name);
		return false;
	}
	return worldLoadZoneMap(zminfo, false, false);
}

AUTO_COMMAND ACMD_NAME("worldLoadWorldGridByPublicName");
void worldLoadWorldGridByPublicNameCommand(const char *public_name)
{
	worldLoadZoneMapByNameSyncWithPatching(public_name);
	ControllerScript_Succeeded();
}

static volatile int zmap_patching;

static void worldZoneMapStartPatchingCallback(const char *filename, void *userData)
{
	assert(zmap_patching);
	InterlockedDecrement(&zmap_patching);
}

static FileScanAction worldZoneMapStartPatchingDir(const char *fullpath, FolderNode *node, void *pUserData_Proc, void *pUserData_Data)
{
	if (node->is_dir)
		return FSA_EXPLORE_DIRECTORY;
	if (node->needs_patching)
	{
		InterlockedIncrement(&zmap_patching);
		fileLoaderRequestAsyncExec(allocAddFilename(fullpath), FILE_MEDIUM_PRIORITY, false, worldZoneMapStartPatchingCallback, NULL);
	}

	return FSA_EXPLORE_DIRECTORY;
}

static void worldZoneMapPatchingCheckDirForFile(const char *filename)
{
	static StashTable stCheckedDirs;
	char dirname[MAX_PATH];
	char binname[MAX_PATH];
	const char *binname2;

	if (!stCheckedDirs)
		stCheckedDirs = stashTableCreateAddress(64);

	strcpy(dirname, filename);
	getDirectoryName(dirname);
	sprintf(binname, "bin/geobin/%s", dirname);
	binname2 = allocAddFilename(binname);
	if (!stashFindInt(stCheckedDirs, binname2, NULL))
	{
		fileScanAllDataDirs2(binname, worldZoneMapStartPatchingDir, NULL);
		verify(stashAddInt(stCheckedDirs, binname2, 1, false));
	}
}

static void worldZoneMapStartPatchingSingle(ZoneMapInfo *zminfo)
{
	// loop through each layer
	worldZoneMapPatchingCheckDirForFile(zminfo->filename);
	FOR_EACH_IN_EARRAY(zminfo->layers, ZoneMapLayerInfo, layer)
	{
		if (!strchr(layer->filename, '/'))
			continue; // Just a filename, no path?  What's going on here?  Some maps have "Default.Layer" in their list
		worldZoneMapPatchingCheckDirForFile(layer->filename);
	}
	FOR_EACH_END;
}

bool worldZoneMapStartPatching(ZoneMapInfo *zminfo)
{
	assert(IsClient());
	if (!zminfo)
		return false;
	// request patches if appropriate
	if (isPatchStreamingOn())
	{
		worldZoneMapStartPatchingSingle(zminfo);
		FOR_EACH_IN_EARRAY(zminfo->secondary_maps, SecondaryZoneMap, secondary_map)
		{
			ZoneMapInfo *secondary_zminfo = worldGetZoneMapByPublicName(secondary_map->map_name); 
			if(!secondary_zminfo) {
				AssertOrAlert("ZONE_MAP_PATCHING_SECONDARY_MAP_FAILED", "Can not find secondary zone map (%s) while patching.", secondary_map->map_name);
				continue;
			}
			worldZoneMapStartPatchingSingle(secondary_zminfo);
		}
		FOR_EACH_END;
	}
	return true;
}

bool worldZoneMapStartPatchingByName(SA_PARAM_NN_STR const char *map_name)
{
	ZoneMapInfo *zminfo = worldGetZoneMapByPublicName(map_name);
	if (!zminfo)
	{
		char ns[MAX_PATH], base[MAX_PATH];
		if (!resExtractNameSpace(map_name, ns, base))
			Errorf("No ZoneMap with the public name \"%s\" exists.", map_name);
		return false;
	}
	return worldZoneMapStartPatching(zminfo);
}


bool worldZoneMapPatching(bool bCheckCallbacks)
{
	return !!zmap_patching || (bCheckCallbacks && zone_loaded_callback);
}

static void worldLoadZoneMapAsyncCheck(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	assert(zone_loaded_callback);
	assert(zone_load_async);
	if (!worldZoneMapPatching(false))
	{
		// Done patching (or nothing needed to be patched)
		verify(worldLoadZoneMap(zone_load_async, false, false));
		zone_loaded_callback(zone_loaded_callback_userdata);
		TimedCallback_Remove(callback);
		zone_loaded_callback = NULL;
		StructDestroySafe(parse_ZoneMapInfo, &zone_load_async);
		zone_loaded_callback_userdata = NULL;
		zone_load_time_callback = NULL;
	}
}

void worldLoadZoneMapAsyncCancel()
{
	if(zone_loaded_callback) {
		world_grid.map_reset_count++;
		zone_loaded_callback(zone_loaded_callback_userdata);
	}
	if(zone_load_time_callback)
		TimedCallback_Remove(zone_load_time_callback);
	zone_loaded_callback = NULL;
	StructDestroySafe(parse_ZoneMapInfo, &zone_load_async);
	zone_loaded_callback_userdata = NULL;
	zone_load_time_callback = NULL;
}

bool worldLoadZoneMapAsync(ZoneMapInfo *zminfo, ZoneMapLoadedCallback callback, void *userData)
{
	if(zone_loaded_callback)
		return true;
	if (isPatchStreamingOn() || isDevelopmentMode())
	{
		if (!worldZoneMapStartPatching(zminfo))
			return false;
		worldSetModTime_AsyncOK(0);
		zone_loaded_callback = callback;
		zone_load_async = StructClone(parse_ZoneMapInfo, zminfo);
		zone_loaded_callback_userdata = userData;
		zone_load_time_callback = TimedCallback_Add(worldLoadZoneMapAsyncCheck, NULL, 0);
		return true;
	} else {
		if (worldLoadZoneMap(zminfo, false, false))
		{
			callback(userData);
			return true;
		} else
			return false;
	}
}

bool worldLoadZoneMapByNameAsync(const char *map_name, ZoneMapLoadedCallback callback, void *userData)
{
	ZoneMapInfo *zminfo = worldGetZoneMapByPublicName(map_name);
	if (!zminfo)
	{
		char ns[MAX_PATH], base[MAX_PATH];
		if (!resExtractNameSpace(map_name, ns, base))
			Errorf("No ZoneMap with the public name \"%s\" exists.", map_name);
		return false;
	}
	worldLoadZoneMapAsyncCancel();
	return worldLoadZoneMapAsync(zminfo, callback, userData);
}

bool worldLoadZoneMapSyncWithPatching(ZoneMapInfo *zminfo, bool force_create_bins, bool load_from_source, bool clear_manifest_cache_after_loading)
{
	// This will do nothing if PatchStreaming is disabled
	// This is only expected to be run with PatchSreamingSimulate, will stall too long with no output with actual patching
	if (isPatchStreamingOn())
	{
		if (!worldZoneMapStartPatching(zminfo))
			return false;
		while (worldZoneMapPatching(false))
			Sleep(1);
	}

	// Done patching (or nothing needed to be patched)
	return worldLoadZoneMapEx(zminfo, force_create_bins, load_from_source, clear_manifest_cache_after_loading);
}

bool worldLoadZoneMapByNameSyncWithPatching(SA_PARAM_NN_STR const char *map_name)
{
	ZoneMapInfo *zminfo = worldGetZoneMapByPublicName(map_name);
	if (!zminfo)
	{
		char ns[MAX_PATH], base[MAX_PATH];
		if (!resExtractNameSpace(map_name, ns, base))
			Errorf("No ZoneMap with the public name \"%s\" exists.", map_name);
		return false;
	}
	return worldLoadZoneMapSyncWithPatching(zminfo, false, false, true);
}

//////////////////////////////////////////////////////////////////////////

GroupTracker *worldAddTempGroup(GroupDef *def, const Mat4 world_mat, TempGroupParams *tgparams, bool in_world)
{
	GroupTracker *tracker;

	if (!def)
		return NULL;

	PERFINFO_AUTO_START_FUNC();
	wl_state.no_sound_for_temp_group = SAFE_MEMBER(tgparams, no_sound);

	if (!def->model && def->model_name)
		groupPostLoad(def);

	// basic culling check
	if (!tgparams || !tgparams->no_culling)
	{
		F32 near_lod_near_dist, far_lod_near_dist, far_lod_far_dist;
		Vec3 world_mid;
		F32 dist_sqrd;

		mulVecMat4(def->bounds.mid, world_mat, world_mid);
		dist_sqrd = distance3Squared(world_mid, wl_state.last_camera_frustum.cammat[3]);
		groupGetDrawVisDistRecursive(def, NULL, 1, &near_lod_near_dist, &far_lod_near_dist, &far_lod_far_dist);

		if (dist_sqrd > SQR(def->bounds.radius + far_lod_far_dist))
		{
			PERFINFO_AUTO_STOP();
			return NULL;
		}

		if (!frustumCheckSphereWorld(&wl_state.last_camera_frustum, world_mid, def->bounds.radius))
		{
			PERFINFO_AUTO_STOP();
			return NULL;
		}
	}

	tracker = trackerAlloc();
	
	// create a temp groupdef
	if (!in_world || (tgparams && tgparams->override_region_name))
	{
		WorldRegion *region = worldGetTempWorldRegionByName(in_world?tgparams->override_region_name:EDITOR_REGION_NAME);

		if (!region->temp_layer)
		{
			region->temp_layer = layerNew(NULL, NULL);
			region->temp_layer->region_name = region->name;
			region->temp_layer->grouptree.def_lib->dummy = true;
		}
		region->bDisableVertexLighting = tgparams->disable_vertex_lighting;

 		tracker->def = groupLibNewGroupDef(region->temp_layer->grouptree.def_lib, NULL, 0, "DUMMY", 0, false, true);
 		tracker->def->is_layer = 1;
	}
	else
	{
		tracker->def = groupLibNewGroupDef(NULL, NULL, 0, "DUMMY", 0, false, true);
	}

	groupLibAddTemporaryDef(tracker->def->def_lib, def, def->name_uid);
	
	// add def to draw as a child
	groupChildInitialize(tracker->def, 0, def, world_mat, 0,
                         (tgparams ? tgparams->seed : 0), 0);

	tracker->def->property_structs.physical_properties.bNoVertexLighting = 1;

	// set properties
	if (tgparams)
	{
		if (tgparams->editor_only)
			tracker->def->property_structs.physical_properties.bVisible = 0;

		if (tgparams->alpha > 0 && tgparams->alpha < 1)
			tracker->def->property_structs.physical_properties.fAlpha = tgparams->alpha;

		if (tgparams->tint_color0)
			groupSetTintColor(tracker->def, tgparams->tint_color0);

		if (tgparams->tint_color1)
			groupSetMaterialPropertyVec3(tracker->def, "Color1", tgparams->tint_color1);

		if (tgparams->dont_cast_shadows)
			tracker->def->property_structs.physical_properties.bDontCastShadows = 1;

		tracker->spline_params = tgparams->spline_params;

		tracker->wireframe = CLAMP(tgparams->wireframe, 0, 3);
		tracker->unlit = tgparams->unlit;

		eaCopyStructs(&tgparams->params, &tracker->def->children[0]->simpleData.params, parse_GroupChildParameter);
	}
	else
	{
		tracker->def->property_structs.physical_properties.bVisible = 0;
	}

	// we don't need collision, volumes, sound spheres, or interaction nodes created for this groupdef
	tracker->def->property_structs.physical_properties.bDummyGroup = 1;

	// update bounds
	groupSetBounds(tracker->def, true);

	trackerOpenEx(tracker, NULL, 1, true, true, tgparams->in_headshot);
	eaPush(&world_grid.temp_trackers, tracker);

	groupLibRemoveTemporaryDef(tracker->def->def_lib, def->name_uid);

	wl_state.no_sound_for_temp_group = false;
	PERFINFO_AUTO_STOP_FUNC();

	return tracker->children[0];
}

//////////////////////////////////////////////////////////////////////////
// region sky overrides

void worldSetRegionSkyGroupOverride(const char *region_name, F32 fade_percent, F32 fade_rate, const char **skies)
{
	WorldRegion *region = zmapGetWorldRegionByNameEx(NULL, region_name, false);
	WorldRegionSkyOverride *sky_override = NULL;
	int i;

	if (!region)
		return;

	for (i = eaSize(&skies) - 1; i >= 0; --i)
	{
		if (!skies[i] || !skies[i][0])
			eaRemove(&skies, i);
	}

	if (fade_percent && eaSize(&skies))
	{
		sky_override = StructAlloc(parse_WorldRegionSkyOverride);
		sky_override->fade_percent = saturate(fade_percent);
		sky_override->fade_rate = fade_rate;

		for (i = 0; i < eaSize(&skies); ++i)
		{
			int idx = eaPush(&sky_override->sky_group.override_list, StructCreate(parse_SkyInfoOverride));
			SET_HANDLE_FROM_STRING("SkyInfo", skies[i], sky_override->sky_group.override_list[idx]->sky);
		}
	}

	if (sky_override && region->sky_override && StructCompare(parse_WorldRegionSkyOverride, sky_override, region->sky_override, 0, 0, 0) == 0)
	{
		StructDestroy(parse_WorldRegionSkyOverride, sky_override);
		return;
	}

	if (region->sky_override)
		StructDestroy(parse_WorldRegionSkyOverride, region->sky_override);
	region->sky_override = sky_override;

	if (wlIsServer())
		region->zmap_parent->sky_override_mod_time++; // send to clients
}

AUTO_EXPR_FUNC(encounter_action) ACMD_NAME("SetRegionSkyOverride1");
void worldSetRegionSkyOverride1(const char *region_name, F32 fade_percent, F32 fade_rate, const char *sky1)
{
	const char **skies = NULL;
	eaStackCreate(&skies, 1);
	eaPush(&skies, sky1);
	worldSetRegionSkyGroupOverride(region_name, fade_percent, fade_rate, skies);
	eaDestroy(&skies);
}

AUTO_EXPR_FUNC(encounter_action) ACMD_NAME("SetRegionSkyOverride2");
void worldSetRegionSkyOverride2(const char *region_name, F32 fade_percent, F32 fade_rate, const char *sky1, const char *sky2)
{
	const char **skies = NULL;
	eaStackCreate(&skies, 2);
	eaPush(&skies, sky1);
	eaPush(&skies, sky2);
	worldSetRegionSkyGroupOverride(region_name, fade_percent, fade_rate, skies);
	eaDestroy(&skies);
}

AUTO_EXPR_FUNC(encounter_action) ACMD_NAME("SetRegionSkyOverride3");
void worldSetRegionSkyOverride3(const char *region_name, F32 fade_percent, F32 fade_rate, const char *sky1, const char *sky2, const char *sky3)
{
	const char **skies = NULL;
	eaStackCreate(&skies, 3);
	eaPush(&skies, sky1);
	eaPush(&skies, sky2);
	eaPush(&skies, sky3);
	worldSetRegionSkyGroupOverride(region_name, fade_percent, fade_rate, skies);
	eaDestroy(&skies);
}

AUTO_COMMAND ACMD_NAME("SetRegionSkyOverride1") ACMD_ACCESSLEVEL(9);
void worldSetRegionSkyOverrideAcmd1(const char *region_name, F32 fade_percent, F32 fade_rate, const char *sky1)
{
	const char **skies = NULL;
	eaStackCreate(&skies, 1);
	eaPush(&skies, sky1);
	worldSetRegionSkyGroupOverride(region_name, fade_percent, fade_rate, skies);
	eaDestroy(&skies);
}

AUTO_COMMAND ACMD_NAME("SetRegionSkyOverride2") ACMD_ACCESSLEVEL(9);
void worldSetRegionSkyOverrideAcmd2(const char *region_name, F32 fade_percent, F32 fade_rate, const char *sky1, const char *sky2)
{
	const char **skies = NULL;
	eaStackCreate(&skies, 2);
	eaPush(&skies, sky1);
	eaPush(&skies, sky2);
	worldSetRegionSkyGroupOverride(region_name, fade_percent, fade_rate, skies);
	eaDestroy(&skies);
}

AUTO_COMMAND ACMD_NAME("SetRegionSkyOverride3") ACMD_ACCESSLEVEL(9);
void worldSetRegionSkyOverrideAcmd3(const char *region_name, F32 fade_percent, F32 fade_rate, const char *sky1, const char *sky2, const char *sky3)
{
	const char **skies = NULL;
	eaStackCreate(&skies, 3);
	eaPush(&skies, sky1);
	eaPush(&skies, sky2);
	eaPush(&skies, sky3);
	worldSetRegionSkyGroupOverride(region_name, fade_percent, fade_rate, skies);
	eaDestroy(&skies);
}

SkyInfoGroup *worldRegionGetSkyOverride(WorldRegion *region, F32 *fade_percent, F32 *fade_rate)
{
	if (!region || !region->sky_override)
	{
		*fade_percent = 0;
		*fade_rate = 1;
		return NULL;
	}

	*fade_percent = region->sky_override->fade_percent;
	*fade_rate = region->sky_override->fade_rate;
	return &region->sky_override->sky_group;
}

//////////////////////////////////////////////////////////////////////////
// Rooms

RoomConnGraph *worldRegionGetRoomConnGraph(WorldRegion *region)
{
	if (!region)
		return NULL;

	return region->room_conn_graph;
}

//////////////////////////////////////////////////////////////////////////
// Civilian generators

WorldCivilianGenerator** worldRegionGetCivilianGenerators(WorldRegion *region)
{
	return SAFE_MEMBER(region, world_civilian_generators);
}

CivilianGenerator* worldGetCivilianGenerator(WorldCivilianGenerator *wlcivgen)
{
	return wlcivgen->civ_gen;
}

void worldSetCivilianGenerator(WorldCivilianGenerator *wlcivgen, CivilianGenerator *civgen)
{
	wlcivgen->civ_gen = civgen;
}

//////////////////////////////////////////////////////////////////////////
// Forbidden positions

WorldForbiddenPosition** worldRegionGetForbiddenPositions(WorldRegion *region)
{
	return SAFE_MEMBER(region, world_forbidden_positions);
}

//////////////////////////////////////////////////////////////////////////
// Path nodes

WorldPathNode** worldRegionGetPathNodes(WorldRegion *region)
{
	return SAFE_MEMBER(region, world_path_nodes);
}

ZoneMapEncounterObjectInfo* zeniObjectFind( const char* mapName, const char* objectName )
{
	ZoneMapEncounterInfo* zeniInfo = RefSystem_ReferentFromString( "ZoneMapEncounterInfo", mapName );
	
	if( zeniInfo ) {
		int it;
		for( it = 0; it != eaSize( &zeniInfo->objects ); ++it ) {
			if( stricmp( objectName, zeniInfo->objects[ it ]->logicalName ) == 0 ) {
				return zeniInfo->objects[ it ];
			}
		}
	}

	return NULL;
}

bool zeniObjIsUGC( ZoneMapEncounterObjectInfo* zeniObj )
{
	if( IS_HANDLE_ACTIVE( zeniObj->displayName ) && IS_HANDLE_ACTIVE( zeniObj->displayDetails )) {
		return true;
	}
	if( zeniObj->ugcComponentID != 0 ) {
		return true;
	}
	return false;
}

bool zeniObjIsUGCData( ZoneMapEncounterObjectInfo* zeniObj )
{
	if( zeniObjIsUGC( zeniObj )) {
		return true;
	}
	if( zeniObj->volume && zeniObj->volume->power_properties ) {
		return true;
	}
	
	return false;
}
