/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "EntityMovementInteraction.h"
#include "EntityMovementManager.h"
#include "mathutil.h"
#include "quat.h"
#include "error.h"
#include "EArray.h"
#include "textparser.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Physics););

AUTO_RUN_MM_REGISTER_REQUESTER_MSG_HANDLER(	mrInteractionMsgHandler,
											"InteractionMovement",
											MRInteraction);

static F32 distToFaceNextWaypoint = 5.f;
AUTO_CMD_FLOAT(distToFaceNextWaypoint, mrInteractionDistToFaceNextWaypoint);

AUTO_STRUCT;
typedef struct MRInteractionToBG {
	MRInteractionPath*				path;
	U32								pathVersion;
} MRInteractionToBG;

AUTO_STRUCT;
typedef struct MRInteractionFGFlags {
	U32								sentStartPathToBG		: 1;
	U32								pathVersionAckedByBG	: 1;
} MRInteractionFGFlags;

AUTO_STRUCT;
typedef struct MRInteractionFG {
	MRInteractionOwnerMsgHandler	msgHandler;		NO_AST
	void*							userPointer;	NO_AST
	MRInteractionPath*				path;
	U32								pathVersion;
	MRInteractionFGFlags			flags;
} MRInteractionFG;

AUTO_STRUCT;
typedef struct MRInteractionBGFlags {
	U32								movingOnRail			: 1;
	U32								doneMovingOnRail		: 1;
	U32								waitingAtWaypoint		: 1;
	U32								acquireChangeAll		: 1;
	U32								changeAllIsAcquired		: 1;
	U32								acquireAnim				: 1;
	U32								animIsAcquired			: 1;
	U32								inputEventHappened		: 1;
	U32								triggerHappened			: 1;
	U32								railMoveFailed			: 1;
	U32								sentReachedWaypoint		: 1;
	U32								sentWaitingForTrigger	: 1;
} MRInteractionBGFlags;

AUTO_STRUCT;
typedef struct MRInteractionBG {
	MRInteractionPath*				path;
	U32								pathVersion;
	U32								pathVersionStarted;
	
	union {
		U32							targetWaypointIndexMutable;
		const U32					targetWaypointIndex;			NO_AST
	};

	U32								triggeredWaypointIndex;
	U32								spcReachedWaypoint;
	U32								spcLingering;
	F32								minDistFromWP;
	U32								spcMinDistChanged;

	U32								spcMoveOnRailStart;
	F32								scaleMoveOnRail;
	Vec3							posMoveOnRailStart;
	Quat							rotMoveOnRailStart;
	Vec2							pyFaceMoveOnRailStart;

	union {
		MRInteractionBGFlags		flagsMutable;
		const MRInteractionBGFlags	flags;							NO_AST
	};
} MRInteractionBG;

AUTO_STRUCT;
typedef struct MRInteractionLocalBG {
	U32								unused;
} MRInteractionLocalBG;

AUTO_STRUCT;
typedef struct MRInteractionToFGFlags {
	U32								finished			: 1;
	U32								failed				: 1;
	U32								reachedWaypoint		: 1;
	U32								waitingForTrigger	: 1;
	U32								hasPathVersion		: 1;
} MRInteractionToFGFlags;

AUTO_STRUCT;
typedef struct MRInteractionToFG {
	U32								pathVersionReceived;
	U32								reachedWaypointIndex;
	MRInteractionToFGFlags			flags;
} MRInteractionToFG;

AUTO_STRUCT;
typedef struct MRInteractionSyncFlags {
	U32								hasTrigger	: 1;
	U32								destroySelf	: 1;
} MRInteractionSyncFlags;

AUTO_STRUCT;
typedef struct MRInteractionSync {
	U32								pathVersionToStart;
	U32								triggerWaypointIndex;
	MRInteractionSyncFlags			flags;
} MRInteractionSync;

static void mrInteractionReleaseBG(const MovementRequesterMsg* msg){
	const U32 handledMsgs =	MR_HANDLED_MSG_CREATE_DETAILS |
							MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP |
							MR_HANDLED_MSG_OUTPUT_ANIMATION;

	mrmReleaseAllDataOwnershipBG(msg);
	mrmHandledMsgsRemoveBG(msg, handledMsgs);
}

static S32 mrInteractionIsStuckBG(	const MovementRequesterMsg* msg,
									MRInteractionBG* bg,
									MRInteractionToFG* toFG,
									const Vec3 pos,
									const MRInteractionWaypoint* wp)
{
	F32 dist = distance3XZ(pos, wp->pos);
	
	if(	!bg->spcMinDistChanged ||
		dist < bg->minDistFromWP - 0.5f)
	{
		bg->minDistFromWP = dist;
		mrmGetProcessCountBG(msg, &bg->spcMinDistChanged);
	}
	else if(mrmProcessCountPlusSecondsHasPassedBG(	msg,
													bg->spcMinDistChanged,
													0.5f))
	{
		toFG->flags.failed = 1;
		mrmEnableMsgUpdatedToFG(msg);

		mrInteractionReleaseBG(msg);
		return 1;
	}
	
	return 0;
}

static void mrInteractionAcquireChangeAllBG(const MovementRequesterMsg* msg,
											MRInteractionBG* bg)
{
	mrmLog(msg, NULL, "Setting flag to acquire CHANGE_ALL.");

	bg->flagsMutable.acquireChangeAll = 1;

	if(!bg->flags.changeAllIsAcquired){
		mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
	}
}

static void mrInteractionReleaseChangeAllBG(const MovementRequesterMsg* msg,
											MRInteractionBG* bg)
{
	if(TRUE_THEN_RESET(bg->flagsMutable.changeAllIsAcquired)){
		mrmLog(msg, NULL, "Releasing CHANGE_ALL.");
		mrmReleaseDataOwnershipBG(msg, MDC_BITS_CHANGE_ALL);
	}
}

static void mrInteractionAcquireAnimBG(	const MovementRequesterMsg* msg,
										MRInteractionBG* bg)
{
	mrmLog(msg, NULL, "Setting flag to acquire ANIMATION.");

	bg->flagsMutable.acquireAnim = 1;

	if(!bg->flags.animIsAcquired){
		mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
	}
}

static void mrInteractionReleaseAnimBG(	const MovementRequesterMsg* msg,
										MRInteractionBG* bg)
{
	if(TRUE_THEN_RESET(bg->flagsMutable.animIsAcquired)){
		mrmLog(msg, NULL, "Releasing ANIMATION.");
		mrmReleaseDataOwnershipBG(msg, MDC_BIT_ANIMATION);
	}
}

static S32 mrInteractionStartWaitingAtWaypointBG(	const MovementRequesterMsg* msg,
													MRInteractionBG* bg,
													const MRInteractionWaypoint* wp)
{
	if(!FALSE_THEN_SET(bg->flagsMutable.waitingAtWaypoint)){
		return 0;
	}
	
	// Go into "waitingAtWaypoint" state.

	mrmLog(msg, NULL, "Starting to wait at WP.");

	bg->flagsMutable.inputEventHappened = 0;
	
	mrmGetProcessCountBG(msg, &bg->spcReachedWaypoint);

	if (wp->flags.forceAnimDuringMove)
	{
		if (!gConf.bNewAnimationSystem &&
			eaiSize(&wp->animBitHandles))
		{
			mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_CREATE_DETAILS);
		}
	}
	else
	{
		if(gConf.bNewAnimationSystem){
			if(wp->animToStart){
				mrmLog(msg, NULL, "animToStart.");
				mrInteractionAcquireAnimBG(msg, bg);
			}
		}
		else if(wp->animBitHandles){
			mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_CREATE_DETAILS);
		}
	}

	if(wp->flags.snapToWaypoint){
		mrmLog(msg, NULL, "snapToWaypoint.");
		mrInteractionAcquireChangeAllBG(msg, bg);
	}

	if(wp->flags.moveToOnRail){
		mrmLog(msg, NULL, "moveToOnRail.");
		mrInteractionAcquireChangeAllBG(msg, bg);
	}
	
	if(wp->flags.releaseOnInput){
		mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_INPUT_EVENT);
	}
		
	mrmLog(msg, NULL, "Done starting to wait at WP.");

	return 1;
}

static void mrInteractionPositionTargetBG(	const MovementRequesterMsg* msg,
											MRInteractionBG* bg,
											MRInteractionToFG* toFG)
{
	const MRInteractionPath*		p = bg->path;
	const MRInteractionWaypoint*	wp;
	Vec3							pos;
	const U32						wpCount = eaSize(&p->wps);
	
	ANALYSIS_ASSUME(p->wps);
	assert(bg->targetWaypointIndex <= wpCount);

	mrmGetPositionBG(msg, pos);

	while(1){
		S32 onGround;
		
		wp = p->wps[bg->targetWaypointIndex];
		
		// Check if I'm done waiting at the waypoint.
		
		if(bg->flags.waitingAtWaypoint){
			mrmLog(msg, NULL, "waitingAtWaypoint.");

			if(wp->flags.releaseOnInput){
				mrmLog(msg, NULL, "releaseOnInput.");

				if(!TRUE_THEN_RESET(bg->flagsMutable.inputEventHappened)){
					mrmLog(msg, NULL, "Waiting for inputEventHappened.");
					break;
				}
			}
			else if(wp->flags.waitHereUntilTriggered){
				mrmLog(msg, NULL, "waitHereUntilTriggered.");

				if(	!bg->flags.triggerHappened ||
					bg->triggeredWaypointIndex < bg->targetWaypointIndex)
				{
					mrmLog(msg, NULL, "not triggered.");
					break;
				}
			}
			else if(!mrmProcessCountPlusSecondsHasPassedBG(	msg,
															bg->spcReachedWaypoint,
															wp->seconds))
			{
				mrmLog(	msg,
						NULL,
						"Waiting for %1.2f seconds since s%u.",
						wp->seconds,
						bg->spcReachedWaypoint);

				break;
			}

			mrmLog(msg, NULL, "Done waitingAtWaypoint.");

			bg->flagsMutable.waitingAtWaypoint = 0;
			mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_CREATE_DETAILS);
		}else{
			S32 reachedWaypoint = 0;

			if(wp->flags.forceAnimDuringMove)
			{
				if (!gConf.bNewAnimationSystem &&
					eaiSize(&wp->animBitHandles))
				{
					mrInteractionAcquireAnimBG(msg, bg);
				}
			}

			if(wp->flags.moveToOnRail){
				// Lerp to waypoint.

				mrmLog(msg, NULL, "moveToOnRail.");

				mrInteractionAcquireChangeAllBG(msg, bg);
				
				if(!bg->flags.movingOnRail){
					mrmLog(msg, NULL, "Waiting for movingOnRail.");
					break;
				}
				else if(!bg->flags.doneMovingOnRail){
					mrmLog(msg, NULL, "Waiting for doneMovingOnRail.");
					break;
				}
				
				reachedWaypoint = 1;
			}else{
				// Walking to waypoint.

				mrmLog(msg, NULL, "Walking to WP.");

				// Check if I'm stuck.

				if(mrInteractionIsStuckBG(msg, bg, toFG, pos, wp)){
					mrmLog(msg, NULL, "Stuck.");
					break;
				}

				// Check if I'm close enough to go to the next waypoint.
				
				if(	bg->targetWaypointIndex < wpCount - 1 &&
					distance3SquaredXZ(wp->pos, pos) <= SQR(1.5f)
					||
					bg->targetWaypointIndex == wpCount - 1 &&
					distance3SquaredXZ(wp->pos, pos) <= SQR(0.5f) &&
					pos[1] < wp->pos[1] + 2.f &&
					(	wp->flags.snapToWaypoint ||
						!mrmGetOnGroundBG(msg, &onGround, NULL) ||
						onGround))
				{
					mrmLog(msg, NULL, "Reached WP.");
					reachedWaypoint = 1;
				}
			}

			// Check if still going towards current waypoint.

			if(!reachedWaypoint){
				mrmLog(msg, NULL, "WP not reached, setting target.");
				mrmTargetSetAsPointBG(msg, wp->pos);
				break;
			}
			
			// Notify FG that the wp was reached.
			
			if(	wp->flags.notifyWhenReached &&
				FALSE_THEN_SET(bg->flagsMutable.sentReachedWaypoint))
			{
				mrmEnableMsgUpdatedToFG(msg);

				toFG->flags.reachedWaypoint = 1;
				toFG->reachedWaypointIndex = bg->targetWaypointIndex;
			}
			
			// Notify FG that a trigger is needed to proceed.
			
			if(	wp->flags.waitHereUntilTriggered
				&&
				(	!bg->flags.triggerHappened ||
					bg->triggeredWaypointIndex < bg->targetWaypointIndex)
				&&
				FALSE_THEN_SET(bg->flagsMutable.sentWaitingForTrigger))
			{
				mrmEnableMsgUpdatedToFG(msg);

				toFG->flags.waitingForTrigger = 1;
				toFG->reachedWaypointIndex = bg->targetWaypointIndex;
			}
			
			// Check if I need to wait at the waypoint.
			
			if(	!wp->flags.moveToOnRail &&
				wp->seconds > 0.f
				||
				wp->flags.releaseOnInput
				||
				wp->flags.waitHereUntilTriggered &&
				(	!bg->flags.triggerHappened ||
					bg->triggeredWaypointIndex < bg->targetWaypointIndex)
				)
			{
				mrmLog(msg, NULL, "Need to wait at WP.");
				mrInteractionStartWaitingAtWaypointBG(msg, bg, wp);
				break;
			}
		}
		
		// Go to the next waypoint, and check if I'm done.
		
		mrmLog(msg, NULL, "Going to next WP.");

		bg->spcMinDistChanged = 0;
		bg->flagsMutable.movingOnRail = 0;
		bg->flagsMutable.doneMovingOnRail = 0;
		bg->flagsMutable.acquireChangeAll = 0;
		bg->flagsMutable.sentReachedWaypoint = 0;
		bg->flagsMutable.sentWaitingForTrigger = 0;

		if(++bg->targetWaypointIndexMutable >= wpCount){
			mrmLog(msg, NULL, "Finished last WP.");
			toFG->flags.finished = 1;
			mrmEnableMsgUpdatedToFG(msg);
			mrInteractionReleaseBG(msg);
			break;
		}
	}

	// Release things that weren't acquired intentionally.
	
	if(!bg->flags.acquireChangeAll){
		mrInteractionReleaseChangeAllBG(msg, bg);

		if(!bg->flags.acquireAnim){
			mrInteractionReleaseAnimBG(msg, bg);
		}
	}
}

static void mrInteractionPositionChangeBG(	const MovementRequesterMsg* msg,
											MRInteractionBG* bg)
{
	const MRInteractionPath*		p = bg->path;
	const MRInteractionWaypoint*	wp;
	const U32						wpCount = eaSize(&p->wps);
	
	ANALYSIS_ASSUME(p->wps);
	assert(bg->targetWaypointIndex <= wpCount);
	
	wp = p->wps[bg->targetWaypointIndex];
	
	if(wp->flags.moveToOnRail){
		U32		spc;
		Vec3	pos;

		mrmLog(msg, NULL, "WP is moveToOnRail.");
		
		mrmGetProcessCountBG(msg, &spc);
		
		if(FALSE_THEN_SET(bg->flagsMutable.movingOnRail)){
			bg->flagsMutable.doneMovingOnRail = 0;
			bg->spcMoveOnRailStart = spc;

			mrmGetPositionAndRotationBG(msg,
										bg->posMoveOnRailStart,
										bg->rotMoveOnRailStart);
										
			mrmGetFacePitchYawBG(msg, bg->pyFaceMoveOnRailStart);

			if(wp->flags.playAnimDuringMove){
				if(!gConf.bNewAnimationSystem){
					if(wp->animToStart){
						mrInteractionAcquireAnimBG(msg, bg);
					}
				}
				else if(wp->animBitHandles){
					mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_CREATE_DETAILS);
				}
			}
		}

		if(wp->seconds > 0.f){
			bg->scaleMoveOnRail =	subS32(spc, bg->spcMoveOnRailStart) *
									MM_SECONDS_PER_PROCESS_COUNT /
									wp->seconds;
		}else{
			bg->scaleMoveOnRail = 1.f;
		}
		
		MINMAX1(bg->scaleMoveOnRail, 0.f, 1.f);
		
		if(bg->scaleMoveOnRail == 1.f){
			bg->flagsMutable.doneMovingOnRail = 1;
		}
		
		interpVec3(bg->scaleMoveOnRail, bg->posMoveOnRailStart, wp->pos, pos);
		
		mrmSetPositionBG(msg, pos);
	}
	else if(wp->flags.snapToWaypoint){
		mrmLog(msg, NULL, "WP is snapToWaypoint.");

		mrmSetPositionBG(msg, wp->pos);
	}
}

static void mrInteractionRotationTargetBG(	const MovementRequesterMsg* msg,
											MRInteractionBG* bg)
{
	const MRInteractionPath*		p = bg->path;
	const MRInteractionWaypoint*	wp;
	const MRInteractionWaypoint*	wpNext;
	Vec3							pos;
	const U32						wpCount = eaSize(&p->wps);
	const U32						i = bg->targetWaypointIndex;
	
	ANALYSIS_ASSUME(p->wps);
	assert(bg->targetWaypointIndex <= wpCount);

	mrmGetPositionBG(msg, pos);

	if(i == wpCount){
		wp = p->wps[i - 1];
		mrmRotationTargetSetAsRotationBG(msg, wp->rot);
	}else{
		wp = p->wps[i];
		
		if(distance3SquaredXZ(wp->pos, pos) >= SQR(distToFaceNextWaypoint)){
			Vec3 dirFace;

			mrmLog(msg, NULL, "Facing current waypoint.");

			subVec3XZ(wp->pos, pos, dirFace);
			dirFace[1] = 0.f;
			normalVec3XZ(dirFace);
			
			mrmRotationTargetSetAsDirectionBG(msg, dirFace);
		}
		else if(i < wpCount - 1){
			Vec3 dirFace;

			wpNext = p->wps[i + 1];
			
			if(	bg->flags.acquireChangeAll &&
				!bg->flags.changeAllIsAcquired &&
				wp->flags.moveToOnRail)
			{
				mrmLog(msg, NULL, "Orienting to current waypoint (moveToOnRail).");
				
				quatToMat3_2(wp->rot, dirFace);
			}
			else if(wpNext->flags.moveToOnRail){
				mrmLog(msg, NULL, "Orienting to current waypoint (next is moveToOnRail).");

				quatToMat3_2(wp->rot, dirFace);
			}else{
				mrmLog(msg, NULL, "Facing next waypoint.");

				subVec3XZ(wpNext->pos, pos, dirFace);
			}

			dirFace[1] = 0.f;
			normalVec3XZ(dirFace);
			
			mrmRotationTargetSetAsDirectionBG(msg, dirFace);
		}else{
			Vec3 dirFace;

			mrmLog(msg, NULL, "Orienting to current waypoint (is last).");

			quatToMat3_2(wp->rot, dirFace);
			dirFace[1] = 0;

			mrmRotationTargetSetAsDirectionBG(msg, dirFace);
		}
	}
}

static void mrInteractionRotationChangeBG(	const MovementRequesterMsg* msg,
											MRInteractionBG* bg)
{
	const MRInteractionPath*		p = bg->path;
	const MRInteractionWaypoint*	wp;
	const U32						wpCount = eaSize(&p->wps);
	
	ANALYSIS_ASSUME(p->wps);
	assert(bg->targetWaypointIndex <= wpCount);
	
	wp = p->wps[bg->targetWaypointIndex];

	if(wp->flags.moveToOnRail){
		Quat	rot;
		Vec3	zVec;
		Vec2	pyFaceTarget;
		Vec2	pyFace;

		quatInterp(bg->scaleMoveOnRail, bg->rotMoveOnRailStart, wp->rot, rot);
		mrmSetRotationBG(msg, rot);
		
		quatToMat3_2(wp->rot, zVec);
		getVec3YP(zVec, &pyFaceTarget[1], &pyFaceTarget[0]);
		interpPY(bg->scaleMoveOnRail, bg->pyFaceMoveOnRailStart, pyFaceTarget, pyFace);
		mrmSetFacePitchYawBG(msg, pyFace);
	}
	else if(wp->flags.snapToWaypoint){
		Vec3 zVec;
		Vec2 pyFace;

		mrmSetRotationBG(msg, wp->rot);
		
		quatToMat3_2(wp->rot, zVec);
		getVec3YP(zVec, &pyFace[1], &pyFace[0]);
		mrmSetFacePitchYawBG(msg, pyFace);
	}
}

static void mrInteractionAnimationBG(	const MovementRequesterMsg* msg,
										MRInteractionBG* bg)
{
	const MRInteractionPath*		p = bg->path;
	const MRInteractionWaypoint*	wp;
	const U32						wpCount = eaSize(&p->wps);
	
	ANALYSIS_ASSUME(p->wps);
	assert(bg->targetWaypointIndex <= wpCount);
	
	wp = p->wps[bg->targetWaypointIndex];

	if(wp->animToStart){
		mrmAnimStartBG(msg, wp->animToStart, 0);
	}

	mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_OUTPUT_ANIMATION);
}

void mrInteractionMsgHandler(const MovementRequesterMsg* msg){
	MRInteractionFG*		fg;
	MRInteractionBG*		bg;
	MRInteractionLocalBG*	localBG;
	MRInteractionToFG*		toFG;
	MRInteractionToBG*		toBG;
	MRInteractionSync*		sync;
	
	MR_MSG_HANDLER_GET_DATA_DEFAULT(msg, MRInteraction);

	switch(msg->in.msgType){
		xcase MR_MSG_FG_BEFORE_DESTROY:{
			if(fg->msgHandler){
				MRInteractionOwnerMsg msgOut = {0};
				msgOut.msgType = MR_INTERACTION_OWNER_MSG_DESTROYED;
				msgOut.userPointer = fg->userPointer;
				fg->msgHandler(&msgOut);

				fg->msgHandler = NULL;
				fg->userPointer = NULL;
			}
		}
		
		xcase MR_MSG_FG_CREATE_TOBG:{
			if(fg->path){
				mrmEnableMsgUpdatedToBG(msg);

				assert(!toBG->path);
				toBG->path = fg->path;
				toBG->pathVersion = fg->pathVersion;
				fg->path = NULL;
			}
		}
		
		xcase MR_MSG_FG_UPDATED_TOFG:{
			if(	TRUE_THEN_RESET(toFG->flags.hasPathVersion) &&
				toFG->pathVersionReceived == fg->pathVersion)
			{
				ASSERT_FALSE_AND_SET(fg->flags.pathVersionAckedByBG);
				fg->flags.sentStartPathToBG = 1;

				sync->pathVersionToStart = fg->pathVersion;
				mrmEnableMsgUpdatedSyncFG(msg);
			}
			
			if(TRUE_THEN_RESET(toFG->flags.reachedWaypoint)){
				if(fg->msgHandler){
					MRInteractionOwnerMsg msgOut = {0};
					msgOut.msgType = MR_INTERACTION_OWNER_MSG_REACHED_WAYPOINT;
					msgOut.userPointer = fg->userPointer;
					msgOut.reachedWaypoint.index = toFG->reachedWaypointIndex;
					fg->msgHandler(&msgOut);
				}
			}
			
			if(TRUE_THEN_RESET(toFG->flags.waitingForTrigger)){
				if(fg->msgHandler){
					MRInteractionOwnerMsg msgOut = {0};
					msgOut.msgType = MR_INTERACTION_OWNER_MSG_WAITING_FOR_TRIGGER;
					msgOut.userPointer = fg->userPointer;
					msgOut.waitingForTrigger.waypointIndex = toFG->reachedWaypointIndex;
					fg->msgHandler(&msgOut);
				}
			}
			
			if(TRUE_THEN_RESET(toFG->flags.finished)){
				assert(!toFG->flags.failed);

				if(fg->msgHandler){
					MRInteractionOwnerMsg msgOut = {0};
					msgOut.msgType = MR_INTERACTION_OWNER_MSG_FINISHED;
					msgOut.userPointer = fg->userPointer;
					fg->msgHandler(&msgOut);
				}
			}
			else if(TRUE_THEN_RESET(toFG->flags.failed)){
				if(fg->msgHandler){
					MRInteractionOwnerMsg msgOut = {0};
					msgOut.msgType = MR_INTERACTION_OWNER_MSG_FAILED;
					msgOut.userPointer = fg->userPointer;
					fg->msgHandler(&msgOut);
				}
			}
		}
		
		xcase MR_MSG_BG_UPDATED_TOBG:{
			if(toBG->path){
				StructDestroySafe(parse_MRInteractionPath, &bg->path);
				bg->path = toBG->path;
				bg->pathVersion = toBG->pathVersion;
				toBG->path = NULL;
				
				// Reset stuff.

				bg->targetWaypointIndexMutable = 0;
				bg->spcMinDistChanged = 0;
				bg->flagsMutable.waitingAtWaypoint = 0;
				bg->flagsMutable.acquireChangeAll = 0;
				bg->flagsMutable.inputEventHappened = 0;
				bg->flagsMutable.sentReachedWaypoint = 0;
				bg->flagsMutable.sentWaitingForTrigger = 0;
				
				// Tell FG the path is received.
				
				toFG->flags.hasPathVersion = 1;
				toFG->pathVersionReceived = bg->pathVersion;
				mrmEnableMsgUpdatedToFG(msg);
				
				// Release ownership and start path from scratch.

				mrInteractionReleaseBG(msg);
			}
		}

		xcase MR_MSG_BG_UPDATED_SYNC:{
			if(sync->flags.destroySelf){
				mrmDestroySelf(msg);
			}

			if(	sync->pathVersionToStart == bg->pathVersion &&
				sync->pathVersionToStart != bg->pathVersionStarted)
			{
				bg->pathVersionStarted = bg->pathVersion;
				bg->minDistFromWP = 0.f;
				bg->spcMinDistChanged = 0;
				bg->flagsMutable.waitingAtWaypoint = 0;
				bg->flagsMutable.acquireChangeAll = 0;
				bg->flagsMutable.inputEventHappened = 0;
				bg->flagsMutable.triggerHappened = 0;
				
				mrInteractionReleaseBG(msg);

				if(bg->path){
					mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
				}
			}
			
			if(	sync->flags.hasTrigger &&
				bg->pathVersionStarted == bg->pathVersion)
			{
				bg->flagsMutable.triggerHappened = 1;
				bg->triggeredWaypointIndex = sync->triggerWaypointIndex;
			}
		}
		
		xcase MR_MSG_FG_TRANSLATE_SERVER_TO_CLIENT:{
			if(SAFE_MEMBER(bg, path)){
				EARRAY_CONST_FOREACH_BEGIN(bg->path->wps, i, isize);
				{
					mmTranslateAnimBitsServerToClient(bg->path->wps[i]->animBitHandles,0);
					mmTranslateAnimBitServerToClient(&bg->path->wps[i]->animToStart,0);
				}
				EARRAY_FOREACH_END;
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
		
		xcase MR_MSG_BG_DATA_WAS_RELEASED:{
			const U32 dataClassBits = msg->in.bg.dataWasReleased.dataClassBits;
			
			if(dataClassBits & MDC_BITS_TARGET_ALL){
				mrInteractionReleaseBG(msg);
				
				if(!toFG->flags.finished){
					toFG->flags.failed = 1;
					mrmEnableMsgUpdatedToFG(msg);
				}
			}
			
			if(dataClassBits & MDC_BITS_CHANGE_ALL){
				mrmReleaseDataOwnershipBG(msg, MDC_BITS_CHANGE_ALL);
				
				bg->flagsMutable.changeAllIsAcquired = 0;

				if(dataClassBits & MDC_BIT_ROTATION_CHANGE){
					Vec2 pyFace;
					Quat rot;

					mrmGetFacePitchYawBG(msg, pyFace);
					mrmShareOldF32BG(msg, "TargetFaceYaw", pyFace[1]);
					
					mrmGetRotationBG(msg, rot);
					mrmShareOldQuatBG(msg, "TargetRotation", rot);
				}

				if(dataClassBits & MDC_BIT_ANIMATION){
					bg->flagsMutable.animIsAcquired = 0;
				}
			}
		}

		xcase MR_MSG_BG_INPUT_EVENT:{
			if(	INRANGE(msg->in.bg.inputEvent.value.mivi, MIVI_BIT_LOW, MIVI_BIT_HIGH) &&
				msg->in.bg.inputEvent.value.bit)
			{
				bg->flagsMutable.inputEventHappened = 1;
				
				mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_INPUT_EVENT);
			}
		}
		
		xcase MR_MSG_BG_DISCUSS_DATA_OWNERSHIP:{
			S32 doneDiscussing = 1;

			if(bg->path){
				if(!mrmAcquireDataOwnershipBG(msg, MDC_BITS_TARGET_ALL, 1, NULL, NULL)){
					doneDiscussing = 0;
				}
			}
			
			if(	bg->flags.acquireChangeAll &&
				!bg->flags.changeAllIsAcquired)
			{
				if(!mrmAcquireDataOwnershipBG(msg, MDC_BITS_CHANGE_ALL, 1, NULL, NULL)){
					doneDiscussing = 0;
				}else{
					bg->flagsMutable.changeAllIsAcquired = 1;
					bg->flagsMutable.animIsAcquired = 1;
					mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_OUTPUT_ANIMATION);
				}
			}

			if(	bg->flags.acquireAnim &&
				!bg->flags.animIsAcquired)
			{
				if(!mrmAcquireDataOwnershipBG(msg, MDC_BIT_ANIMATION, 1, NULL, NULL)){
					doneDiscussing = 0;
				}else{
					bg->flagsMutable.animIsAcquired = 1;
					if (gConf.bNewAnimationSystem) {
						mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_OUTPUT_ANIMATION);
					} else {
						mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_CREATE_DETAILS);
					}
				}
			}
			
			if(doneDiscussing){
				mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
			}
		}
		
		xcase MR_MSG_BG_CREATE_OUTPUT:{
			switch(msg->in.bg.createOutput.dataClassBit){
				xcase MDC_BIT_POSITION_TARGET:{
					mrInteractionPositionTargetBG(msg, bg, toFG);
				}
				
				xcase MDC_BIT_POSITION_CHANGE:{
					mrInteractionPositionChangeBG(msg, bg);
				}
				
				xcase MDC_BIT_ROTATION_TARGET:{
					mrInteractionRotationTargetBG(msg, bg);
				}

				xcase MDC_BIT_ROTATION_CHANGE:{
					mrInteractionRotationChangeBG(msg, bg);
				}

				xcase MDC_BIT_ANIMATION:{
					mrInteractionAnimationBG(msg, bg);
				}
			}
		}

		xcase MR_MSG_BG_CREATE_DETAILS:{
			const MRInteractionPath*		p = bg->path;
			const MRInteractionWaypoint*	wp;
			const U32						wpCount = eaSize(&p->wps);
			
			ANALYSIS_ASSUME(p->wps);
			assert(bg->targetWaypointIndex <= wpCount);
			
			wp = p->wps[bg->targetWaypointIndex];

			EARRAY_INT_CONST_FOREACH_BEGIN(wp->animBitHandles, i, isize);
			{
				mrmAnimAddBitBG(msg, wp->animBitHandles[i]);
			}
			EARRAY_FOREACH_END;
		}
		
		xcase MR_MSG_BG_QUERY_ON_GROUND:{
			msg->out->bg.queryOnGround.onGround = 1;
		}
	}
}

S32 mrInteractionCreate(MovementManager* mm,
						MovementRequester** mrOut)
{
	return mmRequesterCreateBasic(mm, mrOut, mrInteractionMsgHandler);
}

#define GET_FG(fg)		if(!MR_GET_FG(mr, mrInteractionMsgHandler, MRInteraction, fg)){return 0;}
#define GET_SYNC(sync)	if(!MR_GET_SYNC(mr, mrInteractionMsgHandler, MRInteraction, sync)){return 0;}

S32 mrInteractionSetOwner(	MovementRequester* mr,
							MRInteractionOwnerMsgHandler msgHandler,
							void* userPointer)
{
	MRInteractionFG* fg;
	
	GET_FG(&fg);
	
	fg->msgHandler = msgHandler;
	fg->userPointer = userPointer;
	
	return 1;
}

S32 mrInteractionGetOwner(	MovementRequester* mr,
							MRInteractionOwnerMsgHandler msgHandler,
							void** userPointerOut)
{
	MRInteractionFG* fg;
	
	GET_FG(&fg);
	
	if(	!userPointerOut ||
		fg->msgHandler != msgHandler)
	{
		return 0;
	}
	
	*userPointerOut = fg->userPointer;
	
	return 1;
}

S32 mrInteractionSetPath(	MovementRequester* mr,
							MRInteractionPath** pInOut)
{
	MRInteractionPath*	p = SAFE_DEREF(pInOut);
	MRInteractionFG*	fg;
	MRInteractionSync*	sync;
	
	if(!p){
		return 0;
	}
	
	if(!eaSize(&p->wps)){
		Errorf("Setting an InteractionMovement path with no waypoints; ignoring.");
		StructDestroySafe(parse_MRInteractionPath, pInOut);
		return 0;
	}

	GET_FG(&fg);
	GET_SYNC(&sync);

	StructDestroySafe(parse_MRInteractionPath, &fg->path);
	fg->path = p;
	fg->pathVersion++;
	fg->flags.pathVersionAckedByBG = 0;
	fg->flags.sentStartPathToBG = 0;
	*pInOut = NULL;
	mrEnableMsgCreateToBG(mr);

	if(TRUE_THEN_RESET(sync->flags.hasTrigger)){
		mrEnableMsgUpdatedSync(mr);
	}

	return 1;
}

S32 mrInteractionDestroy(MovementRequester** mrInOut){
	MovementRequester*	mr = SAFE_DEREF(mrInOut);
	MRInteractionSync*	sync;

	GET_SYNC(&sync);
	*mrInOut = NULL;
	sync->flags.destroySelf = 1;
	mrEnableMsgUpdatedSync(mr);
	
	return 1;
}

S32 mrInteractionTriggerWaypoint(	MovementRequester* mr,
									U32 waypointIndex)
{
	MRInteractionFG*	fg;
	MRInteractionSync*	sync;
	
	GET_FG(&fg);
	GET_SYNC(&sync);
	
	if(fg->flags.pathVersionAckedByBG){
		sync->flags.hasTrigger = 1;
		sync->triggerWaypointIndex = waypointIndex;
		mrEnableMsgUpdatedSync(mr);
	}
	
	return 1;
}

static void createSitWaypoint(	MRInteractionWaypoint** wpOut,
								MRInteractionPath* p,
								const Vec3 pos,
								const Quat rot)
{
	MRInteractionWaypoint* wp;
	
	*wpOut = wp = StructAlloc(parse_MRInteractionWaypoint);
	eaPush(&p->wps, wp);
	copyVec3(pos, wp->pos);
	copyQuat(rot, wp->rot);
}

S32 mrInteractionCreatePathForSit(	MRInteractionPath** pOut,
									const Vec3 posFeet,
									const Vec3 posKnees,
									const Quat rot,
									const U32* bitHandlesPre,
									const U32* bitHandlesHold,
									F32 secondsToMove,
									F32 secondsPostHold)
{
	MRInteractionPath*		p;
	MRInteractionWaypoint*	wp;
	
	if(	!pOut ||
		!posFeet ||
		!posKnees ||
		!rot ||
		!bitHandlesPre ||
		!bitHandlesHold)
	{
		return 0;
	}

	// Create a new path.
	
	p = *pOut = StructAlloc(parse_MRInteractionPath);

	// Make the feet waypoint to walk to.

	createSitWaypoint(&wp, p, posFeet, rot);
	
	// Make the "knees" waypoint to move to.
	
	createSitWaypoint(&wp, p, posKnees, rot);
	wp->flags.moveToOnRail = 1;
	wp->flags.playAnimDuringMove = 1;
	wp->seconds = MAX(0.f, secondsToMove);

	if(gConf.bNewAnimationSystem){
		wp->animToStart = eaiGet(&bitHandlesPre, 0);
	}else{
		eaiCopy(&wp->animBitHandles, &bitHandlesPre);
	}
	
	// Make the "knees" waypoint to hold at for a minimum time.
	
	createSitWaypoint(&wp, p, posKnees, rot);
	wp->flags.snapToWaypoint = 1;
	wp->seconds = 1.f;

	if(gConf.bNewAnimationSystem){
		wp->animToStart = eaiGet(&bitHandlesHold, 0);
	}else{
		eaiCopy(&wp->animBitHandles, &bitHandlesHold);
	}
	
	// Make the "knees" waypoint to hold at until a key is pressed.
	
	createSitWaypoint(&wp, p, posKnees, rot);
	wp->flags.releaseOnInput = 1;
	wp->flags.snapToWaypoint = 1;

	if(!gConf.bNewAnimationSystem){
		eaiCopy(&wp->animBitHandles, &bitHandlesHold);
	}
	
	// Make the feet waypoint again, to move back to.

	createSitWaypoint(&wp, p, posFeet, rot);
	wp->flags.moveToOnRail = 1;
	wp->flags.jumpHereOnRailFail = 1;
	wp->seconds = MAX(0.f, secondsToMove);
	
	// Make the feet waypoint again, to hold at.

	createSitWaypoint(&wp, p, posFeet, rot);
	wp->flags.snapToWaypoint = 1;
	wp->seconds = MAX(0.f, secondsPostHold);
	
	return 1;
}

#include "AutoGen/EntityMovementInteraction_h_ast.c"
#include "AutoGen/EntityMovementInteraction_c_ast.c"
