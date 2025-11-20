#pragma once

#define PANDORA_ID_TAG "<id>"
#define PANDORA_NAME_TAG "<name>"
#define PANDORA_GAME_TAG "<game>"

typedef enum Language Language;

///////////////////////////////////
// Settings Config

AUTO_STRUCT;
typedef struct TicketTrackerConfig
{
	// django WebSrv address used as a proxy to communicate with PWE RightNow
	char *pWebSrvAddress;
	int iWebSrvPort;
	
	// RightNow Settings
	U32 uRightNowUserQueryLimit; AST(NAME(RightNowUserQueryLimit) DEFAULT(10))
	U32 uRightNowQueryLimit; AST(NAME(RightNowQueryLimit) DEFAULT(500))
	char *pRightNowProductNameOverride; AST(NAME(RightNowProductNameOverride)) // Product Override name for non-Live environments; This will override the passed in product (eg. replace "StarTrek" with "QA")

	char *pandoraAddress;
	// <id> = id
	// <name> = name string
	// <game> = game name in lower case (eg. fightclub, startrek, neverwinter)
	// No offline or deleted characters linked
	char *pandoraAccountLink; // view-account/<name>
	char *pandoraCharacterLink; // character-search/<game>/<id>
	char *pandoraTicketLink; // /view-ticket/<id>

	char *ticketFileDir;
} TicketTrackerConfig;

#define WEBSRV_IS_CONFIGURED(config) (config->pWebSrvAddress && *config->pWebSrvAddress && config->iWebSrvPort)

SA_RET_NN_VALID TicketTrackerConfig *GetTicketTrackerConfig(void);
void LoadAllTicketTrackerConfigFiles(void);

U32 GetRightNowUserQueryLimit(void);
U32 GetRightNowQueryLimit(void);

AUTO_STRUCT;
typedef struct TicketTrackerSavedValues
{
	U32 lastRightNowUpdateTime; // in seconds since 2000
	U32 queryPeriod; AST(DEFAULT(3600)) // in seconds; last closed query period time search that returned fewer than the query limit
} TicketTrackerSavedValues;

void TTSavedValues_Load(void);

U32 TTSavedValues_GetUpdateTime(void);
void TTSavedValues_ModifyUpdateTime(U32 uTime);
U32 TTSavedValues_GetQueryPeriod(void);
void TTSavedValues_ModifyQueryPeriod(U32 uTime);

char *GetPandoraAccountLink (char **estr, const char *accountName);
char *GetPandoraCharacterLink (char **estr, const char *game, U32 uCharacterID);
char *GetPandoraTicketLink (char **estr, U32 uTicketID);

const char *GetTicketFileParentDirectory(void);

///////////////////////////////////
// Auto-Response Rules

AUTO_STRUCT;
typedef struct TicketAutoResponseRule
{
	// Matching Rules
	CONST_STRING_EARRAY eaProducts; AST(POOL_STRING)
	char *pMainCategory; // NULL or empty string = all [main/sub] categories
	char *pCategory;

	// Response Message Text or Message Key
	char *pMessage;
	// Language of text; if pMessage is a message key, the value should be LANGUAGE_DEFAULT == 0
	// If pMessage is translated text, eLanguage should be the proper matching language
	Language eLanguage;
} TicketAutoResponseRule;

AUTO_STRUCT;
typedef struct TicketAutoResponseRuleList
{
	// Only first matching rule is applied, so most general rules should be put last
	EARRAY_OF(TicketAutoResponseRule) eaRules;
} TicketAutoResponseRuleList;