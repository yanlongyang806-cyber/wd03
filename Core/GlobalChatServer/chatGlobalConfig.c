#include "chatGlobalConfig.h"

#include "chatdb.h"
#include "error.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "ResourceInfo.h"
#include "textparser.h"

#include "AutoGen/chatGlobalConfig_h_ast.h"
#include "AutoGen/chatdb_h_ast.h"

GlobalChatServerConfig gGlobalChatServerConfig = {0}; // File should be stored in "localdata\CHATSERVER_CONFIG_FILEPATH"
static EARRAY_OF(GlobalChatServerConfig) stConfigs = NULL;
#define CHATSERVER_CONFIG_FILEPATH "server/ProjectChatServerConfig.txt"

// Config Accessors
int ChatServerGetMailLimit(void)
{
	return gGlobalChatServerConfig.iMaxEmails;
}
bool ChatServerIsMailTimeoutEnabled(void)
{
	return gGlobalChatServerConfig.bEnableMailTimeout && gGlobalChatServerConfig.uMailTimeout > 0;
}
bool ChatServerIsMailReadTimeoutReset(void)
{
	return gGlobalChatServerConfig.bEnableMailTimeout && 
		gGlobalChatServerConfig.bEnableReadMailReset && 
		gGlobalChatServerConfig.uReadMailTimeout > 0;
}
U32 ChatServerGetMailTimeout(void)
{
	return gGlobalChatServerConfig.uMailTimeout;
}
U32 ChatServerGetMailReadTimeout(void)
{
	return gGlobalChatServerConfig.uReadMailTimeout;
}
int ChatServerGetChannelSizeCap(void)
{
	if (gGlobalChatServerConfig.iChannelSizeCap)
		return gGlobalChatServerConfig.iChannelSizeCap;
	return INT_MAX;
}
int ChatServerGetMaxChannels(void)
{
	return gGlobalChatServerConfig.iMaxChannels;
}

static void chatConfigValidate(void)
{
	int i;
	for (i=eaSize(&gGlobalChatServerConfig.ppProductDisplayName)-1; i>=0; i--)
	{
		ProductToDisplayNameMap *prodMap = gGlobalChatServerConfig.ppProductDisplayName[i];
		int j,size;
		char **ppUniqueBuffer = NULL;
		size = eaSize(&prodMap->ppShardMap);
		for (j=0; j<size; j++)
		{
			char *display = prodMap->ppShardMap[j]->shardDisplayName;
			if (display && *display)
			{
				if (eaFindString(&ppUniqueBuffer, display) != -1)
					devassertmsgf(0, "Multiple shards for %s are sharing the same display name '%s'.", 
					prodMap->productDisplayName, display);
				else
					eaPush(&ppUniqueBuffer, display);
			}
		}
		eaDestroy(&ppUniqueBuffer);
	}
}

static void chatConfigReloadCallback(const char *relpath, int when)
{
	loadstart_printf("Reloading Chat Config...");
	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);
	if (!fileExists(relpath))
		; // File was deleted, do we care here?

	StructInit(parse_GlobalChatServerConfig, &gGlobalChatServerConfig);
	ParserReadTextFile(CHATSERVER_CONFIG_FILEPATH, parse_GlobalChatServerConfig, &gGlobalChatServerConfig, PARSER_IGNORE_ALL_UNKNOWN | PARSER_NOERROR_ONIGNORE | PARSER_OPTIONALFLAG);
	chatConfigValidate();
	loadend_printf("done");
}

static bool sbConfigInitialLoadDone = false;
AUTO_STARTUP(ProjectChatServerConfig, 1);
void LoadGlobalChatServerConfig(void)
{
	if (!sbConfigInitialLoadDone)
	{
		StructInit(parse_GlobalChatServerConfig, &gGlobalChatServerConfig);
		sbConfigInitialLoadDone = true;
	}
	// Read in chat server specific data
	if(!ParserReadTextFile(CHATSERVER_CONFIG_FILEPATH, parse_GlobalChatServerConfig, &gGlobalChatServerConfig, PARSER_IGNORE_ALL_UNKNOWN | PARSER_NOERROR_ONIGNORE | PARSER_OPTIONALFLAG));
	chatConfigValidate();
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, CHATSERVER_CONFIG_FILEPATH, chatConfigReloadCallback);
}

void InitGlobalChatServerConfig(void)
{	
	resRegisterDictionaryForEArray("Product Address Map", RESCATEGORY_OTHER, 0, &gGlobalChatServerConfig.ppProductShardMap, parse_ProductToShardMap);
	eaPush(&stConfigs, &gGlobalChatServerConfig);
	resRegisterDictionaryForEArray("Global Chat Server Config Values", RESCATEGORY_OTHER, 0, &stConfigs, parse_GlobalChatServerConfig);
}

#include "AutoGen/chatGlobalConfig_h_ast.c"