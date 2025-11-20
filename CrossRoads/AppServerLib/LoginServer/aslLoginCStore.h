#pragma once
GCC_SYSTEM

#include "ResourceManager.h"

typedef struct AccountProxyKeyValueInfo AccountProxyKeyValueInfo;
typedef struct AccountProxyKeyValueInfoList AccountProxyKeyValueInfoList;
typedef struct CStoreAction CStoreAction;
typedef struct Login2State Login2State;
typedef struct Packet Packet;

void HandleCStoreAction(Login2State *loginLink, Packet *pak);
void APCacheSetKey(U32 uAccountID, SA_PARAM_NN_VALID AccountProxyKeyValueInfo *pInfo);
void APCacheSetAllKeys(U32 uAccountID, SA_PARAM_NN_VALID AccountProxyKeyValueInfoList *pInfoList);
void APCacheRemoveKey(U32 uAccountID, SA_PARAM_NN_STR const char *pKey);
void aslLoginHandleSteamPurchase(Login2State *loginState, Packet *pak);

