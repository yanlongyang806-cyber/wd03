#include "EntityMovementDefault.h"
#include "EntityMovementManager.h"
#include "EntityMovementFx.h"
#include "EntityLib.h"
#include "PhysicsSDK.h"
#include "rand.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Physics););


AUTO_RUN_MM_REGISTER_RESOURCE_MSG_HANDLER(	mmrDrawMsgHandler,
										  "Draw",
										  MMRDraw,
										  MDC_BIT_POSITION_CHANGE);

AUTO_STRUCT;
typedef struct MMRDrawConstantNP {
	U32 unused;
} MMRDrawConstantNP;

AUTO_STRUCT;
typedef struct MMRDrawConstant {
	Vec3 pos;
} MMRDrawConstant;

AUTO_STRUCT;
typedef struct MMRDrawActivatedFG {
	U32 unused;
} MMRDrawActivatedFG;

AUTO_STRUCT;
typedef struct MMRDrawActivatedBG {
	S32	unused;
} MMRDrawActivatedBG;

AUTO_STRUCT;
typedef struct MMRDrawState {
	U32 unused;
} MMRDrawState;

void mmrDrawMsgHandler(const MovementManagedResourceMsg* msg){
	const MMRDrawConstant* constant = msg->in.constant;

	switch(msg->in.msgType){
		xcase MMR_MSG_GET_CONSTANT_DEBUG_STRING:{
			char** estrBuffer = msg->in.getDebugString.estrBuffer;
		}

		xcase MMR_MSG_FG_SET_STATE:{
			mmrmSetAlwaysDrawFG(msg, 1);
		}

		xcase MMR_MSG_FG_DESTROYED:{
		}

		xcase MMR_MSG_FG_DEBUG_DRAW:{
		}

		xcase MMR_MSG_FG_ALWAYS_DRAW:{
			const MovementDrawFuncs*	funcs = msg->in.fg.alwaysDraw.drawFuncs;
			Entity*						e = msg->in.fg.mmUserPointer;
			Vec3						ePos;
			Quat						eRot;
			Vec3						eUpVec;

			if(	!e ||
				!msg->in.activatedStruct)
			{
				break;
			}

			entGetPos(e, ePos);
			entGetRot(e, eRot);

			quatToMat3_1(eRot, eUpVec);

			scaleAddVec3(eUpVec, 4, ePos, ePos);

			funcs->drawLine3D(ePos, 0xff808080, constant->pos, 0xffffffff);
			funcs->drawCapsule3D(constant->pos, zerovec3, 0, 0.5f, 0xffff0000);
		}
	}
}

static U32 mmrDrawGetResourceID(void){
	static U32 id;

	if(!id){
		if(!mmGetManagedResourceIDByMsgHandler(mmrDrawMsgHandler, &id)){
			assert(0);
		}
	}

	return id;
}

void mrSwingMsgHandler(const MovementRequesterMsg* msg);

AUTO_RUN_MM_REGISTER_REQUESTER_MSG_HANDLER(	mrSwingMsgHandler,
											"SwingMovement",
											Swing);

static F32 s_fSwingAccelMaxSpeedFactor = 0.45f;
static F32 s_fSwingClearHeight = 26.0f;
static S32 s_bUseNewSwingingLogic = 1;
static S32 s_uSwingAnchorCooldown = (S32)(MM_STEPS_PER_SECOND * 0.75f);

static F32 s_fSwingPitchMin = RAD(80.f);
static F32 s_fSwingPitchMinRange = RAD(-20.f);
static F32 s_fSwingPitchRangeMin = RAD(-25.f);
static F32 s_fSwingPitchRangeRange = RAD(8.f);

static F32 s_fSwingYawMin = RAD(1.f);

static F32 s_fSwingYawRangeMin = RAD(3.f);
static F32 s_fSwingYawRangeRange = RAD(8.f);

#define DEFAULT_SWING_SPEED 75.f


AUTO_STRUCT;
typedef struct SwingFG {
	S32	unused;
} SwingFG;

AUTO_STRUCT;
typedef struct SwingBGFlags {
	U32				hasAnchor		: 1;
	U32				buttonReleased	: 1;
} SwingBGFlags;

AUTO_STRUCT;
typedef struct SwingBG {
	Vec3			anchor;
	Vec3			vel;
	U32				mmrDrawHandle;
	U32				mmrFxRope;
	F32				yawFace;
	SwingBGFlags	flags;
} SwingBG;

AUTO_STRUCT;
typedef struct SwingLocalBG {
	Quat			rotTarget;
	U32				lastAnchorTimeStamp;
} SwingLocalBG;

AUTO_STRUCT;
typedef struct SwingToFG {
	S32	unused;
} SwingToFG;

AUTO_STRUCT;
typedef struct SwingToBG {
	S32 unused;
} SwingToBG;

AUTO_STRUCT;
typedef struct SwingSync {
	F32 maxSpeed;
	const char * pcSwingingFx;		AST(POOL_STRING)
} SwingSync;

static void mrSwingDestroyRopeBG(	const MovementRequesterMsg* msg,
									SwingBG* bg)
{
	if(1){
		mmrFxDestroyBG(msg, &bg->mmrFxRope);
	}else{
		mrmResourceDestroyBG(	msg,
								mmrDrawGetResourceID(),
								&bg->mmrDrawHandle);
	}
}

#define GET_SYNC(sync)			MR_GET_SYNC(mr, mrSwingMsgHandler, Swing, sync)
#define IF_DIFF_THEN_SET(a, b)	MR_SYNC_SET_IF_DIFF(mr, a, b)

S32 mrSwingSetMaxSpeed( MovementRequester* mr,
						F32 maxSpeed)
{
	SwingSync* sync;
	
	if(GET_SYNC(&sync)){
		MAX1(maxSpeed, 1.f);
		IF_DIFF_THEN_SET(sync->maxSpeed, maxSpeed);
		return 1;
	}

	return 0;
}

// The passed in string of the dynfxinfo
void mrSwingSetFx(MovementRequester* mr, const char *pcSwingingFx)
{
	SwingSync* sync;

	if(pcSwingingFx && GET_SYNC(&sync))
	{
		sync->pcSwingingFx = pcSwingingFx;
	}
}

void mrSwingMsgHandler(const MovementRequesterMsg* msg){
	SwingFG*			fg;
	SwingBG*			bg;
	SwingLocalBG*	localBG;
	SwingToFG*		toFG;
	SwingToBG*		toBG;
	SwingSync*		sync;
	
	MR_MSG_HANDLER_GET_DATA_DEFAULT(msg, Swing);

	switch(msg->in.msgType){
		xcase MR_MSG_FG_CREATE_TOBG:{
			mrmEnableMsgUpdatedToBG(msg);
		}
		
		xcase MR_MSG_BG_GET_DEBUG_STRING:{
			char*	buffer = msg->in.bg.getDebugString.buffer;
			U32		bufferLen = msg->in.bg.getDebugString.bufferLen;

			snprintf_s(	buffer,
						bufferLen,
						"Vel (%1.3f, %1.3f, %1.3f) [%8.8x, %8.8x, %8.8x]\n"
						"Anchor (%1.3f, %1.3f, %1.3f) [%8.8x, %8.8x, %8.8x]\n"
						"Fx %u\n"
						"Flags: %s%s",
						vecParamsXYZ(bg->vel),
						vecParamsXYZ((S32*)bg->vel),
						vecParamsXYZ(bg->anchor),
						vecParamsXYZ((S32*)bg->anchor),
						bg->mmrFxRope,
						bg->flags.buttonReleased ? "buttonReleased, " : "",
						bg->flags.hasAnchor ? "hasAnchor, " : "");
		}

		xcase MR_MSG_GET_SYNC_DEBUG_STRING:{
			char*	buffer = msg->in.getSyncDebugString.buffer;
			U32		bufferLen = msg->in.getSyncDebugString.bufferLen;

			snprintf_s(	buffer,
						bufferLen,
						"MaxSpeed: %1.3f [%8.8x]",
						sync->maxSpeed,
						*(S32*)&sync->maxSpeed);
		}

		xcase MR_MSG_BG_INITIALIZE:{
			mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_INPUT_EVENT);

			if (sync->maxSpeed <= 1){
				sync->maxSpeed = DEFAULT_SWING_SPEED;
			}

		}

		xcase MR_MSG_BG_UPDATED_SYNC:{
 			if (sync->maxSpeed <= 1){
				sync->maxSpeed = DEFAULT_SWING_SPEED;
			}
		}

		xcase MR_MSG_BG_RECEIVE_OLD_DATA:{
			const MovementSharedData* sd = msg->in.bg.receiveOldData.sharedData;
			
			switch(sd->dataType){
				xcase MSDT_VEC3:{
					if(!stricmp(sd->name, "Velocity")){
						copyVec3(	sd->data.vec3,
									bg->vel);
					}
				}
				
				xcase MSDT_F32:{
					if(!stricmp(sd->name, "InputFaceYaw")){
						bg->yawFace = sd->data.f32;
					}
				}

				//xcase MSDT_QUAT:{
				//	if(!stricmp(sd->name, "TargetRotation")){
				//		copyQuat(	sd->data.quat,
				//					bg->rotTarget);
				//	}
				//}
			}
		}
		
		xcase MR_MSG_BG_DATA_WAS_RELEASED:{
			U32 dataClassBits = msg->in.bg.dataWasReleased.dataClassBits;

			if(dataClassBits & MDC_BIT_POSITION_CHANGE){
				Vec3 pyr;
				
				quatToPYR(localBG->rotTarget, pyr);
				
				bg->flags.hasAnchor = 0;

				if(bg->vel[1] > 0.f){
					F32 speed = lengthVec3(bg->vel);
					F32 newSpeed = MIN(150.f, speed + 50.f);
					F32 scale = newSpeed / speed;
						
					scaleVec3(bg->vel, scale, bg->vel);
				}

				mrmShareOldS32BG(msg, "OffGroundIntentionally", 1);
				mrmShareOldVec3BG(msg, "Velocity", bg->vel);
				mrmShareOldQuatBG(msg, "TargetRotation", localBG->rotTarget);
				mrmShareOldF32BG(msg, "TargetFaceYaw", pyr[1]);
				zeroVec3(bg->vel);

				mrSwingDestroyRopeBG(msg, bg);
			}

			mrmReleaseAllDataOwnershipBG(msg);
		}
		
		xcase MR_MSG_BG_DATA_RELEASE_REQUESTED:{
		}
		
		xcase MR_MSG_BG_INPUT_EVENT:{
			if(msg->in.bg.inputEvent.value.mivi == MIVI_BIT_UP){
				if(!msg->in.bg.inputEvent.value.bit){
					if(TRUE_THEN_RESET(bg->flags.hasAnchor)){
						mrmReleaseAllDataOwnershipBG(msg);

						mrSwingDestroyRopeBG(msg, bg);

						mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_CREATE_DETAILS);
					}
				}else{
					S32 onGround;
					
					if(	mrmGetOnGroundBG(msg, &onGround, NULL) &&
						!onGround && 
						mrmProcessCountHasPassedBG(msg, (localBG->lastAnchorTimeStamp + s_uSwingAnchorCooldown)))
					{
						mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
					}
				}
			}
		}
		
		xcase MR_MSG_BG_UPDATED_TOBG:{
		}
		
		xcase MR_MSG_BG_DISCUSS_DATA_OWNERSHIP:{
			if(mrmGetInputValueBitBG(msg, MIVI_BIT_UP)){
				mrmAcquireDataOwnershipBG(msg, MDC_BITS_CHANGE_ALL, 1, NULL, NULL);
			}

			mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
		}
		
		xcase MR_MSG_BG_CREATE_OUTPUT:{
			switch(msg->in.bg.createOutput.dataClassBit){
				xcase MDC_BIT_POSITION_CHANGE:{
					Vec3 posCur;
					
					mrmGetPositionBG(msg, posCur);

					if(	!bg->flags.hasAnchor &&
						mrmGetInputValueBitBG(msg, MIVI_BIT_UP))
					{
						F32		facingYaw, anchorYaw;
						
						mrmGetProcessCountBG(msg, &localBG->lastAnchorTimeStamp);

						facingYaw = bg->yawFace;

						if (s_bUseNewSwingingLogic){
							Vec3	vAnchorVec;
							F32		anchorDist;
							F32		anchorPitch; 
							F32		scalar;
							F32		minPitch;
							F32		pitchRange;
							F32		yawOutSwingRange;

							scalar = (sync->maxSpeed - 60.f)/(40.0f);
							MINMAX1(scalar, 0.0f, 1.0f);

 							minPitch = s_fSwingPitchMin + scalar * s_fSwingPitchMinRange;
							pitchRange = s_fSwingPitchRangeMin + scalar * s_fSwingPitchRangeRange;
							yawOutSwingRange = s_fSwingYawRangeMin + scalar * s_fSwingYawRangeRange;

							devassert(sync->maxSpeed != 0.0f);
							scalar = lengthVec3SquaredXZ(bg->vel) / SQR(sync->maxSpeed);
							MIN1(scalar, 1.0f);

							anchorPitch = minPitch + scalar * pitchRange;
							anchorDist = s_fSwingClearHeight / (1.0f - sinf(HALFPI - anchorPitch));
							anchorYaw = facingYaw;

							// add in some rotation to the anchor vector so we can get a little
							// curvy swing action
							{
								F32 velYaw = getVec3Yaw(bg->vel);
								F32 yawSwingRange = s_fSwingYawMin + scalar * yawOutSwingRange;
								
								if ((velYaw - facingYaw) > 0.0f) anchorYaw -= yawSwingRange;
								else anchorYaw += yawSwingRange;
							}
							
							sphericalCoordsToVec3(vAnchorVec, anchorYaw, anchorPitch, anchorDist);

							//
							addVec3(vAnchorVec, posCur, bg->anchor);
							
							// truncate the vert velocity if we are going up
							if (bg->vel[1] > 10.0f)
							{
								bg->vel[1] = 10.0f;
							}

							// dampen any velocity to the perp pull direction 
							// and then give ourselves a little boost in the swing direction
							{
								Vec3	faceDir = {sinf(facingYaw), 0.f, cosf(facingYaw)};
								Vec3	pullDir;
								Vec3	perpDir;
								F32		speedFactor;
								F32		fPerpDamp;

								crossVec3Up(faceDir, perpDir);
								fPerpDamp = -dotVec3(perpDir, bg->vel) * 0.25f;
								scaleAddVec3(perpDir, fPerpDamp, bg->vel, bg->vel);

								speedFactor = s_fSwingAccelMaxSpeedFactor * sync->maxSpeed;
								
								sphericalCoordsToVec3(vAnchorVec, anchorYaw, anchorPitch, 1.0f);

								crossVec3(perpDir, vAnchorVec, pullDir);
								normalVec3(pullDir);
								scaleAddVec3(pullDir, speedFactor, bg->vel, bg->vel);
							}
							
						}else{
							Vec3	yawVec = {sinf(facingYaw), 0.f, cosf(facingYaw)};
							U32		seed = *(S32*)&bg->vel[1];

							scaleAddVec3(yawVec, 50.f, posCur, bg->anchor);
							bg->anchor[1] += 30.f;

							FOR_BEGIN(i, 3);
							bg->anchor[i] += 5.f * randomF32Seeded(&seed, RandType_LCG);
							FOR_END;

						}
						
						bg->flags.hasAnchor = 1;

						// Create a temp thing until we have an fx.
						if(0){
							MMRDrawConstant constant = {0};
							
							copyVec3(bg->anchor, constant.pos);
							
							mrmResourceCreateBG(msg,
												&bg->mmrDrawHandle,
												mmrDrawGetResourceID(),
												&constant,
												NULL,
												NULL);
						}
					}

					if(bg->flags.hasAnchor){
						F32		len;
						Mat3	matRotation;
						F32		yaw;
						Vec3	yawVecStart;
						Vec3	yawVecEnd;
						Vec3	posEnd;
						F32		speed;
						
						mrmLogSegment(	msg,
										NULL,
										"swing",
										0xff808000,
										posCur,
										bg->anchor);

						subVec3(bg->anchor,
								posCur,
								matRotation[2]);

						len = normalVec3(matRotation[2]);

						if(bg->vel[1] < 0.f){
							F32 fFallingAccel;
							if (mrmGetInputValueBitBG(msg, MIVI_BIT_FORWARD)) {
								fFallingAccel = 140.f;
							}else if (mrmGetInputValueBitBG(msg, MIVI_BIT_BACKWARD)){
								fFallingAccel = 70.f;
							}else{
								fFallingAccel = 120.f;
							}

							bg->vel[1] -= fFallingAccel * MM_SECONDS_PER_STEP;

						}else{
							F32 fFallingDeccel;
							if (mrmGetInputValueBitBG(msg, MIVI_BIT_FORWARD)) {
								fFallingDeccel = 45.f;
							}else if (mrmGetInputValueBitBG(msg, MIVI_BIT_BACKWARD)){
								fFallingDeccel = 140.f;
							}else{
								fFallingDeccel = 60.f;
							}

							bg->vel[1] -= fFallingDeccel * MM_SECONDS_PER_STEP;
						}
						
						speed = lengthVec3(bg->vel);

						if(speed){
							crossVec3(	matRotation[2],
										bg->vel,
										matRotation[1]);
							
							normalVec3(matRotation[1]);

							crossVec3(	matRotation[1],
										matRotation[2],
										matRotation[0]);
							

							if(speed > sync->maxSpeed){
								F32 draggedSpeed = (speed - sync->maxSpeed) * 0.75f + sync->maxSpeed;
								
								scaleVec3(	bg->vel,
											draggedSpeed / speed,
											bg->vel);
								
								speed = draggedSpeed;
							}

							yaw = -speed * MM_SECONDS_PER_STEP / len;

							setVec3(yawVecStart, sinf(yaw), 0.f, cosf(yaw));
							
							mulVecMat3(yawVecStart, matRotation, yawVecEnd);
							
							scaleAddVec3(yawVecEnd, -len, bg->anchor, posEnd);
							
							subVec3(posEnd, posCur, bg->vel);
							
							mrmTranslatePositionBG(msg, bg->vel, 1, 0);
							
							{
								Vec3 posAfter;
								Vec3 vecDelta;
								
								mrmGetPositionBG(msg, posAfter);
								
								subVec3(posAfter, posCur, vecDelta);
								
								if(distance3(vecDelta, bg->vel) > lengthVec3(bg->vel) * 0.1f){
									Vec3 vecToAnchor;
									
									subVec3(bg->anchor, posAfter, vecToAnchor);
									
									if(normalVec3(vecToAnchor) > 1.f){
										Vec3 posAfterFix;
										
										scaleVec3(bg->vel, 0.95f, bg->vel);
										
										scaleVec3(vecToAnchor, 1.f, vecToAnchor);

										mrmTranslatePositionBG(msg, vecToAnchor, 1, 0);
										
										mrmGetPositionBG(msg, posAfterFix);

										if(distance3(posAfterFix, posCur) < 0.25f){
											bg->flags.hasAnchor = 0;

											mrmReleaseAllDataOwnershipBG(msg);

											mrSwingDestroyRopeBG(msg, bg);
										}
									}
								}
							}

							scaleVec3(bg->vel, MM_STEPS_PER_SECOND, bg->vel);
						}
					}
				}
				
				xcase MDC_BIT_ROTATION_CHANGE:{
					Vec3 posCur;
					Quat rotCur;
					Mat3 mat;
					Vec2 pyFaceCur;
					Vec2 pyFace;
					Vec3 dirFaceCur;
					Vec3 dirFaceTarget;
					Vec3 dirFace;
					
					mrmGetPositionBG(msg, posCur);

					subVec3(bg->anchor, posCur, mat[1]);
					normalVec3(mat[1]);

					crossVec3(mat[1], bg->vel, mat[0]);
					normalVec3(mat[0]);
					
					crossVec3(mat[0], mat[1], mat[2]);
					
					mat3ToQuat(mat, localBG->rotTarget);

					mrmGetRotationBG(msg, rotCur);
					
					quatInterp(	0.15f,
								rotCur,
								localBG->rotTarget,
								rotCur);

					mrmSetRotationBG(msg, rotCur);

					// Set the facing (rotate-interp towards rot's mat[2]).

					quatToMat3_2(localBG->rotTarget, dirFaceTarget);
					mrmGetFacePitchYawBG(msg, pyFaceCur);
					createMat3_2_YP(dirFaceCur, pyFaceCur);
					
					rotateUnitVecTowardsUnitVec(dirFaceCur, dirFaceTarget, 0.3f, dirFace);
					
					getVec3YP(dirFace, &pyFace[1], &pyFace[0]);
					mrmSetFacePitchYawBG(msg, pyFace);
				}
				
				xcase MDC_BIT_ANIMATION:{
					if(1){
						MMRFxConstant constant = {0};

						// if there is a name then use it instead of the hard coded webline
						if(sync->pcSwingingFx)
						{
							constant.fxName = sync->pcSwingingFx;
						}
						else
						{
							constant.fxName = "Fx_WebLine";
						}
						copyVec3(bg->anchor, constant.vecTarget);

						mmrFxCreateBG(	msg,
										&bg->mmrFxRope,
										&constant,
										NULL);
					}

					mrmAnimAddBitBG(msg, mmGetAnimBitHandleByName("GRAPPLEMODE", 0));
					mrmAnimAddBitBG(msg, mmGetAnimBitHandleByName("GRAPPLED", 0));
					
					if(bg->vel[1] > 0.f){
						mrmAnimAddBitBG(msg, mmAnimBitHandles.rising);
					}else{
						mrmAnimAddBitBG(msg, mmAnimBitHandles.falling);
					}
				}
			}
		}

		xcase MR_MSG_BG_CREATE_DETAILS:{
			S32 isSettled;
			
			if(	!mrmGetIsSettledBG(msg, &isSettled) ||
				isSettled)
			{
				mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_CREATE_DETAILS);
			}else{
				mrmAnimAddBitBG(msg, mmGetAnimBitHandleByName("GRAPPLEMODE", 0));
			}
		}
	}
}


// todo: make these into a macro, as it's ugly and a pain to add new ones

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

// 
// s_bUseNewSwingingLogic
AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE;
void mmsSwingUseNewLogic(S32 val)
{
	s_bUseNewSwingingLogic = val;
}

AUTO_COMMAND ACMD_CLIENTCMD;
void mmcSwingUseNewLogic(S32 val)
{
	s_bUseNewSwingingLogic = val;
#ifdef GAMECLIENT
	ServerCmd_mmsSwingUseNewLogic(val);
#endif
}


// 
// s_fSwingMaxSpeed
AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE;
void mmsSwingAccelMaxSpeedFactor(F32 val)
{
	s_fSwingAccelMaxSpeedFactor = val;
}

AUTO_COMMAND ACMD_CLIENTCMD;
void mmcSwingAccelMaxSpeedFactor(F32 val)
{
	s_fSwingAccelMaxSpeedFactor = val;
#ifdef GAMECLIENT
	ServerCmd_mmsSwingAccelMaxSpeedFactor(val);
#endif
}


//
// s_fSwingClearHeight
AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE;
void mmsSwingClearHeight(F32 val)
{
	s_fSwingClearHeight = val;
}

AUTO_COMMAND ACMD_CLIENTCMD;
void mmcSwingClearHeight(F32 val)
{
	s_fSwingClearHeight = val;
#ifdef GAMECLIENT
	ServerCmd_mmsSwingClearHeight(val);
#endif
}


//
// s_fSwingPitchMin
AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE;
void mmsSwingPitchMin(F32 val)
{
	s_fSwingPitchMin = val;
}

AUTO_COMMAND ACMD_CLIENTCMD;
void mmcSwingPitchMin(F32 val)
{
	val = RAD(val);
	s_fSwingPitchMin = val;
#ifdef GAMECLIENT
	ServerCmd_mmsSwingPitchMin(val);
#endif
}


//
// s_fSwingPitchMinRange
AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE;
void mmsSwingPitchMinRange(F32 val)
{
	s_fSwingPitchMinRange = val;
}

AUTO_COMMAND ACMD_CLIENTCMD;
void mmcSwingPitchMinRange(F32 val)
{
	val = RAD(val);
	s_fSwingPitchMinRange = val;
#ifdef GAMECLIENT
	ServerCmd_mmsSwingPitchMinRange(val);
#endif
}


//
// s_fSwingPitchRangeMin
AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE;
void mmsSwingPitchRangeMin(F32 val)
{
	s_fSwingPitchRangeMin = val;
}

AUTO_COMMAND ACMD_CLIENTCMD;
void mmcSwingPitchRangeMin(F32 val)
{
	val = RAD(val);
	s_fSwingPitchRangeMin = val;
#ifdef GAMECLIENT
	ServerCmd_mmsSwingPitchRangeMin(val);
#endif
}


//
// s_fSwingPitchRangeRange
AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE;
void mmsSwingPitchRangeRange(F32 val)
{
	s_fSwingPitchRangeRange = val;
}

AUTO_COMMAND ACMD_CLIENTCMD;
void mmcSwingPitchRangeRange(F32 val)
{
	val = RAD(val);
	s_fSwingPitchRangeRange = val;
#ifdef GAMECLIENT
	ServerCmd_mmsSwingPitchRangeRange(val);
#endif
}


// 
// s_uSwingAnchorCooldown
AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE;
void mmsSwingAnchorCooldown(U32 val)
{
	s_uSwingAnchorCooldown = val;
}

AUTO_COMMAND ACMD_CLIENTCMD;
void mmcSwingAnchorCooldown(F32 val)
{
	s_uSwingAnchorCooldown = (U32)(MM_STEPS_PER_SECOND * val);
#ifdef GAMECLIENT
	ServerCmd_mmsSwingAnchorCooldown(s_uSwingAnchorCooldown);
#endif
}


#include "AutoGen/EntityMovementSwing_c_ast.c"