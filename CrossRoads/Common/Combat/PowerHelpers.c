/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "AutoTransDefs.h"
#include "estring.h"
#include "timing.h"

#include "Character.h"
#include "Character_h_ast.h"
#include "CharacterClass.h"
#include "CostumeCommon.h"
#include "Entity.h"
#include "Entity_h_ast.h"
#include "EntitySavedData.h"
#include "EntitySavedData_h_ast.h"
#include "entCritter.h"
#include "entCritter_h_ast.h"
#include "inventoryCommon.h"
#include "itemCommon.h"
#include "itemCommon_h_ast.h"
#include "Powers.h"
#include "Powers_h_ast.h"
#include "PowerHelpers.h"
#include "PowerTree.h"
#include "PowerTree_h_ast.h"
#include "species_common.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// Takes an Entity and a PowerDef, and creates a Power for that Entity to
//  use, without actually adding it to the Entity.  It essentially wraps
//  the init and id calls.  Returns the Power created.
AUTO_TRANS_HELPER;
NOCONST(Power) *entity_CreatePowerHelper(ATH_ARG NOCONST(Entity) *pent,
										 PowerDef *pdef,
										 int iLevel)
{
	U32 uiID;
	NOCONST(Power)* ppow = CONTAINER_NOCONST(Power, power_Create(pdef->pchName));
	power_InitHelper(ppow,iLevel);
	uiID = entity_GetNewPowerIDHelper(pent);
	power_SetIDHelper(ppow,uiID);
	return ppow;
}


// Takes a new power, initializes its transacted data with proper values, and does 
//  generally useful stuff.  Does NOT set the ID or add it to Character or anything
//  of that sort.  Return false if the Power already has already been initialized.
AUTO_TRANS_HELPER;
int power_InitHelper(ATH_ARG NOCONST(Power) *ppow, int iLevel)
{
	if(verify(!ppow->iLevel && !ppow->uiTimeCreated))
	{
		ppow->iLevel = iLevel;
		ppow->uiTimeCreated = timeSecondsSince2000();
		// TODO(JW): Power Creation: Set up lifetimes
		return true;
	}
	return false;
}

// Return the next valid Power ID, which is unique across the entity and any of its pets/siblings
AUTO_TRANS_HELPER;
U32 entity_GetNewPowerIDHelper(ATH_ARG NOCONST(Entity) *pent)
{
	U32 uiID = 0;
	U32 uiIDBase, uiIDEnt = 0, uiIDType = POWERID_TYPE_MAIN;

	// Get the base value
	uiIDBase = pent->pChar->uiPowerIDMax + 1;

	if(NONNULL(pent->pSaved))
	{
		if(pent->pSaved->iPetID != 0)
		{
			// We know it's a saved pet
			uiIDEnt = pent->pSaved->iPetID;
			uiIDType = POWERID_TYPE_SAVEDPET;
		}
		else if(NONNULL(pent->pSaved->pPuppetMaster))
		{
			// It's a main entity, but it may still be playing as a puppet (in which case we still want to
			//  set it up as a saved pet).
			ContainerID cidCur;
			int i;

			if(cidCur = pent->pSaved->pPuppetMaster->curID)
			{
				if(pent->pSaved->pPuppetMaster->curType == GLOBALTYPE_ENTITYSAVEDPET)
				{
					for(i=eaSize(&pent->pSaved->ppOwnedContainers)-1; i>=0; i--)
					{
						if(pent->pSaved->ppOwnedContainers[i]->conID==cidCur)
						{
							// Found a matching SavedPet which your PuppetMaster struct thinks you currently are
							uiIDEnt = pent->pSaved->ppOwnedContainers[i]->uiPetID;
							uiIDType = POWERID_TYPE_SAVEDPET;
							break;
						}
					}
				}
			}
			if(cidCur = pent->pSaved->pPuppetMaster->curTempID) //This is a temp puppet
			{
				for(i=eaSize(&pent->pSaved->ppAllowedCritterPets)-1;i>=0;i--)
				{
					U32 petID = pent->pSaved->ppAllowedCritterPets[i]->uiPetID;

					if(petID = cidCur)
					{
						uiIDEnt = petID + POWERID_SAVEDPET_TEMPPUPPET;
						uiIDType = POWERID_TYPE_SAVEDPET;
					}
				}
			}
		}
	}

	// If everything is in legal range, generate the full ID and increment the base ID on the Character
	if(verify(uiIDType <= ((1<<POWERID_TYPE_BITS)-1))
		&& verify(uiIDEnt <= ((1<<POWERID_ENT_BITS)-1))
		&& verify(uiIDBase <= ((1<<POWERID_BASE_BITS)-1)))
	{
		uiID = POWERID_CREATE(uiIDBase,uiIDEnt,uiIDType);
		pent->pChar->uiPowerIDMax += 1;
	}

	return uiID;
}

// Gets the next PowerID for a Temporary Power.  None of this data is transacted.  Only use for
//  Powers that aren't transacted.  Values are totally transient and will change after a mapmove.
U32 character_GetNewTempPowerID(Character *pchar)
{
	U32 uiID = 0;
	U32 uiIDBase, uiIDEnt = 0, uiIDType = POWERID_TYPE_TEMP;

	// Get the base value
	uiIDBase = pchar->uiTempPowerIDMax + 1;

	// Could get a custom value for the uiIDEnt, but it's almost certainly not necessary.  Easy to fix
	//  if it becomes necessary (e.g. SavedPet Entity gets a temp Power that is propagated to your main Entity).

	// If everything is in legal range, generate the full ID and increment the base ID on the Character
	if(verify(uiIDType <= ((1<<POWERID_TYPE_BITS)-1))
		&& verify(uiIDEnt <= ((1<<POWERID_ENT_BITS)-1))
		&& verify(uiIDBase <= ((1<<POWERID_BASE_BITS)-1)))
	{
		uiID = POWERID_CREATE(uiIDBase,uiIDEnt,uiIDType);
		pchar->uiTempPowerIDMax += 1;
	}

	return uiID;
}


// Sets the Power's ID.  Totally bad things can happen if you call this at a bad time
//  or with a bad value (anything not from character_GetNewPowerIDHelper()).
AUTO_TRANS_HELPER;
void power_SetIDHelper(ATH_ARG NOCONST(Power) *ppow, U32 uiID)
{
	ppow->uiID = uiID;
}

// Sets the Power's level
AUTO_TRANS_HELPER;
void power_SetLevelHelper(ATH_ARG NOCONST(Power) *ppow, int iLevel)
{
	ppow->iLevel = iLevel;
}

// Sets the Power's hue
AUTO_TRANS_HELPER;
void power_SetHueHelper(ATH_ARG NOCONST(Power) *ppow, F32 fHue)
{
	ppow->fHue = fHue;
}

// Sets the Power's emit.  If the passed in value is NULL or not a valid emit name, the emit is cleared.
AUTO_TRANS_HELPER;
void power_SetEmitHelper(ATH_ARG NOCONST(Power) *ppow, const char *cpchEmit)
{
	REMOVE_HANDLE(ppow->hEmit);
	if(cpchEmit && RefSystem_ReferentFromString(g_hPowerEmitDict,cpchEmit))
	{
		SET_HANDLE_FROM_STRING(g_hPowerEmitDict,cpchEmit,ppow->hEmit);
	}
}

// Sets the Power's EntCreateCostume
AUTO_TRANS_HELPER;
void power_SetEntCreateCostumeHelper(ATH_ARG NOCONST(Power) *ppow, S32 iEntCreateCostume)
{
	ppow->iEntCreateCostume = iEntCreateCostume;
}

// Sets the Power's used charges
AUTO_TRANS_HELPER;
void power_SetChargesUsedHelper(ATH_ARG NOCONST(Power) *ppow, int iChargesUsed)
{
	ppow->iChargesUsedTransact = MAX(0, iChargesUsed);
}

// Fixes up the list of Powers granted by the Entity's Character's Class
AUTO_TRANS_HELPER;
void entity_FixPowersClassHelper(ATH_ARG NOCONST(Entity) *pent)
{
	CharacterClass *pclass = GET_REF(pent->pChar->hClass);
	CharacterClassPower **ppPowers = pclass ? pclass->ppPowers : NULL;

	int i, j, s = eaSize(&ppPowers);

	if(s==0)
	{
		eaClearStructNoConst(&pent->pChar->ppPowersClass,parse_Power);
	}

	// Clean out existing bad Powers
	for(j=eaSize(&pent->pChar->ppPowersClass)-1; j>=0; j--)
	{
		PowerDef *pdef = pent->pChar->ppPowersClass[j] ? GET_REF(pent->pChar->ppPowersClass[j]->hDef) : NULL;

		if(!pdef)
		{
			StructDestroyNoConst(parse_Power, eaRemove(&pent->pChar->ppPowersClass,j));
			continue;
		}

		for(i=s-1; i>=0; i--)
		{
			PowerDef *pdefClass = GET_REF(ppPowers[i]->hdef);
			if(pdefClass==pdef)
				break;
		}

		if(i<0)
		{
			StructDestroyNoConst(parse_Power, eaRemove(&pent->pChar->ppPowersClass,j));	
			continue;
		}
	}

	// Add any missing Powers
	for(i=s-1; i>=0; i--)
	{
		PowerDef *pdefClass = GET_REF(ppPowers[i]->hdef);
		if(pdefClass)
		{
			for(j=eaSize(&pent->pChar->ppPowersClass)-1; j>=0; j--)
			{
				assert(pent->pChar->ppPowersClass[j]); // To keep analysis happy
				if(pdefClass==GET_REF(pent->pChar->ppPowersClass[j]->hDef))
					break;
			}

			if(j<0)
			{
				NOCONST(Power) *ppow = entity_CreatePowerHelper(pent,pdefClass,0);
				eaIndexedEnableNoConst(&pent->pChar->ppPowersClass, parse_Power);
				eaIndexedAdd(&pent->pChar->ppPowersClass,ppow);
			}
		}
	}
}

AUTO_TRANS_HELPER_SIMPLE;
static S32 PowerIDsMaskCheck(U32 uiSourceMask, U32 uiID)
{
	if(uiSourceMask > 0 && (uiID & uiSourceMask) == uiSourceMask)
		return true;

	if(uiSourceMask == 0 && (uiID & ~((1<<POWERID_BASE_BITS)-1)) == 0)
		return true;

	return false;
}

// Resets the PowerIDs on the Powers on the Entity.  If uiSourceMask is (U32)-1 (or any value with
//  bits set in the base bits range), then all Powers have their PowerIDs reset.  If uiSourceMask
//  only contains bits in the source bits range (or is 0), then only Powers with matching source bits
//  in the PowerIDs are reset.
// Returns the number of Powers that had their PowerIDs reset.
AUTO_TRANS_HELPER;
S32 entity_ResetPowerIDsHelper(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, U32 uiSourceMask, GameAccountDataExtract *pExtract, bool bJustCount)
{
	int i,j,k;
	S32 iReset = 0;
	S32 bIgnoreSourceMask = false;

	if(ISNULL(pEnt->pChar))
		return iReset;

	if(uiSourceMask & ((1<<POWERID_BASE_BITS)-1))
	{
		bIgnoreSourceMask = true;
	}

	// Fix all Powers in the personal list
	for(i=0; i<eaSize(&pEnt->pChar->ppPowersPersonal); i++)
	{
		if(bIgnoreSourceMask || PowerIDsMaskCheck(uiSourceMask,pEnt->pChar->ppPowersPersonal[i]->uiID))
		{
			if(!bJustCount)
				pEnt->pChar->ppPowersPersonal[i]->uiID = entity_GetNewPowerIDHelper(pEnt);
			iReset++;
		}
	}

	// Fix all Powers in the class list
	for(i=0; i<eaSize(&pEnt->pChar->ppPowersClass); i++)
	{
		if(bIgnoreSourceMask || PowerIDsMaskCheck(uiSourceMask,pEnt->pChar->ppPowersClass[i]->uiID))
		{
			if(!bJustCount)
				pEnt->pChar->ppPowersClass[i]->uiID = entity_GetNewPowerIDHelper(pEnt);
			iReset++;
		}
	}

	// Fix all Powers in the species list
	for(i=0; i<eaSize(&pEnt->pChar->ppPowersSpecies); i++)
	{
		if(bIgnoreSourceMask || PowerIDsMaskCheck(uiSourceMask,pEnt->pChar->ppPowersSpecies[i]->uiID))
		{
			if(!bJustCount)
				pEnt->pChar->ppPowersSpecies[i]->uiID = entity_GetNewPowerIDHelper(pEnt);
			iReset++;
		}
	}

	// Fix all Powers from PowerTrees
	for(i=0; i<eaSize(&pEnt->pChar->ppPowerTrees); i++)
	{
		for(j=0; j<eaSize(&pEnt->pChar->ppPowerTrees[i]->ppNodes); j++)
		{
			for(k=0; k<eaSize(&pEnt->pChar->ppPowerTrees[i]->ppNodes[j]->ppPowers); k++)
			{
				if(bIgnoreSourceMask || PowerIDsMaskCheck(uiSourceMask,pEnt->pChar->ppPowerTrees[i]->ppNodes[j]->ppPowers[k]->uiID))
				{
					if(!bJustCount)
						pEnt->pChar->ppPowerTrees[i]->ppNodes[j]->ppPowers[k]->uiID = entity_GetNewPowerIDHelper(pEnt);
					iReset++;
				}
			}
		}
	}

	// Fix all Powers from Items
	if(NONNULL(pEnt->pInventoryV2))
	{
		for(i=0; i<eaSize(&pEnt->pInventoryV2->ppInventoryBags); i++)
		{
			BagIterator *iter = invbag_trh_IteratorFromEnt(ATR_PASS_ARGS, pEnt,pEnt->pInventoryV2->ppInventoryBags[i]->BagID, pExtract);
			for(; !bagiterator_Stopped(iter); bagiterator_Next(iter))
			{
				NOCONST(Item) *pItem = bagiterator_GetItem(iter);
				if(pItem)	// check any item for powers as they might need fixup
				{
					for(k=0; k<eaSize(&pItem->ppPowers); k++)
					{
						if(bIgnoreSourceMask || PowerIDsMaskCheck(uiSourceMask,pItem->ppPowers[k]->uiID))
						{
							if(!bJustCount)
								pItem->ppPowers[k]->uiID = entity_GetNewPowerIDHelper(pEnt);
							iReset++;
						}
					}
				}
			}
			bagiterator_Destroy(iter);
		}
	}

	if(bIgnoreSourceMask)
	{
		TRANSACTION_APPEND_LOG_SUCCESS("%s reset all %d PowerIDs",pEnt->debugName,iReset);
	}
	else
	{
		TRANSACTION_APPEND_LOG_SUCCESS("%s reset %d PowerIDs from source %d",pEnt->debugName,iReset,uiSourceMask);
	}

	return iReset;
}

// Resets the uiPowerIDMax and all PowerIDs on the Powers on the Entity.
// Returns the number of Powers that had their PowerIDs reset.
AUTO_TRANS_HELPER;
S32 entity_ResetPowerIDsAllHelper(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, GameAccountDataExtract *pExtract)
{
	S32 iReset = 0;

	if(ISNULL(pEnt->pChar))
		return iReset;

	pEnt->pChar->uiPowerIDMax = 0;
	iReset = entity_ResetPowerIDsHelper(ATR_PASS_ARGS, pEnt, -1, pExtract, false);

	return iReset;
}
