#include "CombatPowerStateSwitching.h"
#include "ResourceManager.h"
#include "Character.h"
#include "CombatEval.h"
#include "Entity.h"
#include "PowerAnimFX.h"
#include "MemoryPool.h"
#include "StringCache.h"
#include "GameAccountDataCommon.h"
#include "EntityIterator.h"
#include "file.h"
#include "PowerSlots.h"

#include "CombatPowerStateSwitching_h_ast.h"

#if GAMECLIENT || GAMESERVER
	#include "Character_combat.h"
	#include "PowersMovement.h"
	#include "CharacterAttribsMinimal_h_ast.h"
#endif
#if GAMECLIENT
	#include "gclCombatPowerStateSwitching.h"
	#include "gclCursorModePowerTargeting.h"
	#include "gclCursorModePowerLocationTargeting.h"
	#include "gclCursorMode.h"
#endif
#if GAMESERVER
	#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#endif

DictionaryHandle g_hCombatPowerStateSwitchingPowerDefDict;

#define POWER_MODE_LINKING_ACTID				0x4000FEE5

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static void CombatPowerStateSwitching_Generate(CombatPowerStateSwitchingDef *pDef);

// -------------------------------------------------------------------------------------------------------------------
static void _RebuildPowerArraysForAffectedEnts(CombatPowerStateSwitchingDef *pDef)
{
	Entity *ent;
	EntityIterator *iter;

	if (!pDef)
		return;

	iter = entGetIteratorAllTypesAllPartitions(ENTITYFLAG_IS_PLAYER, 0);
	while(ent = EntityIteratorGetNext(iter))
	{
		if(ent->pChar && ent->pChar->pCombatPowerStateInfo &&
			GET_REF(ent->pChar->pCombatPowerStateInfo->hCombatPowerStateSwitchingDef) == pDef)
		{
			ent->pChar->bResetPowersArray = true;
		}
	}
	EntityIteratorRelease(iter);
}

// -------------------------------------------------------------------------------------------------------------------
AUTO_FIXUPFUNC;
TextParserResult fixupCombatPowerStateSwitchingDef(CombatPowerStateSwitchingDef* pDef, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		xcase FIXUPTYPE_POST_RELOAD:
		{
			if (IsServer() && isDevelopmentMode())
			{
				_RebuildPowerArraysForAffectedEnts(pDef);
			}
		}
	}

	return 1;
}

// -------------------------------------------------------------------------------------------------------------------
static int CombatPowerStateSwitchingDefValidateCB(	enumResourceValidateType eType, 
													const char *pDictName, 
													const char *pResourceName, 
													CombatPowerStateSwitchingDef *pDef, 
													U32 userID)
{
	switch (eType)
	{
		case RESVALIDATE_POST_TEXT_READING:
		{
			CombatPowerStateSwitching_Generate(pDef);

			if (IsClient())
				return VALIDATE_NOT_HANDLED;


			// validate that there is at least one CombatPowerStateDef
			if (pDef->iSpecialPowerSwitchedSlot == -1 && eaSize(&pDef->eaStates) == 0)
			{
				ErrorFilenamef(pDef->pchFilename, "No Modes defined.");
				return VALIDATE_HANDLED;
			}

			// validate there isn't a duplicate CombatPowerStateDef
			FOR_EACH_IN_EARRAY(pDef->eaStates, CombatPowerStateDef, pMode)
			{
				if (!pMode->pchName || !pMode->pchName[0])
				{
					ErrorFilenamef(pDef->pchFilename, "Missing Mode Name.");
					continue;
				}

				FOR_EACH_IN_EARRAY(pDef->eaStates, CombatPowerStateDef, pModeCompare)
				{
					if (pModeCompare != pMode && pModeCompare->pchName == pMode->pchName)
					{
						ErrorFilenamef(pDef->pchFilename, "Duplicate Mode names (%s). This is not allowed.", pMode->pchName);
					}
				}	
				FOR_EACH_END
			}
			FOR_EACH_END

			// go through all the power sets 
			FOR_EACH_IN_EARRAY(pDef->eaPowerSet, CombatPowerStatePowerSet, pPowerSet)
			{
				if (!GET_REF(pPowerSet->hBasePowerDef))
				{
					ErrorFilenamef(pDef->pchFilename, "No BasePowerDef defined in one of the sets.");
					continue;
				}

				// validate that there isn't a duplicate hBasePowerDef
				FOR_EACH_IN_EARRAY(pDef->eaPowerSet, CombatPowerStatePowerSet, pPowerSetCompare)
				{
					if (pPowerSetCompare != pPowerSet && 
						GET_REF(pPowerSetCompare->hBasePowerDef) == GET_REF(pPowerSet->hBasePowerDef))
					{
						ErrorFilenamef(pDef->pchFilename, "Duplicate PowerSet BasePowers (%s). This is not allowed.", 
							REF_HANDLE_GET_STRING(pPowerSetCompare->hBasePowerDef));
					}
				}
				FOR_EACH_END

				// validate that there is a mode defined for each power
				FOR_EACH_IN_EARRAY(pPowerSet->eaPowers, CombatPowerStatePower, pPowerLink)
				{
					if (!GET_REF(pPowerLink->hPowerDef))
					{
						ErrorFilenamef(pDef->pchFilename, "No Power defined for the PowerSet of (%s)", 
										REF_HANDLE_GET_STRING(pPowerSet->hBasePowerDef));
						continue;
					}

					if (pDef->iSpecialPowerSwitchedSlot >= 0)
						continue;

					if (!pPowerLink->pchState || !pPowerLink->pchState[0])
					{
						ErrorFilenamef(pDef->pchFilename, "No Mode defined for a power in set of (%s)", 
										REF_HANDLE_GET_STRING(pPowerSet->hBasePowerDef));
					}
					else if (!CombatPowerStateSwitching_GetStateByName(pPowerLink->pchState, pDef))
					{
						ErrorFilenamef(pDef->pchFilename, "Mode not found for set of (%s)", 
										REF_HANDLE_GET_STRING(pPowerSet->hBasePowerDef));
					}
				}
				FOR_EACH_END
			}
			FOR_EACH_END
		} 
		return VALIDATE_HANDLED;
	}

	return VALIDATE_NOT_HANDLED;
}

// ------------------------------------------------------------------------------------------------------------------------------
AUTO_RUN;
int CombatPowerStateSwitching_AutoRunInit(void)
{
	// Don't load on app servers, other than specific servers
	if (IsAppServerBasedType()) 
		return 0;

	g_hCombatPowerStateSwitchingPowerDefDict = RefSystem_RegisterSelfDefiningDictionary("CombatPowerStateSwitchingDef",
																				false, 
																				parse_CombatPowerStateSwitchingDef, 
																				true, 
																				true, 
																				NULL);

	resDictManageValidation(g_hCombatPowerStateSwitchingPowerDefDict, CombatPowerStateSwitchingDefValidateCB);
	resDictSetDisplayName(g_hCombatPowerStateSwitchingPowerDefDict, "CombatPowerStateSwitchingDef", "CombatPowerStateSwitchingDefs", RESCATEGORY_DESIGN);

	resDictMaintainInfoIndex(g_hCombatPowerStateSwitchingPowerDefDict, ".Name", NULL, NULL, NULL, NULL);

	return 1;
}

// -------------------------------------------------------------------------------------------------------------------
static void CombatPowerStateSwitching_Generate(CombatPowerStateSwitchingDef *pDef)
{
	FOR_EACH_IN_EARRAY(pDef->eaStates, CombatPowerStateDef, pState)
	{
		if (pState->pExprAttribDecayPerTick)
		{
			combateval_Generate(pState->pExprAttribDecayPerTick, kCombatEvalContext_Activate);
		}
		if (pState->pExprPerTickExitMode)
		{
			combateval_Generate(pState->pExprPerTickExitMode, kCombatEvalContext_Activate);
		}
		
	}
	FOR_EACH_END
}

// -------------------------------------------------------------------------------------------------------------------
AUTO_STARTUP(AS_CombatPowerStateSwitching) ASTRT_DEPS(Powers);
void CombatPowerStateSwitching_LoadDefs(void)
{
	// Don't load on app servers, other than specific servers
	if (IsAppServerBasedType()) 
		return;

	resLoadResourcesFromDisk(g_hCombatPowerStateSwitchingPowerDefDict, "defs/powers", 
								".powerstates", "CombatPowerStateSwitching.bin",  
								PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY);
}


// -------------------------------------------------------------------------------------------------------------------
static U32 _GetCurrentAnimFxActID(CombatPowerStateSwitchingInfo *pState)
{
	return POWER_MODE_LINKING_ACTID + pState->uCurActIdOffset;
}

// --------------------------------------------------------------------------------------------------------------------
void CombatPowerStateSwitching_InitCharacter(Character *pChar, const char *pszDef)
{
	if (pszDef && !pChar->pCombatPowerStateInfo)
	{
		CombatPowerStateSwitchingInfo *pState = StructAlloc(parse_CombatPowerStateSwitchingInfo);
		CombatPowerStateSwitchingDef *pDef;
		REF_HANDLE_SET_FROM_STRING(g_hCombatPowerStateSwitchingPowerDefDict, pszDef, pState->hCombatPowerStateSwitchingDef);
		pDef = GET_REF(pState->hCombatPowerStateSwitchingDef);
		if (pDef)
		{
			pChar->pCombatPowerStateInfo = pState;
			pState->iSpecialPowerSwitchedSlot = pDef->iSpecialPowerSwitchedSlot;
		}
		else
		{
			Errorf("CombatPowerStateSwitching_InitCharacter: Could not find def %s.", pszDef);
			StructDestroy(parse_CombatPowerStateSwitchingInfo, pState);
		}
	}
}

// -------------------------------------------------------------------------------------------------------------------
S32 CombatPowerStateSwitching_CanStateSwitch(Character *pChar, CombatPowerStateSwitchingDef *pDef,
											 ECombatPowerStateModeFailReason *peFailOut)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else	
	if (pDef->iCombatLevelLockout && pDef->iCombatLevelLockout > pChar->iLevelCombat)
		return false;

	if (pDef->bDisableActivationWhileKnocked)
	{
		if (pmKnockIsActive(pChar->pEntParent))
		{
			if (peFailOut) 
				*peFailOut = ECombatPowerStateModeFailReason_KNOCKED;
			return false;
		}
	}

	if (pDef->eaiDisableActivationAttribs)
	{
		S32 i;
		for (i = eaiSize(&pDef->eaiDisableActivationAttribs) - 1; i >= 0; --i)
		{
			S32 attrib = pDef->eaiDisableActivationAttribs[i];
			if (attrib >= 0 && IS_NORMAL_ATTRIB(attrib))
			{
				F32 fAttrib = *F32PTR_OF_ATTRIB(pChar->pattrBasic, attrib);
				if (fAttrib > 0)
				{
					if (peFailOut) 
						*peFailOut = ECombatPowerStateModeFailReason_DISABLEDATTRIB;
					return false;
				}
			}
		}
	}
#endif
	return true;
}

// -------------------------------------------------------------------------------------------------------------------
S32 CombatPowerStateSwitching_CanEnterState(Character *pChar, CombatPowerStateDef *pMode, ECombatPowerStateModeFailReason *peReason)
{
	if (pChar->pNearDeath || !entIsAlive(pChar->pEntParent))
		return false;
		
	if (pMode->eRequiredAttrib)
	{
		F32 fAttrib = *F32PTR_OF_ATTRIB(pChar->pattrBasic, pMode->eRequiredAttrib);
		if (fAttrib < pMode->fRequiredAttribAmountToEnter)
		{
			if (peReason) 
				*peReason = ECombatPowerStateModeFailReason_COST;
			return false;
		}
	}

	if (pMode->iDisallowedPowerMode >= 0)
	{
		if (character_HasMode(pChar, pMode->iDisallowedPowerMode))
		{
			if (peReason) 
				*peReason = ECombatPowerStateModeFailReason_DISALLOWEDMODE;
			return false;
		}
	}
	
	if (peReason) 
		*peReason = ECombatPowerStateModeFailReason_NONE;
	
	return true;
}


// -------------------------------------------------------------------------------------------------------------------
CombatPowerStateDef* CombatPowerStateSwitching_GetStateByName(const char *pchState, CombatPowerStateSwitchingDef *pDef)
{
	if (pchState)
	{
		FOR_EACH_IN_EARRAY(pDef->eaStates, CombatPowerStateDef, pMode)
		{
			if (pMode->pchName == pchState)
				return pMode;
		}
		FOR_EACH_END
	}
	
	return NULL;
}

// -------------------------------------------------------------------------------------------------------------------
S32 CombatPowerStateSwitching_DoesPowerMatchMode(Power *pPower, const char *pchModeName)
{
	return pPower && pPower->pchCombatPowersState && pPower->pchCombatPowersState == pchModeName;
}

// -------------------------------------------------------------------------------------------------------------------
S32 CombatPowerStateSwitching_IsPowerIDSlottedInSet(	Character *pChar, 
														CombatPowerStateSwitchingDef *pDef, 
														U32 powerID, 
														const char *pchModeName)
{
	S32 iSlot = character_PowerIDSlot(pChar, powerID);

	if (iSlot >= 0)
	{
		FOR_EACH_IN_EARRAY(pDef->eaPowerSlotSet, CombatPowerStatePowerslotSet, pSet)
		{
			if (pSet->iBasePowerSlot == iSlot)
			{
				return true;
			}

			FOR_EACH_IN_EARRAY(pSet->eaPowerSlots, CombatPowerStatePowerSlot, pSlot)
			{
				if (pSlot->pchState == pchModeName && pSlot->iPowerSlot == iSlot)
				{
					return true;
				}
			}
			FOR_EACH_END
		}
		FOR_EACH_END
	}

	return false;
}

// -------------------------------------------------------------------------------------------------------------------
S32 CombatPowerStateSwitching_IsPowerSlottedInSet(	Character *pChar, 
													CombatPowerStateSwitchingDef *pDef, 
													Power *pPower, 
													const char *pchModeName)
{
	if (pPower)
		CombatPowerStateSwitching_IsPowerIDSlottedInSet(pChar, pDef, pPower->uiID, pchModeName);

	return false;
}

// -------------------------------------------------------------------------------------------------------------------
// pchModeName assumed to be a pooled string
static S32 _ShouldCancelActivationDueToStateExit(	Character *pChar, 
													CombatPowerStateSwitchingDef *pDef,
													PowerActivation *pAct, 
													const char *pchModeName)
{
	Power *pPower = character_ActGetPower(pChar, pAct);
	if (pPower && CombatPowerStateSwitching_DoesPowerMatchMode(pPower, pchModeName))
	{
		return true;
	}

	if (pDef->eaPowerSlotSet)
	{
		pPower = character_FindPowerByID(pChar, pAct->ref.uiID);
		return CombatPowerStateSwitching_IsPowerSlottedInSet(pChar, pDef, pPower, pchModeName);
	}

	return false;
}



// -------------------------------------------------------------------------------------------------------------------
static S32 _DoesPowerContainSwitchState(Power *pPower, const char *pchModeName)
{
	if (pPower->ppSubCombatStatePowers)
	{
		FOR_EACH_IN_EARRAY(pPower->ppSubCombatStatePowers, Power, pLinkedPower)
		{
			if (pLinkedPower->pchCombatPowersState == pchModeName)
				return true;
		}
		FOR_EACH_END
	}

	return false;
}

// -------------------------------------------------------------------------------------------------------------------
// pchModeName assumed to be a pooled string
static S32 _ShouldCancelActivationDueToStateEnter(Character *pChar, 
												PowerActivation *pAct, 
												const char *pchModeName)
{
	Power *pPower = character_ActGetPower(pChar, pAct);
	if (pPower)
	{
		return _DoesPowerContainSwitchState(pPower, pchModeName);
	}

	return false;
}

#if GAMECLIENT
// -------------------------------------------------------------------------------------------------------------------
static S32 _ShouldCancelCursorPowerTargetingDueToStateEnter(Character *pChar, const char *pchModeName)
{
	Power *pPower = gclCursorPowerTargeting_GetCurrentPower();
	if (pPower)
	{
		return _DoesPowerContainSwitchState(pPower, pchModeName);
	}

	pPower = gclCursorPowerLocationTargeting_GetCurrentPower();
	if (pPower)
	{
		return _DoesPowerContainSwitchState(pPower, pchModeName);
	}

	return false;
}
#endif


// --------------------------------------------------------------------------------------------------------------------
// utility function to activate the power
static void _ApplyPower(Character *pChar, CombatPowerStateDef *pModeDef)
{
#if GAMESERVER
	PowerDef *pPowerDef = GET_REF(pModeDef->hApplyPowerDef);
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
		character_ApplyUnownedPowerDef(entGetPartitionIdx(pChar->pEntParent), pChar, pPowerDef, &applyParams);
	}
#endif
}

// -------------------------------------------------------------------------------------------------------------------
// attempts to cancel all attribMods on the character that were applied from the hActivatePowerDef
static void _CancelPower(Character *pChar, CombatPowerStateDef *pModeDef)
{
#if GAMESERVER
	PowerDef *pPowerDef = GET_REF(pModeDef->hApplyPowerDef);
	if(pPowerDef)
	{
		character_CancelModsFromDef(pChar, pPowerDef, entGetRef(pChar->pEntParent), 0, false);
	}
#endif
}



// -------------------------------------------------------------------------------------------------------------------
void CombatPowerStateSwitching_ExitState(	Character *pChar, 
											CombatPowerStateSwitchingDef *pDef, 
											CombatPowerStateSwitchingInfo *pState, 
											CombatPowerStateDef *pModeDef, 
											U32 uiEndTime)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else

	{	
		int iPartitionIdx = entGetPartitionIdx(pChar->pEntParent);
		const char *pchModeName = pModeDef ? pModeDef->pchName : NULL;

		if (pChar->pPowActOverflow && _ShouldCancelActivationDueToStateExit(pChar, pDef, pChar->pPowActOverflow, pchModeName))
		{
			character_ActOverflowCancelReason(iPartitionIdx, pChar, NULL, 0, kAttribType_Null, true);
		}

		if (pChar->pPowActQueued && _ShouldCancelActivationDueToStateExit(pChar, pDef, pChar->pPowActQueued, pchModeName)
			&& pChar->pPowActCurrent)
		{
			character_ActQueuedCancelReason(iPartitionIdx, pChar, NULL, 0, kAttribType_Null, true);
		}

		if (pChar->eChargeMode == kChargeMode_CurrentMaintain && pChar->pPowActCurrent && 
			_ShouldCancelActivationDueToStateExit(pChar, pDef, pChar->pPowActCurrent, pchModeName))
		{
			U8 uchActId = pChar->pPowActCurrent->uchID;
			ChargeMode eMode = pChar->eChargeMode;
			
			character_ActDeactivate(iPartitionIdx, pChar, &uchActId, &eMode, 
									uiEndTime, uiEndTime, false);
		}

#if GAMECLIENT
		gclCombatpowerStateSwitching_OnExit(pChar, pDef, pState, pModeDef);
#endif

	}
	pState->pchLastState = pModeDef ? pModeDef->pchName : NULL;
	
	if (!pModeDef)
	{
		return; // nothing to do
	}
	
#if GAMESERVER
	_CancelPower(pChar, pModeDef);
#endif

	// schedule the animFX to be stopped
	{
		U32 uiAnimFxID = _GetCurrentAnimFxActID(pState);
		EntityRef erSource = entGetRef(pChar->pEntParent);

		if (pModeDef->ppchStickyStanceWords)
		{
			pmBitsStop(pChar->pPowersMovement, uiAnimFxID, 0, kPowerAnimFXType_CombatPowerStateSwitching, 
						erSource, uiEndTime, false);
		}

		if (pModeDef->ppchStickyFX)
		{
			pmFxStop(pChar->pPowersMovement, uiAnimFxID, 0, kPowerAnimFXType_CombatPowerStateSwitching, 
						erSource, erSource, uiEndTime, NULL);
		}
		
	
		if (pModeDef->ppchKeyword)
		{
			pmReleaseAnim(pChar->pPowersMovement, uiEndTime, uiAnimFxID, __FUNCTION__);
		}
	}

	pState->pchCurrentState = NULL;

#endif
}

// -------------------------------------------------------------------------------------------------------------------
void CombatPowerStateSwitching_EnterState(	Character *pChar, 
											CombatPowerStateSwitchingInfo *pState, 
											CombatPowerStateDef *pModeDef, 
											U32 uiStartTime)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	pState->pchCurrentState = pModeDef ? pModeDef->pchName : NULL;

#if GAMECLIENT
	gclCombatpowerStateSwitching_OnEnter(pChar, pState);
#endif	
	if (!pModeDef)
		return; // nothing to do
	
	{	
		int iPartitionIdx = entGetPartitionIdx(pChar->pEntParent);

		if (pChar->pPowActOverflow && _ShouldCancelActivationDueToStateEnter(pChar, pChar->pPowActOverflow, pModeDef->pchName))
		{
			character_ActOverflowCancelReason(iPartitionIdx, pChar, NULL, 0, kAttribType_Null, true);
		}

		if (pChar->pPowActQueued && _ShouldCancelActivationDueToStateEnter(pChar, pChar->pPowActQueued, pModeDef->pchName))
		{
			character_ActQueuedCancelReason(iPartitionIdx, pChar, NULL, 0, kAttribType_Null, true);
		}

		if (pChar->eChargeMode == kChargeMode_CurrentMaintain && pChar->pPowActCurrent && 
			_ShouldCancelActivationDueToStateEnter(pChar, pChar->pPowActCurrent, pModeDef->pchName))
		{
			U8 uchActId = pChar->pPowActCurrent->uchID;
			ChargeMode eMode = pChar->eChargeMode;

			character_ActDeactivate(iPartitionIdx, pChar, &uchActId, &eMode, 
									uiStartTime, uiStartTime, false);
		}

#if GAMECLIENT
		if (_ShouldCancelCursorPowerTargetingDueToStateEnter(pChar, pModeDef->pchName))
		{
			gclCursorMode_ChangeToDefault();
		}
#endif
	}


#if GAMESERVER
	_ApplyPower(pChar, pModeDef);
#endif
		
	// schedule the animFX
	{
		U32 uiAnimFxID = _GetCurrentAnimFxActID(pState);
		EntityRef erSource = entGetRef(pChar->pEntParent);
		
		if (pModeDef->ppchStickyStanceWords)
		{
			pmBitsStartSticky(	pChar->pPowersMovement, uiAnimFxID, 0,
								kPowerAnimFXType_CombatPowerStateSwitching,
								erSource,
								uiStartTime, pModeDef->ppchStickyStanceWords,
								false, false, false);
		}

		if (pModeDef->ppchStickyFX)
		{
			pmFxStart(	pChar->pPowersMovement,
						uiAnimFxID, 0, kPowerAnimFXType_CombatPowerStateSwitching, 
						erSource, erSource, uiStartTime,
						pModeDef->ppchStickyFX, 
						NULL, 0.f, 0.f, 0.f, 0.f, NULL, NULL, NULL, 0, 0);
		}

		if (pModeDef->ppchKeyword)
		{
			pmBitsStartFlash(	pChar->pPowersMovement, uiAnimFxID, 0,
								kPowerAnimFXType_CombatPowerStateSwitching, erSource,
								uiStartTime, pModeDef->ppchKeyword, 
								false, false, false, true, false, false, true, false);
		}

		if (pModeDef->ppchFX)
		{
			pmFxStart(	pChar->pPowersMovement,
						uiAnimFxID, 0, kPowerAnimFXType_CombatPowerStateSwitching, 
						erSource, erSource, uiStartTime,
						pModeDef->ppchFX, 
						NULL, 0.f, 0.f, 0.f, 0.f, NULL, NULL, NULL, 
						EPMFXStartFlags_FLASH, 0);
		}

	}

#endif
}

// -------------------------------------------------------------------------------------------------------------------
void CombatPowerStateSwitching_Update(Character *pChar, F32 fRate)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else

	if (pChar->pCombatPowerStateInfo)
	{
		CombatPowerStateSwitchingDef *pDef = NULL;
		CombatPowerStateSwitchingInfo *pState = NULL;
		CombatPowerStateDef *pMode = NULL;

		pState = pChar->pCombatPowerStateInfo;
		pDef = GET_REF(pState->hCombatPowerStateSwitchingDef);
		if (!pDef)
			return;
		
		if (pState->fCooldown > 0)
		{
			pState->fCooldown -= fRate;
			entity_SetDirtyBit(pChar->pEntParent, parse_Character, pChar, false);
		}
		
		pMode = CombatPowerStateSwitching_GetStateByName(pState->pchCurrentState, pDef);
		if (pMode)
		{
			bool bExit = false;
			bool bContextSetup = false;

			if (pChar->pNearDeath || !entIsAlive(pChar->pEntParent))
				bExit = true;

			if (!bExit && pMode->eRequiredAttrib)
			{
				F32 *pfAttrib = F32PTR_OF_ATTRIB(pChar->pattrBasic, pMode->eRequiredAttrib);

				// if we have decay, handle it and make sure we at least wake up when it would be empty
				if (pMode->pExprAttribDecayPerTick != NULL)
				{
					F32 fNumTicks = fRate/gConf.combatUpdateTimer;
					F32 fTimeToExpire;
					F32 fAttribDecayPerTick = 0.f;

					bContextSetup = true;
					combateval_ContextReset(kCombatEvalContext_Activate);
					combateval_ContextSetupActivate(pChar, NULL, NULL, kCombatEvalPrediction_True);
					fAttribDecayPerTick = combateval_EvalNew(	entGetPartitionIdx(pChar->pEntParent), 
																pMode->pExprAttribDecayPerTick, 
																kCombatEvalContext_Activate, NULL);

					if (fAttribDecayPerTick)
					{
						(*pfAttrib) -= fNumTicks * fAttribDecayPerTick;
						entity_SetDirtyBit(pChar->pEntParent, parse_CharacterAttribs, pChar->pattrBasic, 0);
						entity_SetDirtyBit(pChar->pEntParent, parse_Character, pChar, 0);

						// make sure we wake up when the attrib amount would expire
						fTimeToExpire = (*pfAttrib) / (1.f / gConf.combatUpdateTimer * fAttribDecayPerTick);
						character_SetSleep(pChar, fTimeToExpire);
					}
				}

				if (*pfAttrib <= 0.f)
				{	
					bExit = true;
				}
			}

			if (!bExit && pMode->pExprPerTickExitMode)
			{
				F32 fRet;
				if (!bContextSetup)
				{
					combateval_ContextReset(kCombatEvalContext_Activate);
					combateval_ContextSetupActivate(pChar, NULL, NULL, kCombatEvalPrediction_True);
				}
				fRet = combateval_EvalNew(	entGetPartitionIdx(pChar->pEntParent), 
											pMode->pExprPerTickExitMode, 
											kCombatEvalContext_Activate, NULL);
				bExit = fRet != 0.f;
			}

			

			if (bExit)
			{
				// exit the mode
				// todo: what mode do we go into? for now just exit all modes
				CombatPowerStateSwitching_ExitState(pChar, pDef, pState, pMode, pmTimestamp(0));
#if GAMESERVER
				if (entIsPlayer(pChar->pEntParent))
				{
					ClientCmd_gclCombatPowerStateSwitching_ExitState(pChar->pEntParent, pMode->pchName);
				}
#endif
			}
			
		}
	}
#endif
}


// -------------------------------------------------------------------------------------------------------------------
static CombatPowerStatePowerSet* CombatPowerStateSwitching_GetPowerSetForPowerDef(	Character *pChar, 
																				CombatPowerStateSwitchingDef *pDef, 
																				PowerDef *pPowerDef)
{
	// loop through the def and find the power set
	// if these sets get big or are called alot, we might want to make a lookup 
	FOR_EACH_IN_EARRAY(pDef->eaPowerSet, CombatPowerStatePowerSet, pSet)
	{
		if (GET_REF(pSet->hBasePowerDef) == pPowerDef)
			return pSet;
	}
	FOR_EACH_END
		
	return NULL;
}

// -------------------------------------------------------------------------------------------------------------------
// returns corresponding powerSlot to the new state based on the old state and powerslot 
S32 CombatPowerStateSwitching_GetCorrespondingStatePowerSlot(Character *pChar, const char *pchNewState, const char *pchOldState, S32 iOldSlot)
{
	if (pChar->pCombatPowerStateInfo)
	{
		CombatPowerStateSwitchingInfo *pState = pChar->pCombatPowerStateInfo;
		CombatPowerStateSwitchingDef *pDef = GET_REF(pState->hCombatPowerStateSwitchingDef);

		if (pDef && pDef->eaPowerSlotSet)
		{
			FOR_EACH_IN_EARRAY(pDef->eaPowerSlotSet, CombatPowerStatePowerslotSet, pSet)
			{
				if (pchOldState && !pchNewState)
				{	// was in a defined state but now the default state
					// match the old state then return the basePowerSlot
					FOR_EACH_IN_EARRAY(pSet->eaPowerSlots, CombatPowerStatePowerSlot, pSlotPower)
					{
						if (pSlotPower->iPowerSlot == iOldSlot && pSlotPower->pchState == pchOldState)
							return pSet->iBasePowerSlot;
					}
					FOR_EACH_END
				}
				else if (!pchOldState && pchNewState && iOldSlot == pSet->iBasePowerSlot)
				{	// old state was default going to a new state

					FOR_EACH_IN_EARRAY(pSet->eaPowerSlots, CombatPowerStatePowerSlot, pSlotPower)
					{
						if (pSlotPower->pchState == pchNewState)
							return pSlotPower->iPowerSlot;
					}
					FOR_EACH_END
				}
			}
			FOR_EACH_END
		}
	}

	return -1;
}


// -------------------------------------------------------------------------------------------------------------------
S32 CombatPowerStateSwitching_GetSwitchedStatePowerSlot(Character *pChar, S32 iSlot)
{
	if (pChar->pCombatPowerStateInfo)
	{
		CombatPowerStateSwitchingInfo *pState = pChar->pCombatPowerStateInfo;

		if (pState->pchCurrentState)
		{
			CombatPowerStateSwitchingDef *pDef = GET_REF(pState->hCombatPowerStateSwitchingDef);
			
			if (pDef && pDef->eaPowerSlotSet)
			{
				FOR_EACH_IN_EARRAY(pDef->eaPowerSlotSet, CombatPowerStatePowerslotSet, pSet)
				{
					if (pSet->iBasePowerSlot == iSlot)
					{
						FOR_EACH_IN_EARRAY(pSet->eaPowerSlots, CombatPowerStatePowerSlot, pSlotPower)
						{
							if (pSlotPower->pchState == pState->pchCurrentState)
								return pSlotPower->iPowerSlot;
						}
						FOR_EACH_END
						return -1;
					}
				}
				FOR_EACH_END
			}
		}
	}

	return -1;
}

// -------------------------------------------------------------------------------------------------------------------
Power* CombatPowerStateSwitching_GetSwitchedStatePower(Character *pChar, Power *pPower)
{
#if !GAMESERVER && !GAMECLIENT
	assert(0);
#else
	if (pChar->pCombatPowerStateInfo && pPower)
	{
		CombatPowerStateSwitchingInfo *pState = NULL;
	
		pState = pChar->pCombatPowerStateInfo;
		
		if (pState->iSpecialPowerSwitchedSlot >= 0)
		{
			S32 iSlot = character_PowerIDSlot(pChar, pPower->uiID);
			if (iSlot != pState->iSpecialPowerSwitchedSlot)
				return NULL;
		}
		

		if ((pState->iSpecialPowerSwitchedSlot >= 0 || pState->pchCurrentState) && pPower->ppSubCombatStatePowers)
		{
			FOR_EACH_IN_EARRAY(pPower->ppSubCombatStatePowers, Power, pLinkedPower)
			{
				if (pLinkedPower->pchCombatPowersState == pState->pchCurrentState)
					return pLinkedPower;
			}
			FOR_EACH_END
		}
	}
#endif

	return NULL;
}

// -------------------------------------------------------------------------------------------------------------------
Power* CombatPowerStateSwitching_GetBasePower(Power *pPower)
{
	if (pPower->pParentPower)
		pPower = pPower->pParentPower;
	if (pPower->pCombatPowerStateParent)
		return pPower->pCombatPowerStateParent;

	if (eaSize(&pPower->ppSubCombatStatePowers) > 0)
		return pPower;
	
	return NULL;
}


// -------------------------------------------------------------------------------------------------------------------
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Powers) ACMD_SERVERCMD ACMD_PRIVATE;
void gslCombatPowerStateSwitching_ActivateServer(Entity* pEnt, const char *pchState, U32 uiActivateTime, U8 uActId)
{
	if (pEnt && pEnt->pChar)
	{
		CombatPowerStateSwitchingDef *pDef = NULL;
		CombatPowerStateSwitchingInfo *pState = NULL;

		if (!pEnt->pChar->pCombatPowerStateInfo)
		{
			return;
		}

		pState = pEnt->pChar->pCombatPowerStateInfo;
		pDef = GET_REF(pState->hCombatPowerStateSwitchingDef);
		if (!pDef)
			return;
			
		// allowing some bit of wiggle in the timing
		if ((pState->fCooldown - pEnt->pChar->fTimeSlept) >= 0.3f)
		{	
			// on cooldown
			// todo: want to have the server regulate on the client's state
			return; 
		}
		
		{
			CombatPowerStateDef *pOldMode = NULL;
			if (pState->pchCurrentState)
				pOldMode = CombatPowerStateSwitching_GetStateByName(pState->pchCurrentState, pDef);
			CombatPowerStateSwitching_ExitState(pEnt->pChar, pDef, pState, pOldMode, uiActivateTime);
		}

		if (pchState && pchState[0] != 0)
		{
			CombatPowerStateDef *pNextMode = NULL;
			
			pchState = allocFindString(pchState);

			pNextMode = CombatPowerStateSwitching_GetStateByName(pchState, pDef);

			if (pNextMode)
			{
				pState->uCurActIdOffset = uActId;
				CombatPowerStateSwitching_EnterState(pEnt->pChar, pState, pNextMode, uiActivateTime);
			}

			pState->fCooldown = pDef->fSwitchCooldown;
			entity_SetDirtyBit(pEnt, parse_Character, pEnt->pChar, false);
		}
	}
}

// -------------------------------------------------------------------------------------------------------------------
static void _DestroySubCombatStatePowers(Character *pChar, Power *pPow)
{
	FOR_EACH_IN_EARRAY(pPow->ppSubCombatStatePowers, Power, pLinkedPower)
	{
		power_Destroy(pLinkedPower, pChar);
	}
	FOR_EACH_END
}

// -------------------------------------------------------------------------------------------------------------------
static S32 _AreSubCombatStatePowersValid(Power *pPower, CombatPowerStatePowerSet* pLinkedSet)
{
	S32 iSize = eaSize(&pLinkedSet->eaPowers);
	if(eaSize(&pLinkedSet->eaPowers) != eaSize(&pPower->ppSubCombatStatePowers))
		return false;

	{
		S32 i;
		for (i = 0; i < iSize; ++i)
		{
			CombatPowerStatePower *pStatePowerDef = pLinkedSet->eaPowers[i];
			Power *pStatePower = pPower->ppSubCombatStatePowers[i];

			if (GET_REF(pStatePowerDef->hPowerDef) != GET_REF(pStatePower->hDef))
			{
				return false;
			}
		}
	}

	return true;
}

// -------------------------------------------------------------------------------------------------------------------
void CombatPowerStateSwitching_CreateModePowers(Character *pChar, Power *pPow)
{
	if (pChar->pCombatPowerStateInfo)
	{
		CombatPowerStateSwitchingDef *pDef = NULL;
		CombatPowerStateSwitchingInfo *pState = NULL;
		PowerDef *pPowerDef = NULL;

		pState = pChar->pCombatPowerStateInfo;
		pDef = GET_REF(pState->hCombatPowerStateSwitchingDef);
		if (!pDef)
			return;

		pPowerDef = GET_REF(pPow->hDef);
		if(pPowerDef)
		{
			CombatPowerStatePowerSet* pLinkedSet = CombatPowerStateSwitching_GetPowerSetForPowerDef(pChar, pDef, pPowerDef);
			if (pLinkedSet)
			{
				S32 bGood = false;
				if(pLinkedSet->eaPowers)
				{
					bGood = _AreSubCombatStatePowersValid(pPow, pLinkedSet);
					if(!bGood)
					{
						_DestroySubCombatStatePowers(pChar, pPow);
					}
				}

				if (!bGood)
				{
					FOR_EACH_IN_EARRAY_FORWARDS(pLinkedSet->eaPowers, CombatPowerStatePower, pLinked)
					{
						Power *pLinkedPower = NULL;
						const char *pszLinkedPowerDefName = REF_STRING_FROM_HANDLE(pLinked->hPowerDef);

						if (pszLinkedPowerDefName && 
							(pLinkedPower = power_Create(pszLinkedPowerDefName)))
						{
							eaPush(&pPow->ppSubCombatStatePowers, pLinkedPower);

							pLinkedPower->pCombatPowerStateParent = pPow;
							pLinkedPower->eSource = pPow->eSource;
							pLinkedPower->pSourceItem = pPow->pSourceItem;
							pLinkedPower->fYaw = pPow->fYaw;

							pLinkedPower->pchCombatPowersState = pLinked->pchState;
							power_CreateSubPowers(pLinkedPower);
						}
						else
						{
							Errorf("CombatPowerStateSwitching_CreateModePowers: Power_Create did not find %s", (pszLinkedPowerDefName ? pszLinkedPowerDefName : "Invalid Name!"));
						}
					}
					FOR_EACH_END
				}
				
				if(eaSize(&pLinkedSet->eaPowers) != eaSize(&pPow->ppSubCombatStatePowers))
					Errorf("CombatPowerStateSwitching_CreateModePowers not executed properly on power %s", pPowerDef->pchName);
			}
			else if (pPow->ppSubCombatStatePowers)
			{
				_DestroySubCombatStatePowers(pChar, pPow);
				eaDestroy(&pPow->ppSubCombatStatePowers);
			}
		}
	}
}


// -------------------------------------------------------------------------------------------------------------------
// Quick function to fix backpointers to parent power, generally only used by the client
void CombatPowerStateSwitching_FixSubPowers(Character *pChar, Power *pPow)
{
	if (pChar->pCombatPowerStateInfo)
	{
		CombatPowerStateSwitchingDef *pDef = NULL;
		CombatPowerStateSwitchingInfo *pState = NULL;
		PowerDef *pPowerDef = NULL;

		pState = pChar->pCombatPowerStateInfo;
		pDef = GET_REF(pState->hCombatPowerStateSwitchingDef);
		if (!pDef)
			return;

		pPowerDef = GET_REF(pPow->hDef);
		if(pPowerDef)
		{
			CombatPowerStatePowerSet* pLinkedSet = CombatPowerStateSwitching_GetPowerSetForPowerDef(pChar, pDef, pPowerDef);
			if (pLinkedSet)
			{
				FOR_EACH_IN_EARRAY(pPow->ppSubCombatStatePowers, Power, pLinkedPower)
				{
					PowerDef *pLinkedPowerDef = GET_REF(pLinkedPower->hDef);

					pLinkedPower->pCombatPowerStateParent = pPow;
					pLinkedPower->eSource = pPow->eSource;
					pLinkedPower->pSourceItem = pPow->pSourceItem;
					pLinkedPower->fYaw = pPow->fYaw;
					
					// find the mode
					if (pLinkedPowerDef)
					{
						FOR_EACH_IN_EARRAY(pLinkedSet->eaPowers, CombatPowerStatePower, pLinked)
						{
							if (GET_REF(pLinkedPower->hDef) == pLinkedPowerDef)
							{
								pLinkedPower->pchCombatPowersState = pLinked->pchState;
								break;
							}
						}
						FOR_EACH_END
					}

					power_FixSubPowers(pLinkedPower);
				}
				FOR_EACH_END
			}
			else if (pPow->ppSubCombatStatePowers)
			{
				eaDestroy(&pPow->ppSubCombatStatePowers);
			}
		}
	}
}


#include "CombatPowerStateSwitching_h_ast.c"
