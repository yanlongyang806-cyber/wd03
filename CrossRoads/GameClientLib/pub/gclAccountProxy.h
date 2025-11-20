#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef GCLACCOUNTPROXY_H
#define GCLACCOUNTPROXY_H

#include "net/accountnet.h"

typedef struct AccountProxyProduct AccountProxyProduct;
typedef struct AccountProxyProductList AccountProxyProductList;

U32 gclAPGetKeyValueLastUpdateTime(void);
bool gclAPGetProductList(SA_PARAM_NN_STR const char *pCategory, SA_PRE_NN_FREE SA_POST_NN_VALID EARRAY_OF(AccountProxyProduct) *pppProduct);
bool gclAPCacheSetAllKeyValues(SA_PARAM_NN_VALID AccountProxyKeyValueInfoList *pList);
bool gclAPCacheSetAllKeyContainerValues(SA_PARAM_NN_VALID AccountProxyKeyValueInfoListContainer *pList);
bool gclAPCacheSetKeyValue(SA_PARAM_NN_VALID AccountProxyKeyValueInfo *pInfo);
bool gclAPCacheSetKeyContainerValue(SA_PARAM_NN_VALID AccountProxyKeyValueInfoContainer *pInfo);
void gclAPOnConnect(void);
bool gclAPGetKeyValueInt(SA_PARAM_NN_STR const char *pKey, SA_PRE_NN_FREE SA_POST_NN_VALID S32 *value);
bool gclAPGetKeyValueString(SA_PARAM_NN_STR const char *pKey, SA_PRE_GOOD SA_POST_NN_NN_STR char **estrValue);
bool gclAPIsKeySet(SA_PARAM_NN_STR const char *pKey);
bool gclAPCacheRemoveKeyValue(SA_PARAM_NN_STR const char *pKey);
bool gclAPPrerequisitesMet(SA_PARAM_NN_VALID AccountProxyProduct *product);
S32 gclAPExprAccountGetKeyValueInt(SA_PARAM_NN_STR const char *pKey);

#endif