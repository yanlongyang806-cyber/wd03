#pragma once
#include "AccountServer.h"

// Initialize the account proxy server interface
void AccountProxyServerInit(void);

// Clear the product cache on all account proxies
void ClearAccountProxyProductCache(void);

// Clear the discount cache on all proxies
void ClearAccountProxyDiscountCache(void);

// Send a single key-value to all proxies
void SendKeyValueToAllProxies(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey);

// Sent a request for the removal of a single key-value to all proxies
void SendKeyRemoveToAllProxies(U32 uAccountID, SA_PARAM_NN_STR const char *pKey);

// Send all recruit info to all proxies for an account
void SendRecruitInfoToAllProxies(SA_PARAM_NN_VALID const AccountInfo *pAccount);

// Send a display name update to all proxies
void SendDisplayNameToAllProxies(SA_PARAM_NN_VALID const AccountInfo *pAccount);

// Convert key-value pairs on an account to the account proxy version
SA_RET_NN_VALID AccountProxyKeyValueInfoList *GetProxyKeyValueList(SA_PARAM_NN_VALID AccountInfo *pAccount);

U32 AccountProxyLastSeen(SA_PARAM_NN_STR const char *pProxyName);