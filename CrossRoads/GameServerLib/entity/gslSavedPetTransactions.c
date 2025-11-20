/***************************************************************************
*     Copyright (c) 2006-2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "SavedPetTransactions.h"
#include "objTransactions.h"
#include "gslTransactions.h"
#include "inventoryCommon.h"
#include "gslSavedPet.h"
#include "SavedPetCommon.h"
#include "Character.h"
#include "CharacterClass.h"
#include "Entity.h"
#include "EntityLib.h"
#include "EntitySavedData.h"
#include "Team.h"
#include "gslCritter.h"
#include "gslEntity.h"
#include "entCritter.h"
#include "TeamPetsCommonStructs.h"
#include "AutoTransDefs.h"
#include "MapDescription.h"
#include "Player.h"
#include "Powers.h"
#include "PowerHelpers.h"
#include "qsortG.h"
#include "ItemCommon.h"
#include "itemCommon_h_ast.h"
#include "ItemEnums.h"
#include "itemEnums_h_ast.h"

#include "inventoryCommon.h"
#include "CostumeCommon.h"
#include "CostumeCommonEntity.h"
#include "CostumeCommonLoad.h"
#include "nemesis_common.h"

#include "LoginCommon.h"

#include "Character_h_ast.h"
#include "AutoGen/Entity_h_ast.h"
#include "EntitySavedData_h_ast.h"
#include "AutoGen/entCritter_h_ast.h"
#include "AutoGen/SavedPetCommon_h_ast.h"
#include "AutoGen/nemesis_common_h_ast.h"
#include "Player_h_ast.h"

#include "AutoGen/inventoryCommon_h_ast.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"

// Always propagation slots

static NOCONST(AlwaysPropSlot) *APS_NewAlwaysPropSlot(AlwaysPropSlotDef *pDef, U32 uiSlotID, U32 uiPuppetID)
{
	NOCONST(AlwaysPropSlot) *pSlot = StructCreateNoConst(parse_AlwaysPropSlot);

	SET_HANDLE_FROM_STRING("AlwaysPropSlotDef",pDef->pchName,pSlot->hDef);
	pSlot->iSlotID = uiSlotID;
	pSlot->iPuppetID = uiPuppetID;

	return pSlot;
}

AUTO_TRANS_HELPER;
U32 Entity_FixupFindActiveSpacePuppetID(ATH_ARG NOCONST(Entity) *pEnt)
{
	static U32 eSpace = 0; 
	int i;
	if (!eSpace)
		eSpace = StaticDefineIntGetInt(CharClassTypesEnum, "Space");

	for (i = 0; i < eaSize(&pEnt->pSaved->pPuppetMaster->ppPuppets); i++)
	{
		NOCONST(PuppetEntity) *pPuppetEntity = pEnt->pSaved->pPuppetMaster->ppPuppets[i];
		if (pPuppetEntity->eState == PUPPETSTATE_ACTIVE && pPuppetEntity->eType == eSpace)
		{
			return pPuppetEntity->curID;
		}
	}
	return 0;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, ".Psaved.Ppalwayspropslots, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Pppuppets");
bool Entity_FixupAlwaysProps(ATH_ARG NOCONST(Entity) *pEnt, AlwaysPropSlotDefRefs* pPropSlotRefs)
{
	int i, j;
	NOCONST(AlwaysPropSlot)** eaFixupSlots = NULL;
	U32 uiActivePuppetID = 0;

	// Remove any slots that are in the entity's list but not the new list
	for (i = eaSize(&pEnt->pSaved->ppAlwaysPropSlots)-1; i >= 0; i--)
	{
		NOCONST(AlwaysPropSlot)* pSlot = pEnt->pSaved->ppAlwaysPropSlots[i];
		AlwaysPropSlotDef* pSlotDef = GET_REF(pSlot->hDef);
		bool bValid = (pSlot->iSlotID > 0);
		bool bFound = false;

		if (pSlot->iPuppetID == 0)
		{
			if (!uiActivePuppetID)
				uiActivePuppetID = Entity_FixupFindActiveSpacePuppetID(pEnt);
			pSlot->iPuppetID = uiActivePuppetID;
		}

		if (bValid)
		{
			for (j = i-1; j >= 0; j--)
			{
				NOCONST(AlwaysPropSlot)* pCheckSlot = pEnt->pSaved->ppAlwaysPropSlots[j];
				if (pCheckSlot->iSlotID == pSlot->iSlotID)
				{
					bValid = false;
					break;
				}
			}
		}
		for (j = eaSize(&pPropSlotRefs->eaRefs)-1; j >= 0; j--)
		{
			AlwaysPropSlotDef* pDef = GET_REF(pPropSlotRefs->eaRefs[j]->hDef);
			if (pDef && pDef == pSlotDef && pPropSlotRefs->eaRefs[j]->uiPuppetID == pSlot->iPuppetID)
			{
				bFound = true;
				break;
			}
		}
		if (!bValid || !bFound)
		{
			pSlot = eaRemove(&pEnt->pSaved->ppAlwaysPropSlots, i);
			if (!bFound)
			{
				StructDestroyNoConst(parse_AlwaysPropSlot, pSlot);
			}
			else
			{
				eaPush(&eaFixupSlots, pSlot);
			}
		}
	}

	// Fixup the entity's slots so that the entity has the correct count of each slot
	for (i = 0; i < eaSize(&pPropSlotRefs->eaRefs); i++)
	{
		AlwaysPropSlotDefRef* pRef = pPropSlotRefs->eaRefs[i];
		AlwaysPropSlotDef* pDef = GET_REF(pRef->hDef);
		S32 iCount = 0;
		
		if (!pDef)
			continue;

		for (j = eaSize(&pEnt->pSaved->ppAlwaysPropSlots)-1; j >= 0; j--)
		{
			AlwaysPropSlotDef* pSlotDef = GET_REF(pEnt->pSaved->ppAlwaysPropSlots[j]->hDef);
			if (pSlotDef == pDef
				&& pEnt->pSaved->ppAlwaysPropSlots[j]->iPuppetID == pRef->uiPuppetID)
			{
				if (++iCount > pRef->iCount)
				{
					StructDestroyNoConst(parse_AlwaysPropSlot, eaRemove(&pEnt->pSaved->ppAlwaysPropSlots, j));
				}
			}
		}

		while (iCount < pRef->iCount)
		{
			NOCONST(AlwaysPropSlot)* pNewSlot;
			S32 iSlotCount = eaSize(&pEnt->pSaved->ppAlwaysPropSlots);
			U32 uiSlotID = iSlotCount + 1;
			for (j = 0; j < iSlotCount; j++)
			{
				U32 uiIndex = j + 1;
				if (uiIndex != pEnt->pSaved->ppAlwaysPropSlots[j]->iSlotID)
				{
					uiSlotID = uiIndex;
					break;
				}
			}
			pNewSlot = APS_NewAlwaysPropSlot(pDef,uiSlotID,pPropSlotRefs->eaRefs[i]->uiPuppetID);
			eaInsert(&pEnt->pSaved->ppAlwaysPropSlots, pNewSlot, j);
			
			// If possible, reuse the PetID from a fixup slot
			for (j = eaSize(&eaFixupSlots)-1; j >= 0; j--)
			{
				if (pDef == GET_REF(eaFixupSlots[j]->hDef)
					&& eaFixupSlots[j]->iPuppetID == pRef->uiPuppetID)
				{
					pNewSlot->iPetID = eaFixupSlots[j]->iPetID;
					StructDestroyNoConst(parse_AlwaysPropSlot, eaRemoveFast(&eaFixupSlots, j));
					break;
				}
			}
			iCount++;
		}
	}

	// Make sure that all of the pets referenced by always prop slots exist and are valid
	for (i = eaSize(&pEnt->pSaved->ppAlwaysPropSlots)-1; i >= 0; i--)
	{
		NOCONST(AlwaysPropSlot)* pPropSlot = pEnt->pSaved->ppAlwaysPropSlots[i];

		if (!pPropSlot->iPetID)
			continue;

		for (j = eaSize(&pEnt->pSaved->ppOwnedContainers)-1; j >= 0; j--)
		{
			NOCONST(PetRelationship)* pPet = pEnt->pSaved->ppOwnedContainers[j];
			if (pPet->uiPetID == pPropSlot->iPetID)
			{
				break;
			}
		}
		if (j < 0)
		{
			pPropSlot->iPetID = 0;
		}
	}

	eaDestroyStructNoConst(&eaFixupSlots, parse_AlwaysPropSlot);
	return true;
}

AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Psaved.Ppalwayspropslots, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Pppuppets");
enumTransactionOutcome trEntity_FixupAlwaysProps(ATR_ARGS, NOCONST(Entity)* pEnt, AlwaysPropSlotDefRefs* pPropSlotRefs)
{
	if(NONNULL(pEnt->pSaved) && Entity_FixupAlwaysProps(pEnt, pPropSlotRefs))
	{
		TRANSACTION_RETURN_LOG_SUCCESS("SUCCESS %s Fixed up AlwaysPropSlots (%d Slots)",pEnt->debugName,eaSize(&pEnt->pSaved->ppAlwaysPropSlots));
	}
	TRANSACTION_RETURN_LOG_FAILURE("FAILED %s could not fix up AlwaysPropSlots",pEnt->debugName);
}

// Puppet transformation

AUTO_TRANS_HELPER
	ATR_LOCKS(pEntity, ".Psaved.Ppuppetmaster.Pptemppuppets");
void Entity_RemoveTempPuppetsByType(ATH_ARG NOCONST(Entity) *pEntity, CharClassTypes eType, U32 uiIDExclude)
{
	int i;

	for(i=eaSize(&pEntity->pSaved->pPuppetMaster->ppTempPuppets)-1;i>=0;i--)
	{
		NOCONST(TempPuppetEntity) *pTempPuppet = pEntity->pSaved->pPuppetMaster->ppTempPuppets[i];
		PetDef *pPetDef = GET_REF(pTempPuppet->hPetDef);
		CharClassTypes eClassType = petdef_GetCharacterClassType(pPetDef);

		if(pPetDef && eClassType != eType)
			continue;

		if((U32)pTempPuppet->uiID == uiIDExclude)
			continue;

		eaRemove(&pEntity->pSaved->pPuppetMaster->ppTempPuppets,i);
		StructDestroyNoConst(parse_TempPuppetEntity,pTempPuppet);
	}
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pMaster, ".Pchar, .Psaved, .Costumeref, .Pinventoryv2, .Itemidmax, pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]")
	ATR_LOCKS(pOldPuppet, ".Pchar, .Psaved, .Costumeref, .Pinventoryv2, .Itemidmax")
	ATR_LOCKS(pPuppet, ".Pchar, .Psaved, .Costumeref, .Pinventoryv2");
bool Entity_TransformToPuppet(ATR_ARGS, 
							  ATH_ARG NOCONST(Entity) *pMaster,
							  ATH_ARG NOCONST(Entity) *pOldPuppet, 
							  ATH_ARG NOCONST(Entity) *pPuppet, 
							  NON_CONTAINER CharacterPreSaveInfo *pPreSaveInfo,
							  GameAccountDataExtract *pExtract)
{
	U32 uiPowerMask = 0;
	//First thing is to place the master in his place
	if(ISNULL(pMaster->pSaved) || ISNULL(pMaster->pSaved->pPuppetMaster))
		return false;

	if(NONNULL(pPuppet) && 
		pMaster->pSaved->pPuppetMaster->curID == pPuppet->myContainerID
		&& pMaster->pSaved->pPuppetMaster->curType == pPuppet->myEntityType)
	{
		return false;
	}

	if(ISNULL(pPuppet) &&
		pMaster->pSaved->pPuppetMaster->curID == pMaster->myContainerID
		&& pMaster->pSaved->pPuppetMaster->curType == pMaster->myEntityType)
	{
		return false;
	}

	if(NONNULL(pOldPuppet))
	{
		trCharacterPreSave(ATR_PASS_ARGS,pMaster,pPreSaveInfo);
		Entity_PuppetCopy(pMaster,pOldPuppet);
	}
	else if(pMaster->pSaved->pPuppetMaster->curTempID)
	{
		int i;
		for(i=eaSize(&pMaster->pSaved->pPuppetMaster->ppTempPuppets)-1;i>=0;i--)
		{
			NOCONST(TempPuppetEntity)* pCurrTempPuppet = pMaster->pSaved->pPuppetMaster->ppTempPuppets[i];
			if((ContainerID)pCurrTempPuppet->uiID == pMaster->pSaved->pPuppetMaster->curTempID)
			{
				uiPowerMask = POWERID_CREATE(0,POWERID_SAVEDPET_TEMPPUPPET + pCurrTempPuppet->uiID,POWERID_TYPE_SAVEDPET);
				trhTempPuppetPreSave(pCurrTempPuppet, pPreSaveInfo);
				break;
			}
		}
	}

	if(NONNULL(pPuppet))
	{
		Entity_PuppetCopyEx(pPuppet,pMaster,true);
		pMaster->pSaved->pPuppetMaster->curID = pPuppet->myContainerID;
		pMaster->pSaved->pPuppetMaster->curType = pPuppet->myEntityType;
		pMaster->pSaved->pPuppetMaster->curTempID = 0;
		if (uiPowerMask)
			entity_ResetPowerIDsHelper(ATR_PASS_ARGS,pMaster,uiPowerMask,pExtract,false);
	}

	{
		CharacterClass *pClass = GET_REF(pMaster->pChar->hClass);
		if(pClass)
			Entity_RemoveTempPuppetsByType(pMaster,pClass->eType,-1);
	}

	pMaster->pSaved->pPuppetMaster->uPuppetSwapVersion++;
	return true;

}

AUTO_TRANS_HELPER
	ATR_LOCKS(pEntity, ".Psaved.Ppuppetmaster.Pptemppuppets, .Psaved.Ppownedcontainers, .Psaved.Ppallowedcritterpets");
NOCONST(TempPuppetEntity) *Entity_NewTempPuppet(ATH_ARG NOCONST(Entity) *pEntity, NON_CONTAINER PetDef *pPetDef)
{
	NOCONST(TempPuppetEntity)* pReturn = StructCreateNoConst(parse_TempPuppetEntity);

	pReturn->uiID = entity_GetNextPetIDHelper(pEntity, 0);
	SET_HANDLE_FROM_REFERENT("PetDef",pPetDef,pReturn->hPetDef);
	eaPush(&pEntity->pSaved->pPuppetMaster->ppTempPuppets,pReturn);
	
	return pReturn;
}

AUTO_TRANS_HELPER;
bool Entity_trh_ValidateTempPuppetInventory(ATR_ARGS, 
											ATH_ARG NOCONST(Entity)* pEnt,
											NON_CONTAINER PetDef* pPetDef)
{
	if (NONNULL(pEnt->pInventoryV2))
	{
		S32 i, iNumBags = eaSize(&pEnt->pInventoryV2->ppInventoryBags);

		for (i = 0; i < iNumBags; i++)
		{
			NOCONST(InventoryBag)* pBag = pEnt->pInventoryV2->ppInventoryBags[i];

			// Check to see if any items were added to a 'NoCopy' bag
			if (invbag_trh_flags(pBag) & InvBagFlag_NoCopy)
			{
				if ((pBag->BagID == InvBagIDs_Numeric && eaSize(&pBag->ppIndexedInventorySlots) > 1) ||
					(pBag->BagID != InvBagIDs_Numeric && !inv_bag_trh_BagEmpty(ATR_PASS_ARGS, pBag)))
				{
					Errorf("Temporary puppet '%s' added default items to a 'NoCopy' bag '%s', which will have no effect.", 
						REF_STRING_FROM_HANDLE(pPetDef->hCritterDef), 
						StaticDefineIntRevLookup(InvBagIDsEnum, pBag->BagID));
					return false;
				}
			}
		}
	}
	return true;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pEntity, ".Pchar, .Psaved, .Costumeref, .Pinventoryv2, .Itemidmax, pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]")
	ATR_LOCKS(pOldPuppet, ".Pchar, .Psaved, .Costumeref, .Pinventoryv2, .Itemidmax");
bool Entity_TransformToTempPuppet(ATR_ARGS,
								  ATH_ARG NOCONST(Entity) *pEntity, 
								  ATH_ARG NOCONST(Entity) *pOldPuppet, 
								  NON_CONTAINER PetDef *pPetDef, 
								  NON_CONTAINER CharacterPreSaveInfo *pPreSaveInfo,
								  GameAccountDataExtract *pExtract)
{
	int i;
	U32 uiPowerMask = 0;
	NOCONST(TempPuppetEntity) *pTempPuppet = NULL;
	NOCONST(Entity) *pCritterEntity = NULL;

	if(ISNULL(pEntity) || ISNULL(pEntity->pSaved) || ISNULL(pEntity->pSaved->pPuppetMaster))
		return false;

	if(!pPetDef)
		return false;

	//Find an existing temp puppet matching this pet def
	for(i=eaSize(&pEntity->pSaved->pPuppetMaster->ppTempPuppets)-1;i>=0;i--)
	{
		if(GET_REF(pEntity->pSaved->pPuppetMaster->ppTempPuppets[i]->hPetDef) == pPetDef)
		{
			pTempPuppet = pEntity->pSaved->pPuppetMaster->ppTempPuppets[i];
			break;
		}
	}

	if(pTempPuppet == NULL)
	{
		pTempPuppet = Entity_NewTempPuppet(pEntity, pPetDef);
	}
	
	Entity_RemoveTempPuppetsByType(pEntity,petdef_GetCharacterClassType(pPetDef),pTempPuppet->uiID);

	{
		CritterCreateParams createParams = {0};
		CritterDef * critterDef;
		createParams.enttype = GLOBALTYPE_ENTITYCRITTER;
		createParams.iPartitionIdx = PARTITION_UNINITIALIZED;
		createParams.iLevel = pEntity->pChar->iLevelCombat;
		createParams.pCostume = GET_REF(pTempPuppet->hCostume);
		createParams.bFakeEntity = true;

		critterDef = GET_REF(pPetDef->hCritterDef);
		if (!critterDef)
			return false;

		pCritterEntity = CONTAINER_NOCONST(Entity, critter_CreateByDef(critterDef, &createParams, pPetDef->pchFilename, true));
		if (!pCritterEntity)
			return false;
	}

	Entity_trh_ValidateTempPuppetInventory(ATR_PASS_ARGS, pCritterEntity, pPetDef);

	if(!pCritterEntity->pSaved)
		pCritterEntity->pSaved = StructCreateNoConst(parse_SavedEntityData);

	if(!pCritterEntity->costumeRef.pStoredCostume && GET_REF(pCritterEntity->costumeRef.hReferencedCostume))
	{
		NOCONST(PlayerCostume) *pRef = GET_REF(pCritterEntity->costumeRef.hReferencedCostume);
		NOCONST(PlayerCostume) *pCostume = StructCreateNoConst(parse_PlayerCostume);
		NOCONST(PlayerCostumeSlot) *pSlot;
		int iSlotID = 0;
		PCSlotType *pSlotType = costumeEntity_trh_GetSlotType(ATR_PASS_ARGS, pCritterEntity, 0, false, &iSlotID);
		StructCopyAllNoConst(parse_PlayerCostume, pRef, pCostume);

		pCritterEntity->pSaved->costumeData.iActiveCostume = 0;
		pSlot = StructCreateNoConst(parse_PlayerCostumeSlot);
		pSlot->pCostume = pCostume;
		pSlot->iSlotID = iSlotID;
		pSlot->pcSlotType = (pSlotType ? pSlotType->pcName : NULL);
		eaPush(&pCritterEntity->pSaved->costumeData.eaCostumeSlots,pSlot);
	}
	else if (!GET_REF(pCritterEntity->costumeRef.hReferencedCostume))
	{
		const char* pchCostumeName = REF_STRING_FROM_HANDLE(pCritterEntity->costumeRef.hReferencedCostume);
		if (!pchCostumeName)
			pchCostumeName = "<Unspecified>";
		ErrorDetailsf("Invalid referenced costume (%s) for CritterDef (%s)", 
			pchCostumeName, REF_STRING_FROM_HANDLE(pPetDef->hCritterDef));
		Errorf("TransformToTempPuppet: Couldn't find a valid costume");
		return false;
	}

	if(NONNULL(pOldPuppet))
	{
		trCharacterPreSave(ATR_PASS_ARGS,pEntity,pPreSaveInfo);
		Entity_PuppetCopy(pEntity,pOldPuppet);
	} 
	else if(pEntity->pSaved->pPuppetMaster->curTempID)
	{
		for(i=eaSize(&pEntity->pSaved->pPuppetMaster->ppTempPuppets)-1;i>=0;i--)
		{
			NOCONST(TempPuppetEntity)* pCurrTempPuppet = pEntity->pSaved->pPuppetMaster->ppTempPuppets[i];
			if((ContainerID)pCurrTempPuppet->uiID == pEntity->pSaved->pPuppetMaster->curTempID)
			{
				uiPowerMask = POWERID_CREATE(0,POWERID_SAVEDPET_TEMPPUPPET + pCurrTempPuppet->uiID,POWERID_TYPE_SAVEDPET);
				trhTempPuppetPreSave(pCurrTempPuppet, pPreSaveInfo);
				break;
			}
		}
	}

	if(pTempPuppet)
	{
		Entity_PuppetCopy(pCritterEntity,pEntity);
		pEntity->pSaved->pPuppetMaster->curTempID = pTempPuppet->uiID;
		pEntity->pSaved->pPuppetMaster->curID = 0;
		pEntity->pSaved->pPuppetMaster->curType = GLOBALTYPE_NONE;
		if (uiPowerMask)
			entity_ResetPowerIDsHelper(ATR_PASS_ARGS,pEntity,uiPowerMask,pExtract,false);
		entity_ResetPowerIDsHelper(ATR_PASS_ARGS,pEntity,POWERID_CREATE(0,0,POWERID_TYPE_MAIN),pExtract,false);
	}

	StructDestroyNoConst(parse_Entity, pCritterEntity);

	pEntity->pSaved->pPuppetMaster->uPuppetSwapVersion++;
	return true;
}

AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Itemidmax, .Pchar, .Psaved, .Costumeref, .Pinventoryv2, pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]")
	ATR_LOCKS(pOldPuppet, ".Pchar, .Psaved, .Costumeref, .Pinventoryv2, .Itemidmax")
	ATR_LOCKS(pPuppet, ".Psaved, .Costumeref, .Pinventoryv2, .Pchar");
enumTransactionOutcome trEntity_TransformToPuppet(ATR_ARGS, 
												  NOCONST(Entity) *pEnt,
												  NOCONST(Entity) *pOldPuppet, 
												  NOCONST(Entity) *pPuppet, 
												  NON_CONTAINER CharacterPreSaveInfo *pPreSaveInfo,
												  GameAccountDataExtract *pExtract)
{
	if (bPetTransactionDebug) {
		printf("%s: Transaction Start: puppet transform transaction (%s)\n", pEnt->debugName, REF_STRING_FROM_HANDLE(pEnt->pChar->hClass));
	}
	if(!Entity_TransformToPuppet(ATR_PASS_ARGS,pEnt,pOldPuppet,pPuppet,pPreSaveInfo,pExtract))
	{
		if (bPetTransactionDebug) {
			printf("%s: Transaction Fail: puppet transform transaction\n", pEnt->debugName);
		}
		TRANSACTION_RETURN_LOG_FAILURE("FAILED to transform to puppet: %s/%s",pEnt->debugName,NONNULL(pPuppet) ? pPuppet->debugName : "[NoPuppet]");
	}

	if (bPetTransactionDebug) {
		printf("%s: Transaction Success: puppet transform transaction (%s)\n", pEnt->debugName, REF_STRING_FROM_HANDLE(pEnt->pChar->hClass));
	}
	TRANSACTION_RETURN_LOG_SUCCESS("SUCCESS %s has transformed to %s",pEnt->debugName,NONNULL(pPuppet) ? pPuppet->debugName : "[NoPuppet]");
}

AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Itemidmax, .Pchar, .Psaved, .Costumeref, .Pinventoryv2, pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]")
	ATR_LOCKS(pOldPuppet, ".Pchar, .Psaved, .Costumeref, .Pinventoryv2, .Itemidmax");
enumTransactionOutcome trEntity_TransformToTempPuppet(ATR_ARGS, 
													  NOCONST(Entity) *pEnt, 
													  NOCONST(Entity) *pOldPuppet, 
													  const char *pchPetDef, 
													  NON_CONTAINER CharacterPreSaveInfo *pPreSaveInfo,
													  GameAccountDataExtract *pExtract)
{
	PetDef *pPetDef = RefSystem_ReferentFromString("PetDef",pchPetDef);
	if (bPetTransactionDebug) {
		printf("%s: Transaction Start: Temp Puppet Transform transaction (%s)\n", pEnt->debugName, REF_STRING_FROM_HANDLE(pEnt->pChar->hClass));
	}
	
	if(!Entity_TransformToTempPuppet(ATR_PASS_ARGS,pEnt,pOldPuppet,pPetDef,pPreSaveInfo,pExtract))
	{
		if (bPetTransactionDebug)
		{
			printf("%s: Transaction Fail: Temp Puppet Transform transaction (%s)\n", pEnt->debugName,pchPetDef);
		}
	}
	
	if(bPetTransactionDebug) {
		printf("%s: Transaction Success: Temp Puppet Transform transaction (%s)\n", pEnt->debugName, REF_STRING_FROM_HANDLE(pEnt->pChar->hClass));
	}
	TRANSACTION_RETURN_LOG_SUCCESS("SUCCESS %s has transformed to temp puppet %s", pEnt->debugName, pchPetDef);
}
