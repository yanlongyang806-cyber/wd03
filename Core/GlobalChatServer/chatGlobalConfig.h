#pragma once

// Global Chat Server config, which are loaded from ProjectChatServerConfig.txt
typedef struct SpecialChannelDisplayNameMap SpecialChannelDisplayNameMap;

AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "productName, internalProductName, shardName");
typedef struct ProductToShardMap
{
	char *productName; AST(ESTRING) // External name address (eg. "champions-online")
	char *internalProductName; AST(ESTRING) // internal product name (eg. "FightClub")
	char *shardName; AST(ESTRING)
} ProductToShardMap;

AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "shardName, shardDisplayName");
typedef struct ProductShardToDisplayMap
{
	char *shardName; AST(ESTRING KEY)
	char *shardDisplayName; AST(ESTRING) // empty string here is treated as NULL
} ProductShardToDisplayMap;

AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "productDisplayName, internalProductName");
typedef struct ProductToDisplayNameMap
{
	char *internalProductName; AST(ESTRING KEY) // internal product name (eg. "FightClub")
	char *productDisplayName; AST(ESTRING) // External name address (eg. "champions-online")

	EARRAY_OF(ProductShardToDisplayMap) ppShardMap; // mappings for shard names for this product to display
} ProductToDisplayNameMap;

AUTO_STRUCT;
typedef struct GlobalChatServerConfig
{
	U32 uKey; AST(KEY DEFAULT(1)) // Just for refdict so it can be displayed on the Server Monitor Page

	//Maximum number of spam messages before user is
	//silenced for spam silence duration (seconds)
	S32 iMaxSpamMessages;			AST( NAME(MaxSpamMessages) )
	S32	iSpamSilenceDuration;		AST( NAME(SpamSilenceDuration))
	S32 iSilverSpamDuration;		AST( NAME(SilverSpamDuration))

	// Number of spam message texts to save
	S32 iSpamMessageSaveCount;      AST(DEFAULT(5))

	//value to reset the spam messages count to after the rate limit has been exceeded
	S32 iResetSpamMessages;			AST( NAME(ResetSpamMessages))
	//If two messages are sent within iChatRate seconds, the spam messages count will be incremented
	U32 iChatRate;					AST( NAME(ChatRate))
	// restricted chat rate (new non-subscriber accounts)
	U32 iRestrictedChatRate;		AST( NAME(RestrictedChatRate))
	
	//Maximum naughty value before the user is silenced
	S32 iMaxNaughty;				AST( DEFAULT(DEFAULT_MAX_NAUGHTY) NAME(MaxNaughty))
	//Max number of times a user can unignore someone before their /ignores don't increment the naughty value; 
	//0 disables the feature and means that all ignores will increment the naughty value
	S32 iMaxUnignores;				AST( NAME(MaxUnignores))
	//Amount to increment the naughty value by when issuing a /ignore
	S32 iIgnoreIncrement;			AST( NAME(IgnoreIncrement))
	//Amount to increment the naughty value by when issuing a /ignore_spammer
	S32 iIgnoreSpammerIncrement;	AST( NAME(IgnoreSpammerIncrement))
	//Amount to increment the naughty value by when the chat rate limit has been reached
	S32 iChatRateNaughtyIncrement;	AST( NAME(ChatRateNaughtyIncrement))
	//Time to ban the user for after the ignore threshold has been reached (in hours)
	S32 iBanDuration;				AST( NAME(BanDuration))

	//Maximum number of mail spam messages before user is silenced for iSpamSilenceDuration seconds
	S32 iMaxSpamMail;			AST( NAME(MaxSpamMail))
	//value to reset the Mail messages count to after the rate limit has been exceeded
	S32 iResetSpamMail;			AST( NAME(ResetSpamMail))
	//If two mails are sent within iMailRate seconds, the mail spam count will be incremented
	U32 iMailRate;					AST( NAME(MailRate))
	
	// restricted mail rate (new non-subscriber accounts)
	U32 iRestrictedMailRate;	AST( NAME(RestrictedMailRate))

	// Value that Naughty value is reset to after they exceed iMaxNaughty (should be less than iMaxNaughty)
	S32 iNaughtyReset;				AST(DEFAULT(DEFAULT_MAX_NAUGHTY*3/4))

	// Email Settings
	S32 iMaxEmails;					AST(DEFAULT(EMAIL_MAX_COUNT))
	U32 uMailTimeout;				AST(DEFAULT(EMAIL_MAX_AGE))
	U32 uReadMailTimeout;			AST(DEFAULT(EMAIL_READ_MAX_AGE))
	int iChannelSizeCap; // 0 = disabled
	bool bEnableMailTimeout;
	bool bEnableReadMailReset;

	int iMaxChannels; AST(DEFAULT(MAX_WATCHING))

	U32 iNaughtyQuarterDecay; // Hours between naughty increments for 25% decay
	U32 iNaughtyHalfDecay; // Hours between naughty increments for 50% decay
	U32 iNaughtyFullDecay; // Hours between naughty increments for full 100% decay
	
	STRING_EARRAY ppReservedChannelNames; AST(ESTRING) // Specific channel names that are to be reserved (GCS only)

	SpecialChannelDisplayNameMap **ppSpecialChannelMap;
	ProductToShardMap **ppProductShardMap; // Used for resolving product name addresses to shards
	ProductToDisplayNameMap **ppProductDisplayName; // Used for resolving internal product names to display names, 
		// since GCS does not load messages and doesn't care about translating this (for now)

	// Duration of cache for user handles
	U32 iHandleCacheDuration; AST(DEFAULT(DEFAULT_HANDLE_CACHE_EXPIRATION))
	// Timeout duration for Account Server handle queries
	U32 iHandleQueryTimeout; AST(DEFAULT(10))
} GlobalChatServerConfig;

// Chat Config accessors
int ChatServerGetMailLimit(void);
bool ChatServerIsMailTimeoutEnabled(void);
bool ChatServerIsMailReadTimeoutReset(void);
U32 ChatServerGetMailTimeout(void);
U32 ChatServerGetMailReadTimeout(void);
int ChatServerGetChannelSizeCap(void);
int ChatServerGetMaxChannels(void);

void InitGlobalChatServerConfig(void);