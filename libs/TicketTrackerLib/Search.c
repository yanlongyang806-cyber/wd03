#include "Search.h"

#include "trivia.h"
#include "earray.h"
#include "estring.h"
#include "utils.h"
#include "TicketEntry.h"
#include "TicketTracker.h"
#include "timing.h"
#include "wininclude.h"
//#include <winsock.h>
#include "objContainer.h"
#include "Authentication.h"
#include "Category.h"
#include "EntityDescriptor.h"
#include "StashTable.h"
#include "StringUtil.h"
#include "Message.h"
#include "AppLocale.h"
#include "AutoGen\ticketenums_h_ast.h"

#include "AutoGen/TicketEntry_h_ast.h"
#include "autogen/trivia_h_ast.h"
#include "jira.h"
#include "AutoGen/jira_h_ast.h"

// -------------------------------------------------------------------------------------------
// Sorting comparison function macros. I completely agree with you, this code is very ugly.

#define BASIC_SORT_BY_INT_FUNC(FUNCNAME, VARIABLE) \
int FUNCNAME(const TicketEntry **pEntry1, const TicketEntry **pEntry2, const void *ign) { \
	if      ((*pEntry1)->VARIABLE < (*pEntry2)->VARIABLE) return -1; \
	else if ((*pEntry1)->VARIABLE > (*pEntry2)->VARIABLE) return  1; \
	else return 0; }

#define BASIC_SORT_BY_INT_FUNC_DESC(FUNCNAME, VARIABLE) \
int FUNCNAME(const TicketEntry **pEntry1, const TicketEntry **pEntry2, const void *ign) { \
	if      ((*pEntry1)->VARIABLE < (*pEntry2)->VARIABLE) return  1; \
	else if ((*pEntry1)->VARIABLE > (*pEntry2)->VARIABLE) return -1; \
	else return 0; }

#define BASIC_SORT_BY_STR_FUNC(FUNCNAME, VARIABLE) \
int FUNCNAME(const TicketEntry **pEntry1, const TicketEntry **pEntry2, const void *ign) { \
	return -1 * strcmp((*pEntry1)->VARIABLE, (*pEntry2)->VARIABLE); }

#define BASIC_SORT_BY_STR_FUNC_DESC(FUNCNAME, VARIABLE) \
int FUNCNAME(const TicketEntry **pEntry1, const TicketEntry **pEntry2, const void *ign) { \
	return strcmp((*pEntry1)->VARIABLE, (*pEntry2)->VARIABLE); }

#define DO_SORT(FUNCNAME, DESCENDING) \
	if(DESCENDING) {eaStableSort(ppEntries, NULL, FUNCNAME ## Desc);} \
	else           {eaStableSort(ppEntries, NULL, FUNCNAME);}

// -------------------------------------------------------------------------------------------
// Sorting comparison functions, defined by the above macros. This is prettier.

BASIC_SORT_BY_INT_FUNC     (SortByNewest,     uFiledTime);
BASIC_SORT_BY_INT_FUNC_DESC(SortByNewestDesc, uFiledTime);

BASIC_SORT_BY_INT_FUNC     (SortByID,      uID);
BASIC_SORT_BY_INT_FUNC_DESC(SortByIDDesc,  uID);

BASIC_SORT_BY_INT_FUNC     (SortByGroupID,      uGroupID);
BASIC_SORT_BY_INT_FUNC_DESC(SortByGroupIDDesc,  uGroupID);

BASIC_SORT_BY_INT_FUNC     (SortByPriority,     uPriority);
BASIC_SORT_BY_INT_FUNC_DESC(SortByPriorityDesc, uPriority);

BASIC_SORT_BY_INT_FUNC     (SortByOccurrences,     uOccurrences);
BASIC_SORT_BY_INT_FUNC_DESC(SortByOccurrencesDesc, uOccurrences);

int SortByRepName(const TicketEntry **pEntry1, const TicketEntry **pEntry2, const void *ign)
{
	//TicketTrackerUser *pUser1 = findUserByID((*pEntry1)->uID);
	//TicketTrackerUser *pUser2 = findUserByID((*pEntry2)->uID);
	const char *pUser1 = (*pEntry1)->pRepAccountName;
	const char *pUser2 = (*pEntry2)->pRepAccountName;

	if (!pUser1)
		return -1;
	if (!pUser2)
		return 1;
	return -1 * strcmp(pUser1, pUser2);
}

int SortByRepNameDesc(const TicketEntry **pEntry1, const TicketEntry **pEntry2, const void *ign)
{
	return -1 * SortByRepName(pEntry1, pEntry2, ign);
}

// -------------------------------------------------------------------------------------------
// Sorting code

static const char * SortOrderToStringTable[SORTORDER_MAX] = {
	"Newest",     // SORTORDER_NEWEST
	"ID",         // SORTORDER_ID
	"Group",      // SORTORDER_GROUPID
	"User",       // SORTORDER_REPNAME
	"Priority",   // SORTORDER_PRIORITY
	"Occurrences", // SORTORDER_COUNT
};

const char * sortOrderToString(SortOrder eSortOrder)
{
	return SortOrderToStringTable[eSortOrder];
}

const char * sortOrderEntireEnumString(void)
{
	static char *pEntireEnumString = NULL;
	int i;

	if(pEntireEnumString == NULL)
	{
		// Lazy initialization
		estrPrintf(&pEntireEnumString, "%s", SortOrderToStringTable[0]);
		for(i=1; i<SORTORDER_MAX; i++)
		{
			estrConcatf(&pEntireEnumString, "|%s", SortOrderToStringTable[i]);
		}
	}

	return pEntireEnumString;
}


static void sortBySortOrder(TicketEntry **ppEntries, SortOrder eSortOrder, bool bDescending)
{
	switch(eSortOrder)
	{
		xcase SORTORDER_NEWEST:		DO_SORT(SortByNewest,  bDescending);
		xcase SORTORDER_ID:  		DO_SORT(SortByID,      bDescending);
		xcase SORTORDER_GROUPID:    DO_SORT(SortByGroupID, bDescending);
		xcase SORTORDER_REPNAME:    DO_SORT(SortByRepName, bDescending);
		xcase SORTORDER_PRIORITY:   DO_SORT(SortByPriority,bDescending);
		xcase SORTORDER_COUNT:		DO_SORT(SortByOccurrences, bDescending);

		xdefault:					printf("sortBySortOrder(): Unknown sort order %d\n", eSortOrder);
	};
}

// -------------------------------------------------------------------------------------------
// Search Code

static StashTable sCategoryTicketStash = NULL;
static CRITICAL_SECTION sSearchAccess;

typedef struct TicketStatusCount
{
	STRING_POOLED productName;
	int piTicketStatusCount[TICKETSTATUS_COUNT][LOCALE_ID_COUNT];
	int uTotal;
} TicketStatusCount;

typedef struct TicketSearchCategoryCache
{
	int iMainCategoryIndex;
	int uTicketCount;
	CONTAINERID_EARRAY eaiEntryIDs;

	StashTable pSubcategoryTicketStash;
	StashTable pStatusCountByProduct;
	//int piTicketStatusCount[TICKETSTATUS_COUNT][LOCALE_ID_COUNT]; // deprecated
} TicketSearchCategoryCache;


static TicketSearchCategoryCache* searchCacheFindCategoryCacheInternal(SA_PARAM_NN_VALID Category *mainCategory, Category *category)
{
	TicketSearchCategoryCache *pCache = NULL;
	Message *ms;
	const char *pMainCategoryKey = NULL, *pSubcategoryKey = NULL;
	
	ms = GET_REF(mainCategory->hDisplayName);
	if (!ms)
	{
		Errorf("Missing translation for a main category!");
		return NULL;
	}
	pMainCategoryKey = ms->pcMessageKey;
	if (category)
	{
		ms = GET_REF(category->hDisplayName);
		if (!ms)
		{
			Errorf("Missing translation for a subcategory!");
			return NULL;
		}
		pSubcategoryKey = ms->pcMessageKey;
	}

	stashFindPointer(sCategoryTicketStash, pMainCategoryKey, &pCache);
	if (pCache)
	{
		TicketSearchCategoryCache *pSubCache = NULL;
		if (!pSubcategoryKey)
			return pCache;
		
		stashFindPointer(pCache->pSubcategoryTicketStash, pSubcategoryKey, &pSubCache);
		if (pSubCache)
			return pSubCache;
		return NULL; // could not find subcategory, no tickets exist for it
	}
	return NULL; // could not find main category, no tickets exist for it
}

TicketSearchCategoryCache* searchCacheFindCategoryCache(const char *pMainCategoryKey, const char *pSubcategoryKey)
{
	Category *mainCategory;
	Category *subcategory = NULL;

	if (!pMainCategoryKey || !*pMainCategoryKey)
		return NULL;
	mainCategory = getMainCategory(pMainCategoryKey);
	if (!mainCategory)
	{
		Errorf("Invalid main category: (%s, %s)", pMainCategoryKey, pSubcategoryKey);
		return NULL;
	}
	if (pSubcategoryKey && *pSubcategoryKey)
	{
		subcategory = getCategoryFromMain(mainCategory, pSubcategoryKey);
		if (!subcategory && stricmp(pMainCategoryKey, "CBug.CategoryMain.Stickies") != 0)
		{   // Don't throw this error for stickies
			Errorf("Invalid category pair: (%s, %s)", pMainCategoryKey, pSubcategoryKey);
			return NULL;
		}
	}
	return searchCacheFindCategoryCacheInternal(mainCategory, subcategory);
}

__forceinline static int searchCacheSumStatus (TicketSearchCategoryCache * cache, TicketStatus eStatus)
{
	int iCount = 0, i;
	StashTableIterator iter = {0};
	StashElement elem;

	if (!cache->pStatusCountByProduct)
		return 0;
	if (eStatus <= 0 || eStatus >= TICKETSTATUS_COUNT)
		return cache->uTicketCount;

	stashGetIterator(cache->pStatusCountByProduct, &iter);
	while (stashGetNextElement(&iter, &elem))
	{
		TicketStatusCount *statusCount = stashElementGetPointer(elem);
		for (i=0; i<LOCALE_ID_COUNT; i++)
			iCount += statusCount->piTicketStatusCount[eStatus][i];
	}
	return iCount;
}

__forceinline static int searchCacheSumStatusByProduct (TicketSearchCategoryCache * cache, TicketStatus eStatus, const char *productName)
{
	int iCount = 0, i;
	TicketStatusCount *statusCount = NULL;
	if (!cache->pStatusCountByProduct)
		return 0;
	stashFindPointer(cache->pStatusCountByProduct, productName, &statusCount);
	if (!statusCount)
		return 0;
	if (eStatus <= 0 || eStatus >= TICKETSTATUS_COUNT)
		return statusCount->uTotal;
	for (i=0; i<LOCALE_ID_COUNT; i++)
		iCount += statusCount->piTicketStatusCount[eStatus][i];
	return iCount;
}

// Creates it if it doesn't already exist
static TicketStatusCount *searchCacheFindProductStatusCount(TicketSearchCategoryCache *cache, const char *productName, bool bCreate)
{
	TicketStatusCount *pProductCount = NULL;
	if (cache->pStatusCountByProduct)
		stashFindPointer(cache->pStatusCountByProduct, productName ? productName : "", &pProductCount);
	else
		cache->pStatusCountByProduct = stashTableCreateWithStringKeys(10, StashDefault);
	if (!pProductCount && bCreate)
	{
		pProductCount = calloc(1, sizeof(TicketStatusCount));
		pProductCount->productName = productName;
		stashAddPointer(cache->pStatusCountByProduct, pProductCount->productName, pProductCount, 0);
	}
	return pProductCount;
}

int searchCacheGetCategoryCountForProductLocale(const char *pMainCategoryKey, const char *pSubcategoryKey, TicketStatus eStatus, int iLocaleID, const char *productName)
{
	TicketSearchCategoryCache * cache;
	Category *mainCategory = getMainCategory(pMainCategoryKey);
	Category *subcategory = NULL;

	if (!mainCategory)
	{
		Errorf("Invalid main category: (%s, %s)", pMainCategoryKey, pSubcategoryKey);
		return -1;
	}
	if (pSubcategoryKey && *pSubcategoryKey)
	{
		subcategory = getCategoryFromMain(mainCategory, pSubcategoryKey);
		if (!subcategory && stricmp(pMainCategoryKey, "CBug.CategoryMain.Stickies") != 0)
		{   // Don't throw this error for stickies
			Errorf("Invalid category pair: (%s, %s)", pMainCategoryKey, pSubcategoryKey);
			return -1;
		}
	}
	if (eStatus <= 0 || eStatus >= TICKETSTATUS_COUNT)
	{
		Errorf("Invalid status specified.");
		return -1;
	}
	if (iLocaleID < 0 || iLocaleID >= LOCALE_ID_COUNT)
	{
		Errorf("Invalid locale ID specified.");
		return -1;
	}

	cache = searchCacheFindCategoryCacheInternal(mainCategory, subcategory);
	if (cache)
	{
		if (!cache->pStatusCountByProduct)
			return 0;
		if (productName)
		{
			TicketStatusCount *statusCount = NULL;
			stashFindPointer(cache->pStatusCountByProduct, productName, &statusCount);
			if (!statusCount)
				return 0;
			return statusCount->piTicketStatusCount[eStatus][iLocaleID];
		}
		else
		{
			StashTableIterator iter = {0};
			StashElement elem;
			int iCount = 0;
			stashGetIterator(cache->pStatusCountByProduct, &iter);
			while (stashGetNextElement(&iter, &elem))
			{
				TicketStatusCount *statusCount = stashElementGetPointer(elem);
				iCount += statusCount->piTicketStatusCount[eStatus][iLocaleID];
			}
			return iCount;
		}
	}
	return 0;
}

int searchCacheGetCategoryCountForLocale(const char *pMainCategoryKey, const char *pSubcategoryKey, TicketStatus eStatus, int iLocaleID)
{
	return searchCacheGetCategoryCountForProductLocale(pMainCategoryKey, pSubcategoryKey, eStatus, iLocaleID, NULL);
}

int searchCacheGetCategoryCountByProduct(const char *pMainCategoryKey, const char *pSubcategoryKey, TicketStatus eStatus, const char *productName)
{
	TicketSearchCategoryCache * cache;
	Category *mainCategory = getMainCategory(pMainCategoryKey);
	Category *subcategory = NULL;

	if (!mainCategory)
	{
		Errorf("Invalid main category: (%s, %s)", pMainCategoryKey, pSubcategoryKey);
		return -1;
	}
	if (pSubcategoryKey && *pSubcategoryKey)
	{
		subcategory = getCategoryFromMain(mainCategory, pSubcategoryKey);
		if (!subcategory && stricmp(pMainCategoryKey, "CBug.CategoryMain.Stickies") != 0)
		{   // Don't throw this error for stickies
			Errorf("Invalid category pair: (%s, %s)", pMainCategoryKey, pSubcategoryKey);
			return -1;
		}
	}

	cache = searchCacheFindCategoryCacheInternal(mainCategory, subcategory);
	if (cache)
	{
		if (productName)
			return searchCacheSumStatusByProduct(cache, eStatus, productName);
		else
			return searchCacheSumStatus(cache, eStatus);
	}
	return 0;
}

int searchCacheGetCategoryCount(const char *pMainCategoryKey, const char *pSubcategoryKey, TicketStatus eStatus)
{
	return searchCacheGetCategoryCountByProduct(pMainCategoryKey, pSubcategoryKey, eStatus, NULL);
}

void initializeEntries(void)
{
	ContainerIterator iter = {0};
	Container *con;

	sCategoryTicketStash = stashTableCreateWithStringKeys(eaSize(getCategories()), StashDeepCopyKeys_NeverRelease);

	objInitContainerIteratorFromType(GLOBALTYPE_TICKETENTRY, &iter);
	con = objGetNextContainerFromIterator(&iter);
	while (con)
	{
		TicketEntry * pEntry = CONTAINER_ENTRY(con);
		searchAddTicket(pEntry);
		con = objGetNextContainerFromIterator(&iter);
	}
	objClearContainerIterator(&iter);

	resortPriorities();
	InitializeCriticalSection(&sSearchAccess);
}

void searchAddTicket(TicketEntry *pEntry)
{
	TicketSearchCategoryCache *pCache = NULL;
	int localeID = locGetIDByLanguage(pEntry->eLanguage);
	TicketStatusCount *pProductCount = NULL;

	if (!pEntry->pMainCategory)
		return;
	stashFindPointer(sCategoryTicketStash, pEntry->pMainCategory, &pCache);
	if (!pCache)
	{
		pCache = calloc(1, sizeof(TicketSearchCategoryCache));
		stashAddPointer(sCategoryTicketStash, pEntry->pMainCategory, pCache, false);
	}
	if (pEntry->pCategory)
	{
		TicketSearchCategoryCache *pSubCache = NULL;
		Category *category = getMainCategory(pEntry->pMainCategory);

		if (!pCache->pSubcategoryTicketStash)
			pCache->pSubcategoryTicketStash = stashTableCreateWithStringKeys (category ? eaSize(&category->ppSubCategories): 10, StashDeepCopyKeys_NeverRelease);
		stashFindPointer(pCache->pSubcategoryTicketStash, pEntry->pCategory, &pSubCache);

		if (!pSubCache)
		{
			pSubCache = calloc(1, sizeof(TicketSearchCategoryCache));
			stashAddPointer(pCache->pSubcategoryTicketStash, pEntry->pCategory, pSubCache, false);
		}
		eaiPush(&pSubCache->eaiEntryIDs, pEntry->uID);
		pProductCount = searchCacheFindProductStatusCount(pSubCache, pEntry->pProductName, true);
		pProductCount->piTicketStatusCount[pEntry->eStatus][localeID]++;
		pProductCount->uTotal++;
		pSubCache->uTicketCount++;
	}
	eaiPush(&pCache->eaiEntryIDs, pEntry->uID);
	pProductCount = searchCacheFindProductStatusCount(pCache, pEntry->pProductName, true);
	pProductCount->piTicketStatusCount[pEntry->eStatus][localeID]++;
	pProductCount->uTotal++;
	pCache->uTicketCount++;
}

void searchRemoveTicket(TicketEntry *pEntry)
{
	TicketSearchCategoryCache *pCache = NULL;
	int localeID = locGetIDByLanguage(pEntry->eLanguage);

	stashFindPointer(sCategoryTicketStash, pEntry->pMainCategory, &pCache);
	if (pCache)
	{
		TicketSearchCategoryCache *pSubCache = NULL;
		TicketStatusCount *pProductCount = NULL;
	
		if (eaiFindAndRemove(&pCache->eaiEntryIDs, pEntry->uID) != -1)
		{
			pProductCount = searchCacheFindProductStatusCount(pCache, pEntry->pProductName, false);
			if (pProductCount)
			{
				pProductCount->piTicketStatusCount[pEntry->eStatus][localeID]--;
				pProductCount->uTotal--;
			}
			pCache->uTicketCount--;
		}
		stashFindPointer(pCache->pSubcategoryTicketStash, pEntry->pCategory, &pSubCache);

		if (pSubCache)
		{
			if (eaiFindAndRemove(&pSubCache->eaiEntryIDs, pEntry->uID) != -1)
			{
				pProductCount = searchCacheFindProductStatusCount(pSubCache, pEntry->pProductName, false);
				if (pProductCount)
				{
					pProductCount->piTicketStatusCount[pEntry->eStatus][localeID]--;
					pProductCount->uTotal--;
				}
				pSubCache->uTicketCount--;
			}
		}
	}
}

void searchChangeTicketStatus (TicketEntry *pEntry, TicketStatus ePrevStatus)
{
	TicketSearchCategoryCache *pCache = NULL;
	int localeID = locGetIDByLanguage(pEntry->eLanguage);
	if (pEntry->eStatus == ePrevStatus || ePrevStatus >= TICKETSTATUS_COUNT || pEntry->eStatus >= TICKETSTATUS_COUNT)
		return;
	stashFindPointer(sCategoryTicketStash, pEntry->pMainCategory, &pCache);
	if (pCache)
	{
		TicketSearchCategoryCache *pSubCache = NULL;
		TicketStatusCount *pProductCount = searchCacheFindProductStatusCount(pCache, pEntry->pProductName, false);

		if (pProductCount)
		{
			pProductCount->piTicketStatusCount[ePrevStatus][localeID]--;
			pProductCount->piTicketStatusCount[pEntry->eStatus][localeID]++;
		}

		stashFindPointer(pCache->pSubcategoryTicketStash, pEntry->pCategory, &pSubCache);

		if (pSubCache)
		{
			pProductCount = searchCacheFindProductStatusCount(pSubCache, pEntry->pProductName, false);
			if (pProductCount)
			{
				pProductCount->piTicketStatusCount[ePrevStatus][localeID]--;
				pProductCount->piTicketStatusCount[pEntry->eStatus][localeID]++;
			}
		}
	}
}

#define SORT_PERIOD 1800 // resort every half hour
void resortPriorities(void)
{
	static U32 suLastSortTime = 0;
	U32 uTime = timeSecondsSince2000();

	if (uTime - suLastSortTime > SORT_PERIOD)
	{
		StashTableIterator iter = {0};
		StashElement elem;
		stashGetIterator(sCategoryTicketStash, &iter);

		while (stashGetNextElement(&iter, &elem))
		{
			TicketSearchCategoryCache *pCache = (TicketSearchCategoryCache*) stashElementGetPointer(elem);
			StashTableIterator subiter = {0};
			StashElement subelem;

			TicketEntry_CalculateAndSortByPriority(&pCache->eaiEntryIDs);
			stashGetIterator(pCache->pSubcategoryTicketStash, &subiter);

			while (stashGetNextElement(&subiter, &subelem))
			{
				TicketSearchCategoryCache *pSubcache = (TicketSearchCategoryCache*) stashElementGetPointer(subelem);

				TicketEntry_CalculateAndSortByPriority(&pSubcache->eaiEntryIDs);
			}
		}
		suLastSortTime = uTime;
	}
}

static bool searchMatches(SearchData *pData, TicketEntry *pEntry)
{
	int i;
	int uTime;
	U32 uOccurrences = pEntry->uOccurrences;

	if (pData->eAdminSearch < SEARCH_ADMIN && pEntry->eVisibility == TICKETVISIBLE_HIDDEN)
		return false; // Hide hidden tickets
	if (pData->eAdminSearch < SEARCH_USER && pEntry->eVisibility == TICKETVISIBLE_PRIVATE)
		return false; // Hide private tickets from basic user searches
	if (pData->uFlags & SEARCHFLAG_VISIBILITY)
	{
		if (pData->iVisible-1 != pEntry->eVisibility)
			return false;
	}

	if (pData->eAdminSearch == SEARCH_DEFAULT)
	{
		if (!pEntry->pMainCategory)
			return false;
		if (stricmp(pEntry->pMainCategory, "CBug.CategoryMain.Stickies") == 0)
		{
			for (i=eaSize(&pData->ppCategories)-1; i>=0; i--)
			{
				SearchCategory *pCategory = pData->ppCategories[i];
				const char *pSubKey = subcategoryGetKey(pCategory->iMainCategory, pCategory->iCategory);

				if(pData->uFlags & SEARCHFLAG_PRODUCT_NAME)
				{
					if(pEntry->pProductName == NULL) return false;
					if(!strstri(pEntry->pProductName, pData->pProductName))
						return false;
				}
				if (pSubKey &&  stricmp(pEntry->pCategory, pSubKey ) == 0)
					return true; // always return stickies on basic user searches when subcategories match
			}
		}
	}

	if (pData->searchFunc)
	{
		return pData->searchFunc(pEntry, pData->userData);
	}

	uTime = timeSecondsSince2000();

	if (pData->iJira)
	{
		if (pData->iJira == 1 && pEntry->pJiraIssue == NULL)
			return false;
		if (pData->iJira == 2 && pEntry->pJiraIssue != NULL)
			return false;
	}

	if (pData->eInternal)
	{
		if (pData->eInternal == TICKETSEARCH_INTERNAL)
		{
			if ((pEntry->uFlags & TICKET_FLAG_INTERNAL) == 0)
				return false;
		}
		else if (pData->eInternal == TICKETSEARCH_EXTERNAL)
		{
			if ((pEntry->uFlags & TICKET_FLAG_INTERNAL) != 0)
				return false;
		}
	}

	// This checks if we are filtering on anything that is per-TicketClientUserInfo, and adjusts
	// uOccurrences based on the resultant filter.
	if (pData->uFlags & 
		( SEARCHFLAG_DATESTART | SEARCHFLAG_DATEEND | 
		SEARCHFLAG_SHARD | SEARCHFLAG_CHARACTER_NAME | SEARCHFLAG_ACCOUNT_NAME | SEARCHFLAG_VERSION))
	{
		int lastIndex = eaSize(&pEntry->ppUserInfo)-1;
		bool bIsDateSearch = (pData->uFlags & ( SEARCHFLAG_DATESTART | SEARCHFLAG_DATEEND )) != 0;
		uOccurrences = 0;

		if (lastIndex < 0)
			return false; // No user infos

		if (!bIsDateSearch || pData->eDateType == DATESEARCH_EVERYTHING || pData->eDateType == DATESEARCH_STATUS_UPDATED)
		{
			for (i=lastIndex; i>=0; i--)
			{
				if(UserInfoMatchesFilter(pData, pEntry, (TicketClientUserInfo*)pEntry->ppUserInfo[i]))
				{
					uOccurrences++;
				}
			}
		}
		else if (pData->eDateType == DATESEARCH_FILED)
		{
			if (UserInfoMatchesFilter(pData, pEntry, (TicketClientUserInfo*)pEntry->ppUserInfo[0]))
				uOccurrences++;
		}
		else if (pData->eDateType == DATESEARCH_LAST_UPDATED)
		{
			int iLastUpdateIdx;
			for (iLastUpdateIdx=0; iLastUpdateIdx<lastIndex; iLastUpdateIdx++)
			{
				if (pEntry->ppUserInfo[iLastUpdateIdx]->uUpdateTime == pEntry->uLastTime)
					break;
			}
			if (UserInfoMatchesFilter(pData, pEntry, (TicketClientUserInfo*)pEntry->ppUserInfo[iLastUpdateIdx]))
				uOccurrences++;
		}

		if (uOccurrences == 0)
			return false;
	}

	if (pData->uFlags & SEARCHFLAG_STATUSUPDATE)
	{
		int statusSize;
		if (statusSize = eaSize(&pEntry->ppStatusLog))
		{
			if(pData->uStatusDateStart && pEntry->ppStatusLog[statusSize-1]->uTime < pData->uStatusDateStart)
				return false;
			if(pData->uStatusDateEnd && pEntry->ppStatusLog[statusSize-1]->uTime > pData->uStatusDateEnd)
				return false;
		}
		else
		{
			if(pData->uStatusDateStart && pEntry->uFiledTime < pData->uStatusDateStart)
				return false;
			if(pData->uStatusDateEnd && pEntry->uFiledTime > pData->uStatusDateEnd)
				return false;
		}
	}

	if (pData->uFlags & SEARCHFLAG_MODIFIED)
	{
		U32 uModifiedTime = pEntry->uLastModifiedTime ? pEntry->uLastModifiedTime : pEntry->uFiledTime;
		if(pData->uModifiedDateStart && uModifiedTime < pData->uModifiedDateStart)
			return false;
		if(pData->uModifiedDateEnd && uModifiedTime > pData->uModifiedDateEnd)
			return false;
	}

	if (pData->uFlags & SEARCHFLAG_OCCURRENCES)
	{
		// uOccurrences is already properly filtered at this point, if necessary
		if (uOccurrences < (U32) pData->iOccurrences)
		{
			return false;
		}
	}

	if (pData->uFlags & SEARCHFLAG_STATUS)
	{
		bool bFound = false;
		for (i=eaiSize(&pData->peStatuses)-1; i>=0; i--)
		{
			if(pEntry->eStatus == pData->peStatuses[i])
			{
				bFound = true;
				break;
			}
		}
		if (!bFound) return false;
	}

	if (pData->uFlags & SEARCHFLAG_CLOSEDTIME)
	{
		if (!TICKET_STATUS_IS_CLOSED(pEntry->eStatus))
			return false;
		if(pData->uClosedStart > 0 && pEntry->uEndTime < pData->uClosedStart)
			return false;
		if(pData->uClosedEnd > 0 && pEntry->uEndTime > pData->uClosedEnd)
			return false;
	}

	if(pData->uFlags & SEARCHFLAG_SUMMARY_DESCRIPTION)
	{
		switch(pData->iSearchSummaryDescription)
		{
		case SEARCH_SUMMARY:
			{
				if(pEntry->pSummary == NULL)
				{
					return false;
				}
				if(!StringKeywordSearchMatches(pEntry->pSummary, pData->pSummaryDescription))
				{
					return false;
				}
			}
			break;
		case SEARCH_DESCRIPTION:
			{
				if(pEntry->pUserDescription == NULL)
				{
					return false;
				}
				if(!StringKeywordSearchMatches(pEntry->pUserDescription, pData->pSummaryDescription))
				{
					return false;
				}
			}
			break;
		case SEARCH_SUMMARY_DESCRIPTION:
			{
				if ( (pEntry->pSummary == NULL || !StringKeywordSearchMatches(pEntry->pSummary, pData->pSummaryDescription)) &&
					(pEntry->pUserDescription == NULL || !StringKeywordSearchMatches(pEntry->pUserDescription, pData->pSummaryDescription)) )
				{
					return false;
				}
			}
			break;
		}
	}

	if (pData->uFlags & SEARCHFLAG_LABEL)
	{
		if (pData->pLabel && (!pEntry->pLabel || !strstri(pEntry->pLabel, pData->pLabel)) )
			return false;
	}

	if (pData->uFlags & SEARCHFLAG_CATEGORY)
	{
		int iMainIndex = categoryGetIndex(pEntry->pMainCategory);
		int iIndex = subcategoryGetIndex(iMainIndex, pEntry->pCategory);
		bool bFound = false;
		
		for (i=eaSize(&pData->ppCategories)-1; i>=0; i--)
		{
			SearchCategory *pCategory = pData->ppCategories[i];
			if (pCategory->iMainCategory > -1)
			{
				if (iMainIndex == pCategory->iMainCategory && (iIndex == -1 || pCategory->iCategory == -1 || pCategory->iCategory == iIndex) )
				{
					bFound = true;
					break;
				}
			}
		}

		if (!bFound) return false;
	}

	if (pData->uFlags & SEARCHFLAG_LANGUAGE)
	{
		if (pData->iLocaleID > 0)
		{
			int ticketLocaleID = locGetIDByLanguage(pEntry->eLanguage);
			if (pData->iLocaleID-1 != ticketLocaleID)
			{
				return false;
			}
		}
	}

	if(pData->uFlags & SEARCHFLAG_PRODUCT_NAME)
	{
		if(pEntry->pProductName == NULL)
		{
			return false;
		}
		if(!strstri(pEntry->pProductName, pData->pProductName))
		{
			return false;
		}
	}

	if(pData->uFlags & SEARCHFLAG_PLATFORM)
	{
		if (pData->ePlatform != pEntry->ePlatform)
		{
			return false;
		}
	}

	if(pData->uFlags & SEARCHFLAG_TRIVIA && pData->pTrivia && *pData->pTrivia)
	{
		// TODO tweak trivia for key/value sep
		bool bFoundAnything = false;
		for(i=0; i<eaSize(&pEntry->ppTriviaData); i++)
		{
			if (pData->iSearchTriviaKeyValue == SEARCH_TRIVIA_BOTH || pData->iSearchTriviaKeyValue == SEARCH_TRIVIA_KEY)
			{
				if(pEntry->ppTriviaData[i]->pKey && strstri(pEntry->ppTriviaData[i]->pKey, pData->pTrivia))
				{
					bFoundAnything = true;
					break;
				}
			}
			if (pData->iSearchTriviaKeyValue == SEARCH_TRIVIA_BOTH || pData->iSearchTriviaKeyValue == SEARCH_TRIVIA_VALUE)
			{
				if(pEntry->ppTriviaData[i]->pVal && strstri(pEntry->ppTriviaData[i]->pVal, pData->pTrivia))
				{
					bFoundAnything = true;
					break;
				}
			}
		}

		if (!bFoundAnything)
		{
			return false;
		}
	}

	if (pData->uFlags & SEARCHFLAG_GROUP)
	{
		if (pEntry->uGroupID != pData->uGroupID)
		{
			return false;
		}
	}
	
	if (pData->uFlags & SEARCHFLAG_ASSIGNED)
	{
		if (pData->iIsAssigned)
		{
			if (pData->iIsAssigned == SearchToggle_No && pEntry->pRepAccountName)
				return false;
			if (pData->iIsAssigned == SearchToggle_Yes && !pEntry->pRepAccountName)
				return false;
		}
		if (pData->pRepAccountName && (!pEntry->pRepAccountName 
			|| !strstri(pEntry->pRepAccountName, pData->pRepAccountName)) )
		{
			return false;
		}
	}

	/*if (pData->uFlags & SEARCHFLAG_EXTRA)
	{
		if (pEntry->uUserDataDescriptorID == NULL)
			return false;
		else
		{
			/*ParseTable **ppParseTables = NULL;
			void *structptr = NULL;
			char *pPTIName = NULL;
			loadParseTableAndStruct(&ppParseTables, &structptr, &pPTIName, 
				pEntry->uUserDataDescriptorID, pEntry->pUserDataStr);
			if (ParserSearchForSubstring(ppParseTables, structptr, pData->pExtraSubstring) == -1)
			{
				destroyParseTableAndStruct(&ppParseTables, &structptr);
				return false;
			}
			destroyParseTableAndStruct(&ppParseTables, &structptr);
			if (!entitySearchForSubstring(pEntry->pUserDataStr, pData->pExtraSubstring))
			{
				return false;
			}
		}
	}/**/ // disabled

	if(pData->uFlags & SEARCHFLAG_ANY)
	{
		bool bFoundAnything = false;

		{
			if(pEntry->pUserDescription)
			{
				if(strstri(pEntry->pUserDescription, pData->pAny))
				{
					bFoundAnything = true;
				}
			}
		}

		if(!bFoundAnything)
		{
			if(pEntry->pSummary != NULL)
			{
				if(strstri(pEntry->pSummary, pData->pAny))
				{
					bFoundAnything = true;
				}
			}
		}

		if(!bFoundAnything)
		{
			if(pEntry->pRepAccountName != NULL)
			{
				if(strstri(pEntry->pRepAccountName, pData->pAny))
				{
					bFoundAnything = true;
				}
			}
		}

		if(!bFoundAnything)
		{
			if (pEntry->pJiraIssue)
			{
				if (pEntry->pJiraIssue->key && strstri(pEntry->pJiraIssue->key, pData->pAny))
				{
					bFoundAnything = true;
				}
			}
		}

		if(!bFoundAnything)
		{
			bool bFound = false;
			for (i=eaSize(&pEntry->ppUserInfo)-1; i>=0; i--)
			{
				if (pEntry->ppUserInfo[i]->pAccountName && strstri(pEntry->ppUserInfo[i]->pAccountName, pData->pAny))
				{
					bFoundAnything = true;
					break;
				}
				if (pEntry->ppUserInfo[i]->pCharacterName && strstri(pEntry->ppUserInfo[i]->pCharacterName, pData->pAny))
				{
					bFoundAnything = true;
					break;
				}
				if (pEntry->ppUserInfo[i]->pShardInfoString && strstri(pEntry->ppUserInfo[i]->pShardInfoString, pData->pAny))
				{
					bFoundAnything = true;
					break;
				}
				if (pEntry->ppUserInfo[i]->pVersionString && strstri(pEntry->ppUserInfo[i]->pVersionString, pData->pAny))
				{
					bFoundAnything = true;
					break;
				}
			}
		}
		
		if(!bFoundAnything)
		{
			if(pEntry->pMainCategory != NULL)
			{
				if(strstri(pEntry->pMainCategory, pData->pAny))
				{
					bFoundAnything = true;
				}
			}
		}

		if(!bFoundAnything)
		{
			if(pEntry->pCategory != NULL)
			{
				if(strstri(pEntry->pCategory, pData->pAny))
				{
					bFoundAnything = true;
				}
			}
		}

		if (!bFoundAnything)
		{
			if (pEntry->pLabel && strstri(pEntry->pLabel, pData->pAny))
				bFoundAnything = true;
		}

		if(!bFoundAnything)
		{
			if(pEntry->pProductName != NULL)
			{
				if(strstri(pEntry->pProductName, pData->pAny))
				{
					bFoundAnything = true;
				}
			}
		}

		if(!bFoundAnything)
		{
			if(pEntry->pVersionString != NULL)
			{
				if(strstri(pEntry->pVersionString, pData->pAny))
				{
					bFoundAnything = true;
				}
			}
		}

		if(!bFoundAnything)
		{
			if (strstri(getPlatformName(pEntry->ePlatform), pData->pAny))
			{
				bFoundAnything = true;
			}
		}

		if(!bFoundAnything)
		{
			for(i=0; i<eaSize(&pEntry->ppTriviaData); i++)
			{
				if(pEntry->ppTriviaData[i]->pKey && strstri(pEntry->ppTriviaData[i]->pKey, pData->pAny))
				{
					bFoundAnything = true;
					break;
				}
				if(pEntry->ppTriviaData[i]->pVal && strstri(pEntry->ppTriviaData[i]->pVal, pData->pAny))
				{
					bFoundAnything = true;
					break;
				}
			}
		}

		if(!bFoundAnything)
		{
			return false;
		}
	}

	return true;
}

static TicketEntry * internalSearchNext(SearchData *pData, int iStartingIndex)
{
	TicketEntry *pEntry = NULL;
	if (iStartingIndex < 0 || iStartingIndex >= eaSize(&pData->ppSortedEntries))
		return NULL;
	pEntry = pData->ppSortedEntries[iStartingIndex];
	pData->iNextIndex++;
	return pEntry;
}

static void internalSearch(SearchData *pData)
{
	bool bCategorySearch = false;
	if (pData->uFlags & SEARCHFLAG_CATEGORY)
	{
		int j;
		TicketEntry *pEntry;

		pData->uFlags &= (~SEARCHFLAG_CATEGORY); // Disable this so it doesn't do a redundant check

		for (j=eaSize(&pData->ppCategories)-1; j>=0; j--)
		{
			int i, size;
			TicketSearchCategoryCache *cache;
			const char *pMainKey, *pSubKey;
			pMainKey = categoryGetKey(pData->ppCategories[j]->iMainCategory);
			pSubKey = subcategoryGetKey(pData->ppCategories[j]->iMainCategory, pData->ppCategories[j]->iCategory);
			cache = searchCacheFindCategoryCache(pMainKey, pSubKey);

			if (cache && cache->eaiEntryIDs)
			{
				bCategorySearch = true;
				size = eaiSize(&cache->eaiEntryIDs);
				for (i=0; i<size; i++)
				{
					pEntry = findTicketEntryByID(cache->eaiEntryIDs[i]);
					if (pEntry && searchMatches(pData, pEntry))
						eaPush(&pData->ppSortedEntries, pEntry);
				}
			}

			if (pData->eAdminSearch == SEARCH_DEFAULT || pData->eAdminSearch == SEARCH_ADMIN_GAME)
			{
				// Add stickies for this sub-category
				cache = searchCacheFindCategoryCache("CBug.CategoryMain.Stickies", pSubKey);
				if (cache)
				{
					size = eaiSize(&cache->eaiEntryIDs);
					for (i=0; i<size; i++)
					{
						pEntry = findTicketEntryByID(cache->eaiEntryIDs[i]);
						if (searchMatches(pData, pEntry))
							eaPush(&pData->ppSortedEntries, pEntry);
					}
				}
			}
		}
		pData->uFlags |= SEARCHFLAG_CATEGORY; // put it back in afterwards so it doesn't get changed
	}

	if (!bCategorySearch)
	{
		ContainerIterator iter = {0};
		Container *con;

		eaClear(&pData->ppSortedEntries);
		objInitContainerIteratorFromType(GLOBALTYPE_TICKETENTRY, &iter);
		con = objGetNextContainerFromIterator(&iter);
		while (con)
		{
			TicketEntry * pEntry = CONTAINER_ENTRY(con);
			if(searchMatches(pData, pEntry))
			{
				eaPush(&pData->ppSortedEntries, pEntry);
			}
			con = objGetNextContainerFromIterator(&iter);
		}
		objClearContainerIterator(&iter);
	}
	pData->iNextIndex = 0;
}

static void searchConvertToArrays(SearchData *pData)
{
	int i;
	if (pData->uFlags & SEARCHFLAG_CATEGORY)
	{
		if (pData->iMainCategory > -1 && !pData->ppCategories)
		{
			SearchCategory *pCategory = StructCreate(parse_SearchCategory);
			pCategory->iMainCategory = pData->iMainCategory;
			pCategory->iCategory = pData->iCategory;
			eaPush(&pData->ppCategories, pCategory);
		}

		for (i=eaSize(&pData->ppCategories)-1; i>=0; i--)
		{
			SearchCategory *pCategory = pData->ppCategories[i];
			if (pCategory->pMainCategoryString)
			{
				pCategory->iMainCategory = categoryGetIndex(pCategory->pMainCategoryString);
				if (pCategory->pCategoryString)
					pCategory->iCategory = subcategoryGetIndex(pCategory->iMainCategory, pCategory->pCategoryString);
				else
					pCategory->iCategory = -1;
			}
		}
	}

	if (pData->uFlags & SEARCHFLAG_STATUS && pData->eStatus && !pData->peStatuses)
	{
		eaiPush(&pData->peStatuses, pData->eStatus);
	}
}

TicketEntry * searchFirstEx(SearchData *pData, TicketEntry **ppEntries)
{
	TicketEntry *pEntry;
	int i, size = eaSize(&ppEntries);

	searchConvertToArrays(pData);
	eaClear(&pData->ppSortedEntries);
	for (i=0; i<size; i++)
	{
		if(searchMatches(pData, ppEntries[i]))
		{
			eaPush(&pData->ppSortedEntries, ppEntries[i]);
		}
	}
	if(pData->uFlags & SEARCHFLAG_SORT)
	{
		sortBySortOrder(pData->ppSortedEntries, pData->eSortOrder, pData->bSortDescending);
	}

	pEntry = internalSearchNext(pData, 0);
	
	if(pEntry == NULL)
	{
		eaDestroy(&pData->ppSortedEntries);
	}
	return pEntry;
}

TicketEntry * searchFirst(SearchData *pData)
{
	TicketEntry *pEntry;

	searchConvertToArrays(pData);
	internalSearch(pData);
	if(pData->uFlags & SEARCHFLAG_SORT)
	{
		sortBySortOrder(pData->ppSortedEntries, pData->eSortOrder, pData->bSortDescending);
	}

	pEntry = internalSearchNext(pData, 0);
	
	if(pEntry == NULL)
	{
		eaDestroy(&pData->ppSortedEntries);
	}
	return pEntry;
}

TicketEntry * searchNext(SearchData *pData)
{
	TicketEntry *pEntry = internalSearchNext(pData, pData->iNextIndex);

	if(pEntry == NULL)
	{
		eaDestroy(&pData->ppSortedEntries);
	}
	return pEntry;
}

void searchEnd(SearchData *pData)
{
	if (pData)
	{
		eaDestroy(&pData->ppSortedEntries);
	}
}

bool UserInfoMatchesFilter(SearchData *pSearchData, TicketEntry *pEntry, TicketClientUserInfo *pUserInfo)
{
	if(!pSearchData)
		return true;

	//if (pSearchData->eDateType != DATESEARCH_STATUS_UPDATED)
	{
		if(pSearchData->uFlags & SEARCHFLAG_DATESTART)
		{
			if (pUserInfo->uUpdateTime)
			{
				if (pUserInfo->uUpdateTime < pSearchData->uDateStart)
					return false;
			}
			else if(pUserInfo->uFiledTime < pSearchData->uDateStart)
				return false;
		}

		if(pSearchData->uFlags & SEARCHFLAG_DATEEND)
		{
			if (pUserInfo->uUpdateTime)
			{
				if (pUserInfo->uUpdateTime > pSearchData->uDateEnd)
					return false;
			}
			else if(pUserInfo->uFiledTime > pSearchData->uDateEnd)
				return false;
		}
	}

	if (pSearchData->uFlags & SEARCHFLAG_SHARD)
	{
		if (pSearchData->pShardInfoString)
		{
			if (!pUserInfo->pShardInfoString || !strstri(pUserInfo->pShardInfoString, pSearchData->pShardInfoString))
				return false;
		}
		if (pSearchData->pShardExactName)
		{
			char buffer[128];
			sprintf(buffer, "name (%s)", pSearchData->pShardExactName);
			if (!pUserInfo->pShardInfoString || !strstri(pUserInfo->pShardInfoString, buffer))
				return false;
		}
	}

	if (pSearchData->uFlags & SEARCHFLAG_CHARACTER_NAME)
	{
		if (!pUserInfo->pCharacterName)
			return false;

		if (pSearchData->bExactCharacterName)
		{
			if (stricmp(pUserInfo->pCharacterName, pSearchData->pCharacterName) != 0)
				return false;
		} else if (!strstri(pUserInfo->pCharacterName, pSearchData->pCharacterName))
			return false;
	}

	if(pSearchData->uFlags & SEARCHFLAG_ACCOUNT_NAME)
	{
		if (!pUserInfo->pAccountName)
			return false;

		if (pSearchData->bExactAccountName && stricmp(pUserInfo->pAccountName, pSearchData->pAccountName) != 0)
			return false;

		if(!strstri(pUserInfo->pAccountName, pSearchData->pAccountName))
			return false;
	}
	
	if(pSearchData->uFlags & SEARCHFLAG_VERSION)
	{
		if (!pEntry->pVersionString || !strstri(pEntry->pVersionString, pSearchData->pVersion))
			if (!pUserInfo->pVersionString || !strstri(pUserInfo->pVersionString, pSearchData->pVersion))
				return false;
	}

	return true;
}

U32 FilterOccurenceCount(SearchData *pSearchData, TicketEntry *pEntry)
{
	U32 uOccurrences = pEntry->uOccurrences;
	if(pSearchData)
	{
		int i;
		int size = eaSize(&pEntry->ppUserInfo);
		uOccurrences = 0;

		for (i=0; i<(int) pEntry->uOccurrences && i < size; i++)
		{
			if ( UserInfoMatchesFilter(pSearchData, pEntry, (TicketClientUserInfo*)pEntry->ppUserInfo[i]))
				uOccurrences++;
		}
	}

	return uOccurrences;
}

void searchDataInitialize(SearchData *psd)
{
	// Non-zero defaults
	psd->bSortDescending = true;
	psd->iMainCategory = psd->iCategory = -1;
	psd->iLocaleID = -1;
}

AUTO_FIXUPFUNC;
TextParserResult fixupSearchData(SearchData *psd, enumTextParserFixupType eFixupType, void *pExtraData)
{
	if (eFixupType == FIXUPTYPE_DESTRUCTOR)
	{
		eaDestroy(&psd->ppSortedEntries);
	}
	return PARSERESULT_SUCCESS;
}

#include "autogen/search_h_ast.c"