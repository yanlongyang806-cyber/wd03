

#include "beacon.h"
#include "ESet.h"
#include "MemoryPool.h"
#include "partition_enums.h"
#include "WorldGrid.h"
#include "WorldLib.h"
#include "PhysicsSDK.h"
#include "BeaconClientServerPrivate.h"
#include "wlVolumes.h"
#include "Capsule.h"

int beacon_galaxy_group_count;

#if !PLATFORM_CONSOLE
//#include <stdlib.h>
//
//#include <memory.h>
//#include <string.h>
//#include <limits.h>
//#include <F32.h>

F32 combatBeaconGridBlockSize = 256;
Array combatBeaconArray;
Beacon** encounterBeaconArray;
char** invalidEncounterArray;

BeaconState beacon_state;

void beaconPartitionDeinit(BeaconStatePartition *partition);
void beaconPartitionDestroy(BeaconStatePartition *partition);

// Beacon

MP_DEFINE(Beacon);

Beacon* createBeacon(){
	MP_CREATE(Beacon, 4096);
	
	return MP_ALLOC(Beacon);
}

void destroyCombatBeacon(Beacon* beacon){
	destroyArrayPartialEx(&beacon->gbConns, destroyBeaconConnection);
	destroyArrayPartialEx(&beacon->rbConns, destroyBeaconConnection);

	FOR_EACH_IN_EARRAY(beacon->partitions, BeaconPartitionData, partition)
	{
		if(partition)
			beaconPartitionDataDestroy(beacon, partition);
	}
	FOR_EACH_END
	eaDestroy(&beacon->partitions);
	
	MP_FREE(Beacon, beacon);
}

// Same as wcRayCollide, but forces scene update on scene DNE
S32 beaconRayCollide(	int iPartitionIdx,
						WorldColl* wc,
						const Vec3 source,
						const Vec3 target,
						U32 shapeGroups,
						WorldCollCollideResults* resultsOut)
{
	int count = 0;
	WorldCollCollideResults staticResults;
	WorldCollCollideResults *results = resultsOut ? resultsOut : &staticResults;

	ZeroStruct(results);
	wcRayCollide(beaconGetActiveWorldColl(iPartitionIdx), source, target, shapeGroups, results);

#if !PSDK_DISABLED
	devassert(!beaconIsBeaconizer() || !results->errorFlags.noScene|| psdkSceneLimitReached());
#endif

	return results->hitSomething;
}

S32 beaconCapsuleCollide(	WorldColl* wc, 
							const Vec3 source, 
							const Vec3 target, 
							U32 shapeGroups, 
							WorldCollCollideResults* resultsOut)
{
	int count = 0;
	WorldCollCollideResults staticResults = {0};
	WorldCollCollideResults *results = resultsOut ? resultsOut : &staticResults;

	ZeroStruct(results);
	wcCapsuleCollide(wc, source, target, shapeGroups, results);

#if !PSDK_DISABLED
	devassert(!beaconIsBeaconizer() || !results->errorFlags.noScene || psdkSceneLimitReached());
#endif

	return results->hitSomething;
}

S32 beaconCapsuleCollideCheck(	WorldColl* wc, 
						 const Capsule *cap, 
						 const Vec3 source, 
						 U32 shapeGroups, 
						 WorldCollCollideResults* resultsOut)
{
	int count = 0;
	WorldCollCollideResults staticResults = {0};
	WorldCollCollideResults *results = resultsOut ? resultsOut : &staticResults;

	ZeroStruct(results);
	wcCapsuleCollideCheck(wc, cap, source, shapeGroups, results);
	
#if !PSDK_DISABLED
	devassert(!beaconIsBeaconizer() || !results->errorFlags.noScene || psdkSceneLimitReached());
#endif


	return results->hitSomething;
}

F32 beaconGetDistanceYToCollision(int iPartitionIdx, const Vec3 pos, F32 dy){
	Vec3 pos2;
	WorldCollCollideResults results;
	
	copyVec3(pos, pos2);
	pos2[1] += dy;

	if(beaconRayCollide(iPartitionIdx, beaconGetActiveWorldColl(iPartitionIdx), pos, pos2, WC_FILTER_BIT_MOVEMENT, &results))
	{
		return fabs(results.posWorldImpact[1] - pos[1]);
	}

	return fabs(dy);
}

F32 beaconGetJitteredPointFloorDistance(int iPartitionIdx, const Vec3 posParam){
	#if 1
		const F32 extraHeight = 1.0f;
	#else
		const F32 extraHeight = 3.5f;
	#endif
	Vec3 pos, pos_jitter;
	F32 distance;
	F32 distance_jitter;
	int i, j;

	copyVec3(posParam, pos);
	
	pos[1] += extraHeight;

	PERFINFO_AUTO_START("beaconGetFloorDistance", 1);
		distance = beaconGetDistanceYToCollision(iPartitionIdx, pos, -5000) - extraHeight;
		for(i=0; i<2; i++)
		{
			for(j=0; j<2; j++)
			{
				copyVec3(pos, pos_jitter);
				pos_jitter[i ? 0 : 2] += j ? 0.01 : -0.01;
				distance_jitter = beaconGetDistanceYToCollision(iPartitionIdx, pos_jitter, -5000) - extraHeight;

				if(distance==4999.0)
					distance = distance_jitter;
				else if(distance_jitter!=4999.0)
					MAX1(distance, distance_jitter);
			}
		}
	PERFINFO_AUTO_STOP();
	
	if(distance <= 0.0f){
		distance = 0.0001f;
	}
	
	return distance;
}

F32 beaconGetFloorDistance(int iPartitionIdx, Beacon* b)
{
	return b->floorDistance;
}

F32 beaconGetJitteredPointCeilingDistance(int iPartitionIdx, const Vec3 posParam){
	Vec3 pos, pos_jitter;
	F32 distance;
	F32 distance_jitter;
	int i, j;

	copyVec3(posParam, pos);
	
	PERFINFO_AUTO_START("beaconGetCeilingDistance", 1);
		distance = beaconGetDistanceYToCollision(iPartitionIdx, pos, 30000);
		for(i=0; i<2; i++)
		{
			for(j=0; j<2; j++)
			{
				copyVec3(pos, pos_jitter);
				pos_jitter[i ? 0 : 2] += j ? 0.01 : -0.01;
				distance_jitter = beaconGetDistanceYToCollision(iPartitionIdx, pos_jitter, 30000);

				MIN1(distance, distance_jitter);
			}
		}
	PERFINFO_AUTO_STOP();
	
	if(distance <= 0){
		distance = 0.0001;
	}
	
	return distance;
}

F32 beaconGetCeilingDistance(int iPartitionIdx, Beacon* b){
	if(!*(S32*)&b->ceilingDistance){
		b->ceilingDistance = beaconGetJitteredPointCeilingDistance(iPartitionIdx, b->pos);
	}
	
	return b->ceilingDistance;
}

void beaconClearBeaconData(void)
{
	BeaconStatePartition *partition = beaconStatePartitionGet(0, true);
	assert(eaSize(&beacon_state.partitions)==1);

	// Init combat beacons.
	beaconClearDynConns();

	beaconPartitionDeinit(partition);

	destroyArrayPartialEx(&combatBeaconArray, destroyCombatBeacon);
}

void beaconForEachBlockBounds(BeaconStatePartition *partition, const Vec3 posLow, const Vec3 posHigh, BeaconForEachBlockCallback func, void* userData){
	S32 x, y, z;
	S32 lo[3];
	S32 hi[3];
	Vec3 posMin;
	Vec3 posMax;

	copyVec3(posLow, posMin);
	copyVec3(posHigh, posMax);
	
	beaconMakeGridBlockCoords(posMin);
	beaconMakeGridBlockCoords(posMax);
	
	copyVec3(posMin, lo);
	copyVec3(posMax, hi);

	for(x = lo[0]; x <= hi[0]; x++){
		for(y = lo[1]; y <= hi[1]; y++){
			for(z = lo[2]; z <= hi[2]; z++){
				BeaconBlock* block = beaconGetGridBlockByCoords(partition, x, y, z, 0);
				
				if(block){
					func(&block->beaconArray, userData);
				}
			}
		}
	}	
}

void beaconForEachBlockBoundsIntPartition(int partitionIdx, const Vec3 posLow, const Vec3 posHigh, BeaconForEachBlockCallback func, void* userData)
{
	BeaconStatePartition *partition = beaconStatePartitionGet(partitionIdx, false);

	beaconForEachBlockBounds(partition, posLow, posHigh, func, userData);
}

void beaconForEachBlock(BeaconStatePartition *partition, const Vec3 pos, F32 rx, F32 ry, F32 rz, BeaconForEachBlockCallback func, void* userData){
	Vec3 posLow =	{ pos[0] - rx, pos[1] - ry, pos[2] - rz };
	Vec3 posHigh =	{ pos[0] + rx, pos[1] + ry, pos[2] + rz };

	beaconForEachBlockBounds(partition, posLow, posHigh, func, userData);
}

void beaconForEachBlockIntPartition(int partitionIdx, const Vec3 pos, F32 rx, F32 ry, F32 rz, BeaconForEachBlockCallback func, void* userData)
{
	BeaconStatePartition *partition = beaconStatePartitionGet(partitionIdx, false);

	beaconForEachBlock(partition, pos, rx, ry, rz, func, userData);
}

void beaconTraverseBlocks(BeaconStatePartition* partition, const Vec3 posLow, const Vec3 posHigh, F32 buffer, BeaconBlockTraverseCallback func, void* userdata)
{
	S32 x, y, z;
	S32 lo[3];
	S32 hi[3];
	Vec3 posMin;
	Vec3 posMax;

	copyVec3(posLow, posMin);
	copyVec3(posHigh, posMax);

	subVec3same(posMin, buffer, posMin);
	addVec3same(posMax, buffer, posMax);

	beaconMakeGridBlockCoords(posMin);
	beaconMakeGridBlockCoords(posMax);

	copyVec3(posMin, lo);
	copyVec3(posMax, hi);

	for(x = lo[0]; x <= hi[0]; x++){
		for(y = lo[1]; y <= hi[1]; y++){
			for(z = lo[2]; z <= hi[2]; z++){
				BeaconBlock* block = beaconGetGridBlockByCoords(partition, x, y, z, 0);

				if(block){
					func(block, userdata);
				}
			}
		}
	}	
}

static struct {
	Vec3		pos;
	Beacon*		closestBeacon;
	F32			minDistSQR;
} collectClusterBeacons;

static void beaconGetNearestBeaconHelper(Array* beaconArray, void* userData){
	S32 i;
	S32 count = beaconArray->size;
	Beacon** beacons = (Beacon**)beaconArray->storage;
	
	for(i = 0; i < count; i++){
		Beacon* beacon = beacons[i];
		F32	distSQR = distance3Squared(beacon->pos, collectClusterBeacons.pos);
		
		if(distSQR < collectClusterBeacons.minDistSQR){
			collectClusterBeacons.closestBeacon = beacon;
			collectClusterBeacons.minDistSQR = distSQR;
		}
	}
}

Beacon* beaconGetNearestBeacon(int partitionIdx, const Vec3 pos)
{
	BeaconStatePartition *partition = beaconStatePartitionGet(partitionIdx, false);
	copyVec3(pos, collectClusterBeacons.pos);
	collectClusterBeacons.closestBeacon = NULL;
	collectClusterBeacons.minDistSQR = SQR(50);

	beaconCheckBlocksNeedRebuild(partitionIdx);

	beaconForEachBlock(partition, pos, 50, 1000, 50, beaconGetNearestBeaconHelper, NULL);
	
	if(!collectClusterBeacons.closestBeacon)
	{
		copyVec3(pos, collectClusterBeacons.pos);
		collectClusterBeacons.closestBeacon = NULL;
		collectClusterBeacons.minDistSQR = SQR(300);

		beaconForEachBlock(partition, pos, 350, 1000, 350, beaconGetNearestBeaconHelper, NULL);

		if(!collectClusterBeacons.closestBeacon)
		{
			copyVec3(pos, collectClusterBeacons.pos);
			collectClusterBeacons.closestBeacon = NULL;
			collectClusterBeacons.minDistSQR = SQR(5000);

			beaconForEachBlock(partition, pos, 5000, 1000, 5000, beaconGetNearestBeaconHelper, NULL);
		}
	}
	
	return collectClusterBeacons.closestBeacon;
}

//S32 beaconEntitiesInSameCluster(Entity* e1, Entity* e2){
//	Beacon* b1 = entGetNearestBeacon(e1);
//	Beacon* b2 = entGetNearestBeacon(e2);
//
//	// One or both are in an unbeaconed area.
//	
//	if(!b1 || !b2)
//		return 0;
//
//	if(b1->block->galaxy->cluster == b2->block->galaxy->cluster)
//		return 1;
//
//	return 0;
//}


MP_DEFINE(BeaconAvoidNode);

static BeaconAvoidNode* createBeaconAvoidNode(){
	MP_CREATE(BeaconAvoidNode, 500);
	
	return MP_ALLOC(BeaconAvoidNode);
}

static void destroyBeaconAvoidNode(BeaconAvoidNode* node){
	MP_FREE(BeaconAvoidNode, node);
}


BeaconAvoidNode* beaconAddAvoidNode(Beacon* beacon, Entity* e, AIVolumeEntry *entry, int partitionIdx, BeaconAvoidCheckBits bits, BeaconAvoidNode ***onList)
{
	BeaconAvoidNode* node = createBeaconAvoidNode();
	BeaconPartitionData *partition;

	devassert(e || entry);

	partition = beaconGetPartitionData(beacon, partitionIdx, true);
	
	node->beacon = beacon;
	node->e = e;
	node->entry = entry;
	node->partitionIdx = partitionIdx;
	node->avoidCheckBits = bits;
	node->onList = onList;

	eaPush(&partition->avoidNodes, node);
	
	return node;
}

void beaconDestroyAvoidNode_CalledFromAI(BeaconAvoidNode* node)
{
	Beacon* beacon = node->beacon;
	BeaconPartitionData *partition = beaconGetPartitionData(beacon, node->partitionIdx, false);

	// During map reload, the beacon system could already have destroyed the partition and another system
	//  is trying to destroy its owned avoidNodes, so 'partition' may not be legal.
	if(partition)
		eaFindAndRemoveFast(&partition->avoidNodes, node);	

	destroyBeaconAvoidNode(node);
}

void beaconDestroyAvoidNode_CalledFromBeacon(BeaconAvoidNode* node)
{
	if (node->onList)
		eaFindAndRemoveFast(node->onList, node);

	destroyBeaconAvoidNode(node);
}

#endif

WorldColl* beaconGetWorldColl(WorldColl *wc)
{
#if !PLATFORM_CONSOLE
	if(beaconIsClient())
	{
		return beaconClientGetWorldColl();
	}
	else
	{
		return wc;
	}
#else
	return NULL;
#endif
}

void beaconInit(void)
{
#if !PLATFORM_CONSOLE
	assert(!beacon_state.mainThreadId);
	beacon_state.mainThreadId = GetCurrentThreadId();
	beacon_galaxy_group_count = gConf.uBeaconizerJumpHeight;
	beaconPathStartup();
#endif
}

void beaconOncePerFrame(void)
{
#if !PLATFORM_CONSOLE
	PERFINFO_AUTO_START_FUNC();
	beaconCheckDynConnQueue();

	beaconCheckPathfindQueue();
	PERFINFO_AUTO_STOP();
#endif
}

void beaconSetPartitionCallbacks(BeaconPerPartitionCallback func)
{
	beacon_state.perPartitionCB = func;
}

void beaconMapLoad(ZoneMap *zmap, S32 fullInit)
{
	if(fullInit)
		eaDestroyEx(&beacon_state.partitions, beaconPartitionDestroy);

	beaconCheckDynConnQueue();

	beacon_state.perPartitionCB(beaconPartitionLoad);
}

void beaconMapUnload(void)
{
	eaDestroyEx(&beacon_state.partitions, beaconPartitionDestroy);

	beaconClearDynConns();
}

void beaconBlockCopy(BeaconBlock *dst, BeaconBlock *src)
{
	dst->isGridBlock = src->isGridBlock;
	dst->isCluster = src->isCluster;
	dst->isGalaxy = src->isGalaxy;
	copyVec3(src->pos, dst->pos);
	beaconInitCopyArray(&dst->beaconArray, &src->beaconArray);
}

void beaconPartitionInitialize(BeaconStatePartition *partition)
{
	int i;
	BeaconStatePartition *base = beaconStatePartitionGet(0, false);
	int subblockidx = 0;
	assert(base);

	if(partition->id==0)
		return;

	if(partition->initialized)
		return;
	partition->initialized = true;

	partition->combatBeaconGridBlockTable = stashTableCreateInt(64);

	for(i=0; i<base->combatBeaconGridBlockArray.size; i++)
	{
		BeaconBlock *block = beaconGridBlockCreate(partition->id);
		BeaconBlock *baseBlock = base->combatBeaconGridBlockArray.storage[i];
		
		beaconBlockCopy(block, baseBlock);
		stashIntAddPointer(partition->combatBeaconGridBlockTable,
							beaconMakeGridBlockHashValue(vecParamsXYZ(block->pos)),
							block, false);
		arrayPushBack(&partition->combatBeaconGridBlockArray, block);
	}

	beaconRebuildBlocks(0, 1, partition->id);
}

void beaconPartitionDeinit(BeaconStatePartition *partition)
{
	int i;

	partition->initialized = 0;

	for(i=0; i<combatBeaconArray.size; i++)
	{
		Beacon *b = combatBeaconArray.storage[i];
		BeaconPartitionData *beaconPartition = beaconGetPartitionData(b, partition->id, false);

		if(!beaconPartition)
			continue;
		
		beaconPartitionDataDestroy(b, beaconPartition);
	}

	// Create the hash table for looking up grid blocks.
	stashTableDestroy(partition->combatBeaconGridBlockTable);
	partition->combatBeaconGridBlockTable = NULL;

	// Create the array for indexed lookups on the grid blocks.
	destroyArrayPartialEx(&partition->combatBeaconGridBlockArray, beaconGridBlockDestroy);

	// destroy combat beacon galaxy
	for(i = 0; i < beacon_galaxy_group_count; i++)
	{
		destroyArrayPartialEx(&partition->combatBeaconGalaxyArray[i], beaconGalaxyDestroy);
	}

	// Clear the galaxy and cluster arrays.
	beaconClearAllBlockData(partition);

	// Clear all the beacon memory.
	beaconConnectionClearBadness(partition);
}

void beaconPartitionDestroy(BeaconStatePartition *partition)
{
	if(partition!=&beacon_state.basePartition)
	{
		eSetDestroy(&partition->changedInfos);
		beaconPartitionDeinit(partition);
		free(partition);
	}
}

BeaconStatePartition* beaconStatePartitionGet(int partitionId, int create)
{
	BeaconStatePartition *partition = NULL;

	if(!beacon_state.partitions)
		eaSet(&beacon_state.partitions, &beacon_state.basePartition, 0);

	if(combatBeaconArray.size>BEACON_PARTITION_LIMIT)
		return &beacon_state.basePartition;

	if(!beacon_state.basePartition.combatBeaconGridBlockTable)
		beacon_state.basePartition.combatBeaconGridBlockTable = stashTableCreateInt(64);

	partition = eaGet(&beacon_state.partitions, partitionId);

	if(partition || !create)
		return partition;

	partition = callocStruct(BeaconStatePartition);
	partition->id = partitionId;
	eSetCreate(&partition->changedInfos, 0);
	eaSet(&beacon_state.partitions, partition, partitionId);
	return partition;
}

void beaconPartitionLoad(int partitionIdx)
{
	BeaconStatePartition *partition = NULL;
	if(combatBeaconArray.size > BEACON_PARTITION_LIMIT)
		return;  // Don't deal with partitions on huge maps

	PERFINFO_AUTO_START_FUNC();

	partition = beaconStatePartitionGet(partitionIdx, true);

	beaconPartitionInitialize(partition);

	PERFINFO_AUTO_STOP();
}

void beaconPartitionUnload(int partitionIdx)
{
	BeaconStatePartition *partition = NULL;
	if(combatBeaconArray.size > BEACON_PARTITION_LIMIT)
		return;  // Don't deal with partitions on huge maps

	partition = beaconStatePartitionGet(partitionIdx, true);

	beaconPartitionDestroy(partition);
	eaSet(&beacon_state.partitions, NULL, partitionIdx);
}

void DEFAULT_LATELINK_beaconGatherSpawnPositions(int spawn_only)
{
	// Do nothing
}

static WorldVolumeEntry *wcoGetVolumeEntry(WorldCollObject *wco)
{
	WorldVolumeEntry *entry;
	
	if(wco && wcoGetUserPointer(wco, volumeCollObjectMsgHandler, &entry)){
		return entry;
	}

	return NULL;
}

// This function tries to determine whether the given position is a valid place
// for an entity to be, i.e. not stuck in terrain, out of bounds, etc.
bool beaconIsPositionValid(int iPartitionIdx, const Vec3 pos, const Capsule *cap)
{
#if !PLATFORM_CONSOLE
	static U32 playable_volume_type = 0;
	WorldCollCollideResults results = {0};
	Beacon *beacon = NULL;
	Vec3 centerPos = {0};
	Vec3 groundPos = {0};
	Capsule collideCap = (cap?(*cap):defaultCapsule);

	if(!playable_volume_type){
		playable_volume_type = wlVolumeTypeNameToBitMask("Playable");
	}

	// Martin tells me that the capsule for world collision is hardcoded to have a 1 foot radius, so this
	// matches that.  Please forward any complaints directly to Martin.
	// But it actually allows sliding up to 0.6666.
	collideCap.fRadius = 0.6;

	// Ensure that the starting height is at least radius units off the ground (plus a margin of error)
	MAX1(collideCap.vStart[1], collideCap.fRadius+0.01);

	copyVec3(pos, centerPos);
	addVec3(pos, collideCap.vStart, centerPos);
	scaleAddVec3(collideCap.vDir, collideCap.fLength/2, centerPos, centerPos);

	// Check for capsule collision - should catch most "stuck in a crate" issues
	if (beaconCapsuleCollideCheck(beaconGetActiveWorldColl(iPartitionIdx), &collideCap, pos, WC_FILTER_BIT_MOVEMENT, &results)){
		return false;
	}

	// If there are no beacons, we have to consider all positions valid
	if (combatBeaconArray.size == 0){
		return true;
	}

	// See if there are any nearby beacons with LOS
	beaconSetPathFindEntityAsParameters(6, 6, 0, 0, 0, iPartitionIdx);
	beacon = beaconGetClosestCombatBeacon(iPartitionIdx, centerPos, NULL, 1, NULL, GCCB_REQUIRE_LOS, NULL);

	if (!beacon){
		// Check near ground for beacons with LOS
		copyVec3(centerPos, groundPos);
		groundPos[1] -= 10000.f;

		beaconRayCollide(iPartitionIdx, beaconGetActiveWorldColl(iPartitionIdx), centerPos, groundPos, WC_FILTER_BIT_MOVEMENT, &results);

		if (results.hitSomething){
			WorldVolumeEntry *pVolEntry = wcoGetVolumeEntry(results.wco);

			// If it hit the bottom of the playable volume, return false - this player is underneath the world
			if (pVolEntry && pVolEntry->eaVolumes[iPartitionIdx] && wlVolumeIsType(pVolEntry->eaVolumes[iPartitionIdx], playable_volume_type)){
				return false;
			}

			copyVec3(results.posWorldImpact, groundPos);
			if (!beaconRayCollide(iPartitionIdx, beaconGetActiveWorldColl(iPartitionIdx), groundPos, centerPos, WC_FILTER_BIT_MOVEMENT, NULL)){
				groundPos[1] += 1.f;
				beacon = beaconGetClosestCombatBeacon(iPartitionIdx, groundPos, NULL, 1, NULL, GCCB_REQUIRE_LOS, NULL);
			}
		}
	}

	return (beacon != NULL);
#else
	return true;
#endif

}

#include "Autogen/beacon_h_ast.c"
