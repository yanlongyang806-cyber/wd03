#pragma once

#include "stdtypes.h"

typedef struct AITeam AITeam;
typedef struct GameEncounter GameEncounter;
typedef struct GameInteractable GameInteractable;
typedef struct Entity Entity;
typedef U32 EntityRef;
typedef struct Room Room;
typedef struct RoomPortal RoomPortal;
typedef struct ExprContext ExprContext;
typedef struct AIMastermindDef AIMastermindDef;
typedef struct AIMMHeatDef AIMMHeatDef;

// -----------------------------------------------
typedef struct AIMMRoom
{
	// the encounters in the room that are not for the MM
	GameEncounter	**eaStaticEncounters;
	// encounters in the room that are for the MM
	GameEncounter	**eaMMEncounters;

	// the players in the room
	Entity **eaPlayersInRoom; //  change to refs

	S32		roomDepth;

	// used for graph walking when finding somewhere to spawn
	U32		roomWalkFlag;

	U32		bPlayerHasVisited : 1;
	U32		bPlayerHasLeft : 1;

	// room is safe until the player has left it
	U32		bIsSafeRoom : 1;

} AIMMRoom;

// -----------------------------------------------
typedef struct AIMMRoomConn
{
	GameInteractable *pDoor;


} AIMMRoomConn;


// -----------------------------------------------
typedef struct MMHeatTeamStats
{
	Vec3	vPlayerBarycenter;

	F32		fTeamHealthPercent;

	// the average health percent for a member on the team
	F32		fAvgHealthPercent;

	U32		numDead;
	U32		numInCombat;

	// Timers:
	S64		timeOfLastDeath;
	S64		timeAtLowHP;
	S64		timeAtLastNewRoom;
	S64		timeAtLastCombat;
} MMHeatTeamStats;

// -----------------------------------------------
typedef struct MMHeatPlayer
{
	Vec3		vLastPlayerPos;
	Vec3		vIdlePlayerPos;

	EntityRef	erPlayer;

	F32			fLastHealth;

	// the last time the player past the low health threshold
	S64			timeAtLowHP;

	// the time the player last changed a room (just tracking room exits)
	S64			timeAtLastRoomChange;
	S64			timeAtLastRespawn;

	U32			playerIsDead : 1;
} MMHeatPlayer;

// -----------------------------------------------
typedef struct MMNearbyCritterInfo
{
	S32		numStaticCritters;
	S32		numMMSpawnedCritters;

	S32		numAmbient;

	// number of critters that are in or seeking combat with the player
	S32		numForCombat;
	// the estimated difficulty value for those critters 
	F32		difficultyInCombat;

} MMNearbyCritterInfo;

// -----------------------------------------------
typedef struct MMCombatCritterInfo
{
	S32		numEntities;
	F32		difficulty;
} MMCombatCritterInfo;

// -----------------------------------------------
typedef struct MMSentEncounter
{
	GameEncounter *pEncounter;
	int iPartitionIdx;

	// not to be referenced, 
	// this is just for validation that we're looking at the right spawned team
	AITeam *pTeam;

	// the player the encounter is sent at
	EntityRef erPlayer;

	Vec3 vSpawnPosition;

	U32 staticEncounter : 1;
	U32 leashing : 1;
	U32 pathFailed : 1;
} MMSentEncounter;



// -----------------------------------------------
typedef struct AIMMHeatManager
{
	MMNearbyCritterInfo cachedCritterCountInfo;
	MMCombatCritterInfo cachedCombatCritterInfo;

	// the rooms we care about
	Room			**eaRooms;
	RoomPortal		**eaRoomConns;

	// the rooms that have players in them
	Room			**eaPlayerOccupiedRooms;

	MMHeatPlayer	**eaPlayerHeatInfo;
	MMHeatTeamStats	teamStats;


	// encounters that currently have active entities that we've sent at the player
	MMSentEncounter **eaSentEncounters;
	GameEncounter	*forcedEncounter;	

	GameInteractable **eaCachedGates;

	// used for traversing the room graph
	U32			currentRoomWalkFlag;

	//  heat info
	F32			fHeatLevel;
	S32			iHeatTierSpawnOrderIndex;
	F32			fHeatLevelMin;
	F32			fHeatLevelMax;

	//
	F32			fSpawnDelayTime;
	F32			fOverrideSpawnTime;

	F32			fLastSpawnTimeLeft;

	// timers
	S64			timeLastHeatUpdate;
	S64			timeLastSpawned;
	S64			timeSpawnPeriod;
	S64			timeLastUpdatedCritterCountInfo;
	S64			timeLastUpdatedCombatCritterInfo;
	S64			timeLastWipe;

	U32			isHeatEnabled : 1;
	U32			isEnabled : 1;
} AIMMHeatManager;


void aiMastermindHeat_FirstTickInit();
void aiMastermindHeat_Initialize();
void aiMastermindHeat_Shutdown();
void aiMastermindHeat_Update(AIMastermindDef *pDef);
void aiMastermindHeat_OnMapLoad();
void aiMastermindHeat_OnMapUnload();
void aiMastermindHeatDef_Validate(AIMMHeatDef *pHeatDef, const char *pchFilename);
