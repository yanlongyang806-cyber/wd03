AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Physics););


#include "aiCivilian.h"
#include "aiCivilianPrivate.h"
#include "Entity.h"
#include "EntityMovementManager.h"
#include "EntityMovementProjectile.h"
#include "rand.h"
#include "WorldColl.h"
#include "WorldLib.h"
#include "EntityMovementManager.h"
#include "aiCivilianPrivate_h_ast.h"
#include "AutoGen/AICivMovement_c_ast.h"


static F32 s_fCarAcceleration = 20.f;
static F32 s_fCarBrakeSpeed = 60.f;


AUTO_RUN_MM_REGISTER_UNSYNCED_REQUESTER_MSG_HANDLER(aiCivilianMovementMsgHandler,
													"AICivilianMovement",
													AICivilianMovement);

#define CURVE_STEP		(0.05f)

static const S32 FLOATS_PER_WAYPT = 7;

#define IS_NOT_NEAR_ZERO(x)		((x) < -0.00001f || (x) > 0.00001f)
#define IS_NEAR_ZERO(x)		((x) > -0.00001f && (x) < 0.00001f)

#define PED_MAX_TURN_RATE	(PI*1.42f)

extern const S32 sWAYPOINTLIST_MAX_SIZE;
extern const S32 sWAYPOINTLIST_TRUNCATE_AMOUNT;

#define ANIM_MIN_TURNING_ANGLE	RAD(4.0f)
#define ANIM_MID_TURNING_ANGLE	RAD(8.0f)
#define ANIM_MAX_TURNING_ANGLE	RAD(16.0f)

#define PEDESTRIAN_RUN_THRESHOLD			10.5f

#define PEDESTRIAN_SPEED_REEVALUATE_TIME	20

#define CAR_SPEED_REEVALUATE_TIME			10

#define RAYCAST_UP_DIST		5.f
#define RAYCAST_DOWN_DIST	15.f

// Car Turning Params
static F32 s_fCarMaxTurnRate = 12.0f;
static F32 s_fCarMaxPitchRate = HALFPI;
static F32 s_fCarTurningNormBasis = PI;

// Trolley
static F32 s_fTrolleyMaxTurnRate = 20.0f;
static F32 s_fTrolleyMaxPitchRate = HALFPI;
static F32 s_fTrolleyTurningNormBasis = 7.f;
static F32 s_fTrolleyAccelerationRate = 20.0f;
static F32 s_fTrolleyDecelerationRate = 45.0f;


AUTO_CMD_FLOAT(s_fCarMaxTurnRate, civCarMaxTurnRate);
AUTO_CMD_FLOAT(s_fCarMaxPitchRate, civCarMaxPitchRate);
AUTO_CMD_FLOAT(s_fCarTurningNormBasis, civCarTurningNormBasis);

typedef enum AICivilianStanceType {
	MR_AICIV_STANCE_MOVING,
	MR_AICIV_STANCE_RUNNING,

	MR_AICIV_STANCE_COUNT,
} AICivilianStanceType;

AUTO_STRUCT;
typedef struct AICivilianMovementToBG {

	F32							fSpeedMinimum;
	F32							fSpeedRange;
	AICivilianWaypoint			**eaAddedWaypoints; AST (UNOWNED)
	EAICivilianType				eCivType;
	F32							fFinalRotation;
	
	// the critter speed set from powers
	F32							fCritterSpeedOverride;
	U32							clearWaypointID;

	U32							pause : 1;
	U32							bDoCollision : 1;
	U32							bUseFinalRotation : 1;
	U32							bUseOverrideCritterSpeed : 1;
	
	// make sure to clear all these after sending/receiving
	U32 bHasAddedWaypoints : 1;
	
	U32 bUpdatedPause : 1;
	U32 bUpdatedMovementOptions : 1;
	U32 bUpdatedFinalRotation : 1;
	U32 bInitialization : 1;
	U32 bSetCritterSpeed : 1;
	U32 bUseCritterSpeedUpdated : 1;
	U32 bDisable : 1;

} AICivilianMovementToBG;

AUTO_STRUCT;
typedef struct AICivilianMovementToFG {
	S32 numReachedWp;
	U32 releasedWaypointsID;
} AICivilianMovementToFG;

AUTO_STRUCT;
typedef struct AICivilianMovementFG {
	AICivilianMovementToBG toBG;
	AICivilianMovementToFG toFG;

	U32			spcClearedWaypoints;
} AICivilianMovementFG;

AUTO_STRUCT;
typedef struct AICivilianMovementBG {
	AICivilianMovementToBG toBG;
	AICivilianMovementToFG toFG;
	
	// TODO: should unionize this struct as some variables are only for cars and some only for pedestrians
	Vec3 vCurveWp;
	Quat qLastRot;
	Vec3 vMoveDir;
	Vec3 vMoveDirLast;
	Vec3 vNormal;
	Vec3 vNormalLast;
	U32 forcedTurnCountdown;

	Mat4 avCurveControlPoints;	// this actually isn't a Mat4, it is supposed to be an array of 4 vec3s
								// but structParser won't allow an array of Vec3s and this works just as well
	F32 fCurCurveT;
	U32 numWaypointsInCurve;

	AICivilianWaypoint		**eaWaypoints;		AST (UNOWNED)
	S32						curWpIdx;

	F32 fCurSpeed;
	F32 fCurDesiredSpeed;
	F32 fCurFacing;
	F32 fLastFacingDiff;
	S64 timeLastRaycast;
	S64 timeLastSpeedVar;

	F32 fInterpToGroundDist;

	U32 stanceHandle[MR_AICIV_STANCE_COUNT];

	U32 handlesRotationChange : 1;
	U32 speedMinimumNearZero : 1;
	U32 speedNearZero : 1;
	U32 bCurWpIsGroundAligned : 1;
	U32 bCheckGroundAlignment : 1;
	U32 bCurWpIsCurve : 1;
	U32 bSkippedMovement : 1;
	U32 hasStanceMask : MR_AICIV_STANCE_COUNT;
	
} AICivilianMovementBG;

AUTO_STRUCT;
typedef struct AICivilianMovementLocalBG {
	S32 unused;
} AICivilianMovementLocalBG;

AUTO_STRUCT;
typedef struct AICivilianMovementSync {
	S32 unused;
} AICivilianMovementSync;

static S32 mrAICivGetBitHandleFromStanceType(	AICivilianStanceType stanceType,
												U32* handleOut)
{
	switch(stanceType){
		#define CASE(x, y) xcase x: *handleOut = mmAnimBitHandles.y
		CASE(MR_AICIV_STANCE_MOVING, moving);
		CASE(MR_AICIV_STANCE_RUNNING, running);
		#undef CASE
		xdefault:{
			return 0;
		}
	}

	return 1;
}

static void mrAICivStanceSetBG(	const MovementRequesterMsg* msg,
								AICivilianMovementBG* bg,
								AICivilianStanceType stanceType)
{
	PERFINFO_AUTO_START_FUNC();

	if(FALSE_THEN_SET_BIT(bg->hasStanceMask, BIT(stanceType))){
		U32 bitHandle;

		if(!mrAICivGetBitHandleFromStanceType(stanceType, &bitHandle)){
			PERFINFO_AUTO_STOP();
			return;
		}

		mrmAnimStanceCreateBG(	msg,
			&bg->stanceHandle[stanceType],
			bitHandle);
	}

	PERFINFO_AUTO_STOP();
}

static void mrAICivStanceClearBG(	const MovementRequesterMsg* msg,
									AICivilianMovementBG* bg,
									AICivilianStanceType stanceType)
{
	if(TRUE_THEN_RESET_BIT(bg->hasStanceMask, BIT(stanceType))){
		U32 bitHandle;

		if(!mrAICivGetBitHandleFromStanceType(stanceType, &bitHandle)){
			return;
		}

		mrmAnimStanceDestroyBG(	msg,
								&bg->stanceHandle[stanceType]);
	}
}

static void mrAICivStanceClearAllBG(const MovementRequesterMsg* msg,
									AICivilianMovementBG* bg)
{
	ARRAY_FOREACH_BEGIN(bg->stanceHandle, i);
	{
		mrAICivStanceClearBG(msg, bg, i);
	}
	ARRAY_FOREACH_END;
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline bool aiCarGetWaypointInformation(AICivilianMovementBG *bg)
{
	if (bg->curWpIdx < eaSize(&bg->eaWaypoints))
	{
		AICivilianWaypoint *wp;
		
		ANALYSIS_ASSUME(bg->eaWaypoints);
		wp = bg->eaWaypoints[bg->curWpIdx];

		bg->bCurWpIsGroundAligned = (wp->bIsLeg && wp->leg->bIsGroundCoplanar);
		if (bg->bCurWpIsGroundAligned)
		{
			copyVec3(wp->leg->normal, bg->vNormal);
		}
		
		bg->bCheckGroundAlignment = true;
		bg->bCurWpIsCurve = false;
		
		// note: curves and stop waypoints are exclusive to each other
		if ((wp->car_move_type >= EAICivCarMove_GENERIC_TURN) && (bg->curWpIdx + 2 < eaSize(&bg->eaWaypoints)))
		{
			// NOTE: we may not be able to assume ground co-planar when doing a curved turn
			// only if the two legs are on the same plane..
			// TODO: check if all waypoints are co-planar

			if(bg->bCurWpIsGroundAligned)
			{
				if(fabs(distanceY(wp->pos, bg->eaWaypoints[bg->curWpIdx+1]->pos)) > 0.5 ||
					fabs(distanceY(wp->pos, bg->eaWaypoints[bg->curWpIdx+2]->pos)) > 0.5)
				{
					bg->bCurWpIsGroundAligned = false;
				}
			}

			// set up the control points
			copyVec3(wp->pos, bg->vCurveWp);
			copyVec3(bg->vCurveWp, bg->avCurveControlPoints[0]);
			wp = bg->eaWaypoints[bg->curWpIdx + 1];
			copyVec3(wp->pos, bg->avCurveControlPoints[1]);
			copyVec3(bg->avCurveControlPoints[1], bg->avCurveControlPoints[2]);
			wp = bg->eaWaypoints[bg->curWpIdx + 2];
			copyVec3(wp->pos, bg->avCurveControlPoints[3]);

			bg->bCurWpIsCurve = true;
			bg->fCurCurveT = CURVE_STEP;
			bg->numWaypointsInCurve = 3;
		}
		else if (wp->bIsUTurn && (bg->curWpIdx + 3 < eaSize(&bg->eaWaypoints)))
		{
			// set up the control points
			copyVec3(wp->pos, bg->vCurveWp);
			copyVec3(bg->vCurveWp, bg->avCurveControlPoints[0]);

			wp = bg->eaWaypoints[bg->curWpIdx + 1];
			copyVec3(wp->pos, bg->avCurveControlPoints[1]);

			wp = bg->eaWaypoints[bg->curWpIdx + 2];
			copyVec3(wp->pos, bg->avCurveControlPoints[2]);

			wp = bg->eaWaypoints[bg->curWpIdx + 3];
			copyVec3(wp->pos, bg->avCurveControlPoints[3]);

			bg->bCurWpIsCurve = true;
			bg->fCurCurveT = CURVE_STEP;
			bg->numWaypointsInCurve = 4;

		}
		
		return true;
	}


	return false;
}


// ------------------------------------------------------------------------------------------------------------------
__forceinline bool aiPedestrianGetWaypointInformation(AICivilianMovementBG *bg)
{
	if (bg->curWpIdx < eaSize(&bg->eaWaypoints))
	{
		AICivilianWaypoint *wp;
		ANALYSIS_ASSUME(bg->eaWaypoints);
		wp = bg->eaWaypoints[bg->curWpIdx];
		
		bg->bCurWpIsGroundAligned = (wp->bIsLeg && wp->leg->bIsGroundCoplanar);
		bg->bCheckGroundAlignment = true;
		if (bg->bCurWpIsGroundAligned)
		{
			copyVec3(wp->leg->normal, bg->vNormal);
		}
		
		return true;
	}

	bg->bCurWpIsGroundAligned = false;
	bg->bCheckGroundAlignment = false;

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static void aiCivPedestrian_DoFindGroundCast( const MovementRequesterMsg *msg, 
															AICivilianMovementBG *bg, 
															const Vec3 vCurPos)
{
	WorldColl* wc;
	WorldCollCollideResults results = {0};
	Vec3 pos_up, pos_down;

	copyVec3(vCurPos, pos_up);
	copyVec3(vCurPos, pos_down);
	pos_up[1] += RAYCAST_UP_DIST;
	pos_down[1] -= RAYCAST_DOWN_DIST;
	if(	mrmGetWorldCollBG(msg, &wc) &&
		wcRayCollide(wc, pos_up, pos_down, WC_QUERY_BITS_AI_CIV, &results))
	{
		if (getAngleBetweenNormalizedUpVec3(results.normalWorld) < RAD(60.f))
		{
			copyVec3(results.normalWorld, bg->vNormal);
		}
				
		bg->fInterpToGroundDist = vCurPos[1] - results.posWorldImpact[1];
	}
	else
	{
		bg->fInterpToGroundDist = 0.f;
		zeroVec3(bg->vNormal);
	}
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static void aiCivPedestrian_FindGround( const MovementRequesterMsg *msg, 
													  AICivilianMovementBG *bg, 
													  const AICivilianWaypoint *wp, 
													  const Vec3 vCurPos, 
													  F32 fCastTimer)
{
	if (! bg->bCurWpIsGroundAligned)
	{
		if(ABS_TIME_SINCE(bg->timeLastRaycast) > SEC_TO_ABS_TIME(fCastTimer))
		{
			bg->timeLastRaycast = ABS_TIME;
			aiCivPedestrian_DoFindGroundCast(msg, bg, vCurPos);	
		}
	}
	else if (bg->bCheckGroundAlignment)
	{
		// we need to check if we are on the leg plane, if not we still need to raycast until we're on it
		// this is the case for places like going down steps
		if(ABS_TIME_SINCE(bg->timeLastRaycast) > SEC_TO_ABS_TIME(fCastTimer))
		{
			Vec3 vFromLegStart;
			F32 fDot;

			bg->timeLastRaycast = ABS_TIME;

			subVec3(vCurPos, wp->leg->start, vFromLegStart);
			fDot = dotVec3(vFromLegStart, bg->vNormal);
			if (ABS(fDot) > 0.1f)
			{
				aiCivPedestrian_DoFindGroundCast(msg, bg, vCurPos);	
			}
			else
			{
				bg->bCheckGroundAlignment = false;
				copyVec3(wp->leg->normal, bg->vNormal);
			}
		}
	}
}




// ------------------------------------------------------------------------------------------------------------------
__forceinline static void aiCivCar_DoFindGroundCast(const MovementRequesterMsg *msg, const Vec3 vCurPos, Vec3 vNormal, F32 *pfGroundDist)
{
	WorldColl* wc;
	WorldCollCollideResults results = {0};
	Vec3 pos_up, pos_down;

	copyVec3(vCurPos, pos_up);
	copyVec3(vCurPos, pos_down);
	pos_up[1] += RAYCAST_UP_DIST;
	pos_down[1] -= RAYCAST_DOWN_DIST;
	if(	mrmGetWorldCollBG(msg, &wc) &&
		wcRayCollide(wc, pos_up, pos_down, WC_QUERY_BITS_AI_CIV, &results))
	{
		if (getAngleBetweenNormalizedUpVec3(results.normalWorld) < RAD(60.f))
		{
			copyVec3(results.normalWorld, vNormal);
		}

		*pfGroundDist = vCurPos[1] - results.posWorldImpact[1];
	}
	else
	{
		*pfGroundDist = 0;
		copyVec3(upvec, vNormal);
	}
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static void aiCivCar_DoFindGroundCastAhead(const MovementRequesterMsg *msg, AICivilianMovementBG *bg, const Vec3 vCurPos)
{
	WorldColl* wc;
	WorldCollCollideResults results = {0};
	Vec3 vPosUp, vPosDown;

	scaleAddVec3(bg->vMoveDir, 6.0f, vCurPos, vPosUp);
	copyVec3(vPosUp, vPosDown);
	vPosUp[1] += RAYCAST_UP_DIST;
	vPosDown[1] -= RAYCAST_DOWN_DIST;

	if(	mrmGetWorldCollBG(msg, &wc) &&
		wcRayCollide(wc, vPosUp, vPosDown, WC_QUERY_BITS_AI_CIV, &results))
	{
		if (getAngleBetweenNormalizedUpVec3(results.normalWorld) < RAD(40.f))
		{
			addVec3(results.normalWorld, bg->vNormal, bg->vNormal);
			normalVec3(bg->vNormal);
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static void aiCivCar_FindGround(const MovementRequesterMsg *msg, AICivilianMovementBG *bg, const AICivilianWaypoint *wp, Vec3 vCurPos, F32 fCastTimer)
{
	if (! bg->bCurWpIsGroundAligned)
	{
		if(ABS_TIME_SINCE(bg->timeLastRaycast) > SEC_TO_ABS_TIME(fCastTimer))
		{
			bg->timeLastRaycast = ABS_TIME;
			aiCivCar_DoFindGroundCast(msg, vCurPos, bg->vNormal, &bg->fInterpToGroundDist);
			aiCivCar_DoFindGroundCastAhead(msg, bg, vCurPos);
		}
	}
	else if (bg->bCheckGroundAlignment)
	{
		// we need to check if we are on the leg plane, if not we still need to raycast until we're on it
		// this is the case for places like going down steps
		if(ABS_TIME_SINCE(bg->timeLastRaycast) > SEC_TO_ABS_TIME(fCastTimer))
		{
			Vec3 vFromLegStart;
			F32 fDot;

			bg->timeLastRaycast = ABS_TIME;

			subVec3(vCurPos, wp->leg->start, vFromLegStart);
			fDot = dotVec3(vFromLegStart, bg->vNormal);
			if (ABS(fDot) > 0.1f)
			{
				aiCivCar_DoFindGroundCast(msg, vCurPos, bg->vNormal, &bg->fInterpToGroundDist);	
				aiCivCar_DoFindGroundCastAhead(msg, bg, vCurPos);
			}
			else
			{
				bg->bCheckGroundAlignment = false;
				copyVec3(wp->leg->normal, bg->vNormal);
				aiCivCar_DoFindGroundCastAhead(msg, bg, vCurPos);
			}
		}
	}
}


__forceinline static void aiPedestrianReachedWaypoint(const MovementRequesterMsg *msg, AICivilianMovementBG *bg, AICivilianMovementToFG *toFG, const Vec3 vCurPos)
{
	// we reached our current waypoint, move onto the next.
	bg->curWpIdx++;

	if (aiPedestrianGetWaypointInformation(bg))
	{
		AICivilianWaypoint *wp = bg->eaWaypoints[bg->curWpIdx];
		subVec3(wp->pos, vCurPos, bg->vMoveDir);
	}

	bg->numWaypointsInCurve = 0;

	//
	toFG->numReachedWp++;
	mrmEnableMsgUpdatedToFG(msg);
}

static void aiCivSetSkippedMovement(const MovementRequesterMsg *msg,
									AICivilianMovementBG *bg,
									S32 set)
{
	if(set){
		if(FALSE_THEN_SET(bg->bSkippedMovement)){
			mrAICivStanceClearBG(msg, bg, MR_AICIV_STANCE_MOVING);
			mrAICivStanceClearBG(msg, bg, MR_AICIV_STANCE_RUNNING);
		}
	}
	else if(TRUE_THEN_RESET(bg->bSkippedMovement)){
		// Might need to re-acquire anim.

		mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
	}
}

static void aiCivSetSpeedNearZero(	const MovementRequesterMsg *msg,
									AICivilianMovementBG *bg,
									S32 set)
{
	if(set){
		if(FALSE_THEN_SET(bg->speedNearZero)){
			mrAICivStanceClearBG(msg, bg, MR_AICIV_STANCE_MOVING);
			mrAICivStanceClearBG(msg, bg, MR_AICIV_STANCE_RUNNING);
		}
	}else{
		bg->speedNearZero = 0;
	}
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivMovementPedestrian_CreateOutput(const MovementRequesterMsg *msg, AICivilianMovementBG *bg, AICivilianMovementToFG *toFG)
{
	switch(msg->in.bg.createOutput.dataClassBit)
	{
		xcase MDC_BIT_POSITION_CHANGE: 
		{
			Vec3 vCurPos;
			Vec3 vel;
			AICivilianWaypoint *wp;
			F32 deltaSpeed;
			
			PERFINFO_AUTO_START("PosDel - Ped", 1);

			if (bg->toBG.pause)
			{
				aiCivSetSkippedMovement(msg, bg, 1);

				PERFINFO_AUTO_STOP();
				return;
			}
				
			if (bg->curWpIdx >= eaSize(&bg->eaWaypoints))
			{	// we reached the end of our path, 
				// see if we were told to face something once we got here
				if (TRUE_THEN_RESET(bg->toBG.bUseFinalRotation))
				{
					F32 yawDiff = subAngle(bg->toBG.fFinalRotation, bg->fCurFacing);
					
					if(IS_NOT_NEAR_ZERO(yawDiff))
					{
						mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_OUTPUT_ROTATION_CHANGE);
						bg->handlesRotationChange = 1;
						setVec3FromYaw(bg->vMoveDir, bg->toBG.fFinalRotation);
					}
				}
				aiCivSetSkippedMovement(msg, bg, 1);

				PERFINFO_AUTO_STOP();
				return;
			}

			ANALYSIS_ASSUME(bg->eaWaypoints);
			devassert(bg->curWpIdx >= 0);
			wp = bg->eaWaypoints[bg->curWpIdx];

			if (wp->bStop)
			{
				aiCivSetSkippedMovement(msg, bg, 1);

				PERFINFO_AUTO_STOP();
				return;
			}

			aiCivSetSkippedMovement(msg, bg, 0);
			mrmGetPositionBG(msg, vCurPos);

			// the time between raycasting depends on how fast they are going
			{
				F32 fRayCastTime;
				if (bg->fCurSpeed <= 5.f) {
					fRayCastTime = 1.0f;
				} else if (bg->fCurSpeed < 8.f) {
					fRayCastTime = 0.7f;
				} else if (bg->fCurFacing < 11.f) {
					fRayCastTime = 0.4f;
				}
				else 
					fRayCastTime = 0.2f;
								
				aiCivPedestrian_FindGround(msg, bg, wp, vCurPos, fRayCastTime);
			}
			
			subVec3(wp->pos, vCurPos, bg->vMoveDir);
			
			if(!bg->handlesRotationChange)
			{
				F32 yaw = getVec3Yaw(bg->vMoveDir);
				F32 yawDiff = subAngle(yaw, bg->fCurFacing);

				if(IS_NOT_NEAR_ZERO(yawDiff))
				{
					mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_OUTPUT_ROTATION_CHANGE);
					bg->handlesRotationChange = 1;
				}
			}

			{
				F32 reachedDistSQR = 1.f;
				F32 distSQR  = lengthVec3SquaredXZ(bg->vMoveDir);
				if (bg->numWaypointsInCurve > 3) // check stuck counter 
					reachedDistSQR = SQR(3.f);

				if(distSQR < reachedDistSQR)
				{
					// we reached our current waypoint, move onto the next.
					aiPedestrianReachedWaypoint(msg, bg, toFG, vCurPos);
				}

			}

			if (! bg->bCurWpIsGroundAligned)
			{
				// if we are not on a ground aligned plane,
				// we need to align the move direction to the ground
				F32 fDot = - dotVec3(bg->vMoveDir, bg->vNormal);
				scaleAddVec3(bg->vNormal, fDot, bg->vMoveDir, bg->vMoveDir);
				if (lengthVec3SquaredXZ(bg->vMoveDir) < 0.25f)
				{
					aiPedestrianReachedWaypoint(msg, bg, toFG, vCurPos);
				}
			}
			
			normalVec3(bg->vMoveDir);
			

			// adjust the pedestrian speed 
			{
				F32 fSpeed;

				if (bg->toBG.bUseOverrideCritterSpeed )
				{
					bg->fCurSpeed = bg->toBG.fCritterSpeedOverride;
				}
				else if(ABS_TIME_SINCE(bg->timeLastSpeedVar) > SEC_TO_ABS_TIME(PEDESTRIAN_SPEED_REEVALUATE_TIME))
				{
					bg->fCurSpeed = bg->toBG.fSpeedMinimum + randomPositiveF32() * bg->toBG.fSpeedRange;
					aiCivSetSpeedNearZero(msg, bg, IS_NEAR_ZERO(bg->fCurSpeed));
					bg->timeLastSpeedVar = ABS_TIME;
				}
				

				if( ABS(bg->fLastFacingDiff) < RAD(45) || bg->fCurSpeed >= PEDESTRIAN_RUN_THRESHOLD)
				{
					fSpeed = bg->fCurSpeed;
					aiCivSetSpeedNearZero(msg, bg, 0);
				}
				else
				{
					fSpeed = 0.f;
					aiCivSetSpeedNearZero(msg, bg, 1);
				}

				deltaSpeed = fSpeed * MM_SECONDS_PER_STEP;
				scaleVec3(bg->vMoveDir, deltaSpeed, vel);
			}


			if (IS_NOT_NEAR_ZERO(bg->fInterpToGroundDist))
			{
				F32 fMoveDist;
				F32 fInterpToGroundDist = ABS(bg->fInterpToGroundDist);

				fMoveDist = (fInterpToGroundDist * 0.5f * 5.f);
				fMoveDist = CLAMP(fMoveDist, 6.f, 12.f);
				fMoveDist *= MM_SECONDS_PER_STEP;

				if (fMoveDist >= fInterpToGroundDist)
				{
					fMoveDist = -bg->fInterpToGroundDist;
				}
				else if (bg->fInterpToGroundDist > 0.0f)
				{
					fMoveDist = -fMoveDist;
				}

				bg->fInterpToGroundDist += fMoveDist;
				scaleAddVec3(upvec, fMoveDist, vel, vel);
			}
			
			
			if (bg->toBG.bDoCollision && wp->bIsLeg && !wp->bIsCrosswalk)
			{
				Vec3 vPotentialPos, vDirColOut;
				addVec3(vCurPos, vel, vPotentialPos);
				zeroVec3(vDirColOut);
				if(	!mrmCheckCollisionWithOthersBG(msg, vPotentialPos, vDirColOut, 0.f, 0) ||
					vec3IsZero(vDirColOut))
				{
					mrmTranslatePositionBG(msg, vel, 0, 0);
				}
				else 
				{
					Vec3 vNewDir, vMoveDir;

					if (dotVec3(vDirColOut, bg->vMoveDir) > 0)
					{
						mrmTranslatePositionBG(msg, vel, 0, 0);

						PERFINFO_AUTO_STOP();
						return;
					}
					
					// move perpendicular to the de-penetration direction
					crossVec3Up(vDirColOut, vNewDir);
					if (dotVec3(vNewDir, bg->vMoveDir) < 0)
					{
						scaleVec3(vNewDir, -1.f, vMoveDir);
					}
					else
					{
						copyVec3(vNewDir, vMoveDir);
					}
					normalVec3(vMoveDir);
					scaleVec3(vMoveDir, deltaSpeed, vel);
					// calculate our new potential position
					addVec3(vPotentialPos, vel, vPotentialPos);
					if (acgPointInLeg(vPotentialPos, wp->leg))
					{	// our position is within our current leg, we're allowed to move here
						
						copyVec3(vMoveDir, bg->vMoveDir);

						// we have to project our movement onto the ground plane
						{
							const F32 *pvGroundNormal;
							F32 fDot;
							if (! bg->bCurWpIsGroundAligned)
							{
								pvGroundNormal = bg->vNormal;
							}
							else
							{
								pvGroundNormal = wp->leg->normal;
							}
			
							fDot = - dotVec3(bg->vMoveDir, pvGroundNormal);
							scaleAddVec3(pvGroundNormal, fDot, bg->vMoveDir, bg->vMoveDir);
							scaleVec3(bg->vMoveDir, deltaSpeed, vel);
						}

						// todo: vars vCurveWp & numWaypointsInCurve are used for cars, 
						//	but I don't want to add another variable. Need to split up AICivilianMovementBG
						//	into separate car/ped structs or unionize it
						if (distance3SquaredXZ(bg->vCurveWp, vCurPos) < SQR(deltaSpeed))
						{	// we haven't moved enough, increment stuck
							bg->numWaypointsInCurve++;
						}
						copyVec3(vCurPos, bg->vCurveWp);

						mrmTranslatePositionBG(msg, vel, 0, 0);

						PERFINFO_AUTO_STOP();
						return;
					}
					else
					{	// we will move off the leg, 
						// keep our original movement, and plow through
						scaleVec3(bg->vMoveDir, deltaSpeed, vel);
						mrmTranslatePositionBG(msg, vel, 0, 0);

						PERFINFO_AUTO_STOP();
						return;
					}
				}
			}
			else
			{
				mrmTranslatePositionBG(msg, vel, 0, 0);
			}
			
			if(gConf.bNewAnimationSystem)
			{
				if(!bg->speedNearZero)
				{
					mrAICivStanceSetBG(msg, bg, MR_AICIV_STANCE_MOVING);

					if(bg->fCurSpeed >= PEDESTRIAN_RUN_THRESHOLD) 
					{
						mrAICivStanceSetBG(msg, bg, MR_AICIV_STANCE_RUNNING);
					}
					else
					{
						mrAICivStanceClearBG(msg, bg, MR_AICIV_STANCE_RUNNING);
					}
				}
			}
			PERFINFO_AUTO_STOP();
		}

		xcase MDC_BIT_ROTATION_CHANGE: {

			Vec2 pyFace;
			Quat rot;
			F32 fYawDiff;

			if(bg->toBG.pause)
				return;

			pyFace[0] = 0;
			pyFace[1] = getVec3Yaw(bg->vMoveDir);

			// pedestrian turning, no damping
			fYawDiff = pyFace[1] - bg->fCurFacing;
			fYawDiff = SIMPLEANGLE(fYawDiff);
			if (IS_NEAR_ZERO(fYawDiff))
			{
				mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_OUTPUT_ROTATION_CHANGE);
				bg->handlesRotationChange = 0;
			}
			else
			{
				F32 fYawDelta = PED_MAX_TURN_RATE * MM_SECONDS_PER_STEP;

				if (fYawDelta > ABS(fYawDiff))
				{
					fYawDelta = fYawDiff;
				}
				else if (fYawDiff < 0.0f)
				{
					fYawDelta = -fYawDelta;
				}
				bg->fCurFacing = bg->fCurFacing + fYawDelta;
				bg->fCurFacing = SIMPLEANGLE(bg->fCurFacing);
				pyFace[1] = bg->fCurFacing;

				yawQuat(-pyFace[1], rot);
				
				mrmSetRotationBG(msg, rot);
				mrmSetFacePitchYawBG(msg, pyFace);
			}
			bg->fLastFacingDiff = fYawDiff;
		}
		xcase MDC_BIT_ANIMATION: {

			if(bg->speedNearZero || bg->bSkippedMovement) 
			{
				mrmAnimAddBitBG(msg, mmAnimBitHandles.idle);
				return;
			}
			
			mrmAnimAddBitBG(msg, mmAnimBitHandles.move);
			mrmAnimAddBitBG(msg, mmAnimBitHandles.forward);
			if(bg->fCurSpeed >= PEDESTRIAN_RUN_THRESHOLD)
				mrmAnimAddBitBG(msg, mmAnimBitHandles.run);
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivMovementCar_CreateOutput(const MovementRequesterMsg *msg, AICivilianMovementBG *bg, AICivilianMovementToFG *toFG)
{

	switch(msg->in.bg.createOutput.dataClassBit)
	{
		xcase MDC_BIT_POSITION_CHANGE: {
			Vec3 vCurPos;
			Vec3 vVelocity;
			F32 fDesiredSpeed = 0.0f;
			Vec3 vToWp;
			F32 fDistToWpSQ;
			AICivilianWaypoint *wp;

			PERFINFO_AUTO_START("PosDel - Car", 1);

			if( bg->toBG.pause
				|| 
				bg->curWpIdx >= eaSize(&bg->eaWaypoints)
				||
				bg->speedMinimumNearZero &&
				bg->speedNearZero)
			{
				bg->timeLastSpeedVar = ABS_TIME;
				bg->fCurDesiredSpeed = bg->toBG.fSpeedMinimum;
				aiCivSetSkippedMovement(msg, bg, 1);

				PERFINFO_AUTO_STOP();
				return;
			}

			ANALYSIS_ASSUME(bg->eaWaypoints);
			wp = bg->eaWaypoints[bg->curWpIdx];

			if (wp->bStop)
			{
				aiCivSetSkippedMovement(msg, bg, 1);

				PERFINFO_AUTO_STOP();
				return;
			}

			aiCivSetSkippedMovement(msg, bg, 0);
			mrmGetPositionBG(msg, vCurPos);
			
			// the time between raycasts depends on how fast we are going
			{
				F32 fRayCastTime;
				if (bg->fCurSpeed < 15.f) {
					fRayCastTime = 0.5f;
				} else if (bg->fCurSpeed < 25.f) {
					fRayCastTime = 0.2f;
				} else {
					fRayCastTime = 0.1f;
				}
				aiCivCar_FindGround(msg, bg, wp, vCurPos, fRayCastTime);
			}
			

			if (! bg->bCurWpIsCurve) subVec3(wp->pos, vCurPos, vToWp);
			else subVec3(bg->vCurveWp, vCurPos, vToWp);

			fDistToWpSQ = lengthVec3SquaredXZ(vToWp); 
			if(fDistToWpSQ < SQR(2.0f))
			{
				// we reached our current waypoint, move onto the next.
				if (bg->bCurWpIsCurve == false)
				{
					bg->curWpIdx++;
					if (aiCarGetWaypointInformation(bg))
					{
						wp = bg->eaWaypoints[bg->curWpIdx];
						if (wp->bStop == false)
						{
							subVec3(wp->pos, vCurPos, vToWp);
							fDesiredSpeed = bg->fCurDesiredSpeed;
						}
					}

					//
					toFG->numReachedWp++;
					mrmEnableMsgUpdatedToFG(msg);
				}
				else
				{
					// we're following a spline
					if (bg->fCurCurveT > 1.0001f)
					{
						// we're done riding the curve
						bg->curWpIdx += bg->numWaypointsInCurve;
						toFG->numReachedWp += bg->numWaypointsInCurve;

						if (aiCarGetWaypointInformation(bg))
						{
							wp = bg->eaWaypoints[bg->curWpIdx];
							if (wp->bStop == false)
							{
								subVec3(wp->pos, vCurPos, vToWp);
								fDesiredSpeed = bg->fCurDesiredSpeed;
							}
						}

						mrmEnableMsgUpdatedToFG(msg);

						fDesiredSpeed = bg->fCurDesiredSpeed;
					}
					else
					{
						// step the next point on the curve
						bezierGetPoint3D(bg->avCurveControlPoints, bg->fCurCurveT, bg->vCurveWp);
						subVec3(bg->vCurveWp, vCurPos, vToWp);
						bg->fCurCurveT += CURVE_STEP;
					}
				}
			}
			else if (! bg->bCurWpIsCurve)
			{
				// we haven't reached our current waypoint
				bool bNextStop = true;
				// check if our next way point is a stop
				if ((bg->curWpIdx + 1) < eaSize(&bg->eaWaypoints))
				{
					AICivilianWaypoint *nextWp = bg->eaWaypoints[(bg->curWpIdx + 1)];
					bNextStop = nextWp->bStop;
				}

				if (bNextStop == false)
				{
					fDesiredSpeed = bg->fCurDesiredSpeed;
				}
				else 
				{
					#define SLOW_DISTANCE	(SQR(20.0f))
					if (fDistToWpSQ < SLOW_DISTANCE)
					{
						fDesiredSpeed = 0.3f + (fDistToWpSQ / SLOW_DISTANCE);
						fDesiredSpeed = MIN(fDesiredSpeed, 1.0f);
						fDesiredSpeed *= bg->fCurDesiredSpeed;
					}
					else
					{
						fDesiredSpeed = bg->fCurDesiredSpeed;
					}
				}
			}
			

			if (bg->bCurWpIsCurve)
			{ 
				if (bg->numWaypointsInCurve == 3)
				{
					if (bg->fCurCurveT > 0.44f && bg->fCurCurveT < 0.56f) {
						fDesiredSpeed = bg->fCurDesiredSpeed * 0.8f;
					} else {
						fDesiredSpeed = bg->fCurDesiredSpeed;
					}

				}
				else
				{
					if (bg->fCurCurveT > 0.19f && bg->fCurCurveT < 0.81f) {
						fDesiredSpeed = bg->fCurDesiredSpeed * 0.5f;
					} else {
						fDesiredSpeed = bg->fCurDesiredSpeed;
					}
				}
			}
			


			if (wp->bStop == false)
			{
				if (! bg->bCurWpIsGroundAligned)
				{
					// if we are not on a ground aligned plane,
					// we need to align the move direction to the ground
					fDistToWpSQ = - dotVec3(vToWp, bg->vNormal);
					scaleAddVec3(bg->vNormal, fDistToWpSQ, vToWp, bg->vMoveDir);
				}
				else
				{
					copyVec3(vToWp, bg->vMoveDir);
				}

				normalVec3(bg->vMoveDir);
			}
			else
			{
				bg->fCurSpeed = 0.0f;
				aiCivSetSpeedNearZero(msg, bg, 1);
			}

			// interpolate to our desired speed
			{
				F32 fSpeedDiff = fDesiredSpeed - bg->fCurSpeed;
				
				if (IS_NOT_NEAR_ZERO(fSpeedDiff))
				{

					F32 fDeltaSpeed;
					
					if (fSpeedDiff > 0.0)
					{
						// speeding up
						
						fDeltaSpeed = s_fCarAcceleration * MM_SECONDS_PER_STEP;
						if (fDeltaSpeed > fSpeedDiff)
							fDeltaSpeed = fSpeedDiff;
					}
					else
					{
						fDeltaSpeed = -s_fCarBrakeSpeed * MM_SECONDS_PER_STEP;
						if (fDeltaSpeed < fSpeedDiff)
							fDeltaSpeed = fSpeedDiff;
					}
					
					bg->fCurSpeed += fDeltaSpeed;
					aiCivSetSpeedNearZero(msg, bg, IS_NEAR_ZERO(bg->fCurSpeed));
				}
			}
			
			// adjust the car's desired speed
			if(ABS_TIME_SINCE(bg->timeLastSpeedVar) > SEC_TO_ABS_TIME(CAR_SPEED_REEVALUATE_TIME))
			{
				bg->fCurDesiredSpeed = bg->toBG.fSpeedMinimum + randomPositiveF32() * bg->toBG.fSpeedRange;
				bg->timeLastSpeedVar = ABS_TIME;
			}

			{
				F32 deltaSpeed = bg->fCurSpeed*MM_SECONDS_PER_STEP;
				scaleVec3(bg->vMoveDir, deltaSpeed, vVelocity);
			}

			if (IS_NOT_NEAR_ZERO(bg->fInterpToGroundDist))
			{
				F32 fMoveDist = 3.0f*MM_SECONDS_PER_STEP;

				if (fMoveDist >= ABS(bg->fInterpToGroundDist))
				{
					fMoveDist = -bg->fInterpToGroundDist;
				}
				else if (bg->fInterpToGroundDist > 0.0f)
				{
					fMoveDist = -fMoveDist;
				}

				bg->fInterpToGroundDist += fMoveDist;
				scaleAddVec3(upvec, fMoveDist, vVelocity, vVelocity);
			}
			
			mrmTranslatePositionBG(msg, vVelocity, 0, 0);

			if(gConf.bNewAnimationSystem){
				mrAICivStanceSetBG(msg, bg, MR_AICIV_STANCE_MOVING);
			}

			PERFINFO_AUTO_STOP();
		}
		xcase MDC_BIT_ROTATION_CHANGE: {
			
			Vec2 pyFace;
			Quat qRot;
			F32 fAngleDiff;
			
			if( bg->toBG.pause
				||
				bg->bSkippedMovement
				||
				bg->speedMinimumNearZero &&
				bg->speedNearZero)
			{
				return;
			}

			pyFace[1] = getVec3Yaw(bg->vMoveDir);
			pyFace[0] = 0.0f;

			// cars turning interpolation with some damping
			fAngleDiff = pyFace[1] - bg->fCurFacing;
			fAngleDiff = SIMPLEANGLE(fAngleDiff);
			if (IS_NOT_NEAR_ZERO(fAngleDiff))
			{
				F32 fTurningNorm;
				F32 fYawDelta;
				
				fTurningNorm = fAngleDiff / s_fCarTurningNormBasis;
				fTurningNorm = ABS(fTurningNorm);
				fYawDelta = fTurningNorm * s_fCarMaxTurnRate * MM_SECONDS_PER_STEP;

				if (fYawDelta > ABS(fAngleDiff))
				{
					fYawDelta = fAngleDiff;
				}
				else if (fAngleDiff < 0.0f)
				{
					fYawDelta = -fYawDelta;
				}
				bg->fCurFacing = bg->fCurFacing + fYawDelta;
				bg->fCurFacing = SIMPLEANGLE(bg->fCurFacing);
				pyFace[1] = bg->fCurFacing;

				mrmSetFacePitchYawBG(msg, pyFace);
			}
			bg->fLastFacingDiff = fAngleDiff;

			if(	distance3Squared(bg->vNormal, bg->vNormalLast) > SQR(0.001f) ||
				distance3Squared(bg->vMoveDir, bg->vMoveDirLast) > SQR(0.001f))
			{
				copyVec3(bg->vMoveDir, bg->vMoveDirLast);
				copyVec3(bg->vNormal, bg->vNormalLast);

				bg->forcedTurnCountdown = 30;
			}
			
			if(bg->forcedTurnCountdown){
				Mat3 mtx;
				
				bg->forcedTurnCountdown--;

				crossVec3(bg->vNormal, bg->vMoveDir, mtx[0]);
				crossVec3(mtx[0], bg->vNormal, mtx[2]);
				copyVec3(bg->vNormal, mtx[1]);
				mat3ToQuat(mtx, qRot);
				
				quatInterp(6.2f * MM_SECONDS_PER_STEP, bg->qLastRot, qRot, qRot);
				copyQuat(qRot, bg->qLastRot);

				mrmSetRotationBG(msg, qRot);
			}
		}
		xcase MDC_BIT_ANIMATION: {

			mrmAnimAddBitBG(msg, mmAnimBitHandles.move);
			mrmAnimAddBitBG(msg, mmAnimBitHandles.forward);
			
			if (bg->fLastFacingDiff <= -ANIM_MIN_TURNING_ANGLE)
			{
				if (bg->fLastFacingDiff < -ANIM_MAX_TURNING_ANGLE)
				{
					mrmAnimAddBitBG(msg, mmAnimBitHandles.moveLeft);
				}
				else if (bg->fLastFacingDiff < -ANIM_MID_TURNING_ANGLE)
				{
					mrmAnimAddBitBG(msg, mmAnimBitHandles.left);
				}
				else
				{
					mrmAnimAddBitBG(msg, mmAnimBitHandles.bankLeft);
				}
			}
			else if (bg->fLastFacingDiff >= ANIM_MIN_TURNING_ANGLE)
			{
				if (bg->fLastFacingDiff > ANIM_MAX_TURNING_ANGLE)
				{
					mrmAnimAddBitBG(msg, mmAnimBitHandles.moveRight);
				}
				else if (bg->fLastFacingDiff > ANIM_MID_TURNING_ANGLE)
				{
					mrmAnimAddBitBG(msg, mmAnimBitHandles.right);
				}
				else
				{
					mrmAnimAddBitBG(msg, mmAnimBitHandles.bankRight);
				}
			}
		}
	}
}

void aiCivMovementTrolley_CreateOutput(const MovementRequesterMsg *msg, AICivilianMovementBG *bg, AICivilianMovementToFG *toFG)
{
	switch(msg->in.bg.createOutput.dataClassBit)
	{
		xcase MDC_BIT_POSITION_CHANGE:
		{
			Vec3 vCurPos;
			Vec3 vVelocity;
			F32 fDesiredSpeed = 0.0f;
			Vec3 vToWp;
			F32 fDistToWpSQ;
			AICivilianWaypoint *wp;

			if( bg->toBG.pause
				|| 
				bg->curWpIdx >= eaSize(&bg->eaWaypoints)
				||
				bg->speedMinimumNearZero &&
				bg->speedNearZero)
			{
				bg->timeLastSpeedVar = ABS_TIME;
				bg->fCurDesiredSpeed = bg->toBG.fSpeedMinimum;
				aiCivSetSkippedMovement(msg, bg, 1);
				return;
			}

			ANALYSIS_ASSUME(bg->eaWaypoints);
			wp = bg->eaWaypoints[bg->curWpIdx];

			if (wp->bStop)
			{
				aiCivSetSkippedMovement(msg, bg, 1);
				return;
			}

			aiCivSetSkippedMovement(msg, bg, 0);
			mrmGetPositionBG(msg, vCurPos);

			aiCivCar_FindGround(msg, bg, wp, vCurPos, 0.3f);
			subVec3(wp->pos, vCurPos, vToWp);
			
			fDistToWpSQ = lengthVec3SquaredXZ(vToWp); 
			if(fDistToWpSQ < SQR(2.0f))
			{
				// we reached our current waypoint, move onto the next.
				//if (bg->bCurWpIsCurve == false)
				{
					bg->curWpIdx++;
					if (aiCarGetWaypointInformation(bg))
					{
						wp = bg->eaWaypoints[bg->curWpIdx];
						if (wp->bStop == false)
						{
							subVec3(wp->pos, vCurPos, vToWp);
							fDesiredSpeed = bg->fCurDesiredSpeed;
						}
					}

					//
					toFG->numReachedWp++;
					mrmEnableMsgUpdatedToFG(msg);
				}
			}
			else 
			{
				// we haven't reached our current waypoint
				bool bNextStop = true;
				// check if our next way point is a stop
				if ((bg->curWpIdx + 1) < eaSize(&bg->eaWaypoints))
				{
					AICivilianWaypoint *nextWp = bg->eaWaypoints[(bg->curWpIdx + 1)];
					bNextStop = nextWp->bStop;
				}

				if (bNextStop == false)
				{
					fDesiredSpeed = bg->fCurDesiredSpeed;
				}
				else 
				{
					#define SLOW_DISTANCE	(SQR(20.0f))
					if (fDistToWpSQ < SLOW_DISTANCE)
					{
						fDesiredSpeed = 0.3f + (fDistToWpSQ / SLOW_DISTANCE);
						fDesiredSpeed = MIN(fDesiredSpeed, 1.0f);
						fDesiredSpeed *= bg->fCurDesiredSpeed;
					}
					else
					{
						fDesiredSpeed = bg->fCurDesiredSpeed;
					}
				}
			}
			
			if (wp->bStop == false)
			{
				if (! bg->bCurWpIsGroundAligned)
				{
					// if we are not on a ground aligned plane,
					// we need to align the move direction to the ground
					fDistToWpSQ = - dotVec3(vToWp, bg->vNormal);
					scaleAddVec3(bg->vNormal, fDistToWpSQ, vToWp, bg->vMoveDir);
				}
				else
				{
					copyVec3(vToWp, bg->vMoveDir);
				}

				normalVec3(bg->vMoveDir);
			}
			else
			{
				bg->fCurSpeed = 0.0f;
				aiCivSetSpeedNearZero(msg, bg, 1);
			}

			// interpolate to our desired speed
			{
				F32 fSpeedDiff = fDesiredSpeed - bg->fCurSpeed;
				
				if (IS_NOT_NEAR_ZERO(fSpeedDiff))
				{
					F32 fDeltaSpeed;
					
					if (fSpeedDiff > 0.0)
					{
						// speeding up
						fDeltaSpeed = s_fTrolleyAccelerationRate * MM_SECONDS_PER_STEP;
						if (fDeltaSpeed > fSpeedDiff)
							fDeltaSpeed = fSpeedDiff;
					}
					else
					{
						fDeltaSpeed = -s_fTrolleyDecelerationRate * MM_SECONDS_PER_STEP;
						if (fDeltaSpeed < fSpeedDiff)
							fDeltaSpeed = fSpeedDiff;
					}
					
					bg->fCurSpeed += fDeltaSpeed;
					aiCivSetSpeedNearZero(msg, bg, IS_NEAR_ZERO(bg->fCurSpeed));
				}
			}
			
			// adjust the trolley's desired speed
			
			if(ABS_TIME_SINCE(bg->timeLastSpeedVar) > SEC_TO_ABS_TIME(CAR_SPEED_REEVALUATE_TIME))
			{
				bg->fCurDesiredSpeed = bg->toBG.fSpeedMinimum + randomPositiveF32() * bg->toBG.fSpeedRange;
				bg->timeLastSpeedVar = ABS_TIME;
			}
			

			{
				F32 deltaSpeed = bg->fCurSpeed * MM_SECONDS_PER_STEP;
				scaleVec3(bg->vMoveDir, deltaSpeed, vVelocity);
			}

			if (IS_NOT_NEAR_ZERO(bg->fInterpToGroundDist))
			{
				F32 fMoveDist = 3.0f*MM_SECONDS_PER_STEP;

				if (fMoveDist >= ABS(bg->fInterpToGroundDist))
				{
					fMoveDist = -bg->fInterpToGroundDist;
				}
				else if (bg->fInterpToGroundDist > 0.0f)
				{
					fMoveDist = -fMoveDist;
				}

				bg->fInterpToGroundDist += fMoveDist;
				scaleAddVec3(upvec, fMoveDist, vVelocity, vVelocity);
			}

			mrmTranslatePositionBG(msg, vVelocity, 0, 0);

			if(gConf.bNewAnimationSystem){
				mrAICivStanceSetBG(msg, bg, MR_AICIV_STANCE_MOVING);
			}
		}
		xcase MDC_BIT_ROTATION_CHANGE:
		{
			Vec2 pyFace;
			Quat qRot;
			F32 fAngleDiff;

			if( bg->toBG.pause
				||
				bg->bSkippedMovement
				||
				bg->speedMinimumNearZero &&
				bg->speedNearZero)
			{
				return;
			}

			pyFace[1] = getVec3Yaw(bg->vMoveDir);
			pyFace[0] = 0.0f;

			// trolley turning interpolation with some damping
			fAngleDiff = subAngle(pyFace[1], bg->fCurFacing);
			if (ABS(fAngleDiff) > HALFPI)
			{
				pyFace[1] += PI;
				pyFace[1] = SIMPLEANGLE(pyFace[1]);
				fAngleDiff = pyFace[1] - bg->fCurFacing;
			}

			fAngleDiff = SIMPLEANGLE(fAngleDiff);
			if (IS_NOT_NEAR_ZERO(fAngleDiff))
			{
				F32 fTurningNorm;
				F32 fYawDelta;
				F32 fTurnRate;

				fTurningNorm = fAngleDiff / s_fTrolleyTurningNormBasis;
				fTurningNorm = ABS(fTurningNorm);
				fTurnRate = s_fTrolleyMaxTurnRate * ((bg->fCurSpeed > 5.f) ? 1.f : (bg->fCurSpeed / 5.f));
				fYawDelta = fTurningNorm * fTurnRate * MM_SECONDS_PER_STEP;

				if (fYawDelta > ABS(fAngleDiff))
				{
					fYawDelta = fAngleDiff;
				}
				else if (fAngleDiff < 0.0f)
				{
					fYawDelta = -fYawDelta;
				}
				bg->fCurFacing = bg->fCurFacing + fYawDelta;
				bg->fCurFacing = SIMPLEANGLE(bg->fCurFacing);
				pyFace[1] = bg->fCurFacing;

				mrmSetFacePitchYawBG(msg, pyFace);
			}
			bg->fLastFacingDiff = fAngleDiff;

			if(	distance3Squared(bg->vNormal, bg->vNormalLast) > SQR(0.001f) ||
				distance3Squared(bg->vMoveDir, bg->vMoveDirLast) > SQR(0.001f))
			{
				copyVec3(bg->vMoveDir, bg->vMoveDirLast);
				copyVec3(bg->vNormal, bg->vNormalLast);

				bg->forcedTurnCountdown = 30;
			}

			if(bg->forcedTurnCountdown){
				Mat3 mtx;

				bg->forcedTurnCountdown--;

				crossVec3(bg->vNormal, bg->vMoveDir, mtx[0]);
				crossVec3(mtx[0], bg->vNormal, mtx[2]);
				copyVec3(bg->vNormal, mtx[1]);
				mat3ToQuat(mtx, qRot);

				quatInterp(6.2f * MM_SECONDS_PER_STEP, bg->qLastRot, qRot, qRot);
				copyQuat(qRot, bg->qLastRot);

				mrmSetRotationBG(msg, qRot);
			}
		}

		xcase MDC_BIT_ANIMATION: {

			mrmAnimAddBitBG(msg, mmAnimBitHandles.move);
			mrmAnimAddBitBG(msg, mmAnimBitHandles.forward);

			if (bg->fLastFacingDiff <= -ANIM_MIN_TURNING_ANGLE)
			{
				if (bg->fLastFacingDiff < -ANIM_MAX_TURNING_ANGLE)
				{
					mrmAnimAddBitBG(msg, mmAnimBitHandles.moveLeft);
				}
				else if (bg->fLastFacingDiff < -ANIM_MID_TURNING_ANGLE)
				{
					mrmAnimAddBitBG(msg, mmAnimBitHandles.left);
				}
				else
				{
					mrmAnimAddBitBG(msg, mmAnimBitHandles.bankLeft);
				}
			}
			else if (bg->fLastFacingDiff >= ANIM_MIN_TURNING_ANGLE)
			{
				if (bg->fLastFacingDiff > ANIM_MAX_TURNING_ANGLE)
				{
					mrmAnimAddBitBG(msg, mmAnimBitHandles.moveRight);
				}
				else if (bg->fLastFacingDiff > ANIM_MID_TURNING_ANGLE)
				{
					mrmAnimAddBitBG(msg, mmAnimBitHandles.right);
				}
				else
				{
					mrmAnimAddBitBG(msg, mmAnimBitHandles.bankRight);
				}
			}
		}

	}

}


void aiCivilianMovementMsgHandler(const MovementRequesterMsg* msg){
	AICivilianMovementFG*			fg;
	AICivilianMovementBG*			bg;
	AICivilianMovementToFG*		toFG;
	AICivilianMovementToBG*		toBG;

	// Just grab the ones you need in the locations that need them
	//MR_MSG_HANDLER_GET_DATA_DEFAULT(msg, AICivilianMovement);

	switch(msg->in.msgType){
		xcase MR_MSG_BG_GET_DEBUG_STRING:{
			char*	buffer = msg->in.bg.getDebugString.buffer;
			U32		bufferLen = msg->in.bg.getDebugString.bufferLen;

			snprintf_s(	buffer,
						bufferLen,
						"");
		}

		xcase MR_MSG_FG_CREATE_TOBG:{
			MR_MSG_HANDLER_GET_DATA_FG(msg, AICivilianMovement, fg);
			MR_MSG_HANDLER_GET_DATA_TOBG(msg, AICivilianMovement, toBG);

			mrmEnableMsgUpdatedToBG(msg);

			StructCopyAll(parse_AICivilianMovementToBG, &fg->toBG, toBG);
			
			// clear all data that should not persist
			fg->toBG.eaAddedWaypoints = NULL;

			if (TRUE_THEN_RESET(fg->toBG.clearWaypointID))
			{
				fg->spcClearedWaypoints = mmGetProcessCountAfterSecondsFG(0.f);
			}
			else
			{
				fg->spcClearedWaypoints = 0;
			}

			// clear all the updated flags
			fg->toBG.bHasAddedWaypoints = false;
			fg->toBG.bUpdatedPause = false;
			fg->toBG.bUpdatedMovementOptions = false;
			fg->toBG.bInitialization = false;
			fg->toBG.bUpdatedFinalRotation = false;
			fg->toBG.bSetCritterSpeed = false;
			fg->toBG.bUseCritterSpeedUpdated = false;
		}

		xcase MR_MSG_FG_UPDATED_TOFG: {
			MR_MSG_HANDLER_GET_DATA_TOFG(msg, AICivilianMovement, toFG);
			MR_MSG_HANDLER_GET_DATA_FG(msg, AICivilianMovement, fg);

			if (toFG->numReachedWp)
			{
				if (!fg->spcClearedWaypoints || fg->spcClearedWaypoints != mmGetProcessCountAfterSecondsFG(0.f))
				{
					fg->toFG.numReachedWp += toFG->numReachedWp;
				}
				else
				{
					fg->toFG.numReachedWp = 0;
				}
			}
									
			if (toFG->releasedWaypointsID > fg->toFG.releasedWaypointsID)
				fg->toFG.releasedWaypointsID = toFG->releasedWaypointsID;

			ZeroStruct(toFG);
		}

		xcase MR_MSG_BG_DATA_RELEASE_REQUESTED:{
			MR_MSG_HANDLER_GET_DATA_BG(msg, AICivilianMovement, bg);

			switch(msg->in.bg.dataReleaseRequested.dataClassBits){
				xcase MDC_BIT_ANIMATION:{
					if(!bg->bSkippedMovement){
						msg->out->bg.dataReleaseRequested.denied = 1;
					}
				}
			}
		}

		xcase MR_MSG_BG_INPUT_EVENT:{
		}

		xcase MR_MSG_BG_INITIALIZE: {
			Vec2 py;

			MR_MSG_HANDLER_GET_DATA_BG(msg, AICivilianMovement, bg);

			mrmHandledMsgsSetBG(msg,
								MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP |
									MR_HANDLED_MSGS_OUTPUT_CHANGE_ALL);

			if(gConf.bNewAnimationSystem){
				mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_OUTPUT_ANIMATION);
			}
									
			bg->handlesRotationChange = 1;

			mrmGetFacePitchYawBG(msg, py);
			bg->fCurFacing = py[1];
			setVec3FromYaw(bg->vMoveDir, bg->fCurFacing);
						
			{
				Quat curRot;
				yawQuat(-bg->fCurFacing, curRot);
				copyQuat(curRot, bg->qLastRot);
				mrmSetRotationBG(msg, curRot);
			}
			
		}

		xcase MR_MSG_BG_UPDATED_TOBG:{
			MR_MSG_HANDLER_GET_DATA_TOBG(msg, AICivilianMovement, toBG);
			MR_MSG_HANDLER_GET_DATA_TOFG(msg, AICivilianMovement, toFG);
			MR_MSG_HANDLER_GET_DATA_BG(msg, AICivilianMovement, bg);

			if (toBG->clearWaypointID)
			{
				bg->curWpIdx = 0;
				eaClear(&bg->eaWaypoints);

				// clear any numReached
				bg->toFG.numReachedWp = 0;
				toFG->numReachedWp = 0;
				
				toFG->releasedWaypointsID = toBG->clearWaypointID;

				// update the FG that we released the waypoints
				mrmEnableMsgUpdatedToFG(msg);
			}
			

			if (TRUE_THEN_RESET(toBG->bHasAddedWaypoints))
			{
				bool bHadWaypoints;
				S32 numWaypoints = 0;

				bHadWaypoints = (eaSize(&bg->eaWaypoints) != 0);
				
				// add all the waypoints
				eaPushEArray(&bg->eaWaypoints, &toBG->eaAddedWaypoints);
				eaDestroy(&toBG->eaAddedWaypoints);

				if (!bHadWaypoints)
				{
					if (bg->toBG.eCivType == EAICivilianType_PERSON)
					{
						aiPedestrianGetWaypointInformation(bg);
					}
					else
					{
						aiCarGetWaypointInformation(bg);
					}	
				}

				numWaypoints = eaSize(&bg->eaWaypoints);
				if(numWaypoints > sWAYPOINTLIST_MAX_SIZE)
				{
					S32 numRemove = numWaypoints - (numWaypoints - bg->curWpIdx);
					devassert(numRemove >= 0);
					if (numRemove <= bg->curWpIdx)
					{
						eaRemoveRange(&bg->eaWaypoints, 0, numRemove);
						bg->curWpIdx -= numRemove;
					}
				}
			}

			if (TRUE_THEN_RESET(toBG->bInitialization))
			{
				bg->toBG.eCivType = toBG->eCivType;
			}

			if (TRUE_THEN_RESET(toBG->bUpdatedPause))
			{
				bg->toBG.pause = toBG->pause;
			}
			
			if (TRUE_THEN_RESET(toBG->bUpdatedMovementOptions))
			{
				// reset the speed change timer so we'll update our speed 
				bg->toBG.fSpeedMinimum = toBG->fSpeedMinimum;
				bg->toBG.fSpeedRange = toBG->fSpeedRange;
				
				bg->speedMinimumNearZero = IS_NEAR_ZERO(bg->toBG.fSpeedMinimum);

				bg->timeLastSpeedVar = ABS_TIME - SEC_TO_ABS_TIME(PEDESTRIAN_SPEED_REEVALUATE_TIME);
			}

			if (TRUE_THEN_RESET(toBG->bUpdatedFinalRotation))
			{
				bg->toBG.bUseFinalRotation = toBG->bUseFinalRotation;
				bg->toBG.fFinalRotation = toBG->fFinalRotation;
			}

			if (TRUE_THEN_RESET(toBG->bSetCritterSpeed))
			{
				bg->toBG.fCritterSpeedOverride = toBG->fCritterSpeedOverride;
			}
			if (TRUE_THEN_RESET(toBG->bUseCritterSpeedUpdated))
			{
				bg->toBG.bUseOverrideCritterSpeed = toBG->bUseOverrideCritterSpeed;
			}
		
			aiCivSetSpeedNearZero(msg, bg, IS_NEAR_ZERO(bg->fCurSpeed));
			bg->toBG.bDoCollision = toBG->bDoCollision;
			if (bg->toBG.bDisable != toBG->bDisable)
			{
				bg->toBG.bDisable = toBG->bDisable;
				if (bg->toBG.bDisable)
				{
					mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
					mrmReleaseAllDataOwnershipBG(msg);
				}
				else
				{
					mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
					
				}
			}
		}

		xcase MR_MSG_BG_QUERY_MAX_SPEED: {
			MR_MSG_HANDLER_GET_DATA_BG(msg, AICivilianMovement, bg);

			msg->out->bg.queryMaxSpeed.maxSpeed = bg->fCurSpeed;
		}

		xcase MR_MSG_BG_DATA_WAS_RELEASED:{
			// 
			MR_MSG_HANDLER_GET_DATA_BG(msg, AICivilianMovement, bg);

			if (msg->in.bg.dataWasReleased.dataClassBits & MDC_BIT_ANIMATION){
				mrAICivStanceClearAllBG(msg, bg);
			}
		}

		xcase MR_MSG_BG_DISCUSS_DATA_OWNERSHIP:{
			MR_MSG_HANDLER_GET_DATA_BG(msg, AICivilianMovement, bg);

			if (!bg->toBG.bDisable){
				U32 bitsToAcquire = bg->bSkippedMovement ?
										MDC_BITS_ALL & ~MDC_BIT_ANIMATION :
										MDC_BITS_ALL;

				if(mrmAcquireDataOwnershipBG(msg, bitsToAcquire, 1, NULL, NULL)){
					mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
				}
			} else {
				mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
			}
		}

		xcase MR_MSG_FG_BEFORE_DESTROY:{
			MR_MSG_HANDLER_GET_DATA_TOBG(msg, AICivilianMovement, toBG);
			MR_MSG_HANDLER_GET_DATA_BG(msg, AICivilianMovement, bg);
			MR_MSG_HANDLER_GET_DATA_FG(msg, AICivilianMovement, fg);
			
			if(fg)
			{
				eaDestroy(&fg->toBG.eaAddedWaypoints);
			}
			if(toBG)
			{
				eaDestroy(&toBG->eaAddedWaypoints);
			}
			if(bg)
			{
				eaDestroy(&bg->toBG.eaAddedWaypoints);
				eaDestroy(&bg->eaWaypoints);
			}
		}

		xcase MR_MSG_BG_CREATE_OUTPUT:{
			MR_MSG_HANDLER_GET_DATA_BG(msg, AICivilianMovement, bg);
			MR_MSG_HANDLER_GET_DATA_TOFG(msg, AICivilianMovement, toFG);

			switch(bg->toBG.eCivType)
			{
				xcase EAICivilianType_PERSON:
					aiCivMovementPedestrian_CreateOutput(msg, bg, toFG);
				xcase EAICivilianType_CAR:
					aiCivMovementCar_CreateOutput(msg, bg, toFG);					
				xcase EAICivilianType_TROLLEY:
					aiCivMovementTrolley_CreateOutput(msg, bg, toFG);
			}
			
		}

		xcase MR_MSG_BG_CREATE_DETAILS:{
		}
	}
}


// ------------------------------------------------------------------------------------------------------------------
void mmAICivilianSendAdditionalWaypoints(MovementRequester *mr, AICivilianWaypoint **eaAdditionalWaypoints)
{
	AICivilianMovementFG *fg = NULL;

	if(eaSize(&eaAdditionalWaypoints) && mrGetFG(mr, aiCivilianMovementMsgHandler, &fg))
	{
		mrEnableMsgCreateToBG(mr);

		eaPushEArray(&fg->toBG.eaAddedWaypoints, &eaAdditionalWaypoints);
		fg->toBG.bHasAddedWaypoints = 1;
	}
}

// ------------------------------------------------------------------------------------------------------------------
U32 mmAICivilianClearWaypoints(MovementRequester *mr)
{
	AICivilianMovementFG *fg = NULL;

	if(mrGetFG(mr, aiCivilianMovementMsgHandler, &fg))
	{
		static U32 s_uClearID = 1;
		mrEnableMsgCreateToBG(mr);

		fg->toBG.clearWaypointID = s_uClearID++;
		if (s_uClearID == 0)
			s_uClearID = 1;

		// if there were any waypoints to be added, clear them
		eaClear(&fg->toBG.eaAddedWaypoints);
		fg->toBG.bHasAddedWaypoints = 0;

		// clear any that the bg said we've reached since 
                // they are now invalid that we've cleared the waypoints
		fg->toFG.numReachedWp = 0;
		return fg->toBG.clearWaypointID;
	}

	return 0;
}

// ------------------------------------------------------------------------------------------------------------------
void mmAICivilianMovementSetDesiredSpeed(MovementRequester *mr, F32 fSpeedMinimum, F32 fSpeedRange)
{
	AICivilianMovementFG *fg = NULL;

	if(mrGetFG(mr, aiCivilianMovementMsgHandler, &fg))
	{
		if(fg->toBG.fSpeedMinimum != fSpeedMinimum || fg->toBG.fSpeedRange != fSpeedRange)
		{
			mrEnableMsgCreateToBG(mr);
			fg->toBG.fSpeedMinimum = fSpeedMinimum;
			fg->toBG.fSpeedRange = fSpeedRange;
			fg->toBG.bUpdatedMovementOptions = 1;
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
void mmAICivilianMovementSetPause(MovementRequester *mr, bool on)
{
	AICivilianMovementFG *fg = NULL;

	if(mrGetFG(mr, aiCivilianMovementMsgHandler, &fg))
	{
		if(fg->toBG.pause!= (U32)!!on)
		{
			mrEnableMsgCreateToBG(mr);
			fg->toBG.pause = !!on;
			fg->toBG.bUpdatedPause = 1;
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
void mmAICivilianInitMovement(MovementRequester *mr, EAICivilianType type)
{
	AICivilianMovementFG *fg = NULL;

	if(mrGetFG(mr, aiCivilianMovementMsgHandler, &fg))
	{
		mrEnableMsgCreateToBG(mr);
			
		fg->toBG.eCivType = type;
		

		fg->toBG.bInitialization = true;
		
	}
}



// ------------------------------------------------------------------------------------------------------------------
S32 mmAICivilianGetAndClearReachedWp(MovementRequester *mr)
{
	AICivilianMovementFG *fg = NULL;

	if(mrGetFG(mr, aiCivilianMovementMsgHandler, &fg))
	{
		S32 reached = fg->toFG.numReachedWp;

		fg->toFG.numReachedWp = 0;
		return reached;
	}

	return 0;
}

// ------------------------------------------------------------------------------------------------------------------
U32 mmAICivilianGetAckReleasedWaypointsID(MovementRequester *mr)
{
	AICivilianMovementFG *fg = NULL;

	if(mrGetFG(mr, aiCivilianMovementMsgHandler, &fg))
	{
		U32 released = fg->toFG.releasedWaypointsID;

		fg->toFG.releasedWaypointsID = 0;
		return released;
	}

	return 0;
}

// ------------------------------------------------------------------------------------------------------------------
void mmAICivilianMovementSetCollision(MovementRequester *mr, bool bEnable)
{
	AICivilianMovementFG *fg = NULL;

	if(mrGetFG(mr, aiCivilianMovementMsgHandler, &fg))
	{
		if(fg->toBG.bDoCollision != (U32)!!bEnable)
		{
			mrEnableMsgCreateToBG(mr);
			fg->toBG.bDoCollision = !!bEnable;
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
void mmAICivilianMovementSetFinalFaceRot(MovementRequester *mr, F32 fFacing)
{
	AICivilianMovementFG *fg = NULL;

	if(mrGetFG(mr, aiCivilianMovementMsgHandler, &fg))
	{
		mrEnableMsgCreateToBG(mr);
		fg->toBG.bUpdatedFinalRotation = true;
		fg->toBG.bUseFinalRotation = true;
		fg->toBG.fFinalRotation = fFacing;
	}
}

// ------------------------------------------------------------------------------------------------------------------
void mmAICivilianMovementSetCritterMoveSpeed(MovementRequester *mr, F32 fSpeed)
{
	AICivilianMovementFG *fg = NULL;

	if(mrGetFG(mr, aiCivilianMovementMsgHandler, &fg))
	{
		if (fg->toBG.fCritterSpeedOverride != fSpeed)
		{
			mrEnableMsgCreateToBG(mr);
			fg->toBG.bSetCritterSpeed = true;
			fg->toBG.fCritterSpeedOverride = fSpeed;
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
void mmAICivilianMovementUseCritterOverrideSpeed(MovementRequester *mr, bool bEnable)
{
	AICivilianMovementFG *fg = NULL;

	if(mrGetFG(mr, aiCivilianMovementMsgHandler, &fg))
	{
		bEnable = !!bEnable;

		if (fg->toBG.bUseOverrideCritterSpeed != (U32)bEnable)
		{
			mrEnableMsgCreateToBG(mr);
			fg->toBG.bUseOverrideCritterSpeed = bEnable;
			fg->toBG.bUseCritterSpeedUpdated = true;
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
void mmAICivilianMovementEnable(MovementRequester *mr, bool bEnable)
{
	AICivilianMovementFG *fg = NULL;

	if(mrGetFG(mr, aiCivilianMovementMsgHandler, &fg))
	{
		bEnable = !bEnable;
		if (fg->toBG.bDisable != (U32)bEnable)
		{
			fg->toBG.bDisable = bEnable;
			mrEnableMsgCreateToBG(mr);
		}
	}
	

}

#include "AutoGen/AICivMovement_c_ast.c"

