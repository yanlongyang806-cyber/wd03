#include "RateLimit.h"
#include "RateLimit_h_ast.h"
#include "RateLimit_c_ast.h"

#include "Alerts.h"
#include "Alerts_h_ast.h"
#include "file.h"
#include "FolderCache.h"
#include "HashFunctions.h"
#include "StashTable.h"
#include "timing.h"

AUTO_STRUCT;
typedef struct RateLimitBucket
{
	// The last time this bucket was inspected and the token balance adjusted
	U32 uLastTimeSS2000;

	// This value will effectively decay over time to zero
	U32 uTokens;
} RateLimitBucket;

typedef struct RateLimit
{
	RateLimitConfig * pConfig;
	StashTable * pstBuckets; // Stash of RateLimitBuckets
	StashTable stBlocked; // Stash of blocked keys (no values)
	int * piTokenCost; // Cache of the rate limit costs (indexed by enum)
	char * szAlertKeyPrefix; // Alert key prefix
	StaticDefineInt * pActivityTypeList;
} RateLimit;

typedef struct RateLimitBlockedIter
{
	StashTableIterator stIterator;
} RateLimitBlockedIter;

static unsigned int rl_HashFunction(const void* key, int hashSeed)
{
	// Key is already a hashed integer, so just return it (see RLCheck)
	return PTR_TO_U32(key);
}

static U32 rl_HashKey(SA_PARAM_NN_VALID RateLimit * pRateLimit, int iTier,
	SA_PARAM_NN_STR const char * szKey)
{
	U32 uKey = 0;

	// Normally, we would just let the stash table do the hashing once but
	// custom hash functions wouldn't be aware of the maximum size (see below)
	// and we DO want collisions to reach the same bucket to degrade gracefully
	// when under attack.
	uKey = MurmurHash2(szKey, (U32)strlen(szKey), DEFAULT_HASH_SEED);

	// Reduce the maximum number of buckets
	if (pRateLimit->pConfig->eaTiers[iTier]->uMaximumNumberOfBuckets)
	{
		// This is dangerous if uMaximumNumberOfBuckets is very large
		uKey %= pRateLimit->pConfig->eaTiers[iTier]->uMaximumNumberOfBuckets;
	}

	// Ensure that the key is not 0, an invalid stash table key
	if (U32_TO_PTR(++uKey) == STASH_TABLE_EMPTY_SLOT_KEY) uKey++;

	return uKey;
}

static int rl_HashCompareFunction(const void* key1, const void* key2)
{
	// They're just integers, so compare the two
	U32 uDiff = PTR_TO_U32(key1) - PTR_TO_U32(key2);
	return uDiff ? SIGN(uDiff) : 0;
}

static void rl_Alertf(/* SA_PARAM_NN_VALID (deprecated, clashes with FORMAT_STR) */ RateLimit * pRateLimit,
	/* SA_PARAM_NN_STR (deprecated, clashes with FORMAT_STR) */ const char * szAlertKeyPostfix,
	enumAlertLevel eAlertLevel, FORMAT_STR const char * szFormat, ...)
{
	char * estrAlertKey = NULL;
	char * estrAlertMessage = NULL;

	if (eAlertLevel == ALERTLEVEL_NONE)
	{
		return;
	}

	estrGetVarArgs(&estrAlertMessage, szFormat);
	estrPrintf(&estrAlertKey, "%s_%s", pRateLimit->szAlertKeyPrefix, szAlertKeyPostfix);

	TriggerAlert(estrAlertKey, estrAlertMessage, eAlertLevel, ALERTCATEGORY_NETOPS,
		0, 0, 0, 0, 0, 0, 0);

	estrDestroy(&estrAlertKey);
	estrDestroy(&estrAlertMessage);
}

static void rl_RebuildBucketStashes(SA_PARAM_NN_VALID RateLimit * pRateLimit, int iNumPreviousTiers)
{
	int iNumNewTiers = eaSize(&pRateLimit->pConfig->eaTiers);
	int iCurTier = 0;

	// Clear existing stash tables
	for (iCurTier = 0; iCurTier < MIN(iNumNewTiers, iNumPreviousTiers); iCurTier++)
	{
		stashTableClearStruct(pRateLimit->pstBuckets[iCurTier], NULL, parse_RateLimitBucket);
		stashTableSetMinSize(pRateLimit->pstBuckets[iCurTier], pRateLimit->pConfig->eaTiers[iCurTier]->uMaximumNumberOfBuckets * 2);
	}

	// Destroy any extra stash tables we may have
	for (iCurTier = iNumNewTiers; iCurTier < iNumPreviousTiers; iCurTier++)
	{
		stashTableDestroyStructSafe(&pRateLimit->pstBuckets[iCurTier], NULL, parse_RateLimitBucket);
	}

	// Resize the array
	pRateLimit->pstBuckets = realloc(pRateLimit->pstBuckets, iNumNewTiers * sizeof(StashTable));

	// Create any extra stash tables we may now have
	for (iCurTier = iNumPreviousTiers; iCurTier < iNumNewTiers; iCurTier++)
	{
		pRateLimit->pstBuckets[iCurTier] = stashTableCreateExternalFunctions(pRateLimit->pConfig->eaTiers[iCurTier]->uMaximumNumberOfBuckets * 2, StashDefault,
			rl_HashFunction, rl_HashCompareFunction);
	}
}

// Create or update a rate limit
bool RLSetConfig(SA_PRE_NN_VALID SA_POST_NN_RELEMS(1) RateLimit ** ppRateLimit,
	SA_PARAM_NN_VALID RateLimitConfig * pRateLimitConfig,
	SA_PARAM_NN_STR const char * szAlertKeyPrefix,
	SA_PARAM_NN_VALID StaticDefineInt * pActivityTypeList)
{
	unsigned int uiCurActivityType = 0;
	int iMinActivityIndex = 0;
	int iMaxActivityIndex = 0;
	int iNumActivityTypes = 0;
	int iNumPreviousTiers = 0;

	if (!verify(pRateLimitConfig)) return false;

	PERFINFO_AUTO_START_FUNC();

	DefineGetMinAndMaxInt(pActivityTypeList, &iMinActivityIndex, &iMaxActivityIndex);
	iNumActivityTypes = iMaxActivityIndex - iMinActivityIndex + 1;

	if (!*ppRateLimit)
	{
		*ppRateLimit = callocStruct(RateLimit);
	}
	else
	{
		iNumPreviousTiers = eaSize(&(*ppRateLimit)->pConfig->eaTiers);
	}

	// Copy over the alert prefix only if we aren't reloading the rate limit
	// config (in which case these pointers will be the same)
	if ((*ppRateLimit)->szAlertKeyPrefix != szAlertKeyPrefix)
	{
		SAFE_FREE((*ppRateLimit)->szAlertKeyPrefix);
		(*ppRateLimit)->szAlertKeyPrefix = strdup(szAlertKeyPrefix);
	}

	// We expect pActivityTypeList to remain valid
	(*ppRateLimit)->pActivityTypeList = pActivityTypeList;

	// Reset the configuration
	StructDestroySafe(parse_RateLimitConfig, &(*ppRateLimit)->pConfig);
	(*ppRateLimit)->pConfig = StructClone(parse_RateLimitConfig, pRateLimitConfig);

	rl_RebuildBucketStashes(*ppRateLimit, iNumPreviousTiers);
	stashTableClear((*ppRateLimit)->stBlocked);

	// Rebuild cache of rate limit costs so that it can be indexed by enum
	SAFE_FREE((*ppRateLimit)->piTokenCost);
	(*ppRateLimit)->piTokenCost = callocStructs(int, iNumActivityTypes * eaSize(&pRateLimitConfig->eaTiers));

	EARRAY_CONST_FOREACH_BEGIN(pRateLimitConfig->eaTiers, iCurTier, iNumTiers);
	{
		const RateLimitTier * pTier = pRateLimitConfig->eaTiers[iCurTier];

		EARRAY_CONST_FOREACH_BEGIN(pTier->eaTokenCosts, iCurTokenCost, iNumTokenCost);
		{
			const RateLimitCost * pCost = pTier->eaTokenCosts[iCurTokenCost];
			int iIndex = StaticDefineIntGetInt(pActivityTypeList, pCost->szActivityType) - iMinActivityIndex;

			if (iIndex > -1)
			{
				iIndex += iCurTier * iNumActivityTypes;
				(*ppRateLimit)->piTokenCost[iIndex] = pCost->iCost;
			}
			else
			{
				rl_Alertf(*ppRateLimit, "CONFIG_ERROR", ALERTLEVEL_CRITICAL,
					"Unknown activity type in configuration: %s", pCost->szActivityType);
			}
		}
		EARRAY_FOREACH_END;
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP();

	return true;
}

static void rl_ConfigFileUpdate(FolderCache * pFolderCache,
	FolderNode * pFolderNode, int iVirtualLocation,
	const char * szRelPath, int iWhen, void * pUserData)
{
	RateLimit * pRateLimit = NULL;

	PERFINFO_AUTO_START_FUNC();

	pRateLimit = pUserData;

	if (!devassert(pRateLimit))
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	if (fileExists(szRelPath))
	{
		RateLimitConfig rateLimitConfig = {0};
		RateLimit * pPrevRateLimit = pRateLimit;

		StructInit(parse_RateLimitConfig, &rateLimitConfig);
		ParserReadTextFile(szRelPath, parse_RateLimitConfig, &rateLimitConfig, 0);
		
		// Normally, RLSetConfig might change pRateLimit if it is NULL,
		// but it isn't, so it shouldn't and this is safe.
		RLSetConfig(&pRateLimit, &rateLimitConfig, pRateLimit->szAlertKeyPrefix, pRateLimit->pActivityTypeList);

		StructDeInit(parse_RateLimitConfig, &rateLimitConfig);

		devassert(pRateLimit == pPrevRateLimit);

		printf("Rate limit config loaded: %s\n", szRelPath);
	}

	PERFINFO_AUTO_STOP();
}

// Autoload config from file (pActivityTypeList must remain valid)
bool RLAutoLoadConfigFromFile(SA_PRE_NN_VALID SA_POST_NN_RELEMS(1) RateLimit ** ppRateLimit,
	SA_PARAM_NN_STR const char * szFileName,
	SA_PARAM_NN_VALID RateLimitConfig * pDefaultRateLimitConfig,
	SA_PARAM_NN_STR const char * szAlertKeyPrefix,
	SA_PARAM_NN_VALID StaticDefineInt * pActivityTypeList)
{
	bool bRet = false;

	PERFINFO_AUTO_START_FUNC();

	bRet = RLSetConfig(ppRateLimit, pDefaultRateLimitConfig, szAlertKeyPrefix, pActivityTypeList);
	if (bRet)
	{
		FolderCacheSetCallbackEx(FOLDER_CACHE_CALLBACK_UPDATE, szFileName, rl_ConfigFileUpdate, *ppRateLimit);

		// Go ahead and call it right away incase the file already exists
		rl_ConfigFileUpdate(NULL, NULL, 0, szFileName, 1, *ppRateLimit);
	}

	PERFINFO_AUTO_STOP();

	return bRet;
}

// Destroy a rate limit
void RLDestroy(SA_PRE_NN_VALID SA_POST_FREE RateLimit ** ppRateLimit)
{
	if (!verify(ppRateLimit)) return;
	if (!*ppRateLimit) return;

	PERFINFO_AUTO_START_FUNC();

	if ((*ppRateLimit)->pConfig)
	{
		EARRAY_CONST_FOREACH_BEGIN((*ppRateLimit)->pConfig->eaTiers, iCurTier, iNumTiers);
		{
			stashTableDestroyStructSafe(&(*ppRateLimit)->pstBuckets[iCurTier], NULL, parse_RateLimitBucket);
		}
		EARRAY_FOREACH_END;

		stashTableDestroy((*ppRateLimit)->stBlocked);

		free((*ppRateLimit)->pstBuckets);
		StructDestroy(parse_RateLimitConfig, (*ppRateLimit)->pConfig);
	}

	SAFE_FREE((*ppRateLimit)->piTokenCost);
	SAFE_FREE((*ppRateLimit)->szAlertKeyPrefix);
	free(*ppRateLimit);
	*ppRateLimit = NULL;

	PERFINFO_AUTO_STOP();
}

static void rl_AddToBlockedList(SA_PARAM_NN_VALID RateLimit * pRateLimit,
	SA_PARAM_NN_STR const char * szString)
{
	int iRefCount = 0;

	if (!pRateLimit->stBlocked)
	{
		pRateLimit->stBlocked = stashTableCreateWithStringKeys(0, StashDeepCopyKeys_NeverRelease|StashCaseSensitive);
	}

	stashFindInt(pRateLimit->stBlocked, szString, &iRefCount);

	iRefCount++;

	stashAddInt(pRateLimit->stBlocked, szString, iRefCount, true);
}

static void rl_RemoveFromBlockedList(SA_PARAM_NN_VALID RateLimit * pRateLimit,
	SA_PARAM_NN_STR const char * szString)
{
	int iRefCount = 0;

	if (!pRateLimit->stBlocked)
	{
		return;
	}

	if (stashFindInt(pRateLimit->stBlocked, szString, &iRefCount))
	{
		iRefCount--;

		if (iRefCount <= 0)
		{
			stashRemoveInt(pRateLimit->stBlocked, szString, NULL);
		}
		else
		{
			stashAddInt(pRateLimit->stBlocked, szString, iRefCount, true);
		}
	}
}

SA_RET_OP_VALID RateLimitBlockedIter * RLBlockedIterCreate(SA_PARAM_NN_VALID RateLimit * pRateLimit)
{
	RateLimitBlockedIter * pIter = NULL;

	if (!verify(pRateLimit)) return NULL;

	pIter = callocStruct(RateLimitBlockedIter);
	stashGetIterator(pRateLimit->stBlocked, &pIter->stIterator);
	return pIter;
}

SA_RET_OP_STR const char * RLBlockedIterNext(SA_PARAM_NN_VALID RateLimitBlockedIter * pIter)
{
	StashElement element;

	if (!verify(pIter)) return NULL;

	if (!stashGetNextElement(&pIter->stIterator, &element)) return NULL;

	return stashElementGetKey(element);
}

void RLBlockedIterDestroy(SA_PRE_NN_VALID SA_POST_P_FREE RateLimitBlockedIter * pIter)
{
	SAFE_FREE(pIter);
}

// Refund tokens in a bucket based on the difference between now and the last time we saw it
static void rl_BucketRefundTokens(SA_PARAM_NN_VALID RateLimitBucket * pBucket, U32 uIntervalLength, U32 uTokensPerInterval)
{
	U32 uTimeDifference = 0;
	U32 uTokensToRefund = 0;

	if (!pBucket->uTokens) return;

	uTimeDifference = timeSecondsSince2000() - pBucket->uLastTimeSS2000;
	uTokensToRefund = uTimeDifference / uIntervalLength;

	// Take advantage of uTokensToRefund having been rounded down
	pBucket->uLastTimeSS2000 += uTokensToRefund * uIntervalLength;

	uTokensToRefund = uTokensToRefund * uTokensPerInterval;

	if (uTokensToRefund > pBucket->uTokens) pBucket->uTokens = 0;
	else pBucket->uTokens -= uTokensToRefund;
}

static RateLimitBucket * rl_GetBucketInternal(SA_PARAM_NN_VALID RateLimit * pRateLimit, int iTier,
	SA_PARAM_NN_STR const char * szKey)
{
	U32 uKey = rl_HashKey(pRateLimit, iTier, szKey);
	StashElement stElement = NULL;

	if (stashFindElement(pRateLimit->pstBuckets[iTier], U32_TO_PTR(uKey), &stElement))
	{
		return stashElementGetPointer(stElement);
	}
	else
	{
		return NULL;
	}
}

static RateLimitBucket * rl_GetBucket(SA_PARAM_NN_VALID RateLimit * pRateLimit, int iTier,
	SA_PARAM_NN_STR const char * szKey)
{
	RateLimitBucket * pBucket = rl_GetBucketInternal(pRateLimit, iTier, szKey);
	if (pBucket)
	{
		U32 uLastTokens = pBucket->uTokens;
		U32 uBlockThreshold = pRateLimit->pConfig->eaTiers[iTier]->uBlockTokenThreshold;
		U32 uTokenExpirationInterval = pRateLimit->pConfig->eaTiers[iTier]->uTokenExpirationInterval;
		U32 uTokensExpiredPerInterval = pRateLimit->pConfig->eaTiers[iTier]->uTokensExpiredPerInterval;

		rl_BucketRefundTokens(pBucket, uTokenExpirationInterval, uTokensExpiredPerInterval);
		if (uLastTokens >= uBlockThreshold && pBucket->uTokens < uBlockThreshold)
		{
			rl_RemoveFromBlockedList(pRateLimit, szKey);
		}
	}
	return pBucket;
}

// Returns time in SS2000 for when a block will expire (or zero if there is none)
U32 RLBlockedUntil(SA_PARAM_NN_VALID RateLimit * pRateLimit,
	SA_PARAM_NN_STR const char * szString)
{
	U32 uBlockedUntilMax = 0;

	PERFINFO_AUTO_START_FUNC();

	EARRAY_CONST_FOREACH_BEGIN(pRateLimit->pConfig->eaTiers, iCurTier, iNumTiers);
	{
		RateLimitTier * pTier = pRateLimit->pConfig->eaTiers[iCurTier];
		RateLimitBucket * pBucket = rl_GetBucket(pRateLimit, iCurTier, szString);

		if (pBucket && pBucket->uTokens >= pTier->uBlockTokenThreshold) // rl_GetBucket will decrease the token number, so need to make sure if this tier still being blocked
		{
			U32 uBlockedTime = (pBucket->uTokens - pTier->uBlockTokenThreshold + 1) * pTier->uTokenExpirationInterval / pTier->uTokensExpiredPerInterval;
			U32 uBlockedUntil = pBucket->uLastTimeSS2000 + uBlockedTime;

			if (uBlockedUntil > uBlockedUntilMax)
			{
				uBlockedUntilMax = uBlockedUntil;
			}
		}
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP();

	return uBlockedUntilMax;
}

// Rate limit a string (returns true if it is within the limit and will add it to the running total)
bool RLCheck(SA_PARAM_NN_VALID RateLimit * pRateLimit, SA_PARAM_NN_STR const char * szString, unsigned int uActivityTypeIndex)
{
	bool bAllowed = true;
	int iMinActivityIndex = 0;
	int iMaxActivityIndex = 0;
	int iNumActivityTypes = 0;

	if (!verify(szString)) return false;
	if (!verify(pRateLimit)) return false;
	if (!pRateLimit->pConfig->bEnabled) return true;

	PERFINFO_AUTO_START_FUNC();

	DefineGetMinAndMaxInt(pRateLimit->pActivityTypeList, &iMinActivityIndex, &iMaxActivityIndex);
	iNumActivityTypes = iMaxActivityIndex - iMinActivityIndex + 1;

	EARRAY_CONST_FOREACH_BEGIN(pRateLimit->pConfig->eaTiers, iCurTier, iNumTiers);
	{
		RateLimitTier * pTier = pRateLimit->pConfig->eaTiers[iCurTier];
		U32 uKey = 0;
		RateLimitBucket * pBucket = NULL;
		int iCost = pRateLimit->piTokenCost[iCurTier * iNumActivityTypes + uActivityTypeIndex];
		int iNewTokens = 0;
		U32 uLastTokens = 0;

		uKey = rl_HashKey(pRateLimit, iCurTier, szString);

		// Find or create the bucket
		pBucket = rl_GetBucket(pRateLimit, iCurTier, szString);

		// Early-out if there is no cost
		if (!iCost)
		{
			// Even though there is no cost, make sure the key
			// is not already blocked by this tier
			if (pBucket && pBucket->uTokens >= pTier->uBlockTokenThreshold)
			{
				bAllowed = false;
			}
			continue;
		}

		// Create the bucket if we didn't already have one
		if (!pBucket)
		{
			pBucket = StructCreate(parse_RateLimitBucket);
			stashAddPointer(pRateLimit->pstBuckets[iCurTier], U32_TO_PTR(uKey), pBucket, false);
		}

		if (!devassert(pBucket)) continue;

		// Update the bucket with current information
		uLastTokens = pBucket->uTokens;
		pBucket->uLastTimeSS2000 = timeSecondsSince2000();
		iNewTokens = (int) pBucket->uTokens + iCost;
		if (iNewTokens < 0)
			pBucket->uTokens = 0;
		else
			pBucket->uTokens = (U32) iNewTokens;

		if (pBucket->uTokens > pTier->uMaximumTokens)
		{
			pBucket->uTokens = pTier->uMaximumTokens;
		}

		if (pBucket->uTokens >= pTier->uAlertTokenThreshold &&
			uLastTokens < pTier->uAlertTokenThreshold)
		{
			rl_Alertf(pRateLimit, "WARNING_THRESHOLD", pTier->eAlertLevel,
				"String:%s, Tokens:%u, Key:%u", szString, pBucket->uTokens, uKey);
		}

		if (pBucket->uTokens >= pTier->uBlockTokenThreshold)
		{
			bAllowed = false;
			if (uLastTokens < pTier->uBlockTokenThreshold)
			{
				rl_AddToBlockedList(pRateLimit, szString);
				rl_Alertf(pRateLimit, "BLOCKED", pTier->eAlertLevel,
					"String:%s, Tokens:%u, Key:%u", szString, pBucket->uTokens, uKey);
			}
		}
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP();

	return bAllowed;
}

bool RLRemoveFromRateLimit(SA_PARAM_NN_VALID RateLimit * pRateLimit, 
	SA_PARAM_NN_STR const char * szKey)
{
	bool bResult = false;
	// interate through tiers
	EARRAY_CONST_FOREACH_BEGIN(pRateLimit->pConfig->eaTiers, iCurTier, iNumTiers);
	{
		RateLimitBucket * pBucket = rl_GetBucketInternal(pRateLimit, iCurTier, szKey);
		if (pBucket)
		{
			pBucket->uTokens = 0;
			// change the last touched timestamp for safe measures in case
			// we have a token increase over time feature in the future.
			pBucket->uLastTimeSS2000 = timeSecondsSince2000();
			bResult = true;
		}

	}
	EARRAY_FOREACH_END;
	return bResult;
}




#include "RateLimit_h_ast.c"
#include "RateLimit_c_ast.c"