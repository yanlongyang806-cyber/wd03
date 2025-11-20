/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "PowerEnhancements.h"

#include "Character.h"
#include "CharacterAttribs.h"
#include "CombatConfig.h"
#include "Entity.h"
#include "timing.h"

#include "CombatEval.h"
#include "itemCommon.h"
#include "Powers.h"
#include "PowerReplace.h"
#include "PowerTree.h"
#include "AutoGen/Powers_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

//  TODO(JW): Enhancements: Optimize this whole process

int power_EnhancementAttachIsAllowed(int iPartitionIdx, 
										SA_PARAM_OP_VALID Character *pChar, 
										SA_PARAM_NN_VALID PowerDef *pdefEnhancement, 
										SA_PARAM_NN_VALID PowerDef *pdefTarget,
										int bIsUnownedPower)
{
	int bReturn = true;

	if (pdefTarget->bNeverAttachEnhancements)
		return false;

	if(pdefEnhancement->pExprEnhanceAttach)
	{
		combateval_ContextReset(kCombatEvalContext_Enhance);
		combateval_ContextSetupEnhance(pChar,pdefTarget,bIsUnownedPower);
		bReturn = (0.0f != combateval_EvalNew(iPartitionIdx,pdefEnhancement->pExprEnhanceAttach,kCombatEvalContext_Enhance,NULL));
	}
	return bReturn;
}

static bool ApplyIsAllowed(int iPartitionIdx, SA_PARAM_NN_VALID PowerDef *pdefEnhancement)
{
	int i,r = true;
	PERFINFO_AUTO_START_FUNC();

	for(i=eaSize(&g_ppchPowersDisabled)-1; i>=0; i--)
	{
		if(!strcmp(pdefEnhancement->pchName,g_ppchPowersDisabled[i]))
		{
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	if(pdefEnhancement->pExprEnhanceApply)
	{
		r = (0.0 != combateval_EvalNew(iPartitionIdx,pdefEnhancement->pExprEnhanceApply,kCombatEvalContext_Apply,NULL));
	}
	PERFINFO_AUTO_STOP();
	return r;
}

// evaluates the power's pExprEnhanceAttach on the entity 
int power_EnhancementAttachToEntCreateAllowed(	int iPartitionIdx, 
												SA_PARAM_NN_VALID Character *pOwnerChar, 
												SA_PARAM_NN_VALID PowerDef *pdefEnhancement, 
												SA_PARAM_NN_VALID Entity *pEntCreate)
{
	int bReturn = false;
	if(pdefEnhancement->pExprEnhanceEntCreate && pEntCreate->pChar)
	{
		combateval_ContextReset(kCombatEvalContext_Enhance);
		combateval_ContextSetupEntCreateEnhancements(pOwnerChar, pdefEnhancement, pEntCreate->pChar);
		bReturn = (0.0f != combateval_EvalNew(iPartitionIdx,pdefEnhancement->pExprEnhanceEntCreate,kCombatEvalContext_EntCreateEnhancements,NULL));
	}
	return bReturn;
}

static PowerAttribEnhancements* power_GetPowerAttribEnhancements(Power *pPower, S32 iAtribIdx, bool bCreate)
{
	FOR_EACH_IN_EARRAY(pPower->ppAttribEnhancements, PowerAttribEnhancements, pPowerAttribEnhancements)
	{
		if (pPowerAttribEnhancements->iAttribIdx == iAtribIdx)
		{
			return pPowerAttribEnhancements;
		}
	}
	FOR_EACH_END

	if (bCreate)
	{
		PowerAttribEnhancements *pEnhancement = malloc(sizeof(PowerAttribEnhancements));
		pEnhancement->iAttribIdx = iAtribIdx;
		pEnhancement->puiEnhancementIDs = NULL;
		pEnhancement->puiExpirationEnhancementIDs = NULL;
		eaPush(&pPower->ppAttribEnhancements, pEnhancement);
		return pEnhancement;
	}

	return NULL;
}

// check if the enhancement 
static __forceinline S32 power_CanAttachToPowerAttribs(	SA_PARAM_NN_VALID PowerDef *pEnhancementDef, 
														SA_PARAM_NN_VALID PowerDef *pAttachTo,
														bool bAttachedToParent)
{
	if (pEnhancementDef->eEnhancementAttachUnowned == kEnhancementAttachUnownedType_Never)
	{
		return false;
	}
	if (pEnhancementDef->eEnhancementAttachUnowned == kEnhancementAttachUnownedType_AlwaysIfAttached && 
		!bAttachedToParent)
		return false;

	if (!pAttachTo->bHasAttribApplyUnownedPowers || 
		pAttachTo->eType == kPowerType_Combo ||
		pAttachTo->eType == kPowerType_Innate ||
		pAttachTo->eType == kPowerType_Enhancement)
	{
		return false;
	}

	return true;
}

static void power_AttachEnhancementsToPowerAttribs(	int iPartitionIdx, Character *pChar, 
													Power *pPower, PowerDef *pPowerDef, 
													SA_PARAM_NN_VALID Power *pEnhancementPower, 
													SA_PARAM_NN_VALID PowerDef *pEnhancementDef)
{
	// check all the attribs to see if we have some that have unowned powerDefs that we might need to enhance
	bool bAttached = false;
	

	FOR_EACH_IN_EARRAY(pPowerDef->ppOrderedMods, AttribModDef, pModDef)
	{
		PowerAttribEnhancements *pPowerAttribEnhancement = NULL;
		if (pModDef->pExpiration)
		{
			PowerDef *pExpirationPowerDef = GET_REF(pModDef->pExpiration->hDef);
			if (pExpirationPowerDef && !pExpirationPowerDef->bNeverAttachEnhancements && 
				(pEnhancementDef->eEnhancementAttachUnowned == kEnhancementAttachUnownedType_AlwaysIfAttached ||
					 power_EnhancementAttachIsAllowed(iPartitionIdx, pChar, pEnhancementDef, pExpirationPowerDef, true)))
			{
				pPowerAttribEnhancement = power_GetPowerAttribEnhancements(pPower, pModDef->uiDefIdx, true);
				eaiPushUnique(&pPowerAttribEnhancement->puiExpirationEnhancementIDs, pEnhancementPower->uiID);

				PowersDebugPrintEnt(EPowerDebugFlags_ENHANCEMENT, pChar->pEntParent, 
					"Enhancement: %s attached to Power Attrib %2d Expiration: %s\n",
					pEnhancementDef->pchName, pModDef->uiDefIdx, pExpirationPowerDef->pchName);
			}
		}

		
		if (IS_SPECIAL_ATTRIB(pModDef->offAttrib) && IS_BASIC_ASPECT(pModDef->offAspect) && pModDef->pParams)
		{	// if it's a special attrib, check the types we wish to attach to and try their powerDefs
			PowerDef *pTestPowerDef = NULL;

			switch(pModDef->offAttrib)
			{
				xcase kAttribType_TriggerComplex:
			{
				TriggerComplexParams *pParams = (TriggerComplexParams*)(pModDef->pParams);
				pTestPowerDef = GET_REF(pParams->hDef);
			}
			xcase kAttribType_TriggerSimple:
			{
				TriggerSimpleParams *pParams = (TriggerSimpleParams*)(pModDef->pParams);
				pTestPowerDef = GET_REF(pParams->hDef);
			}
			xcase kAttribType_ApplyPower:
			{
				ApplyPowerParams *pParams = (ApplyPowerParams*)(pModDef->pParams);
				pTestPowerDef = GET_REF(pParams->hDef);
			}
			xcase kAttribType_DamageTrigger:
			{
				DamageTriggerParams *pParams = (DamageTriggerParams*)(pModDef->pParams);
				pTestPowerDef = GET_REF(pParams->hDef);
			}
			xcase kAttribType_KillTrigger:
			{
				KillTriggerParams *pParams = (KillTriggerParams*)(pModDef->pParams);
				pTestPowerDef = GET_REF(pParams->hDef);
			}
			}

			if (pTestPowerDef && !pTestPowerDef->bNeverAttachEnhancements &&
				(pEnhancementDef->eEnhancementAttachUnowned == kEnhancementAttachUnownedType_AlwaysIfAttached ||
					power_EnhancementAttachIsAllowed(iPartitionIdx, pChar, pEnhancementDef, pTestPowerDef, true)))
			{
				if (!pPowerAttribEnhancement)
					pPowerAttribEnhancement = power_GetPowerAttribEnhancements(pPower, pModDef->uiDefIdx, true);

				PowersDebugPrintEnt(EPowerDebugFlags_ENHANCEMENT, pChar->pEntParent, 
					"Enhancement: %s attached to Power's Attrib %2d PowerDef: %s\n",
					pEnhancementDef->pchName, pModDef->uiDefIdx, pTestPowerDef->pchName);

				eaiPushUnique(&pPowerAttribEnhancement->puiEnhancementIDs, pEnhancementPower->uiID);
			}
		}
	}
	FOR_EACH_END

	if (bAttached)
	{
		// The enhancement now knows it enhances something on the source power
		eaiPushUnique(&pEnhancementPower->puiEnhancementIDs, pPower->uiID);
	}
}

// Attempts to attach the given pEnhancementDef to the pPow
static void power_AttachEnhancementsToPower(int iPartitionIdx, Character *pChar, 
											Power *pPower, PowerDef *pPowerDef, 
											SA_PARAM_NN_VALID Power *pEnhancementPower, 
											SA_PARAM_NN_VALID PowerDef *pEnhancementDef)
{
	bool bProcessedComboSubPowers = false;
	bool bAttachedToParent = false;

	if (pChar->bBecomeCritter && pEnhancementPower->eSource!=kPowerSource_AttribMod) // Check for BecomeCritter restriction
		return;
	
	devassert(pEnhancementDef->eType == kPowerType_Enhancement);

	if(power_EnhancementAttachIsAllowed(iPartitionIdx, pChar, pEnhancementDef, pPowerDef, false))
	{
		PowersDebugPrintEnt(EPowerDebugFlags_ENHANCEMENT, pChar->pEntParent, 
							"Enhancement: %s attached to Power: %s\n",
							pEnhancementDef->pchName, pPowerDef->pchName);

		bAttachedToParent = true;

		// The source power now knows it's enhanced by the enhancement
		eaiPushUnique(&pPower->puiEnhancementIDs, pEnhancementPower->uiID);

		// The enhancement now knows it enhances the source power
		eaiPushUnique(&pEnhancementPower->puiEnhancementIDs, pPower->uiID);

		// Make sure to mark the enhancement power dirty
		character_DirtyPower(pChar, pEnhancementPower);

		// If the source power was a combo, we want to add the enhancement to appropriate subs
		if(pPowerDef->eType == kPowerType_Combo)
		{
			bProcessedComboSubPowers = true;

			FOR_EACH_IN_EARRAY(pPower->ppSubPowers, Power, pSubPower)
			{
				PowerDef *pdefSub = GET_REF(pSubPower->hDef);
				if(pdefSub)
				{
					bool bAttachedToSubPower = false;
					if (power_EnhancementAttachIsAllowed(iPartitionIdx, pChar, pEnhancementDef, pdefSub, false))
					{
						eaiPushUnique(&pSubPower->puiEnhancementIDs, pEnhancementPower->uiID);
						bAttachedToSubPower = true;
					}

					if (power_CanAttachToPowerAttribs(pEnhancementDef, pdefSub, bAttachedToSubPower))
					{
						power_AttachEnhancementsToPowerAttribs(iPartitionIdx, pChar, pSubPower, pdefSub, pEnhancementPower, pEnhancementDef);
					}
				}
			}
			FOR_EACH_END
		}
	}

	// if we didn't get to go through the combo subPowers above, 
	// go through them now and see if the enhancement attaches to any attribs
	// but only attach if the unowned attach type requires us checking each attrib
	if (pPowerDef->eType == kPowerType_Combo && !bProcessedComboSubPowers && 
		pEnhancementDef->eEnhancementAttachUnowned == kEnhancementAttachUnownedType_CheckAttachExpr)
	{	
		FOR_EACH_IN_EARRAY(pPower->ppSubPowers, Power, pSubPower)
		{
			PowerDef *pdefSub = GET_REF(pSubPower->hDef);
			if(pdefSub && power_CanAttachToPowerAttribs(pEnhancementDef, pdefSub, false))
			{
				power_AttachEnhancementsToPowerAttribs(iPartitionIdx, pChar, pSubPower, pdefSub, pEnhancementPower, pEnhancementDef);
			}
		}
		FOR_EACH_END
	}

	// try and attach the enhancement to the attribs on this power
	if (power_CanAttachToPowerAttribs(pEnhancementDef, pPowerDef, bAttachedToParent))
	{
		power_AttachEnhancementsToPowerAttribs(iPartitionIdx, pChar, pPower, pPowerDef, pEnhancementPower, pEnhancementDef);
	}
	
	// go through all the sub combatStatePowers and recurse into this function to run attachment on them
	FOR_EACH_IN_EARRAY(pPower->ppSubCombatStatePowers, Power, pSubPower)
	{
		PowerDef *pdefSub = GET_REF(pSubPower->hDef);
		if(pdefSub)
			power_AttachEnhancementsToPower(iPartitionIdx, pChar, pSubPower, pdefSub, pEnhancementPower, pEnhancementDef);
	}
	FOR_EACH_END
}

// Generates the power's internal list of enhancements.  Automatically
//  updates the lists of all attached powers.  Should be called
//  whenever a power is added to a character.
void power_AttachEnhancements(int iPartitionIdx, Character *pchar, Power *ppow)
{
	PowerDef *pdef = GET_REF(ppow->hDef);

	if(pdef)
	{
		int i;

		// Make sure to mark the power dirty
		character_DirtyPower(pchar,ppow);
		entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);

		// Cleanup old arrays
		eaiClear(&ppow->puiEnhancementIDs);
		for(i=eaSize(&ppow->ppSubPowers)-1; i>=0; i--)
		{
			eaiClear(&ppow->ppSubPowers[i]->puiEnhancementIDs);
		}

		// Check for BecomeCritter restriction
		if(pchar->bBecomeCritter && ppow->eSource!=kPowerSource_AttribMod)
		{
			return;
		}

		PERFINFO_AUTO_START_FUNC();

		if(pdef->eType==kPowerType_Enhancement)
		{
			// The power is an enhancement, find non-enhancements/innates
			for(i=eaSize(&pchar->ppPowers)-1; i>=0; i--)
			{
				Power *ppowAttach = pchar->ppPowers[i];
				PowerDef *pdefAttach = GET_REF(ppowAttach->hDef);
				if(pdefAttach
					&& pdefAttach->eType!=kPowerType_Enhancement
					&& pdefAttach->eType!=kPowerType_Innate)
				{
					power_AttachEnhancementsToPower(iPartitionIdx, pchar, ppowAttach, pdefAttach, ppow, pdef);
				}
			}
		}
		else if(pdef->eType!=kPowerType_Innate)
		{
			// The power is not an enhancement/innate, find enhancements
			for(i=eaSize(&pchar->ppPowers)-1; i>=0; i--)
			{
				Power *ppowAttach = pchar->ppPowers[i];
				PowerDef *pdefAttach = GET_REF(ppowAttach->hDef);
				if(pdefAttach
					&& pdefAttach->eType==kPowerType_Enhancement)
				{
					power_AttachEnhancementsToPower(iPartitionIdx, pchar, ppow, pdef, ppowAttach, pdefAttach);
				}
			}
		}
		
		PERFINFO_AUTO_STOP();
	}
}

// Removes the power from all attached powers.  Should be called
//  whenever a power is removed from a character.
void power_DetachEnhancements(Character *pchar, Power *ppow)
{
	int i,j;
	for(i=eaiSize(&ppow->puiEnhancementIDs)-1; i>=0; i--)
	{
		Power *ppowAttached = character_FindPowerByID(pchar,ppow->puiEnhancementIDs[i]);
		if(ppowAttached)
		{
			// Remove the defunct id
			eaiFindAndRemove(&ppowAttached->puiEnhancementIDs,ppow->uiID);
			
			// Cleanup myself and my sub powers (if any)
			if(eaiSize(&ppowAttached->puiEnhancementIDs)<=0)
			{
				eaiDestroy(&ppowAttached->puiEnhancementIDs);
				
				// We know the parent has no enhancements, so destroy all the sub power enhancements
				for(j=eaSize(&ppowAttached->ppSubPowers)-1; j>=0; j--)
				{
					eaiDestroy(&ppowAttached->ppSubPowers[j]->puiEnhancementIDs);
				}
			}
			else
			{
				// The parent has some enhancements left, so we need to remove this one from the sub powers
				for(j=eaSize(&ppowAttached->ppSubPowers)-1; j>=0; j--)
				{
					eaiFindAndRemove(&ppowAttached->ppSubPowers[j]->puiEnhancementIDs,ppow->uiID);
				}
			}

			// if we have any enhancements attaching to attribMods check if we need to remove any 
			FOR_EACH_IN_EARRAY(ppowAttached->ppAttribEnhancements, PowerAttribEnhancements, pPowerAttribEnhancements)
			{
				bool bDestroyed = false;
				if (pPowerAttribEnhancements->puiEnhancementIDs)
				{
					eaiFindAndRemove(&pPowerAttribEnhancements->puiEnhancementIDs, ppow->uiID);
					if (eaiSize(&pPowerAttribEnhancements->puiEnhancementIDs) <= 0)
					{
						eaiDestroy(&pPowerAttribEnhancements->puiEnhancementIDs);
						bDestroyed = true;
					}
				}
								
				if (pPowerAttribEnhancements->puiExpirationEnhancementIDs)
				{
					eaiFindAndRemove(&pPowerAttribEnhancements->puiExpirationEnhancementIDs, ppow->uiID);
					if (eaiSize(&pPowerAttribEnhancements->puiExpirationEnhancementIDs) <= 0)
					{
						eaiDestroy(&pPowerAttribEnhancements->puiExpirationEnhancementIDs);
						bDestroyed = true;
					}
				}

				if (bDestroyed && 
					!pPowerAttribEnhancements->puiEnhancementIDs && 
					!pPowerAttribEnhancements->puiExpirationEnhancementIDs)
				{	// we have removed all the enhancements from this attribMod, destroy and remove it from the power's list
					eaRemoveFast(&ppowAttached->ppAttribEnhancements,FOR_EACH_IDX(-,pPowerAttribEnhancements));
					StructDestroy(parse_PowerAttribEnhancements, pPowerAttribEnhancements);
				}
			}
			FOR_EACH_END
			
		}
	}
}

// returns a list of enhancement powers that apply to the given PowerDef in pppAttachedOut
// This is fairly exhaustive as it goes through every enhancement power on the character to see if should attach. 
void power_GetEnhancementsForUnownedPower(int iPartitionIdx, Character *pChar, 
													PowerDef *pPowDef, Power ***pppAttachedOut)
{
	PERFINFO_AUTO_START_FUNC();

	if (pPowDef->eType != kPowerType_Enhancement)
	{
		// maybe we want to have a separate list of all enhancements if this ends up needing to be used more often than we'd like (almost never)
		FOR_EACH_IN_EARRAY(pChar->ppPowers, Power, pEnhancementPower)
		{
			PowerDef *pEnhancementDef = GET_REF(pEnhancementPower->hDef);

			if (pEnhancementDef && pEnhancementDef->eType == kPowerType_Enhancement && 
				power_EnhancementAttachIsAllowed(iPartitionIdx, pChar, pEnhancementDef, pPowDef, true))
			{
				eaPush(pppAttachedOut, pEnhancementPower);
			}
		}
		FOR_EACH_END
	}

	

	PERFINFO_AUTO_STOP();
}


// returns a list of enhancement powers for the given power's attribute
// if bAttribExpiration is set will get the enhancement for the attributes ModExpiration
void power_GetEnhancementsForAttribModApplyPower(int iPartitionIdx, Character *pChar, 
													AttribMod *pMod, EEnhancedAttribList eEnhancementList, 
													PowerDef *pPowerDef, Power ***peaAttachedOut)
{

	PERFINFO_AUTO_START_FUNC();

	// only get enhancements for mods that have a power
	if (pMod->uiPowerID)
	{
		Power *pPower = NULL;
		PowerRef ref = {0};

		ref.uiID = pMod->uiPowerID;
		ref.iIdxSub = pMod->iPowerIDSub;
		ref.iLinkedSub = pMod->iPowerIDLinkedSub;

		pPower = character_FindPowerByRef(pChar, &ref);
	
		if (pPower)
		{	// we have the power so check if there is any attribs that have enhancements
			PowerAttribEnhancements *pPAE = power_GetPowerAttribEnhancements(pPower, pMod->uiDefIdx, false);
			if (pPAE)
			{
				U32 *puiEnhancements = NULL;
				S32 i;

				if (eEnhancementList == EEnhancedAttribList_DEFAULT)
				{
					puiEnhancements = pPAE->puiEnhancementIDs;
				}
				else
				{
					puiEnhancements = pPAE->puiExpirationEnhancementIDs;
				}

				for(i = eaiSize(&puiEnhancements)-1; i >= 0; i--)
				{
					Power *ppowAttached = character_FindPowerByID(pChar, puiEnhancements[i]);
					if(ppowAttached)
					{
						eaPush(peaAttachedOut, ppowAttached);
					}
				}
			}
		}
	}
	
	PERFINFO_AUTO_STOP();
}

// Uses the power's internal list of enhancements to make an earray of
//  enhancing powers (or powers it is enhancing).
void power_GetEnhancements(int iPartitionIdx, Character *pchar, Power *ppow, Power ***pppAttachedOut)
{
	int i;
	static Power **s_ppEnhancementsLocal = NULL;
	
	PERFINFO_AUTO_START_FUNC();
	
	for(i=eaiSize(&ppow->puiEnhancementIDs)-1; i>=0; i--)
	{
		Power *ppowAttached = character_FindPowerByID(pchar,ppow->puiEnhancementIDs[i]);
		if(ppowAttached)
		{
			eaPush(pppAttachedOut,ppowAttached);
		}
	}
	power_GetEnhancementsTree(pchar,ppow,pppAttachedOut);
	power_GetEnhancementsPowerReplace(iPartitionIdx,pchar->pEntParent,ppow,pppAttachedOut);

	// Get LocalEnhancements off the Item, which need to check the attach (bleh)
	if(ppow->eSource==kPowerSource_Item)
	{
		eaClearFast(&s_ppEnhancementsLocal);
		power_GetEnhancementsLocalItem(pchar->pEntParent,ppow,&s_ppEnhancementsLocal);
		power_AttachEnhancementsLocal(iPartitionIdx,ppow,pchar,s_ppEnhancementsLocal,pppAttachedOut);
	}

	PERFINFO_AUTO_STOP();
}

// Removes all enhancements from the earray that are not legal to apply.  Requires
//  that the combat eval Apply context is already set up.
void power_CheckEnhancements(int iPartitionIdx, Power ***pppListOut)
{
	int i;
	for(i=eaSize(pppListOut)-1;i>=0;i--)
	{
		if(GET_REF((*pppListOut)[i]->hDef) && !ApplyIsAllowed(iPartitionIdx,GET_REF((*pppListOut)[i]->hDef)))
		{
			eaRemoveFast(pppListOut,i);
		}
	}
}

// Filters the list of Local Enhancements to ones that can actually attach, and put them into the
//  attach list
void power_AttachEnhancementsLocal(int iPartitionIdx, Power *ppow, Character *pchar, Power **ppEnhancementsLocal, Power ***pppAttachedOut)
{
	int i;
	PowerDef *pdef = GET_REF(ppow->hDef);
	for(i=eaSize(&ppEnhancementsLocal)-1; i>=0; i--)
	{
		PowerDef *pdefAttach = GET_REF(ppEnhancementsLocal[i]->hDef);
		if(pdefAttach && pdef && power_EnhancementAttachIsAllowed(iPartitionIdx,pchar,pdefAttach,pdef,false))
		{
			if(ppow->pParentPower)
			{
				PowerDef *pdefParent = GET_REF(ppow->pParentPower->hDef);
				if(pdefParent && power_EnhancementAttachIsAllowed(iPartitionIdx,pchar,pdefAttach,pdefParent,false))
				{
					eaPush(pppAttachedOut,ppEnhancementsLocal[i]);
				}
			}
			else
			{
				eaPush(pppAttachedOut,ppEnhancementsLocal[i]);
			}
		}
	}
}

static void power_enhacePowerFields(int iPartitionIdx, Character *pchar, Power *ppowAttached, PowerDef *pEnhancePowerDef) 
{
	ppowAttached->fEnhancedRange += pEnhancePowerDef->fRange;

	if(pEnhancePowerDef->pExprRadius)
	{
		combateval_ContextReset(kCombatEvalContext_Target);
		combateval_ContextSetupTarget(pchar, NULL, NULL);
		ppowAttached->fEnhancedRadius += combateval_EvalNew(iPartitionIdx, pEnhancePowerDef->pExprRadius, kCombatEvalContext_Target, NULL);
	}
}

void power_CalculateAttachEnhancementPowerFields(int iPartitionIdx, Character *pchar)
{
	// go through all the enhancement powers 
	FOR_EACH_IN_EARRAY(pchar->ppPowers, Power, pPower)
	{
		PowerDef *pPowerDef;

		if (!pPower->puiEnhancementIDs)
			continue;

		pPowerDef = GET_REF(pPower->hDef);
		if (pPowerDef && pPowerDef->bEnhancePowerFields && pPowerDef->eType == kPowerType_Enhancement)
		{
			S32 i;
			for (i = eaiSize(&pPower->puiEnhancementIDs) - 1; i >= 0; --i)
			{
				Power *pAttachedPow = character_FindPowerByID(pchar,pPower->puiEnhancementIDs[i]);
				if (pAttachedPow)
				{
					power_enhacePowerFields(iPartitionIdx, pchar, pAttachedPow, pPowerDef);
					
					if (pAttachedPow->ppSubPowers)
					{
						FOR_EACH_IN_EARRAY(pAttachedPow->ppSubPowers, Power, pSubPower)
						{
							if (eaiFind(&pAttachedPow->puiEnhancementIDs, pPower->uiID) != -1)
							{
								power_enhacePowerFields(iPartitionIdx, pchar, pSubPower, pPowerDef);
							}
						}
						FOR_EACH_END
					}


					if (g_bPowersDebug)
					{
						PowerDef *pAttachedDef = GET_REF(pAttachedPow->hDef);
						PowersDebugPrintEnt(EPowerDebugFlags_POWERS, pchar->pEntParent, 
											"Power: %s EnhancedBy: %s: range %.2f radius %.2f\n", 
											pAttachedDef->pchName, 
											pPowerDef->pchName,
											pAttachedPow->fEnhancedRange, 
											pAttachedPow->fEnhancedRadius);
					}
					
				}

				
			}
		}
	}
	FOR_EACH_END

}