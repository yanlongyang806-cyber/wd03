/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "EntityMovementManager.h"
#include "EntityLib.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Physics););

// Platform requester stuff.

AUTO_RUN_MM_REGISTER_REQUESTER_MSG_HANDLER(	mmPlatformMovementMsgHandler,
											"PlatformMovement",
											MovementPlatform);

AUTO_STRUCT;
typedef struct MovementPlatformFG {
	S32								unused;
} MovementPlatformFG;

AUTO_STRUCT;
typedef struct MovementPlatformBG {
	Vec3							vel;

	Quat							targetRot;
} MovementPlatformBG;

AUTO_STRUCT;
typedef struct MovementPlatformLocalBG {
	S32								unused;
} MovementPlatformLocalBG;

AUTO_STRUCT;
typedef struct MovementPlatformToFG {
	S32								unused;
} MovementPlatformToFG;

AUTO_STRUCT;
typedef struct MovementPlatformToBG {
	S32								unused;
} MovementPlatformToBG;

AUTO_STRUCT;
typedef struct MovementPlatformSync {
	F32								maxSpeed;

	Vec3							vel;
	U32								useVel : 1;
} MovementPlatformSync;

static void mmPlatformMovementMsgCreateOutput(	const MovementRequesterMsg* msg,
												MovementPlatformBG* bg,
												MovementPlatformSync* sync)
{
	const MovementRequesterMsgCreateOutputShared* shared = msg->in.bg.createOutput.shared;

	switch(msg->in.bg.createOutput.dataClassBit){
		xcase MDC_BIT_POSITION_TARGET:{
			//mrmTargetSetAsInputBG(msg);
			//mrmTargetSetSpeedAsConstantBG(msg, sync->maxSpeed);
			//mrmTargetSetUseYBG(msg, 1);
		}

		xcase MDC_BIT_POSITION_CHANGE:{
			S32		hasTargetPos = 0;
			Vec3 	targetPos = {0};
			Vec3	targetDir = {0};
			Vec3	targetVel = {0};
			F32		targetSpd = 0;
			F32		distance = 0;

			if(sync->useVel){
				mrmLog(	msg,
						NULL,
						"Applying sync vel:"
						" (%1.2f, %1.2f, %1.2f)"
						" [%8.8x, %8.8x, %8.8x]",
						vecParamsXYZ(sync->vel),
						vecParamsXYZ((S32*)sync->vel));

				copyVec3(sync->vel, bg->vel);
				sync->useVel = 0;
			}

			switch(shared->target.pos->targetType){
				xcase MPTT_POINT:{
					copyVec3(	shared->target.pos->point,
								targetPos);

					hasTargetPos = 1;
				}

				xdefault:{
					zeroVec3(targetDir);
				}
			}

			if(hasTargetPos){
				Vec3 curPos;

				mrmGetPositionBG(msg,
					curPos);

				subVec3(targetPos,
					curPos,
					targetDir);

				distance = normalVec3(targetDir);
			}

			targetSpd = shared->target.pos->speed;

			switch(shared->target.pos->speedType){
				xcase MST_NORMAL:
					targetSpd = sync->maxSpeed ? sync->maxSpeed : 0.2;
				case MST_CONSTANT:{
					normalVec3(targetDir);

					scaleVec3(	targetDir,
								targetSpd > distance ? distance : targetSpd,
								bg->vel);
					
					zeroVec3(targetVel);
				}
			}

			copyVec3(bg->vel, targetVel);

			/*targetVel[0] = 0.00000001;
			targetVel[2] = 0.00000001;*/

			mrmTranslatePositionBG(msg, targetVel, 1, 0);
		}

		xcase MDC_BIT_ROTATION_TARGET:{
		}

		xcase MDC_BIT_ROTATION_CHANGE:{
			Quat curRot;
			Quat newRot;

			mrmGetRotationBG(msg, curRot);

			switch(shared->target.rot->targetType){
				xcase MRTT_INPUT:{
					// Disabled because mmGetControlsDirBG was removed.
					#if 0
					Vec3 dir;

					mmGetControlsDirBG(msg, dir);

					if(!vec3IsZeroXZ(dir)){
						unitQuat(bg->targetRot);

						yawQuat(-getVec3Yaw(dir),
							bg->targetRot);
					}
					#endif
				}

				xcase MRTT_DIRECTION:{
					Vec3 dir;

					copyVec3(	shared->target.rot->dir,
						dir);

					if(!vec3IsZeroXZ(dir)){
						unitQuat(bg->targetRot);

						yawQuat(-getVec3Yaw(dir),
							bg->targetRot);
					}
				}

				xcase MRTT_ROTATION:{
					copyQuat(	shared->target.rot->rot,
						bg->targetRot);
				}

				xcase MRTT_POINT:{
					Vec3 curPos;
					Vec3 dir;

					mrmGetPositionBG(msg, curPos);

					subVec3(shared->target.rot->point,
						curPos,
						dir);

					if(!vec3IsZeroXZ(dir)){
						unitQuat(bg->targetRot);

						yawQuat(-getVec3Yaw(dir),
							bg->targetRot);
					}
				}

				xcase MRTT_ENTITY:{
				}
			}

			switch(shared->target.rot->speedType){
				xcase MST_NORMAL:{
				}

				xcase MST_CONSTANT:{
				}

				xcase MST_IMPULSE:{
				}
			}

			quatInterp(	0.1f,
				curRot,
				bg->targetRot,
				newRot);

			mrmSetRotationBG(msg, newRot);

			{
				Vec3 pyFace;
				quatToPYR(newRot, pyFace);
				mrmSetFacePitchYawBG(msg, pyFace);
			}
		}

		xcase MDC_BIT_ANIMATION:{

		}
	}
}

void mmPlatformMovementMsgHandler(const MovementRequesterMsg* msg){
	MovementPlatformFG*			fg;
	MovementPlatformBG*			bg;
	MovementPlatformLocalBG*	localBG;
	MovementPlatformToFG*		toFG;
	MovementPlatformToBG*		toBG;
	MovementPlatformSync*		sync;

	MR_MSG_HANDLER_GET_DATA_DEFAULT(msg, MovementPlatform);

	switch(msg->in.msgType){
		xcase MR_MSG_FG_AFTER_SYNC:{
			if(TRUE_THEN_RESET(sync->useVel)){
				mrmEnableMsgUpdatedSyncFG(msg);
			}
		}

		xcase MR_MSG_BG_INITIALIZE:{
			const U32 handledMsgs =	MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP;
									
			mrmHandledMsgsAddBG(msg, handledMsgs);
		}

		xcase MR_MSG_BG_QUERY_ON_GROUND:{
			msg->out->bg.queryOnGround.onGround = 0;
		}

		xcase MR_MSG_GET_SYNC_DEBUG_STRING:{
			char*	buffer = msg->in.getSyncDebugString.buffer;
			U32		bufferLen = msg->in.getSyncDebugString.bufferLen;

			snprintf_s(	buffer,
						bufferLen,
						" spd(%1.2f) [%8.8x]"
						" v%s(%1.2f, %1.2f, %1.2f) [%8.8x, %8.8x, %8.8x]",
						sync->maxSpeed,
						*(S32*)&sync->maxSpeed,
						sync->useVel ? ".use" : "",
						vecParamsXYZ(sync->vel),
						vecParamsXYZ((S32*)sync->vel)
						);
		}

		xcase MR_MSG_BG_GET_DEBUG_STRING:{
			char*	buffer = msg->in.bg.getDebugString.buffer;
			U32		bufferLen = msg->in.bg.getDebugString.bufferLen;

			snprintf_s(	buffer,
						bufferLen,
						"v(%1.2f, %1.2f, %1.2f) [%8.8x, %8.8x, %8.8x]"
						" tr(%1.2f, %1.2f, %1.2f, %1.2f) [%8.8x, %8.8x, %8.8x, %8.8x]"
						,
						vecParamsXYZ(bg->vel),
						vecParamsXYZ((S32*)bg->vel),
						quatParamsXYZW(bg->targetRot),
						quatParamsXYZW((S32*)bg->targetRot));
		}

		xcase MR_MSG_BG_DISCUSS_DATA_OWNERSHIP:{
			mrmAcquireDataOwnershipBG(	msg,
										MDC_BIT_ROTATION_CHANGE|MDC_BIT_POSITION_CHANGE,
										0,
										NULL,
										NULL);
		}

		xcase MR_MSG_BG_CREATE_OUTPUT:{
			mmPlatformMovementMsgCreateOutput(msg, bg, sync);
		}
	}
}

S32 mmPlatformMovementSetSpeed(	MovementRequester* mr,
								F32 speed)
{
	MovementPlatformSync* sync;

	if(mrGetSyncFG(	mr,
					mmPlatformMovementMsgHandler,
					&sync,
					NULL))
	{
		sync->maxSpeed = speed;

		return 1;
	}

	return 0;
}

S32 mmPlatformMovementSetVelocity(	MovementRequester* mr,
								 const Vec3 vel)
{
	MovementPlatformSync* sync;

	if(mrGetSyncFG(	mr,
					mmPlatformMovementMsgHandler,
					&sync,
					NULL))
	{
		copyVec3(vel, sync->vel);
		sync->useVel = 1;

		return 1;
	}

	return 0;
}

#include "AutoGen/EntityMovementPlatform_c_ast.c"