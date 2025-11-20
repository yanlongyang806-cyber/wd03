#include "Steam.h"

#include "AccountLog.h"
#include "AccountManagement.h"
#include "Alerts.h"
#include "Money.h"
#include "StringUtil.h"
#include "TimedCallback.h"
#include "billing.h"
#include "crypt.h"
#include "error.h"
#include "httpAsync.h"
#include "httputil.h"
#include "logging.h"
#include "timing.h"
#include "url.h"

#include "Steam_c_ast.h"

// Max seconds to wait between retries querying a Steam transaction (default=240)
U32 guSteamRequestMaxBackoff = MINUTES(4);
AUTO_CMD_INT(guSteamRequestMaxBackoff, SteamRequestMaxBackoff) ACMD_CMDLINE ACMD_CATEGORY(Account_Server_Billing);

AUTO_STRUCT;
typedef struct SteamError
{
	U32 errorcode;
	char *errordesc;
} SteamError;

AUTO_STRUCT;
typedef struct SteamGetUserInfoParams
{
	char *state;
	char *country;
	char *currency;
	char *status;
} SteamGetUserInfoParams;

AUTO_STRUCT;
typedef struct SteamGetUserInfoResponse
{
	char *result;
	SteamGetUserInfoParams *params;
	SteamError *error;
} SteamGetUserInfoResponse;

AUTO_STRUCT;
typedef struct SteamGetUserInfoResult
{
	SteamGetUserInfoResponse *response;
} SteamGetUserInfoResult;

AUTO_STRUCT;
typedef struct SteamTxnParams
{
	U64 orderid;
	U64 transid;
	char *status;
	char *steamurl;
} SteamTxnParams;

AUTO_STRUCT;
typedef struct SteamTxnResponse
{
	char *result;
	SteamTxnParams *params;
	SteamError *error;
} SteamTxnResponse;

AUTO_STRUCT;
typedef struct SteamTxnResult
{
	SteamTxnResponse *response;
} SteamTxnResult;

typedef struct SteamTransactionSource
{
	U32 uAccountID;
	U64 uSteamID;
	U64 uOrderID;
	char *pProxy;
} SteamTransactionSource;

typedef struct SteamGetUserInfoWrapper
{
	SteamTransactionSource source;
	U32 uRequestID;
	SteamGetUserInfoCB pCallback;
	void *pUserData;
} SteamGetUserInfoWrapper;

typedef struct SteamTransactionWrapper
{
	SteamTransactionSource source;
	U32 uRequestID;
	SteamTransactionCB pCallback;
	void *pUserData;

	// Retry info for QueryTxn/FinalizeTxn stuff
	U32 uNextAttemptTime;
	U32 uNextBackoffAmount;
} SteamTransactionWrapper;

typedef struct SteamInitTransactionWrapper
{
	SteamTransactionSource source;
	U32 uRequestID;
	SteamInitTransactionCB pCallback;
	void *pUserData;
} SteamInitTransactionWrapper;

typedef struct SteamRefundTransactionWrapper
{
	SteamTransactionSource source;
	U32 uRequestID;
	SteamRefundTransactionCB pCallback;
	void *pUserData;
} SteamRefundTransactionWrapper;

#define SteamAlertMalformedResponse(response, request) CRITICAL_NETOPS_ALERT("STEAM_WALLET_MALFORMED", "Steam %s request returned a malformed response:\n\n%s", request, response)
#define SteamAlertTimeoutResponse(request) CRITICAL_NETOPS_ALERT("STEAM_WALLET_TIMEOUT", "Steam %s request timed out.", request)

// Logging macros for generic Steam API requests (i.e. no order ID)
#define SteamLogRequest(request, steamid, accountid, proxy, ...) SERVLOG_PAIRS(LOG_ACCOUNT_SERVER_STEAM, "Steam" request "Request", \
	("steamid", "%"FORM_LL"u", steamid) \
	("accountid", "%u", accountid) \
	("proxy", "%s", proxy) \
	__VA_ARGS__)
#define SteamLogResponse(request, response_type, wrapper, ...) SERVLOG_PAIRS(LOG_ACCOUNT_SERVER_STEAM, "Steam" request "Response" response_type, \
	("steamid", "%"FORM_LL"u", wrapper->source.uSteamID) \
	("accountid", "%u", wrapper->source.uAccountID) \
	("proxy", "%s", wrapper->source.pProxy) \
	("requestid", "%u", wrapper->uRequestID) \
	__VA_ARGS__)
#define SteamLogResponseHTTPError(request, wrapper, code, response, ...) SteamLogResponse(request, "HTTPError", wrapper, \
	("code", "%d", code) ("response", "%s", response) __VA_ARGS__)
#define SteamLogResponseMalformed(request, wrapper, ...) SteamLogResponse(request, "Malformed", wrapper, __VA_ARGS__)
#define SteamLogResponseError(request, wrapper, code, error, ...) SteamLogResponse(request, "Error", wrapper, \
	("errorcode", "%u", code) ("errordesc", "%s", error) __VA_ARGS__)
#define SteamLogResponseSuccess(request, wrapper, ...) SteamLogResponse(request, "Success", wrapper, __VA_ARGS__)
#define SteamLogResponseTimeout(request, wrapper, ...) SteamLogResponse(request, "Timeout", wrapper, __VA_ARGS__)

// Logging macros for Steam MicroTxn API requests (i.e. has order ID)
#define SteamLogTxnRequest(request, orderid, steamid, accountid, proxy, ...) SERVLOG_PAIRS(LOG_ACCOUNT_SERVER_STEAM, "Steam" request "Request", \
	("orderid", "%"FORM_LL"u", orderid) \
	("steamid", "%"FORM_LL"u", steamid) \
	("accountid", "%u", accountid) \
	("proxy", "%s", proxy) \
	__VA_ARGS__)
#define SteamLogTxnResponse(request, response_type, wrapper, ...) SERVLOG_PAIRS(LOG_ACCOUNT_SERVER_STEAM, "Steam" request "Response" response_type, \
	("orderid", "%"FORM_LL"u", wrapper->source.uOrderID) \
	("steamid", "%"FORM_LL"u", wrapper->source.uSteamID) \
	("accountid", "%u", wrapper->source.uAccountID) \
	("proxy", "%s", wrapper->source.pProxy) \
	("requestid", "%u", wrapper->uRequestID) \
	__VA_ARGS__)
#define SteamLogTxnResponseHTTPError(request, wrapper, code, response, ...) SteamLogTxnResponse(request, "HTTPError", wrapper, \
	("code", "%d", code) ("response", "%s", response) __VA_ARGS__)
#define SteamLogTxnResponseMalformed(request, wrapper, ...) SteamLogTxnResponse(request, "Malformed", wrapper, __VA_ARGS__)
#define SteamLogTxnResponseError(request, wrapper, code, error, ...) SteamLogTxnResponse(request, "Error", wrapper, \
	("errorcode", "%u", code) ("errordesc", "%s", error) __VA_ARGS__)
#define SteamLogTxnResponseSuccess(request, wrapper, ...) SteamLogTxnResponse(request, "Success", wrapper, __VA_ARGS__)
#define SteamLogTxnResponseTimeout(request, wrapper, ...) SteamLogTxnResponse(request, "Timeout", wrapper, __VA_ARGS__)

static U32 * geaSteamSuccesses = NULL;
static U32 * geaSteamFailures = NULL;

static void RemoveOldOutcomes(U32 uNow, SA_PRE_NN_NN_VALID U32 ** eaOutcomeList)
{
	U32 uLifetime = billingGetSteamFailPeriod();

	EARRAY_INT_FOREACH_REVERSE_BEGIN((*eaOutcomeList), iCurOutcome);
	{
		U32 uTime = (*eaOutcomeList)[iCurOutcome];
		if (uTime + uLifetime < uNow)
		{
			eaiRemoveFast(eaOutcomeList, iCurOutcome);
		}
	}
	EARRAY_FOREACH_END;
}

static void ReportSteamOutcome(bool bSuccess,
	SA_PARAM_NN_STR const char * pResponse, int iCode, SA_PARAM_NN_VALID const char * pRequest)
{
	static U32 uCooldown = 0;

	U32 uNow = timeSecondsSince2000();
	float fSuccessCount, fRate, fFailCount;

	if (bSuccess)
	{
		eaiPush(&geaSteamSuccesses, uNow);
	}
	else
	{
		eaiPush(&geaSteamSuccesses, uNow);
	}

	RemoveOldOutcomes(uNow, &geaSteamSuccesses);
	RemoveOldOutcomes(uNow, &geaSteamFailures);

	fSuccessCount = eaiSize(&geaSteamSuccesses);
	fFailCount = eaiSize(&geaSteamFailures);
	fRate = fSuccessCount / (fSuccessCount + fFailCount);

	if (fRate < billingGetSteamFailThreshold() &&
	    (fFailCount + fSuccessCount) > billingGetSteamFailMinimumCount() && uNow > uCooldown)
	{
		CRITICAL_NETOPS_ALERT("STEAM_WALLET_FAIL_RATIO",
			"Over the past %lu Steam operations, %3.2f%% of them have failed.",
			eaiSize(&geaSteamSuccesses) + eaiSize(&geaSteamFailures), 100.0f-fRate*100.0f);

		uCooldown = uNow + MINUTES(5);
	}

	SERVLOG_PAIRS(LOG_ACCOUNT_SERVER_STEAM, "SteamOperation",
		("success", "%i", bSuccess) ("responseCode", "%i", iCode) ("operation", "%s", pRequest));
}

static U64 GenerateSteamOrderID(U32 uAccountID)
{
	static U16 uPurchaseOrderSequence = 0;
	U64 uOrderID = 0;

	// Order ID is a concatenation of four 16-bit identifiers:
	// Sequence # | Clock time | Account ID | Random value
	++uPurchaseOrderSequence;
	uOrderID += ((U64)uPurchaseOrderSequence << 48);
	uOrderID += ((U64)(timeGetTime() % U16_MAX) << 32);
	uOrderID += ((U64)(uAccountID % U16_MAX) << 16);
	uOrderID += (U64)(cryptSecureRand() % U16_MAX);

	return uOrderID;
}

SA_RET_OP_STR static char *GetURLForSteamAPI(SA_PARAM_NN_STR const char *pAPIName, bool bLive)
{
	const char *pSteamURL = billingGetSteamURL(bLive);
	const char *pSteamVersion = billingGetSteamVersion();
	char *pURL = NULL;
	
	if(!pSteamURL || !pSteamVersion)
	{
		return NULL;
	}

	estrPrintf(&pURL, "%s/%s/%s/", pSteamURL, pAPIName, pSteamVersion);
	return pURL;
}

static PurchaseResult GetPurchaseResultFromSteamError(SA_PARAM_NN_VALID const SteamError *pError)
{
	switch (pError->errorcode)
	{
	case 4: // Internal error
		return PURCHASE_RESULT_STEAM_UNAVAILABLE;
	case 5: // User has not approved transaction
	case 10: // Transaction has been denied by user
		return PURCHASE_RESULT_AUTH_FAIL;
	case 8: // Currency does not match user's Steam Account currency
		return PURCHASE_RESULT_INVALID_CURRENCY;
	case 100: // Insufficient funds
		return PURCHASE_RESULT_INSUFFICIENT_POINTS;
	case 101: // Time limit for finalization has been exceeded
		return PURCHASE_RESULT_COMMIT_FAIL;
	case 2: // The operation failed
	case 3: // Invalid parameter
	case 6: // Transaction has already been completed
	case 7: // User is not logged in
	case 9: // Account does not exist or is temporarily unavailable
	case 11: // Transaction has been denied because user is in a restricted country
	case 102: // Account is disabled
	case 103: // Account is not allowed to purchase
	case 104: // Transaction disallowed due to fraud detection
	default:
		return PURCHASE_RESULT_UNKNOWN_ERROR;
	}
}

static void PopulateArgumentListFromConfig(SA_PARAM_NN_VALID UrlArgumentList *pList, HttpMethod eMethod, SA_PARAM_NN_VALID const SteamGameConfig *pConfig)
{
	urlAddValue(pList, "key", pConfig->apiKey, eMethod);
	urlAddValuef(pList, "appid", eMethod, "%u", pConfig->appID);
}

SA_RET_OP_VALID static UrlArgumentList *CreateSteamAPIRequest(SA_PARAM_NN_VALID const char *pProxy, SA_PARAM_NN_VALID const char *pAPIName, U64 uOrderID, HttpMethod eMethod)
{
	const ShardConfig *pShardConfig = billingGetShardConfig(pProxy);
	const SteamGameConfig *pSteamConfig = NULL;
	char *pURL = NULL;
	UrlArgumentList *pList = NULL;

	PERFINFO_AUTO_START_FUNC();

	if (!billingIsEnabled())
	{
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	if(!pShardConfig || !pShardConfig->product)
	{
		WARNING_NETOPS_ALERT("ACCOUNT_SERVER_NO_SHARD_CONFIG", "A request was generated by %s, but the Account Server doesn't have a billing configuration for that shard!", pProxy);
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	pSteamConfig = billingGetSteamGameConfig(pShardConfig->product);

	if(!pSteamConfig || !pSteamConfig->apiKey || !pSteamConfig->appID)
	{
		WARNING_NETOPS_ALERT("ACCOUNT_SERVER_NO_STEAM_CONFIG", "A Steam request was generated by %s (%s), but the Account Server doesn't have a matching Steam billing configuration!", pProxy, pShardConfig->product);
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	// Create the URL argument list for the request
	pURL = GetURLForSteamAPI(pAPIName, pShardConfig->useSteamLive);

	if(!pURL)
	{
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	pList = urlToUrlArgumentList(pURL);
	estrDestroy(&pURL);
	PopulateArgumentListFromConfig(pList, eMethod, pSteamConfig);

	if (uOrderID)
	{
		urlAddValuef(pList, "orderid", eMethod, "%"FORM_LL"u", uOrderID);
	}

	PERFINFO_AUTO_STOP();
	return pList;
}

static void DestroySteamTransactionSource(SA_PARAM_NN_VALID SteamTransactionSource *pSource)
{
	if (pSource->pProxy) free(pSource->pProxy);
}

static void GetSteamUserInfo_CB(SA_PARAM_NN_STR const char *response, int len, int response_code, SA_PRE_NN_VALID SA_POST_P_FREE SteamGetUserInfoWrapper *pWrapper)
{
	bool bSuccess = false;
	const char *pCountry = NULL;
	const char *pState = NULL;
	const char *pCurrency = NULL;
	const char *pStatus = NULL;
	
	SteamGetUserInfoResult *pResult = NULL;
	char *pError = NULL;

	PERFINFO_AUTO_START_FUNC();

	if (response_code != HTTP_OK)
	{
		ReportSteamOutcome(false, response, response_code, STEAM_GETUSERINFO);
		SteamLogResponseHTTPError(STEAM_GETUSERINFO, pWrapper, response_code, response);
	}
	else
	{
		pResult = ParserReadJSON(response, parse_SteamGetUserInfoResult, &pError);

		if (pError || !pResult || !pResult->response)
		{
			ReportSteamOutcome(false, response, response_code, STEAM_GETUSERINFO);
			SteamLogResponseMalformed(STEAM_GETUSERINFO, pWrapper);
			SteamAlertMalformedResponse(response, STEAM_GETUSERINFO);
		}
		else if (pResult->response->error)
		{
			AccountInfo *pAccount = findAccountByID(pWrapper->source.uAccountID);
			ReportSteamOutcome(false, response, response_code, STEAM_GETUSERINFO);
			SteamLogResponseError(STEAM_GETUSERINFO, pWrapper, pResult->response->error->errorcode, pResult->response->error->errordesc);

			if (pAccount)
			{
				accountLog(pAccount, "Error during Steam GetUserInfo (%d: \"%s\")", pResult->response->error->errorcode, pResult->response->error->errordesc);
			}
		}
		else if (!pResult->response->params)
		{
			ReportSteamOutcome(false, response, response_code, STEAM_GETUSERINFO);
			SteamLogResponseMalformed(STEAM_GETUSERINFO, pWrapper);
			SteamAlertMalformedResponse(response, STEAM_GETUSERINFO);
		}
		else
		{
			ReportSteamOutcome(true, response, response_code, STEAM_GETUSERINFO);
			bSuccess = true;
			pCountry = pResult->response->params->country;
			pState = pResult->response->params->state;
			pCurrency = pResult->response->params->currency;
			pStatus = pResult->response->params->status;
			SteamLogResponseSuccess(STEAM_GETUSERINFO, pWrapper,
				("country", "%s", pCountry) ("state", "%s", pState) ("currency", "%s", pCurrency) ("status", "%s", pStatus));
		}

		estrDestroy(&pError);
	}

	pWrapper->pCallback(bSuccess, pCountry, pState, pCurrency, pStatus, pWrapper->pUserData);
	DestroySteamTransactionSource(&pWrapper->source);
	free(pWrapper);
	StructDestroySafe(parse_SteamGetUserInfoResult, &pResult);
	PERFINFO_AUTO_STOP();
}

static void GetSteamUserInfo_TimeoutCB(SA_PRE_NN_VALID SA_POST_P_FREE SteamGetUserInfoWrapper *pWrapper)
{
	SteamLogResponseTimeout(STEAM_GETUSERINFO, pWrapper);
	pWrapper->pCallback(false, NULL, NULL, NULL, NULL, pWrapper->pUserData);
	DestroySteamTransactionSource(&pWrapper->source);
	free(pWrapper);
}

void GetSteamUserInfo(U32 uAccountID,
	U64 uSteamID,
	SA_PARAM_NN_STR const char *pProxy,
	SA_PARAM_OP_STR const char *pIP,
	SA_PARAM_NN_VALID SteamGetUserInfoCB pCallback,
	SA_PARAM_OP_VALID void *pUserData)
{
	UrlArgumentList *pList = NULL;
	SteamGetUserInfoWrapper *pWrapper = NULL;
	U32 uRequestID = 0;

	PERFINFO_AUTO_START_FUNC();
	pList = CreateSteamAPIRequest(pProxy, STEAM_GETUSERINFO, 0, HTTPMETHOD_GET);

	if (!pList)
	{
		pCallback(false, NULL, NULL, NULL, NULL, pUserData);
		PERFINFO_AUTO_STOP();
		return;
	}

	urlAddValuef(pList, "steamid", HTTPMETHOD_GET, "%"FORM_LL"u", uSteamID);

	if (pIP)
	{
		urlAddValue(pList, "ipaddress", pIP, HTTPMETHOD_GET);
	}

	pWrapper = callocStruct(SteamGetUserInfoWrapper);
	pWrapper->source.uAccountID = uAccountID;
	pWrapper->source.uSteamID = uSteamID;
	pWrapper->source.pProxy = strdup(pProxy);

	pWrapper->pCallback = pCallback;
	pWrapper->pUserData = pUserData;

	SteamLogRequest(STEAM_GETUSERINFO, uSteamID, uAccountID, pProxy, ("ip", "%s", pIP ? pIP : "unknown"));
	uRequestID = haSecureRequestLogged(NULL, billingGetStunnelHost(), billingGetSteamPort(), &pList, GetSteamUserInfo_CB, GetSteamUserInfo_TimeoutCB, guSteamRequestMaxBackoff, pWrapper);
	if (uRequestID)
		pWrapper->uRequestID = uRequestID;
	PERFINFO_AUTO_STOP();
}

static void CompleteInitSteamTransaction(SA_PRE_NN_VALID SA_POST_P_FREE SteamInitTransactionWrapper *pWrapper, PurchaseResult eResult, SA_PARAM_OP_STR const char *pRedirectURL, U64 uTransactionID)
{
	PERFINFO_AUTO_START_FUNC();
	pWrapper->pCallback(eResult, pWrapper->pUserData, pRedirectURL, pWrapper->source.uOrderID, uTransactionID);
	DestroySteamTransactionSource(&pWrapper->source);
	free(pWrapper);
	PERFINFO_AUTO_STOP();
}

static void InitSteamTransaction_CB(SA_PARAM_NN_STR const char *response, int len, int response_code, SA_PRE_NN_VALID SA_POST_P_FREE SteamInitTransactionWrapper *pWrapper)
{
	PurchaseResult eResult = PURCHASE_RESULT_PENDING;
	U64 uOrderID = 0;
	U64 uTransactionID = 0;
	char *pRedirectURL = NULL;

	PERFINFO_AUTO_START_FUNC();

	if (response_code != HTTP_OK)
	{
		ReportSteamOutcome(false, response, response_code, STEAM_INITTXN);
		SteamLogTxnResponseHTTPError(STEAM_INITTXN, pWrapper, response_code, response);
		eResult = PURCHASE_RESULT_STEAM_UNAVAILABLE;
	}
	else
	{
		char *pError = NULL;
		SteamTxnResult *pResult = ParserReadJSON(response, parse_SteamTxnResult, &pError);

		if (pError || !pResult || !pResult->response)
		{
			ReportSteamOutcome(false, response, response_code, STEAM_INITTXN);
			SteamLogTxnResponseMalformed(STEAM_INITTXN, pWrapper);
			SteamAlertMalformedResponse(response, STEAM_INITTXN);
			eResult = PURCHASE_RESULT_UNKNOWN_ERROR;
		}
		else if (pResult->response->error)
		{
			AccountInfo *pAccount = findAccountByID(pWrapper->source.uAccountID);
			ReportSteamOutcome(false, response, response_code, STEAM_INITTXN);
			SteamLogTxnResponseError(STEAM_INITTXN, pWrapper, pResult->response->error->errorcode, pResult->response->error->errordesc);

			if (devassert(pAccount))
			{
				accountLog(pAccount, "Error during Steam InitTxn (%d: \"%s\")", pResult->response->error->errorcode, pResult->response->error->errordesc);
			}

			eResult = GetPurchaseResultFromSteamError(pResult->response->error);
		}
		else if (!pResult->response->params)
		{
			ReportSteamOutcome(false, response, response_code, STEAM_INITTXN);
			SteamLogTxnResponseMalformed(STEAM_INITTXN, pWrapper);
			SteamAlertMalformedResponse(response, STEAM_INITTXN);
			eResult = PURCHASE_RESULT_UNKNOWN_ERROR;
		}
		else
		{
			ReportSteamOutcome(true, response, response_code, STEAM_INITTXN);
			pRedirectURL = pResult->response->params->steamurl ? strdup(pResult->response->params->steamurl) : NULL;
			uOrderID = pResult->response->params->orderid;
			uTransactionID = pResult->response->params->transid;
			SteamLogTxnResponseSuccess(STEAM_INITTXN, pWrapper,
				("transid", "%"FORM_LL"u", uTransactionID) ("redirect", "%s", pRedirectURL ? pRedirectURL : "NONE"));
		}

		estrDestroy(&pError);
		StructDestroySafe(parse_SteamTxnResult, &pResult);
	}

	pWrapper->source.uOrderID = uOrderID;
	CompleteInitSteamTransaction(pWrapper, eResult, pRedirectURL, uTransactionID);
	if (pRedirectURL) free(pRedirectURL);
	PERFINFO_AUTO_STOP();
}

static void InitSteamTransaction_TimeoutCB(SA_PRE_NN_VALID SA_POST_P_FREE SteamInitTransactionWrapper *pWrapper)
{
	PERFINFO_AUTO_START_FUNC();
	SteamAlertTimeoutResponse(STEAM_INITTXN);
	SteamLogTxnResponseTimeout(STEAM_INITTXN, pWrapper);
	CompleteInitSteamTransaction(pWrapper, PURCHASE_RESULT_STEAM_UNAVAILABLE, NULL, 0);
	PERFINFO_AUTO_STOP();
}

PurchaseResult InitSteamTransaction(U32 uAccountID,
	U64 uSteamID,
	SA_PARAM_NN_STR const char *pProxy,
	SA_PARAM_OP_STR const char *pCategory,
	SA_PARAM_NN_STR const char *pCurrency,
	SA_PARAM_NN_STR const char *pLocCode,
	SA_PARAM_NN_VALID EARRAY_OF(const TransactionItem) eaItems,
	SA_PARAM_NN_STR const char *pIP,
	bool bWebPurchase,
	SA_PARAM_NN_VALID SteamInitTransactionCB pCallback,
	SA_PARAM_OP_VALID void *pUserData)
{
	SteamInitTransactionWrapper *pWrapper = NULL;
	UrlArgumentList *pList = NULL;
	U64 uOrderID = 0;
	U32 uRequestID = 0;
	const char *pFirstItem = NULL;

	PERFINFO_AUTO_START_FUNC();
	uOrderID = GenerateSteamOrderID(uAccountID);
	pList = CreateSteamAPIRequest(pProxy, STEAM_INITTXN, uOrderID, HTTPMETHOD_POST);

	if (!pList)
	{
		PERFINFO_AUTO_STOP();
		return PURCHASE_RESULT_STEAM_DISABLED;
	}

	// Populate the identifying info
	urlAddValuef(pList, "steamid", HTTPMETHOD_POST, "%"FORM_LL"u", uSteamID);

	// Populate the product details
	urlAddValuef(pList, "itemcount", HTTPMETHOD_POST, "%u", eaSize(&eaItems));
	urlAddValue(pList, "language", pLocCode, HTTPMETHOD_POST);
	urlAddValue(pList, "currency", pCurrency, HTTPMETHOD_POST);

	EARRAY_FOREACH_BEGIN(eaItems, i);
	{
		const TransactionItem *pItem = eaItems[i];
		const ProductContainer *pProduct = findProductByID(pItem->uProductID);
		const Money *pPrice = NULL;
		const char *pDescription = NULL;
		char key[32] = "";

		if(!pProduct)
		{
			urlDestroy(&pList);
			PERFINFO_AUTO_STOP();
			return PURCHASE_RESULT_INVALID_PRODUCT;
		}

		pPrice = getProductPrice(pProduct, pCurrency);
		if (!devassert(pPrice))
		{
			urlDestroy(&pList);
			PERFINFO_AUTO_STOP();
			return PURCHASE_RESULT_INVALID_CURRENCY;
		}

		sprintf(key, "itemid[%d]", i);
		urlAddValuef(pList, key, HTTPMETHOD_POST, "%d", pItem->uProductID);

		sprintf(key, "qty[%d]", i);
		urlAddValue(pList, key, "1", HTTPMETHOD_POST);

		sprintf(key, "amount[%d]", i);
		urlAddValuef(pList, key, HTTPMETHOD_POST, "%"FORM_LL"d", pPrice->Internal._internal_SubdividedAmount);

		pDescription = pProduct->pDescription;
		EARRAY_FOREACH_BEGIN(pProduct->ppLocalizedInfo, j);
		{
			ProductLocalizedInfo *pInfo = pProduct->ppLocalizedInfo[j];

			if(devassert(pInfo) && !stricmp(pInfo->pLanguageTag, pLocCode))
			{
				pDescription = pInfo->pDescription;
				break;
			}
		}
		EARRAY_FOREACH_END;

		if(!pDescription)
		{
			ErrorOrAlert("STEAM_NODESCRIPTION", "No description for product %s", pProduct->pName);
			urlDestroy(&pList);
			PERFINFO_AUTO_STOP();
			return PURCHASE_RESULT_UNKNOWN_ERROR;
		}

		if(!pFirstItem)
			pFirstItem = pDescription;

		sprintf(key, "description[%d]", i);
		urlAddValue(pList, key, pDescription, HTTPMETHOD_POST);

		if (pCategory)
		{
			sprintf(key, "category[%d]", i);
			urlAddValue(pList, key, pCategory, HTTPMETHOD_POST);
		}
	}
	EARRAY_FOREACH_END;

	urlAddValue(pList, "ipaddress", pIP, HTTPMETHOD_POST);

	if (bWebPurchase)
	{
		urlAddValue(pList, "usersession", "web", HTTPMETHOD_POST);
	}

	pWrapper = callocStruct(SteamInitTransactionWrapper);
	pWrapper->source.uAccountID = uAccountID;
	pWrapper->source.uSteamID = uSteamID;
	pWrapper->source.pProxy = strdup(pProxy);

	pWrapper->pCallback = pCallback;
	pWrapper->pUserData = pUserData;

	SteamLogTxnRequest(STEAM_INITTXN, uOrderID, uSteamID, uAccountID, pProxy,
		("ip", "%s", pIP ? pIP : "unknown")
		("category", "%s", pCategory)
		("currency", "%s", pCurrency)
		("locale", "%s", pLocCode)
		("web", "%d", !!bWebPurchase)
		("item1", "%s", pFirstItem));

	uRequestID = haSecureRequestLogged(NULL, billingGetStunnelHost(), billingGetSteamPort(), &pList, InitSteamTransaction_CB, InitSteamTransaction_TimeoutCB, guSteamRequestMaxBackoff, pWrapper);
	if (uRequestID)
		pWrapper->uRequestID = uRequestID;
	PERFINFO_AUTO_STOP();
	return PURCHASE_RESULT_PENDING;
}
	
static SteamTransactionWrapper **geaSteamTxnQueue = NULL;

static bool UpdateSteamTransaction(SA_PARAM_NN_VALID SteamTransactionWrapper *pWrapper, U64 uTransactionID)
{
	bool bContinue = false;

	PERFINFO_AUTO_START_FUNC();
	bContinue = pWrapper->pCallback(PURCHASE_RESULT_PENDING, pWrapper->pUserData, pWrapper->source.uOrderID, uTransactionID);
	PERFINFO_AUTO_STOP();

	return bContinue;
}

static void CompleteSteamTransaction(SA_PRE_NN_VALID SA_POST_P_FREE SteamTransactionWrapper *pWrapper, PurchaseResult eResult, U64 uTransactionID)
{
	PERFINFO_AUTO_START_FUNC();
	pWrapper->pCallback(eResult, pWrapper->pUserData, pWrapper->source.uOrderID, uTransactionID);
	DestroySteamTransactionSource(&pWrapper->source);
	free(pWrapper);
	PERFINFO_AUTO_STOP();
}

static void RetrySteamTransaction(SA_PARAM_NN_VALID SteamTransactionWrapper *pWrapper)
{
	eaPush(&geaSteamTxnQueue, pWrapper);
}

static void FinalizeSteamTransaction_CB(SA_PARAM_NN_STR const char *response, int len, int response_code, SA_PARAM_NN_VALID SteamTransactionWrapper *pWrapper)
{
	U64 uOrderID = 0;
	U64 uTransactionID = 0;
	char *pError = NULL;
	SteamTxnResult *pResult = NULL;

	PERFINFO_AUTO_START_FUNC();

	if (response_code != HTTP_OK)
	{
		ReportSteamOutcome(false, response, response_code, STEAM_FINALIZETXN);
		SteamLogTxnResponseHTTPError(STEAM_FINALIZETXN, pWrapper, response_code, response);
		RetrySteamTransaction(pWrapper);
		PERFINFO_AUTO_STOP();
		return;
	}
	
	pResult = ParserReadJSON(response, parse_SteamTxnResult, &pError);

	if (pError || !pResult || !pResult->response)
	{
		ReportSteamOutcome(false, response, response_code, STEAM_FINALIZETXN);
		SteamLogTxnResponseMalformed(STEAM_FINALIZETXN, pWrapper);
		SteamAlertMalformedResponse(response, STEAM_FINALIZETXN);
		RetrySteamTransaction(pWrapper);
		estrDestroy(&pError);
	}
	else if (pResult->response->error)
	{
		AccountInfo *pAccount = findAccountByID(pWrapper->source.uAccountID);
		SteamLogTxnResponseError(STEAM_FINALIZETXN, pWrapper, pResult->response->error->errorcode, pResult->response->error->errordesc);
		ReportSteamOutcome(false, response, response_code, STEAM_FINALIZETXN);

		if (devassert(pAccount))
		{
			accountLog(pAccount, "Error during Steam FinalizeTxn (%d: \"%s\")", pResult->response->error->errorcode, pResult->response->error->errordesc);
		}

		CompleteSteamTransaction(pWrapper, GetPurchaseResultFromSteamError(pResult->response->error), 0);
	}
	else
	{
		ReportSteamOutcome(true, response, response_code, STEAM_FINALIZETXN);

		if (pResult->response->params)
		{
			uOrderID = pResult->response->params->orderid;
			uTransactionID = pResult->response->params->transid;
		}

		// Finalize was successful
		SteamLogTxnResponseSuccess(STEAM_FINALIZETXN, pWrapper, ("transid", "%"FORM_LL"u", uTransactionID));
		CompleteSteamTransaction(pWrapper, PURCHASE_RESULT_COMMIT, uTransactionID);
	}

	StructDestroySafe(parse_SteamTxnResult, &pResult);

	PERFINFO_AUTO_STOP();
}

static void FinalizeSteamTransaction_TimeoutCB(SA_PARAM_NN_VALID SteamTransactionWrapper *pWrapper)
{
	PERFINFO_AUTO_START_FUNC();
	SteamLogTxnResponseTimeout(STEAM_FINALIZETXN, pWrapper);
	SteamAlertTimeoutResponse(STEAM_FINALIZETXN);
	RetrySteamTransaction(pWrapper);
	PERFINFO_AUTO_STOP();
}

static void FinalizeSteamTransaction(U64 uOrderID,
	SA_PARAM_NN_STR const char *pProxy,
	SA_PARAM_NN_VALID SteamTransactionWrapper *pWrapper)
{
	UrlArgumentList *pList = NULL;
	U32 uRequestID = 0;

	PERFINFO_AUTO_START_FUNC();
	pList = CreateSteamAPIRequest(pProxy, STEAM_FINALIZETXN, pWrapper->source.uOrderID, HTTPMETHOD_POST);

	if (!pList)
	{
		CompleteSteamTransaction(pWrapper, PURCHASE_RESULT_STEAM_DISABLED, 0);
		PERFINFO_AUTO_STOP();
		return;
	}

	SteamLogTxnRequest(STEAM_FINALIZETXN, uOrderID, pWrapper->source.uSteamID, pWrapper->source.uAccountID, pProxy);
	uRequestID = haSecureRequestLogged(NULL, billingGetStunnelHost(), billingGetSteamPort(), &pList, FinalizeSteamTransaction_CB, FinalizeSteamTransaction_TimeoutCB, guSteamRequestMaxBackoff, pWrapper);
	if (uRequestID)
		pWrapper->uRequestID = uRequestID;
	PERFINFO_AUTO_STOP();
}

static void QuerySteamTransaction_CB(SA_PARAM_NN_STR const char *response, int len, int response_code, SA_PARAM_NN_VALID SteamTransactionWrapper *pWrapper)
{
	U64 uOrderID = 0;
	U64 uTransactionID = 0;
	char *pError = NULL;
	SteamTxnResult *pResult = NULL;

	PERFINFO_AUTO_START_FUNC();

	if (response_code != HTTP_OK)
	{
		ReportSteamOutcome(false, response, response_code, STEAM_QUERYTXN);
		SteamLogTxnResponseHTTPError(STEAM_QUERYTXN, pWrapper, response_code, response);
		RetrySteamTransaction(pWrapper);
		PERFINFO_AUTO_STOP();
		return;
	}

	pResult = ParserReadJSON(response, parse_SteamTxnResult, &pError);

	if (pError || !pResult || !pResult->response)
	{
		ReportSteamOutcome(false, response, response_code, STEAM_QUERYTXN);
		SteamLogTxnResponseMalformed(STEAM_QUERYTXN, pWrapper);
		SteamAlertMalformedResponse(response, STEAM_QUERYTXN);
		RetrySteamTransaction(pWrapper);
		estrDestroy(&pError);
	}
	else if (pResult->response->error)
	{
		ReportSteamOutcome(false, response, response_code, STEAM_QUERYTXN);
		SteamLogTxnResponseError(STEAM_QUERYTXN, pWrapper, pResult->response->error->errorcode, pResult->response->error->errordesc);
		RetrySteamTransaction(pWrapper);
	}
	else if (!pResult->response->params)
	{
		ReportSteamOutcome(false, response, response_code, STEAM_QUERYTXN);
		SteamLogTxnResponseMalformed(STEAM_QUERYTXN, pWrapper);
		SteamAlertMalformedResponse(response, STEAM_QUERYTXN);
		RetrySteamTransaction(pWrapper);
	}
	else
	{
		ReportSteamOutcome(true, response, response_code, STEAM_QUERYTXN);

		uOrderID = pResult->response->params->orderid;
		uTransactionID = pResult->response->params->transid;

		SteamLogTxnResponseSuccess(STEAM_QUERYTXN, pWrapper,
			("transid", "%"FORM_LL"u", uTransactionID)
			("status", "%s", pResult->response->params->status));

		if (!stricmp_safe(pResult->response->params->status, "Approved"))
		{
			// If approved, we call the callback with another "pending" notification and then call FinalizeTxn
			if (UpdateSteamTransaction(pWrapper, uTransactionID))
			{
				FinalizeSteamTransaction(pWrapper->source.uOrderID, pWrapper->source.pProxy, pWrapper);
			}
			else
			{
				CompleteSteamTransaction(pWrapper, PURCHASE_RESULT_COMMIT_FAIL, uTransactionID);
			}
		}
		else if (!stricmp_safe(pResult->response->params->status, "Succeeded"))
		{
			// Transaction has already succeeded, so we should award the points
			CompleteSteamTransaction(pWrapper, PURCHASE_RESULT_COMMIT, uTransactionID);
		}
		else if (!stricmp_safe(pResult->response->params->status, "Failed") ||
					!stricmp_safe(pResult->response->params->status, "Refunded") ||
					!stricmp_safe(pResult->response->params->status, "Chargedback"))
		{
			CompleteSteamTransaction(pWrapper, PURCHASE_RESULT_ROLLBACK, uTransactionID);
		}
		else
		{
			AccountInfo *pAccount = findAccountByID(pWrapper->source.uAccountID);

			if (devassert(pAccount))
			{
				accountLog(pAccount, "QueryTxn returned unknown state: (order: %"FORM_LL"u, state: %s)", uOrderID, pResult->response->params->status);
				AssertOrAlert("STEAM_QUERYTXN_BADSTATE", "Steam QueryTxn request returned unexpected state \"%s\" for order ID %"
					FORM_LL "u / account ID %u. Transaction was rolled back.",
					pResult->response->params->status, uOrderID, pWrapper->source.uAccountID);
			}

			CompleteSteamTransaction(pWrapper, PURCHASE_RESULT_ROLLBACK, uTransactionID);
		}
	}

	StructDestroySafe(parse_SteamTxnResult, &pResult);

	PERFINFO_AUTO_STOP();
}

static void QuerySteamTransaction_TimeoutCB(SA_PARAM_NN_VALID SteamTransactionWrapper *pWrapper)
{
	PERFINFO_AUTO_START_FUNC();
	SteamAlertTimeoutResponse(STEAM_QUERYTXN);
	SteamLogTxnResponseTimeout(STEAM_QUERYTXN, pWrapper);
	RetrySteamTransaction(pWrapper);
	PERFINFO_AUTO_STOP();
}

static void QuerySteamTransaction(U64 uOrderID,
	SA_PARAM_NN_STR const char *pProxy,
	SA_PARAM_NN_VALID SteamTransactionWrapper *pWrapper)
{
	UrlArgumentList *pList = NULL;
	U32 uRequestID = 0;

	PERFINFO_AUTO_START_FUNC();
	pList = CreateSteamAPIRequest(pProxy, STEAM_QUERYTXN, pWrapper->source.uOrderID, HTTPMETHOD_GET);

	if (!pList)
	{
		CompleteSteamTransaction(pWrapper, PURCHASE_RESULT_STEAM_DISABLED, 0);
		PERFINFO_AUTO_STOP();
		return;
	}

	SteamLogTxnRequest(STEAM_QUERYTXN, uOrderID, pWrapper->source.uSteamID, pWrapper->source.uAccountID, pProxy);
	uRequestID = haSecureRequestLogged(NULL, billingGetStunnelHost(), billingGetSteamPort(), &pList, QuerySteamTransaction_CB, QuerySteamTransaction_TimeoutCB, guSteamRequestMaxBackoff, pWrapper);
	if (uRequestID)
		pWrapper->uRequestID = uRequestID;
	PERFINFO_AUTO_STOP();
}

void ProcessSteamQueue(void)
{
	U32 uNow = timeSecondsSince2000();

	PERFINFO_AUTO_START_FUNC();

	EARRAY_FOREACH_REVERSE_BEGIN(geaSteamTxnQueue, i);
	{
		SteamTransactionWrapper *pWrapper = geaSteamTxnQueue[i];

		if (!devassert(pWrapper))
		{
			eaRemoveFast(&geaSteamTxnQueue, i);
			continue;
		}

		if (uNow < pWrapper->uNextAttemptTime)
		{
			continue;
		}

		eaRemoveFast(&geaSteamTxnQueue, i);
		pWrapper->uNextAttemptTime = uNow + pWrapper->uNextBackoffAmount;

		if (pWrapper->uNextBackoffAmount < guSteamRequestMaxBackoff)
			pWrapper->uNextBackoffAmount *= 2;
		if (pWrapper->uNextBackoffAmount > guSteamRequestMaxBackoff)
			pWrapper->uNextBackoffAmount = guSteamRequestMaxBackoff;

		// Inform callback that we're pending, then call QueryTxn
		// But don't extend the key locks! Only do that when we want to finalize
		QuerySteamTransaction(pWrapper->source.uOrderID, pWrapper->source.pProxy, pWrapper);

		// Only process one per frame, max
		break;
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP();
}

void AddOrderToSteamQueue(U32 uAccountID,
	U64 uOrderID,
	SA_PARAM_NN_STR const char *pProxy,
	SA_PARAM_NN_VALID SteamTransactionCB pCallback,
	SA_PARAM_OP_VALID void *pUserData)
{
	SteamTransactionWrapper *pWrapper = NULL;

	pWrapper = callocStruct(SteamTransactionWrapper);
	pWrapper->source.uAccountID = uAccountID;
	pWrapper->source.uOrderID = uOrderID;
	pWrapper->source.pProxy = strdup(pProxy);

	pWrapper->pCallback = pCallback;
	pWrapper->pUserData = pUserData;

	pWrapper->uNextAttemptTime = timeSecondsSince2000();
	pWrapper->uNextBackoffAmount = 1;

	// Technically this isn't a retry, but HUSH
	RetrySteamTransaction(pWrapper);
}

static void RefundSteamTransaction_CB(SA_PARAM_NN_STR const char *response, int len, int response_code, SA_PRE_NN_VALID SA_POST_P_FREE SteamRefundTransactionWrapper *pWrapper)
{
	bool bSuccess = false;
	U64 uOrderID = 0;
	U64 uTransactionID = 0;

	PERFINFO_AUTO_START_FUNC();

	if (response_code != HTTP_OK)
	{
		ReportSteamOutcome(false, response, response_code, STEAM_REFUNDTXN);
		SteamLogTxnResponseHTTPError(STEAM_REFUNDTXN, pWrapper, response_code, response);
	}
	else
	{
		char *pError = NULL;
		SteamTxnResult *pResult = ParserReadJSON(response, parse_SteamTxnResult, &pError);

		if (pError || !pResult || !pResult->response)
		{
			ReportSteamOutcome(false, response, response_code, STEAM_REFUNDTXN);
			SteamLogTxnResponseMalformed(STEAM_REFUNDTXN, pWrapper);
			SteamAlertMalformedResponse(response, STEAM_REFUNDTXN);
		}
		else if (pResult->response->error)
		{
			AccountInfo *pAccount = findAccountByID(pWrapper->source.uAccountID);
			ReportSteamOutcome(false, response, response_code, STEAM_REFUNDTXN);
			SteamLogTxnResponseError(STEAM_REFUNDTXN, pWrapper, pResult->response->error->errorcode, pResult->response->error->errordesc);

			if (pAccount)
			{
				accountLog(pAccount, "Error during Steam RefundTxn (%d: \"%s\")", pResult->response->error->errorcode, pResult->response->error->errordesc);
			}
		}
		else if (!pResult->response->params)
		{
			ReportSteamOutcome(false, response, response_code, STEAM_REFUNDTXN);
			SteamLogTxnResponseMalformed(STEAM_REFUNDTXN, pWrapper);
			SteamAlertMalformedResponse(response, STEAM_REFUNDTXN);
		}
		else
		{
			ReportSteamOutcome(true, response, response_code, STEAM_REFUNDTXN);
			bSuccess = true;
			uOrderID = pResult->response->params->orderid;
			uTransactionID = pResult->response->params->transid;
			SteamLogTxnResponseSuccess(STEAM_REFUNDTXN, pWrapper, ("transid", "%"FORM_LL"u", uTransactionID));
		}

		estrDestroy(&pError);
		StructDestroySafe(parse_SteamTxnResult, &pResult);
	}

	pWrapper->pCallback(bSuccess, pWrapper->pUserData, uOrderID, uTransactionID);
	DestroySteamTransactionSource(&pWrapper->source);
	free(pWrapper);
	PERFINFO_AUTO_STOP();
}

static void RefundSteamTransaction_TimeoutCB(SA_PRE_NN_VALID SA_POST_P_FREE SteamRefundTransactionWrapper *pWrapper)
{
	SteamLogTxnResponseTimeout(STEAM_REFUNDTXN, pWrapper);
	pWrapper->pCallback(false, pWrapper->pUserData, pWrapper->source.uOrderID, 0);
	DestroySteamTransactionSource(&pWrapper->source);
	free(pWrapper);
}

void RefundSteamTransaction(U32 uAccountID,
	U64 uOrderID,
	SA_PARAM_NN_STR const char *pProxy,
	SA_PARAM_NN_VALID SteamRefundTransactionCB pCallback,
	SA_PARAM_OP_VALID void *pUserData)
{
	SteamRefundTransactionWrapper *pWrapper = NULL;
	UrlArgumentList *pList = NULL;
	U32 uRequestID = 0;

	PERFINFO_AUTO_START_FUNC();
	pList = CreateSteamAPIRequest(pProxy, STEAM_REFUNDTXN, uOrderID, HTTPMETHOD_POST);

	if (!pList)
	{
		pCallback(false, pUserData, uOrderID, 0);
		PERFINFO_AUTO_STOP();
		return;
	}

	pWrapper = callocStruct(SteamRefundTransactionWrapper);
	pWrapper->source.uAccountID = uAccountID;
	pWrapper->source.uOrderID = uOrderID;
	pWrapper->source.pProxy = strdup(pProxy);

	pWrapper->pCallback = pCallback;
	pWrapper->pUserData = pUserData;

	SteamLogTxnRequest(STEAM_REFUNDTXN, uOrderID, (S64)0, uAccountID, pProxy);
	uRequestID = haSecureRequestLogged(NULL, billingGetStunnelHost(), billingGetSteamPort(), &pList, RefundSteamTransaction_CB, RefundSteamTransaction_TimeoutCB, guSteamRequestMaxBackoff, pWrapper);
	if (uRequestID)
		pWrapper->uRequestID = uRequestID;
	PERFINFO_AUTO_STOP();
}

#include "Steam_c_ast.c"