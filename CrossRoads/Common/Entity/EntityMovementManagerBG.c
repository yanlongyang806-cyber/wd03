/***************************************************************************
*     Copyright (c) 2005-2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "EntityMovementManagerPrivate.h"

#include "GlobalTypes.h"
#include "LineDist.h"
#include "PhysicsSDK.h"
#include "WorldColl.h"
#include "../wcoll/collide.h"
#include "../wcoll/entworldcoll.h"

#include "MemoryPool.h"
#include "strings_opt.h"
#include "cpu_count.h"
#include "timing_profiler_interface.h"
#include "ThreadManager.h"
#include "mutex.h"
#include "StringCache.h"

#include "EntityMovementDefault.h"
#include "EntityMovementEmote.h"

#include "SimpleCpuUsage.h"

// MS: Remove these #includes.
#include "dynRagDoll.h"
#include "wlBeacon.h"
#include "cmdparse.h"

#define VERIFY_OUTPUT_LISTS				0
#define VERIFY_PREDICTED_STEP_OUTPUTS	0
#define CHECK_CREATE_OUTPUT_TIMER_STOP	0

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Physics););
AUTO_RUN_ANON(memBudgetAddMapping("ThreadStack:mmProcessingThreadMain", BUDGET_Physics););

AUTO_RUN_ANON(memBudgetAddStructMappingIfNotMapped("MovementRequesterHistory", __FILE__););

static S32 mmReleaseDataOwnershipInternalBG(MovementRequester* mr,
											U32 mdcBits,
											const char* reason);

MovementManagerGridSizeGroup mmGridSizeGroups[TYPE_ARRAY_SIZE(MovementSpace, mmGrids)] = {
	{ 64.f, 16.f },
	{ 128.f, 32.f },
	{ 256.f, 64.f },
	{ 512.f, 128.f },
	{ 2048.f, 512.f },
	{ 0, 0 },
};

static void mmOutputListAddTail(MovementOutputList* ol,
								MovementOutput* o)
{
	assert(!o->prev);

	if(ol->tail){
		ol->tail->nextMutable = o;
		o->prevMutable = ol->tail;
	}else{
		assert(!ol->head);
		ol->head = o;
	}
	
	ol->tail = o;
}

static void mmOutputListSetTail(MovementOutputList* ol,
								MovementOutput* o)
{
	if(!ol->head){
		assert(!ol->tail);
		ol->head = o;
	}

	ol->tail = o;
}

static S32 mmOutputListContains(const MovementOutputList* ol,
								const MovementOutput* o)
{
	const MovementOutput* oCur;
	
	for(oCur = ol->head;
		oCur;
		oCur = (oCur == ol->tail ? NULL : oCur->next))
	{
		if(oCur == o){
			return 1;
		}
	}
	
	return 0;
}

#if !MM_VERIFY_REPREDICTS
	#define mmVerifyAnimOutputBG(mm, oTail)
#else
void mmVerifyAnimOutputBG(	MovementManager* mm,
							const MovementOutput* oTail)
{
	MovementThreadData*		td = MM_THREADDATA_BG(mm);
	MovementThreadData*		tdFG = MM_THREADDATA_FG(mm);
	const MovementOutput*	o;
	U32*					stanceBits = NULL;
	
	eaiCopy(&stanceBits,
			&mm->bg.stanceBits);
			
	if(oTail){
		assert(mmOutputListContains(&mm->bg.outputList, oTail));
	}
	
	for(o = FIRST_IF_SET(oTail, mm->bg.outputList.tail);
		o;
		o = o->prev)
	{
		const MovementAnimValues* anim = NULL;

		if(!mmOutputListContains(&tdFG->toFG.outputList, o)){
			anim = &o->data.anim;

			EARRAY_CONST_FOREACH_BEGIN(td->toFG.repredicts, i, isize);
			{
				assert(o != td->toFG.repredicts[i]->o);
			}
			EARRAY_FOREACH_END;
		}else{
			S32 found = 0;

			EARRAY_CONST_FOREACH_BEGIN(td->toFG.repredicts, i, isize);
			{
				const MovementOutputRepredict* mor = td->toFG.repredicts[i];
				
				if(mor->o == o){
					anim = &mor->data.anim;
					found = 1;
					break;
				}
			}
			EARRAY_FOREACH_END;
			
			if(!found){
				break;
			}
		}

		mmAnimValuesApplyStanceDiff(mm, anim, 1, &stanceBits, __FUNCTION__, 0);

		EARRAY_CONST_FOREACH_BEGIN(mm->bg.predictedSteps, i, isize);
		{
			const MovementPredictedStep* ps = mm->bg.predictedSteps[i];

			if(ps->o == o){
				U32* stancesRemaining = NULL;
				
				eaiCopy(&stancesRemaining, &stanceBits);
				
				EARRAY_INT_CONST_FOREACH_BEGIN(ps->in.stanceBits, j, jsize);
				{
					if(eaiFindAndRemove(&stancesRemaining, ps->in.stanceBits[j]) < 0){
						assert(0);
					}
				}
				EARRAY_FOREACH_END;

				assert(!eaiUSize(&stancesRemaining));
				
				eaiDestroy(&stancesRemaining);
				
				break;
			}
		}
		EARRAY_FOREACH_END;
	}
	
	eaiDestroy(&stanceBits);
}
#endif

void mmRequesterMsgInitBG(	MovementRequesterMsgPrivateData* pd,
							MovementRequesterMsgOut* out,
							MovementRequester* mr,
							MovementRequesterMsgType msgType)
{
	mr->bg.flagsMutable.bgUnchangedSinceCopyToFG = 0;
	mmRequesterMsgInit(pd, out, mr, msgType, MM_BG_SLOT);
}

void mmRequesterMsgInitNoChangeBG(	MovementRequesterMsgPrivateData* pd,
									MovementRequesterMsgOut* out,
									MovementRequester* mr,
									MovementRequesterMsgType msgType)
{
	mmRequesterMsgInit(pd, out, mr, msgType, MM_BG_SLOT);
}

static void mmSendMsgsBeforeDiscussionBG(MovementManager* mm){
	PERFINFO_AUTO_START_FUNC();
	
	MEL_FOREACH_BEGIN(iter, mm->bg.mel[MM_BG_EL_BEFORE_DISCUSSION]);
	{
		MovementRequester* mr = MR_FROM_EN_BG(MR_BG_EN_BEFORE_DISCUSSION, iter);

		assert(mr->bg.handledMsgs & MR_HANDLED_MSG_BEFORE_DISCUSSION);
		
		if( mr->bg.flags.destroyed ||
			mr->bg.flags.repredictNotCreatedYet)
		{
			continue;
		}

		MR_PERFINFO_AUTO_START_GUARD(mr, MRC_PT_BEFORE_DISCUSSION);
		{									
			MovementRequesterMsgPrivateData pd;
			
			mmRequesterMsgInitBG(	&pd,
									NULL,
									mr,
									MR_MSG_BG_BEFORE_DISCUSSION);

			mmRequesterMsgSend(&pd);
		}
		MR_PERFINFO_AUTO_STOP_GUARD(mr, MRC_PT_BEFORE_DISCUSSION);
	}
	MEL_FOREACH_END;
	
	PERFINFO_AUTO_STOP();
}

#define mmLogSectionBegin(mm, tag, name)\
		(MMLOG_IS_ENABLED(mm)?wrapped_mmLogSectionBegin(mm,(tag),(name)),0:0)
static void wrapped_mmLogSectionBegin(	MovementManager* mm,
										const char* tag,
										const char* name)
{
	mmLog(	mm,
			NULL,
			"%s%s%s %s%s.",
			tag ? "[" : "",
			NULL_TO_EMPTY(tag),
			tag ? "]" : "",
			MM_LOG_SECTION_PADDING_BEGIN,
			name);
}

#define mmLogSectionEnd(mm, tag, name)\
		(MMLOG_IS_ENABLED(mm)?wrapped_mmLogSectionEnd(mm,(tag),(name)),0:0)
static void wrapped_mmLogSectionEnd(MovementManager* mm,
									const char* tag,
									const char* name)
{
	mmLog(	mm,
			NULL,
			"%s%s%s %s.%s",
			tag ? "[" : "",
			NULL_TO_EMPTY(tag),
			tag ? "]" : "",
			name,
			MM_LOG_SECTION_PADDING_END);
}

static void mmSendMsgsDiscussDataOwnershipBG(	MovementManager* mm,
												MovementThreadData* td,
												S32 isDuringCreateOutput)
{
	PERFINFO_AUTO_START_FUNC();
	
	mmLogSectionBegin(	mm,
						"bg.DISCUSS_DATA_OWNERSHIP",
						"Sending DISCUSS_DATA_OWNERSHIP to requesters");

	MEL_FOREACH_BEGIN(iter, mm->bg.mel[MM_BG_EL_DISCUSS_DATA_OWNERSHIP]);
	{
		MovementRequester* mr = MR_FROM_EN_BG(MR_BG_EN_DISCUSS_DATA_OWNERSHIP, iter);

		assert(mr->bg.handledMsgs & MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);

		if(	mr->bg.flags.destroyed ||
			mr->bg.flags.repredictNotCreatedYet)
		{
			continue;
		}

		MR_PERFINFO_AUTO_START_GUARD(mr, MRC_PT_DISCUSS_DATA_OWNERSHIP);
		{									
			MovementRequesterMsgPrivateData pd;
			
			mrLog(	mr,
					NULL,
					"[bg.DISCUSS_DATA_OWNERSHIP] Sending DISCUSS_DATA_OWNERSHIP.");

			mmRequesterMsgInitBG(	&pd,
									NULL,
									mr,
									MR_MSG_BG_DISCUSS_DATA_OWNERSHIP);

			pd.msg.in.userStruct.toFG = MR_USERSTRUCT_TOFG(mr, MM_BG_SLOT);
			pd.msg.in.userStruct.toBG = MR_USERSTRUCT_TOBG(mr, MM_BG_SLOT);

			if(isDuringCreateOutput){
				pd.msg.in.bg.discussDataOwnership.flags.isDuringCreateOutput = isDuringCreateOutput;
			}
			
			mmRequesterMsgSend(&pd);

			mrLog(	mr,
					NULL,
					"[bg.DISCUSS_DATA_OWNERSHIP] Done sending DISCUSS_DATA_OWNERSHIP.");
		}	
		MR_PERFINFO_AUTO_STOP_GUARD(mr, MRC_PT_DISCUSS_DATA_OWNERSHIP);
	}
	MEL_FOREACH_END;
	
	mmLogSectionEnd(mm,
					"bg.DISCUSS_DATA_OWNERSHIP",
					"Done sending DISCUSS_DATA_OWNERSHIP to requesters");

	PERFINFO_AUTO_STOP_FUNC();
}

static void mmSendMsgsUpdatedToBG(MovementManager* mm){
	PERFINFO_AUTO_START_FUNC();
	
	mmLogSectionBegin(	mm,
						"bg.UPDATED_TOBG",
						"Sending UPDATED_TOBG to requesters");

	EARRAY_CONST_FOREACH_BEGIN(mm->bg.requesters, i, size);
	{
		MovementRequester*				mr = mm->bg.requesters[i];
		MovementRequesterThreadData*	mrtd = MR_THREADDATA_BG(mr);
		MovementRequesterMsgPrivateData pd;
		MovementRequesterMsgOut			out;

		if(!TRUE_THEN_RESET(mrtd->toBG.flagsMutable.hasUserToBG)){
			continue;
		}

		if(	mr->bg.flags.destroyed ||
			mr->bg.flags.repredictNotCreatedYet)
		{
			StructResetVoid(mr->mrc->pti.toBG,
							MR_USERSTRUCT_TOBG(mr, MM_BG_SLOT));
			continue;
		}
		
		mrLog(mr, NULL, "Sending msg UPDATED_TOBG.");
		
		MR_PERFINFO_AUTO_START_GUARD(mr, MRC_PT_UPDATED_TOBG);
		{
			mmRequesterMsgInitBG(	&pd,
									&out,
									mr,
									MR_MSG_BG_UPDATED_TOBG);

			pd.msg.in.userStruct.toFG = MR_USERSTRUCT_TOFG(mr, MM_BG_SLOT);
			pd.msg.in.userStruct.toBG = MR_USERSTRUCT_TOBG(mr, MM_BG_SLOT);

			mmRequesterMsgSend(&pd);
		}	
		MR_PERFINFO_AUTO_STOP_GUARD(mr, MRC_PT_UPDATED_TOBG);
	}
	EARRAY_FOREACH_END;
	
	mmLogSectionEnd(mm,
					"bg.UPDATED_TOBG",
					"Done sending UPDATED_TOBG to requesters");

	PERFINFO_AUTO_STOP();
}

static void mrSendMsgUpdatedSyncBG(	MovementRequester* mr,
									MovementInputStep* miStep)
{
	mrLog(	mr,
			NULL,
			"Sending msg UPDATED_SYNC (sync s%u).",
			SAFE_MEMBER(miStep, pc.serverSync));
	
	MR_PERFINFO_AUTO_START_GUARD(mr, MRC_PT_UPDATED_SYNC);
	{
		MovementRequesterMsgPrivateData pd;
		
		mmRequesterMsgInitBG(	&pd,
								NULL,
								mr,
								MR_MSG_BG_UPDATED_SYNC);

		pd.msg.in.userStruct.toFG = MR_USERSTRUCT_TOFG(mr, MM_BG_SLOT);
		pd.msg.in.userStruct.toBG = MR_USERSTRUCT_TOBG(mr, MM_BG_SLOT);

		mmRequesterMsgSend(&pd);
	}	
	MR_PERFINFO_AUTO_STOP_GUARD(mr, MRC_PT_UPDATED_SYNC);
}

static void mmSendMsgsUpdatedSync(	MovementManager* mm,
									MovementInputStep* miStep)
{
	PERFINFO_AUTO_START_FUNC();

	mmLogSectionBegin(	mm,
						"bg.UPDATED_SYNC",
						"Sending UPDATED_SYNC to requesters");
	
	EARRAY_CONST_FOREACH_BEGIN(mm->bg.requesters, i, size);
	{
		MovementRequester* mr = mm->bg.requesters[i];

		if(!TRUE_THEN_RESET(mr->bg.flagsMutable.hasNewSync)){
			continue;
		}

		if(	mr->bg.flags.destroyed ||
			mr->bg.flags.repredictNotCreatedYet)
		{
			continue;
		}
		
		mrSendMsgUpdatedSyncBG(mr, miStep);
	}
	EARRAY_FOREACH_END;
	
	mmLogSectionEnd(mm,
					"bg.UPDATED_SYNC",
					"Done sending UPDATED_SYNC to requesters");

	PERFINFO_AUTO_STOP();
}

static void mmSendMsgInputEventBG(	MovementManager* mm,
									const MovementInputValue* value)
{
	MEL_FOREACH_BEGIN(iter, mm->bg.mel[MM_BG_EL_INPUT_EVENT]);
	{
		MovementRequester* mr = MR_FROM_EN_BG(MR_BG_EN_INPUT_EVENT, iter);

		assert(mr->bg.handledMsgs & MR_HANDLED_MSG_INPUT_EVENT);

		if(	mr->bg.flags.destroyed ||
			mr->bg.flags.repredictNotCreatedYet)
		{
			continue;
		}

		MR_PERFINFO_AUTO_START_GUARD(mr, MRC_PT_INPUT_EVENT);
		{									
			MovementRequesterMsgPrivateData pd;

			mmRequesterMsgInitBG(	&pd,
									NULL,
									mr,
									MR_MSG_BG_INPUT_EVENT);

			pd.msg.in.userStruct.toFG = MR_USERSTRUCT_TOFG(mr, MM_BG_SLOT);
			pd.msg.in.userStruct.toBG = MR_USERSTRUCT_TOBG(mr, MM_BG_SLOT);
			pd.msg.in.bg.inputEvent.value = *value;

			mmRequesterMsgSend(&pd);
		}
		MR_PERFINFO_AUTO_STOP_GUARD(mr, MRC_PT_INPUT_EVENT);
	}
	MEL_FOREACH_END;
}

static void mmApplyInputBitValueBG(	MovementManager* mm,
									MovementInputValueIndex mivi,
									S32 value,
									S32 isDoubleTap,
									const char* reason)
{
	const U32			valueIndex = mivi - MIVI_BIT_LOW;
	MovementInputValue	miValue = {0};
	
	assert(valueIndex < ARRAY_SIZE(mm->bg.miState->bit));

	if(MMLOG_IS_ENABLED(mm)){
		const char* indexName;

		mmGetInputValueIndexName(	mivi,
									&indexName);

		mmLog(	mm,
				NULL,
				"[bg.input] Change bit (%s): %s = %u%s (was %u)",
				reason,
				indexName,
				value,
				isDoubleTap ? ", DOUBLE TAP" : "",
				mm->bg.miState->bit[valueIndex]);
	}
			
	mm->bg.miState->bit[valueIndex] = value;

	miValue.mivi = mivi;
	miValue.bit = value;
	if(isDoubleTap){
		miValue.flags.isDoubleTap = 1;
	}
	
	mmSendMsgInputEventBG(mm, &miValue);
}

static void mmApplyInputF32ValueBG(	MovementManager* mm,
									MovementInputValueIndex mivi,
									F32 value,
									const char* reason)
{
	const U32			valueIndex = mivi - MIVI_F32_LOW;
	MovementInputValue	miValue = {0};
	
	assert(valueIndex < ARRAY_SIZE(mm->bg.miState->f32));
	
	if(MMLOG_IS_ENABLED(mm)){
		const char* indexName;

		mmGetInputValueIndexName(	mivi,
									&indexName);

		mmLog(	mm,
				NULL,
				"[bg.input] Change F32 (%s): %s = %1.3f [%8.8x] (was %1.3f [%8.8x])",
				reason,
				indexName,
				value,
				*(S32*)&value,
				mm->bg.miState->f32[valueIndex],
				*(S32*)&mm->bg.miState->bit[valueIndex]);
	}

	mm->bg.miState->f32[valueIndex] = value;

	miValue.mivi = mivi;
	miValue.f32 = value;
	
	mmSendMsgInputEventBG(mm, &miValue);
}

static void mmResetAllInputValuesBG(MovementManager* mm,
									const char* reason)
{
	ARRAY_FOREACH_BEGIN(mm->bg.miState->bit, i);
	{
		if(mm->bg.miState->bit[i]){
			mmApplyInputBitValueBG(mm, MIVI_BIT_LOW + i, 0, 0, reason);
		}
	}
	ARRAY_FOREACH_END;
	
	ARRAY_FOREACH_BEGIN(mm->bg.miState->f32, i);
	{
		if(mm->bg.miState->f32[i] != 0.f){
			mmApplyInputF32ValueBG(mm, MIVI_F32_LOW + i, 0.f, reason);
		}
	}
	ARRAY_FOREACH_END;
}

static void mmApplyInputEventBG(MovementManager* mm,
								const MovementInputEvent* mie)
{
	const MovementInputValueIndex mivi = mie->value.mivi;
	
	if(INRANGE(mivi, MIVI_BIT_LOW, MIVI_BIT_HIGH)){
		if(!mm->bg.flags.needsSetPosVersion){
			mmApplyInputBitValueBG(	mm,
									mivi,
									mie->value.bit,
									mie->value.flags.isDoubleTap,
									"input event");
		}
	}
	else if(INRANGE(mivi, MIVI_F32_LOW, MIVI_F32_HIGH)){
		if(!mm->bg.flags.needsSetPosVersion){
			mmApplyInputF32ValueBG( mm,
									mivi,
									mie->value.f32,
									"input event");
		}
	}
	else if(mivi == MIVI_DEBUG_COMMAND){
		mmLog(	mm,
				NULL,
				"[bg.input] Debug cmd: \"%s\".",
				mie->value.command);

		mmSendMsgInputEventBG(mm, &mie->value);
	}
	else if(mivi == MIVI_RESET_ALL_VALUES){
		mmLog(	mm,
				NULL,
				"[bg.input] Reset all values (setPosVersion %u, waiting for %u).",
				mie->value.u32,
				mm->bg.setPosVersion);
				
		mmResetAllInputValuesBG(mm, "reset input event");
				
		mmSendMsgInputEventBG(mm, &mie->value);

		if(mie->value.u32 == mm->bg.setPosVersion){
			mm->bg.flagsMutable.needsSetPosVersion = 0;
			assert(!mm->bg.flags.resetOnNextInputStep);
		}
	}else{
		mmLog(	mm,
				NULL,
				"[bg.input] Unknown input index %u.",
				mivi);
	}
}

typedef S32 (*MovementForEachCellCallback)(	MovementManagerGridCell* cell,
											const void* userPointer);

void mmForEachCell(	const WorldColl* wc,
					const Vec3 boundsMin,
					const Vec3 boundsMax,
					MovementForEachCellCallback callback,
					const void* userPointer)
{
	PERFINFO_AUTO_START_FUNC();
	
	// MS: TODO: spaces is not thread-safe.

	EARRAY_CONST_FOREACH_BEGIN(mgState.fg.spaces, i, isize);
	{
		MovementSpace* space = mgState.fg.spaces[i];
		
		if(space->wc != wc){
			continue;
		}

		ARRAY_FOREACH_BEGIN(space->mmGrids, j);
		{
			MovementManagerGrid*	grid = space->mmGrids + j;
			const F32				cellSize = mmGridSizeGroups[j].cellSize;
			IVec3					lo;
			IVec3					hi;
			IVec3					size;
			F32						offset = 30.f;
			F32						volume;
			F32						maxVolume;
			
			// Send actor destroyed message to all nearby mms.

			if(cellSize){
				FOR_BEGIN(k, 3);
				{
					lo[k] = (S32)floor((boundsMin[k]-offset) / cellSize);
					hi[k] = (S32)floor((boundsMax[k]+offset) / cellSize) + 1;
				}
				FOR_END;
			}else{
				setVec3same(lo, 0);
				setVec3same(hi, 1);
			}
			
			subVec3(hi, lo, size);

			volume = (F32)size[0] * (F32)size[1] * (F32)size[2];
			maxVolume = (F32)stashGetCount(grid->stCells) * 5;

			if(volume > maxVolume){
				StashTableIterator	iter;
				StashElement		elem;

				PERFINFO_AUTO_START("iterate table", 1);

				stashGetIterator(grid->stCells, &iter);
				
				while(stashGetNextElement(&iter, &elem)){
					MovementManagerGridCell* cell = stashElementGetPointer(elem);
					
					if(	cell->posGrid[0] >= lo[0] && cell->posGrid[0] < hi[0] &&
						cell->posGrid[1] >= lo[1] && cell->posGrid[1] < hi[1] &&
						cell->posGrid[2] >= lo[2] && cell->posGrid[2] < hi[2])
					{
						callback(cell, userPointer);
					}
				}

				PERFINFO_AUTO_STOP();
			}else{
				PERFINFO_AUTO_START("iterate grid", 1);

				FOR_BEGIN_FROM(x, lo[0], hi[0]);
					FOR_BEGIN_FROM(y, lo[1], hi[1]);
						FOR_BEGIN_FROM(z, lo[2], hi[2]);
							MovementManagerGridCell*	cell = NULL;
							IVec3						posGrid = {x, y, z};

							if(!mmGridGetCellByGridPosBG(grid, &cell, posGrid, 0)){
								continue;
							}

							callback(cell, userPointer);
						FOR_END;
					FOR_END;
				FOR_END;

				PERFINFO_AUTO_STOP();
			}
		}
		ARRAY_FOREACH_END;
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP();
}

static void mmSendMsgFindUnobstructedPosFG(	MovementManager* mm,
											WorldColl* wc,
											const Capsule* capsule,
											const Vec3 posStart)
{
	if(mm->msgHandler){
		MovementManagerMsgPrivateData	pd = {0};
		MovementManagerMsgOut			out = {0};
		
		pd.mm = mm;
		pd.msg.msgType = MM_MSG_FG_FIND_UNOBSTRUCTED_POS;
		pd.msg.userPointer = mm->userPointer;
		pd.msg.out = &out;
		copyVec3(posStart, pd.msg.fg.findUnobstructedPos.posStart);
		
		mm->msgHandler(&pd.msg);

		if(	out.fg.findUnobstructedPos.flags.found &&
			!wcCapsuleCollideCheck(	wc,
									capsule,
									out.fg.findUnobstructedPos.pos,
									WC_FILTER_BIT_MOVEMENT,
									NULL))
		{
			mmSetPositionFG(mm,
							out.fg.findUnobstructedPos.pos,
							"Geo created - unobstructed pos");
		}else{
			pd.msg.msgType = MM_MSG_FG_MOVE_ME_SOMEWHERE_SAFE;
			pd.msg.out = NULL;
			
			mm->msgHandler(&pd.msg);
		}
	}
}

static S32 mmSendMsgQueryIsPosValidBG(	MovementManager* mm,
										const Vec3 pos)
{
	MovementManagerMsgPrivateData	pd = {0};
	MovementManagerMsgOut			out = {0};

	if(!mm->msgHandler){
		return 0;
	}
	
	pd.msg.msgType = MM_MSG_FG_QUERY_IS_POS_VALID;
	pd.msg.userPointer = mm->userPointer;
	pd.msg.fg.queryIsPosValid.vec3Pos = pos;
	pd.msg.out = &out;
	
	mm->msgHandler(&pd.msg);
	
	return out.fg.queryIsPosValid.flags.posIsValid;
}

static S32 mmCollidePSDKActor(	PSDKActor* psdkActor,
								const Vec3 sceneOffset,
								const Vec3 posStart,
								const Vec3 posEnd,
								F32 radius)
{
#if !PSDK_DISABLED
	U32 shapeCount;
	
	PERFINFO_AUTO_START_FUNC();
	
	shapeCount = psdkActorGetShapeCount(psdkActor);

	FOR_BEGIN(i, (S32)shapeCount);
	{
		const PSDKShape*	psdkShape;
		CollInfo			coll = {0};

		copyVec3(sceneOffset, coll.sceneOffset);
		
		if(	psdkActorGetShapeByIndex(psdkActor, i, &psdkShape) &&
			collideShape(	posStart,
							posEnd,
							&coll,
							radius,
							COLL_BOTHSIDES | COLL_CYLINDER,
							psdkShape))
		{
			PERFINFO_AUTO_STOP();
			return 1;
		}
	}
	FOR_END;

	PERFINFO_AUTO_STOP();
#endif

	return 0;
}

S32 mmMoveTestBG_V0(MovementManager* mm,
					WorldCollGridCell* wcCell,
					const Vec3 pos,
					Vec3 targetPosOut,
					F32 move_dist,
					S32 axis)
{
	MotionState motion = {0};
	copyVec3(pos, targetPosOut);

	targetPosOut[axis] += move_dist;
	ZeroStruct(&motion);
	motion.wcCell = wcCell;
	motion.filterBits = WC_QUERY_BITS_ENTITY_MOVEMENT;
	copyVec3(pos, motion.last_pos);
	copyVec3(targetPosOut, motion.pos);
	subVec3(targetPosOut, pos, motion.vel);
	motion.is_player = mm->bg.flags.isAttachedToClient;
	worldMoveMe(&motion);
	//!collideSweepIgnoreActorCap(pos, targetPosOut, caps[0], &coll, COLL_HITANY|COLL_BOTHSIDES, actor)
	if(motion.stuck_head != STUCK_COMPLETELY && mmSendMsgQueryIsPosValidBG(mm, targetPosOut))
		return 1;
	
	return 0;
}

void mrmMoveToValidPointBG_V0(const MovementRequesterMsg *msg){
#if !PSDK_DISABLED
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;
	WorldCollObject*					wco;
	PSDKActor*							psdkActor;
	MovementManager*					mmFromWCO;
	Vec3								mmMin;
	Vec3								mmMax;
	const Capsule*const*				caps;
	Vec3								pos;
	Vec3								targetpos;
	Vec3								tmppos;
	S32									i;
	const WorldCollIntegrationMsg*		wciMsg = msg->in.bg.wcoActorCreated.wciMsg;
	const F32*							bMin = wciMsg->nobg.actorCreated.boundsMin;
	const F32*							bMax = wciMsg->nobg.actorCreated.boundsMax;
	S32									hitCap = 0;
	F32									move_dist[3][2] = {0};
	CollInfo							coll = {0};
	WorldCollGridCell*					wcCell = NULL;
	U32									shapeCount = 0;
	Vec3								mmWorldMin;
	Vec3								mmWorldMax;
	
	if(pd->msgType != MR_MSG_BG_WCO_ACTOR_CREATED){
		return;
	}

	mm = pd->mm;
	wco = wciMsg->nobg.actorCreated.wco;
	psdkActor = wciMsg->nobg.actorCreated.psdkActor;
	shapeCount = psdkActorGetShapeCount(psdkActor);

	copyVec3(	wciMsg->nobg.actorDestroyed.sceneOffset,
				coll.sceneOffset);

	if(!mmGetCapsules(mm, &caps)){
		return;
	}

	// Check if this geometry is myself.

	if(	mmGetFromWCO(wco, &mmFromWCO) &&
		mmFromWCO == mm)
	{
		return;
	}

	mrmGetPositionBG(msg, pos);
	pos[1] += 0.1;

	mmGetCapsuleBounds(mm, mmMin, mmMax);
	addVec3(mmMin, pos, mmWorldMin);
	addVec3(mmMax, pos, mmWorldMax);

	if(!boxBoxCollision(mmWorldMin, mmWorldMax, bMin, bMax)){
		return;
	}

	copyVec3(pos, tmppos);
	copyVec3(pos, targetpos);
	tmppos[1] += 1.5;
	targetpos[1] += 6;

	for(i=0; i<(S32)shapeCount; i++)
	{
		const PSDKShape *shape = NULL;
		psdkActorGetShapeByIndex(psdkActor, i, &shape);
		// Numbers stolen from entworldcoll.c, line 45, just to match
		hitCap |= collideShape(	tmppos,
								targetpos,
								&coll,
								caps[0]->fRadius*0.667,
								COLL_BOTHSIDES,
								shape);

		if(hitCap)
			break;
	}

	// Determine minimum distance in each direction to move "out" of the object
	for(i=0; i<3; i++)
	{
		move_dist[i][0] = bMax[i]-(mmMin[i]+pos[i])+0.1;
		move_dist[i][1] = bMin[i]-(mmMax[i]+pos[i])-0.1;
	}

	// TODO (AM): This should probably go across all possible scenes

	coll.wc = wciMsg->nobg.actorCreated.wc;

	if(	!wcGetGridCellByWorldPosBG(wciMsg, wciMsg->nobg.actorCreated.wc, &wcCell, pos) || 
		!wcCellGetSceneAndOffset(wcCell, &coll.psdkScene, coll.sceneOffset))
	{
		return;
	}

	coll.filterBits = WC_QUERY_BITS_ENTITY_MOVEMENT;
	
	if(!hitCap && !wcoIsShell(wco))
	{
		Vec3 src;
		copyVec3(pos, src);
		src[1] += 1.5;
		for(i=0; i<(S32)shapeCount; i++)
		{
			Vec3				tmpPos;
			const PSDKShape*	shape = NULL;
			
			psdkActorGetShapeByIndex(psdkActor, i, &shape);
			copyVec3(src, tmpPos);
			tmpPos[1] += move_dist[1][1];

			if(!collideShape(src, tmpPos, &coll, caps[0]->fRadius*0.667, COLL_BOTHSIDES, shape))
				continue;
			
			copyVec3(src, tmpPos);
			tmpPos[1] += move_dist[1][0];

			if(!collideShape(src, tmpPos, &coll, caps[0]->fRadius*0.667, COLL_BOTHSIDES, shape))
				continue;

			hitCap = 1;
			break;
		}
	}

	if(!hitCap){
		return;
	}

	psdkActorSetIgnore(psdkActor, 1);

	if(move_dist[1][0]<6)
	{
		if(mmMoveTestBG_V0(mm, wcCell, pos, targetpos, move_dist[1][0], 1))
		{
			mmSetPositionFG(mm, targetpos, "Geo created - vert < 6");
			psdkActorSetIgnore(psdkActor, 0);
			return;
		}
	}

	for(i=1; i>=0; i--)
	{
		S32 j;
		for(j=2; j>=0; j-=2)
		{
			if(mmMoveTestBG_V0(mm, wcCell, pos, targetpos, move_dist[j][i], j))
			{
				mmSetPositionFG(mm, targetpos, "Geo created - horiz");
				psdkActorSetIgnore(psdkActor, 0);
				return;
			}
		}
	}

	if(move_dist[1][0]>=6)
	{
		if(mmMoveTestBG_V0(mm, wcCell, pos, targetpos, move_dist[1][0], 1))
		{
			mmSetPositionFG(mm, targetpos, "Geo created - vert >= 6");
			psdkActorSetIgnore(psdkActor, 0);
			return;
		}
	}

	mmSendMsgFindUnobstructedPosFG(	mm,
									wciMsg->nobg.actorCreated.wc,
									caps[0],
									targetpos);

	psdkActorSetIgnore(psdkActor, 0);
#endif
}

static void moveItEx(	MovementManager* mm,
						const Vec3 dir,
						const U32 maxSubStepCount,
						F32 addedCapsuleRadius,
						S32 bIgnoreCollisionWithEnts,
						S32 disableStickyGround,
						EntityRef erLurchTarget,
						S32* isStuckOut, 
						S32 *collidedWithOthers);

#define moveIt(mm, dir, maxSubStepCount, isStuckOut)	moveItEx((mm),(dir),(maxSubStepCount), 0.f, false, false, 0, (isStuckOut), NULL)

static S32 mmDebugDrawValidPointAttemps;
AUTO_CMD_INT(mmDebugDrawValidPointAttemps, mmDebugDrawValidPointAttemps);

#if !PSDK_DISABLED
static S32 mmPointIsCollidingWithActorBG(	PSDKActor* psdkActor,
											const Vec3 sceneOffset,
											const Vec3 pos)
{
	Vec3 posHead;
	Vec3 posFeet;
	
	PERFINFO_AUTO_START_FUNC();

	copyVec3(pos, posHead);
	posHead[1] += 6.f;
	copyVec3(pos, posFeet);
	posFeet[1] += 0.5f;

	if(	!psdkActorIsPointInside(psdkActor, sceneOffset, posFeet) &&
		!psdkActorIsPointInside(psdkActor, sceneOffset, posHead) &&
		!mmCollidePSDKActor(psdkActor, sceneOffset, posHead, posFeet, 1.f))
	{
		PERFINFO_AUTO_STOP();
		return 0;
	}
	
	PERFINFO_AUTO_STOP();
	return 1;
}

static S32 mmCanDoThisMoveBG(	MovementManager* mm,
								PSDKActor* psdkActor,
								const Vec3 sceneOffset,
								const Vec3 posStart,
								const Vec3 vecOffset)
{
	Vec3 posTarget;

	MM_CHECK_DYNPOS_DEVONLY(posStart);

	copyVec3(posStart, mm->bg.posMutable);
	addVec3(vecOffset, mm->bg.pos, posTarget);

	if(mmPointIsCollidingWithActorBG(psdkActor, sceneOffset, posTarget)){
		return 0;
	}

	moveIt(mm, vecOffset, 0, NULL);
	
	if(!mmPointIsCollidingWithActorBG(psdkActor, sceneOffset, mm->bg.pos)){
		Vec3 vecDown = {0, -6.f, 0};
		Vec3 vecBack;
		
		psdkActorSetIgnore(psdkActor, 0);
		
		moveIt(mm, vecDown, 0, NULL);
		subVec3(posStart, mm->bg.pos, vecBack);
		moveIt(mm, vecBack, 0, NULL);
		
		if(mmDebugDrawValidPointAttemps){
			globCmdParsef(	"mmAddDebugSegment %f %f %f %f %f %f 0xff00ff00",
							vecParamsXYZ(posStart),
							vecParamsXYZ(posTarget));
		}

		return 1;
	}
	if(mmDebugDrawValidPointAttemps){
		globCmdParsef(	"mmAddDebugSegment %f %f %f %f %f %f 0xffff0000",
						vecParamsXYZ(posStart),
						vecParamsXYZ(posTarget));
	}
	
	return 0;
}

static S32 mmFindValidPointAtRadiusBG(	MovementManager* mm,
										PSDKActor* psdkActor,
										const Vec3 sceneOffset,
										const Vec3 posOrig,
										S32 radius)
{
	FOR_BEGIN_FROM(r1, 0, radius + 1);
	{
		FOR_BEGIN_FROM(c, 0, r1 * 2 + 1);
		{
			S32 c0 = ((c + 1) / 2) * ((c & 1) ? -1 : 1);

			FOR_BEGIN(i0, 3);
			{
				S32 i1 = (i0 + 1) % 3;
				S32 i2 = (i1 + 1) % 3;
				
				FOR_BEGIN(j, 2);
				{
					Vec3 p0;
					Vec3 p1;
					Vec3 p2;
					Vec3 p3;

					p0[i0] = j ? radius : -radius;
					p0[i1] = -r1;
					p0[i2] = c0;

					p1[i0] = p0[i0];
					p1[i1] = r1;
					p1[i2] = -c0;

					p2[i0] = p0[i0];
					p2[i1] = c0;
					p2[i2] = r1;

					p3[i0] = p0[i0];
					p3[i1] = -c0;
					p3[i2] = -r1;

					if(mmCanDoThisMoveBG(mm, psdkActor, sceneOffset, posOrig, p0)){
						return 1;
					}
					
					if(mmCanDoThisMoveBG(mm, psdkActor, sceneOffset, posOrig, p1)){
						return 1;
					}

					if(mmCanDoThisMoveBG(mm, psdkActor, sceneOffset, posOrig, p2)){
						return 1;
					}

					if(mmCanDoThisMoveBG(mm, psdkActor, sceneOffset, posOrig, p3)){
						return 1;
					}
				}
				FOR_END;
			}
			FOR_END;
		}
		FOR_END;
	}
	FOR_END;
	
	return 0;
}

static void mmFindUnobstructedPosFG(MovementManager* mm,
									const WorldCollIntegrationMsg* wciMsg,
									const Vec3 posOrig)
{
	const Capsule*const*	capsules;
	const Capsule*			capsule;
	
	if(mmGetCapsules(mm, &capsules)){
		capsule = capsules[0];
	}else{
		static Capsule capTemp;
		capTemp.fLength = 3;
		capTemp.fRadius = 1.5f;
		capTemp.vDir[1] = 1.f;
		capsule = &capTemp;
	}

	mmSendMsgFindUnobstructedPosFG(	mm,
									wciMsg->nobg.actorCreated.wc,
									capsule,
									posOrig);
}

static S32 mrmMoveToValidPointHelperBG(const MovementRequesterMsg *msg)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;
	WorldCollObject*					wco;
	PSDKActor*							psdkActor;
	Vec3								sceneOffset;
	U32									filterBits;
	MovementManager*					mmFromWCO;
	const WorldCollIntegrationMsg*		wciMsg;
	const F32*							boundsMin;
	const F32*							boundsMax;
	CollInfo							coll = {0};
	WorldCollGridCell*					wcCell = NULL;
	U32									shapeCount = 0;
	Vec3								posOrig;

	wciMsg = msg->in.bg.wcoActorCreated.wciMsg;
	boundsMin = wciMsg->nobg.actorCreated.boundsMin;
	boundsMax = wciMsg->nobg.actorCreated.boundsMax;

	mm = pd->mm;
	wco = wciMsg->nobg.actorCreated.wco;
	psdkActor = wciMsg->nobg.actorCreated.psdkActor;
	shapeCount = psdkActorGetShapeCount(psdkActor);

	copyVec3(	wciMsg->nobg.actorCreated.sceneOffset,
				sceneOffset);

	// Check if this geometry is collidable.

	if(	!psdkActorGetFilterBits(psdkActor, &filterBits) ||
		!(filterBits & WC_QUERY_BITS_ENTITY_MOVEMENT))
	{
		return 0;
	}

	// Check if this geometry is myself.

	if(	mmGetFromWCO(wco, &mmFromWCO) &&
		mmFromWCO == mm)
	{
		return 0;
	}

	// Get the cell and scene.

	coll.wc = wciMsg->nobg.actorCreated.wc;

	if(	!wcGetGridCellByWorldPosBG(	wciMsg,
									wciMsg->nobg.actorCreated.wc,
									&wcCell,
									mm->bg.pos)
		|| 
		!wcCellGetSceneAndOffset(	wcCell,
									&coll.psdkScene,
									coll.sceneOffset))
	{
		return 0;
	}
	
	// Backup original position.
	
	copyVec3(mm->bg.pos, posOrig);

	// Check if I'm stuck, in which case just give up.
	
	{
		Vec3	vecTest = {0, 1, 0};
		S32		isStuck;

		psdkActorSetIgnore(psdkActor, 1);
		moveIt(mm, vecTest, 0, &isStuck);
		psdkActorSetIgnore(psdkActor, 0);

		if(isStuck){
			mmFindUnobstructedPosFG(mm, wciMsg, posOrig);
			return 0;
		}
		
		MM_CHECK_DYNPOS_DEVONLY(posOrig);

		copyVec3(posOrig, mm->bg.posMutable);
	}
	
	// Check if object is moving or was just created.
	
	if(wciMsg->nobg.actorCreated.prev){
		// Object is moving; check if it should push me.

		Mat4 matPrevInv;
		Mat4 matCur;
		Vec3 posRelativePrev;
		Vec3 posWorldCur;
		Vec3 vecOffset;
		Vec3 posFeet;
		Vec3 posHead;
		
		invertMat4(wciMsg->nobg.actorCreated.prev->mat, matPrevInv);
		mulVecMat4(mm->bg.pos, matPrevInv, posRelativePrev);
		
		psdkActorGetMat(psdkActor, matCur);
		subVec3(matCur[3], sceneOffset, matCur[3]);
		mulVecMat4(posRelativePrev, matCur, posWorldCur);
		
		// Ignore the new actor, try to move to the new position.
		
		psdkActorSetIgnore(psdkActor, 1);
		subVec3(posWorldCur, mm->bg.pos, vecOffset);
		moveIt(mm, vecOffset, 0, NULL);
		psdkActorSetIgnore(psdkActor, 0);
		
		// If not colliding with the new actor, move back to original position.

		copyVec3(posWorldCur, posFeet);
		posFeet[1] -= 0.3f;
		copyVec3(posWorldCur, posHead);
		posHead[1] += 6.f;
		
		if(!mmCollidePSDKActor(psdkActor, sceneOffset, posHead, posFeet, 1.f)){
			subVec3(posOrig, mm->bg.pos, vecOffset);
			moveIt(mm, vecOffset, 0, NULL);
		}

		copyVec3(mm->bg.pos, posOrig);
	}
	
	// Check if I'm embedded in the new actor, and if so, move somewhere better.

	if(!psdkActorIsPointInside(psdkActor, sceneOffset, mm->bg.pos)){
		Vec3 posHead;
		copyVec3(mm->bg.pos, posHead);
		posHead[1] += 6.f;
		
		if(!mmCollidePSDKActor(psdkActor, sceneOffset, posHead, mm->bg.pos, 1.f)){
			// Done, not colliding and not inside.
			return 1;
		}
	}
	
	// Check within 5 feet.
	
	psdkActorSetIgnore(psdkActor, 1);

	FOR_BEGIN_FROM(r0, 1, 6);
	{
		if(mmFindValidPointAtRadiusBG(mm, psdkActor, sceneOffset, posOrig, r0)){
			psdkActorSetIgnore(psdkActor, 0);
			return 1;
		}
	}
	FOR_END;
	
	// Nope, check up and down.
	
	{
		Vec3 vecUp = {0, boundsMax[1] + 2 - posOrig[1], 0};
		
		MM_CHECK_DYNPOS_DEVONLY(posOrig);

		psdkActorSetIgnore(psdkActor, 1);
		copyVec3(posOrig, mm->bg.posMutable);
		moveIt(mm, vecUp, 0, NULL);
		psdkActorSetIgnore(psdkActor, 0);

		if(!mmPointIsCollidingWithActorBG(psdkActor, sceneOffset, mm->bg.pos)){
			Vec3 vecDown = {0, boundsMin[1] - mm->bg.pos[1] - 6, 0};
			moveIt(mm, vecDown, 0, NULL);
			return 1;
		}
	}
	
	// Check out to 15 feet, then just give up.
	
	#if 0
	{
		psdkActorSetIgnore(psdkActor, 1);

		FOR_BEGIN_FROM(r0, 6, 16);
		{
			if(mmFindValidPointAtRadiusBG(mm, psdkActor, posOrig, r0)){
				psdkActorSetIgnore(psdkActor, 0);
				return 1;
			}
		}
		FOR_END;

		psdkActorSetIgnore(psdkActor, 0);
	}
	#endif

	// Couldn't find anything good, see if owner can find something usable.
	
	mmFindUnobstructedPosFG(mm, wciMsg, posOrig);

	return 0;
}
#endif

static void mmSetHasChangedOutputDataRecentlyBG(MovementManager* mm);
static void mmSendMsgPosChangedBG(MovementManager* mm);

void mrmMoveToValidPointBG(const MovementRequesterMsg *msg){
#if !PSDK_DISABLED
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	
	if(pd->msgType != MR_MSG_BG_WCO_ACTOR_CREATED){
		return;
	}
	
	// Call old version and return.
	
	mrmMoveToValidPointBG_V0(msg);
	return;

	if(mrmMoveToValidPointHelperBG(msg)){
		MovementManager* mm = pd->mm;

		mm->bg.flagsMutable.sendViewStatusChanged = 1;

		mmSetHasChangedOutputDataRecentlyBG(mm);

		mmSendMsgPosChangedBG(mm);
	}
#endif
}

static void mmSendMsgActorCreatedBG(MovementManager* mm,
									const WorldCollIntegrationMsg* wciMsg)
{
#if !PSDK_DISABLED
	// This is only called during threadswap, so bg is safe.

	MovementRequester*				mr;
	MovementRequesterMsgPrivateData pd;

	if(	!mm->userPointer
		||
		mm->fg.flags.ignoreActorCreate
		||
		!mgState.flags.isServer &&
		!mm->bg.flags.isAttachedToClient)
	{
		return;
	}
	
	if(	mm->bg.pos[0] <= wciMsg->nobg.actorCreated.boundsMin[0] - 5.f ||
		mm->bg.pos[1] <= wciMsg->nobg.actorCreated.boundsMin[1] - 10.f ||
		mm->bg.pos[2] <= wciMsg->nobg.actorCreated.boundsMin[2] - 5.f ||
		mm->bg.pos[0] >= wciMsg->nobg.actorCreated.boundsMax[0] + 5.f ||
		mm->bg.pos[1] >= wciMsg->nobg.actorCreated.boundsMax[1] + 5.f ||
		mm->bg.pos[2] >= wciMsg->nobg.actorCreated.boundsMax[2] + 5.f)
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC();
	
	mr = mm->bg.dataOwner[MDC_POSITION_CHANGE];
	
	if(!mr){
		MovementThreadData* td = MM_THREADDATA_BG(mm);

		mmSendMsgsDiscussDataOwnershipBG(mm, td, 0);

		mr = mm->bg.dataOwner[MDC_POSITION_CHANGE];

		if(!mr){
			// Still a bit hacky, but for now it will work
			// The correct solution is probably some sort of message passing or conflict resolver
			FOR_EACH_IN_EARRAY(mm->fg.requesters, MovementRequester, mrTest)
			{
				if(mrTest->mrc->id==MR_CLASS_ID_SURFACE)
				{
					mr = mrTest;
					break;
				}
				else if(mrTest->mrc->id==MR_CLASS_ID_FLIGHT)
				{
					mr = mrTest;
					break;
				}
			}
			FOR_EACH_END;
		}
	}
	
	if(mr){
		mmRequesterMsgInitBG(	&pd, 
								NULL, 
								mr, 
								MR_MSG_BG_WCO_ACTOR_CREATED);
		
		if(MRLOG_IS_ENABLED(mr)){
			PSDKScene* psdkScene;
			
			psdkActorGetScene(&psdkScene, wciMsg->nobg.actorCreated.psdkActor);
			
			mrLog(	mr,
					NULL,
					"Sending msg WCO_ACTOR_CREATED:\n"
					"WorldCollObject: 0x%p\n"
					"PSDKActor: 0x%p\n"
					"PSDKScene: 0x%p\n"
					"Bounds min: (%1.3f, %1.3f, %1.3f) [%8.8x, %8.8x, %8.8x]\n"
					"Bounds max: (%1.3f, %1.3f, %1.3f) [%8.8x, %8.8x, %8.8x]",
					wciMsg->nobg.actorCreated.wco,
					wciMsg->nobg.actorCreated.psdkActor,
					psdkScene,
					vecParamsXYZ(wciMsg->nobg.actorCreated.boundsMin),
					vecParamsXYZ((S32*)wciMsg->nobg.actorCreated.boundsMin),
					vecParamsXYZ(wciMsg->nobg.actorCreated.boundsMax),
					vecParamsXYZ((S32*)wciMsg->nobg.actorCreated.boundsMax));
		}
		
		pd.msg.in.bg.wcoActorCreated.wciMsg = wciMsg;

		mmRequesterMsgSend(&pd);
	}

	PERFINFO_AUTO_STOP();
#endif
}

static S32 mmCellHandleActorCreated(const MovementManagerGridCell* cell,
									const WorldCollIntegrationMsg* wciMsg)
{
	EARRAY_CONST_FOREACH_BEGIN(cell->managers, i, isize);
	{
		MovementManager* mm = cell->managers[i];

		mmSendMsgActorCreatedBG(mm, wciMsg);
	}
	EARRAY_FOREACH_END;

	return 1;
}

void mmHandleActorCreatedBG(const WorldCollIntegrationMsg* wciMsg){
	mmForEachCell(	wciMsg->nobg.actorCreated.wc,
					wciMsg->nobg.actorCreated.boundsMin,
					wciMsg->nobg.actorCreated.boundsMax,
					mmCellHandleActorCreated,
					wciMsg);
}

static void mmSendMsgActorDestroyedBG(	MovementManager *mm,
										const WorldCollIntegrationMsg* wciMsg)
{
	MovementRequester* mr;
	
	if(	mm->bg.pos[0] <= wciMsg->nobg.actorDestroyed.boundsMin[0] - 5.f ||
		mm->bg.pos[1] <= wciMsg->nobg.actorDestroyed.boundsMin[1] - 10.f ||
		mm->bg.pos[2] <= wciMsg->nobg.actorDestroyed.boundsMin[2] - 5.f ||
		mm->bg.pos[0] >= wciMsg->nobg.actorDestroyed.boundsMax[0] + 5.f ||
		mm->bg.pos[1] >= wciMsg->nobg.actorDestroyed.boundsMax[1] + 5.f ||
		mm->bg.pos[2] >= wciMsg->nobg.actorDestroyed.boundsMax[2] + 5.f)
	{
		return;
	}

	if(mm->msgHandler){
		MovementManagerMsgPrivateData pd = {0};
		
		pd.mm = mm;
		pd.msg.msgType = MM_MSG_FG_NEARBY_GEOMETRY_DESTROYED;
		pd.msg.userPointer = mm->userPointer;
		
		mm->msgHandler(&pd.msg);
	}
	
	// If no requester currently owns MDC_POSITION_CHANGE, see if anyone wants it.
	
	mr = mm->bg.dataOwner[MDC_POSITION_CHANGE];

	if(!mr){
		MovementThreadData* td = MM_THREADDATA_BG(mm);

		mmSendMsgsDiscussDataOwnershipBG(mm, td, 0);

		mr = mm->bg.dataOwner[MDC_POSITION_CHANGE];
	}

	if(mr){
		MovementRequesterMsgPrivateData pd;
		
		mrLog(	mr,
				NULL,
				"Sending msg WCO_ACTOR_DESTROYED:\n"
				"Bounds min: (%1.3f, %1.3f, %1.3f) [%8.8x, %8.8x, %8.8x]\n"
				"Bounds max: (%1.3f, %1.3f, %1.3f) [%8.8x, %8.8x, %8.8x]",
				vecParamsXYZ(wciMsg->nobg.actorDestroyed.boundsMin),
				vecParamsXYZ((S32*)wciMsg->nobg.actorDestroyed.boundsMin),
				vecParamsXYZ(wciMsg->nobg.actorDestroyed.boundsMax),
				vecParamsXYZ((S32*)wciMsg->nobg.actorDestroyed.boundsMax));
	
		mmRequesterMsgInitBG(	&pd, 
								NULL, 
								mr, 
								MR_MSG_BG_WCO_ACTOR_DESTROYED);

		mmRequesterMsgSend(&pd);
	}
}

static S32 mmCellHandleActorDestroyed(	const MovementManagerGridCell* cell,
										const WorldCollIntegrationMsg* wciMsg)
{
	EARRAY_CONST_FOREACH_BEGIN(cell->managers, i, size);
	{
		MovementManager* mm = cell->managers[i];

		mmSendMsgActorDestroyedBG(mm, wciMsg);
	}
	EARRAY_FOREACH_END;

	return 1;
}

void mmHandleActorDestroyedBG(const WorldCollIntegrationMsg* wciMsg){
	mmForEachCell(	wciMsg->nobg.actorDestroyed.wc,
					wciMsg->nobg.actorDestroyed.boundsMin,
					wciMsg->nobg.actorDestroyed.boundsMax,
					mmCellHandleActorDestroyed,
					wciMsg);
}

static void mmLogInputStateBG(	MovementManager* mm,
								const char* prefix,
								const MovementInputStep* miStep)
{
	char		valueString[200];
	const char* indexName;

	valueString[0] = 0;

	FOR_BEGIN(i, MIVI_BIT_COUNT);
	{
		if(mm->bg.miState->bit[i]){
			mmGetInputValueIndexName(	MIVI_BIT_LOW + i,
										&indexName);
			
			strcatf(valueString,
					"\n    bit[%s] = 1",
					indexName);
		}
	}
	FOR_END;

	FOR_BEGIN(i, MIVI_F32_COUNT);
	{
		if(mm->bg.miState->f32[i] != 0.f){
			mmGetInputValueIndexName(	MIVI_F32_LOW + i,
										&indexName);

			strcatf(valueString,
					"\n    f32[%s] = %1.3f [%8.8x]",
					indexName,
					mm->bg.miState->f32[i],
					*(S32*)&mm->bg.miState->f32[i]);
		}
	}
	FOR_END;

	mmLog(	mm,
			NULL,
			"[bg.input] (c%u/s%u) Input state %s:%s",
			miStep->pc.client,
			miStep->pc.server,
			prefix,
			valueString[0] ? valueString : "None set");
}

static void mmApplyInputStepBG(	MovementManager* mm,
								MovementInputStep* miStep)
{
	const MovementInputEvent* mie;

	if(!mm->bg.miState){
		mm->bg.miState = callocStruct(MovementInputState);
	}
	
	if(TRUE_THEN_RESET(mm->bg.flagsMutable.resetOnNextInputStep)){
		mmLog(	mm,
				NULL,
				"[bg.input] Reset all values.");
				
		mmResetAllInputValuesBG(mm, "reset on next input step");
	}
	
	if(mm->bg.flags.needsSetPosVersion){
		mmLog(	mm,
				NULL,
				"[bg.input] Waiting for setPosVersion %u.",
				mm->bg.setPosVersion);
	}

	for(; miStep; miStep = miStep->bg.next){
		if(miStep->mieList.head){
			PERFINFO_AUTO_START_FUNC();

			if(MMLOG_IS_ENABLED(mm)){
				mmLogInputStateBG(mm, "before applying input step", miStep);
			}

			for(mie = miStep->mieList.head;
				mie;
				mie = mie->next)
			{
				mmApplyInputEventBG(mm, mie);
			}

			// Send the state changes.

			if(MMLOG_IS_ENABLED(mm)){
				mmLogInputStateBG(mm, "after applying input step", miStep);
			}
			
			PERFINFO_AUTO_STOP();
		}
		else if(MMLOG_IS_ENABLED(mm)){
			mmLogInputStateBG(mm, "unchanged due to no input events", miStep);
		}
	}
}

static void mmSendMsgsCreateDetails(MovementManager* mm,
									MovementOutput* o)
{
	PERFINFO_AUTO_START("details", 1);

	MEL_FOREACH_BEGIN(iter, mm->bg.mel[MM_BG_EL_CREATE_DETAILS]);
	{
		MovementRequester* mr = MR_FROM_EN_BG(MR_BG_EN_CREATE_DETAILS, iter);

		assert(mr->bg.handledMsgs & MR_HANDLED_MSG_CREATE_DETAILS);

		if(	mr->bg.flags.destroyed ||
			mr->bg.flags.repredictNotCreatedYet)
		{
			continue;
		}

		MR_PERFINFO_AUTO_START_GUARD(mr, MRC_PT_OUTPUT_DETAILS);
		{
			MovementRequesterMsgPrivateData pd;
			
			mrLog(mr, NULL, "Sending CREATE_DETAILS.");

			mmRequesterMsgInitBG(	&pd,
									NULL,
									mr,
									MR_MSG_BG_CREATE_DETAILS);

			pd.msg.in.userStruct.toFG = MR_USERSTRUCT_TOFG(mr, MM_BG_SLOT);
			pd.msg.in.userStruct.toBG = MR_USERSTRUCT_TOBG(mr, MM_BG_SLOT);

			pd.o = o;

			mmRequesterMsgSend(&pd);
		}
		MR_PERFINFO_AUTO_STOP_GUARD(mr, MRC_PT_OUTPUT_DETAILS);
	}
	MEL_FOREACH_END;

	PERFINFO_AUTO_STOP();
}

static void mmSendMsgsPredictDisabledBG(MovementManager* mm,
										MovementRequester* mr)
{
	MovementRequesterMsgPrivateData pd;

	mmRequesterMsgInitBG(	&pd,
							NULL,
							mr,
							MR_MSG_BG_PREDICT_DISABLED);

	pd.msg.in.userStruct.toFG = MR_USERSTRUCT_TOFG(mr, MM_BG_SLOT);
	pd.msg.in.userStruct.toBG = MR_USERSTRUCT_TOBG(mr, MM_BG_SLOT);

	mmRequesterMsgSend(&pd);
}

static void mmSendMsgsPredictEnabledBG(	MovementManager* mm,
										MovementRequester* mr)
{
	MovementRequesterMsgPrivateData pd;

	mmRequesterMsgInitBG(	&pd,
							NULL,
							mr,
							MR_MSG_BG_PREDICT_ENABLED);

	pd.msg.in.userStruct.toFG = MR_USERSTRUCT_TOFG(mr, MM_BG_SLOT);
	pd.msg.in.userStruct.toBG = MR_USERSTRUCT_TOBG(mr, MM_BG_SLOT);

	mmRequesterMsgSend(&pd);
}

static S32 mmShouldPredictDataClassBG(	MovementManager* mm,
										MovementThreadData* td,
										U32 mdc,
										MovementOutput* o)
{
	switch(mdc){
		xcase MDC_POSITION_CHANGE:{
			// Check if prediction should start/continue.
			if (mm->bg.additionalVel.flags.isSet)
			{
				mm->bg.flagsMutable.doNotRestartPrediction = 0;
			}

			if(	!mm->bg.flagsMutable.doNotRestartPrediction &&
					(mm->bg.additionalVel.flags.isSet ||
					mm->bg.constantVel.isSet ||
					mm->bg.target.pos.targetType ||
					mm->bg.flags.isAttachedToClient ||
					mm->flags.isLocal || 
					mm->bg.flags.isInDeathPrediction)
				)
			{
				bool bWasPredicting = mm->bg.flagsMutable.isPredicting;

				mmLog(	mm,
						NULL,
						"[bg.predict] Starting prediction (%salready on): %s%s%s%s",
						mm->bg.flags.isPredicting ? "" : "not ",
						mm->bg.additionalVel.flags.isSet ? "hasAdditionalVel, " : "",
						mm->bg.constantVel.isSet ? "hasConstantVel, " : "",
						mm->bg.target.pos.targetType ? "targetTypeIsSet, " : "",
						mm->bg.flags.isAttachedToClient ? "isAttachedToClient, " : "");
				
				mm->bg.predictStepsRemainingMutable = 30;

				if(FALSE_THEN_SET(mm->bg.flagsMutable.isPredicting)){
					// Copy latest net position to latest local position and make it predicted.

					const MovementNetOutput*	no = td->toBG.net.outputList.tail;
					MovementOutput*				oTail = mm->bg.outputList.tail;

					if(	oTail &&
						no)
					{
						// MS: Is this thread-safe?

						oTail->flagsMutable.isPredicted = 1;
						oTail->flagsMutable.posIsPredicted = 1;

						MM_CHECK_DYNPOS_DEVONLY(no->data.pos);

						copyVec3(	no->data.pos,
									oTail->dataMutable.pos);
						
						copyQuat(	no->data.rot,
									oTail->dataMutable.rot);
						
						copyVec2(	no->data.pyFace,
									oTail->dataMutable.pyFace);
					}
				}

				MM_TD_SET_HAS_TOFG(mm, td);
				td->toFG.flagsMutable.startPredict = 1;
				
				if(!td->toFG.predict){
					td->toFG.predict = callocStruct(MovementThreadDataToFGPredict);
				}

				td->toFG.predict->pcStart = mgState.bg.pc.local.cur;
				
				

				if(!mm->bg.flags.isAttachedToClient){

					if (!bWasPredicting)
					{
						const MovementNetOutput* no = td->toBG.net.outputList.tail;

						if(no){
							MM_CHECK_DYNPOS_DEVONLY(no->data.pos);

							copyVec3(	no->data.pos,
										mm->bg.posMutable);

							copyQuat(	no->data.rot,
										mm->bg.rotMutable);

							copyVec2(   no->data.pyFace,
										mm->bg.pyFaceMutable);
						}
					}
						
					EARRAY_CONST_FOREACH_BEGIN(mm->bg.requesters, i, isize);
					{
						mmSendMsgsPredictEnabledBG(mm, mm->bg.requesters[i]);
					}
					EARRAY_FOREACH_END;
				}
			}

			mm->bg.flagsMutable.doNotRestartPrediction = 0;

			if(!mm->bg.flags.isPredicting){
				return 0;
			}
			
			if(o){
				o->flagsMutable.posIsPredicted = 1;
			}
		}

		xcase MDC_ROTATION_CHANGE:{
			if(!mm->bg.flags.isPredicting){
				return 0;
			}
		}
	}
	
	return 1;
}

static void mmCheckForPredictFinishedBG(MovementManager* mm){
	assert(mm->bg.predictStepsRemaining);
	
	if(!--mm->bg.predictStepsRemainingMutable){
		mm->bg.flagsMutable.isPredicting = 0;

		mmReleaseDataOwnershipInternalBG(	mm->bg.dataOwner[MDC_BIT_POSITION_TARGET],
											MDC_BITS_TARGET_ALL,
											"prediction finished");

		mmReleaseDataOwnershipInternalBG(	mm->bg.dataOwner[MDC_BIT_ROTATION_TARGET],
											MDC_BITS_TARGET_ALL,
											"prediction finished");
											
		mmLog(	mm,
				NULL,
				"[bg.predict] Prediction disabled.");
											
		EARRAY_CONST_FOREACH_BEGIN(mm->bg.requesters, i, isize);
		{
			mmSendMsgsPredictDisabledBG(mm, mm->bg.requesters[i]);
		}
		EARRAY_FOREACH_END;
	}else{
		mmLog(	mm,
				NULL,
				"[bg.predict] %u prediction steps remaining.",
				mm->bg.predictStepsRemaining);
	}
}

static S32 mmAnimStanceExistsBG(MovementManager* mm,
								const U32 bitHandle)
{
	EARRAY_CONST_FOREACH_BEGIN(mm->bg.stances, j, jsize);
	{
		if(bitHandle == mm->bg.stances[j]->animBitHandle){
			return 1;
		}
	}
	EARRAY_FOREACH_END;

	return 0;	
}

static void mmOutputAddAnimValueBG(	MovementManager* mm,
									MovementOutput* o,
									U32 index,
									MovementAnimValueType mavType)
{
	if(!o->data.anim.values){
		o->dataMutable.anim.values = eaPop(&mm->bg.available.animValuesMutable);
	}

	if(!eaiPush(&o->dataMutable.anim.values, MM_ANIM_VALUE(index, mavType))){
		o->flagsMutable.addedAnimValue = 1;
	}
}

static void mmUpdateStanceBitsBG(	MovementManager* mm,
									MovementThreadData* td,
									MovementOutput* o)
{
	// Remove bits that are absent from the handles list.

	EARRAY_INT_CONST_FOREACH_BEGIN(mm->bg.stanceBits, i, isize);
	{
		const U32 bit = mm->bg.stanceBits[i];

		if(!mmAnimStanceExistsBG(mm, bit)){
			eaiRemove(&mm->bg.stanceBitsMutable, i);
			i--;
			isize--;
			
			mmOutputAddAnimValueBG(	mm,
									o,
									bit,
									MAVT_STANCE_OFF);
		}
	}
	EARRAY_FOREACH_END;
	
	// Add bits that are absent from the bits list.

	EARRAY_CONST_FOREACH_BEGIN(mm->bg.stances, i, isize);
	{
		const U32 bit = mm->bg.stances[i]->animBitHandle;
		
		if(eaiFind(&mm->bg.stanceBits, bit) < 0){
			eaiPush(&mm->bg.stanceBitsMutable, bit);

			mmOutputAddAnimValueBG(	mm,
									o,
									bit,
									MAVT_STANCE_ON);
		}
	}
	EARRAY_FOREACH_END;
	
	eaiCopy(&td->toFG.stanceBitsMutable,
			&mm->bg.stanceBits);

	mm->bg.nextFrame[MM_FG_SLOT].flagsMutable.sendStanceBitsToFG = 1;
	mm->bg.nextFrame[MM_BG_SLOT].flagsMutable.sendStanceBitsToFG = 0;
}

static S32 mmHandleNoDataOwnerDuringCreateOutputBG(	MovementManager* mm,
													MovementThreadData* td,
													const U32 mdc)
{
	switch(mdc){
		xcase MDC_POSITION_TARGET:{
			// Default to input for players if no one owns position target.

			if(	mm->bg.flags.isAttachedToClient &&
				!mm->bg.flags.needsSetPosVersion)
			{
				mm->bg.target.pos.targetType = MPTT_INPUT;
			}

			return 0;
		}

		xcase MDC_ROTATION_TARGET:{
			// Default to input for players if no one owns rotation target.

			if(	mm->bg.flags.isAttachedToClient &&
				!mm->bg.flags.needsSetPosVersion)
			{
				mm->bg.target.rot.targetType = MRTT_INPUT;
			}

			return 0;
		}

		xcase MDC_POSITION_CHANGE:{
			// Nobody owns position change, see if anyone wants it.

			if(!mm->bg.flags.mrHandlesMsgDiscussDataOwnership){
				return 0;
			}
			
			mmSendMsgsDiscussDataOwnershipBG(mm, td, 1);
			
			return 1;
		}
	}
	
	return 0;
}

static void mmUpdateAnimAfterCreateOutputBG(MovementManager* mm,
											MovementThreadData* td,
											MovementOutput* o)
{
	// Check if any stances need to be added or removed from the list.

	if(TRUE_THEN_RESET(mm->bg.flagsMutable.animStancesChanged)){
		mmUpdateStanceBitsBG(mm, td, o);
	}

	// Check if lastAnim changed.
	
	if(TRUE_THEN_RESET(mm->bg.flagsMutable.animToStartIsSet)){
		S32 found = 0;

		mmLastAnimCopyToValues(	&o->dataMutable.anim,
								&mm->bg.lastAnim);

		mm->bg.lastAnimMutable.pc = mgState.bg.pc.local.cur;
		if(eaiSize(&mm->bg.lastAnim.flags)){
			eaiClearFast(&mm->bg.lastAnimMutable.flags);
		}

		EARRAY_INT_CONST_FOREACH_BEGIN(o->data.anim.values, i, isize);
		{
			const U32 v = o->data.anim.values[i];

			switch(MM_ANIM_VALUE_GET_TYPE(v)){
				xcase MAVT_LASTANIM_ANIM:{
					// Skip PC.
					i++;
				}
				xcase MAVT_ANIM_TO_START:{
					ASSERT_FALSE_AND_SET(found);
					mm->bg.lastAnimMutable.anim = MM_ANIM_VALUE_GET_INDEX(v);
				}
				xcase MAVT_FLAG:{
					eaiPush(&mm->bg.lastAnimMutable.flags,
							MM_ANIM_VALUE_GET_INDEX(v));
				}
			}
		}
		EARRAY_FOREACH_END;

		assert(found);

		mmLastAnimCopy(	&td->toFG.lastAnimMutable,
						&mm->bg.lastAnim);

		mm->bg.nextFrame[MM_FG_SLOT].flagsMutable.sendLastAnimToFG = 1;
	}
	else if(TRUE_THEN_RESET(mm->bg.flagsMutable.animFlagIsSet)){
		EARRAY_INT_CONST_FOREACH_BEGIN(o->data.anim.values, i, isize);
		{
			const U32 v = o->data.anim.values[i];

			switch(MM_ANIM_VALUE_GET_TYPE(v)){
				xcase MAVT_LASTANIM_ANIM:{
					// Skip PC.
					i++;
				}
				xcase MAVT_ANIM_TO_START:{
					assert(0);
				}
				xcase MAVT_FLAG:{
					eaiPush(&mm->bg.lastAnimMutable.flags,
							MM_ANIM_VALUE_GET_INDEX(v));
				}
			}
		}
		EARRAY_FOREACH_END;

		mmLastAnimCopy(	&td->toFG.lastAnimMutable,
						&mm->bg.lastAnim);

		mm->bg.nextFrame[MM_FG_SLOT].flagsMutable.sendLastAnimToFG = 1;
	}
	else if(TRUE_THEN_RESET(mm->bg.flagsMutable.animOwnershipWasReleased)){
		mmLog(mm, NULL, "[bg.anim] Anim ownership was released, no new anim on this step.");

		EARRAY_INT_CONST_FOREACH_BEGIN(o->data.anim.values, i, isize);
		{
			switch(MM_ANIM_VALUE_GET_TYPE(o->data.anim.values[i])){
				xcase MAVT_LASTANIM_ANIM:{
					// Skip PC.
					i++;
				}
				xcase MAVT_ANIM_TO_START:{
					assert(0);
				}
				xcase MAVT_FLAG:{
					assert(0);
				}
			}
		}
		EARRAY_FOREACH_END;

		mmLastAnimCopyToValues(	&o->dataMutable.anim,
								&mm->bg.lastAnim);
		
		mmOutputAddAnimValueBG(	mm,
								o,
								mgState.animBitHandle.animOwnershipReleased,
								MAVT_ANIM_TO_START);
		
		mm->bg.lastAnimMutable.pc = mgState.bg.pc.local.cur;
		mm->bg.lastAnimMutable.anim = mgState.animBitHandle.animOwnershipReleased;
		if(eaiSize(&mm->bg.lastAnim.flags)){
			eaiClearFast(&mm->bg.lastAnimMutable.flags);
		}

		mmLastAnimCopy(	&td->toFG.lastAnimMutable,
						&mm->bg.lastAnim);

		mm->bg.nextFrame[MM_FG_SLOT].flagsMutable.sendLastAnimToFG = 1;
	}
}

#if !MM_VERIFY_TOFG_VIEW_STATUS
	#define mmVerifyViewStatusToFG(mm)
#else
	static void mmVerifyViewStatusToFG(MovementManager* mm){
		MovementThreadData* td = MM_THREADDATA_BG(mm);

		if(td->toFG.flags.viewStatusChanged){
			assert(td->toFG.flags.posIsAtRest == mm->bg.flags.posIsAtRest);
			assert(td->toFG.flags.rotIsAtRest == mm->bg.flags.rotIsAtRest);
			assert(td->toFG.flags.pyFaceIsAtRest == mm->bg.flags.pyFaceIsAtRest);
		}
	}
#endif

static void mmSendMsgsCreateOutputBG(	MovementManager* mm,
										MovementThreadData* td,
										MovementOutput* o)
{
	MovementRequesterMsgCreateOutputShared shared;

	#if CHECK_CREATE_OUTPUT_TIMER_STOP
		S32 usedOwnerTimer = 0;
	#endif
	
	if(mm->bg.dataOwner[MDC_POSITION_TARGET]){
		PERFINFO_AUTO_START("mmSendMsgsCreateOutputBG:hasPosTargetOwner", 1);

		#if CHECK_CREATE_OUTPUT_TIMER_STOP
			usedOwnerTimer = 1;
		#endif
	}else{
		PERFINFO_AUTO_START("mmSendMsgsCreateOutputBG:noPosTargetOwner", 1);
	}
	
	mmLog(	mm,
			NULL,
			"[bg.createOutput] %s"
			"-^-v-^-v-^-v-^-v-^-v-^-v-^-v-[ CREATE OUTPUT BEGIN ]-^-v-^-v-^-v-^-v-^-v-^-v-^-v-",
			MM_LOG_SECTION_PADDING_BEGIN);

	mm->bg.flagsMutable.viewChanged = 0;

	// Set the shared data.

	MM_CHECK_DYNPOS_DEVONLY(mm->bg.pos);

	copyVec3(	mm->bg.pos,
				shared.orig.pos);

	copyQuat(	mm->bg.rot,
				shared.orig.rot);
				
	shared.target.pos = &mm->bg.target.pos;
	shared.target.rot = &mm->bg.target.rot;
	shared.target.minSpeed = mm->bg.target.minSpeed;
	
	// Clear the current target.

	ZeroStruct(&mm->bg.target);

	if(	mm->bg.flags.isInactive &&
		!mgState.fg.flags.noDisable)
	{
		mmLog(	mm,
				NULL,
				"[bg.createOutput] Movement is DISABLED, probably by a disabled handle.");
	}

	// Send CREATE_OUTPUT message to all the MDC owners.

	ARRAY_FOREACH_BEGIN(mm->bg.dataOwner, mdc);
	{
		MovementRequester*				mr = mm->bg.dataOwner[mdc];
		MovementRequesterMsgPrivateData pd;
		U32								mdcBit;

		if(!mr){
			if(!mmHandleNoDataOwnerDuringCreateOutputBG(mm, td, mdc)){
				continue;
			}
			
			mr = mm->bg.dataOwner[mdc];
			
			if(!mr){
				continue;
			}
		}

		mdcBit = BIT(mdc);

		if(!(mm->bg.dataOwnerEnabledMask & mdcBit)){
			continue;
		}

		// Check if the client wants to enable prediction.

		if(	!mgState.flags.isServer &&
			!mmShouldPredictDataClassBG(mm, td, mdc, o))
		{
			continue;
		}

		// Check if movement is disabled by a MovementDisabledHandle in FG.

		if(	mm->bg.flags.isInactive &&
			!mgState.fg.flags.noDisable)
		{
			continue;
		}

		while(1){
			// Log the MDC.

			if(MMLOG_IS_ENABLED(mm)){
				const char* name;

				mmGetDataClassName(&name, mdc);

				mmLog(	mm,
						NULL,
						"[bg.createOutput] BEGIN: %s (%s[%u])",
						name,
						mr->mrc->name,
						mr->handle);
			}

			PERFINFO_RUN(
				MovementRequesterClassPerfType	perfType = MRC_PT_OUTPUT_POSITION_TARGET + mdc;
				static PERFINFO_TYPE*			perfInfoClass[MDC_COUNT];
				const char*						name;
				
				mmGetDataClassName(&name, mdc);
				PERFINFO_AUTO_START_STATIC(name, perfInfoClass + mdc, 1);
				MR_PERFINFO_AUTO_START(mr, perfType);
			);
			{
				// Start sending the message.

				mmRequesterMsgInitBG(	&pd,
										NULL,
										mr,
										MR_MSG_BG_CREATE_OUTPUT);

				// Check if this is the first message for this frame.

				if(mr->bg.betweenSimCountOfLastCreateOutput != mgState.bg.betweenSim.count){
					mr->bg.betweenSimCountOfLastCreateOutput = mgState.bg.betweenSim.count;
					pd.msg.in.bg.createOutput.flags.isFirstCreateOutputOnThisStep = 1;
				}

				// Other stuff.

				pd.msg.in.userStruct.toFG = MR_USERSTRUCT_TOFG(mr, MM_BG_SLOT);
				pd.msg.in.userStruct.toBG = MR_USERSTRUCT_TOBG(mr, MM_BG_SLOT);

				pd.o = o;
				pd.in.bg.createOutput.dataClassBit = mdcBit;

				pd.msg.in.bg.createOutput.dataClassBit = pd.in.bg.createOutput.dataClassBit;

				pd.msg.in.bg.createOutput.shared = &shared;
				
				if(mgState.bg.flags.isLastStepOnThisFrame){
					pd.msg.in.bg.createOutput.flags.isLastStepOnThisFrame = 1;
				}

				mmVerifyViewStatusToFG(mm);
				mmRequesterMsgSend(&pd);
				mmVerifyViewStatusToFG(mm);
			}
			PERFINFO_RUN(
				MR_PERFINFO_AUTO_STOP(mr, MRC_PT_OUTPUT_POSITION_TARGET + mdc);
				PERFINFO_AUTO_STOP();
			);
			
			if(mm->bg.dataOwner[mdc]){
				break;
			}else{
				// Owner released data, so see if anyone else wants it.
			
				if(mm->bg.flags.mrHandlesMsgDiscussDataOwnership){
					mmSendMsgsDiscussDataOwnershipBG(mm, td, 1);
				}
				
				mr = mm->bg.dataOwner[mdc];
				
				if(!mr){
					break;
				}
			}
		}
	}
	ARRAY_FOREACH_END;
	
	if(TRUE_THEN_RESET(mm->bg.additionalVel.flags.isSet))
		zeroVec3(mm->bg.additionalVel.vel);

	if(TRUE_THEN_RESET(mm->bg.constantVel.isSet))
		zeroVec3(mm->bg.constantVel.vel);

	if(mm->bg.flags.mrHandlesMsgCreateDetails){
		mmVerifyViewStatusToFG(mm);
		mmSendMsgsCreateDetails(mm, o);
		mmVerifyViewStatusToFG(mm);
	}
  
	if(mm->bg.flags.isPredicting){
		o->flagsMutable.isPredicted = 1;
		
		mmCheckForPredictFinishedBG(mm);
	}

	MM_CHECK_DYNPOS_DEVONLY(mm->bg.pos);

	copyVec3(	mm->bg.pos,
				o->dataMutable.pos);

	copyQuat(	mm->bg.rot,
				o->dataMutable.rot);
				
	copyVec2(	mm->bg.pyFace,
				o->dataMutable.pyFace);

	if(	mm->bg.flags.animStancesChanged ||
		mm->bg.flags.animToStartIsSet ||
		mm->bg.flags.animFlagIsSet ||
		mm->bg.flags.animOwnershipWasReleased)
	{
		mmUpdateAnimAfterCreateOutputBG(mm, td, o);
	}
	else if(!o->flags.addedAnimValue &&
			o->data.anim.values)
	{
		eaPush(	&mm->bg.available.animValuesMutable,
				o->data.anim.values);

		o->dataMutable.anim.values = NULL;
	}

	// Check if the view is resting.

	mmVerifyViewStatusToFG(mm);

	if(	(	!mm->bg.flags.posIsAtRest ||
			!mm->bg.flags.rotIsAtRest ||
			!mm->bg.flags.pyFaceIsAtRest)
		&&
		!TRUE_THEN_RESET(mm->bg.flagsMutable.viewChanged)
		&&
		FALSE_THEN_SET(td->toFG.flagsMutable.viewStatusChanged))
	{
		mmSetAfterSimWakesOnceBG(mm);

		#if MM_VERIFY_TOFG_VIEW_STATUS
			td->toFG.frameWhenViewStatusChanged = mgState.frameCount;
		#endif

		mm->bg.flagsMutable.posIsAtRest = 1;
		mm->bg.flagsMutable.rotIsAtRest = 1;
		mm->bg.flagsMutable.pyFaceIsAtRest = 1;

		assert(	td->toFG.flags.posIsAtRest &&
				td->toFG.flags.rotIsAtRest &&
				td->toFG.flags.pyFaceIsAtRest);
	}

	mmVerifyViewStatusToFG(mm);

	mmLog(	mm,
			NULL,
			"[bg.createOutput] "
			"-^-v-^-v-^-v-^-v-^-v-^-v-^-v-[ CREATE OUTPUT END ]-^-v-^-v-^-v-^-v-^-v-^-v-^-v-"
			"%s",
			MM_LOG_SECTION_PADDING_END);

	#if CHECK_CREATE_OUTPUT_TIMER_STOP
		if(usedOwnerTimer){
			PERFINFO_AUTO_STOP_CHECKED("mmSendMsgsCreateOutputBG:hasPosTargetOwner");
		}else{
			PERFINFO_AUTO_STOP_CHECKED("mmSendMsgsCreateOutputBG:noPosTargetOwner");
		}
	#else
		PERFINFO_AUTO_STOP();
	#endif
}

MP_DEFINE(MovementRequesterHistory);

static S32 mmRequesterHistoryCreateBG(MovementRequesterHistory** historyOut){
	if(!historyOut){
		return 0;
	}

	MP_CREATE(MovementRequesterHistory, 10);

	*historyOut = MP_ALLOC(MovementRequesterHistory);

	return 1;
}

static S32 mmRequesterHistoryDestroyBG(	MovementManager* mm,
										MovementRequester* mr,
										MovementRequesterHistory** historyInOut)
{
	MovementRequesterHistory* h = SAFE_DEREF(historyInOut);

	if(!h){
		return 0;
	}

	mmStructDestroy(mr->mrc->pti.toBG,
					h->in.toBG,
					mm);

	mmStructDestroy(mr->mrc->pti.sync,
					h->in.sync,
					mm);

	mmStructDestroy(mr->mrc->pti.syncPublic,
					h->in.syncPublic,
					mm);

	mmStructDestroy(mr->mrc->pti.bg,
					h->out.bg,
					mm);

	mmStructDestroy(mr->mrc->pti.localBG,
					h->out.localBG,
					mm);

	MP_FREE(MovementRequesterHistory, *historyInOut);

	return 1;
}

static void mmRequesterBGPredictCreate(	MovementManager* mm,
										MovementRequester* mr)
{
	if(	!mgState.flags.isServer &&
		mm->bg.flags.isAttachedToClient &&
		!mr->bg.predict)
	{
		mr->bg.predictMutable = callocStruct(MovementRequesterBGPredict);
	}
}

void mmRequesterAddNewToListBG(	MovementManager* mm,
								MovementRequester* mr)
{
	MovementThreadData* td = MM_THREADDATA_BG(mm);

	assert(mmIsBackgroundThread());

	ASSERT_FALSE_AND_SET(mr->bg.flagsMutable.inList);
	eaPush(&mm->bg.requestersMutable, mr);

	mmRequesterBGPredictCreate(mm, mr);

	MM_TD_SET_HAS_TOFG(mm, td);
	td->toFG.flagsMutable.hasNewRequesters = 1;
	eaPush(&td->toFG.newRequestersMutable, mr);

	mr->pcLocalWhenCreated = mgState.bg.pc.local.cur;

	assert(!mr->fg.flags.createdInFG);
	assert(!mr->bg.flags.createdInFG);
}

static void mmRequesterBGPredictDestroy(MovementManager* mm,
										MovementRequester* mr)
{
	MovementRequesterBGPredict* predict = mr->bg.predict;

	if(!predict){
		return;
	}

	EARRAY_CONST_FOREACH_BEGIN(predict->history, i, size);
	{
		mmRequesterHistoryDestroyBG(mm, mr, &predict->historyMutable[i]);
	}
	EARRAY_FOREACH_END;

	eaDestroy(&predict->historyMutable);

	SAFE_FREE(mr->bg.predictMutable);
}

static void mmPredictedStepStoreInputBG(MovementManager* mm,
										MovementOutput* o,
										MovementInputStep* miStep,
										const S32 mrHasUserToBG)
{
	MovementPredictedStep* ps;

	PERFINFO_AUTO_START_FUNC();
	
	// Create predicted step, add to list.
	
	ps = callocStruct(MovementPredictedStep);

	eaPush(&mm->bg.predictedStepsMutable, ps);
	mm->bg.flagsMutable.hasPredictedSteps = 1;

	// Store the relevant output.

	ps->o = o;
	ps->pc = o->pc;

	// Store the control step.

	ps->in.miStep = miStep;

	ASSERT_FALSE_AND_SET(miStep->bg.flags.inRepredict);

	assert(!miStep->bg.flags.finished);

	// Store the handleRequesterInput flag.
	
	ps->in.flags.mrHasUserToBG = !!mrHasUserToBG;

	// Store the requester sync flag.

	ps->in.flags.mrHasNewSync = mm->bg.flags.mrHasNewSync;
	
	// Store the setPosVersion.
	
	if(mm->bg.flags.needsSetPosVersion){
		ps->in.flags.needsSetPosVersion = 1;
		ps->in.setPosVersion = mm->bg.setPosVersion;
	}
	
	// Store the anim state.

	eaiCopy(&ps->in.stanceBits,
			&mm->bg.stanceBits);
			
	mmLastAnimCopy(	&ps->in.lastAnim,
					&mm->bg.lastAnim);

	// Store the requester BG and sync states.

	EARRAY_CONST_FOREACH_BEGIN(mm->bg.requesters, i, size);
	{
		MovementRequester*				mr = mm->bg.requesters[i];
		MovementRequesterThreadData*	mrtd = MR_THREADDATA_BG(mr);
		MovementRequesterBGPredict*		predict = mr->bg.predict;
		MovementRequesterHistory*		h;

		mmRequesterHistoryCreateBG(&h);

		mm->bg.flagsMutable.mrHasHistory = 1;

		eaPush(&predict->historyMutable, h);

		h->cpc = mgState.bg.pc.local.cur;
		
		// Flag this as the first history point for this requester.
		
		if(FALSE_THEN_SET(mr->bg.flagsMutable.wroteHistory)){
			h->in.flags.isFirstHistory = 1;
		}
		
		// Copy flags.
		
		h->in.flags.hasUserToBG = mrtd->toBG.flags.hasUserToBG;
		h->in.flags.hasNewSync = mr->bg.flags.hasNewSync;

		// Copy the toBG struct.

		mmStructAllocAndCopy(	mr->mrc->pti.toBG,
								h->in.toBG,
								MR_USERSTRUCT_TOBG(mr, MM_BG_SLOT),
								mm);

		// Copy the sync struct.

		mmStructAllocAndCopy(	mr->mrc->pti.sync,
								h->in.sync,
								mr->userStruct.sync.bg,
								mm);

		// Copy the public sync struct.

		if(mr->userStruct.syncPublic.bg){
			mmStructAllocAndCopy(	mr->mrc->pti.syncPublic,
									h->in.syncPublic,
									mr->userStruct.syncPublic.bg,
									mm);
		}
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP();
}

static void mmPredictedStepStoreOutputBG(	MovementManager* mm,
											MovementPredictedStep* ps)
{
	PERFINFO_AUTO_START_FUNC();
	
	// Copy the output data.
	
	copyVec3(	mm->bg.pos,
				ps->out.pos);
				
	copyQuat(	mm->bg.rot,
				ps->out.rot);
				
	copyVec2(	mm->bg.pyFace,
				ps->out.pyFace);
	
	// Store the input state.

	ps->out.miState = *mm->bg.miState;

	// Have all the requesters clone their current states.

	EARRAY_CONST_FOREACH_BEGIN(mm->bg.requesters, i, isize);
	{
		MovementRequester*			mr = mm->bg.requesters[i];
		MovementRequesterBGPredict* predict = mr->bg.predict;

		// Find the matching history.

		EARRAY_FOREACH_REVERSE_BEGIN(predict->history, j);
		{
			MovementRequesterHistory* h = predict->history[j];

			if(h->cpc == mgState.bg.pc.local.cur){
				// Copy the bg struct.

				mmStructAllocAndCopy(	mr->mrc->pti.bg,
										h->out.bg,
										mr->userStruct.bg,
										mm);

				// Copy the localBG struct.

				mmStructAllocAndCopy(	mr->mrc->pti.localBG,
										h->out.localBG,
										mr->userStruct.localBG,
										mm);

				// Store the owned data class bits.

				h->out.ownedDataClassBits = mr->bg.ownedDataClassBits;
				
				// Store the handled msgs.
				
				h->out.handledMsgs = mr->bg.handledMsgs;

				break;
			}
		}
		EARRAY_FOREACH_END;
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP();
}

static void mmRequesterGetNameAndHandleBG(	MovementRequester* mr,
											char* buffer,
											U32 bufferLen)
{
	snprintf_s(	buffer,
				bufferLen,
				"%s[%u]",
				mr ? mr->mrc->name : "(none)",
				mr ? mr->handle : 0);
}

static void mmRequesterGetDebugStringBG(MovementRequester* mr,
										char* buffer,
										U32 bufferLen,
										const U32* useThisOwnedDataClassBits,
										const U32* useThisHandledMsgs,
										void* useThisBG,
										void* useThisLocalBG)
{
	MovementRequesterMsgPrivateData pd;
	S32								len;
	char							ownedBitNames[100];
	char							handledMsgsNames[200];

	if(!mr){
		strcpy_s(buffer, bufferLen, "none");
		return;
	}

	ownedBitNames[0] = 0;
	handledMsgsNames[0] = 0;

	mmRequesterMsgInitNoChangeBG(	&pd,
									NULL,
									mr,
									MR_MSG_BG_GET_DEBUG_STRING);
							
	mmGetDataClassNames(SAFESTR(ownedBitNames),
						useThisOwnedDataClassBits ?
							*useThisOwnedDataClassBits :
							mr->bg.ownedDataClassBits);

	mmGetHandledMsgsNames(	SAFESTR(handledMsgsNames),
							useThisHandledMsgs ?
								*useThisHandledMsgs :
								mr->bg.handledMsgs);

	if(mr->fg.netHandle){
		len = snprintf_s(	buffer,
							bufferLen,
							"%s[%u/%u]",
							mr->mrc->name,
							mr->handle,
							mr->fg.netHandle);
	}else{
		len = snprintf_s(	buffer,
							bufferLen,
							"%s[%u]",
							mr->mrc->name,
							mr->handle);
	}

	buffer += len;
	bufferLen -= len;

	len = snprintf_s(	buffer,
						bufferLen,
						" BG:\n"
						"%s%s%s"
						"%s%s%s"
						,
						ownedBitNames[0] ? "OWNS(" : "",
						ownedBitNames,
						ownedBitNames[0] ? ")\n" : "",
						handledMsgsNames[0] ? "HANDLES(" : "",
						handledMsgsNames,
						handledMsgsNames[0] ? ")\n" : "");

	pd.msg.in.bg.getDebugString.buffer = buffer + len;
	pd.msg.in.bg.getDebugString.bufferLen = bufferLen - len;

	if(useThisBG){
		pd.msg.in.userStruct.bg = useThisBG;
	}

	if(useThisLocalBG){
		pd.msg.in.userStruct.localBG = useThisLocalBG;
	}

	mmRequesterMsgSend(&pd);
	
	if(!buffer[len]){
		char* estr = NULL;
		
		estrStackCreate(&estr);
		estrConcatf(&estr, "BG:");
		ParserWriteText(&estr, mr->mrc->pti.bg, pd.msg.in.userStruct.bg, 0, 0, 0);
		estrConcatf(&estr, "\nLocal BG:");
		ParserWriteText(&estr, mr->mrc->pti.localBG, pd.msg.in.userStruct.localBG, 0, 0, 0);
		len += snprintf_s(buffer + len, bufferLen - len, "%s", estr);
		estrDestroy(&estr);
	}
}

S32 mmGetDataClassName(	const char** nameOut,
						U32 mdc)
{
	if(!nameOut){
		return 0;
	}

	switch(mdc){
		xcase MDC_POSITION_TARGET:{
			*nameOut = "posTarget";
		}

		xcase MDC_POSITION_CHANGE:{
			*nameOut = "posChange";
		}

		xcase MDC_ROTATION_TARGET:{
			*nameOut = "rotTarget";
		}

		xcase MDC_ROTATION_CHANGE:{
			*nameOut = "rotChange";
		}

		xcase MDC_ANIMATION:{
			*nameOut = "animation";
		}

		xdefault:{
			*nameOut = "unknown";
			return 0;
		}
	}

	return 1;
}

void mmGetDataClassNames(	char* namesOut,
							S32 namesOutSize,
							U32 mdcBits)
{
	namesOut[0] = 0;
	
	FOR_BEGIN(i, MDC_COUNT);
	{
		U32			bit = BIT(i);
		const char* name;
		
		if(mdcBits & bit){
			mmGetDataClassName(&name, i);
			
			strcatf_s(	namesOut,
						namesOutSize,
						"%s%s",
						namesOut[0] ? ", " : "",
						name);
		}
	}
	FOR_END;
}

void mmGetHandledMsgsNames(	char* namesOut,
							S32 namesOutSize,
							U32 handledMsgs)
{
	namesOut[0] = 0;
	
	FOR_BEGIN(i, MR_HANDLED_MSGS_BIT_COUNT);
	{
		U32			bit = BIT(i);
		const char* name;
		
		if(handledMsgs & bit){
			switch(bit){
				xcase MR_HANDLED_MSG_INPUT_EVENT:
					name = "InputEvent";
				xcase MR_HANDLED_MSG_BEFORE_DISCUSSION:
					name = "BeforeDiscussion";
				xcase MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP:
					name = "DiscussDataOwnership";
				xcase MR_HANDLED_MSG_CREATE_DETAILS:
					name = "CreateDetails";
				xcase MR_HANDLED_MSG_OUTPUT_POSITION_TARGET:
					name = "OutputPosTarget";
				xcase MR_HANDLED_MSG_OUTPUT_POSITION_CHANGE:
					name = "OutputPosChange";
				xcase MR_HANDLED_MSG_OUTPUT_ROTATION_TARGET:
					name = "OutputRotTarget";
				xcase MR_HANDLED_MSG_OUTPUT_ROTATION_CHANGE:
					name = "OutputRotChange";
				xcase MR_HANDLED_MSG_OUTPUT_ANIMATION:
					name = "OutputAnimation";
				xcase MR_HANDLED_MSG_OVERRIDE_VALUE_SHOULD_REJECT:
					name = "OverrideValueShouldReject";
				xdefault:
					name = "Unknown";
			}
			
			strcatf_s(	namesOut,
						namesOutSize,
						"%s%s",
						namesOut[0] ? ", " : "",
						name);
		}
	}
	FOR_END;
}

static void mmLogRequesterStatesBG(	MovementManager* mm,
									const char* stateString,
									S32 logQueuedSyncs)
{
	char	stateTag[100];
	char	dataOwnersTag[100];
	char	dataOwnersTagNB[100];
	char	buffer[5000];
	char*	estrBuffer = NULL;

	estrStackCreateSize(&estrBuffer, 1000);
	
	sprintf(stateTag,
			"[bg_state.requesters:%s] ",
			stateString);

	sprintf(dataOwnersTagNB,
			"bg_state.dataOwners:%s",
			stateString);

	sprintf(dataOwnersTag,
			"[%s] ",
			dataOwnersTagNB);

	mmLog(	mm,
			NULL,
			"%s%s\\/*\\/*\\/--- BEGIN STATE LISTING: %s --------------------\\/*\\/*\\/",
			stateTag,
			MM_LOG_SECTION_PADDING_BEGIN,
			stateString);

	mmLog(	mm,
			NULL,
			"%sPos: (%1.2f, %1.2f, %1.2f) [%8.8x, %8.8x, %8.8x]",
			stateTag,
			vecParamsXYZ(mm->bg.pos),
			vecParamsXYZ((S32*)mm->bg.pos));

	mmLog(	mm,
			NULL,
			"%sRot: (%1.2f, %1.2f, %1.2f, %1.2f) [%8.8x, %8.8x, %8.8x, %8.8x]",
			stateTag,
			quatParamsXYZW(mm->bg.rot),
			quatParamsXYZW((S32*)mm->bg.rot));

	mmLog(	mm,
			NULL,
			"%spyFace: (%1.2f, %1.2f) [%8.8x, %8.8x]",
			stateTag,
			mm->bg.pyFace[0],
			mm->bg.pyFace[1],
			*(S32*)&mm->bg.pyFace[0],
			*(S32*)&mm->bg.pyFace[1]);

	mmLog(	mm,
			NULL,
			"%s--------- ALL REQUESTERS ---------",
			stateTag);

	EARRAY_CONST_FOREACH_BEGIN(mm->bg.requesters, i, isize);
	{
		MovementRequester* mr = mm->bg.requesters[i];

		mmRequesterGetDebugStringBG(mr, SAFESTR(buffer), NULL, NULL, NULL, NULL);

		mmLog(	mm,
				NULL,
				"%s%s",
				stateTag,
				buffer);

		mmRequesterGetSyncDebugString(mr, SAFESTR(buffer), NULL, NULL);
		
		mmLog(	mm,
				NULL,
				"%s%s",
				stateTag,
				buffer);

		EARRAY_CONST_FOREACH_BEGIN(mr->bg.resources, j, jsize);
		{
			MovementManagedResource* mmr = mr->bg.resources[j];

			estrClear(&estrBuffer);

			mmResourceGetConstantDebugString(	mm,
												mmr,
												NULL,
												NULL,
												NULL,
												&estrBuffer);

			mmLog(	mm,
					NULL,
					"%s(%u) %s: %s",
					stateTag,
					mmr->bg.handle,
					mmr->mmrc->name,
					estrBuffer);
		}
		EARRAY_FOREACH_END;
		
		if(logQueuedSyncs){
			EARRAY_CONST_FOREACH_BEGIN(mr->bg.queuedSyncs, j, jsize);
			{
				MovementQueuedSync* qs = mr->bg.queuedSyncs[j];
				
				mmRequesterGetSyncDebugString(mr, SAFESTR(buffer), qs->sync, qs->syncPublic);
				
				mmLog(	mm,
						NULL,
						"[bg_state.%s_sync] Queued Sync %u. s%u: %s",
						stateString,
						j + 1,
						qs->spc,
						buffer);
			}	
			EARRAY_FOREACH_END;
		}
	}
	EARRAY_FOREACH_END;

	mmLog(	mm,
			NULL,
			"%s--------- DATA OWNERS ---------",
			dataOwnersTag);

	ARRAY_FOREACH_BEGIN(mm->bg.dataOwner, i);
	{
		MovementRequester*	mr = mm->bg.dataOwner[i];
		const char*			name = "(none)";

		mmRequesterGetNameAndHandleBG(mr, SAFESTR(buffer));

		mmGetDataClassName(&name, i);

		mmLog(	mm,
				NULL,
				"%s%9s: %s",
				dataOwnersTag,
				name,
				buffer);
	}
	ARRAY_FOREACH_END;

	mmLastAnimLogLocal(mm, &mm->bg.lastAnim, dataOwnersTagNB, "bg.lastAnim");

	mmLog(	mm,
			NULL,
			"%s/\\*/\\*/\\---- END STATE LISTING: %s ------------------------/\\*/\\*/\\"
			"%s",
			stateTag,
			stateString,
			MM_LOG_SECTION_PADDING_END);

	estrDestroy(&estrBuffer);
}

static void mrLogOwnershipRelease(	MovementRequester* mr,
									U32 bits,
									const char* reason)
{
	char bitNames[200];
	
	mmGetDataClassNames(SAFESTR(bitNames), bits);

	mrLog(	mr,
			NULL,
			"Ownership released (%s), notifying: %s.\n",
			reason,
			bitNames);
}

static void mmRequesterSendMsgDataWasReleasedBG(MovementRequester* mr,
												U32 bits,
												const char* reason)
{
	MovementRequesterMsgPrivateData pd;

	if(MRLOG_IS_ENABLED(mr)){
		mrLogOwnershipRelease(mr, bits, reason);
	}

	mmRequesterMsgInitBG(	&pd,
							NULL,
							mr,
							MR_MSG_BG_DATA_WAS_RELEASED);

	pd.in.bg.dataWasReleased.dataClassBits = bits;
	pd.msg.in.bg.dataWasReleased.dataClassBits = bits;

	pd.msg.in.userStruct.toFG = MR_USERSTRUCT_TOFG(mr, MM_BG_SLOT);

	mmRequesterMsgSend(&pd);
}

static void mmSendMsgsReceiveOldDataBG(	MovementManager* mm,
										MovementRequester* mrOldOwner,
										U32 dataClassBits,
										MovementSharedData* sd)
{
	EARRAY_CONST_FOREACH_BEGIN(mm->bg.requesters, i, isize);
	{
		MovementRequester*				mr = mm->bg.requesters[i];
		MovementRequesterMsgPrivateData pd;
		
		if(mr == mrOldOwner){
			continue;
		}
		
		mmRequesterMsgInitBG(	&pd,
								NULL,
								mr,
								MR_MSG_BG_RECEIVE_OLD_DATA);
								
		pd.msg.in.bg.receiveOldData.dataClassBits = dataClassBits;
		pd.msg.in.bg.receiveOldData.sharedData = sd;
		
		mmRequesterMsgSend(&pd);
	}
	EARRAY_FOREACH_END;
}										

S32 mmCanShareOldDataBG(const MovementRequesterMsgPrivateData* pd){
	return	pd &&
			pd->msgType == MR_MSG_BG_DATA_WAS_RELEASED &&
			(	mgState.flags.isServer ||
				pd->mm->bg.flags.isPredicting);
}

void mrmShareOldS32BG(	const MovementRequesterMsg* msg,
						const char* name,
						S32 s32)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementSharedData					sd;

	if(!mmCanShareOldDataBG(pd)){
		return;
	}
	
	mrmLog(	msg,
			NULL,
			"Sharing old S32(%s): %d",
			name,
			s32);

	sd.name = name;
	sd.dataType = MSDT_S32;
	sd.data.s32 = s32;
	
	mmSendMsgsReceiveOldDataBG(	pd->mm,
								pd->mr,
								pd->in.bg.dataWasReleased.dataClassBits,
								&sd);
}

void mrmShareOldF32BG(	const MovementRequesterMsg* msg,
						const char* name,
						F32 f32)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementSharedData					sd;

	if(!mmCanShareOldDataBG(pd)){
		return;
	}
	
	mrmLog(	msg,
			NULL,
			"Sharing old F32(%s): %1.3f [%8.8x]",
			name,
			f32,
			*(S32*)&f32);

	sd.name = name;
	sd.dataType = MSDT_F32;
	sd.data.f32 = f32;
	
	mmSendMsgsReceiveOldDataBG(	pd->mm,
								pd->mr,
								pd->in.bg.dataWasReleased.dataClassBits,
								&sd);
}

void mrmShareOldVec3BG(	const MovementRequesterMsg* msg,
						const char* name,
						const Vec3 vec3)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementSharedData					sd;

	if(!mmCanShareOldDataBG(pd)){
		return;
	}
	
	mrmLog(	msg,
			NULL,
			"Sharing old Vec3(%s): (%1.3f, %1.3f, %1.3f) [%8.8x, %8.8x, %8.8x]",
			name,
			vecParamsXYZ(vec3),
			vecParamsXYZ((S32*)vec3));

	sd.name = name;
	sd.dataType = MSDT_VEC3;
	copyVec3(vec3, sd.data.vec3);
	
	mmSendMsgsReceiveOldDataBG(	pd->mm,
								pd->mr,
								pd->in.bg.dataWasReleased.dataClassBits,
								&sd);
}

void mrmShareOldQuatBG(	const MovementRequesterMsg* msg,
						const char* name,
						const Quat quat)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementSharedData					sd;

	if(!mmCanShareOldDataBG(pd)){
		return;
	}
	
	mrmLog(	msg,
			NULL,
			"Sharing old Quat(%s): (%1.3f, %1.3f, %1.3f, %1.3f) [%8.8x, %8.8x, %8.8x, %8.8x]",
			name,
			quatParamsXYZW(quat),
			quatParamsXYZW((S32*)quat));

	sd.name = name;
	sd.dataType = MSDT_QUAT;
	copyQuat(quat, sd.data.quat);
	
	mmSendMsgsReceiveOldDataBG(	pd->mm,
								pd->mr,
								pd->in.bg.dataWasReleased.dataClassBits,
								&sd);
}

static void mmLogPosAndRotBG(	MovementManager* mm,
								const char* tag)
{
	Mat3	mat;
	Vec3	pos;
	Vec3	pos2;
	
	quatToMat(mm->bg.rot, mat);

	// Log the rot as a RGB unit mat.

	FOR_BEGIN(i, 3);
	{
		U32	color = 0xff000000 | (0xff << ((2 - i) * 8));
		
		scaleAddVec3(mat[i], 2, mm->bg.pos, pos);
		
		if(i != 1){
			scaleAddVec3(mat[1], 0.1f, pos, pos);
		}

		mmLogSegment(	mm,
						NULL,
						tag,
						color,
						mm->bg.pos,
						pos);
	}
	FOR_END;
	
	// Log the facing vector.
	
	createMat3_2_YP(mat[2], mm->bg.pyFace);
	
	scaleAddVec3(mat[1], 2, mm->bg.pos, pos);
	scaleAddVec3(mat[2], 2, pos, pos2);

	mmLogSegment(	mm,
					NULL,
					tag,
					0xffffff00,
					pos,
					pos2);
}

static void mmSharedDataValueGetDebugString(char* buffer,
											size_t bufferLen,
											const char* name,
											MovementSharedDataType valueType,
											const MovementSharedDataValue* value)
{
	switch(valueType){
		xcase MSDT_F32:{
			snprintf_s(	buffer,
						bufferLen,
						"f32[%s] = %1.3f [%8.8x]",
						name,
						value->f32,
						*(S32*)&value->f32);
		}
		
		xcase MSDT_S32:{
			snprintf_s(	buffer,
						bufferLen,
						"s32[%s] = %d",
						name,
						value->s32);
		}

		xdefault:{
			snprintf_s(	buffer,
						bufferLen,
						"unknown_type_%d[%s] = ???",
						valueType,
						name);
		}
	}
}

static void mmGetInputStepTailBG(	MovementInputStep* miStep,
									MovementInputStep** miStepTailOut)
{
	while(miStep->bg.next){
		miStep = miStep->bg.next;
	}
	
	*miStepTailOut = miStep;
}

static void mmLogStancesBG(MovementManager* mm){
	char* estrBuffer = NULL;

	if(!eaSize(&mm->bg.stances)){
		return;
	}

	estrStackCreateSize(&estrBuffer, 200);

	EARRAY_CONST_FOREACH_BEGIN(mm->bg.stances, i, isize);
	{
		const MovementRequesterStance*	s = mm->bg.stances[i];
		MovementRegisteredAnimBit*		bit = NULL;

		mmRegisteredAnimBitGetByHandle(	&mgState.animBitRegistry,
										&bit,
										s->animBitHandle);

		estrConcatf(&estrBuffer,
					"%s(%u) (%s[%u]:%u)%s",
					bit ? bit->bitName : "UNKNOWN",
					s->animBitHandle,
					s->mr->mrc->name,
					s->mr->handle,
					eaFind(&s->mr->bg.stances, s) + 1,
					i == isize - 1 ? "" : ", ");
	}
	EARRAY_FOREACH_END;
	
	mmLog(	mm,
			NULL,
			"[bg.anim] Stances: %s",
			estrBuffer);
}

static void mmLogBeforeSingleStepBG(MovementManager* mm,
									MovementThreadData* td,
									MovementOutput* o,
									MovementInputStep* miStep)
{
	char*	estrBuffer = NULL;
	S32		offsetToServer = 0;

	estrStackCreateSize(&estrBuffer, 1000);

	mmLog(	mm,
			NULL,
			"[bg.time] "
			"\\/*\\/*\\/*\\/*\\/*\\/*\\/*\\/*\\/*\\/*\\/"
			" BEGIN %u "
			"\\/*\\/*\\/*\\/*\\/*\\/*\\/*\\/*\\/*\\/*\\/",
			FIRST_IF_SET(o->pc.client, o->pc.server));

	if(mgState.flags.isServer){
		offsetToServer =	o->pc.server -
							o->pc.client;
	}

	if(	mgState.bg.netReceive.cur.offset.clientToServer ||
		offsetToServer)
	{
		mmLog(	mm,
				NULL,
				"[bg.time] Server process: %u (offset: %d)",
				mgState.bg.pc.local.cur +
					mgState.bg.netReceive.cur.offset.clientToServer,
				FIRST_IF_SET(	mgState.bg.netReceive.cur.offset.clientToServer,
								offsetToServer));
	}

	if(miStep){
		MovementInputStep* miStepTail;
		
		mmGetInputStepTailBG(miStep, &miStepTail);

		mmLog(	mm,
				NULL,
				"[bg.time] Current %s step: c%u, s%u, diff %d",
				miStep->fg.flags.isForced ? "FORCED" : "real",
				miStep->pc.client,
				miStepTail->pc.server,
				miStep->pc.client - 
					miStepTail->pc.server);
	}

	if(eaSize(&td->toBG.miSteps)){
		S32 size = eaSize(&td->toBG.miSteps);

		mmLog(	mm,
				NULL,
				"[bg.input] Input step queue: c(%u-%u), s(%u-%u)",
				td->toBG.miSteps[0]->pc.client,
				td->toBG.miSteps[size - 1]->pc.client,
				td->toBG.miSteps[0]->pc.server,
				td->toBG.miSteps[size - 1]->pc.server);
	}
	else if(mm->bg.flags.isAttachedToClient){
		mmLog(	mm,
				NULL,
				"[bg.input] Input step queue: none");
	}

	mmLog(	mm,
			NULL,
			"[bg.time] Next start: %u",
			mgState.fg.frame.next.pcStart);

	mmLog(	mm,
			NULL,
			"[bg.time] System time: %ums",
			timeGetTime());

	if(mm->bg.flags.noCollision){
		mmLog(	mm,
				NULL,
				"[bg.collision] NoCollision: 1");
	}
	
	if(!mm->bg.gridEntry.cell){
		mmLog(mm, NULL, "[bg.collision] No grid entry.");
	}else{
		MovementManagerGridCell*	cell = mm->bg.gridEntry.cell;
		U32							sizeIndex = mm->bg.gridEntry.gridSizeIndex;

		mmLog(	mm,
				NULL,
				"[bg.collision] Grid pos (%d, %d, %d), %u mms, body radius %1.2f, sizeIndex %d (cell size %1.f, max body radius %1.f)",
				vecParamsXYZ(cell->posGrid),
				eaUSize(&cell->managers),
				mm->bg.bodyRadius,
				sizeIndex,
				sizeIndex < ARRAY_SIZE(mmGridSizeGroups) ? mmGridSizeGroups[sizeIndex].cellSize : 0,
				sizeIndex < ARRAY_SIZE(mmGridSizeGroups) ? mmGridSizeGroups[sizeIndex].maxBodyRadius : 0);
	}
	
	EARRAY_CONST_FOREACH_BEGIN(mm->bg.bodyInstances, i, isize);
	{
		MovementBodyInstance* bi = mm->bg.bodyInstances[i];
		
		if(!bi->body){
			continue;
		}

		estrClear(&estrBuffer);
		mmBodyGetDebugString(bi->body, &estrBuffer);
		
		mmLog(mm, NULL, "[bg.collision] BodyInstance[%u]: %s", i, estrBuffer);
	}
	EARRAY_FOREACH_END;

	mmLogStancesBG(mm);

	if(mm->bg.stOverrideValues){
		StashTableIterator	iter;
		StashElement		elem;
		
		stashGetIterator(mm->bg.stOverrideValues, &iter);
		
		while(stashGetNextElement(&iter, &elem)){
			MovementOverrideValueGroup* movg = stashElementGetPointer(elem);
			
			EARRAY_CONST_FOREACH_BEGIN(movg->movs, i, isize);
			{
				MovementOverrideValue*	mov = movg->movs[i];
				char					valueString[100];

				valueString[0] = 0;
				
				mmSharedDataValueGetDebugString(SAFESTR(valueString),
												movg->namePooled,
												mov->valueType,
												&mov->value);
				
				mmLog(	mm,
						NULL,
						"[bg.overrides]"
						" %s[%u][%u]: %s",
						mov->mr->mrc->name,
						mov->mr->handle,
						mov->handle,
						valueString);
			}
			EARRAY_FOREACH_END;
		}
	}

	mmLogPosAndRotBG(mm, "bg.processBefore");

	mmLogRequesterStatesBG(mm, "BEFORE", 1);
	
	mmLogResource(mm, NULL, "Resources before running step");

	estrDestroy(&estrBuffer);
}

static void mmLogAfterSingleStepBG(	MovementManager* mm,
									MovementOutput* o,
									U32 processCount)
{
	mmLogRequesterStatesBG(mm, "AFTER", 0);

	mmLogPosAndRotBG(mm, "bg.processAfter");
	
	mmLog(	mm,
			NULL,
			"[bg.time] "
			"/\\*/\\*/\\*/\\*/\\*/\\*/\\*/\\*/\\*/\\"
			" END %u "
			"/\\*/\\*/\\*/\\*/\\*/\\*/\\*/\\*/\\*/\\",
			o ?
				FIRST_IF_SET(o->pc.client, o->pc.server) :
				processCount);
}

static void mmRequesterAddToMsgBeforeDiscussionBG(	MovementManager* mm,
													MovementRequester* mr)
{
	if(!mm->bg.mel[MM_BG_EL_BEFORE_DISCUSSION].head){
		ASSERT_FALSE_AND_SET(mm->bg.flagsMutable.mrHandlesMsgBeforeDiscussion);
	}

	mmExecListAddHead(	&mm->bg.mel[MM_BG_EL_BEFORE_DISCUSSION],
						&mr->bg.execNode[MR_BG_EN_BEFORE_DISCUSSION]);
}

static void mmRequesterRemoveFromMsgBeforeDiscussionBG(	MovementManager* mm,
														MovementRequester* mr)
{
	mmExecListRemove(	&mm->bg.mel[MM_BG_EL_BEFORE_DISCUSSION],
						&mr->bg.execNode[MR_BG_EN_BEFORE_DISCUSSION]);
	
	if(!mm->bg.mel[MM_BG_EL_BEFORE_DISCUSSION].head){
		ASSERT_TRUE_AND_RESET(mm->bg.flagsMutable.mrHandlesMsgBeforeDiscussion);
	}
}

static void mmReleaseOwnershipForDestroyedRequestersBG(MovementManager* mm){
	// Take ownership away from destroyed requesters.

	PERFINFO_AUTO_START_FUNC();
	
	ARRAY_FOREACH_BEGIN(mm->bg.dataOwner, i);
	{
		MovementRequester* mr = mm->bg.dataOwner[i];

		if(SAFE_MEMBER(mr, bg.flags.destroyed)){
			U32 bit = BIT(i);

			mr->bg.flagsMutable.bgUnchangedSinceCopyToFG = 0;
			ASSERT_TRUE_AND_RESET_BITS(mr->bg.ownedDataClassBitsMutable, bit);

			mm->bg.dataOwnerMutable[i] = NULL;

			if(	i == MDC_ANIMATION &&
				gConf.bNewAnimationSystem)
			{
				mm->bg.flagsMutable.animOwnershipWasReleased = 1;
			}

			if(	!mr->bg.ownedDataClassBits &&
				mr->bg.handledMsgs & MR_HANDLED_MSG_BEFORE_DISCUSSION)
			{
				mmRequesterRemoveFromMsgBeforeDiscussionBG(mm, mr);
			}
			
			mmRequesterSendMsgDataWasReleasedBG(mr, bit, "requester destroyed");
		}
	}
	ARRAY_FOREACH_END;
	
	PERFINFO_AUTO_STOP();
}

static void mmSendInputStepBackToFG(	MovementManager* mm,
										MovementThreadData* td,
										MovementInputStep* miStep)
{
	assert(!miStep->bg.flags.inRepredict);

	ASSERT_FALSE_AND_SET(miStep->bg.flags.finished);

	eaPush(&td->toFG.finishedInputStepsMutable, miStep);
	
	MM_TD_SET_HAS_TOFG(mm, td);
	td->toFG.flagsMutable.hasFinishedInputSteps = 1;
}

static void mmSendMsgPosChangedBG(MovementManager* mm){
	if(!mm->msgHandler){
		return;
	}

	PERFINFO_AUTO_START_FUNC();
	{
		MovementManagerMsgPrivateData pd = {0};
		
		pd.mm = mm;
		pd.msgType = pd.msg.msgType = MM_MSG_BG_POS_CHANGED;
		pd.msg.bg.posChanged.threadData = mm->userThreadData[MM_BG_SLOT];
		pd.msg.bg.posChanged.pos = mm->bg.pos;
		
		mm->msgHandler(&pd.msg);
	}
	PERFINFO_AUTO_STOP();
}

static void mmSendFullViewStatusNotAtRestBG(MovementManager* mm,
											MovementThreadData* td)
{
	S32 doSend = 0;

	if(TRUE_THEN_RESET(mm->bg.flagsMutable.posIsAtRest)){
		doSend = 1;
	}

	if(TRUE_THEN_RESET(mm->bg.flagsMutable.rotIsAtRest)){
		doSend = 1;
	}

	if(TRUE_THEN_RESET(mm->bg.flagsMutable.pyFaceIsAtRest)){
		doSend = 1;
	}

	mmVerifyViewStatusToFG(mm);

	if(doSend){
		if(FALSE_THEN_SET(td->toFG.flagsMutable.viewStatusChanged)){
			#if MM_VERIFY_TOFG_VIEW_STATUS
				td->toFG.frameWhenViewStatusChanged = mgState.frameCount;
			#endif

			mmSetAfterSimWakesOnceBG(mm);
		}

		td->toFG.flagsMutable.posIsAtRest = 0;
		td->toFG.flagsMutable.rotIsAtRest = 0;
		td->toFG.flagsMutable.pyFaceIsAtRest = 0;
	}

	mmVerifyViewStatusToFG(mm);
}

static void mmRunSingleStepBG(	MovementManager* mm,
								MovementThreadData* td,
								MovementOutput* o,
								MovementInputStep* miStep,
								const S32 hasUserToBG)
{
	PERFINFO_AUTO_START_FUNC();
	
	// Log a bunch of stuff before running physics.

	if(MMLOG_IS_ENABLED(mm)){
		mmLogBeforeSingleStepBG(mm, td, o, miStep);
	}

	if(TRUE_THEN_RESET(mm->bg.flagsMutable.sendViewStatusChanged)){
		mmSendFullViewStatusNotAtRestBG(mm, td);
		mmSendMsgPosChangedBG(mm);
	}

	// Send requester input msgs.

	if(hasUserToBG){
		mmSendMsgsUpdatedToBG(mm);

		if(MMLOG_IS_ENABLED(mm)){
			mmLogRequesterStatesBG(mm, "AFTER UPDATED TOBG", 0);
		}
	}

	// Send updated sync msgs.
	
	if(TRUE_THEN_RESET(mm->bg.flagsMutable.mrHasNewSync)){
		mmSendMsgsUpdatedSync(mm, miStep);

		if(MMLOG_IS_ENABLED(mm)){
			mmLogRequesterStatesBG(mm, "AFTER UPDATED SYNC", 1);
		}
	}

	if(FALSE_THEN_SET(mm->bg.flagsMutable.didProcess)){
		MM_TD_SET_HAS_TOFG(mm, td);
		td->toFG.flagsMutable.didProcess = 1;
	}

	if(mm->bg.flags.mrHandlesMsgBeforeDiscussion){
		mmSendMsgsBeforeDiscussionBG(mm);
	}

	if(miStep){
		mmApplyInputStepBG(mm, miStep);
	}
	
	if(mm->bg.flags.mrHandlesMsgDiscussDataOwnership){
		mmSendMsgsDiscussDataOwnershipBG(mm, td, 0);
	}

	mmSendMsgsCreateOutputBG(mm, td, o);

	if(TRUE_THEN_RESET(mm->bg.flagsMutable.mrWasDestroyedOnThisStep)){
		mmReleaseOwnershipForDestroyedRequestersBG(mm);
	}

	if(MMLOG_IS_ENABLED(mm)){
		mmLogAfterSingleStepBG(mm, o, 0);
	}
	
	PERFINFO_AUTO_STOP();
}

static void mmLogRequesterSyncBG(MovementRequester* mr){
	char buffer[1000];

	mmRequesterGetSyncDebugString(	mr,
									SAFESTR(buffer),
									mr->userStruct.sync.bg,
									mr->userStruct.syncPublic.bg);

	mrLog(	mr,
			NULL,
			"New sync: %s",
			buffer);
}

static void mmSendMsgInitializeBG(	MovementManager* mm,
									MovementRequester* mr)
{
	MovementRequesterMsgPrivateData pd;

	mmRequesterMsgInitBG(	&pd,
							NULL,
							mr,
							MR_MSG_BG_INITIALIZE);

	mmRequesterMsgSend(&pd);
}

static void mmRequesterSetHandledMsgsBG(MovementManager* mm,
										MovementRequester* mr,
										U32 handledMsgs);

static void mmRepredictSetRequestersInputToProcessCountBG(	MovementManager* mm,
															const S32 hasUserToBG)
{
	const U32 cpc = mgState.bg.pc.local.cur;

	EARRAY_CONST_FOREACH_BEGIN(mm->bg.requesters, i, isize);
	{
		MovementRequester*				mr = mm->bg.requesters[i];
		MovementRequesterThreadData*	mrtd = MR_THREADDATA_BG(mr);
		MovementRequesterBGPredict*		predict = mr->bg.predict;
		MovementRequesterHistory*		h = NULL;

		// Fake the step count to make the isFirstCreateOutputOnThisStep flag work.
		
		mr->bg.betweenSimCountOfLastCreateOutput = mgState.bg.betweenSim.count - 1;

		EARRAY_CONST_FOREACH_BEGIN(predict->history, j, jsize);
		{
			MovementRequesterHistory* hCheck = predict->history[j];

			if(hCheck->cpc == cpc){
				h = hCheck;
				break;
			}
		}
		EARRAY_FOREACH_END;

		if(!h){
			mr->bg.flagsMutable.repredictNotCreatedYet = 1;
			mrLog(mr, NULL, "No history for c%u.", cpc);
			continue;
		}

		mrLog(	mr,
				NULL,
				"Found history for c%u: %s%s%s",
				cpc,
				h->in.flags.isFirstHistory ? "isFirstHistory, " : "",
				h->in.flags.hasNewSync ? "hasNewSync, " : "",
				h->in.flags.hasUserToBG ? "hasUserToBG, " : "");

		mr->bg.flagsMutable.repredictNotCreatedYet = 0;

		mrtd->toBG.flagsMutable.hasUserToBG = h->in.flags.hasUserToBG;
		mr->bg.flagsMutable.hasNewSync = h->in.flags.hasNewSync;
			
		if(h->in.flags.isFirstHistory){
			mrLog(	mr,
					NULL,
					"Setting to first history state (c%u), so clearing structs.",
					h->cpc);
								
			StructResetVoid(mr->mrc->pti.bg,
							mr->userStruct.bg);

			StructResetVoid(mr->mrc->pti.localBG,
							mr->userStruct.localBG);

			mmRequesterSetHandledMsgsBG(mm, mr, MR_HANDLED_MSGS_DEFAULT);

			mmSendMsgInitializeBG(mm, mr);
		}

		if(	hasUserToBG ||
			!mr->bg.flags.repredictDidRestoreInput)
		{
			if(h->in.toBG){
				mmStructCopy(	mr->mrc->pti.toBG,
								h->in.toBG,
								MR_USERSTRUCT_TOBG(mr, MM_BG_SLOT));
			}
		}

		if(	mr->bg.flags.hasNewSync ||
			!mr->bg.flags.repredictDidRestoreInput)
		{
			if(h->in.sync){
				mmStructCopy(	mr->mrc->pti.sync,
								h->in.sync,
								mr->userStruct.sync.bg);
			}

			if(h->in.syncPublic){
				mmStructCopy(	mr->mrc->pti.syncPublic,
								h->in.syncPublic,
								mr->userStruct.syncPublic.bg);
			}

			if(MMLOG_IS_ENABLED(mm)){
				mmLogRequesterSyncBG(mr);
			}
		}

		mr->bg.flagsMutable.repredictDidRestoreInput = 1;
	}
	EARRAY_FOREACH_END;
}

static void mmPredictedStepDestroyBG(	MovementManager* mm,
										MovementThreadData* td,
										MovementPredictedStep** psInOut)
{
	MovementPredictedStep* ps = SAFE_DEREF(psInOut);

	if(ps){
		if(ps->in.miStep){
			MovementInputStep* miStep = ps->in.miStep;

			ps->in.miStep = NULL;

			ASSERT_TRUE_AND_RESET(miStep->bg.flags.inRepredict);

			mmSendInputStepBackToFG(mm, td, miStep);
		}
		
		eaiDestroy(&ps->in.stanceBits);

		mmLastAnimReset(&ps->in.lastAnim);

		SAFE_FREE(*psInOut);
	}
}

static void mmRequestersDestroyHistoryUntilBG(	MovementManager* mm,
												const U32 cpcMinToKeep)
{
	mm->bg.flagsMutable.mrHasHistory = 0;
	
	EARRAY_CONST_FOREACH_BEGIN(mm->bg.requesters, i, isize);
	{
		MovementRequester*			mr = mm->bg.requesters[i];
		MovementRequesterBGPredict*	predict = mr->bg.predict;
		S32							first;

		if(!predict){
			continue;
		}

		first = eaSize(&predict->history);

		EARRAY_CONST_FOREACH_BEGIN(predict->history, j, jsize);
		{
			MovementRequesterHistory* h = predict->history[j];

			if(subS32(h->cpc, cpcMinToKeep) >= 0){
				first = j;
				break;
			}

			if(	h->in.flags.isFirstHistory &&
				!mr->bg.flags.receivedNetHandle)
			{
				MovementRequesterMsgPrivateData pd;

				mrLog(mr, NULL, "Destroying first history, never received ");

				mmRequesterMsgInitBG(	&pd,
										NULL,
										mr,
										MR_MSG_BG_IS_MISPREDICTED);

				mmRequesterMsgSend(&pd);
			}

			mmRequesterHistoryDestroyBG(mm, mr, &h);
		}
		EARRAY_FOREACH_END;

		if(first){
			S32 size = eaSize(&predict->history) - first;

			CopyStructsFromOffset(	predict->history,
									first,
									size);

			eaSetSize(	&predict->historyMutable,
						size);

			if(size){
				mm->bg.flagsMutable.mrHasHistory = 1;
			}else{
				eaDestroy(&predict->historyMutable);
			}
		}
		else if(eaSize(&predict->history)){
			mm->bg.flagsMutable.mrHasHistory = 1;
		}
	}
	EARRAY_FOREACH_END;
}

static void mmLogRequesterStateBG(	MovementManager* mm,
									MovementRequester* mr,
									const char* tag,
									const char* prefix,
									const U32* useThisOwnedDataClassBits,
									const U32* useThisHandledMsgs,
									void* useThisBG,
									void* useThisLocalBG)
{
	char buffer[1000];

	mmRequesterGetDebugStringBG(mr,
								SAFESTR(buffer),
								useThisOwnedDataClassBits,
								useThisHandledMsgs,
								useThisBG,
								useThisLocalBG);

	mmLog(	mm,
			NULL,
			"%s%s%s%s: %s",
			tag ? "[" : "",
			tag ? tag : "",
			tag ? "] " : "",
			prefix,
			buffer);
}

static void copyOrResetStruct(	ParseTable* pti,
								void* source,
								void* target)
{
	if(source){
		mmStructCopy(pti, source, target);
	}else{
		StructResetVoid(pti, target);
	}
}

static void mmRequesterSetHandledMsgsBG(MovementManager* mm,
										MovementRequester* mr,
										U32 handledMsgs)
{
	U32 newMsgs = handledMsgs & ~mr->bg.handledMsgs;
	U32 oldMsgs = mr->bg.handledMsgs & ~handledMsgs;
	U32 msgBit;
	
	mr->bg.flagsMutable.bgUnchangedSinceCopyToFG = 0;
	mr->bg.handledMsgsMutable = handledMsgs;

	if(newMsgs){
		for(msgBit = 1; msgBit <= MR_HANDLED_MSGS_ALL; msgBit <<= 1){
			if(newMsgs & msgBit){
				switch(msgBit){
					#define ADD(x, y) mmExecListAddHead(&mm->bg.mel[x], &mr->bg.execNode[y]);

					xcase MR_HANDLED_MSG_INPUT_EVENT:{
						ADD(MM_BG_EL_INPUT_EVENT,
							MR_BG_EN_INPUT_EVENT);
					}
					
					xcase MR_HANDLED_MSG_BEFORE_DISCUSSION:{
						if(mr->bg.ownedDataClassBits){
							mmRequesterAddToMsgBeforeDiscussionBG(mm, mr);
						}
					}
					
					xcase MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP:{
						if(!mm->bg.mel[MM_BG_EL_DISCUSS_DATA_OWNERSHIP].head){
							ASSERT_FALSE_AND_SET(mm->bg.flagsMutable.mrHandlesMsgDiscussDataOwnership);
						}
						
						ADD(MM_BG_EL_DISCUSS_DATA_OWNERSHIP,
							MR_BG_EN_DISCUSS_DATA_OWNERSHIP);
					}
					
					xcase MR_HANDLED_MSG_CREATE_DETAILS:{
						if(!mm->bg.mel[MM_BG_EL_CREATE_DETAILS].head){
							ASSERT_FALSE_AND_SET(mm->bg.flagsMutable.mrHandlesMsgCreateDetails);
						}

						ADD(MM_BG_EL_CREATE_DETAILS,
							MR_BG_EN_CREATE_DETAILS);
					}

					xcase MR_HANDLED_MSG_OVERRIDE_VALUE_SHOULD_REJECT:{
						if(!mm->bg.mel[MM_BG_EL_REJECT_OVERRIDE].head){
							ASSERT_FALSE_AND_SET(mm->bg.flagsMutable.mrHandlesMsgRejectOverride);
						}

						ADD(MM_BG_EL_REJECT_OVERRIDE,
							MR_BG_EN_REJECT_OVERRIDE);
					}
					
					#undef ADD

					#define HANDLE_MDC(x)											\
						xcase MR_HANDLED_MSG_OUTPUT_##x:{							\
							if(mm->bg.dataOwner[MDC_##x] == mr){					\
								assert(mr->bg.ownedDataClassBits & MDC_BIT_##x);	\
								mm->bg.dataOwnerEnabledMaskMutable |= MDC_BIT_##x;	\
							}														\
						}
					
					HANDLE_MDC(POSITION_TARGET)
					HANDLE_MDC(POSITION_CHANGE)
					HANDLE_MDC(ROTATION_TARGET)
					HANDLE_MDC(ROTATION_CHANGE)
					HANDLE_MDC(ANIMATION)
					
					#undef HANDLE_MDC
				}

				newMsgs &= ~msgBit;
				
				if(!newMsgs){
					break;
				}
			}
		}
	}
		
	if(oldMsgs){
		for(msgBit = 1; msgBit <= MR_HANDLED_MSGS_ALL; msgBit <<= 1){
			if(oldMsgs & msgBit){
				switch(msgBit){
					#define REMOVE(x, y) mmExecListRemove(&mm->bg.mel[x], &mr->bg.execNode[y]);

					xcase MR_HANDLED_MSG_INPUT_EVENT:{
						REMOVE(	MM_BG_EL_INPUT_EVENT,
								MR_BG_EN_INPUT_EVENT);
					}
					
					xcase MR_HANDLED_MSG_BEFORE_DISCUSSION:{
						if(mr->bg.ownedDataClassBits){
							mmRequesterRemoveFromMsgBeforeDiscussionBG(mm, mr);
						}
					}
					
					xcase MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP:{
						REMOVE(	MM_BG_EL_DISCUSS_DATA_OWNERSHIP,
								MR_BG_EN_DISCUSS_DATA_OWNERSHIP);

						if(!mm->bg.mel[MM_BG_EL_DISCUSS_DATA_OWNERSHIP].head){
							ASSERT_TRUE_AND_RESET(mm->bg.flagsMutable.mrHandlesMsgDiscussDataOwnership);
						}
					}
					
					xcase MR_HANDLED_MSG_CREATE_DETAILS:{
						REMOVE(	MM_BG_EL_CREATE_DETAILS,
								MR_BG_EN_CREATE_DETAILS);

						if(!mm->bg.mel[MM_BG_EL_CREATE_DETAILS].head){
							ASSERT_TRUE_AND_RESET(mm->bg.flagsMutable.mrHandlesMsgCreateDetails);
						}
					}
					
					xcase MR_HANDLED_MSG_OVERRIDE_VALUE_SHOULD_REJECT:{
						REMOVE(	MM_BG_EL_REJECT_OVERRIDE,
								MR_BG_EN_REJECT_OVERRIDE);

						if(!mm->bg.mel[MM_BG_EL_REJECT_OVERRIDE].head){
							ASSERT_TRUE_AND_RESET(mm->bg.flagsMutable.mrHandlesMsgRejectOverride);
						}
					}

					#undef REMOVE

					#define HANDLE_MDC(x)											\
						xcase MR_HANDLED_MSG_OUTPUT_##x:{							\
							if(mm->bg.dataOwner[MDC_##x] == mr){					\
								assert(mr->bg.ownedDataClassBits & MDC_BIT_##x);	\
								mm->bg.dataOwnerEnabledMaskMutable &= ~MDC_BIT_##x;	\
							}														\
						}
					
					HANDLE_MDC(POSITION_TARGET)
					HANDLE_MDC(POSITION_CHANGE)
					HANDLE_MDC(ROTATION_TARGET)
					HANDLE_MDC(ROTATION_CHANGE)
					HANDLE_MDC(ANIMATION)
					
					#undef HANDLE_MDC
				}

				oldMsgs &= ~msgBit;

				if(!oldMsgs){
					break;
				}				
			}
		}
	}
}

static void mmRepredictRestoreRequesterHistoryBG(	MovementManager* mm,
													MovementRequester* mr,
													MovementRequesterHistory* h,
													S32 index)
{
	copyOrResetStruct(	mr->mrc->pti.bg,
						h->out.bg,
						mr->userStruct.bg);

	copyOrResetStruct(	mr->mrc->pti.localBG,
						h->out.localBG,
						mr->userStruct.localBG);

	if(h->out.bg){
		if(h->out.ownedDataClassBits){
			// Set me as the owner of everything I say I own, might be taken away later.

			ARRAY_FOREACH_BEGIN(mm->bg.dataOwner, i);
			{
				const U32 mdcBit = BIT(i);

				if(h->out.ownedDataClassBits & mdcBit){
					MovementRequester* mrOwner = mm->bg.dataOwner[i];

					// Clear current owner.
					
					if(mrOwner){
						mrOwner->bg.flagsMutable.bgUnchangedSinceCopyToFG = 0;
						ASSERT_TRUE_AND_RESET_BITS(	mrOwner->bg.ownedDataClassBitsMutable,
													mdcBit);

						if(mrOwner->bg.handledMsgs & (MR_HANDLED_MSG_OUTPUT_POSITION_TARGET << i)){
							mm->bg.dataOwnerEnabledMaskMutable &= ~mdcBit;
						}

						if(	!mrOwner->bg.ownedDataClassBits &&
							mrOwner->bg.handledMsgs & MR_HANDLED_MSG_BEFORE_DISCUSSION)
						{
							mmRequesterRemoveFromMsgBeforeDiscussionBG(mm, mrOwner);
						}
					}
					
					// Set myself as current owner.

					if(	!mr->bg.ownedDataClassBits &&
						mr->bg.handledMsgs & MR_HANDLED_MSG_BEFORE_DISCUSSION)
					{
						mmRequesterAddToMsgBeforeDiscussionBG(mm, mr);
					}

					mm->bg.dataOwnerMutable[i] = mr;
					mr->bg.flagsMutable.bgUnchangedSinceCopyToFG = 0;
					ASSERT_FALSE_AND_SET_BITS(	mr->bg.ownedDataClassBitsMutable,
												mdcBit);
					
					// Toggle the bit for whether I handle this message or not.
					
					if(mr->bg.handledMsgs & (MR_HANDLED_MSG_OUTPUT_POSITION_TARGET << i)){
						mm->bg.dataOwnerEnabledMaskMutable |= mdcBit;
					}else{
						mm->bg.dataOwnerEnabledMaskMutable &= ~mdcBit;
					}
				}
			}
			ARRAY_FOREACH_END;
		}
		
		mmRequesterSetHandledMsgsBG(mm, mr, h->out.handledMsgs);
	}

	if(MMLOG_IS_ENABLED(mm)){
		mmLogRequesterStateBG(	mm,
								mr,
								"bg.requester",
								"Setting cur to",
								NULL,
								NULL,
								NULL,
								NULL);
	}
}

static void mmRepredictRequesterBackupLatestInputsBG(	MovementManager* mm,
														MovementRequester* mr)
{
	MovementRequesterThreadData*	mrtd = MR_THREADDATA_BG(mr);
	MovementRequesterBGPredict*		predict = mr->bg.predict;

	// Copy flags.
	
	predict->latestBackup.flags.hasNewSync = mr->bg.flags.hasNewSync;
	predict->latestBackup.flags.hasUserToBG = mrtd->toBG.flags.hasUserToBG;

	// Copy the original toBG struct.

	mmStructAllocAndCopy(	mr->mrc->pti.toBG,
							predict->latestBackup.userStruct.toBG,
							MR_USERSTRUCT_TOBG(mr, MM_BG_SLOT),
							mm);

	// Copy the original sync struct.

	mmStructAllocAndCopy(	mr->mrc->pti.sync,
							predict->latestBackup.userStruct.syncBG,
							mr->userStruct.sync.bg,
							mm);

	// Copy the original syncPublic struct.

	if(mr->userStruct.syncPublic.bg){
		mmStructAllocAndCopy(	mr->mrc->pti.syncPublic,
								predict->latestBackup.userStruct.syncPublic.bg,
								mr->userStruct.syncPublic.bg,
								mm);
	}
}

static void mmRepredictBackupLatestInputsBG(MovementManager* mm){
	mm->bg.latestBackup.flags.mrHasNewSync = mm->bg.flags.mrHasNewSync;

	EARRAY_CONST_FOREACH_BEGIN(mm->bg.requesters, i, isize);
	{
		MovementRequester* mr = mm->bg.requesters[i];
		
		// Flag me to have my sync state restored the first time I'm used.

		mr->bg.flagsMutable.repredictDidRestoreInput = 0;

		// Backup the original inputs.

		mmRepredictRequesterBackupLatestInputsBG(mm, mr);
	}
	EARRAY_FOREACH_END;
}

static void mmRepredictRestoreLatestInputsBG(MovementManager* mm){
	mm->bg.flagsMutable.mrHasNewSync = mm->bg.latestBackup.flags.mrHasNewSync;

	// Restore the original toBG and syncBG for each requester.

	EARRAY_CONST_FOREACH_BEGIN(mm->bg.requesters, i, size);
	{
		MovementRequester*				mr = mm->bg.requesters[i];
		MovementRequesterThreadData*	mrtd = MR_THREADDATA_BG(mr);
		MovementRequesterBGPredict*		predict = mr->bg.predict;

		// Fake the step count to make the isFirstCreateOutputOnThisStep flag work.
		
		mr->bg.betweenSimCountOfLastCreateOutput = mgState.bg.betweenSim.count - 1;

		mr->bg.flagsMutable.repredictNotCreatedYet = 0;
		
		mr->bg.flagsMutable.hasNewSync = predict->latestBackup.flags.hasNewSync;
		mrtd->toBG.flagsMutable.hasUserToBG = predict->latestBackup.flags.hasUserToBG;

		if(predict->latestBackup.userStruct.toBG){
			mmStructCopy(	mr->mrc->pti.toBG,
							predict->latestBackup.userStruct.toBG,
							MR_USERSTRUCT_TOBG(mr, MM_BG_SLOT));

			mmStructDestroy(mr->mrc->pti.toBG,
							predict->latestBackup.userStruct.toBG,
							mm);
		}

		if(predict->latestBackup.userStruct.syncBG){
			mmStructCopy(	mr->mrc->pti.sync,
							predict->latestBackup.userStruct.syncBG,
							mr->userStruct.sync.bg);

			mmStructDestroy(mr->mrc->pti.sync,
							predict->latestBackup.userStruct.syncBG,
							mm);
		}

		if(predict->latestBackup.userStruct.syncPublic.bg){
			mmStructCopy(	mr->mrc->pti.syncPublic,
							predict->latestBackup.userStruct.syncPublic.bg,
							mr->userStruct.syncPublic.bg);

			mmStructDestroy(mr->mrc->pti.syncPublic,
							predict->latestBackup.userStruct.syncPublic.bg,
							mm);
		}
	}
	EARRAY_FOREACH_END;
}

static S32 mmRequesterDestroyStanceByIndexBG(	MovementManager* mm,
												MovementRequester* mr,
												U32 index)
{
	MovementRequesterStance* s;

	PERFINFO_AUTO_START_FUNC();
	
	if(index >= eaUSize(&mr->bg.stances)){
		PERFINFO_AUTO_STOP();
		return 0;
	}
	
	s = mr->bg.stancesMutable[index];
	
	if(!s){
		PERFINFO_AUTO_STOP();
		return 0;
	}
	
	assert(s->mr == mr);
	
	// Remove from mr.
	
	mr->bg.stancesMutable[index] = NULL;

	while(	eaSize(&mr->bg.stances) &&
			!eaTail(&mr->bg.stances))
	{
		eaPop(&mr->bg.stancesMutable);
	}
	
	if(!eaSize(&mr->bg.stances)){
		eaDestroy(&mr->bg.stancesMutable);
	}
	
	// Remove from mm.

	if(eaFindAndRemove(&mm->bg.stancesMutable, s) < 0){
		assertmsg(0, "Stance missing from mm.");
	}
	
	if(!eaSize(&mm->bg.stances)){
		eaDestroy(&mm->bg.stancesMutable);
	}
	
	if(MMLOG_IS_ENABLED(mm)){
		MovementRegisteredAnimBit* bit = NULL;

		mmRegisteredAnimBitGetByHandle(	&mgState.animBitRegistry,
										&bit,
										s->animBitHandle);

		mrLog(	mr,
				NULL,
				"Destroyed %sstance handle %u: %s(%u)",
				s->isPredicted ? "predicted " : "",
				index + 1,
				bit ? bit->bitName : "UNKNOWN",
				s->animBitHandle);
		
		mmLogStancesBG(mm);
	}

	SAFE_FREE(s);

	mm->bg.flagsMutable.animStancesChanged = 1;

	PERFINFO_AUTO_STOP();

	return 1;
}

static void mmRequesterDestroyStancesBG(MovementManager* mm,
										MovementRequester* mr)
{
	while(mr->bg.stances){
		mmRequesterDestroyStanceByIndexBG(mm, mr, eaSize(&mr->bg.stances) - 1);
	}
}

static void mmRepredictSetRequesterToHistoricalStateBG(	MovementManager* mm,
														MovementRequester* mr,
														const S32 index,
														const U32 cpc)
{
	MovementRequesterHistory*		h;
	MovementRequesterThreadData*	mrtd = MR_THREADDATA_BG(mr);
	MovementRequesterBGPredict*		predict = mr->bg.predict;

	// Everything created in BG should have been destroyed.

	assert(mr->bg.flags.createdInFG);
	assert(!mr->bg.ownedDataClassBits);

	// Make me be undestroyed.

	if(!mrtd->toFG.flags.destroyed){
		mr->bg.flagsMutable.destroyed = 0;
	}

	// If this mr has a matching history, then it's not from the future.

	if(!eaSize(&predict->history)){
		mrLog(	mr,
				NULL,
				"[mr.history] No history states available for c%u!",
				cpc);
		
		// Probably a new requester.

		return;
	}

	h = predict->history[0];

	if(h->cpc == cpc){
		mrLog(	mr,
				NULL,
				"[mr.history] Found matching history for c%u!",
				cpc);

		mmRepredictRestoreRequesterHistoryBG(mm, mr, h, index);
	}else{
		mrLog(	mr,
				NULL,
				"[mr.history] First history didn't match c%u!",
				cpc);
	}
	
	mmRequesterDestroyStancesBG(mm, mr);

	{
		MovementRequesterMsgPrivateData pd;

		mmRequesterMsgInitBG(	&pd,
								NULL,
								mr,
								MR_MSG_BG_OVERRIDE_ALL_UNSET);

		mmRequesterMsgSend(&pd);
	}
	
	mr->bg.overrideHandlePrev = 0;
}

static void mmRepredictSetRequestersToHistoricalStateBG(MovementManager* mm,
														const U32 cpc)
{
	EARRAY_CONST_FOREACH_BEGIN(mm->bg.requesters, i, isize);
	{
		mmRepredictSetRequesterToHistoricalStateBG(	mm,
													mm->bg.requesters[i],
													i,
													cpc);
	}
	EARRAY_FOREACH_END;
}

static void mmPredictedStepsDestroyUntilBG(	MovementManager* mm,
											MovementThreadData* td,
											const U32 cpcStart)
{
	while(eaSize(&mm->bg.predictedSteps)){
		MovementPredictedStep*	ps = mm->bg.predictedSteps[0];
		S32						diff = subS32(ps->pc.client, cpcStart);
		
		if(	cpcStart &&
			diff >= 0)
		{
			break;
		}
		
		// Remove from the array.
		
		eaRemove(&mm->bg.predictedStepsMutable, 0);
		
		if(!eaSize(&mm->bg.predictedSteps)){
			eaDestroy(&mm->bg.predictedStepsMutable);
			ASSERT_TRUE_AND_RESET(mm->bg.flagsMutable.hasPredictedSteps);
		}
		
		// Clear the ps.
		
		mmPredictedStepDestroyBG(mm, td, &ps);
	}
}

static void mmSimBodyInstanceDestroyBG(MovementSimBodyInstance* sbi){
	sbi->flags.destroyed = 1;
}

static void mmLogBeforeRepredictBG(	MovementManager* mm,
									MovementThreadData* td)
{
	const MovementThreadDataToBGRepredict* r = td->toBG.repredict;

	mmLog(	mm,
			NULL,
			"[bg.repredict] Repredicting from c%u/s%u (previously c%u/s%u), %u forced steps.",
			r->cpc,
			r->spc,
			SAFE_MEMBER(mm->bg.repredict, cpcPrev),
			SAFE_MEMBER(mm->bg.repredict, spcPrev),
			r->forcedStepCount);
			
	mmLog(	mm,
			NULL,
			"[bg.repredict] All predicted steps:");
			
	EARRAY_CONST_FOREACH_BEGIN(mm->bg.predictedSteps, i, isize);
	{
		MovementPredictedStep* ps = mm->bg.predictedSteps[i];

		mmLog(	mm,
				NULL,
				"[bg.repredict] c%u/s%u: pos(%1.2f, %1.2f, %1.2f)",
				ps->pc.client,
				ps->pc.server,
				vecParamsXYZ(ps->out.pos));
	}
	EARRAY_FOREACH_END;

	mmLogResource(	mm,
					NULL,
					"Resources before reprediction");
}

static void mmOutputRepredictCreateBG(	MovementManager* mm,
										MovementThreadData* td,
										MovementOutputRepredict** morOut,
										MovementOutput* o,
										MovementOutput* oTemp)
{
	MovementOutputRepredict* mor = eaPop(&mm->bg.available.repredictsMutable);
	
	if(mor){
		eaiClearFast(&mor->dataMutable.anim.values);
		oTemp->dataMutable = mor->data;
	}else{
		mmOutputRepredictCreate(&mor);
	}
	
	*morOut = mor;
	mor->frameCount = mgState.frameCount;
	
	// 4/30/2012 AM Grab requester is hitting this - removal suggested by Martin and seems to cause no later crashes
	//if(eaSize(&td->toFG.repredicts)){
	//	assert( td->toFG.repredicts[eaSize(&td->toFG.repredicts) - 1]->o->pc.client ==
	//	o->pc.client - MM_PROCESS_COUNTS_PER_STEP	);
	//}
	
	MM_TD_SET_HAS_TOFG(mm, td);
	td->toFG.flagsMutable.hasRepredicts = 1;
	eaPush(&td->toFG.repredictsMutable, mor);
	
	#if MM_VERIFY_REPREDICTS
		eaiPush(&td->toFG.repredictPCs, o->pc.client);
	#endif
	
	mor->o = o;
	
	if(TRUE_THEN_RESET(mm->bg.flagsMutable.sendForcedSetCountToFG)){
		mmLog(	mm,
				NULL,
				"[bg.repredict] Disabling repredict offset for c%u/s%u.",
				o->pc.client,
				o->pc.server);

		mor->flags.disableRepredictOffset = 1;

		MM_TD_SET_HAS_TOFG(mm, td);
		td->toFG.flagsMutable.hasForcedSetCount = 1;
		td->toFG.forcedSetCount = mm->bg.forcedSetCount;
	}
}

static void mmOverrideTableClearBG(MovementManager* mm){
	if(mm->bg.stOverrideValues){
		StashTableIterator	iter;
		StashElement		elem;
		
		stashGetIterator(mm->bg.stOverrideValues, &iter);
		
		while(stashGetNextElement(&iter, &elem)){
			MovementOverrideValueGroup* movg = stashElementGetPointer(elem);
			
			EARRAY_CONST_FOREACH_BEGIN(movg->movs, i, isize);
			{
				SAFE_FREE(movg->movsMutable[i]);
			}
			EARRAY_FOREACH_END;
			
			eaDestroy(&movg->movsMutable);
			
			SAFE_FREE(movg);
		}
		
		stashTableClear(mm->bg.stOverrideValues);
	}
}

static void mmReclaimSimBodyInstanceHandlesBG(MovementManager* mm){
	while(	eaSize(&mm->bg.simBodyInstances) &&
			!eaTail(&mm->bg.simBodyInstances))
	{
		eaSetSize(	&mm->bg.simBodyInstancesMutable,
					eaSize(&mm->bg.simBodyInstances) - 1);
	}
}

static void mmRepredictInitSimBodiesBG(MovementManager* mm){
	EARRAY_CONST_FOREACH_BEGIN(mm->bg.simBodyInstances, i, isize);
	{
		MovementSimBodyInstance*		sbi = mm->bg.simBodyInstances[i];
		MovementRequesterMsgPrivateData pd;
		MovementRequesterMsgOut			out;

		if(!sbi){
			continue;
		}

		mmRequesterMsgInitBG(	&pd,
								&out,
								sbi->mr,
								MR_MSG_BG_INIT_REPREDICT_SIM_BODY);

		pd.in.bg.initRepredictSimBody.handle = i + 1;
		pd.msg.in.bg.initRepredictSimBody.handle = i + 1;

		mmRequesterMsgSend(&pd);
		
		// Destroy sbi if told to and it wasn't already destroyed using this msg.
		
		if(!pd.in.bg.initRepredictSimBody.handle){
			isize = eaSize(&mm->bg.simBodyInstances);
		}
		else if(out.bg.initRepredictSimBody.unused){
			sbi->mr = NULL;

			mmRareLockEnter(mm);
			{
				assert(mm->bg.simBodyInstances[i] == sbi);
				
				mm->bg.simBodyInstancesMutable[i] = NULL;
			}
			mmRareLockLeave(mm);
			
			mmSimBodyInstanceDestroyBG(sbi);
		}
	}
	EARRAY_FOREACH_END;

	mmRareLockEnter(mm);
	{
		mmReclaimSimBodyInstanceHandlesBG(mm);
	}
	mmRareLockLeave(mm);
}

static void mmSendMsgsForceChangedPosBG(MovementManager* mm){
	EARRAY_CONST_FOREACH_BEGIN(mm->bg.requesters, i, isize);
	{
		MovementRequester*				mr = mm->bg.requesters[i];
		MovementRequesterMsgPrivateData pd;
		
		mmRequesterMsgInitBG(	&pd,
								NULL,
								mr,
								MR_MSG_BG_FORCE_CHANGED_POS);

		mmRequesterMsgSend(&pd);
	}
	EARRAY_FOREACH_END;
}

static void mmSendMsgsForceChangedRotBG(MovementManager* mm){
	EARRAY_CONST_FOREACH_BEGIN(mm->bg.requesters, i, isize);
	{
		MovementRequester*				mr = mm->bg.requesters[i];
		MovementRequesterMsgPrivateData pd;
		
		mmRequesterMsgInitBG(	&pd,
								NULL,
								mr,
								MR_MSG_BG_FORCE_CHANGED_ROT);

		mmRequesterMsgSend(&pd);
	}
	EARRAY_FOREACH_END;
}

static void mmRepredictClearCurrentDataOwnersBG(MovementManager* mm){
	ARRAY_FOREACH_BEGIN(mm->bg.dataOwner, i);
	{
		MovementRequester* mr = mm->bg.dataOwner[i];

		if(mr){
			const U32 bit = BIT(i);
			
			mm->bg.dataOwnerMutable[i] = NULL;
			
			mr->bg.flagsMutable.bgUnchangedSinceCopyToFG = 0;
			ASSERT_TRUE_AND_RESET_BITS(mr->bg.ownedDataClassBitsMutable, bit);

			if(	!mr->bg.ownedDataClassBits &&
				mr->bg.handledMsgs & MR_HANDLED_MSG_BEFORE_DISCUSSION)
			{
				mmRequesterRemoveFromMsgBeforeDiscussionBG(mm, mr);
			}
		}
	}
	ARRAY_FOREACH_END;
}

static void mmRepredictLogRequesterHistoriesBG(MovementManager* mm){
	if(!MMLOG_IS_ENABLED(mm)){
		return;
	}
	
	EARRAY_CONST_FOREACH_BEGIN(mm->bg.requesters, i, isize);
	{
		const MovementRequester*			mr = mm->bg.requesters[i];
		const MovementRequesterBGPredict*	predict = mr->bg.predict;
		S32									size = predict ? eaSize(&predict->history) : 0;
		
		if(!size){
			mrLog(mr, NULL, "No history.");
		}else{
			mrLog(	mr,
					NULL,
					"History: c(%u-%u)",
					predict->history[0]->cpc,
					predict->history[size - 1]->cpc);

			EARRAY_CONST_FOREACH_BEGIN(predict->history, j, jsize);
			{
				const MovementRequesterHistory* h = predict->history[j];

				mrLog(	mr,
						NULL,
						"  c%u: %s%s%s",
						h->cpc,
						h->in.flags.isFirstHistory ? "isFirstHistory, " : "",
						h->in.flags.hasNewSync ? "hasNewSync, " : "",
						h->in.flags.hasUserToBG ? "hasUserToBG, " : "");
			}
			EARRAY_FOREACH_END;
		}
	}
	EARRAY_FOREACH_END;

	mmLogRequesterStatesBG(mm, "BEFORE RESTORE", 0);
}

static void mmRepredictSendMsgsBeforeRepredictBG(MovementManager* mm){
	EARRAY_CONST_FOREACH_BEGIN(mm->bg.requesters, i, isize);
	{
		MovementRequester*				mr = mm->bg.requesters[i];
		MovementRequesterMsgPrivateData pd;

		if(mr->bg.flags.destroyed){
			continue;
		}

		mmRequesterMsgInitBG(	&pd,
								NULL,
								mr,
								MR_MSG_BG_BEFORE_REPREDICT);

		pd.msg.in.userStruct.toFG = MR_USERSTRUCT_TOFG(mr, MM_BG_SLOT);

		mmRequesterMsgSend(&pd);
	}
	EARRAY_FOREACH_END;
}

static void mmRepredictRestoreLatestSetPosVersionBG(MovementManager* mm){
	if(!mm->bg.flags.needsSetPosVersion){
		EARRAY_FOREACH_REVERSE_BEGIN(mm->bg.predictedSteps, i);
		{
			MovementPredictedStep* psCheck = mm->bg.predictedSteps[i];
			
			if(!i){
				break;
			}
			
			if(psCheck->in.flags.needsSetPosVersion){
				mm->bg.flagsMutable.needsSetPosVersion = 1;
				mm->bg.setPosVersionMutable = psCheck->in.setPosVersion;
				break;
			}
		}
		EARRAY_FOREACH_END;
	}
}

static void mmSendRepredictForFirstPredictedStepBG(	MovementManager* mm,
													MovementThreadData* td)
{
	MovementThreadDataToBGRepredict*	r = td->toBG.repredict;
	MovementOutputRepredict*			mor;
	MovementOutput						oTemp = {0};
	MovementPredictedStep*				ps = mm->bg.predictedSteps[0];
	const U32							flagCount = eaiUSize(&mm->bg.lastAnim.flags);
	
	mmOutputRepredictCreateBG(mm, td, &mor, ps->o, &oTemp);

	if(!gConf.bNewAnimationSystem){
		mor->flags.noAnimBitUpdate = 1;
	}else{
		if(TRUE_THEN_RESET(mm->bg.flagsMutable.animStancesChanged)){
			mmUpdateStanceBitsBG(mm, td, &oTemp);
		}
	
		if(	mm->bg.lastAnim.pc != r->lastAnim.pc ||
			mm->bg.lastAnim.anim != r->lastAnim.anim ||
			flagCount > eaiUSize(&r->lastAnim.flags) ||
			CompareStructs(mm->bg.lastAnim.flags, r->lastAnim.flags, flagCount))
		{
			// Start a new anim.
	
			mmLastAnimCopyToValues(	&oTemp.dataMutable.anim,
									&mm->bg.lastAnim);

			mmOutputAddAnimValueBG(	mm,
									&oTemp,
									r->lastAnim.anim,
									MAVT_ANIM_TO_START);

			EARRAY_INT_CONST_FOREACH_BEGIN(r->lastAnim.flags, i, isize);
			{
				mmOutputAddAnimValueBG(	mm,
										&oTemp,
										r->lastAnim.flags[i],
										MAVT_FLAG);
			}
			EARRAY_FOREACH_END;

			mmLastAnimCopy(	&mm->bg.lastAnimMutable,
							&r->lastAnim);
		}else{
			// Add missing flags, if any.
		
			EARRAY_INT_CONST_FOREACH_BEGIN_FROM(r->lastAnim.flags, i, isize, flagCount);
			{
				mmOutputAddAnimValueBG(	mm,
										&oTemp,
										r->lastAnim.flags[i],
										MAVT_FLAG);

				eaiPush(&mm->bg.lastAnimMutable.flags, r->lastAnim.flags[i]);
			}
			EARRAY_FOREACH_END;
		}

		mmLastAnimCopy(	&td->toFG.lastAnimMutable,
						&mm->bg.lastAnim);

		mm->bg.nextFrame[MM_FG_SLOT].flagsMutable.sendLastAnimToFG = 1;
	}

	mor->dataMutable = oTemp.data;
	
	MM_CHECK_DYNPOS_DEVONLY(r->pos);

	copyVec3(	r->pos,
				mor->dataMutable.pos);

	copyQuat(	r->rot,
				mor->dataMutable.rot);

	copyVec2(	r->pyFace,
				mor->dataMutable.pyFace);
}

typedef struct MovementRepredictModeBackup {
	U32 cpc;
	U32 spc;
} MovementRepredictModeBackup;

static S32 mmRepredictModeBeginBG(	MovementManager* mm,
									MovementThreadData* td,
									MovementRepredictModeBackup* backupOut)
{
	S32									psCount;
	MovementPredictedStep*				ps;
	MovementThreadDataToBGRepredict*	r = td->toBG.repredict;
	const U32							cpcStart = r->cpc;
	const U32							forcedStepCount = r->forcedStepCount;
	const U32							cpcForcedCount = forcedStepCount * MM_PROCESS_COUNTS_PER_STEP;
	const U32							spcStart = r->spc;
	S32									sendForcedPosMsg = mm->bg.flags.sendForcedSetCountToFG;
	
	#if MM_VERIFY_REPREDICTS
		U32*							stanceBitsBeforeSet = NULL;
	#endif

	PERFINFO_AUTO_START_FUNC();

	mmVerifyAnimOutputBG(mm, NULL);

	// Log some stuff.

	if(MMLOG_IS_ENABLED(mm)){
		mmLogBeforeRepredictBG(mm, td);
	}
	
	// Clear predicted steps.
	
	mmPredictedStepsDestroyUntilBG(mm, td, cpcStart);

	// Clear requester history.

	if(mm->bg.flags.mrHasHistory){
		mmRequestersDestroyHistoryUntilBG(mm, cpcStart + cpcForcedCount);
	}

	// It's possible to have no matching step.
	
	psCount = eaSize(&mm->bg.predictedSteps);
	ps = psCount ? mm->bg.predictedSteps[0] : NULL;

	if(	!ps ||
		ps->pc.client != cpcStart ||
		forcedStepCount >= eaUSize(&mm->bg.predictedSteps))
	{
		ps = NULL;

		mmSendFullViewStatusNotAtRestBG(mm, td);

		mmLog(	mm,
				NULL,
				"[bg.repredict] Not repredicting c%u/s%u, %u steps c(%u - %u), %u forced steps",
				cpcStart,
				spcStart,
				psCount,
				psCount ? mm->bg.predictedSteps[0]->pc.client : 0,
				psCount ? mm->bg.predictedSteps[psCount - 1]->pc.client : 0,
				forcedStepCount);
	}else{
		// Put the system into repredict mode.
		
		mgState.bg.flagsMutable.isRepredicting = 1;

		mgState.bg.log.forcedModule = "repredict";

		backupOut->cpc = mgState.bg.pc.local.cur;
		backupOut->spc = mgState.bg.pc.server.cur;

		if(!td->toFG.predict){
			td->toFG.predict = callocStruct(MovementThreadDataToFGPredict);
		}

		td->toFG.predict->repredict.spc = r->spc;
		td->toFG.predict->repredict.spcPrev = SAFE_MEMBER(mm->bg.repredict, spcPrev);

		mmLog(	mm,
				NULL,
				"[bg.repredict] Repredicting from c%u/s%u c(%u - %u), %u forced steps.",
				cpcStart,
				spcStart,
				psCount ? mm->bg.predictedSteps[0]->pc.client : 0,
				psCount ? mm->bg.predictedSteps[psCount - 1]->pc.client : 0,
				forcedStepCount);

		// Log the available requester histories.

		mmRepredictLogRequesterHistoriesBG(mm);

		// Backup mm inputs that will be restored after the repredict.
		
		mmRepredictBackupLatestInputsBG(mm);
	}
	
	// Clear the data owners.

	mmRepredictClearCurrentDataOwnersBG(mm);

	// Clear override table.
	
	mmOverrideTableClearBG(mm);
	
	// Set each requester to its initial historical state.

	mmRepredictSetRequestersToHistoricalStateBG(mm, cpcStart + cpcForcedCount);
	
	// Restore stances.
	
	mmVerifyAnimOutputBG(mm, NULL);

	if(ps){
		#if MM_VERIFY_REPREDICTS
		{
			eaiCopy(&stanceBitsBeforeSet,
					&mm->bg.stanceBits);
		}
		#endif

		if(gConf.bNewAnimationSystem){
			eaiCopy(&mm->bg.stanceBitsMutable,
					&ps->in.stanceBits);
		
			mm->bg.flagsMutable.animStancesChanged = 1;

			// Restore lastAnim.

			mmLastAnimCopy(	&mm->bg.lastAnimMutable,
							&ps->in.lastAnim);

			mmLastAnimCopy(	&td->toFG.lastAnimMutable,
							&ps->in.lastAnim);

			mm->bg.nextFrame[MM_FG_SLOT].flagsMutable.sendLastAnimToFG = 1;
		}
	}
	
	// Let requesters do stuff before the repredict (set overrides/stances, send a toFG).
	
	mmRepredictSendMsgsBeforeRepredictBG(mm);
	
	if(ps){
		if(MMLOG_IS_ENABLED(mm)){
			// Log the requester states after restoring.

			mmLogRequesterStatesBG(mm, "AFTER RESTORE", 0);

			// Log the difference between the previously predicted pos and the synced pos.

			mmLog(	mm,
					NULL,
					"[bg.repredict] Prediction missed: c%u by %1.2f feet!",
					ps->pc.client,
					distance3(	r->pos,
								ps->out.pos));
		}
		
		// Restore the input state.

		if(!mm->bg.miState){
			mm->bg.miState = callocStruct(MovementInputState);
		}

		*mm->bg.miState = ps->out.miState;

		// Remove resource states from the future.
		
		mmResourcesRemoveLocalStatesBG(mm, td);

		// Store o for the head.

		mmPredictedStepStoreOutputBG(mm, ps);
		
		// Restore the setPosVersion to the latest one.
		
		mmRepredictRestoreLatestSetPosVersionBG(mm);
		
		// Send a repredict for the first predicted step.
		
		mmSendRepredictForFirstPredictedStepBG(mm, td);
		
		mmVerifyAnimOutputBG(mm, ps->o);
	}

	// Copy the server pos and rot into the current state.

	MM_CHECK_DYNPOS_DEVONLY(r->pos);

	copyVec3(	r->pos,
				mm->bg.posMutable);

	copyQuat(	r->rot,
				mm->bg.rotMutable);
				
	copyVec2(	r->pyFace,
				mm->bg.pyFaceMutable);
				
	if(sendForcedPosMsg){
		mmSendMsgsForceChangedPosBG(mm);
	}
	
	if(TRUE_THEN_RESET(mm->bg.flagsMutable.sendForcedRotMsg)){
		mmSendMsgsForceChangedRotBG(mm);
	}

	// Re-init each simBodyInstance.
	
	mmRepredictInitSimBodiesBG(mm);
	
	// Clean up debug stuff.
	
	#if MM_VERIFY_REPREDICTS
	{
		eaiDestroy(&stanceBitsBeforeSet);
	}
	#endif

	PERFINFO_AUTO_STOP();

	return !!ps;
}

S32 mmGridGetCellByGridPosBG(	MovementManagerGrid* grid,
								MovementManagerGridCell** cellOut,
								const IVec3 posGrid,
								S32 create)
{
	MovementManagerGridCell* cell;

	if(!grid){
		return 0;
	}
	
	if(create){
		readLockU32(&grid->lock);
	}else{
		assert(!mgState.bg.flags.gridIsWritable);
	}

	if(!grid->stCells){
		if(!create){
			return 0;
		}

		assert(mgState.bg.flags.gridIsWritable);

		writeLockU32(&grid->lock, 1);
		{
			if(!grid->stCells){
				grid->stCells = stashTableCreate(	100,
													StashDefault,
													StashKeyTypeFixedSize,
													sizeof(cell->posGrid));
			}
		}
		writeUnlockU32(&grid->lock);
	}
	
	if(stashFindPointer(grid->stCells, posGrid, cellOut)){
		assert(*cellOut);
		if(create){
			assert(mgState.bg.flags.gridIsWritable);
			
			writeLockU32(&cellOut[0]->lock, 0);	
			readUnlockU32(&grid->lock);
		}
		return 1;
	}
	else if(!create){
		return 0;
	}
	
	assert(mgState.bg.flags.gridIsWritable);

	writeLockU32(&grid->lock, 1);

	if(stashFindPointer(grid->stCells, posGrid, cellOut)){
		assert(*cellOut);
	}else{
		*cellOut = cell = callocStruct(MovementManagerGridCell);
		cell->grid = grid;
		copyVec3(posGrid, cell->posGrid);

		if(!stashAddPointer(grid->stCells, cell->posGrid, cell, 0)){
			assert(0);
		}
	}

	writeLockU32(&cellOut[0]->lock, 0);
	writeUnlockU32(&grid->lock);
	readUnlockU32(&grid->lock);
	
	return 1;
}

void mmRemoveFromGridBG(MovementManager* mm){
	MovementManagerGridCell* cell = mm->bg.gridEntry.cell;

	assert(mgState.bg.flags.gridIsWritable);

	if(!cell){
		return;
	}
	
	PERFINFO_AUTO_START_FUNC();

	if(verify(cell->managers)){
		writeLockU32(&cell->lock, 0);
		{
			U32					cellIndex = mm->bg.gridEntry.cellIndex;
			U32					count = eaSize(&cell->managers);
			MovementManager*	mmToMove = cell->managers[count - 1];
			S32					didLockGrid = 0;
			
			if(count == 1){
				didLockGrid = 1;

				writeUnlockU32(&cell->lock);
				writeLockU32(&cell->grid->lock, 0);
				writeLockU32(&cell->lock, 0);

				cellIndex = mm->bg.gridEntry.cellIndex;
				count = eaSize(&cell->managers);
				mmToMove = cell->managers[count - 1];
			}

			assert(	cellIndex < count &&
					cell->managers[cellIndex] == mm &&
					mmToMove->bg.gridEntry.cell == cell &&
					cell->managers[mmToMove->bg.gridEntry.cellIndex] == mmToMove);
					
			if(count == 1){
				// Destroy the cell because no one is in it.

				MovementManagerGridCell* cellTest;

				if(!stashRemovePointer(	cell->grid->stCells,
										cell->posGrid,
										&cellTest))
				{
					assert(0);
				}

				assert(cellTest == cell);

				// Don't need to release writeLock on cell->lock, because it's being freed.
				
				writeUnlockU32(&cell->grid->lock);

				eaDestroy(&cell->managers);
				SAFE_FREE(cell);
			}else{
				mmToMove->bg.gridEntryMutable.cellIndex = cellIndex;
				cell->managers[cellIndex] = mmToMove;

				eaSetSize(&cell->managers, count - 1);

				writeUnlockU32(&cell->lock);

				if(didLockGrid){
					// Cell can't be freed before this because grid is locked.

					writeUnlockU32(&cell->grid->lock);
				}
			}

			mm->bg.gridEntryMutable.cell = NULL;
		}
	}
	
	PERFINFO_AUTO_STOP();
}

void mmUpdateGridSizeIndexBG(MovementManager* mm){
	U32 index = ARRAY_SIZE(mmGridSizeGroups) - 1;

	ARRAY_FOREACH_BEGIN(mmGridSizeGroups, i);
	{
		if(mm->bg.bodyRadius <= mmGridSizeGroups[i].maxBodyRadius){
			index = i;
			break;
		}
	}
	ARRAY_FOREACH_END;

	if(mm->bg.gridEntry.gridSizeIndex != index){
		mm->bg.gridEntryMutable.gridSizeIndex = index;
		
		mmRemoveFromGridBG(mm);
	}
}

static void mmUpdateGridEntryBG(MovementManager* mm){
	MovementManagerGridCell*	cell = NULL;
	MovementManagerGridCell*	oldCell = mm->bg.gridEntry.cell;
	IVec3						posGrid;
	MovementManagerGrid*		grid;
	F32							cellSize;

	assert(mgState.bg.flags.gridIsWritable);

	if(	mm->bg.flags.isInactive &&
		!mgState.fg.flags.noDisable)
	{
		mmRemoveFromGridBG(mm);
		return;
	}

	if(	oldCell &&
		sameVec3(	mm->bg.gridEntry.pos,
					mm->bg.past.pos))
	{
		return;
	}
	
	cellSize = mmGridSizeGroups[mm->bg.gridEntry.gridSizeIndex].cellSize;

	if(cellSize){
		FOR_BEGIN(i, 3);
		{
			posGrid[i] = floor(mm->bg.past.pos[i] / cellSize);
		}
		FOR_END;
	}else{
		zeroVec3(posGrid);
	}
	
	if(oldCell){
		if(sameVec3(oldCell->posGrid,
					posGrid))
		{
			// Copy the pos so the first check passes next time.

			copyVec3(	mm->bg.past.pos,
						mm->bg.gridEntryMutable.pos);

			return;
		}

		mmRemoveFromGridBG(mm);
	}

	if(!mm->space){
		return;
	}

	PERFINFO_AUTO_START("mmUpdateGridEntryBG:add", 1);
	
	grid = mm->space->mmGrids + mm->bg.gridEntry.gridSizeIndex;
	
	if(!mmGridGetCellByGridPosBG(grid, &cell, posGrid, 1)){
		assert(0);
	}
	
	// Cell is write-locked.
	
	assert(cell);

	copyVec3(	mm->bg.past.pos,
				mm->bg.gridEntryMutable.pos);

	mm->bg.gridEntryMutable.cell = cell;
	mm->bg.gridEntryMutable.cellIndex = eaSize(&cell->managers);
	eaPush(&cell->managers, mm);
	
	writeUnlockU32(&cell->lock);

	PERFINFO_AUTO_STOP();
}

static void mmSetPastPosRotFaceBG(	MovementManager* mm,
									MovementThreadData* td)
{
	const MovementNetOutput*	no;
	Vec3						posInPastToPlace;
	Quat						rotInPastToPlace;
	Vec2						pyFaceInPastToPlace;
	S32							found = 0;

	for(no = td->toBG.net.outputList.tail;
		no;
		no = (no == td->toBG.net.outputList.head ? NULL : no->prev))
	{
		S32 diffToView = subS32(mgState.bg.pc.server.curView,
								no->pc.server);

		if(	diffToView >= 0 ||
			no == td->toBG.net.outputList.head)
		{
			if(diffToView > MM_PROCESS_COUNTS_PER_SECOND * 5){
				break;
			}
			else if(!diffToView ||
					no == td->toBG.net.outputList.tail)
			{
				found = 1;
				
				copyVec3(	no->data.pos,
							posInPastToPlace);

				copyQuat(	no->data.rot,
							rotInPastToPlace);
				
				copyVec2(	no->data.pyFace,
							pyFaceInPastToPlace);
			}else{
				// Scale between two net outputs.

				const MovementNetOutput*	noNext = no->next;
				S32							noSPCDelta =	subS32(	noNext->pc.server,
																	no->pc.server);

				found = 1;

				if(noSPCDelta){
					F32 ratio = (F32)diffToView /
								(F32)noSPCDelta;

					ratio = CLAMPF32(ratio, 0.f, 1.f);

					interpVec3(	ratio,
								no->data.pos,
								noNext->data.pos,
								posInPastToPlace);

					quatInterp(	ratio,
								no->data.rot,
								noNext->data.rot,
								rotInPastToPlace);
					
					interpVec2(	ratio,
								no->data.pyFace,
								noNext->data.pyFace,
								pyFaceInPastToPlace);
				}else{
					copyVec3(	no->data.pos,
								posInPastToPlace);

					copyQuat(	no->data.rot,
								rotInPastToPlace);
				
					copyVec2(	no->data.pyFace,
								pyFaceInPastToPlace);
				}
			}

			break;
		}
	}

	if(!found){
		copyVec3(	mm->bg.pos,
					posInPastToPlace);
					
		copyQuat(	mm->bg.rot,
					rotInPastToPlace);
		
		copyVec2(	mm->bg.pyFace,
					pyFaceInPastToPlace);
	}

	copyVec3(	posInPastToPlace,
				mm->bg.past.posMutable);

	copyQuat(	rotInPastToPlace,
				mm->bg.past.rotMutable);
	
	copyVec2(	pyFaceInPastToPlace,
				mm->bg.past.pyFaceMutable);

	// Update grid pos.

	mmUpdateGridEntryBG(mm);
}

static void mmUpdateSimBodiesBG(void){
	static StashTable stAdded;

	PERFINFO_AUTO_START_FUNC();
	
	// Send msg to requesters that need to create a sim body.
	
	EARRAY_CONST_FOREACH_BEGIN(mgState.bg.mrsThatNeedSimBodyCreate, i, isize);
	{
		MovementRequester*				mr = mgState.bg.mrsThatNeedSimBodyCreate[i];
		MovementRequesterMsgPrivateData	pd;

		mmRequesterMsgInitBG(	&pd,
								NULL,
								mr,
								MR_MSG_BG_SIM_BODIES_DO_CREATE);

		mmRequesterMsgSend(&pd);
	}
	EARRAY_FOREACH_END;
	
	eaDestroy(&mgState.bg.mrsThatNeedSimBodyCreate);

	// Update the WCOs in the sim body scene.

	if(	mgState.wcScene &&
		eaSize(&mgState.bg.simBodyInstances))
	{
		wcSceneUpdateWorldCollObjectsBegin(mgState.bg.wciMsg, mgState.wcScene);

		if(!stAdded){
			stAdded = stashTableCreateFixedSize(20, SIZEOF2(MovementSimBodyInstance, posGrid));
		}

		EARRAY_CONST_FOREACH_BEGIN(mgState.bg.simBodyInstances, i, isize);
		{
			MovementSimBodyInstance* sbi = mgState.bg.simBodyInstances[i];
			
			if(sbi->flags.destroyed){
				wcActorDestroy(mgState.bg.wciMsg, &sbi->wcActor);

				mmBodyLockEnter();
				{
					if(eaFindAndRemove(&mgState.bg.simBodyInstancesMutable, sbi) < 0){
						assert(0);
					}
				}
				mmBodyLockLeave();

				SAFE_FREE(sbi);
				i--;
				isize--;
			}else{
				Mat4 mat;

				if(wcActorGetMat(mgState.bg.wciMsg, sbi->wcActor, mat)){
					FOR_BEGIN(j, 3);
					{
						sbi->posGrid[j] = mat[3][j] / 20.f;
					}
					FOR_END;
					
					if(stashAddPointer(stAdded, sbi->posGrid, NULL, 0)){
						wcSceneGatherWorldCollObjectsByRadius(	mgState.bg.wciMsg,
																mgState.wcScene,
																SAFE_MEMBER(sbi->mm->space, wc),
																mat[3],
																40.f);
					}
				}
			}
		}
		EARRAY_FOREACH_END;

		stashTableClear(stAdded);

		wcSceneUpdateWorldCollObjectsEnd(mgState.bg.wciMsg, mgState.wcScene);
	}
	
	PERFINFO_AUTO_STOP();
}

#if _PS3
	#define WAIT_FOR_EVENT(h, ms)	WaitForEvent(h, ms);
	#define DESTROY_EVENT(h)		DestroyEvent(h)
#else
	#define WAIT_FOR_EVENT(h, ms)	WaitForSingleObject(h, ms)
	#define DESTROY_EVENT(h)		CloseHandle(h);
#endif

static DWORD WINAPI mmProcessingThreadMain(MovementProcessingThread* t)
{
	EXCEPTION_HANDLER_BEGIN;

	TlsSetValue(mgState.bg.threads.tls.processingThread, t);

	t->threadID = GetCurrentThreadId();
	
	wcSetThreadIsBG();

	while(1){
		U32 msTimeDiff;

		SIMPLE_CPU_DECLARE_TICKS(ticksStart);
		SIMPLE_CPU_DECLARE_TICKS(ticksEnd);

		PERFINFO_AUTO_START("waitForStartEvent", 1);
		{
			WAIT_FOR_EVENT(t->frameStart.hEvent, INFINITE);
			msTimeDiff = timeGetTime() - t->frameStart.msTimeEventSet;
		}
		PERFINFO_AUTO_STOP();

		ADD_MISC_COUNT(	msTimeDiff,
						"milliseconds since start event set");
		
		autoTimerThreadFrameEnd();
		autoTimerThreadFrameBegin(__FUNCTION__);

		SIMPLE_CPU_TICKS(ticksStart);

		if(t->flags.killThread){
			PERFINFO_AUTO_START("killThread", 1);
			{
				t->frameDone.msTimeEventSet = timeGetTime();
				SetEvent(t->frameDone.hEvent);
				
				while(1){
					// Wait for ThreadManager to kill us.

					SleepEx(INFINITE, TRUE);
				}
			}
			// Unreachable stop.
			//PERFINFO_AUTO_STOP();
		}

		PERFINFO_AUTO_START("frame", 1);
		{
			const MovementProcessingThreadCB	cb = t->cb;
			void*const*const					things = t->things;
			const U32							thingCount = t->thingCount;
			
			while(1){
				U32 index = InterlockedIncrement(&mgState.bg.threads.curThingIndexShared);
				
				if(index >= thingCount){
					break;
				}
				
				cb(t, things[index]);
			}
		}
		PERFINFO_AUTO_STOP();
				
		PERFINFO_AUTO_START("SetEvent:done", 1);
		{
			t->frameDone.msTimeEventSet = timeGetTime();
			SetEvent(t->frameDone.hEvent);
		}
		PERFINFO_AUTO_STOP();

		SIMPLE_CPU_TICKS(ticksEnd);
		SIMPLE_CPU_THREAD_CLOCK(SIMPLE_CPU_USAGE_THREAD_MMBG_MAIN, ticksStart, ticksEnd);
	}

	EXCEPTION_HANDLER_END;
}

static void mmDestroyThreadResultsBG(MovementGlobalStateThreadData* mgtd)
{
	while(eaSize(&mgtd->toBG.threadResults)){
		MovementProcessingThreadResults* r = eaPop(&mgtd->toBG.threadResults);

		eaDestroy(&r->managersAfterSimWakesMutable);
		SAFE_FREE(r);
	}
}

static void mmProcessingThreadsCreate(MovementGlobalStateThreadData* mgtd)
{
	S32 numThreads;
	
    // If not overridden on the command line, use 2 movement threads.
	if(!mgState.bg.threads.desiredThreadCount){
        mgState.bg.threads.desiredThreadCount = 2;
	}
	
    // Cap the number of movement threads by the number of real cores.
	numThreads = mgState.bg.threads.desiredThreadCount;
	MINMAX1(numThreads, 1, getNumRealCpus());
	mgState.bg.threads.desiredThreadCount = numThreads;

	while(eaSize(&mgState.bg.threads.threads) < numThreads){
		MovementProcessingThread* t;
		
		t = callocStruct(MovementProcessingThread);
		t->frameStart.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
		t->frameDone.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
		t->mt = tmCreateThread(mmProcessingThreadMain, t);
		
		eaPush(&mgState.bg.threads.threads, t);
	}
	
	while(eaSize(&mgState.bg.threads.threads) > numThreads){
		MovementProcessingThread* t = eaPop(&mgState.bg.threads.threads);

		// Tell thread to die.
		
		ASSERT_FALSE_AND_SET(t->flags.killThread);
		t->frameStart.msTimeEventSet = timeGetTime();
		SetEvent(t->frameStart.hEvent);
		
		// Wait for thread to die.

		WAIT_FOR_EVENT(t->frameDone.hEvent, INFINITE);
		
		// Destroy managed thread.
		
		tmDestroyThread(t->mt, 0);
		
		// Free thread stuff.
		
		DESTROY_EVENT(t->frameStart.hEvent);
		t->frameStart.hEvent = NULL;
		DESTROY_EVENT(t->frameDone.hEvent);
		t->frameDone.hEvent = NULL;

		if(t->results){
			eaPush(&mgtd->toFG.threadResults, t->results);
			t->results = NULL;
			mgtd->toFG.flags.hasThreadResults = 1;
		}
		
		SAFE_FREE(t);
	}

	EARRAY_CONST_FOREACH_BEGIN(mgState.bg.threads.threads, i, isize);
	{
		MovementProcessingThread* t = mgState.bg.threads.threads[i];

		if(	!t->results &&
			eaSize(&mgtd->toBG.threadResults))
		{
			t->results = eaPop(&mgtd->toBG.threadResults);
			assert(!eaSize(&t->results->managersAfterSimWakes));
		}
	}
	EARRAY_FOREACH_END;

	mmDestroyThreadResultsBG(mgtd);
}

static void mmProcessingThreadsProcess(	void*const* things,
										U32 thingCount,
										MovementProcessingThreadCB cb)
{
	MovementGlobalStateThreadData* mgtd = mgState.threadData + MM_BG_SLOT;

	if(	!things ||
		!thingCount ||
		!cb)
	{
		return;
	}

	if(!mgState.bg.threads.tls.processingThread){
		mgState.bg.threads.tls.processingThread = TlsAlloc();
	}
	
	if(	mgState.flags.isServer &&
		!beaconIsBeaconizer())
	{
		U32 msTimeDiffMin = 0;
		U32 msTimeDiffMax = 0;

		mmProcessingThreadsCreate(mgtd);

		mgState.bg.threads.curThingIndexShared = -1;

		EARRAY_CONST_FOREACH_BEGIN(mgState.bg.threads.threads, i, isize);
		{
			MovementProcessingThread* t = mgState.bg.threads.threads[i];
			
			t->things = things;
			t->thingCount = thingCount;
			t->cb = cb;
			
			t->frameStart.msTimeEventSet = timeGetTime();
			SetEvent(t->frameStart.hEvent);
		}
		EARRAY_FOREACH_END;
		
		EARRAY_CONST_FOREACH_BEGIN(mgState.bg.threads.threads, i, isize);
		{
			MovementProcessingThread*	t = mgState.bg.threads.threads[i];
			U32							msTimeDiff;

			WAIT_FOR_EVENT(t->frameDone.hEvent, INFINITE);
			msTimeDiff = timeGetTime() - t->frameDone.msTimeEventSet;

			if(!i){
				msTimeDiffMin = msTimeDiff;
				msTimeDiffMax = msTimeDiff;
			}else{
				MIN1(msTimeDiffMin, msTimeDiff);
				MAX1(msTimeDiffMax, msTimeDiff);
			}
		}
		EARRAY_FOREACH_END;

		ADD_MISC_COUNT(msTimeDiffMin, "min ms since done event set");
		ADD_MISC_COUNT(msTimeDiffMax, "max ms since done event set");
	}else{
		MovementProcessingThread t = {0};

		t.threadID = GetCurrentThreadId();

		TlsSetValue(mgState.bg.threads.tls.processingThread, &t);

		FOR_BEGIN(i, (S32)thingCount);
		{
			cb(&t, things[i]);
		}
		FOR_END;

		if(t.results){
			eaPush(&mgtd->toFG.threadResults, t.results);
			t.results = NULL;
			mgtd->toFG.flags.hasThreadResults = 1;
		}

		TlsSetValue(mgState.bg.threads.tls.processingThread, NULL);

		mmDestroyThreadResultsBG(mgtd);
	}
}

static void mmSetPastStateBG(	MovementProcessingThread* t,
								MovementManager* mm)
{
	MovementThreadData* td = MM_THREADDATA_BG(mm);
	
	if(mm->bg.flags.isAttachedToClient){
		PERFINFO_AUTO_START("mmSetPastStateBG:client", 1);
	}else{
		PERFINFO_AUTO_START("mmSetPastStateBG:non-client", 1);
	}

	assert(!t->mm);
	t->mmMutable = mm;
	
	if(	mm->bg.flags.hasChangedOutputDataRecently ||
		!mgState.flags.isServer)
	{
		if(subS32(	mgState.bg.pc.server.curView,
					mm->bg.setPastState.spcNewestChange) > 0)
		{
			if(TRUE_THEN_RESET(mm->bg.flagsMutable.hasChangedOutputDataRecently)){
				mmPastStateListCountDecBG(mm);
				mmVerifyPastStateCountBG(mm);
			}
		}
		
		mmSetPastPosRotFaceBG(mm, td);
	}
	
	if(mm->bg.flags.mmrNeedsSetState){
		mmResourcesSetStateBG(mm, td);
	}

	assert(t->mm == mm);
	t->mmMutable = NULL;

	PERFINFO_AUTO_STOP();
}

static void mmPastStateEnterCS(void){
	csEnter(&mgState.bg.setPastState.cs);
}

static void mmPastStateLeaveCS(void){
	csLeave(&mgState.bg.setPastState.cs);
}

static void mmPastStateAddToChangedListBG(MovementManager* mm){
	if(FALSE_THEN_SET(mm->bg.flagsMutable.inSetPastStateChangedList)){
		PERFINFO_AUTO_START_FUNC();
		
		mmPastStateEnterCS();
		{
			eaPush(&mgState.bg.setPastState.managersChangedMutable, mm);
		}
		mmPastStateLeaveCS();
		
		PERFINFO_AUTO_STOP();
	}
}

static void mmPastStateListAddBG(MovementManager* mm){
	mm->bg.setPastState.listIndex = eaPush(&mgState.bg.setPastState.managersMutable, mm);
}

#if MM_VERIFY_PAST_STATE_LIST_COUNT
void mmVerifyPastStateCountBG(MovementManager* mm){
	U32 count = 0;
	
	if(mm->bg.flags.hasChangedOutputDataRecently){
		count++;
	}
	
	if(mm->bg.flags.mmrNeedsSetState){
		count++;
	}
	
	assert(count == mm->bg.setPastState.inListCount);
}
#endif

void mmPastStateListCountIncBG(MovementManager* mm){
	if(mm->bg.setPastState.inListCount++){
		return;
	}
	
	if(mgState.bg.setPastState.flags.isNotWritable){
		mmPastStateAddToChangedListBG(mm);
	}else{
		PERFINFO_AUTO_START_FUNC();
		
		ASSERT_FALSE_AND_SET(mm->bg.flagsMutable.inSetPastStateList);

		mmPastStateEnterCS();
		{
			mmPastStateListAddBG(mm);
		}
		mmPastStateLeaveCS();
		
		PERFINFO_AUTO_STOP();
	}
}

static void mmPastStateListRemoveBG(MovementManager* mm){
	U32 index = mm->bg.setPastState.listIndex;
	U32 size = eaSize(&mgState.bg.setPastState.managers);

	assert(index < size);
	assert(mgState.bg.setPastState.managers[index] == mm);
	
	if(index != size - 1){
		MovementManager* mmLast = mgState.bg.setPastState.managers[size - 1];
		
		assert(mmLast->bg.flags.inSetPastStateList);
		assert(mmLast->bg.setPastState.listIndex == size - 1);
		
		mgState.bg.setPastState.managersMutable[index] = mmLast;
		mmLast->bg.setPastState.listIndex = index;
	}
	
	eaSetSize(&mgState.bg.setPastState.managersMutable, size - 1);
	mm->bg.setPastState.listIndex = 0;
}

void mmPastStateListCountDecBG(MovementManager* mm){
	assert(mm->bg.setPastState.inListCount);
	
	if(--mm->bg.setPastState.inListCount){
		return;
	}
	
	if(mgState.bg.setPastState.flags.isNotWritable){
		mmPastStateAddToChangedListBG(mm);
	}else{
		PERFINFO_AUTO_START_FUNC();
		
		ASSERT_TRUE_AND_RESET(mm->bg.flagsMutable.inSetPastStateList);

		mmPastStateEnterCS();
		{
			mmPastStateListRemoveBG(mm);
		}
		mmPastStateLeaveCS();
		
		PERFINFO_AUTO_STOP();
	}
}

static void mmPastStateProcessChangedListBG(void){
	if(!eaSize(&mgState.bg.setPastState.managersChanged)){
		return;
	}
	
	PERFINFO_AUTO_START_FUNC();

	EARRAY_CONST_FOREACH_BEGIN(mgState.bg.setPastState.managersChanged, i, isize);
	{
		MovementManager* mm = mgState.bg.setPastState.managersChanged[i];
		
		ASSERT_TRUE_AND_RESET(mm->bg.flagsMutable.inSetPastStateChangedList);
		
		if(mm->bg.setPastState.inListCount){
			if(FALSE_THEN_SET(mm->bg.flagsMutable.inSetPastStateList)){
				mmPastStateListAddBG(mm);
			}
		}
		else if(TRUE_THEN_RESET(mm->bg.flagsMutable.inSetPastStateList)){
			mmPastStateListRemoveBG(mm);
		}
	}
	EARRAY_FOREACH_END;
	
	eaClearFast(&mgState.bg.setPastState.managersChangedMutable);
	
	PERFINFO_AUTO_STOP();
}

static void mmAllSetCurrentViewBG(void){
	PERFINFO_AUTO_START_FUNC();

	ASSERT_FALSE_AND_SET(mgState.bg.flagsMutable.gridIsWritable);
	ASSERT_FALSE_AND_SET(mgState.bg.setPastState.flags.isNotWritable);

	#if GAMESERVER
	{
		mmProcessingThreadsProcess(	mgState.bg.setPastState.managers,
									eaSize(&mgState.bg.setPastState.managers),
									mmSetPastStateBG);
	}
	#else
	{
		mmProcessingThreadsProcess(	mgState.bg.managers.client,
									eaSize(&mgState.bg.managers.client),
									mmSetPastStateBG);

		mmProcessingThreadsProcess(	mgState.bg.managers.nonClient,
									eaSize(&mgState.bg.managers.nonClient),
									mmSetPastStateBG);
	}
	#endif
								
	ASSERT_TRUE_AND_RESET(mgState.bg.setPastState.flags.isNotWritable);
	ASSERT_TRUE_AND_RESET(mgState.bg.flagsMutable.gridIsWritable);
	
	mmPastStateProcessChangedListBG();

	mmUpdateSimBodiesBG();

	PERFINFO_AUTO_STOP();
}

static void mmRepredictRunStepsBG(	MovementManager* mm,
									MovementThreadData* td)
{
	U32					forcedStepCount = td->toBG.repredict->forcedStepCount;
	MovementInputStep*	miStepHead = NULL;
	MovementInputStep*	miStepTail = NULL;
	
	PERFINFO_AUTO_START_FUNC();

	// Re-run each step.

	EARRAY_CONST_FOREACH_BEGIN_FROM(mm->bg.predictedSteps, i, isize, 1);
	{
		MovementPredictedStep*		ps = mm->bg.predictedSteps[i];
		MovementOutput*				o = ps->o;
		MovementOutput				oTemp = {0};
		MovementOutputRepredict*	mor;
		
		if(forcedStepCount){
			forcedStepCount--;
			
			// Accumulate the input steps for the skipped steps.
			
			if(!miStepHead){
				miStepTail = miStepHead = ps->in.miStep;
			}else{
				miStepTail = miStepTail->bg.next = ps->in.miStep;
			}

			miStepTail->bg.next = NULL;
			
			continue;
		}

		// Create the repredict.
		
		mmOutputRepredictCreateBG(mm, td, &mor, o, &oTemp);
		
		// Verify stuff.

		if(!mmOutputListContains(&mm->bg.outputList, o)){
			assert(0);
		}else{
			MovementThreadData* tdFG = MM_THREADDATA_FG(mm);
			
			if(!mmOutputListContains(&tdFG->toFG.outputList, o)){
				assert(0);
			}
		}

		// Set the current client and server process counts.

		mgState.bg.pc.local.cur =	o->pc.client;
		
		mgState.bg.pc.server.cur =	mgState.bg.pc.local.cur +
									mgState.bg.netReceive.cur.offset.clientToServer;

		mgState.bg.pc.server.curView =	mgState.bg.pc.server.cur -
										MM_BG_PROCESS_COUNT_OFFSET_TO_CUR_VIEW;

		// Run the scene simulation.
		
		FOR_BEGIN(j, MM_PROCESS_COUNTS_PER_STEP);
		{
			wcSceneSimulate(mgState.bg.wciMsg, mgState.wcScene);
		}
		FOR_END;

		// Set the process count.
		
		oTemp.pc.client = mgState.bg.pc.local.cur;
		oTemp.pc.server = mgState.bg.pc.server.cur;

		// Restore sync flag.
		
		mm->bg.flagsMutable.mrHasNewSync = ps->in.flags.mrHasNewSync;
		
		// Restore the setPosVersion.
		
		if(ps->in.flags.needsSetPosVersion){
			mm->bg.flagsMutable.needsSetPosVersion = 1;
			mm->bg.setPosVersionMutable = ps->in.setPosVersion;
		}

		// Set the requester inputs to their historic state.

		mmRepredictSetRequestersInputToProcessCountBG(	mm,
														ps->in.flags.mrHasUserToBG);

		// Place entities in their past positions.

		mmAllSetCurrentViewBG();

		// Run the step.

		if(miStepTail){
			miStepTail = miStepTail->bg.next = ps->in.miStep;
		}
		
		ps->in.miStep->bg.next = NULL;

		mmVerifyAnimOutputBG(mm, ps->o->prev);

		// Store the stance bits.

		eaiCopy(&ps->in.stanceBits,
				&mm->bg.stanceBits);

		mmLastAnimCopy(	&ps->in.lastAnim,
						&mm->bg.lastAnim);

		mmRunSingleStepBG(	mm,
							td,
							&oTemp,
							FIRST_IF_SET(miStepHead, ps->in.miStep),
							ps->in.flags.mrHasUserToBG);
							
		miStepHead = miStepTail = NULL;

		// The client needs to keep info around so it can repredict.
		
		mmPredictedStepStoreOutputBG(mm, ps);

		// Copy stuff from the temp output, send it to FG.

		mor->dataMutable = oTemp.data;
		mor->flags.notInterped = oTemp.flags.notInterped;

		ZeroStruct(&oTemp);

		mmVerifyAnimOutputBG(mm, ps->o);
	}
	EARRAY_FOREACH_END;
	
	PERFINFO_AUTO_STOP();
}

static void mmRepredictModeEndBG(	MovementManager* mm,
									MovementThreadData* td,
									const MovementRepredictModeBackup* backup)
{
	PERFINFO_AUTO_START_FUNC();

	td->toBG.repredict->cpc = 0;

	mmRepredictRestoreLatestInputsBG(mm);

	mmLog(	mm,
			NULL,
			"[bg.repredict] Done repredicting"
			" -----------------------------------------------------------");

	mgState.bg.log.forcedModule = NULL;

	mgState.bg.pc.local.cur = backup->cpc;
	mgState.bg.pc.server.cur = backup->spc;
	mgState.bg.pc.server.curView =	mgState.bg.pc.server.cur -
									MM_BG_PROCESS_COUNT_OFFSET_TO_CUR_VIEW;

	mgState.bg.flagsMutable.isRepredicting = 0;

	mmLogResource(	mm,
					NULL,
					"Resources after reprediction from %u",
					td->toBG.repredict->spc);

	if(!mm->bg.repredict){
		mm->bg.repredict = callocStruct(MovementManagerBGRepredict);
	}

	mm->bg.repredict->spcPrev = td->toBG.repredict->spc;
	
	PERFINFO_AUTO_STOP();
}

static void mmRepredictBG(	MovementManager* mm,
							MovementThreadData* td)
{
	MovementRepredictModeBackup backup;

	if(!TRUE_THEN_RESET(td->toBG.flagsMutable.doRepredict)){
		return;
	}

	PERFINFO_AUTO_START("mmRepredictBG", 1);

	if(mmRepredictModeBeginBG(mm, td, &backup)){
		mmRepredictRunStepsBG(mm, td);
		mmRepredictModeEndBG(mm, td, &backup);
	}

	PERFINFO_AUTO_STOP();
}

static void mmRequesterApplyQueuedSyncs(MovementManager* mm,
										MovementRequester* mr,
										MovementInputStep* miStep,
										U32 spcOldestAllowed,
										S32 applyAllSyncs)
{
	while(eaSize(&mr->bg.queuedSyncs)){
		MovementQueuedSync* qs = mr->bg.queuedSyncs[0];

		if(	!applyAllSyncs &&
			subS32(qs->spc, spcOldestAllowed) > 0)
		{
			mm->bg.flagsMutable.mrHasQueuedSync = 1;
			break;
		}
		
		mrLog(	mr,
				NULL,
				"[bg.mrSync] Applying queued sync: s%u",
				qs->spc);
		
		if(applyAllSyncs){
			PERFINFO_AUTO_START("applyQueuedSync:all", 1);
		}else{
			PERFINFO_AUTO_START("applyQueuedSync", 1);
		}
		{
			eaRemove(&mr->bg.queuedSyncsMutable, 0);

			mmStructCopy(	mr->mrc->pti.sync,
							qs->sync,
							mr->userStruct.sync.bg);

			mmStructDestroy(mr->mrc->pti.sync,
							qs->sync,
							mm);

			if(qs->syncPublic){
				mmStructCopy(	mr->mrc->pti.syncPublic,
								qs->syncPublic,
								mr->userStruct.syncPublic.bg);

				mmStructDestroy(mr->mrc->pti.syncPublic,
								qs->syncPublic,
								mm);
			}
			
			if(MMLOG_IS_ENABLED(mm)){
				mmLogRequesterSyncBG(mr);
			}

			SAFE_FREE(qs);
			
			mm->bg.flagsMutable.mrHasNewSync = 1;
			mr->bg.flagsMutable.hasNewSync = 1;
		}
		PERFINFO_AUTO_STOP();
	}
}

static void mmApplyQueuedSyncsBG(	MovementManager* mm,
									MovementInputStep* miStep,
									U32 spcOldestAllowed,
									S32 applyAllSyncs)
{
	mm->bg.flagsMutable.mrHasQueuedSync = 0;
	
	EARRAY_CONST_FOREACH_BEGIN(mm->bg.requesters, i, size);
	{
		MovementRequester* mr = mm->bg.requesters[i];

		mmRequesterApplyQueuedSyncs(mm,
									mr,
									miStep,
									spcOldestAllowed,
									applyAllSyncs);
	}
	EARRAY_FOREACH_END;
}

#if !VERIFY_OUTPUT_LISTS
	#define mmVerifyOutputListsBG(mm)
#else
void mmVerifyOutputListsBG(MovementManager* mm){
	const MovementOutput* o;
	
	if(!mm){
		return;
	}

	assert(!SAFE_MEMBER(mm->bg.available.outputList.head, prev));
	assert(!SAFE_MEMBER(mm->bg.available.outputList.tail, next));

	for(o = mm->bg.available.outputList.head; o; o = o->next){
		if(o->next){
			assert(o->next->prev == o);
		}else{
			assert(o == mm->bg.available.outputList.tail);
		}
		
		if(o->prev){
			assert(o->prev->next == o);
		}else{
			assert(o == mm->bg.available.outputList.head);
		}
	}

	assert(!SAFE_MEMBER(mm->bg.outputList.head, prev));
	assert(!SAFE_MEMBER(mm->bg.outputList.tail, next));
	
	{
		U32 foundCount =	!mm->threadData[0].toFG.outputList.head +
							!mm->threadData[0].toFG.outputList.tail +
							!mm->threadData[1].toFG.outputList.head +
							!mm->threadData[1].toFG.outputList.tail;
		
		for(o = mm->bg.outputList.head; o; o = o->next){
			if(o->next){
				assert(o->next->prev == o);
			}else{
				assert(o == mm->bg.outputList.tail);
			}
			
			if(o->prev){
				assert(o->prev->next == o);
			}else{
				assert(o == mm->bg.outputList.head);
			}
			
			ARRAY_FOREACH_BEGIN(mm->threadData, i);
			{
				foundCount +=	(o == mm->threadData[i].toFG.outputList.head) +
								(o == mm->threadData[i].toFG.outputList.tail);
			}
			ARRAY_FOREACH_END;
		}

		assert(foundCount == 4);
	}
}
#endif

#if !VERIFY_PREDICTED_STEP_OUTPUTS
	#define mmVerifyPredictedStepOutputsBG(mm)
#else
static void mmVerifyPredictedStepOutputsBG(MovementManager* mm){
	MovementThreadData*			td = MM_THREADDATA_BG(mm);
	const MovementThreadData*	tdFG = MM_THREADDATA_FG(mm);

	EARRAY_CONST_FOREACH_BEGIN(mm->bg.predictedSteps, i, isize);
	{
		const MovementPredictedStep* ps = mm->bg.predictedSteps[i];

		assert(mmOutputListContains(&mm->bg.outputList, ps->o));
	}
	EARRAY_FOREACH_END;

	{
		MovementOutputList olBackup = td->toFG.outputList;

		if(!td->toFG.outputList.head){
			mmOutputListSetTail(&td->toFG.outputListMutable,
								mm->bg.outputList.head);
		}

		mmOutputListSetTail(&td->toFG.outputListMutable,
							mm->bg.outputList.tail);

		EARRAY_CONST_FOREACH_BEGIN(td->toFG.repredicts, i, isize);
		{
			const MovementOutputRepredict* mor = td->toFG.repredicts[i];

			assert(mmOutputListContains(&tdFG->toFG.outputList, mor->o));
			assert(mmOutputListContains(&td->toFG.outputList, mor->o));
		}
		EARRAY_FOREACH_END;
		
		td->toFG.outputListMutable = olBackup;
	}
}
#endif

static S32 mmShouldRunLatestStepBG(	MovementManager* mm,
									MovementThreadData* td,
									MovementInputStep** miStepOut)
{
	if(mm->bg.flags.isAttachedToClient){
		// Check for a MovementInputStep.
		
		const U32 stepCount = eaSize(&td->toBG.miSteps);

		if(stepCount){
			const MovementInputStep* miStep = *miStepOut = td->toBG.miSteps[0];

			if(	mgState.flags.isServer &&
				miStep->pc.server != mgState.bg.pc.local.cur)
			{
				// Wait until time catches up to me.

				assert(subS32(	miStep->pc.server,
								mgState.bg.pc.local.cur) > 0);
								
				return 0;
			}

			eaRemove(&td->toBG.miStepsMutable, 0);
		}
		else if(mgState.flags.isServer ||
				mgState.flags.noLocalProcessing)
		{
			return 0;
		}
	}
	else if(mgState.bg.flags.isCatchingUp ||
			mgState.flags.noLocalProcessing)
	{
		return 0;
	}
	
	return 1;
}

static void mmGetNewOutputBG(	MovementManager* mm,
								MovementOutput** oOut)
{
	// Create an output, or get one from the available list.
	
	if(mm->bg.available.outputList.head){
		MovementOutput* o = mm->bg.available.outputList.head;
		
		assert(!o->prev);
		
		mmVerifyOutputListsBG(mm);

		if(o->next){
			o->next->prevMutable = NULL;
		}else{
			assert(mm->bg.available.outputList.tail == o);
			mm->bg.available.outputListMutable.tail = NULL;
		}
		
		mm->bg.available.outputListMutable.head = o->next;
		o->nextMutable = NULL;
		
		ZeroStruct(&o->pc);
		zeroVec3(o->dataMutable.pos);
		zeroQuat(o->dataMutable.rot);
		zeroVec2(o->dataMutable.pyFace);
		
		if(eaiSize(&o->dataMutable.anim.values)){
			eaiClearFast(&o->dataMutable.anim.values);
		}
		ZeroStruct(&o->flagsMutable);
		
		*oOut = o;

		mmVerifyOutputListsBG(mm);
	}else{
		mmOutputCreate(oOut);
	}
}

static void mmAckForcedSetCountBG(	MovementManager* mm,
									MovementThreadData* td,
									MovementOutput* o)
{
	mmLog(	mm,
			NULL,
			"[bg.repredict] Setting latest step to not be interped.");

	o->flagsMutable.notInterped = 1;

	MM_TD_SET_HAS_TOFG(mm, td);
	td->toFG.flagsMutable.hasForcedSetCount = 1;
	td->toFG.forcedSetCount = mm->bg.forcedSetCount;
	
	mmSendMsgsForceChangedPosBG(mm);
	mmSendMsgsForceChangedRotBG(mm);
}

static void mmRunLatestSingleStepBG(MovementManager* mm,
									MovementThreadData* td)
{
	MovementInputStep*	miStep = NULL;
	MovementOutput*		o;
	S32					mrHasUserToBG;

	if(!mmShouldRunLatestStepBG(mm, td, &miStep)){
		return;
	}
	
	PERFINFO_AUTO_START_FUNC();

	mmGetNewOutputBG(mm, &o);
	
	mmOutputListSetTail(&td->toFG.outputListMutable, o);

	if(TRUE_THEN_RESET(mm->bg.flagsMutable.sendForcedSetCountToFG)){
		mmAckForcedSetCountBG(mm, td, o);
	}

	mrHasUserToBG = TRUE_THEN_RESET(td->toBG.flagsMutable.mrHasUserToBG);

	o->pc.server =	mgState.bg.pc.local.cur +
					mgState.bg.netReceive.cur.offset.clientToServer;

	if(miStep){
		o->pc.client = miStep->pc.client;

		if(miStep->pc.serverSync){
			mmLog(	mm,
					NULL,
					"[bg.queuedSync] Input step has server sync: s%u",
					miStep->pc.serverSync);
					
			mmApplyQueuedSyncsBG(	mm,
									miStep,
									miStep->pc.serverSync,
									0);
		}

		if(	!mgState.flags.isServer &&
			mm->bg.flags.isAttachedToClient)
		{
			mmPredictedStepStoreInputBG(mm, o, miStep, mrHasUserToBG);
		}
	}
	else if(!mgState.flags.isServer){
		o->pc.client = mgState.bg.pc.local.cur;
	}

	mmRunSingleStepBG(	mm,
						td,
						o,
						miStep,
						mrHasUserToBG);
	
	if(miStep){
		if(	!mgState.flags.isServer &&
			mm->bg.flags.isAttachedToClient)
		{
			MovementPredictedStep* ps = eaTail(&mm->bg.predictedSteps);
			
			mmPredictedStepStoreOutputBG(mm, ps);
		}else{
			mmSendInputStepBackToFG(mm, td, miStep);
		}
	}

	mmOutputListAddTail(&mm->bg.outputListMutable,
						o);

	mmVerifyAnimOutputBG(mm, o);

	mmVerifyOutputListsBG(mm);
	
	PERFINFO_AUTO_STOP();
}

static void mmLogNewRequesterBG(MovementManager* mm,
								MovementRequester* mr)
{
	char buffer[1000];

	mmRequesterGetDebugStringBG(mr, SAFESTR(buffer), NULL, NULL, NULL, NULL);

	mmLog(	mm,
			NULL,
			"[bg.fromFG] New requester from FG: %s",
			buffer);
}

static void mmHandleNewRequestersFromFG(MovementManager* mm,
										MovementThreadData* td)
{
	PERFINFO_AUTO_START_FUNC();
	
	mmLog(	mm,
			NULL,
			"[bg.fromFG] Getting %u new requesters from FG.",
			eaSize(&td->toBG.newRequesters));

	eaPushEArray(	&mm->bg.requestersMutable,
					&td->toBG.newRequesters);

	EARRAY_CONST_FOREACH_BEGIN(td->toBG.newRequesters, i, size);
	{
		MovementRequester*				mr = td->toBG.newRequesters[i];
		MovementRequesterThreadData*	mrtd = MR_THREADDATA_BG(mr);
		
		if(MMLOG_IS_ENABLED(mm)){
			mmLogNewRequesterBG(mm, mr);
		}

		ASSERT_FALSE_AND_SET(mr->bg.flagsMutable.inList);
		
		mmSendMsgInitializeBG(mm, mr);

		if(mrtd->toBG.flags.createdFromServer){
			mr->bg.flagsMutable.wroteHistory = 1;
		}

		mmRequesterBGPredictCreate(mm, mr);
	}
	EARRAY_FOREACH_END;

	eaDestroy(&td->toBG.newRequestersMutable);
	
	PERFINFO_AUTO_STOP();
}

static void mmSetHasChangedOutputDataRecentlyBG(MovementManager* mm){
	mm->bg.setPastState.spcNewestChange = mgState.bg.pc.server.cur;

	if(FALSE_THEN_SET(mm->bg.flagsMutable.hasChangedOutputDataRecently)){
		mmPastStateListCountIncBG(mm);
	}
}

static void mmHandleNewPositionOrRotationFromFG(MovementManager* mm,
												MovementThreadData* td)
{
	if(mgState.flags.noLocalProcessing){
		return;
	}
	
	PERFINFO_AUTO_START_FUNC();
	
	mm->bg.flagsMutable.sendViewStatusChanged = 1;

	mmLog(	mm,
			NULL,
			"[bg.fromFG] Getting new pos or rot from FG: %s%s.",
			td->toBG.flags.useNewPos ? "POS, " : "",
			td->toBG.flags.useNewRot ? "ROT, " : "");

	// Check if a new position was set.

	if(TRUE_THEN_RESET(td->toBG.flagsMutable.useNewPos)){
		mmLog(	mm,
				NULL,
				"[bg.fromFG] Got new position from FG:"
				" (%1.2f, %1.2f, %1.2f)",
				vecParamsXYZ(td->toBG.newPos));

		MM_CHECK_DYNPOS_DEVONLY(td->toBG.newPos);

		copyVec3(	td->toBG.newPos,
					mm->bg.posMutable);
		
		mmSetHasChangedOutputDataRecentlyBG(mm);
		
		mmSendMsgsForceChangedPosBG(mm);
	}

	// Check if a new rotation was set.

	if(TRUE_THEN_RESET(td->toBG.flagsMutable.useNewRot)){
		Vec3 z;
		
		mmLog(	mm,
				NULL,
				"[bg.fromFG] Got new rotation from FG:"
				" (%1.2f, %1.2f, %1.2f, %1.2f)",
				quatParamsXYZW(td->toBG.newRot));

		copyQuat(	td->toBG.newRot,
					mm->bg.rotMutable);
		
		quatToMat3_2(	mm->bg.rot,
						z);
						
		getVec3YP(	z,
					&mm->bg.pyFaceMutable[1],
					&mm->bg.pyFaceMutable[0]);

		mmSetHasChangedOutputDataRecentlyBG(mm);
		
		mmSendMsgsForceChangedRotBG(mm);
	}
	
	PERFINFO_AUTO_STOP();
}

static void mmCopyPredictStateToFG(	MovementManager* mm,
									MovementThreadData* td)
{
	PERFINFO_AUTO_START_FUNC();

	EARRAY_CONST_FOREACH_BEGIN(mm->bg.requesters, i, size);
	{
		MovementRequester*				mr = mm->bg.requesters[i];
		MovementRequesterThreadData*	mrtd;

		if(!mr->mrc->flags.syncToClient){
			continue;
		}

		mrtd = MR_THREADDATA_BG(mr);

		#if MM_VERIFY_TOFG_PREDICT_STATE
		{
			mrLog(	mr,
					NULL,
					"[bg.toFG] Copying predict state toFG %u %u.",
					mr->bg.flags.bgUnchangedSinceCopyToFG,
					mr->bg.flags.copyToFGNextFrame);
		}
		#endif

		if(FALSE_THEN_SET(mr->bg.flagsMutable.bgUnchangedSinceCopyToFG)){
			mr->bg.flagsMutable.copyToFGNextFrame = 1;
		}
		else if(!TRUE_THEN_RESET(mr->bg.flagsMutable.copyToFGNextFrame)){
			ADD_MISC_COUNT(1, "bgUnchangedSinceCopyToFG");

			if(MMLOG_IS_ENABLED(mm)){
				mmLogRequesterStateBG(	mm,
										mr,
										"bg.toFG",
										"Skipping sending BG state to FG",
										NULL,
										NULL,
										NULL,
										NULL);

				mmLogRequesterStateBG(	mm,
										mr,
										"bg.toFG",
										"Current toFG BG state",
										SAFE_MEMBER_ADDR(mrtd->toFG.predict, ownedDataClassBits),
										SAFE_MEMBER_ADDR(mrtd->toFG.predict, handledMsgs),
										SAFE_MEMBER(mrtd->toFG.predict, userStruct.bg),
										NULL);
			}

			#if MM_VERIFY_TOFG_PREDICT_STATE
			PERFINFO_AUTO_START("debug verify toFG predict state", 1);
			{
				MovementRequesterThreadDataToFGPredict* predict = mrtd->toFG.predict;

				#if 0
					assert(predict);
					assert(predict->ownedDataClassBits == mr->bg.ownedDataClassBits);
					assert(predict->handledMsgs == mr->bg.handledMsgs);
					assert(!StructCompare(	mr->mrc->pti.bg,
											predict->userStruct.bg,
											mr->userStruct.bg,
											0, 0, 0));
				#else
					if(	!predict ||
						predict->ownedDataClassBits != mr->bg.ownedDataClassBits ||
						predict->handledMsgs != mr->bg.handledMsgs ||
						StructCompare(	mr->mrc->pti.bg,
										predict->userStruct.bg,
										mr->userStruct.bg,
										0, 0, 0))
					{
						mrLog(	mr,
								NULL,
								"[bg.toFG] ERROR: Invalid toFG state.");
					}
				#endif
			}
			PERFINFO_AUTO_STOP();
			#endif

			continue;
		}

		MR_PERFINFO_AUTO_START_GUARD(mr, MRC_PT_COPY_STATE_TOFG);
		{
			MovementRequesterThreadDataToFGPredict* predict = mrtd->toFG.predict;

			if(!predict){
				predict = mrtd->toFG.predict = callocStruct(MovementRequesterThreadDataToFGPredict);
			}

			#if MM_VERIFY_TOFG_PREDICT_STATE
			{
				predict->debug.frameWhenUpdated = mgState.frameCount;
			}
			#endif

			// Get the data class mask.

			predict->ownedDataClassBits = mr->bg.ownedDataClassBits;

			// Get the handled msgs mask.

			predict->handledMsgs = mr->bg.handledMsgs;

			// Get the current BG state.
			
			mmStructAllocAndCopy(	mr->mrc->pti.bg,
									predict->userStruct.bg,
									mr->userStruct.bg,
									mm);

			// Log what's being sent to the FG.

			if(MMLOG_IS_ENABLED(mm)){
				mmLogRequesterStateBG(	mm,
										mr,
										"bg.toFG",
										"Sending BG state to FG",
										NULL,
										NULL,
										NULL,
										NULL);
			}
		}
		MR_PERFINFO_AUTO_STOP_GUARD(mr, MRC_PT_COPY_STATE_TOFG);

		#if MM_VERIFY_TOFG_PREDICT_STATE
		{
			mrLog(	mr,
					NULL,
					"Copied predict state toFG %u %u.",
					mr->bg.flags.bgUnchangedSinceCopyToFG,
					mr->bg.flags.copyToFGNextFrame);
		}
		#endif
	}
	EARRAY_FOREACH_END;
	
	PERFINFO_AUTO_STOP();// FUNC
}

static void mmHandleRequesterServerStateFromFG(	MovementManager* mm,
												MovementThreadData* td,
												MovementRequester* mr,
												MovementRequesterThreadData* mrtd)
{
	MovementRequesterHistory*	h = NULL;
	S32							found = 0;
	const U32					cpcRepredict = SAFE_MEMBER(td->toBG.repredict, cpc);
	MovementRequesterBGPredict*	predict = mr->bg.predict;

	PERFINFO_AUTO_START_FUNC();

	if(MMLOG_IS_ENABLED(mm)){
		mmLogRequesterStateBG(	mm,
								mr,
								"bg.toBG",
								"New state from server",
								&mrtd->toBG.predict->ownedDataClassBits,
								&mrtd->toBG.predict->handledMsgs,
								mrtd->toBG.predict->userStruct.serverBG,
								NULL);
	}

	// Find the current history that matches the process count.

	EARRAY_CONST_FOREACH_BEGIN(predict->history, i, isize);
	{
		h = predict->history[i];

		if(h->cpc == cpcRepredict){
			mmStructDestroy(mr->mrc->pti.bg,
							h->out.bg,
							mm);

			found = 1;

			if(MMLOG_IS_ENABLED(mm)){
				S32 size = eaSize(&predict->history);
				
				mrLog(	mr,
						NULL,
						"[bg.toBG] History found (%u-%u), updating %u.",
						size ? predict->history[0]->cpc : 0,
						size ? predict->history[size - 1]->cpc : 0,
						cpcRepredict);
			}
			
			break;
		}
	}
	EARRAY_FOREACH_END;

	if(!found){
		// There was no existing history, so make a new one.
		
		if(MMLOG_IS_ENABLED(mm)){
			S32 size = eaSize(&predict->history);
			
			mrLog(	mr,
					NULL,
					"[bg.toBG] No history found (%u-%u), adding %u to head.",
					size ? predict->history[0]->cpc : 0,
					size ? predict->history[size - 1]->cpc : 0,
					cpcRepredict);
		}

		mmRequesterHistoryCreateBG(&h);

		h->cpc = SAFE_MEMBER(td->toBG.repredict, cpc);

		eaInsert(&predict->historyMutable, h, 0);
	}

	// Copy the new bg struct.

	h->out.bg = mrtd->toBG.predict->userStruct.serverBG;
	mrtd->toBG.predict->userStruct.serverBG = NULL;

	h->out.ownedDataClassBits = mrtd->toBG.predict->ownedDataClassBits;
	mrtd->toBG.predict->ownedDataClassBits = 0;
	
	h->out.handledMsgs = mrtd->toBG.predict->handledMsgs;
	mrtd->toBG.predict->handledMsgs = 0;
	
	PERFINFO_AUTO_STOP();
}

static void mmRequesterLogQueuedSync(	MovementRequester* mr,
										MovementQueuedSync* qs)
{
	char buffer[2000];

	buffer[0] = 0;

	mmRequesterGetSyncDebugString(mr, SAFESTR(buffer), qs->sync, qs->syncPublic);

	mrLog(	mr,
			NULL,
			"[bg.mrSync] Queueing sync: s%u\n%s",
			qs->spc,
			buffer);
}

static void mmHandleRequesterSyncFromFG(MovementManager* mm,
										MovementThreadData* td,
										MovementRequester* mr,
										MovementRequesterThreadData* mrtd,
										S32 applyAllSyncs)
{
	PERFINFO_AUTO_START_FUNC();
	
	if(	mm->bg.flags.isAttachedToClient &&
		!applyAllSyncs)
	{
		// Server queues syncs for client-attached mms.
	
		MovementQueuedSync* qs = callocStruct(MovementQueuedSync);

		qs->spc = mgState.bg.pc.local.sync;
		
		mmStructAllocAndCopy(	mr->mrc->pti.sync,
								qs->sync,
								mrtd->toBG.userStruct.sync,
								mm);

		if(mrtd->toBG.userStruct.syncPublic){
			mmStructAllocAndCopy(	mr->mrc->pti.syncPublic,
									qs->syncPublic,
									mrtd->toBG.userStruct.syncPublic,
									mm);
		}

		eaPush(	&mr->bg.queuedSyncsMutable,
				qs);

		mmRequesterLogQueuedSync(mr, qs);
		
		mm->bg.flagsMutable.mrHasQueuedSync = 1;
	}else{
		if(applyAllSyncs){
			mrLog(	mr,
					NULL,
					"[bg.mrSync] Applying non-client sync.");
		}

		if(mm->bg.flags.mrHasQueuedSync){
			// Was a client entity, so flush the syncs.
			
			mmApplyQueuedSyncsBG(mm, NULL, 0, 1);
		}
		
		// Apply syncs immediately.
		
		if(mrtd->toBG.userStruct.sync){
			mr->bg.flagsMutable.hasNewSync = 1;
			
			mmStructCopy(	mr->mrc->pti.sync,
							mrtd->toBG.userStruct.sync,
							mr->userStruct.sync.bg);
		}
		
		if(mrtd->toBG.userStruct.syncPublic){
			mr->bg.flagsMutable.hasNewSync = 1;
			
			mmStructCopy(	mr->mrc->pti.syncPublic,
							mrtd->toBG.userStruct.syncPublic,
							mr->userStruct.syncPublic.bg);
		}
		
		mm->bg.flagsMutable.mrHasNewSync = 1;

		if(MMLOG_IS_ENABLED(mm)){
			mmLogRequesterSyncBG(mr);
		}
	}
	
	PERFINFO_AUTO_STOP();
}

static S32 mmSendMsgsOverrideValueShouldRejectBG(	MovementManager* mm,
													MovementRequester* mrToIgnore,
													const char* name,
													MovementSharedDataType valueType,
													const MovementSharedDataValue* value,
													MovementRequester** mrRejecterOut)
{
	if(!mm->bg.flags.mrHandlesMsgRejectOverride){
		return 0;
	}

	MEL_FOREACH_BEGIN(iter, mm->bg.mel[MM_BG_EL_REJECT_OVERRIDE]);
	{
		MovementRequester* mr = MR_FROM_EN_BG(MR_BG_EN_REJECT_OVERRIDE, iter);

		assert(mr->bg.handledMsgs & MR_HANDLED_MSG_OVERRIDE_VALUE_SHOULD_REJECT);
		
		if(	mr != mrToIgnore &&
			!mr->bg.flags.destroyed)
		{
			MovementRequesterMsgPrivateData pd;
			MovementRequesterMsgOut			out;

			mmRequesterMsgInitBG(	&pd,
									&out,
									mr,
									MR_MSG_BG_OVERRIDE_VALUE_SHOULD_REJECT);

			pd.msg.in.bg.overrideValueShouldReject.name = name;
			pd.msg.in.bg.overrideValueShouldReject.valueType = valueType;
			pd.msg.in.bg.overrideValueShouldReject.value = *value;
			
			mmRequesterMsgSend(&pd);
			
			if(out.bg.overrideValueShouldReject.shouldReject){
				MEL_ITER_DEINIT(iter, mm->bg.mel[MM_BG_EL_REJECT_OVERRIDE]);
				if(mrRejecterOut){
					*mrRejecterOut = mr;
				}
				return 1;
			}
		}
	}
	MEL_FOREACH_END;
	
	return 0;
}

static void mmSendMsgsOverrideValueSetBG(	MovementManager* mm,
											const MovementOverrideValue* mov)
{
	EARRAY_CONST_FOREACH_BEGIN(mm->bg.requesters, i, isize);
	{
		MovementRequester* mr = mm->bg.requesters[i];
		
		if(!mr->bg.flags.destroyed){
			MovementRequesterMsgPrivateData pd;

			mmRequesterMsgInitBG(	&pd,
									NULL,
									mr,
									MR_MSG_BG_OVERRIDE_VALUE_SET);

			pd.msg.in.bg.overrideValueSet.name = mov->movg->namePooled;
			pd.msg.in.bg.overrideValueSet.valueType = mov->valueType;
			pd.msg.in.bg.overrideValueSet.value = mov->value;
			
			pd.msg.in.userStruct.toFG = MR_USERSTRUCT_TOFG(mr, MM_BG_SLOT);

			mmRequesterMsgSend(&pd);
		}
	}
	EARRAY_FOREACH_END;
}

static void mrSendMsgsOverrideValueDestroyedBG(	MovementRequester* mr,
												U32 handle)
{
	MovementRequesterMsgPrivateData pd;

	mmRequesterMsgInitBG(	&pd,
							NULL,
							mr,
							MR_MSG_BG_OVERRIDE_VALUE_DESTROYED);

	pd.msg.in.bg.overrideValueDestroyed.handle = handle;
	
	pd.msg.in.userStruct.toFG = MR_USERSTRUCT_TOFG(mr, MM_BG_SLOT);

	mmRequesterMsgSend(&pd);
}

static void mmSendMsgsOverrideValueUnsetBG(	MovementManager* mm,
											const char* name)
{
	EARRAY_CONST_FOREACH_BEGIN(mm->bg.requesters, i, isize);
	{
		MovementRequester* mr = mm->bg.requesters[i];

		if(!mr->bg.flags.destroyed){
			MovementRequesterMsgPrivateData pd;

			mmRequesterMsgInitBG(	&pd,
									NULL,
									mr,
									MR_MSG_BG_OVERRIDE_VALUE_UNSET);

			pd.msg.in.bg.overrideValueUnset.name = name;
			
			pd.msg.in.userStruct.toFG = MR_USERSTRUCT_TOFG(mr, MM_BG_SLOT);

			mmRequesterMsgSend(&pd);
		}
	}
	EARRAY_FOREACH_END;
}

static void mmRequesterDestroyOverrideValuesBG(	MovementManager* mm,
												MovementRequester* mr)
{
	if(mm->bg.stOverrideValues){
		StashTableIterator	iter;
		StashElement		elem;
		
		stashGetIterator(mm->bg.stOverrideValues, &iter);
		
		while(stashGetNextElement(&iter, &elem)){
			MovementOverrideValueGroup* movg = stashElementGetPointer(elem);
			
			ANALYSIS_ASSUME(movg->movs);
			
			EARRAY_CONST_FOREACH_BEGIN(movg->movs, i, isize);
			{
				MovementOverrideValue* mov = movg->movs[i];
				
				if(mov->mr == mr){
					eaRemove(&movg->movsMutable, i);
					i--;
					isize--;

					SAFE_FREE(mov);

					if(	!isize &&
						!eaSize(&movg->movs))
					{
						stashRemovePointer(	mm->bg.stOverrideValues,
											movg->namePooled,
											NULL);

						mmSendMsgsOverrideValueUnsetBG(	mm,
														movg->namePooled);

						eaDestroy(&movg->movsMutable);
						SAFE_FREE(movg);
					}else{
						mmSendMsgsOverrideValueSetBG(	mm,
														movg->movs[eaSize(&movg->movs) - 1]);
					}
				}
			}
			EARRAY_FOREACH_END;
		}
	}
}

static void mmRequesterReleaseOwnedDataClassBitsBG(	MovementManager* mm,
													MovementRequester* mr)
{
	if(mr->bg.ownedDataClassBits){
		ARRAY_FOREACH_BEGIN(mm->bg.dataOwner, i);
		{
			const U32 bit = BIT(i);
			
			if(mm->bg.dataOwner[i] == mr){
				mr->bg.flagsMutable.bgUnchangedSinceCopyToFG = 0;
				ASSERT_TRUE_AND_RESET_BITS(mr->bg.ownedDataClassBitsMutable, bit);
				mm->bg.dataOwnerMutable[i] = NULL;
				
				if(	i == MDC_ANIMATION &&
					gConf.bNewAnimationSystem)
				{
					mm->bg.flagsMutable.animOwnershipWasReleased = 1;
				}

				if(!mr->bg.ownedDataClassBits){
					assert(!(mr->bg.handledMsgs & MR_HANDLED_MSG_BEFORE_DISCUSSION));
				}
			}else{
				assert(!(mr->bg.ownedDataClassBits & bit));
			}
		}
		ARRAY_FOREACH_END;
		
		assert(!mr->bg.ownedDataClassBits);
	}
}

static void mmRequesterDestroySimBodyInstancesBG(	MovementManager* mm,
													MovementRequester* mr)
{
	mmRareLockEnter(mm);
	{
		EARRAY_CONST_FOREACH_BEGIN(mm->bg.simBodyInstances, i, isize);
		{
			MovementSimBodyInstance* sbi = mm->bg.simBodyInstances[i];
			
			if(!sbi){
				continue;
			}

			assert(sbi->mm == mm);
			
			if(sbi->mr == mr){
				mm->bg.simBodyInstancesMutable[i] = NULL;

				mmReclaimSimBodyInstanceHandlesBG(mm);
				
				isize = eaSize(&mm->bg.simBodyInstances);

				sbi->mm = NULL;
				sbi->mr = NULL;
				
				mmSimBodyInstanceDestroyBG(sbi);
			}
		}
		EARRAY_FOREACH_END;
	}
	mmRareLockLeave(mm);
	
	mmBodyLockEnter();
	{
		eaFindAndRemove(&mgState.bg.mrsThatNeedSimBodyCreate, mr);
	}
	mmBodyLockLeave();
}

static void mmNeedsPostStepBG(MovementManager* mm){
	if(!FALSE_THEN_SET(mm->bg.flagsMutable.inPostStepList)){
		return;
	}
	
	csEnter(&mgState.bg.needsPostStep.cs);
	{
		mmExecListAddHead(	&mgState.bg.needsPostStep.melManagers,
							&mm->bg.execNodePostStep);
	}
	csLeave(&mgState.bg.needsPostStep.cs);
}

static void mrNeedsPostStepMsgBG(MovementRequester* mr){
	MovementManager* mm;

	if(!FALSE_THEN_SET(mr->bg.flagsMutable.needsPostStepMsg)){
		MM_EXTRA_ASSERT(mr->mm->bg.flags.mrNeedsPostStepMsg);
		MM_EXTRA_ASSERT(mr->mm->bg.flags.inPostStepList);
		return;
	}
	
	mm = mr->mm;

	if(!FALSE_THEN_SET(mm->bg.flagsMutable.mrNeedsPostStepMsg)){
		MM_EXTRA_ASSERT(mm->bg.flags.inPostStepList);
		return;
	}
	
	mmNeedsPostStepBG(mm);
}

static void mmPipeNeedsPostStepBG(MovementManager* mm){
	if(!FALSE_THEN_SET(mm->bg.flagsMutable.mrPipeNeedsPostStep)){
		MM_EXTRA_ASSERT(mm->bg.flags.inPostStepList);
		return;
	}
	
	mmNeedsPostStepBG(mm);
}

static void mmPipeQueueDestroyBG(	MovementManager* mm,
									MovementRequesterPipe* mrp)
{
	if(mm == mrp->mrSource->mm){
		if(!FALSE_THEN_SET(mrp->flagsSource.destroy)){
			MM_EXTRA_ASSERT(mm->bg.flags.mrPipeNeedsPostStep);
			MM_EXTRA_ASSERT(mm->bg.flags.inPostStepList);
			return;
		}
	}else{
		MM_EXTRA_ASSERT(mm == mrp->mrTarget->mm);

		if(!FALSE_THEN_SET(mrp->flagsTarget.destroy)){
			MM_EXTRA_ASSERT(mm->bg.flags.mrPipeNeedsPostStep);
			MM_EXTRA_ASSERT(mm->bg.flags.inPostStepList);
			return;
		}
	}
	
	mmPipeNeedsPostStepBG(mm);
}

void mmRequesterDestroyPipesBG(	MovementManager* mm,
								MovementRequester* mr)
{
	if(!eaSize(&mm->bg.pipes)){
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	EARRAY_CONST_FOREACH_BEGIN(mm->bg.pipes, i, isize);
	{
		MovementRequesterPipe* mrp = mm->bg.pipes[i];
		
		if(	mrp->mrSource == mr ||
			mrp->mrTarget == mr)
		{
			mmPipeQueueDestroyBG(mm, mrp);
		}
	}
	EARRAY_FOREACH_END;
	
	PERFINFO_AUTO_STOP();
}

static void mmRequesterHandleDestroyedFromFG(	MovementManager* mm,
												MovementThreadData* td,
												MovementRequester* mr)
{
	MovementRequesterThreadData* mrtd = MR_THREADDATA_BG(mr);
	
	PERFINFO_AUTO_START_FUNC();

	mrLog(mr, NULL, "Received destroy from FG.");
	
	ASSERT_FALSE_AND_SET(mrtd->toFG.flagsMutable.removedFromList);
	MM_TD_SET_HAS_TOFG(mm, td);
	td->toFG.flagsMutable.mrHasUpdate = 1;

	ASSERT_TRUE_AND_RESET(mr->bg.flagsMutable.inList);

	// Clear handledMsgs in order to clear up exec lists.
	
	mmRequesterSetHandledMsgsBG(mm, mr, 0);
	
	// Destroy history.
	
	mmRequesterBGPredictDestroy(mm, mr);
	
	// Remove override values.
	
	mmRequesterDestroyOverrideValuesBG(mm, mr);

	// Release owned data class bits.

	mmRequesterReleaseOwnedDataClassBitsBG(mm, mr);
	
	// Free SimBodyInstance stuff.

	mmRequesterDestroySimBodyInstancesBG(mm, mr);
	
	// Destroy owned resources.
	
	mmDestroyRequesterResourcesBG(mm, mr);
	
	// Destroy pipes.
	
	mmRequesterDestroyPipesBG(mm, mr);
	
	// Destroy stances.
	
	mmRequesterDestroyStancesBG(mm, mr);

	// Send it back to FG.

	mrDestroy(&mr);
	
	PERFINFO_AUTO_STOP();
}

static void mmHandleRequesterUpdatesFromFG(	MovementManager* mm,
											MovementThreadData* td)
{
	S32 applyAllSyncs;
	
	PERFINFO_AUTO_START_FUNC();

	mmLog(	mm,
			NULL,
			"[bg.fromFG] Getting requester updates from FG.");

	applyAllSyncs = TRUE_THEN_RESET(td->toBG.flagsMutable.applySyncNow);
	
	if(MMLOG_IS_ENABLED(mm)){
		if(SAFE_MEMBER(td->toBG.repredict, cpc)){
			mmLog(	mm,
					NULL,
					"[bg.fromFG] Requester updates (cpc %u).",
					td->toBG.repredict->cpc);
		}
		
		if(applyAllSyncs){
			mmLog(	mm,
					NULL,
					"[bg.fromFG] Applying all syncs now.");
		}
	}
	
	if(td->toBG.melRequesters.head){
		MEL_FOREACH_BEGIN(iter, td->toBG.melRequesters);
		{
			MovementRequester*				mr;
			MovementRequesterThreadData*	mrtd;

			mrtd = MRTD_FROM_MEMBER(toBG.execNode, iter.node);
			ASSERT_TRUE_AND_RESET(mrtd->toBG.flagsMutable.hasUpdate);

			mr = MR_FROM_MEMBER(threadData[MM_BG_SLOT], mrtd);
			
			if(TRUE_THEN_RESET(mrtd->toBG.flagsMutable.removeFromList)){
				if(eaFindAndRemove(&mm->bg.requestersMutable, mr) < 0){
					assert(0);
				}
				
				mmRequesterHandleDestroyedFromFG(mm, td, mr);

				continue;
			}

			if(TRUE_THEN_RESET(mrtd->toBG.flagsMutable.receivedNetHandle)){
				mr->bg.flagsMutable.receivedNetHandle = 1;
			}

			if(!mgState.flags.noLocalProcessing){
				if(SAFE_MEMBER(mrtd->toBG.predict, userStruct.serverBG)){
					mmHandleRequesterServerStateFromFG(mm, td, mr, mrtd);
				}

				if(TRUE_THEN_RESET(mrtd->toBG.flagsMutable.hasSync)){
					mmHandleRequesterSyncFromFG(mm, td, mr, mrtd, applyAllSyncs);
				}
			}
		}
		MEL_FOREACH_END;
		
		td->toBG.melRequesters.head = td->toBG.melRequesters.tail = NULL;
	}
	
	PERFINFO_AUTO_STOP();
}

static void mmHandleRepredictsFromFG(	MovementManager* mm,
										MovementThreadData* td)
{
	PERFINFO_AUTO_START_FUNC();
	
	mmLog(	mm,
			NULL,
			"[bg.repredict] Getting %u recycled repredict structs from FG.",
			eaUSize(&td->toBG.repredict->repredicts));

	eaPushEArray(	&mm->bg.available.repredictsMutable,
					&td->toBG.repredict->repredicts);
					
	eaClearFast(&td->toBG.repredict->repredictsMutable);

	PERFINFO_AUTO_STOP();
}

static void mmSendMsgsClientAttachmentChangedBG(MovementManager* mm){
	EARRAY_CONST_FOREACH_BEGIN(mm->bg.requesters, i, isize);
	{
		MovementRequester*				mr = mm->bg.requesters[i];
		MovementRequesterMsgPrivateData pd;

		mmRequesterMsgInitBG(	&pd,
								NULL,
								mr,
								MR_MSG_BG_CLIENT_ATTACHMENT_CHANGED);

		mmRequesterMsgSend(&pd);
	}
	EARRAY_FOREACH_END;
}

static void mmSendMsgUserThreadDataUpdatedBG(MovementManager* mm){
	PERFINFO_AUTO_START_FUNC();
	
	mmLog(	mm,
			NULL,
			"[bg.fromFG] Getting userThreadData update from FG.");

	if(mm->msgHandler){
		MovementManagerMsgPrivateData pd = {0};
		
		pd.mm = mm;
		pd.msg.msgType = MM_MSG_BG_UPDATE_FROM_FG;
		pd.msg.userPointer = mm->userPointer;
		pd.msg.bg.updateFromFG.threadData = mm->userThreadData[MM_BG_SLOT];
	
		mm->msgHandler(&pd.msg);
	}
	
	PERFINFO_AUTO_STOP();
}

static void mmSendMsgAfterUpdatedUserThreadDataToFG(MovementManager* mm){
	PERFINFO_AUTO_START_FUNC();

	if(mm->msgHandler){
		MovementManagerMsgPrivateData pd = {0};
		
		pd.mm = mm;
		pd.msg.msgType = MM_MSG_BG_AFTER_SEND_UPDATE_TO_FG;
		pd.msg.userPointer = mm->userPointer;
		pd.msg.bg.afterSendUpdateToFG.threadData = mm->userThreadData[MM_BG_SLOT];
	
		mm->msgHandler(&pd.msg);
	}
	
	PERFINFO_AUTO_STOP();
}

void mmMsgSetUserThreadDataUpdatedBG(const MovementManagerMsg* msg){
	MovementManagerMsgPrivateData*	pd = MM_MSG_TO_PD(msg);
	MovementThreadData*				td;

	if(	!SAFE_MEMBER(pd, mm) ||
		!MM_MSG_IS_BG(pd->msgType))
	{
		return;
	}
	
	td = MM_THREADDATA_BG(pd->mm);
	
	td->toFG.flagsMutable.userThreadDataHasUpdate = 1;

	mmSetAfterSimWakesOnceBG(pd->mm);
}

static void mmHandleClientAttachmentChangeFromFG(	MovementManager* mm,
													MovementThreadData* td)
{
	PERFINFO_AUTO_START_FUNC();

	mmLog(	mm,
			NULL,
			"[bg.fromFG] Client attachment changed: %s (was %s).",
			td->toBG.flags.isAttachedToClient ? "ON" : "OFF",
			mm->bg.flags.isAttachedToClient ? "ON" : "OFF");

	mm->bg.flagsMutable.isAttachedToClient = td->toBG.flags.isAttachedToClient;
	
	// Release data owned by unsynced requesters (ai can't run on client, etc).
	
	if(mm->bg.flags.isAttachedToClient){
		EARRAY_CONST_FOREACH_BEGIN(mm->bg.requesters, i, isize);
		{
			MovementRequester* mr = mm->bg.requesters[i];

			if(	!mr->mrc->flags.syncToClient &&
				mr->bg.ownedDataClassBits)
			{
				mmReleaseDataOwnershipInternalBG(	mr,
													MDC_BITS_ALL,
													"attached to client");
			}

			mmRequesterBGPredictCreate(mm, mr);
		}
		EARRAY_FOREACH_END;
	}else{
		SAFE_FREE(mm->bg.miState);

		EARRAY_CONST_FOREACH_BEGIN(mm->bg.requesters, i, isize);
		{
			MovementRequester* mr = mm->bg.requesters[i];
			
			mmRequesterBGPredictDestroy(mm, mr);
		}
		EARRAY_FOREACH_END;
		
		mmPredictedStepsDestroyUntilBG(mm, td, 0);

		mm->bg.flagsMutable.mrHasHistory = 0;
	}

	mmSendMsgsClientAttachmentChangedBG(mm);

	PERFINFO_AUTO_STOP();
}

static void mmHandleUpdatesFromFG(	MovementManager* mm,
									MovementThreadData* td)
{
	PERFINFO_AUTO_START_FUNC();
	
	#define HAS_FLAG(x) TRUE_THEN_RESET(td->toBG.flagsMutable.x)
	
	mmLog(	mm,
			NULL,
			"[bg.fromFG] Getting updates from FG\n");
	
	// Check some things that happen very rarely.
	
	if(	td->toBG.flags.hasForcedSet ||
		td->toBG.flags.hasForcedSetRot ||
		td->toBG.flags.hasSetPosVersion ||
		td->toBG.flags.clientWasChanged ||
		td->toBG.flags.noCollisionChanged ||
		td->toBG.flags.isInactiveUpdated ||
		td->toBG.flags.hasNewRequesters || 
		td->toBG.flags.hasUpdatedDeathPrediction || 
		td->toBG.flags.capsuleOrientationMethodChanged)
	{
		if(HAS_FLAG(hasForcedSet)){
			PERFINFO_AUTO_START("hasForcedSet", 1);
			{
				mmLog(mm, NULL, "[bg.fromFG] Got forced set.");

				mm->bg.forcedSetCount = td->toBG.forcedSetCount;
				mm->bg.flagsMutable.sendForcedSetCountToFG = 1;
			}
			PERFINFO_AUTO_STOP();
		}
		
		if(HAS_FLAG(hasForcedSetRot)){
			mm->bg.flagsMutable.sendForcedRotMsg = 1;
		}
		
		if(HAS_FLAG(hasSetPosVersion)){
			mm->bg.setPosVersionMutable = td->toBG.setPosVersion;
			mm->bg.flagsMutable.needsSetPosVersion = 1;
			mm->bg.flagsMutable.resetOnNextInputStep = 1;
		}

		if(HAS_FLAG(clientWasChanged)){
			mmHandleClientAttachmentChangeFromFG(mm, td);
		}
		
		if(HAS_FLAG(noCollisionChanged)){
			mm->bg.flagsMutable.noCollision = td->toBG.flags.noCollision;
		}

		if(HAS_FLAG(isInactiveUpdated)){
			mm->bg.flagsMutable.isInactive = td->toBG.flags.isInactive;
		}

		if(HAS_FLAG(hasNewRequesters)){
			mmHandleNewRequestersFromFG(mm, td);
		}

		if(HAS_FLAG(hasUpdatedDeathPrediction)){
			mm->bg.flagsMutable.isInDeathPrediction = td->toBG.flags.isInDeathPrediction;
		}

		if(HAS_FLAG(capsuleOrientationMethodChanged)){
			mm->bg.flagsMutable.capsuleOrientationUseRotation = td->toBG.flags.capsuleOrientationUseRotation;
		}
	}
	
	// Handle resource updates from FG.

	if(HAS_FLAG(mmrHasUpdate)){
		mmHandleResourceUpdatesFromFG(mm, td);
	}

	// Handle requester updates from FG.

	if(HAS_FLAG(mrHasUpdate)){
		mmHandleRequesterUpdatesFromFG(mm, td);
	}
	
	// Handle used repredicts.
	
	if(HAS_FLAG(hasRepredicts)){
		mmHandleRepredictsFromFG(mm, td);
	}

	// Handle position/rotation updates from FG.

	if(	td->toBG.flags.useNewPos ||
		td->toBG.flags.useNewRot)
	{
		mmHandleNewPositionOrRotationFromFG(mm, td);
	}
	
	// Handle userThreadData update from FG.
	
	if(HAS_FLAG(userThreadDataHasUpdate)){
		mmSendMsgUserThreadDataUpdatedBG(mm);
	}
	
	#undef HAS_FLAG
	
	PERFINFO_AUTO_STOP();
}

static void mmReclaimOutputsBG(	MovementManager* mm,
								MovementThreadData* td)
{
	const MovementThreadData*const	tdFG = MM_THREADDATA_FG(mm);
	const MovementOutputList*const	olFG = &tdFG->toFG.outputList;
	MovementOutputList*const		ol = &mm->bg.outputListMutable;
	
	PERFINFO_AUTO_START_FUNC();

	mmVerifyOutputListsBG(mm);
	mmVerifyPredictedStepOutputsBG(mm);

	// Reclaim outputs between the BG list head and the FG list head.
	
	if(	olFG->head &&
		ol->head != olFG->head)
	{
		MovementOutput*const oTail = olFG->head->prev;
		
		assert(oTail);
		assert(ol->head);
		assert(!ol->head->prev);
		
		assert(oTail->next == olFG->head);
		oTail->nextMutable = NULL;
		olFG->head->prevMutable = NULL;

		mmOutputListAddTail(&mm->bg.available.outputListMutable, ol->head);
		mmOutputListSetTail(&mm->bg.available.outputListMutable, oTail);

		ol->head = olFG->head;
		
		if(td->toFG.outputList.head){
			td->toFG.outputListMutable.head = ol->head;
			td->toFG.outputListMutable.tail = ol->tail;
		}
	}

	mmVerifyOutputListsBG(mm);
	mmVerifyPredictedStepOutputsBG(mm);

	// Copy current bg output list to FG.

	if(!td->toFG.outputList.head){
		td->toFG.outputListMutable = *ol;
	}

	// Drop outputs from the head of the current toFG list until the oldestToKeep is hit.
	
	if(td->toFG.outputList.head){
		const MovementOutput*const	oTail = td->toFG.outputList.tail;
		MovementOutput*				oHead;
		
		if(mgState.flags.isServer){
			for(oHead = td->toFG.outputList.head;
				oHead != oTail &&
					subS32(oHead->pc.server, mgState.bg.pc.local.oldestToKeep) < 0;
				oHead = oHead->next)
			{
				// Do nothing.
			}

			td->toFG.outputListMutable.head = oHead;
		}else{
			for(oHead = td->toFG.outputList.head;
				oHead != oTail &&
					subS32(oHead->pc.client, mgState.bg.pc.local.oldestToKeep) < 0;
				oHead = oHead->next)
			{
				// Do nothing.
			}

			td->toFG.outputListMutable.head = oHead;
		}
	}

	// Set it to the end of the current list, cuz that's close enough.
	
	td->toFG.lastViewedOutputMutable = td->toFG.outputList.tail;
	
	mmVerifyOutputListsBG(mm);
	mmVerifyPredictedStepOutputsBG(mm);
	
	PERFINFO_AUTO_STOP();
}

static void mmUpdateProcessCountsBG(MovementManager* mm,
									MovementThreadData* td)
{
	// Calculate the oldest times to keep stuff from, so the FG knows what's been released.

	if(mm->bg.flags.isAttachedToClient){
		td->toFG.spcOldestToKeep = mgState.bg.netReceive.cur.pc.serverSync -
										3 * MM_PROCESS_COUNTS_PER_SECOND;
	}else{
		td->toFG.spcOldestToKeep = mgState.bg.netReceive.cur.pc.server -
										3 * MM_PROCESS_COUNTS_PER_SECOND;
	}
}

static void mmDestroyOutputsInListBG(MovementOutputList* ol){
	MovementOutput* oHead = ol->head;
	
	while(oHead){
		MovementOutput* oNext = oHead->next;
		mmOutputDestroy(&oHead);
		oHead = oNext;
	}
	
	ZeroStruct(ol);
}

static void mmSetUpdatedToFG(MovementManager* mm){
	MovementThreadData* td = MM_THREADDATA_BG(mm);
	
	if(FALSE_THEN_SET(td->toFG.flagsMutable.inUpdatedList)){
		MovementGlobalStateThreadData* mgtd = mgState.threadData + MM_BG_SLOT;
		
		eaPush(&mgtd->toFG.updatedManagers, mm);
		mgtd->toFG.flags.hasUpdatedManagers = 1;
	}
}

void mmSetAfterSimWakesOnceBG(MovementManager* mm){
	MovementThreadData* td = MM_THREADDATA_BG(mm);
	MovementProcessingThread* t = TlsGetValue(mgState.bg.threads.tls.processingThread);

	if(!t)
		return;

	if(FALSE_THEN_SET(td->toFG.flagsMutable.afterSimOnce)){
		assert(t->threadID == GetCurrentThreadId());
		
		if(!t->results){
			t->results = callocStruct(MovementProcessingThreadResults);
		}

		eaPush(&t->results->managersAfterSimWakesMutable, mm);
	}
}

void mmDestroyAllSimBodyInstancesBG(MovementManager* mm){
	mmRareLockEnter(mm);
	{
		EARRAY_CONST_FOREACH_BEGIN(mm->bg.simBodyInstances, i, isize);
		{
			MovementSimBodyInstance* sbi = mm->bg.simBodyInstances[i];
			
			if(!sbi){
				continue;
			}
			
			assert(sbi->mm == mm);
			
			sbi->mm = NULL;
			sbi->mr = NULL;
			
			mmSimBodyInstanceDestroyBG(sbi);
		}
		EARRAY_FOREACH_END;
		
		eaDestroy(&mm->bg.simBodyInstancesMutable);
	}
	mmRareLockLeave(mm);
}

static void mrPipeMsgDestroyBG(MovementRequesterPipeMsg** msgInOut){
	MovementRequesterPipeMsg* msg = SAFE_DEREF(msgInOut);
	
	if(!msg){
		return;
	}
	
	*msgInOut = NULL;
	
	switch(msg->msgType){
		xcase MR_PIPE_MSG_STRING:{
			SAFE_FREE(msg->string);
		}
	}
	
	SAFE_FREE(msg);
}

static void mrPipeMsgDeliverBG(const MovementRequesterPipe* mrp,
							const MovementRequesterPipeMsg* msg)
{
	MovementRequesterMsgPrivateData pd;

	mmRequesterMsgInitBG(	&pd,
							NULL,
							mrp->mrTarget,
							MR_MSG_BG_PIPE_MSG);
	
	pd.msg.in.bg.pipeMsg.msgType = msg->msgType;
	
	switch(msg->msgType){
		xcase MR_PIPE_MSG_STRING:{
			mrLog(	mrp->mrSource,
					NULL,
					"Delivering PIPE_MSG sent from me (string = \"%s\").",
					msg->string);

			mrLog(	mrp->mrTarget,
					NULL,
					"Receiving PIPE_MSG sent to me (string = \"%s\").",
					msg->string);
					
			pd.msg.in.bg.pipeMsg.string = msg->string;
		}
	}

	mmRequesterMsgSend(&pd);
}

static void mmPipeDestroyByIndexBG(	MovementManager* mm,
									U32 index)
{
	MovementRequesterPipe* mrp = eaRemove(&mm->bg.pipesMutable, index);

	if(mm == mrp->mrSource->mm){
		if(mm != mrp->mrTarget->mm){
			// Remove from the target.
			
			if(eaFindAndRemove(&mrp->mrTarget->mm->bg.pipesMutable, mrp) < 0){
				assertmsg(0, "Pipe not in target's list.");
			}
		}
	}else{
		// Remove from the source.
		
		assert(mm == mrp->mrTarget->mm);
		
		if(eaFindAndRemove(&mrp->mrSource->mm->bg.pipesMutable, mrp) < 0){
			assertmsg(0, "Pipe not in source's list.");
		}
	}

	// Tell the source that the pipe is destroyed.

	if(!mrp->mrSource->bg.flags.destroyed){
		MovementRequesterMsgPrivateData pd;

		mrLog(	mrp->mrSource,
				NULL,
				"Sending msg PIPE_DESTROYED.");

		mmRequesterMsgInitBG(	&pd,
								NULL,
								mrp->mrSource,
								MR_MSG_BG_PIPE_DESTROYED);
		
		pd.msg.in.bg.pipeDestroyed.handle = mrp->handle;

		mmRequesterMsgSend(&pd);
	}
	
	// Destroy msgs.
	
	while(eaSize(&mrp->msgs)){
		MovementRequesterPipeMsg* msg = mrp->msgs[0];
		
		eaRemove(&mrp->msgs, 0);
		
		mrPipeMsgDestroyBG(&msg);
	}
	
	eaDestroy(&mrp->msgs);
	
	// Done.

	SAFE_FREE(mrp);
}

static void mmHandlePipePostStepBG(MovementManager* mm){
	S32 done = 0;
	
	while(!done){
		done = 1;

		EARRAY_CONST_FOREACH_BEGIN(mm->bg.pipes, i, isize);
		{
			MovementRequesterPipe* mrp = mm->bg.pipes[i];
			
			if(	mrp->flagsSource.destroy ||
				mrp->flagsTarget.destroy)
			{
				mmPipeDestroyByIndexBG(mm, i);
				
				// Force a do-over, in case there was any re-entrance.
				// TODO: Do this better, like set a flag for restarts.

				done = 0;
				break;
			}
			
			EARRAY_CONST_FOREACH_BEGIN(mrp->msgs, j, jsize);
			{
				const MovementRequesterPipeMsg*	msg = mrp->msgs[j];

				mrPipeMsgDeliverBG(mrp, mrp->msgs[j]);
				mrPipeMsgDestroyBG(&mrp->msgs[j]);
			}
			EARRAY_FOREACH_END;
			
			eaDestroy(&mrp->msgs);
		}
		EARRAY_FOREACH_END;
	}
}

static void mmRemoveFromListBG(	MovementManager* mm,
								MovementManager*** managers)
{
	// Remove from the list.

	assert(mm->bg.listIndex < eaUSize(managers));
	assert(mm == (*managers)[mm->bg.listIndex]);
	eaRemoveFast(managers, mm->bg.listIndex);

	// Update the new mm in this index.

	if(mm->bg.listIndex < eaUSize(managers)){
		assert(mm != (*managers)[mm->bg.listIndex]);
		assert((*managers)[mm->bg.listIndex]->bg.listIndex == eaUSize(managers));
		(*managers)[mm->bg.listIndex]->bg.listIndexMutable = mm->bg.listIndex;
	}

	mm->bg.listIndexMutable = 0;
}

static void mmHandleDestroyedFromFG(MovementManager* mm,
									MovementThreadData* td)
{
	PERFINFO_AUTO_START_FUNC();

	ASSERT_TRUE_AND_RESET(mm->bg.flagsMutable.inList);

	if(mm->bg.flags.isAttachedToClient){
		mmRemoveFromListBG(mm, &mgState.bg.managers.clientMutable);
	}else{
		mmRemoveFromListBG(mm, &mgState.bg.managers.nonClientMutable);
	}

	// Send destroy ACK to FG.
	
	mmSetUpdatedToFG(mm);
	td->toFG.flagsMutable.destroyed = 1;

	// Deactivate all resources.
	
	ASSERT_FALSE_AND_SET(mgState.bg.flagsMutable.gridIsWritable);
	mmDeactivateAllResourcesBG(mm);
	ASSERT_TRUE_AND_RESET(mgState.bg.flagsMutable.gridIsWritable);

	// Remove from entity index array.

	{
		U32 index = INDEX_FROM_REFERENCE(mm->entityRef);
		
		if(index < ARRAY_SIZE(mgState.bg.entIndexToManager)){
			// Another mm might already be in this index, so check for that.

			if(mgState.bg.entIndexToManager[index] == mm){
				mgState.bg.entIndexToManager[index] = NULL;
			}
		}else{
			assert(mm->flags.isLocal);

			if(mgState.bg.stEntIndexToManager){
				MovementManager* mmTest;
				
				if(	stashIntFindPointer(mgState.bg.stEntIndexToManager, mm->entityRef, &mmTest) &&
					mmTest == mm)
				{
					stashIntRemovePointer(mgState.bg.stEntIndexToManager, mm->entityRef, NULL);
				}
			}
		}
	}

	// Destroy outputs.
	
	ARRAY_FOREACH_BEGIN(mm->threadData, i);
	{
		ZeroStruct(&mm->threadData[i].toFG.outputListMutable);
		mm->threadData[i].toFG.lastViewedOutputMutable = NULL;
	}
	ARRAY_FOREACH_END;
	
	mmDestroyOutputsInListBG(&mm->bg.outputListMutable);
	mmDestroyOutputsInListBG(&mm->bg.available.outputListMutable);

	// Destroy available anim values.

	EARRAY_CONST_FOREACH_BEGIN(mm->bg.available.animValues, i, isize);
	{
		eaiDestroy(&mm->bg.available.animValuesMutable[i]);
	}
	EARRAY_FOREACH_END;

	eaDestroy(&mm->bg.available.animValuesMutable);
	
	// Destroy repredicts.
	
	eaDestroyEx(&td->toFG.repredictsMutable,
				mmOutputRepredictDestroyUnsafe);

	// Destroy predictedSteps.
	
	if(mm->bg.predictedSteps){
		EARRAY_CONST_FOREACH_BEGIN(mm->bg.predictedSteps, i, isize);
		{
			mmPredictedStepDestroyBG(mm, td, &mm->bg.predictedStepsMutable[i]);
		}
		EARRAY_FOREACH_END;
		
		eaDestroy(&mm->bg.predictedStepsMutable);
		
		mm->bg.flagsMutable.hasPredictedSteps = 0;
	}

	// Remove from past state list.
	
	if(TRUE_THEN_RESET(mm->bg.flagsMutable.hasChangedOutputDataRecently)){
		mmPastStateListCountDecBG(mm);
		mmVerifyPastStateCountBG(mm);
	}
	
	if(TRUE_THEN_RESET(mm->bg.flagsMutable.mmrNeedsSetState)){
		mmPastStateListCountDecBG(mm);
		mmVerifyPastStateCountBG(mm);
	}
	
	assert(!mm->bg.flags.inSetPastStateList);
	assert(!mm->bg.flags.inSetPastStateChangedList);

	// Remove from grid.

	ASSERT_FALSE_AND_SET(mgState.bg.flagsMutable.gridIsWritable);
	mmRemoveFromGridBG(mm);
	ASSERT_TRUE_AND_RESET(mgState.bg.flagsMutable.gridIsWritable);

	// Destroy overrides.

	mmOverrideTableClearBG(mm);
	stashTableDestroySafe(&mm->bg.stOverrideValues);
	
	// Destroy all pipes.
	
	EARRAY_CONST_FOREACH_BEGIN(mm->bg.pipes, i, isize);
	{
		mmPipeQueueDestroyBG(mm, mm->bg.pipes[i]);
	}
	EARRAY_FOREACH_END;
	
	mmHandlePipePostStepBG(mm);
	
	// Destroy requesters.

	EARRAY_CONST_FOREACH_BEGIN(mm->bg.requesters, i, size);
	{
		MovementRequester* mr = mm->bg.requesters[i];

		mmBodyLockEnter();
		{
			eaFindAndRemove(&mgState.bg.mrsThatNeedSimBodyCreate, mr);
		}
		mmBodyLockLeave();

		mmRequesterBGPredictDestroy(mm, mr);
		
		// Destroy queued syncs.
		
		EARRAY_CONST_FOREACH_BEGIN(mr->bg.queuedSyncs, j, jsize);
		{
			MovementQueuedSync* qs = mr->bg.queuedSyncs[j];
			
			mmStructDestroy(mr->mrc->pti.sync,
							qs->sync,
							mm);

			mmStructDestroy(mr->mrc->pti.syncPublic,
							qs->syncPublic,
							mm);
			
			SAFE_FREE(qs);
		}
		EARRAY_FOREACH_END;
		
		eaDestroy(&mr->bg.queuedSyncsMutable);
		
		// Release owned data.
		
		if(mr->bg.ownedDataClassBits){
			FOR_BEGIN(j, MDC_COUNT);
			{
				const U32 bit = BIT(j);
				
				if(mr->bg.ownedDataClassBits & bit){
					assert(mm->bg.dataOwner[j] == mr);
					mm->bg.dataOwnerMutable[j] = NULL;

					mr->bg.flagsMutable.bgUnchangedSinceCopyToFG = 0;
					mr->bg.ownedDataClassBitsMutable &= ~bit;
					
					if(	!mr->bg.ownedDataClassBits &&
						mr->bg.handledMsgs & MR_HANDLED_MSG_BEFORE_DISCUSSION)
					{
						mmRequesterRemoveFromMsgBeforeDiscussionBG(mm, mr);
					}
				}
			}
			FOR_END;
		}
		
		// Destroy stances.
		
		mmRequesterDestroyStancesBG(mm, mr);
	}
	EARRAY_FOREACH_END;

	// Destroy all simBodyInstances.

	mmDestroyAllSimBodyInstancesBG(mm);

	// Clear data owners.

	ZeroArray(mm->bg.dataOwnerMutable);

	// Destroy input state.

	SAFE_FREE(mm->bg.miState);
	
	// Destroy stance bits.
	
	eaiDestroy(&mm->bg.stanceBitsMutable);
	assert(!eaSize(&mm->bg.stances));
	mmLastAnimReset(&mm->bg.lastAnimMutable);
	
	// Double check stuff.
	
	assert(!mm->bg.gridEntry.cell);
	
	PERFINFO_AUTO_STOP();
}

static void mmAllUpdateProcessFlagsBG(void){
	if(!mgState.bg.betweenSim.instanceThisFrame){
		if(mgState.flags.isServer){
			mgState.bg.pc.local.oldestToKeep =	mgState.bg.pc.local.cur -
												3 * MM_PROCESS_COUNTS_PER_SECOND;
		}else{
			mgState.bg.pc.local.oldestToKeep =	mgState.bg.netReceive.cur.pc.client -
												MM_PROCESS_COUNTS_PER_SECOND;
			
			if(	subS32(	mgState.bg.pc.local.cur,
						mgState.bg.pc.local.oldestToKeep) >
					10 * MM_PROCESS_COUNTS_PER_SECOND)
			{
				mgState.bg.pc.local.oldestToKeep =	mgState.bg.pc.local.cur -
													10 * MM_PROCESS_COUNTS_PER_SECOND;
			}
		}
	}

	if(mgState.bg.betweenSim.flags.noProcessThisFrame){
		mgState.bg.flagsMutable.doRunCurrentStep = 0;
	}else{
		mgState.bg.flagsMutable.doProcessIfValidStep = !mgState.bg.flags.doProcessIfValidStep;
		mgState.bg.flagsMutable.doRunCurrentStep =	!mgState.flags.noLocalProcessing &&
													mgState.bg.flags.doProcessIfValidStep;
	}

	if(	!mgState.bg.betweenSim.instanceThisFrame &&
		!mgState.bg.frame.cur.stepCount.total
		||
		mgState.bg.frame.cur.stepCount.cur + 1 == mgState.bg.frame.cur.stepCount.total &&
		mgState.bg.flagsMutable.doRunCurrentStep)
	{
		mgState.bg.flagsMutable.isLastStepOnThisFrame =	1;
	}else{
		mgState.bg.flagsMutable.isLastStepOnThisFrame =	0;
	}

	mmGlobalLog("Updated betweenSim process values:\n"
				"%u / %u steps\n"
				"isLastStepOnThisFrame = %u\n"
				"noProcessThisFrame = %u\n"
				"doRunCurrentStep = %u\n"
				"doProcessIfValidStep = %u\n"
				,
				mgState.bg.frame.cur.stepCount.cur,
				mgState.bg.frame.cur.stepCount.total,
				mgState.bg.flags.isLastStepOnThisFrame,
				mgState.bg.betweenSim.flags.noProcessThisFrame,
				mgState.bg.flags.doRunCurrentStep,
				mgState.bg.flags.doProcessIfValidStep);
}

static void mmHandleFirstBetweenSimThisFrameBG(	MovementManager* mm,
												MovementThreadData* td)
{
	S32 doSendMsg = TRUE_THEN_RESET(mm->bg.flagsMutable.sentUserThreadDataUpdateToFG);

	if(!mgState.flags.isServer){
		mmUpdateProcessCountsBG(mm, td);
	}

	// Handle data from FG.

	if(TRUE_THEN_RESET(td->toBG.flagsMutable.hasToBG)){
		mmHandleUpdatesFromFG(mm, td);
	}

	if(doSendMsg){
		mmSendMsgAfterUpdatedUserThreadDataToFG(mm);
	}

	// Check for timed out queued syncs.
	
	if(mgState.flags.isServer){
		S32 applySyncNow = TRUE_THEN_RESET(td->toBG.flagsMutable.applySyncNow);
		
		if(	mm->bg.flags.mrHasQueuedSync ||
			applySyncNow)
		{
			mmApplyQueuedSyncsBG(	mm,
									NULL,
									mgState.bg.pc.local.cur -
										MM_PROCESS_COUNTS_PER_SECOND,
									applySyncNow);
		}
	}else{
		// Check if a repredict is needed.

		mmRepredictBG(mm, td);

		// Release old stuff.

		if(mm->bg.flags.hasPredictedSteps){
			mmPredictedStepsDestroyUntilBG(	mm,
											td,
											mgState.bg.pc.local.oldestToKeep);
		}
		
		if(mm->bg.flags.mrHasHistory){
			mmRequestersDestroyHistoryUntilBG(	mm,
												mgState.bg.pc.local.oldestToKeep);
		}
	}

	mmReclaimOutputsBG(mm, td);
}

static void mmAfterLastStepOnThisFrameBG(	MovementManager* mm,
											MovementThreadData* td)
{
	if(!td->toFG.outputList.head){
		mmOutputListSetTail(&td->toFG.outputListMutable,
							mm->bg.outputList.head);
	}
		
	mmOutputListSetTail(&td->toFG.outputListMutable,
						mm->bg.outputList.tail);

	if(!mgState.flags.isServer){
		while(	td->toFG.flags.hasRepredicts &&
				!mmOutputListContains(&td->toFG.outputList, td->toFG.repredicts[0]->o))
		{
			MovementOutputRepredict* mor = eaRemove(&td->toFG.repredictsMutable, 0);
				
			#if MM_VERIFY_REPREDICTS
				eaiRemove(&td->toFG.repredictPCs, 0);
			#endif

			eaPush(&mm->bg.available.repredictsMutable, mor);
					
			if(!eaSize(&td->toFG.repredicts)){
				td->toFG.flagsMutable.hasRepredicts = 0;
			}
		}
	}
	else if(mm->bg.flags.isAttachedToClient){
		// Make a copy of the BG state so the FG can sync it to the client.

		mmCopyPredictStateToFG(mm, td);
	}
		
	#if VERIFY_PREDICTED_STEP_OUTPUTS
	{
		EARRAY_CONST_FOREACH_BEGIN(td->toFG.repredicts, i, isize);
		{
			const MovementOutputRepredict* mor = td->toFG.repredicts[i];
			assert(mmOutputListContains(&td->toFG.outputList, mor->o));
		}
		EARRAY_FOREACH_END;
	}
	#endif
		
	if(TRUE_THEN_RESET(mm->bg.nextFrame[MM_BG_SLOT].flagsMutable.sendStanceBitsToFG)){
		eaiCopy(&td->toFG.stanceBitsMutable,
				&mm->bg.stanceBits);
	}

	if(TRUE_THEN_RESET(mm->bg.nextFrame[MM_BG_SLOT].flagsMutable.sendLastAnimToFG)){
		mmLastAnimCopy(	&td->toFG.lastAnimMutable,
						&mm->bg.lastAnim);
	}
}

static void mmBetweenSimCommonBG(	MovementProcessingThread* t,
									MovementManager* mm)
{
	MovementThreadData* td;
	S32					startedTimer;

	assert(!t->mm);
	t->mmMutable = mm;

	startedTimer =	mgState.debug.flags.perEntityTimers &&
					mmStartPerEntityTimer(mm);

	td = MM_THREADDATA_BG(mm);
	
	mmVerifyViewStatusToFG(mm);

	mmLog(	mm,
			NULL,
			"[bg.betweenSim] Starting %s\n"
			"Some flags: %s%s%s",
			__FUNCTION__,
			td->toBG.flags.hasToBG ? "hasToBG, " : "",
			mm->bg.flags.mrHasNewSync ? "mrHasNewSync, " : "",
			td->toBG.flags.applySyncNow ? "applySyncNow, " : "");

	mmVerifyAnimOutputBG(mm, NULL);

	mmVerifyViewStatusToFG(mm);

	if(!mgState.bg.betweenSim.instanceThisFrame){
		mmHandleFirstBetweenSimThisFrameBG(mm, td);
	}

	mmVerifyAnimOutputBG(mm, NULL);

	mmVerifyViewStatusToFG(mm);

	if(mgState.bg.flags.doRunCurrentStep){
		mmRunLatestSingleStepBG(mm, td);
	}
	
	mmVerifyViewStatusToFG(mm);

	if(mgState.bg.flags.isLastStepOnThisFrame){
		mmAfterLastStepOnThisFrameBG(mm, td);
	}
						
	mmVerifyAnimOutputBG(mm, NULL);

	mmVerifyViewStatusToFG(mm);

	assert(t->mm == mm);
	t->mmMutable = NULL;

	if(startedTimer){
		PERFINFO_AUTO_STOP();
	}
}

static void mmBetweenSimClientBG(	MovementProcessingThread* t,
									MovementManager* mm)
{
	PerfInfoGuard* piGuard;

	PERFINFO_AUTO_START_FUNC_GUARD(&piGuard);
	mmBetweenSimCommonBG(t, mm);
	PERFINFO_AUTO_STOP_GUARD(&piGuard);
}

static void mmBetweenSimNonClientBG(MovementProcessingThread* t,
									MovementManager* mm)
{
	PerfInfoGuard* piGuard;

	PERFINFO_AUTO_START_FUNC_GUARD(&piGuard);
	mmBetweenSimCommonBG(t, mm);
	PERFINFO_AUTO_STOP_GUARD(&piGuard);
}

static void mmBetweenSimCatchupBG(	MovementProcessingThread* t,
									MovementManager* mm)
{
	PerfInfoGuard* piGuard;

	PERFINFO_AUTO_START_FUNC_GUARD(&piGuard);
	mmBetweenSimCommonBG(t, mm);
	PERFINFO_AUTO_STOP_GUARD(&piGuard);
}

static void mmHandleHasDestroyBG(	MovementProcessingThread* t,
									MovementManager* mm)
{
	MovementThreadData* td = MM_THREADDATA_BG(mm);

	ASSERT_TRUE_AND_RESET(mm->bg.flagsMutable.hasPostStepDestroy);

	if(TRUE_THEN_RESET(mm->bg.flagsMutable.mmrIsDestroyedFromFG)){
		mmResourcesHandleDestroyedFromFG(mm, td);
	}
}

static void mmAllHandleHasDestroyListBG(void){
	if(!eaSize(&mgState.bg.hasPostStepDestroy.managers)){
		return;
	}
	
	PERFINFO_AUTO_START_FUNC();
	
	ASSERT_FALSE_AND_SET(mgState.bg.flagsMutable.gridIsWritable);
	ASSERT_FALSE_AND_SET(mgState.bg.hasPostStepDestroy.flags.managersIsReadOnly);

	mmProcessingThreadsProcess(	mgState.bg.hasPostStepDestroy.managers,
								eaSize(&mgState.bg.hasPostStepDestroy.managers),
								mmHandleHasDestroyBG);
	
	eaClearFast(&mgState.bg.hasPostStepDestroy.managersMutable);

	ASSERT_TRUE_AND_RESET(mgState.bg.hasPostStepDestroy.flags.managersIsReadOnly);
	ASSERT_TRUE_AND_RESET(mgState.bg.flagsMutable.gridIsWritable);

	PERFINFO_AUTO_STOP();
}

static void mmSendMsgsPostStepBG(MovementManager* mm){
	EARRAY_CONST_FOREACH_BEGIN(mm->bg.requesters, j, jsize);
	{
		MovementRequester*				mr = mm->bg.requesters[j];
		MovementRequesterMsgPrivateData pd;
		
		if(!TRUE_THEN_RESET(mr->bg.flagsMutable.needsPostStepMsg)){
			continue;
		}
		
		mrLog(mr, NULL, "Sending msg POST_STEP.");

		mmRequesterMsgInitBG(	&pd,
								NULL,
								mr,
								MR_MSG_BG_POST_STEP);

		mmRequesterMsgSend(&pd);
	}
	EARRAY_FOREACH_END;
}

static void mmAllHandlePostStepListBG(void){
	if(!mgState.bg.needsPostStep.melManagers.head){
		return;
	}
	
	PERFINFO_AUTO_START_FUNC();
	
	while(mgState.bg.needsPostStep.melManagers.head){
		MEL_FOREACH_BEGIN(iter, mgState.bg.needsPostStep.melManagers);
		{
			MovementManager*	mm;
			S32					mrNeedsPostStepMsg;
			S32					mrPipeNeedsPostStep;

			mm = MM_PARENT_FROM_MEMBER(	MovementManager,
										bg.execNodePostStep,
										iter.node);

			ASSERT_TRUE_AND_RESET(mm->bg.flagsMutable.inPostStepList);

			mmExecListRemove(&mgState.bg.needsPostStep.melManagers, iter.node);
			
			mrNeedsPostStepMsg = TRUE_THEN_RESET(mm->bg.flagsMutable.mrNeedsPostStepMsg);
			mrPipeNeedsPostStep = TRUE_THEN_RESET(mm->bg.flagsMutable.mrPipeNeedsPostStep);
			
			mmLog(	mm,
					NULL,
					"[bg.postStep] Starting post-step.");
			
			// Check if any requesters want a POST_STEP msg.
			
			if(mrNeedsPostStepMsg){
				mmSendMsgsPostStepBG(mm);
			}
			
			// Check if any pipes need to be destroyed.
			
			if(mrPipeNeedsPostStep){
				mmHandlePipePostStepBG(mm);
			}
		}
		MEL_FOREACH_END;
	}
		
	PERFINFO_AUTO_STOP();
}

static void mmAllBetweenSimBG(S32 catchup){
	PERFINFO_AUTO_START_FUNC();

	if(catchup){
		mmProcessingThreadsProcess(	mgState.bg.managers.client,
									eaSize(&mgState.bg.managers.client),
									mmBetweenSimCatchupBG);
	}else{
		mmProcessingThreadsProcess(	mgState.bg.managers.client,
									eaSize(&mgState.bg.managers.client),
									mmBetweenSimClientBG);

		mmProcessingThreadsProcess(	mgState.bg.managers.nonClient,
									eaSize(&mgState.bg.managers.nonClient),
									mmBetweenSimNonClientBG);
	}
	
	mmAllHandleHasDestroyListBG();
	mmAllHandlePostStepListBG();

	PERFINFO_AUTO_STOP();
}

static void mmHandleCreateFromFG(	MovementManager* mm,
									MovementThreadData* td)
{
	// New mm.
	
	U32 index = INDEX_FROM_REFERENCE(mm->entityRef);

	if(index < ARRAY_SIZE(mgState.bg.entIndexToManager)){
		mgState.bg.entIndexToManager[index] = mm;
	}else{
		if(!mgState.bg.stEntIndexToManager){
			mgState.bg.stEntIndexToManager = stashTableCreateInt(100);
		}
		
		stashIntAddPointer(	mgState.bg.stEntIndexToManager,
							mm->entityRef,
							mm,
							1);
	}	
	
	assert(!mm->bg.listIndex);
	if(mm->bg.flags.isAttachedToClient){
		mm->bg.listIndexMutable = eaPush(&mgState.bg.managers.clientMutable, mm);
	}else{
		mm->bg.listIndexMutable = eaPush(&mgState.bg.managers.nonClientMutable, mm);
	}

	if(	mgState.flags.logOnCreate
		&&
		(	mgState.debug.logOnCreate.radius <= 0.f ||
			distance3(mgState.debug.logOnCreate.pos, mm->fg.pos) <
				mgState.debug.logOnCreate.radius)
		)
	{
		mmSetDebugging(mm, 1);
	}
}

static void mmAllHandleManagerUpdatesFromFG(MovementGlobalStateThreadData* mgtd,
											MovementProcessingThread* t)
{
	if(!TRUE_THEN_RESET(mgtd->toBG.flags.hasUpdatedManagers)){
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	EARRAY_CONST_FOREACH_BEGIN(mgtd->toBG.updatedManagers, i, size);
	{
		MovementManager*	mm = mgtd->toBG.updatedManagers[i];
		MovementThreadData*	td = MM_THREADDATA_BG(mm);

		assert(!t->mm);
		t->mmMutable = mm;
		
		ASSERT_TRUE_AND_RESET(td->toBG.flagsMutable.inUpdatedList);

		if(FALSE_THEN_SET(mm->bg.flagsMutable.inList)){
			mmHandleCreateFromFG(mm, td);
		}

		if(td->toBG.flags.clientWasChanged){
			if(mm->bg.flags.isAttachedToClient != td->toBG.flags.isAttachedToClient){
				mm->bg.flagsMutable.isAttachedToClient = td->toBG.flags.isAttachedToClient;

				if(mm->bg.flags.isAttachedToClient){
					mmRemoveFromListBG(mm, &mgState.bg.managers.nonClientMutable);
					mm->bg.listIndexMutable = eaPush(&mgState.bg.managers.clientMutable, mm);
				}else{
					mmRemoveFromListBG(mm, &mgState.bg.managers.clientMutable);
					mm->bg.listIndexMutable = eaPush(&mgState.bg.managers.nonClientMutable, mm);
				}
			}
		}

		if(TRUE_THEN_RESET(td->toBG.flagsMutable.destroyed)){
			// Remove mm from bg.
			
			mmHandleDestroyedFromFG(mm, td);
		}

		assert(t->mm == mm);
		t->mmMutable = NULL;
	}
	EARRAY_FOREACH_END;
	
	if(eaSize(&mgtd->toBG.updatedManagers) > 100){
		eaDestroy(&mgtd->toBG.updatedManagers);
	}else{
		eaClearFast(&mgtd->toBG.updatedManagers);
	}
	
	PERFINFO_AUTO_STOP();
}

static void mmAllRunCatchupStepsBG(void){
	S32 doRunCurrentStep;
	S32 isLastStepOnThisFrame;

	if(subS32(	mgState.bg.pc.local.cur,
				mgState.bg.frame.cur.pcStart) >= 0)
	{
		return;
	}
	
	PERFINFO_AUTO_START_FUNC();

	mmGlobalLog("Running catchup steps.");

	doRunCurrentStep = mgState.bg.flags.doRunCurrentStep;
	isLastStepOnThisFrame = mgState.bg.flags.isLastStepOnThisFrame;
	
	mgState.bg.flagsMutable.doRunCurrentStep = 1;
	mgState.bg.flagsMutable.isLastStepOnThisFrame = 0;
	mgState.bg.flagsMutable.isCatchingUp = 1;

	while(subS32(	mgState.bg.pc.local.cur,
					mgState.bg.frame.cur.pcStart) < 0)
	{
		mmAllSetCurrentViewBG();

		mmAllBetweenSimBG(1);

		mgState.bg.pc.local.cur += MM_PROCESS_COUNTS_PER_STEP;
		mgState.bg.pc.server.cur += MM_PROCESS_COUNTS_PER_STEP;
		mgState.bg.pc.server.curView += MM_PROCESS_COUNTS_PER_STEP;
	}

	mgState.bg.flagsMutable.isCatchingUp = 0;
	mgState.bg.flagsMutable.doRunCurrentStep = doRunCurrentStep;
	mgState.bg.flagsMutable.isLastStepOnThisFrame = isLastStepOnThisFrame;

	mmGlobalLog("Done running catchup steps.");

	PERFINFO_AUTO_STOP();
}

static void mmAllRunCurrentStepBG(void){
	PERFINFO_AUTO_START_FUNC();

	if(mgState.bg.flags.doRunCurrentStep){
		mmAllSetCurrentViewBG();
	}

	mmAllBetweenSimBG(0);

	PERFINFO_AUTO_STOP();
}

static void mmAllGatherResultsFromThreadsBG(MovementGlobalStateThreadData* mgtd){
	EARRAY_CONST_FOREACH_BEGIN(mgState.bg.threads.threads, i, isize);
	{
		MovementProcessingThread* t = mgState.bg.threads.threads[i];

		if(	t->results &&
			eaSize(&t->results->managersAfterSimWakes))
		{
			eaPush(&mgtd->toFG.threadResults, t->results);
			t->results = NULL;
			mgtd->toFG.flags.hasThreadResults = 1;
		}
	}
	EARRAY_FOREACH_END;
}

void mmAllHandleBetweenSimBG(void){
	MovementGlobalStateThreadData*	mgtd = mgState.threadData + MM_BG_SLOT;
	MovementProcessingThread		t = {0};

	t.threadID = GetCurrentThreadId();

	TlsSetValue(mgState.bg.threads.tls.processingThread, &t);

	ASSERT_FALSE_AND_SET(mgState.bg.flagsMutable.threadIsBG);
	
	// Get mm updates, 
	
	mmAllHandleManagerUpdatesFromFG(mgtd, &t);
	
	// Determine if processing should be done on this step.

	mmAllUpdateProcessFlagsBG();

	// Run all the player catchups.

	mmAllRunCatchupStepsBG();
	
	// Run the current step.
	
	mmAllRunCurrentStepBG();

	// Clean up after processing.

	if(mgState.bg.flags.doRunCurrentStep){
		// We processed on this frame so increment the step and process counts.

		mgState.bg.frame.cur.stepCount.cur++;

		mgState.bg.pc.local.cur += MM_PROCESS_COUNTS_PER_STEP;
		mgState.bg.pc.server.cur += MM_PROCESS_COUNTS_PER_STEP;
		mgState.bg.pc.server.curView += MM_PROCESS_COUNTS_PER_STEP;
	}

	if(t.results){
		eaPush(&mgtd->toFG.threadResults, t.results);
		t.results = NULL;
		mgtd->toFG.flags.hasThreadResults = 1;
	}

	TlsSetValue(mgState.bg.threads.tls.processingThread, NULL);
	
	mmAllGatherResultsFromThreadsBG(mgtd);

	ASSERT_TRUE_AND_RESET(mgState.bg.flagsMutable.threadIsBG);
}

S32 mmIsRepredictingBG(void){
	return mgState.bg.flags.isRepredicting;
}

static S32 mmSendMsgDataReleaseRequestedBG(	MovementRequester* mr,
											U32 bit)
{
	MovementRequesterMsgPrivateData pd;
	MovementRequesterMsgOut			out;

	mmRequesterMsgInitBG(	&pd,
							&out,
							mr,
							MR_MSG_BG_DATA_RELEASE_REQUESTED);

	pd.in.bg.dataReleaseRequested.mr = mr;
	pd.in.bg.dataReleaseRequested.mrClassID = mr->mrc->id;

	pd.msg.in.bg.dataReleaseRequested.dataClassBits = bit;

	mmRequesterMsgSend(&pd);

	return !out.bg.dataReleaseRequested.denied;
}

S32 mrmRequesterCreateBG(	const MovementRequesterMsg* msg,
							MovementRequester** mrOut,
							const char* name,
							MovementRequesterMsgHandler msgHandler,
							U32 id)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementRequesterMsgType			msgType = SAFE_MEMBER(pd, msgType);
	MovementRequester*					mr;
	
	if(	!pd ||
		!MR_MSG_TYPE_IS_BG(msgType))
	{
		return 0;
	}

	if(	!mmRequesterCreateBasicByName(pd->mm, &mr, name) &&
		!mmRequesterCreate(pd->mm, &mr, NULL, msgHandler, id))
	{
		return 0;
	}
	
	mmSendMsgInitializeBG(pd->mm, mr);
	
	if(mrOut){
		*mrOut = mr;
	}
	
	return 1;
}

static void mmLogAcquireOwnershipFailure(	MovementManager* mm,
											MovementRequester* mr,
											U32	dataClassBits,
											U32 rejectedBit,
											MovementRequester* mrRejecter)
{
	const char*			nameRejected;
	char				bufferTried[100];
	U32					mdc = lowBitIndex(rejectedBit);
	MovementRequester*	mrOwner = mm->bg.dataOwner[mdc];
	
	mmGetDataClassName(&nameRejected, mdc);
	mmGetDataClassNames(SAFESTR(bufferTried), dataClassBits);

	mrLog(	mr,
			NULL,
			"Rejected acquire %s/(%s) from current owner %s[%u].",
			nameRejected,
			bufferTried,
			mrOwner ? mrOwner->mrc->name : "none",
			mrOwner ? mrOwner->handle : 0);

	if(mrRejecter){
		char buffer[100];
		
		mmRequesterGetNameAndHandleBG(	mrRejecter,
										SAFESTR(buffer));

		mrLog(	mr,
				NULL,
				"Rejecter: %s",
				buffer);
	}else{
		mrLog(	mr,
				NULL,
				"Rejecter: Resolver");
	}
}

static void mmLogAcquireOwnershipSuccess(	MovementManager* mm,
											MovementRequester* mr,
											U32	dataClassBits)
{
	char buffer[100];
	
	mmGetDataClassNames(SAFESTR(buffer), dataClassBits);
	
	mrLog(	mr,
			NULL,
			"Successful acquire: %s",
			buffer);
}

S32 mrmIsAttachedToClientBG(const MovementRequesterMsg* msg){
	MovementRequesterMsgPrivateData* pd = MR_MSG_TO_PD(msg);
	
	if(!MR_MSG_TYPE_IS_BG(pd->msgType)){
		return 0;
	}
	
	return SAFE_MEMBER(pd, mm->bg.flags.isAttachedToClient);
}

S32 mrmAcquireDataOwnershipBG(	const MovementRequesterMsg* msg,
								U32 dataClassBits,
								S32 needAllBits,
								U32* acquiredBitsOut,
								U32* ownedBitsOut)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;
	MovementRequester*					mr;

	if(	!pd
		||
		pd->msgType != MR_MSG_BG_DISCUSS_DATA_OWNERSHIP
		||
		pd->mm->bg.flags.isAttachedToClient &&
		!pd->mr->mrc->flags.syncToClient
		||
		!dataClassBits)
	{
		return 0;
	}

	mr = pd->mr;
	
	// Mask dataClassBits to be only bits that I don't own already.
	
	dataClassBits &= MDC_BITS_ALL & ~mr->bg.ownedDataClassBits;
	
	if(acquiredBitsOut){
		*acquiredBitsOut = 0;
	}

	if(ownedBitsOut){
		*ownedBitsOut = mr->bg.ownedDataClassBits;
	}

	if(!dataClassBits){
		// I already own all these bits.
		
		return 1;
	}

	if(	mr->bg.flags.destroyed ||
		mr->bg.flags.repredictNotCreatedYet)
	{
		assert(!mr->bg.ownedDataClassBits);

		return 0;
	}

	mm = mr->mm;

	// Ask the current owners to release their data.

	ARRAY_FOREACH_BEGIN(mm->bg.dataOwner, i);
	{
		MovementRequester*	mrOwner = mm->bg.dataOwner[i];
		U32					bit = BIT(i);

		if(	dataClassBits & bit &&
			mrOwner)
		{
			MovementConflictResolution resolution = MCR_ASK_OWNER;

			assert(mrOwner != mr);

			if(mgState.cb.conflictResolver){
				MovementOwnershipConflict conflict = {0};

				conflict.in.mrOwner = mrOwner;
				conflict.in.mrOwnerClassID = mrOwner->mrc->id;
				conflict.in.mrRequester = mr;
				conflict.in.mrRequesterClassID = mr->mrc->id;
				conflict.in.dataClassBits = bit;

				mgState.cb.conflictResolver(&conflict);

				resolution = conflict.out.resolution;

				switch(resolution){
					xcase MCR_RELEASE_ALLOWED:{
						continue;
					}
					xcase MCR_RELEASE_DENIED:{
						if(MMLOG_IS_ENABLED(mm)){
							mmLogAcquireOwnershipFailure(mm, mr, dataClassBits, bit, NULL);
						}
					}
				}
			}

			if(	resolution == MCR_ASK_OWNER &&
				!mmSendMsgDataReleaseRequestedBG(mrOwner, bit))
			{
				resolution = MCR_RELEASE_DENIED;

				if(MMLOG_IS_ENABLED(mm)){
					mmLogAcquireOwnershipFailure(mm, mr, dataClassBits, bit, mrOwner);
				}
			}

			if(resolution == MCR_RELEASE_DENIED){
				if(needAllBits){
					return 0;
				}

				dataClassBits &= ~bit;
			}
		}
	}
	ARRAY_FOREACH_END;

	if(!dataClassBits){
		// All bits were denied.
		
		return 0;
	}

	// One or more weren't denied, so tell the current owners that their data is released.

	if(MMLOG_IS_ENABLED(mm)){
		mmLogAcquireOwnershipSuccess(mm, mr, dataClassBits);
	}

	ARRAY_FOREACH_BEGIN(mm->bg.dataOwner, i);
	{
		MovementRequester*	mrOwner = mm->bg.dataOwner[i];
		U32					mdcBit = BIT(i);

		if(dataClassBits & mdcBit){
			mm->bg.dataOwnerMutable[i] = mr;

			if(	i == MDC_ANIMATION &&
				gConf.bNewAnimationSystem)
			{
				mm->bg.flagsMutable.animOwnershipWasReleased = 1;
			}
			
			// Toggle the bit for whether I handle this message or not.
			
			if(mr->bg.handledMsgs & (MR_HANDLED_MSG_OUTPUT_POSITION_TARGET << i)){
				mm->bg.dataOwnerEnabledMaskMutable |= mdcBit;
			}else{
				mm->bg.dataOwnerEnabledMaskMutable &= ~mdcBit;
			}

			if(	!mr->bg.ownedDataClassBits &&
				mr->bg.handledMsgs & MR_HANDLED_MSG_BEFORE_DISCUSSION)
			{
				mmRequesterAddToMsgBeforeDiscussionBG(mm, mr);
			}

			mr->bg.flagsMutable.bgUnchangedSinceCopyToFG = 0;
			ASSERT_FALSE_AND_SET_BITS(mr->bg.ownedDataClassBitsMutable, mdcBit);
			
			if(acquiredBitsOut){
				*acquiredBitsOut |= mdcBit;
			}

			if(mrOwner){
				mrOwner->bg.flagsMutable.bgUnchangedSinceCopyToFG = 0;
				ASSERT_TRUE_AND_RESET_BITS(mrOwner->bg.ownedDataClassBitsMutable, mdcBit);

				if(	!mrOwner->bg.ownedDataClassBits &&
					mrOwner->bg.handledMsgs & MR_HANDLED_MSG_BEFORE_DISCUSSION)
				{
					mmRequesterRemoveFromMsgBeforeDiscussionBG(mm, mrOwner);
				}

				mmRequesterSendMsgDataWasReleasedBG(mrOwner, mdcBit, "taken");
			}
		}
	}
	ARRAY_FOREACH_END;

	if(ownedBitsOut){
		*ownedBitsOut = mr->bg.ownedDataClassBits;
	}

	return 1;
}

static S32 mmReleaseDataOwnershipInternalBG(MovementRequester* mr,
											U32 mdcBits,
											const char* reason)
{
	MovementManager*	mm;
	U32					bitsThatMatched = 0;
	
	if(	!mr ||
		!(mr->bg.ownedDataClassBits & mdcBits))
	{
		return 0;
	}

	mm = mr->mm;

	// Release the data and notify myself (for good measure, and in case the bits don't match).

	ARRAY_FOREACH_BEGIN(mm->bg.dataOwner, i);
	{
		MovementRequester*	mrOwner = mm->bg.dataOwner[i];
		U32					bit = BIT(i);

		if(	mdcBits & bit &&
			mrOwner == mr)
		{
			mm->bg.dataOwnerMutable[i] = NULL;
			
			if(	i == MDC_ANIMATION &&
				gConf.bNewAnimationSystem)
			{
				mm->bg.flagsMutable.animOwnershipWasReleased = 1;
			}

			mr->bg.flagsMutable.bgUnchangedSinceCopyToFG = 0;
			ASSERT_TRUE_AND_RESET_BITS(mr->bg.ownedDataClassBitsMutable, bit);

			if(	!mr->bg.ownedDataClassBits &&
				mr->bg.handledMsgs & MR_HANDLED_MSG_BEFORE_DISCUSSION)
			{
				mmRequesterRemoveFromMsgBeforeDiscussionBG(mm, mr);
			}

			bitsThatMatched |= bit;
		}
	}
	ARRAY_FOREACH_END;

	if(bitsThatMatched){
		mmRequesterSendMsgDataWasReleasedBG(mr, bitsThatMatched, reason);
	}

	return !!bitsThatMatched;
}

static void mrLogReleasedDataClassBits(	MovementRequester* mr,
										U32 mdcBits)
{
	char bitNamesReleased[200];
	char bitNamesOwned[200];
	char bitNamesNew[200];
	
	mmGetDataClassNames(SAFESTR(bitNamesReleased), mdcBits);
	mmGetDataClassNames(SAFESTR(bitNamesOwned), mr->bg.ownedDataClassBits);
	mmGetDataClassNames(SAFESTR(bitNamesNew), mr->bg.ownedDataClassBits & ~mdcBits);
	
	mrLog(	mr,
			NULL,
			"Intentionally releasing: %s.\n"
			"Currently own: %s\n"
			"Will own: %s",
			bitNamesReleased,
			bitNamesOwned,
			bitNamesNew);
}

S32 mrmReleaseDataOwnershipBG(	const MovementRequesterMsg* msg,
								U32 mdcBits)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementRequesterMsgType			msgType = SAFE_MEMBER(pd, msgType);
	MovementRequester*					mr;

	if(	!mdcBits
		||
		msgType != MR_MSG_BG_DISCUSS_DATA_OWNERSHIP &&
		msgType != MR_MSG_BG_BEFORE_DISCUSSION &&
		msgType != MR_MSG_BG_CREATE_OUTPUT &&
		msgType != MR_MSG_BG_DATA_WAS_RELEASED &&
		msgType != MR_MSG_BG_UPDATED_SYNC &&
		msgType != MR_MSG_BG_UPDATED_TOBG &&
		msgType != MR_MSG_BG_INPUT_EVENT)
	{
		return 0;
	}
	
	mr = pd->mr;
	
	if(MRLOG_IS_ENABLED(mr)){
		mrLogReleasedDataClassBits(mr, mdcBits);
	}

	return mmReleaseDataOwnershipInternalBG(mr, mdcBits, "self");
}

S32 mrmReleaseAllDataOwnershipBG(const MovementRequesterMsg* msg){
	return mrmReleaseDataOwnershipBG(msg, MDC_BITS_ALL);
}

static S32 mmSendMsgDetailAnimBitRequestedBG(MovementRequester* mr){
	MovementRequester* mrOwner = mr->mm->bg.dataOwner[MDC_ANIMATION];

	if(	mrOwner &&
		mrOwner != mr)
	{
		MovementRequesterMsgPrivateData pd;
		MovementRequesterMsgOut			out;

		mmRequesterMsgInitBG(	&pd,
								&out,
								mrOwner,
								MR_MSG_BG_DETAIL_ANIM_BIT_REQUESTED);

		pd.msg.in.bg.detailAnimBitRequested.mrOther = mr;

		mmRequesterMsgSend(&pd);

		return !out.bg.detailAnimBitRequested.denied;
	}

	return 1;
}

S32 mrmAnimAddBitBG(const MovementRequesterMsg* msg,
					U32 bitHandle)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementOutput*						o;
	MovementRegisteredAnimBit*			bit = NULL;

	if(	!bitHandle
		||
		!pd
		||
		pd->msgType != MR_MSG_BG_CREATE_DETAILS &&
		!(	pd->msgType == MR_MSG_BG_CREATE_OUTPUT &&
			pd->in.bg.createOutput.dataClassBit == MDC_BIT_ANIMATION)
		||
		gConf.bNewAnimationSystem)
	{
		return 0;
	}
	
	PERFINFO_AUTO_START_FUNC();

	assert(bitHandle < mgState.animBitRegistry.bitCount);
	
	if(pd->msgType == MR_MSG_BG_CREATE_OUTPUT){
		// Normal anim bits from owner.

		if(	MMLOG_IS_ENABLED(pd->mm) &&
			mmRegisteredAnimBitGetByHandle(&mgState.animBitRegistry, &bit, bitHandle))
		{
			mrLog(	pd->mr,
					NULL,
					"Added normal %s anim bit: \"%s\"",
					bit->flags.isFlashBit ? "flash" : "steady",
					bit->bitName);
		}
	}
	else if(!mmSendMsgDetailAnimBitRequestedBG(pd->mr)){
		// Animation owner rejected request.
		
		if(	MMLOG_IS_ENABLED(pd->mm) &&
			mmRegisteredAnimBitGetByHandle(&mgState.animBitRegistry, &bit, bitHandle))
		{
			mrLog(	pd->mr,
					NULL,
					"Rejected detail %s anim bit: \"%s\"",
					bit->flags.isFlashBit ? "flash" : "steady",
					bit->bitName);
		}
					
		PERFINFO_AUTO_STOP();

		return 0;
	}
	else if(MMLOG_IS_ENABLED(pd->mm) &&
			mmRegisteredAnimBitGetByHandle(&mgState.animBitRegistry, &bit, bitHandle))
	{
		mrLog(	pd->mr,
				NULL,
				"Added detail %s anim bit: \"%s\"",
				bit->flags.isFlashBit ? "flash" : "steady",
				bit->bitName);
	}

	o = pd->o;

	if(!o->data.anim.values){
		o->dataMutable.anim.values = eaPop(&pd->mm->bg.available.animValuesMutable);
	}

	o->flagsMutable.addedAnimValue = 1;

	eaiPushUnique(	&o->dataMutable.anim.values,
					bitHandle);

	PERFINFO_AUTO_STOP();
	
	return 1;
}

S32 mrmAnimStanceCreateBG(	const MovementRequesterMsg* msg,
							U32* handleOut,
							U32 animBitHandle)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;
	MovementRequester*					mr;
	MovementRequesterStance*			s;
	S32									index;

	PERFINFO_AUTO_START_FUNC();

	if(	!pd ||
		!handleOut ||
		*handleOut ||
		!MR_MSG_TYPE_IS_BG(pd->msgType) ||
		animBitHandle >= mgState.animBitRegistry.bitCount ||
		!gConf.bNewAnimationSystem)
	{
		PERFINFO_AUTO_STOP();
		return 0;
	}

	mm = pd->mm;
	mr = pd->mr;

	s = callocStruct(MovementRequesterStance);
	
	s->mr = mr;
	s->animBitHandle = animBitHandle;
	s->isPredicted = mm->bg.flags.isPredicting;
	
	// Find a handle or just push on the end.
	
	index = eaFind(&mr->bg.stances, NULL);

	if(index < 0){
		index = eaPush(&mr->bg.stancesMutable, s);
	}else{
		mr->bg.stancesMutable[index] = s;
	}
	
	*handleOut = index + 1;
	
	// Add to mm.
	
	eaPush(&mm->bg.stancesMutable, s);
	
	mm->bg.flagsMutable.animStancesChanged = 1;
	
	if (gConf.bNewAnimationSystem && pd->mm) {
		Entity *e = entFromEntityRefAnyPartition(pd->mm->entityRef);
		if (e && e->mm.mrEmoteSetHandle && e->mm.mrEmote != pd->mr)
			mrEmoteSetDestroy(e->mm.mrEmote, &e->mm.mrEmoteSetHandle);
	}

	if(MMLOG_IS_ENABLED(mm)){
		MovementRegisteredAnimBit* bit;

		if(mmRegisteredAnimBitGetByHandle(	&mgState.animBitRegistry,
											&bit,
											animBitHandle))
		{
			mrLog(	mr,
					NULL,
					"Created %sstance handle %u: %s(%u)",
					s->isPredicted ? "predicted " : "",
					*handleOut,
					bit->bitName,
					animBitHandle);
			
			mmLogStancesBG(mm);
		}
	}

	PERFINFO_AUTO_STOP();

	return 1;
}

S32 mrmAnimStanceDestroyBG(	const MovementRequesterMsg* msg,
							U32* handleInOut)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	U32									index;
	S32									result;

	PERFINFO_AUTO_START_FUNC();

	if(	!pd ||
		!handleInOut ||
		!MR_MSG_TYPE_IS_BG(pd->msgType) ||
		!gConf.bNewAnimationSystem)
	{
		PERFINFO_AUTO_STOP();
		return 0;
	}
	
	index = *handleInOut - 1;
	*handleInOut = 0;

	result = mmRequesterDestroyStanceByIndexBG(pd->mm, pd->mr, index);

	if (gConf.bNewAnimationSystem && pd->mm) {
		Entity *e = entFromEntityRefAnyPartition(pd->mm->entityRef);
		if (e && e->mm.mrEmoteSetHandle && e->mm.mrEmote != pd->mr)
			mrEmoteSetDestroy(e->mm.mrEmote, &e->mm.mrEmoteSetHandle);
	}

	PERFINFO_AUTO_STOP();

	return result;
}

S32 mrmAnimStanceDestroyPredictedBG(const MovementRequesterMsg* msg)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	S32									result = 1;

	PERFINFO_AUTO_START_FUNC();

	if(	!pd ||
		!MR_MSG_TYPE_IS_BG(pd->msgType) ||
		!gConf.bNewAnimationSystem)
	{
		PERFINFO_AUTO_STOP();
		return 0;
	}

	EARRAY_CONST_FOREACH_BEGIN(pd->mr->bg.stances, i, isize);
	{
		if (pd->mr->bg.stances[i]->isPredicted)
		{
			S32 localResult = mmRequesterDestroyStanceByIndexBG(pd->mm, pd->mr, i);
			if (localResult) {
				i--;
				isize--;
			}
			result |= localResult;
		}
	}
	FOR_EACH_END;

	PERFINFO_AUTO_STOP();

	return result;
}

S32 mrmAnimResetGroundSpawnBG(	const MovementRequesterMsg *msg)
{
	MovementRequesterMsgPrivateData *pd = MR_MSG_TO_PD(msg);
	if (gConf.bNewAnimationSystem && pd->mm) {
		mrSurfaceSetSpawnedOnGround(pd->mr, false);
	}
	return 1;
}

S32 mrmAnimStartBG(	const MovementRequesterMsg* msg,
					U32 bitHandle,
					U32 id)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementOutput*						o;
	MovementRegisteredAnimBit*			bit = NULL;
	S32									success = 1;

	if(	!bitHandle
		||
		!pd
		||
		pd->msgType != MR_MSG_BG_CREATE_DETAILS &&
		!(	pd->msgType == MR_MSG_BG_CREATE_OUTPUT &&
			pd->in.bg.createOutput.dataClassBit == MDC_BIT_ANIMATION)
		||
		!gConf.bNewAnimationSystem)
	{
		return 0;
	}
	
	PERFINFO_AUTO_START_FUNC();

	assert(bitHandle < mgState.animBitRegistry.bitCount);
	
	o = pd->o;

	if(pd->msgType == MR_MSG_BG_CREATE_OUTPUT){
		MovementManager*	mm = pd->mm;
		MovementThreadData* td = MM_THREADDATA_BG(mm);

		// Normal anim bits from owner.

		if(	MMLOG_IS_ENABLED(pd->mm) &&
			mmRegisteredAnimBitGetByHandle(&mgState.animBitRegistry, &bit, bitHandle))
		{
			mrLog(	pd->mr,
					NULL,
					"Started normal anim: \"%s\"",
					bit->bitName);
		}

		{
			S32 found = 0;

			EARRAY_INT_CONST_FOREACH_BEGIN(o->data.anim.values, i, isize);
			{
				switch(MM_ANIM_VALUE_GET_TYPE(o->data.anim.values[i])){
					xcase MAVT_LASTANIM_ANIM:{
						// Skip PC.
						i++;
					}
					xcase MAVT_ANIM_TO_START:{
						// Replace with the new value.
						ASSERT_FALSE_AND_SET(found);
						o->dataMutable.anim.values[i] = MM_ANIM_VALUE(MM_ANIM_HANDLE_WITH_ID(bitHandle,id), MAVT_ANIM_TO_START);
					}
					xcase MAVT_FLAG:{
						eaiRemove(&o->dataMutable.anim.values, i);
						i--;
						isize--;
					}
				}
			}
			EARRAY_FOREACH_END;

			if(!found)
			{
#if MM_DEBUG_PRINTANIMWORDS
				mmRegisteredAnimBitGetByHandle(&mgState.animBitRegistry, &bit, bitHandle);
				printfColor(COLOR_GREEN, "%s ", __FUNCTION__);
				printfColor(COLOR_GREEN|COLOR_BRIGHT, "%s (%u) ", bit->bitName, id);
				printfColor(COLOR_BRIGHT, "%u : %u = %u + %d\n",
							MM_ANIM_HANDLE_WITH_ID(bitHandle,id),
							mgState.bg.pc.local.cur + mgState.fg.netReceive.cur.offset.clientToServerSync,
							mgState.bg.pc.local.cur,
							mgState.fg.netReceive.cur.offset.clientToServerSync);
#endif
				mmOutputAddAnimValueBG(	mm,
										o,
										MM_ANIM_HANDLE_WITH_ID(bitHandle,id),
										MAVT_ANIM_TO_START);
			}
		}

		mm->bg.flagsMutable.animToStartIsSet = 1;
		mm->bg.flagsMutable.animFlagIsSet = 0;
		mm->bg.flagsMutable.animOwnershipWasReleased = 0;
	}
	else if(mmSendMsgDetailAnimBitRequestedBG(pd->mr)){
		// Animation owner didn't deny request.

		if(	MMLOG_IS_ENABLED(pd->mm) &&
			mmRegisteredAnimBitGetByHandle(&mgState.animBitRegistry, &bit, bitHandle))
		{
			mrLog(	pd->mr,
					NULL,
					"Started detail anim: \"%s\"",
					bit->bitName);
		}

#if MM_DEBUG_PRINTANIMWORDS
		mmRegisteredAnimBitGetByHandle(&mgState.animBitRegistry, &bit, bitHandle);
		printfColor(COLOR_GREEN, "%s (DETAIL) ", __FUNCTION__);
		printfColor(COLOR_GREEN|COLOR_BRIGHT, "%s (%u)\n", bit->bitName, id);
#endif
		mmOutputAddAnimValueBG(	pd->mm,
								o,
								MM_ANIM_HANDLE_WITH_ID(bitHandle,id),
								MAVT_DETAIL_ANIM_TO_START);
	}else{
		// Animation owner denied request.
		
		if(	MMLOG_IS_ENABLED(pd->mm) &&
			mmRegisteredAnimBitGetByHandle(&mgState.animBitRegistry, &bit, bitHandle))
		{
			mrLog(	pd->mr,
					NULL,
					"Rejected detail anim: \"%s\"",
					bit->bitName);
		}
					
		success = 0;
	}

	if (gConf.bNewAnimationSystem && pd->mm) {
		Entity *e = entFromEntityRefAnyPartition(pd->mm->entityRef);
		if (e && e->mm.mrEmoteSetHandle && e->mm.mrEmote != pd->mr)
			mrEmoteSetDestroy(e->mm.mrEmote, &e->mm.mrEmoteSetHandle);
	}

	PERFINFO_AUTO_STOP();
	
	return success;
}

S32 mrmAnimPlayFlagBG(	const MovementRequesterMsg* msg,
						U32 bitHandle,
						U32 id)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;
	MovementOutput*						o;
	MovementRegisteredAnimBit*			bit = NULL;

	if(	!bitHandle
		||
		!pd
		||
		!(	pd->msgType == MR_MSG_BG_CREATE_OUTPUT &&
			pd->in.bg.createOutput.dataClassBit == MDC_BIT_ANIMATION)
		||
		!gConf.bNewAnimationSystem)
	{
		return 0;
	}
	
	PERFINFO_AUTO_START_FUNC();

	assert(bitHandle < mgState.animBitRegistry.bitCount);
	
	o = pd->o;
	mm = pd->mm;
	
	if(	mm->bg.flags.animOwnershipWasReleased
		||
		!mm->bg.flags.animToStartIsSet &&
		(	!mm->bg.lastAnim.anim ||
			mm->bg.lastAnim.anim == mgState.animBitHandle.animOwnershipReleased))
	{
#if MM_DEBUG_PRINTANIMWORDS
		mmRegisteredAnimBitGetByHandle(&mgState.animBitRegistry, &bit, bitHandle);
		printfColor(COLOR_GREEN, "%s (DETAIL) ", __FUNCTION__);
		printfColor(COLOR_GREEN|COLOR_BRIGHT, "%s (%u)\n", bit->bitName, id);
#endif
		mmOutputAddAnimValueBG(	mm,
								o,
								MM_ANIM_HANDLE_WITH_ID(bitHandle,id),
								MAVT_DETAIL_FLAG);
		PERFINFO_AUTO_STOP();
		return 0;
	}

	if(	MMLOG_IS_ENABLED(mm) &&
		mmRegisteredAnimBitGetByHandle(&mgState.animBitRegistry, &bit, bitHandle))
	{
		mrLog(	pd->mr,
				NULL,
				"Played anim flag: \"%s\"",
				bit->bitName);
	}

#if MM_DEBUG_PRINTANIMWORDS
	mmRegisteredAnimBitGetByHandle(&mgState.animBitRegistry, &bit, bitHandle);
	printfColor(COLOR_GREEN, "%s ", __FUNCTION__);
	printfColor(COLOR_GREEN|COLOR_BRIGHT, "%s (%u)\n", bit->bitName, id);
#endif
	mmOutputAddAnimValueBG(	mm,
							o,
							MM_ANIM_HANDLE_WITH_ID(bitHandle,id),
							MAVT_FLAG);

	if(!mm->bg.flags.animToStartIsSet){
		mm->bg.flagsMutable.animFlagIsSet = 1;
	}

	PERFINFO_AUTO_STOP();
	return 1;
}

S32 mrmAnimPlayForcedDetailFlagBG(	const MovementRequesterMsg* msg,
									U32 bitHandle,
									U32 id)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;
	MovementOutput*						o;
	MovementRegisteredAnimBit*			bit = NULL;

	if(	!bitHandle
		||
		!pd
		||
		pd->msgType != MR_MSG_BG_CREATE_DETAILS &&
		!(	pd->msgType == MR_MSG_BG_CREATE_OUTPUT &&
			pd->in.bg.createOutput.dataClassBit == MDC_BIT_ANIMATION)
		||
		!gConf.bNewAnimationSystem)
	{
		return 0;
	}

	PERFINFO_AUTO_START_FUNC();

	assert(bitHandle < mgState.animBitRegistry.bitCount);

	o = pd->o;
	mm = pd->mm;

	if(	MMLOG_IS_ENABLED(mm) &&
		mmRegisteredAnimBitGetByHandle(&mgState.animBitRegistry, &bit, bitHandle))
	{
		mrLog(	pd->mr,
				NULL,
				"Played forced detail anim flag: \"%s\"",
				bit->bitName);
	}

#if MM_DEBUG_PRINTANIMWORDS
	mmRegisteredAnimBitGetByHandle(&mgState.animBitRegistry, &bit, bitHandle);
	printfColor(COLOR_GREEN, "%s ", __FUNCTION__);
	printfColor(COLOR_GREEN|COLOR_BRIGHT, "%s (%u)\n", bit->bitName, id);
#endif
	mmOutputAddAnimValueBG(	mm,
							o,
							MM_ANIM_HANDLE_WITH_ID(bitHandle,id),
							MAVT_DETAIL_FLAG);

	PERFINFO_AUTO_STOP();
	return 1;
}

S32 mrmEnableMsgUpdatedToFG(const MovementRequesterMsg* msg){
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementRequesterThreadData*		mrtd;
	MovementManager*					mm;
	MovementThreadData*					td;

	if(	!pd ||
		!MR_MSG_TYPE_IS_BG(pd->msgType))
	{
		return 0;
	}

	mm = pd->mm;
	td = MM_THREADDATA_BG(mm);
	MM_TD_SET_HAS_TOFG(mm, td);
	td->toFG.flagsMutable.mrHasUserToFG = 1;
	
	mrtd = MR_THREADDATA_BG(pd->mr);
	mrtd->toFG.flagsMutable.hasUserToFG = 1;

	mrLog(	pd->mr,
			NULL,
			"Enabling toFG.");

	return 1;
}

static void mrLogHandledMsgsChangeBG(	MovementRequester* mr,
										const char* verbPrefix,
										U32 handledMsgsChanged)
{
	char handledMsgsChangedNames[200];
	char handledMsgsNowNames[200];
	
	mmGetHandledMsgsNames(	SAFESTR(handledMsgsChangedNames),
							handledMsgsChanged);
						
	mmGetHandledMsgsNames(	SAFESTR(handledMsgsNowNames),
							mr->bg.handledMsgs);
	
	mrLog(	mr,
			NULL,
			"%s handled msgs %s.\n"
			"New handled msgs: %s.",
			verbPrefix,
			handledMsgsChangedNames,
			handledMsgsNowNames);
}

S32 mrmHandledMsgsSetBG(const MovementRequesterMsg* msg,
						U32 handledMsgs)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementRequester*					mr;

	if(	!pd ||
		!MR_MSG_TYPE_IS_BG(pd->msgType))
	{
		return 0;
	}
	
	mr = pd->mr;

	mmRequesterSetHandledMsgsBG(pd->mm,
								mr,
								handledMsgs & MR_HANDLED_MSGS_ALL);
	
	if(MMLOG_IS_ENABLED(pd->mm)){
		mrLogHandledMsgsChangeBG(mr, "Set", handledMsgs);
	}

	return 1;
}

S32 mrmHandledMsgsAddBG(const MovementRequesterMsg* msg,
						U32 handledMsgs)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementRequester*					mr;

	if(	!pd ||
		!MR_MSG_TYPE_IS_BG(pd->msgType))
	{
		return 0;
	}

	mr = pd->mr;
	
	handledMsgs &= MR_HANDLED_MSGS_ALL;
	
	if(~mr->bg.handledMsgs & handledMsgs){
		mmRequesterSetHandledMsgsBG(pd->mm,
									mr,
									mr->bg.handledMsgs | handledMsgs);
	}
	
	if(MMLOG_IS_ENABLED(pd->mm)){
		mrLogHandledMsgsChangeBG(mr, "Added", handledMsgs);
	}

	return 1;
}

S32 mrmHandledMsgsRemoveBG(	const MovementRequesterMsg* msg,
							U32 handledMsgs)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementRequester*					mr;

	if(	!pd ||
		!MR_MSG_TYPE_IS_BG(pd->msgType))
	{
		return 0;
	}
	
	mr = pd->mr;
	
	handledMsgs &= MR_HANDLED_MSGS_ALL;
	
	if(mr->bg.handledMsgs & handledMsgs){
		mmRequesterSetHandledMsgsBG(pd->mm, mr, mr->bg.handledMsgs & ~handledMsgs);
	}
	
	if(MMLOG_IS_ENABLED(pd->mm)){
		mrLogHandledMsgsChangeBG(mr, "Removed", handledMsgs);
	}
	
	return 1;
}

static S32 mmGetByEntityRefBG(	EntityRef er,
								MovementManager** mmOut)
{
	MovementManager*	mm;
	U32					index;

	index = INDEX_FROM_REFERENCE(er);

	if(index < ARRAY_SIZE(mgState.bg.entIndexToManager)){
		mm = mgState.bg.entIndexToManager[index];

		if(	!mm ||
			mm->entityRef != er)
		{
			return 0;
		}
	}
	else if(!stashIntFindPointer(mgState.bg.stEntIndexToManager, er, &mm)){
		return 0;
	}else{
		assert(mm->flags.isLocal);
		assert(mm->entityRef == er);
	}

	*mmOut = mm;

	return 1;
}

static S32 mrmPrivateGetManagerByEntityRefBG(	MovementRequesterMsgPrivateData* pd,
												EntityRef er,
												MovementManager** mmOut)
{
	if(	!pd ||
		!er ||
		!mmOut ||
		!MR_MSG_TYPE_IS_BG(pd->msgType))
	{
		return 0;
	}

	return mmGetByEntityRefBG(er, mmOut);
}

static S32 mrmGetManagerFromEntityRefBG(const MovementRequesterMsg* msg,
										EntityRef er,
										MovementManager** mmOut)
{
	return mrmPrivateGetManagerByEntityRefBG(	MR_MSG_TO_PD(msg),
												er,
												mmOut);
}

static S32 mmShouldUsePastBG(MovementManager* mm){
	if(!mgState.flags.isServer){
		return 1;
	}else{
		return mm->bg.flags.isAttachedToClient;
	}
}

S32 mrmGetInputValueBitBG(	const MovementRequesterMsg* msg,
							MovementInputValueIndex mivi)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	const U32							valueIndex = mivi - MIVI_BIT_LOW;
	
	if(	!pd ||
		!pd->mm->bg.miState ||
		!MR_MSG_TYPE_IS_BG(pd->msgType) ||
		valueIndex >= MIVI_BIT_COUNT)
	{
		return 0;
	}
	
	return pd->mm->bg.miState->bit[valueIndex];
}

S32 mrmGetInputValueBitDiffBG(	const MovementRequesterMsg* msg,
								MovementInputValueIndex indexOne,
								MovementInputValueIndex indexNegativeOne)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	const U32							valueIndexOne = indexOne - MIVI_BIT_LOW;
	const U32							valueIndexNegativeOne = indexNegativeOne - MIVI_BIT_LOW;
	
	if(	!pd ||
		!pd->mm->bg.miState ||
		!MR_MSG_TYPE_IS_BG(pd->msgType) ||
		valueIndexOne >= MIVI_BIT_COUNT ||
		valueIndexNegativeOne >= MIVI_BIT_COUNT)
	{
		return 0;
	}
	
	return	pd->mm->bg.miState->bit[valueIndexOne] -
			pd->mm->bg.miState->bit[valueIndexNegativeOne];
}

F32 mrmGetInputValueF32BG(	const MovementRequesterMsg* msg,
							MovementInputValueIndex mivi)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	const U32							valueIndex = mivi - MIVI_F32_LOW;
	
	if(	!pd ||
		!pd->mm->bg.miState ||
		!MR_MSG_TYPE_IS_BG(pd->msgType) ||
		valueIndex >= MIVI_F32_COUNT)
	{
		return 0;
	}
	
	return pd->mm->bg.miState->f32[valueIndex];
}

S32 mrmGetEntityPositionAndRotationBG(	const MovementRequesterMsg* msg,
										EntityRef er,
										Vec3 posOut,
										Quat rotOut)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(!mrmPrivateGetManagerByEntityRefBG(pd, er, &mm)){
		return 0;
	}
	
	if(mmShouldUsePastBG(pd->mm)){
		if(posOut){
			copyVec3(mm->bg.past.pos, posOut);
		}

		if(rotOut){
			copyQuat(mm->bg.past.rot, rotOut);
		}
	}else{
		if(posOut){
			copyVec3(mm->bg.pos, posOut);
		}

		if(rotOut){
			copyQuat(mm->bg.rot, rotOut);
		}
	}

	return 1;
}

S32 mrmGetEntityPositionBG(	const MovementRequesterMsg* msg,
							EntityRef er,
							Vec3 posOut)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(	!mrmPrivateGetManagerByEntityRefBG(pd, er, &mm) ||
		!posOut)
	{
		return 0;
	}

	if(mmShouldUsePastBG(pd->mm)){
		copyVec3(mm->bg.past.pos, posOut);
	}
	else if(mm->bg.outputList.tail){
		copyVec3(mm->bg.outputList.tail->data.pos, posOut);
	}else{
		copyVec3(mm->bg.pos, posOut);
	}

	return 1;
}

S32 mrmGetEntityRotationBG(	const MovementRequesterMsg* msg,
							EntityRef er,
							Quat rotOut)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(	!mrmPrivateGetManagerByEntityRefBG(pd, er, &mm) ||
		!rotOut)
	{
		return 0;
	}

	if(mmShouldUsePastBG(pd->mm)){
		copyQuat(mm->bg.past.rot, rotOut);
	}
	else if(mm->bg.outputList.tail){
		copyQuat(mm->bg.outputList.tail->data.rot, rotOut);
	}else{
		copyQuat(mm->bg.rot, rotOut);
	}

	return 1;
}

S32 mrmGetEntityFacePitchYawBG( const MovementRequesterMsg *msg,
								EntityRef er,
								Vec2 pyFaceOut)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(	!mrmPrivateGetManagerByEntityRefBG(pd, er, &mm) ||
		!pyFaceOut)
	{
		return 0;
	}

	if(mmShouldUsePastBG(pd->mm)){
		copyVec2(mm->bg.past.pyFace, pyFaceOut);
	}
	else if(mm->bg.outputList.tail){
		copyVec2(mm->bg.outputList.tail->data.pyFace, pyFaceOut);
	}else{
		copyVec2(mm->bg.pyFace, pyFaceOut);
	}

	return 1;
}

S32 mrmGetEntityPositionAndFacePitchYawBG(	const MovementRequesterMsg *msg,
											EntityRef er,
											Vec3 posOut,
											Vec2 pyFaceOut)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(	!mrmPrivateGetManagerByEntityRefBG(pd, er, &mm) ||
		!pyFaceOut || 
		!posOut)
	{
		return 0;
	}

	if(mmShouldUsePastBG(pd->mm)){
		copyVec2(mm->bg.past.pyFace, pyFaceOut);
		copyVec3(mm->bg.past.pos, posOut);
	}
	else if(mm->bg.outputList.tail){
		copyVec2(mm->bg.outputList.tail->data.pyFace, pyFaceOut);
		copyVec3(mm->bg.outputList.tail->data.pos, posOut);
	}else{
		copyVec2(mm->bg.pyFace, pyFaceOut);
		copyVec3(mm->bg.pos, posOut);
	}

	return 1;
}

S32 mrmGetPositionAndRotationBG(const MovementRequesterMsg* msg,
								Vec3 posOut,
								Quat rotOut)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(	!pd ||
		!MR_MSG_TYPE_IS_BG(pd->msgType))
	{
		return 0;
	}

	mm = pd->mm;

	if(posOut){
		copyVec3(mm->bg.pos, posOut);
	}

	if(rotOut){
		copyQuat(mm->bg.rot, rotOut);
	}

	return 1;
}

S32 mrmGetPositionBG(	const MovementRequesterMsg* msg,
						Vec3 posOut)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(	!pd ||
		!MR_MSG_TYPE_IS_BG(pd->msgType) ||
		!posOut)
	{
		return 0;
	}

	mm = pd->mm;

	if(	!mgState.flags.isServer &&
		!mm->bg.flags.isPredicting)
	{
		MovementThreadData* td = MM_THREADDATA_BG(mm);
		MovementNetOutput*	noTail = td->toBG.net.outputList.tail;
		
		if(noTail){
			copyVec3(noTail->data.pos, posOut);
		}else{
			copyVec3(mm->bg.pos, posOut);
		}
	}else{
		copyVec3(mm->bg.pos, posOut);
	}

	return 1;
}

S32 mrmGetRotationBG(	const MovementRequesterMsg* msg,
						Quat rotOut)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(	!pd ||
		!MR_MSG_TYPE_IS_BG(pd->msgType) ||
		!rotOut)
	{
		return 0;
	}

	mm = pd->mm;

	if(	!mgState.flags.isServer &&
		!mm->bg.flags.isPredicting)
	{
		MovementThreadData* td = MM_THREADDATA_BG(mm);
		MovementNetOutput*	noTail = td->toBG.net.outputList.tail;
		
		if(noTail){
			copyQuat(noTail->data.rot, rotOut);
		}else{
			copyQuat(mm->bg.rot, rotOut);
		}
	}else{
		copyQuat(mm->bg.rot, rotOut);
	}

	return 1;
}

S32 mrmGetFacePitchYawBG(	const MovementRequesterMsg* msg,
							Vec2 pyFaceOut)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;
	
	if(	!pd ||
		!MR_MSG_TYPE_IS_BG(pd->msgType) ||
		!pyFaceOut)
	{
		return 0;
	}

	mm = pd->mm;

	if(	!mgState.flags.isServer &&
		!mm->bg.flags.isPredicting)
	{
		MovementThreadData* td = MM_THREADDATA_BG(mm);
		MovementNetOutput*	noTail = td->toBG.net.outputList.tail;
		
		if(noTail){
			copyVec2(noTail->data.pyFace, pyFaceOut);
			return 1;
		}
	}

	copyVec2(mm->bg.pyFace, pyFaceOut);

	return 1;
}

// This function cheats.  TODO(AM)/TODO(MS): fix this
S32 mrmGetNoCollBG(const MovementRequesterMsg* msg, S32 *noCollOut)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(!pd){
		return 0;
	}

	mm = pd->mm;

	*noCollOut = mm->bg.flags.noCollision;

	return 1;
}

S32 mrmGetPrimaryBodyRadiusBG(	const MovementRequesterMsg* msg,
								F32* radiusOut)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(	!pd ||
		!MR_MSG_TYPE_IS_BG(pd->msgType))
	{
		return 0;
	}

	mm = pd->mm;

	EARRAY_CONST_FOREACH_BEGIN(mm->bg.bodyInstances, i, isize);
	{
		MovementBodyInstance* bi = mm->bg.bodyInstances[i];
		
		if(eaSize(&bi->body->capsules)){
			*radiusOut = bi->body->capsules[0]->fRadius;
			return 1;
		}
	}
	EARRAY_FOREACH_END;
	
	if(eaSize(&mm->bg.bodyInstances)){
		*radiusOut = mm->bg.bodyInstances[0]->body->radius;
		return 1;
	}

	return 0;
}

S32 mmGetCapsulesBG(MovementManager* mm,
					const Capsule*const** capsulesOut)
{
	EARRAY_CONST_FOREACH_BEGIN(mm->bg.bodyInstances, i, isize);
	{
		MovementBodyInstance* bi = mm->bg.bodyInstances[i];
		
		if(eaSize(&bi->body->capsules)){
			*capsulesOut = bi->body->capsules;
			return 1;
		}
	}
	EARRAY_FOREACH_END;
	
	return 0;
}

static S32 mmGetEntityDistanceBGInternal(	const MovementRequesterMsg* msg,
											EntityRef er,
											const Vec3 posSourceIn,
											S32 xzOnly,
											S32 ignoreOwnCapsule,
											F32* distOut)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);

	MovementManager*					mm;
	Vec3								posSource;
	Quat								rotSource;
	const Capsule*const*				capSource = NULL;

	MovementManager*					mmTarget;
	Vec3								posTarget;
	Quat								rotTarget;
	const Capsule*const*				capTarget = NULL;

	if(	!pd ||
		!MR_MSG_TYPE_IS_BG(pd->msgType))
	{
		return 0;
	}

	if(!mrmGetManagerFromEntityRefBG(msg, er, &mmTarget)){
		return 0;
	}

	// Get source info.
	mm = pd->mm;

	if(!ignoreOwnCapsule){
		mmGetCapsulesBG(mm, &capSource);
	}

	if(posSourceIn){
		devassertmsg(ignoreOwnCapsule, "This isn't going to be accurate if you're trying to take into account your own capsules");
		copyVec3(posSourceIn, posSource);
		unitQuat(rotSource);
	}else{
		copyVec3(mm->bg.pos, posSource);
		copyQuat(mm->bg.rot, rotSource);
	}

	// Get target info.
	
	mmGetCapsulesBG(mmTarget, &capTarget);
	
	if(mmShouldUsePastBG(mm)){
		copyVec3(mmTarget->bg.past.pos, posTarget);
		copyQuat(mmTarget->bg.past.rot, rotTarget);
	}else{
		copyVec3(mmTarget->bg.pos, posTarget);
		copyQuat(mmTarget->bg.rot, rotTarget);
	}

	*distOut = CapsuleGetDistance(capSource, posSource, rotSource, capTarget, posTarget, rotTarget, NULL, NULL, xzOnly, 0);

	return 1;
}

S32 mrmGetEntityDistanceBG(	const MovementRequesterMsg* msg,
							EntityRef er,
							F32* distOut,
							S32 ignoreOwnCapsule)
{
	return mmGetEntityDistanceBGInternal(msg, er, NULL, 0, ignoreOwnCapsule, distOut);
}

S32 mrmGetEntityDistanceXZBG(const MovementRequesterMsg* msg,
							EntityRef er,
							F32* distOut,
							S32 ignoreOwnCapsule)
{
	return mmGetEntityDistanceBGInternal(msg, er, NULL, 1, ignoreOwnCapsule, distOut);
}

S32 mrmGetPointEntityDistanceBG(const MovementRequesterMsg* msg,
								const Vec3 posSource,
								EntityRef er,
								F32* distOut)
{
	return mmGetEntityDistanceBGInternal(msg, er, posSource, 1, 1, distOut);
}

static S32 mmGetCapsulePointDistanceBGInternal(	const MovementRequesterMsg* msg,
												const Vec3 posTarget,
												S32 xzOnly,
												S32 ignoreOwnCapsule,
												F32* distOut)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;
	Vec3								posSource;
	Quat								rotSource;
	const Capsule*const*				capSource = NULL;

	if(	!pd ||
		!MR_MSG_TYPE_IS_BG(pd->msgType))
	{
		return 0;
	}

	// Get source info.
	mm = pd->mm;

	if(!ignoreOwnCapsule){
		mmGetCapsulesBG(mm, &capSource);
	}
	
	copyVec3(mm->bg.pos, posSource);
	copyQuat(mm->bg.rot, rotSource);

	*distOut = CapsuleGetDistance(capSource, posSource, rotSource, NULL, posTarget, NULL, NULL, NULL, xzOnly, 0);

	return 1;
}

S32 mrmGetCapsulePointDistanceBG(	const MovementRequesterMsg* msg,
									const Vec3 posTarget,
									F32* distOut,
									S32 ignoreOwnCapsule)
{
	return mmGetCapsulePointDistanceBGInternal(msg, posTarget, 0, ignoreOwnCapsule, distOut);
}

S32 mrmGetCapsulePointDistanceXZBG(	const MovementRequesterMsg* msg,
									const Vec3 posTarget,
									F32* distOut,
									S32 ignoreOwnCapsule)
{
	return mmGetCapsulePointDistanceBGInternal(msg, posTarget, 1, ignoreOwnCapsule, distOut);
}

S32 mrmGetLineEntityDistanceBG(	const MovementRequesterMsg* msg,
								const Vec3 lineStart,
								const Vec3 lineDir,
								F32 lineLen,
								F32 radius,
								EntityRef target,
								F32* distOut)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);

	MovementManager*					mm;

	MovementManager*					mmTarget;
	Vec3								posTarget;
	Quat								rotTarget;
	const Capsule*const*				capTarget = NULL;

	if(	!pd ||
		!MR_MSG_TYPE_IS_BG(pd->msgType))
	{
		return 0;
	}

	if(!mrmGetManagerFromEntityRefBG(msg, target, &mmTarget)){
		return 0;
	}

	// Get source mm.
	mm = pd->mm;

	// Get target info.
	mmGetCapsulesBG(mmTarget, &capTarget);

	if(mmShouldUsePastBG(mm)){
		copyVec3(mmTarget->bg.past.pos, posTarget);
		copyQuat(mmTarget->bg.past.rot, rotTarget);
	}else{
		copyVec3(mmTarget->bg.pos, posTarget);
		copyQuat(mmTarget->bg.rot, rotTarget);
	}

	*distOut = CapsuleLineDistance(capTarget, posTarget, rotTarget, lineStart, lineDir, lineLen, radius, NULL, 0);

	return 1;
}

#define EPSILON 0.00001f

S32 mrmGetWorldCollPointDistanceBGInternal(	const MovementRequesterMsg *msg,
											const Vec3 targetPos,
											F32 *distOut,
											U32 xzOnly)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;
	Vec3								posSource;
	Vec3								coll;
	F32									lineDist;

	if(	!pd ||
		!MR_MSG_TYPE_IS_BG(pd->msgType))
	{
		return 0;
	}

	// Get source info.
	mm = pd->mm;

	copyVec3(mm->bg.pos, posSource);

	if(!xzOnly)
		lineDist = sqrt(PointLineDistSquared(targetPos, posSource, upvec, 6, coll));
	else
		lineDist = sqrt(PointLineDistSquaredXZ(targetPos, posSource, upvec, 6, coll));

	if(coll[1]>=posSource[1]-EPSILON && coll[1]<=posSource[1]+6+EPSILON)
	{
		if(lineDist <= ENT_WORLDCAP_DEFAULT_RADIUS)
			lineDist = 0;
		else
			lineDist -= ENT_WORLDCAP_DEFAULT_RADIUS;
	}

	*distOut = lineDist;

	return 1;
}

S32 mrmGetWorldCollPointDistanceBG(	const MovementRequesterMsg *msg,
									const Vec3 targetPos,
									F32 *distOut)
{
	return mrmGetWorldCollPointDistanceBGInternal(msg, targetPos, distOut, 0);
}

S32 mrmGetWorldCollPointDistanceXZBG(	const MovementRequesterMsg *msg,
									const Vec3 targetPos,
									F32 *distOut)
{
	return mrmGetWorldCollPointDistanceBGInternal(msg, targetPos, distOut, 1);
}

static S32 mmSendQueryMessage(	MovementRequester* mr,
								MovementRequesterMsgType msgType,
								MovementRequesterMsgOut* out)
{
	MovementRequesterMsgPrivateData pd;

	if(	!mr ||
		!out)
	{
		return 0;
	}

	mmRequesterMsgInitBG(&pd, out, mr, msgType);
	mmRequesterMsgSend(&pd);

	return 1;
}

S32 mrmGetOnGroundBG(	const MovementRequesterMsg* msg,
						S32* onGroundOut,
						Vec3 groundNormalOut)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;
	MovementRequesterMsgOut				out;

	if(	!pd ||
		!MR_MSG_TYPE_IS_BG(pd->msgType) ||
		!onGroundOut)
	{
		return 0;
	}

	mm = pd->mm;

	if(!mmSendQueryMessage(	mm->bg.dataOwner[MDC_POSITION_CHANGE],
							MR_MSG_BG_QUERY_ON_GROUND,
							&out))
	{
		return 0;
	}

	*onGroundOut = out.bg.queryOnGround.onGround;

	if(groundNormalOut){
		copyVec3(out.bg.queryOnGround.normal, groundNormalOut);
	}

	return 1;
}

S32 mrmGetEntityOnGroundBG(	const MovementRequesterMsg* msg,
							EntityRef er,
							S32* onGroundOut)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;
	MovementRequesterMsgOut				out;

	if(!mrmPrivateGetManagerByEntityRefBG(pd, er, &mm)){
		return 0;
	}

	if(!mmSendQueryMessage(	mm->bg.dataOwner[MDC_POSITION_CHANGE],
							MR_MSG_BG_QUERY_ON_GROUND,
							&out))
	{
		return 0;
	}

	*onGroundOut = out.bg.queryOnGround.onGround;

	return 1;
}

S32 mrmGetVelocityBG(	const MovementRequesterMsg* msg,
						Vec3 velOut)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;
	MovementRequesterMsgOut				out;

	if(	!pd ||
		!MR_MSG_TYPE_IS_BG(pd->msgType) ||
		!velOut)
	{
		return 0;
	}

	mm = pd->mm;

	if(!mmSendQueryMessage(	mm->bg.dataOwner[MDC_POSITION_CHANGE],
							MR_MSG_BG_QUERY_VELOCITY,
							&out))
	{
		return 0;
	}

	copyVec3(	out.bg.queryVelocity.vel,
				velOut);

	return 1;
}

static S32 mrmGetLurchInfoBG(	const MovementRequesterMsg* msg,
								EntityRef *perLurchTargetOut,
								F32* pfAddedCapsuleRadiusOut)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;
	MovementRequesterMsgOut				out;

	if(	!pd ||
		!MR_MSG_TYPE_IS_BG(pd->msgType) ||
		!perLurchTargetOut ||
		!pfAddedCapsuleRadiusOut)
	{
		return 0;
	}

	mm = pd->mm;

	if(!mmSendQueryMessage(	mm->bg.dataOwner[MDC_POSITION_TARGET],
							MR_MSG_BG_QUERY_LURCH_INFO,
							&out))
	{
		return 0;
	}

	*pfAddedCapsuleRadiusOut = out.bg.queryLurchInfo.addedRadius;
	*perLurchTargetOut = out.bg.queryLurchInfo.erTarget;

	return 1;
}

static S32 mmGetCurrentSpeedInternal(	MovementManager *mm, 
	F32 *currentSpeedOut)
{
	MovementRequesterMsgOut	out;

	if(!mmSendQueryMessage(	mm->bg.dataOwner[MDC_POSITION_CHANGE],
		MR_MSG_BG_QUERY_VELOCITY,
		&out))
	{
		return 0;
	}

	*currentSpeedOut = lengthVec3(out.bg.queryVelocity.vel);

	return 1;
}

S32 mrmGetEntityCurrentSpeedBG(	const MovementRequesterMsg* msg,
	EntityRef er,
	F32 *currentSpeedOut)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mmTarget = NULL;

	if(	!pd ||
		!MR_MSG_TYPE_IS_BG(pd->msgType))
	{
		return 0;
	}

	if(!mrmGetManagerFromEntityRefBG(msg, er, &mmTarget)){
		return 0;
	}

	return mmGetCurrentSpeedInternal(mmTarget, currentSpeedOut);
}

static S32 mmGetMaxSpeedInternal(	MovementManager *mm, 
								 F32 *maxSpeedOut)
{
	MovementRequesterMsgOut	out;

	if(!mmSendQueryMessage(	mm->bg.dataOwner[MDC_POSITION_CHANGE],
		MR_MSG_BG_QUERY_MAX_SPEED,
		&out))
	{
		return 0;
	}

	*maxSpeedOut = out.bg.queryMaxSpeed.maxSpeed;

	return 1;
}

S32 mrmGetMaxSpeedBG(	const MovementRequesterMsg* msg,
						F32* maxSpeedOut)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(	!pd ||
		!MR_MSG_TYPE_IS_BG(pd->msgType) ||
		!maxSpeedOut)
	{
		return 0;
	}

	mm = pd->mm;

	return mmGetMaxSpeedInternal(mm, maxSpeedOut);
}

S32 mrmGetEntityMaxSpeedBG(	const MovementRequesterMsg* msg,
							EntityRef er,
							F32 *maxSpeedOut)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mmTarget = NULL;

	if(	!pd ||
		!MR_MSG_TYPE_IS_BG(pd->msgType))
	{
		return 0;
	}

	if(!mrmGetManagerFromEntityRefBG(msg, er, &mmTarget)){
		return 0;
	}

	return mmGetMaxSpeedInternal(mmTarget, maxSpeedOut);
}

S32 mrmGetTurnRateBG(	const MovementRequesterMsg* msg,
						F32* turnRateOut)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;
	MovementRequesterMsgOut				out;

	if(	!pd ||
		!MR_MSG_TYPE_IS_BG(pd->msgType) ||
		!turnRateOut)
	{
		return 0;
	}

	mm = pd->mm;

	if(!mmSendQueryMessage(	mm->bg.dataOwner[MDC_POSITION_CHANGE],
							MR_MSG_BG_QUERY_TURN_RATE,
							&out))
	{
		return 0;
	}

	*turnRateOut = out.bg.queryTurnRate.turnRate;

	return 1;
}

S32 mrmGetIsSettledBG(	const MovementRequesterMsg* msg,
						S32* isSettledOut)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;
	MovementRequesterMsgOut				out;

	if(	!pd ||
		!MR_MSG_TYPE_IS_BG(pd->msgType) ||
		!isSettledOut)
	{
		return 0;
	}

	mm = pd->mm;

	if(!mm->bg.dataOwner[MDC_POSITION_CHANGE]){
		*isSettledOut = 1;
		return 1;
	}

	if(!mmSendQueryMessage(	mm->bg.dataOwner[MDC_POSITION_CHANGE],
							MR_MSG_BG_QUERY_IS_SETTLED,
							&out))
	{
		return 0;
	}

	*isSettledOut = out.bg.queryIsSettled.isSettled;

	return 1;
}

S32 mrmSetAdditionalVelBG(	const MovementRequesterMsg* msg,
							const Vec3 vel, 
							S32 isRepel, 
							S32 resetBGVel)
{
	MovementRequesterMsgPrivateData* pd = MR_MSG_TO_PD(msg);

	if(	!pd
		||
		!vel
		||
		pd->msgType != MR_MSG_BG_DISCUSS_DATA_OWNERSHIP && 
		pd->msgType != MR_MSG_BG_CREATE_OUTPUT)
	{
		return 0;
	}

	if(!vec3IsZero(vel)){
		MovementManager* mm = pd->mm;
		
		addVec3(vel,
				mm->bg.additionalVel.vel,
				mm->bg.additionalVel.vel);

		copyVec3(vel,
				 mm->bg.additionalVel.vel);
				
		mm->bg.additionalVel.flags.isRepel = !!isRepel;
		mm->bg.additionalVel.flags.resetBGVel = !!resetBGVel;
					
		mmLog(	mm,
				NULL,
				"[bg.physics] Adding additional vel (%1.2f, %1.2f, %1.2f), total now (%1.2f, %1.2f, %1.2f)",
				vecParamsXYZ(vel),
				vecParamsXYZ(mm->bg.additionalVel.vel));
		
		mm->bg.additionalVel.flags.isSet = 1;
	}
		
	return 1;
}

S32 mrmGetAdditionalVelBG(	const MovementRequesterMsg* msg,
							Vec3 velOut, 
							S32* isRepelOut, 
							S32* resetBGVelOut)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);

	if(	!pd ||
		pd->msgType != MR_MSG_BG_CREATE_OUTPUT ||
		!pd->mm->bg.additionalVel.flags.isSet)
	{
		return 0;
	}
	
	if(velOut){
		copyVec3(	pd->mm->bg.additionalVel.vel,
					velOut);
	}

	if(isRepelOut){
		*isRepelOut = pd->mm->bg.additionalVel.flags.isRepel;
	}

	if(resetBGVelOut){
		*resetBGVelOut= pd->mm->bg.additionalVel.flags.resetBGVel;
	}

	return 1;
}


S32 mrmSetConstantPushVelBG(const MovementRequesterMsg* msg,
							const Vec3 vel)
{
	MovementRequesterMsgPrivateData* pd = MR_MSG_TO_PD(msg);

	if(	!pd || 
		!vel ||
		(pd->msgType != MR_MSG_BG_DISCUSS_DATA_OWNERSHIP && 
		pd->msgType != MR_MSG_BG_CREATE_OUTPUT))
	{
		return 0;
	}

	if (!vec3IsZero(vel))
	{
		MovementManager* mm = pd->mm;

		copyVec3(vel, mm->bg.constantVel.vel);
		mm->bg.constantVel.isSet = true;

		mmLog(	mm,
			NULL,
			"[bg.physics] Setting constant vel (%1.2f, %1.2f, %1.2f)",
			vecParamsXYZ(vel));
	}
	else
	{
		MovementManager* mm = pd->mm;

		zeroVec3(mm->bg.constantVel.vel);
		mm->bg.constantVel.isSet = false;
		
		mmLog(	mm,
				NULL,
				"[bg.physics] Clearing constant vel");
	}

	return 1;
}

S32 mrmHasConstantPushVelBG(const MovementRequesterMsg* msg)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);

	return (pd && pd->mm->bg.constantVel.isSet);
}

S32	mrmGetConstantPushVelBG(const MovementRequesterMsg* msg,
							Vec3 velOut)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);

	if(	!pd ||
		pd->msgType != MR_MSG_BG_CREATE_OUTPUT ||
		!pd->mm->bg.constantVel.isSet)
	{
		return 0;
	}

	copyVec3(pd->mm->bg.constantVel.vel, velOut);

	return 1;
}

static S32 mrMsgCanSetPositionTarget(const MovementRequesterMsgPrivateData* pd)
{
	return	pd &&
			pd->msgType == MR_MSG_BG_CREATE_OUTPUT &&
			pd->in.bg.createOutput.dataClassBit == MDC_BIT_POSITION_TARGET;
}

S32	mrmTargetSetAsInputBG(const MovementRequesterMsg* msg)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(!mrMsgCanSetPositionTarget(pd)){
		return 0;
	}

	mm = pd->mm;

	mm->bg.target.pos.targetType = MPTT_INPUT;

	mrLog(	pd->mr,
			NULL,
			"Setting target as INPUT.");

	return 1;
}

S32 mrmTargetSetAsStoppedBG(const MovementRequesterMsg* msg)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(!mrMsgCanSetPositionTarget(pd)){
		return 0;
	}

	mm = pd->mm;

	mm->bg.target.pos.targetType = MPTT_STOPPED;

	mrLog(	pd->mr,
			NULL,
			"Setting target as STOPPED.");

	return 1;
}

S32 mrmTargetSetAsVelocityBG(	const MovementRequesterMsg* msg,
								const Vec3 vel)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(!mrMsgCanSetPositionTarget(pd)){
		return 0;
	}

	mm = pd->mm;

	mm->bg.target.pos.targetType = MPTT_VELOCITY;

	copyVec3(	vel,
				mm->bg.target.pos.vel);

	mrLog(	pd->mr,
			NULL,
			"Setting target as VELOCITY:"
			" p(%.2f, %.2f, %.2f)"
			" p[%8.8x, %8.8x, %8.8x]",
			vecParamsXYZ(vel),
			vecParamsXYZ((S32*)vel));

	return 1;
}

S32 mrmTargetSetAsPointBG(	const MovementRequesterMsg* msg,
							const Vec3 target)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(!mrMsgCanSetPositionTarget(pd)){
		return 0;
	}

	mm = pd->mm;

	mm->bg.target.pos.targetType = MPTT_POINT;

	MM_CHECK_DYNPOS_DEVONLY(target);

	copyVec3(	target,
				mm->bg.target.pos.point);

	mrLog(	pd->mr,
			NULL,
			"Setting target as POINT:"
			" p(%.2f, %.2f, %.2f)"
			" p[%8.8x, %8.8x, %8.8x]",
			vecParamsXYZ(target),
			vecParamsXYZ((S32*)target));

	return 1;
}

S32 mrmTargetSetStartJumpBG(const MovementRequesterMsg* msg,
							const Vec3 target,
							S32 startJump)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(!mrMsgCanSetPositionTarget(pd)){
		return 0;
	}

	mm = pd->mm;

	startJump = !!startJump;

	mm->bg.target.pos.flags.startJump = startJump;

	mm->bg.target.pos.flags.hasJumpTarget = !!target;

	if(target){
		MM_CHECK_DYNPOS_DEVONLY(target);

		copyVec3(	target,
					mm->bg.target.pos.jumpTarget);
	}

	mrLog(	pd->mr,
			NULL,
			"Setting target flag STARTJUMP: %d",
			startJump);

	return 1;
}

S32 mrmTargetSetNoWorldCollBG(	const MovementRequesterMsg* msg,
								S32 noWorldColl)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(!mrMsgCanSetPositionTarget(pd)){
		return 0;
	}

	mm = pd->mm;

	noWorldColl = !!noWorldColl;

	mm->bg.target.pos.flags.noWorldColl = noWorldColl;

	mrLog(	pd->mr,
			NULL,
			"Setting target flag NOWORLDCOLL: %d",
			noWorldColl);

	return 1;
}

S32 mrmTargetSetUseYBG(	const MovementRequesterMsg* msg,
						S32 useY)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(!mrMsgCanSetPositionTarget(pd)){
		return 0;
	}

	mm = pd->mm;

	useY = !!useY;

	mm->bg.target.pos.flags.useY = useY;

	mrLog(	pd->mr,
			NULL,
			"Setting target flag USEY: %d",
			useY);

	return 1;
}

S32	mrmTargetSetSpeedAsNormalBG(const MovementRequesterMsg* msg){
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(!mrMsgCanSetPositionTarget(pd)){
		return 0;
	}

	mm = pd->mm;

	mm->bg.target.pos.speedType = MST_NORMAL;

	mrLog(	pd->mr,
			NULL,
			"Setting target speed as NORMAL.");

	return 1;
}

S32	mrmTargetSetSpeedAsOverrideBG(	const MovementRequesterMsg* msg,
									F32 maxSpeed)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(!mrMsgCanSetPositionTarget(pd)){
		return 0;
	}

	mm = pd->mm;

	mm->bg.target.pos.speedType = MST_OVERRIDE;
	mm->bg.target.pos.speed = maxSpeed;

	mrLog(	pd->mr,
			NULL,
			"Setting target speed as OVERRIDE.");

	return 1;
}

S32	mrmTargetSetSpeedAsImpulseBG(const MovementRequesterMsg* msg){
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(!mrMsgCanSetPositionTarget(pd)){
		return 0;
	}

	mm = pd->mm;

	mm->bg.target.pos.speedType = MST_IMPULSE;

	mrLog(	pd->mr,
			NULL,
			"Setting target speed as IMPULSE.");

	return 1;
}

S32	mrmTargetSetSpeedAsConstantBG(	const MovementRequesterMsg* msg,
									F32 speed)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(!mrMsgCanSetPositionTarget(pd)){
		return 0;
	}

	mm = pd->mm;

	mm->bg.target.pos.speedType = MST_CONSTANT;
	mm->bg.target.pos.speed = speed;

	mrLog(	pd->mr,
			NULL,
			"Setting target speed as CONSTANT: %.2f",
			speed);

	return 1;
}

S32	mrmTargetSetMinimumSpeedBG(	const MovementRequesterMsg* msg,
								F32 minSpeed)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(!mrMsgCanSetPositionTarget(pd)){
		return 0;
	}

	mm = pd->mm;

	mm->bg.target.minSpeed = minSpeed;

	mrLog(	pd->mr,
			NULL,
			"Setting target minimum speed speed: %.2f",
			minSpeed);

	return 1;
}

S32	mrmTargetSetTurnRateAsNormalBG(const MovementRequesterMsg* msg){
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(!mrMsgCanSetPositionTarget(pd)){
		return 0;
	}

	mm = pd->mm;

	mm->bg.target.pos.turnRateType = MTRT_NORMAL;

	mrLog(	pd->mr,
			NULL,
			"Setting target turn rate as NORMAL.");

	return 1;
}

S32	mrmTargetSetTurnRateAsOverrideBG(	const MovementRequesterMsg* msg,
										F32 turnRate)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(!mrMsgCanSetPositionTarget(pd)){
		return 0;
	}

	mm = pd->mm;

	mm->bg.target.pos.turnRateType = MTRT_OVERRIDE;
	mm->bg.target.pos.turnRate = turnRate;

	mrLog(	pd->mr,
			NULL,
			"Setting target turn rate as OVERRIDE.");

	return 1;
}

S32	mrmTargetSetFrictionAsNormalBG(const MovementRequesterMsg* msg){
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(!mrMsgCanSetPositionTarget(pd)){
		return 0;
	}

	mm = pd->mm;

	mm->bg.target.pos.frictionType = MFT_NORMAL;

	mrLog(	pd->mr,
			NULL,
			"Setting target friction as NORMAL.");

	return 1;
}

S32	mrmTargetSetFrictionAsOverrideBG(	const MovementRequesterMsg* msg,
										F32 friction)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(!mrMsgCanSetPositionTarget(pd)){
		return 0;
	}

	mm = pd->mm;

	mm->bg.target.pos.frictionType = MFT_OVERRIDE;
	mm->bg.target.pos.friction = friction;

	mrLog(	pd->mr,
			NULL,
			"Setting target friction as OVERRIDE (%1.3f).",
			friction);

	return 1;
}

S32	mrmTargetSetTractionAsNormalBG(const MovementRequesterMsg* msg){
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(!mrMsgCanSetPositionTarget(pd)){
		return 0;
	}

	mm = pd->mm;

	mm->bg.target.pos.tractionType = MTT_NORMAL;

	mrLog(	pd->mr,
			NULL,
			"Setting target traction as NORMAL.");

	return 1;
}

S32	mrmTargetSetTractionAsOverrideBG(	const MovementRequesterMsg* msg,
										F32 traction)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(!mrMsgCanSetPositionTarget(pd)){
		return 0;
	}

	mm = pd->mm;

	mm->bg.target.pos.tractionType = MTT_OVERRIDE;
	mm->bg.target.pos.traction = traction;

	mrLog(	pd->mr,
			NULL,
			"Setting target traction as OVERRIDE (%1.3f).",
			traction);

	return 1;
}

static S32 mmMsgCanSetRotationTarget(const MovementRequesterMsgPrivateData* pd){
	return	pd &&
			pd->msgType == MR_MSG_BG_CREATE_OUTPUT &&
			pd->in.bg.createOutput.dataClassBit == MDC_BIT_ROTATION_TARGET;
}

S32 mrmRotationTargetSetAsStoppedBG(const MovementRequesterMsg* msg){
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(!mmMsgCanSetRotationTarget(pd)){
		return 0;
	}

	mm = pd->mm;

	mm->bg.target.rot.targetType = MRTT_STOPPED;

	mrLog(	pd->mr,
			NULL,
			"Setting rotation target as STOPPED.");

	return 1;
}

S32 mrmRotationTargetSetAsInputBG(const MovementRequesterMsg* msg){
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(!mmMsgCanSetRotationTarget(pd)){
		return 0;
	}

	mm = pd->mm;

	mm->bg.target.rot.targetType = MRTT_INPUT;

	mrLog(	pd->mr,
			NULL,
			"Setting rotation target as INPUT.");

	return 1;
}

S32 mrmRotationTargetSetAsRotationBG(	const MovementRequesterMsg* msg,
										const Quat rot)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(!mmMsgCanSetRotationTarget(pd)){
		return 0;
	}

	mm = pd->mm;

	mm->bg.target.rot.targetType = MRTT_ROTATION;

	copyQuat(	rot,
				mm->bg.target.rot.rot);

	mrLog(	pd->mr,
			NULL,
			"Setting rotation target as ROTATION:"
			" r(%.2f, %.2f, %.2f, %.2f)"
			" r[%8.8x, %8.8x, %8.8x, %8.8x]",
			quatParamsXYZW(rot),
			quatParamsXYZW((S32*)rot));

	return 1;
}

S32 mrmRotationTargetSetAsPointBG(	const MovementRequesterMsg* msg,
									const Vec3 point)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(!mmMsgCanSetRotationTarget(pd)){
		return 0;
	}

	mm = pd->mm;

	mm->bg.target.rot.targetType = MRTT_POINT;

	copyVec3(	point,
				mm->bg.target.rot.point);

	mrLog(	pd->mr,
			NULL,
			"Setting rotation target as POINT:"
			" r(%.2f, %.2f, %.2f)"
			" r[%8.8x, %8.8x, %8.8x]",
			vecParamsXYZ(point),
			vecParamsXYZ((S32*)point));

	return 1;
}

S32 mrmRotationTargetSetAsDirectionBG(	const MovementRequesterMsg* msg,
										const Vec3 dir)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(!mmMsgCanSetRotationTarget(pd)){
		return 0;
	}

	mm = pd->mm;

	mm->bg.target.rot.targetType = MRTT_DIRECTION;

	copyVec3(	dir,
				mm->bg.target.rot.dir);

	mrLog(	pd->mr,
			NULL,
			"Setting rotation target as DIRECTION:"
			" d(%.2f, %.2f, %.2f)"
			" d[%8.8x, %8.8x, %8.8x]",
			vecParamsXYZ(dir),
			vecParamsXYZ((S32*)dir));

	return 1;
}

S32 mrmRotationTargetSetSpeedAsImpulseBG(const MovementRequesterMsg* msg){
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(!mmMsgCanSetRotationTarget(pd)){
		return 0;
	}

	mm = pd->mm;

	mm->bg.target.rot.speedType = MST_IMPULSE;

	mrLog(	pd->mr,
			NULL,
			"Setting rotation target speed to IMPULSE.");

	return 1;
}

S32 mrmRotationTargetSetTurnRateAsNormalBG(const MovementRequesterMsg* msg)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(!mmMsgCanSetRotationTarget(pd)){
		return 0;
	}

	mm = pd->mm;

	mm->bg.target.rot.turnRateType = MTRT_NORMAL;

	mrLog(	pd->mr,
			NULL,
			"Setting rotation turn rate to NORMAL.");

	return 1;
}

S32 mrmRotationTargetSetTurnRateAsOverrideBG(	const MovementRequesterMsg* msg,
												F32 turnRate)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(!mmMsgCanSetRotationTarget(pd)){
		return 0;
	}

	mm = pd->mm;

	mm->bg.target.rot.turnRateType = MTRT_OVERRIDE;
	mm->bg.target.rot.turnRate = turnRate;

	mrLog(	pd->mr,
			NULL,
			"Setting rotation turn rate to OVERRIDE (%1.3f).",
			turnRate);

	return 1;
}

// Gets the rotation for the capsule.
// Non-flying entities are assumed to not need pitch for their capsule rotation.
// The Flight attrib mode determines if the pitch should be ignored when entities
// fly with an "Ignore Pitch" flag. However the way it's implemented, it completely
// removes the pitch component. If this causes a problem in the future we might want to
// pass that flag back to the movement manager and use it all the way here instead of
// removing the pitch component.
static void mmGetRotationForCapsuleCollisionsBG(SA_PARAM_NN_VALID MovementManager* mm, 
												bool bUsePast,
												SA_PARAM_NN_VALID Quat rot)
{
	Vec3 pyr;

	// decide to use facing or movement rotation for the capsule facing.
	// the default is to use facing 
	if (!mm->bg.flags.capsuleOrientationUseRotation)
	{	
		if (bUsePast)
		{
			copyVec2(mm->bg.past.pyFace, pyr);
		}
		else
		{
			copyVec2(mm->bg.pyFace, pyr);
		}

		// Don't touch the pitch if flying. Powers code determines if the pitch should be respected.
		if (!mm->bg.flags.mrIsFlying)
		{
			// Set no pitch
			pyr[0] = 0.f;
		}

		// Set no roll
		pyr[2] = 0.f;

		// Convert to quat
		PYRToQuat(pyr, rot);
	}
	else
	{
		if (bUsePast)
		{
			copyQuat(mm->bg.past.rot, rot);
		}
		else
		{
			copyQuat(mm->bg.rot, rot);
		}
	}
}

static S32 mmCollideWithOthersInGridCellBG(	MovementManager* mm,
											const MovementManagerGridCell* cell,
											Vec3 pos1,
											const Quat rot1,
											const Vec3 vMoveDir,
											S32 shouldUsePast,
											F32 addedCapsuleRadius,
											EntityRef erLurchTarget,
											bool allowAwayFrom)
{
	S32 result = 0;

	EARRAY_CONST_FOREACH_BEGIN(cell->managers, i, size);
	{
		MovementManager* mmOther = cell->managers[i];

		if(	mm != mmOther &&
			!mmOther->bg.flags.noCollision)
		{
			Quat					rot0;
			Vec3					pos0;
			Vec3					pyr0;
			Vec3					vEntToOther;
			F32						len;
			F32						maxDistanceToBeTouching;
			const Capsule*const*	mmCaps = NULL;
			const Capsule*const*	mmOtherCaps = NULL;
			S32						collisionCounts = 1;
			U32						mmCapSet = 0;
			U32						mmOtherCapSet = 0;
			F32						curAddedCapsuleRadius;

			// No roll
			pyr0[2] = 0.f;
			
			maxDistanceToBeTouching =	mm->bg.bodyRadius +
										mmOther->bg.bodyRadius + addedCapsuleRadius;
			
			curAddedCapsuleRadius = addedCapsuleRadius;
			
			if(shouldUsePast){
				copyVec3(	mmOther->bg.past.pos,
							pos0);

				// Get the capsule rotation
				mmGetRotationForCapsuleCollisionsBG(mmOther, shouldUsePast, rot0);
			}else{
				copyVec3(	mmOther->bg.pos,
							pos0);

				// Get the capsule rotation
				mmGetRotationForCapsuleCollisionsBG(mmOther, shouldUsePast, rot0);
			}

			subVec3(pos0, pos1, vEntToOther);

			if(	fabs(vEntToOther[0]) >= maxDistanceToBeTouching ||
				fabs(vEntToOther[1]) >= maxDistanceToBeTouching ||
				fabs(vEntToOther[2]) >= maxDistanceToBeTouching)
			{
				continue;
			}

			if (addedCapsuleRadius > 0.f ||
				allowAwayFrom)
			{	// hard-coded behavior - if we have addedCapsuleRadius, assume we want to ignore any entities that we are moving away from.
				// this is to fix lurching getting stuck in entities when lurching away from them

				F32 fDot = dotVec3(vMoveDir, vEntToOther);
				if (fDot <= 0.f)
				{	// entity is behind our move direction, ignore it
					continue;
				}
				else
				{	// check to see if the entity is within a small arc in front of us, if not ignore it
					F32 len1 = lengthVec3(vMoveDir);
					F32 len2 = lengthVec3(vEntToOther);
					F32 cosval = fDot / (len1 * len2);
					 
					cosval = CLAMPF32(cosval, -1.0f, 1.0f);
					cosval = acosf(cosval);
					if ( cosval >= RAD(45.f)) 
						continue;
				}

				if (erLurchTarget && mmOther->entityRef != erLurchTarget)
				{
					curAddedCapsuleRadius = 0.f;
				}
			}
			
			mmGetCapsulesBG(mm, &mmCaps);
			mmGetCapsulesBG(mmOther, &mmOtherCaps);

			// Negative collision set means to use the alternate capsule set
			if(mm->fg.collisionSet < 0){
				EARRAY_CONST_FOREACH_BEGIN(mmCaps, j, jSize);
				{
					if(mmCaps[j]->iType == 1){
						mmCapSet = 1;
					}
				}
				EARRAY_FOREACH_END;
			}

			if(mmOther->fg.collisionSet < 0){
				EARRAY_CONST_FOREACH_BEGIN(mmOtherCaps, j, jSize);
				{
					if(mmOtherCaps[j]->iType == 1){
						mmOtherCapSet = 1;
					}
				}
				EARRAY_FOREACH_END;
			}

			collisionCounts =	!mm->fg.collisionGroupBits ||
								!mmOther->fg.collisionGroup ||
								mm->fg.collisionGroupBits & mmOther->fg.collisionGroup;

			if(	collisionCounts &&
				mm->fg.collisionSet > 0 &&
				mmOther->fg.collisionSet > 0)
			{
				collisionCounts = mm->fg.collisionSet != mmOther->fg.collisionSet;
			}

			EARRAY_CONST_FOREACH_BEGIN(mmCaps, j, jSize);
			{
				const Capsule* cap1 = mmCaps[j];

				if(cap1->iType != mmCapSet){
					continue;
				}

				EARRAY_CONST_FOREACH_BEGIN(mmOtherCaps, k, kSize);
				{
					const Capsule*	cap0 = mmOtherCaps[k];
					Vec3			hit0;
					Vec3			hit1;

					if(cap0->iType != mmOtherCapSet){
						continue;
					}

					if(	collisionCounts &&
						CapsuleCapsuleCollide(	cap0,
												pos0,
												rot0,
												hit0,
												cap1,
												pos1,
												rot1,
												hit1,
												&len,
												curAddedCapsuleRadius))
					{
						F32 depenetrateDist;
							
						if (!curAddedCapsuleRadius || len < cap0->fRadius + cap1->fRadius)
						{
							depenetrateDist = cap0->fRadius + cap1->fRadius - len;
						}
						else 
						{
							F32 moveDist = lengthVec3(vMoveDir);
							depenetrateDist = cap0->fRadius + cap1->fRadius + curAddedCapsuleRadius - len;
							
							if (depenetrateDist > moveDist)
							{
								depenetrateDist = moveDist;
							}
						}
						
						if (depenetrateDist > EPSILON)
						{
							Vec3 diff;
							subVec3(hit1, hit0, diff);
							normalVec3(diff);
							scaleVec3(diff, depenetrateDist, diff);
							addVec3(pos1, diff, pos1);
						}
						
						result = 1;
					}
				}
				EARRAY_FOREACH_END;
			}
			EARRAY_FOREACH_END;
		}
	}
	EARRAY_FOREACH_END;
	
	return result;
}

static S32 mmCollideWithOthersBG(	MovementManager* mm,
									const Vec3 pos,
									Vec3 dirInOut,
									F32 addedCapsuleRadius,
									EntityRef erLurchTarget,
									bool allowAwayFrom)
{
	S32		result = 0;
	Vec3	pos1;
	Quat	rot1;
	
	IVec3	lo;
	IVec3	hi;
	S32		shouldUsePast = mmShouldUsePastBG(mm);

	if(!mm->space){
		return 0;
	}

	PERFINFO_AUTO_START_FUNC();

	addVec3(pos,
			dirInOut,
			pos1);

	// Get the capsule rotation
	mmGetRotationForCapsuleCollisionsBG(mm, false, rot1);

	ARRAY_FOREACH_BEGIN(mmGridSizeGroups, sizeIndex);
	{
		MovementManagerGrid*	grid = mm->space->mmGrids + sizeIndex;
		const F32				cellSize = mmGridSizeGroups[sizeIndex].cellSize;

		if(cellSize){
			F32 offset = mm->bg.bodyRadius + mmGridSizeGroups[sizeIndex].maxBodyRadius;
			
			FOR_BEGIN(i, 3);
			{
				lo[i] = (S32)floor((pos1[i] - offset) / cellSize);
				hi[i] = (S32)floor((pos1[i] + offset) / cellSize) + 1;
			}
			FOR_END;
		}else{
			setVec3same(lo, 0);
			setVec3same(hi, 1);
		}

		FOR_BEGIN_FROM(x, lo[0], hi[0]);
		{
			FOR_BEGIN_FROM(y, lo[1], hi[1]);
			{
				FOR_BEGIN_FROM(z, lo[2], hi[2]);
				{
					MovementManagerGridCell*	cell;
					IVec3						posGrid = {x, y, z};

					if(!mmGridGetCellByGridPosBG(grid, &cell, posGrid, 0)){
						continue;
					}
					
					result |= mmCollideWithOthersInGridCellBG(	mm,
																cell, 
																pos1, 
																rot1,
																dirInOut,
																shouldUsePast,
																addedCapsuleRadius,
																erLurchTarget,
																allowAwayFrom);
				}
				FOR_END;
			}
			FOR_END;
		}
		FOR_END;
	}
	ARRAY_FOREACH_END;

	subVec3(pos1,
			pos,
			dirInOut);

	PERFINFO_AUTO_STOP();
	
	return result;
}

static void mmLogHitSurface(MovementManager* mm,
							const Vec3 pos,
							const Vec3 normal,
							S32 isGround)
{
	char actorInfoString[1000];

	actorInfoString[0] = 0;

	mmLog(	mm,
			NULL,
			"[bg.physics] Hit surface: %s\n"
			" pos(%1.3f, %1.3f, %1.3f) pos[%8.8x, %8.8x, %8.8x]\n"
			" normal(%1.3f, %1.3f, %1.3f) [%8.8x, %8.8x, %8.8x]"
			,
			isGround ? " isGround" : "",
			vecParamsXYZ(pos),
			vecParamsXYZ((S32*)pos),
			vecParamsXYZ(normal),
			vecParamsXYZ((S32*)normal));

	{
		Vec3 b;

		addVec3(pos,
				normal,
				b);

		mmLogSegment(	mm,
						NULL,
						"bg.physics",
						0,
						pos,
						b);

		#if 0
		{
			mmLogSegment(	mm,
							NULL,
							"bg.physics",
							0,
							psdkMsg->hitSurface.tri[0],
							psdkMsg->hitSurface.tri[1]);

			mmLogSegment(	mm,
							NULL,
							"bg.physics",
							0,
							psdkMsg->hitSurface.tri[1],
							psdkMsg->hitSurface.tri[2]);

			mmLogSegment(	mm,
							NULL,
							"bg.physics",
							0,
							psdkMsg->hitSurface.tri[0],
							psdkMsg->hitSurface.tri[2]);
		}
		#endif
	}
}

static void mmNotifyHitSurface(	MovementManager* mm,
								const Vec3 pos,
								const Vec3 normal, 
								U32 isGround)
{
	if(MMLOG_IS_ENABLED(mm)){
		mmLogHitSurface(mm, pos, normal, isGround);
	}

	if(mm->bg.dataOwner[MDC_POSITION_CHANGE]){
		MovementRequesterMsgPrivateData pd;
		MovementRequester*				mr = mm->bg.dataOwner[MDC_POSITION_CHANGE];
		
		PERFINFO_AUTO_START_FUNC();

		mmRequesterMsgInitBG(	&pd,
								NULL,
								mr,
								MR_MSG_BG_CONTROLLER_MSG);

		copyVec3(	pos,
					pd.msg.in.bg.controllerMsg.pos);
		
		copyVec3(	normal, 
					pd.msg.in.bg.controllerMsg.normal);
		
		pd.msg.in.bg.controllerMsg.isGround = isGround;

		pd.msg.in.userStruct.toFG = MR_USERSTRUCT_TOFG(mr, MM_BG_SLOT);
		pd.msg.in.userStruct.toBG = MR_USERSTRUCT_TOBG(mr, MM_BG_SLOT);

		mmRequesterMsgSend(&pd);
		
		PERFINFO_AUTO_STOP();
	}
}

S32 mmGetWorldCollGridCellBG(	MovementManager* mm,
								const Vec3 pos,
								WorldCollGridCell** wcCellOut)
{
	WorldCollGridCell* wcCell = NULL;

	if(!wcGetGridCellByWorldPosBG(	mgState.bg.wciMsg,
									SAFE_MEMBER(mm->space, wc),
									&wcCell,
									pos))
	{
		// Will let you move in empty space.
	}
	else if(!wcCellHasScene(wcCell, NULL)){
		// Scene isn't created yet, so stall movement until it is.

		wcCellRequestSceneCreateBG(	mgState.bg.wciMsg,
									wcCell);

		return 0;
	}

	if(wcCellOut){
		*wcCellOut = wcCell;
	}

	return 1;
}

static S32 mmActorIgnoredCB(MovementManager* mm,
							const PSDKActor* psdkActor)
{
	MovementManager* mmCheck;
	WorldCollObject* wco;
	
	return	wcoGetFromPSDKActor(&wco, psdkActor) &&
			mmGetFromWCO(wco, &mmCheck) &&
			mmCheck == mm;
}							

static void moveItEx(	MovementManager* mm,
						const Vec3 vecOffset,
						const U32 maxSubStepCount,
						F32 addedCapsuleRadius,
						S32 bIgnoreCollisionWithEnts,
						S32 disableStickyGround,
						EntityRef erLurchTarget,
						S32* isStuckOut,
						S32* collidedWithEntOut)
{
	MotionState		motion = {0};
	F32				lenSQR = lengthVec3Squared(vecOffset);
	U32				stepCount = 1;
	Vec3			vecOffsetScaled;
	const F32		maxStepSize = 1.2f;
	//static S32		iCount = 0;

	if(mm->bg.flags.hasKinematicBody){
		motion.userPointer = mm;
		motion.actorIgnoredCB = mmActorIgnoredCB;
	}

	if(lenSQR > SQR(maxStepSize)){
		F32 len = sqrt(lenSQR);
		F32 scale;
		
		stepCount = ceilf(len / maxStepSize);
		
		if(!stepCount){
			stepCount = 1;
		}
		else if(maxSubStepCount){
			MIN1(stepCount, maxSubStepCount);
		}
		
		scale = 1.f / (F32)stepCount;
		
		scaleVec3(vecOffset, scale, vecOffsetScaled);
	}else{
		copyVec3(vecOffset, vecOffsetScaled);
	}

	motion.is_player = mm->bg.flags.isAttachedToClient;
	motion.use_sticky_ground = !disableStickyGround;

	if(collidedWithEntOut){
		*collidedWithEntOut = 0;
	}
		
	FOR_BEGIN(i, (S32)stepCount);
	{
		Vec3 vecOffsetStep;
		Vec3 vecOffsetStepCache;
		bool secondAttempt = false;
		bool collided = false;
		
retryMovementWithoutAwayFromCapsuleCollision:

		copyVec3(vecOffsetScaled, vecOffsetStep);
		
		if(	!bIgnoreCollisionWithEnts &&
			!gConf.bNoEntityCollision &&
			!mm->bg.flags.noCollision &&
			mmCollideWithOthersBG(mm, mm->bg.pos, vecOffsetStep, addedCapsuleRadius, erLurchTarget, secondAttempt))
		{
			collided = true;
			if (collidedWithEntOut) {
				*collidedWithEntOut = 1;
			}
		}

		if (secondAttempt &&
			nearSameVec3(vecOffsetStep, vecOffsetStepCache))
		{
			// when this happens, there's no point to re-running an identical worldMoveMe, we've likely hit case (a) outlined below
			//printfColor(COLOR_GREEN,"Taking bypass!\n");
			goto bypassRecomputingWorldMoveMe;
		}

		copyVec3(mm->bg.pos, motion.last_pos);
		addVec3(mm->bg.pos, vecOffsetStep, motion.pos);
		copyVec3(vecOffsetStep, motion.vel);

		motion.filterBits = WC_QUERY_BITS_ENTITY_MOVEMENT;
		
		if(!mmGetWorldCollGridCellBG(mm, motion.last_pos, &motion.wcCell)){
			break;
		}

		PERFINFO_AUTO_START("worldMoveMe", 1);
			worldMoveMe(&motion);
		PERFINFO_AUTO_STOP();

		if (collided						&&
			!secondAttempt					&&
			!vec3IsZero(vecOffsetScaled)	&&
			nearSameVec3(motion.pos, mm->bg.pos))
		{
			// when this happens, either
			// (a) a collision capsule is preventing us from entering it at an angle almost perfectly ortogonal to the capsule's surface at the point of collision
			// (b) a collision capsule is trying to force us out of it, but the world geometry is preventing it from doing so
			// we'd like to redo the worldMoveMe math for case (b) such that we can escape but not case (a)
			// which case we're observing won't be known until after we've rechecked the capsule to capsule collisions
			//printfColor(COLOR_RED,"%i : attempting redo : %f %f %f\n",++iCount,vecParamsXYZ(vecOffsetScaled));
			secondAttempt = true;
			collided = false;
			copyVec3(vecOffsetStep, vecOffsetStepCache);
			goto retryMovementWithoutAwayFromCapsuleCollision;
		}

bypassRecomputingWorldMoveMe:

		MM_CHECK_DYNPOS_DEVONLY(motion.pos);

		copyVec3(	motion.pos,
					mm->bg.posMutable);

		if(motion.hit_ground){
			mmNotifyHitSurface(	mm,
								motion.pos,
								motion.ground_normal, 
								1);
		}
		
		// If the normal is the same as the ground, ignore the surface hit.

		if(	motion.hit_surface &&
			(	!motion.hit_ground ||
				!nearSameVec3(motion.ground_normal, motion.surface_normal))
			)
		{
			mmNotifyHitSurface(	mm,
								motion.pos,
								motion.surface_normal,
								0);
		}
		
		if(motion.stuck_head == STUCK_COMPLETELY){
			if(isStuckOut){
				*isStuckOut = 1;
			}
			break;
		}
	}
	FOR_END;

	if(isStuckOut){
		*isStuckOut = 0;
	}
}

S32 mrmTranslatePositionBG(	const MovementRequesterMsg* msg,
							const Vec3 vecOffset,
							S32 useController,
							S32 disableStickyGround)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;
	MovementThreadData*					td;
	S32									collidedWithOthers = 0;
	Vec3								posOld;

	if(	!pd ||
		pd->msgType != MR_MSG_BG_CREATE_OUTPUT ||
		pd->in.bg.createOutput.dataClassBit != MDC_BIT_POSITION_CHANGE ||
		vec3IsZero(vecOffset) ||
		!FINITEVEC3(vecOffset))
	{
		return 0;
	}

	PERFINFO_AUTO_START_FUNC();

	mm = pd->mm;
	td = MM_THREADDATA_BG(mm);
	copyVec3(mm->bg.pos, posOld);
	
	if(useController){
		F32 fAddedCapsuleRadius = 0.f;
		EntityRef erLurchTarget = 0;

		mrLog(	pd->mr,
				NULL,
				"Moving by controller:\n"
				"pos (%1.3f, %1.3f, %1.3f) [%8.8x, %8.8x, %8.8x]\n"
				"vecOffset (%1.3f, %1.3f, %1.3f) [%8.8x, %8.8x, %8.8x]",
				vecParamsXYZ(mm->bg.pos),
				vecParamsXYZ((S32*)mm->bg.pos),
				vecParamsXYZ(vecOffset),
				vecParamsXYZ((S32*)vecOffset));

		if(MMLOG_IS_ENABLED(mm)){
			Vec3 target;

			addVec3(mm->bg.pos,
					vecOffset,
					target);

			mmLogSegment(	mm,
							NULL,
							"bg.translate",
							0,
							mm->bg.pos,
							target);
		}
		
		mrmGetLurchInfoBG(msg, &erLurchTarget, &fAddedCapsuleRadius);

		moveItEx(	mm, 
					vecOffset, 
					20, 
					fAddedCapsuleRadius, 
					mm->bg.flags.mrIgnoresCollisionWithEnts, 
					disableStickyGround,
					erLurchTarget,
					NULL,
					&collidedWithOthers);
	
		if(	collidedWithOthers &&
			mm->bg.flags.mrHandlesCollidedEntMsg)
		{
			MovementRequester* mrTarget = pd->mm->bg.dataOwner[MDC_POSITION_TARGET];
	
			if(mrTarget){
				MovementRequesterMsgPrivateData pdOut;

				mmRequesterMsgInitBG(	&pdOut,
										NULL,
										mrTarget,
										MR_MSG_BG_COLLIDED_ENT);

				mmRequesterMsgSend(&pdOut);
			}
		}
	}else{
		Vec3 vTemp;

		mrLog(	pd->mr,
				NULL,
				"Moving directly:\n"
				" p(%1.3f, %1.3f, %1.3f) [%8.8x, %8.8x, %8.8x]\n"
				" v(%1.3f, %1.3f, %1.3f) [%8.8x, %8.8x, %8.8x]",
				vecParamsXYZ(mm->bg.pos),
				vecParamsXYZ((S32*)mm->bg.pos),
				vecParamsXYZ(vecOffset),
				vecParamsXYZ((S32*)vecOffset));

		addVec3(mm->bg.pos, vecOffset, vTemp);
		MM_CHECK_DYNPOS_DEVONLY(vTemp);
		copyVec3(vTemp, mm->bg.posMutable);

		//wcControllerSetPos(	mm->wcc,
		//					mm->bg.pos);
	}

	if(MMLOG_IS_ENABLED(mm)){
		Vec3 diff;
		Vec3 miss;

		subVec3(mm->bg.pos, posOld, diff);
		subVec3(diff, vecOffset, miss);

		mrLog(	pd->mr,
				NULL,
				"New position:\n"
				" p(%1.3f, %1.3f, %1.3f) [%8.8x, %8.8x, %8.8x]\n"
				" diff(%1.3f, %1.3f, %1.3f) [%8.8x, %8.8x, %8.8x]\n"
				" miss(%1.3f, %1.3f, %1.3f) [%8.8x, %8.8x, %8.8x]",
				vecParamsXYZ(mm->bg.pos),
				vecParamsXYZ((S32*)mm->bg.pos),
				vecParamsXYZ(diff),
				vecParamsXYZ((S32*)diff),
				vecParamsXYZ(miss),
				vecParamsXYZ((S32*)miss));
	}

	mm->bg.flagsMutable.viewChanged = 1;

	mmVerifyViewStatusToFG(mm);

	if(TRUE_THEN_RESET(mm->bg.flagsMutable.posIsAtRest)){
		td->toFG.flagsMutable.posIsAtRest = 0;

		if(FALSE_THEN_SET(td->toFG.flagsMutable.viewStatusChanged)){
			#if MM_VERIFY_TOFG_VIEW_STATUS
				td->toFG.frameWhenViewStatusChanged = mgState.frameCount;
			#endif

			td->toFG.flagsMutable.rotIsAtRest = mm->bg.flags.rotIsAtRest;
			td->toFG.flagsMutable.pyFaceIsAtRest = mm->bg.flags.pyFaceIsAtRest;
			mmSetAfterSimWakesOnceBG(mm);
		}
	}

	mmVerifyViewStatusToFG(mm);

	mmSetHasChangedOutputDataRecentlyBG(mm);

	mmSendMsgPosChangedBG(mm);

	PERFINFO_AUTO_STOP();

	return 1;
}

S32 mrmCheckCollisionWithOthersBG(	const MovementRequesterMsg* msg, 
									const Vec3 vPos, 
									Vec3 vDirInOut,
									F32 addedCapsuleRadius,
									EntityRef erLurchTarget)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(!vDirInOut){
		return 0;
	}

	if(	!pd ||
		!MR_MSG_TYPE_IS_BG(pd->msgType))
	{
		zeroVec3(vDirInOut);
		return 0;
	}
		
	mm = pd->mm; 
	
	return mmCollideWithOthersBG(mm, vPos, vDirInOut, addedCapsuleRadius, erLurchTarget, false);
}

S32 mrmMoveIfCollidingWithOthersBG(const MovementRequesterMsg* msg){
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	Vec3 								posCur;
	Vec3 								dir;
	MovementManager*					mm;
		
	if(	!pd ||
		pd->msgType != MR_MSG_BG_CREATE_OUTPUT ||
		pd->in.bg.createOutput.dataClassBit != MDC_BIT_POSITION_CHANGE)
	{
		return 0;
	}

	mm = pd->mm; 

	if(	mm->bg.flags.noCollision ||
		!pd->mm->bg.flags.isAttachedToClient)
	{
		return 0;
	}
	
	PERFINFO_AUTO_START_FUNC();
		
	mrmGetPositionBG(msg, posCur);
	zeroVec3(dir);
	if(	mmCollideWithOthersBG(mm, posCur, dir, 0, 0, false) &&
		lengthVec3Squared(dir) >= SQR(0.001f))
	{
		const Vec3 vecTinyMove = {0.f, 0.001f, 0.f};
		
		mrmTranslatePositionBG(msg, vecTinyMove, 1, 0);
		PERFINFO_AUTO_STOP();
		return 1;
	}

	PERFINFO_AUTO_STOP();
	return 0;
}

S32 mrmSetStepIsNotInterpedBG(const MovementRequesterMsg* msg){
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;
	MovementThreadData*					td;

	if(	!pd ||
		pd->msgType != MR_MSG_BG_CREATE_OUTPUT ||
		pd->in.bg.createOutput.dataClassBit != MDC_BIT_POSITION_CHANGE)
	{
		return 0;
	}

	mm = pd->mm;
	
	td = MM_THREADDATA_BG(mm);

	mrLog(	pd->mr,
			NULL,
			"Setting output not interped.");

	pd->o->flagsMutable.notInterped = 1;

	return 1;
}

S32 mrmSetPositionBG(	const MovementRequesterMsg* msg,
						const Vec3 pos)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;
	MovementThreadData*					td;

	if(	!pd ||
		pd->msgType != MR_MSG_BG_CREATE_OUTPUT ||
		pd->in.bg.createOutput.dataClassBit != MDC_BIT_POSITION_CHANGE ||
		!FINITEVEC3(pos))
	{
		return 0;
	}

	mm = pd->mm;
	
	if(sameVec3(pos, mm->bg.pos)){
		return 0;
	}

	td = MM_THREADDATA_BG(mm);

	mrLog(	pd->mr,
			NULL,
			"Setting position:"
			" p(%1.2f, %1.2f, %1.2f)"
			" p[%8.8x, %8.8x, %8.8x]",
			vecParamsXYZ(pos),
			vecParamsXYZ((S32*)pos));

	MM_CHECK_DYNPOS_DEVONLY(pos);

	copyVec3(	pos,
				mm->bg.posMutable);

	mm->bg.flagsMutable.viewChanged = 1;

	mmVerifyViewStatusToFG(mm);

	if(TRUE_THEN_RESET(mm->bg.flagsMutable.posIsAtRest)){
		td->toFG.flagsMutable.posIsAtRest = 0;

		if(FALSE_THEN_SET(td->toFG.flagsMutable.viewStatusChanged)){
			#if MM_VERIFY_TOFG_VIEW_STATUS
				td->toFG.frameWhenViewStatusChanged = mgState.frameCount;
			#endif

			td->toFG.flagsMutable.rotIsAtRest = mm->bg.flags.rotIsAtRest;
			td->toFG.flagsMutable.pyFaceIsAtRest = mm->bg.flags.pyFaceIsAtRest;
			mmSetAfterSimWakesOnceBG(mm);
		}
	}
	
	mmVerifyViewStatusToFG(mm);

	mmSetHasChangedOutputDataRecentlyBG(mm);

	mmSendMsgPosChangedBG(mm);

	return 1;
}

S32 mrmSetRotationBG(	const MovementRequesterMsg* msg,
						const Quat rot)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;
	MovementThreadData*					td;

	if(	!pd ||
		pd->msgType != MR_MSG_BG_CREATE_OUTPUT ||
		pd->in.bg.createOutput.dataClassBit != MDC_BIT_ROTATION_CHANGE ||
		!FINITEVEC4(rot))
	{
		return 0;
	}
	
	mm = pd->mm;
	
	if(sameQuat(rot, mm->bg.rot)){
		return 0;
	}
	
	devassertmsgf(	quatIsNormalized(rot),
					"Invalid rot length %1.3f"
					" (%1.3f, %1.3f, %1.3f, %1.3f)"
					" [%8.8x, %8.8x, %8.8x, %8.8x]"
					" set by %s.",
					lengthVec4(rot),
					quatParamsXYZW(rot),
					quatParamsXYZW((S32*)rot),
					pd->mr->mrc->name);

	td = MM_THREADDATA_BG(mm);

	mrLog(	pd->mr,
			NULL,
			"Setting rotation:"
			" r(%1.2f, %1.2f, %1.2f, %1.2f)"
			" r[%8.8x, %8.8x, %8.8x, %8.8x]",
			quatParamsXYZW(rot),
			quatParamsXYZW((S32*)rot));

	copyQuat(rot, mm->bg.rotMutable);

	mm->bg.flagsMutable.viewChanged = 1;

	mmVerifyViewStatusToFG(mm);

	if(TRUE_THEN_RESET(mm->bg.flagsMutable.rotIsAtRest)){
		td->toFG.flagsMutable.rotIsAtRest = 0;

		if(FALSE_THEN_SET(td->toFG.flagsMutable.viewStatusChanged)){
			#if MM_VERIFY_TOFG_VIEW_STATUS
				td->toFG.frameWhenViewStatusChanged = mgState.frameCount;
			#endif

			td->toFG.flagsMutable.posIsAtRest = mm->bg.flags.posIsAtRest;
			td->toFG.flagsMutable.pyFaceIsAtRest = mm->bg.flags.pyFaceIsAtRest;
			mmSetAfterSimWakesOnceBG(mm);
		}
	}

	mmVerifyViewStatusToFG(mm);

	mmSetHasChangedOutputDataRecentlyBG(mm);

	return 1;
}

S32 mrmSetFacePitchYawBG(	const MovementRequesterMsg* msg,
							const Vec2 pyFace)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;
	MovementThreadData*					td;

	if(	!pd ||
		pd->msgType != MR_MSG_BG_CREATE_OUTPUT ||
		pd->in.bg.createOutput.dataClassBit != MDC_BIT_ROTATION_CHANGE ||
		!FINITEVEC2(pyFace))
	{
		return 0;
	}

	mm = pd->mm;
	
	if(sameVec2(pyFace, mm->bg.pyFace)){
		return 0;
	}

	td = MM_THREADDATA_BG(mm);

	mrLog(	pd->mr,
			NULL,
			"Setting face pitch and yaw:"
			" (%1.2f, %1.2f)"
			" [%8.8x, %8.8x]",
			vecParamsXY(pyFace),
			vecParamsXY((S32*)pyFace));

	copyVec2(pyFace, mm->bg.pyFaceMutable);

	mm->bg.flagsMutable.viewChanged = 1;

	mmVerifyViewStatusToFG(mm);

	if(TRUE_THEN_RESET(mm->bg.flagsMutable.pyFaceIsAtRest)){
		td->toFG.flagsMutable.pyFaceIsAtRest = 0;

		if(FALSE_THEN_SET(td->toFG.flagsMutable.viewStatusChanged)){
			#if MM_VERIFY_TOFG_VIEW_STATUS
				td->toFG.frameWhenViewStatusChanged = mgState.frameCount;
			#endif

			td->toFG.flagsMutable.posIsAtRest = mm->bg.flags.posIsAtRest;
			td->toFG.flagsMutable.rotIsAtRest = mm->bg.flags.rotIsAtRest;
			mmSetAfterSimWakesOnceBG(mm);
		}
	}
	
	mmVerifyViewStatusToFG(mm);

	mmSetHasChangedOutputDataRecentlyBG(mm);

	return 1;
}

S32 mrmProcessCountHasPassedBG(	const MovementRequesterMsg* msg,
								U32 processCount)
{
	MovementRequesterMsgPrivateData* pd = MR_MSG_TO_PD(msg);

	if(	!pd ||
		!MR_MSG_TYPE_IS_BG(pd->msgType))
	{
		return 0;
	}
	
	return subS32(	mgState.bg.pc.local.cur +
						mgState.bg.netReceive.cur.offset.clientToServer,
					processCount) >= 0;
}

S32 mrmProcessCountPlusSecondsHasPassedBG(	const MovementRequesterMsg* msg,
											U32 processCount,
											F32 seconds)
{
	return mrmProcessCountHasPassedBG(	msg,
										processCount +
											(U32)(seconds * MM_PROCESS_COUNTS_PER_SECOND));
}

S32	mrmGetProcessCountBG(	const MovementRequesterMsg* msg,
							U32* processCountOut)
{
	MovementRequesterMsgPrivateData* pd = MR_MSG_TO_PD(msg);

	if(	!pd ||
		!MR_MSG_TYPE_IS_BG(pd->msgType) ||
		!processCountOut)
	{
		return 0;
	}
	
	*processCountOut =	mgState.bg.pc.local.cur +
						mgState.bg.netReceive.cur.offset.clientToServer;

	return 1;
}

S32 mrmNeedsSimBodyCreateBG(const MovementRequesterMsg* msg){
	MovementRequesterMsgPrivateData* pd = MR_MSG_TO_PD(msg);

	if(	!pd ||
		pd->msgType != MR_MSG_BG_CREATE_OUTPUT)
	{
		return 0;
	}
	
	mmBodyLockEnter();
	{
		eaPushUnique(&mgState.bg.mrsThatNeedSimBodyCreate, pd->mr);
	}
	mmBodyLockLeave();
	
	return 1;
}

static void mmPSDKActorDescAddCapsule(	PSDKActorDesc* actorDesc,
										const Capsule* c,
										U32 materialIndex,
										F32 density)
{
#if !PSDK_DISABLED
	Mat4			matPart;
	Vec3			pyr;

	getVec3YP(c->vDir, &pyr[1], &pyr[0]);
	pyr[0] = subAngle(RAD(90), pyr[0]);
	pyr[1] = addAngle(pyr[1], PI);
	pyr[2] = 0;
	createMat3YPR(matPart, pyr);
	
	copyVec3(c->vStart, matPart[3]);
	scaleAddVec3(matPart[1], c->fLength * 0.5, matPart[3], matPart[3]);

	psdkActorDescAddCapsule(actorDesc,
							c->fLength,
							c->fRadius,									
							matPart,
							density,
							materialIndex,
							WC_FILTER_BITS_ENTITY,
							WC_SHAPEGROUP_ENTITY);
#endif
}

static void mmSimBodyAllocBG(	MovementManager* mm,
								MovementRequester* mr,
								MovementSimBodyInstance** sbiOut,
								U32* handleOut,
								MovementBody* body)
{
	MovementSimBodyInstance*	sbi;
	U32							handle = 0;

	*sbiOut = sbi = callocStruct(MovementSimBodyInstance);
	
	sbi->mm = mm;
	sbi->mr = mr;
	sbi->body = body;
	
	mmRareLockEnter(mm);
	{
		EARRAY_CONST_FOREACH_BEGIN(mm->bg.simBodyInstances, i, isize);
		{
			if(!mm->bg.simBodyInstances[i]){
				mm->bg.simBodyInstancesMutable[i] = sbi;
				handle = i + 1;
				break;
			}
		}
		EARRAY_FOREACH_END;
		
		if(!handle){
			handle = eaPush(&mm->bg.simBodyInstancesMutable, sbi) + 1;
		}
	}
	mmRareLockLeave(mm);
	
	*handleOut = handle;
}

static void mmCreateSimBodySceneBG(void){
	if(	!mgState.wcScene &&
		!beaconIsBeaconizer())
	{
		wcSceneCreate(mgState.bg.wciMsg, &mgState.wcScene, 0, -RAGDOLL_GRAVITY, "Sim");
	}
}

S32 mrmSimBodyCreateFromIndexBG(const MovementRequesterMsg* msg,
								U32* handleOut,
								U32 bodyIndex,
								U32 materialIndex,
								const Mat4 mat)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;
	MovementBody*						body = NULL;
	MovementSimBodyInstance*			sbi;
	U32									handle = 0;

	if(	!pd ||
		pd->msgType != MR_MSG_BG_SIM_BODIES_DO_CREATE ||
		!handleOut)
	{
		return 0;
	}
	
	mm = pd->mm;

	mmRareLockEnter(mm);
	{
		if(bodyIndex < eaUSize(&mgState.bodies)){
			body = mgState.bodies[bodyIndex];
		}
	}
	mmRareLockLeave(mm);
	
	if(!body){
		return 0;
	}
	
	mmSimBodyAllocBG(mm, pd->mr, &sbi, &handle, body);

	*handleOut = handle;
	
	#if !PSDK_DISABLED
	{
		PSDKBodyDesc	bodyDesc = {0};
		PSDKActorDesc*	actorDesc;
		
		psdkActorDescCreate(&actorDesc);
		
		psdkActorDescSetMat4(actorDesc, mat);
		
		EARRAY_CONST_FOREACH_BEGIN(body->parts, i, isize);
		{
			const MovementBodyPart*	p = body->parts[i];
			Mat4					matPart;
			
			createMat3YPR(matPart, p->pyr);
			copyVec3(p->pos, matPart[3]);
			
			if(!p->geo->cookedMesh.convex){
				PSDKMeshDesc meshDesc = {0};
				
				meshDesc.vertCount = p->geo->mesh.vertCount;
				meshDesc.vertArray = (const Vec3*)p->geo->mesh.verts;

				psdkCookedMeshCreate(	&p->geo->cookedMesh.convex,
										&meshDesc);
			}
			
			psdkActorDescAddMesh(	actorDesc,
									p->geo->cookedMesh.convex,
									matPart,
									1,
									materialIndex,
									WC_FILTER_BITS_ENTITY,
									WC_SHAPEGROUP_ENTITY,
									0);
		}
		EARRAY_FOREACH_END;
		
		EARRAY_CONST_FOREACH_BEGIN(body->capsules, i, isize);
		{
			mmPSDKActorDescAddCapsule(	actorDesc,
										body->capsules[i],
										materialIndex,
										1.0f);
		}
		EARRAY_FOREACH_END;
		
		mmCreateSimBodySceneBG();

		wcActorCreate(	mgState.bg.wciMsg,
						mgState.wcScene,
						&sbi->wcActor,
						sbi,
						actorDesc,
						&bodyDesc,
						0,
						0,
						0);

		psdkActorDescDestroy(&actorDesc);
	}
	#endif

	mmBodyLockEnter();
	{
		eaPush(&mgState.bg.simBodyInstancesMutable, sbi);
	}
	mmBodyLockLeave();

	return 1;
}

S32 mrmSimBodyCreateFromCapsuleBG(	const MovementRequesterMsg* msg,
									U32* handleOut,
									const Capsule* capsule,
									U32 materialIndex,
									F32 density,
									const Mat4 mat)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;
	MovementSimBodyInstance*			sbi;
	U32									handle = 0;

	if(	!pd ||
		pd->msgType != MR_MSG_BG_SIM_BODIES_DO_CREATE ||
		!handleOut)
	{
		return 0;
	}
	
	mm = pd->mm;

	mmSimBodyAllocBG(mm, pd->mr, &sbi, &handle, NULL);

	*handleOut = handle;
	
	#if !PSDK_DISABLED
	{
		PSDKBodyDesc	bodyDesc = {0};
		PSDKActorDesc*	actorDesc;
		
		psdkActorDescCreate(&actorDesc);
		
		psdkActorDescSetMat4(actorDesc, mat);
		
		mmPSDKActorDescAddCapsule(	actorDesc,
									capsule,
									materialIndex,
									density);

		mmCreateSimBodySceneBG();

		wcActorCreate(	mgState.bg.wciMsg,
						mgState.wcScene,
						&sbi->wcActor,
						sbi,
						actorDesc,
						&bodyDesc,
						0,
						0,
						0);

		psdkActorDescDestroy(&actorDesc);
	}
	#endif

	mmBodyLockEnter();
	{
		eaPush(&mgState.bg.simBodyInstancesMutable, sbi);
	}
	mmBodyLockLeave();

	return 1;
}

S32 mrmSimBodyCreateFromBoxBG(	const MovementRequesterMsg* msg,
								U32* handleOut,
								const Vec3 xyzSizeBox,
								const Mat4 matLocalBox,
								U32 materialIndex,
								F32 density,
								const Mat4 mat)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;
	MovementSimBodyInstance*			sbi;
	U32									handle = 0;

	if(	!pd ||
		pd->msgType != MR_MSG_BG_SIM_BODIES_DO_CREATE ||
		!handleOut)
	{
		return 0;
	}
	
	mm = pd->mm;

	mmSimBodyAllocBG(mm, pd->mr, &sbi, &handle, NULL);

	*handleOut = handle;
	
	#if !PSDK_DISABLED
	{
		PSDKBodyDesc	bodyDesc = {0};
		PSDKActorDesc*	actorDesc;
		
		psdkActorDescCreate(&actorDesc);
		
		psdkActorDescSetMat4(actorDesc, mat);
		
		psdkActorDescAddBox(actorDesc,
							xyzSizeBox,
							matLocalBox,
							density,
							materialIndex,
							WC_FILTER_BITS_ENTITY,
							WC_SHAPEGROUP_ENTITY);
		
		mmCreateSimBodySceneBG();

		wcActorCreate(	mgState.bg.wciMsg,
						mgState.wcScene,
						&sbi->wcActor,
						sbi,
						actorDesc,
						&bodyDesc,
						0,
						0,
						0);

		psdkActorDescDestroy(&actorDesc);
	}
	#endif

	mmBodyLockEnter();
	{
		eaPush(&mgState.bg.simBodyInstancesMutable, sbi);
	}
	mmBodyLockLeave();

	return 1;
}

S32 mrmSimBodyDestroyBG(const MovementRequesterMsg* msg,
						U32* handleInOut)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;
	MovementSimBodyInstance*			sbi;
	U32									handle;
	
	if(	!pd
		||
		!handleInOut
		||
		!MR_MSG_TYPE_IS_BG(pd->msgType))
	{
		return 0;
	}
	
	mm = pd->mm;
	
	handle = *handleInOut - 1;
	*handleInOut = 0;
	
	if(handle >= eaUSize(&mm->bg.simBodyInstances)){
		return 0;
	}
	
	if(	pd->msgType == MR_MSG_BG_INIT_REPREDICT_SIM_BODY &&
		pd->in.bg.initRepredictSimBody.handle == handle + 1)
	{
		pd->in.bg.initRepredictSimBody.handle = 0;
	}

	sbi = mm->bg.simBodyInstances[handle];
	
	if(	!sbi ||
		sbi->mr != pd->mr)
	{
		return 0;
	}
	
	mmRareLockEnter(mm);
	{
		mm->bg.simBodyInstancesMutable[handle] = NULL;
	
		mmReclaimSimBodyInstanceHandlesBG(mm);
	}
	mmRareLockLeave(mm);
	
	sbi->mr = NULL;
		
	mmSimBodyInstanceDestroyBG(sbi);
	
	return 1;
}

static S32 mrmSimBodyGetFromHandleBG(	const MovementRequesterMsg* msg,
										U32 handle,
										MovementSimBodyInstance** sbiOut)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;
	MovementSimBodyInstance*			sbi;

	if(	!pd ||
		!handle ||
		!MR_MSG_TYPE_IS_BG(pd->msgType))
	{
		return 0;
	}
	
	mm = pd->mm;
	
	if(handle > eaUSize(&mm->bg.simBodyInstances)){
		return 0;
	}
	
	sbi = mm->bg.simBodyInstances[handle - 1];
	
	if(sbi->mr != pd->mr){
		return 0;
	}
	
	*sbiOut = sbi;
	
	return 1;
}

S32 mrmSimBodyGetPSDKActorBG(	const MovementRequesterMsg* msg,
								U32 handle,
								PSDKActor** psdkActorOut)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementSimBodyInstance*			sbi;

	if(!mrmSimBodyGetFromHandleBG(msg, handle, &sbi)){
		return 0;
	}
	
	return wcActorGetPSDKActor(mgState.bg.wciMsg, sbi->wcActor, psdkActorOut);
}

void mmAddToHasPostStepDestroyListBG(MovementManager* mm){
	if(mm->bg.flags.hasPostStepDestroy){
		return;
	}
	
	assert(!mgState.bg.hasPostStepDestroy.flags.managersIsReadOnly);
	
	csEnter(&mgState.bg.hasPostStepDestroy.cs);
	{
		if(FALSE_THEN_SET(mm->bg.flagsMutable.hasPostStepDestroy)){
			eaPush(&mgState.bg.hasPostStepDestroy.managersMutable, mm);
		}
	}
	csLeave(&mgState.bg.hasPostStepDestroy.cs);
}

static S32 mmOverrideValueGroupFindBG(	MovementManager* mm,
										const char* name,
										MovementOverrideValueGroup** movgOut,
										S32 create)
{
	MovementOverrideValueGroup* movg;

	if(!mm->bg.stOverrideValues){
		if(!create){
			return 0;
		}

		mm->bg.stOverrideValues = stashTableCreateWithStringKeys(5, StashDefault);
	}
	else if(stashFindPointer(mm->bg.stOverrideValues, name, movgOut)){
		return 1;
	}
	else if(!create){
		return 0;
	}
	
	*movgOut = movg = callocStruct(MovementOverrideValueGroup);
	
	movg->namePooled = allocAddCaseSensitiveString(name);

	if(!stashAddPointer(mm->bg.stOverrideValues, name, movg, 0)){
		assert(0);
	}
	
	return 1;
}

static void mrmLogOverride(	const MovementRequesterMsg* msg,
							const char* prefix,
							U32 handle,
							const char* name,
							MovementSharedDataType valueType,
							const MovementSharedDataValue* value)
{
	char buffer[100];
	
	mmSharedDataValueGetDebugString(SAFESTR(buffer), name, valueType, value);
	
	if(handle){
		mrmLog(msg, NULL, "%s override handle %u: %s.", prefix, handle, buffer);
	}else{
		mrmLog(msg, NULL, "%s override: %s.", prefix, buffer);
	}
}

static S32 mrmOverrideValueCreateInternalBG(const MovementRequesterMsg* msg,
											U32* movhOut,
											const char* name,
											MovementSharedDataType valueType,
											const MovementSharedDataValue* value)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementOverrideValueGroup*			movg;
	MovementOverrideValue*				mov;
	MovementRequester*					mrRejecter;

	if(	!pd ||
		!MR_MSG_TYPE_IS_BG(pd->msgType) ||
		pd->msgType == MR_MSG_BG_OVERRIDE_VALUE_DESTROYED ||
		!movhOut)
	{
		if(MRMLOG_IS_ENABLED(msg)){
			mrmLogOverride(	msg,
							"Bad create params when creating",
							0,
							name,
							valueType,
							value);
		}

		return 0;
	}

	if(mmSendMsgsOverrideValueShouldRejectBG(	pd->mm,
												pd->mr,
												name,
												valueType,
												value,
												&mrRejecter))
	{
		if(MRMLOG_IS_ENABLED(msg)){
			char buffer[100];

			sprintf(buffer,
					"Rejected by %s[%d] when creating",
					mrRejecter->mrc->name,
					mrRejecter->handle);

			mrmLogOverride(msg, buffer, 0, name, valueType, value);
		}

		return 0;
	}

	mmOverrideValueGroupFindBG(pd->mm, name, &movg, 1);

	mov = callocStruct(MovementOverrideValue);
	
	*movhOut = mov->handle = ++pd->mr->bg.overrideHandlePrev;
	mov->movg = movg;
	mov->mr = pd->mr;
	mov->valueType = valueType;
	mov->value = *value;
	
	eaPush(&movg->movsMutable, mov);
	
	if(MRMLOG_IS_ENABLED(msg)){
		mrmLogOverride(msg, "Created", mov->handle, name, valueType, value);
	}

	mmSendMsgsOverrideValueSetBG(mov->mr->mm, mov);

	return 1;
}

S32 mrmOverrideValueCreateF32BG(const MovementRequesterMsg* msg,
								U32* movhOut,
								const char* name,
								F32 value)
{
	MovementSharedDataValue	msdv;

	msdv.f32 = value;

	return mrmOverrideValueCreateInternalBG(msg, movhOut, name, MSDT_F32, &msdv);
}

S32 mrmOverrideValueCreateS32BG(const MovementRequesterMsg* msg,
								U32* movhOut,
								const char* name,
								S32 value)
{
	MovementSharedDataValue	msdv;

	msdv.s32 = value;

	return mrmOverrideValueCreateInternalBG(msg, movhOut, name, MSDT_S32, &msdv);
}

S32 mrmOverrideValueDestroyBG(	const MovementRequesterMsg* msg,
								U32* movhInOut,
								const char* name)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementOverrideValueGroup*			movg;
	U32									handle = SAFE_DEREF(movhInOut);
	MovementManager*					mm;

	if(	!pd ||
		!MR_MSG_TYPE_IS_BG(pd->msgType) ||
		!handle ||
		!mmOverrideValueGroupFindBG(pd->mm, name, &movg, 0))
	{
		return 0;
	}

	mm = pd->mm;

	EARRAY_CONST_FOREACH_BEGIN(movg->movs, i, isize);
	{
		MovementOverrideValue* mov = movg->movs[i];
		
		if(	mov->mr != pd->mr &&
			mov->handle != handle)
		{
			continue;
		}

		if(MMLOG_IS_ENABLED(mm)){
			mrmLogOverride(msg, "Destroyed", handle, name, mov->valueType, &mov->value);
		}

		eaRemove(&movg->movsMutable, i);
		isize--;
		SAFE_FREE(mov);

		if(	!i &&
			!isize)
		{
			// No more so remove the entire group.

			stashRemovePointer(	mm->bg.stOverrideValues,
								name,
								NULL);

			eaDestroy(&movg->movsMutable);
			SAFE_FREE(movg);

			mmSendMsgsOverrideValueUnsetBG(mm, name);
		}
		else if(i == isize){
			// Just removed the latest value, so to the previous latest value.

			mov = movg->movs[isize - 1];
			if(MMLOG_IS_ENABLED(mm)){
				mrmLogOverride(msg, "Setting", handle, name, mov->valueType, &mov->value);
			}
			mmSendMsgsOverrideValueSetBG(mm, mov);
		}

		return 1;
	}
	EARRAY_FOREACH_END;

	return 0;
}

S32 mrmOverrideValueDestroyAllBG(	const MovementRequesterMsg* msg,
									const char* name)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementOverrideValueGroup*			movg;

	if(	!pd ||
		!MR_MSG_TYPE_IS_BG(pd->msgType) ||
		!mmOverrideValueGroupFindBG(pd->mm, name, &movg, 0))
	{
		return 0;
	}

	// Remove from the st first in case this is called recursively somehow.

	if(!stashRemovePointer(pd->mm->bg.stOverrideValues, name, NULL)){
		assert(0);
	}

	while(eaSize(&movg->movs)){
		MovementOverrideValue*	mov = movg->movs[0];
		
		eaRemove(&movg->movsMutable, 0);

		mrSendMsgsOverrideValueDestroyedBG(	mov->mr,
											mov->handle);

		SAFE_FREE(mov);
	}

	eaDestroy(&movg->movsMutable);
	SAFE_FREE(movg);

	mmSendMsgsOverrideValueUnsetBG(	pd->mm,
									name);

	return 1;
}

S32 mrmNeedsPostStepMsgBG(const MovementRequesterMsg* msg){
	MovementRequesterMsgPrivateData* pd = MR_MSG_TO_PD(msg);
	
	if(	!pd ||
		!MR_MSG_TYPE_IS_BG(pd->msgType))
	{
		return 0;
	}
	
	mrNeedsPostStepMsgBG(pd->mr);

	return 1;
}

static void mmRequesterSendMsgPipeCreatedBG(MovementManager* mm,
											const MovementRequesterPipe* mrp)
{
	MovementRequesterMsgPrivateData pd;
	MovementManager*				mmSource = mrp->mrSource->mm;

	mrLog(	mrp->mrTarget,
			NULL,
			"Sending msg PIPE_CREATED (er 0x%x, mr %u, mrc %u).",
			mmSource->entityRef,
			mrp->mrSource->handle,
			mrp->mrSource->mrc->id);

	mmRequesterMsgInitBG(	&pd,
							NULL,
							mrp->mrTarget,
							MR_MSG_BG_PIPE_CREATED);
	
	pd.msg.in.bg.pipeCreated.source.er = mmSource->entityRef;
	pd.msg.in.bg.pipeCreated.source.mrClassID = mrp->mrSource->mrc->id;
	pd.msg.in.bg.pipeCreated.source.mrHandle = mrp->mrSource->handle;

	mmRequesterMsgSend(&pd);
}

S32	mrmPipeCreateBG(const MovementRequesterMsg* msg,
					MovementRequesterPipeHandle* mrphOut,
					U32 erTarget,
					U32 mrClassID,
					U32 mrHandle)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;
	MovementManager*					mmTarget;
	MovementRequester*					mr;
	MovementRequester*					mrTarget = NULL;
	MovementRequesterPipe*				mrp;
	
	if(	!pd
		||
		pd->msgType != MR_MSG_BG_POST_STEP &&
		pd->msgType != MR_MSG_BG_PIPE_CREATED
		||
		!mrphOut)
	{
		return 0;
	}
	
	mm = pd->mm;
	mr = pd->mr;

	if(MMLOG_IS_ENABLED(mm)){
		MovementRequesterClass* mrc = eaGet(&mgState.mr.idToClass, mrClassID);

		mrLog(	mr,
				NULL,
				"Attempting to create a pipe: er 0x%x, mrcID %u (%s), mr %u.",
				erTarget,
				mrClassID,
				mrc ? mrc->name : "INVALID ID",
				mrHandle);
	}
	
	if(!mmGetByEntityRefBG(erTarget, &mmTarget)){
		mrLog(	mr,
				NULL,
				"Failed to find target manager.");

		return 0;
	}

	// Find a matching requester.
	
	EARRAY_CONST_FOREACH_BEGIN(mmTarget->bg.requesters, i, isize);
	{
		MovementRequester* mrCheck = mmTarget->bg.requesters[i];

		if(	mrCheck->mrc->id == mrClassID &&
			(	!mrHandle ||
				mrHandle == mrCheck->handle))
		{
			mrTarget = mrCheck;
			break;
		}
	}
	EARRAY_FOREACH_END;
	
	if(!mrTarget){
		mrLog(	mr,
				NULL,
				"Failed to find target requester.");

		return 0;
	}
	
	// Check that the handle is unique (in case the server runs for a hundred years).
	
	while(1){
		S32 found = 0;

		if(!++mm->bg.prevPipeHandle){
			continue;
		}
		
		EARRAY_CONST_FOREACH_BEGIN(mm->bg.pipes, i, isize);
		{
			const MovementRequesterPipe* mrpCheck = mm->bg.pipes[i];

			if(	mrpCheck->mrSource->mm == mm &&
				mrpCheck->handle == mm->bg.prevPipeHandle)
			{
				found = 1;
			}
		}
		EARRAY_FOREACH_END;
		
		if(!found){
			break;
		}
	}

	mrp = callocStruct(MovementRequesterPipe);
	mrp->handle = mm->bg.prevPipeHandle;
	mrp->mrSource = mr;
	mrp->mrTarget = mrTarget;
	
	eaPush(&mm->bg.pipesMutable, mrp);

	if(mm != mmTarget){
		eaPush(&mmTarget->bg.pipesMutable, mrp);
	}
	
	*mrphOut = mrp->handle;
	
	mrLog(	mr,
			NULL,
			"Successfully created pipe handle %u.",
			mrp->handle);
	
	mmRequesterSendMsgPipeCreatedBG(mmTarget, mrp);

	return 1;
}

static S32 mmRequesterGetPipeBG(MovementManager* mm,
								MovementRequester* mr,
								MovementRequesterPipeHandle mrph,
								MovementRequesterPipe** mrpOut)
{
	EARRAY_CONST_FOREACH_BEGIN(mm->bg.pipes, i, isize);
	{
		MovementRequesterPipe* mrp = mm->bg.pipes[i];
		
		if(	mrp->mrSource == mr &&
			mrp->handle == mrph)
		{
			*mrpOut = mrp;
			return 1;
		}
	}
	EARRAY_FOREACH_END;
	
	return 0;
}

S32	mrmPipeDestroyBG(	const MovementRequesterMsg* msg,
						MovementRequesterPipeHandle* mrphInOut)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;
	MovementRequester*					mr;
	MovementRequesterPipeHandle			mrph = SAFE_DEREF(mrphInOut);
	MovementRequesterPipe*				mrp;
	
	if(	!pd ||
		!MR_MSG_TYPE_IS_BG(pd->msgType) ||
		!mrph)
	{
		return 0;
	}
	
	mm = pd->mm;
	mr = pd->mr;
	
	*mrphInOut = 0;
	
	if(!mmRequesterGetPipeBG(mm, mr, mrph, &mrp)){
		return 0;
	}
	
	mrLog(	mr,
			NULL,
			"Destroying pipe handle %u (target: er 0x%x, mr %u).",
			mrph,
			mrp->mrTarget->mm->entityRef,
			mrp->mrTarget->handle);
			
	if(pd->msgType == MR_MSG_BG_PIPE_CREATED){
		mmPipeDestroyByIndexBG(mm, eaFind(&mm->bg.pipes, mrp));
	}else{
		mmPipeQueueDestroyBG(mm, mrp);
	}

	return 1;
}

S32 mrmPipeSendMsgStringBG(	const MovementRequesterMsg* msg,
							MovementRequesterPipeHandle mrph,
							const char* string)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementRequesterPipe*				mrp;
	
	if(	!pd ||
		!MR_MSG_TYPE_IS_BG(pd->msgType) ||
		!mrph ||
		!mmRequesterGetPipeBG(pd->mm, pd->mr, mrph, &mrp))
	{
		return 0;
	}
	
	if(pd->msgType == MR_MSG_BG_PIPE_MSG){
		MovementRequesterPipeMsg mrpMsg;
		
		mrpMsg.msgType = MR_PIPE_MSG_STRING;
		mrpMsg.string = (char*)string;
		
		mrPipeMsgDeliverBG(mrp, &mrpMsg);
	}else{
		MovementRequesterPipeMsg* mrpMsg;

		mrLog(	mrp->mrSource,
				NULL,
				"Queued sending pipe msg (handle %u, string = \"%s\").",
				mrph,
				string);

		mrLog(	mrp->mrTarget,
				NULL,
				"Queued receiving pipe msg (er %u, mr %u, handle %u, string = \"%s\").",
				mrp->mrSource->mm->entityRef,
				mrp->mrSource->handle,
				mrph,
				string);

		mrpMsg = callocStruct(MovementRequesterPipeMsg);
		
		mrpMsg->msgType = MR_PIPE_MSG_STRING;
		mrpMsg->string = strdup(string);
		
		eaPush(&mrp->msgs, mrpMsg);

		mmPipeNeedsPostStepBG(pd->mm);
	}
	
	return 1;
}

S32	mrmEnableMsgCollidedEntBG(const MovementRequesterMsg* msg, 
							  S32 enabled)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(	!pd || 
		!MR_MSG_TYPE_IS_BG(pd->msgType))
	{
		return 0;
	}

	// TODO (MS): Clean up this (breaks prediction, isn't per-requester).
	// TODO (RP): Find out what the problems with this are, we haven't see any...

	mm = pd->mm;
	mm->bg.flagsMutable.mrHandlesCollidedEntMsg = !!enabled;
	
	return 1;
}

S32	mrmIgnoreCollisionWithEntsBG(	const MovementRequesterMsg* msg, 
									S32 enabled)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(	!pd || 
		!MR_MSG_TYPE_IS_BG(pd->msgType))
	{
		return 0;
	}

	// this pattern is the same as above, need to find out what the issue is
	
	mm = pd->mm;
	mm->bg.flagsMutable.mrIgnoresCollisionWithEnts = !!enabled;

	return 1;
}

S32	mrmSetIsFlyingBG(	const MovementRequesterMsg* msg, 
						S32 bFlying)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(	!pd || 
		!MR_MSG_TYPE_IS_BG(pd->msgType))
	{
		return 0;
	}

	// this pattern is the same as above, need to find out what the issue is

	mm = pd->mm;
	mm->bg.flagsMutable.mrIsFlying = !!bFlying;

	return 1;
}


S32 mrmSetUseRotationForCapsuleOrientationBG(const MovementRequesterMsg* msg, bool enabled)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(	!pd || 
		!MR_MSG_TYPE_IS_BG(pd->msgType))
	{
		return 0;
	}

	// this pattern is the same as above, need to find out what the issue is
	mm = pd->mm;
	enabled = !!enabled;

	if ((bool)mm->bg.flags.capsuleOrientationUseRotation != enabled)
	{
		MovementThreadData*	td = MM_THREADDATA_BG(mm);

		mm->bg.flagsMutable.capsuleOrientationUseRotation = enabled;

		td->toFG.flagsMutable.hasToFG = 1;
		td->toFG.flagsMutable.capsuleOrientationMethodChanged = 1;
		td->toFG.flagsMutable.capsuleOrientationUseRotation = enabled;
	}

	return 1;
}


S32 mrmCheckGroundAheadBG(	const MovementRequesterMsg* msg,
							const Vec3 pos,
							const Vec3 dir)
{
	const F32							STEP_HEIGHT		= 3.f;
	const F32							CAST_DEPTH		= 8.f;
	const F32							CAPSULE_RADIUS	= 2.f;
	const F32							THRESHOLD		= 4.5f;
	Vec3								posStartCast;
	Vec3								posEndCast;
	WorldCollCollideResults			results = {0};
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);
	MovementManager*					mm;

	if(	!pd || 
		!MR_MSG_TYPE_IS_BG(pd->msgType))
	{
		return 0;
	}

	mm = pd->mm;

	if(!mm->space){
		return 0;
	}
	
	scaleAddVec3(dir, CAPSULE_RADIUS, pos, posStartCast);
	copyVec3(posStartCast, posEndCast);
	posStartCast[1] += STEP_HEIGHT;
	posEndCast[1] -= CAST_DEPTH;

	if(	wcRayCollide(	mm->space->wc,
						posStartCast,
						posEndCast,
						WC_QUERY_BITS_WORLD_ALL,
						&results) &&
		pos[1] - results.posWorldImpact[1] <= THRESHOLD)
	{
		return 1;
	}

	return 0;
}

S32 mrmGetWorldCollBG(	const MovementRequesterMsg *msg,
						WorldColl** wcOut)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);

	if(	!pd || 
		!MR_MSG_TYPE_IS_BG(pd->msgType) ||
		!wcOut)
	{
		return 0;
	}

	*wcOut = SAFE_MEMBER(pd->mm->space, wc);
	return !!*wcOut;
}

S32 mrmDoNotRestartPrediction(const MovementRequesterMsg *msg)
{
	MovementRequesterMsgPrivateData*	pd = MR_MSG_TO_PD(msg);

	if(	!pd || 
		!MR_MSG_TYPE_IS_BG(pd->msgType))
	{
		return 0;
	}

	pd->mm->bg.flagsMutable.doNotRestartPrediction = true;

	return 1;
}
