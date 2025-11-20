#include "EntityMovementDragon.h"
#include "EntityMovementDragon_c_ast.h"
#include "Entity.h"
#include "EntityMovementDefault.h"
#include "EntityMovementManager.h"
#include "EntityMovementRequesterDefs.h"
#include "mathutil.h"
#include "quat.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Physics););


AUTO_RUN_MM_REGISTER_REQUESTER_MSG_HANDLER(	mrDragonMsgHandler,
											"DragonMovement",
											Dragon);


typedef enum DragonStanceType {
	MR_DRAGON_STANCE_MOVING,
	MR_DRAGON_STANCE_RUNNING,
	MR_DRAGON_STANCE_DRAGONTURN,
	MR_DRAGON_STANCE_TURNLEFT,
	MR_DRAGON_STANCE_TURNRIGHT,
	
	MR_DRAGON_STANCE_COUNT,
} DragonStanceType;


AUTO_STRUCT;
typedef struct DragonBG 
{
	DragonMovementDef	dm;
	Vec3				targetPos;
	Vec3				vel;
	F32					maxSpeed;
	F32					friction;
	F32					traction;
	F32					fDesiredFacingYaw;
	F32					fDesiredRootYaw;

	EntityRef			erFaceTarget;
	EntityRef			erOverrideFaceTarget;
	Vec3				vOverrideFacePos;
	F32					fOverrideYawOffset;
	F32					fOverrideFacingYaw;

	U32					stanceHandle[MR_DRAGON_STANCE_COUNT];

	U32					hasStanceMask : MR_DRAGON_STANCE_COUNT;
	U32					reachedDesiredFacing : 1;

	U32					isMoving : 1;
	U32					isTranslating : 1;
	U32					bHasMovementTarget : 1;
	U32					isRotating : 1;
	U32					bUsedOverride : 1;
	U32					bIsOnGround : 1;
	U32					bOverrideHeadToBodyFacing : 1;
	U32					bOverrideBodyToHeadFacing : 1;
} DragonBG;

AUTO_STRUCT;
typedef struct DragonLocalBG 
{
	S32 unused;
} DragonLocalBG;

AUTO_STRUCT;
typedef struct DragonToFG 
{
	S32	unused;
} DragonToFG;

AUTO_STRUCT;
typedef struct DragonToBGFlags
{
	U32		bSpeedUpdate : 1;
	U32		bFrictionUpdate : 1;
	U32		bTractionUpdate : 1;
	U32		bUpdatedMovementDef : 1;
	U32		bFaceTargetUpdate : 1;
	U32		bOverrideTargetUpdate : 1;
	U32		bOverridePosUpdate : 1;

	U32		bOverrideFacingUpdate : 1;
	U32		bOverrideHeadToBodyFacing : 1;

	U32		bOverrideRotateToFacingUpdate : 1;
	U32		bOverrideBodyToHeadFacing : 1;
	
} DragonToBGFlags;

AUTO_STRUCT;
typedef struct DragonToBG 
{
	DragonMovementDef	dm;
	F32					maxSpeed;
	F32					friction;
	F32					traction;
	EntityRef			erFaceTarget;
	EntityRef			erOverrideFaceTarget;
	Vec3				vOverrideFacePos;
	F32					fOverrideYawOffset;
	
	DragonToBGFlags		flags;

} DragonToBG;

AUTO_STRUCT;
typedef struct DragonFG 
{
	DragonToBG	toBG;
} DragonFG;

AUTO_STRUCT;
typedef struct DragonSync 
{
	S32 unused;
} DragonSync;




static S32 mrDragonGetBitHandleFromStanceType(	DragonStanceType stanceType,
												U32* handleOut)
{
#define CASE(x, y) xcase x: *handleOut = mmAnimBitHandles.y
	switch(stanceType)
	{
		CASE(MR_DRAGON_STANCE_MOVING, moving);
		CASE(MR_DRAGON_STANCE_RUNNING, running);
		CASE(MR_DRAGON_STANCE_DRAGONTURN, dragonTurn);
		CASE(MR_DRAGON_STANCE_TURNLEFT, turnLeft);
		CASE(MR_DRAGON_STANCE_TURNRIGHT, turnRight);
		xdefault:
			return 0;
	}
#undef CASE

	return 1;
}

// ------------------------------------------------------------------------------------------------------------------------------
static void mrDragonStanceSetBG(const MovementRequesterMsg* msg,
								DragonBG* bg,
								DragonStanceType stanceType)
{
	if(FALSE_THEN_SET_BIT(bg->hasStanceMask, BIT(stanceType)))
	{
		U32 bitHandle;

		if(!mrDragonGetBitHandleFromStanceType(stanceType, &bitHandle))
			return;

		mrmAnimStanceCreateBG(	msg,
								&bg->stanceHandle[stanceType],
								bitHandle);
	}
}

// ------------------------------------------------------------------------------------------------------------------------------
static void mrDragonStanceClearBG(	const MovementRequesterMsg* msg,
									DragonBG* bg,
									DragonStanceType stanceType)
{
	if(TRUE_THEN_RESET_BIT(bg->hasStanceMask, BIT(stanceType)))
	{
		U32 bitHandle;

		if(!mrDragonGetBitHandleFromStanceType(stanceType, &bitHandle))
			return;

		mrmAnimStanceDestroyBG(	msg,
								&bg->stanceHandle[stanceType]);
	}
}

// ------------------------------------------------------------------------------------------------------------------------------
static void mrDragonStance_SetTranslating(	const MovementRequesterMsg* msg,
											DragonBG* bg, 
											bool bisTranslating)
{
	if (bg->isTranslating == (U32)bisTranslating)
		return;

	bg->isTranslating = bisTranslating;

	if (bg->isTranslating)
	{
		if (!bg->isMoving)
		{
			bg->isMoving = true;
			mrDragonStanceSetBG(msg, bg, MR_DRAGON_STANCE_MOVING);
		}
	}
	else
	{
		if (!bg->isRotating)
		{
			if (bg->isMoving)
			{
				bg->isMoving = false;
				mrDragonStanceClearBG(msg, bg, MR_DRAGON_STANCE_MOVING);
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------------------
static void mrDragonStance_SetRotating(	const MovementRequesterMsg* msg,
										DragonBG* bg, 
										bool bIsRotating)
{
	if (bg->isRotating == (U32)bIsRotating)
		return;

	bg->isRotating = bIsRotating;

	if (bg->isRotating)
	{
		if (!bg->isMoving)
		{
			bg->isMoving = true;
			mrDragonStanceSetBG(msg, bg, MR_DRAGON_STANCE_MOVING);
		}
	}
	else
	{
		if (!bg->isTranslating)
		{
			if (bg->isMoving)
			{
				bg->isMoving = false;
				mrDragonStanceClearBG(msg, bg, MR_DRAGON_STANCE_MOVING);
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------------------
static F32 mrDragonGetFriction(	const MovementRequesterMsgCreateOutputShared *shared,
								DragonBG *bg)
{
	F32 fFriction;
	
	if(shared->target.pos->frictionType != MST_OVERRIDE)
	{
		fFriction = bg->friction;
	}
	else
	{
		fFriction = shared->target.pos->friction;
	}


	return fFriction * bg->maxSpeed * MM_SECONDS_PER_STEP;
}

// ------------------------------------------------------------------------------------------------------------------------------
static F32 mrDragonGetTraction(	const MovementRequesterMsgCreateOutputShared *shared,
								DragonBG *bg)
{
	F32 fTraction;
	
	if(shared->target.pos->tractionType != MST_OVERRIDE)
	{
		fTraction = bg->traction;
	}
	else
	{
		fTraction = shared->target.pos->traction;
	}


	return fTraction * bg->maxSpeed * MM_SECONDS_PER_STEP;
}


// ------------------------------------------------------------------------------------------------------------------------------
static void mrDragonHandleCreateOutputPositionChange(	const MovementRequesterMsg* msg,
														DragonBG* bg,
														DragonToFG* toFG)
{
	const MovementRequesterMsgCreateOutputShared*	shared = msg->in.bg.createOutput.shared;
	//Vec3 vTargetPos;
	Quat qRot;
	Vec3 pyrRot, vCurPos, vToTargetPos;
	Vec2 vToTargetPY;
	Vec3 vVelTarget;
	F32 fDesiredSpeed = 20.f;
	F32 fAngleDiff;
	F32 fDistToTarget;
	F32 fStepDist;

	// get the target position we're trying to go to
	// get our current facing direction 

	// see if we can go towards our target location
	bg->bHasMovementTarget = false;
	
	bg->bIsOnGround = true;
	if (!bg->bIsOnGround)
	{
		Vec3 vStepVel;
		bg->vel[1] -= 80.f * MM_SECONDS_PER_STEP;
		
		scaleVec3(bg->vel, MM_SECONDS_PER_STEP, vStepVel);

		mrmTranslatePositionBG(msg, vStepVel, true, false);
		return;
	}

	if (shared->target.pos->targetType != MPTT_POINT)
	{
		mrDragonStance_SetTranslating(msg, bg, false);
		mrDragonStanceClearBG(msg, bg, MR_DRAGON_STANCE_RUNNING);
		zeroVec3(bg->vel);
		return; // not handling anything other than position target type
	}

	fDesiredSpeed = bg->maxSpeed;

	mrmGetPositionAndRotationBG(msg, vCurPos, qRot);
	
	copyVec3(shared->target.pos->point, bg->targetPos);
	subVec3(bg->targetPos, vCurPos, vToTargetPos);

	if (lengthVec3Squared(vToTargetPos) < 0.001f)
	{
		mrDragonStance_SetTranslating(msg, bg, false);
		mrDragonStanceClearBG(msg, bg, MR_DRAGON_STANCE_RUNNING);
		zeroVec3(bg->vel);
		return;
	}
	
	bg->bHasMovementTarget = true;
	quatToPYR(qRot, pyrRot);
	
	getVec3YP(vToTargetPos, vToTargetPY+1, vToTargetPY);

	fAngleDiff = subAngle(vToTargetPY[1], pyrRot[1]);
	if (ABS(fAngleDiff) > bg->dm.fMovementFacingAngleThreshold)
	{	// can't go towards the point until we're facing it more
		mrDragonStance_SetTranslating(msg, bg, false);
		mrDragonStanceClearBG(msg, bg, MR_DRAGON_STANCE_RUNNING);
		zeroVec3(bg->vel);
		bg->reachedDesiredFacing = false;
		return;
	}
	else
	{
		copyVec3(vToTargetPos, vVelTarget);
	}

	mrDragonStance_SetTranslating(msg, bg, true);
	mrDragonStanceSetBG(msg, bg, MR_DRAGON_STANCE_RUNNING);

	{
		Vec3 vStepVel;
		F32 friction = mrDragonGetFriction(shared, bg);
		F32 traction = mrDragonGetTraction(shared, bg);
				
		fDistToTarget = normalVec3(vVelTarget);
		scaleVec3(vVelTarget, fDesiredSpeed, vVelTarget);

		mrmApplyFriction(msg, bg->vel, friction, vVelTarget);
		mrmApplyTraction(msg, bg->vel, traction, vVelTarget);
		

		fStepDist = MM_SECONDS_PER_STEP * fDesiredSpeed;
		if (fStepDist > fDistToTarget)
		{
			fStepDist = fDistToTarget;
			scaleVec3(vVelTarget, fStepDist, vStepVel);
		}
		else
		{
			scaleVec3(bg->vel, MM_SECONDS_PER_STEP, vStepVel);
		}
			
		vStepVel[1] -= 80.f * MM_SECONDS_PER_STEP;

		mrmTranslatePositionBG(msg, vStepVel, true, false);
	}
}

// ------------------------------------------------------------------------------------------------------------------------------
static void mrDragonHandleCreateOutputRotationChange(	const MovementRequesterMsg* msg,
														DragonBG* bg,
														DragonToFG* toFG)
{
	const MovementRequesterMsgCreateOutputShared*	shared = msg->in.bg.createOutput.shared;
	Quat qCurRootRot;
	Vec2 pyCurFace;
	Vec3 vCurPos;
	F32 fCurRootYaw;
	F32 fFaceDiff, fFaceStep;
	F32 fRootDiff, fRootStep;
	bool bRotated = false;
	bool bHasFaceTarget = false;

	mrmGetPositionBG(msg, vCurPos);
	
	if (shared->target.rot->targetType == MRTT_DIRECTION && !vec3IsZero(bg->vOverrideFacePos))
	{
		Vec3 vDir;
		subVec3(bg->vOverrideFacePos, vCurPos, vDir);
		if(!vec3IsZeroXZ(vDir))
		{
			bHasFaceTarget = true;
			getVec3YP(vDir, &bg->fDesiredFacingYaw, NULL);
			if (bg->fOverrideYawOffset)
			{
				bg->fOverrideFacingYaw = subAngle(bg->fDesiredFacingYaw, bg->fOverrideYawOffset);
			}
		}
	}
	else if (bg->erFaceTarget || bg->erOverrideFaceTarget)
	{
		EntityRef erTarget = bg->erOverrideFaceTarget ? bg->erOverrideFaceTarget : bg->erFaceTarget;
		Vec3 vTargetFacePos;
		if(mrmGetEntityPositionBG(msg, erTarget, vTargetFacePos))
		{
			Vec3 vDir;
			subVec3(vTargetFacePos, vCurPos, vDir);
			if(!vec3IsZeroXZ(vDir))
			{
				bHasFaceTarget = true;
				getVec3YP(vDir, &bg->fDesiredFacingYaw, NULL);
				if (bg->erOverrideFaceTarget && bg->fOverrideFacingYaw == 0.f)
				{
					bg->fOverrideFacingYaw = subAngle(bg->fDesiredFacingYaw, bg->fOverrideYawOffset);
				}
			}
		}
	}

		
	mrmGetFacePitchYawBG(msg, pyCurFace);
	// get the root rotation diff 
	{
		Vec3 vCurRootDir;

		mrmGetRotationBG(msg, qCurRootRot);
		quatToMat3_2(qCurRootRot, vCurRootDir);
		getVec3YP(vCurRootDir, &fCurRootYaw, NULL);
	}

	if (bg->bHasMovementTarget)
	{	// if we haven't reached our target position yet, rotate towards it so we can move there.
		Vec3 vDir;
		
		mrmGetPositionBG(msg, vCurPos);
		subVec3(bg->targetPos, vCurPos, vDir);
		if(!vec3IsZeroXZ(vDir))
		{
			getVec3YP(vDir, &bg->fDesiredRootYaw, NULL);
			if (!bHasFaceTarget)
				getVec3YP(vDir, &bg->fDesiredFacingYaw, NULL);
		}

		// if we are translating then we snap our movement rotation to our current facing.?
		// 
		bg->reachedDesiredFacing = false;
	}
	else
	{
		switch(shared->target.rot->targetType)
		{
			xcase MRTT_POINT:
			{
				Vec3 vDir;
							
				subVec3(shared->target.rot->point, vCurPos, vDir);
				if(!vec3IsZeroXZ(vDir))
				{
					getVec3YP(vDir, &bg->fDesiredRootYaw, NULL);
					if (!bHasFaceTarget)
						getVec3YP(vDir, &bg->fDesiredFacingYaw, NULL);
				}
			}
			xcase MRTT_DIRECTION:
			{
				// the powers is overriding target, lock down the rotation if we're under direction influence
				Vec3 vDir;
				copyVec3(shared->target.rot->dir, vDir);
				
				bg->bUsedOverride = true;
				bg->fDesiredRootYaw = fCurRootYaw;
				
				if (bg->bOverrideHeadToBodyFacing)
				{
					bg->fDesiredFacingYaw = fCurRootYaw;
				}
				else if (bg->fOverrideFacingYaw)
				{
					bg->fDesiredFacingYaw = bg->fOverrideFacingYaw;
				}
				else
				{
					bg->fDesiredFacingYaw = pyCurFace[1];
				}
				
				if (bg->bOverrideBodyToHeadFacing)
				{
					bg->fDesiredRootYaw = bg->fDesiredFacingYaw;
				}
				
				
				if(vec3IsZeroXZ(vDir))
				{
					return;
				}
			}
			xcase MRTT_ROTATION:
			{
				Vec3 vDir;

				quatToMat3_2(shared->target.rot->rot, vDir);
				getVec3YP(vDir, &bg->fDesiredRootYaw, NULL);
				if (!bHasFaceTarget)
					getVec3YP(vDir, &bg->fDesiredFacingYaw, NULL);
			}

		}
	

		if (bg->bUsedOverride && shared->target.rot->targetType != MRTT_DIRECTION)
		{
			bg->erOverrideFaceTarget = 0;
			bg->fOverrideYawOffset = 0.f;
			bg->fOverrideFacingYaw = 0.f;
			bg->bOverrideHeadToBodyFacing = false;
			bg->bOverrideBodyToHeadFacing = false;
			bg->bUsedOverride = false;
		}
	}

	// get the current facing and diff to our desired
	fFaceDiff = subAngle(bg->fDesiredFacingYaw, pyCurFace[1]);
	fRootDiff = subAngle(bg->fDesiredRootYaw, fCurRootYaw);
	
	
	
	fRootStep = mrmCalculateTurningStep(fRootDiff, 
										bg->dm.fTurnRateBasis, 
										bg->dm.fTurnRate, 
										bg->dm.fMinTurnRate, 
										bg->dm.fMaxTurnRate);

	fFaceStep = mrmCalculateTurningStep(fFaceDiff, 
										bg->dm.fHeadTurnBasis, 
										bg->dm.fHeadTurnRate, 
										bg->dm.fHeadTurnRateMin, 
										bg->dm.fHeadTurnRateMax);

	if (bg->bOverrideBodyToHeadFacing ||
		!bg->reachedDesiredFacing || 
		(ABS(fRootDiff) > bg->dm.fReadjustFacingAngleThreshold) )
	{
		fCurRootYaw = addAngle(fCurRootYaw, fRootStep);
		yawQuat(-fCurRootYaw, qCurRootRot);
		mrmSetRotationBG(msg, qCurRootRot);

		if (!bg->isTranslating)
		{
			bg->reachedDesiredFacing = ABS(fRootDiff) < RAD(3.f);
			if (!bg->reachedDesiredFacing)
				bRotated = true;
		}
	}


	if (!bRotated || bg->isTranslating)
	{	// if we are moving forward, or ending our rotation, stop the turning animation
		mrDragonStance_SetRotating(msg, bg, false);
		mrDragonStanceClearBG(msg, bg, MR_DRAGON_STANCE_DRAGONTURN);
		mrDragonStanceClearBG(msg, bg, MR_DRAGON_STANCE_TURNRIGHT);
		mrDragonStanceClearBG(msg, bg, MR_DRAGON_STANCE_TURNLEFT);
	}
	else 
	{
		mrDragonStance_SetRotating(msg, bg, true);
		
		if (fRootStep < 0.f)
		{
			mrDragonStanceSetBG(msg, bg, MR_DRAGON_STANCE_DRAGONTURN);
			mrDragonStanceSetBG(msg, bg, MR_DRAGON_STANCE_TURNLEFT);
			mrDragonStanceClearBG(msg, bg, MR_DRAGON_STANCE_TURNRIGHT);
		}
		else 
		{
			mrDragonStanceSetBG(msg, bg, MR_DRAGON_STANCE_DRAGONTURN);
			mrDragonStanceSetBG(msg, bg, MR_DRAGON_STANCE_TURNRIGHT);
			mrDragonStanceClearBG(msg, bg, MR_DRAGON_STANCE_TURNLEFT);
		}
	}
		
		
	// head facing
	pyCurFace[1] = addAngle(pyCurFace[1], fFaceStep);
	{
		F32 fFaceVsRootDiff = subAngle(pyCurFace[1], fCurRootYaw);
		F32 fMaxDelta = (! bg->fOverrideFacingYaw) ? bg->dm.fHeadVsRootMaxDelta : bg->dm.fHeadVsRootMaxDeltaOnOverride;
		// check to see if we're past the facing vs root rotation, and if so clamp 
		
		if (ABS(fFaceVsRootDiff) > bg->dm.fHeadVsRootMaxDelta)
		{
			if (fFaceVsRootDiff < 0.f)
			{
				pyCurFace[1] = subAngle(fCurRootYaw, fMaxDelta);
			}
			else
			{
				pyCurFace[1] = addAngle(fCurRootYaw, fMaxDelta);
			}
		}
	}
		
	mrmSetFacePitchYawBG(msg, pyCurFace);

	return;
}

// ------------------------------------------------------------------------------------------------------------------------------
void mrDragonMsgHandler(const MovementRequesterMsg* msg)
{
	DragonFG*			fg;
	DragonBG*			bg;
	DragonLocalBG*	localBG;
	DragonToFG*		toFG;
	DragonToBG*		toBG;
	DragonSync*		sync;

	MR_MSG_HANDLER_GET_DATA_DEFAULT(msg, Dragon);

	switch(msg->in.msgType)
	{
		xcase MR_MSG_BG_INITIALIZE:
		{
			mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
			mrmSetUseRotationForCapsuleOrientationBG(msg, true);
		}
		
		xcase MR_MSG_BG_FORCE_CHANGED_ROT:
		{
			Quat qRot;
			Vec3 pyr;

			mrmGetRotationBG(msg, qRot);
			quatToPYR(qRot, pyr);

			bg->fDesiredFacingYaw = pyr[1];
			bg->fDesiredRootYaw = pyr[1];
		}

		xcase MR_MSG_BG_GET_DEBUG_STRING:
		{
			char*	buffer = msg->in.bg.getDebugString.buffer;
			U32		bufferLen = msg->in.bg.getDebugString.bufferLen;
			/*
			sprintf_s(	buffer,
				bufferLen,
				"");
			*/
		}

		xcase MR_MSG_BG_DATA_WAS_RELEASED:
		{
			if (msg->in.bg.dataWasReleased.dataClassBits & MDC_BIT_ROTATION_CHANGE)
			{
				bg->isTranslating = false;
			}

			if (msg->in.bg.dataWasReleased.dataClassBits & (MDC_BIT_POSITION_CHANGE | MDC_BIT_ROTATION_CHANGE))
			{
				mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
			}

		}

		xcase MR_MSG_BG_DATA_RELEASE_REQUESTED:
		{
			
		}

		xcase MR_MSG_BG_INPUT_EVENT:
		{
		}

		xcase MR_MSG_FG_CREATE_TOBG:
		{
			*toBG = fg->toBG;

			{
				DragonToBGFlags clear = {0};
				fg->toBG.flags = clear;
			}
			
			mrmEnableMsgUpdatedToBG(msg);
		}

		xcase MR_MSG_BG_UPDATED_TOBG:
		{
			if (TRUE_THEN_RESET(toBG->flags.bUpdatedMovementDef))
			{
				bg->dm = toBG->dm;
			}
			if (TRUE_THEN_RESET(toBG->flags.bSpeedUpdate))
			{
				bg->maxSpeed = toBG->maxSpeed;
			}
			if (TRUE_THEN_RESET(toBG->flags.bFrictionUpdate))
			{
				bg->friction = toBG->friction;
			}
			if (TRUE_THEN_RESET(toBG->flags.bTractionUpdate))
			{
				bg->traction = toBG->traction;
			}
			if (TRUE_THEN_RESET(toBG->flags.bFaceTargetUpdate))
			{
				bg->erFaceTarget = toBG->erFaceTarget;
			}
			if (TRUE_THEN_RESET(toBG->flags.bOverrideTargetUpdate))
			{
				bg->erOverrideFaceTarget = toBG->erOverrideFaceTarget;
				bg->fOverrideYawOffset = toBG->fOverrideYawOffset;
			}
			if (TRUE_THEN_RESET(toBG->flags.bOverridePosUpdate))
			{
				copyVec3(toBG->vOverrideFacePos, bg->vOverrideFacePos);
				bg->fOverrideYawOffset = toBG->fOverrideYawOffset;
			}
			if (TRUE_THEN_RESET(toBG->flags.bOverrideFacingUpdate))
			{
				bg->bOverrideHeadToBodyFacing = toBG->flags.bOverrideHeadToBodyFacing;
			}
			if (TRUE_THEN_RESET(toBG->flags.bOverrideRotateToFacingUpdate))
			{
				bg->bOverrideBodyToHeadFacing = toBG->flags.bOverrideBodyToHeadFacing;
			}
		}

		xcase MR_MSG_BG_CONTROLLER_MSG:
		{
			bg->bIsOnGround = msg->in.bg.controllerMsg.isGround;
			if (bg->bIsOnGround)
			{
				bg->vel[1] = 0.f;
			}
		}

		xcase MR_MSG_BG_DISCUSS_DATA_OWNERSHIP:
		{
			U32 bitsToAcquire = MDC_BIT_POSITION_CHANGE | MDC_BIT_ROTATION_CHANGE;
			U32 ownedBits = 0;

			if(mrmAcquireDataOwnershipBG(msg, bitsToAcquire, 0, NULL, &ownedBits))
			{
				if (ownedBits == bitsToAcquire)
				{
					mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
				}
			}
		}

		xcase MR_MSG_BG_CREATE_OUTPUT:
		{
			switch(msg->in.bg.createOutput.dataClassBit)
			{
				xcase MDC_BIT_POSITION_CHANGE:
				{
					mrDragonHandleCreateOutputPositionChange(msg, bg, toFG);
				}

				xcase MDC_BIT_ROTATION_CHANGE:
				{
					mrDragonHandleCreateOutputRotationChange(msg, bg, toFG);
				}
			}
		}

		xcase MR_MSG_BG_CREATE_DETAILS:
		{
		}
	}
}


// ------------------------------------------------------------------------------------------------------------------------------
S32 mrDragon_InitializeSettings(MovementRequester* mr, DragonMovementDef *pSettings)
{
	DragonFG* fg = NULL;

	if (pSettings && mrGetFG(mr, mrDragonMsgHandler, &fg))
	{
		fg->toBG.dm = *pSettings;

		fg->toBG.dm.fTurnRate = RAD(fg->toBG.dm.fTurnRate);
		fg->toBG.dm.fMaxTurnRate = RAD(fg->toBG.dm.fMaxTurnRate);
		fg->toBG.dm.fMinTurnRate = RAD(fg->toBG.dm.fMinTurnRate);
		fg->toBG.dm.fTurnRateBasis = RAD(fg->toBG.dm.fTurnRateBasis);
		fg->toBG.dm.fMovementFacingAngleThreshold = RAD(fg->toBG.dm.fMovementFacingAngleThreshold);
		fg->toBG.dm.fReadjustFacingAngleThreshold = RAD(fg->toBG.dm.fReadjustFacingAngleThreshold);
		fg->toBG.dm.fHeadTurnRate = RAD(fg->toBG.dm.fHeadTurnRate);
		fg->toBG.dm.fHeadTurnBasis = RAD(fg->toBG.dm.fHeadTurnBasis);
		fg->toBG.dm.fHeadTurnRateMin = RAD(fg->toBG.dm.fHeadTurnRateMin);
		fg->toBG.dm.fHeadTurnRateMax = RAD(fg->toBG.dm.fHeadTurnRateMax);
		fg->toBG.dm.fHeadVsRootMaxDelta = RAD(fg->toBG.dm.fHeadVsRootMaxDelta);
		fg->toBG.dm.fHeadVsRootMaxDeltaOnOverride = RAD(fg->toBG.dm.fHeadVsRootMaxDeltaOnOverride);

		fg->toBG.flags.bUpdatedMovementDef = true;
		
		return mrEnableMsgCreateToBG(mr);
	}

	return 0;
}


// ------------------------------------------------------------------------------------------------------------------------------
S32 mrDragon_SetMaxSpeed(	MovementRequester* mr,
							F32 speed)
{
	DragonFG* fg = NULL;

	if (mrGetFG(mr, mrDragonMsgHandler, &fg))
	{
		if (speed != fg->toBG.maxSpeed)
		{
			fg->toBG.maxSpeed = speed;
			fg->toBG.flags.bSpeedUpdate = true;
			return mrEnableMsgCreateToBG(mr);
		}

		return 1;
	}

	return 0;
}


// ------------------------------------------------------------------------------------------------------------------------------
S32 mrDragon_SetTraction(	MovementRequester* mr,
							F32 traction)
{
	DragonFG* fg = NULL;

	if (mrGetFG(mr, mrDragonMsgHandler, &fg))
	{
		if (traction != fg->toBG.traction)
		{
			fg->toBG.traction = traction;
			fg->toBG.flags.bTractionUpdate = true;
			return mrEnableMsgCreateToBG(mr);
		}

		return 1;
	}

	return 0;
}


// ------------------------------------------------------------------------------------------------------------------------------
S32 mrDragon_SetFriction(	MovementRequester* mr,
							F32 friction)
{	
	DragonFG* fg = NULL;

	if (mrGetFG(mr, mrDragonMsgHandler, &fg))
	{
		if (friction != fg->toBG.friction)
		{
			fg->toBG.friction = friction;
			fg->toBG.flags.bFrictionUpdate = true;
			return mrEnableMsgCreateToBG(mr);
		}

		return 1;
	}

	return 0;
}

// ------------------------------------------------------------------------------------------------------------------------------
S32 mrDragon_SetTargetFaceEntity(	MovementRequester* mr,
									EntityRef erEnt)
{
	DragonFG* fg = NULL;

	if (mrGetFG(mr, mrDragonMsgHandler, &fg))
	{
		if (erEnt != fg->toBG.erFaceTarget)
		{
			fg->toBG.erFaceTarget = erEnt;
			fg->toBG.flags.bFaceTargetUpdate = true;
			return mrEnableMsgCreateToBG(mr);
		}

		return 1;
	}

	return 0;
}

// ------------------------------------------------------------------------------------------------------------------------------
S32 mrDragon_SetOverrideTargetAndPowersAngleOffset(	MovementRequester* mr,
													EntityRef erEnt,
													F32 fAngleOffset)
{
	DragonFG* fg = NULL;

	if (mrGetFG(mr, mrDragonMsgHandler, &fg))
	{
		fg->toBG.erOverrideFaceTarget = erEnt;
		fg->toBG.fOverrideYawOffset = RAD(fAngleOffset);
		fg->toBG.flags.bOverrideTargetUpdate = true;
		return mrEnableMsgCreateToBG(mr);
	}

	return 0;
}

// ------------------------------------------------------------------------------------------------------------------------------
S32 mrDragon_SetOverrideTargetPosAndPowersAngleOffset(	MovementRequester* mr,
														const Vec3 vTargetPos,
														F32 fAngleOffset)
{
	DragonFG* fg = NULL;

	if (mrGetFG(mr, mrDragonMsgHandler, &fg))
	{
		copyVec3(vTargetPos, fg->toBG.vOverrideFacePos);
		fg->toBG.fOverrideYawOffset = RAD(fAngleOffset);
		if (!fg->toBG.fOverrideYawOffset)
			fg->toBG.fOverrideYawOffset = RAD(40.f);

		fg->toBG.flags.bOverridePosUpdate = true;
		return mrEnableMsgCreateToBG(mr);
	}

	return 0;
}

// ------------------------------------------------------------------------------------------------------------------------------
S32 mrDragon_SetOverrideRotateHeadToBodyOrientation( MovementRequester *mr)
{
	DragonFG* fg = NULL;

	if (mrGetFG(mr, mrDragonMsgHandler, &fg))
	{
		fg->toBG.flags.bOverrideFacingUpdate = true;
		fg->toBG.flags.bOverrideHeadToBodyFacing = true;
		return mrEnableMsgCreateToBG(mr);
	}

	return 0;
}

// ------------------------------------------------------------------------------------------------------------------------------
S32 mrDragon_SetOverrideRotateBodyToHeadOrientation( MovementRequester *mr)
{
	DragonFG* fg = NULL;

	if (mrGetFG(mr, mrDragonMsgHandler, &fg))
	{
		fg->toBG.flags.bOverrideRotateToFacingUpdate = true;
		fg->toBG.flags.bOverrideBodyToHeadFacing = true;
		return mrEnableMsgCreateToBG(mr);
	}

	return 0;
}

#include "AutoGen/EntityMovementDragon_c_ast.c"
