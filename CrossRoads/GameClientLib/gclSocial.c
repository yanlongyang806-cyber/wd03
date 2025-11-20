#include "GraphicsLib.h"
#include "file.h"
#include "utils.h"
#include "Entity.h"
#include "Player.h"
#include "StringCache.h"
#include "gclStoredCredentials.h"
#include "StashTable.h"
#include "SocialCommon.h"

typedef U32 ContainerID;
typedef U32 EntityRef;
#include "AutoGen\GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gclSocialOpenWebpage(const char *url)
{
	openURL(url);
}

static void screenshotCB(const char *filename, char *title)
{
	char *data, *escaped;
	U32 len, escaped_len;
	data = fileAlloc(filename, &len);
	if (data)
	{
		escaped_len = len * 2;
		escaped = malloc(escaped_len);
		escapeDataStatic(data, len, escaped, escaped_len, false);
		ServerCmd_gslSocialCmdReceiveScreenshot(title, escaped);
		free(escaped);
		free(data);
	}
	SAFE_FREE(title);
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gclSocialRequestScreenshot(bool with_ui, const char *title)
{
	gfxRequestScreenshot(NULL, with_ui, screenshotCB, title?strdup(title):NULL);
}

// Expression functions for the client UI
AUTO_EXPR_FUNC(UIGen);
bool gclSocialGetServiceDisabled(SA_PARAM_OP_VALID Entity* pEntity, const char *service, const char *type)
{
	const char *pool_service, *pool_type;
	if(!pEntity || !pEntity->pPlayer || !pEntity->pPlayer->pActivity || !eaSize(&pEntity->pPlayer->pActivity->disabled))
		return false;
	pool_service = allocAddString(service);
	pool_type = allocAddString(type);
	FOR_EACH_IN_EARRAY(pEntity->pPlayer->pActivity->disabled, PlayerActivityEntry, dis)
		if(dis->service == pool_service && dis->type == pool_type)
			return true;
	FOR_EACH_END
	return false;
}

AUTO_EXPR_FUNC(UIGen);
void gclSocialDisableService(const char *service, const char *type, bool disable)
{
	ServerCmd_social_disable(service, type, disable);
}

AUTO_EXPR_FUNC(UIGen);
int gclSocialGetEnrollmentState(SA_PARAM_OP_VALID Entity* pEntity, const char *service)
{
	bool has_creds;
	char *user=NULL, *token=NULL, *secret=NULL;
	const char *pool_service;

	gclStoredCredentialsGet(service, &user, &token, &secret);
	has_creds = (user && user[0]) || (token && token[0]) || (secret && secret[0]);
	estrDestroy(&user);
	estrDestroy(&token);
	estrDestroy(&secret);
	if(has_creds)
		return -1;

	if(!pEntity || !pEntity->pPlayer || !pEntity->pPlayer->pActivity || !eaSize(&pEntity->pPlayer->pActivity->enrollment))
		return 0;
	pool_service = allocAddString(service);
	FOR_EACH_IN_EARRAY(pEntity->pPlayer->pActivity->enrollment, PlayerActivityEnrollment, enroll)
		if(enroll->service == pool_service)
			return enroll->state;
	FOR_EACH_END
	return 0;
}

AUTO_EXPR_FUNC(UIGen);
U32 gclSocialGetServiceVerbosity(SA_PARAM_OP_VALID Entity* pEntity, const char *service)
{
	const char *pool_service;
	if(!pEntity || !pEntity->pPlayer || !pEntity->pPlayer->pActivity || !eaSize(&pEntity->pPlayer->pActivity->verbosity))
		return false;
	pool_service = allocAddString(service);
	FOR_EACH_IN_EARRAY(pEntity->pPlayer->pActivity->verbosity, PlayerActivityVerbosity, ver)
		if(ver->service == pool_service)
			return ver->level;
	FOR_EACH_END
	return kActivityVerbosity_Default;
}

AUTO_EXPR_FUNC(UIGen);
void gclSocialSetServiceVerbosity(const char *service, U32 level)
{
	ServerCmd_social_verbosity(service, level);
}

static StashTable service_cache = NULL;
static bool requested_service_cache = false;

void gclSocialRequestServiceCache(void)
{
	if(!requested_service_cache)
	{
		ServerCmd_gslSocialRequestServiceCache();
		requested_service_cache = true;
	}
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gclSocialUpdateServiceCache(SocialServices *services)
{
	if(service_cache == NULL)
		service_cache = stashTableCreateWithStringKeys(8, StashDefault);
	else
		stashTableClear(service_cache);
	FOR_EACH_IN_EARRAY(services->ppServices, const char, service)
		stashAddInt(service_cache, service, 1, true);
	FOR_EACH_END
}

AUTO_EXPR_FUNC(UIGen);
bool gclSocialServiceRegistered(const char *service)
{
	int check;
	
	if(service_cache == NULL)
		return false;

	if(stashFindInt(service_cache, service, &check))
	{
		return check;
	}

	return false;
}