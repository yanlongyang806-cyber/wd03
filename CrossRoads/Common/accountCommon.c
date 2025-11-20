#include "accountCommon.h"
#include "network/crypt.h"
#include "net/accountnet.h"
#include "StringUtil.h"
#include "accountnet_h_ast.h"
//#ifndef GAMECLIENT

void permissionsParseTokenString (char ***eaTokenList, AccountPermissionStruct *permissions)
{
	char *tokenString;
	eaClear(eaTokenList);

	if (!permissions || !permissions->pPermissionString)
		return;

	tokenString = strstri(permissions->pPermissionString, ACCOUNT_PERMISSION_TOKEN_PREFIX);
	if (tokenString)
	{
		char * endTokenString = strchr(tokenString, ';');
		char * tokenList, *curToken;
		char * context = NULL;
		int len = endTokenString ? 
			endTokenString - tokenString - ACCOUNT_PERMISSION_TOKEN_PREFIX_LEN : 
			(int) strlen(tokenString) - ACCOUNT_PERMISSION_TOKEN_PREFIX_LEN;
		tokenString += ACCOUNT_PERMISSION_TOKEN_PREFIX_LEN; // advance past the "token:"

		tokenList = malloc(len + 1);
		tokenList[len] = '\0'; // NULL terminate this
		strncpy_s(tokenList, len+1, tokenString, len);

		curToken = strtok_s(tokenList, " ,;", &context);
		while (curToken)
		{
			eaPush(eaTokenList, strdup(curToken));
			curToken = strtok_s(NULL, " ,;", &context);
		}

		free(tokenList);
	}
}

bool permissionsCheckStartTime(AccountPermissionStruct *permissions, U32 time)
{
	char *tokenString;

	if (!permissions || !permissions->pPermissionString)
		return true;

	tokenString = strstri(permissions->pPermissionString, "start:");
	if (tokenString)
	{
		char * endTokenString = strchr(tokenString, ';');
		U32 startTime;
		tokenString += 6; // advance past the "start:"

		if (endTokenString)
		{
			*endTokenString = 0;
		}

		startTime = atoi(tokenString);

		if (endTokenString)
		{
			*endTokenString = ';';
		}

		if (time < startTime)
			return false;
	}
	return true;
}
bool permissionsCheckEndTime(AccountPermissionStruct *permissions, U32 time)
{
	char *tokenString;

	if (!permissions || !permissions->pPermissionString)
		return true;

	tokenString = strstri(permissions->pPermissionString, "end:");
	if (tokenString)
	{
		char * endTokenString = strchr(tokenString, ';');
		U32 endTime;
		tokenString += 4; // advance past the "end:"

		if (endTokenString)
		{
			*endTokenString = 0;
		}

		endTime = atoi(tokenString);

		if (endTokenString)
		{
			*endTokenString = ';';
		}

		if (time > endTime)
			return false;
	}
	return true;
}

bool permissionsGame(AccountPermissionStruct *permissions, const char *pPermission)
{
	char *tokenString;

	if (!permissions || !permissions->pPermissionString)
		return false;

	tokenString = strstri(permissions->pPermissionString, pPermission);
	if (tokenString)
	{
		return true;
	}
	return false;
}

bool permissionsGetKeyValue_s(AccountPermissionStruct *permissions, const char *key, char *valueBuffer, size_t bufferSize)
{
	char *tokenString;
	char keySubstring[32];

	if (!permissions || !permissions->pPermissionString)
		return false;
	sprintf(keySubstring, "%s:", key);

	tokenString = strstri(permissions->pPermissionString, keySubstring);
	if (tokenString)
	{
		char * endTokenString = strchr(tokenString, ';');
		tokenString += strlen(keySubstring); // advance past the key text

		if (endTokenString)
		{
			*endTokenString = 0;
		}

		strcpy_s(valueBuffer, bufferSize, tokenString);

		if (endTokenString)
		{
			*endTokenString = ';';
		}
		return true;
	}
	return false;
}

const char *accountGetHandle(const char *pchName)
{
	const char *pchHandle = strrchr(pchName, '@');
	if (pchHandle)
		pchName = pchHandle + 1;
	while (*pchName == '@' || IS_WHITESPACE(*pchName))
		pchName++;
	return pchName;
}

//#endif

#include "accountCommon_h_ast.c"
