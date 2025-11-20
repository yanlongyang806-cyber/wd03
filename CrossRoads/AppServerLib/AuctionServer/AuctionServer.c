#include "AuctionServer.h"
#include "AppServerLib.h"
#include "aslAuctionServer.h"


#include "AutoGen/AuctionLot_h_ast.h"
#include "autogen/AppServerLib_autogen_remotefuncs.h"
#include "objIndex.h"
#include "objContainer.h"
#include "chatCommonStructs.h"
#include "logging.h"
#include "utilitiesLib.h"
#include "StringCache.h"
#include "characterclass.h"

#include "AutoGen/AuctionServer_h_ast.h"
#include "AutoGen/chatCommonStructs_h_ast.h"
#include "AutoGen/ChatServer_autogen_RemoteFuncs.h"

int gMaxElementsToSearch = 200000;
AUTO_CMD_INT(gMaxElementsToSearch,MaxElementsToSearch) ACMD_COMMANDLINE;

int gMaxCachedStringTables = 1000;
AUTO_CMD_INT(gMaxCachedStringTables, MaxCachedStringTables) ACMD_COMMANDLINE;

SearchTermTable **gAuctionSearchTermCache = NULL;

static SearchTermTable *s_currentSearchTable;

AuctionBlockedItems gBlockedItems;

AUTO_RUN;
int RegisterAuctionServer(void)
{
    aslRegisterApp(GLOBALTYPE_AUCTIONSERVER, AuctionServerLibInit, 0);
    return 1;
}


AUTO_RUN;
void InitAuctionSearchTermCache(void)
{
	eaCreate(&gAuctionSearchTermCache);
	eaIndexedEnable(&gAuctionSearchTermCache, parse_SearchTermTable);
}

AUTO_STARTUP(AuctionServerLoad) ASTRT_DEPS(AS_CharacterAttribs);
void AuctionServer_Load(void)
{
	AuctionConfig_Load();
}

AUTO_FIXUPFUNC;
TextParserResult SearchTermTable_Fixup(SearchTermTable *pSTT,enumTextParserFixupType eFixupType, void *pExtraData)
{
	bool bRet = true;

	switch (eFixupType)
	{
	case FIXUPTYPE_CONSTRUCTOR:
		pSTT->tokens = stashTableCreate(256, StashDefault, StashKeyTypeAddress, sizeof(void*));
		break;
	}

	return bRet;
}

// Manage auction logging
int gLogAuctionSearches = 1;
AUTO_CMD_INT(gLogAuctionSearches, LogAuctionSearches) ACMD_COMMANDLINE;

static void
LogAuctionSearchRequest(const char *label, AuctionLotList *ll, AuctionSearchRequest *request)
{
	if ( gLogAuctionSearches )
	{
		log_printf(LOG_AUCTION, "AuctionSearch:%s:minLevel=%d:maxLevel=%d:itemQuality=%d:minUsageRestrictionCategory=%d:maxUsageRestrictionCategory=%d:ownerID=%d:excludeOwner=%d:sortType=%d:numResults=%d:hitCount=%d:realHitCount=%d:maxResults=%d:timeTaken=%f:stringSearch=%s", 
			label, request->minLevel, request->maxLevel, request->itemQuality, request->minUsageRestrictionCategory, request->maxUsageRestrictionCategory, 
			request->ownerID, request->bExcludeOwner, request->sortType, request->numResults, ll->hitcount, ll->realHitcount, ll->maxResults, ll->timeTaken, request->stringSearch ? request->stringSearch : "" );
	}
}

void Auction_STC_Increment(const char *token)
{
	SearchTermTable *tbl = eaIndexedGetUsingString(&gAuctionSearchTermCache, token);
	if (tbl)
	{	//increase the hitcount for an existing search. If we haven't searched before it'll default to 0.
		tbl->hitcount++;
		s_currentSearchTable = tbl;
	}
}

bool Auction_STC_Search(const char *token, const char *string)
{
	SearchTermTable *tbl = s_currentSearchTable;
	if (!tbl)
	{
		tbl = eaIndexedGetUsingString(&gAuctionSearchTermCache, token);
	}	
	if (!tbl)
	{
		int removeCount = 0;
		while (eaSize(&gAuctionSearchTermCache) + 1 > gMaxCachedStringTables )
		{
			EARRAY_FOREACH_REVERSE_BEGIN(gAuctionSearchTermCache, i);
			{
				if (gAuctionSearchTermCache[i]->hitcount < 1)				
				{
					removeCount++;
					tbl = eaRemove(&gAuctionSearchTermCache, i);
					StructDestroy(parse_SearchTermTable, tbl);
				}
				else
				{
					gAuctionSearchTermCache[i]->hitcount--;
				}
				//don't remove more than 25%
				if (removeCount > gMaxCachedStringTables/4)
					break;
			}
			EARRAY_FOREACH_END;
		}

		tbl = StructCreate(parse_SearchTermTable);
		tbl->term = strdup(token);
		eaIndexedAdd(&gAuctionSearchTermCache, tbl);
	}
	
	{
		int result = 0;
		if (!stashFindInt(tbl->tokens, string, &result))
		{	//no previous search
			result = !!strstri(string, token);
			stashAddInt(tbl->tokens, string, result, true);
		}
		if (result)
			return true;
	}
	return false;
}

bool Auction_FilterForSearch(AuctionLot *pLot, AuctionSearchRequest *pRequest)
{
	AuctionSlot *pSlot;
	ItemDef* pDef;
	S32 i;
	U32 uiPetLevel;

	if (pLot->state != ALS_Open || eaSize(&pLot->ppItemsV2) == 0)
	{	
		return false;
	}
	pSlot = pLot->ppItemsV2[0];

	pDef = SAFE_GET_REF(pSlot->slot.pItem, hItem);

	if (!pDef)
		return false;

	uiPetLevel = SAFE_MEMBER4(pSlot, slot.pItem, pSpecialProps, pSuperCritterPet, uLevel);

	if (pRequest->ownerID)
	{
		if (pRequest->bExcludeOwner && pLot->ownerID == pRequest->ownerID)
			return false;		
		if (!pRequest->bExcludeOwner && pLot->ownerID != pRequest->ownerID)
			return false;
	}
	if (pRequest->minLevel > 0)
	{
		if (item_GetMinLevel(pSlot->slot.pItem) < pRequest->minLevel)
			return false;
	}
	if (pRequest->maxLevel > 0)
	{
		if (item_GetMinLevel(pSlot->slot.pItem) > pRequest->maxLevel)
			return false;
	}
	if (pRequest->maxUsageRestrictionCategory > UsageRestrictionCategory_None)
	{
		if ( ( pSlot->eUICategory < pRequest->minUsageRestrictionCategory ) || ( pSlot->eUICategory > pRequest->maxUsageRestrictionCategory ) )
		{
			return false;
		}
	}
	if (pRequest->itemQuality > -1)
	{
		if (item_GetQuality(pSlot->slot.pItem) != pRequest->itemQuality)
			return false;
	}
	if (pRequest->sortType > 0)
	{
		if (pSlot->iItemSortType != (U32)pRequest->sortType)
			return false;
	}

	if (pRequest->pchRequiredClass)
	{
		UsageRestriction* pRestriction = SAFE_MEMBER(pDef, pRestriction);

		if (pRestriction && eaSize(&pRestriction->ppCharacterClassesAllowed) > 0)
		{
			for (i = 0; i < eaSize(&pRestriction->ppCharacterClassesAllowed); i++)
				if (REF_STRING_FROM_HANDLE(pRestriction->ppCharacterClassesAllowed[i]->hClass) == pRequest->pchRequiredClass)
					break;

			if (i == eaSize(&pRestriction->ppCharacterClassesAllowed))
				return false;
		}
	}

	if (pRequest->eRequiredGemType != 0)
	{
		int iNumGemSlots = eaSize(&pDef->ppItemGemSlots);

		if (pDef->eType == kItemType_Gem && !(pDef->eGemType & pRequest->eRequiredGemType) && pDef->eGemType != kItemGemType_Any)
			return false;

		if (iNumGemSlots > 0)
		{
			for (i = 0; i < iNumGemSlots; i++)
			{
				if (pDef->ppItemGemSlots[i]->eType == pRequest->eRequiredGemType || pDef->ppItemGemSlots[i]->eType == kItemGemType_Any)
					break;

			}
			if (i == iNumGemSlots)
				return false;
		}
		else 
			return false;
	}

	if ((pRequest->uiNumGemSlots > 0) && (pRequest->uiNumGemSlots > pSlot->uiNumGemSlots))
		return false;

	if ((pRequest->uiPetMinLevel > 0) && (pRequest->uiPetMinLevel > uiPetLevel))
		return false;

	if ((pRequest->uiPetMaxLevel > 0) && (pRequest->uiPetMaxLevel < uiPetLevel))
		return false;

	if(pLot->ownerID != pRequest->ownerID && eaSize(&gBlockedItems.eaBlockedItems) > 0)
	{
		if(pDef)
		{
			if(eaIndexedGetUsingString(&gBlockedItems.eaBlockedItems, pDef->pchName))
			{
				// this is a blocked item
				return false;
			}
		}
	}

	if (pRequest->stringSearch)
	{
		if ( pSlot->ppTranslatedNames != NULL )
		{
			Language searchLang = pRequest->stringLanguage;

			// Temporary hack to deal with the ppTranslatedNames array being corrupted by having all the NULL entries
			//  removed.  Should prevent the crash and return english results for corrupted entries.
			if ( eaSize(&pSlot->ppTranslatedNames) <= searchLang )
			{
				if ( eaSize(&pSlot->ppTranslatedNames) < 1 )
				{
					return false;
				}
				else
				{
					searchLang = 0;
				}
			}
			if ( ( pSlot->ppTranslatedNames[searchLang] == NULL ) || ( *pSlot->ppTranslatedNames[searchLang] == '\0' ) )
			{
				return false;
			}
			return Auction_STC_Search(pRequest->stringSearch, pSlot->ppTranslatedNames[searchLang]);
		}
		else
		{
			return false;
		}
	}

	for (i = 0; i < eaiSize(&pRequest->eaiAttribs); i++)
	{
		int idx = eaIndexedFindUsingInt(&pSlot->ppSearchableAttribMagnitudes, pRequest->eaiAttribs[i]);
		if (idx == -1)
			return false;

		if (pSlot->ppSearchableAttribMagnitudes[idx]->fMag <= 0.0f)
			return false;
	}

	return true;
}

// Send on list of auctions that are in mail for this account on to the chat server
// Used to find lots that have been orphaned
AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void Auction_ForwardMailAuctionLotsToChat(S32 iEntityID, S32 iAccountID)
{
	MailedAuctionLots *pLotList = StructCreate(parse_MailedAuctionLots);
	S64 iCount;
	
	iCount = AuctionServer_SearchLotsMailID(iAccountID, &pLotList->uLotIds);
	if(iCount > 0)
	{
		// send list to chat server
		pLotList->iOwnerAccountID = iAccountID;
		pLotList->iRecipientEntityID = iEntityID;
		pLotList->pchShardName = StructAllocString(GetShardNameFromShardInfoString());
		RemoteCommand_chatCommandRemote_CheckMailAuctionLots(GLOBALTYPE_CHATSERVER, 0, pLotList);
	}
	
	StructDestroy(parse_MailedAuctionLots, pLotList);
}

static int Auction_SortByPrice(const AuctionSearchRequest *pRequest, const AuctionLot **pLotA, const AuctionLot **pLotB)
{
	U32 iItemPriceA = (*pLotA)->pBiddingInfo ? MAX((*pLotA)->price, (*pLotA)->pBiddingInfo->iCurrentBid) : (*pLotA)->price;
	U32 iItemPriceB = (*pLotB)->pBiddingInfo ? MAX((*pLotB)->price, (*pLotB)->pBiddingInfo->iCurrentBid) : (*pLotB)->price;
	S32 iPriceComp = iItemPriceA - iItemPriceB;
	
	if (iPriceComp == 0)
	{
	
		if(eaSize(&(*pLotA)->ppItemsV2) < 1 || eaSize(&(*pLotB)->ppItemsV2) < 1 ||
			pRequest->stringLanguage >= eaSize(&(*pLotA)->ppItemsV2[0]->ppTranslatedNames) ||
			pRequest->stringLanguage >= eaSize(&(*pLotB)->ppItemsV2[0]->ppTranslatedNames)
			)
		{
			return 1;
		}
	
		return strcmp(NULL_TO_EMPTY((*pLotA)->ppItemsV2[0]->ppTranslatedNames[pRequest->stringLanguage]), NULL_TO_EMPTY((*pLotB)->ppItemsV2[0]->ppTranslatedNames[pRequest->stringLanguage]));
	}
	else
	{
		return iPriceComp;
	}	
}

static int Auction_SortByPriceDesc(const AuctionSearchRequest *pRequest, const AuctionLot **pLotA, const AuctionLot **pLotB)
{
	return -Auction_SortByPrice(pRequest, pLotA, pLotB);
}

static int Auction_SortByName(const AuctionSearchRequest *pRequest, const AuctionLot **pLotA, const AuctionLot **pLotB)
{
	if(eaSize(&(*pLotA)->ppItemsV2) < 1 || eaSize(&(*pLotB)->ppItemsV2) < 1 ||
		pRequest->stringLanguage >= eaSize(&(*pLotA)->ppItemsV2[0]->ppTranslatedNames) ||
		pRequest->stringLanguage >= eaSize(&(*pLotB)->ppItemsV2[0]->ppTranslatedNames)
		)
	{
		return 1;
	}
	
	{
		S32 iNameComp = strcmp(NULL_TO_EMPTY((*pLotA)->ppItemsV2[0]->ppTranslatedNames[pRequest->stringLanguage]), NULL_TO_EMPTY((*pLotB)->ppItemsV2[0]->ppTranslatedNames[pRequest->stringLanguage]));
		if (iNameComp == 0) {
			U32 iItemPriceA = (*pLotA)->pBiddingInfo ? MAX((*pLotA)->price, (*pLotA)->pBiddingInfo->iCurrentBid) : (*pLotA)->price;
			U32 iItemPriceB = (*pLotB)->pBiddingInfo ? MAX((*pLotB)->price, (*pLotB)->pBiddingInfo->iCurrentBid) : (*pLotB)->price;
			return iItemPriceA - iItemPriceB;
		} else {
			return iNameComp;
		}
	}
}

static int Auction_SortByNameDesc(const AuctionSearchRequest *pRequest, const AuctionLot **pLotA, const AuctionLot **pLotB)
{
	return -Auction_SortByName(pRequest, pLotA, pLotB);
}

static int Auction_SortByPricePerUnit(const AuctionSearchRequest *pRequest, const AuctionLot **pLotA, const AuctionLot **pLotB)
{
	S32 iPriceComp;
	S32 iItemCountA;
	S32 iItemCountB;
	U32 iItemPriceA = (*pLotA)->pBiddingInfo ? MAX((*pLotA)->price, (*pLotA)->pBiddingInfo->iCurrentBid) : (*pLotA)->price;
	U32 iItemPriceB = (*pLotB)->pBiddingInfo ? MAX((*pLotB)->price, (*pLotB)->pBiddingInfo->iCurrentBid) : (*pLotB)->price;
	
	if (eaSize(&(*pLotA)->ppItemsV2) > 0)
	{
		iItemCountA = MAX(1, (*pLotA)->ppItemsV2[0]->slot.pItem->count);
	}
	else
	{
		iItemCountA = 1;
	}

	if (eaSize(&(*pLotB)->ppItemsV2) > 0)
	{
		iItemCountB = MAX(1, (*pLotB)->ppItemsV2[0]->slot.pItem->count);
	}
	else
	{
		iItemCountB = 1;
	}
	
	if (iItemCountA > 1 || iItemCountB > 1)
	{
		iPriceComp = (iItemPriceA / iItemCountA) - (iItemPriceB / iItemCountB);
	}
	else
	{
		iPriceComp = iItemPriceA - iItemPriceB;
	}	

	if (iPriceComp == 0)
	{

		if(eaSize(&(*pLotA)->ppItemsV2) < 1 || eaSize(&(*pLotB)->ppItemsV2) < 1 ||
			pRequest->stringLanguage >= eaSize(&(*pLotA)->ppItemsV2[0]->ppTranslatedNames) ||
			pRequest->stringLanguage >= eaSize(&(*pLotB)->ppItemsV2[0]->ppTranslatedNames)
			)
		{
			return 1;
		}

		return strcmp(NULL_TO_EMPTY((*pLotA)->ppItemsV2[0]->ppTranslatedNames[pRequest->stringLanguage]), NULL_TO_EMPTY((*pLotB)->ppItemsV2[0]->ppTranslatedNames[pRequest->stringLanguage]));
	}
	else
	{
		return iPriceComp;
	}
}

static int Auction_SortByPricePerUnitDesc(const AuctionSearchRequest *pRequest, const AuctionLot **pLotA, const AuctionLot **pLotB)
{
	return -Auction_SortByPricePerUnit(pRequest, pLotA, pLotB);
}

static int Auction_SortByExpireTime(const AuctionSearchRequest *pRequest, const AuctionLot **pLotA, const AuctionLot **pLotB)
{
	S32 iExpireTimeComp = (*pLotA)->uExpireTime - (*pLotB)->uExpireTime;

	if (iExpireTimeComp == 0)
	{

		if(eaSize(&(*pLotA)->ppItemsV2) < 1 || eaSize(&(*pLotB)->ppItemsV2) < 1 ||
			pRequest->stringLanguage >= eaSize(&(*pLotA)->ppItemsV2[0]->ppTranslatedNames) ||
			pRequest->stringLanguage >= eaSize(&(*pLotB)->ppItemsV2[0]->ppTranslatedNames)
			)
		{
			return 1;
		}

		return strcmp(NULL_TO_EMPTY((*pLotA)->ppItemsV2[0]->ppTranslatedNames[pRequest->stringLanguage]), NULL_TO_EMPTY((*pLotB)->ppItemsV2[0]->ppTranslatedNames[pRequest->stringLanguage]));
	}
	else
	{
		return iExpireTimeComp;
	}	
}

static int Auction_SortByExpireTimeDesc(const AuctionSearchRequest *pRequest, const AuctionLot **pLotA, const AuctionLot **pLotB)
{
	return -Auction_SortByExpireTime(pRequest, pLotA, pLotB);
}

static int Auction_SortByItemNumericValue(const AuctionSearchRequest *pRequest, const AuctionLot **pLotA, const AuctionLot **pLotB)
{
	S32 iItemANumericValue = 0;
	S32 iItemBNumericValue = 0;
	S32 iNumericValueComp;

	if (eaSize(&(*pLotA)->ppItemsV2) > 0 && (*pLotA)->ppItemsV2[0]->slot.pItem)
	{
		iItemANumericValue = (*pLotA)->ppItemsV2[0]->slot.pItem->count;
	}

	if (eaSize(&(*pLotB)->ppItemsV2) > 0 && (*pLotB)->ppItemsV2[0]->slot.pItem)
	{
		iItemBNumericValue = (*pLotB)->ppItemsV2[0]->slot.pItem->count;
	}

	iNumericValueComp = iItemANumericValue - iItemBNumericValue;

	if (iNumericValueComp == 0)
	{

		if(eaSize(&(*pLotA)->ppItemsV2) < 1 || eaSize(&(*pLotB)->ppItemsV2) < 1 ||
			pRequest->stringLanguage >= eaSize(&(*pLotA)->ppItemsV2[0]->ppTranslatedNames) ||
			pRequest->stringLanguage >= eaSize(&(*pLotB)->ppItemsV2[0]->ppTranslatedNames)
			)
		{
			return 1;
		}

		return strcmp(NULL_TO_EMPTY((*pLotA)->ppItemsV2[0]->ppTranslatedNames[pRequest->stringLanguage]), NULL_TO_EMPTY((*pLotB)->ppItemsV2[0]->ppTranslatedNames[pRequest->stringLanguage]));
	}
	else
	{
		return iNumericValueComp;
	}	
}

static int Auction_SortByItemNumericValueDesc(const AuctionSearchRequest *pRequest, const AuctionLot **pLotA, const AuctionLot **pLotB)
{
	return -Auction_SortByItemNumericValue(pRequest, pLotA, pLotB);
}

// Returns list of lots that satisfy search request
AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
ACMD_STATIC_RETURN AuctionLotList* Auction_GetLots(AuctionSearchRequest *pRequest)
{
	S64 iCount = 0;
	static AuctionLotList *s_pSearchLotList = NULL;
	static int timer = -1;
	char *estrSearchType = NULL;
	
	if (!s_pSearchLotList) {
		s_pSearchLotList = StructCreate(parse_AuctionLotList);
	}
	eaClearFast(&s_pSearchLotList->ppAuctionLots);
	ea32Clear(&s_pSearchLotList->piAuctionLotsCounts);
	s_pSearchLotList->hitcount = 0;
	s_pSearchLotList->realHitcount = 0;
	s_pSearchLotList->maxResults = 0;
	s_pSearchLotList->timeTaken = 0;
	s_pSearchLotList->cooldownEnd = 0;
	ea32ClearFast(&s_pSearchLotList->eaiNonExistingAuctionLotContainerIDs);
	
	if (!pRequest)
	{
		return s_pSearchLotList;
	}

	PERFINFO_AUTO_START_FUNC();
	
	if (timer == -1)
		timer = timerAlloc();
	timerStart(timer);
	
	PERFINFO_AUTO_START("Search", 1);
	s_currentSearchTable = NULL;
	if (pRequest->stringSearch)
	{	//TODO: We'll need to explode this searchstring for tokens and search on each
		Auction_STC_Increment(pRequest->stringSearch);
	}
	iCount = AuctionServer_SearchLots(s_pSearchLotList, pRequest, Auction_FilterForSearch, &estrSearchType);
	s_currentSearchTable = NULL;
	PERFINFO_AUTO_STOP();
	
	if (s_pSearchLotList->ppAuctionLots && eaSize(&s_pSearchLotList->ppAuctionLots)) {
		PERFINFO_AUTO_START("Sort", 1);

		switch(pRequest->eSortColumn)
		{
		case AuctionSort_Price:
			eaQSort_s(s_pSearchLotList->ppAuctionLots, Auction_SortByPrice, pRequest);
			break;
		case AuctionSort_PriceDesc:
			eaQSort_s(s_pSearchLotList->ppAuctionLots, Auction_SortByPriceDesc, pRequest);
			break;

		case AuctionSort_PricePerUnit:
			eaQSort_s(s_pSearchLotList->ppAuctionLots, Auction_SortByPricePerUnit, pRequest);
			break;
		case AuctionSort_PricePerUnitDesc:
			eaQSort_s(s_pSearchLotList->ppAuctionLots, Auction_SortByPricePerUnitDesc, pRequest);
			break;

		case AuctionSort_Name:
			eaQSort_s(s_pSearchLotList->ppAuctionLots, Auction_SortByName, pRequest);
			break;
		case AuctionSort_NameDesc:
			eaQSort_s(s_pSearchLotList->ppAuctionLots, Auction_SortByNameDesc, pRequest);
			break;

		case AuctionSort_ExpireTimeValueAsc:
			eaQSort_s(s_pSearchLotList->ppAuctionLots, Auction_SortByExpireTime, pRequest);
			break;
		case AuctionSort_ExpireTimeValueDesc:
			eaQSort_s(s_pSearchLotList->ppAuctionLots, Auction_SortByExpireTimeDesc, pRequest);
			break;

		case AuctionSort_ItemNumericValueAsc:
			eaQSort_s(s_pSearchLotList->ppAuctionLots, Auction_SortByItemNumericValue, pRequest);
			break;
		case AuctionSort_ItemNumericValueDesc:
			eaQSort_s(s_pSearchLotList->ppAuctionLots, Auction_SortByItemNumericValueDesc, pRequest);
			break;
		}

		PERFINFO_AUTO_STOP();
	}
	s_pSearchLotList->maxResults = MAX_AUCTION_RETURN_RESULTS;
	s_pSearchLotList->timeTaken = timerElapsed(timer);

	timerPause(timer);
	
	LogAuctionSearchRequest(estrSearchType, s_pSearchLotList, pRequest);
	estrDestroy(&estrSearchType);
	
	PERFINFO_AUTO_STOP();
	return s_pSearchLotList;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
int Auction_GetLotSize(U32 lotID)
{
	Container *con = objGetContainer(GLOBALTYPE_AUCTIONLOT, lotID);
	if (con)
	{
		return eaSize(&((AuctionLot*) con->containerData)->ppItemsV2);
	}
	return 0;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
AuctionLot * Auction_GetLot(U32 lotID)
{
	Container *pCon = objGetContainer(GLOBALTYPE_AUCTIONLOT, lotID);
	AuctionLot *pStoredLot;
	if (pCon)
	{
		pStoredLot = (AuctionLot *)pCon->containerData;
		return StructClone(parse_AuctionLot, pStoredLot);
	}
	return NULL;
}

//comma-separated list of Blocked items for the AuctionServer
static char *spAuctionBlockedItems = NULL;

AUTO_CMD_ESTRING(spAuctionBlockedItems, AuctionBlockedItems) ACMD_AUTO_SETTING(Misc, AUCTIONSERVER) ACMD_CALLBACK(Auction_BlockedItemsChanged);


void Auction_BlockedItemsChanged(CMDARGS)
{

	eaDestroyStruct(&gBlockedItems.eaBlockedItems, parse_AuctionBlockedItem);

	if(spAuctionBlockedItems)
	{
		char **ppStrings = NULL;
		S32 i;

		DivideString(spAuctionBlockedItems, ",", &ppStrings, DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE);

		for(i = 0; i < eaSize(&ppStrings); ++i)
		{
			ItemDef *pItemDef = (ItemDef*)RefSystem_ReferentFromString(g_hItemDict, ppStrings[i]);	
			if(pItemDef)
			{
				if(!eaIndexedGetUsingString(&gBlockedItems.eaBlockedItems, ppStrings[i]))
				{
					AuctionBlockedItem *pBlockedItem = StructCreate(parse_AuctionBlockedItem);
					pBlockedItem->pcItemName = allocAddString(ppStrings[i]);
					if(!gBlockedItems.eaBlockedItems)
					{
						eaIndexedEnable(&gBlockedItems.eaBlockedItems, parse_AuctionBlockedItem);
					}

					eaIndexedAdd(&gBlockedItems.eaBlockedItems, pBlockedItem);
				}
			}
			else
			{
				printf("Error (AuctionServer Block Item): %s is not a valid item.\n", ppStrings[i]);
			}
		}
		eaDestroyEx(&ppStrings, NULL);	
	}
}

#include "AutoGen/AuctionServer_h_ast.c"