#include "aslUGCSearchManagerAccounts.h"
#include "stashTable.h"
#include "../../common/autogen/appserverlib_autogen_remotefuncs.h"
#include "TextParser.h"
#include "aslUGCSearchManagerAccounts_c_ast.h"
#include "alerts.h"
#include "AccountNet.h"
#include "continuousBuilderSupport.h"
#include "file.h"

AUTO_ENUM;
typedef enum AccountUGCBannedState
{
	ACCOUNT_UNKNOWN,
	ACCOUNT_UGCBANNED,
	ACCOUNT_UGCNOTBANNED,
} AccountUGCBannedState;

AUTO_STRUCT;
typedef struct AccountCache
{
	U32 iAccountID; AST(KEY)
	AccountUGCBannedState eState;
} AccountCache;

StashTable sAccountCachesByID = NULL;

static void ReturnAccountPermissions_CB(TransactionReturnVal *returnVal, void *userData)
{
	U32 iAccountID = (U32)(intptr_t)userData;
	AccountCache *pCache;
	AccountProxyKeyValueInfoList *pKeys = NULL;
	char *pUGCPublishBanVal;

	switch(RemoteCommandCheck_aslAPCmdGetAllKeyValues(returnVal, &pKeys))
	{
	case TRANSACTION_OUTCOME_FAILURE:
		CRITICAL_NETOPS_ALERT("SEARCHMANAGER_CANT_GET_KEYVALUES", "The UGCSearch Manager was trying to get permissions for account %u, failed to do so. No account proxy server running?",
			iAccountID);
		return;
	case TRANSACTION_OUTCOME_SUCCESS:
		assert(stashIntFindPointer(sAccountCachesByID, iAccountID, &pCache));
		
		pUGCPublishBanVal = AccountProxyFindValueFromKeyInList(pKeys, GetAccountUgcPublishBanKey());

		if (pUGCPublishBanVal)
		{
			pCache->eState = ACCOUNT_UGCBANNED;
		}
		else
		{
			pCache->eState = ACCOUNT_UGCNOTBANNED;
		}

		StructDestroy(parse_AccountProxyKeyValueInfoList, pKeys);
		return;
	}
}


void aslUGCSearchManagerAddAccountIDForPermissionChecking(U32 iAccountID)
{
	AccountCache *pCache;
	
	if (g_isContinuousBuilder)
	{
		return;
	}
	
	if(0 == iAccountID)
	{
		return;
	}

	if (!sAccountCachesByID)
	{
		sAccountCachesByID = stashTableCreateInt(1 << 16);
	}

	if (!(stashIntFindPointer(sAccountCachesByID, iAccountID, &pCache)))
	{
		pCache = StructCreate(parse_AccountCache);
		pCache->iAccountID = iAccountID;
		stashIntAddPointer(sAccountCachesByID, iAccountID, pCache, false);
		RemoteCommand_aslAPCmdGetAllKeyValues( objCreateManagedReturnVal(ReturnAccountPermissions_CB, (void *)(intptr_t)iAccountID),
			GLOBALTYPE_ACCOUNTPROXYSERVER, 0, iAccountID, 0);
	}
}

//always assume banned if we don't know it yet
bool aslUGCSearchManagerIsAccountUGCPublishBanned(U32 iAccountID)
{
	AccountCache *pCache;
	if (g_isContinuousBuilder)
	{
		return false;
	}

	if (isDevelopmentMode())
	{
		return false;
	}

	if (!sAccountCachesByID)
	{
		return true;
	}

	if (!(stashIntFindPointer(sAccountCachesByID, iAccountID, &pCache)))
	{
		return true;
	}

	if (pCache->eState == ACCOUNT_UGCNOTBANNED)
	{
		return false;
	}

	return true;
}

AUTO_COMMAND;
void aslUGCSearchManagerInvalidatePermissionCache(U32 iAccountID)
{
	AccountCache *pCache;

	if (g_isContinuousBuilder)
	{
		return;
	}
	

	if (!sAccountCachesByID)
	{
		sAccountCachesByID = stashTableCreateInt(1 << 16);
	}

	if (!(stashIntFindPointer(sAccountCachesByID, iAccountID, &pCache)))
	{
		pCache = StructCreate(parse_AccountCache);
		pCache->iAccountID = iAccountID;
		stashIntAddPointer(sAccountCachesByID, iAccountID, pCache, false);
	}

	RemoteCommand_aslAPCmdGetAllKeyValues( objCreateManagedReturnVal(ReturnAccountPermissions_CB, (void *)(intptr_t)iAccountID),
		GLOBALTYPE_ACCOUNTPROXYSERVER, 0, iAccountID, 0);
}


#include "aslUGCSearchManagerAccounts_c_ast.c"
