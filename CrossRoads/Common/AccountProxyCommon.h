#ifndef ACCOUNTPROXYCOMMON_H
#define ACCOUNTPROXYCOMMON_H

#pragma once
GCC_SYSTEM


#include "accountnet.h"
#include "Entity.h"

typedef struct MicroTransactionProductList MicroTransactionProductList;
typedef struct MicroTransactionCategory MicroTransactionCategory;
typedef struct MicroTransactionDef MicroTransactionDef;
typedef struct AccountProxyProduct AccountProxyProduct;
typedef struct AccountProxyProductList AccountProxyProductList;
typedef struct AccountProxyDiscountList AccountProxyDiscountList;
typedef struct ParsedAVP ParsedAVP;

typedef struct NOCONST(AccountProxyLockContainer) NOCONST(AccountProxyLockContainer);

void APSendPrintf(int iID, const char* format, ...);

/************************************************************************/
/* Account Proxy API                                                    */
/************************************************************************/

// Used by the simple set function below
typedef void (*AccountKeyValueSimpleSetCallback)(AccountKeyValueResult result, SA_PARAM_NN_STR const char *key, SA_PARAM_OP_VALID void *userData);

// Simple, non-atomic set operation for key-values.  If increment is true, it will do += instead of = (negative values are okay)
void APSetKeyValueSimple(U32 uAccountID, SA_PARAM_NN_STR const char *key, S64 iValue, bool increment, SA_PARAM_OP_VALID AccountKeyValueSimpleSetCallback callback, SA_PARAM_OP_VALID void *userData);

// Used by the set functions below
typedef void (*AccountKeyValueSetCallback)(AccountKeyValueResult result, U32 accountID, SA_PARAM_NN_STR const char *key, ContainerID containerID, SA_PARAM_OP_VALID void *userData);
typedef void (*AccountKeyValueMoveCallback)(AccountKeyValueResult eResult, ContainerID uLockContainerID, SA_PARAM_OP_VALID void *pUserdata);

// Set functions
void APSetKeyValue(U32 uAccountID, SA_PARAM_NN_STR const char *key, S64 value, SA_PARAM_OP_VALID AccountKeyValueSetCallback callback, SA_PARAM_OP_VALID void *userData);
void APChangeKeyValue(U32 uAccountID, SA_PARAM_NN_STR const char *key, S64 value, SA_PARAM_OP_VALID AccountKeyValueSetCallback callback, SA_PARAM_OP_VALID void *userData);
void APSetKey(U32 uAccountID, SA_PARAM_NN_STR const char *key, SA_PARAM_OP_VALID AccountKeyValueSetCallback callback, SA_PARAM_OP_VALID void *userData);
void APUnsetKey(U32 uAccountID, SA_PARAM_NN_STR const char *key, SA_PARAM_OP_VALID AccountKeyValueSetCallback callback, SA_PARAM_OP_VALID void *userData);
void APMoveKeyValue(U32 uSrcAccountID, SA_PARAM_NN_STR const char *pSrcKey, U32 uDestAccountID, SA_PARAM_NN_STR const char *pDestKey, S64 iValue, SA_PARAM_OP_VALID AccountKeyValueMoveCallback pCallback, SA_PARAM_OP_VALID void *pUserdata);

// Auto transaction helper
bool APFinalizeKeyValue(ATH_ARG NOCONST(AccountProxyLockContainer) *pContainer, U32 accountID, SA_PARAM_NN_STR const char *key, AccountProxyResult result, TransactionLogType eTransactionType);

// Used for purchasing products with a key value (such as points)
typedef void (*AccountBeginPurchaseCallback)(AccountKeyValueResult result, U32 accountID, SA_PARAM_NN_VALID const AccountProxyProduct *product, SA_PARAM_NN_VALID const Money *pExpectedPrice, SA_PARAM_OP_STR const char *pmVID, ContainerID containerID, SA_PARAM_OP_VALID void *userData);
void APBeginPurchase(U32 uAccountID, SA_PARAM_NN_VALID const AccountProxyProduct *pProxyProduct, SA_PARAM_NN_VALID const Money *pExpectedPrice, U32 uOverrideDiscount,
					 SA_PARAM_OP_STR const char *pPaymentMethodID, SA_PARAM_NN_VALID AccountBeginPurchaseCallback pCallback, SA_PARAM_OP_VALID void *pUserData);
bool APFinishPurchase(ATH_ARG NOCONST(AccountProxyLockContainer) *pContainer, U32 accountID, SA_PARAM_NN_VALID const AccountProxyProduct *product, SA_PARAM_NN_STR const char *currency, SA_PARAM_OP_STR const char *pmVID, AccountProxyResult result, TransactionLogType eTransactionType);

typedef enum APProductRequestType
{
	kAPProduct_Microtransactions,
	kAPProduct_PointBuy,
} APProductRequestType;

const char *GetAuthCapture_ErrorMsgKey(PurchaseResult eResult);

void GenerateMicrotransactionList(const AccountProxyProductList *pList, MicroTransactionProductList *pMTList, MicroTransactionCategory *pMTCategory);

// New functions that wrap the functionality in AccountDataCache
typedef void (*ProductListCallback)(const char *pCategory, const AccountProxyProductList *pList, void *pUserdata);
typedef void (*DiscountListCallback)(const AccountProxyDiscountList *pList, void *pUserdata);
typedef void (*MTCatalogCallback)(U32 uContainerID, U32 uAccountID, const MicroTransactionProductList *pList, void *pUserdata);
typedef void (*PointBuyCatalogCallback)(U32 uContainerID, U32 uAccountID, const AccountProxyProductList *pList, void *pUserdata);
void APGetMTCatalog(U32 uContainerID, U32 uAccountID, SA_PARAM_NN_VALID MTCatalogCallback pCallback, SA_PARAM_OP_VALID void *pUserdata);
void APDiscountMTCatalog(SA_PARAM_NN_VALID const MicroTransactionProductList **ppMTList, SA_PARAM_OP_VALID const AccountProxyKeyValueInfoList *pKVList);
void APGetPointBuyCatalog(U32 uContainerID, U32 uAccountID, SA_PARAM_NN_VALID PointBuyCatalogCallback pCallback, SA_PARAM_OP_VALID void *pUserdata);
void APGetProductList(SA_PARAM_NN_STR const char *pCategory, SA_PARAM_NN_VALID ProductListCallback pCallback, SA_PARAM_OP_VALID void *pUserdata);
void APGetDiscountList(SA_PARAM_NN_VALID DiscountListCallback pCallback, SA_PARAM_OP_VALID void *pUserdata);

//void APRequestAllKeyValues(SA_PARAM_NN_VALID Entity *pEntity);
bool APAppendParsedAVPsFromKeyAndValue(const char *pKey, const char *pValue, ParsedAVP ***pppPairs);

typedef struct TransactionReturnVal TransactionReturnVal;
typedef void (*TransactionReturnCallback)(TransactionReturnVal *returnVal, void *data);
void APUpdateGADCache(GlobalType eType, U32 uAccountID, const char *pKey, const char *pValue, TransactionReturnCallback pCallback, void *pUserdata);

void APClientCacheSetAllKeyValues(GlobalType eType, ContainerID uContainerID, U32 uAccountID, SA_PARAM_NN_VALID AccountProxyKeyValueInfoList *pList);
void APClientCacheSetKeyValue(GlobalType eType,ContainerID uContainerID, U32 uAccountID, SA_PARAM_NN_VALID AccountProxyKeyValueInfo *pInfo);
void APClientCacheRemoveKeyValue(GlobalType eType, ContainerID uContainerID, U32 uAccountID, SA_PARAM_NN_STR const char *pKey);
//bool APCacheGetKeyValue(Entity *pEntity, SA_PARAM_NN_STR const char *pKey, char **ppValue);

void APPurchaseProduct(Entity *pEntity,
					   U32 iAccountID,
					   int iLangID,
					   SA_PARAM_NN_STR const char *pCategory,
					   U32 uProductID,
					   SA_PARAM_OP_VALID const Money *pExpectedPrice,
					   U32 uOverrideDiscount,
					   SA_PARAM_OP_STR const char *pPaymentMethodVID,
					   SA_PARAM_OP_STR const char *pClientIP,
					   U64 uItemId);

typedef void (*AccountGetProductCallback)(SA_PARAM_OP_VALID const AccountProxyProduct *pProduct, void *userData);
void APGetProductByID(SA_PARAM_NN_STR const char *pCategory, U32 uProductID, SA_PARAM_NN_VALID AccountGetProductCallback callback, SA_PARAM_OP_VALID void *userData);
void APGetProductByName(SA_PARAM_NN_STR const char *pCategory, SA_PARAM_NN_STR const char *pProductName, SA_PARAM_NN_VALID AccountGetProductCallback callback, SA_PARAM_OP_VALID void *userData);

typedef void (*RequestPaymentMethodsCallback) (U32 entityRef, U32 uAccountID, PaymentMethodsResponse *pResponse);

void APRequestPaymentMethods(Entity *pEnt, U32 uAccountID, U64 uSteamID, RequestPaymentMethodsCallback pCallback);

// Logging function
void APLog(SA_PARAM_OP_VALID Entity *pEnt, U32 iAccountID, SA_PARAM_NN_STR const char *pchAction, FORMAT_STR const char *pchFormat, ...);

/************************************************************************/
/* Account Proxy Auto-commands                                          */
/************************************************************************/

#endif ACCOUNTPROXYCOMMON_H