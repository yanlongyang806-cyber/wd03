/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "EntityMovementDead.h"
#include "CombatConfig.h"
#include "EntityMovementManager.h"
#include "GlobalTypes.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Physics););

AUTO_RUN_MM_REGISTER_REQUESTER_MSG_HANDLER(	mrDeadMsgHandler,
											"DeadMovement",
											MRDead);

AUTO_STRUCT;
typedef struct MRDeadToBG {
	S32 unused;
} MRDeadToBG;

AUTO_STRUCT;
typedef struct MRDeadFG {
	S32 unused;
} MRDeadFG;

AUTO_STRUCT;
typedef struct MRDeadBGFlags {
	U32				isCollapsing	: 1;
	U32				isCollapsed		: 1;
	U32				isStandingUp	: 1;
	U32				playAnimDeath	: 1;
	U32				playAnimRevive	: 1;
} MRDeadBGFlags;

AUTO_STRUCT;
typedef struct MRDeadBG {
	U32 			spcStartCollapsing;
	U32 			spcStartStandingUp;
	MRDeadBGFlags	flags;
} MRDeadBG;

AUTO_STRUCT;
typedef struct MRDeadLocalBG {
	U32*			stanceHandles;
} MRDeadLocalBG;

AUTO_STRUCT;
typedef struct MRDeadToFG {
	S32				unused;
} MRDeadToFG;

AUTO_STRUCT;
typedef struct MRDeadSyncFlags {
	U32				isDead : 1;
	U32				fromNearDeath : 1;
	U32				fromKnockback : 1;
} MRDeadSyncFlags;

AUTO_STRUCT;
typedef struct MRDeadSync {
	U32*			stanceBits;
	U32				directionBit;
	MRDeadSyncFlags	flags;
} MRDeadSync;

static void mrDeadUpdateStancesBG(	const MovementRequesterMsg* msg,
									MRDeadBG* bg,
									MRDeadLocalBG* localBG,
									MRDeadSync* sync,
									S32 doCreate)
{
	while(eaiSize(&localBG->stanceHandles)){
		U32 handle = eaiPop(&localBG->stanceHandles);

		mrmAnimStanceDestroyBG(msg, &handle);
	}

	if(doCreate){
		EARRAY_INT_CONST_FOREACH_BEGIN(sync->stanceBits, i, isize);
		{
			U32 handle = 0;

			if(mrmAnimStanceCreateBG(msg, &handle, sync->stanceBits[i])){
				eaiPush(&localBG->stanceHandles, handle);
			}
		}
		EARRAY_FOREACH_END;
	}
}

void mrDeadMsgHandler(const MovementRequesterMsg* msg){
	MRDeadFG*		fg;
	MRDeadBG*		bg;
	MRDeadLocalBG*	localBG;
	MRDeadToFG*		toFG;
	MRDeadToBG*		toBG;
	MRDeadSync*		sync;
	
	MR_MSG_HANDLER_GET_DATA_DEFAULT(msg, MRDead);

	switch(msg->in.msgType){
		xcase MR_MSG_FG_TRANSLATE_SERVER_TO_CLIENT:{
			mmTranslateAnimBitsServerToClient(sync->stanceBits,0);
		}

		xcase MR_MSG_BG_GET_DEBUG_STRING:{
			#define FLAG(x) bg->flags.x ? #x", " : ""
			snprintf_s(	msg->in.bg.getDebugString.buffer,
						msg->in.bg.getDebugString.bufferLen,
						"Flags: %s%s%s%s%s",
						FLAG(isCollapsing),
						FLAG(isCollapsed),
						FLAG(isStandingUp),
						FLAG(playAnimDeath),
						FLAG(playAnimRevive));
			#undef FLAG
		}
		
		xcase MR_MSG_GET_SYNC_DEBUG_STRING:{
			snprintf_s(	msg->in.getSyncDebugString.buffer,
						msg->in.getSyncDebugString.bufferLen,
						"Flags: %s",
						sync->flags.isDead ? "isDead, " : ""
						);
		}
		
		xcase MR_MSG_BG_INITIALIZE:{
			mrmHandledMsgsRemoveBG(	msg,
									MR_HANDLED_MSG_OUTPUT_POSITION_TARGET |
										MR_HANDLED_MSG_OUTPUT_ROTATION_TARGET);
		}
		
		xcase MR_MSG_BG_DATA_WAS_RELEASED:{
			// Lost ownership of something, so just give up and release everything.

			bg->flags.isCollapsing = 0;
			bg->flags.isStandingUp = 0;
			
			mrmReleaseAllDataOwnershipBG(msg);
			mrDeadUpdateStancesBG(msg, bg, localBG, sync, 0);
			
			mrmHandledMsgsRemoveBG(	msg,
									MR_HANDLED_MSG_OUTPUT_ANIMATION |
										MR_HANDLED_MSG_OVERRIDE_VALUE_SHOULD_REJECT);
			
			// If I'm still dead, keep trying to get back ownership.
			
			if(sync->flags.isDead){
				mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
			}else{
				mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
			}
		}

		xcase MR_MSG_BG_UPDATED_SYNC:{
			// Dead state changed.

			if(sync->flags.isDead){
				// Start trying to collapse.

				mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
			}
			else if(TRUE_THEN_RESET(bg->flags.isCollapsing)){
				// Was collapsing, so stand up.

				ASSERT_FALSE_AND_SET(bg->flags.isStandingUp);
				bg->flags.playAnimRevive = 1;

				mrmGetProcessCountBG(msg, &bg->spcStartStandingUp);

				mrmHandledMsgsAddBG(msg,
									MR_HANDLED_MSG_OUTPUT_ANIMATION |
										MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
			}
		}
		
		xcase MR_MSG_BG_OVERRIDE_VALUE_SHOULD_REJECT:{
			// Reject overrides, since you're dead.

			msg->out->bg.overrideValueShouldReject.shouldReject = 1;
		}
		
		xcase MR_MSG_BG_DISCUSS_DATA_OWNERSHIP:{
			S32 done = 1;
			
			if(	sync->flags.isDead &&
				!bg->flags.isCollapsing)
			{
				// Should be collapsing, but might fail to acquire (roll finishing, etc).

				U32 mdcBitsToAcquire = gConf.bNewAnimationSystem ?
											MDC_BITS_TARGET_ALL | MDC_BIT_ANIMATION | MDC_BIT_ROTATION_CHANGE :
											MDC_BITS_TARGET_ALL | MDC_BIT_ROTATION_CHANGE;

				if(!mrmAcquireDataOwnershipBG(msg, mdcBitsToAcquire, 1, NULL, NULL)){
					done = 0;
				}else{
					// Got target control, so start the collapse.

					bg->flags.isStandingUp = 0;
					bg->flags.isCollapsing = 1;
					bg->flags.isCollapsed = 0;

					bg->flags.playAnimDeath = 1;

					if(gConf.bNewAnimationSystem){
						mrmHandledMsgsAddBG(msg,
											MR_HANDLED_MSG_OUTPUT_ANIMATION |
												MR_HANDLED_MSG_OVERRIDE_VALUE_SHOULD_REJECT);
					}else{
						mrmHandledMsgsAddBG(msg,
											MR_HANDLED_MSG_CREATE_DETAILS |
												MR_HANDLED_MSG_OVERRIDE_VALUE_SHOULD_REJECT);
					}

					mrmGetProcessCountBG(msg, &bg->spcStartCollapsing);
					mrmOverrideValueDestroyAllBG(msg, "MaxSpeed");
				}
			}
			
			if(bg->flags.isStandingUp){
				// Check if I'm done standing up.
				
				if(!mrmProcessCountPlusSecondsHasPassedBG(msg, bg->spcStartStandingUp, g_CombatConfig.fDeathRespawnStandUpTime)){
					done = 0;
				}else{
					bg->flags.isStandingUp = 0;

					mrmReleaseAllDataOwnershipBG(msg);
					mrDeadUpdateStancesBG(msg, bg, localBG, sync, 0);

					if(gConf.bNewAnimationSystem){
						mrmHandledMsgsRemoveBG(	msg,
												MR_HANDLED_MSG_OUTPUT_ANIMATION |
													MR_HANDLED_MSG_OVERRIDE_VALUE_SHOULD_REJECT);
					}else{
						mrmHandledMsgsRemoveBG(	msg,
												MR_HANDLED_MSG_CREATE_DETAILS |
													MR_HANDLED_MSG_OVERRIDE_VALUE_SHOULD_REJECT);
					}
				}
			}
			
			if(done){
				mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
			}
		}

		xcase MR_MSG_BG_CREATE_OUTPUT:{
			switch(msg->in.bg.createOutput.dataClassBit){
				xcase MDC_BIT_ANIMATION:{
					assert(gConf.bNewAnimationSystem);

					if(sync->flags.isDead){
						if(TRUE_THEN_RESET(bg->flags.playAnimDeath)){
							mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_OUTPUT_ANIMATION);

							mrDeadUpdateStancesBG(msg, bg, localBG, sync, 1);

							if (sync->flags.fromKnockback) {
								mrmAnimStartBG(msg, mmAnimBitHandles.death_impact, 0);
							} else {
								mrmAnimStartBG(msg, mmAnimBitHandles.death, 0);
							}

							if (sync->directionBit) {
								mrmAnimPlayFlagBG(msg, sync->directionBit, 0);
							}
						}
					}
					else if(TRUE_THEN_RESET(bg->flags.playAnimRevive)){
						mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_OUTPUT_ANIMATION);

						mrmAnimStartBG(msg, mmAnimBitHandles.revive, 0);
						mrmAnimPlayFlagBG(msg, mmAnimBitHandles.exitDeath, 0); // for use on a death template when the animators haven't setup the revive keyword
					}
				}
			}
		}

		xcase MR_MSG_BG_CREATE_DETAILS:{
			assert(!gConf.bNewAnimationSystem);

			if(sync->flags.isDead){
				if(!bg->flags.isCollapsed){
					if(mrmProcessCountPlusSecondsHasPassedBG(msg, bg->spcStartCollapsing, 0.25f)){
						bg->flags.isCollapsed = 1;
					}
				}

				mrmAnimAddBitBG(msg, mmAnimBitHandles.death);

				if(!bg->flags.isCollapsed){
					mrmAnimAddBitBG(msg, mmAnimBitHandles.dying);
				}
			}
		}

		xcase MR_MSG_BG_BEFORE_REPREDICT:{
			if(	gConf.bNewAnimationSystem &&
				bg->flags.isCollapsing)
			{
				mrDeadUpdateStancesBG(msg, bg, localBG, sync, 1);
			}
		}
	}
}

void mrDeadSetEnabled(	MovementRequester* mr,
						S32 enabled)
{
	MRDeadSync* sync;

	if(MR_GET_SYNC(mr, mrDeadMsgHandler, MRDead, &sync)){
		enabled = !!enabled;
		MR_SYNC_SET_IF_DIFF(mr, sync->flags.isDead, (U32)enabled);

		if(!enabled){
			eaiDestroy(&sync->stanceBits);
		}
		
		mrLog(mr, NULL, "Setting %sdead.", enabled ? "" : "NOT ");
	}
}

void mrDeadAddStanceNamesIfDead(MovementRequester* mr,
								const char*const* stanceNames)
{
	MRDeadSync* sync;

	if(	MR_GET_SYNC(mr, mrDeadMsgHandler, MRDead, &sync) &&
		sync->flags.isDead)
	{
		EARRAY_CONST_FOREACH_BEGIN(stanceNames, i, isize);
		{
			mrLog(mr, NULL, "Adding stance %s.", stanceNames[i]);

			eaiPush(&sync->stanceBits,
					mmGetAnimBitHandleByName(stanceNames[i], 0));
		}
		EARRAY_FOREACH_END;
	}
}

void mrDeadSetDirection(MovementRequester *mr,
						const char *pcDirection)
{
	MRDeadSync *sync;

	if (MR_GET_SYNC(mr, mrDeadMsgHandler, MRDead, &sync) &&
		sync->flags.isDead)
	{
		sync->directionBit = pcDirection ? mmGetAnimBitHandleByName(pcDirection, 1) : 0;
	}
}

void mrDeadSetFromNearDeath(MovementRequester *mr,
							S32 fromNearDeath)
{
	MRDeadSync* sync;

	if(MR_GET_SYNC(mr, mrDeadMsgHandler, MRDead, &sync)){
		fromNearDeath = !!fromNearDeath;
		MR_SYNC_SET_IF_DIFF(mr, sync->flags.fromNearDeath, (U32)fromNearDeath);

		mrLog(mr, NULL, "Setting %sfromNearDeath.", fromNearDeath ? "" : "NOT ");
	}
}

void mrDeadSetFromKnockback(MovementRequester *mr,
							S32 fromKnockback)
{
	MRDeadSync* sync;

	if(MR_GET_SYNC(mr, mrDeadMsgHandler, MRDead, &sync)) {
		fromKnockback = !!fromKnockback;
		MR_SYNC_SET_IF_DIFF(mr, sync->flags.fromKnockback, (U32)fromKnockback);

		mrLog(mr, NULL, "Setting %sfromKnockback.", fromKnockback ? "" : "NOT ");
	}
}

U32 mrDeadWasFromNearDeath(MovementRequester *mr)
{
	MRDeadSync* sync;

	if(MR_GET_SYNC(mr, mrDeadMsgHandler, MRDead, &sync)){
		return sync->flags.fromNearDeath;
	}

	return 0;
}

#include "AutoGen/EntityMovementDead_c_ast.c"
