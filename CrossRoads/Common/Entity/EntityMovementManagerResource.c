/***************************************************************************
*     Copyright (c) 2005-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "EntityMovementManagerPrivate.h"
#include "MemoryPool.h"
#include "structNet.h"
#include "net/net.h"
#include "dynSkeleton.h"

#if GAMECLIENT
	#include "gclDemo.h"
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Physics););

#define PRINT_ALLOCATION_COUNTS 0

static S32 allocedConstants;
static S32 resourceCount;

#if PRINT_ALLOCATION_COUNTS
	static S32 allocedActivated;
#endif

static void mmResourceStateSetInListFG(MovementManagedResourceState* mmrs){
	mmrsTrackFlags(	mmrs,
					MMRS_TRACK_ALLOCED,
					MMRS_TRACK_IN_FG,
					MMRS_TRACK_IN_FG,
					0);

	ASSERT_FALSE_AND_SET(mmrs->fg.flagsMutable.inList);
}

static void mmResourceStateSetNotInListFG(MovementManagedResourceState* mmrs){
	mmrsTrackFlags(	mmrs,
					MMRS_TRACK_ALLOCED | MMRS_TRACK_IN_FG,
					0,
					0,
					MMRS_TRACK_IN_FG);

	ASSERT_TRUE_AND_RESET(mmrs->fg.flagsMutable.inList);
}

void mmResourceConstantAlloc(	MovementManager* mm,
								MovementManagedResourceClass* mmrc,
								void** constantOut,
								void** constantNPOut)
{
	if(constantOut){
		*constantOut = NULL;
		mmStructAllocIfNull(mmrc->pti.constant,
							*constantOut,
							mm);
	}
	
	if(constantNPOut){
		*constantNPOut = NULL;
		mmStructAllocIfNull(mmrc->pti.constantNP,
							*constantNPOut,
							mm);
	}
	
	#if PRINT_ALLOCATION_COUNTS
	{
		mmRequesterLockAcquire();
			allocedConstants += !!constantOut + !!constantNPOut;
			printf("rcAllocedConstantCount = %u\n", allocedConstants);
		mmRequesterLockRelease();
	}
	#endif
}

static void mmResourceConstantFree(	MovementManager* mm,
									MovementManagedResourceClass* mmrc,
									void** constantInOut,
									void** constantNPInOut)
{
	U32 count = 0;
	
	if(SAFE_DEREF(constantInOut)){
		count++;
		
		mmStructDestroy(mmrc->pti.constant,
						*constantInOut,
						mm);
	}

	if(SAFE_DEREF(constantNPInOut)){
		count++;

		mmStructDestroy(mmrc->pti.constantNP,
						*constantNPInOut,
						mm);
	}

	#if PRINT_ALLOCATION_COUNTS
	{
		mmRequesterLockAcquire();
			allocedConstants -= count;
			printf("rcAllocedConstantCount = %u\n", allocedConstants);
		mmRequesterLockRelease();
	}
	#endif
}

#if PRINT_ALLOCATION_COUNTS
	static CRITICAL_SECTION csResourceStateCount;
	static S32				resourceStateCount;
#endif

void mmResourceStateCreate(	MovementManager* mm,
							MovementManagedResource* mmr,
							MovementManagedResourceState** mmrsOut,
							MovementManagedResourceStateType mmrsType,
							const void* state,
							U32 spc)
{
	MovementManagedResourceState* mmrs;
	
	#if PRINT_ALLOCATION_COUNTS
	{
		ATOMIC_INIT_BEGIN;
		{
			InitializeCriticalSection(&csResourceStateCount);
		}
		ATOMIC_INIT_END;
		
		EnterCriticalSection(&csResourceStateCount);
		resourceStateCount++;
		printf("rcStateCount = %u\n", resourceStateCount);
		LeaveCriticalSection(&csResourceStateCount);
	}
	#endif
	
	mmrs = callocStruct(MovementManagedResourceState);
	
	mmrsTrackFlags(mmrs, 0, MMRS_TRACK_ALLOCED, MMRS_TRACK_ALLOCED, ~MMRS_TRACK_ALLOCED);

	mmrs->mmr = mmr;
	mmrs->mmrsType = mmrsType;
	mmrs->spc = spc;
	
	if(state){
		mmStructAllocIfNull(mmr->mmrc->pti.state,
							mmrs->userStruct.state,
							mm);

		mmStructCopy(	mmr->mmrc->pti.state,
						state,
						mmrs->userStruct.state);
	}

	assert(	mmrs->mmrsType >= 0 &&
			mmrs->mmrsType < MMRST_COUNT);

	*mmrsOut = mmrs;
}

static void mmResourceStateDestroy(	MovementManager* mm,
									MovementManagedResourceState* mmrs)
{
	mmrsTrackFlags(	mmrs,
					MMRS_TRACK_ALLOCED,
					MMRS_TRACK_FREED | MMRS_TRACK_IN_BG | MMRS_TRACK_IN_FG,
					MMRS_TRACK_FREED,
					MMRS_TRACK_ALLOCED);

	mmStructDestroy(mmrs->mmr->mmrc->pti.state,
					mmrs->userStruct.state,
					mm);

	SAFE_FREE(mmrs);

	#if PRINT_ALLOCATION_COUNTS
	{
		EnterCriticalSection(&csResourceStateCount);
		resourceStateCount--;
		printf("rcStateCount = %u\n", resourceStateCount);
		LeaveCriticalSection(&csResourceStateCount);
	}
	#endif
}

MP_DEFINE(MovementManagedResource);

void mmResourceAlloc(MovementManagedResource** mmrOut){
	if(!mmrOut){
		return;
	}

	mmRequesterLockAcquire();
	{
		MP_CREATE(MovementManagedResource, 50);

		*mmrOut = MP_ALLOC(MovementManagedResource);
		
		#if PRINT_ALLOCATION_COUNTS
		{
			resourceCount++;
			printf("rcCount = %u\n", resourceCount);
		}
		#endif
	}
	mmRequesterLockRelease();

	//printf("alloced resource 0x%8.8x\n", *mmrOut);
}

static void mmResourceFreeAddStatesToFG(MovementManagedResource* mmr,
										MovementManagedResourceState*** states)
{
	EARRAY_CONST_FOREACH_BEGIN(states[0], i, isize);
	{
		MovementManagedResourceState* mmrs = states[0][i];
		
		if(!mmrs->fg.flags.inList){
			mmResourceStateSetInListFG(mmrs);
			
			#if MM_TRACK_RESOURCE_STATE_FLAGS
			{
				if(eaFind(&mmr->fg.states, mmrs) >= 0){
					assert(0);
				}
			}
			#endif
			
			eaPush(&mmr->fg.statesMutable, mmrs);
		}
	}
	EARRAY_FOREACH_END;

	eaDestroy(states);
}

static void mmResourceFreeFG(	MovementManager* mm,
								MovementManagedResource** mmrInOut)
{
	MovementManagedResource* mmr = SAFE_DEREF(mmrInOut);

	if(!mmr){
		return;
	}
	
	mmrSetAlwaysDrawFG(mm, mmr, 0);
	
	ARRAY_FOREACH_BEGIN(mmr->toFG, i);
	{
		mmResourceFreeAddStatesToFG(mmr, &mmr->toFG[i].statesMutable);
		mmResourceFreeAddStatesToFG(mmr, &mmr->toFG[i].removeStatesMutable);
		mmResourceFreeAddStatesToFG(mmr, &mmr->toBG[i].statesMutable);
		mmResourceDebugRemoveRemoveStatesToBG(mmr, mmr->toBG[i].removeStates);
		mmResourceFreeAddStatesToFG(mmr, &mmr->toBG[i].removeStatesMutable);
	}
	ARRAY_FOREACH_END;
	
	// Clear the inList flag for BG states.
	
	EARRAY_CONST_FOREACH_BEGIN(mmr->bg.states, j, jsize);
	{
		MovementManagedResourceState* mmrs = mmr->bg.states[j];
		
		mmr->bg.statesMutable[j] = NULL;
		
		mmrsTrackFlags(	mmrs,
						MMRS_TRACK_ALLOCED | MMRS_TRACK_IN_BG | MMRS_TRACK_IN_FG,
						MMRS_TRACK_FREED,
						0,
						MMRS_TRACK_IN_BG);
		
		ASSERT_TRUE_AND_RESET(mmrs->bg.flagsMutable.inList);
	}
	EARRAY_FOREACH_END;

	eaDestroy(&mmr->bg.statesMutable);
	
	// Destroy all the FG states (which should be all of them now).

	EARRAY_CONST_FOREACH_BEGIN(mmr->fg.states, j, jsize);
	{
		MovementManagedResourceState* mmrs = mmr->fg.states[j];

		mmrsTrackFlags(	mmrs,
						MMRS_TRACK_ALLOCED | MMRS_TRACK_IN_FG,
						MMRS_TRACK_FREED | MMRS_TRACK_IN_BG,
						0,
						MMRS_TRACK_IN_FG);
		
		ASSERT_TRUE_AND_RESET(mmrs->fg.flagsMutable.inList);

		mmResourceStateDestroy(mm, mmrs);
	}
	EARRAY_FOREACH_END;

	eaDestroy(&mmr->fg.statesMutable);
	
	// Destroy the activated struct.
	
	if(mmr->userStruct.activatedFG){
		mmStructDestroy(mmr->mmrc->pti.activatedFG,
						mmr->userStruct.activatedFG,
						mm);

		#if PRINT_ALLOCATION_COUNTS
		{
			allocedActivated--;
			printf("rcAllocedActivatedCount = %u\n", allocedActivated);
		}
		#endif
	}

	assert(!mmr->fg.flags.inList);
	assert(!mmr->bg.flags.inList);
	assert(!mmr->userStruct.activatedFG);
	assert(!mmr->userStruct.activatedBG);

	mmResourceConstantFree(	mm,
							mmr->mmrc,
							&mmr->userStruct.constant,
							&mmr->userStruct.constantNP);
	
	mmRequesterLockAcquire();
	{
		MP_FREE(MovementManagedResource, *mmrInOut);

		#if PRINT_ALLOCATION_COUNTS
		{
			resourceCount--;
			printf("rcCount = %u\n", resourceCount);
		}
		#endif
	}
	mmRequesterLockRelease();
}

static void mmResourceMsgInitFG(MovementManagedResourceMsgPrivateData* pd,
								MovementManagedResourceMsgOut* out,
								MovementManager* mm,
								MovementManagedResource* mmr,
								MovementManagedResourceMsgType msgType)
{
	mmResourceMsgInit(pd, out, mm, mmr, msgType, MM_FG_SLOT);
}

void mmResourceGetConstantDebugString(	MovementManager* mm,
										MovementManagedResource* mmr,
										MovementManagedResourceClass* mmrc,
										const void* constant,
										const void* constantNP,
										char** estrBufferInOut)
{
	MovementManagedResourceMsgPrivateData pd;

	if(!mmr){
		return;
	}

	mmrc = mmr->mmrc;
	constant = mmr->userStruct.constant;
	constantNP = mmr->userStruct.constantNP;

	estrConcatf(estrBufferInOut,
				"h%u/%u (%s): ",
				mmr->handle,
				mmr->bg.handle,
				mmrc->name);

	estrConcatf(estrBufferInOut, "Constant: ");

	mmResourceMsgInit(	&pd,
						NULL,
						mm,
						NULL,
						MMR_MSG_GET_CONSTANT_DEBUG_STRING,
						mmGetCurrentThreadSlot());

	pd.msg.in.constant = constant;
	pd.msg.in.constantNP = constantNP;

	pd.msg.in.getDebugString.estrBuffer = estrBufferInOut;
	
	mmrc->msgHandler(&pd.msg);
}

static void mmResourceGetStateDebugStringFG(MovementManager* mm,
											MovementManagedResource* mmr,
											char** estrBufferInOut)
{
	MovementManagedResourceMsgPrivateData pd;

	if(!mmr->userStruct.activatedFG){
		return;
	}

	estrConcatf(estrBufferInOut,
				"\nStateFG: ");

	mmResourceMsgInitFG(&pd,
						NULL,
						mm,
						NULL,
						MMR_MSG_FG_GET_STATE_DEBUG_STRING);

	pd.msg.in.activatedStruct = mmr->userStruct.activatedFG;

	pd.msg.in.fg.getStateDebugString.estrBuffer = estrBufferInOut;

	mmr->mmrc->msgHandler(&pd.msg);
}

S32 mmRegisterManagedResourceClass(	const char* name,
									MovementManagedResourceMsgHandler msgHandler,
									const MovementManagedResourceClassParseTables* ptis,
									U32 approvingMDCBit)
{
	MovementManagedResourceClass*	mmrc = NULL;
	S32								found = 0;

	assert(isPower2(approvingMDCBit));
	assert(approvingMDCBit < BIT(MDC_COUNT));

	EARRAY_CONST_FOREACH_BEGIN(mgState.mmr.unregisteredClasses, i, size);
	{
		mmrc = mgState.mmr.unregisteredClasses[i];

		if(!stricmp(mmrc->name, name)){
			assertmsgf(0, "Duplicate resource name: %s", name);
			return 0;
		}

		if(mmrc->msgHandler == msgHandler){
			assertmsgf(0, "Duplicate resource message handler: %s", name);
		}
	}
	EARRAY_FOREACH_END;

	EARRAY_CONST_FOREACH_BEGIN(mgState.mmr.idToClass, i, size);
	{
		mmrc = mgState.mmr.idToClass[i];

		if(!stricmp(mmrc->name, name)){
			found = 1;
			break;
		}
	}
	EARRAY_FOREACH_END;

	if(!found){
		mmrc = callocStruct(MovementManagedResourceClass);
		eaPush(&mgState.mmr.unregisteredClasses, mmrc);
	}

	if(!mmrc->name){
		mmrc->name = strdup(name);
	}

	mmrc->msgHandler = msgHandler;
	mmrc->pti = *ptis;
	mmrc->approvingMDC = highBitIndex(approvingMDCBit);

	FOR_BEGIN(i, MMRC_PT_COUNT);
	{
		char		buffer[100];
		const char* timerName;

		switch(i){
			xcase MMRC_PT_SET_STATE:
				timerName = "SetState";
			xcase MMRC_PT_NET_RECEIVE:
				timerName = "NetReceive";
				
			xdefault:{
				sprintf(buffer, "mmrc_pt[%u]", i);
				timerName = buffer;
			}
		}
		
		assert(i < ARRAY_SIZE(mmrc->perfInfo));

		mmrc->perfInfo[i].name = strdupf(	"%s - %s",
											timerName,
											mmrc->name);
	}
	FOR_END;

	return 1;
}

S32 mmRegisterManagedResourceClassID(	const char* name,
										U32 id)
{
	MovementManagedResourceClass*	mmrc = NULL;
	S32								found = 0;

	assertmsg(id < 1024, "Too many resource IDs.");

	if(eaUSize(&mgState.mmr.idToClass) <= id){
		eaSetSize(&mgState.mmr.idToClass, id + 1);
	}

	assertmsg(!mgState.mmr.idToClass[id], "Resource ID already in use.");

	EARRAY_CONST_FOREACH_BEGIN(mgState.mmr.idToClass, i, size);
	{
		mmrc = mgState.mmr.idToClass[i];

		if(	mmrc &&
			!stricmp(mmrc->name, name))
		{
			assertmsgf(0, "Duplicate resource name: %s", name);
			return 0;
		}
	}
	EARRAY_FOREACH_END;

	EARRAY_CONST_FOREACH_BEGIN(mgState.mmr.unregisteredClasses, i, size);
	{
		mmrc = mgState.mmr.unregisteredClasses[i];

		if(!stricmp(mmrc->name, name)){
			found = 1;
			eaRemove(&mgState.mmr.unregisteredClasses, i);
			if(size == 1){
				eaDestroy(&mgState.mmr.unregisteredClasses);
			}
			break;
		}
	}
	EARRAY_FOREACH_END;

	if(!found){
		mmrc = callocStruct(MovementManagedResourceClass);

		mmrc->name = strdup(name);
	}

	mmrc->id = id;

	mgState.mmr.idToClass[id] = mmrc;

	return 1;
}

S32 mmGetManagedResourceClassByID(	U32 id,
									MovementManagedResourceClass** mmrcOut)
{
	if(	!mmrcOut ||
		id >= eaUSize(&mgState.mmr.idToClass))
	{
		return 0;
	}

	*mmrcOut = mgState.mmr.idToClass[id];

	return !!*mmrcOut;
}

S32 mmGetManagedResourceIDByMsgHandler(	MovementManagedResourceMsgHandler msgHandler,
										U32* idOut)
{
	if(	!idOut ||
		!msgHandler)
	{
		return 0;
	}

	EARRAY_CONST_FOREACH_BEGIN(mgState.mmr.idToClass, i, size);
	{
		MovementManagedResourceClass* mmrc = mgState.mmr.idToClass[i];

		if(SAFE_MEMBER(mmrc, msgHandler == msgHandler)){
			*idOut = i;
		}
	}
	EARRAY_FOREACH_END;

	return 1;
}

void mmResourceMsgInit(	MovementManagedResourceMsgPrivateData* pd,
						MovementManagedResourceMsgOut* out,
						MovementManager* mm,
						MovementManagedResource* mmr,
						MovementManagedResourceMsgType msgType,
						U32 toSlot)
{
	ZeroStruct(pd);
	ZeroStruct(out);

	if(	!pd ||
		toSlot > 1)
	{
		return;
	}

	pd->msgType = msgType;

	pd->mm = mm;
	pd->msg.in.mm = mm;

	if(MMLOG_IS_ENABLED(mm)){
		pd->msg.in.flags.debugging = 1;
	}

	pd->msg.pd = pd;

	if(out){
		pd->msg.out = out;
	}

	pd->msg.in.msgType = msgType;

	if(mmr){
		S32 isFGSlot = toSlot == MM_FG_SLOT;

		pd->mmr = mmr;

		if(isFGSlot){
			pd->msg.in.fg.mmUserPointer = pd->mm->userPointer;
		}

		pd->msg.in.handle = isFGSlot ? mmr->handle : mmr->bg.handle;
		pd->msg.in.constant = mmr->userStruct.constant;
		pd->msg.in.constantNP = mmr->userStruct.constantNP;
		pd->mmrc = mmr->mmrc;
	}
}

void mmResourceMsgSend(MovementManagedResourceMsgPrivateData* pd){
	MovementManagedResourceMsgHandler msgHandler = SAFE_MEMBER2(pd, mmrc, msgHandler);

	if(!msgHandler){
		return;
	}

	msgHandler(&pd->msg);
}

static void mmLogResourceHelper(MovementManager* mm,
								char** estrBufferInOut,
								MovementManagedResource* mmrBeingLogged,
								MovementManagedResource* mmr)
{
	MovementManagedResourceState*const*const	states = mmIsForegroundThreadForLogging() ?
															mmr->fg.states :
															mmr->bg.states;

	if(	!mmr->handle &&
		!mgState.flags.logUnmanagedResources
		||
		mmr->handle &&
		!mgState.flags.logManagedResources)
	{
		return;
	}

	estrConcatf(estrBufferInOut,
				"-- %s0x%8.8p:",
				mmr == mmrBeingLogged ? "me:" : "",
				mmr);

	if(mmIsForegroundThreadForLogging()){
		#define FLAG(x) (mmr->fg.flags.x ? " "#x"," : "")
		estrConcatf(estrBufferInOut,
					"%s"// didSetState.
					"%s"// needsSetState.
					"%s"// sentCreate.
					"%s"// sentDestroyToBG.
					"%s"// hasNetState.
					"%s"// hadLocalState.
					"%s"// waitingForTrigger.
					"%s"// hasAnimBit.
					"%s"// hasUnsentStates.
					"%s"// noPredictedDestroy.
					,
					FLAG(didSetState),
					FLAG(needsSetState),
					FLAG(sentCreate),
					FLAG(sentDestroyToBG),
					FLAG(hasNetState),
					FLAG(hadLocalState),
					FLAG(waitingForTrigger),
					FLAG(hasDetailAnim),
					FLAG(hasUnsentStates),
					FLAG(noPredictedDestroy)
					);
		#undef FLAG

		if(mmr->fg.flags.waitingForWake){
			estrConcatf(estrBufferInOut,
						" waitingForWake(%u),",
						mmr->fg.spcWake);
		}
	}else{
		#define FLAG(x) (mmr->bg.flags.x ? " "#x"," : "")
		estrConcatf(estrBufferInOut,
					"%s"// didSetState.
					"%s"// needsSetState.
					"%s"// hadLocalState.
					"%s"// destroyedFromFG
					,
					FLAG(didSetState),
					FLAG(needsSetState),
					FLAG(hadLocalState),
					FLAG(destroyedFromFG)
					);
		#undef FLAG
	}

	estrConcatf(estrBufferInOut,
				" h%u/%u",
				mmr->handle,
				mmr->bg.handle);

	EARRAY_CONST_FOREACH_BEGIN(states, j, jsize);
	{
		MovementManagedResourceState*	mmrs = states[j];
		const char*						typeName;
		char							typeNameBuffer[100];

		switch(mmrs->mmrsType){
			xcase MMRST_CREATED:
				typeName = "created";
			xcase MMRST_STATE_CHANGE:
				typeName = "state";
			xcase MMRST_DESTROYED:
				typeName = "destroyed";
			xcase MMRST_CLEARED:
				typeName = "cleared";
			xdefault:
				sprintf(typeNameBuffer, "unknown(%u)", mmrs->mmrsType);
				typeName = typeNameBuffer;
		}

		assert(	mmrs->mmrsType >= 0 &&
				mmrs->mmrsType < MMRST_COUNT);

		estrConcatf(estrBufferInOut,
					"%s"		// (
					"%s"		// n
					"%s"		// L
					"%s"		// rFG
					"%s"		// sFG
					"%s"		// rBG
					"%s"		// fBG
					"%s.%u"		// type.spc
					" 0x%8.8p"	// pointer
					"%s"		// )
					,
					j ? "" : " (",
					mmrs->fg.flags.isNetState ? "net." : "",
					mmrs->bg.flags.createdLocally ? "local." : "",
					mmrs->fg.flags.gotRemoveRequestFromBG ? "rFG." : "",
					mmrs->fg.flags.sentRemoveRequestToBG ? "sFG." : "",
					mmrs->bg.flags.sentRemoveRequestToFG ? "rBG." : "",
					mmrs->bg.flags.sentFinishedStateToFG ? "fBG." : "",
					typeName,
					mmrs->spc,
					mmrs,
					j == jsize - 1 ? ")" : ", ");
	}
	EARRAY_FOREACH_END;

	estrConcatf(estrBufferInOut, ": ");

	// Get constant debug string.
	
	mmResourceGetConstantDebugString(	mm,
										mmr,
										NULL,
										NULL,
										NULL,
										estrBufferInOut);
										
	// Get BG state debug string.
	
	if(mmIsForegroundThreadForLogging()){
		mmResourceGetStateDebugStringFG(mm,
										mmr,
										estrBufferInOut);
	}else{
		mmResourceGetStateDebugStringBG(mm,
										mmr,
										estrBufferInOut);
	}

	// Add a newline, for some reason.

	estrConcatf(estrBufferInOut, "\n");
}

void wrapped_mmLogResource(	MovementManager* mm,
							MovementManagedResource* mmr,
							const char* format,
							...)
{
	if(!MMLOG_IS_ENABLED(mm)){
		return;
	}
	
	if(	!mmr &&
		(	mgState.flags.logManagedResources ||
			mgState.flags.logUnmanagedResources)
		||
		mgState.flags.logManagedResources &&
		(	mmIsForegroundThreadForLogging() &&
			mmr->handle
			||
			!mmIsForegroundThreadForLogging() &&
			mmr->bg.handle)
		||
		mgState.flags.logUnmanagedResources &&
		(	mmIsForegroundThreadForLogging() &&
			!mmr->handle
			||
			!mmIsForegroundThreadForLogging() &&
			!mmr->bg.handle))
	{
		char*								estrBuffer;
		MovementManagedResource*const*const	resources = mmIsForegroundThreadForLogging() ?
															mm->fg.resources :
															mm->bg.resources;

		estrStackCreateSize(&estrBuffer, 10000);

		estrConcatf(&estrBuffer,
					"(0x%8.8p, %u) %s.%s",
					mmr,
					mmGetFrameCount(),
					mmIsForegroundThreadForLogging() ? "FG" : "BG",
					mmr ?
						mmr->fg.flags.hasNetState ?
							"net" :
							"loc" :
						"none");

		if(!mmIsForegroundThreadForLogging()){
			estrConcatf(&estrBuffer,
						" %u",
						mgState.bg.pc.local.cur);
		}

		estrConcatf(&estrBuffer, ": ");

		VA_START(va, format);
			estrConcatfv(&estrBuffer, format, va);
		VA_END();

		if(mmr){
			estrConcatf(&estrBuffer, "\nMe: ");

			mmResourceGetConstantDebugString(	mm,
												mmr,
												NULL,
												NULL,
												NULL,
												&estrBuffer);

			if(mmIsForegroundThreadForLogging()){
				mmResourceGetStateDebugStringFG(mm,
												mmr,
												&estrBuffer);
			}else{
				mmResourceGetStateDebugStringBG(mm,
												mmr,
												&estrBuffer);
			}
		}

		if(mmIsForegroundThreadForLogging()){
			estrConcatf(&estrBuffer,
						"\nFG Resources:"
						"%s"// needsSetState.
						"%s"// waitingForTrigger.
						"%s"// hasAnimBit.
						"%s"// hasUnsentStates.
						,
						mm->fg.flags.mmrNeedsSetState ? " needsSetState," : "",
						mm->fg.flags.mmrWaitingForTrigger ? " waitingForTrigger," : "",
						mm->fg.flags.mmrHasDetailAnim ? " hasDetailAnim," : "",
						mm->fg.flags.mmrHasUnsentStates ? " hasUnsentStates," : "");

			if(mm->fg.flags.mmrWaitingForWake){								
				estrConcatf(&estrBuffer,
							" waitingForWake(%u)",
							mm->fg.spcWakeResource);
			}
			
			estrConcatf(&estrBuffer, "\n");
		}else{
			estrConcatf(&estrBuffer,
						"\nBG Resources:"
						"%s"// needsSetState.
						"%s"// isDestroyedFromFG.
						"\n",
						mm->bg.flags.mmrNeedsSetState ? " needsSetState," : "",
						mm->bg.flags.mmrIsDestroyedFromFG ? " isDestroyedFromFG," : "");
		}

		EARRAY_CONST_FOREACH_BEGIN(resources, i, isize);
		{
			if(	!mmr ||
				mmr == resources[i])
			{
				mmLogResourceHelper(mm,
									&estrBuffer,
									mmr,
									resources[i]);
			}
		}
		EARRAY_FOREACH_END;

		mmLog(mm, NULL, "[Resource] %s", estrBuffer);

		estrDestroy(&estrBuffer);
	}
}

static void mmResourceSendMsgDestroyedFG(	MovementManager* mm,
											MovementManagedResource* mmr,
											S32 bClear,
											const char* reason)
{
	MovementManagedResourceMsgPrivateData pd;

	if(!mmr->userStruct.activatedFG){
		assert(!mmr->fg.flags.didSetState);
		return;
	}

	mmLogResource(mm, mmr, "Destroying (%s)", reason);

	mmResourceMsgInitFG(&pd,
						NULL,
						mm,
						mmr,
						MMR_MSG_FG_DESTROYED);

	pd.msg.in.handle = mmr->handle;
	pd.msg.in.activatedStruct = mmr->userStruct.activatedFG;
	pd.msg.in.flags.clear = !!bClear;

	mmResourceMsgSend(&pd);
}

static void mmResourceSetNeedsSetStateFG(	MovementManager* mm,
											MovementManagedResource* mmr,
											const char* reason)
{
	if(FALSE_THEN_SET(mmr->fg.flagsMutable.needsSetState)){
		if(FALSE_THEN_SET(mm->fg.flagsMutable.mmrNeedsSetState)){
			mmHandleAfterSimWakesIncFG(mm, "mmrNeedsSetState", reason);
		}
	}else{
		assert(mm->fg.flags.mmrNeedsSetState);
	}
	
	mmLogResource(	mm,
					mmr,
					"Setting needsSetState (reason: %s)",
					FIRST_IF_SET(reason, "no reason given"));
}

static void mmResourceSetHasUnsentStatesFG(	MovementManager* mm,
											MovementManagedResource* mmr)
{
	if(FALSE_THEN_SET(mmr->fg.flagsMutable.hasUnsentStates)){
		mm->fg.flagsMutable.mmrHasUnsentStates = 1;
	}else{
		assert(mm->fg.flagsMutable.mmrHasUnsentStates);
	}
}

static void mmResourceSendMsgSetStateFG(MovementManager* mm,
										MovementManagedResource* mmr,
										MovementManagedResourceState* mmrsNetOlder,
										MovementManagedResourceState* mmrsNetNewer,
										F32 interpNetOlderToNewer,
										MovementManagedResourceState* mmrsLocalOlder,
										MovementManagedResourceState* mmrsLocalNewer,
										F32 interpLocalOlderToNewer,
										F32 interpLocalToNet)
{
	MovementManagedResourceMsgPrivateData	pd;
	MovementManagedResourceMsgOut			out;

	if(	!mmr->handle &&
		mmr->fg.flagsMutable.didSetState)
	{
		return;
	}

	mmr->fg.flagsMutable.didSetState = 1;

	mmLogResource(	mm,
					mmr,
					"Before setting state for %s resource",
					mmr->mmrc->name);

	// Create an "activated" struct for managed resources.

	if(!mmr->userStruct.activatedFG){
		mmStructAllocIfNull(mmr->mmrc->pti.activatedFG,
							mmr->userStruct.activatedFG,
							mm);

		#if PRINT_ALLOCATION_COUNTS
		{
			allocedActivated++;
			printf("rcAllocedActivatedCount = %u\n", allocedActivated);
		}
		#endif
	}

	mmResourceMsgInitFG(&pd,
						&out,
						mm,
						mmr,
						MMR_MSG_FG_SET_STATE);

	pd.msg.in.handle = mmr->handle;
	pd.msg.in.activatedStruct = mmr->userStruct.activatedFG;
	
	pd.msg.in.fg.setState.state.net.olderStruct = SAFE_MEMBER(mmrsNetOlder, userStruct.state);
	pd.msg.in.fg.setState.state.net.newerStruct = SAFE_MEMBER(mmrsNetNewer, userStruct.state);
	pd.msg.in.fg.setState.state.net.interpOlderToNewer = interpNetOlderToNewer;
	pd.msg.in.fg.setState.state.net.spcStart = SAFE_MEMBER(mmrsNetOlder, spc);
	
	pd.msg.in.fg.setState.state.local.olderStruct = SAFE_MEMBER(mmrsLocalOlder, userStruct.state);
	pd.msg.in.fg.setState.state.local.newerStruct = SAFE_MEMBER(mmrsLocalNewer, userStruct.state);
	pd.msg.in.fg.setState.state.local.interpOlderToNewer = interpLocalOlderToNewer;
	pd.msg.in.fg.setState.state.local.spcStart = SAFE_MEMBER(mmrsLocalOlder, spc);
	
	pd.msg.in.fg.setState.interpLocalToNet = interpLocalToNet;
	
	pd.msg.out = &out;

	mmResourceMsgSend(&pd);

	if(out.fg.setState.flags.needsRetry){
		mmResourceSetNeedsSetStateFG(mm, mmr, "needsRetry");

		mmr->fg.flagsMutable.didSetState = 0;
	}
	else if(mmr->handle){
		MovementManagedResourceState* mmrsTail = eaTail(&mmr->fg.states);

		if(	(	mmrsNetOlder &&
				mmrsTail != mmrsNetOlder)
			||
			(	mmrsLocalOlder &&
				mmrsTail != mmrsLocalOlder))
		{
			mmResourceSetNeedsSetStateFG(mm, mmr, "not at tail yet");
		}
	}

	mmLogResource(	mm,
					mmr,
					"After setting state for %s resource",
					mmr->mmrc->name);
}

static void mmResourceMarkAsUpdatedToBG(MovementManager* mm,
										MovementThreadData* td,
										MovementManagedResource* mmr)
{
	if(mgState.flags.isServer){
		assert(mmr->handle);
	}

	assert(!mmr->fg.flags.sentDestroyToBG);

	if(FALSE_THEN_SET(mmr->toBG[MM_FG_SLOT].flagsMutable.updated)){
		// Add to updated list.
		
		assert(eaFind(&td->toBG.updatedResources, mmr) < 0);

		eaPush(	&td->toBG.updatedResourcesMutable,
				mmr);

		// Flag mm as having an update.

		td->toBG.flagsMutable.hasToBG = 1;
		td->toBG.flagsMutable.mmrHasUpdate = 1;
	}
}

static void mmResourceRemoveStateFG(MovementManager* mm,
									MovementThreadData* td,
									MovementManagedResource* mmr,
									S32 index)
{
	MovementManagedResourceState*	mmrs = mmr->fg.states[index];
	MovementManagedResourceToBG*	toBG = mmr->toBG + MM_FG_SLOT;
	
	mmResourceStateSetNotInListFG(mmrs);
	
	if(	mmr->handle ||
		!mgState.flags.isServer)
	{
		// On the server, unmanaged resources are not kept in the BG thread.

		if(eaFindAndRemove(&toBG->statesMutable, mmrs) >= 0){
			// Found and removed from the new states list.

			assert(!mmrs->bg.flags.inList);
			
			mmResourceStateDestroy(mm, mmrs);
		}else{
			// Wasn't in the new states list, so have to send the remove.

			mmrsTrackFlags(	mmrs,
							MMRS_TRACK_ALLOCED,
							MMRS_TRACK_SENT_REMOVE_TO_BG,
							MMRS_TRACK_SENT_REMOVE_TO_BG,
							0);
			
			ASSERT_FALSE_AND_SET(mmrs->fg.flagsMutable.sentRemoveRequestToBG);

			mmResourceMarkAsUpdatedToBG(mm, td, mmr);

			mmResourceDebugRemoveRemoveStatesToBG(mmr, toBG->removeStates);
			eaPush(	&toBG->removeStatesMutable,
					mmrs);
			mmResourceDebugAddRemoveStatesToBG(mmr, toBG->removeStates);
		}
	}else{
		// Not kept in BG so just destroy now.

		assert(!mmrs->fg.flags.sentRemoveRequestToBG);
		assert(!mmrs->bg.flags.inList);

		mmResourceStateDestroy(mm, mmrs);
	}
				
	eaRemove(	&mmr->fg.statesMutable,
				index);

	mmResourceSetNeedsSetStateFG(mm, mmr, "removed state");
}

static void mmResourceDestroyInternalFG(MovementManager* mm,
										MovementManagedResource* mmr,
										bool bClear)
{
	assert(!mmr->toBG[MM_FG_SLOT].flags.updated);
	assert(!mmr->bg.flags.inList);

	mmRareLockEnter(mm);
	{
		if(eaFindAndRemove(&mm->allResourcesMutable, mmr) < 0){
			assert(0);
		}

		assert(eaFind(&mm->allResources, mmr) < 0);
	}
	mmRareLockLeave(mm);	

	// Remove from the FG list.

	ASSERT_TRUE_AND_RESET(mmr->fg.flagsMutable.inList);

	if(eaFindAndRemove(&mm->fg.resourcesMutable, mmr) < 0){
		assert(0);
	}

	assert(eaFind(&mm->fg.resources, mmr) < 0);

	if(	!eaSize(&mm->fg.resources) &&
		!mgState.flags.isServer)
	{
		mmHandleAfterSimWakesDecFG(mm, "has mmr");
	}

	// Unlink, deactivate, free.

	if(mmr->handle){
		mmResourceSendMsgDestroyedFG(mm, mmr, bClear, "Destroyed from BG");
	}

	mmResourceFreeFG(mm, &mmr);
}

static void mmResourceSendDestroyToBG(	MovementManager* mm,
										MovementThreadData* td,
										MovementManagedResource* mmr,
										bool bClear,
										const char* reason)
{
	if(mmr->fg.flags.sentDestroyToBG){
		return;
	}
	
	if(	mmr->handle ||
		!mgState.flags.isServer)
	{
		mmResourceMarkAsUpdatedToBG(mm, td, mmr);

		while(eaSize(&mmr->fg.states)){
			mmResourceRemoveStateFG(mm, td, mmr, 0);
		}
		
		// This has to be after the previous updates.
		
		mmr->fg.flagsMutable.sentDestroyToBG = 1;

		mmr->toBG[MM_FG_SLOT].flagsMutable.cleared = bClear;
		mmr->toBG[MM_FG_SLOT].flagsMutable.destroyed = 1;
		
		mmLogResource(mm, mmr, "Sending destroy to BG (%s)", reason);
	}else{
		ASSERT_FALSE_AND_SET(mmr->fg.flagsMutable.destroyAfterSetState);
		if (bClear) {
			ASSERT_FALSE_AND_SET(mmr->fg.flagsMutable.clearAfterSetState);
		}
	}
}

static void mmGetCurrentStatesFG(	MovementManager* mm,
									MovementManagedResource* mmr,
									MovementManagedResourceState** mmrsNetOlderOut,
									MovementManagedResourceState** mmrsNetNewerOut,
									F32* interpNetOlderToNewerOut,
									MovementManagedResourceState** mmrsLocalOlderOut,
									MovementManagedResourceState** mmrsLocalNewerOut,
									F32* interpLocalOlderToNewerOut,
									F32* interpLocalToNetOut)
{
	S32			foundNet = 0;
	S32			foundLocal = 0;
	const S32	stateCount = eaSize(&mmr->fg.states);

	// Find the floor state.

	EARRAY_FOREACH_REVERSE_BEGIN(mmr->fg.states, i);
	{
		const MovementManagedResourceState* mmrs = mmr->fg.states[i];
		const MovementManagedResourceState* mmrsNext;

		assert(	mmrs->mmrsType >= 0 &&
			mmrs->mmrsType < MMRST_COUNT);

		if(i + 1 < stateCount){
			mmrsNext = mmr->fg.states[i + 1];
		}else{
			mmrsNext = NULL;
		}

		// Check for a net state.

		if(	!foundNet &&
			mmr->fg.flags.hasNetState &&
			mmrs->fg.flags.isNetState)
		{
			if(subS32(	mgState.fg.netView.spcFloor,
				mmrs->spc) >= 0)
			{
				foundNet = 1;

				*mmrsNetOlderOut = mmr->fg.states[i];

				if(SAFE_MEMBER(mmrsNext, fg.flags.isNetState)){
					S32 diffToNext = subS32(mmrsNext->spc,
						mgState.fg.netView.spcFloor);

					if(diffToNext <= MM_PROCESS_COUNTS_PER_STEP){
						*mmrsNetNewerOut = mmr->fg.states[i + 1];
						*interpNetOlderToNewerOut =	mgState.fg.netView.spcInterpFloorToCeiling;
					}else{
						*mmrsNetNewerOut = NULL;
						*interpNetOlderToNewerOut = 0.f;
					}
				}else{
					*mmrsNetNewerOut = NULL;
					*interpNetOlderToNewerOut = 0.f;
				}
			}
			else if(mgState.fg.netView.spcCeiling == mmrs->spc){
				const MovementManagedResourceState* mmrsPrev = i ? mmr->fg.states[i - 1] : NULL;

				foundNet = 1;

				*mmrsNetNewerOut = mmr->fg.states[i];

				if(SAFE_MEMBER(mmrsPrev, fg.flags.isNetState)){
					*mmrsNetOlderOut = mmr->fg.states[i - 1];
					*interpNetOlderToNewerOut =	mgState.fg.netView.spcInterpFloorToCeiling;
				}else{
					*mmrsNetOlderOut = NULL;
					*interpNetOlderToNewerOut = 1.f;
				}
			}

			if(	foundNet
				&&
				(	foundLocal ||
				!mmr->fg.flags.hadLocalState))
			{
				break;
			}
		}

		// Check for a local state.

		if(	!foundLocal &&
			mmr->fg.flags.hadLocalState &&
			subS32(	mgState.fg.localView.spcCeiling,
			mmrs->spc) >= 0)
		{
			foundLocal = 1;

			if(	mgState.fg.localView.spcCeiling == mmrs->spc &&
				mgState.fg.localView.outputInterp.forward)
			{
				const MovementManagedResourceState* mmrsPrev = i ? mmr->fg.states[i - 1] : NULL;

				*mmrsLocalNewerOut = mmr->fg.states[i];

				if(mmrsPrev){
					*mmrsLocalOlderOut = mmr->fg.states[i - 1];
					*interpLocalOlderToNewerOut = mgState.fg.localView.outputInterp.forward;
				}else{
					*mmrsLocalOlderOut = NULL;
					*interpLocalOlderToNewerOut = 1.f;
				}
			}else{
				*mmrsLocalOlderOut = mmr->fg.states[i];

				if(mmrsNext){
					S32 diffToNext = subS32(mmrsNext->spc,
						mgState.fg.localView.spcCeiling);

					if(diffToNext <= MM_PROCESS_COUNTS_PER_STEP){
						*mmrsNetNewerOut = mmr->fg.states[i + 1];

						*interpLocalOlderToNewerOut = mgState.fg.localView.outputInterp.forward;
						MINMAX1(*interpLocalOlderToNewerOut, 0.f, 1.f);
					}else{
						*mmrsNetNewerOut = NULL;
						*interpLocalOlderToNewerOut = 0.f;
					}
				}else{
					*mmrsNetNewerOut = NULL;
					*interpLocalOlderToNewerOut = 0.f;
				}
			}

			if(	foundNet ||
				!mmr->fg.flags.hasNetState)
			{
				break;
			}
		}
	}
	EARRAY_FOREACH_END;

	mmLog(	mm,
		NULL,
		"[Resource] Getting states for resource %u: %s%s%s",
		mmr->handle,
		foundNet ? "foundNet, " : "",
		foundLocal ? "foundLocal, " : "",
		!foundNet && !foundLocal ? "foundNeither" : "");

	if(foundNet){
		if(!foundLocal){
			*interpLocalToNetOut = 1.f;
		}else{
			F32 diff =	(F32)(S32)(	mgState.fg.netView.spcFloor -
				mmr->fg.spcNetCreate) +
				mgState.fg.netView.spcInterpFloorToCeiling;

			if(diff <= 0.f){
				*interpLocalToNetOut = 0.f;
			}
			else if(diff < MM_PROCESS_COUNTS_PER_SECOND){
				*interpLocalToNetOut = diff / (F32)MM_PROCESS_COUNTS_PER_SECOND;
			}else{
				*interpLocalToNetOut = 1.f;
			}

			mmLog(	mm,
				NULL,
				"[Resource] Resource %u:"
				" spcFloor %u"
				", spcNetCreate %u"
				", interp %1.3f"
				", diff %1.3f",
				mmr->handle,
				mgState.fg.netView.spcFloor,
				mmr->fg.spcNetCreate,
				mgState.fg.netView.spcInterpFloorToCeiling,
				diff);
		}
	}else{
		*interpLocalToNetOut = 0.f;
	}
}

static void mmLogResourceChangeFG(	MovementManager* mm,
									MovementManagedResource* mmr,
									S32* logCountInOut,
									const char* tag,
									U32 argb)
{
	const F32	baseHeight = 8.f;
	Vec3		pos;

	mmGetPositionFG(mm, pos);
	
	pos[1] += baseHeight + 2 * (*logCountInOut)++;

	mmLogPoint(	mm,
				NULL,
				tag,
				argb,
				pos);
}

void mmResourceRemoveStatesOlderThanThisFG(	MovementManager* mm,
											MovementThreadData* td,
											MovementManagedResource* mmr,
											U32 spcOldestToKeep)
{
	if(mmr->fg.flags.sentDestroyToBG){
		return;
	}

	// Remove states older than the current view.

	while(eaSize(&mmr->fg.states) >= 2){
		MovementManagedResourceState* mmrs = mmr->fg.states[0];
		MovementManagedResourceState* mmrsNext = mmr->fg.states[1];

		if(subS32(	mmrsNext->spc,
					spcOldestToKeep) > 0)
		{
			// Second one is newer than the oldest, so wait until we pass it.
			
			break;
		}

		if(	mmrs->fg.flags.isNetState &&
			!mmrsNext->fg.flags.isNetState)
		{
			// Can't remove the net state when the next one is predicted.

			break;
		}
		
		mmLog(	mm,
				NULL,
				"[rc.states] %u. Removing old 0: %u.%u\n",
				spcOldestToKeep,
				mmrs->mmrsType,
				mmrs->spc);

		mmResourceRemoveStateFG(mm,
								td,
								mmr,
								0);
	}
}

static void mmResourceSetStateFG(	MovementManager* mm,
									MovementThreadData* td,
									MovementManagedResource* mmr,
									U32* logCountInOut)
{
	MovementManagedResourceState*	mmrsNetOlder = NULL;
	MovementManagedResourceState*	mmrsNetNewer = NULL;
	F32								interpNetOlderToNewer = 0.f;
	MovementManagedResourceState*	mmrsLocalOlder = NULL;
	MovementManagedResourceState*	mmrsLocalNewer = NULL;
	F32								interpLocalOlderToNewer = 0.f;
	F32								interpLocalToNet = 0.f;
	const char*						shouldBeDestroyedReason = NULL;
	U32								argbDestroy = 0xffff0000;
	S32								bClear = false;
	
	mmResourceRemoveStatesOlderThanThisFG(	mm,
											td,
											mmr,
											mgState.fg.frame.cur.spcOldestToKeep);
	
	mmGetCurrentStatesFG(	mm,
							mmr,
							&mmrsNetOlder,
							&mmrsNetNewer,
							&interpNetOlderToNewer,
							&mmrsLocalOlder,
							&mmrsLocalNewer,
							&interpLocalOlderToNewer,
							&interpLocalToNet);

	if(mmrsNetOlder){
		// Check the net state first.

		switch(mmrsNetOlder->mmrsType){
			xcase MMRST_CREATED:{
				if(!mmr->handle){
					if(	mmr->fg.flags.didSetState &&
						!mmr->fg.flags.waitingForTrigger)
					{
						shouldBeDestroyedReason = "Net state says unmanaged and state was set";
					}else{
						mmResourceSetNeedsSetStateFG(mm, mmr, "!didSetState or waitingForTrigger");
					}
				}
			}

			xcase MMRST_DESTROYED:{
				shouldBeDestroyedReason = "Net state says destroyed";
			}

			xcase MMRST_CLEARED:{
				shouldBeDestroyedReason = "Net state says clear";
				bClear = true;
			}
		}
	}
	else if(!mgState.flags.isServer){
		// Client needs to re-check for mispredicts.

		mmResourceSetNeedsSetStateFG(mm, mmr, "waiting for server states");
	}

	if(!mmrsLocalOlder){
		// Check if set state needs to be queued again (i.e. nothing else is going to do it).

		if(!mgState.flags.isServer){
			if(mmr->fg.flags.hadLocalState){
				mmResourceSetNeedsSetStateFG(mm, mmr, "too young to destroy");
			}
		}
		else if(!mgState.fg.clients){
			mmResourceSetNeedsSetStateFG(mm, mmr, "too young to destroy (no clients)");
		}
		else if(!mmrsLocalNewer){
			mmResourceSetNeedsSetStateFG(mm, mmr, "too young to set state");
		}
		else if(!mmr->handle){
			if(mmr->fg.flags.sentCreate){
				mmResourceSetNeedsSetStateFG(mm, mmr, "too young to destroy (create already sent)");
			}
		}
		else if(mmr->fg.flags.sentDestroy){
			mmResourceSetNeedsSetStateFG(mm, mmr, "too young to destroy (destroy already sent)");
		}
	}
	else if(!shouldBeDestroyedReason){
		// If there's a local state then use that.

		if(	!mgState.flags.isServer &&
			!mm->flags.isLocal &&
			!mmr->fg.flags.hasNetState &&
			!mmr->fg.flags.sentDestroyToBG)
		{
			// Predicted resources get one second to get an ack from the server or they're killed.
		
			const U32 localSPC =	mgState.fg.localView.pcCeiling +
									mgState.fg.netReceive.cur.offset.clientToServerSync;
			
			if(subS32(localSPC, mmrsLocalOlder->spc) > MM_PROCESS_COUNTS_PER_SECOND){
				shouldBeDestroyedReason = "Mispredicted resource, too old";

				argbDestroy = 0xffffff00;
			}
		}

		switch(mmrsLocalOlder->mmrsType){
			xcase MMRST_CREATED:{
				if(mmr->handle){
					break;
				}

				if(!mmr->fg.flags.didSetState){
					mmResourceSetNeedsSetStateFG(mm, mmr, "force set state next frame");
				}
				else if(mgState.flags.isServer){
					if(	mmr->fg.flags.sentCreate ||
						!mgState.fg.clients)
					{
						shouldBeDestroyedReason = "Destroyed unmanaged on server";
					}
				}
				else if(mm->flags.isLocal){
					shouldBeDestroyedReason = "Destroyed unmanaged local on client";
				}
			}

			xcase MMRST_DESTROYED:{
				assert(mmr->handle);

				if(mgState.flags.isServer){
					if(	mmr->fg.flags.sentDestroy ||
						!mgState.fg.clients)
					{
						shouldBeDestroyedReason = "Destroyed managed on server";
					}
				}
				else if(mm->flags.isLocal){
					shouldBeDestroyedReason = "Destroyed managed local on client";
				}
			}

			xcase MMRST_CLEARED:{
				assert(mmr->handle);

				if(mgState.flags.isServer){
					if(	mmr->fg.flags.sentDestroy ||
						!mgState.fg.clients)
					{
						shouldBeDestroyedReason = "Clear managed on server";
						bClear = true;
					}
				}
				else if(mm->flags.isLocal){
					shouldBeDestroyedReason = "Clear managed local on client";
					bClear = true;
				}
			}
		}
	}

	if(mgState.fg.flags.predictDisabled){
		mmrsLocalOlder = mmrsLocalNewer = NULL;
		interpLocalOlderToNewer = 0.f;
	}

	// Check if this resource was mispredicted.

	if(!eaSize(&mmr->fg.states)){
		if(	!mm->fg.flags.destroyed &&
			!mmr->fg.flags.sentDestroyToBG)
		{
			assert(!mmr->fg.flags.hasNetState);
		}

		shouldBeDestroyedReason = "Mispredicted resource, no states";
		
		argbDestroy = 0xffffff00;
	}
	
	mmLogResource(	mm,
					mmr,
					"Using states (net create %u)"
					" c[%u:0x%8.8p >-[%1.3f]-> %u:0x%8.8p]"
					" >-- %1.3f -->"
					" s[%u:0x%8.8p >-[%1.3f]-> %u:0x%8.8p]",
					mmr->fg.spcNetCreate,
					SAFE_MEMBER(mmrsLocalOlder, spc),
					mmrsLocalOlder,
					interpLocalOlderToNewer,
					SAFE_MEMBER(mmrsLocalNewer, spc),
					mmrsLocalNewer,
					interpLocalToNet,
					SAFE_MEMBER(mmrsNetOlder, spc),
					mmrsNetOlder,
					interpNetOlderToNewer,
					SAFE_MEMBER(mmrsNetNewer, spc),
					mmrsNetNewer);

	if(shouldBeDestroyedReason){
		if(!mmr->fg.flags.sentDestroyToBG){
			mmResourceSendMsgDestroyedFG(	mm,
											mmr,
											bClear,
											shouldBeDestroyedReason);

			mmResourceSendDestroyToBG(	mm,
										td,
										mmr,
										bClear,
										shouldBeDestroyedReason);

			if(MMLOG_IS_ENABLED(mm)){
				mmLogResourceChangeFG(	mm,
										mmr,
										logCountInOut,
										"rc.destroy",
										argbDestroy);
			}
		}
	}
	else if(mmrsLocalOlder ||
			mmrsLocalNewer ||
			mmrsNetOlder ||
			mmrsNetNewer)
	{
		if(	mmrsLocalOlder &&
			(mmrsLocalOlder->mmrsType == MMRST_DESTROYED || mmrsLocalOlder->mmrsType == MMRST_CLEARED) &&
			!mmr->fg.flags.noPredictedDestroy)
		{
			mmResourceSendMsgDestroyedFG(	mm,
											mmr,
											(mmrsLocalOlder->mmrsType == MMRST_CLEARED),
											"Local state says destroyed");
		}else{
			if(MMLOG_IS_ENABLED(mm)){
				mmLogResourceChangeFG(	mm,
										mmr,
										logCountInOut,
										"rc.setState",
										0xff00ff00);
			}

			mmResourceSendMsgSetStateFG(mm,
										mmr,
										mmrsNetOlder,
										mmrsNetNewer,
										interpNetOlderToNewer,
										mmrsLocalOlder,
										mmrsLocalNewer,
										interpLocalOlderToNewer,
										interpLocalToNet);
		}
	}
}

static void mmResourceSendMsgWakeFG(MovementManager* mm,
									MovementManagedResource* mmr)
{
	MovementManagedResourceMsgPrivateData pd;

	mmResourceMsgInitFG(&pd,
						NULL,
						mm,
						mmr,
						MMR_MSG_FG_WAKE);

	pd.msg.in.handle = mmr->handle;
	pd.msg.in.activatedStruct = mmr->userStruct.activatedFG;

	mmResourceMsgSend(&pd);
}

static void mmResourceSetWaitingForWakeFG(	MovementManager* mm,
											MovementManagedResource* mmr,
											U32 spc)
{
	mmr->fg.flagsMutable.waitingForWake = 1;
	mmr->fg.spcWake = spc;
	
	if(FALSE_THEN_SET(mm->fg.flagsMutable.mmrWaitingForWake)){
		mmHandleAfterSimWakesIncFG(mm, "mmrWaitingForWake", __FUNCTION__);
		mm->fg.spcWakeResource = spc;
	}
	else if(subS32(spc, mm->fg.spcWakeResource) < 0){
		mm->fg.spcWakeResource = spc;
	}
}

void mmResourcesSendWakeFG(MovementManager* mm){
	if(subS32(mgState.fg.localView.spcCeiling, mm->fg.spcWakeResource) < 0){
		// Not even predicted-spc has reached the wake time, so just skip this.

		MM_DEBUG_COUNT("mmResourcesSendWakeFG:wait", 1);
		
		return;
	}
	
	PERFINFO_AUTO_START_FUNC();

	ASSERT_TRUE_AND_RESET(mm->fg.flagsMutable.mmrWaitingForWake);
	mmHandleAfterSimWakesDecFG(mm, "mmrWaitingForWake");

	EARRAY_CONST_FOREACH_BEGIN(mm->fg.resources, i, isize);
	{
		MovementManagedResource* mmr = mm->fg.resources[i];

		if(	!mmr->fg.flags.waitingForWake ||
			mmr->fg.flags.sentDestroyToBG ||
			!mmr->userStruct.activatedFG)
		{
			continue;
		}

		if(mmr->fg.flags.hadLocalState){
			if(subS32(mgState.fg.localView.spcCeiling, mmr->fg.spcWake) >= 0){
				mmr->fg.flagsMutable.waitingForWake = 0;
				
				mmResourceSendMsgWakeFG(mm, mmr);
			}else{
				mmResourceSetWaitingForWakeFG(mm, mmr, mmr->fg.spcWake);
			}
		}
		else if(subS32(mgState.fg.netView.spcCeiling, mmr->fg.spcWake) >= 0){
			mmr->fg.flagsMutable.waitingForWake = 0;
			
			mmResourceSendMsgWakeFG(mm, mmr);
		}else{
			mmResourceSetWaitingForWakeFG(mm, mmr, mmr->fg.spcWake);
		}
	}
	EARRAY_FOREACH_END;
	
	PERFINFO_AUTO_STOP();
}

void mmResourcesSetStateFG(	MovementManager* mm,
							MovementThreadData* td)
{
	U32 logCount = 0;
	
	PERFINFO_AUTO_START_FUNC();
	
	mmLog(mm, NULL, "[resource] Setting FG resource states.");
	
	ASSERT_TRUE_AND_RESET(mm->fg.flagsMutable.mmrNeedsSetState);
	mmHandleAfterSimWakesDecFG(mm, "mmrNeedsSetState");

	EARRAY_CONST_FOREACH_BEGIN(mm->fg.resources, i, isize);
	{
		MovementManagedResource* mmr = mm->fg.resources[i];

		if(!TRUE_THEN_RESET(mmr->fg.flagsMutable.needsSetState)){
			continue;
		}

		MMR_PERFINFO_AUTO_START_GUARD(mmr, MMRC_PT_SET_STATE);
		{
			mmResourceSetStateFG(	mm,
									td,
									mmr,
									&logCount);

			if(TRUE_THEN_RESET(mmr->fg.flagsMutable.destroyAfterSetState)){
				// Destroy it immediately.
				
				assert(!mmr->fg.flags.inListBG);
				assert(!mmr->bg.flags.inList);

				mmResourceDestroyInternalFG(mm, mmr, TRUE_THEN_RESET(mmr->fg.flagsMutable.clearAfterSetState));
				
				i--;
				isize--;
			}
		}
		MMR_PERFINFO_AUTO_STOP_GUARD(mmr, MMRC_PT_SET_STATE);
	}
	EARRAY_FOREACH_END;
	
	PERFINFO_AUTO_STOP();
}

void mmRemoveRepredictedResourceStatesFG(	MovementManager* mm,
											MovementThreadData* td)
{
	if(MMLOG_IS_ENABLED(mm)){
		mmLog(	mm,
				NULL,
				"[rc.states] Removing states newer than s%u (latest from server s%u)",
				SAFE_MEMBER(td->toFG.predict, repredict.spc),
				mgState.fg.netReceive.cur.pc.server);

		mmLogResource(	mm,
						NULL,
						"These are the resources before post-repredict removal");
	}
	
	EARRAY_CONST_FOREACH_BEGIN(mm->fg.resources, i, size);
	{
		mmResourceRemoveStatesOlderThanThisFG(	mm,
												td,
												mm->fg.resources[i],
												td->toFG.spcOldestToKeep);
	}
	EARRAY_FOREACH_END;

	mmLogResource(mm, NULL, "These are the resources after post-repredict removal");
}

static void mmAddResourceToListFG(	MovementManager* mm,
									MovementManagedResource* mmr)
{
	mmLog(	mm,
			NULL,
			"[rc.add] Added resource %u:%p to FG list",
			mmr->handle,
			mmr);

	ASSERT_FALSE_AND_SET(mmr->fg.flagsMutable.inList);
	
	assert(eaFind(&mm->fg.resources, mmr) < 0);

	if(	!eaPush(&mm->fg.resourcesMutable, mmr) &&
		!mgState.flags.isServer)
	{
		mmHandleAfterSimWakesIncFG(mm, "has mmr", __FUNCTION__);
	}
}

static void mmResourceInsertStateFG(MovementManager* mm,
									MovementManagedResource* mmr,
									MovementManagedResourceState* mmrsNew)
{
	S32 index = eaSize(&mmr->fg.states);
	
	mmResourceStateSetInListFG(mmrsNew);

	EARRAY_CONST_FOREACH_BEGIN(mmr->fg.states, i, isize);
	{
		MovementManagedResourceState* mmrs = mmr->fg.states[i];

		if(subS32(	mmrs->spc,
					mmrsNew->spc) > 0)
		{
			index = i;
			break;
		}
	}
	EARRAY_FOREACH_END;
	
	eaInsert(	&mmr->fg.statesMutable,
				mmrsNew,
				index);

	mmResourceSetNeedsSetStateFG(mm, mmr, "added a new state");
}

static void mmResourcePruneStatesFG(MovementManager* mm,
									MovementThreadData* td,
									MovementManagedResource* mmr,
									S32 isLocalEntity)
{
	S32 foundLocalState = 0;
	
	if(mmr->fg.flags.sentDestroyToBG){
		return;
	}
	
	EARRAY_CONST_FOREACH_BEGIN(mmr->fg.states, i, isize);
	{
		MovementManagedResourceState* mmrs = mmr->fg.states[i];
		
		if(mmrs->fg.flags.isNetState){
			S32 foundNextNetState = 0;
			
			if(TRUE_THEN_RESET(foundLocalState)){
				while(i){
					mmResourceRemoveStateFG(mm,
											td,
											mmr,
											i - 1);
											
					i--;
					isize--;
				}
			}
			
			// Remove all local states between here and the next net state.
			
			EARRAY_CONST_FOREACH_BEGIN_FROM(mmr->fg.states, j, jsize, i + 1);
			{
				MovementManagedResourceState* mmrsAfter = mmr->fg.states[j];
				
				if(mmrsAfter->fg.flags.isNetState){
					while(i + 1 < isize){
						MovementManagedResourceState* mmrsNext = mmr->fg.states[i + 1];
						
						if(mmrsNext->fg.flags.isNetState){
							break;
						}
						
						mmResourceRemoveStateFG(mm, td, mmr, i + 1);
									
						isize--;
						jsize--;
					}
					
					foundNextNetState = 1;
					
					break;
				}
			}
			EARRAY_FOREACH_END;
			
			if(!foundNextNetState){
				const U32 latestSPC = isLocalEntity ?
										mgState.fg.netReceive.cur.pc.serverSync :
										mgState.fg.netReceive.cur.pc.server;
											
				// Remove all local states between here and the latest server time.
				
				while(i + 1 < isize){
					MovementManagedResourceState* mmrsNext = mmr->fg.states[i + 1];
					
					assert(!mmrsNext->fg.flags.isNetState);
					
					if(subS32(	latestSPC,
								mmrsNext->spc) < 0)
					{
						break;
					}
					
					mmResourceRemoveStateFG(mm, td, mmr, i + 1);
									
					isize--;
				}
				
				break;
			}
		}else{
			foundLocalState = 1;
		}
	}
	EARRAY_FOREACH_END;

	if(foundLocalState){
		// Remove states older than the latest server time.

		U32 latestServerTime = isLocalEntity ?
									mgState.fg.netReceive.cur.pc.serverSync :
									mgState.fg.netReceive.cur.pc.server;

		// Remove all local states between here and the latest server time.

		while(eaSize(&mmr->fg.states)){
			MovementManagedResourceState* mmrs = mmr->fg.states[0];

			assert(!mmrs->fg.flags.isNetState);

			if(subS32(	latestServerTime,
						mmrs->spc) < 0)
			{
				break;
			}

			mmResourceRemoveStateFG(mm, td, mmr, 0);
		}
	}
}

void mmHandleResourceUpdatesFromBG(	MovementManager* mm,
									MovementThreadData* td)
{
	// Free the resource states that the background released.

	if(eaSize(&td->toFG.finishedResourceStates)){
		PERFINFO_AUTO_START("handleFinishedResourceStates", 1);

		EARRAY_CONST_FOREACH_BEGIN(td->toFG.finishedResourceStates, i, size);
		{
			MovementManagedResourceState* mmrs = td->toFG.finishedResourceStates[i];

			assert(!mmrs->fg.flags.inList);
			assert(!mmrs->bg.flags.inList);

			mmResourceStateDestroy(mm, mmrs);
		}
		EARRAY_FOREACH_END;

		eaSetSize(&td->toFG.finishedResourceStatesMutable, 0);

		PERFINFO_AUTO_STOP();
	}

	// Check for updated resources.

	if(eaSize(&td->toFG.updatedResources)){
		PERFINFO_AUTO_START("handleUpdatedResources", 1);

		EARRAY_CONST_FOREACH_BEGIN(td->toFG.updatedResources, i, size);
		{
			MovementManagedResource*		mmr = td->toFG.updatedResources[i];
			MovementManagedResourceToFG*	toFG = mmr->toFG + MM_FG_SLOT;

			ASSERT_TRUE_AND_RESET(toFG->flagsMutable.updated);

			mmr->fg.flagsMutable.hadLocalState = 1;

			mmLogResource(mm, mmr, "Updated something from BG");

			// Make sure it's in the FG list.

			if(!mmr->fg.flags.inList){
				if(	mmr->handle ||
					!mgState.flags.isServer)
				{
					ASSERT_FALSE_AND_SET(mmr->fg.flagsMutable.inListBG);
				}
				
				mmAddResourceToListFG(mm, mmr);
			}

			// Get state updates from BG.

			if(eaSize(&toFG->states)){
				mmLogResource(mm, mmr, "Before new states from BG");

				if(!mmr->fg.flags.sentDestroyToBG){
					EARRAY_CONST_FOREACH_BEGIN(toFG->states, j, jsize);
					{
						MovementManagedResourceState* mmrs = toFG->states[j];
						
						assert(!mmrs->fg.flags.inList);
						
						mmResourceInsertStateFG(mm,
												mmr,
												mmrs);
					}
					EARRAY_FOREACH_END;
				}

				eaDestroy(&toFG->statesMutable);
				
				if(!mgState.flags.isServer){
					mmResourcePruneStatesFG(mm,
											td,
											mmr,
											!!mm->fg.mcma);
				}
				
				mmLogResource(mm, mmr, "After new states from BG");
				
				if(mgState.flags.isServer){
					mmResourceSetHasUnsentStatesFG(mm, mmr);
				}

				// Check that the types are in range.

				EARRAY_CONST_FOREACH_BEGIN(mmr->fg.states, j, jsize);
				{
					MovementManagedResourceState* mmrs = mmr->fg.states[j];

					assert(	mmrs->mmrsType >= 0 &&
							mmrs->mmrsType < MMRST_COUNT);
				}
				EARRAY_FOREACH_END;
			}
			
			// Check for remove requests.
			
			if(eaSize(&toFG->removeStates)){
				EARRAY_CONST_FOREACH_BEGIN(toFG->removeStates, j, jsize);
				{
					MovementManagedResourceState*	mmrs = toFG->removeStates[j];
					S32								index;
					
					index = eaFind(&mmr->fg.states, mmrs);
					
					if(index >= 0){
						mmLog(	mm,
								NULL,
								"[Resource] Received remove for resource state 0x%8.8p:0x%8.8p"
								" (index %u)",
								mmr,
								mmrs,
								index);

						mmrs->fg.flagsMutable.gotRemoveRequestFromBG = 1;

						mmResourceRemoveStateFG(mm, td, mmr, index);
					}else{
						mmrsTrackFlags(	mmrs,
										MMRS_TRACK_ALLOCED,
										MMRS_TRACK_IN_FG,
										0,
										0);
										
						assert(!mmrs->fg.flags.inList);

						mmLog(	mm,
								NULL,
								"[Resource] Received remove for resource state 0x%8.8p:0x%8.8p"
								" (not in FG list)",
								mmr,
								mmrs);
					}
				}
				EARRAY_FOREACH_END;
				
				eaDestroy(&toFG->removeStatesMutable);
			}

			// Check if the BG is done with this resource.

			if(TRUE_THEN_RESET(toFG->flagsMutable.destroyed)){
				mmLogResource(mm, mmr, "Destroyed from BG");

				assert(mmr->fg.flags.sentDestroyToBG);
				
				mmResourceDestroyInternalFG(mm, mmr, TRUE_THEN_RESET(toFG->flagsMutable.cleared));
			}
		}
		EARRAY_FOREACH_END;

		// Clear the toFG array.

		if(td->toFG.updatedResources){
			if(eaSize(&td->toFG.updatedResources) < 10){
				eaSetSize(&td->toFG.updatedResourcesMutable, 0);
			}else{
				eaDestroy(&td->toFG.updatedResourcesMutable);
			}
		}

		PERFINFO_AUTO_STOP();
	}
}

void mmResourcesSendToClientFG(	MovementManager* mm,
								Packet* pak,
								S32 isLocalEntity,
								S32 fullUpdate)
{
	EARRAY_CONST_FOREACH_BEGIN(mm->fg.resources, i, size);
	{
		MovementManagedResource*	mmr = mm->fg.resources[i];
		S32							doSendUpdate = fullUpdate;

		if(mmr->fg.flags.sentDestroy){
			doSendUpdate = 0;
		}
		else if(!doSendUpdate){
			if(mmr->fg.flags.hasUnsentStates){
				doSendUpdate =	mmr->handle ||
								!mmr->fg.flags.sentCreate;
			}
		}
		else if(!mmr->handle &&
				mmr->fg.flags.sentCreate)
		{
			doSendUpdate = 0;
		}

		if(!doSendUpdate){
			continue;
		}

		// Updated.

		pktSendBits(pak, 1, 1);

		if(mmr->handle){
			// Has a handle.

			pktSendBits(pak, 1, 1);
			pktSendBits(pak, 32, mmr->handle);
		}else{
			pktSendBits(pak, 1, 0);
		}

		if(mmr->fg.flags.sentCreate){
			assert(mmr->handle);
		}

		if(	fullUpdate ||
			!mmr->fg.flags.sentCreate)
		{
			mmLogResource(mm, mmr, "Sending create");

			{
				MovementManagedResourceClass* mmrc;

				assert(mmGetManagedResourceClassByID(mmr->mmrc->id, &mmrc));

				assert(mmrc == mmr->mmrc);
			}

			MM_CHECK_STRING_WRITE(pak, "newresource");

			pktSendBits(pak, 8, mmr->mmrc->id);

			ParserSend(	mmr->mmrc->pti.constant,
						pak,
						NULL,
						mmr->userStruct.constant,
						SENDDIFF_FLAG_FORCEPACKALL, 0, 0, NULL);

			if(!mmr->userStruct.constantNP){
				pktSendBits(pak, 1, 0);
			}else{
				pktSendBits(pak, 1, 1);
				
				ParserSend(	mmr->mmrc->pti.constantNP,
							pak,
							NULL,
							mmr->userStruct.constantNP,
							SENDDIFF_FLAG_FORCEPACKALL, 0, 0, NULL);
			}

			if(isLocalEntity){
				// Send the requester ID so resource can be matched to its owner.
				
				pktSendBits(pak, 32, SAFE_MEMBER(mmr->mr, handle));
			}
		}

		MM_CHECK_STRING_WRITE(pak, "states");

		if(!verify(eaSize(&mmr->fg.states))){
			pktSendBitsPack(pak, 1, 0);
		}else{
			S32 stateCount = eaSize(&mmr->fg.states);
			S32 first = 0;

			if(!fullUpdate){
				EARRAY_FOREACH_REVERSE_BEGIN(mmr->fg.states, j);
				{
					MovementManagedResourceState* mmrs = mmr->fg.states[j];

					if(mmrs->fg.flags.sentToClients){
						first = j + 1;
						break;
					}
				}
				EARRAY_FOREACH_END;
			}

			stateCount -= first;

			pktSendBitsPack(pak, 1, stateCount);

			EARRAY_CONST_FOREACH_BEGIN_FROM(mmr->fg.states, j, jsize, first);
			{
				MovementManagedResourceState* mmrs = mmr->fg.states[j];

				assert(stateCount);

				stateCount--;

				assert(	mmrs->mmrsType >= 0 &&
						mmrs->mmrsType < MMRST_COUNT);

				pktSendBits(pak, 3, mmrs->mmrsType);
				pktSendBits(pak, 32, mmrs->spc);
				
				// Send state.
				
				if(!mmrs->userStruct.state){
					pktSendBits(pak, 1, 0);
				}else{
					pktSendBits(pak, 1, 1);
					
					ParserSend(	mmr->mmrc->pti.state,
								pak,
								NULL,
								mmrs->userStruct.state,
								SENDDIFF_FLAG_FORCEPACKALL, 0, 0, NULL);
				}
			}
			EARRAY_FOREACH_END;
		}
	}
	EARRAY_FOREACH_END;

	pktSendBits(pak, 1, 0);
}

static void mmFindNetResourceFG(MovementManagedResource** mmrOut,
								MovementManager* mm,
								MovementThreadData* td,
								U32 handle)
{
	MovementManagedResource* mmr;

	if(!handle){
		*mmrOut = NULL;
		return;
	}

	EARRAY_FOREACH_REVERSE_BEGIN(mm->fg.resources, i);
	{
		mmr = mm->fg.resources[i];

		if(	mmr->fg.flags.hasNetState &&
			mmr->handle == handle)
		{
			*mmrOut = mmr;
			return;
		}
	}
	EARRAY_FOREACH_END;
}

static void mmReceiveNewResourceFG(	MovementManager* mm,
									MovementThreadData* td,
									Packet* pak,
									S32 isLocalEntity,
									U32 handle,
									MovementManagedResource** mmrOut)
{
	// Resource wasn't received yet, so receive the data
	//   and then figure out if it already exists.

	U32								id;
	MovementManagedResourceClass*	mmrc;
	void*							constant;
	void*							constantNP = NULL;
	MovementRequester*				mrOwner = NULL;
	MovementManagedResource*		mmr = NULL;

	MM_CHECK_STRING_READ(pak, "newresource");

	id = pktGetBits(pak, 8);

	if(!mmGetManagedResourceClassByID(id, &mmrc)){
		assertmsgf(0, "Invalid resource ID: %u", id);
	}

	// Receive constant into a temporary struct.

	mmResourceConstantAlloc(mm,
							mmrc,
							&constant,
							NULL);
	
	START_BIT_COUNT_STATIC(	pak,
							&mmrc->perfInfo[MMRC_PT_NET_RECEIVE].perfInfo,
							mmrc->perfInfo[MMRC_PT_NET_RECEIVE].name);
	{
		START_BIT_COUNT(pak, "constant");
		ParserRecv(	mmrc->pti.constant,
					pak,
					constant,
					0);
		STOP_BIT_COUNT(pak);			

		if(pktGetBits(pak, 1)){
			START_BIT_COUNT(pak, "constantNP");
			mmResourceConstantAlloc(mm,
									mmrc,
									NULL,
									&constantNP);

			ParserRecv(	mmrc->pti.constantNP,
						pak,
						constantNP,
						0);
			STOP_BIT_COUNT(pak);			
		}
	}
	STOP_BIT_COUNT(pak);
	
	if(mmrc->msgHandler){
		MovementManagedResourceMsgPrivateData pd;

		mmResourceMsgInit(	&pd,
							NULL,
							mm,
							NULL,
							MMR_MSG_FG_TRANSLATE_SERVER_TO_CLIENT,
							MM_FG_SLOT);

		pd.msg.in.fg.translateServerToClient.constant = constant;
		pd.msg.in.fg.translateServerToClient.constantNP = constantNP;

		mmrc->msgHandler(&pd.msg);
	}

	if(isLocalEntity){
		// Find the requester that owns this resource.

		U32 mrHandle = pktGetBits(pak, 32);

		EARRAY_CONST_FOREACH_BEGIN(mm->fg.requesters, i, size);
		{
			MovementRequester* mr = mm->fg.requesters[i];

			if(	!mr->fg.flags.destroyed &&
				mr->fg.netHandle == mrHandle)
			{
				mrOwner = mr;
				break;
			}
		}
		EARRAY_FOREACH_END;
	}

	// Check if there's an existing resource that matches.

	EARRAY_CONST_FOREACH_BEGIN(mm->fg.resources, i, size);
	{
		MovementManagedResource* mmrLocal = mm->fg.resources[i];

		if(	mmrLocal->mmrc == mmrc &&
			!mmrLocal->fg.flags.hasNetState &&
			!mmrLocal->fg.flags.sentDestroyToBG &&
			(	!isLocalEntity ||
				mmrLocal->mr == mrOwner) &&
			!!handle == !!mmrLocal->handle &&
			!StructCompare(	mmrc->pti.constant,
							constant,
							mmrLocal->userStruct.constant,
							0, 0, 0))
		{
			mmr = mmrLocal;
			break;
		}
	}
	EARRAY_FOREACH_END;

	if(!mmr){
		// No existing resource matched.
		
		mmResourceAlloc(&mmr);

		mmr->handle = handle;
		mmr->bg.handle = handle;

		mmr->bg.flagsMutable.hasNetState = 1;
		
		mmr->mmrc = mmrc;
		mmr->userStruct.constant = constant;
		mmr->userStruct.constantNP = constantNP;
		
		mmr->mr = mrOwner;
		
		mmAddResourceToListFG(mm, mmr);

		mmRareLockEnter(mm);
		{
			eaPush(&mm->allResourcesMutable, mmr);
		}
		mmRareLockLeave(mm);
		
		mmLogResource(mm, mmr, "Received unpredicted resource from server");
	}else{
		// Free the received constants.
		
		mmResourceConstantFree(	mm,
								mmrc,
								&constant,
								&constantNP);
								
		// Hooray, found existing resource.
		
		mmr->handle = handle;

		// Tell the BG about the new handle.

		mmr->toBG[MM_FG_SLOT].flagsMutable.receivedServerCreate = 1;
		mmr->toBG[MM_FG_SLOT].newHandle = handle;

		mmLogResource(mm, mmr, "Received predicted resource from server");
	}

	mmResourceMarkAsUpdatedToBG(mm, td, mmr);

	*mmrOut = mmr;
}

static void mmReceiveResourceStatesFG(	MovementManager* mm,
										MovementThreadData* td,
										MovementManagedResource* mmr,
										RecordedResource* recRes,
										Packet* pak,
										S32 isLocalEntity)
{
	S32 stateCount;
	
	START_BIT_COUNT(pak, "stateCount");
		stateCount = pktGetBitsPack(pak, 1);
	STOP_BIT_COUNT(pak);

	if(stateCount){
		// Receive resource states.

		FOR_BEGIN(i, stateCount);
			MovementManagedResourceStateType	mmrsType;
			U32									spc;
			MovementManagedResourceState*		mmrs;
			
			START_BIT_COUNT_STATIC(	pak,
									&mmr->mmrc->perfInfo[MMRC_PT_NET_RECEIVE].perfInfo,
									mmr->mmrc->perfInfo[MMRC_PT_NET_RECEIVE].name);
			{
				START_BIT_COUNT(pak, "state");
				{
					START_BIT_COUNT(pak, "mmrsType");
						mmrsType = pktGetBits(pak, 3);
					STOP_BIT_COUNT(pak);
					
					START_BIT_COUNT(pak, "spc");
						spc = pktGetBits(pak, 32);
					STOP_BIT_COUNT(pak);
				}
				STOP_BIT_COUNT(pak);
				
				mmResourceStateCreate(	mm,
										mmr,
										&mmrs,
										mmrsType,
										NULL,
										spc);
										
				if(pktGetBits(pak, 1)){
					mmStructAllocIfNull(mmr->mmrc->pti.state,
										mmrs->userStruct.state,
										mm);

					START_BIT_COUNT(pak, "state struct");
					ParserRecv(	mmr->mmrc->pti.state,
								pak,
								mmrs->userStruct.state,
								0);
					STOP_BIT_COUNT(pak);
				}
			}
			STOP_BIT_COUNT(pak);
			
			mmrs->fg.flagsMutable.isNetState = 1;
			mmrs->bg.flagsMutable.isNetState = 1;
			
			if(FALSE_THEN_SET(mmr->fg.flagsMutable.hasNetState)){
				mmr->fg.spcNetCreate = spc;
			}

			mmrs->fg.received.frameCount = mmGetFrameCount();
			mmrs->fg.received.pc.client = mgState.fg.netReceive.cur.pc.client;
			mmrs->fg.received.pc.server = mgState.fg.netReceive.cur.pc.server;
			mmrs->fg.received.pc.serverSync = mgState.fg.netReceive.cur.pc.serverSync;

			mmLogResource(	mm,
							mmr,
							"Received new state 0x%8.8p:%u:%u (%u)",
							mmrs,
							mmrs->mmrsType,
							mmrs->spc,
							mgState.fg.netReceive.cur.pc.serverSync);
			
			mmResourceInsertStateFG(mm,
									mmr,
									mmrs);

			eaPush(	&mmr->toBG[MM_FG_SLOT].statesMutable,
					mmrs);

			mmResourceMarkAsUpdatedToBG(mm, td, mmr);

			// Record this state (if we're recording).

			if(recRes){
				RecordedMMRState *recState = StructCreate(parse_RecordedMMRState);
				recState->processCount = spc;
				recState->type = mmrsType;

				if(mmrs->userStruct.state){
					ParserWriteTextEscaped(	&recState->state,
											mmr->mmrc->pti.state,
											mmrs->userStruct.state,
											0, 0, 0);
				}
				
				eaPush(&recRes->states, recState);
			}
		FOR_END;
		
		mmResourcePruneStatesFG(mm,
								td,
								mmr,
								isLocalEntity);
		
		mmLogResource(	mm,
						mmr,
						"Received %u resource states",
						stateCount);
	}
}

static void mmRecordResourceReceiveFG(	RecordedEntityUpdate* recUpdate,
										RecordedResource** recResOut,
										MovementManagedResource* mmr)
{
	// Record the resource's creation if we're recording.
	// This writes the constant data every time; we really only need it once, the first
	// time this resource is created during playback.  We may want to keep a list of
	// which resources the demo recorder has seen.

	RecordedResource* recRes = *recResOut = StructCreate(parse_RecordedResource);
	
	recRes->handle = mmr->handle;
	recRes->id = mmr->mmrc->id;
	
	ParserWriteTextEscaped(	&recRes->constant,
							mmr->mmrc->pti.constant,
							mmr->userStruct.constant,
							0, 0, 0);

	if(mmr->userStruct.constantNP){
		ParserWriteTextEscaped(	&recRes->constantNP,
								mmr->mmrc->pti.constantNP,
								mmr->userStruct.constantNP,
								0, 0, 0);
	}

	// Push the recorded resource to the list of recorded resources.
	
	eaPush(	&recUpdate->resources,
			recRes);
}

void mmReceiveResourcesFG(	MovementManager* mm,
							MovementThreadData* td,
							Packet* pak,
							S32 isLocalEntity,
							S32 fullUpdate,
							RecordedEntityUpdate* recUpdate)
{
	S32 firstNewResource = 1;

	START_BIT_COUNT(pak, "resources");

	while(pktGetBits(pak, 1)){
		U32							handle = 0;
		RecordedResource*			recRes = NULL;
		MovementManagedResource*	mmr = NULL;
		S32							isNewResource = 0;

		if(pktGetBits(pak, 1) == 1){
			// Has handle, so find the existing resource.

			START_BIT_COUNT(pak, "handle");
				handle = pktGetBits(pak, 32);
			STOP_BIT_COUNT(pak);

			mmFindNetResourceFG(&mmr,
								mm,
								td,
								handle);
		}

		if(!mmr){
			isNewResource = 1;
			
			if(TRUE_THEN_RESET(firstNewResource)){
				mmLogResource(mm, NULL, "Resources before receiving new resource");
			}

			START_BIT_COUNT(pak, "newResource");
				mmReceiveNewResourceFG(	mm,
										td,
										pak,
										isLocalEntity,
										handle,
										&mmr);
			STOP_BIT_COUNT(pak);
		}

		if(recUpdate){
			mmRecordResourceReceiveFG(	recUpdate,
										&recRes,
										mmr);
		}

		MM_CHECK_STRING_READ(pak, "states");

		START_BIT_COUNT(pak, "states");
			mmReceiveResourceStatesFG(	mm,
										td,
										mmr,
										recRes,
										pak,
										isLocalEntity);
		STOP_BIT_COUNT(pak);

		mmResourceSetNeedsSetStateFG(mm, mmr, "received a new state");
	}
	
	STOP_BIT_COUNT(pak);
}

void mmReceiveResourceFromReplayFG(	MovementManager* mm,
									RecordedResource* recRes)
{
	MovementManagedResource*	mmr = NULL;
	MovementThreadData*			td = MM_THREADDATA_FG(mm);
	S32							isNewResource = 0;

	mmFindNetResourceFG(&mmr,
						mm,
						td,
						recRes->handle);

	if(!mmr){
		isNewResource = 1;

		mmResourceAlloc(&mmr);

		mmr->handle = recRes->handle;
		mmr->bg.handle = recRes->handle;

		if(!mmGetManagedResourceClassByID(recRes->id, &mmr->mmrc)){
			assertmsgf(0, "Invalid resource ID: %u", recRes->id);
		}

		mmResourceConstantAlloc(mm,
								mmr->mmrc,
								&mmr->userStruct.constant,
								&mmr->userStruct.constantNP);

		{
			char* constant = recRes->constant;
			char* constantNP = recRes->constantNP;

			ParserReadTextEscaped(	&constant,
									mmr->mmrc->pti.constant,
									mmr->userStruct.constant,
									0);
			
			if(constantNP){
				ParserReadTextEscaped(	&constantNP,
										mmr->mmrc->pti.constantNP,
										mmr->userStruct.constantNP,
										0);
			}
		}

		// Demos need a proper implementation at some point.  This is a dirty fix for backward
		//   compatibility with structure changes.
		{
			MovementManagedResourceMsgPrivateData	pd;

			mmResourceMsgInitFG(&pd,
								NULL,
								mm,
								mmr,
								MMR_MSG_FG_TEMP_FIX_FOR_DEMO);

			pd.msg.in.fg.tempFixForDemo.constant = mmr->userStruct.constant;
			pd.msg.in.fg.tempFixForDemo.constantNP = mmr->userStruct.constantNP;

			mmResourceMsgSend(&pd);
		}		

		if(	!eaPush(&mm->fg.resourcesMutable, mmr) &&
			!mgState.flags.isServer)
		{
			mmHandleAfterSimWakesIncFG(mm, "has mmr", __FUNCTION__);
		}
				
		mmRareLockEnter(mm);
		{
			eaPush(&mm->allResourcesMutable, mmr);
		}
		mmRareLockLeave(mm);

		ASSERT_FALSE_AND_SET(mmr->fg.flagsMutable.inList);

		mmr->fg.flagsMutable.hasNetState = 1;
	}

	EARRAY_CONST_FOREACH_BEGIN(recRes->states, i, isize);
	{
		RecordedMMRState*					recState = recRes->states[i];
		MovementManagedResourceState*		mmrs;

		mmResourceStateCreate(	mm,
								mmr,
								&mmrs,
								recState->type,
								NULL,
								recState->processCount);

		if(recState->state){
			char* state = recState->state;
			
			mmStructAllocIfNull(mmr->mmrc->pti.state,
								mmrs->userStruct.state,
								mm);

			ParserReadTextEscaped(	&state,
									mmr->mmrc->pti.state,
									mmrs->userStruct.state,
									0);
		}
		
		mmResourceInsertStateFG(mm,
								mmr,
								mmrs);

		eaPush( &mmr->toBG[MM_FG_SLOT].statesMutable,
				mmrs);

		mmrs->fg.flagsMutable.isNetState = 1;
		mmrs->bg.flagsMutable.isNetState = 1;

		mmResourceMarkAsUpdatedToBG(mm, td, mmr);
	}
	EARRAY_FOREACH_END;

	if(isNewResource){
		mmLogResource(mm, mmr, "Received resource from demo playback");
	}
}

void mmResourcesAfterSendingToClients(MovementManager* mm){
	PERFINFO_AUTO_START_FUNC();

	EARRAY_CONST_FOREACH_BEGIN(mm->fg.resources, i, isize);
	{
		MovementManagedResource* mmr = mm->fg.resources[i];

		if(TRUE_THEN_RESET(mmr->fg.flagsMutable.hasUnsentStates)){
			mmr->fg.flagsMutable.sentCreate = 1;

			mmResourceSetNeedsSetStateFG(mm, mmr, "after sending to clients");

			EARRAY_CONST_FOREACH_BEGIN(mmr->fg.states, j, jsize);
			{
				MovementManagedResourceState* mmrs = mmr->fg.states[j];
				
				mmrs->fg.flagsMutable.sentToClients = 1;

				if(mmrs->mmrsType == MMRST_DESTROYED || mmrs->mmrsType == MMRST_CLEARED){
					mmr->fg.flagsMutable.sentDestroy = 1;
				}
			}
			EARRAY_FOREACH_END;
		}
	}
	EARRAY_FOREACH_END;
	
	PERFINFO_AUTO_STOP();
}

void mmDestroyAllResourcesFG(MovementManager* mm){
	PERFINFO_AUTO_START_FUNC();
	
	assert(mm->fg.flags.destroyedFromBG);

	EARRAY_CONST_FOREACH_BEGIN(mm->allResources, i, isize);
	{
		MovementManagedResource* mmr = mm->allResources[i];

		if(mmr->handle){
			mmResourceSendMsgDestroyedFG(mm, mmr, false, "All resources freed");
		}
	}
	EARRAY_FOREACH_END;
	
	EARRAY_CONST_FOREACH_BEGIN(mm->allResources, i, isize);
	{
		MovementManagedResource* mmr = mm->allResources[i];

		mmr->fg.flagsMutable.inList = 0;
		mmr->bg.flagsMutable.inList = 0;
		
		mmResourceFreeFG(mm, &mmr);
	}
	EARRAY_FOREACH_END;

	eaDestroy(&mm->allResourcesMutable);
	if(	eaSize(&mm->fg.resources) &&
		!mgState.flags.isServer)
	{
		mmHandleAfterSimWakesDecFG(mm, "has mmr");
	}
	eaDestroy(&mm->fg.resourcesMutable);
	eaDestroy(&mm->bg.resourcesMutable);

	if(TRUE_THEN_RESET(mm->fg.flagsMutable.mmrNeedsSetState)){
		mmHandleAfterSimWakesDecFG(mm, "mmrNeedsSetState");
	}
	
	if(TRUE_THEN_RESET(mm->fg.flagsMutable.mmrWaitingForWake)){
		mmHandleAfterSimWakesDecFG(mm, "mmrWaitingForWake");
	}

	PERFINFO_AUTO_STOP();
}

void mmResourceGetNewHandle(MovementManager* mm,
							U32* handleOut)
{
	mmRareLockEnter(mm);
	
	while(1){
		S32 found = 0;

		while(!++mm->lastResourceHandle);

		EARRAY_CONST_FOREACH_BEGIN(mm->allResources, i, size);
		{
			MovementManagedResource* mmr = mm->allResources[i];

			if(mmr->handle == mm->lastResourceHandle){
				found = 1;
				break;
			}
		}
		EARRAY_FOREACH_END;

		if(!found){
			*handleOut = mm->lastResourceHandle;
			break;
		}
	}
	
	mmRareLockLeave(mm);
}

S32 mmResourceCreateFG(	MovementManager* mm,
						U32* handleOut,
						U32 resourceID,
						const void* constant,
						const void* constantNP,
						const void* state)
{
	MovementThreadData*				td;
	MovementManagedResourceClass*	mmrc;
	MovementManagedResource*		mmr;
	MovementManagedResourceState*	mmrs;
	U32								handle = 0;

	if(	!mm ||
		!mmGetManagedResourceClassByID(resourceID, &mmrc) ||
		!constant ||
		!handleOut)
	{
		return 0;
	}
	
	td = MM_THREADDATA_FG(mm);

	// Get a new handle.

	mmResourceGetNewHandle(mm, &handle);
	*handleOut = handle;

	mmResourceAlloc(&mmr);
	
	assert(mmrc);

	mmr->mmrc = mmrc;
	mmr->handle = handle;

	mmr->fg.flagsMutable.hadLocalState = 1;

	mmr->bg.handle = handle;
	mmr->bg.flagsMutable.hadLocalState = 1;

	mmAddResourceToListFG(mm, mmr);

	mmr->fg.flagsMutable.inListBG = 1;

	mmResourceMarkAsUpdatedToBG(mm, td, mmr);
	
	mmRareLockEnter(mm);
	{
		eaPush(&mm->allResourcesMutable, mmr);
	}
	mmRareLockLeave(mm);

	// Copy the constants.
	
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

	// Create the CREATED state and add it to the list.

	mmResourceStateCreate(	mm,
							mmr,
							&mmrs,
							MMRST_CREATED,
							state,
							mgState.fg.frame.cur.pcStart +
								mgState.fg.netReceive.cur.offset.clientToServerSync);
							
	mmResourceSetHasUnsentStatesFG(mm, mmr);

	mmResourceStateSetInListFG(mmrs);

	mmResourceSetNeedsSetStateFG(mm, mmr, "created in FG");

	eaPush(	&mmr->fg.statesMutable,
			mmrs);

	eaPush(	&mmr->toBG[MM_FG_SLOT].statesMutable,
			mmrs);

	mmLogResource(	mm,
					mmr,
					"Created state %p:%u:%u",
					mmrs,
					mmrs->mmrsType,
					mmrs->spc);
	
	return 1;
}

void mmResourceDestroyFG(	MovementManager* mm,
							U32* handleInOut)
{
	MovementThreadData*				td = MM_THREADDATA_FG(mm);
	U32								handle = SAFE_DEREF(handleInOut);
	MovementManagedResource*		mmr = NULL;
	MovementManagedResourceState*	mmrs;
	
	if(!handle){
		return;
	}
	
	*handleInOut = 0;

	EARRAY_CONST_FOREACH_BEGIN(mm->fg.resources, i, isize);
	{
		MovementManagedResource* mmrTemp = mm->fg.resources[i];
		
		if(mmrTemp->handle == handle){
			mmr = mmrTemp;
			break;
		}
	}
	EARRAY_FOREACH_END;
	
	if(!mmr){
		return;
	}
	
	mmResourceMarkAsUpdatedToBG(mm, td, mmr);

	mmResourceStateCreate(	mm,
							mmr,
							&mmrs,
							MMRST_DESTROYED,
							NULL,
							mgState.bg.pc.server.cur);

	mmResourceSetHasUnsentStatesFG(mm, mmr);

	mmResourceStateSetInListFG(mmrs);

	mmLogResource(	mm,
					mmr,
					"Created state %p:%u:%u",
					mmrs,
					mmrs->mmrsType,
					mmrs->spc);

	mmResourceSetNeedsSetStateFG(mm, mmr, "destroyed in FG");

	eaPush(	&mmr->fg.statesMutable,
			mmrs);

	eaPush(	&mmr->toBG[MM_FG_SLOT].statesMutable,
			mmrs);
}

void mmResourcesDebugDrawFG(MovementManager* mm,
							const MovementDrawFuncs* funcs)
{
	EARRAY_CONST_FOREACH_BEGIN(mm->fg.resources, i, isize);
	{
		MovementManagedResource*				mmr = mm->fg.resources[i];
		MovementManagedResourceMsgPrivateData	pd;
		Quat									rot;

		mmResourceMsgInitFG(&pd,
							NULL,
							mm,
							mmr,
							MMR_MSG_FG_DEBUG_DRAW);

		pd.msg.in.handle = mmr->handle;
		pd.msg.in.activatedStruct = mmr->userStruct.activatedFG;
		pd.msg.in.fg.debugDraw.drawFuncs = funcs;

		mmGetPositionFG(mm, pd.msg.in.fg.debugDraw.matWorld[3]);
		mmGetRotationFG(mm, rot);
						
		quatToMat(rot, pd.msg.in.fg.debugDraw.matWorld);

		mmResourceMsgSend(&pd);
	}
	EARRAY_FOREACH_END;
}

void mmResourcesAlwaysDrawFG(	MovementManager* mm,
								const MovementDrawFuncs* funcs)
{
	EARRAY_CONST_FOREACH_BEGIN(mm->fg.resources, i, isize);
	{
		MovementManagedResource*				mmr = mm->fg.resources[i];
		MovementManagedResourceMsgPrivateData	pd;
		Quat									rot;
		
		if(!mmr->fg.flags.handlesAlwaysDraw){
			continue;
		}

		mmResourceMsgInitFG(&pd,
							NULL,
							mm,
							mmr,
							MMR_MSG_FG_ALWAYS_DRAW);

		pd.msg.in.handle = mmr->handle;
		pd.msg.in.activatedStruct = mmr->userStruct.activatedFG;
		pd.msg.in.fg.alwaysDraw.drawFuncs = funcs;

		mmGetPositionFG(mm, pd.msg.in.fg.debugDraw.matWorld[3]);
		mmGetRotationFG(mm, rot);
						
		quatToMat(rot, pd.msg.in.fg.alwaysDraw.matWorld);

		mmResourceMsgSend(&pd);
	}
	EARRAY_FOREACH_END;
}

S32 mmResourceFindFG(	MovementManager* mm,
						U32* startIndexInOut,
						U32 resourceID,
						const void** constantOut,
						const void** constantNPOut,
						const void** activatedOut)
{
	S32 startIndex = SAFE_DEREF(startIndexInOut);

	if(!mm){
		return 0;
	}
	
	MAX1(startIndex, 0);

	EARRAY_CONST_FOREACH_BEGIN_FROM(mm->fg.resources, i, isize, startIndex);
	{
		MovementManagedResource* mmr = mm->fg.resources[i];
		
		if(mmr->mmrc->id == resourceID){
			if(constantOut){
				*constantOut = mmr->userStruct.constant;
			}
			
			if(constantNPOut){
				*constantNPOut = mmr->userStruct.constantNP;
			}
			
			if(activatedOut){
				*activatedOut = mmr->userStruct.activatedFG;
			}
			
			if(startIndexInOut){
				*startIndexInOut = i + 1;
			}
			
			return 1;
		}
	}
	EARRAY_FOREACH_END;
	
	return 0;
}

void mmrSetAlwaysDrawFG(MovementManager* mm,
						MovementManagedResource* mmr,
						S32 enabled)
{
	if(enabled){
		if(FALSE_THEN_SET(mmr->fg.flagsMutable.handlesAlwaysDraw)){
			if(FALSE_THEN_SET(mm->fg.flagsMutable.mmrHandlesAlwaysDraw)){
				eaPush(&mgState.fg.alwaysDrawManagers, mm);
			}
		}
	}else{
		if(TRUE_THEN_RESET(mmr->fg.flagsMutable.handlesAlwaysDraw)){
			S32 found = 0;

			assert(mm->fg.flagsMutable.mmrHandlesAlwaysDraw);

			EARRAY_CONST_FOREACH_BEGIN(mm->fg.resources, i, isize);
			{
				if(mm->fg.resources[i]->fg.flags.handlesAlwaysDraw){
					found = 1;
					break;
				}
			}
			EARRAY_FOREACH_END;
			
			if(!found){
				mm->fg.flagsMutable.mmrHandlesAlwaysDraw = 0;

				if(eaFindAndRemove(&mgState.fg.alwaysDrawManagers, mm) < 0){
					assert(0);
				}
			}
		}
	}
}

S32 mmrmSetAlwaysDrawFG(const MovementManagedResourceMsg* msg,
						S32 enabled)
{
	MovementManagedResourceMsgPrivateData* pd = MMR_MSG_TO_PD(msg);
	
	if(	pd->msgType < MMR_MSG_FG_LOW ||
		pd->msgType > MMR_MSG_FG_HIGH)
	{
		return 0;
	}

	mmrSetAlwaysDrawFG(pd->mm, pd->mmr, enabled);
	
	return 1;
}

S32 mmrmSetHasAnimBitFG(const MovementManagedResourceMsg* msg){
	MovementManagedResourceMsgPrivateData* pd = MMR_MSG_TO_PD(msg);

	if(	pd->msgType < MMR_MSG_FG_LOW ||
		pd->msgType > MMR_MSG_FG_HIGH)
	{
		return 0;
	}

	pd->mm->fg.flagsMutable.mmrHasDetailAnim = 1;
	pd->mmr->fg.flagsMutable.hasDetailAnim = 1;

	mmLog(	pd->mm,
			NULL,
			"[resource] Resource 0x%p (%s[%u]): Setting hasDetailAnim.",
			pd->mmr,
			pd->mmr->mmrc->name,
			pd->mmr->handle);

	return 1;
}

S32 mmrmSetAnimBitFG(	const MovementManagedResourceMsg* msg,
						U32 bitHandle,
						S32 isKeyword)
{
	MovementManagedResourceMsgPrivateData*	pd = MMR_MSG_TO_PD(msg);
	MovementManager*						mm;
	const DynSkeletonPreUpdateParams*		params;

	if(pd->msgType != MMR_MSG_FG_SET_ANIM_BITS){
		return 0;
	}
	
	mm = pd->mm;
	params = pd->in.fg.setAnimBits.params;

	#if GAMECLIENT
		if(demo_playingBack() &&
			(	!gConf.bNewAnimationSystem ||
				isKeyword))
		{
			//the bit ids will likely be different since they will probably be added in a different order
			//than they were when the demo was recorded.. the directional flags, however, should always
			//be added in the same order since they are hard-coded
			bitHandle = mmGetAnimBitHandleByName("Hit", 0);
		}
	#endif

	if(MMLOG_IS_ENABLED(mm)){
		MovementRegisteredAnimBit* bit = NULL;

		mmGetLocalAnimBitFromHandle(&bit, bitHandle, 0);

		mmLog(	pd->mm,
				NULL,
				"[fg.curanim] Resource %s[%u] %s anim bit \"%s\" (%u).",
				pd->mmr->mmrc->name,
				pd->mmr->handle,
				pd->in.fg.setAnimBits.flags.doClearBits ? "removing" : "adding",
				SAFE_MEMBER(bit, bitName),
				bitHandle);
	}

	if(gConf.bNewAnimationSystem){
		MovementRegisteredAnimBit* bit = NULL;

		if(mmGetLocalAnimBitFromHandle(&bit, bitHandle, 0)){
			if (isKeyword)
				params->func.startDetailGraph(params->skeleton, bit->bitName, 0);
			else
				params->func.playDetailFlag(params->skeleton, bit->bitName, 0);
		}
	}else{
		if(pd->in.fg.setAnimBits.flags.doClearBits){
			mmRemoveAnimBit(mm,
							&mgState.animBitRegistry,
							&mm->fg.view->animValuesMutable,
							bitHandle,
							params);
		}else{
			mmAddAnimBit(	mm,
							&mgState.animBitRegistry,
							&mm->fg.view->animValuesMutable,
							bitHandle,
							params);
		}
	}

	return 1;
}

S32 mmrmSetWaitingForTriggerFG(	const MovementManagedResourceMsg* msg,
								S32 enabled)
{
	MovementManagedResourceMsgPrivateData*	pd = MMR_MSG_TO_PD(msg);
	MovementManager*						mm;
	MovementManagedResource*				mmr;
	
	if(	pd->msgType < MMR_MSG_FG_LOW ||
		pd->msgType > MMR_MSG_FG_HIGH)
	{
		return 0;
	}
	
	mm = pd->mm;
	mmr = pd->mmr;

	if(enabled){
		mm->fg.flagsMutable.mmrWaitingForTrigger = 1;
		mmr->fg.flagsMutable.waitingForTrigger = 1;
		
		mmLog(	mm,
				NULL,
				"[resource] Resource 0x%p:%u: Setting waitingForTrigger.",
				mmr,
				mmr->handle);
	}
	else if(TRUE_THEN_RESET(mmr->fg.flagsMutable.waitingForTrigger)){
		assert(mm->fg.flags.mmrWaitingForTrigger);
		
		mmLog(	mm,
				NULL,
				"[resource] Resource 0x%p:%u: Resetting waitingForTrigger.",
				mmr,
				mmr->handle);
	}

	return 1;
}

S32 mmrmSetNeedsSetStateFG(const MovementManagedResourceMsg* msg){
	MovementManagedResourceMsgPrivateData* pd = MMR_MSG_TO_PD(msg);

	if(	pd->msgType < MMR_MSG_FG_LOW ||
		pd->msgType > MMR_MSG_FG_HIGH)
	{
		return 0;
	}

	if(FALSE_THEN_SET(pd->mm->fg.flagsMutable.mmrNeedsSetState)){
		mmHandleAfterSimWakesIncFG(pd->mm, "mmrNeedsSetState", __FUNCTION__);
	}

	pd->mmr->fg.flagsMutable.needsSetState = 1;
	pd->mmr->fg.flagsMutable.didSetState = 0;

	mmLog(	pd->mm,
			NULL,
			"[resource] Resource 0x%p:%u: Setting needsSetState.",
			pd->mmr,
			pd->mmr->handle);

	return 1;
}

S32 mmrmSetWaitingForWakeFG(const MovementManagedResourceMsg* msg,
							U32 spc)
{
	MovementManagedResourceMsgPrivateData*	pd = MMR_MSG_TO_PD(msg);
	MovementManager*						mm;

	if(	pd->msgType < MMR_MSG_FG_LOW ||
		pd->msgType > MMR_MSG_FG_HIGH)
	{
		return 0;
	}

	mm = pd->mm;
	
	mmResourceSetWaitingForWakeFG(mm, pd->mmr, spc);

	mmLog(	mm,
			NULL,
			"[resource] Resource 0x%p:%u: Setting waitingForWake %u (mm spc %u).",
			pd->mmr,
			pd->mmr->handle,
			spc,
			mm->fg.spcWakeResource);

	return 1;
}

S32 mmrmSetNoPredictedDestroyFG(const MovementManagedResourceMsg* msg){
	MovementManagedResourceMsgPrivateData*	pd = MMR_MSG_TO_PD(msg);
	MovementManager*						mm;

	if(	mgState.flags.isServer ||
		pd->msgType < MMR_MSG_FG_LOW ||
		pd->msgType > MMR_MSG_FG_HIGH)
	{
		return 0;
	}

	mm = pd->mm;
	
	pd->mmr->fg.flagsMutable.noPredictedDestroy = 1;

	mmLog(	mm,
			NULL,
			"[resource] Resource 0x%p:%u: Setting noPredictedDestroy.",
			pd->mmr,
			pd->mmr->handle);

	return 1;
}

void mmResourcesSendMsgTrigger(	MovementManager* mm,
								const MovementTrigger* t)
{
	S32 triggerWasEaten = 0;

	EARRAY_CONST_FOREACH_BEGIN(mm->fg.resources, i, isize);
	{
		MovementManagedResource*				mmr = mm->fg.resources[i];
		MovementManagedResourceMsgPrivateData	pd;
		MovementManagedResourceMsgOut			out;
		
		if(!mmr->fg.flags.waitingForTrigger){
			continue;
		}
		
		if(triggerWasEaten){
			mm->fg.flagsMutable.mmrWaitingForTrigger = 1;
			break;
		}

		mmr->fg.flagsMutable.waitingForTrigger = 0;
		
		mmLog(	mm,
				NULL,
				"[resource] Sending trigger to resource 0x%p:%u: %s 0x%8.8x",
				mmr,
				mmr->handle,
				t->flags.isEntityID ? "entity" : "event",
				t->triggerID);

		mmResourceMsgInitFG(&pd,
							&out,
							mm,
							mmr,
							MMR_MSG_FG_TRIGGER);

		pd.msg.in.handle = mmr->handle;
		pd.msg.in.activatedStruct = mmr->userStruct.activatedFG;

		pd.msg.in.fg.trigger.trigger = t;

		mmResourceMsgSend(&pd);
		
		if(out.fg.trigger.flags.eatTrigger){
			triggerWasEaten = 1;
		}
	}
	EARRAY_FOREACH_END;
}

S32 mmResourcesPlayDetailAnimsFG(	MovementManager* mm,
									const DynSkeletonPreUpdateParams* params)
{
	S32 doClearBits;

	if(!mm->fg.flagsMutable.mmrHasDetailAnim){
		return 0;
	}

	doClearBits =	!gConf.bNewAnimationSystem &&
					!params->callCountOnThisFrame;

	if(!doClearBits){
		mm->fg.flagsMutable.mmrHasDetailAnim = 0;
	}

	EARRAY_CONST_FOREACH_BEGIN(mm->fg.resources, i, isize);
	{
		MovementManagedResource*				mmr = mm->fg.resources[i];
		MovementManagedResourceMsgPrivateData	pd;
		
		if(!mmr->fg.flags.hasDetailAnim){
			continue;
		}

		if(!doClearBits){
			mmr->fg.flagsMutable.hasDetailAnim = 0;
		}

		mmResourceMsgInitFG(&pd,
							NULL,
							mm,
							mmr,
							MMR_MSG_FG_SET_ANIM_BITS);

		pd.msg.in.handle = mmr->handle;
		pd.msg.in.activatedStruct = mmr->userStruct.activatedFG;
		
		pd.in.fg.setAnimBits.params = params;
		pd.in.fg.setAnimBits.flags.doClearBits = doClearBits;

		mmResourceMsgSend(&pd);
	}
	EARRAY_FOREACH_END;

	return !doClearBits;
}

void mmResourcesCheckForInvalidStateFG(MovementManager* mm){
	if(!mm){
		return;
	}
	
	mmLogResource(	mm,
					NULL,
					"Before checking for invalid resources.");
	
	EARRAY_CONST_FOREACH_BEGIN(mm->fg.resources, i, isize);
	{
		MovementManagedResource*				mmr = mm->fg.resources[i];
		MovementManagedResourceMsgPrivateData	pd;

		if(!mmr->userStruct.activatedFG){
			continue;
		}

		mmResourceMsgInitFG(&pd,
							NULL,
							mm,
							mmr,
							MMR_MSG_FG_CHECK_FOR_INVALID_STATE);

		pd.msg.in.handle = mmr->handle;
		pd.msg.in.activatedStruct = mmr->userStruct.activatedFG;
		
		mmResourceMsgSend(&pd);
	}
	EARRAY_FOREACH_END;

	mmLogResource(	mm,
					NULL,
					"After checking for invalid resources.");
}

void mmLogResourceStatesFG(MovementManager* mm){
	EARRAY_CONST_FOREACH_BEGIN(mm->fg.resources, i, isize);
	{
		MovementManagedResource*				mmr = mm->fg.resources[i];
		MovementManagedResourceMsgPrivateData	pd;

		if(!mmr->userStruct.activatedFG){
			continue;
		}

		mmResourceMsgInitFG(&pd,
							NULL,
							mm,
							mmr,
							MMR_MSG_FG_LOG_STATE);

		pd.msg.in.handle = mmr->handle;
		pd.msg.in.activatedStruct = mmr->userStruct.activatedFG;
		
		mmResourceMsgSend(&pd);
	}
	EARRAY_FOREACH_END;
}

void mmResourcesSendMsgBodiesDestroyedFG(MovementManager* mm){
	EARRAY_CONST_FOREACH_BEGIN(mm->fg.resources, i, isize);
	{
		MovementManagedResource*				mmr = mm->fg.resources[i];
		MovementManagedResourceMsgPrivateData	pd;

		if(!mmr->userStruct.activatedFG){
			continue;
		}

		mmResourceMsgInitFG(&pd,
							NULL,
							mm,
							mmr,
							MMR_MSG_FG_ALL_BODIES_DESTROYED);

		pd.msg.in.handle = mmr->handle;
		pd.msg.in.activatedStruct = mmr->userStruct.activatedFG;
		
		mmResourceMsgSend(&pd);
	}
	EARRAY_FOREACH_END;
}

void mmResourcesSetNeedsSetStateIfHasUnsentStatesFG(MovementManager* mm){
	EARRAY_CONST_FOREACH_BEGIN(mm->fg.resources, i, isize);
	{
		MovementManagedResource* mmr = mm->fg.resources[i];
		
		if(mmr->fg.flags.hasUnsentStates){
			mmResourceSetNeedsSetStateFG(mm, mmr, "No more clients");
		}
	}
	EARRAY_FOREACH_END;
}

#if MM_TRACK_ALL_RESOURCE_REMOVE_STATES_TOBG
static CRITICAL_SECTION csRemoveStates;

void mmResourceDebugAddRemoveStatesToBG(MovementManagedResource* mmr,
										const MovementManagedResourceState*const* removeStates)
{
	if(!removeStates){
		return;
	}
	
	ATOMIC_INIT_BEGIN;
	{
		InitializeCriticalSection(&csRemoveStates);
	}
	ATOMIC_INIT_END;
	
	EnterCriticalSection(&csRemoveStates);
	if(!mgState.debug.stRemoveStatesToBG){
		mgState.debug.stRemoveStatesToBG = stashTableCreateAddress(1000);
	}
	if(!stashAddPointer(mgState.debug.stRemoveStatesToBG, removeStates, mmr, false)){
		assert(0);
	}
	LeaveCriticalSection(&csRemoveStates);
}

void mmResourceDebugRemoveRemoveStatesToBG(	MovementManagedResource* mmr,
											const MovementManagedResourceState*const* removeStates)
{
	if(!removeStates){
		return;
	}
	
	EnterCriticalSection(&csRemoveStates);
	if(!stashRemovePointer(mgState.debug.stRemoveStatesToBG, removeStates, NULL)){
		assert(0);
	}
	LeaveCriticalSection(&csRemoveStates);
}
#endif

#if MM_TRACK_RESOURCE_STATE_FLAGS
struct {
	CRITICAL_SECTION	cs;
	StashTable			st;
} trackStates;

void mmrsTrackFlags(MovementManagedResourceState* mmrs,
					U32 flagsToAssertSet,
					U32 flagsToAssertUnset,
					U32 flagsToSet,
					U32 flagsToReset)
{
	StashElement	e;
	U32				flags = 0;

	ATOMIC_INIT_BEGIN;
		InitializeCriticalSection(&trackStates.cs);
		trackStates.st = stashTableCreateAddress(100);
	ATOMIC_INIT_END;
	
	EnterCriticalSection(&trackStates.cs);
	
	if(!stashFindElement(trackStates.st, mmrs, &e)){
		assertmsgf(	!flagsToAssertSet,
					"mmrs %p: Expected set flags 0x%x when mmrs never existed.",
					mmrs,
					flagsToAssertSet);
		
		if(!stashAddIntAndGetElement(trackStates.st, mmrs, 0, false, &e)){
			assert(0);
		}
	}else{
		flags = stashElementGetInt(e);
		
		assertmsgf(	!(~flags & flagsToAssertSet),
					"mmrs %p: Flags 0x%x set, 0x%x expected set (missing 0x%x).",
					mmrs,
					flags,
					flagsToAssertSet,
					~flags & flagsToAssertSet);

		assertmsgf(	!(flags & flagsToAssertUnset),
					"mmrs %p: Flags 0x%x set, 0x%x expected unset (has 0x%x).",
					mmrs,
					flags,
					flagsToAssertSet,
					flags & flagsToAssertUnset);
	}
	
	if(	flagsToSet ||
		flagsToReset)
	{
		assertmsgf(	!(flagsToSet & flagsToReset),
					"mmrs %p: Flags to set 0x%x overlaps flags to reset 0x%x (0x%x).",
					mmrs,
					flagsToSet,
					flagsToReset,
					flagsToSet & flagsToReset);

		flags |= flagsToSet;
		flags &= ~flagsToReset;
		
		stashElementSetInt(e, flags);
	}
	
	LeaveCriticalSection(&trackStates.cs);
}
#endif

S32 mmResourcesCopyFromManager(	MovementManager* mm,
								const MovementManager* mmSource,
								U32 id)
{
	if(	!mm ||
		!mmSource)
	{
		return 0;
	}
	
	EARRAY_CONST_FOREACH_BEGIN(mmSource->fg.resources, i, isize);
	{
		MovementManagedResource*				mmrSource = mmSource->fg.resources[i];
		U32										handle;
		MovementManagedResourceMsgPrivateData	pd;
		MovementManagedResource*				mmr;

		if(mmrSource->mmrc->id != id){
			continue;
		}
		
		if(!mmResourceCreateFG(	mm,
								mmrSource->handle ? &handle : NULL,
								id,
								mmrSource->userStruct.constant,
								mmrSource->userStruct.constantNP,
								NULL))
		{
			continue;
		}
		
		mmr = eaTail(&mm->fg.resources);
		assert(mmr->mmrc->id == id);
		
		mmResourceMsgInitFG(&pd,
							NULL,
							mm,
							mmr,
							MMR_MSG_FG_FIXUP_CONSTANT_AFTER_COPY);

		pd.msg.in.fg.fixupConstantAfterCopy.constant = mmr->userStruct.constant;
		pd.msg.in.fg.fixupConstantAfterCopy.constantNP = mmr->userStruct.constantNP;
		pd.msg.in.fg.fixupConstantAfterCopy.mm = mm;
		pd.msg.in.fg.fixupConstantAfterCopy.er = mm->entityRef;
		pd.msg.in.fg.fixupConstantAfterCopy.mmSource = mmSource;
		pd.msg.in.fg.fixupConstantAfterCopy.erSource = mmSource->entityRef;

		mmr->mmrc->msgHandler(&pd.msg);
	}
	EARRAY_FOREACH_END;
	
	return 1;
}
