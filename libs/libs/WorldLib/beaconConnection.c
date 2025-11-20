#include "beaconPrivate.h"
#include "beaconConnection.h"

#include "beaconClient.h"
#include "beaconclientserverprivate.h"
#include "BreakPoint.h"
#include "ESet.h"
#include "genericpoly.h"
#include "LineDist.h"
#include "logging.h"
#include "memorypool.h"
#include "PhysicsSDK.h"
#include "ScratchStack.h"
#include "wcoll/collcache.h"
#include "wcoll/collide.h"
#include "wcoll/entworldcoll.h"
#include "WorldColl.h"
#include "WorldGrid.h"
#include "worldLib.h"
#include "bounds.h"

StashTable smdHullCache = NULL;
static StashTable interactionToDynConnInfo = NULL;

int gEnableRebuild = 1;

static void beaconCheckDynamicConnsActor(	int iPartitionIdx,
											DynConnState show_hide_change,
											void *id, 
											PSDKActor *actor,
											const Vec3 sceneOffset);

void beaconSubBlockDestroy(BeaconBlock* block);
void beaconDynConnPartitionDestroy(BeaconDynamicConnection *dynConn, BeaconDynamicConnectionPartition *partition);
void beaconDynamicInfoPartitionDestroy(BeaconDynamicInfo **dynInfoParam, BeaconDynamicInfoPartition *partition);
BeaconDynamicInfoPartition* beaconDynamicInfoPartitionGet(BeaconDynamicInfo* dynamicInfo, int partitionIdx, int create);
void beaconGalaxyRemakeConnections(BeaconBlock *galaxy);
void beaconGalaxyDestroyConnections(BeaconBlock *galaxy);

typedef struct BeaconNoDynConnVolume {
	Beacon **beacons;

	const void* ptrId;

	union {
		struct {
			Mat4 world_mat;
			Vec3 local_min;
			Vec3 local_max;
		} box;

		struct {
			Vec3 pos;
			F32 radius;
		} sphere;
	};

	U32 isBox : 1;
} BeaconNoDynConnVolume;

typedef struct BeaconWCOToIdSubId {
	WorldCollObject *wco;
	void *id;
	int subId;
} BeaconWCOToIdSubId;

F32 beaconGalaxyGroupJumpIncrement = 5;
StashTable beaconNoDynConnVolumes;

StashTable beaconIdSubIdLookup = NULL;

static S32 beaconIdHasDynConns(void *id, int subId)
{
	BeaconDynamicInfoList *list = NULL;

	if(stashAddressFindPointer(interactionToDynConnInfo, id, &list))
	{
		if(subId==-1)
			return !!eaSize(&list->infos);
		else
			return !!eaGet(&list->infos, subId);
	}

	return 0;
}

void mmKinematicObjectMsgHandler(const WorldCollObjectMsg* msg);

void* wcoStaticToId(WorldCollObject *wco)
{
	WorldCollisionEntry *collEntry = NULL;
	WorldVolumeEntry *volEntry = NULL;
	WorldInteractionEntry *intEntry = NULL;

	if(wcoGetUserPointer(wco, entryCollObjectMsgHandler, &collEntry))
	{
		intEntry = collEntry->base_entry_data.parent_entry;

		return intEntry;
	}
	else if(wcoGetUserPointer(wco, volumeCollObjectMsgHandler, &volEntry))
		return volEntry;

	return NULL;
}

int wcoStaticToSubId(WorldCollObject *wco)
{
	WorldCollisionEntry *collEntry = NULL;
	WorldVolumeEntry *volEntry = NULL;
	WorldInteractionEntry *intEntry = NULL;

	if(wcoGetUserPointer(wco, entryCollObjectMsgHandler, &collEntry))
	{
		intEntry = collEntry->base_entry_data.parent_entry;

		if(intEntry)
			return eaFind(&intEntry->child_entries, (WorldCellEntry*)collEntry);

		return 0;
	}
	else if(wcoGetUserPointer(wco, volumeCollObjectMsgHandler, &volEntry))
		return 0;

	return 0;
}

static void beaconHandleActorCreated(	int iPartitionIdx,
										WorldCollObject *wco, 
										PSDKActor *actor,
										const Vec3 sceneOffset)
{
#if !PLATFORM_CONSOLE
	if(IsGameServerSpecificallly_NotRelatedTypes())
	{
		if(wcoIsDynamic(wco))
		{
			if(!beaconIdHasDynConns(wco, 0))
				beaconCheckDynamicConnsActor(iPartitionIdx, DYN_CONN_CREATE, wco, actor, sceneOffset);
			beaconCheckDynamicConnsActor(iPartitionIdx, DYN_CONN_SHOW, wco, actor, sceneOffset);
		}
		else
		{
			void *id = wcoStaticToId(wco);
			int subId;

			if(!id)
				return;

			subId = wcoStaticToSubId(wco);

			if(!beaconIdHasDynConns(id, subId))
				beaconQueueDynConnCheck(DYN_CONN_CREATE, wco, id, subId, iPartitionIdx);
			beaconQueueDynConnCheck(DYN_CONN_SHOW, wco, id, subId, iPartitionIdx);
		}
	}
#endif
}

static void beaconHandleActorDestroyed(	int iPartitionIdx,
										WorldCollObject *wco,
										PSDKActor* actor,
										const Vec3 sceneOffset)
{
#if !PLATFORM_CONSOLE
	if(IsGameServerSpecificallly_NotRelatedTypes())
	{
		if(wcoIsDynamic(wco))
		{
			beaconCheckDynamicConnsActor(iPartitionIdx, DYN_CONN_DESTROY, wco, actor, sceneOffset);
		}
		else
		{
			BeaconWCOToIdSubId *idsubid = NULL;

			if(!beaconIdSubIdLookup)
				return;

			if(!stashAddressFindPointer(beaconIdSubIdLookup, wco, &idsubid))
				return;

			if(idsubid->wco!=wco)
				return;

			beaconQueueDynConnCheck(DYN_CONN_HIDE, NULL, idsubid->id, idsubid->subId, iPartitionIdx);
		}
	}
#endif
}

void beaconHandleInteractionDestroyed(void *id)
{
	if(beaconIdHasDynConns(id, -1))
		beaconQueueDynConnCheck(DYN_CONN_DESTROY, NULL, id, -1, worldGetAnyCollPartitionIdx());
}

#if !PLATFORM_CONSOLE

typedef struct BeaconConnectionState
{
	BeaconConnMovementStartCallback start;
	BeaconConnMovementIsFinishedCallback isfinished;
	BeaconConnMovementResultCallback result;

	struct {
		FrameLockedTimer*			flt;
	} wcSwapInfo;

	U32							beaconsProcessed;
	U32							beaconWalkResults[BEACON_WALK_RESULTS];
} BeaconConnectionState;

typedef struct BeaconQueuedDynConnCheck {
	void *id;
	int subId;
	WorldCollObject *wco;		// May or may not exist
	WorldCollStoredModelData *smd;
	Mat4 world_mat;
	DynConnState command;
	int partition;
} BeaconQueuedDynConnCheck;

int queueCheckPaused = 0;
BeaconQueuedDynConnCheck **queuedChecks;
BeaconConnectionState beacon_conn_state;

WorldCollIntegration* wciBeacon;

// Memory pools.

MP_DEFINE(BeaconConnection);
MP_DEFINE(BeaconBlockConnection);
MP_DEFINE(BeaconClusterConnection);
MP_DEFINE(BeaconBlock);

// WorldCollIntegration

void destroyQueuedCheck(BeaconQueuedDynConnCheck *check);

static void beaconHandleWCODestroyed(WorldCollObject *wco)
{
	FOR_EACH_IN_EARRAY(queuedChecks, BeaconQueuedDynConnCheck, check)
	{
		if(check->wco==wco)
		{
			destroyQueuedCheck(check);
			eaRemoveFast(&queuedChecks, FOR_EACH_IDX(-, check));
		}
	}
	FOR_EACH_END;
}

void beaconWorldCollIntegrationMsgHandler(const WorldCollIntegrationMsg* msg){
	switch(msg->msgType){
		xcase WCI_MSG_NOBG_WORLDCOLLOBJECT_DESTROYED:{
			beaconHandleWCODestroyed(msg->nobg.worldCollObjectDestroyed.wco);	
		}
		
		xcase WCI_MSG_NOBG_ACTOR_CREATED:{
			beaconHandleActorCreated(	worldGetPartitionIdxByColl(msg->nobg.actorCreated.wc),
										msg->nobg.actorCreated.wco,
										msg->nobg.actorCreated.psdkActor,
										msg->nobg.actorCreated.sceneOffset);
		}

		xcase WCI_MSG_NOBG_ACTOR_DESTROYED:{
			beaconHandleActorDestroyed(	worldGetPartitionIdxByColl(msg->nobg.actorDestroyed.wc),
										msg->nobg.actorDestroyed.wco,
										msg->nobg.actorDestroyed.psdkActor,
										msg->nobg.actorDestroyed.sceneOffset);
		}
	}
}

AUTO_RUN;
void beaconInitWorldCollIntegration(void){
	if(!wciBeacon){
		wcIntegrationCreate(&wciBeacon,
							beaconWorldCollIntegrationMsgHandler,
							NULL,
							"Beacon");
	}
}

// Timer.

const char* beaconCurTimeString(int start){
	static U32 staticStartTime;
	U32 curTime = timeSecondsSince2000();
	
	if(start){
		staticStartTime = curTime;
		
		return NULL;
	}else{
		static char* buffer = NULL;
		
		U32 elapsedTime = curTime - staticStartTime;

		estrPrintf(&buffer, "%d:%2.2d:%2.2d", elapsedTime / 3600, (elapsedTime / 60) % 60, elapsedTime % 60);
		
		return buffer;
	}
}

// BeaconConnection

BeaconConnection* createBeaconConnection(void){
	MP_CREATE(BeaconConnection, 5000);
	
	return MP_ALLOC(BeaconConnection);
}

void destroyBeaconConnection(BeaconConnection* conn){
	MP_FREE(BeaconConnection, conn);
}

size_t getAllocatedBeaconConnectionCount(void){
	return mpGetAllocatedCount(MP_NAME(BeaconConnection));
}

BeaconBlockConnection* beaconBlockConnectionAlloc(void)
{
	MP_CREATE(BeaconBlockConnection, 1000);

	if(eaSize(&beacon_state.connsForRealloc))
	{
		BeaconBlockConnection *conn = eaPop(&beacon_state.connsForRealloc);
		ZeroStruct(conn);
		return conn;
	}

	return MP_ALLOC(BeaconBlockConnection);
}

// BeaconBlockConnection
BeaconBlockConnection* beaconBlockConnectionCreate(BeaconBlock *srcBlock, BeaconBlock *dstBlock)
{
	BeaconBlockConnection* conn = beaconBlockConnectionAlloc();

	conn->srcBlock = srcBlock;
	conn->destBlock = dstBlock;
	arrayPushBackUnique(&dstBlock->bbIncoming, conn);

	return conn;
}

void beaconBlockConnectionDestroy(BeaconBlockConnection* conn)
{
	arrayFindAndRemoveFast(&conn->destBlock->bbIncoming, conn);
	eaDestroy(&conn->conns);

	eaPush(&beacon_state.connsForRealloc, conn);
	//MP_FREE(BeaconBlockConnection, conn);
}

void beaconBlockConnectionAddConnectionData(BeaconBlockConnection *blockConn, F32 minJumpH, F32 minH, F32 maxH, void* conn, S32 disabled)
{
	if(!disabled)
	{
		if(blockConn->blockCount == blockConn->connCount)
		{
			blockConn->minJumpHeight = minJumpH;
			blockConn->minHeight = minH;
			blockConn->maxHeight = maxH;
		}
		else
		{
			if(minJumpH < blockConn->minJumpHeight)
				blockConn->minJumpHeight = minJumpH;

			if(minH < blockConn->minHeight)
				blockConn->minHeight = minH;

			if(maxH > blockConn->maxHeight)
				blockConn->maxHeight = maxH;
		}
	}
}

static F32 getMinJumpHeight(BeaconConnection* conn)
{
	F32 minJumpHeight = conn->distance / 2;
	F32 minHeight = conn->minHeight + conn->groundDist;

	return (minJumpHeight < minHeight) ? minHeight : minJumpHeight;
}

static void beaconConnectionGetRaisedData(BeaconConnection *conn, F32 *minJumpHeightOut, F32 *minHeightOut, F32 *maxHeightOut)
{
	*minJumpHeightOut	= getMinJumpHeight(conn);
	*minHeightOut		= conn->groundDist + conn->minHeight;
	*maxHeightOut		= conn->groundDist + conn->maxHeight;
}

void beaconRaisedBlockConnectionAddBeaconConnection(int partition, BeaconBlockConnection *blockConn, BeaconConnection *beaconConn, S32 disabled)
{
	F32 minJumpHeight;
	F32 minHeight;
	F32 maxHeight;

	beaconConnectionGetRaisedData(beaconConn, &minJumpHeight, &minHeight, &maxHeight);
	beaconBlockConnectionAddConnectionData(blockConn, minJumpHeight, minHeight, maxHeight, beaconConn, disabled);
}

void beaconBlockConnectionRescanHeightsGalaxy(BeaconBlockConnection *blockConn)
{
	if(eaSize(&blockConn->conns)==0)
	{
		blockConn->minHeight = 0;
		blockConn->maxHeight = 0;
		blockConn->minJumpHeight = 0;
		return;
	}

	FOR_EACH_IN_EARRAY(blockConn->conns, BeaconBlockConnection, conn)
	{
		if(FOR_EACH_IDX(-, conn) == 0)
		{
			blockConn->minHeight = conn->minHeight;
			blockConn->maxHeight = conn->maxHeight;
			blockConn->minJumpHeight = conn->minJumpHeight;
		}
		else
		{
			if(conn->minJumpHeight < blockConn->minJumpHeight)
				blockConn->minJumpHeight = conn->minJumpHeight;

			if(conn->minHeight < blockConn->minHeight)
				blockConn->minHeight = conn->minHeight;

			if(conn->maxHeight > blockConn->maxHeight)
				blockConn->maxHeight = conn->maxHeight;
		}
	}
	FOR_EACH_END;
}

void beaconBlockConnectionRescanHeightsSubblock(BeaconBlockConnection *blockConn)
{
	if(eaSize(&blockConn->conns)==0)
	{
		blockConn->minHeight = 0;
		blockConn->maxHeight = 0;
		blockConn->minJumpHeight = 0;
		return;
	}

	FOR_EACH_IN_EARRAY(blockConn->conns, BeaconConnection, conn)
	{
		F32 minHeight, maxHeight, minJumpHeight;

		beaconConnectionGetRaisedData(conn, &minJumpHeight, &minHeight, &maxHeight);

		if(FOR_EACH_IDX(-, conn) == 0)
		{
			blockConn->minHeight = minHeight;
			blockConn->maxHeight = maxHeight;
			blockConn->minJumpHeight = minJumpHeight;
		}
		else
		{
			if(minJumpHeight < blockConn->minJumpHeight)
				blockConn->minJumpHeight = minJumpHeight;

			if(minHeight < blockConn->minHeight)
				blockConn->minHeight = minHeight;

			if(maxHeight > blockConn->maxHeight)
				blockConn->maxHeight = maxHeight;
		}
	}
	FOR_EACH_END;
}

void beaconBlockConnectionRemoveConnectionData(BeaconBlockConnection *blockConn, int partitionId, S32 disabled, void *conn)
{
	//assert(eaFind(&blockConn->conns, conn) != -1);

	if(blockConn->destBlock->galaxy)
		beaconBlockConnectionRescanHeightsGalaxy(blockConn);
	else
		beaconBlockConnectionRescanHeightsSubblock(blockConn);
}

void beaconBlockConnectionRemBeaconConnection(BeaconBlockConnection *blockConn, int partitionId, BeaconConnection *beaconConn)
{
	beaconBlockConnectionRemoveConnectionData(blockConn, partitionId, beaconConn->disabled, beaconConn);
}

void beaconBlockConnectionAddGalaxyConnection(int partitionId, BeaconBlockConnection *galaxyConn, BeaconBlockConnection *destConn)
{
	beaconBlockConnectionAddConnectionData(	galaxyConn, 
											destConn->minJumpHeight, 
											destConn->minHeight, 
											destConn->maxHeight, 
											destConn, 
											destConn->connCount == destConn->blockCount);
}

void beaconBlockConnectionRemoveBlockConnection(BeaconBlockConnection *blockConn, int partitionId, BeaconBlockConnection *conn)
{
	beaconBlockConnectionRemoveConnectionData(blockConn, partitionId, conn->blockCount == conn->connCount, conn);
}

// BeaconClusterConnection

BeaconClusterConnection* beaconClusterConnectionCreate(BeaconBlock *src, BeaconBlock *dst)
{
	BeaconClusterConnection* conn;
	
	MP_CREATE(BeaconClusterConnection, 1000);
	
	conn = MP_ALLOC(BeaconClusterConnection);
	conn->srcCluster = src;
	conn->dstCluster = dst;
	
	return conn;
}

static void beaconClusterConnectionDestroy(BeaconClusterConnection* conn)
{
	arrayFindAndRemoveFast(&conn->dstCluster->bbIncoming, conn);

	MP_FREE(BeaconClusterConnection, conn);
}

// BeaconBlock

BeaconBlock *beaconBlockAlloc()
{
	MP_CREATE(BeaconBlock, 100);

	return MP_ALLOC(BeaconBlock);
}

BeaconBlock* beaconBlockCreate(int partitionIdx)
{
	BeaconBlock* block = beaconBlockAlloc();
	block->partitionIdx = partitionIdx;
	
	return block;	
}

int beaconSubBlockGetIdx(BeaconStatePartition *partition)
{
	if(eaiSize(&partition->subBlockIds))
		return eaiPop(&partition->subBlockIds);

	return partition->nextSubBlockIndex++;
}

BeaconBlock* beaconSubBlockCreate(int partitionIdx)
{
	BeaconBlock* block = beaconBlockAlloc();
	BeaconStatePartition *partition = beaconStatePartitionGet(partitionIdx, true);
	block->partitionIdx = partitionIdx;
	block->globalIndex = beaconSubBlockGetIdx(partition);
	block->isSubBlock = true;
	block->isGalaxy = block->isCluster = block->isGridBlock = false;

	return block;	
}

BeaconBlock* beaconGridBlockCreate(int partitionIdx)
{
	BeaconBlock* block = beaconBlockAlloc();
	block->partitionIdx = partitionIdx;
	block->isGridBlock = true;
	block->isGalaxy = block->isCluster = block->isSubBlock = false;

	return block;	
}

int beaconGalaxyGetIdx(BeaconStatePartition *partition, int galaxySet)
{
	if(eaiSize(&partition->galaxyIds[galaxySet]))
		return eaiPop(&partition->galaxyIds[galaxySet]);

	return partition->nextGalaxyIndex[galaxySet]++;
}

BeaconBlock* beaconGalaxyCreate(int partitionIdx, int galaxySet)
{
	BeaconStatePartition *partition = beaconStatePartitionGet(partitionIdx, true);
	BeaconBlock* block = beaconBlockAlloc();
	block->partitionIdx = partitionIdx;
	block->galaxySet = galaxySet;
	block->isGalaxy = true;
	block->isSubBlock = block->isCluster = block->isGridBlock = false;
	block->globalIndex = beaconGalaxyGetIdx(partition, galaxySet);

	return block;	
}

int beaconClusterGetIdx(BeaconStatePartition *partition)
{
	if(eaiSize(&partition->clusterIds))
		return eaiPop(&partition->clusterIds);

	return partition->nextClusterIndex++;
}

BeaconBlock* beaconClusterCreate(int partitionIdx)
{
	BeaconStatePartition *partition = beaconStatePartitionGet(partitionIdx, true);
	BeaconBlock* block = beaconBlockAlloc();
	block->partitionIdx = partitionIdx;
	block->isCluster = true;
	block->isGalaxy = block->isSubBlock = block->isGridBlock = false;
	block->globalIndex = beaconClusterGetIdx(partition);

	return block;
}

void beaconDestroyBlockArrays(BeaconBlock* block)
{
	destroyArrayPartial(&block->beaconArray);
	destroyArrayPartial(&block->subBlockArray);
	destroyArrayPartial(&block->bbIncoming);
	if(block->isSubBlock)
	{
		beaconSubBlockDestroyConnections(block);
	}
	else if(block->isCluster)
	{
		destroyArrayPartialEx(&block->gbbConns, beaconClusterConnectionDestroy);
		destroyArrayPartialEx(&block->rbbConns, beaconClusterConnectionDestroy);
	}
	else if(block->isGalaxy)
	{
		beaconGalaxyDestroyConnections(block);
	}
	else if(block->isGridBlock)
		;
	else
		devassert(0);
}

void beaconSubBlockDestroy(BeaconBlock* block)
{
	int i;
	BeaconStatePartition *partition;
	if(!block)
		return;

	assert(!block->isCluster && !block->isGalaxy && !block->isGridBlock);

	partition = beaconStatePartitionGet(block->partitionIdx, true);
	eaiPush(&partition->subBlockIds, block->globalIndex);

	for(i=0; i<block->beaconArray.size; i++)
	{
		Beacon *b = block->beaconArray.storage[i];
		BeaconPartitionData *beaconPartition = NULL;
		if(!b)
			continue;

		beaconPartition = beaconGetPartitionData(b, block->partitionIdx, false);

		if(beaconPartition)
			beaconPartition->block = NULL;
	}

	beaconDestroyBlockArrays(block);
	
	MP_FREE(BeaconBlock, block);
}

void beaconGridBlockDestroy(BeaconBlock* block){
	if(!block)
		return;
	
	assert(block->isGridBlock);

	// Gridblock subblocks are owned
	destroyArrayPartialEx(&block->subBlockArray, beaconSubBlockDestroy);
	beaconDestroyBlockArrays(block);
	
	MP_FREE(BeaconBlock, block);
}

void beaconGalaxyDestroy(BeaconBlock* block)
{
	BeaconStatePartition *partition;

	if(!block)
		return;

	assert(block->isGalaxy);

	partition = beaconStatePartitionGet(block->partitionIdx, true);
	eaiPush(&partition->galaxyIds[block->galaxySet], block->globalIndex);

	beaconDestroyBlockArrays(block);
	
	MP_FREE(BeaconBlock, block);
}

void beaconClusterDestroy(BeaconBlock* block)
{
	BeaconStatePartition *partition;
	if(!block)
		return;

	assert(block->isCluster);

	partition = beaconStatePartitionGet(block->partitionIdx, true);
	eaiPush(&partition->clusterIds, block->globalIndex);

	beaconDestroyBlockArrays(block);
	
	MP_FREE(BeaconBlock, block);
}

// BeaconBlock memory pool.
void beaconInitArray_dbg(Array* array, U32 count, const char* file, int line){
	array->storage = realloc_timed(array->storage, count*sizeof(void*), _NORMAL_BLOCK, file, line);
	devassert(array->size==0);
	array->size = 0;
	array->maxSize = count;
	memset(array->storage, 0, array->maxSize*sizeof(void*));
}

void beaconBlockInitArray_dbg(Array* array, U32 count, const char* file, int line){
	array->storage = realloc_timed(array->storage, count*sizeof(void*), _NORMAL_BLOCK, file, line);
	//devassert(array->size==0);
	array->size = 0;
	array->maxSize = count;
	memset(array->storage, 0, array->maxSize*sizeof(void*));
}

void beaconInitCopyArray_dbg(Array* array, Array* source, const char* file, int line){
	beaconInitArray_dbg(array, source->size, file, line);
	memcpy(array->storage, source->storage, sizeof(source->storage[0]) * source->size);
	array->size = source->size;
}

void beaconBlockInitCopyArray_dbg(Array* array, Array* source, const char* file, int line){
	beaconBlockInitArray_dbg(array, source->size, file, line);
	memcpy(array->storage, source->storage, sizeof(source->storage[0]) * source->size);
	array->size = source->size;
}

// Connection stuff.

static int usedPhysicsSteps;

enum {
	WALKRESULT_SUCCESS = 0,
	WALKRESULT_NO_LOS,
	WALKRESULT_BLOCKED_SHORT,
	WALKRESULT_BLOCKED_LONG,
	WALKRESULT_TOO_HIGH,
};

U32 beaconConnectionGetNumWalksProcessed(void)
{
	return beacon_conn_state.beaconsProcessed;
}

U32 beaconConnectionGetNumResults(U32 index)
{
	return beacon_conn_state.beaconWalkResults[index];
}

void beaconWalkStateInit(BeaconWalkState *state, int processCount, Vec3 startPos, Vec3 targetPos)
{
	ZeroStruct(state);

	state->processCount = processCount;
	state->optional = 0;
	state->unmoving = 0;
	copyVec3(startPos, state->startPos);
	copyVec3(targetPos, state->targetPos);

	state->bestDistToTargetSQR = distance3Squared(startPos, state->targetPos);
	subVec3(state->targetPos, startPos, state->srcToDstLineDir);
	state->srcToDstLineDir[1] = 0;
	state->srcToDstLineLen = normalVec3(state->srcToDstLineDir);
	state->bidir = true;
}

int beaconCheckWalkState(BeaconWalkState* state, Vec3 curPos, F32 maxSpeed, F32 *speedOut)
{
	F32 distMovedSQR;
	F32 distToTargetSQR;
	F32 linedist;
	Vec3 linecoll;
	BeaconWalkResult res = BEACON_WALK_UNFINISHED;
	Vec3 posTemp;

	state->processCount--;

	distToTargetSQR = distance3SquaredXZ(curPos, state->targetPos);
	
	distMovedSQR = distance3Squared(curPos, state->lastPos);
	copyVec3(curPos, state->lastPos);

	if(distToTargetSQR < state->bestDistToTargetSQR)
	{
		state->bestDistToTargetSQR = distToTargetSQR;
	}

	if(distToTargetSQR > SQR(BEACONCONFIGVAR(MovementPosTol)))
	{
		F32 dist = sqrt(distToTargetSQR);

		*speedOut = dist * 30 > maxSpeed ? maxSpeed : dist * 30;
	}
	else
	{
		F32 yDiff = state->targetPos[1] - curPos[1];
		if(	yDiff > BEACONCONFIGVAR(MovementMinHeight) && yDiff < BEACONCONFIGVAR(MovementMaxHeight) )
			return BEACON_WALK_SUCCESS;
		else
			return BEACON_WALK_TOO_HIGH;
	}

	if(state->processCount <= 0)
		return BEACON_WALK_STEPS;

	if(distMovedSQR < SQR(BEACONCONFIGVAR(MovementStuckTol)))	// 500X tolerance of AI movement
	{
		// not moving
		state->unmoving++;
		if(state->unmoving > 7)
			return BEACON_WALK_STUCK;
	}
	else
	{
		state->unmoving = 0;
	}

	copyVec3(curPos, posTemp);
	posTemp[1] = state->targetPos[1];
	linedist = PointLineDistSquared(posTemp, state->startPos, state->srcToDstLineDir, state->srcToDstLineLen, linecoll);

	if(linedist > SQR(BEACONCONFIGVAR(MovementDivertTol)) && ((F32)state->processCount/BM_MAX_COUNT)>0.1)
		return BEACON_WALK_DIVERT;
	else if(linedist > SQR(BEACONCONFIGVAR(MovementDivertOpt)))
		state->optional = 1;

	return BEACON_WALK_UNFINISHED;
}

static BeaconWalkResult beaconCanWalkBetweenBeacons(int iPartitionIdx, Beacon* src, Beacon* dst, Vec3 startPos, Entity* e, int *optionalOut, int *bidirOut)
{
	WorldCollCollideResults rcres;
	MotionState motion = {0};
	BeaconWalkState state = {0};
	BeaconWalkResult res;
	Vec3 vel;
	Vec3 start;
	Vec3 lineDir;
	F32 lineLen;
	CollInfo coll;
	WorldCollGridCell *pCell2;

	if(beacon_client.debug_state &&
		!vec3IsZero(beacon_client.debug_state->debug_pos) && 
		(distance3Squared(src->pos, beacon_client.debug_state->debug_pos) < 5 ||
		distance3Squared(dst->pos, beacon_client.debug_state->debug_pos) < 5))
	{
		printf("");
	}

	if(startPos)
	{
		copyVec3(startPos, start);
	}
	else
	{
		copyVec3(src->pos, start);
	}

	wcGetGridCellByWorldPosFG(beaconGetActiveWorldColl(iPartitionIdx), &motion.wcCell, start);
	wcGetGridCellByWorldPosFG(beaconGetActiveWorldColl(iPartitionIdx), &pCell2, dst->pos);
	// OK the real question is, do these cells have different scenes?
	if(	!wcCellGetWorldColl(motion.wcCell,
							&coll.wc)
		||
		!wcCellGetSceneAndOffset(	motion.wcCell,
									&coll.psdkScene,
									coll.sceneOffset))
	{
		coll.wc = NULL;
		coll.psdkScene = NULL;
	}

	{
		WorldColl * wc2;
		wcCellGetWorldColl(pCell2,&wc2);
		devassert(coll.wc == wc2);
	}

	coll.filterBits = WC_FILTER_BIT_MOVEMENT;
	coll.actorIgnoredCB = NULL;
	coll.userPointer = NULL;

	{
		Vec3 vMin,vMax,vPadding;
		copyVec3(start,vMin);
		copyVec3(start,vMax);

		vMin[0] -= 20.0f;
		vMin[1] -= 40.0f;
		vMin[2] -= 20.0f;

		vMax[0] += 20.0f;
		vMax[1] += 40.0f;
		vMax[2] += 20.0f;

		vPadding[0] = 0.0f;
		vPadding[1] = 0.0f;
		vPadding[2] = 0.0f;
		collideBuildAccelerator(vMin,vMax,vPadding,&coll);
	}

	assert(beacon_conn_state.start && beacon_conn_state.isfinished && beacon_conn_state.result);
	copyVec3(start, beacon_process.entityPos);

	// Check LOS first on "up" connections
	if((dst->pos[1] - src->pos[1])/distance3XZ(dst->pos, src->pos) >= 2)
	{
		beaconRayCollide(iPartitionIdx, beaconGetActiveWorldColl(iPartitionIdx), src->pos, dst->pos, WC_FILTER_BIT_MOVEMENT, &rcres);

		if(rcres.hitSomething)
		{
			res = BEACON_WALK_GRADE;
			goto walk_finished;
		}

		beaconRayCollide(iPartitionIdx, beaconGetActiveWorldColl(iPartitionIdx), dst->pos, src->pos, WC_FILTER_BIT_MOVEMENT, &rcres);

		if(rcres.hitSomething)
		{
			res = BEACON_WALK_GRADE;
			goto walk_finished;			
		}
	}

	motion.step_height = 1.5;

	copyVec3(start, motion.last_pos);
	copyVec3(start, motion.pos);
	motion.filterBits = WC_FILTER_BIT_MOVEMENT;

	subVec3(dst->pos, start, lineDir);
	lineLen = normalVec3(lineDir);

	beaconWalkStateInit(&state, BM_MAX_COUNT, start, dst->pos);
	while(state.processCount>0)
	{
		F32 speed;
		Vec3 curPos;
		F32 dist;

		copyVec3(motion.pos, curPos);
		res = beaconCheckWalkState(&state, curPos, 12, &speed);

		if(res>=0)
			break;

		zeroVec3(vel);
		if(motion.hit_ground)
		{
			subVec3(dst->pos, motion.pos, vel);
			vel[1] = 0;
			normalVec3(vel);
			scaleVec3(vel, speed/30, vel);
		}
		else
		{
			Vec3 ground;
			int floor = 0;
			copyVec3(curPos, ground);
			
			vecY(vel) = worldSnapPosToGround(iPartitionIdx, ground, 1, -100, &floor);
			if(!floor)
			{
				res = BEACON_WALK_GRADE;
				break;
			}
		}
		vecY(vel) -= 0.1;				// Gravity

		copyVec3(curPos, motion.last_pos);
		copyVec3(vel, motion.vel);
		addVec3(motion.last_pos, motion.vel, motion.pos);
		wcGetGridCellByWorldPosFG(beaconGetActiveWorldColl(iPartitionIdx), &motion.wcCell, curPos);

		worldMoveMe(&motion);

		if(motion.hit_ground && motion.ground_normal[1]>=-1.0f && motion.ground_normal[1]<=1.0f)
		{
			F32 angle = RAD(90) - asinf(motion.ground_normal[1]);

			// Check to see if grade is too steep
			if(angle>RAD(55))
			{
				// Check to see if grade is opposed to motion
				if(dotVec3(motion.ground_normal, vel)<0)
				{
					res = BEACON_WALK_GRADE;
					break;
				}
				else	// It would be opposed in the opposite direction
					state.bidir = false;
			}
		}

		// Calculate deviation
		dist = PointLineDistSquaredXZ(motion.pos, start, lineDir, lineLen, NULL);

		if(dist > 1)
			state.optional = true;

		if(dist > BEACONCONFIGVAR(MovementDivertTol))
		{
			res = BEACON_WALK_DIVERT;
			break;
		}

		if(distanceY(motion.last_pos, motion.pos)>1.0)
			state.bidir = false;
	}

	copyVec3(motion.pos, beacon_process.entityPos);

walk_finished:

	devassert(res>=0);
	beacon_conn_state.beaconsProcessed++;
	beacon_conn_state.beaconWalkResults[res]++;

	if(res==BEACON_WALK_SUCCESS)
	{
		if(optionalOut)
			*optionalOut = state.optional;
		if(bidirOut)
			*bidirOut = state.bidir;
	}

	if(res==BEACON_WALK_TOO_HIGH)
	{
		//return WALKRESULT_BLOCKED_SHORT;
	}

	if(beaconClientDebugSendWalkResults())
	{
		Packet* pak = BEACON_CLIENT_PACKET_CREATE_VS(BMSG_C2ST_DEBUG_MSG);
		int colors[] = {0xFF00FF00, 0xFFFF0000, 0xFF0000FF, 0xFFFFFF00, 0xFFFF00FF, 0xFFFFFFFF, 0xFFF0F070};
		
		pktSendBits(pak, 32, BDO_WALK);
		SEND_LINE_VEC3(src->pos, dst->pos, colors[res]);

		BEACON_CLIENT_PACKET_SEND_VS(pak);
	}

	collideDisableAccelerator(&coll);

	return res == BEACON_WALK_SUCCESS ? WALKRESULT_SUCCESS : WALKRESULT_BLOCKED_LONG;
}

/* Function beaconGetPassableHeight()
 *	Checks to see if there is a passage between two beacons somewhere above ground.  If
 *	a passable does exist, the "highest" and "lowest" passable heights are modified to
 *	their approximate passable height (accurate within 10 vertical feet).  Otherwise,
 *	"highest" and "lowest" are not modified.
 *
 *	Parameters:
 *		source
 *		target - beacons to be used
 *		highest [out] - highest passable height
 *		lowest [out] - lowest passable height
 *	Returns:
 *		Whether a passage exists between the source beacon and the target beacon.
 */
int beaconGetPassableHeight(int iPartitionIdx, Beacon* source, Beacon* target, float* highest, float* lowest){
	float sourceCeiling, destCeiling;
    float commonCeiling, commonFloor;
    Vec3 horizRayStart, horizRayEnd;
    float curHeight;
    int lowestFound = 0;
    int highestFound = 0;
    float startHeight = *lowest;
    float endHeight = *highest;
	WorldCollCollideResults resultsF;
	WorldCollCollideResults resultsR;
	int collision = 0;

	// Get ceiling height above source.

	sourceCeiling = vecY(source->pos) + beaconGetCeilingDistance(iPartitionIdx, source);

	// Get ceiling height above target.

	destCeiling = vecY(target->pos) + beaconGetCeilingDistance(iPartitionIdx, target);
	
	commonCeiling = MIN(MIN(sourceCeiling, destCeiling), endHeight);
	commonFloor = MAX(MAX(vecY(source->pos), vecY(target->pos)), startHeight);

	if(commonFloor > commonCeiling) // Not possible for a passage to exist
		return 0;

	copyVec3(source->pos, horizRayStart);
	copyVec3(target->pos, horizRayEnd);

	// Test for collisions every ten feet above the beacons, up to the common ceiling height
	//  Find the lowest passage first
	for(curHeight = commonFloor; curHeight < commonCeiling; curHeight += 2.5){
		// Move the horizontal ray end points
		vecY(horizRayStart) = curHeight;
		vecY(horizRayEnd) = curHeight;

		// Do ray collides in case beacons are so close capsule is already there
		collision = beaconRayCollide(iPartitionIdx, beaconGetActiveWorldColl(iPartitionIdx), horizRayStart, horizRayEnd, WC_FILTER_BIT_MOVEMENT, NULL);
		collision = collision || beaconRayCollide(iPartitionIdx, beaconGetActiveWorldColl(iPartitionIdx), horizRayEnd, horizRayStart, WC_FILTER_BIT_MOVEMENT, NULL);
		collision = collision || beaconCapsuleCollide(beaconGetActiveWorldColl(iPartitionIdx), horizRayStart, horizRayEnd, WC_FILTER_BIT_MOVEMENT, NULL);
		collision = collision || beaconCapsuleCollide(beaconGetActiveWorldColl(iPartitionIdx), horizRayEnd, horizRayStart, WC_FILTER_BIT_MOVEMENT, NULL);
			
		if(!collision){
			// There was no collision, record passable height
			if(!lowestFound){
				*lowest = curHeight;
				lowestFound = 1;
				break;
			}
		}
	}

	if(lowestFound)
	{
		// Test for collision with a giant capsule, then keep shrinking it till it fits
		vecY(horizRayStart) = *lowest;
		vecY(horizRayEnd) = *lowest;

		for(curHeight = commonCeiling; curHeight> *lowest; curHeight -= 2.5)
		{
			ZeroStruct(&resultsF);
			wcCapsuleCollideHR(beaconGetActiveWorldColl(iPartitionIdx), horizRayStart, horizRayEnd, 
				WC_FILTER_BIT_MOVEMENT, curHeight - *lowest, 1, &resultsF);
			wcCapsuleCollideHR(beaconGetActiveWorldColl(iPartitionIdx), horizRayEnd, horizRayStart,
				WC_FILTER_BIT_MOVEMENT, curHeight - *lowest, 1, &resultsR);

			if(!resultsF.hitSomething && !resultsR.hitSomething)
			{
				highestFound = 1;
				*highest = curHeight;
				break;
			}

			if(resultsF.hitSomething)
				MIN1(curHeight, vecY(resultsF.posWorldImpact));
			if(resultsR.hitSomething)
				MIN1(curHeight, vecY(resultsR.posWorldImpact));
		}
	}

	if(!lowestFound || !highestFound){
		return 0;
	}
	
	*highest -= vecY(source->pos);
	*lowest -= vecY(source->pos);

	return 1;
}

static int beaconIsVisibleFromBeacon(int iPartitionIdx, Vec3 src, Vec3 dst){
	WorldCollCollideResults res;
	//WorldColl* wc;

	int hit;
	
	hit = beaconRayCollide(iPartitionIdx, beaconGetActiveWorldColl(iPartitionIdx), src, dst, WC_FILTER_BIT_MOVEMENT, &res);
	hit = hit || beaconRayCollide(iPartitionIdx, beaconGetActiveWorldColl(iPartitionIdx), dst, src, WC_FILTER_BIT_MOVEMENT, &res);

	return !hit;
	
	//CollInfo coll;

	//if(!collGrid(NULL, src, dst, &coll, 0, COLL_NOTSELECTABLE | COLL_HITANY | COLL_BOTHSIDES)){
	//	return 1;
	//}

	//return 0;
}

int beaconWithinRadiusXZ(Beacon* source, Beacon* target, float extraDistance){
	float maxdist = max(source->proximityRadius, target->proximityRadius) + extraDistance;
	
	return distance3SquaredXZ(source->pos, target->pos) < SQR(maxdist);
}

static BeaconWalkResult last_walk_result;

int beaconConnectsToBeaconByGround(	int iPartitionIdx,
									Beacon* source,
									Beacon* target,
									Vec3 startPos,
									F32 maxRaiseHeight,
									Entity* ent,
									S32 createEnt,
									S32 *optionalOut,
									S32 *bidirOut)
{
	Vec3 srcPos, dstPos;
	int visible = 0;
	int canWalk;
	int ownsEnt = !ent;
	int i;
	
	maxRaiseHeight = min(maxRaiseHeight, beaconGetCeilingDistance(iPartitionIdx, source));
	maxRaiseHeight = min(maxRaiseHeight, beaconGetCeilingDistance(iPartitionIdx, target));
	
	copyVec3(source->pos, srcPos);
	copyVec3(target->pos, dstPos);
	
	for(i = 0; i <= maxRaiseHeight; i++){
		copyVec3(source->pos, srcPos);
		copyVec3(target->pos, dstPos);

		srcPos[1] += min(i, maxRaiseHeight);
		dstPos[1] += min(i, maxRaiseHeight);

		if(beaconIsVisibleFromBeacon(iPartitionIdx, srcPos, dstPos)){
			visible = 1;																			
			break;
		}
	}
		
	if(visible && !createEnt){
		last_walk_result = WALKRESULT_SUCCESS;
	}else{
		int optional = 0;
		int bidir = 0;
		last_walk_result = beaconCanWalkBetweenBeacons(iPartitionIdx, source, target, NULL, ent, &optional, &bidir);
		
		if(optionalOut)
			*optionalOut = optional;

		if(bidirOut)
			*bidirOut = bidir;
	}
						
	canWalk = last_walk_result == WALKRESULT_SUCCESS;
		
	return canWalk && visible;
}

#define NPC_BEACON_RADIUS 3.f	// replace this with the mapspec one
int beaconConnectsToBeaconNPC(int iPartitionIdx, Beacon* source, Beacon* target){
	Vec3 dir;
	Vec3 pyr;
	Mat3 mat;
	int i;
	int passedCount = 0, failedCount = 0;
	
	subVec3(target->pos, source->pos, dir);
	
	getVec3YP(dir, &pyr[1], &pyr[0]);
	
	pyr[2] = 0;
	
	createMat3YPR(mat, pyr);
	
	for(i = -6; i <= 6; i++){
		Vec3 p1, p2;
		
		scaleVec3(mat[0], i * NPC_BEACON_RADIUS / 6.f, dir);
		
		addVec3(source->pos, dir, p1);
		addVec3(target->pos, dir, p2);

		if(beaconIsVisibleFromBeacon(iPartitionIdx, p1, p2)){
			if(++passedCount == 8){
				return 1;
			}
		}
		else{
			if(++failedCount == 6){
				return 0;
			}
		}
	}
	
	return 0;
}

int beaconCheckEmbedded(int iPartitionIdx, Beacon* source){
	Vec3 p1, p2, dir;

	zeroVec3(dir);
	vecX(dir) += NPC_BEACON_RADIUS;
	addVec3(source->pos, dir, p1);
	subVec3(source->pos, dir, p2);

	if(!beaconIsVisibleFromBeacon(iPartitionIdx, p1, p2))
		return 0;

	zeroVec3(dir);
	vecZ(dir) += NPC_BEACON_RADIUS;
	addVec3(source->pos, dir, p1);
	subVec3(source->pos, dir, p2);

	if(!beaconIsVisibleFromBeacon(iPartitionIdx, p1, p2))
		return 0;
	else
		return 1;
}

static int beaconHasGroundConnectionToBeacon(Beacon* source, Beacon* target, int* index){
	BeaconConnection** conns = (BeaconConnection**)source->gbConns.storage;
	int lo = 0;
	int hi = source->gbConns.size - 1;
	
	while(lo <= hi){
		int mid = (hi + lo) / 2;
		Beacon* midBeacon = conns[mid]->destBeacon;
		
		if(target > midBeacon){
			lo = mid + 1;
		}
		else if(target < midBeacon){
			hi = mid - 1;
		}
		else{
			if(index){
				*index = mid;
			}
			
			return 1;
		}
	}

	return 0;
}

static int beaconHasRaisedConnection(Beacon* source, BeaconConnection* targetConn, int* index){
	BeaconConnection** conns = (BeaconConnection**)source->rbConns.storage;
	int lo = 0;
	int hi = source->rbConns.size - 1;
	
	while(lo <= hi){
		int mid = (hi + lo) / 2;
		BeaconConnection* midConn = conns[mid];
		
		if(targetConn > midConn){
			lo = mid + 1;
		}
		else if(targetConn < midConn){
			hi = mid - 1;
		}
		else{
			if(index){
				*index = mid;
			}
			
			return 1;
		}
	}

	return 0;
}

static Array tempBeacons;
static Array tempSubBlocks;

static int __cdecl compareBeaconConnections(const BeaconConnection** c1p, const BeaconConnection** c2p){
	const BeaconConnection* c1 = *c1p;
	const BeaconConnection* c2 = *c2p;
	
	if(c1 < c2){
		return -1;
	}
	else if(c1 == c2){
		return 0;
	}
	else{
		return 1;
	}
}

static int __cdecl compareBeaconConnectionTargets(const BeaconConnection** c1p, const BeaconConnection** c2p){
	const BeaconConnection* c1 = *c1p;
	const BeaconConnection* c2 = *c2p;
	
	if(c1->destBeacon < c2->destBeacon){
		return -1;
	}
	else if(c1->destBeacon == c2->destBeacon){
		return 0;
	}
	else{
		return 1;
	}
}

static void beaconSortConnsByTarget(Beacon* beacon, int raised){
	if(raised){
		if(beacon->raisedConnsSorted)
			return;
			
		beacon->raisedConnsSorted = 1;

		qsort(	beacon->rbConns.storage,
				beacon->rbConns.size,
				sizeof(beacon->rbConns.storage[0]),
				compareBeaconConnections);
	}else{
		if(beacon->groundConnsSorted)
			return;
			
		beacon->groundConnsSorted = 1;

		qsort(	beacon->gbConns.storage,
				beacon->gbConns.size,
				sizeof(beacon->gbConns.storage[0]),
				compareBeaconConnectionTargets);
	}
}

int allowRaisedSubBlocks = 1;
AUTO_CMD_INT(allowRaisedSubBlocks, allowRaisedSubBlocks);

static S32 beaconBlockSubBlockUsesGround(void)
{
	if(allowRaisedSubBlocks)
	{
		int i;
		WorldRegion **regions = worldGetAllWorldRegions();
		for(i=0; i<eaSize(&regions); i++)
		{
			if(worldRegionGetType(regions[i])==WRT_Space)
				return 0;
		}
	}

	return true;
}

static void beaconBlockSplitSubBlockBeacons(BeaconBlock *newBlock, BeaconBlock *oldBlock, Beacon* srcBeacon)
{
	static Beacon** procList = NULL;
	static BeaconBlock **rebuildList = NULL;
	int useGround = beaconBlockSubBlockUsesGround();
	int i;

	eaClear(&procList);
	eaPush(&procList, srcBeacon);

	eaClearFast(&rebuildList);
	eaPush(&rebuildList, oldBlock);
	eaPush(&rebuildList, newBlock);
	for(i=0; i<oldBlock->bbIncoming.size; i++)
	{
		BeaconBlockConnection *conn = oldBlock->bbIncoming.storage[i];

		assert(conn->destBlock == oldBlock);

		eaPushUnique(&rebuildList, conn->srcBlock);
	}

	while(eaSize(&procList))
	{
		Beacon* b = eaPop(&procList);
		BeaconPartitionData *beaconPart = beaconGetPartitionData(b, newBlock->partitionIdx, true);
		
		// Break old link
		i = arrayFindElement(&beaconPart->block->beaconArray, b);
		arrayRemoveAndFill(&beaconPart->block->beaconArray, i);

		// Make new link
		beaconPart->block = newBlock;
		arrayPushBack(&newBlock->beaconArray, b);

		if(useGround)
		{
			for(i=0; i<b->gbConns.size; i++)
			{
				BeaconConnection *conn = b->gbConns.storage[i];
				Beacon* dest = conn->destBeacon;
				int index = -1;

				if(beaconConnectionIsDisabled(b, newBlock->partitionIdx, conn))
					continue;

				beaconHasGroundConnectionToBeacon(dest, b, &index);

				if(index != -1)
				{
					BeaconPartitionData *partition = NULL;

					conn = dest->gbConns.storage[index];
					if(beaconConnectionIsDisabled(dest, newBlock->partitionIdx, conn))
						continue;

					partition = beaconGetPartitionData(dest, newBlock->partitionIdx, true);

					if(partition->block != oldBlock)
						continue;

					eaPush(&procList, dest);
				}
			}
		}
		else
		{
			for(i=0; i<b->rbConns.size; i++)
			{
				BeaconConnection *conn = b->rbConns.storage[i];
				Beacon* dest = conn->destBeacon;
				BeaconPartitionData *partition = beaconGetPartitionData(dest, newBlock->partitionIdx, true);

				if(beaconConnectionIsDisabled(b, newBlock->partitionIdx, conn))
					continue;

				if(partition->block != oldBlock)
					continue;

				eaPush(&procList, dest);
			}
		}
	}

	FOR_EACH_IN_EARRAY(rebuildList, BeaconBlock, rebuild)
	{
		beaconSubBlockRemakeConnections(rebuild);
	}
	FOR_EACH_END;

	beaconSubBlockCalcPos(newBlock);
	beaconSubBlockCalcPos(oldBlock);
}

static BeaconBlockConnection* findBlockConnection(Array* connsArray, BeaconBlock* destBlock, int* newIndex){
	BeaconBlockConnection** conns = (BeaconBlockConnection**)connsArray->storage;
	int size = connsArray->size;
	int lo = 0;
	int hi = size - 1;

	while(lo <= hi){
		int mid = (hi + lo) / 2;
		BeaconBlock* midBlock = conns[mid]->destBlock;

		if((intptr_t)destBlock > (intptr_t)midBlock){
			lo = mid + 1;
		}
		else if((intptr_t)destBlock < (intptr_t)midBlock){
			hi = mid - 1;
		}
		else{
			return conns[mid];
		}
	}

	// Hopefully at this point, lo contains the index that the new connection should be inserted into.

	if(newIndex){
		*newIndex = lo;
	}

	return NULL;
}

static int blockHasGroundConnectionToBlock(BeaconBlock* source, BeaconBlock* target){
	if(findBlockConnection(&source->gbbConns, target, NULL)){
		return 1;
	}

	return 0;
}

static void beaconBlockSplitGalaxySubBlocks(BeaconBlock *newGalaxy, BeaconBlock *oldGalaxy, int maxJumpHeight, BeaconBlock* srcBlock, BeaconBlock *unreach)
{
	static BeaconBlock** procList = NULL;
	static BeaconBlock** rebuildList = NULL;
	int i;

	eaClear(&procList);
	eaPush(&procList, srcBlock);

	assert(newGalaxy->isGalaxy && !newGalaxy->isGridBlock && !newGalaxy->isCluster);

	eaClear(&rebuildList);
	eaPush(&rebuildList, oldGalaxy);
	eaPush(&rebuildList, newGalaxy);
	for(i=0; i<oldGalaxy->bbIncoming.size; i++)
	{
		BeaconBlockConnection *conn = oldGalaxy->bbIncoming.storage[i];

		assert(conn->destBlock == oldGalaxy);

		eaPushUnique(&rebuildList, conn->srcBlock);
	}

	while(eaSize(&procList))
	{
		BeaconBlock* block = eaPop(&procList);

		// Break old link
		i = arrayFindAndRemoveFast(&block->galaxy->subBlockArray, block);
		assert(i != -1);
		
		// Make new link
		block->galaxy = newGalaxy;
		arrayPushBack(&newGalaxy->subBlockArray, block);

		if(maxJumpHeight == 0)
		{
			for(i=0; i<block->gbbConns.size; i++)
			{
				BeaconBlockConnection *conn = block->gbbConns.storage[i];
				BeaconBlock* dest = conn->destBlock;

				assert(!dest->isGridBlock && !dest->isCluster);

				if(conn->blockCount == conn->connCount)
					continue;

				if(dest->galaxy != oldGalaxy)
					continue;

				if(!blockHasGroundConnectionToBlock(dest, block))
					continue;

				assert(dest != unreach);
				eaPush(&procList, dest);
			}
		}
		else
		{
			for(i=0; i<block->rbbConns.size; i++)
			{
				BeaconBlockConnection *conn = block->rbbConns.storage[i];
				BeaconBlock* dest = conn->destBlock;

				assert(!dest->isGridBlock && !dest->isCluster);

				if(conn->blockCount == conn->connCount)
					continue;

				if(conn->minHeight > maxJumpHeight)
					continue;

				if(dest->galaxy != oldGalaxy)
					continue;

				assert(dest != unreach);
				eaPush(&procList, dest);
			}
		}
	}

	FOR_EACH_IN_EARRAY(rebuildList, BeaconBlock, rebuild)
	{
		beaconGalaxyRemakeConnections(rebuild);
	}
	FOR_EACH_END;
}

static void beaconSplitBlock(BeaconStatePartition *partition, BeaconBlock* gridBlock)
{
	Beacon** beaconArray = (Beacon**)gridBlock->beaconArray.storage;
	int beaconsRemaining = gridBlock->beaconArray.size - 1;
	int placedCount = 0;
	Beacon* beacon;
	BeaconBlock* subBlock = NULL;
	int i;
	int useGround = 1;

	// Check if the block has already been split.
	if(gridBlock->subBlockArray.size){
		return;
	}
	
	// Use searchInstance as a temporary index variable for swapping.

	PERFINFO_AUTO_START("sortConns", 1);
		for(i = 0; i < gridBlock->beaconArray.size; i++)
		{
			BeaconPartitionData *beaconPartition = NULL;
			beacon = beaconArray[i];

			beaconPartition = beaconGetPartitionData(beacon, partition->id, true);
			
			beaconPartition->block = gridBlock;
			beacon->searchInstance = i;

			// TODO(AM): Should really be done only on base partition
			if(!beacon->groundConnsSorted){
				beaconSortConnsByTarget(beacon, 0);
			}
		}
	PERFINFO_AUTO_STOP();
		
	tempSubBlocks.size = 0;

	useGround = beaconBlockSubBlockUsesGround();
	
	for(i = 0; i < gridBlock->beaconArray.size; i++){
		int connCount;
		int j;
		BeaconPartitionData *beaconPartition = NULL;

		beacon = beaconArray[i];

		beaconPartition = beaconGetPartitionData(beacon, partition->id, true);
		
		if(beaconPartition->block == gridBlock){
			// Time to start a new subBlock.
			
			if(subBlock){
				beaconBlockInitCopyArray(&subBlock->beaconArray, &tempBeacons);
			}
			
			tempBeacons.size = 0;
		
			subBlock = beaconSubBlockCreate(partition->id);
			subBlock->parentBlock = gridBlock;
			
			arrayPushBack(&tempBeacons, beacon);
			arrayPushBack(&tempSubBlocks, subBlock);
			
			beaconPartition->block = subBlock;
			assert(placedCount == i);
			placedCount++;
		}else{
			assert(!beaconPartition->block->isGridBlock);
			subBlock = beaconPartition->block;
			assert(subBlock->parentBlock == gridBlock);
		}
		
		if(useGround)
		{
			connCount = beacon->gbConns.size;

			for(j = 0; j < connCount; j++){
				BeaconConnection* conn = beacon->gbConns.storage[j];
				Beacon* destBeacon = conn->destBeacon;
				BeaconPartitionData *destBeaconPartition = beaconGetPartitionData(destBeacon, partition->id, true);

				if(	destBeaconPartition->block == gridBlock &&
					beaconHasGroundConnectionToBeacon(destBeacon, beacon, NULL))
				{
					// Swap with first unplaced beacon.

					Beacon* tempBeacon = beaconArray[placedCount];

					assert(destBeacon->searchInstance >= placedCount);
					assert(tempBeacon->searchInstance == placedCount);

					tempBeacon->searchInstance = destBeacon->searchInstance;
					destBeacon->searchInstance = placedCount;

					beaconArray[destBeacon->searchInstance] = destBeacon;
					beaconArray[tempBeacon->searchInstance] = tempBeacon;

					placedCount++;

					// Put destBeacon in the current subBlock.
					destBeaconPartition->block = subBlock;

					arrayPushBack(&tempBeacons, destBeacon);
				}
			}
		}		
		else
		{
			connCount = beacon->rbConns.size;

			for(j=0; j<connCount; j++)
			{
				BeaconConnection* conn = beacon->rbConns.storage[j];
				Beacon* destBeacon = conn->destBeacon;
				BeaconPartitionData *destBeaconPartition = beaconGetPartitionData(destBeacon, partition->id, true);

				if(	destBeaconPartition->block == gridBlock &&
					conn->minHeight < 3 && fabs(vecY(beacon->pos) - vecY(destBeacon->pos))<5 )
				{
					// Swap with first unplaced beacon.

					Beacon* tempBeacon = beaconArray[placedCount];

					assert(destBeacon->searchInstance >= placedCount);
					assert(tempBeacon->searchInstance == placedCount);

					tempBeacon->searchInstance = destBeacon->searchInstance;
					destBeacon->searchInstance = placedCount;

					beaconArray[destBeacon->searchInstance] = destBeacon;
					beaconArray[tempBeacon->searchInstance] = tempBeacon;

					placedCount++;

					// Put destBeacon in the current subBlock.
					destBeaconPartition->block = subBlock;

					arrayPushBack(&tempBeacons, destBeacon);
				}
			}
		}		
	}
	
	if(subBlock){
		beaconBlockInitCopyArray(&subBlock->beaconArray, &tempBeacons);
	}

	beaconBlockInitCopyArray(&gridBlock->subBlockArray, &tempSubBlocks);

	assert(placedCount == gridBlock->beaconArray.size);
	// Find the center position of each sub-block by averaging the positions of all the beacons in it.

	for(i = 0; i < gridBlock->subBlockArray.size; i++)
		beaconSubBlockCalcPos(gridBlock->subBlockArray.storage[i]);
}

void beaconSubBlockCalcPos(BeaconBlock *subBlock)
{
	int i;
	Vec3 pos = {0,0,0};

	for(i = 0; i < subBlock->beaconArray.size; i++)
	{
		Beacon* beacon = subBlock->beaconArray.storage[i];

		addVec3(pos, beacon->pos, pos);
	}

	scaleVec3(pos, 1.0f / subBlock->beaconArray.size, subBlock->pos);
}

F32 beaconMakeGridBlockCoord(F32 xyz){
	return floor(xyz / combatBeaconGridBlockSize);
}

void beaconMakeGridBlockCoords(Vec3 v){
	v[0] = beaconMakeGridBlockCoord(v[0]);
	v[1] = beaconMakeGridBlockCoord(v[1]);
	v[2] = beaconMakeGridBlockCoord(v[2]);
}

int beaconMakeGridBlockHashValue(int x, int y, int z){
	return ((0x3<<30) | ((x&0x3ff)<<20) | ((y&0x3ff)<<10) | (z&0x3ff));
	//sprintf(hashStringOut, "%d,%d,%d", x, y, z);
}

U32 beaconGetBlockData(Beacon* b, int partitionIdx, BeaconBlock **subBlockOut, BeaconBlock **galaxyOut, BeaconBlock **clusterOut)
{
	BeaconPartitionData *partition;
	if(!b)
		return 0;

	partition = beaconGetPartitionData(b, partitionIdx, 0);

	if(!partition)
		return 0;

	if(subBlockOut)
		*subBlockOut = partition->block;
	if(galaxyOut)
		*galaxyOut = partition->block->galaxy;
	if(clusterOut)
		*clusterOut = partition->block->cluster;

	return 1;
}

BeaconBlock* beaconGetGridBlockByCoords(BeaconStatePartition *partition, int x, int y, int z, int create){
	int blockHashValue = beaconMakeGridBlockHashValue(x, y, z);
	BeaconBlock* block;

	if(!partition)
		return NULL;

	if(!stashIntFindPointer(partition->combatBeaconGridBlockTable, blockHashValue, &block) && create)
	{
		block = beaconGridBlockCreate(partition->id);
		
		setVec3(block->pos, x, y, z);
		
		stashIntAddPointer(partition->combatBeaconGridBlockTable, blockHashValue, block, false);
		
		arrayPushBack(&partition->combatBeaconGridBlockArray, block);
	}
	
	return block;
}

BeaconBlockConnection *beaconBlockGetConnection(BeaconBlock *src, BeaconBlock *dst, int raised)
{
	if(!src || !dst)
		return NULL;

	if(raised)
		return findBlockConnection(&src->rbbConns, dst, NULL);
	else
		return findBlockConnection(&src->gbbConns, dst, NULL);
}

static BeaconBlockConnection* beaconBlockGetAnyConnection(BeaconBlock *src, BeaconBlock *dst)
{
	BeaconBlockConnection *conn = NULL;
	if(!src || !dst)
		return NULL;

	conn = findBlockConnection(&src->gbbConns, dst, NULL);
	if(conn)
		return conn;

	return findBlockConnection(&src->rbbConns, dst, NULL);
}

static Array staticTempGroundConns;
static Array staticTempRaisedConns;

void beaconSubBlockRemakeConnections(BeaconBlock *subBlock)
{
	PERFINFO_AUTO_START_FUNC();

	assert(subBlock->isSubBlock);

	beaconSubBlockDestroyConnections(subBlock);
	beaconSubBlockMakeConnections(subBlock, subBlock->partitionIdx);

	PERFINFO_AUTO_STOP();
}

void beaconSubBlockDestroyConnections(BeaconBlock *subBlock)
{
	PERFINFO_AUTO_START_FUNC();

	assert(subBlock->isSubBlock);
	destroyArrayPartialEx(&subBlock->gbbConns, beaconBlockConnectionDestroy);
	destroyArrayPartialEx(&subBlock->rbbConns, beaconBlockConnectionDestroy);

	PERFINFO_AUTO_STOP();
}

void beaconSubBlockMakeConnections(BeaconBlock *subBlock, int partitionId)
{
	Beacon**		beacons = (Beacon**)subBlock->beaconArray.storage;
	int				beaconCount = subBlock->beaconArray.size;
	int				k;

	PERFINFO_AUTO_START_FUNC();

	assert(subBlock->isSubBlock);

	staticTempGroundConns.size = staticTempRaisedConns.size = 0;

	for(k = 0; k < beaconCount; k++)
	{
		Beacon*					beacon = beacons[k];
		BeaconConnection*		beaconConn;
		BeaconBlock*			destBlock;
		BeaconBlockConnection*	blockConn;
		int 					newIndex;
		int 					j;

		// Check the ground connections.

		for(j = 0; j < beacon->gbConns.size; j++)
		{
			BeaconPartitionData *destPartition;
			beaconConn = beacon->gbConns.storage[j];

			destPartition = beaconGetPartitionData(beaconConn->destBeacon, partitionId, true);
			destBlock = destPartition->block;

			assert(!destBlock->isGridBlock);

			if(destBlock == subBlock)
				continue;

			// Look for an existing connection to destBlock.
			blockConn = findBlockConnection(&staticTempGroundConns, destBlock, &newIndex);

			if(!blockConn)
			{
				// New connection.
				assert(newIndex >= 0 && newIndex <= staticTempGroundConns.size);

				blockConn = beaconBlockConnectionCreate(subBlock, destBlock);

				arrayInsert(&staticTempGroundConns, newIndex, blockConn);
			}

			blockConn->connCount++;
			if(beaconConnectionIsDisabled(beacon, partitionId, beaconConn))
				blockConn->blockCount++;
		}

		// Check the raised connections.

		for(j = 0; j < beacon->rbConns.size; j++)
		{
			BeaconPartitionData *destPartition;
			S32 disabled;

			beaconConn = beacon->rbConns.storage[j];
			destPartition = beaconGetPartitionData(beaconConn->destBeacon, partitionId, true);
			destBlock = destPartition->block;

			assert(!destBlock->isGridBlock);

			if(destBlock == subBlock)
				continue;

			// Look for an existing connection to destBlock
			blockConn = findBlockConnection(&staticTempRaisedConns, destBlock, &newIndex);
			if(!blockConn)
			{
				// New connection.
				assert(newIndex >= 0 && newIndex <= staticTempRaisedConns.size);

				blockConn = beaconBlockConnectionCreate(subBlock, destBlock);
				blockConn->raised = true;

				arrayInsert(&staticTempRaisedConns, newIndex, blockConn);
			}
			
			disabled = beaconConnectionIsDisabled(beacon, partitionId, beaconConn);
			beaconRaisedBlockConnectionAddBeaconConnection(partitionId, blockConn, beaconConn, disabled);

			blockConn->connCount++;
			//assert(eaFind(&blockConn->conns, beaconConn) == -1);
			eaPush(&blockConn->conns, beaconConn);
			if(disabled)
				blockConn->blockCount++;
		}
	}

	// Copy the connections into permanent storage.
	beaconBlockInitCopyArray(&subBlock->gbbConns, &staticTempGroundConns);
	beaconBlockInitCopyArray(&subBlock->rbbConns, &staticTempRaisedConns);

	subBlock->madeConnections = true;

	PERFINFO_AUTO_STOP();
}

void beaconGridBlockMakeBlockConnections(BeaconStatePartition *partition, BeaconBlock* gridBlock){
	int subBlockCount = gridBlock->subBlockArray.size;
	BeaconBlock** subBlocks = (BeaconBlock**)gridBlock->subBlockArray.storage;
	int i;
	
	for(i = 0; i < subBlockCount; i++)
	{
		beaconSubBlockMakeConnections(subBlocks[i], partition->id);
	}
}

void* beaconAllocateMemory(U32 size){
	void* mem = malloc(size);
	
	if(!mem){
		assertmsg(0, "Beaconizer out of memory.");
		return NULL;
	}
	
	memset(mem, 0, size);
	
	beacon_process.memoryAllocated += size;
	
	return mem;
}

static struct {
	Array	array;
	F32		minRadiusSQR;
	F32		maxRadiusSQR;
} compileNearbyData;

static void beaconCompileNearbyBeaconsHelper(Array* beaconArray, void* userData){
	Beacon* source = compileNearbyData.array.storage[0];
	int i;
	
	for(i = 0; i < beaconArray->size; i++){
		Beacon* target = beaconArray->storage[i];
		F32 distSQR;

		if(target == source)
			continue;	
			
		distSQR = distance3SquaredXZ(source->pos, target->pos);

		if(distSQR >= compileNearbyData.minRadiusSQR && distSQR < compileNearbyData.maxRadiusSQR){
			if(compileNearbyData.array.size < compileNearbyData.array.maxSize){
				compileNearbyData.array.storage[compileNearbyData.array.size++] = target;
			}
		}
	}
}

static Array* beaconCompileNearbyBeacons(Beacon* source, F32 minRadius, F32 maxRadius){
	static Beacon** tempBeaconBuffer;
	static Array tempArray;

	if(!tempBeaconBuffer){
		tempBeaconBuffer = calloc(sizeof(*tempBeaconBuffer), 10000);
		
		if(!tempBeaconBuffer){
			tempArray.size = 0;
			return &tempArray;
		}
		
		tempArray.maxSize = 9999;
		tempArray.storage = tempBeaconBuffer + 1;
	}
	
	compileNearbyData.array.size = 1;
	compileNearbyData.array.maxSize = 10000;
	compileNearbyData.array.storage = tempBeaconBuffer;
	compileNearbyData.array.storage[0] = source;
	compileNearbyData.minRadiusSQR = SQR(minRadius);
	compileNearbyData.maxRadiusSQR = SQR(maxRadius);
	
	beaconForEachBlock(beaconStatePartitionGet(0, false), source->pos, 100, 5000, 100, beaconCompileNearbyBeaconsHelper, NULL);
	
	tempArray.size = compileNearbyData.array.size - 1;

	return &tempArray;
}

static int __cdecl compareBeaconProcessConnectionDistanceXZ(const BeaconProcessConnection* b1, const BeaconProcessConnection* b2){
	if(b1->distanceXZ > b2->distanceXZ){
		return 1;
	}
	else if(b1->distanceXZ < b2->distanceXZ){
		return -1;
	}
	else{
		int index1 = b1->targetIndex;
		int index2 = b2->targetIndex;

		if(index1 > index2)
			return 1;
		else if(index1 < index2)
			return -1;
	}

	assert(0);

	return 0;
}

static void beaconCreateBeaconProcessInfo(Beacon* source){
	BeaconProcessInfo* info = beacon_process.infoArray + source->globalIndex;
	BeaconProcessConnection* conns;
	int connCount;
	Array* beaconArray;
	int i;
	
	beaconArray = beaconCompileNearbyBeacons(source, 0, 100);
	
	info->beaconCount = connCount = beaconArray->size;
	info->beacons = conns = beaconAllocateMemory(connCount * sizeof(*info->beacons));

	//printf("allocating: %d nearby beacons = %d bytes\n", beaconCount, beaconCount * sizeof(*info->beacons));
	
	for(i = 0; i < connCount; i++){
		Vec3 diff;
		float yaw;
		Beacon* target = beaconArray->storage[i + 1];
		
		conns[i].targetIndex = target->globalIndex;

		// Calculate the distance from source.
		
		conns[i].distanceXZ = floor(distance3XZ(source->pos, target->pos) + 0.5);
		
		// Calculate the yaw from source.
		
		subVec3(target->pos, source->pos, diff);
		
		yaw = getVec3Yaw(diff);
		
		conns[i].yaw90 = floor(DEG(yaw) / 2 + 0.5);
	}
	
	// Sort the list by distance ascending.
	
	qsort(conns, connCount, sizeof(*conns), compareBeaconProcessConnectionDistanceXZ);
}

static void beaconCreateRaisedConnections(	int iPartitionIdx,
											Beacon* source,
											BeaconProcessConnection* conn,
											F32 openMin,
											F32 openMax,
											F32 ignoreMin,
											F32 ignoreMax)
{
	BeaconProcessRaisedConnection tempRaisedConns[1000];
	int raisedCount = 0;
	float myMin, myMax;
	Beacon* target = combatBeaconArray.storage[conn->targetIndex];
	int i;
		
	PERFINFO_AUTO_START("getPassableHeight", 1);
	
		if(1){
			// Create the lower raised connections.
				
			myMin = vecY(source->pos) + 0.1;
			myMin = max(myMin, openMin);
			myMax = ignoreMin;
			
			while(	raisedCount < ARRAY_SIZE(tempRaisedConns) &&
					beaconGetPassableHeight(iPartitionIdx, source, target, &myMax, &myMin))
			{
				devassert(myMin>=0);
				devassert(myMin<65535);
				devassert(myMax>=0);
				devassert(myMax<65535);
				tempRaisedConns[raisedCount].minHeight = vecY(source->pos) + myMin;
				tempRaisedConns[raisedCount].maxHeight = vecY(source->pos) + myMax;

				raisedCount++;

				myMin = vecY(source->pos) + myMax;
				myMax = ignoreMin;
			}

			if(ignoreMax > ignoreMin){
				F32 highMax = min(openMax, FLT_MAX);
				
				// Create the higher raised connections.
				
				myMin = ignoreMax;
				myMax = highMax;

				while(	raisedCount < ARRAY_SIZE(tempRaisedConns) &&
						beaconGetPassableHeight(iPartitionIdx, source, target, &myMax, &myMin))
				{
					devassert(myMin>=0);
					devassert(myMin<65535);
					devassert(myMax>=0);
					devassert(myMax<65535);
					tempRaisedConns[raisedCount].minHeight = vecY(source->pos) + myMin;
					tempRaisedConns[raisedCount].maxHeight = vecY(source->pos) + myMax;

					raisedCount++;

					myMin = vecY(source->pos) + myMax;
					myMax = highMax;
				}
			}
		}
				
		conn->minHeight = FLT_MAX;
		conn->maxHeight = -FLT_MAX;
		
		if(raisedCount){
			int buffer_size = raisedCount * sizeof(*tempRaisedConns);
			
			conn->raisedConns = beaconAllocateMemory(buffer_size);
			
			conn->raisedCount = raisedCount;

			memcpy(conn->raisedConns, tempRaisedConns, buffer_size);
		
			for(i = 0; i < raisedCount; i++){
				conn->minHeight = min(conn->minHeight, tempRaisedConns[i].minHeight);
				conn->maxHeight = max(conn->maxHeight, tempRaisedConns[i].maxHeight);
			}
		}else{
			conn->raisedConns = NULL;
			conn->raisedCount = 0;
		}
	PERFINFO_AUTO_STOP();
}

static void beaconMakeBeaconLegal(Beacon* beacon){
	U32 legalBeaconIndex = beacon->globalIndex;
	BeaconProcessInfo* info = beacon_process.infoArray + legalBeaconIndex;
	
	if(info->isLegal){
		return;
	}
	
	info->isLegal = 1;
	
	assert(beacon_process.legalCount < combatBeaconArray.size);
	
	if(beacon_process.processOrder){
		if(info->diskSwapBlock == beacon_process.curDiskSwapBlock){
			// Put anything that's in the current BeaconDiskSwapBlock at the front of the process array.
		
			beacon_process.processOrder[beacon_process.legalCount++] = beacon_process.processOrder[beacon_process.nextDiskSwapBlockIndex];
			beacon_process.processOrder[beacon_process.nextDiskSwapBlockIndex++] = legalBeaconIndex;
			
			assert(beacon_process.nextDiskSwapBlockIndex <= beacon_process.legalCount);
		}else{
			beacon_process.processOrder[beacon_process.legalCount++] = legalBeaconIndex;
		}
	}
}

static U32 totalConnectionCount;
static U32 checkedConnectionCount;

static int __cdecl compareBeaconDistanceXZ(const Beacon** b1Param, const Beacon** b2Param){
	const Beacon* b1 = *b1Param;
	const Beacon* b2 = *b2Param;
	
	if(b1->userFloatBcnizer > b2->userFloatBcnizer){
		return 1;
	}
	else if(b1->userFloatBcnizer < b2->userFloatBcnizer){
		return -1;
	}

	return 0;
}

#define BEACON_NUM_GROUPS 30

static struct {
	BeaconStatePartition *partition;
	int partitionIdx;
	Beacon*	source;
	Array	group[BEACON_NUM_GROUPS];
} compileNearbyGroupsData;

#define BEACON_SPACE_EXTEND_RADIUS BEACON_NUM_GROUPS*20

static void beaconCompileNearbyBeaconGroupsHelper(Array* beaconArray, void* userData){
	Beacon* source = compileNearbyGroupsData.source;
	int i;
	F32 maxRadius = 100;
	BeaconPartitionData *sourcePartition = beaconGetPartitionData(source, 0, true);

	if(!stashAddressFindInt(beaconGeoProximityStash, sourcePartition->block, NULL))
		maxRadius = 200;
	
	for(i = 0; i < beaconArray->size; i++){
		Beacon* target = beaconArray->storage[i];
		F32 dist;
		int index;

		if(target == source)
			continue;	
			
		dist = distance3XZ(source->pos, target->pos);

		if(dist>maxRadius)
			continue;
		
		index = dist / 10;

		if(index >= 0 && index < ARRAY_SIZE(compileNearbyGroupsData.group)){
			arrayPushBack(&compileNearbyGroupsData.group[index], target);

			target->userFloatBcnizer = dist;
		}
	}
}

static void beaconCompileNearbyBeaconGroups(Beacon* source){
	int i;
	F32 radius = 100;
	BeaconPartitionData *sourcePartition = beaconGetPartitionData(source, 0, true);
	
	compileNearbyGroupsData.source = source;
	
	for(i = 0; i < ARRAY_SIZE(compileNearbyGroupsData.group); i++){
		compileNearbyGroupsData.group[i].size = 0;
	}

	if(!stashAddressFindInt(beaconGeoProximityStash, sourcePartition->block, NULL))
		radius = 200;

	beaconForEachBlock(beaconStatePartitionGet(0, false), source->pos, radius, 15000, radius, beaconCompileNearbyBeaconGroupsHelper, NULL);
}

static S32 beaconProcessAngleFromBeacon(BeaconProcessInfo* info, Beacon *source, Beacon* b)
{
	Vec3 diff;
	int yaw90;
	S32 angleIncrement = BEACONCONFIGVAR(angleProcessIncrement);
	S32 angleIncrementCount = 360/angleIncrement;

	subVec3(b->pos, source->pos, diff);

	yaw90 = angleIncrementCount/2 + floor(DEG(getVec3Yaw(diff)) / angleIncrement + 0.5);
	yaw90 = (yaw90 + angleIncrementCount) % (angleIncrementCount);
	assert(yaw90 >= 0 && yaw90 < angleIncrementCount);

	return yaw90;
}

static void beaconProcessAngleCompleted(BeaconProcessInfo* info, BeaconProcessAngleInfo *refInfo, Beacon *b, S32 yaw, S32 delta, S32 angleIncrementCount, S32 ground)
{
	S32 offset;
	for(offset = -delta; offset <= delta; offset++){
		int angle90 = (yaw + offset + angleIncrementCount) % angleIncrementCount;
		BeaconProcessAngleInfo* offsetInfo = info->angleInfo->angle + angle90;

		if(ground)
		{
			if(refInfo->posReachedDistXZ > offsetInfo->posReachedDistXZ){
				offsetInfo->posReachedDistXZ = refInfo->posReachedDistXZ;
				copyVec3(refInfo->posReached, offsetInfo->posReached);
			}

			offsetInfo->handledForGround = 1;
		}
		else
		{
			if(refInfo->handledForRaised)
				offsetInfo->handledForRaised = 1;
			else
			{
				if(offsetInfo->ignoreMin == 0 && offsetInfo->ignoreMax == 0){
					offsetInfo->ignoreMin = FLT_MAX;
					offsetInfo->ignoreMax = -FLT_MAX;
					offsetInfo->openMin = -FLT_MAX;
					offsetInfo->openMax = FLT_MAX;
				}

				if(refInfo->ignoreMin < offsetInfo->ignoreMin)
					offsetInfo->ignoreMin = refInfo->ignoreMin;
				if(refInfo->ignoreMax > offsetInfo->ignoreMax)
					offsetInfo->ignoreMax = refInfo->ignoreMax;

				if(refInfo->openMin > offsetInfo->openMin)
					offsetInfo->openMin = refInfo->openMin;
				if(refInfo->openMax < offsetInfo->openMax)
					offsetInfo->openMax = refInfo->openMax;
			}
		}

		if(offsetInfo->handledForGround && offsetInfo->handledForRaised)
		{
			offsetInfo->done = 1;
			info->angleInfo->completedCount++;
		}
	}
}

void beaconProcessCombatBeacon(int iPartitionIdx, Beacon* source, S64 *groundTicks, S64 *raisedTicks){
	static struct {
		BeaconProcessConnection* buffer;
		int maxCount;
	} tempConns;
	
	F32 sourceCeilingHeight = beaconGetCeilingDistance(iPartitionIdx, source);
	F32 sourceFloorHeight = beaconGetFloorDistance(iPartitionIdx, source);
	BeaconProcessInfo* info = beacon_process.infoArray + source->globalIndex;
	BeaconProcessInfo tempInfo;
	int tempConnsCount = 0;
	int i;
	S32 angleIncrement = BEACONCONFIGVAR(angleProcessIncrement);
	S32 angleIncrementCount = 360/angleIncrement;
	int fill_angle_delta = 7*ANGLE_INCREMENT_MIN/angleIncrement;

	if(beacon_client.debug_state &&
		!vec3IsZero(beacon_client.debug_state->debug_pos) &&
		distance3Squared(source->pos, beacon_client.debug_state->debug_pos) < 5)
	{
		printf("");
	}

	MAX1(fill_angle_delta, 1);
	
	if(!info){
		ZeroStruct(&tempInfo);
		info = &tempInfo;
	}
	
	if(!info->angleInfo){
		info->angleInfo = beaconAllocateMemory(sizeof(*info->angleInfo));

		devassert(angleIncrementCount<=ARRAY_SIZE(info->angleInfo->angle));	
		for(i = 0; i < angleIncrementCount; i++){
			copyVec3(source->pos, info->angleInfo->angle[i].posReached);
			vecY(info->angleInfo->angle[i].posReached) -= sourceFloorHeight - 0.1;
		}
	}
	
	beaconCompileNearbyBeaconGroups(source);

	for(i=0; i<source->gbConns.size; i++)
	{
		BeaconConnection *conn = source->gbConns.storage[i];
		Beacon* target = conn->destBeacon;
		S32 angleIndex = beaconProcessAngleFromBeacon(info, source, target);
		BeaconProcessAngleInfo *angleInfo = &info->angleInfo->angle[angleIndex];

		angleInfo->handledForGround = true;

		beaconProcessAngleCompleted(info, angleInfo, source, angleIndex, fill_angle_delta, angleIncrementCount, true);
	}

	for(i=0; i<source->rbConns.size; i++)
	{
		BeaconConnection *conn = source->rbConns.storage[i];
		Beacon* target = conn->destBeacon;
		S32 angleIndex = beaconProcessAngleFromBeacon(info, source, target);
		BeaconProcessAngleInfo *angleInfo = &info->angleInfo->angle[angleIndex];

		angleInfo->handledForRaised = true;

		beaconProcessAngleCompleted(info, angleInfo, source, angleIndex, fill_angle_delta, angleIncrementCount, false);
	}
	
	// Keep finding stuff until empty.	
	while(	info->angleInfo->curGroup < ARRAY_SIZE(compileNearbyGroupsData.group) &&
			info->angleInfo->completedCount < angleIncrementCount)
	{
		Array* beaconArray = &compileNearbyGroupsData.group[info->angleInfo->curGroup++];
		int checkedCount = 0;
		int validCount = 0;
		
		// Don't need to qsort again, since they should still be in qsort order
		for(i = 0; i < beaconArray->size; i++){
			Beacon *target = beaconArray->storage[i];
			S32 angleIndex = beaconProcessAngleFromBeacon(info, source, target);
			BeaconProcessAngleInfo *angleInfo = &info->angleInfo->angle[angleIndex];

			if(!angleInfo->done){
				beaconArray->storage[validCount++] = target;
				target->userFloatBcnizer = angleIndex;
			}			
		}
		
		if(!validCount)
			continue;
		
		beaconArray->size = validCount;

		// For each nearby beacon, find anything closer that connects, then check if that beacon connects.

		for(i = 0; i < beaconArray->size; i++){
			BeaconProcessAngleInfo* angleInfo;
			Beacon* target = beaconArray->storage[i];
			BeaconProcessConnection conn = {0};
			int yaw90 = target->userFloatBcnizer;
			F32 targetCeilingHeight = beaconGetCeilingDistance(iPartitionIdx, target);
			F32 minCeilingHeight = min(sourceCeilingHeight, targetCeilingHeight);
			S64 start, end;
			
			conn.targetIndex = target->globalIndex;
			conn.makeGroundConnection = 0;
			conn.raisedCount = 0;

			angleInfo = info->angleInfo->angle + yaw90;
			
			if(angleInfo->done)
				continue;
				
			checkedCount++;
			
			if(!angleInfo->handledForGround){
				int connectsByGround;

				start = timerCpuTicks64();
				
				// A connection doesn't exist yet, so figure out if target is reachable.
				
				usedPhysicsSteps = 0;
				
				if(source->noGroundConnections){
					copyVec3(target->pos, beacon_process.entityPos);
					connectsByGround = 0;
					last_walk_result = WALKRESULT_NO_LOS;
				}
				else if(target->noGroundConnections){
					copyVec3(angleInfo->posReached, beacon_process.entityPos);
					connectsByGround = 0;
					last_walk_result = WALKRESULT_NO_LOS;
				}
				else{
					int optional = 0;
					int bidir = 0;
					PERFINFO_AUTO_START("connectsByGround", 1);
						connectsByGround = beaconConnectsToBeaconByGround(	iPartitionIdx,
																			source,
																			target,
																			angleInfo->posReached,
																			min(minCeilingHeight, 9),
																			NULL,
																			1,
																			&optional,
																			&bidir);
						conn.optionalWalkCheck = optional;
						conn.bidirWalkCheck = bidir;
					PERFINFO_AUTO_STOP();
				}
						
				if(last_walk_result==BEACON_WALK_SUCCESS)
				{
					copyVec3(beacon_process.entityPos, angleInfo->posReached);
					angleInfo->posReachedDistXZ = distance3SquaredXZ(source->pos, angleInfo->posReached);
				}
				
				if(	connectsByGround )
				{
					int cur_delta = connectsByGround ? fill_angle_delta : 1;
					
					if(connectsByGround){
						conn.makeGroundConnection = 1;
						beaconMakeBeaconLegal(target);
					}

					beaconProcessAngleCompleted(info, angleInfo, source, yaw90, cur_delta, angleIncrementCount, true);
				}
				
				if(usedPhysicsSteps){
					info->stats.physicsSteps += usedPhysicsSteps;
					info->stats.walkedConnections++;
					info->stats.totalDistance += distance3(source->pos, target->pos);
				}

				end = timerCpuTicks64();

				*groundTicks += end - start;
			}
			
			if(!angleInfo->handledForRaised){
				int fill_angles = 0;

				start = timerCpuTicks64();
				
				if(angleInfo->ignoreMin == 0 && angleInfo->ignoreMax == 0){
					angleInfo->ignoreMin = FLT_MAX;
					angleInfo->ignoreMax = -FLT_MAX;
					angleInfo->openMin = vecY(source->pos) + 0.1;
					angleInfo->openMax = vecY(source->pos) + sourceCeilingHeight;
				}
				beaconCreateRaisedConnections(	iPartitionIdx,
												source,
												&conn,
												angleInfo->openMin,
												angleInfo->openMax,
												angleInfo->ignoreMin,
												angleInfo->ignoreMax);
				
				if(conn.raisedCount){
					beaconMakeBeaconLegal(target);

					fill_angles = 1;
					
					if(conn.minHeight < angleInfo->ignoreMin)
						angleInfo->ignoreMin = conn.minHeight;
					if(conn.maxHeight > angleInfo->ignoreMax)
						angleInfo->ignoreMax = conn.maxHeight;
				}
				
				if(vecY(target->pos) + targetCeilingHeight > angleInfo->openMax - 3){
					if(vecY(target->pos) < angleInfo->openMax)
						angleInfo->openMax = vecY(target->pos);
				}
				
				if(vecY(target->pos) < angleInfo->openMin + 3){
					if(vecY(target->pos) + targetCeilingHeight > angleInfo->openMin){
						angleInfo->openMin = vecY(target->pos) + targetCeilingHeight;
					}
				}
				
				if(	angleInfo->ignoreMin < vecY(source->pos) + 6 &&
					angleInfo->ignoreMax > vecY(source->pos) + sourceCeilingHeight - 6)
				{
					if(!angleInfo->handledForRaised){
						angleInfo->handledForRaised = 1;
						
						if(angleInfo->handledForGround){
							// This angle is done.

							angleInfo->done = 1;
							info->angleInfo->completedCount++;
						}
					}
				}

				if(fill_angles){
					beaconProcessAngleCompleted(info, angleInfo, source, yaw90, fill_angle_delta, angleIncrementCount, false);
				}

				end = timerCpuTicks64();

				*raisedTicks += end - start;
			}
			
			if(conn.makeGroundConnection || conn.raisedCount){
				BeaconProcessConnection* newConn;
				
				newConn = dynArrayAddStruct(tempConns.buffer,
											tempConnsCount,
											tempConns.maxCount);
										
				newConn->targetIndex = conn.targetIndex;
				newConn->reachedByGround = conn.makeGroundConnection;
				newConn->reachedByRaised = conn.raisedCount ? 1 : 0;
				newConn->reachedBySomething = newConn->reachedByGround || newConn->reachedByRaised;
				newConn->makeGroundConnection = conn.makeGroundConnection;
				newConn->raisedCount = conn.raisedCount;
				newConn->raisedConns = conn.raisedConns;
				newConn->optionalWalkCheck = conn.optionalWalkCheck;
				newConn->bidirWalkCheck = conn.bidirWalkCheck;

				assert(	tempConnsCount < 2 ||
						tempConns.buffer[tempConnsCount-1].targetIndex != tempConns.buffer[tempConnsCount-2].targetIndex);
			}
		}
	}
	
	SAFE_FREE(info->angleInfo);
	
	// Consolidate all connections into the exact-sized array.	
	PERFINFO_AUTO_START("consolidate", 1);
	
		if(tempConnsCount){
			BeaconProcessConnection* newConns;
			int bufferSize = tempConnsCount * sizeof(*newConns);
			int j = 0;
			
			newConns = beaconAllocateMemory(bufferSize);

			memcpy(newConns, tempConns.buffer, bufferSize);

			SAFE_FREE(info->beacons);
			
			info->beacons = newConns;
			info->beaconCount = tempConnsCount;
		}
	
	PERFINFO_AUTO_STOP();
}

static int blockHasRaisedConnectionToBlock(BeaconBlock* source, BeaconBlock* target, F32 maxJumpHeight){
	BeaconBlockConnection* conn = findBlockConnection(&source->rbbConns, target, NULL);
	
	return conn && conn->minJumpHeight <= maxJumpHeight;
}

static int staticPropagateGalaxyCount;
static int staticSubBlockCount = 0;
static Array tempGalaxySubBlocks;

static void beaconPropagateGalaxy(BeaconBlock* subBlock, BeaconBlock* galaxy, int quiet){
	int i;
	static BeaconBlock **galaxyBlocks = NULL;
	BeaconBlock* block = NULL;
	S32 useGround = 1;
	
	eaClear(&galaxyBlocks);
	eaPush(&galaxyBlocks, galaxy);

	if(allowRaisedSubBlocks && zmapInfoHasSpaceRegion(NULL))
		useGround = 0;

	while(block = eaPop(&galaxyBlocks))
	{
		if(subBlock->galaxy)
			continue;

		subBlock->galaxy = galaxy;
		arrayPushBack(&tempGalaxySubBlocks, subBlock);

		if(!quiet){
			printf(".");
			beaconProcessSetTitle(100.0 * staticPropagateGalaxyCount++ / staticSubBlockCount, NULL);
		}

		if(useGround)
		{
			for(i = 0; i < subBlock->gbbConns.size; i++)
			{
				BeaconBlockConnection* conn = subBlock->gbbConns.storage[i];
				BeaconBlock* destBlock = conn->destBlock;

				if(!destBlock->galaxy && blockHasGroundConnectionToBlock(destBlock, subBlock))
					eaPush(&galaxyBlocks, destBlock);				
			}
		}
		else
		{
			for(i = 0; i < subBlock->rbbConns.size; i++)
			{
				BeaconBlockConnection* conn = subBlock->rbbConns.storage[i];
				BeaconBlock* destBlock = conn->destBlock;

				if(!destBlock->galaxy && blockHasRaisedConnectionToBlock(destBlock, subBlock, 5))
					eaPush(&galaxyBlocks, destBlock);	
			}
		}
	}
}

static int getConnectedGalaxyCount(BeaconBlock* galaxy){
	int i;
	int count = 1;
	
	galaxy->searchInstance = 1;
	
	for(i = 0; i < galaxy->gbbConns.size; i++){
		BeaconBlockConnection* conn = galaxy->gbbConns.storage[i];
		
		if(!conn->destBlock->searchInstance){
			count += getConnectedGalaxyCount(conn->destBlock);
		}
	}
	
	return count;
}

static Array tempGalaxyArray;

static void beaconPropagateGalaxies(BeaconStatePartition *partition, int quiet)
{
	int i;
	
	if(!quiet)
		beaconProcessSetTitle(0, "Galaxize");
	
	tempGalaxyArray.size = 0;

	for(i = 0; i < partition->combatBeaconGridBlockArray.size; i++)
	{
		BeaconBlock* gridBlock = partition->combatBeaconGridBlockArray.storage[i];
		int j;
		
		for(j = 0; j < gridBlock->subBlockArray.size; j++)
		{
			BeaconBlock* subBlock = gridBlock->subBlockArray.storage[j];

			if(!subBlock->galaxy)
			{
				BeaconBlock* galaxy = beaconGalaxyCreate(partition->id, 0);
				
				tempGalaxySubBlocks.size = 0;
				
				beaconPropagateGalaxy(subBlock, galaxy, quiet);
				
				beaconBlockInitCopyArray(&galaxy->subBlockArray, &tempGalaxySubBlocks);

				arrayPushBack(&tempGalaxyArray, galaxy);
			}
		}
	}
	
	beaconBlockInitCopyArray(&partition->combatBeaconGalaxyArray[0], &tempGalaxyArray);

	if(!quiet)
		beaconProcessSetTitle(100, NULL);
}

static void beaconGalaxyDestroyConnections(BeaconBlock *galaxy)
{
	PERFINFO_AUTO_START_FUNC();

	assert(galaxy->isGalaxy);
	destroyArrayPartialEx(&galaxy->gbbConns, beaconBlockConnectionDestroy);
	destroyArrayPartialEx(&galaxy->rbbConns, beaconBlockConnectionDestroy);

	PERFINFO_AUTO_STOP();
}

static void beaconMakeGalaxyConnections(BeaconBlock* galaxy)
{
	int i, j;

	PERFINFO_AUTO_START_FUNC();

	staticTempGroundConns.size = staticTempRaisedConns.size = 0;

	for(i = 0; i < galaxy->subBlockArray.size; i++)
	{
		BeaconBlock*			subBlock = galaxy->subBlockArray.storage[i];
		BeaconBlockConnection*	destConn;
		BeaconBlock*			destGalaxy;
		BeaconBlockConnection*	galaxyConn;
		int						newIndex;

		// Add the ground connections.

		for(j = 0; j < subBlock->gbbConns.size; j++)
		{
			destConn = subBlock->gbbConns.storage[j];
			destGalaxy = destConn->destBlock->galaxy;

			assert(destGalaxy->isGalaxy);

			if(destGalaxy == galaxy)
				continue;

			// Look for an existing connection.

			galaxyConn = findBlockConnection(&staticTempGroundConns, destGalaxy, &newIndex);

			if(!galaxyConn)
			{
				// New connection.

				assert(newIndex >= 0 && newIndex <= staticTempGroundConns.size);

				galaxyConn = beaconBlockConnectionCreate(galaxy, destGalaxy);

				arrayInsert(&staticTempGroundConns, newIndex, galaxyConn);
			}

			beaconBlockConnectionAddGalaxyConnection(galaxy->partitionIdx, galaxyConn, destConn);

			galaxyConn->connCount++;
			if(destConn->connCount == destConn->blockCount)
				galaxyConn->blockCount++;
		}

		// Add the raised connections.

		for(j = 0; j < subBlock->rbbConns.size; j++)
		{
			destConn = subBlock->rbbConns.storage[j];
			destGalaxy = destConn->destBlock->galaxy;

			if(destGalaxy == galaxy)
				continue;

			if (!destGalaxy->isGalaxy)
			{
				printf("destGalaxy is not a galaxy\n");
				assert(destGalaxy->isGalaxy);
			}

			// Look for an existing connection.

			galaxyConn = findBlockConnection(&staticTempRaisedConns, destGalaxy, &newIndex);
			if(!galaxyConn)
			{
				galaxyConn = beaconBlockConnectionCreate(galaxy, destGalaxy);
				galaxyConn->raised = true;
				arrayInsert(&staticTempRaisedConns, newIndex, galaxyConn);
			}

			beaconBlockConnectionAddGalaxyConnection(galaxy->partitionIdx, galaxyConn, destConn);
			eaPushUnique(&galaxyConn->conns, destConn);

			galaxyConn->connCount++;
			if(destConn->connCount == destConn->blockCount)
				galaxyConn->blockCount++;
		}
	}

	beaconBlockInitCopyArray(&galaxy->gbbConns, &staticTempGroundConns);
	beaconBlockInitCopyArray(&galaxy->rbbConns, &staticTempRaisedConns);

	galaxy->madeConnections = true;

	PERFINFO_AUTO_STOP();
}

static void beaconGalaxyRemakeConnections(BeaconBlock *galaxy)
{
	PERFINFO_AUTO_START_FUNC();

	assert(galaxy->isGalaxy);
	beaconGalaxyDestroyConnections(galaxy);
	beaconMakeGalaxyConnections(galaxy);

	PERFINFO_AUTO_STOP();
}

static void beaconMakeAllGalaxyConnections(BeaconStatePartition *partition, int galaxySet, int quiet){
	int i;

	PERFINFO_AUTO_START_FUNC();
	
	if(!quiet)
		beaconProcessSetTitle(0, "GalaxyConn");
	
	for(i = 0; i < partition->combatBeaconGalaxyArray[galaxySet].size; i++)
	{
		BeaconBlock* galaxy = partition->combatBeaconGalaxyArray[galaxySet].storage[i];
		
		if(!quiet)
			beaconProcessSetTitle(100.0 * i / partition->combatBeaconGalaxyArray[galaxySet].size, NULL);

		beaconMakeGalaxyConnections(galaxy);
	}

	if(!quiet)
		beaconProcessSetTitle(100, NULL);

	PERFINFO_AUTO_STOP();
}

static void beaconCreateGalaxies(BeaconStatePartition *partition, int quiet){
	int i, j;
	int groundConnCount = 0;
	int raisedConnCount = 0;

	// Count the subBlocks and clear galaxies.
	
	staticSubBlockCount = 0;
	
	for(i = 0; i < partition->combatBeaconGridBlockArray.size; i++){
		BeaconBlock* gridBlock = partition->combatBeaconGridBlockArray.storage[i];
		
		staticSubBlockCount += gridBlock->subBlockArray.size;

		for(j = 0; j < gridBlock->subBlockArray.size; j++){
			BeaconBlock* subBlock = gridBlock->subBlockArray.storage[j];

			subBlock->galaxy = NULL;
		}
	}
	
	// Propagate the galaxies.
	
	PERFINFO_AUTO_START("propagate", 1);
		beaconPropagateGalaxies(partition, quiet);
	PERFINFO_AUTO_STOP();

	// Create galaxy connections.

	PERFINFO_AUTO_START("connect", 1);
		beaconMakeAllGalaxyConnections(partition, 0, quiet);
	PERFINFO_AUTO_STOP();
	
	// Print some stats.

	if(!quiet){
		printf(	"DONE!\n\n"
				"  Galaxies:     %d\n"
				"  Blocks:       %d\n"
				"  Ground conns: %d\n"
				"  Raised conns: %d\n\n",
				partition->combatBeaconGalaxyArray[0].size,
				staticSubBlockCount,
				groundConnCount,
				raisedConnCount);
	}
}

static void beaconPropagateGalaxyGroup(BeaconBlock* childGalaxy, BeaconBlock* parentGalaxy, F32 maxJumpHeight){
	BeaconBlock **children = NULL;
	BeaconBlockConnection* conn;
	BeaconBlock* destGalaxy, *tempGalaxy;
	int i;
	
	eaPush(&children, childGalaxy);
	
	while(tempGalaxy = eaPop(&children))
	{
		tempGalaxy->galaxy = parentGalaxy;
		arrayPushBack(&tempGalaxySubBlocks, tempGalaxy);
		for(i = 0; i < tempGalaxy->gbbConns.size; i++)
		{
			conn = tempGalaxy->gbbConns.storage[i];
			destGalaxy = conn->destBlock;

			if(!destGalaxy->galaxy && blockHasGroundConnectionToBlock(destGalaxy, tempGalaxy))
			{
				destGalaxy->galaxy = tempGalaxy;
				eaPush(&children, destGalaxy);
			}
		}

		for(i = 0; i < tempGalaxy->rbbConns.size; i++)
		{
			conn = tempGalaxy->rbbConns.storage[i];
			destGalaxy = conn->destBlock;

			if(	!destGalaxy->galaxy &&
				conn->minJumpHeight <= maxJumpHeight &&
				blockHasRaisedConnectionToBlock(destGalaxy, tempGalaxy, maxJumpHeight))
			{
				destGalaxy->galaxy = tempGalaxy;
				eaPush(&children, destGalaxy);
			}
		}
	}

	eaDestroy(&children);
}

static void beaconCreateGalaxyGroups(BeaconStatePartition *partition, int quiet){
	int i, j;
	
	for(i = 0; i < partition->combatBeaconGalaxyArray[0].size; i++)
	{
		BeaconBlock* galaxy = partition->combatBeaconGalaxyArray[0].storage[i];

		galaxy->galaxy = NULL;
	}
	
	for(i = 1; i < beacon_galaxy_group_count; i++)
	{
		int childSet = i - 1;
		int childSetSize = partition->combatBeaconGalaxyArray[childSet].size;
		
		PERFINFO_AUTO_START("propagate", 1);
			tempGalaxyArray.size = 0;

			for(j = 0; j < childSetSize; j++)
			{
				BeaconBlock* childGalaxy = partition->combatBeaconGalaxyArray[childSet].storage[j];
				
				if(!childGalaxy->galaxy)
				{
					BeaconBlock* parentGalaxy = beaconGalaxyCreate(partition->id, i);
					
					arrayPushBack(&tempGalaxyArray, parentGalaxy);
					
					tempGalaxySubBlocks.size = 0;
					
					beaconPropagateGalaxyGroup(childGalaxy, parentGalaxy, i * beaconGalaxyGroupJumpIncrement);
					
					beaconBlockInitCopyArray(&parentGalaxy->subBlockArray, &tempGalaxySubBlocks);
					
					assert(childGalaxy->galaxy == parentGalaxy);
				}
			}
			
			beaconBlockInitCopyArray(&partition->combatBeaconGalaxyArray[i], &tempGalaxyArray);
		PERFINFO_AUTO_STOP();
		
		if(!quiet)
			printf("(%d,", partition->combatBeaconGalaxyArray[i].size);
		
		PERFINFO_AUTO_START("connect", 1);
			beaconMakeAllGalaxyConnections(partition, i, 1);
		PERFINFO_AUTO_STOP();

		if(!quiet)
			printf(")");
	}
}

static void beaconSplitBlocks(BeaconStatePartition *partition, int quiet){
	int i, j;
	
	if(!quiet){
		beaconProcessSetTitle(0, "Split");
	}

	for(i = 0; i < partition->combatBeaconGridBlockArray.size; i++){
		BeaconBlock* block = partition->combatBeaconGridBlockArray.storage[i];
		
		// Destroy the the sub-blocks in this 
		clearArrayEx(&block->subBlockArray, beaconSubBlockDestroy);

		assert(!block->subBlockArray.size);

		beaconSplitBlock(partition, block);
		
		if(!quiet){
			printf("%s%d", i ? "," : "", block->subBlockArray.size);
			
			beaconProcessSetTitle(100.0f * i / partition->combatBeaconGridBlockArray.size, NULL);
		}
		
		for(j = 0; j < block->subBlockArray.size; j++){
			BeaconBlock* subBlock = block->subBlockArray.storage[j];

			subBlock->madeConnections = 1;
		}
	}

	if(!quiet){
		beaconProcessSetTitle(100, NULL);
	}
}

void beaconSplitBlocksAndGalaxies(BeaconStatePartition *partition, int quiet){
	int i;
	
	if(!quiet){
		printf("Splitting Blocks: ");
	}
	
	PERFINFO_AUTO_START("split", 1);
		beaconSplitBlocks(partition, quiet);
	PERFINFO_AUTO_STOP();
	
	if(!quiet){
		printf("\nConnecting Blocks: ");
	}

	PERFINFO_AUTO_START("connect", 1);
		for(i = 0; i < partition->combatBeaconGridBlockArray.size; i++){
			BeaconBlock* gridBlock = partition->combatBeaconGridBlockArray.storage[i];

			if(!quiet){
				printf(".");
			}

			beaconGridBlockMakeBlockConnections(partition, gridBlock);
		}
	PERFINFO_AUTO_STOP();

	if(!quiet){
		printf("\nCreating Galaxies: ");
	}
	
	PERFINFO_AUTO_START("galaxize", 1);
		beaconCreateGalaxies(partition, quiet);
	PERFINFO_AUTO_STOP();
	
	if(!quiet){
		printf("\nDONE!\n\n");
	}
}

static int galaxyHasConnectionToGalaxy(BeaconBlock* source, BeaconBlock* target, BeaconBlockConnection **connOut)
{
	BeaconBlockConnection *conn = findBlockConnection(&source->gbbConns, target, NULL);

	if(conn && conn->blockCount < conn->connCount)
	{
		if(connOut)
			*connOut = conn;
		return 1;
	}

	conn = findBlockConnection(&source->rbbConns, target, NULL);

	if(conn && conn->blockCount < conn->connCount)
	{
		if(connOut)
			*connOut = conn;
		return 1;
	}
	
	if(connOut)
		*connOut = NULL;

	return 0;
}

static int staticPropagateClusterCount;

static void beaconPropagateCluster(BeaconBlock* galaxy, BeaconBlock* cluster, int quiet)
{
	int i;
	BeaconBlock **galaxies = NULL;

	assert(cluster->isCluster == 1);

	eaPush(&galaxies, galaxy);
	while(eaSize(&galaxies))
	{
		BeaconBlock *tempGalaxy = eaPop(&galaxies);
		
		assert(tempGalaxy->isGalaxy==1);

		tempGalaxy->cluster = cluster;
		arrayPushBack(&tempGalaxyArray, tempGalaxy);

		for(i = 0; i < tempGalaxy->gbbConns.size; i++)
		{
			BeaconBlockConnection* conn = tempGalaxy->gbbConns.storage[i];
			BeaconBlock* destGalaxy = conn->destBlock;

			if(conn->blockCount == conn->connCount)
				continue;

			if(!destGalaxy->cluster && galaxyHasConnectionToGalaxy(destGalaxy, tempGalaxy, NULL))
			{
				destGalaxy->cluster = cluster;
				eaPush(&galaxies, destGalaxy);
			}
		}

		for(i = 0; i < tempGalaxy->rbbConns.size; i++)
		{
			BeaconBlockConnection* conn = tempGalaxy->rbbConns.storage[i];
			BeaconBlock* destGalaxy = conn->destBlock;

			if(conn->blockCount == conn->connCount)
				continue;

			if(!destGalaxy->cluster && galaxyHasConnectionToGalaxy(destGalaxy, tempGalaxy, NULL))
			{
				destGalaxy->cluster = cluster;
				eaPush(&galaxies, destGalaxy);
			}
		}
	}

	eaDestroy(&galaxies);
}

static int __cdecl compareBeaconClusterSize(const BeaconBlock** b1, const BeaconBlock** b2){
	int size1 = (*b1)->subBlockArray.size;
	int size2 = (*b2)->subBlockArray.size;

	if(size1 < size2){
		return 1;
	}
	else if(size1 == size2)
		return 0;
	else
		return -1;
}

typedef struct ReverseBlockConnection {
	struct ReverseBlockConnection* next;
	BeaconBlock* source;
} ReverseBlockConnection;

MP_DEFINE(ReverseBlockConnection);

ReverseBlockConnection* createReverseBlockConnection(void){
	MP_CREATE(ReverseBlockConnection, 10000);
	
	return MP_ALLOC(ReverseBlockConnection);
}

void destroyReverseBlockConnection(ReverseBlockConnection* conn){
	MP_FREE(ReverseBlockConnection, conn);
}

static StashTable reverseConnTable;

static void createReverseConnectionTable(void){
	if(reverseConnTable){
		return;
	}
	
	reverseConnTable = stashTableCreateAddress(100);
}

static ReverseBlockConnection* getFirstReverseConnection(BeaconBlock* block){
	ReverseBlockConnection* pResult;

	if(!reverseConnTable){
		return NULL;
	}

	if ( reverseConnTable && stashAddressFindPointer(reverseConnTable, block, &pResult) )
		return pResult;
	return NULL;
}

static void addReverseConnection(BeaconBlock* target, BeaconBlock* source){
	ReverseBlockConnection* conn = getFirstReverseConnection(target);
	ReverseBlockConnection* prev = NULL;
	
	for(; conn; prev = conn, conn = conn->next){
		if(conn->source == source){
			return;
		}
	}
	
	if(prev){
		prev->next = createReverseBlockConnection();
		prev->next->source = source;
	}else{
		conn = createReverseBlockConnection();
		conn->source = source;
		stashAddressAddPointer(reverseConnTable, source, conn, false);
	}
}

static void removeArrayIndex(Array* array, int i){
	assert(i >= 0 && i < array->size);

	array->storage[i] = array->storage[--array->size];
}

static void removeFirstGalaxySubBlock(BeaconStatePartition *partition, BeaconBlock *galaxy)
{
	BeaconBlock* subBlock = galaxy->subBlockArray.storage[0];
	BeaconBlock* gridBlock = subBlock->parentBlock;
	BeaconBlock* tempSubBlock;
	int i;
	
	// Mark indices for all beacons.
	
	for(i = 0; i < gridBlock->beaconArray.size; i++){
		Beacon* beacon = gridBlock->beaconArray.storage[i];
		
		beacon->searchInstance = i;
	}
	
	// Remove all beacons from this subBlock.
	
	for(i = 0; i < subBlock->beaconArray.size; i++){
		Beacon* beacon = subBlock->beaconArray.storage[i];
		Beacon* tempBeacon;
		
		// Remove the beacon from the grid block.
		
		tempBeacon = gridBlock->beaconArray.storage[--gridBlock->beaconArray.size];
		gridBlock->beaconArray.storage[beacon->searchInstance] = tempBeacon;
		tempBeacon->searchInstance = beacon->searchInstance;
		
		// Remove the beacon from the combat beacon array.
		
		assert(beacon->globalIndex >= 0 && beacon->globalIndex < combatBeaconArray.size);
		tempBeacon = combatBeaconArray.storage[--combatBeaconArray.size];
		combatBeaconArray.storage[beacon->globalIndex] = tempBeacon;
		tempBeacon->globalIndex = beacon->globalIndex;
		
		// Free the connections.
		
		destroyArrayPartialEx(&beacon->gbConns, destroyBeaconConnection);
		destroyArrayPartialEx(&beacon->rbConns, destroyBeaconConnection);
	}
	
	// Remove the subBlock from the gridBlock.
	
	tempSubBlock = gridBlock->subBlockArray.storage[--gridBlock->subBlockArray.size];
	gridBlock->subBlockArray.storage[subBlock->searchInstance] = tempSubBlock;
	tempSubBlock->searchInstance = subBlock->searchInstance;
	
	// Remove the gridBlock if it has no more subBlocks.
	
	if(!gridBlock->subBlockArray.size){
		BeaconBlock* tempGridBlock = partition->combatBeaconGridBlockArray.storage[--partition->combatBeaconGridBlockArray.size];
		partition->combatBeaconGridBlockArray.storage[gridBlock->searchInstance] = tempGridBlock;
		tempGridBlock->searchInstance = gridBlock->searchInstance;
				
		stashIntRemovePointer(partition->combatBeaconGridBlockTable, beaconMakeGridBlockHashValue(gridBlock->pos[0], gridBlock->pos[1], gridBlock->pos[2]), NULL);

		beaconGridBlockDestroy(gridBlock);
	}

	clearArrayEx(&subBlock->beaconArray, destroyCombatBeacon);
	beaconSubBlockDestroy(subBlock);
	
	// Remove the subBlock from the galaxy.
	
	galaxy->subBlockArray.storage[0] = galaxy->subBlockArray.storage[--galaxy->subBlockArray.size];
}

void removeFirstGalaxyGalaxyOrSubBlock(BeaconStatePartition *partition, BeaconBlock *galaxy)
{
	BeaconBlock *block = galaxy->subBlockArray.storage[0];
	int i, j;
	if (block->isGalaxy)
	{
		for (i = 0; i < beacon_galaxy_group_count; i++)
		{
			j = arrayFindElement(&partition->combatBeaconGalaxyArray[i], block);
			if (j != -1)
			{
				break;
			}
		}
		while(block->subBlockArray.size)
		{
			removeFirstGalaxyGalaxyOrSubBlock(partition, block);
			removeArrayIndex(&galaxy->subBlockArray, 0);
		}
		beaconGalaxyDestroy(block);
		if (i < beacon_galaxy_group_count)
		{
			removeArrayIndex(&partition->combatBeaconGalaxyArray[i], j);
		}
		else
		{
			printf("Failed to remoive first galaxy or subblock from galaxy\n");
		}
	}
	else
	{
		removeFirstGalaxySubBlock(partition, galaxy);
	}
}

int beaconSubBlockRemoveInvalidConnectionsFromArray(Array* conns)
{
	int removedCount = 0;
	int i;

	for(i = 0; i < conns->size; i++){
		BeaconBlockConnection* conn = conns->storage[i];
		
		if(!conn->destBlock->galaxy->cluster){
			beaconBlockConnectionDestroy(conn);
			removeArrayIndex(conns, i--);
			removedCount++;
		}
	}
	
	return removedCount;
}

int beaconGalaxyRemoveInvalidConnectionsFromArray(Array *conns)
{
	int removedCount = 0;
	int i;
	
	for(i = 0; i < conns->size; i++){
		BeaconBlockConnection* conn = conns->storage[i];
		
		if(!conn->destBlock->cluster){
			beaconBlockConnectionDestroy(conn);
			removeArrayIndex(conns, i--);
			removedCount++;
		}
	}

	return removedCount;
}

int beaconRemoveInvalidConnectionsFromArray(BeaconStatePartition *partition, Array *conns)
{
	int removedCount = 0;
	int i;
	
	for(i = 0; i < conns->size; i++){
		BeaconConnection* conn = conns->storage[i];
		BeaconPartitionData *destPartition = beaconGetPartitionData(conn->destBeacon, partition->id, true);
		
		if(!destPartition->block->galaxy->cluster){
			destroyBeaconConnection(conn);
			removeArrayIndex(conns, i--);
			removedCount++;
		}
	}

	return removedCount;
}

static void beaconGalaxyRemoveInvalidGalaxyConnections(BeaconStatePartition *partition, BeaconBlock* galaxy){
	int removedCount;
	int i;
	
	if(!galaxy->cluster)
	{
		return;
	}
	
	removedCount =	beaconGalaxyRemoveInvalidConnectionsFromArray(&galaxy->gbbConns) +
					beaconGalaxyRemoveInvalidConnectionsFromArray(&galaxy->rbbConns);
	if(!removedCount)
	{
		return;
	}

	for(i = 0; i < galaxy->subBlockArray.size; i++){
		BeaconBlock* subBlock = galaxy->subBlockArray.storage[i];
		int j;

		removedCount =	beaconSubBlockRemoveInvalidConnectionsFromArray(&subBlock->gbbConns) +
						beaconSubBlockRemoveInvalidConnectionsFromArray(&subBlock->rbbConns);
		
		if(!removedCount)
		{
			continue;
		}
		
		for(j = 0; j < subBlock->beaconArray.size; j++){
			Beacon* beacon = subBlock->beaconArray.storage[j];
		
			beaconRemoveInvalidConnectionsFromArray(partition, &beacon->gbConns);
			beaconRemoveInvalidConnectionsFromArray(partition, &beacon->rbConns);
		}
	}
}

static void beaconPruneInvalidGalaxies(BeaconStatePartition *partition)
{
	S32 i, j;
	
	// Pre-setup the reverse connection table.
	
	// Prune!

	beaconProcessSetTitle(0, "Prune");

	// Enumerate all blocks for fast deletion.
	
	for(i = 0; i < partition->combatBeaconGridBlockArray.size; i++)
	{
		BeaconBlock* gridBlock = partition->combatBeaconGridBlockArray.storage[i];
		gridBlock->searchInstance = i;
		
		for(j = 0; j < gridBlock->subBlockArray.size; j++)
		{
			BeaconBlock* subBlock = gridBlock->subBlockArray.storage[j];
			
			subBlock->searchInstance = j;
		}
	}
	
	// Remove galaxy connections to invalid galaxies.
	
	for(i = 0; i < partition->combatBeaconGalaxyArray[0].size; i++)
	{
		BeaconBlock* galaxy = partition->combatBeaconGalaxyArray[0].storage[i];
		beaconGalaxyRemoveInvalidGalaxyConnections(partition, galaxy);
	}

	// Check that all valid-to-invalid beacon connections are gone.
	
	for(i = 0; i < combatBeaconArray.size; i++)
	{
		Beacon* beacon = combatBeaconArray.storage[i];
		BeaconPartitionData *beaconPartition = beaconGetPartitionData(beacon, partition->id, true);
		
		if(!beaconPartition->block->galaxy->cluster)
		{
			continue;
		}
		
		for(j = 0; j < beacon->gbConns.size; j++)
		{
			BeaconConnection* conn = beacon->gbConns.storage[j];
			BeaconPartitionData *destPartition = beaconGetPartitionData(conn->destBeacon, partition->id, true);
			assert(destPartition->block->galaxy->cluster);
		}

		for(j = 0; j < beacon->rbConns.size; j++)
		{
			BeaconConnection* conn = beacon->rbConns.storage[j];
			BeaconPartitionData *destPartition = beaconGetPartitionData(conn->destBeacon, partition->id, true);
			assert(destPartition->block->galaxy->cluster);
		}
	}
	
	// Remove the invalid galaxies.
	
	for(i = 0; i < partition->combatBeaconGalaxyArray[0].size; i++)
	{
		BeaconBlock* galaxy = partition->combatBeaconGalaxyArray[0].storage[i];

		if(galaxy->cluster)
		{
			continue;
		}
		
		while(galaxy->subBlockArray.size)
		{
			removeFirstGalaxySubBlock(partition, galaxy);
		}

		beaconGalaxyDestroy(galaxy);
		removeArrayIndex(&partition->combatBeaconGalaxyArray[0], i--);
	}

	beaconProcessSetTitle(100, NULL);
}

void beaconCheckInvalidSpawns(void)
{
	int i;
	for(i=0; i<eaSize(&encounterBeaconArray); i++)
	{
		Beacon *b = encounterBeaconArray[i];
		if(!b->wasReachedFromValid)
		{
			eaPush(&invalidEncounterArray, strdup(b->encounterStr));
			free(b->encounterStr);
			b->encounterStr = NULL;
		}
	}

	eaClear(&encounterBeaconArray);
}

static bool checkForSpecial(BeaconBlock *block)
{
	int i;
	//BeaconBlockConnection *conn, *revConn;
	if (block->galaxyPrunerHasSpecial)
	{
		return true;
	}
	block->galaxyPruneChecking = 1;
	for(i = 0; i < block->beaconArray.size; i++)
	{
		if (((Beacon*)block->beaconArray.storage[i])->isSpecial)
		{
			block->galaxyPrunerHasSpecial = 1;
			block->galaxyPruneChecking = 0;
			return true;
		}
	}/*
	for(i = 0; i < block->gbbConns.size; i++)
	{
		conn = block->gbbConns.storage[i];
		revConn = beaconBlockGetConnection(conn->destBlock, conn->srcBlock, false);
		if((revConn) &&
			(revConn->minHeight <= beacon_galaxy_group_count * beaconGalaxyGroupJumpIncrement) &&
			(conn->minJumpHeight <= beacon_galaxy_group_count * beaconGalaxyGroupJumpIncrement) &&
			(!conn->destBlock->galaxyPruneChecking) &&
			(checkForSpecial(conn->destBlock)))
		{
			block->galaxyPrunerHasSpecial = 1;
			block->galaxyPruneChecking = 0;
			return true;
		}
	}
	for(i = 0; i < block->rbbConns.size; i++)
	{
		conn = block->rbbConns.storage[i];
		revConn = beaconBlockGetConnection(conn->destBlock, conn->srcBlock, true);
		if((revConn) &&
			(revConn->minHeight <= beacon_galaxy_group_count * beaconGalaxyGroupJumpIncrement) &&
			(conn->minJumpHeight <= beacon_galaxy_group_count * beaconGalaxyGroupJumpIncrement) &&
			(!conn->destBlock->galaxyPruneChecking) &&
			(checkForSpecial(conn->destBlock)))
		{
			block->galaxyPrunerHasSpecial = 1;
			block->galaxyPruneChecking = 0;
			return true;
		}
	}*/
	for(i = 0; i < block->subBlockArray.size; i++)
	{
		BeaconBlock *subBlock = block->subBlockArray.storage[i];
		if ((!subBlock->galaxyPruneChecking) && (checkForSpecial(subBlock)))
		{
			block->galaxyPrunerHasSpecial = 1;
			block->galaxyPruneChecking = 0;
			return true;
		}
	}
	block->galaxyPruneChecking = 0;
	return false;
}

void beaconClusterizeGalaxies(BeaconStatePartition *partition, int requireValid, int quiet)
{
	int i;
	int originalBeaconCount = combatBeaconArray.size;
	
	PERFINFO_AUTO_START("clusterize", 1);

	if(!quiet)
		printf("FORMING GALAXY CLUSTERS:");

	destroyArrayPartialEx(&partition->combatBeaconClusterArray, beaconClusterDestroy);

	for(i = 0; i < partition->combatBeaconGalaxyArray[0].size; i++)
	{
		BeaconBlock* galaxy = partition->combatBeaconGalaxyArray[0].storage[i];

		assert(galaxy->isGalaxy);

		galaxy->cluster = NULL;
	}
	
	// Propagate the clusters.
	
	if(!quiet)
		beaconProcessSetTitle(0, "Clusterize");

	for(i = 0; i < combatBeaconArray.size; i++)
	{
		Beacon* beacon = combatBeaconArray.storage[i];
		
		if(!quiet && !(i % 100))
			printf(".");
		
		if(!requireValid || beacon->isValidStartingPoint)
		{
			BeaconPartitionData *bpd = beaconGetPartitionData(beacon, partition->id, true);
			BeaconBlock* galaxy = bpd->block->galaxy;
			
			assert(galaxy);
			
			if(!galaxy->cluster)
			{
				BeaconBlock* cluster = beaconClusterCreate(partition->id);
				
				tempGalaxyArray.size = 0;
				
				beaconPropagateCluster(galaxy, cluster, quiet);
				
				beaconBlockInitCopyArray(&cluster->subBlockArray, &tempGalaxyArray);
				
				if(!quiet)
					printf("(%d)", cluster->subBlockArray.size);
				
				arrayPushBack(&partition->combatBeaconClusterArray, cluster);
			}
		}
	}
	
	if(!quiet)
		beaconProcessSetTitle(100, NULL);

	// Remove useless galaxies.
	
	if(requireValid)
		beaconPruneInvalidGalaxies(partition);
	
	// Sort the cluster array by cluster size.
	
	qsort(	partition->combatBeaconClusterArray.storage,
			partition->combatBeaconClusterArray.size,
			sizeof(partition->combatBeaconClusterArray.storage[0]),
			compareBeaconClusterSize);

	PERFINFO_AUTO_STOP();

	// Create more galaxy groups.
	
	//printf("----------------------------------------------------------------------------------------------------\n");
	
	PERFINFO_AUTO_START("grouping", 1);

	if(!quiet)
		printf("DONE!\n\nGROUPING GALAXIES: ");

	beaconCreateGalaxyGroups(partition, quiet);
	
	PERFINFO_AUTO_STOP();

	if(!quiet)
	{
		printf(	"DONE!\n\n"
				"  Clusters:        %d\n"
				"  Removed Beacons: %d/%d\n"
				"  Galaxy Groups: ",
				partition->combatBeaconClusterArray.size,
				originalBeaconCount - combatBeaconArray.size,
				originalBeaconCount);

		for(i = 0; i < beacon_galaxy_group_count; i++)
			printf("%s%d", i ? ", " : "", partition->combatBeaconGalaxyArray[i].size);
		
		printf("\n");
	}
}

static struct {
	Vec3		source;
	Beacon**	beacons;
	int			count;
	int			maxCount;
	F32			maxDistSQR;
} collectedBeacons;

static void collectNearbyBeacons(Array* beaconArray, void* userData){
	int i;

	assert(beacon_state.mainThreadId==GetCurrentThreadId());
	
	for(i = 0; i < beaconArray->size; i++){
		Beacon* b = beaconArray->storage[i];
		
		F32 distSQR = distance3Squared(b->pos, collectedBeacons.source);
		
		if(	collectedBeacons.count < collectedBeacons.maxCount &&
			distSQR <= collectedBeacons.maxDistSQR)
		{
			collectedBeacons.beacons[collectedBeacons.count++] = b;
			
			b->userFloat = distSQR;
		}
	}
}

int beaconCompareUserfloat(const Beacon** b1, const Beacon** b2)
{
	F32 d1 = (*b1)->userFloat;
	F32 d2 = (*b2)->userFloat;
	
	if(d1 < d2)
		return -1;
	else if (d1 == d2)
		return 0;
	else
		return 1;
}

static BeaconClusterConnection* beaconGetClusterConnection(	BeaconBlock* srcCluster,
															Vec3 sourcePos,
															BeaconBlock* dstCluster,
															Vec3 targetPos)
{
	int i;
	
	for(i = 0; i < srcCluster->gbbConns.size; i++){
		BeaconClusterConnection* conn = srcCluster->gbbConns.storage[i];
		
		if(	conn->dstCluster == dstCluster &&
			sameVec3(conn->source.pos, sourcePos) &&
			sameVec3(conn->target.pos, targetPos))
		{
			return conn;
		}
	}
	
	return NULL;
}

static WorldInteractionEntry *getInteractionEntry(WorldCollObject *wco)
{
	WorldCollisionEntry *collEnt;
	
	if(wcoGetUserPointer(wco, entryCollObjectMsgHandler, &collEnt)){
		return collEnt ? collEnt->base_entry_data.parent_entry : NULL;
	}

	return NULL;
}

#define BEACON_CLUSTER_BEACON_LIMIT 5


static int beaconCreateClusterConnections(	BeaconStatePartition *partition, 
											Vec3 sourcePos, F32 radius1,
											Vec3 targetPos, F32 radius2,
											WorldInteractionEntry *interactionEntry)
{
	const int bufSize = 1000;
	static Beacon** sourceBeacons;
	int sourceCount;
	static Beacon** targetBeacons;
	int targetCount;
	int i;
	int connCount = 0;
	int validCount = 0;
	Vec3 srcPos, tgtPos;
	F32 groundY;

	copyVec3(sourcePos, srcPos);
	copyVec3(targetPos, tgtPos);

	// Ground snapping tends to put things ever so slightly below the ground
	srcPos[1] += 1;
	tgtPos[1] += 1;
	
	if(!sourceBeacons){
		sourceBeacons = callocStructs(Beacon*, bufSize);
		targetBeacons = callocStructs(Beacon*, bufSize);
	}

	// Get a list of source beacons.
	
	copyVec3(srcPos, collectedBeacons.source);
	collectedBeacons.beacons = sourceBeacons;
	collectedBeacons.count = 0;
	collectedBeacons.maxCount = bufSize;
	collectedBeacons.maxDistSQR = SQR(radius1);
	
	collectedBeacons.source[1] += 10;
	groundY = heightCacheGetHeight(beaconGetActiveWorldColl(partition->id), collectedBeacons.source);
	if(groundY!=-FLT_MAX)
		vecY(collectedBeacons.source) = groundY;
	beaconForEachBlock(partition, collectedBeacons.source, radius1, radius1, radius1, collectNearbyBeacons, NULL);
	
	sourceCount = collectedBeacons.count;
	
	if(!sourceCount){
		return 0;
	}
	
	// Get a list of target beacons.
	copyVec3(tgtPos, collectedBeacons.source);
	collectedBeacons.beacons = targetBeacons;
	collectedBeacons.count = 0;
	collectedBeacons.maxCount = bufSize;
	collectedBeacons.maxDistSQR = SQR(radius2);
	
	collectedBeacons.source[1] += 2;
	groundY = heightCacheGetHeight(beaconGetActiveWorldColl(partition->id), collectedBeacons.source);
	if(groundY!=-FLT_MAX)
		vecY(collectedBeacons.source) = groundY;
	beaconForEachBlock(partition, collectedBeacons.source, radius2, radius2, radius2, collectNearbyBeacons, NULL);
	
	targetCount = collectedBeacons.count;
	
	if(!targetCount){
		return 0;
	}
	
	// Sort the beacons by distance.
	qsort(sourceBeacons, sourceCount, sizeof(sourceBeacons[0]), beaconCompareUserfloat);
	qsort(targetBeacons, targetCount, sizeof(targetBeacons[0]), beaconCompareUserfloat);

	// Filter by LOS
	validCount = 0;
	for(i=0; i<sourceCount && validCount<BEACON_CLUSTER_BEACON_LIMIT; i++)
	{
		WorldCollCollideResults results;
		WorldInteractionEntry* entry = NULL;

		ZeroStruct(&results);
		beaconRayCollide(partition->id, beaconGetActiveWorldColl(partition->id), srcPos, sourceBeacons[i]->pos, WC_FILTER_BIT_MOVEMENT, &results);
		entry = getInteractionEntry(results.wco);

		if(results.hitSomething && (!entry || entry!=interactionEntry) &&
			distance3Squared(results.posWorldImpact, srcPos)>0.1)
		{
			sourceBeacons[i] = NULL;
			continue;
		}

		ZeroStruct(&results);
		beaconRayCollide(partition->id, beaconGetActiveWorldColl(partition->id), sourceBeacons[i]->pos, srcPos, WC_FILTER_BIT_MOVEMENT, &results);
		entry = getInteractionEntry(results.wco);

		if(results.hitSomething && (!entry || entry!=interactionEntry) &&
			distance3Squared(results.posWorldImpact, srcPos)>0.1)
		{
			sourceBeacons[i] = NULL;
			continue;
		}
		validCount++;
	}
	if(validCount>=BEACON_CLUSTER_BEACON_LIMIT)
		sourceCount = i;

	validCount = 0;
	for(i=0; i<targetCount && validCount<BEACON_CLUSTER_BEACON_LIMIT; i++)
	{
		WorldCollCollideResults results;

		ZeroStruct(&results);
		beaconRayCollide(partition->id, beaconGetActiveWorldColl(partition->id), targetBeacons[i]->pos, tgtPos, WC_FILTER_BIT_MOVEMENT, &results);

		if(results.hitSomething && getInteractionEntry(results.wco)!=interactionEntry &&
			distance3Squared(results.posWorldImpact, tgtPos)>0.1)
		{
			targetBeacons[i] = NULL;
			continue;
		}

		ZeroStruct(&results);
		beaconRayCollide(partition->id, beaconGetActiveWorldColl(partition->id), tgtPos, targetBeacons[i]->pos, WC_FILTER_BIT_MOVEMENT, &results);

		if(results.hitSomething && getInteractionEntry(results.wco)!=interactionEntry &&
			distance3Squared(results.posWorldImpact, tgtPos)>0.1)
		{
			targetBeacons[i] = NULL;
			continue;
		}
		validCount++;
	}
	if(validCount>=BEACON_CLUSTER_BEACON_LIMIT)
		targetCount = i;
	
	// Create connections.

	for(i = 0; i < sourceCount; i++){
		Beacon* sourceBeacon = sourceBeacons[i];
		BeaconPartitionData *sourcePartition = NULL;
		BeaconBlock* sourceCluster = NULL;
		int j;

		if(!sourceBeacon)
			continue;

		sourcePartition = beaconGetPartitionData(sourceBeacon, partition->id, false);
		if(!sourcePartition)
			continue;
		
		sourceCluster = SAFE_MEMBER3(sourcePartition, block, galaxy, cluster);
		if(!sourceCluster)
			continue;

		assert(sourceCluster->isCluster);

		for(j = 0; j < targetCount; j++){
			Beacon* targetBeacon = targetBeacons[j];
			BeaconClusterConnection* conn;
			BeaconPartitionData *targetPartition = NULL;
			BeaconBlock* targetCluster = NULL;

			if(!targetBeacon)
				continue;
			
			targetPartition = beaconGetPartitionData(targetBeacon, partition->id, false);
			if(!targetPartition)
				continue;

			targetCluster = SAFE_MEMBER3(targetPartition, block, galaxy, cluster);
			if(!targetCluster || targetCluster==sourceCluster)
				continue;

			assert(targetCluster->isCluster);

			conn = beaconGetClusterConnection(	sourceCluster, srcPos,
												targetCluster, tgtPos);
			
			if(!conn)
			{
				conn = beaconClusterConnectionCreate(sourceCluster, targetCluster);
				
				copyVec3(srcPos, conn->source.pos);
				conn->source.beacon = sourceBeacon;
				
				copyVec3(tgtPos, conn->target.pos);
				conn->target.beacon = targetBeacon;
				
				arrayPushBack(&sourceCluster->gbbConns, conn);

				assert(conn);
			}

			connCount++;
		}
	}
	
	return connCount;
}

void beaconClusterDestroyConnections(BeaconBlock *block)
{
	PERFINFO_AUTO_START_FUNC();

	clearArrayEx(&block->gbbConns, beaconClusterConnectionDestroy);

	PERFINFO_AUTO_STOP();
}

void beaconRemakeAllClusterConnections(BeaconStatePartition *partition)
{
	int i;
	PERFINFO_AUTO_START_FUNC();

	for(i=0; i<partition->combatBeaconClusterArray.size; i++)
	{
		beaconClusterDestroyConnections(partition->combatBeaconClusterArray.storage[i]);
	}

	beaconCreateAllClusterConnections(partition);

	PERFINFO_AUTO_STOP();
}

void beaconCreateAllClusterConnections(BeaconStatePartition *partition)
{
	int i;
	DoorConn **doors = NULL;

	beaconGatherDoors(&doors);

	for(i=0; i<eaSize(&doors); i++)
	{
		DoorConn *dc = doors[i];

		beaconCreateClusterConnections(partition, dc->src, 100, dc->dst, 100, dc->interactionEntry);
	}

	for(i = 0; i < partition->combatBeaconClusterArray.size; i++)
	{
		BeaconBlock *cluster = partition->combatBeaconClusterArray.storage[i];

		cluster->madeConnections = true;
	}

	eaDestroyEx(&doors, NULL);
}

void beaconClearAllBlockData(BeaconStatePartition *partition)
{
	int i;
	
	// Destroy the current block data.
	
	for(i = 0; i < partition->combatBeaconGridBlockArray.size; i++)
	{
		BeaconBlock *gridBlock = partition->combatBeaconGridBlockArray.storage[i];
		clearArrayEx(&gridBlock->subBlockArray, beaconSubBlockDestroy);
	}

	eaiDestroy(&partition->subBlockIds);
	partition->nextSubBlockIndex = 0;
	
	for(i = 0; i < beacon_galaxy_group_count; i++)
	{
		clearArrayEx(&partition->combatBeaconGalaxyArray[i], beaconGalaxyDestroy);
	}

	for(i = 0; i < ARRAY_SIZE(partition->galaxyIds); i++)
	{
		eaiDestroy(&partition->galaxyIds[i]);
		partition->nextGalaxyIndex[i] = 0;
	}
	
	destroyArrayPartialEx(&partition->combatBeaconClusterArray, beaconClusterDestroy);

	eaiDestroy(&partition->clusterIds);
	partition->nextClusterIndex = 0;
}

static void beaconRebuildAddInfo(int partitionId, BeaconDynamicInfo *dynamicInfo)
{

}

typedef struct CheckTriangle {
	Vec3 			min_xyz;
	Vec3 			max_xyz;
	Vec3 			verts[3];
	Vec3 			normal;
} CheckTriangle;

static struct {
	Vec3						min_xyz;
	Vec3						max_xyz;
	WorldCollStoredModelData*	smd;
	Mat4						world_mat;
	Mat4						invert_mat;
	CheckTriangle*				tris;
	S32							tri_count;
	
	BeaconDynamicInfo*			dynamicInfo;
} gatherConnsData;

static int checkInterpedIntersection(Vec3 a, Vec3 b, int interp_idx, int check_idx, F32 interp_to_value){
	F32 diff = b[interp_idx] - a[interp_idx];
	F32 ratio;
	F32 check_value;
	
	if(diff == 0){
		return 0;
	}
	
	ratio = (interp_to_value - a[interp_idx]) / diff;
	
	check_value = a[check_idx] + ratio * (b[check_idx] - a[check_idx]);
	
	if(	check_value >= gatherConnsData.min_xyz[check_idx] &&
		check_value <= gatherConnsData.max_xyz[check_idx])
	{
		return 1;
	}
	
	return 0;
}

static __forceinline void getSegMinMax(Beacon *source,
									   BeaconConnection *conn,
									   Vec3 seg_min_xyz_param,
									   Vec3 seg_max_xyz_param)
{
	// Get the box around the connection segment.
	int i;
	Beacon* target = conn->destBeacon;

	for(i = 0; i < 3; i++){
		if(source->pos[i] < target->pos[i]){
			seg_min_xyz_param[i] = source->pos[i];
			seg_max_xyz_param[i] = target->pos[i];
		}else{
			seg_min_xyz_param[i] = target->pos[i];
			seg_max_xyz_param[i] = source->pos[i];
		}
	}
}

static __forceinline int connIntersectsBoxXZ(	Beacon* source,
												BeaconConnection* conn,
												Vec3 min_xyz,
												Vec3 max_xyz,
												Vec3 seg_min_xyz_param,
												Vec3 seg_max_xyz_param,
												int write_seg_minmax)
{
	Beacon* target = conn->destBeacon;
	Vec3 seg_min_xyz;
	Vec3 seg_max_xyz;
	int found_outside = 0;
	int i;

	PERFINFO_AUTO_START_FUNC();

	copyVec3(seg_min_xyz_param, seg_min_xyz);
	copyVec3(seg_max_xyz_param, seg_max_xyz);

	for(i = 0; i < 3; i += 2){
		if(seg_min_xyz[i] > max_xyz[i] || seg_max_xyz[i] < min_xyz[i]){
			PERFINFO_AUTO_STOP_FUNC();
			return 0;
		}
	}

	// Got to here, so XZ overlap exists somewhere.

	for(i = 0; i < 3; i += 2){
		if(seg_min_xyz[i] <= min_xyz[i]){
			found_outside = 1;
			if(checkInterpedIntersection(source->pos, target->pos, i, 2 - i, min_xyz[i])){
				PERFINFO_AUTO_STOP_FUNC();
				return 1;
			}
		}

		if(seg_max_xyz[i] >= max_xyz[i]){
			found_outside = 1;
			if(checkInterpedIntersection(source->pos, target->pos, i, 2 - i, max_xyz[i])){
				PERFINFO_AUTO_STOP_FUNC();
				return 1;
			}
		}
	}

	PERFINFO_AUTO_STOP_FUNC();

	// If nothing was found outside the box then segment must be fully contained in the box.
	return !found_outside;
}

static __forceinline int connIntersectsHull(Beacon* source,
											BeaconConnection *conn,
											WorldCollStoredModelData *smd,
											GConvexHull *hull,
											S32 raised)
{
	Vec3 end;
	Vec3 start;
	Vec3 dir;
	F32 len;
	int hitFirst;
	int ret;
	
	copyVec3(source->pos, start);
	copyVec3(conn->destBeacon->pos, end);

	if(raised)
	{
		vecY(start) += conn->minHeight;
		vecY(end) = vecY(start) + conn->maxHeight;
	}

	// Finish the diagonal
	vecY(end) += 10;

	subVec3(end, start, dir);
	len = normalVec3(dir);

	hitFirst = hullCapsuleCollision(hull, start, dir, len, 4, gatherConnsData.invert_mat);

	if(hitFirst)
		return true;

	copyVec3(source->pos, start);
	copyVec3(conn->destBeacon->pos, end);

	if(raised)
	{
		vecY(start) += conn->minHeight;
		vecY(end) = vecY(start) + conn->maxHeight;
	}

	// Finish the diagonal
	vecY(start) += 10;

	subVec3(end, start, dir);
	len = normalVec3(dir);

	ret = hullCapsuleCollision(hull, start, dir, len, 4, gatherConnsData.invert_mat);

	return ret;
}							

static __forceinline int connIntersectsOBB(Beacon* source, 
										   BeaconConnection *conn,
										   Mat4 world_mat,
										   Vec3 local_min,
										   Vec3 local_max,
										   U32 isRaised)
{
	Mat4 invert;
	Vec3 ipos;
	Vec3 lineipos;
	F32 dist;
	F32 lineidistSQR;

	invertMat4(world_mat, invert);

	dist = boxLineNearestPoint(local_min, local_max, world_mat, invert, source->pos, conn->destBeacon->pos, ipos);
	lineidistSQR = pointLineDistSquaredXZ(ipos, source->pos, conn->destBeacon->pos, lineipos);

	if(dist < 4)
		return true;

	if(lineidistSQR < SQR(4) && fabs(vecY(lineipos) - vecY(ipos)) < 11)
		return true;

	return false;
}

static void getACD(Vec3 p0, Vec3 p1, F32* a, F32* c, F32* d){
	// Do the subtraction and rotation at the same time to get the segment normal.

	*a = vecZ(p1) - vecZ(p0);
	*c = -(vecX(p1) - vecX(p0));
	*d = -(*a * vecX(p0) + *c * vecZ(p0));
}

static int lineTriangleIntersectPoint(Vec3 src, Vec3 dst, CheckTriangle* tri, Vec3 ipos)
{
	F32     r_denom;
	F32		r;
	Vec3 	dv0p0;
	Vec3 	dp1p0;

	subVec3(tri->verts[0], src, dv0p0);
	subVec3(dst, src, dp1p0);

	r_denom = dotVec3(tri->normal, dp1p0);
	if( r_denom == 0 ) { // line is parallel to the triangle.
		return 0;
	}
	r = dotVec3(tri->normal, dv0p0) / r_denom;

	scaleVec3(dp1p0, r, ipos);
	addVec3(ipos, src, ipos);

	return 1;
}

static int lineIntersectsTriangle(Vec3 p0, Vec3 p1, CheckTriangle* tri){
	int		side_count = 0;
	int		i;
	Vec3	ipos;

	PERFINFO_AUTO_START_FUNC();

	if(!lineTriangleIntersectPoint(p0, p1, tri, ipos))
	{
		// Parallel
		PERFINFO_AUTO_STOP_FUNC();
		return 0;
	}
	
	if(	distance3Squared(p0, ipos) > distance3Squared(p0, p1) ||
		distance3Squared(p1, ipos) > distance3Squared(p1, p0))
	{
		PERFINFO_AUTO_STOP_FUNC();
		return 0;
	}
	
	// Check if the point is in the triangle.
	
	for(i = 0; i < 3; i++){
		Vec3 temp;
		Vec3 pnormal;
		subVec3(tri->verts[(i+1)%3], tri->verts[i], temp);
		crossVec3(temp, tri->normal, pnormal);
		subVec3(ipos, tri->verts[i], temp);
		if(dotVec3(pnormal, temp) >= 0){
			side_count++;
		}
	}
	
	if(!side_count || side_count == 3){
		#if 0
		// This will send the connection points to the client.
		
		{
			Vec3 pos;
			S32 i;
			
			for(i = 0; i < 3; i++){
				copyVec3(ipos, pos);
				pos[i] += 0.5f;
				aiEntSendAddLine(ipos, 0xffffffff, pos, 0xffff0000);

				copyVec3(ipos, pos);
				pos[i] -= 0.5f;
				aiEntSendAddLine(ipos, 0xffffffff, pos, 0xffff0000);
			}
		}
		#endif

		PERFINFO_AUTO_STOP_FUNC();
		return 1;
	}
	
	PERFINFO_AUTO_STOP_FUNC();
	return 0;
}

static int connIntersectsTriangles(Beacon* source, BeaconConnection* conn, Vec3 min_xyz, Vec3 max_xyz){
	Beacon* target = conn->destBeacon;
	F32		sourceTargetDistXZ = distance3XZ(source->pos, target->pos);
	F32		sourceToTargetDistY = vecY(target->pos) - vecY(source->pos);
	F32		connMinY = vecY(source->pos) + conn->minHeight;
	F32		connMaxY = vecY(source->pos) + conn->maxHeight;
	int		i;
	
	PERFINFO_AUTO_START_FUNC();

	for(i = 0; i < gatherConnsData.tri_count; i++){
		CheckTriangle* tri = gatherConnsData.tris + i;
		
		if(connIntersectsBoxXZ(source, conn, tri->min_xyz, tri->max_xyz, min_xyz, max_xyz, 0)){
			Vec3 ipos;
			F32 dist;

			if(lineIntersectsTriangle(source->pos, target->pos, tri)){
				PERFINFO_AUTO_STOP_FUNC();
				return 1;
			}
			
			dist = triLineNearestPoint(source->pos, target->pos, tri->verts, ipos);
			if(dist < 3){
				PERFINFO_AUTO_STOP_FUNC();
				return 1;
			}
			else if(dist<10){
				Vec3 lineipos;
				if(pointLineDistSquaredXZ(ipos, source->pos, target->pos, lineipos) < SQR(3) && 
					fabs(vecY(ipos)-vecY(lineipos))<10)
				{
					PERFINFO_AUTO_STOP_FUNC();
					return 1;
				}
			}
			
		}
	}
	
	PERFINFO_AUTO_STOP_FUNC();
	return 0;
}

static StashTable beaconConnToDynamicConnTable;

MP_DEFINE(BeaconDynamicConnection);

static BeaconDynamicConnection* createBeaconDynamicConnection(void){
	MP_CREATE(BeaconDynamicConnection, 100);
	
	return MP_ALLOC(BeaconDynamicConnection);
}

static void destroyBeaconDynamicConnection(BeaconDynamicConnection* conn){
	if(conn){
		BeaconDynamicConnection *lookup = NULL;
		eaDestroy(&conn->infos);
		FOR_EACH_IN_EARRAY(conn->partitions, BeaconDynamicConnectionPartition, partition)
		{
			if(partition)
				beaconDynConnPartitionDestroy(conn, partition);
		}
		FOR_EACH_END
		eaDestroy(&conn->partitions);

		stashAddressRemovePointer(beaconConnToDynamicConnTable, conn->conn, &lookup);
		devassert(conn==lookup);
		
		MP_FREE(BeaconDynamicConnection, conn);
	}
}

S32	beaconConnectionIsDisabled(Beacon *b, int partitionId, BeaconConnection *conn)
{
	BeaconPartitionData *partition = beaconGetPartitionData(b, partitionId, false);

	if(!partition)
		return false;

	return eaFindCmp(&partition->disabledConns, conn, cmpDynToConn)!=-1;
}

MP_DEFINE(BeaconDynamicInfo);

static BeaconDynamicInfo* createBeaconDynamicInfo(void){
	MP_CREATE(BeaconDynamicInfo, 100);
	
	return MP_ALLOC(BeaconDynamicInfo);
}

static void destroyBeaconDynamicInfo(BeaconDynamicInfo* info){
	MP_FREE(BeaconDynamicInfo, info);
}

BeaconDynamicConnection* beaconGetExistingDynamicConnection(BeaconConnection* conn){
	BeaconDynamicConnection* dynConn = NULL;

	if(stashAddressFindPointer(beaconConnToDynamicConnTable, conn, &dynConn)){
		return dynConn;
	}
	
	return NULL;
}

static BeaconDynamicConnection* getDynamicConnection(Beacon* source, int raised, int index){
	Array* conns = raised ? &source->rbConns : &source->gbConns;
	BeaconDynamicConnection* dynConn;
	BeaconConnection* conn;

	assert(index >= 0 && index < conns->size);
	
	conn = conns->storage[index];
	
	dynConn = beaconGetExistingDynamicConnection(conn);
	
	if(!dynConn){
		dynConn = createBeaconDynamicConnection();
		
		if(!beaconConnToDynamicConnTable){
			beaconConnToDynamicConnTable = stashTableCreateAddress(100);
		}
		
		stashAddressAddPointer(beaconConnToDynamicConnTable, conn, dynConn, false);
		
		dynConn->source = source;
		dynConn->conn = conn;
		dynConn->raised = raised;
	}
	else
		assert(dynConn->conn==conn && dynConn->raised==raised && dynConn->source==source);
	
	return dynConn;
}

static void beaconDynConnAdd(BeaconDynamicConnection *dynConn, Beacon *b, BeaconConnection *conn, S32 raised, S32 index)
{
	if(!conn)
		conn = raised ? b->rbConns.storage[index] : b->gbConns.storage[index];

	if(!dynConn){
		dynConn = getDynamicConnection(b, raised, index);
	}else{
		assert(dynConn->conn == conn);
	}

	eaPush(&gatherConnsData.dynamicInfo->conns, dynConn);

	eaPush(&dynConn->infos, gatherConnsData.dynamicInfo);
}	

typedef struct BeaconDynConnCallbackInfo {
	DynConnCallback cb;
	void *userdata;
} BeaconDynConnCallbackInfo;

static __forceinline GConvexHull* beaconConnectionSMDHasHull(WorldCollStoredModelData *smd)
{
	GConvexHull *hull = NULL;

	stashAddressFindPointer(smdHullCache, smd, &hull);
	return hull;
}

static __forceinline void checkConnection(	Beacon* b,
											BeaconConnection* conn,
											BeaconDynConnCallbackInfo *info, 
											S32 raised,
											S32 index)
{
	Vec3 min_xyz;
	Vec3 max_xyz;
	S32 obb = false;
	GConvexHull *hull = NULL;
	S32 intersected = false;
	
	PERFINFO_AUTO_START_FUNC();

	if(conn->destBeacon->noDynamicConnections)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	getSegMinMax(b, conn, min_xyz, max_xyz);
	if(!connIntersectsBoxXZ(b, conn, gatherConnsData.min_xyz, gatherConnsData.max_xyz, min_xyz, max_xyz, 0))
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	if(gatherConnsData.smd && (hull = beaconConnectionSMDHasHull(gatherConnsData.smd)))
	{
		intersected = connIntersectsHull(	b, 
											conn,
											gatherConnsData.smd,
											hull,
											raised);
	}
	else
	{
		intersected = connIntersectsTriangles(b, conn, min_xyz, max_xyz);
	}

	if(intersected)
		info->cb(info->userdata, b, conn, raised, index);

	PERFINFO_AUTO_STOP_FUNC();
}

static void gatherAllBeaconConnections(Beacon* b, BeaconDynConnCallbackInfo* userData){
	Array* array;
	int i;

	if(b->noDynamicConnections)
		return;

	// This has to go in reverse, because the check connection call back can remove them.
	array = &b->gbConns;
	for(i = array->size-1; i >= 0; i--){
		checkConnection(b, array->storage[i], userData, false, i);
	}
	
	array = &b->rbConns;
	for(i = array->size-1; i >= 0; i--){
		checkConnection(b, array->storage[i], userData, true, i);
	}
}
		
static void gatherConnections(Array* beaconArray, BeaconDynConnCallbackInfo* userData){
	arrayForEachItem(beaconArray, gatherAllBeaconConnections, userData);
}

typedef struct BeaconDynConnMeshTempData {
	Mat4 world_mat;
	Vec3 world_center;
	Vec3 world_min;
	Vec3 world_max;
	F32 radius;
} BeaconDynConnMeshTempData;

void beaconDynConnProcessMesh(const Vec3 *verts,
							  S32 vert_count,
							  const S32 *tris,
							  S32 tri_count,
							  Vec3 min,
							  Vec3 max,
							  Mat4 world_mat,
							  DynConnCallback cb, 
							  void *userdata)
{
	int i;
	Vec3 span_xyz;
	BeaconDynConnCallbackInfo bdcci;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	if(!vert_count || !tri_count)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	bdcci.cb = cb;
	bdcci.userdata = userdata;

	copyMat4(world_mat, gatherConnsData.world_mat);
	invertMat4(world_mat, gatherConnsData.invert_mat);

	gatherConnsData.tri_count = tri_count;
	gatherConnsData.tris = ScratchAlloc(tri_count * sizeof(CheckTriangle));
	mulBoundsAA(min, max, gatherConnsData.world_mat, gatherConnsData.min_xyz, gatherConnsData.max_xyz);

	for(i=0; i<gatherConnsData.tri_count; i++)
	{
		int j;
		CheckTriangle *checktri = &gatherConnsData.tris[i];
		const S32 *tri = &tris[i*3];
		Vec3 v01, v02;

		for(j=0; j<3; j++)
		{
			mulVecMat4(verts[tri[j]], world_mat, checktri->verts[j]);

			if(j==0)
			{
				copyVec3(checktri->verts[j], checktri->min_xyz);
				copyVec3(checktri->verts[j], checktri->max_xyz);
			}
			else
			{
				int k;

				for(k=0; k<3; k++)
				{
					MIN1(checktri->min_xyz[k], checktri->verts[j][k]);
					MAX1(checktri->max_xyz[k], checktri->verts[j][k]);
				}
			}
		}

		subVec3(checktri->verts[1], checktri->verts[0], v01);
		subVec3(checktri->verts[2], checktri->verts[0], v02);

		crossVec3(v01, v02, checktri->normal);
		normalVec3(checktri->normal);
	}

	subVec3(gatherConnsData.max_xyz, gatherConnsData.min_xyz, span_xyz);

	if(span_xyz[0] < 1000 && span_xyz[1] < 1000 && span_xyz[2] < 1000)
	{
		Vec3 mid;
		PERFINFO_AUTO_START("compare beacon conns", 1);
			centerVec3(gatherConnsData.min_xyz, gatherConnsData.max_xyz, mid);
			beaconForEachBlock(	beaconStatePartitionGet(0, false), 
								mid, 
								span_xyz[0] + 100, 
								span_xyz[1] + 100, 
								span_xyz[2] + 100, 
								gatherConnections, 
								&bdcci);
		PERFINFO_AUTO_STOP();
	}

	ScratchFree(gatherConnsData.tris);

	PERFINFO_AUTO_STOP_FUNC();
}

void beaconCacheWCOHulls(BeaconQueuedDynConnCheck **checks)
{
	static StashTable counts = NULL;
	static WorldCollStoredModelData **toHull = NULL;

	if(eaSize(&checks)<50)
		return;

	if(!counts)
		counts = stashTableCreateAddress(40);

	if(!smdHullCache)
		smdHullCache = stashTableCreateAddress(40);

	stashTableClear(counts);
	eaClearFast(&toHull);

	FOR_EACH_IN_EARRAY(checks, BeaconQueuedDynConnCheck, check)
	{
		if(!check->smd)
			continue;

		if(!stashAddressFindInt(counts, check->smd, NULL))
		{
			eaPush(&toHull, check->smd);
			
			stashAddressAddInt(counts, check->smd, 1, true);
		}
	}
	FOR_EACH_END

	FOR_EACH_IN_EARRAY(toHull, WorldCollStoredModelData, smd)
	{
#if !PSDK_DISABLED
		GConvexHull *hull = psdkCreateConvexHull(smd->detail, smd->vert_count, smd->verts);

		if(hull)
			stashAddressAddPointer(smdHullCache, smd, hull, true);
#endif
	}
	FOR_EACH_END
}

void beaconClearWCOHulls(void)
{
	if(!smdHullCache)
		return;

	stashTableClearEx(smdHullCache, NULL, hullDestroy);
}

S32 beaconDynConnCreate(void *id, const Vec3 *verts, S32 vert_count, const S32 *tris, S32 tri_count, Vec3 min, Vec3 max, Mat4 world_mat)
{
#if PSDK_DISABLED
	return 0;
#else
	PERFINFO_AUTO_START_FUNC();

	beaconDynConnProcessMesh(	verts, 
								vert_count,
								tris, 
								tri_count,
								min,
								max,
								world_mat,
								beaconDynConnAdd,
								NULL);
	
	PERFINFO_AUTO_STOP_FUNC();

	return 1;
#endif
}

S32 beaconDynConnAllowedToSet(void)
{
	return combatBeaconArray.size < 100000 || ABS_TIME < SEC_TO_ABS_TIME(5);
}

BeaconPartitionData* beaconGetPartitionData(Beacon* b, int partitionId, int create)
{
	BeaconPartitionData *partition = NULL;

	if(!b)
		return NULL;
	
	if(combatBeaconArray.size > BEACON_PARTITION_LIMIT || beaconIsBeaconizer())
		partitionId = 0;
	
	partition = eaGet(&b->partitions, partitionId);

	if(partition || !create)
		return partition;

	partition = callocStruct(BeaconPartitionData);
	partition->idx = partitionId;
	eaSet(&b->partitions, partition, partitionId);

	return partition;
}

void beaconPartitionDataDestroy(Beacon* b, BeaconPartitionData *partition)
{
	if(!partition)
		return;

	eaDestroyEx(&partition->avoidNodes, beaconDestroyAvoidNode_CalledFromBeacon);
	eaDestroy(&partition->disabledConns);
	eaSet(&b->partitions, NULL, partition->idx);
	free(partition);
}

BeaconDynamicInfoPartition* beaconDynamicInfoPartitionGet(BeaconDynamicInfo* dynamicInfo, int partitionIdx, int create)
{
	BeaconDynamicInfoPartition *partition = NULL;
	
	if(combatBeaconArray.size > BEACON_PARTITION_LIMIT)
		partitionIdx = 0;
	
	partition = eaGet(&dynamicInfo->partitions, partitionIdx);

	if(partition || !create)
		return partition;

	partition = callocStruct(BeaconDynamicInfoPartition);
	eaSet(&dynamicInfo->partitions, partition, partitionIdx);
	partition->idx = partitionIdx;
	partition->connsLastRebuild = true;
	partition->connsEnabled = true;

	return partition;
}

BeaconDynamicConnectionPartition* beaconDynConnPartitionGet(BeaconDynamicConnection *dynConn, int partitionIdx, int create)
{
	BeaconDynamicConnectionPartition *partition = NULL;
	
	if(combatBeaconArray.size > BEACON_PARTITION_LIMIT)
		partitionIdx = 0;

	partition = eaGet(&dynConn->partitions, partitionIdx);

	if(partition || !create)
		return partition;

	partition = callocStruct(BeaconDynamicConnectionPartition);
	partition->idx = partitionIdx;
	eaSet(&dynConn->partitions, partition, partitionIdx);

	return partition;
}

void beaconDynConnPartitionDestroy(BeaconDynamicConnection *dynConn, BeaconDynamicConnectionPartition *partition)
{
	eaSet(&dynConn->partitions, NULL, partition->idx);
	free(partition);
}

void beaconSetDynamicConnections(	BeaconDynamicInfo** dynamicInfoParam,
									int connsEnabled,
									int partitionId)
{
	int state = 0;
	BeaconDynamicInfo* dynamicInfo = *dynamicInfoParam;
	BeaconStatePartition *statepartition = beaconStatePartitionGet(partitionId, true);
	BeaconDynamicInfoPartition *partition = beaconDynamicInfoPartitionGet(dynamicInfo, partitionId, false);

	if(!partition)
		return;

	if(!beaconDynConnAllowedToSet()){
		return;
	}

	assert(dynamicInfo);

	if(!eaSize(&dynamicInfo->conns)){
		return;
	}

	connsEnabled = connsEnabled ? 1 : 0;

	// No point changing it again
	if(connsEnabled == partition->connsEnabled)
		return;

	partition->connsEnabled = connsEnabled;

	// Once it actually changes, check to see if we've logged it in that state
	if(eSetFind(&statepartition->changedInfos, dynamicInfo))
	{
		// If we have logged it and the state is the same, remove it (this should basically always happen)
		if(partition->connsLastRebuild == partition->connsEnabled)
		{
			eSetRemove(&statepartition->changedInfos, dynamicInfo);
			return;
		}
	}

	// Log it as changed
	eSetAdd(&statepartition->changedInfos, dynamicInfo);
	statepartition->needBlocksRebuilt = true;
}

static void beaconDestroyDynConnInfoPartition(int partition, BeaconDynamicInfo *info)
{
	BeaconDynamicInfoPartition *dynInfoPartition = beaconDynamicInfoPartitionGet(info, partition, false);

	if(dynInfoPartition)
		beaconDynamicInfoPartitionDestroy(&info, dynInfoPartition);

	if(eaSize(&info->partitions)==0)
		beaconDestroyDynamicInfo(&info);
}

static void beaconCheckDynamicConns(DynConnState show_hide_change,
								    void *id,
									int subId,
									const Vec3 *verts,
									S32 vert_count,
									const S32 *tris,
									S32 tri_count,
									Vec3 min,
									Vec3 max,
									Mat4 world_mat,
									int partition)
{
	BeaconDynamicInfoList *infoList = NULL;
	BeaconDynamicInfo* info = NULL;

	if(beaconIsBeaconizer() || wlDontLoadBeacons() || !id)
	{
		return;
	}

	if(combatBeaconArray.size > BEACON_PARTITION_LIMIT)
		partition = 0;

	if(!interactionToDynConnInfo)
	{
		interactionToDynConnInfo = stashTableCreateAddress(512);
	}

	stashAddressFindPointer(interactionToDynConnInfo, id, &infoList);

	if(!infoList && show_hide_change!=DYN_CONN_CREATE)
		return;

	if(subId==-1)
		assert(show_hide_change==DYN_CONN_DESTROY);

	if(infoList)
	{
		if(subId==-1)
			info = (BeaconDynamicInfo*)0x1;
		else
			info = eaGet(&infoList->infos, subId);
	}

	if(!info && show_hide_change!=DYN_CONN_CREATE)
		return;

	if(show_hide_change!=DYN_CONN_CREATE)
	{
		if(!info)
			return;

		if(show_hide_change!=DYN_CONN_DESTROY && !eaSize(&info->conns))
			return;
	}

	switch(show_hide_change)
	{
	xcase DYN_CONN_CREATE: {
		BeaconDynamicInfoPartition *dynInfoPartition = NULL;
		
		if(!info)
		{
			info = createBeaconDynamicInfo();
			info->id = id;
			info->subId = subId;

			gatherConnsData.dynamicInfo = info;
			if(!beaconDynConnCreate(id, verts, vert_count, tris, tri_count, min, max, world_mat))
			{
				destroyBeaconDynamicInfo(info);
				return;
			}

			if(!infoList)
			{
				infoList = callocStruct(BeaconDynamicInfoList);
				infoList->id = id;
			}

			if(subId==-1)
				eaSet(&infoList->infos, info, 0);
			else
				eaSet(&infoList->infos, info, subId);

			stashAddressAddPointer(interactionToDynConnInfo, id, infoList, 1);
		}

		if(!eaSize(&info->conns))
			return;

		if(!beaconDynamicInfoPartitionGet(info, partition, false))
		{
			dynInfoPartition = beaconDynamicInfoPartitionGet(info, partition, true);
			
			// Create partition on all dynConns so later uses can rely on its existence
			FOR_EACH_IN_EARRAY(info->conns, BeaconDynamicConnection, dynConn)
			{
				beaconDynConnPartitionGet(dynConn, partition, true);
			}
			FOR_EACH_END

			beaconSetDynamicConnections(&info, 0, partition);
		}
	}
	xcase DYN_CONN_SHOW:
		beaconSetDynamicConnections(&info, 0, partition);
	xcase DYN_CONN_HIDE:
		beaconSetDynamicConnections(&info, 1, partition);
	xcase DYN_CONN_DESTROY: {
			if(subId!=-1)
			{
				beaconDestroyDynConnInfoPartition(partition, info);
			}
			else
			{
				bool empty;
				FOR_EACH_IN_EARRAY(infoList->infos, BeaconDynamicInfo, subInfo)
				{
					if(subInfo)
						beaconDestroyDynConnInfoPartition(partition, subInfo);
				}
				FOR_EACH_END;

				empty = true;
				FOR_EACH_IN_EARRAY(infoList->infos, BeaconDynamicInfo, subInfo)
				{
					if(subInfo)
					{
						empty = false;
						break;
					}
				}
				FOR_EACH_END;

				if(empty)
				{
					stashAddressRemovePointer(interactionToDynConnInfo, infoList->id, NULL);
					free(infoList);
				}
			}
		}
	}
}

static void beaconCheckDynamicConnsActor(	int iPartitionIdx,
											DynConnState show_hide_change,
											void *id, 
											PSDKActor *actor,
											const Vec3 sceneOffset)
{
#if !PSDK_DISABLED
	const PSDKShape*const* shapes = NULL;
	Vec3 min, max;
	U32 i;

	if(!beaconDynConnAllowedToSet())
		return;

	if(!psdkActorGetShapesArray(actor, &shapes))
		return;

	for(i=0; i<psdkActorGetShapeCount(actor); i++)	
	{
		PSDKCookedMesh *mesh = NULL;

		if(	psdkShapeGetCookedMesh(shapes[i], &mesh) && 
			mesh)
		{
			S32 *tris = NULL;
			S32 tricount = 0;
			const Vec3 *verts = NULL;
			S32 vertcount = 0;
			Mat4 shape_mat;
			if(	psdkShapeGetMat(shapes[i], shape_mat) &&
				psdkCookedMeshGetTriangles(mesh, &tris, &tricount) &&
				psdkCookedMeshGetVertices(mesh, &verts, &vertcount) &&
				psdkShapeGetBounds(shapes[i], min, max))
			{
				Mat4 inv;
				Vec3 locMin, locMax;

				subVec3(shape_mat[3], sceneOffset, shape_mat[3]);
				subVec3(min, sceneOffset, min);
				subVec3(max, sceneOffset, max);
				invertMat4(shape_mat, inv);
				mulBoundsAA(min, max, inv, locMin, locMax);
				beaconCheckDynamicConns(show_hide_change, id, 0, verts, vertcount, tris, tricount, locMin, locMax, shape_mat, iPartitionIdx);
			}
		}
	}
#endif
}

static void beaconCheckDynamicConnsSMD(DynConnState show_hide_change,
									   void *id,
									   int subId,
									   WorldCollStoredModelData *smd,
									   Mat4 world_mat,
									   int partition)
{
	if(smd && show_hide_change==DYN_CONN_CREATE && smd->tri_count > gConf.iMaxHideShowCollisionTris && combatBeaconArray.size>0)
	{
		ErrorFilenamef(smd->filename, "Tried hiding/showing object with > %d collision tris: %s %d", gConf.iMaxHideShowCollisionTris, smd->detail, smd->tri_count);
	}

	beaconCheckDynamicConns(show_hide_change, 
							id, 
							subId,
							smd ? smd->verts : NULL, 
							smd ? smd->vert_count : 0, 
							smd ? smd->tris : NULL, 
							smd ? smd->tri_count : 0, 
							smd ? smd->min : NULL, 
							smd ? smd->max : NULL, 
							world_mat,
							partition);
}

void beaconDynamicInfoPartitionDestroy(BeaconDynamicInfo **dynInfoParam, BeaconDynamicInfoPartition *partition)
{
	BeaconDynamicInfo *dynInfo = *dynInfoParam;
	BeaconStatePartition *statePartition = beaconStatePartitionGet(partition->idx, true);
	beaconSetDynamicConnections(dynInfoParam, 1, partition->idx);
	eSetRemove(&statePartition->changedInfos, dynInfo);

	eaSet(&dynInfo->partitions, NULL, partition->idx);
	free(partition);
}

void beaconDestroyDynamicInfo(BeaconDynamicInfo** dynamicInfoParam){
	BeaconDynamicInfo* dynamicInfo = *dynamicInfoParam;

	if(dynamicInfo)
	{
		int i;
		BeaconDynamicInfoList *list = NULL;
		
		// Unset connections in all partitions
		for(i=eaSize(&dynamicInfo->partitions)-1; i>=0; i--)
		{
			if(dynamicInfo->partitions[i])
				beaconDynamicInfoPartitionDestroy(dynamicInfoParam, dynamicInfo->partitions[i]);
		}
		
		for(i = eaSize(&dynamicInfo->conns) - 1; i >= 0; i--){
			BeaconDynamicConnection*	dynConn = dynamicInfo->conns[i];
			S32							index = eaFindAndRemove(&dynConn->infos, dynamicInfo);

			assert(index >= 0);
			
			if(!eaSize(&dynConn->infos))
			{
				// Removed last reference, so delete the dynamic connection.
				FOR_EACH_IN_EARRAY(dynConn->conn->destBeacon->partitions, BeaconPartitionData, bcnPartition)
				{
					BeaconDynamicConnectionPartition *dynConnPartition = NULL;
					if(!bcnPartition)
						continue;

					eaFindAndRemoveFast(&bcnPartition->disabledConns, dynConn);
					dynConn->conn->disabled = false;
				}
				FOR_EACH_END
				
				destroyBeaconDynamicConnection(dynConn);
			}
		}
		
		eaDestroy(&dynamicInfo->conns);

		stashAddressFindPointer(interactionToDynConnInfo, dynamicInfo->id, &list);
		if(list)
			eaSet(&list->infos, NULL, dynamicInfo->subId);
		
		destroyBeaconDynamicInfo(dynamicInfo);
		
		*dynamicInfoParam = NULL;
	}	
}

void beaconQueueDynConnCheck(DynConnState command,
							 WorldCollObject *wco,
							 void* id, 
							 int subId,
							 int partition)
{
	BeaconQueuedDynConnCheck *check = NULL;
	WorldCollModelInstanceData *inst = NULL;
	WorldCollStoredModelData *smd = NULL;

	if(!beaconDynConnAllowedToSet())
		return;

	if(command!=DYN_CONN_CREATE && command!=DYN_CONN_SHOW && !beaconIdHasDynConns(id, subId))
		return;

	if(command==DYN_CONN_CREATE || command==DYN_CONN_SHOW)
	{
		wcoGetStoredModelData(&smd, NULL, wco, WC_FILTER_BIT_MOVEMENT);

		if(!smd)
			return;
	}

	check = calloc(1, sizeof(BeaconQueuedDynConnCheck));

	check->command = command;
	check->wco = wco;
	check->id = id;
	check->subId = subId;
	check->partition = partition;
	check->smd = smd;

	if(wcoGetInstData(&inst, wco, WC_FILTER_BIT_MOVEMENT))
	{
		copyMat4(inst->world_mat, check->world_mat);
		SAFE_FREE(inst);
	}

	eaPush(&queuedChecks, check);
}

BeaconNoDynConnVolume* beaconFindNoDynConnVol(const void *ptrId, int create) 
{
	BeaconNoDynConnVolume *vol = NULL;

	if(!beaconNoDynConnVolumes)
		return NULL;

	if(!stashAddressFindPointer(beaconNoDynConnVolumes, ptrId, &vol) && create)
	{
		vol = calloc(1, sizeof(BeaconNoDynConnVolume));
		vol->ptrId = ptrId;

		stashAddressAddPointer(beaconNoDynConnVolumes, ptrId, vol, true);
	}

	return vol;
}

S32 beaconNoDynConnVolIncludesBeacon(BeaconNoDynConnVolume *vol, Beacon *b)
{
	if(vol->isBox)
		return sphereOrientBoxCollision(b->pos, 0, vol->box.local_min, vol->box.local_max, vol->box.world_mat, NULL);
	else
		return distance3Squared(b->pos, vol->sphere.pos) < SQR(vol->sphere.radius);
}

void beaconNoDynConnVolGather(Array *beacons, BeaconNoDynConnVolume *vol)
{
	int i;
	for (i=0; i<beacons->size; i++)
	{
		Beacon *b = beacons->storage[i];

		if(beaconNoDynConnVolIncludesBeacon(vol, b))
		{
			b->noDynamicConnections = true;
			eaPush(&vol->beacons, b);
		}
	}
}

void beaconNoDynConnVolClear(BeaconNoDynConnVolume *vol)
{
	FOR_EACH_IN_EARRAY(vol->beacons, Beacon, b)
	{
		b->noDynamicConnections = false;
	}
	FOR_EACH_END

	eaClearFast(&vol->beacons);
}

void beaconAddNoDynConnBox(const Mat4 world_mat, const Vec3 local_min, const Vec3 local_max, const void *ptrId)
{
	Vec3 world_min, world_max;
	BeaconNoDynConnVolume *vol = beaconFindNoDynConnVol(ptrId, true);

	if(!vol)
		return;  

	beaconNoDynConnVolClear(vol);

	vol->isBox = true;
	copyMat4(world_mat, vol->box.world_mat);
	copyVec3(local_min, vol->box.local_min);
	copyVec3(local_max, vol->box.local_max);

	mulBoundsAA(local_min, local_max, world_mat, world_min, world_max);
	beaconForEachBlockBounds(beaconStatePartitionGet(0, false), world_min, world_max, beaconNoDynConnVolGather, vol);
}

void beaconAddNoDynConnSphere(const Vec3 pos, F32 radius, const void *ptrId)
{
	BeaconNoDynConnVolume *vol = beaconFindNoDynConnVol(ptrId, true);

	if(!vol)
		return;  

	vol->isBox = false;
	copyVec3(pos, vol->sphere.pos);
	vol->sphere.radius = radius;

	beaconForEachBlock(beaconStatePartitionGet(0, false), pos, radius, radius, radius, beaconNoDynConnVolGather, vol);
}

void beaconRemoveNoDynConnVol(const void* ptrId)
{
	BeaconNoDynConnVolume *vol = beaconFindNoDynConnVol(ptrId, false);

	if(!vol)
		return;

	stashAddressRemovePointer(beaconNoDynConnVolumes, ptrId, NULL);
	beaconNoDynConnVolClear(vol);

	eaDestroy(&vol->beacons);
	free(vol);
}

void beaconConnReInit(void)
{
	if(!beaconNoDynConnVolumes)
		beaconNoDynConnVolumes = stashTableCreateAddress(10);
}

void destroyConnInfoList(BeaconDynamicInfoList *infoList)
{
	FOR_EACH_IN_EARRAY(infoList->infos, BeaconDynamicInfo, info)
		beaconDestroyDynamicInfo(&info);
	FOR_EACH_END;
	free(infoList);
}

void beaconClearDynConns(void)
{
	if(interactionToDynConnInfo)
		stashTableClearEx(interactionToDynConnInfo, NULL, destroyConnInfoList);
	devassert(stashGetCount(beaconConnToDynamicConnTable)==0);
}

void destroyQueuedCheck(BeaconQueuedDynConnCheck *check)
{
	free(check);
}

void beaconCheckDynConnQueue(void)
{
	int i;

	if(	queueCheckPaused ||
		!eaSize(&queuedChecks))
	{
		return;
	}

	if(!beaconIdSubIdLookup)
		beaconIdSubIdLookup = stashTableCreateAddress(10);

	PERFINFO_AUTO_START_FUNC();
	
	if(beaconDynConnAllowedToSet())
	{
		if(eaSize(&queuedChecks)>50)
			loadstart_printf("Building dynamic connections (%d)...", eaSize(&queuedChecks));

		if(eaSize(&queuedChecks)>100 && combatBeaconArray.size > 125000)
		{
			beaconCacheWCOHulls(queuedChecks);
		}

		for(i=0; i<eaSize(&queuedChecks); i++)
		{
			WorldCollObject *wco = queuedChecks[i]->wco;

			if(queuedChecks[i]->command==DYN_CONN_CREATE || queuedChecks[i]->command==DYN_CONN_SHOW)
			{
				if(wco)
				{
					WorldCollModelInstanceData *inst = NULL;
					BeaconWCOToIdSubId *idsubid = NULL;
					if(!wcoGetInstData(&inst, wco, WC_FILTER_BIT_MOVEMENT) || !inst->transient)
					{
						SAFE_FREE(inst);
						continue;
					}
					SAFE_FREE(inst);

					if(!stashAddressFindPointer(beaconIdSubIdLookup, wco, &idsubid))
					{
						idsubid = callocStruct(BeaconWCOToIdSubId);
						stashAddressAddPointer(beaconIdSubIdLookup, wco, idsubid, true);
					}
					idsubid->wco = wco;
					idsubid->id = queuedChecks[i]->id;
					idsubid->subId = queuedChecks[i]->subId;
				}
			}

			beaconCheckDynamicConnsSMD(	queuedChecks[i]->command,
										queuedChecks[i]->id,
										queuedChecks[i]->subId,
										queuedChecks[i]->smd,
										queuedChecks[i]->world_mat,
										queuedChecks[i]->partition);
		}

		beaconClearWCOHulls();

		if(eaSize(&queuedChecks)>50)
			loadend_printf("done");
	}

	eaClearEx(&queuedChecks, destroyQueuedCheck);
	
	PERFINFO_AUTO_STOP();
}

void beaconCleanDynConnQueue(WorldCollObject *wco)
{
	
}

static int clusterID = 0;

int beaconNPCClusterTraverserInternal(Beacon* beacon, int* curSize, int maxSize, int cluster){
	int i;
	int noAutoConnect = 0;

	beacon->NPCClusterProcessed = 1;
	beacon->userInt = cluster;
	(*curSize)++;

	if(beacon->NPCNoAutoConnect)
		noAutoConnect = 1;

	for(i = beacon->gbConns.size - 1; i >= 0; i--)
	{
		BeaconConnection* conn = beacon->gbConns.storage[i];
		devassertmsg(conn->destBeacon != beacon, "Found an NPC beacon with a connection to itself");
		if(conn->destBeacon->userInt != cluster)
			noAutoConnect |= beaconNPCClusterTraverserInternal(conn->destBeacon, curSize, maxSize, cluster);
		if(maxSize && *curSize > maxSize)
			return noAutoConnect;
	}

	return noAutoConnect;
}

int beaconNPCClusterTraverser(Beacon* beacon, int* curSize, int maxSize){
	return beaconNPCClusterTraverserInternal(beacon, curSize, maxSize, ++clusterID);
}

void beaconConnSetMovementStartCallback(BeaconConnMovementStartCallback func)
{
	beacon_conn_state.start = func;
}

void beaconConnSetMovementIsFinishedCallback(BeaconConnMovementIsFinishedCallback func)
{
	beacon_conn_state.isfinished = func;
}

void beaconConnSetMovementResultCallback(BeaconConnMovementResultCallback func)
{
	beacon_conn_state.result = func;
}

#endif


void beaconPauseDynConnQueueCheck(int pause)
{
#if !PLATFORM_CONSOLE
	queueCheckPaused = !!pause;
#endif
}

int beaconPathFindInBlock(int partitionId, Beacon *src, Beacon *dst, BeaconBlock *gridBlock)
{
	static AStarSearchData *data = NULL;
	BeaconPartitionData *srcPart = beaconGetPartitionData(src, partitionId, true);
	BeaconPartitionData *dstPart = beaconGetPartitionData(dst, partitionId, true);

	BeaconBlock *srcSubBlock = srcPart->block;
	BeaconBlock *dstSubBlock = dstPart->block;

	PERFINFO_AUTO_START_FUNC();

	assert(!srcSubBlock->isGridBlock);
	assert(!dstSubBlock->isGridBlock);
	assert(gridBlock->isGridBlock);

	beacon_state.beaconSearchInstance++;

	srcSubBlock->searchInstance = beacon_state.beaconSearchInstance;
	dstSubBlock->searchInstance = beacon_state.beaconSearchInstance;

	srcSubBlock->parentBlock->searchInstance = beacon_state.beaconSearchInstance;
	dstSubBlock->parentBlock->searchInstance = beacon_state.beaconSearchInstance;

	if(!data)
		data = createAStarSearchData();

	clearAStarSearchDataTempInfo(data);
	
	beaconSetPathFindEntityAsParameters(0, 0, false, false, 0, partitionId);
	beaconSetPathFindEntityNoRaised(true);

	data->nodeAStarInfoOffset = offsetof(Beacon, astarInfo);
	data->sourceNode = src;
	data->targetNode = dst;
	copyVec3(src->pos, data->sourcePos);
	copyVec3(dst->pos, data->targetPos);

	AStarSearch(data, &beacon_state.searchFuncsBeaconInBlock);

	PERFINFO_AUTO_STOP();

	return data->pathWasOutput;
}

int beaconBlockPathFindInGalaxy(int partitionId, BeaconBlock *srcBlock, BeaconBlock *dstBlock, int maxJumpHeight)
{
	static AStarSearchData *data = NULL;

	PERFINFO_AUTO_START_FUNC();

	beacon_state.beaconSearchInstance++;

	srcBlock->searchInstance = beacon_state.beaconSearchInstance;
	dstBlock->searchInstance = beacon_state.beaconSearchInstance;

	srcBlock->galaxy->searchInstance = beacon_state.beaconSearchInstance;
	dstBlock->galaxy->searchInstance = beacon_state.beaconSearchInstance;

	if(!data)
		data = createAStarSearchData();

	clearAStarSearchDataTempInfo(data);

	beaconSetPathFindEntityAsParameters(maxJumpHeight, 0, false, false, 0, partitionId);
	if(maxJumpHeight == 0)
		beaconSetPathFindEntityNoRaised(true);

	data->nodeAStarInfoOffset = offsetof(BeaconBlock, astarInfo);
	data->sourceNode = srcBlock;
	data->targetNode = dstBlock;
	copyVec3(srcBlock->pos, data->sourcePos);
	copyVec3(dstBlock->pos, data->targetPos);

	AStarSearch(data, &beacon_state.searchFuncsBlockInGalaxy);

	PERFINFO_AUTO_STOP();

	return data->pathWasOutput;
}

void beaconSubBlockCheckSplit(BeaconBlock *subBlock, BeaconBlock ***newBlocksOut)
{
	int i;
	static Array beacons;
	static Beacon **beaconProcList = NULL;
	BeaconBlock *curBlock = subBlock;
	int useGround = true;

	PERFINFO_AUTO_START_FUNC();

	if(beaconHasSpaceRegion(NULL))
		useGround = false;

	assert(subBlock->isSubBlock);

	clearArray(&beacons);
	arrayPushBackArray(&beacons, &subBlock->beaconArray);
	clearArray(&subBlock->beaconArray);

	// Clear block pointers
	for(i=0; i<beacons.size; i++)
	{
		Beacon* b = beacons.storage[i];
		BeaconPartitionData *bPart = beaconGetPartitionData(b, subBlock->partitionIdx, true);

		assert(bPart->block == subBlock);
		bPart->block = NULL;
	}

	for(i=0; i<beacons.size; i++)
	{
		Beacon* b = beacons.storage[i];
		BeaconPartitionData *bPart = beaconGetPartitionData(b, subBlock->partitionIdx, true);

		if(bPart->block != NULL)
			continue;		// Already tagged

		if(i==0)
			bPart->block = subBlock;

		if(bPart->block == NULL)
		{
			// Create new subBlock
			BeaconBlock *newBlock = beaconSubBlockCreate(subBlock->partitionIdx);

			newBlock->parentBlock = subBlock->parentBlock;
			newBlock->galaxy = subBlock->galaxy;
			arrayPushBack(&newBlock->galaxy->subBlockArray, newBlock);

			bPart->block = newBlock;

			eaPush(newBlocksOut, newBlock);
		}

		eaClearFast(&beaconProcList);
		eaPush(&beaconProcList, b);

		while(eaSize(&beaconProcList))
		{
			Beacon *proc = eaPop(&beaconProcList);
			BeaconPartitionData *procPart = beaconGetPartitionData(proc, subBlock->partitionIdx, true);
			
			arrayPushBack(&bPart->block->beaconArray, proc);

			if(useGround)
			{
				int j;

				for(j=0; j<proc->gbConns.size; j++)
				{
					BeaconConnection *conn = proc->gbConns.storage[j];
					BeaconConnection *revConn = NULL;
					BeaconPartitionData *dstPart = NULL;
					int connidx = -1;

					dstPart = beaconGetPartitionData(conn->destBeacon, subBlock->partitionIdx, true);

					// Different gridBlock beacons
					if(dstPart->block && dstPart->block->parentBlock != procPart->block->parentBlock)
						continue;

					if(beaconConnectionIsDisabled(proc, subBlock->partitionIdx, conn))
						continue;

					beaconHasGroundConnectionToBeacon(conn->destBeacon, proc, &connidx);
					if(connidx != -1)
						revConn = conn->destBeacon->gbConns.storage[connidx];

					if(!revConn || beaconConnectionIsDisabled(conn->destBeacon, subBlock->partitionIdx, revConn))
						continue;
					if(dstPart->block == NULL)
					{
						dstPart->block = procPart->block;

						eaPush(&beaconProcList, conn->destBeacon);
					}
					else
					{
						assert(dstPart->block == procPart->block ||
								arrayFindElement(&beacons, conn->destBeacon) == -1);
					}
				}
			}
			else
			{
				int j;

				for(j=0; j<proc->rbConns.size; j++)
				{
					BeaconConnection *conn = proc->rbConns.storage[j];
					BeaconConnection *revConn = NULL;
					BeaconPartitionData *dstPart = NULL;
					int connidx = -1;

					dstPart = beaconGetPartitionData(conn->destBeacon, subBlock->partitionIdx, true);

					// Different gridBlock beacons
					if(dstPart->block && dstPart->block->parentBlock != procPart->block->parentBlock)
						continue;

					if(fabs(vecY(proc->pos) - vecY(conn->destBeacon->pos))<5)
						continue;

					if(beaconConnectionIsDisabled(proc, subBlock->partitionIdx, conn))
						continue;

					if(conn->minHeight > 3)
						continue;

					revConn = beaconFindConnection(conn->destBeacon, proc, true);

					if(!revConn)
						continue;

					if(revConn->minHeight > 3)
						continue;

					if(beaconConnectionIsDisabled(conn->destBeacon, subBlock->partitionIdx, revConn))
						continue;
					if(dstPart->block == NULL)
					{
						dstPart->block = procPart->block;

						eaPush(&beaconProcList, conn->destBeacon);
					}
					else
					{
						assert(dstPart->block == procPart->block ||
							arrayFindElement(&beacons, conn->destBeacon) == -1);
					}
				}
			}
		}
	}

	PERFINFO_AUTO_STOP();
}

void beaconGalaxyCheckSplit(BeaconBlock *galaxy, BeaconBlock ***newBlocksOut)
{
	BeaconStatePartition *partition = beaconStatePartitionGet(galaxy->partitionIdx, true);
	static Array blockList;
	static BeaconBlock **blockProcList = NULL;
	int i;

	PERFINFO_AUTO_START_FUNC();

	clearArray(&blockList);
	arrayPushBackArray(&blockList, &galaxy->subBlockArray);	
	clearArray(&galaxy->subBlockArray);

	// Clear galaxy pointers
	for(i=0; i<blockList.size; i++)
	{
		BeaconBlock *block = blockList.storage[i];

		block->galaxy = NULL;
	}

	// Propagate and create new galaxies
	for(i=0; i<blockList.size; i++)
	{
		BeaconBlock* block = blockList.storage[i];
		F32 maxJumpHeight = galaxy->galaxySet * beaconGalaxyGroupJumpIncrement;

		if(block->galaxy != NULL)
			continue;
		
		if(i==0)
			block->galaxy = galaxy;

		if(block->galaxy == NULL)
		{
			BeaconBlock *newGalaxy = beaconGalaxyCreate(galaxy->partitionIdx, galaxy->galaxySet);
			newGalaxy->galaxy = galaxy->galaxy;
			newGalaxy->cluster = galaxy->cluster;
			if(newGalaxy->galaxy)
				arrayPushBack(&newGalaxy->galaxy->subBlockArray, newGalaxy);
			if(newGalaxy->cluster)
				arrayPushBack(&newGalaxy->cluster->subBlockArray, newGalaxy);
			arrayPushBack(&partition->combatBeaconGalaxyArray[newGalaxy->galaxySet], newGalaxy);

			block->galaxy = newGalaxy;

			eaPush(newBlocksOut, newGalaxy);
		}

		eaClearFast(&blockProcList);
		eaPush(&blockProcList, block);

		while(eaSize(&blockProcList))
		{
			int j;
			BeaconBlock *proc = eaPop(&blockProcList);

			arrayPushBack(&proc->galaxy->subBlockArray, proc);

			if(maxJumpHeight == 0)
			{
				for(j=0; j<proc->gbbConns.size; j++)
				{
					BeaconBlockConnection *conn = proc->gbbConns.storage[j];
					BeaconBlockConnection *revConn = NULL;
					BeaconBlock* dst = NULL;

					if(conn->blockCount == conn->connCount)
						continue;

					dst = conn->destBlock;

					// Different galaxy
					if(dst->galaxy && dst->galaxy->galaxy != proc->galaxy->galaxy)
						continue;

					revConn = beaconBlockGetConnection(dst, proc, false);

					if(!revConn || revConn->blockCount == revConn->connCount)
						continue;

					assert(!dst->isGridBlock && !dst->isCluster);

					if(dst->galaxy == NULL)
					{
						dst->galaxy = proc->galaxy;

						eaPush(&blockProcList, dst);
					}
					else
					{
						if (!(dst->galaxy == proc->galaxy || arrayFindElement(&blockList, dst) == -1))
						{
							filelog_printf("beaconizerLog", "gbb\n");
							filelog_printf("beaconizerLog", "blockList.size == %d\n", blockList.size);
							filelog_printf("beaconizerLog", "arrayFindElement(&blockList, dst) == %d\n", arrayFindElement(&blockList, dst));
							if (dst)
							{
								if (dst->galaxy)
								{
									filelog_printf("beaconizerLog", "dst->galaxy->pos == (%d,%d,%d)\n", (int)dst->galaxy->pos[0], (int)dst->galaxy->pos[1], (int)dst->galaxy->pos[2]);
									if (dst->galaxy->galaxy)
									{
										filelog_printf("beaconizerLog", "dst->galaxy->galaxy->pos == (%d,%d,%d)\n", (int)dst->galaxy->galaxy->pos[0], (int)dst->galaxy->galaxy->pos[1], (int)dst->galaxy->galaxy->pos[2]);
									}
									else
									{
										filelog_printf("beaconizerLog", "dst->galaxy->galaxy is NULL\n");
									}
								}
								else
								{
									filelog_printf("beaconizerLog", "dst->galaxy is NULL\n");
								}
							}
							else
							{
								filelog_printf("beaconizerLog", "dst is NULL\n");
							}
							if (proc)
							{
								filelog_printf("beaconizerLog", "proc->galaxy == (%d,%d,%d)\n", (int)proc->galaxy->pos[0], (int)proc->galaxy->pos[1], (int)proc->galaxy->pos[2]);
								filelog_printf("beaconizerLog", "proc->gbbConns.size == %d\n", proc->gbbConns.size);
								filelog_printf("beaconizerLog", "proc->isGalaxy == %d\n", proc->isGalaxy);
								filelog_printf("beaconizerLog", "proc->isSubBlock == %d\n", proc->isSubBlock);
							}
							else
							{
								filelog_printf("beaconizerLog", "proc is NULL\n");
							}
							filelog_printf("beaconizerLog", "maxJumpHeight == %d\n", (int)maxJumpHeight);
							filelog_printf("beaconizerLog", "i == %d\n", i);
							filelog_printf("beaconizerLog", "j == %d\n", j);
							if (revConn)
							{
								filelog_printf("beaconizerLog", "revConn->blockCount == %d\n", revConn->blockCount);
								filelog_printf("beaconizerLog", "revConn->connCount == %d\n", (int)revConn->connCount);
							}
							else
							{
								filelog_printf("beaconizerLog", "revConn is NULL\n");
							}
							filelog_printf("beaconizerLog", "\n");
							Errorf("beaconizerLog contains more information that may be useful to debugging this. Please forward this file to the current beaconizer programmer");
						}
					}
				}
			}
			else
			{
				for(j=0; j<proc->rbbConns.size; j++)
				{
					BeaconBlockConnection *conn = proc->rbbConns.storage[j];
					BeaconBlock* dst = conn->destBlock;
					BeaconBlockConnection *revConn = NULL;

					assert(!dst->isGridBlock && !dst->isCluster);

					// Different galaxy
					if(dst->galaxy && dst->galaxy->galaxy != proc->galaxy->galaxy)
						continue;

					if(conn->blockCount == conn->connCount)
						continue;

					if(conn->minHeight > maxJumpHeight)
						continue;

					revConn = beaconBlockGetConnection(dst, proc, true);

					if(!revConn || revConn->minHeight > maxJumpHeight)
						continue;

					if(dst->galaxy == NULL)
					{
						dst->galaxy = proc->galaxy;

						eaPush(&blockProcList, dst);
					}
					else
					{
						if (!(dst->galaxy == proc->galaxy || arrayFindElement(&blockList, dst) == -1))
						{
							filelog_printf("beaconizerLog", "rbb\n");
							filelog_printf("beaconizerLog", "blockList.size == %d\n", blockList.size);
							filelog_printf("beaconizerLog", "arrayFindElement(&blockList, dst) == %d\n", arrayFindElement(&blockList, dst));
							if (dst)
							{
								if (dst->galaxy)
								{
									filelog_printf("beaconizerLog", "dst->galaxy->pos == (%d,%d,%d)\n", (int)dst->galaxy->pos[0], (int)dst->galaxy->pos[1], (int)dst->galaxy->pos[2]);
									if (dst->galaxy->galaxy)
									{
										filelog_printf("beaconizerLog", "dst->galaxy->galaxy->pos == (%d,%d,%d)\n", (int)dst->galaxy->galaxy->pos[0], (int)dst->galaxy->galaxy->pos[1], (int)dst->galaxy->galaxy->pos[2]);
									}
									else
									{
										filelog_printf("beaconizerLog", "dst->galaxy->galaxy is NULL\n");
									}
								}
								else
								{
									filelog_printf("beaconizerLog", "dst->galaxy is NULL\n");
								}
							}
							else
							{
								filelog_printf("beaconizerLog", "dst is NULL\n");
							}
							if (proc)
							{
								filelog_printf("beaconizerLog", "proc->galaxy == (%d,%d,%d)\n", (int)proc->galaxy->pos[0], (int)proc->galaxy->pos[1], (int)proc->galaxy->pos[2]);
								filelog_printf("beaconizerLog", "proc->rbbConns.size == %d\n", proc->rbbConns.size);
								filelog_printf("beaconizerLog", "proc->isGalaxy == %d\n", proc->isGalaxy);
								filelog_printf("beaconizerLog", "proc->isSubBlock == %d\n", proc->isSubBlock);
							}
							else
							{
								filelog_printf("beaconizerLog", "proc is NULL\n");
							}
							filelog_printf("beaconizerLog", "maxJumpHeight == %d\n", (int)maxJumpHeight);
							filelog_printf("beaconizerLog", "i == %d\n", i);
							filelog_printf("beaconizerLog", "j == %d\n", j);
							if (revConn)
							{
								filelog_printf("beaconizerLog", "revConn->blockCount == %d\n", revConn->blockCount);
								filelog_printf("beaconizerLog", "revConn->connCount == %d\n", (int)revConn->connCount);
							}
							else
							{
								filelog_printf("beaconizerLog", "revConn is NULL\n");
							}
							filelog_printf("beaconizerLog", "\n");
							Errorf("beaconizerLog contains more information that may be useful to debugging this. Please forward this file to the current beaconizer programmer.");
						}
					}
				}
			}
		}
	}

	PERFINFO_AUTO_STOP();
}

void beaconClusterCheckSplit(BeaconBlock *cluster, BeaconBlock ***newBlocksOut)
{
	BeaconStatePartition *partition = beaconStatePartitionGet(cluster->partitionIdx, true);
	static Array blockList;
	static BeaconBlock **blockProcList = NULL;
	static ESet processedSet = NULL;
	int i;

	PERFINFO_AUTO_START_FUNC();

	clearArray(&blockList);
	arrayPushBackArray(&blockList, &cluster->subBlockArray);	
	clearArray(&cluster->subBlockArray);
	eSetClear(&processedSet);
	
	// Propagate and create new clusters
	for(i=0; i<blockList.size; i++)
	{
		BeaconBlock* block = blockList.storage[i];

		if(block->cluster != NULL)
			continue;

		if(i==0)
			block->cluster = cluster;

		if(block->cluster == NULL)
		{
			BeaconBlock *newCluster = beaconClusterCreate(cluster->partitionIdx);
			
			arrayPushBack(&partition->combatBeaconClusterArray, newCluster);

			block->cluster = newCluster;

			eaPush(newBlocksOut, newCluster);
		}

		eaClearFast(&blockProcList);
		eaPush(&blockProcList, block);
		eSetAdd(&processedSet, block);

		while(eaSize(&blockProcList))
		{
			int j;
			BeaconBlock *proc = eaPop(&blockProcList);

			arrayPushBack(&proc->cluster->subBlockArray, proc);

			for(j=0; j<proc->gbbConns.size; j++)
			{
				BeaconBlockConnection *conn = proc->gbbConns.storage[j];
				BeaconBlock *dst = conn->destBlock;
				BeaconBlockConnection *revConn = NULL;

				if(conn->blockCount == conn->connCount)
					continue;

				galaxyHasConnectionToGalaxy(dst, proc, &revConn);

				if(!revConn)
					continue;

				if(revConn->blockCount >= conn->connCount)
					continue;

				if(eSetFind(&processedSet, dst))
				{
					assert(dst->cluster == proc->cluster);
					continue;
				}
				else if(proc->cluster != dst->cluster)
					continue;

				// Assert that we're part of the splitting cluster
				assert(dst->cluster == cluster);  
				dst->cluster = proc->cluster;
				eaPush(&blockProcList, dst);
				eSetAdd(&processedSet, dst);
			}

			for(j=0; j<proc->rbbConns.size; j++)
			{
				BeaconBlockConnection *conn = proc->rbbConns.storage[j];
				BeaconBlock *dst = conn->destBlock;
				BeaconBlockConnection *revConn = NULL;

				if(conn->blockCount == conn->connCount)
					continue;

				galaxyHasConnectionToGalaxy(dst, proc, &revConn);

				if(!revConn)
					continue;

				if(revConn->blockCount >= conn->connCount)
					continue;

				if(eSetFind(&processedSet, dst))
				{
					assert(dst->cluster == proc->cluster);
					continue;
				}
				else if(proc->cluster != dst->cluster)
					continue;

				// Assert that we're part of the splitting cluster
				assert(dst->cluster == cluster);  
				dst->cluster = proc->cluster;
				eaPush(&blockProcList, dst);
				eSetAdd(&processedSet, dst);
			}
		}
	}

	PERFINFO_AUTO_STOP();
}

void beaconBlockConnectionSetDisabled(BeaconBlock *srcBlock, BeaconBlockConnection *blockConn, void *conn, int disabled)
{
	S32 checkParent = false;
	if(disabled)
	{
		if(blockConn->raised)
			beaconBlockConnectionRemoveConnectionData(blockConn, srcBlock->partitionIdx, disabled, conn);

		blockConn->blockCount++;

		if(blockConn->blockCount == blockConn->connCount)
			checkParent = true;
	}
	else
	{
		if(blockConn->raised)
		{
			if(!srcBlock->isGalaxy)
				beaconRaisedBlockConnectionAddBeaconConnection(srcBlock->partitionIdx, blockConn, conn, disabled);
			else
				beaconBlockConnectionAddGalaxyConnection(srcBlock->partitionIdx, blockConn, conn);
		}

		blockConn->blockCount--;

		if(blockConn->blockCount == blockConn->connCount - 1) // Just disabled
			checkParent = true;
	}

	if(checkParent)
	{
		BeaconBlock *srcGalaxy = srcBlock->galaxy;
		BeaconBlock *dstGalaxy = blockConn->destBlock->galaxy;
		BeaconBlockConnection *parentConn = beaconBlockGetConnection(srcGalaxy, dstGalaxy, blockConn->raised);

		if(parentConn)
			beaconBlockConnectionSetDisabled(srcGalaxy, parentConn, blockConn, disabled);
	}
}

void beaconConnectionSetDisabled(int partition, BeaconDynamicConnection *dynConn, int disabled, ESet* mergeSet, ESet* splitSet)
{
	BeaconDynamicConnectionPartition *dynConnPartition = beaconDynConnPartitionGet(dynConn, partition, true);
	Beacon *src = dynConn->source;
	BeaconPartitionData *srcPart = beaconGetPartitionData(src, partition, true);
	BeaconPartitionData *dstPart = beaconGetPartitionData(dynConn->conn->destBeacon, partition, true);

	BeaconBlock *srcBlock = srcPart->block;
	BeaconBlock *dstBlock = dstPart->block;

	BeaconBlockConnection *blockConn = beaconBlockGetConnection(srcBlock, dstBlock, dynConn->raised);

	if(disabled)
	{
		dynConnPartition->blockedCount++;

		if(dynConnPartition->blockedCount > 1)
			return;

		// Add the dynamic connection to the source beacon's list of disabled connections.
		eaPush(&srcPart->disabledConns, dynConn);
		dynConn->conn->disabled = true;

		// Add to split set
		eSetAdd(splitSet, dynConn);
		eSetRemove(mergeSet, dynConn);
	}
	else
	{
		// Once in a while an actor will be destroyed after its partition is recreated
		// Rather than try and figure that one out, just ignore it
		if(dynConnPartition->blockedCount <= 0)
			return;

		dynConnPartition->blockedCount--;

		if(dynConnPartition->blockedCount > 0)
			return;

		eaFindAndRemoveFast(&srcPart->disabledConns, dynConn);
		dynConn->conn->disabled = false;

		eSetAdd(mergeSet, dynConn);
	}

	if(blockConn)
		beaconBlockConnectionSetDisabled(srcBlock, blockConn, dynConn->conn, disabled);

	if(beaconDebugIsEnabled())
		beaconDebugDynConnChange(partition, src, dynConn->conn->destBeacon, dynConn->raised, !disabled);
}

static void beaconGetRebuildSet(ESet *rebuild, BeaconBlock ***blocks)
{
	int i;
	eSetClear(rebuild);
	FOR_EACH_IN_EARRAY(*blocks, BeaconBlock, block)
	{
		BeaconBlockConnection *conn = NULL;

		eSetAdd(rebuild, block);

		for(i=0; i<block->bbIncoming.size; i++)
		{
			BeaconBlockConnection *incConn = block->bbIncoming.storage[i];

			assert(incConn->destBlock == block);
			eSetAdd(rebuild, incConn->srcBlock);
		}
	}
	FOR_EACH_END;
}

static void beaconRemakeConnections(BeaconBlock ***blocks)
{
	int blockidx;
	static ESet rebuild = NULL;

	beaconGetRebuildSet(&rebuild, blocks);

	for(blockidx = eSetGetMaxSize(&rebuild) - 1; blockidx >= 0; blockidx--)
	{
		BeaconBlock *block = eSetGetValueAtIndex(&rebuild, blockidx);

		if(block == NULL)
			continue;

		if(block->isSubBlock)
			beaconSubBlockRemakeConnections(block);
		else if(block->isGalaxy)
			beaconGalaxyRemakeConnections(block);
		else if(block->isCluster)
			;
		else
			assert(0);
	}
}

int beaconRebuildCheckSplits(int partitionId, ESet splitSet)
{
	int splitidx;
	int blockidx;
	int splits = 0;
	int galaxySet;
	int clusteridx;
	static ESet blocks = NULL;
	static ESet galaxies = NULL;
	static ESet clusters = NULL;
	static BeaconBlock **allBlocks = NULL;
	static BeaconBlock **newBlocks = NULL;

	PERFINFO_AUTO_START_FUNC();

	eSetClear(&blocks);
	eSetClear(&galaxies);
	eSetClear(&clusters);

	// Build set of modified blocks
	for(splitidx = eSetGetMaxSize(&splitSet) - 1; splitidx >= 0; splitidx--)
	{
		BeaconDynamicConnection *dynConn = eSetGetValueAtIndex(&splitSet, splitidx);
		BeaconPartitionData *srcPart = NULL;
		BeaconPartitionData *dstPart = NULL;

		if(dynConn == NULL)
			continue;
		
		srcPart = beaconGetPartitionData(dynConn->source, partitionId, true);
		dstPart = beaconGetPartitionData(dynConn->conn->destBeacon, partitionId, true);

		// Technically speaking, this could just be: add block if same and have the later operation rebuild 
		// the set
		eSetAdd(&blocks, srcPart->block);
		eSetAdd(&blocks, dstPart->block);
	}

	eaClearFast(&allBlocks);
	for(blockidx = eSetGetMaxSize(&blocks) - 1; blockidx >= 0; blockidx--)
	{
		BeaconBlock *block = eSetGetValueAtIndex(&blocks, blockidx);

		if(block == NULL)
			continue;

		eaClearFast(&newBlocks);
		if(block->beaconArray.size > 1)
		{
			beaconSubBlockCheckSplit(block, &newBlocks);

			splits += eaSize(&newBlocks);
		}

		// Even if this block hasn't split, some neighbors may have
		eaPush(&allBlocks, block);
		eaPushEArray(&allBlocks, &newBlocks);

		// Unrelated to above code, but push galaxy in to avoid a second loop
		eSetAdd(&galaxies, block->galaxy);
	}

	beaconRemakeConnections(&allBlocks);

	for(galaxySet = 0; galaxySet < beacon_galaxy_group_count; galaxySet++)
	{
		int galaxyIdx;

		eaClearFast(&allBlocks);
		eSetClear(&blocks);
		for(galaxyIdx = eSetGetMaxSize(&galaxies) - 1; galaxyIdx >= 0; galaxyIdx--)
		{
			BeaconBlock *galaxy = eSetGetValueAtIndex(&galaxies, galaxyIdx);

			if(galaxy == NULL)
				continue;

			assert(galaxy->isGalaxy);
			assert(galaxy->galaxySet == galaxySet);

			eaClearFast(&newBlocks);
			if(galaxy->subBlockArray.size > 1)
			{
				beaconGalaxyCheckSplit(galaxy, &newBlocks);

				splits += eaSize(&newBlocks);
			}

			// Even if this galaxy hasn't split, some neighbors may have
			// TODO: This actually shouldn't be necessary unless there are new blocks, I think
			eaPush(&allBlocks, galaxy);
			eaPushEArray(&allBlocks, &newBlocks);

			// Again, unrelated to above code except to start the next galaxy set
			if(galaxy->galaxy)
				eSetAdd(&blocks, galaxy->galaxy);
			// And the cluster set
			if(galaxy->cluster)
				eSetAdd(&clusters, galaxy->cluster);
		}

		SWAPP(blocks, galaxies);

		beaconRemakeConnections(&allBlocks);
	}

	for(clusteridx = eSetGetMaxSize(&clusters) - 1; clusteridx >= 0; clusteridx--)
	{
		BeaconBlock *cluster = eSetGetValueAtIndex(&clusters, clusteridx);

		if(cluster == NULL)
			continue;

		eaClearFast(&newBlocks);
		if(cluster->subBlockArray.size > 1)
		{
			beaconClusterCheckSplit(cluster, &newBlocks);

			splits += eaSize(&newBlocks);
		}
	}

	PERFINFO_AUTO_STOP();

	return splits;
}

void beaconClusterMerge(BeaconBlock *srcCluster, BeaconBlock *dstCluster)
{
	int i;
	BeaconStatePartition *partition = beaconStatePartitionGet(srcCluster->partitionIdx, true);
	static BeaconBlock **rebuildList = NULL;

	PERFINFO_AUTO_START_FUNC();

	assert(srcCluster->isCluster);
	assert(dstCluster->isCluster);
	assert(srcCluster != dstCluster);

	eaClearFast(&rebuildList);
	eaPush(&rebuildList, dstCluster);
	for(i=0; i<srcCluster->bbIncoming.size; i++)
	{
		BeaconClusterConnection *conn = srcCluster->bbIncoming.storage[i];

		assert(conn->dstCluster == srcCluster);

		eaPushUnique(&rebuildList, conn->srcCluster);
	}

	// Remove from galaxy array
	arrayFindAndRemoveFast(&partition->combatBeaconClusterArray, srcCluster);

	// Move subblocks
	for(i=0; i<srcCluster->subBlockArray.size; i++)
	{
		BeaconBlock *srcBlock = srcCluster->subBlockArray.storage[i];

		srcBlock->cluster = dstCluster;
		arrayPushBack(&dstCluster->subBlockArray, srcBlock);
	}

	beaconRemakeAllClusterConnections(partition);	

	//if(beaconDebugIsEnabled())
	//	beaconDebugClusterDestroy(partition->id, srcCluster, dstCluster);
	assert(srcCluster->bbIncoming.size == 0);
	beaconClusterDestroy(srcCluster);

	PERFINFO_AUTO_STOP();
}

void beaconGalaxyMerge(BeaconBlock *dstGalaxy, BeaconBlock *srcGalaxy)
{
	int i;
	BeaconStatePartition *partition = beaconStatePartitionGet(dstGalaxy->partitionIdx, true);
	BeaconBlock *srcParent = srcGalaxy->galaxy, *dstParent = dstGalaxy->galaxy;
	BeaconBlock *srcCluster = srcGalaxy->cluster, *dstCluster = dstGalaxy->cluster;
	static BeaconBlock **rebuildList = NULL;

	PERFINFO_AUTO_START_FUNC();

	assert(srcGalaxy->isGalaxy);
	assert(dstGalaxy->isGalaxy);
	assert(dstGalaxy != srcGalaxy);

	eaClearFast(&rebuildList);
	eaPush(&rebuildList, dstGalaxy);
	for(i=0; i<srcGalaxy->bbIncoming.size; i++)
	{
		BeaconBlockConnection *conn = srcGalaxy->bbIncoming.storage[i];

		assert(conn->destBlock == srcGalaxy);

		eaPushUnique(&rebuildList, conn->srcBlock);
	}

	// Remove from parent galaxy
	if(srcGalaxy->galaxy)
		arrayFindAndRemoveFast(&srcGalaxy->galaxy->subBlockArray, srcGalaxy);
	else
		assert(srcGalaxy->galaxySet == beacon_galaxy_group_count - 1);

	// Remove from cluster
	if(srcGalaxy->cluster)
		arrayFindAndRemoveFast(&srcGalaxy->cluster->subBlockArray, srcGalaxy);

	// Remove from galaxy array
	arrayFindAndRemoveFast(&partition->combatBeaconGalaxyArray[srcGalaxy->galaxySet], srcGalaxy);

	// Move subblocks
	for(i=0; i<srcGalaxy->subBlockArray.size; i++)
	{
		BeaconBlock *srcBlock = srcGalaxy->subBlockArray.storage[i];

		srcBlock->galaxy = dstGalaxy;
		arrayPushBack(&dstGalaxy->subBlockArray, srcBlock);
	}

	FOR_EACH_IN_EARRAY(rebuildList, BeaconBlock, rebuild)
	{
		if(rebuild->isSubBlock)
			beaconSubBlockRemakeConnections(rebuild);
		else if(rebuild->isGalaxy)
			beaconGalaxyRemakeConnections(rebuild);
	}
	FOR_EACH_END;

	if(beaconDebugIsEnabled())
		beaconDebugGalaxyDestroy(partition->id, srcGalaxy, dstGalaxy);
	assert(srcGalaxy->bbIncoming.size == 0);
	beaconGalaxyDestroy(srcGalaxy);

	if(srcParent != dstParent)
		beaconGalaxyMerge(dstParent, srcParent);

	if(srcCluster != dstCluster)
	{
		assert(dstGalaxy->galaxySet == 0);
		beaconClusterMerge(srcCluster, dstCluster);
	}

	PERFINFO_AUTO_STOP();
}

void beaconSubBlockMerge(BeaconBlock *dst, BeaconBlock *src)
{
	int i;
	BeaconBlock *srcGalaxy = src->galaxy;
	BeaconBlock *dstGalaxy = dst->galaxy;
	static BeaconBlock **rebuildList = NULL;
	assert(src->isSubBlock);
	assert(dst->isSubBlock);

	assert(src->parentBlock == dst->parentBlock);

	eaClearFast(&rebuildList);
	eaPush(&rebuildList, dst);
	for(i=0; i<src->bbIncoming.size; i++)
	{
		BeaconBlockConnection *conn = src->bbIncoming.storage[i];

		assert(conn->destBlock == src);

		eaPushUnique(&rebuildList, conn->srcBlock);
	}

	// Remove from parent gridblock
	arrayFindAndRemoveFast(&src->parentBlock->subBlockArray, src);
	// Remove from containing galaxy
	arrayFindAndRemoveFast(&src->galaxy->subBlockArray, src);

	// Move beacons
	for(i=0; i<src->beaconArray.size; i++)
	{
		Beacon* b = src->beaconArray.storage[i];
		BeaconPartitionData *bPart = beaconGetPartitionData(b, dst->partitionIdx, true);

		bPart->block = dst;
		arrayPushBack(&dst->beaconArray, b);
	}
	clearArray(&src->beaconArray);
	
	FOR_EACH_IN_EARRAY(rebuildList, BeaconBlock, rebuild)
	{
		beaconSubBlockRemakeConnections(rebuild);
	}
	FOR_EACH_END;

	if(beaconDebugIsEnabled())
		beaconDebugSubBlockDestroy(srcGalaxy->partitionIdx, src, dst);
	assert(src->bbIncoming.size == 0);
	beaconSubBlockDestroy(src);

	if(srcGalaxy != dstGalaxy)
		beaconGalaxyMerge(srcGalaxy, dstGalaxy);
}

int beaconConnectionCheckMerge(int partitionId, Beacon *src, Beacon* dst, BeaconDynamicConnection *dynConn)
{
	int didMerge = false;
	int galaxySet = 0;
	BeaconPartitionData *srcPart = beaconGetPartitionData(src, partitionId, true);
	BeaconPartitionData *dstPart = beaconGetPartitionData(dst, partitionId, true);
	BeaconBlock *srcSubBlock = srcPart->block;
	BeaconBlock *dstSubBlock = dstPart->block;
	BeaconBlock *srcGalaxy = srcSubBlock->galaxy;
	BeaconBlock *dstGalaxy = dstSubBlock->galaxy;

	PERFINFO_AUTO_START_FUNC();

	// Check for merging subblocks in the same grid block
	if( srcSubBlock != dstSubBlock && 
		srcSubBlock->parentBlock == dstSubBlock->parentBlock && 
		!dynConn->raised && 
		beaconHasGroundConnectionToBeacon(dst, src, NULL))
	{
		didMerge = true;

		// Merge everything upwards
		beaconSubBlockMerge(srcSubBlock, dstSubBlock);

		PERFINFO_AUTO_STOP();
		return 1;
	}

	if(srcGalaxy != dstGalaxy &&
		!dynConn->raised &&
		beaconBlockGetConnection(dstGalaxy, srcGalaxy, false))
	{
		didMerge = true;

		// Merge everything upwards
		beaconGalaxyMerge(dstGalaxy, srcGalaxy);

		PERFINFO_AUTO_STOP();
		return 1;
	}

	if(!dynConn->raised)
		return 0;

	srcGalaxy = srcGalaxy->galaxy;
	dstGalaxy = dstGalaxy->galaxy;
	for(galaxySet = 1; galaxySet < beacon_galaxy_group_count; galaxySet++)
	{
		int maxJumpHeight = galaxySet * beaconGalaxyGroupJumpIncrement;
		BeaconBlockConnection *blockConnSrcDst;
		BeaconBlockConnection *blockConnDstSrc;
		if(srcGalaxy == dstGalaxy)
			break;  // Can't merge anymore
		
		blockConnSrcDst = beaconBlockGetConnection(srcGalaxy, dstGalaxy, true);
		blockConnDstSrc = beaconBlockGetConnection(dstGalaxy, srcGalaxy, true);

		if (!blockConnSrcDst || !blockConnDstSrc)
			continue;

		if(blockConnDstSrc->blockCount >= blockConnSrcDst->connCount)
			continue;

		if( maxJumpHeight > blockConnSrcDst->minJumpHeight &&
			maxJumpHeight > blockConnDstSrc->minJumpHeight)
		{
			// Merge everything upwards
			assert(blockConnSrcDst->connCount > blockConnSrcDst->blockCount);

			beaconGalaxyMerge(dstGalaxy, srcGalaxy);

			PERFINFO_AUTO_STOP();
			return 1;
		}

		srcGalaxy = srcGalaxy->galaxy;
		dstGalaxy = dstGalaxy->galaxy;
	}

	PERFINFO_AUTO_STOP();
	return 0;
}

int beaconRebuildCheckMerges(int partitionId, ESet mergeSet)
{
	int merges = 0;
	int mergeidx;
	for(mergeidx = eSetGetMaxSize(&mergeSet) - 1; mergeidx >= 0; mergeidx--)
	{
		BeaconDynamicConnection *dynConn = eSetGetValueAtIndex(&mergeSet, mergeidx);
		
		if(dynConn == NULL)
			continue;

		merges += beaconConnectionCheckMerge(partitionId, dynConn->source, dynConn->conn->destBeacon, dynConn);

		// When enabling connections, they need to be re-sorted.
		beaconSortConnsByTarget(dynConn->source, dynConn->raised);
	}

	return merges;
}

void beaconCheckBlocksNeedRebuild(int partitionId)
{
#if !PLATFORM_CONSOLE
	BeaconStatePartition *partition = eaGet(&beacon_state.partitions, partitionId);
	static ESet mergeSet = NULL;
	static ESet splitSet = NULL;

	if(!gEnableRebuild)
		return;

	if(partition && partition->needBlocksRebuilt)
	{
		int infoidx;
		int merges = 0, splits = 0;
		loadstart_printf("Checking beacons... ");

		partition->needBlocksRebuilt = 0;

		if(eSetGetCount(&partition->changedInfos) == 0)
		{
			loadend_printf("done (no changed remaining)");
			return;
		}

		eSetClear(&mergeSet);
		eSetClear(&splitSet);

		// Disable/enable everything first
		for(infoidx = eSetGetMaxSize(&partition->changedInfos) - 1; infoidx>=0; infoidx--)
		{
			BeaconDynamicInfo *info = eSetGetValueAtIndex(&partition->changedInfos, infoidx);
			BeaconDynamicInfoPartition *infopartition = NULL;

			if(!info)
				continue;

			infopartition = beaconDynamicInfoPartitionGet(info, partitionId, true);
			infopartition->connsLastRebuild = infopartition->connsEnabled;

			FOR_EACH_IN_EARRAY(info->conns, BeaconDynamicConnection, dynConn)
			{
				BeaconConnection *conn = dynConn->conn;
				BeaconDynamicConnectionPartition *dynConnPartition = beaconDynConnPartitionGet(dynConn, partitionId, false);
				
				if(!dynConnPartition)
					continue;

				beaconSortConnsByTarget(dynConn->source, dynConn->raised);

				beaconConnectionSetDisabled(partitionId, dynConn, !infopartition->connsEnabled, &mergeSet, &splitSet);
			}
			FOR_EACH_END;
		}
		eSetClear(&partition->changedInfos);

		// Check splits
		splits = beaconRebuildCheckSplits(partitionId, splitSet);

		// Check merges
		merges = beaconRebuildCheckMerges(partitionId, mergeSet);

		loadend_printf("done. (%d merged, %d split)", merges, splits);
	}
#endif
}


void beaconRebuildBlocks(int requireValid, int quiet, int partitionId){
#if !PLATFORM_CONSOLE
	if(!quiet)
		loadstart_printf("Rebuilding beacon blocks...");
	PERFINFO_AUTO_START("beaconRebuildBlocks", 1);
	{
		BeaconStatePartition *partition = beaconStatePartitionGet(partitionId, false);

		assert(partition);
		// Clear out the old block data.
	
		PERFINFO_AUTO_START("clear blocks", 1);
		
			beaconClearAllBlockData(partition);
					
		// Recreate the blocks.
		
		PERFINFO_AUTO_STOP_START("split", 1);
		
			beaconSplitBlocksAndGalaxies(partition, quiet);
		
		PERFINFO_AUTO_STOP_START("clusterize", 1);
		
			beaconClusterizeGalaxies(partition, requireValid, quiet);
		
		PERFINFO_AUTO_STOP_START("clusterconns", 1);

			beaconCreateAllClusterConnections(partition);

		PERFINFO_AUTO_STOP();
	}

	PERFINFO_AUTO_STOP();
	if(!quiet)
		loadend_printf(" done.");
#endif
}

void beaconSetForceDynConn(int force)
{
#if !PLATFORM_CONSOLE
	beacon_state.forceDynConn = !!force;
#endif
}

void bcnSetRebuildFlag(int partitionIdx)
{
	BeaconStatePartition *partition = beaconStatePartitionGet(partitionIdx, false);

	partition->needBlocksRebuilt = 1;
}

void DEFAULT_LATELINK_beaconGatherDoors(DoorConn ***doors)
{
	// do nothing
}
