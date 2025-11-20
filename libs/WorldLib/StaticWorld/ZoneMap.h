/***************************************************************************



***************************************************************************/

#ifndef _ZONEMAP_H_
#define _ZONEMAP_H_
GCC_SYSTEM

#include "GlobalEnums.h"
#include "WorldLibEnums.h"
#include "ResourceManager.h"

typedef struct AIMastermindDef AIMastermindDef;
typedef struct BasicTexture BasicTexture;
typedef struct BinFileListWithCRCs BinFileListWithCRCs;
typedef struct DisplayMessage DisplayMessage;
typedef struct ExprContext ExprContext;
typedef struct Expression Expression;
typedef struct GenesisMapDescription GenesisMapDescription;
typedef struct GenesisZoneMission GenesisZoneMission;
typedef struct GlobalGAELayerDef GlobalGAELayerDef;
typedef struct GroupDef GroupDef;
typedef struct GroupTracker GroupTracker;
typedef struct Message Message;
typedef struct GenesisMissionZoneChallenge GenesisMissionZoneChallenge;
typedef struct PhotoOptions PhotoOptions;
typedef struct GenesisProceduralEncounterProperties GenesisProceduralEncounterProperties;
typedef struct SceneInfo SceneInfo;
typedef struct SkyInfoGroup SkyInfoGroup;
typedef struct UGCMap UGCMap;
typedef struct WorldRegion WorldRegion;
typedef struct WorldRespawnData WorldRespawnData;
typedef struct WorldVariable WorldVariable;
typedef struct WorldVariableDef WorldVariableDef;
typedef struct WorldZoneMapScope WorldZoneMapScope;
typedef struct ZoneMap ZoneMap;
typedef struct ZoneMapInfo ZoneMapInfo;
typedef struct ZoneMapInfoRequest ZoneMapInfoRequest;
typedef struct ZoneMapLayer ZoneMapLayer;
typedef struct ZoneMapTimeBlock ZoneMapTimeBlock;
typedef struct GenesisZoneMapData GenesisZoneMapData;
typedef struct GenesisZoneMapInfo GenesisZoneMapInfo;
typedef struct RewardTable RewardTable;
typedef struct CharClassCategorySet CharClassCategorySet;

typedef void (*ZmapInfoLockCallback)(ZoneMapInfo *zminfo, void *userdata);

///////////////////////////////////////////////////
//		ZoneMapInfo functions
///////////////////////////////////////////////////

//		Resource dictionary

ZoneMapInfo *					zmapInfoGetByPublicName(const char *name);
void							zmapInfoSetUpdateCallback(resCallback_HandleEvent evt, void *userdata);
void							zmapInfoRemoveUpdateCallback(resCallback_HandleEvent evt);
void							zmapManualFixup(SA_PARAM_NN_VALID ZoneMapInfo *zminfo);

//		Getters

ExprContext *					zmapGetExprContext(void);
bool							zmapInfoIsTestMap(const ZoneMapInfo *zminfo);
bool							zmapInfoIsAvailable(SA_PARAM_OP_VALID const ZoneMapInfo *zminfo, bool allow_private_maps);
SA_ORET_OP_STR const char *		zmapInfoGetPublicName(SA_PARAM_OP_VALID const ZoneMapInfo *zminfo);
SA_ORET_OP_STR const char *		zmapInfoGetCurrentName(SA_PARAM_OP_VALID const ZoneMapInfo *zminfo);
SA_ORET_OP_STR const char *		zmapInfoGetFilename(SA_PARAM_OP_VALID const ZoneMapInfo *zminfo);
void							zmapInfoSetFilenameForDemo(SA_PARAM_NN_VALID ZoneMapInfo *zminfo, SA_PARAM_NN_STR const char* filename);
ZoneMapType						zmapInfoGetMapType(SA_PARAM_OP_VALID const ZoneMapInfo *zminfo);
ZoneMapType						zmapInfoGetMapTypeByName(const char *name);
SA_ORET_OP_STR const char *		zmapInfoGetDefaultQueueDef(SA_PARAM_OP_VALID const ZoneMapInfo *zminfo);
SA_ORET_OP_STR const char *		zmapInfoGetDefaultPVPGameType(SA_PARAM_OP_VALID const ZoneMapInfo *zminfo);
U32								zmapInfoGetDefaultLevelIdx(const ZoneMapInfo *zminfo);
U32								zmapInfoGetMapLevel(SA_PARAM_OP_VALID const ZoneMapInfo *zminfo);
U32								zmapInfoGetMapForceTeamSize(SA_PARAM_OP_VALID const ZoneMapInfo *zminfo);
bool							zmapInfoGetMapIgnoreTeamSizeBonusXP(const ZoneMapInfo *zminfo);
U32								zmapInfoGetNotPlayerVisited(const ZoneMapInfo* zminfo);
U32								zmapInfoGetNoBeacons(const ZoneMapInfo* zminfo);
S32								zmapInfoGetMapDifficulty(const ZoneMapInfo *zminfo);
F32								zmapInfoGetMapSnapOutdoorRes(const ZoneMapInfo* zminfo);
F32								zmapInfoGetMapSnapIndoorRes(const ZoneMapInfo* zminfo);
SA_ORET_OP_OP_STR const char **	zmapInfoGetPrivacy(SA_PARAM_OP_VALID ZoneMapInfo *zminfo);
int							    zmapInfoGetLayerCount(SA_PARAM_OP_VALID const ZoneMapInfo *zminfo);
SA_ORET_OP_STR const char *		zmapInfoGetLayerPath(SA_PARAM_OP_VALID const ZoneMapInfo *zminfo, int i);
SA_ORET_OP_OP_VALID ZoneMapTimeBlock **zmapInfoGetTimeBlocks(SA_PARAM_OP_VALID ZoneMapInfo *zminfo);
bool							zmapInfoAllowEncounterHack(SA_PARAM_OP_VALID const ZoneMapInfo *zminfo); // TomY ENCOUNTER HACK
bool							zmapInfoConfirmPurchasesOnExit(SA_PARAM_OP_VALID const ZoneMapInfo *zminfo);
bool							zmapInfoGetCollectDoorDestStatus(SA_PARAM_OP_VALID const ZoneMapInfo *zminfo);
bool							zmapInfoGetDisableDuels(const ZoneMapInfo *zminfo);
bool							zmapInfoGetPowersRequireValidTarget(const ZoneMapInfo *zminfo);
bool							zmapInfoGetEnableShardVariables(const ZoneMapInfo *zminfo);
bool							zmapInfoGetDisableInstanceChanging(const ZoneMapInfo *zminfo);
bool							zmapInfoGetTeamNotRequired(const ZoneMapInfo *zminfo);
bool							zmapInfoGetGuildNotRequired(const ZoneMapInfo *zminfo);
bool							zmapInfoGetIsGuildOwned(const ZoneMapInfo *zminfo);
bool							zmapInfoGetTerrainStaticLighting(const ZoneMapInfo *zminfo);
bool							zmapInfoGetDisableVisitedTracking(const ZoneMapInfo *zminfo);
bool							zmapInfoGetEffectiveDisableVisitedTracking(const ZoneMapInfo *zminfo);
F32								zmapInfoGetWindLargeObjectRadiusThreshold(SA_PARAM_OP_VALID const ZoneMapInfo *zminfo);
SA_ORET_OP_STR const char*		zmapInfoGetParentMapName(SA_PARAM_OP_VALID const ZoneMapInfo *zminfo);
SA_ORET_OP_STR const char*		zmapInfoGetParentMapSpawnPoint(SA_PARAM_OP_VALID const ZoneMapInfo *zminfo);
SA_ORET_OP_STR const char*		zmapInfoGetStartSpawnName(SA_PARAM_OP_VALID const ZoneMapInfo *zminfo);
ZoneMapInfo*					zmapInfoGetFromRequest(SA_PARAM_NN_VALID ZoneMapInfoRequest *zminfoRequest);
ContainerID						zmapInfoGetUGCProjectID(SA_PARAM_OP_VALID const ZoneMapInfo *zminfo);
bool							zmapInfoGetRecordPlayerMatchStats(SA_PARAM_OP_VALID const ZoneMapInfo *zminfo);
bool							zmapInfoGetEnableUpsellFeatures(SA_PARAM_OP_VALID const ZoneMapInfo *zminfo);
bool							zmapInfoGetRespawnTimes(SA_PARAM_OP_VALID const ZoneMapInfo *zminfo, U32 *min, U32 *max, U32 *increment, U32 *attrition);
ZoneMapUGCUsage					zmapInfoGetUsedInUGC(SA_PARAM_OP_VALID const ZoneMapInfo* zminfo);

//		Display Names

SA_ORET_OP_STR const char *		zmapInfoGetDefaultDisplayNameMsgKey(SA_PARAM_OP_VALID const ZoneMapInfo *zminfo);
SA_ORET_OP_STR const char *		zmapInfoGetDisplayNameMsgKey(SA_PARAM_OP_VALID const ZoneMapInfo *zminfo);
SA_ORET_OP_VALID DisplayMessage *	zmapInfoGetDisplayNameMessage(SA_PARAM_OP_VALID ZoneMapInfo *zminfo);
SA_ORET_OP_VALID Message *		zmapInfoGetDisplayNameMessagePtr(SA_PARAM_OP_VALID ZoneMapInfo *zminfo);

//		Reward Table

SA_ORET_OP_STR const char *		zmapInfoGetRewardTableString(SA_PARAM_OP_VALID ZoneMapInfo *zminfo);
SA_ORET_OP_VALID RewardTable *	zmapInfoGetRewardTable(SA_PARAM_OP_VALID ZoneMapInfo *zminfo);//Uses GET_REF so don't save the reward table pointer
void							zmapInfoSetRewardTable(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, const char *rewardTableKey);

SA_ORET_OP_STR const char *		zmapInfoGetPlayerRewardTableString(SA_PARAM_OP_VALID ZoneMapInfo *zminfo);
SA_ORET_OP_VALID RewardTable *	zmapInfoGetPlayerRewardTable(SA_PARAM_OP_VALID ZoneMapInfo *zminfo);//Uses GET_REF so don't save the reward table pointer
void							zmapInfoSetPlayerRewardTable(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, const char *rewardTableKey);

//		Expressions

Expression *					zmapInfoGetRequiresExpr(SA_PARAM_OP_VALID ZoneMapInfo *zminfo);
void							zmapInfoSetRequiresExpr(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, Expression *expr);

Expression *					zmapInfoGetPermissionExpr(SA_PARAM_OP_VALID ZoneMapInfo *zminfo);
void							zmapInfoSetPermissionExpr(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, Expression *expr);

//		Required Class Category Set

const char *					zmapInfoGetRequiredClassCategorySetString(SA_PARAM_OP_VALID ZoneMapInfo *zminfo);
CharClassCategorySet *			zmapInfoGetRequiredClassCategorySet(SA_PARAM_OP_VALID ZoneMapInfo *zminfo);
void							zmapInfoSetRequiredClassCategorySet(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, const char *categorySet);

//		Mastermind Def
SA_ORET_OP_STR const char *		zmapInfoGetMastermindDefKey(SA_PARAM_OP_VALID ZoneMapInfo *zminfo);
void							zmapInfoSetMastermindDef(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, const char *mastermindDefKey);

//		Civilian Map Def
SA_ORET_OP_STR const char *		zmapInfoGetCivilianMapDefKey(SA_PARAM_OP_VALID ZoneMapInfo *zminfo);
void							zmapInfoSetCivilianMapDef(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, const char *civilianDefKey);

//		Player FSMs
SA_ORET_OP_OP_STR const char **	zmapInfoGetPlayerFSMs(SA_PARAM_OP_VALID ZoneMapInfo* zminfo);

//		Genesis Data

bool							zmapInfoHasGenesisData(SA_PARAM_OP_VALID const ZoneMapInfo *zminfo);
bool							zmapInfoHasBackupGenesisData(SA_PARAM_OP_VALID const ZoneMapInfo *zminfo);
SA_ORET_OP_VALID GenesisZoneMapData *zmapInfoGetGenesisData(SA_PARAM_OP_VALID ZoneMapInfo *zminfo);
SA_ORET_OP_VALID GenesisZoneMapInfo *zmapInfoGetGenesisInfo(SA_PARAM_OP_VALID ZoneMapInfo *zminfo);
SA_ORET_OP_VALID GenesisMapDescription *zmapInfoGetMapDesc(SA_PARAM_OP_VALID ZoneMapInfo *zminfo);
bool							zmapInfoBackupMapDesc(SA_PARAM_OP_VALID ZoneMapInfo *zminfo);
bool							zmapInfoRestoreMapDescFromBackup(SA_PARAM_OP_VALID ZoneMapInfo *zminfo);
bool							zmapInfoRemoveMapDesc(SA_PARAM_OP_VALID ZoneMapInfo *zminfo);
void							zmapInfoClearUGCFile(SA_PARAM_OP_VALID ZoneMapInfo *zminfo);

//		Variables

int								zmapInfoGetVariableCount(SA_PARAM_OP_VALID const ZoneMapInfo *zminfo);
SA_RET_OP_OP_OP_VALID WorldVariableDef ***zmapInfoGetVariableDefs(SA_PARAM_OP_VALID ZoneMapInfo *zminfo);
SA_RET_OP_VALID WorldVariableDef *zmapInfoGetVariableDef(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, int var_idx);
SA_RET_OP_VALID WorldVariableDef *zmapInfoGetVariableDefByName(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, const char *name);
void							zmapInfoAddVariableDef(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, WorldVariableDef *var);
void							zmapInfoRemoveVariableDef(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, int var_idx);
void							zmapInfoModifyVariableDef(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, int var_idx, WorldVariableDef *def);
bool							zmapInfoValidateVariableDefs(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, WorldVariableDef **varDefs, const char *reason, const char *filename);
SA_ORET_OP_OP_STR char**		zmapInfoGetVariableNames(SA_PARAM_OP_VALID ZoneMapInfo *zminfo);

//		GAE Layers

int								zmapInfoGetGAELayersCount(SA_PARAM_OP_VALID const ZoneMapInfo *zminfo);
SA_RET_OP_VALID GlobalGAELayerDef *zmapInfoGetGAELayerDef(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, int var_idx);
void							zmapInfoAddGAELayerDef(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, GlobalGAELayerDef *var);
void							zmapInfoRemoveGAELayerDef(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, int var_idx);
void							zmapInfoModifyGAELayerDef(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, int var_idx, GlobalGAELayerDef *def);

//		Regions

SA_ORET_OP_OP_VALID WorldRegion **zmapInfoGetWorldRegions(SA_PARAM_OP_VALID ZoneMapInfo *zminfo);
// Also gets regions of secondary maps
SA_ORET_OP_OP_VALID WorldRegion **zmapInfoGetAllWorldRegions(SA_PARAM_OP_VALID ZoneMapInfo *zminfo);

U32								zmapInfoHasSpaceRegion(ZoneMapInfo *zminfo);

//		Basic Editing Functions

SA_RET_NN_VALID ZoneMapInfo *		zmapInfoNew(const char *filename, const char *map_name);
SA_RET_OP_VALID ZoneMapInfo *		zmapInfoCopy(SA_PARAM_OP_VALID ZoneMapInfo *zminfo_src, const char *filename, const char *map_name);
void							zmapInfoSetModified(SA_PARAM_OP_VALID ZoneMapInfo *zminfo);
void							zmapInfoSetName(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, const char *map_name);
void							zmapInfoSetMapType(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, ZoneMapType map_type);
void							zmapInfoSetRespawnType(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, ZoneRespawnType respawn_type);
void							zmapInfoSetRespawnWaveTime(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, U32 respawn_time);
void							zmapInfoSetMapLevel(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, U32 level);
void							zmapInfoSetMapDifficulty(ZoneMapInfo *zminfo, S32 eDifficulty);
void							zmapInfoSetMapForceTeamSize(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, U32 force_team_size);
void							zmapInfoSetMapIgnoreTeamSizeBonusXP(ZoneMapInfo *zminfo, bool bIgnoreTeamSizeBonusXP);
void							zmapInfoSetMapUsedInUGC(ZoneMapInfo *zminfo, ZoneMapUGCUsage eUsedInUGC);
void							zmapInfoSetDisplayNameMessage(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, const Message *message);
void							zmapInfoClearPrivacy(SA_PARAM_OP_VALID ZoneMapInfo *zminfo);
void							zmapInfoAddPrivacy(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, const char *privacy);
SA_RET_OP_VALID PhotoOptions *		zmapInfoGetPhotoOptions(SA_PARAM_OP_VALID ZoneMapInfo *zmingo, bool make);
void							zmapInfoSetWindLargeObjectRadiusThreshold(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, F32 radius);
void							zmapInfoSetCollectDoorDestStatus(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, bool state);
void							zmapInfoSetDisableDuels(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, bool disable);
void							zmapInfoSetPowersRequireValidTarget(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, bool enable);
void							zmapInfoSetEnableShardVariables(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, bool allow);
void							zmapInfoSetTerrainStaticLighting(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, bool enable);
void							zmapInfoSetDisableInstanceChanging(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, bool disable);
void							zmapInfoSetTeamNotRequired(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, bool enable);
void							zmapInfoSetGuildNotRequired(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, bool enable);
void							zmapInfoSetGuildOwned(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, bool enable);
void							zmapInfoSetStartSpawnName(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, const char *spawn_name);
void							zmapInfoSetDisableVisitedTracking(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, bool val);
void							zmapInfoSetRecordPlayerMatchStats(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, bool val);
void							zmapInfoSetEnableUpsellFeatures(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, bool val);
void							zmapInfoSetRespawnTimes(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, U32 min, U32 max, U32 increment);
void							zmapInfoSetRespawnMinTime(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, U32 min);
void							zmapInfoSetRespawnMaxTime(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, U32 max);
void							zmapInfoSetRespawnIncrementTime(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, U32 increment);
void							zmapInfoSetRespawnAttritionTime(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, U32 attrition);

//		PVP Data

void							zmapInfoSetDefaultQueueDef(ZoneMapInfo *zminfo, const char *pchQueueDef);
void							zmapInfoSetDefaultPVPGameType(ZoneMapInfo *zminfo, const char *pchGameType);


#ifndef NO_EDITORS

//		Genesis Data Editing

SA_ORET_OP_VALID GenesisZoneMapData *zmapInfoAddGenesisData(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, UGCMapType ugc_type);
void							zmapInfoSetMapDesc(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, SA_PARAM_OP_VALID GenesisMapDescription *map_desc);
void							zmapInfoSetGenesisZoneMissions(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, GenesisZoneMission **data);
void							zmapInfoSetSharedGenesisZoneChallenges(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, GenesisMissionZoneChallenge **challenges);
void							zmapInfoSetEncounterOverrides(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, GenesisProceduralEncounterProperties **properties); // TomY ENCOUNTER_HACK

#endif

//      UGC-specific functions
SA_ORET_OP_VALID ZoneMapInfo *	zmapInfoCreateUGCDummy(const char *map_name, const char *project_prefix, const char *map_filename, const char *display_name, ContainerID ugcProjectID, const char *spawn_name, ZoneMapLightOverrideType light_type);
void							zmapInfoDestroyUGCDummy(SA_PRE_NN_VALID SA_POST_P_FREE ZoneMapInfo *zminfo);

//		Layer Editing

void							zmapInfoAddLayer(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, const char *filename, const char *region_name);;
void							zmapInfoSetLayerRegion(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, int idx, const char *region_name);
void							zmapInfoRemoveLayer(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, int idx);

//		Locking

void							zmapInfoLockEx(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, bool forceGenesisDataLock);
__forceinline void				zmapInfoLock(SA_PARAM_OP_VALID ZoneMapInfo *zminfo) {zmapInfoLockEx(zminfo, false); }
bool							zmapInfoLocked(SA_PARAM_OP_VALID ZoneMapInfo *zminfo);
U32								zmapInfoGetLockOwner(SA_PARAM_OP_VALID ZoneMapInfo *zminfo);
bool							zmapInfoGetLockOwnerIsZero(SA_PARAM_OP_VALID ZoneMapInfo *zminfo);
void							zmapInfoLockQueueActions(SA_PARAM_NN_VALID ResourceActionList* actions, SA_PARAM_NN_VALID ZoneMapInfo *zminfo, bool forceGenesisDataLock);

//		Saving

bool							zmapInfoSave(SA_PARAM_OP_VALID ZoneMapInfo *zminfo);
bool							zmapInfoGetUnsaved(SA_PARAM_OP_VALID ZoneMapInfo *zminfo);
void							zmapInfoSetSaved(SA_PARAM_OP_VALID ZoneMapInfo *zminfo);
int								zmapInfoGetModTime(SA_PARAM_OP_VALID ZoneMapInfo *zminfo);

SA_RET_OP_STR const char *	zmapInfoGetDefaultMessageScope(SA_PARAM_OP_VALID ZoneMapInfo *zminfo);

///////////////////////////////////////////////////
//		ZoneMap functions
///////////////////////////////////////////////////

SA_ORET_OP_VALID ZoneMapInfo *	zmapGetInfo(SA_PARAM_OP_VALID ZoneMap *zmap);
SA_ORET_OP_VALID ZoneMap *		zmapLoad(SA_PARAM_NN_VALID ZoneMapInfo *zminfo);
bool							zmapSaveLayersEx(ZoneMap *zmap, const char *filename, bool force, bool asynchronous, bool skipReferenceLayers);
#define							zmapSaveLayers(zmap, filename, force, asynchronous) zmapSaveLayersEx(zmap, filename, force, asynchronous, false)
void							zmapUnload(SA_PRE_NN_VALID SA_POST_P_FREE ZoneMap *zmap);
bool							zmapIsSaving(SA_PARAM_OP_VALID ZoneMap *zmap);
bool							zmapOrLayersUnsaved(SA_PARAM_OP_VALID ZoneMap *zmap);
bool							zmapCheckFailedValidation(ZoneMap *zmap);

void							zmapGetOffset(SA_PARAM_OP_VALID ZoneMap *zmap, SA_PRE_NN_BYTES(sizeof(Vec3)) SA_POST_NN_VALID Vec3 offset);
SA_ORET_OP_STR const char *		zmapGetDefaultVariableMsgKey(ZoneMap *zmap, int var_idx);
SA_ORET_OP_STR const char*		zmapGetFilename(SA_PARAM_OP_VALID const ZoneMap *zmap);
SA_ORET_OP_STR const char*		zmapGetName(SA_PARAM_OP_VALID const ZoneMap *zmap);
SA_ORET_OP_VALID WorldRegion *	zmapGetWorldRegionByNameEx(SA_PARAM_OP_VALID ZoneMap *zmap, SA_PARAM_OP_STR const char *name, bool create_if_null);

SA_ORET_NN_VALID __forceinline static WorldRegion *zmapGetWorldRegionByName(SA_PARAM_OP_VALID ZoneMap *zmap, SA_PARAM_OP_STR const char *name)
{
	WorldRegion *region = zmapGetWorldRegionByNameEx(zmap, name, true);
	ANALYSIS_ASSUME(region);
	return region;
}

ZoneRespawnType					zmapInfoGetRespawnType(const ZoneMapInfo *zminfo);
U32								zmapInfoGetRespawnWaveTime(const ZoneMapInfo *zminfo);

bool							zmapIsUGCGeneratedMap(ZoneMap *zmap);

GenesisEditType					zmapGetGenesisEditType(SA_PARAM_OP_VALID ZoneMap *zmap);
void							zmapSetGenesisEditType(SA_PARAM_OP_VALID ZoneMap *zmap, GenesisEditType type);
void							zmapSetGenesisViewType(SA_PARAM_OP_VALID ZoneMap *zmap, GenesisViewType type);
void							zmapSetPreviewFlag(SA_PARAM_OP_VALID ZoneMap *zmap);

int								zmapGetLayerCount(SA_PARAM_OP_VALID ZoneMap *zmap);
SA_RET_OP_VALID ZoneMapLayer *		zmapGetLayer(SA_PARAM_OP_VALID ZoneMap *zmap, int layer_idx);
SA_RET_OP_VALID GroupTracker *		zmapGetLayerTracker(SA_PARAM_OP_VALID ZoneMap *zmap, int layer_idx);
SA_RET_OP_VALID ZoneMapLayer *		zmapGetLayerByName(SA_PARAM_OP_VALID ZoneMap *zmap, SA_PARAM_NN_STR const char *layer_filename);
void							zmapTrackerUpdate(SA_PARAM_OP_VALID ZoneMap *zmap, bool force, bool update_terrain);

void							zmapGetBounds(SA_PARAM_OP_VALID ZoneMap *zmap, SA_PRE_NN_ELEMS(3) SA_POST_OP_VALID Vec3 world_min, SA_PRE_NN_ELEMS(3) SA_POST_OP_VALID Vec3 world_max);

//		locking
bool							zmapLocked(ZoneMap *zmap);
bool							zmapGenesisDataLocked(ZoneMap *zmap);

//		layers
SA_RET_OP_VALID ZoneMapLayer *		zmapNewLayer(SA_PARAM_OP_VALID ZoneMap *zmap, int layer_idx, SA_PARAM_NN_STR const char *layer_filename);
SA_RET_OP_VALID ZoneMapLayer *		zmapAddLayer(SA_PARAM_OP_VALID ZoneMap *zmap, SA_PARAM_NN_STR const char *layer_filename, SA_PARAM_OP_STR const char *layer_name, SA_PARAM_OP_STR const char *region_name);
void							zmapSetLayerRegion(SA_PARAM_OP_VALID ZoneMap *zmap, ZoneMapLayer *layer, const char *region_name);
void							zmapRemoveLayer(SA_PARAM_OP_VALID ZoneMap *zmap, int layer_idx);
void							zmapFixupLayerFilenames(SA_PARAM_OP_VALID ZoneMap *zmap);

//		regions
void							zmapRegionSetOverrideCubeMap(SA_PARAM_OP_VALID ZoneMap *zmap, WorldRegion *region, const char *override_cubemap);
void							zmapRegionSetAllowedPetsPerPlayer(ZoneMap *zmap, WorldRegion *region, S32 iAllowedPetsPerPlayer);
void							zmapRegionSetUnteamedPetsPerPlayer(ZoneMap *zmap, WorldRegion *region, S32 iUnteamedPetsPerPlayer);
void							zmapRegionSetVehicleRules(SA_PARAM_OP_VALID ZoneMap *zmap, WorldRegion *region, S32 eVechicleRules);
void							zmapRegionSetSkyGroup(SA_PARAM_OP_VALID ZoneMap *zmap, WorldRegion *region, SkyInfoGroup *sky_group);
void							zmapRegionSetType(SA_PARAM_OP_VALID ZoneMap *zmap, WorldRegion *region, WorldRegionType type);
void							zmapRegionSetWorldGeoClustering(SA_PARAM_OP_VALID ZoneMap *zmap, WorldRegion *region, bool bWorldGeoClustering);
void							zmapRegionSetIndoorLighting(SA_PARAM_OP_VALID ZoneMap *zmap, WorldRegion *region, bool bIndoorLighting);

//		genesis data
void							zmapCommitGenesisData(SA_PARAM_OP_VALID ZoneMap *zmap);

//		scope data
SA_RET_OP_VALID WorldZoneMapScope *zmapGetScope(SA_PARAM_OP_VALID ZoneMap *zmap);

//		external dependencies
SA_RET_OP_VALID BinFileListWithCRCs *zmapGetExternalDepsList(SA_PARAM_OP_VALID ZoneMap *zmap);

// Called at end of make bins and exit
void							zmapForceBinEncounterInfo(void);

// Latelink
LATELINK;
bool isValidRegionTypeForGame(U32 world_region_type);

#endif //_ZONEMAP_H_

