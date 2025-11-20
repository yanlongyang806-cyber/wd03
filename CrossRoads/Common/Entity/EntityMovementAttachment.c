/***************************************************************************
*     Copyright (c) 2005-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "EntityMovementAttachment.h"
#include "EntityMovementManager.h"
#include "Entity.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

AUTO_RUN_MM_REGISTER_RESOURCE_MSG_HANDLER(	mmrAttachmentMsgHandler,
											"Attachment",
											MMRAttachment,
											MDC_BIT_POSITION_CHANGE);

AUTO_STRUCT;
typedef struct MMRAttachmentActivatedFG {
	S32										unused;
} MMRAttachmentActivatedFG;

AUTO_STRUCT;
typedef struct MMRAttachmentActivatedBG {
	S32										unused;
} MMRAttachmentActivatedBG;

AUTO_STRUCT;
typedef struct MMRAttachmentState {
	S32										unused;
} MMRAttachmentState;

void mmrAttachmentMsgHandler(const MovementManagedResourceMsg* msg){
	const MMRAttachmentConstant* constant = msg->in.constant;
	
	switch(msg->in.msgType){
		xcase MMR_MSG_GET_CONSTANT_DEBUG_STRING:{
			char** estrBuffer = msg->in.getDebugString.estrBuffer;
		}
		
		xcase MMR_MSG_FG_SET_STATE:{
			MMRAttachmentActivatedFG*	activated = msg->in.activatedStruct;
			Entity*						beParent = entFromEntityRefAnyPartition(constant->erParent);
			
			assert(0);
		}

		xcase MMR_MSG_FG_DESTROYED:{
			MMRAttachmentActivatedFG* activated = msg->in.activatedStruct;
		}

		xcase MMR_MSG_BG_SET_STATE:{
			MMRAttachmentActivatedBG*	activated = msg->in.activatedStruct;
			Entity*						be = entFromEntityRefAnyPartition(constant->erParent);
			
			if(be){
			}
		}

		xcase MMR_MSG_BG_DESTROYED:{
			MMRAttachmentActivatedBG* activated = msg->in.activatedStruct;
		}
	}
}

static U32 mmrAttachmentGetResourceID(void){
	static U32 id;
	
	if(!id){
		if(!mmGetManagedResourceIDByMsgHandler(mmrAttachmentMsgHandler, &id)){
			assert(0);
		}
	}
	
	return id;
}

S32 mmAttachmentCreateBG(	const MovementRequesterMsg* msg,
							const MMRAttachmentConstant* constant,
							U32* handleOut)
{
	return mrmResourceCreateBG(	msg,
								handleOut,
								mmrAttachmentGetResourceID(),
								constant,
								NULL,
								NULL);
}

S32 mmAttachmentDestroyBG(	const MovementRequesterMsg* msg,
							U32* handleInOut)
{
	return mrmResourceDestroyBG(msg,
								mmrAttachmentGetResourceID(),
								handleInOut);
}

#include "autogen/EntityMovementAttachment_h_ast.c"
#include "autogen/EntityMovementAttachment_c_ast.c"
