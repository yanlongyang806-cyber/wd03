#pragma once
GCC_SYSTEM

/***************************************************************************
*     Copyright (c) 2005-Present, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#if !GAMESERVER && !GAMECLIENT
	#error No Movement code allowed here.
#endif

typedef struct MovementManager		MovementManager;
typedef struct MovementRequester	MovementRequester;
typedef struct MovementRequesterMsg MovementRequesterMsg;
typedef struct SurfaceMovementTurnDef	SurfaceMovementTurnDef;

#define MR_SURFACE_DEFAULT_PLAYER_FRICTION	5.f
#define MR_SURFACE_DEFAULT_CRITTER_FRICTION 8.f
#define MR_SURFACE_DEFAULT_PLAYER_TRACTION	3.f
#define MR_SURFACE_DEFAULT_CRITTER_TRACTION	7.f
#define MR_SURFACE_DEFAULT_GRAVITY			-80.f
#define MR_SURFACE_DEFAULT_JUMPHEIGHT		9.f
#define MR_SURFACE_DEFAULT_SPEED_SLOW		10.f
#define MR_SURFACE_DEFAULT_SPEED_FAST		15.f
#define MR_SURFACE_DEFAULT_PITCH_DIFF_MULT	0.3f

typedef enum MRSurfaceSpeedIndex {
	MR_SURFACE_SPEED_SLOW,
	MR_SURFACE_SPEED_MEDIUM,
	MR_SURFACE_SPEED_FAST,
	MR_SURFACE_SPEED_NATIVE,
	MR_SURFACE_SPEED_COUNT,
} MRSurfaceSpeedIndex;

bool	mrSurfaceCreate(MovementManager* mm,
						MovementRequester** mrOut);

void	mrSurfaceSetDefaults(MovementRequester* mr);

bool	mrSurfaceSpeedPenaltyStart(	MovementRequester* mr,
									U32 id,
									F32 speedPenalty,
									U32 spc);

// use mrSurfaceSpeedPenaltyStop to stop a mrSurfaceSpeedScaleStart
bool	mrSurfaceSpeedScaleStart(	MovementRequester* mr,
									U32 id,
									F32 speedScale,
									U32 spc);

bool	mrSurfaceSpeedPenaltyStop(	MovementRequester* mr,
									U32 id,
									U32 spc);

bool	mrSurfaceDoCameraShake(MovementRequester* mr);

F32		mrSurfaceGetSurfaceImpactSpeed(MovementRequester* mr);

bool	mrSurfaceGetOnGround(MovementRequester* mr);

bool	mrSurfaceSetBackScale(	MovementRequester* mr,
								F32 backScale);

bool	mrSurfaceSetFriction(	MovementRequester* mr,
								F32 friction);

bool	mrSurfaceSetTraction(	MovementRequester* mr,
								F32 traction);

bool	mrSurfaceSetSpeed(	MovementRequester* mr,
							MRSurfaceSpeedIndex index,
							F32 speed);

bool	mrSurfaceSetSpeedRange(	MovementRequester* mr,
								MRSurfaceSpeedIndex index,
								F32 loDirScale,
								F32 hiDirScale);

F32		mrSurfaceGetSpeed(MovementRequester* mr);

bool	mrSurfaceSetGravity(MovementRequester* mr,
							F32 gravity);

bool	mrSurfaceSetJumpGravity(MovementRequester* mr,
								F32 jumpUpGravity,
								F32 jumpDownGravity);

bool	mrSurfaceSetJumpHeight(	MovementRequester* mr,
								F32 jumpHeight);

bool	mrSurfaceGetJumpHeight(	MovementRequester* mr,
								F32* jumpHeightOut);

bool	mrSurfaceSetJumpTraction(	MovementRequester* mr,
									F32 jumpTraction);

bool	mrSurfaceSetJumpSpeed(	MovementRequester* mr,
								F32 jumpSpeed);

bool	mrSurfaceSetVelocity(	MovementRequester* mr,
								const Vec3 vel);

bool	mrSurfaceSetDoJumpTest(	MovementRequester* mr,
								const Vec3 target);

bool	mrSurfaceSetIsStrafing(	MovementRequester* mr,
								bool isStrafing);

// sync version, only needs to be called on server.
bool	mrSurfaceSetIsStrafingOverride(	MovementRequester* mr,
										bool isStrafing, 
										bool enableOverride);

bool	mrSurfaceSetStrafingOverride(	MovementRequester* mr,
										bool isStrafing, 
										U32 spc);

bool	mrSurfaceDisableJump(	MovementRequester* mr,
								bool disableJump, 
								U32 spc);

bool	mrSurfaceSetFlourishData(	MovementRequester *mr,
									bool enabled,
									F32 timer);

bool	mrSurfaceSetInCombat(	MovementRequester *mr,
								bool inCombat);

bool	mrSurfaceSetSpawnedOnGround(	MovementRequester *mr,
										bool spawnedOnGround);

bool	mrSurfaceSetDoCollisionTest(MovementRequester* mr,
									S32 collisionTestFlags,
									const Vec3 loOffset,
									const Vec3 hiOffset,
									F32 radius);

bool	mrSurfaceSetCanStick(	MovementRequester* mr,
								bool enabled);

bool	mrSurfaceSetOrientToSurface(MovementRequester* mr,
									bool enabled);

bool	mrSurfaceSetPhysicsCheating(MovementRequester *mr,
									bool enabled);

bool	mrSurfaceSetEnabled(	MovementRequester* mr, 
								bool enable);

bool	mrSurfaceSetPitchDiffMultiplier(MovementRequester* mr,
										F32 value);

bool	mrSurfaceSetTurnParameters(	MovementRequester* mr, 
									const SurfaceMovementTurnDef *pDef);

bool	mrSurfaceSetTurnRateScale(	MovementRequester* mr, 
									F32 fTurnRateScale, 
									F32 fTurnRateScaleFast);

bool	mrSurfaceResetTurnRateScales(MovementRequester* mr);

void	mrmApplyTraction(	const MovementRequesterMsg* msg,
							Vec3 vVelInOut,
							const F32 tractionLen,
							const Vec3 velTarget);

void	mrmApplyFriction(	const MovementRequesterMsg* msg,
							Vec3 vVelInOut, 
							F32 frictionLen, 
							const Vec3 velTarget);

F32		mrmCalculateTurningStep(F32 fYawDiff, 
								F32 fYawBasis, 
								F32 fYawRate, 
								F32 fMinYawRate, 
								F32 fMaxYawRate);