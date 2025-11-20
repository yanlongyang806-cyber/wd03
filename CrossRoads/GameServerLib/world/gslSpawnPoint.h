/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

typedef struct Entity Entity;
typedef struct WorldScope WorldScope;
typedef struct WorldSpawnPoint WorldSpawnPoint;
typedef struct WorldVariable WorldVariable;
typedef struct WorldVolume WorldVolume;
typedef struct ZoneMap ZoneMap;
typedef struct DoorTransitionSequenceDef DoorTransitionSequenceDef;

typedef struct SpawnPointPartitionState 
{
	int iPartitionIdx;
	WorldVolume *pVolume;
} SpawnPointPartitionState;

typedef struct GameSpawnPoint
{
	// The spawn point's map-level name
	const char *pcName;

	// The world spawn point
	WorldSpawnPoint *pWorldPoint;

	// Player IDs for non-persisted spawn point unlocks
	int *eaiPlayerUnlocks;

	SpawnPointPartitionState **eaPartitionStates;
} GameSpawnPoint;

AUTO_STRUCT;
typedef struct SpawnpointMapMoveData
{
	EntityRef erEnt;
	const char* pchMapName;
	const char* pchSpawnpoint;
	const char* pchQueueName;
	GlobalType eOwnerType;
	ContainerID uOwnerID;
	ContainerID uMapID;
	U32 uPartitionID;
	WorldVariable** eaVars;
	const WorldScope* pScope; AST(UNOWNED)
	REF_TO(DoorTransitionSequenceDef) hTransOverride; AST(REFDICT(DoorTransitionSequenceDef))
	U32 eFlags; //Transfer flags
	bool bIncludeTeammates;
} SpawnpointMapMoveData;

AUTO_STRUCT;
typedef struct PlayerSpawnInfluence
{
	Vec3 vPos;
	int iSpawnIndex; AST(DEFAULT(-1))
	F32 fDistanceSqrFromSpawnIndex;
	F32 fDistanceSqr;
} PlayerSpawnInfluence;

AUTO_STRUCT;
typedef struct PlayerSpawnWeightedVote
{
	int iSpawnIndex; AST(KEY)
	F32 fWeight;
} PlayerSpawnWeightedVote;

// The unlock radius for activatable spawn points
#define SPAWN_POINT_DEFAULT_UNLOCK_RADIUS 150

// Special spawn point, indicates use the player spawn
#define START_SPAWN "StartSpawn"

// Gets a spawn point, if one exists
SA_RET_OP_VALID GameSpawnPoint *spawnpoint_GetByName(SA_PARAM_NN_STR const char *pcSpawnPointName, SA_PARAM_OP_VALID const WorldScope *pScope);

// Gets a spawn point, if one exists, including selecting a random spawn point if the name refers to a logical group
SA_RET_OP_VALID GameSpawnPoint *spawnpoint_GetByNameForSpawning(SA_PARAM_NN_STR const char *pcSpawnPointName, SA_PARAM_OP_VALID const WorldScope *pScope);

// Translates over to GameSpawnPoint from a WorldSpawnPoint
SA_RET_OP_VALID GameSpawnPoint *spawnpoint_GetByEntry(SA_PARAM_NN_VALID WorldSpawnPoint *pSpawnPoint);

GameSpawnPoint* spawnpoint_GetPlayerStartSpawn();

// Move a player on the same map to a spawn point
void spawnpoint_MovePlayerToSpawnPointNearTeam(Entity *pPlayerEnt, bool bActivatedSpawnsOnly, bool bMovePets);
void spawnpoint_MovePlayerToNamedSpawn(Entity *pPlayerEnt, const char *pchNamedSpawnPoint, const WorldScope *pScope, bool bDefaultToStartSpawn);
void spawnpoint_MovePlayerToNearestSpawn(Entity *pPlayerEnt, bool bActivatedSpawnsOnly, bool bMovePets);
void spawnpoint_MovePlayerToStartSpawn(Entity *pPlayerEnt, bool bMovePets);

void spawnpoint_MovePlayerToLocation( Entity *pPlayerEnt, Vec3 vecSpawnLoc, Quat quatSpawnRot, DoorTransitionSequenceDef *pTransDef, bool bMovePets);

// Move a player to a new map and spawn point
void spawnpoint_MovePlayerToMapAndSpawn( Entity *pPlayerEnt, const char *pcMapName, const char *pcNamedSpawnPoint, const char *pchQueueName, GlobalType eOwnerType, ContainerID uOwnerID, ContainerID uMapID, U32 uPartitionID, WorldVariable **eaVariables, const WorldScope *pScope, DoorTransitionSequenceDef* pTransOverride, U32 eFlags, bool bIncludeTeammates );

// Same as the above but initiates through a client confirm
void spawnpoint_RequestMoveConfirm(Entity *pEnt, const char *pcMapName, const char *pcNamedSpawnPoint, 
										const char *pcQueueName, GlobalType eOwnerType, ContainerID uOwnerID, 
										ContainerID uMapID, WorldVariable **eaVariables, const WorldScope *pScope, 
										DoorTransitionSequenceDef* pTransOverride,
										U32 eFlags, bool bIncludeTeammates);

//Gets the map move data for use in the ZoneMapInfo Request for namespaced maps.
SpawnpointMapMoveData* spawnpoint_GetMapMoveData(	Entity *pEnt, const char *pchMapName, const char *pchSpawnpoint, const char *pchQueueName, GlobalType eOwnerType, ContainerID uOwnerID, ContainerID uMapID, U32 uPartitionID, WorldVariable **eaVariables, const WorldScope *pScope, DoorTransitionSequenceDef* pTransOverride, U32 eFlags, bool bIncludeTeammates);

// Check if a spawn point exists
bool spawnpoint_SpawnPointExists(const char *pcName, const WorldScope *pScope);

// Get position.  Returns true if it exists
bool spawnpoint_GetSpawnPosition(const char *pcName, const WorldScope *pScope, Vec3 out_vPosition);

// Report all spawn point positions to the beacon system
void spawnpoint_GatherBeaconPositions(void);

// Called on map/partition load and unload
void spawnpoint_PartitionLoad(int iPartitionIdx);
void spawnpoint_PartitionUnload(int iPartitionIdx);
void spawnpoint_MapLoad(ZoneMap *pZoneMap);
void spawnpoint_MapUnload(void);
void spawnpoint_MapValidate(ZoneMap *pZoneMap);
