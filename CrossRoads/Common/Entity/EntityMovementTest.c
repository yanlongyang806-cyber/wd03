/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "EntityMovementTest.h"
#include "EntityMovementManager.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Physics););

AUTO_RUN_MM_REGISTER_REQUESTER_MSG_HANDLER(	mrTestMsgHandler,
											"TestMovement",
											MRTest);
											
char mrTestAnimName[100] = "hit";
AUTO_CMD_STRING(mrTestAnimName, mrTestAnimName);

char mrTestFlagName[100] = "activate";
AUTO_CMD_STRING(mrTestFlagName, mrTestFlagName);

F32 mrTestSecondsToPlayFlag = 2.f;
AUTO_CMD_FLOAT(mrTestSecondsToPlayFlag, mrTestSecondsToPlayFlag);

F32 mrTestSecondsToDestroy = 5.f;
AUTO_CMD_FLOAT(mrTestSecondsToDestroy, mrTestSecondsToDestroy);

AUTO_STRUCT;
typedef struct MRTestToBG {
	U32							processCount;
} MRTestToBG;

AUTO_STRUCT;
typedef struct MRTestFG {
	MRTestToBG					toBG;
} MRTestFG;

AUTO_STRUCT;
typedef struct MRTestBGFlags {
	U32							createFxServer	: 1;
	U32							createFxClient	: 1;
	U32							hasRunDisabled	: 1;
	U32							playedFlag		: 1;
} MRTestBGFlags;

AUTO_STRUCT;
typedef struct MRTestBG {
	MRTestToBG					toBG;
	U32 						fxHandle;
	U32							spcStartAnim;
	MRTestBGFlags				flags;
} MRTestBG;

AUTO_STRUCT;
typedef struct MRTestLocalBG {
	U32							movhRunDisabled;
} MRTestLocalBG;

AUTO_STRUCT;
typedef struct MRTestToFG {
	S32 stuff;
} MRTestToFG;

AUTO_STRUCT;
typedef struct MRTestSync {
	S32 unused;
} MRTestSync;

void mrTestMsgHandler(const MovementRequesterMsg* msg){
	MRTestFG*		fg;
	MRTestBG*		bg;
	MRTestLocalBG*	localBG;
	MRTestToFG*		toFG;
	MRTestToBG*		toBG;
	MRTestSync*		sync;

	MR_MSG_HANDLER_GET_DATA_DEFAULT(msg, MRTest);

	switch(msg->in.msgType){
		xcase MR_MSG_FG_CREATE_TOBG:{
		}

		xcase MR_MSG_BG_GET_DEBUG_STRING:{
			char*	buffer = msg->in.bg.getDebugString.buffer;
			U32		bufferLen = msg->in.bg.getDebugString.bufferLen;

			snprintf_s(	buffer,
						bufferLen,
						"fx %d",
						bg->fxHandle);
		}
		
		xcase MR_MSG_BG_INITIALIZE:{
			mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_INPUT_EVENT);
		}

		xcase MR_MSG_BG_DATA_WAS_RELEASED:{
			mrmDestroySelf(msg);
		}

		xcase MR_MSG_BG_DATA_RELEASE_REQUESTED:{
			msg->out->bg.dataReleaseRequested.denied = 1;
		}
		
		xcase MR_MSG_BG_UPDATED_TOBG:{
		}

		xcase MR_MSG_BG_INPUT_EVENT:{
			if(msg->in.bg.inputEvent.value.bit){
				MovementInputValueIndex mivi = msg->in.bg.inputEvent.value.mivi;
				
				switch(mivi){
					xcase MIVI_BIT_UP:{
						//bg->flags.createFxServer = 1;
						
						mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
						
						#if 0
						if(FALSE_THEN_SET(bg->flags.hasRunDisabled)){
							mrmOverrideValueCreateS32BG(msg,
														&localBG->movhRunDisabled,
														"RunDisabled",
														1);
						}else{
							bg->flags.hasRunDisabled = 0;

							mrmOverrideValueDestroyBG(	msg,
														&localBG->movhRunDisabled, 
														"RunDisabled");
						}
						#endif
					}
					
					xcase MIVI_BIT_DOWN:{
						//bg->flags.createFxClient = 1;
					}
					
					xcase MIVI_BIT_SLOW:{
						//bg->flags.createFxServer = 1;
						//bg->flags.createFxClient = 1;
					}
				}
			}
		}

		xcase MR_MSG_BG_OVERRIDE_ALL_UNSET:{
			localBG->movhRunDisabled = 0;
		}
		
		xcase MR_MSG_BG_BEFORE_REPREDICT:{
			if(bg->flags.hasRunDisabled){
				mrmOverrideValueCreateS32BG(msg,
											&localBG->movhRunDisabled,
											"RunDisabled",
											1);
			}
		}

		xcase MR_MSG_BG_DISCUSS_DATA_OWNERSHIP:{
			const U32 mdcBits = MDC_BITS_TARGET_ALL | MDC_BIT_ANIMATION;

			if(mrmAcquireDataOwnershipBG(msg, mdcBits, 1, NULL, NULL)){
				mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
			}else{
				mrmDestroySelf(msg);
			}
		}

		xcase MR_MSG_BG_CREATE_OUTPUT:{
			U32 dataClassBit = msg->in.bg.createOutput.dataClassBit;
			
			switch(dataClassBit){
				xcase MDC_BIT_ANIMATION:{
					if(!bg->spcStartAnim){
						mrmAnimStartBG(msg, mmGetAnimBitHandleByName(mrTestAnimName, 0), 0);
						mrmGetProcessCountBG(msg, &bg->spcStartAnim);
					}
					
					if(!bg->flags.playedFlag){
						if(mrmProcessCountPlusSecondsHasPassedBG(	msg,
																	bg->spcStartAnim,
																	mrTestSecondsToPlayFlag))
						{
							bg->flags.playedFlag = 1;

							mrmAnimPlayFlagBG(msg, mmGetAnimBitHandleByName(mrTestFlagName, 0), 0);
						}
					}
					else if(mrmProcessCountPlusSecondsHasPassedBG(	msg,
																	bg->spcStartAnim,
																	mrTestSecondsToDestroy))
					{
						mrmReleaseAllDataOwnershipBG(msg);
					}
				}
			}
		}
	}
}

void mmTestSetDoTest(	MovementRequester* mr,
						U32 startTime)
{
	MRTestFG* fg;

	if(mrGetFG(mr, mrTestMsgHandler, &fg)){
		mrEnableMsgCreateToBG(mr);
		fg->toBG.processCount = startTime;
	}
}

#include "AutoGen/EntityMovementTest_c_ast.c"
