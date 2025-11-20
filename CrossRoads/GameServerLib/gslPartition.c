/***************************************************************************
*     Copyright (c) 2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "entCritter.h"
#include "Entity.h"
#include "EntityIterator.h"
#include "EntityLib.h"
#include "GameServerLib.h"
#include "GlobalTypes.h"
#include "gslBaseStates.h"
#include "gslEncounter.h"
#include "gslEntity.h"
#include "gslMapVariable.h"
#include "gslMechanics.h"
#include "gslPartition.h"
#include "gslQueue.h"
#include "gslTransactions.h"
#include "ServerLib.h"
#include "StringCache.h"
#include "structDefines.h"
#include "TimedCallback.h"
#include "wlVolumes.h"
#include "WorldGrid.h"
#include "StaticWorld/WorldGridPrivate.h"
#include "WorldVariable.h"

#include "AutoGen/AppServerLib_autogen_remotefuncs.h"
#include "gslPartition_h_ast.h"
#include "MapDescription_h_ast.h"
#include "WorldVariable_h_ast.h"
#include "gslPartition_c_ast.h"


// ----------------------------------------------------------------------------------
// Prototypes and Static Variables
// ----------------------------------------------------------------------------------

#define GSL_FOR_EACH_PARTITION(varName) { MapPartition *varName; int _i; for (_i=0; _i < eaSize(&s_ppPartitions); _i++) if ((varName = s_ppPartitions[_i])) {
#define GSL_FOR_EACH_PARTITION_END }}

#define BASE_PARTITION_INDEX	1

#define PARTITION_DESTROY_MAX_ENTS_TO_LOG 10

// Prototypes from "mapchange_common.c"
void game_PartitionCreate(MapPartition *pPartn, bool bInitMapVars);
void game_PartitionDestroy(MapPartition *pPartn);
void game_PartitionDestroyLate(MapPartition *pPartn);


// EArray of existing partitions
static MapPartition **s_ppPartitions = NULL;
static StashTable s_partitionIDFromTransferringEntContainerID = 0;

static bool s_bHasCreatedFirstPartition = false;

static int s_iNumPartitionsSinceServerStart = 0;
static int s_iMaxPartitionsSinceServerStart = 0;


// ----------------------------------------------------------------------------------
// Partition Access Functions
// ----------------------------------------------------------------------------------

// Returns true if a MapPartition with the given id exists
bool partition_ExistsByID(U32 uPartitionID)
{
	return (partition_IdxFromID(uPartitionID) >= 0);
}


bool partition_ExistsByIdx(int iPartitionIdx)
{
	return eaGet(&s_ppPartitions,iPartitionIdx) != NULL;
}


bool partition_IsDestroyed(int iPartitionIdx)
{
	MapPartition *pPart = eaGet(&s_ppPartitions,iPartitionIdx);
	if (pPart) {
		return pPart->bIsDestroyed;
	}
	return true;
}


bool partition_IsBeingDestroyed(int iPartitionIdx)
{
	MapPartition *pPart = eaGet(&s_ppPartitions,iPartitionIdx);
	if (pPart) {
		return pPart->bIsBeingDestroyed;
	}
	return true;
}


// Returns the index of the MapPartition with the ID
int partition_IdxFromID(U32 uPartitionID)
{
	int i;
	for(i=eaSize(&s_ppPartitions)-1; i>=0; i--) {
		if (s_ppPartitions[i] && s_ppPartitions[i]->summary.uPartitionID == uPartitionID) {
			return i;
		}
	}
	return -1;
}

int partition_PublicInstanceIndexFromIdx(int iPartitionIdx)
{
	MapPartition *pPartition;

	pPartition = partition_FromIdx(iPartitionIdx);
	if (pPartition)
	{
		return pPartition->summary.iPublicIndex;
	}
	return 0;
}

void partition_GetPublicIndicesEstring(char **ppOutString)
{
	int i;

	estrClear(ppOutString);

	for(i=eaSize(&s_ppPartitions)-1; i>=0; i--) 
	{
		if (s_ppPartitions[i] && s_ppPartitions[i]->summary.iPublicIndex)
		{
			estrConcatf(ppOutString, "%s%d", estrLength(ppOutString) ? "," : "", s_ppPartitions[i]->summary.iPublicIndex);
		}
	}
}


int partition_GetCurNumPartitionsCeiling(void)
{
	return eaSize(&s_ppPartitions);
}

int partition_GetActualActivePartitionCount(void)
{
	int iRetVal = 0;
	int i;

	for (i = eaSize(&s_ppPartitions) - 1; i >= 0; i--)
	{
		if (s_ppPartitions[i])
		{
			iRetVal++;
		}
	}

	return iRetVal;
}

bool partition_HasPlayersOnAnyPartition(void)
{
	int i;

	for (i = eaSize(&s_ppPartitions) - 1; i >= 0; i--)
	{
		if (s_ppPartitions[i] && s_ppPartitions[i]->summary.iNumPlayers)
		{
			return true;
		}
	}

	return false;
}

int partition_GetTotalPlayerCount(void)
{
	int i, count = 0;

	for (i = eaSize(&s_ppPartitions) - 1; i >= 0; i--)
	{
		if (s_ppPartitions[i]) 
		{
			count += s_ppPartitions[i]->summary.iNumPlayers;
		}
	}

	return count;
}

MapPartition* partition_FromIdx(int iPartitionIdx)
{
	MapPartition *pPart = eaGet(&s_ppPartitions, iPartitionIdx);
	assertmsgf(pPart, "Partition %d does not exist", iPartitionIdx);
	return pPart;
}


// Returns the ID of the MapPartition using the index
U32 partition_IDFromIdx(int iPartitionIdx)
{
	MapPartition *pPart = partition_FromIdx(iPartitionIdx);
	return pPart->summary.uPartitionID;
}


MapPartition* partition_FromEnt(Entity *pEnt)
{
	int iPartitionIdx = entGetPartitionIdx(pEnt);
	return partition_FromIdx(iPartitionIdx);
}


MapDescription* partition_GetMapDescription(int iPartitionIdx)
{
	MapPartition *pPart = partition_FromIdx(iPartitionIdx);
	return &pPart->description;
}

void partition_ExecuteOnEachPartition(PartitionCallbackFunc func)
{
	// Call the function on each non-null partition
	int i;
	for(i=0; i<eaSize(&s_ppPartitions); ++i) {
		if (s_ppPartitions[i] && !s_ppPartitions[i]->bIsDestroyed) {
			(*func)(i);
		}
	}
}


void partition_ExecuteOnEachPartitionWithData(PartitionCallbackDataFunc func, void *pUserData)
{
	// Call the function on each non-null partition
	int i;
	for(i=0; i<eaSize(&s_ppPartitions); ++i) {
		if (s_ppPartitions[i] && !s_ppPartitions[i]->bIsDestroyed) {
			(*func)(i, pUserData);
		}
	}
}


int partition_GetNumPartitionsSinceServerStart(void)
{
	return s_iNumPartitionsSinceServerStart;
}


int partition_GetMaxPartitionsSinceServerStart(void)
{
	return s_iMaxPartitionsSinceServerStart;
}


// ----------------------------------------------------------------------------------
// Partition Summary Data Access Functions
// ----------------------------------------------------------------------------------

// Returns the specific MapPartition's mapVariables, which is a pooled string
// This should be called 'raw' or 'src' to signify that it is just
// the raw data to apply to a map when loaded
const char* partition_MapVariablesFromIdx(int iPartitionIdx)
{
	MapPartition *pPart = eaGet(&s_ppPartitions, iPartitionIdx);
	if (!pPart) {
		assertmsgf(0, "Partition %d does not exist", iPartitionIdx);
	}
	return pPart->summary.pMapVariables;
}


// Returns the specific MapPartition's ownerType.
GlobalType partition_OwnerTypeFromID(U32 uPartitionID)
{
	int i = partition_IdxFromID(uPartitionID);
	if (i>=0) {
		return s_ppPartitions[i]->summary.eOwnerType;
	} else {
		assertmsgf(0, "Partition ID %d does not exist", uPartitionID);
	}
}


// Returns the specific MapPartition's ownerType.
GlobalType partition_OwnerTypeFromIdx(int iPartitionIdx)
{
	if (partition_ExistsByIdx(iPartitionIdx)) {
		return s_ppPartitions[iPartitionIdx]->summary.eOwnerType;
	} else {
		assertmsgf(0, "Partition %d does not exist", iPartitionIdx);
	}
}


// Returns the specific MapPartition's ownerID
ContainerID partition_OwnerIDFromID(U32 uPartitionID)
{
	S32 i = partition_IdxFromID(uPartitionID);
	if (i>=0) {
		return s_ppPartitions[i]->summary.iOwnerID;
	} else {
		assertmsgf(0, "Partition ID %d does not exist", uPartitionID);
	}
}


// Returns the specific MapPartition's ownerID
ContainerID partition_OwnerIDFromIdx(int iPartitionIdx)
{
	if (partition_ExistsByIdx(iPartitionIdx)) {
		return s_ppPartitions[iPartitionIdx]->summary.iOwnerID;
	} else {
		assertmsgf(0, "Partition %d does not exist", iPartitionIdx);
	}
}

void partition_SetOwnerTypeAndIDFromIdx(int iPartitionIdx, GlobalType eOwnerType, ContainerID iOwnerID)
{
	if (partition_ExistsByIdx(iPartitionIdx)) 
	{
		if (s_ppPartitions[iPartitionIdx]->summary.eOwnerType != eOwnerType || s_ppPartitions[iPartitionIdx]->summary.iOwnerID != iOwnerID)
		{
			partition_DebugLogInternal(PARTITION_OWNER_SET, iPartitionIdx, "Now owned by %s", GlobalTypeAndIDToString(eOwnerType, iOwnerID));

			s_ppPartitions[iPartitionIdx]->summary.eOwnerType = eOwnerType;
			s_ppPartitions[iPartitionIdx]->summary.iOwnerID = iOwnerID;
		}

		RemoteCommand_PartitionOwnerChanged(GLOBALTYPE_MAPMANAGER, 0, GetAppGlobalID(), s_ppPartitions[iPartitionIdx]->summary.uPartitionID, eOwnerType, iOwnerID);
	}
}


void partition_GetUnownedPartitionSummaryEArray(MapPartitionSummary ***pppOutArray)
{
	GSL_FOR_EACH_PARTITION(pPart)
		eaPush(pppOutArray, &pPart->summary);
	GSL_FOR_EACH_PARTITION_END
}

Entity* partition_GetPlayerMapOwner(int iPartitionIdx)
{
	if (partition_ExistsByIdx(iPartitionIdx)) 
	{
		MapPartition *pPart = s_ppPartitions[iPartitionIdx];
		if (pPart->summary.eOwnerType == GLOBALTYPE_ENTITYPLAYER)
		{
			char idBuf[128];
			Entity *pEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pPart->summary.iOwnerID);
			if (pEnt) {
				return pEnt;
			}
			if (GET_REF(pPart->hOwnerPlayer)) {
				return GET_REF(pPart->hOwnerPlayer);
			}
			SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYPLAYER), ContainerIDToString(pPart->summary.iOwnerID, idBuf), pPart->hOwnerPlayer);
		}
	}

	return NULL;
}


Team* partition_GetTeamMapOwner(int iPartitionIdx)
{
	if (partition_ExistsByIdx(iPartitionIdx)) 
	{
		MapPartition *pPart = s_ppPartitions[iPartitionIdx];
		if (pPart->summary.eOwnerType == GLOBALTYPE_TEAM)
		{
			char idBuf[128];

			if (GET_REF(pPart->hOwnerTeam)) {
				return GET_REF(pPart->hOwnerTeam);
			}
			SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_TEAM), ContainerIDToString(pPart->summary.iOwnerID, idBuf), pPart->hOwnerTeam);
		}
	}

	return NULL;
}

// ----------------------------------------------------------------------------------
// Partition Player Tracking
// ----------------------------------------------------------------------------------

// save the partition for an entity that will be transferred
void partition_AddUpcomingTransferToPartitionID(U32 iPartitionID, int iContainerID)
{
	int iIdx = partition_IdxFromID(iPartitionID);

	if (iIdx == -1)
	{
		devassertmsg(0, "partition_AddUpcomingTransferIDToPartition called with invalid Partition ID");
		return;
	}


	if (!s_partitionIDFromTransferringEntContainerID) {
		s_partitionIDFromTransferringEntContainerID = stashTableCreateInt(0);
	}

	devassert(stashIntAddInt(s_partitionIDFromTransferringEntContainerID, iContainerID, iPartitionID, true));
	partition_DebugLogInternal(PARTITION_PLAYER_PRE_SEND, iIdx, "Player %u will be sent here", iContainerID);
}

bool partition_UseDefaultPartition(void)
{
	if (GetAppGlobalType() == GLOBALTYPE_WEBREQUESTSERVER || GetAppGlobalType() == GLOBALTYPE_GATEWAYSERVER)
	{
		return true;
	}

	return false;
}

// get the iPartitionIdx for a transferred entity and remove it from the pending list
int partition_PopUpcomingTransferPartitionIdx(ContainerID iContainerID)
{
	U32 iPartitionID = 0;

	if (s_partitionIDFromTransferringEntContainerID && 
		stashIntRemoveInt(s_partitionIDFromTransferringEntContainerID, iContainerID, &iPartitionID))
	{
		return partition_IdxFromID(iPartitionID);
	}
	else 
	{
		if (partition_UseDefaultPartition())
		{
			MapPartitionSummary emptySummary = {0};
			emptySummary.uPartitionID = 1;
			HereIsPartitionInfoForUpcomingMapTransfer(0, iContainerID, 1, &emptySummary);
		
			if (s_partitionIDFromTransferringEntContainerID) 
			{
				stashIntRemoveInt(s_partitionIDFromTransferringEntContainerID, iContainerID, NULL);
			}
		}
	}

	return -1;
}

int partition_GetUpcomingTransferToPartitionIdx(ContainerID iContainerID)
{
	U32 iPartitionID = 0;

	if (!stashIntFindInt(s_partitionIDFromTransferringEntContainerID, iContainerID, &iPartitionID))
	{
		return -1;
	}

	return partition_IdxFromID(iPartitionID);
}

U32 partition_GetUpcomingTransferToPartitionID(ContainerID iContainerID)
{
	U32 iPartitionID = 0;

	if (!stashIntFindInt(s_partitionIDFromTransferringEntContainerID, iContainerID, &iPartitionID))
	{
		return 0;
	}

	if (partition_ExistsByID(iPartitionID))
	{
		return iPartitionID;
	}

	return 0;
}

U32 partition_GetUpcomingTransferToPartitionID_Raw(ContainerID iContainerID)
{
	U32 iPartitionID = 0;

	if (!stashIntFindInt(s_partitionIDFromTransferringEntContainerID, iContainerID, &iPartitionID))
	{
		return 0;
	}

	return iPartitionID;
}





void partition_ResetPlayerCounts(void)
{
	GSL_FOR_EACH_PARTITION(pPart)
		pPart->summary.iNumPlayers = 0;
	GSL_FOR_EACH_PARTITION_END
}


void partition_IncPlayerCount(int iPartitionIdx)
{
	if (iPartitionIdx != PARTITION_ENT_BEING_DESTROYED) {
		MapPartition *pPart = partition_FromIdx(iPartitionIdx);
		pPart->summary.iNumPlayers++;
	}
}


int partition_GetPlayerCount(int iPartitionIdx)
{
	MapPartition *pPart = partition_FromIdx(iPartitionIdx);
	return pPart->summary.iNumPlayers;
}


// Inactivity
F32 partition_GetInactivity(int iPartitionIdx)
{
	MapPartition *pPart = partition_FromIdx(iPartitionIdx);
	return pPart->fInactiveTime;
}


void partition_IncInactivity(int iPartitionIdx, F32 fTime)
{
	MapPartition *pPart = partition_FromIdx(iPartitionIdx);
	pPart->fInactiveTime += fTime;
}


void partition_ClearInactivity(int iPartitionIdx)
{
	MapPartition *pPart = partition_FromIdx(iPartitionIdx);
	pPart->fInactiveTime = 0;
}


// ----------------------------------------------------------------------------------
// Partition Transport Ent Tracking
// ----------------------------------------------------------------------------------

// Returns the specific MapPartition's erTransport
EntityRef partition_erTransportFromIdx(int iPartitionIdx)
{
	if (partition_ExistsByIdx(iPartitionIdx)) {
		return s_ppPartitions[iPartitionIdx]->erTransport;
	} else {
		assertmsgf(0, "Partition %d does not exist", iPartitionIdx);
	}
	return 0;
}


// Sets the specific MapPartition's erTransport
void partition_erTransportSetFromIdx(int iPartitionIdx, EntityRef erTransport)
{
	if (partition_ExistsByIdx(iPartitionIdx)) {
		s_ppPartitions[iPartitionIdx]->erTransport = erTransport;
	} else {
		assertmsgf(0, "Partition %d does not exist", iPartitionIdx);
	}
}


// Fills the EArray with all the MapPartition's transport Entities
void partition_FillTransportEnts(Entity ***pppEntities)
{
	S32 i;
	for(i=eaSize(&s_ppPartitions)-1; i>=0; i--) {
		MapPartition *pPart = s_ppPartitions[i];
		if (pPart) {
			Entity *pEnt = entFromEntityRefAnyPartition(pPart->erTransport);
			if(pEnt &&	!(pEnt->myEntityFlags & ENTITYFLAG_DONOTSEND)) {
				eaPush(pppEntities, pEnt);
			}
		}
	}
}


// ----------------------------------------------------------------------------------
// Partition Lifecycle
// ----------------------------------------------------------------------------------

// Creates a MapPartition and adds it to the list, but does not run any of the "map load" code
MapPartition* partition_Create(MapPartitionSummary *pSummary, char *pReason)
{
	int i;
	MapPartition *pPart = NULL;
	int iCount;

	PERFINFO_AUTO_START_FUNC();

	// Stupid safety check
	if (partition_ExistsByID(pSummary->uPartitionID)) {
		devassertmsgf(1, "Trying to create a partition %d that already exists", pSummary->uPartitionID);
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	coarseTimerAddInstance(NULL, "partition_Create");

	// Create and fill in the MapPartition
	pPart = StructCreate(parse_MapPartition);
	StructCopy(parse_MapPartitionSummary, pSummary, &pPart->summary, 0, 0, 0);

	// Fill in the map description
	StructCopy(parse_MapDescription, &gGSLState.gameServerDescription.baseMapDescription, &pPart->description, 0, 0, 0);
	pPart->description.mapVariables = StructAllocString(pSummary->pMapVariables);
	pPart->description.ownerID = pSummary->iOwnerID;
	pPart->description.ownerType = pSummary->eOwnerType;

	// Add the partition (empty slot is fine, or add to end)
	for(i = BASE_PARTITION_INDEX; i < eaSize(&s_ppPartitions); ++i) {
		if (!s_ppPartitions[i]) {
			break;
		}
	}
	
	assertmsgf(i <= MAX_LEGAL_PARTITION_IDX, "partion idx exceeds max legal... max is %d", MAX_LEGAL_PARTITION_IDX);
	
	pPart->iPartitionIdx = i;
	eaSet(&s_ppPartitions,pPart,i);

	// Create the world collision data and open appropriate cells
	worldCreatePartition(pPart->iPartitionIdx, true);
	worldCheckForNeedToOpenCellsOnPartition(pPart->iPartitionIdx);

	partition_DebugLogInternal(PARTITION_CREATED, pPart->iPartitionIdx, "Created with ID %u, owner %s, map variables %s, v shard ID %u",
		pPart->summary.uPartitionID, GlobalTypeAndIDToString(pPart->summary.eOwnerType, pPart->summary.iOwnerID), pPart->description.mapVariables,
		pPart->summary.iVirtualShardID);

	// Don't start memory leak tracking until first partition is created
	if (!s_bHasCreatedFirstPartition) {
		s_bHasCreatedFirstPartition = true;
		gslStateBeginMemLeakTracking();
	}

	// Increment total number of partitions since server start
	++s_iNumPartitionsSinceServerStart;

	// Update max partitions number if needed
	iCount = partition_GetActualActivePartitionCount();
	if (iCount > s_iMaxPartitionsSinceServerStart) {
		s_iMaxPartitionsSinceServerStart = iCount;
	}

	coarseTimerStopInstance(NULL, "partition_Create");
	PERFINFO_AUTO_STOP();

	return pPart;
}


void partition_Init(MapPartition *pPart, bool bInitMapVars, char *pReason)
{
	if (!pPart) {
		return;
	}
	PERFINFO_AUTO_START_FUNC();

	partition_DebugLogInternal(PARTITION_INITTED, pPart->iPartitionIdx, "Initted because: %s", pReason);

	coarseTimerAddInstance(NULL, "partition_Init");
	game_PartitionCreate(pPart, bInitMapVars);
	coarseTimerStopInstance(NULL, "partition_Init");

	PERFINFO_AUTO_STOP();
}


void partition_ReInitAllActive(char *pReason)
{
	int i;
	char *pTempReason = NULL;
	estrStackCreate(&pTempReason);
	estrPrintf(&pTempReason, "ReInitAllActive because: %s", pReason);

	for(i=0; i<eaSize(&s_ppPartitions); ++i) {
		partition_Init(s_ppPartitions[i], false, pTempReason);
	}

	estrDestroy(&pTempReason);
}


MapPartition* partition_CreateAndInit(MapPartitionSummary *pSummary, const char *pReasonFmt, ...)
{
	char *pReason = NULL;
	MapPartition *pPart;

	PERFINFO_AUTO_START_FUNC();

	estrStackCreate(&pReason);
	estrGetVarArgs(&pReason, pReasonFmt);
	pPart = partition_Create(pSummary, pReason);
	partition_Init(pPart, true, pReason);
	estrDestroy(&pReason);

	PERFINFO_AUTO_STOP();
	return pPart;
}


// This is called on a timed callback after the rest of partition destroy is complete
void partition_DestroyLate(TimedCallback *pCallback, F32 fTimeSinceLastCallback, MapPartition *pPart)
{
	int iPartitionIdx = pPart->iPartitionIdx;
	EntityIterator *pIter;
	Entity *pEnt;
	Entity **ppEnts = NULL;
	int i, iNumExtraEntities= 0;

	partition_DebugLogInternal(PARTITION_DESTROYED, iPartitionIdx, "Destroying");

	coarseTimerAddInstance(NULL, "partition_DestroyLate");
	loadstart_printf("Finishing partition destroy %d...", iPartitionIdx);

	// Hard destroy any other ents still on the partition
	pIter = entGetIteratorAllTypes(iPartitionIdx, 0, 0);
	pPart->bIsDestroyed = false;
	while(pEnt = EntityIteratorGetNext(pIter)) {
		eaPush(&ppEnts, pEnt);
	}

	// Report if any entities somehow survived to the late destroy
	iNumExtraEntities = eaSize(&ppEnts);
	if (iNumExtraEntities) {
		int iLoggedEnts = 0;
		char* estrEntityDetails = NULL;
		estrStackCreate(&estrEntityDetails);

		for (i = 0; i < iNumExtraEntities; i++) {
			pEnt = ppEnts[i];
			if (iLoggedEnts < PARTITION_DESTROY_MAX_ENTS_TO_LOG) {
				// Log information about this entity
				estrConcatf(&estrEntityDetails, "Entity Type %s, Flags %d\n", 
					StaticDefineIntRevLookup(GlobalTypeEnum, pEnt->myEntityType),
					pEnt->myEntityFlags);
				iLoggedEnts++;
			}
			// Destroy the entity
			gslQueueEntityDestroy(pEnt);
			if (pEnt->pCritter && (entGetType(pEnt) == GLOBALTYPE_ENTITYCRITTER)) {
				gslDestroyEntity(pEnt);
			}
		}
		ErrorDetailsf("Partition=%d, numExtraEntities=%d\n%s", iPartitionIdx, iNumExtraEntities, estrEntityDetails);
		Errorf("Extra entities on partition destroy");
		estrDestroy(&estrEntityDetails);
	}

	pPart->bIsDestroyed = true;
	EntityIteratorRelease(pIter);
	eaDestroy(&ppEnts);

	// Finalize game data cleanup
	game_PartitionDestroyLate(pPart);

	// Everything went fine, remove and destroy the partition
	s_ppPartitions[pPart->iPartitionIdx] = NULL;
	StructDestroy(parse_MapPartition, pPart);

	// Destroy the world collision data
	worldCloseCellsOnPartition(iPartitionIdx);
	wlVolumeDestroyPartition(iPartitionIdx);
	worldDestroyPartition(iPartitionIdx);

	loadend_printf("done");
	coarseTimerStopInstance(NULL, "partition_DestroyLate");
}


void partition_DestroyByIdx(int iPartitionIdx, char *pReason)
{
	EntityIterator *pIter;
	MapPartition *pPart;
	MapState *pMapState;
	Entity *pEnt;
	
	pPart = eaGet(&s_ppPartitions, iPartitionIdx);
	if (!pPart) {
		devassertmsgf(pPart, "Partition does not exist");
		return;
	}

	partition_DebugLogInternal(PARTITION_DESTROY_BEGAN, iPartitionIdx, "Beginning destroy because: %s", pReason);

	coarseTimerAddInstance(NULL, "partition_DestroyByIdx");
	loadstart_printf("Stopping partition %d...", pPart->iPartitionIdx);

	// Kick any players on the partition
	pIter = entGetIteratorSingleType(iPartitionIdx, 0, 0, GLOBALTYPE_ENTITYPLAYER);
	while(pEnt = EntityIteratorGetNext(pIter)) {
		// Boot the player off the partition.  Note that normally a partition is only destroyed after all players are gone.
		gslForceLogOutEntity(pEnt, "Map Closed");

		// Mark entities in case code attempts to process them after partition is gone
		pEnt->iPartitionIdx_UseAccessor = PARTITION_ENT_BEING_DESTROYED; 
	}
	EntityIteratorRelease(pIter);

	pPart->bIsBeingDestroyed = true;

	// Clean up partition data
	game_PartitionDestroy(pPart);

	// Mark map state for partition as paused to avoid further processing on the partition
	pMapState = mapState_FromPartitionIdx(iPartitionIdx);
	if (pMapState) {
		pMapState->bPaused = true;
		pMapState->bBeingDestroyed = true;
	}

	// Kill any other ents still on the partition
	pIter = entGetIteratorAllTypes(iPartitionIdx, 0, 0);
	while(pEnt = EntityIteratorGetNext(pIter)) {
		// Destroy the remaining entities
		gslQueueEntityDestroy(pEnt);
		if (entGetType(pEnt) == GLOBALTYPE_ENTITYCRITTER || entIsProjectile(pEnt)) {
			if (pEnt->pCritter) {
				// Clear any linger time on entities, then destroy
				pEnt->pCritter->timeToLinger = -1;
				pEnt->pCritter->StartingTimeToLinger = -1;
			}

			gslDestroyEntity(pEnt);
		}
	}
	EntityIteratorRelease(pIter);

	// Mark partition as destroyed
	pPart->bIsDestroyed = true;

	TimedCallback_Run(partition_DestroyLate, pPart, 3);

	loadend_printf("done");
	coarseTimerStopInstance(NULL, "partition_DestroyByIdx");
}


void partition_InitMakeBinsAndExit(void)
{
	// We want a partition to exist during makebinsandexit
	if (!partition_ExistsByIdx(1)) {
		MapPartitionSummary summary = {0};

		// Only actually create if it hasn't be created in the past
		summary.eOwnerType = gGSLState.gameServerDescription.baseMapDescription.ownerType;
		summary.iOwnerID = gGSLState.gameServerDescription.baseMapDescription.ownerID;
		summary.pMapVariables = gGSLState.gameServerDescription.baseMapDescription.mapVariables;

		partition_CreateAndInit(&summary, "partition_InitMakeBinsAndExit");
	}
}


// ----------------------------------------------------------------------------------
// Partition System Initialization
// ----------------------------------------------------------------------------------

AUTO_RUN;
void partition_InitSystem(void)
{
	exprContextSetStaticCheckPartition(PARTITION_STATIC_CHECK);
}


bool partition_TestForImmediateDeath(int iPartitionIdx)
{
	MapPartition *pPartition = partition_FromIdx(iPartitionIdx);
	if (!pPartition)
	{
		return false;
	}

	return !!(pPartition->eFlags & PARTITION_TESTFORIMMEDIATEDEATH);
}

void partition_SetTestForImmediateDeath(int iPartitionIdx, bool bSet)
{
	MapPartition *pPartition = partition_FromIdx(iPartitionIdx);
	if (!pPartition)
	{
		return;
	}

	if (bSet)
	{
		pPartition->eFlags |= PARTITION_TESTFORIMMEDIATEDEATH;
	}
	else
	{
		pPartition->eFlags &= ~PARTITION_TESTFORIMMEDIATEDEATH;
	}
}

//---------Partition debug logging stuff

#define MAX_LOW_PRIORITY_LOGS_AROUND_EACH_HIGH_PRIORITY 10


AUTO_STRUCT;
typedef struct PartitionDebugLog
{
	enumPartitionLogType eLogType;
	U32 iTime; AST(FORMATSTRING(HTML_SECS_AGO=1))
	char *pFullString; AST(ESTRING)
} PartitionDebugLog;

AUTO_STRUCT;
typedef struct PartitionDebugLogs
{
	int iIdx; AST(KEY)
	PartitionDebugLog **ppLogs;
	int iNumCreates;
	int iNumLowPriority;
} PartitionDebugLogs;

PartitionDebugLogs **ppPartitionLogsByIdx = NULL;

PartitionDebugLogs *partition_FindDebugLogs(int iPartitionIdx)
{
	PartitionDebugLogs *pLogs = eaGet(&ppPartitionLogsByIdx, iPartitionIdx);
	if (pLogs)
	{
		return pLogs;
	}

	pLogs = StructCreate(parse_PartitionDebugLogs);
	pLogs->iIdx = iPartitionIdx;
	eaSet(&ppPartitionLogsByIdx, pLogs, iPartitionIdx);

	return pLogs;
}

void partition_FixupPartitionDebugLogs(PartitionDebugLogs *pLogs)
{

	int iNumLogs = eaSize(&pLogs->ppLogs);

	if (pLogs->iNumCreates == 3)
	{
		while (pLogs->ppLogs[0]->eLogType != PARTITION_CREATED)
		{
			StructDestroy(parse_PartitionDebugLog, eaRemove(&pLogs->ppLogs, 0));
		}

		StructDestroy(parse_PartitionDebugLog, eaRemove(&pLogs->ppLogs, 0));

		while (pLogs->ppLogs[0]->eLogType != PARTITION_CREATED)
		{
			StructDestroy(parse_PartitionDebugLog, eaRemove(&pLogs->ppLogs, 0));
		}

		pLogs->iNumCreates --;

		return;
	}

	if (pLogs->iNumLowPriority == MAX_LOW_PRIORITY_LOGS_AROUND_EACH_HIGH_PRIORITY * 2 + 1)
	{
		PartitionDebugLog *pGroupingLog = pLogs->ppLogs[iNumLogs - MAX_LOW_PRIORITY_LOGS_AROUND_EACH_HIGH_PRIORITY - 1];
		pGroupingLog->eLogType = PARTITION_GROUPED_LOW_PRIORITY;
		estrPrintf(&pGroupingLog->pFullString, "Too many low priority logs in a row, 1 collapsed down into this log");
	}
	else if (pLogs->iNumLowPriority > MAX_LOW_PRIORITY_LOGS_AROUND_EACH_HIGH_PRIORITY * 2 + 1)
	{
		PartitionDebugLog *pGroupingLog = pLogs->ppLogs[iNumLogs - MAX_LOW_PRIORITY_LOGS_AROUND_EACH_HIGH_PRIORITY - 2];

		estrPrintf(&pGroupingLog->pFullString, "Too many low priority logs in a row, %d collapsed down into this log", pLogs->iNumLowPriority - MAX_LOW_PRIORITY_LOGS_AROUND_EACH_HIGH_PRIORITY * 2);
		StructDestroy(parse_PartitionDebugLog, eaRemove(&pLogs->ppLogs, iNumLogs - MAX_LOW_PRIORITY_LOGS_AROUND_EACH_HIGH_PRIORITY - 1));
	}
}

void partition_DebugLogInternal(enumPartitionLogType eLogType, int iPartitionIdx, FORMAT_STR const char *pFmt, ...)
{
	PartitionDebugLog *pLog = StructCreate(parse_PartitionDebugLog);
	PartitionDebugLogs *pLogs = partition_FindDebugLogs(iPartitionIdx);


	estrGetVarArgs(&pLog->pFullString, pFmt);
	pLog->eLogType = eLogType;
	pLog->iTime = timeSecondsSince2000();

	eaPush(&pLogs->ppLogs, pLog);
	if (eLogType == PARTITION_CREATED)
	{
		pLogs->iNumCreates++;
	}

	if (eLogType > PARTITION_LAST_HIGH_PRIORITY)
	{
		pLogs->iNumLowPriority++;
	}
	else
	{
		pLogs->iNumLowPriority = 0;
	}

	partition_FixupPartitionDebugLogs(pLogs);
}

void partition_DumpSinglePartitionLogs(int iPartitionIdx)
{
	char fileName[CRYPTIC_MAX_PATH];
	FILE *pFile;
	char *pDate = NULL;
	int i;
	PartitionDebugLogs *pLogs;

	if (!(pLogs = eaGet(&ppPartitionLogsByIdx, iPartitionIdx)))
	{
		return;
	}

	estrPrintf(&pDate, "%s", timeGetLocalDateStringFromSecondsSince2000(timeSecondsSince2000()));
	estrMakeAllAlphaNumAndUnderscores(&pDate);

	sprintf(fileName, "c:\\temp\\PartitionLogs\\%s\\%d.txt", pDate, iPartitionIdx);
	mkdirtree_const(fileName);
	estrDestroy(&pDate);

	pFile = fopen(fileName, "wt");

	if (!pFile)
	{
		return;
	}

	for (i=0; i < eaSize(&pLogs->ppLogs); i++)
	{
		fprintf(pFile, "%s - %s - %s\n", timeGetLocalDateStringFromSecondsSince2000(pLogs->ppLogs[i]->iTime), 
			StaticDefineIntRevLookup(enumPartitionLogTypeEnum, pLogs->ppLogs[i]->eLogType), 
			pLogs->ppLogs[i]->pFullString);
	}

	fclose(pFile);
}

void partition_DumpPartitionLogs(int iPartitionIdx)
{
	if (iPartitionIdx)
	{
		partition_DumpSinglePartitionLogs(iPartitionIdx);
	}
	else
	{
		int i;

		for (i = 0; i < eaSize(&ppPartitionLogsByIdx); i++)
		{
			if (ppPartitionLogsByIdx[i])
			{
				partition_DumpSinglePartitionLogs(i);
			}
		}
	}
}

AUTO_RUN;
void Partition_InitSystem(void)
{
	resRegisterDictionaryForEArray("Partition logs", RESCATEGORY_OTHER, REDICTFLAG_SPARSE_EARRAY, &ppPartitionLogsByIdx, parse_PartitionDebugLogs);
	resRegisterDictionaryForEArray("Partitions", RESCATEGORY_SYSTEM, REDICTFLAG_SPARSE_EARRAY, &s_ppPartitions, parse_MapPartition);
}


#include "gslPartition_h_ast.c"
#include "gslPartition_c_ast.c"