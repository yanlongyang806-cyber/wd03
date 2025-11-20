#include "gclPlayerControl.h"

#include "Character.h"
#include "character_target.h"
#include "CharacterAttribsMinimal.h"
#include "ClientTargeting.h"
#include "CombatConfig.h"

#include "Entity.h"
#include "EntityIterator.h"
#include "EntityMovementManager.h"
#include "EntityMovementTactical.h"


#include "GameClientLib.h"
#include "gclCamera.h"
#include "gclEntity.h"

#include "gclCommandParse.h"
#include "gclCombatReactivePower.h"
#include "gclCombatPowerStateSwitching.h"
#include "gclControlScheme.h"
#include "gclCursorMode.h"
#include "gclUtils.h"

#include "InteractionUI.h"
#include "inputGamepad.h"
#include "inputData.h"
#include "inputMouse.h"

#include "Player.h"
#include "PowerModes.h"
#include "PowersMovement.h"
#include "RegionRules.h"

#include "UITray.h"
#include "wlInteraction.h"
#include "WorldColl.h"
#include "WorldGrid.h"
#include "WorldLib.h"

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

// using separate static flags for the mouse so we can detect mouse look state changes in
// gclPlayerControl_UpdateMouseInput
static S32 s_bMouseLookFree, s_bMouseLookForced;
static S32 s_bFreeMouseCursorOn = false;
static F32 s_fNavToPosCloseDistance = INTERACT_RANGE;



AUTO_CMD_INT(s_fNavToPosCloseDistance, NavToPosCloseDistance) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);

// ----------------------------------------------------------------------
typedef struct GCLPlayerControl
{
	Vec3	vCurrentNavTargetPos;

	F32		fCurMoveYaw;
	F32		fCurFaceYaw;
	F32		fCurPitch;

	int		bWasMovingWithJoystick;

	// nav to pos
	int		bNavToPos;
	int		bHasUsedNav;

	// facing info
	EntityRef	erFacingTarget;
	int			bFacingTarget;
	EntityRef	erLastFacingTarget;

	U32		bMouseLook_Forced : 1;
	U32		bMouseLook_Free : 1;
	U32		bMode_FacingTarget : 1;
	U32		bAlwaysUseMouseLookForced : 1;
	U32		bSuppressForcedMouseLookThisFrame : 1;
	int		bSuspendForcedMouselook;

	int		bCameraIsFree;

	int		bTurnLeft;
	int		bTurnRight;
	int		bTurnIsStrafe;

	// 
	int		bFollowEntity;
	int		bFollowUntilCombatOrRange;
	F32		fFollowMinRange;

	// STO ship
	int		bNavDisableAutoThrottleAdjust;
	int		bSTOSpaceshipMovement;
	int		bSTOSpaceshipRightTriggerThrottle;
	
	int		input_bCameraRotateRight;
	int		input_bCameraRotateLeft;

	// Predicted throttle value
	F32		fPredictedThrottle;
	U32		uiLastThrottleAdjustTime;

} GCLPlayerControl;

typedef struct ControlButtonStates {
	S32		forward;
	S32		autoForward;
	S32		mouseForward;

	S32		backward;
	S32		left;
	S32		right;
	S32		up;
	S32		down;
	S32		slow;
	S32		turnleft;
	S32		turnright;
	S32		lookUp;
	S32		lookDown;
	
	S32		roll;
	S32		run;
	S32		aim;
	S32		lastaim;
	S32		crouch;
	S32		lastcrouch;
	S32		tactical;
	S32		hardTargetLock;
} ControlButtonStates;

// 
static ControlButtonStates gControlBtnStates[4] = {0};
static GCLPlayerControl gPlayerControlData = {0};
static EntityRef s_currentFollowTargetRef = 0;

static void updateControlTracked(	U32 controlledEntityIndex,
									MovementInputValueIndex mivi,
									S32 value,
									S32 canDoubleTap,
									S32 *pbIsDoubleTapOut,
									const char* fileName,
									U32 fileLine);

#define updateControl(controlledEntityIndex, mivi, value, canDoubleTap)\
		updateControlTracked(controlledEntityIndex, mivi, value, canDoubleTap, NULL, __FILE__, __LINE__)

#define updateControlEx(controlledEntityIndex, mivi, value, canDoubleTap, pIsDoubleTapOut)\
	updateControlTracked(controlledEntityIndex, mivi, value, canDoubleTap, pIsDoubleTapOut, __FILE__, __LINE__)

// Macros for "turnstrafetoggle" gControlBtnStates
#define CONTROLS_LEFT(i) (gControlBtnStates[i].left || (gControlBtnStates[i].turnleft && gPlayerControlData.bTurnIsStrafe))
#define CONTROLS_RIGHT(i) (gControlBtnStates[i].right || (gControlBtnStates[i].turnright && gPlayerControlData.bTurnIsStrafe))
#define CONTROLS_FORWARD(i) (gControlBtnStates[i].forward || gControlBtnStates[i].autoForward || gControlBtnStates[i].mouseForward)



bool gclPlayerControl_IsAlwaysUsingMouseLookForced()
{
	//There are two ways to temporarily suspend bAlwaysUseMouseLookForced: 
	//Through a bindable command, and by calling CameraSuppressForcedMouseLockThisFrame every frame (which is what the STO/NW ui does.)
	return gPlayerControlData.bAlwaysUseMouseLookForced && !gPlayerControlData.bSuspendForcedMouselook && !gPlayerControlData.bSuppressForcedMouseLookThisFrame;
}

// ------------------------------------------------------------------------------------------------------------------------------------
F32 gclPlayerControl_GetCurMoveYaw(void)
{
	return gPlayerControlData.fCurMoveYaw;
}
// ------------------------------------------------------------------------------------------------------------------------------------
F32 gclPlayerControl_GetCurFaceYaw(void)
{
	return gPlayerControlData.fCurFaceYaw;
}

// ------------------------------------------------------------------------------------------------------------------------------------
void gclPlayerControl_Reset(void)
{
	ZeroStruct(&gPlayerControlData);
}

// ------------------------------------------------------------------------------------------------------------------------------------
int gclPlayerControl_IsMouseRotating(void)
{
	return gPlayerControlData.bMouseLook_Forced || gPlayerControlData.bMouseLook_Free;
}

// -------------------------------------------------------------------------------------------------------------
bool gclPlayerControl_IsMouseLooking(void)
{
	return gPlayerControlData.bMouseLook_Forced;
}

// -------------------------------------------------------------------------------------------------------------
static void gclPlayerControl_SetTurnLeft(int bTurnLeft)
{
	gPlayerControlData.bTurnLeft = bTurnLeft;
	gclCamera_SetCameraInputTurnLeft(bTurnLeft);
}

// -------------------------------------------------------------------------------------------------------------
static void gclPlayerControl_SetTurnRight(int bTurnRight)
{
	gPlayerControlData.bTurnRight = bTurnRight;
	gclCamera_SetCameraInputTurnRight(bTurnRight);
}

// ------------------------------------------------------------------------------------------------------------------------------------
void gclPlayerControl_SetTurnToStrafe(S32 bTurnIsStrafe)
{
	if (gPlayerControlData.bTurnIsStrafe == !!bTurnIsStrafe)
		return;

	gPlayerControlData.bTurnIsStrafe = !!bTurnIsStrafe;

	if (gPlayerControlData.bTurnIsStrafe)
	{
		gclPlayerControl_SetTurnRight(false);
		gclPlayerControl_SetTurnLeft(false);
	}
	else
	{
		gclPlayerControl_SetTurnRight(gControlBtnStates[0].turnright);
		gclPlayerControl_SetTurnLeft(gControlBtnStates[0].turnleft);
	}

	updateControl(0, MIVI_BIT_LEFT, CONTROLS_LEFT(0), 0);
	updateControl(0, MIVI_BIT_RIGHT, CONTROLS_RIGHT(0), 0);
}

// ------------------------------------------------------------------------------------------------------------------------------------
// Sets up the control's to treat the turning keys as strafe or not
static void gclPlayerControl_UpdateTurnStrafeToggle(void)
{
	gclPlayerControl_SetTurnToStrafe(gPlayerControlData.bMouseLook_Forced || 
			(!g_CurrentScheme.bTurningTurnsCameraWhenFacingTarget && gPlayerControlData.bMode_FacingTarget));
}

// ------------------------------------------------------------------------------------------------------------------------------------
static void gclPlayerControl_UpdateFacingTargetInfo(Entity *e)
{
	gPlayerControlData.erLastFacingTarget = gPlayerControlData.erFacingTarget;
	gPlayerControlData.erFacingTarget = pmGetSelectedTarget(e);
	if (gPlayerControlData.erFacingTarget != 0 && gPlayerControlData.erFacingTarget != e->myRef)
	{
		if (!g_CurrentScheme.bAutoFaceHostileTargetsOnly || gclEntGetIsFoe(entActivePlayerPtr(), entFromEntityRefAnyPartition(gPlayerControlData.erFacingTarget)))
			gPlayerControlData.bMode_FacingTarget = true;
		else
			gPlayerControlData.bMode_FacingTarget = false;
	}
	else
		gPlayerControlData.bMode_FacingTarget = false;
}

// ------------------------------------------------------------------------------------------------------------------------------------
bool gclPlayerControl_IsFacingTarget(void)
{
	return gPlayerControlData.bMode_FacingTarget;
}

// ------------------------------------------------------------------------------------------------------------------------------------
void gclPlayerControl_SetAlwaysUseMouseLookForced(bool bEnable)
{
	gPlayerControlData.bAlwaysUseMouseLookForced = bEnable;
	if (bEnable)
	{
		s_bMouseLookForced = false;
	}
}

// ------------------------------------------------------------------------------------------------------------------------------------
void gclPlayerControl_UpdateMouseInput(int bFreeLookDown, int bMouseLook_Forced)
{
	int bWasMouseLook = gPlayerControlData.bMouseLook_Forced;
	int bWasMouseFreeLook = gPlayerControlData.bMouseLook_Free;

	if (gclPlayerControl_IsAlwaysUsingMouseLookForced())
	{
		bMouseLook_Forced = true;
	}
	gPlayerControlData.bMouseLook_Forced = bMouseLook_Forced;
	gPlayerControlData.bMouseLook_Free = bFreeLookDown;

	gPlayerControlData.bCameraIsFree = gPlayerControlData.bCameraIsFree | bFreeLookDown;

	gclCamera_OnMouseLook(bFreeLookDown || bMouseLook_Forced);

	if (bWasMouseLook && !bMouseLook_Forced && gGCLState.pPrimaryDevice)
	{	// if the user just released the mouse look button, force an update on the player's face/move yaw
		Entity *e = entActivePlayerPtr();
		F32 camYaw = addAngle(gGCLState.pPrimaryDevice->gamecamera.campyr[1], PI);

		gclPlayerControl_SetMoveAndFaceYawByEnt(e, camYaw);
		if (! gPlayerControlData.bMouseLook_Free)
			gPlayerControlData.bCameraIsFree = false;
	}
}

void gclPlayerControl_SetFreeCamera(int bFreeCamera)
{

	gPlayerControlData.bCameraIsFree = bFreeCamera;
}

// ------------------------------------------------------------------------------------------------------------------------------------
__forceinline static int gclPlayerControl_IsPlayerFlying(Entity *pEnt)
{
	return pEnt->pChar->pattrBasic->fFlight > 0;
}

// ------------------------------------------------------------------------------------------------------------------------------------
void gclPlayerControl_Init(F32 fMoveYaw, F32 fFaceYaw)
{
	gPlayerControlData.fCurFaceYaw = fFaceYaw;
	gPlayerControlData.fCurMoveYaw = fMoveYaw;
	gPlayerControlData.fCurPitch = 0.f;
}


// ------------------------------------------------------------------------------------------------------------------------------------
#define gclPlayerControl_SetMoveAndFaceYaw(mm, fMoveYaw, fFaceYaw, milliseconds)\
		gclPlayerControl_SetMoveAndFaceYawTracked(mm, fMoveYaw, fFaceYaw, milliseconds, __FILE__, __LINE__)

static void gclPlayerControl_SetMoveAndFaceYawTracked(	MovementManager* mm,
														F32 fMoveYaw,
														F32 fFaceYaw,
														U32 milliseconds,
														const char* fileName,
														U32 fileLine)
{
	gPlayerControlData.fCurFaceYaw = fFaceYaw;
	gPlayerControlData.fCurMoveYaw = fMoveYaw;

	mmInputEventSetValueF32(mm, MIVI_F32_FACE_YAW, fFaceYaw, milliseconds);
	mmInputEventSetValueF32(mm, MIVI_F32_MOVE_YAW, fMoveYaw, milliseconds);
}

// ------------------------------------------------------------------------------------------------------------------------------------

void gclPlayerControl_SetMoveAndFaceYawByEntTracked(Entity *pEnt,
													F32 yaw,
													const char* fileName,
													U32 fileLine) 
{
	if (pEnt && entIsLocalPlayer(pEnt))
	{
		MovementManager*	mm;
		if(mmGetLocalManagerByIndex(&mm, 0))
		{
			U32	milliseconds;
			frameLockedTimerGetCurTimes(gGCLState.frameLockedTimer, NULL, &milliseconds, NULL);

			gclPlayerControl_SetMoveAndFaceYawTracked(mm, yaw, yaw, milliseconds, fileName, fileLine);
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------------------------
static void gclPlayerControl_SetPitch(Entity* pEnt, MovementManager* mm, F32 fPitch, U32 uiMilliseconds)
{
	if ( pEnt->pChar && gclPlayerControl_IsPlayerFlying(pEnt) && !gGCLState.pPrimaryDevice->gamecamera.freelook)
	{
		gPlayerControlData.fCurPitch = fPitch;
		mmInputEventSetValueF32(mm, MIVI_F32_PITCH, fPitch, uiMilliseconds);
	}
}

// ------------------------------------------------------------------------------------------------------------------------------------
static void gclPlayerControl_SetPitchAndYawFromDirection(Entity* pEnt, MovementManager* mm, Vec3 vDir, U32 uiMilliseconds)
{
	F32 fYaw, fPitch;
	getVec3YP(vDir, &fYaw, &fPitch);
	gclPlayerControl_SetMoveAndFaceYaw(mm, fYaw, fYaw, uiMilliseconds);
	gclPlayerControl_SetPitch(pEnt, mm, fPitch, uiMilliseconds);
}

// ------------------------------------------------------------------------------------------------------------------------------------
#define gclPlayerControl_mmInputEventF32(mm, mivi, value, milliseconds)\
		gclPlayerControl_mmInputEventF32Tracked(mm, mivi, value, milliseconds, __FILE__, __LINE__)

static void gclPlayerControl_mmInputEventF32Tracked(MovementManager* mm,
													MovementInputValueIndex mivi,
													F32 fValue,
													U32 milliseconds,
													const char* fileName,
													U32 fileLine)
{
	mmInputEventSetValueF32Tracked(mm, mivi, fValue, milliseconds, fileName, fileLine);
	switch(mivi)
	{
		xcase MIVI_F32_MOVE_YAW: 
			gPlayerControlData.fCurMoveYaw = fValue;
		xcase MIVI_F32_FACE_YAW:
			gPlayerControlData.fCurFaceYaw = fValue;
		xcase MIVI_F32_PITCH:
			gPlayerControlData.fCurPitch = fValue;
	}
}

// ------------------------------------------------------------------------------------------------------------------------------------
F32 gclPlayerControl_GetMoveYaw(void)
{
	return gPlayerControlData.fCurMoveYaw;
}

// ------------------------------------------------------------------------------------------------------------------------------------
F32 gclPlayerControl_GetFaceYaw(void)
{
	return gPlayerControlData.fCurFaceYaw;
}

// ------------------------------------------------------------------------------------------------------------------------------------
int gclPlayerControl_IsHoldingAim()
{
	return gControlBtnStates[0].aim;
}

// ------------------------------------------------------------------------------------------------------------------------------------
bool gclPlayerControl_GetCameraSnapToFacing(Vec2 pyFace)
{
	Entity *e = entActivePlayerPtr();

	if (e && e->pChar)
	{
		EntityRef erTarget = pmGetSelectedTarget(e);
		if (erTarget)
		{
			Entity *pTargetEnt = entFromEntityRefAnyPartition(erTarget);
			if (pTargetEnt)
			{
				Vec3 vCurPos, vTargetPos, vDir;
				entGetPos(pTargetEnt, vTargetPos);
				entGetPos(e, vCurPos);

				subVec3(vTargetPos, vCurPos, vDir);

				pyFace[0] = getVec3Pitch(vDir);
				pyFace[1] = getVec3Yaw(vDir);
				return true;
			}
		}
		else
		{
			// if not facing a target, depends on some control scheme stuff
			if (g_CurrentScheme.bStrafing == true)
			{
				entGetFacePY(e, pyFace);
			}
			else
			{
				pyFace[0] = gPlayerControlData.fCurPitch;
				pyFace[1] = gPlayerControlData.fCurMoveYaw;
			}
		}
		return true;
	}

	return false;
}

// ------------------------------------------------------------------------------------------------------------------------------------
static bool gclPlayerControl_AllowFollowTargetInternal( SA_PARAM_NN_VALID Entity *e, SA_PARAM_NN_VALID Entity *target, F32 fDistance )
{
	if ( e == target )
		return false;

	if ( gPlayerControlData.bFollowUntilCombatOrRange )
	{
		if ( gPlayerControlData.fFollowMinRange > 0 && gPlayerControlData.fFollowMinRange >= (S32)fDistance )
		{
			return false;
		}
		if ( character_HasMode(e->pChar, kPowerMode_Combat) )
		{
			return false;
		}
	}

	//In order to follow entities, they must be alive, and have the same allegiance
	// TODO(BH): This shouldn't check allegiance handles, it should instead check allegiance relations
	if ( gConf.bDisableFollowCritters && !gPlayerControlData.bFollowUntilCombatOrRange )
	{
		if ( entIsAlive(target) && entIsPlayer(target) && !critter_IsKOS(PARTITION_CLIENT, e, target) )
		{
			if ( REF_COMPARE_HANDLES(e->hAllegiance, target->hAllegiance) ||
				 REF_COMPARE_HANDLES(e->hAllegiance, target->hSubAllegiance) ||
				 REF_COMPARE_HANDLES(e->hSubAllegiance, target->hAllegiance) )
			{
				return true;
			}
		}

		return false;
	}

	return entIsAlive(target) && (!entIsPlayer(target) || !critter_IsKOS(PARTITION_CLIENT, e, target));
}

// ------------------------------------------------------------------------------------------------------------------------------------
bool gclPlayerControl_AllowFollowTargetByRef( U32 uiEntRef )
{
	Entity *e = entActivePlayerPtr();
	Entity *target = entFromEntityRefAnyPartition(uiEntRef);

	if ( !e || !target )
		return false;

	return gclPlayerControl_AllowFollowTargetInternal(e,target,-1);
}


// ------------------------------------------------------------------------------------------------------------------------------------
static void gclPlayerControl_HandleThrottle( SA_PARAM_NN_VALID Entity* pEnt, Vec3 vTargetDir, F32 fDistance, F32 fNearDistance )
{
	RegionRules* pRules = getRegionRulesFromRegionType( entGetWorldRegionTypeOfEnt(pEnt) );

	if ( gPlayerControlData.bNavDisableAutoThrottleAdjust || !pRules || !pRules->bAllowNavThrottleAdjust )
		return;

	if ( vTargetDir )
	{
		Vec2 vFacePY;
		F32 fAngle, fTargetYaw, fCloseYaw = PI/6.0f;
		entGetFacePY(pEnt, vFacePY);
		getVec3YP(vTargetDir, &fTargetYaw, NULL);
		fAngle = ABS(subAngle(fTargetYaw,vFacePY[1]));

		if ( fAngle < fCloseYaw && (fDistance < 0.0f || fDistance > fNearDistance*2) )
		{
			ServerCmd_MovementThrottleSet(1.0f);
		}
		else if ( fDistance > fNearDistance*2 )
		{
			ServerCmd_MovementThrottleSet(0.5f);
		}
		else if ( fDistance > fNearDistance )
		{
			F32 fThrottle = 0.5f + ((fDistance-fNearDistance)/(fNearDistance))*0.5f;
			ServerCmd_MovementThrottleSet(fThrottle);
		}
		else
		{
			ServerCmd_MovementThrottleSet(0.0f);
		}
	}
	else
	{
		ServerCmd_MovementThrottleSet(0.0f);
	}
}

// ------------------------------------------------------------------------------------------------------------------------------------
static bool gclHandleEntityAvoidance(	SA_PARAM_NN_VALID Entity* pEnt, MovementManager* mm, U32 uiMilliseconds, 
									 Vec3 vTargetPos, Vec3 vTargetDir, F32 fRange )
{
	RegionRules* pRules = getRegionRulesFromRegionType( entGetWorldRegionTypeOfEnt(pEnt) );

	if ( vTargetDir && pRules && pRules->bHandleEntityAvoidance )
	{
		Vec3 vCloseDir, vPlayerPos, vPlayerVel;
		F32 fRangeSqr = SQR(fRange);
		F32 fRangeSqrPlusEps = fRangeSqr + 0.001f;
		F32 fCloseDist = fRangeSqrPlusEps;
		EntityIterator *pIter = entGetIteratorAllTypesAllPartitions(0,0);
		Entity *pCurrEnt;

		entGetPos(pEnt, vPlayerPos);
		entCopyVelocityFG(pEnt, vPlayerVel);
		scaleAddVec3(vPlayerVel, 0.5f, vPlayerPos, vPlayerPos);

		while(pCurrEnt = EntityIteratorGetNext(pIter))
		{
			Vec3 vEntPos, vEntVel, vPlayerToEntDir, vEntToTargetDir;
			F32 fDist, fEntToTargetDistSqr;
			if ( pCurrEnt == pEnt )
				continue;

			entGetPos(pCurrEnt, vEntPos);
			entCopyVelocityFG(pCurrEnt, vEntVel);
			scaleAddVec3(vEntVel, 0.5f, vEntPos, vEntPos);
			subVec3(vTargetPos, vEntPos, vEntToTargetDir);

			fEntToTargetDistSqr = lengthVec3Squared(vEntToTargetDir);

			if ( fEntToTargetDistSqr < fRangeSqr )
				continue;

			subVec3(vEntPos, vPlayerPos, vPlayerToEntDir);

			if ( ABS(vPlayerToEntDir[1]) > fRange*0.66f )
				continue;

			if ( dotVec3(vPlayerToEntDir, vTargetDir) < 0 )
				continue;

			vPlayerToEntDir[1] = 0;
			fDist = lengthVec3Squared(vPlayerToEntDir);

			if ( fDist < fRangeSqrPlusEps )
			{
				fCloseDist = fDist;
				copyVec3(vPlayerToEntDir,vCloseDir);
			}
		}
		EntityIteratorRelease(pIter);

		if ( fCloseDist < fRangeSqrPlusEps )
		{
			Vec3 vAvoidDir, vUp;
			setVec3(vUp, 0, 1, 0);
			crossVec3(vCloseDir, vUp, vAvoidDir);
			gclPlayerControl_SetPitchAndYawFromDirection(pEnt, mm, vAvoidDir, uiMilliseconds);
			return true;
		}
	}

	return false;
}

// ------------------------------------------------------------------------------------------------------------------------------------
void gclPlayerControl_ControllerMovement(MovementManager* mm,
										 F32 fCameraYaw,
										 F32 fCameraPitch,
										 U32 milliseconds, 
										 F32 fStickScale,
										 F32 fStickPitch,
										 F32 fStickYaw)
{
	F32 yaw;

	if (fStickScale != 0.f)
	{
		if (fStickScale > 0.5f)
		{
			fStickScale = 1.0f;
		}
		else if (fStickScale < 0.2f)
		{
			fStickScale = .2f;
		}
	}

	// Scale
	mmInputEventSetValueF32(mm, MIVI_F32_DIRECTION_SCALE, fStickScale, milliseconds);

	// Yaw
	if (gPlayerControlData.bSTOSpaceshipMovement)
	{
		if (fStickYaw > HALFPI)
			fStickYaw = PI - fStickYaw;
		else if (fStickYaw < -HALFPI)
			fStickYaw = -(PI + fStickYaw);
	}

	yaw = addAngle(fStickYaw, fCameraYaw);
	gclPlayerControl_mmInputEventF32(mm, MIVI_F32_MOVE_YAW, yaw, milliseconds);

	if (!gPlayerControlData.bSTOSpaceshipMovement) // Not sure this if is necessary. --poz
	{
		Vec3 vStick, camPYR, vOutDir;
		Mat3 camMat;
		F32 movePitch;

		camPYR[0] = fCameraPitch;
		camPYR[1] = fCameraYaw;
		camPYR[2] = 0.f;

		setVec3(vStick, sinf(fStickYaw), 0, cosf(fStickYaw));
		
		createMat3YPR(camMat, camPYR);
		mulVecMat3(	vStick, camMat, vOutDir);

		getVec3YP(vOutDir, NULL, &movePitch);
		movePitch = -movePitch;
		gclPlayerControl_mmInputEventF32(mm, MIVI_F32_PITCH, movePitch, milliseconds);
		
		gclPlayerControl_mmInputEventF32(mm, MIVI_F32_FACE_YAW, fCameraYaw, milliseconds);
	}

	if (isPlayerAutoForward())
	{
		globCmdParse("autoForward 0");
	}

	// Direction
	mmInputEventSetValueBit(mm, MIVI_BIT_FORWARD, 1, milliseconds, 0);

	gPlayerControlData.bWasMovingWithJoystick = true;
}

// ------------------------------------------------------------------------------------------------------------------------------------
static void gclPlayerControl_FollowTargetEntity(MovementManager* mm,
												Entity *e, 
												U32 milliseconds)
{
	Vec3 entPos;
	Vec3 targPos;
	Vec3 entToTarg;
	F32 maxThrottle;
	F32 dist;
	Entity *target;

	target = entFromEntityRefAnyPartition(s_currentFollowTargetRef);
	if (!target)
		return;

	dist = entGetDistance(e, NULL, target, NULL, targPos);

	if (gclPlayerControl_AllowFollowTargetInternal(e,target,dist))
	{
		F32 throttleClamp = 0.5f; // Must be greater than turnOnlyZone

		entGetPos(e, entPos);
		subVec3(targPos, entPos, entToTarg);

		//Set pitch and yaw
		gclPlayerControl_SetPitchAndYawFromDirection(e, mm, entToTarg, milliseconds);

		// Scale
		// Find the distance between you and the target, 
		// If it's too low don't bother
		// Multiply by an arbitrary coefficient
		// and make sure it's between throttleClamp and 1

		maxThrottle = CLAMP(0.05f * dist, throttleClamp, 1.0f);
		mmInputEventSetValueF32(mm, MIVI_F32_DIRECTION_SCALE, maxThrottle, milliseconds);

		if (dist > gConf.fFollowTargetEntityDistance)
		{				
			mmInputEventSetValueBit(mm, MIVI_BIT_FORWARD, 1, milliseconds, 0);

			gclPlayerControl_HandleThrottle( e, entToTarg, dist, 10.0f );
		}
		else
		{
			mmInputEventSetValueBit(mm, MIVI_BIT_FORWARD, 0, milliseconds, 0);

			gclPlayerControl_HandleThrottle( e, NULL, -1.0f, 0.0f );
		}
		mmInputEventSetValueBit(mm, MIVI_BIT_BACKWARD, 0, milliseconds, 0);
	}
	else
	{
		gPlayerControlData.bFollowEntity = false;
		gPlayerControlData.bFollowUntilCombatOrRange = false;
		s_currentFollowTargetRef = 0;
		//mmInputEventSetValueF32(mm, MIVI_F32_PITCH, 0, milliseconds);
		gclPlayerControl_mmInputEventF32(mm, MIVI_F32_PITCH, 0.f, milliseconds);
	}
}

void gclPlayerControl_NavToPos(MovementManager* mm,
							   Entity *e, 
							   U32 milliseconds,
							   F32 fTargetCamYaw)
{
	Vec3 vEntPos;
	Vec3 vTargetPos;
	Vec3 vDir;
	F32 throttleClamp = 0.5f; // Must be greater than turnOnlyZone
	F32 maxThrottle;
	F32 fDistance;

	entGetPos(e,vEntPos);
	copyVec3(gPlayerControlData.vCurrentNavTargetPos,vTargetPos);
	subVec3(vTargetPos,vEntPos,vDir);

	fDistance = entGetDistance( e, NULL, NULL, vTargetPos, NULL );

	maxThrottle = CLAMP(0.05f * fDistance, throttleClamp, 1.0f);
	mmInputEventSetValueF32(mm, MIVI_F32_DIRECTION_SCALE, maxThrottle, milliseconds);

	if (	(!gPlayerControlData.bFollowUntilCombatOrRange && fDistance > s_fNavToPosCloseDistance) 
		||	(gPlayerControlData.bFollowUntilCombatOrRange && fDistance > gPlayerControlData.fFollowMinRange) )
	{				
		mmInputEventSetValueBit(mm, MIVI_BIT_FORWARD, 1, milliseconds, 0);

		gclPlayerControl_HandleThrottle( e, vDir, fDistance, gPlayerControlData.fFollowMinRange );

		if ( !gclHandleEntityAvoidance( e, mm, milliseconds, vTargetPos, vDir, 50.0f ) )
		{
			//Set pitch and yaw
			gclPlayerControl_SetPitchAndYawFromDirection(e, mm, vDir, milliseconds);
		}
	}
	else
	{
		//cancel nav if we get close to the desired position
		mmInputEventSetValueBit(mm, MIVI_BIT_FORWARD, 0, milliseconds, 0);
		//mmInputEventSetValueF32(mm, MIVI_F32_PITCH, 0, milliseconds);
		gclPlayerControl_mmInputEventF32(mm, MIVI_F32_PITCH, 0, milliseconds);
		gPlayerControlData.bFollowEntity = false;
		gPlayerControlData.bNavToPos = false;
		gclPlayerControl_HandleThrottle( e, NULL, -1.0f, 0.0f );
	}
	mmInputEventSetValueBit(mm, MIVI_BIT_BACKWARD, 0, milliseconds, 0);
}


bool g_DisableTurnToFaceThisFrame;

void gclPlayerControl_DisableTurnToFaceThisFrame()
{
	g_DisableTurnToFaceThisFrame = true;
}

bool g_DisableFollowThisFrame;

void gclPlayerControl_DisableFollowThisFrame()
{
	g_DisableFollowThisFrame = true;
}

static bool gclPlayerControl_ShouldOnlySetMovementYawInput(Entity* pEnt)
{
	PowerActivation* pCurrAct = NULL;
	if (g_CombatConfig.bPlayerControlSetMoveAndFaceYawDuringPowerActivation)
		return false;

	pCurrAct = SAFE_MEMBER2(pEnt, pChar, pPowActCurrent);
	if (pCurrAct)
	{
		if (!pEnt->pChar->bDisableFaceActivate && 
			g_CombatConfig.bFaceActivateSticky)
		{
			return true;
		}
		else
		{
			PowerDef* pPowDef = GET_REF(pCurrAct->hdef);
			PowerTarget* pPowTarget = pPowDef ? GET_REF(pPowDef->hTargetMain) : NULL;
			
			if (pPowTarget && pPowTarget->bFaceActivateSticky)
			{
				return true;
			}
		}
	}
	return false;
}

void gclPlayerControl_DefaultScheme(MovementManager* mm,
									Entity *e, 
									F32 fDTime,
									U32 milliseconds,
									F32 fTargetCamYaw,
									F32 fTargetCamPitch)
{
	S32 setMoveFace = false;

	// Scale
	mmInputEventSetValueF32(mm, MIVI_F32_DIRECTION_SCALE, 1.0f, milliseconds);

	// Turn-to-face will rotate the player towards the camera facing direction
	if (gclCamera_ShouldTurnToFace() && !g_DisableTurnToFaceThisFrame)
	{
		bool bSetPitch = false;
		F32 fMoveYaw, fFaceYaw, fFacePitch;
		GfxCameraController* pCamera = &gGCLState.pPrimaryDevice->gamecamera;
		CameraSettings* pSettings = gclCamera_GetSettings(pCamera);
		
		fFaceYaw = pCamera->campyr[1];
		fFacePitch = pCamera->campyr[0];

		if (e->pPlayer->bUseFacingPitch || gPlayerControlData.bSTOSpaceshipMovement)
		{
			bSetPitch = true;
		}
		if (e->pPlayer->fMovementThrottle >= 0)
		{
			fFacePitch *= -1.0f;
			fFaceYaw = addAngle(fFaceYaw, PI);
		}
		fMoveYaw = fFaceYaw;

		if (gclPlayerControl_ShouldOnlySetMovementYawInput(e))
		{
			gPlayerControlData.fCurMoveYaw = fMoveYaw;
			mmInputEventSetValueF32(mm, MIVI_F32_MOVE_YAW, fMoveYaw, milliseconds);
			return;
		}
		// Adjust the player facing direction so that the player is aiming at the camera target point
		if (pSettings->bAdjustFacingTowardsCameraTarget && gclCamera_IsInMode(kCameraMode_ShooterCamera))
		{
			F32 fRange = ENTITY_DEFAULT_SEND_DISTANCE;
			if (g_CurrentScheme.bCheckActiveWeaponRangeForTargeting)
			{
				fRange = gclClientTarget_GetActiveWeaponRange(e);
			}
			gclCamera_GetAdjustedPlayerFacing(pCamera, e, fRange, &fFaceYaw, &fFacePitch);
		}
		if (bSetPitch)
		{
			gclPlayerControl_mmInputEventF32(mm, MIVI_F32_PITCH, fFacePitch, milliseconds);
		}
		gclPlayerControl_SetMoveAndFaceYaw(mm, fMoveYaw, fFaceYaw, milliseconds);
		return;
	}

	// move bSTOSpaceshipMovement to it's own player control mode
	if (gPlayerControlData.bSTOSpaceshipMovement )
	{	// todo: overriding schemes for game/region/zone
		gclPlayerControl_mmInputEventF32(mm, MIVI_F32_PITCH, 0, milliseconds);
		gclPlayerControl_SetMoveAndFaceYaw(mm, fTargetCamYaw, fTargetCamYaw, milliseconds);
		return; 
	}

	// we are in the facing forward mode
	if(gPlayerControlData.bMouseLook_Forced)
	{
		gclPlayerControl_SetMoveAndFaceYaw(mm, fTargetCamYaw, fTargetCamYaw, milliseconds);
		
		// Pitch
		
		if(gclPlayerControl_IsPlayerFlying(e) || e->pPlayer->bUseFacingPitch)
			gclPlayerControl_mmInputEventF32(mm, MIVI_F32_PITCH, -fTargetCamPitch, milliseconds);
		else if (!nearSameF32(gPlayerControlData.fCurPitch, 0.0f))
			gclPlayerControl_mmInputEventF32(mm, MIVI_F32_PITCH, 0.0f, milliseconds);

		setMoveFace = true;

	}
	else if (!gPlayerControlData.bMouseLook_Free && !gPlayerControlData.bCameraIsFree && !g_DisableTurnToFaceThisFrame)
	{	
		// if we're not in free look mode, and the camera is not free make sure the player
		// face and movement is matching the target camera yaw
		if (!nearSameF32(fTargetCamYaw, gPlayerControlData.fCurMoveYaw))
		{
			setMoveFace = true;
			gclPlayerControl_SetMoveAndFaceYaw(mm, fTargetCamYaw, fTargetCamYaw, milliseconds);
		}
		
		// do the same of the pitch if we are flying
		if(gclPlayerControl_IsPlayerFlying(e)){
			if(!nearSameF32(-fTargetCamPitch, gPlayerControlData.fCurPitch)){
				gclPlayerControl_mmInputEventF32(mm, MIVI_F32_PITCH, -fTargetCamPitch, milliseconds);
			}
		}
		else if (!nearSameF32(gPlayerControlData.fCurPitch, 0.0f)){
			gclPlayerControl_mmInputEventF32(mm, MIVI_F32_PITCH, 0.0f, milliseconds);
		}
	}

	if (!g_DisableTurnToFaceThisFrame)
	{
		gclPlayerControl_SetMoveAndFaceYaw(	mm,
											gPlayerControlData.fCurMoveYaw,
											gPlayerControlData.fCurFaceYaw,
											milliseconds);
	}

	gclPlayerControl_mmInputEventF32(	mm,
										MIVI_F32_PITCH,
										gPlayerControlData.fCurPitch,
										milliseconds);

	if (gPlayerControlData.bMouseLook_Free || gPlayerControlData.bCameraIsFree)
	{	
		if ((gPlayerControlData.bTurnRight ^ gPlayerControlData.bTurnLeft))
		{	// todo: keyboard turning speed should probably be in the control schemes
			F32 fDeltaYaw = gclCamera_GetKeyboardYawTurnSpeed() * fDTime;
			F32 fNewYaw;
			if (gPlayerControlData.bTurnLeft)
				fDeltaYaw = -fDeltaYaw;

			fNewYaw = addAngle(gPlayerControlData.fCurMoveYaw, fDeltaYaw);
			gclPlayerControl_SetMoveAndFaceYaw(mm, fNewYaw, fNewYaw, milliseconds);
		}
	}


	// todo: find out if we still need this
	if (! setMoveFace && !g_DisableTurnToFaceThisFrame)
	{
		// if the camera was moved via the free look, and the use is no longer trying to move the camera 
		// and then trying to move
		if ((g_CurrentScheme.eCameraFollowType != kCameraFollowType_Never || gPlayerControlData.bMode_FacingTarget) 
			&& !gPlayerControlData.bMouseLook_Free && gPlayerControlData.bCameraIsFree && isPlayerMovingEx(true))
		{
			bool bMatchPitch = gclPlayerControl_IsPlayerFlying(e);

			gPlayerControlData.bCameraIsFree = false;

			gclCamera_MatchPlayerFacing(bMatchPitch, false, true, -fTargetCamPitch);

			if (gPlayerControlData.bMode_FacingTarget)
			{	// change the movement/facing if we are facing a target
				F32 newTargetYaw = addAngle(gGCLState.pPrimaryDevice->gamecamera.targetpyr[1], PI);
				gclPlayerControl_SetMoveAndFaceYaw(mm, newTargetYaw, newTargetYaw, milliseconds);
			}
			
			gclPlayerControl_SetPitch(e, mm, -fTargetCamPitch, milliseconds);
		}

	}
	g_DisableTurnToFaceThisFrame = false;
}
// ------------------------------------------------------------------------------------------------------------------------------------
void gclPlayerControl_Update(Entity *e,
							 MovementManager*	mm,
							 F32 fDTime,
							 U32 milliseconds)
{
	F32 				fStickScale;
	F32 				fStickPitch;
	F32 				fStickYaw;
	F32					fStickRightX, fStickRightY;
	F32 				camYaw;
	F32 				camPitch;
	F32					targetCamYaw;
	F32					targetPitch;

	PERFINFO_AUTO_START_FUNC_PIX();

	if(!e || !e->pPlayer || !e->pChar)
	{
		PERFINFO_AUTO_STOP_FUNC_PIX();
		return;
	}

	if(!mmGetLocalManagerByIndex(&mm, 0))
	{
		PERFINFO_AUTO_STOP_FUNC_PIX();
		return;
	}


	gclUtil_GetStick(e, &fStickScale, &fStickPitch, &fStickYaw);
	gamepadGetRightStick(&fStickRightX, &fStickRightY);

	// Trigger throttle
	if (gPlayerControlData.bSTOSpaceshipRightTriggerThrottle)
	{
		F32 fNewThrottle;
		gamepadGetTriggerValues(NULL, &fNewThrottle);
		fNewThrottle = CLAMP(fNewThrottle, 0.f, 1.f);

		if(e && e->pPlayer
			&& abs(e->pPlayer->fMovementThrottle - fNewThrottle) < 0.001)
		{
			e->pPlayer->fMovementThrottle = fNewThrottle;
			ServerCmd_MovementThrottleSet(fNewThrottle);
		}
	}

	// Get the reference angles
	if (gPlayerControlData.bSTOSpaceshipMovement && e)
	{
		Vec3 entDir;
		entGetPosDir(e, NULL, entDir);
		entDir[1] = 0;
		camYaw = getVec3Yaw(entDir);
		targetCamYaw = camYaw;
	}
	else
	{
		camYaw = addAngle(gGCLState.pPrimaryDevice->gamecamera.campyr[1], PI);
		targetCamYaw = addAngle(gGCLState.pPrimaryDevice->gamecamera.targetpyr[1], PI);
	}

	// Reference pitch, compensating for the fact that pitch influences direction while flying
	camPitch = gGCLState.pPrimaryDevice->gamecamera.campyr[0];
	targetPitch = gGCLState.pPrimaryDevice->gamecamera.targetpyr[0];
	

	// If any directional key is pressed or the joystick is moved, stop following. 
	if (isPlayerMoving() || fStickScale>0.0f || g_DisableFollowThisFrame)
	{
		if ( gPlayerControlData.bFollowEntity || gPlayerControlData.bNavToPos )
		{
			gclPlayerControl_mmInputEventF32(mm, MIVI_F32_PITCH, 0.f, milliseconds);
		}
		gPlayerControlData.bFollowEntity = false;
		gPlayerControlData.bNavToPos = false;
		g_DisableFollowThisFrame = false;
	}

	gclPlayerControl_UpdateFacingTargetInfo(e);
	gclPlayerControl_UpdateTurnStrafeToggle();
	
	//
	// Tell the Movement Manager what to do
	//

	// Tilt
	//   STO (flight) uses this. It seems to be a way to get an analog up/down. I think it may overlap with pitch.  --poz
	if (gPlayerControlData.bSTOSpaceshipMovement)
		mmInputEventSetValueF32(mm, MIVI_F32_TILT, fStickPitch, milliseconds);


	if(fStickScale > 0.0f)
	{	// If there is controller input, ignore everything but controls 
		gclPlayerControl_ControllerMovement(mm, camYaw, camPitch, milliseconds, fStickScale, fStickPitch, fStickYaw);
	}
	else if(TRUE_THEN_RESET(gPlayerControlData.bWasMovingWithJoystick))
	{	
		mmInputEventSetValueBit(mm, MIVI_BIT_FORWARD, 0, milliseconds, 0); // Not sure this is needed -- poz
	}
	else if (gPlayerControlData.bFollowEntity)
	{	
		gclPlayerControl_FollowTargetEntity(mm, e, milliseconds);
	}
	else if (gPlayerControlData.bNavToPos)
	{
		gclPlayerControl_NavToPos(mm, e, milliseconds, targetCamYaw);
	}
	else // general case
	{
		if (!gGCLState.bLockPlayerAndCamera)
		{
			// Direction
			gclUpdateAllControls();

			gclPlayerControl_DefaultScheme(mm, e, fDTime, milliseconds, targetCamYaw, targetPitch);
		}
	}


	PERFINFO_AUTO_STOP_FUNC_PIX();
}

bool gclPlayerControl_ShouldAutoUnholster(bool bControlSchemeChanged)
{
	ZoneMapType eMapType = zmapInfoGetMapType(NULL);
	switch (eMapType)
	{
		case ZMTYPE_PVP:
		case ZMTYPE_QUEUED_PVE:
		{
			return true;
		}
		case ZMTYPE_OWNED:
		case ZMTYPE_MISSION:
		case ZMTYPE_SHARED:
		case ZMTYPE_STATIC:
		{
			if (bControlSchemeChanged)
			{
				return !zmapInfoGetPowersRequireValidTarget(NULL);
			}
		}
	}
	return false;
}


AUTO_CMD_INT(gPlayerControlData.bSTOSpaceshipRightTriggerThrottle, STOSpaceshipRightTriggerThrottle) ACMD_ACCESSLEVEL(4);

// TODO: move when we get some subclassing
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE;
void STOSpaceshipMovement(int activate)
{
	if (activate)
	{
		MovementManager *mm;
		gPlayerControlData.bSTOSpaceshipMovement = true;

		if(!mmGetLocalManagerByIndex(&mm, 0))
		{
			return;
		}

		gclPlayerControl_mmInputEventF32(mm, MIVI_F32_PITCH, 0.f, 0);
	}
	else
	{
		gPlayerControlData.bSTOSpaceshipMovement = false;
	}
}


void toggleFollowCB(void)
{
	Entity *pEnt = entActivePlayerPtr();
	if (pEnt && pEnt->pChar)
	{	
		if (gPlayerControlData.bFollowEntity && pEnt->pChar->currentTargetRef && gclPlayerControl_AllowFollowTargetByRef(pEnt->pChar->currentTargetRef))
		{
			s_currentFollowTargetRef = pEnt->pChar->currentTargetRef;
		}
		else if (gPlayerControlData.bFollowEntity && pEnt->pChar->erTargetDual && gclPlayerControl_AllowFollowTargetByRef(pEnt->pChar->erTargetDual))
		{
			s_currentFollowTargetRef = pEnt->pChar->erTargetDual;
		}
		else
		{
			//let gclMovePlayer handle the rest
			s_currentFollowTargetRef = 0;
		}
	}
}

// SetFollow: toggle follow
AUTO_CMD_INT(gPlayerControlData.bFollowEntity, SetFollow) ACMD_ACCESSLEVEL(0) ACMD_CALLBACK(toggleFollowCB);

AUTO_CMD_INT(gControlBtnStates[0].hardTargetLock, HardTargetLock) ACMD_ACCESSLEVEL(0);

bool gclPlayerControl_IsHardLockPressed()
{
	return gControlBtnStates[0].hardTargetLock && g_CombatConfig.bPlayerControlsAllowHardTarget;
}

// Follow: Follows the targeted entity
AUTO_COMMAND ACMD_NAME("Follow") ACMD_ACCESSLEVEL(0);
void cmdFollowTarget(void)
{
	Entity *pEnt = entActivePlayerPtr();
	if (pEnt && pEnt->pChar && gclPlayerControl_AllowFollowTargetByRef(pEnt->pChar->currentTargetRef))
	{
		gPlayerControlData.bFollowEntity = true;
		gPlayerControlData.bNavToPos = false;
		gPlayerControlData.bFollowUntilCombatOrRange = false;
		s_currentFollowTargetRef = pEnt->pChar->currentTargetRef;
	}
	else if (pEnt && pEnt->pChar && gclPlayerControl_AllowFollowTargetByRef(pEnt->pChar->erTargetDual))
	{
		gPlayerControlData.bFollowEntity = true;
		gPlayerControlData.bNavToPos = false;
		gPlayerControlData.bFollowUntilCombatOrRange = false;
		s_currentFollowTargetRef = pEnt->pChar->erTargetDual;
	}
}

AUTO_COMMAND ACMD_NAME("Follow_Resume") ACMD_ACCESSLEVEL(0);
void cmdFollowTarget_Resume(void)
{
	Entity *pEnt = entActivePlayerPtr();
	if (pEnt && pEnt->pChar && s_currentFollowTargetRef)
	{
		gPlayerControlData.bFollowEntity = true;
		gPlayerControlData.bNavToPos = false;
	}
}

AUTO_COMMAND ACMD_NAME("Follow_Cancel") ACMD_ACCESSLEVEL(0);
void cmdFollowTarget_Cancel(void)
{
	gPlayerControlData.bFollowEntity = false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PlayerIsUsingFollow");
bool exprPlayerIsUsingFollow(void)
{
	return gPlayerControlData.bFollowEntity;
}

AUTO_COMMAND ACMD_NAME("FollowUntilInCombatOrInRange") ACMD_ACCESSLEVEL(0);
void cmdFollowTargetUntilInCombat( F32 fMinRange )
{
	Entity *pEnt = entActivePlayerPtr();
	if (pEnt && pEnt->pChar)
	{
		bool bSuccess = false;
		if ( pEnt->pChar->currentTargetRef )
		{
			gPlayerControlData.bFollowEntity = true;
			gPlayerControlData.bNavToPos = false;
			s_currentFollowTargetRef = pEnt->pChar->currentTargetRef;
			bSuccess = true;
		}
		else if ( GET_REF(pEnt->pChar->currentTargetHandle) )
		{
			gPlayerControlData.bFollowEntity = false;
			gPlayerControlData.bNavToPos = true;
			gPlayerControlData.bHasUsedNav = true;
			wlInteractionNodeGetWorldMid(GET_REF(pEnt->pChar->currentTargetHandle),gPlayerControlData.vCurrentNavTargetPos);
			bSuccess = true;
		}

		if ( bSuccess )
		{
			gPlayerControlData.bNavDisableAutoThrottleAdjust = false;
			gPlayerControlData.bFollowUntilCombatOrRange = true;
			gPlayerControlData.fFollowMinRange = fMinRange;
		}
	}
}

AUTO_COMMAND ACMD_NAME("NavToXYZ") ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void cmdNavToXYZ(F32 x, F32 y, F32 z, F32 fCloseDist)
{
	Entity *pEnt = entActivePlayerPtr();
	if (pEnt && pEnt->pChar && pEnt->pPlayer)
	{
		F32 fDistance;
		setVec3(gPlayerControlData.vCurrentNavTargetPos,x,y,z);
		fDistance = entGetDistance(pEnt, NULL, NULL, gPlayerControlData.vCurrentNavTargetPos, NULL);
		
		if (fDistance > s_fNavToPosCloseDistance)
		{
			gPlayerControlData.bNavDisableAutoThrottleAdjust = false;
			gPlayerControlData.bFollowEntity = false;
			gPlayerControlData.bNavToPos = true;
			gPlayerControlData.fFollowMinRange = fCloseDist;
			gPlayerControlData.bFollowUntilCombatOrRange = true;
			gPlayerControlData.bHasUsedNav = true;
		}
	}
}

AUTO_COMMAND ACMD_NAME("NavToPosition") ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0);
void cmdNavToPosition(F32 x, F32 y, F32 z)
{
	cmdNavToXYZ(x, y, z, s_fNavToPosCloseDistance);
}

AUTO_COMMAND ACMD_NAME("NavToXZ") ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void cmdNavToXZ( F32 x, F32 z, F32 fCloseDist )
{
	Entity *pEnt = entActivePlayerPtr();
	if (pEnt && pEnt->pChar && pEnt->pPlayer)
	{
		Vec3 vPos;
		entGetPos(pEnt,vPos);
		cmdNavToXYZ(x, vPos[1], z, fCloseDist);
	}
}

//will navigate to an xz point and will clamp the y if the entity is farther than yrange from the y position
AUTO_COMMAND ACMD_NAME("NavToXZWithRangeY") ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void cmdNavToXZWithRangeY( F32 x, F32 y, F32 z, F32 yrange, F32 fCloseDist )
{
	Entity *pEnt = entActivePlayerPtr();
	if (pEnt && pEnt->pChar && pEnt->pPlayer)
	{
		Vec3 vPos;
		entGetPos(pEnt,vPos);
		cmdNavToXYZ(x,MINF(MAXF(vPos[1],y-yrange),y+yrange),z, fCloseDist);
	}
}

AUTO_COMMAND ACMD_NAME("NavToXZWithRangeYCompress") ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void cmdNavToXZWithRangeYCompress( F32 x, F32 y, F32 z, F32 yrange, F32 min, F32 max, F32 fCloseDist )
{
	Entity *pEnt = entActivePlayerPtr();
	if (pEnt && pEnt->pChar && pEnt->pPlayer)
	{
		Vec3 vPos;
		F32 fPosYScale, fNavY, fOffset, fMin, fMax;
		entGetPos(pEnt,vPos);
		fPosYScale = (vPos[1]-min)/(max-min);
		fOffset = (max-min)*0.1f;
		fMin = MAX(y-yrange,min+fOffset);
		fMax = MIN(y+yrange,max-fOffset);
		fNavY = interpF32(fPosYScale, fMin, fMax);
		cmdNavToXYZ(x,fNavY,z,fCloseDist);
	}
}

AUTO_COMMAND ACMD_NAME("NavTo_Resume") ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void cmdNavTo_Resume(void)
{
	Entity *pEnt = entActivePlayerPtr();
	if (pEnt && gPlayerControlData.bHasUsedNav)
	{
		gPlayerControlData.bNavDisableAutoThrottleAdjust = false;
		gPlayerControlData.bNavToPos = true;
		gPlayerControlData.bFollowEntity = false;
		gPlayerControlData.bFollowUntilCombatOrRange = true;
	}
}

AUTO_COMMAND ACMD_NAME("NavTo_Cancel") ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void cmdNavTo_Cancel(void)
{
	gPlayerControlData.bNavToPos = false;
}

AUTO_COMMAND ACMD_NAME("NavToSpawn_ReceivePosition") ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD;
void cmdNavToSpawn_ReceivePosition(Vec3 vPosition)
{
	cmdNavToXYZ(vPosition[0], vPosition[1], vPosition[2], s_fNavToPosCloseDistance);
}
	

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PlayerIsUsingNavTo");
bool exprPlayerIsUsingNavTo(void)
{
	return gPlayerControlData.bNavToPos;
}

AUTO_CMD_INT(s_bMouseLookFree, camRotate) ACMD_CALLBACK(cmdHandleMouseLook) ACMD_ACCESSLEVEL(0) ACMD_PRODUCTS(FightClub, StarTrek);
AUTO_CMD_INT(s_bMouseLookForced, camMouseLook) ACMD_CALLBACK(cmdHandleMouseLook) ACMD_ACCESSLEVEL(0) ACMD_PRODUCTS(FightClub, StarTrek);
void cmdHandleMouseLook(CMDARGS)
{
	if (gPlayerControlData.bAlwaysUseMouseLookForced)
	{
		s_bMouseLookForced = false;
	}
	gclPlayerControl_UpdateMouseInput(s_bMouseLookFree, s_bMouseLookForced && !s_bFreeMouseCursorOn);
}

AUTO_CMD_INT(gPlayerControlData.bSuspendForcedMouselook, suspendForcedMouselook) ACMD_CALLBACK(cmdHandleForceMouseLookSuspend) ACMD_ACCESSLEVEL(0);
void cmdHandleForceMouseLookSuspend(CMDARGS)
{
	static bool bSuspendedLastCall = false;

	if (!gPlayerControlData.bSuppressForcedMouseLookThisFrame)
		gclPlayerControl_UpdateMouseInput(false, !gPlayerControlData.bSuspendForcedMouselook);

	if (gPlayerControlData.bSuspendForcedMouselook)
	{
		if (!gPlayerControlData.bSuppressForcedMouseLookThisFrame && !bSuspendedLastCall)
		{
			// Only move the cursor to the middle of the screen
			// if the mouse is really locked this frame
			// and we didn't have it locked the last call
			mouseSetScreenPercent(0.5f, 0.5f);
		}		
	}
	bSuspendedLastCall = gPlayerControlData.bSuspendForcedMouselook;
}

void cmdHandleForward(CMDARGS);
void cmdHandleBackward(CMDARGS);
void cmdHandleLeft(CMDARGS);
void cmdHandleRight(CMDARGS);
void cmdHandleAutoForward(CMDARGS);

void gclPlayerControl_StopMoving( bool stopAutorunToo )
{
	gControlBtnStates[0].forward = 0;
	cmdHandleForward(NULL, NULL);
	gControlBtnStates[0].backward = 0;
	cmdHandleBackward(NULL, NULL);
	gControlBtnStates[0].left = 0;
	cmdHandleLeft(NULL, NULL);
	gControlBtnStates[0].right = 0;
	cmdHandleRight(NULL, NULL);
	if( stopAutorunToo ) {
		gControlBtnStates[0].autoForward = 0;
		cmdHandleAutoForward(NULL, NULL);
	}
}

//Disables cursor mode and returns to action combat if the param is true, otherwise does nothing
AUTO_COMMAND ACMD_NAME("DisableCursorMode") ACMD_ACCESSLEVEL(0);
void cmdDisableCursorMode(bool bDisable)
{
	if(bDisable)
	{
		gPlayerControlData.bSuspendForcedMouselook = false;
		cmdHandleForceMouseLookSuspend(NULL, NULL);
	}
}

//Alters the forceMouselook mode and stops the player from moving at the same time
AUTO_CMD_INT(gPlayerControlData.bSuspendForcedMouselook, suspendForcedMouselookAndStopMoving) ACMD_CALLBACK(cmdHandleSuspendForcedMouselookAndStopMoving) ACMD_ACCESSLEVEL(0);
void cmdHandleSuspendForcedMouselookAndStopMoving(CMDARGS)
{
	cmdHandleForceMouseLookSuspend(NULL, NULL);
	gclPlayerControl_StopMoving( false );
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CameraSuppressForcedMouseLockThisFrame");
void exprCameraSuppressForcedMouseLockThisFrame(bool bSuppress)
{
	gPlayerControlData.bSuppressForcedMouseLookThisFrame = bSuppress;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("SetCursorPosScreenPercent");
void exprSetCursorPosScreenPercent(F32 fPctX, F32 fPctY)
{
	mouseSetScreenPercent(fPctX, fPctY);
}

AUTO_COMMAND ACMD_NAME(freeMouseCursor) ACMD_ACCESSLEVEL(0);
void gclPlayerControl_FreeMouseCursor(int bOn)
{
	s_bFreeMouseCursorOn = bOn;
	gclPlayerControl_UpdateMouseInput(s_bMouseLookFree, s_bMouseLookForced && !s_bFreeMouseCursorOn);
}


AUTO_COMMAND ACMD_NAME("HandleMouseLookToggleOrEsc") ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void cmdHandleMouseLookToggleOrEsc()
{
	if (!gclCursorMode_IsDefault())
	{
		gclCursorMode_ChangeToDefault();
	}
	else
	{
		s_bMouseLookForced = !s_bMouseLookForced;
		gclPlayerControl_UpdateMouseInput(s_bMouseLookFree, s_bMouseLookForced);
	}
}

void gclResetMouseLookFlags(void)
{
	s_bMouseLookFree = false;
	s_bMouseLookForced = false;
}

bool getControlButtonState(int index)
{
	switch (index)
	{
	case MIVI_BIT_FORWARD:
		return gControlBtnStates[0].forward | gControlBtnStates[0].autoForward | gControlBtnStates[0].mouseForward;
	case MIVI_BIT_BACKWARD:
		return gControlBtnStates[0].backward;
	case MIVI_BIT_RIGHT:
		return CONTROLS_RIGHT(0);
	case MIVI_BIT_LEFT:
		return CONTROLS_LEFT(0);
	case MIVI_BIT_UP:
		return gControlBtnStates[0].up;
	case MIVI_BIT_DOWN:
		return gControlBtnStates[0].down;
	case MIVI_BIT_SLOW:
		return gControlBtnStates[0].slow;
	default:
		return false;
	}
}

bool isPlayerMovingEx(bool bIgnoreTurning)
{
	return getControlButtonState(MIVI_BIT_FORWARD) || getControlButtonState(MIVI_BIT_BACKWARD) || 
		getControlButtonState(MIVI_BIT_LEFT) || getControlButtonState(MIVI_BIT_RIGHT) || 
		getControlButtonState(MIVI_BIT_UP) || getControlButtonState(MIVI_BIT_DOWN) || 
		(!bIgnoreTurning && (getControlButtonState(MIVI_BIT_TURN_LEFT) || getControlButtonState(MIVI_BIT_TURN_RIGHT))) ||
		getControlButtonState(MIVI_BIT_SLOW);
}

bool isPlayerAutoForward(void)
{
	return !!gControlBtnStates[0].autoForward;
}

static MovementManager* entGetActiveMovement(U32 controlledEntityIndex){
	MovementManager* mm;

	if(mmGetLocalManagerByIndex(&mm, controlledEntityIndex))
	{
		return mm;
	}

	return NULL;
}

static void updateControlTracked(	U32 controlledEntityIndex,
									MovementInputValueIndex mivi,
									S32 value,
									S32 canDoubleTap,
									S32 *pbIsDoubleTapOut,
									const char* fileName,
									U32 fileLine)
{
	// Ignore key-downs if this global is set.  Allow key-ups because ignoring those is bad.
	if(	gGCLState.bLockPlayerAndCamera &&
		value)
	{
		return;
	}

	mmInputEventSetValueBitTracked(	entGetActiveMovement(controlledEntityIndex),
									mivi,
									value,
									gclCmdGetTimeStamp(),
									canDoubleTap,
									pbIsDoubleTapOut,
									fileName,
									fileLine);
}

void gclUpdateAllControls(void)
{
	int i;

	for (i = 0; i < 1; i++)
	{
		updateControl(i, MIVI_BIT_FORWARD, CONTROLS_FORWARD(i), 0);
		updateControl(i, MIVI_BIT_BACKWARD, gControlBtnStates[i].backward, 0);
		updateControl(i, MIVI_BIT_UP, gControlBtnStates[i].up, 0);
		updateControl(i, MIVI_BIT_DOWN, gControlBtnStates[i].down, 0);
		updateControl(i, MIVI_BIT_LEFT, CONTROLS_LEFT(i), 0);
		updateControl(i, MIVI_BIT_RIGHT, CONTROLS_RIGHT(i), 0);
		updateControl(i, MIVI_BIT_SLOW, gControlBtnStates[i].slow, 0);
	}
}

void gclPlayerControl_SetAutoForward(int bAutoForwardEnabled) 
{
	gControlBtnStates[0].autoForward = bAutoForwardEnabled;
}

void gclTurnOffAllControlBits(void)
{
	int i;
	for (i = 0; i < 1; i++)
	{
		gControlBtnStates[i].forward = gControlBtnStates[i].autoForward = 
			gControlBtnStates[i].mouseForward = gControlBtnStates[i].backward = 
			gControlBtnStates[i].up = gControlBtnStates[i].down = 
			gControlBtnStates[i].left = gControlBtnStates[i].right = 
			gControlBtnStates[i].slow = gControlBtnStates[i].turnleft = 
			gControlBtnStates[i].turnright = 0;

		updateControl(i, MIVI_BIT_FORWARD, 0, 0);
		updateControl(i, MIVI_BIT_BACKWARD, 0, 0);
		updateControl(i, MIVI_BIT_UP, 0, 0);
		updateControl(i, MIVI_BIT_DOWN, 0, 0);
		updateControl(i, MIVI_BIT_LEFT, 0, 0);
		updateControl(i, MIVI_BIT_RIGHT, 0, 0);
		updateControl(i, MIVI_BIT_SLOW, 0, 0);
	}
}

static void gclCancelAimToggle(int i)
{
	if(g_CurrentScheme.bAimModeAsToggle)
	{
		gControlBtnStates[i].lastaim = 0;
		updateControl(i, MIVI_BIT_AIM, 0, 0);
	}
}

static void gclCancelCrouchToggle(int i, bool bMove)
{
	if(g_CombatConfig.tactical.aim.bCrouchModeToggles && 
		(!bMove || g_CombatConfig.tactical.aim.bMovementCancelsCrouch))
	{
		gControlBtnStates[i].lastcrouch = 0;
		updateControl(i, MIVI_BIT_CROUCH, 0, 0);
	}
}

static void addDebugCommand(U32 controlledEntityIndex, const char* command){
	mmInputEventDebugCommand(	entGetActiveMovement(controlledEntityIndex),
								command,
								gclCmdGetTimeStamp());
}

AUTO_COMMAND ACMD_NAME(mmControlsDebugCommand);
void cmdMovementControlsDebugCommand(const char* command){
	addDebugCommand(0, command);
}

AUTO_COMMAND ACMD_NAME(mmControlsDebugCommand1);
void cmdMovementControlsDebugCommand1(const char* command){
	addDebugCommand(1, command);
}

AUTO_COMMAND ACMD_NAME(invertibleup) ACMD_ACCESSLEVEL(0);
void cmdInvertibleUp(int active)
{
	S32 i = 0;
	if (input_state.invertUpDown)
	{
		gControlBtnStates[i].down = active;
		updateControl(i, MIVI_BIT_DOWN, gControlBtnStates[i].down, 1);
	}
	else
	{
		gControlBtnStates[i].up = active;
		updateControl(i, MIVI_BIT_UP, gControlBtnStates[i].up, 1);
	}
}
AUTO_COMMAND ACMD_NAME(invertibledown) ACMD_ACCESSLEVEL(0);
void cmdInvertibleDown(int active)
{
	S32 i = 0;
	if (input_state.invertUpDown)
	{
		gControlBtnStates[i].up = active;
		updateControl(i, MIVI_BIT_UP, gControlBtnStates[i].up, 1);
	}
	else
	{
		gControlBtnStates[i].down = active;
		updateControl(i, MIVI_BIT_DOWN, gControlBtnStates[i].down, 1);
	}
}



AUTO_CMD_INT(gControlBtnStates[0].forward, forward) ACMD_CALLBACK(cmdHandleForward) ACMD_ACCESSLEVEL(0);
void cmdHandleForward(CMDARGS)
{
	S32 i = 0;
	S32 bIsDoubleTap = false;

	if (gControlBtnStates[i].forward)
	{
		gclPlayerControl_SetAutoForward(0);
		gclCancelCrouchToggle(i, true);
	}

	updateControlEx(i, MIVI_BIT_FORWARD, CONTROLS_FORWARD(i), gControlBtnStates[i].forward, &bIsDoubleTap);
	
	if (bIsDoubleTap)
	{
		gclCombatReactivePower_HandleDoubleTap(MIVI_BIT_FORWARD);
	}
}


AUTO_CMD_INT(gControlBtnStates[1].forward, forward1) ACMD_CALLBACK(cmdHandleForward1) ACMD_ACCESSLEVEL(0);
void cmdHandleForward1(CMDARGS)
{
	S32 i = 1;
	S32 bIsDoubleTap = false;
	
	if (gControlBtnStates[i].forward)
	{
		gclPlayerControl_SetAutoForward(0);
	}

	updateControlEx(i, MIVI_BIT_FORWARD, CONTROLS_FORWARD(i), gControlBtnStates[i].forward, &bIsDoubleTap);

	if (bIsDoubleTap)
	{
		gclCombatReactivePower_HandleDoubleTap(MIVI_BIT_FORWARD);
	}
}

AUTO_CMD_INT(gControlBtnStates[0].mouseForward, mouseForward) ACMD_CALLBACK(cmdHandleMouseForward) ACMD_ACCESSLEVEL(0);
void cmdHandleMouseForward(CMDARGS)
{
	S32 i = 0;
	if (gControlBtnStates[i].mouseForward)
	{
		gclPlayerControl_SetAutoForward(0);

	}
	updateControl(i, MIVI_BIT_FORWARD, gControlBtnStates[i].mouseForward, 0);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void setMouseForward(int enable)
{
	S32 i = 0;
	gControlBtnStates[i].mouseForward = enable;
	updateControl(i, MIVI_BIT_FORWARD, gControlBtnStates[i].mouseForward, 0);
}

AUTO_CMD_INT(gControlBtnStates[0].autoForward, autoForward) ACMD_CALLBACK(cmdHandleAutoForward) ACMD_ACCESSLEVEL(0);
void cmdHandleAutoForward(CMDARGS)
{
	gclPlayerControl_SetAutoForward(gControlBtnStates[0].autoForward);

	if (gControlBtnStates[0].autoForward)
	{
		gclCancelCrouchToggle(0, true);
	}
	
	gControlBtnStates[0].backward = 0;

	updateControl(0, MIVI_BIT_FORWARD, CONTROLS_FORWARD(0), 0);
	updateControl(0, MIVI_BIT_BACKWARD, gControlBtnStates[0].backward, 0);

}

AUTO_CMD_INT(gControlBtnStates[1].autoForward, autoForward1) ACMD_CALLBACK(cmdHandleAutoForward1) ACMD_ACCESSLEVEL(0);
void cmdHandleAutoForward1(CMDARGS)
{
	S32 i = 1;
	updateControl(i, MIVI_BIT_FORWARD, CONTROLS_FORWARD(i), 0);
}

AUTO_CMD_INT(gControlBtnStates[0].backward, backward) ACMD_CALLBACK(cmdHandleBackward) ACMD_ACCESSLEVEL(0);
void cmdHandleBackward(CMDARGS)
{
	S32 i = 0;
	S32 bIsDoubleTap = false;

	if (gControlBtnStates[i].backward)
	{
		gclPlayerControl_SetAutoForward(0);
		gclCancelCrouchToggle(i, true);
	}

	updateControlEx(i, MIVI_BIT_BACKWARD, gControlBtnStates[i].backward, 1, &bIsDoubleTap);

	if (bIsDoubleTap)
	{
		gclCombatReactivePower_HandleDoubleTap(MIVI_BIT_BACKWARD);
	}
}

AUTO_CMD_INT(gControlBtnStates[1].backward, backward1) ACMD_CALLBACK(cmdHandleBackward1) ACMD_ACCESSLEVEL(0);
void cmdHandleBackward1(CMDARGS)
{
	S32 i = 1;
	S32 bIsDoubleTap = false;

	if (gControlBtnStates[i].backward)
	{
		globCmdParse("autoForward1 0");
	}

	updateControlEx(i, MIVI_BIT_BACKWARD, gControlBtnStates[i].backward, 1, &bIsDoubleTap);

	if (bIsDoubleTap)
	{
		gclCombatReactivePower_HandleDoubleTap(MIVI_BIT_BACKWARD);
	}
}

AUTO_CMD_INT(gControlBtnStates[0].left, left) ACMD_CALLBACK(cmdHandleLeft) ACMD_ACCESSLEVEL(0);
void cmdHandleLeft(CMDARGS)
{
	S32 i = 0;
	S32 bIsDoubleTap = false;

	if(gControlBtnStates[i].left)
	{
		gclCancelCrouchToggle(i, true);
	}

	updateControlEx(i, MIVI_BIT_LEFT, CONTROLS_LEFT(i), gControlBtnStates[i].left, &bIsDoubleTap);

	if (bIsDoubleTap)
	{
		gclCombatReactivePower_HandleDoubleTap(MIVI_BIT_LEFT);
	}
}

AUTO_CMD_INT(gControlBtnStates[1].left, left1) ACMD_CALLBACK(cmdHandleLeft1) ACMD_ACCESSLEVEL(0);
void cmdHandleLeft1(CMDARGS)
{
	S32 i = 1;
	S32 bIsDoubleTap = false;

	updateControlEx(i, MIVI_BIT_LEFT, CONTROLS_LEFT(i), gControlBtnStates[i].left, &bIsDoubleTap);

	if (bIsDoubleTap)
	{
		gclCombatReactivePower_HandleDoubleTap(MIVI_BIT_LEFT);
	}
}

AUTO_CMD_INT(gControlBtnStates[0].right, right) ACMD_CALLBACK(cmdHandleRight) ACMD_ACCESSLEVEL(0);
void cmdHandleRight(CMDARGS)
{
	S32 i = 0;
	S32 bIsDoubleTap = false;

	if(gControlBtnStates[i].right)
	{
		gclCancelCrouchToggle(i, true);
	}

	updateControlEx(i, MIVI_BIT_RIGHT, CONTROLS_RIGHT(i), gControlBtnStates[i].right, &bIsDoubleTap);

	if (bIsDoubleTap)
	{
		gclCombatReactivePower_HandleDoubleTap(MIVI_BIT_RIGHT);
	}
}

AUTO_CMD_INT(gControlBtnStates[1].right, right1) ACMD_CALLBACK(cmdHandleRight1) ACMD_ACCESSLEVEL(0);
void cmdHandleRight1(CMDARGS)
{
	S32 i = 1;
	S32 bIsDoubleTap = false;

	updateControlEx(i, MIVI_BIT_RIGHT, CONTROLS_RIGHT(i), gControlBtnStates[i].right, &bIsDoubleTap);

	if (bIsDoubleTap)
	{
		gclCombatReactivePower_HandleDoubleTap(MIVI_BIT_RIGHT);
	}
}

void gclPlayerControl_SetForward(bool bEnabled)
{
	gControlBtnStates[0].forward = bEnabled;
	updateControl(0, MIVI_BIT_FORWARD, CONTROLS_FORWARD(0), gControlBtnStates[0].forward);

}

void gclPlayerControl_SetBack(bool bEnabled)
{
	gControlBtnStates[0].backward = bEnabled;
	updateControl(0, MIVI_BIT_BACKWARD, gControlBtnStates[0].backward, gControlBtnStates[0].backward);
}

void gclPlayerControl_SetLeft(bool bEnabled)
{
	gControlBtnStates[0].left = bEnabled;
	updateControl(0, MIVI_BIT_LEFT, CONTROLS_LEFT(0), gControlBtnStates[0].left);
}

void gclPlayerControl_SetRight(bool bEnabled)
{
	gControlBtnStates[0].right = bEnabled;
	updateControl(0, MIVI_BIT_RIGHT, CONTROLS_RIGHT(0), gControlBtnStates[0].right);
}


AUTO_CMD_INT(gControlBtnStates[0].up, up) ACMD_CALLBACK(cmdHandleUp) ACMD_ACCESSLEVEL(0);
void cmdHandleUp(CMDARGS)
{
	S32 i = 0;

	if(gControlBtnStates[i].up)
	{
		gclCancelAimToggle(i);
		gclCancelCrouchToggle(i, false);
	}
	updateControl(i, MIVI_BIT_UP, gControlBtnStates[i].up, 1);
}

AUTO_CMD_INT(gControlBtnStates[1].up, up1) ACMD_CALLBACK(cmdHandleUp1) ACMD_ACCESSLEVEL(0);
void cmdHandleUp1(CMDARGS)
{
	S32 i = 1;
	updateControl(i, MIVI_BIT_UP, gControlBtnStates[i].up, 1);
}

AUTO_CMD_INT(gControlBtnStates[0].down, down) ACMD_CALLBACK(cmdHandleDown) ACMD_ACCESSLEVEL(0);
void cmdHandleDown(CMDARGS)
{
	S32 i = 0;
	updateControl(i, MIVI_BIT_DOWN, gControlBtnStates[i].down, 1);
}

AUTO_CMD_INT(gControlBtnStates[1].down, down1) ACMD_CALLBACK(cmdHandleDown1) ACMD_ACCESSLEVEL(0);
void cmdHandleDown1(CMDARGS)
{
	S32 i = 1;
	updateControl(i, MIVI_BIT_DOWN, gControlBtnStates[i].down, 1);
}

static S32 turnUsesInputs;
AUTO_CMD_INT(turnUsesInputs, turnUsesInputs);

AUTO_CMD_INT(gControlBtnStates[0].turnleft, turnleft) ACMD_CALLBACK(cmdHandleTurnLeft) ACMD_ACCESSLEVEL(0);
void cmdHandleTurnLeft(CMDARGS)
{
	S32 i = 0;
	if(	turnUsesInputs &&
		gControlBtnStates[i].turnleft)
	{
		updateControl(i, MIVI_BIT_TURN_LEFT, 1, 0);
	}else{
		updateControl(i, MIVI_BIT_TURN_LEFT, 0, 0);

		if(!gPlayerControlData.bTurnIsStrafe){
			gclPlayerControl_SetTurnLeft(gControlBtnStates[i].turnleft);
		}else{
			if (gControlBtnStates[i].turnleft)
			{
				gclCancelCrouchToggle(i, true);
			}
			updateControl(i, MIVI_BIT_LEFT, CONTROLS_LEFT(i), gControlBtnStates[i].turnleft);
		}
	}
}

AUTO_CMD_INT(gControlBtnStates[1].turnleft, turnleft1) ACMD_CALLBACK(cmdHandleTurnLeft1) ACMD_ACCESSLEVEL(0);
void cmdHandleTurnLeft1(CMDARGS)
{
	S32 i = 1;
	updateControl(i, MIVI_BIT_LEFT, CONTROLS_LEFT(i), gControlBtnStates[i].turnleft);
}

AUTO_CMD_INT(gControlBtnStates[0].turnright, turnright) ACMD_CALLBACK(cmdHandleTurnRight) ACMD_ACCESSLEVEL(0);
void cmdHandleTurnRight(CMDARGS)
{
	S32 i = 0;
	if(	turnUsesInputs &&
		gControlBtnStates[i].turnright)
	{
		updateControl(i, MIVI_BIT_TURN_RIGHT, 1, 0);
	}else{
		updateControl(i, MIVI_BIT_TURN_RIGHT, 0, 0);

		if(!gPlayerControlData.bTurnIsStrafe){
			gclPlayerControl_SetTurnRight(gControlBtnStates[i].turnright);
		}else{
			if (gControlBtnStates[i].turnright)
			{
				gclCancelCrouchToggle(i, true);
			}
			updateControl(i, MIVI_BIT_RIGHT, CONTROLS_RIGHT(i), gControlBtnStates[i].turnright);
		}
	}
}

AUTO_CMD_INT(gControlBtnStates[1].turnright, turnright1) ACMD_CALLBACK(cmdHandleTurnRight1) ACMD_ACCESSLEVEL(0);
void cmdHandleTurnRight1(CMDARGS)
{
	S32 i = 1;
	updateControl(i, MIVI_BIT_RIGHT, CONTROLS_RIGHT(i), gControlBtnStates[i].turnright);
}

AUTO_CMD_INT(gControlBtnStates[0].roll, roll) ACMD_CALLBACK(cmdHandleRoll) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void cmdHandleRoll(CMDARGS)
{
	S32 i = 0;
	updateControl(i, MIVI_BIT_ROLL, gControlBtnStates[i].roll, 1);
}

AUTO_CMD_INT(gControlBtnStates[0].aim, aim) ACMD_CALLBACK(cmdHandleAim) ACMD_ACCESSLEVEL(0);
void cmdHandleAim(CMDARGS)
{
	S32				i = 0;
	Entity*			e = entActivePlayerPtr();
	RegionRules*	pRules = e ? getRegionRulesFromRegionType( entGetWorldRegionTypeOfEnt(e) ) : NULL;

	if(!SAFE_MEMBER(pRules, bAllowCrouchAndAim)){
		return;
	}

	if (gControlBtnStates[i].aim && e && e->pChar)
		gclEntity_NotifyIfAimUnavailable(e);

	if(g_CurrentScheme.bAimModeAsToggle){
		if(gControlBtnStates[i].aim){
			gControlBtnStates[i].lastaim = !gControlBtnStates[i].lastaim;
			updateControl(i, MIVI_BIT_AIM, gControlBtnStates[i].lastaim, 0);
		}
	}else{
		gControlBtnStates[i].lastaim = gControlBtnStates[i].aim;
		updateControl(i, MIVI_BIT_AIM, gControlBtnStates[i].aim, 1);
	}
}

AUTO_CMD_INT(gControlBtnStates[0].crouch, crouch) ACMD_CALLBACK(cmdHandleCrouch) ACMD_ACCESSLEVEL(0) ACMD_PRODUCTS(StarTrek, FightClub);
void cmdHandleCrouch(CMDARGS)
{
	S32				i = 0;
	Entity*			e = entActivePlayerPtr();
	RegionRules*	pRules = e ? getRegionRulesFromEnt(e) : NULL;

	if(!SAFE_MEMBER(pRules, bAllowCrouchAndAim) || !g_CombatConfig.tactical.aim.bSplitAimAndCrouch){
		return;
	}

	if(g_CombatConfig.tactical.aim.bCrouchModeToggles){
		if (gControlBtnStates[i].crouch){
			gControlBtnStates[i].lastcrouch = !gControlBtnStates[i].lastcrouch;
			updateControl(i, MIVI_BIT_CROUCH, gControlBtnStates[i].lastcrouch, 1);
		}
	}else{
		gControlBtnStates[i].lastcrouch = gControlBtnStates[i].crouch;
		updateControl(i, MIVI_BIT_CROUCH, gControlBtnStates[i].crouch, 1);
	}
}

AUTO_CMD_INT(gControlBtnStates[0].run, run) ACMD_CALLBACK(cmdHandleRun) ACMD_ACCESSLEVEL(0);
void cmdHandleRun(CMDARGS)
{
	S32 i = 0;

	if(gControlBtnStates[i].run)
	{
		gclCancelAimToggle(i);
		gclCancelCrouchToggle(i, false);
	}
	updateControl(i, MIVI_BIT_RUN, gControlBtnStates[i].run, 1);
}

AUTO_CMD_INT(gControlBtnStates[0].tactical, tactical) ACMD_CALLBACK(cmdHandleTactical) ACMD_ACCESSLEVEL(0);
void cmdHandleTactical(CMDARGS)
{
	S32 i = 0;
	updateControl(i, MIVI_BIT_TACTICAL, gControlBtnStates[i].tactical, 0);
}

// Both tacticalSpecial and specialClassPower are NW specific commands and presenting a pattern 
// that might benefit being wrapped into its own system. 

// Tactical special flag
static bool s_bTacticalSpecialToggleFlag = false;

AUTO_CMD_INT(s_bTacticalSpecialToggleFlag, tacticalSpecial) ACMD_CALLBACK(cmdHandleTacticalSpecial) ACMD_ACCESSLEVEL(0);
void cmdHandleTacticalSpecial(CMDARGS)
{
	Entity*			e = entActivePlayerPtr();
	TacticalRequesterAimDef *pAim = mrRequesterDef_GetAimDefForEntity(e, NULL);
	TacticalRequesterRollDef *pRoll = mrRequesterDef_GetRollDefForEntity(e, NULL);

	if (!e)
		return;

	if (e->pChar && e->pChar->pCombatReactivePowerInfo)
	{
		gclCombatReactivePower_Exec(s_bTacticalSpecialToggleFlag);
	}
	else if (!pRoll->bRollDisabled)
	{
		gControlBtnStates[0].roll = s_bTacticalSpecialToggleFlag;
		cmdHandleRoll(NULL, NULL);
	}
	else if (!pAim->bAimDisabled)
	{
		gControlBtnStates[0].aim = s_bTacticalSpecialToggleFlag;
		cmdHandleAim(NULL, NULL);
	}
	else if (g_CombatConfig.tactical.iTacticalSpecialFallbackPowerSlot >= 0)
	{
		gclPowerSlotExec(s_bTacticalSpecialToggleFlag, g_CombatConfig.tactical.iTacticalSpecialFallbackPowerSlot);
	}
}

// 
static bool s_bSpecialClassPowerToggleFlag = false;

AUTO_CMD_INT(s_bSpecialClassPowerToggleFlag, specialClassPower) ACMD_CALLBACK(cmdHandleSpecialClassPower) ACMD_ACCESSLEVEL(0);
void cmdHandleSpecialClassPower(CMDARGS)
{
	Entity*			e = entActivePlayerPtr();

	if (!e)
		return;

	if (e->pChar && gclCombatPowerStateSwitching_CanCycleState(e->pChar))
	{
		if (s_bSpecialClassPowerToggleFlag)
			gclCombatPowerStateSwitching_CycleNextState(e->pChar);
	}
	else if (g_CombatConfig.iSpecialClassPowerFallbackPowerSlot >= 0)
	{
		gclPowerSlotExec(s_bSpecialClassPowerToggleFlag, g_CombatConfig.iSpecialClassPowerFallbackPowerSlot);
	}
}


AUTO_CMD_INT(gControlBtnStates[0].slow, slow) ACMD_CALLBACK(cmdHandleSlow) ACMD_ACCESSLEVEL(0);
AUTO_CMD_INT(gControlBtnStates[0].slow, walk) ACMD_CALLBACK(cmdHandleSlow) ACMD_ACCESSLEVEL(0);
void cmdHandleSlow(CMDARGS)
{
	S32 i = 0;
	updateControl(i, MIVI_BIT_SLOW, gControlBtnStates[i].slow, 1);
}

AUTO_CMD_INT(gControlBtnStates[1].slow, slow1) ACMD_CALLBACK(cmdHandleSlow1) ACMD_ACCESSLEVEL(0);
AUTO_CMD_INT(gControlBtnStates[1].slow, walk1) ACMD_CALLBACK(cmdHandleSlow1) ACMD_ACCESSLEVEL(0);
void cmdHandleSlow1(CMDARGS)
{
	S32 i = 1;
	updateControl(i, MIVI_BIT_SLOW, gControlBtnStates[i].slow, 1);
}

AUTO_COMMAND ACMD_NAME("ThrottleToggle") ACMD_ACCESSLEVEL(0);
void cmdThrottleToggle(void)
{
	Entity *e = entActivePlayerPtr();
	if(e && e->pPlayer)
	{
		if (e->pPlayer->fMovementThrottle > 0) 
			e->pPlayer->fMovementThrottle = 0.0;
		else
			e->pPlayer->fMovementThrottle = 1.0;
		gPlayerControlData.bNavDisableAutoThrottleAdjust = true;
		ServerCmd_MovementThrottleSet(e->pPlayer->fMovementThrottle);
	}
}

AUTO_COMMAND ACMD_NAME("ThrottleSet") ACMD_ACCESSLEVEL(0);
void cmdThrottleSet(F32 fThrottle)
{
	Entity *e = entActivePlayerPtr();
	if(e && e->pPlayer)
	{
		fThrottle = CLAMP(fThrottle,-0.25f,1.f);
		e->pPlayer->fMovementThrottle = fThrottle;
		gPlayerControlData.fPredictedThrottle = fThrottle; 
		gPlayerControlData.uiLastThrottleAdjustTime = gGCLState.totalElapsedTimeMs;
		gPlayerControlData.bNavDisableAutoThrottleAdjust = true;
		ServerCmd_MovementThrottleSet(fThrottle);
	}
}

AUTO_COMMAND ACMD_NAME("ThrottleAdjust") ACMD_ACCESSLEVEL(0);
void cmdThrottleAdjust(F32 fThrottleDelta)
{
	Entity *e = entActivePlayerPtr();
	if(e && e->pPlayer)
	{
		F32 fThrottle;
		if (	e->pPlayer->fMovementThrottle != 0.0f
			&&	ABS(e->pPlayer->fMovementThrottle) <= ABS(fThrottleDelta)
			&&	SIGN(e->pPlayer->fMovementThrottle) != SIGN(fThrottleDelta) )
		{
			fThrottle = 0.0f;
		}
		else
		{
			fThrottle = CLAMP(e->pPlayer->fMovementThrottle + fThrottleDelta,-0.25f,1.f);
		}
		e->pPlayer->fMovementThrottle = fThrottle;
		gPlayerControlData.fPredictedThrottle = fThrottle; 
		gPlayerControlData.uiLastThrottleAdjustTime = gGCLState.totalElapsedTimeMs;
		gPlayerControlData.bNavDisableAutoThrottleAdjust = true;
		ServerCmd_MovementThrottleSet(fThrottle);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PlayerGetThrottleValue);
F32 exprPlayerGetThrottleValue(bool bUsePredicted)
{
	F32 fResult = 0.0f;
	if ( !bUsePredicted )
	{
		Entity *e = entActivePlayerPtr();
		if ( e && e->pPlayer )
		{
			fResult = e->pPlayer->fMovementThrottle;
		}
	}
	else
	{
		fResult = gPlayerControlData.fPredictedThrottle;
	}
	return fResult;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PlayerGetLastThrottleElapsedTime);
F32 exprPlayerGetLastThrottleElapsedTime(void)
{
	return MAX((gGCLState.totalElapsedTimeMs - gPlayerControlData.uiLastThrottleAdjustTime) / 1000.0f, 0);
}

int gclPlayerControl_IsInSTOSpaceshipMovement(void)
{
	return gPlayerControlData.bSTOSpaceshipMovement;
}


AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(lookUp) ACMD_CLIENTCMD;
void cmdHandleLookUp(int bLookUp)
{
	gclCamera_SetCameraInputLookUp(bLookUp);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(lookDown) ACMD_CLIENTCMD;
void cmdHandleLookDown(int bLookDown)
{
	gclCamera_SetCameraInputLookDown(bLookDown);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(SelectFriendlyOrTrayExec) ACMD_CATEGORY(Interface) ACMD_HIDE;
void cmdSelectFriendlyTargetOrTrayExec(bool bActive, int tray, int slot)
{
	Entity* e = entActivePlayerPtr();
	Entity* pTarget = NULL;
	TrayElem* pelem = entity_TrayGetTrayElem(e,tray,slot);
	PowerDef* pDef = entity_TrayGetPowerDef(e, pelem);
	PowerTarget *powerTarget = pDef ? GET_REF(pDef->hTargetMain) : NULL;


	cursorModeDefault_OnClickEx(bActive, &pTarget, NULL, NULL);

	if (!e || !pelem || !powerTarget)
		return;

	if (!pTarget || character_TargetMatchesPowerType(entGetPartitionIdx(e), e->pChar, pTarget->pChar, powerTarget) ||
		(gclEntGetIsFoe(e, pTarget) && powerTarget->bRequireSelf))
		gclTrayExec(bActive, pelem);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(InteractOrTrayExec) ACMD_CATEGORY(Interface) ACMD_HIDE;
void cmdInteractOrTrayExec(bool bActive, int tray, int slot)
{
	Entity* e = entActivePlayerPtr();
	Entity* target = target_SelectUnderMouse(e,0,0,NULL,false,true, true);
	WorldInteractionNode* pNode = target_SelectObjectUnderMouse(e, 0);
	bool bInteracted = false;
	TrayElem* pelem = entity_TrayGetTrayElem(e,tray,slot);

	if (!e || !pelem)
		return;

	if(target)
	{
		//Check to see if the character can interact with this target
		EntityRef targetRef = entGetRef(target);

		int i;

		for (i = 0; i < eaSize(&e->pPlayer->InteractStatus.interactOptions.eaOptions); i++)
		{
			if (e->pPlayer->InteractStatus.interactOptions.eaOptions[i]->entRef == targetRef )
			{
				bInteracted = true;
				interaction_InteractWithOption(e, e->pPlayer->InteractStatus.interactOptions.eaOptions[i], false);
				break;
			}
		}
	}
	if (pNode)
	{
		int i;
		for(i=0; i<eaSize(&e->pPlayer->InteractStatus.interactOptions.eaOptions); ++i)
		{
			if (GET_REF(e->pPlayer->InteractStatus.interactOptions.eaOptions[i]->hNode) == pNode)
			{
				bInteracted = true;
				interaction_InteractWithOption(e, e->pPlayer->InteractStatus.interactOptions.eaOptions[i], false);
				break;
			}
		}
	}
	if (!bInteracted)
		gclTrayExec(bActive, pelem);
}

void gclPlayerControl_SuspendMouseLook()
{
	gPlayerControlData.bSuspendForcedMouselook = true;
	cmdHandleForceMouseLookSuspend(NULL,NULL);
}
