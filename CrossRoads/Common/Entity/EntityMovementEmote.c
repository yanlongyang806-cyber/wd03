/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "EntityMovementEmote.h"
#include "EntityMovementEmote_c_ast.h"
#include "EntityMovementManager.h"
#include "EntityMovementFx.h"
#include "EArray.h"
#include "textparser.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Physics););

AUTO_RUN_MM_REGISTER_REQUESTER_MSG_HANDLER(	mrEmoteMsgHandler,
											"EmoteMovement",
											MREmote);

AUTO_STRUCT;
typedef struct MREmoteToBG {
	MREmoteSet**		setsNew;
	U32*				handlesToDestroy;
} MREmoteToBG;

AUTO_STRUCT;
typedef struct MREmoteFG {
	MREmoteToBG			toBG;
} MREmoteFG;

AUTO_STRUCT;
typedef struct MREmoteBGFlags {
	U32					needsAnimation : 1;
} MREmoteBGFlags;

AUTO_STRUCT;
typedef struct MREmoteBG {
	MREmoteSet**		sets;
	MREmoteBGFlags		flags;
	U32					handleLastStarted;
} MREmoteBG;

AUTO_STRUCT;
typedef struct MREmoteSetState {
	U32*				stanceHandles;
} MREmoteSetState;

AUTO_STRUCT;
typedef struct MREmoteLocalBG {
	MREmoteSetState**	setStates;
} MREmoteLocalBG;

AUTO_STRUCT;
typedef struct MREmoteToFG {
	S32					unused;
} MREmoteToFG;

AUTO_STRUCT;
typedef struct MREmoteSync {
	S32					unused;
} MREmoteSync;

static void mrEmoteSetStateDestroyStancesBG(const MovementRequesterMsg* msg,
											MREmoteSetState* state)
{
	EARRAY_INT_CONST_FOREACH_BEGIN(state->stanceHandles, i, isize);
	{
		mrmAnimStanceDestroyBG(msg, &state->stanceHandles[i]);
	}
	EARRAY_FOREACH_END;
}

static void mrEmoteSetStateCreateStancesBG(	const MovementRequesterMsg* msg,
											const MREmoteSet* set,
											MREmoteSetState* state)
{
	mrEmoteSetStateDestroyStancesBG(msg, state);

	EARRAY_INT_CONST_FOREACH_BEGIN(set->stances, j, jsize);
	{
		U32 handle;
		mrmAnimStanceCreateBG(msg, &handle, set->stances[j]);
		eaiPush(&state->stanceHandles, handle);
	}
	EARRAY_FOREACH_END;
}

static void mrEmoteSetStateDestroyBG(	const MovementRequesterMsg* msg,
										MREmoteSetState** stateInOut)
{
	MREmoteSetState* state = *stateInOut;
	
	if(!state){
		return;
	}

	*stateInOut = NULL;

	mrEmoteSetStateDestroyStancesBG(msg, state);
	StructDestroySafe(parse_MREmoteSetState, &state);
}

static void mrEmoteSetDestroyByIndexBG(	const MovementRequesterMsg* msg,
										MREmoteBG* bg,
										MREmoteLocalBG* localBG,
										U32 index)
{
	MREmoteSetState* state = eaRemove(&localBG->setStates, index);

	mrEmoteSetStateDestroyBG(msg, &state);

	StructDestroySafe(parse_MREmoteSet, &bg->sets[index]);
	eaRemove(&bg->sets, index);

	assert(eaSize(&bg->sets) == eaSize(&localBG->setStates));
}

static void mrEmoteCancelBG(const MovementRequesterMsg* msg,
							MREmoteBG* bg,
							MREmoteLocalBG* localBG)
{
	bg->flags.needsAnimation = 0;

	EARRAY_CONST_FOREACH_BEGIN(bg->sets, i, isize);
	{
		MREmoteSet* set = bg->sets[i];

		if(!set->flags.destroyOnMovement){
			set->flags.played = 0;

			if(set->animToStart){
				bg->flags.needsAnimation = 1;
			}

			continue;
		}

		mrEmoteSetDestroyByIndexBG(msg, bg, localBG, i);
		i--;
		isize--;
	}
	EARRAY_FOREACH_END;

	mrmReleaseAllDataOwnershipBG(msg);

	if(bg->flags.needsAnimation){
		mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
	}
}

void mrEmoteMsgHandler(const MovementRequesterMsg* msg){
	MREmoteFG*		fg;
	MREmoteBG*		bg;
	MREmoteLocalBG*	localBG;
	MREmoteToFG*	toFG;
	MREmoteToBG*	toBG;
	MREmoteSync*	sync;
	
	MR_MSG_HANDLER_GET_DATA_DEFAULT(msg, MREmote);

	switch(msg->in.msgType){
		xcase MR_MSG_BG_GET_DEBUG_STRING:{
			snprintf_s(	msg->in.bg.getDebugString.buffer,
						msg->in.bg.getDebugString.bufferLen,
						""
						);
		}
		
		xcase MR_MSG_GET_SYNC_DEBUG_STRING:{
			snprintf_s(	msg->in.getSyncDebugString.buffer,
						msg->in.getSyncDebugString.bufferLen,
						""
						);
		}
		
		xcase MR_MSG_FG_CREATE_TOBG:{
			assert(!toBG->setsNew);
			assert(!toBG->handlesToDestroy);

			*toBG = fg->toBG;
			ZeroStruct(&fg->toBG);
			
			mrmEnableMsgUpdatedToBG(msg);
		}
		
		xcase MR_MSG_FG_TRANSLATE_SERVER_TO_CLIENT:{
			if(bg){
				EARRAY_CONST_FOREACH_BEGIN(bg->sets, i, isize);
				{
					MREmoteSet* set = bg->sets[i];

					mmTranslateAnimBitServerToClient(&set->animToStart,0);
					mmTranslateAnimBitsServerToClient(set->stances,0);
					mmTranslateAnimBitsServerToClient(set->animBitHandles,0);
					mmTranslateAnimBitsServerToClient(set->flashAnimBitHandles,0);
				}
				EARRAY_FOREACH_END;
			}
		}

		xcase MR_MSG_BG_UPDATED_TOBG:{
			assert(eaSize(&bg->sets) == eaSize(&localBG->setStates));

			// Get the new sets.

			EARRAY_CONST_FOREACH_BEGIN(toBG->setsNew, i, isize);
			{
				MREmoteSet* set = toBG->setsNew[i];

				eaPush(&bg->sets, set);

				if(set->animToStart){
					bg->flags.needsAnimation = 1;
				}

				set->flags.played = 0;

				if(!eaiSize(&set->stances)){
					eaPush(&localBG->setStates, NULL);
				}else{
					MREmoteSetState* state = StructAlloc(parse_MREmoteSetState);

					eaPush(&localBG->setStates, state);
					mrEmoteSetStateCreateStancesBG(msg, set, state);
				}
			}
			EARRAY_FOREACH_END;

			eaDestroy(&toBG->setsNew);
			
			assert(eaSize(&bg->sets) == eaSize(&localBG->setStates));

			// Destroy sets by handle.

			EARRAY_INT_CONST_FOREACH_BEGIN(toBG->handlesToDestroy, i, isize);
			{
				U32 handle = toBG->handlesToDestroy[i];

				EARRAY_CONST_FOREACH_BEGIN(bg->sets, j, jsize);
				{
					if(bg->sets[j]->handle == handle){
						bg->sets[j]->flags.destroyed = 1;
					}
				}
				EARRAY_FOREACH_END;
			}
			EARRAY_FOREACH_END;

			eaiDestroy(&toBG->handlesToDestroy);

			// Add needed messages.

			if(gConf.bNewAnimationSystem){
				U32 handledMsgs =	MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP |
									MR_HANDLED_MSG_OUTPUT_ANIMATION;

				if(eaSize(&bg->sets)){
					mrmHandledMsgsAddBG(msg, handledMsgs);
				}else{
					mrmHandledMsgsRemoveBG(msg, handledMsgs);
				}
			}else{
				U32 handledMsgs =	MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP |
									MR_HANDLED_MSG_CREATE_DETAILS;

				if(eaSize(&bg->sets)){
					mrmHandledMsgsAddBG(msg, handledMsgs);
				}else{
					mrmHandledMsgsRemoveBG(msg, handledMsgs);
				}
			}
		}
		
		xcase MR_MSG_BG_DATA_WAS_RELEASED:{
			mrEmoteCancelBG(msg, bg, localBG);
		}

		xcase MR_MSG_BG_DISCUSS_DATA_OWNERSHIP:{
			// Try once to acquire animation, if not, just give up.

			if(!mrmAcquireDataOwnershipBG(msg, MDC_BIT_ANIMATION, 1, NULL, NULL)){
				mrEmoteCancelBG(msg, bg, localBG);
			}else{
				mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_OUTPUT_ANIMATION);
				mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
			}
		}
		
		xcase MR_MSG_BG_CREATE_DETAILS:{
			assert(!gConf.bNewAnimationSystem);

			mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_CREATE_DETAILS);
			
			// Play flash bits.

			EARRAY_CONST_FOREACH_BEGIN(bg->sets, i, isize);
			{
				MREmoteSet* set = bg->sets[i];

				EARRAY_INT_CONST_FOREACH_BEGIN(set->flashAnimBitHandles, j, jsize);
				{
					mrmAnimAddBitBG(msg, set->flashAnimBitHandles[j]);
				}
				EARRAY_FOREACH_END;

				eaiDestroy(&set->flashAnimBitHandles);
			}
			EARRAY_FOREACH_END;
		}
		
		xcase MR_MSG_BG_CREATE_OUTPUT:{
			assert(msg->in.bg.createOutput.dataClassBit == MDC_BIT_ANIMATION);

			EARRAY_CONST_FOREACH_BEGIN(bg->sets, i, isize);
			{
				MREmoteSet* set = bg->sets[i];

				if(gConf.bNewAnimationSystem){
					if(FALSE_THEN_SET(set->flags.played)){
						mrmAnimStartBG(msg, set->animToStart, 0);
						bg->handleLastStarted = set->handle;
					}
				}else{
					EARRAY_INT_CONST_FOREACH_BEGIN(set->animBitHandles, j, jsize);
					{
						mrmAnimAddBitBG(msg, set->animBitHandles[j]);
					}
					EARRAY_FOREACH_END;
				}

				if(!set->fxHandles){
					EARRAY_CONST_FOREACH_BEGIN(set->fx, j, jsize);
					{
						const MREmoteFX*	fx = set->fx[j];
						MMRFxConstant		c = {0};
						U32					handle = 0;
					
						c.fxName = fx->name;
					
						mmrFxCreateBG(	msg,
										fx->isMaintained ? &handle : NULL,
										&c,
										NULL);
					
						eaiPush(&set->fxHandles, handle);
					}
					EARRAY_FOREACH_END;
				}

				if(set->flags.destroyed){
					if(bg->handleLastStarted == set->handle){
						mrmAnimPlayFlagBG(msg, mmAnimBitHandles.exit, 0);
					}

					mrEmoteSetDestroyByIndexBG(msg, bg, localBG, i);
					i--;
					isize--;
				}
			}
			EARRAY_FOREACH_END;

			if(gConf.bNewAnimationSystem){
				mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_OUTPUT_ANIMATION);
			}

			if(!eaSize(&bg->sets)){
				U32 handledMsgs =	MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP |
									MR_HANDLED_MSG_OUTPUT_ANIMATION |
									MR_HANDLED_MSG_CREATE_DETAILS;

				mrmHandledMsgsRemoveBG(msg, handledMsgs);
			}
		}

		xcase MR_MSG_BG_BEFORE_REPREDICT:{
			while(eaSize(&localBG->setStates)){
				MREmoteSetState* state = eaPop(&localBG->setStates);
				mrEmoteSetStateDestroyBG(msg, &state);
			}
			eaDestroy(&localBG->setStates);

			EARRAY_CONST_FOREACH_BEGIN(bg->sets, i, isize);
			{
				const MREmoteSet* set = bg->sets[i];

				if(!eaiSize(&set->stances)){
					eaPush(&localBG->setStates, NULL);
				}else{
					MREmoteSetState* state = StructAlloc(parse_MREmoteSetState);
					eaPush(&localBG->setStates, state);
					mrEmoteSetStateCreateStancesBG(msg, set, state);
				}
			}
			EARRAY_FOREACH_END;
		}
	}
}

#define GET_FG(fg)	if(!MR_GET_FG(mr, mrEmoteMsgHandler, MREmote, fg)){return 0;}

S32 mrEmoteCreate(	MovementManager* mm,
					MovementRequester** mrOut)
{
	return mmRequesterCreateBasic(mm, mrOut, mrEmoteMsgHandler);
}

S32 mrEmoteSetCreate(	MovementRequester* mr,
						MREmoteSet** setInOut,
						U32* handleOut)
{
	static U32	handleNew;
	MREmoteSet* set = SAFE_DEREF(setInOut);
	MREmoteFG*	fg;

	GET_FG(&fg);

	if(!set){
		return 0;
	}

	assert(!set->fxHandles);

	*setInOut = NULL;

	if(!handleOut){
		set->flags.destroyOnMovement = 1;
	}else{
		while(!++handleNew);
		set->handle = *handleOut = handleNew;
	}

	eaPush(&fg->toBG.setsNew, set);

	mrEnableMsgCreateToBG(mr);

	return 1;
}

S32 mrEmoteSetDestroy(	MovementRequester* mr,
						U32* handleInOut)
{
	U32			handle = SAFE_DEREF(handleInOut);
	MREmoteFG*	fg;

	GET_FG(&fg);

	if(!handle){
		return 0;
	}

	eaiPushUnique(&fg->toBG.handlesToDestroy, handle);

	mrEnableMsgCreateToBG(mr);

	return 1;
}

#include "AutoGen/EntityMovementEmote_c_ast.c"
#include "AutoGen/EntityMovementEmote_h_ast.c"

