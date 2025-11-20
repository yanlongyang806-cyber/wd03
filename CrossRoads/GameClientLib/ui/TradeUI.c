#include "Expression.h"
#include "entCritter.h"
#include "EntityLib.h"
#include "EntitySavedData.h"
#include "GameAccountDataCommon.h"
#include "SavedPetCommon.h"

#include "UIGen.h"

#include "gclUtils.h"
#include "ItemCommon.h"
#include "itemCommon_h_ast.h"
#include "ItemEnums.h"
#include "itemEnums_h_ast.h"
#include "inventoryCommon.h"
#include "Player.h"
#include "tradeCommon.h"
#include "gclEntity.h"

#include "FCInventoryUI.h"

#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "tradeCommon_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

extern S32 GenExprItemIsTradedByIndex(SA_PARAM_NN_VALID Entity* pEnt, int BagIdx, int iSlot);

// Begin a trade session with the referenced player.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenBeginTrade");
void trade_BeginTrade(SA_PARAM_OP_VALID Entity* pEnt, EntityRef iEntRef)
{
	Entity* pTargetEnt = entFromEntityRefAnyPartition(iEntRef);
	if (pEnt && pEnt->pPlayer && pTargetEnt && pTargetEnt->pPlayer) {
		ServerCmd_trade_RequestTrade(iEntRef);
	}
}

// Begin a trade session with the referenced player.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenBeginTradeByID");
void trade_BeginTradeByID(SA_PARAM_OP_VALID Entity* pEnt, ContainerID iContainerID)
{
	Entity* pTargetEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iContainerID);
	if (pEnt && pEnt->pPlayer && pTargetEnt && pTargetEnt->pPlayer) {
		ServerCmd_trade_RequestTrade(pTargetEnt->myRef);
	}
}

// Get a list of items that this entity is offering for trade.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetEntityTradeList");
void trade_GetEntityTradeList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, bool bIncludeEmpty)
{
	TradeSlot ***peaPlayerSlotList = ui_GenGetManagedListSafe(pGen, TradeSlot);
	static TradeSlot s_EmptySlot = {0};

	if (SAFE_MEMBER3(pEnt, pPlayer, pTradeBag, ppTradeSlots))
		eaCopy(peaPlayerSlotList, &pEnt->pPlayer->pTradeBag->ppTradeSlots);
	else
		eaClear(peaPlayerSlotList);
	if (bIncludeEmpty)
		eaPush(peaPlayerSlotList, &s_EmptySlot);
	ui_GenSetManagedListSafe(pGen, peaPlayerSlotList, TradeSlot, false);
}

// Get a list of all tradeable numerics, in this player's inventory.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetTradeableNumericList");
void trade_GetTradeableNumericList(SA_PARAM_NN_VALID UIGen* pGen, SA_PARAM_OP_VALID Entity* pEnt)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	InventoryBag *pBag = pEnt ? (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), InvBagIDs_Numeric, pExtract) : NULL;
	Item ***peaItemList = ui_GenGetManagedListSafe(pGen, Item);
	S32 i;
	eaClear(peaItemList);
	if (pBag)
	{
		for (i = 0; i < eaSize(&pBag->ppIndexedInventorySlots); i++)
		{
			InventorySlot *pSlot = pBag->ppIndexedInventorySlots[i];
			Item *pItem = pSlot->pItem;
			ItemDef *pDef = pItem ? GET_REF(pItem->hItem) : NULL;
			S32 iAlready = pDef ? trade_GetNumericItemTradedCount(pEnt, pDef->pchName) : 0;
			if (item_CanTrade(pItem) && pSlot->pItem->count > iAlready)
				eaPush(peaItemList, pItem);
		}
	}
	ui_GenSetManagedListSafe(pGen, peaItemList, Item, false);
}

// Returns true if the target entity is a valid trade target for the given player, false otherwise.
// This currently checks to make sure that the target is a player, alive, and not disconnected.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenCanTradeWith");
bool trade_GenExprCanTradeWith(SA_PARAM_OP_VALID Entity *pPlayer, SA_PARAM_OP_VALID Entity *pTeamMate)
{
	return	pPlayer && pTeamMate &&
			pPlayer != pTeamMate &&
			pTeamMate->myEntityType == GLOBALTYPE_ENTITYPLAYER &&
			entIsAlive(pTeamMate) &&
			!entIsDisconnected(pTeamMate) &&
			entGetDistance(pPlayer, NULL, pTeamMate, NULL, NULL) <= gclEntity_GetInteractRange(pPlayer, pTeamMate, 0);
}

// Returns true if the entity from the given entity ref is a valid trade target for the given player.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenCanTradeWithByRef");
bool trade_GenExprCanTradeWithByRef(SA_PARAM_OP_VALID Entity *pPlayer, EntityRef iEntRef)
{
	Entity* pEnt = entFromEntityRefAnyPartition(iEntRef);
	return (pPlayer && pEnt) ? trade_GenExprCanTradeWith(pPlayer, pEnt) : false;
}

// Returns true if the entity from the given containerid is a valid trade target for the given player.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenCanTradeWithByID");
bool trade_GenExprCanTradeWithByID(SA_PARAM_OP_VALID Entity *pPlayer, ContainerID iContainerID)
{
	Entity* pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iContainerID);
	return (pPlayer && pEnt) ? trade_GenExprCanTradeWith(pPlayer, pEnt) : false;
}

// Returns a reference to the given entity's current trade partner, if any
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetEntityTradePartnerRef");
EntityRef trade_GenExprGetEntityTradePartnerRef(SA_PARAM_OP_VALID Entity* pEnt)
{
	return (pEnt ? pEnt->pPlayer->erTradePartner : 0);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenIsTradeAccepted");
bool trade_GenExprIsTradeAccepted(SA_PARAM_OP_VALID Entity* pEnt)
{
	return ((pEnt && pEnt->pPlayer && pEnt->pPlayer->pTradeBag) ? pEnt->pPlayer->pTradeBag->finished : false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenIsTargetTradeAccepted");
bool trade_GenExprIsTargetTradeAccepted(SA_PARAM_OP_VALID Entity* pEnt)
{
	if (pEnt) {
		Entity* pTargetEnt = entFromEntityRefAnyPartition(pEnt->pPlayer->erTradePartner);
		if (pTargetEnt) {
			return trade_GenExprIsTradeAccepted(pTargetEnt);
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenAddTradeItemByIndex");
void GenExprAddTradeItemByIndex(int SrcBagIdx,
								int SrcSlot,
								int TargetSlot,
								int iCount)
{
	if ( SrcBagIdx >= 0 ) {
		ServerCmd_trade_AddTradeItem(SrcBagIdx, SrcSlot, TargetSlot, iCount);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenAddTradeItemByKey");
bool GenExprAddTradeItemByKey(const char *pchKeyA,
								int TargetSlot,
								int iCount)
{
	UIInventoryKey KeyA = {0};

	if (!gclInventoryParseKey(pchKeyA, &KeyA))
		return false;
	if (!KeyA.pSlot || !KeyA.pSlot->pItem || KeyA.pOwner != entActivePlayerPtr())
		return false;

	if (KeyA.pOwner == KeyA.pEntity)
		ServerCmd_trade_AddTradeItem(KeyA.eBag, KeyA.iSlot, TargetSlot, iCount);
	else
		ServerCmd_trade_AddTradeItemFromEnt(KeyA.eType, KeyA.iContainerID, KeyA.eBag, KeyA.iSlot, TargetSlot, iCount);

	return true;
}

// Add a given amount of numerics to the player's trade offer list.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenAddPlayerTradeNumeric");
void trade_GenAddPlayerTradeNumeric(SA_PARAM_OP_VALID Entity* pEnt, SA_PARAM_NN_STR const char* pchNumericName, SA_PARAM_NN_STR const char* pchCountStr)
{
	int count = atoi(pchCountStr);
	if (count > 0) {
		ServerCmd_trade_AddTradeNumericItem(pchNumericName, count);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenRemoveTradeItem");
void GenExprRemoveTradeItem(SA_PARAM_NN_VALID UIGen *pGen,
							int Slot)
{
	ServerCmd_trade_RemoveTradeItem(Slot);
}

// This will return true if the given numeric string is a valid number and the given entity 
// has at least that many of that numeric.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenIsValidNumericOffer");
bool GenExprIsValidNumericOffer(SA_PARAM_OP_VALID Entity* pEnt, SA_PARAM_NN_VALID const char* pchNumericName, SA_PARAM_NN_VALID const char* CountStr)
{
	S32 count = atoi(CountStr);
	S32 totalCount = count;
	S32 curNumeric = 0;
	int ii = 0;
	
	if (pEnt == NULL || pEnt->pPlayer == NULL || pEnt->pPlayer->pTradeBag == NULL) {
		return false;
	}
	
	curNumeric = inv_GetNumericItemValue(pEnt, pchNumericName);

	// If a numeric is already being offered, it's the combined total that needs to be tested, not the
	// amount being added.
	for (ii = 0; ii < eaSize(&pEnt->pPlayer->pTradeBag->ppTradeSlots); ii++) {
		TradeSlot* pSlot = eaGet(&pEnt->pPlayer->pTradeBag->ppTradeSlots, ii);
		Item* pTradeItem = pSlot->pItem;
		ItemDef* pTradeItemDef = GET_REF(pTradeItem->hItem);
		if (stricmp(pTradeItemDef->pchName, pchNumericName) == 0) {
			// the numeric already exists, so get the combined offer
			totalCount += pTradeItem->count;
			break;
		}
	}

	// Check to make sure that the amount offered is between one and the the player's current numeric,
	// inclusive.  An offer of zero is invalid, whether it's because of an invalid string or an actual
	// offer of nothing.
	return (count > 0 && INRANGE(totalCount, 1, (curNumeric+1)));
}

// Returns true if the given player's item is an equipped bag.
// FIXME: Remove, equivalent to BagIDPlayerBags == BagIndex. --jfw
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenItemIsEquippedBag");
bool GenExprItemIsEquippedBag(SA_PARAM_OP_VALID Entity* pEnt, int BagIndex, int Slot)
{
	return (BagIndex == StaticDefineIntGetInt(InvBagIDsEnum, "PlayerBags"));
}

static void gclTrade_GetTradeableItems(SA_PARAM_NN_VALID UIGen* pGen, SA_PARAM_OP_VALID Entity* pEnt, S32 iBag, bool bIncludePets)
{
	TradeSlotLite ***peaSlots = ui_GenGetManagedListSafe(pGen, TradeSlotLite);
	
	Trade_GetTradeableItems(peaSlots,pEnt,iBag,bIncludePets,true);

	ui_GenSetManagedListSafe(pGen, peaSlots, TradeSlotLite, true);
}

// Get the tradeable items from the given bag, or all bags if passed -1.
// The items are put into TradeSlotLite structures. Excludes saved pets
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetTradeableItemsNoPets");
void trade_GetGetTradeableItemsNoPets(SA_PARAM_NN_VALID UIGen* pGen, SA_PARAM_OP_VALID Entity* pEnt, S32 iBag)
{
	gclTrade_GetTradeableItems(pGen, pEnt, iBag, false);
}

// Get the tradeable items from the given bag, or all bags if passed -1.
// The items are put into TradeSlotLite structures.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetTradeableItems");
void trade_GetGetTradeableItems(SA_PARAM_NN_VALID UIGen* pGen, SA_PARAM_OP_VALID Entity* pEnt, S32 iBag)
{
	gclTrade_GetTradeableItems(pGen, pEnt, iBag, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("SendGiftToPlayer");
void GenExprSendGiftToPlayer(SA_PARAM_OP_VALID Entity* pEnt, int iEntID, int SrcBagIdx, int SrcSlot)
{
	GameAccountDataExtract *pExtract;
	Item* pItem;

	if (!iEntID) {
		return;
	}
	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	pItem = inv_GetItemFromBag(pEnt, SrcBagIdx, SrcSlot, pExtract);

	if (pItem)
	{
		if(item_MoveRequiresUniqueCheck(pEnt, false, SrcBagIdx, SrcSlot, entFromContainerID(entGetPartitionIdx(pEnt), entGetType(pEnt), iEntID), false, InvBagIDs_Inventory, -1, pExtract))
		{
			Entity* pRecipientEnt = entFromContainerIDAnyPartition(entGetType(pEnt), iEntID);
			if (!pRecipientEnt)
			{
				Alertf("SendGiftToPlayer: Couldn't find recipient entity for unique item move. The recipient entity must be on the same GameServer.");
			}
		}
		ServerCmd_trade_SendGift(iEntID, SrcBagIdx, SrcSlot);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetEntityTradePartnerName");
	const char *GenExprGetEntityTradePartnerName(SA_PARAM_OP_VALID Entity* pEnt)
{
	const char *pchName = NULL;

	if (pEnt && pEnt->pPlayer)
	{
		Entity *pTradePartner = entFromEntityRef(entGetPartitionIdx(pEnt), pEnt->pPlayer->erTradePartner);

		if (pTradePartner)
		{
			pchName = entGetLocalName(pTradePartner);
		}
	}

	return NULL_TO_EMPTY(pchName);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetEntityTradePartnerHandle");
	const char *GenExprGetEntityTradePartnerHandle(SA_PARAM_OP_VALID Entity* pEnt)
{
	const char *pchName = NULL;

	if (pEnt && pEnt->pPlayer)
	{
		Entity *pTradePartner = entFromEntityRef(entGetPartitionIdx(pEnt), pEnt->pPlayer->erTradePartner);

		if (pTradePartner)
		{
			pchName = entGetAccountOrLocalName(pTradePartner);
		}
	}

	return NULL_TO_EMPTY(pchName);
}


//*****************************************************************************
// Description: Determines if the pet is currently being offered in a trade by
//				the pet's owner.
// Returns:     < bool >  True if the pet is being offered in a trade
// Parameter:   < SA_PARAM_OP_VALID Entity * pOwner >  The pet's owner
// Parameter:   < SA_PARAM_OP_VALID Entity * pPetEnt >  The pet
//*****************************************************************************
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("IsPetBeingTraded");
bool trade_ExprIsPetBeingTraded(SA_PARAM_OP_VALID Entity* pOwner, SA_PARAM_OP_VALID Entity* pPetEnt)
{
	return trade_IsPetBeingTraded(pPetEnt, pOwner);
}



















