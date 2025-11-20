/***************************************************************************
*     Copyright (c) 2005-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Physics););

#include "EntityMovementTargetedRotation.h"
#include "EntityMovementManager.h"
#include "Quat.h"


AUTO_RUN_MM_REGISTER_REQUESTER_MSG_HANDLER(	mmTargetedRotationMsgHandler,
											"TargetedRotationMovement",
											TargetedRotation);

AUTO_STRUCT;
typedef struct TargetedRotationFG {
	S32	unused;
} TargetedRotationFG;

AUTO_STRUCT;
typedef struct TargetedRotationBG {
	Vec3						vel;
} TargetedRotationBG;

AUTO_STRUCT;
typedef struct TargetedRotationLocalBGFlags {
	U32								createdControlsDir : 1;
} TargetedRotationLocalBGFlags;

AUTO_STRUCT;
typedef struct TargetedRotationLocalBG {
	Vec3							controlsDir;
	TargetedRotationLocalBGFlags	flags;
} TargetedRotationLocalBG;

AUTO_STRUCT;
typedef struct TargetedRotationToFG {
	S32	unused;
} TargetedRotationToFG;

AUTO_STRUCT;
typedef struct TargetedRotationToBG {
	S32 unused;
} TargetedRotationToBG;

AUTO_STRUCT;
typedef struct TargetedRotationSyncFlags {
	U32							enabled : 1;
} TargetedRotationSyncFlags;

AUTO_STRUCT;
typedef struct TargetedRotationSync {
	EntityRef					entRef;
	Vec3						pos;

	TargetedRotationSyncFlags	flags;
} TargetedRotationSync;

void mmTargetedRotationGetControlsDirBG(const MovementRequesterMsg* msg,
										TargetedRotationLocalBG* localBG,
										Vec3 dirOut)
{
	if(FALSE_THEN_SET(localBG->flags.createdControlsDir)){
		Vec3	relativeDir;
		Vec3	yawDir;

		#define DIFF(a, b) (mrmGetInputValueBitBG(msg, a) - mrmGetInputValueBitBG(msg, b))

		setVec3(relativeDir,
				DIFF(MIVI_BIT_RIGHT,	MIVI_BIT_LEFT),
				DIFF(MIVI_BIT_UP,		MIVI_BIT_DOWN),
				DIFF(MIVI_BIT_FORWARD,	MIVI_BIT_BACKWARD));

		#undef DIFF

		sincosf(mrmGetInputValueF32BG(msg, MIVI_F32_MOVE_YAW),
				yawDir + 0,
				yawDir + 2);

		setVec3(localBG->controlsDir,
				relativeDir[0] * yawDir[2] +
				relativeDir[2] * yawDir[0],
				relativeDir[1],
				relativeDir[2] * yawDir[2] -
				relativeDir[0] * yawDir[0]);

		normalVec3XZ(localBG->controlsDir);
	}

	copyVec3(	localBG->controlsDir,
				dirOut);
}

static void mmHandleMsgCreateOutput(const MovementRequesterMsg* msg,
									TargetedRotationBG* bg,
									TargetedRotationLocalBG* localBG,
									TargetedRotationSync* sync)
{
	const MovementRequesterMsgCreateOutputShared* shared = msg->in.bg.createOutput.shared;

	switch(msg->in.bg.createOutput.dataClassBit){
		xcase MDC_BIT_POSITION_CHANGE:{
			S32		ignoreSpeedType = 0;

			S32		slow = 0;
			S32		jump = 0;
			F32		speedScale = 1.f;

			S32		hasTargetPos = 0;

			Vec3	curPos = {0};
			Vec3	pivotPos = {0};
			Vec3	vecToPivotXZ = {0};
			Vec3	dirToPivotXZ = {0};
			F32		curDistToPivotXZ;
			
			// Get the pivot.
			
			if(sync->entRef){
				if(!mrmGetEntityPositionBG(msg, sync->entRef, pivotPos)){
					break;
				}
			}else{
				copyVec3(sync->pos, pivotPos);
			}
			
			// Get the current position and the vec to the pivot.
			
			mrmGetPositionBG(msg, curPos);
			
			subVec3XZ(	pivotPos,
						curPos,
						vecToPivotXZ);
					
			copyVec3(	vecToPivotXZ,
						dirToPivotXZ);
						
			curDistToPivotXZ = normalVec3XZ(dirToPivotXZ);
			
			if(curDistToPivotXZ <= 0.1f){
				break;
			}
					
			// Check the target type.

			switch(shared->target.pos->targetType){
				xcase MPTT_INPUT:{
					Vec3 controlsDirXZ;

					mmTargetedRotationGetControlsDirBG(msg, localBG, controlsDirXZ);

					controlsDirXZ[1] = 0;
					
					if(!vec3IsZeroXZ(controlsDirXZ)){
						speedScale = mrmGetInputValueF32BG(msg, MIVI_F32_DIRECTION_SCALE);
					}

					slow = mrmGetInputValueBitBG(msg, MIVI_BIT_SLOW);
					
					if(	mrmGetInputValueBitBG(msg, MIVI_BIT_UP) &&
						bg->vel[1] <= 0.f)
					{
						bg->vel[1] = 40.f;
					}
					
					if(!vec3IsZeroXZ(controlsDirXZ)){
						F32		d = dotVec3XZ(dirToPivotXZ, controlsDirXZ);
						F32		yawScale = 1.f;
						
						if(d < cosf(RAD(90.f + 35.f))){
							F32 scale = (d - cosf(RAD(90.f + 35.f))) /
										(-1.f - cosf(RAD(90.f + 35.f)));
										
							yawScale = 1.f - scale;
							curDistToPivotXZ += scale * MM_SECONDS_PER_STEP * speedScale * 15;
						}
						else if(d > cosf(RAD(90.f - 35.f))){
							F32 scale = (d - cosf(RAD(90.f - 35.f))) /
										(1.f - cosf(RAD(90.f - 35.f)));
							
							yawScale = 1.f - scale;
							curDistToPivotXZ -= scale * MM_SECONDS_PER_STEP * speedScale * 15;
						}
						
						{
							F32		yawFromPivot;
							Vec3	posAtControlsDirXZ;
							Vec3	vecToControlsDirXZ;
							F32		yawToControlsDir;
							F32		yawDeltaToControlsDir;
							F32		yawDelta;
							Vec3	targetPosXZ;
							F32		yawToTargetPos;
							Vec3	offsetToTargetPos;
						
							yawFromPivot = addAngle(getVec3Yaw(dirToPivotXZ), PI);
							
							addVec3XZ(	curPos,
										controlsDirXZ,
										posAtControlsDirXZ);
							
							subVec3XZ(	posAtControlsDirXZ,
										pivotPos,
										vecToControlsDirXZ);
									
							yawToControlsDir = getVec3Yaw(vecToControlsDirXZ);
							
							yawDeltaToControlsDir = subAngle(yawToControlsDir, yawFromPivot);
							
							yawDelta = yawScale * MM_SECONDS_PER_STEP * speedScale * 15 / curDistToPivotXZ;
							
							if(yawDeltaToControlsDir < 0){
								yawDelta *= -1.f;
							}
							
							yawToTargetPos = addAngle(	yawFromPivot,
														yawDelta);
										
							targetPosXZ[0] = pivotPos[0] + curDistToPivotXZ * sinf(yawToTargetPos);
							targetPosXZ[2] = pivotPos[2] + curDistToPivotXZ * cosf(yawToTargetPos);
							
							subVec3XZ(	targetPosXZ,
										curPos,
										offsetToTargetPos);
										
							bg->vel[1] += -80 * MM_SECONDS_PER_STEP;
										
							offsetToTargetPos[1] = bg->vel[1] * MM_SECONDS_PER_STEP;
							
							mrmTranslatePositionBG(	msg,
													offsetToTargetPos,
													1, 
													0);
						}
					}
				}

				xcase MPTT_POINT:{
					//copyVec3(	shared->target.pos->point,
					//			targetPos);
				}

				xcase MPTT_VELOCITY:{
					// Release for a while.
				}

				xdefault:{
					//zeroVec3(targetDirXZ);
				}
			}

			switch(shared->target.pos->speedType){
				xcase MST_NORMAL:{
				}

				xcase MST_OVERRIDE:{
				}
				
				xdefault:{
					mrmReleaseAllDataOwnershipBG(msg);
					return;
				}
			}
		}

		xcase MDC_BIT_ROTATION_TARGET:{
			mrmRotationTargetSetAsInputBG(msg);
		}

		xcase MDC_BIT_ROTATION_CHANGE:{
			Vec3 pivotPos;
			Quat curRot;
			Quat newRot;
			Quat targetRot;

			mrmGetRotationBG(msg, curRot);

			switch(shared->target.rot->targetType){
				xcase MRTT_INPUT:{
					Vec3 curPos;
					Vec3 dir;

					if(sync->entRef){
						if(!mrmGetEntityPositionBG(msg, sync->entRef, pivotPos)){
							break;
						}
					}else{
						copyVec3(sync->pos, pivotPos);
					}

					mrmGetPositionBG(msg, curPos);

					subVec3(pivotPos,
							curPos,
							dir);

					if(!vec3IsZeroXZ(dir)){
						yawQuat(-getVec3Yaw(dir),
								targetRot);
					}
				}
				
				xdefault:{
					mrmReleaseAllDataOwnershipBG(msg);
					return;
				}
			}

			switch(shared->target.rot->speedType){
				xcase MST_NORMAL:{
				}

				xdefault:{
					mrmReleaseAllDataOwnershipBG(msg);
					return;
				}
			}

			quatInterp(	0.3f,
						curRot,
						targetRot,
						newRot);

			mrmSetRotationBG(msg, newRot);
		}

		xcase MDC_BIT_ANIMATION:{
			Vec3	pos;
			Vec3	vel;
			Quat	rot;
			Vec3	dirFace;
			F32		yawFace;
			F32		yawVel;
			F32		yawDiff;
			
			mrmGetPositionAndRotationBG(msg, pos, rot);
			
			subVec3XZ(	pos,
						shared->orig.pos,
						vel);
			
			mrmAnimAddBitBG(msg, mmAnimBitHandles.lockon);

			if(vec3IsZeroXZ(vel)){
				break;
			}
			
			yawVel = getVec3Yaw(vel);
			
			quatToMat3_2(	rot,
							dirFace);
			
			yawFace = getVec3Yaw(dirFace);
			
			yawDiff = subAngle(yawVel, yawFace);
			
			mrmAnimAddBitBG(msg, mmAnimBitHandles.move);

			if(fabs(yawDiff) < RAD(67.5f)){
				mrmAnimAddBitBG(msg, mmAnimBitHandles.forward);
			}
			else if(fabs(addAngle(yawDiff, PI)) < RAD(67.5f)){
				mrmAnimAddBitBG(msg, mmAnimBitHandles.backward);
			}
			
			if(fabs(yawDiff - RAD(90.f)) < RAD(67.5f)){
				mrmAnimAddBitBG(msg, mmAnimBitHandles.right);
			}
			else if(fabs(yawDiff - RAD(-90.f)) < RAD(67.5f)){
				mrmAnimAddBitBG(msg, mmAnimBitHandles.left);
			}
		}
	}
}

void mmTargetedRotationMsgHandler(const MovementRequesterMsg* msg){
	TargetedRotationFG*			fg;
	TargetedRotationBG*			bg;
	TargetedRotationLocalBG*	localBG;
	TargetedRotationToFG*		toFG;
	TargetedRotationToBG*		toBG;
	TargetedRotationSync*		sync;
	
	MR_MSG_HANDLER_GET_DATA_DEFAULT(msg, TargetedRotation);

	switch(msg->in.msgType){
		xcase MR_MSG_BG_INITIALIZE:{
			const U32 handledMsgs =	MR_HANDLED_MSG_BEFORE_DISCUSSION |
									MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP;
									
			mrmHandledMsgsAddBG(msg, handledMsgs);
		}

		xcase MR_MSG_BG_GET_DEBUG_STRING:{
			char*	buffer = msg->in.bg.getDebugString.buffer;
			U32		bufferLen = msg->in.bg.getDebugString.bufferLen;

			snprintf_s(	buffer,
						bufferLen,
						"");
		}

		xcase MR_MSG_BG_DATA_WAS_RELEASED:{
			if(msg->in.bg.dataWasReleased.dataClassBits & MDC_BIT_ANIMATION){
				mrmReleaseAllDataOwnershipBG(msg);
			}
		}
		
		xcase MR_MSG_BG_BEFORE_DISCUSSION:{
			if(!sync->flags.enabled){
				mrmReleaseAllDataOwnershipBG(msg);
			}
		}
		
		xcase MR_MSG_BG_DISCUSS_DATA_OWNERSHIP:{
			localBG->flags.createdControlsDir = 0;
			
			if(sync->flags.enabled){
				if(!msg->in.bg.discussDataOwnership.flags.isDuringCreateOutput){
					mrmAcquireDataOwnershipBG(msg, MDC_BITS_CHANGE_ALL, 1, NULL, NULL);
				}
			}
		}
		
		xcase MR_MSG_BG_CREATE_OUTPUT:{
			mmHandleMsgCreateOutput(msg,
									bg,
									localBG,
									sync);
		}
	}
}

#define GET_SYNC(sync) MR_GET_SYNC(mr, mmTargetedRotationMsgHandler, TargetedRotation, sync)

S32 mmTargetedRotationMovementSetEnabled(	MovementRequester* mr,
											S32 enabled,
											EntityRef erTarget)
{
	TargetedRotationSync* sync;

	if(GET_SYNC(&sync)){
		sync->flags.enabled = !!enabled;
		sync->entRef = erTarget;

		return 1;
	}

	return 0;
}

#include "AutoGen/EntityMovementTargetedRotation_c_ast.c"

