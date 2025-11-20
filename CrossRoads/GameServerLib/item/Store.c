/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Store.h"

#include "contact_common.h"
#include "Entity.h"
#include "EntitySavedData.h"
#include "entCritter.h"
#include "gslEventSend.h"
#include "GameAccountData\GameAccountData.h"
#include "GameAccountDataCommon.h"
#include "GameStringFormat.h"
#include "gslContact.h"
#include "gslEntity.h"
#include "LoggedTransactions.h"
#include "inventoryCommon.h"
#include "itemCommon.h"
#include "rewardCommon.h"
#include "logging.h"
#include "mission_common.h"
#include "ServerLib.h"
#include "sharedmemory.h"
#include "storeCommon.h"
#include "Expression.h"
#include "Player.h"
#include "rand.h"
#include "Reward.h"
#include "StringCache.h"
#include "EntityLib.h"
#include "SavedPetCommon.h"
#include "gslSavedPet.h"
#include "NotifyCommon.h"
#include "GamePermissionsCommon.h"
#include "MicroTransactions.h"
#include "ExpressionPrivate.h"
#include "gslPartition.h"
#include "Guild.h"
#include "gslGroupProject.h"
#include "gslActivity.h"
#include "AccountProxyCommon.h"

#include "AutoGen/entity_h_ast.h"
#include "AutoGen/storeCommon_h_ast.h"
#include "AutoGen/itemCommon_h_ast.h"
#include "AutoGen/contact_common_h_ast.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/GameServerLib_autogen_remotefuncs.h"
#include "AutoGen/AppServerLib_autogen_remotefuncs.h"

#define STORE_REQUIRES_MISSION_MSG "Store.RequiresMission"
#define STORE_REQUIRES_PERK_MSG "Store.RequiresPerk"
#define STORE_RECIPE_ALREADY_KNOWN_MSG "Store.RecipeAlreadyKnown"
#define STORE_COSTUME_ALREADY_UNLOCKED_MSG "Store.CostumeAlreadyUnlocked"
#define STORE_REQUIREMENTS_NOT_MET_MSG "Store.RequirementsNotMet"
#define STORE_INSUFFICIENT_FUNDS_MSG "Store.InsufficientFunds"
#define STORE_NO_SPACE_IN_BAGS_MSG "Store.NoSpaceInBags"
#define STORE_MAX_PUPPETS "Store.MaxPuppets"
#define STORE_UNIQUE_PET "Store.UniquePet"
#define STORE_INVALID_ALLEGIANCE "Store.InvalidAllegiance"
#define STORE_PET_ACQUIRE_LIMIT "Store.PetAcquireLimit"
#define STORE_UNIQUE_ITEM "Store.UniqueItem"
#define STORE_UNAVAILABLE_ITEM "Store.ItemUnavailable"

#define STORE_DEFAULT_RESEARCH_TIME 4

typedef enum StoreResearchState
{
	kStoreResearchState_None,
	kStoreResearchState_Start,
	kStoreResearchState_Finish,
} StoreResearchState;

typedef struct StoreSellItemCBData{
	ContainerID uEntID;
	BuyBackItemInfo* pItemInfo;
} StoreSellItemCBData;


// Is a specific Item in a store currently available to purchase based
//  on its time-based criteria (Active Event and ShowExpr)
static bool store_ItemIsAvailableNow(Entity *pPlayerEnt, StoreItemDef *pStoreItemDef)
{
	if (pStoreItemDef)
	{
		if (pStoreItemDef->pchActivityName && pStoreItemDef->pchActivityName[0] && !gslActivity_IsActive(pStoreItemDef->pchActivityName))
		{
			return(false);
		}

		if (pStoreItemDef->pExprShowItem)
		{
			MultiVal mvReturn = {0};

			store_ShowItemContextSetup(pPlayerEnt);
			exprContextSetSilentErrors(store_GetShowItemContext(), false);
			exprEvaluate(pStoreItemDef->pExprShowItem, store_GetShowItemContext(), &mvReturn);

			if(!MultiValGetInt(&mvReturn, NULL))
			{
				return(false);
			}
		}
	}
	return(true);
}

static int store_GetPlayerItemCount(Entity* pEnt, const char* pchItemDefName)
{
	static InvBagIDs* s_peExcludeBags = NULL;
	if (!s_peExcludeBags)
	{
		eaiPush(&s_peExcludeBags, InvBagIDs_Buyback);
	}
	return inv_ent_AllBagsCountItemsEx(pEnt, pchItemDefName, s_peExcludeBags);
}

static bool store_BuyItem_CanFitIntoGeneralInventory(Entity* pEnt, 
													 ItemDef* pItemDef, 
													 StoreItemInfo* pStoreItemInfo, 
													 S32 iCount,
													 GameAccountDataExtract *pExtract)
{
	InventoryBag* pInvBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), InvBagIDs_Inventory, pExtract);
	ItemDef* pActualItemDef;
	if (!pInvBag || !pItemDef || !pStoreItemInfo)
	{
		return false;
	}
	if (pItemDef->eType == kItemType_ItemValue) {
		pActualItemDef = pItemDef->pCraft ? GET_REF(pItemDef->pCraft->hItemResult) : NULL;
		if (!pActualItemDef) {
			return false;
		}
	} else {
		pActualItemDef = pItemDef;
	}
	if (pActualItemDef->eType == kItemType_Numeric)
	{
		return true;
	}
	if (!inv_CanItemDefFitInBag(pEnt, pInvBag, pActualItemDef, iCount))
	{
		InventoryBag** eaBags = NULL;
		InventoryBag* pPlayerBagsBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), InvBagIDs_PlayerBags, pExtract);
		PetDef *pPetDef = GET_REF(pActualItemDef->hPetDef);
		S32 i, j, iRemainingStackCount = iCount;

		// If this is a auto-equip pet item, add the overflow bag to the list of bags to check
		if (pPetDef && (pActualItemDef->flags & kItemDefFlag_EquipOnPickup)) {
			InventoryBag* pOverflowBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), InvBagIDs_Overflow, pExtract);
			if (pOverflowBag) {
				eaPush(&eaBags, pOverflowBag);
			}
		}

		// Add the inventory bag and player bags to the list of bags to check
		if (pPlayerBagsBag)
		{
			for (i = 0; i < invbag_maxslots(pEnt, pPlayerBagsBag); i++)
			{
				InventoryBag* pBag = inv_PlayerBagFromSlotIdx(pEnt, i);
				if (pBag && GamePermissions_trh_CanAccessBag(CONTAINER_NOCONST(Entity, pEnt), pBag->BagID, pExtract))
				{
					eaPush(&eaBags, pBag);
				}
			}
		}
		// If the player doesn't have any 
		if (!eaSize(&eaBags))
		{
			return false;
		}
		eaPush(&eaBags, pInvBag);

		// Check all bags in eaBags for room
		for (i = 0; i < eaSize(&eaBags); i++) 
		{
			InventoryBag* pBag = eaBags[i];
			int iMaxSlots = invbag_maxslots(pEnt, pBag);
			if (iMaxSlots < 0)
			{
				// If this is a bag with infinite slots, then stop here
				iRemainingStackCount = 0;
				break;
			}
			for (j = 0; j < iMaxSlots && iRemainingStackCount > 0; j++)
			{
				InventorySlot* pSlot = inv_GetSlotPtr(pBag, j);
				if (ISNULL(pSlot))
				{
					iRemainingStackCount -= pActualItemDef->iStackLimit;
				}
				else if (!pSlot->pItem || 
					inv_MatchingItemInSlot(pBag, j, pActualItemDef->pchName))
				{
					S32 iSlotCount = inv_bag_GetSlotItemCount(pBag, j);
					S32 iFreeCount = pActualItemDef->iStackLimit - iSlotCount;
					S32 iNumToAdd = MIN(iRemainingStackCount, iFreeCount);

					if (iNumToAdd > 0)
					{
						iRemainingStackCount -= iNumToAdd;
					}
				}
			}
		}
		eaDestroy(&eaBags);
		return iRemainingStackCount <= 0;
	}
	return true;
}

static StoreCanBuyError store_GetBuyItemError(SA_PARAM_NN_VALID Entity *pPlayerEnt, 
											  SA_PARAM_OP_VALID StoreDef *pStoreDef, 
											  SA_PARAM_OP_VALID StoreItemDef *pStoreItem, 
											  SA_PARAM_OP_VALID StoreItemInfo *pStoreItemInfo, 
											  SA_PARAM_OP_VALID ItemDef *pItemDef,
											  S32 iCount,
											  bool bSkipBagFullCheck,
											  char** estrDisplayText,
											  MicroTransactionDef **ppRequiredTransaction,
											  GameAccountDataExtract *pExtract)
{
	MissionDef *pMissionDef = pStoreItem ? GET_REF(pStoreItem->hReqMission) : NULL;
	S32 i, j;
	const char *pchUsageRestrictionKey = NULL;
	ItemDef *pNumericItemDef = NULL;
	
	if (!pStoreDef || !pItemDef || !pStoreItemInfo){
		return kStoreCanBuyError_Unknown;
	}
	
	if (estrDisplayText)
		estrClear(estrDisplayText);

	if((pItemDef->flags & kItemDefFlag_Unique) && inv_ent_HasUniqueItem(pPlayerEnt, pItemDef->pchName))
	{
		if (estrDisplayText) {
			entFormatGameMessageKey(pPlayerEnt, estrDisplayText, STORE_UNIQUE_ITEM, STRFMT_ITEMDEF(pItemDef), STRFMT_END);
		}
		return kStoreCanBuyError_ItemUnique;
	}

	if(pItemDef->flags & kItemDefFlag_EquipOnPickup)
	{
		AddSavedPetErrorType eAddSavedPetError = 0;
		PetDef *pPetDef = GET_REF(pItemDef->hPetDef);

		if(pPetDef && !Entity_CanAddSavedPet(pPlayerEnt,pPetDef,0,pItemDef->bMakeAsPuppet,pExtract,&eAddSavedPetError))
		{
			StoreCanBuyError eError = kStoreCanBuyError_CannotEquipPet;
			switch (eAddSavedPetError)
			{
				xcase kAddSavedPetErrorType_MaxPuppets:
				{
					if (estrDisplayText) {
						entFormatGameMessageKey(pPlayerEnt, estrDisplayText, STORE_MAX_PUPPETS, STRFMT_END);
					}
					eError = kStoreCanBuyError_MaxPuppets;
				}
				xcase kAddSavedPetErrorType_InvalidAllegiance:
				{
					if (estrDisplayText) {
						entFormatGameMessageKey(pPlayerEnt, estrDisplayText, STORE_INVALID_ALLEGIANCE, STRFMT_END);
					}
					eError = kStoreCanBuyError_InvalidAllegiance;
				}
				xcase kAddSavedPetErrorType_UniqueCheck:
				{
					if (estrDisplayText) {
						entFormatGameMessageKey(pPlayerEnt, estrDisplayText, STORE_UNIQUE_PET, STRFMT_END);
					}
					eError = kStoreCanBuyError_ItemUnique;
				}
				xcase kAddSavedPetErrorType_AcquireLimit:
				{
					if (estrDisplayText) {
						entFormatGameMessageKey(pPlayerEnt, estrDisplayText, STORE_PET_ACQUIRE_LIMIT, STRFMT_END);
					}
					eError = kStoreCanBuyError_PetAcquireLimit;
				}
			}
			return eError;
		}
	}
	else
	{
		PetDef *pPetDef = GET_REF(pItemDef->hPetDef);
		if (pPetDef && Entity_CheckAcquireLimit(pPlayerEnt, pPetDef, 0))
		{
			if (estrDisplayText) {
				entFormatGameMessageKey(pPlayerEnt, estrDisplayText, STORE_PET_ACQUIRE_LIMIT, STRFMT_END);
			}
			return kStoreCanBuyError_PetAcquireLimit;
		}
	}

	// Check Items that require Missions
	if (pMissionDef) {
		if (estrDisplayText) {
			if (pMissionDef->missionType == MissionType_Perk) {
				entFormatGameMessageKey(pPlayerEnt, estrDisplayText, STORE_REQUIRES_PERK_MSG, STRFMT_STRING("MissionName", entTranslateDisplayMessage(pPlayerEnt, pMissionDef->displayNameMsg)), STRFMT_END);
			}else{
				entFormatGameMessageKey(pPlayerEnt, estrDisplayText, STORE_REQUIRES_MISSION_MSG, STRFMT_STRING("MissionName", entTranslateDisplayMessage(pPlayerEnt, pMissionDef->displayNameMsg)), STRFMT_END);
			}
		}
		
		if (!mission_GetCompletedMissionByDef(mission_GetInfoFromPlayer(pPlayerEnt), pMissionDef)) {
			return kStoreCanBuyError_RequiredMission;
		}
	}
	
	// If the item has a CanBuy expression, run it
	if (pStoreItem && pStoreItem->pExprCanBuy) {
		MultiVal mvReturn = {0};

		exprContextSetSilentErrors(store_GetBuyContext(), false);
		store_BuyContextSetup(pPlayerEnt, pItemDef);
		exprEvaluate(pStoreItem->pExprCanBuy, store_GetBuyContext(), &mvReturn);

		if (ppRequiredTransaction) {
			*ppRequiredTransaction = microtrans_FindDefFromPermissionExpr(pStoreItem->pExprCanBuy);
		}

		if (!MultiValGetInt(&mvReturn, NULL)) {
			if (estrDisplayText) {
				if (GET_REF(pStoreItem->cantBuyMessage.hMessage)) {
					entFormatGameDisplayMessage(pPlayerEnt, estrDisplayText, &pStoreItem->cantBuyMessage, STRFMT_END);
				} else {
					entFormatGameMessageKey(pPlayerEnt, estrDisplayText, STORE_REQUIREMENTS_NOT_MET_MSG, STRFMT_PLAYER(pPlayerEnt), STRFMT_ITEMDEF(pItemDef), STRFMT_END);
				}
			}
			return kStoreCanBuyError_CanBuyRequirements;
		}
	}

	// If a required numeric value has been specified, then check it against the player's numeric
	pNumericItemDef = pStoreItem ? GET_REF(pStoreItem->hRequiredNumeric) : NULL;
	if (pNumericItemDef)
	{
		S32 playerNumericValue;
		playerNumericValue = inv_GetNumericItemValue(pPlayerEnt, pNumericItemDef->pchName);
		if (playerNumericValue < pStoreItem->requiredNumericValue)
		{
			return kStoreCanBuyError_RequiredNumeric;
		}
	}

	// Check if we can afford the item
	for (i = 0; i < eaSize(&pStoreItemInfo->eaCostInfo); i++) {
		if (pStoreItemInfo->eaCostInfo[i]->bTooExpensive) {
			ItemDef* pCostItemDef = GET_REF(pStoreItemInfo->eaCostInfo[i]->hItemDef);
			if(pCostItemDef && estrDisplayText) { 
				entFormatGameMessageKey(pPlayerEnt, estrDisplayText, STORE_INSUFFICIENT_FUNDS_MSG, STRFMT_DISPLAYMESSAGE("Currency", pCostItemDef->displayNameMsg), STRFMT_END);
			}
			return kStoreCanBuyError_CostRequirement;
		}
	}
	
	// For Auto-learn Stores, check that the player is eligible to learn the recipe
	if (pStoreDef->eContents == Store_Recipes){
		// If this is not a recipe, it's invalid data
		if (!(pItemDef->eType == kItemType_ItemRecipe || pItemDef->eType == kItemType_ItemPowerRecipe)) {
			return kStoreCanBuyError_InvalidRecipe;
		}
		// Make sure the player doesn't already know the recipe
		else if (inv_bag_GetItemByName((InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pPlayerEnt), InvBagIDs_Recipe, pExtract), pItemDef->pchName)) {
			if (estrDisplayText) {
				estrClear(estrDisplayText);
				entFormatGameMessageKey(pPlayerEnt, estrDisplayText, STORE_RECIPE_ALREADY_KNOWN_MSG, STRFMT_END);
			}
			return kStoreCanBuyError_RecipeAlreadyKnown;
		}
		// Make sure the player meets the requirements for the recipe
		else if (!itemdef_VerifyUsageRestrictions(entGetPartitionIdx(pPlayerEnt), pPlayerEnt, pItemDef, 0, &pchUsageRestrictionKey, -1)) {
			if (estrDisplayText) {
				estrClear(estrDisplayText);
				entFormatGameMessageKey(pPlayerEnt, estrDisplayText, pchUsageRestrictionKey ? pchUsageRestrictionKey : STORE_REQUIREMENTS_NOT_MET_MSG, STRFMT_PLAYER(pPlayerEnt), STRFMT_ITEMDEF(pItemDef), STRFMT_END);
			}
			return kStoreCanBuyError_RecipeRequirements;
		}
	}
	else if (pStoreDef->eContents == Store_Costumes)
	{
		// if this item does not have a costume, it's invalid
		if (pItemDef->eCostumeMode != kCostumeDisplayMode_Unlock || eaSize(&pItemDef->ppCostumes) == 0)
			return kStoreCanBuyError_InvalidCostume;
		// make sure the player meets the requirements to equip the item (though it will not ever actually be equipped)
		else if (!itemdef_VerifyUsageRestrictions(entGetPartitionIdx(pPlayerEnt), pPlayerEnt, pItemDef, 0, &pchUsageRestrictionKey, -1))
		{
			if (estrDisplayText) {
				estrClear(estrDisplayText);
				entFormatGameMessageKey(pPlayerEnt, estrDisplayText, pchUsageRestrictionKey ? pchUsageRestrictionKey : STORE_REQUIREMENTS_NOT_MET_MSG, STRFMT_PLAYER(pPlayerEnt), STRFMT_ITEMDEF(pItemDef), STRFMT_END);
			}
			return kStoreCanBuyError_CostumeRequirements;
		}
		// make sure the player hasn't already unlocked the costumes on the item
		else
		{
			bool bAllUnlocked = true;
			GameAccountData *pData = entity_GetGameAccount(pPlayerEnt);
			char *estrItem = NULL;

			for (i = eaSize(&pItemDef->ppCostumes) - 1; i >= 0 && bAllUnlocked; i--)
			{
				PlayerCostume *pCostume = GET_REF(pItemDef->ppCostumes[i]->hCostumeRef);
				if(!pCostume)
					continue;

				if(pCostume->bAccountWideUnlock) {
					AttribValuePair *pPair = NULL;
					if(!estrItem) {
						estrStackCreate(&estrItem);
					} else {
						estrClear(&estrItem);
					}
					
					MicroTrans_FormItemEstr(&estrItem, GetShortProductName(), kMicroItemType_PlayerCostume, pCostume->pcName, 1);
					pPair = eaIndexedGetUsingString(&pData->eaCostumeKeys, estrItem);

					if(!pPair) {
						bAllUnlocked = false;
					}
				} else { 
					bool bFound = false;
					for (j = eaSize(&pPlayerEnt->pSaved->costumeData.eaUnlockedCostumeRefs)-1; j >= 0; j--) {
						if (REF_STRING_FROM_HANDLE(pPlayerEnt->pSaved->costumeData.eaUnlockedCostumeRefs[j]->hCostume) == REF_STRING_FROM_HANDLE(pItemDef->ppCostumes[i]->hCostumeRef)) {
							bFound = true;
							break;
						}
					}
					if (!bFound) {
						bAllUnlocked = false;
					}
				}
			}

			estrDestroy(&estrItem);

			if (bAllUnlocked)
			{
				if (estrDisplayText)
				{
					estrClear(estrDisplayText);
					entFormatGameMessageKey(pPlayerEnt, estrDisplayText, STORE_COSTUME_ALREADY_UNLOCKED_MSG, STRFMT_END);
				}
				return kStoreCanBuyError_CostumeAlreadyUnlocked;
			}
		}
	} else if (!bSkipBagFullCheck) {
		bool bBagFull = false;
		InvBagIDs overrideBag = itemAcquireOverride_FromStore(pItemDef);
		
		// Check to see if the item should go into an override bag
		if (overrideBag != InvBagIDs_None)
		{
			InventoryBag* pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pPlayerEnt), overrideBag, pExtract);

			if (pBag)
			{
				if (!inv_CanItemDefFitInBag(pPlayerEnt, pBag, pItemDef, iCount))
				{
					bBagFull = true;
				}
			}
			else
			{
				return kStoreCanBuyError_BagMissing;
			}
		}
		else
		{
			// If there isn't an override bag, check the player's inventory and player bags for room
			if (!store_BuyItem_CanFitIntoGeneralInventory(pPlayerEnt, pItemDef, pStoreItemInfo, iCount, pExtract))
			{
				bBagFull = true;
			}
		}
		if (bBagFull)
		{
			// If the bag is full, generate an error message
			if (estrDisplayText)
			{
				const char *pchErrorMessage = itemHandling_GetErrorMessage(pItemDef);
				if (!pchErrorMessage)
				{
					pchErrorMessage = STORE_NO_SPACE_IN_BAGS_MSG;
				}
				estrClear(estrDisplayText);
				entFormatGameMessageKey(pPlayerEnt, estrDisplayText, pchErrorMessage, STRFMT_END);
			}
			return kStoreCanBuyError_BagFull;
		}
	}

	// Time-based availability check
	if (!store_ItemIsAvailableNow(pPlayerEnt, pStoreItem))
	{
		if (estrDisplayText) {
			entFormatGameMessageKey(pPlayerEnt, estrDisplayText, STORE_UNAVAILABLE_ITEM, STRFMT_END);
		}
		return kStoreCanBuyError_NotCurrentlyAvailable;
	}
	
	return kStoreCanBuyError_None;
}

static bool store_CanBuyItem(SA_PARAM_NN_VALID Entity *pPlayerEnt, 
							 SA_PARAM_OP_VALID StoreDef *pStoreDef, 
							 SA_PARAM_OP_VALID StoreItemDef *pStoreItem, 
							 SA_PARAM_OP_VALID StoreItemInfo *pStoreItemInfo,
							 SA_PARAM_OP_VALID ItemDef *pItemDef,
							 S32 iCount,
							 bool bSkipBagFullCheck,
							 char** estrDisplayText,
							 MicroTransactionDef **ppRequiredTransaction,
							 GameAccountDataExtract *pExtract)
{
	StoreCanBuyError eError;
	eError = store_GetBuyItemError(pPlayerEnt, pStoreDef, pStoreItem, pStoreItemInfo, pItemDef, iCount, bSkipBagFullCheck, estrDisplayText, ppRequiredTransaction, pExtract);
	return eError == kStoreCanBuyError_None;
}

static bool store_ApplyDiscountToCostInfo(Entity* pEnt, StoreDef* pStoreDef, ItemDef* pItemDef, StoreItemCostInfo* pCostInfo)
{
	ItemDef* pCurrencyDef = GET_REF(pCostInfo->hItemDef);
	F32 fTotalDiscountPercent = 0.0f;
	F32 fDiscountedCost;

	eaClear(&pCostInfo->ppchDiscounts);
    if ( pCostInfo->iOriginalCount != 0 )
    {
        // If we are applying the discounts to a cost info that has already had discounts applied, then revert to the original count first.
        pCostInfo->iCount = pCostInfo->iOriginalCount;
    }
    else
    {
	    pCostInfo->iOriginalCount = pCostInfo->iCount;
    }
	fDiscountedCost = pCostInfo->iCount;

	if (pCurrencyDef) {
		int i;
		for (i = eaSize(&pStoreDef->eaDiscountDefs)-1; i >= 0; i--) {
			StoreDiscountDef* pDiscountDef = pStoreDef->eaDiscountDefs[i];
			ItemDef* pDiscountCostItemDef = GET_REF(pDiscountDef->hDiscountCostItem);

			if (pCurrencyDef == pDiscountCostItemDef &&
				(pDiscountDef->eApplyToItemCategory == kItemCategory_None ||
				 eaiFind(&pItemDef->peCategories, pDiscountDef->eApplyToItemCategory) >= 0))
			{
				bool bValid = true;
				if (pDiscountDef->pExprRequires) {
					MultiVal mv = {0};
					store_BuyContextSetup(pEnt, pItemDef);
					exprEvaluate(pDiscountDef->pExprRequires, store_GetBuyContext(), &mv);
					if (mv.type == MULTI_INT && !QuickGetInt(&mv)) {
						bValid = false;
					}
				}
				if (bValid) {
					fTotalDiscountPercent += pDiscountDef->fDiscountPercent;
					eaPush(&pCostInfo->ppchDiscounts, pDiscountDef->pchName);
				}
			}
		}
	}

	if (eaSize(&pCostInfo->ppchDiscounts) > 0)
	{
		fDiscountedCost *= (1.0f - (fTotalDiscountPercent/100.0f));
		pCostInfo->iCount = (int)ceilf(fDiscountedCost);
		return true;
	}
	return false;
}

static int store_StoreItemCostInfoCmp(const StoreItemCostInfo **pCost1, const StoreItemCostInfo **pCost2)
{
	return (*pCost2)->iCount - (*pCost1)->iCount;
}

static void store_CalculateCurrencyCost(Entity *pEnt, 
										StoreDef *pStoreDef, 
										PersistedStore *pPersistStore,
										StoreItemInfo *pItemInfo)
{
	ItemDef *pItemDef;
	ItemDef *pCurrencyDef = GET_REF(pStoreDef->hCurrency);
	StoreItemCostInfo *pCostInfo = NULL;
	S32 iCurrencyValue;
	int iPartitionIdx;
	
	if (!pEnt || !pStoreDef || !pItemInfo) {
		return;
	}
	
	pItemDef = SAFE_GET_REF(pItemInfo->pOwnedItem, hItem);
	if (!pItemDef) {
		return;
	}
	
	pCurrencyDef = GET_REF(pStoreDef->hCurrency);
	if (!pCurrencyDef) {
		return;
	}

	if (!eaGet(&pStoreDef->inventory, pItemInfo->index)) {
		return;
	}
	
	eaClearStruct(&pItemInfo->eaCostInfo, parse_StoreItemCostInfo);
	
	pCostInfo = StructCreate(parse_StoreItemCostInfo);
	iPartitionIdx = entGetPartitionIdx(pEnt);

	{
		S32 levelToSend = (pItemDef->flags & kItemDefFlag_ScaleWhenBought) ? entity_GetSavedExpLevel(pEnt) : item_GetMinLevel(pItemInfo->pOwnedItem);
		pCostInfo->iCount = item_GetStoreEPValue(iPartitionIdx, pEnt, pItemInfo->pOwnedItem, pStoreDef) * pStoreDef->fBuyMultiplier * -ITEM_BUY_MULTIPLIER;
	}
	SET_HANDLE_FROM_REFERENT(g_hItemDict, pCurrencyDef, pCostInfo->hItemDef);
	
	iCurrencyValue = item_GetDefEPValue(iPartitionIdx, pEnt, pCurrencyDef, pCurrencyDef->iLevel, pCurrencyDef->Quality);
	if (iCurrencyValue) {
		pCostInfo->iCount /= iCurrencyValue;
	}
	
	if (!pStoreDef->bIsPersisted && pStoreDef->inventory[pItemInfo->index]->iCount > 0) {
		pCostInfo->iCount *= pStoreDef->inventory[pItemInfo->index]->iCount;
	}

	store_ApplyDiscountToCostInfo(pEnt, pStoreDef, pItemDef, pCostInfo);
	
	pCostInfo->iAvailableCount = inv_GetNumericItemValue(pEnt, pCurrencyDef->pchName);
	if (pCostInfo->iCount > pCostInfo->iAvailableCount) {
		pCostInfo->bTooExpensive = true;
	}
	eaPush(&pItemInfo->eaCostInfo, pCostInfo);
}

static StoreItemInfo* store_SetupStoreItemInfoEx(Entity* pPlayerEnt, 
												 ContactDef* pContactDef,
												 StoreDef* pStoreDef, 
												 PersistedStore* pPersistStore, 
												 int iItemIdx,
												 GameAccountDataExtract *pExtract)
{
	StoreItemDef *pStoreItemDef = NULL;
	StoreRestockDef *pRestockDef = NULL;
	ItemDef *pItemDef = NULL;
	Item *pItem = NULL;
	ItemDef **ppValueRecipes = NULL;
	ItemDefRef **ppValueRecipeRefs = NULL;
	RewardTable *pRewardTable = NULL;
	StoreItemInfo *pItemInfo;
	PersistedStoreItem* pPersistItem = NULL;
	bool bIsValueRecipe, bSkipBagFullCheck;
	int j;
	ItemDef *pRequiredNumericDef = NULL;
	MicroTransactionDef *pRequiredTransaction = NULL;
	U32 uiSeed;
	S32 iQuantity = 1;
	const char *pchRequirementsMessage;

	if(!pPlayerEnt || !pStoreDef)
		return NULL;

	if (pStoreDef->bIsPersisted) {
		if (!pPersistStore || !(pPersistItem = eaGet(&pPersistStore->eaInventory, iItemIdx))) {
			return NULL;
		} else {
			pRestockDef = PersistedStore_FindRestockDefByName(pStoreDef, pPersistItem->pchRestockDef);
			uiSeed = pPersistItem->uSeed;
		}
	} else {
		pStoreItemDef = pStoreDef ? eaGet(&pStoreDef->inventory, iItemIdx) : NULL;
	}
	pItemDef = pStoreItemDef ? GET_REF(pStoreItemDef->hItem) : NULL;
	pRewardTable = pRestockDef ? GET_REF(pRestockDef->hRewardTable) : NULL;

	if (pRewardTable)
	{
		InventoryBag* pBag = NULL;
		InventoryBag** eaBags = NULL;
		S32 iItemLevel = pRestockDef->iItemLevel ? pRestockDef->iItemLevel : pStoreDef->iItemLevel;
		S32 iRewardIndex = pPersistItem->iRewardIndex;
		reward_GenerateBagsForStore(entGetPartitionIdx(pPlayerEnt), pPlayerEnt, pRewardTable, iItemLevel, &uiSeed, &eaBags);
		for (j = 0; j < eaSize(&eaBags); j++)
		{
			S32 iItemCount = eaSize(&eaBags[j]->ppIndexedInventorySlots);
			if (iRewardIndex >= iItemCount)
			{
				iRewardIndex -= iItemCount;
				continue;
			}
			pBag = eaBags[j];
			break;
		}
		if (pBag)
		{
			NOCONST(InventorySlot)* pSlot = CONTAINER_NOCONST(InventorySlot, eaGet(&pBag->ppIndexedInventorySlots, iRewardIndex));
			if (pSlot)
			{
				pItem = (Item*)pSlot->pItem;
				pSlot->pItem = NULL;
			}
			pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
		}
		eaDestroyStruct(&eaBags, parse_InventoryBag);
	}

	if (!pItemDef) {
		if (pItem) {
			StructDestroy(parse_Item, pItem);
		}
		return NULL;
	}

	bIsValueRecipe = (pItemDef->eType == kItemType_ItemValue);
	if (bIsValueRecipe) {
		eaPush(&ppValueRecipes, pItemDef);
		pItemDef = pItemDef->pCraft?GET_REF(pItemDef->pCraft->hItemResult):NULL;
		if (!pItemDef) {
			if (pItem) {
				StructDestroy(parse_Item, pItem);
			}
			eaDestroy(&ppValueRecipes);
			return NULL;
		}
	} else {
		if (pStoreItemDef && eaSize(&pStoreItemDef->ppOverrideValueRecipes) > 0)
		{
			ppValueRecipeRefs = pStoreItemDef->ppOverrideValueRecipes;
		}
		else
		{
			ppValueRecipeRefs = pItemDef->ppValueRecipes;
		}
		for (j = 0; j < eaSize(&ppValueRecipeRefs); j++)
		{
			ItemDef *pValueRecipe = SAFE_GET_REF(ppValueRecipeRefs[j], hDef);
			if (pValueRecipe)
				eaPush(&ppValueRecipes, pValueRecipe);
		}
	}

	pItemInfo = StructCreate(parse_StoreItemInfo);
	if (pItem)
	{
		pItemInfo->pOwnedItem = pItem;
	}
	else
	{
		pItemInfo->pOwnedItem = item_FromEnt(CONTAINER_NOCONST(Entity, pPlayerEnt),pItemDef->pchName,0,NULL,0);
	}
	if (pStoreItemDef)
	{
		pItemInfo->eStoreCategory = pStoreItemDef->eCategory;
		eaiCopy(&pItemInfo->peHighlightCategory, &pStoreItemDef->peHighlightCategory);
		if (pItemInfo->pOwnedItem)
		{
			pItemInfo->pOwnedItem->bForceBind = pStoreItemDef->bForceBind;
		}
	}
	else if (pRestockDef)
	{
		pItemInfo->eStoreCategory = pRestockDef->eCategory;
	}
	pItemInfo->pchStoreName = pStoreDef->name;
	pItemInfo->index = pPersistItem ? pPersistItem->uID : iItemIdx;
	pItemInfo->uExpireTime = pPersistItem ? pPersistItem->uExpireTime : 0;
	pItemInfo->iCount = pStoreItemDef ? pStoreItemDef->iCount : 1;
	pItemInfo->bIsValueRecipe = eaSize(&ppValueRecipes) > 0;
	pItemInfo->iMayBuyInBulk = pItemDef->iMayBuyInBulk;
	pItemInfo->bIsHeader = false;
	pItemInfo->bForceBind = SAFE_MEMBER(pStoreItemDef, bForceBind);
	pItemInfo->bIsFromPersistedStore = pPersistStore != NULL;
	pItemInfo->pchTranslatedLongDescription = StructAllocString(entTranslateDisplayMessage(pPlayerEnt, pStoreItemDef->longFlavorDesc));
	pItemInfo->pchDisplayTex = pStoreItemDef->pchDisplayTex;

	// Activity-related item. Figure out how long it will be around. Only used on non-persisted stuff
	if (!pItemInfo->bIsFromPersistedStore && pItemInfo->uExpireTime==0)
	{
		// Our expire time is that of our associated event. This is relative to the EventClock
		if (pStoreItemDef->pchActivityName && pStoreItemDef->pchActivityName[0])
		{
			pItemInfo->uExpireTime = gslActivity_EventClockEndingTime(pStoreItemDef->pchActivityName);
		}
	}

	if (SAFE_MEMBER(pStoreItemDef, uResearchTime) > 0) {
		pItemInfo->uResearchTime = pStoreItemDef->uResearchTime;
	} else if (SAFE_MEMBER(pContactDef, bIsResearchStoreCollection) || pStoreDef->bIsResearchStore) {
		pItemInfo->uResearchTime = STORE_DEFAULT_RESEARCH_TIME;
	}

	// If this item is a micro transaction placeholder, then set the micro transaction def.
	if (pStoreItemDef && IS_HANDLE_ACTIVE(pStoreItemDef->hReqMicroTransaction))
	{
		COPY_HANDLE(pItemInfo->hMicroTransaction, pStoreItemDef->hReqMicroTransaction);
	}

	// if there is a required numeric, then fill in the info
	pRequiredNumericDef = pStoreItemDef ? GET_REF(pStoreItemDef->hRequiredNumeric) : NULL;
	if (pRequiredNumericDef)
	{
		pItemInfo->pchRequiredNumericName = pRequiredNumericDef->pchName;
		pItemInfo->iRequiredNumericValue = pStoreItemDef->requiredNumericValue;
		pItemInfo->iRequiredNumericIncr = pStoreItemDef->requiredNumericIncr;
	}

	if(pStoreItemDef && pStoreItemDef->iCount > 0)
	{
		iQuantity = pStoreItemDef->iCount;	
	}

	if (eaSize(&ppValueRecipes) > 0 && !SAFE_MEMBER(pStoreItemDef, bForceUseCurrency)) {
		for (j = 0; j < eaSize(&ppValueRecipes); j++) {
			ItemDef *pValueRecipe = ppValueRecipes[j];
			int k, l;

			if (!pValueRecipe->pCraft)
			{
				eaDestroy(&ppValueRecipes);
				StructDestroy(parse_StoreItemInfo, pItemInfo);
				return NULL;
			}

			for (k = eaSize(&pValueRecipe->pCraft->ppPart)-1; k >= 0; k--) {
				ItemDef *pComponentDef = GET_REF(pValueRecipe->pCraft->ppPart[k]->hItem);
				StoreItemCostInfo *pCostInfo = NULL;

				if (!pComponentDef) {
					eaDestroy(&ppValueRecipes);
					StructDestroy(parse_StoreItemInfo, pItemInfo);
					return NULL;
				}

				// find existing component and aggregate
				for (l = eaSize(&pItemInfo->eaCostInfo) - 1; l >= 0; l--) {
					if (GET_REF(pItemInfo->eaCostInfo[l]->hItemDef) == pComponentDef) {
						pCostInfo = pItemInfo->eaCostInfo[l];
						pCostInfo->iCount += (int)pValueRecipe->pCraft->ppPart[k]->fCount * iQuantity;
						break;
					}
				}

				// add a new component if existing one is not found
				if (l < 0) {
					pCostInfo = StructCreate(parse_StoreItemCostInfo);
					eaPush(&pItemInfo->eaCostInfo, pCostInfo);

					pCostInfo->iCount = (int)pValueRecipe->pCraft->ppPart[k]->fCount * iQuantity;
					SET_HANDLE_FROM_REFERENT(g_hItemDict, pComponentDef, pCostInfo->hItemDef);
				}
			}
		}
		for (j = eaSize(&pItemInfo->eaCostInfo)-1; j >= 0; j--) {

			StoreItemCostInfo* pCostInfo = pItemInfo->eaCostInfo[j];
			ItemDef* pCurrentDef = GET_REF(pCostInfo->hItemDef);

			store_ApplyDiscountToCostInfo(pPlayerEnt, pStoreDef, pItemDef, pCostInfo);

			if (pCurrentDef) {
				if (pCurrentDef->eType == kItemType_Numeric) {
					pCostInfo->iAvailableCount = inv_GetNumericItemValue(pPlayerEnt, pCurrentDef->pchName);
				} else {
					pCostInfo->iAvailableCount = store_GetPlayerItemCount(pPlayerEnt, pCurrentDef->pchName);
				}
			}

			if (pCostInfo->iCount > pCostInfo->iAvailableCount) {
				pCostInfo->bTooExpensive = true;
			}
		}

		// sort by descending count
		eaQSort(pItemInfo->eaCostInfo, store_StoreItemCostInfoCmp);
	} else if (SAFE_MEMBER(pStoreItemDef, pPriceAccountKeyValue)) {
		StoreItemCostInfo* pCostInfo = NULL;

		// If the item has an override price using an Account Server key, build a StoreItemCostInfo here
		// that is essentially fake, in that it doesn't represent a price the game can actually charge.
		// Instead, it indicates that the account will have a key-value decremented, and it counts the
		// available amount as being the user's current count of that key. (Implicitly, we decrement by
		// 1 per purchase. If you need to "charge" more than one of the key-value, you'll need to extend
		// this feature.)
		pItemInfo->bUsesAccountKey = true;
		eaClearStruct(&pItemInfo->eaCostInfo, parse_StoreItemCostInfo);

		pCostInfo = StructCreate(parse_StoreItemCostInfo);
		eaPush(&pItemInfo->eaCostInfo, pCostInfo);

		pCostInfo->pPriceAccountKeyValue = pStoreItemDef->pPriceAccountKeyValue;
		pCostInfo->iCount = 1;

		pCostInfo->iAvailableCount = (int)gad_GetAccountValueIntFromExtract(pExtract, pStoreItemDef->pPriceAccountKeyValue);
		pItemInfo->iAccountKeyCount = pCostInfo->iAvailableCount;

		if (!pCostInfo->iAvailableCount) {
			pCostInfo->bTooExpensive = true;
		}
	} else {
		store_CalculateCurrencyCost(pPlayerEnt, pStoreDef, pPersistStore, pItemInfo);
	}

	bSkipBagFullCheck = (pStoreDef->eContents == Store_Injuries);
	pItemInfo->eCanBuyError = store_GetBuyItemError(pPlayerEnt, 
													pStoreDef, 
													pStoreItemDef,
													pItemInfo, 
													pItemDef,
													1,
													bSkipBagFullCheck,
													&pItemInfo->pchRequirementsText,
													&pRequiredTransaction,
													pExtract);

	pchRequirementsMessage = entTranslateDisplayMessage(pPlayerEnt, pStoreItemDef->requirementsMessage);
	if (pchRequirementsMessage) {
		estrClear(&pItemInfo->pchRequirementsMessage);
		estrAppend2(&pItemInfo->pchRequirementsMessage, pchRequirementsMessage);
	} else if (pItemInfo->pchRequirementsMessage) {
		estrDestroy(&pItemInfo->pchRequirementsMessage);
	}

	if (pRequiredTransaction) {
		SET_HANDLE_FROM_REFERENT(g_hMicroTransDefDict, pRequiredTransaction, pItemInfo->hRequiredMicroTransaction);
	} else {
		REMOVE_HANDLE(pItemInfo->hRequiredMicroTransaction);
	}

	eaDestroy(&ppValueRecipes);
	return pItemInfo;
}

static StoreItemInfo* store_SetupStoreItemInfo(Entity* pPlayerEnt, ContactDef* pContactDef, StoreDef* pStoreDef, int iItemIdx, GameAccountDataExtract *pExtract)
{
	PersistedStore* pPersistStore = NULL;
	if (pStoreDef->bIsPersisted) {
		ContactDialog *pContactDialog = SAFE_MEMBER3(pPlayerEnt, pPlayer, pInteractInfo, pContactDialog);
		pPersistStore = pContactDialog ? GET_REF(pContactDialog->hPersistedStore) : NULL;
	}
	return store_SetupStoreItemInfoEx(pPlayerEnt, pContactDef, pStoreDef, pPersistStore, iItemIdx, pExtract);
}

void store_GetStoreItemInfo(Entity *pPlayerEnt, 
							ContactDef *pContactDef, 
							StoreDef *pStoreDef, 
							StoreItemInfo ***peaItemInfo, 
							StoreItemInfo ***peaUnavailableItemInfo, 
							StoreDiscountInfo ***peaDiscountInfo, 
							GameAccountDataExtract *pExtract)
{
	int i, j, k;

	if (pPlayerEnt && pStoreDef && peaItemInfo) {

		if (pStoreDef->bIsPersisted) {
			return;
		}

        // Make sure player has data needed for provisioning.
        if (pStoreDef->bProvisionFromGroupProject)
        {
            GroupProject_ValidateContainer(pPlayerEnt, pStoreDef->eGroupProjectType);
        }

		for (i = 0; i < eaSize(&pStoreDef->inventory); i++)
		{
			StoreItemDef *pStoreItemDef = pStoreDef->inventory[i];

			{
				StoreItemInfo *pItemInfo = store_SetupStoreItemInfo(pPlayerEnt, pContactDef, pStoreDef, i, pExtract);
				if (pItemInfo)
				{
					if (store_ItemIsAvailableNow(pPlayerEnt, pStoreItemDef))
					{
						eaPush(peaItemInfo, pItemInfo);
					}
					else
					{
						// Our associated activity is not active right now or or ShowExpr is not true
						eaPush(peaUnavailableItemInfo, pItemInfo);
					}

					// Add discount info for this item.
					// We send the discount info regardless of if the item is available or not so that the info
					//  will be there in case it does become available.
 					for (j = eaSize(&pItemInfo->eaCostInfo)-1; j >= 0; j--)
					{
						StoreItemCostInfo* pCostInfo = pItemInfo->eaCostInfo[j];
						for (k = eaSize(&pCostInfo->ppchDiscounts)-1; k >= 0; k--)
						{
							const char* pchDiscountName = pCostInfo->ppchDiscounts[k];
							StoreDiscountInfo* pDiscountInfo = eaIndexedGetUsingString(peaDiscountInfo, pchDiscountName);
							if (!pDiscountInfo)
							{
								StoreDiscountDef* pDiscountDef = store_FindDiscountDefByName(pStoreDef, pchDiscountName);
								if (pDiscountDef)
								{
									pDiscountInfo = StructCreate(parse_StoreDiscountInfo);
									pDiscountInfo->pchName = allocAddString(pDiscountDef->pchName);
									pDiscountInfo->fDiscountPercent = pDiscountDef->fDiscountPercent;
									COPY_HANDLE(pDiscountInfo->hDisplayName, pDiscountDef->msgDisplayName.hMessage);
									COPY_HANDLE(pDiscountInfo->hDescription, pDiscountDef->msgDescription.hMessage);
									COPY_HANDLE(pDiscountInfo->hDiscountCostItem, pDiscountDef->hDiscountCostItem);
									eaIndexedEnable(peaDiscountInfo, parse_StoreDiscountInfo);
									eaPush(peaDiscountInfo, pDiscountInfo);
								}
							}
						}
					}
				}
				else
				{
					Errorf("Unable to add store item info for store %s, item %d", pStoreDef->name, i);
				}
			}
		}
	}
}

/*
void store_GetStoreItemInfo(Entity *pPlayerEnt, ContactDef *pContactDef, StoreDef *pStoreDef, StoreItemInfo ***peaItemInfo)
{
	int i;

	if (pPlayerEnt && pStoreDef && peaItemInfo) {
		
		if (pStoreDef->bIsPersisted) {
			return;
		}
		for (i = 0; i < eaSize(&pStoreDef->inventory); i++) {
			StoreItemInfo *pItemInfo = store_SetupStoreItemInfo(pPlayerEnt, pContactDef, pStoreDef, i);
			if ( pItemInfo )
			{
				eaPush(peaItemInfo, pItemInfo);
			}
			else
			{
				Errorf("Unable to add store item info for store %s, item %d", pStoreDef->name, i);
			}
		}
	}
}
*/

// Only returns items which in the store and also owned by OwnerEnt.  Replaces the count with the
// amount of that item found on pOwnerEnt
void store_GetStoreOwnedItemInfo(Entity *pPlayerEnt, Entity* pOwnerEnt, ContactDef *pContactDef, StoreDef *pStoreDef, StoreItemInfo ***peaItemInfo, InvBagIDs eBagToSearch, GameAccountDataExtract *pExtract)
{
	int i;

	if (pPlayerEnt && pOwnerEnt && pStoreDef && peaItemInfo) {
		
		if (pStoreDef->bIsPersisted) {
			return;
		}
		for (i = 0; i < eaSize(&pStoreDef->inventory); i++) {
			ItemDef* pItemDef = GET_REF(pStoreDef->inventory[i]->hItem);
			int iCount = 0;
			if(pItemDef) {
				if(eBagToSearch == InvBagIDs_None) {
					iCount = store_GetPlayerItemCount(pOwnerEnt, pItemDef->pchName);
				} else {
					iCount = inv_ent_CountItems(pOwnerEnt, eBagToSearch, pItemDef->pchName, pExtract);
				}
			}
			if(iCount > 0) {
				StoreItemInfo *pItemInfo = store_SetupStoreItemInfo(pPlayerEnt, pContactDef, pStoreDef, i, pExtract);
				pItemInfo->iCount = iCount;
				eaPush(peaItemInfo, pItemInfo);
			}
		}
	}
}

// Update the items that are sent to the client based on time-based availability. (For now just events
//  and the ShowExpression). Only remove stuff no longer available. Don't bother adding in new stuff
//  because we do not have access to the list of stores attached to the contact at this stage. The player
//  will need to close the dialog and reopen it to get new additions.
void store_UpdateStoreAvailability(Entity *pPlayerEnt, StoreItemInfo ***peaItemInfo, StoreItemInfo ***peaUnavailableItemInfo, GameAccountDataExtract *pExtract)
{
	int i;
	StoreItemInfo **ppItemInfosBecomingUnavailable = NULL;
	
	// Make a list of items which need to be moved from unavailable
	for (i = eaSize(peaItemInfo) - 1; i >= 0; i--)
	{
		StoreItemInfo *pItemInfo = (*peaItemInfo)[i];
		StoreDef *pStoreDef;
		StoreItemDef *pStoreItemDef;

		pStoreDef = RefSystem_ReferentFromString(g_StoreDictionary, pItemInfo->pchStoreName);
		if (pStoreDef)
		{
			if (pItemInfo->bIsFromPersistedStore || pStoreDef->bIsPersisted)
			{
				// Don't mess with Persisted stores
				continue;
			}

			pStoreItemDef = eaGet(&pStoreDef->inventory, pItemInfo->index);

			if (!store_ItemIsAvailableNow(pPlayerEnt, pStoreItemDef))
			{
				// Reset the expire time as we are becoming unavailable.
				pItemInfo->uExpireTime = 0;

				// Move us onto the temporary list of things to be moved.
				eaPush(&ppItemInfosBecomingUnavailable, pItemInfo);
				eaRemove(peaItemInfo, i);
			}
		}
	}

	// Move items to from unavailable list to available if needed
	for (i = eaSize(peaUnavailableItemInfo) - 1; i >= 0; i--)
	{
		StoreItemInfo *pItemInfo = (*peaUnavailableItemInfo)[i];
		StoreDef *pStoreDef;
		StoreItemDef *pStoreItemDef;

		pStoreDef = RefSystem_ReferentFromString(g_StoreDictionary, pItemInfo->pchStoreName);

		if (pStoreDef)
		{
			if (pItemInfo->bIsFromPersistedStore || pStoreDef->bIsPersisted)
			{
				// Don't mess with Persisted stores
				continue;
			}

			pStoreItemDef = eaGet(&pStoreDef->inventory, pItemInfo->index);

			// It has become available
			if (store_ItemIsAvailableNow(pPlayerEnt, pStoreItemDef))
			{
				// We are becoming available. Update the expire time if needed
				
				// Activity-related item. Figure out how long it will be around. Only used on non-persisted stuff
				if (!pItemInfo->bIsFromPersistedStore && pItemInfo->uExpireTime==0)
				{
					// Our expire time is that of our associated event. This is relative to the EventClock
					if (pStoreItemDef->pchActivityName && pStoreItemDef->pchActivityName[0])
					{
						pItemInfo->uExpireTime = gslActivity_EventClockEndingTime(pStoreItemDef->pchActivityName);
					}
				}
				eaPush(peaItemInfo, pItemInfo);
				eaRemove(peaUnavailableItemInfo, i);
			}
		}
	}
	
	// Move things from available to unavailable
	for (i = eaSize(&ppItemInfosBecomingUnavailable) - 1; i >= 0; i--)
	{
		StoreItemInfo *pItemInfo = (ppItemInfosBecomingUnavailable)[i];
		
		eaRemove(&ppItemInfosBecomingUnavailable, i);
		eaPush(peaUnavailableItemInfo, pItemInfo);
	}

	// Get rid of the temporary list
	eaDestroy(&ppItemInfosBecomingUnavailable);
}

void store_RefreshStoreItemInfo(Entity *pPlayerEnt, StoreItemInfo ***peaItemInfo, StoreItemInfo ***peaUnavailableItemInfo, GameAccountDataExtract *pExtract)
{
	int i, j;

	store_UpdateStoreAvailability(pPlayerEnt,peaItemInfo,peaUnavailableItemInfo,pExtract);
	
	for (i = 0; i < eaSize(peaItemInfo); i++)
	{
		bool bSkipBagFullCheck = false;
		StoreItemInfo *pItemInfo = (*peaItemInfo)[i];
		StoreDef *pStoreDef;
		StoreItemDef *pStoreItem = NULL;
		ItemDef* pItemDef = NULL;
		MicroTransactionDef *pRequiredTransaction = NULL;

		if (pItemInfo->bIsFromPersistedStore) {
			continue;
		}

		pStoreDef = RefSystem_ReferentFromString(g_StoreDictionary, pItemInfo->pchStoreName);

		if (pStoreDef)
		{
			if (pStoreDef->bIsPersisted)
			{
				pItemDef = pItemInfo->pOwnedItem ? GET_REF(pItemInfo->pOwnedItem->hItem) : NULL;
			}
			else
			{
				pStoreItem = pStoreDef ? eaGet(&pStoreDef->inventory, pItemInfo->index) : NULL;
				pItemDef = pStoreItem ? GET_REF(pStoreItem->hItem) : NULL;
			}
			bSkipBagFullCheck = (pStoreDef->eContents == Store_Injuries);
		}
		
		if (SAFE_MEMBER(pStoreItem, pPriceAccountKeyValue)) {
			StoreItemCostInfo* pCostInfo = NULL;

			// As in store_SetupStoreItemInfoEx, we build a "fake" cost info that represents the
			// Account Server key cost of this item. See that function for more details.
			pItemInfo->bUsesAccountKey = true;
			eaClearStruct(&pItemInfo->eaCostInfo, parse_StoreItemCostInfo);

			pCostInfo = StructCreate(parse_StoreItemCostInfo);
			eaPush(&pItemInfo->eaCostInfo, pCostInfo);

			pCostInfo->pPriceAccountKeyValue = pStoreItem->pPriceAccountKeyValue;
			pCostInfo->iCount = 1;
			pCostInfo->iAvailableCount = (int)gad_GetAccountValueIntFromExtract(pExtract, pStoreItem->pPriceAccountKeyValue);
			pItemInfo->iAccountKeyCount = pCostInfo->iAvailableCount;
		} else if (!pItemInfo->bIsValueRecipe || (pStoreItem && pStoreItem->bForceUseCurrency)) {
			store_CalculateCurrencyCost(pPlayerEnt, pStoreDef, NULL, pItemInfo);
		}
		
		for (j = 0; j < eaSize(&pItemInfo->eaCostInfo); j++){
			StoreItemCostInfo *pCostInfo = pItemInfo->eaCostInfo[j];
			ItemDef *pCurrencyDef = pCostInfo?GET_REF(pCostInfo->hItemDef):NULL;

			if (pCurrencyDef){
				if (pCurrencyDef->eType == kItemType_Numeric) {
					pCostInfo->iAvailableCount = inv_GetNumericItemValue(pPlayerEnt, pCurrencyDef->pchName);
				} else {
					pCostInfo->iAvailableCount = store_GetPlayerItemCount(pPlayerEnt, pCurrencyDef->pchName);
				}
			}

			if (pCostInfo){
				if (pCostInfo->iCount > pCostInfo->iAvailableCount) {
					pCostInfo->bTooExpensive = true;
				} else {
					pCostInfo->bTooExpensive = false;
				}
			}
		}

		pItemInfo->eCanBuyError = store_GetBuyItemError(pPlayerEnt, 
														pStoreDef,
														pStoreItem, 
														pItemInfo, 
														pItemDef,
														1,
														bSkipBagFullCheck,
														&pItemInfo->pchRequirementsText,
														&pRequiredTransaction,
														pExtract);

		if (pRequiredTransaction) {
			SET_HANDLE_FROM_REFERENT(g_hMicroTransDefDict, pRequiredTransaction, pItemInfo->hRequiredMicroTransaction);
		} else {
			REMOVE_HANDLE(pItemInfo->hRequiredMicroTransaction);
		}
	}
}



void store_UpdateStoreProvisioning(Entity *pPlayerEnt, ContactDialog *pDialog, GameAccountDataExtract *pExtract)
{
	S32 i, j;
	ContactDialogStoreProvisioning **eaUnusedProvisioning = NULL;

	eaCopy(&eaUnusedProvisioning, &pDialog->eaProvisioning);
	for (i = eaSize(&eaUnusedProvisioning) - 1; i >= 0; i--)
		eaClear(&eaUnusedProvisioning[i]->eapchStores);
	eaClear(&pDialog->eaProvisioning);


	for (i = eaSize(&pDialog->eaStoreItems) - 1; i >= 0; i--)
	{
		StoreItemInfo *pItemInfo = pDialog->eaStoreItems[i];
		StoreDef *pStoreDef;

		if (pItemInfo->bIsFromPersistedStore) {
			continue;
		}

		pStoreDef = RefSystem_ReferentFromString(g_StoreDictionary, pItemInfo->pchStoreName);

		if (pStoreDef && pStoreDef->bProvisionFromGroupProject) {
			GroupProjectDef *pProject = GET_REF(pStoreDef->provisioningProjectDef);
			GroupProjectNumericDef *pNumeric = GET_REF(pStoreDef->provisioningNumericDef);
			if (pProject && pNumeric) {
				for (j = eaSize(&pDialog->eaProvisioning) - 1; j >= 0; j--) {
					if (pProject->name == pDialog->eaProvisioning[j]->pchGroupProjectDef
							&& pNumeric->name == pDialog->eaProvisioning[j]->pchGroupProjectNumericDef) {
						break;
					}
				}
				if (j < 0) {
					ContactDialogStoreProvisioning *pProvisioning = NULL;
					const char *pchNumericName = entTranslateDisplayMessage(pPlayerEnt, pNumeric->displayNameMsg);
					GroupProjectContainer *pState = GroupProject_ResolveContainer(pPlayerEnt, pStoreDef->eGroupProjectType);
					GroupProjectState *pProjectState = pState ? eaIndexedGetUsingString(&pState->projectList, pProject->name) : NULL;
					GroupProjectNumericData *pNumericState = pProjectState ? eaIndexedGetUsingString(&pProjectState->numericData, pNumeric->name) : NULL;
					for (j = eaSize(&eaUnusedProvisioning) - 1; j >= 0; j--) {
						if (pProject->name == eaUnusedProvisioning[j]->pchGroupProjectDef
								&& pNumeric->name == eaUnusedProvisioning[j]->pchGroupProjectNumericDef) {
							pProvisioning = eaRemove(&eaUnusedProvisioning, j);
							break;
						}
					}
					if (j < 0) {
						pProvisioning = StructCreate(parse_ContactDialogStoreProvisioning);
					}
					pProvisioning->eGroupProjectType = pStoreDef->eGroupProjectType;
					pProvisioning->pchGroupProjectDef = pProject->name;
					pProvisioning->pchGroupProjectNumericDef = pNumeric->name;
					estrCopy2(&pProvisioning->estrNumericName, pchNumericName);
					pProvisioning->iNumericValue = pNumericState ? pNumericState->numericVal : 0;
					pProvisioning->bStoreGuildMapOwnerMembersOnly = pStoreDef->bGuildMapOwnerMembersOnly;
					eaPush(&pProvisioning->eapchStores, pStoreDef->name);
					eaPush(&pDialog->eaProvisioning, pProvisioning);
				} else {
					eaPushUnique(&pDialog->eaProvisioning[j]->eapchStores, pStoreDef->name);
				}
			}
		}
	}

	eaDestroyStruct(&eaUnusedProvisioning, parse_ContactDialogStoreProvisioning);
}

// Get the maximum number of items allowed in the buyback list
static U32 store_GetBuyBackMaximumItemCount(void)
{
	if(gConf.bUseBuyBackCountOverride)
	{
		return gConf.uBuyBackCountOverride;
	}

	return 50;
}

// Step through all the items on the buyback list and ensure the player actually has them
static void store_UpdateBuyBackList(Entity* pEnt, GameAccountDataExtract *pExtract)
{
	InteractInfo *pInteractInfo = SAFE_MEMBER2(pEnt, pPlayer, pInteractInfo);
//	BuyBackItemInfo** eaNoIDList = NULL;
	S32 i,j;

	if(!pInteractInfo || !pEnt || !pEnt->pPlayer)
	{
		return;
	}

	// check for too many items in the buyback list
	if((U32)eaSize(&pEnt->pPlayer->eaItemBuyBackList) > store_GetBuyBackMaximumItemCount())
	{
		// get ids to cut
		U32 uCutId = pEnt->pPlayer->uBuyBackId - store_GetBuyBackMaximumItemCount();
		U32 uAmountToCount = eaSize(&pEnt->pPlayer->eaItemBuyBackList) - store_GetBuyBackMaximumItemCount();

		for(i = 0; i < eaSize(&pEnt->pPlayer->eaItemBuyBackList); ++i)
		{
			ItemBuyBack *pBuyBack = pEnt->pPlayer->eaItemBuyBackList[i];
			if(pBuyBack && pBuyBack->uBuyBackId <= uCutId)
			{
				pBuyBack->eStatus = kItemBuyBackStatus_Destroy;
				--uAmountToCount;
				if(uAmountToCount == 0)
				{
					// enough have been removed
					break;
				}
			}
		}
	}

	for(i = 0; i < eaSize(&pEnt->pPlayer->eaItemBuyBackList); ++i)
	{
		ItemBuyBack *pBuyBack = pEnt->pPlayer->eaItemBuyBackList[i];
		if(pBuyBack)
		{
			if(pBuyBack->eStatus == kItemBuyBackStatus_OK)
			{
				bool bFound = false;
				// make sure its in list
				for(j = 0; j < eaSize(&pInteractInfo->eaBuyBackList); ++j)
				{
					StoreItemInfo *pStoreInfo = pInteractInfo->eaBuyBackList[j];
					if(pStoreInfo && pStoreInfo->pBuyBackInfo && pStoreInfo->pBuyBackInfo->uBuyBackItemId == pBuyBack->uBuyBackId)
					{
						pStoreInfo->pBuyBackInfo->iCount = pBuyBack->pItem->count;	// update count
						bFound = true;
						break;
					}
				}

				if(!bFound)
				{
					// Add it
					ItemDef *pItemDef = GET_REF(pBuyBack->pItem->hItem);
					StoreItemInfo *pStoreInfo = StructCreate(parse_StoreItemInfo);
					StoreItemCostInfo *pCostInfo = StructCreate(parse_StoreItemCostInfo);
					ItemDef *pCurrencyDef = EMPTY_TO_NULL(pBuyBack->pcCurrency) ? RefSystem_ReferentFromString(g_hItemDict, pBuyBack->pcCurrency) : NULL;
					if(pCurrencyDef && pItemDef)
					{
						pStoreInfo->pBuyBackInfo = StructCreate(parse_BuyBackItemInfo);
						pStoreInfo->pBuyBackInfo->iCost = pBuyBack->uBuyBackPrice;
						pStoreInfo->pBuyBackInfo->iCount = pBuyBack->pItem->count;
						pStoreInfo->pBuyBackInfo->pchItemName = pItemDef->pchName;
						pStoreInfo->pBuyBackInfo->pchResourceName = allocAddString(pBuyBack->pcCurrency);
						pStoreInfo->pBuyBackInfo->uBuyBackItemId = pBuyBack->uBuyBackId;

						// Set up cost info
						pCostInfo->bTooExpensive = false;
						SET_HANDLE_FROM_REFERENT(g_hItemDict, pCurrencyDef, pCostInfo->hItemDef);

						// buy back cost
						pCostInfo->iCount = pBuyBack->uBuyBackPrice;
						eaPush(&pStoreInfo->eaCostInfo, pCostInfo);

						eaPush(&pInteractInfo->eaBuyBackList, pStoreInfo);
					}
				}
			}
			else
			{
				// remove it from list
				for(j = 0; j < eaSize(&pInteractInfo->eaBuyBackList); ++j)
				{
					StoreItemInfo *pStoreInfo = pInteractInfo->eaBuyBackList[j];
					if(pStoreInfo && pStoreInfo->pBuyBackInfo && pStoreInfo->pBuyBackInfo->uBuyBackItemId == pBuyBack->uBuyBackId)
					{
						eaRemove(&pInteractInfo->eaBuyBackList, j);
						StructDestroy(parse_StoreItemInfo, pStoreInfo);
						break;
					}
				}
			}
		}
	}

	// delete pass for destroyed items
	for(i = eaSize(&pEnt->pPlayer->eaItemBuyBackList) - 1; i >= 0 ;--i)
	{
		ItemBuyBack *pBuyBack = pEnt->pPlayer->eaItemBuyBackList[i];
		if(pBuyBack)
		{
			if(pBuyBack->eStatus == kItemBuyBackStatus_Destroy)
			{
				eaFindAndRemove(&pEnt->pPlayer->eaItemBuyBackList, pBuyBack);
				StructDestroy(parse_ItemBuyBack, pBuyBack);
			}
		}
	}

	entity_SetDirtyBit(pEnt, parse_InteractInfo, pInteractInfo, true);
}

// Add sold item to buyback list
void Store_SellItem_CB(TransactionReturnVal *pReturn, StoreSellItemCBData *pData)
{
	Entity *pEnt = pData ? entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pData->uEntID) : NULL;
	InteractInfo *pInteractInfo = SAFE_MEMBER2(pEnt, pPlayer, pInteractInfo);

	if(pEnt && pEnt->pPlayer)
	{

		if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS && pEnt && pInteractInfo)
		{

			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

			if(pData->pItemInfo)
			{
				ItemBuyBack *pBuyBack = eaIndexedGetUsingInt(&pEnt->pPlayer->eaItemBuyBackList, pData->pItemInfo->uBuyBackItemId);
				if(pBuyBack)
				{
					// This item is ready to be bought back
					pBuyBack->eStatus = kItemBuyBackStatus_OK;
				}
			}

			store_UpdateBuyBackList(pEnt, pExtract);
		}
		else
		{
			// failed, remove from earray
			if(pData->pItemInfo)
			{
				ItemBuyBack *pBuyBack = eaIndexedGetUsingInt(&pEnt->pPlayer->eaItemBuyBackList, pData->pItemInfo->uBuyBackItemId);
				if(pBuyBack)
				{
					// Destroy this item
					pBuyBack->eStatus = kItemBuyBackStatus_Destroy;
					eaFindAndRemove(&pEnt->pPlayer->eaItemBuyBackList, pBuyBack);
					StructDestroy(parse_ItemBuyBack, pBuyBack);
				}
			}
		}

		pEnt->pPlayer->uBuyBackTime = 0;	// ok to buy/sell again
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, true);
	}

	// free data
	if(pData)
	{
		if(pData->pItemInfo)
		{
			StructDestroySafe(parse_BuyBackItemInfo, &pData->pItemInfo);
		}
		free(pData);
	}
}

static void store_SellItemHelper(Entity* pEnt, S32 iBagID, S32 iSlot, S32 iCount, S32 iGlobalType, S32 iContainerID, ContactDef *pContactDef, bool bTrySellStore, GameAccountDataExtract *pExtract)
{
	int iPartitionIdx = entGetPartitionIdx(pEnt);
	Entity *pSource = iGlobalType == GLOBALTYPE_ENTITYSAVEDPET ? entity_GetSubEntity(iPartitionIdx,pEnt,iGlobalType,iContainerID) : NULL;
	Item* pItem = inv_GetItemFromBag((pSource?pSource:pEnt), iBagID, iSlot, pExtract);
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	StoreDef *pStoreDef = NULL;
	ItemDef *pCurrencyDef;
	S32 iCost, iCurrencyValue;
	S32 i,j;
	TransactionReturnVal *pReturn = NULL;
	S32 iAllowBuyback = false;
	bool bSellEnabled = false;
	ItemChangeReason reason = {0};
	bool bSellable = pItemDef ? (!(pItemDef->flags & kItemDefFlag_CantSell) && pItemDef->eType != kItemType_Mission && pItemDef->eType != kItemType_MissionGrant) : false;

	if (!pContactDef || !pEnt || !pEnt->pPlayer || iCount < 1 || !pItemDef || (iGlobalType == GLOBALTYPE_ENTITYSAVEDPET && !pSource)) {
		return;
	}

	if(iCount > pItem->count)
	{
		// somehow a count greater than the number of items has been passed in
		Errorf("Entity trying to sell more of item than he has.");

		return;
	}

	if(timeSecondsSince2000() < pEnt->pPlayer->uBuyBackTime)
	{
		// too soon to buy again ... probably a transaction still running
		return;
	}

	if (!pSource) pSource = pEnt;

	// Make sure the item is sellable
	if ((pItemDef->flags & kItemDefFlag_CantSell) ||
		pItemDef->eType == kItemType_Mission ||
		pItemDef->eType == kItemType_MissionGrant)
	{	
		ClientCmd_NotifySend(pEnt, kNotifyType_ItemBuyFailed, TranslateMessageKey("Store.SellFailed"), NULL, NULL);
		return;
	}

	for (i=0; i < eaSize(&pContactDef->stores); i++) {
		pStoreDef = GET_REF(pContactDef->stores[i]->ref);
		if (pStoreDef && pStoreDef->bSellEnabled) {

			if(pStoreDef->eContents == Store_Sellable_Items)
			{
				for(j=0; j < eaSize(&pStoreDef->inventory) && !bSellEnabled; j++)
				{
					if(GET_REF(pStoreDef->inventory[j]->hItem) == pItemDef)
						bSellEnabled = true;
				}
			} else {
				bSellEnabled = true;
			}

			if(bSellEnabled)
				break;
		}
	}
	if (i == eaSize(&pContactDef->stores)) {
		//If none of the stores on the ContactDef can sell the item, try the sell store on the contact dialog
		ContactDialog* pContactDialog = SAFE_MEMBER(pEnt->pPlayer->pInteractInfo, pContactDialog);
		if (bTrySellStore && pContactDialog && GET_REF(pContactDialog->hSellStore)) {
			pStoreDef = GET_REF(pContactDialog->hSellStore);
		} else {
			return;
		}
	}

	if (pStoreDef->pExprRequires)
	{
		MultiVal answer = {0};
		store_BuyContextSetup(pEnt, pItemDef);
		exprEvaluate(pStoreDef->pExprRequires, store_GetBuyContext(), &answer);
		if(answer.type == MULTI_INT)
		{
			if (!QuickGetInt(&answer))
			{
				return;
			}
		}
	}

	pCurrencyDef = GET_REF(pStoreDef->hCurrency);
	if (!pCurrencyDef) {
		return;
	}

	// If this is a bag remove out of an index bag then verify that the bag is empty
	if (pItemDef->eType == kItemType_Bag) {
		InventoryBag *pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pSource), iBagID, pExtract);

		if ((invbag_flags(pBag) & InvBagFlag_PlayerBagIndex) != 0) {
			if (inv_PlayerBagFail(pSource, pBag, iSlot)) {
				return;
			}
		}
	}

	iCost = ITEM_SELL_MULTIPLIER * pStoreDef->fSellMultiplier * item_GetStoreEPValue(iPartitionIdx, pEnt, pItem, pStoreDef);
	iCurrencyValue = item_GetDefEPValue(iPartitionIdx, pEnt, pCurrencyDef, pCurrencyDef->iLevel, pCurrencyDef->Quality);
	if (iCurrencyValue) {
		iCost /= iCurrencyValue;
	}

	if(gConf.bAllowBuyback && bSellable)
	{
		StoreSellItemCBData *pData = NULL;
		ItemBuyBack *pBuyBack;

		iAllowBuyback = true;
		pData = calloc(1, sizeof(StoreSellItemCBData));
		pData->uEntID = pEnt->myContainerID;
		pData->pItemInfo = StructCreate(parse_BuyBackItemInfo);
		pData->pItemInfo->iCost = iCost;
		pData->pItemInfo->iCount = iCount;
		pData->pItemInfo->pchResourceName = allocAddString(RefSystem_StringFromReferent(pCurrencyDef));
		pData->pItemInfo->iItemID = pItem->id;
		pData->pItemInfo->pchItemName = REF_STRING_FROM_HANDLE(pItem->hItem);

		// new buyback code
		pBuyBack = StructCreate(parse_ItemBuyBack);

		pBuyBack->pItem = StructClone(parse_Item, pItem);
		CONTAINER_NOCONST(Item, pBuyBack->pItem)->count = iCount;
		pBuyBack->eStatus = kItemBuyBackStatus_Waiting;
		pBuyBack->pcCurrency = allocAddString(RefSystem_StringFromReferent(pCurrencyDef));
		pBuyBack->uBuyBackId = ++pEnt->pPlayer->uBuyBackId;	
		pBuyBack->uBuyBackPrice = iCost;	// per item

		pData->pItemInfo->uBuyBackItemId = pBuyBack->uBuyBackId;

		// Add item to the player
		if(!pEnt->pPlayer->eaItemBuyBackList)
		{
			eaIndexedEnable(&pEnt->pPlayer->eaItemBuyBackList,parse_ItemBuyBack);
		}
		eaPush(&pEnt->pPlayer->eaItemBuyBackList, pBuyBack);
		pEnt->pPlayer->uBuyBackTime = timeSecondsSince2000() + ITEM_BUY_BACK_TIMEOUT_SECONDS;
		// player is dirty
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, true);

		pReturn = LoggedTransactions_CreateManagedReturnValEnt("SellItem", pEnt, Store_SellItem_CB, pData);
	} else {
		pReturn = LoggedTransactions_CreateManagedReturnValEnt("SellItem", pEnt, NULL, NULL);
	}

	if (iGlobalType == GLOBALTYPE_ENTITYSAVEDPET)
	{
		inv_FillItemChangeReasonStore(&reason, pEnt, "Store:SellItemOnPet", pItemDef->pchName);

		AutoTrans_tr_ItemSellFromBagFromPet(pReturn, GetAppGlobalType(), 
				entGetType(pEnt), entGetContainerID(pEnt), 
				entGetType(pSource), entGetContainerID(pSource), 
				iBagID, pItemDef->pchName, iSlot, pItem->id, iCount, iCost, pCurrencyDef->pchName, &reason, pExtract);
	}
	else
	{
		inv_FillItemChangeReasonStore(&reason, pEnt, "Store:SellItem", pItemDef->pchName);

		AutoTrans_tr_ItemSellFromBag(pReturn, GetAppGlobalType(), 
				entGetType(pEnt), entGetContainerID(pEnt), 
				iBagID, pItemDef->pchName, iSlot, pItem->id, iCount, iCost, pCurrencyDef->pchName, &reason, pExtract);
	}
}

// Sell to the specified contact, must be one that allows dialog-less interactions
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Inventory) ACMD_PRIVATE;
void store_SellItemNoDialog(Entity* pEnt, S32 iBagID, S32 iSlot, S32 iCount, S32 iGlobalType, S32 iContainerID, const char* pchContactDef)
{
	GameAccountDataExtract *pExtract;
	ContactDef *pContactDef = RefSystem_ReferentFromString("ContactDef", pchContactDef);
	int iPartitionIdx = entGetPartitionIdx(pEnt);
	Entity *pSource = iGlobalType == GLOBALTYPE_ENTITYSAVEDPET ? entity_GetSubEntity(iPartitionIdx,pEnt,iGlobalType,iContainerID) : pEnt;

	if(!pContactDef || !pSource)
		return;

	if(!contact_CanInteractRemote(pContactDef, pEnt))
		return;
	
	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	store_SellItemHelper(pEnt, iBagID, iSlot, iCount, iGlobalType, iContainerID, pContactDef, false, pExtract);
}

// Remove Item from specific bag
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Inventory) ACMD_PRIVATE;
void store_SellItem(Entity *pEnt, S32 iBagID, S32 iSlot, S32 iCount, S32 iGlobalType, S32 iContainerID)
{
	ContactDialog *pContactDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	ContactDef *pContactDef = pContactDialog?GET_REF(pContactDialog->hContactDef):NULL;
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

	store_SellItemHelper(pEnt, iBagID, iSlot, iCount, iGlobalType, iContainerID, pContactDef, true, pExtract);
}

static void store_BuyBackItem_CB(TransactionReturnVal *pReturn, StoreSellItemCBData *pData)
{
	Entity *pEnt = pData ? entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pData->uEntID) : NULL;
	InteractInfo *pInteractInfo = SAFE_MEMBER2(pEnt, pPlayer, pInteractInfo);
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

	if(pEnt && pEnt->pPlayer)
	{
		BuyBackItemInfo* pBuyBackInfo = pData->pItemInfo;
		ItemBuyBack *pBuyBack = NULL;
		if(pData->pItemInfo)
		{
			pBuyBack = eaIndexedGetUsingInt(&pEnt->pPlayer->eaItemBuyBackList, pData->pItemInfo->uBuyBackItemId);
		}
		if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
		{
			if(pBuyBack)
			{
				// the call to store_UpdateBuyBackList(pEnt, pExtract); will destroy the item and update the list
				pBuyBack->eStatus = kItemBuyBackStatus_Destroy;
			}
		}
		else
		{
			if(pBuyBack)
			{
				// player can try again
				pBuyBack->eStatus = kItemBuyBackStatus_OK;
			}
		}

		// Do a pass on the player's buyback list
		store_UpdateBuyBackList(pEnt, pExtract);

		// player is dirty
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, true);
	}

	if(pData)
	{
		if(pData->pItemInfo)
		{
			StructDestroySafe(parse_BuyBackItemInfo, &pData->pItemInfo);
		}
		free(pData);
	}
}

int store_CompareStoreItemInfo_Buyback(const StoreItemInfo* a, const StoreItemInfo* b) 
{
	if(!a && !b) {
		return true;
	}
	else if(a && b)
	{
		BuyBackItemInfo *pBuyBackA = a->pBuyBackInfo;
		BuyBackItemInfo *pBuyBackB = b->pBuyBackInfo;

		if(pBuyBackB && pBuyBackA)
		{

			if(pBuyBackA->uBuyBackItemId == pBuyBackB->uBuyBackItemId)
			{
				return true;
			}
		}
	}
	return false;
}

// Remove Item from specific bag
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_SERVERCMD ACMD_CATEGORY(Standard, Inventory);
void store_BuybackItem(Entity *pEnt, S32 iGlobalType, S32 iContainerID, SA_PARAM_OP_VALID StoreItemInfo* pStoreItem)
{
	int iPartitionIdx = entGetPartitionIdx(pEnt);
	Entity *pSource = iGlobalType == GLOBALTYPE_ENTITYSAVEDPET ? entity_GetSubEntity(iPartitionIdx,pEnt,iGlobalType,iContainerID) : NULL;
	InteractInfo *pInteractInfo = SAFE_MEMBER2(pEnt, pPlayer, pInteractInfo);
	int iActualStoreItemInfoIdx = pInteractInfo && pStoreItem ? eaFindCmp(&pInteractInfo->eaBuyBackList, pStoreItem, store_CompareStoreItemInfo_Buyback) : -1;
	StoreItemInfo* pActualStoreItemInfo = iActualStoreItemInfoIdx > -1 && pInteractInfo ? pInteractInfo->eaBuyBackList[iActualStoreItemInfoIdx] : NULL;
	BuyBackItemInfo* pBuyBackInfo = pActualStoreItemInfo ? pActualStoreItemInfo->pBuyBackInfo : NULL;
	int iSlot = -1;
	Item* pItem = NULL;
	ItemDef *pItemDef = NULL;
	ContactDialog *pContactDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	ContactDef *pContactDef = pContactDialog?GET_REF(pContactDialog->hContactDef):NULL;
	S32 iCount = pBuyBackInfo ? pBuyBackInfo->iCount : 0;
	StoreDef *pStoreDef = NULL;
	S32 i;
	TransactionReturnVal *pReturn = NULL;
	StoreSellItemCBData *pData = NULL;
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	ItemChangeReason reason = {0};
	S32 bagID;
	bool bLiteBag;
	ItemBuyBack *pBuyBack = NULL;

	// Get Item
	if(pBuyBackInfo)
	{
		pBuyBack = eaIndexedGetUsingInt(&pEnt->pPlayer->eaItemBuyBackList, pBuyBackInfo->uBuyBackItemId);
		if(pBuyBack)
		{
			if(pBuyBack->eStatus != kItemBuyBackStatus_OK)
			{
				// bad status, can't buy it back
				return;
			}
			pItem = pBuyBack->pItem;
		}
		else
		{
			// no buyback info for this item
			return;
		}

		pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	}

	if (!pContactDef || !pEnt || !pEnt->pPlayer || iCount < 1 || !pItemDef || (iGlobalType == GLOBALTYPE_ENTITYSAVEDPET && !pSource) || !pBuyBackInfo) {
		return;
	}

	if(timeSecondsSince2000() < pEnt->pPlayer->uBuyBackTime)
	{
		// too soon, probably a transaction still running
		return;
	}

	if (!pSource) pSource = pEnt;

	// Make sure the buyback is allowed on the store
	for (i=0; i < eaSize(&pContactDef->stores); i++) {
		pStoreDef = GET_REF(pContactDef->stores[i]->ref);
		if (pStoreDef && pStoreDef->bSellEnabled) {
			break;
		}
	}
	if (i == eaSize(&pContactDef->stores)) {
		pStoreDef = GET_REF(pContactDialog->hSellStore);
		if (!pStoreDef) {
			return;
		}
	}

	bagID = GetBestBagForItemDef(pEnt, pItemDef, iCount, true, pExtract);
	bLiteBag = !!inv_GetLiteBag(pEnt, bagID, pExtract);

	if(!bLiteBag && (pItemDef->flags & kItemDefFlag_LockToRestrictBags) == 0 && pItemDef->eType != kItemType_Injury)
	{
		// put all non-lite non injury and non restricted items in the main inventory bag if possible
		S32 oldBagID = bagID;
		bagID = InvBagIDs_Inventory;	
		if(!item_ItemMoveDestValid(pEnt,pItemDef,NULL,false,bagID,-1,false,pExtract))
		{
			// try using the other bag
			bagID = oldBagID;
		}
	}

	if(bagID != InvBagIDs_Inventory && !bLiteBag && !item_ItemMoveDestValid(pEnt,pItemDef,NULL,false,bagID,-1,false,pExtract) )
	{
		if(pItemDef->eType == kItemType_Injury)
		{
			// Injuries are only allowed in their restrict bags
			ErrorDetailsf("Item %s", pItemDef->pchName);
			Errorf("invtransaction_AddItem: Item of type Injury failed to be placed into its designated bag");
			return;
		}
		if (pItemDef->flags & kItemDefFlag_LockToRestrictBags)
		{
			// If the item is flagged as 'LockToRestrictBag', then it cannot go in the inventory
			ErrorDetailsf("Item %s", pItemDef->pchName);
			Errorf("invtransaction_AddItem: Item flagged as 'LockToRestrictBag' failed to be placed in a restrict bag");
			return;
		}

		// Default to the inventory bag
		bagID = InvBagIDs_Inventory;
	}

	pData = calloc(1, sizeof(StoreSellItemCBData));
	pData->uEntID = pEnt->myContainerID;
	pData->pItemInfo = StructClone(parse_BuyBackItemInfo, pBuyBackInfo);

	pReturn = LoggedTransactions_CreateManagedReturnValEnt("SellItem", pEnt, store_BuyBackItem_CB, pData);

	inv_FillItemChangeReasonStore(&reason, pEnt, "Store:BuybackItem", pItemDef->pchName);

	// set status of item
	pBuyBack->eStatus = kItemBuyBackStatus_BeingBought;

	AutoTrans_tr_ItemBuyback(pReturn,  GetAppGlobalType(), 
			entGetType(pEnt), entGetContainerID(pEnt), iCount, pBuyBackInfo->iCost, pBuyBackInfo->pchResourceName, &reason, pExtract, pItem, bagID);
}

typedef struct StoreBuyItemCBData {
	ContainerID uEntID;
	const char *pchItemName;
	const char *pchStoreName;
	const char *pchContactName;
	const char *pchCurrencyName;
	U32 iCount;
	U32 iEPValue;
	ContainerID uLockID;
} StoreBuyItemCBData;

// Turn off the bPurchaseInProgress flag after the buy transaction completes
void Store_BuyItem_CB(TransactionReturnVal *pReturn, StoreBuyItemCBData *pData)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pData->uEntID);
	InteractInfo *pInteractInfo = SAFE_MEMBER2(pEnt, pPlayer, pInteractInfo);
	if (pInteractInfo) {
		pInteractInfo->bPurchaseInProgress = false;
	}
	
	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		// If this is an injury store, update the count on the item
		ContactDialog* pDialog = pInteractInfo ? pInteractInfo->pContactDialog : NULL;
		if(pData && pDialog && pDialog->state == ContactDialogState_InjuryStore)
		{
			ContactDef* pDef = GET_REF(pDialog->hContactDef);
			if(pDef && pData->pchContactName && stricmp(pDef->name, pData->pchContactName) == 0)
			{
				int i;
				for(i=0; i < eaSize(&pDialog->eaStoreItems); i++)
				{
					Item* pItem = pDialog->eaStoreItems[i]->pItem ? pDialog->eaStoreItems[i]->pItem : pDialog->eaStoreItems[i]->pOwnedItem;
					ItemDef* pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
					if(stricmp(pDialog->eaStoreItems[i]->pchStoreName, pData->pchStoreName) == 0 && pItemDef && stricmp(pItemDef->pchName, pData->pchItemName) == 0)
					{
						pDialog->eaStoreItems[i]->iCount =pDialog->eaStoreItems[i]->iCount - pData->iCount;
						if(pDialog->eaStoreItems[i]->iCount <= 0)
						{
							StructDestroy(parse_StoreItemInfo, eaRemove(&pDialog->eaStoreItems, i));
						}
						break;
					}
				}
			}
		}
		if (pEnt)
		{
			// Record the purchase event
			eventsend_RecordItemPurchased(pEnt, pData->pchItemName, pData->pchStoreName, pData->pchContactName, pData->iCount, pData->iEPValue);
		}
	}
	else // TRANSACTION_OUTCOME_FAILURE
	{
		if (pData->uLockID)
		{
			AutoTrans_AccountProxy_tr_RollbackLock(NULL, GLOBALTYPE_ACCOUNTPROXYSERVER,
				GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, pData->uLockID, entGetAccountID(pEnt), pData->pchCurrencyName);
		}
	}
	
	free(pData);
}

static void store_tr_BuyItem(
	Entity *pEnt,
	Entity *pTargetEnt,
	S32 iCount,
	PersistedStore *pPersistStore,
	StoreDef *pStoreDef,
	StoreItemDef *pStoreItemDef,
	StoreItemInfo *pStoreItemInfo,
	ItemDef *pItemDef,
	ItemDef *pActualItemDef,
	Item *pItemInstance,
	const char *pcCurrencyName,
	StoreItemCostInfoList CostInfoList,
	bool bForceBind,
	GameAccountDataExtract *pExtract,
	GlobalType provisioningProjectContainerType,
	ContainerID provisioningProjectContainerID,
	StoreBuyExtraArgs *extraArgs,
	StoreBuyItemCBData *pData,
	ContainerID lockContainerID)
{
	ItemChangeReason reason = {0};

	// if the store is a recipe trainer and the item is a recipe, buy the recipe straight to the recipe bag
	if (pStoreDef->eContents == Store_Recipes && (pActualItemDef->eType == kItemType_ItemRecipe || pActualItemDef->eType == kItemType_ItemPowerRecipe)) {
		TransactionReturnVal *pReturn;
		U32* eaPets = NULL;
		inv_FillItemChangeReasonStore(&reason, pEnt, "Store:BuyRecipe", pItemDef->pchName);

		// Only lock the pets if the item is unique
		if (pItemDef->flags & kItemDefFlag_Unique) {
			ea32Create(&eaPets);
			Entity_GetPetIDList(pEnt, &eaPets);
		}

		pReturn = LoggedTransactions_CreateManagedReturnValEnt("ItemBuyIntoBag", pEnt, Store_BuyItem_CB, pData);
		AutoTrans_tr_ItemBuyIntoBag(pReturn, GetAppGlobalType(), 
			entGetType(pEnt), entGetContainerID(pEnt), 
			GLOBALTYPE_ENTITYSAVEDPET, &eaPets,
			GLOBALTYPE_PERSISTEDSTORE, pPersistStore?pPersistStore->uContainerID:0,
			GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, lockContainerID,
			InvBagIDs_Recipe, -1, pItemDef, iCount, &CostInfoList, pcCurrencyName, false, NULL, &reason, pExtract,
			provisioningProjectContainerType, provisioningProjectContainerID, extraArgs);
		ea32Destroy(&eaPets);
	}
	// if this is a costume unlock, unlock the costume directly
	else if (pStoreDef->eContents == Store_Costumes && pActualItemDef->eCostumeMode == kCostumeDisplayMode_Unlock && eaSize(&pActualItemDef->ppCostumes) > 0)
	{
		TransactionReturnVal *pReturn = LoggedTransactions_CreateManagedReturnValEnt("ItemBuyCostume", pEnt, Store_BuyItem_CB, pData);
		inv_FillItemChangeReasonStore(&reason, pEnt, "Store:BuyCostume", pItemDef->pchName);
		AutoTrans_tr_ItemBuyCostume(pReturn, GetAppGlobalType(), 
			entGetType(pEnt), entGetContainerID(pEnt), 
			GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, lockContainerID,
			pItemDef, &CostInfoList, pcCurrencyName, &reason, pExtract);
	}
	// if this is an injury, remove the injury
	else if (pStoreDef->eContents == Store_Injuries)
	{
		TransactionReturnVal *pReturn = LoggedTransactions_CreateManagedReturnValEnt("ItemBuyAndRemove", pEnt, Store_BuyItem_CB, pData);
		if(pEnt == pTargetEnt) {
			inv_FillItemChangeReasonStore(&reason, pEnt, "Store:BuyInjuryRemoval", pItemDef->pchName);
			AutoTrans_tr_ItemBuyAndRemove(pReturn, GLOBALTYPE_GAMESERVER, 
				entGetType(pEnt), entGetContainerID(pEnt), 
				GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, lockContainerID,
				InvBagIDs_Injuries, pItemDef, iCount, &CostInfoList, pcCurrencyName, pStoreItemDef, &reason, pExtract);
		} else {
			inv_FillItemChangeReasonStore(&reason, pEnt, "Store:BuyInjuryRemovalForOther", pItemDef->pchName);
			AutoTrans_tr_ItemBuyAndRemoveAcrossEnts(pReturn, GLOBALTYPE_GAMESERVER, 
				entGetType(pEnt), entGetContainerID(pEnt), 
				entGetType(pTargetEnt), entGetContainerID(pTargetEnt), 
				GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, lockContainerID,
				InvBagIDs_Injuries, pItemDef, iCount, &CostInfoList, pcCurrencyName, pStoreItemDef, &reason, pExtract);
		}
	}
	// if it's a numeric item, buy into the numeric bag
	else if (pActualItemDef->eType == kItemType_Numeric) {
		TransactionReturnVal *pReturn = LoggedTransactions_CreateManagedReturnValEnt("ItemBuyIntoBag", pEnt, Store_BuyItem_CB, pData);
		U32* eaPets = NULL;	// No pets but create this as otherwise crash occurs when pets are looked up in auto trans wrapper
		inv_FillItemChangeReasonStore(&reason, pEnt, "Store:BuyNumeric", pItemDef->pchName);
		AutoTrans_tr_ItemBuyIntoBag(pReturn, GetAppGlobalType(),
			entGetType(pEnt), entGetContainerID(pEnt),
			GLOBALTYPE_ENTITYSAVEDPET, &eaPets,
			GLOBALTYPE_PERSISTEDSTORE, pPersistStore?pPersistStore->uContainerID:0,
			GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, lockContainerID,
			InvBagIDs_Numeric, -1, pItemDef, iCount, &CostInfoList, pcCurrencyName, false, NULL, &reason, pExtract,
			provisioningProjectContainerType, provisioningProjectContainerID, extraArgs);
	}
	// otherwise, just buy the item into the player's inventory
	else {
		InvBagIDs overrideBag = itemAcquireOverride_FromStore(pActualItemDef);
		inv_FillItemChangeReasonStore(&reason, pEnt, "Store:BuyItem", pItemDef->pchName);
		if ( overrideBag != InvBagIDs_None )
		{
			TransactionReturnVal *pReturn;
			U32* eaPets = NULL;

			// Only lock the pets if the item is unique
			if (pActualItemDef->flags & kItemDefFlag_Unique) {
				ea32Create(&eaPets);
				Entity_GetPetIDList(pEnt, &eaPets);
			}

			pReturn = LoggedTransactions_CreateManagedReturnValEnt("ItemBuyIntoBag", pEnt, Store_BuyItem_CB, pData);
			AutoTrans_tr_ItemBuyIntoBag(pReturn, GetAppGlobalType(), 
				entGetType(pEnt), entGetContainerID(pEnt), 
				GLOBALTYPE_ENTITYSAVEDPET, &eaPets,
				GLOBALTYPE_PERSISTEDSTORE, pPersistStore?pPersistStore->uContainerID:0,
				GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, lockContainerID,
				overrideBag, -1, pActualItemDef, iCount, &CostInfoList, pcCurrencyName, bForceBind, pItemInstance, &reason, pExtract,
				provisioningProjectContainerType, provisioningProjectContainerID, extraArgs);
			ea32Destroy(&eaPets);
		}
		else
		{
			// Make sure we have room for it, so we can inform the player if we don't and avoid the expensive transaction
			if (store_BuyItem_CanFitIntoGeneralInventory(pEnt, pActualItemDef, pStoreItemInfo, iCount, pExtract)) {

				TransactionReturnVal *pReturn;
				U32* eaPets = NULL;

				// Only lock the pets if the item is unique
				if (pActualItemDef->flags & kItemDefFlag_Unique) {
					ea32Create(&eaPets);
					Entity_GetPetIDList(pEnt, &eaPets);
				}

				pReturn = LoggedTransactions_CreateManagedReturnValEnt("ItemBuyIntoBag", pEnt, Store_BuyItem_CB, pData);
				AutoTrans_tr_ItemBuyIntoBag(pReturn, GetAppGlobalType(), 
					entGetType(pEnt), entGetContainerID(pEnt), 
					GLOBALTYPE_ENTITYSAVEDPET, &eaPets,
					GLOBALTYPE_PERSISTEDSTORE, pPersistStore?pPersistStore->uContainerID:0,
					GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, lockContainerID,
					InvBagIDs_Inventory, -1, pItemDef, iCount, &CostInfoList, pcCurrencyName, bForceBind, pItemInstance, &reason, pExtract,
					provisioningProjectContainerType, provisioningProjectContainerID, extraArgs);
				ea32Destroy(&eaPets);

			} else {
				// Make sure nothing is leaked here!
				ClientCmd_NotifySend(pEnt, kNotifyType_ItemMoveFailed, entTranslateMessageKey(pEnt, "Inventory.InventoryFull"), NULL, NULL);
				pEnt->pPlayer->pInteractInfo->bPurchaseInProgress = false;
				free(pData);

				if (lockContainerID) {
					AutoTrans_AccountProxy_tr_RollbackLock(NULL, GLOBALTYPE_ACCOUNTPROXYSERVER,
						GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, lockContainerID, entGetAccountID(pEnt), pcCurrencyName);
				}
			}
		}
	}
}

typedef struct StoreAccountKeyPurchaseData {
	ContainerID uEntID;
	GlobalType eTargetEntType;
	ContainerID uTargetEntID;
	const char *pchStoreName;
	U32 uStoreItemIndex;
	S32 iCount;

	StoreDef *pStoreDef;
	StoreItemDef *pStoreItemDef;
	ItemDef *pItemDef;
	ItemDef *pActualItemDef;
	bool bForceBind;
	GlobalType provisioningProjectContainerType;
	ContainerID provisioningProjectContainerID;
	StoreBuyExtraArgs *extraArgs;
	StoreBuyItemCBData *pCBData;
} StoreAccountKeyPurchaseData;

static void store_BuyItemAccountKey_CB(AccountKeyValueResult eResult, U32 uAccountID, SA_PARAM_NN_STR const char *pchKey, ContainerID uLockID, SA_PARAM_NN_VALID StoreAccountKeyPurchaseData *pPurchaseData)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pPurchaseData->uEntID);
	Entity *pTargetEnt = entFromContainerIDAnyPartition(pPurchaseData->eTargetEntType, pPurchaseData->uTargetEntID);
	ContactDialog *pContactDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	PersistedStore *pPersistStore = pContactDialog ? GET_REF(pContactDialog->hPersistedStore) : NULL;
	StoreItemInfo *pStoreItemInfo = NULL;
	Item *pItemInstance = NULL;
	GameAccountDataExtract *pExtract = NULL;
	StoreItemCostInfoList CostInfoList = {0};
	int i = 0;

	// Any failure case in this function has to rollback the key lock
	if (!pEnt || !pEnt->pPlayer || !pContactDialog) {
		if (uLockID)
			AutoTrans_AccountProxy_tr_RollbackLock(NULL, GLOBALTYPE_ACCOUNTPROXYSERVER, GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, uLockID, uAccountID, pchKey);
		goto end;
	}

	// Except this kind of failure, because it means there is no key lock
	if (eResult != AKV_SUCCESS) {
		ClientCmd_NotifySend(pEnt, kNotifyType_ItemMoveFailed, entTranslateMessageKey(pEnt, "Store.AccountKeyFail"), NULL, NULL);
		pEnt->pPlayer->pInteractInfo->bPurchaseInProgress = false;
		goto end;
	}

	// Search the store item list for the StoreItemInfo
	for (i = 0; i < eaSize(&pContactDialog->eaStoreItems); i++) {
		if (!stricmp(pContactDialog->eaStoreItems[i]->pchStoreName, pPurchaseData->pchStoreName) && (U32)pContactDialog->eaStoreItems[i]->index == pPurchaseData->uStoreItemIndex) {
			pStoreItemInfo = pContactDialog->eaStoreItems[i];
			break;
		}
	}

	if (!pStoreItemInfo) {
		AutoTrans_AccountProxy_tr_RollbackLock(NULL, GLOBALTYPE_ACCOUNTPROXYSERVER, GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, uLockID, uAccountID, pchKey);
		pEnt->pPlayer->pInteractInfo->bPurchaseInProgress = false;
		goto end;
	}

	CostInfoList.eaCostInfo = pStoreItemInfo->eaCostInfo;
	CostInfoList.iStoreItemCount = MAX(SAFE_MEMBER(pPurchaseData->pStoreItemDef, iCount), 1);
	
	pPurchaseData->pCBData->uLockID = uLockID;
	pPurchaseData->pCBData->pchCurrencyName = allocAddString(pchKey);

	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

	if (pStoreItemInfo->bIsFromPersistedStore)
	{
		pItemInstance = pStoreItemInfo->pOwnedItem;
	}

	store_tr_BuyItem(pEnt, pTargetEnt, pPurchaseData->iCount, pPersistStore, pPurchaseData->pStoreDef,
		pPurchaseData->pStoreItemDef, pStoreItemInfo, pPurchaseData->pItemDef, pPurchaseData->pActualItemDef,
		pItemInstance, pchKey, CostInfoList, pPurchaseData->bForceBind, pExtract,
		pPurchaseData->provisioningProjectContainerType, pPurchaseData->provisioningProjectContainerID,
		pPurchaseData->extraArgs, pPurchaseData->pCBData, uLockID);

end:
	if (pPurchaseData->extraArgs)
	{
		StructDestroy(parse_StoreBuyExtraArgs, pPurchaseData->extraArgs);
	}

	free(pPurchaseData);
}

// Buy an item from a Store
// If the item is from a persisted store, then uStoreItemIndex refers to the ID assigned by the store
static void store_BuyItemEx(Entity *pEnt, Entity* pTargetEnt, const char *pchStoreName, U32 uStoreItemIndex, S32 iCount, bool bRemoveItems, StoreResearchState eResearchState)
{
	ContactDialog *pContactDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	ContactDef *pContactDef = pContactDialog?GET_REF(pContactDialog->hContactDef):NULL;
	PersistedStore *pPersistStore = pContactDialog ? GET_REF(pContactDialog->hPersistedStore) : NULL;
	StoreItemInfo *pStoreItemInfo = NULL;
	StoreDef *pStoreDef = NULL;
	StoreItemDef *pStoreItemDef = NULL;
	ItemDef *pItemDef = NULL;
	ItemDef *pActualItemDef = NULL;
	ItemDef *pCurrencyDef = NULL;
	Item *pItemInstance = NULL;
	const char *pcCurrencyName = NULL;
	MissionDef *pReqMission = NULL;
	StoreItemCostInfoList CostInfoList = {0};
	U32 uCurrentTime = timeSecondsSince2000();
	S32 iEPValue = 0;
	S32 i, iMaxBuyCount;
	StoreBuyItemCBData *pData = NULL;
	char* estrNotification = NULL;
	bool bForceBind = false;
	GameAccountDataExtract *pExtract;
	int iPartitionIdx;
	ItemChangeReason reason = {0};
    S32 iProvisionAvailable = 0;
    GlobalType provisioningProjectContainerType = GLOBALTYPE_NONE;
    ContainerID provisioningProjectContainerID = 0;
    StoreBuyExtraArgs *extraArgs = NULL;
	S32 iRealBuyCount;							// The buy count that does not include the quantity of recipe store items
	bool bAccountKeyValuePurchase = false;
	
	if (!pContactDef || !pEnt || !pEnt->pPlayer) {
		return;
	}
	
	if (stricmp(REF_STRING_FROM_HANDLE(pContactDialog->hPersistStoreDef), pchStoreName)==0) {
		if (!pPersistStore) {
			return;
		}
		pStoreDef = GET_REF(pPersistStore->hStoreDef);
	} else {
		// Search for store in normal store earray
		for (i = 0; i < eaSize(&pContactDef->stores); i++) {
			StoreDef *pCurrentStore = GET_REF(pContactDef->stores[i]->ref);
			if (pCurrentStore && !stricmp(pCurrentStore->name, pchStoreName)){
				pStoreDef = pCurrentStore;
				pStoreItemDef = eaGet(&pStoreDef->inventory, uStoreItemIndex);
				break;
			}
		}

		// If store not found, look for it in a store collection
		if(!pStoreDef && pContactDef->storeCollections && pContactDialog->iCurrentStoreCollection >= 0 && pContactDialog->iCurrentStoreCollection < eaSize(&pContactDef->storeCollections))
		{
			StoreCollection *pCollection = pContactDef->storeCollections[pContactDialog->iCurrentStoreCollection];
			if(pCollection)
			{
				for (i = 0; i < eaSize(&pCollection->eaStores); i++) {
					StoreDef *pCurrentStore = GET_REF(pCollection->eaStores[i]->ref);
					if (pCurrentStore && !stricmp(pCurrentStore->name, pchStoreName)){
						pStoreDef = pCurrentStore;
						pStoreItemDef = eaGet(&pStoreDef->inventory, uStoreItemIndex);
						break;
					}
				}
			}
		}
	}
	// Search the store item list for the StoreItemInfo
	for (i = 0; i < eaSize(&pContactDialog->eaStoreItems); i++) {
		if (!stricmp(pContactDialog->eaStoreItems[i]->pchStoreName, pchStoreName) && (U32)pContactDialog->eaStoreItems[i]->index == uStoreItemIndex) {
			pStoreItemInfo = pContactDialog->eaStoreItems[i];
			break;
		}
	}
	
	if (!pStoreDef || !pStoreItemInfo || !pStoreItemInfo->pOwnedItem) {
		return;
	}

	if (pStoreItemDef && GET_REF(pStoreItemDef->hReqMicroTransaction)) {
		return;
	}

	if (pContactDialog->uStoreResearchTimeExpire) {
		if (eResearchState == kStoreResearchState_Start) {
			return;
		} else if (eResearchState == kStoreResearchState_Finish) {
			if (stricmp(pContactDialog->pchResearchTimerStoreName, pchStoreName)!=0 ||
				pContactDialog->uResearchTimerStoreItemIndex != uStoreItemIndex || 
				pContactDialog->uStoreResearchTimeExpire > uCurrentTime) 
			{
				return;
			}
		}
	}

	iMaxBuyCount = MAX(pStoreItemInfo->iMayBuyInBulk, 1);
	iCount = MIN(iCount, iMaxBuyCount);
	iRealBuyCount = iCount;		// Set the correct buy count
	
	if (pStoreDef->pExprRequires)
	{
		MultiVal answer = {0};
		store_BuyContextSetup(pEnt, pItemDef);
		exprEvaluate(pStoreDef->pExprRequires, store_GetBuyContext(), &answer);
		if(answer.type == MULTI_INT)
		{
			if (!QuickGetInt(&answer))
			{
				return;
			}
		}
	}

    iPartitionIdx = entGetPartitionIdx(pEnt);

    // Current map must be guild owned, and the player must be a member of that guild in order to access this store.
    if ( pStoreDef->bGuildMapOwnerMembersOnly )
    {
        // Map must be guild owned.
        if ( partition_OwnerTypeFromIdx(iPartitionIdx) != GLOBALTYPE_GUILD )
        {
            Errorf("Store %s is flagged bGuildMapOwnerMembersOnly and is not on a guild map.", pStoreDef->name);
            return;
        }

        // The player must be a guild member.
        if ( !guild_IsMember(pEnt) )
        {
            ClientCmd_NotifySend(pEnt, kNotifyType_ItemBuyFailed, entTranslateMessageKey(pEnt, "Store.NotGuildMember"), NULL, NULL);
            return;
        }

        // The map owner ID must match the player's guild ID.
        if ( partition_OwnerIDFromIdx(iPartitionIdx) != guild_GetGuildID(pEnt) )
        {
            ClientCmd_NotifySend(pEnt, kNotifyType_ItemBuyFailed, entTranslateMessageKey(pEnt, "Store.NotGuildMember"), NULL, NULL);
            return;
        }
    }

	if (pStoreItemDef && pStoreItemDef->iCount) {
		iCount *= pStoreItemDef->iCount;
	}

	if (pStoreItemDef) {
		pItemDef = GET_REF(pStoreItemDef->hItem);
		bForceBind = pStoreItemDef->bForceBind;
	} else {
		pItemDef = GET_REF(pStoreItemInfo->pOwnedItem->hItem);
	}
	
	if (!pItemDef) {
		return;
	}
	
	if (pItemDef->eType == kItemType_ItemValue) {
		pActualItemDef = pItemDef->pCraft ? GET_REF(pItemDef->pCraft->hItemResult) : NULL;
		if (!pActualItemDef) {
			return;
		}
	} else {
		pActualItemDef = pItemDef;
	}
	
	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

	if (!store_CanBuyItem(pEnt, pStoreDef, pStoreItemDef, pStoreItemInfo, pItemDef, iCount, bRemoveItems, &estrNotification, NULL, pExtract)){
		if (estrNotification) {
			ClientCmd_NotifySend(pEnt, kNotifyType_ItemBuyFailed, estrNotification, NULL, NULL);
			estrDestroy(&estrNotification);
		}
		return;
	}

    // Re-calculate the discounts just in case they changed since the store interaction began.
    for (i = 0; i < eaSize(&pStoreItemInfo->eaCostInfo); i++)
    {
        store_ApplyDiscountToCostInfo(pEnt, pStoreDef, pItemDef, pStoreItemInfo->eaCostInfo[i]);
    }

	//Check to see if we can afford the item;
	for (i = 0; i < eaSize(&pStoreItemInfo->eaCostInfo); i++)
	{
		StoreItemCostInfo *pCostInfo = pStoreItemInfo->eaCostInfo[i];
		int iHave = 0;

		pCurrencyDef = pCostInfo?GET_REF(pCostInfo->hItemDef):NULL;
		if (pCurrencyDef){
			if (pCurrencyDef->eType == kItemType_Numeric) {
				iHave = inv_GetNumericItemValue(pEnt, pCurrencyDef->pchName);
			} else {
				iHave = store_GetPlayerItemCount(pEnt, pCurrencyDef->pchName);
			}
		}
		else if (pCostInfo && pCostInfo->pPriceAccountKeyValue) {
			// If there's a key-value cost here, it must have been generated because the store item
			// has an override key-value price. This means we'll have to lock an AS key-value for this purchase.
			iHave = gad_GetAccountValueIntFromExtract(pExtract, pCostInfo->pPriceAccountKeyValue);
			pcCurrencyName = pStoreItemDef->pPriceAccountKeyValue;
			bAccountKeyValuePurchase = true;
		}

		if (pCostInfo)
		{
			// iRealBuyCount is the quantity to buy that was passed into this function. As recipe items already increased the count if iCount is used the check value will be too high.
			if (pCostInfo->iCount * iRealBuyCount > iHave)
			{
				ItemDef* pCostItemDef = GET_REF(pCostInfo->hItemDef);
				if (pCostItemDef)
				{
					if(!estrNotification)
						estrCreate(&estrNotification);
					entFormatGameMessageKey(pEnt, &estrNotification, STORE_INSUFFICIENT_FUNDS_MSG, STRFMT_DISPLAYMESSAGE("Currency", pCostItemDef->displayNameMsg), STRFMT_END);
					ClientCmd_NotifySend(pEnt, kNotifyType_ItemBuyFailed, estrNotification, NULL, NULL);
					estrDestroy(&estrNotification);
				}
				return;
			}
		}
	}
	
	iEPValue = item_GetStoreEPValue(iPartitionIdx, pEnt, pStoreItemInfo->pOwnedItem, pStoreDef);

	if (!(eaSize(&pItemDef->ppValueRecipes) > 0 || 
		pItemDef->eType == kItemType_ItemValue || 
		(pStoreItemDef && eaSize(&pStoreItemDef->ppOverrideValueRecipes) > 0) ||
		(pStoreItemDef && pStoreItemDef->pPriceAccountKeyValue)) || 
		(pStoreItemDef && pStoreItemDef->bForceUseCurrency)) 
	{
		pCurrencyDef = GET_REF(pStoreDef->hCurrency);
		if (!pCurrencyDef) {
			return;
		}
		pcCurrencyName = pCurrencyDef->pchName;
	}

	// Only one purchase is allowed at a time
	if (pEnt->pPlayer->pInteractInfo->bPurchaseInProgress) {
		return;
	}
	// If the store item has a research time, then wait to purchase the item
	if (pStoreItemInfo->uResearchTime > 0 && !bRemoveItems) {
		if (!pContactDialog->uStoreResearchTimeExpire) {
			if (eResearchState == kStoreResearchState_Start) {
				U32 uResearchTime = pStoreItemInfo->uResearchTime;
				pContactDialog->uStoreResearchTimeExpire = uCurrentTime + uResearchTime;
				pContactDialog->pchResearchTimerStoreName = allocAddString(pchStoreName);
				pContactDialog->uResearchTimerStoreItemIndex = uStoreItemIndex;
				pContactDialog->bIsResearching = true;
				entity_SetDirtyBit(pEnt, parse_InteractInfo, pEnt->pPlayer->pInteractInfo, true);
				ClientCmd_gclStore_SetResearchTime(pEnt, uResearchTime);
			}
			return;
		} else {
			if (eResearchState == kStoreResearchState_Finish) {
				pContactDialog->uStoreResearchTimeExpire = 0;
				pContactDialog->pchResearchTimerStoreName = NULL;
				pContactDialog->uResearchTimerStoreItemIndex = 0;
				pContactDialog->bIsResearching = false;
				entity_SetDirtyBit(pEnt, parse_InteractInfo, pEnt->pPlayer->pInteractInfo, true);
			} else {
				return;
			}
		}
	} else if (eResearchState != kStoreResearchState_None) {
		return;
	}

    if ( pStoreDef->bProvisionFromGroupProject && ( pStoreDef->eGroupProjectType == GroupProjectType_Guild ) )
    {
        Guild* pGuild;
        GuildMember *pMember;

        // Can't buy from a provisioned guild store if not a guild member.
        if ( !guild_IsMember(pEnt) )
        {
            return;
        }

        pGuild = guild_GetGuild(pEnt);
        if ( pGuild == NULL )
        {
            return;
        }

        pMember = guild_FindMemberInGuild(pEnt, pGuild);
        if ( pMember == NULL )
        {
            return;
        }

        // Make sure the player has permission to buy from provisioned guild stores.
        if ( !guild_HasPermission(pMember->iRank, pGuild, GuildPermission_BuyProvisioned ) )
        {
            ClientCmd_NotifySend(pEnt, kNotifyType_ItemBuyFailed, entTranslateMessageKey(pEnt, "Store.NoProvisionedBuyPermission"), NULL, NULL);
            return;
        }
    }

    if ( pPersistStore || pStoreDef->bProvisionFromGroupProject || pStoreItemDef )
    {
        extraArgs = StructCreate(parse_StoreBuyExtraArgs);
		SET_HANDLE_FROM_REFERENT(g_StoreDictionary, pStoreDef, extraArgs->hStoreDef);
		extraArgs->uStoreItemIndex = uStoreItemIndex;
    }

    if ( pStoreDef->bProvisionFromGroupProject )
    {
        GroupProjectDef *projectDef = GET_REF(pStoreDef->provisioningProjectDef);
        GroupProjectNumericDef *numericDef = GET_REF(pStoreDef->provisioningNumericDef);

        extraArgs->bProvisioned = true;

        if ( projectDef == NULL || numericDef == NULL )
        {
            iProvisionAvailable = 0;
            Errorf("Can't find GroupProjectDef or GroupProjectNumericDef for provisioned store.");
        }
        else
        {
            iProvisionAvailable = gslGroupProject_GetGroupProjectNumericFromPlayer(pEnt, pStoreDef->eGroupProjectType, projectDef->name, numericDef->name);
            extraArgs->provisioningProjectName = projectDef->name;
            extraArgs->provisioningNumericName = numericDef->name;
        }

        if ( iProvisionAvailable == 0 )
        {
            ClientCmd_NotifySend(pEnt, kNotifyType_ItemBuyFailed, entTranslateMessageKey(pEnt, "Store.NotProvisioned"), NULL, NULL);
            StructDestroy(parse_StoreBuyExtraArgs, extraArgs);
            return;
        }

        if ( iCount > iProvisionAvailable )
        {
            iCount = iProvisionAvailable;
        }

        // Get the container ID and type for the provisioning group project container, which must be passed to 
        //  the buy transaction if provisioning is enabled for the store.
        provisioningProjectContainerType = GroupProject_ContainerTypeForProjectType(pStoreDef->eGroupProjectType);
        provisioningProjectContainerID = GroupProject_ContainerIDForProjectType(pEnt, pStoreDef->eGroupProjectType);
    }

    // Warning, don't add code that returns without calling the transaction after bPurchaseInProgress is true.
	pEnt->pPlayer->pInteractInfo->bPurchaseInProgress = true;

	pData = calloc(1, sizeof(StoreBuyItemCBData));
	pData->uEntID = pEnt->myContainerID;
	pData->pchItemName = pItemDef?pItemDef->pchName:NULL;
	pData->pchStoreName = pStoreDef?pStoreDef->name:NULL;
	pData->pchContactName = pContactDef?pContactDef->name:NULL;
	pData->iCount = iCount;
	pData->iEPValue = iEPValue;

	CostInfoList.eaCostInfo = pStoreItemInfo->eaCostInfo;
	CostInfoList.iStoreItemCount = MAX(SAFE_MEMBER(pStoreItemDef, iCount), 1);

	if (pStoreItemInfo->bIsFromPersistedStore)
	{
		pItemInstance = pStoreItemInfo->pOwnedItem;
		if (pItemInstance)
		{
			item_trh_ResetPowerLifetimes(CONTAINER_NOCONST(Item, pItemInstance));
		}
	}

	// If the purchase is with an Account Server key-value, we have to branch here to ask for a lock before doing the purchase
	if (bAccountKeyValuePurchase) {
		StoreAccountKeyPurchaseData *pPurchaseData = callocStruct(StoreAccountKeyPurchaseData);
		pPurchaseData->uEntID = pEnt->myContainerID;
		pPurchaseData->eTargetEntType = pTargetEnt ? pTargetEnt->myEntityType : GLOBALTYPE_NONE;
		pPurchaseData->uTargetEntID = pTargetEnt ? pTargetEnt->myContainerID : 0;
		pPurchaseData->pchStoreName = allocAddString(pchStoreName);
		pPurchaseData->uStoreItemIndex = uStoreItemIndex;
		pPurchaseData->iCount = iCount;

		pPurchaseData->pStoreDef = pStoreDef;
		pPurchaseData->pStoreItemDef = pStoreItemDef;
		pPurchaseData->pItemDef = pItemDef;
		pPurchaseData->pActualItemDef = pActualItemDef;
		pPurchaseData->bForceBind = bForceBind;
		pPurchaseData->provisioningProjectContainerType = provisioningProjectContainerType;
		pPurchaseData->provisioningProjectContainerID = provisioningProjectContainerID;
		pPurchaseData->extraArgs = extraArgs;
		pPurchaseData->pCBData = pData;
		
		APChangeKeyValue(entGetAccountID(pEnt), pcCurrencyName, -1, store_BuyItemAccountKey_CB, pPurchaseData);
		return;
	}

	store_tr_BuyItem(pEnt, pTargetEnt, iCount, pPersistStore, pStoreDef, pStoreItemDef, pStoreItemInfo,
		pItemDef, pActualItemDef, pItemInstance, pcCurrencyName, CostInfoList, bForceBind, pExtract,
		provisioningProjectContainerType, provisioningProjectContainerID, extraArgs, pData, 0);

    if ( extraArgs )
    {
		StructDestroy(parse_StoreBuyExtraArgs, extraArgs);
    }
}

// Buy an item from a Store
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Inventory) ACMD_PRIVATE;
void store_BuyItem(Entity *pEnt, const char *pchStoreName, U32 uStoreItemIndex, S32 iCount)
{
	store_BuyItemEx(pEnt, NULL, pchStoreName, uStoreItemIndex, iCount, false, kStoreResearchState_None);
}

// Buy an item from an Injury Store
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Inventory) ACMD_PRIVATE;
void store_BuyAndRemoveItem(Entity *pEnt, U32 uiTargetEntType, U32 uiTargetEntID, const char *pchStoreName, U32 uStoreItemIndex, S32 iCount)
{
	Entity* pTargetEnt = entity_GetSubEntity(entGetPartitionIdx(pEnt), pEnt, uiTargetEntType, uiTargetEntID);

	if(pEnt && pTargetEnt && pTargetEnt->pSaved)
		store_BuyItemEx(pEnt, pTargetEnt, pchStoreName, uStoreItemIndex, iCount, true, kStoreResearchState_None);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Inventory) ACMD_HIDE;
void store_CancelResearch(Entity *pEnt)
{
	ContactDialog *pContactDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	if (pContactDialog && pContactDialog->bIsResearching)
	{
		pContactDialog->uStoreResearchTimeExpire = 0;
		pContactDialog->pchResearchTimerStoreName = NULL;
		pContactDialog->uResearchTimerStoreItemIndex = 0;
		pContactDialog->bIsResearching = false;
		entity_SetDirtyBit(pEnt, parse_InteractInfo, pEnt->pPlayer->pInteractInfo, kStoreResearchState_Finish);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Inventory) ACMD_HIDE ACMD_PRODUCTS(StarTrek);
void store_StartResearch(Entity *pEnt, const char* pchStoreName, U32 uStoreItemIndex)
{
	ContactDialog *pContactDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	if (pContactDialog && !pContactDialog->bIsResearching)
	{
		store_BuyItemEx(pEnt, NULL, pchStoreName, uStoreItemIndex, 1, false, kStoreResearchState_Start);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Inventory) ACMD_PRIVATE;
void store_FinishResearch(Entity *pEnt)
{
	ContactDialog *pContactDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	if (pContactDialog && pContactDialog->bIsResearching)
	{
		const char* pchStoreName = pContactDialog->pchResearchTimerStoreName;
		U32 uStoreItemIndex = pContactDialog->uResearchTimerStoreItemIndex;
		store_BuyItemEx(pEnt, NULL, pchStoreName, uStoreItemIndex, 1, false, kStoreResearchState_Finish);
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Inventory) ACMD_PRIVATE;
void injuryStore_SetTarget(Entity* pPlayerEnt, U32 uiTargetEntType, U32 uiTargetEntID)
{
	int iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
	Entity* pTargetEnt = entity_GetSubEntity(iPartitionIdx, pPlayerEnt, uiTargetEntType, uiTargetEntID);
	if(pPlayerEnt && pPlayerEnt->pPlayer && pPlayerEnt->pPlayer->pInteractInfo && pTargetEnt)
	{
		// Make sure the player is interacting with an injury store
		ContactDialog* pDialog = pPlayerEnt->pPlayer->pInteractInfo->pContactDialog;
		if(pDialog && pDialog->state == ContactDialogState_InjuryStore)
		{
			// Make sure the target is within range
			bool bInRange = false;
			if(entity_IsOwnerSame(CONTAINER_NOCONST(Entity, pPlayerEnt), CONTAINER_NOCONST(Entity, pTargetEnt))) {
				bInRange = true;
			} else {
				F32 fDist = 0;
				F32 fRange = 0;

				fDist = entGetDistance(pPlayerEnt, NULL, pTargetEnt, NULL, NULL);
				fRange = gslEntity_GetInteractRange(pPlayerEnt, pTargetEnt, NULL);

				bInRange = ( fDist <= fRange );
			}

			if(bInRange)
			{
				// Create the dialog option
				ContactDialogOption* pNewOption = StructCreate(parse_ContactDialogOption);
				pNewOption->eType = ContactIndicator_InjuryHealer;
				pNewOption->pchKey = StructAllocString("OptionsList.InjuryStore");
				pNewOption->pData = StructCreate(parse_ContactDialogOptionData);
				COPY_HANDLE(pNewOption->pData->hTargetContactDef, pDialog->hContactDef);
				pNewOption->pData->iEntID = uiTargetEntID;
				pNewOption->pData->iEntType = uiTargetEntType;
				pNewOption->pData->targetState = ContactDialogState_InjuryStore;
				eaPush(&pDialog->eaOptions, pNewOption);
				contact_InteractResponse(pPlayerEnt, "OptionsList.InjuryStore", NULL, 0, false);
			}
		}
	}
}

bool store_UpdateSellItemInfo(Entity* pEnt, StoreDef* pStore, StoreSellableItemInfo*** peaSellableItems, GameAccountDataExtract *pExtract)
{
	StoreSellableItemInfo* pItemInfo;
	S32 iBagIdx, iSlotIdx, iInvIdx;
	S32 iCost, iCurrencyValue;
	S32 iNumItems = 0;
	int iPartitionIdx;
	ItemDef* pCurrencyDef;

	PERFINFO_AUTO_START_FUNC();
	
	pCurrencyDef = pStore ? GET_REF(pStore->hCurrency) : NULL;

	if (!pEnt || !pEnt->pInventoryV2 || !pCurrencyDef)
	{
		eaClearStruct(peaSellableItems, parse_StoreSellableItemInfo);
		PERFINFO_AUTO_STOP();
		return false;
	}

	iPartitionIdx = entGetPartitionIdx(pEnt);

	for (iBagIdx = eaSize(&pEnt->pInventoryV2->ppInventoryBags)-1; iBagIdx >= 0; iBagIdx--) 
	{
		InventoryBag *pInvBag = pEnt->pInventoryV2->ppInventoryBags[iBagIdx];
		int iNumSlots;
		
		// Check that we can sell out of this bag
		if (!(invbag_flags(pInvBag) & InvBagFlag_SellEnabled)) {
			continue;
		}
		// Make sure the bag is not empty
		if (inv_ent_BagEmpty(pEnt, invbag_bagid(pInvBag), pExtract)) {
			continue;
		}

		// Iterate through each of the bag's slots
		iNumSlots = inv_ent_GetMaxSlots(pEnt, invbag_bagid(pInvBag), pExtract);
		for (iSlotIdx = 0; iSlotIdx < iNumSlots; iSlotIdx++) {
			InventorySlot *pInvSlot = pInvSlot = inv_ent_GetSlotPtr(pEnt, invbag_bagid(pInvBag), iSlotIdx, pExtract);
			Item *pItem = pInvSlot ? pInvSlot->pItem : NULL;
			ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
			bool bSellable = pItemDef ? (!(pItemDef->flags & kItemDefFlag_CantSell) && pItemDef->eType != kItemType_Mission && pItemDef->eType != kItemType_MissionGrant) : false;

			// If store is a sellable_items store, iterate through the possible items to make sure it's on the list
			if(bSellable && pStore->eContents == Store_Sellable_Items)
			{
				for(iInvIdx = eaSize(&pStore->inventory)-1; iInvIdx >= 0; iInvIdx--)
				{
					if(GET_REF(pStore->inventory[iInvIdx]->hItem) == pItemDef)
						break;
				}

				if(iInvIdx < 0)
					bSellable = false;
			}
			
			// Make sure the item is sellable
			if (!bSellable) {
				continue;
			}

			// Get or create a new Item Info struct
			pItemInfo = eaGetStruct(peaSellableItems, parse_StoreSellableItemInfo, iNumItems++);

			if (pItemDef->eType == kItemType_Numeric) {
				pItemInfo->iCount = inv_GetNumericItemValue(pEnt, pItemDef->pchName);
			} else {
				pItemInfo->iCount = pItem->count;
			}
			pItemInfo->iBagID = pInvBag->BagID;
			pItemInfo->iSlot = iSlotIdx;
			pItemInfo->uItemID = pItem->id;
			
			iCurrencyValue = item_GetDefEPValue(iPartitionIdx, pEnt, pCurrencyDef, pCurrencyDef->iLevel, pCurrencyDef->Quality);
			iCost = item_GetStoreEPValue(iPartitionIdx, pEnt, pItem, pStore) * pStore->fSellMultiplier * ITEM_SELL_MULTIPLIER;
			if (iCurrencyValue) {
				iCost /= iCurrencyValue;
			}

			//Set the cost
			pItemInfo->iCost = iCost;
		}
	}
	while (eaSize(peaSellableItems) > iNumItems){
		StructDestroy(parse_StoreSellableItemInfo, eaPop(peaSellableItems));
	}

	PERFINFO_AUTO_STOP();
	return true;
}

void store_Close(Entity* pPlayerEnt)
{
	if(!gConf.bAllowBuybackUntilMapMove && pPlayerEnt)
	{
		InteractInfo *pInteractInfo = SAFE_MEMBER2(pPlayerEnt, pPlayer, pInteractInfo);

		if(pInteractInfo) {
			eaDestroyStruct(&pInteractInfo->eaBuyBackList, parse_StoreItemInfo);
			entity_SetDirtyBit(pPlayerEnt, parse_InteractInfo, pInteractInfo, true);
		}

		// remove all buyback store items
		if(pPlayerEnt->pPlayer)
		{
			eaDestroyStruct(&pPlayerEnt->pPlayer->eaItemBuyBackList, parse_ItemBuyBack);
			pPlayerEnt->pPlayer->uBuyBackTime = 0;
			entity_SetDirtyBit(pPlayerEnt, parse_InteractInfo, pInteractInfo, true);
		}
	}
}

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
// Persisted Stores
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

static void gslPersistedStore_RequestContainerData_CB(TransactionReturnVal* pReturn, ContainerID* puID)
{
	U32 uStoreID;
	enumTransactionOutcome eOutcome;
	Entity* pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, *puID);
	ContactDialog* pContactDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);

	eOutcome = RemoteCommandCheck_aslPersistedStore_PlayerAddRequest(pReturn, &uStoreID);
	
	if (eOutcome != TRANSACTION_OUTCOME_SUCCESS || !uStoreID)
	{
		SAFE_FREE(puID);
		return;
	}
	if (pContactDialog)
	{
		char idBuf[128];
		SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_PERSISTEDSTORE), ContainerIDToString(uStoreID,idBuf), pContactDialog->hPersistedStore);
	}
	else
	{
		RemoteCommand_aslPersistedStore_PlayerRemoveRequestByID(GLOBALTYPE_AUCTIONSERVER, 0, *puID, uStoreID);
	}
	SAFE_FREE(puID);
}

void gslPersistedStore_PlayerAddRequest(Entity* pEnt, StoreDef* pStoreDef)
{
	U32 bHadPersistedStore = false;
	ContainerID* puID = NULL;
	ContactDialog* pContactDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	if (!pContactDialog || !pStoreDef || !pStoreDef->bIsPersisted)
	{
		return;
	}
	if (pStoreDef == GET_REF(pContactDialog->hPersistStoreDef))
	{
		return;
	}
	if (GET_REF(pContactDialog->hPersistStoreDef))
	{
		bHadPersistedStore = true;
	}
	SET_HANDLE_FROM_REFERENT("Store", pStoreDef, pContactDialog->hPersistStoreDef);
	puID = calloc(1, sizeof(ContainerID));
	*puID = entGetContainerID(pEnt);
	RemoteCommand_aslPersistedStore_PlayerAddRequest(
		objCreateManagedReturnVal(gslPersistedStore_RequestContainerData_CB, puID),
		GLOBALTYPE_AUCTIONSERVER, 0,
		*puID, pStoreDef->name, bHadPersistedStore);
}

void gslPersistedStore_PlayerRemoveRequests(Entity* pEnt)
{
	ContactDialog* pContactDialog;
	ContainerID uID;
	if (!pEnt)
	{
		return;
	}
	pContactDialog = SAFE_MEMBER2(pEnt->pPlayer, pInteractInfo, pContactDialog);
	if (pContactDialog)
	{
		REMOVE_HANDLE(pContactDialog->hPersistStoreDef);
		REMOVE_HANDLE(pContactDialog->hPersistedStore);
	}
	uID = entGetContainerID(pEnt);
	RemoteCommand_aslPersistedStore_PlayerRemoveAllRequests(GLOBALTYPE_AUCTIONSERVER, 0, uID);	
}

bool gslPersistedStore_UpdateItemInfo(Entity* pEnt, StoreItemInfo ***peaItemInfo)
{
	ContactDialog* pContactDialog;
	ContactDef* pContactDef;
	PersistedStore* pPersistStore;
	StoreDef* pStoreDef;

	PERFINFO_AUTO_START_FUNC();
	
	pContactDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	pContactDef = pContactDialog ? GET_REF(pContactDialog->hContactDef) : NULL;
	pPersistStore = pContactDialog ? GET_REF(pContactDialog->hPersistedStore) : NULL;
	pStoreDef = pPersistStore ? GET_REF(pPersistStore->hStoreDef): NULL;

	if (pStoreDef && pStoreDef->bIsPersisted &&
		pContactDialog->uLastPersistedStoreVersion != pPersistStore->uVersion)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		S32 i;

		for (i = eaSize(peaItemInfo)-1; i >= 0; i--)
		{
			StoreItemInfo* pItemInfo = (*peaItemInfo)[i];
			if (pItemInfo->bIsFromPersistedStore)
			{
				StructDestroy(parse_StoreItemInfo, eaRemove(peaItemInfo, i));
			}
		}
		for (i = 0; i < eaSize(&pPersistStore->eaInventory); i++)
		{
			StoreItemInfo* pItemInfo = store_SetupStoreItemInfoEx(pEnt, pContactDef, pStoreDef, pPersistStore, i, pExtract);
			if (pItemInfo)
			{
				eaPush(peaItemInfo, pItemInfo);
			}
		}
		pContactDialog->uLastPersistedStoreVersion = pPersistStore->uVersion;

		PERFINFO_AUTO_STOP();
		return true;
	}

	PERFINFO_AUTO_STOP();
	return false;
}


AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
bool gslPersistedStore_VerfiyPlayerRequest(U32 uPlayerID, const char* pchStoreName)
{
	Entity* pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, uPlayerID);
	ContactDialog* pContactDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	if (pContactDialog)
	{
		const char* pchPersistStore = REF_STRING_FROM_HANDLE(pContactDialog->hPersistStoreDef);
		if (stricmp(pchPersistStore, pchStoreName)==0)
		{
			return true;
		}
	}
	return false;
}
