/***************************************************************************



***************************************************************************/

#ifndef _WORLDGRIDPRIVATE_H_
#define _WORLDGRIDPRIVATE_H_
GCC_SYSTEM

#include "file.h"
#include "GlobalEnums.h"
#include "WorldGrid.h"
#include "ZoneMap.h"
#include "ZoneMapLayer.h"
#include "MemoryPool.h"
#include "dynFxManager.h"
#include "../StaticWorld/ZoneMapLayerPrivate.h"
#include "wlTerrain.h"
#include "wlLight.h"
#include "dynWind.h"
#include "dynDraw.h"
#include "MapSnap.h"
#include "../../UtilitiesLib/components/ExpressionPrivate.h"

typedef struct ZoneMapLayer					ZoneMapLayer;
typedef struct ZoneMap						ZoneMap;
typedef struct StashTableImp*				StashTable;
typedef struct WorldColl					WorldColl;
typedef struct WorldCollRoamingCell			WorldCollRoamingCell;
typedef struct WorldCell					WorldCell;
typedef struct WorldCellEntry				WorldCellEntry;
typedef struct WorldCellCullHeader			WorldCellCullHeader;
typedef struct WorldStreamingInfo			WorldStreamingInfo;
typedef struct WorldStreamingPooledInfo		WorldStreamingPooledInfo;
typedef struct WorldClusterState			WorldClusterState;
typedef struct RoomConnGraph				RoomConnGraph;
typedef struct SkyInfoGroup					SkyInfoGroup;
typedef struct WorldTagLocation				WorldTagLocation;
typedef struct WorldZoneMapScope			WorldZoneMapScope;
typedef struct WorldCivilianGenerator		WorldCivilianGenerator;
typedef struct DynFxInfo					DynFxInfo;
typedef struct GenesisZoneMapData			GenesisZoneMapData;
typedef struct AIMastermindDef				AIMastermindDef;
typedef struct CharClassCategorySet			CharClassCategorySet;


//////////////////////////////////////////////////////////////////////////
// Abstract map data

typedef void (*WorldGridBinningStatusFn)(void *userdata, int step, int total_steps);

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(End) AST_IGNORE(LayerType) AST_STRIP_UNDERSCORES;
typedef struct ZoneMapLayerInfo
{
	const char				*filename;			AST( STRUCTPARAM POOL_STRING FILENAME)
	const char				*region_name;		AST( NAME(Region) POOL_STRING )
	bool					genesis;			//	was created using genesis
} ZoneMapLayerInfo;

AUTO_STRUCT AST_STRIP_UNDERSCORES;
typedef struct SecondaryZoneMap
{
	const char				*map_name;				AST( NAME(MapName) STRUCTPARAM )
	Vec3					offset;					AST( NAME(Offset) STRUCTPARAM )
} SecondaryZoneMap;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("\n") AST_STRIP_UNDERSCORES;
typedef struct TerrainMaterialSwap
{
	const char *orig_name;							AST( POOL_STRING STRUCTPARAM )
	const char *replace_name;						AST( POOL_STRING STRUCTPARAM )
} TerrainMaterialSwap;

AUTO_STRUCT;
typedef struct ParentZoneMap
{
	const char *pchMapName;							AST( POOL_STRING )
	char *pchSpawnPoint;						
} ParentZoneMap;

AUTO_STRUCT;
typedef struct WorldRespawnData
{
	U32 min_time; AST( NAME(MinTime) )
	U32 max_time; AST( NAME(MaxTime) )
	U32	increment; AST( NAME(Increment) )
	U32 attrition_time; AST( NAME(AttritionTime) )
} WorldRespawnData;

AUTO_STRUCT AST_IGNORE_STRUCT(UGCProperties);
typedef struct ZoneMapInfo
{
	const char *					map_name;							AST( NAME(MapName) STRUCTPARAM KEY POOL_STRING )
	const char *					filename;							AST( CURRENTFILE )

	// This is only used by	UGC for detecting old maps that are tagged for UGC.
	char *							deprecated_tags;					AST( NAME(Tags) )

	// If this came from a UGCProject, this is the project's container ID 
	ContainerID						ugcProjectID;						AST( NAME(UGCProjectID) )

	// Core Map Definition
	DisplayMessage					display_name;						AST( NAME(DisplayName) STRUCT(parse_DisplayMessage) )

	ZoneMapType						map_type;							AST( NAME(MapType) )
	char *							default_queue;						AST( NAME(DefaultQueue))
	char *							default_gametype;					AST( NAME(DefaultGameType) SUBTABLE(PVPGameTypeEnum))
	char							**private_to;						AST( NAME(PrivateTo) ) // earray of gimme groups or gimme usernames this map is available to.  If the list is not empty this map will not go in the patch.

	SecondaryZoneMap				**secondary_maps;					AST( NAME(SecondaryMap) )
	ZoneMapLayerInfo				**layers;							AST( NAME(Layer) ) // in local map space
	WorldRegion						**regions;							AST( NAME(Region) USERFLAG(TOK_USEROPTIONBIT_1) ) // don't compare this field when deciding whether to reload
	GlobalGAELayerDef				**global_gaelayer_defs;				AST( NAME(GlobalGAELayers) )

	PhotoOptions					*photo_options;						AST( NAME(PhotoOptions) )
	F32								time;								AST( NAME(Time) )
	ZoneMapTimeBlock				**time_blocks;						AST( NAME(TimeBlock) )
	TerrainMaterialSwap				**terrain_material_swaps;			AST( NAME(TerrainMaterialSwap) )
	Vec2							terrain_playable_min;				AST( NAME(TerrainPlayableMin) ) //Only used if there are no playable layers.
	Vec2							terrain_playable_max;				AST( NAME(TerrainPlayableMax) )
	F32								wind_large_object_radius_threshold; AST( NAME(WindLargeObjectRadiusThreshold) DEFAULT(5) ) //controls which objects are deemed to be "large" or "small" by the wind code
	U32								not_player_visited;					AST( NAME(NotPlayerVisited) )
	U32								no_beacons;							AST( NAME(NoBeacons) )
	F32								mapSnapOutdoorRes;					AST( NAME(MapSnapOutdoorRes) )
	F32								mapSnapIndoorRes;					AST( NAME(MapSnapIndoorRes) )
	char							*start_spawn_name;					AST( NAME(StartSpawnName) )
	ZoneRespawnType					eRespawnType;						AST( NAME(RespawnType) DEFAULT(ZoneRespawnType_Default) )
	U32								RespawnWaveTime;					AST( NAME(RespawnWaveTime))	// Only used if you want player to respawn in waves. Useful for PVP game map types.
	REF_TO(RewardTable)				reward_table;						AST( NAME(RewardTable) )
	REF_TO(RewardTable)				player_reward_table;				AST( NAME(PlayerRewardTable) )
	Expression						*requires_expr;						AST( NAME(RequiresExpr) )
	Expression						*permission_expr;					AST( NAME(PermissionExpr) )
	REF_TO(CharClassCategorySet)	required_class_category_set;		AST( NAME(RequiredClassCategorySet) )

	// Genesis Data
	GenesisZoneMapData				*genesis_data;						AST( NAME(GenesisData, ProceduralData) )
	GenesisZoneMapData				*backup_genesis_data;				AST( NAME(BackupGenesisData, BackupProceduralData) )
	GenesisZoneMapInfo				*genesis_info;						AST( NAME(GenesisInfo, GenesisMapData) ) // Created during zonemap fixup
	bool							allow_encounter_hack;				AST( NAME(AllowEncounterHack) )
																		
	// UGC																
	const char						*from_ugc_file;						AST( NAME(FromUGCFile) POOL_STRING FILENAME )
																		
	// Respawn Times													
	WorldRespawnData				*respawn_data;						AST( NAME(RespawnData) )
																		
	// Game Play Data 													
	U32								level;								AST( NAME(Level) )
	U32								force_team_size;					AST( NAME(ForceTeamSize) )
	S32								difficulty;							AST( NAME(Difficulty) )
	WorldVariableDef				**variable_defs;					AST( NAME(Variable) )
	bool							bIgnoreTeamSizeBonusXP;				AST( NAME(IgnoreTeamSizeBonusXP) ) // If this is set the entity kills on this map will not grant bonus XP according to the team size
	bool							confirm_purchases_on_exit;			AST( NAME(ConfirmPurchasesOnExit) ) // If we need to get players to confirm purchases before exiting the map
	bool							collect_door_dest_status;			AST( NAME(CollectDoorDestStatus) ) // Should this map allow door destination status to be collected from the map manager?
	bool							disable_duels;						AST( NAME(DisableDuels) )
	bool							disable_instance_change;			AST( NAME(DisableInstanceChanging) )
	bool							team_not_required;					AST( NAME(TeamNotRequired) ) // For owned maps to allow non-teamed players.  Used for ship interiors.
	bool							terrain_static_lighting;			AST( NAME(TerrainStaticLighting) ) // Calculate static terrain lighting at bin-time.
	bool							enable_shard_variables;				AST( NAME(ShardVariables) )
	bool							disable_visited_tracking;			AST( NAME(DisableVisitedTracking) )
	bool							powers_require_valid_target;		AST( NAME(PowersRequireValidTarget) ) // Override the require valid target setting for powers
	bool							enable_upsell_features;				AST(NAME(EnableUpsellFeatures))
	ParentZoneMap					*pParentMap;						AST( NAME(ParentMap) )	// To allow ripcords from static maps
	const char*						mastermind_def;						AST( NAME(MastermindDef) RESOURCEDICT(AIMastermindDef) POOL_STRING)
	const char*						civilian_def;						AST( NAME(CivilianMapDef) RESOURCEDICT(AICivilianMapDef) POOL_STRING)
	const char						**playerFSMs;						AST( NAME(PlayerFSM) RESOURCEDICT(FSM) POOL_STRING)
	ZoneMapLightOverrideType		light_override;						AST( NAME(LightOverride) )
	bool							record_player_match_stats;			AST( NAME(RecordPlayerMatchStats))
	bool							guild_owned;						AST( NAME(GuildOwned) )
	bool							guild_not_required;					AST( NAME(GuildNotRequired) )
	
	// If set, this map will be treated as in UGC during
	// binning. (Generate a map_snap_mini.hogg, be in the
	// ZoneMapClient.bin)
	//
	// Since WorldCellBinning is done *before* UGCResourceInfos are
	// loaded, the ZoneMap needs to carry around info stating if it is
	// in UGC or not.
	//
	// Also, used by validation.
	ZoneMapUGCUsage					eUsedInUGC;							AST( NAME(UsedInUGC) NAME(GenerateMapSnapMini))

	// Editor Tracking Data
	U32								bfParamsSpecified[3];				AST( USEDFIELD NO_TEXT_SAVE)
	U32								bfParamsSize;						NO_AST

	bool							saving;								AST( NO_WRITE )

	int								mod_time;							NO_AST
	const char *					new_map_name;						NO_AST
	bool							is_new;								NO_AST
} ZoneMapInfo;
extern ParseTable parse_ZoneMapInfo[];

extern DictionaryHandle g_ZoneMapDictionary;
extern DictionaryHandle g_ZoneMapEncounterInfoDictionary;
extern DictionaryHandle g_ZoneMapExternalMapSnapDictionary;

//////////////////////////////////////////////////////////////////////////
// Concrete (loaded) map data

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(End) AST_IGNORE(LayerType) AST_STRIP_UNDERSCORES;
typedef struct ZoneMapLayer
{
	// parsed from zone map file:
	const char				*filename;			AST( STRUCTPARAM POOL_STRING FILENAME)
	const char				*region_name;		AST( NAME(Region) POOL_STRING )
	bool					genesis;			//	was created using genesis
	bool					scratch;			//	is a scratch layer for the world editor

	AST_STOP

	char					*name;

	ZoneMapLayerMode		layer_mode;
	ZoneMapLayerMode		target_mode;

	union
	{
		U32					lock_owner;			// 0 = Not locked.
		U32					locked;				// 0: not locked, 1: locked, 3: locked locally
	};

	bool					saving;				// We're in the middle of an asynchronous save
    bool                    waiting_to_edit;
    bool                    checkout_failed;
	bool					unlock_on_save;		// Flag for asynchronous saves, to unlock when we're done
	bool					reload_pending;

	bool					controller_script_wait_for_edit;

	U32						progress_id;

	__time32_t				last_change_time;

	WorldGridBinningStatusFn bin_status_callback;
	void					*bin_status_userdata;
    
	WorldCellEntry			**cell_entries;
	WorldTagLocation		**tag_locations;

	StashTable				reserved_unique_names;	// holds all unique names in this layer when it is non-editable

	ZoneMapLayer *			dummy_layer;
	ZoneMap	*				zmap_parent;

	ZoneMapGroupTreeLayer grouptree;
	ZoneMapTerrainLayer terrain;

	LayerBounds				bounds;
} ZoneMapLayer;

// The fields with TOK_USEROPTIONBIT_1 require manual copying in the receiveZoneMap function
//AUTO_STRUCT;
typedef struct ZoneMap
{
	ZoneMapInfo				map_info;
	ZoneMapInfo				*last_saved_info;				// since zonemap changes aren't actually applied to the server during
															// editing (or after saving), we store the supposed-to-be-applied zonemap data here
															// for diff'ing purposes

	ZoneMapLayer			**layers;

	struct 
	{
		WorldStreamingInfo			*streaming_info;
		WorldStreamingPooledInfo	*streaming_pooled_info;

		// WorldCellEntry.c
		StashTable				shared_bounds_hash_table;
		int						total_shared_bounds_count;
		U32						shared_bounds_uid_counter;

		// WorldCellFXEntry.c
		StashTable				fx_condition_hash;
		U32						fx_id_counter;
		DictionaryHandle		fx_entry_dict;
		char					fx_entry_dict_name[64];

		// WorldCellMiscEntry.c
		U32						animation_id_counter;
		DictionaryHandle		animation_entry_dict;
		char					animation_entry_dict_name[64];

		// WorldCellDrawableEntry.c
		WorldDrawableListPool	drawable_pool;

		// WorldCellInteractionEntry.c
		StashTable 				interaction_node_hash;
		StashTable 				interaction_costume_hash_table;
		StashTable 				interaction_to_costume_hash;
		int						total_costume_count, interaction_costume_idx;

	} world_cell_data;

	GenesisEditType			genesis_edit_type;
	GenesisViewType			genesis_view_type;
	bool					genesis_data_preview;
	bool					failed_validation;
	bool					deleted_layer; // client-side, must message the server
	U32						tracker_update_time;
	U32						sky_override_mod_time;

	WorldZoneMapScope		*zmap_scope;

	BinFileListWithCRCs		*external_dependencies;

	U32						isUGCGeneratedMap : 1;
} ZoneMap;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(End) AST_STRIP_UNDERSCORES;
typedef struct DependentWorldRegion
{
	const char				*name;					AST( STRUCTPARAM POOL_STRING )
	int						hidden_object_id;		AST( NAME(HiddenObjectID) )
	Vec3					camera_offset;			AST( NAME(CameraOffset) )
} DependentWorldRegion;

AUTO_STRUCT;
typedef struct WorldTagLocation
{
	int tag_id;
	Vec3 position;
} WorldTagLocation;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(End) AST_STRIP_UNDERSCORES;
typedef struct WorldRegionFXSwap
{
	REF_TO(DynFxInfo) hOldFx; 							AST(STRUCTPARAM REQUIRED NAME(OldFx))
	REF_TO(DynFxInfo) hNewFx; 							AST(STRUCTPARAM REQUIRED NAME(NewFx))
} WorldRegionFXSwap;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(End) AST_STRIP_UNDERSCORES;
typedef struct WorldRegionSkyOverride
{
	SkyInfoGroup sky_group;
	F32 fade_percent;
	F32 fade_rate;
} WorldRegionSkyOverride;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(End) AST_STRIP_UNDERSCORES;
typedef struct WorldRegion
{
	const char				*name;					AST( STRUCTPARAM POOL_STRING )
	WorldRegionType			type;					AST( NAME(Type) DEF(WRT_Ground) )
	SkyInfoGroup			*sky_group;				AST( NAME(SkyGroup) )
	DependentWorldRegion	**dependents;			AST( NAME(DependentRegion) )
	const char				*override_cubemap;		AST( NAME(OverrideCubeMap) POOL_STRING )
	WorldRegionFXSwap		**fx_swaps;				AST( NAME(FXSwap) )

	RegionRulesOverride		pRegionRulesOverride;	AST( EMBEDDED_FLAT)
	bool					bWorldGeoClustering;	AST( NAME(WorldGeometryClustering) )
	bool					bUseIndoorLighting;		AST( NAME(UseIndoorLighting) )

	AST_STOP

	bool					bDisableVertexLighting;

	StashTable				fx_swap_table;			// used to look up fx_swaps quickly if there are a lot of them

	ZoneMap					*zmap_parent;

	struct
	{
		BlockRange			cell_extents;			// in primary map cell grid space
		Vec3				world_min, world_max;	// in primary map space
		Vec3				world_visible_geo_min,world_visible_geo_max;
		bool				needs_update;
	} world_bounds;

	struct
	{
		BlockRange			cell_extents;			// in primary map cell grid space
	} binned_bounds;

	struct
	{
		BlockRange			cell_extents;			// in primary map grid space
		Vec3				world_min, world_max;	// in primary map space
		bool				needs_update;
	} terrain_bounds;

	WorldCell				*root_world_cell;
	WorldCellCullHeader		*world_cell_headers;
	bool					preloaded_cell_data;

	WorldCell				*temp_world_cell;
	WorldCellCullHeader		*temp_world_cell_headers;

	HeightMapAtlasRegionData *atlases;				// Atlases used for rendering

	HogFile					*terrain_atlas_hoggs[ATLAS_MAX_LOD+1];
	HogFile					*terrain_model_hoggs[ATLAS_MAX_LOD+1];
	HogFile					*terrain_light_model_hoggs[ATLAS_MAX_LOD+1];
	HogFile					*terrain_coll_hogg;
	HogFile					*world_cell_hogg;

	bool					is_editor_region;
	ZoneMapLayer			*temp_layer;			// dummy layer to use as a parent for temp trackers

	DynFxRegion				fx_region;

	RoomConnGraph			*room_conn_graph;		// room connectivity graph

	WorldCivilianGenerator	**world_civilian_generators;	// civilian gen list
	WorldForbiddenPosition  **world_forbidden_positions;	// forbidden pos list
	WorldPathNode			**world_path_nodes;
	WorldPathNode			**world_path_nodes_editor_only; // nodes that are only visible in the editor (currently just UGC nodes)

	WorldTagLocation		**tag_locations;

	WorldRegionSkyOverride	*sky_override;

	U32						last_used_timestamp;

	// for graphics lib:
	WorldRegionGraphicsData	*graphics_data;

	// for map snaps
	MapSnapRegionData		mapsnap_data;

	// for graphics lib:
	WorldClusterState		*cluster_options;

} WorldRegion;

AUTO_STRUCT AST_STRIP_UNDERSCORES;
typedef struct WorldRegionRules
{
	const char *filename;				AST( CURRENTFILE )
	int region_type;					AST( KEY SUBTABLE(WorldRegionTypeEnum) STRUCTPARAM )

	F32 effective_scale;				AST( NAME(EffectiveScale) DEFAULT(1) )
	F32 lod_scale;						AST( NAME(LODScale) DEFAULT(1) )

	bool no_sky_sun;					AST( NAME(NoSkyFileSun) )

	CamLightRules camLight;
	ShadowRules shadows;
	WorldRegionWindRules wind;			AST( NAME(Wind) )
	WorldRegionLODSettings lod_settings;	AST( NAME(LodSettings))
} WorldRegionRules;

//////////////////////////////////////////////////////////////////////////
// World Grid

typedef void (*WorldGridBinningCallbackFn)(void *userdata);

typedef struct WorldGridBinningCallback
{
	WorldGridBinningCallbackFn	callback;
	void *						userdata;
} WorldGridBinningCallback;


typedef struct WorldGrid
{
	ZoneMap					**maps;
	F32						*map_offsets;

	ZoneMap 				*active_map;

	WorldColl**				eaWorldColls;

	GroupDefLib				*dummy_lib;

	int						object_library_refs;

	int						def_access_time;

	union
	{
		const U32 mod_time;
		U32 mod_time_MUTABLE;
	};

	U32						server_mod_time;
	U32						map_reset_count;
	U32						file_reload_count;

	U32						unsaved : 1;			// used on client-side only; set by worldReceiveUpdate
	U32						needs_reload : 1;		// communicates to the client when an unreloaded change on disk occurs on the server

	char *					deferred_load_map;		// Attempt to load map each frame until we succeed

	int						loading;				// currently loading a group file from disk (disables some defMod/gimme stuff), uses a counter

	WorldRegion				**all_regions;
	WorldRegion				**temp_regions;
	GroupTracker			**temp_trackers;		// trackers that are around for only one frame, used for drawing editor objects

	WorldGridBinningCallback **binning_callbacks;

	// for graphics lib:
	WorldGraphicsData		*graphics_data;

} WorldGrid;

extern WorldGrid world_grid;

#include "../AutoGen/WorldGrid_h_ast.h"
#include "../AutoGen/ZoneMapLayerPrivate_h_ast.h"
#include "../AutoGen/WorldGridPrivate_h_ast.h"

int zmapLoadDictionary(void);
void zmapLoadClientDictionary(void);

void worldBuildCellTree(ZoneMap *zmap, bool open_trackers);

void zmapUpdateBounds(SA_PARAM_OP_VALID ZoneMap *zmap);
void zmapRecalcBounds(SA_PARAM_OP_VALID ZoneMap *zmap, int close_trackers);

SA_ORET_NN_VALID WorldRegion *createWorldRegion(SA_PARAM_OP_VALID ZoneMap *zmap, SA_PARAM_OP_STR const char *name);
void initWorldRegion(SA_PARAM_NN_VALID WorldRegion *region);
void uninitWorldRegion(SA_PARAM_OP_VALID WorldRegion *region);

// returns true if it called the map edit callback
bool worldCheckForNeedToOpenCells(void);
void worldCheckForNeedToOpenCellsOnPartition(int iPartitionIdx);
void worldCloseCellsOnPartition(int iPartitionIdx);

void createLayerData(ZoneMapLayer *layer, bool create_def_lib);

typedef enum TerrainHogFileType
{
	THOG_ATLAS,
	THOG_MODEL,
	THOG_COLL,
	THOG_LIGHTMODEL,
} TerrainHogFileType;

HogFile *worldRegionGetTerrainHogFile(SA_PARAM_NN_VALID WorldRegion *region, TerrainHogFileType type, int lod);

void groupDefRefresh(GroupDef *def);
// child_idx >= 0  --> update child_mod_time for specified child and all_mod_time, causing trackers to get closed and reopened for only the specified child
// child_idx == UPDATE_GROUP_PROPERTIES --> update group_mod_time and all_mod_time, causing trackers to get closed and reopened for this def
// child_idx == UPDATE_REMOVED_CHILD --> update all_mod_time, causing def to get sent (use when removing child defs)
void groupDefModify(SA_PARAM_OP_VALID GroupDef *def, int child_idx, bool user_change);

__forceinline static int getTerrainGridPosKey(SA_PRE_NN_RELEMS(2) const IVec2 pos)
{
	int x = (pos[0] + 0x7fff) & 0xffff;
	int y = (pos[1] + 0x7fff) & 0xffff;
	assert(x || y);
	return (x | (y << 16));
}


__forceinline static bool gridPosInRange(SA_PRE_NN_RELEMS(3) const IVec3 grid_pos, SA_PARAM_NN_VALID const BlockRange *range)
{
	return	grid_pos[0] >= range->min_block[0] && grid_pos[0] <= range->max_block[0] &&
			grid_pos[1] >= range->min_block[1] && grid_pos[1] <= range->max_block[1] &&
			grid_pos[2] >= range->min_block[2] && grid_pos[2] <= range->max_block[2];
}

__forceinline static bool rangesOverlap(SA_PARAM_NN_VALID const BlockRange *range1, SA_PARAM_NN_VALID const BlockRange *range2)
{
	if (range1->min_block[0] > range2->max_block[0])
		return false;
	if (range1->min_block[1] > range2->max_block[1])
		return false;
	if (range2->min_block[0] > range1->max_block[0])
		return false;
	if (range2->min_block[1] > range1->max_block[1])
		return false;
	return true;
}

__forceinline static void rangeSize(SA_PARAM_NN_VALID const BlockRange *range, SA_PRE_NN_ELEMS(3) SA_POST_OP_VALID IVec3 size)
{
	size[0] = range->max_block[0] - range->min_block[0] + 1;
	size[1] = range->max_block[1] - range->min_block[1] + 1;
	size[2] = range->max_block[2] - range->min_block[2] + 1;
}

__forceinline static void worldPosToGridPos(SA_PRE_NN_RELEMS(3) const Vec3 world_pos, SA_PRE_NN_ELEMS(3) SA_POST_OP_VALID IVec3 grid_pos, F32 block_size)
{
	grid_pos[0] = floor(world_pos[0] / block_size);
	grid_pos[1] = floor(world_pos[1] / block_size);
	grid_pos[2] = floor(world_pos[2] / block_size);
}

__forceinline static void convertBlockRange(SA_PARAM_NN_VALID const BlockRange *src_range, F32 src_block_size, SA_PARAM_NN_VALID BlockRange *dst_range, F32 dst_block_size)
{
	Vec3 world_pos;

	scaleVec3(src_range->min_block, src_block_size, world_pos);
	worldPosToGridPos(world_pos, dst_range->min_block, dst_block_size);

	scaleVec3(src_range->max_block, src_block_size, world_pos);
	worldPosToGridPos(world_pos, dst_range->max_block, dst_block_size);
}

//extern ParseTable parse_ZoneMap[];
extern ParseTable parse_WorldTagLocation[];
//extern ParseTable zone_map_array_parseinfo[];

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

#endif //_WORLDGRIDPRIVATE_H_

