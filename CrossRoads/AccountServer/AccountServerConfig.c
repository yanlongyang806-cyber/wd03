#include "AccountServerConfig.h"
#include "AutoGen/AccountServerConfig_h_ast.h"

#include "file.h"
#include "FolderCache.h"
#include "timing.h"
#include "timing_profiler.h"

#define ACCOUNT_SERVER_CONFIG_FILE "server/AccountServer/accountServer_Config.txt"
static AccountServerConfig sAccountServerConfig = {0};

static void AccountServerConfig_FileUpdate(FolderCache * pFolderCache,
	FolderNode * pFolderNode, int iVirtualLocation,
	const char * szRelPath, int iWhen, void * pUserData)
{
	PERFINFO_AUTO_START_FUNC();
	StructInit(parse_AccountServerConfig, &sAccountServerConfig);
	if (fileExists(szRelPath))
	{
		ParserReadTextFile(szRelPath, parse_AccountServerConfig, &sAccountServerConfig, 0);
		printf("Account Server config loaded: %s\n", szRelPath);
	}
	PERFINFO_AUTO_STOP();
}

static void AccountServerConfig_AutoLoadFromFile(SA_PARAM_NN_STR const char * szFileName)
{
	PERFINFO_AUTO_START_FUNC();
	FolderCacheSetCallbackEx(FOLDER_CACHE_CALLBACK_UPDATE, szFileName, AccountServerConfig_FileUpdate, NULL);
	AccountServerConfig_FileUpdate(NULL, NULL, 0, szFileName, 1, NULL);
	PERFINFO_AUTO_STOP();
}

AccountServerConfig *GetAccountServerConfig(void)
{
	return &sAccountServerConfig;
}

CONST_STRING_EARRAY GetCurrencyKeys(void)
{
	return sAccountServerConfig.eaCurrencyKeys;
}

CONST_EARRAY_OF(ZenKeyConversion) GetZenKeyConversions(void)
{
	return sAccountServerConfig.eaZenKeyConversions;
}

void LoadAccountServerConfig(void)
{
	AccountServerConfig_AutoLoadFromFile(ACCOUNT_SERVER_CONFIG_FILE);
}

#include "AutoGen/AccountServerConfig_h_ast.c"