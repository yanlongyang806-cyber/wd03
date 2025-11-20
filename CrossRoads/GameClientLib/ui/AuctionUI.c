#include "earray.h"
#include "estring.h"
#include "UIGen.h"
#include "gclEntity.h"

#include "entCritter.h"
#include "Entity.h"
#include "EntitySavedData.h"
#include "Player.h"

#include "AuctionLot.h"
#include "AuctionLot_Transact.h"

#include "inventoryCommon.h"
#include "NotifyCommon.h"
#include "GameAccountDataCommon.h"
#include "GameStringFormat.h"
#include "GlobalTypes.h"
#include "SavedPetCommon.h"
#include "StringCache.h"
#include "GamePermissionsCommon.h"
#include "mission_common.h"
#include "contact_common.h"
#include "AttribMod.h"
#include "AttribMod_h_ast.h"

#include "FCInventoryUI.h"
#include "GameStringFormat.h"

#include "MailCommon.h"

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "AutoGen/AuctionLot_h_ast.h"
#include "AutoGen/AuctionLotEnums_h_ast.h"
#include "AutoGen/AuctionUI_c_ast.h"

// TODO (aames): Go through and remove all the "DeleteMe_" functions.
// These come from a large data/code combined check-in, which stops using a lot of
// old expression functions, but Mike P needed to keep placeholder versions of them to
// avoid causing a slew of error messages during the check-in process.

// If this flag is set, the search results are cleared in the next model function call.
static bool s_bClearSearchResultsNextTick = false;
static bool s_bClearPlayerBidResultsNextTick = false;

static StashTable stCachedPriceHistories = NULL;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

#define DEFAULT_AUCTIONS_FOR_PLAYER_PAGE_SIZE 100

//minimum delay between requests to refresh the "average recent sale price" for an itemdef
#define AUCTION_UI_PRICE_CHECK_COOLDOWN 60

AUTO_STRUCT;
typedef struct QualityEntry
{
	int iQuality;
	REF_TO(Message) hMessage;
	REF_TO(Message) hSimpleMessage;
} QualityEntry;

AUTO_STRUCT;
typedef struct RestrictCategoryEntry
{
	int iValue;
	const char* messageKey; 
} RestrictCategoryEntry;

static QualityEntry **s_ppQualities;
static RestrictCategoryEntry **s_ppRestrictCategories;
static AuctionLotList* s_FilteredLots;
static AuctionLotList *s_pAuctionLotList; // Search results
static AuctionLotList *s_pAuctionBrokerList; // Broker results
static AuctionLotList *s_pPlayerAuctionLotList; // Auctions owned by player
static AuctionLotList *s_pPlayerAuctionLotBidList; // Auctions bid by player

static bool s_bUpdatePendingLot;
static NOCONST(AuctionLot) *s_pNextPendingLot; // Lot we're constructing on the client
static NOCONST(AuctionLot) *s_pPendingLot; // Lot currently being displayed
static ItemSortType **s_ppSortTypes;
static AuctionSearchRequest *s_pSearchRequest;
static bool s_bDebugAuctionUI = false;
static U32 s_uiCooldown = 0;
static bool s_bWaitingOnSearch = false;
static char *s_pcSearchDescription = NULL;

// Run the UIGen system in debug mode.
AUTO_CMD_INT(s_bDebugAuctionUI, DebugAuctionUI) ACMD_CATEGORY(Debug) ACMD_CMDLINE;

AUTO_RUN;
void AuctionUI_Init(void)
{
	ui_GenInitStaticDefineVars(AuctionSortColumnEnum, "AuctionSort_");
}

void AuctionUI_OncePerFrame(void)
{
	if (s_bUpdatePendingLot)
	{
		StructDestroyNoConst(parse_AuctionLot, s_pPendingLot);
		s_pPendingLot = s_pNextPendingLot;
		s_pNextPendingLot = NULL;
		s_bUpdatePendingLot = false;
	}
}

void AuctionUI_Reset(void)
{
	StructDestroyNoConst(parse_AuctionLot, s_pPendingLot);
	s_pPendingLot = NULL;
	s_pNextPendingLot = NULL;
	s_bUpdatePendingLot = false;
}

static void AuctionListPageCopy(AuctionLot ***peaLots, AuctionLotList **ppAuctionList, int iStart, int iPageSize)
{
	AuctionLotList *pAuctionList = *ppAuctionList;
	if (s_bClearSearchResultsNextTick && ppAuctionList == &s_pAuctionLotList)
	{
		StructDestroySafe(parse_AuctionLotList, ppAuctionList);
		pAuctionList = NULL;
		s_bClearSearchResultsNextTick = false;
	}
	else if (s_bClearPlayerBidResultsNextTick && ppAuctionList == &s_pPlayerAuctionLotBidList)
	{
		StructDestroySafe(parse_AuctionLotList, ppAuctionList);
		pAuctionList = NULL;
		s_bClearPlayerBidResultsNextTick = false;
	}

	if (pAuctionList) {
		int iUsed = 0;
		int i;
		for (i = iStart; iUsed < iPageSize && i < eaSize(&pAuctionList->ppAuctionLots); i++)
		{
			if (eaSize(peaLots) == iUsed)
			{
				eaPush(peaLots, pAuctionList->ppAuctionLots[i]);
			}
			else if (iUsed < eaSize(peaLots))
			{
				(*peaLots)[iUsed] = pAuctionList->ppAuctionLots[i];
			}
			iUsed++;
		}
		
		while (eaSize(peaLots) > iUsed)
		{
			eaPop(peaLots);
		}
	} else {
		eaClear(peaLots);
	}
}

AUTO_STRUCT;
typedef struct AuctionSortType
{
	U32 iValue;
	const char *pcMessageKey;
} AuctionSortType;

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetSortList);
void exprAuctionGetSortList(SA_PARAM_NN_VALID UIGen *pGen)
{
	static AuctionSortType **eaSortTypes = NULL;
	S32 i;
	if (!eaSortTypes) {
		for (i = 0; i < AuctionSort_Count; i++) {
			AuctionSortType *pSortType = StructCreate(parse_AuctionSortType);
			Message *pMessage = StaticDefineGetMessage(AuctionSortColumnEnum, i);
			pSortType->iValue = i;
			pSortType->pcMessageKey = pMessage ? pMessage->pcMessageKey : NULL;
			eaPush(&eaSortTypes, pSortType);
		}
	}
	
	ui_GenSetList(pGen, &eaSortTypes, parse_AuctionSortType);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetRestrictCategoriesList);
void exprAuctionGetRestrictCategoriesList(SA_PARAM_NN_VALID UIGen *pGen)
{
	if (!s_ppRestrictCategories)
	{
		S32 *values = NULL;
		int i, n;
		RestrictCategoryEntry *entry;
		Entity* pEnt = entActivePlayerPtr();

		DefineFillAllKeysAndValues(UsageRestrictionCategoryEnum, NULL, &values);

		n = eaiSize(&values);

		for ( i = 0; i < n; i++ )
		{
			Message *message;
			entry = StructCreate(parse_RestrictCategoryEntry);
			entry->iValue = values[i];
			message = UsageRestriction_GetCategoryMessage(pEnt, entry->iValue);
			if ( message != NULL )
			{
				entry->messageKey = message->pcMessageKey;
			}

			eaPush(&s_ppRestrictCategories, entry);
		}
		eaiDestroy(&values);
	}

	ui_GenSetList(pGen, &s_ppRestrictCategories, parse_RestrictCategoryEntry);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetQualityList);
void exprAuctionGetQualityList(SA_PARAM_NN_VALID UIGen *pGen)
{
	if (!s_ppQualities)
	{
		int i;
		QualityEntry *pNewEntry;
		
		for (i = kItemQuality_None; i < eaSize(&g_ItemQualities.ppQualities); i++)
		{
			char messageName[1024];

			if (i >= 0 && (g_ItemQualities.ppQualities[i]->flags & kItemQualityFlag_HideFromUILists))
				continue;

			pNewEntry = StructCreate(parse_QualityEntry);
			pNewEntry->iQuality = i;
			if (i == kItemQuality_None)
			{
				SET_HANDLE_FROM_STRING("Message", "Auction.DefaultQualityFormatted", pNewEntry->hMessage);
				SET_HANDLE_FROM_STRING("Message", "Auction.DefaultQuality", pNewEntry->hSimpleMessage);
			}
			else
			{
				sprintf(messageName, "Item.Quality.Formatted.%d", i); // From ItemMessages.ms
				SET_HANDLE_FROM_STRING("Message", messageName, pNewEntry->hMessage);
				sprintf(messageName, "Item.Quality.%d", i); // From ItemMessages.ms
				SET_HANDLE_FROM_STRING("Message", messageName, pNewEntry->hSimpleMessage);
			}
			eaPush(&s_ppQualities, pNewEntry);
		}		
	}
	ui_GenSetList(pGen, &s_ppQualities, parse_QualityEntry);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetSortTypeList);
void exprAuctionGetSortTypeList(SA_PARAM_NN_VALID UIGen *pGen)
{
	if (!s_ppSortTypes)
	{
		item_GetSearchableSortTypes(&s_ppSortTypes);
	}
	ui_GenSetList(pGen, &s_ppSortTypes, parse_ItemSortType);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetSortTypeCategoryList);
void exprAuctionGetSortTypeCategoryList(SA_PARAM_NN_VALID UIGen *pGen)
{
	ui_GenSetList(pGen, &gSortTypes.ppItemSortTypeCategories, parse_ItemSortTypeCategory);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetSortTypeListByCategory);
void exprAuctionGetSortTypeListbyCategory(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID ItemSortTypeCategory *pCategory)
{
	ItemSortType ***peaSortTypeList = ui_GenGetManagedListSafe(pGen, ItemSortType);
	S32 iCount = 0;

	if (pCategory && eaiSize(&pCategory->eaiItemSortTypes) > 0)
	{
		S32 i;
		for (i = 0; i < eaiSize(&pCategory->eaiItemSortTypes); i++)
		{
			S32 iItemSortTypeIndex;
			if ((iItemSortTypeIndex = eaIndexedFindUsingInt(&gSortTypes.ppIndexedItemSortTypes, pCategory->eaiItemSortTypes[i])) >= 0)
			{
				ItemSortType *pItemSortType = eaGetStruct(peaSortTypeList, parse_ItemSortType, iCount++);
				StructCopyAll(parse_ItemSortType, gSortTypes.ppIndexedItemSortTypes[iItemSortTypeIndex], pItemSortType);
			}
		}
	}

	eaSetSizeStruct(peaSortTypeList, parse_ItemSortType, iCount);

	ui_GenSetManagedListSafe(pGen, peaSortTypeList, ItemSortType, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetSortTypeListByCategorySize);
S32 exprAuctionGetSortTypeListbyCategorySize(SA_PARAM_OP_VALID ItemSortTypeCategory *pCategory)
{
	S32 i, iCount = 0;

	if (pCategory)
	{
		for (i = 0; i < eaiSize(&pCategory->eaiItemSortTypes); i++)
		{
			if (eaIndexedFindUsingInt(&gSortTypes.ppIndexedItemSortTypes, pCategory->eaiItemSortTypes[i]) >= 0)
				iCount++;
		}
	}

	return iCount;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionSetSearchPageSize);
void DeleteMe_exprAuctionSetSearchPageSize(int iPageSize)
{
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetSearchPageSize);
U32 DeleteMe_exprAuctionGetSearchPageSize(void)
{
	return 10;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetSearchResults);
void DeleteMe_exprAuctionGetSearchResults(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_NN_VALID UIGen *pGen, int iStart)
{
	AuctionLot ***peaLots = ui_GenGetManagedListSafe(pGen, AuctionLot);
	
	if (iStart < 0) {
		iStart = 0;
	}
	AuctionListPageCopy(peaLots, &s_pAuctionLotList, iStart, 10);
	
	ui_GenSetManagedListSafe(pGen, peaLots, AuctionLot, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetBuyList);
void exprAuctionGetBuyList(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_NN_VALID UIGen *pGen, int iStart, int iSize)
{
	AuctionLot ***peaLots = ui_GenGetManagedListSafe(pGen, AuctionLot);
	
	if (iStart < 0) {
		iStart = 0;
	}
	AuctionListPageCopy(peaLots, &s_pAuctionLotList, iStart, iSize);

	ui_GenSetManagedListSafe(pGen, peaLots, AuctionLot, false);
}

bool AuctionListFilter(AuctionLotList* pDst, AuctionLotList* pSrc, bool bUsableItemsOnly, int iFramesWaiting)
{
	bool bAllItemDefsReceived = true;
	if (pDst && pSrc)
	{
		Entity* pEnt = entActivePlayerPtr();
		int i;
		for (i = 0; i < eaSize(&pSrc->ppAuctionLots); i++)
		{
			bool bValid = true;
			if (bUsableItemsOnly)
			{
				int j;
				for (j = 0; j < eaSize(&pSrc->ppAuctionLots[i]->ppItemsV2); j++)
				{
					ItemDef* pDef = SAFE_GET_REF2(pSrc->ppAuctionLots[i]->ppItemsV2[j], slot.pItem, hItem);
					
					if (!pDef)
					{
						if (iFramesWaiting > 100 && pSrc->ppAuctionLots[i]->ppItemsV2[j] && pSrc->ppAuctionLots[i]->ppItemsV2[j]->slot.pItem)
						{
							ErrorDetailsf("Missing Def name: %s", REF_STRING_FROM_HANDLE(pSrc->ppAuctionLots[i]->ppItemsV2[j]->slot.pItem->hItem));
							Errorf("Auction UI spent more than 100 frames waiting for an item def to be received from the server. This may be a sign that there are auctionlots still active whose defs have been removed from the game. Auction UI performance will be slow until this is fixed.");
						}
						bAllItemDefsReceived = false;
					}
					
					if (pDef && !itemdef_VerifyUsageRestrictions(entGetPartitionIdx(pEnt), pEnt, pDef, 0, NULL, -1))
					{
						bValid = false;
						break;
					}
				}
			}
			if (!bValid)
				continue;

			eaPush(&pDst->ppAuctionLots, pSrc->ppAuctionLots[i]);
		}
	}
	return bAllItemDefsReceived;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetFilteredBuyList);
void exprAuctionGetFilteredBuyList(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_NN_VALID UIGen *pGen, int iStart, int iSize, bool bUsableItemsOnly)
{
	AuctionLot ***peaLots = ui_GenGetManagedListSafe(pGen, AuctionLot);
	static bool s_bAllItemsReceived = true;
	static int s_iFramesWaitingForItemDefs = 0;
	static bool bLastFrameUsableOnly = false;

	if (iStart < 0) {
		iStart = 0;
	}

	if (s_FilteredLots && (s_bClearSearchResultsNextTick || (bLastFrameUsableOnly != bUsableItemsOnly)))
	{
		eaClear(&s_FilteredLots->ppAuctionLots);
		StructDestroy(parse_AuctionLotList, s_FilteredLots);
		s_FilteredLots = NULL;
	}

	if (s_bClearSearchResultsNextTick)
	{
		StructDestroySafe(parse_AuctionLotList, &s_pAuctionLotList);
		s_pAuctionLotList = NULL;
		eaClear(peaLots);
		s_bClearSearchResultsNextTick = false;
		s_iFramesWaitingForItemDefs = 0;
	}

	bLastFrameUsableOnly = bUsableItemsOnly;

	if ((!s_FilteredLots || !s_bAllItemsReceived) && s_pAuctionLotList && eaSize(&s_pAuctionLotList->ppAuctionLots) > 0)
	{
		if (!s_FilteredLots)
			s_FilteredLots = StructCreate(parse_AuctionLotList);
		else
			eaClear(&s_FilteredLots->ppAuctionLots);

		s_iFramesWaitingForItemDefs++;

		s_bAllItemsReceived = AuctionListFilter(s_FilteredLots, s_pAuctionLotList, bUsableItemsOnly, s_iFramesWaitingForItemDefs);
	}

	AuctionListPageCopy(peaLots, &s_FilteredLots, iStart, iSize);

	ui_GenSetManagedListSafe(pGen, peaLots, AuctionLot, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetSellList);
void exprAuctionGetSellList(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_NN_VALID UIGen *pGen, int iStart, int iSize)
{
	AuctionLot ***peaLots = ui_GenGetManagedListSafe(pGen, AuctionLot);
	
	if (iStart < 0) {
		iStart = 0;
	}

	AuctionListPageCopy(peaLots, &s_pPlayerAuctionLotList, iStart, iSize);
	
	ui_GenSetManagedListSafe(pGen, peaLots, AuctionLot, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetBidList);
void exprAuctionGetBidList(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_NN_VALID UIGen *pGen, int iStart, int iSize)
{
	AuctionLot ***peaLots = ui_GenGetManagedListSafe(pGen, AuctionLot);

	if (iStart < 0) {
		iStart = 0;
	}

	AuctionListPageCopy(peaLots, &s_pPlayerAuctionLotBidList, iStart, iSize);

	ui_GenSetManagedListSafe(pGen, peaLots, AuctionLot, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetBuyListSize);
U32 exprAuctionGetBuyListSize(void)
{
	return s_pAuctionLotList ? eaSize(&s_pAuctionLotList->ppAuctionLots) : 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetFilteredBuyListSize);
U32 exprAuctionGetFilteredBuyListSize(void)
{
	return s_FilteredLots ? eaSize(&s_FilteredLots->ppAuctionLots) : exprAuctionGetBuyListSize();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetSearchResultCount);
U32 DeleteMe_exprAuctionGetSearchResultCount(void)
{
	return exprAuctionGetBuyListSize();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetSellListSize);
U32 exprAuctionGetSellListSize(void)
{
	return s_pPlayerAuctionLotList ? eaSize(&s_pPlayerAuctionLotList->ppAuctionLots) : 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetBidListSize);
U32 exprAuctionGetBidListSize(void)
{
	return s_pPlayerAuctionLotBidList ? eaSize(&s_pPlayerAuctionLotBidList->ppAuctionLots) : 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetSearchPageResultCount);
U32 DeleteMe_exprAuctionGetSearchPageResultCount(int searchListStart)
{
	return s_pAuctionLotList ? min(eaSize(&s_pAuctionLotList->ppAuctionLots) - searchListStart, 10) : 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetMaxSearchResultCount);
U32 DeleteMe_exprAuctionGetMaxSearchResultCount(void)
{
	return s_pAuctionLotList ? max(1, eaSize(&s_pAuctionLotList->ppAuctionLots)) : 1;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetSearchResultHitCount);
U32 DeleteMe_exprAuctionGetSearchResultHitCount(void)
{
	return s_pAuctionLotList ? s_pAuctionLotList->hitcount : 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetSearchNextStart);
U32 DeleteMe_exprAuctionGetSearchNextStart(void)
{
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetSearchWaiting);
bool exprAuctionGetSearchWaiting(void)
{
	return s_bWaitingOnSearch;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetSearchCooldown);
U32 exprAuctionGetSearchCooldown(void)
{
	U32 now = timeServerSecondsSince2000();
	return (s_uiCooldown == 0 || s_uiCooldown <= now) ? 0 : s_uiCooldown - now;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetSearchMore);
bool DeleteMe_exprAuctionGetSearchMore(void)
{
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetSearchSummary);
char* exprAuctionGetSearchSummary(void)
{
	return s_pcSearchDescription;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionClearSearchBuffer);
void exprAuctionClearSearchBuffer(void)
{
	s_bClearSearchResultsNextTick = true;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE ACMD_CATEGORY(Auction);
void gclAuctionClearSearchResults(void)
{
	exprAuctionClearSearchBuffer();
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE ACMD_CATEGORY(Auction);
void gclAuctionClearPlayerBidResults(void)
{
	s_bClearPlayerBidResultsNextTick = true;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE ACMD_CATEGORY(Auction);
void gclAuctionShowSearchResults(AuctionLotList *pAuctionList)
{
	if (pAuctionList) {
		s_bClearSearchResultsNextTick = false;
		if (!s_pAuctionLotList) {
			s_pAuctionLotList = StructClone(parse_AuctionLotList, pAuctionList);
		} else {
			StructCopy(parse_AuctionLotList, pAuctionList, s_pAuctionLotList, 0, 0, 0);
		}
		printf("Time Taken: %f\n", pAuctionList->timeTaken);
		s_uiCooldown = pAuctionList->cooldownEnd;
	}
	s_bWaitingOnSearch = false;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE ACMD_CATEGORY(Auction);
void gclAuctionBrokerShowSearchResults(AuctionLotList *pAuctionList)
{
	if (pAuctionList) {
		s_bClearSearchResultsNextTick = false;
		if (!s_pAuctionBrokerList) {
			s_pAuctionBrokerList = StructClone(parse_AuctionLotList, pAuctionList);
		} else {
			StructCopy(parse_AuctionLotList, pAuctionList, s_pAuctionBrokerList, 0, 0, 0);
		}
		printf("Time Taken: %f\n", pAuctionList->timeTaken);
		s_uiCooldown = pAuctionList->cooldownEnd;
	}
	s_bWaitingOnSearch = false;
}

static void buildSearchSummaryString(void)
{
	ItemSortType *sortType;
	int i, iQualityIndex = -1;
	
	if (!s_pSearchRequest) {
		s_pSearchRequest = StructCreate(parse_AuctionSearchRequest);
	}
	
	sortType = item_GetSortTypeForID(s_pSearchRequest->sortType);
	if (!sortType)
		return;
	
	if (s_pcSearchDescription) {
		estrDestroy(&s_pcSearchDescription);
	}

	if (s_pSearchRequest->itemQuality >= 0)
	{
		// Since we are no longer guaranteed to have a contiguous set of quality values,
		// we have to search to find the index of the quality entry we are looking for
		for (i = 0; i < eaSize(&s_ppQualities); i++)
		{
			if (s_ppQualities[i]->iQuality == s_pSearchRequest->itemQuality)
			{
				iQualityIndex = i;
				break;
			}
		}

		devassertmsg(iQualityIndex >= 0, "AuctionUI attempted to search by an Item Quality that does not exist");
	}

	if ( s_pSearchRequest->maxUsageRestrictionCategory > UsageRestrictionCategory_None )
	{
		Entity* pEnt = entActivePlayerPtr();
		Message * restrictMessage = UsageRestriction_GetCategoryMessage(pEnt, s_pSearchRequest->maxUsageRestrictionCategory);
		if (s_pSearchRequest->maxLevel != 0 || s_pSearchRequest->minLevel != 0)
		{
			if (s_pSearchRequest->stringSearch && s_pSearchRequest->stringSearch[0])
			{
				FormatGameMessageKey(&s_pcSearchDescription,"Auction.SearchWithCategoryQualityLevelRestrictName", 
					STRFMT_INT("ItemMinLevel",s_pSearchRequest->minLevel), STRFMT_INT("ItemMaxLevel",s_pSearchRequest->maxLevel),
					STRFMT_STRING("ItemName",s_pSearchRequest->stringSearch),
					STRFMT_MESSAGEKEY("ItemQuality",s_pSearchRequest->itemQuality >= 0 ? REF_STRING_FROM_HANDLE(s_ppQualities[iQualityIndex]->hSimpleMessage) :  "Auction.SearchQualityAny"),
					STRFMT_MESSAGEREF("ItemType",sortType->hNameMsg),
					STRFMT_MESSAGE("ItemRestrict", restrictMessage),
					STRFMT_END);
			}
			else
			{
				FormatGameMessageKey(&s_pcSearchDescription,"Auction.SearchWithCategoryQualityLevelRestrict", 
					STRFMT_INT("ItemMinLevel",s_pSearchRequest->minLevel), STRFMT_INT("ItemMaxLevel",s_pSearchRequest->maxLevel),
					STRFMT_MESSAGEKEY("ItemQuality",s_pSearchRequest->itemQuality >= 0 ? REF_STRING_FROM_HANDLE(s_ppQualities[iQualityIndex]->hSimpleMessage) :  "Auction.SearchQualityAny"),
					STRFMT_MESSAGEREF("ItemType",sortType->hNameMsg),
					STRFMT_MESSAGE("ItemRestrict", restrictMessage),
					STRFMT_END);
			}
		}
		else
		{
			if (s_pSearchRequest->stringSearch && s_pSearchRequest->stringSearch[0])
			{
				FormatGameMessageKey(&s_pcSearchDescription,"Auction.SearchWithCategoryQualityRestrictName", 
					STRFMT_STRING("ItemName",s_pSearchRequest->stringSearch),
					STRFMT_MESSAGEKEY("ItemQuality",s_pSearchRequest->itemQuality >= 0 ? REF_STRING_FROM_HANDLE(s_ppQualities[iQualityIndex]->hSimpleMessage) :  "Auction.SearchQualityAny"),
					STRFMT_MESSAGEREF("ItemType",sortType->hNameMsg),
					STRFMT_MESSAGE("ItemRestrict", restrictMessage),
					STRFMT_END);
			}
			else
			{
				FormatGameMessageKey(&s_pcSearchDescription,"Auction.SearchWithCategoryQualityRestrict", 
					STRFMT_MESSAGEKEY("ItemQuality",s_pSearchRequest->itemQuality >= 0 ? REF_STRING_FROM_HANDLE(s_ppQualities[iQualityIndex]->hSimpleMessage) :  "Auction.SearchQualityAny"),
					STRFMT_MESSAGEREF("ItemType",sortType->hNameMsg),
					STRFMT_MESSAGE("ItemRestrict", restrictMessage),
					STRFMT_END);
			}
		}
	}
	else
	{
		if (s_pSearchRequest->maxLevel != 0 || s_pSearchRequest->minLevel != 0)
		{
			if (s_pSearchRequest->stringSearch && s_pSearchRequest->stringSearch[0])
			{
				FormatGameMessageKey(&s_pcSearchDescription,"Auction.SearchWithCategoryQualityLevelName", 
					STRFMT_INT("ItemMinLevel",s_pSearchRequest->minLevel), STRFMT_INT("ItemMaxLevel",s_pSearchRequest->maxLevel),
					STRFMT_STRING("ItemName",s_pSearchRequest->stringSearch),
					STRFMT_MESSAGEKEY("ItemQuality",s_pSearchRequest->itemQuality >= 0 ? REF_STRING_FROM_HANDLE(s_ppQualities[iQualityIndex]->hSimpleMessage) :  "Auction.SearchQualityAny"),
					STRFMT_MESSAGEREF("ItemType",sortType->hNameMsg),
					STRFMT_END);
			}
			else
			{
				FormatGameMessageKey(&s_pcSearchDescription,"Auction.SearchWithCategoryQualityLevel", 
					STRFMT_INT("ItemMinLevel",s_pSearchRequest->minLevel), STRFMT_INT("ItemMaxLevel",s_pSearchRequest->maxLevel),
					STRFMT_MESSAGEKEY("ItemQuality",s_pSearchRequest->itemQuality >= 0 ? REF_STRING_FROM_HANDLE(s_ppQualities[iQualityIndex]->hSimpleMessage) :  "Auction.SearchQualityAny"),
					STRFMT_MESSAGEREF("ItemType",sortType->hNameMsg),
					STRFMT_END);
			}
		}
		else
		{
			if (s_pSearchRequest->stringSearch && s_pSearchRequest->stringSearch[0])
			{
				FormatGameMessageKey(&s_pcSearchDescription,"Auction.SearchWithCategoryQualityName", 
					STRFMT_STRING("ItemName",s_pSearchRequest->stringSearch),
					STRFMT_MESSAGEKEY("ItemQuality",s_pSearchRequest->itemQuality >= 0 ? REF_STRING_FROM_HANDLE(s_ppQualities[iQualityIndex]->hSimpleMessage) :  "Auction.SearchQualityAny"),
					STRFMT_MESSAGEREF("ItemType",sortType->hNameMsg),
					STRFMT_END);
			}
			else
			{
				FormatGameMessageKey(&s_pcSearchDescription,"Auction.SearchWithCategoryQuality", 
					STRFMT_MESSAGEKEY("ItemQuality",s_pSearchRequest->itemQuality >= 0 ? REF_STRING_FROM_HANDLE(s_ppQualities[iQualityIndex]->hSimpleMessage) :  "Auction.SearchQualityAny"),
					STRFMT_MESSAGEREF("ItemType",sortType->hNameMsg),
					STRFMT_END);
			}
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionRequestSortedSearchEx);
void exprAuctionRequestSortedSearchEx(SA_PARAM_OP_VALID Entity *pEnt, int minLevel, int maxLevel, int quality, int itemSortType, const char *searchText, int minUsageRestrictionCategory, int maxUsageRestrictionCategory, int sortColumn, SA_PARAM_OP_STR const char *pchItemSortTypeCategoryName)
{
	ItemSortType *pSortType = item_GetSortTypeForID(itemSortType);
	
	if (!s_pSearchRequest) {
		s_pSearchRequest = StructCreate(parse_AuctionSearchRequest);
	}
	else
		StructInit(parse_AuctionSearchRequest, s_pSearchRequest);
	
	if (pSortType && pSortType->bLevelIgnored)
	{
		minLevel = maxLevel = 0;
	}
	s_pSearchRequest->minLevel = minLevel;
	s_pSearchRequest->maxLevel = maxLevel;
	s_pSearchRequest->itemQuality = quality;
	s_pSearchRequest->minUsageRestrictionCategory = minUsageRestrictionCategory;
	s_pSearchRequest->maxUsageRestrictionCategory = maxUsageRestrictionCategory;
	s_pSearchRequest->sortType = itemSortType;
	if (itemSortType == 0 && pchItemSortTypeCategoryName && pchItemSortTypeCategoryName[0])
	{
		s_pSearchRequest->pchItemSortTypeCategoryName = allocAddString(pchItemSortTypeCategoryName);
	}
	s_pSearchRequest->eSortColumn = sortColumn;
	SAFE_FREE(s_pSearchRequest->stringSearch);
	s_pSearchRequest->stringSearch = StructAllocString(searchText);
	
	buildSearchSummaryString();
	
	ServerCmd_gslAuction_GetLotsForSearch(s_pSearchRequest);
	s_bWaitingOnSearch = true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionRequestAdvancedSearchEx);
void exprAuctionRequestAdvancedSearchEx(int minLevel, int maxLevel, int quality, int itemSortType, const char *searchText, int sortColumn, SA_PARAM_OP_STR const char *pchItemSortTypeCategoryName, U32 uiNumGemSlots, S32 eGemType, const char* pchClass, const char* pchAttribs)
{
	ItemSortType *pSortType = item_GetSortTypeForID(itemSortType);
	char* context = NULL;
	char* pchString = strdup(pchAttribs);
	char* pTok = strtok_s(pchString, " ", &context);
	

	if (!s_pSearchRequest) {
		s_pSearchRequest = StructCreate(parse_AuctionSearchRequest);
	}
	else
		StructInit(parse_AuctionSearchRequest, s_pSearchRequest);

	if (pSortType && pSortType->bLevelIgnored)
	{
		minLevel = maxLevel = 0;
	}
	s_pSearchRequest->minLevel = minLevel;
	s_pSearchRequest->maxLevel = maxLevel;
	s_pSearchRequest->itemQuality = quality;
	s_pSearchRequest->sortType = itemSortType;
	s_pSearchRequest->pchRequiredClass = pchClass;
	s_pSearchRequest->uiNumGemSlots = uiNumGemSlots;
	s_pSearchRequest->eRequiredGemType = eGemType;

	while(pTok)
	{
		AttribType eType = StaticDefineInt_FastStringToInt(AttribTypeEnum, pTok, 0);

		if (eType > 0)
			eaiPush(&s_pSearchRequest->eaiAttribs, eType);

		pTok = strtok_s(NULL, " ", &context);
	}

	if (itemSortType == 0 && pchItemSortTypeCategoryName && pchItemSortTypeCategoryName[0])
	{
		s_pSearchRequest->pchItemSortTypeCategoryName = allocAddString(pchItemSortTypeCategoryName);
	}
	SAFE_FREE(s_pSearchRequest->stringSearch);
	s_pSearchRequest->stringSearch = StructAllocString(searchText);

	buildSearchSummaryString();

	ServerCmd_gslAuction_GetLotsForSearch(s_pSearchRequest);
	s_bWaitingOnSearch = true;
	free(pchString);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionRequestAdvancedPetSearchEx);
void exprAuctionRequestAdvancedPetSearchEx(int minLevel, int maxLevel, int quality, int itemSortType, const char *searchText, int sortColumn, SA_PARAM_OP_STR const char *pchItemSortTypeCategoryName, U32 uiNumGemSlots, S32 eGemType)
{
	ItemSortType *pSortType = item_GetSortTypeForID(itemSortType);

	if (!s_pSearchRequest) {
		s_pSearchRequest = StructCreate(parse_AuctionSearchRequest);
	}
	else
		StructInit(parse_AuctionSearchRequest, s_pSearchRequest);

	if (pSortType && pSortType->bLevelIgnored)
	{
		minLevel = maxLevel = 0;
	}
	s_pSearchRequest->itemQuality = quality;
	s_pSearchRequest->sortType = itemSortType;
	s_pSearchRequest->uiNumGemSlots = uiNumGemSlots;
	s_pSearchRequest->eRequiredGemType = eGemType;
	s_pSearchRequest->uiPetMinLevel = minLevel;
	s_pSearchRequest->uiPetMaxLevel = maxLevel;

	if (itemSortType == 0 && pchItemSortTypeCategoryName && pchItemSortTypeCategoryName[0])
	{
		s_pSearchRequest->pchItemSortTypeCategoryName = allocAddString(pchItemSortTypeCategoryName);
	}
	SAFE_FREE(s_pSearchRequest->stringSearch);
	s_pSearchRequest->stringSearch = StructAllocString(searchText);

	buildSearchSummaryString();

	ServerCmd_gslAuction_GetLotsForSearch(s_pSearchRequest);
	s_bWaitingOnSearch = true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionRequestAuctionBrokerSearch);
void exprAuctionRequestAuctionBrokerSearch(S32 iSortColumn)
{
	ServerCmd_gslAuction_GetLotsForAuctionBroker(iSortColumn);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionRequestSortedSearch);
void exprAuctionRequestSortedSearch(SA_PARAM_OP_VALID Entity *pEnt, int minLevel, int maxLevel, int quality, int itemSortType, const char *searchText, int minUsageRestrictionCategory, int maxUsageRestrictionCategory, int sortColumn)
{
	exprAuctionRequestSortedSearchEx(pEnt, minLevel, maxLevel, quality, itemSortType, searchText, minUsageRestrictionCategory, maxUsageRestrictionCategory, sortColumn, NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionRequestSearch);
void DeleteMe_exprAuctionRequestSearch(SA_PARAM_OP_VALID Entity *pEnt, int minLevel, int maxLevel, int quality, int itemSortType, const char *searchText, int minUsageRestrictionCategory, int maxUsageRestrictionCategory)
{
	exprAuctionRequestSortedSearch(pEnt, minLevel, maxLevel, quality, itemSortType, searchText, minUsageRestrictionCategory, maxUsageRestrictionCategory, AuctionSort_Price);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionRequestSearchFrom);
void DeleteMe_exprAuctionRequestSearchFrom(SA_PARAM_OP_VALID Entity *pEnt, int minLevel, int maxLevel, int quality, int itemSortType, const char *searchText, int start)
{
	ItemSortType *pSortType = item_GetSortTypeForID(itemSortType);
	
	if (!s_pSearchRequest) {
		s_pSearchRequest = StructCreate(parse_AuctionSearchRequest);
	}
	else
		StructInit(parse_AuctionSearchRequest, s_pSearchRequest);
	
	if (pSortType && pSortType->bLevelIgnored)
	{
		minLevel = maxLevel = 0;
	}
	
	s_pSearchRequest->minLevel = minLevel;
	s_pSearchRequest->maxLevel = maxLevel;
	s_pSearchRequest->itemQuality = quality;
	s_pSearchRequest->sortType = itemSortType;
	SAFE_FREE(s_pSearchRequest->stringSearch);
	s_pSearchRequest->stringSearch = StructAllocString(searchText);
	
	buildSearchSummaryString();
	
	ServerCmd_gslAuction_GetLotsForSearch(s_pSearchRequest);
	s_bWaitingOnSearch = true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionRequestSearchNext);
bool DeleteMe_exprAuctionRequestSearchNext(SA_PARAM_OP_VALID Entity *pEnt)
{
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionRequestPurchase);
void exprAuctionRequestPurchase(SA_PARAM_NN_VALID AuctionLot *pLot)
{
	if ((s_pAuctionLotList && eaFind(&s_pAuctionLotList->ppAuctionLots, pLot) >= 0) ||
		(s_pPlayerAuctionLotBidList && eaFind(&s_pPlayerAuctionLotBidList->ppAuctionLots, pLot) >= 0) ||
		(s_pPlayerAuctionLotList && eaFind(&s_pPlayerAuctionLotList->ppAuctionLots, pLot) >= 0))
	{
		if (s_pAuctionLotList)
		{
			eaFindAndRemove(&s_pAuctionLotList->ppAuctionLots, pLot);
		}
		ServerCmd_gslAuction_PurchaseAuction(pLot, true);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionRequestBid);
void exprAuctionRequestBid(SA_PARAM_NN_VALID AuctionLot *pLot, U32 iBidValue)
{
	if (gAuctionConfig.bBiddingEnabled &&
		((s_pAuctionLotList && 
		eaFind(&s_pAuctionLotList->ppAuctionLots, pLot) >= 0) ||
		(s_pPlayerAuctionLotBidList && 
		eaFind(&s_pPlayerAuctionLotBidList->ppAuctionLots, pLot) >= 0))&&
		Auction_AcceptsBids(pLot) && // Make sure the auction accepts bids
		iBidValue >= Auction_GetMinimumNextBidValue(pLot)) // Make sure the bid is at least as much as the current bid + 1 or the starting bid (when there are no bids)
	{
		ServerCmd_gslAuction_BidOnAuction(pLot, iBidValue);
	}
}

//Remove a Sale Listing
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionRequestRemove);
void exprAuctionRequestRemove(SA_PARAM_NN_VALID AuctionLot *pLot)
{
	if (s_pPlayerAuctionLotList && eaFind(&s_pPlayerAuctionLotList->ppAuctionLots, pLot) >= 0)
	{
		ServerCmd_gslAuction_PurchaseAuction(pLot, false);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetAuctionsForPlayer);
void DeleteMe_exprAuctionGetAuctionsForPlayer(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_NN_VALID UIGen *pGen)
{
	AuctionLot ***peaLots = ui_GenGetManagedListSafe(pGen, AuctionLot);
	if (s_pPlayerAuctionLotList) {
		AuctionListPageCopy(peaLots, &s_pPlayerAuctionLotList, 0, 10);
	} else {
		eaClear(peaLots);
	}
	ui_GenSetManagedListSafe(pGen, peaLots, AuctionLot, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetAuctionsForPlayerPageSize);
U32 DeleteMe_exprAuctionGetAuctionsForPlayerPageSize(void)
{
	return 10;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetAuctionsForPlayerPaged);
void DeleteMe_exprAuctionGetAuctionsForPlayerPaged(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_NN_VALID UIGen *pGen, int iPageStart)
{
	AuctionLot ***peaLots = ui_GenGetManagedListSafe(pGen, AuctionLot);
	if (s_pPlayerAuctionLotList) {
		AuctionListPageCopy(peaLots, &s_pPlayerAuctionLotList, iPageStart, 10);
	} else {
		eaClear(peaLots);
	}
	ui_GenSetManagedListSafe(pGen, peaLots, AuctionLot, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetAuctionsForPlayerCount);
U32 DeleteMe_exprAuctionGetAuctionsForPlayerCount(void)
{
	return s_pPlayerAuctionLotList ? eaSize(&s_pPlayerAuctionLotList->ppAuctionLots) : 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetAuctionsForPlayerPageCount);
U32 DeleteMe_exprAuctionGetAuctionsForPlayerPageCount(int iPageStart)
{
	return s_pPlayerAuctionLotList ? min(eaSize(&s_pPlayerAuctionLotList->ppAuctionLots) - iPageStart, 10) : 0;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE ACMD_CATEGORY(Auction);
void gclAuctionShowOwnAuctions(AuctionLotList *pAuctionList)
{
	if (!s_pPlayerAuctionLotList)
	{
		s_pPlayerAuctionLotList = StructCreate(parse_AuctionLotList);
	}
	StructCopy(parse_AuctionLotList, pAuctionList, s_pPlayerAuctionLotList, 0, 0, 0);
	s_uiCooldown = pAuctionList->cooldownEnd;
	s_bWaitingOnSearch = false;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE ACMD_CATEGORY(Auction);
void gclAuctionShowAuctionsBidByPlayer(AuctionLotList *pAuctionList)
{
	s_bClearPlayerBidResultsNextTick = false;
	if (s_pPlayerAuctionLotBidList == NULL)
	{
		s_pPlayerAuctionLotBidList = StructCreate(parse_AuctionLotList);
	}
	StructCopy(parse_AuctionLotList, pAuctionList, s_pPlayerAuctionLotBidList, 0, 0, 0);
	s_uiCooldown = pAuctionList->cooldownEnd;
	s_bWaitingOnSearch = false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionRequestAuctionsForPlayer);
void exprAuctionRequestAuctionsForPlayer(SA_PARAM_OP_VALID Entity *pEnt)
{
	ServerCmd_gslAuction_GetLotsOwnedByPlayer();
	s_bWaitingOnSearch = true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionRequestAuctionsBidByPlayer);
void exprAuctionRequestAuctionsBidByPlayer(SA_PARAM_OP_VALID Entity *pEnt, const char *pchItemName)
{
	ServerCmd_gslAuction_GetLotsBidByPlayer(pchItemName);
	s_bWaitingOnSearch = true;
}

static bool sAuctionLotValid = false;

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetNewLot);
SA_RET_OP_VALID AuctionLot *exprAuctionGetTempLot(void)
{
	if (sAuctionLotValid)
	{
		return CONTAINER_RECONST(AuctionLot, s_pPendingLot);
	}

	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetNewLotItem);
SA_RET_OP_VALID Item *exprAuctionGetTempLotItem(void)
{
	AuctionLot *pLot = exprAuctionGetTempLot();

	if (pLot && eaSize(&pLot->ppItemsV2) > 0 && pLot->ppItemsV2[0] && pLot->ppItemsV2[0]->slot.pItem)
	{
		return pLot->ppItemsV2[0]->slot.pItem;
	}

	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetNewLotCount);
S32 exprAuctionGetTempLotCount(void)
{
	AuctionLot *pLot = exprAuctionGetTempLot();

	if (pLot && eaSize(&pLot->ppItemsV2) > 0 && pLot->ppItemsV2[0] && pLot->ppItemsV2[0]->slot.pItem)
	{
		return pLot->ppItemsV2[0]->slot.pItem->count;
	}

	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionClearNewLotItem);
bool exprAuctionClearTempLotItem(void)
{
	// changed this so it no clears the new auction lot as the UI can have a pointer to the item.
	// Instead the lot is made invalid
	// This will cause client to always have one lot but that is a small amount of memory
	if(sAuctionLotValid)
	{
		sAuctionLotValid = false;
		return true;
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionSetNewLotItem);
bool exprAuctionSetTempLotItem(SA_PARAM_OP_VALID Entity *pOwner, SA_PARAM_NN_VALID Entity *pEnt, U64 uItemID, int iCount)
{
	InvBagIDs eBagID = InvBagIDs_None;
	int iSlotIdx = 0;
	Item *pItem = inv_GetItemAndSlotsByID(pEnt, uItemID, &eBagID, &iSlotIdx);
	InventoryBag *pBag;
	InventorySlot *pSlot;
	GameAccountDataExtract *pExtract;

	s_bUpdatePendingLot = true;

	pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), eBagID, pExtract);

	pSlot = pBag ? inv_GetSlotPtr(pBag, iSlotIdx) : NULL;

	if (!s_pNextPendingLot)
	{
		s_pNextPendingLot = StructCreateNoConst(parse_AuctionLot);
	}

	sAuctionLotValid = false; // the lot can't be valid unless it passes the following ...

	if (pItem 
		&& pSlot 
		&& !(pItem->flags & kItemFlag_Bound)
		&& !(pItem->flags & kItemFlag_BoundToAccount)
		&& !item_IsUnidentified(pItem)) 
	{
		ItemDef *pItemDef = GET_REF(pItem->hItem);
		eaClearStructNoConst(&s_pNextPendingLot->ppItemsV2, parse_AuctionSlot);
		ea32Clear(&s_pNextPendingLot->auctionPetContainerIDs);

		iCount = min(pItem->count, iCount);
		if (pItemDef->eType != kItemType_Numeric && iCount > 0)
		{
			NOCONST(AuctionSlot) *pAuctionItem = StructCreateNoConst(parse_AuctionSlot);
			pAuctionItem->slot.pItem = StructCloneDeConst(parse_Item, pItem);
			pAuctionItem->slot.pItem->count = iCount;
			item_trh_GetOrCreateAlgoProperties(pAuctionItem->slot.pItem);
			if (pOwner)
			{
				ea32Push(&s_pNextPendingLot->auctionPetContainerIDs, entGetContainerID(pEnt));
			}
			else
			{
				ea32Push(&s_pNextPendingLot->auctionPetContainerIDs, 0);
			}
			eaPush(&s_pNextPendingLot->ppItemsV2, pAuctionItem);
			sAuctionLotValid = true;	// lot is now valid to use
			return true;
		}
	}

	if(s_pNextPendingLot)
	{
		StructDestroyNoConst(parse_AuctionLot, s_pNextPendingLot);
		s_pNextPendingLot = NULL;
	}
	notify_NotifySend(NULL, kNotifyType_AuctionFailed, TranslateMessageKey("Auction.CantSellItem"), NULL, NULL);
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionSetNewLotItemByBagIndex);
bool exprAuctionSetNewLotItemByBagIndex(S32 iBagIndex, S32 iSlotIndex, S32 iCount, S32 iInsertAt)
{
	//S32 size = eaSize(&s_eaDeconstructSlotList);
	Entity *pEnt = entActivePlayerPtr();
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
	InventorySlot* pInvSlot = inv_ent_GetSlotPtr(pEnt, iBagIndex, iSlotIndex, pExtract);

	Item *pItem = pInvSlot ? pInvSlot->pItem : NULL;
	ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
	ItemDef *pCraftRecipe = pItemDef ? GET_REF(pItemDef->hCraftRecipe) : NULL;

	if (pItem)
	{
		return exprAuctionSetTempLotItem(NULL, pEnt, pItem->id, iCount);
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionSetNewLotItemByKey);
bool exprAuctionSetNewLotItemByKey(const char *pchKeyA, S32 iCount, S32 iInsertAt)
{
	UIInventoryKey KeyA = {0};

	if (!gclInventoryParseKey(pchKeyA, &KeyA))
		return false;
	if (!KeyA.pSlot || !KeyA.pSlot->pItem || KeyA.pOwner != entActivePlayerPtr())
		return false;

	return exprAuctionSetTempLotItem(KeyA.pOwner != KeyA.pEntity ? KeyA.pOwner : NULL, KeyA.pEntity, KeyA.pSlot->pItem->id, iCount);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionSetNewLotItemFromSavedPet);
bool exprAuctionSetNewLotItemFromSavedPet(SA_PARAM_NN_VALID Entity *pEnt, ContainerID srcPetID, S32 iTargetSlot)
{
	NOCONST(Item)* pItem = CONTAINER_NOCONST(Item, item_FromSavedPet(pEnt, srcPetID));
	NOCONST(AuctionSlot) *pAuctionItem = NULL;

	if(!gConf.bAllowContainerItemsInAuction)
	{
		return false;
	}

	if(!pItem)
	{
		return false;
	}

	s_bUpdatePendingLot = true;

	// All validation passes, add the item
	if (!s_pNextPendingLot)
	{
		s_pNextPendingLot = StructCreateNoConst(parse_AuctionLot);
	}
	eaClearStructNoConst(&s_pNextPendingLot->ppItemsV2, parse_AuctionSlot);
	ea32Clear(&s_pNextPendingLot->auctionPetContainerIDs);


	//Create a new slot to hold a fake version of the item
	pAuctionItem = StructCreateNoConst(parse_AuctionSlot);
	pAuctionItem->slot.pItem = pItem;
	ea32Push(&s_pNextPendingLot->auctionPetContainerIDs, 0);
	eaPush(&s_pNextPendingLot->ppItemsV2, pAuctionItem);
	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionRequestCreateAuctionEx);
void exprAuctionRequestCreateAuctionEx(U32 iBuyoutPrice, U32 iStartingBid, U32 iAuctionDuration)
{
	if (gAuctionConfig.bBiddingEnabled)
	{
		if (iBuyoutPrice == 0 && iStartingBid == 0)
		{
			notify_NotifySend(NULL, kNotifyType_AuctionFailed, TranslateMessageKey("Auction.MustSetBuyoutPriceOrStartingBid"), NULL, NULL);
			return;
		}
		if (iStartingBid && iBuyoutPrice && iBuyoutPrice <= iStartingBid)
		{
			notify_NotifySend(NULL, kNotifyType_AuctionFailed, TranslateMessageKey("Auction.BuyoutPriceMustBeGreaterThanStartingBid"), NULL, NULL);
			return;
		}
	}
	else if (iBuyoutPrice <= 0)
	{
		notify_NotifySend(NULL, kNotifyType_AuctionFailed, TranslateMessageKey("Auction.MustSetPrice"), NULL, NULL);
		return;
	}
	if (s_pPendingLot && eaSize(&s_pPendingLot->ppItemsV2))
	{
		s_pPendingLot->price = iBuyoutPrice;
		if (gAuctionConfig.bBiddingEnabled && iStartingBid)
		{
			s_pPendingLot->pBiddingInfo = StructCreateNoConst(parse_AuctionBiddingInfo);
			s_pPendingLot->pBiddingInfo->iMinimumBid = iStartingBid;
		}
		ServerCmd_gslAuction_CreateLotFromDescription((AuctionLot *)s_pPendingLot, iAuctionDuration);
	}
	else
	{
		notify_NotifySend(NULL, kNotifyType_AuctionFailed, TranslateMessageKey("Auction.MustSetItem"), NULL, NULL);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionRequestCreateAuction);
void exprAuctionRequestCreateAuction(U32 price)
{
	exprAuctionRequestCreateAuctionEx(price, 0, 0);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE ACMD_CATEGORY(Auction);
void gclAuctionPostComplete(const char *pMessage, bool bSuccess)
{
	s_bUpdatePendingLot = true;
	s_pNextPendingLot = NULL;
	notify_NotifySend(NULL, bSuccess ? kNotifyType_AuctionSuccess : kNotifyType_AuctionFailed, pMessage, NULL, NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(IsDebugAuctionUI);
bool exprIsDebugAuctionUI() 
{
	return s_bDebugAuctionUI || (entActivePlayerPtr && entGetAccessLevel(entActivePlayerPtr()) >= ACCESS_DEBUG);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetPriceWithIcon);
char *exprAuctionGetPriceWithIcon(SA_PARAM_OP_VALID AuctionLot *auctionLot)
{
	static char *s_result = NULL;
	estrClear(&s_result);

	if ( auctionLot )
	{
		FormatGameMessageKey(&s_result, "Auction_ItemPrice_WithIcon",
			STRFMT_INT("Amount", auctionLot->price),
			STRFMT_END);
	}

	return s_result;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetRemainingPostings);
U32 exprAuctionGetRemainingPostings(SA_PARAM_NN_VALID Entity *pEnt)
{
	S32 iNumPostedLots = pEnt->pPlayer->uLastLotsPostedByPlayer;

	if (gAuctionConfig.bExpiredAuctionsCountTowardMaximum)
		iNumPostedLots += EmailV3_GetNumNPCMessagesByType(pEnt, kNPCEmailType_ExpiredAuction, true);

	if (pEnt && pEnt->pPlayer)
	{
		return Auction_GetMaximumPostedAuctions(pEnt) - iNumPostedLots;
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetMaxPostings);
U32 exprAuctionGetMaxPostings(SA_PARAM_NN_VALID Entity *pEnt)
{
	return Auction_GetMaximumPostedAuctions(pEnt);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetPostingFee);
U32 exprAuctionGetPostingFee(SA_PARAM_NN_VALID Entity *pEnt, int uiAskingPrice)
{
	ContactDialog* pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	bool bPlayerIsAtAuctionHouse = (pDialog && pDialog->screenType == ContactScreenType_Market);
	return Auction_GetPostingFee(pEnt, uiAskingPrice, 0, 0, bPlayerIsAtAuctionHouse);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetPostingFeeByDuration);
U32 exprAuctionGetPostingFeeByDuration(SA_PARAM_NN_VALID Entity *pEnt, U32 iBuyoutPrice, U32 iStartingBid, U32 iAuctionDuration)
{
	ContactDialog* pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	bool bPlayerIsAtAuctionHouse = (pDialog && pDialog->screenType == ContactScreenType_Market);
	return Auction_GetPostingFee(pEnt, iBuyoutPrice, iStartingBid, iAuctionDuration, bPlayerIsAtAuctionHouse);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetExpireTime);
U32 exprAuctionGetExpireTime(SA_PARAM_NN_VALID Entity *pEnt)
{
	return Auction_GetExpireTime(pEnt);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetMaximumDuration);
U32 exprAuctionGetMaximumDuration(SA_PARAM_NN_VALID Entity *pEnt)
{
	return Auction_GetMaximumDuration(pEnt);
}

AUTO_STARTUP(GclAuctionLoadConfig) ASTRT_DEPS(AS_CharacterAttribs);
void gclAuction_LoadAuctionConfig(void)
{
	AuctionConfig_Load();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetDurationOptions);
void exprAuctionGetDurationOptions(SA_PARAM_NN_VALID UIGen *pGen)
{
	ui_GenSetList(pGen, &gAuctionConfig.eaDurationOptions, parse_AuctionDurationOption);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetMinimumNextBidValue);
U32 exprAuctionGetMinimumNextBidValue(SA_PARAM_NN_VALID AuctionLot *pLot)
{
	if (Auction_AcceptsBids(pLot))
	{
		return Auction_GetMinimumNextBidValue(pLot);
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionAcceptsBids);
bool exprAuctionAcceptsBids(SA_PARAM_NN_VALID AuctionLot *pLot)
{
	Entity *pEnt = entActivePlayerPtr();
	return pEnt && entGetContainerID(pEnt) != pLot->ownerID && Auction_AcceptsBids(pLot);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionAcceptsBuyout);
bool exprAuctionAcceptsBuyout(SA_PARAM_NN_VALID AuctionLot *pLot)
{
	Entity *pEnt = entActivePlayerPtr();
	return pEnt && entGetContainerID(pEnt) != pLot->ownerID && Auction_AcceptsBuyout(pLot);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionCanBeCanceled);
bool exprAuctionCanBeCanceled(SA_PARAM_NN_VALID AuctionLot *pLot)
{
	return Auction_CanBeCanceled(pLot);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetCurrentBid);
U32 exprAuctionGetCurrentBid(SA_PARAM_NN_VALID AuctionLot *pLot)
{
	return pLot->pBiddingInfo ? pLot->pBiddingInfo->iCurrentBid : 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetStartingBid);
U32 exprAuctionGetStartingBid(SA_PARAM_NN_VALID AuctionLot *pLot)
{
	return pLot->pBiddingInfo ? pLot->pBiddingInfo->iMinimumBid : 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetItem);
SA_RET_OP_VALID Item * exprAuctionGetItem(SA_PARAM_OP_VALID AuctionLot *pLot)
{
	if (pLot && eaSize(&pLot->ppItemsV2) > 0 && pLot->ppItemsV2[0])
		return pLot->ppItemsV2[0]->slot.pItem;

	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionBrokerLotGetItemDef);
SA_RET_OP_VALID ItemDef * exprAuctionBrokerGetItemDef(SA_PARAM_OP_VALID AuctionBrokerLot *pLot)
{
	if (pLot)
		return GET_REF(pLot->hItemDef);

	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetItemCount);
U32 exprAuctionGetItemCount(SA_PARAM_OP_VALID AuctionLot *pLot)
{
	if (pLot && eaSize(&pLot->ppItemsV2) > 0 && pLot->ppItemsV2[0] && pLot->ppItemsV2[0]->slot.pItem)
		return pLot->ppItemsV2[0]->slot.pItem->count;

	return 0;
}



AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionPlayerCanBidOrBuyout);
bool exprAuctionPlayerCanBidOrBuyout(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_NN_VALID AuctionLot *pLot)
{
	bool bBidsAreWelcome = Auction_AcceptsBids(pLot);
	bool bBuyoutIsPossible = Auction_AcceptsBuyout(pLot);

	if (pEnt && (bBidsAreWelcome || bBuyoutIsPossible))
	{
		S32 iCurrentResourceCount = inv_GetNumericItemValue(pEnt, Auction_GetCurrencyNumeric());
		if ((bBidsAreWelcome && iCurrentResourceCount >= (S32)Auction_GetMinimumNextBidValue(pLot)) ||
			(bBuyoutIsPossible && iCurrentResourceCount >= (S32)pLot->price))
		{
			return true;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetOwnerName);
const char * exprAuctionGetOwnerName(SA_PARAM_NN_VALID AuctionLot *pLot)
{
	if (pLot->pOptionalData)
	{
		return pLot->pOptionalData->pchOwnerName;
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetOwnerHandle);
const char * exprAuctionGetOwnerHandle(SA_PARAM_NN_VALID AuctionLot *pLot)
{
	if (pLot->pOptionalData)
	{
		return pLot->pOptionalData->pchOwnerHandle;
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionPlayerIsTopBidder);
bool exprAuctionPlayerIsTopBidder(SA_PARAM_NN_VALID AuctionLot *pLot)
{
	Entity *pEnt = entActivePlayerPtr();

	return pEnt && pLot->pBiddingInfo && pLot->pBiddingInfo->iBiddingPlayerContainerID == pEnt->myContainerID;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetDefaultAuctionDurationOption);
SA_RET_OP_VALID const AuctionDurationOption * exprAuctionGetDefaultAuctionDurationOption(void)
{
	FOR_EACH_IN_CONST_EARRAY_FORWARDS(gAuctionConfig.eaDurationOptions, AuctionDurationOption, pOption)
	{
		if (pOption->bUIDefaultDuration)
		{
			return pOption;
		}
	}
	FOR_EACH_END

	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetSalesFeeRatio);
F32 exprAuctionGetSalesFeeRatio(U32 iAuctionDuration)
{
	Entity *pEnt = entActivePlayerPtr();
	ContactDialog* pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	bool bPlayerIsAtAuctionHouse = (pDialog && pDialog->screenType == ContactScreenType_Market);
	AuctionDurationOption *pDurationOption = Auction_GetDurationOption(iAuctionDuration);
	F32 fRatio;

	if(pEnt == NULL || !gAuctionConfig.bAuctionsUseSoldFee)
	{
		// no fee
		return 0.f;
	}
	// Get the sales fee
	if (pDurationOption && pDurationOption->fAuctionSoldFee)
	{
		fRatio = pDurationOption->fAuctionSoldFee;
	}
	else
	{
		fRatio = gAuctionConfig.fAuctionDefaultSoldFee;
	}	

	//
	// This is where account / character price changes should be set
	//
	if(gamePermission_Enabled())
	{
		bool bFound;
		S32 iVal = GamePermissions_trh_GetCachedMaxNumericEx(CONTAINER_NOCONST(Entity, pEnt), GAME_PERMISSION_AUCTION_SOLD_PERCENT, false, &bFound);
		if(bFound)
		{
			fRatio = ((float)iVal) / 100.0f ;
		}
	}

	if (!bPlayerIsAtAuctionHouse)
		fRatio *= gAuctionConfig.fPlayerRoamingFeeMultiplier;

	return fRatio;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetSalesFee);
U32 exprAuctionGetSalesFee(U32 iBuyoutPrice, U32 iStartingBid, U32 iAuctionDuration)
{
	Entity *pEnt = entActivePlayerPtr();
	ContactDialog* pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	bool bPlayerIsAtAuctionHouse = (pDialog && pDialog->screenType == ContactScreenType_Market);
	return Auction_GetSalesFee(pEnt,iBuyoutPrice,iStartingBid,iAuctionDuration, bPlayerIsAtAuctionHouse);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionGetCurrencyNumeric);
const char * exprAuctionGetCurrencyNumeric(void)
{
	return Auction_GetCurrencyNumeric();
}

// Auction Broker functions
#include "mission/contact_common.h"
#include "mission/mission_common.h"
#include "AuctionBrokerCommon.h"

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionBrokerGetList);
void exprAuctionBrokerGetList(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_NN_VALID UIGen *pGen)
{
	AuctionBrokerLot ***peaLots = ui_GenGetManagedListSafe(pGen, AuctionBrokerLot);

	AuctionLotList **ppAuctionList = &s_pAuctionBrokerList;
	int iStart = 0;
	int iPageSize = 6;

	AuctionLotList *pAuctionList = *ppAuctionList;
	if (s_bClearSearchResultsNextTick)
	{
		StructDestroySafe(parse_AuctionLotList, ppAuctionList);
		pAuctionList = NULL;
		s_bClearSearchResultsNextTick = false;
	}

	if (pAuctionList)
	{
		// Grab the Auction Broker Class LevelInfo def
		ContactDialog *pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
		const AuctionBrokerLevelInfo *pLevelInfo = pDialog ? pDialog->pAuctionBrokerLastUsedLevelInfo : NULL;
		if (pLevelInfo && eaSize(&pLevelInfo->ppItemDropInfo) != 0)
		{
			// For each ItemDef in the LevelInfo, look for it in the pAuctionList.
			int iUsed = 0;
			int iItemDef;
			for( iItemDef=0; iItemDef<eaSize(&pLevelInfo->ppItemDropInfo); ++iItemDef )
			{
				ItemDef* pLevelInfoItemDef = GET_REF(pLevelInfo->ppItemDropInfo[iItemDef]->itemDefRef.hDef);
				const char* pLevelInfoItemDefName = REF_STRING_FROM_HANDLE(pLevelInfo->ppItemDropInfo[iItemDef]->itemDefRef.hDef);
				int iAuctionLot = 0;
				AuctionBrokerLot *pABLot = NULL;
				AuctionLot* pMatchingAuctionLot = NULL;
				Item* pMatchingItem = NULL;
				if( pLevelInfoItemDef )
				{
					for( iAuctionLot=0; iAuctionLot<eaSize(&pAuctionList->ppAuctionLots); ++iAuctionLot )
					{
						AuctionLot *pAuctionLot = pAuctionList->ppAuctionLots[iAuctionLot];
						if( pAuctionLot && pAuctionLot->ppItemsV2 && eaSize(&pAuctionLot->ppItemsV2) > 0 && pAuctionLot->ppItemsV2[0] && pAuctionLot->ppItemsV2[0]->slot.pItem )
						{
							ItemDef* pAuctionItemDef = GET_REF(pAuctionLot->ppItemsV2[0]->slot.pItem->hItem);
							if(pAuctionItemDef && !strcmp(pAuctionItemDef->pchName, pLevelInfoItemDef->pchName) )
							{
								pMatchingAuctionLot = pAuctionLot;
								pMatchingItem = pAuctionLot->ppItemsV2[0]->slot.pItem;
								break;
							}
						}
					}
				}

				// If you find it, add the AuctionLot to the AuctionBrokerLotList in the UI
				// If you didn't find a match, so create a dummy AuctionBrokerLot
				if (eaSize(peaLots) == iUsed)
				{
					pABLot = StructCreate(parse_AuctionBrokerLot);
					eaPush(peaLots, pABLot);
				}
				else if (iUsed < eaSize(peaLots))
				{
					pABLot = (*peaLots)[iUsed];
				}
				iUsed++;

				assert(pABLot);
				ANALYSIS_ASSUME(pABLot);
				//Initialize the AuctionBrokerLot
				COPY_HANDLE(pABLot->hItemDef, pLevelInfo->ppItemDropInfo[iItemDef]->itemDefRef.hDef);
				pABLot->pCheapestLot = pMatchingAuctionLot;
				pABLot->uiLotCountAtAnyPrice = (pMatchingAuctionLot && pAuctionList->piAuctionLotsCounts) ? pAuctionList->piAuctionLotsCounts[iAuctionLot] : 0;
				pABLot->uiCheapestPrice = pMatchingAuctionLot ? pMatchingAuctionLot->price : 0;
				pABLot->pWhereFrom = pLevelInfo->ppItemDropInfo[iItemDef]->pchDropInfo;

				if( pMatchingItem )
				{
					if( pABLot->pOwnedItem )
					{
						StructDestroySafe(parse_Item, &pABLot->pOwnedItem);
					}
					pABLot->pOwnedItem = NULL;
					pABLot->pUnownedItem = pMatchingItem;
				}
				else
				{
					if( !pABLot->pOwnedItem || stricmp(REF_STRING_FROM_HANDLE(pABLot->pOwnedItem->hItem), pLevelInfoItemDefName) )
					{
						pABLot->pOwnedItem = (Item*) inv_ItemInstanceFromDefName(pLevelInfoItemDefName, entity_GetSavedExpLevel(pEnt), 0, NULL, NULL, NULL, false, NULL); // Create a Dummy Item
					}
					pABLot->pUnownedItem = pABLot->pOwnedItem;
				}
			}

			while (eaSize(peaLots) > iUsed)
			{
				eaPop(peaLots);
			}
		}
		else
		{
			eaClear(peaLots);
		}
	}
	else
	{
		eaClear(peaLots);
	}

	ui_GenSetManagedListSafe(pGen, peaLots, AuctionBrokerLot, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionHouseGetValidCharacterPaths);
void exprAuctionHouseGetValidCharPaths(SA_PARAM_NN_VALID UIGen *pGen)
{
	CharacterPath ***peaPaths = ui_GenGetManagedListSafe(pGen, CharacterPath);
	eaClear(peaPaths);
	eaPush(peaPaths, NULL);
	CharacterPath_GetCharacterPaths(peaPaths, NULL, false, true);
	ui_GenSetManagedListSafe(pGen, peaPaths, CharacterPath, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionHouseGetSearchableAttribs);
void exprAuctionHouseGetSearchableAttribs(SA_PARAM_NN_VALID UIGen *pGen)
{
	AttribStat ***peaAttribs = ui_GenGetManagedListSafe(pGen, AttribStat);
	int i;
	eaClear(peaAttribs);
	for (i = 0; i < eaiSize(&gAuctionConfig.eaiSearchableAttribs); i++)
	{
		AttribType eType = gAuctionConfig.eaiSearchableAttribs[i];
		AttribStat* pStat = eaGetStruct(peaAttribs, parse_AttribStat, i);

		pStat->pchKeyName = StaticDefineInt_FastIntToString(AttribTypeEnum, eType);
		if(pStat->pchKeyName)
		{
			estrPrintf(&pStat->estrNameMessage,"StaticDefine_AttribType_%s",pStat->pchKeyName);
		}
	}
	ui_GenSetManagedListSafe(pGen, peaAttribs, AttribStat, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionHouseGetSearchDescription);
const char* exprAuctionHouseGetSearchDescription(bool bPetSearch, bool bUsableByMe, int minLevel, int maxLevel, int quality, const char *searchText, U32 uiNumGemSlots, S32 eGemType, const char* pchClass, S32 eAttrib1, S32 eAttrib2, S32 eAttrib3)
{
	static char* s_pchString = NULL;
	char pchQualityMessage[32];
	CharacterClass* pClass = RefSystem_ReferentFromString(g_hCharacterClassDict, pchClass);
	int iNumAttribs = (eAttrib1 > 0) + (eAttrib2 > 0) + (eAttrib3 > 0);

	sprintf(pchQualityMessage, "Item.Quality.%d", quality); // From ItemMessages.ms
	estrClear(&s_pchString);
	FormatGameMessageKey(&s_pchString, "Auction.AdvancedSearchDescFormat",
		STRFMT_INT("MinAndMaxLevel", minLevel > 0 && maxLevel > 0),
		STRFMT_INT("MinLevelOnly", minLevel > 0 && maxLevel == 0),
		STRFMT_INT("MaxLevelOnly", maxLevel > 0 && minLevel == 0),
		STRFMT_INT("MinLevel", minLevel),
		STRFMT_INT("MaxLevel", maxLevel),
		STRFMT_INT("HasQuality", quality > 0),
		STRFMT_MESSAGEKEY("Quality", pchQualityMessage),
		STRFMT_INT("HasText", searchText && searchText[0]),
		STRFMT_STRING("SearchText", searchText),
		STRFMT_INT("NumSlots", uiNumGemSlots),
		STRFMT_INT("PetSearch", bPetSearch),
		STRFMT_INT("HasSlotType", eGemType > 0),
		STRFMT_STRING("SlotTypeName", StaticDefineGetTranslatedMessage(ItemGemTypeEnum, eGemType)),
		STRFMT_INT("UsableByMe", bUsableByMe),
		STRFMT_INT("NumAttribs", iNumAttribs),
		STRFMT_STRING("Attrib1", StaticDefineGetTranslatedMessage(AttribTypeEnum, eAttrib1)),
		STRFMT_STRING("Attrib2", StaticDefineGetTranslatedMessage(AttribTypeEnum, eAttrib2)),
		STRFMT_STRING("Attrib3", StaticDefineGetTranslatedMessage(AttribTypeEnum, eAttrib3)),
		STRFMT_INT("HasClass", !!pClass),
		STRFMT_STRING("ClassName", pClass ? TranslateDisplayMessage(pClass->msgDisplayName) : ""),
		STRFMT_END);
	
	return s_pchString;
}

AUTO_STRUCT;
typedef struct AuctionPriceHistoryClientData
{
	S32 iPrice;
	U32 uiLastRequestTime;
} AuctionPriceHistoryClientData;

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gclAuctionUI_ReceivePriceHistory(const char* pchName, int iPrice)
{
	AuctionPriceHistoryClientData* pData = NULL;
	pchName = allocFindString(pchName);

	if (!pchName)
		return;

	if (!stCachedPriceHistories)
		stCachedPriceHistories = stashTableCreateWithStringKeys(8, StashCaseInsensitive);

	stashFindPointer(stCachedPriceHistories, pchName, &pData);

	if (!pData)
	{
		pData = StructCreate(parse_AuctionPriceHistoryClientData);
		stashAddPointer(stCachedPriceHistories, pchName, pData, true);
	}
	pData->iPrice = iPrice;
}

//Returns -1 if it hasn't received anything from the server.
// May return 0 if the server didn't have a sale price record for the requested item def.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionHouseGetItemPriceHistory);
S32 exprAuctionHouseGetItemPriceHistory(const char* pchDefName)
{
	AuctionPriceHistoryClientData* pData = NULL;

	if (!gAuctionConfig.bEnablePriceHistoryTracking)
		return 0;

	pchDefName = allocFindString(pchDefName);


	if (!pchDefName)
		return 0;

	if (!stCachedPriceHistories)
		stCachedPriceHistories = stashTableCreateWithStringKeys(8, StashCaseInsensitive);

	stashFindPointer(stCachedPriceHistories, pchDefName, &pData);

	if (!pData)
	{
		pData = StructCreate(parse_AuctionPriceHistoryClientData);
		pData->iPrice = -1;
		stashAddPointer(stCachedPriceHistories, pchDefName, pData, true);
	}

	if (pData->uiLastRequestTime + AUCTION_UI_PRICE_CHECK_COOLDOWN < timeSecondsSince2000())
	{
		ServerCmd_gslAuction_CheckPriceHistoryForItem(pchDefName);
		pData->uiLastRequestTime = timeSecondsSince2000();
	}

	return pData->iPrice;
}

#include "AutoGen/AuctionUI_c_ast.c"
