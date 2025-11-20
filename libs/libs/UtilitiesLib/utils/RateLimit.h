#pragma once

#include "Alerts.h"

// See: https://secure.wikimedia.org/wikipedia/en/wiki/Token_bucket
// The difference is that we remove tokens from a bucket instead of adding them
// and possibly allow the "debt" to grow larger than the blocking threshold, depending
// on configuration

AUTO_STRUCT;
typedef struct RateLimitCost
{
	// This activity type should match an enum value for the enum given to RLLoadConfig
	char * szActivityType; AST(STRUCTPARAM)

	// The number of tokens that will be added to a bucket for this activity type
	int iCost; AST(STRUCTPARAM)
} RateLimitCost;

AUTO_STRUCT;
typedef struct RateLimitTier
{
	// If there are this many tokens in a bucket, there will be an alert
	U32 uAlertTokenThreshold; AST(NAME(AlertTokenThreshold))

	// If there are this many tokens in a bucket, rateLimit will return false
	U32 uBlockTokenThreshold; AST(NAME(BlockTokenThreshold))

	// Maximum number of tokens that can be added to a bucket
	U32 uMaximumTokens; AST(NAME(MaximumTokens))

	// Number of seconds to wait per token removal
	U32 uTokenExpirationInterval; AST(NAME(TokenExpirationInterval) ADDNAMES(SecondsPerToken) DEFAULT(1))

	// Number of tokens removed from a bucket each interval
	U32 uTokensExpiredPerInterval; AST(NAME(TokensExpiredPerInterval) ADDNAMES(TokensPerSecond) DEFAULT(1))

	// Maximum number of buckets, 0 (no limit) is effectively U32_MAX
	U32 uMaximumNumberOfBuckets; AST(NAME(MaximumNumberOfBuckets))

	// The level at which to alert (if any)
	enumAlertLevel eAlertLevel; AST(NAME(AlertLevel) DEFAULT(ALERTLEVEL_CRITICAL))

	// An array of costs for different activity types
	EARRAY_OF(RateLimitCost) eaTokenCosts; AST(NAME(TokenCost))
} RateLimitTier;

AUTO_STRUCT;
typedef struct RateLimitConfig
{
	// If false, no rate limiting will occur
	bool bEnabled; AST(DEFAULT(true) NAME(Enabled))

	EARRAY_OF(RateLimitTier) eaTiers; AST(NAME(Tier))
} RateLimitConfig;

typedef struct RateLimit RateLimit;
typedef struct RateLimitBlockedIter RateLimitBlockedIter;

// Create or update a rate limit
bool RLSetConfig(SA_PRE_NN_VALID SA_POST_NN_RELEMS(1) RateLimit ** ppRateLimit,
	SA_PARAM_NN_VALID RateLimitConfig * pRateLimitConfig,
	SA_PARAM_NN_STR const char * szAlertKeyPrefix,
	SA_PARAM_NN_VALID StaticDefineInt * pActivityTypeList);

// Autoload config from file (pActivityTypeList must remain valid)
bool RLAutoLoadConfigFromFile(SA_PRE_NN_VALID SA_POST_NN_RELEMS(1) RateLimit ** ppRateLimit,
	SA_PARAM_NN_STR const char * szFileName,
	SA_PARAM_NN_VALID RateLimitConfig * pDefaultRateLimitConfig,
	SA_PARAM_NN_STR const char * szAlertKeyPrefix,
	SA_PARAM_NN_VALID StaticDefineInt * pActivityTypeList);

// Destroy a rate limit
void RLDestroy(SA_PRE_NN_VALID SA_POST_FREE RateLimit ** ppRateLimit);

// Returns time in SS2000 for when a block will expire (or zero if there is none)
U32 RLBlockedUntil(SA_PARAM_NN_VALID RateLimit * pRateLimit,
	SA_PARAM_NN_STR const char * szString);

// Rate limit a string (returns true if it is within the limit and will add it to the running total)
bool RLCheck(SA_PARAM_NN_VALID RateLimit * pRateLimit, SA_PARAM_NN_STR const char * szString, unsigned int uActivityTypeIndex);

// The following are for iterating over the list of currently blocked items
SA_RET_OP_VALID RateLimitBlockedIter * RLBlockedIterCreate(SA_PARAM_NN_VALID RateLimit * pRateLimit);
SA_RET_OP_STR const char * RLBlockedIterNext(SA_PARAM_NN_VALID RateLimitBlockedIter * pIter);
void RLBlockedIterDestroy(SA_PRE_NN_VALID SA_POST_P_FREE RateLimitBlockedIter * pIter);

// Clear all the rate limit tokens under specific Key
bool RLRemoveFromRateLimit(SA_PARAM_NN_VALID RateLimit * pRateLimit, 
	SA_PARAM_NN_STR const char * szKey);