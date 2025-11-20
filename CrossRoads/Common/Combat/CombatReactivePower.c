
#include "CombatReactivePower.h" 
#include "Character.h"
#include "Entity.h"
#include "GameAccountDataCommon.h"
#include "inventoryCommon.h"
#include "PowerAnimFX.h"
#include "ResourceManager.h"
#include "file.h"
#include "Character_combat.h"
#include "CombatConfig.h"
#include "CombatEval.h"

#if GAMECLIENT || GAMESERVER
	#include "PowersMovement.h"
	#include "EntityMovementTactical.h"
	#include "EntityMovementDefault.h"
	#include "EntityMovementManager.h"
	#include "CombatPowerStateSwitching.h"
	#include "PowerSlots.h"
	#include "CharacterAttribsMinimal_h_ast.h"
#endif

#if GAMECLIENT
	#include "gclCombatReactivePower.h"
	#include "gclCombatPowerStateSwitching.h"
#endif

#if GAMESERVER
	#include "gslCombatReactivePower.h"
#endif

#include "CombatReactivePower_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

#define COMBAT_REACTIVE_BLOCK_ACTID				0x4000BEE5


DictionaryHandle g_hCombatReactivePowerDefDict;

S32 g_CombatReactivePowerDebug = false;

#define CombatReactivePowerDebugPrintEnt(pchar,format, ...) if(g_CombatReactivePowerDebug) combatdebug_PowersDebugPrint((pchar)->pEntParent,EPowerDebugFlags_REACTIVE,format,##__VA_ARGS__)


static void _HandleInitialActivationAttribCost(Character *pChar, CombatReactivePowerDef *pDef);

// -------------------------------------------------------------------------------------------------------------------
static int CombatReactivePowerDefValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, 
											CombatReactivePowerDef *pDef, U32 userID)
{
	switch (eType)
	{
	case RESVALIDATE_POST_TEXT_READING:
		if (pDef->pExprMovementAttribCost)
		{
			combateval_Generate(pDef->pExprMovementAttribCost, kCombatEvalContext_Activate);
		}

		if (pDef->eCostAttrib < 0 || !IS_NORMAL_ATTRIB(pDef->eCostAttrib))
		{
			pDef->eCostAttrib = 0;
			ErrorFilenamef(pDef->pchFilename, "CostAttrib is not set to a valid normal attrib.");
		}
		if (pDef->eMaxCostAttrib < 0 || !IS_NORMAL_ATTRIB(pDef->eMaxCostAttrib))
		{
			pDef->eMaxCostAttrib = 0;
			ErrorFilenamef(pDef->pchFilename, "MaxCostAttrib is not set to a valid normal attrib.");
		}
		return VALIDATE_HANDLED;
	}

	return VALIDATE_NOT_HANDLED;
}

// ------------------------------------------------------------------------------------------------------------------------------
AUTO_RUN;
int CombatReactivePower_RegisterDefsDict(void)
{
	// Don't load on app servers, other than specific servers
	if (IsAppServerBasedType()) 
		return 0;

	g_hCombatReactivePowerDefDict = RefSystem_RegisterSelfDefiningDictionary("CombatReactivePowerDef",
																		false, 
																		parse_CombatReactivePowerDef, 
																		true, 
																		true, 
																		NULL);

	resDictManageValidation(g_hCombatReactivePowerDefDict, CombatReactivePowerDefValidateCB);
	resDictSetDisplayName(g_hCombatReactivePowerDefDict, "CombatReactivePowerDef", "CombatReactivePowerDefs", RESCATEGORY_DESIGN);

	resDictMaintainInfoIndex(g_hCombatReactivePowerDefDict, ".Name", NULL, NULL, NULL, NULL);
	
	return 1;
}

// -------------------------------------------------------------------------------------------------------------------
AUTO_STARTUP(AS_CombatReactivePower) ASTRT_DEPS(ItemTags, Powers);
void CombatReactivePower_LoadDefs(void)
{
	// Don't load on app servers, other than specific servers
	if (IsAppServerBasedType()) 
		return;

	resLoadResourcesFromDisk(g_hCombatReactivePowerDefDict, "defs/powers", 
							".reactive", "ReactivePower.bin",  PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY);
}

// --------------------------------------------------------------------------------------------------------------------
static void CombatReactivePower_ValidateDef(CombatReactivePowerDef *pDef)
{

}

// --------------------------------------------------------------------------------------------------------------------
S32 CombatReactivePower_IsActive(Character *pChar)
{
	return (pChar->pCombatReactivePowerInfo && pChar->pCombatReactivePowerInfo->eState != ECombatReactivePowerState_NONE);
}

// -------------------------------------------------------------------------------------------------------------------
static U32 _GetCurrentAnimFxActID(CombatReactivePowerInfo *pInfo)
{
	return COMBAT_REACTIVE_BLOCK_ACTID + pInfo->uCurActIdOffset;
}

// --------------------------------------------------------------------------------------------------------------------
static U32 _GetCurrentSlowActID(CombatReactivePowerInfo *pInfo, CombatReactivePowerDef *pDef, bool bPreactivate)
{
	if (pDef->fPreactivateSpeedScale == pDef->fActivateSpeedScale)
		return COMBAT_REACTIVE_BLOCK_ACTID + pInfo->uCurActIdOffset;

	if (bPreactivate)
		return COMBAT_REACTIVE_BLOCK_ACTID + pInfo->uCurActIdOffset + COMBAT_REACTIVE_BLOCK_ACTID_OFFSET_MAX;
	
	return COMBAT_REACTIVE_BLOCK_ACTID + pInfo->uCurActIdOffset;
}

// --------------------------------------------------------------------------------------------------------------------
// returns true if it can be deactivated manually
S32 CombatReactivePower_CanToggleDeactivate(CombatReactivePowerDef *pDef)
{
	return (pDef->pRoll == NULL);
}

static S32 _CanCancelPowerActivation(Character *pChar, CombatReactivePowerDef *pDef, PowerActivation *pAct)
{
	if (pAct->eActivationStage == kPowerActivationStage_Charge)
	{	// allow canceling at any point in the charge
		// todo: maybe don't allow cancel in the last 0.15s of the charge if the hit frame is too close to start of activation
		return true;
	}

	if (pAct->eActivationStage == kPowerActivationStage_LungeGrab)
		return false;

	if (pAct->eActivationStage == kPowerActivationStage_Activate)
	{
		PowerDef *pPowerDef = GET_REF(pAct->hdef);
		PowerAnimFX *pAnimFX = NULL;
		F32 fHitTime = 0.f;
		if (!pPowerDef)
			return false;

		if (pDef->piAllowCancelPowerCategory && pPowerDef->piCategories)
		{
			S32 i;
			for (i = eaiSize(&pDef->piAllowCancelPowerCategory)-1; i >= 0; --i)
			{
				if (eaiFind(&pPowerDef->piCategories, pDef->piAllowCancelPowerCategory[i]) >= 0)
					return true;
			}
		}
		
		pAnimFX = GET_REF(pPowerDef->hFX);
		
		// get the hit time
		if (eaiSize(&pAnimFX->piFramesBeforeHit))
			fHitTime = pAnimFX->piFramesBeforeHit[0]/PAFX_FPS;
		
		// see if this power qualifies to be canceled before the hitframe
		// if we don't have a lunge, allow to be canceled 
		if (!pAnimFX->pLunge && !pPowerDef->bHasPredictedMods &&
			!pPowerDef->bHasTeleportAttrib && 
			pAct->fTimeActivating < fHitTime - 0.325f)
			return true;
				
		if (!pPowerDef->fTimeOverrideReactivePower && pAct->fTimeActivating >= fHitTime + 0.2f)
			return true;
		
		if (pAct->fTimeActivating > fHitTime && pPowerDef->fTimeOverrideReactivePower && 
			pChar->pPowActCurrent->fTimerActivate < pPowerDef->fTimeOverrideReactivePower)
			return true;
	}

	return false;
}

// --------------------------------------------------------------------------------------------------------------------
S32 CombatReactivePower_CanStartActivation(	Character *pChar, 
											CombatReactivePowerInfo *pInfo, 
											CombatReactivePowerDef *pDef,
											ECombatReactiveActivateFailReason *peFailOut)
{
	if (peFailOut) *peFailOut = ECombatReactiveActivateFailReason_NONE;
	
	if (pInfo->bLockedOutUntilMaxCost)
	{
		if (peFailOut) *peFailOut = ECombatReactiveActivateFailReason_COSTRECHARGE;
		return false;
	}

	if (pDef->eCostAttrib && pDef->fRequiredAmountToActivate > 0)
	{
		if (*F32PTR_OF_ATTRIB(pChar->pattrBasic, pDef->eCostAttrib) < pDef->fRequiredAmountToActivate)
		{
			if (peFailOut) *peFailOut = ECombatReactiveActivateFailReason_COST;
			return false;
		}
	}
	
	if (pChar->pPowActCurrent)
	{
		if (!_CanCancelPowerActivation(pChar, pDef, pChar->pPowActCurrent))
		{
			if (peFailOut) *peFailOut = ECombatReactiveActivateFailReason_POWER_ACTIVATION;
			return false;
		}
	}

	if (pInfo->fCooldown > 0.f)
	{
		return false;
	}
	
	if (pDef->pRoll)
	{
		if (entIsRolling(pChar->pEntParent))
		{
			if (peFailOut) *peFailOut = ECombatReactiveActivateFailReason_OTHER;
			return false;
		}
	}

	return true;
}


// --------------------------------------------------------------------------------------------------------------------
S32 CombatReactivePower_CanActivate(Character *pChar, CombatReactivePowerDef *pDef, ECombatReactiveActivateFailReason *peFailOut)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	if (peFailOut) *peFailOut = ECombatReactiveActivateFailReason_NONE;

	if (pmKnockIsActive(pChar->pEntParent))
	{
		if (peFailOut) *peFailOut = ECombatReactiveActivateFailReason_KNOCKED;
		return false;
	}

	if (!entIsAlive(pChar->pEntParent))
	{
		if (peFailOut) *peFailOut = ECombatReactiveActivateFailReason_DEAD;
		return false;
	}

	if (pChar->pNearDeath)
	{
		if (peFailOut) *peFailOut = ECombatReactiveActivateFailReason_NEARDEATH;
		return false;
	}

	if (pDef->iCombatLevelLockout && pDef->iCombatLevelLockout > pChar->iLevelCombat)
	{
		if (peFailOut) *peFailOut = ECombatReactiveActivateFailReason_OTHER;
		return false;
	}

	if (pDef->eCostAttrib && !pDef->pRoll)
	{
		if (*F32PTR_OF_ATTRIB(pChar->pattrBasic, pDef->eCostAttrib) <= 0.f)
		{
			if (peFailOut) *peFailOut = ECombatReactiveActivateFailReason_COST;
			return false;
		}
	}

	if (pDef->iDisallowedPowerMode >= 0)
	{
		if (character_HasMode(pChar, pDef->iDisallowedPowerMode))
		{
			if (peFailOut) *peFailOut = ECombatReactiveActivateFailReason_DISALLOWEDMODE;
			return false;
		}
	}

	if (pDef->eaDisabledAttribs)
	{
		S32 i = eaiSize(&pDef->eaDisabledAttribs);
		for (i = eaiSize(&pDef->eaDisabledAttribs) - 1; i >= 0; --i)
		{
			S32 attrib = pDef->eaDisabledAttribs[i];
			if (attrib >= 0 && IS_NORMAL_ATTRIB(attrib))
			{
				F32 fAttrib = *F32PTR_OF_ATTRIB(pChar->pattrBasic, attrib);
				if (fAttrib > 0)
				{
					if (peFailOut) *peFailOut = ECombatReactiveActivateFailReason_DISABLED;
					return false;
				}
			}
		}

	}
	
	// check if we have the required items
	if (pDef->peRequiredItemCategory)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pChar->pEntParent);
		if (pExtract)
		{	// see if we have any of the required items equipped
			if (!inv_ent_HasAnyEquippedItemsWithCategory(pChar->pEntParent, pDef->peRequiredItemCategory, pExtract))
			{
				if (peFailOut) *peFailOut = ECombatReactiveActivateFailReason_ITEM_CATEGORY;
				return false;
			}
		}
	}


#endif

	return true;
}

// --------------------------------------------------------------------------------------------------------------------
void CombatReactivePower_InitCharacter(Character *pChar, const char *pszDef)
{
	if (pszDef)
	{
		CombatReactivePowerInfo *pInfo;
		CombatReactivePowerDef *pDef; 
		pInfo = StructAlloc(parse_CombatReactivePowerInfo);

		REF_HANDLE_SET_FROM_STRING(g_hCombatReactivePowerDefDict, pszDef, pInfo->hCombatBlockDef);

		pDef = GET_REF(pInfo->hCombatBlockDef);
		if (pDef)
		{
			pChar->pCombatReactivePowerInfo = pInfo;
			pChar->pCombatReactivePowerInfo->bIgnoreHitFxDuringActivation = pDef->bIgnoreHitFxDuringActivation;
			pChar->pCombatReactivePowerInfo->bHandlesDoubleTap = (pDef->bCanActivateByDoubleTap || pDef->pRoll != NULL);
		}
		else
		{
			Errorf("CombatReactivePower_InitCharacter: Could not find def %s.", pszDef);
			StructDestroy(parse_CombatReactivePowerInfo, pInfo);
		}
	}
}


// --------------------------------------------------------------------------------------------------------------------
// sets the combat visuals exit time if we have one
static void _UpdateCombatVisualsTime(Character *pChar, CombatReactivePowerDef *pDef)
{
	if (pDef->fInCombatTimer)
	{
		character_SetCombatVisualsExitTime(pChar, pDef->fInCombatTimer);
	}
}

// --------------------------------------------------------------------------------------------------------------------
// utility function to activate the power
static void _ApplyPower(Character *pChar, CombatReactivePowerInfo *pInfo, CombatReactivePowerDef *pDef)
{
#if GAMESERVER
	PowerDef *pPowerDef = GET_REF(pDef->hActivatePowerDef);
	if(pPowerDef)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pChar->pEntParent);
		ApplyUnownedPowerDefParams applyParams = {0};

		applyParams.erTarget = entGetRef(pChar->pEntParent);
		applyParams.pcharSourceTargetType = pChar;
		applyParams.pclass = character_GetClassCurrent(pChar);
		applyParams.iLevel = entity_GetCombatLevel(pChar->pEntParent);
		applyParams.fTableScale = 1.f;
		applyParams.erModOwner = applyParams.erTarget;
		applyParams.pExtract = pExtract;
		// applyParams.uiApplyID = _GetCurrentAnimFxActID(pInfo);
		character_ApplyUnownedPowerDef(entGetPartitionIdx(pChar->pEntParent), pChar, pPowerDef, &applyParams);
		
		CombatReactivePowerDebugPrintEnt(pChar, "CombatReactivePower: Activating power.");
	}
#endif
}

// -------------------------------------------------------------------------------------------------------------------
// attempts to cancel all attribMods on the character that were applied from the hActivatePowerDef
static void _CancelPower(Character *pChar, CombatReactivePowerInfo *pInfo, CombatReactivePowerDef *pDef)
{
#if GAMESERVER
	PowerDef *pPowerDef = GET_REF(pDef->hActivatePowerDef);
	if(pPowerDef)
	{
		character_CancelModsFromDef(pChar, pPowerDef, entGetRef(pChar->pEntParent), 0, false);
		
		CombatReactivePowerDebugPrintEnt(pChar, "CombatReactivePower: Canceling power.");
	}
#endif
}

// --------------------------------------------------------------------------------------------------------------------
// utility function to cancel the bits that were setup for the 
static void _CancelActiveSpeedAndStickyAnimFx(	Character *pChar, 
												CombatReactivePowerInfo *pInfo, 
												CombatReactivePowerDef *pDef, 
												U32 uiDeactivateTime,
												bool bCancelSticky, 
												bool bCancelSpeeds)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	// schedule the speed stop
	if (bCancelSpeeds && pDef->fActivateSpeedScale >= 0.f)
	{
		U32 uiSpeedPenaltyActID = _GetCurrentSlowActID(pInfo, pDef, false);

		if (pDef->fActivateSpeedScale == 0.f)
		{
			pmIgnoreStop(pChar, pChar->pPowersMovement, uiSpeedPenaltyActID, 
							kPowerAnimFXType_ReactivePower, uiDeactivateTime);

			CombatReactivePowerDebugPrintEnt(pChar, "Cancel Root Stop: %u at %u", uiSpeedPenaltyActID, uiDeactivateTime);
		}
		else
		{
			mrSurfaceSpeedPenaltyStop(pChar->pEntParent->mm.mrSurface, uiSpeedPenaltyActID, uiDeactivateTime);
			
			CombatReactivePowerDebugPrintEnt(pChar, "Cancel SpeedPenalty: %u at %u", uiSpeedPenaltyActID, uiDeactivateTime);
		}
	}

	// cancel all the queued stuff
	// schedule the animation/FX changes
	{
		EntityRef erSource = entGetRef(pChar->pEntParent);
		U32 uiActID = _GetCurrentAnimFxActID(pInfo);
		if (pDef->animFx.ppchActivateKeyword)
		{
			pmReleaseAnim(pChar->pPowersMovement, uiDeactivateTime, uiActID, __FUNCTION__);
			
			CombatReactivePowerDebugPrintEnt(pChar, "ReleaseAnim: %u at %u", uiActID, uiDeactivateTime);
		}

		if (bCancelSticky && pDef->animFx.ppchStickyStanceWords)
		{
			pmBitsStop(pChar->pPowersMovement, uiActID, 0, kPowerAnimFXType_ReactivePower, 
						erSource, uiDeactivateTime, false);
			CombatReactivePowerDebugPrintEnt(pChar, "Stop StickyStance: %u at %u", uiActID, uiDeactivateTime);
		}


		if (pDef->animFx.ppchActivateStickyFX)
		{
			pmFxStop(pChar->pPowersMovement, uiActID, 0, kPowerAnimFXType_ReactivePower, 
					erSource, erSource, uiDeactivateTime, NULL);
			
			CombatReactivePowerDebugPrintEnt(pChar, "Stop StickyFX: %u at %u", uiActID, uiDeactivateTime);
		}

	}

	if (pDef->bEnableStrafing)
	{
		mrSurfaceSetStrafingOverride(pChar->pEntParent->mm.mrSurface, false, uiDeactivateTime);
	}

	if (pDef->bDisablesJump)
	{
		mrSurfaceDisableJump(pChar->pEntParent->mm.mrSurface, false, uiDeactivateTime);
	}
#endif
}

// --------------------------------------------------------------------------------------------------------------------
static void _CancelPreactivateSpeedAndStickyAnimFx(	Character *pChar, 
													CombatReactivePowerInfo *pInfo, 
													CombatReactivePowerDef *pDef, 
													U32 uiDeactivateTime)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	// schedule the speed stop
	if (pDef->fPreactivateSpeedScale >= 0.f)
	{
		U32 uiSpeedPenaltyActID = _GetCurrentSlowActID(pInfo, pDef, true);

		if (pDef->fPreactivateSpeedScale == 0.f)
		{
			pmIgnoreStop(pChar, pChar->pPowersMovement, uiSpeedPenaltyActID, 
				kPowerAnimFXType_ReactivePower, uiDeactivateTime);

			CombatReactivePowerDebugPrintEnt(pChar, "Cancel Preactivate Root Stop: %u at %u", uiSpeedPenaltyActID, uiDeactivateTime);
		}
		else
		{
			mrSurfaceSpeedPenaltyStop(pChar->pEntParent->mm.mrSurface, uiSpeedPenaltyActID, uiDeactivateTime);

			CombatReactivePowerDebugPrintEnt(pChar, "Cancel Preactivate SpeedPenalty: %u at %u", uiSpeedPenaltyActID, uiDeactivateTime);
		}
	}

	// cancel all the queued stuff
	// schedule the animation/FX changes
	{
		EntityRef erSource = entGetRef(pChar->pEntParent);
		U32 uiActID = _GetCurrentAnimFxActID(pInfo);
		if (pDef->animFx.ppchPreactivateAnimKeyword)
		{
			pmReleaseAnim(pChar->pPowersMovement, uiDeactivateTime, uiActID, __FUNCTION__);

			CombatReactivePowerDebugPrintEnt(pChar, "Stop Preativate Keyword ReleaseAnim: %u at %u", uiActID, uiDeactivateTime);
		}

		if (pDef->animFx.ppchStickyStanceWords)
		{
			pmBitsStop(pChar->pPowersMovement, uiActID, 0, 
						kPowerAnimFXType_ReactivePower, 
						erSource, uiDeactivateTime, false);

			CombatReactivePowerDebugPrintEnt(pChar, "Stop StickyStance: %u at %u", uiActID, uiDeactivateTime);
		}


		if (pDef->animFx.ppchPreactivateFX)
		{
			pmFxStop(pChar->pPowersMovement, uiActID, 0, kPowerAnimFXType_ReactivePower, 
				erSource, erSource, uiDeactivateTime, NULL);

			CombatReactivePowerDebugPrintEnt(pChar, "Stop PreactivateFX: %u at %u", uiActID, uiDeactivateTime);
		}

	}
#endif
}

// --------------------------------------------------------------------------------------------------------------------
// ECombatReactivePowerState_QUEUED_ACTIVATE
// --------------------------------------------------------------------------------------------------------------------

// --------------------------------------------------------------------------------------------------------------------
// called when the block is canceled during the ECombatReactivePowerState_QUEUED_ACTIVATE state
static void _State_QueuedActivate_Cancel(	Character *pChar, 
											CombatReactivePowerInfo *pInfo, 
											CombatReactivePowerDef *pDef, 
											U32 uiDeactivateTime)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	CombatReactivePowerDebugPrintEnt(pChar, "Canceling ReactiveBlock at QueuedActivate state.");

	// we haven't started preactivating, but we need to cancel the scheduled preactivate things
	_CancelPreactivateSpeedAndStickyAnimFx(pChar, pInfo, pDef, uiDeactivateTime);

#if GAMECLIENT
	gclCombatReactivePower_State_QueuedActivate_Cancel(pChar, pInfo, pDef);
#endif

	pInfo->uiQueuedTime = 0;
	pInfo->eState = ECombatReactivePowerState_NONE;
#endif
}

// --------------------------------------------------------------------------------------------------------------------
// ECombatReactivePowerState_PREACTIVATE
// --------------------------------------------------------------------------------------------------------------------

// --------------------------------------------------------------------------------------------------------------------
// called to enter the preactivate state
static void _State_Preactivate_Enter(Character *pChar, CombatReactivePowerInfo *pInfo, CombatReactivePowerDef *pDef, U32 uiActivateTime)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	// initialize the ECombatReactivePowerState_PREACTIVATE state

	CombatReactivePowerDebugPrintEnt(pChar, "Entering Preactivate State");

	pInfo->eState = ECombatReactivePowerState_PREACTIVATE;
	pInfo->fTimer += pDef->fPreactivateTime + pChar->fTimeSlept;
	pInfo->uiStateTransitionTime = pmTimestampFrom(uiActivateTime, pDef->fPreactivateTime);

	// wake up the combatTick before to start the power activation
	character_SetSleep(pChar, pDef->fPreactivateTime - gConf.combatUpdateTimer);
	
	_HandleInitialActivationAttribCost(pChar, pDef);

	// schedule the speed penalties for the activate stage if they are different than the preactivate ones
	if (pDef->fActivateSpeedScale >= 0.f && 
		pDef->fPreactivateSpeedScale != pDef->fActivateSpeedScale)
	{
		U32 uiSpeedPenaltyActID = _GetCurrentSlowActID(pInfo, pDef, false);

		if (pDef->fActivateSpeedScale == 0.f)
		{	// root the player
			pmIgnoreStart(pChar, pChar->pPowersMovement, uiSpeedPenaltyActID, 
							kPowerAnimFXType_ReactivePower, pInfo->uiStateTransitionTime, NULL);

			CombatReactivePowerDebugPrintEnt(pChar, "Activate Root Start: %u at %u", uiSpeedPenaltyActID, pInfo->uiStateTransitionTime);
		}
		else
		{
			
			mrSurfaceSpeedScaleStart(	pChar->pEntParent->mm.mrSurface,
										uiSpeedPenaltyActID,
										pDef->fActivateSpeedScale,
										pInfo->uiStateTransitionTime);

			CombatReactivePowerDebugPrintEnt(pChar, "Activate Slow Start: %u at %u", uiSpeedPenaltyActID, pInfo->uiStateTransitionTime);
		}
	}

	// schedule the activate anim/FX
	{
		U32 uiActID = _GetCurrentAnimFxActID(pInfo);
		EntityRef erSource = entGetRef(pChar->pEntParent);

		if (pDef->animFx.ppchActivateStickyFX)
		{
			pmFxStart(	pChar->pPowersMovement,
						uiActID, 0, kPowerAnimFXType_ReactivePower, 
						erSource, erSource, pInfo->uiStateTransitionTime,
						pDef->animFx.ppchActivateStickyFX, 
						NULL, 0.f, 0.f, 0.f, 0.f, NULL, NULL, NULL, 0, 0);

			CombatReactivePowerDebugPrintEnt(pChar, "Activate Sticky FX: %u at %u", uiActID, pInfo->uiStateTransitionTime);
		}

		if (pDef->animFx.ppchActivateKeyword)
		{
			pmBitsStartFlash(	pChar->pPowersMovement, uiActID, 0,
								kPowerAnimFXType_ReactivePowerPre, erSource,
								pInfo->uiStateTransitionTime, pDef->animFx.ppchActivateKeyword, 
								false, false, false, true, false, false, true, false);

			CombatReactivePowerDebugPrintEnt(pChar, "Activate Flash Keyword: %u at %u", uiActID, pInfo->uiStateTransitionTime);
		}
	}

	// schedule roll 
	if (pDef->pRoll)
	{	// we should already have the roll direction 
		F32 fRollSeconds = 0.f;
		mrTacticalPerformRoll(pChar->pEntParent->mm.mrTactical, pInfo->fActivateYaw, pInfo->uiStateTransitionTime);

		CombatReactivePowerDebugPrintEnt(pChar, "Starting roll: %u", pInfo->uiStateTransitionTime);

		// calculate the time we will be in roll
		if (pDef->pRoll->fRollSpeed)
		{
			fRollSeconds =	pDef->pRoll->fRollDistance/pDef->pRoll->fRollSpeed + 
							pDef->pRoll->iRollAccelNumberOfFrames / MM_STEPS_PER_SECOND + 
							pDef->pRoll->iRollDecelNumberOfFrames / MM_STEPS_PER_SECOND +
							pDef->pRoll->iRollFrameStart/MM_STEPS_PER_SECOND + pDef->pRoll->fRollPostHoldSeconds + 0.1f;

		}

		pInfo->uiQueuedDeactivateTime = pmTimestampFrom(pInfo->uiStateTransitionTime, fRollSeconds);
	}

	_UpdateCombatVisualsTime(pChar, pDef);

#if GAMECLIENT
	gclCombatReactivePower_State_Preactivate_Enter(pChar, pInfo, pDef);
#endif 

#endif
}

// --------------------------------------------------------------------------------------------------------------------
// called when the block is canceled during the preactivate state
static void _State_Preactivate_Cancel(Character *pChar, 
									  CombatReactivePowerInfo *pInfo, 
									  CombatReactivePowerDef *pDef, 
									  U32 uiCancelTime,
									  bool bImmediate)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	
	CombatReactivePowerDebugPrintEnt(pChar, "\nCancel at Preactive %.2f\n", pInfo->fTimer);

	if (bImmediate || pInfo->fTimer > gConf.combatUpdateTimer)
	{
		bool bCancelSpeeds;	

		_CancelPreactivateSpeedAndStickyAnimFx(pChar, pInfo, pDef, uiCancelTime);
		bCancelSpeeds = pDef->fPreactivateSpeedScale && 
						pDef->fPreactivateSpeedScale != pDef->fActivateSpeedScale;

		_CancelActiveSpeedAndStickyAnimFx(pChar, pInfo, pDef, uiCancelTime, false, bCancelSpeeds);

		pInfo->uiQueuedTime = 0;
		pInfo->fTimer = 0.f;
		pInfo->eState = ECombatReactivePowerState_NONE;
		pInfo->fCooldown = pDef->fCooldown;
	}
	else
	{
		pInfo->uiQueuedTime = uiCancelTime;
		pInfo->fTimer = 0.f;
		pInfo->eState = ECombatReactivePowerState_QUEUE_DEACTIVATE;
	}

	if (pInfo->bAppliedPower)
	{
		pInfo->bAppliedPower = false;
		_CancelPower(pChar, pInfo, pDef);
	}

#if GAMECLIENT
	gclCombatReactivePower_State_Preactivate_Cancel(pChar, pInfo, pDef);
#endif 

#endif
}

// --------------------------------------------------------------------------------------------------------------------
// ECombatReactivePowerState_ACTIVATED
// --------------------------------------------------------------------------------------------------------------------

// --------------------------------------------------------------------------------------------------------------------
static void _Activated_Cancel(Character *pChar, 
							  CombatReactivePowerInfo *pInfo, 
							  CombatReactivePowerDef *pDef, 
							  U32 uiStartTime, 
							  bool bImmediate)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	CombatReactivePowerDebugPrintEnt(pChar, "Entering Queued Deactivate State");

	if (!bImmediate)
	{
		pInfo->uiQueuedTime = uiStartTime;
		pInfo->fTimer = 0.f;
		pInfo->eState = ECombatReactivePowerState_QUEUE_DEACTIVATE;
		character_Wake(pChar);
	}
	else
	{	
		// cancel immediately 
		_CancelActiveSpeedAndStickyAnimFx(pChar, pInfo, pDef, uiStartTime, true, true);

		if (pInfo->bAppliedPower)
		{
			_CancelPower(pChar, pInfo, pDef);
			pInfo->bAppliedPower = false;
		}

		pInfo->uiQueuedTime = 0;
		pInfo->fTimer = 0.f;
		pInfo->eState = ECombatReactivePowerState_NONE;
		pInfo->fCooldown = pDef->fCooldown;

		/// ROLL - cancel it?
	}

#if GAMECLIENT
	gclCombatReactivePower_State_Activated_Cancel(pChar, pInfo, pDef);
#endif

#endif
}

bool CombatReactivePower_IsMoving(Character *pChar)
{
	Vec3 vVelocity;
	entCopyVelocityFG(pChar->pEntParent, vVelocity);
	return (lengthVec3XZ(vVelocity) > SQR(1.f));
}

// --------------------------------------------------------------------------------------------------------------------
static void _HandleMovementAttribCost(Character *pChar, CombatReactivePowerDef *pDef)
{
#if GAMESERVER || GAMECLIENT
	if (pDef->pExprMovementAttribCost && pDef->eCostAttrib)
	{
		F32 *pfAttrib = F32PTR_OF_ATTRIB(pChar->pattrBasic, pDef->eCostAttrib);
		
		if (CombatReactivePower_IsMoving(pChar))
		{
			F32 fCost;
			combateval_ContextReset(kCombatEvalContext_Activate);
			combateval_ContextSetupActivate(pChar, NULL, NULL, kCombatEvalPrediction_None);
			fCost = combateval_EvalNew(	entGetPartitionIdx(pChar->pEntParent), 
										pDef->pExprMovementAttribCost, 
										kCombatEvalContext_Activate, NULL);
			if (fCost > 0)
			{
				(*pfAttrib) -= fCost;
				if ((*pfAttrib) < 0.f)
					(*pfAttrib) = 0.f;
				entity_SetDirtyBit(pChar->pEntParent, parse_CharacterAttribs, pChar->pattrBasic, 0);
				entity_SetDirtyBit(pChar->pEntParent, parse_Character, pChar, 0);
			}
		}
		character_Wake(pChar);
	}
#endif
}

// --------------------------------------------------------------------------------------------------------------------
static void _HandleInitialActivationAttribCost(Character *pChar, CombatReactivePowerDef *pDef)
{
#if GAMESERVER || GAMECLIENT
	if (pDef->fInitialActivationAttribCost > 0 && pDef->eCostAttrib)
	{
		F32 *pfAttrib = F32PTR_OF_ATTRIB(pChar->pattrBasic, pDef->eCostAttrib);

		(*pfAttrib) -= pDef->fInitialActivationAttribCost;
		if ((*pfAttrib) < 0.f)
			(*pfAttrib) = 0.f;

		entity_SetDirtyBit(pChar->pEntParent, parse_CharacterAttribs, pChar->pattrBasic, 0);
		entity_SetDirtyBit(pChar->pEntParent, parse_Character, pChar, 0);
	}
#endif
}

// --------------------------------------------------------------------------------------------------------------------
// powers that are flagged with bPowerActivationDeactivates, do not cancel the reactive power
// if the power does not lock you down at all
static bool _ShouldCancelReactivePowerDueToPowerActivation(PowerActivation *pAct)
{
	PowerDef *pDef = GET_REF(pAct->hdef);
	PowerAnimFX *pFX;
	if (!pDef)
		return true;

	pFX = GET_REF(pDef->hFX);
	if (!pFX)
		return true;

	if (pFX->fSpeedPenaltyDuringActivate == 0.f && 
		pFX->fSpeedPenaltyDuringPreactivate == 0.f &&
		pFX->fSpeedPenaltyDuringCharge == 0.f)
		return false;

	return true;
}

// --------------------------------------------------------------------------------------------------------------------
static bool _ShouldCancelQueuedActivate(CombatReactivePowerInfo *pInfo, PowerActivation *pAct)
{
	return (pAct && pAct->uiTimestampQueued > pInfo->uiQueuedActivateTime);
}

// --------------------------------------------------------------------------------------------------------------------
// main tick update function
void CombatReactivePower_Update(Character *pChar, F32 fRate)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	CombatReactivePowerInfo *pInfo = pChar->pCombatReactivePowerInfo;
	CombatReactivePowerDef *pDef = NULL;
	if (!pInfo)
	{
		return;
	}

	if (pDef = GET_REF(pInfo->hCombatBlockDef))
	{
		if (pInfo->fCooldown > 0.f)
		{
			pInfo->fCooldown -= fRate;
			if (pInfo->fCooldown < 0.f)
				pInfo->fCooldown = 0.f;
		}

		if (pInfo->eState >= ECombatReactivePowerState_QUEUED_ACTIVATE && 
			pInfo->eState <= ECombatReactivePowerState_ACTIVATED)
		{
			if (!CombatReactivePower_CanActivate(pChar, pDef, NULL))
			{
				CombatReactivePower_Stop(pChar, pInfo, pDef, pmTimestamp(0), true);
				return;
			}
			else if (pDef->bPowerActivationDeactivates)
			{
				if ( (pChar->pPowActQueued && _ShouldCancelReactivePowerDueToPowerActivation(pChar->pPowActQueued)) || 
					 (pChar->pPowActCurrent && _ShouldCancelReactivePowerDueToPowerActivation(pChar->pPowActCurrent))   )
				{
					CombatReactivePower_Stop(pChar, pInfo, pDef, pmTimestamp(0), true);
					return;
				}
			}
		}

		if (pInfo->uiQueuedActivateTime && pInfo->fCooldown)
		{
			if (_ShouldCancelQueuedActivate(pInfo, pChar->pPowActQueued) || 
				_ShouldCancelQueuedActivate(pInfo, pChar->pPowActCurrent))
				pInfo->uiQueuedActivateTime = 0;
		}

	#if GAMESERVER
		gslCombatReactivePower_Update(pChar, pInfo, pDef);
	#endif

		switch (pInfo->eState)
		{
			xcase ECombatReactivePowerState_NONE:
			{
		#if GAMECLIENT
				gclCombatReactivePower_UpdateNoneState(pChar, pInfo, pDef);
		#endif

				if (pDef->eCostAttrib && pDef->eMaxCostAttrib)
				{
					F32 fAttrib = *F32PTR_OF_ATTRIB(pChar->pattrBasic, pDef->eCostAttrib);
					if (!pInfo->bLockedOutUntilMaxCost)
					{
						if (fAttrib == 0.f)
						{
							pInfo->bLockedOutUntilMaxCost = true;
						}
					}
					else 
					{
						F32 fMax = *F32PTR_OF_ATTRIB(pChar->pattrBasic, pDef->eMaxCostAttrib);
						if (fAttrib == fMax)
						{
							pInfo->bLockedOutUntilMaxCost = false;
						}
					}
				}
			}

			xcase ECombatReactivePowerState_QUEUED_ACTIVATE:
			{
				U32 uiTime = pmTimestamp(0);
				
				if (uiTime >= pInfo->uiQueuedTime)
				{
					_State_Preactivate_Enter(pChar, pInfo, pDef, pInfo->uiQueuedTime);
				}
				else
				{
					character_Wake(pChar);
				}
			}

			xcase ECombatReactivePowerState_PREACTIVATE:
			{
				U32 uiTime = pmTimestamp(0);

				pInfo->fTimer -= fRate;

				if (!pInfo->bAppliedPower && pInfo->fTimer <= gConf.combatUpdateTimer)
				{
					pInfo->bAppliedPower = true;
					_ApplyPower(pChar, pInfo, pDef);
				}

				if (uiTime >= pInfo->uiStateTransitionTime)
				{
					pInfo->eState = ECombatReactivePowerState_ACTIVATED;
					CombatReactivePowerDebugPrintEnt(pChar, "Entering Activated State");
				}

				character_Wake(pChar);
			}

			xcase ECombatReactivePowerState_ACTIVATED:
			{
				U32 uiTime = pmTimestamp(0);

#if GAMECLIENT
				gclCombatReactivePower_UpdateActivated(pChar, pInfo, pDef);
#endif

				// check if we have a movement cost and we are moving
				if (pDef->pExprMovementAttribCost && pDef->eCostAttrib)
				{
					_HandleMovementAttribCost(pChar, pDef);
				}
				
				if (pInfo->uiQueuedDeactivateTime)
				{
					if (pDef->pRoll && !entIsRolling(pChar->pEntParent))
					{
						_Activated_Cancel(pChar, pInfo, pDef, pInfo->uiQueuedDeactivateTime, false);
					}
					else if (uiTime >= pInfo->uiQueuedDeactivateTime)
					{
						_Activated_Cancel(pChar, pInfo, pDef, pInfo->uiQueuedDeactivateTime, false);
					}
					character_Wake(pChar);
				}

				_UpdateCombatVisualsTime(pChar, pDef);
			}

			xcase ECombatReactivePowerState_QUEUE_DEACTIVATE:
			{
				U32 uiTime = pmTimestamp(0);

				if (pDef->pExprMovementAttribCost && pDef->eCostAttrib)
					_HandleMovementAttribCost(pChar, pDef);

				if (uiTime >= pInfo->uiQueuedTime)
				{
					U32 uiDeactivateTime = pmTimestampFrom(pInfo->uiQueuedTime, pDef->fDeactivateTime);

					CombatReactivePowerDebugPrintEnt(pChar, "Entering Deactivating State");
					
					pInfo->eState = ECombatReactivePowerState_DEACTIVATING;
					pInfo->fTimer = pDef->fDeactivateTime;

					// wake up early to process canceling the power
					character_SetSleep(pChar, pInfo->fTimer - gConf.combatUpdateTimer);
					
					_CancelActiveSpeedAndStickyAnimFx(pChar, pInfo, pDef, uiDeactivateTime, true, true);

					_UpdateCombatVisualsTime(pChar, pDef);
				}
				else
				{
					character_Wake(pChar);
				}
			}

			xcase ECombatReactivePowerState_DEACTIVATING:
			{
				pInfo->fTimer -= fRate;
				
				if (pDef->pExprMovementAttribCost && pDef->eCostAttrib)
					_HandleMovementAttribCost(pChar, pDef);

				if (pInfo->bAppliedPower && pInfo->fTimer <= gConf.combatUpdateTimer)
				{
					pInfo->bAppliedPower = false;
					_CancelPower(pChar, pInfo, pDef);
				}

				if (pInfo->fTimer <= 0.f)
				{
					pInfo->eState = ECombatReactivePowerState_NONE;
					pInfo->fCooldown = pDef->fCooldown;
					CombatReactivePowerDebugPrintEnt(pChar, "Exiting ReactiveBlock Mode");
				}
				else
				{
					_UpdateCombatVisualsTime(pChar, pDef);
					character_Wake(pChar);
				}
			}
		}
	}
#endif
}


// --------------------------------------------------------------------------------------------------------------------
bool CombatReactivePower_Begin(	Character *pChar, 
								CombatReactivePowerInfo *pInfo, 
								CombatReactivePowerDef *pDef, 
								F32 fActivateYaw,
								U32 uiStartTime,
								F32 fTimeOffset)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else

	{	
		int iPartitionIdx = entGetPartitionIdx(pChar->pEntParent);
				
		character_ActOverflowCancelReason(iPartitionIdx, pChar, NULL, 0, kAttribType_Null, true);
		character_ActQueuedCancelReason(iPartitionIdx, pChar, NULL, 0, kAttribType_Null, true);
		if (pChar->pPowActCurrent)
		{
			U32 bCancelledCurrent = character_ActCurrentCancelReason(iPartitionIdx, pChar, true, false, true, kAttribType_Null);
			if (!bCancelledCurrent && IsClient())
			{
				return false;
			}		
		}
	}

	// set the time to when we'll start this
	// queue up the stances and FX
	pInfo->bAppliedPower = false;
	pInfo->uiQueuedDeactivateTime = 0;
	pInfo->uiQueuedActivateTime = 0;

	pInfo->fActivateYaw = fActivateYaw;

	
	CombatReactivePowerDebugPrintEnt(pChar, "Begin Reactive Block %d\n", pInfo->uCurActIdOffset);

	_UpdateCombatVisualsTime(pChar, pDef);

	if (fTimeOffset > 0.f)
	{	// if we have a time offset, start directly in the preactivate state
		//U32 uiActivateTime = pmTimestampFrom(uiStartTime, pDef->fPreactivateTime - fTimeOffset);
		
		pInfo->fTimer = -fTimeOffset;
		pInfo->uiQueuedTime = 0;

		_State_Preactivate_Enter(pChar, pInfo, pDef, uiStartTime);
		pInfo->eState = ECombatReactivePowerState_PREACTIVATE;
	}
	else
	{
		pInfo->uiQueuedTime = uiStartTime;
		pInfo->fTimer = 0;
		pInfo->eState = ECombatReactivePowerState_QUEUED_ACTIVATE;
	}
		
#if GAMECLIENT
	if (pDef->pchCombatPowerState)
		gclCombatPowerStateSwitching_EnterStateByName(pChar, pDef->pchCombatPowerState);
#endif
	
	// wake up to process this
	character_Wake(pChar);

	// schedule the preactivation animFX
	{
		U32 uiAnimFxActID = _GetCurrentAnimFxActID(pInfo);
		EntityRef erSource = entGetRef(pChar->pEntParent);

		if (pDef->animFx.ppchStickyStanceWords)
		{
			pmBitsStartSticky(pChar->pPowersMovement, uiAnimFxActID, 0,
								kPowerAnimFXType_ReactivePower,
								erSource,
								uiStartTime, pDef->animFx.ppchStickyStanceWords,
								false, false, false);
			
			CombatReactivePowerDebugPrintEnt(pChar, "Start Preactivate Stance: %u at %u", uiAnimFxActID, uiStartTime);
		}

		if (pDef->animFx.ppchPreactivateAnimKeyword)
		{
			pmBitsStartFlash(	pChar->pPowersMovement, uiAnimFxActID, 0,
								kPowerAnimFXType_ReactivePowerPre, erSource,
								uiStartTime, pDef->animFx.ppchPreactivateAnimKeyword, 
								false, false, false, true, false, false, true, false);
			
			CombatReactivePowerDebugPrintEnt(pChar, "Start Preactivate Keyword: %u at %u", uiAnimFxActID, uiStartTime);
		}

		if (pDef->animFx.ppchPreactivateFX)
		{
			pmFxStart(pChar->pPowersMovement,
						uiAnimFxActID, 0, kPowerAnimFXType_ReactivePowerPre, 
						erSource, erSource, uiStartTime,
						pDef->animFx.ppchPreactivateFX, 
						NULL, 0.f, 0.f, 0.f, 0.f, NULL, NULL, NULL, 
						EPMFXStartFlags_FLASH, 0);
			
			CombatReactivePowerDebugPrintEnt(pChar, "Start Preactivate FX: %u at %u", uiAnimFxActID, uiStartTime);
		}
	}

	if (pDef->bEnableStrafing)
	{
		mrSurfaceSetStrafingOverride(pChar->pEntParent->mm.mrSurface, true, uiStartTime);
	}

	if (pDef->bDisablesJump)
	{
		mrSurfaceDisableJump(pChar->pEntParent->mm.mrSurface, true, uiStartTime);
	}

	// schedule the preactivation speed penalties if there is one
	if (pDef->fPreactivateSpeedScale >= 0.f)
	{
		U32 uiSpeedPenaltyActID = _GetCurrentSlowActID(pInfo, pDef, true);

		// schedule the speed penalty to kick in when we start blocking
		if (pDef->fPreactivateSpeedScale == 0.f)
		{
			pmIgnoreStart(pChar, pChar->pPowersMovement, uiSpeedPenaltyActID, 
							kPowerAnimFXType_ReactivePower, uiStartTime, NULL);
			CombatReactivePowerDebugPrintEnt(pChar, "Start Preactivate Root: %u at %u", uiSpeedPenaltyActID, uiStartTime);

			// if the activate speed penalty is not the same as the activate, we need to schedule the stop once it goes active
			if (pDef->fActivateSpeedScale != pDef->fPreactivateSpeedScale)
			{
				U32 uiStopTime = pmTimestampFrom(uiStartTime, pDef->fPreactivateTime);
				pmIgnoreStop(pChar, pChar->pPowersMovement, uiSpeedPenaltyActID, 
							 kPowerAnimFXType_ReactivePower, uiStopTime);
				CombatReactivePowerDebugPrintEnt(pChar, "Scheduled Preactivate Root Stop: %u at %u", uiSpeedPenaltyActID, uiStopTime);
			}
		}
		else if (pDef->fPreactivateSpeedScale > 0.f)
		{
			mrSurfaceSpeedScaleStart(	pChar->pEntParent->mm.mrSurface,
										uiSpeedPenaltyActID,
										pDef->fPreactivateSpeedScale,
										uiStartTime);
			CombatReactivePowerDebugPrintEnt(pChar, "Start Preactivate Slow: %u at %u", uiSpeedPenaltyActID, uiStartTime);

			// if the activate speed penalty is not the same as the activate, we need to schedule the stop once it goes active
			if (pDef->fActivateSpeedScale != pDef->fPreactivateSpeedScale)
			{
				U32 uiStopTime = pmTimestampFrom(uiStartTime, pDef->fPreactivateTime);
				mrSurfaceSpeedPenaltyStop(pChar->pEntParent->mm.mrSurface, uiSpeedPenaltyActID, uiStopTime);
				CombatReactivePowerDebugPrintEnt(pChar, "Scheduled Preactivate Slow Stop: %u at %u", uiSpeedPenaltyActID, uiStopTime);
			}
		}
	}

	if (pDef->pRoll && pDef->pRoll->bRollFacesInRollDirection && pDef->fPreactivateTime)
	{
		U32 uiAnimFxActID = _GetCurrentAnimFxActID(pInfo);
		U32 uiTimeStop = pmTimestampFrom(uiStartTime, pDef->fPreactivateTime);
		Vec3 vFaceDirection;

		setVec3FromYaw(vFaceDirection, pInfo->fActivateYaw);

		pmFaceStart(pChar->pPowersMovement, uiAnimFxActID, uiStartTime, uiTimeStop, 0, vFaceDirection, false, true);
	}

#endif
	return true;
}

// --------------------------------------------------------------------------------------------------------------------
void CombatReactivePower_Stop(	Character *pChar, 
									CombatReactivePowerInfo *pInfo, 
									CombatReactivePowerDef *pDef, 
									U32 uiStartTime, bool bImmediate)
{
	if (bImmediate || pInfo->eState == ECombatReactivePowerState_ACTIVATED)
	{
		switch (pInfo->eState)
		{
			xcase ECombatReactivePowerState_QUEUED_ACTIVATE:
				_State_QueuedActivate_Cancel(pChar, pInfo, pDef, uiStartTime);
			xcase ECombatReactivePowerState_PREACTIVATE:
				_State_Preactivate_Cancel(pChar, pInfo, pDef, uiStartTime, bImmediate);
			xcase ECombatReactivePowerState_ACTIVATED:
				_Activated_Cancel(pChar, pInfo, pDef, uiStartTime, bImmediate);
			xcase ECombatReactivePowerState_QUEUE_DEACTIVATE:
			case ECombatReactivePowerState_DEACTIVATING:
				if (bImmediate)
				{
					_Activated_Cancel(pChar, pInfo, pDef, uiStartTime, bImmediate);
				}
		}
	}
	else if (	pInfo->eState == ECombatReactivePowerState_QUEUED_ACTIVATE ||
				pInfo->eState == ECombatReactivePowerState_PREACTIVATE)
	{
		pInfo->uiQueuedDeactivateTime = uiStartTime;
		character_Wake(pChar);
	}

#if GAMECLIENT
	gclCombatReactivePower_OnStopBlock(pChar, pInfo, pDef);
#endif
}

// --------------------------------------------------------------------------------------------------------------------
S32 CombatReactivePower_ShouldPlayHitFx(Character *pChar)
{
	return (!pChar->pCombatReactivePowerInfo || 
		!pChar->pCombatReactivePowerInfo->bIgnoreHitFxDuringActivation ||
		pChar->pCombatReactivePowerInfo->eState != ECombatReactivePowerState_ACTIVATED);
}

static S32 CombatReactivePower_IsStateSwitchedPower(Character *pChar, Power *pPow)
{
#if GAMECLIENT || GAMESERVER
	if ( (pPow->pCombatPowerStateParent != NULL || eaSize(&pPow->ppSubCombatStatePowers) > 0)  
			||
			(pPow->pParentPower &&  (pPow->pParentPower->pCombatPowerStateParent != NULL || eaSize(&pPow->pParentPower->ppSubCombatStatePowers) > 0)))
	{
		return true;
	}
	else if (pChar->pCombatPowerStateInfo)
	{
		CombatPowerStateSwitchingDef *pStateSwitchDef = GET_REF(pChar->pCombatPowerStateInfo->hCombatPowerStateSwitchingDef);
		S32 iSlot = character_PowerIDSlot(pChar, pPow->uiID);
		
		if (pStateSwitchDef && iSlot >= 0)
		{
			FOR_EACH_IN_EARRAY(pStateSwitchDef->eaPowerSlotSet, CombatPowerStatePowerslotSet, pSet)
			{
				FOR_EACH_IN_EARRAY(pSet->eaPowerSlots, CombatPowerStatePowerSlot, pSlot)
				{
					if (pSlot->iPowerSlot == iSlot)
						return true;
				}
				FOR_EACH_END
			}
			FOR_EACH_END
		}
	}
#endif
	return false;
}

// --------------------------------------------------------------------------------------------------------------------
S32 CombatReactivePower_CanActivatePowerDef(Character *pChar, Power *pPow)
{
	if (pChar->pCombatReactivePowerInfo && 
		( (pChar->pCombatReactivePowerInfo->uiQueuedActivateTime != 0) ||
		  (pChar->pCombatReactivePowerInfo->uiQueuedDeactivateTime == 0 && 
				pChar->pCombatReactivePowerInfo->eState >= ECombatReactivePowerState_QUEUED_ACTIVATE
		 		 && pChar->pCombatReactivePowerInfo->eState < ECombatReactivePowerState_QUEUE_DEACTIVATE)))
	{
		CombatReactivePowerDef *pDef = NULL;
		
		if (pDef = GET_REF(pChar->pCombatReactivePowerInfo->hCombatBlockDef))
		{	
			// if this is a roll, allow power activation if we aren't in an activation state
			if (pDef->pRoll && !pChar->pCombatReactivePowerInfo->eState)
				return true;

			if (pChar->pCombatReactivePowerInfo->uiQueuedActivateTime && pChar->pCombatReactivePowerInfo->fCooldown)
				return true;

			if (pDef->bDisallowPowerActivation)
				return false;
			if (pDef->bPowerActivationDeactivates)
				return true;

			if (pDef->pchCombatPowerState)
			{

				// only a trivial case right now, if we have a CombatPowerState defined, only allow powers that 
				// come from this mode
				return CombatReactivePower_IsStateSwitchedPower(pChar, pPow);
			}
		}
	}

	return true;
}


#include "CombatReactivePower_h_ast.c"

