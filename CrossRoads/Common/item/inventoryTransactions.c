#include "EntityLib.h"
#include "AutoGen/Entity_h_ast.h"
#include "EntitySavedData.h"
#include "AutoGen/EntitySavedData_h_ast.h"
#include "Character.h"
#include "CombatConfig.h"
#include "CostumeCommonEntity.h"
#include "CostumeCommonLoad.h"
#include "CostumeCommonTailor.h"
#include "EntityBuild.h"
#include "EntityBuild_h_ast.h"
#include "Player.h"
#include "Player_h_ast.h"

#include "GameAccountDataCommon.h"
#include "GameStringFormat.h"
#include "team.h"
#include "Guild.h"
#include "AutoGen/guild_h_ast.h"
#include "itemCommon.h"
#include "rewardCommon.h"
#include "reward.h"
#include "mission_common.h"
#include "gslEventSend.h"
#include "gslSendToClient.h"
#include "tradeCommon.h"
#include "objTransactions.h"
#include "AutoTransDefs.h"
#include "LoggedTransactions.h"
#include "CharacterAttribs.h"
#include "CharacterClass.h"
#include "gslContact.h"
#include "inventoryCommon.h"
#include "interaction_common.h"
#include "AlgoItem.h"
#include "rand.h"
#include "gslSocial.h"
#include "OfficerCommon.h"
#include "StringCache.h"
#include "itemTransaction.h"
#include "PowerHelpers.h"
#include "PowersMovement.h"
#include "SavedPetCommon.h"
#include "SavedPetTransactions.h"
#include "containerTrade.h"
#include "GamePermissionsCommon.h"
#include "inventoryTransactions.h"
#include "tradeCommon_h_ast.h"
#include "itemupgrade.h"
#include "gslSuperCritterPet.h"

#include "NotifyCommon.h"
#include "Powers.h"
#include "gslActivityLog.h"
#include "logging.h"

#include "gslInteractable.h"
#include "../StaticWorld/WorldCellEntry.h"

#ifdef GAMESERVER
#include "gslCostume.h"
#include "gslMapVariable.h"
#include "gslPartition.h"
#include "GameServerLib.h"
#endif

#include "AutoGen/inventoryTransactions_c_ast.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/inventoryCommon_h_ast.h"
#include "AutoGen/containerTrade_h_ast.h"
#include "ItemEnums.h"
#include "gslGamePermissions.h"
#include "gslItemAssignments.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

AUTO_TRANS_HELPER;
bool inv_trh_ItemGemCheckItemIDs(ATR_ARGS,
									ATH_ARG NOCONST(InventoryBag) *pSrcBag, S32 iSrcSlot, U64 uSrcExpectedItemID,
									 ATH_ARG NOCONST(InventoryBag)* pDstBag, S32 iDstSlot, S32 iDestGemSlot, U64 uDstExpectedItemID)
{
	NOCONST(InventorySlot)* pSrcSlot = inv_trh_GetSlotFromBag(ATR_PASS_ARGS, pSrcBag, iSrcSlot);
	NOCONST(InventorySlot)* pDstSlot = inv_trh_GetSlotFromBag(ATR_PASS_ARGS, pDstBag, iDstSlot);

	U64 uSrcActualItemID = 0;
	U64 uDstActualItemID = 0;

	if (NONNULL(pSrcSlot) && NONNULL(pSrcSlot->pItem))
	{
		uSrcActualItemID = pSrcSlot->pItem->id;
	}
	if (NONNULL(pDstSlot) && NONNULL(pDstSlot->pItem))
	{
		uDstActualItemID = pDstSlot->pItem->id;
	}
	// Must have a valid source ID, and actual and expected IDs must match
	if (!uSrcActualItemID || uSrcActualItemID != uSrcExpectedItemID)
	{
		return false;
	}
	// Only fail if the actual destination ID is valid and it doesn't match the expected ID
	if (!uDstActualItemID || uDstActualItemID != uDstExpectedItemID)
	{
		return false;
	}

	return true;
}

AUTO_TRANS_HELPER;
bool inv_trh_ItemMoveCheckItemIDs(ATR_ARGS,
								  ATH_ARG NOCONST(InventoryBag)* pSrcBag, S32 iSrcSlot, U64 uSrcExpectedItemID,
								  ATH_ARG NOCONST(InventoryBag)* pDstBag, S32 iDstSlot, U64 uDstExpectedItemID)
{
	NOCONST(InventorySlot)* pSrcSlot = inv_trh_GetSlotFromBag(ATR_PASS_ARGS, pSrcBag, iSrcSlot);
	NOCONST(InventorySlot)* pDstSlot = inv_trh_GetSlotFromBag(ATR_PASS_ARGS, pDstBag, iDstSlot);
	U64 uSrcActualItemID = 0;
	U64 uDstActualItemID = 0;

	if (NONNULL(pSrcSlot) && NONNULL(pSrcSlot->pItem))
	{
		uSrcActualItemID = pSrcSlot->pItem->id;
	}
	if (NONNULL(pDstSlot) && NONNULL(pDstSlot->pItem))
	{
		uDstActualItemID = pDstSlot->pItem->id;
	}
	// Must have a valid source ID, and actual and expected IDs must match
	if (!uSrcActualItemID || uSrcActualItemID != uSrcExpectedItemID)
	{
		return false;
	}
	// Only fail if the actual destination ID is valid and it doesn't match the expected ID
	if (uDstActualItemID && uDstActualItemID != uDstExpectedItemID)
	{
		return false;
	}
	return true;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, "pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
bool inv_bag_trh_ItemResetChargesUsed(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, int iItemPowExpiredBag, int iItemPowExpiredSlot, GameAccountDataExtract *pExtract)
{
	NOCONST(InventoryBag)* pBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt,iItemPowExpiredBag, pExtract);
	if (NONNULL(pBag))
	{
		NOCONST(InventorySlot)* pSlot = inv_trh_GetSlotPtr(ATR_PASS_ARGS,pBag,iItemPowExpiredSlot);
		if (NONNULL(pSlot) && NONNULL(pSlot->pItem) && pSlot->pItem->count > 0)
		{
			// Charges used on Powers for stack left in the slot is reset to 0
			int PowerIdx, NumPowers = eaSize(&pSlot->pItem->ppPowers);
			for(PowerIdx=0; PowerIdx<NumPowers; PowerIdx++)
			{
				power_SetChargesUsedHelper(pSlot->pItem->ppPowers[PowerIdx], 0);
			}
			return true;
		}
	}
	return false;
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".pInventoryV2.Peaowneduniqueitems, pInventoryV2.ppLiteBags[], .Psaved.Conowner.Containertype, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, pInventoryV2.ppInventoryBags[], .Pchar.Ilevelexp");
enumTransactionOutcome inv_ent_tr_RemoveItemEx(	ATR_ARGS, NOCONST(Entity)* pEnt, int BagID, int SlotIdx, U64 uItemID, int Count,
												int iItemPowExpiredBag, int iItemPowExpiredSlot, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	NOCONST(Item)* pItem;
	
	if (!inv_bag_trh_ItemResetChargesUsed(ATR_PASS_ARGS,pEnt,iItemPowExpiredBag,iItemPowExpiredSlot, pExtract))
	{
		TRANSACTION_RETURN_LOG_FAILURE(
			"Item Remove Failed To Reset Charges on Item in Bag:Slot [%s:%i] on Ent [%s]", 
			StaticDefineIntRevLookup(InvBagIDsEnum,BagID), SlotIdx,
			pEnt->debugName );
	}

	pItem = inv_trh_GetItemFromBag(ATR_PASS_ARGS, pEnt, BagID, SlotIdx, pExtract);
	if (ISNULL(pItem) || pItem->id != uItemID)
		return TRANSACTION_OUTCOME_FAILURE;

	pItem = CONTAINER_NOCONST(Item, invbag_RemoveItem(ATR_PASS_ARGS, pEnt, false, BagID, SlotIdx, Count, pReason, pExtract));
	if (ISNULL(pItem))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}
	StructDestroyNoConst(parse_Item, pItem);
	return TRANSACTION_OUTCOME_SUCCESS;
}

//transaction to remove an item from a bag
AUTO_TRANSACTION
ATR_LOCKS(pGuild, ".Pcname")
ATR_LOCKS(pGuildBank, ".pInventoryV2.Ppinventorybags[]");
enumTransactionOutcome inv_guild_tr_RemoveItem(ATR_ARGS, NOCONST(Guild)* pGuild, NOCONST(Entity) *pGuildBank, int BagID, int SlotIdx, int Count, ItemChangeReason *pReason)
{
	NOCONST(InventoryBag)* pBag = inv_guildbank_trh_GetBag(ATR_PASS_ARGS,pGuildBank,BagID);
	NOCONST(Item)* pItem = NULL;
	ItemDef *pItemDef = NULL;

	pItem = inv_bag_trh_RemoveItem(ATR_PASS_ARGS, NULL, false, pBag, SlotIdx, Count, pReason);

	if (ISNULL(pBag)) {
		TRANSACTION_RETURN_LOG_FAILURE("Item Remove Failed Bag:Slot [%s:%d] on Guild [%s]: failed to find bag", StaticDefineIntRevLookup(InvBagIDsEnum,BagID), SlotIdx, pGuild->pcName);
	}

	if(ISNULL(pItem)) {
		TRANSACTION_RETURN_LOG_FAILURE("Item Remove Failed Bag:Slot [%s:%d] on Guild [%s]: failed to find item", StaticDefineIntRevLookup(InvBagIDsEnum,BagID), SlotIdx, pGuild->pcName);
	}

	pItemDef = GET_REF(pItem->hItem);

	TRANSACTION_APPEND_LOG_SUCCESS("Item [%s<%"FORM_LL"u>]x%i successfully removed from bag:slot [%s:%i] on Guild [%s] and destroyed", 
		pItemDef ? pItemDef->pchName : pItem->pchDisplayName, pItem->id, Count, StaticDefineIntRevLookup(InvBagIDsEnum, BagID), SlotIdx, pGuild->pcName);

	//destroy the removed item
	//!!!! this could possibly be passed back to the caller instead
	StructDestroyNoConst(parse_Item, pItem);

	return TRANSACTION_OUTCOME_SUCCESS;
}

//transaction to remove an item specified by a def name from a bag
AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".pInventoryV2.Peaowneduniqueitems, pInventoryV2.ppLiteBags[], .Psaved.Conowner.Containerid, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containertype, pInventoryV2.ppInventoryBags[], .Pchar.Ilevelexp");
enumTransactionOutcome inventorybag_RemoveItemByDefName(ATR_ARGS, NOCONST(Entity)* pEnt, int BagID, char* ItemDefName, int Count, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	S32 res = invbag_RemoveItemByDefName(ATR_PASS_ARGS, pEnt, BagID, ItemDefName, Count, pReason, pExtract );

	if ( !res ) 
	{
		TRANSACTION_RETURN_LOG_FAILURE(
			"Item [%s] Remove Failed Bag [%s] on Ent [%s]: Failed to find item",
			ItemDefName,
			StaticDefineIntRevLookup(InvBagIDsEnum,BagID), 
			pEnt->debugName );
	}

	TRANSACTION_APPEND_LOG_SUCCESS("Item [%s]x%i successfully removed from bag [%s] on entity [%s] and destroyed", 
		ItemDefName, Count, StaticDefineIntRevLookup(InvBagIDsEnum, BagID), pEnt->debugName);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".pInventoryV2.Peaowneduniqueitems, pInventoryV2.ppLiteBags[], .Psaved.Conowner.Containerid, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containertype, pInventoryV2.ppInventoryBags[], .Pchar.Ilevelexp");
enumTransactionOutcome inventorybag_RemoveItemByDefNameEx(ATR_ARGS, NOCONST(Entity)* pEnt, int BagID, char* ItemDefName, int Count,
													int iItemPowExpiredBag, int iItemPowExpiredSlot, 
													const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	if (!inv_bag_trh_ItemResetChargesUsed(ATR_PASS_ARGS,pEnt,iItemPowExpiredBag,iItemPowExpiredSlot, pExtract))
	{
		TRANSACTION_RETURN_LOG_FAILURE(
			"Item Remove Failed To Reset Charges on Item %s in Bag %s on Ent %s", 
			ItemDefName,
			StaticDefineIntRevLookup(InvBagIDsEnum,BagID),
			pEnt->debugName );
	}

	return inventorybag_RemoveItemByDefName(ATR_PASS_ARGS, pEnt, BagID, ItemDefName, Count, pReason, pExtract);
}

//transaction to remove an item specified by an item ID name from a bag
AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".pInventoryV2.Peaowneduniqueitems, pInventoryV2.ppLiteBags, .Psaved.Conowner.Containertype, .pInventoryV2.Ppinventorybags, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Pchar.Ilevelexp");
enumTransactionOutcome inv_ent_tr_RemoveItemByID(ATR_ARGS, NOCONST(Entity)* pEnt, U64 itemID,  int Count, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	if (!inv_RemoveItemByID(ATR_PASS_ARGS, pEnt, itemID, Count, 0, pReason, pExtract))
	{
		TRANSACTION_RETURN_LOG_FAILURE(
			"Item Remove by ID [%"FORM_LL"u] Failed for entity [%s]",
			itemID, pEnt->debugName);
	}

	TRANSACTION_RETURN_LOG_SUCCESS(
		"Item Remove by ID [%"FORM_LL"u] Succeded for entity [%s]",
		itemID, pEnt->debugName);
}


AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.ppInventoryBags[], .Psaved.Savedname, .Pplayer.Publicaccountname, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[], pInventoryV2.ppLiteBags[]")
ATR_LOCKS(pGuild, ".Eamembers, .Uoldbanklogidx, .Eabanklog")
ATR_LOCKS(pGuildBank, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[], .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Hallegiance, .Hsuballegiance");
enumTransactionOutcome inv_ent_tr_MoveNumericToGuild(ATR_ARGS, NOCONST(Entity)* pEnt, NOCONST(Guild) *pGuild, NOCONST(Entity) *pGuildBank, int iSrcBagID, int iDstBagID, const char *pcNumeric, int iCount, int iEPValue, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	NOCONST(InventoryBagLite)* pSrcBag = inv_trh_GetLiteBag(ATR_PASS_ARGS, pEnt,iSrcBagID, pExtract);
	NOCONST(InventoryBagLite)* pDstBag = inv_guildbank_trh_GetLiteBag(ATR_PASS_ARGS,pGuildBank,iDstBagID);
	ItemDef *pItemDef = RefSystem_ReferentFromString(g_hItemDict, pcNumeric);

	bool bSuccess =
		NONNULL(pItemDef) && (pItemDef->flags & kItemDefFlag_Tradeable) != 0 && iCount != 0 &&
		(iCount > 0 || inv_guildbank_trh_UpdateBankTabWithdrawLimit(ATR_PASS_ARGS, pEnt->myContainerID, pGuild, pGuildBank, iDstBagID, -iCount, iEPValue*(-iCount))) &&
		inv_lite_trh_AddNumeric(ATR_PASS_ARGS, pGuildBank, true, pDstBag, pcNumeric, iCount, pReason) &&
		inv_lite_trh_AddNumeric(ATR_PASS_ARGS, pEnt, false, pSrcBag, pcNumeric, -iCount, pReason);
	
	
	if (!bSuccess) {
		TRANSACTION_RETURN_LOG_FAILURE("Failed to Move %d Numeric [%s] to Guild Bag [%s] from Ent [%s]",
			iCount, pcNumeric, StaticDefineIntRevLookup(InvBagIDsEnum, iSrcBagID), pEnt->debugName);
	}
	
	if (iCount > 0) {
		inv_guild_trh_AddLog(ATR_PASS_ARGS, pEnt, pGuild, NULL, pcNumeric, pDstBag->BagID, iCount);
	} else {
		inv_guild_trh_AddLog(ATR_PASS_ARGS, pEnt, pGuild, NULL, pcNumeric, pSrcBag->BagID, iCount);
	}
	TRANSACTION_RETURN_LOG_SUCCESS("%d Numeric [%s] Moved to Guild Bag [%s] from Ent [%s]",
		iCount, pcNumeric, StaticDefineIntRevLookup(InvBagIDsEnum, iSrcBagID), pEnt->debugName);
}

AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pinventoryv2.Ppinventorybags[], .Pplayer.Pugckillcreditlimit, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Hallegiance, .Hsuballegiance, pInventoryV2.ppLiteBags[]")
	ATR_LOCKS(pGuild, ".Pcname")
	ATR_LOCKS(pGuildBank, "pInventoryV2.ppLiteBags[]");
enumTransactionOutcome inv_guild_tr_ExchangeNumerics(ATR_ARGS, NOCONST(Entity)* pEnt, NOCONST(Guild)* pGuild, NOCONST(Entity)* pGuildBank, S32 iBagID, const char *pcSrcNumeric, const char *pcDstNumeric, S32 iSrcCount, S32 iDstCount, const ItemChangeReason *pReason)
{
	NOCONST(InventoryBagLite)* pBag = inv_guildbank_trh_GetLiteBag(ATR_PASS_ARGS,pGuildBank,iBagID);
	
	if (NONNULL(pBag) &&
		inv_lite_trh_AddNumeric(ATR_PASS_ARGS, pEnt, true, pBag, pcSrcNumeric, -iSrcCount, pReason) &&
		inv_lite_trh_AddNumeric(ATR_PASS_ARGS, pEnt, true, pBag, pcDstNumeric, iDstCount, pReason)) {
		
		TRANSACTION_RETURN_LOG_SUCCESS("Exchanging %d Numeric %s for %d Numeric %s on Guild %s",
			iSrcCount, pcSrcNumeric, iDstCount, pcDstNumeric, pGuild->pcName);
	}
	
	TRANSACTION_RETURN_LOG_FAILURE("Exchanging %d Numeric %s for %d Numeric %s on Guild %s failed",
		iSrcCount, pcSrcNumeric, iDstCount, pcDstNumeric, pGuild->pcName);
}

AUTO_TRANSACTION
    ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pinventoryv2.Ppinventorybags[], .Pplayer.Pugckillcreditlimit, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Hallegiance, .Hsuballegiance, pInventoryV2.ppLiteBags[]")
    ATR_LOCKS(pGuild, ".Pcname")
    ATR_LOCKS(pGuildBank, "pInventoryV2.ppLiteBags[]");
enumTransactionOutcome inv_guild_tr_PlayerBuyGuildBankTab(ATR_ARGS, NOCONST(Entity)* pEnt, NOCONST(Guild)* pGuild, NOCONST(Entity)* pGuildBank, S32 iBagID, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
    const char *pcCurrencyNumeric = guildBankConfig_GetBankTabPurchaseNumericName();
    const char *pcBankTabNumeric = guildBankConfig_GetBankTabUnlockNumericName();
    NOCONST(InventoryBagLite)* pGuildBag = inv_guildbank_trh_GetLiteBag(ATR_PASS_ARGS, pGuildBank, iBagID);
    NOCONST(InventoryBagLite)* pPlayerBag = inv_trh_GetLiteBag(ATR_PASS_ARGS, pEnt, iBagID, pExtract);
    S32 currentTabCount = inv_trh_GetNumericValue(ATR_PASS_ARGS, pGuildBank, pcBankTabNumeric);
    U32 tabCost = guildBankConfig_GetBankTabPurchaseCost(currentTabCount);

    if (NONNULL(pGuildBag) && NONNULL(pPlayerBag) &&
        inv_lite_trh_AddNumeric(ATR_PASS_ARGS, pEnt, true, pPlayerBag, pcCurrencyNumeric, -(int)tabCost, pReason) &&
        inv_lite_trh_AddNumeric(ATR_PASS_ARGS, pEnt, true, pGuildBag, pcBankTabNumeric, 1, pReason)) 
    {

        TRANSACTION_RETURN_LOG_SUCCESS("Exchanging %d Numeric %s from player for %d Numeric %s to Guild %s",
            tabCost, pcCurrencyNumeric, 1, pcBankTabNumeric, pGuild->pcName);
    }

    TRANSACTION_RETURN_LOG_FAILURE("Exchanging %d Numeric %s from player for %d Numeric %s on Guild %s failed",
        tabCost, pcCurrencyNumeric, 1, pcBankTabNumeric, pGuild->pcName);
}

//transaction to move an item between two bag slots on the same entity
AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Psaved.Ppuppetmaster.Curtype, .Pplayer.Playertype, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .pInventoryV2.Peaowneduniqueitems, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[]");
enumTransactionOutcome inv_ent_tr_MoveItem(ATR_ARGS, NOCONST(Entity)* pEnt, int SrcBagID, int SrcSlotIdx, U64 uSrcItemID, int DestBagID, int DestSlotIdx, U64 uDstItemID, int Count, const ItemChangeReason *pSrcReason, const ItemChangeReason *pDestReason, GameAccountDataExtract *pExtract)
{
	NOCONST(InventoryBag)* pSrcBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt,SrcBagID, pExtract);
	NOCONST(InventoryBag)* pDestBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt,DestBagID, pExtract);
	Item *pItem = invbag_GetItem(ATR_PASS_ARGS, pEnt, SrcBagID, SrcSlotIdx, pExtract);
	ItemDef *pDef = pItem ? GET_REF(pItem->hItem) : NULL;
	U64 itemID = NONNULL(pItem) ? pItem->id: 0;
	
	if (!inv_trh_ItemMoveCheckItemIDs(ATR_PASS_ARGS, pSrcBag, SrcSlotIdx, uSrcItemID, pDestBag, DestSlotIdx, uDstItemID) ||
		!inv_MoveItem(ATR_PASS_ARGS, pEnt, NULL, pDestBag, DestSlotIdx, pEnt, NULL, pSrcBag, SrcSlotIdx, Count, ItemAdd_Silent, pSrcReason, pDestReason, pExtract) )
	{
		TRANSACTION_RETURN_LOG_FAILURE(
			"Item Move Failed: Item [%s<%"FORM_LL"u>]x%i from Bag:Slot [%s:%d] to Bag:Slot [%s:%d] on Ent [%s]", 
			pDef ? pDef->pchName : "?",
			itemID, Count,
			StaticDefineIntRevLookup(InvBagIDsEnum,SrcBagID), SrcSlotIdx,
			StaticDefineIntRevLookup(InvBagIDsEnum,DestBagID), DestSlotIdx,
			pEnt->debugName );
	}
	
	TRANSACTION_RETURN_LOG_SUCCESS(
		"Item Moved: Item [%s<%"FORM_LL"u>]x%i from Entity Bag:Slot [%s:%d] to Entity Bag:Slot [%s:%d] on Ent [%s]", 
		pDef ? pDef->pchName : "?",
		itemID, Count,
		StaticDefineIntRevLookup(InvBagIDsEnum,SrcBagID), SrcSlotIdx,
		StaticDefineIntRevLookup(InvBagIDsEnum,DestBagID), DestSlotIdx,
		pEnt->debugName );
	StructDestroySafe(parse_Item,&pItem);
}

AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Psaved.Ppallowedcritterpets, .Pplayer.Playertype, .Psaved.Ppownedcontainers, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Itemidmax, .Hallegiance, .Hsuballegiance, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Pinventoryv2.Peaowneduniqueitems, pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[]");
enumTransactionOutcome inv_ent_tr_UnGemItem(ATR_ARGS, NOCONST(Entity)* pEnt, int SrcBagID, int SrcSlotIdx, U64 uSrcItemID, int iGemSlotIdx, int DestBagID, int DestSlotIdx, const char *pchCostNumeric, int iCost, const ItemChangeReason *pSrcReason, GameAccountDataExtract *pExtract)
{
	NOCONST(InventoryBag)* pSrcBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt,SrcBagID, pExtract);
	NOCONST(InventoryBag)* pDestBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt,DestBagID, pExtract);
	Item *pSrcItem = invbag_GetItem(ATR_PASS_ARGS, pEnt, SrcBagID, SrcSlotIdx, pExtract);
	const char *pDestDebugName = NULL;

	if(NONNULL(pEnt) && NONNULL(pEnt->debugName))
	{
		pDestDebugName = pEnt->debugName;
	}

	if(!inv_trh_UnGemItem(ATR_PASS_ARGS,pEnt,pSrcBag,SrcSlotIdx,uSrcItemID,iGemSlotIdx,pDestBag,DestSlotIdx,pchCostNumeric,iCost,pExtract,pSrcReason))
	{
		TRANSACTION_RETURN_LOG_FAILURE("Item ungem didn't work");
	}

	TRANSACTION_RETURN_LOG_SUCCESS("Item Ungemmed");
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Psaved.Ppuppetmaster.Curtype, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, .Pplayer.Playertype, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[]");
enumTransactionOutcome inv_ent_tr_GemItem(ATR_ARGS, NOCONST(Entity)* pEnt, int SrcBagID, int SrcSlotIdx, U64 uSrcItemID, int DestBagID, int DestSlotIdx, U64 uDstItemID, S32 uDestGemIdx, const ItemChangeReason *pSrcReason, const ItemChangeReason *pDestReason, GameAccountDataExtract *pExtract)
{
	NOCONST(InventoryBag)* pSrcBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt,SrcBagID, pExtract);
	NOCONST(InventoryBag)* pDestBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt,DestBagID, pExtract);
	Item *pGemItem = invbag_GetItem(ATR_PASS_ARGS, pEnt, SrcBagID, SrcSlotIdx, pExtract);
	Item *pDestItem = invbag_GetItem(ATR_PASS_ARGS, pEnt, DestBagID, DestSlotIdx, pExtract);
	Item *pHoldingItem = NULL;
	ItemDef *pDef = pGemItem ? GET_REF(pGemItem->hItem) : NULL;
	const char *pDestDebugName = NULL;

	if(NONNULL(pEnt) && NONNULL(pEnt->debugName))
	{
		pDestDebugName = pEnt->debugName;
	}

	if(!inv_trh_ItemGemCheckItemIDs(ATR_PASS_ARGS,pSrcBag,SrcSlotIdx,uSrcItemID,pDestBag,DestSlotIdx,uDestGemIdx,uDstItemID) ||
		!inv_trh_MoveGemItem(ATR_PASS_ARGS,NULL,pDestBag,DestSlotIdx,uDestGemIdx,uDstItemID,pEnt,NULL,pSrcBag,SrcSlotIdx,uSrcItemID,1,0,NULL,pSrcReason,pExtract,pDestDebugName))
	{
		TRANSACTION_RETURN_LOG_FAILURE(
			"Gem Item Failed: Item [%s<%"FORM_LL"u>]x%i from Bag:Slot [%s:%d] to Bag:Slot [%s:%d] on Ent [%s]", 
			pDef ? pDef->pchName : "?",
			uSrcItemID, 1,
			StaticDefineIntRevLookup(InvBagIDsEnum,SrcBagID), SrcSlotIdx,
			StaticDefineIntRevLookup(InvBagIDsEnum,DestBagID), DestSlotIdx,
			pEnt->debugName );
	}

	TRANSACTION_RETURN_LOG_SUCCESS("Item Moved: Item [%s<%"FORM_LL"u>]x%i from Entity Bag:Slot [%s:%d] to Entity Bag:Slot [%s:%d] on Ent [%s]", 
		pDef ? pDef->pchName : "?",
		uSrcItemID, 1,
		StaticDefineIntRevLookup(InvBagIDsEnum,SrcBagID), SrcSlotIdx,
		StaticDefineIntRevLookup(InvBagIDsEnum,DestBagID), DestSlotIdx,
		pEnt->debugName );
}

AUTO_TRANS_HELPER;
bool inv_bag_trh_UpgradeItem(ATR_ARGS, ATH_ARG NOCONST(Entity)*pEnt, ATH_ARG NOCONST(InventoryBag) *pSrcBag, int iSrcSlot, U64 uSrcItemID, ATH_ARG NOCONST(InventoryBag) *pModBag, int iModSlotIdx, U32 uModItemID, ItemDef *pModDef, F32 fChance, int eSkillTypeUpgrade, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	NOCONST(Item) *pSrcItem;
	NOCONST(Item) *pModItem;
	NOCONST(InventorySlot)* pModSlot = NULL;
	NOCONST(InventorySlot)* pSrcSlot = NULL;
	ItemDef *pSrcItemDef = NULL;
	ItemDef *pModItemDef = NULL;
	ItemDef *pUpgradeDef = NULL;
	int iRequiredToUpgrade = 0;
	int iCount = 0;
	ItemUpgradeModifiers *pModifierDef = NULL;

	if(pSrcBag->BagID == InvBagIDs_Buyback)
	{
		TRANSACTION_APPEND_LOG_FAILURE("Src bag may not be a buyback bag.");
		return false;
	}

	pSrcSlot = eaGet(&pSrcBag->ppIndexedInventorySlots, iSrcSlot);
	pSrcItemDef = SAFE_GET_REF2(pSrcSlot,pItem,hItem);
	if (ISNULL(pSrcItemDef) || ISNULL(pSrcSlot->pItem)) {
		// There is no provision for an unspecified or empty source slot
		return false;
	}

	pSrcItem = pSrcSlot->pItem;

	if(uModItemID)
	{
		pModSlot = eaGet(&pModBag->ppIndexedInventorySlots, iModSlotIdx);
		pModItemDef = SAFE_GET_REF2(pModSlot,pItem,hItem);
		if (ISNULL(pModItemDef) || ISNULL(pModSlot->pItem)) {
			// There is no provision for an unspecified or empty source slot
			return false;
		}

		pModItem = pModSlot->pItem;
	}
	else if(pModDef)
	{
		pModItemDef = pModDef;
	}

	iCount = pSrcItem->count;
	pUpgradeDef = itemUpgrade_GetUpgrade(pEnt,pSrcItemDef,&iCount,pModDef);

	if(!pUpgradeDef)
	{
		//There is no possible upgrade to make
		return false;
	}

	if(pModDef)
	{
		// Get the modifiers
		pModifierDef = itemUpgrade_FindModifier(pModDef);
	}

	//Is there a chance of failure?
	if(fChance < 1.0)
	{
		F32 fRandValue = randomPositiveF32();

		if(fRandValue > fChance)
		{
			// Always consume the modifier on failure
			if(uModItemID)
			{
				inv_bag_trh_RemoveItem(ATR_PASS_ARGS,pEnt,false,pModBag,iModSlotIdx,1,pReason);
			}

			if(!pModifierDef || !pModifierDef->bNoLossOnFailure)
			{
				inv_bag_trh_RemoveItem(ATR_PASS_ARGS,pEnt,false,pSrcBag,iSrcSlot,1,pReason);
				QueueRemoteCommand_gslItemUpgrade_SetLastResult(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID,pEnt->myContainerID,pSrcItemDef,pUpgradeDef,kItemUpgradeResult_Failure);
				TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GEMS, "ItemUpgrade",
					"EntID %d OldItem \"%s\" OldItemCount %d NewItem \"%s\" Modifier \"%s\" ItemTier %d Result \"Failure\"",
					pEnt->myContainerID, pSrcItemDef->pchName, iCount, pUpgradeDef ? pUpgradeDef->pchName : "None", pModItemDef ? pModItemDef->pchName : "None", itemUpgrade_FindCurrentRank(pUpgradeDef, NULL));
			}
			else
			{
				QueueRemoteCommand_gslItemUpgrade_SetLastResult(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID,pEnt->myContainerID,pSrcItemDef,pUpgradeDef,kItemUpgradeResult_FailureNoLoss);
				TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GEMS, "ItemUpgrade",
					"EntID %d OldItem \"%s\" OldItemCount %d NewItem \"%s\" Modifier \"%s\" ItemTier %d Result \"FailureNoLoss\"",
					pEnt->myContainerID, pSrcItemDef->pchName, iCount, pUpgradeDef ? pUpgradeDef->pchName : "None", pModItemDef ? pModItemDef->pchName : "None", itemUpgrade_FindCurrentRank(pUpgradeDef, NULL));
			}

			return true;
		}
	}//Otherwise we always succeed
	else if(uModItemID && (!pModifierDef || !pModifierDef->bOnlyConsumeModifierOnFail))
	{
		inv_bag_trh_RemoveItem(ATR_PASS_ARGS,pEnt,false,pModBag,iModSlotIdx,1,pReason);
	}

	if(eSkillTypeUpgrade)
	{
		inv_ent_trh_ApplyNumeric(ATR_PASS_ARGS,pEnt,false,"SkillLevel",1,NumericOp_Add,NULL);
	}

	inv_bag_trh_RemoveItem(ATR_PASS_ARGS,pEnt,false,pSrcBag,iSrcSlot,iCount,pReason);
	if(!inv_ent_trh_AddItemFromDef(ATR_PASS_ARGS,pEnt,NULL,InvBagIDs_Inventory,-1,pUpgradeDef->pchName,1,0,NULL,ItemAdd_UseOverflow,NULL,pExtract))
		return false;

	QueueRemoteCommand_gslItemUpgrade_SetLastResult(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID,pEnt->myContainerID,pSrcItemDef,pUpgradeDef,kItemUpgradeResult_Success);

	TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_GEMS, "ItemUpgrade",
		"EntID %d OldItem \"%s\" OldItemCount %d NewItem \"%s\" Modifier \"%s\" ItemTier %d Result \"Success\"",
		pEnt->myContainerID, pSrcItemDef->pchName, iCount, pUpgradeDef ? pUpgradeDef->pchName : "None", pModItemDef ? pModItemDef->pchName : "None", itemUpgrade_FindCurrentRank(pUpgradeDef, NULL));

	return true;
}

AUTO_TRANS_HELPER;
bool inv_trh_ItemUpgrade(ATR_ARGS, ATH_ARG NOCONST(Entity)*pEnt, ATH_ARG NOCONST(InventoryBag) *pSrcBag, int iSrcSlot, U64 uSrcItemID, ATH_ARG NOCONST(InventoryBag) *pModBag, int iModSlotIdx, U32 uModItemID, ItemDef *pItemModDef, F32 fChance, SkillType eSkillTypeUpgrade, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	if(ISNULL(pSrcBag))
	{
		TRANSACTION_APPEND_LOG_FAILURE("Null SrcBag Passed. Ent %s", SAFE_MEMBER(pEnt,debugName));
		return false;
	}

	if(uModItemID && ISNULL(pModBag))
	{
		TRANSACTION_APPEND_LOG_FAILURE("Null ModBag Passed. Ent %s", SAFE_MEMBER(pEnt,debugName));
		return false;
	}

	return inv_bag_trh_UpgradeItem(ATR_PASS_ARGS,pEnt,pSrcBag,iSrcSlot,uSrcItemID,pModBag,iModSlotIdx,uModItemID,pItemModDef,fChance,eSkillTypeUpgrade,pReason,pExtract);
}


AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Psaved.Ppuppetmaster.Curtempid, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppallowedcritterpets, .Pinventoryv2.Ppinventorybags, .Pinventoryv2.Pplitebags, .Pinventoryv2.Peaowneduniqueitems, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pplayer.Playertype, .Itemidmax, .Pplayer.Pugckillcreditlimit, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers");
enumTransactionOutcome inv_ent_tr_UpgradeItem(ATR_ARGS, NOCONST(Entity)* pEnt, int srcBagID, int srcSlotIdx, U64 uSrcItemID, int modBagID, int modSlotIdx, U64 uModItemID, const char *pchItemModDef, F32 fChance, int eSkillTypeUpgrade, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	NOCONST(InventoryBag)* srcBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt,srcBagID,pExtract);
	NOCONST(InventoryBag)* modBag = uModItemID ? inv_trh_GetBag(ATR_PASS_ARGS, pEnt,modBagID,pExtract) : NULL;
	ItemDef *pModDef = pchItemModDef ? (ItemDef*)RefSystem_ReferentFromString(g_hItemDict,pchItemModDef) : NULL;

	if(!inv_trh_ItemUpgrade(ATR_PASS_ARGS,pEnt,srcBag,srcSlotIdx,uSrcItemID,modBag,modSlotIdx,uModItemID,pModDef,fChance,eSkillTypeUpgrade,pReason,pExtract))
	{
		TRANSACTION_RETURN_LOG_FAILURE(
			"Upgrade Item Failed:");
	}

	TRANSACTION_RETURN_LOG_SUCCESS("Upgrade Item Complete:");
}

//Transaction to move an item between two bag slots, involving a guild bag in some way
AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Itemidmax, .Psaved.Savedname, .Pplayer.Publicaccountname, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .pInventoryV2.Peaowneduniqueitems, .Pplayer.Playertype, .Psaved.Ppownedcontainers, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[]")
	ATR_LOCKS(eaPets, ".Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems")
	ATR_LOCKS(pGuild, ".Eamembers, .Uoldbanklogidx, .Eabanklog")
	ATR_LOCKS(pGuildBank, "pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], .pInventoryV2.ppLiteBags[], .pInventoryV2.ppLiteBags[]");
enumTransactionOutcome inv_ent_tr_MoveItemGuild(ATR_ARGS, NOCONST(Entity)* pEnt, CONST_EARRAY_OF(NOCONST(Entity)) eaPets,
												NOCONST(Guild) *pGuild, NOCONST(Entity) *pGuildBank, 
												const MoveItemGuildStruct *srcdestinfo, int iSrcBagID, int iDstBagID,
												const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	NOCONST(InventoryBag)* pSrcBag;
	NOCONST(InventoryBag)* pDstBag;
	NOCONST(Item) *pItem = NULL;
	NOCONST(Item) *pDstItem = NULL;
	ItemDef *pDef;
	bool bFailure = false;
	U64 itemID = 0;
	U64 uSrcItemID = srcdestinfo->uSrcItemID;
	
	if (srcdestinfo->bSrcGuild) {
		pSrcBag = inv_guildbank_trh_GetBag(ATR_PASS_ARGS,pGuildBank,iSrcBagID);
	} else {
		pSrcBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt,iSrcBagID, pExtract);
	}
	
	if (srcdestinfo->bDstGuild) {
		pDstBag = inv_guildbank_trh_GetBag(ATR_PASS_ARGS,pGuildBank,iDstBagID);
	} else {
		pDstBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt,iDstBagID, pExtract);
	}
	
	pItem = inv_bag_trh_GetItem(ATR_PASS_ARGS, pSrcBag, srcdestinfo->iSrcSlotIdx);
	pDef = pItem ? GET_REF(pItem->hItem) : NULL;
	itemID = pItem ? pItem->id : 0;

	// mblattel[19Oct11] Special code to clean up items which got into the guild bank with an ID of zero. Assign them
	//   a unique id based on the manipulating entity.
	if (pItem && itemID==0 && srcdestinfo->bSrcGuild)
	{
		item_trh_SetItemID(pEnt, pItem);
		itemID = pItem->id;
		uSrcItemID = itemID;
	}

	
	bFailure = !inv_guildbank_trh_ManageBankWithdrawLimitAndLog(ATR_PASS_ARGS, pEnt, pGuild, pGuildBank,
		srcdestinfo->iSrcCount, pItem,
		srcdestinfo->bSrcGuild, pSrcBag, iSrcBagID, srcdestinfo->iSrcEPValue, srcdestinfo->iSrcSlotIdx,
		srcdestinfo->bDstGuild, pDstBag, iDstBagID, srcdestinfo->iDstEPValue, srcdestinfo->iDstSlotIdx);

	if (!bFailure) {
		bFailure = !inv_trh_ItemMoveCheckItemIDs(ATR_PASS_ARGS, pSrcBag, srcdestinfo->iSrcSlotIdx, uSrcItemID, pDstBag, srcdestinfo->iDstSlotIdx, srcdestinfo->uDstItemID);
	}
	if (!bFailure) {
		if(srcdestinfo->bDstGuild && srcdestinfo->bSrcGuild) {
			bFailure = !inv_MoveItem(ATR_PASS_ARGS, NULL, NULL, pDstBag, srcdestinfo->iDstSlotIdx, NULL, NULL, pSrcBag, srcdestinfo->iSrcSlotIdx, srcdestinfo->iSrcCount, ItemAdd_Silent, pReason, NULL, NULL);
		} else if (srcdestinfo->bDstGuild) {
			bFailure = !inv_MoveItem(ATR_PASS_ARGS, NULL, NULL, pDstBag,srcdestinfo-> iDstSlotIdx, pEnt, eaPets, pSrcBag, srcdestinfo->iSrcSlotIdx, srcdestinfo->iSrcCount, ItemAdd_Silent, pReason, NULL, NULL);
		} else {
			bFailure = !inv_MoveItem(ATR_PASS_ARGS, pEnt, eaPets, pDstBag, srcdestinfo->iDstSlotIdx, NULL, NULL, pSrcBag, srcdestinfo->iSrcSlotIdx, srcdestinfo->iSrcCount, ItemAdd_Silent, pReason, NULL, pExtract);
		}
	}


	if (bFailure) {
		TRANSACTION_RETURN_LOG_FAILURE(
			"Item Move Failed: Item [%s<%"FORM_LL"u>]x%i from %s Bag:Slot [%s:%d] to %s Bag:Slot [%s:%d] on Ent [%s]",
			pDef ? pDef->pchName : "?",
			itemID, srcdestinfo->iSrcCount,
			srcdestinfo->bSrcGuild ? "Guild" : "Entity", StaticDefineIntRevLookup(InvBagIDsEnum, iSrcBagID), srcdestinfo->iSrcSlotIdx,
			srcdestinfo->bDstGuild ? "Guild" : "Entity", StaticDefineIntRevLookup(InvBagIDsEnum, iDstBagID), srcdestinfo->iDstSlotIdx,
			pEnt->debugName );
	}
	
	TRANSACTION_RETURN_LOG_SUCCESS(
		"Item Moved: Item [%s<%"FORM_LL"u>]x%i from %s Bag:Slot [%s:%d] to %s Bag:Slot [%s:%d] on Ent [%s]", 
		pDef ? pDef->pchName : "?",
		itemID, srcdestinfo->iSrcCount,
		srcdestinfo->bSrcGuild ? "Guild" : "Entity", StaticDefineIntRevLookup(InvBagIDsEnum, iSrcBagID), srcdestinfo->iSrcSlotIdx,
		srcdestinfo->bDstGuild ? "Guild" : "Entity", StaticDefineIntRevLookup(InvBagIDsEnum, iDstBagID), srcdestinfo->iDstSlotIdx,
		pEnt->debugName );
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pEntSrc, ".Psaved.Ppuppetmaster.Curtype, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .pInventoryV2.Peaowneduniqueitems, .Pplayer.Playertype, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[]")
	ATR_LOCKS(eaSrcPets, ".Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems")
	ATR_LOCKS(pEntDst, ".Psaved.Ppuppetmaster.Curtype, .Pplayer.Playertype, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .pInventoryV2.Peaowneduniqueitems, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[]")
	ATR_LOCKS(eaDstPets, ".Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems");
bool inv_ent_trh_MoveItemAcrossEnts(ATR_ARGS,
									ATH_ARG NOCONST(Entity)* pEntSrc, 
									ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaSrcPets,
									int SrcBagID, int SrcSlotIdx, U64 uSrcItemID,
									ATH_ARG NOCONST(Entity)* pEntDst, 
									ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaDstPets,
									int DestBagID, int DestSlotIdx, U64 uDstItemID, int Count,
									const ItemChangeReason *pSrcReason, const ItemChangeReason *pDestReason,
									GameAccountDataExtract *pExtract)
{
	NOCONST(InventoryBag)* pSrcBag = inv_trh_GetBag(ATR_PASS_ARGS, pEntSrc,SrcBagID,pExtract);
	NOCONST(InventoryBag)* pDestBag = inv_trh_GetBag(ATR_PASS_ARGS, pEntDst,DestBagID,pExtract);
	NOCONST(Item) *pItem = inv_bag_trh_GetItem(ATR_PASS_ARGS, pSrcBag, SrcSlotIdx);
	ItemDef *pDef = pItem ? GET_REF(pItem->hItem) : NULL;
	U64 itemID = pItem ? pItem->id : 0;
	
	if (!inv_trh_ItemMoveCheckItemIDs(ATR_PASS_ARGS, pSrcBag, SrcSlotIdx, uSrcItemID, pDestBag, DestSlotIdx, uDstItemID) ||
		!inv_MoveItem(ATR_PASS_ARGS, pEntDst, eaDstPets, pDestBag, DestSlotIdx, pEntSrc, eaSrcPets, pSrcBag, SrcSlotIdx, Count, ItemAdd_Silent, pSrcReason, pDestReason, pExtract))
	{
		TRANSACTION_APPEND_LOG_FAILURE(
			"Item Move Failed: Item [%s<%"FORM_LL"u>]x%i from Bag:Slot [%s:%d] on Entity [%s] to Bag:Slot [%s:%d] on Entity [%s]", 
			pDef ? pDef->pchName : "?",
			itemID, Count,
			StaticDefineIntRevLookup(InvBagIDsEnum,SrcBagID), SrcSlotIdx,
			pEntSrc->debugName,
			StaticDefineIntRevLookup(InvBagIDsEnum,DestBagID), DestSlotIdx,
			pEntDst->debugName );
		return false;
	}
	
	TRANSACTION_APPEND_LOG_SUCCESS(
		"Item Moved: Item [%s<%"FORM_LL"u>]x%i from Bag:Slot [%s:%d] on Entity [%s] to Bag:Slot [%s:%d] on Entity [%s]", 
		pDef ? pDef->pchName : "?",
		itemID, Count,
		StaticDefineIntRevLookup(InvBagIDsEnum,SrcBagID), SrcSlotIdx,
		pEntSrc->debugName,
		StaticDefineIntRevLookup(InvBagIDsEnum,DestBagID), DestSlotIdx,
		pEntDst->debugName );
	return true;
}

//MULTIPLE ENTITY VERSION - inv_ent_tr_MoveItem
//Transaction to move an item between two bag slots on two different entities
AUTO_TRANSACTION
	ATR_LOCKS(pEntSrc, ".Psaved.Ppuppetmaster.Curtype, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .pInventoryV2.Peaowneduniqueitems, .Pplayer.Playertype, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[]")
	ATR_LOCKS(eaSrcPets, ".Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems")
	ATR_LOCKS(pEntDst, ".Psaved.Ppuppetmaster.Curtype, .Pplayer.Playertype, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .pInventoryV2.Peaowneduniqueitems, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[]")
	ATR_LOCKS(eaDstPets, ".Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems");
enumTransactionOutcome inv_ent_tr_MoveItemAcrossEnts(ATR_ARGS, 
													 NOCONST(Entity)* pEntSrc, 
													 CONST_EARRAY_OF(NOCONST(Entity)) eaSrcPets,
													 int iSrcBagID, int iSrcSlotIdx, U64 uSrcItemID,
													 NOCONST(Entity)* pEntDst, 
													 CONST_EARRAY_OF(NOCONST(Entity)) eaDstPets,
													 int iDestBagID, int iDestSlotIdx, U64 uDstItemID, 
													 int iCount, 
													 const ItemChangeReason *pSrcReason, const ItemChangeReason *pDestReason,
													 GameAccountDataExtract *pExtract)
{
	if (!inv_ent_trh_MoveItemAcrossEnts(ATR_PASS_ARGS, 
										pEntSrc, eaSrcPets, iSrcBagID, iSrcSlotIdx, uSrcItemID,
										pEntDst, eaDstPets, iDestBagID, iDestSlotIdx, uDstItemID, iCount, pSrcReason, pDestReason, pExtract))
	{
		TRANSACTION_RETURN_LOG_FAILURE("MoveItemAcrossEnts FAILED");
	}
	TRANSACTION_RETURN_LOG_SUCCESS("MoveItemAcrossEnts SUCCESS");
}


//Transaction to move an item between two bag slots, one or both of which are on a guild
AUTO_TRANSACTION
	ATR_LOCKS(pPlayerEnt, ".Pplayer.Publicaccountname, .Hallegiance, .Hsuballegiance, .Psaved.Savedname")
	ATR_LOCKS(eaPets, ".Psaved.Ppuppetmaster.Curtempid, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppallowedcritterpets, .Pcritter.Petdef, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .pInventoryV2.Peaowneduniqueitems, .Pplayer.Playertype, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[]")
	ATR_LOCKS(pGuild, ".Eamembers, .Uoldbanklogidx, .Eabanklog")
	ATR_LOCKS(pGuildBank, "pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], .pInventoryV2.ppLiteBags[], .pInventoryV2.ppLiteBags[]");
enumTransactionOutcome inv_ent_tr_MoveItemGuildAcrossEnts(ATR_ARGS, NOCONST(Entity)* pPlayerEnt, 
														  S32 iPetIdx,
														  CONST_EARRAY_OF(NOCONST(Entity)) eaPets,
														  NOCONST(Guild) *pGuild, NOCONST(Entity) *pGuildBank,
														  const MoveItemGuildStruct *srcdestinfo, int iSrcBagID, int iDstBagID,
														  const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	NOCONST(InventoryBag)* pSrcBag;
	NOCONST(InventoryBag)* pDstBag;
	NOCONST(Item) *pItem;
	ItemDef *pDef;
	bool bFailure = false;
	U64 itemID = 0;
	
	if (srcdestinfo->bSrcGuild) {
		pSrcBag = inv_guildbank_trh_GetBag(ATR_PASS_ARGS,pGuildBank,iSrcBagID);
	} else {
		pSrcBag = inv_trh_GetBag(ATR_PASS_ARGS, eaPets[iPetIdx],iSrcBagID, pExtract);
	}
	
	if (srcdestinfo->bDstGuild) {
		pDstBag = inv_guildbank_trh_GetBag(ATR_PASS_ARGS,pGuildBank,iDstBagID);
	} else {
		pDstBag = inv_trh_GetBag(ATR_PASS_ARGS, eaPets[iPetIdx],iDstBagID, pExtract);
	}
	
	pItem = inv_bag_trh_GetItem(ATR_PASS_ARGS, pSrcBag, srcdestinfo->iSrcSlotIdx);
	pDef = pItem ? GET_REF(pItem->hItem) : NULL;
	itemID = pItem ? pItem->id : 0;
	
	bFailure = !inv_guildbank_trh_ManageBankWithdrawLimitAndLog(ATR_PASS_ARGS, pPlayerEnt, pGuild, pGuildBank,
		srcdestinfo->iSrcCount, pItem,
		srcdestinfo->bSrcGuild, pSrcBag, iSrcBagID, srcdestinfo->iSrcEPValue, srcdestinfo->iSrcSlotIdx,
		srcdestinfo->bDstGuild, pDstBag, iDstBagID, srcdestinfo->iDstEPValue, srcdestinfo->iDstSlotIdx);
	
	if (!bFailure)
	{
		bFailure = !inv_trh_ItemMoveCheckItemIDs(ATR_PASS_ARGS, pSrcBag, srcdestinfo->iSrcSlotIdx, srcdestinfo->uSrcItemID, pDstBag, srcdestinfo->iDstSlotIdx, srcdestinfo->uDstItemID);
	}
	if (!bFailure) {
		if (srcdestinfo->bDstGuild && srcdestinfo->bSrcGuild) {
			bFailure = !inv_MoveItem(ATR_PASS_ARGS, NULL, NULL, pDstBag, srcdestinfo->iDstSlotIdx, NULL, NULL, pSrcBag, srcdestinfo->iSrcSlotIdx, srcdestinfo->iSrcCount, ItemAdd_Silent, pReason, NULL, NULL);
		} else if (srcdestinfo->bDstGuild) {
			bFailure = !inv_MoveItem(ATR_PASS_ARGS, NULL, NULL, pDstBag, srcdestinfo->iDstSlotIdx, eaPets[iPetIdx], eaPets, pSrcBag, srcdestinfo->iSrcSlotIdx, srcdestinfo->iSrcCount, ItemAdd_Silent, pReason, NULL, NULL);
		} else {
			bFailure = !inv_MoveItem(ATR_PASS_ARGS, eaPets[iPetIdx], eaPets, pDstBag, srcdestinfo->iDstSlotIdx, NULL, NULL, pSrcBag, srcdestinfo->iSrcSlotIdx, srcdestinfo->iSrcCount, ItemAdd_Silent, pReason, NULL, pExtract);
		}
	}
	
	if (bFailure) {
		TRANSACTION_RETURN_LOG_FAILURE(
			"Item Move Failed: Item [%s<%"FORM_LL"u>]x%i from %s Bag:Slot [%s:%d] to %s Bag:Slot [%s:%d] on Ent [%s]", 
			pDef ? pDef->pchName : "?",
			itemID, srcdestinfo->iSrcCount,
			srcdestinfo->bSrcGuild ? "Guild" : "Entity", StaticDefineIntRevLookup(InvBagIDsEnum, iSrcBagID), srcdestinfo->iSrcSlotIdx,
			srcdestinfo->bDstGuild ? "Guild" : "Entity", StaticDefineIntRevLookup(InvBagIDsEnum, iDstBagID), srcdestinfo->iDstSlotIdx,
			pPlayerEnt->debugName );
	} else {
		TRANSACTION_RETURN_LOG_SUCCESS(
			"Item Moved: Item [%s<%"FORM_LL"u>]x%i from %s Bag:Slot [%s:%d] to %s Bag:Slot [%s:%d] on Ent [%s]", 
			pDef ? pDef->pchName : "?",
			itemID, srcdestinfo->iSrcCount,
			srcdestinfo->bSrcGuild ? "Guild" : "Entity", StaticDefineIntRevLookup(InvBagIDsEnum, iSrcBagID), srcdestinfo->iSrcSlotIdx,
			srcdestinfo->bDstGuild ? "Guild" : "Entity", StaticDefineIntRevLookup(InvBagIDsEnum, iDstBagID), srcdestinfo->iDstSlotIdx,
			pPlayerEnt->debugName );
	}
}

//Moves all overflow items from the source to target entity
//Used to push pet overflow bags onto the player
AUTO_TRANSACTION
	ATR_LOCKS(pEntSrc, ".Psaved.Ppuppetmaster.Curtype, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .pInventoryV2.Peaowneduniqueitems, .Pplayer.Playertype, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[]")
	ATR_LOCKS(pEntDst, ".Psaved.Ppuppetmaster.Curtype, .Pplayer.Playertype, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .pInventoryV2.Peaowneduniqueitems, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[]");
enumTransactionOutcome inv_ent_tr_MoveOverflowItemsAcrossEnts(ATR_ARGS, NOCONST(Entity)* pEntSrc, NOCONST(Entity)* pEntDst, const ItemChangeReason *pSrcReason, const ItemChangeReason *pDestReason, GameAccountDataExtract *pExtract)
{
	NOCONST(InventoryBag)* pSrcBag = inv_trh_GetBag(ATR_PASS_ARGS, pEntSrc, 33 /* Literal InvBagIDs_Overflow */,pExtract);
	NOCONST(InventoryBag)* pDestBag = inv_trh_GetBag(ATR_PASS_ARGS, pEntDst, 33 /* Literal InvBagIDs_Overflow */,pExtract);
	int iNumMoved = 0, iNumFailed = 0;
	int NumSlots;
	int ii;

	if (!pSrcBag || !pDestBag)
	{
		TRANSACTION_RETURN_LOG_FAILURE(
			"MoveOverflowItemsAcrossEnts Failed: Either %s or %s does not have an overflow bag", 
			pEntSrc->debugName, 
			pEntDst->debugName);
	}

	//loop for all slots in this bag
	NumSlots = eaSize(&pSrcBag->ppIndexedInventorySlots);
	for(ii=0; ii<NumSlots; ii++)
	{
		NOCONST(InventorySlot)* pSlot = pSrcBag->ppIndexedInventorySlots[ii];

		if (pSlot->pItem)
		{
			if (!inv_bag_trh_MoveItem(ATR_PASS_ARGS, true, pEntDst, NULL, pDestBag, -1, pEntSrc, NULL, pSrcBag, ii, -1, false, false, pSrcReason, pDestReason, pExtract))
			{
				iNumFailed = true;
			}
			else
			{
				++iNumMoved;
			}
		}
	}

	if (iNumFailed)
	{
		TRANSACTION_RETURN_LOG_FAILURE(
			"MoveOverflowItemsAcrossEnts Failed: Moved %d items from Ent [%s] to Ent [%s] and %d items failed to move", 
			iNumMoved,
			pEntSrc->debugName,
			pEntDst->debugName,
			iNumFailed);
	}
	
	TRANSACTION_RETURN_LOG_SUCCESS(
		"MoveOverflowItemsAcrossEnts: Moved %d items from Ent [%s] to Ent [%s]", 
		iNumMoved,
		pEntSrc->debugName,
		pEntDst->debugName );
}


AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.ppInventoryBags[], .Pplayer.Pugckillcreditlimit, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[]");
enumTransactionOutcome inv_tr_ApplyNumeric(ATR_ARGS, NOCONST(Entity)* pEnt, char* ItemDefName, F32 value, int /*NumericOp*/ flags, const ItemChangeReason *pReason)
{
	if( ISNULL(pEnt) )
		TRANSACTION_RETURN_LOG_FAILURE("ApplyNumeric Failed: Ent is NULL");

	if ( inv_ent_trh_ApplyNumeric(ATR_PASS_ARGS, pEnt, false, ItemDefName, value, flags, pReason) )
	{
		TRANSACTION_RETURN_LOG_SUCCESS(
			"Numeric Item [%s] value added %f on Entity [%s]", 
			ItemDefName, 
			value,
			pEnt->debugName);
	}
	else
	{
		TRANSACTION_RETURN_LOG_FAILURE(
			"Could not add %f to Numeric Item [%s] on Entity [%s]", 
			value,
			ItemDefName,
			pEnt->debugName);
	}
}

//transaction to clear an inventory bag
AUTO_TRANSACTION
ATR_LOCKS(pEnt, "pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
enumTransactionOutcome inv_ent_tr_ClearBag(ATR_ARGS, NOCONST(Entity)* pEnt, int BagID, GameAccountDataExtract *pExtract)
{
	NOCONST(InventoryBag)* pBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt,BagID,pExtract);
	NOCONST(InventoryBagLite)* pBagLite = inv_trh_GetLiteBag(ATR_PASS_ARGS, pEnt,BagID,pExtract);

	if ( ISNULL(pBag) && ISNULL(pBagLite))
	{
		TRANSACTION_RETURN_LOG_FAILURE(
			"Clear Bag %s Failed on Ent %s", 
			StaticDefineIntRevLookup(InvBagIDsEnum,BagID),
			pEnt->debugName );
	}

	if (pBag)
	{
		const InvBagDef* pDef = invbag_trh_def(pBag);
		if (pDef && (pDef->pItemArtActive ||
			pDef->pItemArtActiveSecondary ||
			pDef->pItemArtInactive ||
			pDef->pItemArtInactiveSecondary))
			QueueRemoteCommand_ForceRefreshItemArt(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID);

		inv_bag_trh_ClearBag(ATR_PASS_ARGS, pBag);
	}
	else if (pBagLite)
		inv_lite_trh_ClearBag(ATR_PASS_ARGS, pBagLite);

	TRANSACTION_RETURN_LOG_SUCCESS(
		"Bag %s Cleared on Ent %s", 
		StaticDefineIntRevLookup(InvBagIDsEnum,BagID),
		pEnt->debugName );
}


//transaction to remove all items from all the "regular" bags and zero out the numerics bag
AUTO_TRANSACTION
ATR_LOCKS(ent, ".pInventoryV2.Ppinventorybags, .pInventoryV2.Peaowneduniqueitems, .pInventoryV2.Pplitebags");
enumTransactionOutcome inv_ent_tr_ClearAllBags(ATR_ARGS, NOCONST(Entity)* ent)
{
	QueueRemoteCommand_ForceRefreshItemArt(ATR_RESULT_SUCCESS, ent->myEntityType, ent->myContainerID);
	if ( !inv_ent_trh_ClearAllBags(ATR_PASS_ARGS, ent) )
	{
		TRANSACTION_RETURN_LOG_FAILURE(
			"Clear All Bags Failed on Ent %s", 
			ent->debugName );
	}
	TRANSACTION_RETURN_LOG_SUCCESS(
		"All Bags Cleared on Ent %s", 
		ent->debugName );
}
//transaction helper to move all items from the source bag to the dest bag
AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Psaved.Ppuppetmaster.Curtype, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Psaved.Ppownedcontainers, .pInventoryV2.Peaowneduniqueitems, pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Pplayer.Playertype");
int inv_ent_trh_MoveAllItemsFromBagID(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, int iSrcBagID, int iDestBagID, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	NOCONST(InventoryBag)* pSrcBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, iSrcBagID, pExtract);
	NOCONST(InventoryBag)* pDestBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, iDestBagID, pExtract);
	int iNumMoved = 0, iNumFailed = 0;
	int NumSlots;
	int i;

	if (!pSrcBag)
	{
		return false;
	}
	if (!pDestBag)
	{
		return false;
	}

	//loop for all slots in the source bag
	NumSlots = eaSize(&pSrcBag->ppIndexedInventorySlots);
	for(i = 0; i < NumSlots; ++i)
	{
		NOCONST(InventorySlot)* pSlot = pSrcBag->ppIndexedInventorySlots[i];

		if (pSlot->pItem)
		{
			if (!inv_bag_trh_MoveItem(	ATR_PASS_ARGS, true, pEnt, NULL, pDestBag, -1, 
										pEnt, NULL, pSrcBag, i, -1, false, false, pReason, pReason, pExtract))
			{
				iNumFailed = true;
			}
			else
			{
				++iNumMoved;
			}
		}
	}

	if (iNumFailed)
	{
		return false;
	}

	return true;
}



AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Psaved.Ppuppetmaster.Curtype, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Psaved.Ppownedcontainers, .pInventoryV2.Peaowneduniqueitems, pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Pplayer.Playertype");
enumTransactionOutcome inv_ent_tr_MoveAllItemsFromBag(ATR_ARGS, NOCONST(Entity)* pEnt, int iSrcBagID, int iDestBagID, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	if (!inv_ent_trh_MoveAllItemsFromBagID(ATR_PASS_ARGS, pEnt, iSrcBagID, iDestBagID, pReason, pExtract))
	{
		TRANSACTION_RETURN_LOG_FAILURE(
			"MoveAllItemsFromBag Failed: Failed to moved items from Ent [%s] BagID %d to BagID %d", 
			pEnt->debugName,
			iSrcBagID, 
			iDestBagID);
	}
	else
	{
		TRANSACTION_RETURN_LOG_SUCCESS(
			"MoveAllItemsFromBag: Moved items from Ent [%s] BagID %d to BagID %d", 
			pEnt->debugName,
			iSrcBagID, 
			iDestBagID);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Psaved.Ppuppetmaster.Curtype, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Psaved.Ppownedcontainers, .pInventoryV2.Ppinventorybags, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .pInventoryV2.Peaowneduniqueitems, pInventoryV2.ppLiteBags[], .Pplayer.Playertype");
enumTransactionOutcome inv_ent_tr_MoveAllItemsFromBags(ATR_ARGS, NOCONST(Entity)* pEnt, InventoryBagArray *pSrcBagIDs, int iDestBagID, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	if (pSrcBagIDs)
	{
		S32 i, size = eaiSize(&pSrcBagIDs->eaiBagArray);
		for (i = 0; i < size; ++i)
		{
			if (!inv_ent_trh_MoveAllItemsFromBagID(ATR_PASS_ARGS, pEnt, pSrcBagIDs->eaiBagArray[i], iDestBagID, pReason, pExtract))
			{
				TRANSACTION_RETURN_LOG_FAILURE(
					"MoveAllItemsFromBag Failed: Failed to moved items from Ent [%s] BagID %d to BagID %d", 
					pEnt->debugName,
					pSrcBagIDs->eaiBagArray[i], 
					iDestBagID);
			}
		}

		TRANSACTION_RETURN_LOG_SUCCESS(
			"MoveAllItemsFromBag: Moved items from Ent [%s] BagIDs BagID %d", 
			pEnt->debugName,
			iDestBagID);
	}
	else
	{
		TRANSACTION_RETURN_LOG_FAILURE(
			"MoveAllItemsFromBag Failed: Failed to moved items from Ent [%s] ", 
			pEnt->debugName);
	}
}


// Transaction to update ItemSets
AUTO_TRANSACTION
ATR_LOCKS(ent, ".Psaved.Ppownedcontainers, .pInventoryV2.Ppinventorybags, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Pinventoryv2.Peaowneduniqueitems, .Pplayer.Playertype, .pInventoryV2.ppLiteBags[]");
enumTransactionOutcome inv_ent_tr_UpdateItemSets(ATR_ARGS, NOCONST(Entity)* ent, const ItemChangeReason *pReason)
{
	inv_trh_UpdateItemSets(ATR_PASS_ARGS, ent, pReason);

	TRANSACTION_RETURN_LOG_SUCCESS(
		"Updated Item Sets on Ent %s", 
		ent->debugName );
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, "pInventoryV2.ppLiteBags[]");
int inv_ent_trh_CanUseBankForSwap(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, GameAccountDataExtract *pExtract)
{
	if(inv_trh_GetNumericValue(ATR_EMPTY_ARGS, pEnt, "BankBuildSwap") > 0 || GamePermission_ExtractHasToken(pExtract, GAME_PERMISSION_BANK_BUILD_SWAP, false))
	{
		return true;
	}

	return false;	
}

AUTO_TRANS_HELPER;
int inv_ent_trh_BuildSwap(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, bool bSilent, ATH_ARG NOCONST(EntityBuild)* pBuild, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	int ii, Success = true;

	if ( ISNULL(pEnt))
		return Success;

	if ( ISNULL(pEnt->pInventoryV2))
		return Success;

	// First pass just try to get things equipped (which may trigger swaps, but shouldn't fail)
	for(ii=eaSize(&pBuild->ppItems)-1; ii>=0; ii--)
	{
		NOCONST(EntityBuildItem) *pBuildItem = pBuild->ppItems[ii];

		if(pBuildItem->ulItemID)
		{
			// Find the item and try to equip it
			InvBagIDs BagID = -1;
			int iSlot = -1;
			InvGetFlag eGetFlag = InvGetFlag_NoBuyBackBag;
			NOCONST(Item) *pItem;

			// if character has numeric or token then can swap from bags		
			if(!inv_ent_trh_CanUseBankForSwap(ATR_PASS_ARGS, pEnt, pExtract))
			{
				eGetFlag |= InvGetFlag_NoBankBag;
			}
			
			pItem = inv_trh_GetItemByID(ATR_PASS_ARGS, pEnt, pBuildItem->ulItemID, &BagID, &iSlot, eGetFlag);
			if(pItem && !(BagID == pBuildItem->eBagID && iSlot == pBuildItem->iSlot))
			{
				NOCONST(InventoryBag)* pSrcBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt,BagID,pExtract);
				NOCONST(InventoryBag)* pDstBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt,pBuildItem->eBagID,pExtract);
				NOCONST(InventorySlot)* pSrcSlot = NULL;
				int iCount = 0;

				if (NONNULL(pSrcBag))
					pSrcSlot = eaGet(&pSrcBag->ppIndexedInventorySlots, iSlot);
				if (NONNULL(pSrcSlot))
					iCount = pSrcSlot->pItem->count;

				if(!inv_MoveItem(ATR_PASS_ARGS, pEnt, NULL, pDstBag, pBuildItem->iSlot, pEnt, NULL, pSrcBag, iSlot, iCount, ItemAdd_Silent, pReason, pReason, pExtract)) 
				{
					Success = false;
				}
			}
		}
	}

	// Second pass just try to empty slots designated as empty (which may fail)
	for(ii=eaSize(&pBuild->ppItems)-1; ii>=0; ii--)
	{
		NOCONST(EntityBuildItem) *pBuildItem = pBuild->ppItems[ii];

		if(!pBuildItem->ulItemID)
		{
			// Remove the item (and keep pointer, just to be safe)
			Item* pItem = invbag_RemoveItem(ATR_PASS_ARGS, pEnt, bSilent, pBuildItem->eBagID, pBuildItem->iSlot, -1, pReason, pExtract);

			if(pItem)
			{
				// Add the item to any slot in the Inventory (which means it will also try to put things into
				//  the player bags, handle stacking automatically, etc)
				ItemDef* pDef = GET_REF(pItem->hItem);
				if(inv_AddItem(ATR_PASS_ARGS, pEnt, NULL, InvBagIDs_Inventory, -1, (Item*)pItem, pDef->pchName, bSilent ? ItemAdd_Silent : 0, pReason, pExtract) != TRANSACTION_OUTCOME_SUCCESS)
				{
					Success = false;
				}
				StructDestroy(parse_Item,pItem);
			}
		}
	}


	// Whether or not we were entirely successful, make the existing inventory setup the state of the input build
	if(Success)
	{
		inv_ent_trh_BuildFill(ATR_PASS_ARGS,pEnt,bSilent,pBuild);
	}

	return Success;
}


AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".pInventoryV2.Pplitebags, .pInventoryV2.Peaowneduniqueitems, .pInventoryV2.Ppinventorybags, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Ilevelexp");
bool inv_ent_trh_RemoveMissionItems(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, const char* pMissionName, bool bIncludeGrantItems, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	MissionDef *pMissionDef = NULL;
	char strMissionNamespace[ RESOURCE_NAME_MAX_SIZE ];
	int iBag;
	int NumBags;

	if ( ISNULL(pEnt))
		return false;

	if ( ISNULL(pEnt->pInventoryV2))
		return false;

	pMissionDef = (MissionDef*)RefSystem_ReferentFromString(g_MissionDictionary,pMissionName);
	if (!pMissionDef && !resExtractNameSpace_s( pMissionName, SAFESTR( strMissionNamespace ), NULL, 0 ))
		return false;

	NumBags = eaSize(&pEnt->pInventoryV2->ppInventoryBags);
	
	for (iBag=0; iBag<NumBags; iBag++)
	{
		InventoryBag *bag = (InventoryBag*)pEnt->pInventoryV2->ppInventoryBags[iBag];
		BagIterator *iter;

		if (ISNULL(bag)) 
			continue;
		
		iter = invbag_trh_IteratorFromEnt(ATR_PASS_ARGS, pEnt,bag->BagID,pExtract);
		for(; !bagiterator_Stopped(iter); bagiterator_Next(iter))
		{
			Item *pItem = (Item*)bagiterator_GetItem(iter);
			ItemDef *pItemDef = bagiterator_GetDef(iter);
			const char* strItemDefName = NULL;
			bool bFound = false;
		
			if (!pItem)
				continue;

			strItemDefName = REF_STRING_FROM_HANDLE( pItem->hItem );

			if(pMissionDef && pItem->pSpecialProps && pItem->pSpecialProps->pDoorKey && GET_REF(pItem->pSpecialProps->pDoorKey->hMission) == pMissionDef) {
				bFound = true;
			}

			// Check item def
			if (pItemDef && !bFound) {
				//skip mission grant items  (which also have a mission ref on them)
				if (pItemDef->eType == kItemType_MissionGrant && !bIncludeGrantItems)
					continue;
				
				//check to see if item has a matching mission reference
				if ( pMissionDef && ( GET_REF(pItemDef->hMission) == pMissionDef ) )
				{
					bFound = true;
				}
			}

			// MJF Mar/25/2013 -- Namespace items are automatically considered mission items
			// if they have the same namespace.  This is done because namespaced missions
			// can get dropped without any of the defs around, for example if the namespace
			// is for UGC and that namespace becomes unplayable.
			{
				char strItemNamespace[ RESOURCE_NAME_MAX_SIZE ];
				if( !bFound && resExtractNameSpace_s( strItemDefName, SAFESTR( strItemNamespace ), NULL, 0 )) {
					if( stricmp( strMissionNamespace, strItemNamespace ) == 0 ) {
						bFound = true;
					}
				}
			}

			if(bFound) {
				Item *item = bagiterator_RemoveItem(ATR_PASS_ARGS,iter,pEnt,-1,pReason,pExtract);
				StructDestroySafe(parse_Item, &item);
			}
		}
		bagiterator_Destroy(iter);
	}

	// MJF Mar/25/2013 -- Namespace items can't be ItemLites.  This implies that a
	// MissionItem that is an item lite must have its Def present to work.
	if( pMissionDef ) {
		NumBags = eaSize(&pEnt->pInventoryV2->ppLiteBags);

		for (iBag=0; iBag<NumBags; iBag++)
		{
			InventoryBagLite *bag = (InventoryBagLite*)pEnt->pInventoryV2->ppLiteBags[iBag];
			BagIterator *iter;

			if (ISNULL(bag)) 
				continue;

			iter = invbag_trh_LiteIteratorFromEnt(ATR_PASS_ARGS, pEnt,bag->BagID,pExtract);
			for(; !bagiterator_Stopped(iter); bagiterator_Next(iter))
			{
				ItemDef *pItemDef = bagiterator_GetDef(iter);
				bool bFound = false;

				// Check item def
				if (pItemDef && !bFound) {
					//skip mission grant items  (which also have a mission ref on them)
					if (pItemDef->eType == kItemType_MissionGrant && !bIncludeGrantItems)
						continue;

					//check to see if item has a matching mission reference
					if ( GET_REF(pItemDef->hMission) == pMissionDef )
					{
						bFound = true;
					}
				}

				if(bFound) {
					bagiterator_RemoveItem(ATR_PASS_ARGS,iter,pEnt,-1,pReason,pExtract);
				}
			}
			bagiterator_Destroy(iter);
		}
	}

	return true;
}

AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Psaved.Conowner.Containertype, .Pinventoryv2.Ppinventorybags, .Pinventoryv2.Peaowneduniqueitems, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Pinventoryv2.Pplitebags, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid");
enumTransactionOutcome inv_tr_UnlockCostume( ATR_ARGS, NOCONST(Entity)* pEnt, int bSilent, U64 id, ItemChangeReason *pReason, GameAccountDataExtract *pExtract )
{
	NOCONST(Item) *pItem = NULL;
	InvBagIDs eBagID = InvBagIDs_Inventory;
	int iSlot = -1;
	ItemDef *pItemDef = NULL;

	if(ISNULL(pEnt) || ISNULL(pEnt->pPlayer))
	{
		return(TRANSACTION_OUTCOME_FAILURE);
	}

	pItem = inv_trh_GetItemByID(ATR_PASS_ARGS, pEnt, id, &eBagID, &iSlot, InvGetFlag_NoBuyBackBag);

	if(ISNULL(pItem))
	{
		TRANSACTION_RETURN_LOG_FAILURE("No such item %"FORM_LL"d on EntityPlayer[%d]", id, pEnt->myContainerID);
	}

	pItemDef = GET_REF(pItem->hItem);
	if(ISNULL(pItemDef))
	{
		TRANSACTION_RETURN_LOG_FAILURE("No such invalid ItemDef %s on item %"FORM_LL"d on EntityPlayer[%d]", REF_STRING_FROM_HANDLE(pItem->hItem), id, pEnt->myContainerID);
	}

	//Bind it
	if( (pItemDef->flags & kItemDefFlag_BindOnEquip) && !(pItem->flags & kItemFlag_Bound))
		pItem->flags |= kItemFlag_Bound;

	if(!inv_trh_UnlockCostumeOnItem(ATR_PASS_ARGS, pEnt, bSilent, pItem, pExtract))
	{
		TRANSACTION_RETURN_LOG_FAILURE("No costumes on item %"FORM_LL"d that haven't already been unlocked on EntityPlayer[%d]", id, pEnt->myContainerID);
	}
	else
	{
		//If we unlocked something on the item AND it's a delete after unlock item.  Delete it.
		if(pItemDef->bDeleteAfterUnlock)
		{
			NOCONST(InventoryBag) *pBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, eBagID, pExtract);
			NOCONST(InventorySlot) *pSlot = NULL;
			if(NONNULL(pBag))
				pSlot = eaGet(&pBag->ppIndexedInventorySlots,iSlot);

			if(NONNULL(pSlot) && NONNULL(pBag))
			{
				Item *pRemItem = invbag_RemoveItem(ATR_PASS_ARGS, pEnt, true, pBag->BagID, iSlot, -1, pReason, pExtract);
				StructDestroy(parse_Item, pRemItem);
			}
		}
	}

	TRANSACTION_RETURN_LOG_SUCCESS("Successfully unlocked costumes on item %"FORM_LL"d for EntityPlayer[%d]", id, pEnt->myContainerID);
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Eainteriorunlocks, .pInventoryV2.Ppinventorybags");
enumTransactionOutcome inv_tr_UnlockInterior( ATR_ARGS, NOCONST(Entity)* pEnt, int bSilent, U64 id )
{
	NOCONST(Item) *pItem = NULL;

	if(ISNULL(pEnt) || ISNULL(pEnt->pPlayer))
	{
		return(TRANSACTION_OUTCOME_FAILURE);
	}

	pItem = inv_trh_GetItemByID(ATR_PASS_ARGS, pEnt, id, NULL, NULL, InvGetFlag_NoBuyBackBag);

	if(ISNULL(pItem))
	{
		TRANSACTION_RETURN_LOG_FAILURE("No such item %"FORM_LL"d on EntityPlayer[%d]", id, pEnt->myContainerID);
	}

	if(!inv_trh_UnlockInterior(ATR_PASS_ARGS, pEnt, bSilent, pItem))
	{
		TRANSACTION_RETURN_LOG_FAILURE("No interiors on item %"FORM_LL"d that haven't already been unlocked on EntityPlayer[%d]", id, pEnt->myContainerID);
	}

	TRANSACTION_RETURN_LOG_SUCCESS("Successfully unlocked interiors on item %"FORM_LL"d for EntityPlayer[%d]", id, pEnt->myContainerID);
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".pInventoryV2.Pplitebags, .pInventoryV2.Peaowneduniqueitems, .Psaved.Conowner.Containertype, .pInventoryV2.Ppinventorybags, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Pchar.Ilevelexp");
enumTransactionOutcome inv_ent_tr_RemoveMissionItems(ATR_ARGS, NOCONST(Entity)* pEnt, char *pMissionName, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract )
{
	if ( !inv_ent_trh_RemoveMissionItems(ATR_PASS_ARGS, pEnt, pMissionName, false, pReason, pExtract) )
	{	
		TRANSACTION_RETURN_LOG_FAILURE(
			"Remove Mission %s Items on Ent %s failed.", 
			pMissionName,
			pEnt->debugName );
	}

	TRANSACTION_RETURN_LOG_SUCCESS(
		"Remove Mission %s Items on Ent %s succeeded.", 
		pMissionName,
		pEnt->debugName );
}


// This is a debug command to test removing mission items from an ent
AUTO_COMMAND ACMD_SERVERONLY ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Standard, Reward);
void RemoveMissionItems( Entity *pEnt, ACMD_NAMELIST("Mission", REFDICTIONARY) char *MissionName )
{
	TransactionReturnVal* returnVal;
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	ItemChangeReason reason = {0};

	returnVal = LoggedTransactions_CreateManagedReturnValEnt("RemoveMissionItems", pEnt, NULL, NULL);

	inv_FillItemChangeReason(&reason, pEnt, "Mission:RemoveMissionItemsAL9", NULL);

	AutoTrans_inv_ent_tr_RemoveMissionItems(returnVal, GetAppGlobalType(), 
			entGetType(pEnt), entGetContainerID(pEnt), 
			MissionName, &reason, pExtract);
}

AUTO_STRUCT;
typedef struct TradeCBData
{
	EntityRef player1EntRef;
	EntityRef player2EntRef;
	CONTAINERID_EARRAY player1PetIDs;
	CONTAINERID_EARRAY player2PetIDs;
	TransactionReturnCallback userFunc;		NO_AST
	void *userData;							NO_AST
	ContainerTradeData *pContainerTradeData;AST(UNOWNED)
} TradeCBData;

void
InventoryTrade_CB(TransactionReturnVal *pReturn, TradeCBData *cbData)
{
	// call the user callback
	if ( cbData->userFunc != NULL )
	{
		(* cbData->userFunc)(pReturn, cbData->userData);
	}

	if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS && cbData->pContainerTradeData)
	{
		char* estrResult = NULL;
		Entity* pEnt1 = entFromEntityRefAnyPartition(cbData->player1EntRef);
		Entity* pEnt2 = entFromEntityRefAnyPartition(cbData->player2EntRef);
		
		estrStackCreate(&estrResult);

		if (pEnt1 && pEnt2)
		{
			int i, j, n = eaSize(&cbData->pContainerTradeData->ppContainers);
			for (i = 0; i < n; i++)
			{
				ContainerID uPetID = cbData->pContainerTradeData->ppContainers[i]->eNewID;
				Entity* pPetEnt;
				Entity* pSrcEnt = NULL;
				Entity* pDstEnt = NULL;

				if (pPetEnt = entity_GetSubEntity(entGetPartitionIdx(pEnt1), pEnt1, GLOBALTYPE_ENTITYSAVEDPET, uPetID))
				{
					pSrcEnt = pEnt2;
					pDstEnt = pEnt1;
				}
				else if (pPetEnt = entity_GetSubEntity(entGetPartitionIdx(pEnt2), pEnt2, GLOBALTYPE_ENTITYSAVEDPET, uPetID))
				{
					pSrcEnt = pEnt1;
					pDstEnt = pEnt2;
				}
				if (pPetEnt)
				{
					//Fix up pet costume (remove invalid unlockable parts)
					for (j = eaSize(&pPetEnt->pSaved->costumeData.eaCostumeSlots)-1; j >= 0; --j)
					{
						PlayerCostume **eaUnlockedCostumes = NULL;
						PCSlotType *pSlotType = NULL;
						GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pDstEnt);
						NOCONST(PlayerCostume) *pc = StructCloneDeConst(parse_PlayerCostume, pPetEnt->pSaved->costumeData.eaCostumeSlots[j]->pCostume);
						if (!pc) continue;

						pSlotType = costumeLoad_GetSlotType(pPetEnt->pSaved->costumeData.eaCostumeSlots[j]->pcSlotType);
						costumeEntity_GetUnlockCostumes(pDstEnt->pSaved->costumeData.eaUnlockedCostumeRefs, entity_GetGameAccount(pDstEnt), pDstEnt, pPetEnt, &eaUnlockedCostumes);
						costumeTailor_MakeCostumeValid(pc, GET_REF(pPetEnt->pChar->hSpecies), eaUnlockedCostumes, pSlotType, false, false, false, guild_GetGuild(pDstEnt), false, pExtract, false, NULL);
						costumetransaction_StorePlayerCostume(pDstEnt, pPetEnt, 0, j, (PlayerCostume*)pc, SAFE_MEMBER(pSlotType, pcName), 0, kPCPay_Default);

						StructDestroyNoConst(parse_PlayerCostume, pc);
						eaDestroy(&eaUnlockedCostumes);
					}

					//Notify
					estrClear(&estrResult);
					entFormatGameMessageKey(pDstEnt, &estrResult, "Pet_TradeAdd_Success",
						STRFMT_ENTITY_KEY("Pet", pPetEnt),
						STRFMT_END);
					notify_NotifySend(pDstEnt, kNotifyType_PetAdded, estrResult, NULL, NULL);
					gslActivity_AddPetTradeEntry(pSrcEnt, pDstEnt, pPetEnt);
				}
			}
		}
		ContainerTrade_Finish(cbData->pContainerTradeData);
		estrDestroy(&estrResult);
	}

	// free callback data
	StructDestroy(parse_TradeCBData, cbData);
}

void invtransaction_completeTrade(Entity *pEnt1, Entity *pEnt2, TradeBag *pTrade1, TradeBag *pTrade2, TransactionReturnCallback func, void *pvUserData, ContainerTradeData *pContainerTradeData)
{
	TransactionReturnVal* returnVal;
	GameAccountDataExtract *pExtract1, *pExtract2;
	INT_EARRAY petIDs1 = NULL;
	INT_EARRAY petIDs2 = NULL;
	INT_EARRAY newPetIDs = NULL;
	INT_EARRAY oldPetIDs = NULL;
	TradeCBData *cbData;
	ItemDef *itemDef;
	ContainerID petID;
	ItemChangeReason reason1 = {0}, reason2 = {0};

	if (!pEnt1 || !pEnt2)
	{
		return;
	}

	cbData = StructAlloc(parse_TradeCBData);
	cbData->player1EntRef = entGetRef(pEnt1);
	cbData->player2EntRef = entGetRef(pEnt2);
	cbData->userFunc = func;
	cbData->userData = pvUserData;
	cbData->pContainerTradeData = pContainerTradeData;

	returnVal = LoggedTransactions_CreateManagedReturnValEnt("Trade", pEnt1, InventoryTrade_CB, cbData);

	{
		int i;
		for (i = eaSize(&pTrade1->ppTradeSlots)-1; i >= 0; --i)
		{
			TradeSlot *ts = pTrade1->ppTradeSlots[i];
			// is the item actually from a pet's inventory?
			if (ts->tradeSlotPetID)
			{
				Entity *pPet = entity_GetSubEntity(entGetPartitionIdx(pEnt1), pEnt1, GLOBALTYPE_ENTITYSAVEDPET, ts->tradeSlotPetID);
				if (pPet)
				{
					ea32PushUnique(&petIDs1, ts->tradeSlotPetID);
				}
			}
			// does the item reference a database container (such as bridge officer)?
			if ( ts->pItem != NULL )
			{
				itemDef = GET_REF(ts->pItem->hItem);
				if ( ( itemDef != NULL ) && ( itemDef->eType == kItemType_Container ) && ts->pItem->pSpecialProps)
				{
					petID = StringToContainerID(REF_STRING_FROM_HANDLE(ts->pItem->pSpecialProps->pContainerInfo->hSavedPet));
					ea32PushUnique(&cbData->player1PetIDs, petID);
				}
			}
		}
		for (i = eaSize(&pTrade2->ppTradeSlots)-1; i >= 0; --i)
		{
			TradeSlot *ts = pTrade2->ppTradeSlots[i];
			// is the item actually from a pet's inventory?
			if (ts->tradeSlotPetID)
			{
				Entity *pPet = entity_GetSubEntity(entGetPartitionIdx(pEnt2), pEnt2, GLOBALTYPE_ENTITYSAVEDPET, ts->tradeSlotPetID);
				if (pPet)
				{
					ea32PushUnique(&petIDs2, ts->tradeSlotPetID);
				}
			}
			// does the item reference a database container (such as bridge officer)?
			if ( ts->pItem != NULL && ts->pItem->pSpecialProps)
			{
				itemDef = GET_REF(ts->pItem->hItem);
				if ( ( itemDef != NULL ) && ( itemDef->eType == kItemType_Container ) )
				{
					petID = StringToContainerID(REF_STRING_FROM_HANDLE(ts->pItem->pSpecialProps->pContainerInfo->hSavedPet));
					ea32PushUnique(&cbData->player2PetIDs, petID);
				}
			}
		}

		if(pContainerTradeData && eaSize(&pContainerTradeData->ppContainers))
		{
			for(i=0;i<eaSize(&pContainerTradeData->ppContainers);i++)
			{
				ea32Push(&newPetIDs,pContainerTradeData->ppContainers[i]->eNewID);
				ea32Push(&oldPetIDs,pContainerTradeData->ppContainers[i]->eOldID);
			}
		}
	}

	pExtract1 = entity_GetCachedGameAccountDataExtract(pEnt1);
	pExtract2 = entity_GetCachedGameAccountDataExtract(pEnt2);

	inv_FillItemChangeReason(&reason1, pEnt1, "Trade:Complete", pEnt2->debugName);
	inv_FillItemChangeReason(&reason2, pEnt2, "Trade:Complete", pEnt1->debugName);

	AutoTrans_inv_tr_trade(returnVal, GetAppGlobalType(), 
			entGetType(pEnt1), entGetContainerID(pEnt1), 
			entGetType(pEnt2), entGetContainerID(pEnt2), 
			GLOBALTYPE_ENTITYSAVEDPET, &(U32*)petIDs1, 
			GLOBALTYPE_ENTITYSAVEDPET, &(U32*)petIDs2, 
			GLOBALTYPE_ENTITYSAVEDPET, &(U32*)oldPetIDs, 
			GLOBALTYPE_ENTITYSAVEDPET, &(U32*)newPetIDs, 
			pContainerTradeData, pTrade1, pTrade2, &reason1, &reason2, pExtract1, pExtract2 );

	ea32Destroy(&petIDs1);
	ea32Destroy(&petIDs2);
}

AUTO_STRUCT;
typedef struct invTradeData
{
	GlobalType eEntType1;
	ContainerID uEntID1;
	GlobalType eEntType2;
	ContainerID uEntID2;
	TradeBag *pTrade1;
	TradeBag *pTrade2;
	TransactionReturnCallback func;	NO_AST
	void *pUserData;				NO_AST
	ContainerTradeData *pContainerTradeData; AST(UNOWNED)
}invTradeData;

void invTransaction_containerTradeFinished(ContainerTradeData *pData, invTradeData *pTradeData)
{
	Entity* pEnt1 = entFromContainerIDAnyPartition(pTradeData->eEntType1, pTradeData->uEntID1);
	Entity* pEnt2 = entFromContainerIDAnyPartition(pTradeData->eEntType2, pTradeData->uEntID2);

	invtransaction_completeTrade(pEnt1,pEnt2,pTradeData->pTrade1,pTradeData->pTrade2,pTradeData->func,pTradeData->pUserData,pData);

	StructDestroy(parse_invTradeData,pTradeData);
}

void invtransaction_trade(Entity *pEnt1, Entity *pEnt2, TradeBag *pTrade1, TradeBag *pTrade2, TransactionReturnCallback func, void *pvUserData )
{
	int i;
	ContainerTrade **ppContainers = NULL;
	TradeBag *pTradeBag = pTrade1;

	while (pTradeBag != NULL)
	{
		for(i=0;i<eaSize(&pTradeBag->ppTradeSlots);i++)
		{
			Item *pItem = pTradeBag->ppTradeSlots[i]->pItem;
			ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;

			if(pItemDef && pItemDef->eType == kItemType_Container && pItem->pSpecialProps)
			{
				ContainerTrade *pNewTrade = StructCreate(parse_ContainerTrade);

				pNewTrade->eContainerType = pItem->pSpecialProps->pContainerInfo->eContainerType;

				if(pItem->pSpecialProps->pContainerInfo->eContainerType == GLOBALTYPE_ENTITYSAVEDPET)
					pNewTrade->eOldID = StringToContainerID(REF_STRING_FROM_HANDLE(pItem->pSpecialProps->pContainerInfo->hSavedPet));

				pNewTrade->eNewID = 0;

				eaPush(&ppContainers,pNewTrade);
			}
		}

		if(pTradeBag == pTrade1)
			pTradeBag = pTrade2;
		else
			pTradeBag = NULL;
	}

	if(eaSize(&ppContainers) == 0)
	{
		invtransaction_completeTrade(pEnt1,pEnt2,pTrade1,pTrade2,func,pvUserData,NULL);
	}
	else
	{
		ContainerTradeData *pData = StructCreate(parse_ContainerTradeData);
		invTradeData *pTradeData = StructCreate(parse_invTradeData);

		eaCopy(&pData->ppContainers,&ppContainers);
		eaDestroy(&ppContainers);

		pTradeData->uEntID1 = entGetContainerID(pEnt1);
		pTradeData->eEntType1 = entGetType(pEnt1);
		pTradeData->uEntID2 = entGetContainerID(pEnt2);
		pTradeData->eEntType2 = entGetType(pEnt2);
		pTradeData->pTrade1 = StructClone(parse_TradeBag, pTrade1);
		pTradeData->pTrade2 = StructClone(parse_TradeBag, pTrade2);
		pTradeData->pUserData = pvUserData;
		pTradeData->func = func;

		pData->func = invTransaction_containerTradeFinished;
		pData->pUserData = pTradeData;

		ContainerTrade_Begin(pData);
	}
}

AUTO_TRANS_HELPER
	ATR_LOCKS(pTrueSrcEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster, .Psaved.Ppalwayspropslots, .Psaved.Pipetidsremovedfixup, .Psaved.Conowner.Containertype, .pInventoryV2.Peaowneduniqueitems, .Psaved.Pppreferredpetids, .pInventoryV2.Ppinventorybags, .Pplayer.Pugckillcreditlimit, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, pInventoryV2.ppLiteBags")
	ATR_LOCKS(pSrcEnt, ".Pplayer.Publicaccountname")
	ATR_LOCKS(pDstEnt, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppuppetmaster, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pplayer.Publicaccountname, .Psaved.Ppownedcontainers, .Hallegiance, .Hsuballegiance, .Psaved.Ppallowedcritterpets, .pInventoryV2.Ppinventorybags, .pInventoryV2.Pplitebags, .Pplayer.Playertype, .Pplayer.Pugckillcreditlimit, .Pchar.Ilevelexp, .pInventoryV2.Peaowneduniqueitems, .Itemidmax, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Uipoweridmax, .Psaved.Ipetid")
	ATR_LOCKS(eaDstPets, ".Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems")
	ATR_LOCKS(eaOldPets, ".*")
	ATR_LOCKS(eaNewPets, ".*");
bool TransferTradeBagItem(ATR_ARGS, ATH_ARG NOCONST(Entity) *pTrueSrcEnt, ItemDef *pTradeItemDef, TradeSlot *pTradeSlot,
						  ATH_ARG NOCONST(Entity) *pSrcEnt, ATH_ARG NOCONST(Entity) *pDstEnt, 
						  ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaDstPets,
						  ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaOldPets,
						  ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaNewPets,
						  ContainerTradeData *pContainerTradeData,
						  char **pestrSuccess, int i_item,
						  const ItemChangeReason *pSrcReason, const ItemChangeReason *pDestReason,
						  GameAccountDataExtract *pExtract,
						  GameAccountDataExtract *pDstExtract)
{
	S32 iPlayerCount;
	Item *pItem;
	ItemDef *pItemDef;

	if(pTradeItemDef->eType == kItemType_Container && pTradeSlot->pItem->pSpecialProps)
	{
		//This is a temp item that doesn't exist, but represents an owned container on the entity
		int iPet;
		U32 uiPetIDNew;
		ContainerID iPetID = StringToContainerID(REF_STRING_FROM_HANDLE(pTradeSlot->pItem->pSpecialProps->pContainerInfo->hSavedPet));
		NOCONST(Entity) *pPetEnt = NULL;
		int i;

		// Currently it's not allowed to have a valid SrcBagId when trading a container item
		if (pTradeSlot->SrcBagId >= 0)
		{
			TRANSACTION_APPEND_LOG_FAILURE("error: container trade has a SrcBagId %d", pTradeSlot->SrcBagId);
			return false;
		}

		for(i=0;i<eaSize(&pContainerTradeData->ppContainers);i++)
		{
			if(pContainerTradeData->ppContainers[i]->eOldID == iPetID)
			{
				int j;
				NOCONST(Entity) *pOldPet = NULL;

				for(j=0;j<eaSize(&eaOldPets);j++)
				{
					if(eaOldPets[j]->myContainerID == pContainerTradeData->ppContainers[i]->eOldID)
					{
						pOldPet = eaOldPets[j];
						break;
					}
				}

				if(!pOldPet)
					break;

				for(j=0;j<eaSize(&eaNewPets);j++)
				{
					if(eaNewPets[j]->myContainerID == pContainerTradeData->ppContainers[i]->eNewID)
					{
						pPetEnt = eaNewPets[j];
						StructCopyFieldsNoConst(parse_Entity,pOldPet,pPetEnt,TOK_PERSIST,TOK_NO_TRANSACT);

						//Put the containerID back because StructCopyFields will overwrite it
						pPetEnt->myContainerID = pContainerTradeData->ppContainers[i]->eNewID;

						objSetDebugName(pPetEnt->debugName, MAX_NAME_LEN,
							pPetEnt->myEntityType,
							pPetEnt->myContainerID, 0, NULL, NULL);
						break;
					}
				}
			}
		}
		
		if(ISNULL(pTrueSrcEnt->pSaved) || ISNULL(pDstEnt->pSaved) || ISNULL(pPetEnt) || ISNULL(pPetEnt->pSaved))
		{
			TRANSACTION_APPEND_LOG_FAILURE("error: Source entity %s@%s or Destination entity %s@%s are not capable of saved pet trades",
										   pSrcEnt->debugName,pSrcEnt->pPlayer->publicAccountName,pDstEnt->debugName,pDstEnt->pPlayer->publicAccountName);
			return false;
		}
		
		if(!trhOfficer_CanAddOfficer(pDstEnt,NULL,pDstExtract))
		{
			TRANSACTION_APPEND_LOG_FAILURE("error: Destination entity %s@%s cannot have any more pets",pDstEnt->debugName,pDstEnt->pPlayer->publicAccountName);
			return false;
		}
		
		for(iPet=eaSize(&pTrueSrcEnt->pSaved->ppOwnedContainers)-1;iPet>=0;iPet--)
		{
			if(pTrueSrcEnt->pSaved->ppOwnedContainers[iPet]->conID==iPetID)
			{
				NOCONST(PetRelationship) *pNewPetRel = StructCloneNoConst(parse_PetRelationship,pTrueSrcEnt->pSaved->ppOwnedContainers[iPet]);
				
				trh_RemoveSavedPetByID(ATR_PASS_ARGS,pTrueSrcEnt,iPetID);

				uiPetIDNew = entity_GetNextPetIDHelper(pDstEnt,pPetEnt->myContainerID);
				
				pNewPetRel->eState = OWNEDSTATE_OFFLINE;
				pNewPetRel->bTeamRequest = false;
				pNewPetRel->uiPetID = uiPetIDNew;
				pNewPetRel->conID = pPetEnt->myContainerID;
				
				pPetEnt->pSaved->iPetID = uiPetIDNew;
				pPetEnt->pSaved->conOwner.containerID = pDstEnt->myContainerID;
				pPetEnt->pSaved->conOwner.containerType = pDstEnt->myEntityType;
				
				entity_ResetPowerIDsAllHelper(ATR_PASS_ARGS, pPetEnt, pExtract);
				
				eaPush(&pDstEnt->pSaved->ppOwnedContainers,pNewPetRel);

				break;
			}
		}
		
		if(iPet==-1)
		{
			TRANSACTION_APPEND_LOG_FAILURE("error: saved pet %d could not be found or source entity %s@%s",iPetID,pSrcEnt->debugName,pSrcEnt->pPlayer->publicAccountName);
			return false;
		}
		
		if(pTrueSrcEnt->pSaved->pPuppetMaster)
		{
			for(iPet=eaSize(&pTrueSrcEnt->pSaved->pPuppetMaster->ppPuppets)-1;iPet>=0;iPet--)
			{
				if(pTrueSrcEnt->pSaved->pPuppetMaster->ppPuppets[iPet]->curID == iPetID)
				{
					NOCONST(PuppetEntity) *pPuppet = pTrueSrcEnt->pSaved->pPuppetMaster->ppPuppets[iPet];
					
					if(pPuppet->eState == PUPPETSTATE_ACTIVE)
					{
						TRANSACTION_APPEND_LOG_FAILURE("error: cannot remove active puppet (%d) in a trade %s@%s",iPetID,pSrcEnt->debugName,pSrcEnt->pPlayer->publicAccountName);
						return false;
					}
					
					eaRemove(&pTrueSrcEnt->pSaved->pPuppetMaster->ppPuppets,iPet);
					
					if(pDstEnt->pSaved->pPuppetMaster)
					{
						pPuppet->curID = pPetEnt->myContainerID;

						eaPush(&pDstEnt->pSaved->pPuppetMaster->ppPuppets,pPuppet);
					}
					else
					{
						StructDestroyNoConst(parse_PuppetEntity,pPuppet);
					}
				}
			}
		}

		estrConcatf(pestrSuccess, "%d %s, ", pTradeSlot->count, item_GetLogString(pTradeSlot->pItem));

	} else if (pTradeItemDef->eType != kItemType_Numeric) {
		InvBagIDs iDstBagIdx;
		
		//remove the item from the player inventory and save it
		iPlayerCount = inv_ent_trh_GetSlotItemCount(ATR_PASS_ARGS, pTrueSrcEnt, pTradeSlot->SrcBagId, pTradeSlot->SrcSlot, pExtract);
		pItem = invbag_RemoveItem(ATR_PASS_ARGS, pTrueSrcEnt, false, pTradeSlot->SrcBagId, pTradeSlot->SrcSlot, pTradeSlot->count, pSrcReason, pExtract);
		pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
		
		if (!pItem || !pItemDef) {
			TRANSACTION_APPEND_LOG_FAILURE("error: item %d (%s) in player %s@%s's offer points to invalid inventory slot",
										   i_item, pTradeItemDef->pchName, pSrcEnt->debugName, pSrcEnt->pPlayer->publicAccountName);
			StructDestroy(parse_Item, pItem);
			return false;
		}
		if (pTradeSlot->count > iPlayerCount) {
			TRANSACTION_APPEND_LOG_FAILURE("error: item %d (%s) in player %s@%s's offer has count %d when player only has %d",
										   i_item, pItemDef->pchName, pSrcEnt->debugName, pSrcEnt->pPlayer->publicAccountName, pTradeSlot->count, iPlayerCount);
			StructDestroy(parse_Item, pItem);
			return false;
		}
		if (pItem->flags & kItemFlag_Bound) {
			TRANSACTION_APPEND_LOG_FAILURE("error: item %d (%s) in player %s@%s's offer is bound",
										   i_item, pItemDef->pchName, pSrcEnt->debugName, pSrcEnt->pPlayer->publicAccountName);
			StructDestroy(parse_Item, pItem);
			return false;
		}
		
		// Mismatched id's are only okay if splitting a stack (i.e. same def name, stack limit greater than
		// one, and an offer count less than the maximum in the slot).
		if (pItem->id != pTradeSlot->pItem->id && 
			(pTradeSlot->count >= iPlayerCount || pItemDef->iStackLimit == 1 || pItemDef->pchName != pTradeItemDef->pchName)) {
			TRANSACTION_APPEND_LOG_FAILURE("error: item %d (%s) in player %s@%s's offer does not match the item in the inventory slot it points to",
										   i_item, pItemDef->pchName, pSrcEnt->debugName, pSrcEnt->pPlayer->publicAccountName);
			StructDestroy(parse_Item, pItem);
			return false;
		}
		
		item_trh_ClearPowerIDs(CONTAINER_NOCONST(Item,pItem));
		
		iDstBagIdx = itemAcquireOverride_FromTrade(pItemDef);
		if ( iDstBagIdx == InvBagIDs_None )
		{
			iDstBagIdx = pItemDef ? inv_trh_GetBestBagForItemDef(pDstEnt, pItemDef, iPlayerCount, true, pDstExtract) : InvBagIDs_Inventory;
		}
		
		if (inv_AddItem(ATR_PASS_ARGS, pDstEnt, eaDstPets, iDstBagIdx, -1, (Item*)pItem, pItemDef->pchName, 0, pDestReason, pDstExtract) != TRANSACTION_OUTCOME_SUCCESS)
		{
			TRANSACTION_APPEND_LOG_FAILURE("error: item %d (%s) in player %s@%s's offer could not be added to player %s@%s's inventory",
										   i_item, pItemDef->pchName, pSrcEnt->debugName, pSrcEnt->pPlayer->publicAccountName, pDstEnt->debugName, pDstEnt->pPlayer->publicAccountName);
			StructDestroy(parse_Item, pItem);
			return false;
		}
		StructDestroy(parse_Item, pItem);		
		estrConcatf(pestrSuccess, "%d %s, ", pTradeSlot->count, item_GetLogString(pTradeSlot->pItem));		
	} else {
		if (!(pTradeItemDef->flags & kItemDefFlag_Tradeable)) {
			TRANSACTION_APPEND_LOG_FAILURE("error: item %d (%s) in player %s@%s's offer is a non-tradeable numeric",
										   i_item, pTradeItemDef->pchName, pSrcEnt->debugName, pSrcEnt->pPlayer->publicAccountName);
			return false;
		}
		
		if (!inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pTrueSrcEnt, false, pTradeItemDef->pchName, -pTradeSlot->pItem->count, pSrcReason)) {
			TRANSACTION_APPEND_LOG_FAILURE("error: numeric item %d (%s) in player %s@%s's offer could not be removed",
										   i_item, pTradeItemDef->pchName, pSrcEnt->debugName, pSrcEnt->pPlayer->publicAccountName);
			return false;
		}
		
		if (!inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pDstEnt, false, pTradeItemDef->pchName, pTradeSlot->pItem->count, pDestReason)) {
			TRANSACTION_APPEND_LOG_FAILURE("error: numeric item %d (%s) in player %s@%s's offer could not be added to player %s@%s",
										   i_item, pTradeItemDef->pchName, pSrcEnt->debugName, pSrcEnt->pPlayer->publicAccountName, pDstEnt->debugName, pDstEnt->pPlayer->publicAccountName);
			return false;
		}
		
		estrConcatf(pestrSuccess, "1 %s, ", item_GetLogString(pTradeSlot->pItem));
	}

	return true;
}


AUTO_TRANS_HELPER
	ATR_LOCKS(pSrcEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pinventoryv2.Peaowneduniqueitems, .Pplayer.Publicaccountname, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster, .Psaved.Ppalwayspropslots, .Psaved.Pipetidsremovedfixup, .Pinventoryv2.Pplitebags, .Pplayer.Pugckillcreditlimit, .Psaved.Pppreferredpetids, .Pinventoryv2.Ppinventorybags, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype")
	ATR_LOCKS(eaSrcPets, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Psaved.Conowner.Containertype, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster, .Psaved.Ppalwayspropslots, .Psaved.Pipetidsremovedfixup, .Pinventoryv2.Peaowneduniqueitems, .Pinventoryv2.Pplitebags, .Psaved.Pppreferredpetids, .Pinventoryv2.Ppinventorybags, .Pplayer.Pugckillcreditlimit, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid")
	ATR_LOCKS(pDstEnt, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Ppuppetmaster, .Pplayer.Publicaccountname, .Psaved.Ppownedcontainers, .Hallegiance, .Hsuballegiance, .Psaved.Ppallowedcritterpets, .Pinventoryv2.Ppinventorybags, .Pinventoryv2.Pplitebags, .Pplayer.Playertype, .Pplayer.Pugckillcreditlimit, .Pchar.Ilevelexp, .Pinventoryv2.Peaowneduniqueitems, .Itemidmax, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Uipoweridmax, .Psaved.Ipetid")
	ATR_LOCKS(eaDstPets, ".Pcritter.Petdef, .Pinventoryv2.Peaowneduniqueitems")
	ATR_LOCKS(eaOldPets, ".*")
	ATR_LOCKS(eaNewPets, ".*");
bool inv_trh_TransferTradeBag(ATR_ARGS, ATH_ARG NOCONST(Entity) *pSrcEnt, 
							  ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaSrcPets,
							  ATH_ARG NOCONST(Entity) *pDstEnt, 
							  ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaDstPets,
							  ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaOldPets,
							  ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaNewPets,
							  ContainerTradeData *pContainerTradeData,
							  TradeBag *pTradeBag, char **pestrSuccess,
							  const ItemChangeReason *pSrcReason, const ItemChangeReason *pDestReason,
							  GameAccountDataExtract *pExtract,
							  GameAccountDataExtract *pDstExtract)
{
	S32 i;

	estrConcatf(pestrSuccess, "%s to %s (", pSrcEnt->debugName, pDstEnt->debugName);

	if(!eaSize(&pTradeBag->ppTradeSlots))
		estrConcatf(pestrSuccess, "nothing");

	for(i = 0; i < eaSize(&pTradeBag->ppTradeSlots); i++) {
		TradeSlot *pTradeSlot = pTradeBag->ppTradeSlots[i];
		ItemDef *pTradeItemDef = pTradeSlot && pTradeSlot->pItem ? GET_REF(pTradeSlot->pItem->hItem) : NULL;
		
		if (ISNULL(pTradeItemDef)) {
			TRANSACTION_APPEND_LOG_FAILURE("error: item %d in player %s@%s's offer is invalid", i, pSrcEnt->debugName, pSrcEnt->pPlayer->publicAccountName);
			return false;
		}

		if (pTradeSlot->tradeSlotPetID)
		{
			int j;
			for (j = eaSize(&eaSrcPets)-1; j >= 0; --j)
			{
				if (eaSrcPets[j]->myContainerID == (ContainerID)pTradeSlot->tradeSlotPetID)
				{
					if(!TransferTradeBagItem(ATR_PASS_ARGS,eaSrcPets[j],pTradeItemDef,pTradeSlot,pSrcEnt,pDstEnt,eaDstPets,eaOldPets,eaNewPets,pContainerTradeData,pestrSuccess,i,pSrcReason,pDestReason,pExtract,pDstExtract))
						return false;
					break;	
				}
			}
			if(j < 0) // no pet found
			{
				if(!TransferTradeBagItem(ATR_PASS_ARGS,pSrcEnt,pTradeItemDef, pTradeSlot,pSrcEnt,pDstEnt,eaDstPets,eaOldPets,eaNewPets,pContainerTradeData,pestrSuccess,i,pSrcReason,pDestReason,pExtract,pDstExtract))
					return false;
			}
		}
		else
		{
			if(!TransferTradeBagItem(ATR_PASS_ARGS,pSrcEnt,pTradeItemDef, pTradeSlot,pSrcEnt,pDstEnt,eaDstPets,eaOldPets,eaNewPets,pContainerTradeData,pestrSuccess,i,pSrcReason,pDestReason,pExtract,pDstExtract))
				return false;
		}
	}

	estrConcatf(pestrSuccess, ") ");

	return true;
}

//transaction to trade between two players
AUTO_TRANSACTION
	ATR_LOCKS(pEnt1, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Itemidmax, .Pplayer.Publicaccountname, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster, .Psaved.Ppalwayspropslots, .Psaved.Pipetidsremovedfixup, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Pppreferredpetids, .pInventoryV2.Ppinventorybags, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .pInventoryV2.Peaowneduniqueitems, .Pplayer.Pugckillcreditlimit, .Psaved.Ppallowedcritterpets, .pInventoryV2.Pplitebags, .Pplayer.Playertype")
	ATR_LOCKS(pEnt2, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Psaved.Ppalwayspropslots, .Psaved.Pipetidsremovedfixup, .Psaved.Pppreferredpetids, .Psaved.Ppuppetmaster, .Pplayer.Publicaccountname, .Psaved.Ppownedcontainers, .Hallegiance, .Hsuballegiance, .Psaved.Ppallowedcritterpets, .pInventoryV2.Ppinventorybags, .pInventoryV2.Pplitebags, .Pplayer.Playertype, .Pplayer.Pugckillcreditlimit, .Pchar.Ilevelexp, .pInventoryV2.Peaowneduniqueitems, .Itemidmax, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics")
	ATR_LOCKS(eaPets1, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Psaved.Conowner.Containertype, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster, .Psaved.Ppalwayspropslots, .Psaved.Pipetidsremovedfixup, .pInventoryV2.Peaowneduniqueitems, .Pplayer.Pugckillcreditlimit, .Psaved.Pppreferredpetids, .pInventoryV2.Ppinventorybags, .Pcritter.Petdef, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, pInventoryV2.ppLiteBags")
	ATR_LOCKS(eaPets2, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems, .Psaved.Ppbuilds, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster, .Psaved.Ppalwayspropslots, .Psaved.Pipetidsremovedfixup, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Psaved.Pppreferredpetids, .pInventoryV2.Ppinventorybags, .Pplayer.Pugckillcreditlimit, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, pInventoryV2.ppLiteBags")
	ATR_LOCKS(eaOldPets, ".*")
	ATR_LOCKS(eaNewPets, ".*");
enumTransactionOutcome inv_tr_trade(ATR_ARGS, NOCONST(Entity)* pEnt1, NOCONST(Entity)* pEnt2, CONST_EARRAY_OF(NOCONST(Entity)) eaPets1, CONST_EARRAY_OF(NOCONST(Entity)) eaPets2, ATR_ALLOW_FULL_LOCK CONST_EARRAY_OF(NOCONST(Entity)) eaOldPets, ATR_ALLOW_FULL_LOCK CONST_EARRAY_OF(NOCONST(Entity)) eaNewPets,NON_CONTAINER ContainerTradeData *pContainerTradeData, NON_CONTAINER TradeBag *pTrade1, NON_CONTAINER TradeBag *pTrade2, 
									const ItemChangeReason *pReason1, const ItemChangeReason *pReason2,
									GameAccountDataExtract *pExtract1, GameAccountDataExtract *pExtract2)
{
	char *estrSuccess = NULL;

	if (!pTrade1 || !pTrade2) {
		TRANSACTION_RETURN_LOG_FAILURE("error: NULL trade bag");
	}

	if (!inv_trh_TransferTradeBag(ATR_PASS_ARGS, pEnt1, eaPets1, pEnt2, eaPets2, eaOldPets, eaNewPets, pContainerTradeData, pTrade1, &estrSuccess, pReason1, pReason2, pExtract1, pExtract2)) {
		return TRANSACTION_OUTCOME_FAILURE;
	}
	if (!inv_trh_TransferTradeBag(ATR_PASS_ARGS, pEnt2, eaPets2, pEnt1, eaPets1, eaOldPets, eaNewPets, pContainerTradeData, pTrade2, &estrSuccess, pReason2, pReason1, pExtract2, pExtract1)) {
		return TRANSACTION_OUTCOME_FAILURE;
	}

	TRANSACTION_APPEND_LOG_SUCCESS("Success: %s", estrSuccess);
	estrDestroy(&estrSuccess);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void RemoteAddItemEventCallback(ItemTransCBData* data, CmdContext* context)
{
	Entity *pEnt = entFromContainerIDAnyPartition(context->clientType, context->clientID);
	ItemDef *pItemDef = item_DefFromName(data->pchItemDefName);

	if (!pEnt ||
		!pEnt->pPlayer)
		return;

	eventsend_RecordItemGained(pEnt, 
		data->pchItemDefName, 
		pItemDef ? pItemDef->peCategories : NULL,
		data->iCount);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void RemoteRemoveItemEventCallback(ItemTransCBData* data, CmdContext* context)
{
	Entity *pEnt = entFromContainerIDAnyPartition(context->clientType, context->clientID);
	ItemDef *pItemDef = item_DefFromName(data->pchItemDefName);

	if (!pEnt ||
		!pEnt->pPlayer )
		return;

	eventsend_RecordItemLost(pEnt, 
		data->pchItemDefName, 
		pItemDef ? pItemDef->peCategories : NULL,
		data->iCount);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void RemoteItemMovedActions(ItemTransCBData* pData, CmdContext* context)
{
	Entity *pEnt = entFromContainerIDAnyPartition(context->clientType, context->clientID);

	if (pEnt && pData)
	{
		GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		InventoryBag* pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), pData->eBagID, pExtract);
		int iPartitionIdx = entGetPartitionIdx(pEnt);
		
		if (pBag)
		{
			InventorySlot* pSlot = inv_GetSlotFromBag(pBag, pData->iBagSlot);
			if (pSlot && pSlot->pItem)
			{
				int i, j, iNumPowers = item_GetNumItemPowerDefs(pSlot->pItem, true);
				for (i = 0; i < iNumPowers; i++)
				{
					Power* pPower = item_GetPower(pSlot->pItem, i);
					PowerDef* pPowerDef = SAFE_GET_REF(pPower, hDef);
					if (pPowerDef)
					{
						switch (pPowerDef->eType)
						{
							xcase kPowerType_Combo:
							{
								if (item_ItemPowerActive(pEnt, pBag, pSlot->pItem, i))
								{
									power_CreateSubPowers(pPower);
								}
								else
								{
									power_DestroySubPowers(pPower);
								}
							}
							xcase kPowerType_Toggle:
							{
								if (!item_ItemPowerActive(pEnt, pBag, pSlot->pItem, i))
								{
									for (j = eaSize(&pEnt->pChar->ppPowerActToggle)-1; j >= 0; j--)
									{
										PowerActivation *pAct = pEnt->pChar->ppPowerActToggle[j];
										if (pAct->ref.uiID == pPower->uiID)
										{
											character_DeactivateToggle(iPartitionIdx, pEnt->pChar, pAct, pPower, pmTimestamp(0), true);
											break;
										}
									}
								}
							}
							xcase kPowerType_Passive:
							{
								if (!item_ItemPowerActive(pEnt, pBag, pSlot->pItem, i))
								{
									for (j = eaSize(&pEnt->pChar->ppPowerActPassive)-1; j >= 0; j--)
									{
										PowerActivation *pAct = pEnt->pChar->ppPowerActPassive[j];
										if (pAct->ref.uiID == pPower->uiID)
										{
											character_DeactivatePassive(iPartitionIdx, pEnt->pChar, pAct);
											break;
										}
									}
								}
							}
						}
					}
				}
			}
			invbag_HandleMoveEvents(pEnt, pBag, pData->iBagSlot, 0, 1, true);
			ClientCmd_gclBagHandleMoveEvents(pEnt, pData->eBagID, pData->iBagSlot);
		}
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void RemoteAddItemCallback(ItemTransCBData* data, CmdContext* context)
{
	Entity *pEnt = entFromContainerIDAnyPartition(context->clientType, context->clientID);
	ItemDef *pItemDef = NULL;
	char* tmpS = NULL;
	char* estrItemName = NULL;
	Language eLangID = locGetLanguage(getCurrentLocale());
	Item * pItem = NULL;
	GameAccountDataExtract *pExtract;

	if (!pEnt ||
		!pEnt->pPlayer )
		return;

	estrStackCreate(&estrItemName);
	estrStackCreate(&tmpS);

	if (data->pchItemDefName &&
		data->pchItemDefName[0] )
	{
		pItemDef = item_DefFromName(data->pchItemDefName);
	}

	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	pItem = inv_GetItemFromBag(pEnt, data->eBagID, data->iBagSlot, pExtract);

	item_GetFormattedNameFromUntranslatedStrings(pEnt, pItemDef, pItem, eLangID, data->bTranslateName, data->ppchNamesUntranslated, &estrItemName);

	if ( pItemDef &&
		pItemDef->eType == kItemType_Numeric )
	{
		//numeric item
		if (!(pItemDef->flags & kItemDefFlag_Silent))
		{
			// special case for XP floaters
			if ( stricmp(data->pchItemDefName, gConf.pcLevelingNumericItem) == 0 )
			{
				entFormatGameMessageKey(pEnt, &tmpS, "Reward.XPFloater", STRFMT_ITEMDEF(pItemDef), STRFMT_INT("Value", data->value),STRFMT_END);
				notify_NotifySend(pEnt, kNotifyType_ExperienceReceived, tmpS, NULL, NULL);

				//special case here to trigger a reward grant if a level threshold has been crossed
				{	
					int entLevelNumeric = item_GetLevelingNumeric(pEnt);
					int data_value = data->value / (pItemDef->fScaleUI ? pItemDef->fScaleUI : 1);
					int pre_level = LevelFromLevelingNumeric(entLevelNumeric - data_value);
					int post_level = LevelFromLevelingNumeric(entLevelNumeric);
					//if (!entity_CanLevelFully(pEnt) && post_level > g_TrialAccountRestrictions.iXPLevelCap) {
					//	post_level = g_TrialAccountRestrictions.iXPLevelCap;
					//}
					
					if (pre_level < post_level)
					{
						reward_LevelUp(pEnt, "DefaultLevelReward", post_level);
						eventsend_RecordLevelUp(pEnt);
                        gslGamePermissions_EntityLevelUp(pEnt, pre_level, post_level);
					}
				}
			}
			else if ( data->value > 0 )
			{
				const char* pchMessageKey = "Reward.NumericAdd";
				const char *pcTag = NULL;
				if (data->bFromRollover)
				{
					pcTag = "rollover";
				}
				if (data->bFromStore && RefSystem_ReferentFromString(gMessageDict, "Reward.NumericBought"))
				{
					pchMessageKey = "Reward.NumericBought";
				}
				estrClear(&tmpS);
				entFormatGameMessageKey(pEnt, &tmpS, pchMessageKey, STRFMT_ITEMDEF(pItemDef), STRFMT_STRING("Name", estrItemName),STRFMT_INT("Value", data->value ),STRFMT_END);
				notify_NotifySendWithOrigin(pEnt, kNotifyType_NumericReceived, tmpS, NULL, pcTag, data->value, data->vOrigin);
			}
			else
			{
				const char* pchMessageKey = "Reward.NumericSubtract";
				if (data->bFromStore && RefSystem_ReferentFromString(gMessageDict, "Reward.NumericSpent"))
				{
					pchMessageKey = "Reward.NumericSpent";
				}
				estrClear(&tmpS);
				entFormatGameMessageKey(pEnt, &tmpS, pchMessageKey, STRFMT_STRING("Name", estrItemName),STRFMT_INT("Value", -data->value ),STRFMT_END);
				notify_NotifySendWithOrigin(pEnt, kNotifyType_NumericLost, tmpS, NULL, NULL, -data->value, data->vOrigin);
			}
		}

	}
	else if (pItemDef && pItemDef->eType == kItemType_Lore)
	{
		estrClear(&tmpS);
		entFormatGameMessageKey(pEnt, &tmpS, "Reward.DiscoveredLore", STRFMT_ITEMDEF(pItemDef), STRFMT_STRING("Name", estrItemName),STRFMT_END);
		if (!(pItemDef->flags & kItemDefFlag_Silent)){
			ClientCmd_NotifySend(pEnt, kNotifyType_LoreDiscovered, tmpS, pItemDef->pchName, pItemDef->pchIconName);
		}
	}
	else if (pItemDef && (pItemDef->eType == kItemType_ItemRecipe || pItemDef->eType == kItemType_ItemPowerRecipe) && data->eBagID == InvBagIDs_Recipe)
	{
		estrClear(&tmpS);
		entFormatGameMessageKey(pEnt, &tmpS, "Crafting.LearnedRecipe", STRFMT_ITEMDEF(pItemDef), STRFMT_INT("Count", data->iCount), STRFMT_STRING("Name", estrItemName), STRFMT_END);
		if (!(pItemDef->flags & kItemDefFlag_Silent)){
			ClientCmd_NotifySend(pEnt, kNotifyType_CraftingRecipeLearned, tmpS, pItemDef->pchName, pItemDef->pchIconName);
		}
	}
	else if ( pItemDef && (	pItemDef->eType == kItemType_STOBridgeOfficer 
						||	pItemDef->eType == kItemType_AlgoPet ) )
	{
		if (!(pItemDef->flags & kItemDefFlag_Silent))
		{
			estrClear(&tmpS);
			if ((pItem && pItem->id == data->uiItemID) || (pItem = inv_GetItemByID(pEnt,data->uiItemID)))
			{
				estrClear(&estrItemName);
				item_GetDisplayNameFromPetCostume(pEnt, pItem, &estrItemName, NULL);
			}

			if (data->iCount <= 1)
				entFormatGameMessageKey(pEnt, &tmpS, "Reward.YouReceivedCandidate", STRFMT_ITEMDEF(pItemDef), STRFMT_STRING("Name", estrItemName), STRFMT_END);
			else
				entFormatGameMessageKey(pEnt, &tmpS, "Reward.YouReceivedCountCandidate", STRFMT_ITEMDEF(pItemDef), STRFMT_INT("Count", data->iCount), STRFMT_STRING("Name", estrItemName), STRFMT_END);
			notify_NotifySend(pEnt, kNotifyType_ItemReceived, tmpS, pItemDef->pchName, pItemDef->pchIconName);
			if (!(pItemDef->flags & kItemDefFlag_EquipOnPickup))
			{
				contact_CreatePotentialPetFromItem(pEnt, pEnt, data->eBagID, data->iBagSlot, pItemDef, true, true);
			}
		}
	}
	else if ( pItemDef && pItemDef->eType == kItemType_Injury )
	{
		if (!(pItemDef->flags & kItemDefFlag_Silent))
		{
			const char* pchItemTag = NULL;
			if(pItemDef->eTag)
			{
				pchItemTag = StaticDefineGetTranslatedMessage(ItemTagEnum, pItemDef->eTag);
			}
			entFormatGameMessageKey(pEnt, &tmpS, "Reward.YouReceivedInjury", STRFMT_ITEMDEF(pItemDef), STRFMT_INT("Count", data->iCount), STRFMT_STRING("Name", estrItemName), STRFMT_STRING("Severity", pchItemTag), STRFMT_END);
			notify_NotifySend(pEnt, kNotifyType_ItemReceived, tmpS, pItemDef->pchName, pItemDef->pchIconName);
		}
	}
	else if (pItemDef) // 'normal' items
	{
		if (!(pItemDef->flags & kItemDefFlag_Silent) && (pItemDef->eType != kItemType_RewardPack || !(pItemDef->flags & pItemDef->flags & kItemDefFlag_EquipOnPickup)))
		{
			const char *pchTitleFmt = pItemDef && pItemDef->eType == kItemType_Title ? langTranslateMessageKey(entGetLanguage(pEnt), "Reward.YouReceivedTitle") : NULL;
			estrClear(&tmpS);
			if (pchTitleFmt && pchTitleFmt[0])
			{
				entFormatGameString(pEnt, &tmpS, pchTitleFmt, STRFMT_ITEMDEF(pItemDef), STRFMT_STRING("Name", estrItemName), STRFMT_END);
			}
			else if (data->iCount <= 1)
			{
				const char* pchMessageKey = "Reward.YouReceivedItem";
				if (data->bFromStore && RefSystem_ReferentFromString(gMessageDict, "Reward.YouBoughtItem"))
				{
					pchMessageKey = "Reward.YouBoughtItem";
				}
				entFormatGameMessageKey(pEnt, &tmpS, pchMessageKey, STRFMT_ITEMDEF(pItemDef), STRFMT_STRING("Name", estrItemName), STRFMT_END);
			}
			else
			{
				const char* pchMessageKey = "Reward.YouReceivedCountItem";
				if (data->bFromStore && RefSystem_ReferentFromString(gMessageDict, "Reward.YouBoughtCountItem"))
				{
					pchMessageKey = "Reward.YouBoughtCountItem";
				}
				entFormatGameMessageKey(pEnt, &tmpS, pchMessageKey, STRFMT_ITEMDEF(pItemDef), STRFMT_INT("Count", data->iCount), STRFMT_STRING("Name", estrItemName), STRFMT_END);
			}
			if (pItem)
				notify_NotifySendWithItemID(pEnt, kNotifyType_ItemReceived, tmpS, pItemDef->pchName, pItemDef->pchIconName, pItem->id, data->iCount);
			else
				notify_NotifySendWithOrigin(pEnt, kNotifyType_ItemReceived, tmpS, pItemDef->pchName, pItemDef->pchIconName, data->iCount, data->vOrigin);

			if (g_ItemQualities.ppQualities && data->kQuality < (U32)eaSize(&g_ItemQualities.ppQualities))
			{
				if(g_ItemQualities.ppQualities[data->kQuality]->flags & kItemQualityFlag_ReportToSocialNetworks)
				{
					ActivityDataItem *activity = calloc(1, sizeof(ActivityDataItem));
					activity->name = strdup(estrItemName);
					activity->def_name = strdup(data->pchItemDefName);
					gslSocialActivity(pEnt, kActivityType_Item, activity);
				}
			}
			else
			{
				ErrorDetailsf("Item %s, Quality %i",NULL_TO_EMPTY(data->pchItemDefName), data->kQuality);
				Errorf("RemoteAddItemCallback received invalid ItemQuality.");
			}
		}

		// auto-equip items of this type
		if(pItemDef->flags & kItemDefFlag_EquipOnPickup)
		{
			switch (pItemDef->eType)
			{
			case kItemType_Device:
				// don't auto-equip bind-on-equip devices
				if(	gConf.bAutoEquipDevices && !(pItemDef->flags & kItemDefFlag_BindOnEquip) && (data->eBagID == InvBagIDs_Inventory || (data->eBagID >= InvBagIDs_PlayerBags && data->eBagID <= InvBagIDs_PlayerBag9)) )
					RemoteCommand_AutoEquipItem(pEnt->myEntityType, pEnt->myContainerID, pItemDef->pchName, SAFE_MEMBER(pItem, id), data->eBagID, data->iBagSlot);
			break;
			default:
				// ab: not sure what correct behavior here is.
//				RemoteCommand_AutoEquipItem(pEnt->myEntityType, pEnt->myContainerID, pItemDef->pchName, data->eBagID, data->iBagSlot);
			break;
			};
		}
	}

	estrDestroy(&tmpS);
	estrDestroy(&estrItemName);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void RemoteRemoveItemCallback(ItemTransCBData* data, CmdContext* context)
{
	/*This function has been gutted and its functionality moved to a generic callback, item_SendRemovalMessageCB in itemserver.c	
	  The function body has been left just in case somebody thinks of a use for a callback trigger by the transaction rather than from outside it.*/
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void RemoteAddTokenCallback(ItemTransCBData* data, CmdContext* context)
{
	Entity *pEnt = entFromContainerIDAnyPartition(context->clientType, context->clientID);

	if(pEnt)
	{
		characterclasses_SendClassList(pEnt);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void RemoteSkillTypeChangeCallback(ItemTransCBData* data, CmdContext* context)
{
	Entity *pEnt = entFromContainerIDAnyPartition(context->clientType, context->clientID);

	if ( pEnt &&
		pEnt->pPlayer )
	{
		const char *pchSkill = "{UNKNOWN SKILL}";

		switch (pEnt->pPlayer->SkillType)
		{
		case kSkillType_Arms:
			pchSkill = entTranslateMessageKey(pEnt, "SkillType.Arms");
			break;

		case kSkillType_Science:
			pchSkill = entTranslateMessageKey(pEnt, "SkillType.Science");
			break;

		case kSkillType_Mysticism:
			pchSkill = entTranslateMessageKey(pEnt, "SkillType.Mysticism");
			break;
		}

		if (pchSkill)
		{
			char * tmpS = NULL;
			estrStackCreate(&tmpS);
			entFormatGameMessageKey(pEnt, &tmpS, "SkillType.Set", STRFMT_STRING("Skill", pchSkill), STRFMT_END);
			notify_NotifySend(pEnt, kNotifyType_SkillSet, tmpS, NULL, NULL);
			estrDestroy(&tmpS);
		}
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void RemoteExperimentItemCallback(char* pchItemDefName, ItemTransCBData* pData, CmdContext *pContext)
{
	Entity *pEnt = entFromContainerIDAnyPartition(pContext->clientType, pContext->clientID);

	if (pEnt && pEnt->pPlayer)
	{
		Item* pItem = NULL;
		ItemDef * pItemDef = NULL;
		char* tmpS = NULL;
		char* estrItemName = NULL;
		Language eLangID = locGetLanguage(getCurrentLocale());
		GameAccountDataExtract *pExtract;
		
		estrStackCreate(&tmpS);
		estrStackCreate(&estrItemName);

		if (pchItemDefName &&
			pchItemDefName[0] )
		{
			pItemDef = item_DefFromName(pchItemDefName);
		}

		pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		pItem = inv_GetItemFromBag(pEnt, pData->eBagID, pData->iBagSlot, pExtract);

		item_GetFormattedNameFromUntranslatedStrings(pEnt, pItemDef, pItem, eLangID, pData->bTranslateName, pData->ppchNamesUntranslated, &estrItemName);

		if (pData->iCount <= 1)
			entFormatGameMessageKey(pEnt, &tmpS, "Crafting.Experiment.DeconstructedItem", STRFMT_STRING("Name", estrItemName), STRFMT_END);
		else
			entFormatGameMessageKey(pEnt, &tmpS, "Crafting.Experiment.DeconstructedItemCount", STRFMT_INT("Count", pData->iCount), STRFMT_STRING("Name", estrItemName), STRFMT_END);
		ClientCmd_NotifySend(pEnt, kNotifyType_ItemDeconstructed, tmpS, NULL, NULL);

		estrDestroy(&tmpS);
		estrDestroy(&estrItemName);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void RemoteExperimentCallback(NotifyType eNotifyType, const char *pchMsgKey, CmdContext *pContext)
{
	Entity *pEnt = entFromContainerIDAnyPartition(pContext->clientType, pContext->clientID);

	if (pEnt && pEnt->pPlayer)
	{
		char *tmpS = NULL;

		estrStackCreate(&tmpS);
		entFormatGameMessageKey(pEnt, &tmpS, pchMsgKey, STRFMT_END);
		ClientCmd_NotifySend(pEnt, eNotifyType, tmpS, NULL, NULL);

		estrDestroy(&tmpS);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void RemoteScheduleCostumeUnlockCallback(U64 uilItemId, CmdContext* pContext)
{
	Entity *pEnt = entFromContainerIDAnyPartition(pContext->clientType, pContext->clientID);
	Item *pItem = pEnt ? inv_GetItemByID(pEnt, uilItemId) : NULL;

	//Only runs on entities with players
	if(pEnt && pEnt->pPlayer && pItem)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		ItemDef *pDef = GET_REF(pItem->hItem);
		ItemChangeReason reason = {0};

		inv_FillItemChangeReason(&reason, pEnt, "Costume:CostumeUnlock", (pDef ? pDef->pchName : "Unknown") );

		AutoTrans_inv_tr_UnlockCostume(LoggedTransactions_MakeEntReturnVal("ScheduledCostumeUnlock", pEnt), GLOBALTYPE_GAMESERVER, 
				entGetType(pEnt), entGetContainerID(pEnt), 
				false, uilItemId, &reason, pExtract);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void RemoteScheduleInteriorUnlockCallback(U64 uilItemId, CmdContext* pContext)
{
	Entity *pEnt = entFromContainerIDAnyPartition(pContext->clientType, pContext->clientID);
	Item *pItem = pEnt ? inv_GetItemByID(pEnt, uilItemId) : NULL;

	//Only runs on entities with players
	if(pEnt && pEnt->pPlayer && pItem)
	{
		AutoTrans_inv_tr_UnlockInterior(LoggedTransactions_MakeEntReturnVal("ScheduledInteriorUnlock", pEnt), GLOBALTYPE_GAMESERVER, entGetType(pEnt), entGetContainerID(pEnt), false, uilItemId);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void RemoteUpdateItemSets(CmdContext* pContext, GlobalType eEntType, ContainerID uEntID)
{
	Entity *pOwner = entFromContainerIDAnyPartition(pContext->clientType, pContext->clientID);
	if (pOwner)
	{
		Entity *pEnt = entity_GetSubEntity(entGetPartitionIdx(pOwner), pOwner, eEntType, uEntID);
		if(pEnt)
		{
			ItemChangeReason reason = {0};
			inv_FillItemChangeReason(&reason, pEnt, "Item:UpdateItemSets", NULL);

			AutoTrans_inv_ent_tr_UpdateItemSets(LoggedTransactions_MakeEntReturnVal("ScheduledUpdateItemSets", pEnt), GLOBALTYPE_GAMESERVER, 
					entGetType(pEnt), entGetContainerID(pEnt), &reason);
		}
	}
}

typedef struct ItemSetCounter
{
	Item *pItemSet;
	Item **ppMembers;
	Item **ppMembersUnequipped;
} ItemSetCounter;

// Updates the uSetCount on all ItemSet-related Items in the Entity's Inventory
void entity_UpdateItemSetsCount(Entity *pEnt)
{
	if(g_bItemSets && pEnt && pEnt->pChar && pEnt->pInventoryV2)
	{
		S32 i, iBag;
		ItemDef **ppItemSets = NULL;
		ItemSetCounter **ppItemSetCounters = NULL;

		for(iBag = eaSize(&pEnt->pInventoryV2->ppInventoryBags)-1; iBag >= 0; iBag--)
		{
			S32 bEquipped;
			InventoryBag* pBag = pEnt->pInventoryV2->ppInventoryBags[iBag];

			// If it's the ItemSet bag, put every Item in it into an ItemSetCounter
			if(pBag->BagID==InvBagIDs_ItemSet)
			{
				for(i = eaSize(&pBag->ppIndexedInventorySlots)-1; i >= 0; i--)
				{
					InventorySlot* pSlot = pBag->ppIndexedInventorySlots[i];
					if(pSlot->pItem)
					{
						ItemDef *pItemSetDef = GET_REF(pSlot->pItem->hItem);
						if(pItemSetDef)
						{
							S32 iSet = eaFind(&ppItemSets,pItemSetDef);
							if(iSet>=0)
							{
								ANALYSIS_ASSUME(ppItemSetCounters && ppItemSetCounters[iSet]);
								ppItemSetCounters[iSet]->pItemSet = pSlot->pItem;
							}
							else
							{
								ItemSetCounter *pCounter = calloc(1,sizeof(ItemSetCounter));
								pCounter->pItemSet = pSlot->pItem;
								eaPush(&ppItemSets,pItemSetDef);
								eaPush(&ppItemSetCounters,pCounter);
							}
						}
					}
				}
				continue;
			}

			bEquipped = !!(invbag_flags(pBag) & (InvBagFlag_EquipBag|InvBagFlag_WeaponBag|InvBagFlag_ActiveWeaponBag|InvBagFlag_DeviceBag));

			// Otherwise find any Items that belong to an ItemSet, and put them into an
			//  ItemSetCounter as members
			for (i = eaSize(&pBag->ppIndexedInventorySlots)-1; i >= 0; i--)
			{
				InventorySlot* pSlot = pBag->ppIndexedInventorySlots[i];
				if(pSlot->pItem)
				{
					ItemDef *pItemDefSet;
					ItemDef *pItemDef = GET_REF(pSlot->pItem->hItem);

					if (pItemDef)
					{
						S32 iItemSet;
						for (iItemSet = eaSize(&pItemDef->ppItemSets)-1; iItemSet >= 0; iItemSet--)
						{
							pItemDefSet = GET_REF(pItemDef->ppItemSets[iItemSet]->hDef);
							if (pItemDefSet)
							{
								S32 iSet = eaFind(&ppItemSets,pItemDefSet);
								if(iSet>=0)
								{
									ANALYSIS_ASSUME(ppItemSetCounters && ppItemSetCounters[iSet]);
									if(bEquipped)
										eaPush(&ppItemSetCounters[iSet]->ppMembers,pSlot->pItem);
									else
										eaPush(&ppItemSetCounters[iSet]->ppMembersUnequipped,pSlot->pItem);
								}
								else
								{
									ItemSetCounter *pCounter = calloc(1,sizeof(ItemSetCounter));
									if(bEquipped)
										eaPush(&pCounter->ppMembers,pSlot->pItem);
									else
										eaPush(&pCounter->ppMembersUnequipped,pSlot->pItem);
									eaPush(&ppItemSets,pItemDefSet);
									eaPush(&ppItemSetCounters,pCounter);
								}
							}
						}
					}
				}
			}
		}

		// For all the Items related to each ItemSet, save the count of equipped members
		for(i=eaSize(&ppItemSetCounters)-1; i>=0; i--)
		{
			ItemSetCounter *pCounter = ppItemSetCounters[i];
			int j,iCount = eaSize(&pCounter->ppMembers);
			if(pCounter->pItemSet)
				pCounter->pItemSet->uSetCount = (U8)iCount;
			for(j=iCount-1; j>=0; j--)
				pCounter->ppMembers[j]->uSetCount = (U8)iCount;
			for(j=eaSize(&pCounter->ppMembersUnequipped)-1; j>=0; j--)
				pCounter->ppMembersUnequipped[j]->uSetCount = (U8)iCount;
			eaDestroy(&pCounter->ppMembers);
			eaDestroy(&pCounter->ppMembersUnequipped);
			free(pCounter);
		}

		eaDestroy(&ppItemSetCounters);
		eaDestroy(&ppItemSets);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void RemoteUpdateItemSetsPost(CmdContext* pContext)
{
	Entity *pEnt = entFromContainerIDAnyPartition(pContext->clientType, pContext->clientID);
	if(pEnt && pEnt->pChar)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

		entity_UpdateItemSetsCount(pEnt);
		character_DirtyInnateEquip(pEnt->pChar);
		character_ResetPowersArray(entGetPartitionIdx(pEnt), pEnt->pChar, pExtract);
	}
}

static void entCheckBankBagSize(Entity *pEnt, InventoryBag *pBag)
{
	S32 iBankSize;
	S32 iCurBankSize;
	S32 iBaseBankSize = 0;
    GameAccountDataExtract *pExtract;
	
	if (!pEnt || !pBag || !(invbag_flags(pBag) & InvBagFlag_BankBag)) {
		return;
	}

    pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	
	iBaseBankSize = invbag_basemaxslots(pEnt, pBag);
	iCurBankSize = invbag_maxslots(pEnt, pBag);
	iBankSize = ( GamePermission_ExtractHasToken(pExtract, GAME_PERMISSION_BANK_SLOTS_FROM_NUMERIC, false) ? inv_GetNumericItemValue(pEnt, "BankSize") : 0 ) + 
				inv_GetNumericItemValue(pEnt, "BankSizeMicrotrans") +
				iBaseBankSize;
	if (iBankSize > iCurBankSize) {

		AutoTrans_inv_tr_UpdateBankBag(NULL, GetAppGlobalType(), 
				pEnt->myEntityType, pEnt->myContainerID, 
				pBag->BagID, pExtract);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void RemoteNumericCallback(ItemTransCBData* data, CmdContext* context)
{
	Entity *pEnt = entFromContainerIDAnyPartition(context->clientType, context->clientID);
	ItemDef *pItemDef = item_DefFromName(data->pchItemDefName);
	char* tmpS = NULL;
	char* estrItemName = NULL;
	Language eLangID = locGetLanguage(getCurrentLocale());
	S32 i;
	estrStackCreate(&tmpS);
	estrStackCreate(&estrItemName);

	item_GetNameFromUntranslatedStrings(eLangID, data->bTranslateName, data->ppchNamesUntranslated, &estrItemName);

	if (pEnt && pEnt->pPlayer && estrItemName && estrItemName[0] && (data->type==0 || data->value != 0))
	{
		if (data->type ==0)
		{
			if (!(pItemDef->flags & kItemDefFlag_Silent))
			{
				NotifyType eNotifyType;
				estrClear(&tmpS);
				if (stricmp(data->pchItemDefName,"Level")==0)
				{
					inv_LevelNumericSet_GetNotifyString(pEnt, estrItemName, data->value, &tmpS);
					eNotifyType = kNotifyType_NumericLevelSet;
				}
				else
				{
					entFormatGameMessageKey(pEnt, &tmpS, "Reward.NumericSet", STRFMT_STRING("Name", estrItemName),STRFMT_INT("Value", data->value), STRFMT_END);
					eNotifyType = kNotifyType_NumericSet;
				}
				ClientCmd_NotifySend(pEnt, eNotifyType, tmpS, data->pchItemDefName, NULL);
			}
			//special case for leveling numeric
			if ( stricmp(data->pchItemDefName, gConf.pcLevelingNumericItem) == 0 )
			{
				// Clear project-specific log string
				estrDestroy(&pEnt->estrProjSpecificLogString);
			}
		}
		else
		{
			if (!(pItemDef->flags & kItemDefFlag_Silent))
			{
				estrClear(&tmpS);

				// special case for skill points
				// TODO: eventually replace hardcoded item name with some sort of data-based notification specs
				if (strcmpi(data->pchItemDefName, "SkillLevel") == 0)
				{
					int iSkillLevel = inv_GetNumericItemValue(pEnt, data->pchItemDefName);
					entFormatGameMessageKey(pEnt, &tmpS, "Crafting.SkillAdd", STRFMT_MESSAGE("Skill", StaticDefineGetMessage(SkillTypeEnum, pEnt->pPlayer->SkillType)), STRFMT_INT("Diff", data->value), STRFMT_INT("Value", iSkillLevel), STRFMT_END);
					notify_NotifySend(pEnt, kNotifyType_CraftingSkillChanged, tmpS, data->pchItemDefName, NULL);
				}
				else if ( stricmp(data->pchItemDefName, gConf.pcLevelingNumericItem) == 0 )
				{
					entFormatGameMessageKey(pEnt, &tmpS, "Reward.XPFloater", STRFMT_INT("Value", data->value),STRFMT_END);
					notify_NotifySend(pEnt, kNotifyType_ExperienceReceived, tmpS, NULL, NULL);

					//special case here to trigger a reward grant if a level threshold has been crossed
					{	
						int entLevelNumeric = item_GetLevelingNumeric(pEnt);
						int data_value = data->value / (pItemDef->fScaleUI ? pItemDef->fScaleUI : 1);
						int pre_level = LevelFromLevelingNumeric(entLevelNumeric - data_value);
						int post_level = LevelFromLevelingNumeric(entLevelNumeric);
						//if (!entity_CanLevelFully(pEnt) && post_level > g_TrialAccountRestrictions.iXPLevelCap) {
						//	post_level = g_TrialAccountRestrictions.iXPLevelCap;
						//}
						
						if (pre_level < post_level)
						{
							reward_LevelUp(pEnt, "DefaultLevelReward", post_level);
							eventsend_RecordLevelUp(pEnt);
                            gslGamePermissions_EntityLevelUp(pEnt, pre_level, post_level);
							gslItemAssignments_CheckExpressionSlots(pEnt,NULL);
						}
					}
				}
				else if (!data->bSilent)
				{
					if (data->value > 0)
					{
						const char* pchMessageKey = "Reward.NumericAdd";
						const char *pcTag = NULL;
						char* estrItemSold = NULL;
						if (data->bFromRollover)
						{
							pcTag = "rollover";
						}
						if (data->bFromStore && data->pchSoldItem && RefSystem_ReferentFromString(gMessageDict, "Reward.ItemSold"))
						{
							ItemDef* pDef = RefSystem_ReferentFromString(g_hItemDict, data->pchSoldItem);
							estrStackCreate(&estrItemSold);
							if (pDef)
								estrPrintf(&estrItemSold, "%s", TranslateDisplayMessage(pDef->displayNameMsg));
							pchMessageKey = "Reward.ItemSold";
						}
						else if (data->bFromStore && RefSystem_ReferentFromString(gMessageDict, "Reward.NumericBought"))
						{
							pchMessageKey = "Reward.NumericBought";
						}
						entFormatGameMessageKey(pEnt, &tmpS, pchMessageKey, STRFMT_STRING("Name", estrItemName), STRFMT_STRING("ItemSold", estrItemSold), STRFMT_INT("Value", data->value ), STRFMT_END);
						notify_NotifySendWithOrigin(pEnt, kNotifyType_NumericReceived, tmpS, data->pchItemDefName, pcTag, data->value, data->vOrigin);

						estrDestroy(&estrItemSold);
					}
					else
					{
						const char* pchMessageKey = "Reward.NumericSubtract";
						if (data->bFromStore && RefSystem_ReferentFromString(gMessageDict, "Reward.NumericSpent"))
						{
							pchMessageKey = "Reward.NumericSpent";
						}
						entFormatGameMessageKey(pEnt, &tmpS, pchMessageKey, STRFMT_STRING("Name", estrItemName),STRFMT_INT("Value", -data->value ), STRFMT_END);
						notify_NotifySendWithOrigin(pEnt, kNotifyType_NumericLost, tmpS, data->pchItemDefName, NULL, -data->value, data->vOrigin);
					}
				}
			}

			//special case for leveling numeric (usually XP) floaters
		}
	}
	estrDestroy(&tmpS);
	estrDestroy(&estrItemName);
	
	if (pEnt && pEnt->pInventoryV2)
	{
		if (!stricmp(data->pchItemDefName, "BankSize") 
			|| !stricmp(data->pchItemDefName, "BankSizeMicrotrans"))
		{
			for (i = 0; i < eaSize(&pEnt->pInventoryV2->ppInventoryBags); i++)
			{
				InventoryBag *pBag = pEnt->pInventoryV2->ppInventoryBags[i];
				if (invbag_flags(pBag) & InvBagFlag_BankBag)
					entCheckBankBagSize(pEnt, pBag);
			}
		}
		else if (!stricmp(data->pchItemDefName, "AddInvSlots")
			|| !stricmp(data->pchItemDefName, "AddInvSlotsMicrotrans"))
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

			AutoTrans_inv_tr_UpdateAdditionalInventorySlots(NULL, GetAppGlobalType(), 
				pEnt->myEntityType, pEnt->myContainerID,
				pExtract);
		}
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void RemoteNotifyUGCKillCreditLimit(CmdContext* pContext)
{
	Entity* pEnt = entFromContainerIDAnyPartition(pContext->clientType, pContext->clientID);
	if (pEnt)
	{
		char* estrMessage = NULL;
		estrStackCreate(&estrMessage);
		entFormatGameMessageKey(pEnt, &estrMessage, "Item.UI.UGCKillCreditLimit", STRFMT_END);
		notify_NotifySend(pEnt, kNotifyType_UGCKillCreditLimit, estrMessage, NULL, NULL);
		estrDestroy(&estrMessage);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void RemoteInteriorUnlockCallback(CmdContext* context)
{
	Entity *pEnt = entFromContainerIDAnyPartition(context->clientType, context->clientID);

	if (pEnt && pEnt->pPlayer)
	{
		char * tmpS = NULL;
		estrStackCreate(&tmpS);
		entFormatGameMessageKey(pEnt, &tmpS, "Item.UI.InteriorUnlock", STRFMT_END);
		//TODO: add the correct type of notification here
		notify_NotifySend(pEnt, kNotifyType_CostumeUnlocked, tmpS, NULL, NULL);
		estrDestroy(&tmpS);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void RemoteCostumeUnlockCallback(CmdContext* context)
{
	Entity *pEnt = entFromContainerIDAnyPartition(context->clientType, context->clientID);

	if ( pEnt &&
		 pEnt->pPlayer
	   )
	{
		char * tmpS = NULL;
		estrStackCreate(&tmpS);
		entFormatGameMessageKey(pEnt, &tmpS, "Item.UI.CostumeUnlock", STRFMT_END);
		notify_NotifySend(pEnt, kNotifyType_CostumeUnlocked, tmpS, NULL, NULL);
		estrDestroy(&tmpS);

		entity_SetDirtyBit(pEnt, parse_PlayerCostumeData, &pEnt->pSaved->costumeData, true);
		entity_SetDirtyBit(pEnt, parse_SavedEntityData, pEnt->pSaved, true);
	}
}

AUTO_TRANS_HELPER;
void inv_trh_ShrinkBag(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, S32 iBagID, GameAccountDataExtract *pExtract)
{
    int maxSlots;
    int i;
    NOCONST(InventoryBag) *pBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, iBagID, pExtract);

    maxSlots = invbag_trh_maxslots(pEnt, pBag);
    for ( i = eaSize(&pBag->ppIndexedInventorySlots) - 1; i >= maxSlots; i-- )
    {
        // Move any items beyond the expected bag size into the overflow bag.
        if ( pBag->ppIndexedInventorySlots[i]->pItem )
        {
            inv_ent_trh_MoveItem(ATR_PASS_ARGS, pEnt, true, iBagID, InvBagIDs_Overflow, i, -1, -1, false, NULL, pExtract);
        }
    }

    inv_trh_CollapseBag(ATR_PASS_ARGS, pBag);

    return;
}

AUTO_TRANS_HELPER;
enumTransactionOutcome inv_trh_UpdateBankBag(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, S32 iBagID, GameAccountDataExtract *pExtract)
{
	S32 iBankSize;
	S32 iDestBankSize;
	S32 iBaseBankSize = 0;
	NOCONST(InventoryBag) *pBag = inv_trh_GetBag( ATR_PASS_ARGS, pEnt, iBagID, pExtract);
	
	if (!pBag || (invbag_trh_flags(pBag) & InvBagFlag_BankBag) == 0) {
		return TRANSACTION_OUTCOME_FAILURE;
	}
	
    iBaseBankSize = invbag_trh_basemaxslots(pEnt, pBag);
	iBankSize = invbag_trh_maxslots(pEnt, pBag);
	iDestBankSize = ( GamePermission_ExtractHasToken(pExtract, GAME_PERMISSION_BANK_SLOTS_FROM_NUMERIC, false) ? inv_trh_GetNumericValue(ATR_PASS_ARGS, pEnt, "Banksize") : 0 ) +
					inv_trh_GetNumericValue(ATR_PASS_ARGS, pEnt, "BanksizeMicrotrans") +
					iBaseBankSize;
 	if (iDestBankSize != iBankSize) {
 		inv_bag_trh_SetMaxSlots(ATR_PASS_ARGS, pEnt, pBag, iDestBankSize);
 	}

    // If the bank bag is larger than it should be, then shrink it, moving any items in removed slots into overflow.
    if ( iDestBankSize < iBankSize || eaSize(&pBag->ppIndexedInventorySlots) > iDestBankSize )
    {
        inv_trh_ShrinkBag(ATR_PASS_ARGS, pEnt, iBagID, pExtract);
    }
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Playertype, .Psaved.Ppuppetmaster.Curtempid, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppallowedcritterpets, .Pinventoryv2.Ppinventorybags, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, pInventoryV2.ppLiteBags[]");
enumTransactionOutcome inv_tr_UpdateBankBag(ATR_ARGS, NOCONST(Entity)* pEnt, S32 iBagID, GameAccountDataExtract *pExtract)
{
    return inv_trh_UpdateBankBag(ATR_PASS_ARGS, pEnt, iBagID, pExtract);
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, "pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], .Pplayer.Playertype, pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
enumTransactionOutcome inv_tr_UpdateSharedBankBag(ATR_ARGS, NOCONST(Entity)* pEnt, S32 iBagID, GameAccountDataExtract *pExtract)
{
	S32 iBankSize;
	S32 iDestBankSize;
	S32 iBaseBankSize = 0;
	NOCONST(InventoryBag) *pBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, iBagID, pExtract);

	if (!pBag) {
		return TRANSACTION_OUTCOME_FAILURE;
	}

	iBaseBankSize = invbag_trh_basemaxslots(pEnt, pBag);
	iBankSize = invbag_trh_maxslots(pEnt, pBag);
	iDestBankSize = 
		gad_GetAttribIntFromExtract(pExtract, MicroTrans_GetSharedBankSlotGADKey())
		+ iBaseBankSize;
	if (iDestBankSize > iBankSize) {
        inv_bag_trh_SetMaxSlots(ATR_PASS_ARGS, pEnt, pBag, iDestBankSize);
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANS_HELPER;
enumTransactionOutcome inv_trh_UpdateAdditionalInventorySlots(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, GameAccountDataExtract *pExtract)
{
	S32 iNewInvSize, iAddInvSize, iExtraInvSize, iBaseInvSize;
	NOCONST(InventoryBag) *pBag = inv_trh_GetBag( ATR_PASS_ARGS, pEnt, 2 /* Literal InvBagIDs_Inventory */, pExtract);
	
	if (!pBag) {
		return TRANSACTION_OUTCOME_FAILURE;
	}
	
    iBaseInvSize = invbag_trh_basemaxslots(pEnt, pBag);
	iAddInvSize = GamePermission_ExtractHasToken(pExtract, GAME_PERMISSION_INVENTORY_SLOTS_FROM_NUMERIC, false) ? inv_trh_GetNumericValue(ATR_PASS_ARGS, pEnt, "AddInvSlots") : 0;
	iExtraInvSize = inv_trh_GetNumericValue(ATR_PASS_ARGS, pEnt, "AddInvSlotsMicrotrans");
	iNewInvSize = iBaseInvSize + iAddInvSize + iExtraInvSize;
	
 	if (iNewInvSize > invbag_trh_maxslots(pEnt, pBag)) {
 		inv_bag_trh_SetMaxSlots(ATR_PASS_ARGS, pEnt, pBag, iNewInvSize);
 	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, "pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], .Pplayer.Playertype, pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
enumTransactionOutcome inv_tr_UpdateAdditionalInventorySlots(ATR_ARGS, NOCONST(Entity)* pEnt, GameAccountDataExtract *pExtract)
{
    return inv_trh_UpdateAdditionalInventorySlots(ATR_PASS_ARGS, pEnt, pExtract);
}

//transaction to update ents player bags
AUTO_TRANSACTION 
ATR_LOCKS(pEnt, "pInventoryV2.ppInventoryBags[], .Pplayer.Playertype, pInventoryV2.ppLiteBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
enumTransactionOutcome inv_tr_UpdatePlayerBags(ATR_ARGS, NOCONST(Entity)* pEnt, GameAccountDataExtract *pExtract)
{
	inv_trh_UpdatePlayerBags(ATR_PASS_ARGS, pEnt, pExtract);

	TRANSACTION_RETURN_LOG_SUCCESS(
		"Updated Player Bags on Ent %s", 
		pEnt->debugName );
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pinventoryv2.Ppinventorybags, .Pplayer.Playertype, .Psaved.Ppuppetmaster.Curtype, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, pInventoryV2.ppLiteBags[]");
enumTransactionOutcome inv_tr_FixupBags(ATR_ARGS, NOCONST(Entity) *pEnt, GameAccountDataExtract *pExtract)
{
	int i;

	if(ISNULL(pEnt) || ISNULL(pEnt->pInventoryV2))
		return TRANSACTION_OUTCOME_FAILURE;

	inv_tr_UpdateAdditionalInventorySlots(ATR_PASS_ARGS, pEnt, pExtract);

	for (i = 0; i < eaSize(&pEnt->pInventoryV2->ppInventoryBags); i++) {
		NOCONST(InventoryBag) *pBag = pEnt->pInventoryV2->ppInventoryBags[i];
		if (invbag_trh_flags(pBag) & InvBagFlag_BankBag) {
			inv_tr_UpdateBankBag(ATR_PASS_ARGS, pEnt, pBag->BagID, pExtract);
		}
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pEntity, ".Pinventoryv2.Ppinventorybags, .Itemidmax");
enumTransactionOutcome inv_tr_FixupItemIDs(ATR_ARGS, NOCONST(Entity) *pEntity)
{
	int NumBags,iBag;

	if(ISNULL(pEntity))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if(ISNULL(pEntity->pInventoryV2))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	NumBags = eaSize(&pEntity->pInventoryV2->ppInventoryBags);
	for(iBag=0;iBag<NumBags;iBag++)
	{
		int NumSlots,iSlot,iFlags;

		NOCONST(InventoryBag) *pBag = pEntity->pInventoryV2->ppInventoryBags[iBag];
		iFlags = invbag_trh_flags(pBag);

		NumSlots = eaSize(&pBag->ppIndexedInventorySlots);
		for(iSlot=0;iSlot<NumSlots;iSlot++)
		{
			NOCONST(InventorySlot) *pSlot = pBag->ppIndexedInventorySlots[iSlot];

			if(NONNULL(pSlot->pItem) && pSlot->pItem->id == 0)
			{
				// fix ids that are zero
				item_trh_SetItemID(pEntity, pSlot->pItem);
			}
		}
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}


void inv_FixupBags(Entity *pEnt, TransactionReturnCallback pCB, void *userData)
{
	TransactionReturnVal *pVal = NULL;
	GameAccountDataExtract *pExtract;

	PERFINFO_AUTO_START_FUNC();

	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

	if(pCB)
	{
		pVal = objCreateManagedReturnVal(pCB, userData);
	}

	AutoTrans_inv_tr_FixupBags(pVal, GetAppGlobalType(), 
		pEnt->myEntityType, pEnt->myContainerID, pExtract);

	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void RemoteUpdatePlayerBags(CmdContext* context)
{
	TransactionReturnVal* returnVal;
	GameAccountDataExtract *pExtract;
	Entity *pEnt = entFromContainerIDAnyPartition(context->clientType, context->clientID);

	if (!pEnt ||
		!pEnt->pPlayer )
		return;

	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	returnVal = LoggedTransactions_CreateManagedReturnValEnt("UpdatePlayerBags", pEnt, NULL, NULL);

	AutoTrans_inv_tr_UpdatePlayerBags(returnVal, GetAppGlobalType(), 
			entGetType(pEnt), entGetContainerID(pEnt),
			pExtract);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void RemoteDirtyInnateEquipment(CmdContext* context)
{
	Entity *pEnt = entFromContainerIDAnyPartition(context->clientType, context->clientID);

	if (!pEnt ||
		!pEnt->pChar )
		return;

	character_DirtyInnateEquip(pEnt->pChar);
}


//transaction to set players skill type
AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pinventoryv2.Ppinventorybags[], .Pplayer.Skilltype, .Pplayer.Pugckillcreditlimit, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[]");
enumTransactionOutcome inv_tr_SetPlayerSkillType(ATR_ARGS, NOCONST(Entity)* pEnt, int SkillType, const ItemChangeReason *pReason)
{
	if ( ISNULL(pEnt) ||
		 ISNULL(pEnt->pPlayer) )
	{
		TRANSACTION_RETURN_LOG_FAILURE( "Invalid Ent" );
	}

	if (pEnt->pPlayer->SkillType != SkillType)
	{
		pEnt->pPlayer->SkillType = SkillType;

		//on reset skill level is set to 1, level cap to 100  !!!!hard coded here since it is special case code anyway
		if (!inv_ent_trh_ApplyNumeric(ATR_PASS_ARGS, pEnt, false, "SkilllevelCap", 100, NumericOp_SetTo, pReason)
             || !inv_ent_trh_ApplyNumeric(ATR_PASS_ARGS, pEnt, false, "Skilllevel", 1, NumericOp_SetTo, pReason))
		{
			TRANSACTION_RETURN_LOG_FAILURE(
				"Player SkillType set failed on Ent %s, could not set SkillLevel and/or SkilllevelCap", 
				pEnt->debugName );
		}

		{
			QueueRemoteCommand_RemoteSkillTypeChangeCallback(ATR_RESULT_SUCCESS, 0, 0, NULL);
		}
	}

	TRANSACTION_RETURN_LOG_SUCCESS(
		"Updated Player SkillType to %s", 
		StaticDefineIntRevLookup(SkillTypeEnum,pEnt->pPlayer->SkillType) );
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.ppInventoryBags[], .Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pplayer.Pugckillcreditlimit, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[]");
enumTransactionOutcome inv_tr_SetPlayerSkillAndSpecializationType(ATR_ARGS, NOCONST(Entity)* pEnt, int SkillType, int MinorSkillType, const ItemChangeReason *pReason)
{
	if ( ISNULL(pEnt) ||
		ISNULL(pEnt->pPlayer) )
	{
		TRANSACTION_RETURN_LOG_FAILURE( "Invalid Ent" );
	}

	if (pEnt->pPlayer->SkillType != SkillType || ea32Find(&(pEnt->pPlayer->eSkillSpecialization), MinorSkillType) < 0)
	{
		pEnt->pPlayer->SkillType = SkillType;
		eaiDestroy(&(pEnt->pPlayer->eSkillSpecialization));
		eaiPush(&(pEnt->pPlayer->eSkillSpecialization), MinorSkillType);

		//on reset skill level is set to 1, level cap to 100  !!!!hard coded here since it is special case code anyway
		if (!inv_ent_trh_ApplyNumeric(ATR_PASS_ARGS, pEnt, false, "SkilllevelCap", 100, NumericOp_SetTo, pReason)
			|| !inv_ent_trh_ApplyNumeric(ATR_PASS_ARGS, pEnt, false, "Skilllevel", 1, NumericOp_SetTo, pReason))
		{
			TRANSACTION_RETURN_LOG_FAILURE(
				"Player SkillType set failed on Ent %s, could not set SkillLevel and/or SkilllevelCap", 
				pEnt->debugName );
		}

		QueueRemoteCommand_RemoteSkillTypeChangeCallback(ATR_RESULT_SUCCESS, 0, 0, NULL);
	}

	TRANSACTION_RETURN_LOG_SUCCESS(
		"Updated Player SkillType to %s", 
		StaticDefineIntRevLookup(SkillTypeEnum,pEnt->pPlayer->SkillType) );
}

// Set the player's Skill type
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Inventory) ACMD_HIDE ACMD_PRIVATE;
void SetSkillType(Entity* pEnt, int SkillType)
{
	TransactionReturnVal* returnVal;
	ItemChangeReason reason = {0};

	if (!pEnt ||
		!pEnt->pPlayer )
		return;

	returnVal = LoggedTransactions_CreateManagedReturnValEnt("SetSkillType", pEnt, NULL, NULL);

	inv_FillItemChangeReason(&reason, pEnt, "Skill:SetSkillType", NULL);

	AutoTrans_inv_tr_SetPlayerSkillType(returnVal, GetAppGlobalType(), 
			entGetType(pEnt), entGetContainerID(pEnt), 
			SkillType, &reason);
}

// Set the player's Skill and specialization type
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Inventory) ACMD_HIDE ACMD_PRIVATE;
void SetSkillAndSpecializationType(Entity* pEnt, int SkillType, int MinorSkillType)
{
	TransactionReturnVal* returnVal;
	ItemChangeReason reason = {0};

	if (!pEnt ||
		!pEnt->pPlayer )
		return;

	returnVal = LoggedTransactions_CreateManagedReturnValEnt("SetSkillAndSpecializationType", pEnt, NULL, NULL);

	inv_FillItemChangeReason(&reason, pEnt, "Skill:SetSkillAndSpecializationType", NULL);

	AutoTrans_inv_tr_SetPlayerSkillAndSpecializationType(returnVal, GetAppGlobalType(), 
			entGetType(pEnt), entGetContainerID(pEnt), 
			SkillType, MinorSkillType, &reason);
}

// Set the players Skill type
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Inventory) ACMD_HIDE ACMD_PRIVATE;
void SetSkill(Entity* pEnt, ACMD_NAMELIST(SkillTypeEnum, STATICDEFINE) char *pString)
{
	SkillType SkillType = StaticDefineIntGetInt(SkillTypeEnum, pString);

	SetSkillType(pEnt, SkillType);
}

// Set the players Skill Level
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(7) ACMD_CATEGORY(standard, inventory, debug, csr);
void SetSkillLevel(Entity* pEnt, S32 value)
{
	TransactionReturnVal* returnVal;
	ItemChangeReason reason = {0};

	if (!pEnt ||
		!pEnt->pPlayer )
		return;

	returnVal = LoggedTransactions_CreateManagedReturnValEnt("SetSkillLevel", pEnt, NULL, NULL);

	inv_FillItemChangeReason(&reason, pEnt, "Skill:SetSkillLevel", NULL);

	AutoTrans_inv_tr_ApplyNumeric(returnVal, GetAppGlobalType(), 
			entGetType(pEnt), entGetContainerID(pEnt), 
			"Skilllevel", value, NumericOp_SetTo, &reason);
}

// Set the player's Skill Level cap
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(7) ACMD_CATEGORY(standard, inventory, debug, csr);
void SetSkillLevelCap(Entity* pEnt, S32 value)
{
	TransactionReturnVal* returnVal;
	ItemChangeReason reason = {0};

	if (!pEnt ||
		!pEnt->pPlayer )
		return;

	returnVal = LoggedTransactions_CreateManagedReturnValEnt("SetSkillLevelCap", pEnt, NULL, NULL);

	inv_FillItemChangeReason(&reason, pEnt, "Skill:SetSkillLevelCap", NULL);

	AutoTrans_inv_tr_ApplyNumeric(returnVal, GetAppGlobalType(), 
			entGetType(pEnt), entGetContainerID(pEnt), 
			"Skilllevelcap", value, NumericOp_SetTo, &reason);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pItem, ".*");
bool item_trh_DeconstructItem(ATR_ARGS, GlobalType iEntType, ContainerID iContainerID, ATH_ARG NOCONST(Item) *pItem, int iCount, int iEPValue, ItemCraftingComponent ***peaResultComponents)
{
	ItemDef *pBaseDef = NULL;
	ItemCraftingComponent **eaComponents = NULL;
	int i, j;
	bool bAdded = false;
	int iGreatestCompIdx = -1, iGreatestCompCount = 0;

	if (ISNULL(pItem))
	{
#ifdef GAMESERVER
		QueueRemoteCommand_RemoteExperimentCallback(ATR_RESULT_FAIL, iEntType, iContainerID, kNotifyType_ExperimentFailed, "Crafting.Experiment.Failed");
#endif
		TRANSACTION_APPEND_LOG_FAILURE("No item to deconstruct");
		return false;
	}
	pBaseDef = GET_REF(pItem->hItem);
	if (!pBaseDef)
	{
#ifdef GAMESERVER
		QueueRemoteCommand_RemoteExperimentCallback(ATR_RESULT_FAIL, iEntType, iContainerID, kNotifyType_ExperimentFailed, "Crafting.Experiment.Failed");
#endif
		TRANSACTION_APPEND_LOG_FAILURE("Invalid item def name %s", REF_STRING_FROM_HANDLE(pItem->hItem));
		return false;
	}
	if (pBaseDef->flags & kItemDefFlag_Fused)
	{
#ifdef GAMESERVER
		QueueRemoteCommand_RemoteExperimentCallback(ATR_RESULT_FAIL, iEntType, iContainerID, kNotifyType_ExperimentFailed, "Crafting.Experiment.Failed");
#endif
		TRANSACTION_APPEND_LOG_FAILURE("Fused item '%s' cannot be deconstructed", pBaseDef->pchName);
		return false;
	}

	item_GetDeconstructionComponents((Item*) pItem, &eaComponents);

	if (!eaComponents && iEPValue == 0)
	{
#ifdef GAMESERVER
		QueueRemoteCommand_RemoteExperimentCallback(ATR_RESULT_FAIL, iEntType, iContainerID, kNotifyType_ExperimentFailed, "Crafting.Experiment.Failed");
#endif
		TRANSACTION_APPEND_LOG_FAILURE("Item in %s cannot be deconstructed.", pBaseDef->pchName);
		return false;
	}

	for (i = eaSize(&eaComponents) - 1; i >= 0; i--)
	{
		ItemDef *pComponentDef = GET_REF(eaComponents[i]->hItem);
		int iCompCount = item_GetComponentCount(eaComponents[i]);
		int iReceivedCount = 0;

		// determine most plentiful component in case we need to grant a pity component
		if (iCompCount > iGreatestCompCount)
		{
			iGreatestCompIdx = i;
			iGreatestCompCount = iCompCount;
		}

		// do the rolls for the components
		for (j = 0; j < iCount * iCompCount; j++)
		{
			F32 roll = randomPositiveF32Seeded(NULL, RandType_LCG);

			if (roll <= eaComponents[i]->fWeight)
				iReceivedCount++;
		}

		if (iReceivedCount > 0)
		{
			ItemCraftingComponent *pReceivedComponent = StructClone(parse_ItemCraftingComponent, eaComponents[i]);
			pReceivedComponent->fCount = iReceivedCount;
			eaPush(peaResultComponents, pReceivedComponent);
			bAdded = true;
		}
	}

	// grant some component if, by some improbable chance, nothing is granted
	if (!bAdded && iGreatestCompIdx >= 0)
	{
		ItemCraftingComponent *pReceivedComponent = StructClone(parse_ItemCraftingComponent, eaComponents[iGreatestCompIdx]);
		pReceivedComponent->fCount = 1.0f;
		eaPush(peaResultComponents, pReceivedComponent);	
	}

	eaDestroyStruct(&eaComponents, parse_ItemCraftingComponent);
	return true;
}

static int inv_ExperimentComponentCmpCount(const ItemCraftingComponent **pComponent1, const ItemCraftingComponent **pComponent2)
{
	return (int)(*pComponent2)->fCount - (int)(*pComponent1)->fCount;
}

static int inv_ExperimentComponentCmpDef(const ItemCraftingComponent **pComponent1, const ItemCraftingComponent **pComponent2)
{
	ItemDef *pDef1 = GET_REF((*pComponent1)->hItem);
	ItemDef *pDef2 = GET_REF((*pComponent2)->hItem);
	return (intptr_t)pDef2 - (intptr_t)pDef1;
}

// Experiments on the items specified in the experiment data
AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Pchar.Hpath, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pplayer.Skilltype, .pInventoryV2.Pplitebags, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .pInventoryV2.Peaowneduniqueitems, .pInventoryV2.Ppinventorybags, .Pplayer.Pugckillcreditlimit, .Pplayer.Playertype, .Psaved.Ppownedcontainers, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets")
	ATR_LOCKS(eaPets, ".Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems");
enumTransactionOutcome inv_tr_Experiment(ATR_ARGS, NOCONST(Entity)* pEnt, 
										 CONST_EARRAY_OF(NOCONST(Entity)) eaPets,
										 NON_CONTAINER ExperimentData *pExperimentData, 
										 GiveRewardBagsData *pRewardBagsData, 
										 const char *pchNotificationMsgKey,
										 const ItemChangeReason *pReason,
										 GameAccountDataExtract *pExtract)
{
	ItemCraftingComponent **peaComponents = NULL;
	ItemDef *pLastUniqueComponent = NULL;
	NOCONST(Item) **peaItems = NULL;
	char *estrSuccess = NULL;
	int i;

	if (ISNULL(pEnt) || ISNULL(pEnt->pPlayer) || ISNULL(pExperimentData))
	{
		if (NONNULL(pEnt))
			QueueRemoteCommand_RemoteExperimentCallback(ATR_RESULT_FAIL, pEnt->myEntityType, pEnt->myContainerID, kNotifyType_ExperimentFailed, "Crafting.Experiment.Failed");
		TRANSACTION_RETURN_LOG_FAILURE("Invalid params");
	}

	estrPrintf(&estrSuccess, "Experiment %s(%i) {",
		StaticDefineIntRevLookup(SkillTypeEnum, pEnt->pPlayer->SkillType),
		inv_trh_GetNumericValue(ATR_PASS_ARGS, pEnt, "SkillLevel"));

	// first remove all items while getting their EP values
	for (i = 0; i < eaSize(&pExperimentData->ppEntry); i++)
	{
		ExperimentEntry *pExperimentEntry = pExperimentData->ppEntry[i];
		Item *pItem = invbag_RemoveItem(ATR_PASS_ARGS, pEnt, true, pExperimentEntry->SrcBagId, pExperimentEntry->SrcSlot, pExperimentEntry->count, pReason, pExtract);

		if (pItem)
		{
			ItemTransCBData *pData;
			ItemDef *pItemDef = GET_REF(pItem->hItem);

			if (pItemDef && (pItemDef->flags & kItemDefFlag_Enigma))
			{
				eaDestroyStructNoConst(&peaItems, parse_Item);
				StructDestroy(parse_Item, pItem);
				QueueRemoteCommand_RemoteExperimentCallback(ATR_RESULT_FAIL, pEnt->myEntityType, pEnt->myContainerID, kNotifyType_ExperimentFailed, "Crafting.Experiment.Failed");
				estrDestroy(&estrSuccess);
				TRANSACTION_RETURN_LOG_FAILURE("Enigma item '%s' cannot be experimented on", pItemDef->pchName);
			}
			if (pItemDef && pItemDef->kSkillType != kSkillType_None && !(pItemDef->kSkillType & pEnt->pPlayer->SkillType))
			{
				eaDestroyStructNoConst(&peaItems, parse_Item);
				StructDestroy(parse_Item, pItem);
				QueueRemoteCommand_RemoteExperimentCallback(ATR_RESULT_FAIL, pEnt->myEntityType, pEnt->myContainerID, kNotifyType_ExperimentFailed, "Crafting.Experiment.Failed");
				estrDestroy(&estrSuccess);
				TRANSACTION_RETURN_LOG_FAILURE("Item '%s' belongs to a different crafting school than player's", pItemDef->pchName);
			}

			pData = StructCreate(parse_ItemTransCBData);
			item_trh_GetNameUntranslated(CONTAINER_NOCONST(Item,pItem), pEnt, &pData->ppchNamesUntranslated, &pData->bTranslateName);
			pData->iCount = pExperimentEntry->count;
			QueueRemoteCommand_RemoteExperimentItemCallback(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID, pItemDef->pchName, pData);
			eaPush(&peaItems, CONTAINER_NOCONST(Item,pItem));
			eaClear(&pData->ppchNamesUntranslated);
			StructDestroy(parse_ItemTransCBData, pData);
			estrConcatf(&estrSuccess, "%s%i%s", !i ? "" : ",", pExperimentEntry->count, item_GetLogString((Item*) pItem));
		}
		else
		{
			eaDestroyStructNoConst(&peaItems, parse_Item);
			QueueRemoteCommand_RemoteExperimentCallback(ATR_RESULT_FAIL, pEnt->myEntityType, pEnt->myContainerID, kNotifyType_ExperimentFailed, "Crafting.Experiment.Failed");
			estrDestroy(&estrSuccess);
			TRANSACTION_RETURN_LOG_FAILURE("Error removing %i items in slot %i from bag %s", pExperimentEntry->count, pExperimentEntry->SrcSlot, StaticDefineIntRevLookup(InvBagIDsEnum, pExperimentEntry->SrcBagId));
		}
	}

	estrConcatf(&estrSuccess, "}->{");

	// this should never be true, but we should check just in case
	if (eaSize(&peaItems) != eaSize(&pExperimentData->ppEntry))
	{
		eaDestroyStructNoConst(&peaItems, parse_Item);
		QueueRemoteCommand_RemoteExperimentCallback(ATR_RESULT_FAIL, pEnt->myEntityType, pEnt->myContainerID, kNotifyType_ExperimentFailed, "Crafting.Experiment.Failed");
		estrDestroy(&estrSuccess);
		TRANSACTION_RETURN_LOG_FAILURE("Number of experimentation entries don't match number of removed items");
	}

	// compile the array of deconstruction components
	for (i = 0; i < eaSize(&peaItems); i++)
	{
		ItemDef *pItemDef = peaItems[i] ? GET_REF(peaItems[i]->hItem) : NULL;
		if (!item_trh_DeconstructItem(ATR_PASS_ARGS, pEnt->myEntityType, pEnt->myContainerID, peaItems[i], pExperimentData->ppEntry[i]->count, pExperimentData->ppEntry[i]->iEPValue, &peaComponents))
		{
			eaDestroyStructNoConst(&peaItems, parse_Item);
			eaDestroyStruct(&peaComponents, parse_ItemCraftingComponent);
			estrDestroy(&estrSuccess);
			TRANSACTION_RETURN_LOG_FAILURE("Item '%s' failed deconstruction", pItemDef ? pItemDef->pchName : "NULL");
		}
	}
	eaDestroyStructNoConst(&peaItems, parse_Item);

	// aggregate the crafting components and sort them by descending count
	eaQSort(peaComponents, inv_ExperimentComponentCmpDef);
	for (i = eaSize(&peaComponents) - 1; i >= 0; i--)
	{
		ItemDef *pComponentDef = GET_REF(peaComponents[i]->hItem);

		if (!pComponentDef)
		{
			QueueRemoteCommand_RemoteExperimentCallback(ATR_RESULT_FAIL, pEnt->myEntityType, pEnt->myContainerID, kNotifyType_ExperimentFailed, "Crafting.Experiment.Failed");
			eaDestroyStruct(&peaComponents, parse_ItemCraftingComponent);
			estrDestroy(&estrSuccess);
			TRANSACTION_RETURN_LOG_FAILURE("Component def %s could not be found.", REF_STRING_FROM_HANDLE(peaComponents[i]->hItem));
		}

		if (!pLastUniqueComponent || pComponentDef != pLastUniqueComponent)
			pLastUniqueComponent = pComponentDef;
		else
		{
			peaComponents[i]->fCount += peaComponents[i + 1]->fCount;
			StructDestroy(parse_ItemCraftingComponent, eaRemove(&peaComponents, i + 1));
		}
	}
	eaQSort(peaComponents, inv_ExperimentComponentCmpCount);

	// give the components (and experimentation rewards) to the player last (to make sure inventory space is maximized)
	for (i = 0; i < eaSize(&peaComponents); i++)
	{
		ItemDef *pComponentDef = GET_REF(peaComponents[i]->hItem);
		int iCount = (int) peaComponents[i]->fCount;
		Item *item = NULL;

		if (!pComponentDef)
		{
			QueueRemoteCommand_RemoteExperimentCallback(ATR_RESULT_FAIL, pEnt->myEntityType, pEnt->myContainerID, kNotifyType_ExperimentFailed, "Crafting.Experiment.Failed");
			eaDestroyStruct(&peaComponents, parse_ItemCraftingComponent);
			estrDestroy(&estrSuccess);
			TRANSACTION_RETURN_LOG_FAILURE("Component def %s could not be found.", REF_STRING_FROM_HANDLE(peaComponents[i]->hItem));
		}
		
		item = item_FromDefName(pComponentDef->pchName);
		CONTAINER_NOCONST(Item, item)->count = iCount;
		if (iCount > 0 && inv_AddItem(ATR_PASS_ARGS, pEnt, eaPets, InvBagIDs_Overflow, -1, item, pComponentDef->pchName, ItemAdd_Silent, pReason, pExtract) != TRANSACTION_OUTCOME_SUCCESS)
		{
			QueueRemoteCommand_RemoteExperimentCallback(ATR_RESULT_FAIL, pEnt->myEntityType, pEnt->myContainerID, kNotifyType_ExperimentFailed, "Crafting.Experiment.InventoryFull");
			eaDestroyStruct(&peaComponents, parse_ItemCraftingComponent);
			estrDestroy(&estrSuccess);
			StructDestroy(parse_Item,item);
			TRANSACTION_RETURN_LOG_FAILURE("%i of component %s could not be given.", iCount, pComponentDef->pchName);
		}

		StructDestroy(parse_Item,item);
		estrConcatf(&estrSuccess, "%s%i[I(%s)]", !i ? "" : ",", iCount, pComponentDef->pchName);
	}

	estrConcatf(&estrSuccess, "}+{");

	eaDestroyStruct(&peaComponents, parse_ItemCraftingComponent);

	// log contents of reward bags
	rewardbagsdata_Log(pRewardBagsData,&estrSuccess);

	// grant reward bags to player
	if (!inv_trh_GiveRewardBags(ATR_PASS_ARGS, pEnt, eaPets, pRewardBagsData, kRewardOverflow_ForceOverflowBag, NULL, pReason, pExtract, NULL))
	{
		QueueRemoteCommand_RemoteExperimentCallback(ATR_RESULT_FAIL, pEnt->myEntityType, pEnt->myContainerID, kNotifyType_ExperimentFailed, "Crafting.Experiment.InventoryFull");
		estrDestroy(&estrSuccess);
		TRANSACTION_RETURN_LOG_FAILURE("Could not give rewards");
	}
	else
		QueueRemoteCommand_RemoteExperimentCallback(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID, kNotifyType_ExperimentComplete, pchNotificationMsgKey);

	estrConcatf(&estrSuccess, "}");
	TRANSACTION_APPEND_LOG_SUCCESS("%s", estrSuccess);
	estrDestroy(&estrSuccess);

	return TRANSACTION_OUTCOME_SUCCESS;
}

static void ExperimentReturn(TransactionReturnVal *pReturn, void *iEntityID)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, (intptr_t)iEntityID);
	if (pEnt)
	{
		// I hate the numerics handling code so much.
		S32 iSkillLevel = inv_GetNumericItemValue(pEnt, "SkillLevel");
		S32 iSkillLevelCap = inv_GetNumericItemValue(pEnt, "SkillLevelCap");
		if (iSkillLevel >= iSkillLevelCap)
		{
			char *pch = NULL;
			entFormatGameMessageKey(pEnt, &pch, "Crafting_Notify_SkillCapReached",
				STRFMT_ENTITY(pEnt), STRFMT_END);
			notify_NotifySend(pEnt, kNotifyType_CraftingSkillCapReached, pch, NULL, NULL);
			estrDestroy(&pch);
		}

		// Pop up loot dialog if experimentation was successful
		if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
			ClientCmd_LootOverflow(pEnt);
	}
}

// Grants experimentation rewards
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Inventory) ACMD_PRIVATE;
void Experiment(Entity* pEnt, ExperimentData *pExperimentData)
{
	TransactionReturnVal* returnVal;
	GameAccountDataExtract *pExtract;
	GameInteractable *pInteractable;
	WorldInteractionPropertyEntry *pEntry = NULL;
	U32 iServerTime;
	int i, iTotalEPValue = 0;

	if (!pEnt ||
		!pEnt->pPlayer ||
		!pEnt->pPlayer->InteractStatus.bInteracting ||
		!pEnt->pPlayer->pInteractInfo ||
		!pEnt->pPlayer->pInteractInfo->bCrafting ||
		pEnt->pPlayer->pInteractInfo->eCraftingTable == kSkillType_None ||
		!(pEnt->pPlayer->pInteractInfo->eCraftingTable & pEnt->pPlayer->SkillType) ||
		!pExperimentData )
		return;

	// check to make sure experimentation is not attempted before allotted time interval
	iServerTime = timeSecondsSince2000();
	if (pEnt->pPlayer->iLastExperimentTime + ITEM_EXPERIMENT_DELAY_SECS > iServerTime)
		return;
	else
		pEnt->pPlayer->iLastExperimentTime = iServerTime;

	// make sure overflow bag is empty
	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	if (!inv_ent_BagEmpty(pEnt, InvBagIDs_Overflow, pExtract))
	{
		return;
	}

	// calculate item/total EP values
	for (i = 0; i < eaSize(&pExperimentData->ppEntry); i++)
	{
		ExperimentEntry *pExperimentEntry = pExperimentData->ppEntry[i];
		Item *pItem = inv_GetItemFromBag(pEnt, pExperimentEntry->SrcBagId, pExperimentEntry->SrcSlot, pExtract);

		if (!pItem)
		{
			return;
		}

		pExperimentEntry->iEPValue = item_GetEPValue(entGetPartitionIdx(pEnt), pEnt, pItem);
		iTotalEPValue += (pExperimentEntry->iEPValue * pExperimentEntry->count);
	}

	pInteractable = interactable_GetByNode(GET_REF(pEnt->pPlayer->InteractStatus.interactTarget.hInteractionNode));
	if (pInteractable)
		pEntry = interactable_GetPropertyEntry(pInteractable, pEnt->pPlayer->InteractStatus.interactTarget.iInteractionIndex);
	if (pEntry)
	{
		WorldCraftingInteractionProperties *pCraftingProps = interaction_GetCraftingProperties(pEntry);
		if (pCraftingProps)
		{	
			U32* eaPets = NULL;
			RewardContext *pCraftingRewardContext = Reward_CreateOrResetRewardContext(NULL);
			GiveRewardBagsData *pRewardBagsData = StructCreate(parse_GiveRewardBagsData);
			RewardTable *pRewardTable = GET_REF(pCraftingProps->hExperimentRewardTable);
			RewardTable *pCurrTable = pRewardTable;
			RewardTable *pEPTable = NULL;
			const char *pchNotificationMsgKey = "Crafting.Experiment.SuccessMild";
			ItemChangeReason reason = {0};

			// generate reward bags from experimentation reward table
			reward_CraftingContextInitialize(pEnt, pCraftingRewardContext, iTotalEPValue, 0);
			reward_generate(entGetPartitionIdx(pEnt), pEnt, pCraftingRewardContext, pRewardTable, &pRewardBagsData->ppRewardBags, NULL, NULL);

			// find the correct notification msg key for the experimentation success level
			while (pCurrTable && reward_RangeTable(pCurrTable) && !pEPTable)
			{
				switch (reward_GetRangeTableType(pCurrTable))
				{
					xcase kRewardChoiceType_LevelRange:
						pCurrTable = reward_GetRangeTable(pCurrTable, pCraftingRewardContext->RewardLevel);
					xcase kRewardChoiceType_SkillRange:
						pCurrTable = reward_GetRangeTable(pCurrTable, pCraftingRewardContext->SkillLevel);
					xcase kRewardChoiceType_EPRange:
						pEPTable = pCurrTable;
					xdefault:
						pCurrTable = NULL;
				}
			}

			// calculate the success level of the reward
			if (pEPTable)
			{
				int iMaxEntries = eaSize(&pEPTable->ppRewardEntry);
				int iRewardIdx = -1;
				for (i = 0; i < eaSize(&pEPTable->ppRewardEntry); i++)
				{
					if (iTotalEPValue >= pEPTable->ppRewardEntry[i]->MinLevel && iTotalEPValue <=pEPTable->ppRewardEntry[i]->MaxLevel)
					{
						iRewardIdx = i + 1;
						break;
					}
				}

				if (iRewardIdx * 2 < iMaxEntries)
					pchNotificationMsgKey = "Crafting.Experiment.SuccessMild";
				else if (iRewardIdx * 4 < iMaxEntries * 3)
					pchNotificationMsgKey = "Crafting.Experiment.SuccessModerate";
				else
					pchNotificationMsgKey = "Crafting.Experiment.SuccessMajor";
			}

			ea32Create(&eaPets);
			Entity_GetPetIDList(pEnt, &eaPets);

			inv_FillItemChangeReason(&reason, pEnt, "Craft:Experiment", NULL);

			// perform transaction
			returnVal = LoggedTransactions_CreateManagedReturnValEnt("Experiment", pEnt, ExperimentReturn, (void *)(intptr_t)entGetContainerID(pEnt));
			AutoTrans_inv_tr_Experiment(returnVal, GetAppGlobalType(), 
				entGetType(pEnt), entGetContainerID(pEnt), 
				GLOBALTYPE_ENTITYSAVEDPET, &eaPets,
				pExperimentData, pRewardBagsData, pchNotificationMsgKey, &reason, pExtract);

			// cleanup
			StructDestroy(parse_GiveRewardBagsData, pRewardBagsData);
			ea32Destroy(&eaPets);

			// Destroy the reward context
			StructDestroy(parse_RewardContext, pCraftingRewardContext);
		}
	}
}

// This command drills into the currently interacted crafting station's experimentation reward table until
// it finds an EP-ranged table.  The command gets the starting EP values of all of the table's ranged entries
// and sends those values to the client for previewing.
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Inventory) ACMD_PRIVATE;
void ExperimentGetRanges(Entity *pEnt)
{
	ExperimentRanges *pRanges = NULL;
	GameInteractable *pInteractable;
	WorldInteractionPropertyEntry *pEntry = NULL;
	RewardTable *pRewardTable = NULL, *pEPTable = NULL;
	RewardContext *pLocalContext = NULL;
	int i;

	// verify the player is interacting with a crafting station
	if (!pEnt ||
		!pEnt->pPlayer ||
		!pEnt->pPlayer->InteractStatus.bInteracting ||
		!pEnt->pPlayer->pInteractInfo ||
		!pEnt->pPlayer->pInteractInfo->bCrafting ||
		pEnt->pPlayer->pInteractInfo->eCraftingTable == kSkillType_None ||
		!(pEnt->pPlayer->pInteractInfo->eCraftingTable & pEnt->pPlayer->SkillType))
		return;

	// get the experimentation reward table set on the crafting station
	pInteractable = interactable_GetByNode(GET_REF(pEnt->pPlayer->InteractStatus.interactTarget.hInteractionNode));
	if (pInteractable)
		pEntry = interactable_GetPropertyEntry(pInteractable, pEnt->pPlayer->InteractStatus.interactTarget.iInteractionIndex);
	if (pEntry)
	{
		WorldCraftingInteractionProperties *pCraftingProps = interaction_GetCraftingProperties(pEntry);
		if (pCraftingProps)
		{
			pRewardTable = GET_REF(pCraftingProps->hExperimentRewardTable);
		}
	}
	if (!pRewardTable)
		return;

	pLocalContext = Reward_CreateOrResetRewardContext(NULL);

	if (pLocalContext == NULL)
		return;

	// initialize the reward context
	reward_CraftingContextInitialize(pEnt, pLocalContext, 0, 0);

	// iteratively drill into the reward table(s) until we hit an EP ranged table
	while (pRewardTable && reward_RangeTable(pRewardTable) && !pEPTable)
	{
		switch (reward_GetRangeTableType(pRewardTable))
		{
			xcase kRewardChoiceType_LevelRange:
				pRewardTable = reward_GetRangeTable(pRewardTable, pLocalContext->RewardLevel);
			xcase kRewardChoiceType_SkillRange:
				pRewardTable = reward_GetRangeTable(pRewardTable, pLocalContext->SkillLevel);
			xcase kRewardChoiceType_EPRange:
				pEPTable = pRewardTable;
			xcase kRewardChoiceType_TimeRange:
				pRewardTable = reward_GetRangeTable(pRewardTable, pLocalContext->TimeLevel);
			xdefault:
				pRewardTable = NULL;
		}
	}

	// Destroy the reward context
	StructDestroy(parse_RewardContext, pLocalContext);

	if (!pEPTable)
		return;

	// grab the min value from all the range entries to get a list of range values that can be used
	// to determine where a reward will fall in the EP range table; this assumes that the range entries
	// are contiguous
	pRanges = StructCreate(parse_ExperimentRanges);
	for (i = 0; i < eaSize(&pEPTable->ppRewardEntry); i++)
		eaiPush(&pRanges->eaiRanges, pEPTable->ppRewardEntry[i]->MinLevel);

	// send the ranges to the client (will be sorted on the client just in case)
	ClientCmd_ExperimentUpdateRanges(pEnt, pRanges);

	// cleanup
	StructDestroy(parse_ExperimentRanges, pRanges);
}

// Iterate over all items that the entity has and make sure their IDs match the
// expected ID for the 
AUTO_TRANS_HELPER
	ATR_LOCKS(pEnt, ".Pinventoryv2, .Itemidmax");
void inv_ent_FixItemIDs(ATH_ARG NOCONST(Entity) *pEnt)
{
	S32 iBag;
	S32 iSlot;
	if (!pEnt->pInventoryV2)
		return;
	for (iBag = 0; iBag < eaSize(&pEnt->pInventoryV2->ppInventoryBags); iBag++)
	{
		NOCONST(InventoryBag) *pBag = pEnt->pInventoryV2->ppInventoryBags[iBag];
		for (iSlot = 0; iSlot < eaSize(&pBag->ppIndexedInventorySlots); iSlot++)
		{
			NOCONST(InventorySlot) *pSlot = pBag->ppIndexedInventorySlots[iSlot];
			if (pSlot && pSlot->pItem)
				item_trh_SetItemID(pEnt, pSlot->pItem);
		}
	}
}

// Iterate over all items in Name Indexed bags and make sure the name on the
// Inventory Slot matches the item in that slot.
void inv_ent_FixIndexedItemNames(NOCONST(Entity) *pEnt)
{
	S32 iBag;
	S32 iSlot;
	if (!pEnt->pInventoryV2)
		return;
	for (iBag = 0; iBag < eaSize(&pEnt->pInventoryV2->ppInventoryBags); iBag++)
	{
		NOCONST(InventoryBag) *pBag = pEnt->pInventoryV2->ppInventoryBags[iBag];
		if (invbag_trh_flags(pBag) & InvBagFlag_NameIndexed){
			NOCONST(InventorySlot) **eaSlotsToAdd = NULL;
			for (iSlot = eaSize(&pBag->ppIndexedInventorySlots)-1; iSlot >= 0; --iSlot)
			{
				NOCONST(InventorySlot) *pSlot = pBag->ppIndexedInventorySlots[iSlot];
				const char *pchItemName = NULL;
				
				// Find the name of the item in this slot
				if (pSlot->pItem){
					ItemDef *pItemDef = GET_REF(pSlot->pItem->hItem);
					pchItemName = pItemDef?pItemDef->pchName:REF_STRING_FROM_HANDLE(pSlot->pItem->hItem);
				}

				// If the item name doesn't match the slot name, remove the slot and repair it.
				if (pchItemName && pSlot->pchName && stricmp(pchItemName, pSlot->pchName) != 0){
					eaRemove(&pBag->ppIndexedInventorySlots, iSlot);
					pSlot->pchName = allocAddString(pchItemName);
					eaPush(&eaSlotsToAdd, pSlot);
				}
			}

			// Add all of the repaired InventorySlots back to the bag, if they don't exist
			for (iSlot = eaSize(&eaSlotsToAdd)-1; iSlot >= 0; --iSlot){
				if (!eaIndexedGetUsingString(&pBag->ppIndexedInventorySlots, eaSlotsToAdd[iSlot]->pchName)){
					eaIndexedAdd(&pBag->ppIndexedInventorySlots, eaSlotsToAdd[iSlot]);
					eaRemove(&eaSlotsToAdd, iSlot);
				} else {
					// If a slot with that name already existed, we'll just destroy the extra item.
					// I don't think I want to preserve duplicates in this case, since this only
					// affects indexed bags, which are things like recipes, titles, etc.
					// (Item is destroyed below)
				}
			}
			eaDestroyStructNoConst(&eaSlotsToAdd, parse_InventorySlot);
		}
	}
}

// Sets default team loot settings for new characters
void inv_ent_InitializeNewPlayerSettings(NOCONST(Entity) *pEnt)
{
	if (pEnt && pEnt->pTeam)
	{
		pEnt->pTeam->eLootMode = gConf.pcDefaultLootMode ? StaticDefineIntGetInt(LootModeEnum,gConf.pcDefaultLootMode) : LootMode_NeedOrGreed;
		pEnt->pTeam->eLootQuality = allocAddString(gConf.pcNeedBeforeGreedThreshold ? gConf.pcNeedBeforeGreedThreshold : FALLBACK_NEEDORGREED_THRESHOLD);
	}
}

//transaction to remove an item from a bag
AUTO_TRANSACTION
ATR_LOCKS(pEntity, ".Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, .Pinventoryv2.Pplitebags, .Pinventoryv2.Ppinventorybags, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid");
enumTransactionOutcome inv_tr_IdentifyItemInBag(ATR_ARGS, NOCONST(Entity)* pEntity, S32 iBag, S32 iSlot, const char* pchScrollDefName, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	NOCONST(Item)* pItem = inv_trh_GetItemFromBag(ATR_PASS_ARGS, pEntity, iBag, iSlot, pExtract);
	ItemDef* pItemDef = GET_REF(pItem->hItem);
	int iCount = inv_trh_FindItemCountByDefName(ATR_PASS_ARGS, pEntity, pchScrollDefName, 1, true, pReason, pExtract);
	ItemDef* pScrollDef = RefSystem_ReferentFromString(g_hItemDict, pchScrollDefName);

	if (iCount > 0 && pScrollDef->eType == kItemType_IdentifyScroll && pScrollDef->iLevel >= item_trh_GetLevel(pItem))
	{
		if (pItemDef->eType == kItemType_UnidentifiedWrapper)
		{
			if (pItem->pSpecialProps)
			{
				//create the actual item, destroy the stub.
				NOCONST(InventorySlot)* pSlot = inv_ent_trh_GetSlotPtr(ATR_PASS_ARGS, pEntity, iBag, iSlot, pExtract);
				NOCONST(Item)* pResultItem = inv_ItemInstanceFromDefName(REF_STRING_FROM_HANDLE(pItem->pSpecialProps->hIdentifiedItemDef), 0, 0, NULL, NULL, NULL, false, NULL);
				pResultItem->id = pSlot->pItem->id;
				StructDestroyNoConst(parse_Item, pSlot->pItem);
				pSlot->pItem = pResultItem;
			}
			else
			{
				Errorf("Entity tried to identify an UnidentifiedWrapper item that was missing pSpecialProps. Def name: %s", pItemDef->pchName);
				TRANSACTION_RETURN_LOG_FAILURE(
					"Ent %s failed to identify an item", 
					pEntity->debugName );
			}
		}
		else
		{
			pItem->flags &= ~kItemFlag_Unidentified_Unsafe;
		}
		TRANSACTION_RETURN_LOG_SUCCESS(
			"Item identified on Ent %s", 
			pEntity->debugName );
	}
	if (iCount <= 0)
		QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, pEntity->myEntityType, pEntity->myContainerID, "Item.FailedIdentify", kNotifyType_ItemUseFailed);
	else if (pScrollDef->iLevel < item_trh_GetLevel(pItem))
		QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, pEntity->myEntityType, pEntity->myContainerID, "Item.FailedIdentify.LevelTooLow", kNotifyType_ItemUseFailed);
		
	TRANSACTION_RETURN_LOG_FAILURE(
		"Ent %s failed to identify an item", 
		pEntity->debugName );
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, "pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
enumTransactionOutcome inv_ent_tr_SetInvBagHideMode(ATR_ARGS, NOCONST(Entity)* pEnt, int iBagID, int bHide, GameAccountDataExtract *pExtract)
{
	NOCONST(InventoryBag)* pBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt,iBagID, pExtract);
	S32 i;

	if (ISNULL(pBag) || pBag->bHideCostumes == bHide) {
		TRANSACTION_RETURN_LOG_FAILURE("Failed to change the bag hide state from Ent %s", pEnt->debugName);
	}

	pBag->bHideCostumes = bHide;

	// Clear the bHideCostumes flag for each inventory slot
	for (i = eaSize(&pBag->ppIndexedInventorySlots)-1; i >= 0; i--)
	{
		pBag->ppIndexedInventorySlots[i]->bHideCostumes = false;
	}
	TRANSACTION_RETURN_LOG_SUCCESS("Changed the bag hide state from Ent %s", pEnt->debugName);
}

static void SetInvBagHideModeInternal(Entity* pOwner, Entity* pEnt, InvBagIDs eBagID, bool bHide)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pOwner);
	InventoryBag* pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), eBagID, pExtract);
	if (pBag && (invbag_flags(pBag) & InvBagFlag_CostumeHideable) && !pBag->bHideCostumes != !bHide)
	{
		TransactionReturnVal* pReturn;
		pReturn = LoggedTransactions_CreateManagedReturnValEnt("SetInvBagHideMode", pOwner, NULL, NULL);
		AutoTrans_inv_ent_tr_SetInvBagHideMode(pReturn, GetAppGlobalType(), 
			pEnt->myEntityType, pEnt->myContainerID, 
			eBagID, !!bHide, pExtract);
	}
}

// Sets the hide costume mode for an inventory bag
// This command is not hidden or private due to the possibility - however remote - that a player
//   might wish to have a macro show/hide their helmet or whatever. It seems harmless to leave it exposed.
AUTO_COMMAND ACMD_SERVERONLY ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Inventory);
void SetInvBagHideMode(Entity *pEnt, InvBagIDs eBagID, int bHide)
{
	SetInvBagHideModeInternal(pEnt, pEnt, eBagID, bHide);
}

// Sets the hide costume mode for an inventory bag on a pet
AUTO_COMMAND ACMD_SERVERONLY ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Inventory) ACMD_HIDE ACMD_PRODUCTS(StarTrek);
void SetPetInvBagHideMode(Entity *pEnt, U32 eEntType, U32 uEntID, InvBagIDs eBagID, int bHide)
{
	Entity* pSrcEnt = entity_GetSubEntity(entGetPartitionIdx(pEnt), pEnt, eEntType, uEntID);
	if (pSrcEnt)
	{
		SetInvBagHideModeInternal(pEnt, pSrcEnt, eBagID, bHide);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, "pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]");
enumTransactionOutcome inv_ent_tr_SetInvSlotHideMode(ATR_ARGS, 
													 NOCONST(Entity)* pEnt, 
													 int iBagID, 
													 int iSlot, 
													 int bHide,
													 GameAccountDataExtract *pExtract)
{
	NOCONST(InventorySlot)* pSlot = inv_ent_trh_GetSlotPtr(ATR_PASS_ARGS, pEnt, iBagID, iSlot, pExtract);

	if (ISNULL(pSlot) || pSlot->bHideCostumes == (U32)bHide) 
	{
		TRANSACTION_RETURN_LOG_FAILURE("Failed to set the hide costume state for Bag %d, Slot %d on Ent %s", 
			iBagID, iSlot, pEnt->debugName);
	}
	pSlot->bHideCostumes = !!bHide;
	TRANSACTION_RETURN_LOG_SUCCESS("Changed the hide costume state for Bag %d, Slot %d on Ent %s", 
		iBagID, iSlot, pEnt->debugName);
}

static void SetInvSlotHideModeInternal(Entity* pOwner, Entity* pEnt, InvBagIDs eBagID, S32 iSlot, bool bHide)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pOwner);
	InventoryBag* pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), eBagID, pExtract);
	if (pBag && (invbag_flags(pBag) & InvBagFlag_CostumeHideablePerSlot))
	{
		InventorySlot* pSlot = inv_GetSlotFromBag(pBag, iSlot);
		if (pSlot && !pSlot->bHideCostumes != !bHide)
		{
			TransactionReturnVal* pReturn;
			pReturn = LoggedTransactions_CreateManagedReturnValEnt("SetInvSlotHideMode", pOwner, NULL, NULL);
			AutoTrans_inv_ent_tr_SetInvSlotHideMode(pReturn, GetAppGlobalType(), 
					pEnt->myEntityType, pEnt->myContainerID, 
					eBagID, iSlot, !!bHide, pExtract);
		}
	}
}
// Sets the hide costume mode for an inventory slot
// This command is not hidden or private due to the possibility - however remote - that a player
//   might wish to have a macro show/hide their helmet or whatever. It seems harmless to leave it exposed.
AUTO_COMMAND ACMD_SERVERONLY ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Inventory);
void SetInvSlotHideMode(Entity* pEnt, InvBagIDs eBagID, S32 iSlot, bool bHide)
{
	SetInvSlotHideModeInternal(pEnt, pEnt, eBagID, iSlot, bHide);
}

AUTO_COMMAND ACMD_SERVERONLY ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Inventory) ACMD_HIDE;
void SetInvSlotHideModeForEnt(Entity* pEnt, U32 eEntType, ContainerID uEntID, InvBagIDs eBagID, S32 iSlot, bool bHide)
{
	Entity* pSrcEnt = entity_GetSubEntity(entGetPartitionIdx(pEnt), pEnt, eEntType, uEntID);
	if (pSrcEnt)
	{
		SetInvSlotHideModeInternal(pEnt, pSrcEnt, eBagID, iSlot, bHide);
	}
}

#ifdef GAMESERVER

static void inv_ent_AddDoorKeyItemFromDef_CB(TransactionReturnVal* returnVal, void* pData)
{
	EntityRef *pRef = (EntityRef*)pData;
	Entity *pPlayerEnt = entFromEntityRefAnyPartition(*pRef);

	if(pPlayerEnt)
	{
		if(returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
		{
			// Update client
			ClientCmd_ui_ScanInventoryForWaypoints(pPlayerEnt, false);
		}
		else
		{
			// send fail
		}
	}

	SAFE_FREE(pRef);
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.Peaowneduniqueitems, .Psaved.Ppallowedcritterpets, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Itemidmax, .Pplayer.Pugckillcreditlimit, .Psaved.Ppownedcontainers, pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppLiteBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Pplayer.Playertype");
enumTransactionOutcome inv_ent_tr_AddDoorKeyItemForFlashbackMission(ATR_ARGS, NOCONST(Entity)* pEnt, MissionDef *pMissionDef, int iMissionLevel, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	NOCONST(Item)* pItem = CONTAINER_NOCONST(Item, item_FromEnt(pEnt,ITEM_MISSION_DOOR_KEY_DEF, 0, NULL, 0));
	if(ISNULL(pItem)) {
		TRANSACTION_RETURN_LOG_FAILURE("Failed to create item from def: %s", ITEM_MISSION_DOOR_KEY_DEF);
	} else if (!pMissionDef || eaSize(&pMissionDef->eaObjectiveMaps) != 1) {
		TRANSACTION_RETURN_LOG_FAILURE("Failed to create Door Key for Flashback Mission: %s", pMissionDef?pMissionDef->name:NULL);
	} else {
		WorldVariable **eaWorldVars = NULL;
		WorldVariable *pVar = NULL;
		NOCONST(SpecialItemProps)* pProps = (NOCONST(SpecialItemProps)*) item_trh_GetOrCreateSpecialProperties(pItem);
		
		eaCopyStructs(&pMissionDef->eaObjectiveMaps[0]->eaWorldVars, &eaWorldVars, parse_WorldVariable);

		// Add variables for Force Sidekicking
		pVar = StructCreate(parse_WorldVariable);
		pVar->pcName = allocAddString(FORCESIDEKICK_MAPVAR_MIN);
		pVar->eType	= WVAR_INT;
		pVar->iIntVal = iMissionLevel;
		eaPush(&eaWorldVars, pVar);

		pVar = StructCreate(parse_WorldVariable);
		pVar->pcName = allocAddString(FORCESIDEKICK_MAPVAR_MAX);
		pVar->eType	= WVAR_INT;
		pVar->iIntVal = iMissionLevel;
		eaPush(&eaWorldVars, pVar);

		pVar = StructCreate(parse_WorldVariable);
		pVar->pcName = allocAddString(FORCEMISSIONRETURN_MAPVAR);
		pVar->eType	= WVAR_INT;
		pVar->iIntVal = 1;
		eaPush(&eaWorldVars, pVar);

		pVar = StructCreate(parse_WorldVariable);
		pVar->pcName = allocAddString(FLASHBACKMISSION_MAPVAR);
		pVar->eType	= WVAR_STRING;
		pVar->pcStringVal = StructAllocString(pMissionDef->name);
		eaPush(&eaWorldVars, pVar);

		StructDestroyNoConstSafe(parse_ItemDoorKey, &pProps->pDoorKey);
		pProps->pDoorKey = StructCreateNoConst(parse_ItemDoorKey);
		pProps->pDoorKey->pchDoorKey = "Flashback"; // -- hardcoded for now
		pProps->pDoorKey->pchMap = allocAddString(pMissionDef->eaObjectiveMaps[0]->pchMapName);
		pProps->pDoorKey->pchMapVars = StructAllocString(worldVariableArrayToString(eaWorldVars));

		SET_HANDLE_FROM_REFERENT(g_MissionDictionary, pMissionDef, pProps->pDoorKey->hMission);

		eaDestroyStruct(&eaWorldVars, parse_WorldVariable);

		if(inv_AddItem(ATR_PASS_ARGS, pEnt, NULL, 29 /* Literal InvBagIDs_HiddenLocationData */, -1, (Item*)pItem, ITEM_MISSION_DOOR_KEY_DEF, 0, pReason, pExtract) != TRANSACTION_OUTCOME_SUCCESS) {
			StructDestroyNoConst(parse_Item, pItem);
			TRANSACTION_RETURN_LOG_FAILURE("Failed to add door key item [%s<%"FORM_LL"u>] to ent: %s", ITEM_MISSION_DOOR_KEY_DEF, pItem->id, pEnt->debugName);
		}
		StructDestroyNoConst(parse_Item, pItem);
	}
	TRANSACTION_RETURN_LOG_SUCCESS("Successfully added door key item [%s<%"FORM_LL"u>] to ent: %s", ITEM_MISSION_DOOR_KEY_DEF, pItem->id, pEnt->debugName);
}

#endif


AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".pInventoryV2.Peaowneduniqueitems, .pInventoryV2.Ppinventorybags, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Psaved.Conowner.Containertype, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Pchar.Ilevelexp");
enumTransactionOutcome inv_tr_UnlockFromBoundItems(ATR_ARGS, NOCONST(Entity) *pEnt, ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	int NumBags,iBag;
	bool bUnlocked = false;

	if ( ISNULL(pEnt) || ISNULL(pEnt->pPlayer))
	{
		TRANSACTION_RETURN_LOG_FAILURE("No player or entity");
	}

	if ( ISNULL(pEnt->pInventoryV2))
	{
		TRANSACTION_RETURN_LOG_FAILURE("Entity has no inventory");
	}

	NumBags = eaSize(&pEnt->pInventoryV2->ppInventoryBags);
	for(iBag=0;iBag<NumBags;iBag++)
	{
		NOCONST(InventoryBag) *pBag = pEnt->pInventoryV2->ppInventoryBags[iBag];
		int iSlot;
		int NumSlots = eaSize(&pBag->ppIndexedInventorySlots);
		
		for(iSlot=0;iSlot<NumSlots;iSlot++)
		{
			NOCONST(InventorySlot) *pSlot = pBag->ppIndexedInventorySlots[iSlot];

			if ( NONNULL(pSlot->pItem) )
			{
				ItemDef *pItemDef = GET_REF(pSlot->pItem->hItem);

				//Unlock any costumes on items that are bound that are not currently unlocked
				if(	pItemDef && (pSlot->pItem->flags & kItemFlag_Bound)
					&& (pItemDef->eCostumeMode == kCostumeDisplayMode_Unlock || (pSlot->pItem->flags & kItemFlag_Algo))
					&& (eaSize(&pItemDef->ppCostumes) || (pSlot->pItem->pSpecialProps && GET_REF(pSlot->pItem->pSpecialProps->hCostumeRef))))
				{
					bUnlocked |= inv_trh_UnlockCostumeOnItem(ATR_PASS_ARGS, pEnt, true, pSlot->pItem, pExtract);
					if(pItemDef->bDeleteAfterUnlock)
					{
						
						Item *pRemItem = (Item*)inv_bag_trh_RemoveItem(ATR_PASS_ARGS, pEnt, true, pBag, iSlot, pSlot->pItem->count, pReason);
						StructDestroy(parse_Item, pRemItem);
					}
				}
			}
		}
	}

	if(bUnlocked)
	{
		QueueRemoteCommand_RemoteCostumeUnlockCallback(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID);
		TRANSACTION_RETURN_LOG_SUCCESS("Successfully unlocked costumes for EntityPlayer[%d]", pEnt->myContainerID);
	}
	else
	{
		TRANSACTION_RETURN_LOG_FAILURE("Did not unlocked costumes for EntityPlayer[%d].  All costumes already unlocked.", pEnt->myContainerID);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Psaved.Conowner.Containerid, pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], pInventoryV2.ppInventoryBags[], .Psaved.Conowner.Containertype, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Pinventoryv2.Peaowneduniqueitems, pInventoryV2.ppLiteBags[], pInventoryV2.ppLiteBags[]")
ATR_LOCKS(pGuildBank, "pInventoryV2.ppInventoryBags[]");
enumTransactionOutcome inv_bag_tr_ApplySort(ATR_ARGS, NOCONST(Entity)* pEnt, NOCONST(Entity) *pGuildBank, S32 eBagID, NON_CONTAINER S64EarrayWrapper* pSortedItemIDs, S32 bCombineStacks, const ItemChangeReason *pReason, GameAccountDataExtract* pExtract)
{
	ATH_ARG NOCONST(InventoryBag)* pBag;
	int i, j;
	if (ISNULL(pEnt) || ISNULL(pEnt->pPlayer))
	{
		TRANSACTION_RETURN_LOG_FAILURE("No player or entity");
	}

	if (inv_IsGuildBag(eBagID))
	{
		pBag = inv_guildbank_trh_GetBag(ATR_PASS_ARGS, pGuildBank, eBagID);
	}
	else
	{
		pBag = inv_trh_GetBag(ATR_PASS_ARGS, pEnt, eBagID, pExtract);
	}
	if (NONNULL(pBag) && !(invbag_trh_flags(pBag) & (InvBagFlag_EquipBag|InvBagFlag_WeaponBag|InvBagFlag_DeviceBag|InvBagFlag_ActiveWeaponBag)))
	{
		NOCONST(InventorySlot)** ppSlots = NULL;

		// If requested, combine all non-full stacks
		if (bCombineStacks)
		{
			for (i = eaSize(&pBag->ppIndexedInventorySlots)-1; i >= 0; i--)
			{
				NOCONST(InventorySlot)* pSlot = inv_trh_GetSlotFromBag(ATR_PASS_ARGS, pBag, i);

				if (NONNULL(pSlot) && NONNULL(pSlot->pItem))
				{
					NOCONST(Item)* pItem = pSlot->pItem;
					ItemDef* pItemDef = GET_REF(pItem->hItem);
					if (pItemDef)
					{
						if (pItemDef->iStackLimit > 1 && pItem->count < pItemDef->iStackLimit)
						{
							for (j = i-1; j >= 0; j--)
							{
								NOCONST(InventorySlot)* pSlotCombine = pBag->ppIndexedInventorySlots[j];
								if (NONNULL(pSlotCombine->pItem))
								{
									NOCONST(Item)* pItemCombine = pSlotCombine->pItem;
									ItemDef* pItemDefCombine = GET_REF(pItemCombine->hItem);
									if (pItemDefCombine == pItemDef)
									{
										int iFreeSpace = pItemDef->iStackLimit - pItemCombine->count;
										if (iFreeSpace > 0)
										{
											Item* pRemovedItem;
											int iItemCount = pItem->count;
											int iCount = MIN(iItemCount, iFreeSpace);
											pItemCombine->count += iCount;

											if (inv_IsGuildBag(eBagID)) {
												pRemovedItem = CONTAINER_RECONST(Item, inv_bag_trh_RemoveItem(ATR_PASS_ARGS, NULL, true, pBag, i, iCount, pReason));
											} else {
												pRemovedItem = invbag_RemoveItem(ATR_PASS_ARGS, pEnt, true, eBagID, i, iCount, pReason, pExtract);
											}
											StructDestroySafe(parse_Item, &pRemovedItem);

											if (iItemCount == iCount)
											{
												break;
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}

		// Find the slot for each sorted item ID and push it onto the new array
		for (i = 0; i < eaSize(&pSortedItemIDs->eaValues); i++)
		{
			U64 uItemID = (U64)pSortedItemIDs->eaValues[i]->iInt;
			BagIterator* pBagIter = inv_bag_trh_FindItemByID(ATR_PASS_ARGS, pBag, uItemID, NULL);
			if (pBagIter)
			{
				eaPush(&ppSlots, eaRemove(&pBag->ppIndexedInventorySlots, pBagIter->i_cur));
			}
			bagiterator_Destroy(pBagIter);
		}
		// Push all remaining slots on to the end of the new array
		for (i = eaSize(&pBag->ppIndexedInventorySlots)-1; i >= 0; i--)
		{
			eaPush(&ppSlots, eaRemove(&pBag->ppIndexedInventorySlots, i));
		}
		// Generate new slot names for the sorted array and push them back onto the bag
		for (i = 0; i < eaSize(&ppSlots); i++)
		{
			NOCONST(InventorySlot)* pSlot = ppSlots[i];
			inv_InventorySlotSetNameFromIndex(pSlot, i);
			eaPush(&pBag->ppIndexedInventorySlots, pSlot);
		}
		eaDestroy(&ppSlots);
		TRANSACTION_RETURN_LOG_SUCCESS("Successfully sorted bag %s for entity %d", StaticDefineIntRevLookup(InvBagIDsEnum, pBag->BagID), pEnt->myContainerID);
	}
	// TODO: Handle lite bags?
	TRANSACTION_RETURN_LOG_FAILURE("Couldn't sort bag %d for entity %d", eBagID, pEnt->myContainerID);
}

static bool entity_CostumeIsUnlocked(Entity *pEnt, const GameAccountData *pData, PlayerCostume *pCostume, char **estrInput)
{
	char *estrLocal = NULL;
	char *estrToUse = NULL;
	bool bUnlocked = false;
	if(estrInput)
	{
		estrToUse = *estrInput;
	}
	else
	{
		estrStackCreate(&estrLocal);
		estrToUse = estrLocal;
	}

	if(pCostume->bAccountWideUnlock)
	{
		MicroTrans_FormItemEstr(&estrToUse, GetShortProductName(), kMicroItemType_PlayerCostume, pCostume->pcName, 1);

		if(gad_GetCostumeRef(pData, estrToUse))
		{
			bUnlocked = true;
		}
	}
	else
	{
		if(costumeEntity_CostumeUnlockedLocal(pEnt, pCostume))
		{
			bUnlocked = true;
		}
	}

	estrDestroy(&estrLocal);

	return(bUnlocked);
}

bool inv_ent_FixupBoundItems(Entity *pEnt)
{
	bool bRunTransaction = false;
	int NumBags,iBag;
	const GameAccountData *pData = NULL;
	char *estrCostume = NULL;

	if ( !pEnt || !pEnt->pPlayer || !pEnt->pInventoryV2 || !pEnt->pSaved)
		return bRunTransaction;

	estrStackCreate(&estrCostume);

	pData = entity_GetGameAccount(pEnt);
	NumBags = eaSize(&pEnt->pInventoryV2->ppInventoryBags);
	for(iBag=0;iBag<NumBags && !bRunTransaction;iBag++)
	{
		int NumSlots,iSlot;

		InventoryBag *pBag = pEnt->pInventoryV2->ppInventoryBags[iBag];

		NumSlots = eaSize(&pBag->ppIndexedInventorySlots);
		for(iSlot=0;iSlot<NumSlots && !bRunTransaction;iSlot++)
		{
			InventorySlot *pSlot = pBag->ppIndexedInventorySlots[iSlot];

			if ( pSlot->pItem )
			{
				ItemDef *pItemDef = GET_REF(pSlot->pItem->hItem);

				if(	pItemDef && pSlot->pItem->flags & kItemFlag_Bound 
					&& (pItemDef->eCostumeMode == kCostumeDisplayMode_Unlock || (pSlot->pItem->flags & kItemFlag_Algo))
					&& (eaSize(&pItemDef->ppCostumes) || (pSlot->pItem->pSpecialProps && GET_REF(pSlot->pItem->pSpecialProps->hCostumeRef))))
				{
					S32 iCostumeIdx;
					PlayerCostume *pCostume = NULL;

					// pSlot->pItem->pSpecialProps can be NULL if pItemDef->ppCostumes has costumes
					if(pSlot->pItem->pSpecialProps)
					{
						pCostume = GET_REF(pSlot->pItem->pSpecialProps->hCostumeRef);
					}

					if(pItemDef->bDeleteAfterUnlock)
					{
						bRunTransaction = true;
						break;
					}

					// Run the algo costume on the item
					if(pCostume)
					{
						if(!entity_CostumeIsUnlocked(pEnt, pData, pCostume, &estrCostume))
						{
							bRunTransaction = true;
							break;
						}
					}
					
					// Then the costumes on the itemDef
					for(iCostumeIdx=eaSize(&pItemDef->ppCostumes)-1; iCostumeIdx >=0 && !bRunTransaction; iCostumeIdx--)
					{
						pCostume = GET_REF(pItemDef->ppCostumes[iCostumeIdx]->hCostumeRef);
						if(!pCostume)
							continue;

						if(!entity_CostumeIsUnlocked(pEnt, pData, pCostume, &estrCostume))
						{
							bRunTransaction = true;
							break;
						}
					}					
				}
			}
		}
	}

	estrDestroy(&estrCostume);

	if(bRunTransaction)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		ItemChangeReason reason = {0};

		inv_FillItemChangeReason(&reason, pEnt, "Inventory:FixupBoundItem", NULL);

		AutoTrans_inv_tr_UnlockFromBoundItems(LoggedTransactions_MakeEntReturnVal("FixupBoundItems", pEnt), GLOBALTYPE_GAMESERVER,
			entGetType(pEnt), entGetContainerID(pEnt),
			&reason, pExtract);
	}

	return bRunTransaction;
}


// New Item Code Transactions
// ===============================================================================


// helper for invoking an item transaction
// item : does not take ownership of this or free it
bool invtransaction_AddItem(Entity *pEnt, int BagID, int iSlot, Item *item, S32 eFlags, const ItemChangeReason *pReason, TransactionReturnCallback userFunc, void* userData)
{
	ItemDef *pItemDef = GET_REF(item->hItem);
	GameAccountDataExtract *pExtract;
	TransactionReturnVal* returnVal;
	bool bLiteBag;
	U32* eaPets = NULL;

	if(!pItemDef)
		return false;

	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	
	if((pItemDef->flags & kItemDefFlag_Unique) && !(eFlags & ItemAdd_IgnoreUnique))
	{
		Entity_GetPetIDList(pEnt, &eaPets);
	}

	if ( BagID == InvBagIDs_None )
	{
		BagID = GetBestBagForItemDef(pEnt, pItemDef, item->count, true, pExtract);
	}
	bLiteBag = !!inv_GetLiteBag(pEnt, BagID, pExtract);

	if (BagID != InvBagIDs_Inventory && !bLiteBag && !item_ItemMoveDestValid(pEnt,pItemDef,NULL,false,BagID,-1,true,pExtract) )
	{
		if(pItemDef && pItemDef->eType == kItemType_Injury)
		{
			// Injuries are only allowed in their restrict bags
			ErrorDetailsf("Item %s", pItemDef->pchName);
			Errorf("invtransaction_AddItem: Item of type Injury failed to be placed into its designated bag");
			return false;
		}
		if (pItemDef->flags & kItemDefFlag_LockToRestrictBags)
		{
			// If the item is flagged as 'LockToRestrictBag', then it cannot go in the inventory
			ErrorDetailsf("Item %s", pItemDef->pchName);
			Errorf("invtransaction_AddItem: Item flagged as 'LockToRestrictBag' failed to be placed in a restrict bag");
			return false;
		}

		// Default to the inventory bag
		BagID = InvBagIDs_Inventory;
	}


	returnVal = LoggedTransactions_CreateManagedReturnValEnt("AddItem", pEnt, userFunc, userData);

	AutoTrans_inv_AddItem(returnVal, GetAppGlobalType(), 
			entGetType(pEnt), entGetContainerID(pEnt), 
			GLOBALTYPE_ENTITYSAVEDPET, &eaPets, 
			BagID, iSlot, item, pItemDef->pchName, eFlags | ItemAdd_ClearID, pReason, pExtract);

	ea32Destroy(&eaPets);
	return (returnVal ? returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS : false);
}


//transaction to remove an item from a bag
AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".pInventoryV2.Peaowneduniqueitems, pInventoryV2.ppLiteBags[], .Psaved.Conowner.Containertype, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, pInventoryV2.ppInventoryBags[], .Pchar.Ilevelexp");
enumTransactionOutcome inv_ent_tr_RemoveItem(ATR_ARGS, NOCONST(Entity)* pEnt, int BagID, int SlotIdx, U64 uItemID, int Count, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	NOCONST(Item)* pCheckItem = inv_trh_GetItemFromBag(ATR_PASS_ARGS, pEnt, BagID, SlotIdx, pExtract);
	Item* pItem = NULL;
	ItemDef* pItemDef = NULL;
	bool bSuccess = true;

	if (NONNULL(pCheckItem))
		pItemDef = GET_REF(pCheckItem->hItem);

	if (ISNULL(pCheckItem) || pCheckItem->id != uItemID)
		bSuccess = false;

	if (bSuccess)
	{
		pItem = invbag_RemoveItem(ATR_PASS_ARGS, pEnt, false, BagID, SlotIdx, Count, pReason, pExtract);
		bSuccess = !!pItem;
	}
	if (!bSuccess)
	{
		TRANSACTION_RETURN_LOG_FAILURE(
			"Item Remove Failed from Bag:Slot [%s:%i] on Ent [%s]: failed to find item", 
			StaticDefineIntRevLookup(InvBagIDsEnum,BagID), SlotIdx,
			pEnt->debugName );
	}

	TRANSACTION_APPEND_LOG_SUCCESS("Item [%s<%"FORM_LL"u>]x%i successfully removed from bag:slot [%s:%i] on entity [%s] and destroyed", 
		pItemDef ? pItemDef->pchName : pItem->pchDisplayName, pItem->id, Count, StaticDefineIntRevLookup(InvBagIDsEnum, BagID), SlotIdx, pEnt->debugName);

	StructDestroy(parse_Item, pItem);

	return TRANSACTION_OUTCOME_SUCCESS;
}



#include "inventoryTransactions_c_ast.c"
