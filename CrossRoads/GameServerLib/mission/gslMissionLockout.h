/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

typedef struct Entity Entity;
typedef struct MissionDef MissionDef;


// A MissionLockoutList is used to control Missions that have a MissionLockoutType
typedef struct MissionLockoutList
{
	int iPartitionIdx;

	const char *pcMissionRefString;		// Pooled string

	ContainerID *eaiPlayersWithCredit;

	EntityRef *eaiEscorting;			// entities that are being escorted

	bool bInProgress;
} MissionLockoutList;


// Gets the currently active LockoutList for a MissionDef
MissionLockoutList* missionlockout_GetLockoutList(int iPartitionIdx, MissionDef *pDef);

// Destroys the lockout list for a given MissionDef
void missionlockout_DestroyLockoutList(int iPartitionIdx, MissionDef *pDef);

// Destroys all Mission Lockout Lists on a partition, resetting the state of all Mission Lockout
void missionlockout_ResetPartition(int iPartitionIdx);

// Destroys all Mission Lockout Lists, resetting the state of all Mission Lockout
void missionlockout_ResetAllPartitions(void);

// Returns TRUE if this Lockout Mission is already in progress
bool missionlockout_MissionLockoutInProgress(int iPartitionIdx, MissionDef *pDef);

// Returns TRUE if this player is on the lockout list for the given MissionDef
bool missionlockout_PlayerInLockoutList(MissionDef *pDef, Entity *pEnt, int iPartitionIdx);

// Update the player entities with the escort list
void missionlockout_UpdatePlayerEscorting(MissionLockoutList *pLockoutList);

// Adds the specified player to the lockout list for the Mission
void missionlockout_AddPlayerToLockoutList(MissionDef *pDef, Entity *pEnt);

// Locks the Lockout List for the specified Mission
void missionlockout_BeginLockout(int iPartitionIdx, MissionDef *pDef);

// Gets a list of all players on the LockoutList
void missionlockout_GetLockoutListEnts(int iPartitionIdx, MissionDef *pDef, Entity*** entsOut);

// Sets the list of players on the LockoutList
void missionlockout_SetLockoutListEnts(int iPartitionIdx, MissionDef *pDef, Entity*** entsIn);

