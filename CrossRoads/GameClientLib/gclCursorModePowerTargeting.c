#include "gclCursorMode.h"
#include "gclEntity.h"
#include "ClientTargeting.h"
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
#include "GameAccountDataCommon.h"
#include "Character_tick.h"
#include "PowerAnimFX.h"

#include "inputLib.h"
#include "inputMouse.h"
#include "UICore.h"
#include "WorldColl.h"
#include "WorldLib.h"
#include "StringCache.h"
#include "inputKeyBind.h"
#include "Powers.h"
#include "GfxTexAtlas.h"
#include "GfxSprite.h"
#include "UIInternal.h"
#include "gclReticle.h"
#include "CombatPowerStateSwitching.h"

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "gclCursorModePowerTargeting_c_ast.h"

#define POWERTARGETING_CURSOR_MODE_NAME	"powerTargeting"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

typedef struct PowerTargetState
{
	// The ID of the power being used for targeting
	U32 uiPowerId;
	
	// the actual ref of the power being used. Use this one to get the actual power
	PowerRef erPowerRef;
	
	// The FX used to display the direction player is targeting
	dtFx hTargetFX;

	// The node which represents the target
	dtNode hTargetNode;

	// Effect area
	EffectArea eEffectArea;

} PowerTargetState;

AUTO_STRUCT;
typedef struct PowerTargetingConfig
{
	// Cursor name
	const char *pchCursorName;				AST(NAME(CursorName) POOL_STRING)

	// The FX used for cones
	const char *pchConeFxName;				AST(NAME(ConeFxName) POOL_STRING)

	// The FX used for cylinders
	const char *pchCylinderFxName;			AST(NAME(CylinderFxName) POOL_STRING)
		
	// The name of the keybind to push when this cursor mode becomes active
	const char *pchKeybindName;				AST(NAME(KeybindName) POOL_STRING)

	// The parameter name for scaling in the FX file
	const char *pchFxParamNameScale;		AST(NAME("FxParamNameScale") POOL_STRING)

	// if set will send a message to kill it instead of a hardkill on the FX
	const char *pchFxKillMessage;			AST(NAME("FxKillMessage") POOL_STRING)


	S32 cursorX;
	S32 cursorY;

	const char* pCursorTexture2;		AST(POOL_STRING)

	U32 bAllowCharacterEffectArea : 1;
	U32 bIgnoreMouseLockState : 1;

	U32 bShowOnCursor : 1;

	U32 bMoveIntoRangeOnClick : 1;
	
} PowerTargetingConfig;

// Holds the data for targeting
static PowerTargetState s_TargetState;

// Config
static PowerTargetingConfig s_TargetingConfig;

// Forward declarations
static void gclCursorPowerTargeting_OnClick(bool bDown);
static void gclCursorPowerTargeting_OnModeEnter();
static void gclCursorPowerTargeting_OnModeExit();
static void gclCursorPowerTargeting_Update();

AUTO_STARTUP(PowerTargeting);
void CursorMode_PowerTargetingInitialize(void)
{
	// Reset the config
	StructInit(parse_PowerTargetingConfig, &s_TargetingConfig);

	// Load the config
	ParserLoadFiles(NULL, "defs/config/PowerTargetingConfig.def", "PowerTargetingConfig.bin", PARSER_OPTIONALFLAG, parse_PowerTargetingConfig, &s_TargetingConfig);

	if (s_TargetingConfig.pchCursorName)
	{
		// Register this cursor mode
		gclCursorMode_Register(	POWERTARGETING_CURSOR_MODE_NAME,
								s_TargetingConfig.pchCursorName,
								gclCursorPowerTargeting_OnClick, 
								gclCursorPowerTargeting_OnModeEnter,
								gclCursorPowerTargeting_OnModeExit,
								gclCursorPowerTargeting_Update);
	}
}

// Kills the current FX played
static void gclCursorPowerTargeting_RemoveCurrentFX(void)
{
	// Destroy the FX if necessary
	if (s_TargetState.hTargetFX)
	{
		// try and send a message to kill the FX, otherwise we'll just hard kill it
		if (s_TargetingConfig.pchFxKillMessage && s_TargetingConfig.pchFxKillMessage[0])
		{	
			dtFxSendMessage(s_TargetState.hTargetFX, s_TargetingConfig.pchFxKillMessage);
			// keep the handle around because if this starts back up again we want to hard kill it
		}
		else
		{
			dtFxKillEx(s_TargetState.hTargetFX, true, true);
			s_TargetState.hTargetFX = 0;
		}
	}
}

// -------------------------------------------------------------------------------------------------------------------------------------
static PowerDef* gclCursorPowerTargeting_GetCurrentPowerDef(Power **pPowerOut)
{
	Entity* pPlayerEnt = entActivePlayerPtr();

	if (pPlayerEnt)
	{
		Power* ppow = character_FindPowerByRef(pPlayerEnt->pChar, &s_TargetState.erPowerRef);
		if (pPowerOut)
			*pPowerOut = ppow;
		if (ppow)
		{
			return GET_REF(ppow->hDef);
		}
	}
	else if (pPowerOut)
	{
		*pPowerOut = NULL;
	}

	return NULL;
}

// -------------------------------------------------------------------------------------------------------------------------------------
static const char* gclCursorPowerTargeting_GetTargetingReticleFXName()
{
	PowerDef *pPowerDef = gclCursorPowerTargeting_GetCurrentPowerDef(NULL);
	if (pPowerDef)
	{
		PowerAnimFX *pPowerAnimFX = GET_REF(pPowerDef->hFX);
		if (pPowerAnimFX && pPowerAnimFX->pchLocTargetingReticleFxName && pPowerAnimFX->pchLocTargetingReticleFxName[0])
			return pPowerAnimFX->pchLocTargetingReticleFxName;
	}

	if (s_TargetState.eEffectArea == kEffectArea_Cone)
	{
		return s_TargetingConfig.pchConeFxName;
	}
	else if (s_TargetState.eEffectArea == kEffectArea_Cylinder)
	{
		return s_TargetingConfig.pchCylinderFxName;
	}
	
	return NULL;
}

// -------------------------------------------------------------------------------------------------------------------------------------
F32 gclCursorPowerTargeting_PowerGetRadius(Power *pPower, PowerDef *pPowerDef)
{
	F32 fRadius = 5.f; // default to 5 ft

	// Calculate the radius
	if(pPowerDef->fCursorLocationTargetRadius)
	{
		fRadius = pPowerDef->fCursorLocationTargetRadius;
		
		if (pPower && pPower->fEnhancedRadius)
			fRadius += pPower->fEnhancedRadius;
		return fRadius;
	}
	else if(pPowerDef && pPowerDef->eEffectArea == kEffectArea_Sphere && pPowerDef->pExprRadius)
	{
		fRadius = power_GetRadius(PARTITION_CLIENT, NULL, pPower, pPowerDef, NULL, NULL);
	}
	
	return fRadius;
}

// -------------------------------------------------------------------------------------------------------------------------------------
// Adds the FX to the player if not already added
static void gclCursorPowerTargeting_StartFx(void)
{
	// Get the current player
	Entity *pEnt = entActivePlayerPtr();

	if (pEnt && 
		s_TargetState.hTargetFX == 0 && 
		s_TargetState.hTargetNode)
	{
		// Set the FX name
		const char *pchCurFXName = gclCursorPowerTargeting_GetTargetingReticleFXName();
				
		if (pchCurFXName)
		{
			DynParamBlock *pFxParams = NULL;
			
			// Scale
			if (s_TargetingConfig.pchFxParamNameScale && s_TargetingConfig.pchFxParamNameScale[0])
			{
				Power *pPower = NULL;
				PowerDef* pPowerDef = gclCursorPowerTargeting_GetCurrentPowerDef(&pPower);
				if (pPowerDef)
				{
					DynDefineParam *pParam = NULL;
					F32 fRadius = gclCursorPowerTargeting_PowerGetRadius(pPower, pPowerDef);
					F32 fRange = power_GetRange(pPower, pPowerDef);
					Vec3 vecScale;
					
					pFxParams = dynParamBlockCreate();
					
					pParam = StructAlloc(parse_DynDefineParam);
					pParam->pcParamName = s_TargetingConfig.pchFxParamNameScale;
					setVec3(vecScale, fRadius, fRadius, fRange);
					MultiValSetVec3(&pParam->mvVal, &vecScale);
					
					eaPush(&pFxParams->eaDefineParams, pParam);
				}
			}

			// Add the FX to the player
			s_TargetState.hTargetFX = dtAddFx(pEnt->dyn.guidFxMan, pchCurFXName, 
												pFxParams, s_TargetState.hTargetNode, 
												0, 0, 0, NULL, eDynFxSource_UI, NULL, NULL);
		}
	}
}


static bool gclCursorPowerTargeting_GetTargetPosition(const Vec3 rayStart, const Vec3 rayDir, Vec3 vOutPos)
{
	Entity *pEnt = entActivePlayerPtr();

	if (pEnt)
	{
		Vec3 vPlayerPosition;
		Vec3 vDirection;
		Power *pPower = NULL;
		PowerDef *pDef = gclCursorPowerTargeting_GetCurrentPowerDef(&pPower);
		F32 fRange;

		if(!pDef)
			return false;

		fRange = power_GetRange(pPower, pDef);

		entGetCombatPosDir(pEnt, NULL, vPlayerPosition, NULL);
				
		copyVec3(rayDir, vDirection);
		// ignore the pitch for now
		vDirection[1] = 0.f;

		normalVec3(vDirection);

		scaleAddVec3(vDirection, fRange, vPlayerPosition, vOutPos);

		return true;
	}

	return false;	
}

static void gclCursorPowerTargeting_OnModeEnter(void)
{
	// Push the keybind profile
	if (s_TargetingConfig.pchKeybindName && s_TargetingConfig.pchKeybindName[0])
	{
		keybind_PushProfileName(s_TargetingConfig.pchKeybindName);
	}

	gclReticle_DisableDrawReticle();
	
	// Create a new node
	if (!s_TargetState.hTargetNode)
	{
		s_TargetState.hTargetNode = dtNodeCreate();
	}
}

// --------------------------------------------------------------------------------------------------------------------
static void gclCursorPowerTargeting_OnModeExit(void)
{
	Entity* pPlayerEnt = entActivePlayerPtr();
	// Pop the keybind profile
	if (s_TargetingConfig.pchKeybindName && s_TargetingConfig.pchKeybindName[0])
	{
		keybind_PopProfileName(s_TargetingConfig.pchKeybindName);
	}

	// Destroy the FX if necessary
	gclCursorPowerTargeting_RemoveCurrentFX();

	if (s_TargetingConfig.bShowOnCursor)
	{
		Power* ppow = character_FindPowerByID(pPlayerEnt->pChar, s_TargetState.uiPowerId);
		PowerDef* pdef = GET_REF(ppow->hDef);
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

	gclReticle_EnableDrawReticle();

	// Unset the power ID
	s_TargetState.uiPowerId = 0;
	powerref_Set(&s_TargetState.erPowerRef, NULL);
}

// --------------------------------------------------------------------------------------------------------------------
static bool bCancelOnMouseup = true;
static void gclCursorPowerTargeting_Update()
{
	bool bValidPlacement = false;

	Entity *pEnt = entActivePlayerPtr();

	if (pEnt == NULL || pEnt->pChar == NULL || (mouseIsLocked() && !s_TargetingConfig.bIgnoreMouseLockState))
	{
		// Hide the FX
		gclCursorPowerTargeting_RemoveCurrentFX();
		return;
	}

	if (!inpCheckHandled())
	{
		Vec3 rayStart, rayDir, vOutPos;

		target_GetCursorRay(pEnt, rayStart, rayDir);
		
		if (gclCursorPowerTargeting_GetTargetPosition(rayStart, rayDir, vOutPos))
		{
			Quat qTargetRot;
			F32 yaw = 0.f;
			
			// orient back towards the player
			yaw = addAngle(getVec3Yaw(rayDir), PI);
			yawQuat(-yaw, qTargetRot);

			dtNodeSetPosAndRot(s_TargetState.hTargetNode, vOutPos, qTargetRot);

			// Start the FX if not already started
			gclCursorPowerTargeting_StartFx();
		}
		else
		{
			// Hide the FX
			gclCursorPowerTargeting_RemoveCurrentFX();
		}

		if (inpLevelPeek(INP_LBUTTON))//are we still holding the button from a failed activation?
		{
			if (s_TargetState.eEffectArea == kEffectArea_Character)
			{
				if (!bCancelOnMouseup)
				{
					GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
					Power* ppow = character_FindPowerByRef(pEnt->pChar, &s_TargetState.erPowerRef);
					
					F32 delay;
					// Activate the power
					character_PredictTimeForNewActivation(pEnt->pChar, 1, 0, ppow, &delay);
					if (delay <= gConf.combatUpdateTimer)
					{
						character_ActivatePowerByIDClient(PARTITION_CLIENT, pEnt->pChar, s_TargetState.uiPowerId, NULL, NULL, 1, pExtract);
						bCancelOnMouseup = true;
					}
				}
				else if (!pEnt->pChar->pPowActQueued && !pEnt->pChar->pPowActCurrent && pEnt->pChar->pPowActFinished && pEnt->pChar->pPowActFinished->ref.uiID == s_TargetState.uiPowerId)
				{
					//end the state if we're still holding after a completed activation.
					gclCursorMode_ChangeToDefault();
				}
			}
		}
	}
}


// --------------------------------------------------------------------------------------------------------------------
static void gclCursorPowerTargeting_OnClick(bool bDown)
{
	Entity *e = entActivePlayerPtr();

	if (e && e->pChar && (!mouseIsLocked() || s_TargetingConfig.bIgnoreMouseLockState) & !inpCheckHandled())
	{
		if (s_TargetState.eEffectArea != kEffectArea_Character && bDown)
		{
			Entity *petEnt = NULL;
			Vec3 rayStart, rayDir, vOutPos;

			target_GetCursorRay(e, rayStart, rayDir);
			
			if (gclCursorPowerTargeting_GetTargetPosition(rayStart, rayDir, vOutPos))
			{
				GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);

				// Activate the power
				character_ActivatePowerByIDClientOnArbitraryPoint(entGetPartitionIdx(e), e->pChar, s_TargetState.uiPowerId, vOutPos, NULL, bDown, pExtract);

				// Return to default cursor mode
				gclCursorMode_ChangeToDefault();
			}
		}
		else if (s_TargetState.eEffectArea == kEffectArea_Character)
		{
			Entity* pTarget = target_SelectUnderMouse(e, 0, kTargetType_Self, NULL, true, true, false);

			if (pTarget)
			{
				GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
				Power* ppow = character_FindPowerByRef(e->pChar, &s_TargetState.erPowerRef);
				F32 delay;
				// Activate the power
				character_PredictTimeForNewActivation(e->pChar, 1, 0, ppow, &delay);
				if (delay <= gConf.combatUpdateTimer || !bDown)
				{
					character_ActivatePowerByIDClient(PARTITION_CLIENT, e->pChar, s_TargetState.uiPowerId, pTarget, NULL, bDown, pExtract);
					if (bDown)
						bCancelOnMouseup = true;
				}
				else
				{
					bCancelOnMouseup = false;
					return;
				}
			}
			// Return to default cursor mode on mouseup
			if (!bDown && bCancelOnMouseup)
				gclCursorMode_ChangeToDefault();
		}
	}
}

// --------------------------------------------------------------------------------------------------------------------
void gclCursorPowerTargeting_PowerExec()
{
	gclCursorPowerTargeting_OnClick(true);
	gclCursorMode_ChangeToDefault();
	
}

static bool s_enableonnextclicktargeting = false;
AUTO_CMD_INT(s_enableonnextclicktargeting, EnableOnNextClickTargeting);

// --------------------------------------------------------------------------------------------------------------------
// Indicates if the given power def is valid for this targeting mode
bool gclCursorPowerTargeting_PowerValid(Character *pChar, PowerDef *pDef)
{
	// If the cursor name isn't set, then assume that PowerTargeting is disabled.
	// The effect area must be cone or cylinder.
	if (pDef && s_TargetingConfig.pchCursorName &&
		!character_PowerRequiresValidTarget(pChar, pDef) && 
		pDef->fCursorLocationTargetRadius != 0.f &&
		(pDef->eEffectArea == kEffectArea_Cone || pDef->eEffectArea == kEffectArea_Cylinder))
	{
		return true;
	}
	else if (pDef && s_TargetingConfig.pchCursorName && 
				s_TargetingConfig.bAllowCharacterEffectArea && 
				s_enableonnextclicktargeting && 
				pDef->fRange > 0.0f && 
				pDef->fCursorLocationTargetRadius != 0.f &&
				pDef->eEffectArea == kEffectArea_Character)
	{
		PowerTarget* pTarget = GET_REF(pDef->hTargetMain);
		if (!pTarget->bRequireSelf)
			return true;

	}

	return false;
}

// --------------------------------------------------------------------------------------------------------------------
// Initiates mouse targeting for the given power
void gclCursorPowerTargeting_Begin(U32 uiPowerId, EffectArea eEffectArea)
{
	Entity *pEnt = entActivePlayerPtr();
	
	Power* pPow = character_FindPowerByID(pEnt->pChar, uiPowerId);

	if(!pPow)
		return;

	{
		Power *pSwitchedPower = CombatPowerStateSwitching_GetSwitchedStatePower(pEnt->pChar, pPow);
		if (pSwitchedPower)
			pPow = pSwitchedPower;
	}
	
	// Destroy the FX if necessary
	if (s_TargetState.hTargetFX)
	{
		dtFxKillEx(s_TargetState.hTargetFX, true, true);
		s_TargetState.hTargetFX = 0;
	}
	

	if (pEnt && s_TargetingConfig.pchCursorName)
	{
		// Destroy the FX if necessary. This is needed for the cases where we begin this cursor mode without exiting it first.
		gclCursorPowerTargeting_RemoveCurrentFX();

		// Set the power ID
		s_TargetState.uiPowerId = uiPowerId;
		
		// set the powerRef
		powerref_Set(&s_TargetState.erPowerRef, pPow);
		
		// Set the effect area
		s_TargetState.eEffectArea = eEffectArea;

		gclCursorMode_SetModeByName(POWERTARGETING_CURSOR_MODE_NAME);	
	}
	if (s_TargetingConfig.bShowOnCursor)
	{
		PowerDef* pdef = GET_REF(pPow->hDef);
		if (pdef)
		{
			AtlasTex* tex = atlasLoadTexture(pdef->pchIconName);
			UIDeviceState *pState = ui_StateForDevice(NULL);
			pState->cursor.draggedIcon = tex;
			pState->cursor.draggedIcon2 = atlasLoadTexture(s_TargetingConfig.pCursorTexture2);
			pState->cursor.draggedIconX = s_TargetingConfig.cursorX;
			pState->cursor.draggedIconY = s_TargetingConfig.cursorY;
		}
	}

}

// --------------------------------------------------------------------------------------------------------------------

AUTO_EXPR_FUNC(UIGen);
U32 gclCursorPowerTargeting_GetCurrentPowID()
{
	return s_TargetState.uiPowerId;
}

// --------------------------------------------------------------------------------------------------------------------
Power* gclCursorPowerTargeting_GetCurrentPower()
{
	if (s_TargetState.erPowerRef.uiID)
	{
		Entity *pEnt = entActivePlayerPtr();
		if (pEnt && pEnt->pChar)
		{
			return character_FindPowerByRef(pEnt->pChar, &s_TargetState.erPowerRef);
		}
	}

	return NULL;
}

#include "gclCursorModePowerTargeting_c_ast.c"
