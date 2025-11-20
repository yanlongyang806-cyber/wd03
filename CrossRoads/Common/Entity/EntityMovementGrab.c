/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "EntityMovementGrab.h"
#include "autogen/EntityMovementGrab_h_ast.h"
#include "EntityMovementManager.h"
#include "textparser.h"
#include "mathutil.h"
#include "quat.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Physics););

AUTO_RUN_MM_REGISTER_REQUESTER_MSG_HANDLER(	mrGrabMsgHandler,
											"GrabMovement",
											MRGrab);

AUTO_STRUCT;
typedef struct MRGrabFG {
	MRGrabConfig*					config;
	MRGrabStatus					status;
} MRGrabFG;

AUTO_STRUCT;
typedef struct MRGrabBGFlags {
	U32								doLockedAnim	: 1;
	U32								receivedAck		: 1;
	U32								sentReady		: 1;
} MRGrabBGFlags;

AUTO_STRUCT;
typedef struct MRGrabBG {
	MRGrabConfig*					config;
	U32								spcStartApproach;
	U32								spcStartHold;
	MRGrabBGFlags					flags;
} MRGrabBG;

AUTO_STRUCT;
typedef struct MRGrabLocalBG {
	U32								unused;
	MovementRequesterPipeHandle		mrph;			NO_AST
} MRGrabLocalBG;

AUTO_STRUCT;
typedef struct MRGrabToFGFlags {
	U32								hasStatus		: 1;
} MRGrabToFGFlags;

AUTO_STRUCT;
typedef struct MRGrabToFG {
	MRGrabToFGFlags					flags;
	MRGrabStatus					status;
} MRGrabToFG;

AUTO_STRUCT;
typedef struct MRGrabToBG {
	MRGrabConfig*					config;
} MRGrabToBG;

AUTO_STRUCT;
typedef struct MRGrabSyncFlags {
	U32								done : 1;
} MRGrabSyncFlags;

AUTO_STRUCT;
typedef struct MRGrabSync {
	MRGrabSyncFlags					flags;
} MRGrabSync;

static void mrGrabSendStatusToFG(	const MovementRequesterMsg* msg,
									MRGrabStatus status)
{
	MRGrabToFG* toFG = msg->in.userStruct.toFG;
	
	if(FALSE_THEN_SET(toFG->flags.hasStatus)){
		mrmEnableMsgUpdatedToFG(msg);
	}
	
	toFG->status = status;
}

static void mrGrabReleaseAllBG(	const MovementRequesterMsg* msg,
								MRGrabBG* bg,
								MRGrabLocalBG* localBG)
{
	mrmReleaseAllDataOwnershipBG(msg);
	mrmPipeDestroyBG(msg, &localBG->mrph);
	mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
	bg->flags.doLockedAnim = 0;
	mrmDestroySelf(msg);
}

void mrGrabMsgHandler(const MovementRequesterMsg* msg){
	MRGrabFG*		fg;
	MRGrabBG*		bg;
	MRGrabLocalBG*	localBG;
	MRGrabToFG*		toFG;
	MRGrabToBG*		toBG;
	MRGrabSync*		sync;
	
	MR_MSG_HANDLER_GET_DATA_DEFAULT(msg, MRGrab);

	switch(msg->in.msgType){
		xcase MR_MSG_FG_BEFORE_DESTROY:{
		}
		
		xcase MR_MSG_FG_CREATE_TOBG:{
			if(fg->config){
				assert(!toBG->config);
				toBG->config = fg->config;
				fg->config = NULL;
				
				mrmEnableMsgUpdatedToBG(msg);
			}
		}
		
		xcase MR_MSG_FG_UPDATED_TOFG:{
			if(TRUE_THEN_RESET(toFG->flags.hasStatus)){
				fg->status = toFG->status;
			}
		}
		
		xcase MR_MSG_FG_TRANSLATE_SERVER_TO_CLIENT:{
			MRGrabConfig* c = SAFE_MEMBER(bg, config);

			if(c){
				mmTranslateAnimBitsServerToClient(c->actorSource.animBitHandles,0);
				mmTranslateAnimBitsServerToClient(c->actorTarget.animBitHandles,0);
			}
		}

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
		
		xcase MR_MSG_BG_UPDATED_TOBG:{
			if(toBG->config){
				StructDestroySafe(parse_MRGrabConfig, &bg->config);
				bg->config = toBG->config;
				toBG->config = NULL;
				
				mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
				
				mrmGetProcessCountBG(msg, &bg->spcStartApproach);
			}
		}

		xcase MR_MSG_BG_UPDATED_SYNC:{
		}
		
		xcase MR_MSG_BG_POST_STEP:{
			MRGrabConfig* c = bg->config;
			
			if(!c){
				break;
			}

			if(!localBG->mrph){
				mrmPipeCreateBG(msg,
								&localBG->mrph,
								c->flags.isTarget ?
									c->actorSource.er :
									c->actorTarget.er,
								MR_CLASS_ID_GRAB,
								c->flags.isTarget ?
									c->actorSource.mrHandle :
									c->actorTarget.mrHandle);
			}
		}
		
		xcase MR_MSG_BG_BEFORE_REPREDICT:{
			MRGrabConfig* c = bg->config;
			
			if(!c){
				break;
			}

			localBG->mrph = 0;
			
			mrmPipeCreateBG(msg,
							&localBG->mrph,
							c->actorTarget.er,
							MR_CLASS_ID_GRAB,
							0);
		}
		
		xcase MR_MSG_BG_PIPE_CREATED:{
		}
		
		xcase MR_MSG_BG_PIPE_DESTROYED:{
			// Check if the pipe to the other grab requester was destroyed.

			if(msg->in.bg.pipeDestroyed.handle == localBG->mrph){
				localBG->mrph = 0;
				
				mrGrabReleaseAllBG(msg, bg, localBG);
			}
		}
		
		xcase MR_MSG_BG_PIPE_MSG:{
			MRGrabConfig* c = bg->config;
			
			if(!c){
				break;
			}
			
			switch(msg->in.bg.pipeMsg.msgType){
				xcase MR_PIPE_MSG_STRING:{
					const char* s = msg->in.bg.pipeMsg.string;
					
					if(c->flags.isTarget){
						if(!stricmp(s, "TryLockedAnim")){
							bg->flags.doLockedAnim = 1;
							
							mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
						}
						else if(!stricmp(s, "DoLockedAnim")){
							bg->flags.receivedAck = 1;
							
							mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
						}
						else if(!stricmp(s, "Done")){
							mrGrabReleaseAllBG(msg, bg, localBG);
						}
					}else{
						if(!stricmp(s, "Ready")){
							bg->flags.receivedAck = 1;
							
							mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
							mrmPipeSendMsgStringBG(msg, localBG->mrph, "DoLockedAnim");
						}
					}
				}
			}
		}
		
		xcase MR_MSG_BG_DISCUSS_DATA_OWNERSHIP:{
			MRGrabConfig*	c = bg->config;
			S32				doneDiscussing = 1;
			
			if(!c){
				break;
			}

			if(!localBG->mrph){
				mrmNeedsPostStepMsgBG(msg);
				doneDiscussing = 0;
			}
			
			if(bg->flags.doLockedAnim){
				if(!mrmAcquireDataOwnershipBG(msg, MDC_BITS_CHANGE_ALL, 1, NULL, NULL)){
					mrGrabReleaseAllBG(msg, bg, localBG);
				}else{
					mrGrabSendStatusToFG(msg, MR_GRAB_STATUS_HOLDING);

					if(c->flags.isTarget){
						if(FALSE_THEN_SET(bg->flags.sentReady)){
							mrmPipeSendMsgStringBG(msg, localBG->mrph, "Ready");
						}
					}
				}
			}
			else if(!c->flags.isTarget &&
					!mrmAcquireDataOwnershipBG(msg, MDC_BITS_TARGET_ALL, 1, NULL, NULL))
			{
				doneDiscussing = 0;
			}

			if(doneDiscussing){
				mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
			}
		}
		
		xcase MR_MSG_BG_DATA_WAS_RELEASED:{
			const U32 dataClassBits = msg->in.bg.dataWasReleased.dataClassBits;
			
			mrGrabReleaseAllBG(msg, bg, localBG);
		}

		xcase MR_MSG_BG_CREATE_OUTPUT:{
			MRGrabConfig*	c = bg->config;
			Vec3			pos;
			Vec3			posTarget;
			
			mrmGetPositionBG(msg, pos);
			
			if(!mrmGetEntityPositionBG(	msg,
										c->flags.isTarget ?
											c->actorSource.er :
											c->actorTarget.er,
										posTarget))
			{
				// Target doesn't exist?  Just die I guess.
				
				mrGrabReleaseAllBG(msg, bg, localBG);
				break;
			}

			switch(msg->in.bg.createOutput.dataClassBit){
				xcase MDC_BIT_POSITION_TARGET:{
					if(c->flags.isTarget){
					}else{
						if(bg->flags.doLockedAnim){
							if(mrmProcessCountPlusSecondsHasPassedBG(	msg,
																		bg->spcStartHold,
																		c->secondsToHold))
							{
								mrmLog(	msg,
										NULL,
										"Passed secondsToHold (%1.2f).",
										c->secondsToHold);

								mrmPipeSendMsgStringBG(msg, localBG->mrph, "Done");
								mrGrabReleaseAllBG(msg, bg, localBG);
							}
							
							break;
						}
						
						if(mrmProcessCountPlusSecondsHasPassedBG(	msg,
																	bg->spcStartApproach,
																	c->maxSecondsToReachTarget))
						{
							mrGrabReleaseAllBG(msg, bg, localBG);
							break;
						}
						
						// Check if I'm close enough to activate.
						
						if(	distance3SquaredXZ(pos, posTarget) <= SQR(c->distanceToStartHold) &&
							distanceY(pos, posTarget) <= 2.f)
						{
							if(localBG->mrph){
								mrmLog(msg, NULL, "Attempting locked anim.");

								bg->flags.doLockedAnim = 1;
								bg->flags.receivedAck = 0;
								mrmGetProcessCountBG(msg, &bg->spcStartHold);
								mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
								mrmPipeSendMsgStringBG(msg, localBG->mrph, "TryLockedAnim");
							}
						}else{
							mrmTargetSetAsPointBG(msg, posTarget);
						}
					}
				}
				
				xcase MDC_BIT_POSITION_CHANGE:{
					if(c->flags.isTarget){
						Quat rotTarget;
						Vec2 pyFaceTarget;
						
						if(	mrmGetEntityRotationBG(msg, c->actorSource.er, rotTarget) &&
							mrmGetEntityFacePitchYawBG(msg, c->actorSource.er, pyFaceTarget))
						{
							Vec3 dirFace;
							Vec3 vecNormal;
							
							quatToMat3_1(rotTarget, vecNormal);
							
							createMat3_2_YP(dirFace, pyFaceTarget);
							projectVecOntoPlane(dirFace, vecNormal, dirFace);
							normalVec3(dirFace);
							scaleVec3(dirFace, c->distanceToHold, dirFace);
							
							addVec3(dirFace, posTarget, pos);
							
							mrmSetPositionBG(msg, pos);
						}
					}
				}
				
				xcase MDC_BIT_ROTATION_TARGET:{
					mrmRotationTargetSetAsPointBG(msg, posTarget);
				}

				xcase MDC_BIT_ROTATION_CHANGE:{
					if(c->flags.isTarget){
						Vec3 vecToTarget;
						Vec3 pyr;
						Quat rot;
						
						subVec3(posTarget, pos, vecToTarget);

						vecToTarget[1] = 0.f;

						getVec3YP(vecToTarget, &pyr[1], &pyr[0]);
						pyr[2] = 0.f;
						
						mrmSetFacePitchYawBG(msg, pyr);
						
						PYRToQuat(pyr, rot);
						mrmSetRotationBG(msg, rot);
					}
				}
				
				xcase MDC_BIT_ANIMATION:{
					if(mgPublic.isServer){
						MRGrabActor* a = c->flags.isTarget ?
											&c->actorTarget :
											&c->actorSource;
						
						if(c->flags.isTarget){
							if(!bg->flags.doLockedAnim){
								break;
							}
						}
						else if(!bg->flags.receivedAck){
							break;
						}
						
						EARRAY_INT_CONST_FOREACH_BEGIN(a->animBitHandles, i, isize);
						{
							mrmAnimAddBitBG(msg, a->animBitHandles[i]);
						}
						EARRAY_FOREACH_END;
					}
				}
			}
		}
	}
}

S32 mrGrabCreate(	MovementManager* mm,
					MovementRequester** mrOut)
{
	return mmRequesterCreateBasic(mm, mrOut, mrGrabMsgHandler);
}

#define GET_FG(fg)		if(!MR_GET_FG(mr, mrGrabMsgHandler, MRGrab, fg)){return 0;}
#define GET_SYNC(sync)	if(!MR_GET_SYNC(mr, mrGrabMsgHandler, MRGrab, sync)){return 0;}

S32 mrGrabSetConfig(MovementRequester* mr,
					const MRGrabConfig* config)
{
	MRGrabFG* fg;
	
	if(!config){
		return 0;
	}

	GET_FG(&fg);
	
	mmStructAllocAndCopy(	parse_MRGrabConfig,
							fg->config,
							config,
							0);
	
	MAX1(fg->config->secondsToHold, 0.f);
	MAX1(fg->config->distanceToHold, 0.f);

	mrEnableMsgCreateToBG(mr);
	
	mrForcedSimEnableFG(mr, 1);
	
	fg->status = MR_GRAB_STATUS_CHASE;
	
	return 1;
}

S32 mrGrabGetStatus(MovementRequester* mr,
					MRGrabStatus* statusOut)
{
	MRGrabFG* fg;
	
	if(!statusOut){
		return 0;
	}

	GET_FG(&fg)

	*statusOut = fg->status;
	
	return 1;
}

#include "autogen/EntityMovementGrab_h_ast.c"
#include "autogen/EntityMovementGrab_c_ast.c"