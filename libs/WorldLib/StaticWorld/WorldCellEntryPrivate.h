/***************************************************************************



***************************************************************************/

#ifndef __WORLDCELLENTRYPRIVATE_H__
#define __WORLDCELLENTRYPRIVATE_H__
GCC_SYSTEM

#include "WorldCellEntry.h"

typedef struct WorldRegion WorldRegion;
typedef struct WorldCell WorldCell;
typedef struct WorldCellEntry WorldCellEntry;
typedef struct WorldCollisionEntry WorldCollisionEntry;
typedef struct WorldCollisionEntryParsed WorldCollisionEntryParsed;
typedef struct WorldAltPivotEntry WorldAltPivotEntry;
typedef struct WorldAltPivotEntryParsed WorldAltPivotEntryParsed;
typedef struct WorldFXEntry WorldFXEntry;
typedef struct WorldFXEntryParsed WorldFXEntryParsed;
typedef struct WorldAnimationEntry WorldAnimationEntry;
typedef struct WorldAnimationEntryParsed WorldAnimationEntryParsed;
typedef struct WorldInteractionEntry WorldInteractionEntry;
typedef struct WorldInteractionEntryServerParsed WorldInteractionEntryServerParsed;
typedef struct WorldInteractionEntryClientParsed WorldInteractionEntryClientParsed;
typedef struct WorldVolumeEntry WorldVolumeEntry;
typedef struct WorldVolumeEntryServerParsed WorldVolumeEntryServerParsed;
typedef struct WorldVolumeEntryClientParsed WorldVolumeEntryClientParsed;
typedef struct WorldSoundEntry WorldSoundEntry;
typedef struct WorldSoundEntryParsed WorldSoundEntryParsed;
typedef struct WorldLightEntry WorldLightEntry;
typedef struct WorldLightEntryParsed WorldLightEntryParsed;
typedef struct WorldDrawableEntry WorldDrawableEntry;
typedef struct WorldModelInstanceEntry WorldModelInstanceEntry;
typedef struct WorldDrawableEntryParsed WorldDrawableEntryParsed;
typedef struct WorldOcclusionEntryParsed WorldOcclusionEntryParsed;
typedef struct WorldModelEntryParsed WorldModelEntryParsed;
typedef struct WorldModelInstanceEntryParsed WorldModelInstanceEntryParsed;
typedef struct WorldSplinedModelEntryParsed WorldSplinedModelEntryParsed;
typedef struct WorldInstanceParamList WorldInstanceParamList;
typedef struct WorldInstanceParamListParsed WorldInstanceParamListParsed;
typedef struct Model Model;
typedef struct GroupDef GroupDef;
typedef struct GroupTracker GroupTracker;
typedef struct GroupSplineParams GroupSplineParams;
typedef struct MaterialNamedConstant MaterialNamedConstant;
typedef struct WorldCellWeldedBin WorldCellWeldedBin;
typedef struct WorldCellDrawableDataParsed WorldCellDrawableDataParsed;
typedef struct MaterialDraw MaterialDraw;
typedef struct NOCONST(MaterialDraw) NOCONST(MaterialDraw);
typedef struct MaterialDrawParsed MaterialDrawParsed;
typedef struct ModelDraw ModelDraw;
typedef struct NOCONST(ModelDraw) NOCONST(ModelDraw);
typedef struct ModelDrawParsed ModelDrawParsed;
typedef struct WorldDrawableSubobject WorldDrawableSubobject;
typedef struct NOCONST(WorldDrawableSubobject) NOCONST(WorldDrawableSubobject);
typedef struct WorldDrawableSubobjectParsed WorldDrawableSubobjectParsed;
typedef struct WorldDrawableList WorldDrawableList;
typedef struct NOCONST(WorldDrawableList) NOCONST(WorldDrawableList);
typedef struct NOCONST(WorldDrawableListPool) NOCONST(WorldDrawableListPool);
typedef struct WorldDrawableListParsed WorldDrawableListParsed;
typedef struct WorldCellEntryParsed WorldCellEntryParsed;
typedef struct WorldInteractionCostumeParsed WorldInteractionCostumeParsed;
typedef struct WLCostume WLCostume;
typedef struct WorldPlanetProperties WorldPlanetProperties;
typedef struct WorldAtmosphereProperties WorldAtmosphereProperties;
typedef struct WorldScope WorldScope;
typedef struct Room Room;
typedef struct RoomPartition RoomPartition;
typedef struct RoomPortal RoomPortal;
typedef struct WorldLODOverride WorldLODOverride;
typedef struct WorldVolumeElement WorldVolumeElement;
typedef struct WorldStreamingInfo WorldStreamingInfo;
typedef struct WorldStreamingPooledInfo WorldStreamingPooledInfo;
typedef struct WorldWindSourceEntryParsed WorldWindSourceEntryParsed;

#define WORLD_VOLUME_VISDIST_MULTIPLIER 2
#define WORLD_VOLUME_VISDIST_EDITOR_MULTIPLIER 40

#define WORLD_OCCLUSION_VISDIST_MULTIPLIER 5
#define WORLD_OCCLUSION_VISDIST_ADDER 10
#define WORLD_OCCLUSION_VISDIST_EDITOR_MULTIPLIER 40

typedef struct WorldFXCondition
{
	bool state;
	WorldFXEntry **fx_entries;
	WorldVolumeEntry **water_entries;
	char *name;
} WorldFXCondition;

typedef struct WorldInteractionCostumePart
{
	Mat4					matrix;
	const Model				*model;
	Vec4					tint_color;
	WorldDrawableList		*draw_list;
	WorldInstanceParamList	*instance_param_list;
	int						collision; // leave as an int!
} WorldInteractionCostumePart;

typedef struct WorldInteractionCostume
{
	int						name_id;
	Mat4					hand_pivot;
	Mat4					mass_pivot;
	const char				*carry_anim_bit;
	WorldInteractionCostumePart **costume_parts;
	int						*interaction_uids; // only filled in during binning
	int						ref_count;
	ZoneMap					*zmap;
} WorldInteractionCostume;


// all
void worldEntryInit(WorldCellEntry *entry, SA_PARAM_NN_VALID GroupInfo *info, WorldCellEntry ***cell_entry_list, SA_PARAM_OP_VALID GroupDef *def, Room *room, bool making_bins);
void worldEntryInitBoundsFromParsed(WorldStreamingPooledInfo *streaming_pooled_info, WorldCellEntry *entry, WorldCellEntryParsed *entry_parsed);
void worldEntryInitFromParsed(SA_PARAM_NN_VALID ZoneMap *zmap, WorldRegion *region, WorldCell *cell, WorldCellEntry *entry, WorldCellEntryParsed *entry_parsed, bool is_near_fade);
WorldCellWeldedBin *worldWeldedBinInitFromParsed(WorldCell *cell, WorldCellDrawableDataParsed *bin_parsed);
void worldWeldedBinAddEntryFromParsed(WorldCellWeldedBin *bin, WorldCellEntry *entry);
void initGroupInfo(SA_PRE_NN_FREE SA_POST_NN_VALID GroupInfo *info, SA_PARAM_OP_VALID ZoneMapLayer *layer);
U32 getLayerIDBits(SA_PARAM_OP_VALID ZoneMapLayer *layer);
void worldCellEntryApplyTintColor(SA_PARAM_NN_VALID const GroupInfo *info, Vec4 out_color);

// collision
WorldCollisionEntry *createWorldCollisionEntry(SA_PARAM_OP_VALID GroupTracker *tracker, GroupDef *def, Model *model, const char ***texture_swaps, const char ***material_swaps, WorldPlanetProperties *planet, const GroupInfo *info, bool is_terrain);
WorldCellEntry *createWorldCollisionEntryFromParsed(SA_PARAM_NN_VALID ZoneMap *zmap, WorldStreamingPooledInfo *streaming_pooled_info, WorldCollisionEntryParsed *entry_parsed);
bool worldCollisionEntryIsTraversable(WorldCollisionEntry *entry);
void openWorldCollisionEntry(int iPartitionIdx, WorldCollisionEntry *entry);
void openWorldCollisionEntryAll(WorldCollisionEntry *entry);
void closeWorldCollisionEntry(int iPartitionIdx, WorldCollisionEntry *entry);
void worldCellCollisionEntryBeingDestroyed(WorldCollisionEntry *entry);
void worldCellCollisionEntryCheckCookings(void);

// altpivot
WorldAltPivotEntry *createWorldAltPivotEntry(GroupDef *def, GroupInfo *info);
WorldCellEntry *createWorldAltPivotEntryFromParsed(WorldStreamingPooledInfo *streaming_pooled_info, WorldAltPivotEntryParsed *entry_parsed);


// fx
WorldFXEntry *createWorldFXEntry(const Mat4 world_matrix, const Vec3 world_mid, F32 radius, F32 far_lod_far_dist, const GroupInfo *info);
WorldCellEntry *createWorldFXEntryFromParsed(SA_PARAM_NN_VALID ZoneMap *zmap, WorldFXEntryParsed *entry_parsed);
void initWorldFXEntry(SA_PARAM_NN_VALID ZoneMap *zmap, WorldFXEntry *fx_entry, F32 hue, const char *fx_condition, const char *fx_name, const char *fx_params, bool has_target_node, bool target_no_anim, Mat4 target_node_mat, const char *faction_name, const char *filename);
void initWorldFXEntryDebris(WorldFXEntry *fx_entry, Model *model, const Vec3 model_scale, const GroupInfo *info, GroupDef *def, 
							const char ***texture_swaps, const char ***material_swaps, 
							MaterialNamedConstant **material_constants);
void uninitWorldFXEntry(WorldFXEntry *fx_entry);
void startWorldFX(WorldFXEntry *fx_entry);
void stopWorldFX(WorldFXEntry *fx_entry);
void worldFXSetIDCounter(SA_PARAM_NN_VALID ZoneMap *zmap, U32 id_counter);
void worldCellFXReset(SA_PARAM_NN_VALID ZoneMap *zmap);
void setupFXEntryDictionary(SA_PARAM_NN_VALID ZoneMap *zmap);
WorldFXCondition *getWorldFXCondition(ZoneMap *zmap, const char *fx_condition, WorldFXEntry *fx_entry, WorldVolumeEntry *water_entry);


// animation
WorldAnimationEntry *createWorldAnimationEntry(const GroupInfo *info, F32 far_lod_near_dist, F32 far_lod_far_dist, const WorldAnimationProperties *animation_properties);
WorldCellEntry *createWorldAnimationEntryFromParsed(SA_PARAM_NN_VALID ZoneMap *zmap, WorldAnimationEntryParsed *entry_parsed);
bool uninitWorldAnimationEntry(WorldAnimationEntry *animation_entry);
void worldAnimationEntrySetIDCounter(SA_PARAM_NN_VALID ZoneMap *zmap, U32 id_counter);
void worldAnimationEntryResetIDCounter(SA_PARAM_NN_VALID ZoneMap *zmap);
void setupWorldAnimationEntryDictionary(SA_PARAM_NN_VALID ZoneMap *zmap);


// interaction
WorldInteractionEntry *createWorldInteractionEntry(GroupDef **def_chain, int *idxs_in_parent, GroupDef *def, const GroupInfo *info);
WorldCellEntry *createWorldInteractionEntryFromServerParsed(WorldStreamingPooledInfo *streaming_pooled_info, WorldInteractionEntryServerParsed *entry_parsed, bool streaming_mode, bool parsed_will_be_freed);
WorldCellEntry *createWorldInteractionEntryFromClientParsed(WorldStreamingPooledInfo *streaming_pooled_info, WorldInteractionEntryClientParsed *entry_parsed, bool streaming_mode, bool parsed_will_be_freed);
void addWorldInteractionEntryPostParse(SA_PARAM_NN_VALID ZoneMap *zmap, SA_PARAM_NN_VALID WorldInteractionEntry *entry, int interactable_id);
void addChildToInteractionEntry(WorldCellEntry *child, WorldInteractionEntry *parent, int interaction_child_idx);
void freeWorldInteractionEntryData(SA_PARAM_NN_VALID ZoneMap *zmap, WorldInteractionEntry *ent);
bool validateInteractionChildren(SA_PARAM_NN_VALID WorldInteractionEntry *parent, int *fail_index);
bool worldInteractionEntryIsTraversable(WorldInteractionEntry *entry);


// interaction costume
void removeInteractionCostumeRef(SA_PRE_OP_VALID SA_POST_P_FREE WorldInteractionCostume *costume, int interaction_uid);
WorldInteractionCostume *createInteractionCostumeFromParsed(SA_PARAM_NN_VALID ZoneMap *zmap, WorldInteractionCostumeParsed *costume_parsed, int idx);
void getAllInteractionCostumes(SA_PARAM_NN_VALID ZoneMap *zmap, WorldInteractionCostume ***costume_array);
void interactionCostumeLeaveStreamingMode(SA_PARAM_NN_VALID ZoneMap *zmap);
void worldCellInteractionReset(SA_PARAM_NN_VALID ZoneMap *zmap);


// volume
WorldVolumeEntry *createWorldVolumeEntry(GroupDef *def, const GroupInfo *info, U32 volume_type_bits);
WorldVolumeEntry *createWorldVolumeEntryForRoom(Room *room, WorldVolumeElement **volume_elements);
WorldCellEntry *createWorldVolumeEntryFromServerParsed(ZoneMap *zmap, WorldStreamingInfo *streaming_info, WorldStreamingPooledInfo *streaming_pooled_info, WorldVolumeEntryServerParsed *entry_parsed, bool create_all, bool parsed_will_be_freed);
WorldCellEntry *createWorldVolumeEntryFromClientParsed(ZoneMap *zmap, WorldStreamingInfo *streaming_info, WorldStreamingPooledInfo *streaming_pooled_info, WorldVolumeEntryClientParsed *entry_parsed, bool create_all, bool parsed_will_be_freed);
void openWorldVolumeEntry(int iPartitionIdx, WorldVolumeEntry *ent);
void closeWorldVolumeEntry(int iPartitionIdx, WorldVolumeEntry *ent);
WorldVolumeElement *worldVolumeEntryAddSubVolume(WorldVolumeEntry *entry, GroupDef *def, const GroupInfo *info);
void updateWorldIndoorVolumeEntry(WorldVolumeEntry* entry);


// sound
WorldSoundEntry *createWorldSoundEntry(const char *event_name, const char *excluder_str, const char *dsp_str, 
									   const char *group_str, int group_ord, 
									   const GroupInfo *info, const GroupDef *def);
WorldCellEntry *createWorldSoundEntryFromParsed(WorldStreamingPooledInfo *streaming_pooled_info, WorldSoundEntryParsed *entry_parsed);
void destroyWorldSoundEntry(WorldSoundEntry *ent);

// light
bool validateWorldLightEntry(GroupDef **def_chain, const GroupInfo *info, char** error_message);
WorldLightEntry *createWorldLightEntry(GroupDef **def_chain, const GroupInfo *info);
WorldCellEntry *createWorldLightEntryFromParsed(SA_PARAM_NN_VALID ZoneMap *zmap, WorldRegion *region, WorldLightEntryParsed *entry_parsed, bool parsed_will_be_freed);


// wind
WorldWindSourceEntry *createWorldWindSourceEntry(GroupDef *def, const GroupInfo *info);
WorldCellEntry *createWorldWindSourceEntryFromParsed(SA_PARAM_NN_VALID ZoneMap *zmap, WorldRegion *region, WorldWindSourceEntryParsed *entry_parsed);


// occlusion/volume drawable
WorldDrawableEntry *createWorldOcclusionEntry(GroupDef *def, GroupDef *parent_volume_def, Model *customOccluderModel, GroupInfo *info, Vec4 tint_color, bool no_occlusion, bool in_editor, bool doubleSideOccluder);
WorldOcclusionEntry *createWorldOcclusionEntryFromMesh(GMesh *mesh, const Mat4 world_matrix, bool double_sided);
WorldCellEntry *createWorldOcclusionEntryFromParsed(SA_PARAM_NN_VALID ZoneMap *zmap, WorldOcclusionEntryParsed *entry_parsed);


// model drawable
WorldDrawableEntry *createWorldModelEntry(GroupDef *def, Model *model, const GroupInfo *info, 
										  const char ***texture_swaps, const char ***material_swaps, 
										  MaterialNamedConstant **material_constants, 
										  bool is_occluder, const Vec3 model_scale, 
										  const WorldAtmosphereProperties *atmosphere);
WorldCellEntry *createWorldModelEntryFromParsed(SA_PARAM_NN_VALID ZoneMap *zmap, WorldModelEntryParsed *entry_parsed, WorldCell *cell_debug, bool parsed_will_be_freed);

// instanced model drawable
void initWorldModelInstanceEntry(WorldModelInstanceEntry *entry, GroupDef *def, const GroupInfo *info, 
								 const char ***texture_swaps, const char ***material_swaps, 
								 MaterialNamedConstant **material_constants);
WorldCellEntry *createWorldModelInstanceEntryFromParsed(SA_PARAM_NN_VALID ZoneMap *zmap, WorldModelInstanceEntryParsed *entry_parsed, bool parsed_will_be_freed);
WorldModelInstanceInfo *createInstanceInfo(const Mat4 world_matrix, const Vec3 tint_color, const Model *model, WorldPhysicalProperties *props, GroupDef *def);
WorldModelInstanceInfo *createInstanceInfoFromModelEntry(const WorldModelEntry *model_entry);

// splined model drawable
WorldDrawableEntry *createWorldSplinedModelEntry(GroupDef *def, Model *model, const GroupInfo *info, 
												 const char ***texture_swaps, const char ***material_swaps, 
												 MaterialNamedConstant **material_constants);
WorldCellEntry *createWorldSplinedModelEntryFromParsed(SA_PARAM_NN_VALID ZoneMap *zmap, WorldSplinedModelEntryParsed *entry_parsed, bool parsed_will_be_freed);

// drawable stuff
void worldDrawableListPoolReset(SA_PARAM_NN_VALID WorldDrawableListPool *pool_const);
void removeMaterialDrawRefDbg(SA_PARAM_OP_VALID WorldDrawableListPool *pool_const, SA_PRE_OP_VALID SA_POST_P_FREE MaterialDraw *draw_const MEM_DBG_PARMS);
#define removeMaterialDrawRef(pool, draw) removeMaterialDrawRefDbg(pool, draw MEM_DBG_PARMS_CALL)
void removeModelDrawRef(SA_PARAM_OP_VALID WorldDrawableListPool *pool_const, SA_PRE_OP_VALID SA_POST_P_FREE ModelDraw *draw_const MEM_DBG_PARMS);
void removeDrawableSubobjectRefDbg(SA_PARAM_OP_VALID WorldDrawableListPool *pool_const, SA_PRE_OP_VALID SA_POST_P_FREE WorldDrawableSubobject *subobject_const MEM_DBG_PARMS);
#define removeDrawableSubobjectRef(pool, subobject) removeDrawableSubobjectRefDbg(pool, subobject MEM_DBG_PARMS_CALL)

U32 hashDrawableList(const NOCONST(WorldDrawableList) *draw_list, int hashSeed);
void addDrawableListRef(SA_PARAM_OP_VALID WorldDrawableList *draw_list_const MEM_DBG_PARMS);
void removeDrawableListRefDbg(SA_PRE_OP_VALID SA_POST_P_FREE WorldDrawableList *draw_list_const MEM_DBG_PARMS);
void removeDrawableListRefCB(SA_PRE_OP_VALID SA_POST_P_FREE WorldDrawableList *draw_list);
#define removeDrawableListRef(draw_list) removeDrawableListRefDbg(draw_list MEM_DBG_PARMS_CALL)
void freeDrawableListDbg(SA_PRE_OP_VALID SA_POST_P_FREE WorldDrawableList *draw_list_const, bool remove_from_hash_table MEM_DBG_PARMS);
#define freeDrawableList(draw_list, remove_from_hash_table) freeDrawableListDbg(draw_list, remove_from_hash_table MEM_DBG_PARMS_CALL)

void getAllMaterialDraws(SA_PARAM_NN_VALID ZoneMap *zmap, SA_PARAM_NN_VALID MaterialDraw ***draw_array);
void getAllModelDraws(SA_PARAM_NN_VALID ZoneMap *zmap, SA_PARAM_NN_VALID ModelDraw ***draw_array);
void getAllDrawableSubobjects(SA_PARAM_NN_VALID ZoneMap *zmap, SA_PARAM_NN_VALID WorldDrawableSubobject ***subobject_array);
void getAllDrawableLists(SA_PARAM_NN_VALID ZoneMap *zmap, SA_PARAM_NN_VALID WorldDrawableList ***draw_list_array);
void getAllInstanceParamLists(SA_PARAM_NN_VALID ZoneMap *zmap, SA_PARAM_NN_VALID WorldInstanceParamList ***param_list_array, WorldStreamingPooledInfo *old_pooled_info);

MaterialDraw *createMaterialDrawFromParsed(SA_PARAM_NN_VALID ZoneMap *zmap, MaterialDrawParsed *draw_parsed);
ModelDraw *createModelDrawFromParsed(SA_PARAM_NN_VALID ZoneMap *zmap, ModelDrawParsed *draw_parsed);
WorldDrawableSubobject *createDrawableSubobjectFromParsed(SA_PARAM_NN_VALID ZoneMap *zmap, WorldDrawableSubobjectParsed *subobject_parsed);
WorldDrawableList *createDrawableListFromParsed(SA_PARAM_NN_VALID ZoneMap *zmap, WorldDrawableListParsed *draw_list_parsed);
WorldInstanceParamList *createInstanceParamListFromParsed(SA_PARAM_NN_VALID ZoneMap *zmap, WorldInstanceParamListParsed *param_list_parsed);

U32 hashInstanceParamList(const WorldInstanceParamList *param_list, int hashSeed);
void addInstanceParamListRef(SA_PARAM_OP_VALID WorldInstanceParamList *param_list MEM_DBG_PARMS);
void removeInstanceParamListRef(SA_PRE_OP_VALID SA_POST_P_FREE WorldInstanceParamList *param_list MEM_DBG_PARMS);
void removeInstanceParamListRefCB(SA_PRE_OP_VALID SA_POST_P_FREE WorldInstanceParamList *param_list);
void freeInstanceParamList(SA_PRE_OP_VALID SA_POST_P_FREE WorldInstanceParamList *param_list, bool remove_from_hash_table);

WorldCellEntrySharedBounds *createSharedBounds(SA_PARAM_NN_VALID ZoneMap *zmap, const Model *model, const Vec3 local_min, const Vec3 local_max, F32 radius, F32 near_lod_near_dist, F32 far_lod_near_dist, F32 far_lod_far_dist);
WorldCellEntrySharedBounds *createSharedBoundsCopy(SA_PARAM_NN_VALID WorldCellEntrySharedBounds *shared_bounds_src, bool remove_ref);
WorldCellEntrySharedBounds *createSharedBoundsSphere(SA_PARAM_NN_VALID ZoneMap *zmap, F32 radius, F32 near_lod_near_dist, F32 far_lod_near_dist, F32 far_lod_far_dist);
void setSharedBoundsRadius(SA_PARAM_NN_VALID WorldCellEntrySharedBounds *shared_bounds, F32 radius);
void setSharedBoundsMinMax(SA_PARAM_NN_VALID WorldCellEntrySharedBounds *shared_bounds, const Model *model, const Vec3 local_min, const Vec3 local_max);
void setSharedBoundsSphere(SA_PARAM_NN_VALID WorldCellEntrySharedBounds *shared_bounds, F32 radius);
void setSharedBoundsVisDist(SA_PARAM_NN_VALID WorldCellEntrySharedBounds *shared_bounds, F32 near_lod_near_dist, F32 far_lod_near_dist, F32 far_lod_far_dist);
SA_RET_NN_VALID WorldCellEntrySharedBounds *lookupSharedBounds(SA_PARAM_NN_VALID ZoneMap *zmap, SA_PARAM_NN_VALID WorldCellEntrySharedBounds *shared_bounds);
SA_RET_NN_VALID WorldCellEntrySharedBounds *lookupSharedBoundsFromParsed(SA_PARAM_NN_VALID ZoneMap *zmap, SA_PARAM_NN_VALID WorldCellEntrySharedBounds *shared_bounds);
void removeSharedBoundsRef(SA_PRE_OP_VALID SA_POST_P_FREE WorldCellEntrySharedBounds *shared_bounds);
void getAllSharedBounds(SA_PARAM_NN_VALID ZoneMap *zmap, SA_PARAM_NN_VALID WorldCellEntrySharedBounds ***shared_bounds_array, SA_PARAM_OP_VALID WorldStreamingPooledInfo *old_pooled_info);
void worldCellEntryResetSharedBounds(SA_PARAM_NN_VALID ZoneMap *zmap);

void **maintainOldIndices(void **new_struct_array_from_bins, void **old_struct_array_from_bins, int (*cmpFunc)(const void *, const void *), F32 (*distFunc)(const void *, const void *), F32 max_distance);

__forceinline static F32 quantBoundsMax(F32 val)
{
	F32 absval = ABS(val);
	if (absval < 20)
		val = ceilf(val); // quantize to integer multiples of 1
	else if (absval < 100)
		val = 2.f * ceilf(val * 0.5f); // quantize to integer multiples of 2
	else
		val = 4.f * ceilf(val * 0.25f); // quantize to integer multiples of 4
	if (!val)
		val = 0; // deal with -0
	return val;
}

__forceinline static F32 quantBoundsMin(F32 val)
{
	F32 absval = ABS(val);
	if (absval < 20)
		val = floorf(val); // quantize to integer multiples of 1
	else if (absval < 100)
		val =  2.f * floorf(val * 0.5f); // quantize to integer multiples of 2
	else
		val =  4.f * floorf(val * 0.25f); // quantize to integer multiples of 4
	if (!val)
		val = 0; // deal with -0
	return val;
}


#endif //__WORLDCELLENTRYPRIVATE_H__

