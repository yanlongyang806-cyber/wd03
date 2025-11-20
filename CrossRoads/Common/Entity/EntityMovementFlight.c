/***************************************************************************
*     Copyright (c) 2005-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

#include "CombatConfig.h"
#include "EntityMovementFlight.h"
#include "EntityMovementManager.h"
#include "EntityMovementProjectile.h"
#include "Quat.h"
#include "textparser.h"
#include "AutoGen/EntityMovementFlight_c_ast.h"

#define MAX_FLIGHT_PITCH (HALFPI - 0.01)

static F32 			maxGravitySpeedFactor = 0.95f;
static F32 			gravityTractionFactor = 0.35f;
static F32 			gravityPitchOffsetMin = RAD(0.0f);
static F32 			gravityPitchOffsetRange = RAD(37.5f);
static F32 			onGroundFrictionFactor = 10.f;
static F32 			maxSpeedGroundFrictionFactor = 2.0f;
static F32 			groundTractionFactor = 2.0f;
static const F32	speedInputTurning = 2.75f;

AUTO_RUN_MM_REGISTER_PUBLIC_REQUESTER_MSG_HANDLER(	mrFlightMsgHandler,
													"FlightMovement",
													Flight);

typedef enum FlightStanceType {
	MR_FLIGHT_STANCE_MOVING,
	MR_FLIGHT_STANCE_FORWARD,
	MR_FLIGHT_STANCE_JUMPING,
	MR_FLIGHT_STANCE_REPEL,
	
	MR_FLIGHT_STANCE_COUNT,
} FlightStanceType;

AUTO_STRUCT;
typedef struct FlightFG {
	S32	unused;
} FlightFG;

AUTO_STRUCT;
typedef struct FlightBGFlags {
	U32						onGround			: 1;
	U32						hasAdditiveVel		: 1;
	U32						setRepelAnim		: 1;
	U32						setJumpAnim			: 1;
	U32						isTurning			: 1;
	U32						isJumping			: 1;
	U32						turnBecomesStrafe	: 1;
	U32						hasStanceMask		: MR_FLIGHT_STANCE_COUNT;
} FlightBGFlags;

AUTO_STRUCT;
typedef struct FlightBG {
	Vec3					vel;
	Vec3					velAdditive;
	Vec3					pyrTarget;
	U32						offsetResource;
	F32						gravitySpeed;
	F32						forwardPitchMag;
	F32						inputMoveYaw;
	F32						inputFaceYaw;
	FlightBGFlags			flags;
} FlightBG;

AUTO_STRUCT;
typedef struct FlightLocalBGFlags {
	U32						inputDirIsCurrent	: 1;
	U32						tryingToMove		: 1;
} FlightLocalBGFlags;

AUTO_STRUCT;
typedef struct FlightLocalBG {
	Vec3					dirInput;
	Vec3					targetVel;
	F32						curSpeed;
	U32						stanceHandle[MR_FLIGHT_STANCE_COUNT];	NO_AST
	FlightLocalBGFlags		flags;
} FlightLocalBG;

AUTO_STRUCT;
typedef struct FlightToFG {
	S32	unused;
} FlightToFG;

AUTO_STRUCT;
typedef struct FlightToBG {
	S32 unused;
} FlightToBG;

AUTO_STRUCT;
typedef struct FlightSyncFlags {
	U32						fakeroll			: 1;
	U32						isStrafing			: 1;
	U32						useThrottle			: 1;
	U32						useOffsetRotation	: 1;
	U32						isGravitySet		: 1;
	U32						isGlidingSet		: 1;
} FlightSyncFlags;

AUTO_STRUCT;
typedef struct FlightAvoid {
	Vec3					pos;
	F32						radiusMin;
	F32						radiusMax;
} FlightAvoid;

AUTO_STRUCT;
typedef struct FlightSync {
	F32						maxSpeed;
	F32						traction;
	F32						friction;
	F32						turnRate;
	F32						throttle;	// Only used for server-controlled movement
	F32						gravityUp;
	F32						gravityDown;
	F32						glideDown;
	U32						rotPDIgnorePitch : 1;
	U32						rotAllIgnorePitch : 1;
	U32						playJumpBit : 1;

	FlightAvoid**			avoids;
	
	FlightSyncFlags			flags;
} FlightSync;

AUTO_STRUCT;
typedef struct FlightSyncPublic {
	U32						enabled : 1;
} FlightSyncPublic;

static S32 mrFlightGetBitHandleFromStanceType(	FlightStanceType stanceType,
												U32* handleOut)
{
	switch (stanceType) {
#define CASE(x, y) xcase x: *handleOut = mmAnimBitHandles.y
		CASE(MR_FLIGHT_STANCE_FORWARD,	forward);
		CASE(MR_FLIGHT_STANCE_MOVING,	moving);
		CASE(MR_FLIGHT_STANCE_JUMPING,	jumping);
		CASE(MR_FLIGHT_STANCE_REPEL,	repel);
#undef CASE
xdefault: return 0;
	}
	return 1;
}

static bool mrFlightGetNameFromStanceType(	FlightStanceType stanceType,
											char** nameOut)
{
	switch (stanceType) {
#define CASE(x, y) xcase x: *nameOut = #y
		CASE(MR_FLIGHT_STANCE_FORWARD,	forward);
		CASE(MR_FLIGHT_STANCE_MOVING,	moving);
		CASE(MR_FLIGHT_STANCE_JUMPING,	jumping);
		CASE(MR_FLIGHT_STANCE_REPEL,	repel);
#undef CASE
xdefault: return false;
	}
	return true;
}

static void mrFlightStanceSetBG(	const MovementRequesterMsg	*msg,
									FlightBG					*bg,
									FlightLocalBG				*localBG,
									FlightStanceType			stanceType)
{
	if (FALSE_THEN_SET_BIT(bg->flags.hasStanceMask, BIT(stanceType))) {
		U32 bitHandle;

		if (!mrFlightGetBitHandleFromStanceType(stanceType, &bitHandle)) {
			return;
		}

		mrmAnimStanceCreateBG(	msg,
								&localBG->stanceHandle[stanceType],
								bitHandle);
	}
}

static void mrFlightStanceClearBG(	const MovementRequesterMsg	*msg,
									FlightBG					*bg,
									FlightLocalBG				*localBG,
									FlightStanceType			stanceType)
{
	if (TRUE_THEN_RESET_BIT(bg->flags.hasStanceMask, BIT(stanceType))) {
		U32 bitHandle;

		if (!mrFlightGetBitHandleFromStanceType(stanceType, &bitHandle)) {
			return;
		}

		mrmAnimStanceDestroyBG(	msg,
								&localBG->stanceHandle[stanceType]);
	}
}

void updateInputYawBG(	const MovementRequesterMsg* msg, 
						 S32 useMoveYaw,
						 F32* curYawInOut,
						 F32* lastInputYawInOut,
						 S32 turnInputSign);

void mrFlightGetInputDirBG(	const MovementRequesterMsg* msg,
							FlightBG* bg,
							FlightLocalBG* localBG,
							FlightSync* sync,
							Vec3 dirOut)
{
	if(FALSE_THEN_SET(localBG->flags.inputDirIsCurrent)){
		Vec3	dirRelative;
		Vec3	pyr;
		Mat3	mat;

		setVec3(dirRelative,
				mrmGetInputValueBitDiffBG(msg, MIVI_BIT_RIGHT, MIVI_BIT_LEFT),
				mrmGetInputValueBitDiffBG(msg, MIVI_BIT_UP, MIVI_BIT_DOWN) +
					mrmGetInputValueF32BG(msg, MIVI_F32_TILT),
				sync->flags.useThrottle || (sync->flags.isGlidingSet && bg->flags.isJumping == false) ?
					1 :
					mrmGetInputValueBitDiffBG(msg, MIVI_BIT_FORWARD, MIVI_BIT_BACKWARD));

		if(bg->flags.turnBecomesStrafe){
			dirRelative[0] += mrmGetInputValueBitDiffBG(msg, MIVI_BIT_TURN_RIGHT, MIVI_BIT_TURN_LEFT);
			dirRelative[0] = CLAMP(dirRelative[0], -1.f, 1.f);
		}

		pyr[0] = mrmGetInputValueF32BG(msg, MIVI_F32_PITCH);
		pyr[1] = bg->inputMoveYaw;
		pyr[2] = 0;

		createMat3YPR(mat, pyr);
		mulVecMat3(	dirRelative,
					mat,
					localBG->dirInput);

		normalVec3(localBG->dirInput);
	}

	copyVec3(	localBG->dirInput,
				dirOut);
}

static void physicsGetRepelVel(	const MovementRequesterMsg* msg,
								FlightBG* bg)
{
	Vec3	repelVel;
	S32		resetBGVel;
	S32		isRepel;

	if(mrmGetAdditionalVelBG(msg, repelVel, &isRepel, &resetBGVel)){
		addVec3(repelVel, bg->velAdditive, bg->velAdditive);

		if(resetBGVel){
			zeroVec3(bg->vel);
		}

		if (g_CombatConfig.fRepelSpeedAnimationThreshold > 0){
			if (!bg->flags.setRepelAnim)
			{
				if (lengthVec3Squared(bg->velAdditive) >= SQR(g_CombatConfig.fRepelSpeedAnimationThreshold)){
					bg->flags.setRepelAnim = true;
				} else {
					bg->flags.setRepelAnim = false;
				}
			}
		} else {
			bg->flags.setRepelAnim = true;
		}

		bg->flags.hasAdditiveVel = 1;
	}
}

__forceinline static void truncateVector(	Vec3 vInOut,
											F32 truncatedLen)
{
	F32 scale = lengthVec3Squared(vInOut);

	if(scale > SQR(truncatedLen)){
		scale = truncatedLen / sqrtf(scale);
		scaleVec3(vInOut, scale, vInOut);
	}
}

static void mrFlightApplyFriction(FlightBG *bg,
								  FlightLocalBG *localBG,
								  FlightSync *sync,
								  F32 frictionIn,
								  F32 maxSpeed)
{
	if(	!localBG->flags.tryingToMove ||
		localBG->curSpeed - 0.1f > maxSpeed)
	{
		F32 len = normalVec3(bg->vel);
		F32	friction =	MAX(localBG->curSpeed, MAX(maxSpeed,5)) *
						frictionIn *
						MM_SECONDS_PER_STEP;

		if(bg->flags.onGround){
			if(localBG->curSpeed > maxSpeed){
				friction *= maxSpeedGroundFrictionFactor;
			}

			if(!localBG->flags.tryingToMove){
				friction *= onGroundFrictionFactor;
			}
		}

		if(len > friction){
			len -= friction;
		}else{
			len = 0;
		}

		scaleVec3(	bg->vel,
					len,
					bg->vel);
	}
}

static void mrFlightApplyTraction(FlightBG *bg,
								  FlightLocalBG *localBG,
								  FlightSync *sync,
								  F32 tractionIn,
								  F32 maxSpeed)
{
	if(localBG->flags.tryingToMove){
		F32	traction =	MAX(localBG->curSpeed, MAX(maxSpeed, 5)) *
						tractionIn *
						MM_SECONDS_PER_STEP;

		if(!sync->flags.isGravitySet){
			Vec3	diffToTargetVel;
			F32		diffToTargetVelLen;

			subVec3(localBG->targetVel,
					bg->vel,
					diffToTargetVel);

			diffToTargetVelLen = normalVec3(diffToTargetVel);

			if(traction > 0 && diffToTargetVelLen > traction){
				scaleVec3(	diffToTargetVel,
							traction,
							diffToTargetVel);
			}else{
				scaleVec3(	diffToTargetVel,
							diffToTargetVelLen,
							diffToTargetVel);
			}

			addVec3(bg->vel,
					diffToTargetVel,
					bg->vel);
		}else{
			Vec3	accelDir;
			F32		speedScale;

			if(bg->flags.onGround){
				traction *= groundTractionFactor;
			}

			copyVec3(localBG->targetVel, accelDir);
			speedScale = normalVec3(accelDir);

			if(speedScale > traction){
				speedScale = traction;
			}

			scaleVec3(	accelDir,
						speedScale,
						accelDir);

			addVec3(bg->vel,
					accelDir,
					bg->vel);

			if(localBG->curSpeed > maxSpeed){
				truncateVector(bg->vel, localBG->curSpeed);
			}else{
				truncateVector(bg->vel, maxSpeed);
			}

			localBG->curSpeed = lengthVec3(bg->vel);
		}
	}
}

static void mrFlightApplyGravity(const MovementRequesterMsg* msg,
								 FlightBG *bg,
								 FlightLocalBG *localBG,
								 FlightSync *sync,
								 F32 tractionIn,
								 F32 maxSpeed)
{
	F32			traction =	MAX(localBG->curSpeed, MAX(maxSpeed,5)) *
							tractionIn *
							MM_SECONDS_PER_STEP;
	F32			maxGravitySpeed;
	F32			gravityTraction;
	const F32	facingYaw = bg->inputFaceYaw;
	Vec3		gravityDir;
	F32			gravity;

	// use forward gravity
	if(1)
	{
		if(	localBG->flags.tryingToMove && 
			(	ABS(localBG->targetVel[0]) + ABS(localBG->targetVel[2]) >
				ABS(localBG->targetVel[1]))
			)
		{
			bg->forwardPitchMag += MM_SECONDS_PER_STEP * 0.5f;

			if(bg->forwardPitchMag > 1.0f){
				bg->forwardPitchMag = 1.0f;
			}
		}else{
			if(bg->forwardPitchMag > 0.0f){
				bg->forwardPitchMag -= MM_SECONDS_PER_STEP;
				
				if(bg->forwardPitchMag < 0.0f){
					bg->forwardPitchMag = 0.0f;
				}
			}
		}

		if(bg->flags.onGround){
			setVec3(gravityDir, 0.0f, -1.0f, 0.0f);
		}else{
			const F32 gravityPitch = PI - (gravityPitchOffsetMin + (bg->forwardPitchMag * gravityPitchOffsetRange));

			sphericalCoordsToVec3(gravityDir, facingYaw, gravityPitch, 1.0f);							
		}
	}else{
		setVec3(gravityDir, 0.0f, -1.0f, 0.0f);
	}

	gravityTraction = traction * gravityTractionFactor;
	maxGravitySpeed = maxSpeed + maxSpeed * maxGravitySpeedFactor;

	if(bg->vel[1] < 2.0f){
		gravity = sync->gravityDown * maxSpeed;
	}else{
		gravity = sync->gravityUp * maxSpeed;
	}

	// if the user is controlling in gravity direction, add any projected velocity 
	{
		F32 dot = dotVec3(gravityDir, localBG->dirInput);

		if(dot > 0.0f){
			Vec3 vDir;
			
			scaleVec3(gravityDir, dot, vDir);
			scaleAddVec3(vDir, gravityTraction, bg->vel, bg->vel);
		}
	}
			
	scaleAddVec3(gravityDir, gravity * MM_SECONDS_PER_STEP, bg->vel, bg->vel);
	// make sure we do not go past the max speed for gravity
	truncateVector(bg->vel, maxGravitySpeed);

	if(bg->flags.onGround){
		if(mrmGetInputValueBitBG(msg, MIVI_BIT_UP)){
			if(bg->vel[1] < 0.0f){
				bg->vel[1] = 8.0f * gravity * MM_SECONDS_PER_STEP;
			}else{
				bg->vel[1] += 4.0f * gravity * MM_SECONDS_PER_STEP;
			}
		}
	}
}

static void mrFlightApplyGlide(const MovementRequesterMsg* msg,
									FlightBG *bg,
									FlightLocalBG *localBG,
									FlightSync *sync,
									F32 maxSpeed)
{
	if(!bg->flags.isJumping)
	{
		bg->vel[1] = min(bg->vel[1] - sync->glideDown  * MM_SECONDS_PER_STEP, 0 - (sync->glideDown  * MM_SECONDS_PER_STEP / 2));
	}

	
	truncateVector(bg->vel,maxSpeed);
}

static void mrFlightApplyPhysics(const MovementRequesterMsg *msg,
								 FlightBG* bg,
								 FlightLocalBG *localBG,
								 FlightSync* sync,
								 F32 friction,
								 F32 traction,
								 F32 maxSpeed)
{
	mrFlightApplyTraction(bg, localBG, sync, traction, maxSpeed);
	mrFlightApplyFriction(bg, localBG, sync, friction, maxSpeed);
	
	if(sync->flags.isGravitySet){
		mrFlightApplyGravity(msg, bg, localBG, sync, traction, maxSpeed);
	}

	if(sync->flags.isGlidingSet){
		mrFlightApplyGlide(msg, bg, localBG, sync, maxSpeed);
	}
}

static void rotateVecTowardsYOntoPlane(const Vec3 v,
									   const Vec3 n,
									   Vec3 vOut)
{
	F32		length = lengthVec3(v);
	F32		scale;
	Vec3	rotationVec;

	assert(v != vOut);

	crossVec3(	v,
		unitmat[1],
		rotationVec);

	crossVec3(	n,
		rotationVec,
		vOut);

	scale = lengthVec3(vOut);

	if(scale > 0.f){
		scale = length / scale;
		scaleVec3(vOut, scale, vOut);
	}
}

static void mrFlightInputDirIsDirtyBG(	const MovementRequesterMsg* msg,
										FlightLocalBG* localBG,
										const char* reason)
{
	if(TRUE_THEN_RESET(localBG->flags.inputDirIsCurrent)){
		mrmLog(msg, NULL, "Reset inputDirIsCurrent (%s).", reason);
	}
}

static void mrFlightHandleTurningInputBG(	const MovementRequesterMsg* msg,
											FlightBG* bg, 
											FlightLocalBG* localBG,
											const FlightSync* sync)
{
	const S32 turnInputSign = mrmGetInputValueBitDiffBG(msg, MIVI_BIT_TURN_RIGHT, MIVI_BIT_TURN_LEFT);

	mrmLog(	msg,
			NULL,
			"[surface] Turning Input Sign: %d\n"
			"Input MoveYaw: %1.3f [%8.8x]\n"
			"Input FaceYaw: %1.3f [%8.8x]\n",
			turnInputSign,
			bg->inputMoveYaw,
			*(S32*)&bg->inputMoveYaw,
			bg->inputFaceYaw,
			*(S32*)&bg->inputFaceYaw);
	
	if(turnInputSign){
		F32 yawDelta =	MM_SECONDS_PER_STEP *
						speedInputTurning *
						turnInputSign;

		bg->inputMoveYaw = addAngle(bg->inputMoveYaw, yawDelta);
		bg->inputFaceYaw = addAngle(bg->inputFaceYaw, yawDelta);

		mrFlightInputDirIsDirtyBG(msg, localBG, "turning");
	}
}

static void mrFlightHandleCreateOutputPositionChange(	const MovementRequesterMsg* msg,
														FlightBG* bg,
														FlightLocalBG* localBG,
														FlightSync* sync)
{
	const MovementRequesterMsgCreateOutputShared* shared = msg->in.bg.createOutput.shared;

	MovementSpeedType		speedType = shared->target.pos->speedType;
	MovementTurnRateType	turnRateType = shared->target.pos->turnRateType;
	MovementFrictionType	frictionType = shared->target.pos->frictionType;
	MovementTractionType	tractionType = shared->target.pos->tractionType;
	F32						maxSpeed;
	F32						turnRate;
	F32						traction;
	F32						friction;

	if(sync->flags.useOffsetRotation){
		if(!bg->offsetResource){
			MMROffsetConstant constant = {0};
			
			constant.rotationOffset = 3.f;
			
			mmrOffsetResourceCreateBG(	msg,
										&constant,
										&bg->offsetResource);
		}
	}
	else if(bg->offsetResource){
		mmrOffsetResourceDestroyBG(msg, &bg->offsetResource);
	}

	switch(speedType){
		xcase MST_OVERRIDE:{
			maxSpeed = shared->target.pos->speed;
		}

		xcase MST_CONSTANT:{
			maxSpeed = shared->target.pos->speed;
		}

		xdefault:{
			maxSpeed = sync->maxSpeed;
		}
	}

	switch(turnRateType){
		xcase MTRT_OVERRIDE:{
			turnRate = shared->target.pos->turnRate;
		}

		xdefault:{
			turnRate = sync->turnRate;
		}
	}

	switch(tractionType){
		xcase MTRT_OVERRIDE:{
			traction = shared->target.pos->traction;
		}

		xdefault:{
			if(speedType==MST_CONSTANT)
				traction = 0;
			else
				traction = sync->traction;
		}
	}

	switch(frictionType){
		xcase MTRT_OVERRIDE:{
			friction = shared->target.pos->friction;
		}

		xdefault:{
			if(speedType==MST_CONSTANT)
				friction = 0;
			else
				friction = sync->friction;
		}
	}
	
	switch(shared->target.pos->targetType){
		xcase MPTT_INPUT:{
			F32		dirScale;
			Vec3	dirInput;
			
			localBG->curSpeed = lengthVec3(bg->vel);

			dirScale = mrmGetInputValueF32BG(msg, MIVI_F32_DIRECTION_SCALE);

			if(sync->flags.useThrottle){
				dirScale *= sync->throttle;
			}
			else if(sync->flags.isGlidingSet)
			{
				if(mrmGetInputValueBitBG(msg, MIVI_BIT_BACKWARD))
					dirScale = 0.5f;
				else
					dirScale = 1.0f;
			}

			if(bg->flags.isTurning){
				mrFlightHandleTurningInputBG(msg, bg, localBG, sync);
			}

			if (mrmGetInputValueBitBG(msg, MIVI_BIT_UP)){
				bg->flags.isJumping = true;
			} else {
				bg->flags.isJumping = false;
			}

			mrFlightGetInputDirBG(	msg,
										bg,
										localBG,
										sync,
										dirInput);
		
			if(!vec3IsZero(dirInput) && dirScale != 0.0f){
					
				F32 speedScale;
				localBG->flags.tryingToMove = 1;

				if(turnRate && !sync->flags.isStrafing)
				{
					// Limited turning means you go in the direction you're facing
					createMat3_2_YP(dirInput, bg->pyrTarget);
				}

				speedScale = maxSpeed * dirScale;

				scaleVec3(	dirInput,
							speedScale,
							localBG->targetVel);
			}else {
				zeroVec3(localBG->targetVel);
			}
		}

		xcase MPTT_POINT:{
			// Crappy copy of above code
			F32		dirScale = 1.f;
			Vec3	targetPos, pos, dir;
			F32		distance = 0.f;

			localBG->curSpeed = lengthVec3(bg->vel);

			localBG->flags.tryingToMove = 1;

			copyVec3(	shared->target.pos->point,
						targetPos);

			mrmGetPositionBG(msg,pos);

			subVec3(targetPos,pos,dir);

			distance = normalVec3(dir);

			dirScale = sync->flags.useThrottle ? sync->throttle : 1.0f;

			if(!vec3IsZero(dir)){
				if(turnRate){
					// Face the direction we want to go

					Vec3 basicdir;
					copyVec3(dir,basicdir);
					createMat3_2_YP(dir, bg->pyrTarget);

					// Slow down for the approach

					if(!sync->flags.useThrottle && maxSpeed > 0){
						F32 dot;
						F32 rotateTime;
						F32 moveTime;
						
						dot = dotVec3(dir, basicdir);
						MINMAX1(dot, -1.f, 1.f);
						rotateTime = fabs(fixAngle(acos(dot)));
						moveTime = distance / maxSpeed;
						rotateTime = rotateTime / turnRate * 2;
						MAX1(moveTime, rotateTime);
						MIN1(maxSpeed, distance/moveTime);
						MAX1(maxSpeed, shared->target.minSpeed);
					}

					// Cheat to always make sure you can change dir vertically without actually pitching
					dir[1] = interpF32(0.75, basicdir[1], dir[1]);  
				}

				if(maxSpeed * dirScale * MM_SECONDS_PER_STEP > distance)
				{
					scaleVec3(	dir,
								distance / MM_SECONDS_PER_STEP,
								localBG->targetVel);
				}
				else 
				{
					scaleVec3(	dir,
								maxSpeed * dirScale,
								localBG->targetVel);
				}
			}
			else
				zeroVec3(localBG->targetVel);
		}

		xcase MPTT_VELOCITY:{
			localBG->flags.tryingToMove = 1;

			copyVec3(	shared->target.pos->vel,
						localBG->targetVel);
		}

		xcase MPTT_STOPPED:{
			if(friction<traction)
				localBG->flags.tryingToMove = 1;
			zeroVec3(localBG->targetVel);
		}

		xdefault:{
			zeroVec3(localBG->targetVel);
		}
	}

	mrFlightApplyPhysics(msg, bg, localBG, sync, friction, traction, maxSpeed);

	physicsGetRepelVel(msg,bg);

	if(bg->flags.hasAdditiveVel){

		bg->flags.hasAdditiveVel = !mrProjectileApplyFriction(bg->velAdditive, 1);
		if (!bg->flags.hasAdditiveVel){
			bg->flags.setRepelAnim = false;
		}
	}

	// Do translation.

	if(	!vec3IsZero(bg->vel) ||
		bg->flags.hasAdditiveVel)
	{
		Vec3 stepVel;
		Vec3 oldPos;

		copyVec3(	bg->vel,
					stepVel);

		if(bg->flags.hasAdditiveVel){
			addVec3(bg->velAdditive, 
					stepVel, 
					stepVel);
		}

		scaleVec3(	stepVel,
					MM_SECONDS_PER_STEP,
					stepVel);

		bg->flags.onGround = 0;

		mrmGetPositionBG(msg, oldPos);

		mrmTranslatePositionBG(	msg,
			stepVel,
			!shared->target.pos->flags.noWorldColl,
			gConf.bDontUseStickyFlightGroundCollision);

		if(sync->flags.isGravitySet){
			Vec3 newPos;

			mrmGetPositionBG(msg, newPos);

			if(	bg->vel[1] <= 0.0f &&
				newPos[1] >= oldPos[1])
			{
				bg->flags.onGround = 1;
				bg->vel[1] = 0;
			}
		}
	}else{
		mrmMoveIfCollidingWithOthersBG(msg);
	}

	if (gConf.bNewAnimationSystem)
	{
		if (TRUE_THEN_RESET(localBG->flags.tryingToMove)) {
			mrFlightStanceSetBG(msg, bg, localBG, MR_FLIGHT_STANCE_MOVING);
			mrFlightStanceSetBG(msg, bg, localBG, MR_FLIGHT_STANCE_FORWARD);
		} else {
			mrFlightStanceClearBG(msg, bg, localBG, MR_FLIGHT_STANCE_MOVING);
			mrFlightStanceClearBG(msg, bg, localBG, MR_FLIGHT_STANCE_FORWARD);
		}

		if (sync->playJumpBit && bg->flags.isJumping) {
			mrFlightStanceSetBG(msg, bg, localBG, MR_FLIGHT_STANCE_JUMPING);
		} else {
			mrFlightStanceClearBG(msg, bg, localBG, MR_FLIGHT_STANCE_JUMPING);
		}

		if(bg->flags.hasAdditiveVel && bg->flags.setRepelAnim) {
			mrFlightStanceSetBG(msg, bg, localBG, MR_FLIGHT_STANCE_REPEL);
		} else {
			mrFlightStanceClearBG(msg, bg, localBG, MR_FLIGHT_STANCE_REPEL);
		}
	}
}

static void mrFlightHandleTurnRate(	const MovementRequesterMsg* msg,
									FlightBG* bg,
									FlightLocalBG* localBG,
									FlightSync* sync,
									F32 turnRate,
									const Vec3 pyrCur,
									Vec3 pyrInOut)
{
	F32 yawd;
	F32 pitchd;
	F32 maxd = turnRate * MM_SECONDS_PER_STEP; // turn rate defined in radians/s

	// Grab the delta py between target pyr and current pyr.

	pitchd = subAngle(pyrInOut[0], pyrCur[0]);
	yawd = subAngle(pyrInOut[1], pyrCur[1]);

	// Calculate actual pitch and yaw.
	
	{

		if(fabs(pitchd) > maxd){
			pitchd = CLAMP(pitchd,-maxd,maxd);
		}
		
		pyrInOut[0] = pyrCur[0] + pitchd;

		if(fabs(yawd) > maxd){
			yawd = CLAMP(yawd,-maxd,maxd);
		}
		
		pyrInOut[1] = pyrCur[1] + yawd;

		if(sync->flags.fakeroll){
			#define MR_MAX_ROLL (QUARTERPI)
			F32 forwardSpeed, targetRoll, rollDiff, maxDeltaRoll;
									
			// Calculate the max roll angle based on the speed of movement.
			forwardSpeed = SQR(bg->vel[0]) + SQR(bg->vel[1]) + SQR(bg->vel[2]);
			targetRoll = CLAMP(yawd/0.003, -1.0, 1.0f) * (forwardSpeed/SQR(14.0f)) * MR_MAX_ROLL;					
			targetRoll = CLAMP(targetRoll, -MR_MAX_ROLL, MR_MAX_ROLL);

			rollDiff = subAngle(targetRoll,pyrCur[2]);
			
			// Move a percentage of the way there and clamp the speed
			rollDiff *= 0.1f;
			maxDeltaRoll = maxd*2;
			if(fabs(rollDiff) > maxDeltaRoll){
				rollDiff = CLAMP(rollDiff,-maxDeltaRoll,maxDeltaRoll);
			}

			pyrInOut[2] = pyrCur[2] + rollDiff;
		}
	}

	pyrInOut[0] = CLAMP(pyrInOut[0], -QUARTERPI, QUARTERPI);

	// Stick the part-maybe-clamped pyr into our target quat.

	copyVec3(pyrInOut, bg->pyrTarget);
}

static void mrFlightUpdateIsTurningBG(	const MovementRequesterMsg* msg,
										FlightBG* bg,
										FlightLocalBG* localBG)
{
	U32 isTurning = mrmGetInputValueBitBG(msg, MIVI_BIT_TURN_LEFT) ||
					mrmGetInputValueBitBG(msg, MIVI_BIT_TURN_RIGHT);
	
	if(isTurning != bg->flags.isTurning){
		bg->flags.isTurning = isTurning;
		
		mrFlightInputDirIsDirtyBG(	msg,
									localBG,
									isTurning ?
										"turning started" :
										"turning stopped");
	}
}

static void mrFlightHandleCreateOutputRotationChange(	const MovementRequesterMsg* msg,
														FlightBG* bg,
														FlightLocalBG* localBG,
														FlightSync* sync)
{
	const MovementRequesterMsgCreateOutputShared* shared = msg->in.bg.createOutput.shared;

	MovementTurnRateType turnRateType = shared->target.rot->turnRateType;
	F32		turnRate;

	Vec3	pyrCur;
	Quat	rotCur;

	if(shared->target.rot->targetType == MRTT_INPUT){
		if(TRUE_THEN_RESET(bg->flags.turnBecomesStrafe)){
			mrFlightUpdateIsTurningBG(msg, bg, localBG);
		}
	}
	else if(FALSE_THEN_SET(bg->flags.turnBecomesStrafe)){
		bg->flags.isTurning = 0;
		
		mrFlightInputDirIsDirtyBG(msg, localBG, "enabling turnBecomesStafe");
	}

	switch(turnRateType){
		xcase MTRT_OVERRIDE:{
			turnRate = shared->target.rot->turnRate;
		}

		xdefault:{
			turnRate = sync->turnRate;
		}
	}

	mrmGetRotationBG(msg, rotCur);

	{
		Mat3	mat;
		Vec2	pyFaceCur;
		Vec3	dirFaceCur;
		
		mrmGetFacePitchYawBG(msg, pyFaceCur);

		quatToMat3_1(rotCur, mat[1]);
		createMat3_2_YP(dirFaceCur, pyFaceCur);
		crossVec3(mat[1], dirFaceCur, mat[0]);
		
		if(normalVec3(mat[0]) <= 0.f){
			if(mat[1][1] < 0.99f){
				crossVec3(unitmat[1], mat[1], mat[0]);
				normalVec3(mat[0]);
				
				crossVec3(mat[0], mat[1], mat[2]);
			}else{
				crossVec3(unitmat[0], mat[1], mat[2]);
				normalVec3(mat[2]);

				crossVec3(mat[1], mat[2], mat[0]);
			}
		}else{
			crossVec3(mat[0], mat[1], mat[2]);
		}
		
		getMat3YPR(mat, pyrCur);
	}

	switch(shared->target.rot->targetType){
		xcase MRTT_INPUT:{
			Vec3	dirInput;

			mrFlightGetInputDirBG(msg, bg, localBG, sync, dirInput);

			if(!vec3IsZero(dirInput)){
				Vec3 pyr;

				// Trying to move.

				if(sync->flags.isStrafing){
					const F32 yawFace = bg->inputFaceYaw;
					sincosf(yawFace, &dirInput[0], &dirInput[2]);
				}

				getVec3YP(dirInput, pyr + 1, pyr + 0);

				pyr[2] = 0;

				// Clamp pitch
				

				if(g_CombatConfig.fFlightPitchClamp){
					F32 p = RAD(g_CombatConfig.fFlightPitchClamp);
					pyr[0] = CLAMP(pyr[0],-p,p);
				}else{
					pyr[0] = CLAMP(pyr[0],-MAX_FLIGHT_PITCH,MAX_FLIGHT_PITCH);
				}

				if(sync->rotAllIgnorePitch){
					pyr[0] = 0.0f;
				}
				copyVec3(pyr, bg->pyrTarget);
			}else{
				// Not trying to move.

				bg->pyrTarget[0] = 0;
				bg->pyrTarget[2] = 0;

				if(sync->flags.isStrafing){
					bg->pyrTarget[1] = bg->inputFaceYaw;
				}
			}
		}

		xcase MRTT_DIRECTION:{
			Vec3 dir;

			copyVec3(	shared->target.rot->dir,
						dir);

			if(!vec3IsZeroXZ(dir)){
				setVec3(bg->pyrTarget,
						0,
						getVec3Yaw(dir),
						0);
				if(!sync->rotPDIgnorePitch && !sync->rotAllIgnorePitch){
					bg->pyrTarget[0] = getVec3Pitch(dir);
				}
			}
		}

		xcase MRTT_ROTATION:{
			quatToPYR(	shared->target.rot->rot,
						bg->pyrTarget);
			if(sync->rotAllIgnorePitch){
				bg->pyrTarget[0] = 0.0f;
			}
		}

		xcase MRTT_POINT:{
			Vec3 curPos;
			Vec3 dir;

			mrmGetPositionBG(msg, curPos);

			subVec3(shared->target.rot->point,
					curPos,
					dir);

			if(!vec3IsZeroXZ(dir)){
				setVec3(bg->pyrTarget,
						0,
						getVec3Yaw(dir),
						0);
				if(!sync->rotPDIgnorePitch && !sync->rotAllIgnorePitch){
					bg->pyrTarget[0] = getVec3Pitch(dir);
				}
			}
		}

		xcase MRTT_ENTITY:{
			bg->pyrTarget[0] = 0;
			bg->pyrTarget[2] = 0;
		}

		xcase MRTT_STOPPED:{
			bg->pyrTarget[0] = 0;
			bg->pyrTarget[2] = 0;
		}
	}

	if(	g_CombatConfig.bFlightDisableAutoLevel &&
		shared->target.rot->targetType != MRTT_ROTATION)
	{
		if(bg->pyrTarget[0] == 0.0f){
			bg->pyrTarget[0] = pyrCur[0];
		}
	}

	{
		Vec3 pyr;

		interpPYR(0.15, pyrCur, bg->pyrTarget, pyr);

		// Limit turn rate if post interp goes over it

		if(turnRate){
			mrFlightHandleTurnRate(msg, bg, localBG, sync, turnRate, pyrCur, pyr);
		}
		
		{
			Quat rot;

			if(!localBG->flags.tryingToMove){
				PYRToQuat(pyr, rot);
			}else{
				Mat3 mat;

				createMat3_1_YPR(mat[1], pyr);
				
				crossVec3(mat[1], localBG->targetVel, mat[0]);

				if(normalVec3(mat[0]) <= 0.f){
					if(mat[1][1] < 0.99f){
						crossVec3(unitmat[1], mat[1], mat[0]);
						normalVec3(mat[0]);
						
						crossVec3(mat[0], mat[1], mat[2]);
					}else{
						crossVec3(unitmat[0], mat[1], mat[2]);
						normalVec3(mat[2]);

						crossVec3(mat[1], mat[2], mat[0]);
					}
				}else{
					crossVec3(mat[0], mat[1], mat[2]);
				}

				mat3ToQuat(mat, rot);
			}
			
			mrmSetRotationBG(msg, rot);
		}

		mrmSetFacePitchYawBG(msg, pyr);
	}
}

static void mrFlightHandleCreateOutputAnimation(const MovementRequesterMsg* msg,
												FlightBG* bg,
												FlightLocalBG* localBG,
												FlightSync* sync)
{
	if(bg->flags.hasAdditiveVel && bg->flags.setRepelAnim){
		mrmAnimAddBitBG(msg, mmAnimBitHandles.repel);
	}

	if(TRUE_THEN_RESET(localBG->flags.tryingToMove)){
		mrmAnimAddBitBG(msg, mmAnimBitHandles.move);
		mrmAnimAddBitBG(msg, mmAnimBitHandles.forward);
	}

	if (sync->playJumpBit && bg->flags.isJumping){
		mrmAnimAddBitBG(msg, mmAnimBitHandles.jump);
	}
}

static void mrFlightHandleInputEventBG(	const MovementRequesterMsg* msg,
										FlightBG* bg,
										FlightLocalBG* localBG)
{
	if(TRUE_THEN_RESET(localBG->flags.inputDirIsCurrent)){
		mrmLog(msg, NULL, "Reset inputDirIsCurrent.");
	}

	switch(msg->in.bg.inputEvent.value.mivi){
		xcase MIVI_F32_MOVE_YAW:{
			if(!bg->flags.isTurning){
				bg->inputMoveYaw = msg->in.bg.inputEvent.value.f32;
			}
		}
		
		xcase MIVI_F32_FACE_YAW:{
			if(!bg->flags.isTurning){
				bg->inputFaceYaw = msg->in.bg.inputEvent.value.f32;
			}
		}
		
		xcase MIVI_BIT_TURN_LEFT:
		acase MIVI_BIT_TURN_RIGHT:{
			if(!bg->flags.turnBecomesStrafe){
				mrFlightUpdateIsTurningBG(msg, bg, localBG);
			}
		}
	}
}

void mrFlightMsgHandler(const MovementRequesterMsg* msg){
	FlightFG*			fg;
	FlightBG*			bg;
	FlightLocalBG*		localBG;
	FlightToFG*			toFG;
	FlightToBG*			toBG;
	FlightSync*			sync;
	FlightSyncPublic*	syncPublic;
	
	MR_MSG_HANDLER_GET_DATA_DEFAULT_PUBLIC(msg, Flight);

	switch(msg->in.msgType){
		xcase MR_MSG_GET_SYNC_DEBUG_STRING:{
			char*	buffer = msg->in.getSyncDebugString.buffer;
			U32		bufferLen = msg->in.getSyncDebugString.bufferLen;
			
			snprintf_s(	buffer,
						bufferLen,
						"maxSpeed(%1.2f) [%8.8x]\n"
						"traction(%1.2f) [%8.8x]\n"
						"friction(%1.2f) [%8.8x]\n"
						"turnRate(%1.2f) [%8.8x]\n"
						"throttle(%1.2f) [%8.8x]\n"
						"flags: %s%s"
						,
						sync->maxSpeed,
						*(S32*)&sync->maxSpeed,
						sync->traction,
						*(S32*)&sync->traction,
						sync->friction,
						*(S32*)&sync->friction,
						sync->turnRate,
						*(S32*)&sync->turnRate,
						sync->throttle,
						*(S32*)&sync->throttle,
						syncPublic->enabled ? "enabled, " : "",
						sync->flags.fakeroll ? "fakeroll, " : ""
						);
		}

		xcase MR_MSG_BG_GET_DEBUG_STRING:{
			char*	buffer = msg->in.bg.getDebugString.buffer;
			U32		bufferLen = msg->in.bg.getDebugString.bufferLen;

			snprintf_s(	buffer,
						bufferLen,
						"v(%1.2f, %1.2f, %1.2f) [%8.8x, %8.8x, %8.8x]"
						,
						vecParamsXYZ(bg->vel),
						vecParamsXYZ((S32*)bg->vel)
						);
		}
		
		xcase MR_MSG_BG_UPDATED_SYNC:{
			const U32 handledMsgs =	MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP |
									MR_HANDLED_MSG_INPUT_EVENT;
			
			if(syncPublic->enabled){
				localBG->flags.inputDirIsCurrent = 0;

				mrmHandledMsgsAddBG(msg, handledMsgs);
			}else{
				mrmHandledMsgsRemoveBG(msg, handledMsgs);
				mrmReleaseAllDataOwnershipBG(msg);
			}

			if(sync->flags.isStrafing){
				bg->flags.isTurning = 0;
			}else{
				mrFlightUpdateIsTurningBG(msg, bg, localBG);
			}
		}

		xcase MR_MSG_BG_FORCE_CHANGED_POS:{
			bg->flags.onGround = 0;
			zeroVec3(bg->vel);
		}

		xcase MR_MSG_BG_FORCE_CHANGED_ROT:{
			Quat rot;

			mrmGetRotationBG(msg, rot);
			quatToPYR(rot, bg->pyrTarget);
		}
		
		xcase MR_MSG_BG_PREDICT_DISABLED:{
			ZeroStruct(localBG);
			ZeroStruct(bg);
		}

		xcase MR_MSG_BG_PREDICT_ENABLED:{
			Vec2 pyFace;
			mrmGetFacePitchYawBG(msg, pyFace);
			bg->pyrTarget[0] = pyFace[0];
			bg->pyrTarget[1] = pyFace[1];
			bg->pyrTarget[2] = 0.f;
		}

		xcase MR_MSG_BG_INITIALIZE:{
			Quat rot;

			mrmGetRotationBG(msg, rot);
			quatToPYR(rot, bg->pyrTarget);
		}

		xcase MR_MSG_BG_WCO_ACTOR_CREATED:{
			mrmMoveToValidPointBG(msg);
		}

		xcase MR_MSG_BG_DATA_WAS_RELEASED:{
			U32 dataClassBits = msg->in.bg.dataWasReleased.dataClassBits;
			
			// We're no longer flying
			mrmSetIsFlyingBG(msg, 0);
			
			if(syncPublic->enabled){
				mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
			}
			
			if(dataClassBits & MDC_BIT_POSITION_CHANGE){
				if (gConf.bNewAnimationSystem) {
					mrFlightStanceClearBG(msg, bg, localBG, MR_FLIGHT_STANCE_MOVING);
					mrFlightStanceClearBG(msg, bg, localBG, MR_FLIGHT_STANCE_FORWARD);
					mrFlightStanceClearBG(msg, bg, localBG, MR_FLIGHT_STANCE_JUMPING);
					mrFlightStanceClearBG(msg, bg, localBG, MR_FLIGHT_STANCE_REPEL);
				}

				mrmShareOldVec3BG(msg, "Velocity", bg->vel);
				mrmShareOldS32BG(msg, "OffGroundIntentionally", 1);
				
				mmrOffsetResourceDestroyBG(msg, &bg->offsetResource);
			}
			
			if(dataClassBits & MDC_BIT_ROTATION_CHANGE){
				Quat rot;

				PYRToQuat(bg->pyrTarget, rot);
				mrmShareOldQuatBG(msg, "TargetRotation", rot);
				mrmShareOldF32BG(msg, "InputMoveYaw", bg->inputMoveYaw);
				mrmShareOldF32BG(msg, "InputFaceYaw", bg->inputFaceYaw);
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
				
				xcase MSDT_QUAT:{
					if(!stricmp(sd->name, "TargetRotation")){
						quatToPYR(	sd->data.quat,
									bg->pyrTarget);
					}
				}

				xcase MSDT_F32:{
					if(!stricmp(sd->name, "InputFaceYaw")){
						bg->inputFaceYaw = sd->data.f32;
					}
					else if(!stricmp(sd->name, "InputMoveYaw")){
						bg->inputMoveYaw = sd->data.f32;
					}
				}
			}
		}
		
		xcase MR_MSG_BG_QUERY_IS_SETTLED:{
			msg->out->bg.queryIsSettled.isSettled = ! bg->flags.hasAdditiveVel;
		}
		
		xcase MR_MSG_BG_QUERY_ON_GROUND:{
			msg->out->bg.queryOnGround.onGround = 0;
		}

		xcase MR_MSG_BG_QUERY_VELOCITY:{
			copyVec3(bg->vel, msg->out->bg.queryVelocity.vel);
		}

		xcase MR_MSG_BG_QUERY_TURN_RATE:{
			msg->out->bg.queryTurnRate.turnRate = sync->turnRate;
		}

		xcase MR_MSG_BG_QUERY_MAX_SPEED:{
			msg->out->bg.queryMaxSpeed.maxSpeed = sync->maxSpeed;
		}
		
		xcase MR_MSG_BG_INPUT_EVENT:{
			mrFlightHandleInputEventBG(msg, bg, localBG);
		}
		
		xcase MR_MSG_BG_DISCUSS_DATA_OWNERSHIP:{
			if(syncPublic->enabled){
				U32 acquireBits;
				U32 ownedBits;

				mrmSetIsFlyingBG(msg, 1);
				
				acquireBits = gConf.bNewAnimationSystem ?
								MDC_BIT_POSITION_CHANGE | MDC_BIT_ROTATION_CHANGE :
								MDC_BITS_CHANGE_ALL; // includes animation

				mrmAcquireDataOwnershipBG(msg, acquireBits, 0, NULL, &ownedBits);
				
				if(ownedBits == acquireBits){
					mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
				}
			}else{
				mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
				mrmReleaseDataOwnershipBG(	msg,
											MDC_BITS_ALL);
			}
		}
		
		xcase MR_MSG_BG_CREATE_OUTPUT:{
			switch(msg->in.bg.createOutput.dataClassBit){
				xcase MDC_BIT_POSITION_CHANGE:{
					mrFlightHandleCreateOutputPositionChange(msg, bg, localBG, sync);
				}
				
				xcase MDC_BIT_ROTATION_CHANGE:{
					mrFlightHandleCreateOutputRotationChange(msg, bg, localBG, sync);
				}
				
				xcase MDC_BIT_ANIMATION:{
					mrFlightHandleCreateOutputAnimation(msg, bg, localBG, sync);
				}
			}
		}

		xcase MR_MSG_BG_CONTROLLER_MSG:{
			if(!msg->in.bg.controllerMsg.isGround){	
				// For now just project the velocity onto the plane defined by the normal.
				
				projectVecOntoPlane(bg->vel, msg->in.bg.controllerMsg.normal, bg->vel);
				break;
			}

			if(msg->in.bg.controllerMsg.normal[1] > 0.5f){
				if(sync->flags.isGravitySet){
					bg->flags.onGround = 1;
					bg->gravitySpeed = 0.f;
				}
			}

			if(bg->flags.hasAdditiveVel){
				projectVecOntoPlane(bg->velAdditive,
									msg->in.bg.controllerMsg.normal,
									bg->velAdditive);
			}

			if(bg->vel[1] < 0.f){
				projectVecOntoPlane(bg->vel, msg->in.bg.controllerMsg.normal, bg->vel);
			}
		}
	}
}

S32 mrFlightCreate(	MovementManager* mm,
					MovementRequester** mrOut)
{
	return mmRequesterCreateBasic(mm, mrOut, mrFlightMsgHandler);
}

#define GET_SYNC(sync, syncPublic)	MR_GET_SYNC_PUBLIC(mr, mrFlightMsgHandler, Flight, sync, syncPublic)
#define IF_DIFF_THEN_SET(a, b)		MR_SYNC_SET_IF_DIFF(mr, a, b)
#define IF_DIFF_THEN_SET_VEC3(a, b)	if(!sameVec3((a), (b))){copyVec3((b), (a));mrEnableMsgUpdatedSync(mr);}((void)0)

bool mrFlightSetPointAndDirectionRotationsIgnorePitch(MovementRequester* mr,
													 bool ignore)
{
	FlightSync* sync;

	if(GET_SYNC(&sync, NULL)){
		IF_DIFF_THEN_SET(sync->rotPDIgnorePitch, (U32)ignore);

		return true;
	}

	return false;
}

bool mrFlightSetAllRotationTypesIgnorePitch(MovementRequester* mr,
										   bool ignore)
{
	FlightSync* sync;

	if(GET_SYNC(&sync, NULL)){
		IF_DIFF_THEN_SET(sync->rotAllIgnorePitch, (U32)ignore);

		return true;
	}

	return false;
}

bool mrFlightSetUseJumpBit(MovementRequester* mr,
						  bool useJumpBit)
{
	FlightSync* sync;

	if(GET_SYNC(&sync, NULL)){
		IF_DIFF_THEN_SET(sync->playJumpBit, (U32)useJumpBit);
		
		return true;
	}
	return false;
}

bool mrFlightSetMaxSpeed(MovementRequester* mr,
						F32 maxSpeed)
{
	FlightSync* sync;

	if(GET_SYNC(&sync, NULL)){
		IF_DIFF_THEN_SET(sync->maxSpeed, maxSpeed);

		return true;
	}

	return false;
}

bool mrFlightSetGravity(MovementRequester* mr, 
					   F32 gravityUp,
					   F32 gravityDown)
{
	FlightSync* sync;

	if(GET_SYNC(&sync, NULL)){
		S32 isGravitySet = gravityUp || gravityDown;
		
		IF_DIFF_THEN_SET(sync->gravityUp, gravityUp);
		IF_DIFF_THEN_SET(sync->gravityDown, gravityDown);
		IF_DIFF_THEN_SET(sync->flags.isGravitySet, (U32)isGravitySet);

		return true;
	}

	return false;
}

bool mrFlightSetGlideDecent(MovementRequester* mr,
							F32 fDecent)
{
	FlightSync* sync;

	if(GET_SYNC(&sync, NULL)){
		IF_DIFF_THEN_SET(sync->glideDown, fDecent);

		return true;
	}

	return false;
}


bool mrFlightSetTraction(MovementRequester* mr,
						F32 traction)
{
	FlightSync* sync;

	if(GET_SYNC(&sync, NULL)){
		IF_DIFF_THEN_SET(sync->traction, traction);

		return true;
	}

	return false;
}

bool mrFlightSetFriction(MovementRequester* mr,
						F32 friction)
{
	FlightSync* sync;

	if(GET_SYNC(&sync, NULL)){
		IF_DIFF_THEN_SET(sync->friction, friction);

		return true;
	}

	return false;
}

bool mrFlightSetTurnRate(MovementRequester* mr,
						F32 turnRate)
{
	FlightSync* sync;

	if(GET_SYNC(&sync, NULL)){
		IF_DIFF_THEN_SET(sync->turnRate, turnRate);

		return true;
	}

	return false;
}

bool mrFlightGetTurnRate(MovementRequester* mr,
						F32* turnRateOut)
{
	FlightSync* sync;

	if(GET_SYNC(&sync, NULL)){
		*turnRateOut = sync->turnRate;

		return true;
	}

	return false;
}

bool mrFlightSetThrottle(MovementRequester* mr,
						F32 throttle)
{
	FlightSync* sync;

	if(GET_SYNC(&sync, NULL)){
		throttle = CLAMP(throttle, -0.25f, 1.0f);
		IF_DIFF_THEN_SET(sync->throttle, throttle);

		return true;
	}

	return false;
}

bool mrFlightGetThrottle(MovementRequester* mr,
						F32* throttleOut)
{
	FlightSync *sync;

	if(GET_SYNC(&sync, NULL)){
		*throttleOut = sync->throttle;

		return true;
	}

	return false;
}

bool mrFlightSetUseThrottle(MovementRequester* mr,
							bool useThrottle)
{
	FlightSync* sync;

	if(GET_SYNC(&sync, NULL)){
		IF_DIFF_THEN_SET(sync->flags.useThrottle, (U32)!!useThrottle);
		return true;
	}

	return false;
}

bool mrFlightSetUseOffsetRotation(	MovementRequester* mr,
									bool useOffsetRotation)
{
	FlightSync* sync;

	if(GET_SYNC(&sync, NULL)){
		IF_DIFF_THEN_SET(sync->flags.useOffsetRotation, (U32)!!useOffsetRotation);
		return true;
	}

	return false;
}

bool mrFlightSetIsStrafing(MovementRequester* mr,
						  bool isStrafing)
{
	FlightSync* sync;

	if(GET_SYNC(&sync, NULL)){
		IF_DIFF_THEN_SET(sync->flags.isStrafing, (U32)!!isStrafing);
		return true;
	}

	return false;
}

bool mrFlightGetEnabled( MovementRequester* mr)
{
	FlightSyncPublic* syncPublic;

	if(GET_SYNC(NULL, &syncPublic)){
		return syncPublic->enabled;
	}
	return false;
}

bool mrFlightSetEnabled(	MovementRequester* mr,
						bool enabled)
{
	FlightSync*			sync;
	FlightSyncPublic*	syncPublic;

	if(GET_SYNC(&sync, &syncPublic)){
		enabled = !!enabled;

		if(syncPublic->enabled != (U32)enabled){
			IF_DIFF_THEN_SET(sync->gravityUp, 0.0f);
			IF_DIFF_THEN_SET(sync->gravityDown, 0.0f);
			IF_DIFF_THEN_SET(sync->flags.isGravitySet, 0);
		}

		IF_DIFF_THEN_SET(syncPublic->enabled, (U32)enabled);
		
		return true;
	}

	return false;
}

bool mrFlightSetFakeRoll(MovementRequester* mr,
						bool enabled)
{
	FlightSync* sync;

	if(GET_SYNC(&sync, NULL)){
		IF_DIFF_THEN_SET(sync->flags.fakeroll, (U32)!!enabled);

		return true;
	}

	return false;
}

bool mrFlightSetGlide(MovementRequester* mr,
						bool enabled)
{
	FlightSync* sync;

	if(GET_SYNC(&sync, NULL)){
		IF_DIFF_THEN_SET(sync->flags.isGlidingSet, (U32)!!enabled);

		return true;
	}

	return false;
}

bool mrFlightAddAvoid(	MovementRequester* mr,
						Vec3 pos,
						F32 radiusMin,
						F32 radiusMax)
{
	FlightSync* sync;

	if(GET_SYNC(&sync, NULL)){
		FlightAvoid* avoid = StructCreate(parse_FlightAvoid);
		
		copyVec3(pos, avoid->pos);
		avoid->radiusMin = radiusMin;
		avoid->radiusMax = radiusMax;
	
		eaPush(&sync->avoids, avoid);
		
		mrEnableMsgUpdatedSync(mr);
		
		return true;
	}
	
	return false;
}

AUTO_RUN_MM_REGISTER_RESOURCE_MSG_HANDLER(	mmrOffsetResourceMsgHandler,
											"Offset",
											MMROffset,
											MDC_BIT_POSITION_CHANGE);

AUTO_STRUCT;
typedef struct MMROffsetConstantNP {
	S32 unused;
} MMROffsetConstantNP;

AUTO_STRUCT;
typedef struct MMROffsetActivatedFG {
	S32							unused;
	MovementOffsetInstance*		offsetHandle; NO_AST
} MMROffsetActivatedFG;

AUTO_STRUCT;
typedef struct MMROffsetActivatedBG {
	S32 unused;
} MMROffsetActivatedBG;

AUTO_STRUCT;
typedef struct MMROffsetState {
	S32 unused;
} MMROffsetState;

void mmrOffsetResourceMsgHandler(const MovementManagedResourceMsg* msg){
	const MMROffsetConstant* constant = msg->in.constant;
	
	switch(msg->in.msgType){
		xcase MMR_MSG_GET_CONSTANT_DEBUG_STRING:{
			char** estrBuffer = msg->in.getDebugString.estrBuffer;

			estrConcatf(estrBuffer,
						"Offset: %1.3f [%8.8x]",
						constant->rotationOffset,
						*(U32*)&constant->rotationOffset);
		}
		
		xcase MMR_MSG_FG_SET_STATE:{
			MMROffsetActivatedFG*	activated = msg->in.activatedStruct;
			
			assert(msg->in.handle);

			if(!activated->offsetHandle){
				mmCreateOffsetInstanceFG(	msg->in.mm,
											constant->rotationOffset,
											&activated->offsetHandle);
			}
		}

		xcase MMR_MSG_FG_DESTROYED:{
			MMROffsetActivatedFG* activated = msg->in.activatedStruct;
			
			mmDestroyOffsetInstanceFG(	msg->in.mm,
										&activated->offsetHandle);
		}
	}
}

static U32 mmrOffsetGetResourceID(void){
	static U32 id;
	
	if(!id){
		if(!mmGetManagedResourceIDByMsgHandler(mmrOffsetResourceMsgHandler, &id)){
			assert(0);
		}
	}
	
	return id;
}

S32 mmrOffsetResourceCreateBG(	const MovementRequesterMsg* msg,
								const MMROffsetConstant* constant,
								U32* handleOut)
{
	return mrmResourceCreateBG(	msg,
								handleOut,
								mmrOffsetGetResourceID(),
								constant,
								NULL,
								NULL);
}

S32 mmrOffsetResourceDestroyBG(	const MovementRequesterMsg* msg,
								U32* handleInOut)
{
	return mrmResourceDestroyBG(msg,
								mmrOffsetGetResourceID(),
								handleInOut);
}


// todo: make these into a macro, as it's ugly and a pain to add new ones
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

// 
// maxGravitySpeedFactor
AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE;
void mmsFlightGravityMaxSpeedFactor(F32 val){
	maxGravitySpeedFactor = val;
}

AUTO_COMMAND ACMD_CLIENTONLY;
void mmcFlightGravityMaxSpeedFactor(F32 val){
	maxGravitySpeedFactor = val;

	#ifdef GAMECLIENT
		ServerCmd_mmsFlightGravityMaxSpeedFactor(val);
	#endif
}

// 
// gravityTractionFactor
AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE;
void mmsFlightGravityTractionFactor(F32 val){
	gravityTractionFactor = val;
}

AUTO_COMMAND ACMD_CLIENTONLY;
void mmcFlightGravityTractionFactor(F32 val){
	gravityTractionFactor = val;

	#ifdef GAMECLIENT
		ServerCmd_mmsFlightGravityTractionFactor(val);
	#endif
}

// 
// gravityPitchOffsetMin / gravityPitchOffsetRange
AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE;
void mmsFlightGravityPitchOffsetMinMax(	F32 val1,
										F32 val2)
{
	gravityPitchOffsetMin = RAD(val1);
	gravityPitchOffsetRange = RAD(val2);
}

AUTO_COMMAND ACMD_CLIENTONLY;
void mmcFlightGravityPitchOffset(	F32 val1,
									F32 val2)
{
	gravityPitchOffsetMin = RAD(val1);
	gravityPitchOffsetRange = RAD(val2);

	#ifdef GAMECLIENT
		ServerCmd_mmsFlightGravityPitchOffsetMinMax(val1, val2);
	#endif
}

// 
// maxSpeedGroundFrictionFactor
AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE;
void mmsFlightGroundMaxSpeedFrictionFactor(F32 val)
{
	maxSpeedGroundFrictionFactor = val;
}

AUTO_COMMAND ACMD_CLIENTONLY;
void mmcFlightGroundMaxSpeedFrictionFactor(F32 val)
{
	maxSpeedGroundFrictionFactor = val;

	#ifdef GAMECLIENT
		ServerCmd_mmsFlightGroundMaxSpeedFrictionFactor(val);
	#endif
}

// 
// groundTractionFactor
AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE;
void mmsFlightGroundTractionFactor(F32 val)
{
	groundTractionFactor = val;
}

AUTO_COMMAND ACMD_CLIENTONLY;
void mmcFlightGroundTractionFactor(F32 val)
{
	groundTractionFactor = val;

	#ifdef GAMECLIENT
		ServerCmd_mmsFlightGroundTractionFactor(val);
	#endif
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE;
void mmsFlightDontUseStickyGround(bool val)
{
	gConf.bDontUseStickyFlightGroundCollision = val;
}

AUTO_COMMAND ACMD_CLIENTONLY;
void mmcFlightDontUseStickyGround(bool val)
{
	gConf.bDontUseStickyFlightGroundCollision = val;

	#ifdef GAMECLIENT
		ServerCmd_mmsFlightDontUseStickyGround(val);
	#endif
}


#include "AutoGen/EntityMovementFlight_c_ast.c"
#include "AutoGen/EntityMovementFlight_h_ast.c"

