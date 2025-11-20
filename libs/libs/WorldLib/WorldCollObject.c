
#include "WorldCollPrivate.h"
#include "wcoll/collcache.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Physics););

MP_DEFINE(WorldCollObject);

static void wcoMessageInit(	WorldCollObjectMsg* msg,
							WorldCollObject* wco,
							WorldCollObjectMsgType msgType)
{
	ZeroStruct(msg);
	assert(msg);
	msg->msgType = msgType;
	msg->userPointer = wco->userPointer;
	msg->wco = wco;
}

static void wcoMessageSend(const WorldCollObjectMsg* msg){
	msg->wco->msgHandler(msg);
}

void wcoGetDebugString(	WorldCollObject* wco,
						char* buffer,
						S32 bufferLen)
{
	WorldCollObjectMsg msg = {0};
	
	wcoMessageInit(&msg, wco, WCO_MSG_GET_DEBUG_STRING);

	buffer[0] = 0;
	msg.in.getDebugString.buffer = buffer;
	msg.in.getDebugString.bufferLen = bufferLen;

	wcoMessageSend(&msg);
}

static S32 wcoGetFirstActor(const WorldCollObject* wco,
							PSDKActor** actorOut,
							Vec3 sceneOffsetOut)
{
	if(wco){
		AssociationListIterator*	iter;
		PSDKActor*					actor;
		
		for(alItCreate(&iter, wco->alSecondaryCells);
			alItGetValues(iter, NULL, &actor);
			alItGotoNextThenDestroy(&iter))
		{
			if(actor){
				*actorOut = actor;

				if(sceneOffsetOut){
					WorldCollGridCell* cell;
					alItGetOwner(iter, &cell);
					copyVec3(cell->placement->sceneOffset, sceneOffsetOut);
				}

				alItDestroy(&iter);
				return 1;
			}
		}
	}
	
	return 0;
}

void wcoChangeBounds(	WorldCollObject* wco,
						const Vec3 aabbMin,
						const Vec3 aabbMax)
{
	AssociationListIterator*	iter;
	WorldCollGridCell*			cell;

	if(!wco->dynamic){
		return;
	}

	CHECK_FINITEVEC3(aabbMin);
	CHECK_FINITEVEC3(aabbMax);
	
	wco->flags.boundsChanged = 1;
	
	copyVec3(aabbMin, wco->dynamic->aabbMin);
	copyVec3(aabbMax, wco->dynamic->aabbMax);

	for(alItCreate(&iter, wco->alSecondaryCells);
		alItGetOwner(iter, &cell);
		alItGotoNextThenDestroy(&iter))
	{
		cell->wc->fg.flags.wcoDynamicChangedBounds = 1;
	}
}

S32 wcoGetStoredModelData(	const WorldCollStoredModelData** smdOut,
							const WorldCollModelInstanceData** instOut,
							WorldCollObject* wco,
							U32 filterBits)
{
	WorldCollObjectMsg					msg = {0};
	WorldCollObjectMsgGetModelDataOut	out = {0};

	if(	!wco ||
		!smdOut ||
		!filterBits)
	{
		return 0;
	}

	wcoMessageInit(&msg, wco, WCO_MSG_GET_MODEL_DATA);
	msg.in.getModelData.filterBits = filterBits;
	msg.out.getModelData = &out;
	wcoMessageSend(&msg);

	*smdOut = out.modelData;

	if(instOut){
		*instOut = out.instData;
	} else {
		SAFE_FREE(out.instData);
	}
	
	return !!out.modelData;
}

S32 wcoGetInstData(	const WorldCollModelInstanceData** instOut,
					WorldCollObject* wco,
					U32 filterBits)
{
	WorldCollObjectMsg					msg = {0};
	WorldCollObjectMsgGetInstDataOut	getInstData = {0};

	if(	!wco ||
		!instOut ||
		!filterBits)
	{
		return 0;
	}

	wcoMessageInit(&msg, wco, WCO_MSG_GET_INSTANCE_DATA);
	msg.in.getInstData.filterBits = filterBits;
	msg.out.getInstData = &getInstData;
	wcoMessageSend(&msg);

	*instOut = getInstData.instData;

	return !!getInstData.instData;
}

S32 wcoGetActor(WorldCollObject* wco,
				PSDKActor** actorOut,
				Vec3 sceneOffsetOut)
{
	return wcoGetFirstActor(wco, actorOut, sceneOffsetOut);
}

S32 wcoGetBounds(WorldCollObject *wco,
				 Vec3 boundsMinOut,
				 Vec3 boundsMaxOut)
{
#if PSDK_DISABLED
	return 0;
#else
	PSDKActor*	actor;
	Vec3		sceneOffset;
	
	if(	!wco ||
		!boundsMinOut ||
		!boundsMaxOut ||
		!wcoGetFirstActor(wco, &actor, sceneOffset) ||
		!psdkActorGetBounds(actor, boundsMinOut, boundsMaxOut))
	{
		return 0;
	}

	subVec3(boundsMinOut, sceneOffset, boundsMinOut);
	subVec3(boundsMaxOut, sceneOffset, boundsMaxOut);

	return 1;
#endif
}

static void wcIntegrationSendMsgWorldCollObjectDestroyed(	WorldCollIntegration* wci,
															WorldCollObject* wco)
{
	WorldCollIntegrationMsg	msg = {0};

	msg.msgType = WCI_MSG_NOBG_WORLDCOLLOBJECT_DESTROYED;

	msg.wci = wci;
	msg.userPointer = wci->userPointer;

	msg.nobg.worldCollObjectDestroyed.wco = wco;

	wci->msgHandler(&msg);
}

static void wcoDestroyInternal(WorldCollObject** wcoInOut){
	WorldCollObject* wco = SAFE_DEREF(wcoInOut);

	if(wco){
		WorldCollObjectMsg msg;

		alDestroy(&wco->alSecondaryCells);
		alDestroy(&wco->alSecondaryScenes);
		
		if(!wco->flags.destroyedByUser){
			wcoMessageInit(&msg, wco, WCO_MSG_DESTROYED);
			wcoMessageSend(&msg);
		}
		
		if(wco->dynamic){
			// Remove from the dynamics list.
			
			S32 found = 0;
			
			EARRAY_CONST_FOREACH_BEGIN(wcgState.wcs, i, isize);
			{
				WorldColl* wc = wcgState.wcs[i];
				
				if(eaFindAndRemoveFast(&wc->wcosDynamicMutable, wco) >= 0){
					ASSERT_FALSE_AND_SET(found);
				}
			}
			EARRAY_FOREACH_END;
			
			assert(found);
			
			SAFE_FREE(wco->dynamic);
		}
				
		EARRAY_CONST_FOREACH_BEGIN(wcgState.fg.wcis, i, isize);
		{
			wcIntegrationSendMsgWorldCollObjectDestroyed(wcgState.fg.wcis[i], wco);
		}
		EARRAY_FOREACH_END;

		MP_FREE(WorldCollObject, *wcoInOut);
	}
}

static void wcoNotifyRefListEmpty(	WorldCollObject* wco,
									AssociationList* alEmpty)
{
	if(alEmpty == wco->alSecondaryCells){
		alDestroy(&wco->alSecondaryScenes);
		wcoDestroyInternal(&wco);
	}
	else if(wco->alSecondaryScenes){
		assert(alEmpty == wco->alSecondaryScenes);
		
		if(alIsEmpty(wco->alSecondaryCells)){
			wcoDestroyInternal(&wco);
		}
	}
}

MP_DEFINE(WorldCollObjectMsgGetShapeOutInst);

void wcoAddShapeInstance(	WorldCollObjectMsgGetShapeOut* getShape,
							WorldCollObjectMsgGetShapeOutInst** instOut)
{
	WorldCollObjectMsgGetShapeOutInst* inst;
	
	MP_CREATE(WorldCollObjectMsgGetShapeOutInst, 32);
	inst = MP_ALLOC(WorldCollObjectMsgGetShapeOutInst);
	copyMat4(unitmat, inst->mat);
	inst->density = 1;
	eaPush(&getShape->meshes, inst);
	inst->shapeType = WCO_ST_COOKED_MESH;
	*instOut = inst;
}

S32 wcoFindActorInScenePlacement(	const WorldCollObject* wco,
									const WorldCollScenePlacement* sp,
									PSDKActor** psdkActorOut)
{
	AssociationListIterator*	iter;
	WorldCollGridCell*			cell;

	for(alItCreate(&iter, wco->alSecondaryCells);
		alItGetOwner(iter, &cell);
		alItGotoNextThenDestroy(&iter))
	{
		if(	sp == cell->placement &&
			alItGetValues(iter, NULL, psdkActorOut) &&
			*psdkActorOut)
		{
			alItDestroy(&iter);
			return 1;
		}
	}
	
	return 0;
}

static void wcoGetThisActorCount(	WorldCollObject* wco,
									PSDKActor* psdkActor,
									U32* countOut)
{
	AssociationListIterator*	iter;
	PSDKActor*					psdkActorCheck;
	U32							count = 0;
	
	for(alItCreate(&iter, wco->alSecondaryCells);
		alItGetValues(iter, NULL, &psdkActorCheck);
		alItGotoNextThenDestroy(&iter))
	{
		if(psdkActorCheck == psdkActor){
			count++;
		}
	}
	
	*countOut = count;
}

static S32 wcoIsThisActorInAnyGridCell(	WorldCollObject* wco,
										PSDKActor* psdkActor)
{
	AssociationListIterator*	iter;
	PSDKActor*					psdkActorCheck;
	
	for(alItCreate(&iter, wco->alSecondaryCells);
		alItGetValues(iter, NULL, &psdkActorCheck);
		alItGotoNextThenDestroy(&iter))
	{
		if(psdkActorCheck == psdkActor){
			alItDestroy(&iter);
			return 1;
		}
	}
	
	return 0;
}

void wcoSetActorInScenePlacement(	WorldCollObject*const wco,
									WorldCollGridCell*const cellToIgnore,
									WorldCollScenePlacement*const sp,
									const PSDKActor*const psdkActorFrom,
									PSDKActor*const psdkActorTo)
{
	AssociationListIterator*	iter;
	WorldCollGridCell*			cell;

	for(alItCreate(&iter, wco->alSecondaryCells);
		alItGetOwner(iter, &cell);
		alItGotoNextThenDestroy(&iter))
	{
		PSDKActor* psdkActorCurrent;

		if(	cell == cellToIgnore ||
			sp != cell->placement)
		{
			continue;
		}

		alItGetValues(iter, NULL, &psdkActorCurrent);

		if(psdkActorCurrent){
			if(psdkActorFrom){
				assert(psdkActorCurrent == psdkActorFrom);
			}else{
				assert(psdkActorCurrent == psdkActorTo);
			}
		}

		if(psdkActorCurrent != psdkActorTo){
			alItSetUserPointer(iter, psdkActorTo);

			if(psdkActorCurrent){
				// Verify that the previous actor is still in another grid cell.

				if(!wcoIsThisActorInAnyGridCell(wco, psdkActorCurrent)){
					wcPSDKActorTrackPrintOwners(psdkActorCurrent);
					assert(0);
				}

				wcPSDKActorTrackDecrement(NULL, WC_PSDK_ACTOR_OWNER_STATIC, iter, psdkActorCurrent);
			}

			wcPSDKActorTrackIncrement(NULL, WC_PSDK_ACTOR_OWNER_STATIC, iter, psdkActorTo);
		}
	}
}

static void wcIntegrationSendMsgActorCreated(	WorldCollIntegration* wci,
												WorldColl* wc,
												WorldCollObject* wco,
												const Vec3 boundsMin,
												const Vec3 boundsMax,
												PSDKActor* psdkActor,
												const Vec3 sceneOffset,
												U32 isShell)
{
	WorldCollIntegrationMsg msg = {0};
	WorldCollObjectPrev		prev;

	msg.msgType = WCI_MSG_NOBG_ACTOR_CREATED;

	msg.wci = wci;
	msg.userPointer = wci->userPointer;
	msg.nobg.actorCreated.wc = wc;
	msg.nobg.actorCreated.wco = wco;
	copyVec3(boundsMin, msg.nobg.actorCreated.boundsMin);
	copyVec3(boundsMax, msg.nobg.actorCreated.boundsMax);
	msg.nobg.actorCreated.psdkActor = psdkActor;
	copyVec3(sceneOffset, msg.nobg.actorCreated.sceneOffset);
	msg.nobg.actorCreated.isShell = !!isShell;
	
	if(	wco->dynamic &&
		wco->flags.hadPrevActor)
	{
		msg.nobg.actorCreated.prev = &prev;
		copyVec3(wco->dynamic->prev.pos, prev.mat[3]);
		createMat3YPR(prev.mat, wco->dynamic->prev.pyr);
	}

	wci->msgHandler(&msg);
}

static void wcGridCellSendMsgsActorCreated(	WorldCollGridCell* cell,
											WorldCollObject* wco,
											PSDKActor* psdkActor)
{
#if !PSDK_DISABLED
	Vec3 boundsMin;
	Vec3 boundsMax;

	if(	!wcNotificationsEnabled() ||
		!psdkActorGetBounds(psdkActor, boundsMin, boundsMax))
	{
		return;
	}

	if(cell->placement->flags.hasOffset){
		subVec3(boundsMin, cell->placement->sceneOffset, boundsMin);
		subVec3(boundsMax, cell->placement->sceneOffset, boundsMax);
	}

	collCacheInvalidate(psdkActor, boundsMin, boundsMax);

	EARRAY_CONST_FOREACH_BEGIN(wcgState.fg.wcis, i, isize);
	{
		wcIntegrationSendMsgActorCreated(	wcgState.fg.wcis[i],
											cell->wc,
											wco,
											boundsMin,
											boundsMax,
											psdkActor,
											cell->placement->sceneOffset,
											wco->flags.isShell);
	}
	EARRAY_FOREACH_END;
#endif
}

S32 wcoCreateCellActor(	WorldCollObject* wco,
						WorldCollGridCell* cell,
						AssociationNode* node)
{
#if PSDK_DISABLED
    return 0;
#else
    WorldColl*						wc = cell->wc;
	WorldCollObjectMsg				msg = {0};
	WorldCollObjectMsgGetShapeOut	getShape = {0};
	PSDKActor*						psdkActor = NULL;
	PSDKBodyDesc					bodyDesc = {0};
	PSDKActorDesc*					actorDesc;
	S32								validCount = 0;

	PERFINFO_AUTO_START_FUNC();

	if(alNodeGetValues(node, NULL, NULL, &psdkActor)){
		assert(!psdkActor);
	}

	// Get the shape data from the wco's owner.

	bodyDesc.scale = 1.0f;

	PERFINFO_AUTO_START("WCO_MSG_GET_SHAPE", 1);
	{
		wcoMessageInit(&msg, wco, WCO_MSG_GET_SHAPE);

		msg.out.getShape = &getShape;
		getShape.bodyDesc = &bodyDesc;

		wcoMessageSend(&msg);
	}
	PERFINFO_AUTO_STOP_CHECKED("WCO_MSG_GET_SHAPE");

	// Make sure there's some valid meshes.

	EARRAY_CONST_FOREACH_BEGIN(getShape.meshes, i, isize);
	{
		WorldCollObjectMsgGetShapeOutInst* inst = getShape.meshes[i];
		
		if(	inst->shapeType &&
			(	inst->shapeType != WCO_ST_COOKED_MESH ||
				psdkCookedMeshIsValid(inst->mesh))
			)
		{
			validCount++;
		}
	}
	EARRAY_FOREACH_END;

	if(!validCount){
		PERFINFO_AUTO_STOP();
		return 0;
	}

	if(!psdkActorDescCreate(&actorDesc)){
		PERFINFO_AUTO_STOP();
		return 0;
	}
	
	if(cell->placement->flags.hasOffset){
		addVec3(getShape.mat[3],
				cell->placement->sceneOffset,
				getShape.mat[3]);
	}

	psdkActorDescSetMat4(	actorDesc,
							getShape.mat);

	// Add all the meshes.

	EARRAY_CONST_FOREACH_BEGIN(getShape.meshes, i, isize);
	{
		WorldCollObjectMsgGetShapeOutInst* inst = getShape.meshes[i];

		switch(inst->shapeType){
			xcase WCO_ST_COOKED_MESH:{
				if(psdkCookedMeshIsValid(inst->mesh)){
					psdkActorDescAddMesh(	actorDesc,
											inst->mesh,
											inst->mat,
											inst->density,
											SAFE_MEMBER(inst->material, index),
											inst->filter.filterBits,
											inst->filter.shapeGroup,
											0);
				}
			}
			
			xcase WCO_ST_CAPSULE:{
				psdkActorDescAddCapsule(actorDesc,
										inst->capsule.length,
										inst->capsule.radius,
										inst->mat,
										inst->density,
										SAFE_MEMBER(inst->material, index),
										inst->filter.filterBits,
										inst->filter.shapeGroup);
			}

			xcase WCO_ST_SPHERE:{
				psdkActorDescAddSphere(	actorDesc,
										inst->sphere.radius,
										inst->mat,
										inst->density,
										SAFE_MEMBER(inst->material, index),
										inst->filter.filterBits,
										inst->filter.shapeGroup);
			}

			xcase WCO_ST_BOX:{
				psdkActorDescAddBox(actorDesc,
									inst->box.xyzSize,
									inst->mat,
									inst->density,
									SAFE_MEMBER(inst->material, index),
									inst->filter.filterBits,
									inst->filter.shapeGroup);
			}
		}
		
		MP_FREE(WorldCollObjectMsgGetShapeOutInst, inst);
	}
	EARRAY_FOREACH_END;

	eaDestroy(&getShape.meshes);

	// Create the psdkActor.

	psdkActorCreate(&psdkActor,
					wcgState.psdkSimulationOwnership,
					actorDesc,
					getShape.flags.useBodyDesc && getShape.bodyDesc ?
						&bodyDesc :
						NULL,
					cell->placement->ss->psdkScene,
					node,
					getShape.flags.isKinematic,
					0,
					getShape.flags.hasOneWayCollision,
					0);

	psdkActorDescDestroy(&actorDesc);

	if(psdkActor){
		wcPSDKActorTrackCreate(node, WC_PSDK_ACTOR_OWNER_STATIC, NULL, psdkActor);
		alNodeSetUserPointer(node, psdkActor);

		wcoSetActorInScenePlacement(wco, NULL, cell->placement, NULL, psdkActor);

		if(FALSE_THEN_SET(wco->flags.sentMsgActorCreated)){
			wcGridCellSendMsgsActorCreated(cell, wco, psdkActor);
		}

		cell->placement->ss->flags.needsToSimulate = 1;
		wcgState.fg.flags.ssNeedsToSimulate = 1;
	}
	
	PERFINFO_AUTO_STOP();

	return !!psdkActor;
#endif
}

S32	wcoIsShell(const WorldCollObject* wco){
	return SAFE_MEMBER(wco, flags.isShell);
}

S32	wcoIsDynamic(const WorldCollObject* wco){
	return !!SAFE_MEMBER(wco, dynamic);
}

S32 wcoGetPos(	const WorldCollObject* wco,
				Vec3 posOut)
{
#if PSDK_DISABLED
	return 0;
#else
	PSDKActor*	actor;
	Vec3		sceneOffset;

	if(	!wcoGetFirstActor(wco, &actor, sceneOffset) ||
		!psdkActorGetPos(actor, posOut))
	{
		return 0;
	}

	subVec3(posOut, sceneOffset, posOut);
	return 1;
#endif
}

S32 wcoGetMat(	const WorldCollObject* wco,
				Mat4 matOut)
{
#if PSDK_DISABLED
	return 0;
#else
	PSDKActor*	actor;
	Vec3		sceneOffset;

	if(	!wcoGetFirstActor(wco, &actor, sceneOffset) ||
		!psdkActorGetMat(actor, matOut))
	{
		return 0;
	}

	subVec3(matOut[3], sceneOffset, matOut[3]);
	return 1;
#endif
}

S32 wcoGetUserPointer(	const WorldCollObject* wco,
						WorldCollObjectMsgHandler msgHandler,
						void** userPointerOut)
{
	if(	wco &&
		wco->msgHandler == msgHandler &&
		wco->userPointer)
	{
		if(userPointerOut){
			*userPointerOut = wco->userPointer;
		}

		return 1;
	}
	
	return 0;
}

S32 wcoIsInStaticScene(	const WorldCollObject* wco,
						const WorldCollStaticScene* ss)
{
	AssociationListIterator*	iter;
	WorldCollGridCell*			cell;

	if(!ss){
		return 0;
	}
	
	for(alItCreate(&iter, wco->alSecondaryCells);
		alItGetOwner(iter, &cell);
		alItGotoNextThenDestroy(&iter))
	{
		if(ss == SAFE_MEMBER(cell->placement, ss)){
			alItDestroy(&iter);
			return 1;
		}
	}

	return 0;
}

static void wcoDynamicUpdateGridActorsFG(	WorldColl* wc,
											WorldCollObject* wco,
											const IVec3 gridMin,
											const IVec3 gridMax)
{
#if !PSDK_DISABLED
	// For each cell that wco is still in, recreate or move the actor.

	AssociationListIterator*	iter;
	WorldCollGridCell*			cell;
	
	assert(!wcgState.fg.flags.simulating);

	for(alItCreate(&iter, wco->alSecondaryCells);
		alItGetOwner(iter, &cell);
		alItGotoNextThenDestroy(&iter))
	{
		PSDKActor*	psdkActor;
		Mat4		mat;

		if(	cell->gridPos[0] < gridMin[0] ||
			cell->gridPos[0] > gridMax[0] ||
			cell->gridPos[2] < gridMin[2] ||
			cell->gridPos[2] > gridMax[2])
		{
			// wco isn't in this cell anymore.

			alItNodeRemove(iter);
			continue;
		}

		// If existing actor is not kinematic, then recreate it.

		alItGetValues(iter, NULL, &psdkActor);
		
		if(!psdkActor){
			continue;
		}
		
		wco->flags.hadPrevActor = 1;
		psdkActorGetMat(psdkActor, mat);
		copyVec3(mat[3], wco->dynamic->prev.pos);
		getMat3YPR(mat, wco->dynamic->prev.pyr);

		if(!psdkActorIsKinematic(psdkActor)){
			AssociationNode* node;
			
			assert(cell->placement);

			// Remove from all cells except this one.

			wcoSetActorInScenePlacement(wco, cell, cell->placement, psdkActor, NULL);

			alItGetNode(iter, &node);
			wcGridCellActorDestroy(cell, wco, node, &psdkActor);

			wcCellSetActorRefresh(cell, 1);
		}else{
			// Request the new position from wco.
			
			WorldCollObjectMsg				msg = {0};
			WorldCollObjectMsgGetNewMatOut	getNewMat = {0};
			
			copyMat4(mat, getNewMat.mat);
			
			wcoMessageInit(&msg, wco, WCO_MSG_GET_NEW_MAT);

			msg.out.getNewMat = &getNewMat;

			wcoMessageSend(&msg);
			
			// TODO: this needs to send a msg.

			psdkActorMoveMat(psdkActor, getNewMat.mat);
		}
	}
#endif
}

void wcoUpdateGridFG(	WorldColl* wc,
						WorldCollObject* wco,
						const Vec3 aabbMin,
						const Vec3 aabbMax)
{
#if !PSDK_DISABLED
	IVec3	gridMin;
	IVec3 	gridMax;
	IVec3	gridOldMin;
	IVec3	gridOldMax;
	S32 	i;
	S32		isInGrid =	wco->dynamic &&
						!alIsEmpty(wco->alSecondaryCells);
						
	wco->flags.sentMsgActorCreated = 0;
	
	for(i = 0; i < 3; i += 2){
		gridMin[i] = wcWorldCoordToGridIndex(	aabbMin[i] - 50,
												WC_GRID_CELL_SIZE);

		gridMax[i] = wcWorldCoordToGridIndex(	aabbMax[i] + 50,
												WC_GRID_CELL_SIZE);

		if(isInGrid){
			gridOldMin[i] = wcWorldCoordToGridIndex(wco->dynamic->aabbMin[i] - 50,
													WC_GRID_CELL_SIZE);

			gridOldMax[i] = wcWorldCoordToGridIndex(wco->dynamic->aabbMax[i] + 50,
													WC_GRID_CELL_SIZE);
		}
	}

	if(wco->dynamic){
		copyVec3(aabbMin, wco->dynamic->aabbMin);
		copyVec3(aabbMax, wco->dynamic->aabbMax);
	}

	// Add to cells that are new for this wco.

	FOR_BEGIN_FROM(x, gridMin[0], gridMax[0] + 1);
	{
		FOR_BEGIN_FROM(z, gridMin[2], gridMax[2] + 1);
		{
			WorldCollGridCell* cell;
			
			if(	isInGrid &&
				x >= gridOldMin[0] &&
				x <= gridOldMax[0] &&
				z >= gridOldMin[2] &&
				z <= gridOldMax[2])
			{
				// Already in this cell (hopefully).

				continue;
			}

			if(!wcGetOrCreateCellByGridPosFG(wc, &cell, x, z, "Place object")){
				continue;
			}

			cell->wcoCount++;
			
			// If wco is not already in a cell that's in the same ss, increment the ss's wcoCount.
			
			if(cell->placement){
				WorldCollStaticScene* ss = cell->placement->ss;

				if(	!wcoIsInStaticScene(wco, ss) &&
					ss->wcoCountMutable++ == wcgState.fg.sceneConfig.maxActors &&
					ss->cellCount > 1)
				{
					// Went over the max actors per ss, check if it needs to split.
				
					wcgState.fg.flags.ssNeedsToSplit = 1;
					
					printf(	"WorldCollStaticScene 0x%p (%u grid cells) needs to split.\n",
							ss,
							ss->cellCount);
				}
			}
		
			// Create the association with the cell.
		
			alAssociate(NULL,
						wco->dynamic ?
							cell->alPrimaryDynamicWCOs :
							cell->alPrimaryStaticWCOs,
						wco->alSecondaryCells,
						NULL);

			// Force a refresh of actors in the cell.

			if(cell->placement){
				assert(cell->wcoCount <= cell->placement->ss->wcoCount);
				wcCellSetActorRefresh(cell, !!wco->dynamic);
			}
		}
		FOR_END;
	}
	FOR_END;
	
	if(isInGrid){
		wcoDynamicUpdateGridActorsFG(wc, wco, gridMin, gridMax);
	}
#endif
}

S32 wcoCreate(	WorldCollObject** wcoOut,
				WorldColl* wc,
				WorldCollObjectMsgHandler msgHandler,
				void* userPointer,
				const Vec3 aabbMin,
				const Vec3 aabbMax,
				S32 isDynamic,
				S32 isShell)
{
	WorldCollObject* wco;

	if( !wc ||
		!wcoOut ||
		wcgState.fg.threadID != GetCurrentThreadId())
	{
		return 0;
	}

	assert(!*wcoOut);

	PERFINFO_AUTO_START_FUNC();

	MP_CREATE_COMPACT(WorldCollObject, 1000, 2000, 0.80);

	*wcoOut = wco = MP_ALLOC(WorldCollObject);

	wco->msgHandler = msgHandler;
	wco->userPointer = userPointer;
	wco->flags.isShell = !!isShell;
	
	if(isDynamic){
		wco->dynamic = callocStruct(WorldCollObjectDynamic);
		
		eaPush(&wc->wcosDynamicMutable, wco);
	}
	
	// Setup the cells AssociationList.
	
	wcgState.alTypeWCOCells.flags.isPrimary = 0;
	wcgState.alTypeWCOCells.notifyEmptyFunc = wcoNotifyRefListEmpty;

	alCreate(	&wco->alSecondaryCells,
				wco,
				&wcgState.alTypeWCOCells);

	// Setup the scenes AssociationList.

	wcgState.alTypeWCOScenes.flags.isPrimary = 0;
	wcgState.alTypeWCOScenes.notifyEmptyFunc = wcoNotifyRefListEmpty;

	alCreate(	&wco->alSecondaryScenes,
				wco,
				&wcgState.alTypeWCOScenes);

	// Put into grid.

	wcoUpdateGridFG(wc, wco, aabbMin, aabbMax);

	PERFINFO_AUTO_STOP();

	return 1;
}

static void wcoDestroyCommon(WorldCollObject* wco){
	AssociationListIterator*	iter;
	WorldCollGridCell*			cell;
	
	if(!wco){
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	assert(GetCurrentThreadId() == wcgState.fg.threadID);

	wco->flags.destroyed = 1;

	for(alItCreate(&iter, wco->alSecondaryCells);
		alItGetOwner(iter, &cell);
		alItGotoNextThenDestroy(&iter))
	{
		wcCellSetActorRefresh(cell, !!wco->dynamic);
	}

	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND ACMD_NAME("wcoDestroy");
void wcoCmdDestroy(U64 wcoPointer){
	WorldCollObject* wco = (void*)(uintptr_t)wcoPointer;

	if(wcIsValidObject(wco)){
		wcoDestroyCommon(wco);
	}
}

S32 wcoIsDestroyed(WorldCollObject* wco){
	return SAFE_MEMBER(wco, flags.destroyed);
}

void wcoDestroy(WorldCollObject** wcoInOut){
	WorldCollObject* wco = SAFE_DEREF(wcoInOut);

	if(!wco){
		return;
	}

	PERFINFO_AUTO_START_FUNC();
	
	ASSERT_FALSE_AND_SET(wco->flags.destroyedByUser);

	wco->userPointer = NULL;

	wcoDestroyCommon(wco);

	*wcoInOut = NULL;

	PERFINFO_AUTO_STOP();
}

void wcoDestroyAndNotify(WorldCollObject* wco){
	wcoDestroyCommon(wco);
}

void wcoInvalidate(WorldCollObject* wco){
#if !PSDK_DISABLED
	if(wco){
		AssociationListIterator*	iter;
		PSDKActor*					actor;
		
		assert(GetCurrentThreadId() == wcgState.fg.threadID);

		assert(!wco->flags.destroyed);

		for(alItCreate(&iter, wco->alSecondaryCells);
			alItGetValues(iter, NULL, &actor);
			alItGotoNextThenDestroy(&iter))
		{
			psdkActorInvalidate(actor);
		}
	}
#endif
}

S32 wcoGetFromPSDKActor(WorldCollObject** wcoOut,
						const PSDKActor* psdkActor)
{
#if PSDK_DISABLED
	return 0;
#else
	AssociationNode* node;

	if(	!wcoOut ||
		!psdkActorGetUserPointer(&node, psdkActor) ||
		!alNodeGetOwners(node, NULL, wcoOut))
	{
		return 0;
	}

	return !!*wcoOut;
#endif
}
