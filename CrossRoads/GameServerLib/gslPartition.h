/***************************************************************************
*     Copyright (c) 2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once
GCC_SYSTEM

#include "GlobalTypeEnum.h"
#include "MapDescription.h"
#include "partition_enums.h"
#include "ReferenceSystem.h"

typedef struct Entity Entity;
typedef struct Team Team;

AUTO_ENUM;
typedef enum PartitionFlags
{
	PARTITION_TESTFORIMMEDIATEDEATH = 1 << 0,
} PartitionFlags;

// Data that describes a partition of a map.  Some fields map directly to the MapDescription structure.
AUTO_STRUCT;
typedef struct MapPartition
{
	int iPartitionIdx; AST(KEY)
	// index into array of partitions on this gs.exe

	MapPartitionSummary summary;
	MapDescription description;

	EntityRef erTransport;
		// Transport Entity for sending global fx and such, sent to everyone in the partition

	F32 fInactiveTime;

	REF_TO(Entity) hOwnerPlayer;
	REF_TO(Team) hOwnerTeam;

	PartitionFlags eFlags;

	bool bIsDestroyed;       // This is true after destroying entities during the destroy process
	bool bIsBeingDestroyed;  // This is true at start of destroy when destroying entities

} MapPartition;


//actually returns max IDX + 1... so if partitions 0 and 4 exist, will return 5, so you can allocate
//an array of size 5 and have valid slots of all IDXs that might exist
int partition_GetCurNumPartitionsCeiling(void);

int partition_GetActualActivePartitionCount(void);

MapPartition* partition_Create(MapPartitionSummary *pSummary, char *pReason);
MapPartition* partition_CreateAndInit(MapPartitionSummary *pSummary, FORMAT_STR const char *pReasonFmt, ...);
void partition_ReInitAllActive(char *pReason);
MapPartition* partition_FromIdx(int iPartitionIdx);
void partition_DestroyByIdx(int iPartitionIdx, char *pReason);

void partition_InitMakeBinsAndExit(void);

// Returns true if a specific MapPartition exists
bool partition_ExistsByIdx(int iPartitionIdx);
bool partition_ExistsByID(U32 uPartitionID);
bool partition_IsDestroyed(int iPartitionIdx);
bool partition_IsBeingDestroyed(int iPartitionIdx);

// Convert to/from Index
int partition_IdxFromID(U32 uPartitionID);
U32 partition_IDFromIdx(int iPartitionIdx);

// Returns the specific MapPartition's mapVariables, which is a pooled string
SA_RET_OP_VALID const char* partition_MapVariablesFromIdx(int iPartitionIdx);

// Returns the specific MapPartition's ownerType
GlobalType partition_OwnerTypeFromID(U32 uPartitionID);
GlobalType partition_OwnerTypeFromIdx(int iPartitionIdx);

// Returns the specific MapPartition's ownerID
ContainerID partition_OwnerIDFromID(U32 uPartitionID);
ContainerID partition_OwnerIDFromIdx(int iPartitionIdx);

// If the owner is a player, returns that entity, otherwise NULL
Entity* partition_GetPlayerMapOwner(int iPartitionIdx);
Team* partition_GetTeamMapOwner(int iPartitionIdx);

void partition_SetOwnerTypeAndIDFromIdx(int iPartitionIdx, GlobalType eOwnerType, ContainerID iOwnerID);

// Player count
int partition_GetPlayerCount(int iPartitionIdx);
int partition_GetTotalPlayerCount(void);
bool partition_HasPlayersOnAnyPartition(void);

// Inactivity
F32 partition_GetInactivity(int iPartitionIdx);
void partition_IncInactivity(int iPartitionIdx, F32 fTime);
void partition_ClearInactivity(int iPartitionIdx);

// Returns the specific MapPartition's erTransport
EntityRef partition_erTransportFromIdx(int iPartitionIdx);

// Sets the specific MapPartition's erTransport
void partition_erTransportSetFromIdx(int iPartitionIdx, EntityRef erTransport);

// Fills the EArray with all the MapPartition's transport Entities
void partition_FillTransportEnts(Entity ***pppEntities);

MapPartition* partition_FromEnt(Entity *pEnt);
MapDescription *partition_GetMapDescription(int iPartitionIdx);

//note that this happily overwrites any previous ID that might be there, which is needed
//for a case where a transfer aborts and then restarts
void partition_AddUpcomingTransferToPartitionID(U32 iPartitionID, int iContainerID);

//return -1 if either the container ID isn't registered, or the partition no longer exists
int  partition_PopUpcomingTransferPartitionIdx(ContainerID iContainerID);
int partition_GetUpcomingTransferToPartitionIdx(ContainerID iContainerID);

//return 0 if either the container ID isn't registered, or the partition no longer exists
U32 partition_GetUpcomingTransferToPartitionID(ContainerID iContainerID);

//returns the stored ID even if it no longer exists, for better error reporting
U32 partition_GetUpcomingTransferToPartitionID_Raw(ContainerID iContainerID);

void partition_ResetPlayerCounts(void);
void partition_IncPlayerCount(int iPartitionIdx);

typedef void (*PartitionCallbackFunc)(int iPartitionIdx);
typedef void (*PartitionCallbackDataFunc)(int iPartitionIdx, void *pUserData);
void partition_ExecuteOnEachPartition(PartitionCallbackFunc func);
void partition_ExecuteOnEachPartitionWithData(PartitionCallbackDataFunc func, void *pUserData);

//takes all the "official" internal MapPartitionSummaries and sticks pointers to them into the given earray
void partition_GetUnownedPartitionSummaryEArray(MapPartitionSummary ***pppOutArray);

//returns the public instance index (not at all related to partitionIdx)
int partition_PublicInstanceIndexFromIdx(int iPartitionIdx);
static __forceinline int partition_PublicInstanceIndexFromID(U32 uPartitionID)
{
	int iIdx = partition_IdxFromID(uPartitionID);
	if (iIdx == -1)
	{
		return 0;
	}
	return partition_PublicInstanceIndexFromIdx(iIdx);
}
void partition_GetPublicIndicesEstring(char **ppOutString); //will fill the string with "1,3,8" or something like that

int partition_GetNumPartitionsSinceServerStart(void);
int partition_GetMaxPartitionsSinceServerStart(void);

bool partition_TestForImmediateDeath(int iPartitionIdx);
void partition_SetTestForImmediateDeath(int iPartitionIdx, bool bSet);

AUTO_ENUM;
typedef enum
{
	PARTITION_CREATED, 
	PARTITION_DESTROY_BEGAN,
	PARTITION_DESTROYED, 
	PARTITION_OWNER_SET,
	PARTITION_INITTED,


	//-----------------HIGH PRIORITY ABOVE THIS, LOW PRIORITY BELOW------------------
	PARTITION_LAST_HIGH_PRIORITY,
	//-----------------HIGH PRIORITY ABOVE THIS, LOW PRIORITY BELOW------------------




	PARTITION_GROUPED_LOW_PRIORITY, //too many low priority things happened at once, collapsing them into one of these

	PARTITION_TIMEOUT_REQUESTED, 
	PARTITION_PLAYER_PRE_SEND,
	PARTITION_PLAYER_ENTERED,
	PARTITION_PLAYER_LEFT,
	PARTITION_COMMENT,
	
} enumPartitionLogType;

void partition_DebugLogInternal(enumPartitionLogType eLogType, int iPartitionIdx, FORMAT_STR const char *pFmt, ...);

void partition_DumpPartitionLogs(int iPartitionIdx);