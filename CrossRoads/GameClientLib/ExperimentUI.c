/***************************************************************************
*     Copyright (c) 2006-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Expression.h"
#include "tradeCommon.h"
#include "GameAccountDataCommon.h"
#include "gclEntity.h"
#include "UIGen.h"
#include "ItemCommon.h"
#include "itemCommon_h_ast.h"
#include "ItemEnums.h"
#include "itemEnums_h_ast.h"
#include "inventoryCommon.h"
#include "rewardCommon.h"
#include "Mission_Common.h"
#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "tradeCommon_h_ast.h"
#include "qsortG.h"
#include "timing.h"
#include "Player.h"
#include "entCritter.h"

#include "ExperimentUI_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

void ExperimentClearList(void);

/********
* STRUCTS
********/
AUTO_STRUCT;
typedef struct ComponentSlot
{
	Item *pItem;									// component item preview
	int iRecipeCount;								// per-item deconstruction count
	int iMaxNum;									// maximum number that can be procured; displayed in the UI
} ComponentSlot;

// Each experiment slot will only hold up to the number of items in the bag slot from which the item came;
// this means that multiple experiment slots can hold the same item if they came from different bag slots
AUTO_STRUCT;
typedef struct ExperimentSlot
{
	Item *pItem;					AST(UNOWNED)	// item that is set using the inventory/slot indexes immediately before rendering a row (in case the pointer becomes invalidated)
	int iEPVal;										// this is calculated at the same time the item is set to make total cost calculation faster
	float fRewardIndex;								// this is calculated after the EPValue is updated

	ComponentSlot **eaComponents;					// component preview containing AGGREGATED components (i.e. already multiplied by iCount)

	// data sent to the server
	int iCount;										// total number of pItem in the slot
	int iSrcBagId;									// the bag from which this deconstruction item came from
	int iSrcBagSlot;								// the bag slot from which this deconstruction item came from
} ExperimentSlot;

/********
* GLOBALS
********/
// the list of items being experimented on
static ExperimentSlot **s_eaExperimentSlotList = NULL;
static ExperimentSlot **s_eaExperimentSlotListRemoveQueue = NULL;

// the total list of components that comprise the experimented items
static ComponentSlot **s_eaExperimentComponentPreview = NULL;

// holds the EP ranges fetched from the server; should have the minimum value for each range in the range table
static int *s_eaiExperimentRanges = NULL;

// holds the total calculated reward index
static float s_fTotalRewardIdx;

// holds the timer index
static U32 s_iTimerIndex;

static char* s_aExperimentableBagNames[] =
	{"Inventory",
	 "PlayerBag1",
	 "PlayerBag2",
	 "PlayerBag3",
	 "PlayerBag4",
	 "PlayerBag5",
	 "PlayerBag6"};

/*****************
* HELPER FUNCTIONS
*****************/
static int ExperimentComponentSlotCompareCount(const ComponentSlot **ppSlot1, const ComponentSlot **ppSlot2)
{
	return (*ppSlot1)->iRecipeCount - (*ppSlot2)->iRecipeCount;
}

static int ExperimentComponentSlotCompareMaxNum(const ComponentSlot **ppSlot1, const ComponentSlot **ppSlot2)
{
	return (*ppSlot1)->iMaxNum - (*ppSlot2)->iMaxNum;
}

// this function updates the total component preview using the contents of the individual slots
static void ExperimentUpdateTotal(void)
{
	StashTable pComponentStash = stashTableCreateAddress(32);
	StashTableIterator iter;
	StashElement el;
	int i, j;

	eaDestroyStruct(&s_eaExperimentComponentPreview, parse_ComponentSlot);
	for (i = 0; i < eaSize(&s_eaExperimentSlotList); i++)
	{
		ExperimentSlot *pCurrSlot = s_eaExperimentSlotList[i];

		if (!pCurrSlot)
			continue;

		for (j = 0; j < eaSize(&pCurrSlot->eaComponents); j++)
		{
			ComponentSlot *pCurrComponent = pCurrSlot->eaComponents[j];
			ComponentSlot *pTotalComponent;
			ItemDef *pComponentDef = (pCurrComponent && pCurrComponent->pItem) ? GET_REF(pCurrComponent->pItem->hItem) : NULL;

			if (!pComponentDef)
				continue;

			// if component exists in the stash, add up the counts
			if (stashAddressFindPointer(pComponentStash, pComponentDef, &pTotalComponent))
				pTotalComponent->iMaxNum += pCurrComponent->iMaxNum;
			// otherwise add it to the stash
			else
			{
				pTotalComponent = StructClone(parse_ComponentSlot, pCurrComponent);
				pTotalComponent->iRecipeCount = 0;
				stashAddressAddPointer(pComponentStash, pComponentDef, pTotalComponent, false);
			}
		}
	}

	// grab all the stash contents, put them into the earray, and sort it by greatest to least counts
	stashGetIterator(pComponentStash, &iter);
	while (stashGetNextElement(&iter, &el))
		eaPush(&s_eaExperimentComponentPreview, stashElementGetPointer(el));
	eaQSort(s_eaExperimentComponentPreview, ExperimentComponentSlotCompareMaxNum);

	stashTableDestroy(pComponentStash);
}

// gets the per-item component values; does not set the maximum number of components that can be procured
void ExperimentSlotUpdateComponents(Entity *pEnt, ExperimentSlot *pSlot, GameAccountDataExtract *pExtract)
{
	ItemCraftingComponent **eaComponents = NULL;
	Item *pItem = pSlot->pItem;
	int i;

	// clear old component list
	eaClearStruct(&pSlot->eaComponents, parse_ComponentSlot);

	// find the components
	item_GetDeconstructionComponents(pItem, &eaComponents);

	// create the list
	for (i = 0; i < eaSize(&eaComponents); i++)
	{
		ItemDef *pComponentDef = eaComponents[i] ? GET_REF(eaComponents[i]->hItem) : NULL;
		Item *pComponent = pComponentDef ? (Item*) item_FromEnt(CONTAINER_NOCONST(Entity, pEnt), pComponentDef->pchName, 0, NULL, 0) : NULL;
		ComponentSlot *pComponentSlot;

		if (!pComponent)
			continue;

		pComponentSlot = StructCreate(parse_ComponentSlot);
		pComponentSlot->iRecipeCount = floor(eaComponents[i]->fCount);
		pComponentSlot->pItem = pComponent;
		eaPush(&pSlot->eaComponents, pComponentSlot);
	}
	eaDestroyStruct(&eaComponents, parse_ItemCraftingComponent);

	// sort the list
	eaQSort(pSlot->eaComponents, ExperimentComponentSlotCompareCount);
}

// this function ensures that experimentation item counts don't exceed inventory total; this
// also updates the maximum number of components to be procured based on the the per-item count
// and the number of items being experimented for a given slot
static void ExperimentSlotUpdateCounts(Entity *pEnt, ExperimentSlot *pSlot, GameAccountDataExtract *pExtract)
{
	int iItemCount, i;

	if (!pSlot)
		return;

	// ensure object count does not exceed count of items in slot
	iItemCount = inv_ent_GetSlotItemCount(pEnt, pSlot->iSrcBagId, pSlot->iSrcBagSlot, pExtract);
	CLAMP(pSlot->iCount, 0, iItemCount);

	// update component counts to reflect item count
	for (i = 0; i < eaSize(&pSlot->eaComponents); i++)
	{
		if (!pSlot->eaComponents[i])
			return;
		pSlot->eaComponents[i]->iMaxNum	= pSlot->eaComponents[i]->iRecipeCount * pSlot->iCount;
	}
}

static ExperimentData *ExperimentCreateData(void)
{
	ExperimentData *pExperimentData = StructCreate(parse_ExperimentData);
	int i;

	for (i = 0; i < eaSize(&s_eaExperimentSlotList); i++)
	{
		ExperimentSlot *pSlot = s_eaExperimentSlotList[i];
		ExperimentEntry *pExperimentEntry = NULL;

		if (pSlot->iCount <= 0)
			continue;

		pExperimentEntry = StructCreate(parse_ExperimentEntry);
		pExperimentEntry->count = pSlot->iCount;
		pExperimentEntry->SrcBagId = pSlot->iSrcBagId;
		pExperimentEntry->SrcSlot = pSlot->iSrcBagSlot;
		eaPush(&pExperimentData->ppEntry, pExperimentEntry);
	}

	return pExperimentData;
}

static float ExperimentGetRewardIndex(int iEPVal)
{
	float fRewardIndex = 0;
	int i, iCutoff;

	for (i = 0; i < eaiSize(&s_eaiExperimentRanges); i++)
	{
		if (iEPVal < s_eaiExperimentRanges[i])
		{
			iCutoff = (i == 0) ? 0 : s_eaiExperimentRanges[i - 1];
			fRewardIndex += ((iEPVal - iCutoff) / (s_eaiExperimentRanges[i] - iCutoff));
			break;
		}
		else
			fRewardIndex++;
	}

	return fRewardIndex;
}

bool s_bClearList = false;

// Experiments all of the items in the experiment list
static void ExperimentExecute(void)
{
	Entity *pEnt = entActivePlayerPtr();

	if (pEnt && pEnt->pPlayer &&
		pEnt->pPlayer->InteractStatus.bInteracting &&
		pEnt->pPlayer->pInteractInfo &&
		pEnt->pPlayer->pInteractInfo->bCrafting &&
		pEnt->pPlayer->pInteractInfo->eCraftingTable != kSkillType_None &&
		(pEnt->pPlayer->pInteractInfo->eCraftingTable & pEnt->pPlayer->SkillType))
	{
		ExperimentData *pExperimentData = ExperimentCreateData();

		// send experiment command to server
		ServerCmd_Experiment(pExperimentData);

		// cleanup
		StructDestroy(parse_ExperimentData, pExperimentData);
		s_bClearList = true;
	}
}

/************
* EXPRESSIONS
************/
// Returns whether the user can execute the experimentation; used to gray out the "Experiment" button
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CanExperiment");
bool ExperimentCanExecute(void)
{
	return eaSize(&s_eaExperimentSlotList) > 0;
}

// Get a list of items that this player is going to experiment on.
AUTO_EXPR_FUNC(UIGen);
void ExperimentGenGetList(SA_PARAM_NN_VALID UIGen *pGen)
{
	int i;
	ExperimentSlot *pRemove;

	if (s_bClearList)
		ExperimentClearList();

	while (pRemove = eaPop(&s_eaExperimentSlotListRemoveQueue))
	{
		for (i = eaSize(&s_eaExperimentSlotList)-1; i >= 0; i--)
		{
			if (pRemove == s_eaExperimentSlotList[i])
			{
				eaRemove(&s_eaExperimentSlotList, i);
				StructDestroy(parse_ExperimentSlot, pRemove);
			}
		}
	}

	ui_GenSetList(pGen, &s_eaExperimentSlotList, parse_ExperimentSlot);
}

// This will return true if the item in the given entity's specified bag slot is being experimented on, 
// false otherwise.
bool ExperimentIsItemInListByBag(Item* pItem, int iBagIdx, int iSlotIdx, GameAccountDataExtract *pExtract)
{
	// confirm that this is a valid bag id
	if (pItem && iBagIdx >= 0)
	{
		if (s_eaExperimentSlotList) 
		{
			int i;

			// check to see if the experimentation list is already taking something from this slot
			for (i = 0; i < eaSize(&s_eaExperimentSlotList); i++) 
			{
				ExperimentSlot* pCurrSlot = s_eaExperimentSlotList[i];

				if (pCurrSlot && pCurrSlot->iSrcBagId == iBagIdx && pCurrSlot->iSrcBagSlot == iSlotIdx)
					return true;
			}
		}
	}

	return false;
}

// This will return true if the item in the given entity's specified bag slot is being experimented on, 
// false otherwise.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemIsInExperimentByIndex");
bool ExperimentIsItemInListByBagIndex(SA_PARAM_NN_VALID Entity *pEnt, int iBagIdx, int iSlotIdx)
{
	if (iBagIdx >= 0)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		Item* pItem = inv_GetItemFromBag(pEnt, iBagIdx, iSlotIdx, pExtract);
		return ExperimentIsItemInListByBag(pItem, iBagIdx, iSlotIdx, pExtract);
	}
	return false;
}

// This will return true if the item in the given entity's specified bag slot is being experimented on, 
// false otherwise.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemIsInExperiment");
bool ExperimentIsItemInList(SA_PARAM_OP_VALID Entity* pEnt, SA_PARAM_NN_STR const char* pBagName, int iSlot)
{
	int iBagIdx;

	if (!pEnt)
		return false;

	iBagIdx = StaticDefineIntGetInt(InvBagIDsEnum, pBagName);
	return ExperimentIsItemInListByBagIndex(pEnt, iBagIdx, iSlot);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemIsExperimentable");
bool ExperimentIsItemExperimentable(S32 iBagIndex, S32 iSlotIndex)
{
	Entity *pEnt = entActivePlayerPtr();
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	InventorySlot *pInvSlot = inv_ent_GetSlotPtr(pEnt, iBagIndex, iSlotIndex, pExtract);
	Item *pItem = pInvSlot ? pInvSlot->pItem : NULL;
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	ItemCraftingComponent **eaComponents = NULL;
	S32 iItemVal;
	bool bCanExperiment = false;

	if (!pItem || (pItemDef && (pItemDef->flags & kItemDefFlag_Enigma)) || (pItemDef && (pItemDef->flags & kItemDefFlag_Fused)))
		return false;
	if (pItemDef && pItemDef->kSkillType != kSkillType_None && !(pItemDef->kSkillType & pEnt->pPlayer->SkillType))
		return false;

	iItemVal = item_GetEPValue(PARTITION_CLIENT, pEnt, pItem);
	item_GetDeconstructionComponents(pItem, &eaComponents);
	bCanExperiment = (eaSize(&eaComponents) > 0 || iItemVal > 0);
	eaDestroyStruct(&eaComponents, parse_ItemCraftingComponent);

	return bCanExperiment;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("HasExperimentableItems");
bool ExperimentHasExperimentableItems(void)
{
	Entity *pEnt = entActivePlayerPtr();
	GameAccountDataExtract *pExtract;
	U32 i, j;

	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

	for (i = 0; i < ARRAY_SIZE_CHECKED(s_aExperimentableBagNames); i++)
	{
		int iPlayerBagID = StaticDefineIntGetInt(InvBagIDsEnum, s_aExperimentableBagNames[i]);
		InventoryBag *pInvBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), iPlayerBagID, pExtract);
		InventorySlot *pInvSlot = NULL;

		if (pEnt && pInvBag && !inv_ent_BagEmpty(pEnt, iPlayerBagID, pExtract)) 
		{
			// iterate through each of the bag's slots
			U32 iNumSlots = inv_ent_GetMaxSlots(pEnt, iPlayerBagID, pExtract);
			for (j = 0; j < iNumSlots; j++) 
			{
				if (ExperimentIsItemExperimentable(iPlayerBagID, j))
				{
					return true;
				}
			}
		}
	}

	return false;
}

// Add an item to the list of items to be experimented on, returns false if this item cannot be added
AUTO_EXPR_FUNC(UIGen);
bool ExperimentAddItemByBagIndex(S32 iBagIndex, S32 iSlotIndex, S32 iCount, S32 iInsertAt)
{
	Entity *pEnt = entActivePlayerPtr();
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	InventorySlot *pInvSlot = inv_ent_GetSlotPtr(pEnt, iBagIndex, iSlotIndex, pExtract);
	ExperimentSlot *pExperimentSlot;
	S32 i, iSize = eaSize(&s_eaExperimentSlotList);

	if (ExperimentIsItemExperimentable(iBagIndex, iSlotIndex))
	{
		// If the item is already in the experiment list, just add the new quantity
		for (i = 0; i < iSize; i++)
		{
			ExperimentSlot *pCurrSlot = s_eaExperimentSlotList[i];
			if (s_eaExperimentSlotList && pCurrSlot->iSrcBagId == iBagIndex && pCurrSlot->iSrcBagSlot == iSlotIndex)
			{
				// It is in the list, so add the new amount and make sure you don't go over
				if (pCurrSlot->iCount + iCount <= pInvSlot->pItem->count)
					pCurrSlot->iCount += iCount;
				return true;
			}
		}
		if (s_eaExperimentSlotList)
		{
			// Not in bag, so make a new slot and put it in. 
			if (iInsertAt == -1)
			{
				// Fill the existing empty slot and push a new empty onto the end
				pExperimentSlot = s_eaExperimentSlotList[iSize - 1];
				eaPush(&s_eaExperimentSlotList, (ExperimentSlot*) StructCreate(parse_ExperimentSlot));
			}
			else
			{
				// Create a new slot and insert it into the desired position
				pExperimentSlot = (ExperimentSlot*) StructCreate(parse_ExperimentSlot);
				eaInsert(&s_eaExperimentSlotList, pExperimentSlot, iInsertAt);
			}
			pExperimentSlot->iCount = iCount;
			pExperimentSlot->iSrcBagId = iBagIndex;
			pExperimentSlot->iSrcBagSlot = iSlotIndex;

			// fetch ranges from the server
			ServerCmd_ExperimentGetRanges();

			return true;
		}
	}

	return false;
}

// Add an item to the list of items to be experimented on, returns false if this item cannot be added
AUTO_EXPR_FUNC(UIGen);
bool ExperimentAddItem(SA_PARAM_NN_VALID const char* pchBagName, S32 iSlotIndex, S32 iCount, S32 iInsertAt)
{
	S32 iBagIndex = StaticDefineIntGetInt(InvBagIDsEnum, pchBagName);
	return ExperimentAddItemByBagIndex(iBagIndex, iSlotIndex, iCount, iInsertAt);
}

// Removes an item from the list of items to be experimented on
AUTO_EXPR_FUNC(UIGen);
void ExperimentRemoveItem(S32 iSlotIndex, S32 iCount)
{
	S32 iSize = eaSize(&s_eaExperimentSlotList);
	if (s_eaExperimentSlotList && iSlotIndex < iSize - 1)
	{
		ExperimentSlot *pCurrSlot = s_eaExperimentSlotList[iSlotIndex];
		if (iCount == -1 || iCount >= pCurrSlot->iCount)
		{
			eaPush(&s_eaExperimentSlotListRemoveQueue, pCurrSlot);
		}
		else
			pCurrSlot->iCount -= iCount;
	}
}

// Clears the list of items to be experimented on. 
AUTO_EXPR_FUNC(UIGen);
void ExperimentClearList(void)
{
	ExperimentSlot *pEmptySlot;

	// destroy the slots
	eaClearStruct(&s_eaExperimentSlotList, parse_ExperimentSlot);
	eaClearStruct(&s_eaExperimentSlotListRemoveQueue, parse_ExperimentSlot);

	// add a new empty slot
	pEmptySlot = (ExperimentSlot*) StructCreate(parse_ExperimentSlot);
	eaPush(&s_eaExperimentSlotList, pEmptySlot);

	// reset the calculated reward index value
	s_fTotalRewardIdx = 0;
	s_bClearList = false;
}

void experiment_GetItemsFromBag(SA_PARAM_OP_VALID TradeSlotLite*** s_pInvSlotList, const char* pchBagName, GameAccountDataExtract *pExtract)
{
	// get the bag for this offset and confirm it's valid
	int PlayerBagID = StaticDefineIntGetInt(InvBagIDsEnum, pchBagName);
	Entity *pEnt = entActivePlayerPtr();
	InventoryBag *pInvBag;
	InventorySlot *pInvSlot = NULL;
	Item *pItem;

	pInvBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), PlayerBagID, pExtract);

	if (pEnt && pInvBag && !inv_ent_BagEmpty(pEnt, PlayerBagID,pExtract)) 
	{
		// iterate through each of the bag's slots
		int NumSlots = inv_ent_GetMaxSlots(pEnt, PlayerBagID, pExtract);
		int i;
		for (i = 0; i < NumSlots; i++) 
		{
			pInvSlot = inv_ent_GetSlotPtr(pEnt, PlayerBagID, i, pExtract);

			if (pInvSlot 
				&& pInvSlot->pItem
				&& !ExperimentIsItemInListByBagIndex(pEnt, PlayerBagID, i)
				&& ExperimentIsItemExperimentable(PlayerBagID, i))
			{
				// add a new trade slot to the list for each item in the bag
				TradeSlotLite* pSlot = StructCreate(parse_TradeSlotLite);

				pItem = pInvSlot->pItem;
				pSlot->pItem = pItem;
				pSlot->SrcBagId = PlayerBagID;
				pSlot->SrcSlot = i;
				eaPush(s_pInvSlotList, pSlot);	// empty slot for dropping new items in
			}
		}
	}
}

// Get a list of all items in the player's bag
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetExperimentInventoryList");
void experiment_GetExperimentInventoryList(SA_PARAM_NN_VALID UIGen *pGen)
{
	static TradeSlotLite **s_pInvSlotList;
	U32 i;
	GameAccountDataExtract *pExtract = NULL; // Don't have an entity to get this from

	eaDestroyStruct(&s_pInvSlotList, parse_TradeSlotLite);

	for (i = 0; i < ARRAY_SIZE_CHECKED(s_aExperimentableBagNames); i++)
		experiment_GetItemsFromBag(&s_pInvSlotList, s_aExperimentableBagNames[i], pExtract);

	ui_GenSetList(pGen, &s_pInvSlotList, parse_TradeSlotLite);
}

// Gets total value of all items in the experiment list
AUTO_EXPR_FUNC(UIGen);
float ExperimentGetTotalRewardIndex(void)
{
	int i, iTotal = 0;
	for (i = 0; i < eaSize(&s_eaExperimentSlotList); i++)
	{
		ExperimentSlot *pSlot = s_eaExperimentSlotList[i];

		if (!pSlot)
			continue;

		iTotal += (pSlot->iEPVal * pSlot->iCount);
	}
	s_fTotalRewardIdx = ExperimentGetRewardIndex(iTotal);
	return s_fTotalRewardIdx;
}

AUTO_EXPR_FUNC(UIGen);
int ExperimentGetTotalEP(void)
{
	int i, iTotalEP = 0;
	for (i = 0; i < eaSize(&s_eaExperimentSlotList); i++)
	{
		ExperimentSlot *pSlot = s_eaExperimentSlotList[i];

		if (!pSlot)
			continue;

		iTotalEP += (pSlot->iEPVal * pSlot->iCount);
	}
	return iTotalEP;
}

AUTO_EXPR_FUNC(UIGen);
int ExperimentGetNumRewardEntries(void)
{
	return eaiSize(&s_eaiExperimentRanges);
}

AUTO_EXPR_FUNC(UIGen);
int ExperimentGetEPRangeBoundary(int iRangeIndex)
{
	if (iRangeIndex < 0 || iRangeIndex >= eaiSize(&s_eaiExperimentRanges))
		return -1;
	else
		return s_eaiExperimentRanges[iRangeIndex];
}

// Gets the list of preview components for the item in the specified slot and sets it on the gen
AUTO_EXPR_FUNC(UIGen);
void ExperimentGenGetComponentsList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID ExperimentSlot *pSlot)
{
	ui_GenSetList(pGen, pSlot ? &pSlot->eaComponents : NULL, parse_ComponentSlot);
}

// Gets the list of preview components for all experimented items and sets it on the gen
AUTO_EXPR_FUNC(UIGen);
void ExperimentGenGetTotalComponentsList(SA_PARAM_NN_VALID UIGen *pGen)
{
	ExperimentUpdateTotal();
	ui_GenSetList(pGen, &s_eaExperimentComponentPreview, parse_ComponentSlot);
}

// Sets the item pointer on the specified slot
AUTO_EXPR_FUNC(UIGen);
bool ExperimentFixupSlot(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_NN_VALID ExperimentSlot *pSlot)
{
	Item *pItemPrev = pSlot->pItem;
	int iItemCount;
	GameAccountDataExtract *pExtract;

	if (!s_eaExperimentSlotList)
		return true;

	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

	// fixup the item pointer
	if (pSlot == s_eaExperimentSlotList[eaSize(&s_eaExperimentSlotList) - 1])
		pSlot->pItem = NULL;
	else
		pSlot->pItem = inv_GetItemFromBag(pEnt, pSlot->iSrcBagId, pSlot->iSrcBagSlot, pExtract);

	// remove rows with items that were removed/deleted from the bag that aren't at the end of the list
	if (!pSlot->pItem && pSlot != s_eaExperimentSlotList[eaSize(&s_eaExperimentSlotList) - 1])
	{
		eaFindAndRemove(&s_eaExperimentSlotList, pSlot);
		StructDestroy(parse_ExperimentSlot, pSlot);
		return false;
	}

	// ensure slot count doesn't exceed inventory count
	iItemCount = inv_ent_GetSlotItemCount(pEnt, pSlot->iSrcBagId, pSlot->iSrcBagSlot,pExtract);
	pSlot->iCount = CLAMP(pSlot->iCount, 0, iItemCount);

	// update item values
	if (pItemPrev != pSlot->pItem)
	{
		pSlot->iEPVal = pSlot->pItem ? item_GetEPValue(PARTITION_CLIENT, pEnt, pSlot->pItem) : 0;
		ExperimentSlotUpdateComponents(pEnt, pSlot, pExtract);
	}
	ExperimentSlotUpdateCounts(pEnt, pSlot, pExtract);
	pSlot->fRewardIndex = ExperimentGetRewardIndex(pSlot->iEPVal * pSlot->iCount);

	return true;
}

/******
* TIMER
******/
AUTO_EXPR_FUNC(UIGen);
void ExperimentStartTimer(void)
{
	s_iTimerIndex = timerAlloc();
	timerStart(s_iTimerIndex);
}

AUTO_EXPR_FUNC(UIGen);
void ExperimentEndTimer(void)
{
	if (!!s_iTimerIndex)
		timerFree(s_iTimerIndex);
	s_iTimerIndex = 0;
}

AUTO_EXPR_FUNC(UIGen);
bool ExperimentShowTimer(void)
{
	if (!!s_iTimerIndex && timerElapsed(s_iTimerIndex) > (ITEM_EXPERIMENT_DELAY_SECS + ITEM_CRAFT_CLIENT_DELAY_SECS))
	{
		ExperimentEndTimer();
		ExperimentExecute();
	}
	return !!s_iTimerIndex;
}

AUTO_EXPR_FUNC(UIGen);
int ExperimentGetTimeElapsedMs(void)
{
	return MIN(!!s_iTimerIndex ? timerElapsed(s_iTimerIndex) * 1000 : 0, ITEM_EXPERIMENT_DELAY_SECS * 1000);
}

AUTO_EXPR_FUNC(UIGen);
int ExperimentGetTotalTimeMs(void)
{
	return (ITEM_EXPERIMENT_DELAY_SECS * 1000);
}

/********
* PREVIEW
********/
// Receives the experimentation ranges from the server
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE;
void ExperimentUpdateRanges(ExperimentRanges *pRanges)
{
	eaiCopy(&s_eaiExperimentRanges, &pRanges->eaiRanges);
	eaiQSortG(s_eaiExperimentRanges, intCmp);
}

#include "ExperimentUI_c_ast.c"
