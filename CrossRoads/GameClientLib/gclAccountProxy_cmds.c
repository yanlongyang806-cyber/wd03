#include "gclAccountProxy.h"

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gclAPCmdCacheSetAllKeyValues(SA_PARAM_NN_VALID AccountProxyKeyValueInfoList *pList)
{
	devassertmsg(gclAPCacheSetAllKeyValues(pList), "Could not set all key values.");
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gclAPCmdCacheSetKeyValue(SA_PARAM_NN_VALID AccountProxyKeyValueInfo *pInfo)
{
	devassertmsg(gclAPCacheSetKeyValue(pInfo), "Could not set key value.");
}

AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gclAPCmdCacheRemoveKeyValue(SA_PARAM_NN_STR const char *pKey)
{
	gclAPCacheRemoveKeyValue(pKey);
}