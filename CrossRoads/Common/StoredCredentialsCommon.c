#include "StoredCredentialsCommon.h"
#include "GlobalTypes.h"
#include "EString.h"
#include "Player.h"
#include "accountnet.h"
#include "earray.h"

#include "AutoGen/StoredCredentialsCommon_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

void StoredCredentialsUserKey(char **out, const char *service)
{
	estrPrintf(out, "%s.StoredCredentials.%s.User", GetShortProductName(), service);
}

void StoredCredentialsTokenKey(char **out, const char *service)
{
	estrPrintf(out, "%s.StoredCredentials.%s.Token", GetShortProductName(), service);
}

void StoredCredentialsSecretKey(char **out, const char *service)
{
	estrPrintf(out, "%s.StoredCredentials.%s.Secret", GetShortProductName(), service);
}

void StoredCredentialsFromPlayer(Player *pPlayer, StoredCredentials *pCreds, const char *service)
{
	char *keybuf = NULL;
	AccountProxyKeyValueInfo *pInfo;

	StoredCredentialsUserKey(&keybuf, service);
	pInfo = eaIndexedGetUsingString(&pPlayer->ppKeyValueCache, keybuf);
	if(pInfo)
		pCreds->user = pInfo->pValue;
	else
		pCreds->user = NULL;

	StoredCredentialsTokenKey(&keybuf, service);
	pInfo = eaIndexedGetUsingString(&pPlayer->ppKeyValueCache, keybuf);
	if(pInfo)
		pCreds->token = pInfo->pValue;
	else
		pCreds->token = NULL;

	StoredCredentialsSecretKey(&keybuf, service);
	pInfo = eaIndexedGetUsingString(&pPlayer->ppKeyValueCache, keybuf);
	if(pInfo)
		pCreds->secret = pInfo->pValue;
	else
		pCreds->secret = NULL;

	estrDestroy(&keybuf);
}

#include "AutoGen/StoredCredentialsCommon_h_ast.c"