//// The Genesis system.
////
//// Genesis is a system to generate worlds from abstract
//// descriptions.  It generates a complete ZoneMap, along with
//// missions to complete inside of that map.
#pragma once
GCC_SYSTEM

#if !defined(GENESIS_ALLOW_OLD_HEADERS)
#error Including this file was probably a mistake.  UGC should include the appropriate gslUGC*.h files.
#endif

#ifndef NO_EDITORS

#include "WorldLibEnums.h"
#include "GlobalEnums.h"

typedef struct ZoneMap ZoneMap;
typedef struct ZoneMapInfo ZoneMapInfo;
typedef struct GenesisRuntimeStatus GenesisRuntimeStatus;
typedef struct GenesisMapDescription GenesisMapDescription;
typedef struct GenesisEpisode GenesisEpisode;
typedef struct GenesisZoneMapData GenesisZoneMapData;

/// Helper Functions
void genesisMakeRoomNamesList(GenesisMapDescription *pMapDesc, char ***pppNameList);

/// Transmogrification functions
GenesisRuntimeStatus *genesisReseedMapDesc(int iPartitionIdx, ZoneMap *zmap, bool seed_layout, bool seed_detail, const char* external_map_name);
bool genesisTransmogrifyMapDesc(ZoneMapInfo *zminfo, GenesisRuntimeStatus *gen_status, const char* map_prefix);
GenesisRuntimeStatus *genesisCreateExternalMap(int iPartitionIdx, GenesisMapDescription *map_desc, const char *file_name, const char *map_name, const char *map_display_name, U32 seed, U32 detail_seed, bool move_to_nodes, bool force_create_zmap);
GenesisRuntimeStatus* genesisUnfreeze(ZoneMapInfo* zmapInfo);

/// Mass-transmogrification handling
void genesisReseedExternalMapDescs(int iPartitionIdx, GenesisRuntimeStatus* genStatus, ZoneMapInfo** zmapInfos, bool seed_layout, bool seed_detail);
void genesisFreezeExternalMapDescs(int iPartitionIdx, GenesisRuntimeStatus* genStatus, ZoneMapInfo** zmapInfos, bool forVistas);
void genesisUnfreezeExternalMapDescs(GenesisRuntimeStatus* genStatus, ZoneMapInfo** zmapInfos);
void genesisSetExternalMapsType(GenesisRuntimeStatus* genStatus, ZoneMapInfo** zmapInfos, ZoneMapType zmtype);


/// Concrete -> Runtime conversion functions
void genesisGenerate(int iPartitionIdx, ZoneMap *zmap, bool preview_mode, bool write_layers);
void genesisGenerateMissionsOnServer(ZoneMapInfo* zmapInfo);
void genesisGetSpawnPosition(int iPartitionIdx, WorldRegionType region_type, int idx, Vec3 spawn_pos_ret);

/// Episode functions
GenesisRuntimeStatus* genesisCreateEpisode(int iPartitionIdx, GenesisEpisode* episode, const char* episode_root, U32 seed, U32 deatail_seed);
void genesisGenerateEpisodeMissionOnServer(const char* episodeRoot, GenesisEpisode* episode);

#endif
