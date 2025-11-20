#include "chatShardConfig.h"

#include "AppLocale.h"
#include "chatdb.h"
#include "error.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "mathutil.h"
#include "ResourceInfo.h"
#include "textparser.h"

#include "AutoGen/AppLocale_h_ast.h"
#include "AutoGen/chatShardConfig_h_ast.h"
#include "AutoGen/chatdb_h_ast.h"

//ProjectChatServerConfig
ShardChatServerConfig gShardChatServerConfig = {0}; // File should be stored in "localdata\CHATSERVER_CONFIG_FILEPATH"
static EARRAY_OF(ShardChatServerConfig) stConfigs = NULL;
#define CHATSERVER_CONFIG_FILEPATH "server/ProjectChatServerConfig.txt"
int giOverrideNumChatRelays = 1;

//__CATEGORY Chat Server or Relay related Settings
// Number of ChatRelays the ChatServer will start up on launch
AUTO_CMD_INT(giOverrideNumChatRelays, NumChatRelays) ACMD_AUTO_SETTING(Chat, CHATSERVER);

// Config Accessors
U32 ChatConfig_GetAdRepetitionRate(void)
{
	return gShardChatServerConfig.iAdRepetitionRate;
}
U32 ChatConfig_GetPurchaseExclusionDuration(void)
{
	return gShardChatServerConfig.iPurchaseExclusionDuration;
}
bool ChatConfig_HasAds(void)
{
	return eaSize(&gShardChatServerConfig.ppAdMessages) > 0;
}
const char *ChatConfig_GetRandomAd(Language eLanguage)
{
	int size = eaSize(&gShardChatServerConfig.ppAdMessages);
	U32 uRand;
	AdMessage *msg;
	AdTranslation *trans;
	if (!size)
		return NULL;
	uRand = randInt(size);
	
	msg = gShardChatServerConfig.ppAdMessages[uRand];
	trans = eaIndexedGetUsingInt(&msg->ppTranslations, eLanguage);
	if (!trans)
		trans = eaIndexedGetUsingInt(&msg->ppTranslations, msg->eDefaultLanguage);
	if (!trans)
		return NULL;
	return trans->translation;
}
U32 ChatConfig_GetNumberOfChatRelays(void)
{
	if (giOverrideNumChatRelays > 0)
		return giOverrideNumChatRelays;
	if (gShardChatServerConfig.iNumChatRelays == 0)
		return 1;
	return gShardChatServerConfig.iNumChatRelays;
}
U32 ChatConfig_GetMaxUsersPerRelay(void)
{
	if (gShardChatServerConfig.iMaxUsersPerRelay == 0)
		return CHATRELAY_DEFAULT_MAX_USERS;
	return gShardChatServerConfig.iMaxUsersPerRelay;
}
U32 ChatConfig_GetUsersPerRelayWarningLevel(void)
{
	if (gShardChatServerConfig.iMaxUsersPerRelay == 0)
		return INT_MAX; // disabled
	return gShardChatServerConfig.iUserPerRelayWarning;
}

static void chatConfigReloadCallback(const char *relpath, int when)
{
	loadstart_printf("Reloading Chat Config...");
	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);
	if (!fileExists(relpath))
		; // File was deleted, do we care here?

	StructInit(parse_ShardChatServerConfig, &gShardChatServerConfig);
	ParserReadTextFile(CHATSERVER_CONFIG_FILEPATH, parse_ShardChatServerConfig, &gShardChatServerConfig, PARSER_OPTIONALFLAG | PARSER_IGNORE_ALL_UNKNOWN | PARSER_NOERROR_ONIGNORE);
	loadend_printf("done");
}

static bool sbConfigInitialLoadDone = false;
AUTO_STARTUP(ShardChatServerConfig, 1);
void LoadShardChatServerConfig(void)
{
	if (!sbConfigInitialLoadDone)
	{
		StructInit(parse_ShardChatServerConfig, &gShardChatServerConfig);
		sbConfigInitialLoadDone = true;
	}
	// Read in chat server specific data
	if(!ParserReadTextFile(CHATSERVER_CONFIG_FILEPATH, parse_ShardChatServerConfig, &gShardChatServerConfig, PARSER_IGNORE_ALL_UNKNOWN | PARSER_NOERROR_ONIGNORE | PARSER_OPTIONALFLAG));
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, CHATSERVER_CONFIG_FILEPATH, chatConfigReloadCallback);
}

void InitShardChatServerConfig(void)
{	
	eaPush(&stConfigs, &gShardChatServerConfig);
	resRegisterDictionaryForEArray("Chat Server Config Values", RESCATEGORY_OTHER, 0, &stConfigs, parse_ShardChatServerConfig);
}

#include "AutoGen/chatShardConfig_h_ast.c"