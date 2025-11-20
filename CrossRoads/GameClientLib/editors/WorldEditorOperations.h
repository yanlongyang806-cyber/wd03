#ifndef __WORLDEDITOROPERATIONS_H__
#define __WORLDEDITOROPERATIONS_H__
GCC_SYSTEM

#ifndef NO_EDITORS

#include "StashTable.h"
#include "EditLib.h"
#include "Encounter_enums.h"

typedef struct Expression Expression;
typedef struct GroupTracker GroupTracker;
typedef struct TrackerHandle TrackerHandle;
typedef struct Message Message;
typedef struct ZoneMapLayer ZoneMapLayer;
typedef struct ZoneMapInfo ZoneMapInfo;
typedef struct WorldRegion WorldRegion;
typedef struct SkyInfoGroup SkyInfoGroup;
typedef struct WleEncObjSubHandle WleEncObjSubHandle;
typedef struct WorldVariableDef WorldVariableDef;
typedef struct GenesisRuntimeStatus GenesisRuntimeStatus;

typedef struct GlobalGAELayerDef GlobalGAELayerDef;

typedef enum ZoneMapLayerType ZoneMapLayerType;
typedef enum ResourcePackType ResourcePackType;
typedef enum ZoneMapType ZoneMapType;
typedef enum WorldRegionType WorldRegionType;
typedef enum WleUINewMapType WleUINewMapType;

typedef void (*WleZoneMapCBFn)(ZoneMapInfo *zminfo, bool success, void *userdata);

// UTIL
void wleOpRefreshUI(void);

// CLIENT-SIDE OPERATIONS
void wleOpCreateEx(SA_PARAM_NN_VALID const TrackerHandle *parentHandle, GroupDef **defs, Mat4 *mats, F32 *scales, int index, TrackerHandle ***newHandles);
void wleOpCreateList(const TrackerHandle **parentHandles, GroupDef **defs, Mat4 **mats, F32 *scales, int *indices, TrackerHandle ***newHandles, bool performSelection);
SA_RET_OP_VALID TrackerHandle *wleOpCreate(SA_PARAM_NN_VALID const TrackerHandle *parentHandle, int uid, SA_PARAM_NN_VALID Mat4 mat, F32 scale);
void wleOpTouch(const TrackerHandle **handles);
void wleOpInstance(SA_PARAM_NN_VALID const TrackerHandle *handle);
bool wleOpRename(SA_PARAM_NN_VALID const TrackerHandle *handle, SA_PARAM_NN_STR const char *newName);
bool wleOpSetTags(SA_PARAM_NN_VALID const TrackerHandle *handle, SA_PARAM_NN_STR const char *newTags);
void wleOpGroup(TrackerHandle **handles, SA_PARAM_NN_VALID Mat4 pivotMat);
void wleOpAddToGroup(SA_PARAM_NN_VALID const TrackerHandle *parentHandle, TrackerHandle **handles, int index);
void wleOpUngroup(SA_PARAM_NN_VALID const TrackerHandle *handle);
bool wleOpMove(const TrackerHandle **handles, Mat4 *mats);
bool wleOpSetScale(TrackerHandle **handles, Vec3 *scales);
bool wleOpCopyEx(SA_PARAM_NN_VALID const TrackerHandle *parent, TrackerHandle **handles, bool finishUp);
#define wleOpCopy(parent, handles) wleOpCopyEx(parent, handles, true);
bool wleOpDeleteEx(TrackerHandle **handles, bool verbose);
#define wleOpDelete(handles) wleOpDeleteEx(handles, true)
void wleOpReplaceEx(SA_PARAM_NN_VALID const TrackerHandle *parentHandle, TrackerHandle **handles, GroupDef **defs, Mat4 *mats, TrackerHandle ***newHandles);
void wleOpReplaceEach(const TrackerHandle **parentHandles, TrackerHandle **handles, GroupDef **defs, Mat4 **mats, TrackerHandle ***newHandles, bool performSelection);
void wleOpFindAndReplace(SA_PARAM_NN_VALID const TrackerHandle *root, const StashTable replacements);
void wleOpSaveToLib(SA_PARAM_NN_VALID const TrackerHandle *handle, SA_PARAM_NN_STR const char *libPath, SA_PARAM_NN_STR const char *defName);
void wleOpDeleteFromLib(int uid);
void wleOpReseed(const TrackerHandle **handles);
void wleOpSnapDown(SA_PARAM_NN_VALID const TrackerHandle *handle);

void wleOpTransactionBegin();
void wleOpTransactionEnd();

// PROPERTIES-RELATED OPERATIONS AND FUNCTIONS
SA_ORET_OP_VALID GroupTracker *wleOpPropsBegin(SA_PARAM_OP_VALID const TrackerHandle *handle);
void wleOpPropsUpdate(void);
void wleOpPropsEnd(void);
// If you call this one, you have to call wleOpRefreshUI
void wleOpPropsEndNoUIUpdate(void);

// ENCOUNTER OPERATIONS
bool wleOpSetUniqueScopeName(SA_PARAM_OP_VALID TrackerHandle *scopeHandle, SA_PARAM_NN_VALID TrackerHandle *handle, SA_PARAM_NN_STR const char *name);
void wleOpSetUniqueZoneMapScopeName(SA_PARAM_NN_VALID TrackerHandle *handle, SA_PARAM_NN_STR const char *new_name);
bool wleOpLogicalGroup(SA_PARAM_OP_VALID TrackerHandle *scopeHandle, SA_PARAM_OP_STR const char *groupName, const char **uniqueNames, bool validateOnly);
void wleOpAddToLogicalGroup(SA_PARAM_OP_VALID TrackerHandle *scopeHandle, SA_PARAM_NN_STR const char *parentName, const char **uniqueNames, int index);
void wleOpLogicalUngroup(SA_PARAM_OP_VALID TrackerHandle *scopeHandle, SA_PARAM_NN_STR const char *uniqueName);
bool wleOpSetLogicalGroupUniqueScopeName(SA_PARAM_OP_VALID TrackerHandle *scopeHandle, SA_PARAM_NN_STR const char *oldName, SA_PARAM_NN_STR const char *newName);

int wleOpAddPatrolPoint(const TrackerHandle *patrol, int index, Vec3 worldPos);
bool wleOpMovePatrolPoints(const WleEncObjSubHandle **points, Vec3 *pos);
bool wleOpMovePatrolPointToIndex(SA_PARAM_NN_VALID const WleEncObjSubHandle *point, int index);
#define wleOpDeletePatrolPoints(points) wleOpDeletePatrolPointsEx(points, true);
bool wleOpDeletePatrolPointsEx(const WleEncObjSubHandle **points, bool verbose);
bool wleOpDuplicatePatrolPoints(const WleEncObjSubHandle **points);

int wleOpAddEncounterActor(const TrackerHandle *encounter, int index, Mat4 worldMat);
bool wleOpMoveEncounterActors(const WleEncObjSubHandle **actors, Mat4 *worldMats);
bool wleOpMoveEncounterActorToIndex(SA_PARAM_NN_VALID const WleEncObjSubHandle *actor, int index);
#define wleOpDeleteEncounterActors(actors) wleOpDeleteEncounterActorsEx(actors, true);
bool wleOpDeleteEncounterActorsEx(const WleEncObjSubHandle **actors, bool verbose);
bool wleOpDuplicateEncounterActors(const WleEncObjSubHandle **actors);

// MAP OPERATIONS
void wleOpLockZoneMapEx(WleZoneMapCBFn fn, void *userdata, bool forceGenesisDataLock);
__forceinline void wleOpLockZoneMap(WleZoneMapCBFn fn, void *userdata) { wleOpLockZoneMapEx( fn, userdata, false ); }
void wleOpNewZoneMap(const char *dir, const char *fileName, int width, int length, WleUINewMapType world_type, ZoneMapType type, Message *display_name, SA_PARAM_OP_STR const char *publicName, bool createSubfolder);
void wleOpNewLayer(const char *fileName, const char *regionName);
void wleOpImportLayer(SA_PARAM_NN_STR const char *layerName);
void wleOpDeleteLayer(SA_PARAM_NN_STR const char *layerName);
void wleOpSplitLayers(ZoneMapLayer **layers, ZoneMapLayer *dest_layer, IVec2 min, IVec2 max);
void wleOpSetLayerRegion(const char **layerFilenames, const char *regionName);
void wleOpSetRegionOverrideCubemap(SA_PARAM_NN_VALID WorldRegion *region, SA_PARAM_NN_STR const char *cubemapTexName);
void wleOpSetRegionMaxPets(SA_PARAM_NN_VALID WorldRegion *region, int maxPets);
void wleOpSetVehicleRulesChanged(SA_PARAM_NN_VALID WorldRegion *region, int eVehicleRules);
void wleOpSetRegionSkyGroup(SA_PARAM_NN_VALID WorldRegion *region, SkyInfoGroup *sky_group);
void wleOpSetRegionType(SA_PARAM_NN_VALID WorldRegion *region, WorldRegionType type);
void wleOpSetRegionClusterWorldGeo(WorldRegion *region, bool bWorldGeoClustering);
void wleOpSetRegionIndoorLighting(SA_PARAM_NN_VALID WorldRegion *region, bool bIndoorLighting);
void wleOpSetPublicName(const char *publicName);
void wleOpSetMapType(ZoneMapType mapType);
void wleUISetDefaultQueueDef(const char *pchQueueDef);
void wleUISetPVPGameType(const char *pchGameType);
void wleOpSetMapRespawnType(ZoneRespawnType eRespawnType);
void wleOpSetMapRespawnWaveTime(U32 respawn_time);
void wleOpSetMapRespawnMinTime(U32 respawn_time);
void wleOpSetMapRespawnMaxTime(U32 respawn_time);
void wleOpSetMapRespawnIncrementTime(U32 respawn_time);
void wleOpSetMapRespawnAttritionTime(U32 respawn_time);
void wleOpSetMapLevel(U32 level);
void wleOpSetMapDifficulty(EncounterDifficulty eDifficulty);
void wleOpSetMapForceTeamSize(U32 force_team_size);
void wleOpSetMapIgnoreTeamSizeBonusXP(bool bIgnoreTeamSizeBonusXP);
void wleOpSetMapUsedInUGC(bool bUsedInUGC);
void wleOpSetMapDisplayName(const Message *message);
void wleOpSetMapPrivacy(const char *privacyList);
void wleOpSetMapParentMapName(const char *parentMapName);
void wleOpSetMapParentMapSpawn(const char *parentMapSpawnName);
void wleOpSetMapStartSpawn(const char *startSpawnName);
void wleOpSetMapRewardTable(const char *rewardTableKey);
void wleOpSetMapPlayerRewardTable(const char *rewardTableKey);
void wleOpSetMapRequiresExpr(Expression* expr);
void wleOpSetMapPermissionExpr(Expression* expr);
void wleOpSetMapRequiredClassCategorySet(const char* categorySet);
void wleOpSetMapMastermindDef(const char *mastermindDefKey);
void wleOpSetMapCivilianMapDef(const char *civilianMapDefKey);
void wleOpSetMapDisableVisitedTracking(bool disableVisitedTracking);
void wleOpAddVariable(WorldVariableDef *var);
void wleOpRemoveVariable(int index);
void wleOpModifyVariable(int index, WorldVariableDef *var);
void wleOpOpenZoneMap(const char *dir, const char *fileName);
void wleOpSave(void);
void wleOpSaveZoneMapAs(const char *dir, const char *fileName, const char *publicName, bool createSubfolder, bool layersAsRefrence, bool keepExistingReferenceLayers);
void wleOpReloadFromSource(void);
void wleOpUGCPublish(void);
typedef struct TimedCallback TimedCallback;
GenesisRuntimeStatus *wleOpGenesisRegenerate(bool seed_layout, bool seed_detail);
void wleOpGenesisSetEditType(GenesisEditType type);

void wleOpAddGlobalGAELayer(GlobalGAELayerDef *var);
void wleOpRemoveGlobalGAELayer(int index);
void wleOpModifyGlobalGAELayer(int index, GlobalGAELayerDef *var);

// SERVER-SIDE OPERATIONS
void wleOpLockFiles(const char **fileNames, EditAsyncOpFunction callback, void *userdata);
void wleOpLockFile(const char *fileName, EditAsyncOpFunction callback, void *userdata);
void wleOpCommitGenesisData();
void wleOpRestoreGenesisData();
void wleOpUnlockFiles(const char **fileNames, bool save, EditAsyncOpFunction callback, void *userdata);
void wleOpUnlockFile(const char *fileName, EditAsyncOpFunction callback, void *userdata);
void wleOpSaveAndUnlockFile(const char *fileName, EditAsyncOpFunction callback, void *userdata);
void wleOpListLocks(void);

#endif // NO_EDITORS

#endif // __WORLDEDITOROPERATIONS_H__
