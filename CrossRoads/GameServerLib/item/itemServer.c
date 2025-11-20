#include "Entity.h"

#include "stdtypes.h"
#include "LocalTransactionManager.h"
#include "objTransactions.h"
#include "itemTransaction.h"
#include "AutoTransDefs.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

#include "Character.h"
#include "earray.h"
#include "Player.h"
#include "powers.h"
#include "PowersMovement.h"
#include "PowerAnimFX.h"
#include "PowerModes.h"
#include "PowerActivation.h"
#include "EntitySavedData.h"

#include "Character_combat.h"

#include "wlInteraction.h"
#include "oldencounter_common.h"
#include "gslOldEncounter.h"
#include "mission_common.h"
#include "gslMission.h"
#include "gslMission_transact.h"
#include "CharacterAttribs.h"
#include "CharacterClass.h"
#include "CombatConfig.h"
#include "gslContact.h"
#include "contact_common.h"
#include "superCritterPet.h"
#include "gslsuperCritterPet.h"

#include "itemCommon.h"
#include "inventoryCommon.h"
#include "interaction_common.h"
#include "storeCommon.h"
#include "rewardCommon.h"
#include "NotifyCommon.h"
#include "EntityLib.h"
#include "entCritter.h"
#include "Expression.h"
#include "color.h"
#include "Guild.h"
#include "Store.h"

#include "reward.h"
#include "AlgoItem.h"
#include "AlgoItemCommon.h"

#include "GameStringFormat.h"
#include "TimedCallback.h"

#include "gslInteractable.h"
#include "wlEncounter.h"
#include "../StaticWorld/group.h"
#include "../StaticWorld/WorldCellEntry.h"
#include "character_target.h"

#include "Itemart.h"

#include "GameAccountDataCommon.h"
#include "GamePermissionsCommon.h"
#include "GameServerLib.h"
#include "gslGameAccountData.h"
#include "LoggedTransactions.h"
#include "gslPartition.h"
#include "gslSavedPet.h"
#include "gslWarp.h"
#include "gslSendToClient.h"
#include "itemServer.h"
#include "SavedPetCommon.h"
#include "SavedPetCommon_h_ast.h"
#include "StringCache.h"
#include "WorldGrid.h"

#include "AutoGen/ChatServer_autogen_remotefuncs.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/GlobalTypes_h_ast.h"

#include "itemServer_h_ast.h"
#include "inventoryTransactions.h"
#include "inventoryCommon.h"

#include "AutoGen/Player_h_ast.h"	// for testing shared bank
#include "AutoGen/entity_h_ast.h"	// for testing shared bank

void ItemMove(Entity *pEnt, bool bSrcGuild, S32 iSrcBagID, S32 iSrcSlot, bool bDstGuild, S32 iDstBagID, S32 iDstSlot, S32 iCount);
void BagClear(Entity* pEnt, char* bagName);

#define INV_CHANGE_ACTIVE_BAG_SLOT_TIMEOUT 5

void ItemMoveFromOverflowCallback(TransactionReturnVal *pReturnVal, ItemMoveCBData *pData)
{
	if (pData)
	{
		if (!pReturnVal || pReturnVal->eOutcome == TRANSACTION_OUTCOME_FAILURE)
		{
			Entity *pEnt = entFromEntityRefAnyPartition(pData->uiEntRef);

			// if failed to move to general inventory, send inventory full notification
			if (pEnt && pData->iDstBagID == InvBagIDs_Inventory)
				ClientCmd_NotifySend(pEnt, kNotifyType_InventoryFull, entTranslateMessageKey(pEnt, INVENTORY_FULL_MSG), NULL, NULL);
			// otherwise, if we failed to move from the overflow to some specific bag, attempt to
			// move to general inventory again
			else if (pEnt)
				ItemMove(pEnt, false, pData->iSrcBagID, pData->iSrcSlot, false, InvBagIDs_Inventory, -1, pData->iCount);
		}

		StructDestroy(parse_ItemMoveCBData, pData);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Inventory) ACMD_PRIVATE;
void ItemMoveNumericToGuild(Entity *pEnt, S32 iSrcBagID, S32 iDstBagID, const char *pcNumeric, S32 iCount)
{
	GameAccountDataExtract *pExtract;
	TransactionReturnVal* pReturn;
	Guild *pGuild = guild_GetGuild(pEnt);
	Entity *pGuildBank = guild_GetGuildBank(pEnt);
	GuildMember *pMember = pEnt && pGuild ? guild_FindMemberInGuild(pEnt, pGuild) : NULL;
	InventoryBagLite *pDstBag = inv_guildbank_GetLiteBag(pGuildBank, iDstBagID);
	ContactDef *pContactDef = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog) ? GET_REF(pEnt->pPlayer->pInteractInfo->pContactDialog->hContactDef) : NULL;
	S32 iEPValue;
	ItemDef *pItemDef;
	char *estrBuffer = NULL;
	ItemChangeReason reason = {0};
	
	pItemDef = RefSystem_ReferentFromString(g_hItemDict, pcNumeric);
	if (!pItemDef || !(pItemDef->flags & kItemDefFlag_Tradeable)) {
		entFormatGameMessageKey(pEnt, &estrBuffer, "InventoryUI.InvalidNumeric", STRFMT_END);
		ClientCmd_NotifySend(pEnt, kNotifyType_ItemMoveFailed, estrBuffer, NULL, NULL);
		estrDestroy(&estrBuffer);
		return;
	}
	
	if (!pMember) {
		entFormatGameMessageKey(pEnt, &estrBuffer, "InventoryUI.NotInGuild", STRFMT_END);
		ClientCmd_NotifySend(pEnt, kNotifyType_ItemMoveFailed, estrBuffer, NULL, NULL);
		estrDestroy(&estrBuffer);
		return;
	}

	// Two very basic checks. Don't bother doing anything in either case. (not sure how pDstBag could be NULL) WOLF[15Dec11]
	if (iCount==0 || pDstBag==NULL)
	{
		return;
	}
	
	if (iCount > 0)
	{
		if (pMember->iRank >= eaSize(&pDstBag->pGuildBankInfo->eaPermissions) || !(pDstBag->pGuildBankInfo->eaPermissions[pMember->iRank]->ePerms & GuildPermission_Deposit))
		{
			ClientCmd_NotifySend(pEnt, kNotifyType_ItemMoveFailed, entTranslateMessageKey(pEnt, "InventoryUI.NoGuildBankTabDepositPermission"), NULL, NULL);
			return;
		}
	}
	
	iEPValue = item_GetDefEPValue(entGetPartitionIdx(pEnt), pEnt, pItemDef, pItemDef->iLevel, pItemDef->Quality);
	if (iCount < 0)
	{
		S32 iPermissionMax, iNumericCount;
		bool bUsingPermission;

		if (pMember->iRank >= eaSize(&pDstBag->pGuildBankInfo->eaPermissions) || !(pDstBag->pGuildBankInfo->eaPermissions[pMember->iRank]->ePerms & GuildPermission_Withdraw)) {
			ClientCmd_NotifySend(pEnt, kNotifyType_ItemMoveFailed, entTranslateMessageKey(pEnt, "InventoryUI.NoGuildBankTabWithdrawPermission"), NULL, NULL);
			return;
		}

		if (!inv_guildbank_CanWithdrawFromBankTab(pEnt->myContainerID, pGuild, pGuildBank, pDstBag->BagID, -iCount, iEPValue * -iCount)) {
			ClientCmd_NotifySend(pEnt, kNotifyType_ItemMoveFailed, entTranslateMessageKey(pEnt, "InventoryUI.WithdrawLimitReached"), NULL, NULL);
			return;
		}

		iNumericCount = inv_GetNumericItemValue(pEnt, pItemDef->pchName);
		iPermissionMax = GamePermissions_trh_GetCachedMaxNumericEx(CONTAINER_NOCONST(Entity, pEnt), pcNumeric, false, &bUsingPermission);
		if(bUsingPermission && iNumericCount - iCount > iPermissionMax)
		{
			ClientCmd_NotifySend(pEnt, kNotifyType_ItemMoveFailed, 
				langTranslateMessageKeyDefault(entGetLanguage(pEnt), "InventoryUI.TooMuchForTarget", "InventoryUI.TooMuchForTarget"), NULL, NULL);
			return;
		}

	}
	
	if (iCount > 0 && iCount > inv_GetNumericItemValue(pEnt, pcNumeric)) {
		entFormatGameMessageKey(pEnt, &estrBuffer, "InventoryUI.NotEnoughResources", STRFMT_END);
		ClientCmd_NotifySend(pEnt, kNotifyType_ItemMoveFailed, estrBuffer, NULL, NULL);
		estrDestroy(&estrBuffer);
		return;
	}
	
	if (iCount < 0 && -iCount > inv_guildbank_GetNumericItemValue(pGuildBank, iDstBagID, pcNumeric)) {
		entFormatGameMessageKey(pEnt, &estrBuffer, "InventoryUI.NotEnoughResourcesInGuild", STRFMT_END);
		ClientCmd_NotifySend(pEnt, kNotifyType_ItemMoveFailed, estrBuffer, NULL, NULL);
		estrDestroy(&estrBuffer);
		return;
	}

	// check to see if the resource total will be over that of the itemdef.
	if(iCount > 0  && ((S64)iCount + (S64)inv_guildbank_GetNumericItemValue(pGuildBank, iDstBagID, pcNumeric)) > (S64)pItemDef->MaxNumericValue)
	{
		S32 iMax = pItemDef->MaxNumericValue - inv_guildbank_GetNumericItemValue(pGuildBank, iDstBagID, pcNumeric);
		entFormatGameMessageKey(pEnt, &estrBuffer, "InventoryUI.GuildNumericLimitExceeded", STRFMT_INT("Limit", pItemDef->MaxNumericValue), STRFMT_INT("Maximum", iMax), STRFMT_END);
		ClientCmd_NotifySend(pEnt, kNotifyType_ItemMoveFailed, estrBuffer, NULL, NULL);
		estrDestroy(&estrBuffer);
		return;
	}
	
	if (!(pContactDef && contact_IsGuildBank(pContactDef)) && entGetAccessLevel(pEnt) < ACCESS_DEBUG) {
		entFormatGameMessageKey(pEnt, &estrBuffer, "InventoryUI.NotAtGuildBank", STRFMT_END);
		ClientCmd_NotifySend(pEnt, kNotifyType_ItemMoveFailed, estrBuffer, NULL, NULL);
		estrDestroy(&estrBuffer);
		return;
	}
	
	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	pReturn = LoggedTransactions_CreateManagedReturnValEnt("ItemMoveNumeric", pEnt, NULL, NULL);

	inv_FillItemChangeReason(&reason, pEnt, "Item:MoveNumericToGuild", pGuild->pcName);

	AutoTrans_inv_ent_tr_MoveNumericToGuild(pReturn, GetAppGlobalType(), 
			pEnt->myEntityType, pEnt->myContainerID, 
			GLOBALTYPE_GUILD, pGuild->iContainerID, 
			GLOBALTYPE_ENTITYGUILDBANK, pGuild->iContainerID,
			iSrcBagID, iDstBagID, pcNumeric, iCount, iEPValue, &reason, pExtract);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9);
void Guild_Test_Log(Entity *pEnt, char *pchNumeric, S32 iCount)
{
	S32 i;
	for(i = 0; i < iCount; ++i)
	{
		ItemMoveNumericToGuild(pEnt, InvBagIDs_Numeric, InvBagIDs_Numeric, pchNumeric,i + 1);
	}
}

static void EquipItemArt_CB(TransactionReturnVal *returnVal, ItemEquipCB *cbData)
{
	if(returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		Entity *pEnt = entFromContainerIDAnyPartition(cbData->eEntType,cbData->cidEntID);
		GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		Item* pItem = inv_GetItemFromBag(pEnt, cbData->iDstBagID, cbData->iDstBagSlot, pExtract);
		ItemDef* pDef = SAFE_GET_REF(pItem, hItem);

		if (pItem && pDef && eaSize(&g_ItemConfig.eaBagToFxMaps) > 0)
		{
			ClientCmd_gclPlayChangedCostumePartFX(pEnt, cbData->iDstBagID, cbData->iDstBagSlot);
		}
		
		if(pEnt && pEnt->pChar)
		{
			ANALYSIS_ASSUME(pEnt != NULL);
			if(gConf.bItemArt)
			{
				InventoryBag *pBagDst = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), cbData->iDstBagID, pExtract);
				InventoryBag *pBagSrc = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), cbData->iSrcBagID, pExtract);
				const InvBagDef *pBagDefDst = invbag_def(pBagDst);
				const InvBagDef *pBagDefSrc = invbag_def(pBagSrc);
				if((pBagDefDst && (pBagDefDst->pItemArtActive || pBagDefDst->pItemArtActiveSecondary))
					|| (pBagDefSrc && (pBagDefSrc->pItemArtActive || pBagDefSrc->pItemArtActiveSecondary)))
				{
					entity_UpdateItemArtAnimFX(pEnt);
				}
			}
		}
	}
	if (cbData->pCallback)
		cbData->pCallback(returnVal, cbData->pUserData);
	free(cbData);
}

typedef struct UngemItemCallbackData
{
	EntityRef entRef;
} UngemItemCallbackData;

static void ent_UngemItem_CB(TransactionReturnVal *pReturnVal, UngemItemCallbackData *pData)
{
	if (pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		Entity *pEnt = entFromEntityRefAnyPartition(pData->entRef);
		ClientCmd_scp_InvalidateFakeEntities(pEnt);
		scp_resetSummonedPetInventory(pEnt);
	}

	free(pData);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Inventory) ACMD_PRIVATE;
void ItemUngemItem(Entity *pEnt, S32 iSrcBagID, int iSlotIdx, int iGemIdx, S32 iDestBagID, int iDestIdx, const char *pchCurrency)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	InventoryBag *pSrcBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), iSrcBagID, pExtract);
	InventoryBag *pDestBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), iDestBagID, pExtract);
	Item *pSrcItem = inv_bag_GetItem(pSrcBag,iSlotIdx);
	int iCost;
	TransactionReturnVal *returnVal;
	UngemItemCallbackData *pData;
	ItemChangeReason sReason = {0};

	if (!pEnt || !pSrcBag || !pDestBag) {
		return;
	}

	inv_FillItemChangeReason(&sReason,pEnt,"Item:UnGem",NULL);

	iCost = itemGem_GetUnslotCostFromGemIdx(pchCurrency,pEnt,pSrcItem,iGemIdx);

	if(iCost == -1)
	{
		return;
	}
	if(iCost > 0)
	{
		if(iCost > inv_GetNumericItemValue(pEnt,pchCurrency))
		{
			// Send not enough money text
			return;
		}
	}

	pData = malloc(sizeof(UngemItemCallbackData));
	pData->entRef = entGetRef(pEnt);

	returnVal = LoggedTransactions_CreateManagedReturnValEnt("inv_ent_tr_UnGemItem", pEnt, ent_UngemItem_CB, pData);

	AutoTrans_inv_ent_tr_UnGemItem(returnVal,GLOBALTYPE_GAMESERVER,GLOBALTYPE_ENTITYPLAYER,
		pEnt->myContainerID,iSrcBagID,iSlotIdx,pSrcItem->id,iGemIdx,iDestBagID,iDestIdx,pchCurrency,iCost,&sReason,pExtract);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_SERVERCMD;
void entCmd_testUnGemSystem(Entity *pEnt)
{
	int i;
	InventoryBag *invBag = eaIndexedGetUsingInt(&pEnt->pInventoryV2->ppInventoryBags, InvBagIDs_Inventory);
	GameAccountDataExtract *pextract = entity_GetCachedGameAccountDataExtract(pEnt);

	for(i=0;i<eaSize(&invBag->ppIndexedInventorySlots);i++)
	{
		Item *pItem = invBag->ppIndexedInventorySlots[i]->pItem;
		ItemDef *pDef = pItem ? GET_REF(pItem->hItem) : NULL;

		if(pItem && pItem->pSpecialProps && eaSize(&pItem->pSpecialProps->ppItemGemSlots))
		{
			int iGemSlot;

			for(iGemSlot=0;iGemSlot<eaSize(&pItem->pSpecialProps->ppItemGemSlots);iGemSlot++)
			{
				if(IS_HANDLE_ACTIVE(pItem->pSpecialProps->ppItemGemSlots[iGemSlot]->hSlottedItem))
				{
					ItemUngemItem(pEnt,InvBagIDs_Inventory,i,iGemSlot,InvBagIDs_Inventory,-1,"Resources");
				}
			}
		}
	}
}

static bool ItemMoveInternal(Entity *pEnt, Entity *pSrcEnt, bool bSrcGuild, S32 iSrcBagID, S32 iSrcSlot, Entity *pDstEnt, bool bDstGuild, S32 iDstBagID, S32 iDstSlot, S32 iCount, const char *pcActionName, GameAccountDataExtract *pExtract, TransactionReturnCallback userFunc, void *userData)
{
	Guild *pGuild = guild_GetGuild(pEnt);
	Entity *pGuildBank = guild_GetGuildBank(pEnt);
	InventoryBag *pSrcBag = bSrcGuild ? inv_guildbank_GetBag(pGuildBank, iSrcBagID) : (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pSrcEnt), iSrcBagID, pExtract);
	InventoryBag *pDstBag = bDstGuild ? inv_guildbank_GetBag(pGuildBank, iDstBagID) : (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pDstEnt), iDstBagID, pExtract);
	Item* pSrcItem = inv_bag_GetItem(pSrcBag, iSrcSlot);
	Item* pDstItem;
	ItemDef *pSrcItemDef = pSrcItem ? GET_REF(pSrcItem->hItem) : NULL;
	ItemDef *pDstItemDef;
	S32 iSrcStackCount;
	S32 iDstStackCount;
	TransactionReturnVal* returnVal;
	
	if (!pEnt || !pSrcEnt || !pDstEnt || !pSrcBag || !pDstBag || !pSrcItemDef) {
		return false;
	}

	// The iDstSlot can be less than 0. The move transactions deal with this internally so we should be careful
	//  in trying to validate data when the iDstSlot is not specified.
	// This is also true for the iCount


	////////////////////////
	// Special fix up for right clicking on an equip item. We get a -1 DstSlot passed in.
	// Once the -1 DstSlot gets passed into the transaction, it no longer will consider swapping items as valid.
	// So in this particular case we need to find the right slot ahead of time by calling FindBestEquipSlot with
	// true for allowing swapping. This is lame. The alternative would be to make the transaction code
	// in inventoryCommon.c be aware of the need to be able to swap, but inv_bag_trh_MoveItem is also called
	// from other places some of which may not allow itemswapping (such as overflow filling)
	// Long term the various kinds of move should probably be handled separately from each other rather
	// than all piling together on top of the same transaction functions. WOLF[27Oct11]

	if (iDstSlot < 0 && (invbag_flags(pDstBag) & (InvBagFlag_EquipBag|InvBagFlag_WeaponBag|InvBagFlag_ActiveWeaponBag|InvBagFlag_DeviceBag))) {
		// Find the best slot
		S32 iBestDstSlot;
		S32 bCycleSecondary=false;
		iBestDstSlot = inv_bag_ItemMoveFindBestEquipSlot(pDstEnt, pDstBag, pSrcItemDef, true, &bCycleSecondary);
		// We only do this for non-secondary slots since the transaction requires the -1 on secondary slots to trigger the correct behaviour internally
		if (!bCycleSecondary)
		{
			iDstSlot = iBestDstSlot;
		}
	}

	////////////////////////
	// Special fix up for dropping an item on a bag
	// If the player is dropping an item on an occupied player bag slot, change the destination to instead be inside the bag

	if (invbag_flags(pDstBag) & InvBagFlag_PlayerBagIndex) {
		InventoryBag *pPlayerBag = (InventoryBag*)inv_PlayerBagFromSlotIdx(pEnt, iDstSlot);
		if (invbag_maxslots(pEnt, pPlayerBag) || (pPlayerBag && !GamePermissions_trh_CanAccessBag(CONTAINER_NOCONST(Entity, pEnt), pPlayerBag->BagID, pExtract))) {
			pDstBag = pPlayerBag;
			iDstBagID = pDstBag->BagID;
			iDstSlot = -1;
		}
	}

	////////////////////////
	// Check if the destination matches the source, and do nothing in that case
	if (pSrcEnt == pDstEnt && bSrcGuild == bDstGuild && iSrcBagID == iDstBagID && iSrcSlot == iDstSlot) {
		return false;
	}

	////////////////////////
	// Big verification. Note that all verification should really go inside this function or we
	//  will have difficult-to-maintain parallel code. 

	if (!item_ItemMoveValidAcrossEntsWithCount(pEnt, pSrcEnt, pSrcItemDef, iCount,
											   bSrcGuild, iSrcBagID, iSrcSlot,
											   pDstEnt, bDstGuild, iDstBagID, iDstSlot, pExtract))
	{
		return false;
	}

	////////////////////////
	// See if there's already something in the destination slot
	//  and get the item and itemdef of that item. And get the src count

	pDstItem = inv_bag_GetItem(pDstBag, iDstSlot);
	pDstItemDef = pDstItem ? GET_REF(pDstItem->hItem) : NULL;
	iDstStackCount = inv_bag_GetSlotItemCount(pDstBag, iDstSlot);

	iSrcStackCount = inv_bag_GetSlotItemCount(pSrcBag, iSrcSlot);
	

	////////////////////////
	// Okay, everything is fixed up and validated. Break out into different cases
	//  so we can send the appropriate transaction request.
	
	if (scp_itemIsSCP(pSrcItem) && pSrcItem->flags & kItemFlag_Bound)
	{
		// Super Critter Pets have their own move code to handle different bind to player stuff.
		// disable moving them here to reduce hacker exploits.
		return false;
	}

	if (bSrcGuild || bDstGuild) {
		GuildMember *pMember = pGuild ? guild_FindMemberInGuild(pEnt, pGuild) : NULL;
		if (pGuild && pMember) {
			S32 iSrcMoveCount, iDstMoveCount;
			S32 iSrcEPValue, iDstEPValue;
			int iPartitionIdx = entGetPartitionIdx(pEnt);
			ItemChangeReason reason = {0};

			// mblattel[20Oct11]  Calculate the EP costs.
			//   These are somewhat guesses as to the actual EP values since the transaction code
			//   is free to calculate things differently than we do here in terms of number of items being moved, etc.

			item_GuessAtMoveCounts(iCount, iSrcStackCount, iDstStackCount, pSrcItemDef, pDstItemDef, &iSrcMoveCount, &iDstMoveCount);
			
			iSrcEPValue = item_GetStoreEPValue(iPartitionIdx, pEnt, pSrcItem, NULL) * iSrcMoveCount;
			iDstEPValue = item_GetStoreEPValue(iPartitionIdx, pEnt, pDstItem, NULL) * iDstMoveCount;

			if ((bSrcGuild && pEnt == pDstEnt) || (bDstGuild && pEnt == pSrcEnt) || (bSrcGuild == bDstGuild)) {
				U32* eaPets = NULL;

				if(item_MoveRequiresUniqueCheck(pSrcEnt, bSrcGuild, iSrcBagID, iSrcSlot, pDstEnt, bDstGuild, iDstBagID, iDstSlot, pExtract)) {
					ea32Create(&eaPets);
					Entity_GetPetIDList(pSrcEnt, &eaPets);
				}

				// moving an item if :
				// - src-guild to dst-ent
				// - src-ent to dst-guild
				// - either both src and dst are guilds, or neither are
				inv_FillItemChangeReason(&reason, pSrcEnt, "Item:MoveItemToGuild", pGuild->pcName);
				returnVal = LoggedTransactions_CreateManagedReturnValEnt((pcActionName && *pcActionName) ? pcActionName : "ItemMove", pEnt, userFunc, userData);
				itemtransaction_MoveItemGuild_Wrapper(returnVal, GetAppGlobalType(), 
												   entGetType(pSrcEnt), entGetContainerID(pSrcEnt), 
												   GLOBALTYPE_ENTITYSAVEDPET, &eaPets,
												   GLOBALTYPE_GUILD, pGuild->iContainerID,
												   GLOBALTYPE_ENTITYGUILDBANK, pGuild->iContainerID,
												   bSrcGuild, iSrcBagID, iSrcSlot, SAFE_MEMBER(pSrcItem,id), iSrcEPValue, iCount, bDstGuild, iDstBagID, iDstSlot, SAFE_MEMBER(pDstItem,id), iDstEPValue, &reason, pExtract);

				ea32Destroy(&eaPets);
				return true;
			} else {
				if (bSrcGuild) {
					itemtransaction_MoveItemGuildAcrossEnts(pcActionName, userFunc, userData, pEnt, pDstEnt, pGuild, 
						bSrcGuild, iSrcBagID, iSrcSlot, SAFE_MEMBER(pSrcItem,id), iSrcEPValue, iCount, 
						bDstGuild, iDstBagID, iDstSlot, SAFE_MEMBER(pDstItem,id), iDstEPValue);
					
					return true;
				} else {
					
					itemtransaction_MoveItemGuildAcrossEnts(pcActionName, userFunc, userData, pEnt, pDstEnt, pGuild, 
						bSrcGuild, iSrcBagID, iSrcSlot, SAFE_MEMBER(pSrcItem,id), iSrcEPValue, iCount, 
						bDstGuild, iDstBagID, iDstSlot, SAFE_MEMBER(pDstItem,id), iDstEPValue);
					
					return true;
				}
			}
		}
	} else if (pSrcEnt == pDstEnt) {
		ItemEquipCB *cbData = malloc(sizeof(ItemEquipCB));
		ItemChangeReason reason = {0};

		cbData->cidEntID = pEnt->myContainerID;
		cbData->eEntType = pEnt->myEntityType;
		cbData->iSrcBagID = iSrcBagID;
		cbData->iDstBagID = iDstBagID;
		cbData->iDstBagSlot = iDstSlot;
		cbData->pCallback = userFunc;
		cbData->pUserData = userData;

		inv_FillItemChangeReason(&reason, pSrcEnt, "Item:MoveItem", NULL);
		returnVal = LoggedTransactions_CreateManagedReturnValEnt("ItemMove", pEnt, EquipItemArt_CB, cbData);
		AutoTrans_inv_ent_tr_MoveItem(returnVal, GetAppGlobalType(), 
			entGetType(pSrcEnt), entGetContainerID(pSrcEnt), 
			iSrcBagID, iSrcSlot, SAFE_MEMBER(pSrcItem,id), iDstBagID, iDstSlot, SAFE_MEMBER(pDstItem,id), iCount, &reason, &reason, pExtract);

		return true;
	} else {
		ItemChangeReason reason = {0};

		inv_FillItemChangeReason(&reason, pSrcEnt, "Item:MoveItem", NULL);

			returnVal = LoggedTransactions_CreateManagedReturnValEnt((pcActionName && *pcActionName) ? pcActionName : "ItemMove", pEnt, userFunc, userData);
			itemtransaction_MoveItemAcrossEnts(	returnVal, 
												entGetType(pSrcEnt), entGetContainerID(pSrcEnt), iSrcBagID, iSrcSlot, SAFE_MEMBER(pSrcItem,id),
												entGetType(pDstEnt), entGetContainerID(pDstEnt), iDstBagID, iDstSlot, SAFE_MEMBER(pDstItem,id), iCount, &reason, &reason);
		
		return true;
	}

	return false;
}

// Move the item in slot iSrcSlot of iSrcBagID to iDstSlot of iDstBagID. If
// the destination slot is -1, then place it into first available slot.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Inventory) ACMD_PRIVATE;
void ItemMove(Entity *pEnt, bool bSrcGuild, S32 iSrcBagID, S32 iSrcSlot, bool bDstGuild, S32 iDstBagID, S32 iDstSlot, S32 iCount)
{
	TransactionReturnCallback pCallback = NULL;
	void *pCallbackData = NULL;
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

	if (iSrcBagID == InvBagIDs_Overflow)
	{
		ItemMoveCBData *pData = NULL;

		// strictly ensure overflow bag contents are only moved to player and not guild bag
		if (bSrcGuild || bDstGuild)
		{
			return;
		}

		// Find the best bag if the destination is None
		if (iDstBagID == InvBagIDs_None)
		{
			Item *pSrcItem = (Item*) inv_GetItemFromBag(pEnt, iSrcBagID, iSrcSlot, pExtract);
			ItemDef *pSrcItemDef = pSrcItem ? GET_REF(pSrcItem->hItem) : NULL;

			//If there's no item to move, don't try to move it
			if(!pSrcItem || !pSrcItemDef)
			{
				return;
			}

			// with give true it prevents putting items into the equipped (restricted) bags
			iDstBagID = GetBestBagForItemDef(pEnt, pSrcItemDef, iCount, true, pExtract);
			iDstSlot = -1;
		}

		pData = StructCreate(parse_ItemMoveCBData);

		pData->uiEntRef = pEnt->myRef;
		pData->iSrcBagID = iSrcBagID;
		pData->iSrcSlot = iSrcSlot;
		pData->iDstBagID = iDstBagID;
		pData->iDstSlot = iDstSlot;
		pData->iCount = iCount;
		pCallback = ItemMoveFromOverflowCallback;
		pCallbackData = pData;
	}

	if (!ItemMoveInternal(pEnt, pEnt, bSrcGuild, iSrcBagID, iSrcSlot, pEnt, bDstGuild, iDstBagID, iDstSlot, iCount, "ItemMove", pExtract, pCallback, pCallbackData))
	{
		ItemMoveFromOverflowCallback(NULL, pCallbackData);
	}
}

//MULTIPLE ENTITY VERSION - ItemMove
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Inventory) ACMD_PRIVATE;
void ItemMoveAcrossEnts(Entity *pEnt, bool bSrcGuild, S32 iSrcType, U32 iSrcID, S32 iSrcBagID, S32 iSrcSlot, bool bDstGuild, S32 iDstType, U32 iDstID, S32 iDstBagID, S32 iDstSlot, S32 iCount)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	int iPartitionIdx = entGetPartitionIdx(pEnt);
	Entity* pSrcEnt = entity_GetSubEntity(iPartitionIdx, pEnt, iSrcType, iSrcID);
	Entity* pDstEnt = entity_GetSubEntity(iPartitionIdx, pEnt, iDstType, iDstID);
	ItemMoveInternal(pEnt, pSrcEnt, bSrcGuild, iSrcBagID, iSrcSlot, pDstEnt, bDstGuild, iDstBagID, iDstSlot, iCount, "ItemMoveAcrossEnts", pExtract, NULL, NULL);
}

// This command is used to empty a bag into the players main inventory (there needs to be space)
// Using it on an empty bag will move the bag to the main inventory
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD;
void BagToMainBag(Entity *pEnt, S32 bagIdx)
{
	InvBagIDs bagId = InvBagIDs_PlayerBag1 + bagIdx;
	TransactionReturnCallback pCallback = NULL;
	void *pCallbackData = NULL;

	if(pEnt && pEnt->myEntityType == GLOBALTYPE_ENTITYPLAYER && bagId >= InvBagIDs_PlayerBag1 && bagId <= InvBagIDs_PlayerBag9)
	{
		InventoryBag *pBag;
		S32 numSlots;
		bool bFailed = false;

		// get the bag, extract being NULL is on purpose as it allows access to otherwise inaccessible bags
		pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), bagId, NULL);

		if(pBag)
		{
			S32 i;
			numSlots = eaSize(&pBag->ppIndexedInventorySlots);

			for(i = numSlots -1; i>= 0; --i)
			{
				if(inv_bag_GetItem(pBag , i))
				{
					// extract being NULL is on purpose as it allows access to otherwise inaccessible bags
					if(!ItemMoveInternal(pEnt, pEnt, 0, bagId, i, pEnt, 0, InvBagIDs_Inventory, -1, -1, "BagToMainBag", NULL, pCallback, pCallbackData))
					{
						break;
					}
				}
			}

			if(numSlots == 0)
			{
				// extract being NULL is on purpose as it allows access to otherwise inaccessible bags
				ItemMoveInternal(pEnt, pEnt, 0, InvBagIDs_PlayerBags, bagIdx, pEnt, 0, InvBagIDs_Inventory, -1, -1, "BagToMainBag", NULL, pCallback, pCallbackData);
			}
		}

	}

}

// This function exists to pull all items from all Overflow bags of all pets
// into the player's Overflow bag.  It attempts to avoid transactions if possible.
// This is needed to make it so items pushed into Overflow on pets due to validation
// are not lost on the pets, but instead appear on the player.
void ItemCollectPetOverflow(Entity *pPlayerEnt, GameAccountDataExtract *pExtract)
{
	int i;

	if (!pPlayerEnt->pSaved)
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	for(i=eaSize(&pPlayerEnt->pSaved->ppOwnedContainers)-1; i>=0; --i)
	{
		Entity *pPetEnt = GET_REF(pPlayerEnt->pSaved->ppOwnedContainers[i]->hPetRef);
		if (pPetEnt)
		{
			InventoryBag *pInvBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pPetEnt), InvBagIDs_Overflow, pExtract);
			if (pInvBag && !inv_ent_BagEmpty(pPetEnt, InvBagIDs_Overflow, pExtract))
			{
				ItemChangeReason reason = {0};
				inv_FillItemChangeReason(&reason, pPlayerEnt, "Item:MoveOverflowFromPet", pPetEnt->debugName);
				AutoTrans_inv_ent_tr_MoveOverflowItemsAcrossEnts(NULL, GetAppGlobalType(), 
						pPetEnt->myEntityType, pPetEnt->myContainerID, 
						pPlayerEnt->myEntityType, pPlayerEnt->myContainerID,
						&reason, &reason, pExtract);
			}
		}
	}

	PERFINFO_AUTO_STOP();
}

// message for custome is already unlocked
static void CostumeUnlockNothingToUnlockMessage(Entity *pEnt)
{
	char * tmpS = NULL;
	estrStackCreate(&tmpS);
	entFormatGameMessageKey(pEnt, &tmpS, "Item.UI.NoCostumeUnlocked", STRFMT_END);
	notify_NotifySend(pEnt, kNotifyType_CostumeUnlocked, tmpS, NULL, NULL);
	estrDestroy(&tmpS);
}

static void ItemOpenRewardPack_CB(TransactionReturnVal* pReturn, ItemRewardPackCBData* pCBData)
{
	Entity* pEnt = entFromEntityRefAnyPartition(pCBData->erEnt);
	ItemDef* pRewardPackItemDef = GET_REF(pCBData->hRewardPackItem);

	if (pEnt && pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		// Send the reward data to the client if this item is not flagged as EquipOnPickup
		if (!g_RewardConfig.bBlockRewardPackClientSend && pRewardPackItemDef && !(pRewardPackItemDef->flags & kItemDefFlag_EquipOnPickup))
		{
			ItemRewardPackRequestData* pData = StructCreate(parse_ItemRewardPackRequestData);
			COPY_HANDLE(pData->hRewardPackItem, pCBData->hRewardPackItem);
			pData->pRewards = StructCreate(parse_InvRewardRequest);
			pData->ePackResultQuality = inv_FillRewardRequest(pCBData->eaRewardBags, pData->pRewards);
			if (eaSize(&pData->pRewards->eaItemRewards) || eaSize(&pData->pRewards->eaNumericRewards))
			{
				ClientCmd_gclReceiveRewardPackData(pEnt, pData);
			}
			StructDestroy(parse_ItemRewardPackRequestData, pData);
		}
		// Send the unpack message
		if (pRewardPackItemDef && pRewardPackItemDef->pRewardPackInfo)
		{
			const char* pchUnpackMsg = entTranslateDisplayMessage(pEnt, pRewardPackItemDef->pRewardPackInfo->msgUnpackMessage);
			notify_NotifySend(pEnt, kNotifyType_RewardPackOpened, pchUnpackMsg, pRewardPackItemDef->pchName, NULL);
		}
	}
	else if (pEnt) // Failure
	{
		// Send the unpack failure message
		if (pRewardPackItemDef && pRewardPackItemDef->pRewardPackInfo)
		{
			const char* pchFailMsg = entTranslateDisplayMessage(pEnt, pRewardPackItemDef->pRewardPackInfo->msgUnpackFailedMessage);
			notify_NotifySend(pEnt, kNotifyType_RewardPackOpenFailure, pchFailMsg, pRewardPackItemDef->pchName, NULL);
		}
	}
	StructDestroySafe(parse_ItemRewardPackCBData, &pCBData);
}

static bool ItemCanOpenRewardPack(Entity* pEnt, Item* pItem)
{
	ItemDef* pItemDef = GET_REF(pItem->hItem);
	char* estrError = NULL;
	estrStackCreate(&estrError);
	if (!item_CanOpenRewardPack(pEnt, pItemDef, NULL, &estrError))
	{
		notify_NotifySend(pEnt, kNotifyType_RewardPackOpenFailure, estrError, REF_STRING_FROM_HANDLE(pItem->hItem), NULL);
		estrDestroy(&estrError);
		return false;
	}
	estrDestroy(&estrError);
	return true;
}

static void ItemOpenRewardPack(int iPartitionIdx, 
							   Entity* pEnt, 
							   Item* pItem, 
							   ItemDef* pItemDef, 
							   InvBagIDs eBagID, 
							   S32 iSlot)
{
	RewardTable* pRewardTable = pItemDef && pItemDef->pRewardPackInfo ? GET_REF(pItemDef->pRewardPackInfo->hRewardTable) : NULL;

	if (pEnt && pItem && pRewardTable && ItemCanOpenRewardPack(pEnt, pItem)) {
		GiveRewardBagsData GiveRewards = {0};
		GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		ItemRewardPackCBData* pCBData = StructCreate(parse_ItemRewardPackCBData);
		AddSavedPetErrorType eError = kAddSavedPetErrorType_None;
		char* estrError = NULL;
		S32 iLevel = item_GetLevel(pItem) ? item_GetLevel(pItem) : entity_GetSavedExpLevel(pEnt);
		S32 i, j;

		pCBData->erEnt = entGetRef(pEnt);
		SET_HANDLE_FROM_REFERENT("ItemDef", pItemDef, pCBData->hRewardPackItem);
		reward_GenerateBagsForRewardPack(iPartitionIdx, pEnt, pRewardTable, iLevel, &pCBData->eaRewardBags);
		GiveRewards.ppRewardBags = pCBData->eaRewardBags;

		for (i = eaSize(&pCBData->eaRewardBags)-1; i >= 0; i--)
		{
			InventoryBag* pBag = pCBData->eaRewardBags[i];
			for (j = eaSize(&pBag->ppIndexedInventorySlots)-1; j >= 0; j--)
			{
				InventorySlot* pSlot = pBag->ppIndexedInventorySlots[j];
				if (pSlot->pItem)
				{
					ItemDef* pRewardItemDef = GET_REF(pSlot->pItem->hItem);
					PetDef* pPetDef = SAFE_GET_REF(pRewardItemDef, hPetDef);
					if (pPetDef && (pRewardItemDef->flags & kItemDefFlag_EquipOnPickup))
					{
						Entity_CanAddSavedPet(pEnt, pPetDef, 0, pRewardItemDef->bMakeAsPuppet, pExtract, &eError);
					}
					else if (pPetDef && Entity_CheckAcquireLimit(pEnt, pPetDef, 0))
					{
						eError = kAddSavedPetErrorType_AcquireLimit;
					}
					if (eError != kAddSavedPetErrorType_None)
					{
						const char* pchDisplayName = entTranslateDisplayMessage(pEnt, pPetDef->displayNameMsg);
						char pchMsgKey[MAX_PATH];
						estrStackCreate(&estrError);
						sprintf(pchMsgKey, "Item.UI.PetError.%s", StaticDefineIntRevLookup(AddSavedPetErrorTypeEnum, eError));
						entFormatGameMessageKey(pEnt, &estrError, pchMsgKey, STRFMT_STRING("PetName", pchDisplayName), STRFMT_END);
						notify_NotifySend(pEnt, kNotifyType_RewardPackOpenFailure, estrError, pRewardItemDef->pchName, NULL);
						estrDestroy(&estrError);
						break;
					}
				}
			}
			if (j >= 0)
				break;
		}

		if (eaSize(&pCBData->eaRewardBags) && eError == kAddSavedPetErrorType_None)
		{
			TransactionReturnVal* pReturn = LoggedTransactions_CreateManagedReturnValEnt("ItemOpenRewardPack", pEnt, ItemOpenRewardPack_CB, pCBData);
			U32* eaPets = NULL;
			U32 uEntID = entGetContainerID(pEnt);
			ItemChangeReason reason = {0};
			bool bHasUniqueItems = false;

			// Look for unique items
			for(j=eaSize(&GiveRewards.ppRewardBags)-1; j>=0; --j) {
				bHasUniqueItems |= inv_bag_HasAnyUniqueItems(GiveRewards.ppRewardBags[j]);
			}

			// Only lock pets if unique items present
			if (bHasUniqueItems) {
				ea32Create(&eaPets);
				Entity_GetPetIDList(pEnt, &eaPets);
			}

			inv_FillItemChangeReason(&reason, pEnt, "Item:OpeRewardPack", pItemDef ? pItemDef->pchName : NULL);
			
			AutoTrans_item_tr_OpenRewardPack(pReturn, objServerType(), 
											 GLOBALTYPE_ENTITYPLAYER, uEntID, 
											 GLOBALTYPE_ENTITYSAVEDPET, &eaPets,
											 eBagID, iSlot, pItem->id, &GiveRewards, &reason, pExtract);
			ea32Destroy(&eaPets);
		}
		else
		{
			StructDestroy(parse_ItemRewardPackCBData, pCBData);
		}
	}
}

void reward_execute_item(Entity *pEnt, Item *pItem)
{
	ItemDef* pDef = pItem ? GET_REF(pItem->hItem) : NULL;
	if (item_IsRewardPack(pDef))
		ItemOpenRewardPack(pEnt->iPartitionIdx_UseAccessor, pEnt, pItem, pDef, InvBagIDs_Inventory, -1);
	else if (pEnt && pEnt->pChar && pItem)
	{
		// Wait until the next combat tick to execute the powers on the item
		eaPush(&pEnt->pChar->ppAutoExecItems, pItem);
	}
}

static void ItemOpenMicroSpecialMsg(Entity *pEnt, ItemDef *pItemDef, char **eaMsg)
{
	if(pItemDef)
	{

		switch(pItemDef->eSpecialPartType)
		{
		case kSpecialPartType_BankSize:
			{
				entFormatGameMessageKey(pEnt, eaMsg, "OpenedMicroSpecialItemCount",STRFMT_STRING("Item", "Bank size"), STRFMT_INT("Count",pItemDef->uSpecialPartCount),STRFMT_END);
				break;
			}
		case kSpecialPartType_SharedBankSize:
			{
				entFormatGameMessageKey(pEnt, eaMsg, "OpenedMicroSpecialItemCount",STRFMT_STRING("Item", "Shared bank size"), STRFMT_INT("Count",pItemDef->uSpecialPartCount),STRFMT_END);
				break;
			}
		case kSpecialPartType_InventorySize:
			{
				entFormatGameMessageKey(pEnt, eaMsg, "OpenedMicroSpecialItemCount",STRFMT_STRING("Item", "Inventory size"), STRFMT_INT("Count",pItemDef->uSpecialPartCount),STRFMT_END);
				break;
			}

		case kSpecialPartType_CharSlots:
			{
				entFormatGameMessageKey(pEnt, eaMsg, "OpenedMicroSpecialItemCount",STRFMT_STRING("Item", "Character slot"), STRFMT_INT("Count",pItemDef->uSpecialPartCount),STRFMT_END);
				break;
			}
		case kSpecialPartType_CostumeSlots:
			{
				entFormatGameMessageKey(pEnt, eaMsg, "OpenedMicroSpecialItemCount",STRFMT_STRING("Item", "Costume slot"), STRFMT_INT("Count",pItemDef->uSpecialPartCount),STRFMT_END);
				break;
			}
		case kSpecialPartType_Respec:
			{
				entFormatGameMessageKey(pEnt, eaMsg, "OpenedMicroSpecialItemCount",STRFMT_STRING("Item", "Character respec"), STRFMT_INT("Count",pItemDef->uSpecialPartCount),STRFMT_END);
				break;
			}
		case kSpecialPartType_Rename:
			{
				entFormatGameMessageKey(pEnt, eaMsg, "OpenedMicroSpecialItemCount",STRFMT_STRING("Item", "Character rename"), STRFMT_INT("Count",pItemDef->uSpecialPartCount),STRFMT_END);
				break;
			}
		case kSpecialPartType_Retrain:
			{
				entFormatGameMessageKey(pEnt, eaMsg, "OpenedMicroSpecialItemCount",STRFMT_STRING("Item", "Character retrain"), STRFMT_INT("Count",pItemDef->uSpecialPartCount),STRFMT_END);
				break;
			}
		case kSpecialPartType_OfficerSlots:
			{
				entFormatGameMessageKey(pEnt, eaMsg, "OpenedMicroSpecialItemCount",STRFMT_STRING("Item", "Officer slots"), STRFMT_INT("Count",pItemDef->uSpecialPartCount),STRFMT_END);
				break;
			}

		case kSpecialPartType_CostumeChange:
			{
				entFormatGameMessageKey(pEnt, eaMsg, "OpenedMicroSpecialItemCount",STRFMT_STRING("Item", "Costume change"), STRFMT_INT("Count",pItemDef->uSpecialPartCount),STRFMT_END);
				break;
			}
		case kSpecialPartType_ShipCostumeChange:
			{
				entFormatGameMessageKey(pEnt, eaMsg, "OpenedMicroSpecialItemCount",STRFMT_STRING("Item", "Ship costume change"), STRFMT_INT("Count",pItemDef->uSpecialPartCount),STRFMT_END);
				break;
			}
		case kSpecialPartType_ItemAssignmentCompleteNow:
			{
				entFormatGameMessageKey(pEnt, eaMsg, "OpenedMicroSpecialItemCount",STRFMT_STRING("Item", "Item assignment"), STRFMT_INT("Count",pItemDef->uSpecialPartCount),STRFMT_END);
				break;
			}
		case kSpecialPartType_ItemAssignmentUnslotItem:
			{
				entFormatGameMessageKey(pEnt, eaMsg, "OpenedMicroSpecialItemCount",STRFMT_STRING("Item", "Item assignment unslot"), STRFMT_INT("Count",pItemDef->uSpecialPartCount),STRFMT_END);
				break;
			}
		default:
			{
				estrPrintf(eaMsg, "%s", entTranslateMessageKey(pEnt, "OpenedMicroSpecialItem"));
			}
		}

		if(pItemDef->pchPermission)
		{
			entFormatGameMessageKey(pEnt, eaMsg, "OpenedMicroSpecialItemCount",STRFMT_DISPLAYMESSAGE("Item", pItemDef->displayNameMsg), STRFMT_INT("Count",1),STRFMT_END);
		}
	}
	else
	{
		estrPrintf(eaMsg, "%s", entTranslateMessageKey(pEnt, "OpenedMicroSpecialItem"));
	}
}

static void ItemOpenMicroSpecial_CB(TransactionReturnVal* pReturn, ItemOpenMicroSpecialCBData* pCBData)
{
	Entity* pEnt = entFromEntityRefAnyPartition(pCBData->erEnt);
	ItemDef* pItemDef = GET_REF(pCBData->hItemDef);

	if (pEnt && pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		// Send the unpack message
		char* eaUnpackMsg = NULL;
		ItemOpenMicroSpecialMsg(pEnt, pItemDef, &eaUnpackMsg);
		notify_NotifySend(pEnt, kNotifyType_ItemReceived, eaUnpackMsg, pItemDef->pchName, NULL);
		estrDestroy(&eaUnpackMsg);
	}
	else if (pEnt) // Failure
	{
		const char* pchFailMsg = entTranslateMessageKey(pEnt, "UnableToOpenMicroSpecialItem");
		notify_NotifySend(pEnt, kNotifyType_ItemUseFailed, pchFailMsg, pItemDef->pchName, NULL);
	}

	StructDestroySafe(parse_ItemOpenMicroSpecialCBData, &pCBData);
}

static void ItemExperieneGiftFilled_CB(TransactionReturnVal* pReturn, ItemOpenMicroSpecialCBData* pCBData)
{
	Entity* pEnt = entFromEntityRefAnyPartition(pCBData->erEnt);
	ItemDef* pItemDef = GET_REF(pCBData->hItemDef);

	if (pEnt && pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		// Send the unpack message
		const char* pchMsg = entTranslateMessageKey(pEnt, "ItemFilledExpGift");
		notify_NotifySend(pEnt, kNotifyType_ItemReceived, pchMsg, pItemDef->pchName, NULL);
	}
	else if (pEnt) // Failure
	{
		const char* pchFailMsg = entTranslateMessageKey(pEnt, "ItemUnableToFillExpGift");
		notify_NotifySend(pEnt, kNotifyType_ItemUseFailed, pchFailMsg, pItemDef->pchName, NULL);
	}

	StructDestroySafe(parse_ItemOpenMicroSpecialCBData, &pCBData);
}

static void ItemExperieneGiftUsed_CB(TransactionReturnVal* pReturn, ItemOpenMicroSpecialCBData* pCBData)
{
	Entity* pEnt = entFromEntityRefAnyPartition(pCBData->erEnt);
	ItemDef* pItemDef = GET_REF(pCBData->hItemDef);

	if (pEnt && pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		// Send the unpack message
		const char* pchMsg = entTranslateMessageKey(pEnt, "ItemUsedExpGift");
		notify_NotifySend(pEnt, kNotifyType_ItemReceived, pchMsg, pItemDef->pchName, NULL);
	}
	else if (pEnt) // Failure
	{
		const char* pchFailMsg = entTranslateMessageKey(pEnt, "ItemUnableToUseExpGift");
		notify_NotifySend(pEnt, kNotifyType_ItemUseFailed, pchFailMsg, pItemDef->pchName, NULL);
	}

	StructDestroySafe(parse_ItemOpenMicroSpecialCBData, &pCBData);
}

// Check to see if target can receive the experience
bool ItemOpenExperienceGiftGetGiveQuantity(Entity* pEnt, ItemDef* pItemDef)
{
	S32 iLevel = entity_GetSavedExpLevel(pEnt);
	if(iLevel <= g_RewardConfig.GiftData.uMaxGiveLevel)
	{
		return true;
	}

	return false;
}

static bool ItemOpenExperienceGift(int iPartitionIdx, 
	Entity* pEnt, 
	Item* pItem, 
	ItemDef* pItemDef, 
	InvBagIDs eBagID, 
	S32 iSlot,
	const char **pError)
{
	bool bRetVal = false;
	if(pEnt && pItem && pEnt->pPlayer)
	{
		if((pItem->flags & kItemFlag_Full) == 0)
		{
			// item needs to be filled
			if
			(
				!ItemOpenExperienceGiftCanBeFilled(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pEnt), pItemDef)
			)
			{
				if(pError)
				{
					// can't fill set error
					*pError = "Item_Gift_NotEnoughExperience";
				}
			}
			else
			{
				// do fill transaction
				ItemOpenMicroSpecialCBData* pCBData = StructCreate(parse_ItemOpenMicroSpecialCBData);
				U32 uEntID = entGetContainerID(pEnt);
				ItemChangeReason reason = {0};
				TransactionReturnVal* pReturn = LoggedTransactions_CreateManagedReturnValEnt("ItemOpenExperienceGift", pEnt, ItemExperieneGiftFilled_CB, pCBData);
				GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

				pCBData->erEnt = entGetRef(pEnt);
				SET_HANDLE_FROM_REFERENT("ItemDef", pItemDef, pCBData->hItemDef);

				inv_FillItemChangeReason(&reason, pEnt, "Item:FillExperienceGift", NULL);

				AutoTrans_item_tr_ExperienceGiftFill(pReturn, objServerType(), 
					GLOBALTYPE_ENTITYPLAYER, uEntID, 
					eBagID, iSlot, pItem->id, pExtract, &reason);

				bRetVal = true;

			}
		}
		else
		{
			// give exp
			if
			(
				ItemOpenExperienceGiftGetGiveQuantity(pEnt, pItemDef) < 1
			)
			{
				// can't give experience
				if(pError)
				{
					*pError = "Item_Gift_LevelTooLow";
				}
			}
			else
			{
				// give experience
				ItemOpenMicroSpecialCBData* pCBData = StructCreate(parse_ItemOpenMicroSpecialCBData);
				U32 uEntID = entGetContainerID(pEnt);
				ItemChangeReason reason = {0};
				TransactionReturnVal* pReturn = LoggedTransactions_CreateManagedReturnValEnt("ItemOpenExperienceGift", pEnt, ItemExperieneGiftUsed_CB, pCBData);
				GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

				pCBData->erEnt = entGetRef(pEnt);
				SET_HANDLE_FROM_REFERENT("ItemDef", pItemDef, pCBData->hItemDef);

				inv_FillItemChangeReason(&reason, pEnt, "Item:GiveExperienceGift", NULL);

				AutoTrans_item_tr_ExperienceGiftGive(pReturn, objServerType(), 
					GLOBALTYPE_ENTITYPLAYER, uEntID, 
					eBagID, iSlot, pItem->id, pExtract, &reason);

				bRetVal = true;
			}
		}
	}

	return bRetVal;
}

static void ItemOpenMicroSpecial(int iPartitionIdx, 
	Entity* pEnt, 
	Item* pItem, 
	ItemDef* pItemDef, 
	InvBagIDs eBagID, 
	S32 iSlot)
{
	if(pEnt && pItem && pEnt->pPlayer)
	{
		ItemOpenMicroSpecialCBData* pCBData = StructCreate(parse_ItemOpenMicroSpecialCBData);
		GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		U32 uEntID = entGetContainerID(pEnt);
		ItemChangeReason reason = {0};
		TransactionReturnVal* pReturn = LoggedTransactions_CreateManagedReturnValEnt("ItemOpenMicroSpecial", pEnt, ItemOpenMicroSpecial_CB, pCBData);

		pCBData->erEnt = entGetRef(pEnt);
		SET_HANDLE_FROM_REFERENT("ItemDef", pItemDef, pCBData->hItemDef);

		inv_FillItemChangeReason(&reason, pEnt, "Item:OpenMicroSpecial", NULL);

		AutoTrans_item_tr_GrantMicroSpecial(pReturn, objServerType(), 
			GLOBALTYPE_ENTITYPLAYER, uEntID, 
			GLOBALTYPE_GAMEACCOUNTDATA, pEnt->pPlayer->accountID,
			eBagID, iSlot, pItem->id, &reason, pExtract);
	}
}


// "Equip" the item in slot iSrcSlot of bag pchSrcBag into an appropriate
// slot in pchDestBag. "Appropriate" means the first slot for primary
// upgrades, or the first open slot for secondary upgrades. If no secondary
// slots are open, they are all shifted one place, and the item is placed
// into the first slot (and the item in the last slot is placed where the
// source item was).
//
// bAutoUse will automatically use the item (atm this only works for bridge officers - makes them join you)
//
// Currently only upgrades are equippable.
static void ItemEquipInternal(Entity *pEnt, S32 iSrcType, U32 iSrcID, S32 iSrcBagID, S32 iSrcSlot, 
							  S32 iDstType, U32 iDstID, bool bAutoUse, GameAccountDataExtract *pExtract)
{
	int iPartitionIdx = entGetPartitionIdx(pEnt);
	Entity* pSrcEnt = entity_GetSubEntity(iPartitionIdx, pEnt, iSrcType, iSrcID);
	Entity* pDstEnt = entity_GetSubEntity(iPartitionIdx, pEnt, iDstType, iDstID);
	Item* pSrcItem = inv_GetItemFromBag(pSrcEnt, iSrcBagID, iSrcSlot, pExtract);
	ItemDef *pSrcItemDef = pSrcItem ? GET_REF(pSrcItem->hItem) : NULL;
	InvBagIDs iDstBagID;
	int iFirstEmptySlot = -1;
	const char *pchErrorKey = NULL;
	
	if (pSrcEnt && (pSrcEnt->myEntityType == GLOBALTYPE_ENTITYSHAREDBANK ||
		pSrcEnt->myEntityType == GLOBALTYPE_ENTITYGUILDBANK))
	{
		ClientCmd_NotifySend(pEnt, kNotifyType_ItemMoveFailed, "Cannot use an item in a shared or guild bank.", NULL, NULL);
		return;
	}

	if (pSrcItemDef && pDstEnt)
	{
		iDstBagID = GetBestBagForItemDef(pDstEnt, pSrcItemDef, -1, false, pExtract);
		if (iDstBagID == iSrcBagID) {
			iDstBagID = InvBagIDs_Inventory;
		}
		iFirstEmptySlot = inv_ent_GetFirstEmptySlot(pDstEnt, iDstBagID);
	} else {
		return;
	}

	if (item_IsUnidentified(pSrcItem))
	{
		ClientCmd_NotifySend(pEnt, kNotifyType_ItemMoveFailed, "Cannot equip an unidentified item.", NULL, NULL);
		return;
	}


	if (!itemdef_VerifyUsageRestrictions(iPartitionIdx, pDstEnt, pSrcItemDef, item_GetMinLevel(pSrcItem), &pchErrorKey, iFirstEmptySlot)) 
	{ 
		if (pchErrorKey)
		{
			char* pchErrorMsg = NULL;
			entFormatGameMessageKey(pEnt, &pchErrorMsg, pchErrorKey, STRFMT_ITEM(pSrcItem), STRFMT_END);
			ClientCmd_NotifySend(pEnt, kNotifyType_ItemMoveFailed, pchErrorMsg, NULL, NULL);
			estrDestroy(&pchErrorMsg);
		}

		return;
	}
	
	if (item_IsMissionGrant(pSrcItemDef)) {
		// If this is a mission grant item, offer the mission
		MissionDef* pMissionDef = GET_REF(pSrcItemDef->hMission);
		
		if (pMissionDef) {
			contact_OfferMissionFromItem(pEnt, pSrcItemDef, pMissionDef);
		}
	} else if (	(pSrcItemDef->eType == kItemType_STOBridgeOfficer || item_isAlgoPet(pSrcItemDef) )
				&& (pSrcItemDef->flags & kItemDefFlag_EquipOnPickup)==0 && !bAutoUse) {
		//if this is a bridge officer item and it doesn't have the "equip on pickup" flag set, create a power store contact
		contact_CreatePotentialPetFromItem(pEnt, pSrcEnt, iSrcBagID, iSrcSlot, pSrcItemDef, false, false);
	} else if (item_IsSavedPet(pSrcItemDef) || pSrcItemDef->eType == kItemType_STOBridgeOfficer || item_isAlgoPet(pSrcItemDef) ) {
		//if this is a saved pet or it's a bridge officer *with* the "equip on pickup" flag set, give the entity the pet
		PetDef *pSavedPetDef = GET_REF(pSrcItemDef->hPetDef);
		
		if (pSavedPetDef) {
			if(pSrcItemDef->bMakeAsPuppet) {
				gslCreateNewPuppetFromDef(iPartitionIdx,pEnt,pSrcEnt,pSavedPetDef,pSrcItemDef->iLevel,pSrcItem->id,pExtract);
			} else {
				if (pSrcItemDef->flags & kItemDefFlag_DoppelgangerPet)
				{
					Entity* pDoppelgangerSrc = entity_GetTarget(pDstEnt);
					if (pDoppelgangerSrc && pSrcItem->pSpecialProps)
						gslCreateDoppelgangerSavedPetFromDef(iPartitionIdx,pEnt,pSrcEnt,pSrcItem->pSpecialProps->pAlgoPet,pSavedPetDef,pSrcItemDef->iLevel,NULL,NULL,pSrcItem->id,OWNEDSTATE_OFFLINE, NULL, true, pDoppelgangerSrc, pExtract);
					else
						return;
				}
				else
				{
					PropEntIDs propEntIDs = { 0 };
					PropEntIDs_FillWithActiveEntIDs(&propEntIDs, pEnt);
					gslCreateSavedPetFromDef(iPartitionIdx,pEnt,pSrcEnt,pSrcItem->pSpecialProps ? pSrcItem->pSpecialProps->pAlgoPet : NULL,pSavedPetDef,pSrcItemDef->iLevel,NULL,NULL,pSrcItem->id,OWNEDSTATE_ACTIVE,&propEntIDs, true, pExtract);
					PropEntIDs_Destroy(&propEntIDs);
				}
			}
		}
	} else if (item_IsVanityPet(pSrcItemDef) ) {
		gslGAD_UnlockVanityPet(pEnt, pSrcItemDef, pSrcItem->id);
	} else if (item_IsRewardPack(pSrcItemDef)) {
		ItemOpenRewardPack(iPartitionIdx, pSrcEnt, pSrcItem, pSrcItemDef, iSrcBagID, iSrcSlot);
	} else if (item_IsExperienceGift(pSrcItemDef)) {
		if(!ItemOpenExperienceGift(iPartitionIdx, pSrcEnt, pSrcItem, pSrcItemDef, iSrcBagID, iSrcSlot, &pchErrorKey))
		{
			if (pchErrorKey)
			{
				char* pchErrorMsg = NULL;
				entFormatGameMessageKey(pEnt, &pchErrorMsg, pchErrorKey, STRFMT_ITEM(pSrcItem), STRFMT_END);
				ClientCmd_NotifySend(pEnt, kNotifyType_ItemMoveFailed, pchErrorMsg, NULL, NULL);
				estrDestroy(&pchErrorMsg);
			}
		}
	} else if(item_IsMicroSpecial(pSrcItemDef)) {
		ItemOpenMicroSpecial(iPartitionIdx, pSrcEnt, pSrcItem, pSrcItemDef, iSrcBagID, iSrcSlot);
	} else if(item_IsCostumeUnlock(pSrcItemDef) ) {
		if((pSrcItem->flags & kItemFlag_Bound) == 0 && !ItemEntHasUnlockedCostumes(pEnt, pSrcItem))
		{
			ItemChangeReason reason = {0};

			inv_FillItemChangeReason(&reason, pEnt, "Costume:UnlockCostumeThroughItemEquip", NULL);

			AutoTrans_inv_tr_UnlockCostume(LoggedTransactions_MakeEntReturnVal("ItemCostumeUnlock", pEnt), GLOBALTYPE_GAMESERVER, 
					entGetType(pEnt), entGetContainerID(pEnt), 
					false, pSrcItem->id, &reason, pExtract);
		}
		else
		{
			CostumeUnlockNothingToUnlockMessage(pEnt);
		}
	} else if (item_IsAttributeModify(pSrcItemDef)) {
		if(&pSrcItemDef->pAttribModifyValues) {
			
			if(pSrcItemDef->pAttribModifyValues->eSavedPetClassType) {
				PuppetEntity **eaPuppetEntities = NULL;
				int i;

				Entity_GetActivePuppetListByType(pSrcEnt, (CharClassTypes)pSrcItemDef->pAttribModifyValues->eSavedPetClassType, &eaPuppetEntities);
				for (i = eaSize(&eaPuppetEntities)-1; i >= 0; i--) {
					Entity *pEntToChange = SavedPuppet_GetEntity(iPartitionIdx, eaPuppetEntities[i]);
					if(pEntToChange) {
						if(pSrcEnt->pSaved->pPuppetMaster->curID == pEntToChange->myContainerID) {
							entity_ModifySavedAttributes(pSrcEnt,pSrcItemDef,pSrcItemDef->pAttribModifyValues->pTempAttribs,pSrcEnt,pSrcItem->id);
						} else {
							entity_ModifySavedAttributes(pEntToChange,pSrcItemDef,pSrcItemDef->pAttribModifyValues->pTempAttribs,pSrcEnt,pSrcItem->id);
						}
					}
				}
				eaDestroy(&eaPuppetEntities);
			}
			else {
				if(pSrcEnt) {
					entity_ModifySavedAttributes(pSrcEnt,pSrcItemDef,pSrcItemDef->pAttribModifyValues->pTempAttribs,pSrcEnt,pSrcItem->id);
				}
			}
		}
	} else {
		if (item_IsRecipe(pSrcItemDef)) {
			ItemMoveInternal(pEnt, pSrcEnt, inv_IsGuildBag(iSrcBagID), iSrcBagID, iSrcSlot, pDstEnt, inv_IsGuildBag(iDstBagID), iDstBagID, -1, -1, "EquipRecipe", NULL, NULL, pExtract);
		} else if (item_IsInjuryCureGround(pSrcItemDef)) {
			ContactDef* pGroundInjuryContact = RefSystem_ReferentFromString(g_ContactDictionary, "Injury_Cure_Ground_Contact");
			if(pGroundInjuryContact) {
				Entity *pTargetEnt = entity_GetTarget(pEnt);
				contact_InteractBegin(pEnt, NULL, pGroundInjuryContact, NULL, NULL);
				if(pTargetEnt)
					injuryStore_SetTarget(pEnt, entGetType(pTargetEnt), entGetContainerID(pTargetEnt));
			}
		} else if (item_IsInjuryCureSpace(pSrcItemDef)) {
			ContactDef* pSpaceInjuryContact = RefSystem_ReferentFromString(g_ContactDictionary, "Injury_Cure_Space_Contact");
			if(pSpaceInjuryContact) {
				Entity *pTargetEnt = entity_GetTarget(pSrcEnt);
				if(!pTargetEnt)
					pTargetEnt = pSrcEnt;
				contact_InteractBegin(pEnt, NULL, pSpaceInjuryContact, NULL, NULL);
				injuryStore_SetTarget(pEnt, entGetType(pTargetEnt), entGetContainerID(pTargetEnt));
			}
		} else if (item_IsPrimaryEquip(pSrcItemDef)) {

			if (pDstEnt && pDstEnt->pChar)
				character_Wake(pDstEnt->pChar);

			if (iDstBagID == InvBagIDs_Inventory)//if this is going into the inventory, it shouldn't be forced into slot 0.
				ItemMoveInternal(pEnt, pSrcEnt, inv_IsGuildBag(iSrcBagID), iSrcBagID, iSrcSlot, pDstEnt, inv_IsGuildBag(iDstBagID), iDstBagID, -1, -1, "EquipPrimary", NULL, NULL, pExtract);
			else
				ItemMoveInternal(pEnt, pSrcEnt, inv_IsGuildBag(iSrcBagID), iSrcBagID, iSrcSlot, pDstEnt, inv_IsGuildBag(iDstBagID), iDstBagID, 0, -1, "EquipPrimary", NULL, NULL, pExtract);
		} else if (item_IsSecondaryEquip(pSrcItemDef)) {

			if (pDstEnt && pDstEnt->pChar)
				character_Wake(pSrcEnt->pChar);

			ItemMoveInternal(pEnt, pSrcEnt, inv_IsGuildBag(iSrcBagID), iSrcBagID, iSrcSlot, pDstEnt, inv_IsGuildBag(iDstBagID), iDstBagID, -1, -1, "EquipSecondary", NULL, NULL, pExtract);
		} else if (item_IsUpgrade(pSrcItemDef)) {

			if (pDstEnt && pDstEnt->pChar)
				character_Wake(pDstEnt->pChar);

			ItemMoveInternal(pEnt, pSrcEnt, inv_IsGuildBag(iSrcBagID), iSrcBagID, iSrcSlot, pDstEnt, inv_IsGuildBag(iDstBagID), iDstBagID, -1, -1, "EquipUpgrade", NULL, NULL, pExtract);
		} else if (iDstBagID != iSrcBagID) {

			if (pDstEnt && pDstEnt->pChar)
				character_Wake(pDstEnt->pChar);

			ItemMoveInternal(pEnt, pSrcEnt, inv_IsGuildBag(iSrcBagID), iSrcBagID, iSrcSlot, pDstEnt, inv_IsGuildBag(iDstBagID), iDstBagID, -1, -1, "EquipOther", NULL, NULL, pExtract);
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Inventory) ACMD_PRIVATE;
void ItemEquip(Entity *pEnt, S32 iSrcBagID, S32 iSrcSlot)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	GlobalType eType = entGetType(pEnt);
	ContainerID uiID = entGetContainerID(pEnt);
	ItemEquipInternal(pEnt, eType, uiID, iSrcBagID, iSrcSlot, eType, uiID, false, pExtract);
}

//MULTIPLE ENTITY VERSION
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Inventory) ACMD_PRIVATE;
void ItemEquipAcrossEnts(Entity* pEnt, S32 iSrcType, U32 iSrcID, S32 iSrcBagID, S32 iSrcSlot, S32 iDstType, U32 iDstID)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	ItemEquipInternal(pEnt, iSrcType, iSrcID, iSrcBagID, iSrcSlot, iDstType, iDstID, false, pExtract);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(CreateTrainerContactFromItem) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Inventory) ACMD_PRIVATE;
void item_CreateTrainerContactFromItem(Entity* pEnt, S32 iBagID, S32 iBagSlot) 
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	Item* pItem = inv_GetItemFromBag(pEnt, iBagID, iBagSlot, pExtract);
	ItemDef* pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	if (pItemDef && eaSize(&pItemDef->ppTrainableNodes))
	{
		GlobalType eEntType = entGetType(pEnt);
		ContainerID uiEntID = entGetContainerID(pEnt);
		contact_CreatePowerStoreFromItem(pEnt, eEntType, uiEntID, iBagID, iBagSlot, pItem->id, false);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Inventory) ACMD_PRIVATE;
void item_BuyBankTab(Entity *pEnt) {
	ItemDef *pResourcesDef = RefSystem_ReferentFromString(g_hItemDict, "Resources");
	ItemDef *pGuildTabDef = RefSystem_ReferentFromString(g_hItemDict, "Guildbank_Tabs");
	Guild *pGuild = guild_GetGuild(pEnt);
	
	if (pResourcesDef && pGuildTabDef && pGuild) {
		int iPartitionIdx = entGetPartitionIdx(pEnt);
		S32 iResourcesCost = item_GetDefEPValue(iPartitionIdx, pEnt, pResourcesDef, pResourcesDef->iLevel, pResourcesDef->Quality);
		S32 iGuildTabCost = item_GetDefEPValue(iPartitionIdx, pEnt, pGuildTabDef, pGuildTabDef->iLevel, pGuildTabDef->Quality);
		
		if (iResourcesCost > 0) {
			TransactionReturnVal* pReturn = LoggedTransactions_CreateManagedReturnValEnt("BuyBankTab", pEnt, NULL, NULL);
			ItemChangeReason reason = {0};

			inv_FillItemChangeReason(&reason, pEnt, "Guild:BuyBankTab", pGuild->pcName);

			AutoTrans_inv_guild_tr_ExchangeNumerics(pReturn, GetAppGlobalType(), 
					pEnt->myEntityType, pEnt->myContainerID, 
					GLOBALTYPE_GUILD, pGuild->iContainerID, 
					GLOBALTYPE_ENTITYGUILDBANK, pGuild->iContainerID, 
					InvBagIDs_Numeric, "Resources", "GuildBank_Tabs", iGuildTabCost / iResourcesCost, 1, &reason);
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Inventory) ACMD_PRIVATE;
void item_PlayerBuyGuildBankTab(Entity *pEnt) 
{
    ItemDef *pResourcesDef = RefSystem_ReferentFromString(g_hItemDict, guildBankConfig_GetBankTabPurchaseNumericName());
    ItemDef *pGuildTabDef = RefSystem_ReferentFromString(g_hItemDict, guildBankConfig_GetBankTabUnlockNumericName());
    Guild *pGuild = guild_GetGuild(pEnt);
    GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

    if (pResourcesDef && pGuildTabDef && pGuild) 
    {
        int iPartitionIdx = entGetPartitionIdx(pEnt);
        TransactionReturnVal* pReturn = LoggedTransactions_CreateManagedReturnValEnt("PlayerBuyGuildBankTab", pEnt, NULL, NULL);
        ItemChangeReason reason = {0};

        inv_FillItemChangeReason(&reason, pEnt, "Guild:PlayerBuyGuildBankTab", pGuild->pcName);

        AutoTrans_inv_guild_tr_PlayerBuyGuildBankTab(pReturn, GetAppGlobalType(), 
            pEnt->myEntityType, pEnt->myContainerID, 
            GLOBALTYPE_GUILD, pGuild->iContainerID, 
            GLOBALTYPE_ENTITYGUILDBANK, pGuild->iContainerID, 
            InvBagIDs_Numeric, &reason, pExtract);
    }
}

void item_RemoveByID(Entity *pEnt, U64 uItemID, U32 iCount, const ItemChangeReason *pReason)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	TransactionReturnVal* pReturn = LoggedTransactions_CreateManagedReturnValEnt("RemoveByID", pEnt, NULL, NULL);
	AutoTrans_inv_ent_tr_RemoveItemByID(pReturn, GLOBALTYPE_GAMESERVER, 
			entGetType(pEnt), entGetContainerID(pEnt), 
			uItemID, iCount, pReason, pExtract);
}


static void item_ItemRemovalCB(TransactionReturnVal* returnVal, void* userData)
{
	MessageCBData* pCBData = (MessageCBData*)userData;
	Entity *pEnt = entFromEntityRefAnyPartition(pCBData->targetEnt);

	if (pCBData->cbFunc)
	{
		pCBData->cbFunc(returnVal, pCBData->cbData);
	}

	if (pEnt && gConf.bItemArt)
		entity_UpdateItemArtAnimFX(pEnt);

	if ( pEnt &&
		pEnt->pPlayer &&
		pCBData->pMsg)
	{
		notify_NotifySend(pEnt, kNotifyType_ItemLost, pCBData->pMsg, NULL, NULL);

		estrDestroy(&pCBData->pMsg);
		free(pCBData);
	}
}

// Remove All Items from inventory bags
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME("DemoBagFlush") ACMD_SERVERCMD ACMD_CATEGORY(Standard, Inventory);
void item_DemoBagflush(Entity *pEnt)
{
	Entity *pMyPetEnt;
	BagClear(pEnt,(char*)StaticDefineIntRevLookup(InvBagIDsEnum,InvBagIDs_Inventory));

	pMyPetEnt = entity_GetPuppetEntityByType( pEnt, StaticDefineIntGetInt(CharClassTypesEnum, "Ground") == GetCharacterClassEnum( pEnt ) ? "Space" : "Ground", NULL, false, true);
	if (!pMyPetEnt)
		return;
	BagClear(pMyPetEnt,(char*)StaticDefineIntRevLookup(InvBagIDsEnum,InvBagIDs_Inventory));
}

bool item_RemoveFromBagEx(Entity *pEnt, S32 BagID, S32 iSlot, S32 iCount, 
						  S32 iItemPowExpiredBag, S32 iItemPowExpiredSlot, bool bCheckDiscard,
						  const char* msgToUse, const ItemChangeReason *pReason, GameAccountDataExtract *pExtract,
						  TransactionReturnCallback cbFunc, void* cbData)
{
	Item* pItem;
	ItemDef *pItemDef;

	if (inv_IsGuildBag(BagID)) {
		InventoryBag* pBag;
		Guild* pGuild = guild_GetGuild(pEnt);
		Entity *pGuildBank = guild_GetGuildBank(pEnt);
		GuildMember* pMember = pGuild ? guild_FindMemberInGuild(pEnt, pGuild) : NULL;
		pItem = inv_guildbank_GetItemFromBag(pEnt, BagID, iSlot);
		pBag = inv_guildbank_GetBag(pGuildBank, BagID);

		// An item is being removed from the guild, check that this is allowed
		if (!pMember || !pBag || !pBag->pGuildBankInfo || 
			pMember->iRank >= eaSize(&pBag->pGuildBankInfo->eaPermissions) || 
			!(pBag->pGuildBankInfo->eaPermissions[pMember->iRank]->ePerms & GuildPermission_Withdraw))
		{
			ClientCmd_NotifySend(pEnt, kNotifyType_ItemMoveFailed, entTranslateMessageKey(pEnt, "InventoryUI.NoGuildBankTabWithdrawPermission"), NULL, NULL);
			return false;
		}
	} else {
		pItem = inv_GetItemFromBag( pEnt, BagID, iSlot, pExtract);
	}
	pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	
	if (!pItemDef)
		return false;

	if (bCheckDiscard && (pItemDef->flags & kItemDefFlag_CantDiscard))
		return false;

	//if this is a bag remove out of an index bag then verify that the bag is empty
	if (pItemDef->eType == kItemType_Bag)
	{
		InventoryBag *pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), BagID, pExtract);

		if ( invbag_flags(pBag) & InvBagFlag_PlayerBagIndex )
		{
			if ( inv_PlayerBagFail(pEnt, pBag, iSlot) )
				return false;
		}
	}


	{
		MessageCBData* pCBData = calloc(1,sizeof(MessageCBData));
		pCBData->targetEnt = pEnt->myRef;
		pCBData->cbFunc = cbFunc;
		pCBData->cbData = cbData;
		if (!(pItemDef->flags & kItemDefFlag_Silent))
		{
			if (msgToUse && msgToUse[0])
			{
				const char* pchTag = NULL;
				if(pItemDef->eTag)
				{
					pchTag = StaticDefineGetTranslatedMessage(ItemTagEnum, pItemDef->eTag);
				}
				estrCreate(&pCBData->pMsg);
				entFormatGameMessageKey(pEnt, &pCBData->pMsg, msgToUse, STRFMT_STRING("Name", item_GetNameLang(pItem, 0, pEnt)), STRFMT_STRING("Tag", NULL_TO_EMPTY(pchTag)),STRFMT_END);
			}
		}
		itemtransaction_RemoveItemFromBagEx(pEnt, BagID, pItemDef, iSlot, pItem->id, iCount, 
			iItemPowExpiredBag, iItemPowExpiredSlot, pReason,
			item_ItemRemovalCB, pCBData);
	}
	
	return true;
}

// Remove Item from specific bag
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Inventory) ACMD_PRIVATE;
void item_RemoveFromBag(Entity *pEnt, S32 BagID, S32 iSlot, S32 iCount, const char* msgToUse)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	ItemChangeReason reason = {0};

	inv_FillItemChangeReason(&reason, pEnt, "Inventory:ClientRequestedItemRemove", NULL);

	item_RemoveFromBagEx(pEnt, BagID, iSlot, iCount, InvBagIDs_None, -1, true, msgToUse, &reason, pExtract, NULL, NULL);
}


//MULTIPLE ENTITY VERSION - item_RemoveFromBag
// Remove Item from specific bag
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Inventory) ACMD_PRIVATE;
void item_RemoveFromBagForEnt(Entity *pPlayerEnt, S32 iType, U32 iID, S32 BagID, S32 iSlot, S32 iCount)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pPlayerEnt);
	int iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
	Entity* pEnt = entity_GetSubEntity( iPartitionIdx, pPlayerEnt, iType, iID ); //entFromEntityRef( iRef );
	Item* pItem = inv_GetItemFromBag( pEnt, BagID, iSlot, pExtract);
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	ItemChangeReason reason = {0};

	if ( pEnt==NULL )
	{
		return;
	}

	if (!pItemDef)
	{
		return;
	}

	if ((pItemDef->flags & kItemDefFlag_CantDiscard) && entGetAccessLevel(pEnt) < 9)
	{
		return;
	}

	//if this is a bag remove out of an index bag then verify that the bag is empty
	if (pItemDef->eType == kItemType_Bag)
	{
		InventoryBag *pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), BagID, pExtract);

		if ( invbag_flags(pBag) & InvBagFlag_PlayerBagIndex )
		{
			if ( inv_PlayerBagFail(pEnt, pBag, iSlot) )
			{
				return;
			}
		}
	}

	inv_FillItemChangeReason(&reason, pEnt, "Inventory:ClientRequestedItemRemove", NULL);

	itemtransaction_RemoveItemFromBag(pEnt, BagID, pItemDef, iSlot, pItem->id, iCount, &reason, NULL, NULL);
}

AUTO_COMMAND ACMD_CATEGORY(Standard, Inventory);
void Item_Add_FromCritter(Entity *clientEntity, ACMD_NAMELIST("CritterDef", REFDICTIONARY) char *pchName)
{
	if(clientEntity && clientEntity->pChar && pchName)
	{
		CritterDef *pdefCritter = critter_DefGetByName(pchName);

		if(pdefCritter)
		{
			int i;
			ItemChangeReason reason = {0};
			for(i=0;i<eaSize(&pdefCritter->ppCritterItems); i++)
			{
				const char *pcItemName = REF_STRING_FROM_HANDLE(pdefCritter->ppCritterItems[i]->hItem);
				Item *item = item_FromDefName(pcItemName);
				if(pdefCritter->ppCritterItems[i]->bDisabled)
					continue;

				inv_FillItemChangeReason(&reason, clientEntity, "Item:AddFromCritter", pcItemName);

				invtransaction_AddItem(clientEntity, InvBagIDs_None, -1, item, 0, &reason, NULL, NULL);
				StructDestroy(parse_Item,item);
			}
		}
	}
}

// Give an item to yourself. It is put into your inventory
AUTO_COMMAND ACMD_SERVERONLY ACMD_ACCESSLEVEL(5) ACMD_CATEGORY(Standard, Inventory);
void GiveItem(Entity *pEnt, ACMD_NAMELIST("ItemDef", REFDICTIONARY) char *pchItemName)
{
	ItemDef * pDef = item_DefFromName(pchItemName);

	if(pDef)
	{
		Item *item = item_FromDefName(pchItemName);
		ItemChangeReason reason = {0};

		//!!!!  ignore all boosts
		if (pDef->eType == kItemType_Boost || pDef->eType == kItemType_Numeric)
		{
			ClientCmd_NotifySend(pEnt, kNotifyType_Failed, "Boosts and numerics may not given with this command", NULL, NULL);
			return;
		}
		inv_FillItemChangeReason(&reason, pEnt, "Internal:GiveItem", pchItemName);

		invtransaction_AddItem(pEnt, InvBagIDs_None, -1, item, ItemAdd_UseOverflow, &reason, NULL, NULL);
		StructDestroy(parse_Item,item);
	}
}

// Give multiple items to yourself. They are put into your inventory
AUTO_COMMAND ACMD_SERVERONLY ACMD_ACCESSLEVEL(5) ACMD_CATEGORY(Standard, Inventory);
void GiveItemCount(Entity *pEnt, ACMD_NAMELIST("ItemDef", REFDICTIONARY) char *pchItemName, int iCount)
{
	ItemDef *pDef = item_DefFromName(pchItemName);

	if(pDef)
	{
		Item *item = item_FromDefName(pchItemName);
		ItemChangeReason reason = {0};

		item_trh_SetCount(CONTAINER_NOCONST(Item, item), iCount);

		//!!!!  ignore all boosts
		if (pDef->eType == kItemType_Boost || pDef->eType == kItemType_Numeric)
		{
			ClientCmd_NotifySend(pEnt, kNotifyType_Failed, "Boosts and numerics may not given with this command", NULL, NULL);
			return;
		}
		inv_FillItemChangeReason(&reason, pEnt, "Internal:GiveItemCount", pchItemName);

		invtransaction_AddItem(pEnt, InvBagIDs_None, -1, item, ItemAdd_UseOverflow, &reason, NULL, NULL);
		StructDestroy(parse_Item,item);
	}
}

// Give an item to yourself. It is put into your inventory
AUTO_COMMAND ACMD_SERVERONLY ACMD_ACCESSLEVEL(5) ACMD_CATEGORY(Standard, Inventory);
void GiveItemLevelQuality(Entity *pEnt, ACMD_NAMELIST("ItemDef", REFDICTIONARY) char *pchItemName, int iLevel, ACMD_NAMELIST(ItemQualityEnum, STATICDEFINE) char *pchQuality)
{
	ItemDef *pDef = item_DefFromName(pchItemName);
	ItemQuality eQuality = StaticDefineIntGetInt(ItemQualityEnum, pchQuality);
	
	if(pDef && pEnt) {
		NOCONST(Item) *pItem;
		NOCONST(AlgoItemProps)* pProps = NULL;
		ItemChangeReason reason = {0};
		
		//!!!!  ignore all boosts
		if(pDef->eType == kItemType_Boost || pDef->eType == kItemType_Numeric)
		{
			ClientCmd_NotifySend(pEnt, kNotifyType_Failed, "Boosts and numerics may not given with this command", NULL, NULL);
			return;
		}
		
		pItem = CONTAINER_NOCONST(Item, item_FromEnt( CONTAINER_NOCONST(Entity, pEnt),pchItemName, iLevel, NULL, 0));
		pProps = (NOCONST(AlgoItemProps)*)item_trh_GetOrCreateAlgoProperties(pItem);
		pProps->Quality = eQuality;
		inv_FillItemChangeReason(&reason, pEnt, "Internal:GiveItem", pchItemName);

		invtransaction_AddItem(pEnt, InvBagIDs_None, -1, (Item*)pItem, 0, &reason, NULL, NULL);
		StructDestroyNoConst(parse_Item,pItem);
	}
}

typedef struct EquipItemCBData{
	EntityRef entRef;
	ItemDef *pItemDef;
} EquipItemCBData;


static void EquipItemInternal(Entity* pEnt, ItemDef* pItemDef, U64 uItemID, InvBagIDs iSrcBagID, int iSrcSlot, bool bAutoUse, GameAccountDataExtract *pExtract)
{
	if (pEnt && pItemDef) {
		InvBagIDs iDstBagID = GetBestBagForItemDef(pEnt, pItemDef, -1, false, pExtract);
		
		if (uItemID) {
			inv_GetItemAndSlotsByID(pEnt, uItemID, &iSrcBagID, &iSrcSlot);
		} else if (iSrcBagID == InvBagIDs_None) {
			int i, iNumBags = eaSize(&pEnt->pInventoryV2->ppInventoryBags);
			
			for (i = 0; i < iNumBags; i++) {
				InventoryBag* pBag = pEnt->pInventoryV2->ppInventoryBags[i];
				if ((invbag_flags(pBag) &
					(InvBagFlag_SellEnabled | InvBagFlag_EquipBag | InvBagFlag_WeaponBag | InvBagFlag_DeviceBag | InvBagFlag_PlayerBag))) {
					
					inv_GetSlotByItemName(pEnt, pBag->BagID, pItemDef->pchName, &iSrcBagID, &iSrcSlot, pExtract);
					if (iSrcBagID != InvBagIDs_None) {
						break;
					}
				}
			}
		} else if (iSrcSlot < 0) {
			inv_GetSlotByItemName(pEnt, iSrcBagID, pItemDef->pchName, &iSrcBagID, &iSrcSlot, pExtract);
		}
		
		if (iDstBagID != InvBagIDs_None && iSrcSlot >= 0) {
			GlobalType eType = entGetType(pEnt);
			ContainerID uiID = entGetContainerID(pEnt);
			
			ItemEquipInternal(pEnt, eType, uiID, iSrcBagID, iSrcSlot, eType, uiID, bAutoUse, pExtract);
		}
	}	
}

AUTO_COMMAND_REMOTE;
void RecordNewItemID(U64 uItemID, CmdContext* context)
{
	Entity *pEnt = entFromContainerIDAnyPartition(context->clientType, context->clientID);
	NewItemID* pID = StructCreate(parse_NewItemID);
	pID->id = uItemID;
	eaIndexedPushUsingIntIfPossible(&pEnt->pInventoryV2->eaiNewItemIDs, pID->id, pID);
	entity_SetDirtyBit(pEnt, parse_Inventory, pEnt->pInventoryV2, false);
}

// Give an item to yourself. It is put into your inventory
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void ClearNewItemList(Entity *pEnt)
{
	if (pEnt->pInventoryV2)
	{
		eaClearStruct(&pEnt->pInventoryV2->eaiNewItemIDs, parse_NewItemID);
		entity_SetDirtyBit(pEnt, parse_Inventory, pEnt->pInventoryV2, false);

	}
}

void EquipItemCB(TransactionReturnVal *returnVal, EquipItemCBData *pData)
{
	if ( (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS) &&
		(pData) )
	{
		Entity *pEnt = entFromEntityRefAnyPartition(pData->entRef);
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

		EquipItemInternal( pEnt, pData->pItemDef, 0, InvBagIDs_None, -1, true, pExtract );
	}

	if (pData)
		free(pData);
}

AUTO_COMMAND_REMOTE;
void AutoEquipItem(const char* pchItemDefName, U64 uItemID, int iSrcBagID, int iSrcSlot, CmdContext* context)
{
	Entity *pEnt = entFromContainerIDAnyPartition(context->clientType, context->clientID);
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	ItemDef* pItemDef = item_DefFromName(pchItemDefName);
	
	EquipItemInternal(pEnt, pItemDef, uItemID, iSrcBagID, iSrcSlot, false, pExtract);
}

AUTO_COMMAND_REMOTE;
void ItemAddedBroadcastChatMessage(const char* pchItemDefName, const char* pchMsgKey, CmdContext* context)
{
	Entity* pEnt = entFromContainerIDAnyPartition(context->clientType, context->clientID);
	ItemDef* pItemDef = item_DefFromName(pchItemDefName);

	if (pEnt && pItemDef && pchMsgKey && pchMsgKey[0])
	{
		char* estrMsg = NULL;
		estrStackCreate(&estrMsg);
		entFormatGameMessageKey(pEnt, &estrMsg, pchMsgKey,
			STRFMT_ENTITY(pEnt),
			STRFMT_ITEMDEF(pItemDef),
			STRFMT_END);

		//Broadcast the message
		RemoteCommand_userBroadcastGlobalEx(GLOBALTYPE_CHATSERVER, 0, estrMsg, kNotifyType_GameplayAnnounce);
		estrDestroy(&estrMsg);
	}
}

// Give an item to yourself. It is put into your inventory
AUTO_COMMAND ACMD_SERVERONLY ACMD_ACCESSLEVEL(7) ACMD_CATEGORY(Standard, Inventory, csr, debug);
void GiveAndEquipItem(Entity *pEnt, ACMD_NAMELIST("ItemDef", REFDICTIONARY) char *pchItemName)
{
	ItemDef * pDef = item_DefFromName(pchItemName);

	if(pDef)
	{
		//!!!!  ignore all boosts
		if ( pDef->eType == kItemType_Boost)
			return;

		{
			EquipItemCBData *pData = calloc( 1, sizeof(EquipItemCBData));

			if (pData)
			{
				Item *item = item_FromDefName(pchItemName);
				ItemChangeReason reason = {0};

				pData->entRef = pEnt->myRef;
				pData->pItemDef = pDef;
				inv_FillItemChangeReason(&reason, pEnt, "Internal:GiveItem", pchItemName);

				invtransaction_AddItem(pEnt, InvBagIDs_Inventory, -1, item, 0, &reason, EquipItemCB, pData);
				StructDestroy(parse_Item,item);
			}
		}
	}
}



// Remove an Item from yourself. Looks for the first instance of the item and removes it
AUTO_COMMAND ACMD_SERVERONLY ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Inventory) ACMD_PRIVATE;
void RemoveItem(Entity *pEnt, ACMD_NAMELIST("ItemDef", REFDICTIONARY) char *pchItemName)
{
	ItemDef * pItemDef = item_DefFromName(pchItemName);
	if (pItemDef)
	{	
		ItemChangeReason reason = {0};

		//specifying BagID of InvBagIDs_None means all bags
		//specifying a remove slot of -1 removes the item by def name
		//specifying count of -1 removes all instances of this item

		if ((pItemDef->flags & kItemDefFlag_CantDiscard) && entGetAccessLevel(pEnt) < 9)
			return;

		inv_FillItemChangeReason(&reason, pEnt, "Inventory:ClientRequestedItemRemove", NULL);

		itemtransaction_RemoveItemFromBag(pEnt, InvBagIDs_None, pItemDef, -1, 0, -1, &reason, NULL, NULL);
	}
}


// inventoryclear: Removes all items from your inventory, and equip slots.  For debugging
AUTO_COMMAND ACMD_SERVERONLY ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(Standard, Inventory, csr, debug);
void BagClear(Entity* pEnt, char* bagName)
{
	TransactionReturnVal* returnVal = NULL;
	GameAccountDataExtract *pExtract;

	if (!pEnt || !pEnt->pInventoryV2)
		return;

	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	returnVal = LoggedTransactions_CreateManagedReturnValEnt("InventoryClear", pEnt, NULL, NULL);

	AutoTrans_inv_ent_tr_ClearBag(returnVal, GetAppGlobalType(), 
			entGetType(pEnt), entGetContainerID(pEnt), 
			StaticDefineIntGetInt(InvBagIDsEnum, bagName), pExtract );
}


// inventoryclear: Removes all items from your inventory, and equip slots.  For debugging
AUTO_COMMAND ACMD_SERVERONLY ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(Standard, Inventory, csr, debug);
void InventoryClear(Entity* pEnt)
{
	TransactionReturnVal* returnVal = NULL;
	S32 i;
	if (!pEnt || !pEnt->pInventoryV2)
		return;
	for (i = 0; i < eaSize(&pEnt->pInventoryV2->ppInventoryBags); i++)
	{
		InventoryBag *pBag = pEnt->pInventoryV2->ppInventoryBags[i];
		// Clear bag if: 
		// bag is inventory bag OR
		// bag has one of the following flags (basically equipped items and bank):
		//		PlayerBag, StorageOnly, EquipBag, WeaponBag, ActiveWeaponBag, DeviceBag
		// AND bag is NOT the numeric bag.
		if ( ((invbag_flags(pBag) & (InvBagFlag_PlayerBag | InvBagFlag_StorageOnly | InvBagFlag_EquipBag | InvBagFlag_WeaponBag | InvBagFlag_ActiveWeaponBag | InvBagFlag_DeviceBag)) || invbag_bagid(pBag) == InvBagIDs_Inventory)
			&& invbag_bagid(pBag) != InvBagIDs_Numeric)
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
			returnVal = LoggedTransactions_CreateManagedReturnValEnt("InventoryClear", pEnt, NULL, NULL);

			AutoTrans_inv_ent_tr_ClearBag(returnVal, GetAppGlobalType(), 
					entGetType(pEnt), entGetContainerID(pEnt), 
					invbag_bagid(pBag), pExtract );
		}
	}
}

// inventoryclear: Removes all items from your all bags in your inventory.  For debugging
AUTO_COMMAND ACMD_SERVERONLY ACMD_ACCESSLEVEL(7) ACMD_CATEGORY(Standard, Inventory);
void InventoryClearAll(Entity* pEnt)
{
	TransactionReturnVal* returnVal = LoggedTransactions_CreateManagedReturnValEnt("InventoryClearAll", pEnt, NULL, NULL);

	AutoTrans_inv_ent_tr_ClearAllBags(returnVal, GetAppGlobalType(), 
			entGetType(pEnt), entGetContainerID(pEnt) );
}

// inventoryclear: Removes all items from your all bags in your inventory.  For debugging
AUTO_COMMAND_REMOTE;
void ForceRefreshItemArt(CmdContext* context)
{
	Entity *pEnt = entFromContainerIDAnyPartition(context->clientType, context->clientID);
	entity_UpdateItemArtAnimFX(pEnt);
}


// NumericsClear: Removes all items from your numerics bag.  For debugging
AUTO_COMMAND ACMD_SERVERONLY ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Standard, Inventory);
void NumericsClear(Entity* pEnt)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	TransactionReturnVal* returnVal = LoggedTransactions_CreateManagedReturnValEnt("NumericsClear", pEnt, NULL, NULL);

	AutoTrans_inv_ent_tr_ClearBag(returnVal, GetAppGlobalType(), 
			entGetType(pEnt), entGetContainerID(pEnt), 
			InvBagIDs_Numeric, pExtract );
}

AUTO_COMMAND ACMD_SERVERONLY ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(Standard, Inventory);
S32 NumericValue(Entity* pEnt, char* pchNumericItem)
{
	return inv_GetNumericItemValue(pEnt, pchNumericItem);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Inventory) ACMD_PRIVATE;
void InventoryMoveItemsFromBag(Entity *pEnt, S32 iSrcBagID, S32 iDstBagID)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	TransactionReturnVal* pReturnVal = LoggedTransactions_CreateManagedReturnValEnt("InventoryMoveItemsFromBag", pEnt, NULL, NULL);
	ItemChangeReason reason = {0};

	inv_FillItemChangeReason(&reason, NULL, "MassMoveItems", NULL);
	AutoTrans_inv_ent_tr_MoveAllItemsFromBag(	pReturnVal, 
												GetAppGlobalType(), 
												entGetType(pEnt), 
												entGetContainerID(pEnt), 
												iSrcBagID, 
												iDstBagID, 
												&reason,
												pExtract);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Inventory) ACMD_PRIVATE;
void InventoryMoveItemsFromBags(Entity *pEnt, InventoryBagArray *pSrcBagIDs, S32 iDstBagID)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	TransactionReturnVal* pReturnVal = LoggedTransactions_CreateManagedReturnValEnt("InventoryMoveItemsFromBags", pEnt, NULL, NULL);
	ItemChangeReason reason = {0};
	
	inv_FillItemChangeReason(&reason, NULL, "MassMoveItems", NULL);

	AutoTrans_inv_ent_tr_MoveAllItemsFromBags(	pReturnVal, 
												GetAppGlobalType(), 
												entGetType(pEnt), 
												entGetContainerID(pEnt), 
												pSrcBagIDs, 
												iDstBagID,
												&reason,
												pExtract);
}

// --------------------------------------------
// Debug commands for Lore
// --------------------------------------------

AUTO_COMMAND ACMD_SERVERONLY ACMD_ACCESSLEVEL(9);
void LoreGrantAll(Entity* pPlayerEnt)
{
	DictionaryEArrayStruct *pStruct = resDictGetEArrayStruct("ItemDef");

	if (pStruct){
		int i;
		for (i = eaSize(&pStruct->ppReferents)-1; i >= 0; --i){
			ItemDef *pItemDef = pStruct->ppReferents[i];
			if (pItemDef && pItemDef->eType == kItemType_Lore){
				if (inv_ent_AllBagsCountItems(pPlayerEnt, pItemDef->pchName)==0){
					ItemChangeReason reason = {0};
					Item *item = item_FromDefName(pItemDef->pchName);

					inv_FillItemChangeReason(&reason, pPlayerEnt, "Internal:LoreGrantAll", pItemDef->pchName);

					invtransaction_AddItem(pPlayerEnt, InvBagIDs_None, -1, item, ItemAdd_Silent, &reason, NULL, NULL);
					StructDestroy(parse_Item,item);
				}
			}
		}
	}
}

AUTO_COMMAND ACMD_SERVERONLY ACMD_ACCESSLEVEL(9);
void LoreClear(Entity* pEnt)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	TransactionReturnVal* returnVal = LoggedTransactions_CreateManagedReturnValEnt("LoreClear", pEnt, NULL, NULL);

	AutoTrans_inv_ent_tr_ClearBag(returnVal, GetAppGlobalType(), 
			entGetType(pEnt), entGetContainerID(pEnt), 
			InvBagIDs_Lore, pExtract );
}

bool item_AllPowersExpired(Item* pItem, bool bIncludeCharges)
{
	int ii;
	int NumPowers = eaSize(&pItem->ppPowers);
	bool expired = true;

	for(ii=0; ii<NumPowers; ii++)
	{
		Power* pPower = pItem->ppPowers[ii];

		if (!pPower)
			continue;

		if ( !power_IsExpired(pPower, bIncludeCharges) )
			expired = false;
	}

	return expired;
}

bool item_AnyPowersExpired(Item* pItem, bool bIncludeCharges)
{
	int ii;
	int NumPowers = eaSize(&pItem->ppPowers);

	for(ii=0; ii<NumPowers; ii++)
	{
		Power* pPower = pItem->ppPowers[ii];

		if (!pPower)
			continue;

		if ( power_IsExpired(pPower, bIncludeCharges) )
			return true;
	}

	return false;
}
// Craft an item from a recipe with this name.
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Inventory) ACMD_PRIVATE;
void item_CraftCustom(Entity *pEnt, const char * pchRecipeName)
{
	TransactionReturnVal* returnVal;
	GameInteractable *pInteractable = NULL;
	const char* pRewardTableName = NULL;
	WorldInteractionPropertyEntry *pEntry = NULL;
	U32 iServerTime;

	if (!pEnt ||
		!pEnt->pPlayer ||
		!pEnt->pPlayer->InteractStatus.bInteracting ||
		!pEnt->pPlayer->pInteractInfo ||
		!pEnt->pPlayer->pInteractInfo->bCrafting ||
		pEnt->pPlayer->pInteractInfo->eCraftingTable == kSkillType_None || 
		!(pEnt->pPlayer->pInteractInfo->eCraftingTable & pEnt->pPlayer->SkillType))
		return;

	// check to make sure crafting is not happening before allotted interval
	iServerTime = timeSecondsSince2000();
	if (pEnt->pPlayer->iLastCraftTime + ITEM_CRAFT_DELAY_SECS > iServerTime)
		return;
	else
		pEnt->pPlayer->iLastCraftTime = iServerTime;

	// get the reward table from the crafting station
	pInteractable = interactable_GetByNode(GET_REF(pEnt->pPlayer->InteractStatus.interactTarget.hInteractionNode));
	if (pInteractable)
		pEntry = interactable_GetPropertyEntry(pInteractable, pEnt->pPlayer->InteractStatus.interactTarget.iInteractionIndex);
	if (pEntry)
	{
		WorldCraftingInteractionProperties *pCraftingProps = interaction_GetCraftingProperties(pEntry);
		if (pCraftingProps)
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
			RewardContext *pCraftingRewardContext = NULL;
			RewardTable *pRewardTable = GET_REF(pCraftingProps->hCraftRewardTable);
			Item *pRecipe = inv_GetItemFromBagIDByName(pEnt, InvBagIDs_Recipe, pchRecipeName, pExtract);
			ItemDef *pRecipeDef = pRecipe ? GET_REF(pRecipe->hItem) : NULL;

			if (pRecipeDef)
			{
				U32* eaPets = NULL;
				pCraftingRewardContext = Reward_CreateOrResetRewardContext(NULL);
				if (pCraftingRewardContext)
				{
					GiveRewardBagsData *pRewardBagsData = StructCreate(parse_GiveRewardBagsData);
					ItemChangeReason reason = {0};

					// generate crafting table rewards
					reward_CraftingContextInitialize(pEnt, pCraftingRewardContext, 0, pRecipeDef->pRestriction ? pRecipeDef->pRestriction->iSkillLevel : 0);
					reward_generate(entGetPartitionIdx(pEnt), pEnt, pCraftingRewardContext, pRewardTable, &pRewardBagsData->ppRewardBags, NULL, NULL);

					// execute transaction
					returnVal = LoggedTransactions_CreateManagedReturnValEnt("CraftCustomItem", pEnt, NULL, NULL);
					ea32Create(&eaPets);
					Entity_GetPetIDList(pEnt, &eaPets);
					inv_FillItemChangeReason(&reason, pEnt, "Craft:CraftCustomItem", pchRecipeName);

					AutoTrans_tr_InventoryCraftItem(returnVal, GetAppGlobalType(), 
							entGetType(pEnt), entGetContainerID(pEnt), 
							GLOBALTYPE_ENTITYSAVEDPET, &eaPets, 
							pchRecipeName, pRewardBagsData, pCraftingProps->iMaxSkill, &reason, pExtract);

					// cleanup
					StructDestroy(parse_GiveRewardBagsData, pRewardBagsData);
					ea32Destroy(&eaPets);

					// Destroy the reward context
					StructDestroy(parse_RewardContext, pCraftingRewardContext);
				}
			}
		}
	}
}

// Craft an item from a recipe with this name.
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Inventory) ACMD_PRIVATE;
void item_CraftAlgo(Entity *pEnt, CraftData *pCraftData)
{
	TransactionReturnVal* returnVal;
	GameInteractable *pInteractable = NULL;
	const char* pRewardTableName = NULL;
	WorldInteractionPropertyEntry *pEntry = NULL;
	U32 iServerTime;

	//verify that player is interacting with a crafting station
	if (!pEnt ||
		!pEnt->pPlayer ||
		!pEnt->pPlayer->InteractStatus.bInteracting ||
		!pEnt->pPlayer->pInteractInfo ||
		!pEnt->pPlayer->pInteractInfo->bCrafting ||
		pEnt->pPlayer->pInteractInfo->eCraftingTable == kSkillType_None ||
		!(pEnt->pPlayer->pInteractInfo->eCraftingTable & pEnt->pPlayer->SkillType)) 
		return;

	// check to make sure crafting is not happening before allotted interval
	iServerTime = timeSecondsSince2000();
	if (pEnt->pPlayer->iLastCraftTime + ITEM_CRAFT_DELAY_SECS > iServerTime)
		return;
	else
		pEnt->pPlayer->iLastCraftTime = iServerTime;

	// get the reward table from the crafting station
	pInteractable = interactable_GetByNode(GET_REF(pEnt->pPlayer->InteractStatus.interactTarget.hInteractionNode));
	if (pInteractable)
		pEntry = interactable_GetPropertyEntry(pInteractable, pEnt->pPlayer->InteractStatus.interactTarget.iInteractionIndex);
	if (pEntry)
	{
		WorldCraftingInteractionProperties *pCraftingProps = interaction_GetCraftingProperties(pEntry);
		if (pCraftingProps)
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
			RewardContext *pCraftingRewardContext = NULL;
			RewardTable *pRewardTable = GET_REF(pCraftingProps->hCraftRewardTable);
			Item *pRecipeItem = inv_GetItemFromBagIDByName(pEnt, InvBagIDs_Recipe, pCraftData->pcBaseItemRecipeName, pExtract);
			ItemDef *pBaseRecipeDef = pRecipeItem ? GET_REF(pRecipeItem->hItem) : NULL;

			if (pBaseRecipeDef)
			{
				pCraftingRewardContext = Reward_CreateOrResetRewardContext(NULL);
				if (pCraftingRewardContext)
				{
					U32* eaPets = NULL;
					GiveRewardBagsData *pRewardBagsData = StructCreate(parse_GiveRewardBagsData);
					ItemChangeReason reason = {0};

					ea32Create(&eaPets);
					Entity_GetPetIDList(pEnt, &eaPets);

					// generate crafting table rewards
					reward_CraftingContextInitialize(pEnt, pCraftingRewardContext, 0, pBaseRecipeDef->pRestriction ? pBaseRecipeDef->pRestriction->iSkillLevel : 0);
					reward_generate(entGetPartitionIdx(pEnt), pEnt, pCraftingRewardContext, pRewardTable, &pRewardBagsData->ppRewardBags, NULL, NULL);

					// execute transaction
					returnVal = LoggedTransactions_CreateManagedReturnValEnt("CraftAlgoItem", pEnt, NULL, NULL);
					inv_FillItemChangeReason(&reason, pEnt, "Craft:CraftAlgoItem", pBaseRecipeDef->pchName);

					AutoTrans_tr_InventoryCraftAlgoItem(returnVal, GetAppGlobalType(), 
						entGetType(pEnt), entGetContainerID(pEnt),
						GLOBALTYPE_ENTITYSAVEDPET, &eaPets,
						pCraftData, pRewardBagsData, pCraftingProps->iMaxSkill, &reason, pExtract);

					// cleanup
					StructDestroy(parse_GiveRewardBagsData, pRewardBagsData);
					ea32Destroy(&eaPets);

					// Destroy the local context
					StructDestroy(parse_RewardContext, pCraftingRewardContext);
				}
			}
		}
	}
}

extern AlgoTables g_AlgoTables;


AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Inventory) ACMD_PRIVATE;
void item_CraftPreview(Entity *pEnt, CraftData * pCraftData)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	Item *pBaseRecipeItem = NULL;
	ItemDef *pBaseRecipeDef = NULL;
	ItemDef *pBaseItemDef = NULL;
	NOCONST(ItemPowerDefRef) **eaItemPowerDefRefs = NULL;
	NOCONST(Item) *pResultItem = NULL;
	int numPowers;
	int i;

	// ensure player has recipe
	pBaseRecipeItem = inv_GetItemFromBagIDByName(pEnt, InvBagIDs_Recipe, pCraftData->pcBaseItemRecipeName, pExtract);
	if (!pBaseRecipeItem)
	{
		return;
	}

	// make sure recipe has an item result
	pBaseRecipeDef = GET_REF(pBaseRecipeItem->hItem);
	if (!pBaseRecipeDef) 
	{
		return;
	}
	pBaseItemDef = GET_REF(pBaseRecipeDef->pCraft->hItemResult);
	if (!pBaseItemDef) 
	{
		return;
	}

	// accumulate powers to apply to the new item
	numPowers = eaSize(&pCraftData->eaItemPowerRecipes);
	for (i = 0; i < numPowers; i++) 
	{
		Item *pPowerRecipeItem = NULL;
		ItemDef *pPowerRecipeDef;

		if ((pBaseRecipeDef->Group & (1 << i)) == 0)
			continue;

		if (!pCraftData->eaItemPowerRecipes[i] || !pCraftData->eaItemPowerRecipes[i][0])
			continue;

		pPowerRecipeItem = inv_GetItemFromBagIDByName(pEnt, InvBagIDs_Recipe, pCraftData->eaItemPowerRecipes[i], pExtract);
		if (!pPowerRecipeItem) 
			continue;

		pPowerRecipeDef = GET_REF(pPowerRecipeItem->hItem);
		if (!pPowerRecipeDef) 
			continue;

		if (IS_HANDLE_ACTIVE(pPowerRecipeDef->pCraft->hItemPowerResult))
		{
			NOCONST(ItemPowerDefRef) *pPowerDefRef = StructCreateNoConst(parse_ItemPowerDefRef);
			if (pPowerDefRef)
			{
				COPY_HANDLE(pPowerDefRef->hItemPowerDef, pPowerRecipeDef->pCraft->hItemPowerResult);
				pPowerDefRef->iPowerGroup = i;
				eaPush(&eaItemPowerDefRefs, pPowerDefRef);
			}
		}
	}

	// make the result item
	if (numPowers > 0)
		pResultItem = item_CreateAlgoItem(ATR_EMPTY_ARGS, pBaseRecipeDef, pCraftData->eQuality, (ItemPowerDefRef**) eaItemPowerDefRefs);
	else
		pResultItem = CONTAINER_NOCONST(Item, item_FromEnt(CONTAINER_NOCONST(Entity, pEnt),pBaseItemDef->pchName,0,NULL,0));

	// send the item to the client if it was created
	if (pResultItem)
	{
		inv_FixupItemNoConst(pResultItem);
		ClientCmd_CraftingUpdatePreview(pEnt, (Item*) pResultItem);
	}

	eaDestroyStructNoConst(&eaItemPowerDefRefs, parse_ItemPowerDefRef);
	StructDestroyNoConst(parse_Item, pResultItem);
}

// This command drills into the currently interacted crafting station's crafting reward table until
// it finds a skill-ranged table.  The command gets the starting skill level values of all of the table's ranged entries
// and sends those values to the client for previewing.
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Inventory) ACMD_PRIVATE;
void CraftingGetSkillupRanges(Entity *pEnt)
{
	CraftSkillupRanges *pRanges = NULL;
	GameInteractable *pInteractable;
	WorldInteractionPropertyEntry *pEntry = NULL;
	RewardTable *pRewardTable = NULL, *pSkillTable = NULL;
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

	// get the crafting reward table set on the crafting station
	pInteractable = interactable_GetByNode(GET_REF(pEnt->pPlayer->InteractStatus.interactTarget.hInteractionNode));
	if (pInteractable)
		pEntry = interactable_GetPropertyEntry(pInteractable, pEnt->pPlayer->InteractStatus.interactTarget.iInteractionIndex);
	if (pEntry)
	{
		WorldCraftingInteractionProperties *pCraftingProps = interaction_GetCraftingProperties(pEntry);
		if (pCraftingProps)
		{
			pRewardTable = GET_REF(pCraftingProps->hCraftRewardTable);
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
	while (pRewardTable && reward_RangeTable(pRewardTable) && !pSkillTable)
	{
		switch (reward_GetRangeTableType(pRewardTable))
		{
			xcase kRewardChoiceType_LevelRange:
				pRewardTable = reward_GetRangeTable(pRewardTable, pLocalContext->RewardLevel);
			xcase kRewardChoiceType_SkillRange:
				pSkillTable = pRewardTable;
			xdefault:
				pRewardTable = NULL;
		}
	}


	// Destroy the local context
	StructDestroy(parse_RewardContext, pLocalContext);

	if (!pSkillTable)
		return;

	// grab the min value from all the range entries to get a list of range values that can be used
	// to determine the potential skillups; this assumes that the range entries are contiguous
	pRanges = StructCreate(parse_CraftSkillupRanges);
	for (i = 0; i < eaSize(&pSkillTable->ppRewardEntry); i++)
		eaiPush(&pRanges->eaiRanges, pSkillTable->ppRewardEntry[i]->MinLevel);

	// send the ranges to the client (will be sorted on the client just in case)
	ClientCmd_CraftingUpdateSkillupRanges(pEnt, pRanges);

	// cleanup
	StructDestroy(parse_CraftSkillupRanges, pRanges);
}

/*
// Infuse commands
AUTO_COMMAND;
void ItemInfuse(Entity *pEnt, int iBagID, int iBagSlotIdx, int iInfuseSlot, const char *pchItemOption)
{
	if(pEnt)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		TransactionReturnVal* pReturnVal = LoggedTransactions_CreateManagedReturnValEnt("ItemInfuse", pEnt, NULL, NULL);
		ItemChangeReason reason = {0};

		inv_FillItemChangeReason(&reason, pEnt, "Craft:InfuseItem", NULL);

		AutoTrans_tr_ItemInfuse(pReturnVal, GetAppGlobalType(), 
				entGetType(pEnt), entGetContainerID(pEnt), 
				iBagID, iBagSlotIdx, iInfuseSlot, pchItemOption, &reason, pExtract);
	}
}
*/


AUTO_COMMAND ACMD_SERVERONLY ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Standard, Inventory);
void InventoryPrintItemsWithPowers(Entity *e)
{
    int i,j,k;
	Inventory *inv = SAFE_MEMBER(e,pInventoryV2);
    char *msg = 0;

    if(!inv)
        return;

    estrStackCreate(&msg);
    estrPrintf(&msg,"Inventory Items With Powers:\n--------------------\n");
    for(i = eaSize(&inv->ppInventoryBags)-1; i>=0; --i)
    {
        InventoryBag *b = inv->ppInventoryBags[i];
        if(!b)
            continue;
        for(j = eaSize(&b->ppIndexedInventorySlots)-1; j>=0; --j)
        {
            InventorySlot *s = b->ppIndexedInventorySlots[j];
            if(!s)
                continue;
            if(s->pItem && s->pItem->pAlgoProps && eaSize(&s->pItem->pAlgoProps->ppItemPowerDefRefs))
            {
                ItemDef *def = GET_REF(s->pItem->hItem);
                char *delim = "";
                estrConcatf(&msg,"%s: ", SAFE_MEMBER(def,pchName));
                for(k = eaSize(&s->pItem->pAlgoProps->ppItemPowerDefRefs)-1; k>=0; --k)
                {
                    ItemPowerDefRef const *pdr = s->pItem->pAlgoProps->ppItemPowerDefRefs[k];
                    ItemPowerDef const *pd = SAFE_GET_REF(pdr,hItemPowerDef);
                    if(!pd)
                        continue;
                    estrConcatf(&msg,"%s%s",delim,pd->pchName);
                    delim = ", ";
                }
                estrConcatf(&msg,"\n");
                
            }
        }
    }
    gslSendPrintf(e, "%s", msg);
    estrDestroy(&msg);
}

NOCONST(Item) *item_CreateAlgoItem(ATR_ARGS, ItemDef *pBaseRecipeDef, ItemQuality eQuality, ItemPowerDefRef **eaItemPowerDefRefs)
{
	NOCONST(Item) *pFinalItem = NULL;
	ItemDef *pResultDef = NULL;
	AlgoItemLevelsDef *pAlgoItemLevels = NULL;
	int iMaxLevel;

	// get the recipe's result def
	pResultDef = pBaseRecipeDef && pBaseRecipeDef->pCraft ? GET_REF(pBaseRecipeDef->pCraft->hItemResult) : NULL;
	if (!pResultDef)
	{
		TRANSACTION_APPEND_LOG_FAILURE("Could not access Recipe product def for %s", pBaseRecipeDef ? pBaseRecipeDef->pchName : NULL);
		return NULL;
	}

	// make sure the item level is non-zero
	if (pResultDef && pResultDef->iLevel <= 0)
	{
		TRANSACTION_APPEND_LOG_FAILURE("Algo base item def %s must have non-zero level", pResultDef->pchName);
		return NULL;
	}

	// create the algo item
	pFinalItem = CONTAINER_NOCONST(Item, item_FromDefName(pResultDef->pchName));
	
	if (!pFinalItem)
	{
		TRANSACTION_APPEND_LOG_FAILURE("Could not create product item from def %s", pResultDef->pchName);
		return NULL;
	}

	pFinalItem->flags |= kItemFlag_Algo;
	item_trh_SetAlgoPropsQuality(pFinalItem, eQuality);
	item_trh_SetAlgoPropsMinLevel(pFinalItem, pResultDef->iLevel);

	pAlgoItemLevels = eaIndexedGetUsingString(&g_CommonAlgoTables.ppAlgoItemLevels, StaticDefineIntRevLookup(ItemQualityEnum, eQuality));
	iMaxLevel = MIN(g_AlgoTables.MaxLevel, (int) ARRAY_SIZE(pAlgoItemLevels->level));
	item_trh_SetAlgoPropsMinLevel(pFinalItem, CLAMP(item_trh_GetMinLevel(pFinalItem), 1, iMaxLevel));

	if (pAlgoItemLevels)
		item_trh_SetAlgoPropsLevel(pFinalItem, pAlgoItemLevels->level[item_trh_GetMinLevel(pFinalItem) - 1]);
	else
		item_trh_SetAlgoPropsLevel(pFinalItem, pResultDef->iLevel);

	eaCopyStructs(&eaItemPowerDefRefs, (ItemPowerDefRef ***)&pFinalItem->pAlgoProps->ppItemPowerDefRefs, parse_ItemPowerDefRef);

	//Need to add the algo powers to the item before returning it
	item_trh_FixupPowers(pFinalItem);

	return pFinalItem;
}

static void IdentifyItem_CB(TransactionReturnVal *returnVal, IdentifiedItemCBData *cbData)
{
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		Entity* pEnt = entFromEntityRefAnyPartition(cbData->uiEntRef);
		Item* pItem = inv_GetItemByID(pEnt, cbData->ID);
		char* pchName = NULL;
		if (pItem)
		{
			estrStackCreate(&pchName);
			estrPrintf(&pchName, "%s identified!", item_GetName(pItem, pEnt));
			ClientCmd_NotifySendWithItemID(pEnt, kNotifyType_ItemReceived, pchName, item_GetName(pItem, pEnt), item_GetIconName(pItem, NULL), cbData->ID, 1);
			estrDestroy(&pchName);
		}
	}

	if (cbData)
		free(cbData);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Inventory) ACMD_PRIVATE;
void itemIdentify(Entity* pEnt, S32 bagID, S32 slotID, const char* pchScrollDefName)
{
	TransactionReturnVal* returnVal = NULL;
	GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	ItemChangeReason reason = {0};
	Item* pItem = inv_GetItemFromBag(pEnt, bagID, slotID, pExtract);
	ItemDef* pItemDef = NONNULL(pItem) ? GET_REF(pItem->hItem) : NULL;

	if (!pItemDef || !pItem) return;

	if (item_IsUnidentified(pItem))
	{
		IdentifiedItemCBData* pData = calloc(1, sizeof(IdentifiedItemCBData));
		pData->uiEntRef = pEnt->myRef;
		pData->ID = pItem->id;

		inv_FillItemChangeReason(&reason, pEnt, "Inventory:IdentifyItem", (pItemDef ? pItemDef->pchName : "Unknown") );

		returnVal = LoggedTransactions_CreateManagedReturnValEnt("IdentifyItem", pEnt, IdentifyItem_CB, pData);

		AutoTrans_inv_tr_IdentifyItemInBag(returnVal, GetAppGlobalType(), 
				entGetType(pEnt), entGetContainerID(pEnt), 
				bagID, slotID, pchScrollDefName, &reason, pExtract);
	}
}

AUTO_COMMAND ACMD_NAME(BagSort) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Inventory) ACMD_SERVERCMD ACMD_HIDE;
void gslBagSort(Entity* pEnt, S32 eBagID, S32 bCombineStacks)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	S64EarrayWrapper* pSortedItemIDs;
	InventoryBag* pBag;
	Guild* pGuild = NULL;

	if (inv_IsGuildBag(eBagID))
	{
		Entity* pGuildBank = guild_GetGuildBank(pEnt);
		GuildMember* pMember;

		pGuild = guild_GetGuild(pEnt);
		pBag = inv_guildbank_GetBag(pGuildBank, eBagID);
		pMember = guild_FindMemberInGuild(pEnt, pGuild);

		if (!pBag || !pMember || pMember->iRank >= eaSize(&pBag->pGuildBankInfo->eaPermissions) || 
			!(pBag->pGuildBankInfo->eaPermissions[pMember->iRank]->ePerms & GuildPermission_Deposit)) 
		{
			return;
		}
	}
	else
	{
		pBag = CONTAINER_RECONST(InventoryBag, inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), eBagID, pExtract));
	}
	if (pSortedItemIDs = inv_bag_CreateSortData(pEnt, pBag))
	{
		ItemChangeReason reason = {0};
		TransactionReturnVal* pReturn;
		pReturn = LoggedTransactions_CreateManagedReturnValEnt("BagSort", pEnt, NULL, NULL);
		inv_FillItemChangeReason(&reason, pEnt, "Inventory:SortBag", NULL);

		AutoTrans_inv_bag_tr_ApplySort(pReturn, GetAppGlobalType(), entGetType(pEnt), entGetContainerID(pEnt), GLOBALTYPE_ENTITYGUILDBANK, SAFE_MEMBER(pGuild, iContainerID), eBagID, pSortedItemIDs, bCombineStacks, &reason, pExtract);
		StructDestroy(parse_S64EarrayWrapper, pSortedItemIDs);
	}
}

static bool gslBagChangeActiveSlot(Entity* pEnt, S32 eBagID, S32 iActiveIndex, S32 iNewActiveSlot)
{
	if (pEnt)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		InventoryBag* pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), eBagID, pExtract);
		const InvBagDef* pDef = invbag_def(pBag);

		if (pDef && invbag_CanChangeActiveSlot(pEnt, pBag, iActiveIndex, iNewActiveSlot))
		{
			S32 i, iOldActiveSlot = ea32Get(&pBag->eaiActiveSlots, iActiveIndex);
			InventorySlot* pOldSlot = inv_GetSlotFromBag(pBag, iOldActiveSlot);

			// Check if this active slot is already active in another index
			// if it is, remove it from that slot
			if (iNewActiveSlot != -1)
			{
				for (i = eaiSize(&pBag->eaiActiveSlots) - 1; i >= 0; --i)
				{
					if (i != iActiveIndex && pBag->eaiActiveSlots[i] == iNewActiveSlot)
					{
						pBag->eaiActiveSlots[i] = -1;
						break;
					}
				}
			}
		
			// Fixup the bag if it doesn't have all the active slots that it should
			if (eaiSize(&pBag->eaiActiveSlots) != pDef->maxActiveSlots)
			{
				S32 oldSize = eaiSize(&pBag->eaiActiveSlots);
				eaiSetSize(&pBag->eaiActiveSlots, pDef->maxActiveSlots);
				for (i = oldSize; i < pDef->maxActiveSlots; ++i)
				{
					pBag->eaiActiveSlots[i] = -1;
				}
			}

			// Set the new slot ID
			pBag->eaiActiveSlots[iActiveIndex] = iNewActiveSlot;

			// Destroy sub powers on the old active slot
			if (pOldSlot && pOldSlot->pItem)
			{
				int iNumPowers = item_GetNumItemPowerDefs(pOldSlot->pItem, true);
				for (i = 0; i < iNumPowers; i++)
				{
					Power* pPower = item_GetPower(pOldSlot->pItem, i);
					PowerDef* pPowerDef = SAFE_GET_REF(pPower, hDef);
					if (pPowerDef && pPowerDef->eType == kPowerType_Combo &&
						!item_ItemPowerActive(pEnt, pBag, pOldSlot->pItem, i))
					{
						power_DestroySubPowers(pPower);
					}
				}
			}

			// Reset powers - the active bag changed
			character_ResetPowersArray(entGetPartitionIdx(pEnt), pEnt->pChar, pExtract);

			// Items may have new or old innate powers
			character_DirtyInnateEquip(pEnt->pChar);

			// Update Item art
			entity_UpdateItemArtAnimFX(pEnt);

			entity_SetDirtyBit(pEnt, parse_Inventory, pEnt->pInventoryV2, false);

			return true;
		}
	}
	return false;
}

static void gslBagResetActiveSlots(Entity* pEnt, S32 eBagID)
{
	IntEarrayWrapper Bags = {0};
	eaiPush(&Bags.eaInts, eBagID);

	if (pEnt->pInventoryV2)
	{
		S32 i;
		for (i = eaSize(&pEnt->pInventoryV2->ppSlotSwitchRequest)-1; i >= 0; i--)
		{
			eaiPushUnique(&Bags.eaInts, pEnt->pInventoryV2->ppSlotSwitchRequest[i]->eBagID);
		}
		eaDestroyStruct(&pEnt->pInventoryV2->ppSlotSwitchRequest, parse_InvBagSlotSwitchRequest);
	}
	ClientCmd_gclResetActiveSlotsForBags(pEnt, &Bags);
	StructDeInit(parse_IntEarrayWrapper, &Bags);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Inventory) ACMD_SERVERCMD ACMD_PRIVATE;
void gslBagChangeActiveSlotWhenReady(Entity* pEnt, 
									 S32 eBagID, 
									 S32 iActiveIndex, 
									 S32 iNewActiveSlot, 
									 U32 uiTime, 
									 U32 uiRequestID,
									 F32 fDelayOverride)
{
	if (pEnt)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		InventoryBag* pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), eBagID, pExtract);

		if (!inv_AddActiveSlotChangeRequest(pEnt, pBag, iActiveIndex, iNewActiveSlot, uiRequestID, fDelayOverride))
		{
			gslBagResetActiveSlots(pEnt, eBagID);
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Inventory) ACMD_SERVERCMD ACMD_PRIVATE;
void gslBagChangeActiveSlotSendMoveEventTime(Entity* pEnt, U32 uiRequestID, U32 uiTime)
{
	if (pEnt && pEnt->pInventoryV2 && eaSize(&pEnt->pInventoryV2->ppSlotSwitchRequest))
	{
		int i;
		for (i = eaSize(&pEnt->pInventoryV2->ppSlotSwitchRequest)-1; i >= 0; i--)
		{
			InvBagSlotSwitchRequest* pRequest = pEnt->pInventoryV2->ppSlotSwitchRequest[i];
			if (pRequest->uRequestID == uiRequestID)
			{
				pRequest->uTime = uiTime;
				break;
			}
		}
	}
}

void gslUpdateActiveSlotRequests(Entity* pEnt, F32 fElapsed, GameAccountDataExtract* pExtract)
{
	if (pEnt && pEnt->pInventoryV2 && eaSize(&pEnt->pInventoryV2->ppSlotSwitchRequest))
	{
		InvBagSlotSwitchRequest* pRequest = pEnt->pInventoryV2->ppSlotSwitchRequest[0];

		if (!pRequest->bHasChangedSlot && 
			gslBagChangeActiveSlot(pEnt, pRequest->eBagID, pRequest->iIndex, pRequest->iNewActiveSlot))
		{
			ClientCmd_gclChangeActiveBagSlotNow(pEnt, pRequest->uRequestID);
			pRequest->bHasChangedSlot = true;
		}

		if (!pRequest->bHasHandledMoveEvents && pRequest->bHasChangedSlot && pRequest->uTime)
		{
			InventoryBag* pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), pRequest->eBagID, pExtract);
			invbag_HandleMoveEvents(pEnt, pBag, pRequest->iNewActiveSlot, pRequest->uTime, pRequest->uRequestID, false);
			pRequest->bHasHandledMoveEvents = true;
		}

		pRequest->fTimer += fElapsed;
		if (pRequest->bHasChangedSlot && pRequest->fTimer >= pRequest->fDelay &&
			(pRequest->bHasHandledMoveEvents || pRequest->fTimer >= pRequest->fDelay + INV_CHANGE_ACTIVE_BAG_SLOT_TIMEOUT))
		{
			S32 eResetBagID = !pRequest->bHasHandledMoveEvents ? pRequest->eBagID : InvBagIDs_None;
			StructDestroy(parse_InvBagSlotSwitchRequest, eaRemove(&pEnt->pInventoryV2->ppSlotSwitchRequest, 0));

			if (eResetBagID != InvBagIDs_None)
			{
				gslBagResetActiveSlots(pEnt, eResetBagID);
			}
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(ItemWarp) ACMD_CATEGORY(Standard, Inventory) ACMD_PRIVATE ACMD_SERVERCMD;
void gslItemWarp(Entity *pEnt, U64 ulItemID, U32 iEntTargetID)
{
	GameAccountDataExtract *pExtract;
	InvBagIDs bagID;
	int iSlot;
	Item *pItem = inv_GetItemAndSlotsByID(pEnt, ulItemID, &bagID, &iSlot);
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	InventoryBag *pBag;

	if(!pEnt || !pEnt->pSaved || !pEnt->pPlayer)
	{
		return;
	}

	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), bagID, pExtract);

	if(pItem
		&& pItemDef
		&& pBag
		&& bagID != InvBagIDs_Buyback
		&& bagID != InvBagIDs_Overflow
		&& bagID != InvBagIDs_Bank
		&& pItemDef->pWarp)
	{
		//If it's not equipped and must be to be used...
		if(pItemDef->eType == kItemType_Device)
		{
			if(!(pItemDef->flags & kItemDefFlag_CanUseUnequipped) 
				&& !(invbag_flags(pBag) & InvBagFlag_DeviceBag))
			return;
		}
		else if(!(pItemDef->flags & kItemDefFlag_CanUseUnequipped) 
			&& !(invbag_flags(pBag) & InvBagFlag_EquipBag))
			return;

		if(pItem->bWarpActive)
			return;

		switch(pItemDef->pWarp->eWarpType)
		{
		case kItemWarp_SelfToTarget:
			{
				if(!iEntTargetID || !team_GetTeam(pEnt))
					return;

				pItem->bWarpActive = true;
				gslWarp_WarpToTarget_ChargeItem(pEnt, iEntTargetID, ulItemID);
				
				if(!pItemDef->pWarp->bCanMapMove)
					pItem->bWarpActive = false;
			}
			break;
		case kItemWarp_SelfToMapSpawn:
			{
				PlayerWarpToData *pWarp = StructCreate(parse_PlayerWarpToData);

				if(pItemDef->pWarp->bCanMapMove)
					pWarp->pchMap = StructAllocString(pItemDef->pWarp->pchMap);
				pWarp->pchSpawn = StructAllocString(pItemDef->pWarp->pchSpawn);
				pWarp->iTimestamp = timeSecondsSince2000();
				pWarp->uiItemId = pItem->id;

				pItem->bWarpActive = true;

				if(stricmp(zmapInfoGetPublicName(NULL),pItemDef->pWarp->pchMap)==0)
				{
					pWarp->iMapID = gGSLState.gameServerDescription.baseMapDescription.containerID;
					pWarp->uPartitionID = partition_IDFromIdx(entGetPartitionIdx(pEnt));
				}
				
				gslWarp_WarpToLocation(pEnt, pWarp);

				if(!pItemDef->pWarp->bCanMapMove)
					pItem->bWarpActive = false;

				StructDestroy(parse_PlayerWarpToData,pWarp);
			}
			break;
		case kItemWarp_TeamToSelf:
			{
				int i;
				INT_EARRAY piTargets = NULL;
				Team *pTeam = team_GetTeam(pEnt);
				int iPartitionIdx = entGetPartitionIdx(pEnt);

				if( zmapInfoGetMapType(NULL) == ZMTYPE_PVP || entity_IsWarpRestricted(zmapInfoGetPublicName(NULL)))
				{
					char *estrMsg = NULL;
					entFormatGameMessageKey(pEnt, &estrMsg, "RecruitWarp_Failed_MapRestriction",
						STRFMT_STRING("Direction", "to"),
						STRFMT_MESSAGEKEY("MapName", zmapInfoGetDisplayNameMsgKey(NULL)),
						STRFMT_END);
					notify_NotifySend(pEnt, kNotifyType_Failed, estrMsg, NULL, NULL);
					estrDestroy(&estrMsg);
					return;
				}

				//If there's not a team or this item can warp team members off the map
				if(!pTeam 
					|| (pTeam->iGameServerOwnerID && pItemDef->pWarp->bCanMapMove))
					return;

				for(i=eaSize(&pTeam->eaMembers)-1; i>=0; i--)
				{
					TeamMember *pMember = pTeam->eaMembers[i];
					Entity *pTargetEnt = NULL;
					ZoneMapInfo *pZoneInfo = NULL;

					if(!pMember || pMember->iEntID == entGetContainerID(pEnt))
						continue;
	
					// In this case, it means that this will only transport teams members on the map to your current location
					if(!pItemDef->pWarp->bCanMapMove)
						 pTargetEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pMember->iEntID);
					else
						pTargetEnt = GET_REF(pMember->hEnt);

					if(!pTargetEnt || entCheckFlag(pTargetEnt, ENTITYFLAG_PLAYER_DISCONNECTED))
						continue;

					pZoneInfo = zmapInfoGetByPublicName(pMember->pcMapName);
					if(pZoneInfo
						&& (entity_IsWarpRestricted(pMember->pcMapName) 
						|| zmapInfoGetMapType(pZoneInfo) == ZMTYPE_PVP
						|| zmapInfoGetMapType(pZoneInfo) == ZMTYPE_QUEUED_PVE))
					{
						continue;
					}

					eaiPush(&piTargets, pMember->iEntID);
				}
				
				if(eaiSize(&piTargets))
				{
					PlayerWarpToData *pWarp = StructCreate(parse_PlayerWarpToData);
					char *estrFullCharName = NULL;
					const char *pchMsgKey = zmapInfoGetDisplayNameMsgKey(NULL);

					//Setup the character name
					estrPrintf(&estrFullCharName, "%s", entGetPersistedName(pEnt));
					if(pEnt->pPlayer->publicAccountName)
						estrConcatf(&estrFullCharName, "@%s", pEnt->pPlayer->publicAccountName);

					entGetPos(pEnt, pWarp->vecTarget);
					pWarp->iTeamID = team_GetTeamID(pEnt);
					pWarp->iMapID = gGSLState.gameServerDescription.baseMapDescription.containerID;
					pWarp->uPartitionID = partition_IDFromIdx(iPartitionIdx);
					pWarp->iInstance = partition_PublicInstanceIndexFromIdx(iPartitionIdx);
					pWarp->pcMapVariables = allocAddString(partition_MapVariablesFromIdx(iPartitionIdx));
					pWarp->pchMap = StructAllocString(zmapInfoGetPublicName(NULL));
					pWarp->iTimestamp = timeSecondsSince2000();

					for(i=eaiSize(&piTargets)-1; i>=0; i--)
					{
						RemoteCommand_gslWarp_WarpToLocation(GLOBALTYPE_ENTITYPLAYER, piTargets[i], piTargets[i], pWarp, estrFullCharName, pchMsgKey, pItemDef->pWarp->uiTimeToConfirm);
					}

					gslItem_ChargeForWarp(pEnt, pItem);

					StructDestroy(parse_PlayerWarpToData,pWarp);
					estrDestroy(&estrFullCharName);
				}

				eaiDestroy(&piTargets);
			}
			break;
			
		//Warps your teammates and yourself to the map and spawn
		case kItemWarp_TeamToMapSpawn:
			{
				int i;
				INT_EARRAY piTargets = NULL;
				Team *pTeam = team_GetTeam(pEnt);
				int iPartitionIdx = entGetPartitionIdx(pEnt);

				if( zmapInfoGetMapType(NULL) == ZMTYPE_PVP || entity_IsWarpRestricted(zmapInfoGetPublicName(NULL)))
				{
					char *estrMsg = NULL;
					entFormatGameMessageKey(pEnt, &estrMsg, "RecruitWarp_Failed_MapRestriction",
						STRFMT_STRING("Direction", "from"),
						STRFMT_MESSAGEKEY("MapName", zmapInfoGetDisplayNameMsgKey(NULL)),
						STRFMT_END);
					notify_NotifySend(pEnt, kNotifyType_Failed, estrMsg, NULL, NULL);
					estrDestroy(&estrMsg);
					return;
				}

				//If there's not a team or this item can warp team members off the map
				if(!pTeam 
					|| (pTeam->iGameServerOwnerID && pItemDef->pWarp->bCanMapMove))
					return;

				for(i=eaSize(&pTeam->eaMembers)-1; i>=0; i--)
				{
					TeamMember *pMember = pTeam->eaMembers[i];
					Entity *pTargetEnt = NULL;
					ZoneMapInfo *pZoneInfo = NULL;
					if(!pMember)
						continue;

					// In this case, it means that this will only transport teams members on the map to your current location
					if(!pItemDef->pWarp->bCanMapMove)
						pTargetEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pMember->iEntID);
					else
						pTargetEnt = GET_REF(pMember->hEnt);

					if(!pTargetEnt || entCheckFlag(pTargetEnt, ENTITYFLAG_PLAYER_DISCONNECTED))
						continue;

					pZoneInfo = zmapInfoGetByPublicName(pMember->pcMapName);
					if(pZoneInfo
						&& (entity_IsWarpRestricted(pMember->pcMapName) 
							|| zmapInfoGetMapType(pZoneInfo) == ZMTYPE_PVP
							|| zmapInfoGetMapType(pZoneInfo) == ZMTYPE_QUEUED_PVE))
					{
						continue;
					}

					eaiPush(&piTargets, pMember->iEntID);
				}

				if(eaiSize(&piTargets))
				{
					PlayerWarpToData *pWarp = StructCreate(parse_PlayerWarpToData);
					char *estrFullCharName = NULL;
					const char *pchMsgKey = NULL;

					//Setup the character name
					estrPrintf(&estrFullCharName, "%s", entGetPersistedName(pEnt));
					if(pEnt->pPlayer->publicAccountName)
						estrConcatf(&estrFullCharName, "@%s", pEnt->pPlayer->publicAccountName);

					if(pItemDef->pWarp->pchMap)
					{
						ZoneMapInfo *pInfo = zmapInfoGetByPublicName(pItemDef->pWarp->pchMap);
						if(pInfo)
							pchMsgKey = zmapInfoGetDisplayNameMsgKey(pInfo);
						else
						{
							pchMsgKey = "[Unknown Map]";
						}
					}
					else
					{
						pchMsgKey = "Warp_SpawnpointOnMap";
					}

					if(pItemDef->pWarp->bCanMapMove)
						pWarp->pchMap = StructAllocString(pItemDef->pWarp->pchMap);
					if(!pWarp->pchMap || !pItemDef->pWarp->bCanMapMove)
					{
						pWarp->iMapID = gGSLState.gameServerDescription.baseMapDescription.containerID;
						pWarp->uPartitionID = partition_IDFromIdx(iPartitionIdx);
					}
					pWarp->pchSpawn = StructAllocString(pItemDef->pWarp->pchSpawn);
					pWarp->iTimestamp = timeSecondsSince2000();

					pItem->bWarpActive = true;

					for(i=eaiSize(&piTargets)-1; i>=0; i--)
					{
						if((ContainerID)piTargets[i] != entGetContainerID(pEnt))
							RemoteCommand_gslWarp_WarpToLocation(GLOBALTYPE_ENTITYPLAYER, piTargets[i], piTargets[i], pWarp, estrFullCharName, pchMsgKey, pItemDef->pWarp->uiTimeToConfirm);
					}

					gslWarp_WarpToLocation(pEnt, pWarp);

					gslItem_ChargeForWarp(pEnt, pItem);

					StructDestroy(parse_PlayerWarpToData,pWarp);
					estrDestroy(&estrFullCharName);
					if(!pItemDef->pWarp->bCanMapMove)
						pItem->bWarpActive = false;
				}

				eaiDestroy(&piTargets);
			}
			break;
		}
	}
}


void gslItem_ChargeForWarp(Entity *pEnt, Item *pItem)
{
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	if(pEnt && pItem && pItemDef)
	{
		pItem->bWarpActive = false;

		if(pItemDef->pWarp->bLimitedUse)
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
			TransactionReturnVal *pVal = LoggedTransactions_CreateManagedReturnValEnt("RemoveItem for WarpCharges", pEnt, NULL, NULL);
			ItemChangeReason reason = {0};

			inv_FillItemChangeReason(&reason, pEnt, "Powers:RemoveWarpItemOutOfCharges", NULL);

			AutoTrans_inv_ent_tr_RemoveItemByID(pVal, GLOBALTYPE_GAMESERVER, 
					entGetType(pEnt), entGetContainerID(pEnt), 
					pItem->id, 1, &reason, pExtract);
		}

		entity_SetDirtyBit(pEnt, parse_Inventory, pEnt->pInventoryV2, false);
	}
}

void gslItem_RollbackWarp(Entity *pEnt, Item *pItem)
{
	if(pEnt && pItem)
	{
		pItem->bWarpActive = false;

		entity_SetDirtyBit(pEnt, parse_Inventory, pEnt->pInventoryV2, false);
	}
}

#include "itemServer_h_ast.c"

