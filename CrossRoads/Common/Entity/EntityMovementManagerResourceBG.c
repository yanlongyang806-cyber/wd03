/***************************************************************************
*     Copyright (c) 2005-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "EntityMovementManagerPrivate.h"
#include "EntityLib.h"
#include "wlBeacon.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Physics););

static void mmResourceMsgInitBG(MovementManagedResourceMsgPrivateData* pd,
								MovementManagedResourceMsgOut* out,
								MovementManager* mm,
								MovementManagedResource* mmr,
								MovementManagedResourceMsgType msgType)
{
	mmResourceMsgInit(pd, out, mm, mmr, msgType, MM_BG_SLOT);
}

void mmResourceGetStateDebugStringBG(	MovementManager* mm,
										MovementManagedResource* mmr,
										char** estrBufferInOut)
{
	if(mmr->userStruct.activatedBG){
		MovementManagedResourceMsgPrivateData	pd;

		estrConcatf(estrBufferInOut, "\nStateBG: ");

		mmResourceMsgInitBG(&pd,
							NULL,
							mm,
							NULL,
							MMR_MSG_BG_GET_STATE_DEBUG_STRING);

		pd.msg.in.activatedStruct = mmr->userStruct.activatedBG;

		pd.msg.in.bg.getStateDebugString.estrBuffer = estrBufferInOut;

		mmr->mmrc->msgHandler(&pd.msg);
	}
}

static S32 mmSendMessageDetailResourceRequested(MovementRequester* mr,
												MovementManagedResourceClass* mmrc,
												const void* constant,
												const void* constantNP)
{
	MovementRequester* mrOwner = mr->mm->bg.dataOwner[mmrc->approvingMDC];

	if(0){
		// MS: This nneds to be implemented.

		if(mrOwner){
			MovementRequesterMsgPrivateData pd;
			MovementRequesterMsgOut			out;

			mmRequesterMsgInitBG(	&pd,
									&out,
									mrOwner,
									MR_MSG_BG_DETAIL_RESOURCE_REQUESTED);

			pd.msg.in.bg.detailResourceRequested.mrOther = mr;

			mmRequesterMsgSend(&pd);

			return !out.bg.detailResourceRequested.denied;
		}
	}

	return 1;
}

static void mmResourceStateSetInListBG(MovementManagedResourceState* mmrs){
	mmrsTrackFlags(	mmrs,
					MMRS_TRACK_ALLOCED,
					MMRS_TRACK_FREED | MMRS_TRACK_IN_BG,
					MMRS_TRACK_IN_BG,
					0);

	ASSERT_FALSE_AND_SET(mmrs->bg.flagsMutable.inList);
}

static void mmResourceStateSetNotInListBG(MovementManagedResourceState* mmrs){
	mmrsTrackFlags(	mmrs,
					MMRS_TRACK_ALLOCED | MMRS_TRACK_IN_BG,
					MMRS_TRACK_FREED,
					0,
					MMRS_TRACK_IN_BG);

	ASSERT_TRUE_AND_RESET(mmrs->bg.flagsMutable.inList);
}

static void mmResourceMarkAsUpdatedToFG(MovementManager* mm,
										MovementThreadData* td,
										MovementManagedResource* mmr)
{
	if(FALSE_THEN_SET(mmr->toFG[MM_BG_SLOT].flagsMutable.updated)){
		assert(eaFind(&td->toFG.updatedResources, mmr) < 0);

		eaPush(	&td->toFG.updatedResourcesMutable,
				mmr);

		MM_TD_SET_HAS_TOFG(mm, td);
		td->toFG.flagsMutable.mmrHasUpdate = 1;
	}
}

static void mmResourceSetNeedsSetStateBG(	MovementManager* mm,
											MovementManagedResource* mmr,
											const char* reason)
{
	if(FALSE_THEN_SET(mmr->bg.flagsMutable.needsSetState)){
		if(FALSE_THEN_SET(mm->bg.flagsMutable.mmrNeedsSetState)){
			mmPastStateListCountIncBG(mm);
		}else{
			assert(mm->bg.setPastState.inListCount);
		}
	}else{
		assert(mm->bg.flags.mmrNeedsSetState);
	}
}

static void mmResourceInsertStateBG(MovementManager* mm,
									MovementManagedResource* mmr,
									MovementManagedResourceState* mmrsNew,
									const char* reason)
{
	S32 index = eaSize(&mmr->bg.states);
	
	mmResourceStateSetInListBG(mmrsNew);

	EARRAY_CONST_FOREACH_BEGIN(mmr->bg.states, i, isize);
	{
		const MovementManagedResourceState* mmrs = mmr->bg.states[i];

		if(subS32(	mmrs->spc,
					mmrsNew->spc) > 0)
		{
			index = i;
			break;
		}
	}
	EARRAY_FOREACH_END;
	
	eaInsert(	&mmr->bg.statesMutable,
				mmrsNew,
				index);
				
	mmResourceSetNeedsSetStateBG(mm, mmr, reason);
}

static void mmResourceAddNewStateBG(MovementManager* mm,
									MovementManagedResource* mmr,
									MovementManagedResourceState* mmrs,
									const char* reason)
{
	MovementThreadData* td = MM_THREADDATA_BG(mm);

	mmrs->bg.flagsMutable.createdLocally = 1;
	mmr->bg.flagsMutable.hadLocalState = 1;

	mmResourceInsertStateBG(mm,
							mmr,
							mmrs,
							reason);

	// Send to FG.

	eaPush( &mmr->toFG[MM_BG_SLOT].statesMutable,
			mmrs);

	mmResourceMarkAsUpdatedToFG(mm, td, mmr);
}

static void mmResourceHandleNewStatesFromFG(MovementManager* mm,
											MovementManagedResource* mmr,
											MovementManagedResourceToBG* toBG)
{
	EARRAY_CONST_FOREACH_BEGIN(toBG->states, j, jsize);
	{
		MovementManagedResourceState* mmrs = toBG->states[j];
		
		// Validate new state.

		mmLogResource(	mm,
						mmr,
						"Adding state from FG %p:%d:%d",
						mmrs,
						mmrs->mmrsType,
						mmrs->spc);
		
		// Add state.
		
		mmResourceInsertStateBG(mm,
								mmr,
								mmrs,
								__FUNCTION__);
	}
	EARRAY_FOREACH_END;
	
	// Verify and cleanup.

	eaDestroy(&toBG->statesMutable);
}

static void mmResourceHandleRemovedStatesFromFG(MovementManager* mm,
												MovementThreadData* td,
												MovementManagedResource* mmr,
												MovementManagedResourceToBG* toBG)
{
	EARRAY_CONST_FOREACH_BEGIN(toBG->removeStates, j, jsize);
	{
		MovementManagedResourceState* mmrs = toBG->removeStates[j];
		
		if(eaFindAndRemove(&mmr->bg.statesMutable, mmrs) < 0){
			assert(0);
		}
		
		mmResourceStateSetNotInListBG(mmrs);
		
		mmrsTrackFlags(	mmrs,
						MMRS_TRACK_ALLOCED,
						MMRS_TRACK_FREED | MMRS_TRACK_SENT_FINISHED_TO_FG,
						MMRS_TRACK_SENT_FINISHED_TO_FG,
						0);

		ASSERT_FALSE_AND_SET(mmrs->bg.flagsMutable.sentFinishedStateToFG);
		
		eaPush(	&td->toFG.finishedResourceStatesMutable,
				mmrs);
				
		MM_TD_SET_HAS_TOFG(mm, td);
		td->toFG.flagsMutable.mmrHasUpdate = 1;
		
		mmResourceSetNeedsSetStateBG(mm, mmr, __FUNCTION__);
	}
	EARRAY_FOREACH_END;
	
	mmResourceDebugRemoveRemoveStatesToBG(mmr, toBG->removeStates);

	eaDestroy(&toBG->removeStatesMutable);
}

static void mmResourceHandleServerCreateFromFG(	MovementManager* mm,
												MovementManagedResource* mmr,
												MovementManagedResourceToBG* toBG)
{
	U32 handleOld = mmr->bg.handle;
	
	mmLogResource(	mm,
					mmr,
					"Received handle updated from FG (%d -> %d)",
					mmr->bg.handle,
					toBG->newHandle);

	mmr->bg.handle = toBG->newHandle;
	
	mmr->bg.flagsMutable.hasNetState = 1;

	if(	mmr->mr &&
		!mmr->bg.flags.mrDestroyed)
	{
		MovementRequesterMsgPrivateData pd;
		
		mmRequesterMsgInitBG(	&pd,
								NULL,
								mmr->mr,
								MR_MSG_BG_MMR_HANDLE_CHANGED);
		
		pd.msg.in.bg.mmrHandleChanged.handleOld = handleOld;
		pd.msg.in.bg.mmrHandleChanged.handleNew = toBG->newHandle;
		
		mmRequesterMsgSend(&pd);
	}
}

static void mmResourceCreateHelperBG(	MovementManagedResource** mmrOut,
										U32* handleOut,
										MovementManager* mm,
										MovementRequester* mr,
										MovementManagedResourceClass* mmrc,
										const void* constant,
										const void* constantNP,
										const void* state)
{
	MovementThreadData*				td = MM_THREADDATA_BG(mm);
	MovementManagedResource*		mmr;
	MovementManagedResourceState*	mmrs;
	U32								handle = 0;

	if(handleOut){
		mmResourceGetNewHandle(mm, &handle);
		*handleOut = handle;
	}		

	mmResourceAlloc(&mmr);

	mmr->mmrc = mmrc;
	
	mmResourceConstantAlloc(mm,
							mmrc,
							&mmr->userStruct.constant,
							constantNP ?
								&mmr->userStruct.constantNP :
								NULL);

	mmStructCopy(	mmrc->pti.constant,
					constant,
					mmr->userStruct.constant);

	if(constantNP){
		mmStructCopy(	mmrc->pti.constantNP,
						constantNP,
						mmr->userStruct.constantNP);
	}

	// Create the CREATED state.

	mmResourceStateCreate(	mm,
							mmr,
							&mmrs,
							MMRST_CREATED,
							state,
							mgState.bg.pc.server.cur);
							
	mmResourceAddNewStateBG(mm, mmr, mmrs, __FUNCTION__);

	mmLogResource(	mm,
					mmr,
					"Created state %p:%d:%d",
					mmrs,
					mmrs->mmrsType,
					mmrs->spc);

	mmr->mr = mr;
	mmr->handle = handle;
	mmr->bg.handle = handle;

	if(	handle ||
		!entIsServer())
	{
		eaPush(	&mm->bg.resourcesMutable,
				mmr);
		
		mmr->bg.flagsMutable.inList = 1;
	}

	mmRareLockEnter(mm);
	{
		eaPush(&mm->allResourcesMutable, mmr);
	}
	mmRareLockLeave(mm);

	*mmrOut = mmr;
}

static S32 mmResourceHasValidStatesBG(MovementManagedResource* mmr){
	EARRAY_CONST_FOREACH_BEGIN(mmr->bg.states, i, isize);
	{
		MovementManagedResourceState* mmrs = mmr->bg.states[i];
		
		if(!mmrs->bg.flags.sentRemoveRequestToFG){
			return 1;
		}
	}
	EARRAY_FOREACH_END;

	return 0;
}

static S32 mmResourceHasNewLocalStatesBG(MovementManagedResource* mmr){
	EARRAY_CONST_FOREACH_BEGIN(mmr->bg.states, i, isize);
	{
		MovementManagedResourceState* mmrs = mmr->bg.states[i];
		
		if(	mmrs->bg.flags.createdLocally &&
			!mmrs->bg.flags.sentRemoveRequestToFG)
		{
			return 1;
		}
	}
	EARRAY_FOREACH_END;
	
	return 0;
}

static S32 mmFindPreviousResourceBG(U32* handleOut,
									MovementManager* mm,
									MovementRequester* mr,
									MovementManagedResourceClass* mmrc,
									const void* constant,
									const void* constantNP,
									const void* state)
{
	EARRAY_CONST_FOREACH_BEGIN(mm->bg.resources, i, size);
	{
		MovementManagedResource* mmr = mm->bg.resources[i];

		if(	mmr->mmrc == mmrc &&
			mmr->mr == mr &&
			!!handleOut == !!mmr->bg.handle &&
			(	!mmr->bg.handle &&
				!mmResourceHasValidStatesBG(mmr)
				||
				mmr->bg.handle &&
				!mmResourceHasNewLocalStatesBG(mmr)) &&
			!StructCompare(	mmrc->pti.constant,
							mmr->userStruct.constant,
							constant, 0, 0, 0) &&
			!!constantNP == !!mmr->userStruct.constantNP &&
			(	!constantNP ||
				!StructCompare(	mmrc->pti.constantNP,
								mmr->userStruct.constantNP,
								constantNP, 0, 0, 0))
			)
		{
			MovementThreadData*				td = MM_THREADDATA_BG(mm);
			MovementManagedResourceState*	mmrs;

			mmResourceMarkAsUpdatedToFG(mm, td, mmr);
			
			mmResourceStateCreate(	mm,
									mmr,
									&mmrs,
									MMRST_CREATED,
									state,
									mgState.bg.pc.server.cur);
									
			mmrs->bg.flagsMutable.createdLocally = 1;
			mmr->bg.flagsMutable.hadLocalState = 1;
			
			mmResourceInsertStateBG(mm,
									mmr,
									mmrs,
									__FUNCTION__);

			eaPush( &mmr->toFG[MM_BG_SLOT].statesMutable,
					mmrs);

			mmLogResource(	mm,
							mmr,
							"Re-created previous resource %p:%d:%d",
							mmrs,
							mmrs->mmrsType,
							mmrs->spc);

			if(handleOut){
				*handleOut = mmr->bg.handle;
			}

			return 1;
		}
	}
	EARRAY_FOREACH_END;

	return 0;
}

static void mmLogCreateResourceFailedBG(	MovementRequester* mr,
											const char* prefix,
											MovementManagedResource* mmr,
											MovementManagedResourceClass* mmrc,
											const void* constant,
											const void* constantNP)
{
	char* estr = NULL;

	estrStackCreateSize(&estr, 100);

	mmResourceGetConstantDebugString(	mr->mm,
										mmr,
										mmrc,
										constant,
										constantNP,
										&estr);

	mrLog(	mr,
			NULL,
			"%s resource: \"%s\"",
			prefix,
			estr);

	estrDestroy(&estr);
}

S32 mrmResourceCreateBG(const MovementRequesterMsg* msg,
						U32* handleOut,
						U32 resourceID,
						const void* constant,
						const void* constantNP,
						const void* state)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;
	MovementManagedResourceClass*		mmrc;
	MovementManagedResource*			mmr;

	PERFINFO_AUTO_START_FUNC();

	if(beaconIsBeaconizer()){
		PERFINFO_AUTO_STOP();
		return 0;
	}

	if(!mmGetManagedResourceClassByID(resourceID, &mmrc)){
		PERFINFO_AUTO_STOP();
		return 0;
	}

	if(	!pd ||
		!constant ||
		SAFE_DEREF(handleOut) ||
		(	pd->msgType != MR_MSG_BG_CREATE_DETAILS
			&&
			(	pd->msgType != MR_MSG_BG_CREATE_OUTPUT ||
			pd->in.bg.createOutput.dataClassBit != 1U << mmrc->approvingMDC)))
	{
		PERFINFO_AUTO_STOP();
		return 0;
	}

	mm = pd->mm;

	if(	mgState.bg.flags.isRepredicting &&
		mmFindPreviousResourceBG(	handleOut,
									mm,
									pd->mr,
									mmrc,
									constant,
									constantNP,
									state))
	{
		PERFINFO_AUTO_STOP();
		return 1;
	}

	if(pd->msgType == MR_MSG_BG_CREATE_DETAILS){
		// Detail resource, needs approval from data class owner.

		if(!mmSendMessageDetailResourceRequested(	pd->mr,
													mmrc,
													constant,
													constantNP))
		{
			if(MMLOG_IS_ENABLED(mm)){
				mmLogCreateResourceFailedBG(pd->mr,
											"Failed to add detail",
											NULL,
											mmrc,
											constant,
											constantNP);
			}

			PERFINFO_AUTO_STOP();
			return 0;
		}
	}

	mmResourceCreateHelperBG(	&mmr,
								handleOut,
								mm,
								pd->mr,
								mmrc,
								constant,
								constantNP,
								state);

	mmLogResource(	mm,
					mmr,
					"%s",
					pd->msgType == MR_MSG_BG_CREATE_DETAILS ?
						"Added detail resource" :
						"Added normal resource");

	PERFINFO_AUTO_STOP();
	
	return 1;
}

static S32 mmResourceGetLastStateBG(MovementManagedResource* mmr,
									MovementManagedResourceState** mmrsOut)
{
	EARRAY_FOREACH_REVERSE_BEGIN(mmr->bg.states, i);
	{
		MovementManagedResourceState* mmrs = mmr->bg.states[i];
		if(!mmrs->bg.flags.sentRemoveRequestToFG){
			*mmrsOut = mmrs;
			return 1;
		}
	}
	EARRAY_FOREACH_END;
	
	return 0;
}

static S32 mmrWasCreatedAndNotDestroyedBG(MovementManagedResource* mmr){
	MovementManagedResourceState* mmrs;

	return	mmResourceGetLastStateBG(mmr, &mmrs) &&
		(mmrs->mmrsType != MMRST_DESTROYED &&
		mmrs->mmrsType != MMRST_CLEARED);
}

static S32 mmResourceAddStateDestroyedOrClearedBG(	MovementManager* mm,
													MovementManagedResource* mmr,
													S32 bClear,
													const char* reason)
{
	MovementManagedResourceState* mmrs;
	
	if(	!mmr->handle ||
		!mmrWasCreatedAndNotDestroyedBG(mmr))
	{
		return 0;
	}
	
	mmLogResource(mm, mmr, "%s", reason);

	mmResourceStateCreate(	mm,
							mmr,
							&mmrs,
							((!bClear) ? MMRST_DESTROYED : MMRST_CLEARED),
							NULL,
							mgState.bg.pc.server.cur);
	
	mmResourceAddNewStateBG(mm, mmr, mmrs, __FUNCTION__);
	
	return 1;
}

void mmDestroyRequesterResourcesBG(	MovementManager* mm,
									MovementRequester* mr)
{
	EARRAY_CONST_FOREACH_BEGIN(mm->bg.resources, i, size);
	{
		MovementManagedResource* mmr = mm->bg.resources[i];

		if(mmr->mr != mr){
			continue;
		}

		mmr->bg.flagsMutable.mrDestroyed = 1;

		if(	!mmr->bg.flags.noAutoDestroy &&
			mmr->handle)
		{
			mmResourceAddStateDestroyedOrClearedBG(mm, mmr, false, "Destroying requester's resources");
		}
	}
	EARRAY_FOREACH_END;
}

static S32 mmResourceDestroyOrClearByHandleBG(	MovementManager* mm,
												MovementRequester* mr,
												U32 handle,
												MovementManagedResourceClass* mmrc,
												S32 bClear)
{
	MovementThreadData* td = MM_THREADDATA_BG(mm);

	EARRAY_CONST_FOREACH_BEGIN(mm->bg.resources, i, size);
	{
		MovementManagedResource* mmr = mm->bg.resources[i];

		if(	mmr->mr == mr &&
			mmr->bg.handle == handle &&
			mmr->mmrc == mmrc)
		{
			return mmResourceAddStateDestroyedOrClearedBG(	mm,
															mmr,
															bClear,
															(!bClear ? "BG Destroying resource by handle" : 
																	   "BG Clearing resource by handle"));
		}
	}
	EARRAY_FOREACH_END;

	return 0;
}

__forceinline static S32 mrmResourceDestroyOrClearBG(	const MovementRequesterMsg* msg,
														U32 resourceID,
														U32* handleInOut,
														S32 bClear)
{
	U32									handle = SAFE_DEREF(handleInOut);
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementRequester*					mr;
	MovementManager*					mm;
	MovementManagedResourceClass*		mmrc;
	S32									retVal;

	PERFINFO_AUTO_START_FUNC();

	if(	!pd ||
		!handle ||
		pd->msgType <= MR_MSG_BG_LOW ||
		pd->msgType >= MR_MSG_BG_HIGH ||
		!mmGetManagedResourceClassByID(resourceID, &mmrc))
	{
		PERFINFO_AUTO_STOP();
		return 0;
	}

	mr = pd->mr;
	mm = pd->mm;

	*handleInOut = 0;

	retVal = mmResourceDestroyOrClearByHandleBG(mm, mr, handle, mmrc, bClear);

	PERFINFO_AUTO_STOP();

	return retVal;
}

S32 mrmResourceDestroyBG(	const MovementRequesterMsg* msg,
							U32 resourceID,
							U32* handleInOut)
{
	return mrmResourceDestroyOrClearBG(msg, resourceID, handleInOut, false);
}

S32 mrmResourceClearBG(	const MovementRequesterMsg* msg,
						U32 resourceID,
						U32* handleInOut)
{
	return mrmResourceDestroyOrClearBG(msg, resourceID, handleInOut, true);
}

void mmResourcesRemoveLocalStatesBG(MovementManager* mm,
									MovementThreadData* td)
{
	EARRAY_CONST_FOREACH_BEGIN(mm->bg.resources, i, isize);
	{
		MovementManagedResource* mmr = mm->bg.resources[i];
		
		EARRAY_CONST_FOREACH_BEGIN(mmr->bg.states, j, jsize);
		{
			MovementManagedResourceState* mmrs = mmr->bg.states[j];
			
			if(	mmrs->bg.flags.createdLocally &&
				FALSE_THEN_SET(mmrs->bg.flagsMutable.sentRemoveRequestToFG))
			{
				mmrsTrackFlags(	mmrs,
								MMRS_TRACK_ALLOCED,
								MMRS_TRACK_FREED | MMRS_TRACK_SENT_REMOVE_REQUEST_TO_FG,
								MMRS_TRACK_SENT_REMOVE_REQUEST_TO_FG,
								0);
								
				mmLog(	mm,
						NULL,
						"[Resource] Sending remove for local resource state 0x%8.8p:0x%8.8p",
						mmr,
						mmrs);

				mmResourceMarkAsUpdatedToFG(mm, td, mmr);
				
				eaPush(	&mmr->toFG[MM_BG_SLOT].removeStatesMutable,
						mmrs);
			}
		}
		EARRAY_FOREACH_END;
	}
	EARRAY_FOREACH_END;
}

S32 mrmResourceCreateStateBG(	const MovementRequesterMsg* msg,
								U32 resourceID,
								U32 mrHandle,
								const void* state)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;
	MovementRequester*					mr;
	MovementManagedResourceClass*		mmrc;

	PERFINFO_AUTO_START_FUNC();

	if(!mmGetManagedResourceClassByID(resourceID, &mmrc)){
		PERFINFO_AUTO_STOP();
		return 0;
	}

	if(	!pd
		||
		!mrHandle
		||
		(	pd->msgType != MR_MSG_BG_CREATE_DETAILS
			&&
			(	pd->msgType != MR_MSG_BG_CREATE_OUTPUT ||
			pd->in.bg.createOutput.dataClassBit != 1U << mmrc->approvingMDC)))
	{
		PERFINFO_AUTO_STOP();
		return 0;
	}

	mm = pd->mm;
	mr = pd->mr;
	
	EARRAY_CONST_FOREACH_BEGIN(mm->bg.resources, i, size);
	{
		MovementManagedResource*		mmr = mm->bg.resources[i];
		MovementManagedResourceState*	mmrs;

		if(	mmr->mr != mr ||
			mmr->bg.handle != mrHandle ||
			mmr->mmrc != mmrc)
		{
			continue;
		}

		if(!mmrWasCreatedAndNotDestroyedBG(mmr)){
			break;
		}

		mmLogResource(mm, mmr, "BG creating state change");

		mmResourceStateCreate(	mm,
								mmr,
								&mmrs,
								MMRST_STATE_CHANGE,
								state,
								mgState.bg.pc.server.cur);
								
		mmResourceAddNewStateBG(mm, mmr, mmrs, __FUNCTION__);

		PERFINFO_AUTO_STOP();
		return 1;
	}
	EARRAY_FOREACH_END;
	
	PERFINFO_AUTO_STOP();
	return 0;
}

S32 mrmResourceSetNoAutoDestroyBG(	const MovementRequesterMsg* msg,
									U32 resourceID,
									U32 mrHandle)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementRequester*					mr;
	MovementManager*					mm;
	MovementManagedResourceClass*		mmrc;
	S32									retVal = 0;

	PERFINFO_AUTO_START_FUNC();

	if(	mgState.flags.isServer ||
		!pd ||
		!mrHandle ||
		pd->msgType <= MR_MSG_BG_LOW ||
		pd->msgType >= MR_MSG_BG_HIGH ||
		!mmGetManagedResourceClassByID(resourceID, &mmrc))
	{
		PERFINFO_AUTO_STOP();
		return 0;
	}

	mr = pd->mr;
	mm = pd->mm;

	EARRAY_CONST_FOREACH_BEGIN(mm->bg.resources, i, size);
	{
		MovementManagedResource* mmr = mm->bg.resources[i];

		if(	mmr->mr == mr &&
			mmr->bg.handle == mrHandle &&
			mmr->mmrc == mmrc)
		{
			mmr->bg.flagsMutable.noAutoDestroy = 1;
		}
	}
	EARRAY_FOREACH_END;
	
	PERFINFO_AUTO_STOP();

	return retVal;
}

static void mmrGetCurrentStatesBG(	MovementManagedResource* mmr,
									MovementManagedResourceState** mmrsNetOut,
									MovementManagedResourceState** mmrsLocalOut)
{
	S32 foundNet = 0;
	S32 foundLocal = 0;

	// Find the floor state.

	EARRAY_FOREACH_REVERSE_BEGIN(mmr->bg.states, i);
	{
		const MovementManagedResourceState* mmrs = mmr->bg.states[i];

		if(	!foundNet &&
			mmr->bg.flags.hasNetState &&
			mmrs->bg.flags.isNetState &&
			subS32(	mgState.bg.pc.server.curView,
					mmrs->spc) >= 0)
		{
			assert(	mmrs->mmrsType >= 0 &&
					mmrs->mmrsType < MMRST_COUNT);

			*mmrsNetOut = mmr->bg.states[i];

			foundNet = 1;

			if(	foundLocal ||
				!mmr->bg.flags.hadLocalState)
			{
				break;
			}
		}

		if(	!foundLocal &&
			mmr->bg.flags.hadLocalState &&
			subS32(	mgState.bg.pc.server.cur,
					mmrs->spc) >= 0)
		{
			assert(	mmrs->mmrsType >= 0 &&
					mmrs->mmrsType < MMRST_COUNT);

			*mmrsLocalOut = mmr->bg.states[i];

			foundLocal = 1;

			if(	foundNet ||
				!mmr->bg.flags.hasNetState)
			{
				break;
			}
		}
	}
	EARRAY_FOREACH_END;
}

static void mmLogResourceChangeBG(	MovementManager* mm,
									MovementManagedResource* mmr,
									S32* logCountInOut,
									const char* tag,
									U32 color)
{
	const F32	baseHeight = 8.f;
	Vec3		pos;

	copyVec3(mm->bg.pos, pos);
	
	pos[1] += baseHeight + 2 * (*logCountInOut)++;

	mmLogPoint(	mm,
				NULL,
				tag,
				color,
				pos);
}

static void mmResourceSendMsgDestroyedBG(	MovementManager* mm,
											MovementManagedResource* mmr,
											const char* reason)
{
	MovementManagedResourceMsgPrivateData pd;

	if(!mmr->userStruct.activatedBG){
		return;
	}

	mmLogResource(mm, mmr, "Destroying (%s)", reason);

	assert(mmr->userStruct.activatedBG);

	mmResourceMsgInitBG(&pd,
						NULL,
						mm,
						mmr,
						MMR_MSG_BG_DESTROYED);

	pd.msg.in.activatedStruct = mmr->userStruct.activatedBG;
	pd.msg.in.handle = mmr->handle;

	mmResourceMsgSend(&pd);

	mmStructDestroy(mmr->mmrc->pti.activatedBG,
					mmr->userStruct.activatedBG,
					mm);
}

static void mmResourceSendMsgSetStateBG(MovementManager* mm,
										MovementManagedResource* mmr,
										MovementManagedResourceState* mmrsNet,
										MovementManagedResourceState* mmrsLocal)
{
	MovementManagedResourceMsgPrivateData	pd;
	MovementManagedResourceMsgOut			out;

	mmLogResource(	mm,
					mmr,
					"Setting state for %s resource",
					mmr->mmrc->name);

	if(mmr->handle){
		mmStructAllocIfNull(mmr->mmrc->pti.activatedBG,
							mmr->userStruct.activatedBG,
							mm);

		assert(mmr->userStruct.activatedBG);
	}

	mmResourceMsgInitBG(&pd,
						&out,
						mm,
						mmr,
						MMR_MSG_BG_SET_STATE);

	pd.msg.in.activatedStruct = mmr->userStruct.activatedBG;
	pd.msg.in.handle = mmr->handle;
	pd.msg.out = &out;

	mmResourceMsgSend(&pd);

	if(	mmr->handle &&
		out.bg.setState.flags.needsRetry)
	{
		mmResourceSetNeedsSetStateBG(mm, mmr, __FUNCTION__);
	}
}

static void mmResourceSetStateBG(	MovementManager* mm,
									MovementThreadData* td,
									MovementManagedResource* mmr,
									U32* logCountInOut)
{
	MovementManagedResourceState*	mmrsNet = NULL;
	MovementManagedResourceState*	mmrsLocal = NULL;
	const char*						shouldBeDestroyedReason = NULL;
	MovementManagedResourceState*	mmrsTail = eaTail(&mmr->bg.states);

	mmrGetCurrentStatesBG(	mmr,
							&mmrsNet,
							&mmrsLocal);

	if(mmrsNet){
		// Check the net state first.

		switch(mmrsNet->mmrsType){
			xcase MMRST_DESTROYED:{
				shouldBeDestroyedReason = "Net state says destroyed";
			}
			xcase MMRST_CLEARED:{
				shouldBeDestroyedReason = "Net state says cleared";
			}
		}
	}

	if(mmrsLocal){
		// If there's a local state then use that.

		switch(mmrsLocal->mmrsType){
			xcase MMRST_DESTROYED:{
				shouldBeDestroyedReason = "Local state says destroyed";
			}
			xcase MMRST_CLEARED:{
				shouldBeDestroyedReason = "Local state says cleared";
			}
		}
	}
	
	if(!mgState.flags.isServer){
		if(	!mmrsNet ||
			mmrsNet != mmrsTail)
		{
			mmResourceSetNeedsSetStateBG(mm, mmr, __FUNCTION__);
		}
	}
	else if(!mmrsLocal ||
			mmrsLocal != mmrsTail)
	{
		mmResourceSetNeedsSetStateBG(mm, mmr, __FUNCTION__);
	}

	if(shouldBeDestroyedReason){
		mmResourceSendMsgDestroyedBG(mm, mmr, "Disabled");
	}else{
		mmResourceSendMsgSetStateBG(mm,
									mmr,
									mmrsNet,
									mmrsLocal);
	}
}

void mmResourcesSetStateBG(	MovementManager* mm,
							MovementThreadData* td)
{
	U32 logCount = 0;
	
	PERFINFO_AUTO_START_FUNC();
	
	ASSERT_TRUE_AND_RESET(mm->bg.flagsMutable.mmrNeedsSetState);

	mmLog(mm, NULL, "[rc.setState] Setting BG resource states.");

	EARRAY_CONST_FOREACH_BEGIN(mm->bg.resources, i, isize);
	{
		MovementManagedResource* mmr = mm->bg.resources[i];
		
		if(!TRUE_THEN_RESET(mmr->bg.flagsMutable.needsSetState)){
			continue;
		}
		
		PERFINFO_AUTO_START_STATIC(	mmr->mmrc->perfInfo[MMRC_PT_SET_STATE].name,
									&mmr->mmrc->perfInfo[MMRC_PT_SET_STATE].perfInfo,
									1);
		{
			mmResourceSetStateBG(	mm,
									td,
									mmr,
									&logCount);
		}
		PERFINFO_AUTO_STOP();
	}
	EARRAY_FOREACH_END;
	
	// Decrement for mmrNeedsSetState being cleared above.
	
	mmPastStateListCountDecBG(mm);
	mmVerifyPastStateCountBG(mm);

	PERFINFO_AUTO_STOP();// FUNC
}

void mmDeactivateAllResourcesBG(MovementManager* mm){
	EARRAY_CONST_FOREACH_BEGIN(mm->bg.resources, i, isize);
	{
		MovementManagedResource* mmr = mm->bg.resources[i];

		if(mmr->bg.handle){
			mmResourceSendMsgDestroyedBG(mm, mmr, "All resources deactivated");
		}
	}
	EARRAY_FOREACH_END;
}

static void mmResourceHandleDestroyedFromFG(MovementManager* mm,
											MovementThreadData* td,
											MovementManagedResource* mmr)
{
	if(mmr->bg.handle){
		mmResourceSendMsgDestroyedBG(mm, mmr, "Destroyed");
	}
	
	mmResourceMarkAsUpdatedToFG(mm, td, mmr);

	mmr->toFG[MM_BG_SLOT].flagsMutable.cleared = TRUE_THEN_RESET(mmr->bg.flagsMutable.clearedFromFG);
	mmr->toFG[MM_BG_SLOT].flagsMutable.destroyed = 1;
	
	eaDestroy(&mmr->toFG[MM_BG_SLOT].statesMutable);
	eaDestroy(&mmr->toFG[MM_BG_SLOT].removeStatesMutable);
	
	// Release all the states.
	
	EARRAY_CONST_FOREACH_BEGIN(mmr->bg.states, j, jsize);
	{
		MovementManagedResourceState* mmrs = mmr->bg.states[j];
		
		mmResourceStateSetNotInListBG(mmrs);

		eaPush(	&td->toFG.finishedResourceStatesMutable,
				mmrs);
	}
	EARRAY_FOREACH_END;
	
	eaDestroy(&mmr->bg.statesMutable);

	if(eaFindAndRemove(&mm->bg.resourcesMutable, mmr) < 0){
		assert(0);
	}

	if(eaFind(&mm->bg.resources, mmr) >= 0){
		assert(0);
	}

	mmr->bg.flagsMutable.inList = 0;

	mmLogResource(mm, mmr, "BG Destroying resource by toBG");
}

void mmResourcesHandleDestroyedFromFG(	MovementManager* mm,
										MovementThreadData* td)
{
	EARRAY_CONST_FOREACH_BEGIN(mm->bg.resources, i, isize);
	{
		MovementManagedResource* mmr = mm->bg.resources[i];
		
		if(TRUE_THEN_RESET(mmr->bg.flagsMutable.destroyedFromFG)){
			mmResourceHandleDestroyedFromFG(mm, td, mmr);

			i--;
			isize--;
		}
	}
	EARRAY_FOREACH_END;
}

void mmHandleResourceUpdatesFromFG(	MovementManager* mm,
									MovementThreadData* td)
{
	S32 mmrNeedsSetStatePrev;

	PERFINFO_AUTO_START_FUNC();
	
	mmLog(	mm,
			NULL,
			"[rc.fromFG] Getting %d updated resources from FG.",
			eaSize(&td->toBG.updatedResources));
			
	mmrNeedsSetStatePrev = mm->bg.flags.mmrNeedsSetState;
			
	EARRAY_CONST_FOREACH_BEGIN(td->toBG.updatedResources, i, isize);
	{
		MovementManagedResource*		mmr = td->toBG.updatedResources[i];
		MovementManagedResourceToBG*	toBG = MMR_TOBG_BG(mmr);

		ASSERT_TRUE_AND_RESET(toBG->flagsMutable.updated);

		mmLogResource(	mm,
						mmr,
						"Updated something from FG");
		
		// Make sure I'm in the BG list.

		if(FALSE_THEN_SET(mmr->bg.flagsMutable.inList)){
			assert(eaFind(&mm->bg.resources, mmr) < 0);

			eaPush(	&mm->bg.resourcesMutable,
					mmr);
		}

		// Check for state updates.

		if(eaSize(&toBG->states)){
			mmResourceHandleNewStatesFromFG(mm, mmr, toBG);
		}

		// Check for removed resources.

		if(eaSize(&toBG->removeStates)){
			mmResourceHandleRemovedStatesFromFG(mm, td, mmr, toBG);
		}

		// Check for a received server create.

		if(TRUE_THEN_RESET(toBG->flagsMutable.receivedServerCreate)){
			mmResourceHandleServerCreateFromFG(mm, mmr, toBG);
		}

		// Check if the FG cleared me.
		
		if(TRUE_THEN_RESET(toBG->flagsMutable.cleared)){
			ASSERT_FALSE_AND_SET(mmr->bg.flagsMutable.clearedFromFG);
		}

		// Check if the FG destroyed me.

		if(TRUE_THEN_RESET(toBG->flagsMutable.destroyed)){
			ASSERT_FALSE_AND_SET(mmr->bg.flagsMutable.destroyedFromFG);
			
			if(FALSE_THEN_SET(mm->bg.flagsMutable.mmrIsDestroyedFromFG)){
				if(!mm->bg.flags.hasPostStepDestroy){
					mmAddToHasPostStepDestroyListBG(mm);
				}
			}else{
				assert(mm->bg.flags.hasPostStepDestroy);
			}
		}
	}
	EARRAY_FOREACH_END;

	if(eaSize(&td->toBG.updatedResources) >= 10){
		eaDestroy(&td->toBG.updatedResourcesMutable);
	}else{
		eaSetSize(&td->toBG.updatedResourcesMutable, 0);
	}
	
	// Check if mm needs to be put in the setPastState list.
	
	if(	!mmrNeedsSetStatePrev &&
		mm->bg.flags.mmrNeedsSetState)
	{
		
	}
	
	mmLogResource(mm, NULL, "Resources after updates from FG");
	
	PERFINFO_AUTO_STOP();
}

void mmResourcesSendMsgBodiesDestroyedBG(MovementManager* mm){
	EARRAY_CONST_FOREACH_BEGIN(mm->bg.resources, i, isize);
	{
		MovementManagedResource*				mmr = mm->bg.resources[i];
		MovementManagedResourceMsgPrivateData	pd;

		if(!mmr->userStruct.activatedBG){
			continue;
		}

		mmResourceMsgInitBG(&pd,
							NULL,
							mm,
							mmr,
							MMR_MSG_BG_ALL_BODIES_DESTROYED);

		pd.msg.in.handle = mmr->handle;
		pd.msg.in.activatedStruct = mmr->userStruct.activatedBG;
		
		mmResourceMsgSend(&pd);
	}
	EARRAY_FOREACH_END;
}

