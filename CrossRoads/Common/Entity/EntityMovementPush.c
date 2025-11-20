/***************************************************************************
*     Copyright (c) 2005-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

#include "EntityMovementPush.h"
#include "EntityMovementManager.h"
#include "mathutil.h"


AUTO_RUN_MM_REGISTER_REQUESTER_MSG_HANDLER(	mmPushMsgHandler,
											"PushMovement",
											Push);

AUTO_STRUCT;
typedef struct PushToBG {
	Vec3			vel;
	U32				startProcessCount;
} PushToBG;

AUTO_STRUCT;
typedef struct PushFG {
	PushToBG		toBG;
} PushFG;

AUTO_STRUCT;
typedef struct PushBG {
	PushToBG		toBG;
	U32				doStart	: 1;
	U32				started : 1;
} PushBG;

AUTO_STRUCT;
typedef struct PushLocalBG {
	S32 unused;
} PushLocalBG;

AUTO_STRUCT;
typedef struct PushToFG {
	S32 unused;
} PushToFG;

AUTO_STRUCT;
typedef struct PushSync {
	S32 unused;
} PushSync;

void mmPushMsgHandler(const MovementRequesterMsg* msg){
	PushFG*			fg;
	PushBG*			bg;
	PushLocalBG*	localBG;
	PushToFG*		toFG;
	PushToBG*		toBG;
	PushSync*		sync;
	
	MR_MSG_HANDLER_GET_DATA_DEFAULT(msg, Push);

	switch(msg->in.msgType){
		xcase MR_MSG_FG_CREATE_TOBG:{
			mrmEnableMsgUpdatedToBG(msg);
			*toBG = fg->toBG;
		}
		
		xcase MR_MSG_BG_INITIALIZE:{
			const U32 handledMsgs =	MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP;
									
			mrmHandledMsgsAddBG(msg, handledMsgs);
		}

		xcase MR_MSG_BG_GET_DEBUG_STRING:{
			char*	buffer = msg->in.bg.getDebugString.buffer;
			U32		bufferLen = msg->in.bg.getDebugString.bufferLen;

			snprintf_s(	buffer,
						bufferLen,
						" (%1.2f, %1.2f, %1.2f)"
						" [%8.8x, %8.8x, %8.8x]"
						"\n"
						"StartPC: %d,"
						"\n"
						"Flags: %s%s"
						,
						vecParamsXYZ(bg->toBG.vel),
						vecParamsXYZ((S32*)(bg->toBG.vel)),
						bg->toBG.startProcessCount,
						bg->doStart ? "doStart, " : "",
						bg->started ? "started, " : "");
		}

		xcase MR_MSG_BG_DATA_WAS_RELEASED:{
			mrmDestroySelf(msg);
		}
		
		xcase MR_MSG_BG_UPDATED_TOBG:{
			bg->toBG = *toBG;
			bg->doStart = 1;
		}
		
		xcase MR_MSG_BG_DISCUSS_DATA_OWNERSHIP:{
			if(!bg->started){
				if(	bg->doStart &&
					mrmProcessCountHasPassedBG(msg, bg->toBG.startProcessCount))
				{
					bg->doStart = 0;
					
					if(!mrmAcquireDataOwnershipBG(	msg,
													MDC_BITS_TARGET_ALL,// | MDC_BIT_ANIMATION,
													1,
													NULL,
													NULL))
					{
						mrmDestroySelf(msg);
					}
				}
			}
			else// if(!bg->toBG.doStart)
			{
				mrmReleaseAllDataOwnershipBG(msg);
				mrmDestroySelf(msg);
			}
		}
		
		xcase MR_MSG_BG_CREATE_OUTPUT:{
			switch(msg->in.bg.createOutput.dataClassBit){
				xcase MDC_BIT_POSITION_TARGET:{
					if(!bg->started){
						bg->started = 1;
						mrmTargetSetAsVelocityBG(msg, bg->toBG.vel);
						mrmTargetSetSpeedAsImpulseBG(msg);
					}
				}
				
				xcase MDC_BIT_ROTATION_TARGET:{
					Vec3 dir;
					scaleVec3(bg->toBG.vel, -1.f, dir);
					mrmRotationTargetSetAsDirectionBG(msg, dir);
				}
			}
		}
	}
}

void mmPushStartWithVelocity(	MovementRequester* mr,
									const Vec3 vel,
									U32 startProcessCount)
{
	PushFG* fg = NULL;
	
	if(mrGetFG(mr, mmPushMsgHandler, &fg)){
		mrEnableMsgCreateToBG(mr);
		copyVec3(vel, fg->toBG.vel);
		fg->toBG.startProcessCount = startProcessCount ?
										startProcessCount :
										mmGetProcessCountAfterSecondsFG(0);
	}
}

#include "AutoGen/EntityMovementPush_c_ast.c"

