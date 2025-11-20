#include "GameAccountDataCommon.h"
#include "gclCursorMode.h"
#include "gclEntity.h"
#include "gclControlScheme.h"
#include "gclPlayerControl.h"
#include "ClientTargeting.h"
#include "CombatPowerStateSwitching.h"
#include "dynFxInfo.h"
#include "../../WorldLib/AutoGen/dynFxInfo_h_ast.h"
#include "dynFxInterface.h"
#include "dynFxManager.h"
#include "Character_target.h"
#include "Character_combat.h"
#include "mapstate_common.h"
#include "CombatEval.h"
#include "Character.h"
#include "PowerActivation.h"
#include "ProjectileEntity.h"
#include "PowerAnimFX.h"

#include "inputLib.h"
#include "inputMouse.h"
#include "UICore.h"
#include "WorldColl.h"
#include "WorldLib.h"
#include "WorldGrid.h"
#include "GraphicsLib.h"
#include "StringCache.h"
#include "inputKeyBind.h"
#include "GfxCamera.h"
#include "EditLib.h"
#include "Powers.h"
#include "GfxTexAtlas.h"
#include "GfxSprite.h"
#include "UIInternal.h"
#include "gclReticle.h"

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "gclCursorModePowerLocationTargeting_c_ast.h"

#include "GfxPrimitive.h"

#define POWERLOCATIONTARGETING_CURSOR_MODE_NAME	"powerLocationTargeting"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

typedef struct LocationTargetState
{
	// The ID of the power being used for targeting
	U32 uiPowerId;

	// the actual ref of the power being used. Use this one to get the actual power
	PowerRef erPowerRef;

	// The FX applied on the target point
	dtFx hTargetReticleFx;

	// The node that represents the target
	dtNode hTargetReticleNode;

	// Previous out of range status
	U32 bPreviouslyWasAvailable : 1;

	// Current out of range status
	U32 bCurrentlyIsAvailable : 1;
} LocationTargetState;

AUTO_STRUCT;
typedef struct LocationTargetingConfig
{
	// Cursor name
	const char *pchCursorName;				AST(NAME("CursorName") POOL_STRING)

	// The FX used for targeting
	const char *pchTargetReticleFxName;		AST(NAME("TargetReticleFxName") POOL_STRING)

	// The parameter name for scaling in the FX file
	const char *pchFxParamNameScale;		AST(NAME("FxParamNameScale") POOL_STRING)

	// The parameter name for color in the FX file
	const char *pchFxParamNameColor;		AST(NAME("FxParamNameColor") POOL_STRING)

	// Indicates how high from the ground we want to target
	const float fGroundHeightAdjustment;	AST(NAME("GroundHeightAdjustment"))

	// The name of the keybind to push when this cursor mode becomes active
	const char *pchKeybindName;				AST(NAME("KeybindName") POOL_STRING)

	// if set will send a message to kill it instead of a hardkill on the FX
	const char *pchFxKillMessage;			AST(NAME("FxKillMessage") POOL_STRING)

	Vec3 vecColorOutOfRange;				AST(NAME(OutOfRangeColor))
	Vec3 vecColorInRange;					AST(NAME(InRangeColor))

	S32 cursorX;
	S32 cursorY;

	const char* pCursorTexture2;		AST(POOL_STRING)

		// If turned on, the power will cast at the maximum distance when the cursor is pointing
	// at somewhere further than the distance of the power
	U32 bClampToMaxDistance : 1;

	// if true, will disable the location targeting if power cannot be cast for whatever reason via character_ActTestPowerTargeting
	U32 bTestPowerRequirementsOnUpdate : 1;

	U32 bShowOnCursor : 1;

} LocationTargetingConfig;

// Holds the data for location targeting
static LocationTargetState s_LocationTargetState;

// Config
static LocationTargetingConfig s_LocationTargetingConfig;

// Forward declarations
static void gclCursorPowerLocationTargeting_OnClick(bool bDown);
static void gclCursorPowerLocationTargeting_OnModeEnter();
static void gclCursorPowerLocationTargeting_OnModeExit();
static void gclCursorPowerLocationTargeting_Update();

F32 gclCursorPowerTargeting_PowerGetRadius(Power *pPower, PowerDef *pPowerDef);


AUTO_STARTUP(PowerLocationTargeting);
void CursorMode_PowerLocationTargetingInitialize(void)
{
	StructInit(parse_LocationTargetingConfig, &s_LocationTargetingConfig);

	ParserLoadFiles(NULL, "defs/config/LocationTargetingConfig.def", "LocationTargetingConfig.bin", PARSER_OPTIONALFLAG, parse_LocationTargetingConfig, &s_LocationTargetingConfig);

	if (s_LocationTargetingConfig.pchCursorName)
	{
		gclCursorMode_Register(POWERLOCATIONTARGETING_CURSOR_MODE_NAME, s_LocationTargetingConfig.pchCursorName,
			gclCursorPowerLocationTargeting_OnClick, 
			gclCursorPowerLocationTargeting_OnModeEnter,
			gclCursorPowerLocationTargeting_OnModeExit,
			gclCursorPowerLocationTargeting_Update);
	}
}

static void gclCursorPowerLocationTargeting_HideTargetingReticle()
{
	// If the FX is active
	if (s_LocationTargetState.hTargetReticleFx)
	{
		Vec3 z;
		zeroVec3(z);
		dtNodeSetPos(s_LocationTargetState.hTargetReticleNode, z);
	}
}

static bool gclCursorPowerLocationTargeting_GetTargetPosition(	SA_PARAM_NN_VALID Entity *pEntSource, 
																const Vec3 rayStart, 
																const Vec3 rayDir, 
																Vec3 vOutPos, 
																Vec3 vOutNormal, 
																F32 fMaxDistance)
{
	WorldCollCollideResults results = {0};
	Vec3 vRayStart;
	Vec3 vRayEnd;
	
	Mat4 matCam;
	Vec4 vecPlayerCameraPlane;
	Vec3 vecEntPos;
	Vec3 vecCamPos;
	Vec3 vecPlayerCamDir;
	Vec3 vecPlayerCamCursorRayIntersect;
	F32 fCamDist;
	F32 fEntYPos;

	copyVec3(rayStart, vRayStart);

	// Get the source entity position
	entGetPos(pEntSource, vecEntPos);
	fEntYPos = vecEntPos[1];
	// Ignore the y coordinate
	vecEntPos[1] = 0.f;

	// Get the camera matrix
	gfxGetActiveCameraMatrix(matCam);
	
	// Get the camera position
	copyVec3(matCam[3], vecCamPos);

	// Ignore the y coordinate
	vecCamPos[1] = 0.f;

	// Calculate the difference and normalize
	subVec3(vecEntPos, vecCamPos, vecPlayerCamDir);
	fCamDist = distance3(vecEntPos,vecCamPos);
	normalVec3(vecPlayerCamDir); 

	// Create the plane
	makePlane2(vecEntPos, vecPlayerCamDir, vecPlayerCameraPlane);

	// Calculate the ray end point
	{
		F32 fEndPointDist = fMaxDistance && s_LocationTargetingConfig.bClampToMaxDistance ? fMaxDistance + fCamDist : 300.f;
		
		scaleAddVec3(rayDir, fEndPointDist, vRayStart, vRayEnd);
	}

	// See if the ray intersects with the player camera plane
	if (intersectPlane(vRayStart, vRayEnd, vecPlayerCameraPlane, vecPlayerCamCursorRayIntersect))
	{
		// Only respect the intersection point if it's above the player's HEAD
		if (vecPlayerCamCursorRayIntersect[1] > fEntYPos+6.0f)
		{
			// Use the intersection point as the ray start point. This will prevent the ray cast to hit 
			copyVec3(vecPlayerCamCursorRayIntersect, vRayStart);
		}
	}
	
	if (!worldCollideRay(PARTITION_CLIENT, vRayStart, vRayEnd, WC_FILTER_BIT_POWERS, &results))
	{
		// If there is no contact with initial ray, only continue if we have the option set, bClampToMaxDistance
		if(!s_LocationTargetingConfig.bClampToMaxDistance)
			return false;
		
		copyVec3(vRayEnd, vOutPos);
		setVec3(vOutNormal, 0.f, 1.f, 0.f);
	}
	else
	{
		// store the presumed best pos
		copyVec3(results.posWorldImpact, vOutPos);
		copyVec3(results.normalWorld, vOutNormal);
	}
	
	if(s_LocationTargetingConfig.bClampToMaxDistance)
	{
		F32 fDistToEnd;
		
		entGetPos(pEntSource, vecEntPos);

		fDistToEnd = entGetDistance(NULL, vecEntPos, NULL, vOutPos, NULL);

		if (fDistToEnd > fMaxDistance)
		{	// clamp to the max range
			Vec3 vDirLen;
			S32 bHit, bInside = false;
			F32 fHitTime = 0.f;

			subVec3(vOutPos, vRayStart, vDirLen);
						
			bHit = RayVsSphere(vRayStart, vDirLen, vecEntPos, fMaxDistance, &fHitTime, &bInside);
			if (bHit && bInside)
			{	// finds the point on the maxDistance sphere where the ray leaves, 
				// we will use this position to cast from to our max distance point
				Vec3 vHitSpherePos;
				Vec3 vMaxDistPos;
				Vec3 vXZDir;
				F32 fDistanceOut = fMaxDistance - 2.f;
				scaleAddVec3(vDirLen, fHitTime, vRayStart, vHitSpherePos);
				copyVec3(vDirLen, vXZDir);
				vXZDir[1];
				normalVec3(vXZDir);
				
				if (fDistanceOut <= 0.f)
					fDistanceOut = fMaxDistance;

				// push the fMaxDistance in slightly
				scaleAddVec3(vXZDir, fDistanceOut, vecEntPos, vMaxDistPos);

				if(!worldCollideRay(PARTITION_CLIENT, vHitSpherePos, vMaxDistPos, WC_FILTER_BIT_POWERS, &results))
				{
					Vec3 vNewEnd;
					copyVec3(vecEntPos,vNewEnd);
					vNewEnd[1] -= fMaxDistance;
					if(!worldCollideRay(PARTITION_CLIENT, vMaxDistPos, vNewEnd, WC_FILTER_BIT_POWERS, &results))
						return false;
				}

				copyVec3(results.posWorldImpact, vOutPos);
				copyVec3(results.normalWorld, vOutNormal);
			}
			else
			{	// if this is hit, it means the camera cast position is outside of the maxDistance
				// this is a case that should be caught beforehand by clamping that position to the maxDistance sphere around the entity
				return false;
			}

		}
	}
	
	// validate this position
	if (getAngleBetweenVec3(upvec, results.normalWorld) > RAD(40.f))
	{
		Vec3 vTemp,vDown;
		Vec3 vRaySt;

		// the angle is too steep. Try to slide down the surface to find the ground.
		crossVec3(upvec,results.normalWorld,vTemp);
		crossVec3(vTemp,results.normalWorld,vDown);
		normalVec3(vDown);

		scaleAddVec3(results.normalWorld, 0.5f, results.posWorldImpact, vRaySt);
		scaleAddVec3(vDown, fMaxDistance, vRaySt, vRayEnd);

		if (worldCollideRay(PARTITION_CLIENT, vRaySt, vRayEnd, WC_QUERY_BITS_COMBAT, &results))
		{
			if (getAngleBetweenVec3(upvec, results.normalWorld) <= RAD(40.f))
			{
				// use this wonderful spot instead
				copyVec3(results.posWorldImpact, vOutPos);
				copyVec3(results.normalWorld, vOutNormal);
			}
			else
			{
				return false;
			}
		}
		else
		{
			return false;
		}
	}

	// Height adjustment
	vOutPos[1] += s_LocationTargetingConfig.fGroundHeightAdjustment;

	return true;	
}

static void gclCursorPowerLocationTargeting_OnModeEnter(void)
{
	if (s_LocationTargetingConfig.pchKeybindName && s_LocationTargetingConfig.pchKeybindName[0])
	{
		keybind_PushProfileName(s_LocationTargetingConfig.pchKeybindName);
	}

	// Create a new node
	if (!s_LocationTargetState.hTargetReticleNode)
	{
		s_LocationTargetState.hTargetReticleNode = dtNodeCreate();
	}
}

static void gclCursorPowerLocationTargeting_OnModeExit(void)
{
	if (s_LocationTargetingConfig.pchKeybindName && s_LocationTargetingConfig.pchKeybindName[0])
	{
		keybind_PopProfileName(s_LocationTargetingConfig.pchKeybindName);
	}

	// Destroy the FX if necessary
	if (s_LocationTargetState.hTargetReticleFx)
	{
		if (s_LocationTargetingConfig.pchFxKillMessage && s_LocationTargetingConfig.pchFxKillMessage[0])
		{	
			dtFxSendMessage(s_LocationTargetState.hTargetReticleFx, s_LocationTargetingConfig.pchFxKillMessage);
			// keep the handle around because if this starts back up again we want to hard kill it
		}
		else
		{
			dtFxKillEx(s_LocationTargetState.hTargetReticleFx, true, true);
			s_LocationTargetState.hTargetReticleFx = 0;
		}

		
	}

	if (s_LocationTargetingConfig.bShowOnCursor)
	{
		Entity* pPlayerEnt = entActivePlayerPtr();
		if (pPlayerEnt)
		{
			//Power* ppow = character_FindPowerByID(pPlayerEnt->pChar, s_LocationTargetState.uiPowerId);
			Power* ppow = character_FindPowerByRef(pPlayerEnt->pChar, &s_LocationTargetState.erPowerRef);


			PowerDef* pdef = SAFE_GET_REF(ppow, hDef);
			if (pdef)
			{
				UIDeviceState *pState = ui_StateForDevice(NULL);
				if (pState && pState->cursor.draggedIcon && pState->cursor.draggedIcon == atlasLoadTexture(pdef->pchIconName))
				{
					pState->cursor.draggedIcon = NULL;
					pState->cursor.draggedIcon2 = NULL;
					pState->cursor.draggedIconY = 0;
					pState->cursor.draggedIconX = 0;
				}
			}
		}
	}

	gclReticle_EnableDrawReticle();

	s_LocationTargetState.uiPowerId = 0;
	powerref_Set(&s_LocationTargetState.erPowerRef, NULL);

}

// -------------------------------------------------------------------------------------------------------------------------------------
static const char* gclCursorPowerLocationTargeting_GetTargetingReticleFXName(PowerDef *pPowerDef)
{
	if (pPowerDef)
	{
		PowerAnimFX *pPowerAnimFX = GET_REF(pPowerDef->hFX);
		if (pPowerAnimFX && pPowerAnimFX->pchLocTargetingReticleFxName && pPowerAnimFX->pchLocTargetingReticleFxName[0])
			return pPowerAnimFX->pchLocTargetingReticleFxName;
	}
	
	if (s_LocationTargetingConfig.pchTargetReticleFxName && s_LocationTargetingConfig.pchTargetReticleFxName[0])
		return s_LocationTargetingConfig.pchTargetReticleFxName;
	
	return NULL;
}

static void gclCursorPowerLocationTargeting_ApplyFx(Entity *pEnt, Power *pPower, PowerDef *pPowerDef, Vec3 vOutPos)
{
	const char *pchReticleFXName = gclCursorPowerLocationTargeting_GetTargetingReticleFXName(pPowerDef);

	// Apply the FX if necessary
	if (pEnt && pEnt->pChar && pchReticleFXName &&
		(!s_LocationTargetState.hTargetReticleFx || s_LocationTargetState.bCurrentlyIsAvailable != s_LocationTargetState.bPreviouslyWasAvailable))
	{
		DynFxManager *pManager = dynFxGetGlobalFxManager(vOutPos);

		if (pManager)
		{
			DynParamBlock *pFxParams;
			DynDefineParam *pParam;
			F32 fRadius = 0.0f;

			static Vec3 vecScale;
			static Vec3 vecColorOutOfRange = { 255.0f, 0.0f, 0.0f };
			static Vec3 vecColorInRange = { 255.0f, 255.0f, 255.0f };

			// Destroy the FX if necessary
			if (s_LocationTargetState.hTargetReticleFx)
			{
				dtFxKillEx(s_LocationTargetState.hTargetReticleFx, true, true);
				s_LocationTargetState.hTargetReticleFx = 0;
			}

			if(!vec3IsZero(s_LocationTargetingConfig.vecColorInRange))
			{
				copyVec3(s_LocationTargetingConfig.vecColorInRange,vecColorInRange);
			}

			if(!vec3IsZero(s_LocationTargetingConfig.vecColorOutOfRange))
			{
				copyVec3(s_LocationTargetingConfig.vecColorOutOfRange,vecColorOutOfRange);
			}

			fRadius = gclCursorPowerTargeting_PowerGetRadius(pPower, pPowerDef);
			
			if (fRadius > 0.0f)
			{
				pFxParams = dynParamBlockCreate();

				// Scale
				if (s_LocationTargetingConfig.pchFxParamNameScale && s_LocationTargetingConfig.pchFxParamNameScale[0])
				{
					pParam = StructAlloc(parse_DynDefineParam);
					pParam->pcParamName = s_LocationTargetingConfig.pchFxParamNameScale;
					setVec3(vecScale, fRadius, fRadius, fRadius);
					MultiValSetVec3(&pParam->mvVal, &vecScale);
					eaPush(&pFxParams->eaDefineParams, pParam);
				}

				// Color
				if (s_LocationTargetingConfig.pchFxParamNameColor && s_LocationTargetingConfig.pchFxParamNameColor[0])
				{
					pParam = StructAlloc(parse_DynDefineParam);
					pParam->pcParamName = s_LocationTargetingConfig.pchFxParamNameColor;
					MultiValSetVec3(&pParam->mvVal, s_LocationTargetState.bCurrentlyIsAvailable ? &vecColorInRange : &vecColorOutOfRange);
					eaPush(&pFxParams->eaDefineParams, pParam);
				}

				s_LocationTargetState.hTargetReticleFx = dtAddFx(	pManager->guid, 
																	pchReticleFXName, 
																	pFxParams, 0, s_LocationTargetState.hTargetReticleNode, 
																	1.f, 0, NULL, eDynFxSource_UI, NULL, NULL);

				// If we failed to apply the FX
				if (!s_LocationTargetState.hTargetReticleFx)
				{
					// Failed to apply the FX, destroy the node so we do not try and recreate
					dtNodeDestroy(s_LocationTargetState.hTargetReticleNode);
					s_LocationTargetState.hTargetReticleNode = 0;
				}
			}
		}
	}
}

static void gclCursorPowerLocationTargeting_Update()
{
	bool bValidPlacement = false;

	Entity *pEnt = entActivePlayerPtr();

	//Power *pPower = pEnt ? character_FindPowerByID(pEnt->pChar,s_LocationTargetState.uiPowerId) : NULL;
	Power* pPower = pEnt ? character_FindPowerByRef(pEnt->pChar, &s_LocationTargetState.erPowerRef) : NULL;
	PowerDef *pPowerDef = pPower ? GET_REF(pPower->hDef) : NULL;

	if (!pPower || !pPowerDef)
	{
		gclCursorMode_ChangeToDefault();
		return;
	}

	if (pEnt == NULL || pEnt->pChar == NULL || (mouseIsLocked() && !(g_CurrentScheme.bShowMouseLookReticle && gclPlayerControl_IsMouseLooking())))
	{
		gclCursorPowerLocationTargeting_HideTargetingReticle();
		return;
	}

	if (s_LocationTargetingConfig.bTestPowerRequirementsOnUpdate)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		if (!character_ActTestPowerTargeting(entGetPartitionIdx(pEnt), pEnt->pChar, pPower, pPowerDef, true, pExtract))
		{
			gclCursorMode_ChangeToDefault();
			return;
		}
	}

	if (!inpCheckHandled())
	{
		Vec3 rayStart, rayDir, vOutPos, vOutNormal;

		target_GetCursorRayEx(pEnt, rayStart, rayDir, true, false);

		if (gclCursorPowerLocationTargeting_GetTargetPosition(pEnt, rayStart, rayDir, vOutPos, vOutNormal, pPowerDef ? pPowerDef->fRangeSecondary : 0))
		{
			if (pPower && pPowerDef)
			{
				int iPartitionIdx = entGetPartitionIdx(pEnt);
				Quat qRot;

				// Assumes that the target is self
				// TODO: Add support for non self targeting
				s_LocationTargetState.bCurrentlyIsAvailable = 
					character_ActTestDynamic(iPartitionIdx, pEnt->pChar, pPower, pEnt, vOutPos, NULL, NULL, NULL, NULL, true, true, true);

				entGetRot(pEnt,qRot);

				if (s_LocationTargetState.hTargetReticleNode)
				{
					// Set the position of the target reticle node
					Vec3 vNodePos;

					// put the position back on the ground
					copyVec3(vOutPos, vNodePos);
					vNodePos[1] -= s_LocationTargetingConfig.fGroundHeightAdjustment;

					dtNodeSetPosAndRot(s_LocationTargetState.hTargetReticleNode, vNodePos, qRot);

					// Apply the FX
					gclCursorPowerLocationTargeting_ApplyFx(pEnt, pPower, pPowerDef, vOutPos);
				}

				s_LocationTargetState.bPreviouslyWasAvailable = s_LocationTargetState.bCurrentlyIsAvailable;

				bValidPlacement = true;
			}
		}
	}

	if (!bValidPlacement)
	{
		// Hide the targeting reticle
		gclCursorPowerLocationTargeting_HideTargetingReticle();
	}
}

static void gclCursorPowerLocationTargeting_OnClick(bool bDown)
{
	Entity *e = entActivePlayerPtr();

	//Power *pPower = e ? character_FindPowerByID(e->pChar,s_LocationTargetState.uiPowerId) : NULL;
	Power* pPower = e ? character_FindPowerByRef(e->pChar, &s_LocationTargetState.erPowerRef) : NULL;

	PowerDef *pDef = pPower ? GET_REF(pPower->hDef) : NULL;

	if (e && bDown && e->pChar && (!mouseIsLocked() || (g_CurrentScheme.bShowMouseLookReticle && gclPlayerControl_IsMouseLooking()))   & !inpCheckHandled())
	{
		Entity *petEnt = NULL;
		Vec3 rayStart, rayDir, vOutPos, vOutNormal;

		target_GetCursorRayEx(e, rayStart, rayDir, true, false);

		if (gclCursorPowerLocationTargeting_GetTargetPosition(e, rayStart, rayDir, vOutPos, vOutNormal, pDef ? pDef->fRangeSecondary : 0))
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
			int iPartitionIdx = entGetPartitionIdx(e);
			if (pPower && character_ActTestDynamic(iPartitionIdx, e->pChar, pPower, e, vOutPos, NULL, NULL, NULL, NULL, true, true, true))
			{
				character_ActivatePowerByIDClient(iPartitionIdx, e->pChar, s_LocationTargetState.uiPowerId, NULL, vOutPos, bDown, pExtract);
				gclCursorMode_ChangeToDefault();
			}
		}
	}
}

void gclCursorPowerLocationTargeting_PowerExec()
{
	gclCursorPowerLocationTargeting_OnClick(true);
	gclCursorMode_ChangeToDefault();
}

// Initiates location based targeting for the given power
void gclCursorPowerLocationTargeting_Begin(U32 uiPowerId)
{
	Entity* pPlayerEnt = entActivePlayerPtr();
	Power* pPow = character_FindPowerByID(pPlayerEnt->pChar, uiPowerId);

	if(!pPow)
		return;

	{
		Power *pSwitchedPower = CombatPowerStateSwitching_GetSwitchedStatePower(pPlayerEnt->pChar, pPow);
		if (pSwitchedPower)
			pPow = pSwitchedPower;
	}

	// Destroy the FX if necessary. This is needed for the cases where we begin this cursor mode without exiting it first.
	if (s_LocationTargetState.hTargetReticleFx)
	{
		dtFxKillEx(s_LocationTargetState.hTargetReticleFx, true, true);
		s_LocationTargetState.hTargetReticleFx = 0;
	}

	s_LocationTargetState.uiPowerId = uiPowerId;

	// set the powerRef
	powerref_Set(&s_LocationTargetState.erPowerRef, pPow);

	if (s_LocationTargetingConfig.bShowOnCursor)
	{
		PowerDef* pdef = GET_REF(pPow->hDef);
		if (pdef)
		{
			AtlasTex* tex = atlasLoadTexture(pdef->pchIconName);
			UIDeviceState *pState = ui_StateForDevice(NULL);
			pState->cursor.draggedIcon = tex;
			pState->cursor.draggedIcon2 = atlasLoadTexture(s_LocationTargetingConfig.pCursorTexture2);
			pState->cursor.draggedIconX = s_LocationTargetingConfig.cursorX;
			pState->cursor.draggedIconY = s_LocationTargetingConfig.cursorY;
		}
	}

	gclReticle_DisableDrawReticle();

	gclCursorMode_SetModeByName(POWERLOCATIONTARGETING_CURSOR_MODE_NAME);	
}

AUTO_EXPR_FUNC(UIGen);
U32 gclCursorPowerLocationTargeting_GetCurrentPowID()
{
	return s_LocationTargetState.uiPowerId;
}

bool gclCursorPowerLocationTargeting_PowerValid(PowerDef* pDef)
{
	PowerTarget *pPowerTarget = pDef ? GET_REF(pDef->hTargetMain) : NULL;

	// If the cursor name isn't set, then assume that PowerLocationTargeting is disabled.
	// The effect area must be a sphere or character.
	if (s_LocationTargetingConfig.pchCursorName &&
		pPowerTarget &&
		pDef->fRangeSecondary > 0.0f && 
		pPowerTarget->bRequireSelf && // Main target must be self
		(pDef->eEffectArea == kEffectArea_Sphere || pDef->eEffectArea == kEffectArea_Character))
	{
		return true;
	}

	return false;
}

Power* gclCursorPowerLocationTargeting_GetCurrentPower()
{
	if (s_LocationTargetState.erPowerRef.uiID)
	{
		Entity *pEnt = entActivePlayerPtr();
		if (pEnt && pEnt->pChar)
		{
			return character_FindPowerByRef(pEnt->pChar, &s_LocationTargetState.erPowerRef);
		}
	}
	return NULL;
}


#include "gclCursorModePowerLocationTargeting_c_ast.c"
