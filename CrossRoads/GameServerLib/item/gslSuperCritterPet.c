#include "supercritterpet.h"
#include "entEnums.h"
#include "itemCommon.h"
#include "EntitySavedData.h"
#include "AutoTransDefs.h"
#include "entity.h"
#include "inventoryTransactions.h"
#include "GameAccountDataCommon.h"
#include "gslCritter.h"
#include "Character.h"
#include "Entity.h"
#include "CharacterClass.h"
#include "inventorycommon.h"
#include "CharacterAttribs.h"
#include "gslSavedPet.h"
#include "NotifyCommon.h"
#include "Entity.h"
#include "gslentity.h"
#include "gslEventSend.h"
#include "EntityLib.h"
#include "LoggedTransactions.h"
#include "character_mods.h"
#include "PowersMovement.h"
#include "PowerAnimFX.h"
#include "GameStringFormat.h"
#include "player.h"
#include "gslSuperCritterPet.h"
#include "RegionRules.h"
#include "StringUtil.h"
#include "TextFilter.h"
#include "loggingEnums.h"

#include "AutoGen/EntitySavedData_h_ast.h"
#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/gslsupercritterpet_c_ast.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

AUTO_STRUCT;
typedef struct SCPPetStateCBData
{
	int containerType;
	int containerID;
	int iPetIdx;
	int bRefresh;
}SCPPetStateCBData;

AUTO_STRUCT;
typedef struct PetTrainingCBData{
	EntityRef erEnt;
	int iPet;
	int iOldLevel;
	int iNewLevel;
} PetTrainingCBData;

static void scp_resetPetEntInventory(Entity* pPlayerEnt, Entity* pPetEnt, ActiveSuperCritterPet* pActivePet, int idx);
void scp_PetStateCB(TransactionReturnVal* returnVal, SCPPetStateCBData* pData);

void scp_FixupTrainingActiveFlag(Entity* pPlayerEnt)
{
	int i;
	EntitySavedSCPData* pSCPData = scp_GetEntSCPDataStruct(pPlayerEnt);
	if (pSCPData)
	{
		pSCPData->bTrainingActive = false;
		for (i = 0; i < eaSize(&pSCPData->ppSuperCritterPets); i++)
		{
			if (pSCPData->ppSuperCritterPets[i]->uiTimeFinishTraining > 0)
			{
				pSCPData->bTrainingActive = true;
				return;
			}
		}
	}
}

AUTO_TRANS_HELPER;
enumTransactionOutcome scp_trh_SetActivePetLevel(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, int iPetIdx, U32 uiNewLevel, int iCostPaid, char bSetXP, GameAccountDataExtract* pExtract)
{
	int i;
	NOCONST(Item)* pPetItem = inv_trh_GetItemFromBag(ATR_PASS_ARGS, pEnt, 35 /* Literal InvBagIDs_SuperCritterPets */, iPetIdx, pExtract);
	ItemDef* pItemDef = pPetItem ? GET_REF(pPetItem->hItem) : NULL;
	if (pPetItem && pItemDef && pPetItem->pSpecialProps && pPetItem->pSpecialProps->pSuperCritterPet && scp_LevelIsValid(uiNewLevel, CONTAINER_RECONST(Item, pPetItem)))
	{
		NOCONST(SuperCritterPet)* pPet = pPetItem->pSpecialProps->pSuperCritterPet;
		SuperCritterPetDef *pPetDef = GET_REF(pPet->hPetDef);
		U32 uiOldLevel = pPetItem->pSpecialProps->pSuperCritterPet->uLevel;
		if (!pPetDef)
		{
			devassertmsgf(0, "scp_trh_SetActivePetLevel() failed due to a missing pet def \"%s\"!", REF_STRING_FROM_HANDLE(pPet->hPetDef));
			return TRANSACTION_OUTCOME_FAILURE;
		}
		pPetItem->pSpecialProps->pSuperCritterPet->uLevel = uiNewLevel;

		if ( bSetXP )
		{
			pPetItem->pSpecialProps->pSuperCritterPet->uXP = scp_GetTotalXPRequiredForLevel(uiNewLevel, CONTAINER_RECONST(Item, pPetItem));
		}

		//if this level unlocked a new costume, switch to that costume:
		for(i=0; i<eaSize(&pPetDef->ppAltCostumes); i++)
		{
			if (pPetDef->ppAltCostumes[i]->iLevel == uiNewLevel)
			{
				pPet->iCurrentSkin = i;
			}
		}

		TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_PETS, "SCPLevelChange", "PetItemDef %s OldLevel %i NewLevel %i RushCostPaid %i", pItemDef->pchName, uiOldLevel, pPet->uLevel, iCostPaid);

		return TRANSACTION_OUTCOME_SUCCESS;
	}
	devassertmsg(0, "scp_trh_SetActivePetLevel() failed due to a bad pet item or invalid level!");
	return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Pinventoryv2.Ppinventorybags[], pInventoryV2.ppLiteBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
enumTransactionOutcome scp_tr_SetActivePetLevel(ATR_ARGS, NOCONST(Entity)* pEnt, int iPetIdx, U32 uiNewLevel, char bSetXP, GameAccountDataExtract* pExtract)
{
	if (ISNULL(pEnt) || ISNULL(pEnt->pSaved))
		return TRANSACTION_OUTCOME_FAILURE;
	else
	{
		return scp_trh_SetActivePetLevel(ATR_PASS_ARGS, pEnt, iPetIdx, uiNewLevel, 0, bSetXP, pExtract);
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Pinventoryv2.Ppinventorybags[], pInventoryV2.ppLiteBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.Pscpdata.Erscp, .Pchar.Ilevelexp, .Hallegiance, .Hsuballegiance, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics");
enumTransactionOutcome scp_tr_RushSetActivePetLevel(ATR_ARGS, NOCONST(Entity)* pEnt, int iPetIdx, U32 uiNewLevel, int iCost, ItemChangeReason* pReason, GameAccountDataExtract* pExtract)
{
	if (ISNULL(pEnt) || ISNULL(pEnt->pSaved))
		return TRANSACTION_OUTCOME_FAILURE;
	else
	{
		ItemDef* pDef = GET_REF(g_SCPConfig.hRushTrainingCurrency);
		assert(pDef);
		if (!inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pEnt, false, pDef->pchName, -iCost, pReason))
		{
			QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, 0, 0, "SuperCritterPets.InsufficientCurrency", kNotifyType_ItemRequired);
			TRANSACTION_RETURN_LOG_FAILURE("FAILED to pay rush training cost of %i %s", iCost, pDef->pchName);
		}

		return scp_trh_SetActivePetLevel(ATR_PASS_ARGS, pEnt, iPetIdx, uiNewLevel, iCost, false, pExtract);
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, ".Pinventoryv2.Ppinventorybags[], .Psaved.Pscpdata.Isummonedscp, .Psaved.Pscpdata.Fcachedpetbonusxppct, .Psaved.Pscpdata.Erscp, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Hallegiance, .Hsuballegiance, pInventoryV2.ppLiteBags[]");
enumTransactionOutcome scp_trh_RenamePet(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, int iBag, int iPetIdx, const char* pchNewName, ItemChangeReason* pReason, GameAccountDataExtract* pExtract)
{
	if (ISNULL(pEnt) || ISNULL(pEnt->pSaved))
		return TRANSACTION_OUTCOME_FAILURE;
	else
	{
		int iCost = g_SCPConfig.iRenameCost;
		ItemDef* pDef = GET_REF(g_SCPConfig.hRenamingCurrency);
		NOCONST(Item)* pPetItem = inv_trh_GetItemFromBag(ATR_PASS_ARGS, pEnt, iBag, iPetIdx, pExtract);
		ItemDef* pPetItemDef = SAFE_GET_REF(pPetItem, hItem);
		NOCONST(SuperCritterPet)* pPet = SAFE_MEMBER2(pPetItem, pSpecialProps, pSuperCritterPet);
		int iCharacterNameError;
		bool bPaid = false;
		if(ISNULL(pPet) || ISNULL(pPetItemDef))
		{
			return TRANSACTION_OUTCOME_FAILURE;
		}
		if (!ISNULL(pPet->pchName))	//only charge for REnaming- first name is free.
		{
			bPaid = true;
			if (!inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pEnt, false, pDef->pchName, -iCost, pReason))
			{
				QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, 0, 0, "SuperCritterPets.InsufficientCurrencyRename", kNotifyType_ItemRequired);
				TRANSACTION_RETURN_LOG_FAILURE("FAILED to pay pet rename cost of %i %s", iCost, pDef->pchName);
			}
		}

		if (strlen(pchNewName) > 24)	//hard coded pet name character length limit
		{
			TRANSACTION_RETURN_LOG_FAILURE("FAILED to rename pet because the name was longer than 24 characters.");
		}

		iCharacterNameError = StringIsInvalidCharacterName(pchNewName, 0);

		if( iCharacterNameError == STRINGERR_PROFANITY )
		{
			QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, 0, 0, "SuperCritterPets.ProfanePetName", kNotifyType_ItemRequired);
			TRANSACTION_RETURN_LOG_FAILURE("FAILED to rename pet because new name was profane.");
		}
		else if(iCharacterNameError != STRINGERR_NONE)
		{
			QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, 0, 0, "SuperCritterPets.InvalidPetName", kNotifyType_ItemRequired);
			TRANSACTION_RETURN_LOG_FAILURE("FAILED to rename pet because new name violated STRINGERR %d.", iCharacterNameError);
		}
		else
		{
			StructFreeString(pPet->pchName);
			pPet->pchName = StructAllocString(pchNewName);
			TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_PETS, "SCPRename", "PetItemDef %s NewName \"%s\" Cost %i", pPetItemDef->pchName, pchNewName, bPaid ? iCost : 0);
			return TRANSACTION_OUTCOME_SUCCESS;
		}
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Pinventoryv2.Ppinventorybags[], .Psaved.Pscpdata.Isummonedscp, .Psaved.Pscpdata.Fcachedpetbonusxppct, .Psaved.Pscpdata.Erscp, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Hallegiance, .Hsuballegiance, pInventoryV2.ppLiteBags[]");
enumTransactionOutcome scp_tr_RenamePet(ATR_ARGS, NOCONST(Entity)* pEnt, int iBag, int iPetIdx, const char* pchNewName, ItemChangeReason* pReason, GameAccountDataExtract* pExtract)
{
	return scp_trh_RenamePet(ATR_PASS_ARGS, pEnt, iBag, iPetIdx, pchNewName, pReason, pExtract);
}

AUTO_TRANSACTION
	ATR_LOCKS(pPlayerEnt, ".Pplayer.Playertype, .Pplayer.Pugckillcreditlimit, .Psaved.Ppallowedcritterpets, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Pscpdata, pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, pInventoryV2.ppLiteBags[]");
enumTransactionOutcome scp_tr_BindPet(ATR_ARGS, NOCONST(Entity)* pPlayerEnt, int iDst, int iDstSlot, int iSrc, int iSrcSlot, int iBackupDstBag, const char* pchNewName)
{
	NOCONST(InventoryBag)* pDstBag = inv_trh_GetBag(ATR_PASS_ARGS, pPlayerEnt, iDst, NULL);
	NOCONST(InventoryBag)* pSrcBag = inv_trh_GetBag(ATR_PASS_ARGS, pPlayerEnt, iSrc, NULL);
	NOCONST(InventoryBag)* pBackupDstBag = inv_trh_GetBag(ATR_PASS_ARGS, pPlayerEnt, iBackupDstBag, NULL);
	NOCONST(Item)* pItem = inv_bag_trh_GetItem(ATR_PASS_ARGS, pSrcBag, iSrcSlot);
	NOCONST(InventorySlot)* pDstSlot = NULL;
	NOCONST(SuperCritterPet)* pPet = SAFE_MEMBER2(pItem, pSpecialProps, pSuperCritterPet);
	SuperCritterPetDef* pPetDef = pPet ? GET_REF(pPet->hPetDef) : NULL;
	ItemDef* pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	NOCONST(EntitySavedSCPData)* pData = scp_trh_GetOrCreateEntSCPDataStruct(ATR_PASS_ARGS, pPlayerEnt, true);
	int iNumEquipSlots = pPetDef ? eaSize(&pPetDef->ppEquipSlots) : 0;

	if (!pItem)
		TRANSACTION_RETURN_LOG_FAILURE("scp_tr_BindPet called with a NULL pItem in bag %d, slot %d.", iSrc, iSrcSlot);

	if (!pItemDef)
		TRANSACTION_RETURN_LOG_FAILURE("scp_tr_BindPet called with a def-less Item %s", REF_STRING_FROM_HANDLE(pItem->hItem));

	if (!pPet || !(pItemDef->eType == kItemType_SuperCritterPet))
		TRANSACTION_RETURN_LOG_FAILURE("scp_tr_BindPet called with a non-pet Item %s", REF_STRING_FROM_HANDLE(pItem->hItem));

	if (!pPetDef)
		TRANSACTION_RETURN_LOG_FAILURE("scp_tr_BindPet called with an SCP that has an invalid or missing def: %s", REF_STRING_FROM_HANDLE(pPet->hPetDef));

	if (pItemDef && pItemDef->flags & kItemDefFlag_BindOnEquip)
		pItem->flags |= kItemFlag_Bound;

	if(!(invbag_trh_flags(pDstBag) & InvBagFlag_BoundPetStorage) || !pBackupDstBag || !(invbag_trh_flags(pBackupDstBag) & InvBagFlag_BoundPetStorage))
	{
		Errorf("scp_tr_BindPet with a destination that is not flagged for bound pet storage!");
		TRANSACTION_RETURN_LOG_FAILURE("scp_tr_BindPet with a destination that is not flagged for bound pet storage!");
	}

	if (iDstSlot == -1)
		iDstSlot = inv_bag_trh_GetFirstEmptySlot(ATR_PASS_ARGS, pPlayerEnt, pDstBag);


	pDstSlot = iDstSlot >= 0 ? eaGet(&pDstBag->ppIndexedInventorySlots, iDstSlot) : NULL;
	if ( pDstSlot && pDstSlot->pItem )	//if there is a pet in the destination, try to move it
	{
		if((iDst == InvBagIDs_SuperCritterPets && pData->ppSuperCritterPets[iDstSlot]->uiTimeFinishTraining))
		{
			// can't move a training pet, try first backup bag slot instead.
			iDstSlot = -1;
		}

		if (pDstBag==pBackupDstBag) //destination is inactive pet storage
		{
			//try instead to put the new pet in the first available inactive pet storage slot.
			iDstSlot = -1;
		}
		//move existing pet to stable, if there is room (else fail)
		else if (!inv_bag_trh_MoveItem(ATR_PASS_ARGS, true, pPlayerEnt, NULL, pBackupDstBag , -1, pPlayerEnt, NULL, pDstBag, iDstSlot, 1, false, false, NULL, NULL, NULL))
		{
			QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, 0, 0, "SuperCritterPets.NoStableSpace", kNotifyType_InventoryFull);
			TRANSACTION_RETURN_LOG_FAILURE("Bind pet failed because active slot dst had a pet and there was no stable space.");
			return TRANSACTION_OUTCOME_FAILURE;
		}
	}


	if(pPetDef->bLevelToPlayer && pPet->uXP == 0)
	{
		//level the critter based on the player's level
		int newLevel = scp_GetPetStartLevelForPlayerLevel(entity_trh_GetSavedExpLevelLimited(pPlayerEnt), CONTAINER_RECONST(Item, pItem));
		pPet->uLevel = newLevel;
		pPet->uXP = scp_GetTotalXPRequiredForLevel(newLevel, CONTAINER_RECONST(Item, pItem));
	}

	if (iDstSlot >= 0 && inv_bag_trh_MoveItem(ATR_PASS_ARGS, true, pPlayerEnt, NULL, pDstBag, iDstSlot, pPlayerEnt, NULL, pSrcBag, iSrcSlot, 1, false, false, NULL, NULL, NULL))
	{
		//if we bound to the active pet bag, reset the active pet at the dest index.
		if (iDst == InvBagIDs_SuperCritterPets)
		{
			scp_trh_ResetActivePet(ATR_PASS_ARGS, pPlayerEnt, iNumEquipSlots, iDstSlot);

			// rename the pet
			if(pchNewName){
				TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_PETS, "SCPBind", "PetItemDef %s PetLevel %i", pPetDef->pchName, pPet->uLevel);

				return scp_trh_RenamePet(ATR_PASS_ARGS, pPlayerEnt, iDst, iDstSlot, pchNewName, NULL, NULL);
			}
		}
		TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_PETS, "SCPBind", "PetItemDef %s PetLevel %i", pPetDef->pchName, pPet->uLevel);
		return TRANSACTION_OUTCOME_SUCCESS;
	}
	else if (pBackupDstBag && inv_bag_trh_MoveItem(ATR_PASS_ARGS, true, pPlayerEnt, NULL, pBackupDstBag, iDstSlot, pPlayerEnt, NULL, pSrcBag, iSrcSlot, 1, false, false, NULL, NULL, NULL))
	{
		TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_PETS, "SCPBind", "PetItemDef %s PetLevel %i", pPetDef->pchName, pPet->uLevel);
		return TRANSACTION_OUTCOME_SUCCESS;
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_TRANSACTION
	ATR_LOCKS(pPlayerEnt, ".Psaved.pSCPdata, pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], .Pplayer.Playertype, .Pplayer.Pugckillcreditlimit, .Psaved.Ppuppetmaster.Curtempid, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppallowedcritterpets, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, pInventoryV2.ppLiteBags[]");
enumTransactionOutcome scp_tr_UnbindPet(ATR_ARGS, NOCONST(Entity)* pPlayerEnt, int iSrc, int iSrcSlot, int iCost, const ItemChangeReason* pReason)
{
	NOCONST(InventoryBag)* pSrcBag = inv_trh_GetBag(ATR_PASS_ARGS, pPlayerEnt, iSrc, NULL);
	NOCONST(InventoryBag)* pDstBag = inv_trh_GetBag(ATR_PASS_ARGS, pPlayerEnt,  2 /* Literal InvBagIDs_Inventory */, NULL);
	NOCONST(Item)* pItem = inv_bag_trh_GetItem(ATR_PASS_ARGS, pSrcBag, iSrcSlot);
	ItemDef* pItemDef = GET_REF(pItem->hItem);
	NOCONST(SuperCritterPet)* pPet = SAFE_MEMBER2(pItem, pSpecialProps, pSuperCritterPet);
	SuperCritterPetDef* pPetDef = SAFE_GET_REF(pPet, hPetDef);
	if (pItem && pPet && pPetDef)
	{
		int iNumEquipSlots = eaSize(&pPetDef->ppEquipSlots);
		ItemDef* pCurrencyDef = GET_REF(g_SCPConfig.hUnbindingCurrency);

		if(pItemDef->flags & kItemDefFlag_BindOnPickup)
		{
			TRANSACTION_RETURN_LOG_FAILURE("FAILED to unbind a bind-on-pickup pet: this should fail, how did someone try?");
		}

		pItem->flags &= ~kItemFlag_Bound;

		if (!inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pPlayerEnt, false, pCurrencyDef->pchName, -iCost, pReason))
		{
			QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, 0, 0, "SuperCritterPets.InsufficientCurrencyUnbind", kNotifyType_ItemRequired);
			TRANSACTION_RETURN_LOG_FAILURE("FAILED to pay pet unbind cost of %i %s", iCost, pCurrencyDef->pchName);
		}

		if(NONNULL(pPet->pchName))
		{
			//unname pet:
			StructFreeString(pPet->pchName);
			pPet->pchName = NULL;
		}

		if (inv_bag_trh_MoveItem(ATR_PASS_ARGS, true, pPlayerEnt, NULL, pDstBag, -1, pPlayerEnt, NULL, pSrcBag, iSrcSlot, 1, false, false, NULL, NULL, NULL))
		{
			if(iSrc == InvBagIDs_SuperCritterPets)
			{
				scp_trh_ResetActivePet(ATR_PASS_ARGS, pPlayerEnt, iNumEquipSlots, iSrcSlot);
			}
			TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_PETS, "SCPUnBind", "PetItemDef %s PetLevel %i CostPaid %i", pPetDef->pchName, pPet->uLevel, iCost);
			return TRANSACTION_OUTCOME_SUCCESS;
		}
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

//move an item from one pet slot to another slot on same pet.
AUTO_TRANSACTION
	ATR_LOCKS(pPlayerEnt, ".pInventoryV2.Ppinventorybags[], .Psaved.pSCPData, .Pplayer.Playertype, .Psaved.Ppuppetmaster.Curtype, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, pInventoryV2.ppLiteBags[]");
enumTransactionOutcome scp_tr_MovePetEquipmentToPet(ATR_ARGS, NOCONST(Entity)* pPlayerEnt, int iPet, int iEquipSlotSrc, int iEquipSlotDst)
{

	if (NONNULL(pPlayerEnt) && NONNULL(pPlayerEnt->pSaved))
	{
		NOCONST(EntitySavedSCPData)* pData = scp_trh_GetOrCreateEntSCPDataStruct(ATR_PASS_ARGS, pPlayerEnt, true);
		NOCONST(ActiveSuperCritterPet)* pPet = pData->ppSuperCritterPets[iPet];

		NOCONST(InventoryBag)* pPetEquipBag = pPet->pEquipment;

		if (NONNULL(pPet) && NONNULL(pPet->pEquipment))
		{
			if (!scp_trh_IsEquipSlotLocked(ATR_PASS_ARGS, pPlayerEnt, iPet, iEquipSlotDst))
			{
				if (inv_bag_trh_MoveItem(ATR_PASS_ARGS, true, pPlayerEnt, NULL, pPetEquipBag, iEquipSlotDst, pPlayerEnt, NULL, pPetEquipBag, iEquipSlotSrc, 1, false, false, NULL, NULL, NULL))
					return TRANSACTION_OUTCOME_SUCCESS;
			}
		}
	}
	return TRANSACTION_OUTCOME_FAILURE;
}
AUTO_TRANSACTION
	ATR_LOCKS(pPlayerEnt, ".pInventoryV2.Ppinventorybags[], .Psaved.pSCPData, .Pplayer.Playertype, .Psaved.Ppuppetmaster.Curtype, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, pInventoryV2.ppLiteBags[]");
enumTransactionOutcome scp_tr_MovePetEquipment(ATR_ARGS, NOCONST(Entity)* pPlayerEnt, int iPet, int iEquipSlot, int iBagID, int iBagSlot, int bEquip)
{

	if (NONNULL(pPlayerEnt) && NONNULL(pPlayerEnt->pSaved))
	{
		NOCONST(EntitySavedSCPData)* pData = scp_trh_GetOrCreateEntSCPDataStruct(ATR_PASS_ARGS, pPlayerEnt, true);
		NOCONST(ActiveSuperCritterPet)* pPet = pData->ppSuperCritterPets[iPet];

		NOCONST(InventoryBag)* pPetEquipBag = pPet->pEquipment;
		NOCONST(InventoryBag)* pInvBag = inv_trh_GetBag(ATR_PASS_ARGS, pPlayerEnt, iBagID, NULL);

		if (NONNULL(pPet) && NONNULL(pPet->pEquipment))
		{
			if (bEquip && !scp_trh_IsEquipSlotLocked(ATR_PASS_ARGS, pPlayerEnt, iPet, iEquipSlot))
			{
				if (inv_bag_trh_MoveItem(ATR_PASS_ARGS, true, pPlayerEnt, NULL, pPetEquipBag, iEquipSlot, pPlayerEnt, NULL, pInvBag, iBagSlot, 1, false, false, NULL, NULL, NULL))
					return TRANSACTION_OUTCOME_SUCCESS;
			}
			else
			{
				if (inv_bag_trh_MoveItem(ATR_PASS_ARGS, true, pPlayerEnt, NULL, pInvBag, iBagSlot, pPlayerEnt, NULL, pPetEquipBag, iEquipSlot, 1, false, false, NULL, NULL, NULL))
					return TRANSACTION_OUTCOME_SUCCESS;
			}
		}
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

static void scp_PetItemEquippedCB(TransactionReturnVal *pReturnVal, EntityRef *pData)
{
	if (pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		Entity *pEnt = entFromEntityRefAnyPartition(*pData);
		ClientCmd_scp_InvalidateFakeEntities(pEnt);
		scp_resetSummonedPetInventory(pEnt);
	}

	SAFE_FREE(pData);
}

static void scp_PetTrainingFinishedCB(TransactionReturnVal *pReturnVal, PetTrainingCBData *pData)
{
	Entity *pEnt = entFromEntityRefAnyPartition(pData->erEnt);
	EntitySavedSCPData* pSCPData = scp_GetEntSCPDataStruct(pEnt);
	
	if (pData)
	{
		if (pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
		{
			int i;
			int iNewLevel = scp_GetPetCombatLevel(scp_GetActivePetItem(pEnt, pData->iPet));
			int iNewEquipSlots = 0;
			int iNewGemSlots = 0;
			char* estrNotification = NULL;
			estrStackCreate(&estrNotification);
			ClientCmd_scp_InvalidateFakeEntities(pEnt);
			scp_resetSummonedPetInventory(pEnt);

			pSCPData->ppSuperCritterPets[pData->iPet]->uiTimeFinishTraining = 0;
			
			scp_FixupTrainingActiveFlag(pEnt);
		
			entity_SetDirtyBit(pEnt, parse_SavedEntityData, pEnt->pSaved, 0);

			//summon this pet if we don't have one summoned.
			if (pSCPData->iSummonedSCP < 0)
			{
				scp_SetPetState(pEnt, pData->iPet, true, false);
			}

			for (i = 0; i < eaiSize(&g_SCPConfig.eaEquipSlotUnlockLevels); i++)
			{
				if (pData->iOldLevel < g_SCPConfig.eaEquipSlotUnlockLevels[i] &&
					iNewLevel >= g_SCPConfig.eaEquipSlotUnlockLevels[i])
					iNewEquipSlots++;
			}
			for (i = 0; i < eaiSize(&g_SCPConfig.eaGemSlotUnlockLevels); i++)
			{
				if (pData->iOldLevel < g_SCPConfig.eaGemSlotUnlockLevels[i] &&
					iNewLevel >= g_SCPConfig.eaGemSlotUnlockLevels[i])
					iNewGemSlots++;
			}
			entFormatGameMessageKey(pEnt, &estrNotification, "SuperCritterPets.TrainingComplete",
				STRFMT_INT("NumEquipSlots", iNewEquipSlots),
				STRFMT_INT("NumGemSlots", iNewGemSlots),
				STRFMT_INT("LevelDelta", iNewLevel - pData->iOldLevel),
				STRFMT_INT("NewLevel", iNewLevel),
				STRFMT_END);

			ClientCmd_NotifySend(pEnt, kNotifyType_TrainingComplete, estrNotification, NULL, NULL);

			estrDestroy(&estrNotification);
		}
		else
		{
			Errorf("Failed to train pet %s from level %i to %i!", scp_GetActivePetDefName(pEnt, pData->iPet), pData->iOldLevel, pData->iNewLevel);
		}

		SAFE_FREE(pData);
	}
}

AUTO_TRANSACTION
	ATR_LOCKS(pPlayerEnt, ".Psaved.pSCPdata, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Pplayer.Playertype, pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, pInventoryV2.ppLiteBags[]");
enumTransactionOutcome scp_tr_MoveBoundPet(ATR_ARGS, NOCONST(Entity)* pPlayerEnt, int iDst, int iDstSlot, int iSrc, int iSrcSlot)
{
	NOCONST(InventoryBag)* pDstBag = inv_trh_GetBag(ATR_PASS_ARGS, pPlayerEnt, iDst, NULL);
	NOCONST(InventoryBag)* pSrcBag = inv_trh_GetBag(ATR_PASS_ARGS, pPlayerEnt, iSrc, NULL);
	NOCONST(Item)* pSrcItem = inv_bag_trh_GetItem(ATR_PASS_ARGS, pSrcBag, iSrcSlot);
	NOCONST(Item)* pDstItem = inv_bag_trh_GetItem(ATR_PASS_ARGS, pDstBag, iDstSlot);
	NOCONST(EntitySavedSCPData)* pData = scp_trh_GetOrCreateEntSCPDataStruct(ATR_PASS_ARGS, pPlayerEnt, true);

	if (!NONNULL(pSrcItem))
	{
		//maybe this got called twice, and the item has already moved
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if((iDst == InvBagIDs_SuperCritterPets && iDstSlot >= 0 && pData->ppSuperCritterPets[iDstSlot]->uiTimeFinishTraining) || (iSrc == InvBagIDs_SuperCritterPets && pData->ppSuperCritterPets[iSrcSlot]->uiTimeFinishTraining))
	{
		// can't move a training pet
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if (iDstSlot == -1)
	{
		iDstSlot = inv_bag_trh_GetFirstEmptySlot(ATR_PASS_ARGS, pPlayerEnt, pDstBag);
	}

	if (iDstSlot == -1)
	{
		QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, 0, 0, "SuperCritterPets.NoFreeActivePetSlots", kNotifyType_InventoryFull);
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if((iDst == InvBagIDs_SuperCritterPets && pData->ppSuperCritterPets[iDstSlot]->uiTimeFinishTraining) || (iSrc == InvBagIDs_SuperCritterPets && pData->ppSuperCritterPets[iSrcSlot]->uiTimeFinishTraining))
	{
		// can't move a training pet
		return TRANSACTION_OUTCOME_FAILURE;
	}

	//before we actually move the item
	if (iDst == InvBagIDs_SuperCritterPets && iSrc == InvBagIDs_SuperCritterPets)
	{
		//swapping the position of two active pets

		if (iSrcSlot == pData->iSummonedSCP)
		{
			pData->iSummonedSCP = iDstSlot;
		}
		else if (iDstSlot == pData->iSummonedSCP)
		{
			pData->iSummonedSCP = iSrcSlot;
		}

		eaSwap(&pData->ppSuperCritterPets, iDstSlot, iSrcSlot);
	}
	else if (iDst == InvBagIDs_SuperCritterPets)
	{	
		//we moved something into an active slot
		NOCONST(SuperCritterPet)* pPet = SAFE_MEMBER2(pSrcItem, pSpecialProps, pSuperCritterPet);
		SuperCritterPetDef* pDef = pPet ? GET_REF(pPet->hPetDef) : NULL;
		int iEquipSlots = pDef ? eaSize(&pDef->ppEquipSlots) : 0;

		//if this is not a pet, some other move function should have been called (maybe several move operations before the first updated the client?)
		if (!NONNULL(pPet))
		{
			return TRANSACTION_OUTCOME_FAILURE;
		}
		//if the pet isn't bound yet, BindPet should have been called
		if (!(pSrcItem->flags & kItemFlag_Bound))
		{
			return TRANSACTION_OUTCOME_FAILURE;
		}

		//activated a pet
		scp_trh_ResetActivePet(ATR_PASS_ARGS, pPlayerEnt, iEquipSlots, iDstSlot);
	}
	else if (iSrc == InvBagIDs_SuperCritterPets)
	{
		//we moved something out of an active slot

		if (iSrcSlot == pData->iSummonedSCP)
		{
			//We moved the currently summoned pet.
			pData->iSummonedSCP = -1;
		}

		//if it's bound (which it should always be) then the only valid destination is a pet storage bag
		if (!(pSrcItem->flags & kItemFlag_Bound) || 
			!(invbag_trh_flags(pDstBag) & InvBagFlag_BoundPetStorage))
		{
			return TRANSACTION_OUTCOME_FAILURE;
		}

		//deactivated a pet
		scp_trh_ResetActivePet(ATR_PASS_ARGS, pPlayerEnt, 0, iSrcSlot);
	}
	//attempt to move (destroys pSrcItem and pDstItem)

	if (!inv_bag_trh_MoveItem(ATR_PASS_ARGS, true, pPlayerEnt, NULL, pDstBag, iDstSlot, pPlayerEnt, NULL, pSrcBag, iSrcSlot, 1, false, false, NULL, NULL, NULL))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".psaved.pSCPData, .Pplayer.Playertype, pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
enumTransactionOutcome scp_tr_FixupActiveSlots(ATR_ARGS, NOCONST(Entity)* pEnt)
{
	scp_trh_GetOrCreateEntSCPDataStruct(ATR_PASS_ARGS, pEnt, true);
	return TRANSACTION_OUTCOME_SUCCESS;
}

Entity* scp_CreateCritterFromPet( Entity *pOwner, ActiveSuperCritterPet* pActivePet, Item* pPetItem, int iPetIdx)
{
	if (pOwner && pPetItem && pPetItem->pSpecialProps && pPetItem->pSpecialProps->pSuperCritterPet)
	{
		Entity *pPetEnt;
		NOCONST(Entity)* pNCPetEnt;
		ItemDef* pPetItemDef = GET_REF(pPetItem->hItem);
		CritterCreateParams createParams = {0};
		SuperCritterPet* pPet = pPetItem->pSpecialProps->pSuperCritterPet;
		SuperCritterPetDef *pPetDef = GET_REF(pPet->hPetDef);
		CritterDef *pCritterDef = SAFE_GET_REF(pPetDef, hCritterDef);
		int iPartitionIdx = entGetPartitionIdx(pOwner);
		U32 uiTime = pmTimestamp(0);
		const char* pchFXName = NULL;

		if (!pCritterDef || !pPetItemDef)
			return NULL;

		if (scp_CheckFlag(pPetItem, kSuperCritterPetFlag_Dead))
		{
			return NULL;
		}

		createParams.enttype = GLOBALTYPE_ENTITYCRITTER;
		createParams.iPartitionIdx = iPartitionIdx;
		createParams.iLevel = scp_GetPetCombatLevel(pPetItem);
		createParams.pFaction = GET_REF(pOwner->hFaction);
		createParams.erOwner = entGetRef(pOwner);
		createParams.pCostume = pPet->iCurrentSkin > 0 ? GET_REF(pPetDef->ppAltCostumes[pPet->iCurrentSkin-1]->hCostume) : NULL;

		pPetEnt = critter_CreateByDef(pCritterDef, &createParams, pCritterDef->pchFileName, false);
		pNCPetEnt = CONTAINER_NOCONST(Entity, pPetEnt);

		entSetCodeFlagBits(pPetEnt,ENTITYFLAG_CRITTERPET);
		pPetEnt->erCreator = pOwner->myRef;

		scp_resetPetEntInventory(pOwner, pPetEnt, pActivePet, iPetIdx);
		
		gslSavedPet_SetSpawnLocationRotationForPet(iPartitionIdx, pOwner, pPetEnt); 

		pPetEnt->pCritter->displayNameOverride = StructAllocString(pPet->pchName);

		pmFxStop(pOwner->pChar->pPowersMovement, 1, 0, kPowerAnimFXType_FromPet, pOwner->myRef, pOwner->myRef, uiTime, NULL);


		if (pPetDef->ppAltCostumes && pPet->iCurrentSkin > 0 && IS_HANDLE_ACTIVE(pPetDef->ppAltCostumes[pPet->iCurrentSkin-1]->hContinuingPlayerFX))
		{
			pchFXName = REF_STRING_FROM_HANDLE(pPetDef->ppAltCostumes[pPet->iCurrentSkin-1]->hContinuingPlayerFX);
		}
		else if(IS_HANDLE_ACTIVE(pPetDef->hContinuingPlayerFX))
		{
			pchFXName = REF_STRING_FROM_HANDLE(pPetDef->hContinuingPlayerFX);
		}

		if (pchFXName)
		{
			const char** eaNames = NULL;
			eaPush(&eaNames, pchFXName);
			pmFxStart(pOwner->pChar->pPowersMovement,
				1,0,kPowerAnimFXType_FromPet,
				pOwner->myRef,
				pOwner->myRef,
				uiTime,
				eaNames,
				NULL,
				0.0f,
				0.0f,
				0.0f,
				0.0f,
				NULL,
				NULL,
				NULL,
				0,
				0);
		}
		return pPetEnt;
	}
	return NULL;
}

void scp_SummonPetInternal(Entity *pPlayerEnt, int idx)
{
	EntitySavedSCPData* pData = scp_GetEntSCPDataStruct(pPlayerEnt);
	ActiveSuperCritterPet* pPetInfo = pData->ppSuperCritterPets[idx];
	GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pPlayerEnt);
	Item* pPetItem = inv_GetItemFromBag(pPlayerEnt, InvBagIDs_SuperCritterPets, idx, pExtract); 
	Entity* pPetEnt = NULL;
	
	RegionRules* pRegionRules = getRegionRulesFromRegion(entGetWorldRegionOfEnt(pPlayerEnt));

	if(pRegionRules->iAllowedPetsPerPlayer == 0)
	{
		notify_NotifySend(pPlayerEnt, kNotifyType_SuperCritterPet, entTranslateMessageKey(pPlayerEnt, "SuperCritterPets.NoSummonInThisRegion"), NULL, NULL);
		return;
	}

	pPetEnt = scp_CreateCritterFromPet(pPlayerEnt, pPetInfo, pPetItem, idx);

	if (pPetEnt)
	{
		pData->erSCP = pPetEnt->myRef;
		entity_SetDirtyBit(pPlayerEnt, parse_SavedEntityData, pPlayerEnt->pSaved, 0);
	}
}

void Entity_SuperCritterPetFixup(Entity* pEnt)
{
	int i, iNum = 0;
	GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	InventoryBag* pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), InvBagIDs_SuperCritterPets, pExtract);
	EntitySavedSCPData* pData = scp_GetEntSCPDataStruct(pEnt);
	bool bTransactionFixupRequired = false;

	if (!pData)
		return;

	if(pData->erSCP)
	{
		Entity* pPet = entFromEntityRef(pEnt->iPartitionIdx_UseAccessor, pData->erSCP);
		if (pPet  && pPet->erCreator == pEnt->myRef)
		{
			//if this happens it means that a pet got created before this, which (currently) happens only on map login.
			gslQueueEntityDestroy(pPet);
		}
		pData->erSCP = 0;
	}
	
	//Check for missing/incorrect persisted data that will require a transaction to fix.
	iNum = invbag_maxslots(pEnt, pBag);
	if (iNum != eaSize(&pData->ppSuperCritterPets))
		bTransactionFixupRequired = true;
	else
	{
		for (i = 0; i < eaSize(&pData->ppSuperCritterPets); i++)
		{
			if (!pData->ppSuperCritterPets[i] || !pData->ppSuperCritterPets[i]->pEquipment)
			{
				bTransactionFixupRequired = true;
				break;
			}
		}
	}

	scp_FixupTrainingActiveFlag(pEnt);

	if (bTransactionFixupRequired)
	{
		TransactionReturnVal* pVal = NULL;
		SCPPetStateCBData* pCBData;

		pCBData = calloc(1,sizeof(SCPPetStateCBData));
		pCBData->containerType = pEnt->myEntityType;
		pCBData->containerID = pEnt->myContainerID;
		pCBData->iPetIdx = pData->iSummonedSCP;

		pVal = LoggedTransactions_CreateManagedReturnValEnt("SCP_LoginFixup", pEnt, scp_PetStateCB, pCBData);
		AutoTrans_scp_tr_FixupActiveSlots(pVal, GetAppGlobalType(), pEnt->myEntityType, pEnt->myContainerID);
	}
	else if (pData->iSummonedSCP > -1)
	{
		ActiveSuperCritterPet* pPetInfo = pData->ppSuperCritterPets[pData->iSummonedSCP];
		Item* pPetItem = inv_GetItemFromBag(pEnt, InvBagIDs_SuperCritterPets, pData->iSummonedSCP, pExtract);

		if (!scp_CheckFlag(pPetItem, kSuperCritterPetFlag_Dead) && (pPetInfo->uiTimeFinishTraining == 0))
		{
			scp_SummonPetInternal(pEnt, pData->iSummonedSCP);
		}
	}
}

// if player should have a SCP summoned, summons it.
void gslSCPPlayerRespawn(Entity* pPlayerEnt)
{
	EntitySavedSCPData* pData = scp_GetEntSCPDataStruct(pPlayerEnt);
	if (pData)
	{
		if (pData->iSummonedSCP >= 0 && pData->erSCP == 0)
		{
			//a pet should be summoned, but there is no pet entity
			scp_SummonPetInternal(pPlayerEnt, pData->iSummonedSCP);
		}
	}
}

void gslHandleSuperCritterPetsAtLogout(Entity *pOwner)
{
	EntitySavedSCPData* pData = scp_GetEntSCPDataStruct(pOwner);

	if(!pData)
	{
		return;
	}

	if(pData->erSCP)
	{
		Entity* pPet = entFromEntityRef(pOwner->iPartitionIdx_UseAccessor, pData->erSCP);
		gslQueueEntityDestroy(pPet);
		pData->erSCP = 0;
	}
}

AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Psaved.Pscpdata, .Pplayer.Playertype, pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
enumTransactionOutcome scp_tr_ChangeSummonedPet(ATR_ARGS, NOCONST(Entity)* pEnt, int iPetIdx, int bSummoned, int bRevive)
{
	if (ISNULL(pEnt) || ISNULL(pEnt->pSaved))
		return TRANSACTION_OUTCOME_FAILURE;
	else
	{
		NOCONST(Item)* pPetItem = inv_trh_GetItemFromBag(ATR_PASS_ARGS, pEnt, 35 /* Literal InvBagIDs_SuperCritterPets */, iPetIdx, NULL);
		NOCONST(EntitySavedSCPData)* pData = scp_trh_GetOrCreateEntSCPDataStruct(ATR_PASS_ARGS, pEnt, true);
		if (scp_trh_CheckFlag(ATR_PASS_ARGS, pPetItem, kSuperCritterPetFlag_Dead) && bSummoned && !bRevive)
		{
			return TRANSACTION_OUTCOME_FAILURE;
		}
		if (bRevive)
			scp_trh_SetFlag(ATR_PASS_ARGS, pPetItem, kSuperCritterPetFlag_Dead, false);

		if (bSummoned)
		{
			pData->iSummonedSCP = iPetIdx;
		}
		else
			pData->iSummonedSCP = -1;
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
	ATR_LOCKS(pEnt, "pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
enumTransactionOutcome scp_tr_KillPet(ATR_ARGS, NOCONST(Entity)* pEnt, int iPetIdx)
{
	if (ISNULL(pEnt) || ISNULL(pEnt->pSaved))
		return TRANSACTION_OUTCOME_FAILURE;
	else
	{
		NOCONST(Item)* pPetItem = inv_trh_GetItemFromBag(ATR_PASS_ARGS, pEnt, 35 /* Literal InvBagIDs_SuperCritterPets */, iPetIdx, NULL);
		if (scp_trh_CheckFlag(ATR_PASS_ARGS, pPetItem, kSuperCritterPetFlag_Dead))
		{
			return TRANSACTION_OUTCOME_FAILURE;
		}
		scp_trh_SetFlag(ATR_PASS_ARGS, pPetItem, kSuperCritterPetFlag_Dead, true);
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Pinventoryv2.Ppinventorybags[], pInventoryV2.ppLiteBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
enumTransactionOutcome scp_tr_ChangeActivePetCostume(ATR_ARGS, NOCONST(Entity)* pEnt, int iPetIdx, int iCostume, GameAccountDataExtract* pExtract)
{
	if (ISNULL(pEnt) || ISNULL(pEnt->pSaved))
		return TRANSACTION_OUTCOME_FAILURE;
	else
	{
		NOCONST(Item)* pPetItem = inv_trh_GetItemFromBag(ATR_PASS_ARGS, pEnt,  35 /* Literal InvBagIDs_SuperCritterPets */, iPetIdx, pExtract);
		if (pPetItem && pPetItem->pSpecialProps && pPetItem->pSpecialProps->pSuperCritterPet)
		{
			if (!iCostume || scp_trh_IsAltCostumeUnlocked(pEnt, iPetIdx, iCostume - 1, pExtract))//costume 0 is "default"
			{
				pPetItem->pSpecialProps->pSuperCritterPet->iCurrentSkin = iCostume;
				return TRANSACTION_OUTCOME_SUCCESS;
			}
		}
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

void scp_PetStateCB(TransactionReturnVal* returnVal, SCPPetStateCBData* pData)
{
	Entity *pPlayerEnt = entFromContainerIDAnyPartition(pData->containerType, pData->containerID);
	EntitySavedSCPData* pSCPData = scp_GetEntSCPDataStruct(pPlayerEnt);

	if(pSCPData)
	{
		if(returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
		{
			U32 uiTime = pmTimestamp(0);
			pmFxStop(pPlayerEnt->pChar->pPowersMovement, 1, 0, kPowerAnimFXType_FromPet, pPlayerEnt->myRef, pPlayerEnt->myRef, uiTime, NULL);

			if(pData->iPetIdx == pSCPData->iSummonedSCP || (pSCPData->iSummonedSCP == -1 && pSCPData->erSCP))
			{
				// This affected the summoned pet, so we should refresh it.  Note that this will reset it's HP, etc.

				//clean up old pet:
				gslQueueEntityDestroy(entFromEntityRef(entGetPartitionIdx(pPlayerEnt), pSCPData->erSCP));
				pSCPData->erSCP = 0;
			 
				//make new pet:
				if (pSCPData->iSummonedSCP >= 0)
				{
					scp_SummonPetInternal(pPlayerEnt, pSCPData->iSummonedSCP);
				}
			}

			entity_SetDirtyBit(pPlayerEnt, parse_SavedEntityData, pPlayerEnt->pSaved, 0);
		}
	}
	SAFE_FREE(pData);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void scp_UnbindPet(Entity* pPlayerEnt, int iBag, int iSlot)
{
	/*SCPPetStateCBData *pData = NULL;
	TransactionReturnVal* pVal = NULL;
	EntitySavedSCPData* pSCPData = scp_GetEntSCPDataStruct(pPlayerEnt);
	int iCost;
	GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pPlayerEnt);
	InventoryBag* pPetBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pPlayerEnt), iBag, pExtract);
	Item* pPetItem = pPetBag ? inv_bag_GetItem(pPetBag, iSlot) : NULL;
	ItemChangeReason reason = {0};
	//*/

	///We are disabling this for now; designers don't want pets retraded.  SIP 2013.3.21
	return;

	/*
	if(!pPetItem)
	{
		return;
	}

	if (character_HasMode(pPlayerEnt->pChar,kPowerMode_Combat))
	{
		//can't change pet state in combat
		notify_NotifySend(pPlayerEnt, kNotifyType_Failed, entTranslateMessageKey(pPlayerEnt, "SuperCritterPets.NoBindInCombat"), NULL, NULL);
		return;
	}

	if (iBag == InvBagIDs_SuperCritterPets)
	{
		ActiveSuperCritterPet* pPetInfo = pSCPData ? eaGet(&pSCPData->ppSuperCritterPets, iSlot) : NULL;

		//Can't unbind dead or mid-training pets.
		if (scp_CheckFlag(pPetItem, kSuperCritterPetFlag_Dead) ||
			(pPetInfo && pPetInfo->uiTimeFinishTraining != 0))
			return;
	}

	pData = calloc(1,sizeof(SCPPetStateCBData));
	pData->containerType = pPlayerEnt->myEntityType;
	pData->containerID = pPlayerEnt->myContainerID;
	if (iBag == InvBagIDs_SuperCritterPets)
	{
		pData->iPetIdx = iSlot;
	}
	pVal = LoggedTransactions_CreateManagedReturnValEnt("SCP_Unbind", pPlayerEnt, scp_PetStateCB, pData);

	//Casey says this is okay here because it needs to call an expression, which is expensive for a transaction.  
	// Ideally it would be fast and in the transaction.
	iCost = scp_EvalUnbindCost(pPlayerEnt, pPetItem);
	inv_FillItemChangeReason(&reason, pPlayerEnt, "SuperCritterPet unbind", "UseNumeric");

	AutoTrans_scp_tr_UnbindPet(pVal, GetAppGlobalType(), pPlayerEnt->myEntityType, pPlayerEnt->myContainerID, iBag, iSlot, iCost, &reason);
	//*/
}

void scp_KillPet(Entity* pPlayerEnt, int iPet)
{
	SCPPetStateCBData *pData = NULL;
	TransactionReturnVal* pVal = NULL;
	pData = calloc(1,sizeof(SCPPetStateCBData));
	pData->containerType = pPlayerEnt->myEntityType;
	pData->containerID = pPlayerEnt->myContainerID;	
	pData->iPetIdx = iPet;
	pVal = LoggedTransactions_CreateManagedReturnValEnt("scp_KillPet", pPlayerEnt, scp_PetStateCB, pData);

	AutoTrans_scp_tr_KillPet(pVal, GetAppGlobalType(), pPlayerEnt->myEntityType, pPlayerEnt->myContainerID, iPet);

}

//move an item from one pet slot to another (on the same pet) (dst may be occupied -> swap)
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void scp_MovePetEquipmentToPet(Entity* pPlayerEnt, int iPet, int iEquipSlotSrc, int iEquipSlotDst)
{
	TransactionReturnVal* pVal = NULL;
	EntityRef* pRef = NULL;
	GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pPlayerEnt);
	Item* pPetItem = inv_GetItemFromBag(pPlayerEnt, InvBagIDs_SuperCritterPets, iPet, pExtract);
	SuperCritterPet *pPet;
	ActiveSuperCritterPet* pActivePet;
	Item* pItemSrc;
	Item* pItemDst;
	EntitySavedSCPData* pSCPData = scp_GetEntSCPDataStruct(pPlayerEnt);

	if (   !pPetItem 
		|| !pPetItem->pSpecialProps 
		|| !pPetItem->pSpecialProps->pSuperCritterPet
		|| !pSCPData	)
		return;
	pPet = pPetItem->pSpecialProps->pSuperCritterPet;
	pActivePet = pSCPData->ppSuperCritterPets[iPet];
	pItemSrc = inv_bag_GetItem(pActivePet->pEquipment, iEquipSlotSrc);
	pItemDst = inv_bag_GetItem(pActivePet->pEquipment, iEquipSlotDst);
	//validate equip action
	if(!pItemSrc || !scp_CanEquip(pPet, iEquipSlotDst, pItemSrc))
		return;
	if(pItemDst && !scp_CanEquip(pPet, iEquipSlotSrc, pItemDst))
		return;

	pRef = calloc(1, sizeof(EntityRef));
	(*pRef) = pPlayerEnt->myRef;
	pVal = LoggedTransactions_CreateManagedReturnValEnt("scp_MovePetEquipmentToPet", pPlayerEnt, scp_PetItemEquippedCB, pRef);
	AutoTrans_scp_tr_MovePetEquipmentToPet(pVal, GetAppGlobalType(), pPlayerEnt->myEntityType, pPlayerEnt->myContainerID, iPet, iEquipSlotSrc, iEquipSlotDst);

}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void scp_MovePetEquipment(Entity* pPlayerEnt, int iPet, int iEquipSlot, int iBagID, int iBagSlot, bool bEquip)
{
	TransactionReturnVal* pVal = NULL;
	EntityRef* pRef = NULL;
	GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pPlayerEnt);
	Item* pPetItem = inv_GetItemFromBag(pPlayerEnt, InvBagIDs_SuperCritterPets, iPet, pExtract);
	SuperCritterPet *pPet;
	Item* pItem = inv_GetItemFromBag(pPlayerEnt, iBagID, iBagSlot, pExtract);
	
	if (   !pPetItem 
		|| !pPetItem->pSpecialProps 
		|| !pPetItem->pSpecialProps->pSuperCritterPet	)
		return;
	pPet = pPetItem->pSpecialProps->pSuperCritterPet;

	//validate equip action
	if( bEquip)
	{
		if(!pItem || !scp_CanEquip(pPet, iEquipSlot, pItem))
			return;
	}
	else
	{
		//unequip can be a swap; must validate the equip part.
		if(pItem && !scp_CanEquip(pPet, iEquipSlot, pItem))
			return;
	}

	pRef = calloc(1, sizeof(EntityRef));
	(*pRef) = pPlayerEnt->myRef;
	pVal = LoggedTransactions_CreateManagedReturnValEnt("scp_MovePetEquipment", pPlayerEnt, scp_PetItemEquippedCB, pRef);

	AutoTrans_scp_tr_MovePetEquipment(pVal, GetAppGlobalType(), pPlayerEnt->myEntityType, pPlayerEnt->myContainerID, iPet, iEquipSlot, iBagID, iBagSlot, bEquip);

}

// wrapper to call scp_resetPetEntInventory on the currently summoned pet.  
/// It would be better if this were called only by the server, everytime something changes,
/// but the runestone slotting/unslotting is called directly from
/// the client and I don't want to mess with the generic gemslotting code flow -SIP
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void scp_resetSummonedPetInventory(Entity *pPlayerEnt)
{
	EntitySavedSCPData* pSCPData = scp_GetEntSCPDataStruct(pPlayerEnt);
	int iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
	ActiveSuperCritterPet* pActivePet = NULL;
	Entity* pPetEnt = NULL;
	F32 temp;

	if (pSCPData && pSCPData->iSummonedSCP >= 0)
	{
		Item* pPetItem = scp_GetActivePetItem(pPlayerEnt, pSCPData->iSummonedSCP);
		pPetEnt = entFromEntityRef(iPartitionIdx, pSCPData->erSCP);
		pActivePet = pSCPData->ppSuperCritterPets[pSCPData->iSummonedSCP];
		scp_resetPetEntInventory(pPlayerEnt, pPetEnt, pActivePet, pSCPData->iSummonedSCP);

		//recalculate and cache the summoned pet's xp bonuses.
		temp = scp_GetBonusXPPercentFromGems(pPlayerEnt, pPetItem);
		if (temp != pSCPData->fCachedPetBonusXPPct)
		{
			pSCPData->fCachedPetBonusXPPct = temp;
			entity_SetDirtyBit(pPlayerEnt, parse_SavedEntityData, pPlayerEnt->pSaved, false);
		}
	}
}

//rebuilds the summoned pet ent's inventory from the active pet inventory on the player. 
// needs to be called to update the summoned pet after equip/runestone changes if the pet doesn't 
// also get resummoned.
static void scp_resetPetEntInventory(Entity* pPlayerEnt, Entity* pPetEnt, ActiveSuperCritterPet* pActivePet, int idx)
{
	if(pPetEnt && pPetEnt->pChar && IS_HANDLE_ACTIVE(pPetEnt->pChar->hClass))
	{
		CharacterClass *pClass = GET_REF(pPetEnt->pChar->hClass);
		DefaultInventory *pInventory = pClass ? GET_REF(pClass->hInventorySet) : NULL;
		DefaultItemDef **ppitemList = NULL;
		InventoryBag* pPlayerBag = pActivePet->pEquipment;
		NOCONST(Entity)* pNCPetEnt = CONTAINER_NOCONST(Entity, pPetEnt);
		NOCONST(InventoryBag)* pPetBag = NULL;
		Item* pPetItem = scp_GetActivePetItem(pPlayerEnt, idx);
		ItemDef* pPetItemDef = GET_REF(pPetItem->hItem);
		CritterCreateParams createParams = {0};
		SuperCritterPet* pPet = pPetItem->pSpecialProps->pSuperCritterPet;
		SuperCritterPetDef *pPetDef = GET_REF(pPet->hPetDef);
		CritterDef *pCritterDef = SAFE_GET_REF(pPetDef, hCritterDef);
		int iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
		int i;



		if(pInventory)
			inv_ent_trh_InitAndFixupInventory(ATR_EMPTY_ARGS,pNCPetEnt,pInventory,true,true,NULL);
		else if(eaSize(&pCritterDef->ppCritterItems) > 0)
			ErrorFilenamef(pCritterDef->pchFileName,"%s Critter has items, but no inventory set! No items will be equipped",pCritterDef->pchName);
		for ( i = 0; i < eaSize(&pPetEnt->pInventoryV2->ppInventoryBags); i++)
		{
			if (invbag_flags(pPetEnt->pInventoryV2->ppInventoryBags[i]) & InvBagFlag_EquipBag)
			{
				pPetBag = CONTAINER_NOCONST(InventoryBag, pPetEnt->pInventoryV2->ppInventoryBags[i]);
				break;
			}
		}

		if (pPetBag)
		{
			inv_bag_trh_ClearBag(ATR_EMPTY_ARGS, pPetBag);

			//add any items from critter def:
			critter_AddNewCritterItems(pCritterDef, pPetEnt, scp_GetPetCombatLevel(pPetItem));

			//Add player-choosen critter inventory:
			for (i = 0; i < eaSize(&pPlayerBag->ppIndexedInventorySlots); i++)
			{
				if (pPlayerBag->ppIndexedInventorySlots[i]->pItem)
					inv_bag_trh_AddItem(ATR_EMPTY_ARGS, pNCPetEnt, NULL, pPetBag, -1, StructCloneDeConst(parse_Item, pPlayerBag->ppIndexedInventorySlots[i]->pItem), 0, NULL, NULL, NULL);
			}
		}
		//also give it its own item so gem powers are applied
		inv_bag_trh_AddItem(ATR_EMPTY_ARGS, pNCPetEnt, NULL, pPetBag, -1, StructCloneDeConst(parse_Item, pPetItem), 0, NULL, NULL, NULL);
		{
			//set the 'quality' of the pet from the itemdef.
			MultiVal mvIntVal = {0};
			MultiValSetInt(&mvIntVal, pPetItemDef->Quality);
			entSetUIVar(pPetEnt, "Quality", &mvIntVal);
		}

		character_ResetPowersArray(iPartitionIdx, pPetEnt->pChar, NULL);

		pPetEnt->pChar->bSkipAccrueMods = false;

		character_DirtyInnateEquip(pPetEnt->pChar);
		character_DirtyInnatePowers(pPetEnt->pChar);
		character_DirtyPowerStats(pPetEnt->pChar);
		character_AccrueMods(iPartitionIdx,pPetEnt->pChar,0.0f,NULL);
		character_DirtyInnateAccrual(pPetEnt->pChar);

		// Start up passives
		character_RefreshPassives(iPartitionIdx, pPetEnt->pChar, NULL);

		character_UpdateMovement(pPetEnt->pChar,NULL);
	}

}

//summons or unsummons a pet.  Client isn't allowed to summon in combat, but after training pet 
// summons even in combat
void scp_SetPetState(Entity* pPlayerEnt, int iPetIdx, bool bSummoned, bool bFromClient)
{
	EntitySavedSCPData* pSCPData = scp_GetEntSCPDataStruct(pPlayerEnt);
	if (!pSCPData || !pPlayerEnt->pChar || iPetIdx < 0 || iPetIdx >= (eaSize(&pSCPData->ppSuperCritterPets)))
		return;

	if (bFromClient && bSummoned && character_HasMode(pPlayerEnt->pChar,kPowerMode_Combat))
	{
		//can't change pet state in combat
		notify_NotifySend(pPlayerEnt, kNotifyType_SuperCritterPet, entTranslateMessageKey(pPlayerEnt, "SuperCritterPets.NoSummonInCombat"), NULL, NULL);
		return;
	}
	else
	{
		ActiveSuperCritterPet* pPetInfo = pSCPData->ppSuperCritterPets[iPetIdx];
		Item* pPetItem = scp_GetActivePetItem(pPlayerEnt, iPetIdx);
		SCPPetStateCBData *pData = NULL;
		TransactionReturnVal* pVal = NULL;
		
		pSCPData->fCachedPetBonusXPPct = scp_GetBonusXPPercentFromGems(pPlayerEnt, pPetItem);

		if (scp_CheckFlag(pPetItem, kSuperCritterPetFlag_MAX) && bSummoned)
		{
			//can't summon dead pets.
			notify_NotifySend(pPlayerEnt, kNotifyType_SuperCritterPet, entTranslateMessageKey(pPlayerEnt, "SuperCritterPets.NoSummonIfDead"), NULL, NULL);
			return;
		}

		if ((pPetInfo->uiTimeFinishTraining != 0) && bSummoned)
		{
			//can't summon dead pets.
			notify_NotifySend(pPlayerEnt, kNotifyType_SuperCritterPet, entTranslateMessageKey(pPlayerEnt, "SuperCritterPets.NoSummonIfTraining"), NULL, NULL);
			return;
		}

		pData = calloc(1,sizeof(SCPPetStateCBData));
		pData->containerType = pPlayerEnt->myEntityType;
		pData->containerID = pPlayerEnt->myContainerID;	
		pData->iPetIdx = iPetIdx;
		pVal = LoggedTransactions_CreateManagedReturnValEnt("scp_SetPetState", pPlayerEnt, scp_PetStateCB, pData);
		AutoTrans_scp_tr_ChangeSummonedPet(pVal, GetAppGlobalType(), pPlayerEnt->myEntityType, pPlayerEnt->myContainerID, iPetIdx, bSummoned, false);
	}
}

void scp_PetDiedForceDismissCurrentPet(Entity* pPlayerEnt)
{
	EntitySavedSCPData* pSCPData = scp_GetEntSCPDataStruct(pPlayerEnt);
	if (pSCPData && pSCPData->iSummonedSCP)
	{
		scp_SetPetState(pPlayerEnt, pSCPData->iSummonedSCP, false, false);
		notify_NotifySend(pPlayerEnt, kNotifyType_SuperCritterPet, 
						entTranslateMessageKey(pPlayerEnt, "SuperCritterPets.DeadPetForcedDismiss"), 
						"DeadPetForcedDismiss", NULL);
	}
}

//wrapper so the client command can't lie and say it's not a client
AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void scp_CmdSetPetState(Entity* pPlayerEnt, int iPetIdx, bool bSummoned)
{
	scp_SetPetState(pPlayerEnt, iPetIdx, bSummoned, true);
}

void scp_PetReviveCB(TransactionReturnVal* returnVal, SCPPetStateCBData* pData)
{
	Entity *pPlayerEnt = entFromContainerIDAnyPartition(pData->containerType, pData->containerID);
	EntitySavedSCPData* pSCPData = scp_GetEntSCPDataStruct(pPlayerEnt);

	if(pPlayerEnt)
	{
		if(returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
		{
			TransactionReturnVal* pVal = NULL;
			ActiveSuperCritterPet* pPetInfo = pSCPData->ppSuperCritterPets[pData->iPetIdx];
			pSCPData->fCachedPetBonusXPPct = scp_GetBonusXPPercentFromGems(pPlayerEnt, scp_GetActivePetItem(pPlayerEnt, pData->iPetIdx));
			pVal = LoggedTransactions_CreateManagedReturnValEnt("scp_PetReviveCB", pPlayerEnt, scp_PetStateCB, pData);
			AutoTrans_scp_tr_ChangeSummonedPet(pVal, GetAppGlobalType(), pPlayerEnt->myEntityType, pPlayerEnt->myContainerID, pData->iPetIdx, true, true);
			return;
		}
	}
	SAFE_FREE(pData);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void scp_RevivePet(Entity* pPlayerEnt, int idx)
{
	SCPPetStateCBData *pData = NULL;
	TransactionReturnVal* pVal = NULL;
	GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pPlayerEnt);
	ItemChangeReason reason = {0};

	inv_FillItemChangeReason(&reason, pPlayerEnt, "Pets:RevivePet", NULL);

	pData = calloc(1,sizeof(SCPPetStateCBData));
	pData->containerType = pPlayerEnt->myEntityType;
	pData->containerID = pPlayerEnt->myContainerID;
	pData->iPetIdx = idx;

	pVal = LoggedTransactions_CreateManagedReturnValEnt("SCP_Unbind", pPlayerEnt, scp_PetReviveCB, pData);

	AutoTrans_inventorybag_RemoveItemByDefName(pVal, GetAppGlobalType(), pPlayerEnt->myEntityType, pPlayerEnt->myContainerID, InvBagIDs_Inventory, g_PetRestrictions.pchRequiredItemForDeceasedPets, 1, &reason, pExtract);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void scp_ChangeCostume(Entity* pPlayerEnt, int idx, int iAltCostumeIdx)
{
	SCPPetStateCBData *pData = NULL;
	TransactionReturnVal* pVal = NULL;
	GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pPlayerEnt);
	pData = calloc(1,sizeof(SCPPetStateCBData));
	pData->containerType = pPlayerEnt->myEntityType;
	pData->containerID = pPlayerEnt->myContainerID;
	pData->iPetIdx = idx;
	pData->bRefresh = true;
	pVal = LoggedTransactions_CreateManagedReturnValEnt("scp_ChangeCostume", pPlayerEnt, scp_PetStateCB, pData);
	AutoTrans_scp_tr_ChangeActivePetCostume(pVal, GetAppGlobalType(), pPlayerEnt->myEntityType, pPlayerEnt->myContainerID, idx, iAltCostumeIdx, pExtract);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void scp_BindPet(Entity* pPlayerEnt, int iDstBag, int iDstSlot, int iSrcBag, int iSrcSlot, int iBackupBagID, const char* pchNewName)
{
	SCPPetStateCBData *pData = NULL;
	TransactionReturnVal* pVal = NULL;

	if (character_HasMode(pPlayerEnt->pChar,kPowerMode_Combat))
	{
		//can't change pet state in combat
		notify_NotifySend(pPlayerEnt, kNotifyType_Failed, entTranslateMessageKey(pPlayerEnt, "SuperCritterPets.NoSummonInCombat"), NULL, NULL);
		return;
	}

	pData = calloc(1,sizeof(SCPPetStateCBData));
	pData->containerType = pPlayerEnt->myEntityType;
	pData->containerID = pPlayerEnt->myContainerID;
	if (iDstBag == InvBagIDs_SuperCritterPets)	// src bag can't be the active pet bag if this is a bind.
	{
		pData->iPetIdx = iDstSlot;
	}
	pVal = LoggedTransactions_CreateManagedReturnValEnt("scp_BindPet", pPlayerEnt, scp_PetStateCB, pData);
	AutoTrans_scp_tr_BindPet(pVal, GetAppGlobalType(), pPlayerEnt->myEntityType, pPlayerEnt->myContainerID, iDstBag, iDstSlot, iSrcBag, iSrcSlot, iBackupBagID, pchNewName);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void scp_MoveBoundPet(Entity* pPlayerEnt, int iDstBag, int iDstSlot, int iSrcBag, int iSrcSlot)
{
	SCPPetStateCBData *pData = NULL;
	TransactionReturnVal* pVal = NULL;
	EntitySavedSCPData* pSCPData = scp_GetEntSCPDataStruct(pPlayerEnt);

	if (!pSCPData)
		return;

	if (character_HasMode(pPlayerEnt->pChar,kPowerMode_Combat) && (iDstBag == InvBagIDs_SuperCritterPets || iSrcBag == InvBagIDs_SuperCritterPets) && iDstBag != iSrcBag)
	{
		//can't change pet state in combat
		notify_NotifySend(pPlayerEnt, kNotifyType_Failed, entTranslateMessageKey(pPlayerEnt, "SuperCritterPets.NoBindInCombat"), NULL, NULL);
		return;
	}

	if (iDstBag == InvBagIDs_SuperCritterPets && iDstSlot != -1) //-1 should mean first empty slot or fail.
	{
		ActiveSuperCritterPet* pPet = eaGet(&pSCPData->ppSuperCritterPets, iDstSlot);
		if (!pPet || pPet->uiTimeFinishTraining > 0)//if we were passed an invalid pet index
			return;
	}
	if (iSrcBag == InvBagIDs_SuperCritterPets)
	{
		ActiveSuperCritterPet* pPet = eaGet(&pSCPData->ppSuperCritterPets, iSrcSlot);
		if (!pPet || pPet->uiTimeFinishTraining > 0)//if we were passed an invalid pet index
			return;
	}
	pData = calloc(1,sizeof(SCPPetStateCBData));
	pData->containerType = pPlayerEnt->myEntityType;
	pData->containerID = pPlayerEnt->myContainerID;
	// either source or dst could be the summoned scp. (if it's both the Transaction will change pSCPData->iSummonedSCP).
	if(iSrcBag == InvBagIDs_SuperCritterPets && iSrcSlot == pSCPData->iSummonedSCP)
	{
		pData->iPetIdx = iSrcSlot;
	}
	else if(iDstBag == InvBagIDs_SuperCritterPets)
	{
		pData->iPetIdx = iDstSlot;

	}
	pVal = LoggedTransactions_CreateManagedReturnValEnt("scp_MoveBoundPet", pPlayerEnt, scp_PetStateCB, pData);
	AutoTrans_scp_tr_MoveBoundPet(pVal, GetAppGlobalType(), pPlayerEnt->myEntityType, pPlayerEnt->myContainerID, iDstBag, iDstSlot, iSrcBag, iSrcSlot);
}


AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void scp_CmdGetPlayerPet(Entity* pPlayerEnt, EntityRef hEnt)
{
	Entity* pEnt = entFromEntityRefAnyPartition(hEnt);
	EntitySavedSCPData* pData = scp_GetEntSCPDataStruct(pEnt);
	Item* pItem;
	if(pData && pData->iSummonedSCP > -1)
	{
		pItem = inv_GetItemFromBag(pEnt, InvBagIDs_SuperCritterPets, pData->iSummonedSCP, NULL);
		if (pItem)
		{
			ClientCmd_scp_CmdRecievePetItem(pPlayerEnt, pItem);
		}
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void scp_SendActivePetForTraining(Entity* pPlayerEnt, int idx)
{
	TransactionReturnVal* pVal = NULL;
	ActiveSuperCritterPet* pActivePet;
	Item* pPetItem = scp_GetActivePetItem(pPlayerEnt, idx);
	SuperCritterPet* pPetInternal = scp_GetPetFromItem(pPetItem);
	S32 iLevelDelta = 0;
	EntitySavedSCPData* pData = scp_GetEntSCPDataStruct(pPlayerEnt);

	if (!pData || character_HasMode(pPlayerEnt->pChar,kPowerMode_Combat))
		return;

	if (character_HasMode(pPlayerEnt->pChar,kPowerMode_Combat))
	{
		//can't change pet state in combat
		notify_NotifySend(pPlayerEnt, kNotifyType_Failed, entTranslateMessageKey(pPlayerEnt, "SuperCritterPets.NotrainInCombat") , NULL, NULL);
		return;
	}
	pActivePet = eaGet(&pData->ppSuperCritterPets, idx);

	if (!pActivePet || scp_CheckFlag(pPetItem, kSuperCritterPetFlag_Dead) || pActivePet->uiTimeFinishTraining > 0)//if we were passed an invalid pet index
		return;

	scp_SetPetState(pPlayerEnt, idx, false, false);

	pActivePet->uiTimeFinishTraining = timeSecondsSince2000() + scp_EvalTrainingTime(pPlayerEnt, pPetItem);
	pActivePet->uiLastLevelUpTransactionRequest = 0;

	pData->bTrainingActive = true;

	entity_SetDirtyBit(pPlayerEnt, parse_SavedEntityData, pPlayerEnt->pSaved, 0);
}

void scp_CheckForFinishedTraining(Entity* pPlayerEnt)
{
	EntitySavedSCPData* pSCPData;

	PERFINFO_AUTO_START_FUNC();
	
	pSCPData = scp_GetEntSCPDataStruct(pPlayerEnt);
	if (pSCPData && pSCPData->bTrainingActive && eaSize(&pSCPData->ppSuperCritterPets) > 0)
	{
		int i;
		GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pPlayerEnt);
		TransactionReturnVal* pVal = NULL;

		for (i = 0; i < eaSize(&pSCPData->ppSuperCritterPets); i++)
		{
			if (pSCPData->ppSuperCritterPets[i]->uiTimeFinishTraining > 0 && 
				pSCPData->ppSuperCritterPets[i]->uiTimeFinishTraining <= timeSecondsSince2000() &&
				pSCPData->ppSuperCritterPets[i]->uiLastLevelUpTransactionRequest+300 <= timeSecondsSince2000())
			{
				//Pet finished training
				Item* pPetItem = scp_GetActivePetItem(pPlayerEnt, i);
				PetTrainingCBData* pData = calloc(1, sizeof(PetTrainingCBData));

				if (!pPetItem){
					pSCPData->ppSuperCritterPets[i]->uiTimeFinishTraining = 0;
					AssertOrAlert("SUPER_CRITTER_PET_TRAINING","Training complete on an empty active super critter pet slot! Users should never be able to empty a slot while training!");
					return;
				}

				pData->erEnt = pPlayerEnt->myRef;
				pData->iPet = i;
				pData->iOldLevel = scp_GetPetCombatLevel(pPetItem);
				pData->iNewLevel = scp_GetPetLevelAfterTraining(pPetItem);
				pSCPData->ppSuperCritterPets[i]->uiLastLevelUpTransactionRequest = timeSecondsSince2000();

				pVal = LoggedTransactions_CreateManagedReturnValEnt("scp_tr_SetActivePetLevel", pPlayerEnt, scp_PetTrainingFinishedCB, pData);
				AutoTrans_scp_tr_SetActivePetLevel(pVal, GetAppGlobalType(), pPlayerEnt->myEntityType, pPlayerEnt->myContainerID, i, pData->iNewLevel, false, pExtract);
			}
		}
	}

	PERFINFO_AUTO_STOP_FUNC();
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void scp_RushTraining(Entity* pPlayerEnt, S32 iIndex)
{
	EntitySavedSCPData* pSCPData = scp_GetEntSCPDataStruct(pPlayerEnt);
	if (pSCPData && eaSize(&pSCPData->ppSuperCritterPets) > iIndex && iIndex >= 0)
	{
		GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pPlayerEnt);
		TransactionReturnVal* pVal = NULL;

		if (pSCPData->ppSuperCritterPets[iIndex]->uiTimeFinishTraining > 0)
		{
			//Pet still training
			Item* pPetItem = scp_GetActivePetItem(pPlayerEnt, iIndex);
			ItemChangeReason reason = {0};
			PetTrainingCBData* pData = calloc(1, sizeof(PetTrainingCBData));
			int iCost = scp_GetRushTrainingCost(pPlayerEnt, iIndex);
			pData->erEnt = pPlayerEnt->myRef;
			pData->iPet = iIndex;
			pData->iOldLevel = scp_GetPetCombatLevel(pPetItem);
			pData->iNewLevel = scp_GetPetLevelAfterTraining(pPetItem);

			inv_FillItemChangeReason(&reason, pPlayerEnt, "SuperCritterPet rush training", "UseNumeric");
			pVal = LoggedTransactions_CreateManagedReturnValEnt("scp_tr_SetActivePetLevel", pPlayerEnt, scp_PetTrainingFinishedCB, pData);
			AutoTrans_scp_tr_RushSetActivePetLevel(pVal, GetAppGlobalType(), pPlayerEnt->myEntityType, pPlayerEnt->myContainerID, iIndex, pData->iNewLevel, iCost, &reason, pExtract);
		}
	}
}

//set summoned pet's level.  Should be used only for debugging and testing.
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(7) ACMD_NAME(SetPetLevel);
void scp_SetPetLevel(Entity* pPlayerEnt, S32 iLevel)
{
	EntitySavedSCPData* pSCPData = scp_GetEntSCPDataStruct(pPlayerEnt);
	if (pSCPData)
	{
		TransactionReturnVal* pVal = NULL;
		GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pPlayerEnt);
		Item* pPetItem = scp_GetActivePetItem(pPlayerEnt, pSCPData->iSummonedSCP);
		int iIndex = pSCPData->iSummonedSCP;

		if (iIndex <0)
		{
			return;
		}

		if (!scp_LevelIsValid(iLevel, pPetItem))
		{
			ItemDef* pPetItemDef = GET_REF(pPetItem->hItem);
			if (pPetItemDef && pPetItemDef->Quality >= 0 && pPetItemDef->Quality < eafSize(&g_SCPConfig.eafMaxLevelsPerQuality))
			{
				iLevel = g_SCPConfig.eafMaxLevelsPerQuality[pPetItemDef->Quality];	//max level for this pet.
			}
		}

		pVal = LoggedTransactions_CreateManagedReturnValEnt("scp_tr_SetActivePetLevel", pPlayerEnt, NULL, NULL);
		AutoTrans_scp_tr_SetActivePetLevel(pVal, GetAppGlobalType(), pPlayerEnt->myEntityType, pPlayerEnt->myContainerID, iIndex, iLevel, true, pExtract);
	}
}

//set summoned pet's level based on your level.
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(7) ACMD_NAME(PetLevelToMe);
void scp_PetLevelToPlayerLevel(Entity* pPlayerEnt)
{
	EntitySavedSCPData* pData = scp_GetEntSCPDataStruct(pPlayerEnt);
	Item* pPetItem = pData ? scp_GetActivePetItem(pPlayerEnt, pData->iSummonedSCP) : NULL;
	if (pPetItem)
		scp_SetPetLevel(pPlayerEnt, scp_GetPetStartLevelForPlayerLevel(pPlayerEnt->pChar->iLevelExp, pPetItem));
}

//rename your pet. iIndex is the index of your active pet slots.
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void scp_Rename(Entity* pPlayerEnt, S32 iPetIdx, char* pchNewName)
{
	EntitySavedSCPData* pSCPData = scp_GetEntSCPDataStruct(pPlayerEnt);
	if (pSCPData && eaSize(&pSCPData->ppSuperCritterPets) > iPetIdx && iPetIdx >= 0)
	{
		GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pPlayerEnt);
		TransactionReturnVal* pVal = LoggedTransactions_CreateManagedReturnValEnt("scp_tr_RenamePet", pPlayerEnt, NULL, NULL);
		ItemChangeReason reason = {0};
		inv_FillItemChangeReason(&reason, pPlayerEnt, "SuperCritterPet rename", "UseNumeric");
		AutoTrans_scp_tr_RenamePet(pVal, GetAppGlobalType(), pPlayerEnt->myEntityType, pPlayerEnt->myContainerID, InvBagIDs_SuperCritterPets, iPetIdx, pchNewName, &reason, pExtract);
	}
}

AUTO_COMMAND_REMOTE;
void scp_PetGainedXP_CB(int iSlot, int iDelta, CmdContext *pContext)
{
	Entity *pEnt = entFromContainerIDAnyPartition(pContext->clientType, pContext->clientID);
	if (pEnt)
	{
		Item* pPetItem = scp_GetActivePetItem(pEnt, iSlot);
		if (pPetItem && pPetItem->pSpecialProps && pPetItem->pSpecialProps->pSuperCritterPet)
		{
			U32 uPetXP = pPetItem->pSpecialProps->pSuperCritterPet->uXP;
			if (scp_PetXPToLevelLookup(uPetXP, pPetItem) != scp_PetXPToLevelLookup(uPetXP - iDelta, pPetItem))
			{
				ClientCmd_NotifySend(pEnt, kNotifyType_TrainingAvailable, entTranslateMessageKey(pEnt, "SuperCritterPets.TrainingAvailable"), REF_STRING_FROM_HANDLE(pPetItem->hItem), NULL);
				//Pet is ready to level up!
				eventsend_RecordLevelUpPet(pEnt);
			}
		}
	}
}

#include "AutoGen/gslsupercritterpet_c_ast.c"