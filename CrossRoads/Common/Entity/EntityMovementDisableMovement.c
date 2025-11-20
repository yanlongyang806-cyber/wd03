/***************************************************************************
*     Copyright (c) 2005-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Physics););

#include "EntityMovementDisableMovement.h"
#include "EntityMovementManager.h"

AUTO_RUN_MM_REGISTER_REQUESTER_MSG_HANDLER(	mrDisableMsgHandler,
											"DisableMovement",
											MRDisable);

#define GET_SYNC(sync, syncPublic) MR_GET_SYNC(mr, mrDisableMsgHandler, MRDisable, sync)

AUTO_STRUCT;
typedef struct MRDisableFG {
	S32	unused;
} MRDisableFG;

AUTO_STRUCT;
typedef struct MRDisableBG {
	S32	unused;
} MRDisableBG;

AUTO_STRUCT;
typedef struct MRDisableLocalBG {
	S32 unused;
} MRDisableLocalBG;

AUTO_STRUCT;
typedef struct MRDisableToFG {
	S32	unused;
} MRDisableToFG;

AUTO_STRUCT;
typedef struct MRDisableToBG {
	S32 unused;
} MRDisableToBG;

AUTO_STRUCT;
typedef struct MRDisableSync {
	S32 unused;
	U32	start				: 1;
	U32	destroySelf			: 1;
} MRDisableSync;

void mrDisableMsgHandler(const MovementRequesterMsg* msg){
	MRDisableFG*		fg;
	MRDisableBG*		bg;
	MRDisableLocalBG*	localBG;
	MRDisableToFG*		toFG;
	MRDisableToBG*		toBG;
	MRDisableSync*		sync;
	
	MR_MSG_HANDLER_GET_DATA_DEFAULT(msg, MRDisable);

	switch(msg->in.msgType){
		xcase MR_MSG_BG_GET_DEBUG_STRING:{
			char*	buffer = msg->in.bg.getDebugString.buffer;
			U32		bufferLen = msg->in.bg.getDebugString.bufferLen;

			snprintf_s(	buffer,
						bufferLen,
						"");
		}

		xcase MR_MSG_BG_INITIALIZE:{
			const U32 handledMsgs =	MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP;
									
			mrmHandledMsgsAddBG(msg, handledMsgs);
		}

		xcase MR_MSG_BG_WCO_ACTOR_CREATED: {
			mrmMoveToValidPointBG(msg);
		}

		xcase MR_MSG_BG_DISCUSS_DATA_OWNERSHIP:{
			if(sync->start && !sync->destroySelf)
			{
				mrmAcquireDataOwnershipBG(msg, MDC_BITS_TARGET_ALL, 0, NULL, NULL);
			}
			else if(sync->destroySelf)
			{
				mrmDestroySelf(msg);
			}
		}

		xcase MR_MSG_BG_CREATE_OUTPUT:{
			switch(msg->in.bg.createOutput.dataClassBit){
				xcase MDC_BIT_POSITION_TARGET:{
					mrmTargetSetAsStoppedBG(msg);
				}

				xcase MDC_BIT_ROTATION_TARGET:{
					mrmRotationTargetSetAsStoppedBG(msg);
				}
			}
		}
	}
}

S32 mrDisableCreate(MovementManager* mm,
					MovementRequester** mrOut)
{
	MovementRequester*	mr;
	MRDisableSync*		sync;
	
	if(!mmRequesterCreateBasic(mm, &mr, mrDisableMsgHandler)){
		return 0;
	}
	
	*mrOut = mr;
	
	if(!GET_SYNC(&sync, NULL)){
		return 0;
	}
	
	mrEnableMsgUpdatedSync(mr);

	sync->start = 1;

	return 1;
}

S32 mrDisableSetDestroySelfFlag(MovementRequester** mrInOut)
{
	MovementRequester*	mr = SAFE_DEREF(mrInOut);
	MRDisableSync*		sync;

	if(!GET_SYNC(&sync, NULL)){
		return 0;
	}
	
	mrEnableMsgUpdatedSync(mr);
	
	sync->destroySelf = 1;
	
	ANALYSIS_ASSUME(mrInOut);
	*mrInOut = NULL;

	return 1;
}

#include "AutoGen/EntityMovementDisableMovement_c_ast.c"

