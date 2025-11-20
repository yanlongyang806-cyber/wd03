/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "EntityMovementTactical.h"

#include "Character.h"
#include "Character_mods.h"
#include "CombatConfig.h"
#include "GameAccountDataCommon.h"
#include "mathutil.h"
#include "EntityLib.h"
#include "EntityMovementManager.h"
#include "WorldColl.h"
#include "WorldLib.h"
#include "EntityMovementFx.h"
#include "AutoGen/EntityMovementTactical_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Physics););

AUTO_RUN_MM_REGISTER_REQUESTER_MSG_HANDLER(	mrTacticalMsgHandler,
											"TacticalMovement",
											Tactical);

static F32 rollStepUpScaleXZ = 1.f;
AUTO_CMD_FLOAT(rollStepUpScaleXZ, mrTacticalRollStepUpScaleXZ);
static S32 rollIsExactDirection;
AUTO_CMD_INT(rollIsExactDirection, mrTacticalRollIsExactDirection);
static F32	s_fRollFaceTurnRate = TWOPI*4.f; // fairly instant
AUTO_CMD_FLOAT(s_fRollFaceTurnRate, mrTacticalRollFaceTurnRate);

#define TACTICAL_ROLL_TIMEOUT 3.0f

typedef enum TacticalRollState
{
	ETacticalRollState_NONE = 0,
	ETacticalRollState_START = 1, 
	ETacticalRollState_PREROLL,
	ETacticalRollState_ACCEL,
	ETacticalRollState_ROLLING,
	ETacticalRollState_DECCEL,
	ETacticalRollState_POSTROLL,
	ETacticalRollState_FINISHED
} TacticalRollState;

#define MM_FRAMES_TO_SPC(a)		((a)*MM_PROCESS_COUNTS_PER_STEP)
#define MM_FRAMES_TO_SEC(a)		((a)/(F32)MM_STEPS_PER_SECOND)
#define MM_SEC_TO_SPC(a)		((a)*MM_PROCESS_COUNTS_PER_SECOND)


AUTO_STRUCT;
typedef struct TacticalOverridesFlags 
{
	U32						disableRoll		: 1;
	U32						disableAim		: 1;
	U32						disableCrouch	: 1;
	U32						disableSprint	: 1;
	U32						disableQueue	: 1;
	U32						isStop			: 1;
	U32						clearAllDisables: 1;
} TacticalOverridesFlags;

AUTO_STRUCT;
typedef struct TacticalOverrides
{
	U32								spc;
	U32								id;
	TacticalOverridesFlags			flags;
} TacticalOverrides;

AUTO_STRUCT;
typedef struct TacticalBGFlags 
{
	U32 					aimButtonIsDown			: 1;
	U32						startAiming				: 1;
	U32 					isAiming				: 1;
	U32						aimWhenAvailable		: 1;
	U32						aimHeldForMinDuration	: 1;

	U32 					crouchButtonIsDown		: 1;
	U32						startCrouching			: 1;
	U32 					isCrouching				: 1;
	U32						crouchWhenAvailable		: 1;
	
	U32						runButtonIsDown			: 1;
	U32						hasRunMaxDuration		: 1;
	U32 					startRunning			: 1;
	U32 					isRunning				: 1;
	U32						runIsDisabled			: 1;
	U32						runFuelNeedsRefill		: 1;
	
	U32 					rollButtonIsDown		: 1;
	U32 					startRolling			: 1;
	U32						waitingToRoll			: 1;
	U32						disableUpdateDirRoll	: 1;
	U32 					isRolling				: 1;
	U32 					startedRolling			: 1;
	U32 					playedRollAnim			: 1;
	U32						startedRollingInAir		: 1;
	U32						hasYawFaceTarget		: 1;
	U32						useToBGYawOverride		: 1;
	U32						didLargeStepUp			: 1;	
	
	U32						onGroundRolling			: 1;
	U32						appliedRollFuelCost		: 1;
	U32						cooldownIsFromRunning	: 1;
	U32						cooldownIsFromAiming	: 1;

	U32						disableRolling			: 1;
	U32						disableAiming			: 1;
	U32						disableCrouch			: 1;
	U32						disableSprinting		: 1;
	U32						disableQueueRoll		: 1;
	U32						disableQueueAim			: 1;
	U32						disableQueueCrouch		: 1;
	U32						disableQueueSprint		: 1;
	U32						powersBeingUsed			: 1;
} TacticalBGFlags;

AUTO_STRUCT;
typedef struct TacticalBG 
{
	Vec3						dirRoll;
	U32							spcAimingStart;
	
	U32							spcRollingStart;
	U32							spcRollStateStart;
	S32							eRollingState;
	F32							fRollAcceleration;
	F32							fRollDeceleration;

	U32							spcRunningStart;
	F32							runFuel;
	U32							spcCooldownStart;
	U32							spcAimCooldownStart;
	F32							yawFaceTarget;
	F32							yawRollToBGOverride;
	F32							distRolledSQR;
	F32							gravityVel;

	TacticalOverrides**			overridesActive;

	U32							spcScheduledRoll;

	// After being disabled, the next time that these actions can be performed.
	U32							spcDisableRollEnd;	
	U32							spcDisableAimEnd;
	U32							spcDisableCrouchEnd;
	U32							spcDisableSprintEnd;
	U32							spcRollWaitingEnd;

	// FX played for sprinting
	U32							mmrFxSprint;

	union 
	{
		TacticalBGFlags 		flagsMutable;
		const TacticalBGFlags	flags;						NO_AST
	};
} TacticalBG;

AUTO_STRUCT;
typedef struct TacticalLocalBGFlags 
{
	U32						runIsDisabledByOverride	: 1;
	U32						hasDoubleTapDirection	: 1;
} TacticalLocalBGFlags;

AUTO_STRUCT;
typedef struct TacticalLocalBG 
{
	U32								movhMaxSpeed;
	U32								hAutoSprint;
	U32								hAimStrafe;
	U32								hAimJumpDisable;
	Vec3							surfaceNormal;
	Vec3							dirRelativeForDoubleTap;
	TacticalOverrides**				overridesQueued;
	F32								fCurrentRollSpeed;

	U32								stanceAiming;
	U32								stanceCrouching;
	U32								stanceSprint;
	U32								spcClearAllOverrides;

	union {
		TacticalLocalBGFlags		flagsMutable;
		const TacticalLocalBGFlags	flags;					NO_AST
	};
} TacticalLocalBG;

AUTO_STRUCT;
typedef struct TacticalToFGFlags 
{
	U32						enabled					: 1;

	U32						runningUpdated			: 1;
	U32						isRunning				: 1;
	U32						runUsesFuel				: 1;
	
	U32						aimingUpdated			: 1;
	U32						isAiming				: 1;
	U32						aimWhenAvailable		: 1;

	U32						crouchUpdated			: 1;
	U32						isCrouching				: 1;
	U32						crouchWhenAvailable		: 1;

	U32						rollingUpdated			: 1;
	U32						isRolling				: 1;
	U32						waitingToRoll			: 1;
	U32						waitingToRollUpdated	: 1;
	
	U32						cooldownUpdated			: 1;
	U32						cooldownIsFromRunning	: 1;
	U32						cooldownIsFromAiming	: 1;
} TacticalToFGFlags;

AUTO_STRUCT;
typedef struct TacticalToFG 
{
	U32						spcRunningStart;
	F32						maxRunDurationSeconds;
	F32						runFuel;
	U32						spcCooldownStart;
	U32						spcAimCooldownStart;
	F32						runCooldownSeconds;
	F32						rollCooldownSeconds;
	F32						aimCooldownSeconds;
	TacticalToFGFlags		flags;
} TacticalToFG;

AUTO_STRUCT;
typedef struct TacticalToBGFlags 
{
	U32						startRunning			: 1;
	U32						stopRunning				: 1;
	U32						startAiming				: 1;
	U32						stopAiming				: 1;
	U32						startCrouching			: 1;
	U32						stopCrouching			: 1;
		
	U32						disableRunning			: 1;
	U32						enableRunning			: 1;
} TacticalToBGFlags;

AUTO_STRUCT;
typedef struct TacticalToBG 
{
	TacticalToBGFlags		flags;
	U32						spcDisableRunning;
	U32						spcEnableRunning;
	U32						spcClearAllOverrides;

	U32						spcStartRoll;
	F32						scheduledRollYaw;

	TacticalOverrides**		overrides;			AST(NAME("Overrides"))
} TacticalToBG;

AUTO_STRUCT;
typedef struct TacticalFGFromBGFlags 
{
	U32						isAiming			: 1;
	U32						aimWhenAvailable	: 1;
	U32						isCrouching			: 1;
	U32						crouchWhenAvailable : 1;
} TacticalFGFromBGFlags;

AUTO_STRUCT;
typedef struct TacticalFGFromBG 
{
	TacticalFGFromBGFlags	flags;
} TacticalFGFromBG;

AUTO_STRUCT;
typedef struct TacticalFG 
{
	TacticalToBG			toBG;
	TacticalFGFromBG		fromBG;
	TacticalOverrides**		overrides;			AST(NAME("Overrides"))
} TacticalFG;

AUTO_STRUCT;
typedef struct TacticalSyncFlags 
{
	U32						runPlusCrouchDoesRoll		: 1;
	U32						doubleTapDoesRoll			: 1;
	U32						runUsesFuel					: 1;
	U32						rollWhileAiming				: 1;
	U32						bRollIgnoresGlobalCooldown	: 1;
	U32						bSprintDisabled				: 1;
	U32						bAimIgnoresGlobalCooldown	: 1;
	U32						bAutoSprint					: 1;
	U32						bSprintToggles				: 1;
	U32						bAimStrafes					: 1;
	U32						bAimDisablesJump			: 1;
	U32						bIgnoreInput				: 1;
} TacticalSyncFlags;

AUTO_STRUCT;
typedef struct TacticalSyncCrouch 
{
	F32 					speed;
} TacticalSyncCrouch;

AUTO_STRUCT;
typedef struct TacticalSyncRun 
{
	F32 					speed;
	F32 					cooldownSeconds;
	F32 					maxDurationSeconds;
	F32						fuelRefillRate;
	F32						fuelRefillDelay;
} TacticalSyncRun;


AUTO_STRUCT;
typedef struct TacticalSync 
{
	F32 						globalCooldownSeconds;
	TacticalRequesterAimDef		aim;
	TacticalSyncCrouch			crouch;
	TacticalSyncRun				run;
	TacticalRequesterRollDef	roll; AST(NAME("Roll"))
	TacticalSyncFlags			flags;
} TacticalSync;

// ------------------------------------------------------------------------------------------------------------------------
static void mrTacticalUpdateCreateDetailsBG(const MovementRequesterMsg* msg,
											TacticalBG* bg)
{
	if(	bg->flags.isRunning ||
		bg->flags.aimWhenAvailable ||
		bg->flags.isCrouching ||
		bg->flags.crouchWhenAvailable)
	{
		mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_CREATE_DETAILS);
	}else{
		mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_CREATE_DETAILS);
	}
}

// ------------------------------------------------------------------------------------------------------------------------
static void mrTacticalEnabledMsgUpdatedToFG(const MovementRequesterMsg* msg,
											TacticalToFG* toFG)
{
	if(FALSE_THEN_SET(toFG->flags.enabled)){
		mrmEnableMsgUpdatedToFG(msg);
	}
}

// ------------------------------------------------------------------------------------------------------------------------
static void mrTacticalSendAimingToFG(	const MovementRequesterMsg* msg,
										TacticalToFG* toFG,
										const TacticalBG* bg)
{
	toFG->flags.isAiming = bg->flags.isAiming;
	toFG->flags.aimWhenAvailable = bg->flags.aimWhenAvailable;
	if(FALSE_THEN_SET(toFG->flags.aimingUpdated)){
		mrTacticalEnabledMsgUpdatedToFG(msg, toFG);
	}
}

// ------------------------------------------------------------------------------------------------------------------------
static S32 mrTacticalAimCooldownHasPassed(	const MovementRequesterMsg* msg,
											const TacticalBG* bg,
											const TacticalSync* sync)
{
	if (!sync->flags.bAimIgnoresGlobalCooldown &&
		sync->globalCooldownSeconds &&
		!mrmProcessCountPlusSecondsHasPassedBG(	msg, 
		bg->spcCooldownStart,
		sync->globalCooldownSeconds))
	{
		return 0;
	}

	if (sync->aim.fAimCooldown &&
		!mrmProcessCountPlusSecondsHasPassedBG(msg, bg->spcAimCooldownStart, sync->aim.fAimCooldown))
	{
		return 0;
	}

	return 1;
}

// ------------------------------------------------------------------------------------------------------------------------
static void mrTacticalSetAimCooldownBG(	const MovementRequesterMsg* msg,
	TacticalBG* bg,
	const TacticalSync* sync,
	TacticalToFG* toFG)
{
	mrmGetProcessCountBG(msg, &bg->spcAimCooldownStart);

	// Set the global cooldown only if aiming does not ignore it
	if (!sync->flags.bAimIgnoresGlobalCooldown)
	{
		bg->spcCooldownStart = bg->spcAimCooldownStart;
		bg->flagsMutable.cooldownIsFromRunning = 0;
		bg->flagsMutable.cooldownIsFromAiming = 1;
	}
	else
	{
		bg->flagsMutable.cooldownIsFromAiming = 0;
	}

	if(FALSE_THEN_SET(toFG->flags.cooldownUpdated))
	{
		mrTacticalEnabledMsgUpdatedToFG(msg, toFG);
	}

	toFG->spcCooldownStart = bg->spcCooldownStart;
	toFG->spcAimCooldownStart = bg->spcAimCooldownStart;
	toFG->aimCooldownSeconds = sync->aim.fAimCooldown;
	toFG->flags.cooldownIsFromRunning = bg->flagsMutable.cooldownIsFromRunning;
	toFG->flags.cooldownIsFromAiming = bg->flagsMutable.cooldownIsFromAiming;
}

// ------------------------------------------------------------------------------------------------------------------------
static void mrTacticalSetIsAimingBG(const MovementRequesterMsg* msg,
									S32 isAiming)
{
	TacticalBG*				bg = msg->in.userStruct.bg;
	TacticalLocalBG*		localBG = msg->in.userStruct.localBG;
	const TacticalSync*		sync = msg->in.userStruct.sync;
	TacticalToFG*			toFG = msg->in.userStruct.toFG;

	
	if(isAiming)
	{
		if (bg->flags.disableAiming || !mrTacticalAimCooldownHasPassed(msg, bg, sync))
			return;

		assert(bg->flags.aimWhenAvailable);

		if(!bg->flags.isAiming)
		{
			F32 fCrouchSpeed = bg->flags.isCrouching ? sync->crouch.speed : 0.0f;

			// Set tactical cooldown for aim
			mrTacticalSetAimCooldownBG(msg, bg, sync, toFG);

			mrmOverrideValueDestroyBG(	msg, &localBG->movhMaxSpeed, "MaxSpeed");

			if(mrmOverrideValueCreateF32BG(	msg, 
											&localBG->movhMaxSpeed,
											"MaxSpeed",
											MAXF(sync->aim.fSpeed, fCrouchSpeed)))
			{
				mrmLog(msg, NULL, "Aiming enabled.");

				bg->flagsMutable.isAiming = 1;
				mrTacticalSendAimingToFG(msg, toFG, bg);

				mrmAnimStanceCreateBG(msg, &localBG->stanceAiming, mmAnimBitHandles.aim);

				if(sync->flags.bAimStrafes)
				{
					mrmOverrideValueCreateS32BG( msg, &localBG->hAimStrafe, "Strafe", 1);
				}
				if(sync->flags.bAimDisablesJump)
				{
					mrmOverrideValueCreateS32BG( msg, &localBG->hAimJumpDisable, "JumpDisable", 1);
				}
			}
			else
			{
				mrmLog(msg, NULL, "Aiming failed to enable.");

				bg->flagsMutable.aimWhenAvailable = 0;
				mrTacticalSendAimingToFG(msg, toFG, bg);
			}

			
		}
	}
	else if(TRUE_THEN_RESET(bg->flagsMutable.isAiming))
	{
		mrmLog(msg, NULL, "Aiming disabled.");

		toFG->flags.isAiming = 0;
		mrTacticalSendAimingToFG(msg, toFG, bg);

		mrmOverrideValueDestroyBG(	msg, &localBG->movhMaxSpeed, "MaxSpeed");

		if (bg->flags.isCrouching){
			mrmOverrideValueCreateF32BG(msg,
										&localBG->movhMaxSpeed,
										"MaxSpeed",
										sync->crouch.speed);
		}

		if(sync->flags.bAimStrafes)
		{
			mrmOverrideValueDestroyBG( msg, &localBG->hAimStrafe, "Strafe");
		}
		if (sync->flags.bAimDisablesJump)
		{
			mrmOverrideValueDestroyBG( msg, &localBG->hAimJumpDisable, "JumpDisable");
		}

		mrmAnimStanceDestroyBG(msg, &localBG->stanceAiming);
	}
}

// ------------------------------------------------------------------------------------------------------------------------
static void mrTacticalSendCrouchingToFG(const MovementRequesterMsg* msg,
										TacticalToFG* toFG,
										const TacticalBG* bg)
{
	toFG->flags.isCrouching = bg->flags.isCrouching;
	toFG->flags.crouchWhenAvailable = bg->flags.crouchWhenAvailable;
	if(FALSE_THEN_SET(toFG->flags.crouchUpdated)){
		mrTacticalEnabledMsgUpdatedToFG(msg, toFG);
	}
}

// ------------------------------------------------------------------------------------------------------------------------
static void mrTacticalSetIsCrouchingBG(const MovementRequesterMsg* msg,
									   S32 isCrouching)
{
	TacticalBG*				bg = msg->in.userStruct.bg;
	TacticalLocalBG*		localBG = msg->in.userStruct.localBG;
	const TacticalSync*		sync = msg->in.userStruct.sync;
	TacticalToFG*			toFG = msg->in.userStruct.toFG;

	if(isCrouching)
	{
		assert(bg->flags.crouchWhenAvailable);

		if(!bg->flags.isCrouching)
		{

			F32 fAimSpeed = bg->flags.isAiming ? sync->aim.fSpeed : 0.0f;
			mrmOverrideValueDestroyBG(	msg,
										&localBG->movhMaxSpeed,
										"MaxSpeed");

			if(mrmOverrideValueCreateF32BG(	msg,
											&localBG->movhMaxSpeed,
											"MaxSpeed",
											MAXF(sync->crouch.speed, fAimSpeed)))
			{
				mrmLog(msg, NULL, "Crouching enabled.");

				bg->flagsMutable.isCrouching = 1;
				mrTacticalSendCrouchingToFG(msg, toFG, bg);

				mrmAnimStanceCreateBG(msg, &localBG->stanceCrouching, mmAnimBitHandles.crouch);
			}
			else
			{
				mrmLog(msg, NULL, "Crouching failed to enable.");

				bg->flagsMutable.crouchWhenAvailable = 0;
				mrTacticalSendCrouchingToFG(msg, toFG, bg);
			}
		}
	}
	else if(TRUE_THEN_RESET(bg->flagsMutable.isCrouching))
	{
		mrmLog(msg, NULL, "Crouching disabled.");

		toFG->flags.isCrouching = 0;
		mrTacticalSendCrouchingToFG(msg, toFG, bg);

		
		mrmOverrideValueDestroyBG(	msg,
									&localBG->movhMaxSpeed,
									"MaxSpeed");

		if (bg->flags.isAiming)
		{
			mrmOverrideValueCreateF32BG(msg,
										&localBG->movhMaxSpeed,
										"MaxSpeed",
										sync->aim.fSpeed);
		}

		mrmAnimStanceDestroyBG(msg, &localBG->stanceCrouching);
	}
}

// ------------------------------------------------------------------------------------------------------------------------
static void mrTacticalSendRunningToFG(	const MovementRequesterMsg* msg,
										TacticalToFG* toFG,
										const TacticalBG* bg,
										const TacticalSync* sync)
{
	toFG->flags.isRunning = bg->flags.isRunning;
	toFG->flags.runUsesFuel = sync->flags.runUsesFuel;
	
	if(FALSE_THEN_SET(toFG->flags.runningUpdated))
	{
		mrTacticalEnabledMsgUpdatedToFG(msg, toFG);
	}

	if(bg->flags.isRunning)
	{
		toFG->spcRunningStart = bg->spcRunningStart;
	}
	
	toFG->maxRunDurationSeconds = sync->run.maxDurationSeconds;
	toFG->runFuel = bg->runFuel;
}

// ------------------------------------------------------------------------------------------------------------------------
static void mrTacticalSetCooldownEnabledBG(	const MovementRequesterMsg* msg,
											TacticalBG* bg,
											const TacticalSync* sync,
											TacticalToFG* toFG,
											S32 enabled,
											S32 cooldownIsFromRunning)
{
	mrmGetProcessCountBG(msg, &bg->spcCooldownStart);

	if(!enabled)
	{
		// Subtract some unreasonably big number, this is just a hack.

		bg->spcCooldownStart -= 1000 * MM_PROCESS_COUNTS_PER_SECOND;
	}

	if(FALSE_THEN_SET(toFG->flags.cooldownUpdated))
	{
		mrTacticalEnabledMsgUpdatedToFG(msg, toFG);
	}

	bg->flagsMutable.cooldownIsFromRunning = !!cooldownIsFromRunning;
	bg->flagsMutable.cooldownIsFromAiming = 0;

	toFG->flags.cooldownIsFromRunning = bg->flags.cooldownIsFromRunning;
	toFG->flags.cooldownIsFromAiming = bg->flags.cooldownIsFromAiming;
	toFG->spcCooldownStart = bg->spcCooldownStart;
	toFG->runCooldownSeconds = sync->run.cooldownSeconds;
	toFG->rollCooldownSeconds = sync->roll.fRollCooldown;	
}

// ------------------------------------------------------------------------------------------------------------------------
static S32 mrTacticalCreateRunSpeedOverrideBG(	const MovementRequesterMsg* msg,
												TacticalLocalBG* localBG,
												const TacticalSync* sync)
{
	mrmOverrideValueDestroyBG(	msg,
								&localBG->movhMaxSpeed,
								"MaxSpeed");

	mrmOverrideValueDestroyBG(	msg,
								&localBG->hAutoSprint,
								"AutoRun");

	return mrmOverrideValueCreateF32BG(	msg,
										&localBG->movhMaxSpeed,
										"MaxSpeed",
										sync->run.speed) &&
			mrmOverrideValueCreateS32BG( msg,
										&localBG->hAutoSprint,
										"AutoRun",
										sync->flags.bAutoSprint);
}

// ------------------------------------------------------------------------------------------------------------------------
static void mrTacticalSetIsRunningBG(	const MovementRequesterMsg* msg,
										S32 isRunning)
{
	TacticalBG*				bg = msg->in.userStruct.bg;
	TacticalLocalBG*		localBG = msg->in.userStruct.localBG;
	const TacticalSync*		sync = msg->in.userStruct.sync;
	TacticalToFG*			toFG = msg->in.userStruct.toFG;

	if(isRunning)
	{
		if(	!bg->flags.runIsDisabled &&
			!localBG->flags.runIsDisabledByOverride &&
			!bg->flags.isRunning)
		{
			if(mrTacticalCreateRunSpeedOverrideBG(msg, localBG, sync))
			{
				mrmLog(msg, NULL, "Running enabled.");
				
				bg->flagsMutable.isRunning = 1;
				bg->flagsMutable.runFuelNeedsRefill = 0;
				mrmGetProcessCountBG(msg, &bg->spcRunningStart);
				mrTacticalSendRunningToFG(msg, toFG, bg, sync);
				mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_CREATE_DETAILS);
				mrTacticalUpdateCreateDetailsBG(msg, bg);

				mrmAnimStanceCreateBG(msg, &localBG->stanceSprint, mmAnimBitHandles.sprint);
			}
		}
	}
	else if(TRUE_THEN_RESET(bg->flagsMutable.isRunning))
	{
		mrmLog(msg, NULL, "Running disabled.");

		mrTacticalSendRunningToFG(msg, toFG, bg, sync);
		
		mrmOverrideValueDestroyBG(	msg,
									&localBG->movhMaxSpeed,
									"MaxSpeed");

		mrmOverrideValueDestroyBG(	msg,
									&localBG->hAutoSprint,
									"AutoRun");

		if(!bg->flags.isRolling)
		{
			mrTacticalSetCooldownEnabledBG(msg, bg, sync, toFG, 1, 1);
		}

		if(bg->flags.aimWhenAvailable)
		{
			mrTacticalSetIsAimingBG(msg, 1);
		}
		if(bg->flags.crouchWhenAvailable)
		{
			mrTacticalSetIsCrouchingBG(msg, 1);
		}

		mrTacticalUpdateCreateDetailsBG(msg, bg);
		
		if(sync->flags.runUsesFuel)
		{
			bg->flagsMutable.runFuelNeedsRefill = 1;
		}

		mrmAnimStanceDestroyBG(msg, &localBG->stanceSprint);

		// Destroy the sprint FX
		if (bg->mmrFxSprint)
		{
			mmrFxDestroyBG(msg, &bg->mmrFxSprint);
		}		
	}
}

// ------------------------------------------------------------------------------------------------------------------------
static S32 mrTacticalCooldownHasPassed(	const MovementRequesterMsg* msg,
										const TacticalBG* bg,
										const TacticalSync* sync, 
										F32 cooldownSeconds,
										bool bObeyGlobalCooldown)
{
	if(	sync->globalCooldownSeconds && bObeyGlobalCooldown &&
		!mrmProcessCountPlusSecondsHasPassedBG(	msg, 
												bg->spcCooldownStart,
												sync->globalCooldownSeconds))
	{
		return 0;
	}
	if(	cooldownSeconds &&
		!mrmProcessCountPlusSecondsHasPassedBG(	msg, 
												bg->spcCooldownStart,
												cooldownSeconds))
	{
		return 0;
	}
	return 1;
}

static void mrTacticalSendWaitingToRollToFG(const MovementRequesterMsg* msg,
											TacticalToFG* toFG,
											const TacticalBG* bg,
											const TacticalSync* sync)
{
	toFG->flags.waitingToRoll = bg->flags.waitingToRoll;
	if(FALSE_THEN_SET(toFG->flags.waitingToRollUpdated)){
		mrTacticalEnabledMsgUpdatedToFG(msg, toFG);
	}
}

// ------------------------------------------------------------------------------------------------------------------------
__forceinline static void mrTacticalGetDoubleTapRollDir(const MovementRequesterMsg* msg,
														const TacticalLocalBG* localBG,
														Vec3 dirRollOut)
{
	F32 yawRoll = addAngle(	mrmGetInputValueF32BG(msg, MIVI_F32_FACE_YAW),
							getVec3Yaw(localBG->dirRelativeForDoubleTap));

	setVec3(dirRollOut, sinf(yawRoll), 0, cosf(yawRoll));
}

// ------------------------------------------------------------------------------------------------------------------------
__forceinline static void mrTacticalGetRollDirectionFromInput(	const MovementRequesterMsg* msg,
																const TacticalBG* bg,
																Vec3 dirRollOut)
{
	Vec3 dirRelative =	{	mrmGetInputValueBitDiffBG(	msg, MIVI_BIT_RIGHT, MIVI_BIT_LEFT),
							0,
							mrmGetInputValueBitDiffBG(	msg, MIVI_BIT_FORWARD, MIVI_BIT_BACKWARD)
						};

	if(!dirRelative[0])
	{
		dirRelative[0] = mrmGetInputValueBitDiffBG(	msg,
													MIVI_BIT_TURN_RIGHT,
													MIVI_BIT_TURN_LEFT);
	}

	if(	!vec3IsZero(dirRelative) ||
		!bg->flags.hasYawFaceTarget)
	{
		F32 yawRoll = addAngle(	mrmGetInputValueF32BG(msg, MIVI_F32_FACE_YAW),
								getVec3Yaw(dirRelative));

		setVec3(dirRollOut, sinf(yawRoll), 0, cosf(yawRoll));
	}
	else
	{
		F32 yawRoll = addAngle(	bg->yawFaceTarget,
								getVec3Yaw(dirRelative));

		setVec3(dirRollOut, sinf(yawRoll), 0, cosf(yawRoll));
	}
}

// ------------------------------------------------------------------------------------------------------------------------
static S32 mrTacticalHasInputDirectionalInputForRollDirection( const MovementRequesterMsg* msg)
{
	Vec3 dirRelative =	{	mrmGetInputValueBitDiffBG(	msg, MIVI_BIT_RIGHT, MIVI_BIT_LEFT),
							0,
							mrmGetInputValueBitDiffBG(	msg, MIVI_BIT_FORWARD, MIVI_BIT_BACKWARD)
						};

	return !vec3IsZero(dirRelative);
}

// ------------------------------------------------------------------------------------------------------------------------
static void mrTacticalSetIsWaitingToRollBG(const MovementRequesterMsg* msg,
										   S32 waitingToRoll)
{
	TacticalBG*				bg = msg->in.userStruct.bg;
	TacticalLocalBG*		localBG = msg->in.userStruct.localBG;
	const TacticalSync*		sync = msg->in.userStruct.sync;
	TacticalToFG*			toFG = msg->in.userStruct.toFG;

	if(waitingToRoll){
		if(!bg->flags.waitingToRoll){
			mrmLog(msg, NULL, "Started waiting to roll.");
			bg->flagsMutable.waitingToRoll = 1;
			bg->flagsMutable.disableUpdateDirRoll = 1;
			mrTacticalGetRollDirectionFromInput(msg, bg, bg->dirRoll);
			mrmGetProcessCountBG(msg, &bg->spcRollWaitingEnd);
			mrTacticalSendWaitingToRollToFG(msg, toFG, bg, sync);
		}
	}
	else if(TRUE_THEN_RESET(bg->flagsMutable.waitingToRoll)){
		mrmLog(msg, NULL, "Stopped waiting to roll.");
		bg->spcRollWaitingEnd = 0;
		mrTacticalSendWaitingToRollToFG(msg, toFG, bg, sync);
	}
}

// ------------------------------------------------------------------------------------------------------------------------
static S32 mrTacticalGetRollDirection(	const MovementRequesterMsg* msg,
										const TacticalBG* bg,
										const TacticalLocalBG* localBG,
										Vec3 dirRollOut)
{
	const MovementRequesterMsgCreateOutputShared* shared = msg->in.bg.createOutput.shared;

	if(bg->flags.useToBGYawOverride)
	{
		setVec3(dirRollOut, sinf(bg->yawRollToBGOverride), 0, cosf(bg->yawRollToBGOverride));
		return 1;
	}	
	else if(mrmIsAttachedToClientBG(msg))
	{
		if(localBG->flags.hasDoubleTapDirection)
		{
			mrTacticalGetDoubleTapRollDir(msg, localBG, dirRollOut);
			return 1;
		}
		else
		{
			mrTacticalGetRollDirectionFromInput(msg, bg, dirRollOut);
			return 1;
		}
	}

	setVec3(dirRollOut, 0.f, 0.f, 0.f);
	return 0;
}

// ------------------------------------------------------------------------------------------------------------------------
static void mrTacticalRoll_SetState(const MovementRequesterMsg* msg, 
									TacticalBG* bg, 
									TacticalLocalBG* localBG, 
									TacticalRollState eRollState)
{
	if (bg->eRollingState != eRollState)
	{
		bg->eRollingState = eRollState;
		mrmGetProcessCountBG(msg, &bg->spcRollStateStart);

		switch(bg->eRollingState)
		{
			xcase ETacticalRollState_PREROLL:
			{
				localBG->fCurrentRollSpeed = 0.f;
								
				bg->spcRollingStart = bg->spcRollStateStart;

				bg->flagsMutable.playedRollAnim = 0;
			}
		}
		
	}
}

// ------------------------------------------------------------------------------------------------------------------------
static void mrTacticalRoll_Start(	const MovementRequesterMsg* msg,
									TacticalBG* bg,
									TacticalLocalBG* localBG,
									TacticalToFG* toFG,
									const TacticalSync* sync)
{
	const MovementRequesterMsgCreateOutputShared* shared = msg->in.bg.createOutput.shared;

	bg->flagsMutable.didLargeStepUp = 0;

	switch(shared->target.pos->targetType)
	{
		xcase MPTT_STOPPED:
		{
			mrmReleaseAllDataOwnershipBG(msg);

			bg->flagsMutable.isRolling = 0;
			bg->flagsMutable.disableUpdateDirRoll = 0;
			bg->gravityVel = 0.f;
			localBG->flagsMutable.hasDoubleTapDirection = 0;

			toFG->flags.isRolling = 0;
			if(FALSE_THEN_SET(toFG->flags.rollingUpdated))
			{
				mrTacticalEnabledMsgUpdatedToFG(msg, toFG);
			}

			mrTacticalSetCooldownEnabledBG(msg, bg, sync, toFG, 1, 0);

			if(bg->flags.aimWhenAvailable)
			{
				mrTacticalSetIsAimingBG(msg, 1);
			}

			if(bg->flags.crouchWhenAvailable)
			{
				mrTacticalSetIsCrouchingBG(msg, 1);
			}
		}
			
		xdefault:
		{
			if (!bg->flags.disableUpdateDirRoll) 
			{
				if(TRUE_THEN_RESET(bg->flagsMutable.useToBGYawOverride))
				{
					setVec3(bg->dirRoll,
							sinf(bg->yawRollToBGOverride),
							0,
							cosf(bg->yawRollToBGOverride));
				}
				else if(TRUE_THEN_RESET(localBG->flagsMutable.hasDoubleTapDirection))
				{
					mrTacticalGetDoubleTapRollDir(msg, localBG, bg->dirRoll);
				}
				else
				{
					mrTacticalGetRollDirectionFromInput(msg, bg, bg->dirRoll);
				}
			}
		}
	}

	if(bg->flags.isRolling)
	{
		mrTacticalRoll_SetState(msg, bg, localBG, ETacticalRollState_PREROLL);
	}
}

// ------------------------------------------------------------------------------------------------------------------------
static void mrTacticalRoll_HandleGravity(const MovementRequesterMsg* msg, TacticalBG* bg)
{
	if (bg->flagsMutable.startedRollingInAir && !bg->flags.onGroundRolling)
	{
		Vec3 velDown = {0, 0.f, 0};

		bg->gravityVel += -80.f * MM_SECONDS_PER_STEP;
		velDown[1] = bg->gravityVel * MM_SECONDS_PER_STEP;

		mrmTranslatePositionBG(msg, velDown, 1, 0);
	}
	else
	{
		bg->gravityVel = 0.f;
	}
}


// ------------------------------------------------------------------------------------------------------------------------
static void mrTacticalRoll_RollUpdate(	const MovementRequesterMsg* msg,
										TacticalBG* bg,
										TacticalLocalBG* localBG,
										const TacticalSync* sync)
{
	// Check if I've finished the moving part of the roll.
	Vec3	vel;
	Vec3	posBefore;
	Vec3	posAfter;
	S32		isOnGround = true;


	F32		fStepDistance = 0.f;
	U32		spcRollStateEnd = 0;
	
	switch (bg->eRollingState)
	{
		xcase ETacticalRollState_ACCEL:
		{
			localBG->fCurrentRollSpeed += bg->fRollAcceleration * MM_SECONDS_PER_STEP;
			if (localBG->fCurrentRollSpeed > sync->roll.fRollSpeed)
				localBG->fCurrentRollSpeed = sync->roll.fRollSpeed;

			fStepDistance = localBG->fCurrentRollSpeed * MM_SECONDS_PER_STEP;

			spcRollStateEnd = bg->spcRollStateStart + MM_FRAMES_TO_SPC(sync->roll.iRollAccelNumberOfFrames);
		}

		xcase ETacticalRollState_ROLLING:
		{
			F32	fRollSeconds = sync->roll.fRollSpeed ? (sync->roll.fRollDistance / sync->roll.fRollSpeed) : 0.f;

			localBG->fCurrentRollSpeed = sync->roll.fRollSpeed;

			fStepDistance = localBG->fCurrentRollSpeed * MM_SECONDS_PER_STEP;
			
			spcRollStateEnd = bg->spcRollStateStart + MM_SEC_TO_SPC(fRollSeconds);
		}

		xcase ETacticalRollState_DECCEL:
		{
			localBG->fCurrentRollSpeed -= bg->fRollDeceleration * MM_SECONDS_PER_STEP;
			if (localBG->fCurrentRollSpeed <= 0.f)
				localBG->fCurrentRollSpeed = 0.f; // roll should be done soon.

			fStepDistance = localBG->fCurrentRollSpeed * MM_SECONDS_PER_STEP;

			spcRollStateEnd = bg->spcRollStateStart + MM_FRAMES_TO_SPC(sync->roll.iRollDecelNumberOfFrames);
		}
	}


	scaleVec3(bg->dirRoll, fStepDistance, vel);

	mrmGetPositionBG(msg, posBefore);
	
	if(!bg->flagsMutable.startedRollingInAir && !mrmCheckGroundAheadBG(msg, posBefore, bg->dirRoll))
	{
		mrTacticalRoll_SetState(msg, bg, localBG, ETacticalRollState_POSTROLL);
		return;
	}

	bg->flagsMutable.onGroundRolling = 0;
	mrmTranslatePositionBG(msg, vel, 1, 0);
	mrmGetPositionBG(msg, posAfter);

	if (bg->flagsMutable.startedRollingInAir)
	{
		// mrmGetOnGroundBG(msg, &isOnGround, NULL);
		if (bg->flags.onGroundRolling)
			bg->flagsMutable.startedRollingInAir = false;
	}


	
	if(sync->roll.bAllowInAir || bg->flags.onGroundRolling)
	{
		// Went up.

		if(	(posAfter[1] - posBefore[1] >= distance3XZ(posAfter, posBefore) * rollStepUpScaleXZ) &&
			bg->flags.didLargeStepUp &&
			localBG->surfaceNormal[1] < 0.5f)
		{
			mrmLog(msg, NULL, "Undoing large Y move with small XZ move.");

			copyVec3(posBefore, posAfter);
			mrmSetPositionBG(msg, posBefore);
		}

		if(bg->flags.onGroundRolling && !bg->flags.didLargeStepUp)
		{
			// Pop over stuff.

			FOR_BEGIN(i, 3);
			{
				if(distance3XZ(posBefore, posAfter) >= lengthVec3XZ(vel) / 2.f){
					break;
				}else{
					Vec3 velRemain = {0};

					mrmLog(msg, NULL, "Attempting a step-up due to small XZ delta.");

					subVec3XZ(posAfter, posBefore, velRemain);
					subVec3XZ(vel, velRemain, velRemain);

					bg->flagsMutable.onGroundRolling = 0;
					mrmTranslatePositionBG(msg, unitmat[1], 1, 0);
					mrmTranslatePositionBG(msg, velRemain, 1, 0);
					mrmGetPositionBG(msg, posAfter);

					bg->flagsMutable.didLargeStepUp = 1;
				}
			}
			FOR_END;
		}

		if(	bg->flags.onGroundRolling &&
			localBG->surfaceNormal[1] < 0.5f)
		{
			// On a steep surface, so push down it, but not lower than we started.

			Vec3 vecDownSlope = {0, -1, 0};

			mrmLog(msg, NULL, "Pushing down the slope.");

			projectVecOntoPlane(vecDownSlope,
				localBG->surfaceNormal,
				vecDownSlope);

			{
				F32 scale = (posBefore[1] - posAfter[1]) / vecDownSlope[1];

				scaleByVec3(vecDownSlope, scale);
			}

			bg->flagsMutable.onGroundRolling = 0;
			mrmTranslatePositionBG(msg, vecDownSlope, 1, 0);
			mrmGetPositionBG(msg, posAfter);
		}
	}

	if(!bg->flagsMutable.startedRollingInAir && !bg->flags.onGroundRolling)
	{
		Vec3 velDown = {0, -3, 0};

		// Move straight down to the ground or back to start.

		mrmTranslatePositionBG(msg, velDown, 1, 0);

		if(!bg->flags.onGroundRolling)
		{
			mrmSetPositionBG(msg, posBefore);
		}
	}
	else
	{
		mrTacticalRoll_HandleGravity(msg, bg);
	}
	

	if(	sync->roll.fRollFuelCost && 
		g_CombatConfig.tactical.roll.fRollNoCostDistPercentThreshold && 
		!bg->flags.appliedRollFuelCost)
	{
		F32 rollNoCostThreshold = sync->roll.fRollDistance * g_CombatConfig.tactical.roll.fRollNoCostDistPercentThreshold;

		bg->distRolledSQR += distance3Squared(posBefore, posAfter);

		if(bg->distRolledSQR >= SQR(rollNoCostThreshold))
		{
			bg->flagsMutable.appliedRollFuelCost = 1;
			bg->runFuel -= sync->roll.fRollFuelCost;
		}
	}

	if(mrmProcessCountHasPassedBG(msg, spcRollStateEnd))
	{
		switch (bg->eRollingState)
		{
			xcase ETacticalRollState_ACCEL:
			{
				mrTacticalRoll_SetState(msg, bg, localBG, ETacticalRollState_ROLLING);
			}

			xcase ETacticalRollState_ROLLING:
			{
				mrTacticalRoll_SetState(msg, bg, localBG, ETacticalRollState_DECCEL);
				if(sync->roll.fRollFuelCost)
				{
					bg->flagsMutable.runFuelNeedsRefill = 1;
				}
			}

			xcase ETacticalRollState_DECCEL:
			{
				mrTacticalRoll_SetState(msg, bg, localBG, ETacticalRollState_POSTROLL);
			}
		}
	}

}

// ------------------------------------------------------------------------------------------------------------------------
static void mrTacticalRoll_PostRollUpdate(	const MovementRequesterMsg* msg,
											TacticalBG* bg,
											TacticalToFG* toFG,
											const TacticalSync* sync)
{
	// Check if I've finished the post-roll hold.
	U32		spcRollStateEnd = bg->spcRollStateStart + MM_SEC_TO_SPC(sync->roll.fRollPostHoldSeconds);

	if(mrmProcessCountHasPassedBG(msg, spcRollStateEnd))
	{
		mrmReleaseAllDataOwnershipBG(msg);

		toFG->flags.isRolling = 0;

		if(FALSE_THEN_SET(toFG->flags.rollingUpdated))
		{
			mrTacticalEnabledMsgUpdatedToFG(msg, toFG);
		}

		mrTacticalSetCooldownEnabledBG(msg, bg, sync, toFG, 1, 0);

		if(bg->flags.aimWhenAvailable)
		{
			mrTacticalSetIsAimingBG(msg, 1);
		}

		if(bg->flags.crouchWhenAvailable)
		{
			mrTacticalSetIsCrouchingBG(msg, 1);
		}

		if(sync->roll.fRollFuelCost)
		{
			mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
		}

		mrTacticalRoll_SetState(msg, bg, NULL, ETacticalRollState_FINISHED);
	}
	else
	{
		mrTacticalRoll_HandleGravity(msg, bg);
	}
}


// ------------------------------------------------------------------------------------------------------------------------
static int mrTacticalRoll_PreRollUpdate(	const MovementRequesterMsg* msg,
											TacticalBG* bg,
											const TacticalSync* sync)
{
	U32 spcPreRollEnd = bg->spcRollingStart + MM_FRAMES_TO_SPC(sync->roll.iRollFrameStart);

	if (mrmProcessCountHasPassedBG(msg, spcPreRollEnd))
	{
		return true;
	}

	return false;
}


// ------------------------------------------------------------------------------------------------------------------------
static void mrTacticalOutputPositionChangeBG(	const MovementRequesterMsg* msg,
												TacticalBG* bg,
												TacticalLocalBG* localBG,
												TacticalToFG* toFG,
												const TacticalSync* sync)
{
	S32 sanity = 0;

	do {

		switch (bg->eRollingState)
		{
			xcase ETacticalRollState_START:
			{
				mrTacticalRoll_Start(msg, bg, localBG, toFG, sync);
				continue;
			}
		
			xcase ETacticalRollState_PREROLL:
			{
				if (sync->roll.iRollFrameStart == 0)
				{	// no delayed start, move onto the next state
					mrTacticalRoll_SetState(msg, bg, localBG, ETacticalRollState_ACCEL);
					continue;
				}
				else if (mrTacticalRoll_PreRollUpdate(msg, bg, sync))
				{
					mrTacticalRoll_SetState(msg, bg, localBG, ETacticalRollState_ACCEL);
					continue;
				}

			} return;

			xcase ETacticalRollState_ACCEL:
			{
				if (sync->roll.iRollAccelNumberOfFrames == 0)
				{	// no acceleration, move onto the next state
					mrTacticalRoll_SetState(msg, bg, localBG, ETacticalRollState_ROLLING);
					continue;
				}

				mrTacticalRoll_RollUpdate(msg, bg, localBG, sync);

			} return;

			xcase ETacticalRollState_ROLLING:
			{
				mrTacticalRoll_RollUpdate(msg, bg, localBG, sync);
				
			} return;

			xcase ETacticalRollState_DECCEL:
			{
				if (sync->roll.iRollDecelNumberOfFrames == 0)
				{
					mrTacticalRoll_SetState(msg, bg, localBG, ETacticalRollState_POSTROLL);
					continue;
				}

				mrTacticalRoll_RollUpdate(msg, bg, localBG, sync);

			} return;

			xcase ETacticalRollState_POSTROLL:
			{
				mrTacticalRoll_PostRollUpdate(msg, bg, toFG, sync);
				
			} return;

			xdefault:
				return;
		}

	} while (++sanity < 3);
}

// ------------------------------------------------------------------------------------------------------------------------
static void mrTacticalOutputRotationChangeBG(	const MovementRequesterMsg* msg,
												TacticalBG* bg,
												const TacticalSync *sync)
{
	Vec3 pyrRoll = {0, getVec3Yaw(bg->dirRoll), 0};
	Quat rot;

	PYRToQuat(pyrRoll, rot);

	mrmSetRotationBG(msg, rot);

	if (sync->roll.bRollFacesInRollDirection)
	{
		Vec3 vPY;
		F32 fYawDiff;
		mrmGetFacePitchYawBG(msg, vPY);

		fYawDiff = subAngle(pyrRoll[1], vPY[1]);

		if (fYawDiff != 0.f)
		{
			F32 fYawDelta = s_fRollFaceTurnRate * MM_SECONDS_PER_STEP;
			if (fYawDelta > ABS(fYawDiff))
			{
				fYawDelta = fYawDiff;
			}
			else if (fYawDiff < 0.f)
			{
				fYawDelta = -fYawDelta;
			}

			vPY[1] = addAngle(vPY[1], fYawDelta);
			mrmSetFacePitchYawBG(msg, vPY);
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------------
__forceinline static int mrTacticalIsSprintDisabled(const MovementRequesterMsg* msg,
													TacticalBG* bg,
													const TacticalSync* sync)
{
	return	bg->flags.disableSprinting || sync->flags.bSprintDisabled ||
		(bg->spcDisableSprintEnd && !mrmProcessCountPlusSecondsHasPassedBG(msg, bg->spcDisableSprintEnd, 0.15f));
}

// ------------------------------------------------------------------------------------------------------------------------
static void mrTacticalInputEventBG(	const MovementRequesterMsg* msg,
									TacticalBG* bg,
									TacticalLocalBG* localBG,
									const TacticalSync* sync)
{
	switch(msg->in.bg.inputEvent.value.mivi)
	{
		xcase MIVI_BIT_AIM:
		{
			bg->flagsMutable.aimButtonIsDown = msg->in.bg.inputEvent.value.bit;

			mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);

			if(msg->in.bg.inputEvent.value.bit)
			{
				if (g_CombatConfig.tactical.roll.bRollImmediateWhenMovingWhenAiming && 
					mrTacticalHasInputDirectionalInputForRollDirection(msg))
				{
					mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
					bg->flagsMutable.startRolling = 1;
				}
				else
				{
					bg->flagsMutable.startAiming = 1;
				}
			}
			else if(sync->flags.rollWhileAiming)
			{
				bg->flagsMutable.startRolling = 0;
			}
		}

		xcase MIVI_BIT_CROUCH:
		{
			if(g_CombatConfig.tactical.aim.bSplitAimAndCrouch)
			{
				bg->flagsMutable.crouchButtonIsDown = msg->in.bg.inputEvent.value.bit;

				mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);

				if(msg->in.bg.inputEvent.value.bit)
				{
					bg->flagsMutable.startCrouching = 1;
				}
			}
		}

		xcase MIVI_BIT_SLOW:
		{
			if (msg->in.bg.inputEvent.value.bit)
			{
				bg->flagsMutable.runButtonIsDown = 0;
				mrTacticalSetIsRunningBG(msg, 0);
			}
		}
		xcase MIVI_BIT_RUN:
		{
			if (sync->flags.bSprintToggles)
			{
				if (msg->in.bg.inputEvent.value.bit && !mrTacticalIsSprintDisabled(msg, bg, sync))
				{
					bg->flagsMutable.runButtonIsDown = !bg->flagsMutable.isRunning;
				}
				else
				{
					break;
				}
			}
			else
			{
				bg->flagsMutable.runButtonIsDown = msg->in.bg.inputEvent.value.bit;
			}
						
			if (bg->flagsMutable.runButtonIsDown)
			{
				mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
				bg->flagsMutable.startRunning = 1;
			}
			else if (sync->flags.bSprintToggles)
			{
				mrTacticalSetIsRunningBG(msg, 0);
			}
		}

		xcase MIVI_BIT_ROLL:
		{
			bg->flagsMutable.rollButtonIsDown = msg->in.bg.inputEvent.value.bit;
			if(msg->in.bg.inputEvent.value.bit)
			{
				mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
				if (!g_CombatConfig.tactical.roll.bRollOnlyWithDirectionInput)
				{
					bg->flagsMutable.startRolling = 1;
				}
			}
		}
		
		xcase MIVI_F32_ROLL_YAW:
		{
			mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
			bg->yawRollToBGOverride = msg->in.bg.inputEvent.value.f32;
			bg->flagsMutable.useToBGYawOverride = true;
			bg->flagsMutable.startRolling = 1;
		}

		xcase MIVI_BIT_TACTICAL:
		{
			if( mrmGetInputValueBitBG(msg, MIVI_BIT_LEFT)
				|| mrmGetInputValueBitBG(msg, MIVI_BIT_RIGHT)
				|| mrmGetInputValueBitBG(msg, MIVI_BIT_BACKWARD))
			{
				setVec3(localBG->dirRelativeForDoubleTap,
						mrmGetInputValueBitDiffBG(msg, MIVI_BIT_RIGHT, MIVI_BIT_LEFT),
						0,
						mrmGetInputValueBitDiffBG(msg, MIVI_BIT_FORWARD, MIVI_BIT_BACKWARD));

				mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
				bg->flagsMutable.startRolling = 1;
			}	
			else if(mrmGetInputValueBitBG(msg, MIVI_BIT_FORWARD))
			{
				mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
				bg->flagsMutable.startRunning = 1;
			}
		}

		xcase MIVI_BIT_LEFT:
		acase MIVI_BIT_RIGHT:
		acase MIVI_BIT_TURN_LEFT:
		acase MIVI_BIT_TURN_RIGHT:
		acase MIVI_BIT_FORWARD:
		acase MIVI_BIT_BACKWARD:
		{
			// Cancel sprinting when we receive forward or backward input in auto sprint mode
			if (sync->flags.bAutoSprint &&
				bg->flags.isRunning &&
				msg->in.bg.inputEvent.value.bit &&
				(msg->in.bg.inputEvent.value.mivi == MIVI_BIT_FORWARD || msg->in.bg.inputEvent.value.mivi == MIVI_BIT_BACKWARD))
			{
				mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
				bg->flagsMutable.runButtonIsDown = 0;
				mrTacticalSetIsRunningBG(msg, 0);
				return;
			}

			if(	sync->flags.rollWhileAiming &&
				bg->flags.isAiming &&
				msg->in.bg.inputEvent.value.bit)
			{
				mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
				bg->flagsMutable.startRolling = 1;
				return;
			}
			

			if(	sync->flags.doubleTapDoesRoll &&
				msg->in.bg.inputEvent.value.flags.isDoubleTap &&
				!bg->flags.isRolling &&
				FALSE_THEN_SET(localBG->flagsMutable.hasDoubleTapDirection))
			{
				setVec3(localBG->dirRelativeForDoubleTap,
						mrmGetInputValueBitDiffBG(msg, MIVI_BIT_RIGHT, MIVI_BIT_LEFT),
						0,
						mrmGetInputValueBitDiffBG(msg, MIVI_BIT_FORWARD, MIVI_BIT_BACKWARD));

				if(!localBG->dirRelativeForDoubleTap[0])
				{
					localBG->dirRelativeForDoubleTap[0] = mrmGetInputValueBitDiffBG(msg, MIVI_BIT_TURN_RIGHT, MIVI_BIT_TURN_LEFT);
				}

				switch(msg->in.bg.inputEvent.value.mivi)
				{
					xcase MIVI_BIT_LEFT:
					acase MIVI_BIT_TURN_LEFT:
					{
						localBG->dirRelativeForDoubleTap[0] = -1.f;

						if(rollIsExactDirection)
						{
							localBG->dirRelativeForDoubleTap[2] = 0.f;
						}
					}

					xcase MIVI_BIT_RIGHT:
					acase MIVI_BIT_TURN_RIGHT:
					{
						localBG->dirRelativeForDoubleTap[0] = 1.f;

						if(rollIsExactDirection)
						{
							localBG->dirRelativeForDoubleTap[2] = 0.f;
						}
					}

					xcase MIVI_BIT_FORWARD:
					{
						localBG->dirRelativeForDoubleTap[2] = 1.f;

						if(rollIsExactDirection)
						{
							localBG->dirRelativeForDoubleTap[0] = 0.f;
						}
					}

					xcase MIVI_BIT_BACKWARD:
					{
						localBG->dirRelativeForDoubleTap[2] = -1.f;

						if(rollIsExactDirection)
						{
							localBG->dirRelativeForDoubleTap[0] = 0.f;
						}
					}
				}
				mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
				bg->flagsMutable.startRolling = 1;
			}

		}
	}

	// Check if running should turn off because no keys are pressed.
	if(	bg->flags.isRunning &&
		!sync->flags.bSprintToggles &&
		!mrmGetInputValueBitBG(msg, MIVI_BIT_RUN) &&
		!mrmGetInputValueBitBG(msg, MIVI_BIT_FORWARD) &&
		!mrmGetInputValueBitBG(msg, MIVI_BIT_BACKWARD) &&
		!mrmGetInputValueBitBG(msg, MIVI_BIT_LEFT) &&
		!mrmGetInputValueBitBG(msg, MIVI_BIT_RIGHT))
	{
		mrTacticalSetIsRunningBG(msg, 0);
	}
}

// ------------------------------------------------------------------------------------------------------------------------
static void mrTacticalUpdatedToBG(	const MovementRequesterMsg* msg,
									TacticalBG* bg,
									TacticalLocalBG* localBG,
									TacticalToBG* toBG)
{
	// Check for running.

	if(TRUE_THEN_RESET(toBG->flags.startRunning)){
		if(!bg->flags.isRunning){
			bg->flagsMutable.runButtonIsDown = 1;
			mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
			bg->flagsMutable.startRunning = 1;
		}
	} 
	else if(TRUE_THEN_RESET(toBG->flags.stopRunning)){
		if(bg->flags.isRunning){
			bg->flagsMutable.runButtonIsDown = 0;
			mrTacticalSetIsRunningBG(msg, 0);
		}
	}

	// Check for aiming.

	if(TRUE_THEN_RESET(toBG->flags.startAiming)){
		if(!bg->flags.isAiming){
			bg->flagsMutable.aimButtonIsDown = 1;
			mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
			bg->flagsMutable.startAiming = 1;
		}
	}
	else if(TRUE_THEN_RESET(toBG->flags.stopAiming)){
		if(bg->flags.isAiming){
			bg->flagsMutable.aimButtonIsDown = 0;
			mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
		}
	}

	// Check for crouching.

	if(TRUE_THEN_RESET(toBG->flags.startCrouching)){
		if(!bg->flags.isCrouching){
			bg->flagsMutable.crouchButtonIsDown = 1;
			mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
			bg->flagsMutable.startCrouching = 1;
		}
	}
	else if(TRUE_THEN_RESET(toBG->flags.stopCrouching)){
		if(bg->flags.isCrouching){
			bg->flagsMutable.crouchButtonIsDown = 0;
			mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
		}
	}

	// Check for rolling.

	if(toBG->spcStartRoll)
	{
		if(!bg->flags.isRolling)
		{
			mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
			bg->flagsMutable.useToBGYawOverride = 1;
			bg->spcScheduledRoll = toBG->spcStartRoll;
			bg->yawRollToBGOverride = toBG->scheduledRollYaw;
		}

		toBG->spcStartRoll = 0;
	}

	// process the queued overrides

	if(toBG->overrides){
		eaPushEArray(&localBG->overridesQueued, &toBG->overrides);
		eaDestroy(&toBG->overrides);
		mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
	} 

	if (toBG->spcClearAllOverrides){
		localBG->spcClearAllOverrides = toBG->spcClearAllOverrides;
		toBG->spcClearAllOverrides = 0;
		mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
	}

}

// ------------------------------------------------------------------------------------------------------------------------
__forceinline static int mrTacticalIsRollDisabled(	const MovementRequesterMsg* msg,
													TacticalBG* bg,
													TacticalSync* sync)
{
	return	bg->flags.disableRolling || sync->roll.bRollDisabled || 
		(bg->spcDisableRollEnd && !mrmProcessCountPlusSecondsHasPassedBG(msg, bg->spcDisableRollEnd, 0.15f));
}


// ------------------------------------------------------------------------------------------------------------------------
__forceinline static int mrTacticalIsAimDisabled(	const MovementRequesterMsg* msg,
													TacticalBG* bg,
													TacticalSync* sync)
{
	return	bg->flags.disableAiming || sync->aim.bAimDisabled ||
		(bg->spcDisableAimEnd && !mrmProcessCountPlusSecondsHasPassedBG(msg, bg->spcDisableAimEnd, 0.f)); // 0.15f));
}

// ------------------------------------------------------------------------------------------------------------------------
__forceinline static int mrTacticalIsCrouchDisabled(const MovementRequesterMsg* msg,
													TacticalBG* bg)
{
	return	bg->flags.disableCrouch
		||
		(bg->spcDisableAimEnd && !mrmProcessCountPlusSecondsHasPassedBG(msg, bg->spcDisableCrouchEnd, 0.15f));
}

// ------------------------------------------------------------------------------------------------------------------------
static bool mrTacticalCanStartRolling(const MovementRequesterMsg* msg,
									  TacticalBG* bg,
									  TacticalSync* sync,
									  bool bCheckIfRollDisabled)
{
	S32 isOnGround = 0;
	if (!bg->flags.isRolling &&
		(bg->flags.cooldownIsFromRunning || 
		 mrTacticalCooldownHasPassed(msg, bg, sync, sync->roll.fRollCooldown, !sync->flags.bRollIgnoresGlobalCooldown)) &&
		 (sync->roll.bAllowInAir || (mrmGetOnGroundBG(msg, &isOnGround, NULL) && isOnGround)) && 
		 (!sync->roll.fRollFuelCost || bg->runFuel >= sync->roll.fRollFuelCost))
	{
		if (!bCheckIfRollDisabled || !mrTacticalIsRollDisabled(msg, bg, sync)) {
			return true;
		}
	}
	return false;
}


static void mrTacticalHandleStartRolling(const MovementRequesterMsg* msg,
										 TacticalBG* bg,
										 TacticalLocalBG* localBG,
										 TacticalSync* sync,
										 TacticalToFG* toFG)
{
	Vec3 dirRoll; 
	Vec3 posCur;
	S32 isOnGround = true;

	bg->spcDisableRollEnd = 0;
	bg->spcRollWaitingEnd = 0;

	if (sync->roll.bAllowInAir)
		mrmGetOnGroundBG(msg, &isOnGround, NULL);

	// Skip roll if it will go off a ledge.

	mrmGetPositionBG(msg, posCur);

	if(	!mrTacticalGetRollDirection(msg, bg, localBG, dirRoll) ||
		(!isOnGround || mrmCheckGroundAheadBG(msg, posCur, dirRoll)))
	{
		bg->flagsMutable.hasYawFaceTarget = 0;
		
		if(mrmAcquireDataOwnershipBG(	msg,
										MDC_BITS_ALL,
										1,
										NULL,
										NULL))
		{
			bg->distRolledSQR = 0.f;
			bg->flagsMutable.appliedRollFuelCost = 0;
			bg->flagsMutable.isRolling = 1;
			bg->flagsMutable.startedRolling = 0;
			bg->eRollingState = ETacticalRollState_START;
			bg->flagsMutable.rollButtonIsDown = false;
			bg->flagsMutable.startedRollingInAir = !isOnGround;

			mrTacticalSetCooldownEnabledBG(msg, bg, sync, toFG, 0, 0);

			toFG->flags.isRolling = 1;
			if(FALSE_THEN_SET(toFG->flags.rollingUpdated)){
				mrTacticalEnabledMsgUpdatedToFG(msg, toFG);
			}

			mrTacticalSetIsRunningBG(msg, 0);
			mrTacticalSetIsAimingBG(msg, 0);
			mrTacticalSetIsCrouchingBG(msg, 0);

			if(	sync->roll.fRollFuelCost &&
				!g_CombatConfig.tactical.roll.fRollNoCostDistPercentThreshold)
			{
				bg->runFuel -= sync->roll.fRollFuelCost;
				bg->flagsMutable.appliedRollFuelCost = 1;
			}
		}
	}
}

static void mrTacticalStartRollingIfPossible(const MovementRequesterMsg* msg,
											 TacticalBG* bg,
											 TacticalLocalBG* localBG,
											 TacticalSync* sync,
											 TacticalToFG* toFG)
{
	if (!bg->flagsMutable.waitingToRoll && mrTacticalCanStartRolling(msg, bg, sync, false))
	{
		if (mrTacticalIsRollDisabled(msg, bg, sync))
		{
			if (g_CombatConfig.tactical.roll.bQueueTacticalRolls && !bg->flags.disableQueueRoll)
			{
				mrTacticalSetIsWaitingToRollBG(msg, true);
			}
		}
		else
		{
			mrTacticalHandleStartRolling(msg, bg, localBG, sync, toFG);
		}
	}
	if(!bg->flags.isRolling)
	{
		localBG->flagsMutable.hasDoubleTapDirection = 0;

		mrTacticalSetIsRunningBG(msg, 0);

		if(bg->flags.aimWhenAvailable){
			mrTacticalSetIsAimingBG(msg, 1);
		}
		if(bg->flags.crouchWhenAvailable){
			mrTacticalSetIsCrouchingBG(msg, 1);
		}
	}
}


// ------------------------------------------------------------------------------------------------------------------------
static void mrTacticalDiscussData_HandleQueuedDisables(	const MovementRequesterMsg* msg,
														TacticalBG* bg,
														TacticalLocalBG* localBG,
														TacticalSync* sync,
														S32 *doneDiscussing)
{
	S32	rebuildActives = 0;

	if (localBG->spcClearAllOverrides && 
		mrmProcessCountHasPassedBG(msg, localBG->spcClearAllOverrides)){

			rebuildActives = 1;
			eaDestroyStruct(&bg->overridesActive, parse_TacticalOverrides);

			EARRAY_CONST_FOREACH_BEGIN(localBG->overridesQueued, i, isize);
			{
				TacticalOverrides* toNew = localBG->overridesQueued[i];
				if (toNew->spc <= localBG->spcClearAllOverrides){
					StructDestroySafe(parse_TacticalOverrides, &toNew);
					eaRemove(&localBG->overridesQueued, i);
					i--;
					isize--;
				}
			}
			FOR_EACH_END

				localBG->spcClearAllOverrides = 0;
	}

	EARRAY_CONST_FOREACH_BEGIN(localBG->overridesQueued, i, isize);
	{
		TacticalOverrides* toNew = localBG->overridesQueued[i];

		if(!mrmProcessCountHasPassedBG(msg, toNew->spc)){
			continue;
		}

		eaRemove(&localBG->overridesQueued, i);
		i--;
		isize--;

		if(toNew->flags.isStop){
			//find the override in our active list and remove it 
			EARRAY_CONST_FOREACH_BEGIN(bg->overridesActive, j, jsize);
			{
				TacticalOverrides* to = bg->overridesActive[j];

				if(to->id == toNew->id){
					rebuildActives = 1;

					eaRemove(&bg->overridesActive, j);
					StructDestroySafe(parse_TacticalOverrides, &to);
					StructDestroySafe(parse_TacticalOverrides, &toNew);
					break;
				}
			}
			EARRAY_FOREACH_END;

			if(toNew){
				// Didn't find in active list, maybe in the queue because spcs are off.

				EARRAY_CONST_FOREACH_BEGIN(localBG->overridesQueued, j, jsize);
				{
					TacticalOverrides* to = localBG->overridesQueued[j];

					if(to->id == toNew->id){
						// Found the same actID in the list, but we got the stop first, so
						//   remove and ignore it.

						if(j <= i){
							i--;
						}
						isize--;
						jsize--;
						eaRemove(&localBG->overridesQueued, j);

						j--;
						StructDestroySafe(parse_TacticalOverrides, &to);
					}
				}
				EARRAY_FOREACH_END;

				StructDestroySafe(parse_TacticalOverrides, &toNew);
			}

		}else{
			S32 foundMatchingID = false; 

			// check to see if we already have an active with the same ID, if we do- ignore it
			EARRAY_CONST_FOREACH_BEGIN(bg->overridesActive, j, jsize);
			{
				TacticalOverrides* to = bg->overridesActive[j];
				if(to->id == toNew->id){
					foundMatchingID = true;
					break;
				}
			}
			EARRAY_FOREACH_END;

			if (!foundMatchingID){
				rebuildActives = true;
				eaPush(&bg->overridesActive, toNew);
			}
		}

	}
	EARRAY_FOREACH_END;

	if(eaSize(&localBG->overridesQueued)){
		*doneDiscussing = 0;
	}

	bg->flagsMutable.powersBeingUsed = !!eaSize(&bg->overridesActive);

	if(rebuildActives)
	{
		bg->flagsMutable.disableQueueRoll = 0;
		bg->flagsMutable.disableQueueSprint = 0;
		bg->flagsMutable.disableQueueCrouch = 0;
		bg->flagsMutable.disableQueueAim = 0;

		if (eaSize(&bg->overridesActive))
		{
			bool bDisableRolling = false;
			bool bDisableAiming = false;
			bool bDisableSprinting = false;
			bool bDisableCrouching = false;

			FOR_EACH_IN_EARRAY(bg->overridesActive, TacticalOverrides, to)
			{
				bDisableRolling		|= to->flags.disableRoll;
				bDisableAiming		|= to->flags.disableAim;
				bDisableSprinting	|= to->flags.disableSprint;
				bDisableCrouching	|= to->flags.disableCrouch;
				bg->flagsMutable.disableQueueRoll	|= (to->flags.disableRoll && to->flags.disableQueue);
				bg->flagsMutable.disableQueueAim	|= (to->flags.disableAim && to->flags.disableQueue);
				bg->flagsMutable.disableQueueSprint |= (to->flags.disableSprint && to->flags.disableQueue);
				bg->flagsMutable.disableQueueCrouch |= (to->flags.disableCrouch && to->flags.disableQueue);
			}
			FOR_EACH_END

			// rolling
			if (bg->flags.disableRolling != (U32)bDisableRolling)
			{
				if (bDisableRolling)
				{
					bg->flagsMutable.disableRolling = 1;
				}
				else
				{
					bg->flagsMutable.disableRolling = 0;
					mrmGetProcessCountBG(msg, &bg->spcDisableRollEnd);
				}
			}

			// aiming
			if (bg->flags.disableAiming != (U32)bDisableAiming)
			{
				if (bDisableAiming)
				{
					bg->flagsMutable.disableAiming = 1;
					mrTacticalSetIsAimingBG(msg, 0);
					bg->flagsMutable.aimWhenAvailable = 0;
					bg->flagsMutable.aimButtonIsDown = 0;
				}
				else
				{
					bg->flagsMutable.disableAiming = 0;
					mrmGetProcessCountBG(msg, &bg->spcDisableAimEnd);
				}
			}

			// Crouching
			if (bg->flags.disableCrouch != (U32)bDisableCrouching)
			{
				if (bDisableCrouching)
				{
					bg->flagsMutable.disableCrouch = 1;
					mrTacticalSetIsCrouchingBG(msg, 0);
				}
				else
				{
					bg->flagsMutable.disableCrouch = 0;
					mrmGetProcessCountBG(msg, &bg->spcDisableCrouchEnd);

					if(bg->flags.crouchButtonIsDown)
					{
						bg->flagsMutable.crouchWhenAvailable = 0;
					}
				}
			}

			// Sprint.
			if (bg->flags.disableSprinting != (U32)bDisableSprinting)
			{
				if (bDisableSprinting)
				{
					if (g_CombatConfig.tactical.sprint.bSprintToggles && 
						sync->flags.bAutoSprint)
					{
						// This prevents the character continue running
						// after the sprint is enabled again
						bg->flagsMutable.runButtonIsDown = 0;
					}
					bg->flagsMutable.disableSprinting = 1;
					// stop any running we're doing right now. 
					mrTacticalSetIsRunningBG(msg, 0);
				}
				else
				{
					bg->flagsMutable.disableSprinting = 0;
					mrmGetProcessCountBG(msg, &bg->spcDisableSprintEnd);
				}
			}
		}
		else
		{
			if(bg->flags.disableAiming)
			{
				bg->flagsMutable.disableAiming = 0;
				mrmGetProcessCountBG(msg, &bg->spcDisableAimEnd);

				if(bg->flags.aimButtonIsDown)
				{
					bg->flagsMutable.aimWhenAvailable = 0;
				}
			}

			if(bg->flags.disableCrouch)
			{
				bg->flagsMutable.disableCrouch = 0;
				mrmGetProcessCountBG(msg, &bg->spcDisableCrouchEnd);

				if(bg->flags.crouchButtonIsDown)
				{
					bg->flagsMutable.crouchWhenAvailable = 0;
				}
			}

			if(bg->flags.disableRolling)
			{
				bg->flagsMutable.disableRolling = 0;
				mrmGetProcessCountBG(msg, &bg->spcDisableRollEnd);
			}

			if(bg->flags.disableSprinting)
			{
				bg->flagsMutable.disableSprinting = 0;
				mrmGetProcessCountBG(msg, &bg->spcDisableSprintEnd);
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------------
static void mrTacticalDiscussDataOwnershipBG(	const MovementRequesterMsg* msg,
												TacticalBG* bg,
												TacticalLocalBG* localBG,
												TacticalSync* sync,
												TacticalToFG* toFG)
{
	S32 doneDiscussing = 1;
	
	if(localBG->spcClearAllOverrides || eaSize(&localBG->overridesQueued))
	{
		mrTacticalDiscussData_HandleQueuedDisables(msg, bg, localBG, sync, &doneDiscussing);
	}

	// Check for starting aiming.

	if (!mrTacticalIsAimDisabled(msg, bg, sync))
	{
		bg->spcDisableAimEnd = 0;

		if(	TRUE_THEN_RESET(bg->flagsMutable.startAiming) ||
			bg->flags.aimButtonIsDown)
		{
			if(FALSE_THEN_SET(bg->flagsMutable.aimWhenAvailable))
			{
				bg->flagsMutable.aimHeldForMinDuration = 0;

				mrTacticalSendAimingToFG(msg, toFG, bg);

				mrTacticalUpdateCreateDetailsBG(msg, bg);

				mrmGetProcessCountBG(msg, &bg->spcAimingStart);

				if(!bg->flags.isRolling)
				{
					if(	bg->flags.isRunning &&
						!g_CombatConfig.tactical.aim.bSplitAimAndCrouch &&
						sync->flags.runPlusCrouchDoesRoll)
					{
						bg->flagsMutable.startRolling = 1;
					}
					else
					{
						mrTacticalSetIsAimingBG(msg, 1);
					}
				}
			}
		}
	}
	else if(!sync->aim.bAimDisabled &&
			!bg->flags.disableAiming &&
			bg->flags.aimButtonIsDown)
	{
		// Holding down aim, waiting for the disable aim cooldown to expire.

		doneDiscussing = 0;
	}

	// Check for starting crouch.

	if(!mrTacticalIsCrouchDisabled(msg, bg))
	{
		bg->spcDisableCrouchEnd = 0;

		if(	TRUE_THEN_RESET(bg->flagsMutable.startCrouching) ||
			bg->flags.crouchButtonIsDown)
		{
			if(FALSE_THEN_SET(bg->flagsMutable.crouchWhenAvailable)){

				mrTacticalSendCrouchingToFG(msg, toFG, bg);

				mrTacticalUpdateCreateDetailsBG(msg, bg);

				if(!bg->flags.isRolling)
				{
					if(	bg->flags.isRunning &&
						sync->flags.runPlusCrouchDoesRoll)
					{
						bg->flagsMutable.startRolling = 1;
					}
				}
			}
		}
	}
	else if (!bg->flags.disableCrouch &&
			  bg->flags.crouchButtonIsDown)
	{
		doneDiscussing = 0;
	}

	if (bg->flags.crouchWhenAvailable)
	{
		if (!bg->flags.crouchButtonIsDown) 
		{
			bg->flagsMutable.crouchWhenAvailable = 0;
			mrTacticalSendCrouchingToFG(msg, toFG, bg);
			mrTacticalSetIsCrouchingBG(msg, 0);
			mrTacticalUpdateCreateDetailsBG(msg, bg);
		} 
		else 
		{
			S32 bOnGround = false;
			S32 bCanCrouch = true;
			// Jumping/falling always cancels crouch
			if (mrmGetOnGroundBG(msg, &bOnGround, NULL) && !bOnGround) 
			{
				if (bg->flags.isCrouching) 
				{
					mrTacticalSetIsCrouchingBG(msg, 0);
				}
				bCanCrouch = false;
			} 
			else if (g_CombatConfig.tactical.aim.bMovementCancelsCrouch) 
			{
				Vec3 vVel;
				if (mrmGetVelocityBG(msg, vVel)) 
				{
					//Cancel crouch if the entity moves horizontally
					if (!nearSameF32(vVel[0], 0.0f) ||
						!nearSameF32(vVel[2], 0.0f))
					{
						if (bg->flags.isCrouching) 
						{
							mrTacticalSetIsCrouchingBG(msg, 0);
						}
						bCanCrouch = false;
					}
				}
			}
			if (bCanCrouch && 
				!bg->flagsMutable.isCrouching &&
				!bg->flagsMutable.startRolling &&
				!bg->flags.isRolling)
			{
				mrTacticalSetIsCrouchingBG(msg, 1);
			}
			doneDiscussing = 0;
		}
	}
	
	// Check for starting rolling.
	if (bg->flags.rollButtonIsDown && g_CombatConfig.tactical.roll.bRollOnlyWithDirectionInput)
	{
		if (mrTacticalHasInputDirectionalInputForRollDirection(msg))
		{
			bg->flagsMutable.startRolling = true;
		}
		else
		{
			doneDiscussing = 0;
		}
	}

	if(TRUE_THEN_RESET(bg->flagsMutable.startRolling) || 
		(bg->spcScheduledRoll && mrmProcessCountHasPassedBG(msg, bg->spcScheduledRoll)))
	{
		mrTacticalStartRollingIfPossible(msg, bg, localBG, sync, toFG);

		if (bg->flags.waitingToRoll || (bg->flags.rollButtonIsDown && g_CombatConfig.tactical.roll.bRollOnlyWithDirectionInput)) 
		{	
			// we could not r11oll due to some restriction, the roll button is held and we care about it
			doneDiscussing = 0;
		}
		
		bg->spcScheduledRoll = 0;
	} 
	else if (bg->flags.waitingToRoll) 
	{
		if (mrTacticalCanStartRolling(msg, bg, sync, true)) 
		{
			mrTacticalSetIsWaitingToRollBG(msg, false);
			mrTacticalHandleStartRolling(msg, bg, localBG, sync, toFG);
		} 
		else 
		{
			if (mrmProcessCountPlusSecondsHasPassedBG(msg, bg->spcRollWaitingEnd, TACTICAL_ROLL_TIMEOUT))
			{
				bg->flagsMutable.disableUpdateDirRoll = 0;
				mrTacticalSetIsWaitingToRollBG(msg, false);
			} 
			else 
			{
				doneDiscussing = 0;
			}
		}
	} 
	else if (bg->spcScheduledRoll)
		doneDiscussing = 0;

	
	// Check for starting running.

	if(!mrTacticalIsSprintDisabled(msg, bg, sync) &&
		!bg->flags.isRolling &&
		mrTacticalCooldownHasPassed(msg, bg, sync, sync->run.cooldownSeconds, true))
	{
		if (TRUE_THEN_RESET(bg->flagsMutable.startRunning) ||
			bg->flags.runButtonIsDown) 
		{
			bg->spcDisableAimEnd = 0;
			bg->spcDisableCrouchEnd = 0;

			if(	!sync->flags.runUsesFuel ||
				bg->runFuel > 0.f)
			{
				mrTacticalSetIsAimingBG(msg, 0);
				mrTacticalSetIsCrouchingBG(msg, 0);
				mrTacticalSetIsRunningBG(msg, 1);
			}
		}
	}
	else if (!sync->flags.bSprintDisabled &&
			 !bg->flags.disableSprinting &&
			 bg->flags.runButtonIsDown) 
	{
		doneDiscussing = 0;
	}
	
	// Check for stopping running due to max duration passed or fuel expired.

	if(bg->flags.hasRunMaxDuration)
	{
		if(sync->flags.runUsesFuel)
		{
			if(bg->flags.isRunning)
			{
				if(bg->runFuel <= 0)
				{
					mrTacticalSetIsRunningBG(msg, 0);
				}
				else
				{
					bg->runFuel -= MM_SECONDS_PER_STEP;
					
					if(bg->runFuel <= 0.f)
					{
						bg->runFuel = 0.f;
					}
				}

				doneDiscussing = 0;
			}

			if(bg->flags.runFuelNeedsRefill)
			{
				if(!mrmProcessCountPlusSecondsHasPassedBG(	msg,
															bg->spcCooldownStart,
															sync->run.fuelRefillDelay))
				{
					doneDiscussing = 0;
				}
				else
				{
					bg->runFuel += sync->run.fuelRefillRate * MM_SECONDS_PER_STEP;
					
					if(bg->runFuel < sync->run.maxDurationSeconds)
					{
						doneDiscussing = 0;
					}
					else
					{
						bg->runFuel = sync->run.maxDurationSeconds;
						bg->flagsMutable.runFuelNeedsRefill = 0;
					}

					mrTacticalSendRunningToFG(msg, toFG, bg, sync);
				}
			}
		}
		else
		{
			if(bg->flags.isRunning)
			{
				if(mrmProcessCountPlusSecondsHasPassedBG(	msg,
															bg->spcRunningStart,
															sync->run.maxDurationSeconds))
				{
					mrTacticalSetIsRunningBG(msg, 0);
				}
				else
				{
					doneDiscussing = 0;
				}
			}
		}
	} 
	else 
	{
		doneDiscussing = 0;
	}
	
	// Check for stopping aiming due to min duration passed and button not down.

	if(	bg->flags.aimWhenAvailable &&
		!bg->flags.aimButtonIsDown)
	{
		if(mrmProcessCountPlusSecondsHasPassedBG(	msg,
													bg->spcAimingStart,
													sync->aim.fAimMinDurationSeconds))
		{
			bg->flagsMutable.aimWhenAvailable = 0;
			mrTacticalSendAimingToFG(msg, toFG, bg);
			
			mrTacticalSetIsAimingBG(msg, 0);

			mrTacticalUpdateCreateDetailsBG(msg, bg);
		}
		else
		{
			doneDiscussing = 0;
		}
	}

	// If nothing needs to keep running this.
	
	if(doneDiscussing){
		mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
	}
}

void mrTacticalMsgHandler(const MovementRequesterMsg* msg){
	TacticalFG*			fg;
	TacticalBG*			bg;
	TacticalLocalBG*	localBG;
	TacticalToFG*		toFG;
	TacticalToBG*		toBG;
	TacticalSync*		sync;

	MR_MSG_HANDLER_GET_DATA_DEFAULT(msg, Tactical);

	switch(msg->in.msgType){
		xcase MR_MSG_BG_GET_DEBUG_STRING:{
			#define FLAG(x) bg->flags.x ? #x", " : ""
			snprintf_s(	msg->in.bg.getDebugString.buffer,
						msg->in.bg.getDebugString.bufferLen,
						"Roll: start s%d dir(%1.3f, %1.3f, %1.3f)\n"
						"  surfaceNormal(%1.3f, %1.3f, %1.3f)\n"
						"Run: start s%u fuel %1.2f\n"
						"Cooldown: start s%u\n"
						"Flags: %s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s"
						,
						bg->spcRollingStart,
						vecParamsXYZ(bg->dirRoll),
						vecParamsXYZ(localBG->surfaceNormal),
						bg->spcRunningStart,
						bg->runFuel,
						bg->spcCooldownStart,
						FLAG(startAiming),
						FLAG(isAiming),
						FLAG(aimButtonIsDown),
						FLAG(aimWhenAvailable),
						FLAG(startCrouching),
						FLAG(isCrouching),
						FLAG(crouchButtonIsDown),
						FLAG(crouchWhenAvailable),
						FLAG(startRolling),
						FLAG(isRolling),
						FLAG(startedRolling),
						FLAG(isRunning),
						FLAG(onGroundRolling),
						FLAG(runFuelNeedsRefill),
						FLAG(disableRolling),
						FLAG(disableAiming),
						FLAG(disableCrouch),
						FLAG(disableSprinting)
						);
			#undef FLAG

			if(eaSize(&localBG->overridesQueued)){
				strcat_s(	msg->in.bg.getDebugString.buffer,
							msg->in.bg.getDebugString.bufferLen,
							"\nQueued Overrides:\n");

				EARRAY_CONST_FOREACH_BEGIN(localBG->overridesQueued, i, isize);
				{
					const TacticalOverrides* o = localBG->overridesQueued[i];

					strcatf_s(	msg->in.bg.getDebugString.buffer,
								msg->in.bg.getDebugString.bufferLen,
								"%u: s%u %s: %s%s%s%s%s",
								o->id,
								o->spc,
								o->flags.isStop ? "STOP" : "START",
								o->flags.disableAim ? "aim," : "",
								o->flags.disableCrouch ? "crouch," : "",
								o->flags.disableRoll ? "roll," : "",
								o->flags.disableSprint ? "sprint," : "",
								i == isize - 1 ? "" : "\n");
				}
				EARRAY_FOREACH_END;
			}

			if(eaSize(&bg->overridesActive)){
				strcat_s(	msg->in.bg.getDebugString.buffer,
							msg->in.bg.getDebugString.bufferLen,
							"\nActive Overrides:\n");

				EARRAY_CONST_FOREACH_BEGIN(bg->overridesActive, i, isize);
				{
					const TacticalOverrides* o = bg->overridesActive[i];

					strcatf_s(	msg->in.bg.getDebugString.buffer,
								msg->in.bg.getDebugString.bufferLen,
								"%u: s%u %s: %s%s%s%s%s",
								o->id,
								o->spc,
								o->flags.isStop ? "STOP" : "START",
								o->flags.disableAim ? "aim," : "",
								o->flags.disableCrouch ? "crouch," : "",
								o->flags.disableRoll ? "roll," : "",
								o->flags.disableSprint ? "sprint," : "",
								i == isize - 1 ? "" : "\n");
				}
				EARRAY_FOREACH_END;
			}
		}
		
		xcase MR_MSG_GET_SYNC_DEBUG_STRING:{
			snprintf_s(	msg->in.getSyncDebugString.buffer,
						msg->in.getSyncDebugString.bufferLen,
						"Global cooldown %1.2f\n"
						"Roll speed %1.3f, distance %1.3f, cooldown %1.2f, postHoldSeconds %1.2f\n"
						"Run speed %1.3f, maxDuration %1.2f, cooldown %1.2f,%s\n"
						"Aim speed %1.3f, minDuration %1.2f\n"
						"Crouch speed %1.3f"
						,
						sync->globalCooldownSeconds, 
						sync->roll.fRollSpeed,
						sync->roll.fRollDistance,
						sync->roll.fRollPostHoldSeconds,
						sync->roll.fRollCooldown,
						sync->run.speed,
						sync->run.maxDurationSeconds,
						sync->run.cooldownSeconds,
						sync->flags.runUsesFuel ? ", usesFuel" : "",
						sync->aim.fSpeed,
						sync->aim.fAimMinDurationSeconds,
						sync->crouch.speed);
		}
		
		xcase MR_MSG_FG_UPDATED_TOFG:{
			Entity* e;
			
			ASSERT_TRUE_AND_RESET(toFG->flags.enabled);
			
			if(toFG->flags.aimingUpdated)
			{
				fg->fromBG.flags.isAiming = toFG->flags.isAiming;
				fg->fromBG.flags.aimWhenAvailable = toFG->flags.aimWhenAvailable;
			}

			if(toFG->flags.crouchUpdated)
			{
				fg->fromBG.flags.isCrouching = toFG->flags.isCrouching;
				fg->fromBG.flags.crouchWhenAvailable = toFG->flags.crouchWhenAvailable;
			}

			if(mrmGetManagerUserPointerFG(msg, &e))
			{
				bool bTacticalUpdated = false;
				bool bDidRoll = false;

				if(toFG->flags.aimingUpdated)
				{
					if(!e->pChar->bIsAiming != !toFG->flags.isAiming)
					{
						e->pChar->bIsAiming = toFG->flags.isAiming;
						bTacticalUpdated = 1;
					}
					if(!g_CombatConfig.tactical.aim.bSplitAimAndCrouch && !e->pChar->bIsCrouching != !toFG->flags.isAiming)
					{
						e->pChar->bIsCrouching = toFG->flags.isAiming;
						bTacticalUpdated = 1;
					}
				}
				if(toFG->flags.crouchUpdated)
				{
					if(g_CombatConfig.tactical.aim.bSplitAimAndCrouch && !e->pChar->bIsCrouching != !toFG->flags.isCrouching)
					{
						e->pChar->bIsCrouching = toFG->flags.isCrouching;
						bTacticalUpdated = 1;
					}
				}
				
				if(toFG->flags.rollingUpdated)
				{
					e->mm.isRolling = toFG->flags.isRolling;

					if(!e->pChar->bIsRolling != !toFG->flags.isRolling)
					{
						e->pChar->bIsRolling = toFG->flags.isRolling;
						bDidRoll = toFG->flags.isRolling;
						bTacticalUpdated = 1;
					}
				}

				if(toFG->flags.waitingToRollUpdated){
					if(!e->pChar->bIsWaitingToRoll != !toFG->flags.waitingToRoll){
						e->pChar->bIsWaitingToRoll = toFG->flags.waitingToRoll;
						e->pChar->fRollWaitingTimer = 0.0f;
						bTacticalUpdated = 1;
					}
				}

				if(bTacticalUpdated){
					GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
					
					character_RefreshTactical(e->pChar);
					character_RefreshPassives(entGetPartitionIdx(e),e->pChar,pExtract);

					if (bDidRoll)
					{
						character_PayTacticalRollCost(e->pChar);
					}
				}
				
				if(toFG->flags.runningUpdated)
				{
					e->mm.isSprinting = toFG->flags.isRunning;
					e->mm.spcSprintStart = toFG->spcRunningStart;
					e->mm.maxSprintDurationSeconds = toFG->maxRunDurationSeconds;
					e->mm.sprintUsesFuel = toFG->flags.runUsesFuel;
					e->mm.sprintFuel = toFG->runFuel;
				}

				if(toFG->flags.cooldownUpdated)
				{
					e->mm.cooldownIsFromRunning = toFG->flags.cooldownIsFromRunning;
					e->mm.cooldownIsFromAiming = toFG->flags.cooldownIsFromAiming;
					e->mm.spcCooldownStart = toFG->spcCooldownStart;
					e->mm.spcAimCooldownStart = toFG->spcAimCooldownStart;
					e->mm.sprintCooldownSeconds = toFG->runCooldownSeconds;
					e->mm.rollCooldownSeconds = toFG->rollCooldownSeconds;
					e->mm.aimCooldownSeconds = toFG->aimCooldownSeconds;
				}
			}
			
			toFG->flags.aimingUpdated = 0;
			toFG->flags.crouchUpdated = 0;
			toFG->flags.runningUpdated = 0;
			toFG->flags.rollingUpdated = 0;
			toFG->flags.cooldownUpdated = 0;
			toFG->flags.waitingToRollUpdated = 0;
		}

		xcase MR_MSG_BG_INITIALIZE:{
			mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_INPUT_EVENT);
		}

		xcase MR_MSG_BG_INPUT_EVENT:{
			if (!sync->flags.bIgnoreInput)
				mrTacticalInputEventBG(msg, bg, localBG, sync);
		}

		xcase MR_MSG_FG_CREATE_TOBG:{
			*toBG = fg->toBG;
			ZeroStruct(&fg->toBG);

			mrmEnableMsgUpdatedToBG(msg);
		}
		
		xcase MR_MSG_BG_UPDATED_TOBG:{
			mrTacticalUpdatedToBG(msg, bg, localBG, toBG);
		}
		
		xcase MR_MSG_BG_UPDATED_SYNC:{
			bg->flagsMutable.hasRunMaxDuration = sync->run.maxDurationSeconds > 0.f;
			
			if(sync->flags.runUsesFuel)
			{
				MIN1(bg->runFuel, sync->run.maxDurationSeconds);
			}
			else
			{
				bg->runFuel = 0;
				bg->flagsMutable.runFuelNeedsRefill = 0;
			}
			
			if(bg->flags.isRunning)
			{
				if(!mrTacticalCreateRunSpeedOverrideBG(msg, localBG, sync))
				{
					mrTacticalSetIsRunningBG(msg, 0);
				}
			}
			else if(sync->flags.runUsesFuel &&
					FALSE_THEN_SET(bg->flagsMutable.runFuelNeedsRefill))
			{
				// This should only happen when you first enable using fuel.

				bg->runFuel = sync->run.maxDurationSeconds;
				mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
			}

			if (sync->roll.iRollDecelNumberOfFrames > 0)
			{
				F32 fDecelerationSeconds = MM_FRAMES_TO_SEC(sync->roll.iRollDecelNumberOfFrames);
				bg->fRollDeceleration = fabs(sync->roll.fRollSpeed / fDecelerationSeconds);
			}
				
			if (sync->roll.iRollAccelNumberOfFrames > 0)
			{
				F32 fAccelerationSeconds = MM_FRAMES_TO_SEC(sync->roll.iRollAccelNumberOfFrames);
				bg->fRollAcceleration = fabs(sync->roll.fRollSpeed / fAccelerationSeconds);
			}
			
			mrTacticalSendAimingToFG(msg, toFG, bg);
			mrTacticalSendCrouchingToFG(msg, toFG, bg);
			mrTacticalSendRunningToFG(msg, toFG, bg, sync);
		}
		
		xcase MR_MSG_BG_DISCUSS_DATA_OWNERSHIP:{
			mrTacticalDiscussDataOwnershipBG(msg, bg, localBG, sync, toFG);
		}
		
		xcase MR_MSG_BG_OVERRIDE_VALUE_SET:{
			const char*						name = msg->in.bg.overrideValueSet.name;
			const MovementSharedDataType	valueType = msg->in.bg.overrideValueSet.valueType;
			const MovementSharedDataValue	value = msg->in.bg.overrideValueSet.value;

			if(	!stricmp(name, "RunDisabled") &&
				valueType == MSDT_S32)
			{
				localBG->flagsMutable.runIsDisabledByOverride = !!value.s32;
				
				if(localBG->flags.runIsDisabledByOverride){
					mrTacticalSetIsRunningBG(msg, 0);
				}
			}
		}

		xcase MR_MSG_BG_OVERRIDE_VALUE_UNSET:{
			if(!stricmp(msg->in.bg.overrideValueUnset.name, "RunDisabled")){
				localBG->flagsMutable.runIsDisabledByOverride = 0;
			}
		}

		xcase MR_MSG_BG_DATA_WAS_RELEASED:
		{
			mrmReleaseAllDataOwnershipBG(msg);
			
			if(TRUE_THEN_RESET(bg->flagsMutable.isRolling))
			{
				bg->flagsMutable.disableUpdateDirRoll = 0;
				toFG->flags.isRolling = 0;
				localBG->flagsMutable.hasDoubleTapDirection = 0;
				if(FALSE_THEN_SET(toFG->flags.rollingUpdated))
				{
					mrTacticalEnabledMsgUpdatedToFG(msg, toFG);
				}

				if(sync->roll.fRollFuelCost)
				{
					bg->flagsMutable.runFuelNeedsRefill = 1;
				}

				if(bg->flags.aimWhenAvailable)
				{
					mrTacticalSetIsAimingBG(msg, 1);
				}

				if(bg->flags.crouchWhenAvailable)
				{
					mrTacticalSetIsCrouchingBG(msg, 1);
				}
			}

			if(msg->in.bg.dataWasReleased.dataClassBits & MDC_BIT_POSITION_CHANGE)
			{
				Vec3 vGravityVel = {0.f, bg->gravityVel, 0.f};
				
				mrmShareOldVec3BG(msg, "Velocity", vGravityVel);
			}
			
			if(	msg->in.bg.dataWasReleased.dataClassBits & MDC_BIT_ROTATION_CHANGE &&
				bg->flags.hasYawFaceTarget)
			{
				if (!sync->roll.bRollFacesInRollDirection)
				{
					mrmShareOldF32BG(msg, "TargetFaceYaw", bg->yawFaceTarget);
				}
				else
				{
					Vec3 vPY;
					mrmGetFacePitchYawBG(msg, vPY);
					mrmShareOldF32BG(msg, "TargetFaceYaw", vPY[1]);
				}
			}
		}
		
		xcase MR_MSG_BG_RECEIVE_OLD_DATA:{
			const MovementSharedData* sd = msg->in.bg.receiveOldData.sharedData;
			
			switch(sd->dataType){
				xcase MSDT_F32:{
					if(!stricmp(sd->name, "TargetFaceYaw")){
						bg->flagsMutable.hasYawFaceTarget = 1;
						bg->yawFaceTarget = sd->data.f32;
					}
				}
			}
		}

		xcase MR_MSG_BG_CREATE_OUTPUT:{
			switch(msg->in.bg.createOutput.dataClassBit){
				xcase MDC_BIT_POSITION_CHANGE:{
					mrTacticalOutputPositionChangeBG(msg, bg, localBG, toFG, sync);
				}
				
				xcase MDC_BIT_ROTATION_CHANGE:{
					mrTacticalOutputRotationChangeBG(msg, bg, sync);
				}
				
				xcase MDC_BIT_ANIMATION:{
					if(!gConf.bNewAnimationSystem){
						if(FALSE_THEN_SET(bg->flagsMutable.playedRollAnim)){
							mrmAnimAddBitBG(msg, mmAnimBitHandles.flash.dodgeRoll);
						}
					}
					else if(FALSE_THEN_SET(bg->flagsMutable.playedRollAnim)){
						Quat		rot;
						Vec3		pyr;
						Vec2		pyFace;
						F32			yawDiff;
						F32			yawDiffABS;
						const char*	keyword = "RollBackward";

						mrmGetRotationBG(msg, rot);
						quatToPYR(rot, pyr);
						mrmGetFacePitchYawBG(msg, pyFace);
						yawDiff = subAngle(pyr[1], pyFace[1]);
						yawDiffABS = fabs(yawDiff);
							
						if(sync->roll.bRollFacesInRollDirection || yawDiffABS < QUARTERPI * 0.5f){
							keyword = "RollForward";
						}else{
							S32 isRight = yawDiff > 0.f;

							if(yawDiffABS < QUARTERPI * 1.5f){
								keyword = isRight ?
											"RollForwardRight" :
											"RollForwardLeft";
							}
							else if(yawDiffABS < QUARTERPI * 2.5f){
								keyword = isRight ?
											"RollRight" :
											"RollLeft";
							}
							else if(yawDiffABS < QUARTERPI * 3.5f){
								keyword = isRight ?
											"RollBackwardRight" :
											"RollBackwardLeft";
							}
						}
							
						mrmAnimStartBG(msg, mmGetAnimBitHandleByName(keyword, 0), 0);
					}
				}
			}
		}
		
		xcase MR_MSG_BG_CONTROLLER_MSG:{
			if(msg->in.bg.controllerMsg.isGround)
			{
				bg->flagsMutable.onGroundRolling = 1;

				copyVec3(	msg->in.bg.controllerMsg.normal,
							localBG->surfaceNormal);
			}
		}
		
		xcase MR_MSG_BG_CREATE_DETAILS:{
			if(!gConf.bNewAnimationSystem)
			{
				if(bg->flags.isRunning)
				{
					mrmAnimAddBitBG(msg, mmAnimBitHandles.sprint);
				}
				else if(!g_CombatConfig.tactical.aim.bSplitAimAndCrouch && bg->flags.aimWhenAvailable)
				{
					mrmAnimAddBitBG(msg, mmAnimBitHandles.crouch);
				}
				else if (bg->flags.crouchWhenAvailable) 
				{
					mrmAnimAddBitBG(msg, mmAnimBitHandles.crouch);
				}

				if (g_CombatConfig.tactical.aim.bSplitAimAndCrouch && bg->flags.aimWhenAvailable) 
				{
					mrmAnimAddBitBG(msg, mmAnimBitHandles.aim);
				}
			}

			if (bg->flags.isRunning &&
				g_CombatConfig.tactical.sprint.pchSprintFX &&
				g_CombatConfig.tactical.sprint.pchSprintFX[0])
			{
				MMRFxConstant fxConstant = { 0 };

				fxConstant.fxName = g_CombatConfig.tactical.sprint.pchSprintFX;

				// Create the FX
				mmrFxCreateBG(msg, &bg->mmrFxSprint, &fxConstant, NULL);
			}
		}

		xcase MR_MSG_BG_OVERRIDE_ALL_UNSET:{
			localBG->movhMaxSpeed = 0;
			localBG->hAutoSprint = 0;
		}
		
		xcase MR_MSG_BG_BEFORE_REPREDICT:{
			localBG->stanceAiming	= 0;
			localBG->stanceCrouching= 0;
			localBG->stanceSprint	= 0;

			mrTacticalSendAimingToFG(msg, toFG, bg);
			mrTacticalSendCrouchingToFG(msg, toFG, bg);
			mrTacticalSendRunningToFG(msg, toFG, bg, sync);
			
			toFG->flags.isRolling = bg->flags.isRolling;
			if(FALSE_THEN_SET(toFG->flags.rollingUpdated)){
				mrTacticalEnabledMsgUpdatedToFG(msg, toFG);
			}

			if(bg->flags.isAiming || bg->flags.isCrouching)
			{
				F32 fAimSpeed = bg->flags.isAiming ? sync->aim.fSpeed : 0.0f;
				F32 fCrouchSpeed = bg->flags.isCrouching ? sync->crouch.speed : 0.0f;

				mrmOverrideValueCreateF32BG(msg,
											&localBG->movhMaxSpeed,
											"MaxSpeed",
											MAXF(fAimSpeed, fCrouchSpeed));

				if (bg->flags.isAiming) 
				{
					mrmAnimStanceCreateBG(msg, &localBG->stanceAiming, mmAnimBitHandles.aim);

					if(sync->flags.bAimStrafes)
					{
						mrmOverrideValueCreateS32BG( msg, &localBG->hAimStrafe, "Strafe", 1);
					}
					if(sync->flags.bAimDisablesJump)
					{
						mrmOverrideValueCreateS32BG( msg, &localBG->hAimJumpDisable, "JumpDisable", 1);
					}
				}

				if (bg->flags.isCrouching)
				{
					mrmAnimStanceCreateBG(msg, &localBG->stanceCrouching, mmAnimBitHandles.crouch);
				}
			} 
			else if(bg->flags.isRunning)
			{
				mrmOverrideValueCreateF32BG(msg,
											&localBG->movhMaxSpeed,
											"MaxSpeed",
											sync->run.speed);

				mrmOverrideValueCreateS32BG(msg,
											&localBG->hAutoSprint,
											"AutoRun",
											sync->flags.bAutoSprint);

				mrmAnimStanceCreateBG(msg, &localBG->stanceSprint, mmAnimBitHandles.sprint);
			}

			if(eaSize(&localBG->overridesQueued)){
				mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
			}
		}
		
		xcase MR_MSG_BG_OVERRIDE_VALUE_DESTROYED:{
			if(msg->in.bg.overrideValueDestroyed.handle == localBG->movhMaxSpeed){
				mrTacticalSetIsAimingBG(msg, 0);
				bg->flagsMutable.aimWhenAvailable = 0;
				bg->flagsMutable.aimButtonIsDown = 0;
				mrTacticalSendAimingToFG(msg, toFG, bg);

				mrTacticalSetIsCrouchingBG(msg, 0);
				bg->flagsMutable.crouchWhenAvailable = 0;
				bg->flagsMutable.crouchButtonIsDown = 0;
				mrTacticalSendCrouchingToFG(msg, toFG, bg);

				mrTacticalSetIsRunningBG(msg, 0);
				localBG->movhMaxSpeed = 0;
			}
			else if (msg->in.bg.overrideValueDestroyed.handle == localBG->hAutoSprint)
			{
				localBG->hAutoSprint = 0;
			}
		}
		
		xcase MR_MSG_BG_DATA_RELEASE_REQUESTED:{
			if(bg->flags.isRolling){
				// Lets a roll finish if dead requester is trying to take over.

				msg->out->bg.dataReleaseRequested.denied = 1;
			}
		}
	}
}

S32 mrTacticalCreate(	MovementManager* mm,
						MovementRequester** mrOut)
{
	return mmRequesterCreateBasic(mm, mrOut, mrTacticalMsgHandler);
}

#define GET_FG(fg)						if(!MR_GET_FG(mr, mrTacticalMsgHandler, Tactical, fg)){return 0;}
#define GET_SYNC(sync)					if(!MR_GET_SYNC(mr, mrTacticalMsgHandler, Tactical, sync)){return 0;}
#define IF_DIFF_THEN_SET_FLAG(a, b,f)	(((a) != (b))?((a) = (b)),(f)=1:0)
#define IF_DIFF_THEN_SET(a, b)			MR_SYNC_SET_IF_DIFF(mr, a, b)
#define IF_DIFF_THEN_SET_VEC3(a, b)		if(!sameVec3((a), (b))){copyVec3((b), (a));mrEnableMsgUpdatedSync(mr);}((void)0)

S32 mrTacticalSetGlobalCooldown(MovementRequester* mr,
						   		F32 cooldownSeconds)
{
	TacticalSync* sync;

	GET_SYNC(&sync);

	MINMAX1(cooldownSeconds, 0.f, 500.f);
	IF_DIFF_THEN_SET(sync->globalCooldownSeconds, cooldownSeconds);

	return 1;
}

S32 mrTacticalSetRunParams(	MovementRequester* mr,
							S32 enabled,
							F32 runSpeed,
							F32 maxRunDurationSeconds,
							F32 cooldownSeconds,
							bool bAutoSprint,
							bool bSprintToggles)
{
	TacticalSync* sync;

	GET_SYNC(&sync);
	
	IF_DIFF_THEN_SET(sync->run.speed, runSpeed);

	MINMAX1(maxRunDurationSeconds, 0.f, 500.f);
	IF_DIFF_THEN_SET(sync->run.maxDurationSeconds, maxRunDurationSeconds);

	MINMAX1(cooldownSeconds, 0.f, 500.f);
	IF_DIFF_THEN_SET(sync->run.cooldownSeconds, cooldownSeconds);
	
	enabled = !enabled;
	IF_DIFF_THEN_SET(sync->flags.bSprintDisabled, (U32)enabled);

	IF_DIFF_THEN_SET(sync->flags.bAutoSprint, (U32)bAutoSprint);

	IF_DIFF_THEN_SET(sync->flags.bSprintToggles, (U32)bSprintToggles);

	return 1;
}

S32 mrTacticalSetRunFuel(	MovementRequester* mr,
							S32 enabled,
							F32 refillRate,
							F32 refillDelay)
{
	TacticalSync* sync;

	GET_SYNC(&sync);
	
	enabled = !!enabled;
	IF_DIFF_THEN_SET(sync->flags.runUsesFuel, (U32)enabled);
	
	MINMAX1(refillRate, 0.0001f, 10000.f);
	IF_DIFF_THEN_SET(sync->run.fuelRefillRate, refillRate);
	
	MINMAX1(refillDelay, 0.0001f, 10000.f);
	IF_DIFF_THEN_SET(sync->run.fuelRefillDelay, refillDelay);

	return 1;	
}

S32 mrTacticalSetRollParams(MovementRequester* mr,
							const TacticalRequesterRollDef *pDef,
							S32 bRollIgnoresGlobalCooldown,
							S32 bRollOnDoubleTap)
{
	TacticalSync* sync;
	bool bSet = false;
	GET_SYNC(&sync);

	if (memcmp(pDef, &sync->roll, sizeof(TacticalRequesterRollDef)))
	{
		sync->roll = *pDef;
		bSet = true;
	}

	bRollOnDoubleTap = !!bRollOnDoubleTap;
	IF_DIFF_THEN_SET_FLAG(sync->flags.doubleTapDoesRoll, (U32)bRollOnDoubleTap, bSet);

	bRollIgnoresGlobalCooldown = !!bRollIgnoresGlobalCooldown;
	IF_DIFF_THEN_SET_FLAG(sync->flags.bRollIgnoresGlobalCooldown, (U32)bRollIgnoresGlobalCooldown, bSet);
	
	if (bSet)
	{
		mrEnableMsgUpdatedSync(mr);
	}

	return 1;
}

S32 mrTacticalSetAimParams(	MovementRequester* mr,
							const TacticalRequesterAimDef *pDef,
							F32 aimSpeed,
							S32 runPlusCrouchDoesRoll,
							S32 rollWhileAiming,
							S32 bAimIgnoresGlobalCooldown,
							bool bAimStrafes,
							bool bAimDisablesJump)
{
	TacticalSync* sync;
	TacticalRequesterAimDef defCopy;
	bool bSet = false;

	defCopy = *pDef;
	defCopy.fSpeed = aimSpeed;
	
	GET_SYNC(&sync);

	if (memcmp(&defCopy, &sync->aim, sizeof(TacticalRequesterRollDef)))
	{
		sync->aim = defCopy;
		bSet = true;
	}

	runPlusCrouchDoesRoll = !!runPlusCrouchDoesRoll;
	bAimIgnoresGlobalCooldown = !!bAimIgnoresGlobalCooldown;

	IF_DIFF_THEN_SET_FLAG(sync->flags.runPlusCrouchDoesRoll, (U32)runPlusCrouchDoesRoll, bSet);
	IF_DIFF_THEN_SET_FLAG(sync->flags.rollWhileAiming, (U32)rollWhileAiming, bSet);
	IF_DIFF_THEN_SET_FLAG(sync->flags.bAimIgnoresGlobalCooldown, (U32)bAimIgnoresGlobalCooldown, bSet);
	IF_DIFF_THEN_SET_FLAG(sync->flags.bAimStrafes, (U32)bAimStrafes, bSet);
	IF_DIFF_THEN_SET_FLAG(sync->flags.bAimDisablesJump, (U32)bAimDisablesJump, bSet);

	if (bSet)
	{
		mrEnableMsgUpdatedSync(mr);
	}

	return 1;
}

S32 mrTacticalSetCrouchParams(MovementRequester* mr,
							  F32 crouchSpeed,
							  S32 runPlusCrouchDoesRoll)
{
	TacticalSync* sync;
	
	GET_SYNC(&sync);

	runPlusCrouchDoesRoll = !!runPlusCrouchDoesRoll;
	
	IF_DIFF_THEN_SET(sync->crouch.speed, crouchSpeed);
	IF_DIFF_THEN_SET(sync->flags.runPlusCrouchDoesRoll, (U32)runPlusCrouchDoesRoll);

	return 1;
}

S32 mrTacticalSetRunMode(	MovementRequester* mr,
							S32 doRunning)
{
	TacticalFG* fg = NULL;

	if(mrGetFG(mr, mrTacticalMsgHandler, &fg)){
		if(doRunning){
			fg->toBG.flags.startRunning = 1;
			fg->toBG.flags.stopRunning = 0;
		}else{
			fg->toBG.flags.startRunning = 0;
			fg->toBG.flags.stopRunning = 1;
		}

		return mrEnableMsgCreateToBG(mr);
	}

	return 0;
}

S32 mrTacticalSetAimMode(	MovementRequester* mr,
							S32 doAim)
{
	TacticalFG* fg = NULL;

	if (mrGetFG(mr, mrTacticalMsgHandler, &fg))
	{
		if(doAim)
		{
			fg->toBG.flags.startAiming = 1;
			fg->toBG.flags.stopAiming = 0;
		}
		else
		{
			fg->toBG.flags.startAiming = 0;
			fg->toBG.flags.stopAiming = 1;
		}

		return mrEnableMsgCreateToBG(mr);
	}

	return 0;
}

S32 mrTacticalSetCrouchMode(MovementRequester* mr,
							S32 doCrouch)
{
	TacticalFG* fg = NULL;

	if (g_CombatConfig.tactical.aim.bSplitAimAndCrouch &&
		mrGetFG(mr, mrTacticalMsgHandler, &fg))
	{
		if(doCrouch)
		{
			fg->toBG.flags.startCrouching = 1;
			fg->toBG.flags.stopCrouching = 0;
		}
		else
		{
			fg->toBG.flags.startCrouching = 0;
			fg->toBG.flags.stopCrouching = 1;
		}

		return mrEnableMsgCreateToBG(mr);
	}

	return 0;
}

S32 mrTacticalPerformRoll(	MovementRequester* mr,
							F32 yawRoll,
							U32 spc)
{
	TacticalFG* fg = NULL;

	if (mrGetFG(mr, mrTacticalMsgHandler, &fg))
	{
		fg->toBG.spcStartRoll = spc;
		fg->toBG.scheduledRollYaw = yawRoll;
		return mrEnableMsgCreateToBG(mr);
	}

	return 0;
}

S32 mrTacticalGetAimState(	MovementRequester* mr,
							S32* isAimingOut,
							S32* aimWhenAvailableOut)
{
	TacticalFG* fg = NULL;

	if(!mrGetFG(mr, mrTacticalMsgHandler, &fg)){
		return 0;
	}
	
	if(isAimingOut){
		*isAimingOut = fg->fromBG.flags.isAiming;
	}
	
	if(aimWhenAvailableOut){
		*aimWhenAvailableOut = fg->fromBG.flags.aimWhenAvailable;
	}
	
	return 1;
}

S32 mrTacticalGetCrouchState(MovementRequester* mr,
							 S32* isCrouchingOut,
							 S32* crouchWhenAvailableOut)
{
	TacticalFG* fg = NULL;

	if(!mrGetFG(mr, mrTacticalMsgHandler, &fg)){
		return 0;
	}
	
	if(isCrouchingOut){
		*isCrouchingOut = fg->fromBG.flags.isCrouching;
	}
	
	if(crouchWhenAvailableOut){
		*crouchWhenAvailableOut = fg->fromBG.flags.crouchWhenAvailable;
	}
	
	return 1;
}

								
S32 mrTacticalSetRollOnDoubleTap(	MovementRequester* mr,
									S32 enabled)
{
	TacticalSync* sync;
	
	GET_SYNC(&sync);

	enabled = !!enabled;
	IF_DIFF_THEN_SET(sync->flags.doubleTapDoesRoll, (U32)enabled);
	return 1;
}

S32	mrTacticalIgnoreInput( MovementRequester *mr, S32 bIgnoreInput)
{
	TacticalSync* sync;

	GET_SYNC(&sync);

	bIgnoreInput = !!bIgnoreInput;
	IF_DIFF_THEN_SET(sync->flags.bIgnoreInput, (U32)bIgnoreInput);
	return 1;
}


static __forceinline S32 mrTacticalNotifyPowersStartQueue(	MovementRequester* mr,
															U32 id,
															U32 spc, 
															TacticalDisableFlags flags,
															S32 isStop)
{
	TacticalOverrides*	to;
	TacticalFG*			fg;

	GET_FG(&fg);
	
	// if it's a special tactical disable ID, track it because we don't want to send duplicates 
	if (IS_SPECIAL_TACTICAL_DISABLE_ID(id))
	{
		bool bFound = false;
		FOR_EACH_IN_EARRAY(fg->overrides, TacticalOverrides, pOverride)
		{
			if (pOverride->id == id)
			{
				if (isStop)
				{	// this is a stop, so remove it from the override list
					eaRemoveFast(&fg->overrides, FOR_EACH_IDX(-,pOverride));
					StructDestroySafe(parse_TacticalOverrides, &pOverride);
					bFound = true;
					break;
				}
				else
				{	
					// received a duplicate stop
					return 1;
				}
			}
		}
		FOR_EACH_END

		if (isStop && !bFound)
		{	// nothing found to stop, we will ignore it
			return 1;
		}
	}
	
	mrLog(	mr,
			NULL,
			"Disable %s: id %u, spc %u (%s%s%s%s)",
			isStop ? "STOP" : "START",
			id,
			spc,
			flags & TDF_ROLL ? "roll," : "",
			flags & TDF_AIM ? "aim," : "",
			flags & TDF_CROUCH ? "crouch," : "",
			flags & TDF_SPRINT ? "sprint," : "");

	mrEnableMsgCreateToBG(mr);

	to = StructAlloc(parse_TacticalOverrides);
	to->id = id;
	to->spc = spc;
	to->flags.isStop = isStop;

	if(flags){
		if(flags & TDF_ROLL){
			to->flags.disableRoll = 1;
		}
		if(flags & TDF_AIM){
			to->flags.disableAim = 1;
		}
		if(flags & TDF_CROUCH){
			to->flags.disableCrouch = 1;
		}
		if(flags & TDF_SPRINT){
			to->flags.disableSprint = 1;
		}
		if (flags & TDF_QUEUE){
			to->flags.disableQueue = 1;
		}
	}

	eaPush(&fg->toBG.overrides, to);

	if (!isStop && IS_SPECIAL_TACTICAL_DISABLE_ID(id))
	{	
		TacticalOverrides *pNew = StructAlloc(parse_TacticalOverrides);
		StructCopyAll(parse_TacticalOverrides,to,pNew);
		eaPush(&fg->overrides, pNew);
	}
	return 1;
}


S32 mrTacticalNotifyPowersStart(MovementRequester* mr,
								U32 id,
								TacticalDisableFlags flags,
								U32 spc)
{
	return mrTacticalNotifyPowersStartQueue(mr, id, spc, flags, 0);
}

S32 mrTacticalNotifyPowersStop(	MovementRequester* mr,
								U32 id,
								U32 spc)
{
	return mrTacticalNotifyPowersStartQueue(mr, id, spc, TDF_NONE, 1);
}

S32 mrTacticalHasDisableID(	MovementRequester* mr,
							U32 id)
{
	TacticalFG*			fg;

	GET_FG(&fg);

	if (IS_SPECIAL_TACTICAL_DISABLE_ID(id))
	{
		bool bFound = false;
		FOR_EACH_IN_EARRAY(fg->overrides, TacticalOverrides, pOverride)
		{
			if (pOverride->id == id)
			{
				return true;
			}
		}
		FOR_EACH_END
	}

	return false;
}

S32 mrTacticalNotifyClearAllDisables(	MovementRequester* mr,
										U32 spc)
{
	TacticalFG*			fg;
	
	GET_FG(&fg);

	mrLog(	mr,
			NULL,
			"TacticalRequester: Clearing all Disables");
	
	mrEnableMsgCreateToBG(mr);

	fg->toBG.spcClearAllOverrides = spc;
	return 1;
}

#include "AutoGen/EntityMovementTactical_c_ast.c"
