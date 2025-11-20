/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "AccountDataCache.h"
#include "UtilitiesLibEnums.h"
#include "gclAccountProxy.h"
#include "objContainer.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "StashTable.h"
#include "EString.h"
#include "itemCommon.h"
#include "gclSendToServer.h"
#include "accountnet.h"
#include "microtransactions_common.h"
#include "ShardCommon.h"

#include "AccountDataCache_h_ast.h"
#include "accountnet_h_ast.h"
#include "microtransactions_common_h_ast.h"
#include "GameClientLib.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static const U32 ProductCacheTimeout = 5 * 60;
static U32 s_uKeyValueLastUpdateTime;

/************************************************************************/
/* Public functions                                                     */
/************************************************************************/

U32 gclAPGetKeyValueLastUpdateTime(void)
{
	return s_uKeyValueLastUpdateTime;
}

bool gclAPGetProductList(const char *pCategory, EARRAY_OF(AccountProxyProduct) *pppProduct)
{
	devassertmsg(0, "DEPRECATED");
	return false;
}

bool gclAPCacheSetAllKeyValues(AccountProxyKeyValueInfoList *pList)
{
	s_uKeyValueLastUpdateTime = gGCLState.totalElapsedTimeMs;
	return AccountProxySetKeyValueList(StructClone(parse_AccountProxyKeyValueInfoList, pList));
}

bool gclAPCacheSetAllKeyContainerValues(AccountProxyKeyValueInfoListContainer *pList)
{
	AccountProxyKeyValueInfoList *list = StructCreate(parse_AccountProxyKeyValueInfoList);
	S32 i;
	s_uKeyValueLastUpdateTime = gGCLState.totalElapsedTimeMs;
	for (i = 0; i < eaSize(&pList->ppList); i++)
		eaIndexedAdd(&list->ppList, StructClone(parse_AccountProxyKeyValueInfo, (AccountProxyKeyValueInfo *)pList->ppList[i]));
	return AccountProxySetKeyValueList(list);
}

bool gclAPCacheSetKeyValue(AccountProxyKeyValueInfo *pInfo)
{
	AccountProxyKeyValueInfo *info;

	if (!devassertmsg(pInfo, "Cannot replace or add a NULL key value."))
		return false;

	s_uKeyValueLastUpdateTime = gGCLState.totalElapsedTimeMs;

	info = StructClone(parse_AccountProxyKeyValueInfo, pInfo);

	if (info)
		return AccountProxyReplaceKeyValue(info);

	return false;
}

bool gclAPCacheSetKeyContainerValue(AccountProxyKeyValueInfoContainer *pInfo)
{
	AccountProxyKeyValueInfo *info;

	if (!devassertmsg(pInfo, "Cannot replace or add a NULL key value."))
		return false;

	s_uKeyValueLastUpdateTime = gGCLState.totalElapsedTimeMs;

	info = StructClone(parse_AccountProxyKeyValueInfo, (AccountProxyKeyValueInfo *)pInfo);

	if (info)
		return AccountProxyReplaceKeyValue(info);

	return false;
}

void gclAPOnConnect(void)
{
	static bool once = false;

	if (!once && gclServerIsConnected())
	{
		ServerCmd_gslAPCmdRequestAllKeyValues();
		once = true;
	}
}

bool gclAPGetKeyValueInt(const char *pKey, S32 *value)
{
	AccountProxyKeyValueInfo *pItem = NULL;
	gclAPOnConnect();
	if (AccountProxyGetKeyValue(pKey, &pItem))
	{
		*value = pItem->pValue ? atoi(pItem->pValue) : 0;
		return true;
	}
	return false;
}

bool gclAPGetKeyValueString(const char *pKey, char **estrValue)
{
	AccountProxyKeyValueInfo *pItem = NULL;
	gclAPOnConnect();
	if (AccountProxyGetKeyValue(pKey, &pItem))
	{
		estrCopy2(estrValue, pItem->pValue);
		return true;
	}
	return false;
}

bool gclAPIsKeySet(const char *pKey)
{
	S32 val;
	bool result = gclAPGetKeyValueInt(pKey, &val);
	if (result) result = val ? true : false;
	return result;
}

bool gclAPCacheRemoveKeyValue(const char *pKey)
{
	s_uKeyValueLastUpdateTime = gGCLState.totalElapsedTimeMs;
	return AccountProxyRemoveKeyValue(pKey);
}

bool gclAPPrerequisitesMet(AccountProxyProduct *product)
{
	STRING_EARRAY keysUsed = NULL;
	AccountProxyKeyValueInfoList *list = StructCreate(parse_AccountProxyKeyValueInfoList);
	bool ret = false;

	// Get the keys used
	AccountProxyKeysMeetRequirements(NULL, product->ppPrerequisites, AccountGetShardProxyName(), ShardCommon_GetClusterName(), microtrans_GetShardEnvironmentName(), NULL, &keysUsed);

	if (keysUsed)
	{
		EARRAY_CONST_FOREACH_BEGIN(keysUsed, i, s);
			AccountProxyKeyValueInfo *pItem = NULL;
			if (AccountProxyGetKeyValue(keysUsed[i], &pItem)) {
				eaPush(&list->ppList, StructClone(parse_AccountProxyKeyValueInfo, pItem));
			}
		EARRAY_FOREACH_END;

		eaDestroyEString(&keysUsed);
	}

	// Now get the actual result
	ret = AccountProxyKeysMeetRequirements(list, product->ppPrerequisites, AccountGetShardProxyName(), ShardCommon_GetClusterName(), microtrans_GetShardEnvironmentName(), NULL, NULL);

	StructDestroy(parse_AccountProxyKeyValueInfoList, list);

	return ret;
}