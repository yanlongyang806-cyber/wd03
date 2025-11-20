/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "ItemAssignmentsUICommon.h"

#include "contact_common.h"
#include "Entity.h"
#include "file.h"
#include "GameAccountDataCommon.h"
#include "GameStringFormat.h"

#include "inventoryCommon.h"
#include "ItemAssignments.h"
#include "ItemCommon.h"
#include "itemCommon_h_ast.h"
#include "ItemEnums.h"
#include "itemEnums_h_ast.h"
#include "MemoryPool.h"
#include "mission_common.h"
#include "Player.h"
#include "StringCache.h"

#include "StringUtil.h"
#include "strings_opt.h"
#include "cmdparse.h"
#include "ResourceManager.h"

//#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "ItemAssignments_h_ast.h"
//#include "ItemAssignmentsUICommon_c_ast.h"
#include "ItemAssignmentsUICommon_h_ast.h"
//#include "AutoGen/FCInventoryUI_h_ast.h"

#ifdef GAMECLIENT
//#include "gclEntity.h"
#include "gclUIGen.h"
#include "GameClientLib.h"
//#include "UIGen.h"
//#include "GameClientLib.h"
//#include "GraphicsLib.h"
//#include "FCInventoryUI.h"
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

// Not making this an auto-struct since it'd have to use a custom destructor
// the list anyway.
typedef struct ItemList
{
	// The set of all items that may be used
	Item **eaItems;
	// The set of all items that fail item_trh_CanRemoveItem()
	Item **eaNotRemovable;
	// The set of all items that fail on ItemAssignment_trh_CanSlottedItemResideInBag()
	Item **eaBadBag;
} ItemList;

static void ItemList_Reset(ItemList *pItemList)
{
	eaClearFast(&pItemList->eaItems);
	eaClearFast(&pItemList->eaNotRemovable);
	eaClearFast(&pItemList->eaBadBag);
}

static void ItemList_Destroy(ItemList **ppItemList)
{
	if (ppItemList && *ppItemList)
	{
		eaDestroy(&(*ppItemList)->eaItems);
		eaDestroy(&(*ppItemList)->eaNotRemovable);
		eaDestroy(&(*ppItemList)->eaBadBag);
		free(*ppItemList);
		*ppItemList = NULL;
	}
}

// NB: Inspected items are items in bags that are evaluated as possible
// but are unusable for some undefined reason.
static S32 *s_peItemListBag;
static Entity* s_pItemListEnt;
static U32 s_uItemListTime;
static ItemList s_ItemListUniverse;
static ItemList **s_eaItemLists;

ItemAssignmentCompletedDetails* ItemAssignment_FindPossibleCompletedDetails(SA_PARAM_NN_VALID ItemAssignmentPersistedData* pPlayerData, 
	const ItemAssignment *pAssignment)
{
	S32 i;

	if (!pAssignment)
		return NULL;

	for (i=eaSize(&pPlayerData->eaRecentlyCompletedAssignments)-1; i>=0; --i)
	{
		ItemAssignmentCompletedDetails* pCompleteDetails = pPlayerData->eaRecentlyCompletedAssignments[i];
		if ((pCompleteDetails->uAssignmentID == 0
			&& REF_COMPARE_HANDLES(pCompleteDetails->hDef, pAssignment->hDef)
			&& pCompleteDetails->uTimeStarted == pAssignment->uTimeStarted
			&& pCompleteDetails->uDuration == pAssignment->uDuration
			&& pCompleteDetails->pchOutcome == pAssignment->pchRewardOutcome
			&& pCompleteDetails->pchMapMsgKey == pAssignment->pchMapMsgKey
			) || pCompleteDetails->uAssignmentID == pAssignment->uAssignmentID)
		{
			return pCompleteDetails;
		}
	}

	return NULL;
}

ItemAssignment* ItemAssignment_FindPossibleActiveItemAssignment(SA_PARAM_NN_VALID ItemAssignmentPersistedData* pPlayerData, ItemAssignmentCompletedDetails *pCompleteDetails)
{
	S32 i;

	if (!pCompleteDetails)
		return NULL;

	for (i=eaSize(&pPlayerData->eaActiveAssignments)-1; i>=0; --i)
	{
		ItemAssignment* pAssignment = pPlayerData->eaActiveAssignments[i];
		if ((pCompleteDetails->uAssignmentID == 0
			&& REF_COMPARE_HANDLES(pCompleteDetails->hDef, pAssignment->hDef)
			&& pCompleteDetails->uTimeStarted == pAssignment->uTimeStarted
			&& pCompleteDetails->uDuration == pAssignment->uDuration
			&& pCompleteDetails->pchOutcome == pAssignment->pchRewardOutcome
			&& pCompleteDetails->pchMapMsgKey == pAssignment->pchMapMsgKey
			) || pCompleteDetails->uAssignmentID == pAssignment->uAssignmentID)
		{
			return pAssignment;
		}
	}

	return NULL;
}

const char* ItemAssignment_GetIconName(ItemAssignmentDef* pDef, ItemAssignmentCategorySettings* pCategory)
{
	if (pDef && pDef->pchIconName)
	{
		return pDef->pchIconName;
	}
	else if (pCategory && pCategory->pchIconName)
	{
		return pCategory->pchIconName;
	}
	return NULL;
}

const char* ItemAssignment_GetImageName(ItemAssignmentDef* pDef, ItemAssignmentCategorySettings* pCategory)
{
	if (pDef && pDef->pchImage)
	{
		return pDef->pchImage;
	}
	else if (pCategory && pCategory->pchImage)
	{
		return pCategory->pchImage;
	}
	return NULL;
}

// client vs. server time functions.
// Client is in milliseconds, server is counting in seconds. 
// However the client UI needs milliseconds
// We should fix this if possible so they are both using milliseconds.
static U32 ItemAssignment_GetElapsedTime()
{
#if GAMECLIENT
	return gGCLState.totalElapsedTimeMs;
#else
	return timeSecondsSince2000();
#endif
}

// converts from milliseconds to whatever the running process needs the time to be in
static S32 ItemAssignment_ConvertTime(S32 milliseconds)
{
#if GAMECLIENT
	return milliseconds;
#else
	// in seconds
	return milliseconds/1000;
#endif
}

// internal helper function to get all the ItemAssignmentOutcomeUI for the given ItemAssignmentDef using the currently slotted items
void ItemAssignment_GetUIOutcomes(	SA_PARAM_OP_VALID Entity* pEnt, 
									SA_PARAM_OP_VALID ItemAssignmentDef* pDef, 
									ItemAssignmentSlottedItem** eaSlottedItems,
									ItemAssignmentOutcomeUI*** peaOutcomeUI)
{
	S32 i, iCount = 0;

	if (pEnt && pDef)
	{
		GameAccountDataExtract *pExtract = NULL;
		F32* pfOutcomeWeights = NULL;
		F32* pfBaseWeights = NULL;
		ItemAssignmentSlottedItem** eaNoItems = NULL;
		F32 fTotalWeight = 0;
		F32 fTotalBaseWeight = 0;
		F32 fTotalQualityRewardBonus = 0.0f;

		ItemAssignments_CalculateOutcomeWeights(pEnt, pDef, eaSlottedItems, &pfOutcomeWeights);
		if (eaSize(&eaSlottedItems) > 0)
			ItemAssignments_CalculateOutcomeWeights(pEnt, pDef, eaNoItems, &pfBaseWeights);

		for (i = eaiSize(&pfOutcomeWeights)-1; i >= 0; i--)
		{
			fTotalWeight += pfOutcomeWeights[i];
		}
		MAX1F(fTotalWeight, 0.001f);

		for (i = eaiSize(&pfBaseWeights)-1; i >= 0; i--)
		{
			fTotalBaseWeight += pfBaseWeights[i];
		}

		for (i = eaSize(&eaSlottedItems) - 1; i >= 0; i--)
		{
			Item *pItem = inv_GetItemFromBag(pEnt, eaSlottedItems[i]->eBagID, eaSlottedItems[i]->iBagSlot, pExtract ? pExtract : (pExtract = entity_GetCachedGameAccountDataExtract(pEnt)));
			if (pItem && GET_REF(pItem->hItem))
			{
				fTotalQualityRewardBonus += ItemAssignment_GetNumericQualityScaleForItemDef(GET_REF(pItem->hItem));
			}
		}

		for (i = 0; i < eaSize(&pDef->eaOutcomes); i++)
		{
			ItemAssignmentOutcome* pOutcome = pDef->eaOutcomes[i];
			ItemAssignmentOutcomeUI* pOutcomeUI = eaGetStruct(peaOutcomeUI, parse_ItemAssignmentOutcomeUI, iCount++);
			F32 fWeight = pfOutcomeWeights[i];
			F32 fChance = fWeight / fTotalWeight;
			pOutcomeUI->fOutcomePercentChance = fChance * 100.0f; // Convert to percentage
			pOutcomeUI->pchName = allocAddString(pOutcome->pchName);
			pOutcomeUI->pchDisplayName = TranslateDisplayMessage(pOutcome->msgDisplayName);
			pOutcomeUI->pchDescription = TranslateDisplayMessage(pOutcome->msgDescription);
			pOutcomeUI->fBasePercentChance = fTotalBaseWeight > 0.001f ? pfBaseWeights[i] * 100.0f / fTotalBaseWeight : pOutcomeUI->fOutcomePercentChance;
			pOutcomeUI->fQualityRewardBonus = fTotalQualityRewardBonus;
		}

		eaiDestroy(&pfOutcomeWeights);
		eaiDestroy(&pfBaseWeights);
	}

	eaSetSizeStruct(peaOutcomeUI, parse_ItemAssignmentOutcomeUI, iCount);
}

ItemAssignmentSlottedItem** ItemAssignment_GetOrCreateTempItemAssignmentSlottedItemList()
{
	static ItemAssignmentSlottedItem **s_eaSlottedItemsList = NULL;
	S32 i, iSize = eaSize(&pIACache->eaSlots);

	eaSetSizeStruct(&s_eaSlottedItemsList, parse_ItemAssignmentSlottedItem, iSize);

	for (i = 0; i < iSize; i++)
	{
		NOCONST(ItemAssignmentSlottedItem)* pSlottedItem = CONTAINER_NOCONST(ItemAssignmentSlottedItem, s_eaSlottedItemsList[i]);
		ItemAssignmentSlotUI* pSlotUI = pIACache->eaSlots[i];
		pSlottedItem->eBagID = pSlotUI->eBagID;
		pSlottedItem->iBagSlot = pSlotUI->iBagSlot;
		pSlottedItem->iAssignmentSlot = pSlotUI->iAssignmentSlot;
		pSlottedItem->uItemID = pSlotUI->uItemID;
	}

	return s_eaSlottedItemsList;
}

void ItemAssignment_FillInFromCompleteDetails(SA_PARAM_NN_VALID Entity *pEnt, 
	SA_PARAM_NN_VALID ItemAssignmentPersistedData* pPlayerData, 
	SA_PARAM_NN_VALID ItemAssignmentSlotUI*** peaData, 
	SA_PARAM_NN_VALID ItemAssignmentCompletedDetails *pCompleteDetails)
{
	GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	ItemAssignmentDef* pDef = GET_REF(pCompleteDetails->hDef);
	ItemAssignmentSlotUI* pSlotUI;
	ItemAssignmentOutcome* pOutcome = ItemAssignment_GetOutcomeByName(pDef, pCompleteDetails->pchOutcome);
	S32 i;

	for (i = 0; i < eaSize(&pCompleteDetails->eaSlottedItemRefs); i++)
	{
		ItemAssignmentSlottedItemResults* pItemDefRef = pCompleteDetails->eaSlottedItemRefs[i];
		// This is in no way guaranteed to be the correct slot
		ItemAssignmentSlot* pPossibleSlot = pDef ? eaGet(&pDef->eaSlots, MIN(i, eaSize(&pDef->eaSlots) - 1)) : NULL;
		pSlotUI = eaGetStruct(peaData, parse_ItemAssignmentSlotUI, i);
		COPY_HANDLE(pSlotUI->hItemDef, pItemDefRef->hDef);
		pSlotUI->bDestroyed = pItemDefRef->bDestroyed;
		pSlotUI->bNewAssignment = pItemDefRef->bNewAssignment;
		pSlotUI->uNewAssignmentID = pCompleteDetails->uNewAssignmentID;
		pSlotUI->pchIcon = SAFE_MEMBER(pPossibleSlot, pchIcon);
		pSlotUI->pchDestroyDescription = pOutcome && pOutcome->pResults ? TranslateDisplayMessage(pOutcome->pResults->msgDestroyDescription) : NULL;
		pSlotUI->pchNewAssignmentDescription = pOutcome && pOutcome->pResults ? TranslateDisplayMessage(pOutcome->pResults->msgNewAssignmentDescription) : NULL;
		if (pOutcome && pOutcome->pResults)
		{
			COPY_HANDLE(pSlotUI->hNewAssignmentDef, pOutcome->pResults->hNewAssignment);
		}
		else
		{
			REMOVE_HANDLE(pSlotUI->hNewAssignmentDef);
		}
		if (!pSlotUI->bUpdated)
		{
			BagIterator *iter;
			// TODO: This does not handle guild bags, but then again, should this even bother with guild bags?
			NOCONST(InventoryBag) *pBag = inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), pSlotUI->eBagID, pExtract);
			InventorySlot *pInvSlot = pBag ? inv_GetSlotPtrNoConst(pBag, pSlotUI->iBagSlot) : NULL;

			COPY_HANDLE(pSlotUI->hDef, pCompleteDetails->hDef);
			COPY_HANDLE(pSlotUI->hItemDef, pItemDefRef->hDef);
			pSlotUI->uItemID = pItemDefRef->uItemID;
			// This is not quite accurate...
			pSlotUI->iAssignmentSlot = i;
			pSlotUI->bUnslottable = SAFE_MEMBER(pDef, bAllowItemUnslotting);
			pSlotUI->bUpdated = true;

			// Check to see if the existing Bag/Slot accurately represents the item.
			if (pSlotUI->uItemID && (!pInvSlot || !pInvSlot->pItem || pInvSlot->pItem->id != pSlotUI->uItemID))
			{
				// Attempt to locate the item (it may not even exist anymore)
				iter = inv_trh_FindItemByID(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pEnt), pSlotUI->uItemID);
				if (iter && bagiterator_GetCurrentBag(iter))
				{
					pSlotUI->eBagID = bagiterator_GetCurrentBagID(iter);
					pSlotUI->iBagSlot = iter->i_cur;
				}
				else
				{
					pSlotUI->eBagID = InvBagIDs_None;
					pSlotUI->iBagSlot = -1;
				}
				bagiterator_Destroy(iter);
			}
			else if (!pSlotUI->uItemID)
			{
				pSlotUI->eBagID = InvBagIDs_None;
				pSlotUI->iBagSlot = -1;
			}
		}
	}
}

ItemAssignmentUI* ItemAssignment_GetAssignment(	ItemAssignmentUI*** peaData, 
												S32 iIndex, 
												U32 uAssignmentID, 
												S32 iCompletedIndex, 
												const char *pchHeader,
												bool bInsertIfNotFound)
{
	S32 i;
	for (i = iIndex; i < eaSize(peaData); i++)
	{
		if ((uAssignmentID == 0 || (*peaData)[i]->uAssignmentID == uAssignmentID)
			&& (iCompletedIndex < 0 || (*peaData)[i]->iCompletedAssignmentIndex == iCompletedIndex)
			&& (!pchHeader || ((*peaData)[i]->bIsHeader && (*peaData)[i]->pchDisplayName == pchHeader)))
		{
			if (i != iIndex)
			{
				// Swap elements
				ItemAssignmentUI* pTemp = (*peaData)[iIndex];
				(*peaData)[iIndex] = (*peaData)[i];
				(*peaData)[i] = pTemp;
			}
			break;
		}
	}
	
	if (bInsertIfNotFound && i >= eaSize(peaData))
	{
		eaInsert(peaData, StructCreate(parse_ItemAssignmentUI), iIndex);
	}
	return eaGetStruct(peaData, parse_ItemAssignmentUI, iIndex);
}

ItemAssignmentUI* ItemAssignment_CreateActiveHeader(ItemAssignmentUI*** peaData, S32* piCount, const char* pchDisplayNameKey)
{
	const char *pchHeader = TranslateMessageKey(pchDisplayNameKey);
	ItemAssignmentUI* pData = ItemAssignment_GetAssignment(peaData, (*piCount)++, 0, -1, pchHeader, true);
	StructReset(parse_ItemAssignmentUI, pData);
	pData->iCompletedAssignmentIndex = -1;
	pData->pchDisplayName = pchHeader;
	pData->bIsHeader = true;
	return pData;
}

static void gclItemAssignment_GetChainLength(ItemAssignmentPersistedData* pPlayerData, ItemAssignmentDef *pDef, S32 *piChainLength, S32 *piChainLengthNoRepeat, S32 *piChainLengthCompleted, S32 iLength, S32 iLengthNoRepeat, S32 iLengthCompleted)
{
	S32 i;
	bool bCompleted = false;

	if (!pDef)
		return;

	iLength++;
	if (!pDef->bRepeatable)
		iLengthNoRepeat++;

	if (piChainLengthCompleted && pPlayerData)
	{
		i = eaIndexedFindUsingString(&pPlayerData->eaCompletedAssignments, pDef->pchName);
		if (i >= 0)
		{
			if (!pDef->bRepeatable)
				iLengthCompleted++;
		}
		else
			piChainLengthCompleted = NULL;
	}

	if (piChainLength)
	{
		MAX1(*piChainLength, iLength);
	}
	if (piChainLengthNoRepeat)
	{
		MAX1(*piChainLengthNoRepeat, iLengthNoRepeat);
	}
	if (piChainLengthCompleted)
	{
		MAX1(*piChainLengthCompleted, iLengthCompleted);
	}

	for (i = eaSize(&pDef->eaDependencies) - 1; i >= 0; i--)
	{
		gclItemAssignment_GetChainLength(pPlayerData, GET_REF(pDef->eaDependencies[i]->hDef), piChainLength, piChainLengthNoRepeat, piChainLengthCompleted, iLength, iLengthNoRepeat, iLengthCompleted);
	}
}

void FillActiveItemAssignment(	ItemAssignmentUI* pDataOut, 
								const ItemAssignment* pAssignment, 
								Entity* pEnt, 
								ItemAssignmentPersistedData* pPlayerData, 
								U32 uTimeCurrent)
{
	ItemAssignmentDef* pDef = GET_REF(pAssignment->hDef);
	ItemAssignmentCategorySettings* pCategorySettings = pDef ? ItemAssignmentCategory_GetSettings(pDef->eCategory) : NULL;
	ItemAssignmentOutcome* pOutcome = ItemAssignment_GetOutcomeByName(pDef, pAssignment->pchRewardOutcome);
	GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	ItemAssignmentCompletedDetails* pCompleteDetails = ItemAssignment_EntityGetRecentlyCompletedAssignmentByID(pEnt, pAssignment->uAssignmentID);
	ItemAssignmentDef* pDep;
	S32 i;
	
	// todo: this is pretty dumb that most of these fields are checking if pDef is NULL...

	COPY_HANDLE(pDataOut->hDef, pAssignment->hDef);
	pDataOut->pchDisplayName = pDef ? TranslateDisplayMessage(pDef->msgDisplayName) : NULL;
	pDataOut->pchDescription = pDef ? TranslateDisplayMessage(pDef->msgDescription) : NULL;
	pDataOut->pchChainDisplayName = pDef ? TranslateDisplayMessage(pDef->msgAssignmentChainDisplayName) : NULL;
	pDataOut->pchChainDescription = pDef ? TranslateDisplayMessage(pDef->msgAssignmentChainDescription) : NULL;
	pDataOut->iChainLength = 0;
	pDataOut->iChainLengthNoRepeats = 0;
	pDataOut->iChainLengthCompleted = 0;
	pDataOut->iChainDepth = 0;
	pDep = pDef;
	while ((pDep = pDep && pDep->pRequirements ? GET_REF(pDep->pRequirements->hRequiredAssignment) : NULL))
		pDataOut->iChainDepth++;
	gclItemAssignment_GetChainLength(pPlayerData, pDef, &pDataOut->iChainLength, &pDataOut->iChainLengthNoRepeats, &pDataOut->iChainLengthCompleted, 0, 0, 0);
	pDataOut->pchIcon = ItemAssignment_GetIconName(pDef, pCategorySettings);
	pDataOut->pchImage = ItemAssignment_GetImageName(pDef, pCategorySettings);
	pDataOut->pchFeaturedActivity = SAFE_MEMBER(pDef, pchFeaturedActivity);
	pDataOut->bIsFeatured = false;
	pDataOut->pchWeight = pDef ? StaticDefineIntRevLookup(ItemAssignmentWeightTypeEnum, pDef->eWeight) : NULL;
	pDataOut->fWeightValue = pDef ? ItemAssignmentWeightType_GetWeightValue(pDef->eWeight) : 0.f;
	pDataOut->eCategory = SAFE_MEMBER(pDef, eCategory);
	pDataOut->uTimeStarted = pAssignment->uTimeStarted;
	pDataOut->uDuration = ItemAssignment_GetDuration(pAssignment, pDef);
	pDataOut->iCompletionExperience = SAFE_MEMBER(pDef, iCompletionExperience);
	pDataOut->uTimeRemaining = pDataOut->uDuration - (uTimeCurrent - pDataOut->uTimeStarted);
	pDataOut->uAssignmentID = pAssignment->uAssignmentID;
	pDataOut->pchOutcomeName = allocAddString(pAssignment->pchRewardOutcome);
	pDataOut->pchOutcomeDisplayName = pOutcome ? TranslateDisplayMessage(pOutcome->msgDisplayName) : NULL;
	pDataOut->pchOutcomeDescription = pOutcome ? TranslateDisplayMessage(pOutcome->msgDescription) : NULL;
	pDataOut->pchOutcomeDestroyedDescription = pOutcome && pOutcome->pResults ? TranslateDisplayMessage(pOutcome->pResults->msgDestroyDescription) : NULL;
	pDataOut->pchOutcomeNewAssignmentDescription = pOutcome && pOutcome->pResults ? TranslateDisplayMessage(pOutcome->pResults->msgNewAssignmentDescription) : NULL;
	pDataOut->uOutcomeDestroyed = 0;
	pDataOut->uOutcomeNewAssignment = 0;
#ifdef GAMECLIENT
	pDataOut->pchMapDisplayName = gclRequestMapDisplayName(pAssignment->pchMapMsgKey);
#endif
	pDataOut->bHasRewards = !!pAssignment->pchRewardOutcome;
	pDataOut->bIsHeader = false;
	pDataOut->bNew = false;
	if (pDef)
	{
		pDataOut->bIsAbortable = pDef->bIsAbortable;
		pDataOut->bRepeatable = pDef->bRepeatable;
		pDataOut->bHasRequiredSlots = pDef->bHasRequiredSlots;
		pDataOut->bHasOptionalSlots = pDef->bHasOptionalSlots;
		pDataOut->bHasItemCosts = pDef->pRequirements ? eaSize(&pDef->pRequirements->eaItemCosts) : 0;
		pDataOut->iSortOrder = pDef->iSortOrder;
	}


	if (pCategorySettings)
	{
		pDataOut->pchCategoryNumericRank1 = pCategorySettings->pchNumericRank1;
		pDataOut->pchCategoryNumericRank2 = pCategorySettings->pchNumericRank2;
		pDataOut->pchCategoryNumericXP1 = pCategorySettings->pchNumericXP1;
		pDataOut->pchCategoryNumericXP2 = pCategorySettings->pchNumericXP2;
		pDataOut->pchCategoryIcon = pCategorySettings->pchIconName;
	}
	pDataOut->uCompletedTime = 0;
	pDataOut->bIsLockedSlot = false;
	

	if (pDef)
	{
		ItemAssignmentCompleted* pCompleted = ItemAssignments_PlayerGetCompletedAssignment(pEnt, pDef);
		if (pCompleted)
		{
			pDataOut->uCompletedTime = pCompleted->uCompleteTime;
			if (!pDataOut->uCompletedTime)
				pDataOut->uCompletedTime = 1;
		}
	}
	if (pOutcome && pOutcome->pResults)
	{
		COPY_HANDLE(pDataOut->hOutcomeNewAssignmentDef, pOutcome->pResults->hNewAssignment);
	}
	else
	{
		REMOVE_HANDLE(pDataOut->hOutcomeNewAssignmentDef);
	}

	if (!pCompleteDetails) //TODO: This is safe to remove once F2P goes live
		pCompleteDetails = ItemAssignment_FindPossibleCompletedDetails(pPlayerData, pAssignment);

	pDataOut->fQualityRewardBonus = ItemAssignment_GetNumericQualityScale(pEnt, pAssignment, pCompleteDetails, pDef);

	if (pCompleteDetails)
	{
		pDataOut->bHasCompleteDetails = true;
		pDataOut->iCompletedAssignmentIndex = eaFind(&pPlayerData->eaRecentlyCompletedAssignments, pCompleteDetails);
		for (i = eaSize(&pCompleteDetails->eaSlottedItemRefs) - 1; i >= 0; --i)
		{
			if (pCompleteDetails->eaSlottedItemRefs[i]->bDestroyed)
				++pDataOut->uOutcomeDestroyed;
			else if (pCompleteDetails->eaSlottedItemRefs[i]->bNewAssignment)
				++pDataOut->uOutcomeNewAssignment;
		}
	}
	else
	{
		pDataOut->bHasCompleteDetails = false;
		pDataOut->iCompletedAssignmentIndex = -1;
		pDataOut->uOutcomeDestroyed = 0;
		pDataOut->uOutcomeNewAssignment = 0;
	}
}

void FillCompletedItemAssignment(Entity *pEnt, ItemAssignmentUI* pData, ItemAssignmentCompletedDetails* pAssignment, ItemAssignment* pActiveAssignment, U32 uTimeCurrent)
{
	ItemAssignmentDef* pDef = GET_REF(pAssignment->hDef);
	ItemAssignmentCategorySettings* pCategorySettings = pDef ? ItemAssignmentCategory_GetSettings(pDef->eCategory) : NULL;
	ItemAssignmentOutcome* pOutcome = ItemAssignment_GetOutcomeByName(pDef, pAssignment->pchOutcome);
	ItemAssignmentDef* pDep;
	S32 i;

	COPY_HANDLE(pData->hDef, pAssignment->hDef);
	pData->pchDisplayName = pDef ? TranslateDisplayMessage(pDef->msgDisplayName) : NULL;
	pData->pchDescription = pDef ? TranslateDisplayMessage(pDef->msgDescription) : NULL;
	pData->pchChainDisplayName = pDef ? TranslateDisplayMessage(pDef->msgAssignmentChainDisplayName) : NULL;
	pData->pchChainDescription = pDef ? TranslateDisplayMessage(pDef->msgAssignmentChainDescription) : NULL;
	pData->iChainLength = 0;
	pData->iChainLengthNoRepeats = 0;
	pData->iChainDepth = 0;
	pDep = pDef;
	while ((pDep = pDep && pDep->pRequirements ? GET_REF(pDep->pRequirements->hRequiredAssignment) : NULL))
		pData->iChainDepth++;
	gclItemAssignment_GetChainLength(NULL, pDef, &pData->iChainLength, &pData->iChainLengthNoRepeats, &pData->iChainLengthCompleted, 0, 0, 0);
	pData->pchIcon = ItemAssignment_GetIconName(pDef, pCategorySettings);
	pData->pchImage = ItemAssignment_GetImageName(pDef, pCategorySettings);
	pData->pchFeaturedActivity = SAFE_MEMBER(pDef, pchFeaturedActivity);
	pData->bIsFeatured = false;
	pData->pchWeight = pDef ? StaticDefineIntRevLookup(ItemAssignmentWeightTypeEnum, pDef->eWeight) : NULL;
	pData->fWeightValue = pDef ? ItemAssignmentWeightType_GetWeightValue(pDef->eWeight) : 0.f;
	pData->eCategory = SAFE_MEMBER(pDef, eCategory);
	pData->uTimeStarted = pAssignment->uTimeStarted;
	pData->uDuration = ItemAssignment_GetDuration(pAssignment, pDef);
	pData->iCompletionExperience = SAFE_MEMBER(pDef, iCompletionExperience);
	pData->uTimeRemaining = pData->uDuration - (uTimeCurrent - pData->uTimeStarted);
	pData->uAssignmentID = pActiveAssignment ? pActiveAssignment->uAssignmentID : pAssignment->uAssignmentID;
	pData->pchOutcomeName = SAFE_MEMBER(pOutcome, pchName);
	pData->pchOutcomeDisplayName = pOutcome ? TranslateDisplayMessage(pOutcome->msgDisplayName) : NULL;
	pData->pchOutcomeDescription = pOutcome ? TranslateDisplayMessage(pOutcome->msgDescription) : NULL;
	pData->pchOutcomeDestroyedDescription = pOutcome && pOutcome->pResults ? TranslateDisplayMessage(pOutcome->pResults->msgDestroyDescription) : NULL;
	pData->pchOutcomeNewAssignmentDescription = pOutcome && pOutcome->pResults ? TranslateDisplayMessage(pOutcome->pResults->msgNewAssignmentDescription) : NULL;
	pData->uOutcomeDestroyed = 0;
	pData->uOutcomeNewAssignment = 0;
#ifdef GAMECLIENT
	pData->pchMapDisplayName = gclRequestMapDisplayName(pAssignment->pchMapMsgKey);
#endif
	pData->bHasRewards = !!pAssignment->pchOutcome;
	pData->bIsHeader = false;
		
	if (pDef)
	{
		pData->bRepeatable = pDef->bRepeatable;
		pData->bHasRequiredSlots = pDef->bHasRequiredSlots;
		pData->bHasOptionalSlots = pDef->bHasOptionalSlots;
		pData->bHasItemCosts = pDef->pRequirements ? eaSize(&pDef->pRequirements->eaItemCosts) : 0;
		pData->iSortOrder = pDef->iSortOrder;
	}

	pData->bHasCompleteDetails = true;
	pData->bNew = !pAssignment->bMarkedAsRead;
	pData->bRepeatable = SAFE_MEMBER(pDef, bRepeatable);
	if (pCategorySettings)
	{
		pData->pchCategoryNumericRank1 = pCategorySettings->pchNumericRank1;
		pData->pchCategoryNumericRank2 = pCategorySettings->pchNumericRank2;
		pData->pchCategoryNumericXP1 = pCategorySettings->pchNumericXP1;
		pData->pchCategoryNumericXP2 = pCategorySettings->pchNumericXP2;
		pData->pchCategoryIcon = pCategorySettings->pchIconName;
	}

	pData->uCompletedTime = pAssignment->uTimeStarted + pData->uDuration;
	if (pOutcome && pOutcome->pResults)
	{
		COPY_HANDLE(pData->hOutcomeNewAssignmentDef, pOutcome->pResults->hNewAssignment);
	}
	else
	{
		REMOVE_HANDLE(pData->hOutcomeNewAssignmentDef);
	}

	pData->fQualityRewardBonus = ItemAssignment_GetNumericQualityScale(pEnt, pActiveAssignment, pAssignment, pDef);

	for (i = eaSize(&pAssignment->eaSlottedItemRefs) - 1; i >= 0; --i)
	{
		if (pAssignment->eaSlottedItemRefs[i]->bDestroyed)
			++pData->uOutcomeDestroyed;
		else if (pAssignment->eaSlottedItemRefs[i]->bNewAssignment)
			++pData->uOutcomeNewAssignment;
	}
}

int SortActiveItemAssignments(U32 *puFlags, const ItemAssignmentUI** ppA, const ItemAssignmentUI** ppB)
{
	const ItemAssignmentUI* pA = (*ppA);
	const ItemAssignmentUI* pB = (*ppB);

	if (pA->uSortCategory != pB->uSortCategory)
		return (int)pA->uSortCategory - (int)pB->uSortCategory;

	if (pA->bIsHeader != pB->bIsHeader)
		return pB->bIsHeader - pA->bIsHeader;

	if (pA->bHasRewards != pB->bHasRewards)
		return pB->bHasRewards - pA->bHasRewards;

	if (pA->uTimeRemaining != pB->uTimeRemaining)
		return (puFlags && (*puFlags & kGetActiveAssignmentFlags_ReverseTimeRemaining) ? -1 : 1) * ((int)pA->uTimeRemaining - (int)pB->uTimeRemaining);

	return stricmp(pA->pchDisplayName, pB->pchDisplayName);
}




static void gclItemAssignment_CacheInventory(Entity* pEnt, S32 eBagID)
{
	static S32 *eaiValues = NULL;
	GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	InventoryBag* pBag = CONTAINER_RECONST(InventoryBag, inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), eBagID, pExtract));
	S32 i, j;

	// Check to see if the cache is current
	if (s_uItemListTime == ItemAssignment_GetElapsedTime() && s_pItemListEnt == pEnt && eaiFind(&s_peItemListBag, eBagID) >= 0)
	{
		return;
	}

	// Once per frame/per entity update
	if (s_uItemListTime != ItemAssignment_GetElapsedTime() || s_pItemListEnt != pEnt)
	{
		for (i = eaSize(&s_eaItemLists) - 1; i >= 0; i--)
		{
			if (s_eaItemLists[i])
			{
				ItemList_Reset(s_eaItemLists[i]);
			}
		}

		s_pItemListEnt = pEnt;
		s_uItemListTime = ItemAssignment_GetElapsedTime();
		eaiClearFast(&s_peItemListBag);
		ItemList_Reset(&s_ItemListUniverse);
	}

	// check if bag is already cached
	if (eaiFind(&s_peItemListBag, eBagID) >= 0)
		return;

	// Add bag to cache
	eaiPush(&s_peItemListBag, eBagID);

	// Add items from bag(s)
	if (pBag)
	{
		bool bCanResideInBag = ItemAssignment_trh_CanSlottedItemResideInBag(CONTAINER_NOCONST(InventoryBag, pBag));
		for (j = eaSize(&pBag->ppIndexedInventorySlots) - 1; j >= 0; j--)
		{
			InventorySlot *pSlot = pBag->ppIndexedInventorySlots[j];
			if (pSlot->pItem && GET_REF(pSlot->pItem->hItem))
			{
				Item *pItem = pSlot->pItem;
				ItemDef *pItemDef = GET_REF(pItem->hItem);
				bool bCanRemoveItem = false;

				if (!(pItemDef->flags & kItemDefFlag_CanSlotOnAssignment))
					continue;
				bCanRemoveItem = item_CanRemoveItem(pItem);

				for (i = eaiSize(&pItemDef->peCategories) - 1; i >= 0; --i)
				{
					ItemCategory eCategory = pItemDef->peCategories[i];
					ItemList *pList;

					if (eCategory >= eaSize(&s_eaItemLists))
						eaSetSize(&s_eaItemLists, eCategory + 1);
					if (!s_eaItemLists[eCategory])
						s_eaItemLists[eCategory] = calloc(1, sizeof(ItemList));
					pList = s_eaItemLists[eCategory];

					// Add it to an item list
					if (!bCanResideInBag)
						eaPush(&pList->eaBadBag, pItem);
					else if (!bCanRemoveItem)
						eaPush(&pList->eaNotRemovable, pItem);
					else
						eaPush(&pList->eaItems, pItem);
				}

				// Add it to the universe
				if (!bCanResideInBag)
					eaPush(&s_ItemListUniverse.eaBadBag, pItem);
				else if (!bCanRemoveItem)
					eaPush(&s_ItemListUniverse.eaNotRemovable, pItem);
				else
					eaPush(&s_ItemListUniverse.eaItems, pItem);
			}
		}
	}
}

static void gclItemAssignment_CacheDefaultInventoryBags(Entity *pEnt)
{
	S32 i;
	if (!pEnt)
		return;

	for ( i = eaiSize(&g_ItemAssignmentSettings.eaiDefaultInventoryBagCacheIDs) - 1; i >= 0; --i)
	{
		InvBagIDs eBagID = g_ItemAssignmentSettings.eaiDefaultInventoryBagCacheIDs[i];

		if (eBagID != InvBagIDs_None)
			gclItemAssignment_CacheInventory(pEnt, eBagID);
	}

}

static void gclItemAssignmentSlot_CheckItemList(StashTable stRestrictItems, ItemList *pUsedSet, ItemList *pSet,
												Item **ppFound, Item **ppFoundNotRemovable, Item **ppFoundBadBag)
{
	S32 j;
	bool bFound = false, bFoundNotRemovable = false;
	if (ppFound)
	{
		for (j = eaSize(&pSet->eaItems) - 1; j >= 0; --j)
		{
			if (!stashFindPointer(stRestrictItems, pSet->eaItems[j], NULL) && (!pUsedSet || eaFind(&pUsedSet->eaItems, pSet->eaItems[j]) < 0))
			{
				*ppFound = pSet->eaItems[j];
				bFound = true;
				break;
			}
		}
	}
	if (!bFound && ppFoundNotRemovable)
	{
		for (j = eaSize(&pSet->eaNotRemovable) - 1; j >= 0; --j)
		{
			if (!stashFindPointer(stRestrictItems, pSet->eaNotRemovable[j], NULL) && (!pUsedSet || eaFind(&pUsedSet->eaNotRemovable, pSet->eaNotRemovable[j]) < 0))
			{
				*ppFoundNotRemovable = pSet->eaNotRemovable[j];
				bFoundNotRemovable = true;
				break;
			}
		}
	}
	if (!bFound && !bFoundNotRemovable && ppFoundBadBag)
	{
		for (j = eaSize(&pSet->eaBadBag) - 1; j >= 0; --j)
		{
			if (!stashFindPointer(stRestrictItems, pSet->eaBadBag[j], NULL) && (!pUsedSet || eaFind(&pUsedSet->eaBadBag, pSet->eaBadBag[j]) < 0))
			{
				*ppFoundBadBag = pSet->eaBadBag[j];
				break;
			}
		}
	}
}

static bool gclItemAssignmentSlot_CheckInventory(ItemAssignmentSlot *pSlot, ItemCategory *peFailCategory, ItemAssignmentFailsRequiresReason *peFailReason, ItemList *pUsedSet)
{
	static StashTable s_stRestrictItems;
	Item *pFound = NULL, *pFoundNotRemovable = NULL, *pFoundBadBag = NULL;
	S32 i, j, n = eaSize(&s_eaItemLists);
	ItemCategory eFailCategory = kItemCategory_None;

	PERFINFO_AUTO_START_FUNC();

	if (peFailCategory)
		*peFailCategory = kItemCategory_None;
	if (peFailReason)
		*peFailReason = kItemAssignmentFailsRequiresReason_None;

	if (!pSlot)
	{
		PERFINFO_AUTO_STOP();
		return false;
	}
	if (pSlot->bIsOptional)
	{
		PERFINFO_AUTO_STOP();
		return true;
	}

	if (!s_stRestrictItems)
		s_stRestrictItems = stashTableCreateAddress(1024);
	else
		stashTableClear(s_stRestrictItems);

	// Determine set of restricted items
	for (i = eaiSize(&pSlot->peRestrictItemCategories) - 1; i >= 0; --i)
	{
		ItemCategory eCategory = pSlot->peRestrictItemCategories[i];
		ItemList *pList = eCategory < n ? s_eaItemLists[eCategory] : NULL;
		if (!pList)
			continue;

		for (j = eaSize(&pList->eaItems)-1; j >= 0; --j)
			stashAddPointer(s_stRestrictItems, pList->eaItems[j], pList->eaItems[j], false);
		for (j = eaSize(&pList->eaNotRemovable)-1; j >= 0; --j)
			stashAddPointer(s_stRestrictItems, pList->eaNotRemovable[j], pList->eaNotRemovable[j], false);
		for (j = eaSize(&pList->eaBadBag)-1; j >= 0; --j)
			stashAddPointer(s_stRestrictItems, pList->eaBadBag[j], pList->eaBadBag[j], false);
	}

	if (eaiSize(&pSlot->peRequiredItemCategories))
	{
		if (! g_ItemAssignmentSettings.bUseStrictCategoryChecking)
		{
			// Look at the subsets
			for (i = eaiSize(&pSlot->peRequiredItemCategories) - 1; i >= 0; --i)
			{
				ItemCategory eCategory = pSlot->peRequiredItemCategories[i];
				ItemList *pList = eCategory < n ? s_eaItemLists[eCategory] : NULL;
				if (!pList)
					continue;
				gclItemAssignmentSlot_CheckItemList(s_stRestrictItems, pUsedSet, pList, &pFound, &pFoundNotRemovable, &pFoundBadBag);
				if (pFound)
					break;
			}
			eFailCategory = pSlot->peRequiredItemCategories[0];
		}
		else
		{	// using strict category checking, items must contain all the required categories
			for (i = eaiSize(&pSlot->peRequiredItemCategories) - 1; i >= 0; --i)
			{
				ItemCategory eCategory = pSlot->peRequiredItemCategories[i];
				ItemList *pList = eCategory < n ? s_eaItemLists[eCategory] : NULL;
				if (!pList)
					continue;

				FOR_EACH_IN_EARRAY(pList->eaItems, Item, pItem)
				{
					ItemDef *pDef = NULL;

					if (stashFindPointer(s_stRestrictItems, pItem, NULL) ||
						(pUsedSet && eaFind(&pUsedSet->eaItems, pItem) >= 0))
					{	// can't use this item, it's already used or restricted
						continue; 
					}

					pDef = GET_REF(pItem->hItem);
					if (pDef)
					{
						bool bSatisfied = true;
						for (j = eaiSize(&pSlot->peRequiredItemCategories) - 1; j >= 0; --j)
						{
							if (j == i)
								continue;
							if (eaiFind(&pDef->peCategories, pSlot->peRequiredItemCategories[j]) < 0)
							{
								bSatisfied = false;
								break;
							}
						}

						if (bSatisfied)
						{
							pFound = pItem;
							break;
						}
					}
				}
				FOR_EACH_END

					if (pFound)
						break;
			}
		}

	}
	else
	{
		gclItemAssignmentSlot_CheckItemList(s_stRestrictItems, pUsedSet, &s_ItemListUniverse, &pFound, &pFoundNotRemovable, &pFoundBadBag);
	}

	// Claim any found item
	if (pUsedSet)
	{
		if (pFound)
			eaPush(&pUsedSet->eaItems, pFound);
		else if (pFoundBadBag)
			eaPush(&pUsedSet->eaBadBag, pFoundBadBag);
		else if (pFoundNotRemovable)
			eaPush(&pUsedSet->eaNotRemovable, pFoundNotRemovable);
	}

	if (pFound)
	{
		PERFINFO_AUTO_STOP();
		return true;
	}

	if (pFoundNotRemovable)
	{
		if (peFailReason)
			*peFailReason |= kItemAssignmentFailsRequiresReason_CantFillNonremovableItem;
		if (peFailCategory)
			*peFailCategory = eFailCategory;
	}

	if (pFoundBadBag)
	{
		if (peFailReason)
			*peFailReason |= kItemAssignmentFailsRequiresReason_CantFillUnslottableBag;
		if (peFailCategory)
			*peFailCategory = eFailCategory;
	}

	if (!pFoundNotRemovable && !pFoundBadBag)
	{
		if (peFailReason)
			*peFailReason |= kItemAssignmentFailsRequiresReason_CantFillSlots;
		if (peFailCategory)
			*peFailCategory = eFailCategory;
	}

	PERFINFO_AUTO_STOP();
	return false;
}

// Checks that the player has sufficient assignable items for the assignment in the specified bag
static bool gclItemAssignment_CheckInventory(	ItemAssignmentDef* pDef, 
												ItemCategory *peFailCategory, 
												ItemAssignmentFailsRequiresReason *peFailReason, 
												U32 *pbfInvalidSlots, S32 iMaxSlots)
{
	static ItemList s_UsedItems;
	S32 i;
	bool bSuccess = true;
	S32 iAnyUsed = 0;
	S32 n = eaSize(&s_eaItemLists);

	PERFINFO_AUTO_START_FUNC();

	if (pbfInvalidSlots)
	{
		memset(pbfInvalidSlots, 0, (iMaxSlots + 7) / 8);
	}
	ItemList_Reset(&s_UsedItems);

	for (i = 0; i < eaSize(&pDef->eaSlots); i++)
	{
		ItemAssignmentSlot* pAssignmentSlot = pDef->eaSlots[i];
		ItemCategory eCategory = kItemCategory_None;
		ItemAssignmentFailsRequiresReason eFailReason = kItemAssignmentFailsRequiresReason_None;

		if (!gclItemAssignmentSlot_CheckInventory(pAssignmentSlot, &eCategory, &eFailReason, &s_UsedItems))
		{
			bSuccess = false;
			if (peFailCategory && eCategory != kItemCategory_None && *peFailCategory == kItemCategory_None)
				*peFailCategory = eCategory;
			if (peFailReason)
				*peFailReason |= eFailReason;
			if (pbfInvalidSlots && i < iMaxSlots)
				SETB(pbfInvalidSlots, i);
		}
	}

	PERFINFO_AUTO_STOP();
	return bSuccess;
}

// if bTestSlottedAssets is set, then it will not check the inventory to see if the player has the item
// and instead check if the current slotted assets are valid
bool ItemAssignment_GetFailsRequirementsReason(	Entity* pEnt, 
													ItemAssignmentDef* pDef, 
													char** pestrFailsRequires,
													ItemAssignmentFailsRequiresReason* peFailsRequires,
													U32 *pbfInvalidSlots, 
													S32 iMaxSlots,
													bool bTestSlottedAssets)
{
	ItemAssignmentFailsRequiresEntry** eaEntries = NULL;
	ItemAssignmentFailsRequiresReason eReason;
	S32 i;
	ItemCategory eFailCategory = kItemCategory_None;
	ItemAssignmentSlots slots = {0};

	PERFINFO_AUTO_START_FUNC();

	if (bTestSlottedAssets)
	{
		slots.eaSlots = ItemAssignment_GetOrCreateTempItemAssignmentSlottedItemList();
	}

	eReason = ItemAssignments_GetFailsRequiresReason(pEnt, pDef, (slots.eaSlots ? &slots : NULL), &eaEntries);

	if (eaSize(&eaEntries))
	{
		char pchReasonKey[256];
		for (i = 0; i < eaSize(&eaEntries); i++)
		{
			ItemAssignmentFailsRequiresEntry* pEntry = eaEntries[i];
			const char* pchReason = StaticDefineIntRevLookup(ItemAssignmentFailsRequiresReasonEnum, pEntry->eReason);
			const char* pchReasonString = entTranslateMessageKey(pEnt, pEntry->pchReasonKey);

			sprintf(pchReasonKey, "ItemAssignment_Fails%s", pchReason);

			entFormatGameMessageKey(pEnt, pestrFailsRequires, pchReasonKey, 
				STRFMT_STRING("String", NULL_TO_EMPTY(pchReasonString)), 
				STRFMT_INT("Value", pEntry->iValue), 
				STRFMT_END);

			// Also provide a list of flags
			(*peFailsRequires) |= pEntry->eReason;
		}
	}

	eaDestroyStruct(&eaEntries, parse_ItemAssignmentFailsRequiresEntry);

	// Check slotting requirements
	if (!bTestSlottedAssets && !gclItemAssignment_CheckInventory(pDef, &eFailCategory, &eReason, pbfInvalidSlots, iMaxSlots))
	{
		if (eReason & kItemAssignmentFailsRequiresReason_CantFillSlots)
		{
			if (eFailCategory != kItemCategory_None)
			{
				entFormatGameMessageKey(pEnt, pestrFailsRequires, "ItemAssignment_FailsSlotRequirements",
					STRFMT_STRING("Category", StaticDefineIntRevLookup(ItemCategoryEnum, eFailCategory)), 
					STRFMT_END);
			}
			else
			{
				// A slot without requirements failed restrictions
				entFormatGameMessageKey(pEnt, pestrFailsRequires, "ItemAssignment_FailsSlotRestrictions", STRFMT_END);
			}
		}

		// Add message for items that are available in an unslottable bag
		if (eReason & kItemAssignmentFailsRequiresReason_CantFillUnslottableBag)
		{
			entFormatGameMessageKey(pEnt, pestrFailsRequires, "ItemAssignment_FailsItemInUnslottableBag", STRFMT_END);
		}

		// Add a message for items that are not removable
		if (eReason & kItemAssignmentFailsRequiresReason_CantFillNonremovableItem)
		{
			entFormatGameMessageKey(pEnt, pestrFailsRequires, "ItemAssignment_FailsItemNonRemovable", STRFMT_END);
		}

		(*peFailsRequires) |= eReason;
		return true;
	}

	PERFINFO_AUTO_STOP();
	return eReason != kItemAssignmentFailsRequiresReason_None;
}

ItemAssignmentFailsRequiresReason ItemAssignment_UpdateRequirementsInfo(ItemAssignmentUI* pData, 
																		Entity* pEnt, 
																		ItemAssignmentDef* pDef, 
																		bool bTestSlottedAssets)
{
	static U32 s_uLastFlush;
	static ItemAssignmentUI** s_eaDefCache = NULL;
	U32 uTime = ItemAssignment_GetElapsedTime();
	bool bFailsMinLevelReq = false, bFailsMissionReq = false, bFailsAssignmentReq = false;
	ItemAssignmentUI* pCache = NULL;
	const U32 uUpdateTime = ItemAssignment_ConvertTime(1000);
	S32 i;
	S32 iCount = 0;

	if (!pDef)
	{
		if (pData)
		{
			pData->bFailsRequirements = false;
			pData->eFailsRequiresReasons = 0;
			estrClear(&pData->estrFailsRequires);
		}
		return 0;
	}

	if (s_uLastFlush != uTime)
	{
		for (i = eaSize(&s_eaDefCache) - 1; i >= 0; i--)
		{
			// If the assignment was not used for a couple of UI refreshes
			// then toss out the information.
			if (uTime >= s_eaDefCache[i]->uNextRequirementsUpdateTime && 
				uTime - s_eaDefCache[i]->uNextRequirementsUpdateTime >= ITEM_ASSIGNMENTS_REFRESH_TIME_UI * uUpdateTime)
			{
				StructDestroy(parse_ItemAssignmentUI, eaRemove(&s_eaDefCache, i));
			}
		}

		s_uLastFlush = uTime;
	}

	for (i = eaSize(&s_eaDefCache) - 1; i >= 0; i--)
	{
		if (GET_REF(s_eaDefCache[i]->hDef) == pDef)
		{
			pCache = s_eaDefCache[i];
			iCount = i;
			break;
		}
	}
	if (!pCache)
	{
		pCache = StructCreate(parse_ItemAssignmentUI);
		SET_HANDLE_FROM_REFERENT("ItemAssignmentDef", pDef, pCache->hDef);
		iCount = eaSize(&s_eaDefCache);
		eaPush(&s_eaDefCache, pCache);
	}

	if (uTime >= pCache->uNextRequirementsUpdateTime)
	{
		pCache->bFailsRequirements = false;
		pCache->eFailsRequiresReasons = 0;
		estrClear(&pCache->estrFailsRequires);
		if (ItemAssignment_GetFailsRequirementsReason(pEnt, 
														pDef,
														&pCache->estrFailsRequires, 
														&pCache->eFailsRequiresReasons, 
														pCache->bfInvalidSlots, 
														(ARRAY_SIZE(pCache->bfInvalidSlots) * sizeof(pCache->bfInvalidSlots[0]) * 8),
														bTestSlottedAssets))
		{
			pCache->bFailsRequirements = true;
		}

		if (!pCache->uNextRequirementsUpdateTime)
		{
			U32 uFirstUpdateTime = (iCount * ItemAssignment_ConvertTime(100)) % uUpdateTime;
			if (!uFirstUpdateTime)
				uFirstUpdateTime = uUpdateTime;
			pCache->uNextRequirementsUpdateTime = uTime + uFirstUpdateTime;
		}
		else
		{
			pCache->uNextRequirementsUpdateTime = uTime + uUpdateTime;
		}
	}

	// Copy fails requirements information from cache
	if (pData)
	{
		pData->bFailsRequirements = pCache->bFailsRequirements;
		pData->eFailsRequiresReasons = pCache->eFailsRequiresReasons;
		estrCopy(&pData->estrFailsRequires, &pCache->estrFailsRequires);
		if (pDef->pRequirements)
		{
			if (GET_REF(pData->hRequiredMission) != GET_REF(pDef->pRequirements->hRequiredMission))
				COPY_HANDLE(pData->hRequiredMission, pDef->pRequirements->hRequiredMission);
			if (GET_REF(pData->hRequiredAssignment) != GET_REF(pDef->pRequirements->hRequiredAssignment))
				COPY_HANDLE(pData->hRequiredAssignment, pDef->pRequirements->hRequiredAssignment);
		}
		for (i = 0; i < ARRAY_SIZE(pData->bfInvalidSlots); i++)
		{
			pData->bfInvalidSlots[i] = pCache->bfInvalidSlots[i];
		}
	}

	return pCache->eFailsRequiresReasons;
}

void SetItemAssignmentData(	Entity* pEnt, 
							ItemAssignmentUI* pData, 
							ItemAssignmentDef* pDef, 
							bool bIsPersonalAssignment, 
							bool bIsFeatured,
							bool bRequirementsTestSlottedAssets)
{
	ItemAssignmentCategorySettings* pCategorySettings = pDef ? ItemAssignmentCategory_GetSettings(pDef->eCategory) : NULL;
	ItemAssignmentPersistedData* pPlayerData = SAFE_MEMBER2(pEnt, pPlayer, pItemAssignmentPersistedData);
	ItemAssignmentDef* pDep;

	pData->eCategory = SAFE_MEMBER(pDef, eCategory);
	pData->pchDisplayName = pDef ? TranslateDisplayMessage(pDef->msgDisplayName) : NULL;
	pData->pchDescription = pDef ? TranslateDisplayMessage(pDef->msgDescription) : NULL;
	pData->pchChainDisplayName = pDef ? TranslateDisplayMessage(pDef->msgAssignmentChainDisplayName) : NULL;
	pData->pchChainDescription = pDef ? TranslateDisplayMessage(pDef->msgAssignmentChainDescription) : NULL;
	if (pCategorySettings)
	{
		pData->pchCategoryNumericRank1 = pCategorySettings->pchNumericRank1;
		pData->pchCategoryNumericRank2 = pCategorySettings->pchNumericRank2;
		pData->pchCategoryNumericXP1 = pCategorySettings->pchNumericXP1;
		pData->pchCategoryNumericXP2 = pCategorySettings->pchNumericXP2;
		pData->pchCategoryIcon = pCategorySettings->pchIconName;
	}

	pData->iChainLength = 0;
	pData->iChainLengthNoRepeats = 0;
	pData->iChainDepth = 0;
	pDep = pDef;
	while ((pDep = pDep && pDep->pRequirements ? GET_REF(pDep->pRequirements->hRequiredAssignment) : NULL))
		pData->iChainDepth++;
	gclItemAssignment_GetChainLength(pPlayerData, pDef, &pData->iChainLength, &pData->iChainLengthNoRepeats, &pData->iChainLengthCompleted, 0, 0, 0);
	pData->pchIcon = ItemAssignment_GetIconName(pDef, pCategorySettings);
	pData->pchImage = ItemAssignment_GetImageName(pDef, pCategorySettings);
	pData->pchFeaturedActivity = pDef ? pDef->pchFeaturedActivity : NULL;
	pData->bIsFeatured = !!bIsFeatured;
	pData->pchWeight = pDef ? StaticDefineIntRevLookup(ItemAssignmentWeightTypeEnum, pDef->eWeight) : NULL;
	pData->fWeightValue = pDef ? ItemAssignmentWeightType_GetWeightValue(pDef->eWeight) : 0.f;
	ItemAssignment_UpdateRequirementsInfo(pData, pEnt, pDef, bRequirementsTestSlottedAssets);
	pData->bIsHeader = false;
	pData->bIsPersonalAssignment = bIsPersonalAssignment;
	if (GET_REF(pData->hDef) != pDef)
		SET_HANDLE_FROM_REFERENT("ItemAssignmentDef", pDef, pData->hDef);
	pData->uTimeStarted = 0;
	pData->uTimeRemaining = pDef ? pDef->uDuration : 0;
	pData->uDuration = pDef ? pDef->uDuration : 0;
	pData->iCompletionExperience = SAFE_MEMBER(pDef, iCompletionExperience);
	pData->iCompletedAssignmentIndex = -1;
	
	if (pDef)
	{
		pData->bRepeatable = pDef->bRepeatable;
		pData->bHasRequiredSlots = pDef->bHasRequiredSlots;
		pData->bHasOptionalSlots = pDef->bHasOptionalSlots;
		pData->bHasItemCosts = pDef->pRequirements ? eaSize(&pDef->pRequirements->eaItemCosts) != 0 : 0;
		pData->iSortOrder = pDef->iSortOrder;
	}
	pData->uCompletedTime = 0;

	if (pDef && pDef->pRequirements)
		pData->iRequiredNumericValue = pDef->pRequirements->iRequiredNumericValue;

	if (pPlayerData && pDef)
	{
		ItemAssignmentCompleted* pCompleted = ItemAssignments_PlayerGetCompletedAssignment(pEnt, pDef);
		if (pCompleted)
		{
			pData->uCompletedTime = pCompleted->uCompleteTime;
			if (!pData->uCompletedTime)
				pData->uCompletedTime = 1;
		}
	}
}

static void gclAddItemAssignmentToList(	Entity* pEnt,
										ItemAssignmentDef* pDef, 
										ItemCategory *eaiFilterCategories,
										F32* eafWeightFilters,
										ItemAssignmentUI*** peaData, 
										S32* piCount, 
										ItemAssignmentCategory** peaCategories,
										bool bIsPersonalAssignment, 
										bool bAddCategoryHeaders,
										bool bIsFeatured,
										U32 uFlags,									   
										S32 iMinRequiredNumericValue,
										S32 iMaxRequiredNumericValue,
										S32 iUnmetRequiredNumericRange,
										const char *pchStringSearch)
{
	static ItemAssignmentUI s_ItemAssignmentTemp;
	ItemAssignmentUI* pData = NULL;
	ItemAssignmentCategorySettings* pCategorySettings = pDef ? ItemAssignmentCategory_GetSettings(pDef->eCategory) : NULL;
	S32 i;

	if (uFlags & (kGetActiveAssignmentFlags_ExcludeLevelErrors | kGetActiveAssignmentFlags_ExcludeAnyErrors | kGetActiveAssignmentFlags_ExcludeFactionErrors))
	{
		ItemAssignmentFailsRequiresReason eReason = ItemAssignment_UpdateRequirementsInfo(NULL, pEnt, pDef, false);
		if (((uFlags & kGetActiveAssignmentFlags_ExcludeAnyErrors)
			&& eReason != 0)
			|| ((uFlags & kGetActiveAssignmentFlags_ExcludeLevelErrors)
			&& !!(eReason & kItemAssignmentFailsRequiresReason_Level))
			|| ((uFlags & kGetActiveAssignmentFlags_ExcludeFactionErrors)
			&& !!(eReason & kItemAssignmentFailsRequiresReason_Allegiance)))
		{
			return;
		}
	}

	// if we are filtering by category, check to see if the category setting contains one of the categories we're looking for 
	if (eaiSize(&eaiFilterCategories))
	{
		bool bFoundCategory = false;

		if (!pCategorySettings)
			return;

		for (i = eaiSize(&pCategorySettings->peAssociatedItemCategories) - 1; i >= 0; --i)
		{
			if (eaiFind(&eaiFilterCategories, pCategorySettings->peAssociatedItemCategories[i]) >= 0)
			{
				bFoundCategory = true;
				break;
			}
		}

		if (!bFoundCategory)
			return;
	}

	if (eafSize(&eafWeightFilters))
	{
		F32 fDefWeight = ItemAssignmentWeightType_GetWeightValue(pDef->eWeight);

		if (eafFind(&eafWeightFilters, fDefWeight) < 0)
			return;
	}

	if (pDef->pRequirements)
	{
		if (iMinRequiredNumericValue > 0 || iMaxRequiredNumericValue > 0)
		{
			if (pDef->pRequirements->iRequiredNumericValue < iMinRequiredNumericValue || 
				pDef->pRequirements->iRequiredNumericValue > iMaxRequiredNumericValue)
				return;
		}

		// filtering out unmet required numeric
		if (uFlags & kGetActiveAssignmentFlags_HideUnmetRequiredNumeric)
		{
			const char* pchNumeric = REF_STRING_FROM_HANDLE(pDef->pRequirements->hRequiredNumeric);
			if (pchNumeric)
			{
				S32 iNumericValue = inv_GetNumericItemValue(pEnt, pchNumeric);
				if (iNumericValue + iUnmetRequiredNumericRange < pDef->pRequirements->iRequiredNumericValue)	
				{
					return;
				}
			}
		}
	}

	if (pchStringSearch)
	{
		const char* pchDisplayName = TranslateDisplayMessage(pDef->msgDisplayName);

		if (!pchDisplayName || !strstri(pchDisplayName, pchStringSearch))
			return;
	}
	

	pData = eaGetStruct(peaData, parse_ItemAssignmentUI, (*piCount)++);

	SetItemAssignmentData(pEnt, pData, pDef, bIsPersonalAssignment, bIsFeatured, false);

	if ((uFlags & kGetActiveAssignmentFlags_HideUnmetRequirements) && pData->bFailsRequirements)
	{
		(*piCount)--;
		// todo: this needs a function to de-initialize an ItemAssignmentUI
		estrDestroy(&pData->estrFailsRequires);
		return;
	}

	if (pDef && !pDef->bDisabled)
	{
		ItemAssignmentWeight* pWeight = NULL;

		if (bAddCategoryHeaders && pCategorySettings && eaiFind(peaCategories, pDef->eCategory) < 0)
		{
			pData = NULL;

			// Correctish buffering, since UpdateRequirementsInfo expects some consistency between runs
			for (i = *piCount; i < eaSize(peaData); i++)
			{
				if ((*peaData)[i]->bIsHeader && (*peaData)[i]->eCategory == pDef->eCategory)
				{
					pData = eaRemove(peaData, i);
					break;
				}
			}
			if (!pData)
			{
				// New item
				pData = StructCreate(parse_ItemAssignmentUI);
			}
			eaInsert(peaData, pData, (*piCount)++);

			pData->eCategory = pDef->eCategory;
			pData->pchDisplayName = TranslateDisplayMessage(pCategorySettings->msgDisplayName);
			pData->pchDescription = NULL;
			pData->pchIcon = pCategorySettings->pchIconName;
			pData->pchImage = pCategorySettings->pchImage;
			estrClear(&pData->estrFailsRequires);
			pData->bIsHeader = true;
			pData->bFailsRequirements = false;
			pData->bIsPersonalAssignment = false;
			eaiPush(peaCategories, pDef->eCategory);
		}
	}
}

int SortAvailableItemAssignments(const ItemAssignmentUI** ppA, const ItemAssignmentUI** ppB)
{
	const ItemAssignmentUI* pA = (*ppA);
	const ItemAssignmentUI* pB = (*ppB);

	if (pA->eCategory != pB->eCategory)
		return pA->eCategory - pB->eCategory;

	if (pA->bIsHeader != pB->bIsHeader)
		return pB->bIsHeader - pA->bIsHeader;

	return stricmp(pA->pchDisplayName, pB->pchDisplayName);
}

static int SortAvailableItemAssignmentsEx(U32 *puFlags, const ItemAssignmentUI** ppA, const ItemAssignmentUI** ppB)
{
	const ItemAssignmentUI* pA = (*ppA);
	const ItemAssignmentUI* pB = (*ppB);

	if (pA->bIsHeader != pB->bIsHeader)
		return pB->bIsHeader - pA->bIsHeader;

	if ((*puFlags & kGetActiveAssignmentFlags_SortByWeight) && pA->fWeightValue != pB->fWeightValue)
		return pB->fWeightValue - pA->fWeightValue;

	if (*puFlags & kGetActiveAssignmentFlags_SortRequiredNumericAscending)
	{
		if (pA->iRequiredNumericValue != pB->iRequiredNumericValue)
			return pA->iRequiredNumericValue - pB->iRequiredNumericValue;
	}
	else if (*puFlags & kGetActiveAssignmentFlags_SortRequiredNumericDecending)
	{
		if (pA->iRequiredNumericValue != pB->iRequiredNumericValue)
			return pB->iRequiredNumericValue - pA->iRequiredNumericValue;
	}

	if (pA->iSortOrder != pB->iSortOrder)
		return pA->iSortOrder - pB->iSortOrder;

	return stricmp(pA->pchDisplayName, pB->pchDisplayName);
}


// the pchItemCategoryFilters list can be of string enumeration, or ascii integers
void GetAvailableItemAssignmentsByCategoryInternal(	Entity *pEnt, 
													ItemAssignmentUI *** peaData,
													const char *pchBagIDs, 
													const char *pchItemCategoryFilters, 
													const char *pchWeightFilters, 
													U32 uFlags,
													S32 iMinRequiredNumericValue,
													S32 iMaxRequiredNumericValue,
													S32 iUnmetRequiredNumericRange,
													const char *pchStringSearch)
{
	static ItemAssignmentCategory* peCategories = NULL;
	static ItemCategory * s_peFilterCategories = NULL;
	static F32 *s_eafWeightFilters = NULL;

	//ItemAssignmentUI*** peaData = ui_GenGetManagedListSafe(pGen, ItemAssignmentUI);
	//Entity* pEnt = entActivePlayerPtr();
	ItemAssignmentPlayerData* pPlayerData = SAFE_MEMBER2(pEnt, pPlayer, pItemAssignmentData);
	ContactDialog *pContactDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	bool bAddCategoryHeaders = !!(uFlags & kGetActiveAssignmentFlags_AddHeaders);
	bool bAddWeightHeaders = !!(uFlags & kGetActiveAssignmentFlags_AddWeightHeaders);

	S32 i, iCount = 0;

	// parse the arguments for bags and itemCategories
	{
		static const char *s_pStrToks = " \r\n\t,|%";
		char* pchParseBuffer, *pchToken, *pchContext = NULL;

		// Construct the inventory cache
		strdup_alloca(pchParseBuffer, NULL_TO_EMPTY(pchBagIDs));
		if ((pchToken = strtok_r(pchParseBuffer, s_pStrToks, &pchContext)) != NULL)
		{
			do 
			{
				InvBagIDs eBagID = StaticDefineIntGetIntDefault(InvBagIDsEnum, pchToken, InvBagIDs_None);
				if (eBagID != InvBagIDs_None)
					gclItemAssignment_CacheInventory(pEnt, eBagID);
			} while ((pchToken = strtok_r(NULL, s_pStrToks, &pchContext)) != NULL);
		}
		else 
		{
			gclItemAssignment_CacheDefaultInventoryBags(pEnt);
		}

		if (pchItemCategoryFilters && *pchItemCategoryFilters)
		{
			strdup_alloca(pchParseBuffer, NULL_TO_EMPTY(pchItemCategoryFilters));
			if ((pchToken = strtok_r(pchParseBuffer, s_pStrToks, &pchContext)) != NULL)
			{
				do  
				{
					ItemCategory eCategory;

					if (isdigit(*pchToken))
					{
						eCategory = atoi(pchToken);
					}
					else
					{
						eCategory = StaticDefineIntGetIntDefault(ItemCategoryEnum, pchToken, kItemCategory_None);
					}

					if (eCategory != kItemCategory_None)
					{
						eaiPush(&s_peFilterCategories, eCategory);
					}
				} while ((pchToken = strtok_r(NULL, s_pStrToks, &pchContext)) != NULL);
			}

			if (eaiSize(&s_peFilterCategories) == 0)
			{
				eaiPush(&s_peFilterCategories, -1);
			}
		}

		eafClear(&s_eafWeightFilters);

		if (pchWeightFilters && *pchWeightFilters)
		{
			strdup_alloca(pchParseBuffer, NULL_TO_EMPTY(pchWeightFilters));
			if ((pchToken = strtok_r(pchParseBuffer, s_pStrToks, &pchContext)) != NULL)
			{
				do {
					ItemAssignmentWeightType iWeight = StaticDefineIntGetIntDefault(ItemAssignmentWeightTypeEnum, pchToken, kItemAssignmentWeightType_Default);

					if (iWeight > kItemAssignmentWeightType_Default)
					{
						F32 fDefWeight = ItemAssignmentWeightType_GetWeightValue(iWeight);
						eafPush(&s_eafWeightFilters, fDefWeight);
					}
				} while ((pchToken = strtok_r(NULL, s_pStrToks, &pchContext)) != NULL);
			}
		}
	}

	if (pPlayerData)
	{
		if (!(uFlags & kGetActiveAssignmentFlags_ExcludeNonPersonal))
		{
			for (i = eaSize(&pPlayerData->eaVolumeAvailableAssignments)-1; i >= 0; i--)
			{
				ItemAssignmentDefRef* pRef = pPlayerData->eaVolumeAvailableAssignments[i];
				ItemAssignmentDef* pDef = GET_REF(pRef->hDef);
				if (pDef)
				{
					gclAddItemAssignmentToList(pEnt, pDef, s_peFilterCategories, s_eafWeightFilters, peaData, &iCount, 
												&peCategories, false, bAddCategoryHeaders, 
												pRef->bFeatured, uFlags, iMinRequiredNumericValue, iMaxRequiredNumericValue,
												iUnmetRequiredNumericRange, pchStringSearch);
				}

			}
		}
		if (!(uFlags & kGetActiveAssignmentFlags_ExcludePersonal))
		{
			ItemAssignmentPersonalIterator it;
			ItemAssignmentDefRef* pRef = NULL;

			ItemAssignments_InitializeIteratorPersonalAssignments(&it);
			while (pRef = ItemAssignments_IterateOverPersonalAssignments(pPlayerData, &it))
			{
				ItemAssignmentDef* pDef = GET_REF(pRef->hDef);
				if (pDef)
				{
					gclAddItemAssignmentToList(pEnt, pDef, s_peFilterCategories, s_eafWeightFilters, peaData, &iCount, 
												&peCategories, true, bAddCategoryHeaders,
												pRef->bFeatured, uFlags, iMinRequiredNumericValue, iMaxRequiredNumericValue,
												iUnmetRequiredNumericRange, pchStringSearch);
				}

			}
		}
	}

	if (pContactDialog && pContactDialog->screenType == ContactScreenType_ItemAssignments && (uFlags & kGetActiveAssignmentFlags_IncludeContact))
	{
		for (i = eaSize(&pContactDialog->eaItemAssignments)-1; i >= 0; i--)
		{
			ItemAssignmentDefRef* pRef = pContactDialog->eaItemAssignments[i];
			ItemAssignmentDef* pDef = GET_REF(pRef->hDef);
			if (pDef)
				gclAddItemAssignmentToList(pEnt, pDef, s_peFilterCategories, s_eafWeightFilters, peaData, &iCount, 
											&peCategories, true, bAddCategoryHeaders,
											pRef->bFeatured, uFlags, iMinRequiredNumericValue, iMaxRequiredNumericValue,
											iUnmetRequiredNumericRange, pchStringSearch);

		}
	}

	eaSetSizeStruct(peaData, parse_ItemAssignmentUI, iCount);

	if (uFlags & (kGetActiveAssignmentFlags_SortRequiredNumericAscending | 
					kGetActiveAssignmentFlags_SortRequiredNumericDecending | 
					kGetActiveAssignmentFlags_SortByWeight))
	{
		eaQSort_s(*peaData, SortAvailableItemAssignmentsEx, &uFlags);
	}
	else
	{
		eaQSort(*peaData, SortAvailableItemAssignments);
	}

	if (bAddWeightHeaders)
	{
		F32 fLastWeightValue = -1;

		// create a header for each rank
		for (i = 0; i < eaSize(peaData); ++i)
		{
			ItemAssignmentUI *pItemAssignment = (*peaData)[i];
			if (pItemAssignment->fWeightValue != fLastWeightValue)
			{
				ItemAssignmentWeight *pWeight = ItemAssignmentSettings_GetWeightDef(pItemAssignment->fWeightValue);
				if (pWeight)
				{
					ItemAssignmentUI* pHeader = eaGetStruct(peaData, parse_ItemAssignmentUI, iCount++);

					fLastWeightValue = pItemAssignment->fWeightValue;

					if (pHeader)
					{
						pHeader->bIsHeader = true;
						pHeader->pchDisplayName = TranslateDisplayMessage(pWeight->msgDisplayName);
						pHeader->iRequiredNumericValue = -1;
						pHeader->fWeightValue = pItemAssignment->fWeightValue;

						eaRemoveFast(peaData, iCount - 1);
						eaInsert(peaData, pHeader, i);
						i++;
					}
				}
			}

		}

	}


	eaiClear(&peCategories);
	eaiClear(&s_peFilterCategories);
	//ui_GenSetManagedListSafe(pGen, peaData, ItemAssignmentUI, true);
}

bool ItemAssignmentsCanSlotItem(Entity* pEnt, S32 iAssignmentSlot, S32 eBagID, S32 iBagSlot, GameAccountDataExtract* pExtract)
{
	ItemAssignmentSlotUI* pSlotUI = eaGet(&pIACache->eaSlots, iAssignmentSlot);
	ItemAssignmentDef* pDef = pSlotUI ? GET_REF(pSlotUI->hDef) : NULL;
	ItemAssignmentSlot* pSlot = pDef ? eaGet(&pDef->eaSlots, iAssignmentSlot) : NULL;
	if (pEnt && pEnt->pPlayer && pSlot)
	{
		if (ItemAssignments_CanSlotItem(pEnt, pSlot, eBagID, iBagSlot, pExtract))
		{
			return true;
		}
	}
	return false;
}

void ItemAssignmentsClearSlottedItem(SA_PARAM_NN_VALID ItemAssignmentSlotUI* pSlotUI)
{
	REMOVE_HANDLE(pSlotUI->hItemDef);
	pSlotUI->uItemID = 0;
	pSlotUI->eBagID = 0;
	pSlotUI->iBagSlot = 0;
}

static void gclItemAssignmentGetCategoriesString(	ItemCategory* peCategories, 
													F32 fModifier, 
													ItemCategory** peaAddedCategories, 
													char** pestrCategories,
													char** pestrCategoriesRaw)
{
	const char* pchSeparator = TranslateMessageKey("ItemAssignment_CategorySeparator");
	const char* pchCategoryName;
	const char* pchTranslatedName;
	char pchMessageKey[MAX_PATH];
	S32 i, iCount = 0;
	for (i = 0; i < eaiSize(&peCategories); i++)
	{
		ItemCategory eCategory = peCategories[i];

		if (peaAddedCategories && eaiFind(peaAddedCategories, eCategory) >= 0)
			continue;

		// Get the category name
		pchCategoryName = StaticDefineIntRevLookup(ItemCategoryEnum, eCategory);

		if (pchCategoryName && pchCategoryName[0])
		{
			// See if there is a message for this category
			sprintf(pchMessageKey, "StaticDefine_ItemCategory_%s", pchCategoryName);
			pchTranslatedName = TranslateMessageKey(pchMessageKey);
			if (pchTranslatedName)
			{
				if (iCount > 0)
					estrAppend2(pestrCategories, pchSeparator);

				if (fModifier > FLT_EPSILON)
				{
					FormatGameMessageKey(pestrCategories, "ItemAssignment_CategoryBonus", 
						STRFMT_STRING("Name", pchTranslatedName), STRFMT_END);
				}
				else if (fModifier < -FLT_EPSILON)
				{
					FormatGameMessageKey(pestrCategories, "ItemAssignment_CategoryPenalty", 
						STRFMT_STRING("Name", pchTranslatedName), STRFMT_END);
				}
				else
				{
					estrAppend2(pestrCategories, pchTranslatedName);
				}
				iCount++;

				if (peaAddedCategories)
				{
					eaiPush(peaAddedCategories, eCategory);
				}
			}

			if (i != 0)
			{
				estrConcatChar(pestrCategoriesRaw, ' ');
			}

			estrAppend2(pestrCategoriesRaw, pchCategoryName);
		}
	}
}

// sets up strings as well as any modifier values
static void gclItemAssignmentSetSlotUIData(Entity* pEnt,
											ItemAssignmentDef* pDef, 
											ItemAssignmentSlotUI* pSlotUI, 
											ItemAssignmentSlot* pSlot,
											Item* pItem)
{
	S32 i;
	ItemCategory* peAffectedCategories = NULL;
	const char** ppchOutcomeModifiers = ItemAssignments_GetOutcomeModifiersForSlot(pSlot);

	// Affected item categories
	estrClear(&pSlotUI->estrAffectedCategories);
	estrClear(&pSlotUI->estrAffectedCategoriesRaw);

	if (g_ItemAssignmentSettings.pOutcomeWeightWindowConfig == NULL)
	{
		for (i = 0; i < eaSize(&ppchOutcomeModifiers); i++)
		{
			const char* pchModName = ppchOutcomeModifiers[i];
			ItemAssignmentOutcomeModifier* pModifier = ItemAssignment_GetModifierByName(pDef, pchModName);
			ItemAssignmentOutcomeModifierTypeData* pModType = pModifier ? ItemAssignment_GetModifierTypeData(pModifier->eType) : NULL;

			if (pModType)
			{
				F32 fModifier = ItemAssignments_GetOutcomeWeightModifier(	pEnt, NULL, pModifier, 
																			pModType, pSlot, pItem, kItemCategory_None);
				gclItemAssignmentGetCategoriesString(	pModifier->peItemCategories, 
														fModifier, 
														&peAffectedCategories, 
														&pSlotUI->estrAffectedCategories, 
														&pSlotUI->estrAffectedCategoriesRaw);
			}
		}
		eaiDestroy(&peAffectedCategories);
	}
	else
	{
		pSlotUI->fOutcomeModifyValue = ItemAssignments_CalculateOutcomeDisplayValueForSlot(pDef, pItem);
	}
	
	// if we have duration scales, calculate a value that might make sense to the user
	if (g_ItemAssignmentSettings.pDefaultDurationScaleModifierSettings)
	{
		pSlotUI->fDurationModifyValue = ItemAssignments_GetDurationModifier(pEnt, pItem, 
																			g_ItemAssignmentSettings.pDefaultDurationScaleModifierSettings, 
																			pSlot, kItemCategory_None);
		// we've decided to just show the unmodified value
		//pSlotUI->fDurationModifyValue = 1.f - (1.f / (1.f + pSlotUI->fDurationModifyValue));
	}
	

	// Required item categories
	estrClear(&pSlotUI->estrRequiredCategories);
	estrClear(&pSlotUI->estrRequiredCategoriesRaw);
	gclItemAssignmentGetCategoriesString(pSlot->peRequiredItemCategories, 0.0f, NULL, &pSlotUI->estrRequiredCategories, &pSlotUI->estrRequiredCategoriesRaw);
}

void ItemAssignmentsSlotItemCheckSwap(Entity* pEnt, ItemAssignmentSlotUI* pNewSlot, S32 eBagID, S32 iBagSlot, Item* pItem)
{
	ItemAssignmentDef* pDef = GET_REF(pNewSlot->hDef);
	ItemAssignmentSlot* pSlot = pDef ? eaGet(&pDef->eaSlots, pNewSlot->iAssignmentSlot) : NULL;
	S32 i;

	// Try to find the slot that this item currently resides in
	for (i = eaSize(&pIACache->eaSlots)-1; i >= 0; i--)
	{
		ItemAssignmentSlotUI* pSlotUI = pIACache->eaSlots[i];
		if (pSlotUI == pNewSlot)
		{
			continue;
		}
		if (eBagID == pSlotUI->eBagID && iBagSlot == pSlotUI->iBagSlot)
		{
			// Copy the data from the new slot into the old slot
			StructCopyAll(parse_ItemAssignmentSlotUI, pNewSlot, pSlotUI);
			pSlotUI->iAssignmentSlot = i;
			break;
		}
	}

	// Set the new slot data
	COPY_HANDLE(pNewSlot->hItemDef, pItem->hItem);
	pNewSlot->uItemID = pItem->id;
	pNewSlot->eBagID = eBagID;
	pNewSlot->iBagSlot = iBagSlot;
	gclItemAssignmentSetSlotUIData(pEnt, pDef, pNewSlot, pSlot, pItem);
}

// compiles the static ItemAssignmentSlotUI slot list pIACache->eaSlots for the given def
bool buildSlotsForItemAssignment(Entity *pEnt, ItemAssignmentDef *pDef, bool bIgnoreUpdateThrottle, ItemAssignmentSlotUI ***peaItemAssignmentSlotUICache, S32 *piCacheCount)
{
	static ItemList s_UsedSet;
	//Entity* pEnt = entActivePlayerPtr();
	S32 i, iCount = 0;
	static bool s_bFull = true;
	static U32 s_iLastUpdateTime = 0;
	static S32 s_iLastCount = 0;

	// only allow update once per frame
	if (!bIgnoreUpdateThrottle && s_iLastUpdateTime == ItemAssignment_GetElapsedTime())
	{
		*piCacheCount = s_iLastCount;
		return s_bFull;
	}

	s_bFull = true;
	s_iLastUpdateTime = ItemAssignment_GetElapsedTime();

	gclItemAssignment_CacheDefaultInventoryBags(pEnt);

	if (pEnt)
	{
		GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		ItemList_Reset(&s_UsedSet);
		for (i = 0; i < eaSize(&pDef->eaSlots); i++)
		{
			ItemAssignmentSlot* pSlot = pDef->eaSlots[i];
			ItemAssignmentSlotUI* pSlotUI = eaGetStruct(peaItemAssignmentSlotUICache, parse_ItemAssignmentSlotUI, *piCacheCount + iCount++);

			pSlotUI->pchIcon = pSlot->pchIcon;

			if (!pSlotUI->pchIcon)
			{	// go through the required categories and see if it has an icon and use that
				S32 iNumRequired = eaiSize(&pSlot->peRequiredItemCategories);
				S32 xx;
				for (xx = 0; xx < iNumRequired; ++xx)
				{
					ItemCategory eCat = pSlot->peRequiredItemCategories[xx];
					ItemCategoryInfo* pCatInfo = item_GetItemCategoryInfo(eCat);
					if (pCatInfo && pCatInfo->pchIcon)
					{
						pSlotUI->pchIcon = pCatInfo->pchIcon;
						break;
					}
				}
			}

			if (GET_REF(pSlotUI->hDef) != pDef || pSlotUI->iAssignmentSlot != i)
			{
				// If the slot changed somehow, then reset all fields on the slot
				SET_HANDLE_FROM_REFERENT(g_hItemAssignmentDict, pDef, pSlotUI->hDef);
				pSlotUI->iAssignmentSlot = i;
				pSlotUI->bOptionalSlot = pSlot->bIsOptional;
				gclItemAssignmentSetSlotUIData(pEnt, pDef, pSlotUI, pSlot, NULL);
				ItemAssignmentsClearSlottedItem(pSlotUI);
				gclItemAssignmentSlot_CheckInventory(pSlot, NULL, &pSlotUI->eFailsReason, &s_UsedSet);
			}
			else
			{
				Item* pItem = inv_GetItemFromBag(pEnt, pSlotUI->eBagID, pSlotUI->iBagSlot, pExtract);
				if (!pItem || pItem->id != pSlotUI->uItemID)
				{
					gclItemAssignmentSetSlotUIData(pEnt, pDef, pSlotUI, pSlot, NULL);

					// Clear item information if the item moved
					ItemAssignmentsClearSlottedItem(pSlotUI);
				}
				gclItemAssignmentSlot_CheckInventory(pSlot, NULL, &pSlotUI->eFailsReason, &s_UsedSet);
			}
			if (GET_REF(pSlotUI->hItemDef) == NULL)
				s_bFull = false;
		}
	}

	*piCacheCount += iCount;
	s_iLastCount = iCount;
	return s_bFull;
}

bool SetGenSlotsForItemAssignment(	Entity *pEnt, ItemAssignmentSlotUI ***peaItemAssignmentSlotUIs, S32 *piCachedCount, 
									bool bIgnoreUpdateThrottle, ItemAssignmentSlotUI*** peaDataOut, const char* pchAssignmentDef, U32 uOptionFlags)
{
	//ItemAssignmentSlotUI*** peaData = ui_GenGetManagedListSafe(pGen, ItemAssignmentSlotUI);
	ItemAssignmentDef* pDef = ItemAssignment_DefFromName(pchAssignmentDef);
	bool bFull = true;

	PERFINFO_AUTO_START_FUNC();

	eaClear(peaDataOut);

	if (pDef)
	{
		S32 i, iStartCacheIdx;

		iStartCacheIdx = *piCachedCount;

		bFull = buildSlotsForItemAssignment(pEnt, pDef, bIgnoreUpdateThrottle, peaItemAssignmentSlotUIs, piCachedCount);

		for (i = 0; i < eaSize(&pDef->eaSlots); i++)
		{
			ItemAssignmentSlot* pSlot = pDef->eaSlots[i];
			ItemAssignmentSlotUI* pSlotUI = eaGet(peaItemAssignmentSlotUIs, iStartCacheIdx + i);

			if (!pSlotUI)
				continue;

			if (((uOptionFlags & ITEM_ASSIGNMENT_SLOTOPTION_EXCLUDE_OPTIONAL) && pSlot->bIsOptional) || 
				((uOptionFlags & ITEM_ASSIGNMENT_SLOTOPTION_EXCLUDE_REQUIRED) && !pSlot->bIsOptional))
			{
				continue;
			}

			eaPush(peaDataOut, pSlotUI);
		}
	}

	//ui_GenSetManagedListSafe(pGen, peaDataOut, ItemAssignmentSlotUI, false);
	PERFINFO_AUTO_STOP();
	return bFull;
}

// Gets the list of required items as an array of InventorySlot's. Only count and the hItemDef are valid values.
S32 ItemAssignmentsGetRequiredItemsInternal(Entity *pEnt, InventorySlot ***peaInvSlots, const char *pchItemAssignment, U32 uFlags)
{
	// InventorySlot abuse. Yay!
	// It has all the information I need (Def + Count), and it's item/inventory related.
	//
	// NB: The cache is never decreased. If for some unknown reason, some dummy calls this function
	// multiple times with different defs, we want the pointers to remain valid. Even if the contents
	// are garbage.
	//
	// Part of the reason for this is that everywhere else InventorySlots are added to a list, they
	// are unmanaged structs. This is to prevent memory leaks for when some clown calls a standard
	// inventory function and this inventory function on the same gen.
	//
	// It probably would have been less work to create a new struct, but at least this means one less
	// struct type to worry about.
	static NOCONST(InventorySlot) **s_eaCache = NULL;
	static S32 s_iCacheSlotCount = 0;
	static U32 s_uiLastFrameCount = 0;
	//InventorySlot ***peaInvSlots = NULL;
	S32 i, iCurCount = 0;
	ItemAssignmentPlayerData* pPlayerData = SAFE_MEMBER2(pEnt, pPlayer, pItemAssignmentData);
	ContactDialog *pContactDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	ItemAssignmentDef *pDef = NULL;
	NOCONST(InventorySlot) *pSlot; 

#ifdef GAMECLIENT
	// note: s_eaCache could get fairly large depending on the UI and setup- we'll need a reliable way to cleanup this list.
	if (g_ui_State.uiFrameCount != s_uiLastFrameCount)
	{
		static bool s_bWarned = false;
		s_uiLastFrameCount = g_ui_State.uiFrameCount;
		if (!s_bWarned && s_iCacheSlotCount > 200)
		{
			Alertf("ItemAssignmentUI: InventorySlot cache has gotten very large %d.", s_iCacheSlotCount);
			s_bWarned = true;
		}
		s_iCacheSlotCount = 0;
	}
#endif

	//peaInvSlots = ui_GenGetManagedListSafe(pGen, InventorySlot);

	eaClearFast(peaInvSlots);

	if (pPlayerData)
	{
		for (i = eaSize(&pPlayerData->eaVolumeAvailableAssignments)-1; i >= 0; i--)
		{
			ItemAssignmentDefRef* pRef = pPlayerData->eaVolumeAvailableAssignments[i];
			if (!stricmp(pchItemAssignment, REF_STRING_FROM_HANDLE(pRef->hDef)))
			{
				pDef = GET_REF(pRef->hDef);
				break;
			}
		}
		if (!pDef)
		{
			ItemAssignmentPersonalIterator it;
			ItemAssignmentDefRef* pRef = NULL;

			ItemAssignments_InitializeIteratorPersonalAssignments(&it);
			while (pRef = ItemAssignments_IterateOverPersonalAssignments(pPlayerData, &it))
			{
				if (!stricmp(pchItemAssignment, REF_STRING_FROM_HANDLE(pRef->hDef)))
				{
					pDef = GET_REF(pRef->hDef);
					break;
				}
			}
		}
	}
	if (!pDef && pContactDialog && pContactDialog->screenType == ContactScreenType_ItemAssignments)
	{
		for (i = eaSize(&pContactDialog->eaItemAssignments)-1; i >= 0; i--)
		{
			ItemAssignmentDefRef* pRef = pContactDialog->eaItemAssignments[i];
			if (!stricmp(pchItemAssignment, REF_STRING_FROM_HANDLE(pRef->hDef)))
			{
				pDef = GET_REF(pRef->hDef);
				break;
			}
		}
	}

	if (pDef)
	{
		ItemAssignmentRequirements *pReqs = pDef->pRequirements;

		if (pReqs)
		{
			for (i = 0; i < eaSize(&pReqs->eaItemCosts); i++)
			{
				ItemAssignmentItemCost* pItemCost = pReqs->eaItemCosts[i];
				pSlot = eaGetStructNoConst(&s_eaCache, parse_InventorySlot, s_iCacheSlotCount++);
				iCurCount++;

				// Initialize the fake item instance
				if (!pSlot->pItem)
					pSlot->pItem = StructCreateNoConst(parse_Item);
				if (pSlot->pItem)
				{
					COPY_HANDLE(pSlot->pItem->hItem, pItemCost->hItem);
					pSlot->pItem->count = pItemCost->iCount;
				}
				eaPush(peaInvSlots, CONTAINER_RECONST(InventorySlot, pSlot));
			}

			if (!(uFlags & ITEM_ASSIGNMENT_REQUIRED_ITEMS_EXLUDE_NUMERIC) && 
				IS_HANDLE_ACTIVE(pReqs->hRequiredNumeric))
			{
				pSlot = eaGetStructNoConst(&s_eaCache, parse_InventorySlot, s_iCacheSlotCount++);
				iCurCount++;

				// Initialize the fake item instance
				if (!pSlot->pItem)
					pSlot->pItem = StructCreateNoConst(parse_Item);
				if (pSlot->pItem)
				{
					COPY_HANDLE(pSlot->pItem->hItem, pReqs->hRequiredNumeric);
					pSlot->pItem->count = pReqs->iRequiredNumericValue;
				}
				eaPush(peaInvSlots, CONTAINER_RECONST(InventorySlot, pSlot));
			}
		}
	}

	//ui_GenSetManagedListSafe(pGen, peaInvSlots, InventorySlot, false);
	return iCurCount;
}

bool ItemAssignments_HandleNewAssignment(const char *pchName)
{
	ItemAssignmentDef *pDef = RefSystem_ReferentFromString(g_hItemAssignmentDict, pchName);
	if (pDef)
	{
		Message *pDisplayName = GET_REF(pDef->msgDisplayName.hMessage);
		Message *pDescription = GET_REF(pDef->msgDescription.hMessage);
		if ((pDisplayName || !IS_HANDLE_ACTIVE(pDef->msgDisplayName.hMessage))
			&& (pDescription || !IS_HANDLE_ACTIVE(pDef->msgDescription.hMessage)))
		{
			globCmdParsef("ItemAssignmentUI_NewAssignment %s", pchName);
			return true;
		}
	}
	return false;
}

void ItemAssignment_AddChainToList(Entity *pEnt, ItemAssignmentUI ***peaData, S32 *piCount, ItemAssignmentDef *pDef, U32 uFlags, bool bIsPersonalAssignment)
{
	ItemAssignmentUI *pData = NULL;
	S32 i;

	if (!pDef)
		return;
	if (pDef->pRequirements && IS_HANDLE_ACTIVE(pDef->pRequirements->hRequiredAllegiance)
		&& GET_REF(pDef->pRequirements->hRequiredAllegiance) != GET_REF(pEnt->hAllegiance)
		&& GET_REF(pDef->pRequirements->hRequiredAllegiance) != GET_REF(pEnt->hSubAllegiance))
	{
		return;
	}
	if (uFlags & kItemAssignmentChainFlag_ExcludeAssignments)
	{
		// If only showing headers, then ignore assignments that have a required assignment
		if (pDef->pRequirements && IS_HANDLE_ACTIVE(pDef->pRequirements->hRequiredAssignment))
			return;
	}
	if (uFlags & kItemAssignmentChainFlag_ExcludeRepeatable)
	{
		if (pDef->bRepeatable)
			return;
	}

	// Don't duplicate assignments...
	for (i = *piCount - 1; i >= 0; i--)
	{
		if (pDef == GET_REF((*peaData)[i]->hDef))
			return;
	}

	if (!(uFlags & kItemAssignmentChainFlag_ExcludeAssignments))
	{
		// Correctish buffering, since UpdateRequirementsInfo expects some consistency between runs
		for (i = *piCount; i < eaSize(peaData); i++)
		{
			if (!(*peaData)[i]->bIsHeader && pDef == GET_REF((*peaData)[i]->hDef))
			{
				pData = eaRemove(peaData, i);
				break;
			}
		}
		if (!pData)
		{
			// New item
			pData = StructCreate(parse_ItemAssignmentUI);
		}
		eaInsert(peaData, pData, (*piCount)++);

		SetItemAssignmentData(pEnt, pData, pDef, bIsPersonalAssignment, false, false);
		pData->bIsHeader = false;
	}

	if ((!pDef->pRequirements || !IS_HANDLE_ACTIVE(pDef->pRequirements->hRequiredAssignment)) && !(uFlags & kItemAssignmentChainFlag_ExcludeHeaders))
	{
		pData = NULL;

		// Correctish buffering, since UpdateRequirementsInfo expects some consistency between runs
		for (i = *piCount; i < eaSize(peaData); i++)
		{
			if ((*peaData)[i]->bIsHeader && pDef == GET_REF((*peaData)[i]->hDef))
			{
				pData = eaRemove(peaData, i);
				break;
			}
		}
		if (!pData)
		{
			// New item
			pData = StructCreate(parse_ItemAssignmentUI);
		}
		eaInsert(peaData, pData, (*piCount)++);

		SetItemAssignmentData(pEnt, pData, pDef, bIsPersonalAssignment, false, false);
		pData->bIsHeader = true;
	}

	for (i = eaSize(&pDef->eaDependencies) - 1; i >= 0; i--)
	{
		ItemAssignmentDef *pDep = GET_REF(pDef->eaDependencies[i]->hDef);
		if (pDep)
			ItemAssignment_AddChainToList(pEnt, peaData, piCount, pDep, uFlags, bIsPersonalAssignment);
	}
}

int ItemAssignment_OrderChains(const void *ppvA, const void *ppvB, const void *pContext)
{
	ItemAssignmentUI *pA = *(ItemAssignmentUI **)ppvA;
	ItemAssignmentUI *pB = *(ItemAssignmentUI **)ppvB;
	ItemAssignmentDef *pADef = GET_REF(pA->hDef);
	ItemAssignmentDef *pBDef = GET_REF(pB->hDef);
	ItemAssignmentDef *pCur;
	U32 uFlags = (pContext ? *(U32 *)pContext : 0);
	bool bAIncomplete = pA->iChainLengthCompleted < pA->iChainDepth + ((uFlags & kItemAssignmentChainFlag_ChainLengthExcludeRepeatable) ? pA->iChainLengthNoRepeats : pA->iChainLength);
	bool bBIncomplete = pB->iChainLengthCompleted < pB->iChainDepth + ((uFlags & kItemAssignmentChainFlag_ChainLengthExcludeRepeatable) ? pB->iChainLengthNoRepeats : pB->iChainLength);

	if (GET_REF(pA->hDef) == GET_REF(pB->hDef))
	{
		return !!pB->bIsHeader - !!pA->bIsHeader;
	}

	if (bAIncomplete != bBIncomplete)
		return !!bBIncomplete - !!bAIncomplete;

	// Sort by dependencies
	for (pCur = pADef; pCur; pCur = pCur->pRequirements ? GET_REF(pCur->pRequirements->hRequiredAssignment) : NULL)
	{
		if (pCur == pBDef)
			return 1;
	}
	for (pCur = pBDef; pCur; pCur = pCur->pRequirements ? GET_REF(pCur->pRequirements->hRequiredAssignment) : NULL)
	{
		if (pCur == pADef)
			return -1;
	}

	// Sort by length of the following chain
	if (pA->iChainLength != pB->iChainLength)
		return pA->iChainLength - pB->iChainLength;

	return 0;
}

ItemAssignmentEquippedUI* FindItemAssignmentEquippedUIForItemID(U64 id)
{
	FOR_EACH_IN_EARRAY(pIACache->ppItemAssignmentUIs, ItemAssignmentEquippedUI, pItem)
	{
		if (pItem->uid == id)
			return pItem;
	}
	FOR_EACH_END

		return NULL;
}

// helper function for gclItemAssignments_GetLockedAssignmentSlotMessages
static bool _getMissionLockedMessageForPerkUnlock(	Entity* pEnt,
	ItemAssignmentSettingsSlots *pStrictSlotSettings, 
	Mission *pMission, 
	const char **ppchReasons,
	S32 iNumPerks,
	S32 iCurLockedSlot,
	S32 *piNumFoundInOut)
{
	if (!pMission->bHidden && pMission->bDiscovered)
	{
		S32 iPerkOrd = -1; 
		MissionDef *pDef = GET_REF(pMission->rootDefOrig);
		if (pDef && ((iPerkOrd = eaFind(&pStrictSlotSettings->ppchPerkUnlockSlots, pDef->name)) >= 0))
		{
			const char *pchMsg = entTranslateDisplayMessage(pEnt, pDef->uiStringMsg);
			eaSet(&ppchReasons, pchMsg, iPerkOrd);
			(*piNumFoundInOut)++;
			if ((*piNumFoundInOut) >= iNumPerks)
				return true;
			if (iCurLockedSlot+(*piNumFoundInOut) >= pStrictSlotSettings->iMaxActiveAssignmentSlots)
				return true;
		}
	}

	return false;
}

// 
static void gclItemAssignments_GetLockedAssignmentSlotMessages(	Entity* pEnt, 
	SA_PARAM_NN_VALID ItemAssignmentSettingsSlots *pStrictSlotSettings,
	ItemAssignmentUI** eaItemAssignmentUIData, 
	S32 iCurLockedSlot)
{
	S32 iNumPerks = eaSize(&pStrictSlotSettings->ppchPerkUnlockSlots); 
	S32 iNumMessages = eaSize(&pStrictSlotSettings->ppchPerkUnlockSlots) + eaSize(&pStrictSlotSettings->ppUnlockExpression);
	MissionInfo *pInfo = mission_GetInfoFromPlayer(pEnt);
	ItemAssignmentPersistedData *pData = SAFE_MEMBER2(pEnt,pPlayer,pItemAssignmentPersistedData);
	static const char **ppchReasons = NULL;

	if (pInfo && iNumMessages > 0)
	{
		S32 i, iNumFound = 0, iCurReason;
		bool bDone = false;
		if (iCurLockedSlot < 0 || iCurLockedSlot >= pStrictSlotSettings->iMaxActiveAssignmentSlots)
			return;

		eaSetSize(&ppchReasons, iNumMessages);
		for (i = 0; i < iNumMessages; ++i)
		{
			eaSet(&ppchReasons, NULL, i);
		}


		FOR_EACH_IN_EARRAY(pInfo->eaDiscoveredMissions, Mission, pMission)
		{
			if (_getMissionLockedMessageForPerkUnlock(pEnt, pStrictSlotSettings, pMission, ppchReasons, iNumPerks, iCurLockedSlot, &iNumFound))
			{
				bDone = true;
				break;
			}
		}
		FOR_EACH_END

		if (!bDone)
		{
			FOR_EACH_IN_EARRAY(pInfo->missions, Mission, pMission)
			{
				if (_getMissionLockedMessageForPerkUnlock(pEnt, pStrictSlotSettings, pMission, ppchReasons, iNumPerks, iCurLockedSlot, &iNumFound))
					break;
			}
			FOR_EACH_END
		}

		for(i=0;i<eaSize(&pStrictSlotSettings->ppUnlockExpression);i++)
		{
			if(ea32Find(&pData->eaItemAssignmentSlotsUnlocked,pStrictSlotSettings->ppUnlockExpression[i]->key) == -1)
			{
				const char *pchMsg = entTranslateDisplayMessage(pEnt, pStrictSlotSettings->ppUnlockExpression[i]->displayReason);
				eaSet(&ppchReasons, pchMsg, iNumFound);
				iNumFound++;
			}
		}

		iCurReason = 0;
		for (i = iCurLockedSlot; i < pStrictSlotSettings->iMaxActiveAssignmentSlots && iCurReason < iNumMessages; ++i)
		{
			ItemAssignmentUI *pSlot = eaGet(&eaItemAssignmentUIData, i);
			if (!pSlot)
				return;

			while (iCurReason < iNumMessages && ppchReasons[iCurReason] == NULL)
			{
				iCurReason++;
			}
			pSlot->pchDisplayName = eaGet(&ppchReasons, iCurReason);
			iCurReason++;
		}
	}
}

void ItemAssignment_FillItemAssignmentSlots(Entity *pEnt, ItemAssignmentUI ***peaItemAssignmentUIData)
{
	ItemAssignmentPersistedData* pPlayerData = SAFE_MEMBER2(pEnt, pPlayer, pItemAssignmentPersistedData);

	// only enabled if g_ItemAssignmentSettings.pStrictAssignmentSlots is set
	if (pPlayerData && g_ItemAssignmentSettings.pStrictAssignmentSlots)
	{
		ItemAssignmentSettingsSlots *pItemAssignmentSlotSettings = g_ItemAssignmentSettings.pStrictAssignmentSlots;
		U32 uTimeCurrent = timeServerSecondsSince2000();
		S32 i, iFirstLockedSlot = 0;

		S32 iNumAvailableAssignmentSlots = ItemAssignments_GetNumberUnlockedAssignmentSlots(pEnt, g_ItemAssignmentSettings.pStrictAssignmentSlots);

		eaSetSizeStruct(peaItemAssignmentUIData, parse_ItemAssignmentUI, pItemAssignmentSlotSettings->iMaxActiveAssignmentSlots);

		for (i = 0; i < iNumAvailableAssignmentSlots; ++i)
		{
			ItemAssignment *pFoundAssignment = NULL;
			ItemAssignmentUI* pData = NULL;

			{
				// find the itemAssignment by slot number
				FOR_EACH_IN_EARRAY(pPlayerData->eaActiveAssignments, ItemAssignment, pAssignment)
				{
					if (pAssignment->uItemAssignmentSlot == i)
					{
						pFoundAssignment = pAssignment;
						break;
					}
				}
				FOR_EACH_END
			}

			if (pFoundAssignment)
			{
				pData = ItemAssignment_GetAssignment(peaItemAssignmentUIData, i, pFoundAssignment->uAssignmentID, -1, NULL, false);
				if (pData)
					FillActiveItemAssignment(pData, pFoundAssignment, pEnt, pPlayerData, uTimeCurrent);
			}
			else
			{
				pData = ItemAssignment_GetAssignment(peaItemAssignmentUIData, i, 0, -1, NULL, false);
				// todo: fill in the empty assignment
				if (pData)
				{
					pData->bIsLockedSlot = false;
					pData->uAssignmentID = 0;
				}
			}

			if (pData)
				pData->iSlotIndex = i;

		}

		iFirstLockedSlot = i;

		// go through the rest and created locked version
		for (i; i < pItemAssignmentSlotSettings->iMaxActiveAssignmentSlots; ++i)
		{
			ItemAssignmentUI* pData = NULL;
			pData = ItemAssignment_GetAssignment(peaItemAssignmentUIData, i, 0, -1, NULL, false);

			if (pData)
			{	// these are locked
				pData->bIsLockedSlot = true;
				pData->iLevelUnlocked = ItemAssignments_GetRankRequiredToUnlockSlot(g_ItemAssignmentSettings.pStrictAssignmentSlots, i);
				pData->uAssignmentID = 0;
				pData->iSlotIndex = i;
			}
		}

		gclItemAssignments_GetLockedAssignmentSlotMessages(pEnt, g_ItemAssignmentSettings.pStrictAssignmentSlots, 
			*peaItemAssignmentUIData, iFirstLockedSlot);

	}
}

// --------------------------------------------------------------------------------------------------------------------
static int SortAssignmentCategoriesUI(U32 *puFlags, const ItemAssignmentCategoryUI** ppA, const ItemAssignmentCategoryUI** ppB)
{
	if ((*ppB)->iCurrentRank != (*ppA)->iCurrentRank)
		return ((*ppB)->iCurrentRank - (*ppA)->iCurrentRank);

	return ((*ppA)->iSortOrder - (*ppB)->iSortOrder);
}

void ItemAssignment_FillAssignmentCategories(Entity *pEnt, ItemAssignmentCategoryUI ***pppCategories, U32 uFlags)
{
	const ItemAssignmentCategorySettings **eaCateogries = NULL;
	S32 iModelListCount = 0;
	bool bDevMode = (entGetAccessLevel(pEnt) >= 9);

	if (pEnt)
	{
		eaCateogries = ItemAssignmentCategory_GetCategoryList();

		FOR_EACH_IN_EARRAY(eaCateogries, const ItemAssignmentCategorySettings, pCategory)
		{
			ItemAssignmentCategoryUI* pCatUI = NULL;

			if (ItemAssignments_EvaluateCategoryIsHidden(pEnt, pCategory))
				continue;

			pCatUI = eaGetStruct(pppCategories, parse_ItemAssignmentCategoryUI, iModelListCount++);

			if (pCatUI)
			{
				pCatUI->pchName = pCategory->pchName;
				pCatUI->eCategory = pCategory->eCategory;
				pCatUI->pchDisplayName = TranslateDisplayMessage(pCategory->msgDisplayName);
				pCatUI->pchIcon = pCategory->pchIconName;
				pCatUI->bIsHeader = false;
				pCatUI->iSortOrder = pCategory->iSortOrder;

				if (pCategory->pchNumericRank1)
					pCatUI->iCurrentRank = inv_GetNumericItemValue(pEnt, pCategory->pchNumericRank1);
				if (pCategory->pchNumericXP1)
					pCatUI->iCurrentXP = inv_GetNumericItemValue(pEnt, pCategory->pchNumericXP1);

				pCatUI->iNextRank = pCatUI->iCurrentRank + 1;

				if (g_ItemAssignmentSettings.pCategoryRankingSchedule)
				{
					S32 xpForCurrentLevel;
					S32 iNumRanks = ItemAssignments_GetNumRanks(g_ItemAssignmentSettings.pCategoryRankingSchedule);

					pCatUI->iCurrentRank = CLAMP(pCatUI->iCurrentRank, 0, iNumRanks);
					pCatUI->iNextRank = CLAMP(pCatUI->iNextRank, 0, iNumRanks);

					xpForCurrentLevel = ItemAssignments_GetExperienceThresholdForRank(g_ItemAssignmentSettings.pCategoryRankingSchedule, pCatUI->iCurrentRank);
					pCatUI->iNextXP = ItemAssignments_GetExperienceThresholdForRank(g_ItemAssignmentSettings.pCategoryRankingSchedule, pCatUI->iNextRank);

					if (pCatUI->iCurrentXP >= xpForCurrentLevel && 
						xpForCurrentLevel < pCatUI->iNextXP)
					{
						pCatUI->fPercentageThroughCurrentLevel = (F32)(pCatUI->iCurrentXP - xpForCurrentLevel)/(F32)(pCatUI->iNextXP - xpForCurrentLevel); 
					}
					else
					{
						pCatUI->fPercentageThroughCurrentLevel = 0.f;
					}
				}
			}

		}
		FOR_EACH_END

		// first sort it from greatest to least, then create the headers
		eaQSort_s((*pppCategories), SortAssignmentCategoriesUI, &uFlags);

		if (uFlags & lItemAssignmentCategoryUIFlags_RankHeaders)
		{
			S32 iLastRank = -1;
			S32 i;
			
			// create a header for each rank
			for (i = 0; i < eaSize(pppCategories); ++i)
			{
				ItemAssignmentCategoryUI *pCategory = (*pppCategories)[i];

				if (iLastRank != pCategory->iCurrentRank)
				{
					ItemAssignmentCategoryUI* pCatHeader = eaGetStruct(pppCategories, parse_ItemAssignmentCategoryUI, iModelListCount++);
					iLastRank = pCategory->iCurrentRank;

					if (pCatHeader)
					{
						pCatHeader->bIsHeader = true;
						pCatHeader->iCurrentRank = pCategory->iCurrentRank;
						eaRemoveFast(pppCategories, iModelListCount - 1);
						eaInsert(pppCategories, pCatHeader, i);
						i++;
					}
				}
			}
		}

	}
}

// Searches our static list pIACache->pRewardRequestData for a given outcome reward
// pchOutcomePooled is assumed to be a pooled string
ItemAssignmentOutcomeRewardRequest* ItemAssignmentsUI_FindRewardRequestOutcome(const char* pchOutcomePooled)
{
	FOR_EACH_IN_EARRAY(pIACache->pRewardRequestData->eaOutcomes, ItemAssignmentOutcomeRewardRequest, pOutcomeRequest)
	{
		if (pchOutcomePooled == pOutcomeRequest->pchOutcome)
			return pOutcomeRequest;
	}
	FOR_EACH_END

		return NULL;
}

// returns true if there was anything found in the list
bool ItemAssignment_GetFilteredRewards(Entity *pEnt,
										InventorySlot*** peaData,
										ItemAssignmentDef *pAssignmentDef, 
										const char* pchOutcome, 
										U32 uFlags,
										bool bCheckOnly)
{
	

	if (pEnt && pIACache->pRewardRequestData && (!pAssignmentDef || GET_REF(pIACache->pRewardRequestData->hDef) == pAssignmentDef))
	{
		static InventorySlot **s_eaFilteredRewards = NULL;
		const char* pchOutcomePooled = allocFindString(pchOutcome);
		ItemAssignmentOutcomeRewardRequest* pOutcomeRequest = NULL;

		eaClear(&s_eaFilteredRewards);

		pOutcomeRequest = ItemAssignmentsUI_FindRewardRequestOutcome(pchOutcomePooled);
		if (pOutcomeRequest && pOutcomeRequest->pData)
		{
			// copy over the list to a temporary array where we can start filtering it
			eaCopy(&s_eaFilteredRewards, &pOutcomeRequest->pData->eaRewards);

			// see if we need to filter off any rewards that do not show up on another outcome
			if (uFlags & (kItemAssignmentRewardsFlags_UnionOfOtherOutcome | kItemAssignmentRewardsFlags_DifferenceOfOtherOutcome))
			{
				ItemAssignmentOutcomeRewardRequest *pOtherOutcome = NULL;

				// first find another outcome that isn't this one
				FOR_EACH_IN_EARRAY(pIACache->pRewardRequestData->eaOutcomes, ItemAssignmentOutcomeRewardRequest, pFindRequest)
				{
					if (pFindRequest->pchOutcome != pchOutcomePooled)
					{
						pOtherOutcome = pFindRequest;
						break;
					}
				}
				FOR_EACH_END

					if (pOtherOutcome && pOtherOutcome->pData)
					{
						FOR_EACH_IN_EARRAY(s_eaFilteredRewards, InventorySlot, pReward)
						{
							bool bFound = false;

							if (pReward->pItem)
							{
								FOR_EACH_IN_EARRAY(pOtherOutcome->pData->eaRewards, InventorySlot, pRewardOther)
								{
									if (pRewardOther->pItem && 
										REF_COMPARE_HANDLES(pReward->pItem->hItem, pRewardOther->pItem->hItem))
									{
										bFound = true;
										break;
									}
								}
								FOR_EACH_END
							}	

							if ((!bFound && uFlags&kItemAssignmentRewardsFlags_UnionOfOtherOutcome) ||
								(bFound && uFlags&kItemAssignmentRewardsFlags_DifferenceOfOtherOutcome))
							{
								eaRemove(&s_eaFilteredRewards, FOR_EACH_IDX(-,pReward));
							}
						}
						FOR_EACH_END
					}
			}


			// check if we want to filter this list , otherwise just copy the whole thing
			if (uFlags & ItemAssignmentRewardsFlags_NUMERICS)
			{
				FOR_EACH_IN_EARRAY_FORWARDS(s_eaFilteredRewards, InventorySlot, pInventorySlot)
				{
					if (pInventorySlot->pItem)
					{
						ItemDef *pDef = GET_REF(pInventorySlot->pItem->hItem);

						if (!pDef)
							continue;

						if (uFlags & kItemAssignmentRewardsFlags_ExcludeNumerics && pDef->eType == kItemType_Numeric)
							continue;
						if (uFlags & kItemAssignmentRewardsFlags_ExcludeNonNumerics && pDef->eType != kItemType_Numeric)
							continue;

						if (g_ItemAssignmentSettings.pchXPFilterBaseName && 
							uFlags & (kItemAssignmentRewardsFlags_ExcludeNumericXP|kItemAssignmentRewardsFlags_ExcludeNonNumericXP))
						{
							bool bIsXP = strstri(pDef->pchName, g_ItemAssignmentSettings.pchXPFilterBaseName) != NULL;
							if (bIsXP && (uFlags & kItemAssignmentRewardsFlags_ExcludeNumericXP))
								continue;

							if (!bIsXP && (uFlags & kItemAssignmentRewardsFlags_ExcludeNonNumericXP))
								continue;
						}

					}

					// after testing filters, we want this - put it into the list
					if (bCheckOnly)
						return true;

					if (peaData)
					{
						InventorySlot *pNew = StructClone(parse_InventorySlot, pInventorySlot);
						eaPush(peaData, pNew);
					}
				}
				FOR_EACH_END
			}
			else
			{
				if (bCheckOnly)
					return true;

				if (peaData)
					eaCopyStructs(&s_eaFilteredRewards, peaData, parse_InventorySlot);
			}
		}

		eaClear(&s_eaFilteredRewards);
	}


	return peaData ? eaSize(peaData) > 0 : 0;
}

S32 ItemAssignmentPersonalUpdateTime(Entity *pEnt, S32 iPersonalBucket)
{
	ItemAssignmentPersistedData* pPersistedData = SAFE_MEMBER2(pEnt, pPlayer, pItemAssignmentPersistedData);
	if (pPersistedData)
	{
		ItemAssignmentPersonalPersistedBucket *pBucket = eaGet(&pPersistedData->eaPersistedPersonalAssignmentBuckets, iPersonalBucket);
		ItemAssignmentPersonalAssignmentSettings *pBucketSettings = eaGet(&g_ItemAssignmentSettings.eaPersonalAssignmentSettings, iPersonalBucket);
		if (pBucket && pBucketSettings)
		{
			U32 uNow = timeServerSecondsSince2000();
			U32 uPersonalRefreshTime = pBucketSettings->uAssignmentRefreshTime;
			U32 uNextUpdateTime = pBucket->uLastPersonalUpdateTime + uPersonalRefreshTime;

			if (uNextUpdateTime > uNow)
			{
				return uNextUpdateTime - uNow;
			}
		}
	}
	return 0;
}


//#include "ItemAssignmentsUICommon_c_ast.c"
#include "ItemAssignmentsUICommon_h_ast.c"
