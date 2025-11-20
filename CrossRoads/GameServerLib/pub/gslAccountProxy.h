/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef GSLACCOUNTPROXY_H
#define GSLACCOUNTPROXY_H

//#include "accountnet.h"
//#include "Entity.h"
#include "globaltypes.h"
#include "referencesystem.h"

typedef struct AccountProxyProduct AccountProxyProduct;
typedef struct AccountProxyProductList AccountProxyProductList;
typedef struct AccountProxyKeyValueInfo AccountProxyKeyValueInfo;
typedef struct AccountProxyKeyValueInfoList AccountProxyKeyValueInfoList;
typedef struct Entity Entity;
typedef struct GameAccountData GameAccountData;
typedef struct MicroTransactionProduct MicroTransactionProduct;
typedef struct MicroTransactionProductList MicroTransactionProductList;
typedef struct MicroTransactionCategory MicroTransactionCategory;
typedef struct MicroTransactionDef MicroTransactionDef;
typedef struct Money Money;
typedef enum MicroPurchaseErrorType MicroPurchaseErrorType;

AUTO_STRUCT;
typedef struct MicroTransactionInfo {
	EARRAY_OF(MicroTransactionProduct) ppProducts;
	EARRAY_OF(MicroTransactionCategory) ppCategories;
} MicroTransactionInfo;

AUTO_STRUCT;
typedef struct AccountProxyProductTrimmed
{
	U32 uID;
	Money *pFullMoneyPrice;
	Money *pMoneyPrice;
} AccountProxyProductTrimmed;

AUTO_STRUCT;
typedef struct MTUserProduct
{
	AccountProxyProductTrimmed *pProduct;
	bool bPrereqsMet;

	char *pErrorMsg; AST(ESTRING)
	MicroPurchaseErrorType eErrorType;
} MTUserProduct;

AUTO_ENUM;
typedef enum WebDiscountRequestStatus
{
	WDRSTATUS_PROCESSING = 0,
	WDRSTATUS_SUCCESS,
	WDRSTATUS_FAILED,
} WebDiscountRequestStatus;

AUTO_STRUCT;
typedef struct WebDiscountRequest
{
	U32 uRequestID; AST(KEY)
	U32 uAccountID;
	U32 uCharacterID;
	U32 uSubRequestID;
	U32 uTimeSent;
	U32 uTimeReceived;

	AccountProxyProductList *pList;
	AccountProxyKeyValueInfoList *pKVList;

	// Return info
	EARRAY_OF(MTUserProduct) ppProducts;
	WebDiscountRequestStatus eStatus;
	char *pError;
} WebDiscountRequest;

AUTO_ENUM;
typedef enum PurchaseRequestStatus
{
	PURCHASE_PROCESSING = 0,
	PURCHASE_SUCCESS,
	PURCHASE_ROLLBACK, // State during rollback
	PURCHASE_FAILED, // State after rollback or if it failed before the transaction
} PurchaseRequestStatus;

#define PurchaseIsCompleted(eStatus) (eStatus == PURCHASE_SUCCESS || eStatus == PURCHASE_FAILED)

AUTO_STRUCT;
typedef struct PurchaseRequestData
{
	U32 uRequestID; AST(KEY)
	U32 uAccountID;
	U32 uCharacterID;
	U32 uSubRequestID;

	// Purchase data stores direct pointers to these that are unowned
	Entity *pEnt; AST(UNOWNED)
	GameAccountData *pGAD; AST(UNOWNED)

	AccountProxyProduct *pProduct;
	REF_TO(MicroTransactionDef) hDef;
	Money *pExpectedPrice;
	ContainerID containerLockID;

	PurchaseRequestStatus eStatus;
	char *pError;
	char *prereqName; AST(ESTRING)
	
	U32 uTimeComplete;
} PurchaseRequestData;

#define WDR_ERROR_GAMEONLINE "game_online"
#define WDR_ERROR_NOTFOUND "character_unknown"
#define WDR_ERROR_INVALID "character_invalid"
#define WDR_ERROR_UNKNOWN "unknown"
#define WDR_ERROR_INVALID_VERSION "character_invalid_version"
#define WDR_ERROR_PARTIAL_REQUEST "partial_request"

#define WEBPURCHASE_ERROR_DEPRECATED "product_deprecated"
#define WEBPURCHASE_ERROR_RESTRICTED "product_restricted"
#define WEBPURCHASE_ERROR_UNKNOWN "product_unknown"
#define WEBPURCHASE_ERROR_BADCURRENCY "currency_invalid"
#define WEBPURCHASE_ERROR_CURRENCY_INSUFFICIENT "currency_insufficient"
#define WEBPURCHASE_ERROR_PURCHASEFAIL "purchase_failed"
#define WEBPURCHASE_ERROR_PRICE_CHANGED "price_changed"

#define WEBPURCHASE_ERROR_PURCHASEUNKNOWN "purchase_not_found"
#define WEBPURCHASE_ERROR_MISMATCHED_ACCOUNTID "account_mismatch"


// Timeouts in seconds
#define DISCOUNT_REQUEST_TIMEOUT (60)
#define DISCOUNT_RESPONSE_EXPIRE (180)
#define PURCHASE_RESPONSE_EXPIRE (180)

void gslMTProducts_UserCheckProducts(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID GameAccountData *pAccountData, WebDiscountRequest *data);
WebDiscountRequest *gslAPGetDiscountRequest(U32 uRequestID);

void gslAP_WebCStorePurchase (SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_NN_VALID GameAccountData *pAccountData, PurchaseRequestData *pCBStruct);
PurchaseRequestData *gslMTPurchase_GetRequest(U32 uRequestID);

/************************************************************************/
/* Account Proxy API                                                    */
/************************************************************************/

// Used by the simple set function below
//typedef void (*AccountKeyValueSimpleSetCallback)(AccountKeyValueResult result, SA_PARAM_NN_STR const char *key, SA_PARAM_OP_VALID void *userData);

// Simple, non-atomic set operation for key-values.  If increment is true, it will do += instead of = (negative values are okay)
//void gslAPSetKeyValueSimple(const AccountIdentity *pAccountID, SA_PARAM_NN_STR const char *key, S64 iValue, bool increment, SA_PARAM_OP_VALID AccountKeyValueSimpleSetCallback callback, SA_PARAM_OP_VALID void *userData);

// Used by the set functions below
//typedef void (*AccountKeyValueSetCallback)(AccountKeyValueResult result, U32 accountID, SA_PARAM_NN_STR const char *key, ContainerID containerID, SA_PARAM_OP_VALID void *userData);

// Set functions
//void gslAPSetKeyValue(const AccountIdentity *pAccountID, SA_PARAM_NN_STR const char *key, S32 value, SA_PARAM_OP_VALID AccountKeyValueSetCallback callback, SA_PARAM_OP_VALID void *userData);
//void gslAPChangeKeyValue(const AccountIdentity *pAccountID, SA_PARAM_NN_STR const char *key, S32 value, SA_PARAM_OP_VALID AccountKeyValueSetCallback callback, SA_PARAM_OP_VALID void *userData);
//void gslAPSetKey(const AccountIdentity *pAccountID, SA_PARAM_NN_STR const char *key, SA_PARAM_OP_VALID AccountKeyValueSetCallback callback, SA_PARAM_OP_VALID void *userData);
//void gslAPUnsetKey(const AccountIdentity *pAccountID, SA_PARAM_NN_STR const char *key, SA_PARAM_OP_VALID AccountKeyValueSetCallback callback, SA_PARAM_OP_VALID void *userData);

// Auto transaction helper
//bool gslAPFinalizeKeyValue(ATH_ARG NOCONST(AccountProxyLockContainer) *pContainer, U32 accountID, SA_PARAM_NN_STR const char *key, AccountProxyResult result);

// Used for purchasing products with a key value (such as points)
//typedef void (*AccountBeginPurchaseCallback)(AccountKeyValueResult result, U32 accountID, SA_PARAM_NN_VALID const AccountProxyProduct *product, SA_PARAM_NN_STR const char *currency, SA_PARAM_OP_STR const char *pmVID, ContainerID containerID, SA_PARAM_OP_VALID void *userData);
//void gslAPBeginPurchase(U32 accountID, SA_PARAM_NN_VALID const AccountProxyProduct *product, SA_PARAM_NN_STR const char *currency, SA_PARAM_OP_STR const char *pmVID, SA_PARAM_OP_VALID AccountBeginPurchaseCallback callback, SA_PARAM_OP_VALID void *userData);
//bool gslAPFinishPurchase(ATH_ARG NOCONST(AccountProxyLockContainer) *pContainer, U32 accountID, SA_PARAM_NN_VALID const AccountProxyProduct *product, SA_PARAM_NN_STR const char *currency, SA_PARAM_OP_STR const char *pmVID, AccountProxyResult result);

//void gslAPClientCacheReplaceProductList(ContainerID uContainerID, U32 uAccountID, SA_PARAM_NN_STR const char *pCategory, SA_PARAM_NN_VALID AccountProxyProductList *pList);

void gslAPRequestAllKeyValues(SA_PARAM_NN_VALID Entity *pEntity);
void gslAPClientCacheSetAllKeyValues(ContainerID uContainerID, U32 uAccountID, SA_PARAM_NN_VALID AccountProxyKeyValueInfoList *pList);
void gslAPClientCacheSetKeyValue(ContainerID uContainerID, U32 uAccountID, SA_PARAM_NN_VALID AccountProxyKeyValueInfo *pInfo);
void gslAPClientCacheRemoveKeyValue(ContainerID uContainerID, U32 uAccountID, SA_PARAM_NN_STR const char *pKey);
bool gslAPCacheGetKeyValue(Entity *pEntity, SA_PARAM_NN_STR const char *pKey, char **ppValue);


//typedef void (*AccountGetProductCallback)(SA_PARAM_OP_VALID const AccountProxyProduct *pProduct, void *userData);
//void gslAPGetProductByID(SA_PARAM_NN_STR const char *pCategory, U32 uProductID, SA_PARAM_NN_VALID AccountGetProductCallback callback, SA_PARAM_OP_VALID void *userData);
//void gslAPGetProductByName(SA_PARAM_NN_STR const char *pCategory, SA_PARAM_NN_STR const char *pProductName, SA_PARAM_NN_VALID AccountGetProductCallback callback, SA_PARAM_OP_VALID void *userData);

//void gslAPUnlockCostumes(SA_PARAM_NN_VALID Entity *pEntity, SA_PARAM_NN_VALID AccountProxyKeyValueInfoList *pList);
//void gslAPUnlockCostume(SA_PARAM_NN_VALID Entity *pEntity, SA_PARAM_NN_VALID AccountProxyKeyValueInfo *pInfo);

typedef void (*AccountGetSubbedTimeCallback)(U32 uEstimatedTotalSeconds, void *pUserData);
void gslAPGetSubbedTime(SA_PARAM_NN_VALID Entity *pEntity, SA_PARAM_OP_STR const char *pProductInternalName, SA_PARAM_NN_VALID AccountGetSubbedTimeCallback pCallback, SA_PARAM_OP_VALID void *pUserData);

/************************************************************************/
/* Account Proxy Auto-commands                                          */
/************************************************************************/

void gslAPSetKeyValueCmdNoCallback(SA_PARAM_NN_VALID Entity *pEntity, SA_PARAM_NN_STR const char *key, S64 iVal);
void gslAPSetKeyValueCmd(SA_PARAM_NN_VALID Entity *pEntity, SA_PARAM_NN_STR const char *key, S64 iVal);
void gslAPChangeKeyValueCmdNoCallback(SA_PARAM_NN_VALID Entity *pEntity, SA_PARAM_NN_STR const char *key, S64 iVal);
void gslAPChangeKeyValueCmd(SA_PARAM_NN_VALID Entity *pEntity, SA_PARAM_NN_STR const char *key, S64 iVal);
void gslAPGetKeyValuesCmd(SA_PARAM_NN_VALID Entity *pEntity);
void gslAPGetAccountIDFromDisplayNameCmd(SA_PARAM_NN_VALID Entity *pEntity, SA_PARAM_NN_STR const char *pDisplayName);
void gslAPDisplayNameSetKeyValueStringCmd(SA_PARAM_NN_VALID Entity *pEntity, SA_PARAM_NN_STR const char *displayName, SA_PARAM_NN_STR const char *key, SA_PARAM_NN_STR const char *sVal);
void gslAPBanCmd(SA_PARAM_NN_VALID Entity *pEntity, SA_PARAM_NN_STR const char *displayName);
void gslAPGetSubbedTimeCmd(SA_PARAM_NN_VALID Entity *pEntity, SA_PARAM_OP_STR const char *productInternalName);
void gslAPGetSubbedDaysCmd(Entity *pEntity);

void gslAPRequestMTCatalog(SA_PARAM_NN_VALID Entity *pEntity);
void gslAPRequestPointBuyCatalog(SA_PARAM_NN_VALID Entity *pEntity);
// Product List Cache for web request server
void gslAPProductListUpdateCache(void);
MicroTransactionInfo *gslAPGetProductList(void);

// Retrieving discounts
void gslWebRequestTick(void);
WebDiscountRequest *gslAPRequestDiscounts(U32 uContainerID, U32 uAccountID);
// Purchase call
int gslAP_InitiatePurchase(U32 uCharacterID, U32 uAccountID, U32 uProductID, int uExpectedPrice);
PurchaseRequestStatus gslAP_GetPurchaseStatus(U32 uAccountID, U32 uRequestID, SA_PARAM_NN_VALID char **pOutError);


/************************************************************************/
/* MicrotransactionHammer commands                                      */
/************************************************************************/

void gslAPDirectProductList(SA_PARAM_NN_STR const char *pCategory, SA_PARAM_NN_VALID AccountProxyProductList *pList);
void gslAPMicrotransHammer(const char *pchCategory, F32 fSuccessRate);
void gslAPMicrotransHammerDelay(F32 fDelay);
void gslAPMicrotransHammerCurrency(const char *pchCurrency);

#endif