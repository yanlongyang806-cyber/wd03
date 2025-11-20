#include "gclCombatReactivePower.h" 
#include "CombatReactivePower.h" 
#include "Character.h"
#include "Entity.h"
#include "GameAccountDataCommon.h"
#include "GameStringFormat.h"
#include "inventoryCommon.h"
#include "PowerAnimFX.h"
#include "ResourceManager.h"
#include "file.h"
#include "Character_combat.h"
#include "ClientTargeting.h"
#include "PowersMovement.h"
#include "NotifyCommon.h"
#include "gclEntity.h"
#include "GameClientLib.h"
#include "gclCamera.h"
#include "gclControlScheme.h"
#include "gclCombatPowerStateSwitching.h"
#include "gclCommandParse.h"
#include "gclPlayerControl.h"
#include "gclSendToServer.h"
#include "EntityMovementManager.h"
#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"

extern S32 g_CombatReactivePowerDebug;

AUTO_COMMAND ACMD_CATEGORY(Powers,Debug);
void CombatReactivePowerDebugClient(S32 enabled)
{
	g_CombatReactivePowerDebug = !!enabled;
}

#define INPUT_BIT(e)	(1 << (e))


// --------------------------------------------------------------------------------------------------------------------
bool gclCombatReactivePower_HandleDoubleTap(MovementInputValueIndex input)
{
	Entity* e = entActivePlayerPtr();
	
	if (g_CurrentScheme.bDoubleTapDirToRoll && e && e->pChar && 
		e->pChar->pCombatReactivePowerInfo && e->pChar->pCombatReactivePowerInfo->bHandlesDoubleTap && 
		!gclCharacter_HasMountedCostume(e->pChar))
	{
		CombatReactivePowerInfo	*pInfo = e->pChar->pCombatReactivePowerInfo;
		CombatReactivePowerDef *pDef = GET_REF(pInfo->hCombatBlockDef);
		
		if (pInfo->eState == ECombatReactivePowerState_NONE)
		{
			switch (input)
			{
				case MIVI_BIT_FORWARD:
				case MIVI_BIT_BACKWARD:
				case MIVI_BIT_LEFT:
				case MIVI_BIT_RIGHT:
					pInfo->iQueuedInputValue = INPUT_BIT(input);

				xdefault:
					return false;
			}
					
			gclCombatReactivePower_Activate(e->pChar);
			return true;
		}
		
	}
	
	return false;
}

static void _GetCurrentInputIfQueuedInputNotSet(CombatReactivePowerInfo *pInfo)
{
	if (!pInfo->iQueuedInputValue)
	{
		if (getControlButtonState(MIVI_BIT_LEFT))
			pInfo->iQueuedInputValue |= INPUT_BIT(MIVI_BIT_LEFT);
		if (getControlButtonState(MIVI_BIT_RIGHT))
			pInfo->iQueuedInputValue |= INPUT_BIT(MIVI_BIT_RIGHT);
		if (getControlButtonState(MIVI_BIT_BACKWARD))
			pInfo->iQueuedInputValue |= INPUT_BIT(MIVI_BIT_BACKWARD);
		if (getControlButtonState(MIVI_BIT_FORWARD))
			pInfo->iQueuedInputValue |= INPUT_BIT(MIVI_BIT_FORWARD);
	}
}

static bool _HasAnyMovementPressed()
{
	return getControlButtonState(MIVI_BIT_LEFT) || 
			getControlButtonState(MIVI_BIT_RIGHT) || 
			getControlButtonState(MIVI_BIT_BACKWARD) || 
			getControlButtonState(MIVI_BIT_FORWARD);

}

// --------------------------------------------------------------------------------------------------------------------
static bool _GetRollDirectionYaw(CombatReactivePowerInfo *pInfo, F32 *pfYawOut)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();
	
	if (pCamera)
	{
		Vec3 vCameraDirection, vKeyDirection = {0}, vDirectionOut;
		
		_GetCurrentInputIfQueuedInputNotSet(pInfo);
		
		if (pInfo->iQueuedInputValue & INPUT_BIT(MIVI_BIT_LEFT))
		{
			vKeyDirection[0] += -1.f;
		}
		if (pInfo->iQueuedInputValue & INPUT_BIT(MIVI_BIT_RIGHT))
		{
			vKeyDirection[0] += 1.f;
		}

		if (pInfo->iQueuedInputValue & INPUT_BIT(MIVI_BIT_BACKWARD))
		{
			vKeyDirection[2] += -1.f;
		}
		if (pInfo->iQueuedInputValue & INPUT_BIT(MIVI_BIT_FORWARD))
		{
			vKeyDirection[2] += 1.f;
		}
		
		
		if (!pInfo->iQueuedInputValue)
		{	// todo: make sure we queue an activate and the check when we've pressed a direction key
			return false;
		}

		pInfo->iQueuedInputValue = 0;

		if (vec3IsZero(vKeyDirection))
		{
			vKeyDirection[2] = 1.f;
		}

		gclCamera_GetFacingDirection(pCamera, false, vCameraDirection);

		rotateVecAboutAxis(atan2f(vCameraDirection[0], vCameraDirection[2]), upvec, vKeyDirection, vDirectionOut);

		*pfYawOut = getVec3Yaw(vDirectionOut);
		return true;
	}

	*pfYawOut = 0.f;

	return false;
}

// --------------------------------------------------------------------------------------------------------------------
static S32 _ActivateBlock(Character *pChar, CombatReactivePowerInfo *pInfo, CombatReactivePowerDef *pDef)
{
	F32 fTimeOffset = 0.f;
	U32 uiStartTime = pmTimestamp(0);
	F32 fActivateYaw = 0.f;

	if (++pInfo->uCurActIdOffset >= COMBAT_REACTIVE_BLOCK_ACTID_OFFSET_MAX)
		pInfo->uCurActIdOffset = 0;

	if (pDef->pRoll)
	{
		if (!_GetRollDirectionYaw(pInfo, &fActivateYaw))
			return false;
	}

	if (pDef->pExprMovementAttribCost && pDef->eCostAttrib)
	{
		if (!_HasAnyMovementPressed())
		{
			return false;
		}
	}

	if (!CombatReactivePower_Begin(pChar, pInfo, pDef, fActivateYaw, uiStartTime, 0.f))
	{	// something disallowed
		return false;			
	}

	gclCharacter_ForceDismount(pChar);

	// if this is the first input - cancel any queued powers
	{
		int iPartitionIdx = entGetPartitionIdx(pChar->pEntParent);
		character_ActOverflowCancelReason(iPartitionIdx, pChar, NULL, 0, kAttribType_Null, true);
		character_ActQueuedCancelReason(iPartitionIdx, pChar, NULL, 0, kAttribType_Null, true);
		if (pDef->bActivatingDisablesAutoAttack)
		{
			gclAutoAttack_Disable();
		}
	}


	if (pDef->bEnableStrafing)
	{
		clientTarget_SetCameraTargetingUsesDirectionKeysOverride(true, false);
	}
		
	if (gServerLink)
	{
		F32 fPing = gclServerGetCurrentPing();
		fTimeOffset = fPing * 0.5f;
		// printf ("\nPing? :%.4f  , SendTime %.4f\n", fPing, fTimeOffset);
	}


	// tell the server when we're starting
	ServerCmd_CombatReactivePower_ActivateServer(fActivateYaw, uiStartTime, pInfo->uCurActIdOffset, fTimeOffset);

	
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
void gclCombatReactivePower_UpdateNoneState(Character *pChar, CombatReactivePowerInfo *pInfo, CombatReactivePowerDef *pDef)
{
	if(pInfo->uiQueuedActivateTime)
	{
		if (CombatReactivePower_CanStartActivation(pChar, pInfo, pDef, NULL) &&
			CombatReactivePower_CanActivate(pChar, pDef, NULL))
		{
			U32 uiTime = pmTimestamp(0);
			if(uiTime >= pInfo->uiQueuedActivateTime)
			{
				_ActivateBlock(pChar, pInfo, pDef);
			}
		}

	}
}

// --------------------------------------------------------------------------------------------------------------------
void gclCombatReactivePower_UpdateActivated(Character *pChar, CombatReactivePowerInfo *pInfo, CombatReactivePowerDef *pDef)
{
	// if there is a movement cost, we're assuming that once we stop trying to move we deactivate
	if (pDef->pExprMovementAttribCost && pDef->eCostAttrib)
	{
		if (!_HasAnyMovementPressed())
		{
			gclCombatReactivePower_Deactivate(pChar);
		}
	}
}

// --------------------------------------------------------------------------------------------------------------------
void gclCombatReactivePower_Activate(Character *pChar)
{
	// see if we can block
	CombatReactivePowerDef *pDef = NULL;
	CombatReactivePowerInfo *pInfo = NULL;
	ECombatReactiveActivateFailReason eFail = 0;
	if (!pChar->pCombatReactivePowerInfo)
		return;
	
	pInfo = pChar->pCombatReactivePowerInfo;
	pDef = GET_REF(pInfo->hCombatBlockDef);
	if (!pDef)
		return;


	if ((pInfo->eState == ECombatReactivePowerState_NONE) && 
		CombatReactivePower_CanActivate(pChar, pDef, &eFail) && 
		CombatReactivePower_CanStartActivation(pChar, pInfo, pDef, &eFail))
	{
		pInfo->uiQueuedDeactivateTime = 0;
		if (!_ActivateBlock(pChar, pInfo, pDef))
		{
			pInfo->uiQueuedActivateTime = pmTimestamp(0);
		}
	}
	else
	{
		if (pInfo->eState != ECombatReactivePowerState_NONE && !CombatReactivePower_CanToggleDeactivate(pDef))
		{
			pInfo->uiQueuedActivateTime = 0;
			pInfo->iQueuedInputValue = 0;
			return;
		}


		if (eFail == ECombatReactiveActivateFailReason_NONE || 
			eFail == ECombatReactiveActivateFailReason_POWER_ACTIVATION)
		{
			pInfo->uiQueuedActivateTime = pmTimestamp(0);

			if (pDef->pRoll)
			{
				_GetCurrentInputIfQueuedInputNotSet(pInfo);
			}
		}

		if (eFail)
		{
			char *pchTemp = NULL;

			pInfo->iQueuedInputValue = 0;
			
			switch (eFail)
			{
				xcase ECombatReactiveActivateFailReason_COST:
				{
					estrStackCreate(&pchTemp);
					FormatGameMessageKey(&pchTemp,"PowersMessage.Float.FailReactiveCost", STRFMT_END);
				}
				xcase ECombatReactiveActivateFailReason_COSTRECHARGE:
				{
					estrStackCreate(&pchTemp);
					FormatGameMessageKey(&pchTemp,"PowersMessage.Float.FailReactiveCostRecharge", STRFMT_END);
				}
				xcase ECombatReactiveActivateFailReason_KNOCKED:
				{
					estrStackCreate(&pchTemp);
					FormatGameMessageKey(&pchTemp,"PowersMessage.Float.FailReactiveCostKnocked", STRFMT_END);
				}
				xcase ECombatReactiveActivateFailReason_ITEM_CATEGORY:
				{
					const char *pchMessageKey = pDef->pchRequiredItemCategoryErrorMessageKey; 
					estrStackCreate(&pchTemp);
					if (!pchMessageKey)
					{
						pchMessageKey = "PowersMessage.Float.FailReactiveItemCategory";
					}
					
					FormatGameMessageKey(&pchTemp, pchMessageKey, STRFMT_END);
				}
				xcase ECombatReactiveActivateFailReason_DISABLED:
				{
					estrStackCreate(&pchTemp);
					FormatGameMessageKey(&pchTemp,"PowersMessage.Float.FailReactiveDisabled", STRFMT_END);
				}
				xcase ECombatReactiveActivateFailReason_DISALLOWEDMODE:
				{
					estrStackCreate(&pchTemp);
					FormatGameMessageKey(&pchTemp,"PowersMessage.Float.FailReactiveDisallowedMode", STRFMT_END);
				}
			}

			if (pchTemp)
			{
				notify_NotifySend(pChar->pEntParent, kNotifyType_PowerExecutionFailed, pchTemp, NULL, NULL);
				estrDestroy(&pchTemp);
			}
		}
		
	}
}

// --------------------------------------------------------------------------------------------------------------------
void gclCombatReactivePower_Deactivate(Character *pChar)
{
	CombatReactivePowerDef *pDef = NULL;
	CombatReactivePowerInfo *pInfo = NULL;

	if (!pChar->pCombatReactivePowerInfo)
		return;

	pInfo = pChar->pCombatReactivePowerInfo;
	pDef = GET_REF(pInfo->hCombatBlockDef);
	if (!pDef)
		return;

	
	if (CombatReactivePower_CanToggleDeactivate(pDef))
	{
		U32 uiStartTime = pmTimestamp(0);
		
		pInfo->uiQueuedActivateTime = 0;

		if (pInfo->eState == ECombatReactivePowerState_ACTIVATED)
		{
			uiStartTime = pmTimestampFrom(uiStartTime, gConf.combatUpdateTimer);
		}

		ServerCmd_CombatReactivePower_DeactivateServer(uiStartTime, pInfo->eState, pInfo->fTimer);
		
		CombatReactivePower_Stop(pChar, pInfo, pDef, uiStartTime, false);		
	}
	else if (pDef->pRoll)
	{
		if (!pInfo->iQueuedInputValue)
		{
			pInfo->uiQueuedActivateTime = 0;
		}
	}

}

// --------------------------------------------------------------------------------------------------------------------
static void _Reset_Movement_Control(Character *pChar, CombatReactivePowerInfo *pInfo, CombatReactivePowerDef *pDef)
{
	if (pDef->pRoll)
	{
		pInfo->iQueuedInputValue = 0;
	}
}

// --------------------------------------------------------------------------------------------------------------------
void gclCombatReactivePower_OnStopBlock(Character *pChar, CombatReactivePowerInfo *pInfo, CombatReactivePowerDef *pDef)
{
	if (pDef->bEnableStrafing)
	{
		clientTarget_SetCameraTargetingUsesDirectionKeysOverride(false, false);
	}
	
	_Reset_Movement_Control(pChar, pInfo, pDef);
}


// --------------------------------------------------------------------------------------------------------------------
void gclCombatReactivePower_State_QueuedActivate_Cancel(Character *pChar, CombatReactivePowerInfo *pInfo, CombatReactivePowerDef *pDef)
{
	_Reset_Movement_Control(pChar, pInfo, pDef);

	if (pDef->pchCombatPowerState)
		gclCombatPowerStateSwitching_ExitCurrentState(pChar);
}

// --------------------------------------------------------------------------------------------------------------------
void gclCombatReactivePower_State_Preactivate_Cancel(Character *pChar, CombatReactivePowerInfo *pInfo, CombatReactivePowerDef *pDef)
{
	_Reset_Movement_Control(pChar, pInfo, pDef);

	if (pDef->pchCombatPowerState)
		gclCombatPowerStateSwitching_ExitCurrentState(pChar);
}

// --------------------------------------------------------------------------------------------------------------------
void gclCombatReactivePower_State_Activated_Cancel(Character *pChar, CombatReactivePowerInfo *pInfo, CombatReactivePowerDef *pDef)
{
	if (pDef->pchCombatPowerState)
		gclCombatPowerStateSwitching_ExitCurrentState(pChar);

	_Reset_Movement_Control(pChar, pInfo, pDef);
}

// --------------------------------------------------------------------------------------------------------------------
void gclCombatReactivePower_State_Preactivate_Enter(Character *pChar, CombatReactivePowerInfo *pInfo, CombatReactivePowerDef *pDef)
{
	_Reset_Movement_Control(pChar, pInfo, pDef);
}

// --------------------------------------------------------------------------------------------------------------------

AUTO_COMMAND ACMD_NAME("CombatReactivePowerExec") ACMD_ACCESSLEVEL(0);
void gclCombatReactivePower_Exec(bool bActive)
{
	Entity* e = entActivePlayerPtr();

	if (e && e->pChar)
	{
		if (bActive)
		{
			gclCombatReactivePower_Activate(e->pChar);
		}
		else
		{
			gclCombatReactivePower_Deactivate(e->pChar);
		}
	}
}

// --------------------------------------------------------------------------------------------------------------------

// Returns true if the reactive power is defined to be a roll
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CombatReactivePower_ReactiveIsRoll);
S32 exprCombatReactivePower_ReactiveIsRoll()
{
	Entity* e = entActivePlayerPtr();

	if (e && e->pChar && e->pChar->pCombatReactivePowerInfo)
	{
		CombatReactivePowerDef *pDef = GET_REF(e->pChar->pCombatReactivePowerInfo->hCombatBlockDef);

		return (pDef && pDef->pRoll != NULL);
	}

	return false;
}

