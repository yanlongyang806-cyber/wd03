#include "EntityMovementManager.h"
#include "BeaconMovement.h"
#include "Entity.h"
#include "LineDist.h"
#include "wlBeacon.h"
#include "beaconClient.h"


AUTO_RUN_MM_REGISTER_REQUESTER_MSG_HANDLER(	beaconMovementMsgHandler,
											"BeaconMovement",
											BeaconMovement);

AUTO_STRUCT;
typedef struct BeaconMovementFG {
	Vec3				target;
	S32					count;
	U32					targetID;
	U32					recvTargetID;
	U32					reached;
	U32					doMove : 1;
	U32					finished : 1;
	U32					optional : 1;	AST(NAME("Optional"))
} BeaconMovementFG;

AUTO_STRUCT;
typedef struct BeaconMovementBG {
	Vec3				dir;
	U32					targetID;
	U32					sentTargetID;
	BeaconWalkState		walkState;		NO_AST
	U32					doMove : 1;
	U32					finished : 1;
} BeaconMovementBG;

AUTO_STRUCT;
typedef struct BeaconMovementLocalBG {
	S32					unused;
} BeaconMovementLocalBG;

AUTO_STRUCT;
typedef struct BeaconMovementToFG {
	Vec3				pos;
	U32					targetID;
	U32					reached;
	U32					finished : 1;
	U32					optional : 1;	AST(NAME("Optional"))
} BeaconMovementToFG;

AUTO_STRUCT;
typedef struct BeaconMovementToBG {
	Vec3				target;
	S32					count;
	U32					targetID;
	U32					doMove : 1;
} BeaconMovementToBG;

AUTO_STRUCT;
typedef struct BeaconMovementSync {
	S32					ununsed;
} BeaconMovementSync;

void beaconMovementMsgHandler(const MovementRequesterMsg* msg){
	BeaconMovementFG*		fg;
	BeaconMovementBG*		bg;
	BeaconMovementLocalBG*	localBG;
	BeaconMovementToFG*		toFG;
	BeaconMovementToBG*		toBG;
	BeaconMovementSync*		sync;
	
	MR_MSG_HANDLER_GET_DATA_DEFAULT(msg, BeaconMovement);

	switch(msg->in.msgType){
		xcase MR_MSG_FG_CREATE_TOBG:
		{
			mrmEnableMsgUpdatedToBG(msg);
		
			fg->reached = 0;
			toBG->doMove = fg->doMove;
			toBG->count = fg->count;
			toBG->targetID = fg->targetID;
			copyVec3(fg->target, toBG->target);
		}

		xcase MR_MSG_FG_UPDATED_TOFG:
		{
			assert(toFG->targetID);
			assert(toFG->targetID==fg->targetID);
			fg->targetID = 0;
			toFG->targetID = 0;
			fg->reached = toFG->reached;
			fg->finished = toFG->finished;
			fg->optional = toFG->optional;
			toFG->finished = 0;
			toFG->reached = 0;
		}

		xcase MR_MSG_BG_INITIALIZE:{
			const U32 handledMsgs =	MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP;
									
			mrmHandledMsgsAddBG(msg, handledMsgs);
		}

		xcase MR_MSG_BG_DATA_RELEASE_REQUESTED:
		{
			msg->out->bg.dataReleaseRequested.denied = 1;
		}

		xcase MR_MSG_BG_UPDATED_TOBG:
		{
			Vec3 pos;

			assert(toBG->targetID);
			assert(bg->targetID==0);
			ZeroStruct(bg);

			bg->doMove = toBG->doMove;
			bg->targetID = toBG->targetID;
			bg->finished = 0;

			mrmGetPositionBG(msg, pos);
			beaconWalkStateInit(&bg->walkState, toBG->count, pos, toBG->target);

			mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
		}

		xcase MR_MSG_BG_DISCUSS_DATA_OWNERSHIP:
		{
			mrmAcquireDataOwnershipBG(msg, MDC_BIT_POSITION_TARGET, 1, NULL, NULL);

			mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
		}

		xcase MR_MSG_BG_CREATE_OUTPUT:
		{
			U32 dataClassBit = msg->in.bg.createOutput.dataClassBit;
			Vec3 pos;
			Vec3 target;
			static Vec3 start = {0,0,0};

			if(bg->finished)
			{
				bg->walkState.processCount = 0;
				break;
			}

			if(!(dataClassBit & MDC_BIT_POSITION_TARGET))
			{
				devassert(0);
				break;
			}

			mrmGetPositionBG(msg, pos);

			copyVec3(bg->walkState.targetPos, target);
			target[1] = pos[1];  // Disable jumping
			
			{
				BeaconWalkResult res;
				F32 maxSpeed;
				F32 speed = 0;
				mrmGetMaxSpeedBG(msg, &maxSpeed);
				
				res = beaconCheckWalkState(&bg->walkState, pos, maxSpeed, &speed);
				if(res==-1)
				{
					mrmTargetSetAsPointBG(msg, target);
					mrmTargetSetSpeedAsOverrideBG(msg, speed);
				}
				else
				{
					bg->finished = 1;
					toFG->finished = 1;
					toFG->reached = res;
					toFG->optional = bg->walkState.optional;
					mrmEnableMsgUpdatedToFG(msg);
					toFG->targetID = bg->targetID;
					bg->targetID = 0;

					mrmReleaseDataOwnershipBG(msg, MDC_BIT_POSITION_TARGET);
					break;
				}
			}
		}
	}
}

void beaconMovementCreate(MovementRequester **mrOut, MovementManager *mm)
{
	mmRequesterCreateBasic(mm, mrOut, beaconMovementMsgHandler);
}

void beaconMovementDestroy(MovementRequester** mrInOut)
{
	mrDestroy(mrInOut);
}

void beaconMovementSetTarget(MovementRequester* mr, const Vec3 target)
{
	static U32 targetID = 0;
	BeaconMovementFG *fg = NULL;
	mrGetFG(mr, beaconMovementMsgHandler, &fg);
	
	mrEnableMsgCreateToBG(mr);

	targetID++;

	copyVec3(target, fg->target);
	assert(fg->targetID==0);
	fg->doMove = 1;
	fg->reached = 0;
	fg->finished = 0;
	fg->optional = 0;
	fg->targetID = targetID;
}

void beaconMovementSetCount(MovementRequester* mr, int count)
{
	BeaconMovementFG *fg = NULL;
	mrGetFG(mr, beaconMovementMsgHandler, &fg);

	mrEnableMsgCreateToBG(mr);

	fg->count = BM_MAX_COUNT;
	fg->reached = 0;
	fg->finished = 0;
	fg->optional = 0;
}

U32 beaconMovementReachedTarget(MovementRequester* mr)
{
	BeaconMovementFG *fg = NULL;
	mrGetFG(mr, beaconMovementMsgHandler, &fg);

	return fg->reached;
}

U32 beaconMovementFailedOptionalTest(MovementRequester *mr)
{
	BeaconMovementFG *fg = NULL;
	mrGetFG(mr, beaconMovementMsgHandler, &fg);

	return fg->optional;
}

U32 beaconMovementFinished(MovementRequester* mr)
{
	BeaconMovementFG *fg = NULL;
	mrGetFG(mr, beaconMovementMsgHandler, &fg);

	return fg->finished;
}

MovementRequester* g_beaconDebugMR = 0;
Vec3 g_beaconDebugStart;
Vec3 g_beaconDebugEnd;

AUTO_COMMAND ACMD_NAME(bmss);
void beaconMovementSetStart(Entity *e)
{
	entGetPos(e, g_beaconDebugStart);
}

AUTO_COMMAND ACMD_NAME(bmse);
void beaconMovementSetEnd(Entity *e)
{
	entGetPos(e, g_beaconDebugEnd);
}

AUTO_COMMAND ACMD_NAME(bmr);
void beaconMovementRun(Entity *e)
{
	Entity *be = e;
	MovementRequester *bm;
	
	if(!mmRequesterGetByNameFG(be->mm.movement, "beaconmovement", &bm))
	{
		assert(0);
	}
	
	mmSetPositionFG(be->mm.movement, g_beaconDebugStart, __FUNCTION__);

	beaconMovementSetTarget(bm, g_beaconDebugEnd);

	beaconMovementSetCount(bm, BM_MAX_COUNT);
}

#include "autogen/BeaconMovement_c_ast.c"
