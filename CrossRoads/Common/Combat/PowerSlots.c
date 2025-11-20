/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "PowerSlots.h"

#include "EntityLib.h"
#include "error.h"
#include "EString.h"

#include "Character.h"
#include "CharacterClass.h"
#include "Entity.h"
#include "EntityBuild.h"
#include "Powers.h"
#include "PowerActivation.h"
#include "PowerTree.h"
#include "NotifyCommon.h"
#include "StringFormat.h"
#include "GameStringFormat.h"
#include "Player.h"
#include "FolderCache.h"
#include "file.h"
#include "CombatPowerStateSwitching.h"

#include "AutoGen/PowerSlots_h_ast.h"
#ifdef GAMESERVER
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static PowerSlotLoadData s_Slots = {0};
static S32 s_bIgnoreSlotting = false;

PowerSlotsConfig g_PowerSlotsConfig = { 0 };

#define POWERSLOTSET_MAX 10

// Returns the PowerSlots the Character has access to
SA_RET_OP_VALID static PowerSlot **CharacterGetPowerSlotEArray(SA_PARAM_NN_VALID const Character *pchar)
{
	CharacterClass *pclass = character_GetClassCurrent(pchar);
	if(pclass && pclass->ppPowerSlots)
	{
		return pclass->ppPowerSlots;
	}
	return s_Slots.ppSlots;
}

SA_RET_OP_VALID static PowerSlot **CharacterGetPowerSlotEArrayInTray(SA_PARAM_NN_VALID const Character *pchar, int iTray)
{
	EntityBuild *pbuild = pchar->pEntParent ? entity_BuildGet(pchar->pEntParent, iTray) : NULL;
	CharacterClass *pclass = pbuild ? GET_REF(pbuild->hClass) : NULL;
	if(pbuild && pclass && pclass->ppPowerSlots)
	{
		return pclass->ppPowerSlots;
	}
	return s_Slots.ppSlots;
}

SA_RET_OP_VALID PowerSlot *CharacterGetPowerSlotInTrayAtIndex(SA_PARAM_NN_VALID const Character *pchar, int iTray, int iSlot)
{
	EntityBuild *pbuild = pchar->pEntParent ? entity_BuildGet(pchar->pEntParent, iTray) : NULL;
	CharacterClass *pclass = pbuild ? GET_REF(pbuild->hClass) : NULL;
	if(pbuild && pclass && pclass->ppPowerSlots)
	{
		return eaGet(&pclass->ppPowerSlots,iSlot);
	}
	return s_Slots.ppSlots ? eaGet(&s_Slots.ppSlots,iSlot) : NULL;
}

// Builds a comma separated list of translated category names from the given category list
void character_PowerSlotGetTranslatedPowerCategoryNames(SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_NN_VALID char **pestrCategoryNamesOut, SA_PARAM_NN_VALID const S32 * const piCategoryList)
{
	S32 i;

	estrClear(pestrCategoryNamesOut);

	for (i = 0; i < eaiSize(&piCategoryList); i++)
	{
		Message *pCategoryMessage = StaticDefineGetMessage(PowerCategoriesEnum, piCategoryList[i]);

		if (i > 0)
		{
			estrAppend2(pestrCategoryNamesOut, ", ");
		}

		if (pCategoryMessage)
		{
			estrAppend2(pestrCategoryNamesOut, entTranslateMessage(pEnt, pCategoryMessage));
		}
		else
		{
			estrAppend2(pestrCategoryNamesOut, StaticDefineIntRevLookup(PowerCategoriesEnum, piCategoryList[i]));
		}
	}

}

#if defined(GAMESERVER) || defined (GAMECLIENT)
static void character_PowerSlotSendCategoryMismatchError(SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_NN_VALID PowerSlot *pSlot, SA_PARAM_NN_VALID const PowerDef *pPowerDef, bool bInclusionFailed)
{
	Message *pErrorMessage;
	const char *pchMessage = NULL;
	if (bInclusionFailed)
	{
		pchMessage = "PowerSlotsMessage.Error.PowerCategoryDoesNotExistInInclusionList";
	}
	else
	{
		pchMessage = "PowerSlotsMessage.Error.PowerCategoryExistsInExclusionList";
	}
	pErrorMessage = RefSystem_ReferentFromString(gMessageDict, pchMessage);

	if (pErrorMessage)
	{
		char *pchTemp = NULL;
		char *pchCategoryNames = NULL;

		estrStackCreate(&pchTemp);
		estrStackCreate(&pchCategoryNames);

		character_PowerSlotGetTranslatedPowerCategoryNames(pEnt, &pchCategoryNames, bInclusionFailed ? pSlot->peRequires : pSlot->peExcludes);
		FormatMessageKey(&pchTemp, 
			pchMessage, 
			STRFMT_STRING("Power", TranslateDisplayMessage(pPowerDef->msgDisplayName)),
			STRFMT_STRING("PowerSlotCategoryNames", pchCategoryNames),
			STRFMT_END);
		notify_NotifySend(pEnt, kNotifyType_PowerSlottingError_PowerCategoryDoesNotExistInInclusionList, pchTemp, pPowerDef->pchName, NULL);

		estrDestroy(&pchCategoryNames);
		estrDestroy(&pchTemp);
	}
}
#endif

// Returns true if the PowerSlot at the given index allows the PowerDef
S32 character_PowerTraySlotAllowsPowerDef(const Character *pchar, int iTray, int iSlot, const PowerDef *pDef, bool bNotifyErrors)
{
	PowerSlot *pSlot = NULL;
	if(iTray < 0)
		pSlot = character_PowerSlotGetPowerSlot(pchar,iSlot);	
	else
		pSlot = CharacterGetPowerSlotInTrayAtIndex(pchar,iTray,iSlot);
	
	if(!pSlot)
		return false;

	if(pDef->piCategories)
	{
		int i;
		for(i=eaiSize(&pSlot->peRequires)-1; i>=0; i--)
		{
			if(-1==eaiFind(&pDef->piCategories,pSlot->peRequires[i]))
			{
#if defined(GAMESERVER) || defined (GAMECLIENT)
				if (pchar->pEntParent && bNotifyErrors)
				{
					character_PowerSlotSendCategoryMismatchError(pchar->pEntParent, pSlot, pDef, true);
				}
#endif
				return false;
			}
		}
		for(i=eaiSize(&pSlot->peExcludes)-1; i>=0; i--)
		{
			if(-1!=eaiFind(&pDef->piCategories,pSlot->peExcludes[i]))
			{
#if defined(GAMESERVER) || defined (GAMECLIENT)
				if (pchar->pEntParent && bNotifyErrors)
				{
					character_PowerSlotSendCategoryMismatchError(pchar->pEntParent, pSlot, pDef, false);
				}
#endif
				return false;
			}
		}
		return true;
	}
	return false;
}


static U32 **PowerSlotsCurrent(SA_PARAM_NN_VALID Character *pchar, S32 bCreate)
{
	U32 **ppReturn = NULL;
	
	// Lazy create for the main structure
	if(!pchar->pSlots && bCreate)
	{
		pchar->pSlots = StructAlloc(parse_CharacterPowerSlots);
		entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
	}

	if(pchar->pSlots)
	{
		if(pchar->pSlots->uiIndex < POWERSLOTSET_MAX)
		{
			PowerSlotSet *pset;
			if(pchar->pSlots->uiIndex >= eaUSize(&pchar->pSlots->ppSets))
			{
				// Persisted data cannot consist of sparse earrays -BZ
				while (pchar->pSlots->uiIndex >= eaUSize(&pchar->pSlots->ppSets))
				{
					eaPush(&pchar->pSlots->ppSets, StructAlloc(parse_PowerSlotSet));
				}

				entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
			}

			pset = pchar->pSlots->ppSets[pchar->pSlots->uiIndex];
			
			if(pset)
			{
				ppReturn = &(pset->puiPowerIDs);
			}
		}
	}

	return ppReturn;
}

static U32 *PowerSlotsCurrentDirect(SA_PARAM_NN_VALID const Character *pchar)
{
	U32 *pReturn = NULL;

	if(pchar->pSlots)
	{
		if(pchar->pSlots->uiIndex < POWERSLOTSET_MAX)
		{
			PowerSlotSet *pset;
			int s = eaSize(&pchar->pSlots->ppSets);
			//JWCHECK
			entity_SetDirtyBit(pchar->pEntParent, parse_Character, (Character*)pchar, false);
			if(pchar->pSlots->uiIndex < (U32)s)
			{
				pset = pchar->pSlots->ppSets[pchar->pSlots->uiIndex];

				if(pset)
				{
					pReturn = pset->puiPowerIDs;
				}
			}
		}
	}

	return pReturn;
}

static U32 **PowerSlotsByIndex(SA_PARAM_NN_VALID Character *pchar, U32 uiBuildIndex)
{
	U32 **ppReturn = NULL;

	if(pchar && pchar->pSlots && uiBuildIndex < POWERSLOTSET_MAX)
	{
		PowerSlotSet *pset;
		
		if(uiBuildIndex >= eaUSize(&pchar->pSlots->ppSets))
		{
			// Persisted data cannot consist of sparse earrays -BZ
			while (uiBuildIndex >= eaUSize(&pchar->pSlots->ppSets))
			{
				eaPush(&pchar->pSlots->ppSets, StructAlloc(parse_PowerSlotSet));
			}

			entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
		}

		pset = pchar->pSlots->ppSets[uiBuildIndex];
			
		ppReturn = &(pset->puiPowerIDs);
	}

	return ppReturn;
}

// Returns true if the Character's PowerSlots are valid, meaning all
//  slotted Powers are allowed in their slots.  Checks only the
//  current set.  Any invalid slottings are removed.
S32 character_PowerSlotsValidate(const Character *pchar)
{
	S32 bValid = true;
	if(pchar->pSlots)
	{
		int i;
		U32 *puiIDs = PowerSlotsCurrentDirect(pchar);
		if(puiIDs)
		{
			for(i=ea32Size(&puiIDs)-1; i>=0; i--)
			{
				if(puiIDs[i])
				{
					Power *ppow = character_FindPowerByID(pchar,puiIDs[i]);
					PowerDef *pdef = ppow ? GET_REF(ppow->hDef) : NULL;
					if(!pdef || !character_PowerTraySlotAllowsPowerDef(pchar, -1, i, pdef, false))
					{
						bValid = false;
						puiIDs[i] = 0;
					}
				}
			}
		}
	}
	return bValid;
}

static bool character_PowerSlotIsLockedByCooldown(Character* pChar, int iTray, int iSlot, bool bNotifyErrors)
{
	// See if there is a power in this slot that is on cooldown:
	S32 idOfOldPowerInThisSlot = character_PowerSlotGetFromTray(pChar, iTray, iSlot);
	Power *pOldPower = character_FindPowerByID(pChar, idOfOldPowerInThisSlot);

	if( !g_PowerSlotsConfig.bDisableSlottingIfCooldown)
	{
		return false;
	}

	// if the old power in this slot is on cooldown, return false.
	if(pOldPower && pOldPower->fTimeRecharge)
	{
#if defined(GAMESERVER) || defined(GAMECLIENT)
		if (bNotifyErrors)
		{
			notify_NotifySend(pChar->pEntParent, kNotifyType_PowerSlottingError_SlotIsLocked, entTranslateMessageKey(pChar->pEntParent, "Powers.SlottingDisabledError.Cooldown"), NULL, NULL);
		}
#endif
		return true;
	}
	return false;
}

// Places the Power with the specified ID into the specified PowerSlot.
//  If the PowerID is 0, it empties the slot. Returns true if successful.
//  Generally should be called on the server.
//  bSwapIfPresent: True: if the power is already on this tray, move it to the new slot and move what was here to where it was. False: fail if the power is on the tray.
//  bIgnoreCooldown: skip cooldown, even if game is configured to have one. e.g. the server set this for you because you leveled up.
S32 character_PowerTraySlotSet(	int iPartitionIdx,
							    Character *pChar,
								int iTray,
								int iSlot,
								U32 uiIDPower,
								bool bSwapIfPresent,
								GameAccountDataExtract *pExtract,
								bool bIgnoreCooldown,
								bool bNotifyErrors)
{
	int iSlotCount = character_PowerSlotCountInTray(pChar, iTray);
	U32 **ppSlots;
	S32 oldSlotOfThisPower ;

	// Valid slot index
	if(iSlot>=iSlotCount)
		return false;

	// Make sure the slot is unlocked
	if (!character_PowerSlotIsUnlockedBySlotInTray(pChar, iTray, iSlot))
	{
#if defined(GAMESERVER) || defined(GAMECLIENT)
		if (bNotifyErrors)
		{
			Message *pErrorMessage = RefSystem_ReferentFromString(gMessageDict, "PowerSlotsMessage.Error.LockedSlot");
			if (pErrorMessage && pChar->pEntParent)
			{
				notify_NotifySend(pChar->pEntParent, kNotifyType_PowerSlottingError_SlotIsLocked, 
					entTranslateMessage(pChar->pEntParent, pErrorMessage), NULL, NULL);
			}
		}
#endif
		return false;
	}

	// Valid power
	if(uiIDPower)
	{
		Power *ppow = character_FindPowerByID(pChar,uiIDPower);
		PowerDef *pdef = ppow ? GET_REF(ppow->hDef) : NULL;
		if(!pdef)
			return false;

		if(ppow->uiPowerSlotReplacementID)
			return false;

		if(!character_PowerTraySlotAllowsPowerDef(pChar, iTray, iSlot, pdef, bNotifyErrors))
			return false;
	}

	// Get the current slots
	ppSlots = PowerSlotsByIndex(pChar, iTray < 0 ? pChar->pSlots->uiIndex : iTray);
	if(!ppSlots)
		return false;

	if(uiIDPower)
	{
		// If the power is already slotted, 
		// swap whatever's in this slot there or fail (depending on bSwapIfPresent)
		oldSlotOfThisPower = ea32Find(ppSlots,uiIDPower);
		if(-1 != oldSlotOfThisPower && bSwapIfPresent)
		{
			PowerSlot* pOldSlot = CharacterGetPowerSlotInTrayAtIndex(pChar, iTray, oldSlotOfThisPower);
			if(pOldSlot->bPreventSwappingDuringCooldown)
			{
				//the old slot of this power does not allow swapping powers while on cooldown.
				if(character_PowerSlotIsLockedByCooldown(pChar, iTray, oldSlotOfThisPower, bNotifyErrors))
				{
					//and it's on cooldown, so this power can't move to the new slot.
					return false;
				}
			}
			else
			{
				//this is just swapping two powers on the same tray, so we don't need cooldown to prevent exploits.
				bIgnoreCooldown = true;
			}
			(*ppSlots)[oldSlotOfThisPower] = (*ppSlots)[iSlot];
		}
		else if(-1 != oldSlotOfThisPower && !bSwapIfPresent)
		{
			return false;
		}
	}

	if (iSlot >= 0 && !bIgnoreCooldown)
	{
		if(character_PowerSlotIsLockedByCooldown(pChar, iTray, iSlot, bNotifyErrors))
		{
			return false;
		}
	}

	if(!bIgnoreCooldown 
		&& g_PowerSlotsConfig.fSecondsCooldownAfterSlotting
		&& (!g_PowerSlotsConfig.eModeToDisableCooldown 
			|| !character_HasMode(pChar, g_PowerSlotsConfig.eModeToDisableCooldown)))
	{
		Power *ppow = character_FindPowerByID(pChar,uiIDPower);
		if(ppow && entGetAccessLevel(pChar->pEntParent) < 9)
		{
			//set a cooldown on the new power
			power_SetRecharge(iPartitionIdx, pChar, ppow, g_PowerSlotsConfig.fSecondsCooldownAfterSlotting);
#ifdef GAMESERVER
			//tell the client so UI updates.
			ClientCmd_PowerSetRechargeClient(pChar->pEntParent,uiIDPower,g_PowerSlotsConfig.fSecondsCooldownAfterSlotting);
#endif
		}
	}
	  

	// Set size to be safe, set value
	ea32SetSize(ppSlots,iSlotCount);
	(*ppSlots)[iSlot] = uiIDPower;
	entity_SetDirtyBit(pChar->pEntParent,parse_Character,pChar,0);

	// Make sure to refresh any passives after a slotting change
	character_RefreshPassives(iPartitionIdx,pChar,pExtract);
	
	return true;
}

S32 character_PowerTraySlotSetNode(	int iPartitionIdx,
									Character *pchar,
									int iTray,
									int iSlot,
									PTNodeDef *pNodeDef,
									PTGroupDef *pGroupDef,
									bool bSwapIfPresent,
									GameAccountDataExtract *pExtract,
									bool bIgnoreCooldown)
{
	PTNode *pNode = powertree_FindNode(pchar, NULL, pNodeDef->pchNameFull);
	Power *pPower = (pNode && pNode->ppPowers) ? eaTail(&pNode->ppPowers) : NULL;
	U32 uiIDPower = pPower ? pPower->uiID : 0;

	// Make sure the slot is unlocked
	if (!character_PowerSlotIsUnlockedBySlotInTray(pchar, iTray, iSlot))
	{
		return false;
	}

	if (!uiIDPower)
		return false;

	if (character_PowerTraySlotSet(iPartitionIdx, pchar, iTray, iSlot, uiIDPower, bSwapIfPresent, pExtract, false, bIgnoreCooldown))
	{
		return true;
	}
	else
	{
		int i;
		for (i = 0; i < eaSize(&pGroupDef->ppNodes); i++)
		{
			if (pGroupDef->ppNodes[i] && pGroupDef->ppNodes[i]->bSlave)
			{
				PTNodeDef *pSlaveNodeDef = pGroupDef->ppNodes[i];
				PTNode *pSlaveNode = pSlaveNodeDef ? powertree_FindNode(pchar, NULL, pSlaveNodeDef->pchNameFull) : NULL;
				const Power *pSlavePower = (pSlaveNode && pSlaveNode->ppPowers[pSlaveNode->iRank]) ? pSlaveNode->ppPowers[pSlaveNode->iRank] : NULL;
				uiIDPower = pSlavePower ? pSlavePower->uiID : 0;

				if (uiIDPower && character_PowerTraySlotSet(iPartitionIdx, pchar, iTray, iSlot, uiIDPower, bSwapIfPresent, pExtract, false, bIgnoreCooldown))
				{	
					return true;
				}
			}
		}
	}
	return false;
}

// Attempts to swap the IDs in the two specified slots.
//  Generally should be called on the server.
S32 character_PowerTraySlotSwap(int iPartitionIdx,
								Character *pchar,
								int iTrayA,
								int iSlotA,
								int iTrayB,
								int iSlotB,
								GameAccountDataExtract *pExtract)
{
	S32 bSuccess = false;

	if(iSlotA != iSlotB &&
		character_PowerSlotIsUnlockedBySlotInTray(pchar, iTrayA, iSlotA) &&
		character_PowerSlotIsUnlockedBySlotInTray(pchar, iTrayB, iSlotB))
	{
		U32 uiIDA = character_PowerSlotGetFromTray(pchar,iTrayA,iSlotA);
		U32 uiIDB = character_PowerSlotGetFromTray(pchar,iTrayB,iSlotB);

		if(uiIDA || uiIDB)
		{
			PowerSlot* pSlotA = CharacterGetPowerSlotInTrayAtIndex(pchar, iTrayA, iSlotA);
			PowerSlot* pSlotB = CharacterGetPowerSlotInTrayAtIndex(pchar, iTrayA, iSlotB);
			if(pSlotA->bPreventSwappingDuringCooldown && character_PowerSlotIsLockedByCooldown(pchar, iTrayA, iSlotA, true)
			|| pSlotB->bPreventSwappingDuringCooldown && character_PowerSlotIsLockedByCooldown(pchar, iTrayB, iSlotB, true))
			{
				return false;
			}
			// Unslot both slots
			character_PowerTraySlotSet(iPartitionIdx, pchar, iTrayA, iSlotA, 0, false, pExtract, true, false);
			character_PowerTraySlotSet(iPartitionIdx, pchar, iTrayB, iSlotB, 0, false, pExtract, true, false);

			// Slot both slots with swapped IDs.	(no cooldown for power swaps)
			bSuccess = character_PowerTraySlotSet(iPartitionIdx, pchar, iTrayA, iSlotA, uiIDB, false, pExtract, true, false) && 
				character_PowerTraySlotSet(iPartitionIdx, pchar, iTrayB, iSlotB, uiIDA, false, pExtract, true, false);

			if(!bSuccess)
			{
				// Slot old IDs on failure
				character_PowerTraySlotSet(iPartitionIdx, pchar, iTrayA, iSlotA, uiIDA, false, pExtract, true, false);
				character_PowerTraySlotSet(iPartitionIdx, pchar, iTrayB, iSlotB, uiIDB, false, pExtract, true, false);
			}
		}
	}

	return bSuccess;
}

// Rebuilds or cleans up the Character's BecomeCritter-specific PowerSlots
static void CharacterPowerSlotsBecomeCritterUpdate(SA_PARAM_NN_VALID Character *pchar)
{
	if(pchar->bBecomeCritter)
	{
		int i, j, s, t;
		int iTray = -1;
		U32 *puiOld = NULL;
		if(pchar->pSlotSetBecomeCritter)
		{
			ea32Copy(&puiOld,&pchar->pSlotSetBecomeCritter->puiPowerIDs);
			StructDestroy(parse_PowerSlotSet, pchar->pSlotSetBecomeCritter);
		}

		pchar->pSlotSetBecomeCritter = StructCreate(parse_PowerSlotSet);

		s = eaSize(&pchar->ppPowers);
		t = character_PowerSlotCountInTray(pchar,iTray);

		ea32SetSize(&pchar->pSlotSetBecomeCritter->puiPowerIDs,t);

		// Simple auto slot loop
		for(i=0; i<s; i++)
		{
			Power *ppow = pchar->ppPowers[i];
			PowerDef *pdef = GET_REF(ppow->hDef);
			if(pdef && ppow->eSource==kPowerSource_AttribMod)
			{
				for(j=0; j<t; j++)
				{
					if(!pchar->pSlotSetBecomeCritter->puiPowerIDs[j]
						&& character_PowerTraySlotAllowsPowerDef(pchar, iTray, j, pdef, false))
					{
						pchar->pSlotSetBecomeCritter->puiPowerIDs[j] = ppow->uiID;
						break;
					}
				}
			}
		}

		// If the slot data changed, set the dirty bit
		if(eaiCompare(&puiOld,&pchar->pSlotSetBecomeCritter->puiPowerIDs))
			entity_SetDirtyBit(pchar->pEntParent,parse_Character,pchar,false);
		ea32Destroy(&puiOld);
	}
}


// Automatically fills the Character's PowerSlots with Powers, first-come, first-served,
//  assuming they're not already slotted and not in the earray of pre-existing IDs.
//  Cleans out taken slots if the IDs are no longer valid.
//  will respect PowerDef's DoNotAutoSlot
void character_PowerSlotsAutoSet(int iPartitionIdx,
								 Character *pchar,
								 const U32 *puiIDs,
								 GameAccountDataExtract *pExtract)
{
	int i;
	int iPowIdx, iPowSize;
	int iSlotIdx, iSlotSize;

	if(entIsServer())
		CharacterPowerSlotsBecomeCritterUpdate(pchar);

	if(!pchar->pSlots)
		PowerSlotsCurrent(pchar,true);

	if(!pchar->pSlots)
		return;

	for(i=eaSize(&pchar->pSlots->ppSets)-1; i>=0; i--)
	{
		U32 **ppSlots = PowerSlotsByIndex(pchar,(U32)i);
		if(ppSlots)
		{
			iSlotSize = character_PowerSlotCountInTray(pchar,i);
			if(iSlotSize > eaiSize(ppSlots))
			{
				eaiSetSize(ppSlots, iSlotSize);
			}

			// Clean out bad IDs first
			for(iSlotIdx=0; iSlotIdx < iSlotSize; iSlotIdx++)
			{
				if(*ppSlots && (*ppSlots)[iSlotIdx])
				{
					if(!character_FindPowerByID(pchar,(*ppSlots)[iSlotIdx]))
					{
						// TODO(JW): Hack: Don't have this Power, might have upgraded the rank of a node
						U32 uiID = 0;
						PTNode *pNode = NULL;
						Power *ppow = character_FindPowerByIDTree(pchar,(*ppSlots)[iSlotIdx],NULL,&pNode);
						if(ppow && pNode)
						{
							ppow = powertreenode_GetActivatablePower(pNode);
							if(ppow && ppow->uiID!=(*ppSlots)[iSlotIdx])
							{
								uiID = ppow->uiID;
							}
						}
						// Character doesn't have the Power with that ID anymore, replace with 0 (to free the slot)
						//  or the best ID from the node
						(*ppSlots)[iSlotIdx] = uiID;
					}
				}
			}

			iPowSize = eaSize(&pchar->ppPowers);

			for(iPowIdx=0; iPowIdx < iPowSize; iPowIdx++)
			{
				Power *ppow = pchar->ppPowers[iPowIdx];
				PowerDef *pdef = NULL;
				U32 uiID = pchar->ppPowers[iPowIdx]->uiID;
				int iPowFound;

				iPowFound = ea32Find(&puiIDs,uiID);
				if(iPowFound >= 0)
					continue;	// In our list of IDs to not slot

				iPowFound = ea32Find(ppSlots,uiID);
				if(iPowFound >= 0)
					continue;	// Already slotted

				// Don't slot Powers that are hidden, replacing other Powers, or mirror another Power's PowerSlot state
				if(ppow->bHideInUI || ppow->bIsReplacing || ppow->uiPowerSlotReplacementID)
					continue;
				
				pdef = GET_REF(ppow->hDef);
				// do not allow powers that are set to not auto-slot
				if (pdef && pdef->bDoNotAutoSlot)
					continue;

				// TODO(JW): Hack: Don't auto-slot Powers from AttribMods if bBecomeCritter is true,
				//  since that uses a special tray anyway.  This isn't entirely correct, since the
				//  Power could be from a GrantPower AttribMod instead, but that's not real likely
				//  at the moment.
				if(pchar->bBecomeCritter && ppow->eSource==kPowerSource_AttribMod)
					continue;

				// Go through all the slots to find one this ID can use
				for(iSlotIdx=0; iSlotIdx < iSlotSize; iSlotIdx++)
				{
					if(*ppSlots && (*ppSlots)[iSlotIdx])
					{
						// Slot already taken
						S32 bOccupied = true;
						
						PowerDef *pdefAutoSlotReplace;
						PowerSlot *pPowerSlot = character_PowerSlotGetPowerSlot(pchar,iSlotIdx);
						if(pPowerSlot && (pdefAutoSlotReplace = GET_REF(pPowerSlot->hDefAutoSlotReplace)))
						{
							Power *ppowOccupied = character_FindPowerByID(pchar,(*ppSlots)[iSlotIdx]);
							if(ppowOccupied && pdefAutoSlotReplace==GET_REF(ppowOccupied->hDef))
								bOccupied = false;
						}

						if(bOccupied || !character_PowerSlotIsUnlocked(pchar, pPowerSlot))
							continue;
					}

					if(character_PowerTraySlotSet(iPartitionIdx, pchar, i, iSlotIdx, uiID, false, pExtract, true, false))
						break;
				}
			}
		}
	}
}

// Replaces all instances of the old ID with the new ID.  Used in pre-commit fixup from a respec.
void character_PowerSlotsReplaceID(Character *pchar,
								   U32 uiIDOld,
								   U32 uiIDNew)
{
	int i,j;

	if(!pchar->pSlots)
		return;

	for(i=eaSize(&pchar->pSlots->ppSets)-1; i>=0; i--)
	{
		U32 **ppSlots = PowerSlotsByIndex(pchar,(U32)i);
		if(ppSlots)
		{
			for(j=eaiSize(ppSlots)-1; j>=0; j--)
			{
				if(*ppSlots && (*ppSlots)[j]==uiIDOld)
				{
					(*ppSlots)[j] = uiIDNew;
				}
			}
		}
	}
}



// Returns the specified PowerSlot
PowerSlot *character_PowerSlotGetPowerSlot(const Character *pchar, int iSlot)
{
	PowerSlot **ppSlots = CharacterGetPowerSlotEArray(pchar);
	if(ppSlots)
	{
		return eaGet(&ppSlots,iSlot);
	}
	return NULL;
}

// Indicates whether the power slot is unlocked for the given character
bool character_PowerSlotIsUnlocked(const Character *pChar, SA_PARAM_NN_VALID PowerSlot *pPowerSlot)
{
	if (pPowerSlot->iRequiredLevel > 0)
	{
		Entity *pEnt = pChar->pEntParent;

		if (pEnt)
		{
			// Make sure the combat level is at least equal to the same level defined in the power slot
			return entity_GetCombatLevel(pEnt) >= pPowerSlot->iRequiredLevel;
		}

		return false;
	}

	return true;
}

// Indicates whether the power slot in the given index is unlocked for the given character
bool character_PowerSlotIsUnlockedBySlotInTray(const Character *pChar, S32 iTray, S32 iSlot)
{
	// Get the power slot
	PowerSlot *pPowerSlot = CharacterGetPowerSlotInTrayAtIndex(pChar, iTray, iSlot);

	if (pPowerSlot)
	{
		return character_PowerSlotIsUnlocked(pChar, pPowerSlot);
	}

	return false;
}

// Returns the ID currently set in the specified PowerSlot.
U32 character_PowerSlotGetFromTray(Character *pchar,
									 int iTray,
									 int iSlot)
{
	U32 r = 0;
	PERFINFO_AUTO_START_FUNC();
	if(pchar->bBecomeCritter && 
		(iTray == -1 || (pchar->pSlots && pchar->pSlots->uiIndex == (U32)iTray)) )
	{
		U32 *pSlots = pchar->pSlotSetBecomeCritter ? pchar->pSlotSetBecomeCritter->puiPowerIDs : NULL;
		r = ea32Get(&pSlots,iSlot);
	}
	else if(pchar->pSlots)
	{
		U32 **ppSlots = PowerSlotsByIndex(pchar,iTray >= 0 ? iTray : pchar->pSlots->uiIndex);
		
		if (iTray == -1)
		{
			S32 iSlotStateReplace = CombatPowerStateSwitching_GetSwitchedStatePowerSlot(pchar, iSlot);
			if (iSlotStateReplace >= 0)
				iSlot = iSlotStateReplace;
		}

		if(ppSlots)
		{
			r = ea32Get(ppSlots,iSlot);
		}
	}
	PERFINFO_AUTO_STOP();
	return r;
}

// Sets the index into the EArray of PowerSlot sets on the Character.  Returns if the value changed.
S32 character_PowerSlotSetCurrent(Character *pchar,
								  U32 uiIndex)
{
	if(uiIndex < POWERSLOTSET_MAX)
	{
		// If we're setting to something other than 0, and we don't have the structure, make one
		if(uiIndex > 0 && !pchar->pSlots)
		{
			pchar->pSlots = StructAlloc(parse_CharacterPowerSlots);
			entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
		}

		// If we're setting it to something new, set it and return true
		if(pchar->pSlots && pchar->pSlots->uiIndex != uiIndex)
		{
			pchar->pSlots->uiIndex = uiIndex;
			entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
			return true;
		}
	}
	return false;
}

// internal function that takes in an optional Power* and should be the uiID of the given power
static S32 character_GetPowerSlotInernal(SA_PARAM_NN_VALID Character *pChar, 
										 SA_PARAM_OP_VALID Power *pPow, U32 uiID)
{
	S32 iSlot = -1;
	U32 **ppSlots = PowerSlotsCurrent(pChar,false);
	if(ppSlots)
	{
		iSlot = ea32Find(ppSlots, uiID);

		if(iSlot < 0)
		{
			// I'd really prefer to place this burden upon the caller, since the caller probably already
			//  has a Power*.  However, since it's here now we might as well make use of it until it
			//  gets to slow.
			if (!pPow)
				pPow = character_FindPowerByID(pChar, uiID);

			if (pPow)
			{
				if(pPow->uiPowerSlotReplacementID)
				{
					iSlot = character_GetPowerSlotInernal(pChar, NULL, pPow->uiPowerSlotReplacementID);
				}
				else if (pPow->pCombatPowerStateParent)
				{
					iSlot = character_GetPowerSlotInernal(pChar, NULL, pPow->pCombatPowerStateParent->uiID);
				}
				else if (pPow->bIsReplacing)
				{
					for(iSlot = ea32Size(ppSlots)-1; iSlot >= 0; iSlot--)
					{
						Power *ppowReplaced = character_FindPowerByID(pChar, (*ppSlots)[iSlot]);
						if(ppowReplaced && ppowReplaced->uiReplacementID == uiID)
						{
							break;
						}
					}
				}
			}

		}
	}
	return iSlot;
}

// Returns the PowerSlot index if the Character has the given Power currently slotted, otherwise returns -1
S32 character_GetPowerSlot(Character *pChar, Power *pPow) 
{
	U32 uiPowerID = -1, subIdx = 0;
	S16 linkedSubIdx = 0;
	power_GetIDAndSubIdx(pPow, &uiPowerID, &subIdx, &linkedSubIdx);

	return character_GetPowerSlotInernal(pChar, pPow, uiPowerID);
}

// Returns the PowerSlot index if the Character has the given PowerID currently slotted, otherwise returns -1
S32 character_PowerIDSlot(Character *pchar, U32 uiID)
{
	return character_GetPowerSlotInernal(pchar, NULL, uiID);
}

// Returns the PowerSlot index if the Character has the given PowerID currently slotted in iTray's build, otherwise returns -1
S32 character_PowerTrayIDSlot(	Character *pchar,
								int iTray,
								U32 uiID)
{
	S32 iSlot = -1;
	U32 **ppSlots = PowerSlotsByIndex(pchar,iTray);
	if(ppSlots)
	{
		iSlot = ea32Find(ppSlots,uiID);
		
		if(iSlot < 0)
		{
			Power *ppow = character_FindPowerByID(pchar, uiID);
			if(ppow && ppow->uiPowerSlotReplacementID)
			{
				iSlot = character_GetPowerSlotInernal(pchar, NULL, ppow->uiPowerSlotReplacementID);
			}
			else if(ppow && ppow->bIsReplacing)
			{
				for(iSlot = ea32Size(ppSlots)-1; iSlot >= 0; iSlot--)
				{
					Power *ppowReplaced = character_FindPowerByID(pchar, (*ppSlots)[iSlot]);
					if(ppowReplaced && ppowReplaced->uiReplacementID == uiID)
					{
						break;
					}
				}
			}
		}
	}
	return iSlot;
}

// Returns the "type" of PowerSlot if the Character has the given PowerID currently slotted.  Returns NULL if the
//  PowerID isn't slotted, or the PowerSlot it's in has no "type".
const char *character_PowerIDSlotType(Character *pchar,
									  U32 uiID)
{
	const char *pchType = NULL;
	S32 iSlot = character_GetPowerSlotInernal(pchar,NULL,uiID);
	if(iSlot!=-1)
	{
		PowerSlot **ppPowerSlots = CharacterGetPowerSlotEArray(pchar);
		int s = eaSize(&ppPowerSlots);
		if(iSlot<s)
		{
			pchType = ppPowerSlots[iSlot]->pchType;
		}
	}
	return pchType;
}

// Returns if the Power must be slotted to be used.  Also checks the parent Power if it's a child.
S32 power_SlottingRequired(Power *ppow)
{
	PowerDef *pdef = GET_REF(ppow->hDef);
	if(pdef && pdef->bSlottingRequired)
		return true;

	if(ppow->pParentPower)
	{
		pdef = GET_REF(ppow->pParentPower->hDef);
		if(pdef && pdef->bSlottingRequired)
			return true;
	}
	return false;
}

// Returns true if the Character has the given Power currently slotted.  If slotting is being ignored
//  it always returns true.
S32 character_PowerSlotted(Character *pchar, Power *ppow)
{
	U32 uiPowerID = -1, subIdx = 0;
	S16 linkedSubIdx = 0;

	power_GetIDAndSubIdx(ppow, &uiPowerID, &subIdx, &linkedSubIdx);
	
	return character_PowerIDSlotted(pchar,uiPowerID);
}

// Returns true if the Character has the given PowerID currently slotted.
S32 character_PowerIDSlotted(Character *pchar,
							 U32 uiID)
{
	if(s_bIgnoreSlotting)
	{
		return true;
	}
	else
	{
		if(pchar->bBecomeCritter)
			return pchar->pSlotSetBecomeCritter ? (ea32Find(&pchar->pSlotSetBecomeCritter->puiPowerIDs,uiID) >= 0) : false;
		else
			return(character_GetPowerSlotInernal(pchar, NULL, uiID) >= 0);
	}
	return false;
}

// Fills the passed in earray with the PowerIDs that are currently slotted, in order.  May contain 0s.
void character_PowerIDsSlottedByIndex(Character *pchar,
							   U32 **ppuiIDs,
							   U32 uiIndex)
{
	U32 **ppSlots = PowerSlotsByIndex(pchar,uiIndex);
	if(ppSlots)
	{
		ea32Copy(ppuiIDs,ppSlots);
	}
	else
	{
		ea32SetSize(ppuiIDs,character_PowerSlotCountInTray(pchar,uiIndex));
	}
}

// Returns the number of PowerSlots in the Character's current PowerSlotSet
S32 character_PowerSlotCount(SA_PARAM_NN_VALID const Character *pchar)
{
	PowerSlot **ppPowerSlots = CharacterGetPowerSlotEArray(pchar);
	return eaSize(&ppPowerSlots);
}

// Returns the number of PowerSlots in the Character's PowerSlotSet
S32 character_PowerSlotCountInTray(SA_PARAM_NN_VALID const Character *pchar, int iTray)
{
	PowerSlot **ppPowerSlots = CharacterGetPowerSlotEArrayInTray(pchar, iTray >= 0 ? iTray : pchar->pSlots->uiIndex);
	return eaSize(&ppPowerSlots);
}

// Returns whether the character is eligible to make changes to the power tray - checks AL, powermodes and cooldowns.
bool character_CanModifyPowerTray(SA_PARAM_NN_VALID Character *pChar, int iTray, int iSlot, bool bNotifyIfFalse)
{
	if (entGetAccessLevel(pChar->pEntParent) >= 9)
		return true;
	
	if (g_PowerSlotsConfig.eModeToDisableSlotting && 
		character_HasMode(pChar, g_PowerSlotsConfig.eModeToDisableSlotting))
	{

#if defined(GAMESERVER) || defined(GAMECLIENT)
		if (bNotifyIfFalse)
		{
			notify_NotifySend(pChar->pEntParent, kNotifyType_PowerSlottingError_SlotIsLocked, entTranslateMessageKey(pChar->pEntParent, "Powers.SlottingDisabledError.PowerMode"), NULL, NULL);
		}
#endif
		return false;
	}

	if (g_PowerSlotsConfig.eRequiredModeForSlotting &&
		!character_HasMode(pChar, g_PowerSlotsConfig.eRequiredModeForSlotting))
	{
#if defined(GAMESERVER) || defined(GAMECLIENT)
		if (bNotifyIfFalse)
		{
			notify_NotifySend(pChar->pEntParent, kNotifyType_PowerSlottingError_SlotIsLocked, entTranslateMessageKey(pChar->pEntParent, "Powers.SlottingDisabledError.PowerMode"), NULL, NULL);
		}
#endif
		return false;
	}

	return true;
}

static void PowerSlotsConfigLoadInternal(const char *pchPath, S32 iWhen)
{
	loadstart_printf("Loading PowerSlotsConfig...");

	StructReset(parse_PowerSlotsConfig, &g_PowerSlotsConfig);

	ParserLoadFiles(NULL, 
		"defs/config/PowerSlotsConfig.def", 
		"PowerSlotsConfig.bin", 
		PARSER_OPTIONALFLAG, 
		parse_PowerSlotsConfig,
		&g_PowerSlotsConfig);

	loadend_printf(" done.");
}

static void PowerSlotsConfigLoad(void)
{
	PowerSlotsConfigLoadInternal(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/PowerSlotsConfig.def", PowerSlotsConfigLoadInternal);
}

AUTO_STARTUP(PowerSlots)  ASTRT_DEPS(PowerCategories, PowerModes);
void PowerSlotsLoad(void)
{
	char *pchSharedMemory = NULL;

	switch( GetAppGlobalType() ) {
		case GLOBALTYPE_UGCSEARCHMANAGER: case GLOBALTYPE_UGCDATAMANAGER:
			return;
	}

	// Load the config
	PowerSlotsConfigLoad();

	loadstart_printf("Loading %s..","PowerSlots");
	MakeSharedMemoryName("PowerSlots.bin",&pchSharedMemory);
	ParserLoadFilesShared(pchSharedMemory,NULL,"defs/config/PowerSlots.def","PowerSlots.bin",PARSER_OPTIONALFLAG,parse_PowerSlotLoadData,&s_Slots);
	estrDestroy(&pchSharedMemory);
	loadend_printf(" done (%d %s).",eaSize(&s_Slots.ppSlots),"PowerSlots");
}

AUTO_COMMAND ACMD_CMDLINE;
void PowersIgnoreSlotting(int ignore)
{
	s_bIgnoreSlotting = ignore;
}

#include "AutoGen/PowerSlots_h_ast.c"
