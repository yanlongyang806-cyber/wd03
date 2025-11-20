#include "GameAccountDataCommon.h"
#include "gclClickToMove.h"
#include "ClientTargeting.h"
#include "GameClientLib.h"
#include "gclEntity.h"
#include "Character.h"
#include "cmdParse.h"
#include "cmdClient.h"
#include "Player.h"
#include "PowerActivation.h"
#include "WorldLib.h"
#include "WorldColl.h"
#include "wlInteraction.h"
#include "Character_combat.h"
#include "Character_target.h"
#include "EditorManager.h"
#include "gclCommandParse.h"
#include "WorldGrid.h"
#include "gclplayercontrol.h"
#include "EntityMovementManager.h"
#include "EntityMovementTactical.h"
#include "ClientTargeting.h"
#include "uitray.h"
#include "tray.h"
#include "gclCursorMode.h"
#include "ControlScheme.h"
#include "ClientTargeting.h"
#include "powerslots.h"

#include "GraphicsLib.h"
#include "GfxPrimitive.h"

#include "inputKeyBind.h"
#include "inputMouse.h"
#include "inputLib.h"

static bool s_bPlayerAIEnabled = false;
static bool s_bMovementButtonIsDown = false;
static bool s_bAttackButtonIsDown = false;
static bool s_bAttackButtonDoubleClicked = false;
static U32	s_OnNextClickPower = 0;
static EntityRef s_followRef = 0;
static int s_bAttemptToShift = 0;

extern ControlScheme g_CurrentScheme;

//bind this to a button to trigger tactical rolls towards the cursor
//AUTO_CMD_INT(s_bAttemptToShift, ctm_AttemptToShift) ACMD_ACCESSLEVEL(0);

/*
	Putting this file to rest as NW has decided not to pursue click-to-move any further.

	NONE OF THESE FEATURES are used by any active projects at Cryptic as of 9/2/2011,
	so feel free to modify this file in any way you'd like. If you're looking to enable 
	click-to-move as a prototype for your project, here are the general things to keep in mind:

	* bEnableClickToMove should be enabled in the controlscheme you're using.

	* You'll need to bind some combination of the following to your mouse buttons:
	  -> +ctm_UnifiedButtonDown on LButton (Makes left button designate a destination or execute tray 1 slot 1 when clicked)
	  -> ctm_UnifiedButtonDoubleclick on LeftDoubleClick (Makes double-left-clicking an enemy move you into melee range and then attack)
	  -> +ctm_UnifiedButtonDrag on LeftDrag (Makes holding down the left button move you towards the cursor)

	* Alternately, if you're looking for more of a league of legends/RTS control scheme, you can do:
	  -> +ctm_IssueCommandButtonDown on RButton (makes right-clicking designate a follow/autoattack target or a destination in the world.)
	  -> Also, bind RightDrag as in the above variant.

	* You can also enable a click-to-move cursor mode (see ctm_RegisterCursorMode as an example,) 
	  which will handle everything except drag-moving. You'll still need to bind that.

	* Also, features were added to the power-targeting cursor modes (see gclCursorModePowerLocationTargeting.c and gclCursorModePowerTargeting.c)
	  You can turn on a flag to make single-target powers wait for a click (as well as displaying their icon on the cursor) before activation.

	This stuff was written as a super-quick prototype and is BY NO MEANS in a finished state.
	It needs a large amount of love and polish to be shippable, but in general it works well. 
	It was meant to be the *primary* controlscheme for a game, not to co-exist with WADS or standard mmo controls.

	Note: You will also need to modify LOTS of other controlscheme/gconf/combatconfig options related to the camera, 
	hard/soft targets, power execution, movement and tactical rolling to get things feeling right. I'm not going 
	to go into all of them here because it would wind up reading like a primer on the entire combat/camera/control
	system, but just keep in mind that things will likely not feel quite right after you've followed the above steps.

	~CMiller
*/
static void drawClickToMoveTarget(bool bAutoAttackTarget)
{
	Entity *pEntPlayer = entActivePlayerPtr();
	Vec3 origin;
	Vec3 p1, p2, p3;
	F32 sinMovement = sinf((gGCLState.totalElapsedTimeMs % 600) * 2*PI/600)/2 + 0.5;
	Color color = bAutoAttackTarget ? colorFromRGBA(0xaa1111FF) : colorFromRGBA(0x5555FFFF);

	if (!pEntPlayer)
		return;

	copyVec3(gGCLState.v3MoveToLocation, origin);
	origin[1] += 0.25f;

	copyVec3(origin, p1);
	copyVec3(origin, p2);
	copyVec3(origin, p3);
	p1[0] -= 2.0f + sinMovement;
	p1[2] -= 1.0f;
	p2[0] -= 2.0f + sinMovement;
	p2[2] += 1.0f;
	p3[0] -= 0.0f + sinMovement;
	p3[2] += 0.0f;
	gfxDrawTriangle3D(p3, p1, p2, color);
	copyVec3(origin, p1);
	copyVec3(origin, p2);
	copyVec3(origin, p3);
	p1[2] -= 2.0f + sinMovement;
	p1[0] -= 1.0f;
	p2[2] -= 2.0f + sinMovement;
	p2[0] += 1.0f;
	p3[2] -= 0.0f + sinMovement;
	p3[0] += 0.0f;
	gfxDrawTriangle3D(p3, p1, p2, color);
	copyVec3(origin, p1);
	copyVec3(origin, p2);
	copyVec3(origin, p3);
	p1[0] += 2.0f + sinMovement;
	p1[2] -= 1.0f;
	p2[0] += 2.0f + sinMovement;
	p2[2] += 1.0f;
	p3[0] += 0.0f + sinMovement;
	p3[2] += 0.0f;
	gfxDrawTriangle3D(p3, p1, p2, color);
	copyVec3(origin, p1);
	copyVec3(origin, p2);
	copyVec3(origin, p3);
	p1[2] += 2.0f + sinMovement;
	p1[0] -= 1.0f;
	p2[2] += 2.0f + sinMovement;
	p2[0] += 1.0f;
	p3[2] += 0.0f + sinMovement;
	p3[0] += 0.0f;
	gfxDrawTriangle3D(p3, p1, p2, color);
}

static void drawClickToMoveArrow(void)
{
	Entity *pEntPlayer = entActivePlayerPtr();
	GfxCameraController *camera = gfxGetActiveCameraController();
	Vec3 origin, entPos, toEntDir;
	Vec3 p1, p2;
	Vec3 camLookAtVec;
	Vec3 upVec = {0, 1, 0};
	Vec3 rightVec;
	Color color = colorFromRGBA(0x5555FFFF);

	if (!pEntPlayer)
		return;

	gfxSetPrimZTest(0);
	copyVec3(camera->last_camera_matrix[2], camLookAtVec);
	scaleVec3(camLookAtVec, 0.5f, camLookAtVec);

	copyVec3(gGCLState.v3MoveToLocation, origin);
	//origin[1] += 1.0f;
	entGetPosDir(pEntPlayer, entPos, NULL);
	subVec3(origin, entPos, toEntDir);
	normalVec3(toEntDir);
	crossVec3(upVec, toEntDir, rightVec);

	if (distance3Squared(entPos, origin) >= SQR(20))
	{
		scaleVec3(toEntDir, 20.0f, toEntDir);
		addVec3(entPos, toEntDir, origin);
		scaleVec3(toEntDir, 0.1f, toEntDir);
	}
	else
	{
		scaleVec3(toEntDir, 2.0f, toEntDir);
	}

	copyVec3(origin, p1);
	copyVec3(origin, p2);
	subVec3(p1, toEntDir, p1);
	subVec3(p2, toEntDir, p2);
	addVec3(p1, rightVec, p1);
	subVec3(p2, rightVec, p2);
	addVec3(p1, camLookAtVec, p1);
	addVec3(p2, camLookAtVec, p2);
	gfxDrawTriangle3D(origin, p1, p2, color);

	subVec3(p1, toEntDir, p1);
	subVec3(p2, toEntDir, p2);
	scaleVec3(rightVec, 0.3f, rightVec);
	subVec3(p1, rightVec, p1);
	addVec3(p2, rightVec, p2);
	gfxDrawTriangle3D(origin, p1, p2, color);
	gfxSetPrimZTest(1);
}

void ctm_MoveToEnt(Entity* pTarget)
{
	Vec3 targetPos;
	Vec3 playerPos;
	Vec3 movementVec;
	Entity* pEntPlayer = entActivePlayerPtr();
	entGetPos(pTarget, targetPos);
	entGetPos(pEntPlayer, playerPos);
	subVec3(targetPos, playerPos, movementVec);
	s_followRef = pTarget->myRef;
	if	(targetPos[0] < 10000.0f && targetPos[0] > -10000.0f &&
		targetPos[1] < 10000.0f && targetPos[1] > -10000.0f &&
		targetPos[2] < 10000.0f && targetPos[2] > -10000.0f)
	{
		pEntPlayer->pPlayer->bMovingToLocation = true;
		copyVec3(targetPos, gGCLState.v3MoveToLocation);
	}
}

void ctm_SetDestinationToPos(Vec3 worldPos)
{
	Entity* e = entActivePlayerPtr();
	F32 used, total;
	s_followRef = 0;

	entGetRollCooldownTimes(e, &used, &total);
	//if we're trying this as a tactical roll, cancel all powers and set the yaw
	if (s_bAttemptToShift && used == total)
	{
		Vec3 result, myPos;
		U32 milliseconds;
		entGetPos(e, myPos);
		subVec3(worldPos, myPos, result);
		character_ActAllCancel(PARTITION_CLIENT,e->pChar,true);
		frameLockedTimerGetCurTimes(gGCLState.frameLockedTimer, NULL, &milliseconds, NULL);
		mmInputEventSetValueF32(e->mm.movement, MIVI_F32_ROLL_YAW, getVec3Yaw(result), milliseconds);
	}
	else
	{
		e->pPlayer->bMovingToLocation = true;
		gGCLState.bMovingTowardsCursor = false;
		copyVec3(worldPos, gGCLState.v3MoveToLocation);

	}
}

void ctm_MoveTowardsCursor()
{
	Entity* e = entActivePlayerPtr();
	Vec3 rayStart, rayDir, myPos;
	F32 scale;
	entGetPos(e, myPos);
	target_GetCursorRay(e, rayStart, rayDir);

	s_followRef = 0;
	scale = (myPos[1]-rayStart[1])/rayDir[1];
	if (scale >= 0.0f)
	{
		scaleVec3(rayDir, scale, rayDir);
		addVec3(rayStart, rayDir, gGCLState.v3MoveToLocation);
		e->pPlayer->bMovingToLocation = false;
		gGCLState.bMovingTowardsCursor = true;
	}
}

//attack or set movement destination, works with click-n-hold powers
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void ctm_UnifiedButtonDown(bool bActive)
{
	Entity* e = entActivePlayerPtr();
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
	TrayElem* pelem = NULL;
	Power *ppow = NULL;
	U32 ppowID = 0;
	Vec3 worldPos;
	Entity* pTarget = NULL;
	Vec3 vHardTargetPos;
	Entity* pHardTarget = entity_GetTarget(e);
	if (pHardTarget)
		entGetPos(pHardTarget, vHardTargetPos);

	pelem = entity_TrayGetTrayElem(e, 0, 0);
	ppow = pelem ? EntTrayGetActivatedPower(e,pelem,false,NULL) : NULL;
	if (ppow)
	{
		ppowID = ppow->pParentPower ? ppow->pParentPower->uiID : ppow->uiID;
	}
	else
		ppowID = 0;

	if (pTarget || (pHardTarget && distance3SquaredXZ(vHardTargetPos, worldPos) <= 16))
	{
		if (pTarget)
			entity_SetTarget(e, pTarget->myRef);
		if (ppow && (!bActive || EntIsPowerTrayActivatable(e->pChar, ppow, pTarget, pExtract)))
		{
			character_ActivatePowerByIDClient(PARTITION_CLIENT, e->pChar, ppowID, NULL, NULL, bActive, pExtract);
			if (bActive)
			{
				gGCLState.bMovingTowardsCursor = false;
				e->pPlayer->bMovingToLocation = false;
			}
		}
	}
	else if (!bActive)
	{
		ctm_SetDestinationToPos(worldPos);
	}
}

//RTS-style "move here or autoattack this person" button.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void ctm_IssueCommandButtonDown(bool bActive)
{
	Entity* e = entActivePlayerPtr();
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
	TrayElem* pelem = NULL;
	Power *ppow = NULL;
	U32 ppowID = 0;
	Vec3 worldPos;
	Entity* pTarget = target_SelectUnderMouse(e, kTargetType_Alive,kTargetType_Self, worldPos, true, true, false);

	pelem = entity_TrayGetTrayElem(e, 0, 0);
	ppow = pelem ? EntTrayGetActivatedPower(e,pelem,false,NULL) : NULL;
	if (ppow)
	{
		ppowID = ppow->pParentPower ? ppow->pParentPower->uiID : ppow->uiID;
	}
	else
		ppowID = 0;

	if (pTarget)
	{
		entity_SetTarget(e, pTarget->myRef);
		ctm_MoveToEnt(pTarget);

		gclAutoAttack_SetOverrideID(ppowID);
		gclAutoAttack_DefaultAutoAttack(1);
	}
	else if (!bActive)
	{
		ctm_SetDestinationToPos(worldPos);
	}
}

//set an enemy as a follow target & whack him when we get there
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void ctm_UnifiedButtonDoubleclick()
{
	Entity* e = entActivePlayerPtr();
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
	TrayElem* pelem = NULL;
	Power *ppow = NULL;
	Entity* pTarget = target_SelectUnderMouse(e, kTargetType_Alive,kTargetType_Self, NULL, true, true, false);

	pelem = entity_TrayGetTrayElem(e, 0, 0);
	ppow = pelem ? EntTrayGetActivatedPower(e,pelem,false,NULL) : NULL;

	if (pTarget)
	{
		entity_SetTarget(e, pTarget->myRef);
		if (!ppow || !EntIsPowerTrayActivatable(e->pChar, ppow, NULL, pExtract))
		{
			ctm_MoveToEnt(pTarget);
		}
	}
}

static U32 dragStartTime = 0;
#define CTM_DRAG_TOLERANCE 75 //in ms

//move towards cursor
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void ctm_UnifiedButtonDrag(bool bActive)
{
	Entity* e = entActivePlayerPtr();
	if (bActive)
	{
		dragStartTime = gGCLState.totalElapsedTimeMs;
		gGCLState.bMovingTowardsCursor = true;
	}
	else if (gGCLState.totalElapsedTimeMs > dragStartTime + CTM_DRAG_TOLERANCE)
	{
		gGCLState.bMovingTowardsCursor = false;
		e->pPlayer->bMovingToLocation = false;
	}

}

//cursor mode on-click handler
static void ctm_OnClick(bool bDown)
{
	Entity* e = entActivePlayerPtr();
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
	TrayElem* pelem = NULL;
	Power *ppow = NULL;
	U32 ppowID = 0;
	Vec3 worldPos;
	Entity* pTarget = NULL;
	Vec3 vHardTargetPos;
	Entity* pHardTarget = entity_GetTarget(e);
	if (pHardTarget)
		entGetPos(pHardTarget, vHardTargetPos);

	pelem = entity_TrayGetTrayElem(e, 0, inpLevelPeek(INP_CONTROL) ? 1 : 0);
	ppow = pelem ? EntTrayGetActivatedPower(e,pelem,false,NULL) : NULL;
	if (ppow)
	{
		ppowID = ppow->pParentPower ? ppow->pParentPower->uiID : ppow->uiID;
	}
	else
		ppowID = 0;

	//do everything a normal click does, plus handle movement.
	cursorModeDefault_OnClickEx(bDown, &pTarget, NULL, worldPos);

	if (pTarget || (pHardTarget && distance3SquaredXZ(vHardTargetPos, worldPos) <= 16))
	{
		if (pTarget)
			entity_SetTarget(e, pTarget->myRef);
		if (ppow && (!bDown || EntIsPowerTrayActivatable(e->pChar, ppow, pTarget, pExtract)))
		{
			character_ActivatePowerByIDClient(PARTITION_CLIENT, e->pChar, ppowID, NULL, NULL, bDown, pExtract);
			if (bDown)
			{
				gGCLState.bMovingTowardsCursor = false;
				e->pPlayer->bMovingToLocation = false;
			}
		}
	}
	else if (!bDown)
	{
		ctm_SetDestinationToPos(worldPos);
	}
}

//this is dumb, used as a safeguard to prevent eternal mousedown.
static int s_movebutton = INP_RBUTTON;

//Function to register an actual cursor mode to handle most of the click-to-move stuff.
AUTO_COMMAND;
void ctm_RegisterCursorMode()
{
	gclCursorMode_Register("ClickToMoveCursorMode", "Default", ctm_OnClick, NULL, NULL, targetCursor_Update);//still use the targetcursor features
	gclCursorMode_SetDefault("ClickToMoveCursorMode");
	gclCursorMode_SetModeByName("ClickToMoveCursorMode");
	s_movebutton = INP_LBUTTON;
}

void clickToMoveTick(void)
{
	Vec3 myPos;
 	Entity *pEntPlayer = entActivePlayerPtr();
	TrayElem* pelem = NULL;
	Power *ppow = NULL;
	U32 ppowID = 0;
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntPlayer);
	bool bNoMovementTowardsLocationThisFrame = false;
	if (!pEntPlayer || emIsEditorActive())
		return;

	pelem = entity_TrayGetTrayElem(pEntPlayer, 0, inpLevelPeek(INP_CONTROL) ? 1 : 0);
	ppow = pelem ? EntTrayGetActivatedPower(pEntPlayer,pelem,false,NULL) : NULL;
	if (ppow)
	{
		ppowID = ppow->pParentPower ? ppow->pParentPower->uiID : ppow->uiID;
	}
	else
		ppowID = 0;
	entGetPosDir(pEntPlayer, myPos, NULL);

	//cancel Lol-style autoattack if we have no target
	if (!s_followRef && gclAutoAttack_PowerIDAttacking(ppowID))
	{
		gclAutoAttack_Disable();
	}

	//if we're en route...
	if (pEntPlayer->pPlayer->bMovingToLocation)
	{
		if (s_followRef > 0)
		{
			//and we're following somebody...
			Entity* pTarget = entFromEntityRefAnyPartition(s_followRef);
			PowerDef *pdef = GET_REF(ppow->hDef);
			if (!pTarget || !entIsAlive(pTarget))
			{
				//cancel follow if they're dead
				s_followRef = 0;
				pEntPlayer->pPlayer->bMovingToLocation = false;
			}
			if (ppow && (gclAutoAttack_GetOverrideID() == 0) && EntIsPowerTrayActivatable(pEntPlayer->pChar, ppow, pTarget, pExtract))
			{
				if (entGetDistance(pEntPlayer, NULL, pTarget, NULL, NULL) <= (pdef->fRange-1) && character_ActivatePowerByIDClient(PARTITION_CLIENT, pEntPlayer->pChar, ppowID, NULL, NULL, 1, pExtract))
				{
					//if we're NOT using LoL-style autoattack, cancel follow once we're able to attack once.
					s_followRef = 0;
					pEntPlayer->pPlayer->bMovingToLocation = false;
				}
			}
			else if (entGetDistance(pEntPlayer, NULL, pTarget, NULL, NULL) >= (pdef->fRange-1))
			{
				//update follow target as new destination
				ctm_MoveToEnt(pTarget);
			}
			else
			{
				//flag to prevent movement this frame without turning it off forever
				bNoMovementTowardsLocationThisFrame = true;
			}
		}
		//end movement when we're within 2 feet of destination
		else if (distance3SquaredXZ(gGCLState.v3MoveToLocation, myPos) < SQR(2.0f))
			pEntPlayer->pPlayer->bMovingToLocation = false;
	}

	//safeguard to cancel drag-movement if the button isn't held down. s_movebutton was an icky hack, a better solution is needed.
	if (gGCLState.bMovingTowardsCursor && !inpLevelPeek(s_movebutton))
		gGCLState.bMovingTowardsCursor = false;

	//only move towards the cursor if we're sure this is a drag.
 	if (gGCLState.bMovingTowardsCursor && gGCLState.totalElapsedTimeMs > dragStartTime + CTM_DRAG_TOLERANCE)
	{
		ctm_MoveTowardsCursor();
	}
	
	if ((gGCLState.bMovingTowardsCursor || pEntPlayer->pPlayer->bMovingToLocation) && !bNoMovementTowardsLocationThisFrame)
	{
		//actual movement and facing happens here.
		Vec3 result;
		subVec3(gGCLState.v3MoveToLocation, myPos, result);
		gclPlayerControl_SetMoveAndFaceYawByEnt(pEntPlayer, getVec3Yaw(result));

		setMouseForward(1);
		gclPlayerControl_SetFreeCamera(true);
		gclPlayerControl_DisableTurnToFaceThisFrame();
	}
	else 
		setMouseForward(0);

	if (gGCLState.bMovingTowardsCursor && gGCLState.totalElapsedTimeMs > dragStartTime + CTM_DRAG_TOLERANCE)
	{
		//you can roll if you want to, you can leave your friends behind
		F32 used, total;
		entGetRollCooldownTimes(pEntPlayer, &used, &total);
		if (s_bAttemptToShift && used == total)
		{
			//I swear to God, this is how Martin told me to do this. We have to convince the mm that this was a double-tap forwards.
			mmInputEventSetValueBit(pEntPlayer->mm.movement, MIVI_BIT_FORWARD, 1, gGCLState.totalElapsedTimeMs, 1);
			mmInputEventSetValueBit(pEntPlayer->mm.movement, MIVI_BIT_FORWARD, 1, gGCLState.totalElapsedTimeMs, 1);
			mmInputEventSetValueBit(pEntPlayer->mm.movement, MIVI_BIT_FORWARD, 0, gGCLState.totalElapsedTimeMs, 1);
		}
	}

	if (gGCLState.bMovingTowardsCursor && !bNoMovementTowardsLocationThisFrame && !gGCLState.pPrimaryDevice->gamecamera.rotate && (gGCLState.totalElapsedTimeMs > dragStartTime + CTM_DRAG_TOLERANCE))
		drawClickToMoveArrow();
	else if (pEntPlayer->pPlayer->bMovingToLocation)
		drawClickToMoveTarget(s_followRef > 0);
}