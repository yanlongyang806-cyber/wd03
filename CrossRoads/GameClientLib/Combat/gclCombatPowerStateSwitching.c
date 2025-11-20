#include "CombatPowerStateSwitching.h"

#include "Character.h"
#include "ClientTargeting.h"
#include "Entity.h"
#include "gclCombatPowerStateSwitching.h"
#include "gclCursorModePowerTargeting.h"
#include "gclCursorModePowerLocationTargeting.h"
#include "gclCursorMode.h"
#include "gclEntity.h"
#include "PowersMovement.h"
#include "NotifyCommon.h"
#include "GameStringFormat.h"
#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "PowersAutoDesc.h"
#include "PowerSlots.h"
#include "StringCache.h"

// -------------------------------------------------------------------------------------------------------------------
static CombatPowerStateDef* _GetNextMode(	Character *pChar,
											const char *pchCurMode, 
											CombatPowerStateSwitchingDef *pDef,
											ECombatPowerStateModeFailReason *peFailReason,
											CombatPowerStateDef **pFailedStateOut)
{
	S32 iStartIdx = -1;
	S32 i = 0, iCount = 0, iSize = eaSize(&pDef->eaStates);

	if (!iSize)
		return NULL;

	if (!pchCurMode)
	{
		CombatPowerStateDef* pNextMode = NULL;
		iStartIdx = 0;
	}
	else if (iSize > 1)
	{

		// our current mode, find the next
		for (i = 0; i < iSize; ++i)
		{
			CombatPowerStateDef *pMode = pDef->eaStates[i];
			if (pMode->pchName == pchCurMode)
			{
				iStartIdx = i + 1;
				if (iStartIdx >= iSize)
					iStartIdx = 0;
				break;
			}
		}
	}
	else return NULL;

	i = iStartIdx;
	do {
		if (CombatPowerStateSwitching_CanEnterState(pChar, pDef->eaStates[i], peFailReason))
		{
			return pDef->eaStates[i];
		}

		if (pFailedStateOut && !(*pFailedStateOut))
		{
			*pFailedStateOut = pDef->eaStates[i];
		}

		if (++i >= iSize)
			i = 0;
	} while(++iCount < iSize);

	return NULL;
}

// -------------------------------------------------------------------------------------------------------------------
static void _EnterState(Character *pChar, 
						CombatPowerStateSwitchingInfo *pState, 
						CombatPowerStateSwitchingDef *pDef, 
						CombatPowerStateDef *pNewState)
{
	U32 uiTime = pmTimestamp(0);
		
	{
		CombatPowerStateDef *pOldState = NULL;
		if (pState->pchCurrentState)
			pOldState = CombatPowerStateSwitching_GetStateByName(pState->pchCurrentState, pDef);

		if (pOldState == pNewState)
		{
			return;
		}

		if (pDef->bDismountOnStateSwitch)
		{
			gclCharacter_ForceDismount(pChar);
		}

		CombatPowerStateSwitching_ExitState(pChar, pDef, pState, pOldState, uiTime);
	}

	if (pNewState)
	{
		pState->uCurActIdOffset++;
		if (pState->uCurActIdOffset > 10)
			pState->uCurActIdOffset = 0;
	}

	ServerCmd_gslCombatPowerStateSwitching_ActivateServer((pNewState ? pNewState->pchName : NULL), uiTime, pState->uCurActIdOffset);

	CombatPowerStateSwitching_EnterState(pChar, pState, pNewState, uiTime);

	pState->fCooldown = pDef->fSwitchCooldown;

}

// -------------------------------------------------------------------------------------------------------------------
static void _NotifyFailReason(Character *pChar, ECombatPowerStateModeFailReason eFailReason, CombatPowerStateDef *pFailedState)
{
	// was not able to cycle a mode, send a notification if we have a valid reason
	if (!gConf.bHideCombatMessages)
	{
		char *pchTemp = NULL;
		Language eLang = locGetLanguage(getCurrentLocale());

		switch (eFailReason)
		{
			xcase ECombatPowerStateModeFailReason_COST:
			{
				char const * pchAttrName = NULL;

				if (pFailedState)
					pchAttrName = attrib_AutoDescName(pFailedState->eRequiredAttrib, eLang);

				estrStackCreate(&pchTemp);
				FormatGameMessageKey(&pchTemp,"PowersMessage.Float.PowerStateSwitchingCost", 
									STRFMT_STRING("Resource",pchAttrName), STRFMT_END);
			}

			xcase ECombatPowerStateModeFailReason_DISALLOWEDMODE:
			{
				estrStackCreate(&pchTemp);
				FormatGameMessageKey(&pchTemp,"PowersMessage.Float.PowerStateSwitchDisallowMode", STRFMT_END);
			}

			xcase ECombatPowerStateModeFailReason_KNOCKED:
			{
				estrStackCreate(&pchTemp);
				FormatGameMessageKey(&pchTemp,"PowersMessage.Float.PowerStateSwitchKnocked", STRFMT_END);
			}

			xcase ECombatPowerStateModeFailReason_DISABLEDATTRIB:
			{
				estrStackCreate(&pchTemp);
				FormatGameMessageKey(&pchTemp,"PowersMessage.Float.PowerStateSwitchDisabled", STRFMT_END);
			}
		}

		if (pchTemp)
		{
			notify_NotifySend(pChar->pEntParent, kNotifyType_PowerExecutionFailed, pchTemp, NULL, NULL);
			estrDestroy(&pchTemp);
		}
	}
}

// -------------------------------------------------------------------------------------------------------------------
void gclCombatPowerStateSwitching_CycleNextState(Character *pChar)
{
	CombatPowerStateSwitchingDef *pDef = NULL;
	CombatPowerStateSwitchingInfo *pState = NULL;
	ECombatPowerStateModeFailReason eFailReason = ECombatPowerStateModeFailReason_NONE;
	if (!pChar->pCombatPowerStateInfo)
	{
		return;
	}

	pState = pChar->pCombatPowerStateInfo;
	pDef = GET_REF(pState->hCombatPowerStateSwitchingDef);
	if (!pDef)
		return;

	// check if we can cycle out of the current state
	{
		CombatPowerStateDef *pCurState = CombatPowerStateSwitching_GetStateByName(pChar->pCombatPowerStateInfo->pchCurrentState, pDef);
		if (pCurState && pCurState->bDisallowUserExitState)
			return;
	}

	if (!CombatPowerStateSwitching_CanStateSwitch(pChar, pDef, &eFailReason))
	{
		_NotifyFailReason(pChar, eFailReason, NULL);
		return;
	}
	
	
	if (pState->fCooldown <= 0.f)
	{
		CombatPowerStateDef *pNewState = NULL;
		CombatPowerStateDef *pFailedState = NULL;

		pNewState = _GetNextMode(pChar, pState->pchCurrentState, pDef, &eFailReason, &pFailedState);

		if (eFailReason)
		{
			_NotifyFailReason(pChar, eFailReason, pFailedState);
			return;
		}

		_EnterState(pChar, pState, pDef, pNewState);
	}
}

// -------------------------------------------------------------------------------------------------------------------
S32 gclCombatPowerStateSwitching_CanCycleState(Character *pChar)
{
	CombatPowerStateSwitchingDef *pDef = NULL;
	
	if (!pChar->pCombatPowerStateInfo)
	{
		return false;
	}

	pDef = GET_REF(pChar->pCombatPowerStateInfo->hCombatPowerStateSwitchingDef);
	if (!pDef)
		return false;

	if (pDef->bDisallowStateCycling)
		return false;
		

	return true;
}

// -------------------------------------------------------------------------------------------------------------------
void gclCombatPowerStateSwitching_EnterStateByName(Character *pChar, const char *pchMode)
{
	if (pChar->pCombatPowerStateInfo)
	{
		CombatPowerStateSwitchingDef *pDef = NULL;
		CombatPowerStateDef *pNewState = NULL;
		
		pDef = GET_REF(pChar->pCombatPowerStateInfo->hCombatPowerStateSwitchingDef);
		if (!pDef)
			return;

		if (!CombatPowerStateSwitching_CanStateSwitch(pChar, pDef, NULL))
			return;

		pNewState = CombatPowerStateSwitching_GetStateByName(pchMode, pDef);
		
		_EnterState(pChar, pChar->pCombatPowerStateInfo, pDef, pNewState);
	}
	
}

// -------------------------------------------------------------------------------------------------------------------
void gclCombatPowerStateSwitching_ExitCurrentState(Character *pChar)
{
	if (pChar->pCombatPowerStateInfo)
	{
		CombatPowerStateSwitchingDef *pDef = NULL;

		pDef = GET_REF(pChar->pCombatPowerStateInfo->hCombatPowerStateSwitchingDef);
		if (!pDef)
			return;

		_EnterState(pChar, pChar->pCombatPowerStateInfo, pDef, NULL);
	}

}

// -------------------------------------------------------------------------------------------------------------------
static Power* _GetRootPower(Power *pPower)
{
	if (pPower->pParentPower)
		pPower = pPower->pParentPower;
	if (pPower->pCombatPowerStateParent)
		pPower = pPower->pCombatPowerStateParent;
	return pPower;
}

// -------------------------------------------------------------------------------------------------------------------
static S32 _ShouldCancelCursorPowerTargetingDueToStateExit(	Character *pChar, 
															CombatPowerStateSwitchingDef *pDef, 
															const char *pchModeName)
{
	Power *pPower = gclCursorPowerTargeting_GetCurrentPower();
	if (pPower)
	{
		if (CombatPowerStateSwitching_DoesPowerMatchMode(pPower, pchModeName))
			return true;

		if (pDef->eaPowerSlotSet)
		{
			pPower = _GetRootPower(pPower);
			if (CombatPowerStateSwitching_IsPowerSlottedInSet(pChar, pDef, pPower, pchModeName))
				return true;
		}
	}

	pPower = gclCursorPowerLocationTargeting_GetCurrentPower();
	if (pPower && CombatPowerStateSwitching_DoesPowerMatchMode(pPower, pchModeName))
	{
		if (CombatPowerStateSwitching_DoesPowerMatchMode(pPower, pchModeName))
			return true;

		if (pDef->eaPowerSlotSet)
		{
			pPower = _GetRootPower(pPower);

			if (CombatPowerStateSwitching_IsPowerSlottedInSet(pChar, pDef, pPower, pchModeName))
				return true;
		}
	}

	return false;
}


// -------------------------------------------------------------------------------------------------------------------
void gclCombatpowerStateSwitching_OnExit(	Character *pChar, 
											CombatPowerStateSwitchingDef *pDef, 
											CombatPowerStateSwitchingInfo *pState, 
											CombatPowerStateDef *pModeDef)
{
	const char *pchModeName = pModeDef ? pModeDef->pchName : NULL;

	pState->uAutoAttackLastPowerID = 0;

	if (_ShouldCancelCursorPowerTargetingDueToStateExit(pChar, pDef, pchModeName))
	{
		gclCursorMode_ChangeToDefault();
	}

	// see if auto-attack is enabled for a power that was in the last state, if so disable auto-attack
	if (gclAutoAttack_IsEnabled())
	{
		U32 powID = gclAutoAttack_GetExplicitPowerID();
		if (CombatPowerStateSwitching_IsPowerIDSlottedInSet(pChar, pDef, powID, pchModeName))
		{
			pState->uAutoAttackLastPowerID = powID;
			gclAutoAttack_Disable();
		}
	}
}

// -------------------------------------------------------------------------------------------------------------------
void gclCombatpowerStateSwitching_OnEnter(Character *pChar, 
											CombatPowerStateSwitchingInfo *pState)
{
	CombatPowerStateSwitchingDef *pDef = GET_REF(pState->hCombatPowerStateSwitchingDef);

	if (!pDef)
		return;

	if (pState->uAutoAttackLastPowerID)
	{
		S32 iLastAutoAttackSlot = character_PowerIDSlot(pChar, pState->uAutoAttackLastPowerID);
		if (iLastAutoAttackSlot >= 0)
		{
			S32 iActSlot = CombatPowerStateSwitching_GetCorrespondingStatePowerSlot(pChar, 
																			pState->pchCurrentState,
																			pState->pchLastState, 
																			iLastAutoAttackSlot);
			if (iActSlot >= 0)
			{
				U32 uPowID = character_PowerSlotGetFromTray(pChar, -1, iActSlot);
				if (uPowID)
				{
					gclAutoAttack_SetExplicitPowerEnabledID(uPowID);
					gclAutoAttack_DefaultAutoAttack(true);
				}
			}
		}
	}

}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void CombatPowerStateCycleNext()
{
	Entity* e = entActivePlayerPtr();

	if (e && e->pChar && gclCombatPowerStateSwitching_CanCycleState(e->pChar))
	{
		gclCombatPowerStateSwitching_CycleNextState(e->pChar);
	}
}

// returns true if the character is not in a defined state (the default)
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CombatPowerState_IsInDefaultState);
S32 exprCombatPowerState_IsInDefaultState()
{
	Entity* e = entActivePlayerPtr();
	
	if (e && e->pChar && e->pChar->pCombatPowerStateInfo)
	{
		return e->pChar->pCombatPowerStateInfo->pchCurrentState == NULL;
	}

	return false;
}

// -------------------------------------------------------------------------------------------------------------------

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMDFAST;
void gclCombatPowerStateSwitching_ExitState(const char *pchOldState)
{
	Entity* e = entActivePlayerPtr();

	if (e && e->pChar && e->pChar->pCombatPowerStateInfo)
	{
		CombatPowerStateSwitchingInfo *pState = e->pChar->pCombatPowerStateInfo;
		if (pchOldState)
		{
			pchOldState = allocFindString(pchOldState);
			devassertmsg(pchOldState, "CombatPowerStateSwitching_CancelState: Invalid state sent from server.");
		}

		if (pState->pchCurrentState == pchOldState)
		{
			CombatPowerStateSwitchingDef *pDef = NULL;
			CombatPowerStateDef *pMode = NULL;
			
			pDef = GET_REF(pState->hCombatPowerStateSwitchingDef);
			if (!pDef) return;

			pMode = CombatPowerStateSwitching_GetStateByName(pState->pchCurrentState, pDef);

			CombatPowerStateSwitching_ExitState(e->pChar, pDef, pState, pMode, pmTimestamp(0));
		}
	}
}