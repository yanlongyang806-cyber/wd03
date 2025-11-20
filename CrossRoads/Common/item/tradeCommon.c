#include "itemCommon.h"
#include "inventoryCommon.h"
#include "tradeCommon.h"
#include "EString.h"
#include "GameAccountDataCommon.h"
#include "GameStringFormat.h"
#include "Entity.h"
#include "EntitySavedData.h"
#include "Player.h"
#include "GlobalTypes.h"
#include "SavedPetCommon.h"
#include "OfficerCommon.h"
#include "GamePermissionsCommon.h"

#include "Entity_h_ast.h"
#include "tradeCommon_h_ast.h"
#include "entCritter.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

#if GAMESERVER || GAMECLIENT
char *trade_OfferIsValid(Entity *pEnt1, GameAccountDataExtract *pExtract)
{
	char *estrBuffer = NULL;
	Entity *pEnt2, *pPet = NULL;
	S32 i, iNumItems, iAvailableSpace, iSrcCount;
	TradeSlot *pSlot = NULL;
	ItemDef *pItemDef = NULL;
	Item *pBagItem;
	InventoryBag* pSrcBag;
	GameAccountDataExtract *pExtract2 = NULL;
	int iPartitionIdx;
	StashTable stBagCounts = NULL;
	StashTableIterator it = {0};
	StashElement Elem;

	if (!pEnt1) {
		return NULL;
	}

	if (!pEnt1->pPlayer || !pEnt1->pPlayer->pTradeBag || !pEnt1->pPlayer->erTradePartner) {
		entFormatGameMessageKey(pEnt1, &estrBuffer, "TradeError_PlayerNotTrading",
			STRFMT_PLAYER(pEnt1),
			STRFMT_END);
		return estrBuffer;
	}

	iPartitionIdx = entGetPartitionIdx(pEnt1);
	pEnt2 = entFromEntityRef(iPartitionIdx, pEnt1->pPlayer->erTradePartner);

	if (!pEnt2 || !pEnt2->pPlayer || !pEnt2->pPlayer->pTradeBag || pEnt2->pPlayer->erTradePartner != entGetRef(pEnt1)) {
		entFormatGameMessageKey(pEnt1, &estrBuffer, "TradeError_TargetNotTrading",
			STRFMT_PLAYER(pEnt1),
			STRFMT_TARGET(pEnt2),
			STRFMT_END);
		return estrBuffer;
	}

	pExtract2 = entity_GetCachedGameAccountDataExtract(pEnt2);
	stBagCounts = stashTableCreateInt(16);

	for (i = 0; i < eaSize(&pEnt1->pPlayer->pTradeBag->ppTradeSlots); i++) {
		pSlot = pEnt1->pPlayer->pTradeBag->ppTradeSlots[i];
		pItemDef = pSlot && pSlot->pItem ? GET_REF(pSlot->pItem->hItem) : NULL;
		pPet = NULL;

		if (pSlot->tradeSlotPetID)
		{
			pPet = entity_GetSubEntity(iPartitionIdx, pEnt1, GLOBALTYPE_ENTITYSAVEDPET, pSlot->tradeSlotPetID);
		}
		else 
		{
			if ((pSrcBag = CONTAINER_RECONST(InventoryBag, inv_GetBag(CONTAINER_NOCONST(Entity, pEnt1), pSlot->SrcBagId, pExtract))) &&
				 !item_ItemMoveValidTemporaryPuppetCheck(pEnt1, pSrcBag))
			{
				// Don't allow the player to trade an item on a temporary puppet
				entFormatGameMessageKey(pEnt1, &estrBuffer, "TradeError_ItemOnTempPuppet",
					STRFMT_PLAYER(pEnt1),
					STRFMT_TARGET(pEnt2),
					STRFMT_INT("slotnum", i),
					STRFMT_END);
				stashTableDestroy(stBagCounts);
				return estrBuffer;
			}
		}

		if (!pItemDef) {
			entFormatGameMessageKey(pEnt1, &estrBuffer, "TradeError_ItemInvalid",
				STRFMT_PLAYER(pEnt1),
				STRFMT_TARGET(pEnt2),
				STRFMT_INT("slotnum", i),
				STRFMT_END);
			stashTableDestroy(stBagCounts);
			return estrBuffer;
		}

		if (pItemDef->eType == kItemType_Numeric) {
			S32 iNumericGamePermissionMax;
			if (!(pItemDef->flags & kItemDefFlag_Tradeable)) {
				entFormatGameMessageKey(pEnt1, &estrBuffer, "TradeError_NumericNotTradeable",
					STRFMT_PLAYER(pEnt1),
					STRFMT_TARGET(pEnt2),
					STRFMT_INT("slotnum", i),
					STRFMT_INT("count", pSlot->count),
					STRFMT_ITEM(pSlot->pItem),
					STRFMT_ITEMDEF(pItemDef),
					STRFMT_END);
				stashTableDestroy(stBagCounts);
				return estrBuffer;
			}

			if (pSlot->pItem->count < 0 || pSlot->pItem->count < pItemDef->MinNumericValue) {
				entFormatGameMessageKey(pEnt1, &estrBuffer, "TradeError_NumericValueTooSmall",
					STRFMT_PLAYER(pEnt1),
					STRFMT_TARGET(pEnt2),
					STRFMT_INT("slotnum", i),
					STRFMT_INT("count", pSlot->count),
					STRFMT_ITEM(pSlot->pItem),
					STRFMT_ITEMDEF(pItemDef),
					STRFMT_END);
				stashTableDestroy(stBagCounts);
				return estrBuffer;
			}

			if (pPet)
			{
				iSrcCount = inv_GetNumericItemValue(pPet, pItemDef->pchName);
			}
			else
			{
				iSrcCount = inv_GetNumericItemValue(pEnt1, pItemDef->pchName);
			}

			if (pSlot->pItem->count > iSrcCount) {
				entFormatGameMessageKey(pEnt1, &estrBuffer, "TradeError_NotEnoughNumeric",
					STRFMT_PLAYER(pEnt1),
					STRFMT_TARGET(pEnt2),
					STRFMT_INT("slotnum", i),
					STRFMT_INT("count", pSlot->pItem->count),
					STRFMT_ITEM(pSlot->pItem),
					STRFMT_ITEMDEF(pItemDef),
					STRFMT_END);
				stashTableDestroy(stBagCounts);
				return estrBuffer;
			}

			if (pSlot->pItem->count > pItemDef->MaxNumericValue || (iSrcCount - pSlot->pItem->count) < pItemDef->MinNumericValue) {
				entFormatGameMessageKey(pEnt1, &estrBuffer, "TradeError_NumericValueTooLarge",
					STRFMT_PLAYER(pEnt1),
					STRFMT_TARGET(pEnt2),
					STRFMT_INT("slotnum", i),
					STRFMT_INT("count", pSlot->pItem->count),
					STRFMT_ITEM(pSlot->pItem),
					STRFMT_ITEMDEF(pItemDef),
					STRFMT_END);
				stashTableDestroy(stBagCounts);
				return estrBuffer;
			}

			iSrcCount = inv_GetNumericItemValue(pEnt2, pItemDef->pchName);
			iNumericGamePermissionMax = GamePermissions_trh_GetCachedMaxNumeric(CONTAINER_NOCONST(Entity, pEnt2), pItemDef->pchName, true);

			if ((iSrcCount + pSlot->pItem->count) > min(pItemDef->MaxNumericValue, iNumericGamePermissionMax)) {
				entFormatGameMessageKey(pEnt1, &estrBuffer, "TradeError_NumericValueTooLargeForTarget",
					STRFMT_PLAYER(pEnt1),
					STRFMT_TARGET(pEnt2),
					STRFMT_INT("slotnum", i),
					STRFMT_INT("count", pSlot->pItem->count),
					STRFMT_ITEM(pSlot->pItem),
					STRFMT_ITEMDEF(pItemDef),
					STRFMT_END);
				stashTableDestroy(stBagCounts);
				return estrBuffer;
			}
		} else {
			InvBagIDs eCountBag = InvBagIDs_Inventory;

			if (pPet)
			{
				pBagItem = inv_GetItemFromBag(pPet, pSlot->SrcBagId, pSlot->SrcSlot, pExtract);
			}
			else
			{
				pBagItem = inv_GetItemFromBag(pEnt1, pSlot->SrcBagId, pSlot->SrcSlot, pExtract);
			}
			
			if (!pBagItem && pSlot->SrcBagId != -1) {
				entFormatGameMessageKey(pEnt1, &estrBuffer, "TradeError_ItemNotFound",
					STRFMT_PLAYER(pEnt1),
					STRFMT_TARGET(pEnt2),
					STRFMT_INT("slotnum", i),
					STRFMT_INT("count", pSlot->count),
					STRFMT_ITEM(pSlot->pItem),
					STRFMT_ITEMDEF(pItemDef),
					STRFMT_END);
				stashTableDestroy(stBagCounts);
				return estrBuffer;
			}

			if (pPet)
			{
				iSrcCount = inv_ent_CountItems(pPet, pSlot->SrcBagId, pItemDef->pchName, pExtract);
			}
			else
			{
				iSrcCount = inv_ent_CountItems(pEnt1, pSlot->SrcBagId, pItemDef->pchName, pExtract);
			}

			if(pBagItem)
			{
				if (pSlot->pItem->id != pBagItem->id &&
					(pItemDef->iStackLimit == 1 || pSlot->count >= iSrcCount || pItemDef->pchName != REF_STRING_FROM_HANDLE(pSlot->pItem->hItem))) {
						entFormatGameMessageKey(pEnt1, &estrBuffer, "TradeError_ItemMismatch",
							STRFMT_PLAYER(pEnt1),
							STRFMT_TARGET(pEnt2),
							STRFMT_INT("slotnum", i),
							STRFMT_INT("count", pSlot->count),
							STRFMT_ITEM(pSlot->pItem),
							STRFMT_ITEMDEF(pItemDef),
							STRFMT_END);
						stashTableDestroy(stBagCounts);
						return estrBuffer;
				}

				if (pSlot->count > iSrcCount) {
					entFormatGameMessageKey(pEnt1, &estrBuffer, "TradeError_InsufficientItemCount",
						STRFMT_PLAYER(pEnt1),
						STRFMT_TARGET(pEnt2),
						STRFMT_INT("slotnum", i),
						STRFMT_INT("count", pSlot->count),
						STRFMT_ITEM(pSlot->pItem),
						STRFMT_ITEMDEF(pItemDef),
						STRFMT_END);
					stashTableDestroy(stBagCounts);
					return estrBuffer;
				}

				if (pBagItem->flags & (kItemFlag_Bound | kItemFlag_BoundToAccount)) {
					entFormatGameMessageKey(pEnt1, &estrBuffer, "TradeError_ItemBound",
						STRFMT_PLAYER(pEnt1),
						STRFMT_TARGET(pEnt2),
						STRFMT_INT("slotnum", i),
						STRFMT_INT("count", pSlot->count),
						STRFMT_ITEM(pSlot->pItem),
						STRFMT_ITEMDEF(pItemDef),
						STRFMT_END);
					stashTableDestroy(stBagCounts);
					return estrBuffer;
				}

				if ((pItemDef->flags & kItemDefFlag_LockToRestrictBags) && eaiFind(&pItemDef->peRestrictBagIDs, eCountBag) < 0)
				{
					eCountBag = GetBestBagForItemDef(pEnt2, pItemDef, pSlot->count, true, pExtract2);
				}
			} else {
				if(pItemDef->eType == kItemType_Container && pSlot->pItem->pSpecialProps && pSlot->pItem->pSpecialProps->pContainerInfo)
				{
					ContainerID iPetID = StringToContainerID(REF_STRING_FROM_HANDLE(pSlot->pItem->pSpecialProps->pContainerInfo->hSavedPet));
					int iPet;

					for(iPet=eaSize(&pEnt1->pSaved->ppOwnedContainers)-1;iPet>=0;iPet--)
					{
						if(iPetID == pEnt1->pSaved->ppOwnedContainers[iPet]->conID)
						{
							Entity *pEntPet = SavedPet_GetEntity(entGetPartitionIdx(pEnt1), pEnt1->pSaved->ppOwnedContainers[iPet]);

							if(!Entity_IsPetTransferValid(pEnt1, pEnt2, pEntPet, &estrBuffer))
							{
								stashTableDestroy(stBagCounts);
								return estrBuffer;
							}

							break;
						}
					}
					

					if(i==-1)
					{
						entFormatGameMessageKey(pEnt1, &estrBuffer, "TradeError_PetNotFound",
							STRFMT_PLAYER(pEnt1),
							STRFMT_INT("slotnum", i),
							STRFMT_END);
						stashTableDestroy(stBagCounts);
						return estrBuffer;
					}

					// Container items do not need to be counted towards the total number of items since they are immediately turned into a saved pet
					eCountBag = InvBagIDs_None;
				}
			}

			if (eCountBag != InvBagIDs_None)
			{
				int iCount;
				if (!stashIntFindInt(stBagCounts, eCountBag, &iCount))
					iCount = 0;
				stashIntAddInt(stBagCounts, eCountBag, iCount + 1, true);
			}
		}
	}

	iNumItems = 0;
	iAvailableSpace = 0;

	stashGetIterator(stBagCounts, &it);
	while (stashGetNextElement(&it, &Elem))
	{
		InvBagIDs eBag = stashElementGetIntKey(Elem);
		iNumItems = stashElementGetInt(Elem);
		iAvailableSpace = inv_ent_AvailableSlots(pEnt2, eBag, pExtract2);

		if (eBag == InvBagIDs_Inventory)
		{
			// When checking the inventory bag, also check the player bags.
			iAvailableSpace +=
				inv_ent_AvailableSlots(pEnt2, InvBagIDs_PlayerBag1, pExtract2) +
				inv_ent_AvailableSlots(pEnt2, InvBagIDs_PlayerBag2, pExtract2) +
				inv_ent_AvailableSlots(pEnt2, InvBagIDs_PlayerBag3, pExtract2) +
				inv_ent_AvailableSlots(pEnt2, InvBagIDs_PlayerBag4, pExtract2) +
				inv_ent_AvailableSlots(pEnt2, InvBagIDs_PlayerBag5, pExtract2) +
				inv_ent_AvailableSlots(pEnt2, InvBagIDs_PlayerBag6, pExtract2) +
				inv_ent_AvailableSlots(pEnt2, InvBagIDs_PlayerBag7, pExtract2) +
				inv_ent_AvailableSlots(pEnt2, InvBagIDs_PlayerBag8, pExtract2) +
				inv_ent_AvailableSlots(pEnt2, InvBagIDs_PlayerBag9, pExtract2);
		}

		if (iNumItems > iAvailableSpace)
		{
			break;
		}
	}

	stashTableDestroy(stBagCounts);

	if (iNumItems > iAvailableSpace)
	{
		entFormatGameMessageKey(pEnt1, &estrBuffer, "TradeError_InsufficientSpace",
			STRFMT_PLAYER(pEnt1),
			STRFMT_TARGET(pEnt2),
			STRFMT_INT("numitems", i),
			STRFMT_ITEM(pSlot->pItem),
			STRFMT_ITEMDEF(pItemDef),
			STRFMT_END);
		return estrBuffer;
	}

	return NULL;
}

#endif

//*****************************************************************************
// Description:	Determines if the pet entity is being traded by the src entity 
// Returns:     < bool >  True if pet is in pSrcEnt's trade bag
// Parameter:   < Entity * pPetEnt >  The pet entity
// Parameter:   < Entity * pSrcEnt >  The owning player entity
//*****************************************************************************
bool trade_IsPetBeingTraded( Entity* pPetEnt, Entity* pSrcEnt ) 
{
	if(!pPetEnt || !pSrcEnt)
		return false;

	if(entGetType(pPetEnt) == GLOBALTYPE_ENTITYSAVEDPET && pPetEnt->pCritter)
	{
		if(pSrcEnt && pSrcEnt->pPlayer && pSrcEnt->pPlayer->pTradeBag)
		{
			int iNumSlots = eaSize(&pSrcEnt->pPlayer->pTradeBag->ppTradeSlots);
			TradeSlot* pSlot = NULL;
			int i;
			ContainerID iPetID = entGetContainerID(pPetEnt);

			for(i=0;i<iNumSlots;i++)
			{
				pSlot = eaGet(&pSrcEnt->pPlayer->pTradeBag->ppTradeSlots,i);

				if(pSlot && pSlot->pItem && pSlot->pItem->pSpecialProps && pSlot->pItem->pSpecialProps->pContainerInfo
					&& StringToContainerID(REF_STRING_FROM_HANDLE(pSlot->pItem->pSpecialProps->pContainerInfo->hSavedPet)) == iPetID)
				{
					return true;
				}
			}
		}
	}

	return false;
}

// Returns the amount of the given numeric item already offered for trade, or zero if not being traded.
int trade_GetNumericItemTradedCount(SA_PARAM_OP_VALID Entity* pEnt, SA_PARAM_NN_STR const char* pItemDefName)
{
	int count = 0;
	int ii = 0;

	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pTradeBag) {
		for (ii = 0; ii < eaSize(&pEnt->pPlayer->pTradeBag->ppTradeSlots); ii++) {
			TradeSlot* pTradeSlot = eaGet(&pEnt->pPlayer->pTradeBag->ppTradeSlots, ii);
			Item* pTradeItem = pTradeSlot->pItem;

			if (pTradeItem) {
				ItemDef* pTradeItemDef = GET_REF(pTradeItem->hItem);
				if (stricmp(pTradeItemDef->pchName, pItemDefName) == 0) {
					count = pTradeItem->count;
					break;
				}
			}
		}
	}

	return count;
}

#ifdef GAMECLIENT
extern S32 GenExprItemIsTradedByIndex(SA_PARAM_NN_VALID Entity* pEnt, int BagIdx, int iSlot);
#endif

void Trade_GetTradeableItems(TradeSlotLite ***peaSlots, Entity* pEnt, S32 iBag, bool bIncludePets, bool bIncludeUnidentified)
{
	S32 iCount = 0;
	
	if (pEnt && pEnt->pInventoryV2)
	{
		S32 i;
		S32 j;
		S32 iPet;
		for (j = 0; j < eaSize(&pEnt->pInventoryV2->ppInventoryBags); j++)
		{
			InventoryBag *pBag = pEnt->pInventoryV2->ppInventoryBags[j];

			bool bInANormalBag = itemHandling_IsBagTradeable(invbag_bagid(pBag));

			if (((iBag < 0 && bInANormalBag) || (invbag_bagid(pBag) == iBag)) && !(invbag_flags(pBag) & InvBagFlag_BankBag))
			{
				for (i = 0; i < eaSize(&pBag->ppIndexedInventorySlots); i++)
				{
					InventorySlot *pSlot = pBag->ppIndexedInventorySlots[i];
					if (pSlot->pItem && item_CanTrade(pSlot->pItem) && (bIncludeUnidentified || !item_IsUnidentified(pSlot->pItem)))
					{
						ItemDef *pDef = GET_REF(pSlot->pItem->hItem);
						S32 iTotal;
						if (pDef->eType == kItemType_Numeric)
						{
							iTotal = pSlot->pItem->count;
							iTotal -= trade_GetNumericItemTradedCount(pEnt, pDef->pchName);
						}
						else
						{
							iTotal = pSlot->pItem->count;
#if GAMECLIENT
							iTotal -= GenExprItemIsTradedByIndex(pEnt, invbag_bagid(pBag), i);
#endif
						}
						if (iTotal > 0)
						{
							TradeSlotLite *pTrade = eaGetStruct(peaSlots, parse_TradeSlotLite, iCount++);
							pTrade->count = iTotal;
							pTrade->SrcSlot = i;
							pTrade->SrcBagId = invbag_bagid(pBag);
							pTrade->pItem = pSlot->pItem;
						}
					}
				}
				if (iBag >= 0)
					break;
			}
		}

		if(bIncludePets) 
		{
			for(iPet=eaSize(&pEnt->pSaved->ppOwnedContainers)-1;iPet>=0;iPet--)
			{
				PuppetEntity *pPuppet = SavedPet_GetPuppetFromPet(pEnt,pEnt->pSaved->ppOwnedContainers[iPet]);
				Entity *pEntPet = SavedPet_GetEntity(PARTITION_CLIENT, pEnt->pSaved->ppOwnedContainers[iPet]);
				TradeSlotLite *pTrade = NULL;
				PetDef *pPetDef = pEntPet && pEntPet->pCritter ? GET_REF(pEntPet->pCritter->petDef) : NULL;
				ItemDef *pItemDef = pPetDef ? GET_REF(pPetDef->hTradableItem) : NULL;

				if(!pEntPet || !pItemDef) {
					continue;
				}

				// Can't trade a pet that's active
				if(pEnt->pSaved->ppOwnedContainers[iPet]->curEntity)
				{
					continue;
				}

				// Can't trade a puppet that's active
				if(pPuppet && pPuppet->eState == PUPPETSTATE_ACTIVE)
				{
					continue;
				}

				// Can't trade a pet which has items in its inventory
				if(pEntPet && pEntPet->pInventoryV2)
				{
					int NumBags,ItemCount,ii;

					NumBags = eaSize(&pEntPet->pInventoryV2->ppInventoryBags);

					ItemCount = 0;
					for(ii=0;ii<NumBags && !ItemCount;ii++)
					{
						if(pEntPet->pInventoryV2->ppInventoryBags[ii]->BagID != InvBagIDs_Numeric)
							ItemCount += inv_bag_CountItems(pEntPet->pInventoryV2->ppInventoryBags[ii], NULL);
					}

					if(ItemCount)
					{
						continue;
					}
				}

				// Success
				pTrade = eaGetStruct(peaSlots, parse_TradeSlotLite, iCount++);
				pTrade->count = 1;
				pTrade->SrcSlot = -1;
				pTrade->SrcBagId = -1;
				pTrade->pItem = item_FromPetInfo(pItemDef->pchName,entGetContainerID(pEntPet),GET_REF(pEnt->hAllegiance),GET_REF(pEnt->hSubAllegiance),inv_ent_getEntityItemQuality(pEntPet));
				pTrade->tradeSlotPetID = entGetContainerID(pEntPet);
			}
		}
	}

	while (eaSize(peaSlots) > iCount)
		StructDestroy(parse_TradeSlotLite, eaPop(peaSlots));
}

#include "AutoGen/tradeCommon_h_ast.c"
