#pragma once

// Shard Chat Server config, which are loaded from ProjectChatServerConfig.txt
#include "language\AppLocale.h"
typedef struct SpecialChannelDisplayNameMap SpecialChannelDisplayNameMap;

AUTO_STRUCT;
typedef struct AdTranslation
{
	Language eLanguage; AST(KEY)
	char *translation;
} AdTranslation;

AUTO_STRUCT;
typedef struct AdMessage
{
	EARRAY_OF(AdTranslation) ppTranslations;
	Language eDefaultLanguage;
} AdMessage;

AUTO_STRUCT;
typedef struct ShardChatServerConfig
{
	U32 uKey; AST(KEY DEFAULT(1)) // Just for refdict so it can be displayed on the Server Monitor Page

	SpecialChannelDisplayNameMap **ppSpecialChannelMap;

	// Shard Chat Server Configuration Settings

	// Vivox Settings
	char *pchVoiceServer;		AST(NAME(VoiceServer))
	char *pchVoiceAdmin;		AST(NAME(VoiceAdmin))
	char *pchVoicePassword;		AST(NAME(VoicePassword))
	char *pchVoiceServerDev;	AST(NAME(VoiceServerDev))
	char *pchVoiceAdminDev;		AST(NAME(VoiceAdminDev))
	char *pchVoicePasswordDev;	AST(NAME(VoicePasswordDev))
	U32 bVoiceEnabled : 1;			AST(NAME(VoiceEnabled))
	U32 bVoiceAds : 1;				AST(NAME(VoiceAds))

	// C-store Ad Spamming
	U32 iAdRepetitionRate; // Spams every X seconds
	U32 iPurchaseExclusionDuration; // People who've purchased in the last X seconds are excluded from spammage
	EARRAY_OF(AdMessage) ppAdMessages;

	// Chat Relays
	U32 iNumChatRelays; AST(DEFAULT(10)) // Number of chat relays to start on startup, minimum of 1
	U32 iMaxUsersPerRelay; AST(DEFAULT(CHATRELAY_DEFAULT_MAX_USERS)) // Maximum number of users on a relay
	U32 iUserPerRelayWarning; AST(DEFAULT(CHATRELAY_USERCOUNT_WARNING_CRITERIA))

	U32 iLocalChatThrottlePeriod; AST(DEFAULT(2)) // Seconds between message bursts - 0 to disable
	U32 iLocalChatBufferSize; AST(DEFAULT(4)) // Message per burst before throttling

	U32 bDisableVoiceInGuildChannels : 1; AST(NAME(DisableGuildVoice))

	// Blacklist settings
	U32 uMaxBlacklistNum; AST(DEFAULT(2)) // Number of Blacklist violations a user can accrue before being banned
	U32 uBlacklistDuration; AST(DEFAULT(600)) // Number of seconds since last violation before blacklist violation count is reset
} ShardChatServerConfig;

// Chat Config accessors
U32 ChatConfig_GetAdRepetitionRate(void);
U32 ChatConfig_GetPurchaseExclusionDuration(void);
bool ChatConfig_HasAds(void);
const char *ChatConfig_GetRandomAd(Language eLanguage);
U32 ChatConfig_GetNumberOfChatRelays(void);
U32 ChatConfig_GetMaxUsersPerRelay(void);
U32 ChatConfig_GetUsersPerRelayWarningLevel(void);

void InitShardChatServerConfig(void);