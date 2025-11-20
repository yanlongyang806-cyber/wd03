#include "UGCSearchCache.h"

#include "timing.h"
#include "error.h"
#include "textparser.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "StringUtil.h"

#include "ugcprojectcommon.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

typedef struct UGCSearchCacheConfig UGCSearchCacheConfig;
typedef struct UGCSearchCacheEntry UGCSearchCacheEntry;
typedef enum UGCSearchCacheCriteria UGCSearchCacheCriteria;

// UGC Search Cache Config is loaded from a file and contains all game-specific configuration on the
// searches that should be cached and for how long they should be cached.
AUTO_STRUCT;
typedef struct UGCSearchCacheConfig
{
	// How long a search result is cached before it is refreshed
	U32 uSearchCacheDurationInSeconds;				AST(NAME(SearchCacheDurationInSeconds))

	// Individual search cache configs
	UGCSearchCacheEntry **eaUGCSearchCacheEntries;	AST(NAME(UGCSearchCacheEntry))
} UGCSearchCacheConfig;
extern ParseTable parse_UGCSearchCacheConfig[];
#define TYPE_parse_UGCSearchCacheConfig UGCSearchCacheConfig

// UGC Search Cache Config is loaded from a file and contains all game-specific configuration on the
// searches that should be cached and for how long they should be cached.
AUTO_STRUCT;
typedef struct UGCSearchCacheResult
{
	U32 uLastRefreshSeconds;						AST(NAME(LastRefreshSeconds))

	UGCProjectSearchInfo *pUGCProjectSearchInfo;	AST(NAME(UGCProjectSearchInfo))
} UGCSearchCacheResult;
extern ParseTable parse_UGCSearchCacheResult[];
#define TYPE_parse_UGCSearchCacheResult UGCSearchCacheResult

// UGC Search Cache Entry defines specific search criteria that should have a cached result
AUTO_STRUCT;
typedef struct UGCSearchCacheEntry
{
	UGCSearchCacheCriteria AccessLevel;						AST(NAME(AccessLevel))
	UGCSearchCacheCriteria IsReviewer;						AST(NAME(IsReviewer))
	UGCSearchCacheCriteria UGCProjects;						AST(NAME(UGCProjects))
	UGCSearchCacheCriteria UGCSeries;						AST(NAME(UGCSeries))
	//UGCSearchCacheCriteria Filters;							AST(NAME(Filters)) // unsupported
	//UGCSearchCacheCriteria PlayerAllegiance;				AST(NAME(PlayerAllegiance)) // unsupported
	//UGCSearchCacheCriteria Simple_Raw;						AST(NAME(Simple_Raw)) // unsupported
	//UGCSearchCacheCriteria Simple_SSSTree;					AST(NAME(Simple_SSSTree)) // unsupported
	UGCSearchCacheCriteria PlayerLevel;						AST(NAME(PlayerLevel))
	UGCSearchCacheCriteria PlayerLevelMin;					AST(NAME(PlayerLevelMin))
	UGCSearchCacheCriteria PlayerLevelMax;					AST(NAME(PlayerLevelMax))
	UGCSearchCacheCriteria Lang;							AST(NAME(Lang))
	//UGCSearchCacheCriteria Location;						AST(NAME(Location)) // unsupported
	UGCSearchCacheCriteria PublishedInLastNDays;			AST(NAME(PublishedInLastNDays))
	UGCSearchCacheCriteria FeaturedIncludeArchives;			AST(NAME(FeaturedIncludeArchives))
	//UGCSearchCacheCriteria Subscription;					AST(NAME(Subscription)) // unsupported
	UGCSearchCacheCriteria SpecialType;						AST(NAME(SpecialType))

	UGCProjectSearchInfo *pUGCProjectSearchInfo;			AST(NAME(UGCProjectSearchInfo))

	UGCSearchCacheResult **eaUGCSearchCacheResults;			AST(NO_WRITE)
} UGCSearchCacheEntry;
extern ParseTable parse_UGCSearchCacheEntry[];
#define TYPE_parse_UGCSearchCacheEntry UGCSearchCacheEntry

// UGC Search Cache Criteria indicates how to match a particular field in UGCProjectSearchInfo against UGCSearchCacheEntry
AUTO_ENUM;
typedef enum UGCSearchCacheCriteria {
	// Ignore means don't even match the field. This will actually assume the fields match.
	// Used for fields like PlayerLevel in the case of NW where all UGC content is level-less.
	// Setting PlayerLevel to DontCare allows any player of any level to get the same search results from the cache as anyone else.
	UGCSearchCacheCriteria_Ignore		= 0,

	// Exact means this field must be an exact match for even considering to cache the incoming UGCProjectSearchInfo against the entry.
	// Used for fields like Simple_Raw, where we never want to cache incoming UGCProjectSearchInfo requests with this field being non-empty.
	UGCSearchCacheCriteria_Exact		= 1,

	// Separate means actually cache separately ever instance of this field for incoming UGCProjectSearchInfo requests
	// Used for fields whose difference matters. For example, in a multi-language game (STO and NW) the Language field must have this criteria.
	UGCSearchCacheCriteria_Separate		= 2
} UGCSearchCacheCriteria;
extern StaticDefineInt UGCSearchCacheCriteriaEnum[];

static UGCSearchCacheConfig *s_pUGCSearchCacheConfig = NULL;

static void ugcSearchCacheConfigLoad()
{
	loadstart_printf("Loading UGC Search Cache Config...");

	if(s_pUGCSearchCacheConfig)
		StructReset(parse_UGCSearchCacheConfig, s_pUGCSearchCacheConfig);
	else
		s_pUGCSearchCacheConfig = StructCreate(parse_UGCSearchCacheConfig);

	if(!ParserLoadFiles(NULL, "genesis/ugc_search_cache_config.txt", "UGCSearchCacheConfig.bin", PARSER_BINS_ARE_SHARED | PARSER_OPTIONALFLAG, parse_UGCSearchCacheConfig, s_pUGCSearchCacheConfig))
		Errorf("Error loading UGC Search Cache Config");

	loadend_printf(" done");
}

static void ugcSearchCacheConfigReload(const char *pchRelPath, int UNUSED_when)
{
	fileWaitForExclusiveAccess(pchRelPath);
	errorLogFileIsBeingReloaded(pchRelPath);

	ugcSearchCacheConfigLoad();
}

AUTO_STARTUP(UGCSearchCache);
void ugcSearchCacheStartup(void)
{
	ugcSearchCacheConfigLoad();

	if(isDevelopmentMode())
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "genesis/ugc_search_cache_config.txt", ugcSearchCacheConfigReload);
}

static bool ugcSearchMatch(UGCProjectSearchInfo *pUGCProjectSearchInfo1, UGCProjectSearchInfo *pUGCProjectSearchInfo2, UGCSearchCacheEntry *pUGCSearchCacheEntry,
	bool bTreatSeparateAsExact)
{
#define RETURN_FALSE_UNLESS_MATCH_BASIC(criteria, field) \
	do { if(UGCSearchCacheCriteria_Exact == pUGCSearchCacheEntry->criteria || (bTreatSeparateAsExact && UGCSearchCacheCriteria_Separate == pUGCSearchCacheEntry->criteria)) \
		if(pUGCProjectSearchInfo1->field != pUGCProjectSearchInfo2->field) \
			return false; \
	} while(0)

	RETURN_FALSE_UNLESS_MATCH_BASIC(AccessLevel, iAccessLevel);
	RETURN_FALSE_UNLESS_MATCH_BASIC(IsReviewer, bIsReviewer);
	RETURN_FALSE_UNLESS_MATCH_BASIC(UGCProjects, bUGCProjects);
	RETURN_FALSE_UNLESS_MATCH_BASIC(UGCSeries, bUGCSeries);
	if(pUGCProjectSearchInfo1->ppFilters || pUGCProjectSearchInfo2->ppFilters) // not really supporting match on pointers yet
		return false;
	if(!nullStr(pUGCProjectSearchInfo1->pchPlayerAllegiance) || !nullStr(pUGCProjectSearchInfo2->pchPlayerAllegiance)) // not really supporting match on PlayerAllegiance yet
		return false;
	if(!nullStr(pUGCProjectSearchInfo1->pSimple_Raw) || !nullStr(pUGCProjectSearchInfo2->pSimple_Raw)) // not really supporting match on text yet
		return false;
	if(!nullStr(pUGCProjectSearchInfo1->pSimple_SSSTree) || !nullStr(pUGCProjectSearchInfo2->pSimple_SSSTree)) // not really supporting match on text yet
		return false;
	RETURN_FALSE_UNLESS_MATCH_BASIC(PlayerLevel, iPlayerLevel);
	RETURN_FALSE_UNLESS_MATCH_BASIC(PlayerLevelMin, iPlayerLevelMin);
	RETURN_FALSE_UNLESS_MATCH_BASIC(PlayerLevelMax, iPlayerLevelMax);
	RETURN_FALSE_UNLESS_MATCH_BASIC(Lang, eLang);
	if(!nullStr(pUGCProjectSearchInfo1->pchLocation) || !nullStr(pUGCProjectSearchInfo2->pchLocation)) // not really supporting match on text yet
		return false;
	RETURN_FALSE_UNLESS_MATCH_BASIC(PublishedInLastNDays, iPublishedInLastNDays);
	RETURN_FALSE_UNLESS_MATCH_BASIC(FeaturedIncludeArchives, bFeaturedIncludeArchives);
	if(pUGCProjectSearchInfo1->pSubscription || pUGCProjectSearchInfo2->pSubscription) // not really supporting match on pointers yet
		return false;
	RETURN_FALSE_UNLESS_MATCH_BASIC(SpecialType, eSpecialType);

	return true;
}

static UGCSearchCacheEntry *ugcSearchCacheFindEntry(UGCProjectSearchInfo *pUGCProjectSearchInfo)
{
	FOR_EACH_IN_EARRAY(s_pUGCSearchCacheConfig->eaUGCSearchCacheEntries, UGCSearchCacheEntry, pUGCSearchCacheEntry)
	{
		if(ugcSearchMatch(pUGCProjectSearchInfo, pUGCSearchCacheEntry->pUGCProjectSearchInfo, pUGCSearchCacheEntry, /*bTreatSeparateAsExact=*/false))
			return pUGCSearchCacheEntry;
	}
	FOR_EACH_END;

	return NULL;
}

UGCProjectSearchInfo *ugcSearchCacheFind(UGCProjectSearchInfo *pUGCProjectSearchInfo)
{
	UGCSearchCacheEntry *pUGCSearchCacheEntry = ugcSearchCacheFindEntry(pUGCProjectSearchInfo);
	if(pUGCSearchCacheEntry)
	{
		FOR_EACH_IN_EARRAY(pUGCSearchCacheEntry->eaUGCSearchCacheResults, UGCSearchCacheResult, pUGCSearchCacheResult)
		{
			if(ugcSearchMatch(pUGCProjectSearchInfo, pUGCSearchCacheResult->pUGCProjectSearchInfo, pUGCSearchCacheEntry, /*bTreatSeparateAsExact=*/true))
			{
				if(pUGCSearchCacheResult->uLastRefreshSeconds + s_pUGCSearchCacheConfig->uSearchCacheDurationInSeconds > timeSecondsSince2000())
					return pUGCSearchCacheResult->pUGCProjectSearchInfo;
				else
					return NULL;
			}
		}
		FOR_EACH_END;
	}
	return NULL;
}

void ugcSearchCacheStore(UGCProjectSearchInfo *pUGCProjectSearchInfo)
{
	UGCSearchCacheEntry *pUGCSearchCacheEntry = ugcSearchCacheFindEntry(pUGCProjectSearchInfo);
	if(pUGCSearchCacheEntry)
	{
		FOR_EACH_IN_EARRAY(pUGCSearchCacheEntry->eaUGCSearchCacheResults, UGCSearchCacheResult, pUGCSearchCacheResult)
		{
			if(pUGCProjectSearchInfo == pUGCSearchCacheResult->pUGCProjectSearchInfo) // if this is true, then we are trying to Store something we pulled out using Find. Just bail.
				return;

			if(ugcSearchMatch(pUGCProjectSearchInfo, pUGCSearchCacheResult->pUGCProjectSearchInfo, pUGCSearchCacheEntry, /*bTreatSeparateAsExact=*/true))
			{
				StructDestroySafe(parse_UGCSearchResult, &pUGCSearchCacheResult->pUGCProjectSearchInfo->pUGCSearchResult);
				pUGCSearchCacheResult->uLastRefreshSeconds = timeSecondsSince2000();
				pUGCSearchCacheResult->pUGCProjectSearchInfo->pUGCSearchResult = StructClone(parse_UGCSearchResult, pUGCProjectSearchInfo->pUGCSearchResult);
				return;
			}
		}
		FOR_EACH_END;

		{
			UGCSearchCacheResult *pUGCSearchCacheResult = StructCreate(parse_UGCSearchCacheResult);
			pUGCSearchCacheResult->uLastRefreshSeconds = timeSecondsSince2000();
			pUGCSearchCacheResult->pUGCProjectSearchInfo = StructClone(parse_UGCProjectSearchInfo, pUGCProjectSearchInfo);
			eaPush(&pUGCSearchCacheEntry->eaUGCSearchCacheResults, pUGCSearchCacheResult);
		}
	}
}

#include "UGCSearchCache_c_ast.c"
