#include "LootUI.h"

#include "earray.h"
#include "EntitySavedData.h"

#include "rewardcommon.h"
#include "GameAccountDataCommon.h"
#include "gclEntity.h"
#include "cmdparse.h"
#include "Expression.h"
#include "Player.h"

#include "UIGen.h"

#include "Entity.h"
#include "EntityLib.h"
#include "ItemCommon.h"
#include "itemCommon_h_ast.h"
#include "ItemEnums.h"
#include "itemEnums_h_ast.h"
#include "inventoryCommon.h"
#include "StringCache.h"
#include "tradeCommon.h"
#include "mission_common.h"
#include "tradeCommon_h_ast.h"
#include "AutoGen/Entity_h_ast.h"
#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "LootUI_h_ast.h"
#include "LootUI_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static TradeSlot **s_eaCompareList = NULL;
static TradeSlot **s_pLootTradeSlotList = NULL;
static bool s_bOverflow = false;

AUTO_STRUCT;
typedef struct LootCompareSlot
{
	TradeSlot Slot; AST(EMBEDDED_FLAT)
	S32 iDiffCount;
} LootCompareSlot;

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void LootTake(int iSlot, U64 iID)
{
	if (s_bOverflow)
		ServerCmd_ItemMove(false, InvBagIDs_Overflow, iSlot, false, InvBagIDs_None, -1, -1);
	else//slot is unreliable if we don't own the bag, so use ID instead
		ServerCmd_loot_InteractTake(iID, InvBagIDs_None, -1);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void LootTakeInventory(int iSlot, U64 iID)
{
	if (s_bOverflow)
		ServerCmd_ItemMove(false, InvBagIDs_Overflow, iSlot, false, InvBagIDs_Inventory, -1, -1);
	else
		ServerCmd_loot_InteractTake(iID, InvBagIDs_Inventory, -1);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void LootTakeAll(void)
{
	if (s_bOverflow)
	{
		Entity *pEnt = entActivePlayerPtr();
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		InventoryBag *pOverflowBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), InvBagIDs_Overflow, pExtract);
		int i;

		for (i = 0; i < eaSize(&pOverflowBag->ppIndexedInventorySlots); i++)
		{
			if (pOverflowBag->ppIndexedInventorySlots[i]->pItem)
				ServerCmd_ItemMove(false, InvBagIDs_Overflow, i, false, InvBagIDs_None, -1, -1);
		}
	}
	else
		ServerCmd_loot_InteractTakeAll(InvBagIDs_None);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void LootTakeAllSpecifyBag(InvBagIDs eBag)
{
	if (s_bOverflow)
	{
		Entity *pEnt = entActivePlayerPtr();
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		InventoryBag *pOverflowBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), InvBagIDs_Overflow, pExtract);
		int i;

		for (i = 0; i < eaSize(&pOverflowBag->ppIndexedInventorySlots); i++)
		{
			if (pOverflowBag->ppIndexedInventorySlots[i]->pItem)
				ServerCmd_ItemMove(false, InvBagIDs_Overflow, i, false, eBag, -1, -1);
		}
	}
	else
		ServerCmd_loot_InteractTakeAll(eBag);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void LootTakeAllInventory(void)
{
	LootTakeAllSpecifyBag(InvBagIDs_Inventory);
}


AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void LootTakeAllOfTypeSpecifyBag(const char* pchType, InvBagIDs eBag)
{
	ItemType eItemType = pchType ? StaticDefineIntGetInt(ItemTypeEnum, pchType) : -1;
	
	if(eItemType == -1)
		return;

	if (s_bOverflow)
	{
		Entity *pEnt = entActivePlayerPtr();
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		InventoryBag *pOverflowBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), InvBagIDs_Overflow, pExtract);
		int i;

		for (i = 0; i < eaSize(&pOverflowBag->ppIndexedInventorySlots); i++)
		{
			if (pOverflowBag->ppIndexedInventorySlots[i]->pItem) {
				ItemDef* pDef = GET_REF(pOverflowBag->ppIndexedInventorySlots[i]->pItem->hItem);
				if(pDef && pDef->eType == eItemType)
					ServerCmd_ItemMove(false, InvBagIDs_Overflow, i, false, eBag, -1, -1);
			}
		}
	}
	else
		ServerCmd_loot_InteractTakeAllOfType(eItemType, eBag);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void LootTakeAllExceptTypeSpecifyBag(const char* pchType, InvBagIDs eBag)
{
	ItemType eItemType = pchType ? StaticDefineIntGetInt(ItemTypeEnum, pchType) : -1;

	if(eItemType == -1)
		LootTakeAllSpecifyBag(eBag);

	if (s_bOverflow)
	{
		Entity *pEnt = entActivePlayerPtr();
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		InventoryBag *pOverflowBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), InvBagIDs_Overflow, pExtract);
		int i;

		for (i = 0; i < eaSize(&pOverflowBag->ppIndexedInventorySlots); i++)
		{
			if (pOverflowBag->ppIndexedInventorySlots[i]->pItem) {
				ItemDef* pDef = GET_REF(pOverflowBag->ppIndexedInventorySlots[i]->pItem->hItem);
				if(!pDef || pDef->eType != eItemType)
					ServerCmd_ItemMove(false, InvBagIDs_Overflow, i, false, eBag, -1, -1);
			}
		}
	}
	else
		ServerCmd_loot_InteractTakeAllExceptType(eItemType, eBag);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void LootOverflowDiscard(S32 iSlot)
{
	if (s_bOverflow)
		ServerCmd_item_RemoveFromBag(InvBagIDs_Overflow, iSlot, -1, NULL);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void LootOverflowDiscardAll(void)
{
	if (s_bOverflow)
	{
		Entity *pEnt = entActivePlayerPtr();
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		InventoryBag *pOverflowBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), InvBagIDs_Overflow, pExtract);
		int i;

		for (i = 0; i < eaSize(&pOverflowBag->ppIndexedInventorySlots); i++)
		{
			if (pOverflowBag->ppIndexedInventorySlots[i]->pItem)
				ServerCmd_item_RemoveFromBag(InvBagIDs_Overflow, i, -1, NULL);
		}
	}
}

// Don't take loot, just destroy the client list
AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void LootCancel(Entity *pEnt)
{
	eaDestroyStruct(&s_pLootTradeSlotList, parse_TradeSlot);
}

static bool gclLootCreateTradeSlotListFromBag(InventoryBag* pBag, TradeSlot*** peaList)
{
	if (inv_bag_CountItems(pBag, NULL))
	{
		BagIterator* iter = invbag_IteratorFromBag(CONTAINER_NOCONST(InventoryBag, pBag));

		for (; !bagiterator_Stopped(iter); bagiterator_Next(iter))
		{
			TradeSlot *pNewSlot;
			Item *pItem = (Item*)bagiterator_GetItem(iter);
			ItemDef *pItemDef = bagiterator_GetDef(iter);
			int iItemCount = bagiterator_GetItemCount(iter);

			if (!pItem)
				continue;

			pNewSlot = StructCreate(parse_TradeSlot);
			pNewSlot->pItem = StructClone(parse_Item, pItem);
			pNewSlot->count = iItemCount;
			pNewSlot->SrcBagId = pBag->BagID;
			pNewSlot->SrcSlot = iter->i_cur;
			eaPush(peaList, pNewSlot);
		}
		bagiterator_Destroy(iter);
		return true;
	}
	return false;
}

// User interacted with a loot bag, pop up the loot dialog.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE;
void LootInteraction(Entity *pEnt, InventoryBag *pLootBag)
{
	eaDestroyStruct(&s_pLootTradeSlotList, parse_TradeSlot);
	if (pLootBag)
	{
		s_bOverflow = (pLootBag->BagID == InvBagIDs_Overflow);

		// get a list of all items in the specified bag
		gclLootCreateTradeSlotListFromBag(pLootBag, &s_pLootTradeSlotList);

		// set interaction status on the client
		if (pEnt && pEnt->pPlayer && !s_bOverflow)
		{
			pEnt->pPlayer->InteractStatus.bInteracting = true;
			entity_SetDirtyBit(pEnt, parse_EntInteractStatus, &pEnt->pPlayer->InteractStatus, true);
			entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
		}

		globCmdParse("GenSendMessage Loot_Root Show");
	}
}

// Update the contents of the loot bag (called from the server)
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE;
void LootUpdateList(Entity *pEnt, InventoryBag *pLootBag)
{
	// ignore if this is trying to update the overflow bag with non-overflow bag data (or vice versa)
	if ((pLootBag->BagID == InvBagIDs_Overflow) != s_bOverflow)
		return;

	eaClearStruct(&s_pLootTradeSlotList, parse_TradeSlot);
	
	if (pLootBag)
	{
		gclLootCreateTradeSlotListFromBag(pLootBag, &s_pLootTradeSlotList);
	}
}

// User interacted with a loot bag, pop up the loot dialog.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE;
void LootDestroyList(Entity *pEnt)
{
	eaDestroyStruct(&s_pLootTradeSlotList, parse_TradeSlot);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE;
void LootUpdateCompareList(Entity *pEnt, InventoryBag *pCompareBag)
{
	eaDestroyStruct(&s_eaCompareList, parse_TradeSlot);

	if (pCompareBag)
	{
		gclLootCreateTradeSlotListFromBag(pCompareBag, &s_eaCompareList);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(LootDestroyCompareList);
void lootui_DestroyCompareList(void)
{
	eaDestroyStruct(&s_eaCompareList, parse_TradeSlot);
}

static bool lootui_UpdateOverflowList(void)
{
	if (s_bOverflow)
	{
		Entity *pEnt = entActivePlayerPtr();
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		InventoryBag *pOverflowBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), InvBagIDs_Overflow, pExtract);
		if (pEnt && pOverflowBag)
			LootUpdateList(entActivePlayerPtr(), pOverflowBag);
		return true;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetLootInteractionList);
void lootui_GetLootInteractionList(SA_PARAM_NN_VALID UIGen *pGen)
{
	Entity *pPlayer = entActivePlayerPtr();
	int i, j;
	static InventorySlot **ppSlots = NULL;

	if(!pPlayer || !pPlayer->pPlayer)
		return;

	if(eaSize(&ppSlots))
	{
		eaClearStruct(&ppSlots,parse_InventorySlot);
	}

	for(i=0;i<eaSize(&pPlayer->pPlayer->InteractStatus.interactOptions.eaOptions);i++)
	{
		if(pPlayer->pPlayer->InteractStatus.interactOptions.eaOptions[i]->entRef == pPlayer->pPlayer->InteractStatus.overrideRef
			&& eaSize(&pPlayer->pPlayer->InteractStatus.interactOptions.eaOptions[i]->eaLootBags) > 0)
		{
			for (j = 0; j < eaSize(&pPlayer->pPlayer->InteractStatus.interactOptions.eaOptions[i]->eaLootBags); j++)
			{
				eaPushStructs(&ppSlots,&pPlayer->pPlayer->InteractStatus.interactOptions.eaOptions[i]->eaLootBags[j]->ppIndexedInventorySlots,parse_InventorySlot);
			}
		}
	}
	ui_GenSetManagedListSafe(pGen,&ppSlots, InventorySlot,false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetLootInteractionTradeSlotList);
void lootui_GetLootInteractionTradeSlotList(SA_PARAM_NN_VALID UIGen *pGen)
{
	Entity *pPlayer = entActivePlayerPtr();
	int i, j;
	TradeSlot **eaNewSlots = NULL;
	TradeSlot ***ppSlots = ui_GenGetManagedListSafe(pGen, TradeSlot);

	if(!pPlayer || !pPlayer->pPlayer)
		return;

	if(eaSize(ppSlots))
	{
		eaClearStruct(ppSlots,parse_TradeSlot);
	}

	for(i=0;i<eaSize(&pPlayer->pPlayer->InteractStatus.interactOptions.eaOptions);i++)
	{
		if(pPlayer->pPlayer->InteractStatus.interactOptions.eaOptions[i]->entRef == pPlayer->pPlayer->InteractStatus.overrideRef
			&& eaSize(&pPlayer->pPlayer->InteractStatus.interactOptions.eaOptions[i]->eaLootBags) > 0)
		{
			for (j = 0; j < eaSize(&pPlayer->pPlayer->InteractStatus.interactOptions.eaOptions[i]->eaLootBags); j++)
			{
				gclLootCreateTradeSlotListFromBag(pPlayer->pPlayer->InteractStatus.interactOptions.eaOptions[i]->eaLootBags[j],&eaNewSlots);
			}
		}
		eaCopy(ppSlots, &eaNewSlots);
		
	}
	ui_GenSetManagedListSafe(pGen,ppSlots, TradeSlot,true);
}

// Get a list of currently available loot.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetLootTradeSlotList);
void lootui_GetLootTradeSlotList(SA_PARAM_NN_VALID UIGen *pGen)
{
	// update the list in real time if looting the overflow bag
	lootui_UpdateOverflowList();

	// Band-aid to try to fix a crash: I'll just copy the structs.
	// This is bad for performance, but protects against s_pLootTradeSlotList being modified
	// at the wrong time and crashing the client.
	{
		TradeSlot ***peaSlots = ui_GenGetManagedListSafe(pGen, TradeSlot);
		eaCopyStructs(&s_pLootTradeSlotList, peaSlots, parse_TradeSlot);
		ui_GenSetManagedListSafe(pGen, peaSlots, TradeSlot, true);
	}
}

// Get a list of currently available loot, and compare it to the items in s_eaCompareList if possible.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetLootTradeSlotListCompare);
void lootui_GetLootTradeSlotListCompare(SA_PARAM_NN_VALID UIGen *pGen)
{
	LootCompareSlot ***peaSlots = ui_GenGetManagedListSafe(pGen, LootCompareSlot);

	// update the list in real time if looting the overflow bag
	lootui_UpdateOverflowList();

	if (!eaSize(&s_eaCompareList))
	{
		S32 i, iSize = eaSize(&s_pLootTradeSlotList);
		eaSetSizeStruct(peaSlots, parse_LootCompareSlot, iSize);
		for (i = 0; i < iSize; i++)
		{
			TradeSlot* pSlot = s_pLootTradeSlotList[i];
			LootCompareSlot* pCompareSlot = eaGetStruct(peaSlots, parse_LootCompareSlot, i);
			pCompareSlot->iDiffCount = 0;
			StructCopyAll(parse_TradeSlot, pSlot, &pCompareSlot->Slot);
		}
	}
	else
	{
		S32 i, j, iSize = eaSize(&s_pLootTradeSlotList);
		eaSetSizeStruct(peaSlots, parse_LootCompareSlot, iSize);
		for (i = 0; i < iSize; i++)
		{
			LootCompareSlot* pNewSlot;
			TradeSlot* pSlot = s_pLootTradeSlotList[i];
			ItemDef* pItemDef = pSlot->pItem ? GET_REF(pSlot->pItem->hItem) : NULL;
			S32 iCompareCount = 0;
			if (pItemDef)
			{
				for (j = eaSize(&s_eaCompareList)-1; j >= 0; j--)
				{
					if (s_eaCompareList[j]->pItem
						&& pItemDef == GET_REF(s_eaCompareList[j]->pItem->hItem))
					{
						iCompareCount = s_eaCompareList[j]->count;
						break;
					}
				}
			}
			pNewSlot = eaGetStruct(peaSlots, parse_LootCompareSlot, i);
			pNewSlot->iDiffCount = pSlot->count - iCompareCount;
			StructCopyAll(parse_TradeSlot, pSlot, &pNewSlot->Slot);
		}
	}
	ui_GenSetManagedListSafe(pGen, peaSlots, LootCompareSlot, true);
}

//EXPRESSIONS

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(LootCloseCheck);
bool LootCloseCheck(SA_PARAM_OP_VALID Entity *pEnt)
{
	return !s_bOverflow && (!pEnt || !pEnt->pPlayer || !pEnt->pPlayer->InteractStatus.bInteracting);
}

AUTO_EXPR_FUNC(UIGen);
bool LootIsOverflow(void)
{
	return s_bOverflow;
}

AUTO_EXPR_FUNC(UIGen);
bool InteractionIsCrafting(void)
{
	Entity *pEnt = entActivePlayerPtr();
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pInteractInfo) {
		return pEnt->pPlayer->pInteractInfo->bCrafting;
	} else {
		return false;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenLootOverflow);
void LootOverflow(Entity *pEnt)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	InventoryBag *pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), InvBagIDs_Overflow, pExtract);

	// cannot open overflow bag while the current loot UI is open
	if (s_pLootTradeSlotList)
		return;

	// open the loot UI using the local overflow bag info
	LootInteraction(pEnt, pBag);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(LootTake);
void LootTakeExpr(S32 iSlot, U64 iID)
{
	LootTake(iSlot, iID);
}

AUTO_EXPR_FUNC(UIGen);
void LootEndInteraction(void)
{
	ServerCmd_interactloot_StopInteracting();
}

// This can be used by the server to open the overflow UI on the client
AUTO_COMMAND ACMD_NAME(LootOverflow) ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE;
void LootOverflowCmd(void)
{
	LootOverflow(entActivePlayerPtr());
}

Item* GetLootItem( S32 iSlot )
{
	TradeSlot *pSlot = eaGet(&s_pLootTradeSlotList, iSlot);
	if (pSlot)
		return pSlot->pItem;
	return NULL;
}

/*******************
* NEED OR GREED
*******************/
// STRUCTURES AND GLOBALS

// This structure holds the need or greed client data for the UI
AUTO_STRUCT;
typedef struct NeedOrGreedClientData
{
	TeamLootItemSlot **eaItems;
} NeedOrGreedClientData;

// Global need or greed client data
static NeedOrGreedClientData s_NeedOrGreedData = {NULL};
static NeedOrGreedClientData s_NeedOrGreedRemoveQueue = {NULL};
static int iTimerIdx = 0;

// CLIENT COMMANDS

// This is called for all team members when someone in the team interacts with
// a need or greed bag for the first time
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE;
void NeedOrGreedInteract(Entity *pEnt, EntityRef uiLootEnt, const char *pchLootInteractable, InventoryBag *pLootBag)
{
	BagIterator *iter;

	if (!pEnt || !pEnt->pPlayer)
		return;
	
	if (!gConf.bLootModesDontCountAsInteraction)
	{
		pEnt->pPlayer->InteractStatus.bInteracting = true;
		entity_SetDirtyBit(pEnt, parse_EntInteractStatus, &pEnt->pPlayer->InteractStatus, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}
	
	if (!iTimerIdx)
	{
		iTimerIdx = timerAlloc();
		timerStart(iTimerIdx);
	}

	// append info for the newly looted items
	iter = invbag_IteratorFromBag(CONTAINER_NOCONST(InventoryBag, pLootBag));

	for (; !bagiterator_Stopped(iter); bagiterator_Next(iter))
	{
		Item *pItem = (Item*)bagiterator_GetItem(iter);
		ItemDef *pItemDef = bagiterator_GetDef(iter);
		if (pItem)
		{
			TeamLootItemSlot *pNewItemSlot = StructCreate(parse_TeamLootItemSlot);
			ItemQuality eQuality;

			pNewItemSlot->uiLootEnt = uiLootEnt;
			pNewItemSlot->pchLootInteractable = allocAddString(pchLootInteractable);
			pNewItemSlot->pItem = StructClone(parse_Item, pItem);
			pNewItemSlot->iCount = bagiterator_GetItemCount(iter);
			pNewItemSlot->iItemID = pItem->id;
			pNewItemSlot->iLocalStartTime = timerElapsed(iTimerIdx) * 1000;

			eQuality = item_GetQuality(pNewItemSlot->pItem);
			if (eQuality < 0 || eQuality >= eaSize(&g_ItemQualities.ppQualities))
				eQuality = 0;

			pNewItemSlot->iTimeLimit = 1000 * g_ItemQualities.ppQualities[eQuality]->iNeedBeforeGreedDelay;
			eaPush(&s_NeedOrGreedData.eaItems, pNewItemSlot);
		}
	}
	bagiterator_Destroy(iter);

	// ensure the UI is showing
	if (ui_GenFind("NeedOrGreed_Root", kUIGenTypeNone))
		globCmdParse("GenSendMessage NeedOrGreed_Root Show");
}

// EXPRESSIONS

// This helper removes the associated item slot from the list model
void NeedOrGreedItemSlotRemove(TeamLootItemSlot *pItemSlot)
{
	eaPush(&s_NeedOrGreedRemoveQueue.eaItems, pItemSlot);
}

// This is used to tell the server that the current player chose "Need" on the specified item slot
AUTO_EXPR_FUNC(UIGen);
void NeedOrGreedChooseNeed(SA_PARAM_NN_VALID TeamLootItemSlot *pItemSlot)
{
	ServerCmd_NeedOrGreedChoose(pItemSlot->uiLootEnt, pItemSlot->pchLootInteractable, pItemSlot->iItemID, NeedOrGreedChoice_Need);
	NeedOrGreedItemSlotRemove(pItemSlot);
}

// This is used to tell the server that the current player chose "Greed" on the specified item slot
AUTO_EXPR_FUNC(UIGen);
void NeedOrGreedChooseGreed(SA_PARAM_NN_VALID TeamLootItemSlot *pItemSlot)
{
	ServerCmd_NeedOrGreedChoose(pItemSlot->uiLootEnt, pItemSlot->pchLootInteractable, pItemSlot->iItemID, NeedOrGreedChoice_Greed);
	NeedOrGreedItemSlotRemove(pItemSlot);
}

// This is used to tell the server that the current player chose "Pass" on the specified item slot
AUTO_EXPR_FUNC(UIGen);
void NeedOrGreedChoosePass(SA_PARAM_NN_VALID TeamLootItemSlot *pItemSlot)
{
	ServerCmd_NeedOrGreedChoose(pItemSlot->uiLootEnt, pItemSlot->pchLootInteractable, pItemSlot->iItemID, NeedOrGreedChoice_Pass);
	NeedOrGreedItemSlotRemove(pItemSlot);
}

AUTO_EXPR_FUNC(UIGen);
int NeedOrGreedGetItemTimeRemainingMS(SA_PARAM_NN_VALID TeamLootItemSlot *pItemSlot)
{
	return pItemSlot->iLocalStartTime + pItemSlot->iTimeLimit - 1000 * timerElapsed(iTimerIdx);
}

// This sets the list of looted need-or-greed items onto the specified gen's model
AUTO_EXPR_FUNC(UIGen);
void NeedOrGreedGetItems(SA_PARAM_NN_VALID UIGen *pGen)
{
	// remove items that are unique and this player has
	S32 i;
	Entity *pEnt = entActivePlayerPtr();

	if(pEnt)
	{
		for(i = eaSize(&s_NeedOrGreedData.eaItems) - 1; i >= 0; --i)
		{
			TeamLootItemSlot *pItemSlot = s_NeedOrGreedData.eaItems[i];
			Item *pItem = pItemSlot->pItem;
			int iTimeRemaining = NeedOrGreedGetItemTimeRemainingMS(pItemSlot);
			int iQueueIndex;

			// Remove items queued for removal.
			if (iTimeRemaining < 0)
			{
				NeedOrGreedChoosePass(pItemSlot);
			}
			if((iQueueIndex = eaFind(&s_NeedOrGreedRemoveQueue.eaItems, pItemSlot)) >= 0)
			{
				eaRemove(&s_NeedOrGreedData.eaItems, i);
				eaRemove(&s_NeedOrGreedRemoveQueue.eaItems, iQueueIndex);
				StructDestroy(parse_TeamLootItemSlot, pItemSlot);
				continue;
			}
			if(pItem)
			{
				ItemDef *pDef = GET_REF(pItem->hItem);
				if(pDef && (pDef->flags & kItemDefFlag_Unique) != 0 && inv_ent_AllBagsCountItems(pEnt, pDef->pchName) > 0)
				{
					NeedOrGreedItemSlotRemove(pItemSlot);
				}
			}
		}
	}

	ui_GenSetList(pGen, &s_NeedOrGreedData.eaItems, parse_TeamLootItemSlot);
}

AUTO_EXPR_FUNC(UIGen);
S32 NeedOrGreedCountItems(void)
{
	return eaSize(&s_NeedOrGreedData.eaItems);
}

/*******************
* MASTER LOOTER
*******************/
// STRUCTURES AND GLOBALS

// This is the structure used in the model of the list of team members that can
// be assigned an item
AUTO_STRUCT;
typedef struct MasterLooterTeamMember
{
	char *peasDisplayName;		AST(ESTRING)	// display name for the UI
	EntityRef uiRef;							// entity ref of the team member
} MasterLooterTeamMember;

// This is the structure that holds all of the client data for the UI
AUTO_STRUCT;
typedef struct MasterLooterClientData
{
    MasterLooterTeamMember **peaMembers;		// current team members that can be assigned loot
    TeamLootItemSlot **peaItems;				// loot items
	TeamLootItemSlot **eaQueueForDeletion;
} MasterLooterClientData;

// Global master looter client data
static MasterLooterClientData s_MasterLooterData = {0};

// EXPRESSION FUNCTIONS

// This function sets the passed-in UIGen's model to the list of items in the
// master loot bag
AUTO_EXPR_FUNC(UIGen);
void MasterLooterGetItems(SA_PARAM_NN_VALID UIGen *pGen)
{
	int i;
	for (i = 0; i < eaSize(&s_MasterLooterData.eaQueueForDeletion); i++)
	{
		TeamLootItemSlot *pItemSlot = s_MasterLooterData.eaQueueForDeletion[i];
		eaFindAndRemove(&s_MasterLooterData.peaItems, pItemSlot);
		StructDestroy(parse_TeamLootItemSlot, pItemSlot);
	}
	eaClearFast(&s_MasterLooterData.eaQueueForDeletion);
	ui_GenSetList(pGen, &s_MasterLooterData.peaItems, parse_TeamLootItemSlot);
}

// This function sets the passed-in UIGen's model to the list of the current player's
// team members; reupdated every call in case someone joins/leaves the team
AUTO_EXPR_FUNC(UIGen);
void MasterLooterGetTeamMembers(SA_PARAM_NN_VALID UIGen *pGen)
{
	Entity *pEnt = entActivePlayerPtr();
	Team *pTeam = team_GetTeam(pEnt);
	Entity **pTeamMembers = NULL;
	int i;

	// if the current player is not part of a team, return
	if (!pTeam)
		return;

	// clear old team member data
	eaClear(&s_MasterLooterData.peaMembers);

	// recreate team member data
	team_GetOnMapEntsUnique(entGetPartitionIdx(pEnt), &pTeamMembers, pTeam, false);
	for (i = eaSize(&pTeamMembers) - 1; i >= 0; i--)
	{
		MasterLooterTeamMember *pNewMember = NULL;
		char *pchName;

		if (!pTeamMembers[i])
			continue;

		pchName = (char*) SAFE_MEMBER2(pTeamMembers[i], pSaved, savedName);
		if (!pchName)
			continue;
		pNewMember = StructCreate(parse_MasterLooterTeamMember);
		pNewMember->uiRef = pTeamMembers[i]->myRef;
		estrCopy2(&pNewMember->peasDisplayName, pchName);
		eaPush(&s_MasterLooterData.peaMembers, pNewMember);
	}

	ui_GenSetList(pGen, &s_MasterLooterData.peaMembers, parse_MasterLooterTeamMember);
	eaDestroy(&pTeamMembers);
}

// This function gives the specified item stack to the specified player
AUTO_EXPR_FUNC(UIGen);
void MasterLooterGiveLoot(SA_PARAM_NN_VALID TeamLootItemSlot *pItemSlot, EntityRef uiEntityRef)
{
	if (!pItemSlot || !uiEntityRef)
		return;

	if (pItemSlot->iItemID > -1)
		ServerCmd_MasterLooterGiveLoot(pItemSlot->iItemID, uiEntityRef);

	eaPush(&s_MasterLooterData.eaQueueForDeletion, pItemSlot);
}

// This function is used to prompt the team to roll on a particular item
AUTO_EXPR_FUNC(UIGen);
void MasterLooterPromptRoll(SA_PARAM_NN_VALID TeamLootItemSlot *pItemSlot)
{
	if (pItemSlot->iItemID < 0)
		return;

	ServerCmd_MasterLooterPromptRoll(pItemSlot->iItemID);
}

// CLIENT COMMANDS

// This is called when a player interacts with the master looter bag
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE;
void MasterLooterInteract(Entity *pEnt, EntityRef uiLootEnt, const char *pchLootInteractable, InventoryBag *pLootBag)
{
	BagIterator *iter;
	
    if (!pEnt || !pEnt->pPlayer)
		return;

	if (!gConf.bLootModesDontCountAsInteraction)
	{
		pEnt->pPlayer->InteractStatus.bInteracting = true;
		entity_SetDirtyBit(pEnt, parse_EntInteractStatus, &pEnt->pPlayer->InteractStatus, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}
	
	// clear old data
    StructDeInit(parse_MasterLooterClientData, &s_MasterLooterData);
    StructInit(parse_MasterLooterClientData,&s_MasterLooterData);

	// update the items
	iter = invbag_IteratorFromBag(CONTAINER_NOCONST(InventoryBag, pLootBag));

	for (; !bagiterator_Stopped(iter); bagiterator_Next(iter))
	{
		Item *pItem = (Item*)bagiterator_GetItem(iter);
		ItemDef *pItemDef = bagiterator_GetDef(iter);
		if (pItem)
		{
			TeamLootItemSlot *pNewItemSlot = StructCreate(parse_TeamLootItemSlot);

			// master looter UI does not currently support use of roll data
			pNewItemSlot->pItem = StructClone(parse_Item, pItem);
			pNewItemSlot->iCount = bagiterator_GetItemCount(iter);
			pNewItemSlot->iItemID = pItem->id;
			eaPush(&s_MasterLooterData.peaItems, pNewItemSlot);
		}
	}
	bagiterator_Destroy(iter);

	// show UI
    globCmdParse("GenSendMessage MasterLooter_Root Show");
}

#include "LootUI_h_ast.c"
#include "LootUI_c_ast.c"
