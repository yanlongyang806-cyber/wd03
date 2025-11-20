#include "AccountProxyCommon.h"
#include "AccountProxyCommon_c_ast.h"
#include "SuperAssert.h"

#include "AccountDataCache.h"
#include "AutoTransDefs.h"
#include "chatCommon.h"
#include "DiscountShared.h"
#include "Entity.h"
#include "Entity_h_ast.h"
#include "EntityLib.h"
#include "file.h"
#include "GameAccountData\GameAccountData.h"
#include "GameAccountDataCommon.h"
#include "GameStringFormat.h"
#include "GlobalTypeEnum.h"
#include "inventoryCommon.h"
#include "itemCommon.h"
#include "logging.h"
#include "MicroTransactions.h"
#include "microtransactions_common.h"
#include "MicroTransactions_Transact.h"
#include "Money.h"
#include "NotifyCommon.h"
#include "objContainer.h"
#include "objTransactions.h"
#include "Player.h"
#include "Powers.h"
#include "SavedPetCommon.h"
#include "ShardCommon.h"
#include "netprivate.h"
#include "rand.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "TimedCallback.h"

#include "AccountDataCache_h_ast.h"
#include "accountnet_h_ast.h"
#include "GameAccountData_h_ast.h"
#include "MicroTransactions_h_ast.h"
#include "microtransactions_common_h_ast.h"
#include "Player_h_ast.h"

#ifdef GAMESERVER
#include "gslGameAccountData.h"
#include "LoggedTransactions.h"
#include "gslMicroTransactions.h"
#include "gslSendToClient.h"
#include "gslTransactions.h"
#include "WebRequestServer/wrContainerSubs.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/ChatServer_autogen_RemoteFuncs.h"
#endif

#ifdef APPSERVER
#include "aslLoginServer.h"
#include "aslLoginCStore.h"
#include "aslLogin2_StateMachine.h"
#endif

#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/AppServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"

AUTO_STRUCT;
typedef struct CallbackHolder
{
	char *key;											AST(ESTRING)
	AccountKeyValueSetCallback callback;				NO_AST
	AccountKeyValueMoveCallback moveCallback;			NO_AST
	void *userData;										NO_AST
} CallbackHolder;

AUTO_STRUCT;
typedef struct SimpleSetHolderCommon
{
	char *pKey;											AST(ESTRING)
	AccountKeyValueSimpleSetCallback callback;			NO_AST
	void *userData;										NO_AST
} SimpleSetHolderCommon;

typedef struct GetProductCallbackHolder
{
	AccountGetProductCallback callback;
	void *userData;
} GetProductCallbackHolder;


AUTO_STRUCT;
typedef struct PurchaseHolder
{
	EntityRef entRef;
	U32 iAccountID;
	int iLangID;
	char *pPaymentMethodVID;	AST(ESTRING)
	char *pIP;					AST(ESTRING)
	Money *pExpectedPrice;
	U32 uOverridePercentageDiscount;

	// item id for coupon discount, used to destroy item on success
	U64 uCouponItemId;
	
} PurchaseHolder;

/************************************************************************/
/* Private functions                                                    */
/************************************************************************/

static const GameAccountData *APGetGameAccount(Entity *pEnt, U32 iAccountID)
{
	if(pEnt)
	{
		return entity_GetGameAccount(pEnt);
	}
	else
	{
#ifdef APPSERVER
        Login2State *loginState = aslLogin2_GetActiveLoginStateByAccountID(iAccountID);
		if( loginState )
        {
			return GET_REF(loginState->hGameAccountData);
        }
#endif
	}

	return NULL;
}

static void APNotifySend(Entity *pEnt, U32 iAccountID, NotifyType eType, const char *pchTranslatedText, MicroTransactionDef *pDef)
{
#ifdef GAMESERVER
	if(pEnt)
	{
		notify_NotifySend(pEnt, eType, pchTranslatedText, pDef ? pDef->pchName : "none", NULL);
	}
#endif
#ifdef APPSERVER
	if(iAccountID)
	{
        Login2State *loginState = aslLogin2_GetActiveLoginStateByAccountID(iAccountID);

		if( loginState )
		{
            aslLogin2_Notify(loginState, pchTranslatedText, eType, pDef ? pDef->pchName : "none" );
		}
	}
#endif
}

//Notifies the client thru the login server or game server 
void APSendPrintf(int iID, const char* format, ...)
{
	char *estrBuffer = NULL;
	VA_START(args, format);
	estrConcatfv(&estrBuffer, format, args);

#ifdef GAMESERVER
	{
		Entity *pEnt = entFromEntityRefAnyPartition((EntityRef)iID);
		if(pEnt)
		{
			gslSendPrintf(pEnt, "%s", estrBuffer);
		}
	}
#endif
#ifdef APPSERVER
	{
		LoginLink *loginLink = aslFindLoginLinkForCookie(iID);
		aslNotifyLogin(loginLink,estrBuffer,kNotifyType_Failed,NULL);
	}
#endif

	VA_END();
	estrDestroy(&estrBuffer);
}

void APLog(Entity *pEnt, U32 iAccountID, const char *pchAction, const char *pchFormat, ...)
{
	if(pEnt)
	{
		VA_START(args, pchFormat);
		entLog_vprintf(LOG_MICROTRANSACTIONS, pEnt, pchAction, pchFormat, args);
		VA_END();
	}
	else
	{
		char *pchNewFormat = NULL;
		estrStackCreate(&pchNewFormat);
		estrPrintf(&pchNewFormat, "%s: %s", pchAction, pchFormat);

		//Do the printf
		VA_START(args, pchFormat);
		log_vprintf(LOG_MICROTRANSACTIONS, pchNewFormat, args);
		VA_END();
		
		estrDestroy(&pchNewFormat);
	}
}

static void SendKeyValueCommandResponse(SA_PARAM_NN_VALID TransactionReturnVal *returnVal, SA_PARAM_OP_VALID void *userData)
{
	CallbackHolder *holder = userData;
	AccountProxySetResponse *response = NULL;
	AccountKeyValueResult result = AKV_FAILURE;
	U32 uAccountID = 0;
	ContainerID containerID = 0;
	char *pKey = NULL;

	switch (RemoteCommandCheck_aslAPCmdSendLockRequest(returnVal, &response))
	{
	case TRANSACTION_OUTCOME_FAILURE:
		// This is assumed to happen when the account proxy server crashes
	xcase TRANSACTION_OUTCOME_SUCCESS:
		result = response->result;
		uAccountID = response->uAccountID;
		containerID = response->containerID;
		pKey = response->pKey;
		response->pKey = NULL;
		StructDestroy(parse_AccountProxySetResponse, response);
	}

	if (holder && holder->callback)
		(*holder->callback)(result, uAccountID, pKey ? pKey : holder->key, containerID, holder->userData);

	estrDestroy(&pKey);

	if (holder)
		StructDestroy(parse_CallbackHolder, holder);
}

static void SendKeyValueCommand(U32 uAccountID, SA_PARAM_OP_STR const char *key, S64 iValue, AccountProxySetOperation operation,
								SA_PARAM_OP_VALID AccountKeyValueSetCallback callback, SA_PARAM_OP_VALID void *userData)
{
	CallbackHolder *holder = StructCreate(parse_CallbackHolder);
	
	if (key) estrCopy2(&holder->key, key);
	holder->callback = callback;
	holder->userData = userData;

	RemoteCommand_aslAPCmdSendLockRequest(
		objCreateManagedReturnVal(SendKeyValueCommandResponse, holder),
		GLOBALTYPE_ACCOUNTPROXYSERVER, 0,
		uAccountID, key, iValue, operation);
}

static void SendKeyValueMoveCommandResponse(SA_PARAM_NN_VALID TransactionReturnVal *pReturnVal, SA_PRE_OP_VALID SA_POST_FREE CallbackHolder *pHolder)
{
	AccountProxyMoveResponse *pResponse = NULL;
	AccountKeyValueResult eResult = AKV_FAILURE;
	ContainerID uLockContainerID = 0;

	if (RemoteCommandCheck_aslAPCmdMoveKeyValue(pReturnVal, &pResponse) == TRANSACTION_OUTCOME_SUCCESS)
	{
		eResult = pResponse->eResult;
		uLockContainerID = pResponse->uLockContainerID;
		StructDestroy(parse_AccountProxyMoveResponse, pResponse);
	}

	if (pHolder && pHolder->moveCallback)
	{
		(*pHolder->moveCallback)(eResult, uLockContainerID, pHolder->userData);
	}

	if (pHolder)
	{
		StructDestroy(parse_CallbackHolder, pHolder);
	}
}

static void SendKeyValueMoveCommand(U32 uSrcAccountID, SA_PARAM_OP_STR const char *pSrcKey, U32 uDestAccountID, 
	SA_PARAM_OP_STR const char *pDestKey, S64 iValue, SA_PARAM_OP_VALID AccountKeyValueMoveCallback pCallback, SA_PARAM_OP_VALID void *pUserdata)
{
	CallbackHolder *pHolder = StructCreate(parse_CallbackHolder);

	pHolder->moveCallback = pCallback;
	pHolder->userData = pUserdata;

	RemoteCommand_aslAPCmdMoveKeyValue(objCreateManagedReturnVal(SendKeyValueMoveCommandResponse, pHolder), GLOBALTYPE_ACCOUNTPROXYSERVER, 0, uSrcAccountID, pSrcKey, uDestAccountID, pDestKey, iValue);
}

static void SetKeyValueSimpleResponse(SA_PARAM_NN_VALID TransactionReturnVal *returnVal, SA_PARAM_NN_VALID SimpleSetHolderCommon *holder)
{
	AccountKeyValueResult result = AKV_FAILURE;

	// Don't care if it succeeded or not since default is failure
	RemoteCommandCheck_aslAPCmdSetKeyValue(returnVal, (int *)&result);

	if (holder && holder->callback)
		(*holder->callback)(result, holder->pKey, holder->userData);

	if (holder)
		StructDestroy(parse_SimpleSetHolderCommon, holder);
}


/************************************************************************/
/* Public functions                                                     */
/************************************************************************/

// Simple, non-atomic set operation for key-values.  If increment is true, it will do += instead of = (negative values are okay)
void APSetKeyValueSimple(U32 uAccountID, SA_PARAM_NN_STR const char *key, S64 iValue, bool increment, SA_PARAM_OP_VALID AccountKeyValueSimpleSetCallback callback, SA_PARAM_OP_VALID void *userData)
{
	SimpleSetHolderCommon *holder = StructCreate(parse_SimpleSetHolderCommon);

	estrCopy2(&holder->pKey, key);
	holder->callback = callback;
	holder->userData = userData;

	RemoteCommand_aslAPCmdSetKeyValue(
		objCreateManagedReturnVal(SetKeyValueSimpleResponse, holder),
		GLOBALTYPE_ACCOUNTPROXYSERVER, 0,
		uAccountID, key, iValue, increment ? AKV_OP_INCREMENT : AKV_OP_SET);
}

void APSetKeyValue(U32 uAccountID, SA_PARAM_NN_STR const char *key, S64 value, SA_PARAM_OP_VALID AccountKeyValueSetCallback callback, SA_PARAM_OP_VALID void *userData)
{
	SendKeyValueCommand(uAccountID, key, value, AKV_OP_SET, callback, userData);
}

void APChangeKeyValue(U32 uAccountID, SA_PARAM_NN_STR const char *key, S64 value, SA_PARAM_OP_VALID AccountKeyValueSetCallback callback, SA_PARAM_OP_VALID void *userData)
{
	SendKeyValueCommand(uAccountID, key, value, AKV_OP_INCREMENT, callback, userData);
}

void APSetKey(U32 uAccountID, SA_PARAM_NN_STR const char *key, SA_PARAM_OP_VALID AccountKeyValueSetCallback callback, SA_PARAM_OP_VALID void *userData)
{
	APSetKeyValue(uAccountID, key, 1, callback, userData);
}

void APUnsetKey(U32 uAccountID, SA_PARAM_NN_STR const char *key, SA_PARAM_OP_VALID AccountKeyValueSetCallback callback, SA_PARAM_OP_VALID void *userData)
{
	APSetKeyValue(uAccountID, key, 0, callback, userData);
}

void APMoveKeyValue(U32 uSrcAccountID, SA_PARAM_NN_STR const char *pSrcKey, U32 uDestAccountID, SA_PARAM_NN_STR const char *pDestKey, S64 iValue, SA_PARAM_OP_VALID AccountKeyValueMoveCallback pCallback, SA_PARAM_OP_VALID void *pUserdata)
{
	SendKeyValueMoveCommand(uSrcAccountID, pSrcKey, uDestAccountID, pDestKey, iValue, pCallback, pUserdata);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pContainer, ".Plock.Uaccountid, .Plock.Pkey, .Plock.Result, .Plock.Fdestroytime, .Plock.Etransactiontype");
bool APFinalizeKeyValue(ATH_ARG NOCONST(AccountProxyLockContainer) *pContainer, U32 accountID, SA_PARAM_NN_STR const char *key, AccountProxyResult result, TransactionLogType eTransactionType)
{
	if (ISNULL(pContainer) || ISNULL(key)) return false;
	if (ISNULL(pContainer->pLock)) return false;
	if (pContainer->pLock->result == APRESULT_TIMED_OUT) return false;
	if (pContainer->pLock->uAccountID != accountID) return false;
	if (key && key[0] && stricmp(pContainer->pLock->pKey, key)) return false;

	pContainer->pLock->result = result;
	pContainer->pLock->fDestroyTime = 0;
	pContainer->pLock->eTransactionType = eTransactionType;
	return true;
}

static AccountKeyValueResult getKeyValueResultFromPurchase(PurchaseResult ePurchaseResult)
{
	switch (ePurchaseResult)
	{
	case PURCHASE_RESULT_COMMIT: return AKV_SUCCESS;
	case PURCHASE_RESULT_MISSING_ACCOUNT: return AKV_FAILURE;
	case PURCHASE_RESULT_INVALID_PAYMENT_METHOD: return AKV_FAILURE;
	case PURCHASE_RESULT_INVALID_PRODUCT: return AKV_FAILURE;
	case PURCHASE_RESULT_CANNOT_ACTIVATE: return AKV_LOCKED;
	case PURCHASE_RESULT_PENDING: return AKV_SUCCESS;
	case PURCHASE_RESULT_INVALID_PRICE: return AKV_FAILURE;
	case PURCHASE_RESULT_INSUFFICIENT_POINTS: return AKV_INVALID_RANGE;
	case PURCHASE_RESULT_COMMIT_FAIL: return AKV_FAILURE;
	case PURCHASE_RESULT_INVALID_CURRENCY: return AKV_INVALID_KEY;
	case PURCHASE_RESULT_AUTH_FAIL: return AKV_FAILURE;
	case PURCHASE_RESULT_MISSING_PRODUCTS: return AKV_FAILURE;
	case PURCHASE_RESULT_ROLLBACK: return AKV_FAILURE;
	case PURCHASE_RESULT_ROLLBACK_FAIL: return AKV_FAILURE;
	case PURCHASE_RESULT_INVALID_IP: return AKV_FAILURE;
	case PURCHASE_RESULT_RETRIEVED: return AKV_FAILURE;
	case PURCHASE_RESULT_RETRIEVE_FAILED: return AKV_FAILURE;
	case PURCHASE_RESULT_PENDING_PAYPAL: return AKV_FAILURE;
	case PURCHASE_RESULT_DELAY_FAILURE: return AKV_FAILURE;
	case PURCHASE_RESULT_SPENDING_CAP_REACHED: return AKV_FAILURE;
	case PURCHASE_RESULT_INVALID_PURCHASE_ID: return AKV_FAILURE;
	case PURCHASE_RESULT_PRICE_CHANGED: return AKV_FORBIDDEN_CHANGE;
	case PURCHASE_RESULT_CURRENCY_LOCKED: return AKV_LOCKED;
	case PURCHASE_RESULT_UNKNOWN_ERROR: return AKV_FAILURE;
	default: return AKV_FAILURE;
	}
}

AUTO_STRUCT;
typedef struct APPurchaseInformation
{
	U32 uAccountID;
	AccountProxyProduct *pAccountProxyProduct;
	Money *pExpectedPrice;
	char *pPaymentMethodID;

	AccountBeginPurchaseCallback pCallback; NO_AST
	void *pUserData;						NO_AST
} APPurchaseInformation;

static void APBeginPurchaseResponse(SA_PARAM_NN_VALID TransactionReturnVal *pReturnValue, SA_PARAM_NN_VALID void *pUserData)
{
	AccountKeyValueResult eKeyValueResult = AKV_FAILURE;
	APPurchaseInformation *pInfo = pUserData;
	ContainerID containerID = 0;
	AuthCaptureResultInfo * pResponse = NULL;

	if (RemoteCommandCheck_aslAPCmdAuthCapture(pReturnValue, &pResponse) == TRANSACTION_OUTCOME_SUCCESS)
	{
		eKeyValueResult = getKeyValueResultFromPurchase(pResponse->eResult);
		containerID = pResponse->uLockContainerID;
		StructDestroy(parse_AuthCaptureResultInfo, pResponse);
	}

	if (devassert(pInfo->pCallback))
		(*pInfo->pCallback)(eKeyValueResult, pInfo->uAccountID, pInfo->pAccountProxyProduct, pInfo->pExpectedPrice, pInfo->pPaymentMethodID, containerID, pInfo->pUserData);

	StructDestroy(parse_APPurchaseInformation, pInfo);
}

void APBeginPurchase(U32 uAccountID, SA_PARAM_NN_VALID const AccountProxyProduct *pProxyProduct, SA_PARAM_NN_VALID const Money *pExpectedPrice, U32 uOverrideDiscount,
					 SA_PARAM_OP_STR const char *pPaymentMethodID, SA_PARAM_NN_VALID AccountBeginPurchaseCallback pCallback, SA_PARAM_OP_VALID void *pUserData)
{
	const char *pKey = NULL;
	const Money *pMoneyPrice = NULL;

	PERFINFO_AUTO_START_FUNC();

	// We must have a product and currency
	if(!devassert(pProxyProduct && moneyCurrency(pExpectedPrice)))
	{
		if (devassert(pCallback)) (*pCallback)(AKV_INVALID_KEY, uAccountID, pProxyProduct, pExpectedPrice, pPaymentMethodID, 0, pUserData);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Make sure we found a price and that it's for a virtual currency
	if (isRealCurrency(moneyCurrency(pExpectedPrice)) || !(pKey = moneyKeyName(pExpectedPrice)))
	{
		if (devassert(pCallback)) (*pCallback)(AKV_INVALID_KEY, uAccountID, pProxyProduct, pExpectedPrice, pPaymentMethodID, 0, pUserData);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Begin the actual purchase using auth-capture method
	{
		AuthCaptureRequest request = {0};
		TransactionItem * pItem = StructCreate(parse_TransactionItem);
		APPurchaseInformation * pInfo = StructCreate(parse_APPurchaseInformation);

		StructInit(parse_AuthCaptureRequest, &request);
		request.bAuthOnly = true;
		request.pCurrency = strdup(moneyCurrency(pExpectedPrice));
		request.pPaymentMethodID = strdup(pPaymentMethodID);
		request.uAccountID = uAccountID;

		pItem->uProductID = pProxyProduct->uID;
		pItem->pPrice = StructClone(parse_Money, pExpectedPrice);
		pItem->uOverridePercentageDiscount = uOverrideDiscount;
		eaPush(&request.eaItems, pItem);

		pInfo->pAccountProxyProduct = StructClone(parse_AccountProxyProduct, pProxyProduct);
		pInfo->pCallback = pCallback;
		pInfo->pExpectedPrice = StructClone(parse_Money, pExpectedPrice);
		pInfo->pPaymentMethodID = strdup(pPaymentMethodID);
		pInfo->pUserData = pUserData;
		pInfo->uAccountID = uAccountID;
		
		RemoteCommand_aslAPCmdAuthCapture(objCreateManagedReturnVal(APBeginPurchaseResponse, pInfo),
			GLOBALTYPE_ACCOUNTPROXYSERVER, 0, &request);

		StructDeInit(parse_AuthCaptureRequest, &request);
	}

	PERFINFO_AUTO_STOP_FUNC();
}

AUTO_TRANS_HELPER
ATR_LOCKS(pContainer, ".Plock.Uaccountid, .Plock.Result, .Plock.Fdestroytime, .Plock.Pkey, .Plock.Etransactiontype");
bool APFinishPurchase(ATH_ARG NOCONST(AccountProxyLockContainer) *pContainer, U32 accountID, SA_PARAM_NN_VALID const AccountProxyProduct *product, SA_PARAM_NN_STR const char *currency, SA_PARAM_OP_STR const char *pmVID, AccountProxyResult result, TransactionLogType eTransactionType)
{
	return APFinalizeKeyValue(pContainer, accountID, currency, result, eTransactionType);
}

typedef struct ProductListCallbackWrapper
{
	ProductListCallback pCallback;
	char *pCategory;
	void *pUserdata;
} ProductListCallbackWrapper;

typedef struct DiscountListCallbackWrapper
{
	DiscountListCallback pCallback;
	void *pUserdata;
} DiscountListCallbackWrapper;

static MicroTransactionProductList *sCachedMicroTransactionList = NULL;
static bool sbAPCachingDebug = false;
AUTO_CMD_INT(sbAPCachingDebug, APCachingDebug) ACMD_CMDLINE;

static void APGetProductListCB(TransactionReturnVal *pReturnVal, ProductListCallbackWrapper *pWrapper)
{
	AccountProxyProductList *pList = NULL;

	PERFINFO_AUTO_START_FUNC();
	if (!sCachedMicroTransactionList)
	{
		sCachedMicroTransactionList = StructCreate(parse_MicroTransactionProductList);
	}

	if (RemoteCommandCheck_aslAPCmdRequestProductList(pReturnVal, &pList) == TRANSACTION_OUTCOME_SUCCESS)
	{
		if (ADCReplaceProductCache(pList))
		{
			if (sbAPCachingDebug)
			{
				printf("Received new product list: (%d, %d)\n", pList->uTimestamp, pList->uVersion);
			}

			if (g_pMainMTCategory)
			{
				PERFINFO_AUTO_START("Cache update", 1);
				StructReset(parse_MicroTransactionProductList, sCachedMicroTransactionList);
				GenerateMicrotransactionList(pList, sCachedMicroTransactionList, g_pMainMTCategory);
				PERFINFO_AUTO_STOP();
			}
		}

		ADCFreshenProducts(0);
		StructDestroy(parse_AccountProxyProductList, pList);
	}

	(*pWrapper->pCallback)(pWrapper->pCategory, ADCGetProductsByCategory(pWrapper->pCategory), pWrapper->pUserdata);
	free(pWrapper->pCategory);
	free(pWrapper);
	PERFINFO_AUTO_STOP();
}

void APGetProductList(SA_PARAM_NN_STR const char *pCategory, SA_PARAM_NN_VALID ProductListCallback pCallback, SA_PARAM_OP_VALID void *pUserdata)
{
	const AccountProxyProductList *pList = NULL;

	if (ADCProductsAreFresh() && (pList = ADCGetProductsByCategory(pCategory)))
	{
		(*pCallback)(pCategory, pList, pUserdata);
	}
	else
	{
		ProductListCallbackWrapper *pWrapper = calloc(1, sizeof(ProductListCallbackWrapper));
		pWrapper->pCallback = pCallback;
		pWrapper->pCategory = strdup(pCategory);
		pWrapper->pUserdata = pUserdata;
		RemoteCommand_aslAPCmdRequestProductList(objCreateManagedReturnVal(APGetProductListCB, pWrapper),
			GLOBALTYPE_ACCOUNTPROXYSERVER, 0, ADCGetProductCacheTime(), ADCGetProductCacheVersion());
		ADCFreshenProducts(0);

		if (sbAPCachingDebug)
		{
			printf("Requesting product list update: (%d, %d)\n", ADCGetProductCacheTime(), ADCGetProductCacheVersion());
		}
	}
}

static void APGetDiscountListCB(TransactionReturnVal *pReturnVal, DiscountListCallbackWrapper *pWrapper)
{
	AccountProxyDiscountList *pList = NULL;

	if (RemoteCommandCheck_aslAPCmdRequestDiscountList(pReturnVal, &pList) == TRANSACTION_OUTCOME_SUCCESS)
	{
		if (ADCReplaceDiscountCache(pList))
		{
			if (sbAPCachingDebug)
			{
				printf("Received new discount list: (%d, %d)\n", pList->uTimestamp, pList->uVersion);
			}
		}

		ADCFreshenDiscounts(0);
		StructDestroy(parse_AccountProxyDiscountList, pList);
	}

	(*pWrapper->pCallback)(ADCGetDiscounts(), pWrapper->pUserdata);
	free(pWrapper);
}

void APGetDiscountList(SA_PARAM_NN_VALID DiscountListCallback pCallback, SA_PARAM_OP_VALID void *pUserdata)
{
	const AccountProxyDiscountList *pList = NULL;

	if (ADCDiscountsAreFresh() && (pList = ADCGetDiscounts()) && pList->uVersion)
	{
		(*pCallback)(pList, pUserdata);
	}
	else
	{
		DiscountListCallbackWrapper *pWrapper = calloc(1, sizeof(DiscountListCallbackWrapper));
		pWrapper->pCallback = pCallback;
		pWrapper->pUserdata = pUserdata;
		RemoteCommand_aslAPCmdRequestDiscountList(objCreateManagedReturnVal(APGetDiscountListCB, pWrapper),
			GLOBALTYPE_ACCOUNTPROXYSERVER, 0, ADCGetDiscountCacheTime(), ADCGetDiscountCacheVersion());
		ADCFreshenDiscounts(0);

		if (sbAPCachingDebug)
		{
			printf("Requesting discount list update: (%d, %d)\n", ADCGetDiscountCacheTime(), ADCGetDiscountCacheVersion());
		}
	}
}

typedef struct CatalogRequestWrapper
{
	U32 uContainerID;
	U32 uAccountID;
	U32 uProductVersion;
	U32 uDiscountVersion;

	union {
		MTCatalogCallback pMTCallback;
		PointBuyCatalogCallback pPointBuyCallback;
	};

	void *pUserdata;
} CatalogRequestWrapper;

static void APTryDiscount(SA_PARAM_NN_VALID MicroTransactionProduct ***pppMTProducts, SA_PARAM_NN_VALID MicroTransactionProduct ***pppDiscounted, SA_PARAM_OP_VALID const AccountProxyDiscount *pDiscount, SA_PARAM_OP_VALID const AccountProxyKeyValueInfoList *pKVList)
{
	bool bPrereqsChecked = false;
	int i = 0;

	PERFINFO_AUTO_START_FUNC();
	for (i = eaSize(pppMTProducts) - 1; i >= 0; --i)
	{
		MicroTransactionProduct *pMTProduct = (*pppMTProducts)[i];
		AccountProxyProduct *pProduct = NULL;

		if (!devassert(pMTProduct))
			continue; // Go to next product

		pProduct = pMTProduct->pProduct;

		if (!devassert(pProduct))
			continue; // Go to next product

		// Only okay because we know microtransaction products will only have one currency
		// If there isn't exactly one currency, skip this product -- 0 currencies would mean that we crash, more than one is not supported
		if (eaSize(&pProduct->ppFullMoneyPrices) != 1 || stricmp(moneyCurrency(pProduct->ppFullMoneyPrices[0]), microtrans_GetShardCurrencyExactName()))
			continue;

		if (pDiscount && !DiscountShared_AppliesToProduct(pDiscount->pInternalName, pDiscount->ppProducts, pDiscount->ppCategories, pDiscount->bBlacklistProducts,
			pDiscount->bBlacklistCategories, pProduct->pName, pProduct->pInternalName, pProduct->ppCategories))
			continue; // Go to next product (discount doesn't affect this product)

		if (!bPrereqsChecked)
		{
			if (pDiscount && !AccountProxyKeysMeetRequirements(pKVList, pDiscount->ppPrerequisites, AccountGetShardProxyName(), ShardCommon_GetClusterName(), microtrans_GetShardEnvironmentName(), NULL, NULL))
				break; // Skip all products, go to next discount (we don't qualify for this discount)

			bPrereqsChecked = true;
		}

		StructDestroySafe(parse_Money, &pProduct->ppMoneyPrices[0]);

		if (pDiscount)
		{
			U32 uPrice = moneyCountPoints(pProduct->ppFullMoneyPrices[0]);
			APPLY_DISCOUNT_EFFECT(uPrice, pDiscount->uPercentageDiscount);
			pProduct->ppMoneyPrices[0] = moneyCreateFromInt(uPrice, microtrans_GetShardCurrencyExactName());;
		}
		else
		{
			pProduct->ppMoneyPrices[0] = StructClone(parse_Money, pProduct->ppFullMoneyPrices[0]);
		}

		eaRemoveFast(pppMTProducts, i);
		eaPush(pppDiscounted, pMTProduct);
	}
	PERFINFO_AUTO_STOP();
}

void APDiscountMTCatalog(SA_PARAM_NN_VALID const MicroTransactionProductList **ppMTList, SA_PARAM_OP_VALID const AccountProxyKeyValueInfoList *pKVList)
{
	MicroTransactionProduct **ppDiscounted = NULL;
	const AccountProxyDiscountList *pDiscountList = ADCGetDiscounts();

	// The only valid inputs for ppMTList are a location to get the discounted MT list, or the already-sent-to-you cached MT list
	assert(!*ppMTList || *ppMTList == sCachedMicroTransactionList);

	PERFINFO_AUTO_START_FUNC();
	eaIndexedEnable(&ppDiscounted, parse_MicroTransactionProduct);
	eaSetCapacity(&ppDiscounted, eaSize(&sCachedMicroTransactionList->ppProducts));

	EARRAY_CONST_FOREACH_BEGIN(pDiscountList->ppList, iCurDiscount, iNumDiscounts);
	{
		const AccountProxyDiscount *pDiscount = pDiscountList->ppList[iCurDiscount];
		U32 uNow = timeSecondsSince2000();

		if (!devassert(pDiscount))
			continue; // Go to next discount

		if (!DiscountShared_IsActive(pDiscount, uNow))
			continue; // Go to next discount (skip inactive discounts)

		APTryDiscount(&sCachedMicroTransactionList->ppProducts, &ppDiscounted, pDiscount, pKVList);
	}
	EARRAY_FOREACH_END;

	APTryDiscount(&sCachedMicroTransactionList->ppProducts, &ppDiscounted, NULL, pKVList);
	eaDestroy(&sCachedMicroTransactionList->ppProducts);
	sCachedMicroTransactionList->ppProducts = ppDiscounted;

	*ppMTList = sCachedMicroTransactionList;
	PERFINFO_AUTO_STOP();
}

static void APMTCatalog_DiscountCB(SA_PARAM_OP_VALID const AccountProxyDiscountList *pList, SA_PARAM_NN_VALID CatalogRequestWrapper *pWrapper)
{
	PERFINFO_AUTO_START_FUNC();
	pWrapper->pMTCallback(pWrapper->uContainerID, pWrapper->uAccountID, sCachedMicroTransactionList, pWrapper->pUserdata);
	free(pWrapper);
	PERFINFO_AUTO_STOP();
}

static void APMTCatalog_ProductCB(SA_PARAM_NN_VALID const char *pCategory, SA_PARAM_OP_VALID const AccountProxyProductList *pList, SA_PARAM_NN_VALID CatalogRequestWrapper *pWrapper)
{
	APGetDiscountList(APMTCatalog_DiscountCB, pWrapper);
}

void APGetMTCatalog(U32 uContainerID, U32 uAccountID, SA_PARAM_NN_VALID MTCatalogCallback pCallback, SA_PARAM_OP_VALID void *pUserdata)
{
	CatalogRequestWrapper *pWrapper = NULL;
	static char pcBuffer[128] = {0};

	if (!g_pMainMTCategory) return;

	if (!pcBuffer[0])
		sprintf(pcBuffer, "%s.%s", microtrans_GetShardCategoryPrefix(), g_pMainMTCategory->pchName);

	pWrapper = calloc(1, sizeof(CatalogRequestWrapper));
	pWrapper->uContainerID = uContainerID;
	pWrapper->uAccountID = uAccountID;
	pWrapper->uProductVersion = ADCGetProductCacheVersion();
	pWrapper->uDiscountVersion = ADCGetDiscountCacheVersion();
	pWrapper->pMTCallback = pCallback;
	pWrapper->pUserdata = pUserdata;

	APGetProductList(pcBuffer, APMTCatalog_ProductCB, pWrapper);
}

static void APPointBuyCatalog_DiscountCB(SA_PARAM_OP_VALID const AccountProxyDiscountList *pList, SA_PARAM_NN_VALID CatalogRequestWrapper *pWrapper)
{
	AccountProxyProductList *pProductList = StructClone(parse_AccountProxyProductList, ADCGetProductsByCategory(microtrans_GetGlobalPointBuyCategory()));
	pWrapper->pPointBuyCallback(pWrapper->uContainerID, pWrapper->uAccountID, pProductList, pWrapper->pUserdata);
	free(pWrapper);
	StructDestroy(parse_AccountProxyProductList, pProductList);
}

static void APPointBuyCatalog_ProductCB(SA_PARAM_NN_VALID const char *pCategory, SA_PARAM_OP_VALID const AccountProxyProductList *pList, SA_PARAM_NN_VALID CatalogRequestWrapper *pWrapper)
{
	// Honestly we just care that this call caused the product list to be refreshed if necessary
	APGetDiscountList(APPointBuyCatalog_DiscountCB, pWrapper);
}

void APGetPointBuyCatalog(U32 uContainerID, U32 uAccountID, SA_PARAM_NN_VALID PointBuyCatalogCallback pCallback, SA_PARAM_OP_VALID void *pUserdata)
{
	CatalogRequestWrapper *pWrapper = NULL;
	const char *pcPointBuyCategory = microtrans_GetGlobalPointBuyCategory();

	pWrapper = calloc(1, sizeof(CatalogRequestWrapper));
	pWrapper->uContainerID = uContainerID;
	pWrapper->uAccountID = uAccountID;
	pWrapper->uProductVersion = ADCGetProductCacheVersion();
	pWrapper->uDiscountVersion = ADCGetDiscountCacheVersion();
	pWrapper->pPointBuyCallback = pCallback;
	pWrapper->pUserdata = pUserdata;

	APGetProductList(pcPointBuyCategory, APPointBuyCatalog_ProductCB, pWrapper);
}

void GenerateMicrotransactionList(const AccountProxyProductList *pList, MicroTransactionProductList *pMTList, MicroTransactionCategory *pMTCategory)
{
	S32 i, iCatIdx;

	if (!pList)
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC();
	for (i = eaSize(&pList->ppList) - 1; i >= 0; i--)
	{
		const AccountProxyProduct *pProduct = pList->ppList[i];
		MicroTransactionDef *pDef = microtrans_findDefFromAPProd(pProduct);
		bool bFoundCat = false;

		for (iCatIdx = eaSize(&pProduct->ppCategories) - 1; iCatIdx >= 0; iCatIdx--)
		{
			MicroTransactionCategory *pProdCategory = microtrans_CategoryFromStr(pProduct->ppCategories[iCatIdx]);
			if(pProdCategory && pMTCategory == pProdCategory)
			{
				bFoundCat = true;
			}
		}

		//If there's a micro trans def
		if (bFoundCat && pProduct && pDef)
		{
			MicroTransactionProduct *pMTProduct = StructCreate(parse_MicroTransactionProduct);
			pMTProduct->uID = pProduct->uID;

			SET_HANDLE_FROM_REFERENT(g_hMicroTransDefDict, pDef, pMTProduct->hDef);

			pMTProduct->pProduct = StructClone(parse_AccountProxyProduct, pProduct);
			pMTProduct->bFreeForPremiumMembers = microtrans_IsPremiumEntitlement(pDef);

			PERFINFO_AUTO_START("Category dup", 1);
			for (iCatIdx = eaSize(&pProduct->ppCategories) - 1; iCatIdx >= 0; iCatIdx--)
			{
				MicroTransactionCategory *pProdCategory = microtrans_CategoryFromStr(pProduct->ppCategories[iCatIdx]);
				if(pProdCategory)
				{
					if (eaFindString(&pMTProduct->ppchCategories, pProdCategory->pchName) == -1)
						eaPush(&pMTProduct->ppchCategories, allocAddString(pProdCategory->pchName));
				}
			}
			PERFINFO_AUTO_STOP();

			eaPush(&pMTList->ppProducts, pMTProduct);
		}
	}
	PERFINFO_AUTO_STOP();
}

/************************************************************************/
/* Auto-command support                                                 */
/************************************************************************/

AUTO_TRANSACTION
ATR_LOCKS(pContainer, ".Plock.Uaccountid, .Plock.Result, .Plock.Fdestroytime, .Plock.Pkey, .Plock.Etransactiontype");
enumTransactionOutcome AccountProxy_tr_FinishLock(ATR_ARGS, NOCONST(AccountProxyLockContainer) *pContainer, U32 accountID, SA_PARAM_NN_STR const char *key, U32 eTransactionType)
{
	return APFinalizeKeyValue(pContainer, accountID, key, APRESULT_COMMIT, eTransactionType) ? TRANSACTION_OUTCOME_SUCCESS : TRANSACTION_OUTCOME_FAILURE;
}

AUTO_TRANSACTION
ATR_LOCKS(pContainer, ".Plock.Uaccountid, .Plock.Result, .Plock.Fdestroytime, .Plock.Pkey, .Plock.Etransactiontype");
enumTransactionOutcome AccountProxy_tr_RollbackLock(ATR_ARGS, NOCONST(AccountProxyLockContainer) *pContainer, U32 accountID, SA_PARAM_NN_STR const char *key)
{
    return APFinalizeKeyValue(pContainer, accountID, key, APRESULT_ROLLBACK, TransLogType_Other) ? TRANSACTION_OUTCOME_SUCCESS : TRANSACTION_OUTCOME_FAILURE;
}

static void APGetProductByIDCB(TransactionReturnVal *returnStruct, void *userData)
{
	AccountProxyProduct *product;
	GetProductCallbackHolder *holder = userData;

	if (RemoteCommandCheck_aslAPCmdGetProductByID(returnStruct, &product) != TRANSACTION_OUTCOME_SUCCESS || !product)
	{
		(*holder->callback)(NULL, holder->userData);
		free(holder);
		return;
	}

	(*holder->callback)(product, holder->userData);
	StructDestroy(parse_AccountProxyProduct, product);
	free(holder);
}

void APGetProductByID(SA_PARAM_NN_STR const char *pCategory, U32 uProductID, SA_PARAM_NN_VALID AccountGetProductCallback callback, SA_PARAM_OP_VALID void *userData)
{
	GetProductCallbackHolder *holder = calloc(sizeof(GetProductCallbackHolder), 1);

	holder->callback = callback;
	holder->userData = userData;

	RemoteCommand_aslAPCmdGetProductByID(objCreateManagedReturnVal(APGetProductByIDCB, holder), GLOBALTYPE_ACCOUNTPROXYSERVER, 0, pCategory, uProductID);
}

static void APGetProductByNameCB(TransactionReturnVal *returnStruct, void *userData)
{
	AccountProxyProduct *product;
	GetProductCallbackHolder *holder = userData;

	if (RemoteCommandCheck_aslAPCmdGetProductByName(returnStruct, &product) != TRANSACTION_OUTCOME_SUCCESS || !product)
	{
		(*holder->callback)(NULL, holder->userData);
		free(holder);
		return;
	}

	(*holder->callback)(product, holder->userData);
	StructDestroy(parse_AccountProxyProduct, product);
	free(holder);
}

void APGetProductByName(const char *pCategory, const char *pProductName, AccountGetProductCallback callback, void *userData)
{
	GetProductCallbackHolder *holder = calloc(sizeof(GetProductCallbackHolder), 1);

	holder->callback = callback;
	holder->userData = userData;

	RemoteCommand_aslAPCmdGetProductByName(objCreateManagedReturnVal(APGetProductByNameCB, holder), GLOBALTYPE_ACCOUNTPROXYSERVER, 0, pCategory, pProductName);
}

/************************************************************************/
/* Purchase logic                                                       */
/************************************************************************/

AUTO_STRUCT;
typedef struct PurchaseLogicStruct
{
	ContainerID iEntityContID;
	U32 uAccountID;
	int iLangID;
	AccountProxyProduct *pProduct;
	REF_TO(MicroTransactionDef) hDef;
	Money *pExpectedPrice;
	char *pPaymentMethodVID;
	ContainerID containerID;
	MicroTransactionRewards* pRewards;

	// item id for coupon discount, used to destroy item on success
	U64 uCouponItemId;
} PurchaseLogicStruct;

static void RollbackPurchase_CB(TransactionReturnVal *pReturn, PurchaseLogicStruct *pCBStruct)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pCBStruct->iEntityContID);
	const char *pchAction = NULL;

	if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		pchAction = "KeyRollbackSuccess";
	}
	else
	{
		pchAction = "KeyRollbackFailure";
	}
	
	APLog(pEnt, pCBStruct->uAccountID, pchAction, "AccountID %d, ProductID %d, Currency %s",
		pCBStruct->uAccountID,
		pCBStruct->pProduct->uID,
		moneyCurrency(pCBStruct->pExpectedPrice));
	
	StructDestroy(parse_PurchaseLogicStruct,pCBStruct);
}

AUTO_TRANSACTION
ATR_LOCKS(pContainer, ".Plock.Uaccountid, .Plock.Result, .Plock.Fdestroytime, .Plock.Pkey, .Plock.Etransactiontype");
enumTransactionOutcome AccountProxy_tr_RollbackPurchase(ATR_ARGS, NOCONST(AccountProxyLockContainer) *pContainer, U32 uAccountID, const AccountProxyProduct *pProduct, const char *currency, const char *pPaymentMethodVID)
{
	if(APFinishPurchase(pContainer, uAccountID, pProduct, currency, pPaymentMethodVID, APRESULT_ROLLBACK, TransLogType_Other))
	{
		return(TRANSACTION_OUTCOME_SUCCESS);
	}
	else
	{
		return(TRANSACTION_OUTCOME_FAILURE);
	}
}

#ifdef GAMESERVER

typedef struct APMessageCBData
{
	EntityRef targetEnt;
	char* pMsg;
} APMessageCBData;


static void AP_ItemRemovalCB(TransactionReturnVal* returnVal, void* userData)
{
	APMessageCBData* pCBData = (APMessageCBData*)userData;
	Entity *pEnt = entFromEntityRefAnyPartition(pCBData->targetEnt);

	if (pEnt &&
		pEnt->pPlayer &&
		pCBData->pMsg)
	{
		notify_NotifySend(pEnt, kNotifyType_ItemLost, pCBData->pMsg, NULL, NULL);

		estrDestroy(&pCBData->pMsg);
		free(pCBData);
	}
}

#endif

static void FinishPurchase(TransactionReturnVal *pReturn, PurchaseLogicStruct *pCBStruct)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pCBStruct->iEntityContID);
	const char *pchNotifyMesg = NULL;
	NotifyType eNotifyType = kNotifyType_MicroTransFailed;
	switch (pReturn->eOutcome)
	{
	default:
	case TRANSACTION_OUTCOME_FAILURE:
		{

			PurchaseLogicStruct *pNewCBStruct = StructClone(parse_PurchaseLogicStruct, pCBStruct);
			if(pNewCBStruct)
			{
				TransactionReturnVal *returnVal = NULL;

				if(pEnt)
				{
#ifdef GAMESERVER
					returnVal = LoggedTransactions_CreateManagedReturnValEnt("MicroPurchaseRollback", pEnt, RollbackPurchase_CB, pNewCBStruct);
#endif
				}
				else if(entIsServer())
				{
#ifdef GAMESERVER
					returnVal = LoggedTransactions_CreateManagedReturnVal("MicroPurchaseRollback", RollbackPurchase_CB, pNewCBStruct);
#endif
				}
				else
				{
					returnVal = objCreateManagedReturnVal(RollbackPurchase_CB, pNewCBStruct);
				}

				devassert(returnVal != NULL);

				APLog(pEnt, pCBStruct->uAccountID, "MicroTransactionFailed", "AccountID %d, ProductID %d, Currency %s",
					pCBStruct->uAccountID,
					pCBStruct->pProduct->uID,
					moneyCurrency(pCBStruct->pExpectedPrice));

				AutoTrans_AccountProxy_tr_RollbackPurchase(returnVal, objServerType(), 
					GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, pNewCBStruct->containerID, 
					pNewCBStruct->uAccountID,
					pNewCBStruct->pProduct,
					moneyCurrency(pNewCBStruct->pExpectedPrice),
					pNewCBStruct->pPaymentMethodVID);
			}

			eNotifyType = kNotifyType_MicroTransFailed;
			pchNotifyMesg = "MicroTrans_GrantMicroTransFailed_Mesg";
			break;
		}
	case TRANSACTION_OUTCOME_SUCCESS:
		{
			MicroTransactionDef *pDef = GET_REF(pCBStruct->hDef);
			
			APLog(pEnt, pCBStruct->uAccountID, "MicroTransactionSuccess", "AccountID %d, ProductID %d, Currency %s",
				pCBStruct->uAccountID,
				pCBStruct->pProduct->uID,
				moneyCurrency(pCBStruct->pExpectedPrice));

			eNotifyType = kNotifyType_MicroTransSuccess;
			pchNotifyMesg = "MicroTrans_Success_Mesg";

			if(pEnt 
				&& pEnt->pPlayer 
				&& pEnt->pPlayer->pMicroTransInfo 
				&& pDef 
				&& pDef->bOnePerCharacter)
			{
				entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
				entity_SetDirtyBit(pEnt, parse_PlayerMTInfo, pEnt->pPlayer->pMicroTransInfo, false);
			}

#ifdef GAMESERVER
			if(pEnt && pDef)
			{
				int i;
				bool bRunTrans = false;
				for(i=eaSize(&pDef->eaParts)-1; i>=0; i--)
				{
					PowerDef *pPowerDef = GET_REF(pDef->eaParts[i]->hPowerDef);
					if( pPowerDef 
						&& pDef->eaParts[i]->ePartType == kMicroPart_VanityPet)
					{
						bRunTrans = true;

						RemoteCommand_entityCmd_VanityPetUnlocked( pEnt->myEntityType, pEnt->myContainerID, pPowerDef->pchName);
					}
				}
				if (pCBStruct->pRewards && eaSize(&pCBStruct->pRewards->eaRewards))
				{
					RemoteCommand_entityCmd_DisplayMicroTransRewards(pEnt->myEntityType, pEnt->myContainerID, pCBStruct->pRewards);
				}

				if(bRunTrans)
				{
					gslGAD_ProcessNewVersion(pEnt, true, false);
				}
				// Time isn't exact, but is close enough
				RemoteCommand_ChatAd_UpdatePurchaseTime(GLOBALTYPE_CHATSERVER, 0, entGetAccountID(pEnt), timeSecondsSince2000());

				// remove the coupon item used to buy this
				if(pCBStruct->uCouponItemId > 0)
				{
					Item *pItem = inv_GetItemByID(pEnt, pCBStruct->uCouponItemId);
					if(pItem)
					{
						ItemDef *pItemDef = GET_REF(pItem->hItem);
						GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
						APMessageCBData* pCBData = calloc(1,sizeof(APMessageCBData));
						TransactionReturnVal* pReturnCI;
						ItemChangeReason reason = {0};

						pCBData->targetEnt = pEnt->myRef;
						if (!(pItemDef->flags & kItemDefFlag_Silent))
						{
							const char* pchTag = NULL;
							if(pItemDef->eTag)
							{
								pchTag = StaticDefineGetTranslatedMessage(ItemTagEnum, pItemDef->eTag);
							}
							estrCreate(&pCBData->pMsg);
							entFormatGameMessageKey(pEnt, &pCBData->pMsg, "Reward.YouUsedItem", STRFMT_STRING("Name", item_GetNameLang(pItem, 0, pEnt)), STRFMT_STRING("Tag", NULL_TO_EMPTY(pchTag)),STRFMT_END);
						}

						inv_FillItemChangeReason(&reason, pEnt, "MicroTrans:CouponUsed", (pItemDef ? pItemDef->pchName : "Unknown") );

						pReturnCI = LoggedTransactions_CreateManagedReturnValEnt("RemoveByID", pEnt, AP_ItemRemovalCB, pCBData);
						AutoTrans_inv_ent_tr_RemoveItemByID(pReturnCI, GLOBALTYPE_GAMESERVER, 
								entGetType(pEnt), entGetContainerID(pEnt), 
								pCBStruct->uCouponItemId, 1, &reason, pExtract);
					}
				}
			}
#endif

			break;
		}
	}

	if(pchNotifyMesg && pchNotifyMesg[0])
	{
		char *estrMesgBuffer = NULL;

		langFormatGameMessageKey(pCBStruct->iLangID, &estrMesgBuffer, pchNotifyMesg, STRFMT_END);

		APNotifySend(pEnt, pCBStruct->uAccountID, eNotifyType, estrMesgBuffer, GET_REF(pCBStruct->hDef));

		estrDestroy(&estrMesgBuffer);
	}
	StructDestroy(parse_PurchaseLogicStruct, pCBStruct);
}

AUTO_TRANSACTION
ATR_LOCKS(pContainer, ".Plock.Uaccountid, .Plock.Result, .Plock.Fdestroytime, .Plock.Pkey, .Plock.Etransactiontype")
ATR_LOCKS(pEnt, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pplayer.Pmicrotransinfo.Eaonetimepurchases, .Psaved.Ppbuilds, .Pplayer.Pplayeraccountdata.Iversion, .Pplayer.Pugckillcreditlimit, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Ppownedcontainers, .pInventoryV2.Peaowneduniqueitems, .Psaved.Ppuppetmaster.Pppuppets, .pInventoryV2.Ppinventorybags, .pInventoryV2.Pplitebags, .Pplayer.Playertype, .Itemidmax, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Uiindexbuild")
ATR_LOCKS(eaPets, ".Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems, .Pcritter, .Pchar")
ATR_LOCKS(pAccountData, ".Iaccountid, .Eakeys, .Iversion, .Eacostumekeys, .Idayssubscribed, .Eatokens, .Eapermissions, .Eavanitypets, .Eaallpurchases[], .Blinkedaccount, .Bshadowaccount, .Eaaccountkeyvalues, .Umaxcharacterlevelcached, .Bbilled");
enumTransactionOutcome AccountProxy_tr_FinishPurchase_MicroTransactionDef(ATR_ARGS,
																		  NOCONST(AccountProxyLockContainer) *pContainer,
																		  NOCONST(Entity)* pEnt,
																		  CONST_EARRAY_OF(NOCONST(Entity)) eaPets,
																		  NOCONST(GameAccountData)* pAccountData,
																		  MicroTransactionRewards *pRewards,
																		  U32 uAccountID,
																		  const AccountProxyProduct *pProduct,
																		  const MicroTransactionDef *pDef,
																		  const char *pDefName,
																		  const char *pCurrency,
																		  const char *pPaymentMethodVID,
																		  ItemChangeReason *pReason)
{
	enumTransactionOutcome eOutcome = TRANSACTION_OUTCOME_FAILURE;

	PERFINFO_AUTO_START_FUNC();
	if(trhMicroTransactionDef_Grant(ATR_PASS_ARGS, pEnt, eaPets, pAccountData, pRewards, pDef, pDefName, pReason))
	{
		//Mark the fact that we did this transaction
		trhMicroTransDef_AddPurchaseStamp(pAccountData, pDef, pDefName, pProduct, pCurrency);

		//Finish the purchase
		if (APFinishPurchase(pContainer, uAccountID, pProduct, pCurrency, pPaymentMethodVID, APRESULT_COMMIT, TransLogType_MicroPurchase))
		{
			eOutcome = TRANSACTION_OUTCOME_SUCCESS;
		}
	}
	PERFINFO_AUTO_STOP();

	return eOutcome;
}

static void PurchaseProductCB(AccountKeyValueResult result, U32 uAccountID, const AccountProxyProduct *pProduct, const Money *pExpectedPrice, const char *pPaymentMethodVID, ContainerID containerID, PurchaseLogicStruct *pCBStruct)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pCBStruct->iEntityContID);
	const GameAccountData *pData = APGetGameAccount(pEnt, uAccountID);
	MicroTransactionDef *pDef = GET_REF(pCBStruct->hDef);

	pCBStruct->containerID = containerID;
	
	//If this is the game server and there isn't an entity or player, then quit
	if(entIsServer() && (!pEnt || !pEnt->pPlayer))
	{
		PurchaseLogicStruct *pNewCBStruct = StructClone(parse_PurchaseLogicStruct, pCBStruct);
		if(pNewCBStruct)
		{
			TransactionReturnVal *returnVal = objCreateManagedReturnVal(RollbackPurchase_CB, pNewCBStruct);

			APLog(NULL, pNewCBStruct->uAccountID, "RollingBackNoEnt", "AccountID %d, ProductID %d, Currency %s",
				pNewCBStruct->uAccountID,
				pNewCBStruct->pProduct->uID,
				moneyCurrency(pExpectedPrice));

			AutoTrans_AccountProxy_tr_RollbackPurchase(returnVal, objServerType(), 
				GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, containerID, 
				pNewCBStruct->uAccountID,
				pNewCBStruct->pProduct,
				moneyCurrency(pExpectedPrice),
				pNewCBStruct->pPaymentMethodVID);
		}
	}
	else if(pDef && pData)
	{
		const char *pchNotifyMesg = NULL;
		char *estrErrorMesg = NULL;
		NotifyType eNotifyType = kNotifyType_MicroTransFailed;

		estrStackCreate(&estrErrorMesg);

		switch (result)
		{
		case AKV_SUCCESS:
			{
#ifdef GAMESERVER
				MicroPurchaseErrorType eError = microtrans_GetCanPurchaseError(
					GetAppGlobalType() == GLOBALTYPE_WEBREQUESTSERVER ? WEBREQUEST_PARTITION_INDEX : entGetPartitionIdx(pEnt), 
					pEnt, pData, pDef, pCBStruct->iLangID, &estrErrorMesg);
#else
				MicroPurchaseErrorType eError = microtrans_GetCanPurchaseError(entGetPartitionIdx_NoAssert(pEnt), pEnt, pData, pDef, pCBStruct->iLangID, &estrErrorMesg);
#endif
				
				if(eError == kMicroPurchaseErrorType_None)
				{
					PurchaseLogicStruct *pNewCBStruct = StructClone(parse_PurchaseLogicStruct, pCBStruct);
					TransactionReturnVal *returnVal = NULL;
					U32* eaPets = NULL;
#ifdef GAMESERVER
					ItemChangeReason reason = {0};
#endif

					pNewCBStruct->pRewards = StructCreate(parse_MicroTransactionRewards);

					if(entIsServer())
					{
#ifdef GAMESERVER
						returnVal = LoggedTransactions_CreateManagedReturnValEnt("MicroPurchase", pEnt, FinishPurchase, pNewCBStruct);
#endif
					}
					else
					{
						returnVal = objCreateManagedReturnVal(FinishPurchase, pNewCBStruct);
					}
					devassert(returnVal != NULL);

					APLog(pEnt, pCBStruct->uAccountID, "KeyChangedsuccess", "AccountID %d, ProductID %d, Currency %s",
						pCBStruct->uAccountID,
						pCBStruct->pProduct->uID,
						moneyCurrency(pExpectedPrice));

#ifdef GAMESERVER
					MicroTrans_GenerateRewardBags(entGetPartitionIdx_NoAssert(pEnt), pEnt, pDef, pNewCBStruct->pRewards);
					inv_FillItemChangeReason(&reason, pEnt, "MicroTrans_FinishPurchase", pProduct->pInternalName);
#endif

					if (microtrans_GrantsUniqueItem(pDef, pNewCBStruct->pRewards))
					{
						ea32Create(&eaPets);
						Entity_GetPetIDList(pEnt, &eaPets);
					}

					AutoTrans_AccountProxy_tr_FinishPurchase_MicroTransactionDef(returnVal,
						objServerType(),
						GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, containerID, 
						GLOBALTYPE_ENTITYPLAYER, pEnt ? pEnt->myContainerID : 0, 
						GLOBALTYPE_ENTITYSAVEDPET, &eaPets,
						GLOBALTYPE_GAMEACCOUNTDATA, pCBStruct->uAccountID,
						pNewCBStruct->pRewards, uAccountID, pProduct, pDef, pDef->pchName,
						moneyCurrency(pExpectedPrice), pPaymentMethodVID,
#ifdef GAMESERVER
						&reason
#else
						NULL
#endif
						);

					//Set this flag so we don't notify the client
					eNotifyType = kNotifyType_MicroTransSuccess;

					ea32Destroy(&eaPets);
				}
				else
				{
					PurchaseLogicStruct *pNewCBStruct = StructClone(parse_PurchaseLogicStruct, pCBStruct);
					if(pNewCBStruct)
					{	
						TransactionReturnVal *returnVal = objCreateManagedReturnVal(RollbackPurchase_CB, pNewCBStruct);

						APLog(NULL, pNewCBStruct->uAccountID, "RollingBackItemRestricted", "AccountID %d, ProductID %d, Currency %s",
							pNewCBStruct->uAccountID,
							pNewCBStruct->pProduct->uID,
							moneyCurrency(pExpectedPrice));

						AutoTrans_AccountProxy_tr_RollbackPurchase(returnVal, objServerType(), 
							GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, containerID, 
							pNewCBStruct->uAccountID,
							pNewCBStruct->pProduct,
							moneyCurrency(pExpectedPrice),
							pNewCBStruct->pPaymentMethodVID);
					}
				}
				
			}
			xcase AKV_INVALID_KEY:
				pchNotifyMesg = "MicroTrans_InvalidKey_Mesg";
			xcase AKV_NONEXISTANT:
				pchNotifyMesg = "MicroTrans_CannotFindKey_Mesg";
			xcase AKV_INVALID_RANGE:
				pchNotifyMesg = "MicroTrans_NotEnoughCurrency_Mesg";
			xcase AKV_LOCKED:
				pchNotifyMesg = "MicroTrans_UnfinishedTransaction_Mesg";
			xcase AKV_FORBIDDEN_CHANGE:
				ADCStaleProducts();
				ADCStaleDiscounts();
				eNotifyType = kNotifyType_MicroTransFailed_PriceChanged;
				pchNotifyMesg = "MicroTrans_PriceChanged_Mesg";
			xdefault:
			case AKV_FAILURE:
				pchNotifyMesg = "MicroTrans_GenericFailure_Mesg";
		}

		if((eNotifyType == kNotifyType_MicroTransFailed || eNotifyType == kNotifyType_MicroTransFailed_PriceChanged)
			&& ((pchNotifyMesg && *pchNotifyMesg) || (estrErrorMesg && *estrErrorMesg)) )
		{
			const char *pchTranslatedMesg = estrErrorMesg;
			char *estrMesgBuffer = NULL;

			if(pchNotifyMesg && *pchNotifyMesg)
			{
				char pchCurrencyMsgKey[MAX_PATH];
				const char* pchCurrencyDisplayName;

				sprintf(pchCurrencyMsgKey, "Currency%s_Mesg", moneyCurrency(pExpectedPrice));
				pchCurrencyDisplayName = langTranslateMessageKey(pCBStruct->iLangID, pchCurrencyMsgKey);
				if (!pchCurrencyDisplayName || !pchCurrencyDisplayName[0])
					pchCurrencyDisplayName = moneyCurrency(pExpectedPrice);

				langFormatGameMessageKey(pCBStruct->iLangID, &estrMesgBuffer, pchNotifyMesg, 
					STRFMT_STRING("Currency", pchCurrencyDisplayName),
					STRFMT_END);

				pchTranslatedMesg = estrMesgBuffer;
			}

			APNotifySend(pEnt, pCBStruct->uAccountID, eNotifyType, pchTranslatedMesg, GET_REF(pCBStruct->hDef));

			APLog(pEnt, pCBStruct->uAccountID, "KeyChangedFailed", "AccountID %d, ProductID %d, Currency %s, Reason \"%s\"",
				pCBStruct->uAccountID,
				pCBStruct->pProduct->uID,
				moneyCurrency(pExpectedPrice),
				pchTranslatedMesg);

			estrDestroy(&estrMesgBuffer);
		}

		estrDestroy(&estrErrorMesg);
	}
	else
	{
		PurchaseLogicStruct *pNewCBStruct = StructClone(parse_PurchaseLogicStruct, pCBStruct);
		if(pNewCBStruct)
		{
			TransactionReturnVal *returnVal = objCreateManagedReturnVal(RollbackPurchase_CB, pNewCBStruct);

			APLog(pEnt, pNewCBStruct->uAccountID, "RollingBack - NoGAD/Def", "AccountID %d, ProductID %d, Currency %s",
				pNewCBStruct->uAccountID,
				pNewCBStruct->pProduct->uID,
				moneyCurrency(pExpectedPrice));

			AutoTrans_AccountProxy_tr_RollbackPurchase(returnVal, objServerType(), 
				GLOBALTYPE_ACCOUNTPROXYSERVER_LOCKS, containerID, 
				pNewCBStruct->uAccountID,
				pNewCBStruct->pProduct,
				moneyCurrency(pExpectedPrice),
				pNewCBStruct->pPaymentMethodVID);
		}
	}
	StructDestroy(parse_PurchaseLogicStruct, pCBStruct);
}

const char *GetAuthCapture_ErrorMsgKey(PurchaseResult eResult)
{
	switch(eResult)
	{
	case PURCHASE_RESULT_PENDING:
		return "MicroTrans_Purchase_Pending";
	
	xcase PURCHASE_RESULT_COMMIT:
		return "MicroTrans_Purchase_Success";

	xcase PURCHASE_RESULT_ROLLBACK:
		return "MicroTrans_Purchase_Canceled";
	
	xcase PURCHASE_RESULT_MISSING_ACCOUNT:
	case PURCHASE_RESULT_MISSING_PRODUCTS:
	case PURCHASE_RESULT_INVALID_PAYMENT_METHOD:
	case PURCHASE_RESULT_INVALID_PRODUCT:
	case PURCHASE_RESULT_INVALID_IP:
	case PURCHASE_RESULT_INVALID_CURRENCY:
	case PURCHASE_RESULT_INVALID_PRICE:
		return "MicroTrans_Purchase_Catastrophe";

	xcase PURCHASE_RESULT_NOT_READY:
		return "MicroTrans_Purchase_NotReady";

	xcase PURCHASE_RESULT_CANNOT_ACTIVATE:
		return "MicroTrans_Purchase_Processing";
	
	xcase PURCHASE_RESULT_INSUFFICIENT_POINTS:
		return "MicroTrans_Purchase_NotEnoughCurrency";
	
	xcase PURCHASE_RESULT_COMMIT_FAIL:
		return "MicroTrans_Purchase_CommitFailure";
	
	xcase PURCHASE_RESULT_AUTH_FAIL:
		return "MicroTrans_Purchase_AuthFailure";

	xcase PURCHASE_RESULT_SPENDING_CAP_REACHED:
		return "MicroTrans_Purchase_SpendingCap";

	xcase PURCHASE_RESULT_STEAM_DISABLED:
		return "MicroTrans_Purchase_SteamDisabled";

	xcase PURCHASE_RESULT_STEAM_UNAVAILABLE:
		return "MicroTrans_Purchase_SteamFailure";
	
	xcase PURCHASE_RESULT_ROLLBACK_FAIL:
	case PURCHASE_RESULT_RETRIEVED:
	case PURCHASE_RESULT_RETRIEVE_FAILED:
	case PURCHASE_RESULT_DELAY_FAILURE:
	default:
		return "MicroTrans_Purchase_GenericFailure";
	//case PURCHASE_RESULT_PENDING_PAYPAL:  Dunno what to do about paypal
	}
}

static void AuthCapture_CB(TransactionReturnVal *pVal, PurchaseLogicStruct *pCBStruct)
{
	NotifyType eType = kNotifyType_MicroTrans_PointBuyFailed;
	const char *pchNotifyMsg = NULL;
	char *estrMsg = NULL;
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pCBStruct->iEntityContID);
	AuthCaptureResultInfo *pResponse = NULL;

	if(RemoteCommandCheck_aslAPCmdAuthCapture(pVal, &pResponse) == TRANSACTION_OUTCOME_SUCCESS)
	{
		pchNotifyMsg = GetAuthCapture_ErrorMsgKey(pResponse->eResult);
		if(pResponse->eResult == PURCHASE_RESULT_COMMIT)
		{
			eType = kNotifyType_MicroTrans_PointBuySuccess;
		}
		else
		{
			eType = kNotifyType_MicroTrans_PointBuyFailed;
		}
		StructDestroy(parse_AuthCaptureResultInfo, pResponse);
	}
	else
	{
		eType = kNotifyType_MicroTrans_PointBuyFailed;
		pchNotifyMsg = "MicroTrans_Purchase_GenericFailure";
	}

	if(pchNotifyMsg && *pchNotifyMsg)
	{
		langFormatGameMessageKey(pCBStruct->iLangID, &estrMsg, pchNotifyMsg, STRFMT_END);
		APNotifySend(pEnt, pCBStruct->uAccountID, eType, estrMsg, NULL);
	}

	estrDestroy(&estrMsg);
	StructDestroy(parse_PurchaseLogicStruct, pCBStruct);
}

static void GetProductCB(SA_PARAM_OP_VALID const AccountProxyProduct *product, void *userData)
{
	PurchaseHolder *holder = userData;
	Entity *entity = entFromEntityRefAnyPartition(holder->entRef);
	Player *player = entity ? entGetPlayer(entity) : NULL;
	MicroTransactionDef *pDef = product ? microtrans_findDefFromAPProd(product) : NULL;
	const GameAccountData *pData = APGetGameAccount(entity, holder->iAccountID);

	if(!product || (!pDef && !holder->pPaymentMethodVID))
	{
		char *estrMesgBuffer = NULL;

		langFormatGameMessageKey(holder->iLangID, &estrMesgBuffer, "MicroTrans_ProductNotFound_Mesg", STRFMT_END);

		APNotifySend(entity, holder->iAccountID, kNotifyType_MicroTransFailed, estrMesgBuffer, NULL);

		estrDestroy(&estrMesgBuffer);
	}
	else if(pDef)
	{
		if(pDef->bDeprecated)
		{
			char *estrMesgBuffer = NULL;

			langFormatGameMessageKey(holder->iLangID, &estrMesgBuffer, "MicroTrans_PurchaseNotAllowed_Mesg", STRFMT_END);

			APNotifySend(entity, holder->iAccountID, kNotifyType_MicroTransFailed, estrMesgBuffer, NULL);

			estrDestroy(&estrMesgBuffer);
		}
		else if(pDef && IS_HANDLE_ACTIVE(pDef->hRequiredPurchase) && !microtrans_HasPurchased(pData, GET_REF(pDef->hRequiredPurchase)))
		{
			char *estrMesgBuffer = NULL;
			MicroTransactionDef *pRequiredDef = GET_REF(pDef->hRequiredPurchase);
			if(pRequiredDef)
			{
				langFormatGameMessageKey(holder->iLangID, &estrMesgBuffer, "MicroTrans_PurchaseRequired_Mesg", 
				STRFMT_DISPLAYMESSAGE("Name", pRequiredDef->displayNameMesg),
				STRFMT_END);
			}
			else
			{
				langFormatGameMessageKey(holder->iLangID, &estrMesgBuffer, "MicroTrans_PurchaseRequired_Mesg", 
					STRFMT_STRING("Name", "Untranslated - [Unknown Product]"),
					STRFMT_END);
			}

			APNotifySend(entity, holder->iAccountID, kNotifyType_MicroTransFailed, estrMesgBuffer, pDef);

			estrDestroy(&estrMesgBuffer);
		}
		else
		{
			PurchaseLogicStruct *pCBStruct = StructCreate(parse_PurchaseLogicStruct);
			pCBStruct->pExpectedPrice = StructClone(parse_Money, holder->pExpectedPrice);
			pCBStruct->pPaymentMethodVID = StructAllocString(holder->pPaymentMethodVID);
			pCBStruct->pProduct = StructClone(parse_AccountProxyProduct, product);
			pCBStruct->uAccountID = holder->iAccountID;
			pCBStruct->iEntityContID = entity ? entGetContainerID(entity) : 0;
			pCBStruct->iLangID = holder->iLangID;
			pCBStruct->uCouponItemId = holder->uCouponItemId;

			SET_HANDLE_FROM_REFERENT(g_hMicroTransDefDict, pDef, pCBStruct->hDef);

			if(pCBStruct)
			{
				APLog(entity, holder->iAccountID, "FoundProduct", "AccountID %d, ProductID %d, Currency %s",
					holder->iAccountID,
					pCBStruct->pProduct->uID,
					moneyCurrency(pCBStruct->pExpectedPrice));
				APBeginPurchase(holder->iAccountID, product, holder->pExpectedPrice, holder->uOverridePercentageDiscount, holder->pPaymentMethodVID, PurchaseProductCB, pCBStruct);
			}
		}
	}
	//Auth-capture purchase
	else
	{
		PurchaseLogicStruct *pCBStruct = StructCreate(parse_PurchaseLogicStruct);
		if(pCBStruct)
		{
			AuthCaptureRequest *pRequest = StructCreate(parse_AuthCaptureRequest);
			TransactionItem *pItem = StructCreate(parse_TransactionItem);
			TransactionReturnVal *pVal = objCreateManagedReturnVal(AuthCapture_CB, pCBStruct);
		
			pCBStruct->pExpectedPrice = StructClone(parse_Money, holder->pExpectedPrice);
			pCBStruct->pPaymentMethodVID = StructAllocString(holder->pPaymentMethodVID);
			pCBStruct->pProduct = StructClone(parse_AccountProxyProduct, product);
			pCBStruct->uAccountID = holder->iAccountID;
			pCBStruct->iEntityContID = entity ? entGetContainerID(entity) : 0;
			pCBStruct->iLangID = holder->iLangID;

			pItem->uProductID = product->uID;
			pItem->pPrice = StructClone(parse_Money, holder->pExpectedPrice);
			pItem->uOverridePercentageDiscount = holder->uOverridePercentageDiscount;

			eaPush(&pRequest->eaItems, pItem);

			pRequest->pIP = StructAllocString(holder->pIP);
			pRequest->pCurrency = StructAllocString(moneyCurrency(holder->pExpectedPrice));
			pRequest->pPaymentMethodID = StructAllocString(holder->pPaymentMethodVID);
			pRequest->uAccountID = holder->iAccountID;

			RemoteCommand_aslAPCmdAuthCapture(pVal, GLOBALTYPE_ACCOUNTPROXYSERVER, 0, pRequest);

			StructDestroy(parse_AuthCaptureRequest, pRequest);
		}
	}

	StructDestroy(parse_PurchaseHolder, holder);
}

void APPurchaseProduct(Entity *pEntity, U32 iAccountID, int iLangID, const char *pCategory, U32 uProductID, const Money *pExpectedPrice, U32 uOverrideDiscount, const char *pPaymentMethodVID, const char *pClientIP, U64 uItemId)
{
	PurchaseHolder *holder = StructCreate(parse_PurchaseHolder);
	if(pEntity)
	{
		holder->entRef = entGetRef(pEntity);
		holder->iAccountID = iAccountID;
	}
	else if(iAccountID)
	{
		holder->iAccountID = iAccountID;
	}
	else
	{
		StructDestroy(parse_PurchaseHolder, holder);
		return;
	}

	holder->iLangID = iLangID;
	holder->pExpectedPrice = StructClone(parse_Money, pExpectedPrice);
	holder->uOverridePercentageDiscount = uOverrideDiscount;
	holder->uCouponItemId = uItemId;

	if(pPaymentMethodVID && strlen(pPaymentMethodVID))
	{
		estrCopy2(&holder->pPaymentMethodVID, pPaymentMethodVID);
		estrCopy2(&holder->pIP, pClientIP);
	}
	else
	{
		char *pShardCurrency = NULL;
		estrPrintf(&pShardCurrency, "_%s", microtrans_GetShardCurrency());

		//Check to see if the user sent an invalid currency for the purchase
		if(pExpectedPrice && moneyCurrency(pExpectedPrice) && stricmp(pShardCurrency, moneyCurrency(pExpectedPrice)))
		{
			StructDestroy(parse_PurchaseHolder, holder);
			return;
		}
	}

	APLog(pEntity, iAccountID ? iAccountID : pEntity->pPlayer->accountID, "FindProduct", "AccountID %d, Category \"%s\", ProductID %d, Currency %s",
		iAccountID ? iAccountID : pEntity->pPlayer->accountID,
		pCategory,
		uProductID,
		moneyCurrency(pExpectedPrice));

	APGetProductByID(pCategory, uProductID, GetProductCB, holder);
}


AUTO_TRANSACTION
ATR_LOCKS(pAccountData, ".Eaaccountkeyvalues[]");
enumTransactionOutcome AP_tr_UpdateGADCache(ATR_ARGS, NOCONST(GameAccountData) *pAccountData, const char *pKey, const char *pValue)
{
	NOCONST(AccountProxyKeyValueInfoContainer) *pInfo = eaIndexedGetUsingString(&pAccountData->eaAccountKeyValues, pKey);

	if (nullStr(pValue))
	{
		// If the value is blank, we're removing it
		if (pInfo)
		{
			eaIndexedRemoveUsingString(&pAccountData->eaAccountKeyValues, pKey);
			StructDestroyNoConst(parse_AccountProxyKeyValueInfoContainer, pInfo);
		}
	}
	else
	{
		// Otherwise we're adding or updating it
		if (pInfo)
		{
			estrCopy2(&pInfo->pValue, pValue);
		}
		else
		{
			pInfo = StructCreateNoConst(parse_AccountProxyKeyValueInfoContainer);
			estrCopy2(&pInfo->pKey, pKey);
			estrCopy2(&pInfo->pValue, pValue);
			eaIndexedPushUsingStringIfPossible(&pAccountData->eaAccountKeyValues, pKey, pInfo);
		}
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

bool APAppendParsedAVPsFromKeyAndValue(const char *pKey, const char *pValue, ParsedAVP ***pppPairs)
{
	char *pchItem = estrStackCreateFromStr(pKey);
	char *pchGameTitle = NULL;
	MicroItemType eType = kMicroItemType_None;
	char *pchItemName = NULL;
	ParsedAVP *pPair = NULL;
	bool bAppendedAny = false;

	S32 iCount = MicroTrans_TokenizeItemID(pchItem, &pchGameTitle, &eType, &pchItemName);
	if (!iCount)
	{
		goto end;
	}

	if (!pchGameTitle || !pchItemName || stricmp(GetShortProductName(), pchGameTitle))
	{
		goto end;
	}

	switch (eType)
	{
	default:
		break;
	case kMicroItemType_PlayerCostume:
	case kMicroItemType_Species:
		// Unlock a single species or costume.
		pPair = StructCreate(parse_ParsedAVP);

		pPair->pchAttribute = StructAllocString(pKey);
		pPair->pchValue = StructAllocString(pValue);
		pPair->eType = eType;
		pPair->iCount = iCount;
		pPair->pchGameTitle = StructAllocString(pchGameTitle);
		pPair->pchItemIdent = StructAllocString(pchItemName);

		eaPush(pppPairs, pPair);
		bAppendedAny = true;
		break;
	case kMicroItemType_Costume:
		{
			// Unlock costumes from an item, which can reference multiple costumes.
			ItemDef *pDef = item_DefFromName(pchItemName);

			if (pDef)
			{
				EARRAY_FOREACH_BEGIN(pDef->ppCostumes, iCostume);
				{
					char *pchCostumeRef = NULL;
					MicroTrans_FormItemEstr(&pchCostumeRef, GetShortProductName(), kMicroItemType_PlayerCostume, REF_STRING_FROM_HANDLE(pDef->ppCostumes[iCostume]->hCostumeRef), 1);

					pPair = StructCreate(parse_ParsedAVP);

					pPair->pchAttribute = StructAllocString(pchCostumeRef);
					pPair->pchValue = StructAllocString("1");
					pPair->eType = kMicroItemType_PlayerCostume;
					pPair->iCount = 1;
					pPair->pchGameTitle = StructAllocString(pchGameTitle);
					pPair->pchItemIdent = StructAllocString(REF_STRING_FROM_HANDLE(pDef->ppCostumes[iCostume]->hCostumeRef));

					eaPush(pppPairs, pPair);
					bAppendedAny = true;

					estrDestroy(&pchCostumeRef);
				}
				EARRAY_FOREACH_END;
			}
		}
		break;
	}

end:
	estrDestroy(&pchItem);
	return bAppendedAny;
}

void APUpdateGADCache(GlobalType eType, U32 uAccountID, const char *pKey, const char *pValue, TransactionReturnCallback pCallback, void *pUserdata)
{
	TransactionReturnVal *pReturnVal = NULL;

	if (pCallback)
		pReturnVal = objCreateManagedReturnVal(pCallback, pUserdata);

	AutoTrans_AP_tr_UpdateGADCache(pReturnVal, eType, GLOBALTYPE_GAMEACCOUNTDATA, uAccountID, pKey, pValue);
}

void APClientCacheRemoveKeyValue(GlobalType eType, ContainerID uContainerID, U32 uAccountID, const char *pKey)
{
	if(!uAccountID)
		return;

	switch(eType)
	{
	case GLOBALTYPE_ENTITYPLAYER:
		{
			Entity *pEntity = entFromContainerIDAnyPartition(eType, uContainerID);
			if (pEntity)
			{
				Player *pPlayer = entGetPlayer(pEntity);
				if(pPlayer)
				{
					int i = eaIndexedFindUsingString(&pPlayer->ppKeyValueCache, pKey);
					if(i >= 0)
					{
						AccountProxyKeyValueInfo *pInfo = pPlayer->ppKeyValueCache[i];
						eaRemove(&pPlayer->ppKeyValueCache, i);
						StructDestroy(parse_AccountProxyKeyValueInfo, pInfo);
					}
					entity_SetDirtyBit(pEntity, parse_Player, pPlayer, false);
				}
#ifdef GAMESERVER
				ClientCmd_gclAPCmdCacheRemoveKeyValue(pEntity, pKey);
#endif
			}
		}
		break;
	case GLOBALTYPE_LOGINSERVER:
		{
#ifdef APPSERVER
			APCacheRemoveKey(uAccountID, pKey);
#endif
		}
		break;
	}
}

void APClientCacheSetKeyValue(GlobalType eType, ContainerID uContainerID, U32 uAccountID, AccountProxyKeyValueInfo *pInfo)
{
	if(!uAccountID || !pInfo)
		return;

	switch(eType)
	{
	case GLOBALTYPE_ENTITYPLAYER:
		{
			Entity *pEntity = entFromContainerIDAnyPartition(eType, uContainerID);
			if (pEntity)
			{
				Player *pPlayer = entGetPlayer(pEntity);
				if(pPlayer)
				{
					int i = eaIndexedFindUsingString(&pPlayer->ppKeyValueCache, pInfo->pKey);
					if(i >= 0)
					{
						AccountProxyKeyValueInfo *pInfoToDest = pPlayer->ppKeyValueCache[i];
						StructCopyAll(parse_AccountProxyKeyValueInfo, pInfo, pInfoToDest);
					}
					else
					{
						eaPush(&pPlayer->ppKeyValueCache, StructClone(parse_AccountProxyKeyValueInfo, pInfo));
					}
					eaIndexedEnable(&pPlayer->ppKeyValueCache, parse_AccountProxyKeyValueInfo);
					entity_SetDirtyBit(pEntity, parse_Player, pPlayer, false);
				}
#ifdef GAMESERVER
				ClientCmd_gclAPCmdCacheSetKeyValue(pEntity, pInfo);
#endif
			}
		}
		break;
	case GLOBALTYPE_LOGINSERVER:
		{
#ifdef APPSERVER
			APCacheSetKey(uAccountID, pInfo);
#endif
		}
		break;
	}
}

void APClientCacheSetAllKeyValues(GlobalType eType, ContainerID uContainerID, U32 uAccountID, AccountProxyKeyValueInfoList *pInfoList)
{
	if(!uAccountID)
		return;

	switch(eType)
	{
	case GLOBALTYPE_ENTITYPLAYER:
		{
			Entity *pEntity = entFromContainerIDAnyPartition(eType, uContainerID);
			if (pEntity)
			{
				Player *pPlayer = entGetPlayer(pEntity);
				if(pPlayer)
				{
					eaCopyStructs(&pInfoList->ppList, &pPlayer->ppKeyValueCache, parse_AccountProxyKeyValueInfo);
					eaIndexedEnable(&pPlayer->ppKeyValueCache, parse_AccountProxyKeyValueInfo);
					entity_SetDirtyBit(pEntity, parse_Player, pPlayer, false);
				}
#ifdef GAMESERVER
				ClientCmd_gclAPCmdCacheSetAllKeyValues(pEntity, pInfoList);
#endif
			}
		}
		break;
	case GLOBALTYPE_LOGINSERVER:
		{
#ifdef APPSERVER
			APCacheSetAllKeys(uAccountID, pInfoList);
#endif
		}
		break;
	}
}

typedef struct PaymentCBHolder
{
	EntityRef eRef;
	U32 uAccountID;
	RequestPaymentMethodsCallback pCB;
} PaymentCBHolder;

static void PaymentMethods_CB(TransactionReturnVal *pVal, PaymentCBHolder *pHolder)
{
	PaymentMethodsResponse *pResponse = NULL;
	
	devassert(pHolder!=NULL);
	
	if(RemoteCommandCheck_aslAPCmdRequestPaymentMethods(pVal, &pResponse) == TRANSACTION_OUTCOME_SUCCESS)
	{
		if(pResponse)
		{
			int i;
			for(i=eaSize(&pResponse->eaPaymentMethods)-1; i>=0; i--)
			{
				NOCONST(CachedPaymentMethod) *pMethod = CONTAINER_NOCONST(CachedPaymentMethod, pResponse->eaPaymentMethods[i]);
				if(pMethod->methodProvider != TPROVIDER_Steam)
				{
					eaRemove(&pResponse->eaPaymentMethods, i);
					StructDestroyNoConst(parse_CachedPaymentMethod, pMethod);
				}
			}
		}
		(*pHolder->pCB)(pHolder->eRef, pHolder->uAccountID, pResponse);
	}
	else
	{
		(*pHolder->pCB)(pHolder->eRef, pHolder->uAccountID, NULL);
	}

	free(pHolder);
	StructDestroy(parse_PaymentMethodsResponse, pResponse);
}

void APRequestPaymentMethods(Entity *pEntity, U32 uAccountID, U64 uSteamID, RequestPaymentMethodsCallback pCallback)
{
	if(!microtrans_PointBuyingEnabled())
	{
		return;
	}
	else
	{
		PaymentCBHolder *pHolder = calloc(1, sizeof(PaymentCBHolder));
		pHolder->pCB = pCallback;
		if(pEntity)
		{
			if(pEntity->pPlayer->accountID)
			{
				pHolder->eRef = entGetRef(pEntity);
				pHolder->uAccountID = pEntity->pPlayer->accountID;
			}
			else
			{
				free(pHolder);
				return;
			}
		}
		else if(uAccountID)
		{
			pHolder->uAccountID = uAccountID;
		}
		else
		{
			free(pHolder);
			return;
		}

		{
			TransactionReturnVal *pVal = objCreateManagedReturnVal(PaymentMethods_CB, pHolder);
			RemoteCommand_aslAPCmdRequestPaymentMethods(pVal, GLOBALTYPE_ACCOUNTPROXYSERVER, 0, pHolder->uAccountID, uSteamID);
		}
	}
}

#include "AccountProxyCommon_c_ast.c"