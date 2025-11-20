
#include "ItemAssignmentsUI.h"
#include "ItemAssignmentsUICommon.h"

#include "contact_common.h"
#include "Entity.h"
#include "file.h"
#include "GameAccountDataCommon.h"
#include "GameStringFormat.h"
#include "gclEntity.h"
#include "gclUIGen.h"
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
#include "UIGen.h"
#include "GameClientLib.h"
#include "GraphicsLib.h"
#include "FCInventoryUI.h"
#include "StringUtil.h"
#include "strings_opt.h"
#include "cmdparse.h"
#include "ResourceManager.h"

#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "ItemAssignments_h_ast.h"
//#include "ItemAssignmentsUI_c_ast.h"
#include "Autogen/ItemAssignmentsUI_c_ast.h"

#include "ItemAssignmentsUICommon_h_ast.h"
#include "AutoGen/FCInventoryUI_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static U32 s_uNextRewardRequestTime = 0;
static ItemAssignmentDefRef** s_eaRemoteAssignmentRefs = NULL;
static U32 s_uNextRemoteRequestTime = 0;

AUTO_RUN;
void ItemAssignmentsUICreateCache(void)
{
	pIACache = StructCreate(parse_ItemAssignmentCachedStruct);
}

AUTO_STRUCT;
typedef struct ItemAssignmentModifierUI
{
	UIItemCategory UICategory; AST(EMBEDDED_FLAT)
	const char *pchModifierName; AST(POOL_STRING)
	const char *pchModifierPrefix; AST(POOL_STRING)
	F32 fModifier;

	// The actual number of modified outcomes
	S32 iModifiedOutcomes;

	const char *pchDisplayName; AST(NAME(OutcomeDisplayName, OutcomeDisplayName1) UNOWNED)
	const char *pchOutcome; AST(NAME(Outcome, Outcome1) POOL_STRING)
} ItemAssignmentModifierUI;

typedef struct SortData
{
	const char **eaPrefixes;
	const char **eaOutcomes;
} SortData;

static int gclInvCompareItemAssignmentModifierUI(const ItemAssignmentModifierUI **ppLeft, const ItemAssignmentModifierUI **ppRight, const SortData *pSortData)
{
	const UIItemCategory *pLeftCategory = &(*ppLeft)->UICategory;
	const UIItemCategory *pRightCategory = &(*ppRight)->UICategory;
	S32 iGrandPosLeft = eaFind(&pSortData->eaOutcomes, (*ppLeft)->pchModifierName);
	S32 iGrandPosRight = eaFind(&pSortData->eaOutcomes, (*ppRight)->pchModifierName);

	if (iGrandPosLeft < 0 && iGrandPosRight >= 0)
		return 1;
	if (iGrandPosLeft >= 0 && iGrandPosRight < 0)
		return -1;
	if (iGrandPosLeft >= 0 && iGrandPosRight >= 0 && iGrandPosLeft != iGrandPosRight)
		return iGrandPosLeft - iGrandPosRight;

	if (!pLeftCategory->eCategory && pRightCategory->eCategory)
		return -1;
	if (pLeftCategory->eCategory && !pRightCategory->eCategory)
		return 1;
	if (!pLeftCategory->eCategory && !pRightCategory->eCategory)
		return 0;

	return gclInvCompareUICategoryOrdered(&pLeftCategory, &pRightCategory, &pSortData->eaPrefixes);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenItemAssignmentsGetCategoryModifiers");
bool GenExprItemAssignmentsGetCategoryModifiers(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID ItemAssignmentSlotUI *pSlotUI, const char *pchOutcome, const char *pchPrefix, U32 uFlags)
{
	static SortData s_sortData;
	char *pchBuffer = NULL;
	char *pchContext = NULL;
	char *pchToken;
	Entity* pEnt = entActivePlayerPtr();
	ItemAssignmentDef *pDef = pSlotUI ? GET_REF(pSlotUI->hDef) : NULL;
	ItemAssignmentSlot *pSlot = pSlotUI && pDef ? eaGet(&pDef->eaSlots, pSlotUI->iAssignmentSlot) : NULL;
	ItemAssignmentModifierUI ***peaCategoryModifier = ui_GenGetManagedListSafe(pGen, ItemAssignmentModifierUI);
	ItemAssignmentOutcome* pOutcome;
	S32 i, j, k, iCount = 0;

	pchOutcome = allocFindString(pchOutcome);
	pOutcome = ItemAssignment_GetOutcomeByName(pDef, pchOutcome);
	if (!pOutcome)
	{
		eaClearStruct(peaCategoryModifier, parse_ItemAssignmentModifierUI);
		ui_GenSetManagedListSafe(pGen, peaCategoryModifier, ItemAssignmentModifierUI, true);
		return false;
	}

	strdup_alloca(pchBuffer, pchPrefix);
	eaClearFast(&s_sortData.eaPrefixes);
	if (pchToken = strtok_r(pchBuffer, " \r\n\t,|", &pchContext))
	{
		do
		{
			eaPush(&s_sortData.eaPrefixes, allocAddString(pchToken));
		} while (pchToken = strtok_r(NULL, " \r\n\t,|", &pchContext));
	}

	if (pSlot)
	{
		const char** ppchOutcomeModifiers = ItemAssignments_GetOutcomeModifiersForSlot(pSlot);

		for (i = 0; i < eaSize(&ppchOutcomeModifiers); ++i)
		{
			const char *pchModName = ppchOutcomeModifiers[i];
			ItemAssignmentOutcomeModifier* pOutcomeModifier = ItemAssignment_GetModifierByName(pDef, pchModName);
			ItemAssignmentOutcomeModifierTypeData* pModType = pOutcomeModifier ? ItemAssignment_GetModifierTypeData(pOutcomeModifier->eType) : NULL;

			if (pModType && eaFind(&pModType->ppchAffectedOutcomes, pchOutcome) >= 0)
			{
				if (uFlags & UIItemCategoryFlag_CategorizedInventoryHeaders)
				{
					ItemAssignmentModifierUI *pHeader = NULL;

					for (k = iCount; k < eaSize(peaCategoryModifier); ++k)
					{
						if ((*peaCategoryModifier)[k]->UICategory.eCategory == kItemCategory_None && (*peaCategoryModifier)[k]->pchModifierName == pOutcomeModifier->pchName)
						{
							pHeader = (*peaCategoryModifier)[k];
							(*peaCategoryModifier)[k] = (*peaCategoryModifier)[iCount];
							(*peaCategoryModifier)[iCount] = pHeader;
							iCount++;
							break;
						}
					}

					if (!pHeader)
					{
						pHeader = eaGetStruct(peaCategoryModifier, parse_ItemAssignmentModifierUI, iCount++);
						ZeroStruct(&pHeader->UICategory);
						pHeader->UICategory.eCategory = kItemCategory_None;
						pHeader->pchModifierName = pOutcomeModifier->pchName;
					}
					pHeader->fModifier = 0.0f;
					pHeader->pchModifierPrefix = pchOutcome;
					pHeader->iModifiedOutcomes = 1;
					pHeader->pchOutcome = pchOutcome;
					pHeader->pchDisplayName = TranslateDisplayMessage(pOutcome->msgDisplayName);
				}

				for (j = 0; j < eaiSize(&pOutcomeModifier->peItemCategories); ++j)
				{
					ItemCategory eCategory = pOutcomeModifier->peItemCategories[j];
					ItemAssignmentModifierUI *pModifier = NULL;
					const char *pchName = StaticDefineIntRevLookup(ItemCategoryEnum, eCategory);
					const char *pchCategoryPrefix = NULL;

					for (k = 0; k < eaSize(&s_sortData.eaPrefixes); k++)
					{
						if (strStartsWith(pchName, s_sortData.eaPrefixes[k]) && (!pchCategoryPrefix || strlen(pchCategoryPrefix) < strlen(s_sortData.eaPrefixes[k])))
							pchCategoryPrefix = s_sortData.eaPrefixes[k];
					}

					for (k = iCount; k < eaSize(peaCategoryModifier); ++k)
					{
						if ((*peaCategoryModifier)[k]->UICategory.eCategory == eCategory && (*peaCategoryModifier)[k]->pchModifierName == pOutcomeModifier->pchName)
						{
							pModifier = (*peaCategoryModifier)[k];
							(*peaCategoryModifier)[k] = (*peaCategoryModifier)[iCount];
							(*peaCategoryModifier)[iCount] = pModifier;
							iCount++;
							break;
						}
					}

					if (!pModifier)
					{
						pModifier = eaGetStruct(peaCategoryModifier, parse_ItemAssignmentModifierUI, iCount++);
						gclInvSetCategoryInfo(&pModifier->UICategory, eCategory, pchName, pchCategoryPrefix);
						pModifier->pchModifierName = pOutcomeModifier->pchName;
					}
					pModifier->fModifier = ItemAssignments_GetOutcomeWeightModifier(pEnt, NULL, pOutcomeModifier, pModType, pSlot, NULL, eCategory);
					pModifier->pchModifierPrefix = pchOutcome;
					pModifier->iModifiedOutcomes = 1;
					pModifier->pchOutcome = pchOutcome;
					pModifier->pchDisplayName = TranslateDisplayMessage(pOutcome->msgDisplayName);
				}
			}
		}
	}

	eaStableSort(*peaCategoryModifier, &s_sortData, gclInvCompareItemAssignmentModifierUI);
	eaSetSizeStruct(peaCategoryModifier, parse_ItemAssignmentModifierUI, iCount);
	ui_GenSetManagedListSafe(pGen, peaCategoryModifier, ItemAssignmentModifierUI, true);
	return pSlot != NULL;
}

static void gclItemAssignment_GenGetUIOutcomes(	SA_PARAM_NN_VALID UIGen* pGen,
												SA_PARAM_OP_VALID Entity* pEnt, 
												SA_PARAM_OP_VALID ItemAssignmentDef* pDef, 
												ItemAssignmentSlottedItem** eaSlottedItems)
{
	ItemAssignmentOutcomeUI*** peaData = ui_GenGetManagedListSafe(pGen, ItemAssignmentOutcomeUI);

	ItemAssignment_GetUIOutcomes(pEnt, pDef, eaSlottedItems, peaData);

	ui_GenSetManagedListSafe(pGen, peaData, ItemAssignmentOutcomeUI, true);
}



// model call to get all the ItemAssignmentOutcomeUI for the given assignment by ID, using the currently slotted items
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetOutcomesForActiveItemAssignment);
void GenExprGetOutcomesForActiveItemAssignment(SA_PARAM_NN_VALID UIGen* pGen, U32 uAssignmentID)
{
	Entity* pEnt = entActivePlayerPtr();
	ItemAssignmentPersistedData* pPlayerData = SAFE_MEMBER2(pEnt, pPlayer, pItemAssignmentPersistedData);
	ItemAssignmentSlottedItem** eaSlottedItems = NULL;
	ItemAssignment* pAssignment = NULL;
	ItemAssignmentDef* pDef = NULL;

	if (pPlayerData)
	{
		pAssignment = ItemAssignment_FindActiveAssignmentByID(pPlayerData, uAssignmentID);

		if (!pAssignment)
		{
			ItemAssignmentCompletedDetails* pCompleteAssignment = ItemAssignment_FindCompletedAssignmentByID(pPlayerData, uAssignmentID);
			if (pCompleteAssignment)
				pDef = GET_REF(pCompleteAssignment->hDef);
		}
		else
		{
			pDef = GET_REF(pAssignment->hDef);
		}
	}

	if (pAssignment)
	{
		eaSlottedItems = (ItemAssignmentSlottedItem**)pAssignment->eaSlottedItems;
	}

	gclItemAssignment_GenGetUIOutcomes(pGen, pEnt, pDef, eaSlottedItems);
}

// model call to get all the ItemAssignmentOutcomeUI for the given assignment def, using the currently slotted items
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetOutcomesForNewItemAssignment);
void GenExprGetOutcomesForNewItemAssignment(SA_PARAM_NN_VALID UIGen* pGen, const char* pchAssignmentDef)
{
	ItemAssignmentSlottedItem** eaSlottedItems = NULL;
	Entity* pEnt = entActivePlayerPtr();
	ItemAssignmentDef* pDef = ItemAssignment_DefFromName(pchAssignmentDef);

	eaSlottedItems = ItemAssignment_GetOrCreateTempItemAssignmentSlottedItemList();

	gclItemAssignment_GenGetUIOutcomes(pGen, pEnt, pDef, eaSlottedItems);
}

// returns the percent chance for the assignment's outcome given the slotted items
static ItemAssignmentOutcomeUI* gclItemAssignment_GetOutcome(const char* pchAssignmentDef, const char *pchOutcome)
{
	static struct 
	{
		//ItemAssignmentSlottedItem** eaSlottedItems;
		ItemAssignmentOutcomeUI** eaOutcomeUI;
		const char *pchAssignmentDef; // pooled string
		U32 iLastUpdateTime;
	} s_cachedData = {0};

	pchAssignmentDef = allocAddString(pchAssignmentDef);
	pchOutcome = allocAddString(pchOutcome);

	// see if we need to update this data
	// only allow update once per frame
	if (s_cachedData.iLastUpdateTime != gGCLState.totalElapsedTimeMs || 
		s_cachedData.pchAssignmentDef != pchAssignmentDef)
	{
		Entity* pEnt = entActivePlayerPtr();
		ItemAssignmentDef* pDef = ItemAssignment_DefFromName(pchAssignmentDef);
		ItemAssignmentSlottedItem** eaSlottedItems;

		eaSlottedItems = ItemAssignment_GetOrCreateTempItemAssignmentSlottedItemList();

		ItemAssignment_GetUIOutcomes(pEnt, pDef, eaSlottedItems, &s_cachedData.eaOutcomeUI);

		s_cachedData.iLastUpdateTime = gGCLState.totalElapsedTimeMs;
		s_cachedData.pchAssignmentDef = pchAssignmentDef;
	}

	FOR_EACH_IN_EARRAY(s_cachedData.eaOutcomeUI, ItemAssignmentOutcomeUI, pOutcomeUI)
	{
		if (pOutcomeUI->pchName == pchOutcome)
		{
			return pOutcomeUI;
		}
	}
	FOR_EACH_END

	return NULL;
}

// --------------------------------------------------------------------------------------------------------------------

// returns the modified percent chance for the assignment's outcome given the slotted items
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemAssignmentGetPercentageChanceForOutcome);
F32 gclItemAssignment_GetPercentageChanceForOutcome(const char* pchAssignmentDef, const char *pchOutcome)
{
	ItemAssignmentOutcomeUI *pOutcome = gclItemAssignment_GetOutcome(pchAssignmentDef, pchOutcome);
	if (pOutcome)
	{
		return pOutcome->fOutcomePercentChance;
	}

	return 0.f;
}

// returns the base percent chance for the assignment's outcome given the slotted items
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemAssignmentGetBasePercentageChanceForOutcome);
F32 gclItemAssignment_GetBasePercentageChanceForOutcome(const char* pchAssignmentDef, const char *pchOutcome)
{
	ItemAssignmentOutcomeUI *pOutcome = gclItemAssignment_GetOutcome(pchAssignmentDef, pchOutcome);
	if (pOutcome)
	{
		return pOutcome->fBasePercentChance;
	}

	return 0.f;
}


// Returns the modified duration for the assignment given the slotted items
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemAssignmentGetModifiedDurationForAssignment);
U32 gclItemAssignment_GetModifiedDurationForAssignment(const char *pchAssignmentDef)
{
	Entity* pEnt = entActivePlayerPtr();
	ItemAssignmentDef* pDef = ItemAssignment_DefFromName(pchAssignmentDef);

	if (pEnt && pDef)
	{
		ItemAssignmentSlottedItem** eaSlottedItems;
		eaSlottedItems = ItemAssignment_GetOrCreateTempItemAssignmentSlottedItemList();
		return ItemAssignments_CalculateDuration(pEnt, pDef, eaSlottedItems);
	}

	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetSlottedItemsForActiveItemAssignment);
void GenExprGetSlottedItemsForActiveItemAssignment(SA_PARAM_NN_VALID UIGen* pGen, U32 uAssignmentID)
{
	ItemAssignmentSlotUI*** peaData = ui_GenGetManagedListSafe(pGen, ItemAssignmentSlotUI);
	Entity* pEnt = entActivePlayerPtr();
	ItemAssignmentPersistedData* pPlayerData = SAFE_MEMBER2(pEnt, pPlayer, pItemAssignmentPersistedData);
	ItemAssignment* pData = NULL;
	ItemAssignmentCompletedDetails* pCompleteDetails = NULL;
	ItemAssignmentSlotUI* pSlotUI;
	S32 i;

	if (pPlayerData)
	{
		for (i = eaSize(&pPlayerData->eaActiveAssignments) - 1; i >= 0; --i)
		{
			if (pPlayerData->eaActiveAssignments[i]->uAssignmentID == uAssignmentID)
			{
				pData = pPlayerData->eaActiveAssignments[i];
				break;
			}
		}
	}

	// Mark slots stale
	for (i = eaSize(peaData) - 1; i >= 0; i--)
	{
		(*peaData)[i]->bUpdated = false;
	}

	if (pData)
	{
		GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		ItemAssignmentDef* pDef = pData ? GET_REF(pData->hDef) : NULL;

		if (pData->pchRewardOutcome)
		{
			pCompleteDetails = ItemAssignment_EntityGetRecentlyCompletedAssignmentByID(pEnt, uAssignmentID);
			if (!pCompleteDetails) // TODO: This is safe to remove once F2P goes live
				pCompleteDetails = ItemAssignment_FindPossibleCompletedDetails(pPlayerData, pData);
		}

		for (i = 0; i < eaSize(&pData->eaSlottedItems); i++)
		{
			ItemAssignmentSlottedItem* pSlottedItem = pData->eaSlottedItems[i];
			// This is in no way guaranteed to be the correct slot
			ItemAssignmentSlot* pPossibleSlot = pDef ? eaGet(&pDef->eaSlots, MIN(i, eaSize(&pDef->eaSlots) - 1)) : NULL;
			Item* pItem = ItemAssignments_GetItemFromSlottedItem(pEnt, pSlottedItem, pExtract);

			pSlotUI = eaGetStruct(peaData, parse_ItemAssignmentSlotUI, i);
			COPY_HANDLE(pSlotUI->hDef, pData->hDef);
			pSlotUI->uItemID = pSlottedItem->uItemID;
			pSlotUI->eBagID = pSlottedItem->eBagID;
			pSlotUI->iBagSlot = pSlottedItem->iBagSlot;
			pSlotUI->iAssignmentSlot = pSlottedItem->iAssignmentSlot;
			pSlotUI->bUnslottable = SAFE_MEMBER(pDef, bAllowItemUnslotting);
			pSlotUI->bDestroyed = false;
			pSlotUI->bNewAssignment = false;
			pSlotUI->uNewAssignmentID = 0;
			pSlotUI->bUpdated = true;
			pSlotUI->pchIcon = SAFE_MEMBER(pPossibleSlot, pchIcon);
			pSlotUI->pchDestroyDescription = NULL;
			pSlotUI->pchNewAssignmentDescription = NULL;

			if (pItem)
			{
				COPY_HANDLE(pSlotUI->hItemDef, pItem->hItem);
			}
			else
			{
				REMOVE_HANDLE(pSlotUI->hItemDef);
			}
		}

		if (pCompleteDetails)
		{
			ItemAssignment_FillInFromCompleteDetails(pEnt, pPlayerData, peaData, pCompleteDetails);
		}
	}

	// Remove stale slots
	for (i = eaSize(peaData) - 1; i >= 0; i--)
	{
		if (!(*peaData)[i]->bUpdated)
		{
			StructDestroy(parse_ItemAssignmentSlotUI, eaRemove(peaData, i));
		}
	}

	ui_GenSetManagedListSafe(pGen, peaData, ItemAssignmentSlotUI, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetSlottedItemsForCompletedItemAssignment);
void GenExprGetSlottedItemsForCompletedItemAssignment(SA_PARAM_NN_VALID UIGen* pGen, S32 iIndex)
{
	ItemAssignmentSlotUI*** peaData = ui_GenGetManagedListSafe(pGen, ItemAssignmentSlotUI);
	Entity* pEnt = entActivePlayerPtr();
	ItemAssignmentPersistedData* pPlayerData = SAFE_MEMBER2(pEnt, pPlayer, pItemAssignmentPersistedData);
	ItemAssignmentCompletedDetails* pCompleteDetails = pPlayerData ? eaGet(&pPlayerData->eaRecentlyCompletedAssignments, iIndex) : NULL;
	S32 i;

	// Mark slots stale
	for (i = eaSize(peaData) - 1; i >= 0; i--)
	{
		(*peaData)[i]->bUpdated = false;
	}

	if (pCompleteDetails)
	{
		ItemAssignment_FillInFromCompleteDetails(pEnt, pPlayerData, peaData, pCompleteDetails);
	}

	// Remove stale slots
	for (i = eaSize(peaData) - 1; i >= 0; i--)
	{
		if (!(*peaData)[i]->bUpdated)
		{
			StructDestroy(parse_ItemAssignmentSlotUI, eaRemove(peaData, i));
		}
	}

	ui_GenSetManagedListSafe(pGen, peaData, ItemAssignmentSlotUI, true);
}

// gets a list of all the item assignments using the g_ItemAssignmentSettings.pStrictAssignmentSlots
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetItemAssignmentSlots);
void GenExprGetItemAssignmentSlots(SA_PARAM_NN_VALID UIGen* pGen, U32 uFlags)
{
	ItemAssignmentUI*** peaItemAssignmentUIData = ui_GenGetManagedListSafe(pGen, ItemAssignmentUI);
	Entity* pEnt = entActivePlayerPtr();

	ItemAssignment_FillItemAssignmentSlots(pEnt,peaItemAssignmentUIData);

	ui_GenSetManagedListSafe(pGen, peaItemAssignmentUIData, ItemAssignmentUI, true);
}

// Returns the first available ItemAssignment. Returns -1 if no slots available
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenItemAssignmentsGetFirstUnusedItemAssignmentSlot);
S32 gclItemAssignment_GetFirstUnusedItemAssignmentSlot()
{
	Entity* pEnt = entActivePlayerPtr();
	ItemAssignmentPersistedData* pPlayerData = SAFE_MEMBER2(pEnt, pPlayer, pItemAssignmentPersistedData);

	if (pPlayerData && g_ItemAssignmentSettings.pStrictAssignmentSlots)
	{
		S32 iNumAvailableAssignmentSlots = ItemAssignments_GetNumberUnlockedAssignmentSlots(pEnt, g_ItemAssignmentSettings.pStrictAssignmentSlots);
		U32 uUsedSlots = 0;
		if (iNumAvailableAssignmentSlots <= eaSize(&pPlayerData->eaActiveAssignments))
			return -1;

		FOR_EACH_IN_EARRAY(pPlayerData->eaActiveAssignments, ItemAssignment, pAssignment)
		{
			uUsedSlots |= (1 << pAssignment->uItemAssignmentSlot);
		}
		FOR_EACH_END

		{
			S32 i;
			for (i = 0; i < iNumAvailableAssignmentSlots; ++i)
			{
				if (!(uUsedSlots & 1))
				{
					return i;
				}
				uUsedSlots = (uUsedSlots >> 1);
			}
		}
	}

	return -1;
}

// bAddHeaders will add 'ReadyToTurnIn' and 'InProgress' headers
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetActiveItemAssignments);
void GenExprGetActiveItemAssignments(SA_PARAM_NN_VALID UIGen* pGen, U32 uFlags)
{
	ItemAssignmentUI*** peaData = ui_GenGetManagedListSafe(pGen, ItemAssignmentUI);
	Entity* pEnt = entActivePlayerPtr();
	ItemAssignmentPersistedData* pPlayerData = SAFE_MEMBER2(pEnt, pPlayer, pItemAssignmentPersistedData);
	S32 i, j, iCount = 0;
	U32 uTimeCurrent = timeServerSecondsSince2000();
	bool bHasInProgress = false;
	bool bHasReadyToTurnIn = false;
	bool bHasCompleted = false;
	U32 uHeaders = uFlags & AVAILABLE_ITEM_ASSIGNMENT_HEADER_MASK;
	bool bStatusHeaders = (uHeaders == kGetActiveAssignmentFlags_AddHeaders || uHeaders == kGetActiveAssignmentFlags_AddStatusHeaders);
	bool bCategoryHeaders = (uHeaders == kGetActiveAssignmentFlags_AddCategoryHeaders || uHeaders == kGetActiveAssignmentFlags_AddAllCategoryHeaders);
	U32 uValidCategories[256 / 32] = {0};
	ItemAssignmentUI* pData;

	if (pPlayerData)
	{
		for (i = 0; i < eaSize(&pPlayerData->eaActiveAssignments); i++)
		{
			ItemAssignment* pAssignment = pPlayerData->eaActiveAssignments[i];

			if (!pAssignment->pchRewardOutcome && (uFlags & kGetActiveAssignmentFlags_ExcludeIncomplete))
				continue;
			if (pAssignment->pchRewardOutcome && (uFlags & kGetActiveAssignmentFlags_ExcludeReady))
				continue;

			if (!(uFlags & kGetActiveAssignmentFlags_DontFill))
			{
				pData = ItemAssignment_GetAssignment(peaData, iCount++, pAssignment->uAssignmentID, -1, NULL, true);
				FillActiveItemAssignment(pData, pAssignment, pEnt, pPlayerData, uTimeCurrent);
			}
			else
			{
				pData = NULL;
			}

			if (bStatusHeaders)
			{
				if (!pAssignment->pchRewardOutcome)
					bHasInProgress = true;
				if (pAssignment->pchRewardOutcome)
					bHasReadyToTurnIn = true;
				if (pData)
					pData->uSortCategory = !!pAssignment->pchRewardOutcome;
			}
			else if (bCategoryHeaders)
			{
				ItemAssignmentDef *pDef = GET_REF(pAssignment->hDef);
				if (pData)
					pData->uSortCategory = (U8)SAFE_MEMBER(pDef, eCategory);
				SETB(uValidCategories, (U8)SAFE_MEMBER(pDef, eCategory));
			}
		}

		if ((uFlags & kGetActiveAssignmentFlags_IncludeCompleted))
		{
			for (i = 0; i < eaSize(&pPlayerData->eaRecentlyCompletedAssignments); i++)
			{
				ItemAssignmentCompletedDetails *pAssignment = pPlayerData->eaRecentlyCompletedAssignments[i];
				ItemAssignment *pActiveAssignment;

				// Do not duplicate assignments
				for (j = iCount - 1; j >= 0; j--)
				{
					if ((*peaData)[j]->iCompletedAssignmentIndex == i)
						break;
				}
				if (j >= 0)
					continue;

				pActiveAssignment = ItemAssignment_FindPossibleActiveItemAssignment(pPlayerData, pAssignment);
				if (pActiveAssignment && (uFlags & kGetActiveAssignmentFlags_ExcludeReady))
					continue;
				if (!pAssignment->bMarkedAsRead && (uFlags & kGetActiveAssignmentFlags_ExcludeUnread))
					continue;
				if (pAssignment->bMarkedAsRead && (uFlags & kGetActiveAssignmentFlags_IncludeUnread))
					continue;

				if (!(uFlags & kGetActiveAssignmentFlags_DontFill))
				{
					pData = ItemAssignment_GetAssignment(peaData, iCount++, 0, i, NULL, true);
					FillCompletedItemAssignment(pEnt, pData, pAssignment, pActiveAssignment, uTimeCurrent);
					pData->iCompletedAssignmentIndex = i;
					pData->bHasRewards = false;
				}
				else
				{
					pData = NULL;
				}

				if (bStatusHeaders)
				{
					bHasCompleted = true;
					if (pData)
						pData->uSortCategory = 2;
				}
				else if (bCategoryHeaders)
				{
					ItemAssignmentDef *pDef = GET_REF(pAssignment->hDef);
					if (pData)
						pData->uSortCategory = (U8)SAFE_MEMBER(pDef, eCategory);
					SETB(uValidCategories, (U8)SAFE_MEMBER(pDef, eCategory));
				}
			}
		}
	}

	if (bStatusHeaders)
	{
		if (bHasInProgress)
		{
			pData = ItemAssignment_CreateActiveHeader(peaData, &iCount, "ItemAssignment_InProgressHeader");
			pData->bHasRewards = false;
			pData->uSortCategory = 0;
		}
		if (bHasReadyToTurnIn)
		{
			pData = ItemAssignment_CreateActiveHeader(peaData, &iCount, "ItemAssignment_ReadyToTurnInHeader");
			pData->bHasRewards = true;
			pData->uSortCategory = 1;
		}
		if (bHasCompleted)
		{
			pData = ItemAssignment_CreateActiveHeader(peaData, &iCount, "ItemAssignment_CompletedHeader");
			pData->bHasRewards = false;
			pData->uSortCategory = 2;
		}
	}
	else if (bCategoryHeaders)
	{
		for (i = 0; i < 255; ++i)
		{
			if (uHeaders == kGetActiveAssignmentFlags_AddAllCategoryHeaders || TSTB(uValidCategories, i))
			{
				ItemAssignmentCategorySettings* pCategorySettings = ItemAssignmentCategory_GetSettings(i);
				if (pCategorySettings)
				{
					pData = ItemAssignment_CreateActiveHeader(peaData, &iCount, REF_STRING_FROM_HANDLE(pCategorySettings->msgDisplayName.hMessage));
					pData->pchIcon = pCategorySettings->pchIconName;
					pData->pchImage = pCategorySettings->pchImage;
					pData->eCategory = i;
					pData->pchCategoryNumericRank1 = pCategorySettings->pchNumericRank1;
					pData->pchCategoryNumericRank2 = pCategorySettings->pchNumericRank2;
					pData->pchCategoryNumericXP1 = pCategorySettings->pchNumericXP1;
					pData->pchCategoryNumericXP2 = pCategorySettings->pchNumericXP2;
					pData->pchCategoryIcon = pCategorySettings->pchIconName;
					pData->uSortCategory = i;
				}
			}
		}
	}

	eaSetSizeStruct(peaData, parse_ItemAssignmentUI, iCount);
	eaQSort_s(*peaData, SortActiveItemAssignments, &uFlags);

	ui_GenSetManagedListSafe(pGen, peaData, ItemAssignmentUI, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetCompletedItemAssignments);
void GenExprGetCompletedItemAssignments(SA_PARAM_NN_VALID UIGen* pGen)
{
	GenExprGetActiveItemAssignments(pGen, kGetActiveAssignmentFlags_ExcludeIncomplete | kGetActiveAssignmentFlags_IncludeCompleted);
}

// the pchItemCategoryFilters list can be of string enumeration, or ascii integers
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetAvailableItemAssignmentsByItemCategory);
void GenExprGetAvailableItemAssignmentsByCategory(	SA_PARAM_NN_VALID UIGen* pGen, 
													const char *pchBagIDs, 
													const char *pchItemCategoryFilters, 
													U32 uFlags)
{
	Entity* pEnt = entActivePlayerPtr();
	ItemAssignmentUI*** peaData = ui_GenGetManagedListSafe(pGen, ItemAssignmentUI);
	GetAvailableItemAssignmentsByCategoryInternal(pEnt, peaData, pchBagIDs, pchItemCategoryFilters, NULL, uFlags, -1, -1, 0, NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemAssignmentsGetAvailableWithFilters);
void GenExprGetAvailableItemAssignmentsWithFilters(SA_PARAM_NN_VALID UIGen* pGen, 
													const char *pchBagIDs, 
													const char *pchItemCategoryFilters, 
													const char *pchWeightFilters, 
													U32 uFlags,
													S32 iMinRequiredNumericValue,
													S32 imaxRequiredNumericValue,
													S32 iUnmetRequiredNumericRange,
													const char *pchStringSearch)
{
	ItemAssignmentUI*** peaData = ui_GenGetManagedListSafe(pGen, ItemAssignmentUI);
	Entity* pEnt = entActivePlayerPtr();

	GetAvailableItemAssignmentsByCategoryInternal(pEnt, peaData, pchBagIDs, pchItemCategoryFilters, pchWeightFilters, 
													uFlags, iMinRequiredNumericValue, imaxRequiredNumericValue, 
													iUnmetRequiredNumericRange, pchStringSearch);

	ui_GenSetManagedListSafe(pGen, peaData, ItemAssignmentUI, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetAvailableItemAssignmentsEx);
void GenExprGetAvailableItemAssignmentsEx(SA_PARAM_NN_VALID UIGen* pGen, const char *pchBagIDs, U32 uFlags)
{
	GenExprGetAvailableItemAssignmentsByCategory(pGen, pchBagIDs, NULL, uFlags);
}



AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetAvailableItemAssignments);
void GenExprGetAvailableItemAssignments(SA_PARAM_NN_VALID UIGen* pGen, S32 eBagID, U32 uFlags)
{
	GenExprGetAvailableItemAssignmentsEx(pGen, StaticDefineIntRevLookup(InvBagIDsEnum, eBagID), uFlags);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMD;
void gclItemAssignments_ReceiveRemoteAssignments(ItemAssignmentDefRefs* pRefs)
{
	eaClearStruct(&s_eaRemoteAssignmentRefs, parse_ItemAssignmentDefRef);
	if (pRefs)
	{
		eaCopyStructs(&pRefs->eaRefs, &s_eaRemoteAssignmentRefs, parse_ItemAssignmentDefRef);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetRemoteItemAssignments);
void GenExprGetRemoteItemAssignments(SA_PARAM_NN_VALID UIGen* pGen, S32 eCategory)
{
	ItemAssignmentUI*** peaData = ui_GenGetManagedListSafe(pGen, ItemAssignmentUI);
	int i, iCount = 0;
	U32 uCurrentTime = timeSecondsSince2000();

	if ((!eaSize(&s_eaRemoteAssignmentRefs) || isDevelopmentMode()) && uCurrentTime > s_uNextRemoteRequestTime)
	{
		ServerCmd_RequestRemoteAssignments();
		s_uNextRemoteRequestTime = uCurrentTime + ITEM_ASSIGNMENTS_REMOTE_REQUEST_TIMEOUT;
	}
	for (i = 0; i < eaSize(&s_eaRemoteAssignmentRefs); i++)
	{
		ItemAssignmentDef* pDef = GET_REF(s_eaRemoteAssignmentRefs[i]->hDef);
		if (pDef && (eCategory < 0 || pDef->eCategory == eCategory))
		{
			ItemAssignmentUI* pData = eaGetStruct(peaData, parse_ItemAssignmentUI, iCount++);
			ItemAssignmentCategorySettings* pCategorySettings = ItemAssignmentCategory_GetSettings(pDef->eCategory);
			COPY_HANDLE(pData->hDef, s_eaRemoteAssignmentRefs[i]->hDef);
			pData->pchDisplayName = TranslateDisplayMessage(pDef->msgDisplayName);
			pData->pchDescription = TranslateDisplayMessage(pDef->msgDescription);
			pData->pchIcon = ItemAssignment_GetIconName(pDef, pCategorySettings);
			pData->pchImage = ItemAssignment_GetImageName(pDef, pCategorySettings);
			pData->pchFeaturedActivity = SAFE_MEMBER(pDef, pchFeaturedActivity);
			pData->pchWeight = StaticDefineIntRevLookup(ItemAssignmentWeightTypeEnum, pDef->eWeight);
			pData->eCategory = pDef->eCategory;
			pData->bIsAbortable = pDef->bIsAbortable;
		}
	}

	eaSetSizeStruct(peaData, parse_ItemAssignmentUI, iCount);
	eaQSort(*peaData, SortAvailableItemAssignments);
	ui_GenSetList(pGen, peaData, parse_ItemAssignmentUI);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenClearRemoteItemAssignments);
void GenExprClearRemoteItemAssignments(void)
{
	eaDestroyStruct(&s_eaRemoteAssignmentRefs, parse_ItemAssignmentDefRef);
}

static U32 s_uNextUpdateTime = 0;

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(UpdateAvailableItemAssignments);
void GenExprUpdateAvailableItemAssignments(void)
{
	Entity* pEnt = entActivePlayerPtr();
	U32 uCurrentTime = timeSecondsSince2000();

	if (pEnt && pEnt->pPlayer && uCurrentTime >= s_uNextUpdateTime)
	{
		ServerCmd_gslRequestItemAssignments();
		s_uNextUpdateTime = uCurrentTime + ITEM_ASSIGNMENTS_REFRESH_TIME_UI;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetUpdateAvailableItemAssignmentsCooldown);
S32 GenExprGetUpdateAvailableItemAssignmentsCooldown(void)
{
	U32 uCurrentTime = timeSecondsSince2000();
	return uCurrentTime >= s_uNextUpdateTime ? 0 : s_uNextUpdateTime - uCurrentTime;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemAssignmentsCanSlotItem);
bool GenExprItemAssignmentsCanSlotItem(S32 iAssignmentSlot, S32 eBagID, S32 iBagSlot)
{
	bool bCanSlot = false;
	Entity* pEnt = entActivePlayerPtr();
	if (pEnt)
	{
		GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		bCanSlot = ItemAssignmentsCanSlotItem(pEnt, iAssignmentSlot, eBagID, iBagSlot, pExtract);
	}
	return bCanSlot;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemAssignmentsClearSlottedItem);
void GenExprItemAssignmentsClearSlottedItem(int iSlot)
{
	ItemAssignmentSlotUI* pSlotUI = eaGet(&pIACache->eaSlots, iSlot);
	if (pSlotUI)
		ItemAssignmentsClearSlottedItem(pSlotUI);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemAssignmentsClearSlottedItems);
void GenExprItemAssignmentsClearSlottedItems(void)
{
	S32 i;
	for (i = eaSize(&pIACache->eaSlots)-1; i >= 0; i--)
	{
		ItemAssignmentsClearSlottedItem(pIACache->eaSlots[i]);
	}

	eaSetSizeStruct(&pIACache->eaSlots, parse_ItemAssignmentSlotUI, 0);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemAssignmentsSlotItem);
bool GenExprItemAssignmentsSlotItem(S32 iAssignmentSlot, S32 eBagID, S32 iBagSlot)
{
	bool bSuccess = false;
	Entity* pEnt = entActivePlayerPtr();
	ItemAssignmentSlotUI* pSlotUI = eaGet(&pIACache->eaSlots, iAssignmentSlot);
	if (pEnt && pSlotUI)
	{
		GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		if (ItemAssignmentsCanSlotItem(pEnt, iAssignmentSlot, eBagID, iBagSlot, pExtract))
		{
			Item* pItem = inv_GetItemFromBag(pEnt, eBagID, iBagSlot, pExtract);
			if (pItem)
			{
				ItemAssignmentsSlotItemCheckSwap(pEnt, pSlotUI, eBagID, iBagSlot, pItem);
				bSuccess = true;
			}
		}
	}
	return bSuccess;
}

// wrapper for ItemAssignmentsSlotItem that will make sure we have an assignment slot for the index
// and then set the ItemAssignmentDef on the slot
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemAssignmentsSlotItemWithItemAssignmentDef);
bool GenExprItemAssignmentsSlotItemWithItemAssignmentDef(S32 iAssignmentSlot, 
	S32 eBagID, 
	S32 iBagSlot, 
	const char* pchAssignmentDef)
{
	ItemAssignmentSlotUI* pSlotUI = NULL;
	ItemAssignmentDef* pDef = ItemAssignment_DefFromName(pchAssignmentDef);

	if(eaSize(&pIACache->eaSlots) <= iAssignmentSlot)
	{
		eaSetSizeStruct(&pIACache->eaSlots, parse_ItemAssignmentSlotUI, iAssignmentSlot+1);
	}

	pSlotUI = eaGet(&pIACache->eaSlots, iAssignmentSlot);
	if (pDef && pSlotUI)
	{
		SET_HANDLE_FROM_REFERENT(g_hItemAssignmentDict, pDef, pSlotUI->hDef);
	}

	return GenExprItemAssignmentsSlotItem(iAssignmentSlot, eBagID, iBagSlot);
}

// This checks to see if the item can go in any slot (whether or not the slot is already filled)
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemAssignmentsIsItemSlottable");
bool GenExprItemAssignmentsIsItemSlottable(S32 eBagID, S32 iBagSlot)
{
	bool bSuccess = false;
	Entity* pEnt = entActivePlayerPtr();
	GameAccountDataExtract* pExtract = NULL;
	S32 i;
	for (i = eaSize(&pIACache->eaSlots) - 1; i >= 0; i--)
	{
		ItemAssignmentSlotUI* pSlotUI = pIACache->eaSlots[i];

		if (!pExtract)
			pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

		if (ItemAssignmentsCanSlotItem(pEnt, i, eBagID, iBagSlot, pExtract))
		{
			bSuccess = true;
			break;
		}
	}
	return bSuccess;
}



// This checks to see if there is an empty slot that supports the item
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemAssignmentsCanAutoSlotItem");
bool GenExprItemAssignmentsCanAutoSlotItem(S32 eBagID, S32 iBagSlot)
{
	bool bSuccess = false;
	Entity* pEnt = entActivePlayerPtr();
	GameAccountDataExtract* pExtract = NULL;
	S32 i;
	for (i = eaSize(&pIACache->eaSlots) - 1; i >= 0; i--)
	{
		ItemAssignmentSlotUI* pSlotUI = pIACache->eaSlots[i];

		// If there's something already in the slot...
		if (pSlotUI->uItemID)
			continue;

		if (!pExtract)
			pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

		if (ItemAssignmentsCanSlotItem(pEnt, i, eBagID, iBagSlot, pExtract))
		{
			bSuccess = true;
			break;
		}
	}
	return bSuccess;
}

// This slots the item in the first open slot
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemAssignmentsAutoSlotItem");
bool GenExprItemAssignmentsAutoSlotItem(S32 eBagID, S32 iBagSlot)
{
	bool bSuccess = false;
	Entity* pEnt = entActivePlayerPtr();
	GameAccountDataExtract* pExtract = NULL;
	S32 i;
	for (i = 0; i < eaSize(&pIACache->eaSlots); i++)
	{
		ItemAssignmentSlotUI* pSlotUI = pIACache->eaSlots[i];

		// If there's something already in the slot...
		if (pSlotUI->uItemID)
			continue;

		if (!pExtract)
			pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

		if (ItemAssignmentsCanSlotItem(pEnt, i, eBagID, iBagSlot, pExtract))
		{
			Item* pItem = inv_GetItemFromBag(pEnt, eBagID, iBagSlot, pExtract);
			if (pItem)
			{
				ItemAssignmentsSlotItemCheckSwap(pEnt, pSlotUI, eBagID, iBagSlot, pItem);
				bSuccess = true;
			}
			break;
		}
	}
	return bSuccess;
}

// Returns the slot that the item is currently in, or -1 if not slotted
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemGetAssignmentSlot");
S32 GenExprItemGetNewAssignmentSlot(S32 eBagID, S32 iBagSlot)
{
	Entity* pEnt = entActivePlayerPtr();
	S32 iSlot = -1;
	GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	Item* pItem = inv_GetItemFromBag(pEnt, eBagID, iBagSlot, pExtract);
	if (pItem)
	{
		for (iSlot = eaSize(&pIACache->eaSlots) - 1; iSlot >= 0; iSlot--)
		{
			if (pIACache->eaSlots[iSlot]->uItemID == pItem->id)
			{
				break;
			}
		}
	}
	return iSlot;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemAssignmentsGetSlottedItemCount);
S32 GenExprItemAssignmentsGetSlottedItemCount(void)
{
	S32 i, iCount = 0;
	for (i = eaSize(&pIACache->eaSlots)-1; i >= 0; i--)
	{
		if (pIACache->eaSlots[i]->uItemID)
		{
			iCount++;
		}
	}
	return iCount;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetSlotsForItemAssignmentWithOptions);
bool GenExprGenGetSlotsForItemAssignmentWithOptions(SA_PARAM_NN_VALID UIGen* pGen, const char* pchAssignmentDef, U32 uOptionFlags)
{
	bool bFull = true;
	S32 iCount = 0;
	Entity *pEnt = entActivePlayerPtr();
	ItemAssignmentSlotUI*** peaGenData = ui_GenGetManagedListSafe(pGen, ItemAssignmentSlotUI);

	bFull = SetGenSlotsForItemAssignment(pEnt, &pIACache->eaSlots, &iCount, false, peaGenData, pchAssignmentDef, uOptionFlags);

	eaSetSizeStruct(&pIACache->eaSlots, parse_ItemAssignmentSlotUI, iCount);

	ui_GenSetManagedListSafe(pGen, peaGenData, ItemAssignmentSlotUI, false);

	return bFull;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetSlotsForItemAssignment);
bool GenExprGenGetSlotsForItemAssignment(SA_PARAM_NN_VALID UIGen* pGen, const char* pchAssignmentDef)
{
	return GenExprGenGetSlotsForItemAssignmentWithOptions(pGen, pchAssignmentDef, 0);
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemAssignmentGetSlotsForItemAssignmentDef);
bool GenExprItemAssignmentGetSlotsForItemAssignmentDef(SA_PARAM_NN_VALID UIGen* pGen, const char* pchAssignmentDef, U32 uOptionFlags)
{
	static S32 s_iCacheSlotCount = 0;
	static U32 s_uiLastFrameCount = 0;
	static ItemAssignmentSlotUI **s_eaItemAssignmentSlotUIs = NULL;
	Entity *pEnt = entActivePlayerPtr();
	bool bReturn = false;
	ItemAssignmentSlotUI*** peaGenData = ui_GenGetManagedListSafe(pGen, ItemAssignmentSlotUI);

	// note: s_eaItemAssignmentSlotUIs could get fairly large depending on the UI and setup- 
	// we'll need a reliable way to cleanup this list.
	if (g_ui_State.uiFrameCount != s_uiLastFrameCount)
	{
		s_uiLastFrameCount = g_ui_State.uiFrameCount;
		if (s_iCacheSlotCount > 150)
		{
			Alertf("ItemAssignmentUI: ItemAssignmentSlotUI cache has gotten very large %d.", s_iCacheSlotCount);
		}
		s_iCacheSlotCount = 0;
	}

	bReturn = SetGenSlotsForItemAssignment(pEnt, &s_eaItemAssignmentSlotUIs, &s_iCacheSlotCount, true, peaGenData, pchAssignmentDef, uOptionFlags);

	ui_GenSetManagedListSafe(pGen, peaGenData, ItemAssignmentSlotUI, false);

	return bReturn;
}



AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemAssignmentHasOptionalSlots);
bool GenExprItemAssignmentHasOptionalSlots(const char* pchAssignmentDef)
{
	ItemAssignmentDef* pDef = ItemAssignment_DefFromName(pchAssignmentDef);
	if (pDef)
	{
		return pDef->bHasOptionalSlots;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemAssignmentHasOutcome);
bool GenExprItemAssignmentHasOutcome(const char* pchOutcome)
{
	if (pIACache->pRewardRequestData)
	{
		if (ItemAssignmentsUI_FindRewardRequestOutcome(allocFindString(pchOutcome)))
			return true;
	}

	return false;
}

// Model call to get the outcome rewards for the given outcome, with flags for filtering
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemAssignmentGetFilteredRewards);
void GenExprItemAssignment_GetFilteredRewards(SA_PARAM_NN_VALID UIGen* pGen, 
								const char *pchAssignmentDef, 
								const char* pchOutcome, 
								U32 uFlags)
{
	InventorySlot*** peaData = ui_GenGetManagedListSafe(pGen, InventorySlot);
	Entity* pEnt = entActivePlayerPtr();
	ItemAssignmentDef* pAssignmentDef = NULL;

	eaClearStruct(peaData, parse_InventorySlot);

	if (pchAssignmentDef)
	{
		pAssignmentDef = ItemAssignment_DefFromName(pchAssignmentDef);
		if (!pAssignmentDef)
		{
			ui_GenSetManagedListSafe(pGen, peaData, InventorySlot, true);
			return;
		}
	}

	ItemAssignment_GetFilteredRewards(pEnt,peaData,pAssignmentDef,pchOutcome,uFlags,false);
	
	ui_GenSetManagedListSafe(pGen, peaData, InventorySlot, true);
}

// returns true if there any rewards for the given outcome. 
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemAssignmentCheckHasFilteredRewards);
bool GenExprItemAssignmentCheckForFilteredRewards(	const char *pchAssignmentDef, 
													const char* pchOutcome, 
													U32 uFlags)
{
	Entity* pEnt = entActivePlayerPtr();
	ItemAssignmentDef* pAssignmentDef = NULL;

	if (pchAssignmentDef)
	{
		pAssignmentDef = ItemAssignment_DefFromName(pchAssignmentDef);
		if (!pAssignmentDef)
		{
			return false;
		}
	}

	return ItemAssignment_GetFilteredRewards(pEnt, NULL, pAssignmentDef, pchOutcome, uFlags, true);
}

// Model call to get the outcome rewards for the given outcome
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetRewardsForItemAssignmentOutcome);
void GenExprGetRewardsForItemAssignmentOutcome(SA_PARAM_NN_VALID UIGen* pGen, const char* pchOutcome)
{
	GenExprItemAssignment_GetFilteredRewards(pGen, NULL, pchOutcome, 0);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemAssignmentMessageFormatGetXPGainForOutcome);
const char *GenExprItemAssignment_MessageFormatGetXPGainForOutcome(ExprContext *pContext, ACMD_EXPR_DICT(Message) const char *pchMessageKey, const char* pchOutcome)
{
	Entity* pEnt = entActivePlayerPtr();

	if (pEnt && pIACache->pRewardRequestData && g_ItemAssignmentSettings.pchXPFilterBaseName)
	{
		const char* pchOutcomePooled = allocFindString(pchOutcome);
		const char *pchCategoryXp = NULL;
		const char *pchXp = NULL;
		S32 iCategoryXPValue = 0;
		S32 iXPValue = 0;

		FOR_EACH_IN_EARRAY(pIACache->pRewardRequestData->eaOutcomes, ItemAssignmentOutcomeRewardRequest, pOutcomeRequest)
		{
			if (pchOutcomePooled == pOutcomeRequest->pchOutcome)
			{
				FOR_EACH_IN_EARRAY(pOutcomeRequest->pData->eaNumericRewards, ItemNumericData, pNumericData)
				{
					// check if we are filtering out the XP numerics, but we need the substring 
					// on g_ItemAssignmentSettings.pchXPFilterBaseName
					ItemDef *pDef = GET_REF(pNumericData->hDef);
					if (pDef && strstri(pDef->pchName, g_ItemAssignmentSettings.pchXPFilterBaseName))
					{
						if (!stricmp(pDef->pchName, g_ItemAssignmentSettings.pchXPFilterBaseName))
						{
							pchXp = TranslateDisplayMessage(pDef->displayNameMsg);
							iXPValue = pNumericData->iNumericValue;
						}
						else
						{
							pchCategoryXp = TranslateDisplayMessage(pDef->displayNameMsg);
							iXPValue = pNumericData->iNumericValue;
						}
					}
				}
				FOR_EACH_END

					FOR_EACH_IN_EARRAY(pOutcomeRequest->pData->eaRewards, InventorySlot, pInventorySlot)
				{
					if (pInventorySlot->pItem)
					{
						ItemDef *pDef = GET_REF(pInventorySlot->pItem->hItem);

						if (pDef && strstri(pDef->pchName, g_ItemAssignmentSettings.pchXPFilterBaseName))
						{
							if (!stricmp(pDef->pchName, g_ItemAssignmentSettings.pchXPFilterBaseName))
							{
								pchXp = item_GetName(pInventorySlot->pItem, pEnt);
								iXPValue = pInventorySlot->pItem->count;
							}
							else
							{
								pchCategoryXp = item_GetName(pInventorySlot->pItem, pEnt);
								iCategoryXPValue = pInventorySlot->pItem->count;
							}
						}
					}
				}
				FOR_EACH_END

					break;
			}
		}
		FOR_EACH_END

			if (pchCategoryXp && pchXp)
				return MessageExprFormatString2Int2(pContext, pchMessageKey, pchCategoryXp, pchXp, iCategoryXPValue, iXPValue);
		if (pchXp)
			return MessageExprFormatStringInt(pContext, pchMessageKey, pchXp, iXPValue);
		if (pchCategoryXp)
			return MessageExprFormatStringInt(pContext, pchMessageKey, pchCategoryXp, iCategoryXPValue);

		return "";
	}

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemAssignmentMessageFormatGetCompletionExperience);
const char *GenExprItemAssignment_MessageFormatGetCompletionExperience(	ExprContext *pContext, 
	ACMD_EXPR_DICT(Message) const char *pchMessageKey, 
	const char *pchItemAssignmentName)
{
	Entity* pEnt = entActivePlayerPtr();

	if (pEnt)
	{
		const char *pchCategoryXp = NULL;
		S32 iCategoryXPValue = 0;
		ItemAssignmentDef* pDef = RefSystem_ReferentFromString(g_hItemAssignmentDict, pchItemAssignmentName);
		ItemAssignmentCategorySettings *pCatSettings = pDef ? ItemAssignmentCategory_GetSettings(pDef->eCategory) : NULL;

		if (!pDef || !pCatSettings || !pCatSettings->pchNumericXP1)
			return "";

		pchCategoryXp = gclInvExprInventoryNumericDisplayName(pEnt, pCatSettings->pchNumericXP1);
		if (!pchCategoryXp)
			return "";

		return MessageExprFormatStringInt(pContext, pchMessageKey, pchCategoryXp, pDef->iCompletionExperience);
	}

	return "";
}


SA_RET_NN_VALID extern Item *gclInvExprItemGetDummy(const char *pchItemDef);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetDummyRewardsForItemAssignmentOutcome);
void GenExprGetDummyRewardsForItemAssignmentOutcome(ExprContext *pContext, SA_PARAM_NN_VALID UIGen* pGen, const char* pchOutcome, ACMD_EXPR_SUBEXPR_IN pSubExpr)
{
	static int s_iGenDataHandle;
	static const char *s_pchGenData;
	static char *s_estrExprResult;
	InventorySlot*** peaData = ui_GenGetManagedListSafe(pGen, InventorySlot);
	Entity* pEnt = entActivePlayerPtr();
	S32 iCount = 0;

	if (!s_pchGenData)
		s_pchGenData = allocAddString("GenData");

	if (pEnt && pIACache->pRewardRequestData)
	{
		const char* pchOutcomePooled = allocFindString(pchOutcome);
		S32 i, j, k;

		for (i = eaSize(&pIACache->pRewardRequestData->eaOutcomes)-1; i >= 0; i--)
		{
			ItemAssignmentOutcomeRewardRequest* pOutcomeRequest = pIACache->pRewardRequestData->eaOutcomes[i];
			if (pchOutcomePooled == pOutcomeRequest->pchOutcome)
			{
				if (pOutcomeRequest->pData)
				{
					ParseTable *pTable;
					void *pGenData;
					ExprContext *pSubContext;

					for (j = eaSize(&pOutcomeRequest->pData->eaNumericRewards)-1; j >= 0; j--)
					{
						ItemNumericData* pNumericData = pOutcomeRequest->pData->eaNumericRewards[j];
						if (inv_FillNumericRewardRequestClient(pEnt, pNumericData, pOutcomeRequest->pData))
						{
							eaRemove(&pOutcomeRequest->pData->eaNumericRewards, j);
							StructDestroy(parse_ItemNumericData, pNumericData);
						}
					}

					pSubContext = ui_GenGetContext(pGen);
					pGenData = exprContextGetVarPointerAndTypePooled(pSubContext, s_pchGenData, &pTable);
					for (j = 0; j < eaSize(&pOutcomeRequest->pData->eaRewards); j++)
					{
						InventorySlot *pSource = pOutcomeRequest->pData->eaRewards[j];
						NOCONST(InventorySlot) *pExisting = NULL;
						MultiVal mv = {0};

						exprContextSetPointerVarPooledCached(pSubContext, s_pchGenData, pSource->pItem, parse_Item, true, true, &s_iGenDataHandle);
						exprEvaluateSubExpr(pSubExpr, pContext, pSubContext, &mv, false);

						if (mv.type == MULTI_STRING)
							MultiValToEString(&mv, &s_estrExprResult);

						if (s_estrExprResult && *s_estrExprResult)
						{
							const char *pchPooledResult = allocFindString(s_estrExprResult);
							for (k = iCount - 1; k >= 0 && pchPooledResult; k--)
							{
								InventorySlot *pInited = (*peaData)[k];
								ItemDef *pDef = pInited->pItem ? GET_REF(pInited->pItem->hItem) : NULL;
								const char *pchDefName = pDef ? pDef->pchName : pInited->pItem ? REF_STRING_FROM_HANDLE(pInited->pItem->hItem) : NULL;
								if (pchDefName == pchPooledResult)
								{
									pExisting = CONTAINER_NOCONST(InventorySlot, pInited);
									break;
								}
							}
						}

						if (pExisting && pExisting->pItem)
						{
							// Increment existing item
							pExisting->pItem->count += !pSource->pItem ? 1 : pSource->pItem->count < 1 ? 1 : pSource->pItem->count;
						}
						else if (s_estrExprResult && *s_estrExprResult)
						{
							// New dummy item
							Item *pDummy = gclInvExprItemGetDummy(s_estrExprResult);
							pExisting = eaGetStruct(peaData, parse_InventorySlot, iCount++);
							if (!pExisting->pItem)
								pExisting->pItem = StructCloneDeConst(parse_Item, pDummy);
							else
								StructCopyAllDeConst(parse_Item, pDummy, pExisting->pItem);
						}
						else
						{
							// Copy item from source
							pExisting = eaGetStruct(peaData, parse_InventorySlot, iCount++);
							StructCopyAllDeConst(parse_InventorySlot, pSource, pExisting);
						}

						estrClear(&s_estrExprResult);
					}

					exprContextSetPointerVarPooledCached(pSubContext, s_pchGenData, pGenData, pTable, true, true, &s_iGenDataHandle);
				}
				break;
			}
		}
	}

	eaSetSizeStruct(peaData, parse_InventorySlot, iCount);
	ui_GenSetManagedListSafe(pGen, peaData, InventorySlot, true);
}

// --------------------------------------------------------------------------------------------------------------------

// This is a model call to get a list of sample rewards for the item assignment from the first outcome 
// only valid if the ItemAssignmentSettings's bGenerateSampleRewardTable is set. 
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemAssignmentGetSampleRewards);
void GenExprItemAssignmentGetSampleRewards(ExprContext *pContext, 
	const char* pchAssignmentDef, 
	SA_PARAM_NN_VALID UIGen* pGen)
{
	ItemAssignmentDef* pAssignmentDef = NULL;
	InventorySlot*** peaData = ui_GenGetManagedListSafe(pGen, InventorySlot);

	eaClearStruct(peaData, parse_InventorySlot);

	if (pchAssignmentDef)
		pAssignmentDef = ItemAssignment_DefFromName(pchAssignmentDef);

	if (pAssignmentDef)
	{
		ItemAssignmentOutcome *pSampleOutcome = eaTail(&pAssignmentDef->eaOutcomes);

		if (SAFE_MEMBER2(pSampleOutcome, pResults, pSampleRewards))
		{
			// check if we've created the client-side rewards for the InvRewardRequest
			if (!eaSize(&pSampleOutcome->pResults->pSampleRewards->eaRewards))
			{
				inv_FillRewardRequestClient(NULL, pSampleOutcome->pResults->pSampleRewards, pSampleOutcome->pResults->pSampleRewards, true);
			}
			eaCopyStructs(&pSampleOutcome->pResults->pSampleRewards->eaRewards, peaData, parse_InventorySlot);
		}
	}

	ui_GenSetManagedListSafe(pGen, peaData, InventorySlot, true);
}


// starts a given assignment using the slotted items in pIACache->eaSlots, in the given assignment slot
// 
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemAssignmentsStartAssignmentAtSlot);
bool GenExprItemAssignments_StartAssignmentAtSlot(const char* pchAssignmentDef, S32 iSlot)
{
	ItemAssignmentDef* pDef = ItemAssignment_DefFromName(pchAssignmentDef);
	if (pDef)
	{
		ItemAssignmentSlots Slots = {0};
		S32 i, iSize = eaSize(&pIACache->eaSlots);

		// do a check to make sure this slot is valid
		if (g_ItemAssignmentSettings.pStrictAssignmentSlots)
		{
			Entity *pEnt = entActivePlayerPtr();
			if (pEnt)
			{	
				if (!ItemAssignments_IsValidNewItemAssignmentSlot(pEnt, g_ItemAssignmentSettings.pStrictAssignmentSlots, iSlot))
				{	// todo: say that we failed because the assignment slot was invalid
					return false;
				}
			}
		}

		for (i = 0; i < iSize; i++)
		{
			ItemAssignmentSlotUI* pSlotUI = pIACache->eaSlots[i];
			if (pSlotUI->uItemID)
			{
				NOCONST(ItemAssignmentSlottedItem)* pSlot = StructCreateNoConst(parse_ItemAssignmentSlottedItem);
				pSlot->uItemID = pSlotUI->uItemID;
				pSlot->iAssignmentSlot = pSlotUI->iAssignmentSlot;
				eaPush(&Slots.eaSlots, (ItemAssignmentSlottedItem*)pSlot);
			}
		}

		ServerCmd_gslItemAssignments_StartNewAssignment(pchAssignmentDef, iSlot, &Slots);
		StructDeInit(parse_ItemAssignmentSlots, &Slots);
		return true;
	}
	return false;
}

// starts a given assignment using the slotted items in pIACache->eaSlots
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemAssignmentsStartAssignment);
bool GenExprItemAssignmentsStartAssignment(const char* pchAssignmentDef)
{
	return GenExprItemAssignments_StartAssignmentAtSlot(pchAssignmentDef, 0);
}

int SortByRecommendation(const ItemRecommendationValue **ppLeft, const ItemRecommendationValue **ppRight)
{
	const ItemRecommendationValue *pLeft = *ppLeft;
	const ItemRecommendationValue *pRight = *ppRight;
	F32 fDelta = pLeft->fValue - pRight->fValue;

	// We want higher values first
	if (!nearf(fDelta, 0))
		return fDelta < 0 ? 1 : -1;

	if (pLeft->pBag->BagID != pRight->pBag->BagID)
		return (int)pLeft->pBag->BagID - (int)pRight->pBag->BagID;

	return stricmp(pLeft->pSlot->pchName, pRight->pSlot->pchName);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemAssignmentSlotGetRecommendedItems");
bool GenExprItemAssignmentSlotGetRecommendedItems(SA_PARAM_NN_VALID UIGen *pGen, const char *pchAssignment, S32 iSlot, const char* pchOutcomes, S32 iLimit, const char *pchBagIds, const char *pchPrefix, U32 uOptions)
{
	static InventoryBag **s_eaBags = NULL;
	static InventorySlot **s_eaInvSlots = NULL;
	static ItemRecommendationValue **s_eaValues = NULL;
	static F32 *s_eafWeights = NULL;
	static S32 *s_eaiOutcomes = NULL;
	static NOCONST(ItemAssignmentSlottedItem) **s_eaSlottedItems = NULL;
	ItemAssignmentDef *pDef = RefSystem_ReferentFromString(g_hItemAssignmentDict, pchAssignment);
	ItemAssignmentSlot *pSlotDef = pDef ? eaGet(&pDef->eaSlots, iSlot) : NULL;
	Entity *pEnt = entActivePlayerPtr();
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	char *pchBuffer, *pchToken, *pchContext = NULL;
	S32 i, j, k, iValues = 0;
	InventoryBag *pBag;
	ItemRecommendationValue *pRecommendation;

	eaClearFast(&s_eaBags);
	eaClearFast(&s_eaInvSlots);
	eaiClearFast(&s_eaiOutcomes);

	strdup_alloca(pchBuffer, pchBagIds);
	if (pchToken = strtok_r(pchBuffer, " \r\n\t,|%", &pchContext))
	{
		do
		{
			InvBagIDs BagID = StaticDefineIntGetInt(InvBagIDsEnum, pchToken);
			NOCONST(InventoryBag) *pEntBag = inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), BagID, pExtract);
			if (pEntBag)
				eaPush(&s_eaBags, (InventoryBag *)pEntBag);
		} while (pchToken = strtok_r(NULL, " \r\n\t,|%", &pchContext));
	}

	strdup_alloca(pchBuffer, pchOutcomes);
	if ((pchToken = strtok_r(pchBuffer, " \r\n\t,|%", &pchContext)) && pDef)
	{
		do
		{
			const char *pchOutcome = allocFindString(pchToken);
			S32 iOutcome;
			for (iOutcome = eaSize(&pDef->eaOutcomes) - 1; pchOutcome && iOutcome >= 0; --iOutcome)
			{
				if (pDef->eaOutcomes[iOutcome]->pchName == pchOutcome)
				{
					eaiPush(&s_eaiOutcomes, iOutcome);
					break;
				}
			}
		} while (pchToken = strtok_r(NULL, " \r\n\t,|%", &pchContext));
	}

	if (pSlotDef && eaiSize(&s_eaiOutcomes) && eaSize(&s_eaBags))
	{
		if (!eaSize(&s_eaSlottedItems))
			eaSetSizeStructNoConst(&s_eaSlottedItems, parse_ItemAssignmentSlottedItem, 1);

		// Compute the values for the item
		for (i = eaSize(&s_eaBags) - 1; i >= 0; --i)
		{
			pBag = s_eaBags[i];
			for (j = eaSize(&pBag->ppIndexedInventorySlots) - 1; j >= 0; --j)
			{
				InventorySlot *pSlot = pBag->ppIndexedInventorySlots[j];
				S32 iBagSlot = pSlot->pchName ? atoi(pSlot->pchName) : 0;
				ItemDef *pItemDef = pSlot->pItem ? GET_REF(pSlot->pItem->hItem) : NULL;
				if (pItemDef && ItemAssignmentsCanSlotItem(pEnt, iSlot, pBag->BagID, iBagSlot, pExtract)
					&& (!(uOptions & ITEM_ASSIGNMENT_RECOMMEND_EXCLUDE_NEW_SLOT) || GenExprItemGetNewAssignmentSlot(pBag->BagID, iBagSlot) < 0))
				{
					F32 fSum = 0, fMaximize = 0;
					pRecommendation = eaGetStruct(&s_eaValues, parse_ItemRecommendationValue, iValues++);
					pRecommendation->pBag = pBag;
					pRecommendation->pSlot = pSlot;
					pRecommendation->fValue = 0;

					// Calculate the value
					s_eaSlottedItems[0]->iAssignmentSlot = iSlot;
					s_eaSlottedItems[0]->eBagID = pBag->BagID;
					s_eaSlottedItems[0]->iBagSlot = iBagSlot;
					s_eaSlottedItems[0]->uItemID = pSlot->pItem->id;
					eafClear(&s_eafWeights);
					ItemAssignments_CalculateOutcomeWeights(pEnt, pDef, (ItemAssignmentSlottedItem **)s_eaSlottedItems, &s_eafWeights);
					for (k = eafSize(&s_eafWeights) - 1; k >= 0; --k)
						fSum += s_eafWeights[k];
					for (k = eaiSize(&s_eaiOutcomes) - 1; k >= 0; --k)
						fMaximize += s_eafWeights[s_eaiOutcomes[k]];
					pRecommendation->fValue = fMaximize / fSum;
				}
			}
		}

		eaSetSizeStruct(&s_eaValues, parse_ItemRecommendationValue, iValues);
		eaQSort(s_eaValues, SortByRecommendation);

		for (i = 0; i < eaSize(&s_eaValues) && i < iLimit; i++)
		{
			pRecommendation = s_eaValues[i];
			eaPushUnique(&s_eaBags, pRecommendation->pBag);
			eaPush(&s_eaInvSlots, pRecommendation->pSlot);
		}
	}

	return gclInvCreateCategorizedSlots(pGen, pEnt, s_eaBags, s_eaInvSlots, pchPrefix, uOptions);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CLIENTCMD;
void gclItemAssignments_ReceiveRewardsForOutcome(ItemAssignmentRewardRequestData* pData)
{
	Entity* pEnt = entActivePlayerPtr();

	s_uNextRewardRequestTime = 0;
	StructDestroySafe(parse_ItemAssignmentRewardRequestData, &pIACache->pRewardRequestData);

	if (pEnt && pData)
	{
		GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		S32 i;

		pIACache->pRewardRequestData = StructCreate(parse_ItemAssignmentRewardRequestData);
		COPY_HANDLE(pIACache->pRewardRequestData->hDef, pData->hDef);

		eaSetSizeStruct(&pIACache->pRewardRequestData->eaOutcomes, 
			parse_ItemAssignmentOutcomeRewardRequest,
			eaSize(&pData->eaOutcomes));

		for (i = 0; i < eaSize(&pData->eaOutcomes); i++)
		{
			ItemAssignmentOutcomeRewardRequest* pRequestOutcome = pIACache->pRewardRequestData->eaOutcomes[i];
			pRequestOutcome->pchOutcome = allocAddString(pData->eaOutcomes[i]->pchOutcome);

			if (pData->eaOutcomes[i]->pData)
			{
				if (!pRequestOutcome->pData)
					pRequestOutcome->pData = StructCreate(parse_InvRewardRequest);

				inv_FillRewardRequestClient(pEnt, pData->eaOutcomes[i]->pData, pRequestOutcome->pData, true);
			}
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemAssignmentRequestRewards);
void GenExprItemAssignmentRequestRewards(const char* pchAssignmentDef, U32 uAssignmentID)
{
	U32 uCurrentTime = timeSecondsSince2000();

	if (uCurrentTime >= s_uNextRewardRequestTime)
	{
		ServerCmd_ItemAssignmentRequestRewards(pchAssignmentDef, uAssignmentID);
		s_uNextRewardRequestTime = uCurrentTime + ITEM_ASSIGNMENT_REWARD_REQUEST_TIMEOUT;
	}
}

// check if the player still has the assignment.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemAssignmentHasAssignment);
bool GenExprItemAssignmentIsAssignmentStillValid(const char* pchAssignmentName)
{
	Entity* pEnt = entActivePlayerPtr();
	ItemAssignmentDef* pDef = ItemAssignment_DefFromName(pchAssignmentName);
	if (pDef && SAFE_MEMBER2(pEnt, pPlayer, pItemAssignmentData))
	{
		return ItemAssignments_HasAssignment(pEnt, pDef, pEnt->pPlayer->pItemAssignmentData);
	}
	return false;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(IsItemSlottedOnActiveItemAssignment);
bool GenExprIsItemSlottedOnActiveItemAssignment(SA_PARAM_OP_VALID Item* pItem, const char* pchAssignmentName)
{
	Entity* pEnt = entActivePlayerPtr();
	ItemAssignmentPersistedData* pPersistedData = SAFE_MEMBER2(pEnt, pPlayer, pItemAssignmentPersistedData);
	ItemAssignmentDef* pFindDef = ItemAssignment_DefFromName(pchAssignmentName);

	if (pItem && pPersistedData && pFindDef)
	{
		S32 i, j;
		for (i = eaSize(&pPersistedData->eaActiveAssignments)-1; i >= 0; i--)
		{
			ItemAssignment* pAssignment = pPersistedData->eaActiveAssignments[i];

			if (pFindDef != GET_REF(pAssignment->hDef))
				continue;

			for (j = eaSize(&pAssignment->eaSlottedItems)-1; j >= 0; j--)
			{
				ItemAssignmentSlottedItem* pSlottedItem = pAssignment->eaSlottedItems[j];
				if (pSlottedItem->uItemID == pItem->id)
				{
					return true;
				}
			}
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemGetActiveAssignment");
SA_RET_OP_VALID ItemAssignmentUI *GenExprItemGetActiveAssignment(SA_PARAM_OP_VALID Item* pItem)
{
	static U32 s_uLastTime;
	static U64 *s_eaiLastItemId;
	static ItemAssignmentUI s_Assignment;

	Entity* pEnt = entActivePlayerPtr();
	ItemAssignmentPersistedData* pPlayerData = SAFE_MEMBER2(pEnt, pPlayer, pItemAssignmentPersistedData);
	S32 i, j;
	U32 uTimeCurrent = timeServerSecondsSince2000();

	if (!pItem)
	{
		return NULL;
	}

	if (s_uLastTime == gGCLState.totalElapsedTimeMs && eai64Find(&s_eaiLastItemId, pItem->id) >= 0)
	{
		return &s_Assignment;
	}

	if (pPlayerData)
	{
		for (i = 0; i < eaSize(&pPlayerData->eaActiveAssignments); i++)
		{
			ItemAssignment* pAssignment = pPlayerData->eaActiveAssignments[i];

			for (j = eaSize(&pAssignment->eaSlottedItems) - 1; j >= 0; j--)
			{
				if (pAssignment->eaSlottedItems[j]->uItemID == pItem->id)
				{
					break;
				}
			}

			if (j >= 0)
			{
				FillActiveItemAssignment(&s_Assignment, pAssignment, pEnt, pPlayerData, uTimeCurrent);

				// Update cache
				eai64Clear(&s_eaiLastItemId);
				for (j = eaSize(&pAssignment->eaSlottedItems) - 1; j >= 0; j--)
				{
					eai64Push(&s_eaiLastItemId, pAssignment->eaSlottedItems[j]->uItemID);
				}

				return &s_Assignment;
			}
		}
	}

	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemAssignmentFindNextToComplete");
SA_RET_OP_VALID ItemAssignmentUI *GenExprItemAssignmentFindNextToComplete(void)
{
	static U32 s_uLastTime;
	static ItemAssignmentUI s_Assignment;

	Entity* pEnt = entActivePlayerPtr();
	ItemAssignmentPersistedData* pPlayerData = SAFE_MEMBER2(pEnt, pPlayer, pItemAssignmentPersistedData);
	S32 i;
	U32 uTimeCurrent = timeServerSecondsSince2000();
	ItemAssignment* pBestAssignment = NULL;
	U32 uBestTime = 0;

	if (!pPlayerData || !eaSize(&pPlayerData->eaActiveAssignments))
	{
		return NULL;
	}

	if (s_uLastTime == gGCLState.totalElapsedTimeMs)
	{
		return &s_Assignment;
	}

	for (i = eaSize(&pPlayerData->eaActiveAssignments) - 1; i >= 0; --i)
	{
		ItemAssignment* pAssignment = pPlayerData->eaActiveAssignments[i];
		ItemAssignmentDef* pDef = GET_REF(pAssignment->hDef);
		if (!pDef || pAssignment->pchRewardOutcome)
			continue;

		if (!pBestAssignment || pAssignment->uTimeStarted + ItemAssignment_GetDuration(pAssignment, pDef) < uBestTime)
		{
			pBestAssignment = pAssignment;
			uBestTime = pAssignment->uTimeStarted + ItemAssignment_GetDuration(pAssignment, pDef);
		}
	}

	if (pBestAssignment)
	{
		FillActiveItemAssignment(&s_Assignment, pBestAssignment, pEnt, pPlayerData, uTimeCurrent);
		s_uLastTime = gGCLState.totalElapsedTimeMs;
		return &s_Assignment;
	}

	return NULL;
}

// Returns true if the player has any active item assignments that are completed waiting for collection
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemAssignmentHasAnyReadyToComplete);
S32 GenExprItemAssignmentHasAnyReadyToComplete()
{
	Entity* pEnt = entActivePlayerPtr();
	ItemAssignmentPersistedData* pPlayerData = SAFE_MEMBER2(pEnt, pPlayer, pItemAssignmentPersistedData);

	if (pPlayerData)
	{
		FOR_EACH_IN_EARRAY(pPlayerData->eaActiveAssignments, ItemAssignment, pAssignment)
		{
			if (pAssignment->pchRewardOutcome)
				return true;
		}
		FOR_EACH_END
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetMaxActiveAssignmentPoints);
S32 GenExprGetMaxActiveAssignmentPoints(void)
{
	Entity* pEnt = entActivePlayerPtr();
	return ItemAssignments_GetMaxAssignmentPoints(pEnt);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetActiveAssignmentPointsRemaining);
S32 GenExprGetActiveAssignmentPointsRemaining(void)
{
	Entity* pEnt = entActivePlayerPtr();
	return ItemAssignments_GetRemainingAssignmentPoints(pEnt);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemAssignmentsGetValidSlots);
void ExprItemAssignmentsGetValidSlots(SA_PARAM_NN_VALID UIGen* pGen, S32 eBagID, S32 iBagSlot)
{
	ItemAssignmentSlotUI*** peaSlots = ui_GenGetManagedListSafe(pGen, ItemAssignmentSlotUI);
	Entity* pEnt = entActivePlayerPtr();
	GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	int i;
	int iCount = 0;
	for (i = 0; i < eaSize(&pIACache->eaSlots); i++)
	{
		if (ItemAssignmentsCanSlotItem(pEnt, i, eBagID, iBagSlot, pExtract))
		{
			eaGetStruct(peaSlots, parse_ItemAssignmentSlotUI, iCount++);
		}
	}
	eaSetSizeStruct(peaSlots, parse_ItemAssignmentSlotUI, iCount);
	ui_GenSetManagedListSafe(pGen, peaSlots, ItemAssignmentSlotUI, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemAssignmentsCheckForOneValidSlot);
bool ExprItemAssignmentsCheckForOneValidSlot(S32 eBagID, S32 iBagSlot)
{
	Entity* pEnt = entActivePlayerPtr();
	GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	int i;
	int bFound = false;
	for (i = 0; i < eaSize(&pIACache->eaSlots); i++)
	{
		if (ItemAssignmentsCanSlotItem(pEnt, i, eBagID, iBagSlot, pExtract))
		{
			if (!bFound)
				bFound = true;
			else
				break;
		}
	}
	return (i >= eaSize(&pIACache->eaSlots));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemAssignmentsCanSlotItemInSlot);
bool ExprItemAssignmentsCanSlot(S32 iSlot, S32 eBagID, S32 iBagSlot)
{
	Entity* pEnt = entActivePlayerPtr();
	GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	return ItemAssignmentsCanSlotItem(pEnt, iSlot, eBagID, iBagSlot, pExtract);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemAssignmentsGetNumCompleteNowTokens);
S32 ExprItemAssignmentsGetNumCompleteNowTokens(void)
{
	Entity* pEnt = entActivePlayerPtr();
	return gad_GetAttribInt(entity_GetGameAccount(pEnt), MicroTrans_GetItemAssignmentCompleteNowGADKey());
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemAssignmentsGetNumUnslotTokens);
S32 ExprItemAssignmentsGetNumUnslotTokens(void)
{
	Entity* pEnt = entActivePlayerPtr();
	return gad_GetAttribInt(entity_GetGameAccount(pEnt), MicroTrans_GetItemAssignmentUnslotTokensGADKey());
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenItemAssignmentsGetRequiredItemsEx");
S32 GenExprItemAssignmentsGetRequiredItemsEx(SA_PARAM_NN_VALID UIGen *pGen, const char *pchItemAssignment, U32 uOptionFlags)
{
	S32 iReturn;
	InventorySlot ***peaInvSlots = ui_GenGetManagedListSafe(pGen, InventorySlot);
	Entity *pEnt = entActivePlayerPtr();

	iReturn = ItemAssignmentsGetRequiredItemsInternal(pEnt,peaInvSlots, pchItemAssignment, uOptionFlags);

	ui_GenSetManagedListSafe(pGen, peaInvSlots, InventorySlot, false);

	return iReturn;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenItemAssignmentsGetRequiredItems");
S32 GenExprItemAssignmentsGetRequiredItems(SA_PARAM_NN_VALID UIGen *pGen, const char *pchItemAssignment)
{
	Entity *pEnt = entActivePlayerPtr();
	InventorySlot ***peaInvSlots = ui_GenGetManagedListSafe(pGen, InventorySlot);
	S32 iReturn;

	iReturn = ItemAssignmentsGetRequiredItemsInternal(pEnt, peaInvSlots, pchItemAssignment, 0);

	ui_GenSetManagedListSafe(pGen, peaInvSlots, InventorySlot, false);

	return iReturn;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemAssignmentsHasItemCosts");
bool GenExprItemAssignments_HasItemCosts(const char *pchItemAssignment)
{
	ItemAssignmentDef* pDef = RefSystem_ReferentFromString(g_hItemAssignmentDict, pchItemAssignment);

	if (pDef && pDef->pRequirements)
	{
		return eaSize(&pDef->pRequirements->eaItemCosts) > 0;
	}

	return false;
}

// checks if the given weight is numerically equal to the one of the assignment
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemAssignmentsIsOfWeight");
bool GenExprItemAssignments_IsOfWeight(const char *pchItemAssignment, const char *pchWeight)
{
	ItemAssignmentDef* pDef = RefSystem_ReferentFromString(g_hItemAssignmentDict, pchItemAssignment);
	ItemAssignmentWeightType eWeight = StaticDefineIntGetIntDefault(ItemAssignmentWeightTypeEnum, pchWeight, kItemAssignmentWeightType_Default);
	F32 fGivenWeight = ItemAssignmentWeightType_GetWeightValue(eWeight);
	F32 fAssignmentWeight = pDef ? ItemAssignmentWeightType_GetWeightValue(pDef->eWeight) : 0.f;

	return (fAssignmentWeight == fGivenWeight);
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenItemAssignmentsGetRequiredNumericValue");
S32 GenExprItemAssignmentsGetRequiredNumericValue(SA_PARAM_OP_VALID ItemAssignmentUI *pAssignment)
{
	if (pAssignment)
	{
		ItemAssignmentDef *pDef = GET_REF(pAssignment->hDef);
		if (pDef && 
			pDef->pRequirements && 
			IS_HANDLE_ACTIVE(pDef->pRequirements->hRequiredNumeric) && 
			pDef->pRequirements->iRequiredNumericValue > 0)
		{
			return pDef->pRequirements->iRequiredNumericValue;
		}
	}

	return -1;
}

// Get the slot's required categories
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenItemAssignmentsGetRequiredCategories");
bool GenExprItemAssignmentsGetRequiredCategories(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID ItemAssignmentSlotUI *pSlotUI, const char *pchPrefix)
{
	ItemAssignmentDef *pDef = pSlotUI ? GET_REF(pSlotUI->hDef) : NULL;
	ItemAssignmentSlot *pSlot = pSlotUI && pDef ? eaGet(&pDef->eaSlots, pSlotUI->iAssignmentSlot) : NULL;

	if (pSlot)
	{
		gclInvCreateCategoryList(pGen, pSlot->peRequiredItemCategories, pchPrefix);
		return true;
	}
	return false;
}

// Get the slot's restricted categories
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenItemAssignmentsGetRestrictCategories");
bool GenExprItemAssignmentsGetRestrictCategories(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID ItemAssignmentSlotUI *pSlotUI, const char *pchPrefix)
{
	ItemAssignmentDef *pDef = pSlotUI ? GET_REF(pSlotUI->hDef) : NULL;
	ItemAssignmentSlot *pSlot = pSlotUI && pDef ? eaGet(&pDef->eaSlots, pSlotUI->iAssignmentSlot) : NULL;

	if (pSlot)
	{
		gclInvCreateCategoryList(pGen, pSlot->peRestrictItemCategories, pchPrefix);
		return true;
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemAssignmentSlotGetOverallOutcomeModifier");
F32 GenExprItemAssignmentSlotGetOverallModifier(SA_PARAM_OP_VALID ItemAssignmentSlotUI *pSlotUI, /*ItemCategory*/ S32 eCategory, const char *pchOutcome)
{
	Entity* pEnt = entActivePlayerPtr();	
	GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	ItemAssignmentDef *pDef = pSlotUI ? GET_REF(pSlotUI->hDef) : NULL;
	ItemAssignmentSlot *pSlotDef = pDef ? eaGet(&pDef->eaSlots, pSlotUI->iAssignmentSlot) : NULL;
	ItemAssignmentOutcome* pOutcome = ItemAssignment_GetOutcomeByName(pDef, pchOutcome);
	F32 fContribution = 0;
	S32 i;

	pchOutcome = allocFindString(pchOutcome);

	if (pEnt && pSlotDef && pOutcome)
	{
		Item* pItem = inv_GetItemFromBag(pEnt, pSlotUI->eBagID, pSlotUI->iBagSlot, pExtract);
		const char** ppchOutcomeModifiers = ItemAssignments_GetOutcomeModifiersForSlot(pSlotDef);

		for (i = 0; i < eaSize(&ppchOutcomeModifiers); ++i)
		{
			ItemAssignmentOutcomeModifier *pModifier = ItemAssignment_GetModifierByName(pDef, ppchOutcomeModifiers[i]);
			ItemAssignmentOutcomeModifierTypeData* pModType = pModifier ? ItemAssignment_GetModifierTypeData(pModifier->eType) : NULL;

			if (pModType && eaFind(&pModType->ppchAffectedOutcomes, pchOutcome) >= 0)
			{
				// TODO(jm): This should be providing the item, but in Star Trek, we don't want the weight to be included.
				// This function probably needs some flags to direct ItemAssignments_GetOutcomeWeightModifier() what to ignore.
				fContribution += ItemAssignments_GetOutcomeWeightModifier(pEnt, pOutcome, pModifier, pModType, pSlotDef, NULL, (ItemCategory)eCategory);
			}
		}
	}

	return fContribution;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemAssignmentActiveCount");
S32 GenExprItemAssignmentActiveCount(U32 uFlags, const char *pchFilterSubstring)
{
	Entity* pEnt = entActivePlayerPtr();
	ItemAssignmentPersistedData* pPlayerData = SAFE_MEMBER2(pEnt, pPlayer, pItemAssignmentPersistedData);
	S32 i, iCount = 0;
	ItemAssignmentCategory eCategory = pchFilterSubstring && *pchFilterSubstring ? StaticDefineIntGetIntDefault(ItemAssignmentCategoryEnum, pchFilterSubstring, atoi(pchFilterSubstring)) : kItemAssignmentCategory_None;
	if (!pPlayerData)
		return 0;
	for (i = eaSize(&pPlayerData->eaActiveAssignments) - 1; i >= 0; i--)
	{
		ItemAssignmentDef *pDef = GET_REF(pPlayerData->eaActiveAssignments[i]->hDef);

		if (pPlayerData->eaActiveAssignments[i]->pchRewardOutcome)
		{
			if ((uFlags & kItemAssignmentCount_ExcludeWithOutcome))
				continue;
		}
		else
		{
			if ((uFlags & kItemAssignmentCount_ExcludeWithoutOutcome))
				continue;
		}

		if (pDef && (uFlags & kItemAssignmentCount_ExcludeFree))
		{
			if (!pDef->iAssignmentPointCost)
				continue;
		}

		if (pchFilterSubstring && *pchFilterSubstring)
		{
			if ((uFlags & kItemAssignmentCount_FilterCategory))
			{
				if (!pDef || pDef->eCategory != eCategory)
					continue;
			}
			else
			{
				const char *pchName = pDef ? pDef->pchName : REF_STRING_FROM_HANDLE(pPlayerData->eaActiveAssignments[i]->hDef);
				if (!pchName)
					continue;

				if (strstri(pchName, pchFilterSubstring))
				{
					if ((uFlags & kItemAssignmentCount_ExcludeFilter))
						continue;
				}
				else
				{
					if ((uFlags & kItemAssignmentCount_IncludeFilter))
						continue;
				}
			}
		}

		if ((uFlags & kItemAssignmentCount_Items))
			iCount += eaSize(&pPlayerData->eaActiveAssignments[i]->eaSlottedItems);
		else
			iCount++;
	}

	if ((uFlags & kItemAssignmentCount_RecentlyCompleted))
	{
		for (i = eaSize(&pPlayerData->eaRecentlyCompletedAssignments) - 1; i >= 0; --i)
		{
			if (ItemAssignment_EntityGetActiveAssignmentByID(pEnt, pPlayerData->eaRecentlyCompletedAssignments[i]->uAssignmentID))
				continue;

			if ((uFlags & kItemAssignmentCount_Unread) && pPlayerData->eaRecentlyCompletedAssignments[i]->bMarkedAsRead)
				continue;

			if ((uFlags & kItemAssignmentCount_Items))
				iCount += eaSize(&pPlayerData->eaRecentlyCompletedAssignments[i]->eaSlottedItemRefs);
			else
				iCount++;
		}
	}

	if ((uFlags & kItemAssignmentCount_ChainHeaders))
	{
		for (i = eaSize(&pPlayerData->eaCompletedAssignments) - 1; i >= 0; --i)
		{
			ItemAssignmentDef *pDef = GET_REF(pPlayerData->eaCompletedAssignments[i]->hDef);
			if (!pDef || !eaSize(&pDef->eaDependencies) || (pDef->pRequirements && IS_HANDLE_ACTIVE(pDef->pRequirements->hRequiredAssignment)))
				continue;

			iCount++;
		}
	}
	return iCount;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemAssignmentUpdateTime");
S32 GenExprItemAssignmentUpdateTime(void)
{
	U32 uNow = timeServerSecondsSince2000();
	U32 uAlign = MAX(g_ItemAssignmentSettings.uAssignmentRefreshTime, 1);
	return ((uNow + uAlign) / uAlign) * uAlign - uNow;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemAssignmentPersonalUpdateTimeEx");
S32 GenExprItemAssignmentPersonalUpdateTimeEx(S32 iPersonalBucket)
{
	Entity* pEnt = entActivePlayerPtr();
	return ItemAssignmentPersonalUpdateTime(pEnt,iPersonalBucket);
}

// The amount of time remaining in seconds until the next personal assignment update
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemAssignmentPersonalUpdateTime");
S32 GenExprItemAssignmentPersonalUpdateTime(void)
{
	return GenExprItemAssignmentPersonalUpdateTimeEx(0);
}

// The amount of time remaining until the next assignment update for the player's current contact dialog
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemAssignmentContactUpdateTime");
S32 GenExprItemAssignmentContactUpdateTime(void)
{
	Entity* pEnt = entActivePlayerPtr();
	ContactDialog* pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	if (pDialog)
	{
		U32 uNow = timeServerSecondsSince2000();
		U32 uAlign = MAX(pDialog->uItemAssignmentRefreshTime, 1);
		return ((uNow + uAlign) / uAlign) * uAlign - uNow;
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemAssignmentRisk");
F32 GenExprItemAssignmentRisk(const char *pchAssignmentName, U32 uFlags)
{
	ItemAssignmentDef *pDef = RefSystem_ReferentFromString(g_hItemAssignmentDict, pchAssignmentName);
	S32 i;
	F32 fTotal = 0;
	if (pDef)
	{
		bool bMax = !!(uFlags & kItemAssignmentRiskFlags_Max);
		for (i = 0; i < eaSize(&pDef->eaOutcomes); i++)
		{
			if (pDef->eaOutcomes[i]->pResults)
			{
				ItemAssignmentOutcomeResults *pResults = pDef->eaOutcomes[i]->pResults;

				if ((uFlags & kItemAssignmentRiskFlags_Destroy)
					&& (!IS_HANDLE_ACTIVE(pResults->msgDestroyDescription.hMessage)
					|| !(uFlags & kItemAssignmentRiskFlags_IgnoreDescribedDestroy)))
				{
					fTotal += bMax ? 1 : ea32Size(&pResults->peDestroyItemsOfQuality) ? pDef->eaOutcomes[i]->pResults->fDestroyChance : 0;
				}

				if ((uFlags & kItemAssignmentRiskFlags_NewAssignment)
					&& (!IS_HANDLE_ACTIVE(pResults->msgNewAssignmentDescription.hMessage)
					|| !(uFlags & kItemAssignmentRiskFlags_IgnoreDescribedNewAssignment)))
				{
					fTotal += bMax ? 1 : IS_HANDLE_ACTIVE(pResults->hNewAssignment) ? pDef->eaOutcomes[i]->pResults->fNewAssignmentChance : 0;
				}
			}
		}
	}
	return fTotal;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemAssignmentCategoryInfo");
SA_RET_OP_VALID ItemAssignmentCategorySettings* GenExprItemAssignmentCategory(U32 eCategory)
{
	return ItemAssignmentCategory_GetSettings(eCategory);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemAssignmentGetNumericQualityScaleForItemDef");
F32 GenExprItemAssignmentGetNumericQualityScaleForItemDef(SA_PARAM_OP_VALID ItemDef* pItemDef)
{
	return ItemAssignment_GetNumericQualityScaleForItemDef(pItemDef);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PlayerCanAccessItemAssignments");
bool GenExprPlayerCanAccessItemAssignments(void)
{
	Entity* pEnt = entActivePlayerPtr();

	return ItemAssignments_PlayerCanAccessAssignments(pEnt);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetItemAssignmentUI");
SA_RET_OP_VALID ItemAssignmentUI *GenExprItemAssignment(const char *pchItemAssignment)
{
	static ItemAssignmentUI s_Data;
	static U32 s_uRequirementUpdateTimeMs = 0;
	Entity* pEnt = entActivePlayerPtr();
	ItemAssignmentDef* pDef = RefSystem_ReferentFromString(g_hItemAssignmentDict, pchItemAssignment);
	ItemAssignmentPlayerData* pPlayerData = SAFE_MEMBER2(pEnt, pPlayer, pItemAssignmentData);
	bool bPersonal = false;

	if (!pDef || !pPlayerData)
	{
		return NULL;
	}

	if (GET_REF(s_Data.hDef) == pDef)
	{
		if (s_uRequirementUpdateTimeMs + 500 < gGCLState.totalElapsedTimeMs)
		{
			ItemAssignment_UpdateRequirementsInfo(&s_Data, pEnt, pDef, g_ItemAssignmentSettings.bGetItemAssignmentUIChecksSlottedNotInventory);
			s_uRequirementUpdateTimeMs = gGCLState.totalElapsedTimeMs;
		}
		return &s_Data;
	}

	{
		ItemAssignmentPersonalIterator it;
		ItemAssignmentDefRef* pRef = NULL;

		ItemAssignments_InitializeIteratorPersonalAssignments(&it);
		while (pRef = ItemAssignments_IterateOverPersonalAssignments(pPlayerData, &it))
		{
			if (GET_REF(pRef->hDef) == pDef)
			{
				bPersonal = true;
				break;
			}
		}

	}

	s_uRequirementUpdateTimeMs = gGCLState.totalElapsedTimeMs;
	SetItemAssignmentData(pEnt, &s_Data, pDef, bPersonal, false, g_ItemAssignmentSettings.bGetItemAssignmentUIChecksSlottedNotInventory);
	return &s_Data;
}

static void ItemAssignments_DictionaryUpdate(enumResourceEventType eType, const char *pDictName, const char *pRefName, Referent pReferent, void *pUserData)
{
	if (eType == RESEVENT_RESOURCE_ADDED)
	{
		pRefName = allocAddString(pRefName);
		if (pRefName && eaFindAndRemove(&pIACache->eaWaitingAssignments, pRefName) >= 0)
		{
			ItemAssignmentDef *pDef = RefSystem_ReferentFromString(g_hItemAssignmentDict, pRefName);

			if (!stricmp(pDictName, "Message"))
			{
				S32 i;
				for (i = eaSize(&pIACache->eaWaitingAssignments) - 1; i >= 0; i--)
				{
					pDef = RefSystem_ReferentFromString(g_hItemAssignmentDict, pIACache->eaWaitingAssignments[i]);
					if (pDef
						&& (GET_REF(pDef->msgDisplayName.hMessage) || !IS_HANDLE_ACTIVE(pDef->msgDisplayName.hMessage))
						&& (GET_REF(pDef->msgDescription.hMessage) || !IS_HANDLE_ACTIVE(pDef->msgDescription.hMessage))
						)
					{
						ItemAssignments_HandleNewAssignment(pDef->pchName);
						eaFindAndRemove(&pIACache->eaWaitingAssignments, pDef->pchName);
					}
				}
			}
			else if (pDef && !ItemAssignments_HandleNewAssignment(pRefName))
			{
				if (!GET_REF(pDef->msgDisplayName.hMessage) && IS_HANDLE_ACTIVE(pDef->msgDisplayName.hMessage))
				{
					eaPush(&pIACache->eaWaitingAssignments, REF_STRING_FROM_HANDLE(pDef->msgDisplayName.hMessage));
				}
				if (!GET_REF(pDef->msgDescription.hMessage) && IS_HANDLE_ACTIVE(pDef->msgDescription.hMessage))
				{
					eaPush(&pIACache->eaWaitingAssignments, REF_STRING_FROM_HANDLE(pDef->msgDescription.hMessage));
				}
				eaPush(&pIACache->eaWaitingAssignments, pDef->pchName);
			}
		}
	}

	if (eaSize(&pIACache->eaWaitingAssignments) <= 0)
	{
		resDictRemoveEventCallback(g_hItemAssignmentDict, ItemAssignments_DictionaryUpdate);
		resDictRemoveEventCallback(gMessageDict, ItemAssignments_DictionaryUpdate);
	}
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gclItemAssignment_NewAssignment(const char *pchAssignmentDef)
{
	if (!ItemAssignments_HandleNewAssignment(pchAssignmentDef))
	{
		eaPush(&pIACache->eaWaitingAssignments, allocAddString(pchAssignmentDef));

		resDictRegisterEventCallback(g_hItemAssignmentDict, ItemAssignments_DictionaryUpdate, NULL);
		resDictRegisterEventCallback(gMessageDict, ItemAssignments_DictionaryUpdate, NULL);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemAssignmentGetChains");
void gclItemAssignment_GetChains(SA_PARAM_NN_VALID UIGen *pGen, const char *pchFilter, U32 uFlags)
{
	ItemAssignmentUI ***peaChains = ui_GenGetManagedListSafe(pGen, ItemAssignmentUI);
	Entity *pEnt = entActivePlayerPtr();
	ItemAssignmentPersistedData* pPlayerData = SAFE_MEMBER2(pEnt, pPlayer, pItemAssignmentPersistedData);
	S32 i, iCount = 0;

	if (uFlags & kItemAssignmentChainFlag_OnlyStarted)
	{
		if (pPlayerData)
		{
			for (i = eaSize(&pPlayerData->eaCompletedAssignments) - 1; i >= 0; i--)
			{
				ItemAssignmentDef *pDef = GET_REF(pPlayerData->eaCompletedAssignments[i]->hDef);
				if (!pDef || !eaSize(&pDef->eaDependencies))
					continue;
				if (pchFilter && *pchFilter && stricmp(pchFilter, pDef->pchName))
					continue;
				ItemAssignment_AddChainToList(pEnt, peaChains, &iCount, pDef, uFlags, false);
			}
		}
	}
	else
	{
		FOR_EACH_IN_REFDICT(g_hItemAssignmentDict, ItemAssignmentDef, pDef);
		{
			if (!eaSize(&pDef->eaDependencies))
				continue;
			if (pchFilter && *pchFilter && stricmp(pchFilter, pDef->pchName))
				continue;
			ItemAssignment_AddChainToList(pEnt, peaChains, &iCount, pDef, uFlags, false);
		}
		FOR_EACH_END;
	}

	eaSetSizeStruct(peaChains, parse_ItemAssignmentUI, iCount);
	eaStableSort((*peaChains), &uFlags, ItemAssignment_OrderChains);
	ui_GenSetManagedListSafe(pGen, peaChains, ItemAssignmentUI, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemAssignmentGetPercentTimeRemaining");
F32 gclItemAssignment_GetPercentTimeRemaining(SA_PARAM_OP_VALID ItemAssignmentUI *pAssignment)
{
	if (pAssignment)
	{
		U32 uTimeCurrent = timeServerSecondsSince2000();
		ItemAssignmentDef *pDef = GET_REF(pAssignment->hDef);
		U32 uDuration = ItemAssignment_GetDuration(pAssignment, pDef);

		if (uDuration)
		{
			return (F32)pAssignment->uTimeRemaining / (F32)uDuration;
		}

		return 1.f;
	}

	return 0.f;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemAssignmentGetAssignmentCategories");
void gclItemAssignment_GetAssignmentCategories(SA_PARAM_NN_VALID UIGen *pGen, U32 uFlags)
{
	static ItemAssignmentCategoryUI **s_eaCategories = NULL;
	Entity* pEnt = entActivePlayerPtr();

	eaClearStruct(&s_eaCategories,parse_ItemAssignmentCategoryUI);

	ItemAssignment_FillAssignmentCategories(pEnt,&s_eaCategories, uFlags);

	ui_GenSetListSafe(pGen, &s_eaCategories, ItemAssignmentCategoryUI);
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemAssignmentGetCategoryDisplayNameTranslated");
const char* gclItemAssignment_GetCategoryDisplayNameTranslated(const char *pchName)
{
	S32 category = StaticDefineIntGetIntDefault(ItemAssignmentCategoryEnum, pchName, -1);

	if (category != -1)
	{
		ItemAssignmentCategorySettings* pCategory = ItemAssignmentCategory_GetSettings(category);
		if (pCategory)
		{
			return TranslateDisplayMessage(pCategory->msgDisplayName);
		}
	}

	return "";
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemAssignmentGetMetaXPForNextRank");
S32 gclItemAssignment_GetMetaXPForNextRank()
{
	Entity* pEnt = entActivePlayerPtr();
	if (pEnt && g_ItemAssignmentSettings.pMetaRankingSchedule && 
		g_ItemAssignmentSettings.pMetaRankingSchedule->pchNumericRank)
	{
		S32 iCurrentRank = inv_GetNumericItemValue(pEnt, g_ItemAssignmentSettings.pMetaRankingSchedule->pchNumericRank);

		return ItemAssignments_GetExperienceThresholdForRank(g_ItemAssignmentSettings.pMetaRankingSchedule, iCurrentRank+1);
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemAssignmentMetaXPPercentThroughRank");
F32 gclItemAssignment_MetaXPPercentThroughRank()
{
	Entity* pEnt = entActivePlayerPtr();
	if (pEnt && g_ItemAssignmentSettings.pMetaRankingSchedule && 
		g_ItemAssignmentSettings.pMetaRankingSchedule->pchNumericXP &&
		g_ItemAssignmentSettings.pMetaRankingSchedule->pchNumericRank)
	{
		S32 iCurrentRank = inv_GetNumericItemValue(pEnt, g_ItemAssignmentSettings.pMetaRankingSchedule->pchNumericRank);
		S32 iCurrentXP = inv_GetNumericItemValue(pEnt, g_ItemAssignmentSettings.pMetaRankingSchedule->pchNumericXP);
		S32 iNextRank = iCurrentRank + 1;

		S32 iXPForCurrentRank, iXPForNextRank;

		iXPForCurrentRank = ItemAssignments_GetExperienceThresholdForRank(g_ItemAssignmentSettings.pMetaRankingSchedule, iCurrentRank);
		iXPForNextRank = ItemAssignments_GetExperienceThresholdForRank(g_ItemAssignmentSettings.pMetaRankingSchedule, iNextRank);

		if (iCurrentXP >= iXPForCurrentRank && 
			iXPForCurrentRank < iXPForNextRank)
		{
			return (F32)(iCurrentXP - iXPForCurrentRank)/(F32)(iXPForNextRank - iXPForCurrentRank); 
		}
	}

	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("IsItemOnActiveItemAssignment");
S32 gclItemAssignment_IsItemOnAssignment(SA_PARAM_OP_VALID Item *pItem)
{
	Entity* pEnt = entActivePlayerPtr();
	ItemAssignmentPersistedData* pPlayerData = SAFE_MEMBER2(pEnt, pPlayer, pItemAssignmentPersistedData);

	if (pItem && pPlayerData)
	{
		FOR_EACH_IN_EARRAY_FORWARDS(pPlayerData->eaActiveAssignments, ItemAssignment, pAssignment)
		{
			FOR_EACH_IN_EARRAY_FORWARDS(pAssignment->eaSlottedItems, ItemAssignmentSlottedItem, pSlot)
			{
				if (pSlot->uItemID == pItem->id)
					return true;
			}
			FOR_EACH_END
		}
		FOR_EACH_END
	}

	return false;
}

MP_DEFINE(ItemAssignmentEquippedUI);

// refreshes the ItemAssignmentEquippedUI for the given bag
static void CacheItemAssignmentUIForBag(Entity *pEnt, 
										ItemAssignmentPersistedData* pPlayerData, 
										InventoryBag *pBag, 
										UICategorizedInventorySlot **ppCategorizedSlots)
{
	static S32 s_cacheFlag = 0;

	s_cacheFlag ++;

	if (pPlayerData)
	{
		// remove all the ItemAssignmentEquippedUI that are associated with the bag
		FOR_EACH_IN_EARRAY(ppCategorizedSlots, UICategorizedInventorySlot, pCategorizedSlot)
		{
			if (pCategorizedSlot->pSlot && pCategorizedSlot->pSlot->pItem)
			{
				// try and find the associated ItemAssignmentEquippedUI for this item
				ItemAssignmentEquippedUI *pEquippedAssignment;
				pEquippedAssignment = FindItemAssignmentEquippedUIForItemID(pCategorizedSlot->pSlot->pItem->id);
				if (pEquippedAssignment)
				{	
					ItemAssignment *pFoundAssignment = NULL;

					pFoundAssignment = ItemAssignments_FindActiveAssignmentWithSlottedItem(pPlayerData, pCategorizedSlot->pSlot->pItem->id);

					if (pFoundAssignment)
					{
						U32 uTimeCurrent = timeServerSecondsSince2000();

						REF_HANDLE_REMOVE(pEquippedAssignment->assignmentUI.hDef);

						FillActiveItemAssignment(	&pEquippedAssignment->assignmentUI, 
							pFoundAssignment, 
							pEnt, 
							pPlayerData, 
							uTimeCurrent);

						pEquippedAssignment->cacheFlag = s_cacheFlag;

						pCategorizedSlot->pAssignmentUI = &pEquippedAssignment->assignmentUI;
					}
				}
				else
				{	// no pEquippedAssignment, we need to make one if we can find an assignment for this item
					ItemAssignment *pFoundAssignment = NULL;

					// todo: need to find if there's a completed assignment
					pFoundAssignment = ItemAssignments_FindActiveAssignmentWithSlottedItem(pPlayerData, pCategorizedSlot->pSlot->pItem->id);

					if (pFoundAssignment)
					{
						U32 uTimeCurrent = timeServerSecondsSince2000();

						MP_CREATE(ItemAssignmentEquippedUI, 16);

						pEquippedAssignment = MP_ALLOC(ItemAssignmentEquippedUI);
						pEquippedAssignment->bagID = pBag->BagID;
						pEquippedAssignment->uid = pCategorizedSlot->pSlot->pItem->id;
						pEquippedAssignment->cacheFlag = s_cacheFlag;

						FillActiveItemAssignment(&pEquippedAssignment->assignmentUI, pFoundAssignment, pEnt, pPlayerData, uTimeCurrent);

						pEquippedAssignment->cacheFlag = s_cacheFlag;

						eaPush(&pIACache->ppItemAssignmentUIs, pEquippedAssignment);

						pCategorizedSlot->pAssignmentUI = &pEquippedAssignment->assignmentUI;
					}

				}
			}

		}
		FOR_EACH_END
	}


	// remove the ones that weren't updated
	FOR_EACH_IN_EARRAY(pIACache->ppItemAssignmentUIs, ItemAssignmentEquippedUI, pItem)
	{
		if (pItem->bagID == pBag->BagID && pItem->cacheFlag != s_cacheFlag)
		{
			eaRemove(&pIACache->ppItemAssignmentUIs, FOR_EACH_IDX(-,pItem));

			REF_HANDLE_REMOVE(pItem->assignmentUI.hDef);

			MP_FREE(ItemAssignmentEquippedUI, pItem);
		}
	}
	FOR_EACH_END
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetItemAssignmentGenInventoryBagGetSlots);
bool GenExprItemAssignmentGenInventoryBagGetSlots(SA_PARAM_NN_VALID UIGen *pGen, 
													SA_PARAM_OP_VALID InventoryBag *pBag, 
													const char *pchPrefix, 
													U32 uOptions)
{
	if (gclInvExprGenInventoryBagGetCategorizedInventory(pGen, pBag, pchPrefix, uOptions))
	{
		Entity* pEnt = entActivePlayerPtr();
		ItemAssignmentPersistedData* pPlayerData = SAFE_MEMBER2(pEnt, pPlayer, pItemAssignmentPersistedData);

		UICategorizedInventorySlot ***peaSlots = ui_GenGetManagedListSafe(pGen, UICategorizedInventorySlot);
		FOR_EACH_IN_EARRAY((*peaSlots), UICategorizedInventorySlot, pSlot)
		{
			pSlot->pAssignmentUI = NULL;
		}
		FOR_EACH_END

			CacheItemAssignmentUIForBag(pEnt, pPlayerData, pBag, (*peaSlots));

		return true;
	}

	return false;
}

// returns the number of items owned by the entity in the appropriate bags 
AUTO_EXPR_FUNC(UIGen, ItemEval, entityutil) ACMD_NAME(ItemAssignmentGetItemCount);
int gclItemAssignment_GetItemCount(SA_PARAM_OP_VALID Entity *pEnt, 
									ACMD_EXPR_RES_DICT(ItemDef) const char *pcItemName, 
									S32 iMaxToFind)
{
	ItemDef *pDef = (ItemDef*)RefSystem_ReferentFromString(g_hItemDict,pcItemName);

	if (!pEnt || !pEnt->pInventoryV2 || !pDef) {
		return false;
	}

	if (iMaxToFind <= 0) 
		iMaxToFind = INT_MAX;

	if (pDef->eType == kItemType_Numeric) {
		return inv_GetNumericItemValue(pEnt, pcItemName);
	} else {
		InvBagFlag eSearchBags = ItemAssignments_GetSearchInvBagFlags();
		return inv_FindItemCountByDefNameEx(pEnt, eSearchBags, pDef->pchName, iMaxToFind);
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemAssignmentHasRequiredItemCount");
bool gclItemAssignment_HasRequiredItemCountSlot(SA_PARAM_OP_VALID InventorySlot *pSlot)
{
	ItemDef *pRequiredDef = pSlot && pSlot->pItem ? GET_REF(pSlot->pItem->hItem) : NULL;
	Entity *pEnt = entActivePlayerPtr();
	if (!pRequiredDef)
		return false;
	if (pRequiredDef->eType == kItemType_Numeric)
		return pSlot->pItem->count <= inv_GetNumericItemValue(pEnt, pRequiredDef->pchName);
	return pSlot->pItem->count <= inv_FindItemCountByDefName(pEnt, pRequiredDef->pchName, pSlot->pItem->count);
}

// Gets the cost for the given assignment to be force completed. 
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ItemAssignmentGetForceCompleteCost");
S32 GenExprItemAssignmentGetForceCompleteCost(U32 uAssignmentID)
{
	Entity *pEnt = entActivePlayerPtr();
		
	if (SAFE_MEMBER2(pEnt, pPlayer, pItemAssignmentPersistedData))
	{
		ItemAssignment* pAssignment = NULL;
		pAssignment = ItemAssignment_FindActiveAssignmentByID(pEnt->pPlayer->pItemAssignmentPersistedData, uAssignmentID);
		if (pAssignment)
		{
			S32 iCost = 0;
			ItemAssignments_GetForceCompleteNumericCost(pEnt, pAssignment, &iCost);
			return iCost;
		}
	}

	return 0;
}


// Gets a list of filtered UICategorizedInventorySlot from multiple bags then further filters out those that are 
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemAssignmentMultiBagGetCategorizedInventory);
bool GenExprItemAssignmentMultiBagGetCategorizedInventory(	SA_PARAM_NN_VALID UIGen *pGen, 
															SA_PARAM_OP_VALID Entity *pEnt, 
															const char *pchBagIds, 
															const char *pchPrefix, 
															const char *pchIncludeCategories, 
															const char *pchExcludeCategories, 
															U32 uOptions)
{
	if (gclInvExprGenInventoryMultiBagGetCategorizedInventory(	pGen, pEnt, pchBagIds, pchPrefix, 
		pchIncludeCategories, pchExcludeCategories, uOptions) )
	{	// filter out any items that are already slotted
		UICategorizedInventorySlot ***peaSlots = ui_GenGetManagedListSafe(pGen, UICategorizedInventorySlot);

		if (eaSize(&pIACache->eaSlots))
		{
			FOR_EACH_IN_EARRAY(*peaSlots, UICategorizedInventorySlot, pSlots)
			{
				if (pSlots->pSlot && pSlots->pSlot->pItem)
				{
					FOR_EACH_IN_EARRAY(pIACache->eaSlots, ItemAssignmentSlotUI, pAssignmentSlot)
					{
						if (pSlots->pSlot->pItem->id == pAssignmentSlot->uItemID)
						{
							eaRemove(peaSlots, FOR_EACH_IDX(-, pSlots));
							break;
						}
					}
					FOR_EACH_END
				}
			}
			FOR_EACH_END
		}

		return true;
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ItemAssignmentSlotGetDestroyChance);
F32 GenExprItemAssignmentSlotGetDestroyChance(SA_PARAM_OP_VALID ItemAssignmentSlotUI *pSlotUI, SA_PARAM_OP_VALID Item *pItem, const char *pchOutcome)
{
	ItemAssignmentDef *pDef;
	ItemAssignmentOutcome *pOutcome;
	ItemDef *pItemDef;

	if (!pSlotUI || !pItem)
		return 0;

	pDef = GET_REF(pSlotUI->hDef);
	if (!pDef)
		return 0;

	pItemDef = GET_REF(pItem->hItem);
	if (!pItemDef)
		return 0;

	pOutcome = ItemAssignment_GetOutcomeByName(pDef, pchOutcome);
	if (!pOutcome)
		return 0;

	if (pOutcome->pResults && ItemAssignments_CheckDestroyRequirements(pItemDef, pOutcome))
		return pOutcome->pResults->fDestroyChance;

	return 0;
}

AUTO_STARTUP(ItemAssignmentUI) ASTRT_DEPS(ItemAssignmentCategories);
void ItemAssignmentUIStartup(void)
{
	ui_GenInitStaticDefineVars(ItemAssignmentCategoryEnum, "ItemAssignmentCategory_");
	ui_GenInitStaticDefineVars(ItemAssignmentFailsRequiresReasonEnum, "ItemAssignmentError_");

	ui_GenInitStaticDefineVars(ItemAssignmentCountFlagsEnum, "ItemAssignmentCount");
	ui_GenInitStaticDefineVars(GetActiveAssignmentFlagsEnum, "ItemAssignment");
	ui_GenInitStaticDefineVars(ItemAssignmentRiskFlagsEnum, "ItemAssignmentRisk");
	ui_GenInitStaticDefineVars(ItemAssignmentRewardsFlagsEnum, "ItemAssignmentRewards_");
	ui_GenInitStaticDefineVars(ItemAssignmentCategoryUIFlagsEnum, "ItemAssignmentCategoryUI");

	// TODO: Put this in enum ItemAssignmentRecommendFlags?
	ui_GenInitIntVar("ItemAssignmentExcludeNewAssignmentSlot", ITEM_ASSIGNMENT_RECOMMEND_EXCLUDE_NEW_SLOT);

	ui_GenInitIntVar("ItemAssignmentSlotExcludeRequired", ITEM_ASSIGNMENT_SLOTOPTION_EXCLUDE_REQUIRED);
	ui_GenInitIntVar("ItemAssignmentSlotExcludeOptional", ITEM_ASSIGNMENT_SLOTOPTION_EXCLUDE_OPTIONAL);

	ui_GenInitIntVar("ItemAssignmentRequirementsExcludeNumeric", ITEM_ASSIGNMENT_REQUIRED_ITEMS_EXLUDE_NUMERIC);


	ui_GenInitStaticDefineVars(ItemAssignmentChainFlagsEnum, "ItemAssignmentChain");
}

#include "ItemAssignmentsUI_c_ast.c"