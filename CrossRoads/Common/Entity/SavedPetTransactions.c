/***************************************************************************
*     Copyright (c) 2006-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "SavedPetTransactions.h"
#include "objTransactions.h"
#include "inventoryCommon.h"
#include "SavedPetCommon.h"
#include "Character.h"
#include "CharacterClass.h"
#include "ContinuousBuilderSupport.h"
#include "Powers.h"
#include "PowerHelpers.h"
#include "Entity.h"
#include "EntityLib.h"
#include "EntitySavedData.h"
#include "Team.h"
#include "entCritter.h"
#include "TeamPetsCommonStructs.h"
#include "AutoTransDefs.h"
#include "MapDescription.h"
#include "Player.h"
#include "GlobalTypeEnum.h"
#include "OfficerCommon.h"
#include "qsortG.h"
#include "ItemCommon.h"
#include "itemCommon_h_ast.h"
#include "ItemEnums.h"
#include "itemEnums_h_ast.h"

#include "CostumeCommon.h"
#include "PowerTreeHelpers.h"
#include "nemesis_common.h"

#include "LoginCommon.h"
#include "StringUtil.h"
#include "logging.h"

#include "Character_h_ast.h"
#include "AutoGen/Entity_h_ast.h"
#include "EntitySavedData_h_ast.h"
#include "AutoGen/entCritter_h_ast.h"
#include "AutoGen/SavedPetCommon_h_ast.h"
#include "AutoGen/nemesis_common_h_ast.h"
#include "Player_h_ast.h"

#include "AutoGen/inventoryCommon_h_ast.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"

#ifdef GAMESERVER
#include "Autogen/GameServerLib_autogen_QueuedFuncs.h"
#endif

AUTO_TRANS_HELPER
	ATR_LOCKS(eMasterEntity, ".Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Pppuppets, .Psaved.Ppalwayspropslots")
	ATR_LOCKS(pSavedPet, ".Psaved.Ipetid, .Pchar.Hclass, .Pchar.Pppowerspersonal, .Pchar.Pppowersclass, .Pchar.Pppowersspecies, .Pchar.Pppowertrees")
	ATR_LOCKS(pRelationShip, ".Bteamrequest, .eaPurposes");
bool SavedPet_th_FlagRequestMaxCheck(ATH_ARG NOCONST(Entity) *eMasterEntity, ATH_ARG NOCONST(Entity) *pSavedPet, 
									 ATH_ARG NOCONST(PetRelationship) *pRelationShip, U32 uiPropEntID, bool bTeamRequest, 
									 int iSlotID, S32 ePropCategory, bool bMakeChanges)
{
	bool bReturn = true;
	
	if(ISNULL(eMasterEntity->pSaved))
		return(false);

	//Max pets requesting team request is 1 less than max team members
	if(bTeamRequest)
	{
		int i, iActive=0;

		if(!(pRelationShip->bTeamRequest)) //Already has team memberstatus
		{
			for(i=0;i<eaSize(&eMasterEntity->pSaved->ppOwnedContainers);i++)
			{
				NOCONST(PetRelationship) *pRel = eMasterEntity->pSaved->ppOwnedContainers[i];

				if(pRel->bTeamRequest)
					iActive++;
			}

			if(iActive >= TEAM_MAX_SIZE - 1)
				bReturn = false;
		}

		// Puppets are not allowed to get team request status
		if (NONNULL(eMasterEntity->pSaved->pPuppetMaster))
		{
			for(i=0;i<eaSize(&eMasterEntity->pSaved->pPuppetMaster->ppPuppets);i++)
			{
				if(eMasterEntity->pSaved->pPuppetMaster->ppPuppets[i]->curID == pSavedPet->myContainerID)
				{
					bReturn = false;
				}
			}
		}
	}

	if (uiPropEntID)
	{
		S32 iNumValid = 0;

		if (eaSize(&eMasterEntity->pSaved->ppAlwaysPropSlots))
		{
			S32* piSlots = NULL;

			if (ePropCategory >= 0)
			{
				S32 iSlotIdx = AlwaysPropSlot_trh_FindByPetID(eMasterEntity, pSavedPet->pSaved->iPetID, uiPropEntID, ePropCategory);
				if (iSlotIdx >= 0)
				{
					ea32Push(&piSlots, iSlotIdx);
				}
			}
			else
			{
				AlwaysPropSlot_trh_FindAllByPetID(eMasterEntity, pSavedPet->pSaved->iPetID, uiPropEntID, &piSlots);
			}

			if(iSlotID!=-1)
			{
				S32 iSlotIdx = AlwaysPropSlot_trh_FindBySlotID(eMasterEntity, iSlotID);
				if (iSlotIdx >= 0)
				{
					ea32Push(&piSlots, iSlotIdx);
				}
			}

			if(!piSlots)
			{
				S32 iSlotIdx;
				//Find an open slot
				for(iSlotIdx=eaSize(&eMasterEntity->pSaved->ppAlwaysPropSlots)-1;iSlotIdx>=0;iSlotIdx--)
				{
					NOCONST(AlwaysPropSlot)* pPropSlot = eMasterEntity->pSaved->ppAlwaysPropSlots[iSlotIdx];
					AlwaysPropSlotDef *pSlotDef = GET_REF(pPropSlot->hDef);
					if (!pSlotDef || (ePropCategory >= 0 && pSlotDef->eCategory != ePropCategory))
						continue;
					if(pPropSlot->iPuppetID != uiPropEntID)
						continue;
					if (!pPropSlot->iPetID && SavedPet_th_AlwaysPropSlotCheckRestrictions(pSavedPet, pRelationShip, pSlotDef))
					{
						if (bMakeChanges)
						{
							pPropSlot->iPetID = pSavedPet->pSaved->iPetID;
						}
						iNumValid++;
						break;
					}
				}
			} 
			else 
			{
				S32 i;
				for (i = ea32Size(&piSlots)-1; i >= 0; i--)
				{
					S32 iSlotIdx = piSlots[i];
					NOCONST(AlwaysPropSlot)* pPropSlot = eMasterEntity->pSaved->ppAlwaysPropSlots[iSlotIdx];
					// Succeed if there is a match on the slot, even if slot was previously occupied
					AlwaysPropSlotDef *pSlotDef = GET_REF(pPropSlot->hDef);
					if (!pSlotDef)
						continue;
					if(pPropSlot->iPuppetID != uiPropEntID)
						continue;
					if (SavedPet_th_AlwaysPropSlotCheckRestrictions(pSavedPet, pRelationShip, pSlotDef))
					{
						if(bMakeChanges)
						{
							pPropSlot->iPetID = pSavedPet->pSaved->iPetID;
						}
						iNumValid++;
					}
					else if (bMakeChanges && pPropSlot->iPetID == pSavedPet->pSaved->iPetID)
					{
						pPropSlot->iPetID = 0;
					}
				}
				ea32Destroy(&piSlots);
			}
		}

		if (iNumValid==0)
			bReturn = false;
	}

	if (bMakeChanges)
	{
		
	}

	return bReturn;
}


// Returns the next valid PetID for the Owner Entity.  Optionally excludes
//  one specific Pet Entity from that review by ContainerID, in order to allow
//  (but not require) an existing Pet to re-use its value if it's already in the
//  Owner's list.
AUTO_TRANS_HELPER
	ATR_LOCKS(pEntOwner, ".Psaved.Ppownedcontainers, .Psaved.Ppallowedcritterpets, .Psaved.Ppuppetmaster.Pptemppuppets");
U32 entity_GetNextPetIDHelper(ATH_ARG NOCONST(Entity) *pEntOwner, ContainerID cidExclude)
{
	int i;
	U32 *puiIDsUsed = NULL;
	U32 uiID = 1;

	// Collect IDs from the Owned Containers
	for(i=eaSize(&pEntOwner->pSaved->ppOwnedContainers)-1; i>=0; i--)
	{
		ContainerID cidOwned = pEntOwner->pSaved->ppOwnedContainers[i]->conID;
		if(cidOwned==cidExclude)
			continue;

		ea32PushUnique(&puiIDsUsed,pEntOwner->pSaved->ppOwnedContainers[i]->uiPetID);
	}

	// Collect IDs from the "Allowed Critter Pets"
	for(i=eaSize(&pEntOwner->pSaved->ppAllowedCritterPets)-1; i>=0; i--)
	{
		ea32PushUnique(&puiIDsUsed,pEntOwner->pSaved->ppAllowedCritterPets[i]->uiPetID);
	}

	for(i=0;i<eaSize(&pEntOwner->pSaved->pPuppetMaster->ppTempPuppets);i++)
	{
		ea32PushUnique(&puiIDsUsed,pEntOwner->pSaved->pPuppetMaster->ppTempPuppets[i]->uiID);
	}

	ea32QSort(puiIDsUsed,cmpU32);

	// Find the next available ID
	for(i=0; i<ea32Size(&puiIDsUsed); i++)
	{
		if(uiID < puiIDsUsed[i])
			break;

		uiID = puiIDsUsed[i] + 1;
	}

	return uiID;
}


// Returns a new earray filled with all the Entity's OwnderContainers ContainerIDs
ContainerID* entity_GetOwnedContainerIDsArray(Entity *pEntOwner)
{
	int i;
	ContainerID *pcidOwned = NULL;
	for(i=0; i<eaSize(&pEntOwner->pSaved->ppOwnedContainers); i++)
	{
		ContainerID cidOwned = pEntOwner->pSaved->ppOwnedContainers[i]->conID;
		if(cidOwned)
			ea32Push(&pcidOwned,cidOwned);
	}
	return pcidOwned;
}


AUTO_TRANSACTION
	ATR_LOCKS(pOwner, "pInventoryV2.Peaowneduniqueitems, pInventoryV2.ppLiteBags, .Psaved.Ppallowedcritterpets, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Ppownedcontainers, .pInventoryV2.Ppinventorybags, .Psaved.Conowner.Containertype, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Pchar.Ilevelexp, .Psaved.Ppuppetmaster.Pptemppuppets");
enumTransactionOutcome trAddAllowedCritterPet(ATR_ARGS, NOCONST(Entity) *pOwner, PetDef *pPetDef, U64 uiItemID, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	NOCONST(PetDefRefCont) *newPetDef;
	int i;
	bool bDupe = false;

	if(ISNULL(pOwner) || ISNULL(pOwner->pSaved))
		TRANSACTION_RETURN_LOG_FAILURE("Invalid Entity for trAddAllowedCritterPet");
	
	if (gConf.bDiscardDuplicateCritterPets)
	{
		for(i=0;i<eaSize(&pOwner->pSaved->ppAllowedCritterPets);i++)
		{
			if(stricmp(REF_STRING_FROM_HANDLE(pOwner->pSaved->ppAllowedCritterPets[i]->hPet), pPetDef->pchPetName) == 0)
			{
				bDupe = true;
				break;
			}
		}
	}

	if (bDupe && !gConf.bDiscardDuplicateCritterPets)
		TRANSACTION_RETURN_LOG_FAILURE("Cannot add new Allowed Critter Pet %s as it already exisits on character %s[%d]",pPetDef->pchPetName,GlobalTypeToName(pOwner->myEntityType),pOwner->myContainerID);

	if (!bDupe)
	{
		newPetDef = StructCreateNoConst(parse_PetDefRefCont);
		SET_HANDLE_FROM_STRING(g_hPetStoreDict,pPetDef->pchPetName,newPetDef->hPet);
		newPetDef->uiPetID = entity_GetNextPetIDHelper(pOwner,0);
		eaPush(&pOwner->pSaved->ppAllowedCritterPets,newPetDef);
	}

	if(uiItemID)
	{
		S32 bFailed = false;
		if(!inv_RemoveItemByID(ATR_PASS_ARGS,pOwner,uiItemID,1,ItemAdd_UseOverflow,pReason,pExtract))
		{
			bFailed = true;
		}

		if(bFailed)
		{
			TRANSACTION_APPEND_LOG_FAILURE("%s[%d] Unable to remove item by id %"FORM_LL"d",
				GlobalTypeToName(pOwner->myEntityType), pOwner->myContainerID, uiItemID);
			return false;
		}
	}

	if (bDupe && gConf.bDiscardDuplicateCritterPets)
	{
		TRANSACTION_RETURN_LOG_SUCCESS("Discarded critter pet %s since it already exists on %s[%d]",pPetDef->pchPetName,GlobalTypeToName(pOwner->myEntityType),pOwner->myContainerID);
	}
	else
	{
		TRANSACTION_RETURN_LOG_SUCCESS("Added Allowed Critter Pet %s to %s[%d]",pPetDef->pchPetName,GlobalTypeToName(pOwner->myEntityType),pOwner->myContainerID);
	}
}

AUTO_TRANS_HELPER
	ATR_LOCKS(ent, ".Psaved.Ppuppetmaster.Pppuppetrequests");
bool Entity_ClearPuppetRequest(ATH_ARG NOCONST(Entity) *ent, const char *pchType, const char *pchName)
{
	int i;

	if(!pchType || !pchName)
		return false;

	for(i=eaSize(&ent->pSaved->pPuppetMaster->ppPuppetRequests)-1;i>=0;i--)
	{
		NOCONST(PuppetRequest) *pRequest = ent->pSaved->pPuppetMaster->ppPuppetRequests[i];
		if(strcmp(pchType,pRequest->pchType) == 0 && strcmp(pchName,pRequest->pchName) == 0)
		{
			StructDestroyNoConst(parse_PuppetRequest, eaRemove(&ent->pSaved->pPuppetMaster->ppPuppetRequests,i));
			return true;
		}
	}

	return false;
}

AUTO_TRANSACTION
	ATR_LOCKS(ent, ".Psaved.Ppuppetmaster.Pppuppetrequests");
enumTransactionOutcome trEntity_ClearPuppetRequest(ATR_ARGS, NOCONST(Entity) *ent, const char *pchType, const char *pchName)
{
	if(!Entity_ClearPuppetRequest(ent,pchType,pchName))
		TRANSACTION_RETURN_LOG_FAILURE("FAILED to remove puppet from request list: %s",ent->debugName);

	TRANSACTION_RETURN_LOG_SUCCESS("SUCCESS puppet request list is cleared for ent: %s",ent->debugName);
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pMaster, ".Psaved.Ppuppetmaster.Pppuppets, .Psaved.Ppuppetmaster.Pppuppetrequests")
	ATR_LOCKS(pPuppet, ".Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Hclass");
bool Entity_AddPuppet(ATR_ARGS, ATH_ARG NOCONST(Entity) *pMaster, ATH_ARG NOCONST(Entity) *pPuppet, const char *pchType, const char *pchName, int bMakeActive)
{
	NOCONST(PuppetEntity) *pNewPuppet = NULL;
	CharacterClass *pClass = GET_REF(pPuppet->pChar->hClass);
	CharClassCategorySet *pSet = CharClassCategorySet_getCategorySetFromClass(pClass);

	if(ISNULL(pMaster->pSaved) || ISNULL(pMaster->pSaved->pPuppetMaster))
	{
		return false;
	}

	if (pPuppet->pSaved->conOwner.containerID != 0 && 
		(pPuppet->pSaved->conOwner.containerID != pMaster->myContainerID || pPuppet->pSaved->conOwner.containerType != pMaster->myEntityType))
	{
		TRANSACTION_RETURN_LOG_FAILURE("Can't make %s[%d] a puppet of %s[%d], already owned by %s[%d]", 
			GlobalTypeToName(pPuppet->myEntityType), pPuppet->myContainerID, 
			GlobalTypeToName(pMaster->myEntityType), pMaster->myContainerID,
			GlobalTypeToName(pPuppet->pSaved->conOwner.containerType),pPuppet->pSaved->conOwner.containerID);
	}

	objSetDebugName(pPuppet->debugName, MAX_NAME_LEN,
		pPuppet->myEntityType,
		pPuppet->myContainerID, 0, NULL, NULL);


	pPuppet->pSaved->conOwner.containerID = pMaster->myContainerID;
	pPuppet->pSaved->conOwner.containerType = pMaster->myEntityType;

	pNewPuppet = StructCreateNoConst(parse_PuppetEntity);

	pNewPuppet->eType = pClass ? (U32)pClass->eType : 0;
	pNewPuppet->eState = bMakeActive ? PUPPETSTATE_ACTIVE : PUPPETSTATE_OFFLINE;

	pNewPuppet->curType = pPuppet->myEntityType;
	pNewPuppet->curID = pPuppet->myContainerID;

	eaPush(&pMaster->pSaved->pPuppetMaster->ppPuppets,pNewPuppet);

	Entity_ClearPuppetRequest(pMaster,pchType,pchName);

	return true;
}

AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Psaved.Ppuppetmaster.Pppuppetrequests, .Psaved.Ppuppetmaster.Pppuppets")
	ATR_LOCKS(pPuppet, ".Pchar.Hclass, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype");
enumTransactionOutcome trEntity_AddPuppet(ATR_ARGS, NOCONST(Entity) *pEnt, NOCONST(Entity) *pPuppet, const char *pchType, const char *pchName, int bMakeActive)
{
	if(!Entity_AddPuppet(ATR_PASS_ARGS, pEnt, pPuppet, pchType, pchName, bMakeActive))
	{
		TRANSACTION_RETURN_LOG_FAILURE("FAILED to add puppet to master: %s/%s",pEnt->debugName,NONNULL(pPuppet) ? pPuppet->debugName : "Master Puppet")
	}

	TRANSACTION_RETURN_LOG_SUCCESS("SUCCESS %s is now a puppet of %s",NONNULL(pPuppet) ? pPuppet->debugName : "Master Puppet", pEnt->debugName);
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pOwner, ".Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, .Psaved.Ppownedcontainers, .Pinventoryv2.Pplitebags, .Pplayer.Publicaccountname, .Hallegiance, .Hsuballegiance, .Psaved.Ppuppetmaster.Pppuppets, .Psaved.Ppallowedcritterpets, .Psaved.Ppalwayspropslots, .Psaved.Ppuppetmaster.Pppuppetrequests, .Pinventoryv2.Ppinventorybags, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Ppuppetmaster.Pptemppuppets")
	ATR_LOCKS(pEntSrc, ".Pinventoryv2.Ppinventorybags, .Psaved.Conowner.Containertype, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Pinventoryv2.Peaowneduniqueitems, .Pinventoryv2.Pplitebags, .Psaved.Conowner.Containerid")
	ATR_LOCKS(pPet, ".Psaved.Pscpdata, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Psaved.Ipetid, .Pchar, .Pcritter.Petdef, .Psaved.Savedname, .Pinventoryv2.Ppinventorybags, .Pinventoryv2.Pplitebags, .Pinventoryv2.Peaowneduniqueitems, .Itemidmax, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Pplayer.Playertype");
bool trhAddSavedPet(ATR_ARGS, ATH_ARG NOCONST(Entity)* pOwner, ATH_ARG NOCONST(Entity)* pEntSrc, 
					ATH_ARG NOCONST(Entity)* pPet, int eRelationship, int eState, U32 **peaPropEntIDs, bool bTeamRequest, U64 uiItemId, bool bMakeActive,
					const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	NOCONST(PetRelationship) *conRelation;
	int i;
	bool bFound = false;
	PetDef *pPetDef = NONNULL(pPet->pCritter) ? GET_REF(pPet->pCritter->petDef) : NULL;
	CritterDef *pPetCritterDef = pPetDef ? GET_REF(pPetDef->hCritterDef) : NULL;
	U32 uiPetID;

	if (pPet->pSaved->conOwner.containerID != 0 && 
		(pPet->pSaved->conOwner.containerID != pOwner->myContainerID || pPet->pSaved->conOwner.containerType != pOwner->myEntityType))
	{
		TRANSACTION_APPEND_LOG_FAILURE("Can't make %s[%d] a child of %s[%d], already owned by %s[%d]", 
			GlobalTypeToName(pPet->myEntityType), pPet->myContainerID, 
			GlobalTypeToName(pOwner->myEntityType), pOwner->myContainerID,
			GlobalTypeToName(pPet->pSaved->conOwner.containerType),pPet->pSaved->conOwner.containerID);
		return false;
	}

	if(pPetDef  && ((!pPetDef->bCanBePuppet && !trhOfficer_CanAddOfficer(pOwner, NULL, pExtract))
				|| (pPetDef->bCanBePuppet && pPetCritterDef && !trhEntity_CanAddPuppet(pOwner, pPetCritterDef->pchClass, pExtract))))
	{
		TRANSACTION_APPEND_LOG_FAILURE("error: Destination entity %s@%s cannot have any more pets",pOwner->debugName,pOwner->pPlayer->publicAccountName);
		return false;
	}

	pPet->pSaved->conOwner.containerType = pOwner->myEntityType;
	pPet->pSaved->conOwner.containerID = pOwner->myContainerID;
	uiPetID = entity_GetNextPetIDHelper(pOwner,pPet->myContainerID);
	pPet->pSaved->iPetID = uiPetID;

	for (i = 0; i < eaSize(&pOwner->pSaved->ppOwnedContainers); i++)
	{
		conRelation = pOwner->pSaved->ppOwnedContainers[i];
		if (conRelation->conID == pPet->myContainerID)
		{
			conRelation->eRelationship = eRelationship;
			conRelation->eState = eState;
			conRelation->uiPetID = uiPetID;
			bFound = true;
		}
		else if (eRelationship == CONRELATION_PRIMARY_PET && conRelation->eRelationship == CONRELATION_PRIMARY_PET)
		{
			conRelation->eRelationship = CONRELATION_PET;
		}
	}

	if (bFound)
	{
		// Make sure that the pet's inventory is valid
		inv_trh_FixupInventory(ATR_PASS_ARGS, pPet, true, pReason);
		// Reset all the Pet's PowerID data to ensure that they're all unique
		entity_ResetPowerIDsAllHelper(ATR_PASS_ARGS, pPet, pExtract);

		TRANSACTION_RETURN_LOG_SUCCESS("%s[%d] now a child of %s[%d]", 
			GlobalTypeToName(pPet->myEntityType), pPet->myContainerID, 
			GlobalTypeToName(pOwner->myEntityType), pOwner->myContainerID);
		return true;
	}

	conRelation = StructCreateNoConst(parse_PetRelationship);
	conRelation->conID = pPet->myContainerID;
	conRelation->eRelationship = eRelationship;
	conRelation->eState = eState;
	conRelation->uiPetID = uiPetID;

	conRelation->bTeamRequest = false;

	if(NONNULL(pPet->pCritter) && NONNULL(pPet->pChar))
	{
		if(bTeamRequest && SavedPet_th_FlagRequestMaxCheck(pOwner,pPet,conRelation,0,true,-1,-1,true))
			conRelation->bTeamRequest = true;
		for (i=0; i < ea32Size(peaPropEntIDs); i++)
		{
			U32 uiPropEntID = (*peaPropEntIDs)[i];
			SavedPet_th_FlagRequestMaxCheck(pOwner,pPet,conRelation,uiPropEntID ,false,-1,kAlwaysPropSlotCategory_Default,true);
		}
	}
	
	if(!conRelation->bTeamRequest && conRelation->eState == OWNEDSTATE_ACTIVE) //Nothing passed, mark as offline
		conRelation->eState = OWNEDSTATE_OFFLINE;

	if(pPetDef && pPetDef->bCanBePuppet)
	{
		if(!Entity_AddPuppet(ATR_PASS_ARGS,pOwner,pPet,pPetDef->pchPetName,pPet->pSaved->savedName,bMakeActive)) {
			TRANSACTION_APPEND_LOG_FAILURE("%s[%d] Unable to add pet as a puppet (%s)", GlobalTypeToName(pPet->myEntityType), pPet->myContainerID, pPet->debugName);
			return false;
		}
	}

	eaPush(&pOwner->pSaved->ppOwnedContainers, conRelation);

	if(uiItemId)
	{
		S32 bFailed = false;
		if(NONNULL(pEntSrc))
		{
			if(!inv_RemoveItemByID(ATR_PASS_ARGS,pEntSrc,uiItemId,1,ItemAdd_UseOverflow,pReason,pExtract))
			{
				bFailed = true;
			}
		}
		else if(!inv_RemoveItemByID(ATR_PASS_ARGS,pOwner,uiItemId,1,ItemAdd_UseOverflow,pReason,pExtract))
		{
			bFailed = true;
		}

		if(bFailed)
		{
			TRANSACTION_APPEND_LOG_FAILURE("%s[%d] Unable to remove item by id %"FORM_LL"d",
				GlobalTypeToName(pPet->myEntityType), pPet->myContainerID, uiItemId);
			return false;
		}
	}

	// Change the pet's species based on the owner's allegiance if there is a SpeciesChange for this pet's species
	entity_trh_AllegianceSpeciesChange(ATR_PASS_ARGS, pPet, GET_REF(pOwner->hAllegiance));
	// Make sure that the pet's inventory is valid
	inv_trh_FixupInventory(ATR_PASS_ARGS, pPet, true, pReason);
	// Reset all the Pet's PowerID data to ensure that they're all unique
	entity_ResetPowerIDsAllHelper(ATR_PASS_ARGS, pPet, pExtract);

	TRANSACTION_APPEND_LOG_SUCCESS("%s[%d] now a child of %s[%d]", 
		GlobalTypeToName(pPet->myEntityType), pPet->myContainerID, 
		GlobalTypeToName(pOwner->myEntityType), pOwner->myContainerID);
	return true;
}

AUTO_TRANSACTION
ATR_LOCKS(pOwner, ".Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Psaved.Ppownedcontainers, .pInventoryV2.Peaowneduniqueitems, .Pplayer.Publicaccountname, .Hallegiance, .Hsuballegiance, .Psaved.Ppuppetmaster.Pppuppets, .Psaved.Ppallowedcritterpets, .Psaved.Ppalwayspropslots, .Psaved.Ppuppetmaster.Pppuppetrequests, .pInventoryV2.Ppinventorybags, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, pInventoryV2.ppLiteBags, .Psaved.Ppuppetmaster.Pptemppuppets")
ATR_LOCKS(pEntSrc, ".pInventoryV2.Ppinventorybags, .Psaved.Conowner.Containerid, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containertype, .pInventoryV2.Peaowneduniqueitems, pInventoryV2.ppLiteBags")
ATR_LOCKS(pPet, ".Psaved.Pscpdata, .Pchar, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Psaved.Ipetid, .Pcritter.Petdef, .Psaved.Savedname, .pInventoryV2.Ppinventorybags, .pInventoryV2.Pplitebags, .pInventoryV2.Peaowneduniqueitems, .Itemidmax, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Pplayer.Playertype");
enumTransactionOutcome trAddSavedPet(ATR_ARGS, NOCONST(Entity)* pOwner, NOCONST(Entity)* pEntSrc, NOCONST(Entity)* pPet, 
									 int eRelationship, int eState, PropEntIDs *pPropEntIDs, int bTeamRequest, U64 uiItemId, int bMakeActive,
									 const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	if(!trhAddSavedPet(ATR_PASS_ARGS, pOwner, pEntSrc, pPet, eRelationship, eState, &pPropEntIDs->eauiPropEntIDs, bTeamRequest, uiItemId, bMakeActive, pReason, pExtract))
	{
		TRANSACTION_RETURN_LOG_FAILURE("FAILED to add saved pet: %s[%d] to owner: %s[%d]",
			GlobalTypeToName(pPet->myEntityType), pPet->myContainerID, GlobalTypeToName(pOwner->myEntityType), pOwner->myContainerID);
	}

	TRANSACTION_RETURN_LOG_SUCCESS("%s[%d] now a child of %s[%d]", 
		GlobalTypeToName(pPet->myEntityType), pPet->myContainerID, 
		GlobalTypeToName(pOwner->myEntityType), pOwner->myContainerID);
}


AUTO_TRANS_HELPER
ATR_LOCKS(pDstEnt, ".Psaved.Conowner.Containerid, .Pplayer.Publicaccountname, .Psaved.Conowner.Containertype, .pInventoryV2.Peaowneduniqueitems, .Psaved.Ppownedcontainers, .Hallegiance, .Hsuballegiance, .Psaved.Ppuppetmaster.Pppuppets, .Psaved.Ppallowedcritterpets, .Psaved.Ppalwayspropslots, .Psaved.Ppuppetmaster.Pppuppetrequests, .pInventoryV2.Ppinventorybags, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, pInventoryV2.ppLiteBags, .Psaved.Ppuppetmaster.Pptemppuppets")
ATR_LOCKS(pItem, ".Pspecialprops.Pcontainerinfo.Hsavedpet, .Hitem")
ATR_LOCKS(eaContainerItemPets, ".Psaved.Pscpdata, .Pchar, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Psaved.Ipetid, .Pcritter.Petdef, .Psaved.Savedname, .pInventoryV2.Ppinventorybags, .pInventoryV2.Pplitebags, .pInventoryV2.Peaowneduniqueitems, .Itemidmax, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Pplayer.Playertype");
bool trhAddSavedPetFromContainerItem(ATR_ARGS, ATH_ARG NOCONST(Entity)* pDstEnt, ATH_ARG NOCONST(Item)* pItem, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaContainerItemPets, int bMakeActive, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	ContainerID iPetID = 0;
	ItemDef* itemDef = NULL;

	if(ISNULL(pDstEnt->pSaved))
	{
		TRANSACTION_APPEND_LOG_FAILURE("error: Destination entity %s@%s are not capable of saved pet transfers",
			pDstEnt->debugName,pDstEnt->pPlayer->publicAccountName);
		return false;
	}

	if(ISNULL(pItem) || ISNULL(pItem->pSpecialProps) || ISNULL(pItem->pSpecialProps->pContainerInfo))
	{
		TRANSACTION_APPEND_LOG_FAILURE("error: Invalid container item");
		return false;
	}

	iPetID = StringToContainerID(REF_STRING_FROM_HANDLE(pItem->pSpecialProps->pContainerInfo->hSavedPet));
	itemDef = GET_REF(pItem->hItem);

	if(itemDef && itemDef->eType == kItemType_Container)
	{
		if(NONNULL(eaContainerItemPets))
		{
			int iPetIdx;
			for(iPetIdx = 0; iPetIdx < eaSize(&eaContainerItemPets); iPetIdx++)
			{
				if(iPetID == eaContainerItemPets[iPetIdx]->myContainerID)
				{
					bool retval;
					U32 *eaPropEntIDs = NULL;
					int i;
					for (i = 0; i < eaSize(&pDstEnt->pSaved->pPuppetMaster->ppPuppets); i++)
					{
						NOCONST(PuppetEntity) *pPuppet = pDstEnt->pSaved->pPuppetMaster->ppPuppets[i];
						if (pPuppet->eState == PUPPETSTATE_ACTIVE)
						{
							ea32Push(&eaPropEntIDs, pPuppet->curID);
						}
					}
					retval = trhAddSavedPet(ATR_PASS_ARGS, pDstEnt, NULL, eaContainerItemPets[iPetIdx], CONRELATION_PRIMARY_PET, OWNEDSTATE_ACTIVE, &eaPropEntIDs, true, 0, bMakeActive, pReason, pExtract);
					ea32Destroy(&eaPropEntIDs);
					return retval;
				}
			}
		}
	}

	TRANSACTION_APPEND_LOG_FAILURE("error: Unable to get pet entity from container item. ID: %i", iPetID);
	return false;
}

AUTO_TRANSACTION
	ATR_LOCKS(pOwner, ".Pplayer.Nemesisinfo.Eanemesisstates, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Psaved.Ppownedcontainers, .pInventoryV2.Peaowneduniqueitems, .Pplayer.Publicaccountname, .Hallegiance, .Hsuballegiance, .Psaved.Ppuppetmaster.Pppuppets, .Psaved.Ppallowedcritterpets, .Psaved.Ppalwayspropslots, .Psaved.Ppuppetmaster.Pppuppetrequests, .pInventoryV2.Ppinventorybags, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, pInventoryV2.ppLiteBags, .Psaved.Ppuppetmaster.Pptemppuppets")
	ATR_LOCKS(pPet, ".Psaved.Pscpdata, .Pchar, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Psaved.Ipetid, .Pcritter.Petdef, .Psaved.Savedname, .pInventoryV2.Ppinventorybags, .pInventoryV2.Pplitebags, .pInventoryV2.Peaowneduniqueitems, .Itemidmax, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Pplayer.Playertype");
enumTransactionOutcome trAddNemesisSavedPet(ATR_ARGS, NOCONST(Entity)* pOwner, NOCONST(Entity)* pPet, int eRelationship, int eState, PropEntIDs *pPropEntIDs, int bTeamRequest, int eNemesisState, ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	NOCONST(PlayerNemesisState)* pNewNemesisState = NULL;
	int i;

	if (!(eNemesisState > NemesisState_None && eNemesisState < NemesisState_Max)){
		TRANSACTION_RETURN_LOG_FAILURE("Invalid Nemesis state %d", eState);
	}

	// Players may only have MAX_NEMESIS_COUNT Nemeses at a time
	if (eaSize(&pOwner->pPlayer->nemesisInfo.eaNemesisStates) >= MAX_NEMESIS_COUNT){
		TRANSACTION_RETURN_LOG_FAILURE("Player tried to make a Nemesis, but already has too many.");
	}

	// Add the saved pet as normal
	if (trAddSavedPet(ATR_PASS_ARGS, pOwner, NULL, pPet, eRelationship, eState, pPropEntIDs, bTeamRequest, 0, 0, pReason, pExtract) != TRANSACTION_OUTCOME_SUCCESS){
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// Make sure Nemesis does not already exist on player
	if (eaIndexedGetUsingInt(&pOwner->pPlayer->nemesisInfo.eaNemesisStates, pPet->myContainerID)){
		TRANSACTION_RETURN_LOG_SUCCESS("Nemesis already exists, doing nothing");
	}

	// Make sure player does not already have a primary nemesis
	if (eNemesisState == NemesisState_Primary){
		for (i = eaSize(&pOwner->pPlayer->nemesisInfo.eaNemesisStates)-1; i >= 0; --i){
			if (pOwner->pPlayer->nemesisInfo.eaNemesisStates[i]->eState == NemesisState_Primary){
				TRANSACTION_RETURN_LOG_FAILURE("Can't add Nemesis: Player already has primary nemesis");
			}
		}
	}
	
	// Add Nemesis info
	pNewNemesisState = StructCreateNoConst(parse_PlayerNemesisState);
	pNewNemesisState->iNemesisID = pPet->myContainerID;
	pNewNemesisState->eState = eNemesisState;
	eaIndexedAdd(&pOwner->pPlayer->nemesisInfo.eaNemesisStates, pNewNemesisState);

	// Send Events
	QueueRemoteCommand_eventsend_RemoteRecordNemesisState(ATR_RESULT_SUCCESS, 0, 0, pNewNemesisState->iNemesisID, NemesisState_Created);
	QueueRemoteCommand_eventsend_RemoteRecordNemesisState(ATR_RESULT_SUCCESS, 0, 0, pNewNemesisState->iNemesisID, pNewNemesisState->eState);

	TRANSACTION_RETURN_LOG_SUCCESS("Nemesis successfully added");
}

AUTO_TRANS_HELPER
	ATR_LOCKS(ent, ".Pchar, .Pinventoryv2");
void Entity_MakeMasterCopy(ATH_ARG NOCONST(Entity) *ent)
{
	ent->pChar = StructCreateNoConst(parse_Character);
	ent->pInventoryV2 = StructCreateNoConst(parse_Inventory);
}

AUTO_TRANS_HELPER
	ATR_LOCKS(ent, ".Psaved, .Pplayer.Accesslevel, .Pplayer.AccountaccessLevel, .Hallegiance, .Hsuballegiance");
bool Entity_AddPuppetCreateRequest(ATH_ARG NOCONST(Entity) *ent, LoginPuppetInfo* pInfo)
{
	NOCONST(SavedEntityData) *pSavedData = ent->pSaved;
	S32 i;

	if(ISNULL(pSavedData) || ISNULL(pSavedData->pPuppetMaster) || ISNULL(pInfo->pchType))
	{
		return false;
	}

	if (SAFE_MEMBER2(ent, pPlayer, accessLevel) != ACCESS_DEBUG && 
		SAFE_MEMBER2(ent, pPlayer, accountAccessLevel) != ACCESS_DEBUG &&
		!g_isContinuousBuilder)
	{
		if (eaSize(&pSavedData->pPuppetMaster->ppPuppetRequests) >= g_PetRestrictions.iRequiredPuppetRequestCount)
		{
			return false;
		}

		for (i = eaSize(&g_PetRestrictions.eaAllowedPuppetRequests)-1; i >= 0; i--)
		{
			PuppetRequestChoice* pChoice = g_PetRestrictions.eaAllowedPuppetRequests[i];
			if (pChoice->pcAllegiance && (stricmp(pChoice->pcAllegiance, REF_STRING_FROM_HANDLE(ent->hAllegiance)) != 0) &&
				(stricmp(pChoice->pcAllegiance, REF_STRING_FROM_HANDLE(ent->hSubAllegiance)) != 0))
			{
				continue;
			}
			if (stricmp(pInfo->pchType, pChoice->pcCritterDef) == 0)
			{
				break;
			}
		}
		if (i < 0)
		{
			return false;
		}
	}

	if ((!pInfo->pchName) || StringIsInvalidCommonName(pInfo->pchName, SAFE_MEMBER2(ent, pPlayer, accessLevel)))
	{
		return false;
	}

	{
		NOCONST(PuppetRequest)* pRequest = StructCreateNoConst( parse_PuppetRequest );

		pRequest->pchName = pInfo->pchName ? StructAllocString( pInfo->pchName ) : NULL;
		pRequest->pchType = StructAllocString( pInfo->pchType );

		eaPush( &pSavedData->pPuppetMaster->ppPuppetRequests, pRequest );
	}

	return true;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(ent, ".Psaved, .Pplayer.Accesslevel, .Pplayer.AccountaccessLevel, .Hallegiance, .Hsuballegiance");
bool Entity_AddPetCreateRequest(ATH_ARG NOCONST(Entity) *ent, LoginPetInfo* pInfo)
{
	NOCONST(SavedEntityData) *pSavedData = ent->pSaved;
	S32 i;

	if(ISNULL(pSavedData) || ISNULL(pInfo->pchType))
	{
		return false;
	}

	if (SAFE_MEMBER2(ent, pPlayer, accessLevel) != ACCESS_DEBUG && 
		SAFE_MEMBER2(ent, pPlayer, accountAccessLevel) != ACCESS_DEBUG &&
		!g_isContinuousBuilder && (!pInfo->pPetDef || !pInfo->pPetDef->bAutoGrant))
	{
		if (eaSize(&pSavedData->ppPetRequests) >= g_PetRestrictions.iRequiredPetRequestCount)
		{
			return false;
		}
		for (i = eaSize(&g_PetRestrictions.eaAllowedPetRequests)-1; i >= 0; i--)
		{
			PetRequestChoice* pChoice = g_PetRestrictions.eaAllowedPetRequests[i];
			if (pChoice->pcAllegiance && (stricmp(pChoice->pcAllegiance, REF_STRING_FROM_HANDLE(ent->hAllegiance)) != 0) && 
				(stricmp(pChoice->pcAllegiance, REF_STRING_FROM_HANDLE(ent->hSubAllegiance)) != 0))
			{
				continue;
			}
			if (stricmp(pInfo->pchType, pChoice->pcPetDef)==0)
			{
				break;
			}
		}
		if (i < 0)
		{
			return false;
		}
	}

	if ((!pInfo->pchName) || StringIsInvalidCommonName(pInfo->pchName, SAFE_MEMBER2(ent, pPlayer, accessLevel)))
	{
		return false;
	}

	{
		NOCONST(PetRequest)* pRequest = StructCreateNoConst( parse_PetRequest );

		pRequest->pchName = pInfo->pchName ? StructAllocString( pInfo->pchName ) : NULL;
		pRequest->pchType = StructAllocString( pInfo->pchType );

		if ( pInfo->pConstCostume )
		{
			pRequest->pCostume = StructCreateNoConst( parse_PlayerCostume );
			StructCopyDeConst( parse_PlayerCostume, pInfo->pConstCostume, pRequest->pCostume, 0, 0, 0 ); 
		}

		eaPush( &pSavedData->ppPetRequests, pRequest );
	}

	return true;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, ".Psaved.Pppetrequests");
bool Entity_ClearPetRequests(ATH_ARG NOCONST(Entity) *pEnt)
{
	if(ISNULL(pEnt->pSaved))
	{
		return false;
	}

	eaClearStructNoConst( &pEnt->pSaved->ppPetRequests, parse_PetRequest );
	//eaClear(&pSavedData->ppchPetRequests);

	return true;
}

AUTO_TRANSACTION
	ATR_LOCKS(ent, ".Psaved.Pppetrequests");
enumTransactionOutcome trEntity_ClearPetRequests(ATR_ARGS, NOCONST(Entity) *ent)
{
	if(!Entity_ClearPetRequests(ent))
		TRANSACTION_RETURN_LOG_FAILURE("FAILED to clear puppet request list: %s",ent->debugName);

	TRANSACTION_RETURN_LOG_SUCCESS("SUCCESS puppet request list is cleared for ent: %s",ent->debugName);
}

AUTO_TRANS_HELPER
	ATR_LOCKS(ent, ".Psaved.Ppuppetmaster");
bool Entity_MakePuppetMaster(ATH_ARG NOCONST(Entity) *ent)
{
	if(ISNULL(ent->pSaved) || !ISNULL(ent->pSaved->pPuppetMaster))
	{
		return false;
	}

	ent->pSaved->pPuppetMaster = StructCreateNoConst(parse_PuppetMaster);
	ent->pSaved->pPuppetMaster->curID = ent->myContainerID;
	ent->pSaved->pPuppetMaster->curType = ent->myEntityType;

	//pSavedData->pPuppetMaster->pMasterCopy = StructCreate(parse_Entity);
	//Entity_MakeMasterCopy(pSavedData->pPuppetMaster->pMasterCopy);
	//Entity_AddPuppet(ATR_EMPTY_ARGS,ent,ent);

	return true;
}

AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curid")
	ATR_LOCKS(pPuppet, ".Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype");
enumTransactionOutcome trEntity_SetCurrentPuppetID(ATR_ARGS, NOCONST(Entity) *pEnt, NOCONST(Entity) *pPuppet)
{
	if(!ISNULL(pEnt->pSaved) && !ISNULL(pEnt->pSaved->pPuppetMaster))
	{
		//Validate that the current puppet is owed by the entity
		if(pPuppet->pSaved->conOwner.containerID != pEnt->myContainerID || pPuppet->pSaved->conOwner.containerType != pEnt->myEntityType)
		{
			TRANSACTION_RETURN_LOG_FAILURE("FAILED %s does not own puppet of ID %d",pEnt->debugName, pPuppet->myContainerID);
		}
		//If the current type is entity player, then the type and ID are not set yet
		if(pEnt->pSaved->pPuppetMaster->curType == GLOBALTYPE_ENTITYPLAYER)
		{
			pEnt->pSaved->pPuppetMaster->curType = pPuppet->myEntityType;
			pEnt->pSaved->pPuppetMaster->curID = pPuppet->myContainerID;

			TRANSACTION_RETURN_LOG_SUCCESS("SUCCESS %s has the current ID of %d",pEnt->debugName,pPuppet->myContainerID);
		}

		TRANSACTION_RETURN_LOG_FAILURE("FAILED %s has an ID already set to %d",pEnt->debugName,pEnt->pSaved->pPuppetMaster->curID);
	}
	TRANSACTION_RETURN_LOG_FAILURE("FAILED %s is not a puppet master",pEnt->debugName);

}

AUTO_TRANSACTION
ATR_LOCKS(ent, ".Psaved.Ppbuilds, .Psaved.Ppuppetmaster, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Psaved.Ppownedcontainers, .Pplayer.Publicaccountname, .Hallegiance, .Hsuballegiance, .Psaved.Ppallowedcritterpets, .Psaved.Ppalwayspropslots, .pInventoryV2.Ppinventorybags, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .pInventoryV2.Peaowneduniqueitems, pInventoryV2.ppLiteBags")
ATR_LOCKS(pPuppet, ".Psaved.Pscpdata, .Pchar, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Psaved.Ipetid, .Pcritter.Petdef, .Psaved.Savedname, .pInventoryV2.Ppinventorybags, .Itemidmax, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .pInventoryV2.Pplitebags, .pInventoryV2.Peaowneduniqueitems, .Pplayer.Playertype");
enumTransactionOutcome trEntity_MakePuppetMaster(ATR_ARGS, NOCONST(Entity) *ent, NOCONST(Entity) *pPuppet, ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	if(!Entity_MakePuppetMaster(ent))
	{
		if(ISNULL(ent->pSaved->pPuppetMaster))
			TRANSACTION_RETURN_LOG_FAILURE("FAILED to make puppet master: %s",ent->debugName);
	}

	if(!ISNULL(pPuppet))
	{
		trAddSavedPet(ATR_PASS_ARGS,ent,NULL,pPuppet,CONRELATION_PET,OWNEDSTATE_OFFLINE,0,0,0,0,pReason,pExtract);
		trEntity_AddPuppet(ATR_PASS_ARGS,ent,pPuppet,NULL,NULL,true);

		trEntity_SetCurrentPuppetID(ATR_PASS_ARGS,ent,pPuppet);
	}

	TRANSACTION_RETURN_LOG_SUCCESS("SUCCESS %s is now puppet master",ent->debugName);

}

AUTO_TRANSACTION
	ATR_LOCKS(pEntity, ".Psaved.Ppownedcontainers");
enumTransactionOutcome trEntity_UpdateAwayTeamPets(ATR_ARGS, NOCONST(Entity) *pEntity, AwayTeamMembers* pMembers)
{
	if ( NONNULL(pEntity) )
	{
		S32 i, c;
		for (i = 0; i < eaSize(&pEntity->pSaved->ppOwnedContainers); i++)
		{
			S32 iMembersCount = pMembers ? eaSize(&pMembers->eaMembers) : 0;
			U32 uiPetID = pEntity->pSaved->ppOwnedContainers[i]->conID;
			
			for ( c = 0; c < iMembersCount; c++ )
			{
				if ( pMembers->eaMembers[c]->eEntType == GLOBALTYPE_ENTITYPLAYER )
					continue;

				if ( uiPetID == pMembers->eaMembers[c]->iEntID )
				{
					pEntity->pSaved->ppOwnedContainers[i]->eState = OWNEDSTATE_ACTIVE;
					pEntity->pSaved->ppOwnedContainers[i]->bTeamRequest = true;
					break;
				}
			}

			if ( c == iMembersCount )
			{
				pEntity->pSaved->ppOwnedContainers[i]->bTeamRequest = false;
				pEntity->pSaved->ppOwnedContainers[i]->eState = OWNEDSTATE_OFFLINE;
			}
		}
		TRANSACTION_RETURN_LOG_SUCCESS("SUCCESS %s: Updated Away Team Pets",pEntity->debugName);
	}

	TRANSACTION_RETURN_LOG_FAILURE("FAILED: Invalid Player Entity");
}

AUTO_TRANS_HELPER;
void Entity_SwapAlwaysPropSlots(ATH_ARG NOCONST(Entity) *pMaster, ATH_ARG NOCONST(Entity) *pOldEnt, U32 uNewID)
{
	int i, j;
	if (NONNULL(pOldEnt) && NONNULL(pOldEnt->pCritter))
	{
		NOCONST(PuppetEntity)* pOldPuppet = NULL;
		for (i = eaSize(&pMaster->pSaved->pPuppetMaster->ppPuppets)-1; i >= 0; i--)
		{
			if (pOldEnt->myEntityType == pMaster->pSaved->pPuppetMaster->ppPuppets[i]->curType &&
				pOldEnt->myContainerID == pMaster->pSaved->pPuppetMaster->ppPuppets[i]->curID)
			{
				pOldPuppet = pMaster->pSaved->pPuppetMaster->ppPuppets[i];
				break;
			}
		}
		if (NONNULL(pOldPuppet))
		{
			PetDef* pOldPetDef = GET_REF(pOldEnt->pCritter->petDef);

			eaClearStructNoConst(&pOldPuppet->ppSavedPropSlots, parse_AlwaysPropSlot);

			if (pOldPetDef)
			{
				for (i = 0; i < eaSize(&pMaster->pSaved->ppAlwaysPropSlots); i++)
				{
					for (j = 0; j < eaSize(&pOldPetDef->ppAlwaysPropSlot); j++)
					{
						if (GET_REF(pMaster->pSaved->ppAlwaysPropSlots[i]->hDef) == GET_REF(pOldPetDef->ppAlwaysPropSlot[j]->hPropDef))
						{
							eaPush(&pOldPuppet->ppSavedPropSlots, eaRemove(&pMaster->pSaved->ppAlwaysPropSlots, i));
							i--;
							break;
						}
					}		
				}
			}
		}
	}
	if (uNewID)
	{
		NOCONST(PuppetEntity)* pNewPuppet = NULL;
		for (i = eaSize(&pMaster->pSaved->pPuppetMaster->ppPuppets)-1; i >= 0; i--)
		{
			if (uNewID == pMaster->pSaved->pPuppetMaster->ppPuppets[i]->curID)
			{
				pNewPuppet = pMaster->pSaved->pPuppetMaster->ppPuppets[i];
				break;
			}
		}
		if (NONNULL(pNewPuppet))
		{
			S32 iPuppetSlotCount = eaSize(&pNewPuppet->ppSavedPropSlots);
			for (i = 0; i < iPuppetSlotCount; i++)
			{
				NOCONST(AlwaysPropSlot)* pSlot = StructCloneNoConst(parse_AlwaysPropSlot, pNewPuppet->ppSavedPropSlots[i]);
				S32 iOwnerSlotCount = eaSize(&pMaster->pSaved->ppAlwaysPropSlots);
				U32 uiSlotID = iOwnerSlotCount + 1;
				for (j = 0; j < iOwnerSlotCount; j++)
				{
					U32 uiIndex = j + 1;
					if (uiIndex != pMaster->pSaved->ppAlwaysPropSlots[j]->iSlotID)
					{
						uiSlotID = uiIndex;
						break;
					}
				}
				pSlot->iSlotID = uiSlotID;
				eaInsert(&pMaster->pSaved->ppAlwaysPropSlots, pSlot, j);
			}
		}
	}
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, ".Psaved.Ppuppetmaster.Pppuppets, .Psaved.Ppuppetmaster.Lastactiveid, .Psaved.Ppalwayspropslots")
	ATR_LOCKS(pOldEnt, ".Pcritter.petdef");
bool Entity_SetPuppetStateActive(ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(Entity) *pOldEnt, U32 uNewID, U32 uOldID, U32 bSetLastActiveID)
{
	int i;
	NOCONST(PuppetEntity)* pOldPuppet = NULL;
	NOCONST(PuppetEntity)* pNewPuppet = NULL;

	if (ISNULL(pEnt) || ISNULL(pEnt->pSaved) || ISNULL(pEnt->pSaved->pPuppetMaster))
		return false;

	for (i = eaSize(&pEnt->pSaved->pPuppetMaster->ppPuppets)-1; i >= 0; i--)
	{
		NOCONST(PuppetEntity) *pPuppet = pEnt->pSaved->pPuppetMaster->ppPuppets[i];
		if (uOldID == pPuppet->curID)
		{
			pOldPuppet = pPuppet;
		}
		else if (uNewID == pPuppet->curID)
		{
			pNewPuppet = pPuppet;
		}
	}

	if (ISNULL(pNewPuppet))
		return false;

	if (NONNULL(pOldPuppet) && (pOldPuppet->eType != pNewPuppet->eType || pOldPuppet->eState != PUPPETSTATE_ACTIVE))
		return false;

	if (bSetLastActiveID && NONNULL(pOldPuppet))
	{
		pEnt->pSaved->pPuppetMaster->lastActiveID = pOldPuppet->curID;
	}
	if (NONNULL(pOldPuppet))
	{
		pOldPuppet->eState = PUPPETSTATE_OFFLINE;
	}
	pNewPuppet->eState = PUPPETSTATE_ACTIVE;

	if (NONNULL(pOldEnt))
	{
		Entity_SwapAlwaysPropSlots(pEnt, pOldEnt, uNewID);
	}
	return true;
}

AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Psaved.Ppuppetmaster.Pppuppets, .Psaved.Ppuppetmaster.Lastactiveid, .Psaved.Ppalwayspropslots")
	ATR_LOCKS(pOldEnt, ".Pcritter.petdef");
enumTransactionOutcome trEntity_SetPuppetStateActive(ATR_ARGS, NOCONST(Entity) *pEnt, NOCONST(Entity) *pOldEnt, U32 uNewID, U32 uOldID, U32 bSetLastActiveID)
{
	if(NONNULL(pEnt->pSaved) && Entity_SetPuppetStateActive(pEnt, pOldEnt, uNewID, uOldID, bSetLastActiveID))
	{
		TRANSACTION_RETURN_LOG_SUCCESS("SUCCESS %s Set %d as an active puppet",pEnt->debugName,uNewID);
	}
	TRANSACTION_RETURN_LOG_FAILURE("FAILED %s could not set %d as an active puppet",pEnt->debugName,uNewID);
}

AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Psaved.Ppuppetmaster.Hpreferredcategoryset");
enumTransactionOutcome trEntity_SetPreferredCategorySet(ATR_ARGS, NOCONST(Entity) *pEnt, const char* pchSetName)
{
	if (NONNULL(pEnt->pSaved) && NONNULL(pEnt->pSaved->pPuppetMaster))
	{
		REF_HANDLE_SET_FROM_STRING(g_hCharacterClassCategorySetDict, pchSetName, pEnt->pSaved->pPuppetMaster->hPreferredCategorySet);
		TRANSACTION_RETURN_LOG_SUCCESS("SUCCESS %s set %s as the preferred category set", pEnt->debugName,pchSetName);
	}
	TRANSACTION_RETURN_LOG_FAILURE("FAILED %s could not set %s as the preferred category set",pEnt->debugName,pchSetName);
}

AUTO_TRANSACTION
	ATR_LOCKS(pSavedPet, ".Psaved.Conowner.Containertype, .Psaved.Conowner.Containerid, .Psaved.Ipetid")
	ATR_LOCKS(pOwner, ".Psaved.Ppownedcontainers, .Psaved.Ppallowedcritterpets, .Psaved.Ppuppetmaster.Pptemppuppets");
enumTransactionOutcome trSavedPet_UpdateOwner(ATR_ARGS, NOCONST(Entity) *pSavedPet, NOCONST(Entity) *pOwner)
{
	int i;
	ContainerID iOwnerID = pOwner->myContainerID;
	int eOwnerType = pOwner->myEntityType;
	if(!ISNULL(pSavedPet->pSaved))
	{
		if(pSavedPet->pSaved->conOwner.containerType != eOwnerType
			|| pSavedPet->pSaved->conOwner.containerID != iOwnerID)
		{
			U32 uiPetID;
			pSavedPet->pSaved->conOwner.containerID = iOwnerID;
			pSavedPet->pSaved->conOwner.containerType = eOwnerType;

			uiPetID = entity_GetNextPetIDHelper(pOwner,pSavedPet->myContainerID);
			pSavedPet->pSaved->iPetID = uiPetID;

			for (i = 0; i < eaSize(&pOwner->pSaved->ppOwnedContainers); i++)
			{
				if(pOwner->pSaved->ppOwnedContainers[i]->conID == pSavedPet->myContainerID)
				{
					pOwner->pSaved->ppOwnedContainers[i]->uiPetID = uiPetID;
					break;
				}
			}

			TRANSACTION_RETURN_LOG_SUCCESS("SUCCESS %s Set %s:%d as owner",pSavedPet->debugName,GlobalTypeToName(eOwnerType),iOwnerID);
		}
	}
	TRANSACTION_RETURN_LOG_FAILURE("FAILED %s could not set new owner (%s:%d)",pSavedPet->debugName,GlobalTypeToName(eOwnerType),iOwnerID);
}

AUTO_TRANSACTION
	ATR_LOCKS(pSavedPet, ".Pchar");
enumTransactionOutcome trSavedPet_UpdateSpeciesForAllegiance(ATR_ARGS, NOCONST(Entity) *pSavedPet, AllegianceDef *pDef)
{
	return entity_trh_AllegianceSpeciesChange(ATR_PASS_ARGS, pSavedPet, pDef);
}

// (drazza) This is a terrible, terrible hack, but if I don't do this, the autotransaction
// system will complain that there are no ATR locks in the following two transactions. Since
// these two autotransactions are designed to set myContainerID, the errors do not do anything
// for us. Unfortunately this means we are locking a field unnecessarily
#define FAKE_ATRLOCKS_NOTHING(e) e

// This is a transaction that intentionally breaks a saved pet by setting its container ID to 0 for debug purposes
AUTO_TRANSACTION
	ATR_LOCKS(pSavedPet, ".Itemidmax");
enumTransactionOutcome trSavedPet_StompContainerID(ATR_ARGS, NOCONST(Entity) *pSavedPet)
{
	U32 uiOldContainerID;

	if (ISNULL(pSavedPet))
	{
		TRANSACTION_RETURN_LOG_FAILURE("FAILED Invalid Saved Pet Entity");
	}

	// Read the comment above as to why this is being done
	FAKE_ATRLOCKS_NOTHING(pSavedPet->ItemIDMax);

 	uiOldContainerID = pSavedPet->myContainerID;
	pSavedPet->myContainerID = 0;

	TRANSACTION_RETURN_LOG_SUCCESS("SUCCESS %s Set pet ID %d to %d",pSavedPet->debugName,uiOldContainerID,pSavedPet->myContainerID);
}

// This is a transaction that is designed to fix the breakage caused by StompContainerID and similar bugs. Improper use of this is very dangerous
AUTO_TRANSACTION
	ATR_LOCKS(pSavedPet, ".Itemidmax");
enumTransactionOutcome trSavedPet_FixPetContainerID(ATR_ARGS, NOCONST(Entity) *pSavedPet, U32 uiContainerID)
{
	U32 uiOldContainerID;

	if (ISNULL(pSavedPet))
	{
		TRANSACTION_RETURN_LOG_FAILURE("FAILED Invalid Saved Pet Entity");
	}

	// Read the comment above as to why this is being done
	FAKE_ATRLOCKS_NOTHING(pSavedPet->ItemIDMax);

 	uiOldContainerID = pSavedPet->myContainerID;
	pSavedPet->myContainerID = uiContainerID;

	TRANSACTION_RETURN_LOG_SUCCESS("SUCCESS %s Fixed pet ID from %d to %d",pSavedPet->debugName,uiOldContainerID,pSavedPet->myContainerID);
}