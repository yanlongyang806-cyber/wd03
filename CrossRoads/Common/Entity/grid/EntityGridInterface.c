#include "EntityGrid.h"
#include "EntityGridPrivate.h"


#include "EntityMovementManager.h"
#include "ScratchStack.h"

typedef struct Entity Entity;
#define MIN_PLAYER_CRITTER_RADIUS 200

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

void entInitGrid(Entity* e)
{
	F32 dxyz;

	devassertmsg(e->myEntityType, "Trying to add a non-valid entity to entgrid, please get Raoul or Ben");
	
	dxyz = MAX(MIN_PLAYER_CRITTER_RADIUS, e->fEntitySendDistance);
	
	if(!e->egNode)
	{
		e->egNode = createEntityGridNode(e, dxyz, 1);
	}
	else
	{
		egUpdateNodeRadius(e->egNode, dxyz, 1);
	}

	if(entCheckFlag(e, ENTITYFLAG_IS_PLAYER))
	{
		if(!e->egNodePlayer)
		{
			e->egNodePlayer = createEntityGridNode(e, dxyz, 0);
		}
		else
		{
			egUpdateNodeRadius(e->egNodePlayer, dxyz, 0);
		}
	}
}

void entGridUpdate(Entity* e, int create)
{
	int iPartitionIdx;
	int forceUpdate = 0, radiusChanged = 0;
	F32 sendRadius;
	Vec3 ePos;
	EntityMovementThreadData* td = NULL;
	S32 copyUpdated = 0;

	PERFINFO_AUTO_START_FUNC();

	mmGetUserThreadData(e->mm.movement, &td);

	if(!e->egNode && create)
		entInitGrid(e);

	if(!e->egNode) {
		PERFINFO_AUTO_STOP();
		return;
	}

	iPartitionIdx = entGetPartitionIdx(e);
	if ((iPartitionIdx == PARTITION_IN_TRANSACTION) || (iPartitionIdx == PARTITION_ORPHAN_PET)) {
		PERFINFO_AUTO_STOP();
		return; // Don't add to grid if entity is in a transaction 
	}

	sendRadius = MAX(MIN_PLAYER_CRITTER_RADIUS, e->fEntitySendDistance);
	sendRadius += e->collRadiusCached;
	sendRadius = (int)sendRadius;

	forceUpdate = e->myEntityFlags != e->egNode->entFlags;

	if(*(int*)&e->egNode->radius != *(int*)&sendRadius)	
		radiusChanged = 1;	

	e->egNode->entFlags = entGetFlagBits(e);

	if (entCheckFlag(e, ENTITYFLAG_IS_PLAYER))
	{
		forceUpdate |= e->myEntityFlags != e->egNodePlayer->entFlags;
		if(*(int*)&e->egNodePlayer->radius != *(int*)&sendRadius)	
			radiusChanged = 1;	

		e->egNodePlayer->entFlags = entGetFlagBits(e);
	}

	PERFINFO_AUTO_START("egUpdate", 1);
	entGetPos(e, ePos);
	if (radiusChanged)
	{
		egUpdateNodeRadius(e->egNode, sendRadius, 1);
	}

	egUpdate(iPartitionIdx, 0, e->egNode, &td->entGridPosCopy, &copyUpdated, ePos, forceUpdate || radiusChanged);
	PERFINFO_AUTO_STOP();

	if(entCheckFlag(e, ENTITYFLAG_IS_PLAYER))
	{
		PERFINFO_AUTO_START("egUpdatePlayer", 1);
		if (radiusChanged)
			egUpdateNodeRadius(e->egNodePlayer, sendRadius, 0);
		egUpdate(iPartitionIdx, 2, e->egNodePlayer, &td->entGridPosCopyPlayer, &copyUpdated, ePos, forceUpdate || radiusChanged);
		PERFINFO_AUTO_STOP();
	}
	
	if(copyUpdated){
		mmSendMsgUserThreadDataUpdatedToBG(e->mm.movement);
	}

	PERFINFO_AUTO_STOP();
}

#if ENTITY_GRID_DEBUG
	void verifyGridNode(void* owner, EntityGridNode* node){
		Entity* ent = validEntFromId((int)owner);
		
		assert(ent);
		
		assert(ent->megaGridNode == node);
	}
#endif

void entGridFree(Entity *e)
{
	if(e->egNode){
		destroyEntityGridNode(e->egNode);
		e->egNode = NULL;

		#if ENTITY_GRID_DEBUG
			egVerifyGrid(0, verifyGridNode);
		#endif
	}

	if(e->egNodePlayer){
		destroyEntityGridNode(e->egNodePlayer);
		e->egNodePlayer = NULL;
	}
}

int entGridProximityLookup(int iPartitionIdx, const Vec3 pos, Entity** result, int playersOnly)
{
	return egGetNodesInRange(iPartitionIdx, playersOnly ? 2 : 0, pos, result);
}

int entGridProximityLookupEArray(int iPartitionIdx, const Vec3 pos, Entity*** result, int playersOnly)
{
	Entity** buf = ScratchAlloc(MAX_ENTITIES_PRIVATE * sizeof(Entity *));
	int count;

	count = egGetNodesInRange(iPartitionIdx, playersOnly ? 2 : 0, pos, buf);

	eaSetSize(result, count);
	memcpy(*result, buf, count * sizeof(Entity *));

	ScratchFree(buf);
	return count;
}

int entGridProximityLookupEx(int iPartitionIdx, const Vec3 pos, Entity** result, F32 distance, EntityFlags entFlagsToMatch, EntityFlags entFlagsToExclude, Entity* sourceEnt)
{
	// search for points within distance
	return egGetNodesInRangeWithDistAndFlags(iPartitionIdx,
		FlagsMatchAny(entFlagsToMatch,ENTITYFLAG_IS_PLAYER) ? 2 : 0,
		pos, distance, entFlagsToMatch, entFlagsToExclude, result, sourceEnt);
}

int entGridProximityLookupExEArray(int iPartitionIdx, const Vec3 pos, Entity*** result, F32 distance, EntityFlags entFlagsToMatch, EntityFlags entFlagsToExclude, Entity* sourceEnt)
{
	static Entity* buf[MAX_ENTITIES_PRIVATE];
	//Entity** buf = ScratchAlloc(MAX_ENTITIES_PRIVATE * sizeof(Entity *));
	int count;

	count = egGetNodesInRangeWithDistAndFlags(iPartitionIdx,
		FlagsMatchAny(entFlagsToMatch,ENTITYFLAG_IS_PLAYER) ? 2 : 0,
		pos, distance, entFlagsToMatch, entFlagsToExclude, buf, sourceEnt);

	if(result)
	{
		eaSetSize(result, count);
		memcpy(*result, buf, count * sizeof(Entity *));
	}//ScratchFree(buf);

	return count;
}

int entGridProximityLookupDynArraySaveDist(int iPartitionIdx, const Vec3 pos, EntAndDist** result, int* count, int* maxCount, F32 distance, EntityFlags entFlagsToMatch, EntityFlags entFlagsToExclude, Entity* ignoreEnt)
{
	return egGetNodesInRangeSaveDist(iPartitionIdx,
		FlagsMatchAny(entFlagsToMatch,ENTITYFLAG_IS_PLAYER) ? 2 : 0,
		pos, distance, entFlagsToMatch, entFlagsToExclude, result, count, maxCount, ignoreEnt);
}

void entGridCopyGridPos(const EntityGridNode* node,
						EntGridPosCopy* gridPosCopy)
{
	copyVec3(node->grid_pos, gridPosCopy->posGrid);
	gridPosCopy->bitsRadius = node->radius_hi_bits - node->use_small_cells;
}

