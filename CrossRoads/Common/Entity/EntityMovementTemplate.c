/***************************************************************************
*     Copyright (c) 2005-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

// NOTE: Copy this file and do the following:
//       1. Find-and-Replace the word "Template" (case sensitive) with your requester name.
//       2. Find-and-Replace the word "//REMOVED:" with blank "".
//       3. Change the #if 0 to #if 1.

#if 0

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Physics););

#include "EntityMovementTemplate.h"


//REMOVED:AUTO_RUN_MM_REGISTER_REQUESTER_MSG_HANDLER(	mrTemplateMsgHandler,
//REMOVED:											"TemplateMovement",
//REMOVED:											Template);

//REMOVED:AUTO_STRUCT;
typedef struct TemplateFG {
	S32	unused;
} TemplateFG;

//REMOVED:AUTO_STRUCT;
typedef struct TemplateBG {
	S32	unused;
} TemplateBG;

//REMOVED:AUTO_STRUCT;
typedef struct TemplateLocalBG {
	S32 unused;
} TemplateLocalBG;

//REMOVED:AUTO_STRUCT;
typedef struct TemplateToFG {
	S32	unused;
} TemplateToFG;

//REMOVED:AUTO_STRUCT;
typedef struct TemplateToBG {
	S32 unused;
} TemplateToBG;

//REMOVED:AUTO_STRUCT;
typedef struct TemplateSync {
	S32 unused;
} TemplateSync;

void mrTemplateMsgHandler(const MovementRequesterMsg* msg){
	TemplateFG*			fg;
	TemplateBG*			bg;
	TemplateLocalBG*	localBG;
	TemplateToFG*		toFG;
	TemplateToBG*		toBG;
	TemplateSync*		sync;
	
	MR_MSG_HANDLER_GET_DATA_DEFAULT(msg, Template);

	switch(msg->in.msgType){
		xcase MR_MSG_FG_CREATE_TOBG:{
			mrmEnableMsgUpdatedToBG(msg);
		}
		
		xcase MR_MSG_BG_GET_DEBUG_STRING:{
			char*	buffer = msg->in.bg.getDebugString.buffer;
			U32		bufferLen = msg->in.bg.getDebugString.bufferLen;

			sprintf_s(	buffer,
						bufferLen,
						"");
		}

		xcase MR_MSG_BG_DATA_WAS_RELEASED:{
		}
		
		xcase MR_MSG_BG_DATA_RELEASE_REQUESTED:{
		}
		
		xcase MR_MSG_BG_INPUT_EVENT:{
		}
		
		xcase MR_MSG_BG_UPDATED_TOBG:{
		}
		
		xcase MR_MSG_BG_DISCUSS_DATA_OWNERSHIP:{
		}
		
		xcase MR_MSG_BG_CREATE_OUTPUT:{
		}

		xcase MR_MSG_BG_CREATE_DETAILS:{
		}
	}
}

#include "AutoGen/EntityMovementTemplate_c_ast.c"

#endif