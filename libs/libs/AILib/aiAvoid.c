#include "aiAvoid.h"

#include "aiDebug.h"
#include "aiLib.h"
#include "aiMovement.h"

#include "Character.h"
#include "Character_target.h"
#include "beaconPath.h"
#include "EntityMovementManager.h"
#include "gslVolume.h"
#include "gslMapState.h"
#include "LineDist.h"
#include "MemoryPool.h"
#include "wlVolumes.h"
#include "WorldGrid.h"
#include "bounds.h"

#include "aiStruct_h_ast.h"

#define AI_AVOID_FUDGE_RADIUS 0.5

typedef int (*AIVolumeCompare)(AIVolumeInstance *volume, void *p);
typedef void (*AIRemoveInstanceCallback)(Entity *be, AIVolumeInstance* volume);
typedef void (*AIProxEntryCallback)(AIVolumeEntry* entry, Entity *e, AIStatusTableEntry *status);

AIVolumeInstance* aiVolume_FindByUid(AIVolumeInfo *volumeInfo, U32 uid);
AIVolumeInstance* aiVolume_Find(AIVolumeInfo *volumeInfo, AIVolumeCompare cmp, void *p);
void aiVolume_AddInstance(AIVolumeInfo *volumeInfo, AIVolumeInstance *volume);
void aiVolume_RemoveInstance(Entity *be, AIVolumeInfo *volumeInfo, U32 uid, AIRemoveInstanceCallback cb);
void aiVolume_UpdateInstance(AIVolumeInfo *volumeInfo, U32 oldUid, U32 newUid);
void aiVolume_RemoveAllInstances(AIVolumeInfo *volumeInfo);
void aiVolume_AddProximityVolumeEntry(Entity *e, AIVolumeInfo *volumeInfo, const AIVolumeInfo *srcVolumeInfo, AIStatusTableEntry *srcStatus, AIProxEntryCallback cb);
void aiVolume_RemoveProximityVolumeEntry(Entity* be, AIVolumeInfo *volumeInfo, AIStatusTableEntry* status, AIProxEntryCallback cb);


static void aiAvoidRemoveNodes(AIVolumeAvoidInfo *volumeInfo)
{
	eaDestroyEx(&volumeInfo->placed.avoidNodes, beaconDestroyAvoidNode_CalledFromAI);
}

static void aiAvoidUpdateHelper(Array* beacons, Entity* e)
{
	AIVarsBase* aib = e->aibase;
	F32 avoidRadius = max(70.0, aib->avoid.base.maxRadius);
	int count = beacons->size;
	int i;

	for(i = 0; i < count; i++)
	{
		Beacon* b = beacons->storage[i];
		F32 dist = entGetDistance(e, NULL, NULL, b->pos, NULL);
		
		if(dist <= avoidRadius+100)
		{
			BeaconAvoidNode* node = NULL;
			BeaconAvoidCheckBits bits = 0;

			if(dist<aib->avoid.base.maxRadius)
				bits = BEACON_AVOID_POINT;
			else if(dist > aib->avoid.base.maxRadius+15)
				bits = BEACON_AVOID_LINE;
			else 
				bits = BEACON_AVOID_LINE | BEACON_AVOID_POINT;
			
			node = beaconAddAvoidNode(b, e, NULL, entGetPartitionIdx(e), bits, &aib->avoid.placed.avoidNodes);

			eaPush(&aib->avoid.placed.avoidNodes, node);
		}
	}	
}

static struct {
	Entity* e;
	Beacon* closestCombatBeacon;
}staticCheckAvoidBeacon;

static int aiCheckIfShouldAvoidBeacon(Beacon* beacon){
	if(aiShouldAvoidBeacon(entGetRef(staticCheckAvoidBeacon.e), beacon, 0))
		return 0;
	
	//TODO: add proper flying support
	return beaconGalaxyPathExists(staticCheckAvoidBeacon.closestCombatBeacon, beacon, 10, 0);
}

static struct {
	Vec3 pos;
	int maxCount;
	int curCount;
	float rxz;
	float ry;
	float minRangeSQR;
	float maxRangeSQR;
	Beacon** array;
} staticBeaconSearchData;

typedef int (*AIFindBeaconApproveFunction)(Beacon* beacon);

static void aiFindBeaconInRangeHelper(Array* beaconArray, AIFindBeaconApproveFunction approveFunc){
	int i;

	for(i = 0; i < beaconArray->size; i++){
		Beacon* b = beaconArray->storage[i];

		if(b->gbConns.size || b->rbConns.size){
			float distSQR = distance3Squared(b->pos, staticBeaconSearchData.pos);

			if(	staticBeaconSearchData.curCount < staticBeaconSearchData.maxCount &&
				distSQR >= staticBeaconSearchData.minRangeSQR &&
				distSQR <= staticBeaconSearchData.maxRangeSQR)
			{
				if(!approveFunc || approveFunc(b)){
					b->userFloat = distSQR;
					staticBeaconSearchData.array[staticBeaconSearchData.curCount++] = b;
				}
			}
		}
	}
}

Beacon* aiFindBeaconInRange(Entity* e,
							AIVarsBase* aib,
							const Vec3 centerPos,
							float rxz,
							float ry,
							float minRange,
							float maxRange,
							AIFindBeaconApproveFunction approveFunc)
{
	Beacon* array[1000];
	int partitionIdx = entGetPartitionIdx(e);

	aib->time.lastPathFind = ABS_TIME_PARTITION(partitionIdx);

	staticBeaconSearchData.array = array;
	staticBeaconSearchData.maxCount = ARRAY_SIZE(array);
	staticBeaconSearchData.curCount = 0;
	staticBeaconSearchData.rxz = rxz;
	staticBeaconSearchData.ry = ry;
	staticBeaconSearchData.minRangeSQR = SQR(minRange);
	staticBeaconSearchData.maxRangeSQR = SQR(maxRange);
	entGetPos(e, staticBeaconSearchData.pos);

	beaconForEachBlockIntPartition(partitionIdx, centerPos, rxz, ry, rxz, aiFindBeaconInRangeHelper, approveFunc);
	if(staticBeaconSearchData.curCount){
		int i;
		qsort(array, staticBeaconSearchData.curCount, sizeof(array[0]), beaconCompareUserfloat);

		i = rand() % (staticBeaconSearchData.curCount > 5 ? 5 : staticBeaconSearchData.curCount);

		return array[i];
	}

	return NULL;
}

void aiAvoidUpdate(Entity* be, AIVarsBase* aib)
{
	Vec3 myPos;
	int isInAvoidSphere = false;

	entGetPos(be, myPos);
	
	if(	*(int*)&aib->avoid.base.maxRadius != *(int*)&aib->avoid.placed.maxRadius
		||
		!sameVec3((int*)myPos, (int*)aib->avoid.placed.pos))
	{
		F32 range = aib->avoid.base.maxRadius+100;
		aiAvoidRemoveNodes(&aib->avoid);
		
		if(*(int*)&aib->avoid.base.maxRadius)
			beaconForEachBlockIntPartition(entGetPartitionIdx(be), myPos, range, range, range, aiAvoidUpdateHelper, be);
		
		aib->avoid.placed.maxRadius = aib->avoid.base.maxRadius;
		copyVec3(myPos, aib->avoid.placed.pos);
	}
}

Beacon* aiFindAvoidBeaconInRange(Entity* e, AIVarsBase* aib)
{
	Vec3 myPos;

	entGetPos(e, myPos);

	beaconSetPathFindEntity(entGetRef(e), 10, aiMovementGetPathEntHeight(e));
	staticCheckAvoidBeacon.e = e;
	staticCheckAvoidBeacon.closestCombatBeacon = beaconGetClosestCombatBeacon(entGetPartitionIdx(e), myPos, NULL, 1, NULL, GCCB_PREFER_LOS | GCCB_STARTS_IN_AVOID, NULL);

	if(!staticCheckAvoidBeacon.closestCombatBeacon)
		return NULL;
	else
	{
		F32 range = MAX(100, aib->avoid.base.maxRadius+75);
		return aiFindBeaconInRange(e, aib, myPos, range, range, aib->avoid.base.maxRadius+5, aib->avoid.base.maxRadius+75, aiCheckIfShouldAvoidBeacon);
	}
}

int aiShouldAvoidEntity(Entity* be, AIVarsBase* aib, Entity* target, AIVarsBase* aibTarget)
{
	// critter_IsKOS: a quick work-around to make entities not avoid friendly entities
	//	though, if there would be a case where we want friendlies to avoid each other for some reason, 
	//	like if a hostile power applied to a friendly that damages other nearby friendlies
	//	we will want to change this.
	if (critter_IsKOS(entGetPartitionIdx(be), be, target))
	{
		AIVolumeAvoidInstance* avoid;
		S32 levelDiff = be->pChar->iLevelCombat - target->pChar->iLevelCombat;

		avoid = (AIVolumeAvoidInstance*)aibTarget->avoid.base.list;
		while(avoid)
		{
			if(levelDiff <= avoid->maxLevelDiff)
				return 1;

			avoid = (AIVolumeAvoidInstance*)avoid->base.next;
		}
	}

	return 0;
}

int aiShouldSoftAvoidEntity(Entity* be, AIVarsBase* aib, Entity* target, AIVarsBase* aibTarget)
{
	// critter_IsKOS: a quick work-around to make entities not avoid friendly entities
	//	though, if there would be a case where we want friendlies to avoid each other for some reason, 
	//	like if a hostile power applied to a friendly that damages other nearby friendlies
	//	we will want to change this.
	if (critter_IsKOS(entGetPartitionIdx(be), be, target) && aibTarget->softAvoid.base.list)
	{
		return 1;
	}

	return 0;
}

int OVERRIDE_LATELINK_aiShouldAvoidBeacon(U32 ref, Beacon* beacon, F32 height)
{
	Vec3 myPos;
	Vec3 bcnPos;
	Entity* e = entFromEntityRefAnyPartition(ref);
	BeaconPartitionData *partition;
	AIConfig *config = NULL;

	if(!e || !e->pChar)
		return 0;

	partition = beaconGetPartitionData(beacon, entGetPartitionIdx(e), false);

	if(!partition)
		return 0;

	if(!e->aibase)
		return 0;

	config = aiGetConfig(e, e->aibase);
	if(config && config->movementParams.ignoreAvoid)
		return 0;

	entGetPos(e, myPos);
	copyVec3(beacon->pos, bcnPos);
	bcnPos[1] += height;
	
	FOR_EACH_IN_EARRAY(partition->avoidNodes, BeaconAvoidNode, node)
	{
		if(!(node->avoidCheckBits & BEACON_AVOID_POINT))
			continue;
		if(node->e && node->e!=e)
		{
			Entity* target = node->e;
			AIVarsBase* aibTarget = target->aibase;
			S32 levelDiff = e->pChar->iLevelCombat - target->pChar->iLevelCombat;
			F32 dist = entGetDistance(target, NULL, NULL, bcnPos, NULL);
			AIVolumeAvoidInstance* avoid;

			avoid = (AIVolumeAvoidInstance*)aibTarget->avoid.base.list;
			while(avoid)
			{
				if(dist <= avoid->base.radius+AI_AVOID_FUDGE_RADIUS && 
					levelDiff <= avoid->maxLevelDiff)
					return 1;
				avoid = (AIVolumeAvoidInstance*)avoid->base.next;
			}
		}
		else if(!node->e)
		{
			if(aiAvoidEntryCheckPoint(e, bcnPos, node->entry, true, NULL))
				return 1;
		}
	}
	FOR_EACH_END

	return 0;
}

int OVERRIDE_LATELINK_aiShouldAvoidLine(U32 ref, Beacon *bcnSrc, const Vec3 posSrc, Beacon *bcnDst, F32 height, BeaconPartitionData * pStartBeaconPartitionData)
{
	int i;
	Vec3 myPos;
	F32 myRadius;
	Vec3 bcnSrcPos, bcnDstPos, bcnLineDir;
	F32 bcnLineDist;
	Entity* e = entFromEntityRefAnyPartition(ref);
	BeaconPartitionData *partition = NULL;
	AIConfig *config = NULL;

	if(!e || !e->pChar)
		return 0;

	if(!e->aibase)
		return 0;

	config = aiGetConfig(e, e->aibase);
	if(config && config->movementParams.ignoreAvoid)
		return 0;
	
	entGetPos(e, myPos);
	if(posSrc)
		copyVec3(posSrc, bcnSrcPos);
	else
		copyVec3(bcnSrc->pos, bcnSrcPos);
	bcnSrcPos[1] += height;

	copyVec3(bcnDst->pos, bcnDstPos);
	bcnDstPos[1] += height;

	subVec3(bcnDstPos, bcnSrcPos, bcnLineDir);
	bcnLineDist = normalVec3(bcnLineDir);

	myRadius = entGetPrimaryCapsuleRadius(e);
	for(i=0; i<2; i++)
	{
		Beacon *beacon = !i ? bcnSrc : bcnDst;
		if(!beacon && i==0 && pStartBeaconPartitionData)
		{
			partition = pStartBeaconPartitionData;
		}
		else if (beacon)
		{
			partition = beaconGetPartitionData(beacon, entGetPartitionIdx(e), false);
		}

		if(!partition)
			return 0;

		FOR_EACH_IN_EARRAY(partition->avoidNodes, BeaconAvoidNode, node)
		{
			if(!(node->avoidCheckBits & BEACON_AVOID_LINE))
				continue;
			if(node->e && node->e!=e)
			{
				Entity* target = node->e;
				AIVarsBase* aibTarget = target->aibase;
				S32 levelDiff = e->pChar->iLevelCombat - target->pChar->iLevelCombat;
				F32 dist;
				AIVolumeAvoidInstance* avoid;
				dist = entLineDistance(bcnSrcPos, myRadius, bcnLineDir, bcnLineDist, target, NULL);

				avoid = (AIVolumeAvoidInstance*)aibTarget->avoid.base.list;
				while(avoid)
				{
					if(dist <= avoid->base.radius && levelDiff <= avoid->maxLevelDiff)
						return 1;
					avoid = (AIVolumeAvoidInstance*)avoid->base.next;
				}
			}
			else if(!node->e)
			{
				if (aiAvoidEntryCheckLine(e, bcnSrcPos, bcnDstPos, node->entry, 1, NULL))
				{
					return true;
				}
			}
		}
		FOR_EACH_END
	}

	return 0;
}


MP_DEFINE(AIVolumeAttractInstance);
MP_DEFINE(AIVolumeAvoidInstance);
MP_DEFINE(AIVolumeSoftAvoidInstance);

AIVolumeAvoidInstance* AIVolumeAvoidInstance_Create(int maxLevelDiff, F32 radius, U32 uid)
{
	AIVolumeAvoidInstance *avoid;
	MP_CREATE(AIVolumeAvoidInstance, 100);
	
	avoid = MP_ALLOC(AIVolumeAvoidInstance);
	StructInit(parse_AIVolumeAvoidInstance, avoid);
	avoid->maxLevelDiff = maxLevelDiff;
	avoid->base.radius = radius;
	avoid->base.uid = uid;

	// Set the ref count
	avoid->base.refCount = 1;

	return avoid;
}

void AIVolumeAvoidInstance_Destroy(AIVolumeAvoidInstance* avoid)
{
	MP_FREE(AIVolumeAvoidInstance, avoid);
}

AIVolumeSoftAvoidInstance* AIVolumeSoftAvoidInstance_Create(int magnitude, F32 radius, U32 uid)
{
	AIVolumeSoftAvoidInstance *avoid;
	MP_CREATE(AIVolumeSoftAvoidInstance, 100);

	avoid = MP_ALLOC(AIVolumeSoftAvoidInstance);
	StructInit(parse_AIVolumeSoftAvoidInstance, avoid);
	avoid->magnitude = magnitude;
	avoid->base.radius = radius;
	avoid->base.uid = uid;

	// Set the ref count
	avoid->base.refCount = 1;

	return avoid;
}

void AIVolumeSoftAvoidInstance_Destroy(AIVolumeSoftAvoidInstance* avoid)
{
	MP_FREE(AIVolumeSoftAvoidInstance, avoid);
}

AIVolumeAttractInstance* AIVolumeAttractionInstance_Create(F32 radius, U32 uid)
{
	AIVolumeAttractInstance *volume;
	MP_CREATE(AIVolumeAttractInstance, 100);

	volume = MP_ALLOC(AIVolumeAttractInstance);
	StructInit(parse_AIVolumeAttractInstance, volume);
	volume->base.radius = radius;
	volume->base.uid = uid;

	// Set the ref count
	volume->base.refCount = 1;

	return volume;
}

void AIVolumeAttractionInstance_Destroy(AIVolumeAttractInstance* avoid)
{
	MP_FREE(AIVolumeAttractInstance, avoid);
}


void aiVolumeInstance_Destroy(AIVolumeInstance* volume)
{
	if (volume->eType == AIVolumeType_AVOID)
	{
		AIVolumeAvoidInstance_Destroy((AIVolumeAvoidInstance*)volume);
	}
	else if (volume->eType == AIVolumeType_ATTRACT)
	{
		AIVolumeAttractionInstance_Destroy((AIVolumeAttractInstance*)volume);
	}
	else
	{
		devassert(volume->eType == AIVolumeType_SOFT_AVOID);
		AIVolumeSoftAvoidInstance_Destroy((AIVolumeSoftAvoidInstance*)volume);
	}
}

void aiAvoidAddInstance(Entity* be, AIVarsBase* aib, int maxLevelDiff, F32 radius, U32 uid)
{
	AIVolumeAvoidInstance* volume;
	AIVolumeInfo *volumeInfo = &aib->avoid.base;

	volume = (AIVolumeAvoidInstance*)aiVolume_FindByUid(volumeInfo, uid);
	if (volume)
	{
		// Increment the ref count
		volume->base.refCount++;

		volume->maxLevelDiff = maxLevelDiff;
		volume->base.radius = radius;
		return;
	}

	volume = AIVolumeAvoidInstance_Create(maxLevelDiff, radius, uid);

	aiVolume_AddInstance(volumeInfo, &volume->base);

	AI_DEBUG_PRINT(be, AI_LOG_COMBAT, 5, 
		"ADDED avoid instance %d: %d lvl diff at %1.1fft\n",
		volume->base.uid, volume->maxLevelDiff, volume->base.radius);
}

static void aiAvoid_RemoveInstanceCallback(Entity *be, AIVolumeAvoidInstance* volume)
{
	AI_DEBUG_PRINT(be, AI_LOG_COMBAT, 5, 
		"^1REMOVED ^0avoid instance ^4%d^0: ^4%d ^0lvl diff at ^4%1.1f^0ft.\n",
		volume->base.uid, volume->maxLevelDiff, volume->base.radius);
}

void aiAvoidRemoveInstance(Entity *be, AIVarsBase *aib, U32 uid)
{
	aiVolume_RemoveInstance(be, &aib->avoid.base, uid, (AIRemoveInstanceCallback)aiAvoid_RemoveInstanceCallback);
}

void aiAvoidUpdateInstance(Entity* be, AIVarsBase* aib, U32 oldUid, U32 newUid)
{
	aiVolume_UpdateInstance(&aib->avoid.base, oldUid, newUid);
}

void aiAvoidDestroy(Entity* be, AIVarsBase* aib)
{
	eaDestroy(&aib->avoid.base.volumeEntities);
	
	aiVolume_RemoveAllInstances(&aib->avoid.base);
	
	aiAvoidRemoveNodes(&aib->avoid);
}

void aiSoftAvoidAddInstance(Entity* be, AIVarsBase* aib, int magnitude, F32 radius, U32 uid)
{
	AIVolumeSoftAvoidInstance* volume;
	AIVolumeInfo *volumeInfo = &aib->softAvoid.base;

	volume = (AIVolumeSoftAvoidInstance*)aiVolume_FindByUid(volumeInfo, uid);
	if (volume)
	{
		// Increment the ref count
		volume->base.refCount++;

		volume->magnitude = magnitude;
		volume->base.radius = radius;
		return;
	}

	volume = AIVolumeSoftAvoidInstance_Create(magnitude, radius, uid);

	aiVolume_AddInstance(volumeInfo, &volume->base);

	AI_DEBUG_PRINT(be, AI_LOG_COMBAT, 5, 
		"ADDED soft avoid instance %d: %d magnitude at %1.1fft\n",
		volume->base.uid, volume->magnitude, volume->base.radius);
}

static void aiSoftAvoid_RemoveInstanceCallback(Entity *be, AIVolumeSoftAvoidInstance* volume)
{
	AI_DEBUG_PRINT(be, AI_LOG_COMBAT, 5, 
		"^1REMOVED ^0softavoid instance ^4%d^0: ^4%d ^0magnitude at ^4%1.1f^0ft.\n",
		volume->base.uid, volume->magnitude, volume->base.radius);
}

void aiSoftAvoidRemoveInstance(Entity *be, AIVarsBase *aib, U32 uid)
{
	aiVolume_RemoveInstance(be, &aib->softAvoid.base, uid, (AIRemoveInstanceCallback)aiSoftAvoid_RemoveInstanceCallback);
}

static void aiSoftAvoidAddProxEntryCallback(AIVolumeEntry* newEntry, Entity *e, AIStatusTableEntry *status)
{
	AI_DEBUG_PRINT(e, AI_LOG_COMBAT, 5, "Adding soft avoid entry for entity %d", status->entRef);
}

void aiSoftAvoidAddProximityVolumeEntry(Entity* be, AIVarsBase* aib, AIStatusTableEntry* status)
{
	if(!status->inSoftAvoidList)
	{
		AIVolumeSoftAvoidInfo *info = &aib->softAvoid;
		Entity* statusEnt = entFromEntityRef(entGetPartitionIdx(be), status->entRef);

		devassert(statusEnt);

		status->inSoftAvoidList = 1;

		aiVolume_AddProximityVolumeEntry(be, &info->base, &statusEnt->aibase->softAvoid.base, status, aiSoftAvoidAddProxEntryCallback);
	}
}

static void aiSoftAvoidRemoveProxEntryCallback(AIVolumeEntry* entry, Entity *e, AIStatusTableEntry *status)
{
	AI_DEBUG_PRINT(e, AI_LOG_COMBAT, 5, "Removing soft avoid entry for entity %d", status->entRef);
}

void aiSoftAvoidRemoveProximityVolumeEntry(Entity* be, AIVarsBase* aib, AIStatusTableEntry* status)
{
	if(status->inSoftAvoidList)
	{
		status->inSoftAvoidList = 0;

		aiVolume_RemoveProximityVolumeEntry(be, &aib->softAvoid.base, status, aiSoftAvoidRemoveProxEntryCallback);
	}
}

void aiSoftAvoidDestroy(Entity* be, AIVarsBase* aib)
{
	eaDestroy(&aib->softAvoid.base.volumeEntities);

	aiVolume_RemoveAllInstances(&aib->softAvoid.base);
}

MP_DEFINE(AIVolumeEntry);

void aiAvoidEntryFillSphere(AIVolumeEntry *entry, EntityRef entRef, const Vec3 spherePos, F32 radius, 
							AIVolumeInstance *instance, AIStatusTableEntry* status)
{
	devassertmsg(!entRef || !spherePos, "Can't be centered on an entity and have a position too");

	entry->entRef = entRef;

	if(spherePos) 
		copyVec3(spherePos, entry->spherePos);

	entry->sphereRadius = radius;
	entry->status = status;
	entry->instance = instance;
	
	entry->isSphere = 1;

	entry->destroyOnRemove = !!entRef;
}

void aiAvoidEntryFillBox(AIVolumeEntry *entry, const Mat4 boxMat, const Vec3 locMin, const Vec3 locMax)
{
	copyMat4(boxMat, entry->boxMat);
	invertMat4(boxMat, entry->boxInvMat);
	copyVec3(locMin, entry->boxLocMin);
	copyVec3(locMax, entry->boxLocMax);

	entry->isSphere = 0;

	entry->destroyOnRemove = 0;
}

AIVolumeEntry* aiAvoidEntryCreate(void *pIdPtr)
{
	AIVolumeEntry* entry;

	MP_CREATE(AIVolumeEntry, 4);

	entry = MP_ALLOC(AIVolumeEntry);
	entry->pIdPtr = pIdPtr;

	return entry;
}

AIVolumeEntry* aiAvoidEntryCreateSphere(int partitionIdx, EntityRef entRef, const Vec3 spherePos, F32 radius, 
										void *pIdPtr, AIVolumeInstance *instance, AIStatusTableEntry* status)
{
	AIVolumeEntry* entry = aiAvoidEntryCreate(pIdPtr);

	entry->partitionIdx = partitionIdx;
	aiAvoidEntryFillSphere(entry, entRef, spherePos, radius, instance, status);

	return entry;
}

AIVolumeEntry* aiAvoidEntryCreateBox(int partitionIdx, const Mat4 boxMat, const Vec3 locMin, const Vec3 locMax, void *pIdPtr)
{
	AIVolumeEntry* entry = aiAvoidEntryCreate(pIdPtr);

	entry->partitionIdx = partitionIdx;
	aiAvoidEntryFillBox(entry, boxMat, locMin, locMax);

	return entry;
}

void aiAvoidEntryDestroy(AIVolumeEntry* entry)
{
	MP_FREE(AIVolumeEntry, entry);
}

void aiAvoidEntryRemove(AIVolumeEntry* entry)
{
	if(entry->destroyOnRemove)
		aiAvoidEntryDestroy(entry);
}

static void aiAvoid_AddProxEntryCallback(AIVolumeEntry* newEntry, Entity *e, AIStatusTableEntry *status)
{
	AI_DEBUG_PRINT(e, AI_LOG_COMBAT, 5, "Adding avoid entry for entity %d", status->entRef);
	aiMovementAvoidEntryAdd(e, e->aibase, newEntry);
}

void aiAvoidAddProximityVolumeEntry(Entity* be, AIVarsBase* aib, AIStatusTableEntry* status)
{
	if(!status->inAvoidList)
	{
		AIVolumeAvoidInfo *info = &aib->avoid;
		Entity* statusEnt = entFromEntityRef(entGetPartitionIdx(be), status->entRef);

		devassert(statusEnt);

		status->inAvoidList = 1;

		aiVolume_AddProximityVolumeEntry(be, &info->base, &statusEnt->aibase->avoid.base, status, aiAvoid_AddProxEntryCallback);
	}
}


static void aiAvoidRemoveProxEntryCallback(AIVolumeEntry* entry, Entity *e, AIStatusTableEntry *status)
{
	AI_DEBUG_PRINT(e, AI_LOG_COMBAT, 5, "Removing avoid entry for entity %d", status->entRef);
	// The background movement code needs to know about avoid entries, so this just
	// tells the movement system to queue this for deletion and delete it when the
	// bg doesn't have a pointer to it anymore
	aiMovementAvoidEntryRemove(e, e->aibase, entry);
}

void aiAvoidRemoveProximityVolumeEntry(Entity* be, AIVarsBase* aib, AIStatusTableEntry* status)
{
	if(status->inAvoidList)
	{
		status->inAvoidList = 0;

		aiVolume_RemoveProximityVolumeEntry(be, &aib->avoid.base, status, aiAvoidRemoveProxEntryCallback);


#ifdef AI_PARANOID_AVOID
		{
			Entity *statusEnt = entFromEntityRef(status->entRef);
			if(statusEnt)
			{
				S32 i;
				AIVolumeAvoidInstance *instance;
				for(instance = statusEnt->aibase->avoid.list; instance; instance = instance->next)
				{
					for(i=0; i<eaSize(&aib->avoid.volumeEntities); i++)
					{
						AIVolumeEntry *entry = aib->avoid.volumeEntities[i];
						//devassert(entry->instance!=instance);
					}
				}
			}
		}

#endif


	}
}

// -------------------------------------------------------------------------------------------------------
// 
static void aiAvoidUpdateEnvHelper(Array* beacons, AIVolumeEntry* entry)
{
	int count = beacons->size;
	int i;
	AIPartitionState *partition = aiPartitionStateGet(entry->partitionIdx, false);

	for(i = 0; i < count; i++)
	{
		Beacon* b = beacons->storage[i];

		if(entry->isSphere)
		{
			F32 distSQR = distance3Squared(b->pos, entry->spherePos);
			if(distSQR <= SQR(entry->sphereRadius+100))
			{
				BeaconAvoidNode* node = NULL;
				BeaconAvoidCheckBits bits = 0;
				
				if(distSQR < SQR(entry->sphereRadius))
					bits = BEACON_AVOID_POINT;
				else if(distSQR > SQR(entry->sphereRadius + 15))
					bits = BEACON_AVOID_LINE;
				else		// +/- 15 ft checks both due to capsule sizes
					bits = BEACON_AVOID_POINT | BEACON_AVOID_LINE;

				node = beaconAddAvoidNode(b, NULL, entry, entry->partitionIdx, bits, &partition->envAvoidNodes);

				eaPush(&partition->envAvoidNodes, node);
			}
		}
		else
		{
			Vec3 boxSpacePos;
			Vec3 offset = {100,100,100}, offMin, offMax;
			Vec3 offsetIn = {15, 15, 15};

			subVec3(entry->boxLocMin, offset, offMin);
			addVec3(entry->boxLocMax, offset, offMax);

			mulVecMat4(b->pos, entry->boxInvMat, boxSpacePos);
			if(pointBoxCollision(boxSpacePos, offMin, offMax))
			{
				BeaconAvoidCheckBits bits = 0;
				BeaconAvoidNode* node = NULL;
				Vec3 min15More, max15More;
				
				subVec3(entry->boxLocMin, offsetIn, min15More);
				addVec3(entry->boxLocMax, offsetIn, max15More);
				
				if(pointBoxCollision(boxSpacePos, entry->boxLocMin, entry->boxLocMax))
					bits = BEACON_AVOID_POINT;
				else if(pointBoxCollision(boxSpacePos, min15More, max15More))
					bits = BEACON_AVOID_POINT | BEACON_AVOID_LINE;
				else
					bits = BEACON_AVOID_LINE;

				node = beaconAddAvoidNode(b, NULL, entry, entry->partitionIdx, bits, &partition->envAvoidNodes);

				eaPush(&partition->envAvoidNodes, node);
			}
		}
	}	
}

static void aiAvoidUpdateEnvironmentVolumes(int partitionIdx)
{
	int i;
	AIPartitionState *partition = aiPartitionStateGet(partitionIdx, false);

	if (!partition)
		return;

	PERFINFO_AUTO_START_FUNC();

	eaClearEx(&partition->envAvoidNodes, beaconDestroyAvoidNode_CalledFromAI);

	for(i = eaSize(&partition->envAvoidEntries)-1; i >= 0; i--)
	{
		AIVolumeEntry* entry = partition->envAvoidEntries[i];
		Vec3 boundsMin;
		Vec3 boundsMax;

		if(entry->isSphere)
		{
			subVec3same(entry->spherePos, entry->sphereRadius+35, boundsMin);
			addVec3same(entry->spherePos, entry->sphereRadius+35, boundsMax);
		}
		else
		{
			mulBoundsAA(entry->boxLocMin, entry->boxLocMax, entry->boxMat, boundsMin, boundsMax);
			subVec3same(boundsMin, 35, boundsMin);
			addVec3same(boundsMax, 35, boundsMax);
		}
		
		beaconForEachBlockBoundsIntPartition(partitionIdx, boundsMin, boundsMax, aiAvoidUpdateEnvHelper, entry);
	}

	PERFINFO_AUTO_STOP();
}

static AIVolumeEntry *aiAvoidFindOrCreateEntry(void *pIdPtr, int partitionIdx, int create)
{
	AIPartitionState *partition = aiPartitionStateGet(partitionIdx, false);

	assert(partition);
	
	FOR_EACH_IN_EARRAY(partition->envAvoidEntries, AIVolumeEntry, entry)
	{
		if(entry->pIdPtr==pIdPtr)
			return entry;
	}
	FOR_EACH_END

	if(create)
	{
		AIVolumeEntry *entry = aiAvoidEntryCreate(pIdPtr);
		//TODO: figure out why the movement destruction is slow enough to make this not work right
		//eaDestroyEx(&envAvoidEntriesOld, aiAvoidEntryDestroy);
		entry->partitionIdx = partitionIdx;
		eaPush(&partition->envAvoidEntries, entry);
		return entry;
	}

	return NULL;
}

static void aiAvoidVolumeAddSphere(int partitionIdx, const Vec3 spherePos, F32 sphereRadius, void *pIdPtr)
{
	AIVolumeEntry* entry = aiAvoidFindOrCreateEntry(pIdPtr, partitionIdx, true);

	aiAvoidEntryFillSphere(entry, 0, spherePos, sphereRadius, NULL, NULL);
}

static void aiAvoidVolumeAddBox(int partitionIdx, const Mat4 boxMat, const Vec3 boxLocalMin, const Vec3 boxLocalMax, void *pIdPtr)
{
	AIVolumeEntry* entry = NULL;
	
	PERFINFO_AUTO_START_FUNC();

	entry = aiAvoidFindOrCreateEntry(pIdPtr, partitionIdx, true);

	aiAvoidEntryFillBox(entry, boxMat, boxLocalMin, boxLocalMax);

	PERFINFO_AUTO_STOP();
}

void aiAvoidVolumeRemove(int partitionIdx, void *pIdPtr)
{
	int i;
	AIPartitionState *partition = aiPartitionStateGet(partitionIdx, false);
	
	if (partition)
	{
		for(i=eaSize(&partition->envAvoidEntries)-1; i>=0; --i)
		{
			AIVolumeEntry *entry = partition->envAvoidEntries[i];
			if (entry->pIdPtr == pIdPtr)
			{
				eaPush(&partition->envAvoidEntriesOld, entry);
				eaRemove(&partition->envAvoidEntries, i);
				aiAvoidUpdateEnvironmentVolumes(partitionIdx);
				break;
			}
		}
	}
	
	if(partitionIdx==0)
	{
		FOR_EACH_IN_EARRAY(aiMapState.partitions, AIPartitionState, otherPartition)
		{
			if(otherPartition && otherPartition->idx!=0)
				aiAvoidVolumeRemove(otherPartition->idx, pIdPtr);
		}
		FOR_EACH_END
	}
}

void aiAvoidVolumeRemoveAll(int partitionIdx)
{
	AIPartitionState *partition = aiPartitionStateGet(partitionIdx, false);

	if (partition)
	{
		eaPushEArray(&partition->envAvoidEntriesOld, &partition->envAvoidEntries);
		eaClear(&partition->envAvoidEntries);
		aiAvoidUpdateEnvironmentVolumes(partitionIdx);
	}
	
	if(partitionIdx==0)
	{
		FOR_EACH_IN_EARRAY(aiMapState.partitions, AIPartitionState, otherPartition)
		{
			if(otherPartition && otherPartition->idx!=0)
				aiAvoidVolumeRemoveAll(otherPartition->idx);
		}
		FOR_EACH_END
	}
}

void aiAvoidAddEnvironmentEntries(Entity* e, AIVarsBase* aib)
{
	int i;
	AIPartitionState *partition = aiPartitionStateGet(entGetPartitionIdx(e), false);

	if (partition)
	{
		for(i = eaSize(&partition->envAvoidEntries)-1; i >= 0; i--)
		{
			eaPush(&aib->avoid.base.volumeEntities, partition->envAvoidEntries[i]);
			aiMovementAvoidEntryAdd(e, aib, partition->envAvoidEntries[i]);
		}
	}
	
}

F32 aiVolumeEntry_AsSphereGetDistanceToSelf(Entity* e, const MovementRequesterMsg* msg, const AIVolumeEntry* entry)
{
	devassert((e && !msg) || (!e && msg));
	devassert(entry->isSphere);

	if(entry->entRef)
	{
		if(e)
		{
			Entity* avoidEnt = entFromEntityRef(entGetPartitionIdx(e), entry->entRef);
			if(avoidEnt)
				return entGetDistance(e, NULL, avoidEnt, NULL, NULL);
			
			return FLT_MAX;
		}
		else
		{
			F32 dist = 0.f;

			if(!mrmGetEntityDistanceBG(msg, entry->entRef, &dist, false))
				return FLT_MAX;

			return dist;
		}
	}
	else
	{
		if(e)
		{
			return entGetDistance(e, NULL, NULL, entry->spherePos, NULL);
		}
		else
		{
			F32 dist = 0.f;
			if(!mrmGetCapsulePointDistanceBG(msg, entry->spherePos, &dist, false))
				return FLT_MAX;

			return dist;
		}
	}

}

int aiAvoidEntryCheckSelf(Entity* e, const MovementRequesterMsg* msg, const AIVolumeEntry* entry)
{
	devassert((e && !msg) || (!e && msg));

	if(entry->isSphere)
	{
		F32 dist = FLT_MAX;

		dist = aiVolumeEntry_AsSphereGetDistanceToSelf(e, msg, entry);
		
		return dist < entry->sphereRadius;
	}
	else
	{
		Vec3 pos;
		Vec3 boxSpacePos;

		if(e)
			entGetPos(e, pos);
		else
		{
			if(!mrmGetPositionBG(msg, pos))
				return false;
		}

		mulVecMat4(pos, entry->boxInvMat, boxSpacePos);

		return pointBoxCollision(boxSpacePos, entry->boxLocMin, entry->boxLocMax);
	}
}

static F32 aiAvoidGetCapsuleRadius(bool isFG, Entity *e,const MovementRequesterMsg* msg)
{
	F32 radius = 3;

	if(isFG)
	{
		radius = e ? entGetPrimaryCapsuleRadius(e) : 0;
	}
	else
	{
		mrmGetPrimaryBodyRadiusBG(msg, &radius);
	}

	return radius;
}

int aiAvoidEntryCheckPoint(Entity *e, const Vec3 sourcePos, const AIVolumeEntry* entry, int isFG,
						   const MovementRequesterMsg* msg)
{
	devassert(isFG && !msg || !isFG && msg);

	if(entry->isSphere)
	{
		// I have no idea whether this case is consistent with the other 3 [RMARR - 4/6/11]
		Vec3 entPos = {0,0,0};
		Vec3 offset = {0,0,0};
		if(e && sourcePos)
		{
			entGetPos(e, entPos);
			subVec3(sourcePos, entPos, offset);
		}
		if(entry->entRef)
		{
			F32 dist = FLT_MAX;

			if(isFG)
			{
				Entity* avoidEnt = entFromEntityRefAnyPartition(entry->entRef);

				if(avoidEnt)
					dist = entGetDistanceOffset(e, sourcePos, offset, avoidEnt, NULL, NULL);
			}
			else
			{
				if(!mrmGetPointEntityDistanceBG(msg, sourcePos, entry->entRef, &dist))
					return false;
			}

			if(dist < entry->sphereRadius+AI_AVOID_FUDGE_RADIUS)
				return true;
		}
		else
		{
			F32 dist;

			if(isFG)
			{
				dist = entGetDistanceOffset(e, sourcePos, offset, NULL, entry->spherePos, NULL);
			}
			else
			{
				if(!mrmGetCapsulePointDistanceBG(msg, entry->spherePos, &dist, 0))
					return false;
			}
			if(dist < entry->sphereRadius+AI_AVOID_FUDGE_RADIUS)
				return true;
		}
	}
	else
	{
		Vec3 boxSpacePos;
		Vec3 pos;
		F32 radius = aiAvoidGetCapsuleRadius(isFG,e,msg);

		if (sourcePos)
		{
			copyVec3(sourcePos, pos);
		}
		else if(isFG)
		{
			devassert(e);
			entGetPos(e, pos);
		}
		else
		{
			mrmGetPositionBG(msg, pos);
		}

		pos[1] += 0.25;

		mulVecMat4(pos, entry->boxInvMat, boxSpacePos);

		// we're going to check a sphere around the point, which I think is what aiAvoidEntryCheckLine effectively does.  [RMARR - 4/6/11]
		if (boxSphereCollision(entry->boxLocMin, entry->boxLocMax,boxSpacePos,radius+AI_AVOID_FUDGE_RADIUS))
			return true;
	}

	return false;
}

int aiAvoidEntryCheckLine(Entity *e, const Vec3 rayStart, const Vec3 rayEnd, const AIVolumeEntry* entry,
						  int isFG, const MovementRequesterMsg* msg)
{
	devassert(!isFG && msg && !e || isFG && !msg && e);

	if(entry->isSphere)
	{
		Vec3 rayDir;
		F32 rayLen;
		F32 radius = 3;

		subVec3(rayEnd, rayStart, rayDir);
		rayLen = lengthVec3(rayDir);
		normalVec3(rayDir);

		radius = aiAvoidGetCapsuleRadius(isFG,e,msg);

		if(entry->entRef)
		{
			if(isFG)
			{
				Entity* avoidEnt = entFromEntityRef(entGetPartitionIdx(e), entry->entRef);
				if(avoidEnt && entLineDistance(rayStart, radius, rayDir, rayLen, avoidEnt, NULL) < entry->sphereRadius)
					return true;
			}
			else
			{
				F32 dist;

				if(!mrmGetLineEntityDistanceBG(msg, rayStart, rayDir, rayLen, radius, entry->entRef, &dist))
					return false;
				if(dist < entry->sphereRadius+AI_AVOID_FUDGE_RADIUS)
					return true;
			}
		}
		else
		{
			F32 distSQR = PointLineDistSquared(entry->spherePos, rayStart, rayDir, rayLen, NULL);;

			if(distSQR < SQR(entry->sphereRadius+radius+AI_AVOID_FUDGE_RADIUS))
				return true;
		}
	}
	else
	{
		Vec3 boxLineStart;
		Vec3 boxLineEnd;
		Vec3 boxMin, boxMax;
		Vec3 coll;
		F32 radius = aiAvoidGetCapsuleRadius(isFG,e,msg);

		mulVecMat4(rayStart, entry->boxInvMat, boxLineStart);
		mulVecMat4(rayEnd, entry->boxInvMat, boxLineEnd);

		subVec3same(entry->boxLocMin, radius+AI_AVOID_FUDGE_RADIUS, boxMin);
		addVec3same(entry->boxLocMax, radius+AI_AVOID_FUDGE_RADIUS, boxMax);

		if(lineBoxCollision(boxLineStart, boxLineEnd, boxMin, boxMax, coll))
		{
			Vec3 vStartToColl, vCollToEnd;
			Vec3 vLineDir;
			subVec3(boxLineEnd,boxLineStart,vLineDir);

			subVec3(coll, boxLineStart, vStartToColl);
			subVec3(boxLineEnd, coll, vCollToEnd);

			return (dotVec3(vStartToColl, vLineDir) >= 0.f && dotVec3(vCollToEnd, vLineDir) >= 0.f);
		}
	}

	return false;
}

// ---------------------------------------------------------------------------------
// Attraction
// ---------------------------------------------------------------------------------

void aiAttractAddInstance(Entity* be, AIVarsBase *aib, F32 radius, U32 uid)
{
	AIVolumeAttractInstance* volume;

	volume = (AIVolumeAttractInstance*)aiVolume_FindByUid(&aib->attract.base, uid);
	if (volume)
	{
		// Increment the ref count
		volume->base.refCount++;

		volume->base.radius = radius;
		return;
	}

	volume = AIVolumeAttractionInstance_Create(radius, uid);

	aiVolume_AddInstance(&aib->attract.base, &volume->base);

	AI_DEBUG_PRINT(be, AI_LOG_COMBAT, 5, "ADDED avoid instance %d: %1.1fft\n", volume->base.uid, volume->base.radius);
}

void aiAttractRemoveInstance(Entity* be, AIVarsBase *aib, U32 uid)
{
	aiVolume_RemoveInstance(be, &aib->attract.base, uid, NULL);
}

void aiAttractUpdateInstance(Entity* be, AIVarsBase* aib, U32 oldUid, U32 newUid)
{
	aiVolume_UpdateInstance(&aib->attract.base, oldUid, newUid);
}

void aiAttractDestroy(Entity* be, AIVarsBase* aib)
{
	eaDestroy(&aib->attract.base.volumeEntities);

	aiVolume_RemoveAllInstances(&aib->attract.base);
}

void aiAttractAddProximityVolumeEntry(Entity* be, AIVarsBase* aib, AIStatusTableEntry* status)
{
	if(!status->inAttractList)
	{
		Entity* statusEnt = entFromEntityRef(entGetPartitionIdx(be), status->entRef);

		devassert(statusEnt);

		status->inAttractList = 1;

		aiVolume_AddProximityVolumeEntry(be, &aib->attract.base, &statusEnt->aibase->attract.base, status, NULL);
	}
}

void aiAttractRemoveProximityVolumeEntry(Entity* be, AIVarsBase* aib, AIStatusTableEntry* status)
{
	if(status->inAttractList)
	{
		status->inAttractList = 0;
		aiVolume_RemoveProximityVolumeEntry(be, &aib->attract.base, status, NULL);
	}
}


// Returns the first volume it find itself within, otherwise returns the closest volume
// that's within X feet of the edge
AIVolumeEntry* aiAttractGetClosestWithin(Entity* be, AIVarsBase* aib, F32 withinXFeet, F32 *distTo)
{
	F32 fClosest = FLT_MAX;
	AIVolumeEntry *pClosestEntry = NULL;

	FOR_EACH_IN_EARRAY(aib->attract.base.volumeEntities, AIVolumeEntry, pVolume)
	{
		F32 distance;
		// only supporting spheres for now
		devassert(pVolume->isSphere);
	
		// get the distance to the volume
		distance = aiVolumeEntry_AsSphereGetDistanceToSelf(be, NULL, pVolume);
		
		if (distance < pVolume->sphereRadius)
		{	// we're in this volume, so just return it
			*distTo = 0.f;
			return pVolume;
		}

		distance = distance - pVolume->sphereRadius;
		if (distance <= withinXFeet && distance < fClosest)
		{
			fClosest = distance;
			pClosestEntry = pVolume;
		}
	}
	FOR_EACH_END

	*distTo = fClosest;
	return pClosestEntry;
}

// ---------------------------------------------------------------------------------
// AI Volume Functions
// ---------------------------------------------------------------------------------

AIVolumeInstance* aiVolume_FindByUid(AIVolumeInfo *volumeInfo, U32 uid)
{
	AIVolumeInstance* volume;


	for(volume = volumeInfo->list; volume; volume = volume->next)
	{
		if(volume->uid == uid)
		{
			return volume;
		}
	}

	return NULL;
}

AIVolumeInstance* aiVolume_Find(AIVolumeInfo *volumeInfo, AIVolumeCompare cmp, void *p)
{
	AIVolumeInstance* volume;

	if (!cmp)
		return NULL;

	for(volume = volumeInfo->list; volume; volume = volume->next)
	{
		if(cmp(volume, p))
		{
			return volume;
		}
	}

	return NULL;
}

void aiVolume_AddInstance(AIVolumeInfo *volumeInfo, AIVolumeInstance *volume)
{
	volume->next = volumeInfo->list;
	volumeInfo->list = volume;

	if(volume->radius > volumeInfo->maxRadius)
		volumeInfo->maxRadius = volume->radius;
}

void aiVolume_RemoveInstance(Entity *be, AIVolumeInfo *volumeInfo, U32 uid, AIRemoveInstanceCallback cb)
{
	AIVolumeInstance* volume;
	AIVolumeInstance* prev;
	F32 maxRadius = 0.f;

	volume = volumeInfo->list;
	prev = NULL; 
	while(volume)
	{
		if(volume->uid == uid)
		{
			AIVolumeInstance* next;

			// Decrement the ref count
			volume->refCount--;

			if (volume->refCount <= 0)
			{
				// We need to actually delete the volume since refCount dropped to 0
				if(prev)
					next = prev->next = volume->next;
				else
					next = volumeInfo->list = volume->next;

				if (cb) 
					cb(be, volume);

				aiVolumeInstance_Destroy(volume);

				volume = next;
			}
			else
			{
				if(volume->radius > maxRadius)
					maxRadius = volume->radius;

				prev = volume;
				volume = volume->next;
			}
		}
		else
		{
			if(volume->radius > maxRadius)
				maxRadius = volume->radius;

			prev = volume;
			volume = volume->next;
		}
	}

	volumeInfo->maxRadius = maxRadius;
}

void aiVolume_UpdateInstance(AIVolumeInfo *volumeInfo, U32 oldUid, U32 newUid)
{
	AIVolumeInstance* volume = aiVolume_FindByUid(volumeInfo, oldUid);
	if (volume)
	{
		volume->uid = newUid;
	}
}


void aiVolume_RemoveAllInstances(AIVolumeInfo *volumeInfo)
{
	while(volumeInfo->list)
	{
		AIVolumeInstance* next = volumeInfo->list->next;
		aiVolumeInstance_Destroy(volumeInfo->list);
		volumeInfo->list = next;
	}

	volumeInfo->maxRadius = 0.f;
}

void aiVolume_AddProximityVolumeEntry(Entity *e, AIVolumeInfo *volumeInfo, const AIVolumeInfo *srcVolumeInfo, 
									  AIStatusTableEntry *srcStatus, AIProxEntryCallback cb)
{
	AIVolumeInstance* instance;
	AIVolumeEntry* entry;

	instance = srcVolumeInfo->list;
	while(instance)
	{
		entry = aiAvoidEntryCreateSphere(entGetPartitionIdx(e), srcStatus->entRef, NULL, instance->radius, NULL, instance, srcStatus);

		eaPush(&volumeInfo->volumeEntities, entry);

		if (cb) 
			cb(entry, e, srcStatus);

		instance = instance->next;
	}
}

void aiVolume_RemoveProximityVolumeEntry(Entity* be, AIVolumeInfo *volumeInfo, AIStatusTableEntry* status,
										 AIProxEntryCallback cb)
{
	int i;
	Entity *statusEnt = entFromEntityRef(entGetPartitionIdx(be), status->entRef);

	for(i = eaSize(&volumeInfo->volumeEntities) - 1; i >= 0; i--)
	{
		AIVolumeEntry* entry = volumeInfo->volumeEntities[i];

		if(entry->status == status)
		{
			devassert(entry->status->entRef == status->entRef);

			if (cb) 
				cb(entry, be, status);

			eaRemoveFast(&volumeInfo->volumeEntities, i);
		}
	}
}

void aiVolumeEntry_GetPosition(const AIVolumeEntry *volume, Vec3 vPos)
{
	if(volume->isSphere)
	{
		if (volume->entRef)
		{
			Entity* ent = entFromEntityRefAnyPartition(volume->entRef);
			if(ent)
			{
				entGetPos(ent, vPos);
			}
			else
			{
				zeroVec3(vPos);
			}
		}
		else
		{
			copyVec3(volume->spherePos, vPos);
		}
	}
	else
	{
		Vec3 vLocalMid;
		interpVec3(0.5f, volume->boxLocMin, volume->boxLocMin, vLocalMid);
		mulVecMat4(vLocalMid, volume->boxMat, vPos);
	}
}

F32 aiVolumeEntry_GetRadius(const AIVolumeEntry *volume)
{
	if(volume->isSphere)
	{
		return volume->sphereRadius;
	}
	else 
	{
		return distance3(volume->boxLocMin, volume->boxLocMax);
	}	
}

typedef struct AIAvoidProcessVolumeData {
	int partition;
} AIAvoidProcessVolumeData;

void aiAvoidProcessVolumes(WorldVolumeEntry *entry, AIAvoidProcessVolumeData *data)
{
	WorldAIVolumeProperties *aiProps = entry ? entry->server_volume.ai_volume_properties : NULL;
	if(aiProps && aiProps->avoid)
	{
		FOR_EACH_IN_EARRAY(entry->elements, WorldVolumeElement, element)
		{
			if (element->volume_shape == WL_VOLUME_BOX) 
			{
				aiAvoidVolumeAddBox(data->partition, element->world_mat, element->local_min, element->local_max, element);
			} 
			else 
			{
				aiAvoidVolumeAddSphere(data->partition, element->world_mat[3], element->radius, element);
			}
		}
		FOR_EACH_END
	}
}

void aiAvoidPartitionLoad(int partitionIdx)
{
	AIPartitionState *partition = NULL;
	AIAvoidProcessVolumeData data = {0};

	PERFINFO_AUTO_START_FUNC();

	partition = aiPartitionStateGet(partitionIdx, true);

	data.partition = partitionIdx;

	// Scan for avoid volumes
	volume_ForEachEntry(NULL, aiAvoidProcessVolumes, &data);
	aiAvoidUpdateEnvironmentVolumes(partitionIdx);

	PERFINFO_AUTO_STOP();
}
