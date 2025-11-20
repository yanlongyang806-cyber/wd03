#include "gclStoredCredentials.h"
#include "StoredCredentialsCommon.h"
#include "EString.h"
#include "accountnet.h"
#include "gclAccountProxy.h"
#include "cmdparse.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

void gclStoredCredentialsStore(const char *service, const char *user, const char *token, const char *secret)
{
	AccountProxyKeyValueInfo info = {0};

	StoredCredentialsUserKey(&info.pKey, service);
	estrPrintf(&info.pValue, "%s", user);
	gclAPCacheSetKeyValue(&info);

	StoredCredentialsTokenKey(&info.pKey, service);
	estrPrintf(&info.pValue, "%s", token);
	gclAPCacheSetKeyValue(&info);

	StoredCredentialsSecretKey(&info.pKey, service);
	estrPrintf(&info.pValue, "%s", secret);
	gclAPCacheSetKeyValue(&info);

	estrDestroy(&info.pKey);
	estrDestroy(&info.pValue);

	ServerCmd_gslStoreCredentials(service, user, token, secret);
}

void gclStoredCredentialsGet(const char *service, char **user, char **token, char **secret)
{
	char *key=NULL;
	if(user)
	{
		StoredCredentialsUserKey(&key, service);
		gclAPGetKeyValueString(key, user);
	}
	if(token)
	{
		StoredCredentialsTokenKey(&key, service);
		gclAPGetKeyValueString(key, token);
	}
	if(secret)
	{
		StoredCredentialsSecretKey(&key, service);
		gclAPGetKeyValueString(key, secret);
	}
	estrDestroy(&key);
}