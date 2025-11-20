/***************************************************************************



***************************************************************************/

#ifndef _WORLDGRIDLOAD_H_
#define _WORLDGRIDLOAD_H_
GCC_SYSTEM

#include "group.h"

typedef struct LibFileLoad LibFileLoad;
typedef struct ZoneMapInfo ZoneMapInfo;
typedef struct ZoneMapLayer ZoneMapLayer;
typedef struct UGCBinningData UGCBinningData;

typedef void (*voidVoidFunc)(void);
typedef void (*mapFunc)(ZoneMapInfo *zminfo);

SA_RET_OP_STR char *worldMakeBinName(SA_PARAM_NN_STR const char *fname);
SA_RET_OP_STR char *worldMakeBinDir(SA_PARAM_NN_STR const char *fname);

void groupInvalidateBounds(GroupDef *def);

void groupSetModelRecursive(GroupDef *def, bool force); // Sets the model pointer for the GroupDef and all children. If already set and not forced, doies nothing.

LibFileLoad *worldLoadLayerGroupFile(ZoneMapLayer *layer, const char *filename);
void groupLibFromLoaded(GroupDef ***defs, int file_version, GroupDefLib *def_lib, ZoneMapLayer *layer, const char *fname, bool object_library);
void worldLoadBeacons(void);

bool worldGridShouldDeleteUntouchedFiles(void);
void worldGridMakeAllBins(bool remove, bool do_memdump, bool do_validate); // Writes to slave_file (Server only)
void worldGridDeleteAllBins(void);
void worldGridDeleteAllOutdatedBins(void);
void worldGridDeleteBinsForZoneMap(const char *mapName);
void worldGridFixupAllMaps(bool dry_run);
void worldGridLoadAllMaps(voidVoidFunc callback);
void worldForEachMap(mapFunc callback);
void worldGridCalculateDependencies(char *filename);

LibFileLoad *loadLayerFromDisk(const char *filename);
LibFileLoad *loadLibFromDisk(const char *filename);

void groupEnsureValidVersion(GroupDef *defload);
void groupFixupAfterRead(GroupDef *defload, bool useBitfield);
void groupFixupChildren(GroupDef *defload);
void groupPostLoad(GroupDef *def);

void worldFixupVolumeInteractions(GroupDef *def, WorldVolumeEntry *entry, GroupVolumePropertiesServer *volume_properties);

void reloadFileLayer(const char *relpath);

void validateEncounterLayerProperties(WorldEncounterLayerProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references);
void validateInteractionProperties(WorldInteractionProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references);
void validatePlanetProperties(WorldPlanetProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references);
void validateSkyVolumeProperties(WorldSkyVolumeProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references);
void validateDebrisFieldProperties(WorldDebrisFieldProperties *properties, GroupDef *def, const char *filename, const char *group_name, int group_uid, bool check_references);
void validateSoundVolumeProperties(WorldSoundVolumeProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references);
void validateSoundConnProperties(WorldSoundConnProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references);
void validateAnimationProperties(WorldAnimationProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references);
void validateCurve(WorldCurve *curve, const char *filename, const char *group_name, int group_uid, bool check_references);
void validateCurveGaps(WorldCurveGaps *curve_gaps, const char *filename, const char *group_name, int group_uid, bool check_references);
void validateChildCurve(WorldChildCurve *parameters, const char *filename, const char *group_name, int group_uid, bool check_references);
void validateLODOverride(WorldLODOverride *lod_override, const char *filename, const char *group_name, int group_uid, bool check_references);
void validateEncounterHackProperties(WorldEncounterHackProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references);
void validateEncounterProperties(WorldEncounterProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references);
void validateActionVolumeProperties(WorldActionVolumeProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references);
void validatePowerVolumeProperties(WorldPowerVolumeProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references);
void validateWarpVolumeProperties(WorldWarpVolumeProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references);
void validateLandmarkVolumeProperties(WorldLandmarkVolumeProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references);
void validateNeighborhoodVolumeProperties(WorldNeighborhoodVolumeProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references);
void validateInteractionVolumeProperties(WorldInteractionProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references);
void validateAIVolumeProperties(WorldAIVolumeProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references);
void validateBeaconVolumeProperties(WorldBeaconVolumeProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references);
void validateEventVolumeProperties(WorldEventVolumeProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references);
void validateSpawnProperties(WorldSpawnProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references);
void validatePatrolProperties(WorldPatrolProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references);
void validateWaterVolumeProperties(WorldWaterVolumeProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references);
void validateIndoorVolumeProperties(WorldIndoorVolumeProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references);
void validateFXVolumeProperties(WorldFXVolumeProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references);
void validateTriggerConditionProperties(WorldTriggerConditionProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references);
void validateLayerFSMProperties(WorldLayerFSMProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references);
void validateAutoPlacementProperties(WorldAutoPlacementProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references);
void validateCivilianVolumeProperties(WorldCivilianVolumeProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references);
void validateMastermindVolumeProperties(WorldMastermindVolumeProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references);
void validateGenesisChallengeProperties(WorldGenesisChallengeProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references);
void validateTerrainExclusionProperties(WorldTerrainExclusionProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references);
void validateCivilianPOIProperties(WorldCivilianPOIProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references);
void validateWindSourceProperties(WorldWindSourceProperties *properties, const char *filename, const char *group_name, int group_uid, bool check_references);
// GroupProps : add validate function declaration here


void validateGroupPropertyStructs(GroupDef *def, const char *filename, const char *name_str, int name_uid, bool check_references);

GroupChild **generateBuildingChildren(GroupDef *def, const Mat4 world_mat, U32 parent_seed);

int worldLoadObjectPreviewTimestamps(StashTable timestamp_table);

#ifndef NO_EDITORS

// Source data reporting functions
void worldGridCalculateMapLayerSizes(void);

#endif



#endif //_WORLDGRIDLOAD_H_
