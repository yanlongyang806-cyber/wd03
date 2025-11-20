/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
**************************************************************************/

#include "Character.h"
#include "CharacterClass.h"
#include "CombatEnums.h"
#include "CostumeCommon.h"
#include "CostumeCommonEntity.h"
#include "CostumeCommonLoad.h"
#include "CostumeCommonTailor.h"
#include "Entity.h"
#include "EntitySavedData.h"
#include "error.h"
#include "Estring.h"
#include "Expression.h"
#include "gslCostume.h"
#include "gslInteractable.h"
#include "gslSendToClient.h"
#include "mission_common.h"
#include "SavedPetCommon.h"
#include "StringUtil.h"
#include "wlInteraction.h"
#include "WorldLibEnums.h"
#include "player.h"
#include "GameAccountDataCommon.h"
#include "Guild.h"
#include "NotifyCommon.h"
#include "tradeCommon.h"
#include "PowerAnimFX.h"

#include "../Common/AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


// --------------------------------------------------------------------------
//  Debugging Commands
// --------------------------------------------------------------------------

AUTO_COMMAND ACMD_NAME(TestCostumeFromInteraction) ACMD_ACCESSLEVEL(9);
void costume_TestCostumeFromInteractionCmd(Entity *ent)
{
	GameInteractable *pInteractable = interactable_FindClosestInteractable(ent, 0xffffffff, NULL);
	if (!pInteractable)
	{
		Errorf("Failed to find any interaction nodes near the player!");
		return;
	}
	costumeEntity_SetDestructibleObjectCostumeByName(ent, pInteractable->pcNodeName);
}


AUTO_COMMAND ACMD_NAME(ChangeCostume) ACMD_SERVERCMD ACMD_ACCESSLEVEL(9);
void costume_ChangeCostumeCmd(Entity *pEnt, ACMD_NAMELIST("PlayerCostume", REFDICTIONARY)  char *costumeName)
{
	PlayerCostume *pCostume = (PlayerCostume*)costumeEntity_CostumeFromName(costumeName);
	if ( !pCostume ) {
		gslSendPrintf(pEnt,"Failed to find costume %s", costumeName);
		return;
	}

	costumetransaction_ChangePlayerCostume(pEnt, pCostume);
}


AUTO_COMMAND ACMD_NAME(ChangeCostume) ACMD_LIST(gEntConCmdList) ACMD_ACCESSLEVEL(9);
void entCon_ChangeCostume(Entity *pEnt, ACMD_NAMELIST("PlayerCostume", REFDICTIONARY)  char *costumeName)
{
	costume_ChangeCostumeCmd(pEnt, costumeName);
}


// --------------------------------------------------------------------------
//  CSR Commands
// --------------------------------------------------------------------------

// This command replaces all of the costumes on the entity. This can change the gender of the entity.
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_NAME(ReplaceCostumes) ACMD_CATEGORY(csr);
void costume_ReplaceCostumesCmd(Entity *pEnt, ACMD_NAMELIST("PlayerCostume", REFDICTIONARY)  char *costumeName)
{
	// This command does not work in games that use slot sets
	if(pEnt && pEnt->pSaved && !pEnt->pSaved->costumeData.pcSlotSet)
	{
		PlayerCostume *pCostume = (PlayerCostume*)costumeEntity_CostumeFromName(costumeName);
		if(!pCostume)
		{
			gslSendPrintf(pEnt,"Failed to find costume %s", costumeName);
			return;
		}

		costumetransaction_ReplaceCostumes(pEnt, pCostume);
	}
}

// Replace all of the costumes but maintain same gender, requires male and female costume names
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_NAME(ReplaceCostumesSameGender) ACMD_CATEGORY(csr);
void costume_ReplaceCostumesSameGenderCmd(Entity *pEnt, ACMD_NAMELIST("PlayerCostume", REFDICTIONARY)  char *costumeNameMale, ACMD_NAMELIST("PlayerCostume", REFDICTIONARY)  char *costumeNameFemale)
{
	// This command does not work in games that use slot sets
	if(pEnt && pEnt->pSaved && !pEnt->pSaved->costumeData.pcSlotSet && eaSize(&pEnt->pSaved->costumeData.eaCostumeSlots) > 0)
	{
		PlayerCostume *pBaseCostume = costumeEntity_GetActiveSavedCostume(pEnt);
		char *pcCostumeName = NULL;
		PCSkeletonDef *pSkel = pBaseCostume ? GET_REF(pBaseCostume->hSkeleton) : NULL;
		if(pSkel)
		{
			if(pSkel->eGender == Gender_Male)
			{
				pcCostumeName = costumeNameMale;
			}
			else if(pSkel->eGender == Gender_Female)
			{
				pcCostumeName = costumeNameFemale;
			}
			
			if(pcCostumeName)
			{
				PlayerCostume *pCostume = (PlayerCostume*)costumeEntity_CostumeFromName(pcCostumeName);
				if(pCostume)
				{
					PCSkeletonDef *pSkelcostume = GET_REF(pCostume->hSkeleton);
					if(pSkelcostume && pSkelcostume->eGender == pSkel->eGender)
					{
						costume_ReplaceCostumesCmd(pEnt, pcCostumeName);
					}
					return;
				}		
			}
		}
		gslSendPrintf(pEnt,"No costume gender that matches entity gender.");
	}
	
}


// Changes a player's costume to the default and gives them a free costume change
AUTO_COMMAND ACMD_NAME(BadCostume, force_character_costume_change) ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(csr, XMLRPC);
S32 costume_BadCostumeCmd(Entity *pEnt)
{
	if (pEnt && pEnt->myEntityType == GLOBALTYPE_ENTITYPLAYER)
	{
		//For STO to make sure we always change the ground costume for a player not the ship costume
		Entity *pPlayerEnt = entity_GetPuppetEntityByType( pEnt, "Ground", NULL, true, true );
		if (pPlayerEnt) pEnt = pPlayerEnt;
	}

	if(pEnt && pEnt->pSaved)
	{
		costumetransaction_CostumeAndCredit(pEnt);
		return true;
	}

	return false;
}

// --------------------------------------------------------------------------
//  Player Commands
// --------------------------------------------------------------------------

AUTO_COMMAND ACMD_NAME(ChangeMood) ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRODUCTS(FightClub, StarTrek);
void costume_ChangeMoodCmd(Entity *pEnt, ACMD_NAMELIST("CostumeMood", REFDICTIONARY) const char *pcMood)
{
	PCMood *pMood = RefSystem_ReferentFromString(g_hCostumeMoodDict, pcMood);
	if ( !pMood ) {
		gslSendPrintf(pEnt, "Failed to find mood %s", pcMood);
		return;
	}

	costumetransaction_ChangeMood(pEnt, pcMood);
}

AUTO_COMMAND ACMD_NAME(StorePlayerCostume) ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void costume_StorePlayerCostumeCmd(Entity *pEnt, PCCostumeStorageType eCostumeType, U32 uContainerID, int iIndex, PlayerCostume *pCostume, const char *pcSlotType, PCPaymentMethod ePayMethod)
{
	bool bOnGround = (entGetWorldRegionTypeOfEnt(pEnt) == WRT_Ground);
	SpeciesDef *pSpecies = NULL;
	PCSkeletonDef *pSkeleton = NULL;
	Entity *pOtherEnt = NULL;
	PCSlotDef *pSlotDef = NULL;
	PCSlotType *pSlotType = NULL;
	PlayerCostumeSlot *pCostumeSlot = NULL;
	char *errString = NULL;
	S32 i, iCost;

	if (!pCostume)
	{
		ClientCmd_CostumeCreator_SetStorePlayerCostumeResult(pEnt, false, "StoreCostume.BadCostume");
		return;
	}

	if (iIndex < 0)
	{
		ClientCmd_CostumeCreator_SetStorePlayerCostumeResult(pEnt, false, "StoreCostume.InvalidIndex");
		return;
	}

	pOtherEnt = costumeEntity_GetStoreCostumeEntity(pEnt, eCostumeType, uContainerID);
	if (!pOtherEnt)
	{
		char *estrErrorMessage = NULL;
		estrPrintf(&estrErrorMessage, "StoreCostume.EntityNotFound.%s", StaticDefineIntRevLookup(PCCostumeStorageTypeEnum, eCostumeType));
		ClientCmd_CostumeCreator_SetStorePlayerCostumeResult(pEnt, false, estrErrorMessage);
		estrDestroy(&estrErrorMessage);
		return;
	}

	// Only execute the store command if the player can really edit the costume
	if (!costumeEntity_CanPlayerEditCostume(pEnt, pOtherEnt))
	{
		ClientCmd_CostumeCreator_SetStorePlayerCostumeResult(pEnt, false, "StoreCostume.CannotEditCostume");
		return;
	}

	// Prevent editing of pets in the current trade
	if (trade_IsPetBeingTraded(pOtherEnt, pEnt))
	{
		ClientCmd_CostumeCreator_SetStorePlayerCostumeResult(pEnt, false, "StoreCostume.TradingEntity");
		return;
	}

	// Validate costume species
	pSpecies = pOtherEnt->pChar ? GET_REF(pOtherEnt->pChar->hSpecies) : NULL;
	if (pSpecies && GET_REF(pCostume->hSpecies) != pSpecies)
	{
		ClientCmd_CostumeCreator_SetStorePlayerCostumeResult(pEnt, false, "StoreCostume.DifferentSpecies");
		return;
	}

	// Validate costume skeleton
	if (pOtherEnt->pSaved)
	{
		for (i = 0; !pSkeleton && i < eaSize(&pOtherEnt->pSaved->costumeData.eaCostumeSlots); i++)
		{
			if (pOtherEnt->pSaved->costumeData.eaCostumeSlots[i]->pCostume)
				pSkeleton = GET_REF(pOtherEnt->pSaved->costumeData.eaCostumeSlots[i]->pCostume->hSkeleton);
		}
	}
	if (pSkeleton && GET_REF(pCostume->hSkeleton) != pSkeleton)
	{
		ClientCmd_CostumeCreator_SetStorePlayerCostumeResult(pEnt, false, "StoreCostume.DifferentSkeleton");
		return;
	}

	// Validate costume slot
	if (!costumeEntity_IsCostumeSlotUnlocked(pEnt, pOtherEnt, iIndex))
	{
		ClientCmd_CostumeCreator_SetStorePlayerCostumeResult(pEnt, false, "StoreCostume.NotUnlocked");
		return;
	}

	// Get the current slot information
	if (!costumeEntity_GetStoreCostumeSlot(pEnt, pOtherEnt, iIndex, &pSlotDef, &pCostumeSlot))
	{
		ClientCmd_CostumeCreator_SetStorePlayerCostumeResult(pEnt, false, "StoreCostume.InvalidIndex");
		return;
	}

	// Check if the costume is locked
	if (pCostumeSlot && pCostumeSlot->pCostume && pCostumeSlot->pCostume->bPlayerCantChange)
	{
		ClientCmd_CostumeCreator_SetStorePlayerCostumeResult(pEnt, false, "StoreCostume.CannotChangeCostume");
		return;
	}

	// Validate new slot type
	if (pSlotDef)
	{
		pSlotType = costumeLoad_GetSlotType(pcSlotType);

		// If the slot def requires a slot type, then ensure a valid slot type was provided
		if (pSlotDef->pcSlotType && *pSlotDef->pcSlotType && !pSlotType)
		{
			ClientCmd_CostumeCreator_SetStorePlayerCostumeResult(pEnt, false, "StoreCostume.InvalidSlotType");
			return;
		}

		// Ensure the slot type is valid
		if (pSlotType && (!pSlotDef->pcSlotType || !*pSlotDef->pcSlotType || stricmp(pSlotDef->pcSlotType, pSlotType->pcName) != 0))
		{
			for (i = eaSize(&pSlotDef->eaOptionalSlotTypes) - 1; i >= 0; i--)
			{
				if (stricmp(pSlotDef->eaOptionalSlotTypes[i], pSlotType->pcName) == 0)
					break;
			}
			if (i < 0)
			{
				ClientCmd_CostumeCreator_SetStorePlayerCostumeResult(pEnt, false, "StoreCostume.InvalidSlotType");
				return;
			}
		}
	}
	else
	{
		// No slot type required
		pSlotType = NULL;
	}

	// KDB remove unneeded parts (such as geo none) then make sure costume is valid for player
	costumeTailor_StripUnnecessary(CONTAINER_NOCONST(PlayerCostume, pCostume));

	// Validate provided costume
	if (!costumeValidate_ValidatePlayerCreated(pCostume, pSpecies, pSlotType, pEnt, pOtherEnt, &errString, NULL, NULL, false))
	{
		ClientCmd_CostumeCreator_SetStorePlayerCostumeResult(pEnt, false, "StoreCostume.BadCostume");
		estrDestroy(&errString);
		return;
	}
	estrDestroy(&errString);

	// Calculate cost
	if (pCostumeSlot && pCostumeSlot->pCostume && !costumeEntity_TestCostumeForFreeChange(pEnt, pOtherEnt, iIndex))
	{
		NOCONST(PlayerCostume) *pTempCostume = StructCloneDeConst(parse_PlayerCostume, pCostumeSlot->pCostume);
		PlayerCostume **eaUnlockedCostumes = NULL;
		const char **eapchPowerFXBones = NULL;
		GameAccountData *pGameAccount = entity_GetGameAccount(pEnt);
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

		// Get supporting information
		costumeEntity_GetUnlockCostumes(pEnt->pSaved->costumeData.eaUnlockedCostumeRefs, pGameAccount, pEnt, pOtherEnt, &eaUnlockedCostumes);
		entity_FindPowerFXBones(pEnt, &eapchPowerFXBones);

		// Make the slot's current costume valid before trying to calculate the price
		costumeTailor_FillAllBones(pTempCostume, pSpecies, NULL, pSlotType, true, false, true);
		costumeTailor_MakeCostumeValid(pTempCostume, pSpecies, eaUnlockedCostumes, pSlotType, false, false, false, guild_GetGuild(pEnt), false, pExtract, false, eapchPowerFXBones);

		// Calculate the cost to change
		iCost = costumeEntity_GetCostToChange(pEnt, eCostumeType, pTempCostume, CONTAINER_NOCONST(PlayerCostume, pCostume), NULL);

		// Cleanup temp
		eaDestroy(&eaUnlockedCostumes);
		eaDestroy(&eapchPowerFXBones);
		StructDestroyNoConst(parse_PlayerCostume, pTempCostume);
	}
	else
	{
		// This is a new costume or the costume change is free
		iCost = 0;
	}

	// Fix up entity-related info on the costume
	costumeEntity_ApplyEntityInfoToCostume(pOtherEnt, pCostume);

	// Store the costume
	costumetransaction_StorePlayerCostume(pEnt, pEnt == pOtherEnt ? NULL : pOtherEnt, eCostumeType, iIndex, pCostume, SAFE_MEMBER(pSlotType, pcName), iCost, ePayMethod);

	// Now check to see if the costume needs to be stored on the active puppet as well
	if (pEnt == pOtherEnt && pEnt->pSaved->pPuppetMaster)
	{
		for (i = 0; i < eaSize(&pEnt->pSaved->pPuppetMaster->ppPuppets); i++)
		{
			PuppetEntity *pPuppetEnt = pEnt->pSaved->pPuppetMaster->ppPuppets[i];
			if (pPuppetEnt->curID == pEnt->pSaved->pPuppetMaster->curID
				&& pPuppetEnt->curType == pEnt->pSaved->pPuppetMaster->curType
				&& GET_REF(pPuppetEnt->hEntityRef))
			{
				// Now store in the puppet
				costumetransaction_StorePlayerCostume(pEnt, GET_REF(pPuppetEnt->hEntityRef), eCostumeType, iIndex, pCostume, SAFE_MEMBER(pSlotType, pcName), 0, ePayMethod);
				break;
			}
		}
	}
}


AUTO_COMMAND ACMD_NAME(DeletePlayerCostume) ACMD_SERVERCMD ACMD_ACCESSLEVEL(9) ACMD_PRIVATE;
void costume_DeletePlayerCostumeCmd(Entity *pEnt, PCCostumeStorageType eCostumeType, int iIndex)
{
	costumetransaction_StorePlayerCostume(pEnt, NULL, eCostumeType, iIndex, NULL, NULL, 0, kPCPay_Default);
}



// Sets active costume
AUTO_COMMAND ACMD_NAME(SetActiveCostume) ACMD_SERVERCMD ACMD_ACCESSLEVEL(0);
void costume_SetActiveCostumeCmd(Entity *pEnt, PCCostumeStorageType eCostumeType, int iIndex)
{
	if (!pEnt || !pEnt->pSaved || g_CostumeConfig.bDisablePlayerActiveChange) {
		return;
	}

	// Need to check this outside the transaction to avoid excessive locking
	if (!costumeEntity_IsCostumeSlotUnlocked(pEnt, pEnt, iIndex)) {
		gslSendPrintf(pEnt, "Slot not unlocked");
		return;
	}

	costumetransaction_SetPlayerActiveCostume(pEnt, eCostumeType, iIndex);
}


// Sets active costume
AUTO_COMMAND ACMD_NAME(SetPetActiveCostume) ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRODUCTS(StarTrek, FightClub);
void costume_SetPetActiveCostumeCmd(Entity *pEnt, const char *pchName, int iIndex)
{
	int i, j, num;
	int iPetArraySize;
	Entity *pMyPetEnt = NULL;
	int iPartitionIdx;

	if ((!pEnt) || (!pEnt->pSaved) || !pEnt->pSaved->pPuppetMaster) return;
	iPetArraySize = eaSize(&pEnt->pSaved->ppOwnedContainers);

	if ( !pchName ) {
		gslSendPrintf(pEnt, "No Name Entered");
		return;
	}

	iPartitionIdx = entGetPartitionIdx(pEnt);

	//find the pet by name
	for ( i = 0; i < iPetArraySize; i++ )
	{
		PetRelationship* pPet = pEnt->pSaved->ppOwnedContainers[i];
		Entity* pPetEnt = SavedPet_GetEntity(iPartitionIdx, pPet);

		if ( pPetEnt==NULL ) continue;

		num = eaSize( &pEnt->pSaved->pPuppetMaster->ppPuppets );
		for ( j = 0; j < num; j++ )
		{
			PuppetEntity* pPuppet = pEnt->pSaved->pPuppetMaster->ppPuppets[j];

			if ( pPuppet && (pPuppet->curID == pPetEnt->myContainerID) )
			{
				break;
			}
		}

		if (j >= num)
		{
			if (!strcmp(pchName,entGetLocalName(pPetEnt)))
			{
				pMyPetEnt = pPetEnt;
				break;
			}
		}
	}

	if ( !pMyPetEnt ) {
		gslSendPrintf(pEnt, "Name Not Found \"%s\"", pchName);
		return;
	}

	// Don't allow the active costume to be set if the pet is currently being traded
	if(trade_IsPetBeingTraded(pMyPetEnt, pEnt))
	{
		return;
	}

	// Need to check this outside the transaction to avoid excessive locking
	if (!costumeEntity_IsCostumeSlotUnlocked(pEnt, pMyPetEnt, iIndex)) {
		gslSendPrintf(pEnt, "Slot not unlocked");
		return;
	}

	costumetransaction_SetPlayerActiveCostume(pMyPetEnt, -1, iIndex);
}


// Check to see if the player can edit their costume.
AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME("Tailor_CheckIfTailor") ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_PRIVATE;
void gslCmdTailor_CheckIfTailor(Entity *pEnt)
{
	ClientCmd_Tailor_EnableTailor(pEnt, costumeEntity_CanPlayerEditCostume(pEnt, pEnt));
}


// --------------------------------------------------------------------------
//  Expression Command Wrappers
// --------------------------------------------------------------------------

// Tells the Player to attempt to Tailor a specific costume index, if they can edit their costume at this time
AUTO_EXPR_FUNC(player, mission);
void TailorCostumeIndex(ExprContext* context, int index)
{
	Entity *pEnt = exprContextGetVarPointerUnsafePooled(context, g_PlayerVarName);
	if(pEnt && costumeEntity_CanPlayerEditCostume(pEnt, pEnt))
	{
		ClientCmd_Tailor_AutoEditCostumeIndex(pEnt,index);
	}
}

