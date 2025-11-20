
#include "WorldCollPrivate.h"
#include "LineDist.h"
#include "mutex.h"
#include "wcoll/collcache.h"
#include "rand.h"
#include "wlBeacon.h"
#include "wlPerf.h"

#include "SimpleCpuUsage.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("AssociationList", BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("ThreadStack:wcThreadMain", BUDGET_Physics););

WorldCollGlobalState wcgState;

MP_DEFINE(WorldCollGridCell);
MP_DEFINE(WorldCollMaterial);

const F32 defaultSFriction = 0.5f;
const F32 defaultDFriction = 0.5f;
const F32 defaultRestitution = 0.5f;
const F32 defaultGravity = -32.0f;

#if _PS3
	static __thread S32 wcgIsBackgroundThread;
#else
	static S32 wcgIsBackgroundThreadTlsIndex;
#endif

static S32 __forceinline wcThreadIsFG(void){
	if(!wcgState.fg.threadID){
		return 0;
	}

	return wcgState.fg.threadID == GetCurrentThreadId();
}

void wcSetThreadIsBG(void){
	if(GetCurrentThreadId() == wcgState.fg.threadID){
		return;
	}
	
	#if _PS3
		wcgIsBackgroundThread = 1;
	#else
		if(!wcgIsBackgroundThreadTlsIndex){
			wcgIsBackgroundThreadTlsIndex = TlsAlloc();
		}
		TlsSetValue(wcgIsBackgroundThreadTlsIndex, (void*)(intptr_t)1);
	#endif
}

S32 wcThreadIsBG(void){
	if(GetCurrentThreadId() == wcgState.fg.threadID){
		return 0;
	}
	
	#if _PS3
		return wcgIsBackgroundThread;
	#else
		return	wcgIsBackgroundThreadTlsIndex &&
				!!TlsGetValue(wcgIsBackgroundThreadTlsIndex);
	#endif
}

S32 wcNotificationOverride = 1;
AUTO_CMD_INT(wcNotificationOverride, wcNotificationOverride);

S32 wcNotificationsEnabled(void){
	return	!wcgState.flags.actorChangeMsgsDisabled &&
			wcNotificationOverride;
}

static void wcIntegrationSendMsgActorDestroyed(	WorldCollIntegration* wci,
												WorldColl* wc,
												WorldCollObject* wco,
												const Vec3 boundsMin,
												const Vec3 boundsMax,
												PSDKActor* psdkActor,
												const Vec3 sceneOffset)
{
	WorldCollIntegrationMsg msg = {0};

	msg.msgType = WCI_MSG_NOBG_ACTOR_DESTROYED;

	msg.wci = wci;
	msg.userPointer = wci->userPointer;

	msg.nobg.actorDestroyed.wc = wc;
	msg.nobg.actorDestroyed.wco = wco;
	copyVec3(boundsMin, msg.nobg.actorDestroyed.boundsMin);
	copyVec3(boundsMax, msg.nobg.actorDestroyed.boundsMax);

	msg.nobg.actorDestroyed.psdkActor = psdkActor;
	copyVec3(sceneOffset, msg.nobg.actorDestroyed.sceneOffset);

	wci->msgHandler(&msg);
}

static void wcGridCellSendMsgsActorDestroyed(	WorldCollGridCell* cell,
												WorldCollObject* wco,
												PSDKActor* psdkActor)
{
#if !PSDK_DISABLED
	Vec3 boundsMin;
	Vec3 boundsMax;

	if(	!wcNotificationsEnabled() ||
		!psdkActor)
	{
		return;
	}
	
	PERFINFO_AUTO_START_FUNC();

	if(psdkActorGetBounds(psdkActor, boundsMin, boundsMax)){
		if(cell->placement->flags.hasOffset){
			subVec3(boundsMin, cell->placement->sceneOffset, boundsMin);
			subVec3(boundsMax, cell->placement->sceneOffset, boundsMax);
		}

		collCacheInvalidate(psdkActor, boundsMin, boundsMax);

		EARRAY_CONST_FOREACH_BEGIN(wcgState.fg.wcis, i, isize);
		{
			wcIntegrationSendMsgActorDestroyed(	wcgState.fg.wcis[i],
												cell->wc,
												wco,
												boundsMin,
												boundsMax,
												psdkActor,
												cell->placement->sceneOffset);
		}
		EARRAY_FOREACH_END;
	}
	
	PERFINFO_AUTO_STOP();
#endif
}

void wcGridCellActorDestroy(WorldCollGridCell* cell,
							WorldCollObject* wco,
							AssociationNode* node,
							PSDKActor** psdkActorInOut)
{
#if !PSDK_DISABLED
	if(	!*psdkActorInOut
		&&
		(	!alNodeGetValues(node, NULL, NULL, psdkActorInOut) ||
			!*psdkActorInOut))
	{
		return;
	}

	globMovementLog("[coll] wco 0x%p destroying actor 0x%p.", wco, *psdkActorInOut);

	wcPSDKActorTrackDestroy(node, WC_PSDK_ACTOR_OWNER_STATIC, NULL, *psdkActorInOut);
	alNodeSetUserPointer(node, NULL);

	wcGridCellSendMsgsActorDestroyed(cell, wco, *psdkActorInOut);

	psdkActorDestroy(	psdkActorInOut,
						wcgState.psdkSimulationOwnership);

	cell->placement->ss->flags.needsToSimulate = 1;
	wcgState.fg.flags.ssNeedsToSimulate = 1;
#endif
}

static void wcGridCellDestroyActorCB(	WorldCollGridCell* cell,
										AssociationList* al,
										AssociationNode* node,
										PSDKActor* psdkActor)
{
#if !PSDK_DISABLED
	WorldCollScenePlacement*	sp = cell->placement;
	WorldCollObject*			wco = NULL;
	S32							wcoHasCellInSamePlacement = 0;

	if(	cell->alPrimaryStaticWCOs &&
		cell->alPrimaryDynamicWCOs)
	{
		assert(	al == cell->alPrimaryStaticWCOs ||
				al == cell->alPrimaryDynamicWCOs);
	}
	
	assert(cell->wcoCount);
	cell->wcoCount--;
	
	// Check if any of the wco's remaining cells are in the placment.
	
	alNodeGetOwners(node, NULL, &wco);
	assert(wco);

	if(sp){
		AssociationListIterator*	iter;
		WorldCollGridCell*			cellOther;

		for(alItCreate(&iter, wco->alSecondaryCells);
			alItGetOwner(iter, &cellOther);
			alItGotoNext(iter))
		{
			PSDKActor*			psdkActorOther;
			AssociationNode*	nodeOther;

			if(cellOther->placement != sp){
				continue;
			}
				
			wcoHasCellInSamePlacement = 1;
			
			if(!psdkActor){
				continue;
			}

			// Verify that the same actor is in the other cell.
				
			alItGetValues(iter, NULL, &psdkActorOther);
				
			if(psdkActorOther){
				// Cell already has the actor, verify sameness.

				assert(psdkActorOther == psdkActor);
			}else{
				// Cell has no actor, so set it.

				wcPSDKActorTrackIncrement(NULL, WC_PSDK_ACTOR_OWNER_STATIC, iter, psdkActor);
				alItSetUserPointer(iter, psdkActor);
			}
				
			// Point the psdkActor at an existing node.
				
			alItGetNode(iter, &nodeOther);
			psdkActorSetUserPointer(psdkActor, nodeOther);
		}
	
		alItDestroy(&iter);
		
		if(!wcoHasCellInSamePlacement){
			assertmsgf(	sp->ss->wcoCount > cell->wcoCount,
						"sp->ss->wcoCount(%u) <= cell->wcoCount(%u)",
						sp->ss->wcoCount,
						cell->wcoCount);

			sp->ss->wcoCountMutable--;
		}
	}
	
	if(!wcoHasCellInSamePlacement){
		// No more cells want this actor, so destroy it.

		wcGridCellActorDestroy(cell, wco, node, &psdkActor);
	}else{
		wcPSDKActorTrackDecrement(node, WC_PSDK_ACTOR_OWNER_STATIC, NULL, psdkActor);
	}
#endif
}

static void wcGridCellCreate(	WorldColl* wc,
								WorldCollGridCell** cellOut,
								S32 gridX,
								S32 gridZ)
{
	WorldCollGridCell* cell;

	MP_CREATE(WorldCollGridCell, 10);

	wcgState.alTypeCellWCOs.flags.isPrimary = 1;
	wcgState.alTypeCellWCOs.userPointerDestructor = wcGridCellDestroyActorCB;

	cell = MP_ALLOC(WorldCollGridCell);

	cell->wc = wc;

	alCreate(	&cell->alPrimaryStaticWCOs,
				cell,
				&wcgState.alTypeCellWCOs);

	alCreate(	&cell->alPrimaryDynamicWCOs,
				cell,
				&wcgState.alTypeCellWCOs);

	setVec3(cell->gridPos, gridX, 0, gridZ);
	scaleVec3(cell->gridPos, WC_GRID_CELL_SIZE, cell->worldPos);

	*cellOut = cell;
}

S32 wcoIsInScenePlacementMoreThanOnce(	const WorldCollObject* wco,
										const WorldCollScenePlacement* sp)
{
	AssociationListIterator*	iter;
	WorldCollGridCell*			cell;
	S32							found = 0;
	
	for(alItCreate(&iter, wco->alSecondaryCells);
		alItGetOwner(iter, &cell);
		alItGotoNextThenDestroy(&iter))
	{
		if(cell->placement == sp){
			if(++found == 2){
				alItDestroy(&iter);
				return 1;
			}
		}
	}

	return 0;
}

void wcCellGetWCOsInSceneCount(	WorldCollGridCell* cell,
								WorldCollStaticScene* ss,
								U32* countOut)
{
	AssociationListIterator*	iter;
	WorldCollObject*			wco;
	U32							count = 0;
	
	for(alItCreate(&iter, cell->alPrimaryStaticWCOs);
		alItGetOwner(iter, &wco);
		alItGotoNextThenDestroy(&iter))
	{
		if(wcoIsInStaticScene(wco, ss)){
			count++;
		}
	}
	
	for(alItCreate(&iter, cell->alPrimaryDynamicWCOs);
		alItGetOwner(iter, &wco);
		alItGotoNextThenDestroy(&iter))
	{
		if(wcoIsInStaticScene(wco, ss)){
			count++;
		}
	}

	*countOut = count;
}

static void wcCellGetUniqueWCOCountInScenePlacement(	const WorldCollGridCell* cell,
														const WorldCollScenePlacement* sp,
														U32* countOut,
														S32 includeStatic,
														S32 includeDynamic)
{
	AssociationListIterator*	iter;
	WorldCollObject*			wco;
	U32							count = 0;
	
	if(includeStatic){
		for(alItCreate(&iter, cell->alPrimaryStaticWCOs);
			alItGetOwner(iter, &wco);
			alItGotoNextThenDestroy(&iter))
		{
			if(!wcoIsInScenePlacementMoreThanOnce(wco, sp)){
				count++;
			}
		}
	}
	
	if(includeDynamic){
		for(alItCreate(&iter, cell->alPrimaryDynamicWCOs);
			alItGetOwner(iter, &wco);
			alItGotoNextThenDestroy(&iter))
		{
			if(!wcoIsInScenePlacementMoreThanOnce(wco, sp)){
				count++;
			}
		}
	}

	*countOut = count;
}

static void wcoRemoveFromScenePlacementByCell(	WorldCollObject* wco,
												AssociationNode* nodeWCO,
												WorldCollGridCell* cell,
												PSDKActor* psdkActor)
{
#if !PSDK_DISABLED
	AssociationListIterator*	iter;
	WorldCollGridCell*			cellOther;
	S32							foundActorInOtherCell = 0;

	for(alItCreate(&iter, wco->alSecondaryCells);
		alItGetOwner(iter, &cellOther);
		alItGotoNextThenDestroy(&iter))
	{
		if(cellOther == cell){
			PSDKActor* psdkActorVerify;
			
			alItGetValues(iter, NULL, &psdkActorVerify);
			assert(psdkActorVerify == psdkActor);
			
			if(psdkActor){
				alItSetUserPointer(iter, NULL);
			}
		}
		else if(psdkActor &&
				cellOther->placement == cell->placement)
		{
			// Found another cell in the same placement.
		
			AssociationNode*	node;
			PSDKActor*			psdkActorOther;

			foundActorInOtherCell = 1;
			
			// Point the actor at the existing node.

			alItGetNode(iter, &node);
			psdkActorSetUserPointer(psdkActor, node);

			// Verify the node's actor.
		
			alItGetValues(iter, NULL, &psdkActorOther);
			
			if(psdkActorOther){
				// Node already had the actor so just ignore it.
				
				assert(psdkActorOther == psdkActor);
			}else{
				// Put the actor in this node.

				wcPSDKActorTrackIncrement(NULL, WC_PSDK_ACTOR_OWNER_STATIC, iter, psdkActor);
				alItSetUserPointer(iter, psdkActor);
			}
		}
	}
	
	if(!foundActorInOtherCell){
		wcGridCellActorDestroy(cell, wco, nodeWCO, &psdkActor);
	}else{
		wcPSDKActorTrackDecrement(nodeWCO, WC_PSDK_ACTOR_OWNER_STATIC, NULL, psdkActor);
	}
#endif
}

static void wcGridCellRemoveListFromScenePlacement(	WorldCollGridCell* cell,
													AssociationList* al)
{
	AssociationListIterator*	iter;
	WorldCollObject*			wco;

	for(alItCreate(&iter, al);
		alItGetOwner(iter, &wco);
		alItGotoNextThenDestroy(&iter))
	{
		PSDKActor*			psdkActor;
		AssociationNode*	node;
		
		alItGetValues(iter, NULL, &psdkActor);
		alItGetNode(iter, &node);
		wcoRemoveFromScenePlacementByCell(wco, node, cell, psdkActor);
	}
}

static void wcGridCellRemoveFromScenePlacement(	WorldCollGridCell* cell,
												S32* spWasDestroyedOut,
												S32* ssWasDestroyedOut)
{
#if !PSDK_DISABLED
	WorldColl*					wc = cell->wc;
	WorldCollScenePlacement*	sp = cell->placement;
	WorldCollStaticScene*		ss;
	
	if(ssWasDestroyedOut){
		*ssWasDestroyedOut = 0;
	}

	if(!sp){
		return;
	}

	ss = sp->ss;
	
	// Find all actors that are unique to this cell and destroy them if they're unique.
	
	wcGridCellRemoveListFromScenePlacement(cell, cell->alPrimaryStaticWCOs);
	wcGridCellRemoveListFromScenePlacement(cell, cell->alPrimaryDynamicWCOs);

	// Remove the cell from the placement.

	cell->placementMutable = NULL;

	if(eaFindAndRemove(&sp->cellsMutable, cell) < 0){
		assert(0);
	}

	assert(ss->cellCount);
	ss->cellCountMutable--;

	// Destroy empty placements.

	if(!eaSize(&sp->cells)){
		eaDestroy(&sp->cellsMutable);

		// Remove placement from wc.

		if(eaFindAndRemove(&wc->placementsMutable, sp) < 0){
			assert(0);
		}

		if(!eaSize(&wc->placements)){
			eaDestroy(&wc->placementsMutable);
		}

		// Remove placement from ss.

		if(eaFindAndRemove(&ss->placementsMutable, sp) < 0){
			assert(0);
		}

		// Free placement.

		SAFE_FREE(sp);

		if(spWasDestroyedOut){
			*spWasDestroyedOut = 1;
		}
	}

	// Subtract that count of wco that are not still in the scene.

	{
		U32 wcoCountInScene;

		wcCellGetWCOsInSceneCount(cell, ss, &wcoCountInScene);
		assert(wcoCountInScene <= cell->wcoCount);
		assert(ss->wcoCount >= cell->wcoCount - wcoCountInScene);
		ss->wcoCountMutable -= cell->wcoCount - wcoCountInScene;
	}
	
	if(!eaSize(&ss->placements)){
		assert(!ss->wcoCount);
		assert(!ss->cellCount);

		eaDestroy(&ss->placementsMutable);
		
		psdkSceneDestroy(&ss->psdkSceneMutable);

		if(eaFindAndRemove(&wcgState.fg.staticScenes, ss) < 0){
			assert(0);
		}
		
		SAFE_FREE(ss);

		if(ssWasDestroyedOut){
			*ssWasDestroyedOut = 1;
		}
	}
#endif
}

static void wcGridCellDestroy(WorldCollGridCell* cell){
	alDestroy(&cell->alPrimaryStaticWCOs);
	alDestroy(&cell->alPrimaryDynamicWCOs);
	
	assert(!cell->wcoCount);
	
	wcGridCellRemoveFromScenePlacement(cell, NULL, NULL);

	MP_FREE(WorldCollGridCell, cell);
}

void wcCellSetActorRefresh(	WorldCollGridCell* cell,
							S32 isDynamicRefresh)
{
	cell->wc->fg.flags.cellNeedsActorRefresh = 1;

	if(isDynamicRefresh){
		cell->flags.wcoDynamicNeedsActorRefresh = 1;
	}else{
		cell->flags.wcoStaticNeedsActorRefresh = 1;
	}
}

S32 wcWorldCoordToGridIndex(F32 coord,
							F32 gridBlockSize)
{
	return (S32)floorf(coord / gridBlockSize+0.00001);
}

static void wcResizeGrid(	WorldCollGrid* g,
							S32 lo[3],
							S32 hi[3])
{
	S32 				oldSizeX = g->size[0];
	S32 				newSizeX = g->size[0] = hi[0] - lo[0];
	S32 				newSizeZ = g->size[2] = hi[2] - lo[2];
	WorldCollGridCell** newCells;

	newCells = callocStructs(WorldCollGridCell*, newSizeX * newSizeZ);

	if(g->cellsXZ){
		FOR_BEGIN_FROM(x, g->lo[0], g->hi[0]);
		{
			FOR_BEGIN_FROM(z, g->lo[2], g->hi[2]);
			{
				S32 oldIndex = x - g->lo[0] + oldSizeX * (z - g->lo[2]);
				S32 newIndex = x - lo[0] + newSizeX * (z - lo[2]);

				newCells[newIndex] = g->cellsXZ[oldIndex];
			}
			FOR_END;
		}
		FOR_END;

		free(g->cellsXZ);
	}

	g->cellsXZ = newCells;

	copyVec3(lo, g->lo);
	copyVec3(hi, g->hi);
}

AUTO_CMD_INT(wcgState.assertOnBadGridIndex, wcAssertOnBadGridIndexServer) ACMD_SERVERONLY;
AUTO_CMD_INT(wcgState.assertOnBadGridIndex, wcAssertOnBadGridIndexClient) ACMD_CLIENTONLY;

static S32 wcGridGetCellByGridPosEx(WorldCollGrid* g,
									WorldCollGridCell** cellOut,
									S32 x,
									S32 z,
									S32 create,
									WorldColl* wc,
									WorldCollGridCell* preExistingCell,
									WorldCollGridCell*** cellPtrPtrOut,
									const char* reason)
{
	IVec3	lo;
	IVec3	hi;
	S32		notInRange = 1|2;

	if(!g){
		return 0;
	}

	if(wc){
		if(	x < -WC_GRID_CELL_MAX_COORD ||
			x > WC_GRID_CELL_MAX_COORD ||
			z < -WC_GRID_CELL_MAX_COORD ||
			z > WC_GRID_CELL_MAX_COORD)
		{
			if(	wcgState.assertOnBadGridIndex &&
				create)
			{
				assertmsgf(0, "Bad WorldColl grid index: (%d, %d)", x, z);
			}

			if(cellOut){
				*cellOut = NULL;
			}
			return 0;
		}
	}

	copyVec3(g->lo, lo);
	copyVec3(g->hi, hi);

 	if(!g->cellsXZ){
		if(!create){
			if(cellOut){
				*cellOut = NULL;
			}
			return 0;
		}

		lo[0] = x;
		hi[0] = x + 1;
		lo[2] = z;
		hi[2] = z + 1;
	}else{
		if(x < lo[0]){
			lo[0] = x;
		}
		else if(x >= hi[0]){
			hi[0] = x + 1;
		}
		else{
			notInRange &= ~1;
		}

		if(z < lo[2]){
			lo[2] = z;
		}
		else if(z >= hi[2]){
			hi[2] = z + 1;
		}
		else{
			notInRange &= ~2;
		}
	}

	if(notInRange){
		if(!create){
			if(cellOut){
				*cellOut = NULL;
			}
			return 0;
		}else{
			wcResizeGrid(g, lo, hi);
		}
	}

	if(!g->cellsXZ){
		if(cellOut){
			*cellOut = NULL;
		}
		return 0;
	}else{
		const U32			index = x - g->lo[0] +
									g->size[0] * (z - g->lo[2]);
		WorldCollGridCell*	cell = g->cellsXZ[index];

		if(!cell){
			if(!create){
				if(cellOut){
					*cellOut = NULL;
				}
				return 0;
			}

			if(wc){
				assert(!preExistingCell);

				wcGridCellCreate(wc, &cell, x, z);
				
				g->flags.newCellToSendToBG = 1;
				g->cellsXZ[index] = cell;

				cell->size = WC_GRID_CELL_SIZE;

				EARRAY_CONST_FOREACH_BEGIN(wc->placements, i, isize);
				{
					WorldCollScenePlacement* sp = wc->placements[i];

					sp->flags.needsRecheckGridOffset = 1;
					sp->ss->flags.spNeedsRecheckGridOffset = 1;
					wcgState.fg.flags.spNeedsRecheckGridOffset = 1;
				}
				EARRAY_FOREACH_END;
			}
			else if(preExistingCell){
				cell = g->cellsXZ[index] = preExistingCell;
			}
		}

		if(cellPtrPtrOut){
			*cellPtrPtrOut = cell ? &g->cellsXZ[index] : NULL;
		}

		if(cellOut){
			*cellOut = cell;
		}

		return 1;
	}
}

static void wcGridClampPosXZ(	const WorldCollGrid* g,
								IVec3 gridPosInOut)
{
	MINMAX1(gridPosInOut[0], g->lo[0], g->hi[0] - 1);
	MINMAX1(gridPosInOut[2], g->lo[2], g->hi[2] - 1);
}

S32 wcGridGetCellByGridPos(	WorldCollGrid* g,
							WorldCollGridCell** cellOut,
							S32 x,
							S32 z)
{
	return wcGridGetCellByGridPosEx(g,
									cellOut,
									x,
									z,
									0,
									NULL,
									NULL,
									NULL,
									NULL);
}

S32 wcGetCellByGridPosFG(	WorldColl* wc,
							WorldCollGridCell** cellOut,
							S32 x,
							S32 z)
{
	return wcGridGetCellByGridPosEx(SAFE_MEMBER_ADDR(wc, fg.grid),
									cellOut,
									x,
									z,
									0,
									NULL,
									NULL,
									NULL,
									NULL);
}

S32 wcGetOrCreateCellByGridPosFG(	WorldColl* wc,
									WorldCollGridCell** cellOut,
									S32 x,
									S32 z,
									const char* reason)
{
	return wcGridGetCellByGridPosEx(SAFE_MEMBER_ADDR(wc, fg.grid),
									cellOut,
									x,
									z,
									1,
									wc,
									NULL,
									NULL,
									reason);
}

S32 wcCellGetWorldColl(	WorldCollGridCell* cell,
						WorldColl** wcOut)
{
	if(	!cell ||
		!wcOut)
	{
		return 0;
	}

	*wcOut = cell->wc;

	return 1;
}

S32 wcCellGetSceneAndOffset(WorldCollGridCell* cell,
							PSDKScene** psdkSceneOut,
							Vec3 sceneOffsetOut)
{
	if(!SAFE_MEMBER(cell, placement)){
		return 0;
	}

	if(psdkSceneOut){
		*psdkSceneOut = cell->placement->ss->psdkScene;
	}

	if(sceneOffsetOut){
		copyVec3(	cell->placement->sceneOffset,
					sceneOffsetOut);
	}

	return 1;
}

static void wcCellSetNeedsSceneCreateBG(WorldCollGridCell* cell){
	writeLockU32(&cell->wc->threadData[WC_BG_SLOT].toFG.lockFlags, 0);
	writeLockU32(&cell->threadData[WC_BG_SLOT].toFG.lockFlags, 0);

	cell->threadData[WC_BG_SLOT].toFG.flags.needsSceneCreate = 1;
	cell->wc->threadData[WC_BG_SLOT].toFG.flags.cellNeedsSceneCreate = 1;

	writeUnlockU32(&cell->threadData[WC_BG_SLOT].toFG.lockFlags);
	writeUnlockU32(&cell->wc->threadData[WC_BG_SLOT].toFG.lockFlags);
}

S32 wcCellRequestSceneCreateBG(	const WorldCollIntegrationMsg* msg,
								WorldCollGridCell* cell)
{
	if(	msg->msgType != WCI_MSG_BG_BETWEEN_SIM ||
		!cell ||
		cell->placement)
	{
		return 0;
	}

	wcCellSetNeedsSceneCreateBG(cell);

	return 1;
}

S32 wcGetGridCellByWorldPosBG(	const WorldCollIntegrationMsg* msg,
								WorldColl* wc,
								WorldCollGridCell** cellOut,
								const Vec3 pos)
{
	if(	!wc
		||
		msg->msgType != WCI_MSG_BG_BETWEEN_SIM &&
		msg->msgType != WCI_MSG_NOBG_ACTOR_CREATED)
	{
		return 0;
	}

	return wcGridGetCellByGridPosEx(&wc->bg.grid,
									cellOut,
									wcWorldCoordToGridIndex(pos[0], WC_GRID_CELL_SIZE),
									wcWorldCoordToGridIndex(pos[2], WC_GRID_CELL_SIZE),
									0,
									NULL,
									NULL,
									NULL,
									NULL);
}

S32	wcGetGridCellByWorldPosFG(	WorldColl* wc,
								WorldCollGridCell** cellOut,
								const Vec3 pos)
{
	if(!wc){
		return 0;
	}

	return wcGridGetCellByGridPosEx(&wc->fg.grid,
									cellOut,
									wcWorldCoordToGridIndex(pos[0], WC_GRID_CELL_SIZE),
									wcWorldCoordToGridIndex(pos[2], WC_GRID_CELL_SIZE),
									0,
									NULL,
									NULL,
									NULL,
									NULL);
}

typedef S32 (*WorldCollForEachGridPosCB)(	WorldColl* wc,
											S32 x,
											S32 z,
											void* userPointer);

static void wcForEachGridPosFG(	WorldColl* wc,
								WorldCollForEachGridPosCB callback,
								void* userPointer)
{
	FOR_BEGIN_FROM(x, wc->fg.grid.lo[0], wc->fg.grid.hi[0]);
	{
		FOR_BEGIN_FROM(z, wc->fg.grid.lo[2], wc->fg.grid.hi[2]);
		{
			if(!callback(wc, x, z, userPointer)){
				return;
			}
		}
		FOR_END;
	}
	FOR_END;
}

static S32 wcDestroyGridPosCB(	WorldColl* wc,
								S32 x,
								S32 z,
								void* userPointer)
{
	WorldCollGridCell** cellPtrPtr;
	WorldCollGridCell*	cell;

	wcGridGetCellByGridPosEx(	SAFE_MEMBER_ADDR(wc, fg.grid),
								&cell,
								x,
								z,
								0,
								wc,
								NULL,
								&cellPtrPtr,
								NULL);

	if(cell){
		wcGridCellDestroy(cell);

		*cellPtrPtr = NULL;
	}
	
	return 1;
}

static void wcDestroyAllGridCells(WorldColl* wc){
	wcForEachGridPosFG(wc, wcDestroyGridPosCB, NULL);
	assert(!wc->placements);

	SAFE_FREE(wc->fg.grid.cellsXZ);
	ZeroStruct(&wc->fg.grid);
	
	SAFE_FREE(wc->bg.grid.cellsXZ);
	ZeroStruct(&wc->bg.grid);
}

static S32 wcGridGetCellByWorldPos(	WorldColl* wc,
									WorldCollGrid* g,
									WorldCollGridCell** cellOut,
									const Vec3 pos)
{
	CHECK_FINITEVEC3(pos);
	return wcGridGetCellByGridPos(	g,
									cellOut,
									wcWorldCoordToGridIndex(pos[0], WC_GRID_CELL_SIZE),
									wcWorldCoordToGridIndex(pos[2], WC_GRID_CELL_SIZE));
}

static void wcStaticSceneGeometryInvalidatedFunc(	PSDKScene* psdkScene,
													void* psdkSceneUserPointer,
													PSDKActor* actor,
													void* actorUserPointer)
{
	WorldCollStaticScene*	ss = psdkSceneUserPointer;
	AssociationNode* 		nodeWCO = actorUserPointer;
	WorldCollObject* 		wco;

	if(!alNodeGetOwners(nodeWCO, NULL, &wco)){
		return;
	}

	EARRAY_CONST_FOREACH_BEGIN(ss->placements, i, isize);
	{
		const WorldCollScenePlacement* sp = ss->placements[i];

		EARRAY_CONST_FOREACH_BEGIN(sp->cells, j, jsize);
		{
			WorldCollGridCell* cell = sp->cells[j];
		
			wcCellSetActorRefresh(cell, !!wco->dynamic);
		}
		EARRAY_FOREACH_END;
	}
	EARRAY_FOREACH_END;
}

static S32 wcFindStaticScenePlacement(	const WorldColl* wc,
										const WorldCollStaticScene* ss,
										WorldCollScenePlacement** spOut)
{
	EARRAY_CONST_FOREACH_BEGIN(wc->placements, i, isize);
	{
		if(wc->placements[i]->ss == ss){
			if(spOut){
				*spOut = wc->placements[i];
			}
			return 1;
		}
	}
	EARRAY_FOREACH_END;

	return 0;
}

static S32 wcStaticSceneCreate(WorldCollStaticScene** ssOut)
{
#if PSDK_DISABLED
	return 0;
#else
	PSDKSceneDesc			sceneDesc = {0};
	WorldCollStaticScene*	ss = callocStruct(WorldCollStaticScene);

	switch(wcgState.fg.sceneConfig.staticPruningType){
		xcase 1:
			sceneDesc.staticPruningType = PSDK_SPT_DYNAMIC_AABB_TREE;
		xdefault:
			sceneDesc.staticPruningType = PSDK_SPT_STATIC_AABB_TREE;
	}
			
	switch(wcgState.fg.sceneConfig.dynamicPruningType){
		xcase 1:
			sceneDesc.dynamicPruningType = PSDK_SPT_DYNAMIC_AABB_TREE;
		xcase 2:
			sceneDesc.dynamicPruningType = PSDK_SPT_OCTREE;
		xcase 3:
			sceneDesc.dynamicPruningType = PSDK_SPT_QUADTREE;
		xcase 4:
			sceneDesc.dynamicPruningType = PSDK_SPT_NONE;
		xdefault:
			sceneDesc.dynamicPruningType = PSDK_SPT_STATIC_AABB_TREE;
	}

	sceneDesc.subdivisionLevel = wcgState.fg.sceneConfig.subdivisionLevel;

	if(!wcgState.fg.sceneConfig.maxBoundXYZ){
		wcgState.fg.sceneConfig.maxBoundXYZ = 32 * 1024;
	}

	sceneDesc.maxBoundXYZ = wcgState.fg.sceneConfig.maxBoundXYZ;
			
	sceneDesc.gravity = defaultGravity;

	if(!psdkSceneCreate(&ss->psdkSceneMutable,
						NULL,
						&sceneDesc,
						ss,
						wcStaticSceneGeometryInvalidatedFunc))
	{
		SAFE_FREE(ss);
		return 0;
	}

	eaPush(&wcgState.fg.staticScenes, ss);

	*ssOut = ss;
	return 1;
#endif
}

static S32 wcStaticSceneHasOverlappedPlacement(	const WorldCollStaticScene* ss,
												const IVec3 lo,
												const IVec3 hi,
												const WorldColl* wcIgnored)
{
	EARRAY_CONST_FOREACH_BEGIN(ss->placements, i, isize);
	{
		WorldCollScenePlacement*	sp = ss->placements[i];
		const WorldCollGrid*		g = &sp->wc->fg.grid;
		S32							overlaps = 1;

		if(sp->wc == wcIgnored){
			continue;
		}

		ARRAY_FOREACH_BEGIN_STEP(sp->gridOffset, j, 2);
		{
			if(	lo[j] >= sp->gridOffset[j] + g->hi[j] ||
				hi[j] <= sp->gridOffset[j] + g->lo[j])
			{
				overlaps = 0;
				break;
			}
		}
		ARRAY_FOREACH_END;

		if(overlaps){
			return 1;
		}
	}
	EARRAY_FOREACH_END;

	return 0;
}

static S32 wcStaticSceneMakePlacementGridOffset(WorldCollStaticScene* ss,
												const WorldColl* wc,
												IVec3 gridOffsetOut)
{
	PERFINFO_AUTO_START_FUNC();

	FOR_BEGIN(r, WC_GRID_CELL_MAX_COORD);
	{
		FOR_BEGIN(i, r + 1);
		{
			IVec3	offsets[8] = {
				{r, 0, i},
				{r, 0, -i},
				{-r, 0, i},
				{-r, 0, -i},
				{i, 0, r},
				{-i, 0, r},
				{i, 0, -r},
				{-i, 0, -r}};

			ARRAY_FOREACH_BEGIN(offsets, j);
			{
				IVec3 lo;
				IVec3 hi;

				addVec3(offsets[j], wc->fg.grid.lo, lo);
				addVec3(offsets[j], wc->fg.grid.hi, hi);

				if(	lo[0] < -WC_GRID_CELL_MAX_COORD ||
					hi[0] > WC_GRID_CELL_MAX_COORD ||
					lo[2] < -WC_GRID_CELL_MAX_COORD ||
					hi[2] > WC_GRID_CELL_MAX_COORD ||
					wcStaticSceneHasOverlappedPlacement(ss, lo, hi, wc))
				{
					continue;
				}

				copyVec3(offsets[j], gridOffsetOut);
				PERFINFO_AUTO_STOP();
				return 1;
			}
			ARRAY_FOREACH_END;
		}
		FOR_END;
	}
	FOR_END;

	PERFINFO_AUTO_STOP();
	return 0;
}

static void wcCellCreateScene(WorldCollGridCell* cell){
#if !PSDK_DISABLED
	WorldColl* wc = cell->wc;

	assert(!cell->placement);

	// Find an existing scene with enough free space.

	EARRAY_CONST_FOREACH_BEGIN(wc->placements, i, isize);
	{
		WorldCollScenePlacement*	sp = cell->wc->placements[i];
		U32							wcoCountInScene;
		U32							wcoCountNotInScene;
			
		wcCellGetWCOsInSceneCount(cell, sp->ss, &wcoCountInScene);
			
		wcoCountNotInScene = cell->wcoCount - wcoCountInScene;

		if(sp->ss->wcoCount + wcoCountNotInScene > wcgState.fg.sceneConfig.maxActors){
			continue;
		}

		cell->placementMutable = sp;
		assert(eaFind(&sp->cells, cell) < 0);
		eaPush(&sp->cellsMutable, cell);
		sp->ss->wcoCountMutable += wcoCountNotInScene;
		sp->ss->cellCountMutable++;

		break;
	}
	EARRAY_FOREACH_END;

	if(!cell->placement){
		WorldCollStaticScene*		ssBest = NULL;
		U32							wcoCountBest = 0;
		WorldCollScenePlacement*	sp;
		IVec3						gridOffsetBest;

		// Find the ss with most available space.

		EARRAY_CONST_FOREACH_BEGIN(wcgState.fg.staticScenes, i, isize);
		{
			WorldCollStaticScene*	ss = wcgState.fg.staticScenes[i];
			U32						wcoCountNew;
			IVec3					gridOffset;

			wcoCountNew = ss->wcoCount + cell->wcoCount;

			if(wcFindStaticScenePlacement(wc, ss, NULL)){
				U32 wcosAlreadyInSceneCount;

				wcCellGetWCOsInSceneCount(cell, ss, &wcosAlreadyInSceneCount);
				assert(wcosAlreadyInSceneCount <= cell->wcoCount);
				wcoCountNew -= wcosAlreadyInSceneCount;
			}

			if(	wcoCountNew < PSDK_MAX_ACTORS_PER_SCENE
				&&
				(	!ssBest ||
					wcoCountNew < wcoCountBest)
				&&
				wcStaticSceneMakePlacementGridOffset(ss, wc, gridOffset))
			{
				ssBest = ss;
				wcoCountBest = wcoCountNew;
				copyVec3(gridOffset, gridOffsetBest);
			}
		}
		EARRAY_FOREACH_END;

		if(	!ssBest ||
			wcoCountBest >= wcgState.fg.sceneConfig.maxActors)
		{
			WorldCollStaticScene* ssNew;

			if(wcStaticSceneCreate(&ssNew)){
				static U32 seed;
				if(!seed){
					seed = GetCurrentThreadId();
				}
				ssBest = ssNew;
				wcoCountBest = cell->wcoCount;

				// We need identical results from the beaconizer, and we can't risk floating point error creeping into the calculations
				// This whole feature is dubious, because it pushes the collision into ranges that the floating point unit is not really
				// capable of producing good results for.  [RMARR - 10/31/11]
				if (beaconIsBeaconizer())
				{
					setVec3(gridOffsetBest,0.0f,0.0f,0.0f);
				}
				else
				{
					setVec3(gridOffsetBest,
							randomIntRangeSeeded(&seed, RandType_LCG, -1, 1),
							0,
							randomIntRangeSeeded(&seed, RandType_LCG, -1, 1));
				}
			}
		}

		if(!ssBest){
			FatalErrorf("Can't create WorldCollStaticScene.");
		}

		if(!wcFindStaticScenePlacement(wc, ssBest, &sp)){
			sp = callocStruct(WorldCollScenePlacement);

			sp->wc = wc;
			sp->ss = ssBest;

			copyVec3(gridOffsetBest, sp->gridOffset);
			scaleVec3(sp->gridOffset, WC_GRID_CELL_SIZE, sp->sceneOffset);
			sp->flags.hasOffset = !vec3IsZero(sp->gridOffset);

			eaPush(&ssBest->placementsMutable, sp);
			eaPush(&wc->placementsMutable, sp);
		}

		cell->placementMutable = sp;
		eaPush(&sp->cellsMutable, cell);
		sp->ss->wcoCountMutable = wcoCountBest;
		sp->ss->cellCountMutable++;
	}

	if(cell->placement){
		wcCellSetActorRefresh(cell, 0);
		wcCellSetActorRefresh(cell, 1);

		wcgState.flags.needsMaterialUpdate = 1;
	}
#endif
}

S32 wcCellHasScene(	WorldCollGridCell* cell,
					const char* createReason)
{
	if(cell){
		if(cell->placement){
			return 1;
		}

		if(createReason){
			if(wcThreadIsFG()){
				if(FALSE_THEN_SET(cell->flags.needsSceneCreate)){
					verbose_printf(	"Flagging cell (%d, %d) for creation (%d objects): %s\n",
									cell->gridPos[0],
									cell->gridPos[2],
									alGetCount(cell->alPrimaryStaticWCOs),
									createReason);

					cell->wc->fg.flags.cellNeedsSceneCreate = 1;
				}
			}
			else if(wcThreadIsBG()){
				wcCellSetNeedsSceneCreateBG(cell);
			}
		}
	}

	return 0;
}

static void wcCheckForDynamicBoundsChangesFG(WorldColl* wc){
	if(!TRUE_THEN_RESET(wc->fg.flags.wcoDynamicChangedBounds)){
		return;
	}
	
	PERFINFO_AUTO_START_FUNC();

	EARRAY_CONST_FOREACH_BEGIN(wc->wcosDynamic, i, isize);
	{
		WorldCollObject* wco = wc->wcosDynamic[i];

		assert(wco->dynamic);

		if(TRUE_THEN_RESET(wco->flags.boundsChanged)){
			wcoUpdateGridFG(wc,
							wco,
							wco->dynamic->aabbMin,
							wco->dynamic->aabbMax);
		}
	}
	EARRAY_FOREACH_END;
	
	PERFINFO_AUTO_STOP();
}

static void wcRecreateAllStaticScenesFG(void){
	PERFINFO_AUTO_START_FUNC();
	
	while(eaSize(&wcgState.fg.staticScenes)){
		WorldCollStaticScene*	ss = wcgState.fg.staticScenes[0];
		S32						ssWasDestroyed = 0;

		assert(eaSize(&ss->placements));

		while(!ssWasDestroyed){
			WorldCollScenePlacement*	sp = ss->placements[0];
			WorldCollGridCell*			cell = sp->cells[0];
			
			cell->flags.needsSceneCreate = 1;
			cell->wc->fg.flags.cellNeedsSceneCreate = 1;
			
			wcGridCellRemoveFromScenePlacement(cell, NULL, &ssWasDestroyed);
		}
	}
	
	PERFINFO_AUTO_STOP();
}

static void wcStaticSceneSplitCB(	WorldCollStaticScene* ss,
									void* userPointer)
{
	PERFINFO_AUTO_START_FUNC();
	
	while(	ss->wcoCount > wcgState.fg.sceneConfig.maxActors &&
			ss->cellCount > 1)
	{
		WorldCollGridCell*	cellToRemove = NULL;
		U32					uniqueCountMax = 0;
		S32					ssWasDestroyed;

		// Find the cell with the most unique WCOs.

		EARRAY_CONST_FOREACH_BEGIN(ss->placements, i, isize);
		{
			WorldCollScenePlacement* sp = ss->placements[i];

			EARRAY_CONST_FOREACH_BEGIN(sp->cells, j, jsize);
			{
				WorldCollGridCell*	cell = sp->cells[j];
				U32					uniqueCount;
				
				wcCellGetUniqueWCOCountInScenePlacement(cell, sp, &uniqueCount, 1, 1);
				
				if(	!cellToRemove ||
					uniqueCount > uniqueCountMax)
				{
					cellToRemove = cell;
					uniqueCountMax = uniqueCount;
				}
			}
			EARRAY_FOREACH_END;
		}
		EARRAY_FOREACH_END;
					
		wcGridCellRemoveFromScenePlacement(cellToRemove, NULL, &ssWasDestroyed);
			
		assert(!ssWasDestroyed);
			
		cellToRemove->flags.needsSceneCreate = 1;
		cellToRemove->wc->fg.flags.cellNeedsSceneCreate = 1;
	}
	
	PERFINFO_AUTO_STOP();
}

static S32 wcStaticSceneGetAtGridPos(	const WorldCollStaticScene* ss,
										const IVec3 gridPos,
										WorldColl** wcOut,
										WorldCollScenePlacement** spOut,
										WorldCollGridCell** cellOut)
{
	EARRAY_CONST_FOREACH_BEGIN(ss->placements, i, isize);
	{
		WorldCollScenePlacement*	sp = ss->placements[i];
		const WorldCollGrid*		g = &sp->wc->fg.grid;
		S32							isIn = 1;

		ARRAY_FOREACH_BEGIN_STEP(sp->gridOffset, j, 2);
		{
			if(	gridPos[j] < sp->gridOffset[j] + g->lo[j] ||
				gridPos[j] >= sp->gridOffset[j] + g->hi[j])
			{
				isIn = 0;
				break;
			}
		}
		ARRAY_FOREACH_END;

		if(!isIn){
			continue;
		}

		if(wcOut){
			*wcOut = sp->wc;
		}

		if(spOut){
			*spOut = sp;
		}

		if(cellOut){
			*cellOut = NULL;

			wcGridGetCellByGridPos(	&sp->wc->fg.grid,
									cellOut,
									gridPos[0] - sp->gridOffset[0],
									gridPos[2] - sp->gridOffset[2]);
		}

		return 1;
	}
	EARRAY_FOREACH_END;

	return 0;
}

static void wcStaticSceneCheckGridOffsetCB(	WorldCollStaticScene* ss,
											void* userPointer)
{
	if(!TRUE_THEN_RESET(ss->flags.spNeedsRecheckGridOffset)){
		return;
	}

	EARRAY_CONST_FOREACH_BEGIN(ss->placements, i, isize);
	{
		WorldCollScenePlacement*	sp = ss->placements[i];
		const WorldCollGrid*		g;
		IVec3						lo;
		IVec3						hi;

		if(!TRUE_THEN_RESET(sp->flags.needsRecheckGridOffset)){
			continue;
		}

		g = &sp->wc->fg.grid;
		addVec3(sp->gridOffset, g->lo, lo);
		addVec3(sp->gridOffset, g->hi, hi);

		if(wcStaticSceneHasOverlappedPlacement(ss, lo, hi, sp->wc)){
			S32 spWasDestroyed = 0;

			while(!spWasDestroyed){
				WorldCollGridCell* cell = sp->cells[0];

				wcGridCellRemoveFromScenePlacement(cell, &spWasDestroyed, NULL);
				wcCellHasScene(cell, "moved after grid change");
			}
		}
	}
	EARRAY_FOREACH_END;
}

static S32 wcCreateNecessarySceneForGridPosCB(	WorldColl* wc,
												S32 x,
												S32 z,
												void* userPointer)
{
	WorldCollGridCell* cell;

	if(!wcGridGetCellByGridPos(&wc->fg.grid, &cell, x, z)){
		return 1;
	}

	if(TRUE_THEN_RESET(cell->threadData[WC_BG_SLOT].toFG.flags.needsSceneCreate)){
		cell->flags.needsSceneCreate = 1;
	}

	if(TRUE_THEN_RESET(cell->flags.needsSceneCreate)){
		wcCellCreateScene(cell);
	}
	
	return 1;
}

static void wcCreateNecessaryScenesFG(WorldColl* wc){
	if(TRUE_THEN_RESET(wc->threadData[WC_BG_SLOT].toFG.flags.cellNeedsSceneCreate)){
		wc->fg.flags.cellNeedsSceneCreate = 1;
	}
	
	if(!TRUE_THEN_RESET(wc->fg.flags.cellNeedsSceneCreate)){
		return;
	}

	PERFINFO_AUTO_START_FUNC();
	
	wcForEachGridPosFG(wc, wcCreateNecessarySceneForGridPosCB, NULL);
	
	PERFINFO_AUTO_STOP();
}

static void wcCellUpdateActorsInList(	WorldCollGridCell* cell,
										AssociationList* al)
{
#if !PSDK_DISABLED
	AssociationListIterator*	iter;
	WorldCollObject*			wco;

	for(alItCreate(&iter, al);
		alItGetOwner(iter, &wco);
		alItGotoNextThenDestroy(&iter))
	{
		PSDKActor* psdkActor;
		
		if(wco->flags.destroyed){
			alItNodeRemove(iter);
			continue;
		}
		
		if(!alItGetValues(iter, NULL, &psdkActor)){
			assert(0);
		}

		if(	psdkActor &&
			!psdkActorHasInvalidGeo(psdkActor))
		{
			continue;
		}

		// Actor needs to be created or re-created.

		if(	!psdkActor &&
			wcoFindActorInScenePlacement(wco, cell->placement, &psdkActor))
		{
			// Got the actor from another cell.
			// Put the actor in this wco just in case the invalid geo case below is hit.
			// If this isn't here then wcoSetActorInScenePlacement might fail an assert
			// when it detects that the actor isn't in any other cells.

			wcPSDKActorTrackIncrement(NULL, WC_PSDK_ACTOR_OWNER_STATIC, iter, psdkActor);
			alItSetUserPointer(iter, psdkActor);
		}
			
		// Remove this actor from all cells if it's invalid.
			
		if(	!psdkActor ||
			psdkActorHasInvalidGeo(psdkActor))
		{
			// Destroy invalidated actor.

			AssociationNode* node;
				
			alItGetNode(iter, &node);

			if(psdkActor){
				// Actor is invalidated, so remove it from all cells and destroy it.

				PERFINFO_AUTO_START("destroyOldActor", 1);
					
				wcoSetActorInScenePlacement(wco, cell, cell->placement, psdkActor, NULL);

				wcGridCellActorDestroy(cell, wco, node, &psdkActor);
					
				PERFINFO_AUTO_STOP();
			}

			wcoCreateCellActor(wco, cell, node);
		}
	}
#endif
}

static void wcCellUpdateActors(WorldCollGridCell* cell){
	if(!cell->placement){
		cell->flags.wcoStaticNeedsActorRefresh = 0;
		cell->flags.wcoDynamicNeedsActorRefresh = 0;
	}
	
	if(	!cell->flags.wcoStaticNeedsActorRefresh &&
		!cell->flags.wcoDynamicNeedsActorRefresh)
	{
		return;
	}
	
	PERFINFO_AUTO_START_FUNC();

	// Check for missing or invalidated actors.

	if(TRUE_THEN_RESET(cell->flags.wcoDynamicNeedsActorRefresh)){
		PERFINFO_AUTO_START("updateDynamicActors", 1);
			wcCellUpdateActorsInList(cell, cell->alPrimaryDynamicWCOs);
		PERFINFO_AUTO_STOP();
	}

	if(TRUE_THEN_RESET(cell->flags.wcoStaticNeedsActorRefresh)){
		PERFINFO_AUTO_START("updateStaticActors", 1);
			wcCellUpdateActorsInList(cell, cell->alPrimaryStaticWCOs);
		PERFINFO_AUTO_STOP();
	}

	PERFINFO_AUTO_STOP();
}

void wcForEachWorldColl(ForEachWorldCollCallback callback,
						void* userPointer)
{
	EARRAY_CONST_FOREACH_BEGIN(wcgState.wcs, i, isize);
	{
		callback(wcgState.wcs[i], userPointer);
	}
	EARRAY_FOREACH_END;
}

void wcForEachStaticScene(	ForEachStaticSceneCallback callback,
							void* userPointer)
{
	EARRAY_CONST_FOREACH_BEGIN(wcgState.fg.staticScenes, i, isize);
	{
		callback(wcgState.fg.staticScenes[i], userPointer);
	}
	EARRAY_FOREACH_END;
}

void wcForEachIntegration(	ForEachWorldCollIntegrationCallback callback,
							void* userPointer)
{
	EARRAY_CONST_FOREACH_BEGIN(wcgState.fg.wcis, i, isize);
	{
		callback(wcgState.fg.wcis[i], userPointer);
	}
	EARRAY_FOREACH_END;
}

static void wcForceSimulateStaticScenesCB(	WorldCollStaticScene* ss,
											void* unused)
{
	ss->flags.needsToSimulate = 1;
	wcgState.fg.flags.ssNeedsToSimulate = 1;
}

AUTO_COMMAND;
void wcForceSimulateStaticScenes(void){
	wcForEachStaticScene(wcForceSimulateStaticScenesCB, NULL);
}

#if !PSDK_DISABLED
static void wcStaticSceneSimulateCB(WorldCollStaticScene* ss,
									S32* didSimulateOut)
{
	U32 actorCount = 0;

	if(!ss->flags.needsToSimulate){
		return;
	}

	*didSimulateOut = 1;

	psdkSceneGetActorCounts(ss->psdkScene, &actorCount, NULL, NULL, NULL, NULL);
			
	#define START(x) else if(actorCount <= x){\
		PERFINFO_AUTO_START("simulateForNewActors(<="#x")", 1);}
	if(0){}
	START(1000)
	START(2000)
	START(3000)
	START(4000)
	START(5000)
	START(6000)
	START(7000)
	START(8000)
	START(9000)
	START(10000)
	START(15000)
	START(20000)
	START(25000)
	START(30000)
	START(35000)
	START(40000)
	START(45000)
	START(50000)
	START(55000)
	START(60000)
	START(65000)
	else{PERFINFO_AUTO_START("simulateForNewActors(>65000)", 1);}
	#undef START
	{
		psdkSceneSimulate(ss->psdkScene, 0.001f);
	}
	PERFINFO_AUTO_STOP();
}
#endif

#if !PSDK_DISABLED
static void wcStaticSceneFetchResultsCB(WorldCollStaticScene* ss,
										void* userPointer)
{
	if(TRUE_THEN_RESET(ss->flags.needsToSimulate)){
		psdkSceneFetchResults(ss->psdkScene, 1);
	}
}
#endif

static void wcGlobalSimulateForNewActorsFG(void){
#if !PSDK_DISABLED
	S32 didSimulate = 0;

	psdkSimulationLockFG(wcgState.psdkSimulationOwnership);
	psdkSimulationLockBG(wcgState.psdkSimulationOwnership);
	
	PERFINFO_AUTO_START("simulateForNewActors", 1);
	
	wcForEachStaticScene(wcStaticSceneSimulateCB, &didSimulate);

	if(didSimulate){
		PERFINFO_AUTO_START("fetchResults", 1);
		wcForEachStaticScene(wcStaticSceneFetchResultsCB, NULL);
		PERFINFO_AUTO_STOP();
	}

	PERFINFO_AUTO_STOP();
	
	psdkSimulationUnlockBG(wcgState.psdkSimulationOwnership);
	psdkSimulationUnlockFG(wcgState.psdkSimulationOwnership);
#endif
}

static S32 wcUpdateActorsForGridPosCB(	WorldColl* wc,
										S32 x,
										S32 z,
										void* userPointer)
{
	WorldCollGridCell* cell;

	if(wcGetCellByGridPosFG(wc, &cell, x, z)){
		wcCellUpdateActors(cell);
	}
	
	return 1;
}

static void wcUpdateActorsFG(WorldColl* wc){
#if !PSDK_DISABLED
	if(!TRUE_THEN_RESET(wc->fg.flags.cellNeedsActorRefresh)){
		return;
	}
	
	PERFINFO_AUTO_START_FUNC();

	wcForEachGridPosFG(wc, wcUpdateActorsForGridPosCB, NULL);

	PERFINFO_AUTO_STOP();
#endif
}

S32 wcMaterialCreate(	WorldCollMaterial** wcmOut,
						const PSDKMaterialDesc* materialDesc)
{
#if PSDK_DISABLED
	return 0;
#else
	if(	!wcmOut ||
		!materialDesc)
	{
		return 0;
	}else{
		WorldCollMaterial* wcm;

		MP_CREATE(WorldCollMaterial, 16);

		//add a dummy 0th material so we don't over write the physx default material
		if (eaSize(&wcgState.materials) == 0) {
			wcm = MP_ALLOC(WorldCollMaterial);
			wcm->index = eaPush(&wcgState.materials, wcm);
		}

		wcm = MP_ALLOC(WorldCollMaterial);

		wcm->index = eaPush(&wcgState.materials, wcm);
		wcm->updated = 1;
		wcm->materialDesc = *materialDesc;

		wcgState.flags.needsMaterialUpdate = 1;

		*wcmOut = wcm;

		return 1;
	}
#endif
}

S32 wcMaterialGetByIndex(	WorldCollMaterial** wcmOut,
							U32 index,
							const PSDKMaterialDesc* materialDesc)
{
#if PSDK_DISABLED
	return 0;
#else
	if(	!wcmOut ||
		!materialDesc ||
		index > 0xffff)
	{
		return 0;
	}else{
		while(eaUSize(&wcgState.materials) <= index){
			wcMaterialCreate(wcmOut, materialDesc);
		}

		*wcmOut = wcgState.materials[index];

		wcMaterialSetDesc(*wcmOut, materialDesc);

		return 1;
	}
#endif
}

S32	wcMaterialGetIndex(	WorldCollMaterial* wcm,
						U32* indexOut)
{
	if(	!wcm ||
		!indexOut)
	{
		return 0;
	}

	*indexOut = wcm->index;

	return 1;
}

S32 wcMaterialSetDesc(	WorldCollMaterial* wcm,
						const PSDKMaterialDesc* materialDesc)
{
#if PSDK_DISABLED
	return 0;
#else
	if(	!wcm ||
		!materialDesc)
	{
		return 0;
	}

	wcm->materialDesc = *materialDesc;
	wcm->updated = 1;
	wcgState.flags.needsMaterialUpdate = 1;

	return 1;
#endif
}

static void wcPSDKSceneUpdateMaterialsFG(	PSDKScene* psdkScene,
											U32* materialSetCountInOut,
											S32 materialUpdated)
{
#if !PSDK_DISABLED
	U32 materialSetCount = *materialSetCountInOut;
	U32 i;

	if(materialUpdated){
		for(i = 0; i < materialSetCount; i++){
			assert(i < eaUSize(&wcgState.materials));

			if(wcgState.materials[i]->updated){
				psdkSceneSetMaterial(	psdkScene,
										i,
										&wcgState.materials[i]->materialDesc);
			}
		}
	}

	while(materialSetCount < eaUSize(&wcgState.materials)){
		psdkSceneSetMaterial(	psdkScene,
								materialSetCount,
								&wcgState.materials[materialSetCount]->materialDesc);

		materialSetCount++;
	}
	
	*materialSetCountInOut = materialSetCount;
#endif
}

static void wcStaticSceneUpdateMaterialsCB(	WorldCollStaticScene* ss,
											const S32* materialUpdated)
{
	wcPSDKSceneUpdateMaterialsFG(	ss->psdkScene,
									&ss->materialSetCount,
									*materialUpdated);
}

static void wcIntegrationUpdateMaterialsCB(	const WorldCollIntegration* wci,
											const S32* materialUpdated)
{
	EARRAY_CONST_FOREACH_BEGIN(wci->scenes, j, jsize);
	{
		WorldCollScene* scene = wci->scenes[j];
			
		wcPSDKSceneUpdateMaterialsFG(	scene->psdkScene,
										&scene->materialSetCount,
										*materialUpdated);
	}
	EARRAY_FOREACH_END;
}

static void wcGlobalUpdateMaterialsFG(void){
	S32 materialUpdated = 0;

	PERFINFO_AUTO_START_FUNC();

	EARRAY_CONST_FOREACH_BEGIN(wcgState.materials, i, isize);
	{
		if(wcgState.materials[i]->updated){
			materialUpdated = 1;
			break;
		}
	}
	EARRAY_FOREACH_END;

	wcForEachStaticScene(wcStaticSceneUpdateMaterialsCB, &materialUpdated);
	wcForEachIntegration(wcIntegrationUpdateMaterialsCB, &materialUpdated);
	
	if(materialUpdated){
		EARRAY_CONST_FOREACH_BEGIN(wcgState.materials, i, isize);
		{
			wcgState.materials[i]->updated = 0;
		}
		EARRAY_FOREACH_END;
	}

	wcgState.flags.needsMaterialUpdate = 0;
	
	PERFINFO_AUTO_STOP();
}

static S32 wcUpdateBGGridForGridPosCB(	WorldColl* wc,
										S32 x,
										S32 z,
										void* userPointer)
{
	WorldCollGridCell* cell;

	if(wcGetCellByGridPosFG(wc, &cell, x, z)){
		wcGridGetCellByGridPosEx(	SAFE_MEMBER_ADDR(wc, bg.grid),
									NULL,
									x,
									z,
									1,
									NULL,
									cell,
									NULL,
									NULL);
	}
	
	return 1;
}

static void wcUpdateBGGridFG(WorldColl* wc){
	if(!TRUE_THEN_RESET(wc->fg.grid.flags.newCellToSendToBG)){
		return;
	}
	
	PERFINFO_AUTO_START_FUNC();

	wcForEachGridPosFG(wc, wcUpdateBGGridForGridPosCB, NULL);
	
	PERFINFO_AUTO_STOP();
}

static void wcUpdateWhileSimSleepsNOBG(WorldColl* wc){
	if(wcgState.fg.flags.simulating){
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	wcCheckForDynamicBoundsChangesFG(wc);
	wcCreateNecessaryScenesFG(wc);
	wcUpdateActorsFG(wc);
	wcUpdateBGGridFG(wc);

	PERFINFO_AUTO_STOP_FUNC();
}

static void wcSimulateAndFetchAllScenes(S32 variableOnly,
										F32 variableTimestep,
										F32 timeStep)
{
#if !PSDK_DISABLED
	psdkSimulationLockBG(wcgState.psdkSimulationOwnership);

	PERFINFO_AUTO_START("simulateAllScenes", 1);
	{
		EARRAY_CONST_FOREACH_BEGIN(wcgState.bg.scenes, i, isize);
			WorldCollScene* scene = wcgState.bg.scenes[i];

			if(	!variableOnly ||
				scene->flags.useVariableTimestep)
			{
				psdkSceneSimulate(	scene->psdkScene,
									scene->flags.useVariableTimestep ?
										variableTimestep :
										timeStep);
			}
		EARRAY_FOREACH_END;
	}
	PERFINFO_AUTO_STOP_START("fetchAllScenes", 1);
	{
		EARRAY_CONST_FOREACH_BEGIN(wcgState.bg.scenes, i, isize);
			WorldCollScene* scene = wcgState.bg.scenes[i];

			if(	!variableOnly ||
				scene->flags.useVariableTimestep)
			{
				psdkSceneFetchResults(scene->psdkScene, 1);
			}
		EARRAY_FOREACH_END;
	}
	PERFINFO_AUTO_STOP();

	psdkSimulationUnlockBG(wcgState.psdkSimulationOwnership);
#endif
}

static void wcSendMsgsBetweenSimBG(	U32 instanceThisFrame,
									F32 deltaSecondsPerStep,
									S32 noProcessThisFrame)
{
	EARRAY_CONST_FOREACH_BEGIN(wcgState.bg.wcis, i, isize);
	{
		WorldCollIntegration*	wci = wcgState.bg.wcis[i];
		WorldCollIntegrationMsg	msg = {0};

		msg.msgType = WCI_MSG_BG_BETWEEN_SIM;

		msg.wci = wci;
		msg.userPointer = wci->userPointer;

		msg.bg.betweenSim.instanceThisFrame = instanceThisFrame;
		msg.bg.betweenSim.deltaSeconds = deltaSecondsPerStep;
		msg.bg.betweenSim.flags.noProcessThisFrame = noProcessThisFrame;

		wci->msgHandler(&msg);
	}
	EARRAY_FOREACH_END;
}

static void wcRunThreadFrame(void){
	S64 time_physics_budget_start = 0;
	PERFINFO_AUTO_START_FUNC();

	wlPerfStartPhysicsBudget(&time_physics_budget_start);

	etlAddEvent(wcgState.eventTimer, "Run frame", ELT_CODE, ELTT_BEGIN);

	if(!wcgState.threadShared.stepCount){
		wcSimulateAndFetchAllScenes(1,
									wcgState.threadShared.deltaSeconds,
									0);

		wcSendMsgsBetweenSimBG(0, wcgState.threadShared.deltaSecondsPerStep, 1);
	}else{
		F32 variableTimeStep =	wcgState.threadShared.deltaSeconds /
								wcgState.threadShared.stepCount;

		FOR_BEGIN(frameStepIndex, (S32)wcgState.threadShared.stepCount);
		{
			wcSimulateAndFetchAllScenes(0,
										variableTimeStep,
										wcgState.threadShared.deltaSecondsPerStep);

			wcSendMsgsBetweenSimBG(	frameStepIndex,
											wcgState.threadShared.deltaSecondsPerStep,
											0);
		}
		FOR_END;
	}

	etlAddEvent(wcgState.eventTimer, "Run frame", ELT_CODE, ELTT_END);

	wlPerfEndPhysicsBudget(time_physics_budget_start);

	PERFINFO_AUTO_STOP();
}

static void wcGlobalUpdateWhileSimSleeps(void){
	if(wcgState.fg.flags.simulating){
		return;
	}

	// Check for new WorldCollIntegrations to send to BG.

	if(TRUE_THEN_RESET(wcgState.fg.flags.wciListUpdatedToBG)){
		PERFINFO_AUTO_START("updatedIntegrationListToBG", 1);

		EARRAY_CONST_FOREACH_BEGIN(wcgState.fg.wcis, i, isize);
		{
			WorldCollIntegration* wci = wcgState.fg.wcis[i];

			if(FALSE_THEN_SET(wci->fg.flags.inListBG)){
				eaPush(&wcgState.bg.wcisMutable, wci);
			}
		}
		EARRAY_FOREACH_END;

		PERFINFO_AUTO_STOP();
	}

	if(TRUE_THEN_RESET(wcgState.fg.flags.ssRecreateAll)){
		wcRecreateAllStaticScenesFG();
	}

	if(TRUE_THEN_RESET(wcgState.fg.flags.spNeedsRecheckGridOffset)){
		wcForEachStaticScene(wcStaticSceneCheckGridOffsetCB, NULL);
	}
	
	if(TRUE_THEN_RESET(wcgState.fg.flags.ssNeedsToSplit)){
		wcForEachStaticScene(wcStaticSceneSplitCB, NULL);
	}

	if(TRUE_THEN_RESET(wcgState.flags.needsMaterialUpdate)){
		wcGlobalUpdateMaterialsFG();
	}

	EARRAY_CONST_FOREACH_BEGIN(wcgState.wcs, i, isize);
	{
		WorldColl* wc = wcgState.wcs[i];

		wcUpdateWhileSimSleepsNOBG(wc);
	}
	EARRAY_FOREACH_END;

	if(TRUE_THEN_RESET(wcgState.fg.flags.ssNeedsToSimulate)){
		wcGlobalSimulateForNewActorsFG();
	}
}

static S32 wcStartSimFG(const FrameLockedTimer* flt){
#if PSDK_DISABLED
	return 0;
#else
	if(wcgState.fg.flags.simulating){
		return 0;
	}

	etlAddEvent(wcgState.eventTimer, "Simulate (FG)", ELT_CODE, ELTT_BEGIN);

	wcGlobalUpdateWhileSimSleeps();

	wcgState.fg.flags.simulating = 1;

	PERFINFO_AUTO_START("startSimulation", 1);
	{
		frameLockedTimerGetProcesses(	flt,
										NULL,
										&wcgState.threadShared.stepCount,
										NULL,
										NULL);

		frameLockedTimerGetCurTimes(flt,
									&wcgState.threadShared.deltaSeconds,
									NULL,
									NULL);

		wcgState.threadShared.deltaSecondsPerStep = 1.f/60.f;

		wcgState.flags.fgSlot = !wcgState.flags.fgSlot;
		WC_BG_SLOT = !wcgState.flags.fgSlot;

		psdkSimulationLockFG(wcgState.psdkSimulationOwnership);

		if(wcgState.flags.notThreaded){
			wcRunThreadFrame();
		}else{
			PERFINFO_AUTO_START("SetEvent", 1);
				SetEvent(wcgState.threadShared.eventStartFrameToBG);
			PERFINFO_AUTO_STOP();
		}
	}
	PERFINFO_AUTO_STOP();

	etlAddEvent(wcgState.eventTimer, "Simulate (FG)", ELT_CODE, ELTT_END);
	return 1;
#endif
}

void wcWaitForSimulationToEndFG(S32 waitForever,
								S32* isDoneSimulatingOut,
								S32 skipUpdateWhileSimSleeps)
{
#if !PSDK_DISABLED
	S32 done = 1;

	if(isDoneSimulatingOut){
		*isDoneSimulatingOut = 0;
	}

	if(!wcThreadIsFG()){
		return;
	}

	if(!wcgState.fg.flags.simulating){
		if(isDoneSimulatingOut){
			*isDoneSimulatingOut = 1;
		}

		if(!skipUpdateWhileSimSleeps){
			wcGlobalUpdateWhileSimSleeps();
		}

		return;
	}

	if(!wcgState.flags.notThreaded){
		PERFINFO_AUTO_START("waitForDone", 1);
		{
			DWORD waitResult;

			etlAddEvent(wcgState.eventTimer, "Wait for thread", ELT_CODE, ELTT_BEGIN);
			#if _PS3
				waitResult = WaitForEvent(	wcgState.threadShared.eventFrameFinishedToFG,
											waitForever ? INFINITE : 0);
			#else
				WaitForSingleObjectWithReturn(	wcgState.threadShared.eventFrameFinishedToFG,
												waitForever ? INFINITE : 0,
												waitResult);
			#endif

			etlAddEvent(wcgState.eventTimer, "Wait for thread", ELT_CODE, ELTT_END);

			done =	waitResult == WAIT_OBJECT_0 ||
					waitResult == WAIT_ABANDONED_0;
		}
		PERFINFO_AUTO_STOP();
	}

	if(!done){
		return;
	}

	PERFINFO_AUTO_START("done", 1);
	{
		psdkSimulationUnlockFG(wcgState.psdkSimulationOwnership);

		wcgState.fg.flags.simulating = 0;

		if(TRUE_THEN_RESET(wcgState.fg.flags.connectToDebugger)){
			psdkConnectRemoteDebugger("localhost", 5425);
		}

		if(isDoneSimulatingOut){
			*isDoneSimulatingOut = 1;
		}

		if(!skipUpdateWhileSimSleeps){
			wcGlobalUpdateWhileSimSleeps();
		}
	}
	PERFINFO_AUTO_STOP();
#endif
}

S32 wcForceSimulationUpdate(void){
	if(!wcThreadIsFG()){
		return 0;
	}
	
	wcWaitForSimulationToEndFG(1, NULL, 0);
	
	return 1;
}

static void wcSendMsgsBeforeSimSleepsFG(const FrameLockedTimer* flt){
	EARRAY_CONST_FOREACH_BEGIN(wcgState.fg.wcis, i, isize);
	{
		WorldCollIntegration*	wci = wcgState.fg.wcis[i];
		WorldCollIntegrationMsg	msg = {0};

		msg.msgType = WCI_MSG_FG_BEFORE_SIM_SLEEPS;

		msg.wci = wci;
		msg.userPointer = wci->userPointer;

		msg.fg.beforeSimSleeps.flt = flt;

		wci->msgHandler(&msg);
	}
	EARRAY_FOREACH_END;
}

static void wcPrintSceneInfoWhileSimSleeps(void){
#if !PSDK_DISABLED
	if(!TRUE_THEN_RESET(wcgState.flags.printSceneInfoOnSwap)){
		return;
	}

	EARRAY_CONST_FOREACH_BEGIN(wcgState.fg.wcis, i, isize);
		WorldCollIntegration* wci = wcgState.fg.wcis[i];
		
		EARRAY_CONST_FOREACH_BEGIN(wci->scenes, j, jsize);
			WorldCollScene* scene = wci->scenes[j];
			U32				psdkActorCount;
			
			psdkSceneGetActorCounts(scene->psdkScene, &psdkActorCount, NULL, NULL, NULL, NULL);

			printf(	"WorldCollIntegration 0x%p:%s, Scene 0x%p:%s:"
					" %d WorldCollActors, %d PSDKActors, %d WCOS (%d al)\n",
					wci,
					wci->name,
					scene,
					scene->name,
					eaSize(&scene->actors),
					psdkActorCount,
					stashGetCount(scene->wcos.st),
					alGetCount(scene->wcos.alPrimaryWCOs));
		EARRAY_FOREACH_END;
	EARRAY_FOREACH_END;
#endif
}

static void wcIntegrationSendMsgWhileSimSleepsCB(	WorldCollIntegration* wci,
													const FrameLockedTimer* flt)
{
	WorldCollIntegrationMsg	msg = {0};

	msg.msgType = WCI_MSG_NOBG_WHILE_SIM_SLEEPS;

	msg.wci = wci;
	msg.userPointer = wci->userPointer;

	msg.nobg.whileSimSleeps.flt = flt;

	wci->msgHandler(&msg);
}

static void wcSendMsgsWhileSimSleepsFG(const FrameLockedTimer* flt){
	wcPrintSceneInfoWhileSimSleeps();

	wcForEachIntegration(wcIntegrationSendMsgWhileSimSleepsCB, (void*)flt);
}

static void wcSendMsgsAfterSimWakesFG(const FrameLockedTimer* flt){
	EARRAY_CONST_FOREACH_BEGIN(wcgState.fg.wcis, i, isize);
	{
		WorldCollIntegration*	wci = wcgState.fg.wcis[i];
		WorldCollIntegrationMsg	msg = {0};

		msg.msgType = WCI_MSG_FG_AFTER_SIM_WAKES;

		msg.wci = wci;
		msg.userPointer = wci->userPointer;

		msg.fg.afterSimWakes.flt = flt;

		wci->msgHandler(&msg);
	}
	EARRAY_FOREACH_END;
}

void wcSwapSimulation(const FrameLockedTimer* flt){
	if(!wcThreadIsFG()){
		return;
	}

	PERFINFO_AUTO_START_FUNC_PIX();
	{
		#define RUN(x, y)	PERFINFO_AUTO_START_PIX(#x, 1);\
							coarseTimerAddInstance(NULL, #x);\
							x y;\
							coarseTimerStopInstance(NULL, #x);\
							PERFINFO_AUTO_STOP_CHECKED_PIX(#x)
		{
			RUN(wcSendMsgsBeforeSimSleepsFG, (flt));
			RUN(wcWaitForSimulationToEndFG, (1, NULL, 0));
			RUN(wcSendMsgsWhileSimSleepsFG, (flt));
			RUN(wcStartSimFG, (flt));
			RUN(wcSendMsgsAfterSimWakesFG, (flt));
		}
		#undef RUN
	}
	PERFINFO_AUTO_STOP_FUNC_PIX();
}

S32 wcGetThreadGrid(WorldColl* wc,
					WorldCollGrid** gOut)
{
	if(!wc){
		return 0;
	}

	if(wcThreadIsFG()){
		*gOut = &wc->fg.grid;
	}
	else if(wcThreadIsBG()){
		*gOut = &wc->bg.grid;
	}else{
		return 0;
	}
	
	return 1;
}

static S32 wcCollideGetGetGridCell(	WorldCollGrid* g,
									WorldCollGridCell** cellOut,
									S32 x,
									S32 z,
									const char* reason,
									WorldCollCollideResultsErrorFlags* errorFlagsOut)
{
	if(!wcGridGetCellByGridPos(g, cellOut, x, z)){
		if(errorFlagsOut){
			errorFlagsOut->noCell = 1;
		}
		return 0;
	}
			
	if(!wcCellHasScene(*cellOut, reason)){
		if(errorFlagsOut){
			errorFlagsOut->noScene = 1;
		}
		return 0;
	}

	return 1;
}

#if !PSDK_DISABLED
static S32 wcCollideValidateResults(WorldColl* wc,
									WorldCollGrid* g,
									const WorldCollGridCell* cell,
									const PSDKRaycastResults* psdkResults,
									WorldCollCollideResults* resultsOut)
{
	WorldCollGridCell* cellAtPos;

	if(	!wcGridGetCellByWorldPos(wc, g, &cellAtPos, psdkResults->posWorldImpact) ||
		cell != cellAtPos)
	{
		return 0;
	}

	if(psdkResults->actor){
		AssociationList*	alWCO;
		AssociationList*	alCell;
		AssociationNode*	node;
		WorldCollGridCell*	cellCheck;

		psdkActorGetUserPointer(&node, psdkResults->actor);
		alNodeGetValues(node, &alCell, &alWCO, NULL);
		alGetOwner(&cellCheck, alCell);

		if(cellCheck->wc != wc){
			return 0;
		}

		if(resultsOut){
			resultsOut->node = node;
			resultsOut->cell = cellCheck;
			resultsOut->psdkActor = psdkResults->actor;
			alGetOwner(&resultsOut->wco, alWCO);
		}
	}

	if(resultsOut){
		resultsOut->hitSomething = 1;

		copyVec3(	psdkResults->posWorldImpact,
					resultsOut->posWorldImpact);

		copyVec3(	psdkResults->posWorldEnd,
					resultsOut->posWorldEnd);

		copyVec3(	psdkResults->normalWorld,
					resultsOut->normalWorld);

		resultsOut->tri.index = psdkResults->tri.index;
		resultsOut->tri.u = psdkResults->tri.u;
		resultsOut->tri.v = psdkResults->tri.v;

		resultsOut->distance = psdkResults->distance;
	}

	return 1;
}
#endif

static S32 wcCollideGetGridPosXZ(	const WorldCollGrid* g,
									const Vec3 pos,
									IVec3 gridPosOut,
									WorldCollCollideResultsErrorFlags* errorFlagsOut)
{
	FOR_BEGIN_STEP(i, 3, 2);
	{
		if(!FINITE(pos[i])){
			if(errorFlagsOut){
				errorFlagsOut->invalidCoord = 1;
			}

			return 0;
		}

		gridPosOut[i] = wcWorldCoordToGridIndex(pos[i], WC_GRID_CELL_SIZE);
	}
	FOR_END;

	// Clamp to legal bounds.

	wcGridClampPosXZ(g, gridPosOut);

	return 1;
}

#define WC_FOREACH_XZ_BEGIN(x, z, gridPosSource, gridPosTarget, dx, dz){	\
			S32 x, z;														\
			S32 dx = gridPosSource[0] > gridPosTarget[0] ? -1 : 1;			\
			S32 dz = gridPosSource[2] > gridPosTarget[2] ? -1 : 1;			\
			gridPosTarget[0] += dx;											\
			gridPosTarget[2] += dz;											\
			for(x = gridPosSource[0]; x != gridPosTarget[0]; x += dx){		\
				for(z = gridPosSource[2]; z != gridPosTarget[2]; z += dz){	\
					FORCED_FOREACH_BEGIN_SEMICOLON
#define WC_FOREACH_XZ_END												\
				}														\
			}															\
			gridPosTarget[0] -= dx;										\
			gridPosTarget[2] -= dz;										\
		}FORCED_FOREACH_END_SEMICOLON

S32 wcRayCollide(	WorldColl* wc,
					const Vec3 source,
					const Vec3 target,
					U32 filterBits,
					WorldCollCollideResults* resultsOut)
{
#if !PSDK_DISABLED
	IVec3 					gridPosSource;
	IVec3 					gridPosTarget;
	CollCacheParams			cacheParams;
	WorldCollCollideResults	resultsTemp;
	WorldCollGrid*			g;

	if(	!wc ||
		!source ||
		!target)
	{
		return 0;
	}

	if(!resultsOut){
		resultsOut = &resultsTemp;
	}

	copyVec3(source, cacheParams.start);
	copyVec3(target, cacheParams.end);
	cacheParams.flags = filterBits;
	cacheParams.radius = 0;

	if(collCacheFind(&cacheParams, resultsOut)){
		return resultsOut->hitSomething;
	}
	
	if(!wcGetThreadGrid(wc, &g)){
		return 0;
	}

	PERFINFO_AUTO_START_FUNC();

	ZeroStruct(resultsOut);

	if(	!wcCollideGetGridPosXZ(g, source, gridPosSource, SAFE_MEMBER_ADDR(resultsOut, errorFlags)) ||
		!wcCollideGetGridPosXZ(g, target, gridPosTarget, SAFE_MEMBER_ADDR(resultsOut, errorFlags)))
	{
		PERFINFO_AUTO_STOP(); // FUNC
		return 0;
	}

	WC_FOREACH_XZ_BEGIN(x, z, gridPosSource, gridPosTarget, dx, dz);
	{
		WorldCollGridCell*	cell;
		PSDKRaycastResults	psdkResults = {0};

		if(!wcCollideGetGetGridCell(g,
									&cell,
									x,
									z,
									"Raycast",
									SAFE_MEMBER_ADDR(resultsOut, errorFlags)))
		{
			continue;
		}

		if(!psdkRaycastClosestShape(WC_GRID_CELL_SCENE_AND_OFFSET(cell),
									source,
									target,
									filterBits,
									0,
									&psdkResults))
		{
			continue;
		}

		if(!wcCollideValidateResults(wc, g, cell, &psdkResults, resultsOut)){
			continue;
		}

		collCacheSet(&cacheParams, resultsOut);

		PERFINFO_AUTO_STOP(); // FUNC
		return 1;
	}
	WC_FOREACH_XZ_END;

	collCacheSet(&cacheParams, resultsOut);

	PERFINFO_AUTO_STOP(); // FUNC
#endif

	return 0;
}

#if !PSDK_DISABLED
typedef struct WCRayCollideMultiResultCBData {
	WorldCollCollideResultsCB	callback;
	void*						userPointer;

	WorldColl*					wc;
	WorldCollGrid*				g;
	const WorldCollGridCell*	cell;
} WCRayCollideMultiResultCBData;

static S32 wcRayCollideMultiResultCB(	WCRayCollideMultiResultCBData* cbData,
										const PSDKRaycastResults* psdkResults)
{
	WorldCollCollideResults results = {0};

	if(wcCollideValidateResults(cbData->wc,
								cbData->g,
								cbData->cell,
								psdkResults,
								&results))
	{
		if(!cbData->callback(	cbData->userPointer,
								&results))
		{
			return 0;
		}
	}

	return 1;
}
#endif

S32 wcRayCollideMultiResult(WorldColl* wc,
							const Vec3 source,
							const Vec3 target,
							U32 filterBits,
							WorldCollCollideResultsCB callback,
							void* userPointer,
							WorldCollCollideResultsErrorFlags* errorFlagsOut)
{
#if !PSDK_DISABLED
	IVec3 			gridPosSource;
	IVec3 			gridPosTarget;
	WorldCollGrid*	g;

	if(	!wcGetThreadGrid(wc, &g) ||
		!source ||
		!target ||
		!callback)
	{
		return 0;
	}

	PERFINFO_AUTO_START_FUNC();

	if(	!wcCollideGetGridPosXZ(g, source, gridPosSource, errorFlagsOut) ||
		!wcCollideGetGridPosXZ(g, target, gridPosTarget, errorFlagsOut))
	{
		PERFINFO_AUTO_STOP(); // FUNC
		return 0;
	}

	// Go through each scene that intersects the ray, and check it.

	WC_FOREACH_XZ_BEGIN(x, z, gridPosSource, gridPosTarget, dx, dz);
	{
		WorldCollGridCell*				cell;
		WCRayCollideMultiResultCBData	cbData;

		if(!wcCollideGetGetGridCell(g,
									&cell,
									x,
									z,
									"MultiRaycast",
									errorFlagsOut))
		{
			continue;
		}

		cbData.callback = callback;
		cbData.userPointer = userPointer;
		cbData.wc = wc;
		cbData.g = g;
		cbData.cell = cell;

		psdkRaycastShapeMultiResult(WC_GRID_CELL_SCENE_AND_OFFSET(cell),
									source,
									target,
									filterBits,
									0,
									wcRayCollideMultiResultCB,
									&cbData);
	}
	WC_FOREACH_XZ_END;

	PERFINFO_AUTO_STOP();
#endif

	return 1;
}

S32 wcQueryTrianglesInYAxisCylinder(WorldColl* wc,
									U32 filterBits,
									const Vec3 source,
									const Vec3 target,
									F32 radius,
									WorldCollQueryTrianglesCB callback,
									void* userPointer)
{
#if PSDK_DISABLED
	return 0;
#else
	WorldCollGridCell*	cell;
	IVec3				gridPosSource;
	WorldCollGrid*		g;
	
	if(!wcGetThreadGrid(wc, &g)){
		return 0;
	}

	if(!wcCollideGetGridPosXZ(g, source, gridPosSource, NULL)){
		return 0;
	}

	if(!wcCollideGetGetGridCell(g,
								&cell,
								vecParamsXZ(gridPosSource),
								"Querying triangles",
								NULL))
	{
		return 0;
	}

	return psdkSceneQueryTrianglesInYAxisCylinder(	WC_GRID_CELL_SCENE_AND_OFFSET(cell),
													filterBits,
													source,
													target,
													radius,
													callback,
													userPointer);
#endif
}

S32 wcQueryTrianglesInCapsule(	WorldColl* wc,
								U32 filterBits,
								const Vec3 source,
								const Vec3 target,
								F32 radius,
								WorldCollQueryTrianglesCB callback,
								void* userPointer)
{
#if PSDK_DISABLED
	return 0;
#else
	WorldCollGridCell*	cell;
	IVec3				gridPosSource;
	WorldCollGrid*		g;
	
	if(!wcGetThreadGrid(wc, &g)){
		return 0;
	}

	if(!wcCollideGetGridPosXZ(g, source, gridPosSource, NULL)){
		return 0;
	}

	if(!wcCollideGetGetGridCell(g,
								&cell,
								vecParamsXZ(gridPosSource),
								"Querying triangles",
								NULL))
	{
		return 0;
	}

	return psdkSceneQueryTrianglesInCapsule(WC_GRID_CELL_SCENE_AND_OFFSET(cell),
											filterBits,
											source,
											target,
											radius,
											callback,
											userPointer);
#endif
}

S32	wcCapsuleCollideHR(WorldColl* wc,
						const Vec3 source,
						const Vec3 target,
						U32 filterBits,
						F32 fHeight,
						F32 fRadius,
						WorldCollCollideResults* resultsOut)
{
	static Capsule cap;
	cap.fLength = fHeight;
	cap.fRadius = fRadius;
	setVec3(cap.vStart, 0, fRadius, 0);
	setVec3(cap.vDir, 0, 1, 0);
	return wcCapsuleCollideEx(wc, cap, source, target, filterBits, resultsOut);
}

S32 wcCapsuleCollide(	WorldColl* wc,
						const Vec3 source,
						const Vec3 target,
						U32 filterBits,
						WorldCollCollideResults* resultsOut)
{
	static const Capsule StandardCapsule = { {0, 1.5f, 0}, {0, 1.0f, 0}, 1.5f, 1.5f};
	return wcCapsuleCollideEx(wc, StandardCapsule, source, target, filterBits, resultsOut);
}

S32	wcCapsuleCollideEx(	WorldColl* wc,
						const Capsule cap,
						const Vec3 source,
						const Vec3 target,
						U32 filterBits,
						WorldCollCollideResults* resultsOut)
{
#if !PSDK_DISABLED
	IVec3 			gridPosSource;
	IVec3			gridPosTarget;
	WorldCollGrid*	g;

	if(!wcGetThreadGrid(wc, &g)){
		return 0;
	}

	PERFINFO_AUTO_START_FUNC();

	ZeroStruct(resultsOut);

	if(	!wcCollideGetGridPosXZ(g, source, gridPosSource, SAFE_MEMBER_ADDR(resultsOut, errorFlags)) ||
		!wcCollideGetGridPosXZ(g, target, gridPosTarget, SAFE_MEMBER_ADDR(resultsOut, errorFlags)))
	{
		PERFINFO_AUTO_STOP(); // FUNC
		return 0;
	}

	WC_FOREACH_XZ_BEGIN(x, z, gridPosSource, gridPosTarget, dx, dz);
	{
		WorldCollGridCell*	cell;
		PSDKRaycastResults	psdkResults = {0};

		if(!wcCollideGetGetGridCell(g,
									&cell,
									x,
									z,
									"Capsule cast",
									SAFE_MEMBER_ADDR(resultsOut, errorFlags)))
		{
			continue;
		}

		if(!psdkCapsulecastClosestShape(WC_GRID_CELL_SCENE_AND_OFFSET(cell),
										cap,
										source,
										target,
										filterBits,
										&psdkResults))
		{
			continue;
		}

		if(!wcCollideValidateResults(wc, g, cell, &psdkResults, resultsOut)){
			continue;
		}

		PERFINFO_AUTO_STOP(); // FUNC

		return 1;
	}
	WC_FOREACH_XZ_END;

	PERFINFO_AUTO_STOP(); // FUNC
#endif

	return 0;
}

S32	wcBoxCollide(	WorldColl* wc,
					const Vec3 minLocalOBB,
					const Vec3 maxLocalOBB,
					const Mat4 matWorldOBB,
					const Vec3 target,
					U32 filterBits,
					WorldCollCollideResults* resultsOut)
{
#if !PSDK_DISABLED
	IVec3 			gridPosSource;
	IVec3 			gridPosTarget;
	F32				radiusOBB;
	WorldCollGrid*	g;

	if(!wcGetThreadGrid(wc, &g)){
		return 0;
	}

	PERFINFO_AUTO_START_FUNC();

	ZeroStruct(resultsOut);

	// Calculate the box radius.

	radiusOBB = 0.f;

	FOR_BEGIN(i, 3);
	{
		F32 a = fabs(minLocalOBB[i]);
		F32 b = fabs(maxLocalOBB[i]);
		F32 maxComp = MAX(a, b);

		radiusOBB += SQR(maxComp);
	}
	FOR_END;

	radiusOBB = sqrt(radiusOBB);

	// Create the integer index of the source and target points.

	FOR_BEGIN_STEP(i, 3, 2);
	{
		F32 sourceComp = matWorldOBB[3][i];
		F32 targetComp = target[i];

		if(	!FINITE(sourceComp) ||
			!FINITE(targetComp))
		{
			if(resultsOut){
				resultsOut->errorFlags.invalidCoord = 1;
			}

			return 0;
		}

		if(sourceComp > targetComp){
			sourceComp += radiusOBB;
			targetComp -= radiusOBB;
		}else{
			sourceComp -= radiusOBB;
			targetComp += radiusOBB;
		}

		gridPosSource[i] = wcWorldCoordToGridIndex(sourceComp, WC_GRID_CELL_SIZE);
		gridPosTarget[i] = wcWorldCoordToGridIndex(targetComp, WC_GRID_CELL_SIZE);
	}
	FOR_END;

	// Clamp to legal bounds.

	wcGridClampPosXZ(g, gridPosSource);
	wcGridClampPosXZ(g, gridPosTarget);

	WC_FOREACH_XZ_BEGIN(x, z, gridPosSource, gridPosTarget, dx, dz);
	{
		WorldCollGridCell*	cell;
		PSDKRaycastResults	psdkResults = {0};

		if(!wcCollideGetGetGridCell(g,
									&cell,
									x,
									z,
									"Box cast",
									SAFE_MEMBER_ADDR(resultsOut, errorFlags)))
		{
			continue;
		}

		if(!psdkOBBCastClosestShape(WC_GRID_CELL_SCENE_AND_OFFSET(cell),
									minLocalOBB,
									maxLocalOBB,
									matWorldOBB,
									target,
									filterBits,
									&psdkResults))
		{
			continue;
		}

		if(!wcCollideValidateResults(wc, g, cell, &psdkResults, resultsOut)){
			continue;
		}

		PERFINFO_AUTO_STOP();

		return 1;
	}
	WC_FOREACH_XZ_END;

	PERFINFO_AUTO_STOP();
#endif

	return 0;
}

S32	wcCapsuleCollideCheck(	WorldColl *wc,
							const Capsule* capsule,
							const Vec3 source,
							U32 filterBits,
							WorldCollCollideResults* resultsOut)
{
#if PSDK_DISABLED
	return 0;
#else
	WorldCollGridCell*	cell;
	IVec3				gridPosSource;
	WorldCollGrid*		g;

	if(!wcGetThreadGrid(wc, &g)){
		return 0;
	}

	PERFINFO_AUTO_START_FUNC();

	ZeroStruct(resultsOut);

	// Create the integer index of the source and target points.

	if(!wcCollideGetGridPosXZ(g, source, gridPosSource, SAFE_MEMBER_ADDR(resultsOut, errorFlags))){
		PERFINFO_AUTO_STOP();
		return 0;
	}

	if(!wcCollideGetGetGridCell(g,
								&cell,
								vecParamsXZ(gridPosSource),
								"Capsule cast",
								SAFE_MEMBER_ADDR(resultsOut, errorFlags)))
	{
		PERFINFO_AUTO_STOP();
		return 0;
	}

	if(!psdkCapsuleCheck(	WC_GRID_CELL_SCENE_AND_OFFSET(cell),
							capsule,
							source,
							filterBits))
	{
		PERFINFO_AUTO_STOP();
		return 0;
	}

	if(resultsOut){
		resultsOut->hitSomething = 1;
	}

	PERFINFO_AUTO_STOP();
	return 1;
#endif
}

static DWORD WINAPI wcThreadMain(WorldColl* wc){
	EXCEPTION_HANDLER_BEGIN

	wcSetThreadIsBG();

	while(1){
		SIMPLE_CPU_DECLARE_TICKS(ticksStart);
		SIMPLE_CPU_DECLARE_TICKS(ticksEnd);

		WaitForEvent(wcgState.threadShared.eventStartFrameToBG, INFINITE);

		if(wcgState.threadShared.threadShouldDestroySelf){
			SetEvent(wcgState.threadShared.eventFrameFinishedToFG);

			while(1){
				// Wait for ThreadManager to kill us.

				SleepEx(INFINITE, TRUE);
			}
		}

		autoTimerThreadFrameBegin(__FUNCTION__);

		SIMPLE_CPU_TICKS(ticksStart);

		wcRunThreadFrame();

		SetEvent(wcgState.threadShared.eventFrameFinishedToFG);

		SIMPLE_CPU_TICKS(ticksEnd);
		SIMPLE_CPU_THREAD_CLOCK(SIMPLE_CPU_USAGE_THREAD_WORLDCOLL_MAIN, ticksStart, ticksEnd);

		autoTimerThreadFrameEnd();
	}
	return 0;
	EXCEPTION_HANDLER_END
}

static void wcGlobalSetThisThreadIsFG(void){
	ATOMIC_INIT_BEGIN;
	{
		assert(!wcgState.fg.threadID);
		wcgState.fg.threadID = GetCurrentThreadId();
	}
	ATOMIC_INIT_END;
}

static void wcGlobalInitialize(void){
	ATOMIC_INIT_BEGIN;
	{
		wcgState.flags.initialized = 1;

		wcGlobalSetThisThreadIsFG();
		
		#if !PSDK_DISABLED
			psdkSimulationOwnershipCreate(&wcgState.psdkSimulationOwnership);
		#endif

		wcgState.threadShared.eventStartFrameToBG = CreateEvent(NULL, FALSE, FALSE, NULL);
		wcgState.threadShared.eventFrameFinishedToFG = CreateEvent(NULL, FALSE, FALSE, NULL);

		wcgState.managedThread = tmCreateThreadEx(wcThreadMain, NULL, 256*1024, -1);
		assert(wcgState.managedThread);

		#if _PS3
			// Raise the priority of the physics thread so that it can start
			//   working sooner and offload tasks to spu.
			
			tmSetThreadRelativePriority(wcgState.managedThread, 1);
		#endif
		
		tmSetThreadProcessorIdx(wcgState.managedThread, THREADINDEX_WORLDCOLL);

		wcgState.eventTimer = etlCreateEventOwner("WorldColl", "WorldColl", "WorldLib");
	}
	ATOMIC_INIT_END;
}

static void wcSceneGeometryInvalidatedCB(	PSDKScene* psdkScene,
											WorldCollScene* scene,
											PSDKActor* psdkActor,
											WorldCollActor* actor)
{
}

S32 wcSceneCreate(	const WorldCollIntegrationMsg* msg,
					WorldCollScene** sceneOut,
					S32 useVariableTimestep,
					F32 gravity,
					const char* name)
{
#if !PSDK_DISABLED
	if(	msg->msgType == WCI_MSG_NOBG_WHILE_SIM_SLEEPS ||
		msg->msgType == WCI_MSG_BG_BETWEEN_SIM)
	{
		WorldCollScene* scene;
		PSDKSceneDesc	psdkSceneDesc = {0};

		scene = callocStruct(WorldCollScene);
		
		scene->wci = msg->wci;
		
		scene->name = strdup(SAFE_DEREF(name) ? name : "no name");

		eaPush(&msg->wci->scenes, scene);
		eaPush(&wcgState.bg.scenesMutable, scene);

		psdkSceneDesc.useVariableTimestep = !!useVariableTimestep;

		psdkSceneDesc.gravity = gravity;

		psdkSceneCreate(&scene->psdkScene,
						wcgState.psdkSimulationOwnership,
						&psdkSceneDesc,
						scene,
						wcSceneGeometryInvalidatedCB);

		*sceneOut = scene;

		return 1;
	}
#endif
	return 0;
}

S32 wcSceneDestroy(	const WorldCollIntegrationMsg* msg,
					WorldCollScene** sceneInOut)
{
	return 0;
}

S32 wcSceneSimulate(const WorldCollIntegrationMsg* msg,
					WorldCollScene* scene)
{
#if !PSDK_DISABLED
	if(	scene &&
		(	msg->msgType == WCI_MSG_NOBG_WHILE_SIM_SLEEPS ||
			msg->msgType == WCI_MSG_BG_BETWEEN_SIM))
	{
		psdkSceneSimulate(	scene->psdkScene,
							wcgState.threadShared.deltaSecondsPerStep);

		psdkSceneFetchResults(scene->psdkScene, 1);
		return 1;
	}
#endif	
	
	return 0;
}

void wcSceneObjectNodeDestructorCB(	void* listOwner,
									AssociationList* al,
									AssociationNode* dyingNode,
									void* userPointer)
{
#if !PSDK_DISABLED
	WorldCollSceneObjectNode*	wcSceneObjectNode = userPointer;
	WorldCollScene*				scene = wcSceneObjectNode->scene;
	WorldCollObject*			wco;

	PERFINFO_AUTO_START_FUNC();
	
	// Remove from the st.

	if(!alNodeGetOwners(dyingNode, NULL, &wco)){
		assert(0);
	}
	
	assert(wco);

	{
		WorldCollSceneObjectNode* nodeToTest;
		if(!stashRemovePointer(scene->wcos.st, wco, &nodeToTest)){
			assert(0);
		}
		assert(nodeToTest == wcSceneObjectNode);
	}

	// Destroy the actor.

	assert(wcSceneObjectNode->alNode == dyingNode);

	wcPSDKActorTrackDestroy(wcSceneObjectNode->alNode,
							WC_PSDK_ACTOR_OWNER_SCENE_STATIC,
							NULL,
							wcSceneObjectNode->psdkActor);

	psdkActorDestroy(	&wcSceneObjectNode->psdkActor,
						wcgState.psdkSimulationOwnership);

	// Remove from the inactive list.

	if(wcSceneObjectNode->next){
		wcSceneObjectNode->next->prev = wcSceneObjectNode->prev;
	}

	if(wcSceneObjectNode->prev){
		wcSceneObjectNode->prev->next = wcSceneObjectNode->next;
		wcSceneObjectNode->prev = NULL;
	}else{
		assert(wcSceneObjectNode == scene->wcos.nodes[wcSceneObjectNode->listIndex]);
		scene->wcos.nodes[wcSceneObjectNode->listIndex] = wcSceneObjectNode->next;
	}

	// Done.

	SAFE_FREE(wcSceneObjectNode);

	PERFINFO_AUTO_STOP();
#endif
}

#if !PSDK_DISABLED
typedef struct WorldCollSceneQueryShapesCallbackData {
	WorldCollScene*		scene;
	WorldColl*			wc;
} WorldCollSceneQueryShapesCallbackData;

static void wcSceneQueryShapesCB(const PSDKQueryShapesCBData* psdkCBData){
	WorldCollSceneQueryShapesCallbackData*	cbData = psdkCBData->input.userPointer;
	WorldCollScene*							scene = cbData->scene;
	WorldColl*								wc = cbData->wc;

	if(!scene->wcos.st){
		scene->wcos.st = stashTableCreateAddress(100);
	}

	if(!scene->wcos.alPrimaryWCOs){
		wcgState.alTypeSceneWCOs.flags.isPrimary = 1;
		wcgState.alTypeSceneWCOs.userPointerDestructor = wcSceneObjectNodeDestructorCB;

		alCreate(	&scene->wcos.alPrimaryWCOs,
					scene,
					&wcgState.alTypeSceneWCOs);
	}

	FOR_BEGIN(i, (S32)psdkCBData->shapeCount);
	{
		void*						shape = psdkCBData->shapes[i];
		PSDKActor*					psdkActor;
		AssociationNode*			node;
		WorldCollGridCell*			cell;
		WorldCollObject*			wco;
		WorldCollSceneObjectNode*	wcSceneObjectNode;

		if(	!psdkShapeGetActor(shape, &psdkActor) ||
			!psdkActorGetUserPointer(&node, psdkActor) ||
			!alNodeGetOwners(node, &cell, &wco) ||
			cell->wc != wc)
		{
			continue;
		}

		if(stashFindPointer(scene->wcos.st, wco, &wcSceneObjectNode)){
			assert(wcSceneObjectNode->scene == scene);

			// Move to the active list.

			if(wcSceneObjectNode->listIndex != scene->wcos.activeNodeListIndex){
				// Remove from the inactive list.

				if(wcSceneObjectNode->next){
					wcSceneObjectNode->next->prev = wcSceneObjectNode->prev;
				}

				if(wcSceneObjectNode->prev){
					wcSceneObjectNode->prev->next = wcSceneObjectNode->next;
					wcSceneObjectNode->prev = NULL;
				}else{
					assert(wcSceneObjectNode == scene->wcos.nodes[wcSceneObjectNode->listIndex]);
					scene->wcos.nodes[wcSceneObjectNode->listIndex] = wcSceneObjectNode->next;
				}

				// Add to active list.

				wcSceneObjectNode->listIndex = scene->wcos.activeNodeListIndex;

				wcSceneObjectNode->next = scene->wcos.nodes[wcSceneObjectNode->listIndex];
				if(wcSceneObjectNode->next){
					wcSceneObjectNode->next->prev = wcSceneObjectNode;
				}

				scene->wcos.nodes[wcSceneObjectNode->listIndex] = wcSceneObjectNode;
			}
		}else{
			wcSceneObjectNode = callocStruct(WorldCollSceneObjectNode);

			wcSceneObjectNode->scene = scene;

			if(!stashAddPointer(scene->wcos.st, wco, wcSceneObjectNode, 0)){
				assert(0);
			}

			alAssociate(&wcSceneObjectNode->alNode,
						scene->wcos.alPrimaryWCOs,
						wco->alSecondaryScenes,
						wcSceneObjectNode);

			psdkActorCopy(	scene->psdkScene,
							&wcSceneObjectNode->psdkActor,
							wcSceneObjectNode,
							psdkActor,
							cell->placement->flags.hasOffset ?
								cell->placement->sceneOffset :
								NULL);
			
			wcPSDKActorTrackCreate(	wcSceneObjectNode->alNode,
									WC_PSDK_ACTOR_OWNER_SCENE_STATIC,
									NULL,
									wcSceneObjectNode->psdkActor);

			// Add to active list.

			wcSceneObjectNode->listIndex = scene->wcos.activeNodeListIndex;

			wcSceneObjectNode->next = scene->wcos.nodes[wcSceneObjectNode->listIndex];
			if(wcSceneObjectNode->next){
				wcSceneObjectNode->next->prev = wcSceneObjectNode;
			}

			scene->wcos.nodes[wcSceneObjectNode->listIndex] = wcSceneObjectNode;
		}
	}
	FOR_END;
}
#endif

S32 wcSceneUpdateWorldCollObjectsBegin(	const WorldCollIntegrationMsg* msg,
										WorldCollScene* scene)
{
	if(	msg->msgType != WCI_MSG_NOBG_WHILE_SIM_SLEEPS &&
		msg->msgType != WCI_MSG_BG_BETWEEN_SIM
		||
		!scene
		||
		scene->flags.isUpdatingWorldCollObjects)
	{
		return 0;
	}

	scene->wcos.activeNodeListIndex = !scene->wcos.activeNodeListIndex;
	scene->flags.isUpdatingWorldCollObjects = 1;

	return 1;
}

S32 wcSceneUpdateWorldCollObjectsEnd(	const WorldCollIntegrationMsg* msg,
										WorldCollScene* scene)
{
	if(	msg->msgType != WCI_MSG_NOBG_WHILE_SIM_SLEEPS &&
		msg->msgType != WCI_MSG_BG_BETWEEN_SIM
		||
		!scene
		||
		!scene->flags.isUpdatingWorldCollObjects)
	{
		return 0;
	}

	PERFINFO_AUTO_START("DestroyActors", 1);
	{
		WorldCollSceneObjectNode* n;

		for(n = scene->wcos.nodes[!scene->wcos.activeNodeListIndex]; n;){
			WorldCollSceneObjectNode* next = n->next;

			alNodeRemove(n->alNode);

			n = next;
		}

		scene->wcos.nodes[!scene->wcos.activeNodeListIndex] = NULL;
	}
	PERFINFO_AUTO_STOP();
	
	scene->flags.isUpdatingWorldCollObjects = 0;
	
	return 1;
}

S32	wcSceneGatherWorldCollObjects(	const WorldCollIntegrationMsg* msg,
									WorldCollScene* scene,
									WorldColl* wc,
									const Vec3 aabbMin,
									const Vec3 aabbMax)
{
#if !PSDK_DISABLED
	WorldCollScenePlacement*	spChecked[10];
	S32							spCheckedCount = 0;
	WorldCollGrid*				g;
	IVec3						lo;
	IVec3						hi;

	if(	msg->msgType != WCI_MSG_NOBG_WHILE_SIM_SLEEPS &&
		msg->msgType != WCI_MSG_BG_BETWEEN_SIM
		||
		!SAFE_MEMBER(scene, flags.isUpdatingWorldCollObjects)
		||
		!wc
		||
		!aabbMin
		||
		!aabbMax)
	{
		return 0;
	}

	PERFINFO_AUTO_START_FUNC();

	if(msg->msgType == WCI_MSG_BG_BETWEEN_SIM){
		g = &wc->bg.grid;
	}else{
		g = &wc->fg.grid;
	}

	ARRAY_FOREACH_BEGIN(lo, i);
	{
		lo[i] = wcWorldCoordToGridIndex(aabbMin[i], WC_GRID_CELL_SIZE);
		MAX1(lo[i], g->lo[i]);
		hi[i] = wcWorldCoordToGridIndex(aabbMax[i], WC_GRID_CELL_SIZE) + 1;
		MIN1(hi[i], g->hi[i]);
	}
	ARRAY_FOREACH_END;

	FOR_BEGIN_FROM(x, lo[0], hi[0]);
	{
		FOR_BEGIN_FROM(z, lo[2], hi[2]);
		{
			WorldCollGridCell*						cell;
			PSDKQueryShapesInAABBParams				params;
			S32										alreadyChecked = 0;
			WorldCollSceneQueryShapesCallbackData	cbData = {0};

			if(!wcCollideGetGetGridCell(g, &cell, x, z, NULL, NULL)){
				continue;
			}

			FOR_BEGIN(i, spCheckedCount);
			{
				if(spChecked[i] == cell->placement){
					alreadyChecked = 1;
					break;
				}
			}
			FOR_END;
					
			if(alreadyChecked){
				continue;
			}

			if(spCheckedCount < ARRAY_SIZE(spChecked)){
				spChecked[spCheckedCount++] = cell->placement;
			}

			params.filterBits = WC_QUERY_BITS_WORLD_ALL;
			copyVec3(aabbMin, params.aabbMin);
			copyVec3(aabbMax, params.aabbMax);

			params.callback = wcSceneQueryShapesCB;
			cbData.scene = scene;
			cbData.wc = wc;
			params.userPointer = &cbData;

			psdkSceneQueryShapesInAABB(	WC_GRID_CELL_SCENE_AND_OFFSET(cell),
										&params);
		}
		FOR_END;
	}
	FOR_END;

	PERFINFO_AUTO_STOP();//FUNC
#endif

	return 1;
}

S32	wcSceneGatherWorldCollObjectsByRadius(	const WorldCollIntegrationMsg* msg,
											WorldCollScene* scene,
											WorldColl* wc,
											const Vec3 pos,
											F32 radius)
{
	Vec3 vecRadius = {radius, radius, radius};
	Vec3 aabbMin;
	Vec3 aabbMax;
	
	subVec3(pos, vecRadius, aabbMin);
	addVec3(pos, vecRadius, aabbMax);
	
	return wcSceneGatherWorldCollObjects(msg, scene, wc, aabbMin, aabbMax);
}

S32	wcSceneGetPSDKScene( WorldCollScene* scene, PSDKScene** psdkSceneOut)
{
	if (!scene || !scene->psdkScene)
		return 0;
	*psdkSceneOut = scene->psdkScene;
	return 1;
}

#if WORLDCOLL_VERIFY_ACTORS
typedef struct WorldCollActorDebugInfo {
	void**	owners;
	U32*	ownerTypes;
} WorldCollActorDebugInfo;

static S32 wcPSDKActorTrackCommon(	void** ownerInOut,
									AssociationListIterator* iter,
									PSDKActor* psdkActor)
{
	if(!psdkActor){
		return 0;
	}
	
	if(	!*ownerInOut &&
		iter)
	{
		alItGetNode(iter, (AssociationNode**)ownerInOut);
	}
	
	assert(*ownerInOut);
	
	return 1;
}

void wcPSDKActorTrackCreate(void* owner,
							WorldCollPSDKActorOwnerType ownerType,
							AssociationListIterator* iter,
							PSDKActor* psdkActor)
{
	WorldCollActorDebugInfo* info;
	
	if(!wcPSDKActorTrackCommon(&owner, iter, psdkActor)){
		return;
	}

	if(!wcgState.stActors){
		wcgState.stActors = stashTableCreateAddress(1000);
	}
	
	if(!wcgState.stOwnerToActor){
		wcgState.stOwnerToActor = stashTableCreateAddress(1000);
	}
	
	info = callocStruct(WorldCollActorDebugInfo);
	
	if(!stashAddPointer(wcgState.stActors, psdkActor, info, 0)){
		assert(0);
	}
	
	eaPush(&info->owners, owner);
	eaiPush(&info->ownerTypes, ownerType);
	
	if(!stashAddPointer(wcgState.stOwnerToActor, owner, psdkActor, 0)){
		assert(0);
	}
}

void wcPSDKActorTrackIncrement(	void* owner,
								WorldCollPSDKActorOwnerType ownerType,
								AssociationListIterator* iter,
								PSDKActor* psdkActor)
{
	WorldCollActorDebugInfo* info;
	
	if(!wcPSDKActorTrackCommon(&owner, iter, psdkActor)){
		return;
	}

	if(!stashFindPointer(wcgState.stActors, psdkActor, &info)){
		assert(0);
	}
	
	assert(eaSize(&info->owners));
	assert(eaSize(&info->owners) == eaiSize(&info->ownerTypes));

	assert(eaFind(&info->owners, owner) < 0);
	
	eaPush(&info->owners, owner);
	eaiPush(&info->ownerTypes, ownerType);
	
	if(!stashAddPointer(wcgState.stOwnerToActor, owner, psdkActor, 0)){
		assert(0);
	}
}

void wcPSDKActorTrackDecrement(	void* owner,
								WorldCollPSDKActorOwnerType ownerType,
								AssociationListIterator* iter,
								PSDKActor* psdkActor)
{
	WorldCollActorDebugInfo* info;
	
	if(!wcPSDKActorTrackCommon(&owner, iter, psdkActor)){
		return;
	}

	if(!stashFindPointer(wcgState.stActors, psdkActor, &info)){
		assert(0);
	}
	
	assert(eaSize(&info->owners));

	{
		S32 index = eaFindAndRemove(&info->owners, owner);
		
		assert(index >= 0);
		assert(index < eaiSize(&info->ownerTypes));
		assert(info->ownerTypes[index] == ownerType);
		assert(eaiSize(&info->ownerTypes) == eaSize(&info->owners) + 1);
		assert(eaSize(&info->owners));
		
		eaiRemove(&info->ownerTypes, index);

		{
			PSDKActor* psdkActorVerify;
			
			if(!stashRemovePointer(wcgState.stOwnerToActor, owner, &psdkActorVerify)){
				assert(0);
			}
			
			assert(psdkActorVerify == psdkActor);
		}
	}
}

void wcPSDKActorTrackDestroy(	void* owner,
								U32 ownerType,
								AssociationListIterator* iter,
								PSDKActor* psdkActor)
{
	WorldCollActorDebugInfo* info;
	
	if(!wcPSDKActorTrackCommon(&owner, iter, psdkActor)){
		return;
	}

	if(!stashFindPointer(wcgState.stActors, psdkActor, &info)){
		assert(0);
	}
	
	assert(eaSize(&info->owners));

	{
		S32 index = eaFindAndRemove(&info->owners, owner);
		
		assert(!index);
		assert(eaiSize(&info->ownerTypes) == 1);
		assert(info->ownerTypes[index] == ownerType);
		assert(eaiSize(&info->ownerTypes) == eaSize(&info->owners) + 1);
		assert(!eaSize(&info->owners));
		
		eaiRemove(&info->ownerTypes, index);

		if(!stashRemovePointer(wcgState.stActors, psdkActor, NULL)){
			assert(0);
		}
		
		eaDestroy(&info->owners);
		eaiDestroy(&info->ownerTypes);
		SAFE_FREE(info);

		{
			PSDKActor* psdkActorVerify;
			
			if(!stashRemovePointer(wcgState.stOwnerToActor, owner, &psdkActorVerify)){
				assert(0);
			}
			
			assert(psdkActorVerify == psdkActor);
		}
	}
}

void wcPSDKActorTrackPrintOwners(PSDKActor* psdkActor){
	WorldCollActorDebugInfo* info;

	printf("Owners of PSDKActor 0x%p begin.\n", psdkActor);
	
	if(stashFindPointer(wcgState.stActors, psdkActor, &info)){
		assert(eaSize(&info->owners) == eaiSize(&info->ownerTypes));
		
		EARRAY_CONST_FOREACH_BEGIN(info->owners, i, isize);
		{
			printf("   %d: 0x%p\n", info->ownerTypes[i], info->owners[i]);
		}
		EARRAY_FOREACH_END;
	}

	printf("Owners of PSDKActor 0x%p end.\n", psdkActor);
}
#endif // WORLDCOLL_VERIFY_ACTORS

#if PSDK_DISABLED
	#define CHECK_ACTOR_MSG(msg)			return 0;((void)0)
	#define CHECK_ACTOR_AND_MSG(actor, msg)	return 0;((void)0)
#else
	#define CHECK_ACTOR_MSG(msg)	if(	(msg)->msgType != WCI_MSG_NOBG_WHILE_SIM_SLEEPS &&\
										(msg)->msgType != WCI_MSG_BG_BETWEEN_SIM)\
									{return 0;}((void)0)

	#define CHECK_ACTOR_AND_MSG(actor, msg)	if(	!(actor) ||\
												(msg)->msgType != WCI_MSG_NOBG_WHILE_SIM_SLEEPS &&\
												(msg)->msgType != WCI_MSG_BG_BETWEEN_SIM)\
											{return 0;}((void)0)
#endif

S32 wcActorCreate(	const WorldCollIntegrationMsg* msg,
					WorldCollScene* scene,
					WorldCollActor** wcActorOut,
					void* userPointer,
					PSDKActorDesc* actorDesc,
					PSDKBodyDesc* bodyDesc,
					S32 isKinematic,
					S32 hasContactEvent,
					S32 disableGravity)
{
#if PSDK_DISABLED
	return 0;
#else
	WorldCollActor* actor;

	CHECK_ACTOR_MSG(msg);

	if(!scene){
		return 0;
	}

	actor = callocStruct(WorldCollActor);

	actor->userPointer = userPointer;
	actor->scene = scene;

	psdkActorCreate(&actor->psdkActor,
					wcgState.psdkSimulationOwnership,
					actorDesc,
					bodyDesc,
					scene->psdkScene,
					actor,
					isKinematic,
					hasContactEvent,
					false,
					disableGravity);

	wcPSDKActorTrackCreate(actor, WC_PSDK_ACTOR_OWNER_SCENE_DYNAMIC, NULL, actor->psdkActor);

	eaPush(&scene->actors, actor);

	*wcActorOut = actor;
	
	return 1;
#endif
}

S32 wcActorDestroy(	const WorldCollIntegrationMsg* msg,
					WorldCollActor** actorInOut)
{
#if PSDK_DISABLED
	return 0;
#else
	WorldCollActor* actor = SAFE_DEREF(actorInOut);

	CHECK_ACTOR_AND_MSG(actor, msg);

	if(eaFindAndRemove(&actor->scene->actors, actor) < 0){
		assert(0);
	}

	wcPSDKActorTrackDestroy(actor, WC_PSDK_ACTOR_OWNER_SCENE_DYNAMIC, NULL, actor->psdkActor);

	psdkActorDestroy(	&actor->psdkActor,
 						wcgState.psdkSimulationOwnership);

	SAFE_FREE(actor);

	return 1;
#endif
}

S32 wcActorGetUserPointer(	WorldCollActor* actor,
							void** userPointerOut)
{
	if(	!userPointerOut ||
		!actor)
	{
		return 0;
	}

	*userPointerOut = actor->userPointer;
	return 1;
}

S32 wcActorGetPSDKActor(const WorldCollIntegrationMsg* msg,
						WorldCollActor* actor,
						PSDKActor** psdkActorOut)
{
	CHECK_ACTOR_AND_MSG(actor, msg);

	#if !PSDK_DISABLED
	if(	!actor->psdkActor ||
		!psdkActorOut)
	{
		return 0;
	}

	*psdkActorOut = actor->psdkActor;

	return 1;
	#endif
}

S32 wcActorSetMat(	const WorldCollIntegrationMsg* msg,
					WorldCollActor* actor,
					const Mat4 mat)
{
	CHECK_ACTOR_AND_MSG(actor, msg);

	#if !PSDK_DISABLED
	if(!mat){
		return 0;
	}

	return psdkActorSetMat(actor->psdkActor, mat);
	#endif
}

S32 wcActorGetMat(	const WorldCollIntegrationMsg* msg,
					WorldCollActor* actor,
					Mat4 matOut)
{
	CHECK_ACTOR_AND_MSG(actor, msg);
	#if !PSDK_DISABLED
	if(!matOut){
		return 0;
	}

	return psdkActorGetMat(actor->psdkActor, matOut);
	#endif
}


S32 wcActorGetVels(	const WorldCollIntegrationMsg* msg,
					WorldCollActor* actor,
					Vec3 velOut,
					Vec3 angVelOut)
{
	CHECK_ACTOR_AND_MSG(actor, msg);

	#if !PSDK_DISABLED
	return psdkActorGetVels(actor->psdkActor, velOut, angVelOut);
	#endif
}

S32 wcActorMove(const WorldCollIntegrationMsg* msg,
				WorldCollActor* actor,
				const Vec3 pos)
{
	CHECK_ACTOR_AND_MSG(actor, msg);
	#if !PSDK_DISABLED
	return psdkActorMove(actor->psdkActor, pos);
	#endif
}

S32 wcActorSetCollidable(	const WorldCollIntegrationMsg* msg,
							WorldCollActor* actor,
							S32 collidable)
{
	CHECK_ACTOR_AND_MSG(actor, msg);
	#if !PSDK_DISABLED
	return psdkActorSetCollidable(actor->psdkActor, collidable);
	#endif
}

S32 wcActorRotate(	const WorldCollIntegrationMsg* msg,
					WorldCollActor* actor,
					const Quat rot)
{
	CHECK_ACTOR_AND_MSG(actor, msg);
	#if !PSDK_DISABLED
	return psdkActorRotate(actor->psdkActor, rot);
	#endif
}

S32 wcActorSetVels(	const WorldCollIntegrationMsg* msg,
					WorldCollActor* actor,
					const Vec3 vel,
					const Vec3 angVel)
{
	CHECK_ACTOR_AND_MSG(actor, msg);
	#if !PSDK_DISABLED
	return psdkActorSetVels(actor->psdkActor, vel, angVel);
	#endif
}

S32 wcActorAddVel(	const WorldCollIntegrationMsg* msg,
					WorldCollActor* actor,
					const Vec3 vel)
{
	CHECK_ACTOR_AND_MSG(actor, msg);
	#if !PSDK_DISABLED
	return psdkActorAddVel(actor->psdkActor, vel);
	#endif
}

S32 wcActorAddForce(const WorldCollIntegrationMsg* msg,
					WorldCollActor* actor,
					const Vec3 force,
					S32 isAcceleration,
					S32 shouldWakeup)
{
	CHECK_ACTOR_AND_MSG(actor, msg);
	#if !PSDK_DISABLED
	return psdkActorAddForce(	actor->psdkActor,
								force,
								isAcceleration,
								shouldWakeup);
	#endif
}

S32 wcActorAddAngVel(	const WorldCollIntegrationMsg* msg,
						WorldCollActor* actor,
						const Vec3 vel)
{
	CHECK_ACTOR_AND_MSG(actor, msg);
	#if !PSDK_DISABLED
	return psdkActorAddAngVel(actor->psdkActor, vel);
	#endif
}

S32 wcActorCreateAndAddCCDSkeleton(	const WorldCollIntegrationMsg* msg,
									WorldCollActor* actor,
									const Vec3 center)
{
	CHECK_ACTOR_AND_MSG(actor, msg);
	#if !PSDK_DISABLED
	return psdkActorCreateAndAddCCDSkeleton(actor->psdkActor, center);
	#endif
}

S32 wcActorWakeUp(	const WorldCollIntegrationMsg* msg,
					WorldCollActor* actor)
{
	CHECK_ACTOR_AND_MSG(actor, msg);
	#if !PSDK_DISABLED
	return psdkActorWakeUp(actor->psdkActor);
	#endif
}

static void wcIntegrationSendMsgWorldCollExists(WorldCollIntegration* wci,
												WorldColl* wc)
{
	WorldCollIntegrationMsg	msg = {0};

	msg.msgType = WCI_MSG_FG_WORLDCOLL_EXISTS;

	msg.wci = wci;
	msg.userPointer = wci->userPointer;

	msg.fg.worldCollExists.wc = wc;

	wci->msgHandler(&msg);
}

static void wcIntegrationSendMsgWorldCollDestroyed(	WorldCollIntegration* wci,
													WorldColl* wc)
{
	WorldCollIntegrationMsg	msg = {0};

	msg.msgType = WCI_MSG_NOBG_WORLDCOLL_DESTROYED;

	msg.wci = wci;
	msg.userPointer = wci->userPointer;

	msg.nobg.worldCollDestroyed.wc = wc;

	wci->msgHandler(&msg);
}

S32 wcCreate(WorldColl** wcOut){
	WorldColl* wc;

	if(!wcOut){
		return 0;
	}
	
	wcGlobalInitialize();
	
	if(!wcThreadIsFG() 
#if !PSDK_DISABLED
		|| !wcgState.psdkSimulationOwnership
#endif
	  )
	{
		return 0;
	}

	assert(!*wcOut);

	*wcOut = wc = callocStruct(WorldColl);

	eaPush(&wcgState.wcsMutable, wc);

	EARRAY_CONST_FOREACH_BEGIN(wcgState.fg.wcis, i, isize);
	{
		wcIntegrationSendMsgWorldCollExists(wcgState.fg.wcis[i], wc);
	}
	EARRAY_FOREACH_END;

	return 1;
}

void wcDestroy(WorldColl** wcInOut){
	WorldColl* wc = SAFE_DEREF(wcInOut);

	if(wc){
		*wcInOut = NULL;

		// Disable destroy/create msgs.
		
		ASSERT_FALSE_AND_SET(wcgState.flags.actorChangeMsgsDisabled);
		
		// Wait for the simulation thread to stop so BG data can be destroyed.

		coarseTimerAddInstance(NULL, "wcWaitForSimulationToEndFG");
		wcWaitForSimulationToEndFG(1, NULL, 0);
		coarseTimerStopInstance(NULL, "wcWaitForSimulationToEndFG");

		// Reset the collision cache (this should be in a wci msg handler).

		coarseTimerAddInstance(NULL, "collCacheReset");
		collCacheReset();
		coarseTimerStopInstance(NULL, "collCacheReset");

		// Destroy all the grid cells.

		coarseTimerAddInstance(NULL, "wcDestroyAllGridCells");
		wcDestroyAllGridCells(wc);
		coarseTimerStopInstance(NULL, "wcDestroyAllGridCells");
		
		// Notify integrations.

		coarseTimerAddInstance(NULL, "wcIntegrationSendMsgWorldCollDestroyed");
		EARRAY_CONST_FOREACH_BEGIN(wcgState.fg.wcis, i, isize);
		{
			wcIntegrationSendMsgWorldCollDestroyed(wcgState.fg.wcis[i], wc);
		}
		EARRAY_FOREACH_END;
		coarseTimerStopInstance(NULL, "wcIntegrationSendMsgWorldCollDestroyed");

		if(eaFindAndRemove(&wcgState.wcsMutable, wc) < 0){
			assert(0);
		}

		// Done.

		SAFE_FREE(wc);

		// Re-enable 

		ASSERT_TRUE_AND_RESET(wcgState.flags.actorChangeMsgsDisabled);
	}
}


static S32 wcForceCreateSceneForGridPosCB(	WorldColl* wc,
											S32 x,
											S32 z,
											void* userPointer)
{
	WorldCollGridCell *cell;

	if(wcGetCellByGridPosFG(wc, &cell, x, z)){
		wcCellHasScene(cell, "Force create");
	}

	return 1;	
}

void wcCreateAllScenes(WorldColl* wc){
	PERFINFO_AUTO_START_FUNC();

	wcForEachGridPosFG(wc, wcForceCreateSceneForGridPosCB, NULL);
	wcForceSimulationUpdate();
	
	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND ACMD_NAME(wcCreateAllScenes);
void wcCmdCreateAllScenes(void){
	EARRAY_CONST_FOREACH_BEGIN(wcgState.wcs, i, isize);
	{
		WorldColl* wc = wcgState.wcs[i];
		
		wcCreateAllScenes(wc);
	}
	EARRAY_FOREACH_END;
}

void wcIntegrationCreate(	WorldCollIntegration** wciOut,
							WorldCollIntegrationMsgHandler msgHandler,
							void* userPointer,
							const char* name)
{
	WorldCollIntegration* wci;

	wcGlobalSetThisThreadIsFG();

	if(	!wciOut ||
		!msgHandler ||
		!wcThreadIsFG())
	{
		return;
	}

	wci = callocStruct(WorldCollIntegration);

	wci->msgHandler = msgHandler;
	wci->name = strdup(name);
	wci->userPointer = userPointer;

	eaPush(&wcgState.fg.wcisMutable, wci);
	wcgState.fg.flags.wciListUpdatedToBG = 1;

	EARRAY_CONST_FOREACH_BEGIN(wcgState.wcs, i, isize);
	{
		wcIntegrationSendMsgWorldCollExists(wci, wcgState.wcs[i]);
	}
	EARRAY_FOREACH_END;

	*wciOut = wci;
}

typedef struct WorldCollIsValidCallbackData {
	const WorldCollObject*	wco;
	S32						isValid;
} WorldCollIsValidCallbackData;

static S32 wcIsValidObjectForGridPosCB(	WorldColl* wc,
										S32 x,
										S32 z,
										WorldCollIsValidCallbackData* data)
{
	WorldCollGridCell* cell;
	
	if(wcGetCellByGridPosFG(wc, &cell, x, z)){
		AssociationListIterator*	iter;
		WorldCollObject*			wco;

		for(alItCreate(&iter, cell->alPrimaryStaticWCOs);
			alItGetOwner(iter, &wco);
			alItGotoNextThenDestroy(&iter))
		{
			if(wco == data->wco){
				alItDestroy(&iter);
				data->isValid = 1;
				return 0;
			}
		}
	}
	
	return 1;
}

static void wcIsValidObjectCB(	WorldColl* wc,
								WorldCollIsValidCallbackData* data)
{
	wcForEachGridPosFG(wc, wcIsValidObjectForGridPosCB, data);
}

S32 wcIsValidObject(const WorldCollObject* wco){
	WorldCollIsValidCallbackData data = {0};

	if(	!wco ||
		!wcThreadIsFG())
	{
		return 0;
	}

	data.wco = wco;

	wcForEachWorldColl(wcIsValidObjectCB, &data);

	return data.isValid;
}

S32	wcTraverseObjects(	WorldColl* wc,
						WorldCollObjectTraverseCB callback,
						void* userPointer,
						const Vec3 min_xyz,
						const Vec3 max_xyz,
						U32 unique,
						U32 traverse_types)
{
	static StashTable	wcoStash;
	IVec3				lo;
	IVec3				hi;
	S32					first = 1;

	if(	!wc ||
		!callback ||
		!wcThreadIsFG() ||
		0 == (traverse_types & (WCO_TRAVERSE_STATIC|WCO_TRAVERSE_DYNAMIC)))
	{
		return 0;
	}

	if(	!wcoStash &&
		unique)
	{
		wcoStash = stashTableCreateAddress(30);
	}

	if(unique){
		stashTableClear(wcoStash);
	}

	if(	min_xyz &&
		max_xyz)
	{
		FOR_BEGIN(i, 3);
		{
			lo[i] = floor(min_xyz[i] / WC_GRID_CELL_SIZE);
			MAX1(lo[i], wc->fg.grid.lo[i]);
			hi[i] = floor(max_xyz[i] / WC_GRID_CELL_SIZE) + 1;
			MIN1(hi[i], wc->fg.grid.hi[i]);
		}
		FOR_END;
	}else{
		copyVec3(wc->fg.grid.lo, lo);
		copyVec3(wc->fg.grid.hi, hi);
	}

	FOR_BEGIN_FROM(x, lo[0], hi[0]);
	{
		FOR_BEGIN_FROM(z, lo[2], hi[2]);
		{
			WorldCollGridCell* cell;

			if(wcGetCellByGridPosFG(wc, &cell, x, z)){
				AssociationListIterator*	iter;
				WorldCollObject*			wco;

				if(traverse_types & WCO_TRAVERSE_STATIC)
					for(alItCreate(&iter, cell->alPrimaryStaticWCOs);
						alItGetOwner(iter, &wco);
						alItGotoNextThenDestroy(&iter))
					{
						WorldCollObjectTraverseParams params = {0};

						params.wco = wco;
						params.first = first;
						params.traverse_type = WCO_TRAVERSE_STATIC;

						if(	!unique ||
							stashAddressAddInt(wcoStash, wco, 1, 0))
						{
							callback(userPointer, &params);
						}

						first = 0;
					}

				if(traverse_types & WCO_TRAVERSE_DYNAMIC)
					for(alItCreate(&iter, cell->alPrimaryDynamicWCOs);
						alItGetOwner(iter, &wco);
						alItGotoNextThenDestroy(&iter))
					{
						WorldCollObjectTraverseParams params = {0};

						params.wco = wco;
						params.first = first;
						params.traverse_type = WCO_TRAVERSE_DYNAMIC;

						if(	!unique ||
							stashAddressAddInt(wcoStash, wco, 1, 0))
						{
							callback(userPointer, &params);
						}

						first = 0;
					}
			}
		}
		FOR_END;
	}
	FOR_END;

	return 1;
}

AUTO_COMMAND ACMD_NAME("wcThreaded", "wcThread") ACMD_COMMANDLINE;
void wcSetThreaded(S32 enabled){
	if(!wcgState.flags.initialized){
		wcgState.flags.notThreaded = !enabled;
	}
}

S32 wcIsThreaded(void){
	return !wcgState.flags.notThreaded;
}


AUTO_COMMAND ACMD_SERVERONLY;
void wcConnectToPSDKDebuggerServer(	char *host,
									S32 port)
{
	wcgState.fg.flags.connectToDebugger = 1;
}

AUTO_COMMAND ACMD_CLIENTONLY;
void wcConnectToPSDKDebuggerClient(	char *host,
									S32 port)
{
	wcgState.fg.flags.connectToDebugger = 1;
}

AUTO_COMMAND;
void wcRecreateAllStaticScenes(void){
	wcgState.fg.flags.ssRecreateAll = 1;
}

AUTO_COMMAND;
void wcSceneSetMaxActors(U32 value){
#if !PSDK_DISABLED
	value = MIN(value, PSDK_MAX_ACTORS_PER_SCENE);

	if(wcgState.fg.sceneConfig.maxActors != value){
		wcgState.fg.sceneConfig.maxActors = value;
		wcRecreateAllStaticScenes();
	}
#endif
}

AUTO_COMMAND;
void wcSceneSetPruningType(	U32 staticPruningType,
							U32 dynamicPruningType)
{
	if(	wcgState.fg.sceneConfig.staticPruningType != staticPruningType ||
		wcgState.fg.sceneConfig.dynamicPruningType != dynamicPruningType)
	{
		wcgState.fg.sceneConfig.staticPruningType = staticPruningType;
		wcgState.fg.sceneConfig.dynamicPruningType = dynamicPruningType;
		wcRecreateAllStaticScenes();
	}
}

AUTO_COMMAND;
void wcSceneSetSubdivisionLevel(U32 level){
	level = MINMAX(level, 1, 8);

	if(wcgState.fg.sceneConfig.subdivisionLevel != level){
		wcgState.fg.sceneConfig.subdivisionLevel = level;
		wcRecreateAllStaticScenes();
	}
}

AUTO_COMMAND;
void wcSceneSetMaxBoundXYZ(U32 bound){
	if(wcgState.fg.sceneConfig.maxBoundXYZ != bound){
		wcgState.fg.sceneConfig.maxBoundXYZ = bound;
		wcRecreateAllStaticScenes();
	}
}

AUTO_RUN;
void wcAutoRun(void){
	wcgState.fg.sceneConfig.maxActors = WC_DEFAULT_MAX_ACTORS_PER_SCENE;
}

static char getCharForIndex(U32 index){
	if(index < 10){
		return '0' + index;
	}

	if(index - 10 < 26){
		return 'A' + index - 10;
	}

	if(index - 10 - 26 < 26){
		return 'a' + index - 10 - 26;
	}

	return '?';
}

AUTO_COMMAND;
void wcPrintDebugInfo(void){
#if !PSDK_DISABLED
	printf("WorldColl Debug Info Begin -------------------------------------------------\n");

	printfColor(COLOR_GREEN, "X = Non-empty cell.\n");
	printfColor(COLOR_RED, "X = Cell that exists but is empty.\n");
	printfColor(COLOR_BRIGHT|COLOR_GREEN, "X = Cell is in a collision scene.\n");
	printf("Max actors per scene = %u\n", wcgState.fg.sceneConfig.maxActors);
	printf("Cell size = %u\n", WC_GRID_CELL_SIZE);

	EARRAY_CONST_FOREACH_BEGIN(wcgState.fg.staticScenes, i, isize);
	{
		WorldCollStaticScene*	ss = wcgState.fg.staticScenes[i];
		U32						instanceID;
		U32						actorCount;
		U32						nxActorCount;
		U32						actorCreateCount;
		U32						actorCreateFailedCount;
		U32						actorDestroyCount;
		PSDKScenePruningType	staticPruningType;
		PSDKScenePruningType	dynamicPruningType;
		IVec3					loScene;
		IVec3					hiScene;
			
		psdkSceneGetInstanceID(ss->psdkScene, &instanceID);
			
		psdkSceneGetActorCounts(ss->psdkScene,
								&actorCount,
								&nxActorCount,
								&actorCreateCount,
								&actorCreateFailedCount,
								&actorDestroyCount);
									
		psdkSceneGetConfig(	ss->psdkScene,
							&staticPruningType,
							&dynamicPruningType);

		printf(	"%d/%d: Scene 0x%p(#%d) (%d cells, %d wcos, %d actors (%d PhysX), %d created, %d failed, %d destroyed, %d spt, %d dpt).\n",
				i + 1,
				isize,
				ss,
				instanceID,
				ss->cellCount,
				ss->wcoCount,
				actorCount,
				nxActorCount,
				actorCreateCount,
				actorCreateFailedCount,
				actorDestroyCount,
				staticPruningType,
				dynamicPruningType);

		EARRAY_CONST_FOREACH_BEGIN(ss->placements, j, jsize);
		{
			WorldCollScenePlacement*	sp = ss->placements[j];
			WorldColl*					wc = sp->wc;
			const WorldCollGrid*		g = &wc->fg.grid;
			IVec3						lo;
			IVec3						hi;
			char						indexChar = getCharForIndex(j);

			addVec3(g->lo, sp->gridOffset, lo);
			addVec3(g->hi, sp->gridOffset, hi);

			if(!j){
				copyVec3(lo, loScene);
				copyVec3(hi, hiScene);
			}else{
				MINVEC3(lo, loScene, loScene);
				MAXVEC3(hi, hiScene, hiScene);
			}

			printfColor(COLOR_BRIGHT | (1 + (j % 7)),
						"%c. WorldColl 0x%p (%d, %d) - (%d, %d), offset (%d, %d) cell size %d.\n",
						indexChar,
						wc,
						g->lo[0],
						g->lo[2],
						g->hi[0],
						g->hi[2],
						sp->gridOffset[0],
						sp->gridOffset[2],
						WC_GRID_CELL_SIZE);
		}
		EARRAY_FOREACH_END;

		FOR_REVERSE_BEGIN_TO(z, hiScene[2] - 1, loScene[2]);
		{
			FOR_BEGIN_FROM(x, loScene[0], hiScene[0]);
			{
				WorldColl*					wc;
				WorldCollScenePlacement*	sp;
				WorldCollGridCell*			cell;
				IVec3						gridPos = {x, 0, z};

				if(wcStaticSceneGetAtGridPos(ss, gridPos, &wc, &sp, &cell)){
					S32		index = eaFind(&ss->placements, sp);
					char	indexChar = getCharForIndex(index);
					S32		cellInPlacement = SAFE_MEMBER(cell, placement) == sp;

					assert(index >= 0);

					printfColor((cellInPlacement ? COLOR_BRIGHT : 0) | (1 + (index % 7)),
								"%c",
								indexChar);
				}else{
					printf(" ");
				}
			}
			FOR_END;

			printf("\n");
		}
		FOR_END;

		EARRAY_CONST_FOREACH_BEGIN(ss->placements, j, jsize);
		{
			WorldCollScenePlacement*	sp = ss->placements[j];
			WorldColl*					wc = sp->wc;
			const WorldCollGrid*		g = &wc->fg.grid;

			printf(	"WorldColl 0x%p (%d, %d) - (%d, %d), offset (%d, %d) cell size %d.\n",
					wc,
					g->lo[0],
					g->lo[2],
					g->hi[0],
					g->hi[2],
					sp->gridOffset[0],
					sp->gridOffset[2],
					WC_GRID_CELL_SIZE);
				
			FOR_REVERSE_BEGIN_TO(z, g->hi[2] - 1, g->lo[2]);
			{
				FOR_BEGIN_FROM(x, g->lo[0], g->hi[0]);
				{
					char				theChar = x ? z ? 'X' : '-' : z ? '|' : '+';
					WorldCollGridCell*	cell;
				
					if(!wcGetCellByGridPosFG(wc, &cell, x, z)){
						printfColor(COLOR_RED|COLOR_GREEN|COLOR_BLUE,
									"%c",
									theChar == 'X' ? ' ' : theChar);
					}
					else if(cell->placement == sp){
						if(cell->wcoCount){
							printfColor(COLOR_BRIGHT|COLOR_GREEN, "%c", theChar);
						}else{
							printfColor(COLOR_BRIGHT|COLOR_RED, "%c", theChar);
						}
					}
					else if(cell->placement){
						if(cell->wcoCount){
							printfColor(COLOR_GREEN, "%c", theChar);
						}else{
							printfColor(COLOR_RED, "%c", theChar);
						}
					}else{
						if(cell->wcoCount){
							printfColor(COLOR_BLUE, "%c", theChar);
						}else{
							printfColor(COLOR_RED|COLOR_BLUE, "%c", theChar);
						}
					}
				}
				FOR_END;
			
				printf("\n");
			}
			FOR_END;
		
			EARRAY_CONST_FOREACH_BEGIN(sp->cells, k, ksize);
			{
				WorldCollGridCell*	cell = sp->cells[k];
				U32					uniqueStaticCount;
				U32					uniqueDynamicCount;
				
				alGetCount(cell->alPrimaryDynamicWCOs);
				
				wcCellGetUniqueWCOCountInScenePlacement(cell, sp, &uniqueStaticCount, 1, 0);
				wcCellGetUniqueWCOCountInScenePlacement(cell, sp, &uniqueDynamicCount, 0, 1);
				
				printf(	"  Cell ( %4d, %4d ) 0x%p: %5d static (%5d unique), %5d dynamic(%5d unique).\n",
						cell->gridPos[0],
						cell->gridPos[2],
						cell,
						alGetCount(cell->alPrimaryStaticWCOs),
						uniqueStaticCount,
						alGetCount(cell->alPrimaryDynamicWCOs),
						uniqueDynamicCount);
			}
			EARRAY_FOREACH_END;
		}
		EARRAY_FOREACH_END;
	}
	EARRAY_FOREACH_END;
	
	EARRAY_CONST_FOREACH_BEGIN(wcgState.fg.wcis, i, isize);
	{
		WorldCollIntegration* wci = wcgState.fg.wcis[i];
		
		EARRAY_CONST_FOREACH_BEGIN(wci->scenes, j, jsize);
		{
			
		}
		EARRAY_FOREACH_END;
	}
	EARRAY_FOREACH_END;

	printf("WorldColl Debug Info End ---------------------------------------------------\n");
#endif
}

AUTO_COMMAND;
void wcCellPrintDebugInfo(	S32 x,
							S32 z,
							S32 showAllObjects)
{
#if !PSDK_DISABLED
	printf("WorldCollGridCell Debug Info Begin -------------------------------------------------\n");

	EARRAY_CONST_FOREACH_BEGIN(wcgState.wcs, i, isize);
	{
		WorldColl*				wc = wcgState.wcs[i];
		const WorldCollGrid*	g = &wc->fg.grid;
		WorldCollGridCell*		cell;

		printf(	"WorldColl 0x%p (%d, %d) - (%d, %d), cell size %d.\n",
				wc,
				g->lo[0],
				g->lo[2],
				g->hi[0],
				g->hi[2],
				WC_GRID_CELL_SIZE);
				
		printf(	"PhysicsSDK: %d scenes.\n",
				psdkGetSceneCount());

		if(!wcGetCellByGridPosFG(wc, &cell, x, z)){
			printf("Cell %d, %d doesn't exist.\n", x, z);
			continue;
		}else{
			AssociationListIterator*	iter;
			WorldCollObject*			wco;
			
			if(cell->placement){
				printf(	"Cell 0x%p (%d, %d), offset (%d, %d)/(%1.2f, %1.2f): %d wcos.\n",
						cell,
						x,
						z,
						vecParamsXZ(cell->placement->gridOffset),
						vecParamsXZ(cell->placement->sceneOffset),
						cell->wcoCount);
			}else{
				printf(	"Cell 0x%p (%d, %d), not placed: %d wcos.\n",
						cell,
						x,
						z,
						cell->wcoCount);
			}
			
			FOR_BEGIN(j, 2);
			{
				if(!j){
					if(!cell->placement){
						printf("All WCOs (there's no collision scene):\n");
					}else{
						printf("Unique WCOs:\n");
					}
				}else{
					if(	!cell->placement ||
						!showAllObjects)
					{
						break;
					}

					printf("Shared WCOs:\n");
				}
			
				printf(	"####. %8s %8s %8s (pos) (bou)-(nds)[flags]: owner description\n",
						"wco",
						"actor",
						"shape");

				FOR_BEGIN(k, 2);
				{
					AssociationList*	al = k ? cell->alPrimaryDynamicWCOs : cell->alPrimaryStaticWCOs;
					U32					count = 0;
					
					for(alItCreate(&iter, al);
						alItGetOwner(iter, &wco);
						alItGotoNextThenDestroy(&iter))
					{
						if(	!cell->placement
							||
							!j &&
							!wcoIsInScenePlacementMoreThanOnce(wco, cell->placement)
							||
							j &&
							wcoIsInScenePlacementMoreThanOnce(wco, cell->placement))
						{
							PSDKActor*	psdkActor;
							Vec3		pos = {0};
							Vec3		boundsMin = {0};
							Vec3		boundsMax = {0};
							U32			filterBits = 0;
							char		buffer[1000] = "";
							char		extraString[1000] = "";
							
							alItGetValues(iter, NULL, &psdkActor);
							
							if(psdkActor){
								psdkActorGetPos(psdkActor, pos);
								subVec3(pos, cell->placement->sceneOffset, pos);
								psdkActorGetBounds(psdkActor, boundsMin, boundsMax);
								subVec3(boundsMin, cell->placement->sceneOffset, boundsMin);
								subVec3(boundsMax, cell->placement->sceneOffset, boundsMax);
								psdkActorGetFilterBits(psdkActor, &filterBits);
							}

							wcoGetDebugString(wco, SAFESTR(buffer));
							
							if(wco->flags.destroyed){
								strcatf(extraString, ", destroyed");
							}
							
							if(wco->flags.destroyedByUser){
								strcatf(extraString, ", destroyedByUser");
							}
							
							if(wco->flags.isShell){
								strcatf(extraString, ", isShell");
							}
							
							printf(	"%4d. %8.8p %8.8p %8.8x (%1.2f, %1.2f, %1.2f) (%1.2f, %1.2f, %1.2f)-(%1.2f, %1.2f, %1.2f)%s: %s\n",
									++count,
									wco,
									psdkActor,
									filterBits,
									vecParamsXYZ(pos),
									vecParamsXYZ(boundsMin),
									vecParamsXYZ(boundsMax),
									extraString,
									buffer);
						}
					}
				}
				FOR_END;
				
				printf(	"####. %8s %8s %8s (pos) (bou)-(nds)[flags]: owner description\n",
						"wco",
						"actor",
						"shape");
			}
			FOR_END;
		}
	}
	EARRAY_FOREACH_END;

	printf("WorldCollGridCell Debug Info End ---------------------------------------------------\n");
#endif
}

AUTO_COMMAND;
void wcPrintSceneInfo(void){
	wcgState.flags.printSceneInfoOnSwap = 1;
}

